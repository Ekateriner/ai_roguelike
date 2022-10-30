#include "aiLibrary.h"
#include "ecsTypes.h"
#include "aiUtils.h"
#include "math.h"
#include "raylib.h"
#include "blackboard.h"
#include <map>

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

  void react(Event coming_evt) override {
    for (BehNode *node : nodes)
    {
      node->react(coming_evt);
    }
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

struct NotNode : public BehNode
{
  BehNode* node;

  NotNode(BehNode *_node) : node(_node) {}

  virtual ~NotNode()
  {
    delete node;
  }

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = node->update(ecs, entity, bb);
    if (res == BEH_FAIL)
      return BEH_SUCCESS;
    else if (res == BEH_SUCCESS) 
      return BEH_FAIL;
    throw "Wrong node result";
  }

  void react(Event coming_evt) override {
    node->react(coming_evt);
  }
};

struct Parallel : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_RUNNING)
        return res;    
    }
    return BEH_RUNNING;
  }
};

struct OrNode : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult total_res = BEH_FAIL;
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res == BEH_SUCCESS)
        return BEH_SUCCESS;
      else if (res == BEH_RUNNING)
        throw "Wrong node result";
    }
    return BEH_FAIL;
  }
};

struct AndNode : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res == BEH_FAIL)
        return BEH_FAIL;
      else if (res == BEH_RUNNING)
        throw "Wrong node result";
    }
    return BEH_SUCCESS;
  }
};

struct ReactNode : public BehNode
{
  std::map<Event, BehNode*> reactions;
  BehNode* node;
  Event proccessed_evt = NoEvent;
  
  ReactNode(BehNode *_node) : node(_node) {}

  ReactNode &addReaction(Event evt, BehNode *node)
  {
    reactions[evt] = node;
    return *this;
  } 

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    // if (proccessed_evt == NoEvent)
    //   return node->update(ecs, entity, bb);
    // else
    // {
    //   BehResult res = reactions[proccessed_evt]->update(ecs, entity, bb);
    //   if (res == BEH_FAIL)
    //     proccessed_evt = NoEvent;
    //   return res;
    // }
    return node->update(ecs, entity, bb);
  }

  void react(Event coming_evt) override {
    for (auto [event, _] : reactions) {
      if (event == proccessed_evt)
        break;
      if (event == coming_evt) {
        proccessed_evt = coming_evt;
        break;
      }
    }

    node->react(coming_evt);
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

  void react(Event coming_evt) override {}
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

  void react(Event coming_evt) override {}
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
      if (ecs.is_valid(closestEnemy) && closestDist <= distance)
      {
        bb.set<flecs::entity>(entityBb, closestEnemy);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }

  void react(Event coming_evt) override {}
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

  void react(Event coming_evt) override {}
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

  void react(Event coming_evt) override {}
};

struct RouteGo : public BehNode
{
  RouteGo() {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      entity.each<WayPoint>([&](flecs::entity point) 
      {
        point.get([&](const Position &target_pos)
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
    });
    return res;
  }

  void react(Event coming_evt) override {}
};

struct GetNextPoint : public BehNode
{
  GetNextPoint() {}

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    flecs::entity next;
    entity.each<WayPoint>([&](flecs::entity cur_point) 
    {
      cur_point.each<WayPoint>([&](flecs::entity next_point) 
      {
        entity.remove<WayPoint>(cur_point);
        entity.add<WayPoint>(next_point);
        res = BEH_SUCCESS;
      });
    });
    return res;
  }

  void react(Event coming_evt) override {}
};

struct AskHelp : public BehNode
{
  size_t entityBb = size_t(-1); // wraps to 0xff...
  const char *target_bb_name;

  AskHelp(flecs::entity entity, const char *bb_name, const char *_target_bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
    target_bb_name = _target_bb_name;
  }

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    static auto swarmQuery = ecs.query<const Swarm, Blackboard, BehaviourTree>();
    flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
    if (!ecs.is_valid(targetEntity)){
      return BEH_FAIL;
    }
    entity.set([&](const Swarm &swarm)
    {
      swarmQuery.each([&](flecs::entity ally, const Swarm &aswarm, Blackboard &abb, BehaviourTree &abt)
      {
        if (swarm.idx != aswarm.idx)
          return;
        flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
        size_t targetBb = reg_entity_blackboard_var<flecs::entity>(ally, target_bb_name);
        abb.set<flecs::entity>(targetBb, targetEntity);
        abt.root->react(HelpEvent);
      });
    });
    return BEH_SUCCESS;
  }

  void react(Event coming_evt) override {}
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

BehNode *parallel(const std::vector<BehNode*> &nodes)
{
  Parallel *par = new Parallel;
  for (BehNode *node : nodes)
    par->pushNode(node);
  return par;
}

BehNode *with_reaction(BehNode* node, const std::vector<std::pair<Event, BehNode*>> &reactions)
{
  ReactNode *rea = new ReactNode(node);
  for (auto [evt, react] : reactions)
    rea->addReaction(evt, react);
  return rea;
}

BehNode *and_node(const std::vector<BehNode*> &nodes)
{
  AndNode *an = new AndNode;
  for (BehNode *node : nodes)
    an->pushNode(node);
  return an;
}

BehNode *or_node(const std::vector<BehNode*> &nodes)
{
  OrNode *on = new OrNode;
  for (BehNode *node : nodes)
    on->pushNode(node);
  return on;
}

BehNode *not_node(BehNode* node) 
{
  return new NotNode(node);
}

BehNode *move_to_entity(flecs::entity entity, const char *bb_name)
{
  return new MoveToEntity(entity, bb_name);
}

BehNode *is_low_hp(float thres)
{
  return new IsLowHp(thres);
}

BehNode *find_enemy(flecs::entity entity, float dist, const char *bb_name)
{
  return new FindEnemy(entity, dist, bb_name);
}

BehNode *flee(flecs::entity entity, const char *bb_name)
{
  return new Flee(entity, bb_name);
}

BehNode *patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
{
  return new Patrol(entity, patrol_dist, bb_name);
}

BehNode *get_next_point() 
{
  return new GetNextPoint();
}

BehNode *route_go() 
{
  return new RouteGo();
}

BehNode *ask_help(flecs::entity entity, const char *bb_name, const char *target_bb_name) 
{
  return new AskHelp(entity, bb_name, target_bb_name);
}