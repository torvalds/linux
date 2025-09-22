//===- OptEmitter.h - Helper for emitting options. --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_OPTEMITTER_H
#define LLVM_UTILS_TABLEGEN_OPTEMITTER_H

namespace llvm {
class Record;
int CompareOptionRecords(Record *const *Av, Record *const *Bv);
} // namespace llvm
#endif
