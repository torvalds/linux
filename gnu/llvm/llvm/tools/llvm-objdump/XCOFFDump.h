//===-- XCOFFDump.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_OBJDUMP_XCOFFDUMP_H
#define LLVM_TOOLS_LLVM_OBJDUMP_XCOFFDUMP_H

#include "llvm/Object/XCOFFObjectFile.h"

namespace llvm {

class formatted_raw_ostream;
class MCSubtargetInfo;
struct SymbolInfoTy;

namespace objdump {
std::optional<XCOFF::StorageMappingClass>
getXCOFFSymbolCsectSMC(const object::XCOFFObjectFile &Obj,
                       const object::SymbolRef &Sym);

std::optional<object::SymbolRef>
getXCOFFSymbolContainingSymbolRef(const object::XCOFFObjectFile &Obj,
                                  const object::SymbolRef &Sym);

bool isLabel(const object::XCOFFObjectFile &Obj, const object::SymbolRef &Sym);

std::string getXCOFFSymbolDescription(const SymbolInfoTy &SymbolInfo,
                                      StringRef SymbolName);

Error getXCOFFRelocationValueString(const object::XCOFFObjectFile &Obj,
                                    const object::RelocationRef &RelRef,
                                    bool SymbolDescription,
                                    llvm::SmallVectorImpl<char> &Result);

void dumpTracebackTable(ArrayRef<uint8_t> Bytes, uint64_t Address,
                        formatted_raw_ostream &OS, uint64_t End,
                        const MCSubtargetInfo &STI,
                        const object::XCOFFObjectFile *Obj);
} // namespace objdump
} // namespace llvm
#endif
