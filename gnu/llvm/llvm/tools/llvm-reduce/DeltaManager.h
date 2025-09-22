//===- DeltaManager.h - Runs Delta Passes to reduce Input -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file calls each specialized Delta pass in order to reduce the input IR
// file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_REDUCE_DELTAMANAGER_H
#define LLVM_TOOLS_LLVM_REDUCE_DELTAMANAGER_H

namespace llvm {
class raw_ostream;
class TestRunner;

void printDeltaPasses(raw_ostream &OS);
void runDeltaPasses(TestRunner &Tester, int MaxPassIterations);
} // namespace llvm

#endif
