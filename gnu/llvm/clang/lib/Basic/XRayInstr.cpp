//===--- XRayInstr.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is part of XRay, a function call instrumentation system.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/XRayInstr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"

namespace clang {

XRayInstrMask parseXRayInstrValue(StringRef Value) {
  XRayInstrMask ParsedKind =
      llvm::StringSwitch<XRayInstrMask>(Value)
          .Case("all", XRayInstrKind::All)
          .Case("custom", XRayInstrKind::Custom)
          .Case("function",
                XRayInstrKind::FunctionEntry | XRayInstrKind::FunctionExit)
          .Case("function-entry", XRayInstrKind::FunctionEntry)
          .Case("function-exit", XRayInstrKind::FunctionExit)
          .Case("typed", XRayInstrKind::Typed)
          .Case("none", XRayInstrKind::None)
          .Default(XRayInstrKind::None);
  return ParsedKind;
}

void serializeXRayInstrValue(XRayInstrSet Set,
                             SmallVectorImpl<StringRef> &Values) {
  if (Set.Mask == XRayInstrKind::All) {
    Values.push_back("all");
    return;
  }

  if (Set.Mask == XRayInstrKind::None) {
    Values.push_back("none");
    return;
  }

  if (Set.has(XRayInstrKind::Custom))
    Values.push_back("custom");

  if (Set.has(XRayInstrKind::Typed))
    Values.push_back("typed");

  if (Set.has(XRayInstrKind::FunctionEntry) &&
      Set.has(XRayInstrKind::FunctionExit))
    Values.push_back("function");
  else if (Set.has(XRayInstrKind::FunctionEntry))
    Values.push_back("function-entry");
  else if (Set.has(XRayInstrKind::FunctionExit))
    Values.push_back("function-exit");
}
} // namespace clang
