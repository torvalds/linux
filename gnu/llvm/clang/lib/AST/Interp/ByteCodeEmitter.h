//===--- ByteCodeEmitter.h - Instruction emitter for the VM -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the instruction emitters.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_LINKEMITTER_H
#define LLVM_CLANG_AST_INTERP_LINKEMITTER_H

#include "Context.h"
#include "PrimType.h"
#include "Program.h"
#include "Source.h"

namespace clang {
namespace interp {
enum Opcode : uint32_t;

/// An emitter which links the program to bytecode for later use.
class ByteCodeEmitter {
protected:
  using LabelTy = uint32_t;
  using AddrTy = uintptr_t;
  using Local = Scope::Local;

public:
  /// Compiles the function into the module.
  Function *compileFunc(const FunctionDecl *FuncDecl);

protected:
  ByteCodeEmitter(Context &Ctx, Program &P) : Ctx(Ctx), P(P) {}

  virtual ~ByteCodeEmitter() {}

  /// Define a label.
  void emitLabel(LabelTy Label);
  /// Create a label.
  LabelTy getLabel() { return ++NextLabel; }

  /// Methods implemented by the compiler.
  virtual bool visitFunc(const FunctionDecl *E) = 0;
  virtual bool visitExpr(const Expr *E) = 0;
  virtual bool visitDeclAndReturn(const VarDecl *E, bool ConstantContext) = 0;

  /// Emits jumps.
  bool jumpTrue(const LabelTy &Label);
  bool jumpFalse(const LabelTy &Label);
  bool jump(const LabelTy &Label);
  bool fallthrough(const LabelTy &Label);

  /// We're always emitting bytecode.
  bool isActive() const { return true; }

  /// Callback for local registration.
  Local createLocal(Descriptor *D);

  /// Parameter indices.
  llvm::DenseMap<const ParmVarDecl *, ParamOffset> Params;
  /// Lambda captures.
  llvm::DenseMap<const ValueDecl *, ParamOffset> LambdaCaptures;
  /// Offset of the This parameter in a lambda record.
  ParamOffset LambdaThisCapture{0, false};
  /// Local descriptors.
  llvm::SmallVector<SmallVector<Local, 8>, 2> Descriptors;

private:
  /// Current compilation context.
  Context &Ctx;
  /// Program to link to.
  Program &P;
  /// Index of the next available label.
  LabelTy NextLabel = 0;
  /// Offset of the next local variable.
  unsigned NextLocalOffset = 0;
  /// Label information for linker.
  llvm::DenseMap<LabelTy, unsigned> LabelOffsets;
  /// Location of label relocations.
  llvm::DenseMap<LabelTy, llvm::SmallVector<unsigned, 5>> LabelRelocs;
  /// Program code.
  std::vector<std::byte> Code;
  /// Opcode to expression mapping.
  SourceMap SrcMap;

  /// Returns the offset for a jump or records a relocation.
  int32_t getOffset(LabelTy Label);

  /// Emits an opcode.
  template <typename... Tys>
  bool emitOp(Opcode Op, const Tys &... Args, const SourceInfo &L);

protected:
#define GET_LINK_PROTO
#include "Opcodes.inc"
#undef GET_LINK_PROTO
};

} // namespace interp
} // namespace clang

#endif
