//===- IRTransformLayer.h - Run all IR through a functor --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Run all IR passed in through a user supplied functor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_IRTRANSFORMLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_IRTRANSFORMLAYER_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include <memory>

namespace llvm {
namespace orc {

/// A layer that applies a transform to emitted modules.
/// The transform function is responsible for locking the ThreadSafeContext
/// before operating on the module.
class IRTransformLayer : public IRLayer {
public:
  using TransformFunction = unique_function<Expected<ThreadSafeModule>(
      ThreadSafeModule, MaterializationResponsibility &R)>;

  IRTransformLayer(ExecutionSession &ES, IRLayer &BaseLayer,
                   TransformFunction Transform = identityTransform);

  void setTransform(TransformFunction Transform) {
    this->Transform = std::move(Transform);
  }

  void emit(std::unique_ptr<MaterializationResponsibility> R,
            ThreadSafeModule TSM) override;

  static ThreadSafeModule identityTransform(ThreadSafeModule TSM,
                                            MaterializationResponsibility &R) {
    return TSM;
  }

private:
  IRLayer &BaseLayer;
  TransformFunction Transform;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_IRTRANSFORMLAYER_H
