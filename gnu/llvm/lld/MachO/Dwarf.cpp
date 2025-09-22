//===- DWARF.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Dwarf.h"
#include "InputFiles.h"
#include "InputSection.h"
#include "OutputSegment.h"

#include <memory>

using namespace lld;
using namespace lld::macho;
using namespace llvm;

std::unique_ptr<DwarfObject> DwarfObject::create(ObjFile *obj) {
  auto dObj = std::make_unique<DwarfObject>();
  bool hasDwarfInfo = false;
  // LLD only needs to extract the source file path and line numbers from the
  // debug info, so we initialize DwarfObject with just the sections necessary
  // to get that path. The debugger will locate the debug info via the object
  // file paths that we emit in our STABS symbols, so we don't need to process &
  // emit them ourselves.
  for (const InputSection *isec : obj->debugSections) {
    if (StringRef *s =
            StringSwitch<StringRef *>(isec->getName())
                .Case(section_names::debugInfo, &dObj->infoSection.Data)
                .Case(section_names::debugLine, &dObj->lineSection.Data)
                .Case(section_names::debugStrOffs, &dObj->strOffsSection.Data)
                .Case(section_names::debugAbbrev, &dObj->abbrevSection)
                .Case(section_names::debugStr, &dObj->strSection)
                .Default(nullptr)) {
      *s = toStringRef(isec->data);
      hasDwarfInfo = true;
    }
  }

  if (hasDwarfInfo)
    return dObj;
  return nullptr;
}
