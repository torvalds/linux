//===- loop_proto_to_llvm.h - Protobuf-C++ conversion -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines functions for converting between protobufs and LLVM IR.
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstddef>
#include <string>

namespace clang_fuzzer {
class LoopFunction;

std::string LoopFunctionToLLVMString(const LoopFunction &input);
std::string LoopProtoToLLVM(const uint8_t *data, size_t size);
}
