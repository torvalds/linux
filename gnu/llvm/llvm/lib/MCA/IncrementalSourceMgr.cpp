//===-------------------- IncrementalSourceMgr.cpp ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines some implementations for IncrementalSourceMgr.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/IncrementalSourceMgr.h"
#ifndef NDEBUG
#include "llvm/Support/Format.h"
#endif

using namespace llvm;
using namespace llvm::mca;

void IncrementalSourceMgr::clear() {
  Staging.clear();
  InstStorage.clear();
  TotalCounter = 0U;
  EOS = false;
}

void IncrementalSourceMgr::updateNext() {
  ++TotalCounter;
  Instruction *I = Staging.front();
  Staging.pop_front();
  I->reset();

  if (InstFreedCB)
    InstFreedCB(I);
}

#ifndef NDEBUG
void IncrementalSourceMgr::printStatistic(raw_ostream &OS) {
  unsigned MaxInstStorageSize = InstStorage.size();
  if (MaxInstStorageSize <= TotalCounter) {
    auto Ratio = double(MaxInstStorageSize) / double(TotalCounter);
    OS << "Cache ratio = " << MaxInstStorageSize << " / " << TotalCounter
       << llvm::format(" (%.2f%%)", (1.0 - Ratio) * 100.0) << "\n";
  } else {
    OS << "Error: Number of created instructions "
       << "are larger than the number of issued instructions\n";
  }
}
#endif
