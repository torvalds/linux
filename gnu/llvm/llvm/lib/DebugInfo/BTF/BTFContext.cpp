//===- BTFContext.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the BTFContext interface, this is used by
// llvm-objdump tool to print source code alongside disassembly.
// In fact, currently it is a simple wrapper for BTFParser instance.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/BTF/BTFContext.h"

#define DEBUG_TYPE "debug-info-btf-context"

using namespace llvm;
using object::ObjectFile;
using object::SectionedAddress;

DILineInfo BTFContext::getLineInfoForAddress(SectionedAddress Address,
                                             DILineInfoSpecifier Specifier) {
  const BTF::BPFLineInfo *LineInfo = BTF.findLineInfo(Address);
  DILineInfo Result;
  if (!LineInfo)
    return Result;

  Result.LineSource = BTF.findString(LineInfo->LineOff);
  Result.FileName = BTF.findString(LineInfo->FileNameOff);
  Result.Line = LineInfo->getLine();
  Result.Column = LineInfo->getCol();
  return Result;
}

DILineInfo BTFContext::getLineInfoForDataAddress(SectionedAddress Address) {
  // BTF does not convey such information.
  return {};
}

DILineInfoTable
BTFContext::getLineInfoForAddressRange(SectionedAddress Address, uint64_t Size,
                                       DILineInfoSpecifier Specifier) {
  // This function is used only from llvm-rtdyld utility and a few
  // JITEventListener implementations. Ignore it for now.
  return {};
}

DIInliningInfo
BTFContext::getInliningInfoForAddress(SectionedAddress Address,
                                      DILineInfoSpecifier Specifier) {
  // BTF does not convey such information
  return {};
}

std::vector<DILocal> BTFContext::getLocalsForAddress(SectionedAddress Address) {
  // BTF does not convey such information
  return {};
}

std::unique_ptr<BTFContext>
BTFContext::create(const ObjectFile &Obj,
                   std::function<void(Error)> ErrorHandler) {
  auto Ctx = std::make_unique<BTFContext>();
  BTFParser::ParseOptions Opts;
  Opts.LoadLines = true;
  if (Error E = Ctx->BTF.parse(Obj, Opts))
    ErrorHandler(std::move(E));
  return Ctx;
}
