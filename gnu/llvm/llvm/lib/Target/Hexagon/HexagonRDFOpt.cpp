//===- HexagonRDFOpt.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "RDFCopy.h"
#include "RDFDeadCode.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineDominanceFrontier.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RDFGraph.h"
#include "llvm/CodeGen/RDFLiveness.h"
#include "llvm/CodeGen/RDFRegisters.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <limits>
#include <utility>

using namespace llvm;
using namespace rdf;

namespace llvm {

  void initializeHexagonRDFOptPass(PassRegistry&);
  FunctionPass *createHexagonRDFOpt();

} // end namespace llvm

static unsigned RDFCount = 0;

static cl::opt<unsigned>
    RDFLimit("hexagon-rdf-limit",
             cl::init(std::numeric_limits<unsigned>::max()));

extern cl::opt<unsigned> RDFFuncBlockLimit;

static cl::opt<bool> RDFDump("hexagon-rdf-dump", cl::Hidden);
static cl::opt<bool> RDFTrackReserved("hexagon-rdf-track-reserved", cl::Hidden);

namespace {

  class HexagonRDFOpt : public MachineFunctionPass {
  public:
    HexagonRDFOpt() : MachineFunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineDominatorTreeWrapperPass>();
      AU.addRequired<MachineDominanceFrontier>();
      AU.setPreservesAll();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    StringRef getPassName() const override {
      return "Hexagon RDF optimizations";
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    static char ID;

  private:
    MachineDominatorTree *MDT;
    MachineRegisterInfo *MRI;
  };

struct HexagonCP : public CopyPropagation {
  HexagonCP(DataFlowGraph &G) : CopyPropagation(G) {}

  bool interpretAsCopy(const MachineInstr *MI, EqualityMap &EM) override;
};

struct HexagonDCE : public DeadCodeElimination {
  HexagonDCE(DataFlowGraph &G, MachineRegisterInfo &MRI)
    : DeadCodeElimination(G, MRI) {}

  bool rewrite(NodeAddr<InstrNode*> IA, SetVector<NodeId> &Remove);
  void removeOperand(NodeAddr<InstrNode*> IA, unsigned OpNum);

