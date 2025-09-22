//===- DXContainer.cpp - DXContainer object file implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/DXContainer.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::object;

static Error parseFailed(const Twine &Msg) {
  return make_error<GenericBinaryError>(Msg.str(), object_error::parse_failed);
}

template <typename T>
static Error readStruct(StringRef Buffer, const char *Src, T &Struct) {
  // Don't read before the beginning or past the end of the file
  if (Src < Buffer.begin() || Src + sizeof(T) > Buffer.end())
    return parseFailed("Reading structure out of file bounds");

  memcpy(&Struct, Src, sizeof(T));
  // DXContainer is always little endian
  if (sys::IsBigEndianHost)
    Struct.swapBytes();
  return Error::success();
}

template <typename T>
static Error readInteger(StringRef Buffer, const char *Src, T &Val,
                         Twine Str = "structure") {
  static_assert(std::is_integral_v<T>,
                "Cannot call readInteger on non-integral type.");
  // Don't read before the beginning or past the end of the file
  if (Src < Buffer.begin() || Src + sizeof(T) > Buffer.end())
    return parseFailed(Twine("Reading ") + Str + " out of file bounds");

  // The DXContainer offset table is comprised of uint32_t values but not padded
  // to a 64-bit boundary. So Parts may start unaligned if there is an odd
  // number of parts and part data itself is not required to be padded.
  if (reinterpret_cast<uintptr_t>(Src) % alignof(T) != 0)
    memcpy(reinterpret_cast<char *>(&Val), Src, sizeof(T));
  else
    Val = *reinterpret_cast<const T *>(Src);
  // DXContainer is always little endian
  if (sys::IsBigEndianHost)
    sys::swapByteOrder(Val);
  return Error::success();
}

DXContainer::DXContainer(MemoryBufferRef O) : Data(O) {}

Error DXContainer::parseHeader() {
  return readStruct(Data.getBuffer(), Data.getBuffer().data(), Header);
}

Error DXContainer::parseDXILHeader(StringRef Part) {
  if (DXIL)
    return parseFailed("More than one DXIL part is present in the file");
  const char *Current = Part.begin();
  dxbc::ProgramHeader Header;
  if (Error Err = readStruct(Part, Current, Header))
    return Err;
  Current += offsetof(dxbc::ProgramHeader, Bitcode) + Header.Bitcode.Offset;
  DXIL.emplace(std::make_pair(Header, Current));
  return Error::success();
}

Error DXContainer::parseShaderFeatureFlags(StringRef Part) {
  if (ShaderFeatureFlags)
    return parseFailed("More than one SFI0 part is present in the file");
  uint64_t FlagValue = 0;
  if (Error Err = readInteger(Part, Part.begin(), FlagValue))
    return Err;
  ShaderFeatureFlags = FlagValue;
  return Error::success();
}

Error DXContainer::parseHash(StringRef Part) {
  if (Hash)
    return parseFailed("More than one HASH part is present in the file");
  dxbc::ShaderHash ReadHash;
  if (Error Err = readStruct(Part, Part.begin(), ReadHash))
    return Err;
  Hash = ReadHash;
  return Error::success();
}

Error DXContainer::parsePSVInfo(StringRef Part) {
  if (PSVInfo)
    return parseFailed("More than one PSV0 part is present in the file");
  PSVInfo = DirectX::PSVRuntimeInfo(Part);
  // Parsing the PSVRuntime info occurs late because we need to read data from
  // other parts first.
  return Error::success();
}

Error DirectX::Signature::initialize(StringRef Part) {
  dxbc::ProgramSignatureHeader SigHeader;
  if (Error Err = readStruct(Part, Part.begin(), SigHeader))
    return Err;
  size_t Size = sizeof(dxbc::ProgramSignatureElement) * SigHeader.ParamCount;

  if (Part.size() < Size + SigHeader.FirstParamOffset)
    return parseFailed("Signature parameters extend beyond the part boundary");

  Parameters.Data = Part.substr(SigHeader.FirstParamOffset, Size);

  StringTableOffset = SigHeader.FirstParamOffset + static_cast<uint32_t>(Size);
  StringTable = Part.substr(SigHeader.FirstParamOffset + Size);

  for (const auto &Param : Parameters) {
    if (Param.NameOffset < StringTableOffset)
      return parseFailed("Invalid parameter name offset: name starts before "
                         "the first name offset");
    if (Param.NameOffset - StringTableOffset > StringTable.size())
      return parseFailed("Invalid parameter name offset: name starts after the "
                         "end of the part data");
  }
  return Error::success();
}

