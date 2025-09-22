//===- CallGraphSort.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This is based on the ELF port, see ELF/CallGraphSort.cpp for the details
/// about the algorithm.
///
//===----------------------------------------------------------------------===//

#include "CallGraphSort.h"
#include "COFFLinkerContext.h"
#include "InputFiles.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"

#include <numeric>

using namespace llvm;
using namespace lld;
using namespace lld::coff;

namespace {
struct Edge {
  int from;
  uint64_t weight;
};

struct Cluster {
  Cluster(int sec, size_t s) : next(sec), prev(sec), size(s) {}

  double getDensity() const {
    if (size == 0)
      return 0;
    return double(weight) / double(size);
  }

  int next;
  int prev;
  uint64_t size;
  uint64_t weight = 0;
  uint64_t initialWeight = 0;
  Edge bestPred = {-1, 0};
};

class CallGraphSort {
public:
  CallGraphSort(const COFFLinkerContext &ctx);

  DenseMap<const SectionChunk *, int> run();

private:
  std::vector<Cluster> clusters;
  std::vector<const SectionChunk *> sections;

  const COFFLinkerContext &ctx;
};

// Maximum amount the combined cluster density can be worse than the original
// cluster to consider merging.
constexpr int MAX_DENSITY_DEGRADATION = 8;

// Maximum cluster size in bytes.
constexpr uint64_t MAX_CLUSTER_SIZE = 1024 * 1024;
} // end anonymous namespace

using SectionPair = std::pair<const SectionChunk *, const SectionChunk *>;

// Take the edge list in Config->CallGraphProfile, resolve symbol names to
// Symbols, and generate a graph between InputSections with the provided
// weights.
CallGraphSort::CallGraphSort(const COFFLinkerContext &ctx) : ctx(ctx) {
  const MapVector<SectionPair, uint64_t> &profile = ctx.config.callGraphProfile;
  DenseMap<const SectionChunk *, int> secToCluster;

  auto getOrCreateNode = [&](const SectionChunk *isec) -> int {
    auto res = secToCluster.try_emplace(isec, clusters.size());
    if (res.second) {
      sections.push_back(isec);
      clusters.emplace_back(clusters.size(), isec->getSize());
    }
    return res.first->second;
  };

  // Create the graph.
  for (const std::pair<SectionPair, uint64_t> &c : profile) {
    const auto *fromSec = cast<SectionChunk>(c.first.first->repl);
    const auto *toSec = cast<SectionChunk>(c.first.second->repl);
    uint64_t weight = c.second;

    // Ignore edges between input sections belonging to different output
    // sections.  This is done because otherwise we would end up with clusters
    // containing input sections that can't actually be placed adjacently in the
    // output.  This messes with the cluster size and density calculations.  We
    // would also end up moving input sections in other output sections without
    // moving them closer to what calls them.
    if (ctx.getOutputSection(fromSec) != ctx.getOutputSection(toSec))
      continue;

    int from = getOrCreateNode(fromSec);
    int to = getOrCreateNode(toSec);

    clusters[to].weight += weight;

    if (from == to)
      continue;

    // Remember the best edge.
    Cluster &toC = clusters[to];
    if (toC.bestPred.from == -1 || toC.bestPred.weight < weight) {
      toC.bestPred.from = from;
      toC.bestPred.weight = weight;
    }
  }
  for (Cluster &c : clusters)
    c.initialWeight = c.weight;
}

// It's bad to merge clusters which would degrade the density too much.
static bool isNewDensityBad(Cluster &a, Cluster &b) {
  double newDensity = double(a.weight + b.weight) / double(a.size + b.size);
  return newDensity < a.getDensity() / MAX_DENSITY_DEGRADATION;
}

// Find the leader of V's belonged cluster (represented as an equivalence
// class). We apply union-find path-halving technique (simple to implement) in
// the meantime as it decreases depths and the time complexity.
static int getLeader(std::vector<int> &leaders, int v) {
  while (leaders[v] != v) {
    leaders[v] = leaders[leaders[v]];
    v = leaders[v];
  }
  return v;
}

