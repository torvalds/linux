//==-- X86LoadValueInjectionLoadHardening.cpp - LVI load hardening for x86 --=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Description: This pass finds Load Value Injection (LVI) gadgets consisting
/// of a load from memory (i.e., SOURCE), and any operation that may transmit
/// the value loaded from memory over a covert channel, or use the value loaded
/// from memory to determine a branch/call target (i.e., SINK). After finding
/// all such gadgets in a given function, the pass minimally inserts LFENCE
/// instructions in such a manner that the following property is satisfied: for
/// all SOURCE+SINK pairs, all paths in the CFG from SOURCE to SINK contain at
/// least one LFENCE instruction. The algorithm that implements this minimal
/// insertion is influenced by an academic paper that minimally inserts memory
/// fences for high-performance concurrent programs:
///         http://www.cs.ucr.edu/~lesani/companion/oopsla15/OOPSLA15.pdf
/// The algorithm implemented in this pass is as follows:
/// 1. Build a condensed CFG (i.e., a GadgetGraph) consisting only of the
/// following components:
///    - SOURCE instructions (also includes function arguments)
///    - SINK instructions
///    - Basic block entry points
///    - Basic block terminators
///    - LFENCE instructions
/// 2. Analyze the GadgetGraph to determine which SOURCE+SINK pairs (i.e.,
/// gadgets) are already mitigated by existing LFENCEs. If all gadgets have been
/// mitigated, go to step 6.
/// 3. Use a heuristic or plugin to approximate minimal LFENCE insertion.
/// 4. Insert one LFENCE along each CFG edge that was cut in step 3.
/// 5. Go to step 2.
/// 6. If any LFENCEs were inserted, return `true` from runOnMachineFunction()
/// to tell LLVM that the function was modified.
///
//===----------------------------------------------------------------------===//

#include "ImmutableGraph.h"
#include "X86.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominanceFrontier.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RDFGraph.h"
#include "llvm/CodeGen/RDFLiveness.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define PASS_KEY "x86-lvi-load"
#define DEBUG_TYPE PASS_KEY

STATISTIC(NumFences, "Number of LFENCEs inserted for LVI mitigation");
STATISTIC(NumFunctionsConsidered, "Number of functions analyzed");
STATISTIC(NumFunctionsMitigated, "Number of functions for which mitigations "
                                 "were deployed");
STATISTIC(NumGadgets, "Number of LVI gadgets detected during analysis");

static cl::opt<std::string> OptimizePluginPath(
    PASS_KEY "-opt-plugin",
    cl::desc("Specify a plugin to optimize LFENCE insertion"), cl::Hidden);

static cl::opt<bool> NoConditionalBranches(
    PASS_KEY "-no-cbranch",
    cl::desc("Don't treat conditional branches as disclosure gadgets. This "
             "may improve performance, at the cost of security."),
    cl::init(false), cl::Hidden);

static cl::opt<bool> EmitDot(
    PASS_KEY "-dot",
    cl::desc(
        "For each function, emit a dot graph depicting potential LVI gadgets"),
    cl::init(false), cl::Hidden);

static cl::opt<bool> EmitDotOnly(
    PASS_KEY "-dot-only",
    cl::desc("For each function, emit a dot graph depicting potential LVI "
             "gadgets, and do not insert any fences"),
    cl::init(false), cl::Hidden);

static cl::opt<bool> EmitDotVerify(
    PASS_KEY "-dot-verify",
    cl::desc("For each function, emit a dot graph to stdout depicting "
             "potential LVI gadgets, used for testing purposes only"),
    cl::init(false), cl::Hidden);

static llvm::sys::DynamicLibrary OptimizeDL;
typedef int (*OptimizeCutT)(unsigned int *Nodes, unsigned int NodesSize,
                            unsigned int *Edges, int *EdgeValues,
                            int *CutEdges /* out */, unsigned int EdgesSize);
static OptimizeCutT OptimizeCut = nullptr;

namespace {

struct MachineGadgetGraph : ImmutableGraph<MachineInstr *, int> {
  static constexpr int GadgetEdgeSentinel = -1;
  static constexpr MachineInstr *const ArgNodeSentinel = nullptr;

