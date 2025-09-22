//===-- TargetLibraryInfo.cpp - Runtime library information ----------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TargetLibraryInfo class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/TargetParser/Triple.h"
using namespace llvm;

static cl::opt<TargetLibraryInfoImpl::VectorLibrary> ClVectorLibrary(
    "vector-library", cl::Hidden, cl::desc("Vector functions library"),
    cl::init(TargetLibraryInfoImpl::NoLibrary),
    cl::values(clEnumValN(TargetLibraryInfoImpl::NoLibrary, "none",
                          "No vector functions library"),
               clEnumValN(TargetLibraryInfoImpl::Accelerate, "Accelerate",
                          "Accelerate framework"),
               clEnumValN(TargetLibraryInfoImpl::DarwinLibSystemM,
                          "Darwin_libsystem_m", "Darwin libsystem_m"),
               clEnumValN(TargetLibraryInfoImpl::LIBMVEC_X86, "LIBMVEC-X86",
                          "GLIBC Vector Math library"),
               clEnumValN(TargetLibraryInfoImpl::MASSV, "MASSV",
                          "IBM MASS vector library"),
               clEnumValN(TargetLibraryInfoImpl::SVML, "SVML",
                          "Intel SVML library"),
               clEnumValN(TargetLibraryInfoImpl::SLEEFGNUABI, "sleefgnuabi",
                          "SIMD Library for Evaluating Elementary Functions"),
               clEnumValN(TargetLibraryInfoImpl::ArmPL, "ArmPL",
                          "Arm Performance Libraries"),
               clEnumValN(TargetLibraryInfoImpl::AMDLIBM, "AMDLIBM",
                          "AMD vector math library")));

StringLiteral const TargetLibraryInfoImpl::StandardNames[LibFunc::NumLibFuncs] =
    {
#define TLI_DEFINE_STRING
#include "llvm/Analysis/TargetLibraryInfo.def"
};

std::string VecDesc::getVectorFunctionABIVariantString() const {
  assert(!VectorFnName.empty() && "Vector function name must not be empty.");
  SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  Out << VABIPrefix << "_" << ScalarFnName << "(" << VectorFnName << ")";
  return std::string(Out.str());
}

// Recognized types of library function arguments and return types.
enum FuncArgTypeID : char {
  Void = 0, // Must be zero.
  Bool,     // 8 bits on all targets
  Int16,
  Int32,
  Int,
  IntPlus, // Int or bigger.
  Long,    // Either 32 or 64 bits.
  IntX,    // Any integer type.
  Int64,
  LLong,    // 64 bits on all targets.
  SizeT,    // size_t.
  SSizeT,   // POSIX ssize_t.
  Flt,      // IEEE float.
  Dbl,      // IEEE double.
  LDbl,     // Any floating type (TODO: tighten this up).
  Floating, // Any floating type.
  Ptr,      // Any pointer type.
  Struct,   // Any struct type.
  Ellip,    // The ellipsis (...).
  Same,     // Same argument type as the previous one.
};

typedef std::array<FuncArgTypeID, 8> FuncProtoTy;

static const FuncProtoTy Signatures[] = {
#define TLI_DEFINE_SIG
#include "llvm/Analysis/TargetLibraryInfo.def"
};

static_assert(sizeof Signatures / sizeof *Signatures == LibFunc::NumLibFuncs,
              "Missing library function signatures");

static bool hasSinCosPiStret(const Triple &T) {
  // Only Darwin variants have _stret versions of combined trig functions.
  if (!T.isOSDarwin())
    return false;

  // The ABI is rather complicated on x86, so don't do anything special there.
  if (T.getArch() == Triple::x86)
    return false;

  if (T.isMacOSX() && T.isMacOSXVersionLT(10, 9))
    return false;

  if (T.isiOS() && T.isOSVersionLT(7, 0))
    return false;

  return true;
}

static bool hasBcmp(const Triple &TT) {
  // Posix removed support from bcmp() in 2001, but the glibc and several
  // implementations of the libc still have it.
  if (TT.isOSLinux())
    return TT.isGNUEnvironment() || TT.isMusl();
  // Both NetBSD and OpenBSD are planning to remove the function. Windows does
  // not have it.
  return TT.isOSFreeBSD() || TT.isOSSolaris();
}

static bool isCallingConvCCompatible(CallingConv::ID CC, StringRef TT,
                                     FunctionType *FuncTy) {
  switch (CC) {
  default:
    return false;
  case llvm::CallingConv::C:
    return true;
  case llvm::CallingConv::ARM_APCS:
  case llvm::CallingConv::ARM_AAPCS:
  case llvm::CallingConv::ARM_AAPCS_VFP: {

    // The iOS ABI diverges from the standard in some cases, so for now don't
    // try to simplify those calls.
    if (Triple(TT).isiOS())
      return false;

    if (!FuncTy->getReturnType()->isPointerTy() &&
        !FuncTy->getReturnType()->isIntegerTy() &&
        !FuncTy->getReturnType()->isVoidTy())
      return false;

    for (auto *Param : FuncTy->params()) {
      if (!Param->isPointerTy() && !Param->isIntegerTy())
        return false;
    }
    return true;
  }
  }
  return false;
}

bool TargetLibraryInfoImpl::isCallingConvCCompatible(CallBase *CI) {
  return ::isCallingConvCCompatible(CI->getCallingConv(),
                                    CI->getModule()->getTargetTriple(),
                                    CI->getFunctionType());
}

bool TargetLibraryInfoImpl::isCallingConvCCompatible(Function *F) {
  return ::isCallingConvCCompatible(F->getCallingConv(),
                                    F->getParent()->getTargetTriple(),
                                    F->getFunctionType());
}

static void initializeBase(TargetLibraryInfoImpl &TLI, const Triple &T) {
  bool ShouldExtI32Param, ShouldExtI32Return;
  bool ShouldSignExtI32Param, ShouldSignExtI32Return;
  TargetLibraryInfo::initExtensionsForTriple(
      ShouldExtI32Param, ShouldExtI32Return, ShouldSignExtI32Param,
      ShouldSignExtI32Return, T);
  TLI.setShouldExtI32Param(ShouldExtI32Param);
  TLI.setShouldExtI32Return(ShouldExtI32Return);
  TLI.setShouldSignExtI32Param(ShouldSignExtI32Param);
  TLI.setShouldSignExtI32Return(ShouldSignExtI32Return);

  // Let's assume by default that the size of int is 32 bits, unless the target
  // is a 16-bit architecture because then it most likely is 16 bits. If that
  // isn't true for a target those defaults should be overridden below.
  TLI.setIntSize(T.isArch16Bit() ? 16 : 32);
}

