//==-- CGFunctionInfo.h - Representation of function argument/return types -==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines CGFunctionInfo and associated types used in representing the
// LLVM source types and ABI-coerced types for function arguments and
// return values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CODEGEN_CGFUNCTIONINFO_H
#define LLVM_CLANG_CODEGEN_CGFUNCTIONINFO_H

#include "clang/AST/CanonicalType.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>

namespace clang {
namespace CodeGen {

/// ABIArgInfo - Helper class to encapsulate information about how a
/// specific C type should be passed to or returned from a function.
class ABIArgInfo {
public:
  enum Kind : uint8_t {
    /// Direct - Pass the argument directly using the normal converted LLVM
    /// type, or by coercing to another specified type stored in
    /// 'CoerceToType').  If an offset is specified (in UIntData), then the
    /// argument passed is offset by some number of bytes in the memory
    /// representation. A dummy argument is emitted before the real argument
    /// if the specified type stored in "PaddingType" is not zero.
    Direct,

    /// Extend - Valid only for integer argument types. Same as 'direct'
    /// but also emit a zero/sign extension attribute.
    Extend,

    /// Indirect - Pass the argument indirectly via a hidden pointer with the
    /// specified alignment (0 indicates default alignment) and address space.
    Indirect,

    /// IndirectAliased - Similar to Indirect, but the pointer may be to an
    /// object that is otherwise referenced.  The object is known to not be
    /// modified through any other references for the duration of the call, and
    /// the callee must not itself modify the object.  Because C allows
    /// parameter variables to be modified and guarantees that they have unique
    /// addresses, the callee must defensively copy the object into a local
    /// variable if it might be modified or its address might be compared.
    /// Since those are uncommon, in principle this convention allows programs
    /// to avoid copies in more situations.  However, it may introduce *extra*
    /// copies if the callee fails to prove that a copy is unnecessary and the
    /// caller naturally produces an unaliased object for the argument.
    IndirectAliased,

    /// Ignore - Ignore the argument (treat as void). Useful for void and
    /// empty structs.
    Ignore,

    /// Expand - Only valid for aggregate argument types. The structure should
    /// be expanded into consecutive arguments for its constituent fields.
    /// Currently expand is only allowed on structures whose fields
    /// are all scalar types or are themselves expandable types.
    Expand,

    /// CoerceAndExpand - Only valid for aggregate argument types. The
    /// structure should be expanded into consecutive arguments corresponding
    /// to the non-array elements of the type stored in CoerceToType.
    /// Array elements in the type are assumed to be padding and skipped.
    CoerceAndExpand,

    /// InAlloca - Pass the argument directly using the LLVM inalloca attribute.
    /// This is similar to indirect with byval, except it only applies to
    /// arguments stored in memory and forbids any implicit copies.  When
    /// applied to a return type, it means the value is returned indirectly via
    /// an implicit sret parameter stored in the argument struct.
    InAlloca,
    KindFirst = Direct,
    KindLast = InAlloca
  };

private:
  llvm::Type *TypeData; // canHaveCoerceToType()
  union {
    llvm::Type *PaddingType; // canHavePaddingType()
    llvm::Type *UnpaddedCoerceAndExpandType; // isCoerceAndExpand()
  };
  struct DirectAttrInfo {
    unsigned Offset;
    unsigned Align;
  };
  struct IndirectAttrInfo {
    unsigned Align;
    unsigned AddrSpace;
  };
  union {
    DirectAttrInfo DirectAttr;     // isDirect() || isExtend()
    IndirectAttrInfo IndirectAttr; // isIndirect()
    unsigned AllocaFieldIndex; // isInAlloca()
  };
  Kind TheKind;
  bool PaddingInReg : 1;
  bool InAllocaSRet : 1;    // isInAlloca()
  bool InAllocaIndirect : 1;// isInAlloca()
  bool IndirectByVal : 1;   // isIndirect()
  bool IndirectRealign : 1; // isIndirect()
  bool SRetAfterThis : 1;   // isIndirect()
  bool InReg : 1;           // isDirect() || isExtend() || isIndirect()
  bool CanBeFlattened: 1;   // isDirect()
  bool SignExt : 1;         // isExtend()

  bool canHavePaddingType() const {
    return isDirect() || isExtend() || isIndirect() || isIndirectAliased() ||
           isExpand();
  }
  void setPaddingType(llvm::Type *T) {
    assert(canHavePaddingType());
    PaddingType = T;
  }

