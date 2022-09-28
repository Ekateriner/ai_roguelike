#pragma once
#include <vector>
#include <flecs.h>
#include <iostream>
#include "stateMachine.h"

class HierarchicalStateMachine
{
  int curStateIdx = 0;
  std::vector<HierarchicalStateMachine*> states;
  std::vector<std::vector<std::pair<StateTransition*, int>>> transitions;
  State* innerstate = nullptr;
public:
  HierarchicalStateMachine() = default;
  HierarchicalStateMachine(State *st) : innerstate(st) {};
  HierarchicalStateMachine(const HierarchicalStateMachine &sm) = default;
  HierarchicalStateMachine(HierarchicalStateMachine &&sm) = default;

  ~HierarchicalStateMachine();

  HierarchicalStateMachine &operator=(const HierarchicalStateMachine &sm) = default;
  HierarchicalStateMachine &operator=(HierarchicalStateMachine &&sm) = default;
  
  void enter() const;
  void exit() const;
  void act(float dt, flecs::world &ecs, flecs::entity entity);

  int addState(State *st);
  int addState(HierarchicalStateMachine *st);
  void addTransition(StateTransition *trans, int from, int to);

  void get_state() {
    std::cout << curStateIdx << "-";
    if (innerstate == nullptr)
      states[curStateIdx]->get_state();
    else
      std::cout << "inner" << std::endl;
  }
};

