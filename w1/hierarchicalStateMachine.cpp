#include "hierarchicalStateMachine.h"

HierarchicalStateMachine::~HierarchicalStateMachine()
{
  for (HierarchicalStateMachine* state : states)
    delete state;
  states.clear();
  for (auto &transList : transitions)
    for (auto &transition : transList)
      delete transition.first;
  transitions.clear();
}

void HierarchicalStateMachine::enter() const {
  if (innerstate != nullptr)
    innerstate->enter();
  else 
    states[curStateIdx]->enter();
}

void HierarchicalStateMachine::exit() const {
  if (innerstate != nullptr)
    innerstate->exit();
  else 
    states[curStateIdx]->exit();
}

void HierarchicalStateMachine::act(float dt, flecs::world &ecs, flecs::entity entity)
{
  if (innerstate != nullptr) {
    innerstate->act(dt, ecs, entity);
    return;
  }
  if (curStateIdx < states.size())
  {
    for (const std::pair<StateTransition*, int> &transition : transitions[curStateIdx])
      if (transition.first->isAvailable(ecs, entity))
      {
        states[curStateIdx]->exit();
        curStateIdx = transition.second;
        states[curStateIdx]->enter();
        break;
      }
    states[curStateIdx]->act(dt, ecs, entity);
  }
  else
    curStateIdx = 0;
}

int HierarchicalStateMachine::addState(State *st)
{
  int idx = states.size();
  states.push_back(new HierarchicalStateMachine(st));
  transitions.push_back(std::vector<std::pair<StateTransition*, int>>());
  return idx;
}

int HierarchicalStateMachine::addState(HierarchicalStateMachine *st)
{
  int idx = states.size();
  states.push_back(st);
  transitions.push_back(std::vector<std::pair<StateTransition*, int>>());
  return idx;
}

void HierarchicalStateMachine::addTransition(StateTransition *trans, int from, int to)
{
  transitions[from].push_back(std::make_pair(trans, to));
}

