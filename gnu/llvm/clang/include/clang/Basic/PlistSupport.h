//===- PlistSupport.h - Plist Output Utilities ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_PLISTSUPPORT_H
#define LLVM_CLANG_BASIC_PLISTSUPPORT_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

namespace clang {
namespace markup {

using FIDMap = llvm::DenseMap<FileID, unsigned>;

inline unsigned AddFID(FIDMap &FIDs, SmallVectorImpl<FileID> &V,
                   FileID FID) {
  FIDMap::iterator I = FIDs.find(FID);
  if (I != FIDs.end())
    return I->second;
  unsigned NewValue = V.size();
  FIDs[FID] = NewValue;
  V.push_back(FID);
  return NewValue;
}

inline unsigned AddFID(FIDMap &FIDs, SmallVectorImpl<FileID> &V,
                   const SourceManager &SM, SourceLocation L) {
  FileID FID = SM.getFileID(SM.getExpansionLoc(L));
  return AddFID(FIDs, V, FID);
}

inline unsigned GetFID(const FIDMap &FIDs, FileID FID) {
  FIDMap::const_iterator I = FIDs.find(FID);
  assert(I != FIDs.end());
  return I->second;
}

inline unsigned GetFID(const FIDMap &FIDs, const SourceManager &SM,
                       SourceLocation L) {
  FileID FID = SM.getFileID(SM.getExpansionLoc(L));
  return GetFID(FIDs, FID);
}

inline raw_ostream &Indent(raw_ostream &o, const unsigned indent) {
  for (unsigned i = 0; i < indent; ++i)
    o << ' ';
  return o;
}

inline raw_ostream &EmitPlistHeader(raw_ostream &o) {
  static const char *PlistHeader =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" "
      "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
      "<plist version=\"1.0\">\n";
  return o << PlistHeader;
}

inline raw_ostream &EmitInteger(raw_ostream &o, int64_t value) {
  o << "<integer>";
  o << value;
  o << "</integer>";
  return o;
}

inline raw_ostream &EmitString(raw_ostream &o, StringRef s) {
  o << "<string>";
  for (char c : s) {
    switch (c) {
    default:
      o << c;
      break;
    case '&':
      o << "&amp;";
      break;
    case '<':
      o << "&lt;";
      break;
    case '>':
      o << "&gt;";
      break;
    case '\'':
      o << "&apos;";
      break;
    case '\"':
      o << "&quot;";
      break;
    }
  }
  o << "</string>";
  return o;
}

inline void EmitLocation(raw_ostream &o, const SourceManager &SM,
                         SourceLocation L, const FIDMap &FM, unsigned indent) {
  if (L.isInvalid()) return;

  FullSourceLoc Loc(SM.getExpansionLoc(L), const_cast<SourceManager &>(SM));

  Indent(o, indent) << "<dict>\n";
  Indent(o, indent) << " <key>line</key>";
  EmitInteger(o, Loc.getExpansionLineNumber()) << '\n';
  Indent(o, indent) << " <key>col</key>";
  EmitInteger(o, Loc.getExpansionColumnNumber()) << '\n';
  Indent(o, indent) << " <key>file</key>";
  EmitInteger(o, GetFID(FM, SM, Loc)) << '\n';
  Indent(o, indent) << "</dict>\n";
}

inline void EmitRange(raw_ostream &o, const SourceManager &SM,
                      CharSourceRange R, const FIDMap &FM, unsigned indent) {
  if (R.isInvalid()) return;

  assert(R.isCharRange() && "cannot handle a token range");
  Indent(o, indent) << "<array>\n";
  EmitLocation(o, SM, R.getBegin(), FM, indent + 1);

  // The ".getLocWithOffset(-1)" emulates the behavior of an off-by-one bug
  // in Lexer that is already fixed. It is here for backwards compatibility
  // even though it is incorrect.
  EmitLocation(o, SM, R.getEnd().getLocWithOffset(-1), FM, indent + 1);
  Indent(o, indent) << "</array>\n";
}

} // namespace markup
} // namespace clang

#endif // LLVM_CLANG_BASIC_PLISTSUPPORT_H
