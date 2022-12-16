#include "pathfinder.h"
#include "dungeonUtils.h"
#include <algorithm>
#include <iostream>

float heuristic(IVec2 lhs, IVec2 rhs)
{
  return sqrtf(sqr(float(lhs.x - rhs.x)) + sqr(float(lhs.y - rhs.y)));
};

template<typename T>
static size_t coord_to_idx(T x, T y, size_t w)
{
  return size_t(y) * w + size_t(x);
}

static std::vector<IVec2> reconstruct_path(std::vector<IVec2> prev, IVec2 to, size_t width)
{
  IVec2 curPos = to;
  std::vector<IVec2> res = {curPos};
  while (prev[coord_to_idx(curPos.x, curPos.y, width)] != IVec2{-1, -1})
  {
    curPos = prev[coord_to_idx(curPos.x, curPos.y, width)];
    res.insert(res.begin(), curPos);
  }
  return res;
}

static std::vector<IVec2> find_path_a_star(const DungeonData &dd, IVec2 from, IVec2 to,
                                           IVec2 lim_min, IVec2 lim_max)
{
  if (from.x < 0 || from.y < 0 || from.x >= int(dd.width) || from.y >= int(dd.height))
    return std::vector<IVec2>();
  size_t inpSize = dd.width * dd.height;

  std::vector<float> g(inpSize, std::numeric_limits<float>::max());
  std::vector<float> f(inpSize, std::numeric_limits<float>::max());
  std::vector<IVec2> prev(inpSize, {-1,-1});

  auto getG = [&](IVec2 p) -> float { return g[coord_to_idx(p.x, p.y, dd.width)]; };
  auto getF = [&](IVec2 p) -> float { return f[coord_to_idx(p.x, p.y, dd.width)]; };

  g[coord_to_idx(from.x, from.y, dd.width)] = 0;
  f[coord_to_idx(from.x, from.y, dd.width)] = heuristic(from, to);

  std::vector<IVec2> openList = {from};
  std::vector<IVec2> closedList;

  while (!openList.empty())
  {
    size_t bestIdx = 0;
    float bestScore = getF(openList[0]);
    for (size_t i = 1; i < openList.size(); ++i)
    {
      float score = getF(openList[i]);
      if (score < bestScore)
      {
        bestIdx = i;
        bestScore = score;
      }
    }
    if (openList[bestIdx] == to)
      return reconstruct_path(prev, to, dd.width);
    IVec2 curPos = openList[bestIdx];
    openList.erase(openList.begin() + bestIdx);
    if (std::find(closedList.begin(), closedList.end(), curPos) != closedList.end())
      continue;
    size_t idx = coord_to_idx(curPos.x, curPos.y, dd.width);
    closedList.emplace_back(curPos);
    auto checkNeighbour = [&](IVec2 p)
    {
      // out of bounds
      if (p.x < lim_min.x || p.y < lim_min.y || p.x >= lim_max.x || p.y >= lim_max.y)
        return;
      size_t idx = coord_to_idx(p.x, p.y, dd.width);
      // not empty
      if (dd.tiles[idx] == dungeon::wall)
        return;
      float edgeWeight = 1.f;
      float gScore = getG(curPos) + 1.f * edgeWeight; // we're exactly 1 unit away
      if (gScore < getG(p))
      {
        prev[idx] = curPos;
        g[idx] = gScore;
        f[idx] = gScore + heuristic(p, to);
      }
      bool found = std::find(openList.begin(), openList.end(), p) != openList.end();
      if (!found)
        openList.emplace_back(p);
    };
    checkNeighbour({curPos.x + 1, curPos.y + 0});
    checkNeighbour({curPos.x - 1, curPos.y + 0});
    checkNeighbour({curPos.x + 0, curPos.y + 1});
    checkNeighbour({curPos.x + 0, curPos.y - 1});
  }
  // empty path
  return std::vector<IVec2>();
}

constexpr size_t splitTiles = 10;

