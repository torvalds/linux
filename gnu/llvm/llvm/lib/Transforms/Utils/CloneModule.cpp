//===- CloneModule.cpp - Clone an entire module ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CloneModule interface which makes a copy of an
// entire module.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
using namespace llvm;

namespace llvm {
class Constant;
}

static void copyComdat(GlobalObject *Dst, const GlobalObject *Src) {
  const Comdat *SC = Src->getComdat();
  if (!SC)
    return;
  Comdat *DC = Dst->getParent()->getOrInsertComdat(SC->getName());
  DC->setSelectionKind(SC->getSelectionKind());
  Dst->setComdat(DC);
}

/// This is not as easy as it might seem because we have to worry about making
/// copies of global variables and functions, and making their (initializers and
/// references, respectively) refer to the right globals.
///
/// Cloning un-materialized modules is not currently supported, so any
/// modules initialized via lazy loading should be materialized before cloning
std::unique_ptr<Module> llvm::CloneModule(const Module &M) {
  // Create the value map that maps things from the old module over to the new
  // module.
  ValueToValueMapTy VMap;
  return CloneModule(M, VMap);
}

std::unique_ptr<Module> llvm::CloneModule(const Module &M,
                                          ValueToValueMapTy &VMap) {
  return CloneModule(M, VMap, [](const GlobalValue *GV) { return true; });
}

