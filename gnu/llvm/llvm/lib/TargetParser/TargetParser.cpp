//===-- TargetParser - Parser for target features ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise hardware features such as
// FPU/CPU/ARCH names as well as specific support such as HDIV, etc.
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/TargetParser.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;
using namespace AMDGPU;

namespace {

struct GPUInfo {
  StringLiteral Name;
  StringLiteral CanonicalName;
  AMDGPU::GPUKind Kind;
  unsigned Features;
};

constexpr GPUInfo R600GPUs[] = {
  // Name       Canonical    Kind        Features
  //            Name
  {{"r600"},    {"r600"},    GK_R600,    FEATURE_NONE },
  {{"rv630"},   {"r600"},    GK_R600,    FEATURE_NONE },
  {{"rv635"},   {"r600"},    GK_R600,    FEATURE_NONE },
  {{"r630"},    {"r630"},    GK_R630,    FEATURE_NONE },
  {{"rs780"},   {"rs880"},   GK_RS880,   FEATURE_NONE },
  {{"rs880"},   {"rs880"},   GK_RS880,   FEATURE_NONE },
  {{"rv610"},   {"rs880"},   GK_RS880,   FEATURE_NONE },
  {{"rv620"},   {"rs880"},   GK_RS880,   FEATURE_NONE },
  {{"rv670"},   {"rv670"},   GK_RV670,   FEATURE_NONE },
  {{"rv710"},   {"rv710"},   GK_RV710,   FEATURE_NONE },
  {{"rv730"},   {"rv730"},   GK_RV730,   FEATURE_NONE },
  {{"rv740"},   {"rv770"},   GK_RV770,   FEATURE_NONE },
  {{"rv770"},   {"rv770"},   GK_RV770,   FEATURE_NONE },
  {{"cedar"},   {"cedar"},   GK_CEDAR,   FEATURE_NONE },
  {{"palm"},    {"cedar"},   GK_CEDAR,   FEATURE_NONE },
  {{"cypress"}, {"cypress"}, GK_CYPRESS, FEATURE_FMA  },
  {{"hemlock"}, {"cypress"}, GK_CYPRESS, FEATURE_FMA  },
  {{"juniper"}, {"juniper"}, GK_JUNIPER, FEATURE_NONE },
  {{"redwood"}, {"redwood"}, GK_REDWOOD, FEATURE_NONE },
  {{"sumo"},    {"sumo"},    GK_SUMO,    FEATURE_NONE },
  {{"sumo2"},   {"sumo"},    GK_SUMO,    FEATURE_NONE },
  {{"barts"},   {"barts"},   GK_BARTS,   FEATURE_NONE },
  {{"caicos"},  {"caicos"},  GK_CAICOS,  FEATURE_NONE },
  {{"aruba"},   {"cayman"},  GK_CAYMAN,  FEATURE_FMA  },
  {{"cayman"},  {"cayman"},  GK_CAYMAN,  FEATURE_FMA  },
  {{"turks"},   {"turks"},   GK_TURKS,   FEATURE_NONE }
};

// This table should be sorted by the value of GPUKind
// Don't bother listing the implicitly true features
constexpr GPUInfo AMDGCNGPUs[] = {
    // clang-format off
    // Name         Canonical    Kind        Features
    //              Name
    {{"gfx600"},    {"gfx600"},  GK_GFX600,  FEATURE_FAST_FMA_F32},
    {{"tahiti"},    {"gfx600"},  GK_GFX600,  FEATURE_FAST_FMA_F32},
    {{"gfx601"},    {"gfx601"},  GK_GFX601,  FEATURE_NONE},
    {{"pitcairn"},  {"gfx601"},  GK_GFX601,  FEATURE_NONE},
    {{"verde"},     {"gfx601"},  GK_GFX601,  FEATURE_NONE},
    {{"gfx602"},    {"gfx602"},  GK_GFX602,  FEATURE_NONE},
    {{"hainan"},    {"gfx602"},  GK_GFX602,  FEATURE_NONE},
    {{"oland"},     {"gfx602"},  GK_GFX602,  FEATURE_NONE},
    {{"gfx700"},    {"gfx700"},  GK_GFX700,  FEATURE_NONE},
    {{"kaveri"},    {"gfx700"},  GK_GFX700,  FEATURE_NONE},
    {{"gfx701"},    {"gfx701"},  GK_GFX701,  FEATURE_FAST_FMA_F32},
    {{"hawaii"},    {"gfx701"},  GK_GFX701,  FEATURE_FAST_FMA_F32},
    {{"gfx702"},    {"gfx702"},  GK_GFX702,  FEATURE_FAST_FMA_F32},
    {{"gfx703"},    {"gfx703"},  GK_GFX703,  FEATURE_NONE},
    {{"kabini"},    {"gfx703"},  GK_GFX703,  FEATURE_NONE},
    {{"mullins"},   {"gfx703"},  GK_GFX703,  FEATURE_NONE},
    {{"gfx704"},    {"gfx704"},  GK_GFX704,  FEATURE_NONE},
    {{"bonaire"},   {"gfx704"},  GK_GFX704,  FEATURE_NONE},
    {{"gfx705"},    {"gfx705"},  GK_GFX705,  FEATURE_NONE},
    {{"gfx801"},    {"gfx801"},  GK_GFX801,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK},
    {{"carrizo"},   {"gfx801"},  GK_GFX801,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK},
    {{"gfx802"},    {"gfx802"},  GK_GFX802,  FEATURE_FAST_DENORMAL_F32},
    {{"iceland"},   {"gfx802"},  GK_GFX802,  FEATURE_FAST_DENORMAL_F32},
    {{"tonga"},     {"gfx802"},  GK_GFX802,  FEATURE_FAST_DENORMAL_F32},
    {{"gfx803"},    {"gfx803"},  GK_GFX803,  FEATURE_FAST_DENORMAL_F32},
    {{"fiji"},      {"gfx803"},  GK_GFX803,  FEATURE_FAST_DENORMAL_F32},
    {{"polaris10"}, {"gfx803"},  GK_GFX803,  FEATURE_FAST_DENORMAL_F32},
    {{"polaris11"}, {"gfx803"},  GK_GFX803,  FEATURE_FAST_DENORMAL_F32},
    {{"gfx805"},    {"gfx805"},  GK_GFX805,  FEATURE_FAST_DENORMAL_F32},
    {{"tongapro"},  {"gfx805"},  GK_GFX805,  FEATURE_FAST_DENORMAL_F32},
    {{"gfx810"},    {"gfx810"},  GK_GFX810,  FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK},
    {{"stoney"},    {"gfx810"},  GK_GFX810,  FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK},
    {{"gfx900"},    {"gfx900"},  GK_GFX900,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK},
    {{"gfx902"},    {"gfx902"},  GK_GFX902,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK},
    {{"gfx904"},    {"gfx904"},  GK_GFX904,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK},
    {{"gfx906"},    {"gfx906"},  GK_GFX906,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK|FEATURE_SRAMECC},
    {{"gfx908"},    {"gfx908"},  GK_GFX908,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK|FEATURE_SRAMECC},
    {{"gfx909"},    {"gfx909"},  GK_GFX909,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK},
    {{"gfx90a"},    {"gfx90a"},  GK_GFX90A,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK|FEATURE_SRAMECC},
    {{"gfx90c"},    {"gfx90c"},  GK_GFX90C,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK},
    {{"gfx940"},    {"gfx940"},  GK_GFX940,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK|FEATURE_SRAMECC},
    {{"gfx941"},    {"gfx941"},  GK_GFX941,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK|FEATURE_SRAMECC},
    {{"gfx942"},    {"gfx942"},  GK_GFX942,  FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK|FEATURE_SRAMECC},
    {{"gfx1010"},   {"gfx1010"}, GK_GFX1010, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_XNACK|FEATURE_WGP},
    {{"gfx1011"},   {"gfx1011"}, GK_GFX1011, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_XNACK|FEATURE_WGP},
    {{"gfx1012"},   {"gfx1012"}, GK_GFX1012, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_XNACK|FEATURE_WGP},
    {{"gfx1013"},   {"gfx1013"}, GK_GFX1013, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_XNACK|FEATURE_WGP},
    {{"gfx1030"},   {"gfx1030"}, GK_GFX1030, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1031"},   {"gfx1031"}, GK_GFX1031, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1032"},   {"gfx1032"}, GK_GFX1032, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1033"},   {"gfx1033"}, GK_GFX1033, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1034"},   {"gfx1034"}, GK_GFX1034, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1035"},   {"gfx1035"}, GK_GFX1035, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1036"},   {"gfx1036"}, GK_GFX1036, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1100"},   {"gfx1100"}, GK_GFX1100, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1101"},   {"gfx1101"}, GK_GFX1101, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1102"},   {"gfx1102"}, GK_GFX1102, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1103"},   {"gfx1103"}, GK_GFX1103, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1150"},   {"gfx1150"}, GK_GFX1150, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1151"},   {"gfx1151"}, GK_GFX1151, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1152"},   {"gfx1152"}, GK_GFX1152, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1200"},   {"gfx1200"}, GK_GFX1200, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx1201"},   {"gfx1201"}, GK_GFX1201, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},

    {{"gfx9-generic"},      {"gfx9-generic"},    GK_GFX9_GENERIC,    FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_XNACK},
    {{"gfx10-1-generic"},   {"gfx10-1-generic"}, GK_GFX10_1_GENERIC, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_XNACK|FEATURE_WGP},
    {{"gfx10-3-generic"},   {"gfx10-3-generic"}, GK_GFX10_3_GENERIC, FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx11-generic"},     {"gfx11-generic"},   GK_GFX11_GENERIC,   FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    {{"gfx12-generic"},     {"gfx12-generic"},   GK_GFX12_GENERIC,   FEATURE_FAST_FMA_F32|FEATURE_FAST_DENORMAL_F32|FEATURE_WAVE32|FEATURE_WGP},
    // clang-format on
};

