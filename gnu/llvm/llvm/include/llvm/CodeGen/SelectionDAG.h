//===- llvm/CodeGen/SelectionDAG.h - InstSelection DAG ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SelectionDAG class, and transitively defines the
// SDNode class and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAG_H
#define LLVM_CODEGEN_SELECTIONDAG_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/DAGCombine.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ArrayRecycler.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/RecyclingAllocator.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace llvm {

class DIExpression;
class DILabel;
class DIVariable;
class Function;
class Pass;
class Type;
template <class GraphType> struct GraphTraits;
template <typename T, unsigned int N> class SmallSetVector;
template <typename T, typename Enable> struct FoldingSetTrait;
class AAResults;
class BlockAddress;
class BlockFrequencyInfo;
class Constant;
class ConstantFP;
class ConstantInt;
class DataLayout;
struct fltSemantics;
class FunctionLoweringInfo;
class FunctionVarLocs;
class GlobalValue;
struct KnownBits;
class LLVMContext;
class MachineBasicBlock;
class MachineConstantPoolValue;
class MCSymbol;
class OptimizationRemarkEmitter;
class ProfileSummaryInfo;
class SDDbgValue;
class SDDbgOperand;
class SDDbgLabel;
class SelectionDAG;
class SelectionDAGTargetInfo;
class TargetLibraryInfo;
class TargetLowering;
class TargetMachine;
class TargetSubtargetInfo;
class Value;

template <typename T> class GenericSSAContext;
using SSAContext = GenericSSAContext<Function>;
template <typename T> class GenericUniformityInfo;
using UniformityInfo = GenericUniformityInfo<SSAContext>;

class SDVTListNode : public FoldingSetNode {
  friend struct FoldingSetTrait<SDVTListNode>;

  /// A reference to an Interned FoldingSetNodeID for this node.
  /// The Allocator in SelectionDAG holds the data.
  /// SDVTList contains all types which are frequently accessed in SelectionDAG.
  /// The size of this list is not expected to be big so it won't introduce
  /// a memory penalty.
  FoldingSetNodeIDRef FastID;
  const EVT *VTs;
  unsigned int NumVTs;
  /// The hash value for SDVTList is fixed, so cache it to avoid
  /// hash calculation.
  unsigned HashValue;

public:
  SDVTListNode(const FoldingSetNodeIDRef ID, const EVT *VT, unsigned int Num) :
      FastID(ID), VTs(VT), NumVTs(Num) {
    HashValue = ID.ComputeHash();
  }

  SDVTList getSDVTList() {
    SDVTList result = {VTs, NumVTs};
    return result;
  }
};

/// Specialize FoldingSetTrait for SDVTListNode
/// to avoid computing temp FoldingSetNodeID and hash value.
template<> struct FoldingSetTrait<SDVTListNode> : DefaultFoldingSetTrait<SDVTListNode> {
  static void Profile(const SDVTListNode &X, FoldingSetNodeID& ID) {
    ID = X.FastID;
  }

  static bool Equals(const SDVTListNode &X, const FoldingSetNodeID &ID,
                     unsigned IDHash, FoldingSetNodeID &TempID) {
    if (X.HashValue != IDHash)
      return false;
    return ID == X.FastID;
  }

  static unsigned ComputeHash(const SDVTListNode &X, FoldingSetNodeID &TempID) {
    return X.HashValue;
  }
};

template <> struct ilist_alloc_traits<SDNode> {
  static void deleteNode(SDNode *) {
    llvm_unreachable("ilist_traits<SDNode> shouldn't see a deleteNode call!");
  }
};

/// Keeps track of dbg_value information through SDISel.  We do
/// not build SDNodes for these so as not to perturb the generated code;
/// instead the info is kept off to the side in this structure. Each SDNode may
/// have one or more associated dbg_value entries. This information is kept in
/// DbgValMap.
/// Byval parameters are handled separately because they don't use alloca's,
/// which busts the normal mechanism.  There is good reason for handling all
/// parameters separately:  they may not have code generated for them, they
/// should always go at the beginning of the function regardless of other code
/// motion, and debug info for them is potentially useful even if the parameter
/// is unused.  Right now only byval parameters are handled separately.
class SDDbgInfo {
  BumpPtrAllocator Alloc;
  SmallVector<SDDbgValue*, 32> DbgValues;
  SmallVector<SDDbgValue*, 32> ByvalParmDbgValues;
  SmallVector<SDDbgLabel*, 4> DbgLabels;
  using DbgValMapType = DenseMap<const SDNode *, SmallVector<SDDbgValue *, 2>>;
  DbgValMapType DbgValMap;

public:
  SDDbgInfo() = default;
  SDDbgInfo(const SDDbgInfo &) = delete;
  SDDbgInfo &operator=(const SDDbgInfo &) = delete;

  void add(SDDbgValue *V, bool isParameter);

  void add(SDDbgLabel *L) { DbgLabels.push_back(L); }

  /// Invalidate all DbgValues attached to the node and remove
  /// it from the Node-to-DbgValues map.
  void erase(const SDNode *Node);

  void clear() {
    DbgValMap.clear();
    DbgValues.clear();
    ByvalParmDbgValues.clear();
    DbgLabels.clear();
    Alloc.Reset();
  }

  BumpPtrAllocator &getAlloc() { return Alloc; }

  bool empty() const {
    return DbgValues.empty() && ByvalParmDbgValues.empty() && DbgLabels.empty();
  }

  ArrayRef<SDDbgValue*> getSDDbgValues(const SDNode *Node) const {
    auto I = DbgValMap.find(Node);
    if (I != DbgValMap.end())
      return I->second;
    return ArrayRef<SDDbgValue*>();
  }

  using DbgIterator = SmallVectorImpl<SDDbgValue*>::iterator;
  using DbgLabelIterator = SmallVectorImpl<SDDbgLabel*>::iterator;

  DbgIterator DbgBegin() { return DbgValues.begin(); }
  DbgIterator DbgEnd()   { return DbgValues.end(); }
  DbgIterator ByvalParmDbgBegin() { return ByvalParmDbgValues.begin(); }
  DbgIterator ByvalParmDbgEnd()   { return ByvalParmDbgValues.end(); }
  DbgLabelIterator DbgLabelBegin() { return DbgLabels.begin(); }
  DbgLabelIterator DbgLabelEnd()   { return DbgLabels.end(); }
};

void checkForCycles(const SelectionDAG *DAG, bool force = false);

/// This is used to represent a portion of an LLVM function in a low-level
/// Data Dependence DAG representation suitable for instruction selection.
/// This DAG is constructed as the first step of instruction selection in order
/// to allow implementation of machine specific optimizations
/// and code simplifications.
///
/// The representation used by the SelectionDAG is a target-independent
/// representation, which has some similarities to the GCC RTL representation,
/// but is significantly more simple, powerful, and is a graph form instead of a
/// linear form.
///
class SelectionDAG {
  const TargetMachine &TM;
  const SelectionDAGTargetInfo *TSI = nullptr;
  const TargetLowering *TLI = nullptr;
  const TargetLibraryInfo *LibInfo = nullptr;
  const FunctionVarLocs *FnVarLocs = nullptr;
  MachineFunction *MF;
  MachineFunctionAnalysisManager *MFAM = nullptr;
  Pass *SDAGISelPass = nullptr;
  LLVMContext *Context;
  CodeGenOptLevel OptLevel;

  UniformityInfo *UA = nullptr;
  FunctionLoweringInfo * FLI = nullptr;

  /// The function-level optimization remark emitter.  Used to emit remarks
  /// whenever manipulating the DAG.
  OptimizationRemarkEmitter *ORE;

  ProfileSummaryInfo *PSI = nullptr;
  BlockFrequencyInfo *BFI = nullptr;

  /// List of non-single value types.
  FoldingSet<SDVTListNode> VTListMap;

  /// Pool allocation for misc. objects that are created once per SelectionDAG.
  BumpPtrAllocator Allocator;

  /// The starting token.
  SDNode EntryNode;

  /// The root of the entire DAG.
  SDValue Root;

  /// A linked list of nodes in the current DAG.
  ilist<SDNode> AllNodes;

  /// The AllocatorType for allocating SDNodes. We use
  /// pool allocation with recycling.
  using NodeAllocatorType = RecyclingAllocator<BumpPtrAllocator, SDNode,
                                               sizeof(LargestSDNode),
                                               alignof(MostAlignedSDNode)>;

  /// Pool allocation for nodes.
  NodeAllocatorType NodeAllocator;

  /// This structure is used to memoize nodes, automatically performing
  /// CSE with existing nodes when a duplicate is requested.
  FoldingSet<SDNode> CSEMap;

  /// Pool allocation for machine-opcode SDNode operands.
  BumpPtrAllocator OperandAllocator;
  ArrayRecycler<SDUse> OperandRecycler;

  /// Tracks dbg_value and dbg_label information through SDISel.
  SDDbgInfo *DbgInfo;

  using CallSiteInfo = MachineFunction::CallSiteInfo;

  struct NodeExtraInfo {
    CallSiteInfo CSInfo;
    MDNode *HeapAllocSite = nullptr;
    MDNode *PCSections = nullptr;
    MDNode *MMRA = nullptr;
    bool NoMerge = false;
  };
  /// Out-of-line extra information for SDNodes.
  DenseMap<const SDNode *, NodeExtraInfo> SDEI;

  /// PersistentId counter to be used when inserting the next
  /// SDNode to this SelectionDAG. We do not place that under
  /// `#if LLVM_ENABLE_ABI_BREAKING_CHECKS` intentionally because
  /// it adds unneeded complexity without noticeable
  /// benefits (see discussion with @thakis in D120714).
  uint16_t NextPersistentId = 0;

public:
  /// Clients of various APIs that cause global effects on
  /// the DAG can optionally implement this interface.  This allows the clients
  /// to handle the various sorts of updates that happen.
  ///
  /// A DAGUpdateListener automatically registers itself with DAG when it is
  /// constructed, and removes itself when destroyed in RAII fashion.
  struct DAGUpdateListener {
    DAGUpdateListener *const Next;
    SelectionDAG &DAG;

    explicit DAGUpdateListener(SelectionDAG &D)
      : Next(D.UpdateListeners), DAG(D) {
      DAG.UpdateListeners = this;
    }

    virtual ~DAGUpdateListener() {
      assert(DAG.UpdateListeners == this &&
             "DAGUpdateListeners must be destroyed in LIFO order");
      DAG.UpdateListeners = Next;
    }

    /// The node N that was deleted and, if E is not null, an
    /// equivalent node E that replaced it.
    virtual void NodeDeleted(SDNode *N, SDNode *E);

    /// The node N that was updated.
    virtual void NodeUpdated(SDNode *N);

    /// The node N that was inserted.
    virtual void NodeInserted(SDNode *N);
  };

  struct DAGNodeDeletedListener : public DAGUpdateListener {
    std::function<void(SDNode *, SDNode *)> Callback;

    DAGNodeDeletedListener(SelectionDAG &DAG,
                           std::function<void(SDNode *, SDNode *)> Callback)
        : DAGUpdateListener(DAG), Callback(std::move(Callback)) {}

    void NodeDeleted(SDNode *N, SDNode *E) override { Callback(N, E); }

   private:
    virtual void anchor();
  };

  struct DAGNodeInsertedListener : public DAGUpdateListener {
    std::function<void(SDNode *)> Callback;

    DAGNodeInsertedListener(SelectionDAG &DAG,
                            std::function<void(SDNode *)> Callback)
        : DAGUpdateListener(DAG), Callback(std::move(Callback)) {}

    void NodeInserted(SDNode *N) override { Callback(N); }

  private:
    virtual void anchor();
  };

  /// Help to insert SDNodeFlags automatically in transforming. Use
  /// RAII to save and resume flags in current scope.
  class FlagInserter {
    SelectionDAG &DAG;
    SDNodeFlags Flags;
    FlagInserter *LastInserter;

  public:
    FlagInserter(SelectionDAG &SDAG, SDNodeFlags Flags)
        : DAG(SDAG), Flags(Flags),
          LastInserter(SDAG.getFlagInserter()) {
      SDAG.setFlagInserter(this);
    }
    FlagInserter(SelectionDAG &SDAG, SDNode *N)
        : FlagInserter(SDAG, N->getFlags()) {}

    FlagInserter(const FlagInserter &) = delete;
    FlagInserter &operator=(const FlagInserter &) = delete;
    ~FlagInserter() { DAG.setFlagInserter(LastInserter); }

    SDNodeFlags getFlags() const { return Flags; }
  };

  /// When true, additional steps are taken to
  /// ensure that getConstant() and similar functions return DAG nodes that
  /// have legal types. This is important after type legalization since
  /// any illegally typed nodes generated after this point will not experience
  /// type legalization.
  bool NewNodesMustHaveLegalTypes = false;

private:
  /// DAGUpdateListener is a friend so it can manipulate the listener stack.
  friend struct DAGUpdateListener;

  /// Linked list of registered DAGUpdateListener instances.
  /// This stack is maintained by DAGUpdateListener RAII.
  DAGUpdateListener *UpdateListeners = nullptr;

  /// Implementation of setSubgraphColor.
  /// Return whether we had to truncate the search.
  bool setSubgraphColorHelper(SDNode *N, const char *Color,
                              DenseSet<SDNode *> &visited,
                              int level, bool &printed);

  template <typename SDNodeT, typename... ArgTypes>
  SDNodeT *newSDNode(ArgTypes &&... Args) {
    return new (NodeAllocator.template Allocate<SDNodeT>())
        SDNodeT(std::forward<ArgTypes>(Args)...);
  }

  /// Build a synthetic SDNodeT with the given args and extract its subclass
  /// data as an integer (e.g. for use in a folding set).
  ///
  /// The args to this function are the same as the args to SDNodeT's
  /// constructor, except the second arg (assumed to be a const DebugLoc&) is
  /// omitted.
  template <typename SDNodeT, typename... ArgTypes>
  static uint16_t getSyntheticNodeSubclassData(unsigned IROrder,
                                               ArgTypes &&... Args) {
    // The compiler can reduce this expression to a constant iff we pass an
    // empty DebugLoc.  Thankfully, the debug location doesn't have any bearing
    // on the subclass data.
    return SDNodeT(IROrder, DebugLoc(), std::forward<ArgTypes>(Args)...)
        .getRawSubclassData();
  }

