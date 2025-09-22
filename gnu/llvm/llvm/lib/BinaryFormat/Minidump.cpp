//===-- Minidump.cpp - Minidump constants and structures ---------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/Minidump.h"

using namespace llvm::minidump;

constexpr uint32_t Header::MagicSignature;
constexpr uint16_t Header::MagicVersion;
