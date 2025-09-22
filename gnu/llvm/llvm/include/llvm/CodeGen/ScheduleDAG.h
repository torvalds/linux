//===- llvm/CodeGen/ScheduleDAG.h - Common Base Class -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Implements the ScheduleDAG class, which is used as the common base
/// class for instruction schedulers. This encapsulates the scheduling DAG,
/// which is shared between SelectionDAG and MachineInstr scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULEDAG_H
#define LLVM_CODEGEN_SCHEDULEDAG_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

namespace llvm {

template <class GraphType> struct GraphTraits;
template<class Graph> class GraphWriter;
class LLVMTargetMachine;
class MachineFunction;
class MachineRegisterInfo;
class MCInstrDesc;
struct MCSchedClassDesc;
class SDNode;
class SUnit;
class ScheduleDAG;
class TargetInstrInfo;
class TargetRegisterClass;
class TargetRegisterInfo;

  /// Scheduling dependency. This represents one direction of an edge in the
  /// scheduling DAG.
  class SDep {
  public:
    /// These are the different kinds of scheduling dependencies.
    enum Kind {
      Data,        ///< Regular data dependence (aka true-dependence).
      Anti,        ///< A register anti-dependence (aka WAR).
      Output,      ///< A register output-dependence (aka WAW).
      Order        ///< Any other ordering dependency.
    };

    // Strong dependencies must be respected by the scheduler. Artificial
    // dependencies may be removed only if they are redundant with another
    // strong dependence.
    //
    // Weak dependencies may be violated by the scheduling strategy, but only if
    // the strategy can prove it is correct to do so.
    //
    // Strong OrderKinds must occur before "Weak".
    // Weak OrderKinds must occur after "Weak".
    enum OrderKind {
      Barrier,      ///< An unknown scheduling barrier.
      MayAliasMem,  ///< Nonvolatile load/Store instructions that may alias.
      MustAliasMem, ///< Nonvolatile load/Store instructions that must alias.
      Artificial,   ///< Arbitrary strong DAG edge (no real dependence).
      Weak,         ///< Arbitrary weak DAG edge.
      Cluster       ///< Weak DAG edge linking a chain of clustered instrs.
    };

  private:
    /// A pointer to the depending/depended-on SUnit, and an enum
    /// indicating the kind of the dependency.
    PointerIntPair<SUnit *, 2, Kind> Dep;

    /// A union discriminated by the dependence kind.
    union {
      /// For Data, Anti, and Output dependencies, the associated register. For
      /// Data dependencies that don't currently have a register/ assigned, this
      /// is set to zero.
      unsigned Reg;

      /// Additional information about Order dependencies.
      unsigned OrdKind; // enum OrderKind
    } Contents;

    /// The time associated with this edge. Often this is just the value of the
    /// Latency field of the predecessor, however advanced models may provide
    /// additional information about specific edges.
    unsigned Latency = 0u;

  public:
    /// Constructs a null SDep. This is only for use by container classes which
    /// require default constructors. SUnits may not/ have null SDep edges.
    SDep() : Dep(nullptr, Data) {}

    /// Constructs an SDep with the specified values.
    SDep(SUnit *S, Kind kind, unsigned Reg)
      : Dep(S, kind), Contents() {
      switch (kind) {
      default:
        llvm_unreachable("Reg given for non-register dependence!");
      case Anti:
      case Output:
        assert(Reg != 0 &&
               "SDep::Anti and SDep::Output must use a non-zero Reg!");
        Contents.Reg = Reg;
        Latency = 0;
        break;
      case Data:
        Contents.Reg = Reg;
        Latency = 1;
        break;
      }
    }

    SDep(SUnit *S, OrderKind kind)
      : Dep(S, Order), Contents(), Latency(0) {
      Contents.OrdKind = kind;
    }

    /// Returns true if the specified SDep is equivalent except for latency.
    bool overlaps(const SDep &Other) const;

    bool operator==(const SDep &Other) const {
      return overlaps(Other) && Latency == Other.Latency;
    }

    bool operator!=(const SDep &Other) const {
      return !operator==(Other);
    }

    /// Returns the latency value for this edge, which roughly means the
    /// minimum number of cycles that must elapse between the predecessor and
    /// the successor, given that they have this edge between them.
    unsigned getLatency() const {
      return Latency;
    }

