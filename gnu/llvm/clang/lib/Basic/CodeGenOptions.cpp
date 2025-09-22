//===--- CodeGenOptions.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/CodeGenOptions.h"
#include <string.h>

namespace clang {

CodeGenOptions::CodeGenOptions() {
#define CODEGENOPT(Name, Bits, Default) Name = Default;
#define ENUM_CODEGENOPT(Name, Type, Bits, Default) set##Name(Default);
#include "clang/Basic/CodeGenOptions.def"

  RelocationModel = llvm::Reloc::PIC_;
  memcpy(CoverageVersion, "408*", 4);
}

void CodeGenOptions::resetNonModularOptions(StringRef ModuleFormat) {
  // First reset all CodeGen options only. The Debug options are handled later.
#define DEBUGOPT(Name, Bits, Default)
#define VALUE_DEBUGOPT(Name, Bits, Default)
#define ENUM_DEBUGOPT(Name, Type, Bits, Default)
#define CODEGENOPT(Name, Bits, Default) Name = Default;
#define ENUM_CODEGENOPT(Name, Type, Bits, Default) set##Name(Default);
// Do not reset AST affecting code generation options.
#define AFFECTING_VALUE_CODEGENOPT(Name, Bits, Default)
#include "clang/Basic/CodeGenOptions.def"

  // Next reset all debug options that can always be reset, because they never
  // affect the PCM.
#define DEBUGOPT(Name, Bits, Default)
#define VALUE_DEBUGOPT(Name, Bits, Default)
#define ENUM_DEBUGOPT(Name, Type, Bits, Default)
#define BENIGN_DEBUGOPT(Name, Bits, Default) Name = Default;
#define BENIGN_VALUE_DEBUGOPT(Name, Bits, Default) Name = Default;
#define BENIGN_ENUM_DEBUGOPT(Name, Type, Bits, Default) set##Name(Default);
#include "clang/Basic/DebugOptions.def"

  // Conditionally reset debug options that only matter when the debug info is
  // emitted into the PCM (-gmodules).
  if (ModuleFormat == "raw" && !DebugTypeExtRefs) {
#define DEBUGOPT(Name, Bits, Default) Name = Default;
#define VALUE_DEBUGOPT(Name, Bits, Default) Name = Default;
#define ENUM_DEBUGOPT(Name, Type, Bits, Default) set##Name(Default);
#define BENIGN_DEBUGOPT(Name, Bits, Default)
#define BENIGN_VALUE_DEBUGOPT(Name, Bits, Default)
#define BENIGN_ENUM_DEBUGOPT(Name, Type, Bits, Default)
#include "clang/Basic/DebugOptions.def"
  }

  RelocationModel = llvm::Reloc::PIC_;
  memcpy(CoverageVersion, "408*", 4);
}

}  // end namespace clang