/// Initialize the set of available library functions based on the specified
/// target triple. This should be carefully written so that a missing target
/// triple gets a sane set of defaults.
static void initializeLibCalls(TargetLibraryInfoImpl &TLI, const Triple &T,
                               ArrayRef<StringLiteral> StandardNames) {
  // Set IO unlocked variants as unavailable
  // Set them as available per system below
  TLI.setUnavailable(LibFunc_getc_unlocked);
  TLI.setUnavailable(LibFunc_getchar_unlocked);
  TLI.setUnavailable(LibFunc_putc_unlocked);
  TLI.setUnavailable(LibFunc_putchar_unlocked);
  TLI.setUnavailable(LibFunc_fputc_unlocked);
  TLI.setUnavailable(LibFunc_fgetc_unlocked);
  TLI.setUnavailable(LibFunc_fread_unlocked);
  TLI.setUnavailable(LibFunc_fwrite_unlocked);
  TLI.setUnavailable(LibFunc_fputs_unlocked);
  TLI.setUnavailable(LibFunc_fgets_unlocked);

  // There is really no runtime library on AMDGPU, apart from
  // __kmpc_alloc/free_shared.
  if (T.isAMDGPU()) {
    TLI.disableAllFunctions();
    TLI.setAvailable(llvm::LibFunc___kmpc_alloc_shared);
    TLI.setAvailable(llvm::LibFunc___kmpc_free_shared);
    return;
  }

  // memset_pattern{4,8,16} is only available on iOS 3.0 and Mac OS X 10.5 and
  // later. All versions of watchOS support it.
  if (T.isMacOSX()) {
    // available IO unlocked variants on Mac OS X
    TLI.setAvailable(LibFunc_getc_unlocked);
    TLI.setAvailable(LibFunc_getchar_unlocked);
    TLI.setAvailable(LibFunc_putc_unlocked);
    TLI.setAvailable(LibFunc_putchar_unlocked);
    TLI.setUnavailable(LibFunc_memrchr);

    if (T.isMacOSXVersionLT(10, 5)) {
      TLI.setUnavailable(LibFunc_memset_pattern4);
      TLI.setUnavailable(LibFunc_memset_pattern8);
      TLI.setUnavailable(LibFunc_memset_pattern16);
    }
  } else if (T.isiOS()) {
    if (T.isOSVersionLT(3, 0)) {
      TLI.setUnavailable(LibFunc_memset_pattern4);
      TLI.setUnavailable(LibFunc_memset_pattern8);
      TLI.setUnavailable(LibFunc_memset_pattern16);
    }
  } else if (!T.isWatchOS()) {
    TLI.setUnavailable(LibFunc_memset_pattern4);
    TLI.setUnavailable(LibFunc_memset_pattern8);
    TLI.setUnavailable(LibFunc_memset_pattern16);
  }

  if (!hasSinCosPiStret(T)) {
    TLI.setUnavailable(LibFunc_sinpi);
    TLI.setUnavailable(LibFunc_sinpif);
    TLI.setUnavailable(LibFunc_cospi);
    TLI.setUnavailable(LibFunc_cospif);
    TLI.setUnavailable(LibFunc_sincospi_stret);
    TLI.setUnavailable(LibFunc_sincospif_stret);
  }

  if (!hasBcmp(T))
    TLI.setUnavailable(LibFunc_bcmp);

  if (T.isMacOSX() && T.getArch() == Triple::x86 &&
      !T.isMacOSXVersionLT(10, 7)) {
    // x86-32 OSX has a scheme where fwrite and fputs (and some other functions
    // we don't care about) have two versions; on recent OSX, the one we want
    // has a $UNIX2003 suffix. The two implementations are identical except
    // for the return value in some edge cases.  However, we don't want to
    // generate code that depends on the old symbols.
    TLI.setAvailableWithName(LibFunc_fwrite, "fwrite$UNIX2003");
    TLI.setAvailableWithName(LibFunc_fputs, "fputs$UNIX2003");
  }

  // iprintf and friends are only available on XCore, TCE, and Emscripten.
  if (T.getArch() != Triple::xcore && T.getArch() != Triple::tce &&
      T.getOS() != Triple::Emscripten) {
    TLI.setUnavailable(LibFunc_iprintf);
    TLI.setUnavailable(LibFunc_siprintf);
    TLI.setUnavailable(LibFunc_fiprintf);
  }

  // __small_printf and friends are only available on Emscripten.
  if (T.getOS() != Triple::Emscripten) {
    TLI.setUnavailable(LibFunc_small_printf);
    TLI.setUnavailable(LibFunc_small_sprintf);
    TLI.setUnavailable(LibFunc_small_fprintf);
  }

  if (T.isOSWindows() && !T.isOSCygMing()) {
    // XXX: The earliest documentation available at the moment is for VS2015/VC19:
    // https://docs.microsoft.com/en-us/cpp/c-runtime-library/floating-point-support?view=vs-2015
    // XXX: In order to use an MSVCRT older than VC19,
    // the specific library version must be explicit in the target triple,
    // e.g., x86_64-pc-windows-msvc18.
    bool hasPartialC99 = true;
    if (T.isKnownWindowsMSVCEnvironment()) {
      VersionTuple Version = T.getEnvironmentVersion();
      hasPartialC99 = (Version.getMajor() == 0 || Version.getMajor() >= 19);
    }

    // Latest targets support C89 math functions, in part.
    bool isARM = (T.getArch() == Triple::aarch64 ||
                  T.getArch() == Triple::arm);
    bool hasPartialFloat = (isARM ||
                            T.getArch() == Triple::x86_64);

    // Win32 does not support float C89 math functions, in general.
    if (!hasPartialFloat) {
      TLI.setUnavailable(LibFunc_acosf);
      TLI.setUnavailable(LibFunc_asinf);
      TLI.setUnavailable(LibFunc_atan2f);
      TLI.setUnavailable(LibFunc_atanf);
      TLI.setUnavailable(LibFunc_ceilf);
      TLI.setUnavailable(LibFunc_cosf);
      TLI.setUnavailable(LibFunc_coshf);
      TLI.setUnavailable(LibFunc_expf);
      TLI.setUnavailable(LibFunc_floorf);
      TLI.setUnavailable(LibFunc_fmodf);
      TLI.setUnavailable(LibFunc_log10f);
      TLI.setUnavailable(LibFunc_logf);
      TLI.setUnavailable(LibFunc_modff);
      TLI.setUnavailable(LibFunc_powf);
      TLI.setUnavailable(LibFunc_remainderf);
      TLI.setUnavailable(LibFunc_remquof);
      TLI.setUnavailable(LibFunc_sinf);
      TLI.setUnavailable(LibFunc_sinhf);
      TLI.setUnavailable(LibFunc_sqrtf);
      TLI.setUnavailable(LibFunc_tanf);
      TLI.setUnavailable(LibFunc_tanhf);
    }
    if (!isARM)
      TLI.setUnavailable(LibFunc_fabsf);
    TLI.setUnavailable(LibFunc_frexpf);
    TLI.setUnavailable(LibFunc_ldexpf);

    // Win32 does not support long double C89 math functions.
    TLI.setUnavailable(LibFunc_acosl);
    TLI.setUnavailable(LibFunc_asinl);
    TLI.setUnavailable(LibFunc_atan2l);
    TLI.setUnavailable(LibFunc_atanl);
    TLI.setUnavailable(LibFunc_ceill);
    TLI.setUnavailable(LibFunc_cosl);
    TLI.setUnavailable(LibFunc_coshl);
    TLI.setUnavailable(LibFunc_expl);
    TLI.setUnavailable(LibFunc_fabsl);
    TLI.setUnavailable(LibFunc_floorl);
    TLI.setUnavailable(LibFunc_fmodl);
    TLI.setUnavailable(LibFunc_frexpl);
    TLI.setUnavailable(LibFunc_ldexpl);
    TLI.setUnavailable(LibFunc_log10l);
    TLI.setUnavailable(LibFunc_logl);
    TLI.setUnavailable(LibFunc_modfl);
    TLI.setUnavailable(LibFunc_powl);
    TLI.setUnavailable(LibFunc_remainderl);
    TLI.setUnavailable(LibFunc_remquol);
    TLI.setUnavailable(LibFunc_sinl);
    TLI.setUnavailable(LibFunc_sinhl);
    TLI.setUnavailable(LibFunc_sqrtl);
    TLI.setUnavailable(LibFunc_tanl);
    TLI.setUnavailable(LibFunc_tanhl);

    // Win32 does not fully support C99 math functions.
    if (!hasPartialC99) {
      TLI.setUnavailable(LibFunc_acosh);
      TLI.setUnavailable(LibFunc_acoshf);
      TLI.setUnavailable(LibFunc_asinh);
      TLI.setUnavailable(LibFunc_asinhf);
      TLI.setUnavailable(LibFunc_atanh);
      TLI.setUnavailable(LibFunc_atanhf);
      TLI.setAvailableWithName(LibFunc_cabs, "_cabs");
      TLI.setUnavailable(LibFunc_cabsf);
      TLI.setUnavailable(LibFunc_cbrt);
      TLI.setUnavailable(LibFunc_cbrtf);
      TLI.setAvailableWithName(LibFunc_copysign, "_copysign");
      TLI.setAvailableWithName(LibFunc_copysignf, "_copysignf");
      TLI.setUnavailable(LibFunc_exp2);
      TLI.setUnavailable(LibFunc_exp2f);
      TLI.setUnavailable(LibFunc_expm1);
      TLI.setUnavailable(LibFunc_expm1f);
      TLI.setUnavailable(LibFunc_fmax);
      TLI.setUnavailable(LibFunc_fmaxf);
      TLI.setUnavailable(LibFunc_fmin);
      TLI.setUnavailable(LibFunc_fminf);
      TLI.setUnavailable(LibFunc_log1p);
      TLI.setUnavailable(LibFunc_log1pf);
      TLI.setUnavailable(LibFunc_log2);
      TLI.setUnavailable(LibFunc_log2f);
      TLI.setAvailableWithName(LibFunc_logb, "_logb");
      if (hasPartialFloat)
        TLI.setAvailableWithName(LibFunc_logbf, "_logbf");
      else
        TLI.setUnavailable(LibFunc_logbf);
      TLI.setUnavailable(LibFunc_rint);
      TLI.setUnavailable(LibFunc_rintf);
      TLI.setUnavailable(LibFunc_round);
      TLI.setUnavailable(LibFunc_roundf);
      TLI.setUnavailable(LibFunc_trunc);
      TLI.setUnavailable(LibFunc_truncf);
    }

    // Win32 does not support long double C99 math functions.
    TLI.setUnavailable(LibFunc_acoshl);
    TLI.setUnavailable(LibFunc_asinhl);
    TLI.setUnavailable(LibFunc_atanhl);
    TLI.setUnavailable(LibFunc_cabsl);
    TLI.setUnavailable(LibFunc_cbrtl);
    TLI.setUnavailable(LibFunc_copysignl);
    TLI.setUnavailable(LibFunc_exp2l);
    TLI.setUnavailable(LibFunc_expm1l);
    TLI.setUnavailable(LibFunc_fmaxl);
    TLI.setUnavailable(LibFunc_fminl);
    TLI.setUnavailable(LibFunc_log1pl);
    TLI.setUnavailable(LibFunc_log2l);
    TLI.setUnavailable(LibFunc_logbl);
    TLI.setUnavailable(LibFunc_nearbyintl);
    TLI.setUnavailable(LibFunc_rintl);
    TLI.setUnavailable(LibFunc_roundl);
    TLI.setUnavailable(LibFunc_truncl);

    // Win32 does not support these functions, but
    // they are generally available on POSIX-compliant systems.
    TLI.setUnavailable(LibFunc_access);
    TLI.setUnavailable(LibFunc_chmod);
    TLI.setUnavailable(LibFunc_closedir);
    TLI.setUnavailable(LibFunc_fdopen);
    TLI.setUnavailable(LibFunc_fileno);
    TLI.setUnavailable(LibFunc_fseeko);
    TLI.setUnavailable(LibFunc_fstat);
    TLI.setUnavailable(LibFunc_ftello);
    TLI.setUnavailable(LibFunc_gettimeofday);
    TLI.setUnavailable(LibFunc_memccpy);
    TLI.setUnavailable(LibFunc_mkdir);
    TLI.setUnavailable(LibFunc_open);
    TLI.setUnavailable(LibFunc_opendir);
    TLI.setUnavailable(LibFunc_pclose);
    TLI.setUnavailable(LibFunc_popen);
    TLI.setUnavailable(LibFunc_read);
    TLI.setUnavailable(LibFunc_rmdir);
    TLI.setUnavailable(LibFunc_stat);
    TLI.setUnavailable(LibFunc_strcasecmp);
    TLI.setUnavailable(LibFunc_strncasecmp);
    TLI.setUnavailable(LibFunc_unlink);
    TLI.setUnavailable(LibFunc_utime);
    TLI.setUnavailable(LibFunc_write);
  }

  if (T.isOSWindows() && !T.isWindowsCygwinEnvironment()) {
    // These functions aren't available in either MSVC or MinGW environments.
    TLI.setUnavailable(LibFunc_bcmp);
    TLI.setUnavailable(LibFunc_bcopy);
    TLI.setUnavailable(LibFunc_bzero);
    TLI.setUnavailable(LibFunc_chown);
    TLI.setUnavailable(LibFunc_ctermid);
    TLI.setUnavailable(LibFunc_ffs);
    TLI.setUnavailable(LibFunc_flockfile);
    TLI.setUnavailable(LibFunc_fstatvfs);
    TLI.setUnavailable(LibFunc_ftrylockfile);
    TLI.setUnavailable(LibFunc_funlockfile);
    TLI.setUnavailable(LibFunc_getitimer);
    TLI.setUnavailable(LibFunc_getlogin_r);
    TLI.setUnavailable(LibFunc_getpwnam);
    TLI.setUnavailable(LibFunc_htonl);
    TLI.setUnavailable(LibFunc_htons);
    TLI.setUnavailable(LibFunc_lchown);
    TLI.setUnavailable(LibFunc_lstat);
    TLI.setUnavailable(LibFunc_memrchr);
    TLI.setUnavailable(LibFunc_ntohl);
    TLI.setUnavailable(LibFunc_ntohs);
    TLI.setUnavailable(LibFunc_pread);
    TLI.setUnavailable(LibFunc_pwrite);
    TLI.setUnavailable(LibFunc_readlink);
    TLI.setUnavailable(LibFunc_realpath);
    TLI.setUnavailable(LibFunc_setitimer);
    TLI.setUnavailable(LibFunc_statvfs);
    TLI.setUnavailable(LibFunc_stpcpy);
    TLI.setUnavailable(LibFunc_stpncpy);
    TLI.setUnavailable(LibFunc_times);
    TLI.setUnavailable(LibFunc_uname);
    TLI.setUnavailable(LibFunc_unsetenv);
    TLI.setUnavailable(LibFunc_utimes);

    // MinGW does have ldexpf, but it is a plain wrapper over regular ldexp.
    // Therefore it's not beneficial to transform code to use it, i.e.
    // just pretend that the function is not available.
    TLI.setUnavailable(LibFunc_ldexpf);
  }

  // Pick just one set of new/delete variants.
  if (T.isOSMSVCRT()) {
    // MSVC, doesn't have the Itanium new/delete.
    TLI.setUnavailable(LibFunc_ZdaPv);
    TLI.setUnavailable(LibFunc_ZdaPvRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_ZdaPvSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZdaPvSt11align_val_tRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_ZdaPvj);
    TLI.setUnavailable(LibFunc_ZdaPvjSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZdaPvm);
    TLI.setUnavailable(LibFunc_ZdaPvmSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZdlPv);
    TLI.setUnavailable(LibFunc_ZdlPvRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_ZdlPvSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZdlPvSt11align_val_tRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_ZdlPvj);
    TLI.setUnavailable(LibFunc_ZdlPvjSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZdlPvm);
    TLI.setUnavailable(LibFunc_ZdlPvmSt11align_val_t);
    TLI.setUnavailable(LibFunc_Znaj);
    TLI.setUnavailable(LibFunc_ZnajRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_ZnajSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZnajSt11align_val_tRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_Znam);
    TLI.setUnavailable(LibFunc_ZnamRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_ZnamRKSt9nothrow_t12__hot_cold_t);
    TLI.setUnavailable(LibFunc_ZnamSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZnamSt11align_val_tRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_Znwj);
    TLI.setUnavailable(LibFunc_ZnwjRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_ZnwjSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZnwjSt11align_val_tRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_Znwm);
    TLI.setUnavailable(LibFunc_ZnwmRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_ZnwmRKSt9nothrow_t12__hot_cold_t);
    TLI.setUnavailable(LibFunc_ZnwmSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZnwmSt11align_val_tRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_Znwm12__hot_cold_t);
    TLI.setUnavailable(LibFunc_ZnwmSt11align_val_t12__hot_cold_t);
    TLI.setUnavailable(LibFunc_ZnwmSt11align_val_tRKSt9nothrow_t12__hot_cold_t);
    TLI.setUnavailable(LibFunc_Znam12__hot_cold_t);
    TLI.setUnavailable(LibFunc_ZnamSt11align_val_t12__hot_cold_t);
    TLI.setUnavailable(LibFunc_ZnamSt11align_val_tRKSt9nothrow_t12__hot_cold_t);
  } else {
    // Not MSVC, assume it's Itanium.
    TLI.setUnavailable(LibFunc_msvc_new_int);
    TLI.setUnavailable(LibFunc_msvc_new_int_nothrow);
    TLI.setUnavailable(LibFunc_msvc_new_longlong);
    TLI.setUnavailable(LibFunc_msvc_new_longlong_nothrow);
    TLI.setUnavailable(LibFunc_msvc_delete_ptr32);
    TLI.setUnavailable(LibFunc_msvc_delete_ptr32_nothrow);
    TLI.setUnavailable(LibFunc_msvc_delete_ptr32_int);
    TLI.setUnavailable(LibFunc_msvc_delete_ptr64);
    TLI.setUnavailable(LibFunc_msvc_delete_ptr64_nothrow);
    TLI.setUnavailable(LibFunc_msvc_delete_ptr64_longlong);
    TLI.setUnavailable(LibFunc_msvc_new_array_int);
    TLI.setUnavailable(LibFunc_msvc_new_array_int_nothrow);
    TLI.setUnavailable(LibFunc_msvc_new_array_longlong);
    TLI.setUnavailable(LibFunc_msvc_new_array_longlong_nothrow);
    TLI.setUnavailable(LibFunc_msvc_delete_array_ptr32);
    TLI.setUnavailable(LibFunc_msvc_delete_array_ptr32_nothrow);
    TLI.setUnavailable(LibFunc_msvc_delete_array_ptr32_int);
    TLI.setUnavailable(LibFunc_msvc_delete_array_ptr64);
    TLI.setUnavailable(LibFunc_msvc_delete_array_ptr64_nothrow);
    TLI.setUnavailable(LibFunc_msvc_delete_array_ptr64_longlong);
  }

  switch (T.getOS()) {
  case Triple::MacOSX:
    // exp10 and exp10f are not available on OS X until 10.9 and iOS until 7.0
    // and their names are __exp10 and __exp10f. exp10l is not available on
    // OS X or iOS.
    TLI.setUnavailable(LibFunc_exp10l);
    if (T.isMacOSXVersionLT(10, 9)) {
      TLI.setUnavailable(LibFunc_exp10);
      TLI.setUnavailable(LibFunc_exp10f);
    } else {
      TLI.setAvailableWithName(LibFunc_exp10, "__exp10");
      TLI.setAvailableWithName(LibFunc_exp10f, "__exp10f");
    }
    break;
  case Triple::IOS:
  case Triple::TvOS:
  case Triple::WatchOS:
  case Triple::XROS:
    TLI.setUnavailable(LibFunc_exp10l);
    if (!T.isWatchOS() &&
        (T.isOSVersionLT(7, 0) || (T.isOSVersionLT(9, 0) && T.isX86()))) {
      TLI.setUnavailable(LibFunc_exp10);
      TLI.setUnavailable(LibFunc_exp10f);
    } else {
      TLI.setAvailableWithName(LibFunc_exp10, "__exp10");
      TLI.setAvailableWithName(LibFunc_exp10f, "__exp10f");
    }
    break;
  case Triple::Linux:
    // exp10, exp10f, exp10l is available on Linux (GLIBC) but are extremely
    // buggy prior to glibc version 2.18. Until this version is widely deployed
    // or we have a reasonable detection strategy, we cannot use exp10 reliably
    // on Linux.
    //
    // Fall through to disable all of them.
    [[fallthrough]];
  default:
    TLI.setUnavailable(LibFunc_exp10);
    TLI.setUnavailable(LibFunc_exp10f);
    TLI.setUnavailable(LibFunc_exp10l);
  }

  // ffsl is available on at least Darwin, Mac OS X, iOS, FreeBSD, and
  // Linux (GLIBC):
  // http://developer.apple.com/library/mac/#documentation/Darwin/Reference/ManPages/man3/ffsl.3.html
  // http://svn.freebsd.org/base/head/lib/libc/string/ffsl.c
  // http://www.gnu.org/software/gnulib/manual/html_node/ffsl.html
  switch (T.getOS()) {
  case Triple::Darwin:
  case Triple::MacOSX:
  case Triple::IOS:
  case Triple::TvOS:
  case Triple::WatchOS:
  case Triple::XROS:
  case Triple::FreeBSD:
  case Triple::Linux:
    break;
  default:
    TLI.setUnavailable(LibFunc_ffsl);
  }

  // ffsll is available on at least FreeBSD and Linux (GLIBC):
  // http://svn.freebsd.org/base/head/lib/libc/string/ffsll.c
  // http://www.gnu.org/software/gnulib/manual/html_node/ffsll.html
  switch (T.getOS()) {
  case Triple::Darwin:
  case Triple::MacOSX:
  case Triple::IOS:
  case Triple::TvOS:
  case Triple::WatchOS:
  case Triple::XROS:
  case Triple::FreeBSD:
  case Triple::Linux:
    break;
  default:
    TLI.setUnavailable(LibFunc_ffsll);
  }

  // The following functions are available on at least FreeBSD:
  // http://svn.freebsd.org/base/head/lib/libc/string/fls.c
  // http://svn.freebsd.org/base/head/lib/libc/string/flsl.c
  // http://svn.freebsd.org/base/head/lib/libc/string/flsll.c
  if (!T.isOSFreeBSD()) {
    TLI.setUnavailable(LibFunc_fls);
    TLI.setUnavailable(LibFunc_flsl);
    TLI.setUnavailable(LibFunc_flsll);
  }

  // The following functions are only available on GNU/Linux (using glibc).
  // Linux variants without glibc (eg: bionic, musl) may have some subset.
  if (!T.isOSLinux() || !T.isGNUEnvironment()) {
    TLI.setUnavailable(LibFunc_dunder_strdup);
    TLI.setUnavailable(LibFunc_dunder_strtok_r);
    TLI.setUnavailable(LibFunc_dunder_isoc99_scanf);
    TLI.setUnavailable(LibFunc_dunder_isoc99_sscanf);
    TLI.setUnavailable(LibFunc_under_IO_getc);
    TLI.setUnavailable(LibFunc_under_IO_putc);
    // But, Android and musl have memalign.
    if (!T.isAndroid() && !T.isMusl())
      TLI.setUnavailable(LibFunc_memalign);
    TLI.setUnavailable(LibFunc_fopen64);
    TLI.setUnavailable(LibFunc_fseeko64);
    TLI.setUnavailable(LibFunc_fstat64);
    TLI.setUnavailable(LibFunc_fstatvfs64);
    TLI.setUnavailable(LibFunc_ftello64);
    TLI.setUnavailable(LibFunc_lstat64);
    TLI.setUnavailable(LibFunc_open64);
    TLI.setUnavailable(LibFunc_stat64);
    TLI.setUnavailable(LibFunc_statvfs64);
    TLI.setUnavailable(LibFunc_tmpfile64);

    // Relaxed math functions are included in math-finite.h on Linux (GLIBC).
    // Note that math-finite.h is no longer supported by top-of-tree GLIBC,
    // so we keep these functions around just so that they're recognized by
    // the ConstantFolder.
    TLI.setUnavailable(LibFunc_acos_finite);
    TLI.setUnavailable(LibFunc_acosf_finite);
    TLI.setUnavailable(LibFunc_acosl_finite);
    TLI.setUnavailable(LibFunc_acosh_finite);
    TLI.setUnavailable(LibFunc_acoshf_finite);
    TLI.setUnavailable(LibFunc_acoshl_finite);
    TLI.setUnavailable(LibFunc_asin_finite);
    TLI.setUnavailable(LibFunc_asinf_finite);
    TLI.setUnavailable(LibFunc_asinl_finite);
    TLI.setUnavailable(LibFunc_atan2_finite);
    TLI.setUnavailable(LibFunc_atan2f_finite);
    TLI.setUnavailable(LibFunc_atan2l_finite);
    TLI.setUnavailable(LibFunc_atanh_finite);
    TLI.setUnavailable(LibFunc_atanhf_finite);
    TLI.setUnavailable(LibFunc_atanhl_finite);
    TLI.setUnavailable(LibFunc_cosh_finite);
    TLI.setUnavailable(LibFunc_coshf_finite);
    TLI.setUnavailable(LibFunc_coshl_finite);
    TLI.setUnavailable(LibFunc_exp10_finite);
    TLI.setUnavailable(LibFunc_exp10f_finite);
    TLI.setUnavailable(LibFunc_exp10l_finite);
    TLI.setUnavailable(LibFunc_exp2_finite);
    TLI.setUnavailable(LibFunc_exp2f_finite);
    TLI.setUnavailable(LibFunc_exp2l_finite);
    TLI.setUnavailable(LibFunc_exp_finite);
    TLI.setUnavailable(LibFunc_expf_finite);
    TLI.setUnavailable(LibFunc_expl_finite);
    TLI.setUnavailable(LibFunc_log10_finite);
    TLI.setUnavailable(LibFunc_log10f_finite);
    TLI.setUnavailable(LibFunc_log10l_finite);
    TLI.setUnavailable(LibFunc_log2_finite);
    TLI.setUnavailable(LibFunc_log2f_finite);
    TLI.setUnavailable(LibFunc_log2l_finite);
    TLI.setUnavailable(LibFunc_log_finite);
    TLI.setUnavailable(LibFunc_logf_finite);
    TLI.setUnavailable(LibFunc_logl_finite);
    TLI.setUnavailable(LibFunc_pow_finite);
    TLI.setUnavailable(LibFunc_powf_finite);
    TLI.setUnavailable(LibFunc_powl_finite);
    TLI.setUnavailable(LibFunc_sinh_finite);
    TLI.setUnavailable(LibFunc_sinhf_finite);
    TLI.setUnavailable(LibFunc_sinhl_finite);
    TLI.setUnavailable(LibFunc_sqrt_finite);
    TLI.setUnavailable(LibFunc_sqrtf_finite);
    TLI.setUnavailable(LibFunc_sqrtl_finite);
  }

  if ((T.isOSLinux() && T.isGNUEnvironment()) ||
      (T.isAndroid() && !T.isAndroidVersionLT(28))) {
    // available IO unlocked variants on GNU/Linux and Android P or later
    TLI.setAvailable(LibFunc_getc_unlocked);
    TLI.setAvailable(LibFunc_getchar_unlocked);
    TLI.setAvailable(LibFunc_putc_unlocked);
    TLI.setAvailable(LibFunc_putchar_unlocked);
    TLI.setAvailable(LibFunc_fputc_unlocked);
    TLI.setAvailable(LibFunc_fgetc_unlocked);
    TLI.setAvailable(LibFunc_fread_unlocked);
    TLI.setAvailable(LibFunc_fwrite_unlocked);
    TLI.setAvailable(LibFunc_fputs_unlocked);
    TLI.setAvailable(LibFunc_fgets_unlocked);
  }

  if (T.isAndroid() && T.isAndroidVersionLT(21)) {
    TLI.setUnavailable(LibFunc_stpcpy);
    TLI.setUnavailable(LibFunc_stpncpy);
  }

  if (T.isPS()) {
    // PS4/PS5 do have memalign.
    TLI.setAvailable(LibFunc_memalign);

    // PS4/PS5 do not have new/delete with "unsigned int" size parameter;
    // they only have the "unsigned long" versions.
    TLI.setUnavailable(LibFunc_ZdaPvj);
    TLI.setUnavailable(LibFunc_ZdaPvjSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZdlPvj);
    TLI.setUnavailable(LibFunc_ZdlPvjSt11align_val_t);
    TLI.setUnavailable(LibFunc_Znaj);
    TLI.setUnavailable(LibFunc_ZnajRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_ZnajSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZnajSt11align_val_tRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_Znwj);
    TLI.setUnavailable(LibFunc_ZnwjRKSt9nothrow_t);
    TLI.setUnavailable(LibFunc_ZnwjSt11align_val_t);
    TLI.setUnavailable(LibFunc_ZnwjSt11align_val_tRKSt9nothrow_t);

    // None of the *_chk functions.
    TLI.setUnavailable(LibFunc_memccpy_chk);
    TLI.setUnavailable(LibFunc_memcpy_chk);
    TLI.setUnavailable(LibFunc_memmove_chk);
    TLI.setUnavailable(LibFunc_mempcpy_chk);
    TLI.setUnavailable(LibFunc_memset_chk);
    TLI.setUnavailable(LibFunc_snprintf_chk);
    TLI.setUnavailable(LibFunc_sprintf_chk);
    TLI.setUnavailable(LibFunc_stpcpy_chk);
    TLI.setUnavailable(LibFunc_stpncpy_chk);
    TLI.setUnavailable(LibFunc_strcat_chk);
    TLI.setUnavailable(LibFunc_strcpy_chk);
    TLI.setUnavailable(LibFunc_strlcat_chk);
    TLI.setUnavailable(LibFunc_strlcpy_chk);
    TLI.setUnavailable(LibFunc_strlen_chk);
    TLI.setUnavailable(LibFunc_strncat_chk);
    TLI.setUnavailable(LibFunc_strncpy_chk);
    TLI.setUnavailable(LibFunc_vsnprintf_chk);
    TLI.setUnavailable(LibFunc_vsprintf_chk);

    // Various Posix system functions.
    TLI.setUnavailable(LibFunc_access);
    TLI.setUnavailable(LibFunc_chmod);
    TLI.setUnavailable(LibFunc_chown);
    TLI.setUnavailable(LibFunc_closedir);
    TLI.setUnavailable(LibFunc_ctermid);
    TLI.setUnavailable(LibFunc_execl);
    TLI.setUnavailable(LibFunc_execle);
    TLI.setUnavailable(LibFunc_execlp);
    TLI.setUnavailable(LibFunc_execv);
    TLI.setUnavailable(LibFunc_execvP);
    TLI.setUnavailable(LibFunc_execve);
    TLI.setUnavailable(LibFunc_execvp);
    TLI.setUnavailable(LibFunc_execvpe);
    TLI.setUnavailable(LibFunc_fork);
    TLI.setUnavailable(LibFunc_fstat);
    TLI.setUnavailable(LibFunc_fstatvfs);
    TLI.setUnavailable(LibFunc_getenv);
    TLI.setUnavailable(LibFunc_getitimer);
    TLI.setUnavailable(LibFunc_getlogin_r);
    TLI.setUnavailable(LibFunc_getpwnam);
    TLI.setUnavailable(LibFunc_gettimeofday);
    TLI.setUnavailable(LibFunc_lchown);
    TLI.setUnavailable(LibFunc_lstat);
    TLI.setUnavailable(LibFunc_mkdir);
    TLI.setUnavailable(LibFunc_open);
    TLI.setUnavailable(LibFunc_opendir);
    TLI.setUnavailable(LibFunc_pclose);
    TLI.setUnavailable(LibFunc_popen);
    TLI.setUnavailable(LibFunc_pread);
    TLI.setUnavailable(LibFunc_pwrite);
    TLI.setUnavailable(LibFunc_read);
    TLI.setUnavailable(LibFunc_readlink);
    TLI.setUnavailable(LibFunc_realpath);
    TLI.setUnavailable(LibFunc_rename);
    TLI.setUnavailable(LibFunc_rmdir);
    TLI.setUnavailable(LibFunc_setitimer);
    TLI.setUnavailable(LibFunc_stat);
    TLI.setUnavailable(LibFunc_statvfs);
    TLI.setUnavailable(LibFunc_system);
    TLI.setUnavailable(LibFunc_times);
    TLI.setUnavailable(LibFunc_tmpfile);
    TLI.setUnavailable(LibFunc_unlink);
    TLI.setUnavailable(LibFunc_uname);
    TLI.setUnavailable(LibFunc_unsetenv);
    TLI.setUnavailable(LibFunc_utime);
    TLI.setUnavailable(LibFunc_utimes);
    TLI.setUnavailable(LibFunc_valloc);
    TLI.setUnavailable(LibFunc_write);

    // Miscellaneous other functions not provided.
    TLI.setUnavailable(LibFunc_atomic_load);
    TLI.setUnavailable(LibFunc_atomic_store);
    TLI.setUnavailable(LibFunc___kmpc_alloc_shared);
    TLI.setUnavailable(LibFunc___kmpc_free_shared);
    TLI.setUnavailable(LibFunc_dunder_strndup);
    TLI.setUnavailable(LibFunc_bcmp);
    TLI.setUnavailable(LibFunc_bcopy);
    TLI.setUnavailable(LibFunc_bzero);
    TLI.setUnavailable(LibFunc_cabs);
    TLI.setUnavailable(LibFunc_cabsf);
    TLI.setUnavailable(LibFunc_cabsl);
    TLI.setUnavailable(LibFunc_ffs);
    TLI.setUnavailable(LibFunc_flockfile);
    TLI.setUnavailable(LibFunc_fseeko);
    TLI.setUnavailable(LibFunc_ftello);
    TLI.setUnavailable(LibFunc_ftrylockfile);
    TLI.setUnavailable(LibFunc_funlockfile);
    TLI.setUnavailable(LibFunc_htonl);
    TLI.setUnavailable(LibFunc_htons);
    TLI.setUnavailable(LibFunc_isascii);
    TLI.setUnavailable(LibFunc_memccpy);
    TLI.setUnavailable(LibFunc_mempcpy);
    TLI.setUnavailable(LibFunc_memrchr);
    TLI.setUnavailable(LibFunc_ntohl);
    TLI.setUnavailable(LibFunc_ntohs);
    TLI.setUnavailable(LibFunc_reallocf);
    TLI.setUnavailable(LibFunc_roundeven);
    TLI.setUnavailable(LibFunc_roundevenf);
    TLI.setUnavailable(LibFunc_roundevenl);
    TLI.setUnavailable(LibFunc_stpcpy);
    TLI.setUnavailable(LibFunc_stpncpy);
    TLI.setUnavailable(LibFunc_strlcat);
    TLI.setUnavailable(LibFunc_strlcpy);
    TLI.setUnavailable(LibFunc_strndup);
    TLI.setUnavailable(LibFunc_strnlen);
    TLI.setUnavailable(LibFunc_toascii);
  }

  // As currently implemented in clang, NVPTX code has no standard library to
  // speak of.  Headers provide a standard-ish library implementation, but many
  // of the signatures are wrong -- for example, many libm functions are not
  // extern "C".
  //
  // libdevice, an IR library provided by nvidia, is linked in by the front-end,
  // but only used functions are provided to llvm.  Moreover, most of the
  // functions in libdevice don't map precisely to standard library functions.
  //
  // FIXME: Having no standard library prevents e.g. many fastmath
  // optimizations, so this situation should be fixed.
  if (T.isNVPTX()) {
    TLI.disableAllFunctions();
    TLI.setAvailable(LibFunc_nvvm_reflect);
    TLI.setAvailable(llvm::LibFunc_malloc);
    TLI.setAvailable(llvm::LibFunc_free);

    // TODO: We could enable the following two according to [0] but we haven't
    //       done an evaluation wrt. the performance implications.
    // [0]
    // https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#dynamic-global-memory-allocation-and-operations
    //
    //    TLI.setAvailable(llvm::LibFunc_memcpy);
    //    TLI.setAvailable(llvm::LibFunc_memset);

    TLI.setAvailable(llvm::LibFunc___kmpc_alloc_shared);
    TLI.setAvailable(llvm::LibFunc___kmpc_free_shared);
  } else {
    TLI.setUnavailable(LibFunc_nvvm_reflect);
  }

  // These vec_malloc/free routines are only available on AIX.
  if (!T.isOSAIX()) {
    TLI.setUnavailable(LibFunc_vec_calloc);
    TLI.setUnavailable(LibFunc_vec_malloc);
    TLI.setUnavailable(LibFunc_vec_realloc);
    TLI.setUnavailable(LibFunc_vec_free);
  }

  if (T.isOSAIX())
    TLI.setUnavailable(LibFunc_memrchr);

  TLI.addVectorizableFunctionsFromVecLib(ClVectorLibrary, T);
}

