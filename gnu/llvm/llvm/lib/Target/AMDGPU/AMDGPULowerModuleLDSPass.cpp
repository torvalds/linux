//===-- AMDGPULowerModuleLDSPass.cpp ------------------------------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass eliminates local data store, LDS, uses from non-kernel functions.
// LDS is contiguous memory allocated per kernel execution.
//
// Background.
//
// The programming model is global variables, or equivalently function local
// static variables, accessible from kernels or other functions. For uses from
// kernels this is straightforward - assign an integer to the kernel for the
// memory required by all the variables combined, allocate them within that.
// For uses from functions there are performance tradeoffs to choose between.
//
// This model means the GPU runtime can specify the amount of memory allocated.
// If this is more than the kernel assumed, the excess can be made available
// using a language specific feature, which IR represents as a variable with
// no initializer. This feature is referred to here as "Dynamic LDS" and is
// lowered slightly differently to the normal case.
//
// Consequences of this GPU feature:
// - memory is limited and exceeding it halts compilation
// - a global accessed by one kernel exists independent of other kernels
// - a global exists independent of simultaneous execution of the same kernel
// - the address of the global may be different from different kernels as they
//   do not alias, which permits only allocating variables they use
// - if the address is allowed to differ, functions need help to find it
//
// Uses from kernels are implemented here by grouping them in a per-kernel
// struct instance. This duplicates the variables, accurately modelling their
// aliasing properties relative to a single global representation. It also
// permits control over alignment via padding.
//
// Uses from functions are more complicated and the primary purpose of this
// IR pass. Several different lowering are chosen between to meet requirements
// to avoid allocating any LDS where it is not necessary, as that impacts
// occupancy and may fail the compilation, while not imposing overhead on a
// feature whose primary advantage over global memory is performance. The basic
// design goal is to avoid one kernel imposing overhead on another.
//
// Implementation.
//
// LDS variables with constant annotation or non-undef initializer are passed
// through unchanged for simplification or error diagnostics in later passes.
// Non-undef initializers are not yet implemented for LDS.
//
// LDS variables that are always allocated at the same address can be found
// by lookup at that address. Otherwise runtime information/cost is required.
//
// The simplest strategy possible is to group all LDS variables in a single
// struct and allocate that struct in every kernel such that the original
// variables are always at the same address. LDS is however a limited resource
// so this strategy is unusable in practice. It is not implemented here.
//
// Strategy | Precise allocation | Zero runtime cost | General purpose |
//  --------+--------------------+-------------------+-----------------+
//   Module |                 No |               Yes |             Yes |
//    Table |                Yes |                No |             Yes |
//   Kernel |                Yes |               Yes |              No |
//   Hybrid |                Yes |           Partial |             Yes |
//
// "Module" spends LDS memory to save cycles. "Table" spends cycles and global
// memory to save LDS. "Kernel" is as fast as kernel allocation but only works
// for variables that are known reachable from a single kernel. "Hybrid" picks
// between all three. When forced to choose between LDS and cycles we minimise
// LDS use.

// The "module" lowering implemented here finds LDS variables which are used by
// non-kernel functions and creates a new struct with a field for each of those
// LDS variables. Variables that are only used from kernels are excluded.
//
// The "table" lowering implemented here has three components.
// First kernels are assigned a unique integer identifier which is available in
// functions it calls through the intrinsic amdgcn_lds_kernel_id. The integer
// is passed through a specific SGPR, thus works with indirect calls.
// Second, each kernel allocates LDS variables independent of other kernels and
// writes the addresses it chose for each variable into an array in consistent
// order. If the kernel does not allocate a given variable, it writes undef to
// the corresponding array location. These arrays are written to a constant
// table in the order matching the kernel unique integer identifier.
// Third, uses from non-kernel functions are replaced with a table lookup using
// the intrinsic function to find the address of the variable.
//
// "Kernel" lowering is only applicable for variables that are unambiguously
// reachable from exactly one kernel. For those cases, accesses to the variable
// can be lowered to ConstantExpr address of a struct instance specific to that
// one kernel. This is zero cost in space and in compute. It will raise a fatal
// error on any variable that might be reachable from multiple kernels and is
// thus most easily used as part of the hybrid lowering strategy.
//
// Hybrid lowering is a mixture of the above. It uses the zero cost kernel
// lowering where it can. It lowers the variable accessed by the greatest
// number of kernels using the module strategy as that is free for the first
// variable. Any futher variables that can be lowered with the module strategy
// without incurring LDS memory overhead are. The remaining ones are lowered
// via table.
//
// Consequences
// - No heuristics or user controlled magic numbers, hybrid is the right choice
// - Kernels that don't use functions (or have had them all inlined) are not
//   affected by any lowering for kernels that do.
// - Kernels that don't make indirect function calls are not affected by those
//   that do.
// - Variables which are used by lots of kernels, e.g. those injected by a
//   language runtime in most kernels, are expected to have no overhead
// - Implementations that instantiate templates per-kernel where those templates
//   use LDS are expected to hit the "Kernel" lowering strategy
// - The runtime properties impose a cost in compiler implementation complexity
//
// Dynamic LDS implementation
// Dynamic LDS is lowered similarly to the "table" strategy above and uses the
// same intrinsic to identify which kernel is at the root of the dynamic call
// graph. This relies on the specified behaviour that all dynamic LDS variables
// alias one another, i.e. are at the same address, with respect to a given
// kernel. Therefore this pass creates new dynamic LDS variables for each kernel
// that allocates any dynamic LDS and builds a table of addresses out of those.
// The AMDGPUPromoteAlloca pass skips kernels that use dynamic LDS.
// The corresponding optimisation for "kernel" lowering where the table lookup
// is elided is not implemented.
//
//
// Implementation notes / limitations
// A single LDS global variable represents an instance per kernel that can reach
// said variables. This pass essentially specialises said variables per kernel.
// Handling ConstantExpr during the pass complicated this significantly so now
// all ConstantExpr uses of LDS variables are expanded to instructions. This
// may need amending when implementing non-undef initialisers.
//
// Lowering is split between this IR pass and the back end. This pass chooses
// where given variables should be allocated and marks them with metadata,
// MD_absolute_symbol. The backend places the variables in coincidentally the
// same location and raises a fatal error if something has gone awry. This works
// in practice because the only pass between this one and the backend that
// changes LDS is PromoteAlloca and the changes it makes do not conflict.
//
// Addresses are written to constant global arrays based on the same metadata.
//
// The backend lowers LDS variables in the order of traversal of the function.
// This is at odds with the deterministic layout required. The workaround is to
// allocate the fixed-address variables immediately upon starting the function
// where they can be placed as intended. This requires a means of mapping from
// the function to the variables that it allocates. For the module scope lds,
// this is via metadata indicating whether the variable is not required. If a
// pass deletes that metadata, a fatal error on disagreement with the absolute
// symbol metadata will occur. For kernel scope and dynamic, this is by _name_
// correspondence between the function and the variable. It requires the
// kernel to have a name (which is only a limitation for tests in practice) and
// for nothing to rename the corresponding symbols. This is a hazard if the pass
// is run multiple times during debugging. Alternative schemes considered all
// involve bespoke metadata.
//
// If the name correspondence can be replaced, multiple distinct kernels that
// have the same memory layout can map to the same kernel id (as the address
// itself is handled by the absolute symbol metadata) and that will allow more
// uses of the "kernel" style faster lowering and reduce the size of the lookup
// tables.
//
// There is a test that checks this does not fire for a graphics shader. This
// lowering is expected to work for graphics if the isKernel test is changed.
//
// The current markUsedByKernel is sufficient for PromoteAlloca but is elided
// before codegen. Replacing this with an equivalent intrinsic which lasts until
// shortly after the machine function lowering of LDS would help break the name
// mapping. The other part needed is probably to amend PromoteAlloca to embed
// the LDS variables it creates in the same struct created here. That avoids the
// current hazard where a PromoteAlloca LDS variable might be allocated before
// the kernel scope (and thus error on the address check). Given a new invariant
// that no LDS variables exist outside of the structs managed here, and an
// intrinsic that lasts until after the LDS frame lowering, it should be
// possible to drop the name mapping and fold equivalent memory layouts.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUTargetMachine.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "Utils/AMDGPUMemoryUtils.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/ReplaceConstant.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/OptimizedStructLayout.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <vector>

