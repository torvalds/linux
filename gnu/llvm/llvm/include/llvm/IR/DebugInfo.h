//===- DebugInfo.h - Debug Information Helpers ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a bunch of datatypes that are useful for creating and
// walking debug info in LLVM IR form. They essentially provide wrappers around
// the information in the global variables that's needed when constructing the
// DWARF information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DEBUGINFO_H
#define LLVM_IR_DEBUGINFO_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include <optional>

namespace llvm {

class DbgDeclareInst;
class DbgValueInst;
class DbgVariableIntrinsic;
class DbgVariableRecord;
class Instruction;
class Module;

/// Finds dbg.declare intrinsics declaring local variables as living in the
/// memory that 'V' points to.
TinyPtrVector<DbgDeclareInst *> findDbgDeclares(Value *V);
/// As above, for DVRDeclares.
TinyPtrVector<DbgVariableRecord *> findDVRDeclares(Value *V);

/// Finds the llvm.dbg.value intrinsics describing a value.
void findDbgValues(
    SmallVectorImpl<DbgValueInst *> &DbgValues, Value *V,
    SmallVectorImpl<DbgVariableRecord *> *DbgVariableRecords = nullptr);

/// Finds the debug info intrinsics describing a value.
void findDbgUsers(
    SmallVectorImpl<DbgVariableIntrinsic *> &DbgInsts, Value *V,
    SmallVectorImpl<DbgVariableRecord *> *DbgVariableRecords = nullptr);

/// Find subprogram that is enclosing this scope.
DISubprogram *getDISubprogram(const MDNode *Scope);

/// Produce a DebugLoc to use for each dbg.declare that is promoted to a
/// dbg.value.
DebugLoc getDebugValueLoc(DbgVariableIntrinsic *DII);
DebugLoc getDebugValueLoc(DbgVariableRecord *DVR);

/// Strip debug info in the module if it exists.
///
/// To do this, we remove all calls to the debugger intrinsics and any named
/// metadata for debugging. We also remove debug locations for instructions.
/// Return true if module is modified.
bool StripDebugInfo(Module &M);
bool stripDebugInfo(Function &F);

/// Downgrade the debug info in a module to contain only line table information.
///
/// In order to convert debug info to what -gline-tables-only would have
/// created, this does the following:
///   1) Delete all debug intrinsics.
///   2) Delete all non-CU named metadata debug info nodes.
///   3) Create new DebugLocs for each instruction.
///   4) Create a new CU debug info, and similarly for every metadata node
///      that's reachable from the CU debug info.
///   All debug type metadata nodes are unreachable and garbage collected.
bool stripNonLineTableDebugInfo(Module &M);

/// Update the debug locations contained within the MD_loop metadata attached
/// to the instruction \p I, if one exists. \p Updater is applied to Metadata
/// operand in the MD_loop metadata: the returned value is included in the
/// updated loop metadata node if it is non-null.
void updateLoopMetadataDebugLocations(
    Instruction &I, function_ref<Metadata *(Metadata *)> Updater);

/// Return Debug Info Metadata Version by checking module flags.
unsigned getDebugMetadataVersionFromModule(const Module &M);

/// Utility to find all debug info in a module.
///
/// DebugInfoFinder tries to list all debug info MDNodes used in a module. To
/// list debug info MDNodes used by an instruction, DebugInfoFinder uses
/// processDeclare, processValue and processLocation to handle DbgDeclareInst,
/// DbgValueInst and DbgLoc attached to instructions. processModule will go
/// through all DICompileUnits in llvm.dbg.cu and list debug info MDNodes
/// used by the CUs.
class DebugInfoFinder {
public:
  /// Process entire module and collect debug info anchors.
  void processModule(const Module &M);
  /// Process a single instruction and collect debug info anchors.
  void processInstruction(const Module &M, const Instruction &I);