  void setUnpaddedCoerceToType(llvm::Type *T) {
    assert(isCoerceAndExpand());
    UnpaddedCoerceAndExpandType = T;
  }

public:
  ABIArgInfo(Kind K = Direct)
      : TypeData(nullptr), PaddingType(nullptr), DirectAttr{0, 0}, TheKind(K),
        PaddingInReg(false), InAllocaSRet(false),
        InAllocaIndirect(false), IndirectByVal(false), IndirectRealign(false),
        SRetAfterThis(false), InReg(false), CanBeFlattened(false),
        SignExt(false) {}

  static ABIArgInfo getDirect(llvm::Type *T = nullptr, unsigned Offset = 0,
                              llvm::Type *Padding = nullptr,
                              bool CanBeFlattened = true, unsigned Align = 0) {
    auto AI = ABIArgInfo(Direct);
    AI.setCoerceToType(T);
    AI.setPaddingType(Padding);
    AI.setDirectOffset(Offset);
    AI.setDirectAlign(Align);
    AI.setCanBeFlattened(CanBeFlattened);
    return AI;
  }
  static ABIArgInfo getDirectInReg(llvm::Type *T = nullptr) {
    auto AI = getDirect(T);
    AI.setInReg(true);
    return AI;
  }

  static ABIArgInfo getSignExtend(QualType Ty, llvm::Type *T = nullptr) {
    assert(Ty->isIntegralOrEnumerationType() && "Unexpected QualType");
    auto AI = ABIArgInfo(Extend);
    AI.setCoerceToType(T);
    AI.setPaddingType(nullptr);
    AI.setDirectOffset(0);
    AI.setDirectAlign(0);
    AI.setSignExt(true);
    return AI;
  }

  static ABIArgInfo getZeroExtend(QualType Ty, llvm::Type *T = nullptr) {
    assert(Ty->isIntegralOrEnumerationType() && "Unexpected QualType");
    auto AI = ABIArgInfo(Extend);
    AI.setCoerceToType(T);
    AI.setPaddingType(nullptr);
    AI.setDirectOffset(0);
    AI.setDirectAlign(0);
    AI.setSignExt(false);
    return AI;
  }

  // ABIArgInfo will record the argument as being extended based on the sign
  // of its type.
  static ABIArgInfo getExtend(QualType Ty, llvm::Type *T = nullptr) {
    assert(Ty->isIntegralOrEnumerationType() && "Unexpected QualType");
    if (Ty->hasSignedIntegerRepresentation())
      return getSignExtend(Ty, T);
    return getZeroExtend(Ty, T);
  }

  static ABIArgInfo getExtendInReg(QualType Ty, llvm::Type *T = nullptr) {
    auto AI = getExtend(Ty, T);
    AI.setInReg(true);
    return AI;
  }
  static ABIArgInfo getIgnore() {
    return ABIArgInfo(Ignore);
  }
  static ABIArgInfo getIndirect(CharUnits Alignment, bool ByVal = true,
                                bool Realign = false,
                                llvm::Type *Padding = nullptr) {
    auto AI = ABIArgInfo(Indirect);
    AI.setIndirectAlign(Alignment);
    AI.setIndirectByVal(ByVal);
    AI.setIndirectRealign(Realign);
    AI.setSRetAfterThis(false);
    AI.setPaddingType(Padding);
    return AI;
  }

  /// Pass this in memory using the IR byref attribute.
  static ABIArgInfo getIndirectAliased(CharUnits Alignment, unsigned AddrSpace,
                                       bool Realign = false,
                                       llvm::Type *Padding = nullptr) {
    auto AI = ABIArgInfo(IndirectAliased);
    AI.setIndirectAlign(Alignment);
    AI.setIndirectRealign(Realign);
    AI.setPaddingType(Padding);
    AI.setIndirectAddrSpace(AddrSpace);
    return AI;
  }

