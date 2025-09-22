//===- GVMaterializer.h - Interface for GV materializers --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an abstract interface for loading a module from some
// place.  This interface allows incremental or random access loading of
// functions from the file.  This is useful for applications like JIT compilers
// or interprocedural optimizers that do not need the entire program in memory
// at the same time.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GVMATERIALIZER_H
#define LLVM_IR_GVMATERIALIZER_H

#include <vector>

namespace llvm {

class Error;
class GlobalValue;
class StructType;

class GVMaterializer {
protected:
  GVMaterializer() = default;

public:
  virtual ~GVMaterializer();

  /// Make sure the given GlobalValue is fully read.
  ///
  virtual Error materialize(GlobalValue *GV) = 0;

  /// Make sure the entire Module has been completely read.
  ///
  virtual Error materializeModule() = 0;

  virtual Error materializeMetadata() = 0;
  virtual void setStripDebugInfo() = 0;

  virtual std::vector<StructType *> getIdentifiedStructTypes() const = 0;
};

} // end namespace llvm

#endif // LLVM_IR_GVMATERIALIZER_H
