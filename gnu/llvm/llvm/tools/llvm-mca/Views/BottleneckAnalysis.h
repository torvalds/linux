//===--------------------- BottleneckAnalysis.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the bottleneck analysis view.
///
/// This view internally observes backend pressure increase events in order to
/// identify problematic data dependencies and processor resource interferences.
///
/// Example of bottleneck analysis report for a dot-product on X86 btver2:
///
/// Cycles with backend pressure increase [ 40.76% ]
/// Throughput Bottlenecks: 
///   Resource Pressure       [ 39.34% ]
///   - JFPA  [ 39.34% ]
///   - JFPU0  [ 39.34% ]
///   Data Dependencies:      [ 1.42% ]
///   - Register Dependencies [ 1.42% ]
///   - Memory Dependencies   [ 0.00% ]
///
/// According to the example, backend pressure increased during the 40.76% of
/// the simulated cycles.  In particular, the major cause of backend pressure
/// increases was the contention on floating point adder JFPA accessible from
/// pipeline resource JFPU0.
///
/// At the end of each cycle, if pressure on the simulated out-of-order buffers
/// has increased, a backend pressure event is reported.
/// In particular, this occurs when there is a delta between the number of uOps
/// dispatched and the number of uOps issued to the underlying pipelines.
///
/// The bottleneck analysis view is also responsible for identifying and
/// printing the most "critical" sequence of dependent instructions according to
/// the simulated run.
///
/// Below is the critical sequence computed for the dot-product example on
/// btver2:
///
///              Instruction                     Dependency Information
/// +----< 2.    vhaddps %xmm3, %xmm3, %xmm4
/// |
/// |    < loop carried > 
/// |
/// |      0.    vmulps	 %xmm0, %xmm0, %xmm2
/// +----> 1.    vhaddps %xmm2, %xmm2, %xmm3     ## RESOURCE interference:  JFPA [ probability: 73% ]
/// +----> 2.    vhaddps %xmm3, %xmm3, %xmm4     ## REGISTER dependency:  %xmm3
/// |
/// |    < loop carried > 
/// |
/// +----> 1.    vhaddps %xmm2, %xmm2, %xmm3     ## RESOURCE interference:  JFPA [ probability: 73% ]
///
///
/// The algorithm that computes the critical sequence is very similar to a
/// critical path analysis.
/// 
/// A dependency graph is used internally to track dependencies between nodes.
/// Nodes of the graph represent instructions from the input assembly sequence,
/// and edges of the graph represent data dependencies or processor resource
/// interferences.
///
/// Edges are dynamically 'discovered' by observing instruction state
/// transitions and backend pressure increase events. Edges are internally
/// ranked based on their "criticality". A dependency is considered to be
/// critical if it takes a long time to execute, and if it contributes to
/// backend pressure increases. Criticality is internally measured in terms of
/// cycles; it is computed for every edge in the graph as a function of the edge
/// latency and the number of backend pressure increase cycles contributed by
/// that edge.
///
/// At the end of simulation, costs are propagated to nodes through the edges of
/// the graph, and the most expensive path connecting the root-set (a
/// set of nodes with no predecessors) to a leaf node is reported as critical
/// sequence.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_BOTTLENECK_ANALYSIS_H
#define LLVM_TOOLS_LLVM_MCA_BOTTLENECK_ANALYSIS_H

#include "Views/InstructionView.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace mca {

class PressureTracker {
  const MCSchedModel &SM;

  // Resource pressure distribution. There is an element for every processor
  // resource declared by the scheduling model. Quantities are number of cycles.
  SmallVector<unsigned, 4> ResourcePressureDistribution;

  // Each processor resource is associated with a so-called processor resource
  // mask. This vector allows to correlate processor resource IDs with processor
  // resource masks. There is exactly one element per each processor resource
  // declared by the scheduling model.
  SmallVector<uint64_t, 4> ProcResID2Mask;

  // Maps processor resource state indices (returned by calls to
  // `getResourceStateIndex(Mask)` to processor resource identifiers.
  SmallVector<unsigned, 4> ResIdx2ProcResID;

  // Maps Processor Resource identifiers to ResourceUsers indices.
  SmallVector<unsigned, 4> ProcResID2ResourceUsersIndex;

  // Identifies the last user of a processor resource unit.
  // This vector is updated on every instruction issued event.
  // There is one entry for every processor resource unit declared by the
  // processor model. An all_ones value is treated like an invalid instruction
  // identifier.
  using User = std::pair<unsigned, unsigned>;
  SmallVector<User, 4> ResourceUsers;