  static ABIArgInfo getIndirectInReg(CharUnits Alignment, bool ByVal = true,
                                     bool Realign = false) {
    auto AI = getIndirect(Alignment, ByVal, Realign);
    AI.setInReg(true);
    return AI;
  }
  static ABIArgInfo getInAlloca(unsigned FieldIndex, bool Indirect = false) {
    auto AI = ABIArgInfo(InAlloca);
    AI.setInAllocaFieldIndex(FieldIndex);
    AI.setInAllocaIndirect(Indirect);
    return AI;
  }
  static ABIArgInfo getExpand() {
    auto AI = ABIArgInfo(Expand);
    AI.setPaddingType(nullptr);
    return AI;
  }
  static ABIArgInfo getExpandWithPadding(bool PaddingInReg,
                                         llvm::Type *Padding) {
    auto AI = getExpand();
    AI.setPaddingInReg(PaddingInReg);
    AI.setPaddingType(Padding);
    return AI;
  }

  /// \param unpaddedCoerceToType The coerce-to type with padding elements
  ///   removed, canonicalized to a single element if it would otherwise
  ///   have exactly one element.
  static ABIArgInfo getCoerceAndExpand(llvm::StructType *coerceToType,
                                       llvm::Type *unpaddedCoerceToType) {
#ifndef NDEBUG
    // Check that unpaddedCoerceToType has roughly the right shape.

    // Assert that we only have a struct type if there are multiple elements.
    auto unpaddedStruct = dyn_cast<llvm::StructType>(unpaddedCoerceToType);
    assert(!unpaddedStruct || unpaddedStruct->getNumElements() != 1);

    // Assert that all the non-padding elements have a corresponding element
    // in the unpadded type.
    unsigned unpaddedIndex = 0;
    for (auto eltType : coerceToType->elements()) {
      if (isPaddingForCoerceAndExpand(eltType)) continue;
      if (unpaddedStruct) {
        assert(unpaddedStruct->getElementType(unpaddedIndex) == eltType);
      } else {
        assert(unpaddedIndex == 0 && unpaddedCoerceToType == eltType);
      }
      unpaddedIndex++;
    }

    // Assert that there aren't extra elements in the unpadded type.
    if (unpaddedStruct) {
      assert(unpaddedStruct->getNumElements() == unpaddedIndex);
    } else {
      assert(unpaddedIndex == 1);
    }
#endif

    auto AI = ABIArgInfo(CoerceAndExpand);
    AI.setCoerceToType(coerceToType);
    AI.setUnpaddedCoerceToType(unpaddedCoerceToType);
    return AI;
  }

  static bool isPaddingForCoerceAndExpand(llvm::Type *eltType) {
    if (eltType->isArrayTy()) {
      assert(eltType->getArrayElementType()->isIntegerTy(8));
      return true;
    } else {
      return false;
    }
  }

  Kind getKind() const { return TheKind; }
  bool isDirect() const { return TheKind == Direct; }
  bool isInAlloca() const { return TheKind == InAlloca; }
  bool isExtend() const { return TheKind == Extend; }
  bool isIgnore() const { return TheKind == Ignore; }
  bool isIndirect() const { return TheKind == Indirect; }
  bool isIndirectAliased() const { return TheKind == IndirectAliased; }
  bool isExpand() const { return TheKind == Expand; }
  bool isCoerceAndExpand() const { return TheKind == CoerceAndExpand; }

  bool canHaveCoerceToType() const {
    return isDirect() || isExtend() || isCoerceAndExpand();
  }

  // Direct/Extend accessors
  unsigned getDirectOffset() const {
    assert((isDirect() || isExtend()) && "Not a direct or extend kind");
    return DirectAttr.Offset;
  }
  void setDirectOffset(unsigned Offset) {
    assert((isDirect() || isExtend()) && "Not a direct or extend kind");
    DirectAttr.Offset = Offset;
  }

  unsigned getDirectAlign() const {
    assert((isDirect() || isExtend()) && "Not a direct or extend kind");
    return DirectAttr.Align;
  }
  void setDirectAlign(unsigned Align) {
    assert((isDirect() || isExtend()) && "Not a direct or extend kind");
    DirectAttr.Align = Align;
  }

  bool isSignExt() const {
    assert(isExtend() && "Invalid kind!");
    return SignExt;
  }
  void setSignExt(bool SExt) {
    assert(isExtend() && "Invalid kind!");
    SignExt = SExt;
  }

  llvm::Type *getPaddingType() const {
    return (canHavePaddingType() ? PaddingType : nullptr);
  }

  bool getPaddingInReg() const {
    return PaddingInReg;
  }
  void setPaddingInReg(bool PIR) {
    PaddingInReg = PIR;
  }

