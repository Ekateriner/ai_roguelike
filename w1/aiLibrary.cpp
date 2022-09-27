#include "aiLibrary.h"
#include <flecs.h>
#include "ecsTypes.h"
#include <bx/rng.h>
#include <cfloat>
#include <cmath>

static bx::RngShr3 rng;

class AttackEnemyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &/*ecs*/, flecs::entity /*entity*/) const override {}
};

template<typename T>
T sqr(T a){ return a*a; }

template<typename T, typename U>
static float dist_sq(const T &lhs, const U &rhs) { return float(sqr(lhs.x - rhs.x) + sqr(lhs.y - rhs.y)); }

template<typename T, typename U>
static float dist(const T &lhs, const U &rhs) { return sqrtf(dist_sq(lhs, rhs)); }

template<typename T, typename U>
static int move_towards(const T &from, const U &to)
{
  int deltaX = to.x - from.x;
  int deltaY = to.y - from.y;
  if (abs(deltaX) > abs(deltaY))
    return deltaX > 0 ? EA_MOVE_RIGHT : EA_MOVE_LEFT;
  return deltaY > 0 ? EA_MOVE_UP : EA_MOVE_DOWN;
}

static int inverse_move(int move)
{
  return move == EA_MOVE_LEFT ? EA_MOVE_RIGHT :
         move == EA_MOVE_RIGHT ? EA_MOVE_LEFT :
         move == EA_MOVE_UP ? EA_MOVE_DOWN :
         move == EA_MOVE_DOWN ? EA_MOVE_UP : move;
}


template<typename Callable>
static void on_closest_enemy_pos(flecs::world &ecs, flecs::entity entity, Callable c)
{
  static auto enemiesQuery = ecs.query<const Position, const Team>();
  entity.set([&](const Position &pos, const Team &t, Action &a)
  {
    flecs::entity closestEnemy;
    float closestDist = FLT_MAX;
    Position closestPos;
    enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
    {
      if (t.team == et.team)
        return;
      float curDist = dist(epos, pos);
      if (curDist < closestDist)
      {
        closestDist = curDist;
        closestPos = epos;
        closestEnemy = enemy;
      }
    });
    if (ecs.is_valid(closestEnemy))
      c(a, pos, closestPos);
  });
}

template<typename Callable>
static void on_closest_ally_pos(flecs::world &ecs, flecs::entity entity, Callable c)
{
  static auto enemiesQuery = ecs.query<const Position, const Team>();
  entity.set([&](const Position &pos, const Team &t, Action &a)
  {
    flecs::entity closestAlly;
    float closestDist = FLT_MAX;
    Position closestPos;
    enemiesQuery.each([&](flecs::entity ally, const Position &apos, const Team &at)
    {
      if (t.team != at.team)
        return;
      float curDist = dist(apos, pos);
      if (curDist < closestDist && curDist > FLT_EPSILON)
      {
        closestDist = curDist;
        closestPos = apos;
        closestAlly = ally;
      }
    });
    if (ecs.is_valid(closestAlly))
      c(a, pos, closestPos);
  });
}

class MoveToEnemyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    on_closest_enemy_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &enemy_pos)
    {
      a.action = move_towards(pos, enemy_pos);
    });
  }
};

class MoveToAllyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    on_closest_ally_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &ally_pos)
    {
      a.action = move_towards(pos, ally_pos);
    });
  }
};

// class MoveToPlayerState : public State
// {
// public:
//   void enter() const override {}
//   void exit() const override {}
//   void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
//   {
//     on_closest_ally_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &ally_pos)
//     {
//       a.action = move_towards(pos, ally_pos);
//     });
//   }
// };

class FleeFromEnemyState : public State
{
public:
  FleeFromEnemyState() {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    on_closest_enemy_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &enemy_pos)
    {
      a.action = inverse_move(move_towards(pos, enemy_pos));
    });
  }
};