/// Initialize the set of available library functions based on the specified
/// target triple. This should be carefully written so that a missing target
/// triple gets a sane set of defaults.
static void initialize(TargetLibraryInfoImpl &TLI, const Triple &T,
                       ArrayRef<StringLiteral> StandardNames) {
  initializeBase(TLI, T);
  initializeLibCalls(TLI, T, StandardNames);
}

TargetLibraryInfoImpl::TargetLibraryInfoImpl() {
  // Default to nothing being available.
  memset(AvailableArray, 0, sizeof(AvailableArray));
  initializeBase(*this, Triple());
}

TargetLibraryInfoImpl::TargetLibraryInfoImpl(const Triple &T) {
  // Default to everything being available.
  memset(AvailableArray, -1, sizeof(AvailableArray));

  initialize(*this, T, StandardNames);
}

TargetLibraryInfoImpl::TargetLibraryInfoImpl(const TargetLibraryInfoImpl &TLI)
    : CustomNames(TLI.CustomNames), ShouldExtI32Param(TLI.ShouldExtI32Param),
      ShouldExtI32Return(TLI.ShouldExtI32Return),
      ShouldSignExtI32Param(TLI.ShouldSignExtI32Param),
      ShouldSignExtI32Return(TLI.ShouldSignExtI32Return),
      SizeOfInt(TLI.SizeOfInt) {
  memcpy(AvailableArray, TLI.AvailableArray, sizeof(AvailableArray));
  VectorDescs = TLI.VectorDescs;
  ScalarDescs = TLI.ScalarDescs;
}

