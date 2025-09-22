//===--- X86.cpp - Implement X86 target feature support -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements X86 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include <optional>

namespace clang {
namespace targets {

static constexpr Builtin::Info BuiltinInfoX86[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_HEADER_BUILTIN(ID, TYPE, ATTRS, HEADER, LANGS, FEATURE)         \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::HEADER, LANGS},
#include "clang/Basic/BuiltinsX86.def"

#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_HEADER_BUILTIN(ID, TYPE, ATTRS, HEADER, LANGS, FEATURE)         \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::HEADER, LANGS},
#include "clang/Basic/BuiltinsX86_64.def"
};

static const char *const GCCRegNames[] = {
    "ax",    "dx",    "cx",    "bx",    "si",      "di",    "bp",    "sp",
    "st",    "st(1)", "st(2)", "st(3)", "st(4)",   "st(5)", "st(6)", "st(7)",
    "argp",  "flags", "fpcr",  "fpsr",  "dirflag", "frame", "xmm0",  "xmm1",
    "xmm2",  "xmm3",  "xmm4",  "xmm5",  "xmm6",    "xmm7",  "mm0",   "mm1",
    "mm2",   "mm3",   "mm4",   "mm5",   "mm6",     "mm7",   "r8",    "r9",
    "r10",   "r11",   "r12",   "r13",   "r14",     "r15",   "xmm8",  "xmm9",
    "xmm10", "xmm11", "xmm12", "xmm13", "xmm14",   "xmm15", "ymm0",  "ymm1",
    "ymm2",  "ymm3",  "ymm4",  "ymm5",  "ymm6",    "ymm7",  "ymm8",  "ymm9",
    "ymm10", "ymm11", "ymm12", "ymm13", "ymm14",   "ymm15", "xmm16", "xmm17",
    "xmm18", "xmm19", "xmm20", "xmm21", "xmm22",   "xmm23", "xmm24", "xmm25",
    "xmm26", "xmm27", "xmm28", "xmm29", "xmm30",   "xmm31", "ymm16", "ymm17",
    "ymm18", "ymm19", "ymm20", "ymm21", "ymm22",   "ymm23", "ymm24", "ymm25",
    "ymm26", "ymm27", "ymm28", "ymm29", "ymm30",   "ymm31", "zmm0",  "zmm1",
    "zmm2",  "zmm3",  "zmm4",  "zmm5",  "zmm6",    "zmm7",  "zmm8",  "zmm9",
    "zmm10", "zmm11", "zmm12", "zmm13", "zmm14",   "zmm15", "zmm16", "zmm17",
    "zmm18", "zmm19", "zmm20", "zmm21", "zmm22",   "zmm23", "zmm24", "zmm25",
    "zmm26", "zmm27", "zmm28", "zmm29", "zmm30",   "zmm31", "k0",    "k1",
    "k2",    "k3",    "k4",    "k5",    "k6",      "k7",
    "cr0",   "cr2",   "cr3",   "cr4",   "cr8",
    "dr0",   "dr1",   "dr2",   "dr3",   "dr6",     "dr7",
    "bnd0",  "bnd1",  "bnd2",  "bnd3",
    "tmm0",  "tmm1",  "tmm2",  "tmm3",  "tmm4",    "tmm5",  "tmm6",  "tmm7",
    "r16",   "r17",   "r18",   "r19",   "r20",     "r21",   "r22",   "r23",
    "r24",   "r25",   "r26",   "r27",   "r28",     "r29",   "r30",   "r31",
};

const TargetInfo::AddlRegName AddlRegNames[] = {
    {{"al", "ah", "eax", "rax"}, 0},
    {{"bl", "bh", "ebx", "rbx"}, 3},
    {{"cl", "ch", "ecx", "rcx"}, 2},
    {{"dl", "dh", "edx", "rdx"}, 1},
    {{"esi", "rsi"}, 4},
    {{"edi", "rdi"}, 5},
    {{"esp", "rsp"}, 7},
    {{"ebp", "rbp"}, 6},
    {{"r8d", "r8w", "r8b"}, 38},
    {{"r9d", "r9w", "r9b"}, 39},
    {{"r10d", "r10w", "r10b"}, 40},
    {{"r11d", "r11w", "r11b"}, 41},
    {{"r12d", "r12w", "r12b"}, 42},
    {{"r13d", "r13w", "r13b"}, 43},
    {{"r14d", "r14w", "r14b"}, 44},
    {{"r15d", "r15w", "r15b"}, 45},
    {{"r16d", "r16w", "r16b"}, 165},
    {{"r17d", "r17w", "r17b"}, 166},
    {{"r18d", "r18w", "r18b"}, 167},
    {{"r19d", "r19w", "r19b"}, 168},
    {{"r20d", "r20w", "r20b"}, 169},
    {{"r21d", "r21w", "r21b"}, 170},
    {{"r22d", "r22w", "r22b"}, 171},
    {{"r23d", "r23w", "r23b"}, 172},
    {{"r24d", "r24w", "r24b"}, 173},
    {{"r25d", "r25w", "r25b"}, 174},
    {{"r26d", "r26w", "r26b"}, 175},
    {{"r27d", "r27w", "r27b"}, 176},
    {{"r28d", "r28w", "r28b"}, 177},
    {{"r29d", "r29w", "r29b"}, 178},
    {{"r30d", "r30w", "r30b"}, 179},
    {{"r31d", "r31w", "r31b"}, 180},
};
} // namespace targets
} // namespace clang

using namespace clang;
using namespace clang::targets;

bool X86TargetInfo::setFPMath(StringRef Name) {
  if (Name == "387") {
    FPMath = FP_387;
    return true;
  }
  if (Name == "sse") {
    FPMath = FP_SSE;
    return true;
  }
  return false;
}

bool X86TargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {
  // FIXME: This *really* should not be here.
  // X86_64 always has SSE2.
  if (getTriple().getArch() == llvm::Triple::x86_64)
    setFeatureEnabled(Features, "sse2", true);

  using namespace llvm::X86;

  SmallVector<StringRef, 16> CPUFeatures;
  getFeaturesForCPU(CPU, CPUFeatures);
  for (auto &F : CPUFeatures)
    setFeatureEnabled(Features, F, true);

  std::vector<std::string> UpdatedFeaturesVec;
  std::vector<std::string> UpdatedAVX10FeaturesVec;
  enum { FE_NOSET = -1, FE_FALSE, FE_TRUE };
  int HasEVEX512 = FE_NOSET;
  bool HasAVX512F = Features.lookup("avx512f");
  bool HasAVX10 = Features.lookup("avx10.1-256");
  bool HasAVX10_512 = Features.lookup("avx10.1-512");
  std::string LastAVX10;
  std::string LastAVX512;
  for (const auto &Feature : FeaturesVec) {
    // Expand general-regs-only to -x86, -mmx and -sse
    if (Feature == "+general-regs-only") {
      UpdatedFeaturesVec.push_back("-x87");
      UpdatedFeaturesVec.push_back("-mmx");
      UpdatedFeaturesVec.push_back("-sse");
      continue;
    }

    if (Feature.substr(1, 6) == "avx10.") {
      if (Feature[0] == '+') {
        HasAVX10 = true;
        if (StringRef(Feature).ends_with("512"))
          HasAVX10_512 = true;
        LastAVX10 = Feature;
      } else if (HasAVX10 && Feature == "-avx10.1-256") {
        HasAVX10 = false;
        HasAVX10_512 = false;
      } else if (HasAVX10_512 && Feature == "-avx10.1-512") {
        HasAVX10_512 = false;
      }
      // Postpone AVX10 features handling after AVX512 settled.
      UpdatedAVX10FeaturesVec.push_back(Feature);
      continue;
    } else if (!HasAVX512F && StringRef(Feature).starts_with("+avx512")) {
      HasAVX512F = true;
      LastAVX512 = Feature;
    } else if (HasAVX512F && Feature == "-avx512f") {
      HasAVX512F = false;
    } else if (HasEVEX512 != FE_TRUE && Feature == "+evex512") {
      HasEVEX512 = FE_TRUE;
      continue;
    } else if (HasEVEX512 != FE_FALSE && Feature == "-evex512") {
      HasEVEX512 = FE_FALSE;
      continue;
    }

    UpdatedFeaturesVec.push_back(Feature);
  }
  llvm::append_range(UpdatedFeaturesVec, UpdatedAVX10FeaturesVec);
  // HasEVEX512 is a three-states flag. We need to turn it into [+-]evex512
  // according to other features.
  if (HasAVX512F) {
    UpdatedFeaturesVec.push_back(HasEVEX512 == FE_FALSE ? "-evex512"
                                                        : "+evex512");
    if (HasAVX10 && !HasAVX10_512 && HasEVEX512 != FE_FALSE)
      Diags.Report(diag::warn_invalid_feature_combination)
          << LastAVX512 + " " + LastAVX10 + "; will be promoted to avx10.1-512";
  } else if (HasAVX10) {
    if (HasEVEX512 != FE_NOSET)
      Diags.Report(diag::warn_invalid_feature_combination)
          << LastAVX10 + (HasEVEX512 == FE_TRUE ? " +evex512" : " -evex512");
    UpdatedFeaturesVec.push_back(HasAVX10_512 ? "+evex512" : "-evex512");
  }

  if (!TargetInfo::initFeatureMap(Features, Diags, CPU, UpdatedFeaturesVec))
    return false;

  // Can't do this earlier because we need to be able to explicitly enable
  // or disable these features and the things that they depend upon.

  // Enable popcnt if sse4.2 is enabled and popcnt is not explicitly disabled.
  auto I = Features.find("sse4.2");
  if (I != Features.end() && I->getValue() &&
      !llvm::is_contained(UpdatedFeaturesVec, "-popcnt"))
    Features["popcnt"] = true;

  // Additionally, if SSE is enabled and mmx is not explicitly disabled,
  // then enable MMX.
  I = Features.find("sse");
  if (I != Features.end() && I->getValue() &&
      !llvm::is_contained(UpdatedFeaturesVec, "-mmx"))
    Features["mmx"] = true;

  // Enable xsave if avx is enabled and xsave is not explicitly disabled.
  I = Features.find("avx");
  if (I != Features.end() && I->getValue() &&
      !llvm::is_contained(UpdatedFeaturesVec, "-xsave"))
    Features["xsave"] = true;

  // Enable CRC32 if SSE4.2 is enabled and CRC32 is not explicitly disabled.
  I = Features.find("sse4.2");
  if (I != Features.end() && I->getValue() &&
      !llvm::is_contained(UpdatedFeaturesVec, "-crc32"))
    Features["crc32"] = true;

  return true;
}