#include <cstdio>

#define DEBUG_TYPE "amdgpu-lower-module-lds"

using namespace llvm;
using namespace AMDGPU;

namespace {

cl::opt<bool> SuperAlignLDSGlobals(
    "amdgpu-super-align-lds-globals",
    cl::desc("Increase alignment of LDS if it is not on align boundary"),
    cl::init(true), cl::Hidden);

enum class LoweringKind { module, table, kernel, hybrid };
cl::opt<LoweringKind> LoweringKindLoc(
    "amdgpu-lower-module-lds-strategy",
    cl::desc("Specify lowering strategy for function LDS access:"), cl::Hidden,
    cl::init(LoweringKind::hybrid),
    cl::values(
        clEnumValN(LoweringKind::table, "table", "Lower via table lookup"),
        clEnumValN(LoweringKind::module, "module", "Lower via module struct"),
        clEnumValN(
            LoweringKind::kernel, "kernel",
            "Lower variables reachable from one kernel, otherwise abort"),
        clEnumValN(LoweringKind::hybrid, "hybrid",
                   "Lower via mixture of above strategies")));

template <typename T> std::vector<T> sortByName(std::vector<T> &&V) {
  llvm::sort(V.begin(), V.end(), [](const auto *L, const auto *R) {
    return L->getName() < R->getName();
  });
  return {std::move(V)};
}

class AMDGPULowerModuleLDS {
  const AMDGPUTargetMachine &TM;

  static void
  removeLocalVarsFromUsedLists(Module &M,
                               const DenseSet<GlobalVariable *> &LocalVars) {
    // The verifier rejects used lists containing an inttoptr of a constant
    // so remove the variables from these lists before replaceAllUsesWith
    SmallPtrSet<Constant *, 8> LocalVarsSet;
    for (GlobalVariable *LocalVar : LocalVars)
      LocalVarsSet.insert(cast<Constant>(LocalVar->stripPointerCasts()));

    removeFromUsedLists(
        M, [&LocalVarsSet](Constant *C) { return LocalVarsSet.count(C); });

    for (GlobalVariable *LocalVar : LocalVars)
      LocalVar->removeDeadConstantUsers();
  }

  static void markUsedByKernel(Function *Func, GlobalVariable *SGV) {
    // The llvm.amdgcn.module.lds instance is implicitly used by all kernels
    // that might call a function which accesses a field within it. This is
    // presently approximated to 'all kernels' if there are any such functions
    // in the module. This implicit use is redefined as an explicit use here so
    // that later passes, specifically PromoteAlloca, account for the required
    // memory without any knowledge of this transform.

    // An operand bundle on llvm.donothing works because the call instruction
    // survives until after the last pass that needs to account for LDS. It is
    // better than inline asm as the latter survives until the end of codegen. A
    // totally robust solution would be a function with the same semantics as
    // llvm.donothing that takes a pointer to the instance and is lowered to a
    // no-op after LDS is allocated, but that is not presently necessary.

    // This intrinsic is eliminated shortly before instruction selection. It
    // does not suffice to indicate to ISel that a given global which is not
    // immediately used by the kernel must still be allocated by it. An
    // equivalent target specific intrinsic which lasts until immediately after
    // codegen would suffice for that, but one would still need to ensure that
    // the variables are allocated in the anticipated order.
    BasicBlock *Entry = &Func->getEntryBlock();
    IRBuilder<> Builder(Entry, Entry->getFirstNonPHIIt());

    Function *Decl =
        Intrinsic::getDeclaration(Func->getParent(), Intrinsic::donothing, {});

    Value *UseInstance[1] = {
        Builder.CreateConstInBoundsGEP1_32(SGV->getValueType(), SGV, 0)};

    Builder.CreateCall(
        Decl, {}, {OperandBundleDefT<Value *>("ExplicitUse", UseInstance)});
  }

public:
  AMDGPULowerModuleLDS(const AMDGPUTargetMachine &TM_) : TM(TM_) {}

  struct LDSVariableReplacement {
    GlobalVariable *SGV = nullptr;
    DenseMap<GlobalVariable *, Constant *> LDSVarsToConstantGEP;
  };

  // remap from lds global to a constantexpr gep to where it has been moved to
  // for each kernel
  // an array with an element for each kernel containing where the corresponding
  // variable was remapped to

  static Constant *getAddressesOfVariablesInKernel(
      LLVMContext &Ctx, ArrayRef<GlobalVariable *> Variables,
      const DenseMap<GlobalVariable *, Constant *> &LDSVarsToConstantGEP) {
    // Create a ConstantArray containing the address of each Variable within the
    // kernel corresponding to LDSVarsToConstantGEP, or poison if that kernel
    // does not allocate it
    // TODO: Drop the ptrtoint conversion

    Type *I32 = Type::getInt32Ty(Ctx);

    ArrayType *KernelOffsetsType = ArrayType::get(I32, Variables.size());

    SmallVector<Constant *> Elements;
    for (GlobalVariable *GV : Variables) {
      auto ConstantGepIt = LDSVarsToConstantGEP.find(GV);
      if (ConstantGepIt != LDSVarsToConstantGEP.end()) {
        auto elt = ConstantExpr::getPtrToInt(ConstantGepIt->second, I32);
        Elements.push_back(elt);
      } else {
        Elements.push_back(PoisonValue::get(I32));
      }
    }
    return ConstantArray::get(KernelOffsetsType, Elements);
  }

