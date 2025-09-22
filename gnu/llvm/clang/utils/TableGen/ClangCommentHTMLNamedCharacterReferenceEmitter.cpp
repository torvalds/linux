//===--- ClangCommentHTMLNamedCharacterReferenceEmitter.cpp -----------------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits an efficient function to translate HTML named
// character references to UTF-8 sequences.
//
//===----------------------------------------------------------------------===//

#include "TableGenBackends.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <vector>

using namespace llvm;

/// Convert a code point to the corresponding UTF-8 sequence represented
/// as a C string literal.
///
/// \returns true on success.
static bool translateCodePointToUTF8(unsigned CodePoint,
                                     SmallVectorImpl<char> &CLiteral) {
  char Translated[UNI_MAX_UTF8_BYTES_PER_CODE_POINT];
  char *TranslatedPtr = Translated;
  if (!ConvertCodePointToUTF8(CodePoint, TranslatedPtr))
    return false;

  StringRef UTF8(Translated, TranslatedPtr - Translated);

  raw_svector_ostream OS(CLiteral);
  OS << "\"";
  for (size_t i = 0, e = UTF8.size(); i != e; ++i) {
    OS << "\\x";
    OS.write_hex(static_cast<unsigned char>(UTF8[i]));
  }
  OS << "\"";

  return true;
}

void clang::EmitClangCommentHTMLNamedCharacterReferences(RecordKeeper &Records,
                                                         raw_ostream &OS) {
  std::vector<Record *> Tags = Records.getAllDerivedDefinitions("NCR");
  std::vector<StringMatcher::StringPair> NameToUTF8;
  SmallString<32> CLiteral;
  for (std::vector<Record *>::iterator I = Tags.begin(), E = Tags.end();
       I != E; ++I) {
    Record &Tag = **I;
    std::string Spelling = std::string(Tag.getValueAsString("Spelling"));
    uint64_t CodePoint = Tag.getValueAsInt("CodePoint");
    CLiteral.clear();
    CLiteral.append("return ");
    if (!translateCodePointToUTF8(CodePoint, CLiteral)) {
      SrcMgr.PrintMessage(Tag.getLoc().front(),
                          SourceMgr::DK_Error,
                          Twine("invalid code point"));
      continue;
    }
    CLiteral.append(";");

    StringMatcher::StringPair Match(Spelling, std::string(CLiteral));
    NameToUTF8.push_back(Match);
  }

  emitSourceFileHeader("HTML named character reference to UTF-8 translation",
                       OS, Records);

  OS << "StringRef translateHTMLNamedCharacterReferenceToUTF8(\n"
        "                                             StringRef Name) {\n";
  StringMatcher("Name", NameToUTF8, OS).Emit();
  OS << "  return StringRef();\n"
     << "}\n\n";
}
