//===- RDFLiveness.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Recalculate the liveness information given a data flow graph.
// This includes block live-ins and kill flags.

#ifndef LLVM_CODEGEN_RDFLIVENESS_H
#define LLVM_CODEGEN_RDFLIVENESS_H

#include "RDFGraph.h"
#include "RDFRegisters.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/MC/LaneBitmask.h"
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace llvm {

class MachineBasicBlock;
class MachineDominanceFrontier;
class MachineDominatorTree;
class MachineRegisterInfo;
class TargetRegisterInfo;

namespace rdf {
namespace detail {

using NodeRef = std::pair<NodeId, LaneBitmask>;

} // namespace detail
} // namespace rdf
} // namespace llvm

namespace std {

template <> struct hash<llvm::rdf::detail::NodeRef> {
  std::size_t operator()(llvm::rdf::detail::NodeRef R) const {
    return std::hash<llvm::rdf::NodeId>{}(R.first) ^
           std::hash<llvm::LaneBitmask::Type>{}(R.second.getAsInteger());
  }
};

} // namespace std

namespace llvm::rdf {

struct Liveness {
public:
  using LiveMapType = RegisterAggrMap<MachineBasicBlock *>;
  using NodeRef = detail::NodeRef;
  using NodeRefSet = std::unordered_set<NodeRef>;
  using RefMap = std::unordered_map<RegisterId, NodeRefSet>;

  Liveness(MachineRegisterInfo &mri, const DataFlowGraph &g)
      : DFG(g), TRI(g.getTRI()), PRI(g.getPRI()), MDT(g.getDT()),
        MDF(g.getDF()), LiveMap(g.getPRI()), Empty(), NoRegs(g.getPRI()) {}

  NodeList getAllReachingDefs(RegisterRef RefRR, NodeAddr<RefNode *> RefA,
                              bool TopShadows, bool FullChain,
                              const RegisterAggr &DefRRs);

  NodeList getAllReachingDefs(NodeAddr<RefNode *> RefA) {
    return getAllReachingDefs(RefA.Addr->getRegRef(DFG), RefA, false, false,
                              NoRegs);
  }

  NodeList getAllReachingDefs(RegisterRef RefRR, NodeAddr<RefNode *> RefA) {
    return getAllReachingDefs(RefRR, RefA, false, false, NoRegs);
  }

  NodeSet getAllReachedUses(RegisterRef RefRR, NodeAddr<DefNode *> DefA,
                            const RegisterAggr &DefRRs);

  NodeSet getAllReachedUses(RegisterRef RefRR, NodeAddr<DefNode *> DefA) {
    return getAllReachedUses(RefRR, DefA, NoRegs);
  }

  std::pair<NodeSet, bool> getAllReachingDefsRec(RegisterRef RefRR,
                                                 NodeAddr<RefNode *> RefA,
                                                 NodeSet &Visited,
                                                 const NodeSet &Defs);

  NodeAddr<RefNode *> getNearestAliasedRef(RegisterRef RefRR,
                                           NodeAddr<InstrNode *> IA);

  LiveMapType &getLiveMap() { return LiveMap; }
  const LiveMapType &getLiveMap() const { return LiveMap; }

  const RefMap &getRealUses(NodeId P) const {
    auto F = RealUseMap.find(P);
    return F == RealUseMap.end() ? Empty : F->second;
  }

  void computePhiInfo();
  void computeLiveIns();
  void resetLiveIns();
  void resetKills();
  void resetKills(MachineBasicBlock *B);

  void trace(bool T) { Trace = T; }

private:
  const DataFlowGraph &DFG;
  const TargetRegisterInfo &TRI;
  const PhysicalRegisterInfo &PRI;
  const MachineDominatorTree &MDT;
  const MachineDominanceFrontier &MDF;
  LiveMapType LiveMap;
  const RefMap Empty;
  const RegisterAggr NoRegs;
  bool Trace = false;

  // Cache of mapping from node ids (for RefNodes) to the containing
  // basic blocks. Not computing it each time for each node reduces
  // the liveness calculation time by a large fraction.
  DenseMap<NodeId, MachineBasicBlock *> NBMap;

  // Phi information:
  //
  // RealUseMap
  // map: NodeId -> (map: RegisterId -> NodeRefSet)
  //      phi id -> (map: register -> set of reached non-phi uses)
  DenseMap<NodeId, RefMap> RealUseMap;

  // Inverse iterated dominance frontier.
  std::map<MachineBasicBlock *, std::set<MachineBasicBlock *>> IIDF;

  // Live on entry.
  std::map<MachineBasicBlock *, RefMap> PhiLON;

  // Phi uses are considered to be located at the end of the block that
  // they are associated with. The reaching def of a phi use dominates the
  // block that the use corresponds to, but not the block that contains
  // the phi itself. To include these uses in the liveness propagation (up
  // the dominator tree), create a map: block -> set of uses live on exit.
  std::map<MachineBasicBlock *, RefMap> PhiLOX;

  MachineBasicBlock *getBlockWithRef(NodeId RN) const;
  void traverse(MachineBasicBlock *B, RefMap &LiveIn);
  void emptify(RefMap &M);

  std::pair<NodeSet, bool>
  getAllReachingDefsRecImpl(RegisterRef RefRR, NodeAddr<RefNode *> RefA,
                            NodeSet &Visited, const NodeSet &Defs,
                            unsigned Nest, unsigned MaxNest);
};

raw_ostream &operator<<(raw_ostream &OS, const Print<Liveness::RefMap> &P);

} // end namespace llvm::rdf

#endif // LLVM_CODEGEN_RDFLIVENESS_H
