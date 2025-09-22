//===--- Types.cpp - Driver input & temporary type information ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Types.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/Arg.h"
#include <cassert>
#include <cstring>

using namespace clang::driver;
using namespace clang::driver::types;

struct TypeInfo {
  const char *Name;
  const char *TempSuffix;
  ID PreprocessedType;
  class PhasesBitSet {
    unsigned Bits = 0;

  public:
    constexpr PhasesBitSet(std::initializer_list<phases::ID> Phases) {
      for (auto Id : Phases)
        Bits |= 1 << Id;
    }
    bool contains(phases::ID Id) const { return Bits & (1 << Id); }
  } Phases;
};

static constexpr TypeInfo TypeInfos[] = {
#define TYPE(NAME, ID, PP_TYPE, TEMP_SUFFIX, ...) \
  { NAME, TEMP_SUFFIX, TY_##PP_TYPE, { __VA_ARGS__ }, },
#include "clang/Driver/Types.def"
#undef TYPE
};
static const unsigned numTypes = std::size(TypeInfos);

static const TypeInfo &getInfo(unsigned id) {
  assert(id > 0 && id - 1 < numTypes && "Invalid Type ID.");
  return TypeInfos[id - 1];
}

const char *types::getTypeName(ID Id) {
  return getInfo(Id).Name;
}

types::ID types::getPreprocessedType(ID Id) {
  ID PPT = getInfo(Id).PreprocessedType;
  assert((getInfo(Id).Phases.contains(phases::Preprocess) !=
          (PPT == TY_INVALID)) &&
         "Unexpected Preprocess Type.");
  return PPT;
}

static bool isPreprocessedModuleType(ID Id) {
  return Id == TY_CXXModule || Id == TY_PP_CXXModule;
}

static bool isPreprocessedHeaderUnitType(ID Id) {
  return Id == TY_CXXSHeader || Id == TY_CXXUHeader || Id == TY_CXXHUHeader ||
         Id == TY_PP_CXXHeaderUnit;
}

types::ID types::getPrecompiledType(ID Id) {
  if (isPreprocessedModuleType(Id))
    return TY_ModuleFile;
  if (isPreprocessedHeaderUnitType(Id))
    return TY_HeaderUnit;
  if (onlyPrecompileType(Id))
    return TY_PCH;
  return TY_INVALID;
}

const char *types::getTypeTempSuffix(ID Id, bool CLStyle) {
  if (CLStyle) {
    switch (Id) {
    case TY_Object:
    case TY_LTO_BC:
      return "obj";
    case TY_Image:
      return "exe";
    case TY_PP_Asm:
      return "asm";
    default:
      break;
    }
  }
  return getInfo(Id).TempSuffix;
}

bool types::onlyPrecompileType(ID Id) {
  return getInfo(Id).Phases.contains(phases::Precompile) &&
         !isPreprocessedModuleType(Id);
}

bool types::canTypeBeUserSpecified(ID Id) {
  static const clang::driver::types::ID kStaticLangageTypes[] = {
      TY_CUDA_DEVICE,   TY_HIP_DEVICE,    TY_PP_CHeader,
      TY_PP_ObjCHeader, TY_PP_CXXHeader,  TY_PP_ObjCXXHeader,
      TY_PP_CXXModule,  TY_LTO_IR,        TY_LTO_BC,
      TY_Plist,         TY_RewrittenObjC, TY_RewrittenLegacyObjC,
      TY_Remap,         TY_PCH,           TY_Object,
      TY_Image,         TY_dSYM,          TY_Dependencies,
      TY_CUDA_FATBIN,   TY_HIP_FATBIN};
  return !llvm::is_contained(kStaticLangageTypes, Id);
}

bool types::appendSuffixForType(ID Id) {
  return Id == TY_PCH || Id == TY_dSYM || Id == TY_CUDA_FATBIN ||
         Id == TY_HIP_FATBIN;
}

bool types::canLipoType(ID Id) {
  return (Id == TY_Nothing ||
          Id == TY_Image ||
          Id == TY_Object ||
          Id == TY_LTO_BC);
}

bool types::isAcceptedByClang(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_Asm:
  case TY_C: case TY_PP_C:
  case TY_CL: case TY_PP_CL: case TY_CLCXX: case TY_PP_CLCXX:
  case TY_CUDA: case TY_PP_CUDA:
  case TY_CUDA_DEVICE:
  case TY_HIP:
  case TY_PP_HIP:
  case TY_HIP_DEVICE:
  case TY_ObjC: case TY_PP_ObjC: case TY_PP_ObjC_Alias:
  case TY_CXX: case TY_PP_CXX:
  case TY_ObjCXX: case TY_PP_ObjCXX: case TY_PP_ObjCXX_Alias:
  case TY_CHeader: case TY_PP_CHeader:
  case TY_CLHeader:
  case TY_ObjCHeader: case TY_PP_ObjCHeader:
  case TY_CXXHeader: case TY_PP_CXXHeader:
  case TY_CXXSHeader:
  case TY_CXXUHeader:
  case TY_CXXHUHeader:
  case TY_PP_CXXHeaderUnit:
  case TY_ObjCXXHeader: case TY_PP_ObjCXXHeader:
  case TY_CXXModule: case TY_PP_CXXModule:
  case TY_AST: case TY_ModuleFile: case TY_PCH:
  case TY_LLVM_IR: case TY_LLVM_BC:
  case TY_API_INFO:
    return true;
  }
}