  static GlobalVariable *buildLookupTable(
      Module &M, ArrayRef<GlobalVariable *> Variables,
      ArrayRef<Function *> kernels,
      DenseMap<Function *, LDSVariableReplacement> &KernelToReplacement) {
    if (Variables.empty()) {
      return nullptr;
    }
    LLVMContext &Ctx = M.getContext();

    const size_t NumberVariables = Variables.size();
    const size_t NumberKernels = kernels.size();

    ArrayType *KernelOffsetsType =
        ArrayType::get(Type::getInt32Ty(Ctx), NumberVariables);

    ArrayType *AllKernelsOffsetsType =
        ArrayType::get(KernelOffsetsType, NumberKernels);

    Constant *Missing = PoisonValue::get(KernelOffsetsType);
    std::vector<Constant *> overallConstantExprElts(NumberKernels);
    for (size_t i = 0; i < NumberKernels; i++) {
      auto Replacement = KernelToReplacement.find(kernels[i]);
      overallConstantExprElts[i] =
          (Replacement == KernelToReplacement.end())
              ? Missing
              : getAddressesOfVariablesInKernel(
                    Ctx, Variables, Replacement->second.LDSVarsToConstantGEP);
    }

    Constant *init =
        ConstantArray::get(AllKernelsOffsetsType, overallConstantExprElts);

    return new GlobalVariable(
        M, AllKernelsOffsetsType, true, GlobalValue::InternalLinkage, init,
        "llvm.amdgcn.lds.offset.table", nullptr, GlobalValue::NotThreadLocal,
        AMDGPUAS::CONSTANT_ADDRESS);
  }

  void replaceUseWithTableLookup(Module &M, IRBuilder<> &Builder,
                                 GlobalVariable *LookupTable,
                                 GlobalVariable *GV, Use &U,
                                 Value *OptionalIndex) {
    // Table is a constant array of the same length as OrderedKernels
    LLVMContext &Ctx = M.getContext();
    Type *I32 = Type::getInt32Ty(Ctx);
    auto *I = cast<Instruction>(U.getUser());

    Value *tableKernelIndex = getTableLookupKernelIndex(M, I->getFunction());

    if (auto *Phi = dyn_cast<PHINode>(I)) {
      BasicBlock *BB = Phi->getIncomingBlock(U);
      Builder.SetInsertPoint(&(*(BB->getFirstInsertionPt())));
    } else {
      Builder.SetInsertPoint(I);
    }

    SmallVector<Value *, 3> GEPIdx = {
        ConstantInt::get(I32, 0),
        tableKernelIndex,
    };
    if (OptionalIndex)
      GEPIdx.push_back(OptionalIndex);

    Value *Address = Builder.CreateInBoundsGEP(
        LookupTable->getValueType(), LookupTable, GEPIdx, GV->getName());

    Value *loaded = Builder.CreateLoad(I32, Address);

    Value *replacement =
        Builder.CreateIntToPtr(loaded, GV->getType(), GV->getName());

    U.set(replacement);
  }

  void replaceUsesInInstructionsWithTableLookup(
      Module &M, ArrayRef<GlobalVariable *> ModuleScopeVariables,
      GlobalVariable *LookupTable) {

    LLVMContext &Ctx = M.getContext();
    IRBuilder<> Builder(Ctx);
    Type *I32 = Type::getInt32Ty(Ctx);

    for (size_t Index = 0; Index < ModuleScopeVariables.size(); Index++) {
      auto *GV = ModuleScopeVariables[Index];

      for (Use &U : make_early_inc_range(GV->uses())) {
        auto *I = dyn_cast<Instruction>(U.getUser());
        if (!I)
          continue;

        replaceUseWithTableLookup(M, Builder, LookupTable, GV, U,
                                  ConstantInt::get(I32, Index));
      }
    }
  }

  static DenseSet<Function *> kernelsThatIndirectlyAccessAnyOfPassedVariables(
      Module &M, LDSUsesInfoTy &LDSUsesInfo,
      DenseSet<GlobalVariable *> const &VariableSet) {

    DenseSet<Function *> KernelSet;

    if (VariableSet.empty())
      return KernelSet;

    for (Function &Func : M.functions()) {
      if (Func.isDeclaration() || !isKernelLDS(&Func))
        continue;
      for (GlobalVariable *GV : LDSUsesInfo.indirect_access[&Func]) {
        if (VariableSet.contains(GV)) {
          KernelSet.insert(&Func);
          break;
        }
      }
    }

    return KernelSet;
  }

  static GlobalVariable *
  chooseBestVariableForModuleStrategy(const DataLayout &DL,
                                      VariableFunctionMap &LDSVars) {
    // Find the global variable with the most indirect uses from kernels

    struct CandidateTy {
      GlobalVariable *GV = nullptr;
      size_t UserCount = 0;
      size_t Size = 0;

      CandidateTy() = default;

      CandidateTy(GlobalVariable *GV, uint64_t UserCount, uint64_t AllocSize)
          : GV(GV), UserCount(UserCount), Size(AllocSize) {}

      bool operator<(const CandidateTy &Other) const {
        // Fewer users makes module scope variable less attractive
        if (UserCount < Other.UserCount) {
          return true;
        }
        if (UserCount > Other.UserCount) {
          return false;
        }

        // Bigger makes module scope variable less attractive
        if (Size < Other.Size) {
          return false;
        }

        if (Size > Other.Size) {
          return true;
        }

        // Arbitrary but consistent
        return GV->getName() < Other.GV->getName();
      }
    };

    CandidateTy MostUsed;

    for (auto &K : LDSVars) {
      GlobalVariable *GV = K.first;
      if (K.second.size() <= 1) {
        // A variable reachable by only one kernel is best lowered with kernel
        // strategy
        continue;
      }
      CandidateTy Candidate(
          GV, K.second.size(),
          DL.getTypeAllocSize(GV->getValueType()).getFixedValue());
      if (MostUsed < Candidate)
        MostUsed = Candidate;
    }

    return MostUsed.GV;
  }

  static void recordLDSAbsoluteAddress(Module *M, GlobalVariable *GV,
                                       uint32_t Address) {
    // Write the specified address into metadata where it can be retrieved by
    // the assembler. Format is a half open range, [Address Address+1)
    LLVMContext &Ctx = M->getContext();
    auto *IntTy =
        M->getDataLayout().getIntPtrType(Ctx, AMDGPUAS::LOCAL_ADDRESS);
    auto *MinC = ConstantAsMetadata::get(ConstantInt::get(IntTy, Address));
    auto *MaxC = ConstantAsMetadata::get(ConstantInt::get(IntTy, Address + 1));
    GV->setMetadata(LLVMContext::MD_absolute_symbol,
                    MDNode::get(Ctx, {MinC, MaxC}));
  }

  DenseMap<Function *, Value *> tableKernelIndexCache;
  Value *getTableLookupKernelIndex(Module &M, Function *F) {
    // Accesses from a function use the amdgcn_lds_kernel_id intrinsic which
    // lowers to a read from a live in register. Emit it once in the entry
    // block to spare deduplicating it later.
    auto [It, Inserted] = tableKernelIndexCache.try_emplace(F);
    if (Inserted) {
      Function *Decl =
          Intrinsic::getDeclaration(&M, Intrinsic::amdgcn_lds_kernel_id, {});

      auto InsertAt = F->getEntryBlock().getFirstNonPHIOrDbgOrAlloca();
      IRBuilder<> Builder(&*InsertAt);

      It->second = Builder.CreateCall(Decl, {});
    }

    return It->second;
  }

