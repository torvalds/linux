//===- RDFGraph.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Target-independent, SSA-based data flow graph for register data flow (RDF)
// for a non-SSA program representation (e.g. post-RA machine code).
//
//
// *** Introduction
//
// The RDF graph is a collection of nodes, each of which denotes some element
// of the program. There are two main types of such elements: code and refe-
// rences. Conceptually, "code" is something that represents the structure
// of the program, e.g. basic block or a statement, while "reference" is an
// instance of accessing a register, e.g. a definition or a use. Nodes are
// connected with each other based on the structure of the program (such as
// blocks, instructions, etc.), and based on the data flow (e.g. reaching
// definitions, reached uses, etc.). The single-reaching-definition principle
// of SSA is generally observed, although, due to the non-SSA representation
// of the program, there are some differences between the graph and a "pure"
// SSA representation.
//
//
// *** Implementation remarks
//
// Since the graph can contain a large number of nodes, memory consumption
// was one of the major design considerations. As a result, there is a single
// base class NodeBase which defines all members used by all possible derived
// classes. The members are arranged in a union, and a derived class cannot
// add any data members of its own. Each derived class only defines the
// functional interface, i.e. member functions. NodeBase must be a POD,
// which implies that all of its members must also be PODs.
// Since nodes need to be connected with other nodes, pointers have been
// replaced with 32-bit identifiers: each node has an id of type NodeId.
// There are mapping functions in the graph that translate between actual
// memory addresses and the corresponding identifiers.
// A node id of 0 is equivalent to nullptr.
//
//
// *** Structure of the graph
//
// A code node is always a collection of other nodes. For example, a code
// node corresponding to a basic block will contain code nodes corresponding
// to instructions. In turn, a code node corresponding to an instruction will
// contain a list of reference nodes that correspond to the definitions and
// uses of registers in that instruction. The members are arranged into a
// circular list, which is yet another consequence of the effort to save
// memory: for each member node it should be possible to obtain its owner,
// and it should be possible to access all other members. There are other
// ways to accomplish that, but the circular list seemed the most natural.
//
// +- CodeNode -+
// |            | <---------------------------------------------------+
// +-+--------+-+                                                     |
//   |FirstM  |LastM                                                  |
//   |        +-------------------------------------+                 |
//   |                                              |                 |
//   V                                              V                 |
//  +----------+ Next +----------+ Next       Next +----------+ Next  |
//  |          |----->|          |-----> ... ----->|          |----->-+
//  +- Member -+      +- Member -+                 +- Member -+
//
// The order of members is such that related reference nodes (see below)
// should be contiguous on the member list.
//
// A reference node is a node that encapsulates an access to a register,
// in other words, data flowing into or out of a register. There are two
// major kinds of reference nodes: defs and uses. A def node will contain
// the id of the first reached use, and the id of the first reached def.
// Each def and use will contain the id of the reaching def, and also the
// id of the next reached def (for def nodes) or use (for use nodes).
// The "next node sharing the same reaching def" is denoted as "sibling".
// In summary:
// - Def node contains: reaching def, sibling, first reached def, and first
// reached use.
// - Use node contains: reaching def and sibling.
//
// +-- DefNode --+
// | R2 = ...    | <---+--------------------+
// ++---------+--+     |                    |
//  |Reached  |Reached |                    |
//  |Def      |Use     |                    |
//  |         |        |Reaching            |Reaching
//  |         V        |Def                 |Def
//  |      +-- UseNode --+ Sib  +-- UseNode --+ Sib       Sib
//  |      | ... = R2    |----->| ... = R2    |----> ... ----> 0
//  |      +-------------+      +-------------+
//  V
// +-- DefNode --+ Sib
// | R2 = ...    |----> ...
// ++---------+--+
//  |         |
//  |         |
// ...       ...
//
// To get a full picture, the circular lists connecting blocks within a
// function, instructions within a block, etc. should be superimposed with
// the def-def, def-use links shown above.
// To illustrate this, consider a small example in a pseudo-assembly:
// foo:
//   add r2, r0, r1   ; r2 = r0+r1
//   addi r0, r2, 1   ; r0 = r2+1
//   ret r0           ; return value in r0
//
// The graph (in a format used by the debugging functions) would look like:
//
//   DFG dump:[
//   f1: Function foo
//   b2: === %bb.0 === preds(0), succs(0):
//   p3: phi [d4<r0>(,d12,u9):]
//   p5: phi [d6<r1>(,,u10):]
//   s7: add [d8<r2>(,,u13):, u9<r0>(d4):, u10<r1>(d6):]
//   s11: addi [d12<r0>(d4,,u15):, u13<r2>(d8):]
//   s14: ret [u15<r0>(d12):]
//   ]
//
// The f1, b2, p3, etc. are node ids. The letter is prepended to indicate the
// kind of the node (i.e. f - function, b - basic block, p - phi, s - state-
// ment, d - def, u - use).
// The format of a def node is:
//   dN<R>(rd,d,u):sib,
// where
//   N   - numeric node id,
//   R   - register being defined
//   rd  - reaching def,
//   d   - reached def,
//   u   - reached use,
//   sib - sibling.
// The format of a use node is:
//   uN<R>[!](rd):sib,
// where
//   N   - numeric node id,
//   R   - register being used,
//   rd  - reaching def,
//   sib - sibling.
// Possible annotations (usually preceding the node id):
//   +   - preserving def,
//   ~   - clobbering def,
//   "   - shadow ref (follows the node id),
//   !   - fixed register (appears after register name).
//
// The circular lists are not explicit in the dump.
//
//
// *** Node attributes
//
// NodeBase has a member "Attrs", which is the primary way of determining
// the node's characteristics. The fields in this member decide whether
// the node is a code node or a reference node (i.e. node's "type"), then
// within each type, the "kind" determines what specifically this node
// represents. The remaining bits, "flags", contain additional information
// that is even more detailed than the "kind".
// CodeNode's kinds are:
// - Phi:   Phi node, members are reference nodes.
// - Stmt:  Statement, members are reference nodes.
// - Block: Basic block, members are instruction nodes (i.e. Phi or Stmt).
// - Func:  The whole function. The members are basic block nodes.
// RefNode's kinds are:
// - Use.
// - Def.
//
// Meaning of flags:
// - Preserving: applies only to defs. A preserving def is one that can
//   preserve some of the original bits among those that are included in
//   the register associated with that def. For example, if R0 is a 32-bit
//   register, but a def can only change the lower 16 bits, then it will
//   be marked as preserving.
// - Shadow: a reference that has duplicates holding additional reaching
//   defs (see more below).
// - Clobbering: applied only to defs, indicates that the value generated
//   by this def is unspecified. A typical example would be volatile registers
//   after function calls.
// - Fixed: the register in this def/use cannot be replaced with any other
//   register. A typical case would be a parameter register to a call, or
//   the register with the return value from a function.
// - Undef: the register in this reference the register is assumed to have
//   no pre-existing value, even if it appears to be reached by some def.
//   This is typically used to prevent keeping registers artificially live
//   in cases when they are defined via predicated instructions. For example:
//     r0 = add-if-true cond, r10, r11                (1)
//     r0 = add-if-false cond, r12, r13, implicit r0  (2)
//     ... = r0                                       (3)
//   Before (1), r0 is not intended to be live, and the use of r0 in (3) is
//   not meant to be reached by any def preceding (1). However, since the
//   defs in (1) and (2) are both preserving, these properties alone would
//   imply that the use in (3) may indeed be reached by some prior def.
//   Adding Undef flag to the def in (1) prevents that. The Undef flag
//   may be applied to both defs and uses.
// - Dead: applies only to defs. The value coming out of a "dead" def is
//   assumed to be unused, even if the def appears to be reaching other defs
//   or uses. The motivation for this flag comes from dead defs on function
//   calls: there is no way to determine if such a def is dead without
//   analyzing the target's ABI. Hence the graph should contain this info,
//   as it is unavailable otherwise. On the other hand, a def without any
//   uses on a typical instruction is not the intended target for this flag.
//
// *** Shadow references
//
// It may happen that a super-register can have two (or more) non-overlapping
// sub-registers. When both of these sub-registers are defined and followed
// by a use of the super-register, the use of the super-register will not
// have a unique reaching def: both defs of the sub-registers need to be
// accounted for. In such cases, a duplicate use of the super-register is
// added and it points to the extra reaching def. Both uses are marked with
// a flag "shadow". Example:
// Assume t0 is a super-register of r0 and r1, r0 and r1 do not overlap:
//   set r0, 1        ; r0 = 1
//   set r1, 1        ; r1 = 1
//   addi t1, t0, 1   ; t1 = t0+1
//
// The DFG:
//   s1: set [d2<r0>(,,u9):]
//   s3: set [d4<r1>(,,u10):]
//   s5: addi [d6<t1>(,,):, u7"<t0>(d2):, u8"<t0>(d4):]
//
// The statement s5 has two use nodes for t0: u7" and u9". The quotation
// mark " indicates that the node is a shadow.
//