    /// Sets the latency for this edge.
    void setLatency(unsigned Lat) {
      Latency = Lat;
    }

    //// Returns the SUnit to which this edge points.
    SUnit *getSUnit() const;

    //// Assigns the SUnit to which this edge points.
    void setSUnit(SUnit *SU);

    /// Returns an enum value representing the kind of the dependence.
    Kind getKind() const;

    /// Shorthand for getKind() != SDep::Data.
    bool isCtrl() const {
      return getKind() != Data;
    }

    /// Tests if this is an Order dependence between two memory accesses
    /// where both sides of the dependence access memory in non-volatile and
    /// fully modeled ways.
    bool isNormalMemory() const {
      return getKind() == Order && (Contents.OrdKind == MayAliasMem
                                    || Contents.OrdKind == MustAliasMem);
    }

    /// Tests if this is an Order dependence that is marked as a barrier.
    bool isBarrier() const {
      return getKind() == Order && Contents.OrdKind == Barrier;
    }

    /// Tests if this is could be any kind of memory dependence.
    bool isNormalMemoryOrBarrier() const {
      return (isNormalMemory() || isBarrier());
    }

    /// Tests if this is an Order dependence that is marked as
    /// "must alias", meaning that the SUnits at either end of the edge have a
    /// memory dependence on a known memory location.
    bool isMustAlias() const {
      return getKind() == Order && Contents.OrdKind == MustAliasMem;
    }

    /// Tests if this a weak dependence. Weak dependencies are considered DAG
    /// edges for height computation and other heuristics, but do not force
    /// ordering. Breaking a weak edge may require the scheduler to compensate,
    /// for example by inserting a copy.
    bool isWeak() const {
      return getKind() == Order && Contents.OrdKind >= Weak;
    }

    /// Tests if this is an Order dependence that is marked as
    /// "artificial", meaning it isn't necessary for correctness.
    bool isArtificial() const {
      return getKind() == Order && Contents.OrdKind == Artificial;
    }

    /// Tests if this is an Order dependence that is marked as "cluster",
    /// meaning it is artificial and wants to be adjacent.
    bool isCluster() const {
      return getKind() == Order && Contents.OrdKind == Cluster;
    }

    /// Tests if this is a Data dependence that is associated with a register.
    bool isAssignedRegDep() const {
      return getKind() == Data && Contents.Reg != 0;
    }

    /// Returns the register associated with this edge. This is only valid on
    /// Data, Anti, and Output edges. On Data edges, this value may be zero,
    /// meaning there is no associated register.
    unsigned getReg() const {
      assert((getKind() == Data || getKind() == Anti || getKind() == Output) &&
             "getReg called on non-register dependence edge!");
      return Contents.Reg;
    }

    /// Assigns the associated register for this edge. This is only valid on
    /// Data, Anti, and Output edges. On Anti and Output edges, this value must
    /// not be zero. On Data edges, the value may be zero, which would mean that
    /// no specific register is associated with this edge.
    void setReg(unsigned Reg) {
      assert((getKind() == Data || getKind() == Anti || getKind() == Output) &&
             "setReg called on non-register dependence edge!");
      assert((getKind() != Anti || Reg != 0) &&
             "SDep::Anti edge cannot use the zero register!");
      assert((getKind() != Output || Reg != 0) &&
             "SDep::Output edge cannot use the zero register!");
      Contents.Reg = Reg;
    }

    void dump(const TargetRegisterInfo *TRI = nullptr) const;
  };

  /// Scheduling unit. This is a node in the scheduling DAG.
  class SUnit {
  private:
    enum : unsigned { BoundaryID = ~0u };

    union {
      SDNode *Node;        ///< Representative node.
      MachineInstr *Instr; ///< Alternatively, a MachineInstr.
    };

  public:
    SUnit *OrigNode = nullptr; ///< If not this, the node from which this node
                               /// was cloned. (SD scheduling only)

    const MCSchedClassDesc *SchedClass =
        nullptr; ///< nullptr or resolved SchedClass.

    const TargetRegisterClass *CopyDstRC =
        nullptr; ///< Is a special copy node if != nullptr.
    const TargetRegisterClass *CopySrcRC = nullptr;

    SmallVector<SDep, 4> Preds;  ///< All sunit predecessors.
    SmallVector<SDep, 4> Succs;  ///< All sunit successors.