bool types::isAcceptedByFlang(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_Fortran:
  case TY_PP_Fortran:
    return true;
  case TY_LLVM_IR:
  case TY_LLVM_BC:
    return true;
  }
}

bool types::isDerivedFromC(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_PP_C:
  case TY_C:
  case TY_CL:
  case TY_PP_CL:
  case TY_CLCXX:
  case TY_PP_CLCXX:
  case TY_PP_CUDA:
  case TY_CUDA:
  case TY_CUDA_DEVICE:
  case TY_PP_HIP:
  case TY_HIP:
  case TY_HIP_DEVICE:
  case TY_PP_ObjC:
  case TY_PP_ObjC_Alias:
  case TY_ObjC:
  case TY_PP_CXX:
  case TY_CXX:
  case TY_PP_ObjCXX:
  case TY_PP_ObjCXX_Alias:
  case TY_ObjCXX:
  case TY_RenderScript:
  case TY_PP_CHeader:
  case TY_CHeader:
  case TY_CLHeader:
  case TY_PP_ObjCHeader:
  case TY_ObjCHeader:
  case TY_PP_CXXHeader:
  case TY_CXXHeader:
  case TY_PP_ObjCXXHeader:
  case TY_ObjCXXHeader:
  case TY_CXXModule:
  case TY_PP_CXXModule:
    return true;
  }
}

bool types::isObjC(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_ObjC: case TY_PP_ObjC: case TY_PP_ObjC_Alias:
  case TY_ObjCXX: case TY_PP_ObjCXX:
  case TY_ObjCHeader: case TY_PP_ObjCHeader:
  case TY_ObjCXXHeader: case TY_PP_ObjCXXHeader: case TY_PP_ObjCXX_Alias:
    return true;
  }
}

bool types::isOpenCL(ID Id) { return Id == TY_CL || Id == TY_CLCXX; }

bool types::isCXX(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_CXX: case TY_PP_CXX:
  case TY_ObjCXX: case TY_PP_ObjCXX: case TY_PP_ObjCXX_Alias:
  case TY_CXXHeader: case TY_PP_CXXHeader:
  case TY_CXXSHeader:
  case TY_CXXUHeader:
  case TY_CXXHUHeader:
  case TY_PP_CXXHeaderUnit:
  case TY_ObjCXXHeader: case TY_PP_ObjCXXHeader:
  case TY_CXXModule:
  case TY_PP_CXXModule:
  case TY_ModuleFile:
  case TY_PP_CLCXX:
  case TY_CUDA: case TY_PP_CUDA: case TY_CUDA_DEVICE:
  case TY_HIP:
  case TY_PP_HIP:
  case TY_HIP_DEVICE:
    return true;
  }
}

bool types::isLLVMIR(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_LLVM_IR:
  case TY_LLVM_BC:
  case TY_LTO_IR:
  case TY_LTO_BC:
    return true;
  }
}

bool types::isCuda(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_CUDA:
  case TY_PP_CUDA:
  case TY_CUDA_DEVICE:
    return true;
  }
}

bool types::isHIP(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_HIP:
  case TY_PP_HIP:
  case TY_HIP_DEVICE:
    return true;
  }
}

bool types::isHLSL(ID Id) { return Id == TY_HLSL; }

bool types::isSrcFile(ID Id) {
  return Id != TY_Object && getPreprocessedType(Id) != TY_INVALID;
}

