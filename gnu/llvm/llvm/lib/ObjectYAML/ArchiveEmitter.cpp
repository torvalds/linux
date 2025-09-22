//===- ArchiveEmitter.cpp ---------------------------- --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/ArchiveYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace ArchYAML;

namespace llvm {
namespace yaml {

bool yaml2archive(ArchYAML::Archive &Doc, raw_ostream &Out, ErrorHandler EH) {
  Out.write(Doc.Magic.data(), Doc.Magic.size());

  if (Doc.Content) {
    Doc.Content->writeAsBinary(Out);
    return true;
  }

  if (!Doc.Members)
    return true;

  auto WriteField = [&](StringRef Field, uint8_t Size) {
    Out.write(Field.data(), Field.size());
    for (size_t I = Field.size(); I != Size; ++I)
      Out.write(' ');
  };

  for (const Archive::Child &C : *Doc.Members) {
    for (auto &P : C.Fields)
      WriteField(P.second.Value, P.second.MaxLength);

    if (C.Content)
      C.Content->writeAsBinary(Out);
    if (C.PaddingByte)
      Out.write(*C.PaddingByte);
  }

  return true;
}

} // namespace yaml
} // namespace llvm