    typedef SmallVectorImpl<SDep>::iterator pred_iterator;
    typedef SmallVectorImpl<SDep>::iterator succ_iterator;
    typedef SmallVectorImpl<SDep>::const_iterator const_pred_iterator;
    typedef SmallVectorImpl<SDep>::const_iterator const_succ_iterator;

    unsigned NodeNum = BoundaryID;     ///< Entry # of node in the node vector.
    unsigned NodeQueueId = 0;          ///< Queue id of node.
    unsigned NumPreds = 0;             ///< # of SDep::Data preds.
    unsigned NumSuccs = 0;             ///< # of SDep::Data sucss.
    unsigned NumPredsLeft = 0;         ///< # of preds not scheduled.
    unsigned NumSuccsLeft = 0;         ///< # of succs not scheduled.
    unsigned WeakPredsLeft = 0;        ///< # of weak preds not scheduled.
    unsigned WeakSuccsLeft = 0;        ///< # of weak succs not scheduled.
    unsigned TopReadyCycle = 0; ///< Cycle relative to start when node is ready.
    unsigned BotReadyCycle = 0; ///< Cycle relative to end when node is ready.

  private:
    unsigned Depth = 0;  ///< Node depth.
    unsigned Height = 0; ///< Node height.

  public:
    bool isVRegCycle      : 1;         ///< May use and def the same vreg.
    bool isCall           : 1;         ///< Is a function call.
    bool isCallOp         : 1;         ///< Is a function call operand.
    bool isTwoAddress     : 1;         ///< Is a two-address instruction.
    bool isCommutable     : 1;         ///< Is a commutable instruction.
    bool hasPhysRegUses   : 1;         ///< Has physreg uses.
    bool hasPhysRegDefs   : 1;         ///< Has physreg defs that are being used.
    bool hasPhysRegClobbers : 1;       ///< Has any physreg defs, used or not.
    bool isPending        : 1;         ///< True once pending.
    bool isAvailable      : 1;         ///< True once available.
    bool isScheduled      : 1;         ///< True once scheduled.
    bool isScheduleHigh   : 1;         ///< True if preferable to schedule high.
    bool isScheduleLow    : 1;         ///< True if preferable to schedule low.
    bool isCloned         : 1;         ///< True if this node has been cloned.
    bool isUnbuffered     : 1;         ///< Uses an unbuffered resource.
    bool hasReservedResource : 1;      ///< Uses a reserved resource.
    unsigned short NumRegDefsLeft = 0; ///< # of reg defs with no scheduled use.
    unsigned short Latency = 0;        ///< Node latency.

  private:
    bool isDepthCurrent   : 1;         ///< True if Depth is current.
    bool isHeightCurrent  : 1;         ///< True if Height is current.
    bool isNode : 1; ///< True if the representative is an SDNode
    bool isInst : 1; ///< True if the representative is a MachineInstr

  public:
    Sched::Preference SchedulingPref : 4; ///< Scheduling preference.
    static_assert(Sched::Preference::Last <= (1 << 4),
                  "not enough bits in bitfield");

    /// Constructs an SUnit for pre-regalloc scheduling to represent an
    /// SDNode and any nodes flagged to it.
    SUnit(SDNode *node, unsigned nodenum)
        : Node(node), NodeNum(nodenum), isVRegCycle(false), isCall(false),
          isCallOp(false), isTwoAddress(false), isCommutable(false),
          hasPhysRegUses(false), hasPhysRegDefs(false),
          hasPhysRegClobbers(false), isPending(false), isAvailable(false),
          isScheduled(false), isScheduleHigh(false), isScheduleLow(false),
          isCloned(false), isUnbuffered(false), hasReservedResource(false),
          isDepthCurrent(false), isHeightCurrent(false), isNode(true),
          isInst(false), SchedulingPref(Sched::None) {}

    /// Constructs an SUnit for post-regalloc scheduling to represent a
    /// MachineInstr.
    SUnit(MachineInstr *instr, unsigned nodenum)
        : Instr(instr), NodeNum(nodenum), isVRegCycle(false), isCall(false),
          isCallOp(false), isTwoAddress(false), isCommutable(false),
          hasPhysRegUses(false), hasPhysRegDefs(false),
          hasPhysRegClobbers(false), isPending(false), isAvailable(false),
          isScheduled(false), isScheduleHigh(false), isScheduleLow(false),
          isCloned(false), isUnbuffered(false), hasReservedResource(false),
          isDepthCurrent(false), isHeightCurrent(false), isNode(false),
          isInst(true), SchedulingPref(Sched::None) {}

