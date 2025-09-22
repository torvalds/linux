//===- EPCDebugObjectRegistrar.h - EPC-based debug registration -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ExecutorProcessControl based registration of debug objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EPCDEBUGOBJECTREGISTRAR_H
#define LLVM_EXECUTIONENGINE_ORC_EPCDEBUGOBJECTREGISTRAR_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Memory.h"

#include <cstdint>
#include <memory>

namespace llvm {
namespace orc {

class ExecutionSession;

/// Abstract interface for registering debug objects in the executor process.
class DebugObjectRegistrar {
public:
  virtual Error registerDebugObject(ExecutorAddrRange TargetMem,
                                    bool AutoRegisterCode) = 0;
  virtual ~DebugObjectRegistrar() = default;
};

/// Use ExecutorProcessControl to register debug objects locally or in a remote
/// executor process.
class EPCDebugObjectRegistrar : public DebugObjectRegistrar {
public:
  EPCDebugObjectRegistrar(ExecutionSession &ES, ExecutorAddr RegisterFn)
      : ES(ES), RegisterFn(RegisterFn) {}

  Error registerDebugObject(ExecutorAddrRange TargetMem,
                            bool AutoRegisterCode) override;

private:
  ExecutionSession &ES;
  ExecutorAddr RegisterFn;
};

/// Create a ExecutorProcessControl-based DebugObjectRegistrar that emits debug
/// objects to the GDB JIT interface. This will use the EPC's lookupSymbols
/// method to find the registration/deregistration  function addresses by name.
///
/// If RegistrationFunctionsDylib is non-None then it will be searched to find
/// the registration functions. If it is None then the process dylib will be
/// loaded to find the registration functions.
Expected<std::unique_ptr<EPCDebugObjectRegistrar>> createJITLoaderGDBRegistrar(
    ExecutionSession &ES,
    std::optional<ExecutorAddr> RegistrationFunctionDylib = std::nullopt);

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EPCDEBUGOBJECTREGISTRAR_H
