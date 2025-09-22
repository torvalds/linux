//===--- TargetInfo.cpp - Information about Target machine ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the TargetInfo interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/LangOptions.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/TargetParser.h"
#include <cstdlib>
using namespace clang;

static const LangASMap DefaultAddrSpaceMap = {0};
// The fake address space map must have a distinct entry for each
// language-specific address space.
static const LangASMap FakeAddrSpaceMap = {
    0,  // Default
    1,  // opencl_global
    3,  // opencl_local
    2,  // opencl_constant
    0,  // opencl_private
    4,  // opencl_generic
    5,  // opencl_global_device
    6,  // opencl_global_host
    7,  // cuda_device
    8,  // cuda_constant
    9,  // cuda_shared
    1,  // sycl_global
    5,  // sycl_global_device
    6,  // sycl_global_host
    3,  // sycl_local
    0,  // sycl_private
    10, // ptr32_sptr
    11, // ptr32_uptr
    12, // ptr64
    13, // hlsl_groupshared
    20, // wasm_funcref
};

// TargetInfo Constructor.
TargetInfo::TargetInfo(const llvm::Triple &T) : Triple(T) {
  // Set defaults.  Defaults are set for a 32-bit RISC platform, like PPC or
  // SPARC.  These should be overridden by concrete targets as needed.
  BigEndian = !T.isLittleEndian();
  TLSSupported = true;
  VLASupported = true;
  NoAsmVariants = false;
  HasLegalHalfType = false;
  HalfArgsAndReturns = false;
  HasFloat128 = false;
  HasIbm128 = false;
  HasFloat16 = false;
  HasBFloat16 = false;
  HasFullBFloat16 = false;
  HasLongDouble = true;
  HasFPReturn = true;
  HasStrictFP = false;
  PointerWidth = PointerAlign = 32;
  BoolWidth = BoolAlign = 8;
  IntWidth = IntAlign = 32;
  LongWidth = LongAlign = 32;
  LongLongWidth = LongLongAlign = 64;
  Int128Align = 128;

  // Fixed point default bit widths
  ShortAccumWidth = ShortAccumAlign = 16;
  AccumWidth = AccumAlign = 32;
  LongAccumWidth = LongAccumAlign = 64;
  ShortFractWidth = ShortFractAlign = 8;
  FractWidth = FractAlign = 16;
  LongFractWidth = LongFractAlign = 32;

  // Fixed point default integral and fractional bit sizes
  // We give the _Accum 1 fewer fractional bits than their corresponding _Fract
  // types by default to have the same number of fractional bits between _Accum
  // and _Fract types.
  PaddingOnUnsignedFixedPoint = false;
  ShortAccumScale = 7;
  AccumScale = 15;
  LongAccumScale = 31;

  SuitableAlign = 64;
  DefaultAlignForAttributeAligned = 128;
  MinGlobalAlign = 0;
  // From the glibc documentation, on GNU systems, malloc guarantees 16-byte
  // alignment on 64-bit systems and 8-byte alignment on 32-bit systems. See
  // https://www.gnu.org/software/libc/manual/html_node/Malloc-Examples.html.
  // This alignment guarantee also applies to Windows and Android. On Darwin
  // and OpenBSD, the alignment is 16 bytes on both 64-bit and 32-bit systems.
  if (T.isGNUEnvironment() || T.isWindowsMSVCEnvironment() || T.isAndroid() ||
      T.isOHOSFamily())
    NewAlign = Triple.isArch64Bit() ? 128 : Triple.isArch32Bit() ? 64 : 0;
  else if (T.isOSDarwin() || T.isOSOpenBSD())
    NewAlign = 128;
  else
    NewAlign = 0; // Infer from basic type alignment.
  HalfWidth = 16;
  HalfAlign = 16;
  FloatWidth = 32;
  FloatAlign = 32;
  DoubleWidth = 64;
  DoubleAlign = 64;
  LongDoubleWidth = 64;
  LongDoubleAlign = 64;
  Float128Align = 128;
  Ibm128Align = 128;
  LargeArrayMinWidth = 0;
  LargeArrayAlign = 0;
  MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 0;
  MaxVectorAlign = 0;
  MaxTLSAlign = 0;
  SizeType = UnsignedLong;
  PtrDiffType = SignedLong;
  IntMaxType = SignedLongLong;
  IntPtrType = SignedLong;
  WCharType = SignedInt;
  WIntType = SignedInt;
  Char16Type = UnsignedShort;
  Char32Type = UnsignedInt;
  Int64Type = SignedLongLong;
  Int16Type = SignedShort;
  SigAtomicType = SignedInt;
  ProcessIDType = SignedInt;
  UseSignedCharForObjCBool = true;
  UseBitFieldTypeAlignment = true;
  UseZeroLengthBitfieldAlignment = false;
  UseLeadingZeroLengthBitfield = true;
  UseExplicitBitFieldAlignment = true;
  ZeroLengthBitfieldBoundary = 0;
  MaxAlignedAttribute = 0;
  HalfFormat = &llvm::APFloat::IEEEhalf();
  FloatFormat = &llvm::APFloat::IEEEsingle();
  DoubleFormat = &llvm::APFloat::IEEEdouble();
  LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  Float128Format = &llvm::APFloat::IEEEquad();
  Ibm128Format = &llvm::APFloat::PPCDoubleDouble();
  MCountName = "mcount";
  UserLabelPrefix = "_";
  RegParmMax = 0;
  SSERegParmMax = 0;
  HasAlignMac68kSupport = false;
  HasBuiltinMSVaList = false;
  IsRenderScriptTarget = false;
  HasAArch64SVETypes = false;
  HasRISCVVTypes = false;
  AllowAMDGPUUnsafeFPAtomics = false;
  HasUnalignedAccess = false;
  ARMCDECoprocMask = 0;

  // Default to no types using fpret.
  RealTypeUsesObjCFPRetMask = 0;

  // Default to not using fp2ret for __Complex long double
  ComplexLongDoubleUsesFP2Ret = false;

  // Set the C++ ABI based on the triple.
  TheCXXABI.set(Triple.isKnownWindowsMSVCEnvironment()
                    ? TargetCXXABI::Microsoft
                    : TargetCXXABI::GenericItanium);

  // Default to an empty address space map.
  AddrSpaceMap = &DefaultAddrSpaceMap;
  UseAddrSpaceMapMangling = false;

  // Default to an unknown platform name.
  PlatformName = "unknown";
  PlatformMinVersion = VersionTuple();

  MaxOpenCLWorkGroupSize = 1024;

  MaxBitIntWidth.reset();
}

