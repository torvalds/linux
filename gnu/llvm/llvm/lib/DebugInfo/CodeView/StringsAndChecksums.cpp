//===- StringsAndChecksums.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/Support/Error.h"
#include <cassert>

using namespace llvm;
using namespace llvm::codeview;

StringsAndChecksumsRef::StringsAndChecksumsRef() = default;

StringsAndChecksumsRef::StringsAndChecksumsRef(
    const DebugStringTableSubsectionRef &Strings)
    : Strings(&Strings) {}

StringsAndChecksumsRef::StringsAndChecksumsRef(
    const DebugStringTableSubsectionRef &Strings,
    const DebugChecksumsSubsectionRef &Checksums)
    : Strings(&Strings), Checksums(&Checksums) {}

void StringsAndChecksumsRef::initializeStrings(
    const DebugSubsectionRecord &SR) {
  assert(SR.kind() == DebugSubsectionKind::StringTable);
  assert(!Strings && "Found a string table even though we already have one!");

  OwnedStrings = std::make_shared<DebugStringTableSubsectionRef>();
  consumeError(OwnedStrings->initialize(SR.getRecordData()));
  Strings = OwnedStrings.get();
}

void StringsAndChecksumsRef::reset() {
  resetStrings();
  resetChecksums();
}

void StringsAndChecksumsRef::resetStrings() {
  OwnedStrings.reset();
  Strings = nullptr;
}

void StringsAndChecksumsRef::resetChecksums() {
  OwnedChecksums.reset();
  Checksums = nullptr;
}

void StringsAndChecksumsRef::setStrings(
    const DebugStringTableSubsectionRef &StringsRef) {
  OwnedStrings = std::make_shared<DebugStringTableSubsectionRef>();
  *OwnedStrings = StringsRef;
  Strings = OwnedStrings.get();
}

void StringsAndChecksumsRef::setChecksums(
    const DebugChecksumsSubsectionRef &CS) {
  OwnedChecksums = std::make_shared<DebugChecksumsSubsectionRef>();
  *OwnedChecksums = CS;
  Checksums = OwnedChecksums.get();
}

void StringsAndChecksumsRef::initializeChecksums(
    const DebugSubsectionRecord &FCR) {
  assert(FCR.kind() == DebugSubsectionKind::FileChecksums);
  if (Checksums)
    return;

  OwnedChecksums = std::make_shared<DebugChecksumsSubsectionRef>();
  consumeError(OwnedChecksums->initialize(FCR.getRecordData()));
  Checksums = OwnedChecksums.get();
}
