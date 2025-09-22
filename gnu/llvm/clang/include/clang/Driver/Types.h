//===--- Types.h - Input & Temporary Driver Types ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_TYPES_H
#define LLVM_CLANG_DRIVER_TYPES_H

#include "clang/Driver/Phases.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Option/ArgList.h"

namespace llvm {
class StringRef;
}
namespace clang {
namespace driver {
class Driver;
namespace types {
  enum ID {
    TY_INVALID,
#define TYPE(NAME, ID, PP_TYPE, TEMP_SUFFIX, ...) TY_##ID,
#include "clang/Driver/Types.def"
#undef TYPE
    TY_LAST
  };

  /// getTypeName - Return the name of the type for \p Id.
  const char *getTypeName(ID Id);

  /// getPreprocessedType - Get the ID of the type for this input when
  /// it has been preprocessed, or INVALID if this input is not
  /// preprocessed.
  ID getPreprocessedType(ID Id);

  /// getPrecompiledType - Get the ID of the type for this input when
  /// it has been precompiled, or INVALID if this input is not
  /// precompiled.
  ID getPrecompiledType(ID Id);

  /// getTypeTempSuffix - Return the suffix to use when creating a
  /// temp file of this type, or null if unspecified.
  const char *getTypeTempSuffix(ID Id, bool CLStyle = false);

  /// onlyPrecompileType - Should this type only be precompiled.
  bool onlyPrecompileType(ID Id);

  /// canTypeBeUserSpecified - Can this type be specified on the
  /// command line (by the type name); this is used when forwarding
  /// commands to gcc.
  bool canTypeBeUserSpecified(ID Id);

  /// appendSuffixForType - When generating outputs of this type,
  /// should the suffix be appended (instead of replacing the existing
  /// suffix).
  bool appendSuffixForType(ID Id);

  /// canLipoType - Is this type acceptable as the output of a
  /// universal build (currently, just the Nothing, Image, and Object
  /// types).
  bool canLipoType(ID Id);

  /// isAcceptedByClang - Can clang handle this input type.
  bool isAcceptedByClang(ID Id);

  /// isAcceptedByFlang - Can flang handle this input type.
  bool isAcceptedByFlang(ID Id);

  /// isDerivedFromC - Is the input derived from C.
  ///
  /// That is, does the lexer follow the rules of
  /// TokenConcatenation::AvoidConcat. If this is the case, the preprocessor may
  /// add and remove whitespace between tokens. Used to determine whether the
  /// input can be processed by -fminimize-whitespace.
  bool isDerivedFromC(ID Id);

  /// isCXX - Is this a "C++" input (C++ and Obj-C++ sources and headers).
  bool isCXX(ID Id);

  /// Is this LLVM IR.
  bool isLLVMIR(ID Id);

  /// isCuda - Is this a CUDA input.
  bool isCuda(ID Id);

  /// isHIP - Is this a HIP input.
  bool isHIP(ID Id);

  /// isObjC - Is this an "ObjC" input (Obj-C and Obj-C++ sources and headers).
  bool isObjC(ID Id);

  /// isOpenCL - Is this an "OpenCL" input.
  bool isOpenCL(ID Id);

  /// isHLSL - Is this an HLSL input.
  bool isHLSL(ID Id);

  /// isSrcFile - Is this a source file, i.e. something that still has to be
  /// preprocessed. The logic behind this is the same that decides if the first
  /// compilation phase is a preprocessing one.
  bool isSrcFile(ID Id);

  /// lookupTypeForExtension - Lookup the type to use for the file
  /// extension \p Ext.
  ID lookupTypeForExtension(llvm::StringRef Ext);

  /// lookupTypeForTypSpecifier - Lookup the type to use for a user
  /// specified type name.
  ID lookupTypeForTypeSpecifier(const char *Name);

  /// getCompilationPhases - Get the list of compilation phases ('Phases') to be
  /// done for type 'Id' up until including LastPhase.
  llvm::SmallVector<phases::ID, phases::MaxNumberOfPhases>
  getCompilationPhases(ID Id, phases::ID LastPhase = phases::IfsMerge);
  llvm::SmallVector<phases::ID, phases::MaxNumberOfPhases>
  getCompilationPhases(const clang::driver::Driver &Driver,
                       llvm::opt::DerivedArgList &DAL, ID Id);

  /// lookupCXXTypeForCType - Lookup CXX input type that corresponds to given
  /// C type (used for clang++ emulation of g++ behaviour)
  ID lookupCXXTypeForCType(ID Id);

  /// Lookup header file input type that corresponds to given
  /// source file type (used for clang-cl emulation of \Yc).
  ID lookupHeaderTypeForSourceType(ID Id);

} // end namespace types
} // end namespace driver
} // end namespace clang

#endif
