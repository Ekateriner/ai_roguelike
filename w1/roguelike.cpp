#include "roguelike.h"
#include "ecsTypes.h"
#include <debugdraw/debugdraw.h>
#include "stateMachine.h"
#include "hierarchicalStateMachine.h"
#include "aiLibrary.h"
#include "app.h"
#include <cfloat>
#include <cmath>

//for scancodes
#include <GLFW/glfw3.h>

static void add_patrol_attack_flee_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    int patrol = sm.addState(create_patrol_state(3.f));
    int moveToEnemy = sm.addState(create_move_to_enemy_state());
    int fleeFromEnemy = sm.addState(create_flee_from_enemy_state());

    sm.addTransition(create_enemy_available_transition(3.f), patrol, moveToEnemy);
    sm.addTransition(create_negate_transition(create_enemy_available_transition(5.f)), moveToEnemy, patrol);

    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(60.f), create_enemy_available_transition(5.f)),
                     moveToEnemy, fleeFromEnemy);
    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(60.f), create_enemy_available_transition(3.f)),
                     patrol, fleeFromEnemy);

    sm.addTransition(create_negate_transition(create_enemy_available_transition(7.f)), fleeFromEnemy, patrol);
  });
}

static void add_patrol_flee_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    int patrol = sm.addState(create_patrol_state(3.f));
    int fleeFromEnemy = sm.addState(create_flee_from_enemy_state());

    sm.addTransition(create_enemy_available_transition(3.f), patrol, fleeFromEnemy);
    sm.addTransition(create_negate_transition(create_enemy_available_transition(5.f)), fleeFromEnemy, patrol);
  });
}

static void add_attack_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    sm.addState(create_move_to_enemy_state());
  });
}

static void add_barbarian_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    // normal patrol
    int patrol = sm.addState(create_patrol_state(3.f));
    int moveToEnemy = sm.addState(create_move_to_enemy_state());
    int fleeFromEnemy = sm.addState(create_flee_from_enemy_state());

    // barbarian state
    int endlessMove = sm.addState(create_move_to_enemy_state());
    

    sm.addTransition(create_hitpoints_less_than_transition(50.f), moveToEnemy, endlessMove);
    sm.addTransition(create_hitpoints_less_than_transition(50.f), patrol, endlessMove);
    sm.addTransition(create_hitpoints_less_than_transition(50.f), fleeFromEnemy, endlessMove);

    sm.addTransition(create_enemy_available_transition(3.f), patrol, moveToEnemy);
    sm.addTransition(create_negate_transition(create_enemy_available_transition(5.f)), moveToEnemy, patrol);

    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(70.f), create_enemy_available_transition(5.f)),
                     moveToEnemy, fleeFromEnemy);
    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(70.f), create_enemy_available_transition(3.f)),
                     patrol, fleeFromEnemy);

    sm.addTransition(create_negate_transition(create_enemy_available_transition(7.f)), fleeFromEnemy, patrol);
  });
}

static void add_healer_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    // normal patrol
    int patrol = sm.addState(create_patrol_state(3.f));
    int moveToEnemy = sm.addState(create_move_to_enemy_state());
    int fleeFromEnemy = sm.addState(create_flee_from_enemy_state());

    // heal state
    int heal = sm.addState(create_heal_state(20.f));

    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(50.f), create_ability_available_transition(5.0f)),
                     moveToEnemy, heal);
    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(50.f), create_ability_available_transition(5.0f)),
                     patrol, heal);
    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(50.f), create_ability_available_transition(5.0f)),
                     fleeFromEnemy, heal);

    sm.addTransition(create_enemy_available_transition(3.f), patrol, moveToEnemy);
    sm.addTransition(create_enemy_available_transition(3.f), heal, moveToEnemy);
    sm.addTransition(create_negate_transition(create_enemy_available_transition(5.f)), moveToEnemy, patrol);
    sm.addTransition(create_negate_transition(create_ability_available_transition(5.0f)), heal, patrol);

    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(70.f), create_enemy_available_transition(5.f)),
                     moveToEnemy, fleeFromEnemy);
    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(70.f), create_enemy_available_transition(3.f)),
                     patrol, fleeFromEnemy);
    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(70.f), create_enemy_available_transition(5.f)),
                     heal, fleeFromEnemy);
    
    sm.addTransition(create_negate_transition(create_enemy_available_transition(7.f)), fleeFromEnemy, patrol);
  });
}