  /// Process a DILocalVariable.
  void processVariable(const Module &M, const DILocalVariable *DVI);
  /// Process debug info location.
  void processLocation(const Module &M, const DILocation *Loc);
  /// Process a DbgRecord (e.g, treat a DbgVariableRecord like a
  /// DbgVariableIntrinsic).
  void processDbgRecord(const Module &M, const DbgRecord &DR);

  /// Process subprogram.
  void processSubprogram(DISubprogram *SP);

  /// Clear all lists.
  void reset();

private:
  void processCompileUnit(DICompileUnit *CU);
  void processScope(DIScope *Scope);
  void processType(DIType *DT);
  bool addCompileUnit(DICompileUnit *CU);
  bool addGlobalVariable(DIGlobalVariableExpression *DIG);
  bool addScope(DIScope *Scope);
  bool addSubprogram(DISubprogram *SP);
  bool addType(DIType *DT);

public:
  using compile_unit_iterator =
      SmallVectorImpl<DICompileUnit *>::const_iterator;
  using subprogram_iterator = SmallVectorImpl<DISubprogram *>::const_iterator;
  using global_variable_expression_iterator =
      SmallVectorImpl<DIGlobalVariableExpression *>::const_iterator;
  using type_iterator = SmallVectorImpl<DIType *>::const_iterator;
  using scope_iterator = SmallVectorImpl<DIScope *>::const_iterator;

  iterator_range<compile_unit_iterator> compile_units() const {
    return make_range(CUs.begin(), CUs.end());
  }

  iterator_range<subprogram_iterator> subprograms() const {
    return make_range(SPs.begin(), SPs.end());
  }

  iterator_range<global_variable_expression_iterator> global_variables() const {
    return make_range(GVs.begin(), GVs.end());
  }

  iterator_range<type_iterator> types() const {
    return make_range(TYs.begin(), TYs.end());
  }

  iterator_range<scope_iterator> scopes() const {
    return make_range(Scopes.begin(), Scopes.end());
  }

