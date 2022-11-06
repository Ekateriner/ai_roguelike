#include "aiLibrary.h"
#include "ecsTypes.h"
#include "aiUtils.h"
#include "math.h"
#include "raylib.h"
#include "blackboard.h"
#include <ranges>
#include <random>

static auto& get_engine() {
  static std::random_device rd{};
  static std::default_random_engine engine(rd());

  return engine;
}

struct CompoundNode : public BehNode
{
  std::vector<BehNode*> nodes;

  virtual ~CompoundNode()
  {
    for (BehNode *node : nodes)
      delete node;
    nodes.clear();
  }

  CompoundNode &pushNode(BehNode *node)
  {
    nodes.push_back(node);
    return *this;
  }
};

struct Sequence : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_SUCCESS)
        return res;
    }
    return BEH_SUCCESS;
  }
};

struct Selector : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_FAIL)
        return res;
    }
    return BEH_FAIL;
  }
};

struct UtilitySelector : public CompoundNode
{
  std::vector<utility_function> utilities;
  bool soft_max = false;
  std::vector<float> inertia;

  UtilitySelector(bool use_soft_max = false) : soft_max(use_soft_max) {}

  UtilitySelector &pushNode(BehNode *node, const utility_function& func)
  {
    nodes.push_back(node);
    utilities.push_back(func);
    inertia.push_back(0.);
    return *this;
  }

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    if (soft_max) {
      std::vector<float> utilityScores;
      float sum = 0;
      for (auto i : std::views::iota(size_t(0), utilities.size()))
      {
        const float utilityScore = exp(utilities[i](bb)) + inertia[i];
        sum += utilityScore;
        utilityScores.push_back(utilityScore);
      }
      for (auto _ : std::views::iota(size_t(0), nodes.size())) {
        // generate
        std::uniform_real_distribution<float> dist(0, sum);
        float proba = dist(get_engine());

        size_t nodeIdx = 0;
        for(; proba > 0; nodeIdx++) {
          proba -= utilityScores[nodeIdx];
        }

        BehResult res = nodes[nodeIdx]->update(ecs, entity, bb);
        if (res != BEH_FAIL) {
          update_inertia(nodeIdx);
          return res;
        }
        
        sum -= utilityScores[nodeIdx];
        utilityScores[nodeIdx] = 0;
      }
    }
    else {
      std::vector<std::pair<float, size_t>> utilityScores;
      for (size_t i = 0; i < utilities.size(); ++i)
      {
        const float utilityScore = utilities[i](bb) + inertia[i];
        utilityScores.push_back(std::make_pair(utilityScore, i));
      }
      std::sort(utilityScores.begin(), utilityScores.end(), [](auto &lhs, auto &rhs)
      {
        return lhs.first > rhs.first;
      });
      for (const std::pair<float, size_t> &node : utilityScores)
      {
        size_t nodeIdx = node.second;
        BehResult res = nodes[nodeIdx]->update(ecs, entity, bb);
        if (res != BEH_FAIL) {
          update_inertia(nodeIdx);
          return res;
        }
      }
    }

    return BEH_FAIL;
  }

private:
  float inertia_step = 1.0;
  float cooldown = 0.1;
  void update_inertia(size_t nodeIdx) {
    float prev = inertia[nodeIdx];
    std::ranges::fill(inertia, 0);
    if (prev > 0)
      inertia[nodeIdx] = prev - cooldown;
    else
      inertia[nodeIdx] = prev + inertia_step;
  }
};

struct MoveToEntity : public BehNode
{
  size_t entityBb = size_t(-1); // wraps to 0xff...
  MoveToEntity(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      targetEntity.get([&](const Position &target_pos)
      {
        if (pos != target_pos)
        {
          a.action = move_towards(pos, target_pos);
          res = BEH_RUNNING;
        }
        else
          res = BEH_SUCCESS;
      });
    });
    return res;
  }
};

struct MoveToPosition : public BehNode
{
  size_t targetBb = size_t(-1); // wraps to 0xff...
  MoveToPosition(flecs::entity entity, const char *bb_name)
  {
    targetBb = reg_entity_blackboard_var<Position>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      Position target_pos = bb.get<Position>(targetBb);
      if (pos != target_pos)
      {
        a.action = move_towards(pos, target_pos);
        res = BEH_RUNNING;
      }
      else
        res = BEH_SUCCESS;
    });
    return res;
  }
};

struct IsLowHp : public BehNode
{
  float threshold = 0.f;
  IsLowHp(float thres) : threshold(thres) {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &) override
  {
    BehResult res = BEH_SUCCESS;
    entity.get([&](const Hitpoints &hp)
    {
      res = hp.hitpoints < threshold ? BEH_SUCCESS : BEH_FAIL;
    });
    return res;
  }
};

