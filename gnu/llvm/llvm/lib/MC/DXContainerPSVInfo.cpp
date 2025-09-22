//===- llvm/MC/DXContainerPSVInfo.cpp - DXContainer PSVInfo -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/DXContainerPSVInfo.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::mcdxbc;
using namespace llvm::dxbc::PSV;

static constexpr size_t npos = StringRef::npos;

static size_t FindSequence(ArrayRef<uint32_t> Buffer,
                           ArrayRef<uint32_t> Sequence) {
  if (Buffer.size() < Sequence.size())
    return npos;
  for (size_t Idx = 0; Idx <= Buffer.size() - Sequence.size(); ++Idx) {
    if (0 == memcmp(static_cast<const void *>(&Buffer[Idx]),
                    static_cast<const void *>(Sequence.begin()),
                    Sequence.size() * sizeof(uint32_t)))
      return Idx;
  }
  return npos;
}

static void
ProcessElementList(StringTableBuilder &StrTabBuilder,
                   SmallVectorImpl<uint32_t> &IndexBuffer,
                   SmallVectorImpl<v0::SignatureElement> &FinalElements,
                   SmallVectorImpl<StringRef> &SemanticNames,
                   ArrayRef<PSVSignatureElement> Elements) {
  for (const auto &El : Elements) {
    // Put the name in the string table and the name list.
    StrTabBuilder.add(El.Name);
    SemanticNames.push_back(El.Name);

    v0::SignatureElement FinalElement;
    memset(&FinalElement, 0, sizeof(v0::SignatureElement));
    FinalElement.Rows = static_cast<uint8_t>(El.Indices.size());
    FinalElement.StartRow = El.StartRow;
    FinalElement.Cols = El.Cols;
    FinalElement.StartCol = El.StartCol;
    FinalElement.Allocated = El.Allocated;
    FinalElement.Kind = El.Kind;
    FinalElement.Type = El.Type;
    FinalElement.Mode = El.Mode;
    FinalElement.DynamicMask = El.DynamicMask;
    FinalElement.Stream = El.Stream;

    size_t Idx = FindSequence(IndexBuffer, El.Indices);
    if (Idx == npos) {
      FinalElement.IndicesOffset = static_cast<uint32_t>(IndexBuffer.size());
      IndexBuffer.insert(IndexBuffer.end(), El.Indices.begin(),
                         El.Indices.end());
    } else
      FinalElement.IndicesOffset = static_cast<uint32_t>(Idx);
    FinalElements.push_back(FinalElement);
  }
}

void PSVRuntimeInfo::write(raw_ostream &OS, uint32_t Version) const {
  assert(IsFinalized && "finalize must be called before write");

  uint32_t InfoSize;
  uint32_t BindingSize;
  switch (Version) {
  case 0:
    InfoSize = sizeof(dxbc::PSV::v0::RuntimeInfo);
    BindingSize = sizeof(dxbc::PSV::v0::ResourceBindInfo);
    break;
  case 1:
    InfoSize = sizeof(dxbc::PSV::v1::RuntimeInfo);
    BindingSize = sizeof(dxbc::PSV::v0::ResourceBindInfo);
    break;
  case 2:
    InfoSize = sizeof(dxbc::PSV::v2::RuntimeInfo);
    BindingSize = sizeof(dxbc::PSV::v2::ResourceBindInfo);
    break;
  case 3:
  default:
    InfoSize = sizeof(dxbc::PSV::v3::RuntimeInfo);
    BindingSize = sizeof(dxbc::PSV::v2::ResourceBindInfo);
  }

  // Write the size of the info.
  support::endian::write(OS, InfoSize, llvm::endianness::little);

  // Write the info itself.
  OS.write(reinterpret_cast<const char *>(&BaseData), InfoSize);

  uint32_t ResourceCount = static_cast<uint32_t>(Resources.size());

  support::endian::write(OS, ResourceCount, llvm::endianness::little);
  if (ResourceCount > 0)
    support::endian::write(OS, BindingSize, llvm::endianness::little);

  for (const auto &Res : Resources)
    OS.write(reinterpret_cast<const char *>(&Res), BindingSize);

  // PSV Version 0 stops after the resource list.
  if (Version == 0)
    return;

  support::endian::write(OS,
                         static_cast<uint32_t>(DXConStrTabBuilder.getSize()),
                         llvm::endianness::little);

  // Write the string table.
  DXConStrTabBuilder.write(OS);

  // Write the index table size, then table.
  support::endian::write(OS, static_cast<uint32_t>(IndexBuffer.size()),
                         llvm::endianness::little);
  for (auto I : IndexBuffer)
    support::endian::write(OS, I, llvm::endianness::little);

  if (SignatureElements.size() > 0) {
    // write the size of the signature elements.
    support::endian::write(OS,
                           static_cast<uint32_t>(sizeof(v0::SignatureElement)),
                           llvm::endianness::little);

    // write the signature elements.
    OS.write(reinterpret_cast<const char *>(&SignatureElements[0]),
             SignatureElements.size() * sizeof(v0::SignatureElement));
  }

  for (const auto &MaskVector : OutputVectorMasks)
    support::endian::write_array(OS, ArrayRef<uint32_t>(MaskVector),
                                 llvm::endianness::little);
  support::endian::write_array(OS, ArrayRef<uint32_t>(PatchOrPrimMasks),
                               llvm::endianness::little);
  for (const auto &MaskVector : InputOutputMap)
    support::endian::write_array(OS, ArrayRef<uint32_t>(MaskVector),
                                 llvm::endianness::little);
  support::endian::write_array(OS, ArrayRef<uint32_t>(InputPatchMap),
                               llvm::endianness::little);
  support::endian::write_array(OS, ArrayRef<uint32_t>(PatchOutputMap),
                               llvm::endianness::little);
}