void X86TargetInfo::setFeatureEnabled(llvm::StringMap<bool> &Features,
                                      StringRef Name, bool Enabled) const {
  if (Name == "sse4") {
    // We can get here via the __target__ attribute since that's not controlled
    // via the -msse4/-mno-sse4 command line alias. Handle this the same way
    // here - turn on the sse4.2 if enabled, turn off the sse4.1 level if
    // disabled.
    if (Enabled)
      Name = "sse4.2";
    else
      Name = "sse4.1";
  }

  Features[Name] = Enabled;
  llvm::X86::updateImpliedFeatures(Name, Enabled, Features);
}

/// handleTargetFeatures - Perform initialization based on the user
/// configured set of features.
bool X86TargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                         DiagnosticsEngine &Diags) {
  for (const auto &Feature : Features) {
    if (Feature[0] != '+')
      continue;

    if (Feature == "+mmx") {
      HasMMX = true;
    } else if (Feature == "+aes") {
      HasAES = true;
    } else if (Feature == "+vaes") {
      HasVAES = true;
    } else if (Feature == "+pclmul") {
      HasPCLMUL = true;
    } else if (Feature == "+vpclmulqdq") {
      HasVPCLMULQDQ = true;
    } else if (Feature == "+lzcnt") {
      HasLZCNT = true;
    } else if (Feature == "+rdrnd") {
      HasRDRND = true;
    } else if (Feature == "+fsgsbase") {
      HasFSGSBASE = true;
    } else if (Feature == "+bmi") {
      HasBMI = true;
    } else if (Feature == "+bmi2") {
      HasBMI2 = true;
    } else if (Feature == "+popcnt") {
      HasPOPCNT = true;
    } else if (Feature == "+rtm") {
      HasRTM = true;
    } else if (Feature == "+prfchw") {
      HasPRFCHW = true;
    } else if (Feature == "+rdseed") {
      HasRDSEED = true;
    } else if (Feature == "+adx") {
      HasADX = true;
    } else if (Feature == "+tbm") {
      HasTBM = true;
    } else if (Feature == "+lwp") {
      HasLWP = true;
    } else if (Feature == "+fma") {
      HasFMA = true;
    } else if (Feature == "+f16c") {
      HasF16C = true;
    } else if (Feature == "+gfni") {
      HasGFNI = true;
    } else if (Feature == "+evex512") {
      HasEVEX512 = true;
    } else if (Feature == "+avx10.1-256") {
      HasAVX10_1 = true;
    } else if (Feature == "+avx10.1-512") {
      HasAVX10_1_512 = true;
    } else if (Feature == "+avx512cd") {
      HasAVX512CD = true;
    } else if (Feature == "+avx512vpopcntdq") {
      HasAVX512VPOPCNTDQ = true;
    } else if (Feature == "+avx512vnni") {
      HasAVX512VNNI = true;
    } else if (Feature == "+avx512bf16") {
      HasAVX512BF16 = true;
    } else if (Feature == "+avx512fp16") {
      HasAVX512FP16 = true;
      HasLegalHalfType = true;
    } else if (Feature == "+avx512dq") {
      HasAVX512DQ = true;
    } else if (Feature == "+avx512bitalg") {
      HasAVX512BITALG = true;
    } else if (Feature == "+avx512bw") {
      HasAVX512BW = true;
    } else if (Feature == "+avx512vl") {
      HasAVX512VL = true;
    } else if (Feature == "+avx512vbmi") {
      HasAVX512VBMI = true;
    } else if (Feature == "+avx512vbmi2") {
      HasAVX512VBMI2 = true;
    } else if (Feature == "+avx512ifma") {
      HasAVX512IFMA = true;
    } else if (Feature == "+avx512vp2intersect") {
      HasAVX512VP2INTERSECT = true;
    } else if (Feature == "+sha") {
      HasSHA = true;
    } else if (Feature == "+sha512") {
      HasSHA512 = true;
    } else if (Feature == "+shstk") {
      HasSHSTK = true;
    } else if (Feature == "+sm3") {
      HasSM3 = true;
    } else if (Feature == "+sm4") {
      HasSM4 = true;
    } else if (Feature == "+movbe") {
      HasMOVBE = true;
    } else if (Feature == "+sgx") {
      HasSGX = true;
    } else if (Feature == "+cx8") {
      HasCX8 = true;
    } else if (Feature == "+cx16") {
      HasCX16 = true;
    } else if (Feature == "+fxsr") {
      HasFXSR = true;
    } else if (Feature == "+xsave") {
      HasXSAVE = true;
    } else if (Feature == "+xsaveopt") {
      HasXSAVEOPT = true;
    } else if (Feature == "+xsavec") {
      HasXSAVEC = true;
    } else if (Feature == "+xsaves") {
      HasXSAVES = true;
    } else if (Feature == "+mwaitx") {
      HasMWAITX = true;
    } else if (Feature == "+pku") {
      HasPKU = true;
    } else if (Feature == "+clflushopt") {
      HasCLFLUSHOPT = true;
    } else if (Feature == "+clwb") {
      HasCLWB = true;
    } else if (Feature == "+wbnoinvd") {
      HasWBNOINVD = true;
    } else if (Feature == "+prefetchi") {
      HasPREFETCHI = true;
    } else if (Feature == "+clzero") {
      HasCLZERO = true;
    } else if (Feature == "+cldemote") {
      HasCLDEMOTE = true;
    } else if (Feature == "+rdpid") {
      HasRDPID = true;
    } else if (Feature == "+rdpru") {
      HasRDPRU = true;
    } else if (Feature == "+kl") {
      HasKL = true;
    } else if (Feature == "+widekl") {
      HasWIDEKL = true;
    } else if (Feature == "+retpoline-external-thunk") {
      HasRetpolineExternalThunk = true;
    } else if (Feature == "+sahf") {
      HasLAHFSAHF = true;
    } else if (Feature == "+waitpkg") {
      HasWAITPKG = true;
    } else if (Feature == "+movdiri") {
      HasMOVDIRI = true;
    } else if (Feature == "+movdir64b") {
      HasMOVDIR64B = true;
    } else if (Feature == "+pconfig") {
      HasPCONFIG = true;
    } else if (Feature == "+ptwrite") {
      HasPTWRITE = true;
    } else if (Feature == "+invpcid") {
      HasINVPCID = true;
    } else if (Feature == "+save-args") {
      HasSaveArgs = true;
    } else if (Feature == "+enqcmd") {
      HasENQCMD = true;
    } else if (Feature == "+hreset") {
      HasHRESET = true;
    } else if (Feature == "+amx-bf16") {
      HasAMXBF16 = true;
    } else if (Feature == "+amx-fp16") {
      HasAMXFP16 = true;
    } else if (Feature == "+amx-int8") {
      HasAMXINT8 = true;
    } else if (Feature == "+amx-tile") {
      HasAMXTILE = true;
    } else if (Feature == "+amx-complex") {
      HasAMXCOMPLEX = true;
    } else if (Feature == "+cmpccxadd") {
      HasCMPCCXADD = true;
    } else if (Feature == "+raoint") {
      HasRAOINT = true;
    } else if (Feature == "+avxifma") {
      HasAVXIFMA = true;
    } else if (Feature == "+avxneconvert") {
      HasAVXNECONVERT= true;
    } else if (Feature == "+avxvnni") {
      HasAVXVNNI = true;
    } else if (Feature == "+avxvnniint16") {
      HasAVXVNNIINT16 = true;
    } else if (Feature == "+avxvnniint8") {
      HasAVXVNNIINT8 = true;
    } else if (Feature == "+serialize") {
      HasSERIALIZE = true;
    } else if (Feature == "+tsxldtrk") {
      HasTSXLDTRK = true;
    } else if (Feature == "+uintr") {
      HasUINTR = true;
    } else if (Feature == "+usermsr") {
      HasUSERMSR = true;
    } else if (Feature == "+crc32") {
      HasCRC32 = true;
    } else if (Feature == "+x87") {
      HasX87 = true;
    } else if (Feature == "+fullbf16") {
      HasFullBFloat16 = true;
    } else if (Feature == "+egpr") {
      HasEGPR = true;
    } else if (Feature == "+inline-asm-use-gpr32") {
      HasInlineAsmUseGPR32 = true;
    } else if (Feature == "+push2pop2") {
      HasPush2Pop2 = true;
    } else if (Feature == "+ppx") {
      HasPPX = true;
    } else if (Feature == "+ndd") {
      HasNDD = true;
    } else if (Feature == "+ccmp") {
      HasCCMP = true;
    } else if (Feature == "+nf") {
      HasNF = true;
    } else if (Feature == "+cf") {
      HasCF = true;
    } else if (Feature == "+zu") {
      HasZU = true;
    } else if (Feature == "+branch-hint") {
      HasBranchHint = true;
    }

    X86SSEEnum Level = llvm::StringSwitch<X86SSEEnum>(Feature)
                           .Case("+avx512f", AVX512F)
                           .Case("+avx2", AVX2)
                           .Case("+avx", AVX)
                           .Case("+sse4.2", SSE42)
                           .Case("+sse4.1", SSE41)
                           .Case("+ssse3", SSSE3)
                           .Case("+sse3", SSE3)
                           .Case("+sse2", SSE2)
                           .Case("+sse", SSE1)
                           .Default(NoSSE);
    SSELevel = std::max(SSELevel, Level);

    HasFloat16 = SSELevel >= SSE2;

    // X86 target has bfloat16 emulation support in the backend, where
    // bfloat16 is treated as a 32-bit float, arithmetic operations are
    // performed in 32-bit, and the result is converted back to bfloat16.
    // Truncation and extension between bfloat16 and 32-bit float are supported
    // by the compiler-rt library. However, native bfloat16 support is currently
    // not available in the X86 target. Hence, HasFullBFloat16 will be false
    // until native bfloat16 support is available. HasFullBFloat16 is used to
    // determine whether to automatically use excess floating point precision
    // for bfloat16 arithmetic operations in the front-end.
    HasBFloat16 = SSELevel >= SSE2;

    XOPEnum XLevel = llvm::StringSwitch<XOPEnum>(Feature)
                         .Case("+xop", XOP)
                         .Case("+fma4", FMA4)
                         .Case("+sse4a", SSE4A)
                         .Default(NoXOP);
    XOPLevel = std::max(XOPLevel, XLevel);
  }

  // LLVM doesn't have a separate switch for fpmath, so only accept it if it
  // matches the selected sse level.
  if ((FPMath == FP_SSE && SSELevel < SSE1) ||
      (FPMath == FP_387 && SSELevel >= SSE1)) {
    Diags.Report(diag::err_target_unsupported_fpmath)
        << (FPMath == FP_SSE ? "sse" : "387");
    return false;
  }

  // FIXME: We should allow long double type on 32-bits to match with GCC.
  // This requires backend to be able to lower f80 without x87 first.
  if (!HasX87 && LongDoubleFormat == &llvm::APFloat::x87DoubleExtended())
    HasLongDouble = false;

  return true;
}

