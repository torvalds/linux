//===-- argdumper.cpp --------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/JSON.h"

using namespace llvm;

int main(int argc, char *argv[]) {
  json::Array Arguments;
  for (int i = 1; i < argc; i++) {
    Arguments.push_back(argv[i]);
  }
  llvm::outs() << json::Object({{"arguments", std::move(Arguments)}});
  return 0;
}
