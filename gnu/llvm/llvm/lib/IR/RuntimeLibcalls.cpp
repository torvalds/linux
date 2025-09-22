//===- RuntimeLibcalls.cpp - Interface for runtime libcalls -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/RuntimeLibcalls.h"

using namespace llvm;
using namespace RTLIB;

/// Set default libcall names. If a target wants to opt-out of a libcall it
/// should be placed here.
void RuntimeLibcallsInfo::initLibcalls(const Triple &TT) {
  std::fill(std::begin(LibcallRoutineNames), std::end(LibcallRoutineNames),
            nullptr);

#define HANDLE_LIBCALL(code, name) setLibcallName(RTLIB::code, name);
#include "llvm/IR/RuntimeLibcalls.def"
#undef HANDLE_LIBCALL

  // Initialize calling conventions to their default.
  for (int LC = 0; LC < RTLIB::UNKNOWN_LIBCALL; ++LC)
    setLibcallCallingConv((RTLIB::Libcall)LC, CallingConv::C);

  // Use the f128 variants of math functions on x86_64
  if (TT.getArch() == Triple::ArchType::x86_64 && TT.isGNUEnvironment()) {
    setLibcallName(RTLIB::REM_F128, "fmodf128");
    setLibcallName(RTLIB::FMA_F128, "fmaf128");
    setLibcallName(RTLIB::SQRT_F128, "sqrtf128");
    setLibcallName(RTLIB::CBRT_F128, "cbrtf128");
    setLibcallName(RTLIB::LOG_F128, "logf128");
    setLibcallName(RTLIB::LOG_FINITE_F128, "__logf128_finite");
    setLibcallName(RTLIB::LOG2_F128, "log2f128");
    setLibcallName(RTLIB::LOG2_FINITE_F128, "__log2f128_finite");
    setLibcallName(RTLIB::LOG10_F128, "log10f128");
    setLibcallName(RTLIB::LOG10_FINITE_F128, "__log10f128_finite");
    setLibcallName(RTLIB::EXP_F128, "expf128");
    setLibcallName(RTLIB::EXP_FINITE_F128, "__expf128_finite");
    setLibcallName(RTLIB::EXP2_F128, "exp2f128");
    setLibcallName(RTLIB::EXP2_FINITE_F128, "__exp2f128_finite");
    setLibcallName(RTLIB::EXP10_F128, "exp10f128");
    setLibcallName(RTLIB::SIN_F128, "sinf128");
    setLibcallName(RTLIB::COS_F128, "cosf128");
    setLibcallName(RTLIB::TAN_F128, "tanf128");
    setLibcallName(RTLIB::SINCOS_F128, "sincosf128");
    setLibcallName(RTLIB::ASIN_F128, "asinf128");
    setLibcallName(RTLIB::ACOS_F128, "acosf128");
    setLibcallName(RTLIB::ATAN_F128, "atanf128");
    setLibcallName(RTLIB::SINH_F128, "sinhf128");
    setLibcallName(RTLIB::COSH_F128, "coshf128");
    setLibcallName(RTLIB::TANH_F128, "tanhf128");
    setLibcallName(RTLIB::POW_F128, "powf128");
    setLibcallName(RTLIB::POW_FINITE_F128, "__powf128_finite");
    setLibcallName(RTLIB::CEIL_F128, "ceilf128");
    setLibcallName(RTLIB::TRUNC_F128, "truncf128");
    setLibcallName(RTLIB::RINT_F128, "rintf128");
    setLibcallName(RTLIB::NEARBYINT_F128, "nearbyintf128");
    setLibcallName(RTLIB::ROUND_F128, "roundf128");
    setLibcallName(RTLIB::ROUNDEVEN_F128, "roundevenf128");
    setLibcallName(RTLIB::FLOOR_F128, "floorf128");
    setLibcallName(RTLIB::COPYSIGN_F128, "copysignf128");
    setLibcallName(RTLIB::FMIN_F128, "fminf128");
    setLibcallName(RTLIB::FMAX_F128, "fmaxf128");
    setLibcallName(RTLIB::LROUND_F128, "lroundf128");
    setLibcallName(RTLIB::LLROUND_F128, "llroundf128");
    setLibcallName(RTLIB::LRINT_F128, "lrintf128");
    setLibcallName(RTLIB::LLRINT_F128, "llrintf128");
    setLibcallName(RTLIB::LDEXP_F128, "ldexpf128");
    setLibcallName(RTLIB::FREXP_F128, "frexpf128");
  }

  // For IEEE quad-precision libcall names, PPC uses "kf" instead of "tf".
  if (TT.isPPC()) {
    setLibcallName(RTLIB::ADD_F128, "__addkf3");
    setLibcallName(RTLIB::SUB_F128, "__subkf3");
    setLibcallName(RTLIB::MUL_F128, "__mulkf3");
    setLibcallName(RTLIB::DIV_F128, "__divkf3");
    setLibcallName(RTLIB::POWI_F128, "__powikf2");
    setLibcallName(RTLIB::FPEXT_F32_F128, "__extendsfkf2");
    setLibcallName(RTLIB::FPEXT_F64_F128, "__extenddfkf2");
    setLibcallName(RTLIB::FPROUND_F128_F32, "__trunckfsf2");
    setLibcallName(RTLIB::FPROUND_F128_F64, "__trunckfdf2");
    setLibcallName(RTLIB::FPTOSINT_F128_I32, "__fixkfsi");
    setLibcallName(RTLIB::FPTOSINT_F128_I64, "__fixkfdi");
    setLibcallName(RTLIB::FPTOSINT_F128_I128, "__fixkfti");
    setLibcallName(RTLIB::FPTOUINT_F128_I32, "__fixunskfsi");
    setLibcallName(RTLIB::FPTOUINT_F128_I64, "__fixunskfdi");
    setLibcallName(RTLIB::FPTOUINT_F128_I128, "__fixunskfti");
    setLibcallName(RTLIB::SINTTOFP_I32_F128, "__floatsikf");
    setLibcallName(RTLIB::SINTTOFP_I64_F128, "__floatdikf");
    setLibcallName(RTLIB::SINTTOFP_I128_F128, "__floattikf");
    setLibcallName(RTLIB::UINTTOFP_I32_F128, "__floatunsikf");
    setLibcallName(RTLIB::UINTTOFP_I64_F128, "__floatundikf");
    setLibcallName(RTLIB::UINTTOFP_I128_F128, "__floatuntikf");
    setLibcallName(RTLIB::OEQ_F128, "__eqkf2");
    setLibcallName(RTLIB::UNE_F128, "__nekf2");
    setLibcallName(RTLIB::OGE_F128, "__gekf2");
    setLibcallName(RTLIB::OLT_F128, "__ltkf2");
    setLibcallName(RTLIB::OLE_F128, "__lekf2");
    setLibcallName(RTLIB::OGT_F128, "__gtkf2");
    setLibcallName(RTLIB::UO_F128, "__unordkf2");
  }

  // A few names are different on particular architectures or environments.
  if (TT.isOSDarwin()) {
    // For f16/f32 conversions, Darwin uses the standard naming scheme,
    // instead of the gnueabi-style __gnu_*_ieee.
    // FIXME: What about other targets?
    setLibcallName(RTLIB::FPEXT_F16_F32, "__extendhfsf2");
    setLibcallName(RTLIB::FPROUND_F32_F16, "__truncsfhf2");

    // Some darwins have an optimized __bzero/bzero function.
    switch (TT.getArch()) {
    case Triple::x86:
    case Triple::x86_64:
      if (TT.isMacOSX() && !TT.isMacOSXVersionLT(10, 6))
        setLibcallName(RTLIB::BZERO, "__bzero");
      break;
    case Triple::aarch64:
    case Triple::aarch64_32:
      setLibcallName(RTLIB::BZERO, "bzero");
      break;
    default:
      break;
    }

    if (darwinHasSinCos(TT)) {
      setLibcallName(RTLIB::SINCOS_STRET_F32, "__sincosf_stret");
      setLibcallName(RTLIB::SINCOS_STRET_F64, "__sincos_stret");
      if (TT.isWatchABI()) {
        setLibcallCallingConv(RTLIB::SINCOS_STRET_F32,
                              CallingConv::ARM_AAPCS_VFP);
        setLibcallCallingConv(RTLIB::SINCOS_STRET_F64,
                              CallingConv::ARM_AAPCS_VFP);
      }
    }

    switch (TT.getOS()) {
    case Triple::MacOSX:
      if (TT.isMacOSXVersionLT(10, 9)) {
        setLibcallName(RTLIB::EXP10_F32, nullptr);
        setLibcallName(RTLIB::EXP10_F64, nullptr);
      } else {
        setLibcallName(RTLIB::EXP10_F32, "__exp10f");
        setLibcallName(RTLIB::EXP10_F64, "__exp10");
      }
      break;
    case Triple::IOS:
      if (TT.isOSVersionLT(7, 0)) {
        setLibcallName(RTLIB::EXP10_F32, nullptr);
        setLibcallName(RTLIB::EXP10_F64, nullptr);
        break;
      }
      [[fallthrough]];
    case Triple::TvOS:
    case Triple::WatchOS:
    case Triple::XROS:
      setLibcallName(RTLIB::EXP10_F32, "__exp10f");
      setLibcallName(RTLIB::EXP10_F64, "__exp10");
      break;
    default:
      break;
    }
  } else {
    setLibcallName(RTLIB::FPEXT_F16_F32, "__gnu_h2f_ieee");
    setLibcallName(RTLIB::FPROUND_F32_F16, "__gnu_f2h_ieee");
  }

  if (TT.isGNUEnvironment() || TT.isOSFuchsia() ||
      (TT.isAndroid() && !TT.isAndroidVersionLT(9))) {
    setLibcallName(RTLIB::SINCOS_F32, "sincosf");
    setLibcallName(RTLIB::SINCOS_F64, "sincos");
    setLibcallName(RTLIB::SINCOS_F80, "sincosl");
    setLibcallName(RTLIB::SINCOS_F128, "sincosl");
    setLibcallName(RTLIB::SINCOS_PPCF128, "sincosl");
  }

  if (TT.isPS()) {
    setLibcallName(RTLIB::SINCOS_F32, "sincosf");
    setLibcallName(RTLIB::SINCOS_F64, "sincos");
  }

  if (TT.isOSOpenBSD()) {
    setLibcallName(RTLIB::STACKPROTECTOR_CHECK_FAIL, nullptr);
  }

  if (TT.isOSWindows() && !TT.isOSCygMing()) {
    setLibcallName(RTLIB::LDEXP_F32, nullptr);
    setLibcallName(RTLIB::LDEXP_F80, nullptr);
    setLibcallName(RTLIB::LDEXP_F128, nullptr);
    setLibcallName(RTLIB::LDEXP_PPCF128, nullptr);

    setLibcallName(RTLIB::FREXP_F32, nullptr);
    setLibcallName(RTLIB::FREXP_F80, nullptr);
    setLibcallName(RTLIB::FREXP_F128, nullptr);
    setLibcallName(RTLIB::FREXP_PPCF128, nullptr);
  }

  if (TT.isAArch64()) {
    if (TT.isOSMSVCRT()) {
      // MSVCRT doesn't have powi; fall back to pow
      setLibcallName(RTLIB::POWI_F32, nullptr);
      setLibcallName(RTLIB::POWI_F64, nullptr);
    }
  }

  // Disable most libcalls on AMDGPU.
  if (TT.isAMDGPU()) {
    for (int I = 0; I < RTLIB::UNKNOWN_LIBCALL; ++I) {
      if (I < RTLIB::ATOMIC_LOAD || I > RTLIB::ATOMIC_FETCH_NAND_16)
        setLibcallName(static_cast<RTLIB::Libcall>(I), nullptr);
    }
  }

  // Disable most libcalls on NVPTX.
  if (TT.isNVPTX()) {
    for (int I = 0; I < RTLIB::UNKNOWN_LIBCALL; ++I)
      if (I < RTLIB::ATOMIC_LOAD || I > RTLIB::ATOMIC_FETCH_NAND_16)
        setLibcallName(static_cast<RTLIB::Libcall>(I), nullptr);
  }

  if (TT.isARM() || TT.isThumb()) {
    // These libcalls are not available in 32-bit.
    setLibcallName(RTLIB::SHL_I128, nullptr);
    setLibcallName(RTLIB::SRL_I128, nullptr);
    setLibcallName(RTLIB::SRA_I128, nullptr);
    setLibcallName(RTLIB::MUL_I128, nullptr);
    setLibcallName(RTLIB::MULO_I64, nullptr);
    setLibcallName(RTLIB::MULO_I128, nullptr);

    if (TT.isOSMSVCRT()) {
      // MSVCRT doesn't have powi; fall back to pow
      setLibcallName(RTLIB::POWI_F32, nullptr);
      setLibcallName(RTLIB::POWI_F64, nullptr);
    }
  }

  if (TT.getArch() == Triple::ArchType::avr) {
    // Division rtlib functions (not supported), use divmod functions instead
    setLibcallName(RTLIB::SDIV_I8, nullptr);
    setLibcallName(RTLIB::SDIV_I16, nullptr);
    setLibcallName(RTLIB::SDIV_I32, nullptr);
    setLibcallName(RTLIB::UDIV_I8, nullptr);
    setLibcallName(RTLIB::UDIV_I16, nullptr);
    setLibcallName(RTLIB::UDIV_I32, nullptr);

    // Modulus rtlib functions (not supported), use divmod functions instead
    setLibcallName(RTLIB::SREM_I8, nullptr);
    setLibcallName(RTLIB::SREM_I16, nullptr);
    setLibcallName(RTLIB::SREM_I32, nullptr);
    setLibcallName(RTLIB::UREM_I8, nullptr);
    setLibcallName(RTLIB::UREM_I16, nullptr);
    setLibcallName(RTLIB::UREM_I32, nullptr);
  }

  if (TT.getArch() == Triple::ArchType::hexagon) {
    // These cause problems when the shift amount is non-constant.
    setLibcallName(RTLIB::SHL_I128, nullptr);
    setLibcallName(RTLIB::SRL_I128, nullptr);
    setLibcallName(RTLIB::SRA_I128, nullptr);
  }

  if (TT.isLoongArch()) {
    if (!TT.isLoongArch64()) {
      // Set libcalls.
      setLibcallName(RTLIB::MUL_I128, nullptr);
      // The MULO libcall is not part of libgcc, only compiler-rt.
      setLibcallName(RTLIB::MULO_I64, nullptr);
    }
    // The MULO libcall is not part of libgcc, only compiler-rt.
    setLibcallName(RTLIB::MULO_I128, nullptr);
  }

  if (TT.isMIPS32()) {
    // These libcalls are not available in 32-bit.
    setLibcallName(RTLIB::SHL_I128, nullptr);
    setLibcallName(RTLIB::SRL_I128, nullptr);
    setLibcallName(RTLIB::SRA_I128, nullptr);
    setLibcallName(RTLIB::MUL_I128, nullptr);
    setLibcallName(RTLIB::MULO_I64, nullptr);
    setLibcallName(RTLIB::MULO_I128, nullptr);
  }

  if (TT.isPPC()) {
    if (!TT.isPPC64()) {
      // These libcalls are not available in 32-bit.
      setLibcallName(RTLIB::SHL_I128, nullptr);
      setLibcallName(RTLIB::SRL_I128, nullptr);
      setLibcallName(RTLIB::SRA_I128, nullptr);
      setLibcallName(RTLIB::MUL_I128, nullptr);
      setLibcallName(RTLIB::MULO_I64, nullptr);
    }
    setLibcallName(RTLIB::MULO_I128, nullptr);
  }

  if (TT.isRISCV32()) {
    // These libcalls are not available in 32-bit.
    setLibcallName(RTLIB::SHL_I128, nullptr);
    setLibcallName(RTLIB::SRL_I128, nullptr);
    setLibcallName(RTLIB::SRA_I128, nullptr);
    setLibcallName(RTLIB::MUL_I128, nullptr);
    setLibcallName(RTLIB::MULO_I64, nullptr);
  }

  if (TT.isSPARC()) {
    if (!TT.isSPARC64()) {
      // These libcalls are not available in 32-bit.
      setLibcallName(RTLIB::MULO_I64, nullptr);
      setLibcallName(RTLIB::MUL_I128, nullptr);
      setLibcallName(RTLIB::SHL_I128, nullptr);
      setLibcallName(RTLIB::SRL_I128, nullptr);
      setLibcallName(RTLIB::SRA_I128, nullptr);
    }
    setLibcallName(RTLIB::MULO_I128, nullptr);
  }

  if (TT.isSystemZ()) {
    setLibcallName(RTLIB::SRL_I128, nullptr);
    setLibcallName(RTLIB::SHL_I128, nullptr);
    setLibcallName(RTLIB::SRA_I128, nullptr);
  }

  if (TT.isX86()) {
    if (TT.getArch() == Triple::ArchType::x86) {
      // These libcalls are not available in 32-bit.
      setLibcallName(RTLIB::SHL_I128, nullptr);
      setLibcallName(RTLIB::SRL_I128, nullptr);
      setLibcallName(RTLIB::SRA_I128, nullptr);
      setLibcallName(RTLIB::MUL_I128, nullptr);
      // The MULO libcall is not part of libgcc, only compiler-rt.
      setLibcallName(RTLIB::MULO_I64, nullptr);
    }

    // The MULO libcall is not part of libgcc, only compiler-rt.
    setLibcallName(RTLIB::MULO_I128, nullptr);

    if (TT.isOSMSVCRT()) {
      // MSVCRT doesn't have powi; fall back to pow
      setLibcallName(RTLIB::POWI_F32, nullptr);
      setLibcallName(RTLIB::POWI_F64, nullptr);
    }
  }
}
