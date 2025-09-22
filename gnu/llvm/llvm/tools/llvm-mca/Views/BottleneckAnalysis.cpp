//===--------------------- BottleneckAnalysis.cpp ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the functionalities used by the BottleneckAnalysis
/// to report bottleneck info.
///
//===----------------------------------------------------------------------===//

#include "Views/BottleneckAnalysis.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MCA/Support.h"
#include "llvm/Support/Format.h"

namespace llvm {
namespace mca {

#define DEBUG_TYPE "llvm-mca"

PressureTracker::PressureTracker(const MCSchedModel &Model)
    : SM(Model),
      ResourcePressureDistribution(Model.getNumProcResourceKinds(), 0),
      ProcResID2Mask(Model.getNumProcResourceKinds(), 0),
      ResIdx2ProcResID(Model.getNumProcResourceKinds(), 0),
      ProcResID2ResourceUsersIndex(Model.getNumProcResourceKinds(), 0) {
  computeProcResourceMasks(SM, ProcResID2Mask);

  // Ignore the invalid resource at index zero.
  unsigned NextResourceUsersIdx = 0;
  for (unsigned I = 1, E = Model.getNumProcResourceKinds(); I < E; ++I) {
    const MCProcResourceDesc &ProcResource = *SM.getProcResource(I);
    ProcResID2ResourceUsersIndex[I] = NextResourceUsersIdx;
    NextResourceUsersIdx += ProcResource.NumUnits;
    uint64_t ResourceMask = ProcResID2Mask[I];
    ResIdx2ProcResID[getResourceStateIndex(ResourceMask)] = I;
  }

  ResourceUsers.resize(NextResourceUsersIdx);
  std::fill(ResourceUsers.begin(), ResourceUsers.end(),
            std::make_pair<unsigned, unsigned>(~0U, 0U));
}

void PressureTracker::getResourceUsers(uint64_t ResourceMask,
                                       SmallVectorImpl<User> &Users) const {
  unsigned Index = getResourceStateIndex(ResourceMask);
  unsigned ProcResID = ResIdx2ProcResID[Index];
  const MCProcResourceDesc &PRDesc = *SM.getProcResource(ProcResID);
  for (unsigned I = 0, E = PRDesc.NumUnits; I < E; ++I) {
    const User U = getResourceUser(ProcResID, I);
    if (U.second && IPI.contains(U.first))
      Users.emplace_back(U);
  }
}

void PressureTracker::onInstructionDispatched(unsigned IID) {
  IPI.insert(std::make_pair(IID, InstructionPressureInfo()));
}

void PressureTracker::onInstructionExecuted(unsigned IID) { IPI.erase(IID); }

void PressureTracker::handleInstructionIssuedEvent(
    const HWInstructionIssuedEvent &Event) {
  unsigned IID = Event.IR.getSourceIndex();
  for (const ResourceUse &Use : Event.UsedResources) {
    const ResourceRef &RR = Use.first;
    unsigned Index = ProcResID2ResourceUsersIndex[RR.first];
    Index += llvm::countr_zero(RR.second);
    ResourceUsers[Index] = std::make_pair(IID, Use.second.getNumerator());
  }
}

void PressureTracker::updateResourcePressureDistribution(
    uint64_t CumulativeMask) {
  while (CumulativeMask) {
    uint64_t Current = CumulativeMask & (-CumulativeMask);
    unsigned ResIdx = getResourceStateIndex(Current);
    unsigned ProcResID = ResIdx2ProcResID[ResIdx];
    uint64_t Mask = ProcResID2Mask[ProcResID];

    if (Mask == Current) {
      ResourcePressureDistribution[ProcResID]++;
      CumulativeMask ^= Current;
      continue;
    }

    Mask ^= Current;
    while (Mask) {
      uint64_t SubUnit = Mask & (-Mask);
      ResIdx = getResourceStateIndex(SubUnit);
      ProcResID = ResIdx2ProcResID[ResIdx];
      ResourcePressureDistribution[ProcResID]++;
      Mask ^= SubUnit;
    }

    CumulativeMask ^= Current;
  }
}

void PressureTracker::handlePressureEvent(const HWPressureEvent &Event) {
  assert(Event.Reason != HWPressureEvent::INVALID &&
         "Unexpected invalid event!");

  switch (Event.Reason) {
  default:
    break;

  case HWPressureEvent::RESOURCES: {
    const uint64_t ResourceMask = Event.ResourceMask;
    updateResourcePressureDistribution(Event.ResourceMask);

    for (const InstRef &IR : Event.AffectedInstructions) {
      const Instruction &IS = *IR.getInstruction();
      unsigned BusyResources = IS.getCriticalResourceMask() & ResourceMask;
      if (!BusyResources)
        continue;

      unsigned IID = IR.getSourceIndex();
      IPI[IID].ResourcePressureCycles++;
    }
    break;
  }

  case HWPressureEvent::REGISTER_DEPS:
    for (const InstRef &IR : Event.AffectedInstructions) {
      unsigned IID = IR.getSourceIndex();
      IPI[IID].RegisterPressureCycles++;
    }
    break;

  case HWPressureEvent::MEMORY_DEPS:
    for (const InstRef &IR : Event.AffectedInstructions) {
      unsigned IID = IR.getSourceIndex();
      IPI[IID].MemoryPressureCycles++;
    }
  }
}

#ifndef NDEBUG
void DependencyGraph::dumpDependencyEdge(raw_ostream &OS,
                                         const DependencyEdge &DepEdge,
                                         MCInstPrinter &MCIP) const {
  unsigned FromIID = DepEdge.FromIID;
  unsigned ToIID = DepEdge.ToIID;
  assert(FromIID < ToIID && "Graph should be acyclic!");

  const DependencyEdge::Dependency &DE = DepEdge.Dep;
  assert(DE.Type != DependencyEdge::DT_INVALID && "Unexpected invalid edge!");

  OS << " FROM: " << FromIID << " TO: " << ToIID << "             ";
  if (DE.Type == DependencyEdge::DT_REGISTER) {
    OS << " - REGISTER: ";
    MCIP.printRegName(OS, DE.ResourceOrRegID);
  } else if (DE.Type == DependencyEdge::DT_MEMORY) {
    OS << " - MEMORY";
  } else {
    assert(DE.Type == DependencyEdge::DT_RESOURCE &&
           "Unsupported dependency type!");
    OS << " - RESOURCE MASK: " << DE.ResourceOrRegID;
  }
  OS << " - COST: " << DE.Cost << '\n';
}
#endif // NDEBUG

void DependencyGraph::pruneEdges(unsigned Iterations) {
  for (DGNode &N : Nodes) {
    unsigned NumPruned = 0;
    const unsigned Size = N.OutgoingEdges.size();
    // Use a cut-off threshold to prune edges with a low frequency.
    for (unsigned I = 0, E = Size; I < E; ++I) {
      DependencyEdge &Edge = N.OutgoingEdges[I];
      if (Edge.Frequency == Iterations)
        continue;
      double Factor = (double)Edge.Frequency / Iterations;
      if (0.10 < Factor)
        continue;
      Nodes[Edge.ToIID].NumPredecessors--;
      std::swap(Edge, N.OutgoingEdges[E - 1]);
      --E;
      ++NumPruned;
    }

    if (NumPruned)
      N.OutgoingEdges.resize(Size - NumPruned);
  }
}

void DependencyGraph::initializeRootSet(
    SmallVectorImpl<unsigned> &RootSet) const {
  for (unsigned I = 0, E = Nodes.size(); I < E; ++I) {
    const DGNode &N = Nodes[I];
    if (N.NumPredecessors == 0 && !N.OutgoingEdges.empty())
      RootSet.emplace_back(I);
  }
}

void DependencyGraph::propagateThroughEdges(SmallVectorImpl<unsigned> &RootSet,
                                            unsigned Iterations) {
  SmallVector<unsigned, 8> ToVisit;

  // A critical sequence is computed as the longest path from a node of the
  // RootSet to a leaf node (i.e. a node with no successors).  The RootSet is
  // composed of nodes with at least one successor, and no predecessors.
  //
  // Each node of the graph starts with an initial default cost of zero.  The
  // cost of a node is a measure of criticality: the higher the cost, the bigger
  // is the performance impact.
  // For register and memory dependencies, the cost is a function of the write
  // latency as well as the actual delay (in cycles) caused to users.
  // For processor resource dependencies, the cost is a function of the resource
  // pressure. Resource interferences with low frequency values are ignored.
  //
  // This algorithm is very similar to a (reverse) Dijkstra.  Every iteration of
  // the inner loop selects (i.e. visits) a node N from a set of `unvisited
  // nodes`, and then propagates the cost of N to all its neighbors.
  //
  // The `unvisited nodes` set initially contains all the nodes from the
  // RootSet.  A node N is added to the `unvisited nodes` if all its
  // predecessors have been visited already.
  //
  // For simplicity, every node tracks the number of unvisited incoming edges in
  // field `NumVisitedPredecessors`.  When the value of that field drops to
  // zero, then the corresponding node is added to a `ToVisit` set.
  //
  // At the end of every iteration of the outer loop, set `ToVisit` becomes our
  // new `unvisited nodes` set.
  //
  // The algorithm terminates when the set of unvisited nodes (i.e. our RootSet)
  // is empty. This algorithm works under the assumption that the graph is
  // acyclic.
  do {
    for (unsigned IID : RootSet) {
      const DGNode &N = Nodes[IID];
      for (const DependencyEdge &DepEdge : N.OutgoingEdges) {
        unsigned ToIID = DepEdge.ToIID;
        DGNode &To = Nodes[ToIID];
        uint64_t Cost = N.Cost + DepEdge.Dep.Cost;
        // Check if this is the most expensive incoming edge seen so far.  In
        // case, update the total cost of the destination node (ToIID), as well
        // its field `CriticalPredecessor`.
        if (Cost > To.Cost) {
          To.CriticalPredecessor = DepEdge;
          To.Cost = Cost;
          To.Depth = N.Depth + 1;
        }
        To.NumVisitedPredecessors++;
        if (To.NumVisitedPredecessors == To.NumPredecessors)
          ToVisit.emplace_back(ToIID);
      }
    }

    std::swap(RootSet, ToVisit);
    ToVisit.clear();
  } while (!RootSet.empty());
}

void DependencyGraph::getCriticalSequence(
    SmallVectorImpl<const DependencyEdge *> &Seq) const {
  // At this stage, nodes of the graph have been already visited, and costs have
  // been propagated through the edges (see method `propagateThroughEdges()`).

  // Identify the node N with the highest cost in the graph. By construction,
  // that node is the last instruction of our critical sequence.
  // Field N.Depth would tell us the total length of the sequence.
  //
  // To obtain the sequence of critical edges, we simply follow the chain of
  // critical predecessors starting from node N (field
  // DGNode::CriticalPredecessor).
  const auto It =
      llvm::max_element(Nodes, [](const DGNode &Lhs, const DGNode &Rhs) {
        return Lhs.Cost < Rhs.Cost;
      });
  unsigned IID = std::distance(Nodes.begin(), It);
  Seq.resize(Nodes[IID].Depth);
  for (const DependencyEdge *&DE : llvm::reverse(Seq)) {
    const DGNode &N = Nodes[IID];
    DE = &N.CriticalPredecessor;
    IID = N.CriticalPredecessor.FromIID;
  }
}

void BottleneckAnalysis::printInstruction(formatted_raw_ostream &FOS,
                                          const MCInst &MCI,
                                          bool UseDifferentColor) const {
  FOS.PadToColumn(14);
  if (UseDifferentColor)
    FOS.changeColor(raw_ostream::CYAN, true, false);
  FOS << printInstructionString(MCI);
  if (UseDifferentColor)
    FOS.resetColor();
}

void BottleneckAnalysis::printCriticalSequence(raw_ostream &OS) const {
  // Early exit if no bottlenecks were found during the simulation.
  if (!SeenStallCycles || !BPI.PressureIncreaseCycles)
    return;

  SmallVector<const DependencyEdge *, 16> Seq;
  DG.getCriticalSequence(Seq);
  if (Seq.empty())
    return;

  OS << "\nCritical sequence based on the simulation:\n\n";

  const DependencyEdge &FirstEdge = *Seq[0];
  ArrayRef<llvm::MCInst> Source = getSource();
  unsigned FromIID = FirstEdge.FromIID % Source.size();
  unsigned ToIID = FirstEdge.ToIID % Source.size();
  bool IsLoopCarried = FromIID >= ToIID;

  formatted_raw_ostream FOS(OS);
  FOS.PadToColumn(14);
  FOS << "Instruction";
  FOS.PadToColumn(58);
  FOS << "Dependency Information";

  bool HasColors = FOS.has_colors();

  unsigned CurrentIID = 0;
  if (IsLoopCarried) {
    FOS << "\n +----< " << FromIID << ".";
    printInstruction(FOS, Source[FromIID], HasColors);
    FOS << "\n |\n |    < loop carried > \n |";
  } else {
    while (CurrentIID < FromIID) {
      FOS << "\n        " << CurrentIID << ".";
      printInstruction(FOS, Source[CurrentIID]);
      CurrentIID++;
    }

    FOS << "\n +----< " << CurrentIID << ".";
    printInstruction(FOS, Source[CurrentIID], HasColors);
    CurrentIID++;
  }

  for (const DependencyEdge *&DE : Seq) {
    ToIID = DE->ToIID % Source.size();
    unsigned LastIID = CurrentIID > ToIID ? Source.size() : ToIID;

    while (CurrentIID < LastIID) {
      FOS << "\n |      " << CurrentIID << ".";
      printInstruction(FOS, Source[CurrentIID]);
      CurrentIID++;
    }

    if (CurrentIID == ToIID) {
      FOS << "\n +----> " << ToIID << ".";
      printInstruction(FOS, Source[CurrentIID], HasColors);
    } else {
      FOS << "\n |\n |    < loop carried > \n |"
          << "\n +----> " << ToIID << ".";
      printInstruction(FOS, Source[ToIID], HasColors);
    }
    FOS.PadToColumn(58);

    const DependencyEdge::Dependency &Dep = DE->Dep;
    if (HasColors)
      FOS.changeColor(raw_ostream::SAVEDCOLOR, true, false);

    if (Dep.Type == DependencyEdge::DT_REGISTER) {
      FOS << "## REGISTER dependency:  ";
      if (HasColors)
        FOS.changeColor(raw_ostream::MAGENTA, true, false);
      getInstPrinter().printRegName(FOS, Dep.ResourceOrRegID);
    } else if (Dep.Type == DependencyEdge::DT_MEMORY) {
      FOS << "## MEMORY dependency.";
    } else {
      assert(Dep.Type == DependencyEdge::DT_RESOURCE &&
             "Unsupported dependency type!");
      FOS << "## RESOURCE interference:  ";
      if (HasColors)
        FOS.changeColor(raw_ostream::MAGENTA, true, false);
      FOS << Tracker.resolveResourceName(Dep.ResourceOrRegID);
      if (HasColors) {
        FOS.resetColor();
        FOS.changeColor(raw_ostream::SAVEDCOLOR, true, false);
      }
      FOS << " [ probability: " << ((DE->Frequency * 100) / Iterations)
          << "% ]";
    }
    if (HasColors)
      FOS.resetColor();
    ++CurrentIID;
  }

  while (CurrentIID < Source.size()) {
    FOS << "\n        " << CurrentIID << ".";
    printInstruction(FOS, Source[CurrentIID]);
    CurrentIID++;
  }

  FOS << '\n';
  FOS.flush();
}

#ifndef NDEBUG
void DependencyGraph::dump(raw_ostream &OS, MCInstPrinter &MCIP) const {
  OS << "\nREG DEPS\n";
  for (const DGNode &Node : Nodes)
    for (const DependencyEdge &DE : Node.OutgoingEdges)
      if (DE.Dep.Type == DependencyEdge::DT_REGISTER)
        dumpDependencyEdge(OS, DE, MCIP);

  OS << "\nMEM DEPS\n";
  for (const DGNode &Node : Nodes)
    for (const DependencyEdge &DE : Node.OutgoingEdges)
      if (DE.Dep.Type == DependencyEdge::DT_MEMORY)
        dumpDependencyEdge(OS, DE, MCIP);

  OS << "\nRESOURCE DEPS\n";
  for (const DGNode &Node : Nodes)
    for (const DependencyEdge &DE : Node.OutgoingEdges)
      if (DE.Dep.Type == DependencyEdge::DT_RESOURCE)
        dumpDependencyEdge(OS, DE, MCIP);
}
#endif // NDEBUG

void DependencyGraph::addDependency(unsigned From, unsigned To,
                                    DependencyEdge::Dependency &&Dep) {
  DGNode &NodeFrom = Nodes[From];
  DGNode &NodeTo = Nodes[To];
  SmallVectorImpl<DependencyEdge> &Vec = NodeFrom.OutgoingEdges;

  auto It = find_if(Vec, [To, Dep](DependencyEdge &DE) {
    return DE.ToIID == To && DE.Dep.ResourceOrRegID == Dep.ResourceOrRegID;
  });

  if (It != Vec.end()) {
    It->Dep.Cost += Dep.Cost;
    It->Frequency++;
    return;
  }

  DependencyEdge DE = {Dep, From, To, 1};
  Vec.emplace_back(DE);
  NodeTo.NumPredecessors++;
}

BottleneckAnalysis::BottleneckAnalysis(const MCSubtargetInfo &sti,
                                       MCInstPrinter &Printer,
                                       ArrayRef<MCInst> S, unsigned NumIter)
    : InstructionView(sti, Printer, S), Tracker(sti.getSchedModel()),
      DG(S.size() * 3), Iterations(NumIter), TotalCycles(0),
      PressureIncreasedBecauseOfResources(false),
      PressureIncreasedBecauseOfRegisterDependencies(false),
      PressureIncreasedBecauseOfMemoryDependencies(false),
      SeenStallCycles(false), BPI() {}

void BottleneckAnalysis::addRegisterDep(unsigned From, unsigned To,
                                        unsigned RegID, unsigned Cost) {
  bool IsLoopCarried = From >= To;
  unsigned SourceSize = getSource().size();
  if (IsLoopCarried) {
    DG.addRegisterDep(From, To + SourceSize, RegID, Cost);
    DG.addRegisterDep(From + SourceSize, To + (SourceSize * 2), RegID, Cost);
    return;
  }
  DG.addRegisterDep(From + SourceSize, To + SourceSize, RegID, Cost);
}

void BottleneckAnalysis::addMemoryDep(unsigned From, unsigned To,
                                      unsigned Cost) {
  bool IsLoopCarried = From >= To;
  unsigned SourceSize = getSource().size();
  if (IsLoopCarried) {
    DG.addMemoryDep(From, To + SourceSize, Cost);
    DG.addMemoryDep(From + SourceSize, To + (SourceSize * 2), Cost);
    return;
  }
  DG.addMemoryDep(From + SourceSize, To + SourceSize, Cost);
}

void BottleneckAnalysis::addResourceDep(unsigned From, unsigned To,
                                        uint64_t Mask, unsigned Cost) {
  bool IsLoopCarried = From >= To;
  unsigned SourceSize = getSource().size();
  if (IsLoopCarried) {
    DG.addResourceDep(From, To + SourceSize, Mask, Cost);
    DG.addResourceDep(From + SourceSize, To + (SourceSize * 2), Mask, Cost);
    return;
  }
  DG.addResourceDep(From + SourceSize, To + SourceSize, Mask, Cost);
}

void BottleneckAnalysis::onEvent(const HWInstructionEvent &Event) {
  const unsigned IID = Event.IR.getSourceIndex();
  if (Event.Type == HWInstructionEvent::Dispatched) {
    Tracker.onInstructionDispatched(IID);
    return;
  }
  if (Event.Type == HWInstructionEvent::Executed) {
    Tracker.onInstructionExecuted(IID);
    return;
  }

  if (Event.Type != HWInstructionEvent::Issued)
    return;

  ArrayRef<llvm::MCInst> Source = getSource();
  const Instruction &IS = *Event.IR.getInstruction();
  unsigned To = IID % Source.size();

  unsigned Cycles = 2 * Tracker.getResourcePressureCycles(IID);
  uint64_t ResourceMask = IS.getCriticalResourceMask();
  SmallVector<std::pair<unsigned, unsigned>, 4> Users;
  while (ResourceMask) {
    uint64_t Current = ResourceMask & (-ResourceMask);
    Tracker.getResourceUsers(Current, Users);
    for (const std::pair<unsigned, unsigned> &U : Users)
      addResourceDep(U.first % Source.size(), To, Current, U.second + Cycles);
    Users.clear();
    ResourceMask ^= Current;
  }

  const CriticalDependency &RegDep = IS.getCriticalRegDep();
  if (RegDep.Cycles) {
    Cycles = RegDep.Cycles + 2 * Tracker.getRegisterPressureCycles(IID);
    unsigned From = RegDep.IID % Source.size();
    addRegisterDep(From, To, RegDep.RegID, Cycles);
  }

  const CriticalDependency &MemDep = IS.getCriticalMemDep();
  if (MemDep.Cycles) {
    Cycles = MemDep.Cycles + 2 * Tracker.getMemoryPressureCycles(IID);
    unsigned From = MemDep.IID % Source.size();
    addMemoryDep(From, To, Cycles);
  }

  Tracker.handleInstructionIssuedEvent(
      static_cast<const HWInstructionIssuedEvent &>(Event));

  // Check if this is the last simulated instruction.
  if (IID == ((Iterations * Source.size()) - 1))
    DG.finalizeGraph(Iterations);
}

void BottleneckAnalysis::onEvent(const HWPressureEvent &Event) {
  assert(Event.Reason != HWPressureEvent::INVALID &&
         "Unexpected invalid event!");

  Tracker.handlePressureEvent(Event);

  switch (Event.Reason) {
  default:
    break;

  case HWPressureEvent::RESOURCES:
    PressureIncreasedBecauseOfResources = true;
    break;
  case HWPressureEvent::REGISTER_DEPS:
    PressureIncreasedBecauseOfRegisterDependencies = true;
    break;
  case HWPressureEvent::MEMORY_DEPS:
    PressureIncreasedBecauseOfMemoryDependencies = true;
    break;
  }
}

void BottleneckAnalysis::onCycleEnd() {
  ++TotalCycles;

  bool PressureIncreasedBecauseOfDataDependencies =
      PressureIncreasedBecauseOfRegisterDependencies ||
      PressureIncreasedBecauseOfMemoryDependencies;
  if (!PressureIncreasedBecauseOfResources &&
      !PressureIncreasedBecauseOfDataDependencies)
    return;

  ++BPI.PressureIncreaseCycles;
  if (PressureIncreasedBecauseOfRegisterDependencies)
    ++BPI.RegisterDependencyCycles;
  if (PressureIncreasedBecauseOfMemoryDependencies)
    ++BPI.MemoryDependencyCycles;
  if (PressureIncreasedBecauseOfDataDependencies)
    ++BPI.DataDependencyCycles;
  if (PressureIncreasedBecauseOfResources)
    ++BPI.ResourcePressureCycles;
  PressureIncreasedBecauseOfResources = false;
  PressureIncreasedBecauseOfRegisterDependencies = false;
  PressureIncreasedBecauseOfMemoryDependencies = false;
}

void BottleneckAnalysis::printBottleneckHints(raw_ostream &OS) const {
  if (!SeenStallCycles || !BPI.PressureIncreaseCycles) {
    OS << "\n\nNo resource or data dependency bottlenecks discovered.\n";
    return;
  }

  double PressurePerCycle =
      (double)BPI.PressureIncreaseCycles * 100 / TotalCycles;
  double ResourcePressurePerCycle =
      (double)BPI.ResourcePressureCycles * 100 / TotalCycles;
  double DDPerCycle = (double)BPI.DataDependencyCycles * 100 / TotalCycles;
  double RegDepPressurePerCycle =
      (double)BPI.RegisterDependencyCycles * 100 / TotalCycles;
  double MemDepPressurePerCycle =
      (double)BPI.MemoryDependencyCycles * 100 / TotalCycles;

  OS << "\n\nCycles with backend pressure increase [ "
     << format("%.2f", floor((PressurePerCycle * 100) + 0.5) / 100) << "% ]";

  OS << "\nThroughput Bottlenecks: "
     << "\n  Resource Pressure       [ "
     << format("%.2f", floor((ResourcePressurePerCycle * 100) + 0.5) / 100)
     << "% ]";

  if (BPI.PressureIncreaseCycles) {
    ArrayRef<unsigned> Distribution = Tracker.getResourcePressureDistribution();
    const MCSchedModel &SM = getSubTargetInfo().getSchedModel();
    for (unsigned I = 0, E = Distribution.size(); I < E; ++I) {
      unsigned ReleaseAtCycles = Distribution[I];
      if (ReleaseAtCycles) {
        double Frequency = (double)ReleaseAtCycles * 100 / TotalCycles;
        const MCProcResourceDesc &PRDesc = *SM.getProcResource(I);
        OS << "\n  - " << PRDesc.Name << "  [ "
           << format("%.2f", floor((Frequency * 100) + 0.5) / 100) << "% ]";
      }
    }
  }

  OS << "\n  Data Dependencies:      [ "
     << format("%.2f", floor((DDPerCycle * 100) + 0.5) / 100) << "% ]";
  OS << "\n  - Register Dependencies [ "
     << format("%.2f", floor((RegDepPressurePerCycle * 100) + 0.5) / 100)
     << "% ]";
  OS << "\n  - Memory Dependencies   [ "
     << format("%.2f", floor((MemDepPressurePerCycle * 100) + 0.5) / 100)
     << "% ]\n";
}

void BottleneckAnalysis::printView(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);
  printBottleneckHints(TempStream);
  TempStream.flush();
  OS << Buffer;
  printCriticalSequence(OS);
}

} // namespace mca.
} // namespace llvm
