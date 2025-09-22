//===---------- ObjectTransformLayer.cpp - Object Transform Layer ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace orc {

char ObjectTransformLayer::ID;

using BaseT = RTTIExtends<ObjectTransformLayer, ObjectLayer>;

ObjectTransformLayer::ObjectTransformLayer(ExecutionSession &ES,
                                           ObjectLayer &BaseLayer,
                                           TransformFunction Transform)
    : BaseT(ES), BaseLayer(BaseLayer), Transform(std::move(Transform)) {}

void ObjectTransformLayer::emit(
    std::unique_ptr<MaterializationResponsibility> R,
    std::unique_ptr<MemoryBuffer> O) {
  assert(O && "Module must not be null");

  // If there is a transform set then apply it.
  if (Transform) {
    if (auto TransformedObj = Transform(std::move(O)))
      O = std::move(*TransformedObj);
    else {
      R->failMaterialization();
      getExecutionSession().reportError(TransformedObj.takeError());
      return;
    }
  }

  BaseLayer.emit(std::move(R), std::move(O));
}

} // End namespace orc.
} // End namespace llvm.