/// X86TargetInfo::getTargetDefines - Return the set of the X86-specific macro
/// definitions for this particular subtarget.
void X86TargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  // Inline assembly supports X86 flag outputs.
  Builder.defineMacro("__GCC_ASM_FLAG_OUTPUTS__");

  std::string CodeModel = getTargetOpts().CodeModel;
  if (CodeModel == "default")
    CodeModel = "small";
  Builder.defineMacro("__code_model_" + CodeModel + "__");

  // Target identification.
  if (getTriple().getArch() == llvm::Triple::x86_64) {
    Builder.defineMacro("__amd64__");
    Builder.defineMacro("__amd64");
    Builder.defineMacro("__x86_64");
    Builder.defineMacro("__x86_64__");
    if (getTriple().getArchName() == "x86_64h") {
      Builder.defineMacro("__x86_64h");
      Builder.defineMacro("__x86_64h__");
    }
  } else {
    DefineStd(Builder, "i386", Opts);
  }

  Builder.defineMacro("__SEG_GS");
  Builder.defineMacro("__SEG_FS");
  Builder.defineMacro("__seg_gs", "__attribute__((address_space(256)))");
  Builder.defineMacro("__seg_fs", "__attribute__((address_space(257)))");

  // Subtarget options.
  // FIXME: We are hard-coding the tune parameters based on the CPU, but they
  // truly should be based on -mtune options.
  using namespace llvm::X86;
  switch (CPU) {
  case CK_None:
    break;
  case CK_i386:
    // The rest are coming from the i386 define above.
    Builder.defineMacro("__tune_i386__");
    break;
  case CK_i486:
  case CK_WinChipC6:
  case CK_WinChip2:
  case CK_C3:
    defineCPUMacros(Builder, "i486");
    break;
  case CK_PentiumMMX:
    Builder.defineMacro("__pentium_mmx__");
    Builder.defineMacro("__tune_pentium_mmx__");
    [[fallthrough]];
  case CK_i586:
  case CK_Pentium:
    defineCPUMacros(Builder, "i586");
    defineCPUMacros(Builder, "pentium");
    break;
  case CK_Pentium3:
  case CK_PentiumM:
    Builder.defineMacro("__tune_pentium3__");
    [[fallthrough]];
  case CK_Pentium2:
  case CK_C3_2:
    Builder.defineMacro("__tune_pentium2__");
    [[fallthrough]];
  case CK_PentiumPro:
  case CK_i686:
    defineCPUMacros(Builder, "i686");
    defineCPUMacros(Builder, "pentiumpro");
    break;
  case CK_Pentium4:
    defineCPUMacros(Builder, "pentium4");
    break;
  case CK_Yonah:
  case CK_Prescott:
  case CK_Nocona:
    defineCPUMacros(Builder, "nocona");
    break;
  case CK_Core2:
  case CK_Penryn:
    defineCPUMacros(Builder, "core2");
    break;
  case CK_Bonnell:
    defineCPUMacros(Builder, "atom");
    break;
  case CK_Silvermont:
    defineCPUMacros(Builder, "slm");
    break;
  case CK_Goldmont:
    defineCPUMacros(Builder, "goldmont");
    break;
  case CK_GoldmontPlus:
    defineCPUMacros(Builder, "goldmont_plus");
    break;
  case CK_Tremont:
    defineCPUMacros(Builder, "tremont");
    break;
  // Gracemont and later atom-cores use P-core cpu macros.
  case CK_Gracemont:
  case CK_Nehalem:
  case CK_Westmere:
  case CK_SandyBridge:
  case CK_IvyBridge:
  case CK_Haswell:
  case CK_Broadwell:
  case CK_SkylakeClient:
  case CK_SkylakeServer:
  case CK_Cascadelake:
  case CK_Cooperlake:
  case CK_Cannonlake:
  case CK_IcelakeClient:
  case CK_Rocketlake:
  case CK_IcelakeServer:
  case CK_Tigerlake:
  case CK_SapphireRapids:
  case CK_Alderlake:
  case CK_Raptorlake:
  case CK_Meteorlake:
  case CK_Arrowlake:
  case CK_ArrowlakeS:
  case CK_Lunarlake:
  case CK_Pantherlake:
  case CK_Sierraforest:
  case CK_Grandridge:
  case CK_Graniterapids:
  case CK_GraniterapidsD:
  case CK_Emeraldrapids:
  case CK_Clearwaterforest:
    // FIXME: Historically, we defined this legacy name, it would be nice to
    // remove it at some point. We've never exposed fine-grained names for
    // recent primary x86 CPUs, and we should keep it that way.
    defineCPUMacros(Builder, "corei7");
    break;
  case CK_KNL:
    defineCPUMacros(Builder, "knl");
    break;
  case CK_KNM:
    break;
  case CK_Lakemont:
    defineCPUMacros(Builder, "i586", /*Tuning*/false);
    defineCPUMacros(Builder, "pentium", /*Tuning*/false);
    Builder.defineMacro("__tune_lakemont__");
    break;
  case CK_K6_2:
    Builder.defineMacro("__k6_2__");
    Builder.defineMacro("__tune_k6_2__");
    [[fallthrough]];
  case CK_K6_3:
    if (CPU != CK_K6_2) { // In case of fallthrough
      // FIXME: GCC may be enabling these in cases where some other k6
      // architecture is specified but -m3dnow is explicitly provided. The
      // exact semantics need to be determined and emulated here.
      Builder.defineMacro("__k6_3__");
      Builder.defineMacro("__tune_k6_3__");
    }
    [[fallthrough]];
  case CK_K6:
    defineCPUMacros(Builder, "k6");
    break;
  case CK_Athlon:
  case CK_AthlonXP:
    defineCPUMacros(Builder, "athlon");
    if (SSELevel != NoSSE) {
      Builder.defineMacro("__athlon_sse__");
      Builder.defineMacro("__tune_athlon_sse__");
    }
    break;
  case CK_K8:
  case CK_K8SSE3:
  case CK_x86_64:
    defineCPUMacros(Builder, "k8");
    break;
  case CK_x86_64_v2:
  case CK_x86_64_v3:
  case CK_x86_64_v4:
    break;
  case CK_AMDFAM10:
    defineCPUMacros(Builder, "amdfam10");
    break;
  case CK_BTVER1:
    defineCPUMacros(Builder, "btver1");
    break;
  case CK_BTVER2:
    defineCPUMacros(Builder, "btver2");
    break;
  case CK_BDVER1:
    defineCPUMacros(Builder, "bdver1");
    break;
  case CK_BDVER2:
    defineCPUMacros(Builder, "bdver2");
    break;
  case CK_BDVER3:
    defineCPUMacros(Builder, "bdver3");
    break;
  case CK_BDVER4:
    defineCPUMacros(Builder, "bdver4");
    break;
  case CK_ZNVER1:
    defineCPUMacros(Builder, "znver1");
    break;
  case CK_ZNVER2:
    defineCPUMacros(Builder, "znver2");
    break;
  case CK_ZNVER3:
    defineCPUMacros(Builder, "znver3");
    break;
  case CK_ZNVER4:
    defineCPUMacros(Builder, "znver4");
    break;
  case CK_ZNVER5:
    defineCPUMacros(Builder, "znver5");
    break;
  case CK_Geode:
    defineCPUMacros(Builder, "geode");
    break;
  }

  // Target properties.
  Builder.defineMacro("__REGISTER_PREFIX__", "");

  // Define __NO_MATH_INLINES on linux/x86 so that we don't get inline
  // functions in glibc header files that use FP Stack inline asm which the
  // backend can't deal with (PR879).
  Builder.defineMacro("__NO_MATH_INLINES");

  if (HasAES)
    Builder.defineMacro("__AES__");

  if (HasVAES)
    Builder.defineMacro("__VAES__");

  if (HasPCLMUL)
    Builder.defineMacro("__PCLMUL__");

  if (HasVPCLMULQDQ)
    Builder.defineMacro("__VPCLMULQDQ__");

  // Note, in 32-bit mode, GCC does not define the macro if -mno-sahf. In LLVM,
  // the feature flag only applies to 64-bit mode.
  if (HasLAHFSAHF || getTriple().getArch() == llvm::Triple::x86)
    Builder.defineMacro("__LAHF_SAHF__");

  if (HasLZCNT)
    Builder.defineMacro("__LZCNT__");

  if (HasRDRND)
    Builder.defineMacro("__RDRND__");

  if (HasFSGSBASE)
    Builder.defineMacro("__FSGSBASE__");

  if (HasBMI)
    Builder.defineMacro("__BMI__");

  if (HasBMI2)
    Builder.defineMacro("__BMI2__");

  if (HasPOPCNT)
    Builder.defineMacro("__POPCNT__");

  if (HasRTM)
    Builder.defineMacro("__RTM__");

  if (HasPRFCHW)
    Builder.defineMacro("__PRFCHW__");

  if (HasRDSEED)
    Builder.defineMacro("__RDSEED__");

  if (HasADX)
    Builder.defineMacro("__ADX__");

  if (HasTBM)
    Builder.defineMacro("__TBM__");

  if (HasLWP)
    Builder.defineMacro("__LWP__");

  if (HasMWAITX)
    Builder.defineMacro("__MWAITX__");

  if (HasMOVBE)
    Builder.defineMacro("__MOVBE__");

  switch (XOPLevel) {
  case XOP:
    Builder.defineMacro("__XOP__");
    [[fallthrough]];
  case FMA4:
    Builder.defineMacro("__FMA4__");
    [[fallthrough]];
  case SSE4A:
    Builder.defineMacro("__SSE4A__");
    [[fallthrough]];
  case NoXOP:
    break;
  }

  if (HasFMA)
    Builder.defineMacro("__FMA__");

  if (HasF16C)
    Builder.defineMacro("__F16C__");

  if (HasGFNI)
    Builder.defineMacro("__GFNI__");

  if (HasEVEX512)
    Builder.defineMacro("__EVEX512__");
  if (HasAVX10_1)
    Builder.defineMacro("__AVX10_1__");
  if (HasAVX10_1_512)
    Builder.defineMacro("__AVX10_1_512__");
  if (HasAVX512CD)
    Builder.defineMacro("__AVX512CD__");
  if (HasAVX512VPOPCNTDQ)
    Builder.defineMacro("__AVX512VPOPCNTDQ__");
  if (HasAVX512VNNI)
    Builder.defineMacro("__AVX512VNNI__");
  if (HasAVX512BF16)
    Builder.defineMacro("__AVX512BF16__");
  if (HasAVX512FP16)
    Builder.defineMacro("__AVX512FP16__");
  if (HasAVX512DQ)
    Builder.defineMacro("__AVX512DQ__");
  if (HasAVX512BITALG)
    Builder.defineMacro("__AVX512BITALG__");
  if (HasAVX512BW)
    Builder.defineMacro("__AVX512BW__");
  if (HasAVX512VL) {
    Builder.defineMacro("__AVX512VL__");
    Builder.defineMacro("__EVEX256__");
  }
  if (HasAVX512VBMI)
    Builder.defineMacro("__AVX512VBMI__");
  if (HasAVX512VBMI2)
    Builder.defineMacro("__AVX512VBMI2__");
  if (HasAVX512IFMA)
    Builder.defineMacro("__AVX512IFMA__");
  if (HasAVX512VP2INTERSECT)
    Builder.defineMacro("__AVX512VP2INTERSECT__");
  if (HasSHA)
    Builder.defineMacro("__SHA__");
  if (HasSHA512)
    Builder.defineMacro("__SHA512__");

  if (HasFXSR)
    Builder.defineMacro("__FXSR__");
  if (HasXSAVE)
    Builder.defineMacro("__XSAVE__");
  if (HasXSAVEOPT)
    Builder.defineMacro("__XSAVEOPT__");
  if (HasXSAVEC)
    Builder.defineMacro("__XSAVEC__");
  if (HasXSAVES)
    Builder.defineMacro("__XSAVES__");
  if (HasPKU)
    Builder.defineMacro("__PKU__");
  if (HasCLFLUSHOPT)
    Builder.defineMacro("__CLFLUSHOPT__");
  if (HasCLWB)
    Builder.defineMacro("__CLWB__");
  if (HasWBNOINVD)
    Builder.defineMacro("__WBNOINVD__");
  if (HasSHSTK)
    Builder.defineMacro("__SHSTK__");
  if (HasSGX)
    Builder.defineMacro("__SGX__");
  if (HasSM3)
    Builder.defineMacro("__SM3__");
  if (HasSM4)
    Builder.defineMacro("__SM4__");
  if (HasPREFETCHI)
    Builder.defineMacro("__PREFETCHI__");
  if (HasCLZERO)
    Builder.defineMacro("__CLZERO__");
  if (HasKL)
    Builder.defineMacro("__KL__");
  if (HasWIDEKL)
    Builder.defineMacro("__WIDEKL__");
  if (HasRDPID)
    Builder.defineMacro("__RDPID__");
  if (HasRDPRU)
    Builder.defineMacro("__RDPRU__");
  if (HasCLDEMOTE)
    Builder.defineMacro("__CLDEMOTE__");
  if (HasWAITPKG)
    Builder.defineMacro("__WAITPKG__");
  if (HasMOVDIRI)
    Builder.defineMacro("__MOVDIRI__");
  if (HasMOVDIR64B)
    Builder.defineMacro("__MOVDIR64B__");
  if (HasPCONFIG)
    Builder.defineMacro("__PCONFIG__");
  if (HasPTWRITE)
    Builder.defineMacro("__PTWRITE__");
  if (HasINVPCID)
    Builder.defineMacro("__INVPCID__");
  if (HasENQCMD)
    Builder.defineMacro("__ENQCMD__");
  if (HasHRESET)
    Builder.defineMacro("__HRESET__");
  if (HasAMXTILE)
    Builder.defineMacro("__AMX_TILE__");
  if (HasAMXINT8)
    Builder.defineMacro("__AMX_INT8__");
  if (HasAMXBF16)
    Builder.defineMacro("__AMX_BF16__");
  if (HasAMXFP16)
    Builder.defineMacro("__AMX_FP16__");
  if (HasAMXCOMPLEX)
    Builder.defineMacro("__AMX_COMPLEX__");
  if (HasCMPCCXADD)
    Builder.defineMacro("__CMPCCXADD__");
  if (HasRAOINT)
    Builder.defineMacro("__RAOINT__");
  if (HasAVXIFMA)
    Builder.defineMacro("__AVXIFMA__");
  if (HasAVXNECONVERT)
    Builder.defineMacro("__AVXNECONVERT__");
  if (HasAVXVNNI)
    Builder.defineMacro("__AVXVNNI__");
  if (HasAVXVNNIINT16)
    Builder.defineMacro("__AVXVNNIINT16__");
  if (HasAVXVNNIINT8)
    Builder.defineMacro("__AVXVNNIINT8__");
  if (HasSERIALIZE)
    Builder.defineMacro("__SERIALIZE__");
  if (HasTSXLDTRK)
    Builder.defineMacro("__TSXLDTRK__");
  if (HasUINTR)
    Builder.defineMacro("__UINTR__");
  if (HasUSERMSR)
    Builder.defineMacro("__USERMSR__");
  if (HasCRC32)
    Builder.defineMacro("__CRC32__");
  if (HasEGPR)
    Builder.defineMacro("__EGPR__");
  if (HasPush2Pop2)
    Builder.defineMacro("__PUSH2POP2__");
  if (HasPPX)
    Builder.defineMacro("__PPX__");
  if (HasNDD)
    Builder.defineMacro("__NDD__");
  if (HasCCMP)
    Builder.defineMacro("__CCMP__");
  if (HasNF)
    Builder.defineMacro("__NF__");
  if (HasCF)
    Builder.defineMacro("__CF__");
  if (HasZU)
    Builder.defineMacro("__ZU__");
  if (HasEGPR && HasPush2Pop2 && HasPPX && HasNDD && HasCCMP && HasNF &&
      HasCF && HasZU)
    Builder.defineMacro("__APX_F__");
  if (HasEGPR && HasInlineAsmUseGPR32)
    Builder.defineMacro("__APX_INLINE_ASM_USE_GPR32__");

  // Each case falls through to the previous one here.
  switch (SSELevel) {
  case AVX512F:
    Builder.defineMacro("__AVX512F__");
    [[fallthrough]];
  case AVX2:
    Builder.defineMacro("__AVX2__");
    [[fallthrough]];
  case AVX:
    Builder.defineMacro("__AVX__");
    [[fallthrough]];
  case SSE42:
    Builder.defineMacro("__SSE4_2__");
    [[fallthrough]];
  case SSE41:
    Builder.defineMacro("__SSE4_1__");
    [[fallthrough]];
  case SSSE3:
    Builder.defineMacro("__SSSE3__");
    [[fallthrough]];
  case SSE3:
    Builder.defineMacro("__SSE3__");
    [[fallthrough]];
  case SSE2:
    Builder.defineMacro("__SSE2__");
    Builder.defineMacro("__SSE2_MATH__"); // -mfp-math=sse always implied.
    [[fallthrough]];
  case SSE1:
    Builder.defineMacro("__SSE__");
    Builder.defineMacro("__SSE_MATH__"); // -mfp-math=sse always implied.
    [[fallthrough]];
  case NoSSE:
    break;
  }

  if (Opts.MicrosoftExt && getTriple().getArch() == llvm::Triple::x86) {
    switch (SSELevel) {
    case AVX512F:
    case AVX2:
    case AVX:
    case SSE42:
    case SSE41:
    case SSSE3:
    case SSE3:
    case SSE2:
      Builder.defineMacro("_M_IX86_FP", Twine(2));
      break;
    case SSE1:
      Builder.defineMacro("_M_IX86_FP", Twine(1));
      break;
    default:
      Builder.defineMacro("_M_IX86_FP", Twine(0));
      break;
    }
  }

  // Each case falls through to the previous one here.
  if (HasMMX) {
    Builder.defineMacro("__MMX__");
  }

  if (CPU >= CK_i486 || CPU == CK_None) {
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  }
  if (HasCX8)
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
  if (HasCX16 && getTriple().getArch() == llvm::Triple::x86_64)
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16");

  if (HasFloat128)
    Builder.defineMacro("__SIZEOF_FLOAT128__", "16");
}