#ifndef LLVM_CODEGEN_RDFGRAPH_H
#define LLVM_CODEGEN_RDFGRAPH_H

#include "RDFRegisters.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

// RDF uses uint32_t to refer to registers. This is to ensure that the type
// size remains specific. In other places, registers are often stored using
// unsigned.
static_assert(sizeof(uint32_t) == sizeof(unsigned), "Those should be equal");

namespace llvm {

class MachineBasicBlock;
class MachineDominanceFrontier;
class MachineDominatorTree;
class MachineFunction;
class MachineInstr;
class MachineOperand;
class raw_ostream;
class TargetInstrInfo;
class TargetRegisterInfo;

namespace rdf {

using NodeId = uint32_t;

struct DataFlowGraph;

struct NodeAttrs {
  // clang-format off
  enum : uint16_t {
    None          = 0x0000,   // Nothing

    // Types: 2 bits
    TypeMask      = 0x0003,
    Code          = 0x0001,   // 01, Container
    Ref           = 0x0002,   // 10, Reference

    // Kind: 3 bits
    KindMask      = 0x0007 << 2,
    Def           = 0x0001 << 2,  // 001
    Use           = 0x0002 << 2,  // 010
    Phi           = 0x0003 << 2,  // 011
    Stmt          = 0x0004 << 2,  // 100
    Block         = 0x0005 << 2,  // 101
    Func          = 0x0006 << 2,  // 110

