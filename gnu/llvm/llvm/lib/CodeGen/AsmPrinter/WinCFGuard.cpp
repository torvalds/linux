//===-- CodeGen/AsmPrinter/WinCFGuard.cpp - Control Flow Guard Impl ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing the metadata for Windows Control Flow
// Guard, including address-taken functions and valid longjmp targets.
//
//===----------------------------------------------------------------------===//

#include "WinCFGuard.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCStreamer.h"

#include <vector>

using namespace llvm;

WinCFGuard::WinCFGuard(AsmPrinter *A) : Asm(A) {}

WinCFGuard::~WinCFGuard() = default;

void WinCFGuard::endFunction(const MachineFunction *MF) {

  // Skip functions without any longjmp targets.
  if (MF->getLongjmpTargets().empty())
    return;

  // Copy the function's longjmp targets to a module-level list.
  llvm::append_range(LongjmpTargets, MF->getLongjmpTargets());
}

/// Returns true if this function's address is escaped in a way that might make
/// it an indirect call target. Function::hasAddressTaken gives different
/// results when a function is called directly with a function prototype
/// mismatch, which requires a cast.
static bool isPossibleIndirectCallTarget(const Function *F) {
  SmallVector<const Value *, 4> Users{F};
  while (!Users.empty()) {
    const Value *FnOrCast = Users.pop_back_val();
    for (const Use &U : FnOrCast->uses()) {
      const User *FnUser = U.getUser();
      if (isa<BlockAddress>(FnUser))
        continue;
      if (const auto *Call = dyn_cast<CallBase>(FnUser)) {
        if (!Call->isCallee(&U))
          return true;
      } else if (isa<Instruction>(FnUser)) {
        // Consider any other instruction to be an escape. This has some weird
        // consequences like no-op intrinsics being an escape or a store *to* a
        // function address being an escape.
        return true;
      } else if (const auto *C = dyn_cast<Constant>(FnUser)) {
        // If this is a constant pointer cast of the function, don't consider
        // this escape. Analyze the uses of the cast as well. This ensures that
        // direct calls with mismatched prototypes don't end up in the CFG
        // table. Consider other constants, such as vtable initializers, to
        // escape the function.
        if (C->stripPointerCasts() == F)
          Users.push_back(FnUser);
        else
          return true;
      }
    }
  }
  return false;
}

MCSymbol *WinCFGuard::lookupImpSymbol(const MCSymbol *Sym) {
  if (Sym->getName().starts_with("__imp_"))
    return nullptr;
  return Asm->OutContext.lookupSymbol(Twine("__imp_") + Sym->getName());
}

void WinCFGuard::endModule() {
  const Module *M = Asm->MMI->getModule();
  std::vector<const MCSymbol *> GFIDsEntries;
  std::vector<const MCSymbol *> GIATsEntries;
  for (const Function &F : *M) {
    if (isPossibleIndirectCallTarget(&F)) {
      // If F is a dllimport and has an "__imp_" symbol already defined, add the
      // "__imp_" symbol to the .giats section.
      if (F.hasDLLImportStorageClass()) {
        if (MCSymbol *impSym = lookupImpSymbol(Asm->getSymbol(&F))) {
          GIATsEntries.push_back(impSym);
        }
      }
      // Add the function's symbol to the .gfids section.
      // Note: For dllimport functions, MSVC sometimes does not add this symbol
      // to the .gfids section, but only adds the corresponding "__imp_" symbol
      // to the .giats section. Here we always add the symbol to the .gfids
      // section, since this does not introduce security risks.
      GFIDsEntries.push_back(Asm->getSymbol(&F));
    }
  }

  if (GFIDsEntries.empty() && GIATsEntries.empty() && LongjmpTargets.empty())
    return;

  // Emit the symbol index of each GFIDs entry to form the .gfids section.
  auto &OS = *Asm->OutStreamer;
  OS.switchSection(Asm->OutContext.getObjectFileInfo()->getGFIDsSection());
  for (const MCSymbol *S : GFIDsEntries)
    OS.emitCOFFSymbolIndex(S);

  // Emit the symbol index of each GIATs entry to form the .giats section.
  OS.switchSection(Asm->OutContext.getObjectFileInfo()->getGIATsSection());
  for (const MCSymbol *S : GIATsEntries) {
    OS.emitCOFFSymbolIndex(S);
  }

  // Emit the symbol index of each longjmp target to form the .gljmp section.
  OS.switchSection(Asm->OutContext.getObjectFileInfo()->getGLJMPSection());
  for (const MCSymbol *S : LongjmpTargets) {
    OS.emitCOFFSymbolIndex(S);
  }
}
