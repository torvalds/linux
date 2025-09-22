//===- DXContainerEmitter.cpp - Convert YAML to a DXContainer -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Binary emitter for yaml to DXContainer binary
///
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/MC/DXContainerPSVInfo.h"
#include "llvm/ObjectYAML/ObjectYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class DXContainerWriter {
public:
  DXContainerWriter(DXContainerYAML::Object &ObjectFile)
      : ObjectFile(ObjectFile) {}

  Error write(raw_ostream &OS);

private:
  DXContainerYAML::Object &ObjectFile;

  Error computePartOffsets();
  Error validatePartOffsets();
  Error validateSize(uint32_t Computed);

  void writeHeader(raw_ostream &OS);
  void writeParts(raw_ostream &OS);
};
} // namespace

Error DXContainerWriter::validateSize(uint32_t Computed) {
  if (!ObjectFile.Header.FileSize)
    ObjectFile.Header.FileSize = Computed;
  else if (*ObjectFile.Header.FileSize < Computed)
    return createStringError(errc::result_out_of_range,
                             "File size specified is too small.");
  return Error::success();
}

Error DXContainerWriter::validatePartOffsets() {
  if (ObjectFile.Parts.size() != ObjectFile.Header.PartOffsets->size())
    return createStringError(
        errc::invalid_argument,
        "Mismatch between number of parts and part offsets.");
  uint32_t RollingOffset =
      sizeof(dxbc::Header) + (ObjectFile.Header.PartCount * sizeof(uint32_t));
  for (auto I : llvm::zip(ObjectFile.Parts, *ObjectFile.Header.PartOffsets)) {
    if (RollingOffset > std::get<1>(I))
      return createStringError(errc::invalid_argument,
                               "Offset mismatch, not enough space for data.");
    RollingOffset =
        std::get<1>(I) + sizeof(dxbc::PartHeader) + std::get<0>(I).Size;
  }
  if (Error Err = validateSize(RollingOffset))
    return Err;

  return Error::success();
}

Error DXContainerWriter::computePartOffsets() {
  if (ObjectFile.Header.PartOffsets)
    return validatePartOffsets();
  uint32_t RollingOffset =
      sizeof(dxbc::Header) + (ObjectFile.Header.PartCount * sizeof(uint32_t));
  ObjectFile.Header.PartOffsets = std::vector<uint32_t>();
  for (const auto &Part : ObjectFile.Parts) {
    ObjectFile.Header.PartOffsets->push_back(RollingOffset);
    RollingOffset += sizeof(dxbc::PartHeader) + Part.Size;
  }
  if (Error Err = validateSize(RollingOffset))
    return Err;

  return Error::success();
}

void DXContainerWriter::writeHeader(raw_ostream &OS) {
  dxbc::Header Header;
  memcpy(Header.Magic, "DXBC", 4);
  memcpy(Header.FileHash.Digest, ObjectFile.Header.Hash.data(), 16);
  Header.Version.Major = ObjectFile.Header.Version.Major;
  Header.Version.Minor = ObjectFile.Header.Version.Minor;
  Header.FileSize = *ObjectFile.Header.FileSize;
  Header.PartCount = ObjectFile.Parts.size();
  if (sys::IsBigEndianHost)
    Header.swapBytes();
  OS.write(reinterpret_cast<char *>(&Header), sizeof(Header));
  SmallVector<uint32_t> Offsets(ObjectFile.Header.PartOffsets->begin(),
                                ObjectFile.Header.PartOffsets->end());
  if (sys::IsBigEndianHost)
    for (auto &O : Offsets)
      sys::swapByteOrder(O);
  OS.write(reinterpret_cast<char *>(Offsets.data()),
           Offsets.size() * sizeof(uint32_t));
}

