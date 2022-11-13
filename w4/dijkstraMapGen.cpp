#include "dijkstraMapGen.h"
#include "ecsTypes.h"
#include "dungeonUtils.h"
#include <queue>
#include <cmath>
#include <functional>

using dijkstra_tail = std::pair<float, std::pair<size_t, size_t>>;

template<typename Callable>
static void query_dungeon_data(flecs::world &ecs, Callable c)
{
  static auto dungeonDataQuery = ecs.query<const DungeonData>();

  dungeonDataQuery.each(c);
}

template<typename Callable>
static void query_characters_positions(flecs::world &ecs, Callable c)
{
  static auto characterPositionQuery = ecs.query<const Position, const Team>();

  characterPositionQuery.each(c);
}

constexpr float invalid_tile_value = 1e5f;

static void init_tiles(std::vector<float> &map, const DungeonData &dd)
{
  map.resize(dd.width * dd.height);
  for (float &v : map)
    v = invalid_tile_value;
}

// scan version, could be implemented as Dijkstra version as well
static void process_dmap(std::vector<float> &map, const DungeonData &dd)
{
  bool done = false;
  auto getMapAt = [&](size_t x, size_t y, float def)
  {
    if (x < dd.width && y < dd.width && dd.tiles[y * dd.width + x] == dungeon::floor)
      return map[y * dd.width + x];
    return def;
  };
  auto getMinNei = [&](size_t x, size_t y)
  {
    float val = map[y * dd.width + x];
    val = std::min(val, getMapAt(x - 1, y + 0, val));
    val = std::min(val, getMapAt(x + 1, y + 0, val));
    val = std::min(val, getMapAt(x + 0, y - 1, val));
    val = std::min(val, getMapAt(x + 0, y + 1, val));
    return val;
  };
  while (!done)
  {
    done = true;
    for (size_t y = 0; y < dd.height; ++y)
      for (size_t x = 0; x < dd.width; ++x)
      {
        const size_t i = y * dd.width + x;
        if (dd.tiles[i] != dungeon::floor)
          continue;
        const float myVal = getMapAt(x, y, invalid_tile_value);
        const float minVal = getMinNei(x, y);
        if (minVal < myVal - 1.f)
        {
          map[i] = minVal + 1.f;
          done = false;
        }
      }
  }
}

float linear_update(int, int, float val, int, int) {
  return val + 1;
}

static void update_map(std::vector<float> &map, const DungeonData &dd, size_t x, size_t y, 
                       std::function<float(int, int, float, int, int)> neigh_map = linear_update) {
  //dijkstra
  std::priority_queue<dijkstra_tail, std::vector<dijkstra_tail>, std::greater<dijkstra_tail>> queue;
  std::vector<bool> visited(map.size(), false);
  queue.push({0., {x, y}});

  while (!queue.empty()) 
  {
    auto [cur_map, position] = queue.top();
    auto [cur_x, cur_y] = position;
    queue.pop();

    if (visited[cur_y * dd.width + cur_x])
      continue;
    
    visited[cur_y * dd.width + cur_x] = true;
    map[cur_y * dd.width + cur_x] = cur_map;

    if (cur_x > 0 && 
        !visited[cur_y * dd.width + (cur_x - 1)] && 
        dd.tiles[cur_y * dd.width + (cur_x - 1)] == dungeon::floor) {
      float val = neigh_map(cur_x, cur_y, cur_map, cur_x - 1, cur_y);
      if (map[cur_y * dd.width + cur_x - 1] > val) {
        map[cur_y * dd.width + cur_x - 1] = val;  
        queue.push({val, {cur_x - 1, cur_y}});
      }
    }

    if (cur_x < dd.width - 1 && 
        !visited[cur_y * dd.width + (cur_x + 1)] &&
        dd.tiles[cur_y * dd.width + (cur_x + 1)] == dungeon::floor) {
      float val = neigh_map(cur_x, cur_y, cur_map, cur_x + 1, cur_y);
      if (map[cur_y * dd.width + cur_x + 1] > val) {
        map[cur_y * dd.width + cur_x + 1] = val;  
        queue.push({val, {cur_x + 1, cur_y}});
      }
    }

    if (cur_y > 0 && 
        !visited[(cur_y - 1) * dd.width + cur_x] &&
        dd.tiles[(cur_y - 1) * dd.width + cur_x] == dungeon::floor) {
      float val = neigh_map(cur_x, cur_y, cur_map, cur_x, cur_y - 1);
      if (map[(cur_y - 1) * dd.width + cur_x] > val) {
        map[(cur_y - 1) * dd.width + cur_x] = val;  
        queue.push({val, {cur_x, cur_y - 1}});
      }
    }

    if (cur_y < dd.height - 1 && 
        !visited[(cur_y + 1) * dd.width + cur_x] &&
        dd.tiles[(cur_y + 1) * dd.width + cur_x] == dungeon::floor) {
      float val = neigh_map(cur_x, cur_y, cur_map, cur_x, cur_y + 1);
      if (map[(cur_y + 1) * dd.width + cur_x] > val) {
        map[(cur_y + 1) * dd.width + cur_x] = val;  
        queue.push({val, {cur_x, cur_y + 1}});
      }
    }
  }
}