const GPUInfo *getArchEntry(AMDGPU::GPUKind AK, ArrayRef<GPUInfo> Table) {
  GPUInfo Search = { {""}, {""}, AK, AMDGPU::FEATURE_NONE };

  auto I =
      llvm::lower_bound(Table, Search, [](const GPUInfo &A, const GPUInfo &B) {
        return A.Kind < B.Kind;
      });

  if (I == Table.end() || I->Kind != Search.Kind)
    return nullptr;
  return I;
}

} // namespace

StringRef llvm::AMDGPU::getArchFamilyNameAMDGCN(GPUKind AK) {
  switch (AK) {
  case AMDGPU::GK_GFX9_GENERIC:
    return "gfx9";
  case AMDGPU::GK_GFX10_1_GENERIC:
  case AMDGPU::GK_GFX10_3_GENERIC:
    return "gfx10";
  case AMDGPU::GK_GFX11_GENERIC:
    return "gfx11";
  case AMDGPU::GK_GFX12_GENERIC:
    return "gfx12";
  default: {
    StringRef ArchName = getArchNameAMDGCN(AK);
    return ArchName.empty() ? "" : ArchName.drop_back(2);
  }
  }
}

StringRef llvm::AMDGPU::getArchNameAMDGCN(GPUKind AK) {
  if (const auto *Entry = getArchEntry(AK, AMDGCNGPUs))
    return Entry->CanonicalName;
  return "";
}