// Out of line virtual dtor for TargetInfo.
TargetInfo::~TargetInfo() {}

void TargetInfo::resetDataLayout(StringRef DL, const char *ULP) {
  DataLayoutString = DL.str();
  UserLabelPrefix = ULP;
}

bool
TargetInfo::checkCFProtectionBranchSupported(DiagnosticsEngine &Diags) const {
  Diags.Report(diag::err_opt_not_valid_on_target) << "cf-protection=branch";
  return false;
}

bool
TargetInfo::checkCFProtectionReturnSupported(DiagnosticsEngine &Diags) const {
  Diags.Report(diag::err_opt_not_valid_on_target) << "cf-protection=return";
  return false;
}

/// getTypeName - Return the user string for the specified integer type enum.
/// For example, SignedShort -> "short".
const char *TargetInfo::getTypeName(IntType T) {
  switch (T) {
  default: llvm_unreachable("not an integer!");
  case SignedChar:       return "signed char";
  case UnsignedChar:     return "unsigned char";
  case SignedShort:      return "short";
  case UnsignedShort:    return "unsigned short";
  case SignedInt:        return "int";
  case UnsignedInt:      return "unsigned int";
  case SignedLong:       return "long int";
  case UnsignedLong:     return "long unsigned int";
  case SignedLongLong:   return "long long int";
  case UnsignedLongLong: return "long long unsigned int";
  }
}

/// getTypeConstantSuffix - Return the constant suffix for the specified
/// integer type enum. For example, SignedLong -> "L".
const char *TargetInfo::getTypeConstantSuffix(IntType T) const {
  switch (T) {
  default: llvm_unreachable("not an integer!");
  case SignedChar:
  case SignedShort:
  case SignedInt:        return "";
  case SignedLong:       return "L";
  case SignedLongLong:   return "LL";
  case UnsignedChar:
    if (getCharWidth() < getIntWidth())
      return "";
    [[fallthrough]];
  case UnsignedShort:
    if (getShortWidth() < getIntWidth())
      return "";
    [[fallthrough]];
  case UnsignedInt:      return "U";
  case UnsignedLong:     return "UL";
  case UnsignedLongLong: return "ULL";
  }
}

/// getTypeFormatModifier - Return the printf format modifier for the
/// specified integer type enum. For example, SignedLong -> "l".