  using GraphT = ImmutableGraph<MachineInstr *, int>;
  using Node = typename GraphT::Node;
  using Edge = typename GraphT::Edge;
  using size_type = typename GraphT::size_type;
  MachineGadgetGraph(std::unique_ptr<Node[]> Nodes,
                     std::unique_ptr<Edge[]> Edges, size_type NodesSize,
                     size_type EdgesSize, int NumFences = 0, int NumGadgets = 0)
      : GraphT(std::move(Nodes), std::move(Edges), NodesSize, EdgesSize),
        NumFences(NumFences), NumGadgets(NumGadgets) {}
  static inline bool isCFGEdge(const Edge &E) {
    return E.getValue() != GadgetEdgeSentinel;
  }
  static inline bool isGadgetEdge(const Edge &E) {
    return E.getValue() == GadgetEdgeSentinel;
  }
  int NumFences;
  int NumGadgets;
};

class X86LoadValueInjectionLoadHardeningPass : public MachineFunctionPass {
public:
  X86LoadValueInjectionLoadHardeningPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "X86 Load Value Injection (LVI) Load Hardening";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;

  static char ID;

private:
  using GraphBuilder = ImmutableGraphBuilder<MachineGadgetGraph>;
  using Edge = MachineGadgetGraph::Edge;
  using Node = MachineGadgetGraph::Node;
  using EdgeSet = MachineGadgetGraph::EdgeSet;
  using NodeSet = MachineGadgetGraph::NodeSet;

  const X86Subtarget *STI = nullptr;
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

  std::unique_ptr<MachineGadgetGraph>
  getGadgetGraph(MachineFunction &MF, const MachineLoopInfo &MLI,
                 const MachineDominatorTree &MDT,
                 const MachineDominanceFrontier &MDF) const;
  int hardenLoadsWithPlugin(MachineFunction &MF,
                            std::unique_ptr<MachineGadgetGraph> Graph) const;
  int hardenLoadsWithHeuristic(MachineFunction &MF,
                               std::unique_ptr<MachineGadgetGraph> Graph) const;
  int elimMitigatedEdgesAndNodes(MachineGadgetGraph &G,
                                 EdgeSet &ElimEdges /* in, out */,
                                 NodeSet &ElimNodes /* in, out */) const;
  std::unique_ptr<MachineGadgetGraph>
  trimMitigatedEdges(std::unique_ptr<MachineGadgetGraph> Graph) const;
  int insertFences(MachineFunction &MF, MachineGadgetGraph &G,
                   EdgeSet &CutEdges /* in, out */) const;
  bool instrUsesRegToAccessMemory(const MachineInstr &I, unsigned Reg) const;
  bool instrUsesRegToBranch(const MachineInstr &I, unsigned Reg) const;
  inline bool isFence(const MachineInstr *MI) const {
    return MI && (MI->getOpcode() == X86::LFENCE ||
                  (STI->useLVIControlFlowIntegrity() && MI->isCall()));
  }
};

} // end anonymous namespace

namespace llvm {

template <>
struct GraphTraits<MachineGadgetGraph *>
    : GraphTraits<ImmutableGraph<MachineInstr *, int> *> {};

template <>
struct DOTGraphTraits<MachineGadgetGraph *> : DefaultDOTGraphTraits {
  using GraphType = MachineGadgetGraph;
  using Traits = llvm::GraphTraits<GraphType *>;
  using NodeRef = typename Traits::NodeRef;
  using EdgeRef = typename Traits::EdgeRef;
  using ChildIteratorType = typename Traits::ChildIteratorType;
  using ChildEdgeIteratorType = typename Traits::ChildEdgeIteratorType;

  DOTGraphTraits(bool IsSimple = false) : DefaultDOTGraphTraits(IsSimple) {}

  std::string getNodeLabel(NodeRef Node, GraphType *) {
    if (Node->getValue() == MachineGadgetGraph::ArgNodeSentinel)
      return "ARGS";

    std::string Str;
    raw_string_ostream OS(Str);
    OS << *Node->getValue();
    return OS.str();
  }

  static std::string getNodeAttributes(NodeRef Node, GraphType *) {
    MachineInstr *MI = Node->getValue();
    if (MI == MachineGadgetGraph::ArgNodeSentinel)
      return "color = blue";
    if (MI->getOpcode() == X86::LFENCE)
      return "color = green";
    return "";
  }

  static std::string getEdgeAttributes(NodeRef, ChildIteratorType E,
                                       GraphType *) {
    int EdgeVal = (*E.getCurrent()).getValue();
    return EdgeVal >= 0 ? "label = " + std::to_string(EdgeVal)
                        : "color = red, style = \"dashed\"";
  }
};

} // end namespace llvm

