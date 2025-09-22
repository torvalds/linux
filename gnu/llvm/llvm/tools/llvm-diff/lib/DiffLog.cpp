//===-- DiffLog.h - Difference Log Builder and accessories ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines the interface to the LLVM difference log builder.
//
//===----------------------------------------------------------------------===//

#include "DiffLog.h"
#include "DiffConsumer.h"
#include "llvm/ADT/StringRef.h"

using namespace llvm;

LogBuilder::~LogBuilder() {
  if (consumer)
    consumer->logf(*this);
}

StringRef LogBuilder::getFormat() const { return Format; }

unsigned LogBuilder::getNumArguments() const { return Arguments.size(); }
const Value *LogBuilder::getArgument(unsigned I) const { return Arguments[I]; }

DiffLogBuilder::~DiffLogBuilder() { consumer.logd(*this); }

void DiffLogBuilder::addMatch(const Instruction *L, const Instruction *R) {
  Diff.push_back(DiffRecord(L, R));
}
void DiffLogBuilder::addLeft(const Instruction *L) {
  // HACK: VS 2010 has a bug in the stdlib that requires this.
  Diff.push_back(DiffRecord(L, DiffRecord::second_type(nullptr)));
}
void DiffLogBuilder::addRight(const Instruction *R) {
  // HACK: VS 2010 has a bug in the stdlib that requires this.
  Diff.push_back(DiffRecord(DiffRecord::first_type(nullptr), R));
}

unsigned DiffLogBuilder::getNumLines() const { return Diff.size(); }

DiffChange DiffLogBuilder::getLineKind(unsigned I) const {
  return (Diff[I].first ? (Diff[I].second ? DC_match : DC_left)
                        : DC_right);
}
const Instruction *DiffLogBuilder::getLeft(unsigned I) const {
  return Diff[I].first;
}
const Instruction *DiffLogBuilder::getRight(unsigned I) const {
  return Diff[I].second;
}