void prebuild_map(flecs::world &ecs)
{
  auto mapQuery = ecs.query<const DungeonData>();

  ecs.defer([&]()
  {
    mapQuery.each([&](flecs::entity e, const DungeonData &dd)
    {
      // go through each super tile
      const size_t width = dd.width / splitTiles;
      const size_t height = dd.height / splitTiles;

      auto check_border = [&](size_t xx, size_t yy,
                              size_t dir_x, size_t dir_y,
                              int offs_x, int offs_y,
                              std::vector<PathPortal> &portals)
      {
        int spanFrom = -1;
        int spanTo = -1;
        for (size_t i = 0; i < splitTiles; ++i)
        {
          size_t x = xx * splitTiles + i * dir_x;
          size_t y = yy * splitTiles + i * dir_y;
          size_t nx = x + offs_x;
          size_t ny = y + offs_y;
          if (dd.tiles[y * dd.width + x] != dungeon::wall &&
              dd.tiles[ny * dd.width + nx] != dungeon::wall)
          {
            if (spanFrom < 0)
              spanFrom = i;
            spanTo = i;
          }
          else if (spanFrom >= 0)
          {
            // write span
            portals.push_back({xx * splitTiles + spanFrom * dir_x + offs_x,
                               yy * splitTiles + spanFrom * dir_y + offs_y,
                               xx * splitTiles + spanTo * dir_x,
                               yy * splitTiles + spanTo * dir_y});
            spanFrom = -1;
          }
        }
        if (spanFrom >= 0)
        {
          portals.push_back({xx * splitTiles + spanFrom * dir_x + offs_x,
                             yy * splitTiles + spanFrom * dir_y + offs_y,
                             xx * splitTiles + spanTo * dir_x,
                             yy * splitTiles + spanTo * dir_y});
        }
      };

      std::vector<PathPortal> portals;
      std::vector<std::vector<size_t>> tilePortalsIndices;

      auto push_portals = [&](size_t x, size_t y,
                              int offs_x, int offs_y,
                              const std::vector<PathPortal> &new_portals)
      {
        for (const PathPortal &portal : new_portals)
        {
          size_t idx = portals.size();
          portals.push_back(portal);
          tilePortalsIndices[y * width + x].push_back(idx);
          tilePortalsIndices[(y + offs_y) * width + x + offs_x].push_back(idx);
        }
      };
      for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width; ++x)
        {
          tilePortalsIndices.push_back(std::vector<size_t>{});
          // check top
          if (y > 0)
          {
            std::vector<PathPortal> topPortals;
            check_border(x, y, 1, 0, 0, -1, topPortals);
            push_portals(x, y, 0, -1, topPortals);
          }
          // left
          if (x > 0)
          {
            std::vector<PathPortal> leftPortals;
            check_border(x, y, 0, 1, -1, 0, leftPortals);
            push_portals(x, y, -1, 0, leftPortals);
          }
        }
      for (size_t tidx = 0; tidx < tilePortalsIndices.size(); ++tidx)
      {
        const std::vector<size_t> &indices = tilePortalsIndices[tidx];
        size_t x = tidx % width;
        size_t y = tidx / width;
        IVec2 limMin{int((x + 0) * splitTiles), int((y + 0) * splitTiles)};
        IVec2 limMax{int((x + 1) * splitTiles), int((y + 1) * splitTiles)};
        for (size_t i = 0; i < indices.size(); ++i)
        {
          PathPortal &firstPortal = portals[indices[i]];
          for (size_t j = i + 1; j < indices.size(); ++j)
          {
            PathPortal &secondPortal = portals[indices[j]];
            // check path from i to j
            // check each position (to find closest dist) (could be made more optimal)
            bool noPath = false;
            std::vector<IVec2> optimPath {}; 
            for (size_t fromY = std::max(firstPortal.startY, size_t(limMin.y));
                        fromY <= std::min(firstPortal.endY, size_t(limMax.y - 1)) && !noPath; ++fromY)
            {
              for (size_t fromX = std::max(firstPortal.startX, size_t(limMin.x));
                          fromX <= std::min(firstPortal.endX, size_t(limMax.x - 1)) && !noPath; ++fromX)
              {
                for (size_t toY = std::max(secondPortal.startY, size_t(limMin.y));
                            toY <= std::min(secondPortal.endY, size_t(limMax.y - 1)) && !noPath; ++toY)
                {
                  for (size_t toX = std::max(secondPortal.startX, size_t(limMin.x));
                              toX <= std::min(secondPortal.endX, size_t(limMax.x - 1)) && !noPath; ++toX)
                  {
                    IVec2 from{int(fromX), int(fromY)};
                    IVec2 to{int(toX), int(toY)};
                    std::vector<IVec2> path = find_path_a_star(dd, from, to, limMin, limMax);
                    if (path.empty() && from != to)
                    {
                      noPath = true; // if we found that there's no path at all - we can break out
                      break;
                    }
                    if (optimPath.empty() || path.size() < optimPath.size()) {
                      optimPath = path;
                    }
                  }
                }
              }
            }
            // write pathable data and length
            if (noPath)
              continue;
            firstPortal.conns.push_back({indices[j], float(optimPath.size()), optimPath});
            std::reverse(optimPath.begin(), optimPath.end());
            secondPortal.conns.push_back({indices[i], float(optimPath.size()), optimPath});
          }
        }
      }
      e.set(DungeonPortals{splitTiles, portals, tilePortalsIndices});
    });
  });
}

