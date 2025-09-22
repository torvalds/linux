//===-- ExampleClangProtoFuzzer.cpp - Fuzz Clang --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a function that runs Clang on a single
///  input and uses libprotobuf-mutator to find new inputs. This function is
///  then linked into the Fuzzer library.
///
//===----------------------------------------------------------------------===//

#include "cxx_proto.pb.h"
#include "handle-cxx/handle_cxx.h"
#include "proto-to-cxx/proto_to_cxx.h"
#include "fuzzer-initialize/fuzzer_initialize.h"
#include "src/libfuzzer/libfuzzer_macro.h"

using namespace clang_fuzzer;

DEFINE_BINARY_PROTO_FUZZER(const Function& input) {
  auto S = FunctionToString(input);
  HandleCXX(S, "./test.cc", GetCLArgs());
}