  template <typename SDNodeTy>
  static uint16_t getSyntheticNodeSubclassData(unsigned Opc, unsigned Order,
                                                SDVTList VTs, EVT MemoryVT,
                                                MachineMemOperand *MMO) {
    return SDNodeTy(Opc, Order, DebugLoc(), VTs, MemoryVT, MMO)
         .getRawSubclassData();
  }

  void createOperands(SDNode *Node, ArrayRef<SDValue> Vals);

  void removeOperands(SDNode *Node) {
    if (!Node->OperandList)
      return;
    OperandRecycler.deallocate(
        ArrayRecycler<SDUse>::Capacity::get(Node->NumOperands),
        Node->OperandList);
    Node->NumOperands = 0;
    Node->OperandList = nullptr;
  }
  void CreateTopologicalOrder(std::vector<SDNode*>& Order);

public:
  // Maximum depth for recursive analysis such as computeKnownBits, etc.
  static constexpr unsigned MaxRecursionDepth = 6;

  explicit SelectionDAG(const TargetMachine &TM, CodeGenOptLevel);
  SelectionDAG(const SelectionDAG &) = delete;
  SelectionDAG &operator=(const SelectionDAG &) = delete;
  ~SelectionDAG();

  /// Prepare this SelectionDAG to process code in the given MachineFunction.
  void init(MachineFunction &NewMF, OptimizationRemarkEmitter &NewORE,
            Pass *PassPtr, const TargetLibraryInfo *LibraryInfo,
            UniformityInfo *UA, ProfileSummaryInfo *PSIin,
            BlockFrequencyInfo *BFIin, FunctionVarLocs const *FnVarLocs);

  void init(MachineFunction &NewMF, OptimizationRemarkEmitter &NewORE,
            MachineFunctionAnalysisManager &AM,
            const TargetLibraryInfo *LibraryInfo, UniformityInfo *UA,
            ProfileSummaryInfo *PSIin, BlockFrequencyInfo *BFIin,
            FunctionVarLocs const *FnVarLocs) {
    init(NewMF, NewORE, nullptr, LibraryInfo, UA, PSIin, BFIin, FnVarLocs);
    MFAM = &AM;
  }

  void setFunctionLoweringInfo(FunctionLoweringInfo * FuncInfo) {
    FLI = FuncInfo;
  }

  /// Clear state and free memory necessary to make this
  /// SelectionDAG ready to process a new block.
  void clear();

  MachineFunction &getMachineFunction() const { return *MF; }
  const Pass *getPass() const { return SDAGISelPass; }
  MachineFunctionAnalysisManager *getMFAM() { return MFAM; }

  CodeGenOptLevel getOptLevel() const { return OptLevel; }
  const DataLayout &getDataLayout() const { return MF->getDataLayout(); }
  const TargetMachine &getTarget() const { return TM; }
  const TargetSubtargetInfo &getSubtarget() const { return MF->getSubtarget(); }
  template <typename STC> const STC &getSubtarget() const {
    return MF->getSubtarget<STC>();
  }
  const TargetLowering &getTargetLoweringInfo() const { return *TLI; }
  const TargetLibraryInfo &getLibInfo() const { return *LibInfo; }
  const SelectionDAGTargetInfo &getSelectionDAGInfo() const { return *TSI; }
  const UniformityInfo *getUniformityInfo() const { return UA; }
  /// Returns the result of the AssignmentTrackingAnalysis pass if it's
  /// available, otherwise return nullptr.
  const FunctionVarLocs *getFunctionVarLocs() const { return FnVarLocs; }
  LLVMContext *getContext() const { return Context; }
  OptimizationRemarkEmitter &getORE() const { return *ORE; }
  ProfileSummaryInfo *getPSI() const { return PSI; }
  BlockFrequencyInfo *getBFI() const { return BFI; }

  FlagInserter *getFlagInserter() { return Inserter; }
  void setFlagInserter(FlagInserter *FI) { Inserter = FI; }

  /// Just dump dot graph to a user-provided path and title.
  /// This doesn't open the dot viewer program and
  /// helps visualization when outside debugging session.
  /// FileName expects absolute path. If provided
  /// without any path separators then the file
  /// will be created in the current directory.
  /// Error will be emitted if the path is insane.
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dumpDotGraph(const Twine &FileName, const Twine &Title);
#endif

  /// Pop up a GraphViz/gv window with the DAG rendered using 'dot'.
  void viewGraph(const std::string &Title);
  void viewGraph();

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  std::map<const SDNode *, std::string> NodeGraphAttrs;
#endif

  /// Clear all previously defined node graph attributes.
  /// Intended to be used from a debugging tool (eg. gdb).
  void clearGraphAttrs();

  /// Set graph attributes for a node. (eg. "color=red".)
  void setGraphAttrs(const SDNode *N, const char *Attrs);

  /// Get graph attributes for a node. (eg. "color=red".)
  /// Used from getNodeAttributes.
  std::string getGraphAttrs(const SDNode *N) const;

  /// Convenience for setting node color attribute.
  void setGraphColor(const SDNode *N, const char *Color);

  /// Convenience for setting subgraph color attribute.
  void setSubgraphColor(SDNode *N, const char *Color);

  using allnodes_const_iterator = ilist<SDNode>::const_iterator;

  allnodes_const_iterator allnodes_begin() const { return AllNodes.begin(); }
  allnodes_const_iterator allnodes_end() const { return AllNodes.end(); }

  using allnodes_iterator = ilist<SDNode>::iterator;

  allnodes_iterator allnodes_begin() { return AllNodes.begin(); }
  allnodes_iterator allnodes_end() { return AllNodes.end(); }

  ilist<SDNode>::size_type allnodes_size() const {
    return AllNodes.size();
  }

  iterator_range<allnodes_iterator> allnodes() {
    return make_range(allnodes_begin(), allnodes_end());
  }
  iterator_range<allnodes_const_iterator> allnodes() const {
    return make_range(allnodes_begin(), allnodes_end());
  }

  /// Return the root tag of the SelectionDAG.
  const SDValue &getRoot() const { return Root; }

  /// Return the token chain corresponding to the entry of the function.
  SDValue getEntryNode() const {
    return SDValue(const_cast<SDNode *>(&EntryNode), 0);
  }

  /// Set the current root tag of the SelectionDAG.
  ///
  const SDValue &setRoot(SDValue N) {
    assert((!N.getNode() || N.getValueType() == MVT::Other) &&
           "DAG root value is not a chain!");
    if (N.getNode())
      checkForCycles(N.getNode(), this);
    Root = N;
    if (N.getNode())
      checkForCycles(this);
    return Root;
  }

#ifndef NDEBUG
  void VerifyDAGDivergence();
#endif

  /// This iterates over the nodes in the SelectionDAG, folding
  /// certain types of nodes together, or eliminating superfluous nodes.  The
  /// Level argument controls whether Combine is allowed to produce nodes and
  /// types that are illegal on the target.
  void Combine(CombineLevel Level, AAResults *AA, CodeGenOptLevel OptLevel);

  /// This transforms the SelectionDAG into a SelectionDAG that
  /// only uses types natively supported by the target.
  /// Returns "true" if it made any changes.
  ///
  /// Note that this is an involved process that may invalidate pointers into
  /// the graph.
  bool LegalizeTypes();

  /// This transforms the SelectionDAG into a SelectionDAG that is
  /// compatible with the target instruction selector, as indicated by the
  /// TargetLowering object.
  ///
  /// Note that this is an involved process that may invalidate pointers into
  /// the graph.
  void Legalize();

  /// Transforms a SelectionDAG node and any operands to it into a node
  /// that is compatible with the target instruction selector, as indicated by
  /// the TargetLowering object.
  ///
  /// \returns true if \c N is a valid, legal node after calling this.
  ///
  /// This essentially runs a single recursive walk of the \c Legalize process
  /// over the given node (and its operands). This can be used to incrementally
  /// legalize the DAG. All of the nodes which are directly replaced,
  /// potentially including N, are added to the output parameter \c
  /// UpdatedNodes so that the delta to the DAG can be understood by the
  /// caller.
  ///
  /// When this returns false, N has been legalized in a way that make the
  /// pointer passed in no longer valid. It may have even been deleted from the
  /// DAG, and so it shouldn't be used further. When this returns true, the
  /// N passed in is a legal node, and can be immediately processed as such.
  /// This may still have done some work on the DAG, and will still populate
  /// UpdatedNodes with any new nodes replacing those originally in the DAG.
  bool LegalizeOp(SDNode *N, SmallSetVector<SDNode *, 16> &UpdatedNodes);

  /// This transforms the SelectionDAG into a SelectionDAG
  /// that only uses vector math operations supported by the target.  This is
  /// necessary as a separate step from Legalize because unrolling a vector
  /// operation can introduce illegal types, which requires running
  /// LegalizeTypes again.
  ///
  /// This returns true if it made any changes; in that case, LegalizeTypes
  /// is called again before Legalize.
  ///
  /// Note that this is an involved process that may invalidate pointers into
  /// the graph.
  bool LegalizeVectors();

  /// This method deletes all unreachable nodes in the SelectionDAG.
  void RemoveDeadNodes();

  /// Remove the specified node from the system.  This node must
  /// have no referrers.
  void DeleteNode(SDNode *N);

  /// Return an SDVTList that represents the list of values specified.
  SDVTList getVTList(EVT VT);
  SDVTList getVTList(EVT VT1, EVT VT2);
  SDVTList getVTList(EVT VT1, EVT VT2, EVT VT3);
  SDVTList getVTList(EVT VT1, EVT VT2, EVT VT3, EVT VT4);
  SDVTList getVTList(ArrayRef<EVT> VTs);

  //===--------------------------------------------------------------------===//
  // Node creation methods.

  /// Create a ConstantSDNode wrapping a constant value.
  /// If VT is a vector type, the constant is splatted into a BUILD_VECTOR.
  ///
  /// If only legal types can be produced, this does the necessary
  /// transformations (e.g., if the vector element type is illegal).
  /// @{
  SDValue getConstant(uint64_t Val, const SDLoc &DL, EVT VT,
                      bool isTarget = false, bool isOpaque = false);
  SDValue getConstant(const APInt &Val, const SDLoc &DL, EVT VT,
                      bool isTarget = false, bool isOpaque = false);

  SDValue getAllOnesConstant(const SDLoc &DL, EVT VT, bool IsTarget = false,
                             bool IsOpaque = false) {
    return getConstant(APInt::getAllOnes(VT.getScalarSizeInBits()), DL, VT,
                       IsTarget, IsOpaque);
  }

  SDValue getConstant(const ConstantInt &Val, const SDLoc &DL, EVT VT,
                      bool isTarget = false, bool isOpaque = false);
  SDValue getIntPtrConstant(uint64_t Val, const SDLoc &DL,
                            bool isTarget = false);
  SDValue getShiftAmountConstant(uint64_t Val, EVT VT, const SDLoc &DL);
  SDValue getShiftAmountConstant(const APInt &Val, EVT VT, const SDLoc &DL);
  SDValue getVectorIdxConstant(uint64_t Val, const SDLoc &DL,
                               bool isTarget = false);

  SDValue getTargetConstant(uint64_t Val, const SDLoc &DL, EVT VT,
                            bool isOpaque = false) {
    return getConstant(Val, DL, VT, true, isOpaque);
  }
  SDValue getTargetConstant(const APInt &Val, const SDLoc &DL, EVT VT,
                            bool isOpaque = false) {
    return getConstant(Val, DL, VT, true, isOpaque);
  }
  SDValue getTargetConstant(const ConstantInt &Val, const SDLoc &DL, EVT VT,
                            bool isOpaque = false) {
    return getConstant(Val, DL, VT, true, isOpaque);
  }

  /// Create a true or false constant of type \p VT using the target's
  /// BooleanContent for type \p OpVT.
  SDValue getBoolConstant(bool V, const SDLoc &DL, EVT VT, EVT OpVT);
  /// @}

  /// Create a ConstantFPSDNode wrapping a constant value.
  /// If VT is a vector type, the constant is splatted into a BUILD_VECTOR.
  ///
  /// If only legal types can be produced, this does the necessary
  /// transformations (e.g., if the vector element type is illegal).
  /// The forms that take a double should only be used for simple constants
  /// that can be exactly represented in VT.  No checks are made.
  /// @{
  SDValue getConstantFP(double Val, const SDLoc &DL, EVT VT,
                        bool isTarget = false);
  SDValue getConstantFP(const APFloat &Val, const SDLoc &DL, EVT VT,
                        bool isTarget = false);
  SDValue getConstantFP(const ConstantFP &V, const SDLoc &DL, EVT VT,
                        bool isTarget = false);
  SDValue getTargetConstantFP(double Val, const SDLoc &DL, EVT VT) {
    return getConstantFP(Val, DL, VT, true);
  }
  SDValue getTargetConstantFP(const APFloat &Val, const SDLoc &DL, EVT VT) {
    return getConstantFP(Val, DL, VT, true);
  }
  SDValue getTargetConstantFP(const ConstantFP &Val, const SDLoc &DL, EVT VT) {
    return getConstantFP(Val, DL, VT, true);
  }
  /// @}