const char *TargetInfo::getTypeFormatModifier(IntType T) {
  switch (T) {
  default: llvm_unreachable("not an integer!");
  case SignedChar:
  case UnsignedChar:     return "hh";
  case SignedShort:
  case UnsignedShort:    return "h";
  case SignedInt:
  case UnsignedInt:      return "";
  case SignedLong:
  case UnsignedLong:     return "l";
  case SignedLongLong:
  case UnsignedLongLong: return "ll";
  }
}

/// getTypeWidth - Return the width (in bits) of the specified integer type
/// enum. For example, SignedInt -> getIntWidth().
unsigned TargetInfo::getTypeWidth(IntType T) const {
  switch (T) {
  default: llvm_unreachable("not an integer!");
  case SignedChar:
  case UnsignedChar:     return getCharWidth();
  case SignedShort:
  case UnsignedShort:    return getShortWidth();
  case SignedInt:
  case UnsignedInt:      return getIntWidth();
  case SignedLong:
  case UnsignedLong:     return getLongWidth();
  case SignedLongLong:
  case UnsignedLongLong: return getLongLongWidth();
  };
}

TargetInfo::IntType TargetInfo::getIntTypeByWidth(
    unsigned BitWidth, bool IsSigned) const {
  if (getCharWidth() == BitWidth)
    return IsSigned ? SignedChar : UnsignedChar;
  if (getShortWidth() == BitWidth)
    return IsSigned ? SignedShort : UnsignedShort;
  if (getIntWidth() == BitWidth)
    return IsSigned ? SignedInt : UnsignedInt;
  if (getLongWidth() == BitWidth)
    return IsSigned ? SignedLong : UnsignedLong;
  if (getLongLongWidth() == BitWidth)
    return IsSigned ? SignedLongLong : UnsignedLongLong;
  return NoInt;
}

TargetInfo::IntType TargetInfo::getLeastIntTypeByWidth(unsigned BitWidth,
                                                       bool IsSigned) const {
  if (getCharWidth() >= BitWidth)
    return IsSigned ? SignedChar : UnsignedChar;
  if (getShortWidth() >= BitWidth)
    return IsSigned ? SignedShort : UnsignedShort;
  if (getIntWidth() >= BitWidth)
    return IsSigned ? SignedInt : UnsignedInt;
  if (getLongWidth() >= BitWidth)
    return IsSigned ? SignedLong : UnsignedLong;
  if (getLongLongWidth() >= BitWidth)
    return IsSigned ? SignedLongLong : UnsignedLongLong;
  return NoInt;
}

FloatModeKind TargetInfo::getRealTypeByWidth(unsigned BitWidth,
                                             FloatModeKind ExplicitType) const {
  if (getHalfWidth() == BitWidth)
    return FloatModeKind::Half;
  if (getFloatWidth() == BitWidth)
    return FloatModeKind::Float;
  if (getDoubleWidth() == BitWidth)
    return FloatModeKind::Double;

  switch (BitWidth) {
  case 96:
    if (&getLongDoubleFormat() == &llvm::APFloat::x87DoubleExtended())
      return FloatModeKind::LongDouble;
    break;
  case 128:
    // The caller explicitly asked for an IEEE compliant type but we still
    // have to check if the target supports it.
    if (ExplicitType == FloatModeKind::Float128)
      return hasFloat128Type() ? FloatModeKind::Float128
                               : FloatModeKind::NoFloat;
    if (ExplicitType == FloatModeKind::Ibm128)
      return hasIbm128Type() ? FloatModeKind::Ibm128
                             : FloatModeKind::NoFloat;
    if (&getLongDoubleFormat() == &llvm::APFloat::PPCDoubleDouble() ||
        &getLongDoubleFormat() == &llvm::APFloat::IEEEquad())
      return FloatModeKind::LongDouble;
    if (hasFloat128Type())
      return FloatModeKind::Float128;
    break;
  }

  return FloatModeKind::NoFloat;
}

/// getTypeAlign - Return the alignment (in bits) of the specified integer type
/// enum. For example, SignedInt -> getIntAlign().
unsigned TargetInfo::getTypeAlign(IntType T) const {
  switch (T) {
  default: llvm_unreachable("not an integer!");
  case SignedChar:
  case UnsignedChar:     return getCharAlign();
  case SignedShort:
  case UnsignedShort:    return getShortAlign();
  case SignedInt:
  case UnsignedInt:      return getIntAlign();
  case SignedLong:
  case UnsignedLong:     return getLongAlign();
  case SignedLongLong:
  case UnsignedLongLong: return getLongLongAlign();
  };
}