constexpr MachineInstr *MachineGadgetGraph::ArgNodeSentinel;
constexpr int MachineGadgetGraph::GadgetEdgeSentinel;

char X86LoadValueInjectionLoadHardeningPass::ID = 0;

void X86LoadValueInjectionLoadHardeningPass::getAnalysisUsage(
    AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
  AU.addRequired<MachineLoopInfoWrapperPass>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addRequired<MachineDominanceFrontier>();
  AU.setPreservesCFG();
}

static void writeGadgetGraph(raw_ostream &OS, MachineFunction &MF,
                             MachineGadgetGraph *G) {
  WriteGraph(OS, G, /*ShortNames*/ false,
             "Speculative gadgets for \"" + MF.getName() + "\" function");
}

bool X86LoadValueInjectionLoadHardeningPass::runOnMachineFunction(
    MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "***** " << getPassName() << " : " << MF.getName()
                    << " *****\n");
  STI = &MF.getSubtarget<X86Subtarget>();
  if (!STI->useLVILoadHardening())
    return false;

  // FIXME: support 32-bit
  if (!STI->is64Bit())
    report_fatal_error("LVI load hardening is only supported on 64-bit", false);

  // Don't skip functions with the "optnone" attr but participate in opt-bisect.
  const Function &F = MF.getFunction();
  if (!F.hasOptNone() && skipFunction(F))
    return false;

  ++NumFunctionsConsidered;
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  LLVM_DEBUG(dbgs() << "Building gadget graph...\n");
  const auto &MLI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  const auto &MDT = getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  const auto &MDF = getAnalysis<MachineDominanceFrontier>();
  std::unique_ptr<MachineGadgetGraph> Graph = getGadgetGraph(MF, MLI, MDT, MDF);
  LLVM_DEBUG(dbgs() << "Building gadget graph... Done\n");
  if (Graph == nullptr)
    return false; // didn't find any gadgets

  if (EmitDotVerify) {
    writeGadgetGraph(outs(), MF, Graph.get());
    return false;
  }

  if (EmitDot || EmitDotOnly) {
    LLVM_DEBUG(dbgs() << "Emitting gadget graph...\n");
    std::error_code FileError;
    std::string FileName = "lvi.";
    FileName += MF.getName();
    FileName += ".dot";
    raw_fd_ostream FileOut(FileName, FileError);
    if (FileError)
      errs() << FileError.message();
    writeGadgetGraph(FileOut, MF, Graph.get());
    FileOut.close();
    LLVM_DEBUG(dbgs() << "Emitting gadget graph... Done\n");
    if (EmitDotOnly)
      return false;
  }

  int FencesInserted;
  if (!OptimizePluginPath.empty()) {
    if (!OptimizeDL.isValid()) {
      std::string ErrorMsg;
      OptimizeDL = llvm::sys::DynamicLibrary::getPermanentLibrary(
          OptimizePluginPath.c_str(), &ErrorMsg);
      if (!ErrorMsg.empty())
        report_fatal_error(Twine("Failed to load opt plugin: \"") + ErrorMsg +
                           "\"");
      OptimizeCut = (OptimizeCutT)OptimizeDL.getAddressOfSymbol("optimize_cut");
      if (!OptimizeCut)
        report_fatal_error("Invalid optimization plugin");
    }
    FencesInserted = hardenLoadsWithPlugin(MF, std::move(Graph));
  } else { // Use the default greedy heuristic
    FencesInserted = hardenLoadsWithHeuristic(MF, std::move(Graph));
  }

  if (FencesInserted > 0)
    ++NumFunctionsMitigated;
  NumFences += FencesInserted;
  return (FencesInserted > 0);
}

