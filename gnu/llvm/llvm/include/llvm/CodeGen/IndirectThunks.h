//===---- IndirectThunks.h - Indirect thunk insertion helpers ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Contains a base ThunkInserter class that simplifies injection of MI thunks
/// as well as a default implementation of MachineFunctionPass wrapping
/// several `ThunkInserter`s for targets to extend.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_INDIRECTTHUNKS_H
#define LLVM_CODEGEN_INDIRECTTHUNKS_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

namespace llvm {

/// This class assists in inserting MI thunk functions into the module and
/// rewriting the existing machine functions to call these thunks.
///
/// One of the common cases is implementing security mitigations that involve
/// replacing some machine code patterns with calls to special thunk functions.
///
/// Inserting a module pass late in the codegen pipeline may increase memory
/// usage, as it serializes the transformations and forces preceding passes to
/// produce machine code for all functions before running the module pass.
/// For that reason, ThunkInserter can be driven by a MachineFunctionPass by
/// passing one MachineFunction at a time to its `run(MMI, MF)` method.
/// Then, the derived class should
/// * call createThunkFunction from its insertThunks method exactly once for
///   each of the thunk functions to be inserted
/// * populate the thunk in its populateThunk method
///
/// Note that if some other pass is responsible for rewriting the functions,
/// the insertThunks method may simply create all possible thunks at once,
/// probably postponed until the first occurrence of possibly affected MF.
///
/// Alternatively, insertThunks method can rewrite MF by itself and only insert
/// the thunks being called. In that case InsertedThunks variable can be used
/// to track which thunks were already inserted.
///
/// In any case, the thunk function has to be inserted on behalf of some other
/// function and then populated on its own "iteration" later - this is because
/// MachineFunctionPass will see the newly created functions, but they first
/// have to go through the preceding passes from the same pass manager,
/// possibly even through the instruction selector.
//
// FIXME Maybe implement a documented and less surprising way of modifying
//       the module from a MachineFunctionPass that is restricted to inserting
//       completely new functions to the module.
template <typename Derived, typename InsertedThunksTy = bool>
class ThunkInserter {
  Derived &getDerived() { return *static_cast<Derived *>(this); }

  // A variable used to track whether (and possible which) thunks have been
  // inserted so far. InsertedThunksTy is usually a bool, but can be other types
  // to represent more than one type of thunk. Requires an |= operator to
  // accumulate results.
  InsertedThunksTy InsertedThunks;

protected:
  // Interface for subclasses to use.

  /// Create an empty thunk function.
  ///
  /// The new function will eventually be passed to populateThunk. If multiple
  /// thunks are created, populateThunk can distinguish them by their names.
  void createThunkFunction(MachineModuleInfo &MMI, StringRef Name,
                           bool Comdat = true, StringRef TargetAttrs = "");

protected:
  // Interface for subclasses to implement.
  //
  // Note: all functions are non-virtual and are called via getDerived().
  // Note: only doInitialization() has an implementation.

  /// Initializes thunk inserter.
  void doInitialization(Module &M) {}

  /// Returns common prefix for thunk function's names.
  const char *getThunkPrefix(); // undefined

  /// Checks if MF may use thunks (true - maybe, false - definitely not).
  bool mayUseThunk(const MachineFunction &MF); // undefined

  /// Rewrites the function if necessary, returns the set of thunks added.
  InsertedThunksTy insertThunks(MachineModuleInfo &MMI, MachineFunction &MF,
                                InsertedThunksTy ExistingThunks); // undefined