StringRef llvm::AMDGPU::getArchNameR600(GPUKind AK) {
  if (const auto *Entry = getArchEntry(AK, R600GPUs))
    return Entry->CanonicalName;
  return "";
}

AMDGPU::GPUKind llvm::AMDGPU::parseArchAMDGCN(StringRef CPU) {
  for (const auto &C : AMDGCNGPUs) {
    if (CPU == C.Name)
      return C.Kind;
  }

  return AMDGPU::GPUKind::GK_NONE;
}

AMDGPU::GPUKind llvm::AMDGPU::parseArchR600(StringRef CPU) {
  for (const auto &C : R600GPUs) {
    if (CPU == C.Name)
      return C.Kind;
  }

  return AMDGPU::GPUKind::GK_NONE;
}

unsigned AMDGPU::getArchAttrAMDGCN(GPUKind AK) {
  if (const auto *Entry = getArchEntry(AK, AMDGCNGPUs))
    return Entry->Features;
  return FEATURE_NONE;
}

unsigned AMDGPU::getArchAttrR600(GPUKind AK) {
  if (const auto *Entry = getArchEntry(AK, R600GPUs))
    return Entry->Features;
  return FEATURE_NONE;
}

void AMDGPU::fillValidArchListAMDGCN(SmallVectorImpl<StringRef> &Values) {
  // XXX: Should this only report unique canonical names?
  for (const auto &C : AMDGCNGPUs)
    Values.push_back(C.Name);
}