  static std::vector<Function *> assignLDSKernelIDToEachKernel(
      Module *M, DenseSet<Function *> const &KernelsThatAllocateTableLDS,
      DenseSet<Function *> const &KernelsThatIndirectlyAllocateDynamicLDS) {
    // Associate kernels in the set with an arbitrary but reproducible order and
    // annotate them with that order in metadata. This metadata is recognised by
    // the backend and lowered to a SGPR which can be read from using
    // amdgcn_lds_kernel_id.

    std::vector<Function *> OrderedKernels;
    if (!KernelsThatAllocateTableLDS.empty() ||
        !KernelsThatIndirectlyAllocateDynamicLDS.empty()) {

      for (Function &Func : M->functions()) {
        if (Func.isDeclaration())
          continue;
        if (!isKernelLDS(&Func))
          continue;

        if (KernelsThatAllocateTableLDS.contains(&Func) ||
            KernelsThatIndirectlyAllocateDynamicLDS.contains(&Func)) {
          assert(Func.hasName()); // else fatal error earlier
          OrderedKernels.push_back(&Func);
        }
      }

      // Put them in an arbitrary but reproducible order
      OrderedKernels = sortByName(std::move(OrderedKernels));

      // Annotate the kernels with their order in this vector
      LLVMContext &Ctx = M->getContext();
      IRBuilder<> Builder(Ctx);

      if (OrderedKernels.size() > UINT32_MAX) {
        // 32 bit keeps it in one SGPR. > 2**32 kernels won't fit on the GPU
        report_fatal_error("Unimplemented LDS lowering for > 2**32 kernels");
      }

      for (size_t i = 0; i < OrderedKernels.size(); i++) {
        Metadata *AttrMDArgs[1] = {
            ConstantAsMetadata::get(Builder.getInt32(i)),
        };
        OrderedKernels[i]->setMetadata("llvm.amdgcn.lds.kernel.id",
                                       MDNode::get(Ctx, AttrMDArgs));
      }
    }
    return OrderedKernels;
  }

  static void partitionVariablesIntoIndirectStrategies(
      Module &M, LDSUsesInfoTy const &LDSUsesInfo,
      VariableFunctionMap &LDSToKernelsThatNeedToAccessItIndirectly,
      DenseSet<GlobalVariable *> &ModuleScopeVariables,
      DenseSet<GlobalVariable *> &TableLookupVariables,
      DenseSet<GlobalVariable *> &KernelAccessVariables,
      DenseSet<GlobalVariable *> &DynamicVariables) {

    GlobalVariable *HybridModuleRoot =
        LoweringKindLoc != LoweringKind::hybrid
            ? nullptr
            : chooseBestVariableForModuleStrategy(
                  M.getDataLayout(), LDSToKernelsThatNeedToAccessItIndirectly);

    DenseSet<Function *> const EmptySet;
    DenseSet<Function *> const &HybridModuleRootKernels =
        HybridModuleRoot
            ? LDSToKernelsThatNeedToAccessItIndirectly[HybridModuleRoot]
            : EmptySet;

    for (auto &K : LDSToKernelsThatNeedToAccessItIndirectly) {
      // Each iteration of this loop assigns exactly one global variable to
      // exactly one of the implementation strategies.

      GlobalVariable *GV = K.first;
      assert(AMDGPU::isLDSVariableToLower(*GV));
      assert(K.second.size() != 0);

      if (AMDGPU::isDynamicLDS(*GV)) {
        DynamicVariables.insert(GV);
        continue;
      }

      switch (LoweringKindLoc) {
      case LoweringKind::module:
        ModuleScopeVariables.insert(GV);
        break;

      case LoweringKind::table:
        TableLookupVariables.insert(GV);
        break;

      case LoweringKind::kernel:
        if (K.second.size() == 1) {
          KernelAccessVariables.insert(GV);
        } else {
          report_fatal_error(
              "cannot lower LDS '" + GV->getName() +
              "' to kernel access as it is reachable from multiple kernels");
        }
        break;

      case LoweringKind::hybrid: {
        if (GV == HybridModuleRoot) {
          assert(K.second.size() != 1);
          ModuleScopeVariables.insert(GV);
        } else if (K.second.size() == 1) {
          KernelAccessVariables.insert(GV);
        } else if (set_is_subset(K.second, HybridModuleRootKernels)) {
          ModuleScopeVariables.insert(GV);
        } else {
          TableLookupVariables.insert(GV);
        }
        break;
      }
      }
    }

    // All LDS variables accessed indirectly have now been partitioned into
    // the distinct lowering strategies.
    assert(ModuleScopeVariables.size() + TableLookupVariables.size() +
               KernelAccessVariables.size() + DynamicVariables.size() ==
           LDSToKernelsThatNeedToAccessItIndirectly.size());
  }

  static GlobalVariable *lowerModuleScopeStructVariables(
      Module &M, DenseSet<GlobalVariable *> const &ModuleScopeVariables,
      DenseSet<Function *> const &KernelsThatAllocateModuleLDS) {
    // Create a struct to hold the ModuleScopeVariables
    // Replace all uses of those variables from non-kernel functions with the
    // new struct instance Replace only the uses from kernel functions that will
    // allocate this instance. That is a space optimisation - kernels that use a
    // subset of the module scope struct and do not need to allocate it for
    // indirect calls will only allocate the subset they use (they do so as part
    // of the per-kernel lowering).
    if (ModuleScopeVariables.empty()) {
      return nullptr;
    }

    LLVMContext &Ctx = M.getContext();

    LDSVariableReplacement ModuleScopeReplacement =
        createLDSVariableReplacement(M, "llvm.amdgcn.module.lds",
                                     ModuleScopeVariables);

    appendToCompilerUsed(M, {static_cast<GlobalValue *>(
                                ConstantExpr::getPointerBitCastOrAddrSpaceCast(
                                    cast<Constant>(ModuleScopeReplacement.SGV),
                                    PointerType::getUnqual(Ctx)))});

    // module.lds will be allocated at zero in any kernel that allocates it
    recordLDSAbsoluteAddress(&M, ModuleScopeReplacement.SGV, 0);

    // historic
    removeLocalVarsFromUsedLists(M, ModuleScopeVariables);

    // Replace all uses of module scope variable from non-kernel functions
    replaceLDSVariablesWithStruct(
        M, ModuleScopeVariables, ModuleScopeReplacement, [&](Use &U) {
          Instruction *I = dyn_cast<Instruction>(U.getUser());
          if (!I) {
            return false;
          }
          Function *F = I->getFunction();
          return !isKernelLDS(F);
        });

    // Replace uses of module scope variable from kernel functions that
    // allocate the module scope variable, otherwise leave them unchanged
    // Record on each kernel whether the module scope global is used by it

    for (Function &Func : M.functions()) {
      if (Func.isDeclaration() || !isKernelLDS(&Func))
        continue;

      if (KernelsThatAllocateModuleLDS.contains(&Func)) {
        replaceLDSVariablesWithStruct(
            M, ModuleScopeVariables, ModuleScopeReplacement, [&](Use &U) {
              Instruction *I = dyn_cast<Instruction>(U.getUser());
              if (!I) {
                return false;
              }
              Function *F = I->getFunction();
              return F == &Func;
            });

        markUsedByKernel(&Func, ModuleScopeReplacement.SGV);
      }
    }

    return ModuleScopeReplacement.SGV;
  }

