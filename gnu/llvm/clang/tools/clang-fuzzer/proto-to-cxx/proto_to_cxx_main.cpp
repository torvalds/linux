//==-- proto_to_cxx_main.cpp - Driver for protobuf-C++ conversion ----------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements a simple driver to print a C++ program from a protobuf.
//
//===----------------------------------------------------------------------===//
#include <fstream>
#include <iostream>
#include <streambuf>
#include <string>

#include "proto_to_cxx.h"

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    std::fstream in(argv[i]);
    std::string str((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    std::cout << "// " << argv[i] << std::endl;
    std::cout << clang_fuzzer::ProtoToCxx(
        reinterpret_cast<const uint8_t *>(str.data()), str.size());
  }
}