TargetLibraryInfoImpl::TargetLibraryInfoImpl(TargetLibraryInfoImpl &&TLI)
    : CustomNames(std::move(TLI.CustomNames)),
      ShouldExtI32Param(TLI.ShouldExtI32Param),
      ShouldExtI32Return(TLI.ShouldExtI32Return),
      ShouldSignExtI32Param(TLI.ShouldSignExtI32Param),
      ShouldSignExtI32Return(TLI.ShouldSignExtI32Return),
      SizeOfInt(TLI.SizeOfInt) {
  std::move(std::begin(TLI.AvailableArray), std::end(TLI.AvailableArray),
            AvailableArray);
  VectorDescs = TLI.VectorDescs;
  ScalarDescs = TLI.ScalarDescs;
}

TargetLibraryInfoImpl &TargetLibraryInfoImpl::operator=(const TargetLibraryInfoImpl &TLI) {
  CustomNames = TLI.CustomNames;
  ShouldExtI32Param = TLI.ShouldExtI32Param;
  ShouldExtI32Return = TLI.ShouldExtI32Return;
  ShouldSignExtI32Param = TLI.ShouldSignExtI32Param;
  ShouldSignExtI32Return = TLI.ShouldSignExtI32Return;
  SizeOfInt = TLI.SizeOfInt;
  memcpy(AvailableArray, TLI.AvailableArray, sizeof(AvailableArray));
  return *this;
}