/// isTypeSigned - Return whether an integer types is signed. Returns true if
/// the type is signed; false otherwise.
bool TargetInfo::isTypeSigned(IntType T) {
  switch (T) {
  default: llvm_unreachable("not an integer!");
  case SignedChar:
  case SignedShort:
  case SignedInt:
  case SignedLong:
  case SignedLongLong:
    return true;
  case UnsignedChar:
  case UnsignedShort:
  case UnsignedInt:
  case UnsignedLong:
  case UnsignedLongLong:
    return false;
  };
}

/// adjust - Set forced language options.
/// Apply changes to the target information with respect to certain
/// language options which change the target configuration and adjust
/// the language based on the target options where applicable.
void TargetInfo::adjust(DiagnosticsEngine &Diags, LangOptions &Opts) {
  if (Opts.NoBitFieldTypeAlign)
    UseBitFieldTypeAlignment = false;

  switch (Opts.WCharSize) {
  default: llvm_unreachable("invalid wchar_t width");
  case 0: break;
  case 1: WCharType = Opts.WCharIsSigned ? SignedChar : UnsignedChar; break;
  case 2: WCharType = Opts.WCharIsSigned ? SignedShort : UnsignedShort; break;
  case 4: WCharType = Opts.WCharIsSigned ? SignedInt : UnsignedInt; break;
  }

  if (Opts.AlignDouble) {
    DoubleAlign = LongLongAlign = 64;
    LongDoubleAlign = 64;
  }

  // HLSL explicitly defines the sizes and formats of some data types, and we
  // need to conform to those regardless of what architecture you are targeting.
  if (Opts.HLSL) {
    LongWidth = LongAlign = 64;
    if (!Opts.NativeHalfType) {
      HalfFormat = &llvm::APFloat::IEEEsingle();
      HalfWidth = HalfAlign = 32;
    }
  }

  if (Opts.OpenCL) {
    // OpenCL C requires specific widths for types, irrespective of
    // what these normally are for the target.
    // We also define long long and long double here, although the
    // OpenCL standard only mentions these as "reserved".
    IntWidth = IntAlign = 32;
    LongWidth = LongAlign = 64;
    LongLongWidth = LongLongAlign = 128;
    HalfWidth = HalfAlign = 16;
    FloatWidth = FloatAlign = 32;

    // Embedded 32-bit targets (OpenCL EP) might have double C type
    // defined as float. Let's not override this as it might lead
    // to generating illegal code that uses 64bit doubles.
    if (DoubleWidth != FloatWidth) {
      DoubleWidth = DoubleAlign = 64;
      DoubleFormat = &llvm::APFloat::IEEEdouble();
    }
    LongDoubleWidth = LongDoubleAlign = 128;

    unsigned MaxPointerWidth = getMaxPointerWidth();
    assert(MaxPointerWidth == 32 || MaxPointerWidth == 64);
    bool Is32BitArch = MaxPointerWidth == 32;
    SizeType = Is32BitArch ? UnsignedInt : UnsignedLong;
    PtrDiffType = Is32BitArch ? SignedInt : SignedLong;
    IntPtrType = Is32BitArch ? SignedInt : SignedLong;

    IntMaxType = SignedLongLong;
    Int64Type = SignedLong;

    HalfFormat = &llvm::APFloat::IEEEhalf();
    FloatFormat = &llvm::APFloat::IEEEsingle();
    LongDoubleFormat = &llvm::APFloat::IEEEquad();

    // OpenCL C v3.0 s6.7.5 - The generic address space requires support for
    // OpenCL C 2.0 or OpenCL C 3.0 with the __opencl_c_generic_address_space
    // feature
    // OpenCL C v3.0 s6.2.1 - OpenCL pipes require support of OpenCL C 2.0
    // or later and __opencl_c_pipes feature
    // FIXME: These language options are also defined in setLangDefaults()
    // for OpenCL C 2.0 but with no access to target capabilities. Target
    // should be immutable once created and thus these language options need
    // to be defined only once.
    if (Opts.getOpenCLCompatibleVersion() == 300) {
      const auto &OpenCLFeaturesMap = getSupportedOpenCLOpts();
      Opts.OpenCLGenericAddressSpace = hasFeatureEnabled(
          OpenCLFeaturesMap, "__opencl_c_generic_address_space");
      Opts.OpenCLPipes =
          hasFeatureEnabled(OpenCLFeaturesMap, "__opencl_c_pipes");
      Opts.Blocks =
          hasFeatureEnabled(OpenCLFeaturesMap, "__opencl_c_device_enqueue");
    }
  }

  if (Opts.DoubleSize) {
    if (Opts.DoubleSize == 32) {
      DoubleWidth = 32;
      LongDoubleWidth = 32;
      DoubleFormat = &llvm::APFloat::IEEEsingle();
      LongDoubleFormat = &llvm::APFloat::IEEEsingle();
    } else if (Opts.DoubleSize == 64) {
      DoubleWidth = 64;
      LongDoubleWidth = 64;
      DoubleFormat = &llvm::APFloat::IEEEdouble();
      LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    }
  }

  if (Opts.LongDoubleSize) {
    if (Opts.LongDoubleSize == DoubleWidth) {
      LongDoubleWidth = DoubleWidth;
      LongDoubleAlign = DoubleAlign;
      LongDoubleFormat = DoubleFormat;
    } else if (Opts.LongDoubleSize == 128) {
      LongDoubleWidth = LongDoubleAlign = 128;
      LongDoubleFormat = &llvm::APFloat::IEEEquad();
    } else if (Opts.LongDoubleSize == 80) {
      LongDoubleFormat = &llvm::APFloat::x87DoubleExtended();
      if (getTriple().isWindowsMSVCEnvironment()) {
        LongDoubleWidth = 128;
        LongDoubleAlign = 128;
      } else { // Linux
        if (getTriple().getArch() == llvm::Triple::x86) {
          LongDoubleWidth = 96;
          LongDoubleAlign = 32;
        } else {
          LongDoubleWidth = 128;
          LongDoubleAlign = 128;
        }
      }
    }
  }

  if (Opts.NewAlignOverride)
    NewAlign = Opts.NewAlignOverride * getCharWidth();

  // Each unsigned fixed point type has the same number of fractional bits as
  // its corresponding signed type.
  PaddingOnUnsignedFixedPoint |= Opts.PaddingOnUnsignedFixedPoint;
  CheckFixedPointBits();

  if (Opts.ProtectParens && !checkArithmeticFenceSupported()) {
    Diags.Report(diag::err_opt_not_valid_on_target) << "-fprotect-parens";
    Opts.ProtectParens = false;
  }

  if (Opts.MaxBitIntWidth)
    MaxBitIntWidth = static_cast<unsigned>(Opts.MaxBitIntWidth);

  if (Opts.FakeAddressSpaceMap)
    AddrSpaceMap = &FakeAddrSpaceMap;
}

