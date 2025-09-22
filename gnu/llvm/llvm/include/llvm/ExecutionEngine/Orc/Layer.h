//===---------------- Layer.h -- Layer interfaces --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Layer interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LAYER_H
#define LLVM_EXECUTIONENGINE_ORC_LAYER_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Mangling.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ExtensibleRTTI.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace orc {

/// IRMaterializationUnit is a convenient base class for MaterializationUnits
/// wrapping LLVM IR. Represents materialization responsibility for all symbols
/// in the given module. If symbols are overridden by other definitions, then
/// their linkage is changed to available-externally.
class IRMaterializationUnit : public MaterializationUnit {
public:
  using SymbolNameToDefinitionMap = std::map<SymbolStringPtr, GlobalValue *>;

  /// Create an IRMaterializationLayer. Scans the module to build the
  /// SymbolFlags and SymbolToDefinition maps.
  IRMaterializationUnit(ExecutionSession &ES,
                        const IRSymbolMapper::ManglingOptions &MO,
                        ThreadSafeModule TSM);

  /// Create an IRMaterializationLayer from a module, and pre-existing
  /// SymbolFlags and SymbolToDefinition maps. The maps must provide
  /// entries for each definition in M.
  /// This constructor is useful for delegating work from one
  /// IRMaterializationUnit to another.
  IRMaterializationUnit(ThreadSafeModule TSM, Interface I,
                        SymbolNameToDefinitionMap SymbolToDefinition);

  /// Return the ModuleIdentifier as the name for this MaterializationUnit.
  StringRef getName() const override;

  /// Return a reference to the contained ThreadSafeModule.
  const ThreadSafeModule &getModule() const { return TSM; }

protected:
  ThreadSafeModule TSM;
  SymbolNameToDefinitionMap SymbolToDefinition;

private:
  static SymbolStringPtr getInitSymbol(ExecutionSession &ES,
                                       const ThreadSafeModule &TSM);

  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
};

/// Interface for layers that accept LLVM IR.
class IRLayer {
public:
  IRLayer(ExecutionSession &ES, const IRSymbolMapper::ManglingOptions *&MO)
      : ES(ES), MO(MO) {}

  virtual ~IRLayer();

  /// Returns the ExecutionSession for this layer.
  ExecutionSession &getExecutionSession() { return ES; }

  /// Get the mangling options for this layer.
  const IRSymbolMapper::ManglingOptions *&getManglingOptions() const {
    return MO;
  }

  /// Sets the CloneToNewContextOnEmit flag (false by default).
  ///
  /// When set, IR modules added to this layer will be cloned on to a new
  /// context before emit is called. This can be used by clients who want
  /// to load all IR using one LLVMContext (to save memory via type and
  /// constant uniquing), but want to move Modules to fresh contexts before
  /// compiling them to enable concurrent compilation.
  /// Single threaded clients, or clients who load every module on a new
  /// context, need not set this.
  void setCloneToNewContextOnEmit(bool CloneToNewContextOnEmit) {
    this->CloneToNewContextOnEmit = CloneToNewContextOnEmit;
  }

  /// Returns the current value of the CloneToNewContextOnEmit flag.
  bool getCloneToNewContextOnEmit() const { return CloneToNewContextOnEmit; }

  /// Add a MaterializatinoUnit representing the given IR to the JITDylib
  /// targeted by the given tracker.
  virtual Error add(ResourceTrackerSP RT, ThreadSafeModule TSM);

  /// Adds a MaterializationUnit representing the given IR to the given
  /// JITDylib. If RT is not specif
  Error add(JITDylib &JD, ThreadSafeModule TSM) {
    return add(JD.getDefaultResourceTracker(), std::move(TSM));
  }

  /// Emit should materialize the given IR.
  virtual void emit(std::unique_ptr<MaterializationResponsibility> R,
                    ThreadSafeModule TSM) = 0;

private:
  bool CloneToNewContextOnEmit = false;
  ExecutionSession &ES;
  const IRSymbolMapper::ManglingOptions *&MO;
};

/// MaterializationUnit that materializes modules by calling the 'emit' method
/// on the given IRLayer.
class BasicIRLayerMaterializationUnit : public IRMaterializationUnit {
public:
  BasicIRLayerMaterializationUnit(IRLayer &L,
                                  const IRSymbolMapper::ManglingOptions &MO,
                                  ThreadSafeModule TSM);

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;

  IRLayer &L;
};

/// Interface for Layers that accept object files.
class ObjectLayer : public RTTIExtends<ObjectLayer, RTTIRoot> {
public:
  static char ID;

  ObjectLayer(ExecutionSession &ES);
  virtual ~ObjectLayer();

  /// Returns the execution session for this layer.
  ExecutionSession &getExecutionSession() { return ES; }

  /// Adds a MaterializationUnit for the object file in the given memory buffer
  /// to the JITDylib for the given ResourceTracker.
  virtual Error add(ResourceTrackerSP RT, std::unique_ptr<MemoryBuffer> O,
                    MaterializationUnit::Interface I);

  /// Adds a MaterializationUnit for the object file in the given memory buffer
  /// to the JITDylib for the given ResourceTracker. The interface for the
  /// object will be built using the default object interface builder.
  Error add(ResourceTrackerSP RT, std::unique_ptr<MemoryBuffer> O);

  /// Adds a MaterializationUnit for the object file in the given memory buffer
  /// to the given JITDylib.
  Error add(JITDylib &JD, std::unique_ptr<MemoryBuffer> O,
            MaterializationUnit::Interface I) {
    return add(JD.getDefaultResourceTracker(), std::move(O), std::move(I));
  }

  /// Adds a MaterializationUnit for the object file in the given memory buffer
  /// to the given JITDylib. The interface for the object will be built using
  /// the default object interface builder.
  Error add(JITDylib &JD, std::unique_ptr<MemoryBuffer> O);

  /// Emit should materialize the given IR.
  virtual void emit(std::unique_ptr<MaterializationResponsibility> R,
                    std::unique_ptr<MemoryBuffer> O) = 0;

private:
  ExecutionSession &ES;
};

/// Materializes the given object file (represented by a MemoryBuffer
/// instance) by calling 'emit' on the given ObjectLayer.
class BasicObjectLayerMaterializationUnit : public MaterializationUnit {
public:
  /// Create using the default object interface builder function.
  static Expected<std::unique_ptr<BasicObjectLayerMaterializationUnit>>
  Create(ObjectLayer &L, std::unique_ptr<MemoryBuffer> O);

  BasicObjectLayerMaterializationUnit(ObjectLayer &L,
                                      std::unique_ptr<MemoryBuffer> O,
                                      Interface I);

  /// Return the buffer's identifier as the name for this MaterializationUnit.
  StringRef getName() const override;

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;

  ObjectLayer &L;
  std::unique_ptr<MemoryBuffer> O;
};

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LAYER_H