void AMDGPU::fillValidArchListR600(SmallVectorImpl<StringRef> &Values) {
  for (const auto &C : R600GPUs)
    Values.push_back(C.Name);
}

AMDGPU::IsaVersion AMDGPU::getIsaVersion(StringRef GPU) {
  AMDGPU::GPUKind AK = parseArchAMDGCN(GPU);
  if (AK == AMDGPU::GPUKind::GK_NONE) {
    if (GPU == "generic-hsa")
      return {7, 0, 0};
    if (GPU == "generic")
      return {6, 0, 0};
    return {0, 0, 0};
  }

  // clang-format off
  switch (AK) {
  case GK_GFX600:  return {6, 0, 0};
  case GK_GFX601:  return {6, 0, 1};
  case GK_GFX602:  return {6, 0, 2};
  case GK_GFX700:  return {7, 0, 0};
  case GK_GFX701:  return {7, 0, 1};
  case GK_GFX702:  return {7, 0, 2};
  case GK_GFX703:  return {7, 0, 3};
  case GK_GFX704:  return {7, 0, 4};
  case GK_GFX705:  return {7, 0, 5};
  case GK_GFX801:  return {8, 0, 1};
  case GK_GFX802:  return {8, 0, 2};
  case GK_GFX803:  return {8, 0, 3};
  case GK_GFX805:  return {8, 0, 5};
  case GK_GFX810:  return {8, 1, 0};
  case GK_GFX900:  return {9, 0, 0};
  case GK_GFX902:  return {9, 0, 2};
  case GK_GFX904:  return {9, 0, 4};
  case GK_GFX906:  return {9, 0, 6};
  case GK_GFX908:  return {9, 0, 8};
  case GK_GFX909:  return {9, 0, 9};
  case GK_GFX90A:  return {9, 0, 10};
  case GK_GFX90C:  return {9, 0, 12};
  case GK_GFX940:  return {9, 4, 0};
  case GK_GFX941:  return {9, 4, 1};
  case GK_GFX942:  return {9, 4, 2};
  case GK_GFX1010: return {10, 1, 0};
  case GK_GFX1011: return {10, 1, 1};
  case GK_GFX1012: return {10, 1, 2};
  case GK_GFX1013: return {10, 1, 3};
  case GK_GFX1030: return {10, 3, 0};
  case GK_GFX1031: return {10, 3, 1};
  case GK_GFX1032: return {10, 3, 2};
  case GK_GFX1033: return {10, 3, 3};
  case GK_GFX1034: return {10, 3, 4};
  case GK_GFX1035: return {10, 3, 5};
  case GK_GFX1036: return {10, 3, 6};
  case GK_GFX1100: return {11, 0, 0};
  case GK_GFX1101: return {11, 0, 1};
  case GK_GFX1102: return {11, 0, 2};
  case GK_GFX1103: return {11, 0, 3};
  case GK_GFX1150: return {11, 5, 0};
  case GK_GFX1151: return {11, 5, 1};
  case GK_GFX1152: return {11, 5, 2};
  case GK_GFX1200: return {12, 0, 0};
  case GK_GFX1201: return {12, 0, 1};

  // Generic targets return the lowest common denominator
  // within their family. That is, the ISA that is the most
  // restricted in terms of features.
  //
  // gfx9-generic is tricky because there is no lowest
  // common denominator, so we return gfx900 which has mad-mix
  // but this family doesn't have it.
  //
  // This API should never be used to check for a particular
  // feature anyway.
  //
  // TODO: Split up this API depending on its caller so
  // generic target handling is more obvious and less risky.
  case GK_GFX9_GENERIC:    return {9, 0, 0};
  case GK_GFX10_1_GENERIC: return {10, 1, 0};
  case GK_GFX10_3_GENERIC: return {10, 3, 0};
  case GK_GFX11_GENERIC:   return {11, 0, 3};
  case GK_GFX12_GENERIC:   return {12, 0, 0};
  default:         return {0, 0, 0};
  }
  // clang-format on
}