bool X86TargetInfo::isValidFeatureName(StringRef Name) const {
  return llvm::StringSwitch<bool>(Name)
      .Case("adx", true)
      .Case("aes", true)
      .Case("amx-bf16", true)
      .Case("amx-complex", true)
      .Case("amx-fp16", true)
      .Case("amx-int8", true)
      .Case("amx-tile", true)
      .Case("avx", true)
      .Case("avx10.1-256", true)
      .Case("avx10.1-512", true)
      .Case("avx2", true)
      .Case("avx512f", true)
      .Case("avx512cd", true)
      .Case("avx512vpopcntdq", true)
      .Case("avx512vnni", true)
      .Case("avx512bf16", true)
      .Case("avx512fp16", true)
      .Case("avx512dq", true)
      .Case("avx512bitalg", true)
      .Case("avx512bw", true)
      .Case("avx512vl", true)
      .Case("avx512vbmi", true)
      .Case("avx512vbmi2", true)
      .Case("avx512ifma", true)
      .Case("avx512vp2intersect", true)
      .Case("avxifma", true)
      .Case("avxneconvert", true)
      .Case("avxvnni", true)
      .Case("avxvnniint16", true)
      .Case("avxvnniint8", true)
      .Case("bmi", true)
      .Case("bmi2", true)
      .Case("cldemote", true)
      .Case("clflushopt", true)
      .Case("clwb", true)
      .Case("clzero", true)
      .Case("cmpccxadd", true)
      .Case("crc32", true)
      .Case("cx16", true)
      .Case("enqcmd", true)
      .Case("evex512", true)
      .Case("f16c", true)
      .Case("fma", true)
      .Case("fma4", true)
      .Case("fsgsbase", true)
      .Case("fxsr", true)
      .Case("general-regs-only", true)
      .Case("gfni", true)
      .Case("hreset", true)
      .Case("invpcid", true)
      .Case("kl", true)
      .Case("widekl", true)
      .Case("lwp", true)
      .Case("lzcnt", true)
      .Case("mmx", true)
      .Case("movbe", true)
      .Case("movdiri", true)
      .Case("movdir64b", true)
      .Case("mwaitx", true)
      .Case("pclmul", true)
      .Case("pconfig", true)
      .Case("pku", true)
      .Case("popcnt", true)
      .Case("prefetchi", true)
      .Case("prfchw", true)
      .Case("ptwrite", true)
      .Case("raoint", true)
      .Case("rdpid", true)
      .Case("rdpru", true)
      .Case("rdrnd", true)
      .Case("rdseed", true)
      .Case("rtm", true)
      .Case("sahf", true)
      .Case("serialize", true)
      .Case("sgx", true)
      .Case("sha", true)
      .Case("sha512", true)
      .Case("shstk", true)
      .Case("sm3", true)
      .Case("sm4", true)
      .Case("sse", true)
      .Case("sse2", true)
      .Case("sse3", true)
      .Case("ssse3", true)
      .Case("sse4", true)
      .Case("sse4.1", true)
      .Case("sse4.2", true)
      .Case("sse4a", true)
      .Case("tbm", true)
      .Case("tsxldtrk", true)
      .Case("uintr", true)
      .Case("usermsr", true)
      .Case("vaes", true)
      .Case("vpclmulqdq", true)
      .Case("wbnoinvd", true)
      .Case("waitpkg", true)
      .Case("x87", true)
      .Case("xop", true)
      .Case("xsave", true)
      .Case("xsavec", true)
      .Case("xsaves", true)
      .Case("xsaveopt", true)
      .Case("egpr", true)
      .Case("push2pop2", true)
      .Case("ppx", true)
      .Case("ndd", true)
      .Case("ccmp", true)
      .Case("nf", true)
      .Case("cf", true)
      .Case("zu", true)
      .Default(false);
}

