//===-- ExampleClangLLVMProtoFuzzer.cpp - Fuzz Clang ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///  This file implements a function that compiles a single LLVM IR string as
///  input and uses libprotobuf-mutator to find new inputs. This function is
///  then linked into the Fuzzer library.
///
//===----------------------------------------------------------------------===//

#include "cxx_loop_proto.pb.h"
#include "fuzzer-initialize/fuzzer_initialize.h"
#include "handle-llvm/handle_llvm.h"
#include "proto-to-llvm/loop_proto_to_llvm.h"
#include "src/libfuzzer/libfuzzer_macro.h"

using namespace clang_fuzzer;

DEFINE_BINARY_PROTO_FUZZER(const LoopFunction &input) {
  auto S = LoopFunctionToLLVMString(input);
  HandleLLVM(S, GetCLArgs());
}
