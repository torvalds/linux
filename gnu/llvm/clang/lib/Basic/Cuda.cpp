#include "clang/Basic/Cuda.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/VersionTuple.h"

namespace clang {

struct CudaVersionMapEntry {
  const char *Name;
  CudaVersion Version;
  llvm::VersionTuple TVersion;
};
#define CUDA_ENTRY(major, minor)                                               \
  {                                                                            \
    #major "." #minor, CudaVersion::CUDA_##major##minor,                       \
        llvm::VersionTuple(major, minor)                                       \
  }

static const CudaVersionMapEntry CudaNameVersionMap[] = {
    CUDA_ENTRY(7, 0),
    CUDA_ENTRY(7, 5),
    CUDA_ENTRY(8, 0),
    CUDA_ENTRY(9, 0),
    CUDA_ENTRY(9, 1),
    CUDA_ENTRY(9, 2),
    CUDA_ENTRY(10, 0),
    CUDA_ENTRY(10, 1),
    CUDA_ENTRY(10, 2),
    CUDA_ENTRY(11, 0),
    CUDA_ENTRY(11, 1),
    CUDA_ENTRY(11, 2),
    CUDA_ENTRY(11, 3),
    CUDA_ENTRY(11, 4),
    CUDA_ENTRY(11, 5),
    CUDA_ENTRY(11, 6),
    CUDA_ENTRY(11, 7),
    CUDA_ENTRY(11, 8),
    CUDA_ENTRY(12, 0),
    CUDA_ENTRY(12, 1),
    CUDA_ENTRY(12, 2),
    CUDA_ENTRY(12, 3),
    CUDA_ENTRY(12, 4),
    CUDA_ENTRY(12, 5),
    {"", CudaVersion::NEW, llvm::VersionTuple(std::numeric_limits<int>::max())},
    {"unknown", CudaVersion::UNKNOWN, {}} // End of list tombstone.
};
#undef CUDA_ENTRY

const char *CudaVersionToString(CudaVersion V) {
  for (auto *I = CudaNameVersionMap; I->Version != CudaVersion::UNKNOWN; ++I)
    if (I->Version == V)
      return I->Name;

  return CudaVersionToString(CudaVersion::UNKNOWN);
}

CudaVersion CudaStringToVersion(const llvm::Twine &S) {
  std::string VS = S.str();
  for (auto *I = CudaNameVersionMap; I->Version != CudaVersion::UNKNOWN; ++I)
    if (I->Name == VS)
      return I->Version;
  return CudaVersion::UNKNOWN;
}

CudaVersion ToCudaVersion(llvm::VersionTuple Version) {
  for (auto *I = CudaNameVersionMap; I->Version != CudaVersion::UNKNOWN; ++I)
    if (I->TVersion == Version)
      return I->Version;
  return CudaVersion::UNKNOWN;
}

namespace {
struct OffloadArchToStringMap {
  OffloadArch arch;
  const char *arch_name;
  const char *virtual_arch_name;
};
} // namespace

#define SM2(sm, ca) {OffloadArch::SM_##sm, "sm_" #sm, ca}
#define SM(sm) SM2(sm, "compute_" #sm)
#define GFX(gpu) {OffloadArch::GFX##gpu, "gfx" #gpu, "compute_amdgcn"}
static const OffloadArchToStringMap arch_names[] = {
    // clang-format off
    {OffloadArch::UNUSED, "", ""},
    SM2(20, "compute_20"), SM2(21, "compute_20"), // Fermi
    SM(30), {OffloadArch::SM_32_, "sm_32", "compute_32"}, SM(35), SM(37),  // Kepler
    SM(50), SM(52), SM(53),          // Maxwell
    SM(60), SM(61), SM(62),          // Pascal
    SM(70), SM(72),                  // Volta
    SM(75),                          // Turing
    SM(80), SM(86),                  // Ampere
    SM(87),                          // Jetson/Drive AGX Orin
    SM(89),                          // Ada Lovelace
    SM(90),                          // Hopper
    SM(90a),                         // Hopper
    GFX(600),  // gfx600
    GFX(601),  // gfx601
    GFX(602),  // gfx602
    GFX(700),  // gfx700
    GFX(701),  // gfx701
    GFX(702),  // gfx702
    GFX(703),  // gfx703
    GFX(704),  // gfx704
    GFX(705),  // gfx705
    GFX(801),  // gfx801
    GFX(802),  // gfx802
    GFX(803),  // gfx803
    GFX(805),  // gfx805
    GFX(810),  // gfx810
    {OffloadArch::GFX9_GENERIC, "gfx9-generic", "compute_amdgcn"},
    GFX(900),  // gfx900
    GFX(902),  // gfx902
    GFX(904),  // gfx903
    GFX(906),  // gfx906
    GFX(908),  // gfx908
    GFX(909),  // gfx909
    GFX(90a),  // gfx90a
    GFX(90c),  // gfx90c
    GFX(940),  // gfx940
    GFX(941),  // gfx941
    GFX(942),  // gfx942
    {OffloadArch::GFX10_1_GENERIC, "gfx10-1-generic", "compute_amdgcn"},
    GFX(1010), // gfx1010
    GFX(1011), // gfx1011
    GFX(1012), // gfx1012
    GFX(1013), // gfx1013
    {OffloadArch::GFX10_3_GENERIC, "gfx10-3-generic", "compute_amdgcn"},
    GFX(1030), // gfx1030
    GFX(1031), // gfx1031
    GFX(1032), // gfx1032
    GFX(1033), // gfx1033
    GFX(1034), // gfx1034
    GFX(1035), // gfx1035
    GFX(1036), // gfx1036
    {OffloadArch::GFX11_GENERIC, "gfx11-generic", "compute_amdgcn"},
    GFX(1100), // gfx1100
    GFX(1101), // gfx1101
    GFX(1102), // gfx1102
    GFX(1103), // gfx1103
    GFX(1150), // gfx1150
    GFX(1151), // gfx1151
    GFX(1152), // gfx1152
    {OffloadArch::GFX12_GENERIC, "gfx12-generic", "compute_amdgcn"},
    GFX(1200), // gfx1200
    GFX(1201), // gfx1201
    {OffloadArch::AMDGCNSPIRV, "amdgcnspirv", "compute_amdgcn"},
    {OffloadArch::Generic, "generic", ""},
    // clang-format on
};
#undef SM
#undef SM2
#undef GFX

const char *OffloadArchToString(OffloadArch A) {
  auto result = std::find_if(
      std::begin(arch_names), std::end(arch_names),
      [A](const OffloadArchToStringMap &map) { return A == map.arch; });
  if (result == std::end(arch_names))
    return "unknown";
  return result->arch_name;
}

const char *OffloadArchToVirtualArchString(OffloadArch A) {
  auto result = std::find_if(
      std::begin(arch_names), std::end(arch_names),
      [A](const OffloadArchToStringMap &map) { return A == map.arch; });
  if (result == std::end(arch_names))
    return "unknown";
  return result->virtual_arch_name;
}

OffloadArch StringToOffloadArch(llvm::StringRef S) {
  auto result = std::find_if(
      std::begin(arch_names), std::end(arch_names),
      [S](const OffloadArchToStringMap &map) { return S == map.arch_name; });
  if (result == std::end(arch_names))
    return OffloadArch::UNKNOWN;
  return result->arch;
}

CudaVersion MinVersionForOffloadArch(OffloadArch A) {
  if (A == OffloadArch::UNKNOWN)
    return CudaVersion::UNKNOWN;

  // AMD GPUs do not depend on CUDA versions.
  if (IsAMDOffloadArch(A))
    return CudaVersion::CUDA_70;

  switch (A) {
  case OffloadArch::SM_20:
  case OffloadArch::SM_21:
  case OffloadArch::SM_30:
  case OffloadArch::SM_32_:
  case OffloadArch::SM_35:
  case OffloadArch::SM_37:
  case OffloadArch::SM_50:
  case OffloadArch::SM_52:
  case OffloadArch::SM_53:
    return CudaVersion::CUDA_70;
  case OffloadArch::SM_60:
  case OffloadArch::SM_61:
  case OffloadArch::SM_62:
    return CudaVersion::CUDA_80;
  case OffloadArch::SM_70:
    return CudaVersion::CUDA_90;
  case OffloadArch::SM_72:
    return CudaVersion::CUDA_91;
  case OffloadArch::SM_75:
    return CudaVersion::CUDA_100;
  case OffloadArch::SM_80:
    return CudaVersion::CUDA_110;
  case OffloadArch::SM_86:
    return CudaVersion::CUDA_111;
  case OffloadArch::SM_87:
    return CudaVersion::CUDA_114;
  case OffloadArch::SM_89:
  case OffloadArch::SM_90:
    return CudaVersion::CUDA_118;
  case OffloadArch::SM_90a:
    return CudaVersion::CUDA_120;
  default:
    llvm_unreachable("invalid enum");
  }
}

CudaVersion MaxVersionForOffloadArch(OffloadArch A) {
  // AMD GPUs do not depend on CUDA versions.
  if (IsAMDOffloadArch(A))
    return CudaVersion::NEW;

  switch (A) {
  case OffloadArch::UNKNOWN:
    return CudaVersion::UNKNOWN;
  case OffloadArch::SM_20:
  case OffloadArch::SM_21:
    return CudaVersion::CUDA_80;
  case OffloadArch::SM_30:
  case OffloadArch::SM_32_:
    return CudaVersion::CUDA_102;
  case OffloadArch::SM_35:
  case OffloadArch::SM_37:
    return CudaVersion::CUDA_118;
  default:
    return CudaVersion::NEW;
  }
}

bool CudaFeatureEnabled(llvm::VersionTuple Version, CudaFeature Feature) {
  return CudaFeatureEnabled(ToCudaVersion(Version), Feature);
}

bool CudaFeatureEnabled(CudaVersion Version, CudaFeature Feature) {
  switch (Feature) {
  case CudaFeature::CUDA_USES_NEW_LAUNCH:
    return Version >= CudaVersion::CUDA_92;
  case CudaFeature::CUDA_USES_FATBIN_REGISTER_END:
    return Version >= CudaVersion::CUDA_101;
  }
  llvm_unreachable("Unknown CUDA feature.");
}
} // namespace clang
