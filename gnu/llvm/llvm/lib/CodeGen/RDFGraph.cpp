//===- RDFGraph.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Target-independent, SSA-based data flow graph for register data flow (RDF).
//
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominanceFrontier.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RDFGraph.h"
#include "llvm/CodeGen/RDFRegisters.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

// Printing functions. Have them here first, so that the rest of the code
// can use them.
namespace llvm::rdf {

raw_ostream &operator<<(raw_ostream &OS, const Print<RegisterRef> &P) {
  P.G.getPRI().print(OS, P.Obj);
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<NodeId> &P) {
  if (P.Obj == 0)
    return OS << "null";
  auto NA = P.G.addr<NodeBase *>(P.Obj);
  uint16_t Attrs = NA.Addr->getAttrs();
  uint16_t Kind = NodeAttrs::kind(Attrs);
  uint16_t Flags = NodeAttrs::flags(Attrs);
  switch (NodeAttrs::type(Attrs)) {
  case NodeAttrs::Code:
    switch (Kind) {
    case NodeAttrs::Func:
      OS << 'f';
      break;
    case NodeAttrs::Block:
      OS << 'b';
      break;
    case NodeAttrs::Stmt:
      OS << 's';
      break;
    case NodeAttrs::Phi:
      OS << 'p';
      break;
    default:
      OS << "c?";
      break;
    }
    break;
  case NodeAttrs::Ref:
    if (Flags & NodeAttrs::Undef)
      OS << '/';
    if (Flags & NodeAttrs::Dead)
      OS << '\\';
    if (Flags & NodeAttrs::Preserving)
      OS << '+';
    if (Flags & NodeAttrs::Clobbering)
      OS << '~';
    switch (Kind) {
    case NodeAttrs::Use:
      OS << 'u';
      break;
    case NodeAttrs::Def:
      OS << 'd';
      break;
    case NodeAttrs::Block:
      OS << 'b';
      break;
    default:
      OS << "r?";
      break;
    }
    break;
  default:
    OS << '?';
    break;
  }
  OS << P.Obj;
  if (Flags & NodeAttrs::Shadow)
    OS << '"';
  return OS;
}

static void printRefHeader(raw_ostream &OS, const Ref RA,
                           const DataFlowGraph &G) {
  OS << Print(RA.Id, G) << '<' << Print(RA.Addr->getRegRef(G), G) << '>';
  if (RA.Addr->getFlags() & NodeAttrs::Fixed)
    OS << '!';
}

raw_ostream &operator<<(raw_ostream &OS, const Print<Def> &P) {
  printRefHeader(OS, P.Obj, P.G);
  OS << '(';
  if (NodeId N = P.Obj.Addr->getReachingDef())
    OS << Print(N, P.G);
  OS << ',';
  if (NodeId N = P.Obj.Addr->getReachedDef())
    OS << Print(N, P.G);
  OS << ',';
  if (NodeId N = P.Obj.Addr->getReachedUse())
    OS << Print(N, P.G);
  OS << "):";
  if (NodeId N = P.Obj.Addr->getSibling())
    OS << Print(N, P.G);
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<Use> &P) {
  printRefHeader(OS, P.Obj, P.G);
  OS << '(';
  if (NodeId N = P.Obj.Addr->getReachingDef())
    OS << Print(N, P.G);
  OS << "):";
  if (NodeId N = P.Obj.Addr->getSibling())
    OS << Print(N, P.G);
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<PhiUse> &P) {
  printRefHeader(OS, P.Obj, P.G);
  OS << '(';
  if (NodeId N = P.Obj.Addr->getReachingDef())
    OS << Print(N, P.G);
  OS << ',';
  if (NodeId N = P.Obj.Addr->getPredecessor())
    OS << Print(N, P.G);
  OS << "):";
  if (NodeId N = P.Obj.Addr->getSibling())
    OS << Print(N, P.G);
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<Ref> &P) {
  switch (P.Obj.Addr->getKind()) {
  case NodeAttrs::Def:
    OS << PrintNode<DefNode *>(P.Obj, P.G);
    break;
  case NodeAttrs::Use:
    if (P.Obj.Addr->getFlags() & NodeAttrs::PhiRef)
      OS << PrintNode<PhiUseNode *>(P.Obj, P.G);
    else
      OS << PrintNode<UseNode *>(P.Obj, P.G);
    break;
  }
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<NodeList> &P) {
  unsigned N = P.Obj.size();
  for (auto I : P.Obj) {
    OS << Print(I.Id, P.G);
    if (--N)
      OS << ' ';
  }
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<NodeSet> &P) {
  unsigned N = P.Obj.size();
  for (auto I : P.Obj) {
    OS << Print(I, P.G);
    if (--N)
      OS << ' ';
  }
  return OS;
}

namespace {

template <typename T> struct PrintListV {
  PrintListV(const NodeList &L, const DataFlowGraph &G) : List(L), G(G) {}

  using Type = T;
  const NodeList &List;
  const DataFlowGraph &G;
};

template <typename T>
raw_ostream &operator<<(raw_ostream &OS, const PrintListV<T> &P) {
  unsigned N = P.List.size();
  for (NodeAddr<T> A : P.List) {
    OS << PrintNode<T>(A, P.G);
    if (--N)
      OS << ", ";
  }
  return OS;
}

} // end anonymous namespace

raw_ostream &operator<<(raw_ostream &OS, const Print<Phi> &P) {
  OS << Print(P.Obj.Id, P.G) << ": phi ["
     << PrintListV<RefNode *>(P.Obj.Addr->members(P.G), P.G) << ']';
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<Stmt> &P) {
  const MachineInstr &MI = *P.Obj.Addr->getCode();
  unsigned Opc = MI.getOpcode();
  OS << Print(P.Obj.Id, P.G) << ": " << P.G.getTII().getName(Opc);
  // Print the target for calls and branches (for readability).
  if (MI.isCall() || MI.isBranch()) {
    MachineInstr::const_mop_iterator T =
        llvm::find_if(MI.operands(), [](const MachineOperand &Op) -> bool {
          return Op.isMBB() || Op.isGlobal() || Op.isSymbol();
        });
    if (T != MI.operands_end()) {
      OS << ' ';
      if (T->isMBB())
        OS << printMBBReference(*T->getMBB());
      else if (T->isGlobal())
        OS << T->getGlobal()->getName();
      else if (T->isSymbol())
        OS << T->getSymbolName();
    }
  }
  OS << " [" << PrintListV<RefNode *>(P.Obj.Addr->members(P.G), P.G) << ']';
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<Instr> &P) {
  switch (P.Obj.Addr->getKind()) {
  case NodeAttrs::Phi:
    OS << PrintNode<PhiNode *>(P.Obj, P.G);
    break;
  case NodeAttrs::Stmt:
    OS << PrintNode<StmtNode *>(P.Obj, P.G);
    break;
  default:
    OS << "instr? " << Print(P.Obj.Id, P.G);
    break;
  }
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<Block> &P) {
  MachineBasicBlock *BB = P.Obj.Addr->getCode();
  unsigned NP = BB->pred_size();
  std::vector<int> Ns;
  auto PrintBBs = [&OS](const std::vector<int> &Ns) -> void {
    unsigned N = Ns.size();
    for (int I : Ns) {
      OS << "%bb." << I;
      if (--N)
        OS << ", ";
    }
  };

  OS << Print(P.Obj.Id, P.G) << ": --- " << printMBBReference(*BB)
     << " --- preds(" << NP << "): ";
  for (MachineBasicBlock *B : BB->predecessors())
    Ns.push_back(B->getNumber());
  PrintBBs(Ns);

  unsigned NS = BB->succ_size();
  OS << "  succs(" << NS << "): ";
  Ns.clear();
  for (MachineBasicBlock *B : BB->successors())
    Ns.push_back(B->getNumber());
  PrintBBs(Ns);
  OS << '\n';

  for (auto I : P.Obj.Addr->members(P.G))
    OS << PrintNode<InstrNode *>(I, P.G) << '\n';
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<Func> &P) {
  OS << "DFG dump:[\n"
     << Print(P.Obj.Id, P.G)
     << ": Function: " << P.Obj.Addr->getCode()->getName() << '\n';
  for (auto I : P.Obj.Addr->members(P.G))
    OS << PrintNode<BlockNode *>(I, P.G) << '\n';
  OS << "]\n";
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<RegisterSet> &P) {
  OS << '{';
  for (auto I : P.Obj)
    OS << ' ' << Print(I, P.G);
  OS << " }";
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Print<RegisterAggr> &P) {
  OS << P.Obj;
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS,
                        const Print<DataFlowGraph::DefStack> &P) {
  for (auto I = P.Obj.top(), E = P.Obj.bottom(); I != E;) {
    OS << Print(I->Id, P.G) << '<' << Print(I->Addr->getRegRef(P.G), P.G)
       << '>';
    I.down();
    if (I != E)
      OS << ' ';
  }
  return OS;
}

// Node allocation functions.
//
// Node allocator is like a slab memory allocator: it allocates blocks of
// memory in sizes that are multiples of the size of a node. Each block has
// the same size. Nodes are allocated from the currently active block, and
// when it becomes full, a new one is created.
// There is a mapping scheme between node id and its location in a block,
// and within that block is described in the header file.
//
void NodeAllocator::startNewBlock() {
  void *T = MemPool.Allocate(NodesPerBlock * NodeMemSize, NodeMemSize);
  char *P = static_cast<char *>(T);
  Blocks.push_back(P);
  // Check if the block index is still within the allowed range, i.e. less
  // than 2^N, where N is the number of bits in NodeId for the block index.
  // BitsPerIndex is the number of bits per node index.
  assert((Blocks.size() < ((size_t)1 << (8 * sizeof(NodeId) - BitsPerIndex))) &&
         "Out of bits for block index");
  ActiveEnd = P;
}

bool NodeAllocator::needNewBlock() {
  if (Blocks.empty())
    return true;

  char *ActiveBegin = Blocks.back();
  uint32_t Index = (ActiveEnd - ActiveBegin) / NodeMemSize;
  return Index >= NodesPerBlock;
}

Node NodeAllocator::New() {
  if (needNewBlock())
    startNewBlock();

  uint32_t ActiveB = Blocks.size() - 1;
  uint32_t Index = (ActiveEnd - Blocks[ActiveB]) / NodeMemSize;
  Node NA = {reinterpret_cast<NodeBase *>(ActiveEnd), makeId(ActiveB, Index)};
  ActiveEnd += NodeMemSize;
  return NA;
}

NodeId NodeAllocator::id(const NodeBase *P) const {
  uintptr_t A = reinterpret_cast<uintptr_t>(P);
  for (unsigned i = 0, n = Blocks.size(); i != n; ++i) {
    uintptr_t B = reinterpret_cast<uintptr_t>(Blocks[i]);
    if (A < B || A >= B + NodesPerBlock * NodeMemSize)
      continue;
    uint32_t Idx = (A - B) / NodeMemSize;
    return makeId(i, Idx);
  }
  llvm_unreachable("Invalid node address");
}

void NodeAllocator::clear() {
  MemPool.Reset();
  Blocks.clear();
  ActiveEnd = nullptr;
}

// Insert node NA after "this" in the circular chain.
void NodeBase::append(Node NA) {
  NodeId Nx = Next;
  // If NA is already "next", do nothing.
  if (Next != NA.Id) {
    Next = NA.Id;
    NA.Addr->Next = Nx;
  }
}

// Fundamental node manipulator functions.

// Obtain the register reference from a reference node.
RegisterRef RefNode::getRegRef(const DataFlowGraph &G) const {
  assert(NodeAttrs::type(Attrs) == NodeAttrs::Ref);
  if (NodeAttrs::flags(Attrs) & NodeAttrs::PhiRef)
    return G.unpack(RefData.PR);
  assert(RefData.Op != nullptr);
  return G.makeRegRef(*RefData.Op);
}

// Set the register reference in the reference node directly (for references
// in phi nodes).
void RefNode::setRegRef(RegisterRef RR, DataFlowGraph &G) {
  assert(NodeAttrs::type(Attrs) == NodeAttrs::Ref);
  assert(NodeAttrs::flags(Attrs) & NodeAttrs::PhiRef);
  RefData.PR = G.pack(RR);
}

// Set the register reference in the reference node based on a machine
// operand (for references in statement nodes).
void RefNode::setRegRef(MachineOperand *Op, DataFlowGraph &G) {
  assert(NodeAttrs::type(Attrs) == NodeAttrs::Ref);
  assert(!(NodeAttrs::flags(Attrs) & NodeAttrs::PhiRef));
  (void)G;
  RefData.Op = Op;
}

// Get the owner of a given reference node.
Node RefNode::getOwner(const DataFlowGraph &G) {
  Node NA = G.addr<NodeBase *>(getNext());

  while (NA.Addr != this) {
    if (NA.Addr->getType() == NodeAttrs::Code)
      return NA;
    NA = G.addr<NodeBase *>(NA.Addr->getNext());
  }
  llvm_unreachable("No owner in circular list");
}

// Connect the def node to the reaching def node.
void DefNode::linkToDef(NodeId Self, Def DA) {
  RefData.RD = DA.Id;
  RefData.Sib = DA.Addr->getReachedDef();
  DA.Addr->setReachedDef(Self);
}

// Connect the use node to the reaching def node.
void UseNode::linkToDef(NodeId Self, Def DA) {
  RefData.RD = DA.Id;
  RefData.Sib = DA.Addr->getReachedUse();
  DA.Addr->setReachedUse(Self);
}

// Get the first member of the code node.
Node CodeNode::getFirstMember(const DataFlowGraph &G) const {
  if (CodeData.FirstM == 0)
    return Node();
  return G.addr<NodeBase *>(CodeData.FirstM);
}

// Get the last member of the code node.
Node CodeNode::getLastMember(const DataFlowGraph &G) const {
  if (CodeData.LastM == 0)
    return Node();
  return G.addr<NodeBase *>(CodeData.LastM);
}

// Add node NA at the end of the member list of the given code node.
void CodeNode::addMember(Node NA, const DataFlowGraph &G) {
  Node ML = getLastMember(G);
  if (ML.Id != 0) {
    ML.Addr->append(NA);
  } else {
    CodeData.FirstM = NA.Id;
    NodeId Self = G.id(this);
    NA.Addr->setNext(Self);
  }
  CodeData.LastM = NA.Id;
}

// Add node NA after member node MA in the given code node.
void CodeNode::addMemberAfter(Node MA, Node NA, const DataFlowGraph &G) {
  MA.Addr->append(NA);
  if (CodeData.LastM == MA.Id)
    CodeData.LastM = NA.Id;
}

// Remove member node NA from the given code node.
void CodeNode::removeMember(Node NA, const DataFlowGraph &G) {
  Node MA = getFirstMember(G);
  assert(MA.Id != 0);

  // Special handling if the member to remove is the first member.
  if (MA.Id == NA.Id) {
    if (CodeData.LastM == MA.Id) {
      // If it is the only member, set both first and last to 0.
      CodeData.FirstM = CodeData.LastM = 0;
    } else {
      // Otherwise, advance the first member.
      CodeData.FirstM = MA.Addr->getNext();
    }
    return;
  }

  while (MA.Addr != this) {
    NodeId MX = MA.Addr->getNext();
    if (MX == NA.Id) {
      MA.Addr->setNext(NA.Addr->getNext());
      // If the member to remove happens to be the last one, update the
      // LastM indicator.
      if (CodeData.LastM == NA.Id)
        CodeData.LastM = MA.Id;
      return;
    }
    MA = G.addr<NodeBase *>(MX);
  }
  llvm_unreachable("No such member");
}

// Return the list of all members of the code node.
NodeList CodeNode::members(const DataFlowGraph &G) const {
  static auto True = [](Node) -> bool { return true; };
  return members_if(True, G);
}

// Return the owner of the given instr node.
Node InstrNode::getOwner(const DataFlowGraph &G) {
  Node NA = G.addr<NodeBase *>(getNext());

  while (NA.Addr != this) {
    assert(NA.Addr->getType() == NodeAttrs::Code);
    if (NA.Addr->getKind() == NodeAttrs::Block)
      return NA;
    NA = G.addr<NodeBase *>(NA.Addr->getNext());
  }
  llvm_unreachable("No owner in circular list");
}

// Add the phi node PA to the given block node.
void BlockNode::addPhi(Phi PA, const DataFlowGraph &G) {
  Node M = getFirstMember(G);
  if (M.Id == 0) {
    addMember(PA, G);
    return;
  }

  assert(M.Addr->getType() == NodeAttrs::Code);
  if (M.Addr->getKind() == NodeAttrs::Stmt) {
    // If the first member of the block is a statement, insert the phi as
    // the first member.
    CodeData.FirstM = PA.Id;
    PA.Addr->setNext(M.Id);
  } else {
    // If the first member is a phi, find the last phi, and append PA to it.
    assert(M.Addr->getKind() == NodeAttrs::Phi);
    Node MN = M;
    do {
      M = MN;
      MN = G.addr<NodeBase *>(M.Addr->getNext());
      assert(MN.Addr->getType() == NodeAttrs::Code);
    } while (MN.Addr->getKind() == NodeAttrs::Phi);

    // M is the last phi.
    addMemberAfter(M, PA, G);
  }
}

// Find the block node corresponding to the machine basic block BB in the
// given func node.
Block FuncNode::findBlock(const MachineBasicBlock *BB,
                          const DataFlowGraph &G) const {
  auto EqBB = [BB](Node NA) -> bool { return Block(NA).Addr->getCode() == BB; };
  NodeList Ms = members_if(EqBB, G);
  if (!Ms.empty())
    return Ms[0];
  return Block();
}

// Get the block node for the entry block in the given function.
Block FuncNode::getEntryBlock(const DataFlowGraph &G) {
  MachineBasicBlock *EntryB = &getCode()->front();
  return findBlock(EntryB, G);
}

// Target operand information.
//

// For a given instruction, check if there are any bits of RR that can remain
// unchanged across this def.
bool TargetOperandInfo::isPreserving(const MachineInstr &In,
                                     unsigned OpNum) const {
  return TII.isPredicated(In);
}

// Check if the definition of RR produces an unspecified value.
bool TargetOperandInfo::isClobbering(const MachineInstr &In,
                                     unsigned OpNum) const {
  const MachineOperand &Op = In.getOperand(OpNum);
  if (Op.isRegMask())
    return true;
  assert(Op.isReg());
  if (In.isCall())
    if (Op.isDef() && Op.isDead())
      return true;
  return false;
}

// Check if the given instruction specifically requires
bool TargetOperandInfo::isFixedReg(const MachineInstr &In,
                                   unsigned OpNum) const {
  if (In.isCall() || In.isReturn() || In.isInlineAsm())
    return true;
  // Check for a tail call.
  if (In.isBranch())
    for (const MachineOperand &O : In.operands())
      if (O.isGlobal() || O.isSymbol())
        return true;

  const MCInstrDesc &D = In.getDesc();
  if (D.implicit_defs().empty() && D.implicit_uses().empty())
    return false;
  const MachineOperand &Op = In.getOperand(OpNum);
  // If there is a sub-register, treat the operand as non-fixed. Currently,
  // fixed registers are those that are listed in the descriptor as implicit
  // uses or defs, and those lists do not allow sub-registers.
  if (Op.getSubReg() != 0)
    return false;
  Register Reg = Op.getReg();
  ArrayRef<MCPhysReg> ImpOps =
      Op.isDef() ? D.implicit_defs() : D.implicit_uses();
  return is_contained(ImpOps, Reg);
}

//
// The data flow graph construction.
//

DataFlowGraph::DataFlowGraph(MachineFunction &mf, const TargetInstrInfo &tii,
                             const TargetRegisterInfo &tri,
                             const MachineDominatorTree &mdt,
                             const MachineDominanceFrontier &mdf)
    : DefaultTOI(std::make_unique<TargetOperandInfo>(tii)), MF(mf), TII(tii),
      TRI(tri), PRI(tri, mf), MDT(mdt), MDF(mdf), TOI(*DefaultTOI),
      LiveIns(PRI) {}

DataFlowGraph::DataFlowGraph(MachineFunction &mf, const TargetInstrInfo &tii,
                             const TargetRegisterInfo &tri,
                             const MachineDominatorTree &mdt,
                             const MachineDominanceFrontier &mdf,
                             const TargetOperandInfo &toi)
    : MF(mf), TII(tii), TRI(tri), PRI(tri, mf), MDT(mdt), MDF(mdf), TOI(toi),
      LiveIns(PRI) {}

// The implementation of the definition stack.
// Each register reference has its own definition stack. In particular,
// for a register references "Reg" and "Reg:subreg" will each have their
// own definition stacks.

// Construct a stack iterator.
DataFlowGraph::DefStack::Iterator::Iterator(const DataFlowGraph::DefStack &S,
                                            bool Top)
    : DS(S) {
  if (!Top) {
    // Initialize to bottom.
    Pos = 0;
    return;
  }
  // Initialize to the top, i.e. top-most non-delimiter (or 0, if empty).
  Pos = DS.Stack.size();
  while (Pos > 0 && DS.isDelimiter(DS.Stack[Pos - 1]))
    Pos--;
}

// Return the size of the stack, including block delimiters.
unsigned DataFlowGraph::DefStack::size() const {
  unsigned S = 0;
  for (auto I = top(), E = bottom(); I != E; I.down())
    S++;
  return S;
}

// Remove the top entry from the stack. Remove all intervening delimiters
// so that after this, the stack is either empty, or the top of the stack
// is a non-delimiter.
void DataFlowGraph::DefStack::pop() {
  assert(!empty());
  unsigned P = nextDown(Stack.size());
  Stack.resize(P);
}

// Push a delimiter for block node N on the stack.
void DataFlowGraph::DefStack::start_block(NodeId N) {
  assert(N != 0);
  Stack.push_back(Def(nullptr, N));
}

// Remove all nodes from the top of the stack, until the delimited for
// block node N is encountered. Remove the delimiter as well. In effect,
// this will remove from the stack all definitions from block N.
void DataFlowGraph::DefStack::clear_block(NodeId N) {
  assert(N != 0);
  unsigned P = Stack.size();
  while (P > 0) {
    bool Found = isDelimiter(Stack[P - 1], N);
    P--;
    if (Found)
      break;
  }
  // This will also remove the delimiter, if found.
  Stack.resize(P);
}

// Move the stack iterator up by one.
unsigned DataFlowGraph::DefStack::nextUp(unsigned P) const {
  // Get the next valid position after P (skipping all delimiters).
  // The input position P does not have to point to a non-delimiter.
  unsigned SS = Stack.size();
  bool IsDelim;
  assert(P < SS);
  do {
    P++;
    IsDelim = isDelimiter(Stack[P - 1]);
  } while (P < SS && IsDelim);
  assert(!IsDelim);
  return P;
}

// Move the stack iterator down by one.
unsigned DataFlowGraph::DefStack::nextDown(unsigned P) const {
  // Get the preceding valid position before P (skipping all delimiters).
  // The input position P does not have to point to a non-delimiter.
  assert(P > 0 && P <= Stack.size());
  bool IsDelim = isDelimiter(Stack[P - 1]);
  do {
    if (--P == 0)
      break;
    IsDelim = isDelimiter(Stack[P - 1]);
  } while (P > 0 && IsDelim);
  assert(!IsDelim);
  return P;
}

// Register information.

RegisterAggr DataFlowGraph::getLandingPadLiveIns() const {
  RegisterAggr LR(getPRI());
  const Function &F = MF.getFunction();
  const Constant *PF = F.hasPersonalityFn() ? F.getPersonalityFn() : nullptr;
  const TargetLowering &TLI = *MF.getSubtarget().getTargetLowering();
  if (RegisterId R = TLI.getExceptionPointerRegister(PF))
    LR.insert(RegisterRef(R));
  if (!isFuncletEHPersonality(classifyEHPersonality(PF))) {
    if (RegisterId R = TLI.getExceptionSelectorRegister(PF))
      LR.insert(RegisterRef(R));
  }
  return LR;
}

// Node management functions.

// Get the pointer to the node with the id N.
NodeBase *DataFlowGraph::ptr(NodeId N) const {
  if (N == 0)
    return nullptr;
  return Memory.ptr(N);
}

// Get the id of the node at the address P.
NodeId DataFlowGraph::id(const NodeBase *P) const {
  if (P == nullptr)
    return 0;
  return Memory.id(P);
}

// Allocate a new node and set the attributes to Attrs.
Node DataFlowGraph::newNode(uint16_t Attrs) {
  Node P = Memory.New();
  P.Addr->init();
  P.Addr->setAttrs(Attrs);
  return P;
}

// Make a copy of the given node B, except for the data-flow links, which
// are set to 0.
Node DataFlowGraph::cloneNode(const Node B) {
  Node NA = newNode(0);
  memcpy(NA.Addr, B.Addr, sizeof(NodeBase));
  // Ref nodes need to have the data-flow links reset.
  if (NA.Addr->getType() == NodeAttrs::Ref) {
    Ref RA = NA;
    RA.Addr->setReachingDef(0);
    RA.Addr->setSibling(0);
    if (NA.Addr->getKind() == NodeAttrs::Def) {
      Def DA = NA;
      DA.Addr->setReachedDef(0);
      DA.Addr->setReachedUse(0);
    }
  }
  return NA;
}

// Allocation routines for specific node types/kinds.

Use DataFlowGraph::newUse(Instr Owner, MachineOperand &Op, uint16_t Flags) {
  Use UA = newNode(NodeAttrs::Ref | NodeAttrs::Use | Flags);
  UA.Addr->setRegRef(&Op, *this);
  return UA;
}

PhiUse DataFlowGraph::newPhiUse(Phi Owner, RegisterRef RR, Block PredB,
                                uint16_t Flags) {
  PhiUse PUA = newNode(NodeAttrs::Ref | NodeAttrs::Use | Flags);
  assert(Flags & NodeAttrs::PhiRef);
  PUA.Addr->setRegRef(RR, *this);
  PUA.Addr->setPredecessor(PredB.Id);
  return PUA;
}

Def DataFlowGraph::newDef(Instr Owner, MachineOperand &Op, uint16_t Flags) {
  Def DA = newNode(NodeAttrs::Ref | NodeAttrs::Def | Flags);
  DA.Addr->setRegRef(&Op, *this);
  return DA;
}

Def DataFlowGraph::newDef(Instr Owner, RegisterRef RR, uint16_t Flags) {
  Def DA = newNode(NodeAttrs::Ref | NodeAttrs::Def | Flags);
  assert(Flags & NodeAttrs::PhiRef);
  DA.Addr->setRegRef(RR, *this);
  return DA;
}

Phi DataFlowGraph::newPhi(Block Owner) {
  Phi PA = newNode(NodeAttrs::Code | NodeAttrs::Phi);
  Owner.Addr->addPhi(PA, *this);
  return PA;
}

Stmt DataFlowGraph::newStmt(Block Owner, MachineInstr *MI) {
  Stmt SA = newNode(NodeAttrs::Code | NodeAttrs::Stmt);
  SA.Addr->setCode(MI);
  Owner.Addr->addMember(SA, *this);
  return SA;
}

Block DataFlowGraph::newBlock(Func Owner, MachineBasicBlock *BB) {
  Block BA = newNode(NodeAttrs::Code | NodeAttrs::Block);
  BA.Addr->setCode(BB);
  Owner.Addr->addMember(BA, *this);
  return BA;
}

Func DataFlowGraph::newFunc(MachineFunction *MF) {
  Func FA = newNode(NodeAttrs::Code | NodeAttrs::Func);
  FA.Addr->setCode(MF);
  return FA;
}

// Build the data flow graph.
void DataFlowGraph::build(const Config &config) {
  reset();
  BuildCfg = config;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  ReservedRegs = MRI.getReservedRegs();
  bool SkipReserved = BuildCfg.Options & BuildOptions::OmitReserved;

  auto Insert = [](auto &Set, auto &&Range) {
    Set.insert(Range.begin(), Range.end());
  };

  if (BuildCfg.TrackRegs.empty()) {
    std::set<RegisterId> BaseSet;
    if (BuildCfg.Classes.empty()) {
      // Insert every register.
      for (unsigned R = 1, E = getPRI().getTRI().getNumRegs(); R != E; ++R)
        BaseSet.insert(R);
    } else {
      for (const TargetRegisterClass *RC : BuildCfg.Classes) {
        for (MCPhysReg R : *RC)
          BaseSet.insert(R);
      }
    }
    for (RegisterId R : BaseSet) {
      if (SkipReserved && ReservedRegs[R])
        continue;
      Insert(TrackedUnits, getPRI().getUnits(RegisterRef(R)));
    }
  } else {
    // Track set in Config overrides everything.
    for (unsigned R : BuildCfg.TrackRegs) {
      if (SkipReserved && ReservedRegs[R])
        continue;
      Insert(TrackedUnits, getPRI().getUnits(RegisterRef(R)));
    }
  }

  TheFunc = newFunc(&MF);

  if (MF.empty())
    return;

  for (MachineBasicBlock &B : MF) {
    Block BA = newBlock(TheFunc, &B);
    BlockNodes.insert(std::make_pair(&B, BA));
    for (MachineInstr &I : B) {
      if (I.isDebugInstr())
        continue;
      buildStmt(BA, I);
    }
  }

  Block EA = TheFunc.Addr->getEntryBlock(*this);
  NodeList Blocks = TheFunc.Addr->members(*this);

  // Collect function live-ins and entry block live-ins.
  MachineBasicBlock &EntryB = *EA.Addr->getCode();
  assert(EntryB.pred_empty() && "Function entry block has predecessors");
  for (std::pair<unsigned, unsigned> P : MRI.liveins())
    LiveIns.insert(RegisterRef(P.first));
  if (MRI.tracksLiveness()) {
    for (auto I : EntryB.liveins())
      LiveIns.insert(RegisterRef(I.PhysReg, I.LaneMask));
  }

  // Add function-entry phi nodes for the live-in registers.
  for (RegisterRef RR : LiveIns.refs()) {
    if (RR.isReg() && !isTracked(RR)) // isReg is likely guaranteed
      continue;
    Phi PA = newPhi(EA);
    uint16_t PhiFlags = NodeAttrs::PhiRef | NodeAttrs::Preserving;
    Def DA = newDef(PA, RR, PhiFlags);
    PA.Addr->addMember(DA, *this);
  }

  // Add phis for landing pads.
  // Landing pads, unlike usual backs blocks, are not entered through
  // branches in the program, or fall-throughs from other blocks. They
  // are entered from the exception handling runtime and target's ABI
  // may define certain registers as defined on entry to such a block.
  RegisterAggr EHRegs = getLandingPadLiveIns();
  if (!EHRegs.empty()) {
    for (Block BA : Blocks) {
      const MachineBasicBlock &B = *BA.Addr->getCode();
      if (!B.isEHPad())
        continue;

      // Prepare a list of NodeIds of the block's predecessors.
      NodeList Preds;
      for (MachineBasicBlock *PB : B.predecessors())
        Preds.push_back(findBlock(PB));

      // Build phi nodes for each live-in.
      for (RegisterRef RR : EHRegs.refs()) {
        if (RR.isReg() && !isTracked(RR))
          continue;
        Phi PA = newPhi(BA);
        uint16_t PhiFlags = NodeAttrs::PhiRef | NodeAttrs::Preserving;
        // Add def:
        Def DA = newDef(PA, RR, PhiFlags);
        PA.Addr->addMember(DA, *this);
        // Add uses (no reaching defs for phi uses):
        for (Block PBA : Preds) {
          PhiUse PUA = newPhiUse(PA, RR, PBA);
          PA.Addr->addMember(PUA, *this);
        }
      }
    }
  }

  // Build a map "PhiM" which will contain, for each block, the set
  // of references that will require phi definitions in that block.
  BlockRefsMap PhiM(getPRI());
  for (Block BA : Blocks)
    recordDefsForDF(PhiM, BA);
  for (Block BA : Blocks)
    buildPhis(PhiM, BA);

  // Link all the refs. This will recursively traverse the dominator tree.
  DefStackMap DM;
  linkBlockRefs(DM, EA);

  // Finally, remove all unused phi nodes.
  if (!(BuildCfg.Options & BuildOptions::KeepDeadPhis))
    removeUnusedPhis();
}

RegisterRef DataFlowGraph::makeRegRef(unsigned Reg, unsigned Sub) const {
  assert(RegisterRef::isRegId(Reg) || RegisterRef::isMaskId(Reg));
  assert(Reg != 0);
  if (Sub != 0)
    Reg = TRI.getSubReg(Reg, Sub);
  return RegisterRef(Reg);
}

RegisterRef DataFlowGraph::makeRegRef(const MachineOperand &Op) const {
  assert(Op.isReg() || Op.isRegMask());
  if (Op.isReg())
    return makeRegRef(Op.getReg(), Op.getSubReg());
  return RegisterRef(getPRI().getRegMaskId(Op.getRegMask()),
                     LaneBitmask::getAll());
}

// For each stack in the map DefM, push the delimiter for block B on it.
void DataFlowGraph::markBlock(NodeId B, DefStackMap &DefM) {
  // Push block delimiters.
  for (auto &P : DefM)
    P.second.start_block(B);
}

// Remove all definitions coming from block B from each stack in DefM.
void DataFlowGraph::releaseBlock(NodeId B, DefStackMap &DefM) {
  // Pop all defs from this block from the definition stack. Defs that were
  // added to the map during the traversal of instructions will not have a
  // delimiter, but for those, the whole stack will be emptied.
  for (auto &P : DefM)
    P.second.clear_block(B);

  // Finally, remove empty stacks from the map.
  for (auto I = DefM.begin(), E = DefM.end(), NextI = I; I != E; I = NextI) {
    NextI = std::next(I);
    // This preserves the validity of iterators other than I.
    if (I->second.empty())
      DefM.erase(I);
  }
}

// Push all definitions from the instruction node IA to an appropriate
// stack in DefM.
void DataFlowGraph::pushAllDefs(Instr IA, DefStackMap &DefM) {
  pushClobbers(IA, DefM);
  pushDefs(IA, DefM);
}

// Push all definitions from the instruction node IA to an appropriate
// stack in DefM.
void DataFlowGraph::pushClobbers(Instr IA, DefStackMap &DefM) {
  NodeSet Visited;
  std::set<RegisterId> Defined;

  // The important objectives of this function are:
  // - to be able to handle instructions both while the graph is being
  //   constructed, and after the graph has been constructed, and
  // - maintain proper ordering of definitions on the stack for each
  //   register reference:
  //   - if there are two or more related defs in IA (i.e. coming from
  //     the same machine operand), then only push one def on the stack,
  //   - if there are multiple unrelated defs of non-overlapping
  //     subregisters of S, then the stack for S will have both (in an
  //     unspecified order), but the order does not matter from the data-
  //     -flow perspective.

  for (Def DA : IA.Addr->members_if(IsDef, *this)) {
    if (Visited.count(DA.Id))
      continue;
    if (!(DA.Addr->getFlags() & NodeAttrs::Clobbering))
      continue;

    NodeList Rel = getRelatedRefs(IA, DA);
    Def PDA = Rel.front();
    RegisterRef RR = PDA.Addr->getRegRef(*this);

    // Push the definition on the stack for the register and all aliases.
    // The def stack traversal in linkNodeUp will check the exact aliasing.
    DefM[RR.Reg].push(DA);
    Defined.insert(RR.Reg);
    for (RegisterId A : getPRI().getAliasSet(RR.Reg)) {
      if (RegisterRef::isRegId(A) && !isTracked(RegisterRef(A)))
        continue;
      // Check that we don't push the same def twice.
      assert(A != RR.Reg);
      if (!Defined.count(A))
        DefM[A].push(DA);
    }
    // Mark all the related defs as visited.
    for (Node T : Rel)
      Visited.insert(T.Id);
  }
}

// Push all definitions from the instruction node IA to an appropriate
// stack in DefM.
void DataFlowGraph::pushDefs(Instr IA, DefStackMap &DefM) {
  NodeSet Visited;
#ifndef NDEBUG
  std::set<RegisterId> Defined;
#endif

  // The important objectives of this function are:
  // - to be able to handle instructions both while the graph is being
  //   constructed, and after the graph has been constructed, and
  // - maintain proper ordering of definitions on the stack for each
  //   register reference:
  //   - if there are two or more related defs in IA (i.e. coming from
  //     the same machine operand), then only push one def on the stack,
  //   - if there are multiple unrelated defs of non-overlapping
  //     subregisters of S, then the stack for S will have both (in an
  //     unspecified order), but the order does not matter from the data-
  //     -flow perspective.

  for (Def DA : IA.Addr->members_if(IsDef, *this)) {
    if (Visited.count(DA.Id))
      continue;
    if (DA.Addr->getFlags() & NodeAttrs::Clobbering)
      continue;

    NodeList Rel = getRelatedRefs(IA, DA);
    Def PDA = Rel.front();
    RegisterRef RR = PDA.Addr->getRegRef(*this);
#ifndef NDEBUG
    // Assert if the register is defined in two or more unrelated defs.
    // This could happen if there are two or more def operands defining it.
    if (!Defined.insert(RR.Reg).second) {
      MachineInstr *MI = Stmt(IA).Addr->getCode();
      dbgs() << "Multiple definitions of register: " << Print(RR, *this)
             << " in\n  " << *MI << "in " << printMBBReference(*MI->getParent())
             << '\n';
      llvm_unreachable(nullptr);
    }
#endif
    // Push the definition on the stack for the register and all aliases.
    // The def stack traversal in linkNodeUp will check the exact aliasing.
    DefM[RR.Reg].push(DA);
    for (RegisterId A : getPRI().getAliasSet(RR.Reg)) {
      if (RegisterRef::isRegId(A) && !isTracked(RegisterRef(A)))
        continue;
      // Check that we don't push the same def twice.
      assert(A != RR.Reg);
      DefM[A].push(DA);
    }
    // Mark all the related defs as visited.
    for (Node T : Rel)
      Visited.insert(T.Id);
  }
}

// Return the list of all reference nodes related to RA, including RA itself.
// See "getNextRelated" for the meaning of a "related reference".
NodeList DataFlowGraph::getRelatedRefs(Instr IA, Ref RA) const {
  assert(IA.Id != 0 && RA.Id != 0);

  NodeList Refs;
  NodeId Start = RA.Id;
  do {
    Refs.push_back(RA);
    RA = getNextRelated(IA, RA);
  } while (RA.Id != 0 && RA.Id != Start);
  return Refs;
}

// Clear all information in the graph.
void DataFlowGraph::reset() {
  Memory.clear();
  BlockNodes.clear();
  TrackedUnits.clear();
  ReservedRegs.clear();
  TheFunc = Func();
}

// Return the next reference node in the instruction node IA that is related
// to RA. Conceptually, two reference nodes are related if they refer to the
// same instance of a register access, but differ in flags or other minor
// characteristics. Specific examples of related nodes are shadow reference
// nodes.
// Return the equivalent of nullptr if there are no more related references.
Ref DataFlowGraph::getNextRelated(Instr IA, Ref RA) const {
  assert(IA.Id != 0 && RA.Id != 0);

  auto IsRelated = [this, RA](Ref TA) -> bool {
    if (TA.Addr->getKind() != RA.Addr->getKind())
      return false;
    if (!getPRI().equal_to(TA.Addr->getRegRef(*this),
                           RA.Addr->getRegRef(*this))) {
      return false;
    }
    return true;
  };

  RegisterRef RR = RA.Addr->getRegRef(*this);
  if (IA.Addr->getKind() == NodeAttrs::Stmt) {
    auto Cond = [&IsRelated, RA](Ref TA) -> bool {
      return IsRelated(TA) && &RA.Addr->getOp() == &TA.Addr->getOp();
    };
    return RA.Addr->getNextRef(RR, Cond, true, *this);
  }

  assert(IA.Addr->getKind() == NodeAttrs::Phi);
  auto Cond = [&IsRelated, RA](Ref TA) -> bool {
    if (!IsRelated(TA))
      return false;
    if (TA.Addr->getKind() != NodeAttrs::Use)
      return true;
    // For phi uses, compare predecessor blocks.
    return PhiUse(TA).Addr->getPredecessor() ==
           PhiUse(RA).Addr->getPredecessor();
  };
  return RA.Addr->getNextRef(RR, Cond, true, *this);
}

// Find the next node related to RA in IA that satisfies condition P.
// If such a node was found, return a pair where the second element is the
// located node. If such a node does not exist, return a pair where the
// first element is the element after which such a node should be inserted,
// and the second element is a null-address.
template <typename Predicate>
std::pair<Ref, Ref> DataFlowGraph::locateNextRef(Instr IA, Ref RA,
                                                 Predicate P) const {
  assert(IA.Id != 0 && RA.Id != 0);

  Ref NA;
  NodeId Start = RA.Id;
  while (true) {
    NA = getNextRelated(IA, RA);
    if (NA.Id == 0 || NA.Id == Start)
      break;
    if (P(NA))
      break;
    RA = NA;
  }

  if (NA.Id != 0 && NA.Id != Start)
    return std::make_pair(RA, NA);
  return std::make_pair(RA, Ref());
}

// Get the next shadow node in IA corresponding to RA, and optionally create
// such a node if it does not exist.
Ref DataFlowGraph::getNextShadow(Instr IA, Ref RA, bool Create) {
  assert(IA.Id != 0 && RA.Id != 0);

  uint16_t Flags = RA.Addr->getFlags() | NodeAttrs::Shadow;
  auto IsShadow = [Flags](Ref TA) -> bool {
    return TA.Addr->getFlags() == Flags;
  };
  auto Loc = locateNextRef(IA, RA, IsShadow);
  if (Loc.second.Id != 0 || !Create)
    return Loc.second;

  // Create a copy of RA and mark is as shadow.
  Ref NA = cloneNode(RA);
  NA.Addr->setFlags(Flags | NodeAttrs::Shadow);
  IA.Addr->addMemberAfter(Loc.first, NA, *this);
  return NA;
}

// Create a new statement node in the block node BA that corresponds to
// the machine instruction MI.
void DataFlowGraph::buildStmt(Block BA, MachineInstr &In) {
  Stmt SA = newStmt(BA, &In);

  auto isCall = [](const MachineInstr &In) -> bool {
    if (In.isCall())
      return true;
    // Is tail call?
    if (In.isBranch()) {
      for (const MachineOperand &Op : In.operands())
        if (Op.isGlobal() || Op.isSymbol())
          return true;
      // Assume indirect branches are calls. This is for the purpose of
      // keeping implicit operands, and so it won't hurt on intra-function
      // indirect branches.
      if (In.isIndirectBranch())
        return true;
    }
    return false;
  };

  auto isDefUndef = [this](const MachineInstr &In, RegisterRef DR) -> bool {
    // This instruction defines DR. Check if there is a use operand that
    // would make DR live on entry to the instruction.
    for (const MachineOperand &Op : In.all_uses()) {
      if (Op.getReg() == 0 || Op.isUndef())
        continue;
      RegisterRef UR = makeRegRef(Op);
      if (getPRI().alias(DR, UR))
        return false;
    }
    return true;
  };

  bool IsCall = isCall(In);
  unsigned NumOps = In.getNumOperands();

  // Avoid duplicate implicit defs. This will not detect cases of implicit
  // defs that define registers that overlap, but it is not clear how to
  // interpret that in the absence of explicit defs. Overlapping explicit
  // defs are likely illegal already.
  BitVector DoneDefs(TRI.getNumRegs());
  // Process explicit defs first.
  for (unsigned OpN = 0; OpN < NumOps; ++OpN) {
    MachineOperand &Op = In.getOperand(OpN);
    if (!Op.isReg() || !Op.isDef() || Op.isImplicit())
      continue;
    Register R = Op.getReg();
    if (!R || !R.isPhysical() || !isTracked(RegisterRef(R)))
      continue;
    uint16_t Flags = NodeAttrs::None;
    if (TOI.isPreserving(In, OpN)) {
      Flags |= NodeAttrs::Preserving;
      // If the def is preserving, check if it is also undefined.
      if (isDefUndef(In, makeRegRef(Op)))
        Flags |= NodeAttrs::Undef;
    }
    if (TOI.isClobbering(In, OpN))
      Flags |= NodeAttrs::Clobbering;
    if (TOI.isFixedReg(In, OpN))
      Flags |= NodeAttrs::Fixed;
    if (IsCall && Op.isDead())
      Flags |= NodeAttrs::Dead;
    Def DA = newDef(SA, Op, Flags);
    SA.Addr->addMember(DA, *this);
    assert(!DoneDefs.test(R));
    DoneDefs.set(R);
  }

  // Process reg-masks (as clobbers).
  BitVector DoneClobbers(TRI.getNumRegs());
  for (unsigned OpN = 0; OpN < NumOps; ++OpN) {
    MachineOperand &Op = In.getOperand(OpN);
    if (!Op.isRegMask())
      continue;
    uint16_t Flags = NodeAttrs::Clobbering | NodeAttrs::Fixed | NodeAttrs::Dead;
    Def DA = newDef(SA, Op, Flags);
    SA.Addr->addMember(DA, *this);
    // Record all clobbered registers in DoneDefs.
    const uint32_t *RM = Op.getRegMask();
    for (unsigned i = 1, e = TRI.getNumRegs(); i != e; ++i) {
      if (!isTracked(RegisterRef(i)))
        continue;
      if (!(RM[i / 32] & (1u << (i % 32))))
        DoneClobbers.set(i);
    }
  }

  // Process implicit defs, skipping those that have already been added
  // as explicit.
  for (unsigned OpN = 0; OpN < NumOps; ++OpN) {
    MachineOperand &Op = In.getOperand(OpN);
    if (!Op.isReg() || !Op.isDef() || !Op.isImplicit())
      continue;
    Register R = Op.getReg();
    if (!R || !R.isPhysical() || !isTracked(RegisterRef(R)) || DoneDefs.test(R))
      continue;
    RegisterRef RR = makeRegRef(Op);
    uint16_t Flags = NodeAttrs::None;
    if (TOI.isPreserving(In, OpN)) {
      Flags |= NodeAttrs::Preserving;
      // If the def is preserving, check if it is also undefined.
      if (isDefUndef(In, RR))
        Flags |= NodeAttrs::Undef;
    }
    if (TOI.isClobbering(In, OpN))
      Flags |= NodeAttrs::Clobbering;
    if (TOI.isFixedReg(In, OpN))
      Flags |= NodeAttrs::Fixed;
    if (IsCall && Op.isDead()) {
      if (DoneClobbers.test(R))
        continue;
      Flags |= NodeAttrs::Dead;
    }
    Def DA = newDef(SA, Op, Flags);
    SA.Addr->addMember(DA, *this);
    DoneDefs.set(R);
  }

  for (unsigned OpN = 0; OpN < NumOps; ++OpN) {
    MachineOperand &Op = In.getOperand(OpN);
    if (!Op.isReg() || !Op.isUse())
      continue;
    Register R = Op.getReg();
    if (!R || !R.isPhysical() || !isTracked(RegisterRef(R)))
      continue;
    uint16_t Flags = NodeAttrs::None;
    if (Op.isUndef())
      Flags |= NodeAttrs::Undef;
    if (TOI.isFixedReg(In, OpN))
      Flags |= NodeAttrs::Fixed;
    Use UA = newUse(SA, Op, Flags);
    SA.Addr->addMember(UA, *this);
  }
}

// Scan all defs in the block node BA and record in PhiM the locations of
// phi nodes corresponding to these defs.
void DataFlowGraph::recordDefsForDF(BlockRefsMap &PhiM, Block BA) {
  // Check all defs from block BA and record them in each block in BA's
  // iterated dominance frontier. This information will later be used to
  // create phi nodes.
  MachineBasicBlock *BB = BA.Addr->getCode();
  assert(BB);
  auto DFLoc = MDF.find(BB);
  if (DFLoc == MDF.end() || DFLoc->second.empty())
    return;

  // Traverse all instructions in the block and collect the set of all
  // defined references. For each reference there will be a phi created
  // in the block's iterated dominance frontier.
  // This is done to make sure that each defined reference gets only one
  // phi node, even if it is defined multiple times.
  RegisterAggr Defs(getPRI());
  for (Instr IA : BA.Addr->members(*this)) {
    for (Ref RA : IA.Addr->members_if(IsDef, *this)) {
      RegisterRef RR = RA.Addr->getRegRef(*this);
      if (RR.isReg() && isTracked(RR))
        Defs.insert(RR);
    }
  }

  // Calculate the iterated dominance frontier of BB.
  const MachineDominanceFrontier::DomSetType &DF = DFLoc->second;
  SetVector<MachineBasicBlock *> IDF(DF.begin(), DF.end());
  for (unsigned i = 0; i < IDF.size(); ++i) {
    auto F = MDF.find(IDF[i]);
    if (F != MDF.end())
      IDF.insert(F->second.begin(), F->second.end());
  }

  // Finally, add the set of defs to each block in the iterated dominance
  // frontier.
  for (auto *DB : IDF) {
    Block DBA = findBlock(DB);
    PhiM[DBA.Id].insert(Defs);
  }
}

// Given the locations of phi nodes in the map PhiM, create the phi nodes
// that are located in the block node BA.
void DataFlowGraph::buildPhis(BlockRefsMap &PhiM, Block BA) {
  // Check if this blocks has any DF defs, i.e. if there are any defs
  // that this block is in the iterated dominance frontier of.
  auto HasDF = PhiM.find(BA.Id);
  if (HasDF == PhiM.end() || HasDF->second.empty())
    return;

  // Prepare a list of NodeIds of the block's predecessors.
  NodeList Preds;
  const MachineBasicBlock *MBB = BA.Addr->getCode();
  for (MachineBasicBlock *PB : MBB->predecessors())
    Preds.push_back(findBlock(PB));

  const RegisterAggr &Defs = PhiM[BA.Id];
  uint16_t PhiFlags = NodeAttrs::PhiRef | NodeAttrs::Preserving;

  for (RegisterRef RR : Defs.refs()) {
    Phi PA = newPhi(BA);
    PA.Addr->addMember(newDef(PA, RR, PhiFlags), *this);

    // Add phi uses.
    for (Block PBA : Preds) {
      PA.Addr->addMember(newPhiUse(PA, RR, PBA), *this);
    }
  }
}

// Remove any unneeded phi nodes that were created during the build process.
void DataFlowGraph::removeUnusedPhis() {
  // This will remove unused phis, i.e. phis where each def does not reach
  // any uses or other defs. This will not detect or remove circular phi
  // chains that are otherwise dead. Unused/dead phis are created during
  // the build process and this function is intended to remove these cases
  // that are easily determinable to be unnecessary.

  SetVector<NodeId> PhiQ;
  for (Block BA : TheFunc.Addr->members(*this)) {
    for (auto P : BA.Addr->members_if(IsPhi, *this))
      PhiQ.insert(P.Id);
  }

  static auto HasUsedDef = [](NodeList &Ms) -> bool {
    for (Node M : Ms) {
      if (M.Addr->getKind() != NodeAttrs::Def)
        continue;
      Def DA = M;
      if (DA.Addr->getReachedDef() != 0 || DA.Addr->getReachedUse() != 0)
        return true;
    }
    return false;
  };

  // Any phi, if it is removed, may affect other phis (make them dead).
  // For each removed phi, collect the potentially affected phis and add
  // them back to the queue.
  while (!PhiQ.empty()) {
    auto PA = addr<PhiNode *>(PhiQ[0]);
    PhiQ.remove(PA.Id);
    NodeList Refs = PA.Addr->members(*this);
    if (HasUsedDef(Refs))
      continue;
    for (Ref RA : Refs) {
      if (NodeId RD = RA.Addr->getReachingDef()) {
        auto RDA = addr<DefNode *>(RD);
        Instr OA = RDA.Addr->getOwner(*this);
        if (IsPhi(OA))
          PhiQ.insert(OA.Id);
      }
      if (RA.Addr->isDef())
        unlinkDef(RA, true);
      else
        unlinkUse(RA, true);
    }
    Block BA = PA.Addr->getOwner(*this);
    BA.Addr->removeMember(PA, *this);
  }
}

// For a given reference node TA in an instruction node IA, connect the
// reaching def of TA to the appropriate def node. Create any shadow nodes
// as appropriate.
template <typename T>
void DataFlowGraph::linkRefUp(Instr IA, NodeAddr<T> TA, DefStack &DS) {
  if (DS.empty())
    return;
  RegisterRef RR = TA.Addr->getRegRef(*this);
  NodeAddr<T> TAP;

  // References from the def stack that have been examined so far.
  RegisterAggr Defs(getPRI());

  for (auto I = DS.top(), E = DS.bottom(); I != E; I.down()) {
    RegisterRef QR = I->Addr->getRegRef(*this);

    // Skip all defs that are aliased to any of the defs that we have already
    // seen. If this completes a cover of RR, stop the stack traversal.
    bool Alias = Defs.hasAliasOf(QR);
    bool Cover = Defs.insert(QR).hasCoverOf(RR);
    if (Alias) {
      if (Cover)
        break;
      continue;
    }

    // The reaching def.
    Def RDA = *I;

    // Pick the reached node.
    if (TAP.Id == 0) {
      TAP = TA;
    } else {
      // Mark the existing ref as "shadow" and create a new shadow.
      TAP.Addr->setFlags(TAP.Addr->getFlags() | NodeAttrs::Shadow);
      TAP = getNextShadow(IA, TAP, true);
    }

    // Create the link.
    TAP.Addr->linkToDef(TAP.Id, RDA);

    if (Cover)
      break;
  }
}

// Create data-flow links for all reference nodes in the statement node SA.
template <typename Predicate>
void DataFlowGraph::linkStmtRefs(DefStackMap &DefM, Stmt SA, Predicate P) {
#ifndef NDEBUG
  RegisterSet Defs(getPRI());
#endif

  // Link all nodes (upwards in the data-flow) with their reaching defs.
  for (Ref RA : SA.Addr->members_if(P, *this)) {
    uint16_t Kind = RA.Addr->getKind();
    assert(Kind == NodeAttrs::Def || Kind == NodeAttrs::Use);
    RegisterRef RR = RA.Addr->getRegRef(*this);
#ifndef NDEBUG
    // Do not expect multiple defs of the same reference.
    assert(Kind != NodeAttrs::Def || !Defs.count(RR));
    Defs.insert(RR);
#endif

    auto F = DefM.find(RR.Reg);
    if (F == DefM.end())
      continue;
    DefStack &DS = F->second;
    if (Kind == NodeAttrs::Use)
      linkRefUp<UseNode *>(SA, RA, DS);
    else if (Kind == NodeAttrs::Def)
      linkRefUp<DefNode *>(SA, RA, DS);
    else
      llvm_unreachable("Unexpected node in instruction");
  }
}

// Create data-flow links for all instructions in the block node BA. This
// will include updating any phi nodes in BA.
void DataFlowGraph::linkBlockRefs(DefStackMap &DefM, Block BA) {
  // Push block delimiters.
  markBlock(BA.Id, DefM);

  auto IsClobber = [](Ref RA) -> bool {
    return IsDef(RA) && (RA.Addr->getFlags() & NodeAttrs::Clobbering);
  };
  auto IsNoClobber = [](Ref RA) -> bool {
    return IsDef(RA) && !(RA.Addr->getFlags() & NodeAttrs::Clobbering);
  };

  assert(BA.Addr && "block node address is needed to create a data-flow link");
  // For each non-phi instruction in the block, link all the defs and uses
  // to their reaching defs. For any member of the block (including phis),
  // push the defs on the corresponding stacks.
  for (Instr IA : BA.Addr->members(*this)) {
    // Ignore phi nodes here. They will be linked part by part from the
    // predecessors.
    if (IA.Addr->getKind() == NodeAttrs::Stmt) {
      linkStmtRefs(DefM, IA, IsUse);
      linkStmtRefs(DefM, IA, IsClobber);
    }

    // Push the definitions on the stack.
    pushClobbers(IA, DefM);

    if (IA.Addr->getKind() == NodeAttrs::Stmt)
      linkStmtRefs(DefM, IA, IsNoClobber);

    pushDefs(IA, DefM);
  }

  // Recursively process all children in the dominator tree.
  MachineDomTreeNode *N = MDT.getNode(BA.Addr->getCode());
  for (auto *I : *N) {
    MachineBasicBlock *SB = I->getBlock();
    Block SBA = findBlock(SB);
    linkBlockRefs(DefM, SBA);
  }

  // Link the phi uses from the successor blocks.
  auto IsUseForBA = [BA](Node NA) -> bool {
    if (NA.Addr->getKind() != NodeAttrs::Use)
      return false;
    assert(NA.Addr->getFlags() & NodeAttrs::PhiRef);
    return PhiUse(NA).Addr->getPredecessor() == BA.Id;
  };

  RegisterAggr EHLiveIns = getLandingPadLiveIns();
  MachineBasicBlock *MBB = BA.Addr->getCode();

  for (MachineBasicBlock *SB : MBB->successors()) {
    bool IsEHPad = SB->isEHPad();
    Block SBA = findBlock(SB);
    for (Instr IA : SBA.Addr->members_if(IsPhi, *this)) {
      // Do not link phi uses for landing pad live-ins.
      if (IsEHPad) {
        // Find what register this phi is for.
        Ref RA = IA.Addr->getFirstMember(*this);
        assert(RA.Id != 0);
        if (EHLiveIns.hasCoverOf(RA.Addr->getRegRef(*this)))
          continue;
      }
      // Go over each phi use associated with MBB, and link it.
      for (auto U : IA.Addr->members_if(IsUseForBA, *this)) {
        PhiUse PUA = U;
        RegisterRef RR = PUA.Addr->getRegRef(*this);
        linkRefUp<UseNode *>(IA, PUA, DefM[RR.Reg]);
      }
    }
  }

  // Pop all defs from this block from the definition stacks.
  releaseBlock(BA.Id, DefM);
}

// Remove the use node UA from any data-flow and structural links.
void DataFlowGraph::unlinkUseDF(Use UA) {
  NodeId RD = UA.Addr->getReachingDef();
  NodeId Sib = UA.Addr->getSibling();

  if (RD == 0) {
    assert(Sib == 0);
    return;
  }

  auto RDA = addr<DefNode *>(RD);
  auto TA = addr<UseNode *>(RDA.Addr->getReachedUse());
  if (TA.Id == UA.Id) {
    RDA.Addr->setReachedUse(Sib);
    return;
  }

  while (TA.Id != 0) {
    NodeId S = TA.Addr->getSibling();
    if (S == UA.Id) {
      TA.Addr->setSibling(UA.Addr->getSibling());
      return;
    }
    TA = addr<UseNode *>(S);
  }
}

// Remove the def node DA from any data-flow and structural links.
void DataFlowGraph::unlinkDefDF(Def DA) {
  //
  //         RD
  //         | reached
  //         | def
  //         :
  //         .
  //        +----+
  // ... -- | DA | -- ... -- 0  : sibling chain of DA
  //        +----+
  //         |  | reached
  //         |  : def
  //         |  .
  //         | ...  : Siblings (defs)
  //         |
  //         : reached
  //         . use
  //        ... : sibling chain of reached uses

  NodeId RD = DA.Addr->getReachingDef();

  // Visit all siblings of the reached def and reset their reaching defs.
  // Also, defs reached by DA are now "promoted" to being reached by RD,
  // so all of them will need to be spliced into the sibling chain where
  // DA belongs.
  auto getAllNodes = [this](NodeId N) -> NodeList {
    NodeList Res;
    while (N) {
      auto RA = addr<RefNode *>(N);
      // Keep the nodes in the exact sibling order.
      Res.push_back(RA);
      N = RA.Addr->getSibling();
    }
    return Res;
  };
  NodeList ReachedDefs = getAllNodes(DA.Addr->getReachedDef());
  NodeList ReachedUses = getAllNodes(DA.Addr->getReachedUse());

  if (RD == 0) {
    for (Ref I : ReachedDefs)
      I.Addr->setSibling(0);
    for (Ref I : ReachedUses)
      I.Addr->setSibling(0);
  }
  for (Def I : ReachedDefs)
    I.Addr->setReachingDef(RD);
  for (Use I : ReachedUses)
    I.Addr->setReachingDef(RD);

  NodeId Sib = DA.Addr->getSibling();
  if (RD == 0) {
    assert(Sib == 0);
    return;
  }

  // Update the reaching def node and remove DA from the sibling list.
  auto RDA = addr<DefNode *>(RD);
  auto TA = addr<DefNode *>(RDA.Addr->getReachedDef());
  if (TA.Id == DA.Id) {
    // If DA is the first reached def, just update the RD's reached def
    // to the DA's sibling.
    RDA.Addr->setReachedDef(Sib);
  } else {
    // Otherwise, traverse the sibling list of the reached defs and remove
    // DA from it.
    while (TA.Id != 0) {
      NodeId S = TA.Addr->getSibling();
      if (S == DA.Id) {
        TA.Addr->setSibling(Sib);
        break;
      }
      TA = addr<DefNode *>(S);
    }
  }

  // Splice the DA's reached defs into the RDA's reached def chain.
  if (!ReachedDefs.empty()) {
    auto Last = Def(ReachedDefs.back());
    Last.Addr->setSibling(RDA.Addr->getReachedDef());
    RDA.Addr->setReachedDef(ReachedDefs.front().Id);
  }
  // Splice the DA's reached uses into the RDA's reached use chain.
  if (!ReachedUses.empty()) {
    auto Last = Use(ReachedUses.back());
    Last.Addr->setSibling(RDA.Addr->getReachedUse());
    RDA.Addr->setReachedUse(ReachedUses.front().Id);
  }
}

bool DataFlowGraph::isTracked(RegisterRef RR) const {
  return !disjoint(getPRI().getUnits(RR), TrackedUnits);
}

bool DataFlowGraph::hasUntrackedRef(Stmt S, bool IgnoreReserved) const {
  SmallVector<MachineOperand *> Ops;

  for (Ref R : S.Addr->members(*this)) {
    Ops.push_back(&R.Addr->getOp());
    RegisterRef RR = R.Addr->getRegRef(*this);
    if (IgnoreReserved && RR.isReg() && ReservedRegs[RR.idx()])
      continue;
    if (!isTracked(RR))
      return true;
  }
  for (const MachineOperand &Op : S.Addr->getCode()->operands()) {
    if (!Op.isReg() && !Op.isRegMask())
      continue;
    if (!llvm::is_contained(Ops, &Op))
      return true;
  }
  return false;
}

} // end namespace llvm::rdf