Error DXContainer::parsePartOffsets() {
  uint32_t LastOffset =
      sizeof(dxbc::Header) + (Header.PartCount * sizeof(uint32_t));
  const char *Current = Data.getBuffer().data() + sizeof(dxbc::Header);
  for (uint32_t Part = 0; Part < Header.PartCount; ++Part) {
    uint32_t PartOffset;
    if (Error Err = readInteger(Data.getBuffer(), Current, PartOffset))
      return Err;
    if (PartOffset < LastOffset)
      return parseFailed(
          formatv(
              "Part offset for part {0} begins before the previous part ends",
              Part)
              .str());
    Current += sizeof(uint32_t);
    if (PartOffset >= Data.getBufferSize())
      return parseFailed("Part offset points beyond boundary of the file");
    // To prevent overflow when reading the part name, we subtract the part name
    // size from the buffer size, rather than adding to the offset. Since the
    // file header is larger than the part header we can't reach this code
    // unless the buffer is at least as large as a part header, so this
    // subtraction can't underflow.
    if (PartOffset >= Data.getBufferSize() - sizeof(dxbc::PartHeader::Name))
      return parseFailed("File not large enough to read part name");
    PartOffsets.push_back(PartOffset);

    dxbc::PartType PT =
        dxbc::parsePartType(Data.getBuffer().substr(PartOffset, 4));
    uint32_t PartDataStart = PartOffset + sizeof(dxbc::PartHeader);
    uint32_t PartSize;
    if (Error Err = readInteger(Data.getBuffer(),
                                Data.getBufferStart() + PartOffset + 4,
                                PartSize, "part size"))
      return Err;
    StringRef PartData = Data.getBuffer().substr(PartDataStart, PartSize);
    LastOffset = PartOffset + PartSize;
    switch (PT) {
    case dxbc::PartType::DXIL:
      if (Error Err = parseDXILHeader(PartData))
        return Err;
      break;
    case dxbc::PartType::SFI0:
      if (Error Err = parseShaderFeatureFlags(PartData))
        return Err;
      break;
    case dxbc::PartType::HASH:
      if (Error Err = parseHash(PartData))
        return Err;
      break;
    case dxbc::PartType::PSV0:
      if (Error Err = parsePSVInfo(PartData))
        return Err;
      break;
    case dxbc::PartType::ISG1:
      if (Error Err = InputSignature.initialize(PartData))
        return Err;
      break;
    case dxbc::PartType::OSG1:
      if (Error Err = OutputSignature.initialize(PartData))
        return Err;
      break;
    case dxbc::PartType::PSG1:
      if (Error Err = PatchConstantSignature.initialize(PartData))
        return Err;
      break;
    case dxbc::PartType::Unknown:
      break;
    }
  }

  // Fully parsing the PSVInfo requires knowing the shader kind which we read
  // out of the program header in the DXIL part.
  if (PSVInfo) {
    if (!DXIL)
      return parseFailed("Cannot fully parse pipeline state validation "
                         "information without DXIL part.");
    if (Error Err = PSVInfo->parse(DXIL->first.ShaderKind))
      return Err;
  }
  return Error::success();
}

Expected<DXContainer> DXContainer::create(MemoryBufferRef Object) {
  DXContainer Container(Object);
  if (Error Err = Container.parseHeader())
    return std::move(Err);
  if (Error Err = Container.parsePartOffsets())
    return std::move(Err);
  return Container;
}

void DXContainer::PartIterator::updateIteratorImpl(const uint32_t Offset) {
  StringRef Buffer = Container.Data.getBuffer();
  const char *Current = Buffer.data() + Offset;
  // Offsets are validated during parsing, so all offsets in the container are
  // valid and contain enough readable data to read a header.
  cantFail(readStruct(Buffer, Current, IteratorState.Part));
  IteratorState.Data =
      StringRef(Current + sizeof(dxbc::PartHeader), IteratorState.Part.Size);
  IteratorState.Offset = Offset;
}