bool TargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeatureVec) const {
  for (const auto &F : FeatureVec) {
    StringRef Name = F;
    if (Name.empty())
      continue;
    // Apply the feature via the target.
    if (Name[0] != '+' && Name[0] != '-')
      Diags.Report(diag::warn_fe_backend_invalid_feature_flag) << Name;
    else
      setFeatureEnabled(Features, Name.substr(1), Name[0] == '+');
  }
  return true;
}

ParsedTargetAttr TargetInfo::parseTargetAttr(StringRef Features) const {
  ParsedTargetAttr Ret;
  if (Features == "default")
    return Ret;
  SmallVector<StringRef, 1> AttrFeatures;
  Features.split(AttrFeatures, ",");

  // Grab the various features and prepend a "+" to turn on the feature to
  // the backend and add them to our existing set of features.
  for (auto &Feature : AttrFeatures) {
    // Go ahead and trim whitespace rather than either erroring or
    // accepting it weirdly.
    Feature = Feature.trim();

    // TODO: Support the fpmath option. It will require checking
    // overall feature validity for the function with the rest of the
    // attributes on the function.
    if (Feature.starts_with("fpmath="))
      continue;

    if (Feature.starts_with("branch-protection=")) {
      Ret.BranchProtection = Feature.split('=').second.trim();
      continue;
    }

    // While we're here iterating check for a different target cpu.
    if (Feature.starts_with("arch=")) {
      if (!Ret.CPU.empty())
        Ret.Duplicate = "arch=";
      else
        Ret.CPU = Feature.split("=").second.trim();
    } else if (Feature.starts_with("tune=")) {
      if (!Ret.Tune.empty())
        Ret.Duplicate = "tune=";
      else
        Ret.Tune = Feature.split("=").second.trim();
    } else if (Feature.starts_with("no-"))
      Ret.Features.push_back("-" + Feature.split("-").second.str());
    else
      Ret.Features.push_back("+" + Feature.str());
  }
  return Ret;
}

TargetInfo::CallingConvKind
TargetInfo::getCallingConvKind(bool ClangABICompat4) const {
  if (getCXXABI() != TargetCXXABI::Microsoft &&
      (ClangABICompat4 || getTriple().isPS4()))
    return CCK_ClangABI4OrPS4;
  return CCK_Default;
}

