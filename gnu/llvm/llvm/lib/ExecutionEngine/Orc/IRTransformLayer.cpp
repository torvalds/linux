//===-------------- IRTransformLayer.cpp - IR Transform Layer -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace orc {

IRTransformLayer::IRTransformLayer(ExecutionSession &ES, IRLayer &BaseLayer,
                                   TransformFunction Transform)
    : IRLayer(ES, BaseLayer.getManglingOptions()), BaseLayer(BaseLayer),
      Transform(std::move(Transform)) {}

void IRTransformLayer::emit(std::unique_ptr<MaterializationResponsibility> R,
                            ThreadSafeModule TSM) {
  assert(TSM && "Module must not be null");

  if (auto TransformedTSM = Transform(std::move(TSM), *R))
    BaseLayer.emit(std::move(R), std::move(*TransformedTSM));
  else {
    R->failMaterialization();
    getExecutionSession().reportError(TransformedTSM.takeError());
  }
}

} // End namespace orc.
} // End namespace llvm.
