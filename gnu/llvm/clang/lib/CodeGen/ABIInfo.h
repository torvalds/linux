//===----- ABIInfo.h - ABI information access & encapsulation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_ABIINFO_H
#define LLVM_CLANG_LIB_CODEGEN_ABIINFO_H

#include "clang/AST/Attr.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Type.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Type.h"

namespace llvm {
class Value;
class LLVMContext;
class DataLayout;
class Type;
} // namespace llvm

namespace clang {
class ASTContext;
class CodeGenOptions;
class TargetInfo;

namespace CodeGen {
class ABIArgInfo;
class Address;
class CGCXXABI;
class CGFunctionInfo;
class CodeGenFunction;
class CodeGenTypes;
class RValue;
class AggValueSlot;

// FIXME: All of this stuff should be part of the target interface
// somehow. It is currently here because it is not clear how to factor
// the targets to support this, since the Targets currently live in a
// layer below types n'stuff.

/// ABIInfo - Target specific hooks for defining how a type should be
/// passed or returned from functions.
class ABIInfo {
protected:
  CodeGen::CodeGenTypes &CGT;
  llvm::CallingConv::ID RuntimeCC;

public:
  ABIInfo(CodeGen::CodeGenTypes &cgt)
      : CGT(cgt), RuntimeCC(llvm::CallingConv::C) {}

  virtual ~ABIInfo();

  virtual bool allowBFloatArgsAndRet() const { return false; }

  CodeGen::CGCXXABI &getCXXABI() const;
  ASTContext &getContext() const;
  llvm::LLVMContext &getVMContext() const;
  const llvm::DataLayout &getDataLayout() const;
  const TargetInfo &getTarget() const;
  const CodeGenOptions &getCodeGenOpts() const;

  /// Return the calling convention to use for system runtime
  /// functions.
  llvm::CallingConv::ID getRuntimeCC() const { return RuntimeCC; }

  virtual void computeInfo(CodeGen::CGFunctionInfo &FI) const = 0;

  /// EmitVAArg - Emit the target dependent code to load a value of
  /// \arg Ty from the va_list pointed to by \arg VAListAddr.

  // FIXME: This is a gaping layering violation if we wanted to drop
  // the ABI information any lower than CodeGen. Of course, for
  // VAArg handling it has to be at this level; there is no way to
  // abstract this out.
  virtual RValue EmitVAArg(CodeGen::CodeGenFunction &CGF,
                           CodeGen::Address VAListAddr, QualType Ty,
                           AggValueSlot Slot) const = 0;

  bool isAndroid() const;
  bool isOHOSFamily() const;

  /// Emit the target dependent code to load a value of
  /// \arg Ty from the \c __builtin_ms_va_list pointed to by \arg VAListAddr.
  virtual RValue EmitMSVAArg(CodeGen::CodeGenFunction &CGF,
                             CodeGen::Address VAListAddr, QualType Ty,
                             AggValueSlot Slot) const;

  virtual bool isHomogeneousAggregateBaseType(QualType Ty) const;

  virtual bool isHomogeneousAggregateSmallEnough(const Type *Base,
                                                 uint64_t Members) const;
  virtual bool isZeroLengthBitfieldPermittedInHomogeneousAggregate() const;

  /// isHomogeneousAggregate - Return true if a type is an ELFv2 homogeneous
  /// aggregate.  Base is set to the base element type, and Members is set
  /// to the number of base elements.
  bool isHomogeneousAggregate(QualType Ty, const Type *&Base,
                              uint64_t &Members) const;

  // Implement the Type::IsPromotableIntegerType for ABI specific needs. The
  // only difference is that this considers bit-precise integer types as well.
  bool isPromotableIntegerTypeForABI(QualType Ty) const;

  /// A convenience method to return an indirect ABIArgInfo with an
  /// expected alignment equal to the ABI alignment of the given type.
  CodeGen::ABIArgInfo
  getNaturalAlignIndirect(QualType Ty, bool ByVal = true, bool Realign = false,
                          llvm::Type *Padding = nullptr) const;

  CodeGen::ABIArgInfo getNaturalAlignIndirectInReg(QualType Ty,
                                                   bool Realign = false) const;

  virtual void appendAttributeMangling(TargetAttr *Attr,
                                       raw_ostream &Out) const;
  virtual void appendAttributeMangling(TargetVersionAttr *Attr,
                                       raw_ostream &Out) const;
  virtual void appendAttributeMangling(TargetClonesAttr *Attr, unsigned Index,
                                       raw_ostream &Out) const;
  virtual void appendAttributeMangling(StringRef AttrStr,
                                       raw_ostream &Out) const;
};

/// Target specific hooks for defining how a type should be passed or returned
/// from functions with one of the Swift calling conventions.
class SwiftABIInfo {
protected:
  CodeGenTypes &CGT;
  bool SwiftErrorInRegister;

  bool occupiesMoreThan(ArrayRef<llvm::Type *> scalarTypes,
                        unsigned maxAllRegisters) const;

public:
  SwiftABIInfo(CodeGen::CodeGenTypes &CGT, bool SwiftErrorInRegister)
      : CGT(CGT), SwiftErrorInRegister(SwiftErrorInRegister) {}

  virtual ~SwiftABIInfo();

  /// Returns true if an aggregate which expands to the given type sequence
  /// should be passed / returned indirectly.
  virtual bool shouldPassIndirectly(ArrayRef<llvm::Type *> ComponentTys,
                                    bool AsReturnValue) const;

  /// Returns true if the given vector type is legal from Swift's calling
  /// convention perspective.
  virtual bool isLegalVectorType(CharUnits VectorSize, llvm::Type *EltTy,
                                 unsigned NumElts) const;

  /// Returns true if swifterror is lowered to a register by the target ABI.
  bool isSwiftErrorInRegister() const { return SwiftErrorInRegister; };
};
} // end namespace CodeGen
} // end namespace clang

#endif
