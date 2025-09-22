//===-------------------- Layer.cpp - Layer interfaces --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Layer.h"

#include "llvm/ExecutionEngine/Orc/DebugUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ObjectFileInterface.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "orc"

namespace llvm {
namespace orc {

IRLayer::~IRLayer() = default;

Error IRLayer::add(ResourceTrackerSP RT, ThreadSafeModule TSM) {
  assert(RT && "RT can not be null");
  auto &JD = RT->getJITDylib();
  return JD.define(std::make_unique<BasicIRLayerMaterializationUnit>(
                       *this, *getManglingOptions(), std::move(TSM)),
                   std::move(RT));
}

IRMaterializationUnit::IRMaterializationUnit(
    ExecutionSession &ES, const IRSymbolMapper::ManglingOptions &MO,
    ThreadSafeModule TSM)
    : MaterializationUnit(Interface()), TSM(std::move(TSM)) {

  assert(this->TSM && "Module must not be null");

  MangleAndInterner Mangle(ES, this->TSM.getModuleUnlocked()->getDataLayout());
  this->TSM.withModuleDo([&](Module &M) {
    for (auto &G : M.global_values()) {
      // Skip globals that don't generate symbols.

      if (!G.hasName() || G.isDeclaration() || G.hasLocalLinkage() ||
          G.hasAvailableExternallyLinkage() || G.hasAppendingLinkage())
        continue;

      // thread locals generate different symbols depending on whether or not
      // emulated TLS is enabled.
      if (G.isThreadLocal() && MO.EmulatedTLS) {
        auto &GV = cast<GlobalVariable>(G);

        auto Flags = JITSymbolFlags::fromGlobalValue(GV);

        auto EmuTLSV = Mangle(("__emutls_v." + GV.getName()).str());
        SymbolFlags[EmuTLSV] = Flags;
        SymbolToDefinition[EmuTLSV] = &GV;

        // If this GV has a non-zero initializer we'll need to emit an
        // __emutls.t symbol too.
        if (GV.hasInitializer()) {
          const auto *InitVal = GV.getInitializer();

          // Skip zero-initializers.
          if (isa<ConstantAggregateZero>(InitVal))
            continue;
          const auto *InitIntValue = dyn_cast<ConstantInt>(InitVal);
          if (InitIntValue && InitIntValue->isZero())
            continue;

          auto EmuTLST = Mangle(("__emutls_t." + GV.getName()).str());
          SymbolFlags[EmuTLST] = Flags;
        }
        continue;
      }

      // Otherwise we just need a normal linker mangling.
      auto MangledName = Mangle(G.getName());
      SymbolFlags[MangledName] = JITSymbolFlags::fromGlobalValue(G);
      if (G.getComdat() &&
          G.getComdat()->getSelectionKind() != Comdat::NoDeduplicate)
        SymbolFlags[MangledName] |= JITSymbolFlags::Weak;
      SymbolToDefinition[MangledName] = &G;
    }

    // If we need an init symbol for this module then create one.
    if (!getStaticInitGVs(M).empty()) {
      size_t Counter = 0;

      do {
        std::string InitSymbolName;
        raw_string_ostream(InitSymbolName)
            << "$." << M.getModuleIdentifier() << ".__inits." << Counter++;
        InitSymbol = ES.intern(InitSymbolName);
      } while (SymbolFlags.count(InitSymbol));

      SymbolFlags[InitSymbol] = JITSymbolFlags::MaterializationSideEffectsOnly;
    }
  });
}

IRMaterializationUnit::IRMaterializationUnit(
    ThreadSafeModule TSM, Interface I,
    SymbolNameToDefinitionMap SymbolToDefinition)
    : MaterializationUnit(std::move(I)), TSM(std::move(TSM)),
      SymbolToDefinition(std::move(SymbolToDefinition)) {}

StringRef IRMaterializationUnit::getName() const {
  if (TSM)
    return TSM.withModuleDo(
        [](const Module &M) -> StringRef { return M.getModuleIdentifier(); });
  return "<null module>";
}

void IRMaterializationUnit::discard(const JITDylib &JD,
                                    const SymbolStringPtr &Name) {
  LLVM_DEBUG(JD.getExecutionSession().runSessionLocked([&]() {
    dbgs() << "In " << JD.getName() << " discarding " << *Name << " from MU@"
           << this << " (" << getName() << ")\n";
  }););

  auto I = SymbolToDefinition.find(Name);
  assert(I != SymbolToDefinition.end() &&
         "Symbol not provided by this MU, or previously discarded");
  assert(!I->second->isDeclaration() &&
         "Discard should only apply to definitions");
  I->second->setLinkage(GlobalValue::AvailableExternallyLinkage);
  // According to the IR verifier, "Declaration[s] may not be in a Comdat!"
  // Remove it, if this is a GlobalObject.
  if (auto *GO = dyn_cast<GlobalObject>(I->second))
    GO->setComdat(nullptr);
  SymbolToDefinition.erase(I);
}

BasicIRLayerMaterializationUnit::BasicIRLayerMaterializationUnit(
    IRLayer &L, const IRSymbolMapper::ManglingOptions &MO, ThreadSafeModule TSM)
    : IRMaterializationUnit(L.getExecutionSession(), MO, std::move(TSM)), L(L) {
}

void BasicIRLayerMaterializationUnit::materialize(
    std::unique_ptr<MaterializationResponsibility> R) {

  // Throw away the SymbolToDefinition map: it's not usable after we hand
  // off the module.
  SymbolToDefinition.clear();

  // If cloneToNewContextOnEmit is set, clone the module now.
  if (L.getCloneToNewContextOnEmit())
    TSM = cloneToNewContext(TSM);

#ifndef NDEBUG
  auto &ES = R->getTargetJITDylib().getExecutionSession();
  auto &N = R->getTargetJITDylib().getName();
#endif // NDEBUG

  LLVM_DEBUG(ES.runSessionLocked(
      [&]() { dbgs() << "Emitting, for " << N << ", " << *this << "\n"; }););
  L.emit(std::move(R), std::move(TSM));
  LLVM_DEBUG(ES.runSessionLocked([&]() {
    dbgs() << "Finished emitting, for " << N << ", " << *this << "\n";
  }););
}

char ObjectLayer::ID;

ObjectLayer::ObjectLayer(ExecutionSession &ES) : ES(ES) {}

ObjectLayer::~ObjectLayer() = default;

Error ObjectLayer::add(ResourceTrackerSP RT, std::unique_ptr<MemoryBuffer> O,
                       MaterializationUnit::Interface I) {
  assert(RT && "RT can not be null");
  auto &JD = RT->getJITDylib();
  return JD.define(std::make_unique<BasicObjectLayerMaterializationUnit>(
                       *this, std::move(O), std::move(I)),
                   std::move(RT));
}

Error ObjectLayer::add(ResourceTrackerSP RT, std::unique_ptr<MemoryBuffer> O) {
  auto I = getObjectFileInterface(getExecutionSession(), O->getMemBufferRef());
  if (!I)
    return I.takeError();
  return add(std::move(RT), std::move(O), std::move(*I));
}

Error ObjectLayer::add(JITDylib &JD, std::unique_ptr<MemoryBuffer> O) {
  auto I = getObjectFileInterface(getExecutionSession(), O->getMemBufferRef());
  if (!I)
    return I.takeError();
  return add(JD, std::move(O), std::move(*I));
}

Expected<std::unique_ptr<BasicObjectLayerMaterializationUnit>>
BasicObjectLayerMaterializationUnit::Create(ObjectLayer &L,
                                            std::unique_ptr<MemoryBuffer> O) {

  auto ObjInterface =
      getObjectFileInterface(L.getExecutionSession(), O->getMemBufferRef());

  if (!ObjInterface)
    return ObjInterface.takeError();

  return std::make_unique<BasicObjectLayerMaterializationUnit>(
      L, std::move(O), std::move(*ObjInterface));
}

BasicObjectLayerMaterializationUnit::BasicObjectLayerMaterializationUnit(
    ObjectLayer &L, std::unique_ptr<MemoryBuffer> O, Interface I)
    : MaterializationUnit(std::move(I)), L(L), O(std::move(O)) {}

StringRef BasicObjectLayerMaterializationUnit::getName() const {
  if (O)
    return O->getBufferIdentifier();
  return "<null object>";
}

void BasicObjectLayerMaterializationUnit::materialize(
    std::unique_ptr<MaterializationResponsibility> R) {
  L.emit(std::move(R), std::move(O));
}

void BasicObjectLayerMaterializationUnit::discard(const JITDylib &JD,
                                                  const SymbolStringPtr &Name) {
  // This is a no-op for object files: Having removed 'Name' from SymbolFlags
  // the symbol will be dead-stripped by the JIT linker.
}

} // End namespace orc.
} // End namespace llvm.