std::unique_ptr<MachineGadgetGraph>
X86LoadValueInjectionLoadHardeningPass::getGadgetGraph(
    MachineFunction &MF, const MachineLoopInfo &MLI,
    const MachineDominatorTree &MDT,
    const MachineDominanceFrontier &MDF) const {
  using namespace rdf;

  // Build the Register Dataflow Graph using the RDF framework
  DataFlowGraph DFG{MF, *TII, *TRI, MDT, MDF};
  DFG.build();
  Liveness L{MF.getRegInfo(), DFG};
  L.computePhiInfo();

  GraphBuilder Builder;
  using GraphIter = typename GraphBuilder::BuilderNodeRef;
  DenseMap<MachineInstr *, GraphIter> NodeMap;
  int FenceCount = 0, GadgetCount = 0;
  auto MaybeAddNode = [&NodeMap, &Builder](MachineInstr *MI) {
    auto Ref = NodeMap.find(MI);
    if (Ref == NodeMap.end()) {
      auto I = Builder.addVertex(MI);
      NodeMap[MI] = I;
      return std::pair<GraphIter, bool>{I, true};
    }
    return std::pair<GraphIter, bool>{Ref->getSecond(), false};
  };

  // The `Transmitters` map memoizes transmitters found for each def. If a def
  // has not yet been analyzed, then it will not appear in the map. If a def
  // has been analyzed and was determined not to have any transmitters, then
  // its list of transmitters will be empty.
  DenseMap<NodeId, std::vector<NodeId>> Transmitters;

  // Analyze all machine instructions to find gadgets and LFENCEs, adding
  // each interesting value to `Nodes`
  auto AnalyzeDef = [&](NodeAddr<DefNode *> SourceDef) {
    SmallSet<NodeId, 8> UsesVisited, DefsVisited;
    std::function<void(NodeAddr<DefNode *>)> AnalyzeDefUseChain =
        [&](NodeAddr<DefNode *> Def) {
          if (Transmitters.contains(Def.Id))
            return; // Already analyzed `Def`

          // Use RDF to find all the uses of `Def`
          rdf::NodeSet Uses;
          RegisterRef DefReg = Def.Addr->getRegRef(DFG);
          for (auto UseID : L.getAllReachedUses(DefReg, Def)) {
            auto Use = DFG.addr<UseNode *>(UseID);
            if (Use.Addr->getFlags() & NodeAttrs::PhiRef) { // phi node
              NodeAddr<PhiNode *> Phi = Use.Addr->getOwner(DFG);
              for (const auto& I : L.getRealUses(Phi.Id)) {
                if (DFG.getPRI().alias(RegisterRef(I.first), DefReg)) {
                  for (const auto &UA : I.second)
                    Uses.emplace(UA.first);
                }
              }
            } else { // not a phi node
              Uses.emplace(UseID);
            }
          }

          // For each use of `Def`, we want to know whether:
          // (1) The use can leak the Def'ed value,
          // (2) The use can further propagate the Def'ed value to more defs
          for (auto UseID : Uses) {
            if (!UsesVisited.insert(UseID).second)
              continue; // Already visited this use of `Def`

            auto Use = DFG.addr<UseNode *>(UseID);
            assert(!(Use.Addr->getFlags() & NodeAttrs::PhiRef));
            MachineOperand &UseMO = Use.Addr->getOp();
            MachineInstr &UseMI = *UseMO.getParent();
            assert(UseMO.isReg());

            // We naively assume that an instruction propagates any loaded
            // uses to all defs unless the instruction is a call, in which
            // case all arguments will be treated as gadget sources during
            // analysis of the callee function.
            if (UseMI.isCall())
              continue;

            // Check whether this use can transmit (leak) its value.
            if (instrUsesRegToAccessMemory(UseMI, UseMO.getReg()) ||
                (!NoConditionalBranches &&
                 instrUsesRegToBranch(UseMI, UseMO.getReg()))) {
              Transmitters[Def.Id].push_back(Use.Addr->getOwner(DFG).Id);
              if (UseMI.mayLoad())
                continue; // Found a transmitting load -- no need to continue
                          // traversing its defs (i.e., this load will become
                          // a new gadget source anyways).
            }

            // Check whether the use propagates to more defs.
            NodeAddr<InstrNode *> Owner{Use.Addr->getOwner(DFG)};
            rdf::NodeList AnalyzedChildDefs;
            for (const auto &ChildDef :
                 Owner.Addr->members_if(DataFlowGraph::IsDef, DFG)) {
              if (!DefsVisited.insert(ChildDef.Id).second)
                continue; // Already visited this def
              if (Def.Addr->getAttrs() & NodeAttrs::Dead)
                continue;
              if (Def.Id == ChildDef.Id)
                continue; // `Def` uses itself (e.g., increment loop counter)

              AnalyzeDefUseChain(ChildDef);

              // `Def` inherits all of its child defs' transmitters.
              for (auto TransmitterId : Transmitters[ChildDef.Id])
                Transmitters[Def.Id].push_back(TransmitterId);
            }
          }

          // Note that this statement adds `Def.Id` to the map if no
          // transmitters were found for `Def`.
          auto &DefTransmitters = Transmitters[Def.Id];

          // Remove duplicate transmitters
          llvm::sort(DefTransmitters);
          DefTransmitters.erase(llvm::unique(DefTransmitters),
                                DefTransmitters.end());
        };

    // Find all of the transmitters
    AnalyzeDefUseChain(SourceDef);
    auto &SourceDefTransmitters = Transmitters[SourceDef.Id];
    if (SourceDefTransmitters.empty())
      return; // No transmitters for `SourceDef`

    MachineInstr *Source = SourceDef.Addr->getFlags() & NodeAttrs::PhiRef
                               ? MachineGadgetGraph::ArgNodeSentinel
                               : SourceDef.Addr->getOp().getParent();
    auto GadgetSource = MaybeAddNode(Source);
    // Each transmitter is a sink for `SourceDef`.
    for (auto TransmitterId : SourceDefTransmitters) {
      MachineInstr *Sink = DFG.addr<StmtNode *>(TransmitterId).Addr->getCode();
      auto GadgetSink = MaybeAddNode(Sink);
      // Add the gadget edge to the graph.
      Builder.addEdge(MachineGadgetGraph::GadgetEdgeSentinel,
                      GadgetSource.first, GadgetSink.first);
      ++GadgetCount;
    }
  };

  LLVM_DEBUG(dbgs() << "Analyzing def-use chains to find gadgets\n");
  // Analyze function arguments
  NodeAddr<BlockNode *> EntryBlock = DFG.getFunc().Addr->getEntryBlock(DFG);
  for (NodeAddr<PhiNode *> ArgPhi :
       EntryBlock.Addr->members_if(DataFlowGraph::IsPhi, DFG)) {
    NodeList Defs = ArgPhi.Addr->members_if(DataFlowGraph::IsDef, DFG);
    llvm::for_each(Defs, AnalyzeDef);
  }
  // Analyze every instruction in MF
  for (NodeAddr<BlockNode *> BA : DFG.getFunc().Addr->members(DFG)) {
    for (NodeAddr<StmtNode *> SA :
         BA.Addr->members_if(DataFlowGraph::IsCode<NodeAttrs::Stmt>, DFG)) {
      MachineInstr *MI = SA.Addr->getCode();
      if (isFence(MI)) {
        MaybeAddNode(MI);
        ++FenceCount;
      } else if (MI->mayLoad()) {
        NodeList Defs = SA.Addr->members_if(DataFlowGraph::IsDef, DFG);
        llvm::for_each(Defs, AnalyzeDef);
      }
    }
  }
  LLVM_DEBUG(dbgs() << "Found " << FenceCount << " fences\n");
  LLVM_DEBUG(dbgs() << "Found " << GadgetCount << " gadgets\n");
  if (GadgetCount == 0)
    return nullptr;
  NumGadgets += GadgetCount;

  // Traverse CFG to build the rest of the graph
  SmallSet<MachineBasicBlock *, 8> BlocksVisited;
  std::function<void(MachineBasicBlock *, GraphIter, unsigned)> TraverseCFG =
      [&](MachineBasicBlock *MBB, GraphIter GI, unsigned ParentDepth) {
        unsigned LoopDepth = MLI.getLoopDepth(MBB);
        if (!MBB->empty()) {
          // Always add the first instruction in each block
          auto NI = MBB->begin();
          auto BeginBB = MaybeAddNode(&*NI);
          Builder.addEdge(ParentDepth, GI, BeginBB.first);
          if (!BlocksVisited.insert(MBB).second)
            return;

          // Add any instructions within the block that are gadget components
          GI = BeginBB.first;
          while (++NI != MBB->end()) {
            auto Ref = NodeMap.find(&*NI);
            if (Ref != NodeMap.end()) {
              Builder.addEdge(LoopDepth, GI, Ref->getSecond());
              GI = Ref->getSecond();
            }
          }

          // Always add the terminator instruction, if one exists
          auto T = MBB->getFirstTerminator();
          if (T != MBB->end()) {
            auto EndBB = MaybeAddNode(&*T);
            if (EndBB.second)
              Builder.addEdge(LoopDepth, GI, EndBB.first);
            GI = EndBB.first;
          }
        }
        for (MachineBasicBlock *Succ : MBB->successors())
          TraverseCFG(Succ, GI, LoopDepth);
      };
  // ArgNodeSentinel is a pseudo-instruction that represents MF args in the
  // GadgetGraph
  GraphIter ArgNode = MaybeAddNode(MachineGadgetGraph::ArgNodeSentinel).first;
  TraverseCFG(&MF.front(), ArgNode, 0);
  std::unique_ptr<MachineGadgetGraph> G{Builder.get(FenceCount, GadgetCount)};
  LLVM_DEBUG(dbgs() << "Found " << G->nodes_size() << " nodes\n");
  return G;
}