  SDValue getGlobalAddress(const GlobalValue *GV, const SDLoc &DL, EVT VT,
                           int64_t offset = 0, bool isTargetGA = false,
                           unsigned TargetFlags = 0);
  SDValue getTargetGlobalAddress(const GlobalValue *GV, const SDLoc &DL, EVT VT,
                                 int64_t offset = 0, unsigned TargetFlags = 0) {
    return getGlobalAddress(GV, DL, VT, offset, true, TargetFlags);
  }
  SDValue getFrameIndex(int FI, EVT VT, bool isTarget = false);
  SDValue getTargetFrameIndex(int FI, EVT VT) {
    return getFrameIndex(FI, VT, true);
  }
  SDValue getJumpTable(int JTI, EVT VT, bool isTarget = false,
                       unsigned TargetFlags = 0);
  SDValue getTargetJumpTable(int JTI, EVT VT, unsigned TargetFlags = 0) {
    return getJumpTable(JTI, VT, true, TargetFlags);
  }
  SDValue getJumpTableDebugInfo(int JTI, SDValue Chain, const SDLoc &DL);
  SDValue getConstantPool(const Constant *C, EVT VT,
                          MaybeAlign Align = std::nullopt, int Offs = 0,
                          bool isT = false, unsigned TargetFlags = 0);
  SDValue getTargetConstantPool(const Constant *C, EVT VT,
                                MaybeAlign Align = std::nullopt, int Offset = 0,
                                unsigned TargetFlags = 0) {
    return getConstantPool(C, VT, Align, Offset, true, TargetFlags);
  }
  SDValue getConstantPool(MachineConstantPoolValue *C, EVT VT,
                          MaybeAlign Align = std::nullopt, int Offs = 0,
                          bool isT = false, unsigned TargetFlags = 0);
  SDValue getTargetConstantPool(MachineConstantPoolValue *C, EVT VT,
                                MaybeAlign Align = std::nullopt, int Offset = 0,
                                unsigned TargetFlags = 0) {
    return getConstantPool(C, VT, Align, Offset, true, TargetFlags);
  }
  // When generating a branch to a BB, we don't in general know enough
  // to provide debug info for the BB at that time, so keep this one around.
  SDValue getBasicBlock(MachineBasicBlock *MBB);
  SDValue getExternalSymbol(const char *Sym, EVT VT);
  SDValue getTargetExternalSymbol(const char *Sym, EVT VT,
                                  unsigned TargetFlags = 0);
  SDValue getMCSymbol(MCSymbol *Sym, EVT VT);

  SDValue getValueType(EVT);
  SDValue getRegister(unsigned Reg, EVT VT);
  SDValue getRegisterMask(const uint32_t *RegMask);
  SDValue getEHLabel(const SDLoc &dl, SDValue Root, MCSymbol *Label);
  SDValue getLabelNode(unsigned Opcode, const SDLoc &dl, SDValue Root,
                       MCSymbol *Label);
  SDValue getBlockAddress(const BlockAddress *BA, EVT VT, int64_t Offset = 0,
                          bool isTarget = false, unsigned TargetFlags = 0);
  SDValue getTargetBlockAddress(const BlockAddress *BA, EVT VT,
                                int64_t Offset = 0, unsigned TargetFlags = 0) {
    return getBlockAddress(BA, VT, Offset, true, TargetFlags);
  }

  SDValue getCopyToReg(SDValue Chain, const SDLoc &dl, unsigned Reg,
                       SDValue N) {
    return getNode(ISD::CopyToReg, dl, MVT::Other, Chain,
                   getRegister(Reg, N.getValueType()), N);
  }