static void add_cleric_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    int patrol = sm.addState(create_patrol_state(3.f));
    int moveToEnemy = sm.addState(create_move_to_enemy_state());
    int moveToAlly = sm.addState(create_move_to_ally_state());
    int heal = sm.addState(create_ally_heal_state(20.f, 2.0f));

    sm.addTransition(create_and_transition(create_ally_hitpoints_less_than_transition(50.f, 1.f), create_ability_available_transition(10.0f)),
                     moveToEnemy, heal);
    sm.addTransition(create_and_transition(create_ally_hitpoints_less_than_transition(50.f, 1.f), create_ability_available_transition(10.0f)),
                     moveToAlly, heal);
    sm.addTransition(create_and_transition(create_ally_hitpoints_less_than_transition(50.f, 1.f), create_ability_available_transition(10.0f)),
                     patrol, heal);

    sm.addTransition(create_enemy_available_transition(3.f), patrol, moveToEnemy);
    sm.addTransition(create_enemy_available_transition(3.f), moveToAlly, moveToEnemy);
    sm.addTransition(create_and_transition(create_negate_transition(create_ability_available_transition(10.0f)), create_enemy_available_transition(3.f)), 
                     heal, moveToEnemy);

    sm.addTransition(create_and_transition(create_negate_transition(create_enemy_available_transition(5.f)), create_ally_available_transition(3.0f)), 
                     moveToEnemy, patrol);
    sm.addTransition(create_ally_available_transition(3.0f), moveToAlly, patrol);
    sm.addTransition(create_and_transition(create_negate_transition(create_ability_available_transition(10.0f)), create_ally_available_transition(3.0f)), 
                     heal, patrol);

    sm.addTransition(create_ally_hitpoints_less_than_transition(45.f, 10.0f), moveToEnemy, moveToAlly);
    sm.addTransition(create_negate_transition(create_ally_available_transition(3.f)), patrol, moveToAlly);
    sm.addTransition(create_negate_transition(create_ally_available_transition(2.f)), heal, moveToAlly);
  });
}

static void add_crafter_sm(flecs::entity entity)
{
  entity.get([](HierarchicalStateMachine &sm)
  {
    Position eat_pos{3, 3};
    Position sleep_pos{5, 5};
    Position craft_pos{1, 4};
    Position sell_pos{-2, -2};
    Position buy_pos{-2, -4};

    // sleep sm 
    HierarchicalStateMachine* sleep_sm = new HierarchicalStateMachine();
    {
      int move_to_sleep = sleep_sm->addState(create_move_to_state(sleep_pos));
      int sleep = sleep_sm->addState(create_activity_state(12.f, EA_SLEEP));

      sleep_sm->addTransition(create_near_transition(sleep_pos, FLT_EPSILON), move_to_sleep, sleep);
      sleep_sm->addTransition(create_negate_transition(create_near_transition(sleep_pos, FLT_EPSILON)), sleep, move_to_sleep);
    }

    // eat sm 
    HierarchicalStateMachine* eat_sm = new HierarchicalStateMachine();
    {
      int move_to_eat = eat_sm->addState(create_move_to_state(eat_pos));
      int eat = eat_sm->addState(create_activity_state(2.f, EA_EAT));

      eat_sm->addTransition(create_near_transition(eat_pos, FLT_EPSILON), move_to_eat, eat);
      eat_sm->addTransition(create_negate_transition(create_near_transition(eat_pos, FLT_EPSILON)), eat, move_to_eat);
    }

    // talk sm 
    HierarchicalStateMachine* talk_sm = new HierarchicalStateMachine();
    {
      int move_to_talk = talk_sm->addState(create_move_to_ally_state());
      int talk = talk_sm->addState(create_activity_state(4.f, EA_TALK));

      talk_sm->addTransition(create_ally_available_transition(1.0f), move_to_talk, talk);
      talk_sm->addTransition(create_negate_transition(create_ally_available_transition(1.0f)), talk, move_to_talk);
    }

    //work sm
    HierarchicalStateMachine* work_sm = new HierarchicalStateMachine();
    {
      int move_to_craft = work_sm->addState(create_move_to_state(craft_pos));
      int craft = work_sm->addState(create_activity_state(8.f, EA_CRAFT));
      int move_to_sell = work_sm->addState(create_move_to_state(sell_pos));
      int sell = work_sm->addState(create_activity_state(2.f, EA_SELL));
      int move_to_buy = work_sm->addState(create_move_to_state(buy_pos));
      int buy = work_sm->addState(create_activity_state(2.f, EA_BUY));

      work_sm->addTransition(create_near_transition(craft_pos, FLT_EPSILON), move_to_craft, craft);
      work_sm->addTransition(create_near_transition(sell_pos, FLT_EPSILON), move_to_sell, sell);
      work_sm->addTransition(create_near_transition(buy_pos, FLT_EPSILON), move_to_buy, buy);

      work_sm->addTransition(create_activity_end_transition(), craft, move_to_sell);
      work_sm->addTransition(create_activity_end_transition(), sell, move_to_buy);
      work_sm->addTransition(create_activity_end_transition(), buy, move_to_craft);
    }

    int sleep = sm.addState(sleep_sm);
    int work = sm.addState(work_sm);
    int eat = sm.addState(eat_sm);
    int talk = sm.addState(talk_sm);
    int wander = sm.addState(create_patrol_state(10.0f));

    sm.addTransition(create_time_transition(8.0, 12.0), sleep, eat);
    sm.addTransition(create_activity_end_transition(), sleep, eat);
    sm.addTransition(create_time_transition(20.0, 24.0), work, eat);
    sm.addTransition(create_activity_end_transition(), eat, work);
    sm.addTransition(create_time_transition(60.0, 6.0), work, sleep);
    sm.addTransition(create_time_transition(60.0, 6.0), wander, sleep);
    sm.addTransition(create_and_transition(create_time_transition(60.0, 6.0), create_negate_transition(create_ally_available_transition(2.0))),
                     talk, sleep);
    sm.addTransition(create_time_transition(40.0, 60.0), work, wander);
    sm.addTransition(create_time_transition(13.0, 40.0), wander, work);
    sm.addTransition(create_activity_end_transition(), talk, wander);
    sm.addTransition(create_need_talk_transition(), work, talk);
    sm.addTransition(create_ally_available_transition(3.0f), wander, talk);
  });
}

