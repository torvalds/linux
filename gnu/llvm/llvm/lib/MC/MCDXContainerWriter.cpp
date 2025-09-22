//===- llvm/MC/MCDXContainerWriter.cpp - DXContainer Writer -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCDXContainerWriter.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;

MCDXContainerTargetWriter::~MCDXContainerTargetWriter() {}

namespace {
class DXContainerObjectWriter : public MCObjectWriter {
  ::support::endian::Writer W;

  /// The target specific DXContainer writer instance.
  std::unique_ptr<MCDXContainerTargetWriter> TargetObjectWriter;

public:
  DXContainerObjectWriter(std::unique_ptr<MCDXContainerTargetWriter> MOTW,
                          raw_pwrite_stream &OS)
      : W(OS, llvm::endianness::little), TargetObjectWriter(std::move(MOTW)) {}

  ~DXContainerObjectWriter() override {}

private:
  void recordRelocation(MCAssembler &Asm, const MCFragment *Fragment,
                        const MCFixup &Fixup, MCValue Target,
                        uint64_t &FixedValue) override {}

  uint64_t writeObject(MCAssembler &Asm) override;
};
} // namespace

uint64_t DXContainerObjectWriter::writeObject(MCAssembler &Asm) {
  // Start the file size as the header plus the size of the part offsets.
  // Presently DXContainer files usually contain 7-10 parts. Reserving space for
  // 16 part offsets gives us a little room for growth.
  llvm::SmallVector<uint64_t, 16> PartOffsets;
  uint64_t PartOffset = 0;
  for (const MCSection &Sec : Asm) {
    uint64_t SectionSize = Asm.getSectionAddressSize(Sec);
    // Skip empty sections.
    if (SectionSize == 0)
      continue;

    assert(SectionSize < std::numeric_limits<uint32_t>::max() &&
           "Section size too large for DXContainer");

    PartOffsets.push_back(PartOffset);
    PartOffset += sizeof(dxbc::PartHeader) + SectionSize;
    PartOffset = alignTo(PartOffset, Align(4ul));
    // The DXIL part also writes a program header, so we need to include its
    // size when computing the offset for a part after the DXIL part.
    if (Sec.getName() == "DXIL")
      PartOffset += sizeof(dxbc::ProgramHeader);
  }
  assert(PartOffset < std::numeric_limits<uint32_t>::max() &&
         "Part data too large for DXContainer");

  uint64_t PartStart =
      sizeof(dxbc::Header) + (PartOffsets.size() * sizeof(uint32_t));
  uint64_t FileSize = PartStart + PartOffset;
  assert(FileSize < std::numeric_limits<uint32_t>::max() &&
         "File size too large for DXContainer");

  // Write the header.
  W.write<char>({'D', 'X', 'B', 'C'});
  // Write 16-bytes of 0's for the hash.
  W.OS.write_zeros(16);
  // Write 1.0 for file format version.
  W.write<uint16_t>(1u);
  W.write<uint16_t>(0u);
  // Write the file size.
  W.write<uint32_t>(static_cast<uint32_t>(FileSize));
  // Write the number of parts.
  W.write<uint32_t>(static_cast<uint32_t>(PartOffsets.size()));
  // Write the offsets for the part headers for each part.
  for (uint64_t Offset : PartOffsets)
    W.write<uint32_t>(static_cast<uint32_t>(PartStart + Offset));

  for (const MCSection &Sec : Asm) {
    uint64_t SectionSize = Asm.getSectionAddressSize(Sec);
    // Skip empty sections.
    if (SectionSize == 0)
      continue;

    unsigned Start = W.OS.tell();
    // Write section header.
    W.write<char>(ArrayRef<char>(Sec.getName().data(), 4));

    uint64_t PartSize = SectionSize;

    if (Sec.getName() == "DXIL")
      PartSize += sizeof(dxbc::ProgramHeader);
    // DXContainer parts should be 4-byte aligned.
    PartSize = alignTo(PartSize, Align(4));
    W.write<uint32_t>(static_cast<uint32_t>(PartSize));
    if (Sec.getName() == "DXIL") {
      dxbc::ProgramHeader Header;
      memset(reinterpret_cast<void *>(&Header), 0, sizeof(dxbc::ProgramHeader));

      const Triple &TT = Asm.getContext().getTargetTriple();
      VersionTuple Version = TT.getOSVersion();
      uint8_t MajorVersion = static_cast<uint8_t>(Version.getMajor());
      uint8_t MinorVersion =
          static_cast<uint8_t>(Version.getMinor().value_or(0));
      Header.Version =
          dxbc::ProgramHeader::getVersion(MajorVersion, MinorVersion);
      if (TT.hasEnvironment())
        Header.ShaderKind =
            static_cast<uint16_t>(TT.getEnvironment() - Triple::Pixel);

      // The program header's size field is in 32-bit words.
      Header.Size = (SectionSize + sizeof(dxbc::ProgramHeader) + 3) / 4;
      memcpy(Header.Bitcode.Magic, "DXIL", 4);
      VersionTuple DXILVersion = TT.getDXILVersion();
      Header.Bitcode.MajorVersion = DXILVersion.getMajor();
      Header.Bitcode.MinorVersion = DXILVersion.getMinor().value_or(0);
      Header.Bitcode.Offset = sizeof(dxbc::BitcodeHeader);
      Header.Bitcode.Size = SectionSize;
      if (sys::IsBigEndianHost)
        Header.swapBytes();
      W.write<char>(ArrayRef<char>(reinterpret_cast<char *>(&Header),
                                   sizeof(dxbc::ProgramHeader)));
    }
    Asm.writeSectionData(W.OS, &Sec);
    unsigned Size = W.OS.tell() - Start;
    W.OS.write_zeros(offsetToAlignment(Size, Align(4)));
  }
  return 0;
}

std::unique_ptr<MCObjectWriter> llvm::createDXContainerObjectWriter(
    std::unique_ptr<MCDXContainerTargetWriter> MOTW, raw_pwrite_stream &OS) {
  return std::make_unique<DXContainerObjectWriter>(std::move(MOTW), OS);
}
