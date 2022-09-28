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
  if (deltaX == 0 && deltaY == 0)
    return EA_NOP;
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

class MoveToState : public State
{
  Position target_pos;
public:
  MoveToState(Position tgt) : target_pos(tgt) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.set([&](const Position &pos, Action &a, Activity& activ)
    {
      a.action = move_towards(pos, target_pos);
      activ.state = A_NOP;
    });
  }
};

class SomeActivityState : public State
{
  float duration;
  Actions action;
public:
  SomeActivityState(float dur, Actions a) : duration(dur), action(a) {}
  void enter() const override {}
  void exit() const override {}
  void act(float dt, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.set([&](const Position &pos, Action &a, Activity &activ)
    {
      if (activ.state == A_NOP) {
        activ.state = A_START;
      }
      else if (activ.state == A_START) {
        activ.last_time = duration;
        a.action = action;
        activ.state = A_PROCESS;
      }
      else if (activ.state == A_PROCESS) {
        activ.last_time -= dt;
        a.action = action;
        if (activ.last_time <= 0.0) {
          activ.state = A_END;
          a.action = EA_NOP;
        }
      }
    });
  }
};

//---------------------------------------transitions---------------------------------------


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

class NearTransition : public StateTransition
{
  Position position;
  float triggerDist;
public:
  NearTransition(Position pos, float dist) : position(pos), triggerDist(dist) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    static auto enemiesQuery = ecs.query<const Position, const Team>();
    bool is_near = false;
    entity.get([&](const Position &pos)
    {
      float curDist = dist(position, pos);
      is_near |= curDist <= triggerDist;
    });
    return is_near;
  }
};

class ActivityEndTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool is_end = false;
    entity.get([&](const Activity &activ)
    {
      is_end |= activ.state == A_END;
    });
    return is_end;
  }
};

class NeedTalkTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool is_talk = false;
    entity.get([&](const Action &a, const Activity &activ)
    {
      is_talk |= activ.state == A_PROCESS && (a.action == EA_BUY || a.action == EA_SELL);
    });
    return is_talk;
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

class TimeTransition : public StateTransition
{
  float lhs;
  float rhs;
public:
  TimeTransition(float in_lhs, float in_rhs) : lhs(in_lhs), rhs(in_rhs) {}
  
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    static auto globalTime = ecs.query<Time>();
    float cur_time = 0.f;
    globalTime.each([&](flecs::entity /*e*/, Time &gtime){
      cur_time = gtime.time;
    });
    cur_time = cur_time - 24.0f * floor(cur_time / 72.0f);

    return (cur_time >= lhs && cur_time <= rhs) || \
           (lhs > rhs && cur_time >= lhs && cur_time <= rhs + 72.0f) || \
           (lhs > rhs && cur_time >= lhs - 72.0f && cur_time <= rhs);
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

class OrTransition : public StateTransition
{
  const StateTransition *lhs; // we own it
  const StateTransition *rhs; // we own it
public:
  OrTransition(const StateTransition *in_lhs, const StateTransition *in_rhs) : lhs(in_lhs), rhs(in_rhs) {}
  ~OrTransition() override
  {
    delete lhs;
    delete rhs;
  }

  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return lhs->isAvailable(ecs, entity) || rhs->isAvailable(ecs, entity);
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

State *create_move_to_state(Position pos)
{
  return new MoveToState(pos);
}

State *create_activity_state(float dur, Actions a)
{
  return new SomeActivityState(dur, a);
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

StateTransition *create_near_transition(Position pos, float dist)
{
  return new NearTransition(pos, dist);
}

StateTransition *create_activity_end_transition()
{
  return new ActivityEndTransition();
}

StateTransition *create_time_transition(float lhs, float rhs) {
  return new TimeTransition(lhs, rhs);
}

StateTransition *create_need_talk_transition() {
  return new NeedTalkTransition();
}

StateTransition *create_negate_transition(StateTransition *in)
{
  return new NegateTransition(in);
}
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs)
{
  return new AndTransition(lhs, rhs);
}
StateTransition *create_or_transition(StateTransition *lhs, StateTransition *rhs)
{
  return new OrTransition(lhs, rhs);
}