static flecs::entity create_monster(flecs::world &ecs, int x, int y, uint32_t color, int team = 1)
{
  return ecs.entity()
    .set(Position{x, y})
    .set(MovePos{x, y})
    .set(PatrolPos{x, y})
    .set(Hitpoints{100.f})
    .set(Action{EA_NOP})
    .set(Color{color})
    .set(StateMachine{})
    .set(Team{team})
    .set(NumActions{1, 0})
    .set(MeleeDamage{20.f});
}

static flecs::entity create_crafter(flecs::world &ecs, int x, int y, uint32_t color, int team = 0)
{
  return ecs.entity()
    .set(Position{x, y})
    .set(MovePos{x, y})
    .set(PatrolPos{x, y})
    .set(Hitpoints{100.f})
    .set(Action{EA_NOP})
    .set(Color{color})
    .set(HierarchicalStateMachine{})
    .set(Team{team})
    .set(NumActions{1, 0})
    .set(MeleeDamage{20.f})
    .set(Activity{0.0, A_NOP});
}

static flecs::entity create_monster_with_ability(flecs::world &ecs, int x, int y, uint32_t color, int team = 1)
{
  return ecs.entity()
    .set(Position{x, y})
    .set(MovePos{x, y})
    .set(PatrolPos{x, y})
    .set(Hitpoints{100.f})
    .set(Action{EA_NOP})
    .set(Color{color})
    .set(StateMachine{})
    .set(Team{team})
    .set(NumActions{1, 0})
    .set(MeleeDamage{20.f})
    .set(Ability{0.0f, 0.0f});
}

static void create_player(flecs::world &ecs, int x, int y)
{
  ecs.entity("player")
    .set(Position{x, y})
    .set(MovePos{x, y})
    .set(Hitpoints{100.f})
    .set(Color{0xffeeeeee})
    .set(Action{EA_NOP})
    .add<IsPlayer>()
    .set(Team{0})
    .set(PlayerInput{})
    .set(NumActions{2, 0})
    .set(MeleeDamage{40.f});
}

static void create_heal(flecs::world &ecs, int x, int y, float amount)
{
  ecs.entity()
    .set(Position{x, y})
    .set(HealAmount{amount})
    .set(Color{0xff4444ff});
}

