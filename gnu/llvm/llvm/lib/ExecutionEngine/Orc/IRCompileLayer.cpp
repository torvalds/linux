//===--------------- IRCompileLayer.cpp - IR Compiling Layer --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"

namespace llvm {
namespace orc {

IRCompileLayer::IRCompiler::~IRCompiler() = default;

IRCompileLayer::IRCompileLayer(ExecutionSession &ES, ObjectLayer &BaseLayer,
                               std::unique_ptr<IRCompiler> Compile)
    : IRLayer(ES, ManglingOpts), BaseLayer(BaseLayer),
      Compile(std::move(Compile)) {
  ManglingOpts = &this->Compile->getManglingOptions();
}

void IRCompileLayer::setNotifyCompiled(NotifyCompiledFunction NotifyCompiled) {
  std::lock_guard<std::mutex> Lock(IRLayerMutex);
  this->NotifyCompiled = std::move(NotifyCompiled);
}

void IRCompileLayer::emit(std::unique_ptr<MaterializationResponsibility> R,
                          ThreadSafeModule TSM) {
  assert(TSM && "Module must not be null");

  if (auto Obj = TSM.withModuleDo(*Compile)) {
    {
      std::lock_guard<std::mutex> Lock(IRLayerMutex);
      if (NotifyCompiled)
        NotifyCompiled(*R, std::move(TSM));
      else
        TSM = ThreadSafeModule();
    }
    BaseLayer.emit(std::move(R), std::move(*Obj));
  } else {
    R->failMaterialization();
    getExecutionSession().reportError(Obj.takeError());
  }
}

} // End namespace orc.
} // End namespace llvm.
