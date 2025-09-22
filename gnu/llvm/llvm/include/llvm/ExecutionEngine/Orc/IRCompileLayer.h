//===- IRCompileLayer.h -- Eagerly compile IR for JIT -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains the definition for a basic, eagerly compiling layer of the JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_IRCOMPILELAYER_H
#define LLVM_EXECUTIONENGINE_ORC_IRCOMPILELAYER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <functional>
#include <memory>
#include <mutex>

namespace llvm {

class Module;

namespace orc {

class IRCompileLayer : public IRLayer {
public:
  class IRCompiler {
  public:
    IRCompiler(IRSymbolMapper::ManglingOptions MO) : MO(std::move(MO)) {}
    virtual ~IRCompiler();
    const IRSymbolMapper::ManglingOptions &getManglingOptions() const {
      return MO;
    }
    virtual Expected<std::unique_ptr<MemoryBuffer>> operator()(Module &M) = 0;

  protected:
    IRSymbolMapper::ManglingOptions &manglingOptions() { return MO; }

  private:
    IRSymbolMapper::ManglingOptions MO;
  };

  using NotifyCompiledFunction = std::function<void(
      MaterializationResponsibility &R, ThreadSafeModule TSM)>;

  IRCompileLayer(ExecutionSession &ES, ObjectLayer &BaseLayer,
                 std::unique_ptr<IRCompiler> Compile);

  IRCompiler &getCompiler() { return *Compile; }

  void setNotifyCompiled(NotifyCompiledFunction NotifyCompiled);

  void emit(std::unique_ptr<MaterializationResponsibility> R,
            ThreadSafeModule TSM) override;

private:
  mutable std::mutex IRLayerMutex;
  ObjectLayer &BaseLayer;
  std::unique_ptr<IRCompiler> Compile;
  const IRSymbolMapper::ManglingOptions *ManglingOpts;
  NotifyCompiledFunction NotifyCompiled = NotifyCompiledFunction();
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_IRCOMPILELAYER_H