TargetLibraryInfoImpl &TargetLibraryInfoImpl::operator=(TargetLibraryInfoImpl &&TLI) {
  CustomNames = std::move(TLI.CustomNames);
  ShouldExtI32Param = TLI.ShouldExtI32Param;
  ShouldExtI32Return = TLI.ShouldExtI32Return;
  ShouldSignExtI32Param = TLI.ShouldSignExtI32Param;
  ShouldSignExtI32Return = TLI.ShouldSignExtI32Return;
  SizeOfInt = TLI.SizeOfInt;
  std::move(std::begin(TLI.AvailableArray), std::end(TLI.AvailableArray),
            AvailableArray);
  return *this;
}

static StringRef sanitizeFunctionName(StringRef funcName) {
  // Filter out empty names and names containing null bytes, those can't be in
  // our table.
  if (funcName.empty() || funcName.contains('\0'))
    return StringRef();

  // Check for \01 prefix that is used to mangle __asm declarations and
  // strip it if present.
  return GlobalValue::dropLLVMManglingEscape(funcName);
}

static DenseMap<StringRef, LibFunc>
buildIndexMap(ArrayRef<StringLiteral> StandardNames) {
  DenseMap<StringRef, LibFunc> Indices;
  unsigned Idx = 0;
  Indices.reserve(LibFunc::NumLibFuncs);
  for (const auto &Func : StandardNames)
    Indices[Func] = static_cast<LibFunc>(Idx++);
  return Indices;
}

