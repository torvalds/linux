//===-- llvm/Target/TargetIntrinsicInfo.h - Instruction Info ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the target intrinsic instructions to the code generator.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TARGETINTRINSICINFO_H
#define LLVM_TARGET_TARGETINTRINSICINFO_H

#include "llvm/ADT/StringRef.h"
#include <string>

namespace llvm {

class Function;
class Module;
class Type;

//---------------------------------------------------------------------------
///
/// TargetIntrinsicInfo - Interface to description of machine instruction set
///
class TargetIntrinsicInfo {
  TargetIntrinsicInfo(const TargetIntrinsicInfo &) = delete;
  void operator=(const TargetIntrinsicInfo &) = delete;
public:
  TargetIntrinsicInfo();
  virtual ~TargetIntrinsicInfo();

  /// Return the name of a target intrinsic, e.g. "llvm.bfin.ssync".
  /// The Tys and numTys parameters are for intrinsics with overloaded types
  /// (e.g., those using iAny or fAny). For a declaration for an overloaded
  /// intrinsic, Tys should point to an array of numTys pointers to Type,
  /// and must provide exactly one type for each overloaded type in the
  /// intrinsic.
  virtual std::string getName(unsigned IID, Type **Tys = nullptr,
                              unsigned numTys = 0) const = 0;

  /// Look up target intrinsic by name. Return intrinsic ID or 0 for unknown
  /// names.
  virtual unsigned lookupName(const char *Name, unsigned Len) const =0;

  unsigned lookupName(StringRef Name) const {
    return lookupName(Name.data(), Name.size());
  }

  /// Return the target intrinsic ID of a function, or 0.
  virtual unsigned getIntrinsicID(const Function *F) const;

  /// Returns true if the intrinsic can be overloaded.
  virtual bool isOverloaded(unsigned IID) const = 0;

  /// Create or insert an LLVM Function declaration for an intrinsic,
  /// and return it. The Tys and numTys are for intrinsics with overloaded
  /// types. See above for more information.
  virtual Function *getDeclaration(Module *M, unsigned ID, Type **Tys = nullptr,
                                   unsigned numTys = 0) const = 0;
};

} // End llvm namespace

#endif
