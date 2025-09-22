//===- Cloning.h - Clone various parts of LLVM programs ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines various functions that are used to clone chunks of LLVM
// code for various purposes.  This varies from copying whole modules into new
// modules, to cloning functions with different arguments, to inlining
// functions, to copying basic blocks to support loop unrolling or superblock
// formation, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CLONING_H
#define LLVM_TRANSFORMS_UTILS_CLONING_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <functional>
#include <memory>
#include <vector>

namespace llvm {

class AAResults;
class AllocaInst;
class BasicBlock;
class BlockFrequencyInfo;
class DebugInfoFinder;
class DominatorTree;
class Function;
class Instruction;
class Loop;
class LoopInfo;
class Module;
class ProfileSummaryInfo;
class ReturnInst;
class DomTreeUpdater;

/// Return an exact copy of the specified module
std::unique_ptr<Module> CloneModule(const Module &M);
std::unique_ptr<Module> CloneModule(const Module &M, ValueToValueMapTy &VMap);

/// Return a copy of the specified module. The ShouldCloneDefinition function
/// controls whether a specific GlobalValue's definition is cloned. If the
/// function returns false, the module copy will contain an external reference
/// in place of the global definition.
std::unique_ptr<Module>
CloneModule(const Module &M, ValueToValueMapTy &VMap,
            function_ref<bool(const GlobalValue *)> ShouldCloneDefinition);

/// This struct can be used to capture information about code
/// being cloned, while it is being cloned.
struct ClonedCodeInfo {
  /// This is set to true if the cloned code contains a normal call instruction.
  bool ContainsCalls = false;

  /// This is set to true if there is memprof related metadata (memprof or
  /// callsite metadata) in the cloned code.
  bool ContainsMemProfMetadata = false;

  /// This is set to true if the cloned code contains a 'dynamic' alloca.
  /// Dynamic allocas are allocas that are either not in the entry block or they
  /// are in the entry block but are not a constant size.
  bool ContainsDynamicAllocas = false;

  /// All cloned call sites that have operand bundles attached are appended to
  /// this vector.  This vector may contain nulls or undefs if some of the
  /// originally inserted callsites were DCE'ed after they were cloned.
  std::vector<WeakTrackingVH> OperandBundleCallSites;

  /// Like VMap, but maps only unsimplified instructions. Values in the map
  /// may be dangling, it is only intended to be used via isSimplified(), to
  /// check whether the main VMap mapping involves simplification or not.
  DenseMap<const Value *, const Value *> OrigVMap;

  ClonedCodeInfo() = default;