bool X86TargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("adx", HasADX)
      .Case("aes", HasAES)
      .Case("amx-bf16", HasAMXBF16)
      .Case("amx-complex", HasAMXCOMPLEX)
      .Case("amx-fp16", HasAMXFP16)
      .Case("amx-int8", HasAMXINT8)
      .Case("amx-tile", HasAMXTILE)
      .Case("avx", SSELevel >= AVX)
      .Case("avx10.1-256", HasAVX10_1)
      .Case("avx10.1-512", HasAVX10_1_512)
      .Case("avx2", SSELevel >= AVX2)
      .Case("avx512f", SSELevel >= AVX512F)
      .Case("avx512cd", HasAVX512CD)
      .Case("avx512vpopcntdq", HasAVX512VPOPCNTDQ)
      .Case("avx512vnni", HasAVX512VNNI)
      .Case("avx512bf16", HasAVX512BF16)
      .Case("avx512fp16", HasAVX512FP16)
      .Case("avx512dq", HasAVX512DQ)
      .Case("avx512bitalg", HasAVX512BITALG)
      .Case("avx512bw", HasAVX512BW)
      .Case("avx512vl", HasAVX512VL)
      .Case("avx512vbmi", HasAVX512VBMI)
      .Case("avx512vbmi2", HasAVX512VBMI2)
      .Case("avx512ifma", HasAVX512IFMA)
      .Case("avx512vp2intersect", HasAVX512VP2INTERSECT)
      .Case("avxifma", HasAVXIFMA)
      .Case("avxneconvert", HasAVXNECONVERT)
      .Case("avxvnni", HasAVXVNNI)
      .Case("avxvnniint16", HasAVXVNNIINT16)
      .Case("avxvnniint8", HasAVXVNNIINT8)
      .Case("bmi", HasBMI)
      .Case("bmi2", HasBMI2)
      .Case("cldemote", HasCLDEMOTE)
      .Case("clflushopt", HasCLFLUSHOPT)
      .Case("clwb", HasCLWB)
      .Case("clzero", HasCLZERO)
      .Case("cmpccxadd", HasCMPCCXADD)
      .Case("crc32", HasCRC32)
      .Case("cx8", HasCX8)
      .Case("cx16", HasCX16)
      .Case("enqcmd", HasENQCMD)
      .Case("evex512", HasEVEX512)
      .Case("f16c", HasF16C)
      .Case("fma", HasFMA)
      .Case("fma4", XOPLevel >= FMA4)
      .Case("fsgsbase", HasFSGSBASE)
      .Case("fxsr", HasFXSR)
      .Case("gfni", HasGFNI)
      .Case("hreset", HasHRESET)
      .Case("invpcid", HasINVPCID)
      .Case("kl", HasKL)
      .Case("widekl", HasWIDEKL)
      .Case("lwp", HasLWP)
      .Case("lzcnt", HasLZCNT)
      .Case("mmx", HasMMX)
      .Case("movbe", HasMOVBE)
      .Case("movdiri", HasMOVDIRI)
      .Case("movdir64b", HasMOVDIR64B)
      .Case("save-args", HasSaveArgs)
      .Case("mwaitx", HasMWAITX)
      .Case("pclmul", HasPCLMUL)
      .Case("pconfig", HasPCONFIG)
      .Case("pku", HasPKU)
      .Case("popcnt", HasPOPCNT)
      .Case("prefetchi", HasPREFETCHI)
      .Case("prfchw", HasPRFCHW)
      .Case("ptwrite", HasPTWRITE)
      .Case("raoint", HasRAOINT)
      .Case("rdpid", HasRDPID)
      .Case("rdpru", HasRDPRU)
      .Case("rdrnd", HasRDRND)
      .Case("rdseed", HasRDSEED)
      .Case("retpoline-external-thunk", HasRetpolineExternalThunk)
      .Case("rtm", HasRTM)
      .Case("sahf", HasLAHFSAHF)
      .Case("serialize", HasSERIALIZE)
      .Case("sgx", HasSGX)
      .Case("sha", HasSHA)
      .Case("sha512", HasSHA512)
      .Case("shstk", HasSHSTK)
      .Case("sm3", HasSM3)
      .Case("sm4", HasSM4)
      .Case("sse", SSELevel >= SSE1)
      .Case("sse2", SSELevel >= SSE2)
      .Case("sse3", SSELevel >= SSE3)
      .Case("ssse3", SSELevel >= SSSE3)
      .Case("sse4.1", SSELevel >= SSE41)
      .Case("sse4.2", SSELevel >= SSE42)
      .Case("sse4a", XOPLevel >= SSE4A)
      .Case("tbm", HasTBM)
      .Case("tsxldtrk", HasTSXLDTRK)
      .Case("uintr", HasUINTR)
      .Case("usermsr", HasUSERMSR)
      .Case("vaes", HasVAES)
      .Case("vpclmulqdq", HasVPCLMULQDQ)
      .Case("wbnoinvd", HasWBNOINVD)
      .Case("waitpkg", HasWAITPKG)
      .Case("x86", true)
      .Case("x86_32", getTriple().getArch() == llvm::Triple::x86)
      .Case("x86_64", getTriple().getArch() == llvm::Triple::x86_64)
      .Case("x87", HasX87)
      .Case("xop", XOPLevel >= XOP)
      .Case("xsave", HasXSAVE)
      .Case("xsavec", HasXSAVEC)
      .Case("xsaves", HasXSAVES)
      .Case("xsaveopt", HasXSAVEOPT)
      .Case("fullbf16", HasFullBFloat16)
      .Case("egpr", HasEGPR)
      .Case("push2pop2", HasPush2Pop2)
      .Case("ppx", HasPPX)
      .Case("ndd", HasNDD)
      .Case("ccmp", HasCCMP)
      .Case("nf", HasNF)
      .Case("cf", HasCF)
      .Case("zu", HasZU)
      .Case("branch-hint", HasBranchHint)
      .Default(false);
}

