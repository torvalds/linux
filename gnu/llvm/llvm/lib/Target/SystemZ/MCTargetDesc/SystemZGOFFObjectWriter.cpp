//===- SystemZGOFFObjectWriter.cpp - SystemZ GOFF writer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SystemZMCTargetDesc.h"
#include "llvm/MC/MCGOFFObjectWriter.h"
#include <memory>

using namespace llvm;

namespace {
class SystemZGOFFObjectWriter : public MCGOFFObjectTargetWriter {
public:
  SystemZGOFFObjectWriter();
};
} // end anonymous namespace

SystemZGOFFObjectWriter::SystemZGOFFObjectWriter()
    : MCGOFFObjectTargetWriter() {}

std::unique_ptr<MCObjectTargetWriter> llvm::createSystemZGOFFObjectWriter() {
  return std::make_unique<SystemZGOFFObjectWriter>();
}