  bool isSimplified(const Value *From, const Value *To) const {
    return OrigVMap.lookup(From) != To;
  }
};

/// Return a copy of the specified basic block, but without
/// embedding the block into a particular function.  The block returned is an
/// exact copy of the specified basic block, without any remapping having been
/// performed.  Because of this, this is only suitable for applications where
/// the basic block will be inserted into the same function that it was cloned
/// from (loop unrolling would use this, for example).
///
/// Also, note that this function makes a direct copy of the basic block, and
/// can thus produce illegal LLVM code.  In particular, it will copy any PHI
/// nodes from the original block, even though there are no predecessors for the
/// newly cloned block (thus, phi nodes will have to be updated).  Also, this
/// block will branch to the old successors of the original block: these
/// successors will have to have any PHI nodes updated to account for the new
/// incoming edges.
///
/// The correlation between instructions in the source and result basic blocks
/// is recorded in the VMap map.
///
/// If you have a particular suffix you'd like to use to add to any cloned
/// names, specify it as the optional third parameter.
///
/// If you would like the basic block to be auto-inserted into the end of a
/// function, you can specify it as the optional fourth parameter.
///
/// If you would like to collect additional information about the cloned
/// function, you can specify a ClonedCodeInfo object with the optional fifth
/// parameter.
BasicBlock *CloneBasicBlock(const BasicBlock *BB, ValueToValueMapTy &VMap,
                            const Twine &NameSuffix = "", Function *F = nullptr,
                            ClonedCodeInfo *CodeInfo = nullptr,
                            DebugInfoFinder *DIFinder = nullptr);

/// Return a copy of the specified function and add it to that
/// function's module.  Also, any references specified in the VMap are changed
/// to refer to their mapped value instead of the original one.  If any of the
/// arguments to the function are in the VMap, the arguments are deleted from
/// the resultant function.  The VMap is updated to include mappings from all of
/// the instructions and basicblocks in the function from their old to new
/// values.  The final argument captures information about the cloned code if
/// non-null.
///
/// \pre VMap contains no non-identity GlobalValue mappings.
///
Function *CloneFunction(Function *F, ValueToValueMapTy &VMap,
                        ClonedCodeInfo *CodeInfo = nullptr);

enum class CloneFunctionChangeType {
  LocalChangesOnly,
  GlobalChanges,
  DifferentModule,
  ClonedModule,
};

/// Clone OldFunc into NewFunc, transforming the old arguments into references
/// to VMap values.  Note that if NewFunc already has basic blocks, the ones
/// cloned into it will be added to the end of the function.  This function
/// fills in a list of return instructions, and can optionally remap types
/// and/or append the specified suffix to all values cloned.
///
/// If \p Changes is \a CloneFunctionChangeType::LocalChangesOnly, VMap is
/// required to contain no non-identity GlobalValue mappings. Otherwise,
/// referenced metadata will be cloned.
///
/// If \p Changes is less than \a CloneFunctionChangeType::DifferentModule
/// indicating cloning into the same module (even if it's LocalChangesOnly), if
/// debug info metadata transitively references a \a DISubprogram, it will be
/// cloned, effectively upgrading \p Changes to GlobalChanges while suppressing
/// cloning of types and compile units.
///
/// If \p Changes is \a CloneFunctionChangeType::DifferentModule, the new
/// module's \c !llvm.dbg.cu will get updated with any newly created compile
/// units. (\a CloneFunctionChangeType::ClonedModule leaves that work for the
/// caller.)
///
/// FIXME: Consider simplifying this function by splitting out \a
/// CloneFunctionMetadataInto() and expecting / updating callers to call it
/// first when / how it's needed.
void CloneFunctionInto(Function *NewFunc, const Function *OldFunc,
                       ValueToValueMapTy &VMap, CloneFunctionChangeType Changes,
                       SmallVectorImpl<ReturnInst *> &Returns,
                       const char *NameSuffix = "",
                       ClonedCodeInfo *CodeInfo = nullptr,
                       ValueMapTypeRemapper *TypeMapper = nullptr,
                       ValueMaterializer *Materializer = nullptr);

void CloneAndPruneIntoFromInst(Function *NewFunc, const Function *OldFunc,
                               const Instruction *StartingInst,
                               ValueToValueMapTy &VMap, bool ModuleLevelChanges,
                               SmallVectorImpl<ReturnInst *> &Returns,
                               const char *NameSuffix = "",
                               ClonedCodeInfo *CodeInfo = nullptr);

/// This works exactly like CloneFunctionInto,
/// except that it does some simple constant prop and DCE on the fly.  The
/// effect of this is to copy significantly less code in cases where (for
/// example) a function call with constant arguments is inlined, and those
/// constant arguments cause a significant amount of code in the callee to be
/// dead.  Since this doesn't produce an exactly copy of the input, it can't be
/// used for things like CloneFunction or CloneModule.
///
/// If ModuleLevelChanges is false, VMap contains no non-identity GlobalValue
/// mappings.
///
void CloneAndPruneFunctionInto(Function *NewFunc, const Function *OldFunc,
                               ValueToValueMapTy &VMap, bool ModuleLevelChanges,
                               SmallVectorImpl<ReturnInst*> &Returns,
                               const char *NameSuffix = "",
                               ClonedCodeInfo *CodeInfo = nullptr);

/// This class captures the data input to the InlineFunction call, and records
/// the auxiliary results produced by it.
class InlineFunctionInfo {
public:
  explicit InlineFunctionInfo(
      function_ref<AssumptionCache &(Function &)> GetAssumptionCache = nullptr,
      ProfileSummaryInfo *PSI = nullptr,
      BlockFrequencyInfo *CallerBFI = nullptr,
      BlockFrequencyInfo *CalleeBFI = nullptr, bool UpdateProfile = true)
      : GetAssumptionCache(GetAssumptionCache), PSI(PSI), CallerBFI(CallerBFI),
        CalleeBFI(CalleeBFI), UpdateProfile(UpdateProfile) {}

  /// If non-null, InlineFunction will update the callgraph to reflect the
  /// changes it makes.
  function_ref<AssumptionCache &(Function &)> GetAssumptionCache;
  ProfileSummaryInfo *PSI;
  BlockFrequencyInfo *CallerBFI, *CalleeBFI;

  /// InlineFunction fills this in with all static allocas that get copied into
  /// the caller.
  SmallVector<AllocaInst *, 4> StaticAllocas;

  /// InlineFunction fills this in with callsites that were inlined from the
  /// callee. This is only filled in if CG is non-null.
  SmallVector<WeakTrackingVH, 8> InlinedCalls;

  /// All of the new call sites inlined into the caller.
  ///
  /// 'InlineFunction' fills this in by scanning the inlined instructions, and
  /// only if CG is null. If CG is non-null, instead the value handle
  /// `InlinedCalls` above is used.
  SmallVector<CallBase *, 8> InlinedCallSites;

  /// Update profile for callee as well as cloned version. We need to do this
  /// for regular inlining, but not for inlining from sample profile loader.
  bool UpdateProfile;