// Returns the number of remaining gadget edges that could not be eliminated
int X86LoadValueInjectionLoadHardeningPass::elimMitigatedEdgesAndNodes(
    MachineGadgetGraph &G, EdgeSet &ElimEdges /* in, out */,
    NodeSet &ElimNodes /* in, out */) const {
  if (G.NumFences > 0) {
    // Eliminate fences and CFG edges that ingress and egress the fence, as
    // they are trivially mitigated.
    for (const Edge &E : G.edges()) {
      const Node *Dest = E.getDest();
      if (isFence(Dest->getValue())) {
        ElimNodes.insert(*Dest);
        ElimEdges.insert(E);
        for (const Edge &DE : Dest->edges())
          ElimEdges.insert(DE);
      }
    }
  }

  // Find and eliminate gadget edges that have been mitigated.
  int RemainingGadgets = 0;
  NodeSet ReachableNodes{G};
  for (const Node &RootN : G.nodes()) {
    if (llvm::none_of(RootN.edges(), MachineGadgetGraph::isGadgetEdge))
      continue; // skip this node if it isn't a gadget source

    // Find all of the nodes that are CFG-reachable from RootN using DFS
    ReachableNodes.clear();
    std::function<void(const Node *, bool)> FindReachableNodes =
        [&](const Node *N, bool FirstNode) {
          if (!FirstNode)
            ReachableNodes.insert(*N);
          for (const Edge &E : N->edges()) {
            const Node *Dest = E.getDest();
            if (MachineGadgetGraph::isCFGEdge(E) && !ElimEdges.contains(E) &&
                !ReachableNodes.contains(*Dest))
              FindReachableNodes(Dest, false);
          }
        };
    FindReachableNodes(&RootN, true);

    // Any gadget whose sink is unreachable has been mitigated
    for (const Edge &E : RootN.edges()) {
      if (MachineGadgetGraph::isGadgetEdge(E)) {
        if (ReachableNodes.contains(*E.getDest())) {
          // This gadget's sink is reachable
          ++RemainingGadgets;
        } else { // This gadget's sink is unreachable, and therefore mitigated
          ElimEdges.insert(E);
        }
      }
    }
  }
  return RemainingGadgets;
}