// We can't use a generic validation scheme for the features accepted here
// versus subtarget features accepted in the target attribute because the
// bitfield structure that's initialized in the runtime only supports the
// below currently rather than the full range of subtarget features. (See
// X86TargetInfo::hasFeature for a somewhat comprehensive list).
bool X86TargetInfo::validateCpuSupports(StringRef FeatureStr) const {
  return llvm::StringSwitch<bool>(FeatureStr)
#define X86_FEATURE_COMPAT(ENUM, STR, PRIORITY) .Case(STR, true)
#define X86_MICROARCH_LEVEL(ENUM, STR, PRIORITY) .Case(STR, true)
#include "llvm/TargetParser/X86TargetParser.def"
      .Default(false);
}

static llvm::X86::ProcessorFeatures getFeature(StringRef Name) {
  return llvm::StringSwitch<llvm::X86::ProcessorFeatures>(Name)
#define X86_FEATURE_COMPAT(ENUM, STR, PRIORITY)                                \
  .Case(STR, llvm::X86::FEATURE_##ENUM)

#include "llvm/TargetParser/X86TargetParser.def"
      ;
  // Note, this function should only be used after ensuring the value is
  // correct, so it asserts if the value is out of range.
}

unsigned X86TargetInfo::multiVersionSortPriority(StringRef Name) const {
  // Valid CPUs have a 'key feature' that compares just better than its key
  // feature.
  using namespace llvm::X86;
  CPUKind Kind = parseArchX86(Name);
  if (Kind != CK_None) {
    ProcessorFeatures KeyFeature = getKeyFeature(Kind);
    return (getFeaturePriority(KeyFeature) << 1) + 1;
  }

  // Now we know we have a feature, so get its priority and shift it a few so
  // that we have sufficient room for the CPUs (above).
  return getFeaturePriority(getFeature(Name)) << 1;
}

bool X86TargetInfo::validateCPUSpecificCPUDispatch(StringRef Name) const {
  return llvm::X86::validateCPUSpecificCPUDispatch(Name);
}

char X86TargetInfo::CPUSpecificManglingCharacter(StringRef Name) const {
  return llvm::X86::getCPUDispatchMangling(Name);
}

void X86TargetInfo::getCPUSpecificCPUDispatchFeatures(
    StringRef Name, llvm::SmallVectorImpl<StringRef> &Features) const {
  SmallVector<StringRef, 32> TargetCPUFeatures;
  llvm::X86::getFeaturesForCPU(Name, TargetCPUFeatures, true);
  for (auto &F : TargetCPUFeatures)
    Features.push_back(F);
}

// We can't use a generic validation scheme for the cpus accepted here
// versus subtarget cpus accepted in the target attribute because the
// variables intitialized by the runtime only support the below currently
// rather than the full range of cpus.
bool X86TargetInfo::validateCpuIs(StringRef FeatureStr) const {
  return llvm::StringSwitch<bool>(FeatureStr)
#define X86_VENDOR(ENUM, STRING) .Case(STRING, true)
#define X86_CPU_TYPE_ALIAS(ENUM, ALIAS) .Case(ALIAS, true)
#define X86_CPU_TYPE(ENUM, STR) .Case(STR, true)
#define X86_CPU_SUBTYPE_ALIAS(ENUM, ALIAS) .Case(ALIAS, true)
#define X86_CPU_SUBTYPE(ENUM, STR) .Case(STR, true)
#include "llvm/TargetParser/X86TargetParser.def"
      .Default(false);
}

static unsigned matchAsmCCConstraint(const char *Name) {
  auto RV = llvm::StringSwitch<unsigned>(Name)
                .Case("@cca", 4)
                .Case("@ccae", 5)
                .Case("@ccb", 4)
                .Case("@ccbe", 5)
                .Case("@ccc", 4)
                .Case("@cce", 4)
                .Case("@ccz", 4)
                .Case("@ccg", 4)
                .Case("@ccge", 5)
                .Case("@ccl", 4)
                .Case("@ccle", 5)
                .Case("@ccna", 5)
                .Case("@ccnae", 6)
                .Case("@ccnb", 5)
                .Case("@ccnbe", 6)
                .Case("@ccnc", 5)
                .Case("@ccne", 5)
                .Case("@ccnz", 5)
                .Case("@ccng", 5)
                .Case("@ccnge", 6)
                .Case("@ccnl", 5)
                .Case("@ccnle", 6)
                .Case("@ccno", 5)
                .Case("@ccnp", 5)
                .Case("@ccns", 5)
                .Case("@cco", 4)
                .Case("@ccp", 4)
                .Case("@ccs", 4)
                .Default(0);
  return RV;
}

bool X86TargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  default:
    return false;
  // Constant constraints.
  case 'e': // 32-bit signed integer constant for use with sign-extending x86_64
            // instructions.
  case 'Z': // 32-bit unsigned integer constant for use with zero-extending
            // x86_64 instructions.
  case 's':
    Info.setRequiresImmediate();
    return true;
  case 'I':
    Info.setRequiresImmediate(0, 31);
    return true;
  case 'J':
    Info.setRequiresImmediate(0, 63);
    return true;
  case 'K':
    Info.setRequiresImmediate(-128, 127);
    return true;
  case 'L':
    Info.setRequiresImmediate({int(0xff), int(0xffff), int(0xffffffff)});
    return true;
  case 'M':
    Info.setRequiresImmediate(0, 3);
    return true;
  case 'N':
    Info.setRequiresImmediate(0, 255);
    return true;
  case 'O':
    Info.setRequiresImmediate(0, 127);
    return true;
  case 'W':
    switch (*++Name) {
    default:
      return false;
    case 's':
      Info.setAllowsRegister();
      return true;
    }
  // Register constraints.
  case 'Y': // 'Y' is the first character for several 2-character constraints.
    // Shift the pointer to the second character of the constraint.
    Name++;
    switch (*Name) {
    default:
      return false;
    case 'z': // First SSE register.
    case '2':
    case 't': // Any SSE register, when SSE2 is enabled.
    case 'i': // Any SSE register, when SSE2 and inter-unit moves enabled.
    case 'm': // Any MMX register, when inter-unit moves enabled.
    case 'k': // AVX512 arch mask registers: k1-k7.
      Info.setAllowsRegister();
      return true;
    }
  case 'f': // Any x87 floating point stack register.
    // Constraint 'f' cannot be used for output operands.
    if (Info.ConstraintStr[0] == '=')
      return false;
    Info.setAllowsRegister();
    return true;
  case 'a': // eax.
  case 'b': // ebx.
  case 'c': // ecx.
  case 'd': // edx.
  case 'S': // esi.
  case 'D': // edi.
  case 'A': // edx:eax.
  case 't': // Top of floating point stack.
  case 'u': // Second from top of floating point stack.
  case 'q': // Any register accessible as [r]l: a, b, c, and d.
  case 'y': // Any MMX register.
  case 'v': // Any {X,Y,Z}MM register (Arch & context dependent)
  case 'x': // Any SSE register.
  case 'k': // Any AVX512 mask register (same as Yk, additionally allows k0
            // for intermideate k reg operations).
  case 'Q': // Any register accessible as [r]h: a, b, c, and d.
  case 'R': // "Legacy" registers: ax, bx, cx, dx, di, si, sp, bp.
  case 'l': // "Index" registers: any general register that can be used as an
            // index in a base+index memory access.
    Info.setAllowsRegister();
    return true;
  // Floating point constant constraints.
  case 'C': // SSE floating point constant.
  case 'G': // x87 floating point constant.
    return true;
  case 'j':
    Name++;
    switch (*Name) {
    default:
      return false;
    case 'r':
      Info.setAllowsRegister();
      return true;
    case 'R':
      Info.setAllowsRegister();
      return true;
    }
  case '@':
    // CC condition changes.
    if (auto Len = matchAsmCCConstraint(Name)) {
      Name += Len - 1;
      Info.setAllowsRegister();
      return true;
    }
    return false;
  }
}