  // This version of the getCopyToReg method takes an extra operand, which
  // indicates that there is potentially an incoming glue value (if Glue is not
  // null) and that there should be a glue result.
  SDValue getCopyToReg(SDValue Chain, const SDLoc &dl, unsigned Reg, SDValue N,
                       SDValue Glue) {
    SDVTList VTs = getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, getRegister(Reg, N.getValueType()), N, Glue };
    return getNode(ISD::CopyToReg, dl, VTs,
                   ArrayRef(Ops, Glue.getNode() ? 4 : 3));
  }

  // Similar to last getCopyToReg() except parameter Reg is a SDValue
  SDValue getCopyToReg(SDValue Chain, const SDLoc &dl, SDValue Reg, SDValue N,
                       SDValue Glue) {
    SDVTList VTs = getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, Reg, N, Glue };
    return getNode(ISD::CopyToReg, dl, VTs,
                   ArrayRef(Ops, Glue.getNode() ? 4 : 3));
  }

  SDValue getCopyFromReg(SDValue Chain, const SDLoc &dl, unsigned Reg, EVT VT) {
    SDVTList VTs = getVTList(VT, MVT::Other);
    SDValue Ops[] = { Chain, getRegister(Reg, VT) };
    return getNode(ISD::CopyFromReg, dl, VTs, Ops);
  }

  // This version of the getCopyFromReg method takes an extra operand, which
  // indicates that there is potentially an incoming glue value (if Glue is not
  // null) and that there should be a glue result.
  SDValue getCopyFromReg(SDValue Chain, const SDLoc &dl, unsigned Reg, EVT VT,
                         SDValue Glue) {
    SDVTList VTs = getVTList(VT, MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, getRegister(Reg, VT), Glue };
    return getNode(ISD::CopyFromReg, dl, VTs,
                   ArrayRef(Ops, Glue.getNode() ? 3 : 2));
  }

  SDValue getCondCode(ISD::CondCode Cond);

  /// Return an ISD::VECTOR_SHUFFLE node. The number of elements in VT,
  /// which must be a vector type, must match the number of mask elements
  /// NumElts. An integer mask element equal to -1 is treated as undefined.
  SDValue getVectorShuffle(EVT VT, const SDLoc &dl, SDValue N1, SDValue N2,
                           ArrayRef<int> Mask);

  /// Return an ISD::BUILD_VECTOR node. The number of elements in VT,
  /// which must be a vector type, must match the number of operands in Ops.
  /// The operands must have the same type as (or, for integers, a type wider
  /// than) VT's element type.
  SDValue getBuildVector(EVT VT, const SDLoc &DL, ArrayRef<SDValue> Ops) {
    // VerifySDNode (via InsertNode) checks BUILD_VECTOR later.
    return getNode(ISD::BUILD_VECTOR, DL, VT, Ops);
  }

  /// Return an ISD::BUILD_VECTOR node. The number of elements in VT,
  /// which must be a vector type, must match the number of operands in Ops.
  /// The operands must have the same type as (or, for integers, a type wider
  /// than) VT's element type.
  SDValue getBuildVector(EVT VT, const SDLoc &DL, ArrayRef<SDUse> Ops) {
    // VerifySDNode (via InsertNode) checks BUILD_VECTOR later.
    return getNode(ISD::BUILD_VECTOR, DL, VT, Ops);
  }

  /// Return a splat ISD::BUILD_VECTOR node, consisting of Op splatted to all
  /// elements. VT must be a vector type. Op's type must be the same as (or,
  /// for integers, a type wider than) VT's element type.
  SDValue getSplatBuildVector(EVT VT, const SDLoc &DL, SDValue Op) {
    // VerifySDNode (via InsertNode) checks BUILD_VECTOR later.
    if (Op.getOpcode() == ISD::UNDEF) {
      assert((VT.getVectorElementType() == Op.getValueType() ||
              (VT.isInteger() &&
               VT.getVectorElementType().bitsLE(Op.getValueType()))) &&
             "A splatted value must have a width equal or (for integers) "
             "greater than the vector element type!");
      return getNode(ISD::UNDEF, SDLoc(), VT);
    }

    SmallVector<SDValue, 16> Ops(VT.getVectorNumElements(), Op);
    return getNode(ISD::BUILD_VECTOR, DL, VT, Ops);
  }

  // Return a splat ISD::SPLAT_VECTOR node, consisting of Op splatted to all
  // elements.
  SDValue getSplatVector(EVT VT, const SDLoc &DL, SDValue Op) {
    if (Op.getOpcode() == ISD::UNDEF) {
      assert((VT.getVectorElementType() == Op.getValueType() ||
              (VT.isInteger() &&
               VT.getVectorElementType().bitsLE(Op.getValueType()))) &&
             "A splatted value must have a width equal or (for integers) "
             "greater than the vector element type!");
      return getNode(ISD::UNDEF, SDLoc(), VT);
    }
    return getNode(ISD::SPLAT_VECTOR, DL, VT, Op);
  }

  /// Returns a node representing a splat of one value into all lanes
  /// of the provided vector type.  This is a utility which returns
  /// either a BUILD_VECTOR or SPLAT_VECTOR depending on the
  /// scalability of the desired vector type.
  SDValue getSplat(EVT VT, const SDLoc &DL, SDValue Op) {
    assert(VT.isVector() && "Can't splat to non-vector type");
    return VT.isScalableVector() ?
      getSplatVector(VT, DL, Op) : getSplatBuildVector(VT, DL, Op);
  }

  /// Returns a vector of type ResVT whose elements contain the linear sequence
  ///   <0, Step, Step * 2, Step * 3, ...>
  SDValue getStepVector(const SDLoc &DL, EVT ResVT, const APInt &StepVal);

  /// Returns a vector of type ResVT whose elements contain the linear sequence
  ///   <0, 1, 2, 3, ...>
  SDValue getStepVector(const SDLoc &DL, EVT ResVT);

  /// Returns an ISD::VECTOR_SHUFFLE node semantically equivalent to
  /// the shuffle node in input but with swapped operands.
  ///
  /// Example: shuffle A, B, <0,5,2,7> -> shuffle B, A, <4,1,6,3>
  SDValue getCommutedVectorShuffle(const ShuffleVectorSDNode &SV);

  /// Convert Op, which must be of float type, to the
  /// float type VT, by either extending or rounding (by truncation).
  SDValue getFPExtendOrRound(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be a STRICT operation of float type, to the
  /// float type VT, by either extending or rounding (by truncation).
  std::pair<SDValue, SDValue>
  getStrictFPExtendOrRound(SDValue Op, SDValue Chain, const SDLoc &DL, EVT VT);

  /// Convert *_EXTEND_VECTOR_INREG to *_EXTEND opcode.
  static unsigned getOpcode_EXTEND(unsigned Opcode) {
    switch (Opcode) {
    case ISD::ANY_EXTEND:
    case ISD::ANY_EXTEND_VECTOR_INREG:
      return ISD::ANY_EXTEND;
    case ISD::ZERO_EXTEND:
    case ISD::ZERO_EXTEND_VECTOR_INREG:
      return ISD::ZERO_EXTEND;
    case ISD::SIGN_EXTEND:
    case ISD::SIGN_EXTEND_VECTOR_INREG:
      return ISD::SIGN_EXTEND;
    }
    llvm_unreachable("Unknown opcode");
  }

  /// Convert *_EXTEND to *_EXTEND_VECTOR_INREG opcode.
  static unsigned getOpcode_EXTEND_VECTOR_INREG(unsigned Opcode) {
    switch (Opcode) {
    case ISD::ANY_EXTEND:
    case ISD::ANY_EXTEND_VECTOR_INREG:
      return ISD::ANY_EXTEND_VECTOR_INREG;
    case ISD::ZERO_EXTEND:
    case ISD::ZERO_EXTEND_VECTOR_INREG:
      return ISD::ZERO_EXTEND_VECTOR_INREG;
    case ISD::SIGN_EXTEND:
    case ISD::SIGN_EXTEND_VECTOR_INREG:
      return ISD::SIGN_EXTEND_VECTOR_INREG;
    }
    llvm_unreachable("Unknown opcode");
  }

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either any-extending or truncating it.
  SDValue getAnyExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either sign-extending or truncating it.
  SDValue getSExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either zero-extending or truncating it.
  SDValue getZExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either any/sign/zero-extending (depending on IsAny /
  /// IsSigned) or truncating it.
  SDValue getExtOrTrunc(SDValue Op, const SDLoc &DL,
                        EVT VT, unsigned Opcode) {
    switch(Opcode) {
      case ISD::ANY_EXTEND:
        return getAnyExtOrTrunc(Op, DL, VT);
      case ISD::ZERO_EXTEND:
        return getZExtOrTrunc(Op, DL, VT);
      case ISD::SIGN_EXTEND:
        return getSExtOrTrunc(Op, DL, VT);
    }
    llvm_unreachable("Unsupported opcode");
  }

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either sign/zero-extending (depending on IsSigned) or
  /// truncating it.
  SDValue getExtOrTrunc(bool IsSigned, SDValue Op, const SDLoc &DL, EVT VT) {
    return IsSigned ? getSExtOrTrunc(Op, DL, VT) : getZExtOrTrunc(Op, DL, VT);
  }

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by first bitcasting (from potential vector) to
  /// corresponding scalar type then either any-extending or truncating it.
  SDValue getBitcastedAnyExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by first bitcasting (from potential vector) to
  /// corresponding scalar type then either sign-extending or truncating it.
  SDValue getBitcastedSExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by first bitcasting (from potential vector) to
  /// corresponding scalar type then either zero-extending or truncating it.
  SDValue getBitcastedZExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Return the expression required to zero extend the Op
  /// value assuming it was the smaller SrcTy value.
  SDValue getZeroExtendInReg(SDValue Op, const SDLoc &DL, EVT VT);

  /// Return the expression required to zero extend the Op
  /// value assuming it was the smaller SrcTy value.
  SDValue getVPZeroExtendInReg(SDValue Op, SDValue Mask, SDValue EVL,
                               const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the integer type VT, by
  /// either truncating it or performing either zero or sign extension as
  /// appropriate extension for the pointer's semantics.
  SDValue getPtrExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Return the expression required to extend the Op as a pointer value
  /// assuming it was the smaller SrcTy value. This may be either a zero extend
  /// or a sign extend.
  SDValue getPtrExtendInReg(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert Op, which must be of integer type, to the integer type VT,
  /// by using an extension appropriate for the target's
  /// BooleanContent for type OpVT or truncating it.
  SDValue getBoolExtOrTrunc(SDValue Op, const SDLoc &SL, EVT VT, EVT OpVT);

  /// Create negative operation as (SUB 0, Val).
  SDValue getNegative(SDValue Val, const SDLoc &DL, EVT VT);

  /// Create a bitwise NOT operation as (XOR Val, -1).
  SDValue getNOT(const SDLoc &DL, SDValue Val, EVT VT);

  /// Create a logical NOT operation as (XOR Val, BooleanOne).
  SDValue getLogicalNOT(const SDLoc &DL, SDValue Val, EVT VT);

  /// Create a vector-predicated logical NOT operation as (VP_XOR Val,
  /// BooleanOne, Mask, EVL).
  SDValue getVPLogicalNOT(const SDLoc &DL, SDValue Val, SDValue Mask,
                          SDValue EVL, EVT VT);

  /// Convert a vector-predicated Op, which must be an integer vector, to the
  /// vector-type VT, by performing either vector-predicated zext or truncating
  /// it. The Op will be returned as-is if Op and VT are vectors containing
  /// integer with same width.
  SDValue getVPZExtOrTrunc(const SDLoc &DL, EVT VT, SDValue Op, SDValue Mask,
                           SDValue EVL);

  /// Convert a vector-predicated Op, which must be of integer type, to the
  /// vector-type integer type VT, by either truncating it or performing either
  /// vector-predicated zero or sign extension as appropriate extension for the
  /// pointer's semantics. This function just redirects to getVPZExtOrTrunc
  /// right now.
  SDValue getVPPtrExtOrTrunc(const SDLoc &DL, EVT VT, SDValue Op, SDValue Mask,
                             SDValue EVL);

  /// Returns sum of the base pointer and offset.
  /// Unlike getObjectPtrOffset this does not set NoUnsignedWrap by default.
  SDValue getMemBasePlusOffset(SDValue Base, TypeSize Offset, const SDLoc &DL,
                               const SDNodeFlags Flags = SDNodeFlags());
  SDValue getMemBasePlusOffset(SDValue Base, SDValue Offset, const SDLoc &DL,
                               const SDNodeFlags Flags = SDNodeFlags());

  /// Create an add instruction with appropriate flags when used for
  /// addressing some offset of an object. i.e. if a load is split into multiple
  /// components, create an add nuw from the base pointer to the offset.
  SDValue getObjectPtrOffset(const SDLoc &SL, SDValue Ptr, TypeSize Offset) {
    SDNodeFlags Flags;
    Flags.setNoUnsignedWrap(true);
    return getMemBasePlusOffset(Ptr, Offset, SL, Flags);
  }

  SDValue getObjectPtrOffset(const SDLoc &SL, SDValue Ptr, SDValue Offset) {
    // The object itself can't wrap around the address space, so it shouldn't be
    // possible for the adds of the offsets to the split parts to overflow.
    SDNodeFlags Flags;
    Flags.setNoUnsignedWrap(true);
    return getMemBasePlusOffset(Ptr, Offset, SL, Flags);
  }

  /// Return a new CALLSEQ_START node, that starts new call frame, in which
  /// InSize bytes are set up inside CALLSEQ_START..CALLSEQ_END sequence and
  /// OutSize specifies part of the frame set up prior to the sequence.
  SDValue getCALLSEQ_START(SDValue Chain, uint64_t InSize, uint64_t OutSize,
                           const SDLoc &DL) {
    SDVTList VTs = getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain,
                      getIntPtrConstant(InSize, DL, true),
                      getIntPtrConstant(OutSize, DL, true) };
    return getNode(ISD::CALLSEQ_START, DL, VTs, Ops);
  }

  /// Return a new CALLSEQ_END node, which always must have a
  /// glue result (to ensure it's not CSE'd).
  /// CALLSEQ_END does not have a useful SDLoc.
  SDValue getCALLSEQ_END(SDValue Chain, SDValue Op1, SDValue Op2,
                         SDValue InGlue, const SDLoc &DL) {
    SDVTList NodeTys = getVTList(MVT::Other, MVT::Glue);
    SmallVector<SDValue, 4> Ops;
    Ops.push_back(Chain);
    Ops.push_back(Op1);
    Ops.push_back(Op2);
    if (InGlue.getNode())
      Ops.push_back(InGlue);
    return getNode(ISD::CALLSEQ_END, DL, NodeTys, Ops);
  }

  SDValue getCALLSEQ_END(SDValue Chain, uint64_t Size1, uint64_t Size2,
                         SDValue Glue, const SDLoc &DL) {
    return getCALLSEQ_END(
        Chain, getIntPtrConstant(Size1, DL, /*isTarget=*/true),
        getIntPtrConstant(Size2, DL, /*isTarget=*/true), Glue, DL);
  }

  /// Return true if the result of this operation is always undefined.
  bool isUndef(unsigned Opcode, ArrayRef<SDValue> Ops);

  /// Return an UNDEF node. UNDEF does not have a useful SDLoc.
  SDValue getUNDEF(EVT VT) {
    return getNode(ISD::UNDEF, SDLoc(), VT);
  }

  /// Return a node that represents the runtime scaling 'MulImm * RuntimeVL'.
  SDValue getVScale(const SDLoc &DL, EVT VT, APInt MulImm,
                    bool ConstantFold = true);

  SDValue getElementCount(const SDLoc &DL, EVT VT, ElementCount EC,
                          bool ConstantFold = true);

  /// Return a GLOBAL_OFFSET_TABLE node. This does not have a useful SDLoc.
  SDValue getGLOBAL_OFFSET_TABLE(EVT VT) {
    return getNode(ISD::GLOBAL_OFFSET_TABLE, SDLoc(), VT);
  }

  /// Gets or creates the specified node.
  ///
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT,
                  ArrayRef<SDUse> Ops);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT,
                  ArrayRef<SDValue> Ops, const SDNodeFlags Flags);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, ArrayRef<EVT> ResultTys,
                  ArrayRef<SDValue> Ops);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList,
                  ArrayRef<SDValue> Ops, const SDNodeFlags Flags);

  // Use flags from current flag inserter.
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT,
                  ArrayRef<SDValue> Ops);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList,
                  ArrayRef<SDValue> Ops);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue Operand);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, SDValue N3);

  // Specialize based on number of operands.
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue Operand,
                  const SDNodeFlags Flags);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, const SDNodeFlags Flags);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, SDValue N3, const SDNodeFlags Flags);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, SDValue N3, SDValue N4);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, SDValue N3, SDValue N4, SDValue N5);

  // Specialize again based on number of operands for nodes with a VTList
  // rather than a single VT.
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList, SDValue N);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList, SDValue N1,
                  SDValue N2);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList, SDValue N1,
                  SDValue N2, SDValue N3);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList, SDValue N1,
                  SDValue N2, SDValue N3, SDValue N4);
  SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList, SDValue N1,
                  SDValue N2, SDValue N3, SDValue N4, SDValue N5);

  /// Compute a TokenFactor to force all the incoming stack arguments to be
  /// loaded from the stack. This is used in tail call lowering to protect
  /// stack arguments from being clobbered.
  SDValue getStackArgumentTokenFactor(SDValue Chain);

  /* \p CI if not null is the memset call being lowered.
   * \p OverrideTailCall is an optional parameter that can be used to override
   * the tail call optimization decision. */
  SDValue
  getMemcpy(SDValue Chain, const SDLoc &dl, SDValue Dst, SDValue Src,
            SDValue Size, Align Alignment, bool isVol, bool AlwaysInline,
            const CallInst *CI, std::optional<bool> OverrideTailCall,
            MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo,
            const AAMDNodes &AAInfo = AAMDNodes(), AAResults *AA = nullptr);

  /* \p CI if not null is the memset call being lowered.
   * \p OverrideTailCall is an optional parameter that can be used to override
   * the tail call optimization decision. */
  SDValue getMemmove(SDValue Chain, const SDLoc &dl, SDValue Dst, SDValue Src,
                     SDValue Size, Align Alignment, bool isVol,
                     const CallInst *CI, std::optional<bool> OverrideTailCall,
                     MachinePointerInfo DstPtrInfo,
                     MachinePointerInfo SrcPtrInfo,
                     const AAMDNodes &AAInfo = AAMDNodes(),
                     AAResults *AA = nullptr);

  SDValue getMemset(SDValue Chain, const SDLoc &dl, SDValue Dst, SDValue Src,
                    SDValue Size, Align Alignment, bool isVol,
                    bool AlwaysInline, const CallInst *CI,
                    MachinePointerInfo DstPtrInfo,
                    const AAMDNodes &AAInfo = AAMDNodes());

  SDValue getAtomicMemcpy(SDValue Chain, const SDLoc &dl, SDValue Dst,
                          SDValue Src, SDValue Size, Type *SizeTy,
                          unsigned ElemSz, bool isTailCall,
                          MachinePointerInfo DstPtrInfo,
                          MachinePointerInfo SrcPtrInfo);

  SDValue getAtomicMemmove(SDValue Chain, const SDLoc &dl, SDValue Dst,
                           SDValue Src, SDValue Size, Type *SizeTy,
                           unsigned ElemSz, bool isTailCall,
                           MachinePointerInfo DstPtrInfo,
                           MachinePointerInfo SrcPtrInfo);

  SDValue getAtomicMemset(SDValue Chain, const SDLoc &dl, SDValue Dst,
                          SDValue Value, SDValue Size, Type *SizeTy,
                          unsigned ElemSz, bool isTailCall,
                          MachinePointerInfo DstPtrInfo);

  /// Helper function to make it easier to build SetCC's if you just have an
  /// ISD::CondCode instead of an SDValue.
  SDValue getSetCC(const SDLoc &DL, EVT VT, SDValue LHS, SDValue RHS,
                   ISD::CondCode Cond, SDValue Chain = SDValue(),
                   bool IsSignaling = false) {
    assert(LHS.getValueType().isVector() == RHS.getValueType().isVector() &&
           "Vector/scalar operand type mismatch for setcc");
    assert(LHS.getValueType().isVector() == VT.isVector() &&
           "Vector/scalar result type mismatch for setcc");
    assert(Cond != ISD::SETCC_INVALID &&
           "Cannot create a setCC of an invalid node.");
    if (Chain)
      return getNode(IsSignaling ? ISD::STRICT_FSETCCS : ISD::STRICT_FSETCC, DL,
                     {VT, MVT::Other}, {Chain, LHS, RHS, getCondCode(Cond)});
    return getNode(ISD::SETCC, DL, VT, LHS, RHS, getCondCode(Cond));
  }

  /// Helper function to make it easier to build VP_SETCCs if you just have an
  /// ISD::CondCode instead of an SDValue.
  SDValue getSetCCVP(const SDLoc &DL, EVT VT, SDValue LHS, SDValue RHS,
                     ISD::CondCode Cond, SDValue Mask, SDValue EVL) {
    assert(LHS.getValueType().isVector() && RHS.getValueType().isVector() &&
           "Cannot compare scalars");
    assert(Cond != ISD::SETCC_INVALID &&
           "Cannot create a setCC of an invalid node.");
    return getNode(ISD::VP_SETCC, DL, VT, LHS, RHS, getCondCode(Cond), Mask,
                   EVL);
  }

  /// Helper function to make it easier to build Select's if you just have
  /// operands and don't want to check for vector.
  SDValue getSelect(const SDLoc &DL, EVT VT, SDValue Cond, SDValue LHS,
                    SDValue RHS, SDNodeFlags Flags = SDNodeFlags()) {
    assert(LHS.getValueType() == VT && RHS.getValueType() == VT &&
           "Cannot use select on differing types");
    auto Opcode = Cond.getValueType().isVector() ? ISD::VSELECT : ISD::SELECT;
    return getNode(Opcode, DL, VT, Cond, LHS, RHS, Flags);
  }

  /// Helper function to make it easier to build SelectCC's if you just have an
  /// ISD::CondCode instead of an SDValue.
  SDValue getSelectCC(const SDLoc &DL, SDValue LHS, SDValue RHS, SDValue True,
                      SDValue False, ISD::CondCode Cond) {
    return getNode(ISD::SELECT_CC, DL, True.getValueType(), LHS, RHS, True,
                   False, getCondCode(Cond));
  }

  /// Try to simplify a select/vselect into 1 of its operands or a constant.
  SDValue simplifySelect(SDValue Cond, SDValue TVal, SDValue FVal);

  /// Try to simplify a shift into 1 of its operands or a constant.
  SDValue simplifyShift(SDValue X, SDValue Y);

  /// Try to simplify a floating-point binary operation into 1 of its operands
  /// or a constant.
  SDValue simplifyFPBinop(unsigned Opcode, SDValue X, SDValue Y,
                          SDNodeFlags Flags);

  /// VAArg produces a result and token chain, and takes a pointer
  /// and a source value as input.
  SDValue getVAArg(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                   SDValue SV, unsigned Align);

  /// Gets a node for an atomic cmpxchg op. There are two
  /// valid Opcodes. ISD::ATOMIC_CMO_SWAP produces the value loaded and a
  /// chain result. ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS produces the value loaded,
  /// a success flag (initially i1), and a chain.
  SDValue getAtomicCmpSwap(unsigned Opcode, const SDLoc &dl, EVT MemVT,
                           SDVTList VTs, SDValue Chain, SDValue Ptr,
                           SDValue Cmp, SDValue Swp, MachineMemOperand *MMO);

  /// Gets a node for an atomic op, produces result (if relevant)
  /// and chain and takes 2 operands.
  SDValue getAtomic(unsigned Opcode, const SDLoc &dl, EVT MemVT, SDValue Chain,
                    SDValue Ptr, SDValue Val, MachineMemOperand *MMO);

  /// Gets a node for an atomic op, produces result and chain and
  /// takes 1 operand.
  SDValue getAtomic(unsigned Opcode, const SDLoc &dl, EVT MemVT, EVT VT,
                    SDValue Chain, SDValue Ptr, MachineMemOperand *MMO);

  /// Gets a node for an atomic op, produces result and chain and takes N
  /// operands.
  SDValue getAtomic(unsigned Opcode, const SDLoc &dl, EVT MemVT,
                    SDVTList VTList, ArrayRef<SDValue> Ops,
                    MachineMemOperand *MMO);

  /// Creates a MemIntrinsicNode that may produce a
  /// result and takes a list of operands. Opcode may be INTRINSIC_VOID,
  /// INTRINSIC_W_CHAIN, or a target-specific opcode with a value not
  /// less than FIRST_TARGET_MEMORY_OPCODE.
  SDValue getMemIntrinsicNode(
      unsigned Opcode, const SDLoc &dl, SDVTList VTList, ArrayRef<SDValue> Ops,
      EVT MemVT, MachinePointerInfo PtrInfo, Align Alignment,
      MachineMemOperand::Flags Flags = MachineMemOperand::MOLoad |
                                       MachineMemOperand::MOStore,
      LocationSize Size = 0, const AAMDNodes &AAInfo = AAMDNodes());

  inline SDValue getMemIntrinsicNode(
      unsigned Opcode, const SDLoc &dl, SDVTList VTList, ArrayRef<SDValue> Ops,
      EVT MemVT, MachinePointerInfo PtrInfo,
      MaybeAlign Alignment = std::nullopt,
      MachineMemOperand::Flags Flags = MachineMemOperand::MOLoad |
                                       MachineMemOperand::MOStore,
      LocationSize Size = 0, const AAMDNodes &AAInfo = AAMDNodes()) {
    // Ensure that codegen never sees alignment 0
    return getMemIntrinsicNode(Opcode, dl, VTList, Ops, MemVT, PtrInfo,
                               Alignment.value_or(getEVTAlign(MemVT)), Flags,
                               Size, AAInfo);
  }

  SDValue getMemIntrinsicNode(unsigned Opcode, const SDLoc &dl, SDVTList VTList,
                              ArrayRef<SDValue> Ops, EVT MemVT,
                              MachineMemOperand *MMO);

  /// Creates a LifetimeSDNode that starts (`IsStart==true`) or ends
  /// (`IsStart==false`) the lifetime of the portion of `FrameIndex` between
  /// offsets `Offset` and `Offset + Size`.
  SDValue getLifetimeNode(bool IsStart, const SDLoc &dl, SDValue Chain,
                          int FrameIndex, int64_t Size, int64_t Offset = -1);

  /// Creates a PseudoProbeSDNode with function GUID `Guid` and
  /// the index of the block `Index` it is probing, as well as the attributes
  /// `attr` of the probe.
  SDValue getPseudoProbeNode(const SDLoc &Dl, SDValue Chain, uint64_t Guid,
                             uint64_t Index, uint32_t Attr);

  /// Create a MERGE_VALUES node from the given operands.
  SDValue getMergeValues(ArrayRef<SDValue> Ops, const SDLoc &dl);

  /// Loads are not normal binary operators: their result type is not
  /// determined by their operands, and they produce a value AND a token chain.
  ///
  /// This function will set the MOLoad flag on MMOFlags, but you can set it if
  /// you want.  The MOStore flag must not be set.
  SDValue getLoad(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                  MachinePointerInfo PtrInfo,
                  MaybeAlign Alignment = MaybeAlign(),
                  MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
                  const AAMDNodes &AAInfo = AAMDNodes(),
                  const MDNode *Ranges = nullptr);
  SDValue getLoad(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                  MachineMemOperand *MMO);
  SDValue
  getExtLoad(ISD::LoadExtType ExtType, const SDLoc &dl, EVT VT, SDValue Chain,
             SDValue Ptr, MachinePointerInfo PtrInfo, EVT MemVT,
             MaybeAlign Alignment = MaybeAlign(),
             MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
             const AAMDNodes &AAInfo = AAMDNodes());
  SDValue getExtLoad(ISD::LoadExtType ExtType, const SDLoc &dl, EVT VT,
                     SDValue Chain, SDValue Ptr, EVT MemVT,
                     MachineMemOperand *MMO);
  SDValue getIndexedLoad(SDValue OrigLoad, const SDLoc &dl, SDValue Base,
                         SDValue Offset, ISD::MemIndexedMode AM);
  SDValue getLoad(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT,
                  const SDLoc &dl, SDValue Chain, SDValue Ptr, SDValue Offset,
                  MachinePointerInfo PtrInfo, EVT MemVT, Align Alignment,
                  MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
                  const AAMDNodes &AAInfo = AAMDNodes(),
                  const MDNode *Ranges = nullptr);
  inline SDValue getLoad(
      ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT, const SDLoc &dl,
      SDValue Chain, SDValue Ptr, SDValue Offset, MachinePointerInfo PtrInfo,
      EVT MemVT, MaybeAlign Alignment = MaybeAlign(),
      MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
      const AAMDNodes &AAInfo = AAMDNodes(), const MDNode *Ranges = nullptr) {
    // Ensures that codegen never sees a None Alignment.
    return getLoad(AM, ExtType, VT, dl, Chain, Ptr, Offset, PtrInfo, MemVT,
                   Alignment.value_or(getEVTAlign(MemVT)), MMOFlags, AAInfo,
                   Ranges);
  }
  SDValue getLoad(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT,
                  const SDLoc &dl, SDValue Chain, SDValue Ptr, SDValue Offset,
                  EVT MemVT, MachineMemOperand *MMO);

  /// Helper function to build ISD::STORE nodes.
  ///
  /// This function will set the MOStore flag on MMOFlags, but you can set it if
  /// you want.  The MOLoad and MOInvariant flags must not be set.

  SDValue
  getStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
           MachinePointerInfo PtrInfo, Align Alignment,
           MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
           const AAMDNodes &AAInfo = AAMDNodes());
  inline SDValue
  getStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
           MachinePointerInfo PtrInfo, MaybeAlign Alignment = MaybeAlign(),
           MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
           const AAMDNodes &AAInfo = AAMDNodes()) {
    return getStore(Chain, dl, Val, Ptr, PtrInfo,
                    Alignment.value_or(getEVTAlign(Val.getValueType())),
                    MMOFlags, AAInfo);
  }
  SDValue getStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
                   MachineMemOperand *MMO);
  SDValue
  getTruncStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
                MachinePointerInfo PtrInfo, EVT SVT, Align Alignment,
                MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
                const AAMDNodes &AAInfo = AAMDNodes());
  inline SDValue
  getTruncStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
                MachinePointerInfo PtrInfo, EVT SVT,
                MaybeAlign Alignment = MaybeAlign(),
                MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
                const AAMDNodes &AAInfo = AAMDNodes()) {
    return getTruncStore(Chain, dl, Val, Ptr, PtrInfo, SVT,
                         Alignment.value_or(getEVTAlign(SVT)), MMOFlags,
                         AAInfo);
  }
  SDValue getTruncStore(SDValue Chain, const SDLoc &dl, SDValue Val,
                        SDValue Ptr, EVT SVT, MachineMemOperand *MMO);
  SDValue getIndexedStore(SDValue OrigStore, const SDLoc &dl, SDValue Base,
                          SDValue Offset, ISD::MemIndexedMode AM);

  SDValue getLoadVP(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT,
                    const SDLoc &dl, SDValue Chain, SDValue Ptr, SDValue Offset,
                    SDValue Mask, SDValue EVL, MachinePointerInfo PtrInfo,
                    EVT MemVT, Align Alignment,
                    MachineMemOperand::Flags MMOFlags, const AAMDNodes &AAInfo,
                    const MDNode *Ranges = nullptr, bool IsExpanding = false);
  inline SDValue
  getLoadVP(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT,
            const SDLoc &dl, SDValue Chain, SDValue Ptr, SDValue Offset,
            SDValue Mask, SDValue EVL, MachinePointerInfo PtrInfo, EVT MemVT,
            MaybeAlign Alignment = MaybeAlign(),
            MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
            const AAMDNodes &AAInfo = AAMDNodes(),
            const MDNode *Ranges = nullptr, bool IsExpanding = false) {
    // Ensures that codegen never sees a None Alignment.
    return getLoadVP(AM, ExtType, VT, dl, Chain, Ptr, Offset, Mask, EVL,
                     PtrInfo, MemVT, Alignment.value_or(getEVTAlign(MemVT)),
                     MMOFlags, AAInfo, Ranges, IsExpanding);
  }
  SDValue getLoadVP(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT,
                    const SDLoc &dl, SDValue Chain, SDValue Ptr, SDValue Offset,
                    SDValue Mask, SDValue EVL, EVT MemVT,
                    MachineMemOperand *MMO, bool IsExpanding = false);
  SDValue getLoadVP(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                    SDValue Mask, SDValue EVL, MachinePointerInfo PtrInfo,
                    MaybeAlign Alignment, MachineMemOperand::Flags MMOFlags,
                    const AAMDNodes &AAInfo, const MDNode *Ranges = nullptr,
                    bool IsExpanding = false);
  SDValue getLoadVP(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                    SDValue Mask, SDValue EVL, MachineMemOperand *MMO,
                    bool IsExpanding = false);
  SDValue getExtLoadVP(ISD::LoadExtType ExtType, const SDLoc &dl, EVT VT,
                       SDValue Chain, SDValue Ptr, SDValue Mask, SDValue EVL,
                       MachinePointerInfo PtrInfo, EVT MemVT,
                       MaybeAlign Alignment, MachineMemOperand::Flags MMOFlags,
                       const AAMDNodes &AAInfo, bool IsExpanding = false);
  SDValue getExtLoadVP(ISD::LoadExtType ExtType, const SDLoc &dl, EVT VT,
                       SDValue Chain, SDValue Ptr, SDValue Mask, SDValue EVL,
                       EVT MemVT, MachineMemOperand *MMO,
                       bool IsExpanding = false);
  SDValue getIndexedLoadVP(SDValue OrigLoad, const SDLoc &dl, SDValue Base,
                           SDValue Offset, ISD::MemIndexedMode AM);
  SDValue getStoreVP(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
                     SDValue Offset, SDValue Mask, SDValue EVL, EVT MemVT,
                     MachineMemOperand *MMO, ISD::MemIndexedMode AM,
                     bool IsTruncating = false, bool IsCompressing = false);
  SDValue getTruncStoreVP(SDValue Chain, const SDLoc &dl, SDValue Val,
                          SDValue Ptr, SDValue Mask, SDValue EVL,
                          MachinePointerInfo PtrInfo, EVT SVT, Align Alignment,
                          MachineMemOperand::Flags MMOFlags,
                          const AAMDNodes &AAInfo, bool IsCompressing = false);
  SDValue getTruncStoreVP(SDValue Chain, const SDLoc &dl, SDValue Val,
                          SDValue Ptr, SDValue Mask, SDValue EVL, EVT SVT,
                          MachineMemOperand *MMO, bool IsCompressing = false);
  SDValue getIndexedStoreVP(SDValue OrigStore, const SDLoc &dl, SDValue Base,
                            SDValue Offset, ISD::MemIndexedMode AM);

  SDValue getStridedLoadVP(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType,
                           EVT VT, const SDLoc &DL, SDValue Chain, SDValue Ptr,
                           SDValue Offset, SDValue Stride, SDValue Mask,
                           SDValue EVL, EVT MemVT, MachineMemOperand *MMO,
                           bool IsExpanding = false);
  SDValue getStridedLoadVP(EVT VT, const SDLoc &DL, SDValue Chain, SDValue Ptr,
                           SDValue Stride, SDValue Mask, SDValue EVL,
                           MachineMemOperand *MMO, bool IsExpanding = false);
  SDValue getExtStridedLoadVP(ISD::LoadExtType ExtType, const SDLoc &DL, EVT VT,
                              SDValue Chain, SDValue Ptr, SDValue Stride,
                              SDValue Mask, SDValue EVL, EVT MemVT,
                              MachineMemOperand *MMO, bool IsExpanding = false);
  SDValue getStridedStoreVP(SDValue Chain, const SDLoc &DL, SDValue Val,
                            SDValue Ptr, SDValue Offset, SDValue Stride,
                            SDValue Mask, SDValue EVL, EVT MemVT,
                            MachineMemOperand *MMO, ISD::MemIndexedMode AM,
                            bool IsTruncating = false,
                            bool IsCompressing = false);
  SDValue getTruncStridedStoreVP(SDValue Chain, const SDLoc &DL, SDValue Val,
                                 SDValue Ptr, SDValue Stride, SDValue Mask,
                                 SDValue EVL, EVT SVT, MachineMemOperand *MMO,
                                 bool IsCompressing = false);

  SDValue getGatherVP(SDVTList VTs, EVT VT, const SDLoc &dl,
                      ArrayRef<SDValue> Ops, MachineMemOperand *MMO,
                      ISD::MemIndexType IndexType);
  SDValue getScatterVP(SDVTList VTs, EVT VT, const SDLoc &dl,
                       ArrayRef<SDValue> Ops, MachineMemOperand *MMO,
                       ISD::MemIndexType IndexType);

  SDValue getMaskedLoad(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Base,
                        SDValue Offset, SDValue Mask, SDValue Src0, EVT MemVT,
                        MachineMemOperand *MMO, ISD::MemIndexedMode AM,
                        ISD::LoadExtType, bool IsExpanding = false);
  SDValue getIndexedMaskedLoad(SDValue OrigLoad, const SDLoc &dl, SDValue Base,
                               SDValue Offset, ISD::MemIndexedMode AM);
  SDValue getMaskedStore(SDValue Chain, const SDLoc &dl, SDValue Val,
                         SDValue Base, SDValue Offset, SDValue Mask, EVT MemVT,
                         MachineMemOperand *MMO, ISD::MemIndexedMode AM,
                         bool IsTruncating = false, bool IsCompressing = false);
  SDValue getIndexedMaskedStore(SDValue OrigStore, const SDLoc &dl,
                                SDValue Base, SDValue Offset,
                                ISD::MemIndexedMode AM);
  SDValue getMaskedGather(SDVTList VTs, EVT MemVT, const SDLoc &dl,
                          ArrayRef<SDValue> Ops, MachineMemOperand *MMO,
                          ISD::MemIndexType IndexType, ISD::LoadExtType ExtTy);
  SDValue getMaskedScatter(SDVTList VTs, EVT MemVT, const SDLoc &dl,
                           ArrayRef<SDValue> Ops, MachineMemOperand *MMO,
                           ISD::MemIndexType IndexType,
                           bool IsTruncating = false);
  SDValue getMaskedHistogram(SDVTList VTs, EVT MemVT, const SDLoc &dl,
                             ArrayRef<SDValue> Ops, MachineMemOperand *MMO,
                             ISD::MemIndexType IndexType);

  SDValue getGetFPEnv(SDValue Chain, const SDLoc &dl, SDValue Ptr, EVT MemVT,
                      MachineMemOperand *MMO);
  SDValue getSetFPEnv(SDValue Chain, const SDLoc &dl, SDValue Ptr, EVT MemVT,
                      MachineMemOperand *MMO);

  /// Construct a node to track a Value* through the backend.
  SDValue getSrcValue(const Value *v);

  /// Return an MDNodeSDNode which holds an MDNode.
  SDValue getMDNode(const MDNode *MD);

  /// Return a bitcast using the SDLoc of the value operand, and casting to the
  /// provided type. Use getNode to set a custom SDLoc.
  SDValue getBitcast(EVT VT, SDValue V);

  /// Return an AddrSpaceCastSDNode.
  SDValue getAddrSpaceCast(const SDLoc &dl, EVT VT, SDValue Ptr, unsigned SrcAS,
                           unsigned DestAS);

  /// Return a freeze using the SDLoc of the value operand.
  SDValue getFreeze(SDValue V);

  /// Return an AssertAlignSDNode.
  SDValue getAssertAlign(const SDLoc &DL, SDValue V, Align A);

  /// Swap N1 and N2 if Opcode is a commutative binary opcode
  /// and the canonical form expects the opposite order.
  void canonicalizeCommutativeBinop(unsigned Opcode, SDValue &N1,
                                    SDValue &N2) const;

  /// Return the specified value casted to
  /// the target's desired shift amount type.
  SDValue getShiftAmountOperand(EVT LHSTy, SDValue Op);

  /// Expand the specified \c ISD::VAARG node as the Legalize pass would.
  SDValue expandVAArg(SDNode *Node);

  /// Expand the specified \c ISD::VACOPY node as the Legalize pass would.
  SDValue expandVACopy(SDNode *Node);

  /// Return a GlobalAddress of the function from the current module with
  /// name matching the given ExternalSymbol. Additionally can provide the
  /// matched function.
  /// Panic if the function doesn't exist.
  SDValue getSymbolFunctionGlobalAddress(SDValue Op,
                                         Function **TargetFunction = nullptr);

  /// *Mutate* the specified node in-place to have the
  /// specified operands.  If the resultant node already exists in the DAG,
  /// this does not modify the specified node, instead it returns the node that
  /// already exists.  If the resultant node does not exist in the DAG, the
  /// input node is returned.  As a degenerate case, if you specify the same
  /// input operands as the node already has, the input node is returned.
  SDNode *UpdateNodeOperands(SDNode *N, SDValue Op);
  SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2);
  SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2,
                               SDValue Op3);
  SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2,
                               SDValue Op3, SDValue Op4);
  SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2,
                               SDValue Op3, SDValue Op4, SDValue Op5);
  SDNode *UpdateNodeOperands(SDNode *N, ArrayRef<SDValue> Ops);

  /// Creates a new TokenFactor containing \p Vals. If \p Vals contains 64k
  /// values or more, move values into new TokenFactors in 64k-1 blocks, until
  /// the final TokenFactor has less than 64k operands.
  SDValue getTokenFactor(const SDLoc &DL, SmallVectorImpl<SDValue> &Vals);

  /// *Mutate* the specified machine node's memory references to the provided
  /// list.
  void setNodeMemRefs(MachineSDNode *N,
                      ArrayRef<MachineMemOperand *> NewMemRefs);

  // Calculate divergence of node \p N based on its operands.
  bool calculateDivergence(SDNode *N);

  // Propagates the change in divergence to users
  void updateDivergence(SDNode * N);

  /// These are used for target selectors to *mutate* the
  /// specified node to have the specified return type, Target opcode, and
  /// operands.  Note that target opcodes are stored as
  /// ~TargetOpcode in the node opcode field.  The resultant node is returned.
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT, SDValue Op1);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT,
                       SDValue Op1, SDValue Op2);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT,
                       SDValue Op1, SDValue Op2, SDValue Op3);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT,
                       ArrayRef<SDValue> Ops);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1, EVT VT2);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1,
                       EVT VT2, ArrayRef<SDValue> Ops);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1,
                       EVT VT2, EVT VT3, ArrayRef<SDValue> Ops);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1,
                       EVT VT2, SDValue Op1, SDValue Op2);
  SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, SDVTList VTs,
                       ArrayRef<SDValue> Ops);

  /// This *mutates* the specified node to have the specified
  /// return type, opcode, and operands.
  SDNode *MorphNodeTo(SDNode *N, unsigned Opc, SDVTList VTs,
                      ArrayRef<SDValue> Ops);

  /// Mutate the specified strict FP node to its non-strict equivalent,
  /// unlinking the node from its chain and dropping the metadata arguments.
  /// The node must be a strict FP node.
  SDNode *mutateStrictFPToFP(SDNode *Node);

  /// These are used for target selectors to create a new node
  /// with specified return type(s), MachineInstr opcode, and operands.
  ///
  /// Note that getMachineNode returns the resultant node.  If there is already
  /// a node of the specified opcode and operands, it returns that node instead
  /// of the current one.
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT,
                                SDValue Op1);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT,
                                SDValue Op1, SDValue Op2);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT,
                                SDValue Op1, SDValue Op2, SDValue Op3);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT,
                                ArrayRef<SDValue> Ops);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, SDValue Op1, SDValue Op2);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, SDValue Op1, SDValue Op2, SDValue Op3);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, ArrayRef<SDValue> Ops);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, EVT VT3, SDValue Op1, SDValue Op2);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, EVT VT3, SDValue Op1, SDValue Op2,
                                SDValue Op3);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, EVT VT1,
                                EVT VT2, EVT VT3, ArrayRef<SDValue> Ops);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                ArrayRef<EVT> ResultTys, ArrayRef<SDValue> Ops);
  MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl, SDVTList VTs,
                                ArrayRef<SDValue> Ops);

  /// A convenience function for creating TargetInstrInfo::EXTRACT_SUBREG nodes.
  SDValue getTargetExtractSubreg(int SRIdx, const SDLoc &DL, EVT VT,
                                 SDValue Operand);

  /// A convenience function for creating TargetInstrInfo::INSERT_SUBREG nodes.
  SDValue getTargetInsertSubreg(int SRIdx, const SDLoc &DL, EVT VT,
                                SDValue Operand, SDValue Subreg);

  /// Get the specified node if it's already available, or else return NULL.
  SDNode *getNodeIfExists(unsigned Opcode, SDVTList VTList,
                          ArrayRef<SDValue> Ops, const SDNodeFlags Flags);
  SDNode *getNodeIfExists(unsigned Opcode, SDVTList VTList,
                          ArrayRef<SDValue> Ops);

  /// Check if a node exists without modifying its flags.
  bool doesNodeExist(unsigned Opcode, SDVTList VTList, ArrayRef<SDValue> Ops);

  /// Creates a SDDbgValue node.
  SDDbgValue *getDbgValue(DIVariable *Var, DIExpression *Expr, SDNode *N,
                          unsigned R, bool IsIndirect, const DebugLoc &DL,
                          unsigned O);

  /// Creates a constant SDDbgValue node.
  SDDbgValue *getConstantDbgValue(DIVariable *Var, DIExpression *Expr,
                                  const Value *C, const DebugLoc &DL,
                                  unsigned O);

  /// Creates a FrameIndex SDDbgValue node.
  SDDbgValue *getFrameIndexDbgValue(DIVariable *Var, DIExpression *Expr,
                                    unsigned FI, bool IsIndirect,
                                    const DebugLoc &DL, unsigned O);

  /// Creates a FrameIndex SDDbgValue node.
  SDDbgValue *getFrameIndexDbgValue(DIVariable *Var, DIExpression *Expr,
                                    unsigned FI,
                                    ArrayRef<SDNode *> Dependencies,
                                    bool IsIndirect, const DebugLoc &DL,
                                    unsigned O);

  /// Creates a VReg SDDbgValue node.
  SDDbgValue *getVRegDbgValue(DIVariable *Var, DIExpression *Expr,
                              unsigned VReg, bool IsIndirect,
                              const DebugLoc &DL, unsigned O);

  /// Creates a SDDbgValue node from a list of locations.
  SDDbgValue *getDbgValueList(DIVariable *Var, DIExpression *Expr,
                              ArrayRef<SDDbgOperand> Locs,
                              ArrayRef<SDNode *> Dependencies, bool IsIndirect,
                              const DebugLoc &DL, unsigned O, bool IsVariadic);

  /// Creates a SDDbgLabel node.
  SDDbgLabel *getDbgLabel(DILabel *Label, const DebugLoc &DL, unsigned O);

  /// Transfer debug values from one node to another, while optionally
  /// generating fragment expressions for split-up values. If \p InvalidateDbg
  /// is set, debug values are invalidated after they are transferred.
  void transferDbgValues(SDValue From, SDValue To, unsigned OffsetInBits = 0,
                         unsigned SizeInBits = 0, bool InvalidateDbg = true);

  /// Remove the specified node from the system. If any of its
  /// operands then becomes dead, remove them as well. Inform UpdateListener
  /// for each node deleted.
  void RemoveDeadNode(SDNode *N);

  /// This method deletes the unreachable nodes in the
  /// given list, and any nodes that become unreachable as a result.
  void RemoveDeadNodes(SmallVectorImpl<SDNode *> &DeadNodes);

  /// Modify anything using 'From' to use 'To' instead.
  /// This can cause recursive merging of nodes in the DAG.  Use the first
  /// version if 'From' is known to have a single result, use the second
  /// if you have two nodes with identical results (or if 'To' has a superset
  /// of the results of 'From'), use the third otherwise.
  ///
  /// These methods all take an optional UpdateListener, which (if not null) is
  /// informed about nodes that are deleted and modified due to recursive
  /// changes in the dag.
  ///
  /// These functions only replace all existing uses. It's possible that as
  /// these replacements are being performed, CSE may cause the From node
  /// to be given new uses. These new uses of From are left in place, and
  /// not automatically transferred to To.
  ///
  void ReplaceAllUsesWith(SDValue From, SDValue To);
  void ReplaceAllUsesWith(SDNode *From, SDNode *To);
  void ReplaceAllUsesWith(SDNode *From, const SDValue *To);

  /// Replace any uses of From with To, leaving
  /// uses of other values produced by From.getNode() alone.
  void ReplaceAllUsesOfValueWith(SDValue From, SDValue To);

  /// Like ReplaceAllUsesOfValueWith, but for multiple values at once.
  /// This correctly handles the case where
  /// there is an overlap between the From values and the To values.
  void ReplaceAllUsesOfValuesWith(const SDValue *From, const SDValue *To,
                                  unsigned Num);

  /// If an existing load has uses of its chain, create a token factor node with
  /// that chain and the new memory node's chain and update users of the old
  /// chain to the token factor. This ensures that the new memory node will have
  /// the same relative memory dependency position as the old load. Returns the
  /// new merged load chain.
  SDValue makeEquivalentMemoryOrdering(SDValue OldChain, SDValue NewMemOpChain);

  /// If an existing load has uses of its chain, create a token factor node with
  /// that chain and the new memory node's chain and update users of the old
  /// chain to the token factor. This ensures that the new memory node will have
  /// the same relative memory dependency position as the old load. Returns the
  /// new merged load chain.
  SDValue makeEquivalentMemoryOrdering(LoadSDNode *OldLoad, SDValue NewMemOp);

  /// Topological-sort the AllNodes list and a
  /// assign a unique node id for each node in the DAG based on their
  /// topological order. Returns the number of nodes.
  unsigned AssignTopologicalOrder();

  /// Move node N in the AllNodes list to be immediately
  /// before the given iterator Position. This may be used to update the
  /// topological ordering when the list of nodes is modified.
  void RepositionNode(allnodes_iterator Position, SDNode *N) {
    AllNodes.insert(Position, AllNodes.remove(N));
  }

  /// Returns an APFloat semantics tag appropriate for the given type. If VT is
  /// a vector type, the element semantics are returned.
  static const fltSemantics &EVTToAPFloatSemantics(EVT VT) {
    switch (VT.getScalarType().getSimpleVT().SimpleTy) {
    default: llvm_unreachable("Unknown FP format");
    case MVT::f16:     return APFloat::IEEEhalf();
    case MVT::bf16:    return APFloat::BFloat();
    case MVT::f32:     return APFloat::IEEEsingle();
    case MVT::f64:     return APFloat::IEEEdouble();
    case MVT::f80:     return APFloat::x87DoubleExtended();
    case MVT::f128:    return APFloat::IEEEquad();
    case MVT::ppcf128: return APFloat::PPCDoubleDouble();
    }
  }

  /// Add a dbg_value SDNode. If SD is non-null that means the
  /// value is produced by SD.
  void AddDbgValue(SDDbgValue *DB, bool isParameter);

  /// Add a dbg_label SDNode.
  void AddDbgLabel(SDDbgLabel *DB);

  /// Get the debug values which reference the given SDNode.
  ArrayRef<SDDbgValue*> GetDbgValues(const SDNode* SD) const {
    return DbgInfo->getSDDbgValues(SD);
  }