  static DenseMap<Function *, LDSVariableReplacement>
  lowerKernelScopeStructVariables(
      Module &M, LDSUsesInfoTy &LDSUsesInfo,
      DenseSet<GlobalVariable *> const &ModuleScopeVariables,
      DenseSet<Function *> const &KernelsThatAllocateModuleLDS,
      GlobalVariable *MaybeModuleScopeStruct) {

    // Create a struct for each kernel for the non-module-scope variables.

    DenseMap<Function *, LDSVariableReplacement> KernelToReplacement;
    for (Function &Func : M.functions()) {
      if (Func.isDeclaration() || !isKernelLDS(&Func))
        continue;

      DenseSet<GlobalVariable *> KernelUsedVariables;
      // Allocating variables that are used directly in this struct to get
      // alignment aware allocation and predictable frame size.
      for (auto &v : LDSUsesInfo.direct_access[&Func]) {
        if (!AMDGPU::isDynamicLDS(*v)) {
          KernelUsedVariables.insert(v);
        }
      }

      // Allocating variables that are accessed indirectly so that a lookup of
      // this struct instance can find them from nested functions.
      for (auto &v : LDSUsesInfo.indirect_access[&Func]) {
        if (!AMDGPU::isDynamicLDS(*v)) {
          KernelUsedVariables.insert(v);
        }
      }

      // Variables allocated in module lds must all resolve to that struct,
      // not to the per-kernel instance.
      if (KernelsThatAllocateModuleLDS.contains(&Func)) {
        for (GlobalVariable *v : ModuleScopeVariables) {
          KernelUsedVariables.erase(v);
        }
      }

      if (KernelUsedVariables.empty()) {
        // Either used no LDS, or the LDS it used was all in the module struct
        // or dynamically sized
        continue;
      }

      // The association between kernel function and LDS struct is done by
      // symbol name, which only works if the function in question has a
      // name This is not expected to be a problem in practice as kernels
      // are called by name making anonymous ones (which are named by the
      // backend) difficult to use. This does mean that llvm test cases need
      // to name the kernels.
      if (!Func.hasName()) {
        report_fatal_error("Anonymous kernels cannot use LDS variables");
      }

      std::string VarName =
          (Twine("llvm.amdgcn.kernel.") + Func.getName() + ".lds").str();

      auto Replacement =
          createLDSVariableReplacement(M, VarName, KernelUsedVariables);

      // If any indirect uses, create a direct use to ensure allocation
      // TODO: Simpler to unconditionally mark used but that regresses
      // codegen in test/CodeGen/AMDGPU/noclobber-barrier.ll
      auto Accesses = LDSUsesInfo.indirect_access.find(&Func);
      if ((Accesses != LDSUsesInfo.indirect_access.end()) &&
          !Accesses->second.empty())
        markUsedByKernel(&Func, Replacement.SGV);

      // remove preserves existing codegen
      removeLocalVarsFromUsedLists(M, KernelUsedVariables);
      KernelToReplacement[&Func] = Replacement;

      // Rewrite uses within kernel to the new struct
      replaceLDSVariablesWithStruct(
          M, KernelUsedVariables, Replacement, [&Func](Use &U) {
            Instruction *I = dyn_cast<Instruction>(U.getUser());
            return I && I->getFunction() == &Func;
          });
    }
    return KernelToReplacement;
  }

  static GlobalVariable *
  buildRepresentativeDynamicLDSInstance(Module &M, LDSUsesInfoTy &LDSUsesInfo,
                                        Function *func) {
    // Create a dynamic lds variable with a name associated with the passed
    // function that has the maximum alignment of any dynamic lds variable
    // reachable from this kernel. Dynamic LDS is allocated after the static LDS
    // allocation, possibly after alignment padding. The representative variable
    // created here has the maximum alignment of any other dynamic variable
    // reachable by that kernel. All dynamic LDS variables are allocated at the
    // same address in each kernel in order to provide the documented aliasing
    // semantics. Setting the alignment here allows this IR pass to accurately
    // predict the exact constant at which it will be allocated.

    assert(isKernelLDS(func));

    LLVMContext &Ctx = M.getContext();
    const DataLayout &DL = M.getDataLayout();
    Align MaxDynamicAlignment(1);

    auto UpdateMaxAlignment = [&MaxDynamicAlignment, &DL](GlobalVariable *GV) {
      if (AMDGPU::isDynamicLDS(*GV)) {
        MaxDynamicAlignment =
            std::max(MaxDynamicAlignment, AMDGPU::getAlign(DL, GV));
      }
    };

    for (GlobalVariable *GV : LDSUsesInfo.indirect_access[func]) {
      UpdateMaxAlignment(GV);
    }

    for (GlobalVariable *GV : LDSUsesInfo.direct_access[func]) {
      UpdateMaxAlignment(GV);
    }

    assert(func->hasName()); // Checked by caller
    auto emptyCharArray = ArrayType::get(Type::getInt8Ty(Ctx), 0);
    GlobalVariable *N = new GlobalVariable(
        M, emptyCharArray, false, GlobalValue::ExternalLinkage, nullptr,
        Twine("llvm.amdgcn." + func->getName() + ".dynlds"), nullptr, GlobalValue::NotThreadLocal, AMDGPUAS::LOCAL_ADDRESS,
        false);
    N->setAlignment(MaxDynamicAlignment);

    assert(AMDGPU::isDynamicLDS(*N));
    return N;
  }

  DenseMap<Function *, GlobalVariable *> lowerDynamicLDSVariables(
      Module &M, LDSUsesInfoTy &LDSUsesInfo,
      DenseSet<Function *> const &KernelsThatIndirectlyAllocateDynamicLDS,
      DenseSet<GlobalVariable *> const &DynamicVariables,
      std::vector<Function *> const &OrderedKernels) {
    DenseMap<Function *, GlobalVariable *> KernelToCreatedDynamicLDS;
    if (!KernelsThatIndirectlyAllocateDynamicLDS.empty()) {
      LLVMContext &Ctx = M.getContext();
      IRBuilder<> Builder(Ctx);
      Type *I32 = Type::getInt32Ty(Ctx);

      std::vector<Constant *> newDynamicLDS;

      // Table is built in the same order as OrderedKernels
      for (auto &func : OrderedKernels) {

        if (KernelsThatIndirectlyAllocateDynamicLDS.contains(func)) {
          assert(isKernelLDS(func));
          if (!func->hasName()) {
            report_fatal_error("Anonymous kernels cannot use LDS variables");
          }

          GlobalVariable *N =
              buildRepresentativeDynamicLDSInstance(M, LDSUsesInfo, func);

          KernelToCreatedDynamicLDS[func] = N;

          markUsedByKernel(func, N);

          auto emptyCharArray = ArrayType::get(Type::getInt8Ty(Ctx), 0);
          auto GEP = ConstantExpr::getGetElementPtr(
              emptyCharArray, N, ConstantInt::get(I32, 0), true);
          newDynamicLDS.push_back(ConstantExpr::getPtrToInt(GEP, I32));
        } else {
          newDynamicLDS.push_back(PoisonValue::get(I32));
        }
      }
      assert(OrderedKernels.size() == newDynamicLDS.size());

      ArrayType *t = ArrayType::get(I32, newDynamicLDS.size());
      Constant *init = ConstantArray::get(t, newDynamicLDS);
      GlobalVariable *table = new GlobalVariable(
          M, t, true, GlobalValue::InternalLinkage, init,
          "llvm.amdgcn.dynlds.offset.table", nullptr,
          GlobalValue::NotThreadLocal, AMDGPUAS::CONSTANT_ADDRESS);

      for (GlobalVariable *GV : DynamicVariables) {
        for (Use &U : make_early_inc_range(GV->uses())) {
          auto *I = dyn_cast<Instruction>(U.getUser());
          if (!I)
            continue;
          if (isKernelLDS(I->getFunction()))
            continue;

          replaceUseWithTableLookup(M, Builder, table, GV, U, nullptr);
        }
      }
    }
    return KernelToCreatedDynamicLDS;
  }