bool TargetInfo::areDefaultedSMFStillPOD(const LangOptions &LangOpts) const {
  return LangOpts.getClangABICompat() > LangOptions::ClangABI::Ver15;
}

LangAS TargetInfo::getOpenCLTypeAddrSpace(OpenCLTypeKind TK) const {
  switch (TK) {
  case OCLTK_Image:
  case OCLTK_Pipe:
    return LangAS::opencl_global;

  case OCLTK_Sampler:
    return LangAS::opencl_constant;

  default:
    return LangAS::Default;
  }
}

//===----------------------------------------------------------------------===//


static StringRef removeGCCRegisterPrefix(StringRef Name) {
  if (Name[0] == '%' || Name[0] == '#')
    Name = Name.substr(1);

  return Name;
}

/// isValidClobber - Returns whether the passed in string is
/// a valid clobber in an inline asm statement. This is used by
/// Sema.
bool TargetInfo::isValidClobber(StringRef Name) const {
  return (isValidGCCRegisterName(Name) || Name == "memory" || Name == "cc" ||
          Name == "unwind");
}

/// isValidGCCRegisterName - Returns whether the passed in string
/// is a valid register name according to GCC. This is used by Sema for
/// inline asm statements.
bool TargetInfo::isValidGCCRegisterName(StringRef Name) const {
  if (Name.empty())
    return false;

  // Get rid of any register prefix.
  Name = removeGCCRegisterPrefix(Name);
  if (Name.empty())
    return false;

  ArrayRef<const char *> Names = getGCCRegNames();

  // If we have a number it maps to an entry in the register name array.
  if (isDigit(Name[0])) {
    unsigned n;
    if (!Name.getAsInteger(0, n))
      return n < Names.size();
  }

  // Check register names.
  if (llvm::is_contained(Names, Name))
    return true;

  // Check any additional names that we have.
  for (const AddlRegName &ARN : getGCCAddlRegNames())
    for (const char *AN : ARN.Names) {
      if (!AN)
        break;
      // Make sure the register that the additional name is for is within
      // the bounds of the register names from above.
      if (AN == Name && ARN.RegNum < Names.size())
        return true;
    }

  // Now check aliases.
  for (const GCCRegAlias &GRA : getGCCRegAliases())
    for (const char *A : GRA.Aliases) {
      if (!A)
        break;
      if (A == Name)
        return true;
    }

  return false;
}

StringRef TargetInfo::getNormalizedGCCRegisterName(StringRef Name,
                                                   bool ReturnCanonical) const {
  assert(isValidGCCRegisterName(Name) && "Invalid register passed in");

  // Get rid of any register prefix.
  Name = removeGCCRegisterPrefix(Name);

  ArrayRef<const char *> Names = getGCCRegNames();

  // First, check if we have a number.
  if (isDigit(Name[0])) {
    unsigned n;
    if (!Name.getAsInteger(0, n)) {
      assert(n < Names.size() && "Out of bounds register number!");
      return Names[n];
    }
  }

  // Check any additional names that we have.
  for (const AddlRegName &ARN : getGCCAddlRegNames())
    for (const char *AN : ARN.Names) {
      if (!AN)
        break;
      // Make sure the register that the additional name is for is within
      // the bounds of the register names from above.
      if (AN == Name && ARN.RegNum < Names.size())
        return ReturnCanonical ? Names[ARN.RegNum] : Name;
    }

  // Now check aliases.
  for (const GCCRegAlias &RA : getGCCRegAliases())
    for (const char *A : RA.Aliases) {
      if (!A)
        break;
      if (A == Name)
        return RA.Register;
    }

  return Name;
}

