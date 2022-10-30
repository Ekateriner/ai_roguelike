#pragma once

#include "blackboard.h"

enum Events {
  NoEvent,
  HelpEvent
};

struct Event {
  Events event;
  Blackboard event_bb;
};