    /// Constructs a placeholder SUnit.
    SUnit()
        : Node(nullptr), isVRegCycle(false), isCall(false), isCallOp(false),
          isTwoAddress(false), isCommutable(false), hasPhysRegUses(false),
          hasPhysRegDefs(false), hasPhysRegClobbers(false), isPending(false),
          isAvailable(false), isScheduled(false), isScheduleHigh(false),
          isScheduleLow(false), isCloned(false), isUnbuffered(false),
          hasReservedResource(false), isDepthCurrent(false),
          isHeightCurrent(false), isNode(false), isInst(false),
          SchedulingPref(Sched::None) {}

    /// Boundary nodes are placeholders for the boundary of the
    /// scheduling region.
    ///
    /// BoundaryNodes can have DAG edges, including Data edges, but they do not
    /// correspond to schedulable entities (e.g. instructions) and do not have a
    /// valid ID. Consequently, always check for boundary nodes before accessing
    /// an associative data structure keyed on node ID.
    bool isBoundaryNode() const { return NodeNum == BoundaryID; }

    /// Assigns the representative SDNode for this SUnit. This may be used
    /// during pre-regalloc scheduling.
    void setNode(SDNode *N) {
      assert(!isInst && "Setting SDNode of SUnit with MachineInstr!");
      Node = N;
      isNode = true;
    }

    /// Returns the representative SDNode for this SUnit. This may be used
    /// during pre-regalloc scheduling.
    SDNode *getNode() const {
      assert(!isInst && (isNode || !Instr) &&
             "Reading SDNode of SUnit without SDNode!");
      return Node;
    }

    /// Returns true if this SUnit refers to a machine instruction as
    /// opposed to an SDNode.
    bool isInstr() const { return isInst && Instr; }

    /// Assigns the instruction for the SUnit. This may be used during
    /// post-regalloc scheduling.
    void setInstr(MachineInstr *MI) {
      assert(!isNode && "Setting MachineInstr of SUnit with SDNode!");
      Instr = MI;
      isInst = true;
    }

    /// Returns the representative MachineInstr for this SUnit. This may be used
    /// during post-regalloc scheduling.
    MachineInstr *getInstr() const {
      assert(!isNode && (isInst || !Node) &&
             "Reading MachineInstr of SUnit without MachineInstr!");
      return Instr;
    }

    /// Adds the specified edge as a pred of the current node if not already.
    /// It also adds the current node as a successor of the specified node.
    bool addPred(const SDep &D, bool Required = true);

    /// Adds a barrier edge to SU by calling addPred(), with latency 0
    /// generally or latency 1 for a store followed by a load.
    bool addPredBarrier(SUnit *SU) {
      SDep Dep(SU, SDep::Barrier);
      unsigned TrueMemOrderLatency =
        ((SU->getInstr()->mayStore() && this->getInstr()->mayLoad()) ? 1 : 0);
      Dep.setLatency(TrueMemOrderLatency);
      return addPred(Dep);
    }

    /// Removes the specified edge as a pred of the current node if it exists.
    /// It also removes the current node as a successor of the specified node.
    void removePred(const SDep &D);

    /// Returns the depth of this node, which is the length of the maximum path
    /// up to any node which has no predecessors.
    unsigned getDepth() const {
      if (!isDepthCurrent)
        const_cast<SUnit *>(this)->ComputeDepth();
      return Depth;
    }

    /// Returns the height of this node, which is the length of the
    /// maximum path down to any node which has no successors.
    unsigned getHeight() const {
      if (!isHeightCurrent)
        const_cast<SUnit *>(this)->ComputeHeight();
      return Height;
    }

    /// If NewDepth is greater than this node's depth value, sets it to
    /// be the new depth value. This also recursively marks successor nodes
    /// dirty.
    void setDepthToAtLeast(unsigned NewDepth);

    /// If NewHeight is greater than this node's height value, set it to be
    /// the new height value. This also recursively marks predecessor nodes
    /// dirty.
    void setHeightToAtLeast(unsigned NewHeight);