Error DirectX::PSVRuntimeInfo::parse(uint16_t ShaderKind) {
  Triple::EnvironmentType ShaderStage = dxbc::getShaderStage(ShaderKind);

  const char *Current = Data.begin();
  if (Error Err = readInteger(Data, Current, Size))
    return Err;
  Current += sizeof(uint32_t);

  StringRef PSVInfoData = Data.substr(sizeof(uint32_t), Size);

  if (PSVInfoData.size() < Size)
    return parseFailed(
        "Pipeline state data extends beyond the bounds of the part");

  using namespace dxbc::PSV;

  const uint32_t PSVVersion = getVersion();

  // Detect the PSVVersion by looking at the size field.
  if (PSVVersion == 3) {
    v3::RuntimeInfo Info;
    if (Error Err = readStruct(PSVInfoData, Current, Info))
      return Err;
    if (sys::IsBigEndianHost)
      Info.swapBytes(ShaderStage);
    BasicInfo = Info;
  } else if (PSVVersion == 2) {
    v2::RuntimeInfo Info;
    if (Error Err = readStruct(PSVInfoData, Current, Info))
      return Err;
    if (sys::IsBigEndianHost)
      Info.swapBytes(ShaderStage);
    BasicInfo = Info;
  } else if (PSVVersion == 1) {
    v1::RuntimeInfo Info;
    if (Error Err = readStruct(PSVInfoData, Current, Info))
      return Err;
    if (sys::IsBigEndianHost)
      Info.swapBytes(ShaderStage);
    BasicInfo = Info;
  } else if (PSVVersion == 0) {
    v0::RuntimeInfo Info;
    if (Error Err = readStruct(PSVInfoData, Current, Info))
      return Err;
    if (sys::IsBigEndianHost)
      Info.swapBytes(ShaderStage);
    BasicInfo = Info;
  } else
    return parseFailed(
        "Cannot read PSV Runtime Info, unsupported PSV version.");

  Current += Size;

  uint32_t ResourceCount = 0;
  if (Error Err = readInteger(Data, Current, ResourceCount))
    return Err;
  Current += sizeof(uint32_t);

  if (ResourceCount > 0) {
    if (Error Err = readInteger(Data, Current, Resources.Stride))
      return Err;
    Current += sizeof(uint32_t);

    size_t BindingDataSize = Resources.Stride * ResourceCount;
    Resources.Data = Data.substr(Current - Data.begin(), BindingDataSize);

    if (Resources.Data.size() < BindingDataSize)
      return parseFailed(
          "Resource binding data extends beyond the bounds of the part");

    Current += BindingDataSize;
  } else
    Resources.Stride = sizeof(v2::ResourceBindInfo);

  // PSV version 0 ends after the resource bindings.
  if (PSVVersion == 0)
    return Error::success();

  // String table starts at a 4-byte offset.
  Current = reinterpret_cast<const char *>(
      alignTo<4>(reinterpret_cast<uintptr_t>(Current)));

  uint32_t StringTableSize = 0;
  if (Error Err = readInteger(Data, Current, StringTableSize))
    return Err;
  if (StringTableSize % 4 != 0)
    return parseFailed("String table misaligned");
  Current += sizeof(uint32_t);
  StringTable = StringRef(Current, StringTableSize);

  Current += StringTableSize;

  uint32_t SemanticIndexTableSize = 0;
  if (Error Err = readInteger(Data, Current, SemanticIndexTableSize))
    return Err;
  Current += sizeof(uint32_t);

  SemanticIndexTable.reserve(SemanticIndexTableSize);
  for (uint32_t I = 0; I < SemanticIndexTableSize; ++I) {
    uint32_t Index = 0;
    if (Error Err = readInteger(Data, Current, Index))
      return Err;
    Current += sizeof(uint32_t);
    SemanticIndexTable.push_back(Index);
  }

  uint8_t InputCount = getSigInputCount();
  uint8_t OutputCount = getSigOutputCount();
  uint8_t PatchOrPrimCount = getSigPatchOrPrimCount();

  uint32_t ElementCount = InputCount + OutputCount + PatchOrPrimCount;

  if (ElementCount > 0) {
    if (Error Err = readInteger(Data, Current, SigInputElements.Stride))
      return Err;
    Current += sizeof(uint32_t);
    // Assign the stride to all the arrays.
    SigOutputElements.Stride = SigPatchOrPrimElements.Stride =
        SigInputElements.Stride;

    if (Data.end() - Current <
        (ptrdiff_t)(ElementCount * SigInputElements.Stride))
      return parseFailed(
          "Signature elements extend beyond the size of the part");

    size_t InputSize = SigInputElements.Stride * InputCount;
    SigInputElements.Data = Data.substr(Current - Data.begin(), InputSize);
    Current += InputSize;

    size_t OutputSize = SigOutputElements.Stride * OutputCount;
    SigOutputElements.Data = Data.substr(Current - Data.begin(), OutputSize);
    Current += OutputSize;

    size_t PSize = SigPatchOrPrimElements.Stride * PatchOrPrimCount;
    SigPatchOrPrimElements.Data = Data.substr(Current - Data.begin(), PSize);
    Current += PSize;
  }

  ArrayRef<uint8_t> OutputVectorCounts = getOutputVectorCounts();
  uint8_t PatchConstOrPrimVectorCount = getPatchConstOrPrimVectorCount();
  uint8_t InputVectorCount = getInputVectorCount();

  auto maskDwordSize = [](uint8_t Vector) {
    return (static_cast<uint32_t>(Vector) + 7) >> 3;
  };

  auto mapTableSize = [maskDwordSize](uint8_t X, uint8_t Y) {
    return maskDwordSize(Y) * X * 4;
  };

  if (usesViewID()) {
    for (uint32_t I = 0; I < OutputVectorCounts.size(); ++I) {
      // The vector mask is one bit per component and 4 components per vector.
      // We can compute the number of dwords required by rounding up to the next
      // multiple of 8.
      uint32_t NumDwords =
          maskDwordSize(static_cast<uint32_t>(OutputVectorCounts[I]));
      size_t NumBytes = NumDwords * sizeof(uint32_t);
      OutputVectorMasks[I].Data = Data.substr(Current - Data.begin(), NumBytes);
      Current += NumBytes;
    }

    if (ShaderStage == Triple::Hull && PatchConstOrPrimVectorCount > 0) {
      uint32_t NumDwords = maskDwordSize(PatchConstOrPrimVectorCount);
      size_t NumBytes = NumDwords * sizeof(uint32_t);
      PatchOrPrimMasks.Data = Data.substr(Current - Data.begin(), NumBytes);
      Current += NumBytes;
    }
  }

  // Input/Output mapping table
  for (uint32_t I = 0; I < OutputVectorCounts.size(); ++I) {
    if (InputVectorCount == 0 || OutputVectorCounts[I] == 0)
      continue;
    uint32_t NumDwords = mapTableSize(InputVectorCount, OutputVectorCounts[I]);
    size_t NumBytes = NumDwords * sizeof(uint32_t);
    InputOutputMap[I].Data = Data.substr(Current - Data.begin(), NumBytes);
    Current += NumBytes;
  }

  // Hull shader: Input/Patch mapping table
  if (ShaderStage == Triple::Hull && PatchConstOrPrimVectorCount > 0 &&
      InputVectorCount > 0) {
    uint32_t NumDwords =
        mapTableSize(InputVectorCount, PatchConstOrPrimVectorCount);
    size_t NumBytes = NumDwords * sizeof(uint32_t);
    InputPatchMap.Data = Data.substr(Current - Data.begin(), NumBytes);
    Current += NumBytes;
  }

  // Domain Shader: Patch/Output mapping table
  if (ShaderStage == Triple::Domain && PatchConstOrPrimVectorCount > 0 &&
      OutputVectorCounts[0] > 0) {
    uint32_t NumDwords =
        mapTableSize(PatchConstOrPrimVectorCount, OutputVectorCounts[0]);
    size_t NumBytes = NumDwords * sizeof(uint32_t);
    PatchOutputMap.Data = Data.substr(Current - Data.begin(), NumBytes);
    Current += NumBytes;
  }

  return Error::success();
}