    // Flags: 7 bits for now
    FlagMask      = 0x007F << 5,
    Shadow        = 0x0001 << 5,  // 0000001, Has extra reaching defs.
    Clobbering    = 0x0002 << 5,  // 0000010, Produces unspecified values.
    PhiRef        = 0x0004 << 5,  // 0000100, Member of PhiNode.
    Preserving    = 0x0008 << 5,  // 0001000, Def can keep original bits.
    Fixed         = 0x0010 << 5,  // 0010000, Fixed register.
    Undef         = 0x0020 << 5,  // 0100000, Has no pre-existing value.
    Dead          = 0x0040 << 5,  // 1000000, Does not define a value.
  };
  // clang-format on

  static uint16_t type(uint16_t T) { //
    return T & TypeMask;
  }
  static uint16_t kind(uint16_t T) { //
    return T & KindMask;
  }
  static uint16_t flags(uint16_t T) { //
    return T & FlagMask;
  }
  static uint16_t set_type(uint16_t A, uint16_t T) {
    return (A & ~TypeMask) | T;
  }

  static uint16_t set_kind(uint16_t A, uint16_t K) {
    return (A & ~KindMask) | K;
  }

  static uint16_t set_flags(uint16_t A, uint16_t F) {
    return (A & ~FlagMask) | F;
  }

  // Test if A contains B.
  static bool contains(uint16_t A, uint16_t B) {
    if (type(A) != Code)
      return false;
    uint16_t KB = kind(B);
    switch (kind(A)) {
    case Func:
      return KB == Block;
    case Block:
      return KB == Phi || KB == Stmt;
    case Phi:
    case Stmt:
      return type(B) == Ref;
    }
    return false;
  }
};

struct BuildOptions {
  enum : unsigned {
    None = 0x00,
    KeepDeadPhis = 0x01, // Do not remove dead phis during build.
    OmitReserved = 0x02, // Do not track reserved registers.
  };
};

template <typename T> struct NodeAddr {
  NodeAddr() = default;
  NodeAddr(T A, NodeId I) : Addr(A), Id(I) {}

  // Type cast (casting constructor). The reason for having this class
  // instead of std::pair.
  template <typename S>
  NodeAddr(const NodeAddr<S> &NA) : Addr(static_cast<T>(NA.Addr)), Id(NA.Id) {}

  bool operator==(const NodeAddr<T> &NA) const {
    assert((Addr == NA.Addr) == (Id == NA.Id));
    return Addr == NA.Addr;
  }
  bool operator!=(const NodeAddr<T> &NA) const { //
    return !operator==(NA);
  }

  T Addr = nullptr;
  NodeId Id = 0;
};

struct NodeBase;

struct RefNode;
struct DefNode;
struct UseNode;
struct PhiUseNode;

struct CodeNode;
struct InstrNode;
struct PhiNode;
struct StmtNode;
struct BlockNode;
struct FuncNode;

// Use these short names with rdf:: qualification to avoid conflicts with
// preexisting names. Do not use 'using namespace rdf'.
using Node = NodeAddr<NodeBase *>;

using Ref = NodeAddr<RefNode *>;
using Def = NodeAddr<DefNode *>;
using Use = NodeAddr<UseNode *>; // This may conflict with llvm::Use.
using PhiUse = NodeAddr<PhiUseNode *>;

using Code = NodeAddr<CodeNode *>;
using Instr = NodeAddr<InstrNode *>;
using Phi = NodeAddr<PhiNode *>;
using Stmt = NodeAddr<StmtNode *>;
using Block = NodeAddr<BlockNode *>;
using Func = NodeAddr<FuncNode *>;

// Fast memory allocation and translation between node id and node address.
// This is really the same idea as the one underlying the "bump pointer
// allocator", the difference being in the translation. A node id is
// composed of two components: the index of the block in which it was
// allocated, and the index within the block. With the default settings,
// where the number of nodes per block is 4096, the node id (minus 1) is:
//
// bit position:                11             0
// +----------------------------+--------------+
// | Index of the block         |Index in block|
// +----------------------------+--------------+
//
// The actual node id is the above plus 1, to avoid creating a node id of 0.
//
// This method significantly improved the build time, compared to using maps
// (std::unordered_map or DenseMap) to translate between pointers and ids.
struct NodeAllocator {
  // Amount of storage for a single node.
  enum { NodeMemSize = 32 };