  llvm::Type *getCoerceToType() const {
    assert(canHaveCoerceToType() && "Invalid kind!");
    return TypeData;
  }

  void setCoerceToType(llvm::Type *T) {
    assert(canHaveCoerceToType() && "Invalid kind!");
    TypeData = T;
  }

  llvm::StructType *getCoerceAndExpandType() const {
    assert(isCoerceAndExpand());
    return cast<llvm::StructType>(TypeData);
  }

  llvm::Type *getUnpaddedCoerceAndExpandType() const {
    assert(isCoerceAndExpand());
    return UnpaddedCoerceAndExpandType;
  }

  ArrayRef<llvm::Type *>getCoerceAndExpandTypeSequence() const {
    assert(isCoerceAndExpand());
    if (auto structTy =
          dyn_cast<llvm::StructType>(UnpaddedCoerceAndExpandType)) {
      return structTy->elements();
    } else {
      return llvm::ArrayRef(&UnpaddedCoerceAndExpandType, 1);
    }
  }

  bool getInReg() const {
    assert((isDirect() || isExtend() || isIndirect()) && "Invalid kind!");
    return InReg;
  }

  void setInReg(bool IR) {
    assert((isDirect() || isExtend() || isIndirect()) && "Invalid kind!");
    InReg = IR;
  }

  // Indirect accessors
  CharUnits getIndirectAlign() const {
    assert((isIndirect() || isIndirectAliased()) && "Invalid kind!");
    return CharUnits::fromQuantity(IndirectAttr.Align);
  }
  void setIndirectAlign(CharUnits IA) {
    assert((isIndirect() || isIndirectAliased()) && "Invalid kind!");
    IndirectAttr.Align = IA.getQuantity();
  }

  bool getIndirectByVal() const {
    assert(isIndirect() && "Invalid kind!");
    return IndirectByVal;
  }
  void setIndirectByVal(bool IBV) {
    assert(isIndirect() && "Invalid kind!");
    IndirectByVal = IBV;
  }

  unsigned getIndirectAddrSpace() const {
    assert(isIndirectAliased() && "Invalid kind!");
    return IndirectAttr.AddrSpace;
  }

  void setIndirectAddrSpace(unsigned AddrSpace) {
    assert(isIndirectAliased() && "Invalid kind!");
    IndirectAttr.AddrSpace = AddrSpace;
  }

  bool getIndirectRealign() const {
    assert((isIndirect() || isIndirectAliased()) && "Invalid kind!");
    return IndirectRealign;
  }
  void setIndirectRealign(bool IR) {
    assert((isIndirect() || isIndirectAliased()) && "Invalid kind!");
    IndirectRealign = IR;
  }

  bool isSRetAfterThis() const {
    assert(isIndirect() && "Invalid kind!");
    return SRetAfterThis;
  }
  void setSRetAfterThis(bool AfterThis) {
    assert(isIndirect() && "Invalid kind!");
    SRetAfterThis = AfterThis;
  }

  unsigned getInAllocaFieldIndex() const {
    assert(isInAlloca() && "Invalid kind!");
    return AllocaFieldIndex;
  }
  void setInAllocaFieldIndex(unsigned FieldIndex) {
    assert(isInAlloca() && "Invalid kind!");
    AllocaFieldIndex = FieldIndex;
  }

  unsigned getInAllocaIndirect() const {
    assert(isInAlloca() && "Invalid kind!");
    return InAllocaIndirect;
  }
  void setInAllocaIndirect(bool Indirect) {
    assert(isInAlloca() && "Invalid kind!");
    InAllocaIndirect = Indirect;
  }

  /// Return true if this field of an inalloca struct should be returned
  /// to implement a struct return calling convention.
  bool getInAllocaSRet() const {
    assert(isInAlloca() && "Invalid kind!");
    return InAllocaSRet;
  }

  void setInAllocaSRet(bool SRet) {
    assert(isInAlloca() && "Invalid kind!");
    InAllocaSRet = SRet;
  }

  bool getCanBeFlattened() const {
    assert(isDirect() && "Invalid kind!");
    return CanBeFlattened;
  }

  void setCanBeFlattened(bool Flatten) {
    assert(isDirect() && "Invalid kind!");
    CanBeFlattened = Flatten;
  }