bool TargetLibraryInfoImpl::getLibFunc(StringRef funcName, LibFunc &F) const {
  funcName = sanitizeFunctionName(funcName);
  if (funcName.empty())
    return false;

  static const DenseMap<StringRef, LibFunc> Indices =
      buildIndexMap(StandardNames);

  if (auto Loc = Indices.find(funcName); Loc != Indices.end()) {
    F = Loc->second;
    return true;
  }
  return false;
}

// Return true if ArgTy matches Ty.

static bool matchType(FuncArgTypeID ArgTy, const Type *Ty, unsigned IntBits,
                      unsigned SizeTBits) {
  switch (ArgTy) {
  case Void:
    return Ty->isVoidTy();
  case Bool:
    return Ty->isIntegerTy(8);
  case Int16:
    return Ty->isIntegerTy(16);
  case Int32:
    return Ty->isIntegerTy(32);
  case Int:
    return Ty->isIntegerTy(IntBits);
  case IntPlus:
    return Ty->isIntegerTy() && Ty->getPrimitiveSizeInBits() >= IntBits;
  case IntX:
    return Ty->isIntegerTy();
  case Long:
    // TODO: Figure out and use long size.
    return Ty->isIntegerTy() && Ty->getPrimitiveSizeInBits() >= IntBits;
  case Int64:
    return Ty->isIntegerTy(64);
  case LLong:
    return Ty->isIntegerTy(64);
  case SizeT:
  case SSizeT:
    return Ty->isIntegerTy(SizeTBits);
  case Flt:
    return Ty->isFloatTy();
  case Dbl:
    return Ty->isDoubleTy();
    // TODO: Tighten this up.
  case LDbl:
    return Ty->isFloatingPointTy();
  case Floating:
    return Ty->isFloatingPointTy();
  case Ptr:
    return Ty->isPointerTy();
  case Struct:
    return Ty->isStructTy();
  default:
    break;
  }

  llvm_unreachable("Invalid type");
}

