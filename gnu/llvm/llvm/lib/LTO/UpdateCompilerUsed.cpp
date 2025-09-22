//==-LTOInternalize.cpp - LLVM Link Time Optimizer Internalization Utility -==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a helper to run the internalization part of LTO.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/legacy/UpdateCompilerUsed.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

namespace {

// Helper class that collects AsmUsed and user supplied libcalls.
class PreserveLibCallsAndAsmUsed {
public:
  PreserveLibCallsAndAsmUsed(const StringSet<> &AsmUndefinedRefs,
                             const TargetMachine &TM,
                             std::vector<GlobalValue *> &LLVMUsed)
      : AsmUndefinedRefs(AsmUndefinedRefs), TM(TM), LLVMUsed(LLVMUsed) {}

  void findInModule(Module &TheModule) {
    initializeLibCalls(TheModule);
    for (Function &F : TheModule)
      findLibCallsAndAsm(F);
    for (GlobalVariable &GV : TheModule.globals())
      findLibCallsAndAsm(GV);
    for (GlobalAlias &GA : TheModule.aliases())
      findLibCallsAndAsm(GA);
  }

private:
  // Inputs
  const StringSet<> &AsmUndefinedRefs;
  const TargetMachine &TM;

  // Temps
  llvm::Mangler Mangler;
  StringSet<> Libcalls;

  // Output
  std::vector<GlobalValue *> &LLVMUsed;

  // Collect names of runtime library functions. User-defined functions with the
  // same names are added to llvm.compiler.used to prevent them from being
  // deleted by optimizations.
  void initializeLibCalls(const Module &TheModule) {
    TargetLibraryInfoImpl TLII(Triple(TM.getTargetTriple()));
    TargetLibraryInfo TLI(TLII);

    // TargetLibraryInfo has info on C runtime library calls on the current
    // target.
    for (unsigned I = 0, E = static_cast<unsigned>(LibFunc::NumLibFuncs);
         I != E; ++I) {
      LibFunc F = static_cast<LibFunc>(I);
      if (TLI.has(F))
        Libcalls.insert(TLI.getName(F));
    }

    SmallPtrSet<const TargetLowering *, 1> TLSet;

    for (const Function &F : TheModule) {
      const TargetLowering *Lowering =
          TM.getSubtargetImpl(F)->getTargetLowering();

      if (Lowering && TLSet.insert(Lowering).second)
        // TargetLowering has info on library calls that CodeGen expects to be
        // available, both from the C runtime and compiler-rt.
        for (unsigned I = 0, E = static_cast<unsigned>(RTLIB::UNKNOWN_LIBCALL);
             I != E; ++I)
          if (const char *Name =
                  Lowering->getLibcallName(static_cast<RTLIB::Libcall>(I)))
            Libcalls.insert(Name);
    }
  }

  void findLibCallsAndAsm(GlobalValue &GV) {
    // There are no restrictions to apply to declarations.
    if (GV.isDeclaration())
      return;

    // There is nothing more restrictive than private linkage.
    if (GV.hasPrivateLinkage())
      return;

    // Conservatively append user-supplied runtime library functions (supplied
    // either directly, or via a function alias) to llvm.compiler.used.  These
    // could be internalized and deleted by optimizations like -globalopt,
    // causing problems when later optimizations add new library calls (e.g.,
    // llvm.memset => memset and printf => puts).
    // Leave it to the linker to remove any dead code (e.g. with -dead_strip).
    GlobalValue *FuncAliasee = nullptr;
    if (isa<GlobalAlias>(GV)) {
      auto *A = cast<GlobalAlias>(&GV);
      FuncAliasee = dyn_cast<Function>(A->getAliasee());
    }
    if ((isa<Function>(GV) || FuncAliasee) && Libcalls.count(GV.getName())) {
      LLVMUsed.push_back(&GV);
      return;
    }

    SmallString<64> Buffer;
    TM.getNameWithPrefix(Buffer, &GV, Mangler);
    if (AsmUndefinedRefs.count(Buffer))
      LLVMUsed.push_back(&GV);
  }
};

} // namespace anonymous

void llvm::updateCompilerUsed(Module &TheModule, const TargetMachine &TM,
                              const StringSet<> &AsmUndefinedRefs) {
  std::vector<GlobalValue *> UsedValues;
  PreserveLibCallsAndAsmUsed(AsmUndefinedRefs, TM, UsedValues)
      .findInModule(TheModule);

  if (UsedValues.empty())
    return;

  appendToCompilerUsed(TheModule, UsedValues);
}