void dmaps::gen_player_approach_map(flecs::world &ecs, std::vector<float> &map)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    query_characters_positions(ecs, [&](const Position &pos, const Team &t)
    {
      if (t.team == 0) // player team hardcode
      {
        map[pos.y * dd.width + pos.x] = 0.f;
        update_map(map, dd, pos.x, pos.y, linear_update);
      }
    });
  });
}

void dmaps::gen_player_vision_map(flecs::world &ecs, std::vector<float> &map)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    query_characters_positions(ecs, [&](const Position &pos, const Team &t)
    {
      if (t.team == 0) // player team hardcode
      {
        map[pos.y * dd.width + pos.x] = 0.f;
        update_map(map, dd, pos.x, pos.y, 
        [&](int /*prev_x*/, int /*prev_y*/, float /*val*/, int new_x, int new_y) {
          float dir_x = 2 * (pos.x > new_x) - 1;
          float dir_y = 2 * (pos.y > new_y) - 1;
          float new_val = 0;
          if (abs(pos.x - new_x) > abs(pos.y - new_y)) {
            new_val = map[new_y * dd.width + (new_x + dir_x)] + 1 + 
                      (dd.tiles[pos.y * dd.width + (pos.x - dir_x)] == dungeon::wall) * invalid_tile_value;
          }
          else if (abs(pos.x - new_x) == abs(pos.y - new_y)) {
            new_val = map[(new_y + dir_y) * dd.width + (new_x + dir_x)] + 2 +
                      (dd.tiles[(pos.y - dir_y) * dd.width + (pos.x - dir_x)] == dungeon::wall) * invalid_tile_value;
          }
          else {
            new_val = map[(new_y + dir_y) * dd.width + new_x] + 1 +
                      (dd.tiles[(pos.y - dir_y) * dd.width + pos.x] == dungeon::wall) * invalid_tile_value;
          }
          return std::min(new_val, invalid_tile_value);
        });
      }
    });
  });
}

void dmaps::gen_player_flee_map(flecs::world &ecs, std::vector<float> &map)
{
  ecs.entity("approach_map").get([&](const DijkstraMapData &dmap) {
    map = dmap.map;
  });
  for (float &v : map)
    if (v < invalid_tile_value)
      v *= -1.2f;
}

void dmaps::gen_archer_map(flecs::world &ecs, std::vector<float> &map)
{
  ecs.entity("approach_map").get([&](const DijkstraMapData &dmap) {
    map = dmap.map;
  });
  for (float &v : map)
    if (v < invalid_tile_value)
      v = abs(v - 4);// + (v > 4) * 2;
}

void dmaps::gen_hive_pack_map(flecs::world &ecs, std::vector<float> &map)
{
  static auto hiveQuery = ecs.query<const Position, const Hive>();
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    hiveQuery.each([&](const Position &pos, const Hive &)
    {
      map[pos.y * dd.width + pos.x] = 0.f;
      update_map(map, dd, pos.x, pos.y);
    });
  });
}

