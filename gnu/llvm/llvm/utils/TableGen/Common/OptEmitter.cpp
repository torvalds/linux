//===- OptEmitter.cpp - Helper for emitting options.----------- -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OptEmitter.h"
#include "llvm/ADT/Twine.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <cctype>
#include <cstring>

namespace llvm {

// Ordering on Info. The logic should match with the consumer-side function in
// llvm/Option/OptTable.h.
// FIXME: Make this take StringRefs instead of null terminated strings to
// simplify callers.
static int StrCmpOptionName(const char *A, const char *B) {
  const char *X = A, *Y = B;
  char a = tolower(*A), b = tolower(*B);
  while (a == b) {
    if (a == '\0')
      return strcmp(A, B);

    a = tolower(*++X);
    b = tolower(*++Y);
  }

  if (a == '\0') // A is a prefix of B.
    return 1;
  if (b == '\0') // B is a prefix of A.
    return -1;

  // Otherwise lexicographic.
  return (a < b) ? -1 : 1;
}

int CompareOptionRecords(Record *const *Av, Record *const *Bv) {
  const Record *A = *Av;
  const Record *B = *Bv;

  // Sentinel options precede all others and are only ordered by precedence.
  bool ASent = A->getValueAsDef("Kind")->getValueAsBit("Sentinel");
  bool BSent = B->getValueAsDef("Kind")->getValueAsBit("Sentinel");
  if (ASent != BSent)
    return ASent ? -1 : 1;

  // Compare options by name, unless they are sentinels.
  if (!ASent)
    if (int Cmp = StrCmpOptionName(A->getValueAsString("Name").str().c_str(),
                                   B->getValueAsString("Name").str().c_str()))
      return Cmp;

  if (!ASent) {
    std::vector<StringRef> APrefixes = A->getValueAsListOfStrings("Prefixes");
    std::vector<StringRef> BPrefixes = B->getValueAsListOfStrings("Prefixes");

    for (std::vector<StringRef>::const_iterator APre = APrefixes.begin(),
                                                AEPre = APrefixes.end(),
                                                BPre = BPrefixes.begin(),
                                                BEPre = BPrefixes.end();
         APre != AEPre && BPre != BEPre; ++APre, ++BPre) {
      if (int Cmp = StrCmpOptionName(APre->str().c_str(), BPre->str().c_str()))
        return Cmp;
    }
  }

  // Then by the kind precedence;
  int APrec = A->getValueAsDef("Kind")->getValueAsInt("Precedence");
  int BPrec = B->getValueAsDef("Kind")->getValueAsInt("Precedence");
  if (APrec == BPrec && A->getValueAsListOfStrings("Prefixes") ==
                            B->getValueAsListOfStrings("Prefixes")) {
    PrintError(A->getLoc(), Twine("Option is equivalent to"));
    PrintError(B->getLoc(), Twine("Other defined here"));
    PrintFatalError("Equivalent Options found.");
  }
  return APrec < BPrec ? -1 : 1;
}

} // namespace llvm