  bool runOnModule(Module &M) {
    CallGraph CG = CallGraph(M);
    bool Changed = superAlignLDSGlobals(M);

    Changed |= eliminateConstantExprUsesOfLDSFromAllInstructions(M);

    Changed = true; // todo: narrow this down

    // For each kernel, what variables does it access directly or through
    // callees
    LDSUsesInfoTy LDSUsesInfo = getTransitiveUsesOfLDS(CG, M);

    // For each variable accessed through callees, which kernels access it
    VariableFunctionMap LDSToKernelsThatNeedToAccessItIndirectly;
    for (auto &K : LDSUsesInfo.indirect_access) {
      Function *F = K.first;
      assert(isKernelLDS(F));
      for (GlobalVariable *GV : K.second) {
        LDSToKernelsThatNeedToAccessItIndirectly[GV].insert(F);
      }
    }

    // Partition variables accessed indirectly into the different strategies
    DenseSet<GlobalVariable *> ModuleScopeVariables;
    DenseSet<GlobalVariable *> TableLookupVariables;
    DenseSet<GlobalVariable *> KernelAccessVariables;
    DenseSet<GlobalVariable *> DynamicVariables;
    partitionVariablesIntoIndirectStrategies(
        M, LDSUsesInfo, LDSToKernelsThatNeedToAccessItIndirectly,
        ModuleScopeVariables, TableLookupVariables, KernelAccessVariables,
        DynamicVariables);

    // If the kernel accesses a variable that is going to be stored in the
    // module instance through a call then that kernel needs to allocate the
    // module instance
    const DenseSet<Function *> KernelsThatAllocateModuleLDS =
        kernelsThatIndirectlyAccessAnyOfPassedVariables(M, LDSUsesInfo,
                                                        ModuleScopeVariables);
    const DenseSet<Function *> KernelsThatAllocateTableLDS =
        kernelsThatIndirectlyAccessAnyOfPassedVariables(M, LDSUsesInfo,
                                                        TableLookupVariables);

    const DenseSet<Function *> KernelsThatIndirectlyAllocateDynamicLDS =
        kernelsThatIndirectlyAccessAnyOfPassedVariables(M, LDSUsesInfo,
                                                        DynamicVariables);

    GlobalVariable *MaybeModuleScopeStruct = lowerModuleScopeStructVariables(
        M, ModuleScopeVariables, KernelsThatAllocateModuleLDS);

    DenseMap<Function *, LDSVariableReplacement> KernelToReplacement =
        lowerKernelScopeStructVariables(M, LDSUsesInfo, ModuleScopeVariables,
                                        KernelsThatAllocateModuleLDS,
                                        MaybeModuleScopeStruct);

    // Lower zero cost accesses to the kernel instances just created
    for (auto &GV : KernelAccessVariables) {
      auto &funcs = LDSToKernelsThatNeedToAccessItIndirectly[GV];
      assert(funcs.size() == 1); // Only one kernel can access it
      LDSVariableReplacement Replacement =
          KernelToReplacement[*(funcs.begin())];

      DenseSet<GlobalVariable *> Vec;
      Vec.insert(GV);

      replaceLDSVariablesWithStruct(M, Vec, Replacement, [](Use &U) {
        return isa<Instruction>(U.getUser());
      });
    }

    // The ith element of this vector is kernel id i
    std::vector<Function *> OrderedKernels =
        assignLDSKernelIDToEachKernel(&M, KernelsThatAllocateTableLDS,
                                      KernelsThatIndirectlyAllocateDynamicLDS);

    if (!KernelsThatAllocateTableLDS.empty()) {
      LLVMContext &Ctx = M.getContext();
      IRBuilder<> Builder(Ctx);

      // The order must be consistent between lookup table and accesses to
      // lookup table
      auto TableLookupVariablesOrdered =
          sortByName(std::vector<GlobalVariable *>(TableLookupVariables.begin(),
                                                   TableLookupVariables.end()));

      GlobalVariable *LookupTable = buildLookupTable(
          M, TableLookupVariablesOrdered, OrderedKernels, KernelToReplacement);
      replaceUsesInInstructionsWithTableLookup(M, TableLookupVariablesOrdered,
                                               LookupTable);

      // Strip amdgpu-no-lds-kernel-id from all functions reachable from the
      // kernel. We may have inferred this wasn't used prior to the pass.
      //
      // TODO: We could filter out subgraphs that do not access LDS globals.
      for (Function *F : KernelsThatAllocateTableLDS)
        removeFnAttrFromReachable(CG, F, {"amdgpu-no-lds-kernel-id"});
    }

    DenseMap<Function *, GlobalVariable *> KernelToCreatedDynamicLDS =
        lowerDynamicLDSVariables(M, LDSUsesInfo,
                                 KernelsThatIndirectlyAllocateDynamicLDS,
                                 DynamicVariables, OrderedKernels);

    // All kernel frames have been allocated. Calculate and record the
    // addresses.
    {
      const DataLayout &DL = M.getDataLayout();

      for (Function &Func : M.functions()) {
        if (Func.isDeclaration() || !isKernelLDS(&Func))
          continue;

        // All three of these are optional. The first variable is allocated at
        // zero. They are allocated by AMDGPUMachineFunction as one block.
        // Layout:
        //{
        //  module.lds
        //  alignment padding
        //  kernel instance
        //  alignment padding
        //  dynamic lds variables
        //}

        const bool AllocateModuleScopeStruct =
            MaybeModuleScopeStruct &&
            KernelsThatAllocateModuleLDS.contains(&Func);

        auto Replacement = KernelToReplacement.find(&Func);
        const bool AllocateKernelScopeStruct =
            Replacement != KernelToReplacement.end();

        const bool AllocateDynamicVariable =
            KernelToCreatedDynamicLDS.contains(&Func);

        uint32_t Offset = 0;

        if (AllocateModuleScopeStruct) {
          // Allocated at zero, recorded once on construction, not once per
          // kernel
          Offset += DL.getTypeAllocSize(MaybeModuleScopeStruct->getValueType());
        }

        if (AllocateKernelScopeStruct) {
          GlobalVariable *KernelStruct = Replacement->second.SGV;
          Offset = alignTo(Offset, AMDGPU::getAlign(DL, KernelStruct));
          recordLDSAbsoluteAddress(&M, KernelStruct, Offset);
          Offset += DL.getTypeAllocSize(KernelStruct->getValueType());
        }

        // If there is dynamic allocation, the alignment needed is included in
        // the static frame size. There may be no reference to the dynamic
        // variable in the kernel itself, so without including it here, that
        // alignment padding could be missed.
        if (AllocateDynamicVariable) {
          GlobalVariable *DynamicVariable = KernelToCreatedDynamicLDS[&Func];
          Offset = alignTo(Offset, AMDGPU::getAlign(DL, DynamicVariable));
          recordLDSAbsoluteAddress(&M, DynamicVariable, Offset);
        }

        if (Offset != 0) {
          (void)TM; // TODO: Account for target maximum LDS
          std::string Buffer;
          raw_string_ostream SS{Buffer};
          SS << format("%u", Offset);

          // Instead of explicitly marking kernels that access dynamic variables
          // using special case metadata, annotate with min-lds == max-lds, i.e.
          // that there is no more space available for allocating more static
          // LDS variables. That is the right condition to prevent allocating
          // more variables which would collide with the addresses assigned to
          // dynamic variables.
          if (AllocateDynamicVariable)
            SS << format(",%u", Offset);

          Func.addFnAttr("amdgpu-lds-size", Buffer);
        }
      }
    }

    for (auto &GV : make_early_inc_range(M.globals()))
      if (AMDGPU::isLDSVariableToLower(GV)) {
        // probably want to remove from used lists
        GV.removeDeadConstantUsers();
        if (GV.use_empty())
          GV.eraseFromParent();
      }

    return Changed;
  }

private:
  // Increase the alignment of LDS globals if necessary to maximise the chance
  // that we can use aligned LDS instructions to access them.
  static bool superAlignLDSGlobals(Module &M) {
    const DataLayout &DL = M.getDataLayout();
    bool Changed = false;
    if (!SuperAlignLDSGlobals) {
      return Changed;
    }

    for (auto &GV : M.globals()) {
      if (GV.getType()->getPointerAddressSpace() != AMDGPUAS::LOCAL_ADDRESS) {
        // Only changing alignment of LDS variables
        continue;
      }
      if (!GV.hasInitializer()) {
        // cuda/hip extern __shared__ variable, leave alignment alone
        continue;
      }

      Align Alignment = AMDGPU::getAlign(DL, &GV);
      TypeSize GVSize = DL.getTypeAllocSize(GV.getValueType());

      if (GVSize > 8) {
        // We might want to use a b96 or b128 load/store
        Alignment = std::max(Alignment, Align(16));
      } else if (GVSize > 4) {
        // We might want to use a b64 load/store
        Alignment = std::max(Alignment, Align(8));
      } else if (GVSize > 2) {
        // We might want to use a b32 load/store
        Alignment = std::max(Alignment, Align(4));
      } else if (GVSize > 1) {
        // We might want to use a b16 load/store
        Alignment = std::max(Alignment, Align(2));
      }

      if (Alignment != AMDGPU::getAlign(DL, &GV)) {
        Changed = true;
        GV.setAlignment(Alignment);
      }
    }
    return Changed;
  }