void PSVRuntimeInfo::finalize(Triple::EnvironmentType Stage) {
  IsFinalized = true;
  BaseData.SigInputElements = static_cast<uint32_t>(InputElements.size());
  BaseData.SigOutputElements = static_cast<uint32_t>(OutputElements.size());
  BaseData.SigPatchOrPrimElements =
      static_cast<uint32_t>(PatchOrPrimElements.size());

  SmallVector<StringRef, 32> SemanticNames;

  // Build a string table and set associated offsets to be written when
  // write() is called
  ProcessElementList(DXConStrTabBuilder, IndexBuffer, SignatureElements,
                     SemanticNames, InputElements);
  ProcessElementList(DXConStrTabBuilder, IndexBuffer, SignatureElements,
                     SemanticNames, OutputElements);
  ProcessElementList(DXConStrTabBuilder, IndexBuffer, SignatureElements,
                     SemanticNames, PatchOrPrimElements);

  DXConStrTabBuilder.add(EntryName);

  DXConStrTabBuilder.finalize();
  for (auto ElAndName : zip(SignatureElements, SemanticNames)) {
    llvm::dxbc::PSV::v0::SignatureElement &El = std::get<0>(ElAndName);
    StringRef Name = std::get<1>(ElAndName);
    El.NameOffset = static_cast<uint32_t>(DXConStrTabBuilder.getOffset(Name));
    if (sys::IsBigEndianHost)
      El.swapBytes();
  }

  BaseData.EntryNameOffset =
      static_cast<uint32_t>(DXConStrTabBuilder.getOffset(EntryName));

  if (!sys::IsBigEndianHost)
    return;
  BaseData.swapBytes();
  BaseData.swapBytes(Stage);
  for (auto &Res : Resources)
    Res.swapBytes();
}

void Signature::write(raw_ostream &OS) {
  SmallVector<dxbc::ProgramSignatureElement> SigParams;
  SigParams.reserve(Params.size());
  StringTableBuilder StrTabBuilder((StringTableBuilder::DWARF));

  // Name offsets are from the start of the part. Pre-calculate the offset to
  // the start of the string table so that it can be added to the table offset.
  uint32_t TableStart = sizeof(dxbc::ProgramSignatureHeader) +
                        (sizeof(dxbc::ProgramSignatureElement) * Params.size());

  for (const auto &P : Params) {
    // zero out the data
    dxbc::ProgramSignatureElement FinalElement;
    memset(&FinalElement, 0, sizeof(dxbc::ProgramSignatureElement));
    FinalElement.Stream = P.Stream;
    FinalElement.NameOffset =
        static_cast<uint32_t>(StrTabBuilder.add(P.Name)) + TableStart;
    FinalElement.Index = P.Index;
    FinalElement.SystemValue = P.SystemValue;
    FinalElement.CompType = P.CompType;
    FinalElement.Register = P.Register;
    FinalElement.Mask = P.Mask;
    FinalElement.ExclusiveMask = P.ExclusiveMask;
    FinalElement.MinPrecision = P.MinPrecision;
    SigParams.push_back(FinalElement);
  }

  StrTabBuilder.finalizeInOrder();
  stable_sort(SigParams, [&](const dxbc::ProgramSignatureElement &L,
                             const dxbc::ProgramSignatureElement R) {
    return std::tie(L.Stream, L.Register, L.NameOffset) <
           std::tie(R.Stream, R.Register, R.NameOffset);
  });
  if (sys::IsBigEndianHost)
    for (auto &El : SigParams)
      El.swapBytes();

  dxbc::ProgramSignatureHeader Header = {static_cast<uint32_t>(Params.size()),
                                         sizeof(dxbc::ProgramSignatureHeader)};
  if (sys::IsBigEndianHost)
    Header.swapBytes();
  OS.write(reinterpret_cast<const char *>(&Header),
           sizeof(dxbc::ProgramSignatureHeader));
  OS.write(reinterpret_cast<const char *>(SigParams.data()),
           sizeof(dxbc::ProgramSignatureElement) * SigParams.size());
  StrTabBuilder.write(OS);
}