struct FindEnemy : public BehNode
{
  size_t entityBb = size_t(-1);
  float distance = 0;
  FindEnemy(flecs::entity entity, float in_dist, const char *bb_name) : distance(in_dist)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    static auto enemiesQuery = ecs.query<const Position, const Team>();
    entity.set([&](const Position &pos, const Team &t)
    {
      flecs::entity closestEnemy;
      float closestDist = FLT_MAX;
      enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
      {
        if (t.team == et.team)
          return;
        float curDist = dist(epos, pos);
        if (curDist < closestDist)
        {
          closestDist = curDist;
          closestEnemy = enemy;
        }
      });
      if (ecs.is_valid(closestEnemy) && closestDist <= distance)
      {
        bb.set<flecs::entity>(entityBb, closestEnemy);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct ClosestEnemyTo : public BehNode
{
  size_t enemyBb = size_t(-1);
  size_t positionBb = size_t(-1);

  ClosestEnemyTo(flecs::entity entity, const char *position_bb_name, const char *enemy_bb_name)
  {
    positionBb = reg_entity_blackboard_var<flecs::entity>(entity, position_bb_name);
    enemyBb = reg_entity_blackboard_var<flecs::entity>(entity, enemy_bb_name);
  }

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    Position pos = bb.get<Position>(positionBb);
    static auto enemiesQuery = ecs.query<const Position, const Team>();
    entity.set([&](const Team &t)
    {
      flecs::entity closestEnemy;
      float closestDist = FLT_MAX;
      enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
      {
        if (t.team == et.team)
          return;
        float curDist = dist(epos, pos);
        if (curDist < closestDist)
        {
          closestDist = curDist;
          closestEnemy = enemy;
        }
      });
      if (ecs.is_valid(closestEnemy))
      {
        bb.set<flecs::entity>(enemyBb, closestEnemy);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct Flee : public BehNode
{
  size_t entityBb = size_t(-1);
  Flee(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      targetEntity.get([&](const Position &target_pos)
      {
        a.action = inverse_move(move_towards(pos, target_pos));
      });
    });
    return res;
  }
};

struct Patrol : public BehNode
{
  size_t pposBb = size_t(-1);
  float patrolDist = 1.f;
  Patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
    : patrolDist(patrol_dist)
  {
    pposBb = reg_entity_blackboard_var<Position>(entity, bb_name);
    entity.set([&](Blackboard &bb, const Position &pos)
    {
      bb.set<Position>(pposBb, pos);
    });
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      Position patrolPos = bb.get<Position>(pposBb);
      if (dist(pos, patrolPos) > patrolDist)
        a.action = move_towards(pos, patrolPos);
      else
        a.action = GetRandomValue(EA_MOVE_START, EA_MOVE_END - 1); // do a random walk
    });
    return res;
  }
};

struct MoveRandom : public BehNode
{
  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      a.action = GetRandomValue(EA_MOVE_START, EA_MOVE_END - 1); // do a random walk
    });
    return res;
  }
};

struct PatchUp : public BehNode
{
  float hpThreshold = 100.f;
  PatchUp(float threshold) : hpThreshold(threshold) {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &) override
  {
    BehResult res = BEH_SUCCESS;
    entity.set([&](Action &a, Hitpoints &hp)
    {
      if (hp.hitpoints >= hpThreshold)
        return;
      res = BEH_RUNNING;
      a.action = EA_HEAL_SELF;
    });
    return res;
  }
};



BehNode *sequence(const std::vector<BehNode*> &nodes)
{
  Sequence *seq = new Sequence;
  for (BehNode *node : nodes)
    seq->pushNode(node);
  return seq;
}

BehNode *selector(const std::vector<BehNode*> &nodes)
{
  Selector *sel = new Selector;
  for (BehNode *node : nodes)
    sel->pushNode(node);
  return sel;
}

BehNode *utility_selector(const std::vector<std::pair<BehNode*, utility_function>> &nodes)
{
  UtilitySelector *usel = new UtilitySelector;
  for (auto& [node, util] : nodes) {
    usel->pushNode(node, util);
  }
  return usel;
}

BehNode *move_to_entity(flecs::entity entity, const char *bb_name)
{
  return new MoveToEntity(entity, bb_name);
}

BehNode *move_to_position(flecs::entity entity, const char *bb_name) {
  return new MoveToPosition(entity, bb_name);
}

BehNode *is_low_hp(float thres)
{
  return new IsLowHp(thres);
}

BehNode *find_enemy(flecs::entity entity, float dist, const char *bb_name)
{
  return new FindEnemy(entity, dist, bb_name);
}

BehNode *closest_enemy_to(flecs::entity entity, const char *position_bb_name, const char *enemy_bb_name)
{
  return new ClosestEnemyTo(entity, position_bb_name, enemy_bb_name);
}

BehNode *flee(flecs::entity entity, const char *bb_name)
{
  return new Flee(entity, bb_name);
}

BehNode *patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
{
  return new Patrol(entity, patrol_dist, bb_name);
}

BehNode *random_move()
{
  return new MoveRandom();
}

BehNode *patch_up(float thres)
{
  return new PatchUp(thres);
}