bool TargetLibraryInfoImpl::isValidProtoForLibFunc(const FunctionType &FTy,
                                                   LibFunc F,
                                                   const Module &M) const {
  unsigned NumParams = FTy.getNumParams();

  switch (F) {
    // Special handling for <complex.h> functions:
  case LibFunc_cabs:
  case LibFunc_cabsf:
  case LibFunc_cabsl: {
    Type *RetTy = FTy.getReturnType();
    if (!RetTy->isFloatingPointTy())
      return false;

    Type *ParamTy = FTy.getParamType(0);
    // NOTE: These prototypes are target specific and currently support
    // "complex" passed as an array or discrete real & imaginary parameters.
    // Add other calling conventions to enable libcall optimizations.
    if (NumParams == 1)
      return (ParamTy->isArrayTy() && ParamTy->getArrayNumElements() == 2 &&
              ParamTy->getArrayElementType() == RetTy);
    else if (NumParams == 2)
      return ParamTy == RetTy && FTy.getParamType(1) == RetTy;

    return false;
  }
    // Special handling for the sincospi functions that return either
    // a struct or vector:
  case LibFunc_sincospi_stret:
  case LibFunc_sincospif_stret: {
    if (NumParams != 1)
      return false;

    Type *RetTy = FTy.getReturnType();
    Type *ParamTy = FTy.getParamType(0);
    if (auto *Ty = dyn_cast<StructType>(RetTy)) {
      if (Ty->getNumElements() != 2)
        return false;
      return (Ty->getElementType(0) == ParamTy &&
              Ty->getElementType(1) == ParamTy);
    }

    if (auto *Ty = dyn_cast<FixedVectorType>(RetTy)) {
      if (Ty->getNumElements() != 2)
        return false;
      return Ty->getElementType() == ParamTy;
    }

    return false;
  }

  default:
    break;
  }

  unsigned IntBits = getIntSize();
  unsigned SizeTBits = getSizeTSize(M);
  unsigned Idx = 0;

  // Iterate over the type ids in the function prototype, matching each
  // against the function's type FTy, starting with its return type.
  // Return true if both match in number and kind, inclduing the ellipsis.
  Type *Ty = FTy.getReturnType(), *LastTy = Ty;
  const auto &ProtoTypes = Signatures[F];
  for (auto TyID : ProtoTypes) {
    if (Idx && TyID == Void)
      // Except in the first position where it designates the function's
      // return type Void ends the argument list.
      break;

    if (TyID == Ellip) {
      // The ellipsis ends the protoype list but is not a part of FTy's
      // argument list.  Except when it's last it must be followed by
      // Void.
      assert(Idx == ProtoTypes.size() - 1 || ProtoTypes[Idx + 1] == Void);
      return FTy.isFunctionVarArg();
    }

    if (TyID == Same) {
      assert(Idx != 0 && "Type ID 'Same' must not be first!");
      if (Ty != LastTy)
        return false;
    } else {
      if (!Ty || !matchType(TyID, Ty, IntBits, SizeTBits))
        return false;
      LastTy = Ty;
    }

    if (Idx == NumParams) {
      // There's at least one and at most two more type ids than there are
      // arguments in FTy's argument list.
      Ty = nullptr;
      ++Idx;
      continue;
    }

    Ty = FTy.getParamType(Idx++);
  }

  // Return success only if all entries on both lists have been processed
  // and the function is not a variadic one.
  return Idx == NumParams + 1 && !FTy.isFunctionVarArg();
}

bool TargetLibraryInfoImpl::getLibFunc(const Function &FDecl,
                                       LibFunc &F) const {
  // Intrinsics don't overlap w/libcalls; if our module has a large number of
  // intrinsics, this ends up being an interesting compile time win since we
  // avoid string normalization and comparison.
  if (FDecl.isIntrinsic()) return false;

  const Module *M = FDecl.getParent();
  assert(M && "Expecting FDecl to be connected to a Module.");

  if (FDecl.LibFuncCache == Function::UnknownLibFunc)
    if (!getLibFunc(FDecl.getName(), FDecl.LibFuncCache))
      FDecl.LibFuncCache = NotLibFunc;

  if (FDecl.LibFuncCache == NotLibFunc)
    return false;

  F = FDecl.LibFuncCache;
  return isValidProtoForLibFunc(*FDecl.getFunctionType(), F, *M);
}

bool TargetLibraryInfoImpl::getLibFunc(unsigned int Opcode, Type *Ty,
                                       LibFunc &F) const {
  // Must be a frem instruction with float or double arguments.
  if (Opcode != Instruction::FRem || (!Ty->isDoubleTy() && !Ty->isFloatTy()))
    return false;

  F = Ty->isDoubleTy() ? LibFunc_fmod : LibFunc_fmodf;
  return true;
}

void TargetLibraryInfoImpl::disableAllFunctions() {
  memset(AvailableArray, 0, sizeof(AvailableArray));
}

static bool compareByScalarFnName(const VecDesc &LHS, const VecDesc &RHS) {
  return LHS.getScalarFnName() < RHS.getScalarFnName();
}

static bool compareByVectorFnName(const VecDesc &LHS, const VecDesc &RHS) {
  return LHS.getVectorFnName() < RHS.getVectorFnName();
}

static bool compareWithScalarFnName(const VecDesc &LHS, StringRef S) {
  return LHS.getScalarFnName() < S;
}

void TargetLibraryInfoImpl::addVectorizableFunctions(ArrayRef<VecDesc> Fns) {
  llvm::append_range(VectorDescs, Fns);
  llvm::sort(VectorDescs, compareByScalarFnName);

  llvm::append_range(ScalarDescs, Fns);
  llvm::sort(ScalarDescs, compareByVectorFnName);
}

static const VecDesc VecFuncs_Accelerate[] = {
#define TLI_DEFINE_ACCELERATE_VECFUNCS
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_ACCELERATE_VECFUNCS
};

static const VecDesc VecFuncs_DarwinLibSystemM[] = {
#define TLI_DEFINE_DARWIN_LIBSYSTEM_M_VECFUNCS
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_DARWIN_LIBSYSTEM_M_VECFUNCS
};

static const VecDesc VecFuncs_LIBMVEC_X86[] = {
#define TLI_DEFINE_LIBMVEC_X86_VECFUNCS
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_LIBMVEC_X86_VECFUNCS
};

static const VecDesc VecFuncs_MASSV[] = {
#define TLI_DEFINE_MASSV_VECFUNCS
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_MASSV_VECFUNCS
};

static const VecDesc VecFuncs_SVML[] = {
#define TLI_DEFINE_SVML_VECFUNCS
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_SVML_VECFUNCS
};