  void dump() const;
};

/// A class for recording the number of arguments that a function
/// signature requires.
class RequiredArgs {
  /// The number of required arguments, or ~0 if the signature does
  /// not permit optional arguments.
  unsigned NumRequired;
public:
  enum All_t { All };

  RequiredArgs(All_t _) : NumRequired(~0U) {}
  explicit RequiredArgs(unsigned n) : NumRequired(n) {
    assert(n != ~0U);
  }

  /// Compute the arguments required by the given formal prototype,
  /// given that there may be some additional, non-formal arguments
  /// in play.
  ///
  /// If FD is not null, this will consider pass_object_size params in FD.
  static RequiredArgs forPrototypePlus(const FunctionProtoType *prototype,
                                       unsigned additional) {
    if (!prototype->isVariadic()) return All;

    if (prototype->hasExtParameterInfos())
      additional += llvm::count_if(
          prototype->getExtParameterInfos(),
          [](const FunctionProtoType::ExtParameterInfo &ExtInfo) {
            return ExtInfo.hasPassObjectSize();
          });

    return RequiredArgs(prototype->getNumParams() + additional);
  }

  static RequiredArgs forPrototypePlus(CanQual<FunctionProtoType> prototype,
                                       unsigned additional) {
    return forPrototypePlus(prototype.getTypePtr(), additional);
  }

  static RequiredArgs forPrototype(const FunctionProtoType *prototype) {
    return forPrototypePlus(prototype, 0);
  }

  static RequiredArgs forPrototype(CanQual<FunctionProtoType> prototype) {
    return forPrototypePlus(prototype.getTypePtr(), 0);
  }

  bool allowsOptionalArgs() const { return NumRequired != ~0U; }
  unsigned getNumRequiredArgs() const {
    assert(allowsOptionalArgs());
    return NumRequired;
  }

  /// Return true if the argument at a given index is required.
  bool isRequiredArg(unsigned argIdx) const {
    return argIdx == ~0U || argIdx < NumRequired;
  }

