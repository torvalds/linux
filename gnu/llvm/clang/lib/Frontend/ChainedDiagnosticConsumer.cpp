//===- ChainedDiagnosticConsumer.cpp - Chain Diagnostic Clients -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/ChainedDiagnosticConsumer.h"

using namespace clang;

void ChainedDiagnosticConsumer::anchor() { }