  NodeAllocator(uint32_t NPB = 4096)
      : NodesPerBlock(NPB), BitsPerIndex(Log2_32(NPB)),
        IndexMask((1 << BitsPerIndex) - 1) {
    assert(isPowerOf2_32(NPB));
  }

  NodeBase *ptr(NodeId N) const {
    uint32_t N1 = N - 1;
    uint32_t BlockN = N1 >> BitsPerIndex;
    uint32_t Offset = (N1 & IndexMask) * NodeMemSize;
    return reinterpret_cast<NodeBase *>(Blocks[BlockN] + Offset);
  }

  NodeId id(const NodeBase *P) const;
  Node New();
  void clear();

private:
  void startNewBlock();
  bool needNewBlock();

  uint32_t makeId(uint32_t Block, uint32_t Index) const {
    // Add 1 to the id, to avoid the id of 0, which is treated as "null".
    return ((Block << BitsPerIndex) | Index) + 1;
  }

  const uint32_t NodesPerBlock;
  const uint32_t BitsPerIndex;
  const uint32_t IndexMask;
  char *ActiveEnd = nullptr;
  std::vector<char *> Blocks;
  using AllocatorTy = BumpPtrAllocatorImpl<MallocAllocator, 65536>;
  AllocatorTy MemPool;
};

using RegisterSet = std::set<RegisterRef>;

struct TargetOperandInfo {
  TargetOperandInfo(const TargetInstrInfo &tii) : TII(tii) {}
  virtual ~TargetOperandInfo() = default;

  virtual bool isPreserving(const MachineInstr &In, unsigned OpNum) const;
  virtual bool isClobbering(const MachineInstr &In, unsigned OpNum) const;
  virtual bool isFixedReg(const MachineInstr &In, unsigned OpNum) const;

  const TargetInstrInfo &TII;
};

// Packed register reference. Only used for storage.
struct PackedRegisterRef {
  RegisterId Reg;
  uint32_t MaskId;
};

struct LaneMaskIndex : private IndexedSet<LaneBitmask> {
  LaneMaskIndex() = default;

  LaneBitmask getLaneMaskForIndex(uint32_t K) const {
    return K == 0 ? LaneBitmask::getAll() : get(K);
  }

  uint32_t getIndexForLaneMask(LaneBitmask LM) {
    assert(LM.any());
    return LM.all() ? 0 : insert(LM);
  }

  uint32_t getIndexForLaneMask(LaneBitmask LM) const {
    assert(LM.any());
    return LM.all() ? 0 : find(LM);
  }
};

struct NodeBase {
public:
  // Make sure this is a POD.
  NodeBase() = default;

  uint16_t getType() const { return NodeAttrs::type(Attrs); }
  uint16_t getKind() const { return NodeAttrs::kind(Attrs); }
  uint16_t getFlags() const { return NodeAttrs::flags(Attrs); }
  NodeId getNext() const { return Next; }