public:
  /// Return true if there are any SDDbgValue nodes associated
  /// with this SelectionDAG.
  bool hasDebugValues() const { return !DbgInfo->empty(); }

  SDDbgInfo::DbgIterator DbgBegin() const { return DbgInfo->DbgBegin(); }
  SDDbgInfo::DbgIterator DbgEnd() const  { return DbgInfo->DbgEnd(); }

  SDDbgInfo::DbgIterator ByvalParmDbgBegin() const {
    return DbgInfo->ByvalParmDbgBegin();
  }
  SDDbgInfo::DbgIterator ByvalParmDbgEnd() const {
    return DbgInfo->ByvalParmDbgEnd();
  }

  SDDbgInfo::DbgLabelIterator DbgLabelBegin() const {
    return DbgInfo->DbgLabelBegin();
  }
  SDDbgInfo::DbgLabelIterator DbgLabelEnd() const {
    return DbgInfo->DbgLabelEnd();
  }

  /// To be invoked on an SDNode that is slated to be erased. This
  /// function mirrors \c llvm::salvageDebugInfo.
  void salvageDebugInfo(SDNode &N);

  void dump() const;

  /// In most cases this function returns the ABI alignment for a given type,
  /// except for illegal vector types where the alignment exceeds that of the
  /// stack. In such cases we attempt to break the vector down to a legal type
  /// and return the ABI alignment for that instead.
  Align getReducedAlign(EVT VT, bool UseABI);

  /// Create a stack temporary based on the size in bytes and the alignment
  SDValue CreateStackTemporary(TypeSize Bytes, Align Alignment);

  /// Create a stack temporary, suitable for holding the specified value type.
  /// If minAlign is specified, the slot size will have at least that alignment.
  SDValue CreateStackTemporary(EVT VT, unsigned minAlign = 1);

  /// Create a stack temporary suitable for holding either of the specified
  /// value types.
  SDValue CreateStackTemporary(EVT VT1, EVT VT2);

  SDValue FoldSymbolOffset(unsigned Opcode, EVT VT,
                           const GlobalAddressSDNode *GA,
                           const SDNode *N2);

  SDValue FoldConstantArithmetic(unsigned Opcode, const SDLoc &DL, EVT VT,
                                 ArrayRef<SDValue> Ops,
                                 SDNodeFlags Flags = SDNodeFlags());

  /// Fold floating-point operations when all operands are constants and/or
  /// undefined.
  SDValue foldConstantFPMath(unsigned Opcode, const SDLoc &DL, EVT VT,
                             ArrayRef<SDValue> Ops);

  /// Constant fold a setcc to true or false.
  SDValue FoldSetCC(EVT VT, SDValue N1, SDValue N2, ISD::CondCode Cond,
                    const SDLoc &dl);

  /// Return true if the sign bit of Op is known to be zero.
  /// We use this predicate to simplify operations downstream.
  bool SignBitIsZero(SDValue Op, unsigned Depth = 0) const;

  /// Return true if 'Op & Mask' is known to be zero.  We
  /// use this predicate to simplify operations downstream.  Op and Mask are
  /// known to be the same type.
  bool MaskedValueIsZero(SDValue Op, const APInt &Mask,
                         unsigned Depth = 0) const;

  /// Return true if 'Op & Mask' is known to be zero in DemandedElts.  We
  /// use this predicate to simplify operations downstream.  Op and Mask are
  /// known to be the same type.
  bool MaskedValueIsZero(SDValue Op, const APInt &Mask,
                         const APInt &DemandedElts, unsigned Depth = 0) const;

  /// Return true if 'Op' is known to be zero in DemandedElts.  We
  /// use this predicate to simplify operations downstream.
  bool MaskedVectorIsZero(SDValue Op, const APInt &DemandedElts,
                          unsigned Depth = 0) const;

  /// Return true if '(Op & Mask) == Mask'.
  /// Op and Mask are known to be the same type.
  bool MaskedValueIsAllOnes(SDValue Op, const APInt &Mask,
                            unsigned Depth = 0) const;

  /// For each demanded element of a vector, see if it is known to be zero.
  APInt computeVectorKnownZeroElements(SDValue Op, const APInt &DemandedElts,
                                       unsigned Depth = 0) const;

  /// Determine which bits of Op are known to be either zero or one and return
  /// them in Known. For vectors, the known bits are those that are shared by
  /// every vector element.
  /// Targets can implement the computeKnownBitsForTargetNode method in the
  /// TargetLowering class to allow target nodes to be understood.
  KnownBits computeKnownBits(SDValue Op, unsigned Depth = 0) const;

  /// Determine which bits of Op are known to be either zero or one and return
  /// them in Known. The DemandedElts argument allows us to only collect the
  /// known bits that are shared by the requested vector elements.
  /// Targets can implement the computeKnownBitsForTargetNode method in the
  /// TargetLowering class to allow target nodes to be understood.
  KnownBits computeKnownBits(SDValue Op, const APInt &DemandedElts,
                             unsigned Depth = 0) const;

  /// Used to represent the possible overflow behavior of an operation.
  /// Never: the operation cannot overflow.
  /// Always: the operation will always overflow.
  /// Sometime: the operation may or may not overflow.
  enum OverflowKind {
    OFK_Never,
    OFK_Sometime,
    OFK_Always,
  };

  /// Determine if the result of the signed addition of 2 nodes can overflow.
  OverflowKind computeOverflowForSignedAdd(SDValue N0, SDValue N1) const;

  /// Determine if the result of the unsigned addition of 2 nodes can overflow.
  OverflowKind computeOverflowForUnsignedAdd(SDValue N0, SDValue N1) const;

  /// Determine if the result of the addition of 2 nodes can overflow.
  OverflowKind computeOverflowForAdd(bool IsSigned, SDValue N0,
                                     SDValue N1) const {
    return IsSigned ? computeOverflowForSignedAdd(N0, N1)
                    : computeOverflowForUnsignedAdd(N0, N1);
  }

  /// Determine if the result of the addition of 2 nodes can never overflow.
  bool willNotOverflowAdd(bool IsSigned, SDValue N0, SDValue N1) const {
    return computeOverflowForAdd(IsSigned, N0, N1) == OFK_Never;
  }

  /// Determine if the result of the signed sub of 2 nodes can overflow.
  OverflowKind computeOverflowForSignedSub(SDValue N0, SDValue N1) const;

  /// Determine if the result of the unsigned sub of 2 nodes can overflow.
  OverflowKind computeOverflowForUnsignedSub(SDValue N0, SDValue N1) const;

  /// Determine if the result of the sub of 2 nodes can overflow.
  OverflowKind computeOverflowForSub(bool IsSigned, SDValue N0,
                                     SDValue N1) const {
    return IsSigned ? computeOverflowForSignedSub(N0, N1)
                    : computeOverflowForUnsignedSub(N0, N1);
  }

  /// Determine if the result of the sub of 2 nodes can never overflow.
  bool willNotOverflowSub(bool IsSigned, SDValue N0, SDValue N1) const {
    return computeOverflowForSub(IsSigned, N0, N1) == OFK_Never;
  }

  /// Determine if the result of the signed mul of 2 nodes can overflow.
  OverflowKind computeOverflowForSignedMul(SDValue N0, SDValue N1) const;

  /// Determine if the result of the unsigned mul of 2 nodes can overflow.
  OverflowKind computeOverflowForUnsignedMul(SDValue N0, SDValue N1) const;

  /// Determine if the result of the mul of 2 nodes can overflow.
  OverflowKind computeOverflowForMul(bool IsSigned, SDValue N0,
                                     SDValue N1) const {
    return IsSigned ? computeOverflowForSignedMul(N0, N1)
                    : computeOverflowForUnsignedMul(N0, N1);
  }

  /// Determine if the result of the mul of 2 nodes can never overflow.
  bool willNotOverflowMul(bool IsSigned, SDValue N0, SDValue N1) const {
    return computeOverflowForMul(IsSigned, N0, N1) == OFK_Never;
  }

  /// Test if the given value is known to have exactly one bit set. This differs
  /// from computeKnownBits in that it doesn't necessarily determine which bit
  /// is set.
  bool isKnownToBeAPowerOfTwo(SDValue Val, unsigned Depth = 0) const;

  /// Test if the given _fp_ value is known to be an integer power-of-2, either
  /// positive or negative.
  bool isKnownToBeAPowerOfTwoFP(SDValue Val, unsigned Depth = 0) const;

  /// Return the number of times the sign bit of the register is replicated into
  /// the other bits. We know that at least 1 bit is always equal to the sign
  /// bit (itself), but other cases can give us information. For example,
  /// immediately after an "SRA X, 2", we know that the top 3 bits are all equal
  /// to each other, so we return 3. Targets can implement the
  /// ComputeNumSignBitsForTarget method in the TargetLowering class to allow
  /// target nodes to be understood.
  unsigned ComputeNumSignBits(SDValue Op, unsigned Depth = 0) const;

  /// Return the number of times the sign bit of the register is replicated into
  /// the other bits. We know that at least 1 bit is always equal to the sign
  /// bit (itself), but other cases can give us information. For example,
  /// immediately after an "SRA X, 2", we know that the top 3 bits are all equal
  /// to each other, so we return 3. The DemandedElts argument allows
  /// us to only collect the minimum sign bits of the requested vector elements.
  /// Targets can implement the ComputeNumSignBitsForTarget method in the
  /// TargetLowering class to allow target nodes to be understood.
  unsigned ComputeNumSignBits(SDValue Op, const APInt &DemandedElts,
                              unsigned Depth = 0) const;

  /// Get the upper bound on bit size for this Value \p Op as a signed integer.
  /// i.e.  x == sext(trunc(x to MaxSignedBits) to bitwidth(x)).
  /// Similar to the APInt::getSignificantBits function.
  /// Helper wrapper to ComputeNumSignBits.
  unsigned ComputeMaxSignificantBits(SDValue Op, unsigned Depth = 0) const;

  /// Get the upper bound on bit size for this Value \p Op as a signed integer.
  /// i.e.  x == sext(trunc(x to MaxSignedBits) to bitwidth(x)).
  /// Similar to the APInt::getSignificantBits function.
  /// Helper wrapper to ComputeNumSignBits.
  unsigned ComputeMaxSignificantBits(SDValue Op, const APInt &DemandedElts,
                                     unsigned Depth = 0) const;

  /// Return true if this function can prove that \p Op is never poison
  /// and, if \p PoisonOnly is false, does not have undef bits.
  bool isGuaranteedNotToBeUndefOrPoison(SDValue Op, bool PoisonOnly = false,
                                        unsigned Depth = 0) const;

  /// Return true if this function can prove that \p Op is never poison
  /// and, if \p PoisonOnly is false, does not have undef bits. The DemandedElts
  /// argument limits the check to the requested vector elements.
  bool isGuaranteedNotToBeUndefOrPoison(SDValue Op, const APInt &DemandedElts,
                                        bool PoisonOnly = false,
                                        unsigned Depth = 0) const;

  /// Return true if this function can prove that \p Op is never poison.
  bool isGuaranteedNotToBePoison(SDValue Op, unsigned Depth = 0) const {
    return isGuaranteedNotToBeUndefOrPoison(Op, /*PoisonOnly*/ true, Depth);
  }

  /// Return true if this function can prove that \p Op is never poison. The
  /// DemandedElts argument limits the check to the requested vector elements.
  bool isGuaranteedNotToBePoison(SDValue Op, const APInt &DemandedElts,
                                 unsigned Depth = 0) const {
    return isGuaranteedNotToBeUndefOrPoison(Op, DemandedElts,
                                            /*PoisonOnly*/ true, Depth);
  }

  /// Return true if Op can create undef or poison from non-undef & non-poison
  /// operands. The DemandedElts argument limits the check to the requested
  /// vector elements.
  ///
  /// \p ConsiderFlags controls whether poison producing flags on the
  /// instruction are considered.  This can be used to see if the instruction
  /// could still introduce undef or poison even without poison generating flags
  /// which might be on the instruction.  (i.e. could the result of
  /// Op->dropPoisonGeneratingFlags() still create poison or undef)
  bool canCreateUndefOrPoison(SDValue Op, const APInt &DemandedElts,
                              bool PoisonOnly = false,
                              bool ConsiderFlags = true,
                              unsigned Depth = 0) const;

  /// Return true if Op can create undef or poison from non-undef & non-poison
  /// operands.
  ///
  /// \p ConsiderFlags controls whether poison producing flags on the
  /// instruction are considered.  This can be used to see if the instruction
  /// could still introduce undef or poison even without poison generating flags
  /// which might be on the instruction.  (i.e. could the result of
  /// Op->dropPoisonGeneratingFlags() still create poison or undef)
  bool canCreateUndefOrPoison(SDValue Op, bool PoisonOnly = false,
                              bool ConsiderFlags = true,
                              unsigned Depth = 0) const;

  /// Return true if the specified operand is an ISD::OR or ISD::XOR node
  /// that can be treated as an ISD::ADD node.
  /// or(x,y) == add(x,y) iff haveNoCommonBitsSet(x,y)
  /// xor(x,y) == add(x,y) iff isMinSignedConstant(y) && !NoWrap
  /// If \p NoWrap is true, this will not match ISD::XOR.
  bool isADDLike(SDValue Op, bool NoWrap = false) const;

  /// Return true if the specified operand is an ISD::ADD with a ConstantSDNode
  /// on the right-hand side, or if it is an ISD::OR with a ConstantSDNode that
  /// is guaranteed to have the same semantics as an ADD. This handles the
  /// equivalence:
  ///     X|Cst == X+Cst iff X&Cst = 0.
  bool isBaseWithConstantOffset(SDValue Op) const;

  /// Test whether the given SDValue (or all elements of it, if it is a
  /// vector) is known to never be NaN. If \p SNaN is true, returns if \p Op is
  /// known to never be a signaling NaN (it may still be a qNaN).
  bool isKnownNeverNaN(SDValue Op, bool SNaN = false, unsigned Depth = 0) const;

  /// \returns true if \p Op is known to never be a signaling NaN.
  bool isKnownNeverSNaN(SDValue Op, unsigned Depth = 0) const {
    return isKnownNeverNaN(Op, true, Depth);
  }

  /// Test whether the given floating point SDValue is known to never be
  /// positive or negative zero.
  bool isKnownNeverZeroFloat(SDValue Op) const;

  /// Test whether the given SDValue is known to contain non-zero value(s).
  bool isKnownNeverZero(SDValue Op, unsigned Depth = 0) const;

  /// Test whether the given float value is known to be positive. +0.0, +inf and
  /// +nan are considered positive, -0.0, -inf and -nan are not.
  bool cannotBeOrderedNegativeFP(SDValue Op) const;

  /// Test whether two SDValues are known to compare equal. This
  /// is true if they are the same value, or if one is negative zero and the
  /// other positive zero.
  bool isEqualTo(SDValue A, SDValue B) const;

  /// Return true if A and B have no common bits set. As an example, this can
  /// allow an 'add' to be transformed into an 'or'.
  bool haveNoCommonBitsSet(SDValue A, SDValue B) const;

  /// Test whether \p V has a splatted value for all the demanded elements.
  ///
  /// On success \p UndefElts will indicate the elements that have UNDEF
  /// values instead of the splat value, this is only guaranteed to be correct
  /// for \p DemandedElts.
  ///
  /// NOTE: The function will return true for a demanded splat of UNDEF values.
  bool isSplatValue(SDValue V, const APInt &DemandedElts, APInt &UndefElts,
                    unsigned Depth = 0) const;

  /// Test whether \p V has a splatted value.
  bool isSplatValue(SDValue V, bool AllowUndefs = false) const;

  /// If V is a splatted value, return the source vector and its splat index.
  SDValue getSplatSourceVector(SDValue V, int &SplatIndex);

  /// If V is a splat vector, return its scalar source operand by extracting
  /// that element from the source vector. If LegalTypes is true, this method
  /// may only return a legally-typed splat value. If it cannot legalize the
  /// splatted value it will return SDValue().
  SDValue getSplatValue(SDValue V, bool LegalTypes = false);

  /// If a SHL/SRA/SRL node \p V has shift amounts that are all less than the
  /// element bit-width of the shift node, return the valid constant range.
  std::optional<ConstantRange>
  getValidShiftAmountRange(SDValue V, const APInt &DemandedElts,
                           unsigned Depth) const;

  /// If a SHL/SRA/SRL node \p V has a uniform shift amount
  /// that is less than the element bit-width of the shift node, return it.
  std::optional<uint64_t> getValidShiftAmount(SDValue V,
                                              const APInt &DemandedElts,
                                              unsigned Depth = 0) const;

  /// If a SHL/SRA/SRL node \p V has a uniform shift amount
  /// that is less than the element bit-width of the shift node, return it.
  std::optional<uint64_t> getValidShiftAmount(SDValue V,
                                              unsigned Depth = 0) const;

  /// If a SHL/SRA/SRL node \p V has shift amounts that are all less than the
  /// element bit-width of the shift node, return the minimum possible value.
  std::optional<uint64_t> getValidMinimumShiftAmount(SDValue V,
                                                     const APInt &DemandedElts,
                                                     unsigned Depth = 0) const;

  /// If a SHL/SRA/SRL node \p V has shift amounts that are all less than the
  /// element bit-width of the shift node, return the minimum possible value.
  std::optional<uint64_t> getValidMinimumShiftAmount(SDValue V,
                                                     unsigned Depth = 0) const;

  /// If a SHL/SRA/SRL node \p V has shift amounts that are all less than the
  /// element bit-width of the shift node, return the maximum possible value.
  std::optional<uint64_t> getValidMaximumShiftAmount(SDValue V,
                                                     const APInt &DemandedElts,
                                                     unsigned Depth = 0) const;

  /// If a SHL/SRA/SRL node \p V has shift amounts that are all less than the
  /// element bit-width of the shift node, return the maximum possible value.
  std::optional<uint64_t> getValidMaximumShiftAmount(SDValue V,
                                                     unsigned Depth = 0) const;

  /// Match a binop + shuffle pyramid that represents a horizontal reduction
  /// over the elements of a vector starting from the EXTRACT_VECTOR_ELT node /p
  /// Extract. The reduction must use one of the opcodes listed in /p
  /// CandidateBinOps and on success /p BinOp will contain the matching opcode.
  /// Returns the vector that is being reduced on, or SDValue() if a reduction
  /// was not matched. If \p AllowPartials is set then in the case of a
  /// reduction pattern that only matches the first few stages, the extracted
  /// subvector of the start of the reduction is returned.
  SDValue matchBinOpReduction(SDNode *Extract, ISD::NodeType &BinOp,
                              ArrayRef<ISD::NodeType> CandidateBinOps,
                              bool AllowPartials = false);

  /// Utility function used by legalize and lowering to
  /// "unroll" a vector operation by splitting out the scalars and operating
  /// on each element individually.  If the ResNE is 0, fully unroll the vector
  /// op. If ResNE is less than the width of the vector op, unroll up to ResNE.
  /// If the  ResNE is greater than the width of the vector op, unroll the
  /// vector op and fill the end of the resulting vector with UNDEFS.
  SDValue UnrollVectorOp(SDNode *N, unsigned ResNE = 0);

  /// Like UnrollVectorOp(), but for the [US](ADD|SUB|MUL)O family of opcodes.
  /// This is a separate function because those opcodes have two results.
  std::pair<SDValue, SDValue> UnrollVectorOverflowOp(SDNode *N,
                                                     unsigned ResNE = 0);

  /// Return true if loads are next to each other and can be
  /// merged. Check that both are nonvolatile and if LD is loading
  /// 'Bytes' bytes from a location that is 'Dist' units away from the
  /// location that the 'Base' load is loading from.
  bool areNonVolatileConsecutiveLoads(LoadSDNode *LD, LoadSDNode *Base,
                                      unsigned Bytes, int Dist) const;

  /// Infer alignment of a load / store address. Return std::nullopt if it
  /// cannot be inferred.
  MaybeAlign InferPtrAlign(SDValue Ptr) const;

  /// Split the scalar node with EXTRACT_ELEMENT using the provided VTs and
  /// return the low/high part.
  std::pair<SDValue, SDValue> SplitScalar(const SDValue &N, const SDLoc &DL,
                                          const EVT &LoVT, const EVT &HiVT);

  /// Compute the VTs needed for the low/hi parts of a type
  /// which is split (or expanded) into two not necessarily identical pieces.
  std::pair<EVT, EVT> GetSplitDestVTs(const EVT &VT) const;

  /// Compute the VTs needed for the low/hi parts of a type, dependent on an
  /// enveloping VT that has been split into two identical pieces. Sets the
  /// HisIsEmpty flag when hi type has zero storage size.
  std::pair<EVT, EVT> GetDependentSplitDestVTs(const EVT &VT, const EVT &EnvVT,
                                               bool *HiIsEmpty) const;

  /// Split the vector with EXTRACT_SUBVECTOR using the provided
  /// VTs and return the low/high part.
  std::pair<SDValue, SDValue> SplitVector(const SDValue &N, const SDLoc &DL,
                                          const EVT &LoVT, const EVT &HiVT);

  /// Split the vector with EXTRACT_SUBVECTOR and return the low/high part.
  std::pair<SDValue, SDValue> SplitVector(const SDValue &N, const SDLoc &DL) {
    EVT LoVT, HiVT;
    std::tie(LoVT, HiVT) = GetSplitDestVTs(N.getValueType());
    return SplitVector(N, DL, LoVT, HiVT);
  }

  /// Split the explicit vector length parameter of a VP operation.
  std::pair<SDValue, SDValue> SplitEVL(SDValue N, EVT VecVT, const SDLoc &DL);

  /// Split the node's operand with EXTRACT_SUBVECTOR and
  /// return the low/high part.
  std::pair<SDValue, SDValue> SplitVectorOperand(const SDNode *N, unsigned OpNo)
  {
    return SplitVector(N->getOperand(OpNo), SDLoc(N));
  }

  /// Widen the vector up to the next power of two using INSERT_SUBVECTOR.
  SDValue WidenVector(const SDValue &N, const SDLoc &DL);

  /// Append the extracted elements from Start to Count out of the vector Op in
  /// Args. If Count is 0, all of the elements will be extracted. The extracted
  /// elements will have type EVT if it is provided, and otherwise their type
  /// will be Op's element type.
  void ExtractVectorElements(SDValue Op, SmallVectorImpl<SDValue> &Args,
                             unsigned Start = 0, unsigned Count = 0,
                             EVT EltVT = EVT());

  /// Compute the default alignment value for the given type.
  Align getEVTAlign(EVT MemoryVT) const;

  /// Test whether the given value is a constant int or similar node.
  SDNode *isConstantIntBuildVectorOrConstantInt(SDValue N) const;

  /// Test whether the given value is a constant FP or similar node.
  SDNode *isConstantFPBuildVectorOrConstantFP(SDValue N) const ;

  /// \returns true if \p N is any kind of constant or build_vector of
  /// constants, int or float. If a vector, it may not necessarily be a splat.
  inline bool isConstantValueOfAnyType(SDValue N) const {
    return isConstantIntBuildVectorOrConstantInt(N) ||
           isConstantFPBuildVectorOrConstantFP(N);
  }

  /// Set CallSiteInfo to be associated with Node.
  void addCallSiteInfo(const SDNode *Node, CallSiteInfo &&CallInfo) {
    SDEI[Node].CSInfo = std::move(CallInfo);
  }
  /// Return CallSiteInfo associated with Node, or a default if none exists.
  CallSiteInfo getCallSiteInfo(const SDNode *Node) {
    auto I = SDEI.find(Node);
    return I != SDEI.end() ? std::move(I->second).CSInfo : CallSiteInfo();
  }
  /// Set HeapAllocSite to be associated with Node.
  void addHeapAllocSite(const SDNode *Node, MDNode *MD) {
    SDEI[Node].HeapAllocSite = MD;
  }
  /// Return HeapAllocSite associated with Node, or nullptr if none exists.
  MDNode *getHeapAllocSite(const SDNode *Node) const {
    auto I = SDEI.find(Node);
    return I != SDEI.end() ? I->second.HeapAllocSite : nullptr;
  }
  /// Set PCSections to be associated with Node.
  void addPCSections(const SDNode *Node, MDNode *MD) {
    SDEI[Node].PCSections = MD;
  }
  /// Set MMRAMetadata to be associated with Node.
  void addMMRAMetadata(const SDNode *Node, MDNode *MMRA) {
    SDEI[Node].MMRA = MMRA;
  }
  /// Return PCSections associated with Node, or nullptr if none exists.
  MDNode *getPCSections(const SDNode *Node) const {
    auto It = SDEI.find(Node);
    return It != SDEI.end() ? It->second.PCSections : nullptr;
  }
  /// Return the MMRA MDNode associated with Node, or nullptr if none
  /// exists.
  MDNode *getMMRAMetadata(const SDNode *Node) const {
    auto It = SDEI.find(Node);
    return It != SDEI.end() ? It->second.MMRA : nullptr;
  }
  /// Set NoMergeSiteInfo to be associated with Node if NoMerge is true.
  void addNoMergeSiteInfo(const SDNode *Node, bool NoMerge) {
    if (NoMerge)
      SDEI[Node].NoMerge = NoMerge;
  }
  /// Return NoMerge info associated with Node.
  bool getNoMergeSiteInfo(const SDNode *Node) const {
    auto I = SDEI.find(Node);
    return I != SDEI.end() ? I->second.NoMerge : false;
  }

  /// Copy extra info associated with one node to another.
  void copyExtraInfo(SDNode *From, SDNode *To);

  /// Return the current function's default denormal handling kind for the given
  /// floating point type.
  DenormalMode getDenormalMode(EVT VT) const {
    return MF->getDenormalMode(EVTToAPFloatSemantics(VT));
  }

  bool shouldOptForSize() const;

  /// Get the (commutative) neutral element for the given opcode, if it exists.
  SDValue getNeutralElement(unsigned Opcode, const SDLoc &DL, EVT VT,
                            SDNodeFlags Flags);

  /// Some opcodes may create immediate undefined behavior when used with some
  /// values (integer division-by-zero for example). Therefore, these operations
  /// are not generally safe to move around or change.
  bool isSafeToSpeculativelyExecute(unsigned Opcode) const {
    switch (Opcode) {
    case ISD::SDIV:
    case ISD::SREM:
    case ISD::SDIVREM:
    case ISD::UDIV:
    case ISD::UREM:
    case ISD::UDIVREM:
      return false;
    default:
      return true;
    }
  }

  /// Check if the provided node is save to speculatively executed given its
  /// current arguments. So, while `udiv` the opcode is not safe to
  /// speculatively execute, a given `udiv` node may be if the denominator is
  /// known nonzero.
  bool isSafeToSpeculativelyExecuteNode(const SDNode *N) const {
    switch (N->getOpcode()) {
    case ISD::UDIV:
      return isKnownNeverZero(N->getOperand(1));
    default:
      return isSafeToSpeculativelyExecute(N->getOpcode());
    }
  }

  SDValue makeStateFunctionCall(unsigned LibFunc, SDValue Ptr, SDValue InChain,
                                const SDLoc &DLoc);