bool TargetInfo::validateOutputConstraint(ConstraintInfo &Info) const {
  const char *Name = Info.getConstraintStr().c_str();
  // An output constraint must start with '=' or '+'
  if (*Name != '=' && *Name != '+')
    return false;

  if (*Name == '+')
    Info.setIsReadWrite();

  Name++;
  while (*Name) {
    switch (*Name) {
    default:
      if (!validateAsmConstraint(Name, Info)) {
        // FIXME: We temporarily return false
        // so we can add more constraints as we hit it.
        // Eventually, an unknown constraint should just be treated as 'g'.
        return false;
      }
      break;
    case '&': // early clobber.
      Info.setEarlyClobber();
      break;
    case '%': // commutative.
      // FIXME: Check that there is a another register after this one.
      break;
    case 'r': // general register.
      Info.setAllowsRegister();
      break;
    case 'm': // memory operand.
    case 'o': // offsetable memory operand.
    case 'V': // non-offsetable memory operand.
    case '<': // autodecrement memory operand.
    case '>': // autoincrement memory operand.
      Info.setAllowsMemory();
      break;
    case 'g': // general register, memory operand or immediate integer.
    case 'X': // any operand.
      Info.setAllowsRegister();
      Info.setAllowsMemory();
      break;
    case ',': // multiple alternative constraint.  Pass it.
      // Handle additional optional '=' or '+' modifiers.
      if (Name[1] == '=' || Name[1] == '+')
        Name++;
      break;
    case '#': // Ignore as constraint.
      while (Name[1] && Name[1] != ',')
        Name++;
      break;
    case '?': // Disparage slightly code.
    case '!': // Disparage severely.
    case '*': // Ignore for choosing register preferences.
    case 'i': // Ignore i,n,E,F as output constraints (match from the other
              // chars)
    case 'n':
    case 'E':
    case 'F':
      break;  // Pass them.
    }

    Name++;
  }

  // Early clobber with a read-write constraint which doesn't permit registers
  // is invalid.
  if (Info.earlyClobber() && Info.isReadWrite() && !Info.allowsRegister())
    return false;

  // If a constraint allows neither memory nor register operands it contains
  // only modifiers. Reject it.
  return Info.allowsMemory() || Info.allowsRegister();
}

bool TargetInfo::resolveSymbolicName(const char *&Name,
                                     ArrayRef<ConstraintInfo> OutputConstraints,
                                     unsigned &Index) const {
  assert(*Name == '[' && "Symbolic name did not start with '['");
  Name++;
  const char *Start = Name;
  while (*Name && *Name != ']')
    Name++;

  if (!*Name) {
    // Missing ']'
    return false;
  }

  std::string SymbolicName(Start, Name - Start);

  for (Index = 0; Index != OutputConstraints.size(); ++Index)
    if (SymbolicName == OutputConstraints[Index].getName())
      return true;

  return false;
}

bool TargetInfo::validateInputConstraint(
                              MutableArrayRef<ConstraintInfo> OutputConstraints,
                              ConstraintInfo &Info) const {
  const char *Name = Info.ConstraintStr.c_str();

  if (!*Name)
    return false;

  while (*Name) {
    switch (*Name) {
    default:
      // Check if we have a matching constraint
      if (*Name >= '0' && *Name <= '9') {
        const char *DigitStart = Name;
        while (Name[1] >= '0' && Name[1] <= '9')
          Name++;
        const char *DigitEnd = Name;
        unsigned i;
        if (StringRef(DigitStart, DigitEnd - DigitStart + 1)
                .getAsInteger(10, i))
          return false;

        // Check if matching constraint is out of bounds.
        if (i >= OutputConstraints.size()) return false;

        // A number must refer to an output only operand.
        if (OutputConstraints[i].isReadWrite())
          return false;

        // If the constraint is already tied, it must be tied to the
        // same operand referenced to by the number.
        if (Info.hasTiedOperand() && Info.getTiedOperand() != i)
          return false;

        // The constraint should have the same info as the respective
        // output constraint.
        Info.setTiedOperand(i, OutputConstraints[i]);
      } else if (!validateAsmConstraint(Name, Info)) {
        // FIXME: This error return is in place temporarily so we can
        // add more constraints as we hit it.  Eventually, an unknown
        // constraint should just be treated as 'g'.
        return false;
      }
      break;
    case '[': {
      unsigned Index = 0;
      if (!resolveSymbolicName(Name, OutputConstraints, Index))
        return false;

      // If the constraint is already tied, it must be tied to the
      // same operand referenced to by the number.
      if (Info.hasTiedOperand() && Info.getTiedOperand() != Index)
        return false;

      // A number must refer to an output only operand.
      if (OutputConstraints[Index].isReadWrite())
        return false;

      Info.setTiedOperand(Index, OutputConstraints[Index]);
      break;
    }
    case '%': // commutative
      // FIXME: Fail if % is used with the last operand.
      break;
    case 'i': // immediate integer.
      break;
    case 'n': // immediate integer with a known value.
      Info.setRequiresImmediate();
      break;
    case 'I':  // Various constant constraints with target-specific meanings.
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
      if (!validateAsmConstraint(Name, Info))
        return false;
      break;
    case 'r': // general register.
      Info.setAllowsRegister();
      break;
    case 'm': // memory operand.
    case 'o': // offsettable memory operand.
    case 'V': // non-offsettable memory operand.
    case '<': // autodecrement memory operand.
    case '>': // autoincrement memory operand.
      Info.setAllowsMemory();
      break;
    case 'g': // general register, memory operand or immediate integer.
    case 'X': // any operand.
      Info.setAllowsRegister();
      Info.setAllowsMemory();
      break;
    case 'E': // immediate floating point.
    case 'F': // immediate floating point.
    case 'p': // address operand.
      break;
    case ',': // multiple alternative constraint.  Ignore comma.
      break;
    case '#': // Ignore as constraint.
      while (Name[1] && Name[1] != ',')
        Name++;
      break;
    case '?': // Disparage slightly code.
    case '!': // Disparage severely.
    case '*': // Ignore for choosing register preferences.
      break;  // Pass them.
    }

    Name++;
  }

  return true;
}