  uint16_t getAttrs() const { return Attrs; }
  void setAttrs(uint16_t A) { Attrs = A; }
  void setFlags(uint16_t F) { setAttrs(NodeAttrs::set_flags(getAttrs(), F)); }

  // Insert node NA after "this" in the circular chain.
  void append(Node NA);

  // Initialize all members to 0.
  void init() { memset(this, 0, sizeof *this); }

  void setNext(NodeId N) { Next = N; }

protected:
  uint16_t Attrs;
  uint16_t Reserved;
  NodeId Next; // Id of the next node in the circular chain.
  // Definitions of nested types. Using anonymous nested structs would make
  // this class definition clearer, but unnamed structs are not a part of
  // the standard.
  struct Def_struct {
    NodeId DD, DU; // Ids of the first reached def and use.
  };
  struct PhiU_struct {
    NodeId PredB; // Id of the predecessor block for a phi use.
  };
  struct Code_struct {
    void *CP;             // Pointer to the actual code.
    NodeId FirstM, LastM; // Id of the first member and last.
  };
  struct Ref_struct {
    NodeId RD, Sib; // Ids of the reaching def and the sibling.
    union {
      Def_struct Def;
      PhiU_struct PhiU;
    };
    union {
      MachineOperand *Op;   // Non-phi refs point to a machine operand.
      PackedRegisterRef PR; // Phi refs store register info directly.
    };
  };

  // The actual payload.
  union {
    Ref_struct RefData;
    Code_struct CodeData;
  };
};
// The allocator allocates chunks of 32 bytes for each node. The fact that
// each node takes 32 bytes in memory is used for fast translation between
// the node id and the node address.
static_assert(sizeof(NodeBase) <= NodeAllocator::NodeMemSize,
              "NodeBase must be at most NodeAllocator::NodeMemSize bytes");

using NodeList = SmallVector<Node, 4>;
using NodeSet = std::set<NodeId>;

struct RefNode : public NodeBase {
  RefNode() = default;

  RegisterRef getRegRef(const DataFlowGraph &G) const;

  MachineOperand &getOp() {
    assert(!(getFlags() & NodeAttrs::PhiRef));
    return *RefData.Op;
  }

  void setRegRef(RegisterRef RR, DataFlowGraph &G);
  void setRegRef(MachineOperand *Op, DataFlowGraph &G);

  NodeId getReachingDef() const { return RefData.RD; }
  void setReachingDef(NodeId RD) { RefData.RD = RD; }

  NodeId getSibling() const { return RefData.Sib; }
  void setSibling(NodeId Sib) { RefData.Sib = Sib; }

  bool isUse() const {
    assert(getType() == NodeAttrs::Ref);
    return getKind() == NodeAttrs::Use;
  }

  bool isDef() const {
    assert(getType() == NodeAttrs::Ref);
    return getKind() == NodeAttrs::Def;
  }

  template <typename Predicate>
  Ref getNextRef(RegisterRef RR, Predicate P, bool NextOnly,
                 const DataFlowGraph &G);
  Node getOwner(const DataFlowGraph &G);
};

struct DefNode : public RefNode {
  NodeId getReachedDef() const { return RefData.Def.DD; }
  void setReachedDef(NodeId D) { RefData.Def.DD = D; }
  NodeId getReachedUse() const { return RefData.Def.DU; }
  void setReachedUse(NodeId U) { RefData.Def.DU = U; }

  void linkToDef(NodeId Self, Def DA);
};

struct UseNode : public RefNode {
  void linkToDef(NodeId Self, Def DA);
};

struct PhiUseNode : public UseNode {
  NodeId getPredecessor() const {
    assert(getFlags() & NodeAttrs::PhiRef);
    return RefData.PhiU.PredB;
  }
  void setPredecessor(NodeId B) {
    assert(getFlags() & NodeAttrs::PhiRef);
    RefData.PhiU.PredB = B;
  }
};

struct CodeNode : public NodeBase {
  template <typename T> T getCode() const { //
    return static_cast<T>(CodeData.CP);
  }
  void setCode(void *C) { CodeData.CP = C; }

  Node getFirstMember(const DataFlowGraph &G) const;
  Node getLastMember(const DataFlowGraph &G) const;
  void addMember(Node NA, const DataFlowGraph &G);
  void addMemberAfter(Node MA, Node NA, const DataFlowGraph &G);
  void removeMember(Node NA, const DataFlowGraph &G);