private:
  void InsertNode(SDNode *N);
  bool RemoveNodeFromCSEMaps(SDNode *N);
  void AddModifiedNodeToCSEMaps(SDNode *N);
  SDNode *FindModifiedNodeSlot(SDNode *N, SDValue Op, void *&InsertPos);
  SDNode *FindModifiedNodeSlot(SDNode *N, SDValue Op1, SDValue Op2,
                               void *&InsertPos);
  SDNode *FindModifiedNodeSlot(SDNode *N, ArrayRef<SDValue> Ops,
                               void *&InsertPos);
  SDNode *UpdateSDLocOnMergeSDNode(SDNode *N, const SDLoc &loc);

  void DeleteNodeNotInCSEMaps(SDNode *N);
  void DeallocateNode(SDNode *N);

  void allnodes_clear();

  /// Look up the node specified by ID in CSEMap.  If it exists, return it.  If
  /// not, return the insertion token that will make insertion faster.  This
  /// overload is for nodes other than Constant or ConstantFP, use the other one
  /// for those.
  SDNode *FindNodeOrInsertPos(const FoldingSetNodeID &ID, void *&InsertPos);

  /// Look up the node specified by ID in CSEMap.  If it exists, return it.  If
  /// not, return the insertion token that will make insertion faster.  Performs
  /// additional processing for constant nodes.
  SDNode *FindNodeOrInsertPos(const FoldingSetNodeID &ID, const SDLoc &DL,
                              void *&InsertPos);

  /// Maps to auto-CSE operations.
  std::vector<CondCodeSDNode*> CondCodeNodes;

  std::vector<SDNode*> ValueTypeNodes;
  std::map<EVT, SDNode*, EVT::compareRawBits> ExtendedValueTypeNodes;
  StringMap<SDNode*> ExternalSymbols;

  std::map<std::pair<std::string, unsigned>, SDNode *> TargetExternalSymbols;
  DenseMap<MCSymbol *, SDNode *> MCSymbols;

  FlagInserter *Inserter = nullptr;
};

template <> struct GraphTraits<SelectionDAG*> : public GraphTraits<SDNode*> {
  using nodes_iterator = pointer_iterator<SelectionDAG::allnodes_iterator>;

  static nodes_iterator nodes_begin(SelectionDAG *G) {
    return nodes_iterator(G->allnodes_begin());
  }

  static nodes_iterator nodes_end(SelectionDAG *G) {
    return nodes_iterator(G->allnodes_end());
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SELECTIONDAG_H