StringRef AMDGPU::getCanonicalArchName(const Triple &T, StringRef Arch) {
  assert(T.isAMDGPU());
  auto ProcKind = T.isAMDGCN() ? parseArchAMDGCN(Arch) : parseArchR600(Arch);
  if (ProcKind == GK_NONE)
    return StringRef();

  return T.isAMDGCN() ? getArchNameAMDGCN(ProcKind) : getArchNameR600(ProcKind);
}

void AMDGPU::fillAMDGPUFeatureMap(StringRef GPU, const Triple &T,
                                  StringMap<bool> &Features) {
  // XXX - What does the member GPU mean if device name string passed here?
  if (T.isSPIRV() && T.getOS() == Triple::OSType::AMDHSA) {
    // AMDGCN SPIRV must support the union of all AMDGCN features.
    Features["atomic-ds-pk-add-16-insts"] = true;
    Features["atomic-flat-pk-add-16-insts"] = true;
    Features["atomic-buffer-global-pk-add-f16-insts"] = true;
    Features["atomic-global-pk-add-bf16-inst"] = true;
    Features["atomic-fadd-rtn-insts"] = true;
    Features["ci-insts"] = true;
    Features["dot1-insts"] = true;
    Features["dot2-insts"] = true;
    Features["dot3-insts"] = true;
    Features["dot4-insts"] = true;
    Features["dot5-insts"] = true;
    Features["dot7-insts"] = true;
    Features["dot8-insts"] = true;
    Features["dot9-insts"] = true;
    Features["dot10-insts"] = true;
    Features["dot11-insts"] = true;
    Features["dl-insts"] = true;
    Features["16-bit-insts"] = true;
    Features["dpp"] = true;
    Features["gfx8-insts"] = true;
    Features["gfx9-insts"] = true;
    Features["gfx90a-insts"] = true;
    Features["gfx940-insts"] = true;
    Features["gfx10-insts"] = true;
    Features["gfx10-3-insts"] = true;
    Features["gfx11-insts"] = true;
    Features["gfx12-insts"] = true;
    Features["image-insts"] = true;
    Features["fp8-conversion-insts"] = true;
    Features["s-memrealtime"] = true;
    Features["s-memtime-inst"] = true;
    Features["gws"] = true;
    Features["fp8-insts"] = true;
    Features["fp8-conversion-insts"] = true;
    Features["atomic-ds-pk-add-16-insts"] = true;
    Features["mai-insts"] = true;
    Features["wavefrontsize32"] = true;
    Features["wavefrontsize64"] = true;
  } else if (T.isAMDGCN()) {
    switch (parseArchAMDGCN(GPU)) {
    case GK_GFX1201:
    case GK_GFX1200:
    case GK_GFX12_GENERIC:
      Features["ci-insts"] = true;
      Features["dot7-insts"] = true;
      Features["dot8-insts"] = true;
      Features["dot9-insts"] = true;
      Features["dot10-insts"] = true;
      Features["dot11-insts"] = true;
      Features["dl-insts"] = true;
      Features["atomic-ds-pk-add-16-insts"] = true;
      Features["atomic-flat-pk-add-16-insts"] = true;
      Features["atomic-buffer-global-pk-add-f16-insts"] = true;
      Features["atomic-global-pk-add-bf16-inst"] = true;
      Features["16-bit-insts"] = true;
      Features["dpp"] = true;
      Features["gfx8-insts"] = true;
      Features["gfx9-insts"] = true;
      Features["gfx10-insts"] = true;
      Features["gfx10-3-insts"] = true;
      Features["gfx11-insts"] = true;
      Features["gfx12-insts"] = true;
      Features["atomic-fadd-rtn-insts"] = true;
      Features["image-insts"] = true;
      Features["fp8-conversion-insts"] = true;
      break;
    case GK_GFX1152:
    case GK_GFX1151:
    case GK_GFX1150:
    case GK_GFX1103:
    case GK_GFX1102:
    case GK_GFX1101:
    case GK_GFX1100:
    case GK_GFX11_GENERIC:
      Features["ci-insts"] = true;
      Features["dot5-insts"] = true;
      Features["dot7-insts"] = true;
      Features["dot8-insts"] = true;
      Features["dot9-insts"] = true;
      Features["dot10-insts"] = true;
      Features["dl-insts"] = true;
      Features["16-bit-insts"] = true;
      Features["dpp"] = true;
      Features["gfx8-insts"] = true;
      Features["gfx9-insts"] = true;
      Features["gfx10-insts"] = true;
      Features["gfx10-3-insts"] = true;
      Features["gfx11-insts"] = true;
      Features["atomic-fadd-rtn-insts"] = true;
      Features["image-insts"] = true;
      Features["gws"] = true;
      break;
    case GK_GFX1036:
    case GK_GFX1035:
    case GK_GFX1034:
    case GK_GFX1033:
    case GK_GFX1032:
    case GK_GFX1031:
    case GK_GFX1030:
    case GK_GFX10_3_GENERIC:
      Features["ci-insts"] = true;
      Features["dot1-insts"] = true;
      Features["dot2-insts"] = true;
      Features["dot5-insts"] = true;
      Features["dot6-insts"] = true;
      Features["dot7-insts"] = true;
      Features["dot10-insts"] = true;
      Features["dl-insts"] = true;
      Features["16-bit-insts"] = true;
      Features["dpp"] = true;
      Features["gfx8-insts"] = true;
      Features["gfx9-insts"] = true;
      Features["gfx10-insts"] = true;
      Features["gfx10-3-insts"] = true;
      Features["image-insts"] = true;
      Features["s-memrealtime"] = true;
      Features["s-memtime-inst"] = true;
      Features["gws"] = true;
      break;
    case GK_GFX1012:
    case GK_GFX1011:
      Features["dot1-insts"] = true;
      Features["dot2-insts"] = true;
      Features["dot5-insts"] = true;
      Features["dot6-insts"] = true;
      Features["dot7-insts"] = true;
      Features["dot10-insts"] = true;
      [[fallthrough]];
    case GK_GFX1013:
    case GK_GFX1010:
    case GK_GFX10_1_GENERIC:
      Features["dl-insts"] = true;
      Features["ci-insts"] = true;
      Features["16-bit-insts"] = true;
      Features["dpp"] = true;
      Features["gfx8-insts"] = true;
      Features["gfx9-insts"] = true;
      Features["gfx10-insts"] = true;
      Features["image-insts"] = true;
      Features["s-memrealtime"] = true;
      Features["s-memtime-inst"] = true;
      Features["gws"] = true;
      break;
    case GK_GFX942:
    case GK_GFX941:
    case GK_GFX940:
      Features["gfx940-insts"] = true;
      Features["fp8-insts"] = true;
      Features["fp8-conversion-insts"] = true;
      Features["atomic-ds-pk-add-16-insts"] = true;
      Features["atomic-flat-pk-add-16-insts"] = true;
      Features["atomic-global-pk-add-bf16-inst"] = true;
      Features["gfx90a-insts"] = true;
      Features["atomic-buffer-global-pk-add-f16-insts"] = true;
      Features["atomic-fadd-rtn-insts"] = true;
      Features["dot3-insts"] = true;
      Features["dot4-insts"] = true;
      Features["dot5-insts"] = true;
      Features["dot6-insts"] = true;
      Features["mai-insts"] = true;
      Features["dl-insts"] = true;
      Features["dot1-insts"] = true;
      Features["dot2-insts"] = true;
      Features["dot7-insts"] = true;
      Features["dot10-insts"] = true;
      Features["gfx9-insts"] = true;
      Features["gfx8-insts"] = true;
      Features["16-bit-insts"] = true;
      Features["dpp"] = true;
      Features["s-memrealtime"] = true;
      Features["ci-insts"] = true;
      Features["s-memtime-inst"] = true;
      Features["gws"] = true;
      break;
    case GK_GFX90A:
      Features["gfx90a-insts"] = true;
      Features["atomic-buffer-global-pk-add-f16-insts"] = true;
      Features["atomic-fadd-rtn-insts"] = true;
      [[fallthrough]];
    case GK_GFX908:
      Features["dot3-insts"] = true;
      Features["dot4-insts"] = true;
      Features["dot5-insts"] = true;
      Features["dot6-insts"] = true;
      Features["mai-insts"] = true;
      [[fallthrough]];
    case GK_GFX906:
      Features["dl-insts"] = true;
      Features["dot1-insts"] = true;
      Features["dot2-insts"] = true;
      Features["dot7-insts"] = true;
      Features["dot10-insts"] = true;
      [[fallthrough]];
    case GK_GFX90C:
    case GK_GFX909:
    case GK_GFX904:
    case GK_GFX902:
    case GK_GFX900:
    case GK_GFX9_GENERIC:
      Features["gfx9-insts"] = true;
      [[fallthrough]];
    case GK_GFX810:
    case GK_GFX805:
    case GK_GFX803:
    case GK_GFX802:
    case GK_GFX801:
      Features["gfx8-insts"] = true;
      Features["16-bit-insts"] = true;
      Features["dpp"] = true;
      Features["s-memrealtime"] = true;
      [[fallthrough]];
    case GK_GFX705:
    case GK_GFX704:
    case GK_GFX703:
    case GK_GFX702:
    case GK_GFX701:
    case GK_GFX700:
      Features["ci-insts"] = true;
      [[fallthrough]];
    case GK_GFX602:
    case GK_GFX601:
    case GK_GFX600:
      Features["image-insts"] = true;
      Features["s-memtime-inst"] = true;
      Features["gws"] = true;
      break;
    case GK_NONE:
      break;
    default:
      llvm_unreachable("Unhandled GPU!");
    }
  } else {
    if (GPU.empty())
      GPU = "r600";

    switch (llvm::AMDGPU::parseArchR600(GPU)) {
    case GK_CAYMAN:
    case GK_CYPRESS:
    case GK_RV770:
    case GK_RV670:
      // TODO: Add fp64 when implemented.
      break;
    case GK_TURKS:
    case GK_CAICOS:
    case GK_BARTS:
    case GK_SUMO:
    case GK_REDWOOD:
    case GK_JUNIPER:
    case GK_CEDAR:
    case GK_RV730:
    case GK_RV710:
    case GK_RS880:
    case GK_R630:
    case GK_R600:
      break;
    default:
      llvm_unreachable("Unhandled GPU!");
    }
  }
}