  NodeList members(const DataFlowGraph &G) const;
  template <typename Predicate>
  NodeList members_if(Predicate P, const DataFlowGraph &G) const;
};

struct InstrNode : public CodeNode {
  Node getOwner(const DataFlowGraph &G);
};

struct PhiNode : public InstrNode {
  MachineInstr *getCode() const { return nullptr; }
};

struct StmtNode : public InstrNode {
  MachineInstr *getCode() const { //
    return CodeNode::getCode<MachineInstr *>();
  }
};

struct BlockNode : public CodeNode {
  MachineBasicBlock *getCode() const {
    return CodeNode::getCode<MachineBasicBlock *>();
  }

  void addPhi(Phi PA, const DataFlowGraph &G);
};

struct FuncNode : public CodeNode {
  MachineFunction *getCode() const {
    return CodeNode::getCode<MachineFunction *>();
  }

  Block findBlock(const MachineBasicBlock *BB, const DataFlowGraph &G) const;
  Block getEntryBlock(const DataFlowGraph &G);
};

struct DataFlowGraph {
  DataFlowGraph(MachineFunction &mf, const TargetInstrInfo &tii,
                const TargetRegisterInfo &tri, const MachineDominatorTree &mdt,
                const MachineDominanceFrontier &mdf);
  DataFlowGraph(MachineFunction &mf, const TargetInstrInfo &tii,
                const TargetRegisterInfo &tri, const MachineDominatorTree &mdt,
                const MachineDominanceFrontier &mdf,
                const TargetOperandInfo &toi);

  struct Config {
    Config() = default;
    Config(unsigned Opts) : Options(Opts) {}
    Config(ArrayRef<const TargetRegisterClass *> RCs) : Classes(RCs) {}
    Config(ArrayRef<MCPhysReg> Track) : TrackRegs(Track.begin(), Track.end()) {}
    Config(ArrayRef<RegisterId> Track)
        : TrackRegs(Track.begin(), Track.end()) {}

    unsigned Options = BuildOptions::None;
    SmallVector<const TargetRegisterClass *> Classes;
    std::set<RegisterId> TrackRegs;
  };

  NodeBase *ptr(NodeId N) const;
  template <typename T> T ptr(NodeId N) const { //
    return static_cast<T>(ptr(N));
  }

  NodeId id(const NodeBase *P) const;

  template <typename T> NodeAddr<T> addr(NodeId N) const {
    return {ptr<T>(N), N};
  }

  Func getFunc() const { return TheFunc; }
  MachineFunction &getMF() const { return MF; }
  const TargetInstrInfo &getTII() const { return TII; }
  const TargetRegisterInfo &getTRI() const { return TRI; }
  const PhysicalRegisterInfo &getPRI() const { return PRI; }
  const MachineDominatorTree &getDT() const { return MDT; }
  const MachineDominanceFrontier &getDF() const { return MDF; }
  const RegisterAggr &getLiveIns() const { return LiveIns; }

  struct DefStack {
    DefStack() = default;

    bool empty() const { return Stack.empty() || top() == bottom(); }

  private:
    using value_type = Def;
    struct Iterator {
      using value_type = DefStack::value_type;

      Iterator &up() {
        Pos = DS.nextUp(Pos);
        return *this;
      }
      Iterator &down() {
        Pos = DS.nextDown(Pos);
        return *this;
      }

      value_type operator*() const {
        assert(Pos >= 1);
        return DS.Stack[Pos - 1];
      }
      const value_type *operator->() const {
        assert(Pos >= 1);
        return &DS.Stack[Pos - 1];
      }
      bool operator==(const Iterator &It) const { return Pos == It.Pos; }
      bool operator!=(const Iterator &It) const { return Pos != It.Pos; }

    private:
      friend struct DefStack;

      Iterator(const DefStack &S, bool Top);

      // Pos-1 is the index in the StorageType object that corresponds to
      // the top of the DefStack.
      const DefStack &DS;
      unsigned Pos;
    };

  public:
    using iterator = Iterator;

    iterator top() const { return Iterator(*this, true); }
    iterator bottom() const { return Iterator(*this, false); }
    unsigned size() const;

    void push(Def DA) { Stack.push_back(DA); }
    void pop();
    void start_block(NodeId N);
    void clear_block(NodeId N);

  private:
    friend struct Iterator;

    using StorageType = std::vector<value_type>;

    bool isDelimiter(const StorageType::value_type &P, NodeId N = 0) const {
      return (P.Addr == nullptr) && (N == 0 || P.Id == N);
    }

