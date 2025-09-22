//===-- StructuralHash.cpp - IR Hashing -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/StructuralHash.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace {

// Basic hashing mechanism to detect structural change to the IR, used to verify
// pass return status consistency with actual change. In addition to being used
// by the MergeFunctions pass.

class StructuralHashImpl {
  uint64_t Hash;

  void hash(uint64_t V) { Hash = hashing::detail::hash_16_bytes(Hash, V); }

  // This will produce different values on 32-bit and 64-bit systens as
  // hash_combine returns a size_t. However, this is only used for
  // detailed hashing which, in-tree, only needs to distinguish between
  // differences in functions.
  template <typename T> void hashArbitaryType(const T &V) {
    hash(hash_combine(V));
  }

  void hashType(Type *ValueType) {
    hash(ValueType->getTypeID());
    if (ValueType->isIntegerTy())
      hash(ValueType->getIntegerBitWidth());
  }

public:
  StructuralHashImpl() : Hash(4) {}

  void updateOperand(Value *Operand) {
    hashType(Operand->getType());

    // The cases enumerated below are not exhaustive and are only aimed to
    // get decent coverage over the function.
    if (ConstantInt *ConstInt = dyn_cast<ConstantInt>(Operand)) {
      hashArbitaryType(ConstInt->getValue());
    } else if (ConstantFP *ConstFP = dyn_cast<ConstantFP>(Operand)) {
      hashArbitaryType(ConstFP->getValue());
    } else if (Argument *Arg = dyn_cast<Argument>(Operand)) {
      hash(Arg->getArgNo());
    } else if (Function *Func = dyn_cast<Function>(Operand)) {
      // Hashing the name will be deterministic as LLVM's hashing infrastructure
      // has explicit support for hashing strings and will not simply hash
      // the pointer.
      hashArbitaryType(Func->getName());
    }
  }

  void updateInstruction(const Instruction &Inst, bool DetailedHash) {
    hash(Inst.getOpcode());

    if (!DetailedHash)
      return;

    hashType(Inst.getType());

    // Handle additional properties of specific instructions that cause
    // semantic differences in the IR.
    if (const auto *ComparisonInstruction = dyn_cast<CmpInst>(&Inst))
      hash(ComparisonInstruction->getPredicate());

    for (const auto &Op : Inst.operands())
      updateOperand(Op);
  }

  // A function hash is calculated by considering only the number of arguments
  // and whether a function is varargs, the order of basic blocks (given by the
  // successors of each basic block in depth first order), and the order of
  // opcodes of each instruction within each of these basic blocks. This mirrors
  // the strategy FunctionComparator::compare() uses to compare functions by
  // walking the BBs in depth first order and comparing each instruction in
  // sequence. Because this hash currently does not look at the operands, it is
  // insensitive to things such as the target of calls and the constants used in
  // the function, which makes it useful when possibly merging functions which
  // are the same modulo constants and call targets.
  //
  // Note that different users of StructuralHash will want different behavior
  // out of it (i.e., MergeFunctions will want something different from PM
  // expensive checks for pass modification status). When modifying this
  // function, most changes should be gated behind an option and enabled
  // selectively.
  void update(const Function &F, bool DetailedHash) {
    // Declarations don't affect analyses.
    if (F.isDeclaration())
      return;

    hash(0x62642d6b6b2d6b72); // Function header

    hash(F.isVarArg());
    hash(F.arg_size());

    SmallVector<const BasicBlock *, 8> BBs;
    SmallPtrSet<const BasicBlock *, 16> VisitedBBs;

    // Walk the blocks in the same order as
    // FunctionComparator::cmpBasicBlocks(), accumulating the hash of the
    // function "structure." (BB and opcode sequence)
    BBs.push_back(&F.getEntryBlock());
    VisitedBBs.insert(BBs[0]);
    while (!BBs.empty()) {
      const BasicBlock *BB = BBs.pop_back_val();

      // This random value acts as a block header, as otherwise the partition of
      // opcodes into BBs wouldn't affect the hash, only the order of the
      // opcodes
      hash(45798);
      for (auto &Inst : *BB)
        updateInstruction(Inst, DetailedHash);

      for (const BasicBlock *Succ : successors(BB))
        if (VisitedBBs.insert(Succ).second)
          BBs.push_back(Succ);
    }
  }

  void update(const GlobalVariable &GV) {
    // Declarations and used/compiler.used don't affect analyses.
    // Since there are several `llvm.*` metadata, like `llvm.embedded.object`,
    // we ignore anything with the `.llvm` prefix
    if (GV.isDeclaration() || GV.getName().starts_with("llvm."))
      return;
    hash(23456); // Global header
    hash(GV.getValueType()->getTypeID());
  }

  void update(const Module &M, bool DetailedHash) {
    for (const GlobalVariable &GV : M.globals())
      update(GV);
    for (const Function &F : M)
      update(F, DetailedHash);
  }

  uint64_t getHash() const { return Hash; }
};

} // namespace

IRHash llvm::StructuralHash(const Function &F, bool DetailedHash) {
  StructuralHashImpl H;
  H.update(F, DetailedHash);
  return H.getHash();
}

IRHash llvm::StructuralHash(const Module &M, bool DetailedHash) {
  StructuralHashImpl H;
  H.update(M, DetailedHash);
  return H.getHash();
}
