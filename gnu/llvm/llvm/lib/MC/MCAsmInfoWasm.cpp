//===-- MCAsmInfoWasm.cpp - Wasm asm properties -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines target asm properties related what form asm statements
// should take in general on Wasm-based targets
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmInfoWasm.h"
using namespace llvm;

void MCAsmInfoWasm::anchor() {}

MCAsmInfoWasm::MCAsmInfoWasm() {
  HasIdentDirective = true;
  HasNoDeadStrip = true;
  WeakRefDirective = "\t.weak\t";
  PrivateGlobalPrefix = ".L";
  PrivateLabelPrefix = ".L";
}
