//===- RandomIRBuilder.h - Utils for randomly mutation IR -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provides the Mutator class, which is used to mutate IR for fuzzing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_RANDOMIRBUILDER_H
#define LLVM_FUZZMUTATE_RANDOMIRBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include <random>

namespace llvm {
class AllocaInst;
class BasicBlock;
class Function;
class GlobalVariable;
class Instruction;
class LLVMContext;
class Module;
class Type;
class Value;

namespace fuzzerop {
class SourcePred;
}

using RandomEngine = std::mt19937;

struct RandomIRBuilder {
  RandomEngine Rand;
  SmallVector<Type *, 16> KnownTypes;

  uint64_t MinArgNum = 0;
  uint64_t MaxArgNum = 5;
  uint64_t MinFunctionNum = 1;

  RandomIRBuilder(int Seed, ArrayRef<Type *> AllowedTypes)
      : Rand(Seed), KnownTypes(AllowedTypes.begin(), AllowedTypes.end()) {}

  // TODO: Try to make this a bit less of a random mishmash of functions.

  /// Create a stack memory at the head of the function, store \c Init to the
  /// memory if provided.
  AllocaInst *createStackMemory(Function *F, Type *Ty, Value *Init = nullptr);
  /// Find or create a global variable. It will be initialized by random
  /// constants that satisfies \c Pred. It will also report whether this global
  /// variable found or created.
  std::pair<GlobalVariable *, bool>
  findOrCreateGlobalVariable(Module *M, ArrayRef<Value *> Srcs,
                             fuzzerop::SourcePred Pred);
  enum SourceType {
    SrcFromInstInCurBlock,
    FunctionArgument,
    InstInDominator,
    SrcFromGlobalVariable,
    NewConstOrStack,
    EndOfValueSource,
  };
  /// Find a "source" for some operation, which will be used in one of the
  /// operation's operands. This either selects an instruction in \c Insts or
  /// returns some new arbitrary Value.
  Value *findOrCreateSource(BasicBlock &BB, ArrayRef<Instruction *> Insts);
  /// Find a "source" for some operation, which will be used in one of the
  /// operation's operands. This either selects an instruction in \c Insts that
  /// matches \c Pred, or returns some new Value that matches \c Pred. The
  /// values in \c Srcs should be source operands that have already been
  /// selected.
  Value *findOrCreateSource(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                            ArrayRef<Value *> Srcs, fuzzerop::SourcePred Pred,
                            bool allowConstant = true);
  /// Create some Value suitable as a source for some operation.
  Value *newSource(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                   ArrayRef<Value *> Srcs, fuzzerop::SourcePred Pred,
                   bool allowConstant = true);

  enum SinkType {
    /// TODO: Also consider pointers in function argument.
    SinkToInstInCurBlock,
    PointersInDominator,
    InstInDominatee,
    NewStore,
    SinkToGlobalVariable,
    EndOfValueSink,
  };
  /// Find a viable user for \c V in \c Insts, which should all be contained in
  /// \c BB. This may also create some new instruction in \c BB and use that.
  Instruction *connectToSink(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                             Value *V);
  /// Create a user for \c V in \c BB.
  Instruction *newSink(BasicBlock &BB, ArrayRef<Instruction *> Insts, Value *V);
  Value *findPointer(BasicBlock &BB, ArrayRef<Instruction *> Insts);
  /// Return a uniformly choosen type from \c AllowedTypes
  Type *randomType();
  Function *createFunctionDeclaration(Module &M, uint64_t ArgNum);
  Function *createFunctionDeclaration(Module &M);
  Function *createFunctionDefinition(Module &M, uint64_t ArgNum);
  Function *createFunctionDefinition(Module &M);
};

} // namespace llvm

#endif // LLVM_FUZZMUTATE_RANDOMIRBUILDER_H
