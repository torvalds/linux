//==-- SwiftCallingConv.h - Swift ABI lowering ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines constants and types related to Swift ABI lowering. The same ABI
// lowering applies to both sync and async functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CODEGEN_SWIFTCALLINGCONV_H
#define LLVM_CLANG_CODEGEN_SWIFTCALLINGCONV_H

#include "clang/AST/CanonicalType.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Type.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>

namespace llvm {
  class IntegerType;
  class Type;
  class StructType;
  class VectorType;
}

namespace clang {
class FieldDecl;
class ASTRecordLayout;

namespace CodeGen {
class ABIArgInfo;
class CodeGenModule;
class CGFunctionInfo;

namespace swiftcall {

class SwiftAggLowering {
  CodeGenModule &CGM;

  struct StorageEntry {
    CharUnits Begin;
    CharUnits End;
    llvm::Type *Type;

    CharUnits getWidth() const {
      return End - Begin;
    }
  };
  SmallVector<StorageEntry, 4> Entries;
  bool Finished = false;

public:
  SwiftAggLowering(CodeGenModule &CGM) : CGM(CGM) {}

  void addOpaqueData(CharUnits begin, CharUnits end) {
    addEntry(nullptr, begin, end);
  }

  void addTypedData(QualType type, CharUnits begin);
  void addTypedData(const RecordDecl *record, CharUnits begin);
  void addTypedData(const RecordDecl *record, CharUnits begin,
                    const ASTRecordLayout &layout);
  void addTypedData(llvm::Type *type, CharUnits begin);
  void addTypedData(llvm::Type *type, CharUnits begin, CharUnits end);

  void finish();

  /// Does this lowering require passing any data?
  bool empty() const {
    assert(Finished && "didn't finish lowering before calling empty()");
    return Entries.empty();
  }

  /// According to the target Swift ABI, should a value with this lowering
  /// be passed indirectly?
  ///
  /// Note that this decision is based purely on the data layout of the
  /// value and does not consider whether the type is address-only,
  /// must be passed indirectly to match a function abstraction pattern, or
  /// anything else that is expected to be handled by high-level lowering.
  ///
  /// \param asReturnValue - if true, answer whether it should be passed
  ///   indirectly as a return value; if false, answer whether it should be
  ///   passed indirectly as an argument
  bool shouldPassIndirectly(bool asReturnValue) const;

  using EnumerationCallback =
    llvm::function_ref<void(CharUnits offset, CharUnits end, llvm::Type *type)>;

  /// Enumerate the expanded components of this type.
  ///
  /// The component types will always be legal vector, floating-point,
  /// integer, or pointer types.
  void enumerateComponents(EnumerationCallback callback) const;

  /// Return the types for a coerce-and-expand operation.
  ///
  /// The first type matches the memory layout of the data that's been
  /// added to this structure, including explicit [N x i8] arrays for any
  /// internal padding.
  ///
  /// The second type removes any internal padding members and, if only
  /// one element remains, is simply that element type.
  std::pair<llvm::StructType*, llvm::Type*> getCoerceAndExpandTypes() const;

private:
  void addBitFieldData(const FieldDecl *field, CharUnits begin,
                       uint64_t bitOffset);
  void addLegalTypedData(llvm::Type *type, CharUnits begin, CharUnits end);
  void addEntry(llvm::Type *type, CharUnits begin, CharUnits end);
  void splitVectorEntry(unsigned index);
  static bool shouldMergeEntries(const StorageEntry &first,
                                 const StorageEntry &second,
                                 CharUnits chunkSize);
};

/// Should an aggregate which expands to the given type sequence
/// be passed/returned indirectly under swiftcall?
bool shouldPassIndirectly(CodeGenModule &CGM,
                          ArrayRef<llvm::Type*> types,
                          bool asReturnValue);

/// Return the maximum voluntary integer size for the current target.
CharUnits getMaximumVoluntaryIntegerSize(CodeGenModule &CGM);

/// Return the Swift CC's notion of the natural alignment of a type.
CharUnits getNaturalAlignment(CodeGenModule &CGM, llvm::Type *type);

/// Is the given integer type "legal" for Swift's perspective on the
/// current platform?
bool isLegalIntegerType(CodeGenModule &CGM, llvm::IntegerType *type);

/// Is the given vector type "legal" for Swift's perspective on the
/// current platform?
bool isLegalVectorType(CodeGenModule &CGM, CharUnits vectorSize,
                       llvm::VectorType *vectorTy);
bool isLegalVectorType(CodeGenModule &CGM, CharUnits vectorSize,
                       llvm::Type *eltTy, unsigned numElts);

/// Minimally split a legal vector type.
std::pair<llvm::Type*, unsigned>
splitLegalVectorType(CodeGenModule &CGM, CharUnits vectorSize,
                     llvm::VectorType *vectorTy);

/// Turn a vector type in a sequence of legal component vector types.
///
/// The caller may assume that the sum of the data sizes of the resulting
/// types will equal the data size of the vector type.
void legalizeVectorType(CodeGenModule &CGM, CharUnits vectorSize,
                        llvm::VectorType *vectorTy,
                        llvm::SmallVectorImpl<llvm::Type*> &types);

/// Is the given record type required to be passed and returned indirectly
/// because of language restrictions?
///
/// This considers *only* mandatory indirectness due to language restrictions,
/// such as C++'s non-trivially-copyable types and Objective-C's __weak
/// references.  A record for which this returns true may still be passed
/// indirectly for other reasons, such as being too large to fit in a
/// reasonable number of registers.
bool mustPassRecordIndirectly(CodeGenModule &CGM, const RecordDecl *record);

/// Classify the rules for how to return a particular type.
ABIArgInfo classifyReturnType(CodeGenModule &CGM, CanQualType type);

/// Classify the rules for how to pass a particular type.
ABIArgInfo classifyArgumentType(CodeGenModule &CGM, CanQualType type);

/// Compute the ABI information of a swiftcall function.  This is a
/// private interface for Clang.
void computeABIInfo(CodeGenModule &CGM, CGFunctionInfo &FI);

/// Is swifterror lowered to a register by the target ABI?
bool isSwiftErrorLoweredInRegister(CodeGenModule &CGM);

} // end namespace swiftcall
} // end namespace CodeGen
} // end namespace clang

#endif
