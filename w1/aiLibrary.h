#pragma once

#include "stateMachine.h"
#include "ecsTypes.h"

// states
State *create_attack_enemy_state();
State *create_move_to_enemy_state();
State *create_move_to_ally_state();
//State *create_move_to_player_state();
State *create_flee_from_enemy_state();
State *create_patrol_state(float patrol_dist);
State *create_heal_state(float power);
State *create_ally_heal_state(float power, float dist);
State *create_nop_state();
State *create_move_to_state(Position pos);
State *create_activity_state(float dur, Actions a);

// transitions
StateTransition *create_enemy_available_transition(float dist);
StateTransition *create_ally_available_transition(float dist);
StateTransition *create_enemy_reachable_transition();
StateTransition *create_hitpoints_less_than_transition(float thres);
StateTransition *create_ally_hitpoints_less_than_transition(float thres, float dist);
StateTransition *create_ability_available_transition(float cd);
StateTransition *create_near_transition(Position pos, float dist);
StateTransition *create_activity_end_transition();
StateTransition *create_negate_transition(StateTransition *in);
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs);
StateTransition *create_or_transition(StateTransition *lhs, StateTransition *rhs);
StateTransition *create_time_transition(float lhs, float rhs);
StateTransition *create_need_talk_transition();