  unsigned compile_unit_count() const { return CUs.size(); }
  unsigned global_variable_count() const { return GVs.size(); }
  unsigned subprogram_count() const { return SPs.size(); }
  unsigned type_count() const { return TYs.size(); }
  unsigned scope_count() const { return Scopes.size(); }

private:
  SmallVector<DICompileUnit *, 8> CUs;
  SmallVector<DISubprogram *, 8> SPs;
  SmallVector<DIGlobalVariableExpression *, 8> GVs;
  SmallVector<DIType *, 8> TYs;
  SmallVector<DIScope *, 8> Scopes;
  SmallPtrSet<const MDNode *, 32> NodesSeen;
};

/// Assignment Tracking (at).
namespace at {
//
// Utilities for enumerating storing instructions from an assignment ID.
//
/// A range of instructions.
using AssignmentInstRange =
    iterator_range<SmallVectorImpl<Instruction *>::iterator>;
/// Return a range of instructions (typically just one) that have \p ID
/// as an attachment.
/// Iterators invalidated by adding or removing DIAssignID metadata to/from any
/// instruction (including by deleting or cloning instructions).
AssignmentInstRange getAssignmentInsts(DIAssignID *ID);
/// Return a range of instructions (typically just one) that perform the
/// assignment that \p DAI encodes.
/// Iterators invalidated by adding or removing DIAssignID metadata to/from any
/// instruction (including by deleting or cloning instructions).
inline AssignmentInstRange getAssignmentInsts(const DbgAssignIntrinsic *DAI) {
  return getAssignmentInsts(DAI->getAssignID());
}

inline AssignmentInstRange getAssignmentInsts(const DbgVariableRecord *DVR) {
  assert(DVR->isDbgAssign() &&
         "Can't get assignment instructions for non-assign DVR!");
  return getAssignmentInsts(DVR->getAssignID());
}

//
// Utilities for enumerating llvm.dbg.assign intrinsic from an assignment ID.
//
/// High level: this is an iterator for llvm.dbg.assign intrinsics.
/// Implementation details: this is a wrapper around Value's User iterator that
/// dereferences to a DbgAssignIntrinsic ptr rather than a User ptr.
class DbgAssignIt
    : public iterator_adaptor_base<DbgAssignIt, Value::user_iterator,
                                   typename std::iterator_traits<
                                       Value::user_iterator>::iterator_category,
                                   DbgAssignIntrinsic *, std::ptrdiff_t,
                                   DbgAssignIntrinsic **,
                                   DbgAssignIntrinsic *&> {
public:
  DbgAssignIt(Value::user_iterator It) : iterator_adaptor_base(It) {}
  DbgAssignIntrinsic *operator*() const { return cast<DbgAssignIntrinsic>(*I); }
};
/// A range of llvm.dbg.assign intrinsics.
using AssignmentMarkerRange = iterator_range<DbgAssignIt>;
/// Return a range of dbg.assign intrinsics which use \ID as an operand.
/// Iterators invalidated by deleting an intrinsic contained in this range.
AssignmentMarkerRange getAssignmentMarkers(DIAssignID *ID);
/// Return a range of dbg.assign intrinsics for which \p Inst performs the
/// assignment they encode.
/// Iterators invalidated by deleting an intrinsic contained in this range.
inline AssignmentMarkerRange getAssignmentMarkers(const Instruction *Inst) {
  if (auto *ID = Inst->getMetadata(LLVMContext::MD_DIAssignID))
    return getAssignmentMarkers(cast<DIAssignID>(ID));
  else
    return make_range(Value::user_iterator(), Value::user_iterator());
}

inline SmallVector<DbgVariableRecord *>
getDVRAssignmentMarkers(const Instruction *Inst) {
  if (auto *ID = Inst->getMetadata(LLVMContext::MD_DIAssignID))
    return cast<DIAssignID>(ID)->getAllDbgVariableRecordUsers();
  return {};
}

/// Delete the llvm.dbg.assign intrinsics linked to \p Inst.
void deleteAssignmentMarkers(const Instruction *Inst);

/// Replace all uses (and attachments) of \p Old with \p New.
void RAUW(DIAssignID *Old, DIAssignID *New);

/// Remove all Assignment Tracking related intrinsics and metadata from \p F.
void deleteAll(Function *F);

/// Calculate the fragment of the variable in \p DAI covered
/// from (Dest + SliceOffsetInBits) to
///   to (Dest + SliceOffsetInBits + SliceSizeInBits)
///
/// Return false if it can't be calculated for any reason.
/// Result is set to nullopt if the intersect equals the variable fragment (or
/// variable size) in DAI.
///
/// Result contains a zero-sized fragment if there's no intersect.
bool calculateFragmentIntersect(
    const DataLayout &DL, const Value *Dest, uint64_t SliceOffsetInBits,
    uint64_t SliceSizeInBits, const DbgAssignIntrinsic *DbgAssign,
    std::optional<DIExpression::FragmentInfo> &Result);
bool calculateFragmentIntersect(
    const DataLayout &DL, const Value *Dest, uint64_t SliceOffsetInBits,
    uint64_t SliceSizeInBits, const DbgVariableRecord *DVRAssign,
    std::optional<DIExpression::FragmentInfo> &Result);

/// Replace DIAssignID uses and attachments with IDs from \p Map.
/// If an ID is unmapped a new ID is generated and added to \p Map.
void remapAssignID(DenseMap<DIAssignID *, DIAssignID *> &Map, Instruction &I);

/// Helper struct for trackAssignments, below. We don't use the similar
/// DebugVariable class because trackAssignments doesn't (yet?) understand
/// partial variables (fragment info) as input and want to make that clear and
/// explicit using types. In addition, eventually we will want to understand
/// expressions that modify the base address too, which a DebugVariable doesn't
/// capture.
struct VarRecord {
  DILocalVariable *Var;
  DILocation *DL;