    unsigned nextUp(unsigned P) const;
    unsigned nextDown(unsigned P) const;

    StorageType Stack;
  };

  // Make this std::unordered_map for speed of accessing elements.
  // Map: Register (physical or virtual) -> DefStack
  using DefStackMap = std::unordered_map<RegisterId, DefStack>;

  void build(const Config &config);
  void build() { build(Config()); }

  void pushAllDefs(Instr IA, DefStackMap &DM);
  void markBlock(NodeId B, DefStackMap &DefM);
  void releaseBlock(NodeId B, DefStackMap &DefM);

  PackedRegisterRef pack(RegisterRef RR) {
    return {RR.Reg, LMI.getIndexForLaneMask(RR.Mask)};
  }
  PackedRegisterRef pack(RegisterRef RR) const {
    return {RR.Reg, LMI.getIndexForLaneMask(RR.Mask)};
  }
  RegisterRef unpack(PackedRegisterRef PR) const {
    return RegisterRef(PR.Reg, LMI.getLaneMaskForIndex(PR.MaskId));
  }

  RegisterRef makeRegRef(unsigned Reg, unsigned Sub) const;
  RegisterRef makeRegRef(const MachineOperand &Op) const;

  Ref getNextRelated(Instr IA, Ref RA) const;
  Ref getNextShadow(Instr IA, Ref RA, bool Create);

  NodeList getRelatedRefs(Instr IA, Ref RA) const;

  Block findBlock(MachineBasicBlock *BB) const { return BlockNodes.at(BB); }

  void unlinkUse(Use UA, bool RemoveFromOwner) {
    unlinkUseDF(UA);
    if (RemoveFromOwner)
      removeFromOwner(UA);
  }

  void unlinkDef(Def DA, bool RemoveFromOwner) {
    unlinkDefDF(DA);
    if (RemoveFromOwner)
      removeFromOwner(DA);
  }

  bool isTracked(RegisterRef RR) const;
  bool hasUntrackedRef(Stmt S, bool IgnoreReserved = true) const;

  // Some useful filters.
  template <uint16_t Kind> static bool IsRef(const Node BA) {
    return BA.Addr->getType() == NodeAttrs::Ref && BA.Addr->getKind() == Kind;
  }

  template <uint16_t Kind> static bool IsCode(const Node BA) {
    return BA.Addr->getType() == NodeAttrs::Code && BA.Addr->getKind() == Kind;
  }

  static bool IsDef(const Node BA) {
    return BA.Addr->getType() == NodeAttrs::Ref &&
           BA.Addr->getKind() == NodeAttrs::Def;
  }

  static bool IsUse(const Node BA) {
    return BA.Addr->getType() == NodeAttrs::Ref &&
           BA.Addr->getKind() == NodeAttrs::Use;
  }

  static bool IsPhi(const Node BA) {
    return BA.Addr->getType() == NodeAttrs::Code &&
           BA.Addr->getKind() == NodeAttrs::Phi;
  }

  static bool IsPreservingDef(const Def DA) {
    uint16_t Flags = DA.Addr->getFlags();
    return (Flags & NodeAttrs::Preserving) && !(Flags & NodeAttrs::Undef);
  }

private:
  void reset();

  RegisterAggr getLandingPadLiveIns() const;

  Node newNode(uint16_t Attrs);
  Node cloneNode(const Node B);
  Use newUse(Instr Owner, MachineOperand &Op, uint16_t Flags = NodeAttrs::None);
  PhiUse newPhiUse(Phi Owner, RegisterRef RR, Block PredB,
                   uint16_t Flags = NodeAttrs::PhiRef);
  Def newDef(Instr Owner, MachineOperand &Op, uint16_t Flags = NodeAttrs::None);
  Def newDef(Instr Owner, RegisterRef RR, uint16_t Flags = NodeAttrs::PhiRef);
  Phi newPhi(Block Owner);
  Stmt newStmt(Block Owner, MachineInstr *MI);
  Block newBlock(Func Owner, MachineBasicBlock *BB);
  Func newFunc(MachineFunction *MF);

  template <typename Predicate>
  std::pair<Ref, Ref> locateNextRef(Instr IA, Ref RA, Predicate P) const;

  using BlockRefsMap = RegisterAggrMap<NodeId>;

  void buildStmt(Block BA, MachineInstr &In);
  void recordDefsForDF(BlockRefsMap &PhiM, Block BA);
  void buildPhis(BlockRefsMap &PhiM, Block BA);
  void removeUnusedPhis();

