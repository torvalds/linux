//===-- IRMutator.h - Mutation engine for fuzzing IR ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provides the IRMutator class, which drives mutations on IR based on a
// configurable set of strategies. Some common strategies are also included
// here.
//
// Fuzzer-friendly (de)serialization functions are also provided, as these
// are usually needed when mutating IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_IRMUTATOR_H
#define LLVM_FUZZMUTATE_IRMUTATOR_H

#include "llvm/FuzzMutate/OpDescriptor.h"
#include "llvm/Support/ErrorHandling.h"
#include <optional>

namespace llvm {
class BasicBlock;
class Function;
class Instruction;
class Module;

struct RandomIRBuilder;

/// Base class for describing how to mutate a module. mutation functions for
/// each IR unit forward to the contained unit.
class IRMutationStrategy {
public:
  virtual ~IRMutationStrategy() = default;

  /// Provide a weight to bias towards choosing this strategy for a mutation.
  ///
  /// The value of the weight is arbitrary, but a good default is "the number of
  /// distinct ways in which this strategy can mutate a unit". This can also be
  /// used to prefer strategies that shrink the overall size of the result when
  /// we start getting close to \c MaxSize.
  virtual uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                             uint64_t CurrentWeight) = 0;

  /// @{
  /// Mutators for each IR unit. By default these forward to a contained
  /// instance of the next smaller unit.
  virtual void mutate(Module &M, RandomIRBuilder &IB);
  virtual void mutate(Function &F, RandomIRBuilder &IB);
  virtual void mutate(BasicBlock &BB, RandomIRBuilder &IB);
  virtual void mutate(Instruction &I, RandomIRBuilder &IB) {
    llvm_unreachable("Strategy does not implement any mutators");
  }
  /// @}
};

using TypeGetter = std::function<Type *(LLVMContext &)>;

/// Entry point for configuring and running IR mutations.
class IRMutator {
  std::vector<TypeGetter> AllowedTypes;
  std::vector<std::unique_ptr<IRMutationStrategy>> Strategies;

public:
  IRMutator(std::vector<TypeGetter> &&AllowedTypes,
            std::vector<std::unique_ptr<IRMutationStrategy>> &&Strategies)
      : AllowedTypes(std::move(AllowedTypes)),
        Strategies(std::move(Strategies)) {}

  /// Calculate the size of module as the number of objects in it, i.e.
  /// instructions, basic blocks, functions, and aliases.
  ///
  /// \param M module
  /// \return number of objects in module
  static size_t getModuleSize(const Module &M);

  /// Mutate given module. No change will be made if no strategy is selected.
  ///
  /// \param M  module to mutate
  /// \param Seed seed for random mutation
  /// \param MaxSize max module size (see getModuleSize)
  void mutateModule(Module &M, int Seed, size_t MaxSize);
};

/// Strategy that injects operations into the function.
class InjectorIRStrategy : public IRMutationStrategy {
  std::vector<fuzzerop::OpDescriptor> Operations;

  std::optional<fuzzerop::OpDescriptor> chooseOperation(Value *Src,
                                                        RandomIRBuilder &IB);

public:
  InjectorIRStrategy() : Operations(getDefaultOps()) {}
  InjectorIRStrategy(std::vector<fuzzerop::OpDescriptor> &&Operations)
      : Operations(std::move(Operations)) {}
  static std::vector<fuzzerop::OpDescriptor> getDefaultOps();

  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return Operations.size();
  }

  using IRMutationStrategy::mutate;
  void mutate(Function &F, RandomIRBuilder &IB) override;
  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;
};

/// Strategy that deletes instructions when the Module is too large.
class InstDeleterIRStrategy : public IRMutationStrategy {
public:
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override;

  using IRMutationStrategy::mutate;
  void mutate(Function &F, RandomIRBuilder &IB) override;
  void mutate(Instruction &Inst, RandomIRBuilder &IB) override;
};

/// Strategy that modifies instruction attributes and operands.
class InstModificationIRStrategy : public IRMutationStrategy {
public:
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 4;
  }

  using IRMutationStrategy::mutate;
  void mutate(Instruction &Inst, RandomIRBuilder &IB) override;
};

/// Strategy that generates new function calls and inserts function signatures
/// to the modules. If any signatures are present in the module it will be
/// called.
class InsertFunctionStrategy : public IRMutationStrategy {
public:
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 10;
  }

  using IRMutationStrategy::mutate;
  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;
};

/// Strategy to split a random block and insert a random CFG in between.
class InsertCFGStrategy : public IRMutationStrategy {
private:
  uint64_t MaxNumCases;
  enum CFGToSink { Return, DirectSink, SinkOrSelfLoop, EndOfCFGToLink };

public:
  InsertCFGStrategy(uint64_t MNC = 8) : MaxNumCases(MNC){};
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 5;
  }

  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;

private:
  void connectBlocksToSink(ArrayRef<BasicBlock *> Blocks, BasicBlock *Sink,
                           RandomIRBuilder &IB);
};

/// Strategy to insert PHI Nodes at the head of each basic block.
class InsertPHIStrategy : public IRMutationStrategy {
public:
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 2;
  }

  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;
};

/// Strategy to select a random instruction and add a new sink (user) to it to
/// increate data dependency.
class SinkInstructionStrategy : public IRMutationStrategy {
public:
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 2;
  }

  void mutate(Function &F, RandomIRBuilder &IB) override;
  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;
};

/// Strategy to randomly select a block and shuffle the operations without
/// affecting data dependency.
class ShuffleBlockStrategy : public IRMutationStrategy {
public:
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 2;
  }

  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;
};

/// Fuzzer friendly interface for the llvm bitcode parser.
///
/// \param Data Bitcode we are going to parse
/// \param Size Size of the 'Data' in bytes
/// \return New module or nullptr in case of error
std::unique_ptr<Module> parseModule(const uint8_t *Data, size_t Size,
                                    LLVMContext &Context);

/// Fuzzer friendly interface for the llvm bitcode printer.
///
/// \param M Module to print
/// \param Dest Location to store serialized module
/// \param MaxSize Size of the destination buffer
/// \return Number of bytes that were written. When module size exceeds MaxSize
///         returns 0 and leaves Dest unchanged.
size_t writeModule(const Module &M, uint8_t *Dest, size_t MaxSize);

/// Try to parse module and verify it. May output verification errors to the
/// errs().
/// \return New module or nullptr in case of error.
std::unique_ptr<Module> parseAndVerify(const uint8_t *Data, size_t Size,
                                       LLVMContext &Context);

} // namespace llvm

#endif // LLVM_FUZZMUTATE_IRMUTATOR_H
