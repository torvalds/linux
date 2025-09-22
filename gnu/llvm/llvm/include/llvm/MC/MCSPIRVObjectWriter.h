//===-- llvm/MC/MCSPIRVObjectWriter.h - SPIR-V Object Writer -----*- C++ *-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSPIRVOBJECTWRITER_H
#define LLVM_MC_MCSPIRVOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {

class MCSPIRVObjectTargetWriter : public MCObjectTargetWriter {
protected:
  explicit MCSPIRVObjectTargetWriter() {}

public:
  Triple::ObjectFormatType getFormat() const override { return Triple::SPIRV; }
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::SPIRV;
  }
};

class SPIRVObjectWriter final : public MCObjectWriter {
  support::endian::Writer W;
  std::unique_ptr<MCSPIRVObjectTargetWriter> TargetObjectWriter;

  struct VersionInfoType {
    unsigned Major = 0;
    unsigned Minor = 0;
    unsigned Bound = 0;
  } VersionInfo;

public:
  SPIRVObjectWriter(std::unique_ptr<MCSPIRVObjectTargetWriter> MOTW,
                    raw_pwrite_stream &OS)
      : W(OS, llvm::endianness::little), TargetObjectWriter(std::move(MOTW)) {}

  void setBuildVersion(unsigned Major, unsigned Minor, unsigned Bound);

private:
  void recordRelocation(MCAssembler &Asm, const MCFragment *Fragment,
                        const MCFixup &Fixup, MCValue Target,
                        uint64_t &FixedValue) override {}

  uint64_t writeObject(MCAssembler &Asm) override;
  void writeHeader(const MCAssembler &Asm);
};

/// Construct a new SPIR-V writer instance.
///
/// \param MOTW - The target specific SPIR-V writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
std::unique_ptr<MCObjectWriter>
createSPIRVObjectWriter(std::unique_ptr<MCSPIRVObjectTargetWriter> MOTW,
                        raw_pwrite_stream &OS);

} // namespace llvm

#endif