static void mergeClusters(std::vector<Cluster> &cs, Cluster &into, int intoIdx,
                          Cluster &from, int fromIdx) {
  int tail1 = into.prev, tail2 = from.prev;
  into.prev = tail2;
  cs[tail2].next = intoIdx;
  from.prev = tail1;
  cs[tail1].next = fromIdx;
  into.size += from.size;
  into.weight += from.weight;
  from.size = 0;
  from.weight = 0;
}

// Group InputSections into clusters using the Call-Chain Clustering heuristic
// then sort the clusters by density.
DenseMap<const SectionChunk *, int> CallGraphSort::run() {
  std::vector<int> sorted(clusters.size());
  std::vector<int> leaders(clusters.size());

  std::iota(leaders.begin(), leaders.end(), 0);
  std::iota(sorted.begin(), sorted.end(), 0);
  llvm::stable_sort(sorted, [&](int a, int b) {
    return clusters[a].getDensity() > clusters[b].getDensity();
  });

  for (int l : sorted) {
    // The cluster index is the same as the index of its leader here because
    // clusters[L] has not been merged into another cluster yet.
    Cluster &c = clusters[l];

    // Don't consider merging if the edge is unlikely.
    if (c.bestPred.from == -1 || c.bestPred.weight * 10 <= c.initialWeight)
      continue;

    int predL = getLeader(leaders, c.bestPred.from);
    if (l == predL)
      continue;

    Cluster *predC = &clusters[predL];
    if (c.size + predC->size > MAX_CLUSTER_SIZE)
      continue;

    if (isNewDensityBad(*predC, c))
      continue;

    leaders[l] = predL;
    mergeClusters(clusters, *predC, predL, c, l);
  }

  // Sort remaining non-empty clusters by density.
  sorted.clear();
  for (int i = 0, e = (int)clusters.size(); i != e; ++i)
    if (clusters[i].size > 0)
      sorted.push_back(i);
  llvm::stable_sort(sorted, [&](int a, int b) {
    return clusters[a].getDensity() > clusters[b].getDensity();
  });

  DenseMap<const SectionChunk *, int> orderMap;
  // Sections will be sorted by increasing order. Absent sections will have
  // priority 0 and be placed at the end of sections.
  int curOrder = INT_MIN;
  for (int leader : sorted) {
    for (int i = leader;;) {
      orderMap[sections[i]] = curOrder++;
      i = clusters[i].next;
      if (i == leader)
        break;
    }
  }
  if (!ctx.config.printSymbolOrder.empty()) {
    std::error_code ec;
    raw_fd_ostream os(ctx.config.printSymbolOrder, ec, sys::fs::OF_None);
    if (ec) {
      error("cannot open " + ctx.config.printSymbolOrder + ": " + ec.message());
      return orderMap;
    }
    // Print the symbols ordered by C3, in the order of increasing curOrder
    // Instead of sorting all the orderMap, just repeat the loops above.
    for (int leader : sorted)
      for (int i = leader;;) {
        const SectionChunk *sc = sections[i];

        // Search all the symbols in the file of the section
        // and find out a DefinedCOFF symbol with name that is within the
        // section.
        for (Symbol *sym : sc->file->getSymbols())
          if (auto *d = dyn_cast_or_null<DefinedCOFF>(sym))
            // Filter out non-COMDAT symbols and section symbols.
            if (d->isCOMDAT && !d->getCOFFSymbol().isSection() &&
                sc == d->getChunk())
              os << sym->getName() << "\n";
        i = clusters[i].next;
        if (i == leader)
          break;
      }
  }

  return orderMap;
}

// Sort sections by the profile data provided by  /call-graph-ordering-file
//
// This first builds a call graph based on the profile data then merges sections
// according to the C³ heuristic. All clusters are then sorted by a density
// metric to further improve locality.
DenseMap<const SectionChunk *, int>
coff::computeCallGraphProfileOrder(const COFFLinkerContext &ctx) {
  return CallGraphSort(ctx).run();
}