  bool run();
};

} // end anonymous namespace

char HexagonRDFOpt::ID = 0;

INITIALIZE_PASS_BEGIN(HexagonRDFOpt, "hexagon-rdf-opt",
      "Hexagon RDF optimizations", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominanceFrontier)
INITIALIZE_PASS_END(HexagonRDFOpt, "hexagon-rdf-opt",
      "Hexagon RDF optimizations", false, false)

bool HexagonCP::interpretAsCopy(const MachineInstr *MI, EqualityMap &EM) {
  auto mapRegs = [&EM] (RegisterRef DstR, RegisterRef SrcR) -> void {
    EM.insert(std::make_pair(DstR, SrcR));
  };

  DataFlowGraph &DFG = getDFG();
  unsigned Opc = MI->getOpcode();
  switch (Opc) {
    case Hexagon::A2_combinew: {
      const MachineOperand &DstOp = MI->getOperand(0);
      const MachineOperand &HiOp = MI->getOperand(1);
      const MachineOperand &LoOp = MI->getOperand(2);
      assert(DstOp.getSubReg() == 0 && "Unexpected subregister");
      mapRegs(DFG.makeRegRef(DstOp.getReg(), Hexagon::isub_hi),
              DFG.makeRegRef(HiOp.getReg(),  HiOp.getSubReg()));
      mapRegs(DFG.makeRegRef(DstOp.getReg(), Hexagon::isub_lo),
              DFG.makeRegRef(LoOp.getReg(), LoOp.getSubReg()));
      return true;
    }
    case Hexagon::A2_addi: {
      const MachineOperand &A = MI->getOperand(2);
      if (!A.isImm() || A.getImm() != 0)
        return false;
      [[fallthrough]];
    }
    case Hexagon::A2_tfr: {
      const MachineOperand &DstOp = MI->getOperand(0);
      const MachineOperand &SrcOp = MI->getOperand(1);
      mapRegs(DFG.makeRegRef(DstOp.getReg(), DstOp.getSubReg()),
              DFG.makeRegRef(SrcOp.getReg(), SrcOp.getSubReg()));
      return true;
    }
  }

  return CopyPropagation::interpretAsCopy(MI, EM);
}

bool HexagonDCE::run() {
  bool Collected = collect();
  if (!Collected)
    return false;

  const SetVector<NodeId> &DeadNodes = getDeadNodes();
  const SetVector<NodeId> &DeadInstrs = getDeadInstrs();

  using RefToInstrMap = DenseMap<NodeId, NodeId>;

  RefToInstrMap R2I;
  SetVector<NodeId> PartlyDead;
  DataFlowGraph &DFG = getDFG();

  for (NodeAddr<BlockNode*> BA : DFG.getFunc().Addr->members(DFG)) {
    for (auto TA : BA.Addr->members_if(DFG.IsCode<NodeAttrs::Stmt>, DFG)) {
      NodeAddr<StmtNode*> SA = TA;
      for (NodeAddr<RefNode*> RA : SA.Addr->members(DFG)) {
        R2I.insert(std::make_pair(RA.Id, SA.Id));
        if (DFG.IsDef(RA) && DeadNodes.count(RA.Id))
          if (!DeadInstrs.count(SA.Id))
            PartlyDead.insert(SA.Id);
      }
    }
  }

  // Nodes to remove.
  SetVector<NodeId> Remove = DeadInstrs;

  bool Changed = false;
  for (NodeId N : PartlyDead) {
    auto SA = DFG.addr<StmtNode*>(N);
    if (trace())
      dbgs() << "Partly dead: " << *SA.Addr->getCode();
    Changed |= rewrite(SA, Remove);
  }

  return erase(Remove) || Changed;
}

void HexagonDCE::removeOperand(NodeAddr<InstrNode*> IA, unsigned OpNum) {
  MachineInstr *MI = NodeAddr<StmtNode*>(IA).Addr->getCode();

  auto getOpNum = [MI] (MachineOperand &Op) -> unsigned {
    for (unsigned i = 0, n = MI->getNumOperands(); i != n; ++i)
      if (&MI->getOperand(i) == &Op)
        return i;
    llvm_unreachable("Invalid operand");
  };
  DenseMap<NodeId,unsigned> OpMap;
  DataFlowGraph &DFG = getDFG();
  NodeList Refs = IA.Addr->members(DFG);
  for (NodeAddr<RefNode*> RA : Refs)
    OpMap.insert(std::make_pair(RA.Id, getOpNum(RA.Addr->getOp())));

  MI->removeOperand(OpNum);

  for (NodeAddr<RefNode*> RA : Refs) {
    unsigned N = OpMap[RA.Id];
    if (N < OpNum)
      RA.Addr->setRegRef(&MI->getOperand(N), DFG);
    else if (N > OpNum)
      RA.Addr->setRegRef(&MI->getOperand(N-1), DFG);
  }
}

bool HexagonDCE::rewrite(NodeAddr<InstrNode*> IA, SetVector<NodeId> &Remove) {
  if (!getDFG().IsCode<NodeAttrs::Stmt>(IA))
    return false;
  DataFlowGraph &DFG = getDFG();
  MachineInstr &MI = *NodeAddr<StmtNode*>(IA).Addr->getCode();
  auto &HII = static_cast<const HexagonInstrInfo&>(DFG.getTII());
  if (HII.getAddrMode(MI) != HexagonII::PostInc)
    return false;
  unsigned Opc = MI.getOpcode();
  unsigned OpNum, NewOpc;
  switch (Opc) {
    case Hexagon::L2_loadri_pi:
      NewOpc = Hexagon::L2_loadri_io;
      OpNum = 1;
      break;
    case Hexagon::L2_loadrd_pi:
      NewOpc = Hexagon::L2_loadrd_io;
      OpNum = 1;
      break;
    case Hexagon::V6_vL32b_pi:
      NewOpc = Hexagon::V6_vL32b_ai;
      OpNum = 1;
      break;
    case Hexagon::S2_storeri_pi:
      NewOpc = Hexagon::S2_storeri_io;
      OpNum = 0;
      break;
    case Hexagon::S2_storerd_pi:
      NewOpc = Hexagon::S2_storerd_io;
      OpNum = 0;
      break;
    case Hexagon::V6_vS32b_pi:
      NewOpc = Hexagon::V6_vS32b_ai;
      OpNum = 0;
      break;
    default:
      return false;
  }
  auto IsDead = [this] (NodeAddr<DefNode*> DA) -> bool {
    return getDeadNodes().count(DA.Id);
  };
  NodeList Defs;
  MachineOperand &Op = MI.getOperand(OpNum);
  for (NodeAddr<DefNode*> DA : IA.Addr->members_if(DFG.IsDef, DFG)) {
    if (&DA.Addr->getOp() != &Op)
      continue;
    Defs = DFG.getRelatedRefs(IA, DA);
    if (!llvm::all_of(Defs, IsDead))
      return false;
    break;
  }

  // Mark all nodes in Defs for removal.
  for (auto D : Defs)
    Remove.insert(D.Id);

  if (trace())
    dbgs() << "Rewriting: " << MI;
  MI.setDesc(HII.get(NewOpc));
  MI.getOperand(OpNum+2).setImm(0);
  removeOperand(IA, OpNum);
  if (trace())
    dbgs() << "       to: " << MI;

  return true;
}

bool HexagonRDFOpt::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  // Perform RDF optimizations only if number of basic blocks in the
  // function is less than the limit
  if (MF.size() > RDFFuncBlockLimit) {
    if (RDFDump)
      dbgs() << "Skipping " << getPassName() << ": too many basic blocks\n";
    return false;
  }