// Below is based on the following information:
// +------------------------------------+-------------------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------+
// |           Processor Name           | Cache Line Size (Bytes) |                                                                            Source                                                                            |
// +------------------------------------+-------------------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------+
// | i386                               |                      64 | https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-optimization-manual.pdf                                          |
// | i486                               |                      16 | "four doublewords" (doubleword = 32 bits, 4 bits * 32 bits = 16 bytes) https://en.wikichip.org/w/images/d/d3/i486_MICROPROCESSOR_HARDWARE_REFERENCE_MANUAL_%281990%29.pdf and http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.126.4216&rep=rep1&type=pdf (page 29) |
// | i586/Pentium MMX                   |                      32 | https://www.7-cpu.com/cpu/P-MMX.html                                                                                                                         |
// | i686/Pentium                       |                      32 | https://www.7-cpu.com/cpu/P6.html                                                                                                                            |
// | Netburst/Pentium4                  |                      64 | https://www.7-cpu.com/cpu/P4-180.html                                                                                                                        |
// | Atom                               |                      64 | https://www.7-cpu.com/cpu/Atom.html                                                                                                                          |
// | Westmere                           |                      64 | https://en.wikichip.org/wiki/intel/microarchitectures/sandy_bridge_(client) "Cache Architecture"                                                             |
// | Sandy Bridge                       |                      64 | https://en.wikipedia.org/wiki/Sandy_Bridge and https://www.7-cpu.com/cpu/SandyBridge.html                                                                    |
// | Ivy Bridge                         |                      64 | https://blog.stuffedcow.net/2013/01/ivb-cache-replacement/ and https://www.7-cpu.com/cpu/IvyBridge.html                                                      |
// | Haswell                            |                      64 | https://www.7-cpu.com/cpu/Haswell.html                                                                                                                       |
// | Broadwell                          |                      64 | https://www.7-cpu.com/cpu/Broadwell.html                                                                                                                     |
// | Skylake (including skylake-avx512) |                      64 | https://www.nas.nasa.gov/hecc/support/kb/skylake-processors_550.html "Cache Hierarchy"                                                                       |
// | Cascade Lake                       |                      64 | https://www.nas.nasa.gov/hecc/support/kb/cascade-lake-processors_579.html "Cache Hierarchy"                                                                  |
// | Skylake                            |                      64 | https://en.wikichip.org/wiki/intel/microarchitectures/kaby_lake "Memory Hierarchy"                                                                           |
// | Ice Lake                           |                      64 | https://www.7-cpu.com/cpu/Ice_Lake.html                                                                                                                      |
// | Knights Landing                    |                      64 | https://software.intel.com/en-us/articles/intel-xeon-phi-processor-7200-family-memory-management-optimizations "The Intel® Xeon Phi™ Processor Architecture" |
// | Knights Mill                       |                      64 | https://software.intel.com/sites/default/files/managed/9e/bc/64-ia-32-architectures-optimization-manual.pdf?countrylabel=Colombia "2.5.5.2 L1 DCache "       |
// +------------------------------------+-------------------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------+
std::optional<unsigned> X86TargetInfo::getCPUCacheLineSize() const {
  using namespace llvm::X86;
  switch (CPU) {
    // i386
    case CK_i386:
    // i486
    case CK_i486:
    case CK_WinChipC6:
    case CK_WinChip2:
    case CK_C3:
    // Lakemont
    case CK_Lakemont:
      return 16;

    // i586
    case CK_i586:
    case CK_Pentium:
    case CK_PentiumMMX:
    // i686
    case CK_PentiumPro:
    case CK_i686:
    case CK_Pentium2:
    case CK_Pentium3:
    case CK_PentiumM:
    case CK_C3_2:
    // K6
    case CK_K6:
    case CK_K6_2:
    case CK_K6_3:
    // Geode
    case CK_Geode:
      return 32;

    // Netburst
    case CK_Pentium4:
    case CK_Prescott:
    case CK_Nocona:
    // Atom
    case CK_Bonnell:
    case CK_Silvermont:
    case CK_Goldmont:
    case CK_GoldmontPlus:
    case CK_Tremont:
    case CK_Gracemont:

    case CK_Westmere:
    case CK_SandyBridge:
    case CK_IvyBridge:
    case CK_Haswell:
    case CK_Broadwell:
    case CK_SkylakeClient:
    case CK_SkylakeServer:
    case CK_Cascadelake:
    case CK_Nehalem:
    case CK_Cooperlake:
    case CK_Cannonlake:
    case CK_Tigerlake:
    case CK_SapphireRapids:
    case CK_IcelakeClient:
    case CK_Rocketlake:
    case CK_IcelakeServer:
    case CK_Alderlake:
    case CK_Raptorlake:
    case CK_Meteorlake:
    case CK_Arrowlake:
    case CK_ArrowlakeS:
    case CK_Lunarlake:
    case CK_Pantherlake:
    case CK_Sierraforest:
    case CK_Grandridge:
    case CK_Graniterapids:
    case CK_GraniterapidsD:
    case CK_Emeraldrapids:
    case CK_Clearwaterforest:
    case CK_KNL:
    case CK_KNM:
    // K7
    case CK_Athlon:
    case CK_AthlonXP:
    // K8
    case CK_K8:
    case CK_K8SSE3:
    case CK_AMDFAM10:
    // Bobcat
    case CK_BTVER1:
    case CK_BTVER2:
    // Bulldozer
    case CK_BDVER1:
    case CK_BDVER2:
    case CK_BDVER3:
    case CK_BDVER4:
    // Zen
    case CK_ZNVER1:
    case CK_ZNVER2:
    case CK_ZNVER3:
    case CK_ZNVER4:
    case CK_ZNVER5:
    // Deprecated
    case CK_x86_64:
    case CK_x86_64_v2:
    case CK_x86_64_v3:
    case CK_x86_64_v4:
    case CK_Yonah:
    case CK_Penryn:
    case CK_Core2:
      return 64;

    // The following currently have unknown cache line sizes (but they are probably all 64):
    // Core
    case CK_None:
      return std::nullopt;
  }
  llvm_unreachable("Unknown CPU kind");
}

