//===- llvm/IR/StructuralHash.h - IR Hashing --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides hashing of the LLVM IR structure to be used to check
// Passes modification status.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_STRUCTURALHASH_H
#define LLVM_IR_STRUCTURALHASH_H

#include <cstdint>

namespace llvm {

class Function;
class Module;

using IRHash = uint64_t;

/// Returns a hash of the function \p F.
/// \param F The function to hash.
/// \param DetailedHash Whether or not to encode additional information in the
/// hash. The additional information added into the hash when this flag is set
/// to true includes instruction and operand type information.
IRHash StructuralHash(const Function &F, bool DetailedHash = false);

/// Returns a hash of the module \p M by hashing all functions and global
/// variables contained within. \param M The module to hash. \param DetailedHash
/// Whether or not to encode additional information in the function hashes that
/// composed the module hash.
IRHash StructuralHash(const Module &M, bool DetailedHash = false);

} // end namespace llvm

#endif