    /// Sets a flag in this node to indicate that its stored Depth value
    /// will require recomputation the next time getDepth() is called.
    void setDepthDirty();

    /// Sets a flag in this node to indicate that its stored Height value
    /// will require recomputation the next time getHeight() is called.
    void setHeightDirty();

    /// Tests if node N is a predecessor of this node.
    bool isPred(const SUnit *N) const {
      for (const SDep &Pred : Preds)
        if (Pred.getSUnit() == N)
          return true;
      return false;
    }

    /// Tests if node N is a successor of this node.
    bool isSucc(const SUnit *N) const {
      for (const SDep &Succ : Succs)
        if (Succ.getSUnit() == N)
          return true;
      return false;
    }

    bool isTopReady() const {
      return NumPredsLeft == 0;
    }
    bool isBottomReady() const {
      return NumSuccsLeft == 0;
    }

    /// Orders this node's predecessor edges such that the critical path
    /// edge occurs first.
    void biasCriticalPath();

    void dumpAttributes() const;

  private:
    void ComputeDepth();
    void ComputeHeight();
  };

  /// Returns true if the specified SDep is equivalent except for latency.
  inline bool SDep::overlaps(const SDep &Other) const {
    if (Dep != Other.Dep)
      return false;
    switch (Dep.getInt()) {
    case Data:
    case Anti:
    case Output:
      return Contents.Reg == Other.Contents.Reg;
    case Order:
      return Contents.OrdKind == Other.Contents.OrdKind;
    }
    llvm_unreachable("Invalid dependency kind!");
  }

  //// Returns the SUnit to which this edge points.
  inline SUnit *SDep::getSUnit() const { return Dep.getPointer(); }

  //// Assigns the SUnit to which this edge points.
  inline void SDep::setSUnit(SUnit *SU) { Dep.setPointer(SU); }

  /// Returns an enum value representing the kind of the dependence.
  inline SDep::Kind SDep::getKind() const { return Dep.getInt(); }

  //===--------------------------------------------------------------------===//

  /// This interface is used to plug different priorities computation
  /// algorithms into the list scheduler. It implements the interface of a
  /// standard priority queue, where nodes are inserted in arbitrary order and
  /// returned in priority order.  The computation of the priority and the
  /// representation of the queue are totally up to the implementation to
  /// decide.
  class SchedulingPriorityQueue {
    virtual void anchor();

    unsigned CurCycle = 0;
    bool HasReadyFilter;

  public:
    SchedulingPriorityQueue(bool rf = false) :  HasReadyFilter(rf) {}

    virtual ~SchedulingPriorityQueue() = default;

    virtual bool isBottomUp() const = 0;

    virtual void initNodes(std::vector<SUnit> &SUnits) = 0;
    virtual void addNode(const SUnit *SU) = 0;
    virtual void updateNode(const SUnit *SU) = 0;
    virtual void releaseState() = 0;

    virtual bool empty() const = 0;

    bool hasReadyFilter() const { return HasReadyFilter; }

    virtual bool tracksRegPressure() const { return false; }

    virtual bool isReady(SUnit *) const {
      assert(!HasReadyFilter && "The ready filter must override isReady()");
      return true;
    }

    virtual void push(SUnit *U) = 0;

    void push_all(const std::vector<SUnit *> &Nodes) {
      for (SUnit *SU : Nodes)
        push(SU);
    }

    virtual SUnit *pop() = 0;

    virtual void remove(SUnit *SU) = 0;

    virtual void dump(ScheduleDAG *) const {}

    /// As each node is scheduled, this method is invoked.  This allows the
    /// priority function to adjust the priority of related unscheduled nodes,
    /// for example.
    virtual void scheduledNode(SUnit *) {}

    virtual void unscheduledNode(SUnit *) {}

    void setCurCycle(unsigned Cycle) {
      CurCycle = Cycle;
    }

    unsigned getCurCycle() const {
      return CurCycle;
    }
  };

  class ScheduleDAG {
  public:
    const LLVMTargetMachine &TM;        ///< Target processor
    const TargetInstrInfo *TII;         ///< Target instruction information
    const TargetRegisterInfo *TRI;      ///< Target processor register info
    MachineFunction &MF;                ///< Machine function
    MachineRegisterInfo &MRI;           ///< Virtual/real register map
    std::vector<SUnit> SUnits;          ///< The scheduling units.
    SUnit EntrySU;                      ///< Special node for the region entry.
    SUnit ExitSU;                       ///< Special node for the region exit.

#ifdef NDEBUG
    static const bool StressSched = false;
#else
    bool StressSched;
#endif