class PatrolState : public State
{
  float patrolDist;
public:
  PatrolState(float dist) : patrolDist(dist) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.set([&](const Position &pos, const PatrolPos &ppos, Action &a)
    {
      if (dist(pos, ppos) > patrolDist)
        a.action = move_towards(pos, ppos); // do a recovery walk
      else
      {
        // do a random walk
        a.action = EA_MOVE_START + (rng.gen() % (EA_MOVE_END - EA_MOVE_START));
      }
    });
  }
};

class NopState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override {}
};

class HealState : public State
{
  float healPower;
public:
  HealState(float power) : healPower(power) {} 
  void enter() const override {}
  void exit() const override {}
  void act(float /*dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.remove<Targets>();
    static auto globalTime = ecs.query<Time>();
    float cur_time = 0.f;
    globalTime.each([&](flecs::entity /*e*/, Time &gtime){
      cur_time = gtime.time;
    });
    
    entity.set([&](Action &a, Ability &ability)
    {
      a.action = EA_HEAL;
      ability.power = healPower;
      ability.lastAbilityUsage = cur_time;
    });
    entity.add<Targets>(entity);
  }
};

class AllyHealState : public State
{
  float healPower;
  float healDist;
public:
  AllyHealState(float power, float dist) : healPower(power), healDist(dist) {} 
  void enter() const override {}
  void exit() const override {}
  void act(float /*dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.remove<Targets>();
    static auto globalTime = ecs.query<Time>();
    float cur_time = 0.f;
    globalTime.each([&](flecs::entity /*e*/, Time &gtime){
      cur_time = gtime.time;
    });
    
    static auto alliesQuery = ecs.query<const Position, const Team>();
    entity.set([&](const Position &pos, const Team &t, Action &a, Ability &ability)
    {
      alliesQuery.each([&](flecs::entity ally, const Position &apos, const Team &at)
      {
        if (t.team != at.team)
          return;
        float curDist = dist(apos, pos);
        if (curDist <= healDist && curDist > FLT_EPSILON) {
          a.action = EA_HEAL;
          ability.power = healPower;
          ability.lastAbilityUsage = cur_time;
          entity.add<Targets>(ally);
        }
      });
    });
  }
};

class EnemyAvailableTransition : public StateTransition
{
  float triggerDist;
public:
  EnemyAvailableTransition(float in_dist) : triggerDist(in_dist) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    static auto enemiesQuery = ecs.query<const Position, const Team>();
    bool enemiesFound = false;
    entity.get([&](const Position &pos, const Team &t)
    {
      enemiesQuery.each([&](flecs::entity /*enemy*/, const Position &epos, const Team &et)
      {
        if (t.team == et.team)
          return;
        float curDist = dist(epos, pos);
        enemiesFound |= curDist <= triggerDist;
      });
    });
    return enemiesFound;
  }
};

class AllyAvailableTransition : public StateTransition
{
  float triggerDist;
public:
  AllyAvailableTransition(float in_dist) : triggerDist(in_dist) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    static auto alliesQuery = ecs.query<const Position, const Team>();
    bool alliesFound = false;
    entity.get([&](const Position &pos, const Team &t)
    {
      alliesQuery.each([&](flecs::entity /*ally*/, const Position &apos, const Team &at)
      {
        if (t.team != at.team)
          return;
        float curDist = dist(apos, pos);
        alliesFound |= (curDist <= triggerDist && curDist > FLT_EPSILON);
      });
    });
    return alliesFound;
  }
};

class HitpointsLessThanTransition : public StateTransition
{
  float threshold;
public:
  HitpointsLessThanTransition(float in_thres) : threshold(in_thres) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool hitpointsThresholdReached = false;
    entity.get([&](const Hitpoints &hp)
    {
      hitpointsThresholdReached |= hp.hitpoints < threshold;
    });
    return hitpointsThresholdReached;
  }
};