static const VecDesc VecFuncs_SLEEFGNUABI_VF2[] = {
#define TLI_DEFINE_SLEEFGNUABI_VF2_VECFUNCS
#define TLI_DEFINE_VECFUNC(SCAL, VEC, VF, VABI_PREFIX)                         \
  {SCAL, VEC, VF, /* MASK = */ false, VABI_PREFIX},
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_SLEEFGNUABI_VF2_VECFUNCS
};
static const VecDesc VecFuncs_SLEEFGNUABI_VF4[] = {
#define TLI_DEFINE_SLEEFGNUABI_VF4_VECFUNCS
#define TLI_DEFINE_VECFUNC(SCAL, VEC, VF, VABI_PREFIX)                         \
  {SCAL, VEC, VF, /* MASK = */ false, VABI_PREFIX},
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_SLEEFGNUABI_VF4_VECFUNCS
};
static const VecDesc VecFuncs_SLEEFGNUABI_VFScalable[] = {
#define TLI_DEFINE_SLEEFGNUABI_SCALABLE_VECFUNCS
#define TLI_DEFINE_VECFUNC(SCAL, VEC, VF, MASK, VABI_PREFIX)                   \
  {SCAL, VEC, VF, MASK, VABI_PREFIX},
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_SLEEFGNUABI_SCALABLE_VECFUNCS
};

static const VecDesc VecFuncs_ArmPL[] = {
#define TLI_DEFINE_ARMPL_VECFUNCS
#define TLI_DEFINE_VECFUNC(SCAL, VEC, VF, MASK, VABI_PREFIX)                   \
  {SCAL, VEC, VF, MASK, VABI_PREFIX},
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_ARMPL_VECFUNCS
};

const VecDesc VecFuncs_AMDLIBM[] = {
#define TLI_DEFINE_AMDLIBM_VECFUNCS
#define TLI_DEFINE_VECFUNC(SCAL, VEC, VF, MASK, VABI_PREFIX)                   \
  {SCAL, VEC, VF, MASK, VABI_PREFIX},
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_AMDLIBM_VECFUNCS
};

void TargetLibraryInfoImpl::addVectorizableFunctionsFromVecLib(
    enum VectorLibrary VecLib, const llvm::Triple &TargetTriple) {
  switch (VecLib) {
  case Accelerate: {
    addVectorizableFunctions(VecFuncs_Accelerate);
    break;
  }
  case DarwinLibSystemM: {
    addVectorizableFunctions(VecFuncs_DarwinLibSystemM);
    break;
  }
  case LIBMVEC_X86: {
    addVectorizableFunctions(VecFuncs_LIBMVEC_X86);
    break;
  }
  case MASSV: {
    addVectorizableFunctions(VecFuncs_MASSV);
    break;
  }
  case SVML: {
    addVectorizableFunctions(VecFuncs_SVML);
    break;
  }
  case SLEEFGNUABI: {
    switch (TargetTriple.getArch()) {
    default:
      break;
    case llvm::Triple::aarch64:
    case llvm::Triple::aarch64_be:
      addVectorizableFunctions(VecFuncs_SLEEFGNUABI_VF2);
      addVectorizableFunctions(VecFuncs_SLEEFGNUABI_VF4);
      addVectorizableFunctions(VecFuncs_SLEEFGNUABI_VFScalable);
      break;
    }
    break;
  }
  case ArmPL: {
    switch (TargetTriple.getArch()) {
    default:
      break;
    case llvm::Triple::aarch64:
    case llvm::Triple::aarch64_be:
      addVectorizableFunctions(VecFuncs_ArmPL);
      break;
    }
    break;
  }
  case AMDLIBM: {
    addVectorizableFunctions(VecFuncs_AMDLIBM);
    break;
  }
  case NoLibrary:
    break;
  }
}

bool TargetLibraryInfoImpl::isFunctionVectorizable(StringRef funcName) const {
  funcName = sanitizeFunctionName(funcName);
  if (funcName.empty())
    return false;

  std::vector<VecDesc>::const_iterator I =
      llvm::lower_bound(VectorDescs, funcName, compareWithScalarFnName);
  return I != VectorDescs.end() && StringRef(I->getScalarFnName()) == funcName;
}

StringRef TargetLibraryInfoImpl::getVectorizedFunction(StringRef F,
                                                       const ElementCount &VF,
                                                       bool Masked) const {
  const VecDesc *VD = getVectorMappingInfo(F, VF, Masked);
  if (VD)
    return VD->getVectorFnName();
  return StringRef();
}

const VecDesc *
TargetLibraryInfoImpl::getVectorMappingInfo(StringRef F, const ElementCount &VF,
                                            bool Masked) const {
  F = sanitizeFunctionName(F);
  if (F.empty())
    return nullptr;
  std::vector<VecDesc>::const_iterator I =
      llvm::lower_bound(VectorDescs, F, compareWithScalarFnName);
  while (I != VectorDescs.end() && StringRef(I->getScalarFnName()) == F) {
    if ((I->getVectorizationFactor() == VF) && (I->isMasked() == Masked))
      return &(*I);
    ++I;
  }
  return nullptr;
}

TargetLibraryInfo TargetLibraryAnalysis::run(const Function &F,
                                             FunctionAnalysisManager &) {
  if (!BaselineInfoImpl)
    BaselineInfoImpl =
        TargetLibraryInfoImpl(Triple(F.getParent()->getTargetTriple()));
  return TargetLibraryInfo(*BaselineInfoImpl, &F);
}

unsigned TargetLibraryInfoImpl::getWCharSize(const Module &M) const {
  if (auto *ShortWChar = cast_or_null<ConstantAsMetadata>(
      M.getModuleFlag("wchar_size")))
    return cast<ConstantInt>(ShortWChar->getValue())->getZExtValue();
  return 0;
}

unsigned TargetLibraryInfoImpl::getSizeTSize(const Module &M) const {
  // There is really no guarantee that sizeof(size_t) is equal to sizeof(int*).
  // If that isn't true then it should be possible to derive the SizeTTy from
  // the target triple here instead and do an early return.

  // Historically LLVM assume that size_t has same size as intptr_t (hence
  // deriving the size from sizeof(int*) in address space zero). This should
  // work for most targets. For future consideration: DataLayout also implement
  // getIndexSizeInBits which might map better to size_t compared to
  // getPointerSizeInBits. Hard coding address space zero here might be
  // unfortunate as well. Maybe getDefaultGlobalsAddressSpace() or
  // getAllocaAddrSpace() is better.
  unsigned AddressSpace = 0;
  return M.getDataLayout().getPointerSizeInBits(AddressSpace);
}

TargetLibraryInfoWrapperPass::TargetLibraryInfoWrapperPass()
    : ImmutablePass(ID), TLA(TargetLibraryInfoImpl()) {
  initializeTargetLibraryInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

TargetLibraryInfoWrapperPass::TargetLibraryInfoWrapperPass(const Triple &T)
    : ImmutablePass(ID), TLA(TargetLibraryInfoImpl(T)) {
  initializeTargetLibraryInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

TargetLibraryInfoWrapperPass::TargetLibraryInfoWrapperPass(
    const TargetLibraryInfoImpl &TLIImpl)
    : ImmutablePass(ID), TLA(TLIImpl) {
  initializeTargetLibraryInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

TargetLibraryInfoWrapperPass::TargetLibraryInfoWrapperPass(
    const TargetLibraryInfo &TLIOther)
    : TargetLibraryInfoWrapperPass(*TLIOther.Impl) {}

AnalysisKey TargetLibraryAnalysis::Key;

// Register the basic pass.
INITIALIZE_PASS(TargetLibraryInfoWrapperPass, "targetlibinfo",
                "Target Library Information", false, true)
char TargetLibraryInfoWrapperPass::ID = 0;

void TargetLibraryInfoWrapperPass::anchor() {}

void TargetLibraryInfoImpl::getWidestVF(StringRef ScalarF,
                                        ElementCount &FixedVF,
                                        ElementCount &ScalableVF) const {
  ScalarF = sanitizeFunctionName(ScalarF);
  // Use '0' here because a type of the form <vscale x 1 x ElTy> is not the
  // same as a scalar.
  ScalableVF = ElementCount::getScalable(0);
  FixedVF = ElementCount::getFixed(1);
  if (ScalarF.empty())
    return;

  std::vector<VecDesc>::const_iterator I =
      llvm::lower_bound(VectorDescs, ScalarF, compareWithScalarFnName);
  while (I != VectorDescs.end() && StringRef(I->getScalarFnName()) == ScalarF) {
    ElementCount *VF =
        I->getVectorizationFactor().isScalable() ? &ScalableVF : &FixedVF;
    if (ElementCount::isKnownGT(I->getVectorizationFactor(), *VF))
      *VF = I->getVectorizationFactor();
    ++I;
  }
}
