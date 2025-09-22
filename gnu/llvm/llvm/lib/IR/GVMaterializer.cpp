//===-- GVMaterializer.cpp - Base implementation for GV materializers -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Minimal implementation of the abstract interface for materializing
// GlobalValues.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/GVMaterializer.h"
using namespace llvm;

GVMaterializer::~GVMaterializer() = default;