  void pushClobbers(Instr IA, DefStackMap &DM);
  void pushDefs(Instr IA, DefStackMap &DM);
  template <typename T> void linkRefUp(Instr IA, NodeAddr<T> TA, DefStack &DS);
  template <typename Predicate>
  void linkStmtRefs(DefStackMap &DefM, Stmt SA, Predicate P);
  void linkBlockRefs(DefStackMap &DefM, Block BA);

  void unlinkUseDF(Use UA);
  void unlinkDefDF(Def DA);

  void removeFromOwner(Ref RA) {
    Instr IA = RA.Addr->getOwner(*this);
    IA.Addr->removeMember(RA, *this);
  }

  // Default TOI object, if not given in the constructor.
  std::unique_ptr<TargetOperandInfo> DefaultTOI;

  MachineFunction &MF;
  const TargetInstrInfo &TII;
  const TargetRegisterInfo &TRI;
  const PhysicalRegisterInfo PRI;
  const MachineDominatorTree &MDT;
  const MachineDominanceFrontier &MDF;
  const TargetOperandInfo &TOI;

  RegisterAggr LiveIns;
  Func TheFunc;
  NodeAllocator Memory;
  // Local map:  MachineBasicBlock -> NodeAddr<BlockNode*>
  std::map<MachineBasicBlock *, Block> BlockNodes;
  // Lane mask map.
  LaneMaskIndex LMI;

  Config BuildCfg;
  std::set<unsigned> TrackedUnits;
  BitVector ReservedRegs;
}; // struct DataFlowGraph

template <typename Predicate>
Ref RefNode::getNextRef(RegisterRef RR, Predicate P, bool NextOnly,
                        const DataFlowGraph &G) {
  // Get the "Next" reference in the circular list that references RR and
  // satisfies predicate "Pred".
  auto NA = G.addr<NodeBase *>(getNext());

  while (NA.Addr != this) {
    if (NA.Addr->getType() == NodeAttrs::Ref) {
      Ref RA = NA;
      if (G.getPRI().equal_to(RA.Addr->getRegRef(G), RR) && P(NA))
        return NA;
      if (NextOnly)
        break;
      NA = G.addr<NodeBase *>(NA.Addr->getNext());
    } else {
      // We've hit the beginning of the chain.
      assert(NA.Addr->getType() == NodeAttrs::Code);
      // Make sure we stop here with NextOnly. Otherwise we can return the
      // wrong ref. Consider the following while creating/linking shadow uses:
      //   -> code -> sr1 -> sr2 -> [back to code]
      // Say that shadow refs sr1, and sr2 have been linked, but we need to
      // create and link another one. Starting from sr2, we'd hit the code
      // node and return sr1 if the iteration didn't stop here.
      if (NextOnly)
        break;
      Code CA = NA;
      NA = CA.Addr->getFirstMember(G);
    }
  }
  // Return the equivalent of "nullptr" if such a node was not found.
  return Ref();
}

template <typename Predicate>
NodeList CodeNode::members_if(Predicate P, const DataFlowGraph &G) const {
  NodeList MM;
  auto M = getFirstMember(G);
  if (M.Id == 0)
    return MM;

  while (M.Addr != this) {
    if (P(M))
      MM.push_back(M);
    M = G.addr<NodeBase *>(M.Addr->getNext());
  }
  return MM;
}

template <typename T> struct Print {
  Print(const T &x, const DataFlowGraph &g) : Obj(x), G(g) {}

  const T &Obj;
  const DataFlowGraph &G;
};

template <typename T> Print(const T &, const DataFlowGraph &) -> Print<T>;

template <typename T> struct PrintNode : Print<NodeAddr<T>> {
  PrintNode(const NodeAddr<T> &x, const DataFlowGraph &g)
      : Print<NodeAddr<T>>(x, g) {}
};

raw_ostream &operator<<(raw_ostream &OS, const Print<RegisterRef> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<NodeId> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<Def> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<Use> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<PhiUse> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<Ref> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<NodeList> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<NodeSet> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<Phi> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<Stmt> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<Instr> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<Block> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<Func> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<RegisterSet> &P);
raw_ostream &operator<<(raw_ostream &OS, const Print<RegisterAggr> &P);
raw_ostream &operator<<(raw_ostream &OS,
                        const Print<DataFlowGraph::DefStack> &P);

} // end namespace rdf
} // end namespace llvm

#endif // LLVM_CODEGEN_RDFGRAPH_H