static std::vector<IVec2> reconstruct_graph_path(const DungeonPortals& dp, std::vector<size_t> prev, 
                                                 size_t from_idx, std::vector<std::vector<IVec2>> fromConn,
                                                 size_t to_idx, std::vector<std::vector<IVec2>> toConn)
{
  size_t cur_idx = to_idx;
  std::vector<IVec2> res{};
  while (prev[cur_idx] != dp.portals.size() + 2)
  {
    size_t prev_idx = prev[cur_idx];
    if (cur_idx == to_idx) {
      res.insert(res.begin(), toConn[prev_idx].begin(), toConn[prev_idx].end());
    }
    else if (prev_idx == from_idx) {
      res.insert(res.begin(), fromConn[cur_idx].begin(), fromConn[cur_idx].end());
    }
    else {
      auto& path = dp.portals[prev_idx].conns[cur_idx].path;
      res.insert(res.begin(), path.begin(), path.end());
    }
    cur_idx = prev_idx;
  }
  return res;
}


std::vector<IVec2> find_path_global(const DungeonData &dd, const DungeonPortals& dp, 
                                    IVec2 from, IVec2 to)
{
  if (from.x < 0 || from.y < 0 || from.x >= int(dd.width) || from.y >= int(dd.height))
    return std::vector<IVec2>();

  // build graph
  std::vector<std::pair<float, float>> portalsPos;
  for (auto& port : dp.portals) {
    portalsPos.push_back(std::pair(float(port.startX + port.endX)/2., float(port.startY + port.endY)/2.));
  }
  portalsPos.push_back({from.x, from.y});
  portalsPos.push_back({to.x, to.y});

  std::vector<std::vector<float>> edges(portalsPos.size(), std::vector<float>(portalsPos.size(), -1.));
  for (size_t i = 0; i < dp.portals.size(); i++) {
    for (auto& conn : dp.portals[i].conns) {
      edges[i][conn.connIdx] = conn.score;
    }
  }

  size_t from_idx = dp.portals.size();
  size_t to_idx = from_idx + 1;

  //calc near for start and end
  std::vector<std::vector<IVec2>> fromConn(dp.portals.size());
  std::vector<std::vector<IVec2>> toConn(dp.portals.size());

  {
    size_t x = from.x / splitTiles;
    size_t y = from.y / splitTiles;
    IVec2 limMin{int((x + 0) * splitTiles), int((y + 0) * splitTiles)};
    IVec2 limMax{int((x + 1) * splitTiles), int((y + 1) * splitTiles)};
    for(size_t i = 0; i < dp.portals.size(); i++) {
      if (std::abs(portalsPos[from_idx].first - portalsPos[i].first) <= splitTiles &&
          std::abs(portalsPos[from_idx].second - portalsPos[i].second) <= splitTiles) {
        bool noPath = false;
        std::vector<IVec2> optimPath{};
        for (size_t toY = std::max(dp.portals[i].startY, size_t(limMin.y));
                          toY <= std::min(dp.portals[i].endY, size_t(limMax.y - 1)) && !noPath; ++toY)
        {
          for (size_t toX = std::max(dp.portals[i].startX, size_t(limMin.x));
                      toX <= std::min(dp.portals[i].endX, size_t(limMax.x - 1)) && !noPath; ++toX)
          {
            IVec2 from{int(from.x), int(from.y)};
            IVec2 to{int(toX), int(toY)};
            std::vector<IVec2> path = find_path_a_star(dd, from, to, limMin, limMax);
            if (path.empty() && from != to)
            {
              noPath = true; // if we found that there's no path at all - we can break out
              break;
            }
            if (optimPath.empty() || path.size() < optimPath.size()) {
              optimPath = path;
            }
          }
        }

        edges[from_idx][i] = optimPath.size();
        edges[i][from_idx] = optimPath.size();
        fromConn[i] = optimPath;
      }
    }
  }

  {
    size_t x = to.x / splitTiles;
    size_t y = to.y / splitTiles;
    IVec2 limMin{int((x + 0) * splitTiles), int((y + 0) * splitTiles)};
    IVec2 limMax{int((x + 1) * splitTiles), int((y + 1) * splitTiles)};
    for(size_t i = 0; i < dp.portals.size(); i++) {
      if (std::abs(portalsPos[to_idx].first - portalsPos[i].first) <= splitTiles &&
          std::abs(portalsPos[to_idx].second - portalsPos[i].second) <= splitTiles) {
        bool noPath = false;
        std::vector<IVec2> optimPath{};
        for (size_t fromY = std::max(dp.portals[i].startY, size_t(limMin.y));
                          fromY <= std::min(dp.portals[i].endY, size_t(limMax.y - 1)) && !noPath; ++fromY)
        {
          for (size_t fromX = std::max(dp.portals[i].startX, size_t(limMin.x));
                      fromX <= std::min(dp.portals[i].endX, size_t(limMax.x - 1)) && !noPath; ++fromX)
          {
            IVec2 from{int(fromX), int(fromY)};
            IVec2 to{int(to.x), int(to.y)};
            std::vector<IVec2> path = find_path_a_star(dd, from, to, limMin, limMax);
            if (path.empty() && from != to)
            {
              noPath = true; // if we found that there's no path at all - we can break out
              break;
            }
            if (optimPath.empty() || path.size() < optimPath.size()) {
              optimPath = path;
            }
          }
        }

        edges[to_idx][i] = optimPath.size();
        edges[i][to_idx] = optimPath.size();
        toConn[i] = optimPath;
      }
    }
  }

  // find path
  
  std::vector<float> g(portalsPos.size(), std::numeric_limits<float>::max());
  std::vector<float> f(portalsPos.size(), std::numeric_limits<float>::max());
  std::vector<size_t> prev(portalsPos.size(), portalsPos.size());

  auto graph_heuristic = [&](size_t from, size_t to) -> float { 
    return sqrtf(sqr(portalsPos[from].first - portalsPos[to].first) + 
                 sqr(portalsPos[from].second - portalsPos[to].second)); 
  };

  g[from_idx] = 0;
  f[from_idx] = graph_heuristic(from_idx, to_idx);

  std::vector<size_t> openList = {from_idx};
  std::vector<size_t> closedList;

  while (!openList.empty())
  {
    size_t bestIdx = 0;
    float bestScore = f[openList[0]];
    for (size_t i = 1; i < openList.size(); ++i)
    {
      float score = f[openList[i]];
      if (score < bestScore)
      {
        bestIdx = i;
        bestScore = score;
      }
    }
    if (openList[bestIdx] == to_idx)
      return reconstruct_graph_path(dp, prev, from_idx, fromConn, to_idx, toConn);
    size_t cur_idx = openList[bestIdx];
    openList.erase(openList.begin() + bestIdx);
    if (std::find(closedList.begin(), closedList.end(), cur_idx) != closedList.end())
      continue;
    closedList.emplace_back(cur_idx);
    auto checkNeighbour = [&](size_t idx)
    {
      float edgeWeight = 1.f;
      float gScore = g[cur_idx] + edges[cur_idx][idx]; // we're exactly 1 unit away
      if (gScore < g[idx])
      {
        prev[idx] = cur_idx;
        g[idx] = gScore;
        f[idx] = gScore + graph_heuristic(idx, to_idx);
      }
      bool found = std::find(openList.begin(), openList.end(), idx) != openList.end();
      if (!found)
        openList.emplace_back(idx);
    };
    for(size_t j = 0; j < edges[cur_idx].size(); j++) {
      if (edges[cur_idx][j] != -1)
        checkNeighbour(j);
    }
  }
  // empty path
  return std::vector<IVec2>();
}
