//===- llvm/MC/MCDXContainerWriter.h - DXContainer Writer -*- C++ -------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDXCONTAINERWRITER_H
#define LLVM_MC_MCDXCONTAINERWRITER_H

#include "llvm/MC/MCObjectWriter.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

class raw_pwrite_stream;

class MCDXContainerTargetWriter : public MCObjectTargetWriter {
protected:
  MCDXContainerTargetWriter() {}

public:
  virtual ~MCDXContainerTargetWriter();

  Triple::ObjectFormatType getFormat() const override {
    return Triple::DXContainer;
  }
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::DXContainer;
  }
};

/// Construct a new DXContainer writer instance.
///
/// \param MOTW - The target specific DXContainer writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
std::unique_ptr<MCObjectWriter>
createDXContainerObjectWriter(std::unique_ptr<MCDXContainerTargetWriter> MOTW,
                              raw_pwrite_stream &OS);

} // end namespace llvm

#endif // LLVM_MC_MCDXCONTAINERWRITER_H
