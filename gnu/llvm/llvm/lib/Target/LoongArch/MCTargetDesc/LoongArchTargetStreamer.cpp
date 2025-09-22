//===-- LoongArchTargetStreamer.cpp - LoongArch Target Streamer Methods ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides LoongArch specific target streamer methods.
//
//===----------------------------------------------------------------------===//

#include "LoongArchTargetStreamer.h"

using namespace llvm;

LoongArchTargetStreamer::LoongArchTargetStreamer(MCStreamer &S)
    : MCTargetStreamer(S) {}

void LoongArchTargetStreamer::setTargetABI(LoongArchABI::ABI ABI) {
  assert(ABI != LoongArchABI::ABI_Unknown &&
         "Improperly initialized target ABI");
  TargetABI = ABI;
}
