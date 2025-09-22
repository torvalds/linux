//===-- ObjectCache.h - Class definition for the ObjectCache ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_OBJECTCACHE_H
#define LLVM_EXECUTIONENGINE_OBJECTCACHE_H

#include <memory>

namespace llvm {

class MemoryBuffer;
class MemoryBufferRef;
class Module;

/// This is the base ObjectCache type which can be provided to an
/// ExecutionEngine for the purpose of avoiding compilation for Modules that
/// have already been compiled and an object file is available.
class ObjectCache {
  virtual void anchor();

public:
  ObjectCache() = default;

  virtual ~ObjectCache() = default;

  /// notifyObjectCompiled - Provides a pointer to compiled code for Module M.
  virtual void notifyObjectCompiled(const Module *M, MemoryBufferRef Obj) = 0;

  /// Returns a pointer to a newly allocated MemoryBuffer that contains the
  /// object which corresponds with Module M, or 0 if an object is not
  /// available.
  virtual std::unique_ptr<MemoryBuffer> getObject(const Module* M) = 0;
};

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_OBJECTCACHE_H
