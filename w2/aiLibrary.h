#pragma once

#include "stateMachine.h"
#include "behaviourTree.h"
#include "math.h"
#include "blackboard.h"
#include "aiUtils.h"

// states
State *create_attack_enemy_state();
State *create_move_to_enemy_state();
State *create_flee_from_enemy_state();
State *create_patrol_state(float patrol_dist);
State *create_nop_state();

// transitions
StateTransition *create_enemy_available_transition(float dist);
StateTransition *create_enemy_reachable_transition();
StateTransition *create_hitpoints_less_than_transition(float thres);
StateTransition *create_negate_transition(StateTransition *in);
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs);

BehNode *sequence(const std::vector<BehNode*> &nodes);
BehNode *selector(const std::vector<BehNode*> &nodes);
BehNode *parallel(const std::vector<BehNode*> &nodes);
BehNode *with_reaction(BehNode* node, const std::vector<std::pair<Event, BehNode*>> &reactions);

BehNode *and_node(const std::vector<BehNode*> &nodes);
BehNode *or_node(const std::vector<BehNode*> &nodes);
BehNode *not_node(BehNode* node);

BehNode *move_to_entity(flecs::entity entity, const char *bb_name);
BehNode *is_low_hp(float thres);
BehNode *find_enemy(flecs::entity entity, float dist, const char *bb_name);
BehNode *flee(flecs::entity entity, const char *bb_name);
BehNode *patrol(flecs::entity entity, float patrol_dist, const char *bb_name);
BehNode *get_next_point();
BehNode *route_go();
BehNode *ask_help(flecs::entity entity, const char *bb_name, const char *target_bb_name);

template<class T>
struct FindClosest : public BehNode
{
  size_t entityBb = size_t(-1);
  FindClosest(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    static auto healsQuery = ecs.query<const Position, const T>();
    entity.set([&](const Position &pos)
    {
      flecs::entity closestHeal;
      float closestDist = FLT_MAX;
      healsQuery.each([&](flecs::entity heal, const Position &epos, const T /**/)
      {
        float curDist = dist(epos, pos);
        if (curDist < closestDist)
        {
          closestDist = curDist;
          closestHeal = heal;
        }
      });
      if (ecs.is_valid(closestHeal))
      {
        bb.set<flecs::entity>(entityBb, closestHeal);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }

  void react(Event coming_evt) override {}
};

template<class T>
BehNode *find_closest(flecs::entity entity, const char *bb_name)
{
  return new FindClosest<T>(entity, bb_name);
}