bool TargetInfo::validatePointerAuthKey(const llvm::APSInt &value) const {
  return false;
}

void TargetInfo::CheckFixedPointBits() const {
  // Check that the number of fractional and integral bits (and maybe sign) can
  // fit into the bits given for a fixed point type.
  assert(ShortAccumScale + getShortAccumIBits() + 1 <= ShortAccumWidth);
  assert(AccumScale + getAccumIBits() + 1 <= AccumWidth);
  assert(LongAccumScale + getLongAccumIBits() + 1 <= LongAccumWidth);
  assert(getUnsignedShortAccumScale() + getUnsignedShortAccumIBits() <=
         ShortAccumWidth);
  assert(getUnsignedAccumScale() + getUnsignedAccumIBits() <= AccumWidth);
  assert(getUnsignedLongAccumScale() + getUnsignedLongAccumIBits() <=
         LongAccumWidth);

  assert(getShortFractScale() + 1 <= ShortFractWidth);
  assert(getFractScale() + 1 <= FractWidth);
  assert(getLongFractScale() + 1 <= LongFractWidth);
  assert(getUnsignedShortFractScale() <= ShortFractWidth);
  assert(getUnsignedFractScale() <= FractWidth);
  assert(getUnsignedLongFractScale() <= LongFractWidth);

  // Each unsigned fract type has either the same number of fractional bits
  // as, or one more fractional bit than, its corresponding signed fract type.
  assert(getShortFractScale() == getUnsignedShortFractScale() ||
         getShortFractScale() == getUnsignedShortFractScale() - 1);
  assert(getFractScale() == getUnsignedFractScale() ||
         getFractScale() == getUnsignedFractScale() - 1);
  assert(getLongFractScale() == getUnsignedLongFractScale() ||
         getLongFractScale() == getUnsignedLongFractScale() - 1);

  // When arranged in order of increasing rank (see 6.3.1.3a), the number of
  // fractional bits is nondecreasing for each of the following sets of
  // fixed-point types:
  // - signed fract types
  // - unsigned fract types
  // - signed accum types
  // - unsigned accum types.
  assert(getLongFractScale() >= getFractScale() &&
         getFractScale() >= getShortFractScale());
  assert(getUnsignedLongFractScale() >= getUnsignedFractScale() &&
         getUnsignedFractScale() >= getUnsignedShortFractScale());
  assert(LongAccumScale >= AccumScale && AccumScale >= ShortAccumScale);
  assert(getUnsignedLongAccumScale() >= getUnsignedAccumScale() &&
         getUnsignedAccumScale() >= getUnsignedShortAccumScale());

  // When arranged in order of increasing rank (see 6.3.1.3a), the number of
  // integral bits is nondecreasing for each of the following sets of
  // fixed-point types:
  // - signed accum types
  // - unsigned accum types
  assert(getLongAccumIBits() >= getAccumIBits() &&
         getAccumIBits() >= getShortAccumIBits());
  assert(getUnsignedLongAccumIBits() >= getUnsignedAccumIBits() &&
         getUnsignedAccumIBits() >= getUnsignedShortAccumIBits());

  // Each signed accum type has at least as many integral bits as its
  // corresponding unsigned accum type.
  assert(getShortAccumIBits() >= getUnsignedShortAccumIBits());
  assert(getAccumIBits() >= getUnsignedAccumIBits());
  assert(getLongAccumIBits() >= getUnsignedLongAccumIBits());
}

void TargetInfo::copyAuxTarget(const TargetInfo *Aux) {
  auto *Target = static_cast<TransferrableTargetInfo*>(this);
  auto *Src = static_cast<const TransferrableTargetInfo*>(Aux);
  *Target = *Src;
}
