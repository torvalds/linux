//===-- ObjectFileInterface.h - MU interface utils for objects --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for building MaterializationUnit::Interface objects from
// object files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_OBJECTFILEINTERFACE_H
#define LLVM_EXECUTIONENGINE_ORC_OBJECTFILEINTERFACE_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace orc {

/// Adds an initializer symbol to the given MU interface.
/// The init symbol's name is guaranteed to be unique within I, and will be of
/// the form $.<ObjFileName>.__inits.<N>, where N is some integer.
void addInitSymbol(MaterializationUnit::Interface &I, ExecutionSession &ES,
                   StringRef ObjFileName);

/// Returns a MaterializationUnit::Interface for the object file contained in
/// the given buffer, or an error if the buffer does not contain a valid object
/// file.
Expected<MaterializationUnit::Interface>
getObjectFileInterface(ExecutionSession &ES, MemoryBufferRef ObjBuffer);

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_OBJECTFILEINTERFACE_H
