//===- DWARFDebugAranges.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

void DWARFAddressRange::dump(raw_ostream &OS, uint32_t AddressSize,
                             DIDumpOptions DumpOpts,
                             const DWARFObject *Obj) const {

  OS << (DumpOpts.DisplayRawContents ? " " : "[");
  DWARFFormValue::dumpAddress(OS, AddressSize, LowPC);
  OS << ", ";
  DWARFFormValue::dumpAddress(OS, AddressSize, HighPC);
  OS << (DumpOpts.DisplayRawContents ? "" : ")");

  if (Obj)
    DWARFFormValue::dumpAddressSection(*Obj, OS, DumpOpts, SectionIndex);
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const DWARFAddressRange &R) {
  R.dump(OS, /* AddressSize */ 8);
  return OS;
}
