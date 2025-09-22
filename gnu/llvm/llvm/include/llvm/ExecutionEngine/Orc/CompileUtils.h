//===- CompileUtils.h - Utilities for compiling IR in the JIT ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains utilities for compiling IR to object files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_COMPILEUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_COMPILEUTILS_H

#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include <memory>

namespace llvm {

class MemoryBuffer;
class Module;
class ObjectCache;
class TargetMachine;

namespace orc {

IRSymbolMapper::ManglingOptions
irManglingOptionsFromTargetOptions(const TargetOptions &Opts);

/// Simple compile functor: Takes a single IR module and returns an ObjectFile.
/// This compiler supports a single compilation thread and LLVMContext only.
/// For multithreaded compilation, use ConcurrentIRCompiler below.
class SimpleCompiler : public IRCompileLayer::IRCompiler {
public:
  using CompileResult = std::unique_ptr<MemoryBuffer>;

  /// Construct a simple compile functor with the given target.
  SimpleCompiler(TargetMachine &TM, ObjectCache *ObjCache = nullptr)
      : IRCompiler(irManglingOptionsFromTargetOptions(TM.Options)), TM(TM),
        ObjCache(ObjCache) {}

  /// Set an ObjectCache to query before compiling.
  void setObjectCache(ObjectCache *NewCache) { ObjCache = NewCache; }

  /// Compile a Module to an ObjectFile.
  Expected<CompileResult> operator()(Module &M) override;

private:
  IRSymbolMapper::ManglingOptions
  manglingOptionsForTargetMachine(const TargetMachine &TM);

  CompileResult tryToLoadFromObjectCache(const Module &M);
  void notifyObjectCompiled(const Module &M, const MemoryBuffer &ObjBuffer);

  TargetMachine &TM;
  ObjectCache *ObjCache = nullptr;
};

/// A SimpleCompiler that owns its TargetMachine.
///
/// This is convenient for clients who don't want to own their TargetMachines,
/// e.g. LLJIT.
class TMOwningSimpleCompiler : public SimpleCompiler {
public:
  TMOwningSimpleCompiler(std::unique_ptr<TargetMachine> TM,
                         ObjectCache *ObjCache = nullptr)
      : SimpleCompiler(*TM, ObjCache), TM(std::move(TM)) {}

private:
  // FIXME: shared because std::functions (and consequently
  // IRCompileLayer::CompileFunction) are not moveable.
  std::shared_ptr<llvm::TargetMachine> TM;
};

/// A thread-safe version of SimpleCompiler.
///
/// This class creates a new TargetMachine and SimpleCompiler instance for each
/// compile.
class ConcurrentIRCompiler : public IRCompileLayer::IRCompiler {
public:
  ConcurrentIRCompiler(JITTargetMachineBuilder JTMB,
                       ObjectCache *ObjCache = nullptr);

  void setObjectCache(ObjectCache *ObjCache) { this->ObjCache = ObjCache; }

  Expected<std::unique_ptr<MemoryBuffer>> operator()(Module &M) override;

private:
  JITTargetMachineBuilder JTMB;
  ObjectCache *ObjCache = nullptr;
};

} // end namespace orc

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_COMPILEUTILS_H