static void create_powerup(flecs::world &ecs, int x, int y, float amount)
{
  ecs.entity()
    .set(Position{x, y})
    .set(PowerupAmount{amount})
    .set(Color{0xff00ffff});
}

static void create_gloabal_time(flecs::world &ecs)
{
  ecs.entity().set(Time{0.0f});
}

static void register_roguelike_systems(flecs::world &ecs)
{
  ecs.system<PlayerInput, Action, const IsPlayer>()
    .each([&](PlayerInput &inp, Action &a, const IsPlayer)
    {
      bool left = app_keypressed(GLFW_KEY_LEFT);
      bool right = app_keypressed(GLFW_KEY_RIGHT);
      bool up = app_keypressed(GLFW_KEY_UP);
      bool down = app_keypressed(GLFW_KEY_DOWN);
      if (left && !inp.left)
        a.action = EA_MOVE_LEFT;
      if (right && !inp.right)
        a.action = EA_MOVE_RIGHT;
      if (up && !inp.up)
        a.action = EA_MOVE_UP;
      if (down && !inp.down)
        a.action = EA_MOVE_DOWN;
      inp.left = left;
      inp.right = right;
      inp.up = up;
      inp.down = down;
    });
  ecs.system<const Position, const Color>()
    .each([&](const Position &pos, const Color color)
    {
      DebugDrawEncoder dde;
      dde.begin(0);
      dde.push();
        dde.setColor(color.color);
        dde.drawQuad(bx::Vec3(0, 0, 1), bx::Vec3(pos.x, pos.y, 0.f), 1.f);
      dde.pop();
      dde.end();
    });
}


void init_roguelike(flecs::world &ecs)
{
  register_roguelike_systems(ecs);

  // add_patrol_attack_flee_sm(create_monster(ecs, 5, 5, 0xffee00ee));
  // add_patrol_attack_flee_sm(create_monster(ecs, 10, -5, 0xffee00ee));
  // add_patrol_flee_sm(create_monster(ecs, -5, -5, 0xff111111));
  // add_attack_sm(create_monster(ecs, -5, 5, 0xff00ff00));
  // add_barbarian_sm(create_monster(ecs, 4, -4, 0xffffff00));
  // add_healer_sm(create_monster_with_ability(ecs, -4, -4, 0xffff00ff));
  // add_cleric_sm(create_monster_with_ability(ecs, 1, 1, 0xff00ffff, 0));
  add_crafter_sm(create_crafter(ecs, 4, 3, 0xffee00ee));

  create_player(ecs, 0, 0);

  // create_powerup(ecs, 7, 7, 10.f);
  // create_powerup(ecs, 10, -6, 10.f);
  // create_powerup(ecs, 4, -4, 10.f);

  // create_heal(ecs, -5, -5, 50.f);
  // create_heal(ecs, -5, 5, 50.f);

  create_gloabal_time(ecs);
}

static bool is_player_acted(flecs::world &ecs)
{
  static auto processPlayer = ecs.query<const IsPlayer, const Action>();
  bool playerActed = false;
  processPlayer.each([&](const IsPlayer, const Action &a)
  {
    playerActed = a.action != EA_NOP;
  });
  return playerActed;
}

static bool upd_player_actions_count(flecs::world &ecs)
{
  static auto updPlayerActions = ecs.query<const IsPlayer, NumActions>();
  bool actionsReached = false;
  updPlayerActions.each([&](const IsPlayer, NumActions &na)
  {
    na.curActions = (na.curActions + 1) % na.numActions;
    actionsReached |= na.curActions == 0;
  });
  return actionsReached;
}

static Position move_pos(Position pos, int action)
{
  if (action == EA_MOVE_LEFT)
    pos.x--;
  else if (action == EA_MOVE_RIGHT)
    pos.x++;
  else if (action == EA_MOVE_UP)
    pos.y++;
  else if (action == EA_MOVE_DOWN)
    pos.y--;
  return pos;
}