types::ID types::lookupTypeForExtension(llvm::StringRef Ext) {
  return llvm::StringSwitch<types::ID>(Ext)
      .Case("c", TY_C)
      .Case("C", TY_CXX)
      .Case("F", TY_Fortran)
      .Case("f", TY_PP_Fortran)
      .Case("h", TY_CHeader)
      .Case("H", TY_CXXHeader)
      .Case("i", TY_PP_C)
      .Case("m", TY_ObjC)
      .Case("M", TY_ObjCXX)
      .Case("o", TY_Object)
      .Case("S", TY_Asm)
      .Case("s", TY_PP_Asm)
      .Case("bc", TY_LLVM_BC)
      .Case("cc", TY_CXX)
      .Case("CC", TY_CXX)
      .Case("cl", TY_CL)
      .Case("cli", TY_PP_CL)
      .Case("clcpp", TY_CLCXX)
      .Case("clii", TY_PP_CLCXX)
      .Case("cp", TY_CXX)
      .Case("cu", TY_CUDA)
      .Case("hh", TY_CXXHeader)
      .Case("ii", TY_PP_CXX)
      .Case("ll", TY_LLVM_IR)
      .Case("mi", TY_PP_ObjC)
      .Case("mm", TY_ObjCXX)
      .Case("rs", TY_RenderScript)
      .Case("adb", TY_Ada)
      .Case("ads", TY_Ada)
      .Case("asm", TY_PP_Asm)
      .Case("ast", TY_AST)
      .Case("ccm", TY_CXXModule)
      .Case("cpp", TY_CXX)
      .Case("CPP", TY_CXX)
      .Case("c++", TY_CXX)
      .Case("C++", TY_CXX)
      .Case("cui", TY_PP_CUDA)
      .Case("cxx", TY_CXX)
      .Case("CXX", TY_CXX)
      .Case("F03", TY_Fortran)
      .Case("f03", TY_PP_Fortran)
      .Case("F08", TY_Fortran)
      .Case("f08", TY_PP_Fortran)
      .Case("F90", TY_Fortran)
      .Case("f90", TY_PP_Fortran)
      .Case("F95", TY_Fortran)
      .Case("f95", TY_PP_Fortran)
      .Case("for", TY_PP_Fortran)
      .Case("FOR", TY_PP_Fortran)
      .Case("fpp", TY_Fortran)
      .Case("FPP", TY_Fortran)
      .Case("gch", TY_PCH)
      .Case("hip", TY_HIP)
      .Case("hipi", TY_PP_HIP)
      .Case("hpp", TY_CXXHeader)
      .Case("hxx", TY_CXXHeader)
      .Case("iim", TY_PP_CXXModule)
      .Case("iih", TY_PP_CXXHeaderUnit)
      .Case("lib", TY_Object)
      .Case("mii", TY_PP_ObjCXX)
      .Case("obj", TY_Object)
      .Case("ifs", TY_IFS)
      .Case("pch", TY_PCH)
      .Case("pcm", TY_ModuleFile)
      .Case("c++m", TY_CXXModule)
      .Case("cppm", TY_CXXModule)
      .Case("cxxm", TY_CXXModule)
      .Case("hlsl", TY_HLSL)
      .Default(TY_INVALID);
}

types::ID types::lookupTypeForTypeSpecifier(const char *Name) {
  for (unsigned i=0; i<numTypes; ++i) {
    types::ID Id = (types::ID) (i + 1);
    if (canTypeBeUserSpecified(Id) &&
        strcmp(Name, getInfo(Id).Name) == 0)
      return Id;
  }
  // Accept "cu" as an alias for "cuda" for NVCC compatibility
  if (strcmp(Name, "cu") == 0) {
    return types::TY_CUDA;
  }
  return TY_INVALID;
}

llvm::SmallVector<phases::ID, phases::MaxNumberOfPhases>
types::getCompilationPhases(ID Id, phases::ID LastPhase) {
  llvm::SmallVector<phases::ID, phases::MaxNumberOfPhases> P;
  const auto &Info = getInfo(Id);
  for (int I = 0; I <= LastPhase; ++I)
    if (Info.Phases.contains(static_cast<phases::ID>(I)))
      P.push_back(static_cast<phases::ID>(I));
  assert(P.size() <= phases::MaxNumberOfPhases && "Too many phases in list");
  return P;
}

llvm::SmallVector<phases::ID, phases::MaxNumberOfPhases>
types::getCompilationPhases(const clang::driver::Driver &Driver,
                            llvm::opt::DerivedArgList &DAL, ID Id) {
  return types::getCompilationPhases(Id, Driver.getFinalPhase(DAL));
}

ID types::lookupCXXTypeForCType(ID Id) {
  switch (Id) {
  default:
    return Id;

  case types::TY_C:
    return types::TY_CXX;
  case types::TY_PP_C:
    return types::TY_PP_CXX;
  case types::TY_CHeader:
    return types::TY_CXXHeader;
  case types::TY_PP_CHeader:
    return types::TY_PP_CXXHeader;
  }
}

ID types::lookupHeaderTypeForSourceType(ID Id) {
  switch (Id) {
  default:
    return Id;

  // FIXME: Handle preprocessed input types.
  case types::TY_C:
    return types::TY_CHeader;
  case types::TY_CXX:
  case types::TY_CXXModule:
    return types::TY_CXXHeader;
  case types::TY_ObjC:
    return types::TY_ObjCHeader;
  case types::TY_ObjCXX:
    return types::TY_ObjCXXHeader;
  case types::TY_CL:
  case types::TY_CLCXX:
    return types::TY_CLHeader;
  }
}
