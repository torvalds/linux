//==-- proto_to_cxx.h - Protobuf-C++ conversion ----------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines functions for converting between protobufs and C++.
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstddef>
#include <string>

namespace clang_fuzzer {
class Function;
class LoopFunction;

std::string FunctionToString(const Function &input);
std::string ProtoToCxx(const uint8_t *data, size_t size);
std::string LoopFunctionToString(const LoopFunction &input);
std::string LoopProtoToCxx(const uint8_t *data, size_t size);
}