    // This class is designed to be passed by reference only. Copy constructor
    // is declared as deleted here to make the derived classes have deleted
    // implicit-declared copy constructor, which suppresses the warnings from
    // static analyzer when the derived classes own resources that are freed in
    // their destructors, but don't have user-written copy constructors (rule
    // of three).
    ScheduleDAG(const ScheduleDAG &) = delete;
    ScheduleDAG &operator=(const ScheduleDAG &) = delete;

    explicit ScheduleDAG(MachineFunction &mf);

    virtual ~ScheduleDAG();

    /// Clears the DAG state (between regions).
    void clearDAG();

    /// Returns the MCInstrDesc of this SUnit.
    /// Returns NULL for SDNodes without a machine opcode.
    const MCInstrDesc *getInstrDesc(const SUnit *SU) const {
      if (SU->isInstr()) return &SU->getInstr()->getDesc();
      return getNodeDesc(SU->getNode());
    }

    /// Pops up a GraphViz/gv window with the ScheduleDAG rendered using 'dot'.
    virtual void viewGraph(const Twine &Name, const Twine &Title);
    virtual void viewGraph();

    virtual void dumpNode(const SUnit &SU) const = 0;
    virtual void dump() const = 0;
    void dumpNodeName(const SUnit &SU) const;

    /// Returns a label for an SUnit node in a visualization of the ScheduleDAG.
    virtual std::string getGraphNodeLabel(const SUnit *SU) const = 0;

    /// Returns a label for the region of code covered by the DAG.
    virtual std::string getDAGName() const = 0;

    /// Adds custom features for a visualization of the ScheduleDAG.
    virtual void addCustomGraphFeatures(GraphWriter<ScheduleDAG*> &) const {}

#ifndef NDEBUG
    /// Verifies that all SUnits were scheduled and that their state is
    /// consistent. Returns the number of scheduled SUnits.
    unsigned VerifyScheduledDAG(bool isBottomUp);
#endif

  protected:
    void dumpNodeAll(const SUnit &SU) const;

  private:
    /// Returns the MCInstrDesc of this SDNode or NULL.
    const MCInstrDesc *getNodeDesc(const SDNode *Node) const;
  };

  class SUnitIterator {
    SUnit *Node;
    unsigned Operand;

    SUnitIterator(SUnit *N, unsigned Op) : Node(N), Operand(Op) {}

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = SUnit;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    bool operator==(const SUnitIterator& x) const {
      return Operand == x.Operand;
    }
    bool operator!=(const SUnitIterator& x) const { return !operator==(x); }

    pointer operator*() const {
      return Node->Preds[Operand].getSUnit();
    }
    pointer operator->() const { return operator*(); }

    SUnitIterator& operator++() {                // Preincrement
      ++Operand;
      return *this;
    }
    SUnitIterator operator++(int) { // Postincrement
      SUnitIterator tmp = *this; ++*this; return tmp;
    }

    static SUnitIterator begin(SUnit *N) { return SUnitIterator(N, 0); }
    static SUnitIterator end  (SUnit *N) {
      return SUnitIterator(N, (unsigned)N->Preds.size());
    }

    unsigned getOperand() const { return Operand; }
    const SUnit *getNode() const { return Node; }

    /// Tests if this is not an SDep::Data dependence.
    bool isCtrlDep() const {
      return getSDep().isCtrl();
    }
    bool isArtificialDep() const {
      return getSDep().isArtificial();
    }
    const SDep &getSDep() const {
      return Node->Preds[Operand];
    }
  };

  template <> struct GraphTraits<SUnit*> {
    typedef SUnit *NodeRef;
    typedef SUnitIterator ChildIteratorType;
    static NodeRef getEntryNode(SUnit *N) { return N; }
    static ChildIteratorType child_begin(NodeRef N) {
      return SUnitIterator::begin(N);
    }
    static ChildIteratorType child_end(NodeRef N) {
      return SUnitIterator::end(N);
    }
  };