bool X86TargetInfo::validateOutputSize(const llvm::StringMap<bool> &FeatureMap,
                                       StringRef Constraint,
                                       unsigned Size) const {
  // Strip off constraint modifiers.
  Constraint = Constraint.ltrim("=+&");

  return validateOperandSize(FeatureMap, Constraint, Size);
}

bool X86TargetInfo::validateInputSize(const llvm::StringMap<bool> &FeatureMap,
                                      StringRef Constraint,
                                      unsigned Size) const {
  return validateOperandSize(FeatureMap, Constraint, Size);
}

bool X86TargetInfo::validateOperandSize(const llvm::StringMap<bool> &FeatureMap,
                                        StringRef Constraint,
                                        unsigned Size) const {
  switch (Constraint[0]) {
  default:
    break;
  case 'k':
  // Registers k0-k7 (AVX512) size limit is 64 bit.
  case 'y':
    return Size <= 64;
  case 'f':
  case 't':
  case 'u':
    return Size <= 128;
  case 'Y':
    // 'Y' is the first character for several 2-character constraints.
    switch (Constraint[1]) {
    default:
      return false;
    case 'm':
      // 'Ym' is synonymous with 'y'.
    case 'k':
      return Size <= 64;
    case 'z':
      // XMM0/YMM/ZMM0
      if (hasFeatureEnabled(FeatureMap, "avx512f") &&
          hasFeatureEnabled(FeatureMap, "evex512"))
        // ZMM0 can be used if target supports AVX512F and EVEX512 is set.
        return Size <= 512U;
      else if (hasFeatureEnabled(FeatureMap, "avx"))
        // YMM0 can be used if target supports AVX.
        return Size <= 256U;
      else if (hasFeatureEnabled(FeatureMap, "sse"))
        return Size <= 128U;
      return false;
    case 'i':
    case 't':
    case '2':
      // 'Yi','Yt','Y2' are synonymous with 'x' when SSE2 is enabled.
      if (SSELevel < SSE2)
        return false;
      break;
    }
    break;
  case 'v':
  case 'x':
    if (hasFeatureEnabled(FeatureMap, "avx512f") &&
        hasFeatureEnabled(FeatureMap, "evex512"))
      // 512-bit zmm registers can be used if target supports AVX512F and
      // EVEX512 is set.
      return Size <= 512U;
    else if (hasFeatureEnabled(FeatureMap, "avx"))
      // 256-bit ymm registers can be used if target supports AVX.
      return Size <= 256U;
    return Size <= 128U;

  }

  return true;
}

std::string X86TargetInfo::convertConstraint(const char *&Constraint) const {
  switch (*Constraint) {
  case '@':
    if (auto Len = matchAsmCCConstraint(Constraint)) {
      std::string Converted = "{" + std::string(Constraint, Len) + "}";
      Constraint += Len - 1;
      return Converted;
    }
    return std::string(1, *Constraint);
  case 'a':
    return std::string("{ax}");
  case 'b':
    return std::string("{bx}");
  case 'c':
    return std::string("{cx}");
  case 'd':
    return std::string("{dx}");
  case 'S':
    return std::string("{si}");
  case 'D':
    return std::string("{di}");
  case 'p': // Keep 'p' constraint (address).
    return std::string("p");
  case 't': // top of floating point stack.
    return std::string("{st}");
  case 'u':                        // second from top of floating point stack.
    return std::string("{st(1)}"); // second from top of floating point stack.
  case 'W':
    assert(Constraint[1] == 's');
    return '^' + std::string(Constraint++, 2);
  case 'Y':
    switch (Constraint[1]) {
    default:
      // Break from inner switch and fall through (copy single char),
      // continue parsing after copying the current constraint into
      // the return string.
      break;
    case 'k':
    case 'm':
    case 'i':
    case 't':
    case 'z':
    case '2':
      // "^" hints llvm that this is a 2 letter constraint.
      // "Constraint++" is used to promote the string iterator
      // to the next constraint.
      return std::string("^") + std::string(Constraint++, 2);
    }
    [[fallthrough]];
  case 'j':
    switch (Constraint[1]) {
    default:
      // Break from inner switch and fall through (copy single char),
      // continue parsing after copying the current constraint into
      // the return string.
      break;
    case 'r':
    case 'R':
      // "^" hints llvm that this is a 2 letter constraint.
      // "Constraint++" is used to promote the string iterator
      // to the next constraint.
      return std::string("^") + std::string(Constraint++, 2);
    }
    [[fallthrough]];
  default:
    return std::string(1, *Constraint);
  }
}

void X86TargetInfo::fillValidCPUList(SmallVectorImpl<StringRef> &Values) const {
  bool Only64Bit = getTriple().getArch() != llvm::Triple::x86;
  llvm::X86::fillValidCPUArchList(Values, Only64Bit);
}

void X86TargetInfo::fillValidTuneCPUList(SmallVectorImpl<StringRef> &Values) const {
  llvm::X86::fillValidTuneCPUList(Values);
}

ArrayRef<const char *> X86TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::AddlRegName> X86TargetInfo::getGCCAddlRegNames() const {
  return llvm::ArrayRef(AddlRegNames);
}

ArrayRef<Builtin::Info> X86_32TargetInfo::getTargetBuiltins() const {
  return llvm::ArrayRef(BuiltinInfoX86, clang::X86::LastX86CommonBuiltin -
                                            Builtin::FirstTSBuiltin + 1);
}

ArrayRef<Builtin::Info> X86_64TargetInfo::getTargetBuiltins() const {
  return llvm::ArrayRef(BuiltinInfoX86,
                        X86::LastTSBuiltin - Builtin::FirstTSBuiltin);
}
