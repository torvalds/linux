//===--- EvalEmitter.h - Instruction emitter for the VM ---------*- C++ -*-===//
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

#ifndef LLVM_CLANG_AST_INTERP_EVALEMITTER_H
#define LLVM_CLANG_AST_INTERP_EVALEMITTER_H

#include "EvaluationResult.h"
#include "InterpState.h"
#include "PrimType.h"
#include "Source.h"
#include "llvm/Support/Error.h"

namespace clang {
namespace interp {
class Context;
class Function;
class InterpStack;
class Program;
enum Opcode : uint32_t;

/// An emitter which evaluates opcodes as they are emitted.
class EvalEmitter : public SourceMapper {
public:
  using LabelTy = uint32_t;
  using AddrTy = uintptr_t;
  using Local = Scope::Local;

  EvaluationResult interpretExpr(const Expr *E,
                                 bool ConvertResultToRValue = false);
  EvaluationResult interpretDecl(const VarDecl *VD, bool CheckFullyInitialized);

  /// Clean up all resources.
  void cleanup();

  InterpState &getState() { return S; }

protected:
  EvalEmitter(Context &Ctx, Program &P, State &Parent, InterpStack &Stk);

  virtual ~EvalEmitter();

  /// Define a label.
  void emitLabel(LabelTy Label);
  /// Create a label.
  LabelTy getLabel();

  /// Methods implemented by the compiler.
  virtual bool visitExpr(const Expr *E) = 0;
  virtual bool visitDeclAndReturn(const VarDecl *VD, bool ConstantContext) = 0;
  virtual bool visitFunc(const FunctionDecl *F) = 0;

  /// Emits jumps.
  bool jumpTrue(const LabelTy &Label);
  bool jumpFalse(const LabelTy &Label);
  bool jump(const LabelTy &Label);
  bool fallthrough(const LabelTy &Label);

  /// Since expressions can only jump forward, predicated execution is
  /// used to deal with if-else statements.
  bool isActive() const { return CurrentLabel == ActiveLabel; }

  /// Callback for registering a local.
  Local createLocal(Descriptor *D);

  /// Returns the source location of the current opcode.
  SourceInfo getSource(const Function *F, CodePtr PC) const override {
    return (F && F->hasBody()) ? F->getSource(PC) : CurrentSource;
  }

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
  /// Current program.
  Program &P;
  /// Callee evaluation state.
  InterpState S;
  /// Location to write the result to.
  EvaluationResult EvalResult;
  /// Whether the result should be converted to an RValue.
  bool ConvertResultToRValue = false;
  /// Whether we should check if the result has been fully
  /// initialized.
  bool CheckFullyInitialized = false;

  /// Temporaries which require storage.
  llvm::DenseMap<unsigned, std::unique_ptr<char[]>> Locals;

  Block *getLocal(unsigned Index) const {
    auto It = Locals.find(Index);
    assert(It != Locals.end() && "Missing local variable");
    return reinterpret_cast<Block *>(It->second.get());
  }

  void updateGlobalTemporaries();

  // The emitter always tracks the current instruction and sets OpPC to a token
  // value which is mapped to the location of the opcode being evaluated.
  CodePtr OpPC;
  /// Location of the current instruction.
  SourceInfo CurrentSource;

  /// Next label ID to generate - first label is 1.
  LabelTy NextLabel = 1;
  /// Label being executed - 0 is the entry label.
  LabelTy CurrentLabel = 0;
  /// Active block which should be executed.
  LabelTy ActiveLabel = 0;

protected:
#define GET_EVAL_PROTO
#include "Opcodes.inc"
#undef GET_EVAL_PROTO
};

} // namespace interp
} // namespace clang

#endif