  template <> struct GraphTraits<ScheduleDAG*> : public GraphTraits<SUnit*> {
    typedef pointer_iterator<std::vector<SUnit>::iterator> nodes_iterator;
    static nodes_iterator nodes_begin(ScheduleDAG *G) {
      return nodes_iterator(G->SUnits.begin());
    }
    static nodes_iterator nodes_end(ScheduleDAG *G) {
      return nodes_iterator(G->SUnits.end());
    }
  };

  /// This class can compute a topological ordering for SUnits and provides
  /// methods for dynamically updating the ordering as new edges are added.
  ///
  /// This allows a very fast implementation of IsReachable, for example.
  class ScheduleDAGTopologicalSort {
    /// A reference to the ScheduleDAG's SUnits.
    std::vector<SUnit> &SUnits;
    SUnit *ExitSU;

    // Have any new nodes been added?
    bool Dirty = false;

    // Outstanding added edges, that have not been applied to the ordering.
    SmallVector<std::pair<SUnit *, SUnit *>, 16> Updates;

    /// Maps topological index to the node number.
    std::vector<int> Index2Node;
    /// Maps the node number to its topological index.
    std::vector<int> Node2Index;
    /// a set of nodes visited during a DFS traversal.
    BitVector Visited;

    /// Makes a DFS traversal and mark all nodes affected by the edge insertion.
    /// These nodes will later get new topological indexes by means of the Shift
    /// method.
    void DFS(const SUnit *SU, int UpperBound, bool& HasLoop);

    /// Reassigns topological indexes for the nodes in the DAG to
    /// preserve the topological ordering.
    void Shift(BitVector& Visited, int LowerBound, int UpperBound);

    /// Assigns the topological index to the node n.
    void Allocate(int n, int index);

    /// Fix the ordering, by either recomputing from scratch or by applying
    /// any outstanding updates. Uses a heuristic to estimate what will be
    /// cheaper.
    void FixOrder();

  public:
    ScheduleDAGTopologicalSort(std::vector<SUnit> &SUnits, SUnit *ExitSU);

    /// Add a SUnit without predecessors to the end of the topological order. It
    /// also must be the first new node added to the DAG.
    void AddSUnitWithoutPredecessors(const SUnit *SU);

    /// Creates the initial topological ordering from the DAG to be scheduled.
    void InitDAGTopologicalSorting();

    /// Returns an array of SUs that are both in the successor
    /// subtree of StartSU and in the predecessor subtree of TargetSU.
    /// StartSU and TargetSU are not in the array.
    /// Success is false if TargetSU is not in the successor subtree of
    /// StartSU, else it is true.
    std::vector<int> GetSubGraph(const SUnit &StartSU, const SUnit &TargetSU,
                                 bool &Success);

    /// Checks if \p SU is reachable from \p TargetSU.
    bool IsReachable(const SUnit *SU, const SUnit *TargetSU);

    /// Returns true if addPred(TargetSU, SU) creates a cycle.
    bool WillCreateCycle(SUnit *TargetSU, SUnit *SU);

    /// Updates the topological ordering to accommodate an edge to be
    /// added from SUnit \p X to SUnit \p Y.
    void AddPred(SUnit *Y, SUnit *X);

    /// Queues an update to the topological ordering to accommodate an edge to
    /// be added from SUnit \p X to SUnit \p Y.
    void AddPredQueued(SUnit *Y, SUnit *X);

    /// Updates the topological ordering to accommodate an edge to be
    /// removed from the specified node \p N from the predecessors of the
    /// current node \p M.
    void RemovePred(SUnit *M, SUnit *N);

    /// Mark the ordering as temporarily broken, after a new node has been
    /// added.
    void MarkDirty() { Dirty = true; }

    typedef std::vector<int>::iterator iterator;
    typedef std::vector<int>::const_iterator const_iterator;
    iterator begin() { return Index2Node.begin(); }
    const_iterator begin() const { return Index2Node.begin(); }
    iterator end() { return Index2Node.end(); }
    const_iterator end() const { return Index2Node.end(); }

    typedef std::vector<int>::reverse_iterator reverse_iterator;
    typedef std::vector<int>::const_reverse_iterator const_reverse_iterator;
    reverse_iterator rbegin() { return Index2Node.rbegin(); }
    const_reverse_iterator rbegin() const { return Index2Node.rbegin(); }
    reverse_iterator rend() { return Index2Node.rend(); }
    const_reverse_iterator rend() const { return Index2Node.rend(); }
  };

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULEDAG_H
