//===--- Function.h - Bytecode function for the VM --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Function.h"
#include "Opcode.h"
#include "Program.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/Builtins.h"

using namespace clang;
using namespace clang::interp;

Function::Function(Program &P, const FunctionDecl *F, unsigned ArgSize,
                   llvm::SmallVectorImpl<PrimType> &&ParamTypes,
                   llvm::DenseMap<unsigned, ParamDescriptor> &&Params,
                   llvm::SmallVectorImpl<unsigned> &&ParamOffsets,
                   bool HasThisPointer, bool HasRVO, bool UnevaluatedBuiltin)
    : P(P), Loc(F->getBeginLoc()), F(F), ArgSize(ArgSize),
      ParamTypes(std::move(ParamTypes)), Params(std::move(Params)),
      ParamOffsets(std::move(ParamOffsets)), HasThisPointer(HasThisPointer),
      HasRVO(HasRVO), Variadic(F->isVariadic()),
      IsUnevaluatedBuiltin(UnevaluatedBuiltin) {}

Function::ParamDescriptor Function::getParamDescriptor(unsigned Offset) const {
  auto It = Params.find(Offset);
  assert(It != Params.end() && "Invalid parameter offset");
  return It->second;
}

SourceInfo Function::getSource(CodePtr PC) const {
  assert(PC >= getCodeBegin() && "PC does not belong to this function");
  assert(PC <= getCodeEnd() && "PC Does not belong to this function");
  assert(hasBody() && "Function has no body");
  unsigned Offset = PC - getCodeBegin();
  using Elem = std::pair<unsigned, SourceInfo>;
  auto It = llvm::lower_bound(SrcMap, Elem{Offset, {}}, llvm::less_first());
  if (It == SrcMap.end())
    return SrcMap.back().second;
  return It->second;
}

bool Function::isVirtual() const {
  if (const auto *M = dyn_cast<CXXMethodDecl>(F))
    return M->isVirtual();
  return false;
}