  struct InstructionPressureInfo {
    unsigned RegisterPressureCycles;
    unsigned MemoryPressureCycles;
    unsigned ResourcePressureCycles;
  };
  DenseMap<unsigned, InstructionPressureInfo> IPI;

  void updateResourcePressureDistribution(uint64_t CumulativeMask);

  User getResourceUser(unsigned ProcResID, unsigned UnitID) const {
    unsigned Index = ProcResID2ResourceUsersIndex[ProcResID];
    return ResourceUsers[Index + UnitID];
  }

public:
  PressureTracker(const MCSchedModel &Model);

  ArrayRef<unsigned> getResourcePressureDistribution() const {
    return ResourcePressureDistribution;
  }

  void getResourceUsers(uint64_t ResourceMask,
                        SmallVectorImpl<User> &Users) const;

  unsigned getRegisterPressureCycles(unsigned IID) const {
    assert(IPI.contains(IID) && "Instruction is not tracked!");
    const InstructionPressureInfo &Info = IPI.find(IID)->second;
    return Info.RegisterPressureCycles;
  }

  unsigned getMemoryPressureCycles(unsigned IID) const {
    assert(IPI.contains(IID) && "Instruction is not tracked!");
    const InstructionPressureInfo &Info = IPI.find(IID)->second;
    return Info.MemoryPressureCycles;
  }

  unsigned getResourcePressureCycles(unsigned IID) const {
    assert(IPI.contains(IID) && "Instruction is not tracked!");
    const InstructionPressureInfo &Info = IPI.find(IID)->second;
    return Info.ResourcePressureCycles;
  }

  const char *resolveResourceName(uint64_t ResourceMask) const {
    unsigned Index = getResourceStateIndex(ResourceMask);
    unsigned ProcResID = ResIdx2ProcResID[Index];
    const MCProcResourceDesc &PRDesc = *SM.getProcResource(ProcResID);
    return PRDesc.Name;
  }

  void onInstructionDispatched(unsigned IID);
  void onInstructionExecuted(unsigned IID);

  void handlePressureEvent(const HWPressureEvent &Event);
  void handleInstructionIssuedEvent(const HWInstructionIssuedEvent &Event);
};

// A dependency edge.
struct DependencyEdge {
  enum DependencyType { DT_INVALID, DT_REGISTER, DT_MEMORY, DT_RESOURCE };

  // Dependency edge descriptor.
  //
  // It specifies the dependency type, as well as the edge cost in cycles.
  struct Dependency {
    DependencyType Type;
    uint64_t ResourceOrRegID;
    uint64_t Cost;
  };
  Dependency Dep;

  unsigned FromIID;
  unsigned ToIID;

  // Used by the bottleneck analysis to compute the interference
  // probability for processor resources.
  unsigned Frequency;
};

// A dependency graph used by the bottleneck analysis to describe data
// dependencies and processor resource interferences between instructions.
//
// There is a node (an instance of struct DGNode) for every instruction in the
// input assembly sequence. Edges of the graph represent dependencies between
// instructions.
//
// Each edge of the graph is associated with a cost value which is used
// internally to rank dependency based on their impact on the runtime
// performance (see field DependencyEdge::Dependency::Cost). In general, the
// higher the cost of an edge, the higher the impact on performance.
//
// The cost of a dependency is a function of both the latency and the number of
// cycles where the dependency has been seen as critical (i.e. contributing to
// back-pressure increases).
//
// Loop carried dependencies are carefully expanded by the bottleneck analysis
// to guarantee that the graph stays acyclic. To this end, extra nodes are
// pre-allocated at construction time to describe instructions from "past and
// future" iterations. The graph is kept acyclic mainly because it simplifies
// the complexity of the algorithm that computes the critical sequence.
class DependencyGraph {
  struct DGNode {
    unsigned NumPredecessors;
    unsigned NumVisitedPredecessors;
    uint64_t Cost;
    unsigned Depth;

    DependencyEdge CriticalPredecessor;
    SmallVector<DependencyEdge, 8> OutgoingEdges;
  };
  SmallVector<DGNode, 16> Nodes;

  DependencyGraph(const DependencyGraph &) = delete;
  DependencyGraph &operator=(const DependencyGraph &) = delete;