uint8_t DirectX::PSVRuntimeInfo::getSigInputCount() const {
  if (const auto *P = std::get_if<dxbc::PSV::v3::RuntimeInfo>(&BasicInfo))
    return P->SigInputElements;
  if (const auto *P = std::get_if<dxbc::PSV::v2::RuntimeInfo>(&BasicInfo))
    return P->SigInputElements;
  if (const auto *P = std::get_if<dxbc::PSV::v1::RuntimeInfo>(&BasicInfo))
    return P->SigInputElements;
  return 0;
}

uint8_t DirectX::PSVRuntimeInfo::getSigOutputCount() const {
  if (const auto *P = std::get_if<dxbc::PSV::v3::RuntimeInfo>(&BasicInfo))
    return P->SigOutputElements;
  if (const auto *P = std::get_if<dxbc::PSV::v2::RuntimeInfo>(&BasicInfo))
    return P->SigOutputElements;
  if (const auto *P = std::get_if<dxbc::PSV::v1::RuntimeInfo>(&BasicInfo))
    return P->SigOutputElements;
  return 0;
}

uint8_t DirectX::PSVRuntimeInfo::getSigPatchOrPrimCount() const {
  if (const auto *P = std::get_if<dxbc::PSV::v3::RuntimeInfo>(&BasicInfo))
    return P->SigPatchOrPrimElements;
  if (const auto *P = std::get_if<dxbc::PSV::v2::RuntimeInfo>(&BasicInfo))
    return P->SigPatchOrPrimElements;
  if (const auto *P = std::get_if<dxbc::PSV::v1::RuntimeInfo>(&BasicInfo))
    return P->SigPatchOrPrimElements;
  return 0;
}