static void process_actions(flecs::world &ecs)
{
  //process abilities
  static auto processAbilities = ecs.query<Action, const Ability>(); 
  ecs.defer([&]
  {
    processAbilities.each([&](flecs::entity entity, Action &a, const Ability &ability) {
      if (a.action == EA_HEAL) {
        entity.each<Targets>([&](flecs::entity target) {
          target.set([&](Hitpoints &hp) {
            hp.hitpoints += ability.power;
          });
        });
        a.action = EA_NOP;
      }
    });
  });

  static auto processActions = ecs.query<Action, Position, MovePos, const MeleeDamage, const Team>();
  static auto checkAttacks = ecs.query<const MovePos, Hitpoints, const Team>();
  // Process all actions
  ecs.defer([&]
  {
    processActions.each([&](flecs::entity entity, Action &a, Position &pos, MovePos &mpos, const MeleeDamage &dmg, const Team &team)
    {
      Position nextPos = move_pos(pos, a.action);
      bool blocked = false;
      checkAttacks.each([&](flecs::entity enemy, const MovePos &epos, Hitpoints &hp, const Team &enemy_team)
      {
        if (entity != enemy && epos == nextPos)
        {
          blocked = true;
          if (team.team != enemy_team.team)
            hp.hitpoints -= dmg.damage;
        }
      });
      if (blocked)
        a.action = EA_NOP;
      else
        mpos = nextPos;
    });
    // now move
    processActions.each([&](flecs::entity entity, Action &a, Position &pos, MovePos &mpos, const MeleeDamage &, const Team&)
    {
      pos = mpos;
      a.action = EA_NOP;
    });
  });

  static auto deleteAllDead = ecs.query<const Hitpoints>();
  ecs.defer([&]
  {
    deleteAllDead.each([&](flecs::entity entity, const Hitpoints &hp)
    {
      if (hp.hitpoints <= 0.f)
        entity.destruct();
    });
  });

  static auto playerPickup = ecs.query<const IsPlayer, const Position, Hitpoints, MeleeDamage>();
  static auto healPickup = ecs.query<const Position, const HealAmount>();
  static auto powerupPickup = ecs.query<const Position, const PowerupAmount>();
  ecs.defer([&]
  {
    playerPickup.each([&](const IsPlayer&, const Position &pos, Hitpoints &hp, MeleeDamage &dmg)
    {
      healPickup.each([&](flecs::entity entity, const Position &ppos, const HealAmount &amt)
      {
        if (pos == ppos)
        {
          hp.hitpoints += amt.amount;
          entity.destruct();
        }
      });
      powerupPickup.each([&](flecs::entity entity, const Position &ppos, const PowerupAmount &amt)
      {
        if (pos == ppos)
        {
          dmg.damage += amt.amount;
          entity.destruct();
        }
      });
    });
  });
}

void process_turn(flecs::world &ecs)
{
  static auto stateMachineAct = ecs.query<StateMachine>();
  static auto stateHMachineAct = ecs.query<HierarchicalStateMachine>();
  if (is_player_acted(ecs))
  {
    if (upd_player_actions_count(ecs))
    {
      // Plan action for NPCs
      ecs.defer([&]
      {
        stateMachineAct.each([&](flecs::entity e, StateMachine &sm)
        {
          sm.act(1.f, ecs, e);
        });
        stateHMachineAct.each([&](flecs::entity e, HierarchicalStateMachine &sm)
        {
          sm.act(1.f, ecs, e);
        });
      });

      static auto globalTime = ecs.query<Time>();
      globalTime.each([&](flecs::entity /*e*/, Time &gtime){
        gtime.time += 1.0f;
      });
    }
    process_actions(ecs);
  }
}

void print_stats(flecs::world &ecs)
{
  bgfx::dbgTextClear();
  float cur_time;
  static auto globalTime = ecs.query<Time>();
  globalTime.each([&](flecs::entity /*e*/, Time &gtime){
    cur_time = gtime.time;
  });
  cur_time = cur_time - 72.0f * floor(cur_time / 72.0f);
  static auto playerStatsQuery = ecs.query<const IsPlayer, const Hitpoints, const MeleeDamage>();
  playerStatsQuery.each([&](const IsPlayer &, const Hitpoints &hp, const MeleeDamage &dmg)
  {
    bgfx::dbgTextPrintf(0, 1, 0x0f, "hp: %d", (int)hp.hitpoints);
    bgfx::dbgTextPrintf(0, 2, 0x0f, "power: %d", (int)dmg.damage);
    bgfx::dbgTextPrintf(0, 3, 0x0f, "time: %f", (float)cur_time);
  });
}