  void addDependency(unsigned From, unsigned To,
                     DependencyEdge::Dependency &&DE);

  void pruneEdges(unsigned Iterations);
  void initializeRootSet(SmallVectorImpl<unsigned> &RootSet) const;
  void propagateThroughEdges(SmallVectorImpl<unsigned> &RootSet,
                             unsigned Iterations);

#ifndef NDEBUG
  void dumpDependencyEdge(raw_ostream &OS, const DependencyEdge &DE,
                          MCInstPrinter &MCIP) const;
#endif

public:
  DependencyGraph(unsigned Size) : Nodes(Size) {}

  void addRegisterDep(unsigned From, unsigned To, unsigned RegID,
                      unsigned Cost) {
    addDependency(From, To, {DependencyEdge::DT_REGISTER, RegID, Cost});
  }

  void addMemoryDep(unsigned From, unsigned To, unsigned Cost) {
    addDependency(From, To, {DependencyEdge::DT_MEMORY, /* unused */ 0, Cost});
  }

  void addResourceDep(unsigned From, unsigned To, uint64_t Mask,
                      unsigned Cost) {
    addDependency(From, To, {DependencyEdge::DT_RESOURCE, Mask, Cost});
  }

  // Called by the bottleneck analysis at the end of simulation to propagate
  // costs through the edges of the graph, and compute a critical path.
  void finalizeGraph(unsigned Iterations) {
    SmallVector<unsigned, 16> RootSet;
    pruneEdges(Iterations);
    initializeRootSet(RootSet);
    propagateThroughEdges(RootSet, Iterations);
  }

  // Returns a sequence of edges representing the critical sequence based on the
  // simulated run. It assumes that the graph has already been finalized (i.e.
  // method `finalizeGraph()` has already been called on this graph).
  void getCriticalSequence(SmallVectorImpl<const DependencyEdge *> &Seq) const;

#ifndef NDEBUG
  void dump(raw_ostream &OS, MCInstPrinter &MCIP) const;
#endif
};

/// A view that collects and prints a few performance numbers.
class BottleneckAnalysis : public InstructionView {
  PressureTracker Tracker;
  DependencyGraph DG;

  unsigned Iterations;
  unsigned TotalCycles;

  bool PressureIncreasedBecauseOfResources;
  bool PressureIncreasedBecauseOfRegisterDependencies;
  bool PressureIncreasedBecauseOfMemoryDependencies;
  // True if throughput was affected by dispatch stalls.
  bool SeenStallCycles;

  struct BackPressureInfo {
    // Cycles where backpressure increased.
    unsigned PressureIncreaseCycles;
    // Cycles where backpressure increased because of pipeline pressure.
    unsigned ResourcePressureCycles;
    // Cycles where backpressure increased because of data dependencies.
    unsigned DataDependencyCycles;
    // Cycles where backpressure increased because of register dependencies.
    unsigned RegisterDependencyCycles;
    // Cycles where backpressure increased because of memory dependencies.
    unsigned MemoryDependencyCycles;
  };
  BackPressureInfo BPI;

  // Used to populate the dependency graph DG.
  void addRegisterDep(unsigned From, unsigned To, unsigned RegID, unsigned Cy);
  void addMemoryDep(unsigned From, unsigned To, unsigned Cy);
  void addResourceDep(unsigned From, unsigned To, uint64_t Mask, unsigned Cy);

  void printInstruction(formatted_raw_ostream &FOS, const MCInst &MCI,
                        bool UseDifferentColor = false) const;

  // Prints a bottleneck message to OS.
  void printBottleneckHints(raw_ostream &OS) const;
  void printCriticalSequence(raw_ostream &OS) const;

public:
  BottleneckAnalysis(const MCSubtargetInfo &STI, MCInstPrinter &MCIP,
                     ArrayRef<MCInst> Sequence, unsigned Iterations);

  void onCycleEnd() override;
  void onEvent(const HWStallEvent &Event) override { SeenStallCycles = true; }
  void onEvent(const HWPressureEvent &Event) override;
  void onEvent(const HWInstructionEvent &Event) override;

  void printView(raw_ostream &OS) const override;
  StringRef getNameAsString() const override { return "BottleneckAnalysis"; }
  bool isSerializable() const override { return false; }

#ifndef NDEBUG
  void dump(raw_ostream &OS, MCInstPrinter &MCIP) const { DG.dump(OS, MCIP); }
#endif
};

} // namespace mca
} // namespace llvm

#endif