  /// Populate the thunk function with instructions.
  ///
  /// If multiple thunks are created, the content that must be inserted in the
  /// thunk function body should be derived from the MF's name.
  ///
  /// Depending on the preceding passes in the pass manager, by the time
  /// populateThunk is called, MF may have a few target-specific instructions
  /// (such as a single MBB containing the return instruction).
  void populateThunk(MachineFunction &MF); // undefined

public:
  void init(Module &M) {
    InsertedThunks = InsertedThunksTy{};
    getDerived().doInitialization(M);
  }
  // return `true` if `MMI` or `MF` was modified
  bool run(MachineModuleInfo &MMI, MachineFunction &MF);
};

template <typename Derived, typename InsertedThunksTy>
void ThunkInserter<Derived, InsertedThunksTy>::createThunkFunction(
    MachineModuleInfo &MMI, StringRef Name, bool Comdat,
    StringRef TargetAttrs) {
  assert(Name.starts_with(getDerived().getThunkPrefix()) &&
         "Created a thunk with an unexpected prefix!");

  Module &M = const_cast<Module &>(*MMI.getModule());
  LLVMContext &Ctx = M.getContext();
  auto *Type = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *F = Function::Create(Type,
                                 Comdat ? GlobalValue::LinkOnceODRLinkage
                                        : GlobalValue::InternalLinkage,
                                 Name, &M);
  if (Comdat) {
    F->setVisibility(GlobalValue::HiddenVisibility);
    F->setComdat(M.getOrInsertComdat(Name));
  }

  // Add Attributes so that we don't create a frame, unwind information, or
  // inline.
  AttrBuilder B(Ctx);
  B.addAttribute(llvm::Attribute::NoUnwind);
  B.addAttribute(llvm::Attribute::Naked);
  if (TargetAttrs != "")
    B.addAttribute("target-features", TargetAttrs);
  F->addFnAttrs(B);

  // Populate our function a bit so that we can verify.
  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> Builder(Entry);

  Builder.CreateRetVoid();

  // MachineFunctions aren't created automatically for the IR-level constructs
  // we already made. Create them and insert them into the module.
  MachineFunction &MF = MMI.getOrCreateMachineFunction(*F);
  // A MachineBasicBlock must not be created for the Entry block; code
  // generation from an empty naked function in C source code also does not
  // generate one.  At least GlobalISel asserts if this invariant isn't
  // respected.

  // Set MF properties. We never use vregs...
  MF.getProperties().set(MachineFunctionProperties::Property::NoVRegs);
}

template <typename Derived, typename InsertedThunksTy>
bool ThunkInserter<Derived, InsertedThunksTy>::run(MachineModuleInfo &MMI,
                                                   MachineFunction &MF) {
  // If MF is not a thunk, check to see if we need to insert a thunk.
  if (!MF.getName().starts_with(getDerived().getThunkPrefix())) {
    // Only add thunks if one of the functions may use them.
    if (!getDerived().mayUseThunk(MF))
      return false;

    // The target can use InsertedThunks to detect whether relevant thunks
    // have already been inserted.
    // FIXME: Provide the way for insertThunks to notify us whether it changed
    //        the MF, instead of conservatively assuming it did.
    InsertedThunks |= getDerived().insertThunks(MMI, MF, InsertedThunks);
    return true;
  }

  // If this *is* a thunk function, we need to populate it with the correct MI.
  getDerived().populateThunk(MF);
  return true;
}

/// Basic implementation of MachineFunctionPass wrapping one or more
/// `ThunkInserter`s passed as type parameters.
template <typename... Inserters>
class ThunkInserterPass : public MachineFunctionPass {
protected:
  std::tuple<Inserters...> TIs;

  ThunkInserterPass(char &ID) : MachineFunctionPass(ID) {}

public:
  bool doInitialization(Module &M) override {
    initTIs(M, TIs);
    return false;
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    auto &MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
    return runTIs(MMI, MF, TIs);
  }

private:
  template <typename... ThunkInserterT>
  static void initTIs(Module &M,
                      std::tuple<ThunkInserterT...> &ThunkInserters) {
    (..., std::get<ThunkInserterT>(ThunkInserters).init(M));
  }

  template <typename... ThunkInserterT>
  static bool runTIs(MachineModuleInfo &MMI, MachineFunction &MF,
                     std::tuple<ThunkInserterT...> &ThunkInserters) {
    return (0 | ... | std::get<ThunkInserterT>(ThunkInserters).run(MMI, MF));
  }
};

} // namespace llvm

#endif