std::unique_ptr<MachineGadgetGraph>
X86LoadValueInjectionLoadHardeningPass::trimMitigatedEdges(
    std::unique_ptr<MachineGadgetGraph> Graph) const {
  NodeSet ElimNodes{*Graph};
  EdgeSet ElimEdges{*Graph};
  int RemainingGadgets =
      elimMitigatedEdgesAndNodes(*Graph, ElimEdges, ElimNodes);
  if (ElimEdges.empty() && ElimNodes.empty()) {
    Graph->NumFences = 0;
    Graph->NumGadgets = RemainingGadgets;
  } else {
    Graph = GraphBuilder::trim(*Graph, ElimNodes, ElimEdges, 0 /* NumFences */,
                               RemainingGadgets);
  }
  return Graph;
}

int X86LoadValueInjectionLoadHardeningPass::hardenLoadsWithPlugin(
    MachineFunction &MF, std::unique_ptr<MachineGadgetGraph> Graph) const {
  int FencesInserted = 0;

  do {
    LLVM_DEBUG(dbgs() << "Eliminating mitigated paths...\n");
    Graph = trimMitigatedEdges(std::move(Graph));
    LLVM_DEBUG(dbgs() << "Eliminating mitigated paths... Done\n");
    if (Graph->NumGadgets == 0)
      break;

    LLVM_DEBUG(dbgs() << "Cutting edges...\n");
    EdgeSet CutEdges{*Graph};
    auto Nodes = std::make_unique<unsigned int[]>(Graph->nodes_size() +
                                                  1 /* terminator node */);
    auto Edges = std::make_unique<unsigned int[]>(Graph->edges_size());
    auto EdgeCuts = std::make_unique<int[]>(Graph->edges_size());
    auto EdgeValues = std::make_unique<int[]>(Graph->edges_size());
    for (const Node &N : Graph->nodes()) {
      Nodes[Graph->getNodeIndex(N)] = Graph->getEdgeIndex(*N.edges_begin());
    }
    Nodes[Graph->nodes_size()] = Graph->edges_size(); // terminator node
    for (const Edge &E : Graph->edges()) {
      Edges[Graph->getEdgeIndex(E)] = Graph->getNodeIndex(*E.getDest());
      EdgeValues[Graph->getEdgeIndex(E)] = E.getValue();
    }
    OptimizeCut(Nodes.get(), Graph->nodes_size(), Edges.get(), EdgeValues.get(),
                EdgeCuts.get(), Graph->edges_size());
    for (int I = 0; I < Graph->edges_size(); ++I)
      if (EdgeCuts[I])
        CutEdges.set(I);
    LLVM_DEBUG(dbgs() << "Cutting edges... Done\n");
    LLVM_DEBUG(dbgs() << "Cut " << CutEdges.count() << " edges\n");

    LLVM_DEBUG(dbgs() << "Inserting LFENCEs...\n");
    FencesInserted += insertFences(MF, *Graph, CutEdges);
    LLVM_DEBUG(dbgs() << "Inserting LFENCEs... Done\n");
    LLVM_DEBUG(dbgs() << "Inserted " << FencesInserted << " fences\n");

    Graph = GraphBuilder::trim(*Graph, NodeSet{*Graph}, CutEdges);
  } while (true);

  return FencesInserted;
}