class AllyHitpointsLessThanTransition : public StateTransition
{
  float threshold;
  float triggerDist;
public:
  AllyHitpointsLessThanTransition(float in_thres, float in_dist) : threshold(in_thres), triggerDist(in_dist) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool hitpointsThresholdReached = false;
    static auto alliesQuery = ecs.query<const Position, const Team, const Hitpoints>();
    entity.get([&](const Position &pos, const Team &t)
    {
      alliesQuery.each([&](flecs::entity /*ally*/, const Position &apos, const Team &at, const Hitpoints &ahp)
      {
        if (t.team != at.team)
          return;
        float curDist = dist(apos, pos);
        hitpointsThresholdReached |= (curDist <= triggerDist && curDist > FLT_EPSILON && ahp.hitpoints < threshold);
      });
    });
    return hitpointsThresholdReached;
  }
};

class AbilityAvailableTransition : public StateTransition
{
  float cooldown;
public:
  AbilityAvailableTransition(float cd) : cooldown(cd) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool abilityAvailable = false;
    
    static auto globalTime = ecs.query<Time>();
    float cur_time = 0.f;
    globalTime.each([&](flecs::entity /*e*/, Time &gtime){
      cur_time = gtime.time;
    });

    entity.get([&](const Ability &ability)
    {
      abilityAvailable |= (ability.lastAbilityUsage + cooldown) < cur_time;
    });
    return abilityAvailable;
  }
};

class EnemyReachableTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return false;
  }
};

class NegateTransition : public StateTransition
{
  const StateTransition *transition; // we own it
public:
  NegateTransition(const StateTransition *in_trans) : transition(in_trans) {}
  ~NegateTransition() override { delete transition; }

  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return !transition->isAvailable(ecs, entity);
  }
};

class AndTransition : public StateTransition
{
  const StateTransition *lhs; // we own it
  const StateTransition *rhs; // we own it
public:
  AndTransition(const StateTransition *in_lhs, const StateTransition *in_rhs) : lhs(in_lhs), rhs(in_rhs) {}
  ~AndTransition() override
  {
    delete lhs;
    delete rhs;
  }

  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return lhs->isAvailable(ecs, entity) && rhs->isAvailable(ecs, entity);
  }
};


// states
State *create_attack_enemy_state()
{
  return new AttackEnemyState();
}
State *create_move_to_enemy_state()
{
  return new MoveToEnemyState();
}
State *create_move_to_ally_state()
{
  return new MoveToAllyState();
}
// State *create_move_to_player_state()
// {
//   return new MoveToPlayerState();
// }

State *create_flee_from_enemy_state()
{
  return new FleeFromEnemyState();
}


State *create_patrol_state(float patrol_dist)
{
  return new PatrolState(patrol_dist);
}

State *create_heal_state(float power)
{
  return new HealState(power);
}

State *create_ally_heal_state(float power, float dist)
{
  return new AllyHealState(power, dist);
}

State *create_nop_state()
{
  return new NopState();
}

// transitions
StateTransition *create_enemy_available_transition(float dist)
{
  return new EnemyAvailableTransition(dist);
}

StateTransition *create_ally_available_transition(float dist)
{
  return new AllyAvailableTransition(dist);
}

StateTransition *create_enemy_reachable_transition()
{
  return new EnemyReachableTransition();
}

StateTransition *create_hitpoints_less_than_transition(float thres)
{
  return new HitpointsLessThanTransition(thres);
}

StateTransition *create_ally_hitpoints_less_than_transition(float thres, float dist)
{
  return new AllyHitpointsLessThanTransition(thres, dist);
}

StateTransition *create_ability_available_transition(float cd)
{
  return new AbilityAvailableTransition(cd);
}

StateTransition *create_negate_transition(StateTransition *in)
{
  return new NegateTransition(in);
}
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs)
{
  return new AndTransition(lhs, rhs);
}