void DXContainerWriter::writeParts(raw_ostream &OS) {
  uint32_t RollingOffset =
      sizeof(dxbc::Header) + (ObjectFile.Header.PartCount * sizeof(uint32_t));
  for (auto I : llvm::zip(ObjectFile.Parts, *ObjectFile.Header.PartOffsets)) {
    if (RollingOffset < std::get<1>(I)) {
      uint32_t PadBytes = std::get<1>(I) - RollingOffset;
      OS.write_zeros(PadBytes);
    }
    DXContainerYAML::Part P = std::get<0>(I);
    RollingOffset = std::get<1>(I) + sizeof(dxbc::PartHeader);
    uint32_t PartSize = P.Size;

    OS.write(P.Name.c_str(), 4);
    if (sys::IsBigEndianHost)
      sys::swapByteOrder(P.Size);
    OS.write(reinterpret_cast<const char *>(&P.Size), sizeof(uint32_t));

    dxbc::PartType PT = dxbc::parsePartType(P.Name);

    uint64_t DataStart = OS.tell();
    switch (PT) {
    case dxbc::PartType::DXIL: {
      if (!P.Program)
        continue;
      dxbc::ProgramHeader Header;
      Header.Version = dxbc::ProgramHeader::getVersion(P.Program->MajorVersion,
                                                       P.Program->MinorVersion);
      Header.Unused = 0;
      Header.ShaderKind = P.Program->ShaderKind;
      memcpy(Header.Bitcode.Magic, "DXIL", 4);
      Header.Bitcode.MajorVersion = P.Program->DXILMajorVersion;
      Header.Bitcode.MinorVersion = P.Program->DXILMinorVersion;
      Header.Bitcode.Unused = 0;

      // Compute the optional fields if needed...
      if (P.Program->DXILOffset)
        Header.Bitcode.Offset = *P.Program->DXILOffset;
      else
        Header.Bitcode.Offset = sizeof(dxbc::BitcodeHeader);

      if (P.Program->DXILSize)
        Header.Bitcode.Size = *P.Program->DXILSize;
      else
        Header.Bitcode.Size = P.Program->DXIL ? P.Program->DXIL->size() : 0;

      if (P.Program->Size)
        Header.Size = *P.Program->Size;
      else
        Header.Size = sizeof(dxbc::ProgramHeader) + Header.Bitcode.Size;

      uint32_t BitcodeOffset = Header.Bitcode.Offset;
      if (sys::IsBigEndianHost)
        Header.swapBytes();
      OS.write(reinterpret_cast<const char *>(&Header),
               sizeof(dxbc::ProgramHeader));
      if (P.Program->DXIL) {
        if (BitcodeOffset > sizeof(dxbc::BitcodeHeader)) {
          uint32_t PadBytes = BitcodeOffset - sizeof(dxbc::BitcodeHeader);
          OS.write_zeros(PadBytes);
        }
        OS.write(reinterpret_cast<char *>(P.Program->DXIL->data()),
                 P.Program->DXIL->size());
      }
      break;
    }
    case dxbc::PartType::SFI0: {
      // If we don't have any flags we can continue here and the data will be
      // zeroed out.
      if (!P.Flags.has_value())
        continue;
      uint64_t Flags = P.Flags->getEncodedFlags();
      if (sys::IsBigEndianHost)
        sys::swapByteOrder(Flags);
      OS.write(reinterpret_cast<char *>(&Flags), sizeof(uint64_t));
      break;
    }
    case dxbc::PartType::HASH: {
      if (!P.Hash.has_value())
        continue;
      dxbc::ShaderHash Hash = {0, {0}};
      if (P.Hash->IncludesSource)
        Hash.Flags |= static_cast<uint32_t>(dxbc::HashFlags::IncludesSource);
      memcpy(&Hash.Digest[0], &P.Hash->Digest[0], 16);
      if (sys::IsBigEndianHost)
        Hash.swapBytes();
      OS.write(reinterpret_cast<char *>(&Hash), sizeof(dxbc::ShaderHash));
      break;
    }
    case dxbc::PartType::PSV0: {
      if (!P.Info.has_value())
        continue;
      mcdxbc::PSVRuntimeInfo PSV;
      memcpy(&PSV.BaseData, &P.Info->Info, sizeof(dxbc::PSV::v3::RuntimeInfo));
      PSV.Resources = P.Info->Resources;
      PSV.EntryName = P.Info->EntryName;

      for (auto El : P.Info->SigInputElements)
        PSV.InputElements.push_back(mcdxbc::PSVSignatureElement{
            El.Name, El.Indices, El.StartRow, El.Cols, El.StartCol,
            El.Allocated, El.Kind, El.Type, El.Mode, El.DynamicMask,
            El.Stream});

      for (auto El : P.Info->SigOutputElements)
        PSV.OutputElements.push_back(mcdxbc::PSVSignatureElement{
            El.Name, El.Indices, El.StartRow, El.Cols, El.StartCol,
            El.Allocated, El.Kind, El.Type, El.Mode, El.DynamicMask,
            El.Stream});

      for (auto El : P.Info->SigPatchOrPrimElements)
        PSV.PatchOrPrimElements.push_back(mcdxbc::PSVSignatureElement{
            El.Name, El.Indices, El.StartRow, El.Cols, El.StartCol,
            El.Allocated, El.Kind, El.Type, El.Mode, El.DynamicMask,
            El.Stream});

      static_assert(PSV.OutputVectorMasks.size() == PSV.InputOutputMap.size());
      for (unsigned I = 0; I < PSV.OutputVectorMasks.size(); ++I) {
        PSV.OutputVectorMasks[I].insert(PSV.OutputVectorMasks[I].begin(),
                                        P.Info->OutputVectorMasks[I].begin(),
                                        P.Info->OutputVectorMasks[I].end());
        PSV.InputOutputMap[I].insert(PSV.InputOutputMap[I].begin(),
                                     P.Info->InputOutputMap[I].begin(),
                                     P.Info->InputOutputMap[I].end());
      }

      PSV.PatchOrPrimMasks.insert(PSV.PatchOrPrimMasks.begin(),
                                  P.Info->PatchOrPrimMasks.begin(),
                                  P.Info->PatchOrPrimMasks.end());
      PSV.InputPatchMap.insert(PSV.InputPatchMap.begin(),
                               P.Info->InputPatchMap.begin(),
                               P.Info->InputPatchMap.end());
      PSV.PatchOutputMap.insert(PSV.PatchOutputMap.begin(),
                                P.Info->PatchOutputMap.begin(),
                                P.Info->PatchOutputMap.end());

      PSV.finalize(static_cast<Triple::EnvironmentType>(
          Triple::Pixel + P.Info->Info.ShaderStage));
      PSV.write(OS, P.Info->Version);
      break;
    }
    case dxbc::PartType::ISG1:
    case dxbc::PartType::OSG1:
    case dxbc::PartType::PSG1: {
      mcdxbc::Signature Sig;
      if (P.Signature.has_value()) {
        for (const auto &Param : P.Signature->Parameters) {
          Sig.addParam(Param.Stream, Param.Name, Param.Index, Param.SystemValue,
                       Param.CompType, Param.Register, Param.Mask,
                       Param.ExclusiveMask, Param.MinPrecision);
        }
      }
      Sig.write(OS);
      break;
    }
    case dxbc::PartType::Unknown:
      break; // Skip any handling for unrecognized parts.
    }
    uint64_t BytesWritten = OS.tell() - DataStart;
    RollingOffset += BytesWritten;
    if (BytesWritten < PartSize)
      OS.write_zeros(PartSize - BytesWritten);
    RollingOffset += PartSize;
  }
}

Error DXContainerWriter::write(raw_ostream &OS) {
  if (Error Err = computePartOffsets())
    return Err;
  writeHeader(OS);
  writeParts(OS);
  return Error::success();
}

namespace llvm {
namespace yaml {

bool yaml2dxcontainer(DXContainerYAML::Object &Doc, raw_ostream &Out,
                      ErrorHandler EH) {
  DXContainerWriter Writer(Doc);
  if (Error Err = Writer.write(Out)) {
    handleAllErrors(std::move(Err),
                    [&](const ErrorInfoBase &Err) { EH(Err.message()); });
    return false;
  }
  return true;
}

} // namespace yaml
} // namespace llvm