  static LDSVariableReplacement createLDSVariableReplacement(
      Module &M, std::string VarName,
      DenseSet<GlobalVariable *> const &LDSVarsToTransform) {
    // Create a struct instance containing LDSVarsToTransform and map from those
    // variables to ConstantExprGEP
    // Variables may be introduced to meet alignment requirements. No aliasing
    // metadata is useful for these as they have no uses. Erased before return.

    LLVMContext &Ctx = M.getContext();
    const DataLayout &DL = M.getDataLayout();
    assert(!LDSVarsToTransform.empty());

    SmallVector<OptimizedStructLayoutField, 8> LayoutFields;
    LayoutFields.reserve(LDSVarsToTransform.size());
    {
      // The order of fields in this struct depends on the order of
      // variables in the argument which varies when changing how they
      // are identified, leading to spurious test breakage.
      auto Sorted = sortByName(std::vector<GlobalVariable *>(
          LDSVarsToTransform.begin(), LDSVarsToTransform.end()));

      for (GlobalVariable *GV : Sorted) {
        OptimizedStructLayoutField F(GV,
                                     DL.getTypeAllocSize(GV->getValueType()),
                                     AMDGPU::getAlign(DL, GV));
        LayoutFields.emplace_back(F);
      }
    }

    performOptimizedStructLayout(LayoutFields);

    std::vector<GlobalVariable *> LocalVars;
    BitVector IsPaddingField;
    LocalVars.reserve(LDSVarsToTransform.size()); // will be at least this large
    IsPaddingField.reserve(LDSVarsToTransform.size());
    {
      uint64_t CurrentOffset = 0;
      for (auto &F : LayoutFields) {
        GlobalVariable *FGV =
            static_cast<GlobalVariable *>(const_cast<void *>(F.Id));
        Align DataAlign = F.Alignment;

        uint64_t DataAlignV = DataAlign.value();
        if (uint64_t Rem = CurrentOffset % DataAlignV) {
          uint64_t Padding = DataAlignV - Rem;

          // Append an array of padding bytes to meet alignment requested
          // Note (o +      (a - (o % a)) ) % a == 0
          //      (offset + Padding       ) % align == 0

          Type *ATy = ArrayType::get(Type::getInt8Ty(Ctx), Padding);
          LocalVars.push_back(new GlobalVariable(
              M, ATy, false, GlobalValue::InternalLinkage,
              PoisonValue::get(ATy), "", nullptr, GlobalValue::NotThreadLocal,
              AMDGPUAS::LOCAL_ADDRESS, false));
          IsPaddingField.push_back(true);
          CurrentOffset += Padding;
        }

        LocalVars.push_back(FGV);
        IsPaddingField.push_back(false);
        CurrentOffset += F.Size;
      }
    }

    std::vector<Type *> LocalVarTypes;
    LocalVarTypes.reserve(LocalVars.size());
    std::transform(
        LocalVars.cbegin(), LocalVars.cend(), std::back_inserter(LocalVarTypes),
        [](const GlobalVariable *V) -> Type * { return V->getValueType(); });

    StructType *LDSTy = StructType::create(Ctx, LocalVarTypes, VarName + ".t");

    Align StructAlign = AMDGPU::getAlign(DL, LocalVars[0]);

    GlobalVariable *SGV = new GlobalVariable(
        M, LDSTy, false, GlobalValue::InternalLinkage, PoisonValue::get(LDSTy),
        VarName, nullptr, GlobalValue::NotThreadLocal, AMDGPUAS::LOCAL_ADDRESS,
        false);
    SGV->setAlignment(StructAlign);

    DenseMap<GlobalVariable *, Constant *> Map;
    Type *I32 = Type::getInt32Ty(Ctx);
    for (size_t I = 0; I < LocalVars.size(); I++) {
      GlobalVariable *GV = LocalVars[I];
      Constant *GEPIdx[] = {ConstantInt::get(I32, 0), ConstantInt::get(I32, I)};
      Constant *GEP = ConstantExpr::getGetElementPtr(LDSTy, SGV, GEPIdx, true);
      if (IsPaddingField[I]) {
        assert(GV->use_empty());
        GV->eraseFromParent();
      } else {
        Map[GV] = GEP;
      }
    }
    assert(Map.size() == LDSVarsToTransform.size());
    return {SGV, std::move(Map)};
  }

