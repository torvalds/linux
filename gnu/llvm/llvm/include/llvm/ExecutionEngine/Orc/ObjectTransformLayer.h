//===- ObjectTransformLayer.h - Run all objects through functor -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Run all objects passed in through a user supplied functor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_OBJECTTRANSFORMLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_OBJECTTRANSFORMLAYER_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include <algorithm>
#include <memory>

namespace llvm {
namespace orc {

class ObjectTransformLayer
    : public RTTIExtends<ObjectTransformLayer, ObjectLayer> {
public:
  static char ID;

  using TransformFunction =
      std::function<Expected<std::unique_ptr<MemoryBuffer>>(
          std::unique_ptr<MemoryBuffer>)>;

  ObjectTransformLayer(ExecutionSession &ES, ObjectLayer &BaseLayer,
                       TransformFunction Transform = TransformFunction());

  void emit(std::unique_ptr<MaterializationResponsibility> R,
            std::unique_ptr<MemoryBuffer> O) override;

  void setTransform(TransformFunction Transform) {
    this->Transform = std::move(Transform);
  }

private:
  ObjectLayer &BaseLayer;
  TransformFunction Transform;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_OBJECTTRANSFORMLAYER_H