  unsigned getOpaqueData() const { return NumRequired; }
  static RequiredArgs getFromOpaqueData(unsigned value) {
    if (value == ~0U) return All;
    return RequiredArgs(value);
  }
};

// Implementation detail of CGFunctionInfo, factored out so it can be named
// in the TrailingObjects base class of CGFunctionInfo.
struct CGFunctionInfoArgInfo {
  CanQualType type;
  ABIArgInfo info;
};

/// CGFunctionInfo - Class to encapsulate the information about a
/// function definition.
class CGFunctionInfo final
    : public llvm::FoldingSetNode,
      private llvm::TrailingObjects<CGFunctionInfo, CGFunctionInfoArgInfo,
                                    FunctionProtoType::ExtParameterInfo> {
  typedef CGFunctionInfoArgInfo ArgInfo;
  typedef FunctionProtoType::ExtParameterInfo ExtParameterInfo;

  /// The LLVM::CallingConv to use for this function (as specified by the
  /// user).
  unsigned CallingConvention : 8;

  /// The LLVM::CallingConv to actually use for this function, which may
  /// depend on the ABI.
  unsigned EffectiveCallingConvention : 8;

  /// The clang::CallingConv that this was originally created with.
  LLVM_PREFERRED_TYPE(CallingConv)
  unsigned ASTCallingConvention : 6;

  /// Whether this is an instance method.
  LLVM_PREFERRED_TYPE(bool)
  unsigned InstanceMethod : 1;

  /// Whether this is a chain call.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ChainCall : 1;

  /// Whether this function is called by forwarding arguments.
  /// This doesn't support inalloca or varargs.
  LLVM_PREFERRED_TYPE(bool)
  unsigned DelegateCall : 1;

  /// Whether this function is a CMSE nonsecure call
  LLVM_PREFERRED_TYPE(bool)
  unsigned CmseNSCall : 1;

  /// Whether this function is noreturn.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoReturn : 1;

  /// Whether this function is returns-retained.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ReturnsRetained : 1;

  /// Whether this function saved caller registers.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoCallerSavedRegs : 1;

  /// How many arguments to pass inreg.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasRegParm : 1;
  unsigned RegParm : 3;

  /// Whether this function has nocf_check attribute.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoCfCheck : 1;

  /// Log 2 of the maximum vector width.
  unsigned MaxVectorWidth : 4;

  RequiredArgs Required;

  /// The struct representing all arguments passed in memory.  Only used when
  /// passing non-trivial types with inalloca.  Not part of the profile.
  llvm::StructType *ArgStruct;
  unsigned ArgStructAlign : 31;
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasExtParameterInfos : 1;

  unsigned NumArgs;

  ArgInfo *getArgsBuffer() {
    return getTrailingObjects<ArgInfo>();
  }
  const ArgInfo *getArgsBuffer() const {
    return getTrailingObjects<ArgInfo>();
  }

  ExtParameterInfo *getExtParameterInfosBuffer() {
    return getTrailingObjects<ExtParameterInfo>();
  }
  const ExtParameterInfo *getExtParameterInfosBuffer() const{
    return getTrailingObjects<ExtParameterInfo>();
  }

  CGFunctionInfo() : Required(RequiredArgs::All) {}

public:
  static CGFunctionInfo *
  create(unsigned llvmCC, bool instanceMethod, bool chainCall,
         bool delegateCall, const FunctionType::ExtInfo &extInfo,
         ArrayRef<ExtParameterInfo> paramInfos, CanQualType resultType,
         ArrayRef<CanQualType> argTypes, RequiredArgs required);
  void operator delete(void *p) { ::operator delete(p); }

  // Friending class TrailingObjects is apparently not good enough for MSVC,
  // so these have to be public.
  friend class TrailingObjects;
  size_t numTrailingObjects(OverloadToken<ArgInfo>) const {
    return NumArgs + 1;
  }
  size_t numTrailingObjects(OverloadToken<ExtParameterInfo>) const {
    return (HasExtParameterInfos ? NumArgs : 0);
  }

  typedef const ArgInfo *const_arg_iterator;
  typedef ArgInfo *arg_iterator;

  MutableArrayRef<ArgInfo> arguments() {
    return MutableArrayRef<ArgInfo>(arg_begin(), NumArgs);
  }
  ArrayRef<ArgInfo> arguments() const {
    return ArrayRef<ArgInfo>(arg_begin(), NumArgs);
  }

  const_arg_iterator arg_begin() const { return getArgsBuffer() + 1; }
  const_arg_iterator arg_end() const { return getArgsBuffer() + 1 + NumArgs; }
  arg_iterator arg_begin() { return getArgsBuffer() + 1; }
  arg_iterator arg_end() { return getArgsBuffer() + 1 + NumArgs; }

  unsigned  arg_size() const { return NumArgs; }

  bool isVariadic() const { return Required.allowsOptionalArgs(); }
  RequiredArgs getRequiredArgs() const { return Required; }
  unsigned getNumRequiredArgs() const {
    return isVariadic() ? getRequiredArgs().getNumRequiredArgs() : arg_size();
  }

  bool isInstanceMethod() const { return InstanceMethod; }

  bool isChainCall() const { return ChainCall; }

  bool isDelegateCall() const { return DelegateCall; }

  bool isCmseNSCall() const { return CmseNSCall; }

  bool isNoReturn() const { return NoReturn; }

  /// In ARC, whether this function retains its return value.  This
  /// is not always reliable for call sites.
  bool isReturnsRetained() const { return ReturnsRetained; }

  /// Whether this function no longer saves caller registers.
  bool isNoCallerSavedRegs() const { return NoCallerSavedRegs; }

  /// Whether this function has nocf_check attribute.
  bool isNoCfCheck() const { return NoCfCheck; }

  /// getASTCallingConvention() - Return the AST-specified calling
  /// convention.
  CallingConv getASTCallingConvention() const {
    return CallingConv(ASTCallingConvention);
  }

  /// getCallingConvention - Return the user specified calling
  /// convention, which has been translated into an LLVM CC.
  unsigned getCallingConvention() const { return CallingConvention; }

  /// getEffectiveCallingConvention - Return the actual calling convention to
  /// use, which may depend on the ABI.
  unsigned getEffectiveCallingConvention() const {
    return EffectiveCallingConvention;
  }
  void setEffectiveCallingConvention(unsigned Value) {
    EffectiveCallingConvention = Value;
  }

  bool getHasRegParm() const { return HasRegParm; }
  unsigned getRegParm() const { return RegParm; }

  FunctionType::ExtInfo getExtInfo() const {
    return FunctionType::ExtInfo(isNoReturn(), getHasRegParm(), getRegParm(),
                                 getASTCallingConvention(), isReturnsRetained(),
                                 isNoCallerSavedRegs(), isNoCfCheck(),
                                 isCmseNSCall());
  }

  CanQualType getReturnType() const { return getArgsBuffer()[0].type; }

  ABIArgInfo &getReturnInfo() { return getArgsBuffer()[0].info; }
  const ABIArgInfo &getReturnInfo() const { return getArgsBuffer()[0].info; }

  ArrayRef<ExtParameterInfo> getExtParameterInfos() const {
    if (!HasExtParameterInfos) return {};
    return llvm::ArrayRef(getExtParameterInfosBuffer(), NumArgs);
  }
  ExtParameterInfo getExtParameterInfo(unsigned argIndex) const {
    assert(argIndex <= NumArgs);
    if (!HasExtParameterInfos) return ExtParameterInfo();
    return getExtParameterInfos()[argIndex];
  }

  /// Return true if this function uses inalloca arguments.
  bool usesInAlloca() const { return ArgStruct; }

  /// Get the struct type used to represent all the arguments in memory.
  llvm::StructType *getArgStruct() const { return ArgStruct; }
  CharUnits getArgStructAlignment() const {
    return CharUnits::fromQuantity(ArgStructAlign);
  }
  void setArgStruct(llvm::StructType *Ty, CharUnits Align) {
    ArgStruct = Ty;
    ArgStructAlign = Align.getQuantity();
  }

  /// Return the maximum vector width in the arguments.
  unsigned getMaxVectorWidth() const {
    return MaxVectorWidth ? 1U << (MaxVectorWidth - 1) : 0;
  }

  /// Set the maximum vector width in the arguments.
  void setMaxVectorWidth(unsigned Width) {
    assert(llvm::isPowerOf2_32(Width) && "Expected power of 2 vector");
    MaxVectorWidth = llvm::countr_zero(Width) + 1;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    ID.AddInteger(getASTCallingConvention());
    ID.AddBoolean(InstanceMethod);
    ID.AddBoolean(ChainCall);
    ID.AddBoolean(DelegateCall);
    ID.AddBoolean(NoReturn);
    ID.AddBoolean(ReturnsRetained);
    ID.AddBoolean(NoCallerSavedRegs);
    ID.AddBoolean(HasRegParm);
    ID.AddInteger(RegParm);
    ID.AddBoolean(NoCfCheck);
    ID.AddBoolean(CmseNSCall);
    ID.AddInteger(Required.getOpaqueData());
    ID.AddBoolean(HasExtParameterInfos);
    if (HasExtParameterInfos) {
      for (auto paramInfo : getExtParameterInfos())
        ID.AddInteger(paramInfo.getOpaqueValue());
    }
    getReturnType().Profile(ID);
    for (const auto &I : arguments())
      I.type.Profile(ID);
  }
  static void Profile(llvm::FoldingSetNodeID &ID, bool InstanceMethod,
                      bool ChainCall, bool IsDelegateCall,
                      const FunctionType::ExtInfo &info,
                      ArrayRef<ExtParameterInfo> paramInfos,
                      RequiredArgs required, CanQualType resultType,
                      ArrayRef<CanQualType> argTypes) {
    ID.AddInteger(info.getCC());
    ID.AddBoolean(InstanceMethod);
    ID.AddBoolean(ChainCall);
    ID.AddBoolean(IsDelegateCall);
    ID.AddBoolean(info.getNoReturn());
    ID.AddBoolean(info.getProducesResult());
    ID.AddBoolean(info.getNoCallerSavedRegs());
    ID.AddBoolean(info.getHasRegParm());
    ID.AddInteger(info.getRegParm());
    ID.AddBoolean(info.getNoCfCheck());
    ID.AddBoolean(info.getCmseNSCall());
    ID.AddInteger(required.getOpaqueData());
    ID.AddBoolean(!paramInfos.empty());
    if (!paramInfos.empty()) {
      for (auto paramInfo : paramInfos)
        ID.AddInteger(paramInfo.getOpaqueValue());
    }
    resultType.Profile(ID);
    for (ArrayRef<CanQualType>::iterator
           i = argTypes.begin(), e = argTypes.end(); i != e; ++i) {
      i->Profile(ID);
    }
  }
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