static bool isWave32Capable(StringRef GPU, const Triple &T) {
  bool IsWave32Capable = false;
  // XXX - What does the member GPU mean if device name string passed here?
  if (T.isAMDGCN()) {
    switch (parseArchAMDGCN(GPU)) {
    case GK_GFX1201:
    case GK_GFX1200:
    case GK_GFX1152:
    case GK_GFX1151:
    case GK_GFX1150:
    case GK_GFX1103:
    case GK_GFX1102:
    case GK_GFX1101:
    case GK_GFX1100:
    case GK_GFX1036:
    case GK_GFX1035:
    case GK_GFX1034:
    case GK_GFX1033:
    case GK_GFX1032:
    case GK_GFX1031:
    case GK_GFX1030:
    case GK_GFX1012:
    case GK_GFX1011:
    case GK_GFX1013:
    case GK_GFX1010:
    case GK_GFX12_GENERIC:
    case GK_GFX11_GENERIC:
    case GK_GFX10_3_GENERIC:
    case GK_GFX10_1_GENERIC:
      IsWave32Capable = true;
      break;
    default:
      break;
    }
  }
  return IsWave32Capable;
}

std::pair<FeatureError, StringRef>
AMDGPU::insertWaveSizeFeature(StringRef GPU, const Triple &T,
                              StringMap<bool> &Features) {
  bool IsWave32Capable = isWave32Capable(GPU, T);
  const bool IsNullGPU = GPU.empty();
  const bool HaveWave32 = Features.count("wavefrontsize32");
  const bool HaveWave64 = Features.count("wavefrontsize64");
  if (HaveWave32 && HaveWave64) {
    return {AMDGPU::INVALID_FEATURE_COMBINATION,
            "'wavefrontsize32' and 'wavefrontsize64' are mutually exclusive"};
  }
  if (HaveWave32 && !IsNullGPU && !IsWave32Capable) {
    return {AMDGPU::UNSUPPORTED_TARGET_FEATURE, "wavefrontsize32"};
  }
  // Don't assume any wavesize with an unknown subtarget.
  if (!IsNullGPU) {
    // Default to wave32 if available, or wave64 if not
    if (!HaveWave32 && !HaveWave64) {
      StringRef DefaultWaveSizeFeature =
          IsWave32Capable ? "wavefrontsize32" : "wavefrontsize64";
      Features.insert(std::make_pair(DefaultWaveSizeFeature, true));
    }
  }
  return {NO_ERROR, StringRef()};
}