  if (RDFLimit.getPosition()) {
    if (RDFCount >= RDFLimit)
      return false;
    RDFCount++;
  }

  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  const auto &MDF = getAnalysis<MachineDominanceFrontier>();
  const auto &HII = *MF.getSubtarget<HexagonSubtarget>().getInstrInfo();
  const auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  MRI = &MF.getRegInfo();
  bool Changed;

  if (RDFDump)
    MF.print(dbgs() << "Before " << getPassName() << "\n", nullptr);

  DataFlowGraph G(MF, HII, HRI, *MDT, MDF);
  // Dead phi nodes are necessary for copy propagation: we can add a use
  // of a register in a block where it would need a phi node, but which
  // was dead (and removed) during the graph build time.
  DataFlowGraph::Config Cfg;
  Cfg.Options = RDFTrackReserved
                    ? BuildOptions::KeepDeadPhis
                    : BuildOptions::KeepDeadPhis | BuildOptions::OmitReserved;
  G.build(Cfg);

  if (RDFDump)
    dbgs() << "Starting copy propagation on: " << MF.getName() << '\n'
           << PrintNode<FuncNode*>(G.getFunc(), G) << '\n';
  HexagonCP CP(G);
  CP.trace(RDFDump);
  Changed = CP.run();

  if (RDFDump)
    dbgs() << "Starting dead code elimination on: " << MF.getName() << '\n'
           << PrintNode<FuncNode*>(G.getFunc(), G) << '\n';
  HexagonDCE DCE(G, *MRI);
  DCE.trace(RDFDump);
  Changed |= DCE.run();

  if (Changed) {
    if (RDFDump) {
      dbgs() << "Starting liveness recomputation on: " << MF.getName() << '\n'
             << PrintNode<FuncNode*>(G.getFunc(), G) << '\n';
    }
    Liveness LV(*MRI, G);
    LV.trace(RDFDump);
    LV.computeLiveIns();
    LV.resetLiveIns();
    LV.resetKills();
  }

  if (RDFDump)
    MF.print(dbgs() << "After " << getPassName() << "\n", nullptr);

  return false;
}

FunctionPass *llvm::createHexagonRDFOpt() {
  return new HexagonRDFOpt();
}