  VarRecord(DbgVariableIntrinsic *DVI)
      : Var(DVI->getVariable()), DL(getDebugValueLoc(DVI)) {}
  VarRecord(DbgVariableRecord *DVR)
      : Var(DVR->getVariable()), DL(getDebugValueLoc(DVR)) {}
  VarRecord(DILocalVariable *Var, DILocation *DL) : Var(Var), DL(DL) {}
  friend bool operator<(const VarRecord &LHS, const VarRecord &RHS) {
    return std::tie(LHS.Var, LHS.DL) < std::tie(RHS.Var, RHS.DL);
  }
  friend bool operator==(const VarRecord &LHS, const VarRecord &RHS) {
    return std::tie(LHS.Var, LHS.DL) == std::tie(RHS.Var, RHS.DL);
  }
};

} // namespace at

template <> struct DenseMapInfo<at::VarRecord> {
  static inline at::VarRecord getEmptyKey() {
    return at::VarRecord(DenseMapInfo<DILocalVariable *>::getEmptyKey(),
                         DenseMapInfo<DILocation *>::getEmptyKey());
  }

  static inline at::VarRecord getTombstoneKey() {
    return at::VarRecord(DenseMapInfo<DILocalVariable *>::getTombstoneKey(),
                         DenseMapInfo<DILocation *>::getTombstoneKey());
  }

  static unsigned getHashValue(const at::VarRecord &Var) {
    return hash_combine(Var.Var, Var.DL);
  }

  static bool isEqual(const at::VarRecord &A, const at::VarRecord &B) {
    return A == B;
  }
};

namespace at {
/// Map of backing storage to a set of variables that are stored to it.
/// TODO: Backing storage shouldn't be limited to allocas only. Some local
/// variables have their storage allocated by the calling function (addresses
/// passed in with sret & byval parameters).
using StorageToVarsMap =
    DenseMap<const AllocaInst *, SmallSetVector<VarRecord, 2>>;

/// Track assignments to \p Vars between \p Start and \p End.

void trackAssignments(Function::iterator Start, Function::iterator End,
                      const StorageToVarsMap &Vars, const DataLayout &DL,
                      bool DebugPrints = false);

/// Describes properties of a store that has a static size and offset into a
/// some base storage. Used by the getAssignmentInfo functions.
struct AssignmentInfo {
  AllocaInst const *Base;  ///< Base storage.
  uint64_t OffsetInBits;   ///< Offset into Base.
  uint64_t SizeInBits;     ///< Number of bits stored.
  bool StoreToWholeAlloca; ///< SizeInBits equals the size of the base storage.

  AssignmentInfo(const DataLayout &DL, AllocaInst const *Base,
                 uint64_t OffsetInBits, uint64_t SizeInBits)
      : Base(Base), OffsetInBits(OffsetInBits), SizeInBits(SizeInBits),
        StoreToWholeAlloca(
            OffsetInBits == 0 &&
            SizeInBits == DL.getTypeSizeInBits(Base->getAllocatedType())) {}
};

std::optional<AssignmentInfo> getAssignmentInfo(const DataLayout &DL,
                                                const MemIntrinsic *I);
std::optional<AssignmentInfo> getAssignmentInfo(const DataLayout &DL,
                                                const StoreInst *SI);
std::optional<AssignmentInfo> getAssignmentInfo(const DataLayout &DL,
                                                const AllocaInst *AI);

} // end namespace at

/// Convert @llvm.dbg.declare intrinsics into sets of @llvm.dbg.assign
/// intrinsics by treating stores to the dbg.declare'd address as assignments
/// to the variable. Not all kinds of variables are supported yet; those will
/// be left with their dbg.declare intrinsics.
/// The pass sets the debug-info-assignment-tracking module flag to true to
/// indicate assignment tracking has been enabled.
class AssignmentTrackingPass : public PassInfoMixin<AssignmentTrackingPass> {
  /// Note: this method does not set the debug-info-assignment-tracking module
  /// flag.
  bool runOnFunction(Function &F);

public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Return true if assignment tracking is enabled for module \p M.
bool isAssignmentTrackingEnabled(const Module &M);

} // end namespace llvm

#endif // LLVM_IR_DEBUGINFO_H
