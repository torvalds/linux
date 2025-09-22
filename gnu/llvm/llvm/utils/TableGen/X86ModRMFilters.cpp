//===- X86ModRMFilters.cpp - Disassembler ModR/M filterss -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "X86ModRMFilters.h"

using namespace llvm::X86Disassembler;

void ModRMFilter::anchor() {}

void DumbFilter::anchor() {}

void ModFilter::anchor() {}

void ExtendedFilter::anchor() {}

void ExtendedRMFilter::anchor() {}

void ExactFilter::anchor() {}