  void reset() {
    StaticAllocas.clear();
    InlinedCalls.clear();
    InlinedCallSites.clear();
  }
};

/// This function inlines the called function into the basic
/// block of the caller.  This returns false if it is not possible to inline
/// this call.  The program is still in a well defined state if this occurs
/// though.
///
/// Note that this only does one level of inlining.  For example, if the
/// instruction 'call B' is inlined, and 'B' calls 'C', then the call to 'C' now
/// exists in the instruction stream.  Similarly this will inline a recursive
/// function by one level.
///
/// Note that while this routine is allowed to cleanup and optimize the
/// *inlined* code to minimize the actual inserted code, it must not delete
/// code in the caller as users of this routine may have pointers to
/// instructions in the caller that need to remain stable.
///
/// If ForwardVarArgsTo is passed, inlining a function with varargs is allowed
/// and all varargs at the callsite will be passed to any calls to
/// ForwardVarArgsTo. The caller of InlineFunction has to make sure any varargs
/// are only used by ForwardVarArgsTo.
///
/// The callee's function attributes are merged into the callers' if
/// MergeAttributes is set to true.
InlineResult InlineFunction(CallBase &CB, InlineFunctionInfo &IFI,
                            bool MergeAttributes = false,
                            AAResults *CalleeAAR = nullptr,
                            bool InsertLifetime = true,
                            Function *ForwardVarArgsTo = nullptr);

/// Clones a loop \p OrigLoop.  Returns the loop and the blocks in \p
/// Blocks.
///
/// Updates LoopInfo and DominatorTree assuming the loop is dominated by block
/// \p LoopDomBB.  Insert the new blocks before block specified in \p Before.
/// Note: Only innermost loops are supported.
Loop *cloneLoopWithPreheader(BasicBlock *Before, BasicBlock *LoopDomBB,
                             Loop *OrigLoop, ValueToValueMapTy &VMap,
                             const Twine &NameSuffix, LoopInfo *LI,
                             DominatorTree *DT,
                             SmallVectorImpl<BasicBlock *> &Blocks);

/// Remaps instructions in \p Blocks using the mapping in \p VMap.
void remapInstructionsInBlocks(ArrayRef<BasicBlock *> Blocks,
                               ValueToValueMapTy &VMap);

/// Split edge between BB and PredBB and duplicate all non-Phi instructions
/// from BB between its beginning and the StopAt instruction into the split
/// block. Phi nodes are not duplicated, but their uses are handled correctly:
/// we replace them with the uses of corresponding Phi inputs. ValueMapping
/// is used to map the original instructions from BB to their newly-created
/// copies. Returns the split block.
BasicBlock *DuplicateInstructionsInSplitBetween(BasicBlock *BB,
                                                BasicBlock *PredBB,
                                                Instruction *StopAt,
                                                ValueToValueMapTy &ValueMapping,
                                                DomTreeUpdater &DTU);

/// Updates profile information by adjusting the entry count by adding
/// EntryDelta then scaling callsite information by the new count divided by the
/// old count. VMap is used during inlinng to also update the new clone
void updateProfileCallee(
    Function *Callee, int64_t EntryDelta,
    const ValueMap<const Value *, WeakTrackingVH> *VMap = nullptr);

/// Find the 'llvm.experimental.noalias.scope.decl' intrinsics in the specified
/// basic blocks and extract their scope. These are candidates for duplication
/// when cloning.
void identifyNoAliasScopesToClone(
    ArrayRef<BasicBlock *> BBs, SmallVectorImpl<MDNode *> &NoAliasDeclScopes);

/// Find the 'llvm.experimental.noalias.scope.decl' intrinsics in the specified
/// instruction range and extract their scope. These are candidates for
/// duplication when cloning.
void identifyNoAliasScopesToClone(
    BasicBlock::iterator Start, BasicBlock::iterator End,
    SmallVectorImpl<MDNode *> &NoAliasDeclScopes);

/// Duplicate the specified list of noalias decl scopes.
/// The 'Ext' string is added as an extension to the name.
/// Afterwards, the ClonedScopes contains the mapping of the original scope
/// MDNode onto the cloned scope.
/// Be aware that the cloned scopes are still part of the original scope domain.
void cloneNoAliasScopes(
    ArrayRef<MDNode *> NoAliasDeclScopes,
    DenseMap<MDNode *, MDNode *> &ClonedScopes,
    StringRef Ext, LLVMContext &Context);

/// Adapt the metadata for the specified instruction according to the
/// provided mapping. This is normally used after cloning an instruction, when
/// some noalias scopes needed to be cloned.
void adaptNoAliasScopes(
    llvm::Instruction *I, const DenseMap<MDNode *, MDNode *> &ClonedScopes,
    LLVMContext &Context);

/// Clone the specified noalias decl scopes. Then adapt all instructions in the
/// NewBlocks basicblocks to the cloned versions.
/// 'Ext' will be added to the duplicate scope names.
void cloneAndAdaptNoAliasScopes(ArrayRef<MDNode *> NoAliasDeclScopes,
                                ArrayRef<BasicBlock *> NewBlocks,
                                LLVMContext &Context, StringRef Ext);

/// Clone the specified noalias decl scopes. Then adapt all instructions in the
/// [IStart, IEnd] (IEnd included !) range to the cloned versions. 'Ext' will be
/// added to the duplicate scope names.
void cloneAndAdaptNoAliasScopes(ArrayRef<MDNode *> NoAliasDeclScopes,
                                Instruction *IStart, Instruction *IEnd,
                                LLVMContext &Context, StringRef Ext);
} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CLONING_H