int X86LoadValueInjectionLoadHardeningPass::hardenLoadsWithHeuristic(
    MachineFunction &MF, std::unique_ptr<MachineGadgetGraph> Graph) const {
  // If `MF` does not have any fences, then no gadgets would have been
  // mitigated at this point.
  if (Graph->NumFences > 0) {
    LLVM_DEBUG(dbgs() << "Eliminating mitigated paths...\n");
    Graph = trimMitigatedEdges(std::move(Graph));
    LLVM_DEBUG(dbgs() << "Eliminating mitigated paths... Done\n");
  }

  if (Graph->NumGadgets == 0)
    return 0;

  LLVM_DEBUG(dbgs() << "Cutting edges...\n");
  EdgeSet CutEdges{*Graph};

  // Begin by collecting all ingress CFG edges for each node
  DenseMap<const Node *, SmallVector<const Edge *, 2>> IngressEdgeMap;
  for (const Edge &E : Graph->edges())
    if (MachineGadgetGraph::isCFGEdge(E))
      IngressEdgeMap[E.getDest()].push_back(&E);

  // For each gadget edge, make cuts that guarantee the gadget will be
  // mitigated. A computationally efficient way to achieve this is to either:
  // (a) cut all egress CFG edges from the gadget source, or
  // (b) cut all ingress CFG edges to the gadget sink.
  //
  // Moreover, the algorithm tries not to make a cut into a loop by preferring
  // to make a (b)-type cut if the gadget source resides at a greater loop depth
  // than the gadget sink, or an (a)-type cut otherwise.
  for (const Node &N : Graph->nodes()) {
    for (const Edge &E : N.edges()) {
      if (!MachineGadgetGraph::isGadgetEdge(E))
        continue;

      SmallVector<const Edge *, 2> EgressEdges;
      SmallVector<const Edge *, 2> &IngressEdges = IngressEdgeMap[E.getDest()];
      for (const Edge &EgressEdge : N.edges())
        if (MachineGadgetGraph::isCFGEdge(EgressEdge))
          EgressEdges.push_back(&EgressEdge);

      int EgressCutCost = 0, IngressCutCost = 0;
      for (const Edge *EgressEdge : EgressEdges)
        if (!CutEdges.contains(*EgressEdge))
          EgressCutCost += EgressEdge->getValue();
      for (const Edge *IngressEdge : IngressEdges)
        if (!CutEdges.contains(*IngressEdge))
          IngressCutCost += IngressEdge->getValue();

      auto &EdgesToCut =
          IngressCutCost < EgressCutCost ? IngressEdges : EgressEdges;
      for (const Edge *E : EdgesToCut)
        CutEdges.insert(*E);
    }
  }
  LLVM_DEBUG(dbgs() << "Cutting edges... Done\n");
  LLVM_DEBUG(dbgs() << "Cut " << CutEdges.count() << " edges\n");

  LLVM_DEBUG(dbgs() << "Inserting LFENCEs...\n");
  int FencesInserted = insertFences(MF, *Graph, CutEdges);
  LLVM_DEBUG(dbgs() << "Inserting LFENCEs... Done\n");
  LLVM_DEBUG(dbgs() << "Inserted " << FencesInserted << " fences\n");

  return FencesInserted;
}