std::unique_ptr<Module> llvm::CloneModule(
    const Module &M, ValueToValueMapTy &VMap,
    function_ref<bool(const GlobalValue *)> ShouldCloneDefinition) {

  assert(M.isMaterialized() && "Module must be materialized before cloning!");

  // First off, we need to create the new module.
  std::unique_ptr<Module> New =
      std::make_unique<Module>(M.getModuleIdentifier(), M.getContext());
  New->setSourceFileName(M.getSourceFileName());
  New->setDataLayout(M.getDataLayout());
  New->setTargetTriple(M.getTargetTriple());
  New->setModuleInlineAsm(M.getModuleInlineAsm());
  New->IsNewDbgInfoFormat = M.IsNewDbgInfoFormat;

  // Loop over all of the global variables, making corresponding globals in the
  // new module.  Here we add them to the VMap and to the new Module.  We
  // don't worry about attributes or initializers, they will come later.
  //
  for (const GlobalVariable &I : M.globals()) {
    GlobalVariable *NewGV = new GlobalVariable(
        *New, I.getValueType(), I.isConstant(), I.getLinkage(),
        (Constant *)nullptr, I.getName(), (GlobalVariable *)nullptr,
        I.getThreadLocalMode(), I.getType()->getAddressSpace());
    NewGV->copyAttributesFrom(&I);
    VMap[&I] = NewGV;
  }

  // Loop over the functions in the module, making external functions as before
  for (const Function &I : M) {
    Function *NF =
        Function::Create(cast<FunctionType>(I.getValueType()), I.getLinkage(),
                         I.getAddressSpace(), I.getName(), New.get());
    NF->copyAttributesFrom(&I);
    VMap[&I] = NF;
  }

  // Loop over the aliases in the module
  for (const GlobalAlias &I : M.aliases()) {
    if (!ShouldCloneDefinition(&I)) {
      // An alias cannot act as an external reference, so we need to create
      // either a function or a global variable depending on the value type.
      // FIXME: Once pointee types are gone we can probably pick one or the
      // other.
      GlobalValue *GV;
      if (I.getValueType()->isFunctionTy())
        GV = Function::Create(cast<FunctionType>(I.getValueType()),
                              GlobalValue::ExternalLinkage, I.getAddressSpace(),
                              I.getName(), New.get());
      else
        GV = new GlobalVariable(*New, I.getValueType(), false,
                                GlobalValue::ExternalLinkage, nullptr,
                                I.getName(), nullptr, I.getThreadLocalMode(),
                                I.getType()->getAddressSpace());
      VMap[&I] = GV;
      // We do not copy attributes (mainly because copying between different
      // kinds of globals is forbidden), but this is generally not required for
      // correctness.
      continue;
    }
    auto *GA = GlobalAlias::create(I.getValueType(),
                                   I.getType()->getPointerAddressSpace(),
                                   I.getLinkage(), I.getName(), New.get());
    GA->copyAttributesFrom(&I);
    VMap[&I] = GA;
  }

  for (const GlobalIFunc &I : M.ifuncs()) {
    // Defer setting the resolver function until after functions are cloned.
    auto *GI =
        GlobalIFunc::create(I.getValueType(), I.getAddressSpace(),
                            I.getLinkage(), I.getName(), nullptr, New.get());
    GI->copyAttributesFrom(&I);
    VMap[&I] = GI;
  }

  // Now that all of the things that global variable initializer can refer to
  // have been created, loop through and copy the global variable referrers
  // over...  We also set the attributes on the global now.
  //
  for (const GlobalVariable &G : M.globals()) {
    GlobalVariable *GV = cast<GlobalVariable>(VMap[&G]);

    SmallVector<std::pair<unsigned, MDNode *>, 1> MDs;
    G.getAllMetadata(MDs);
    for (auto MD : MDs)
      GV->addMetadata(MD.first, *MapMetadata(MD.second, VMap));

    if (G.isDeclaration())
      continue;

    if (!ShouldCloneDefinition(&G)) {
      // Skip after setting the correct linkage for an external reference.
      GV->setLinkage(GlobalValue::ExternalLinkage);
      continue;
    }
    if (G.hasInitializer())
      GV->setInitializer(MapValue(G.getInitializer(), VMap));

    copyComdat(GV, &G);
  }

  // Similarly, copy over function bodies now...
  //
  for (const Function &I : M) {
    Function *F = cast<Function>(VMap[&I]);

    if (I.isDeclaration()) {
      // Copy over metadata for declarations since we're not doing it below in
      // CloneFunctionInto().
      SmallVector<std::pair<unsigned, MDNode *>, 1> MDs;
      I.getAllMetadata(MDs);
      for (auto MD : MDs)
        F->addMetadata(MD.first, *MapMetadata(MD.second, VMap));
      continue;
    }

    if (!ShouldCloneDefinition(&I)) {
      // Skip after setting the correct linkage for an external reference.
      F->setLinkage(GlobalValue::ExternalLinkage);
      // Personality function is not valid on a declaration.
      F->setPersonalityFn(nullptr);
      continue;
    }

    Function::arg_iterator DestI = F->arg_begin();
    for (const Argument &J : I.args()) {
      DestI->setName(J.getName());
      VMap[&J] = &*DestI++;
    }

    SmallVector<ReturnInst *, 8> Returns; // Ignore returns cloned.
    CloneFunctionInto(F, &I, VMap, CloneFunctionChangeType::ClonedModule,
                      Returns);

    if (I.hasPersonalityFn())
      F->setPersonalityFn(MapValue(I.getPersonalityFn(), VMap));

    copyComdat(F, &I);
  }

  // And aliases
  for (const GlobalAlias &I : M.aliases()) {
    // We already dealt with undefined aliases above.
    if (!ShouldCloneDefinition(&I))
      continue;
    GlobalAlias *GA = cast<GlobalAlias>(VMap[&I]);
    if (const Constant *C = I.getAliasee())
      GA->setAliasee(MapValue(C, VMap));
  }

  for (const GlobalIFunc &I : M.ifuncs()) {
    GlobalIFunc *GI = cast<GlobalIFunc>(VMap[&I]);
    if (const Constant *Resolver = I.getResolver())
      GI->setResolver(MapValue(Resolver, VMap));
  }

  // And named metadata....
  for (const NamedMDNode &NMD : M.named_metadata()) {
    NamedMDNode *NewNMD = New->getOrInsertNamedMetadata(NMD.getName());
    for (const MDNode *N : NMD.operands())
      NewNMD->addOperand(MapMetadata(N, VMap));
  }

  return New;
}

extern "C" {

LLVMModuleRef LLVMCloneModule(LLVMModuleRef M) {
  return wrap(CloneModule(*unwrap(M)).release());
}

}