  template <typename PredicateTy>
  static void replaceLDSVariablesWithStruct(
      Module &M, DenseSet<GlobalVariable *> const &LDSVarsToTransformArg,
      const LDSVariableReplacement &Replacement, PredicateTy Predicate) {
    LLVMContext &Ctx = M.getContext();
    const DataLayout &DL = M.getDataLayout();

    // A hack... we need to insert the aliasing info in a predictable order for
    // lit tests. Would like to have them in a stable order already, ideally the
    // same order they get allocated, which might mean an ordered set container
    auto LDSVarsToTransform = sortByName(std::vector<GlobalVariable *>(
        LDSVarsToTransformArg.begin(), LDSVarsToTransformArg.end()));

    // Create alias.scope and their lists. Each field in the new structure
    // does not alias with all other fields.
    SmallVector<MDNode *> AliasScopes;
    SmallVector<Metadata *> NoAliasList;
    const size_t NumberVars = LDSVarsToTransform.size();
    if (NumberVars > 1) {
      MDBuilder MDB(Ctx);
      AliasScopes.reserve(NumberVars);
      MDNode *Domain = MDB.createAnonymousAliasScopeDomain();
      for (size_t I = 0; I < NumberVars; I++) {
        MDNode *Scope = MDB.createAnonymousAliasScope(Domain);
        AliasScopes.push_back(Scope);
      }
      NoAliasList.append(&AliasScopes[1], AliasScopes.end());
    }

    // Replace uses of ith variable with a constantexpr to the corresponding
    // field of the instance that will be allocated by AMDGPUMachineFunction
    for (size_t I = 0; I < NumberVars; I++) {
      GlobalVariable *GV = LDSVarsToTransform[I];
      Constant *GEP = Replacement.LDSVarsToConstantGEP.at(GV);

      GV->replaceUsesWithIf(GEP, Predicate);

      APInt APOff(DL.getIndexTypeSizeInBits(GEP->getType()), 0);
      GEP->stripAndAccumulateInBoundsConstantOffsets(DL, APOff);
      uint64_t Offset = APOff.getZExtValue();

      Align A =
          commonAlignment(Replacement.SGV->getAlign().valueOrOne(), Offset);

      if (I)
        NoAliasList[I - 1] = AliasScopes[I - 1];
      MDNode *NoAlias =
          NoAliasList.empty() ? nullptr : MDNode::get(Ctx, NoAliasList);
      MDNode *AliasScope =
          AliasScopes.empty() ? nullptr : MDNode::get(Ctx, {AliasScopes[I]});

      refineUsesAlignmentAndAA(GEP, A, DL, AliasScope, NoAlias);
    }
  }

  static void refineUsesAlignmentAndAA(Value *Ptr, Align A,
                                       const DataLayout &DL, MDNode *AliasScope,
                                       MDNode *NoAlias, unsigned MaxDepth = 5) {
    if (!MaxDepth || (A == 1 && !AliasScope))
      return;

    for (User *U : Ptr->users()) {
      if (auto *I = dyn_cast<Instruction>(U)) {
        if (AliasScope && I->mayReadOrWriteMemory()) {
          MDNode *AS = I->getMetadata(LLVMContext::MD_alias_scope);
          AS = (AS ? MDNode::getMostGenericAliasScope(AS, AliasScope)
                   : AliasScope);
          I->setMetadata(LLVMContext::MD_alias_scope, AS);

          MDNode *NA = I->getMetadata(LLVMContext::MD_noalias);
          NA = (NA ? MDNode::intersect(NA, NoAlias) : NoAlias);
          I->setMetadata(LLVMContext::MD_noalias, NA);
        }
      }

      if (auto *LI = dyn_cast<LoadInst>(U)) {
        LI->setAlignment(std::max(A, LI->getAlign()));
        continue;
      }
      if (auto *SI = dyn_cast<StoreInst>(U)) {
        if (SI->getPointerOperand() == Ptr)
          SI->setAlignment(std::max(A, SI->getAlign()));
        continue;
      }
      if (auto *AI = dyn_cast<AtomicRMWInst>(U)) {
        // None of atomicrmw operations can work on pointers, but let's
        // check it anyway in case it will or we will process ConstantExpr.
        if (AI->getPointerOperand() == Ptr)
          AI->setAlignment(std::max(A, AI->getAlign()));
        continue;
      }
      if (auto *AI = dyn_cast<AtomicCmpXchgInst>(U)) {
        if (AI->getPointerOperand() == Ptr)
          AI->setAlignment(std::max(A, AI->getAlign()));
        continue;
      }
      if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
        unsigned BitWidth = DL.getIndexTypeSizeInBits(GEP->getType());
        APInt Off(BitWidth, 0);
        if (GEP->getPointerOperand() == Ptr) {
          Align GA;
          if (GEP->accumulateConstantOffset(DL, Off))
            GA = commonAlignment(A, Off.getLimitedValue());
          refineUsesAlignmentAndAA(GEP, GA, DL, AliasScope, NoAlias,
                                   MaxDepth - 1);
        }
        continue;
      }
      if (auto *I = dyn_cast<Instruction>(U)) {
        if (I->getOpcode() == Instruction::BitCast ||
            I->getOpcode() == Instruction::AddrSpaceCast)
          refineUsesAlignmentAndAA(I, A, DL, AliasScope, NoAlias, MaxDepth - 1);
      }
    }
  }
};

class AMDGPULowerModuleLDSLegacy : public ModulePass {
public:
  const AMDGPUTargetMachine *TM;
  static char ID;

  AMDGPULowerModuleLDSLegacy(const AMDGPUTargetMachine *TM_ = nullptr)
      : ModulePass(ID), TM(TM_) {
    initializeAMDGPULowerModuleLDSLegacyPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    if (!TM)
      AU.addRequired<TargetPassConfig>();
  }

  bool runOnModule(Module &M) override {
    if (!TM) {
      auto &TPC = getAnalysis<TargetPassConfig>();
      TM = &TPC.getTM<AMDGPUTargetMachine>();
    }

    return AMDGPULowerModuleLDS(*TM).runOnModule(M);
  }
};

} // namespace
char AMDGPULowerModuleLDSLegacy::ID = 0;

char &llvm::AMDGPULowerModuleLDSLegacyPassID = AMDGPULowerModuleLDSLegacy::ID;

INITIALIZE_PASS_BEGIN(AMDGPULowerModuleLDSLegacy, DEBUG_TYPE,
                      "Lower uses of LDS variables from non-kernel functions",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(AMDGPULowerModuleLDSLegacy, DEBUG_TYPE,
                    "Lower uses of LDS variables from non-kernel functions",
                    false, false)

ModulePass *
llvm::createAMDGPULowerModuleLDSLegacyPass(const AMDGPUTargetMachine *TM) {
  return new AMDGPULowerModuleLDSLegacy(TM);
}

PreservedAnalyses AMDGPULowerModuleLDSPass::run(Module &M,
                                                ModuleAnalysisManager &) {
  return AMDGPULowerModuleLDS(TM).runOnModule(M) ? PreservedAnalyses::none()
                                                 : PreservedAnalyses::all();
}