int X86LoadValueInjectionLoadHardeningPass::insertFences(
    MachineFunction &MF, MachineGadgetGraph &G,
    EdgeSet &CutEdges /* in, out */) const {
  int FencesInserted = 0;
  for (const Node &N : G.nodes()) {
    for (const Edge &E : N.edges()) {
      if (CutEdges.contains(E)) {
        MachineInstr *MI = N.getValue(), *Prev;
        MachineBasicBlock *MBB;                  // Insert an LFENCE in this MBB
        MachineBasicBlock::iterator InsertionPt; // ...at this point
        if (MI == MachineGadgetGraph::ArgNodeSentinel) {
          // insert LFENCE at beginning of entry block
          MBB = &MF.front();
          InsertionPt = MBB->begin();
          Prev = nullptr;
        } else if (MI->isBranch()) { // insert the LFENCE before the branch
          MBB = MI->getParent();
          InsertionPt = MI;
          Prev = MI->getPrevNode();
          // Remove all egress CFG edges from this branch because the inserted
          // LFENCE prevents gadgets from crossing the branch.
          for (const Edge &E : N.edges()) {
            if (MachineGadgetGraph::isCFGEdge(E))
              CutEdges.insert(E);
          }
        } else { // insert the LFENCE after the instruction
          MBB = MI->getParent();
          InsertionPt = MI->getNextNode() ? MI->getNextNode() : MBB->end();
          Prev = InsertionPt == MBB->end()
                     ? (MBB->empty() ? nullptr : &MBB->back())
                     : InsertionPt->getPrevNode();
        }
        // Ensure this insertion is not redundant (two LFENCEs in sequence).
        if ((InsertionPt == MBB->end() || !isFence(&*InsertionPt)) &&
            (!Prev || !isFence(Prev))) {
          BuildMI(*MBB, InsertionPt, DebugLoc(), TII->get(X86::LFENCE));
          ++FencesInserted;
        }
      }
    }
  }
  return FencesInserted;
}

bool X86LoadValueInjectionLoadHardeningPass::instrUsesRegToAccessMemory(
    const MachineInstr &MI, unsigned Reg) const {
  if (!MI.mayLoadOrStore() || MI.getOpcode() == X86::MFENCE ||
      MI.getOpcode() == X86::SFENCE || MI.getOpcode() == X86::LFENCE)
    return false;

  const int MemRefBeginIdx = X86::getFirstAddrOperandIdx(MI);
  if (MemRefBeginIdx < 0) {
    LLVM_DEBUG(dbgs() << "Warning: unable to obtain memory operand for loading "
                         "instruction:\n";
               MI.print(dbgs()); dbgs() << '\n';);
    return false;
  }

  const MachineOperand &BaseMO =
      MI.getOperand(MemRefBeginIdx + X86::AddrBaseReg);
  const MachineOperand &IndexMO =
      MI.getOperand(MemRefBeginIdx + X86::AddrIndexReg);
  return (BaseMO.isReg() && BaseMO.getReg() != X86::NoRegister &&
          TRI->regsOverlap(BaseMO.getReg(), Reg)) ||
         (IndexMO.isReg() && IndexMO.getReg() != X86::NoRegister &&
          TRI->regsOverlap(IndexMO.getReg(), Reg));
}

bool X86LoadValueInjectionLoadHardeningPass::instrUsesRegToBranch(
    const MachineInstr &MI, unsigned Reg) const {
  if (!MI.isConditionalBranch())
    return false;
  for (const MachineOperand &Use : MI.uses())
    if (Use.isReg() && Use.getReg() == Reg)
      return true;
  return false;
}

INITIALIZE_PASS_BEGIN(X86LoadValueInjectionLoadHardeningPass, PASS_KEY,
                      "X86 LVI load hardening", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominanceFrontier)
INITIALIZE_PASS_END(X86LoadValueInjectionLoadHardeningPass, PASS_KEY,
                    "X86 LVI load hardening", false, false)

FunctionPass *llvm::createX86LoadValueInjectionLoadHardeningPass() {
  return new X86LoadValueInjectionLoadHardeningPass();
}
