//===-- CGValue.h - LLVM CodeGen wrappers for llvm::Value* ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These classes implement wrappers around llvm::Value in order to
// fully represent the range of values for C L- and R- values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGVALUE_H
#define LLVM_CLANG_LIB_CODEGEN_CGVALUE_H

#include "Address.h"
#include "CGPointerAuthInfo.h"
#include "CodeGenTBAA.h"
#include "EHScopeStack.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

namespace llvm {
  class Constant;
  class MDNode;
}

namespace clang {
namespace CodeGen {
class AggValueSlot;
class CGBuilderTy;
class CodeGenFunction;
struct CGBitFieldInfo;

/// RValue - This trivial value class is used to represent the result of an
/// expression that is evaluated.  It can be one of three things: either a
/// simple LLVM SSA value, a pair of SSA values for complex numbers, or the
/// address of an aggregate value in memory.
class RValue {
  friend struct DominatingValue<RValue>;

  enum FlavorEnum { Scalar, Complex, Aggregate };

  union {
    // Stores first and second value.
    struct {
      llvm::Value *first;
      llvm::Value *second;
    } Vals;

    // Stores aggregate address.
    Address AggregateAddr;
  };

  unsigned IsVolatile : 1;
  unsigned Flavor : 2;

public:
  RValue() : Vals{nullptr, nullptr}, Flavor(Scalar) {}

  bool isScalar() const { return Flavor == Scalar; }
  bool isComplex() const { return Flavor == Complex; }
  bool isAggregate() const { return Flavor == Aggregate; }

  bool isVolatileQualified() const { return IsVolatile; }

  /// getScalarVal() - Return the Value* of this scalar value.
  llvm::Value *getScalarVal() const {
    assert(isScalar() && "Not a scalar!");
    return Vals.first;
  }

  /// getComplexVal - Return the real/imag components of this complex value.
  ///
  std::pair<llvm::Value *, llvm::Value *> getComplexVal() const {
    return std::make_pair(Vals.first, Vals.second);
  }

  /// getAggregateAddr() - Return the Value* of the address of the aggregate.
  Address getAggregateAddress() const {
    assert(isAggregate() && "Not an aggregate!");
    return AggregateAddr;
  }

  llvm::Value *getAggregatePointer(QualType PointeeType,
                                   CodeGenFunction &CGF) const {
    return getAggregateAddress().getBasePointer();
  }

  static RValue getIgnored() {
    // FIXME: should we make this a more explicit state?
    return get(nullptr);
  }

  static RValue get(llvm::Value *V) {
    RValue ER;
    ER.Vals.first = V;
    ER.Flavor = Scalar;
    ER.IsVolatile = false;
    return ER;
  }
  static RValue get(Address Addr, CodeGenFunction &CGF) {
    return RValue::get(Addr.emitRawPointer(CGF));
  }
  static RValue getComplex(llvm::Value *V1, llvm::Value *V2) {
    RValue ER;
    ER.Vals = {V1, V2};
    ER.Flavor = Complex;
    ER.IsVolatile = false;
    return ER;
  }
  static RValue getComplex(const std::pair<llvm::Value *, llvm::Value *> &C) {
    return getComplex(C.first, C.second);
  }
  // FIXME: Aggregate rvalues need to retain information about whether they are
  // volatile or not.  Remove default to find all places that probably get this
  // wrong.

  /// Convert an Address to an RValue. If the Address is not
  /// signed, create an RValue using the unsigned address. Otherwise, resign the
  /// address using the provided type.
  static RValue getAggregate(Address addr, bool isVolatile = false) {
    RValue ER;
    ER.AggregateAddr = addr;
    ER.Flavor = Aggregate;
    ER.IsVolatile = isVolatile;
    return ER;
  }
};

/// Does an ARC strong l-value have precise lifetime?
enum ARCPreciseLifetime_t {
  ARCImpreciseLifetime, ARCPreciseLifetime
};

/// The source of the alignment of an l-value; an expression of
/// confidence in the alignment actually matching the estimate.
enum class AlignmentSource {
  /// The l-value was an access to a declared entity or something
  /// equivalently strong, like the address of an array allocated by a
  /// language runtime.
  Decl,

  /// The l-value was considered opaque, so the alignment was
  /// determined from a type, but that type was an explicitly-aligned
  /// typedef.
  AttributedType,

  /// The l-value was considered opaque, so the alignment was
  /// determined from a type.
  Type
};

/// Given that the base address has the given alignment source, what's
/// our confidence in the alignment of the field?
static inline AlignmentSource getFieldAlignmentSource(AlignmentSource Source) {
  // For now, we don't distinguish fields of opaque pointers from
  // top-level declarations, but maybe we should.
  return AlignmentSource::Decl;
}

class LValueBaseInfo {
  AlignmentSource AlignSource;

public:
  explicit LValueBaseInfo(AlignmentSource Source = AlignmentSource::Type)
    : AlignSource(Source) {}
  AlignmentSource getAlignmentSource() const { return AlignSource; }
  void setAlignmentSource(AlignmentSource Source) { AlignSource = Source; }

  void mergeForCast(const LValueBaseInfo &Info) {
    setAlignmentSource(Info.getAlignmentSource());
  }
};

/// LValue - This represents an lvalue references.  Because C/C++ allow
/// bitfields, this is not a simple LLVM pointer, it may be a pointer plus a
/// bitrange.
class LValue {
  enum {
    Simple,       // This is a normal l-value, use getAddress().
    VectorElt,    // This is a vector element l-value (V[i]), use getVector*
    BitField,     // This is a bitfield l-value, use getBitfield*.
    ExtVectorElt, // This is an extended vector subset, use getExtVectorComp
    GlobalReg,    // This is a register l-value, use getGlobalReg()
    MatrixElt     // This is a matrix element, use getVector*
  } LVType;

  union {
    Address Addr = Address::invalid();
    llvm::Value *V;
  };

  union {
    // Index into a vector subscript: V[i]
    llvm::Value *VectorIdx;

    // ExtVector element subset: V.xyx
    llvm::Constant *VectorElts;

    // BitField start bit and size
    const CGBitFieldInfo *BitFieldInfo;
  };

  QualType Type;

  // 'const' is unused here
  Qualifiers Quals;

  // objective-c's ivar
  bool Ivar:1;

  // objective-c's ivar is an array
  bool ObjIsArray:1;

  // LValue is non-gc'able for any reason, including being a parameter or local
  // variable.
  bool NonGC: 1;

  // Lvalue is a global reference of an objective-c object
  bool GlobalObjCRef : 1;

  // Lvalue is a thread local reference
  bool ThreadLocalRef : 1;

  // Lvalue has ARC imprecise lifetime.  We store this inverted to try
  // to make the default bitfield pattern all-zeroes.
  bool ImpreciseLifetime : 1;

  // This flag shows if a nontemporal load/stores should be used when accessing
  // this lvalue.
  bool Nontemporal : 1;

  LValueBaseInfo BaseInfo;
  TBAAAccessInfo TBAAInfo;

  Expr *BaseIvarExp;

private:
  void Initialize(QualType Type, Qualifiers Quals, Address Addr,
                  LValueBaseInfo BaseInfo, TBAAAccessInfo TBAAInfo) {
    this->Type = Type;
    this->Quals = Quals;
    const unsigned MaxAlign = 1U << 31;
    CharUnits Alignment = Addr.getAlignment();
    assert((isGlobalReg() || !Alignment.isZero() || Type->isIncompleteType()) &&
           "initializing l-value with zero alignment!");
    if (Alignment.getQuantity() > MaxAlign) {
      assert(false && "Alignment exceeds allowed max!");
      Alignment = CharUnits::fromQuantity(MaxAlign);
    }
    this->Addr = Addr;
    this->BaseInfo = BaseInfo;
    this->TBAAInfo = TBAAInfo;

    // Initialize Objective-C flags.
    this->Ivar = this->ObjIsArray = this->NonGC = this->GlobalObjCRef = false;
    this->ImpreciseLifetime = false;
    this->Nontemporal = false;
    this->ThreadLocalRef = false;
    this->BaseIvarExp = nullptr;
  }

  void initializeSimpleLValue(Address Addr, QualType Type,
                              LValueBaseInfo BaseInfo, TBAAAccessInfo TBAAInfo,
                              ASTContext &Context) {
    Qualifiers QS = Type.getQualifiers();
    QS.setObjCGCAttr(Context.getObjCGCAttrKind(Type));
    LVType = Simple;
    Initialize(Type, QS, Addr, BaseInfo, TBAAInfo);
    assert(Addr.getBasePointer()->getType()->isPointerTy());
  }

public:
  bool isSimple() const { return LVType == Simple; }
  bool isVectorElt() const { return LVType == VectorElt; }
  bool isBitField() const { return LVType == BitField; }
  bool isExtVectorElt() const { return LVType == ExtVectorElt; }
  bool isGlobalReg() const { return LVType == GlobalReg; }
  bool isMatrixElt() const { return LVType == MatrixElt; }

  bool isVolatileQualified() const { return Quals.hasVolatile(); }
  bool isRestrictQualified() const { return Quals.hasRestrict(); }
  unsigned getVRQualifiers() const {
    return Quals.getCVRQualifiers() & ~Qualifiers::Const;
  }

  QualType getType() const { return Type; }

  Qualifiers::ObjCLifetime getObjCLifetime() const {
    return Quals.getObjCLifetime();
  }

  bool isObjCIvar() const { return Ivar; }
  void setObjCIvar(bool Value) { Ivar = Value; }

  bool isObjCArray() const { return ObjIsArray; }
  void setObjCArray(bool Value) { ObjIsArray = Value; }

  bool isNonGC () const { return NonGC; }
  void setNonGC(bool Value) { NonGC = Value; }

  bool isGlobalObjCRef() const { return GlobalObjCRef; }
  void setGlobalObjCRef(bool Value) { GlobalObjCRef = Value; }

  bool isThreadLocalRef() const { return ThreadLocalRef; }
  void setThreadLocalRef(bool Value) { ThreadLocalRef = Value;}

  ARCPreciseLifetime_t isARCPreciseLifetime() const {
    return ARCPreciseLifetime_t(!ImpreciseLifetime);
  }
  void setARCPreciseLifetime(ARCPreciseLifetime_t value) {
    ImpreciseLifetime = (value == ARCImpreciseLifetime);
  }
  bool isNontemporal() const { return Nontemporal; }
  void setNontemporal(bool Value) { Nontemporal = Value; }

  bool isObjCWeak() const {
    return Quals.getObjCGCAttr() == Qualifiers::Weak;
  }
  bool isObjCStrong() const {
    return Quals.getObjCGCAttr() == Qualifiers::Strong;
  }

  bool isVolatile() const {
    return Quals.hasVolatile();
  }

  Expr *getBaseIvarExp() const { return BaseIvarExp; }
  void setBaseIvarExp(Expr *V) { BaseIvarExp = V; }

  TBAAAccessInfo getTBAAInfo() const { return TBAAInfo; }
  void setTBAAInfo(TBAAAccessInfo Info) { TBAAInfo = Info; }

  const Qualifiers &getQuals() const { return Quals; }
  Qualifiers &getQuals() { return Quals; }

  LangAS getAddressSpace() const { return Quals.getAddressSpace(); }

  CharUnits getAlignment() const { return Addr.getAlignment(); }
  void setAlignment(CharUnits A) { Addr.setAlignment(A); }

  LValueBaseInfo getBaseInfo() const { return BaseInfo; }
  void setBaseInfo(LValueBaseInfo Info) { BaseInfo = Info; }

  KnownNonNull_t isKnownNonNull() const { return Addr.isKnownNonNull(); }
  LValue setKnownNonNull() {
    Addr.setKnownNonNull();
    return *this;
  }

  // simple lvalue
  llvm::Value *getPointer(CodeGenFunction &CGF) const;
  llvm::Value *emitResignedPointer(QualType PointeeTy,
                                   CodeGenFunction &CGF) const;
  llvm::Value *emitRawPointer(CodeGenFunction &CGF) const;

  Address getAddress() const { return Addr; }

  void setAddress(Address address) { Addr = address; }

  CGPointerAuthInfo getPointerAuthInfo() const {
    return Addr.getPointerAuthInfo();
  }

  // vector elt lvalue
  Address getVectorAddress() const {
    assert(isVectorElt());
    return Addr;
  }
  llvm::Value *getRawVectorPointer(CodeGenFunction &CGF) const {
    assert(isVectorElt());
    return Addr.emitRawPointer(CGF);
  }
  llvm::Value *getVectorPointer() const {
    assert(isVectorElt());
    return Addr.getBasePointer();
  }
  llvm::Value *getVectorIdx() const {
    assert(isVectorElt());
    return VectorIdx;
  }

  Address getMatrixAddress() const {
    assert(isMatrixElt());
    return Addr;
  }
  llvm::Value *getMatrixPointer() const {
    assert(isMatrixElt());
    return Addr.getBasePointer();
  }
  llvm::Value *getMatrixIdx() const {
    assert(isMatrixElt());
    return VectorIdx;
  }

  // extended vector elements.
  Address getExtVectorAddress() const {
    assert(isExtVectorElt());
    return Addr;
  }
  llvm::Value *getRawExtVectorPointer(CodeGenFunction &CGF) const {
    assert(isExtVectorElt());
    return Addr.emitRawPointer(CGF);
  }
  llvm::Constant *getExtVectorElts() const {
    assert(isExtVectorElt());
    return VectorElts;
  }

  // bitfield lvalue
  Address getBitFieldAddress() const {
    assert(isBitField());
    return Addr;
  }
  llvm::Value *getRawBitFieldPointer(CodeGenFunction &CGF) const {
    assert(isBitField());
    return Addr.emitRawPointer(CGF);
  }

  const CGBitFieldInfo &getBitFieldInfo() const {
    assert(isBitField());
    return *BitFieldInfo;
  }

  // global register lvalue
  llvm::Value *getGlobalReg() const { assert(isGlobalReg()); return V; }

  static LValue MakeAddr(Address Addr, QualType type, ASTContext &Context,
                         LValueBaseInfo BaseInfo, TBAAAccessInfo TBAAInfo) {
    LValue R;
    R.LVType = Simple;
    R.initializeSimpleLValue(Addr, type, BaseInfo, TBAAInfo, Context);
    R.Addr = Addr;
    assert(Addr.getType()->isPointerTy());
    return R;
  }

  static LValue MakeVectorElt(Address vecAddress, llvm::Value *Idx,
                              QualType type, LValueBaseInfo BaseInfo,
                              TBAAAccessInfo TBAAInfo) {
    LValue R;
    R.LVType = VectorElt;
    R.VectorIdx = Idx;
    R.Initialize(type, type.getQualifiers(), vecAddress, BaseInfo, TBAAInfo);
    return R;
  }

  static LValue MakeExtVectorElt(Address Addr, llvm::Constant *Elts,
                                 QualType type, LValueBaseInfo BaseInfo,
                                 TBAAAccessInfo TBAAInfo) {
    LValue R;
    R.LVType = ExtVectorElt;
    R.VectorElts = Elts;
    R.Initialize(type, type.getQualifiers(), Addr, BaseInfo, TBAAInfo);
    return R;
  }

  /// Create a new object to represent a bit-field access.
  ///
  /// \param Addr - The base address of the bit-field sequence this
  /// bit-field refers to.
  /// \param Info - The information describing how to perform the bit-field
  /// access.
  static LValue MakeBitfield(Address Addr, const CGBitFieldInfo &Info,
                             QualType type, LValueBaseInfo BaseInfo,
                             TBAAAccessInfo TBAAInfo) {
    LValue R;
    R.LVType = BitField;
    R.BitFieldInfo = &Info;
    R.Initialize(type, type.getQualifiers(), Addr, BaseInfo, TBAAInfo);
    return R;
  }

  static LValue MakeGlobalReg(llvm::Value *V, CharUnits alignment,
                              QualType type) {
    LValue R;
    R.LVType = GlobalReg;
    R.Initialize(type, type.getQualifiers(), Address::invalid(),
                 LValueBaseInfo(AlignmentSource::Decl), TBAAAccessInfo());
    R.V = V;
    return R;
  }

  static LValue MakeMatrixElt(Address matAddress, llvm::Value *Idx,
                              QualType type, LValueBaseInfo BaseInfo,
                              TBAAAccessInfo TBAAInfo) {
    LValue R;
    R.LVType = MatrixElt;
    R.VectorIdx = Idx;
    R.Initialize(type, type.getQualifiers(), matAddress, BaseInfo, TBAAInfo);
    return R;
  }

  RValue asAggregateRValue() const {
    return RValue::getAggregate(getAddress(), isVolatileQualified());
  }
};

/// An aggregate value slot.
class AggValueSlot {
  /// The address.
  Address Addr;

  // Qualifiers
  Qualifiers Quals;

  /// DestructedFlag - This is set to true if some external code is
  /// responsible for setting up a destructor for the slot.  Otherwise
  /// the code which constructs it should push the appropriate cleanup.
  bool DestructedFlag : 1;

  /// ObjCGCFlag - This is set to true if writing to the memory in the
  /// slot might require calling an appropriate Objective-C GC
  /// barrier.  The exact interaction here is unnecessarily mysterious.
  bool ObjCGCFlag : 1;

  /// ZeroedFlag - This is set to true if the memory in the slot is
  /// known to be zero before the assignment into it.  This means that
  /// zero fields don't need to be set.
  bool ZeroedFlag : 1;

  /// AliasedFlag - This is set to true if the slot might be aliased
  /// and it's not undefined behavior to access it through such an
  /// alias.  Note that it's always undefined behavior to access a C++
  /// object that's under construction through an alias derived from
  /// outside the construction process.
  ///
  /// This flag controls whether calls that produce the aggregate
  /// value may be evaluated directly into the slot, or whether they
  /// must be evaluated into an unaliased temporary and then memcpy'ed
  /// over.  Since it's invalid in general to memcpy a non-POD C++
  /// object, it's important that this flag never be set when
  /// evaluating an expression which constructs such an object.
  bool AliasedFlag : 1;

  /// This is set to true if the tail padding of this slot might overlap
  /// another object that may have already been initialized (and whose
  /// value must be preserved by this initialization). If so, we may only
  /// store up to the dsize of the type. Otherwise we can widen stores to
  /// the size of the type.
  bool OverlapFlag : 1;

  /// If is set to true, sanitizer checks are already generated for this address
  /// or not required. For instance, if this address represents an object
  /// created in 'new' expression, sanitizer checks for memory is made as a part
  /// of 'operator new' emission and object constructor should not generate
  /// them.
  bool SanitizerCheckedFlag : 1;

  AggValueSlot(Address Addr, Qualifiers Quals, bool DestructedFlag,
               bool ObjCGCFlag, bool ZeroedFlag, bool AliasedFlag,
               bool OverlapFlag, bool SanitizerCheckedFlag)
      : Addr(Addr), Quals(Quals), DestructedFlag(DestructedFlag),
        ObjCGCFlag(ObjCGCFlag), ZeroedFlag(ZeroedFlag),
        AliasedFlag(AliasedFlag), OverlapFlag(OverlapFlag),
        SanitizerCheckedFlag(SanitizerCheckedFlag) {}

public:
  enum IsAliased_t { IsNotAliased, IsAliased };
  enum IsDestructed_t { IsNotDestructed, IsDestructed };
  enum IsZeroed_t { IsNotZeroed, IsZeroed };
  enum Overlap_t { DoesNotOverlap, MayOverlap };
  enum NeedsGCBarriers_t { DoesNotNeedGCBarriers, NeedsGCBarriers };
  enum IsSanitizerChecked_t { IsNotSanitizerChecked, IsSanitizerChecked };

  /// ignored - Returns an aggregate value slot indicating that the
  /// aggregate value is being ignored.
  static AggValueSlot ignored() {
    return forAddr(Address::invalid(), Qualifiers(), IsNotDestructed,
                   DoesNotNeedGCBarriers, IsNotAliased, DoesNotOverlap);
  }

  /// forAddr - Make a slot for an aggregate value.
  ///
  /// \param quals - The qualifiers that dictate how the slot should
  /// be initialied. Only 'volatile' and the Objective-C lifetime
  /// qualifiers matter.
  ///
  /// \param isDestructed - true if something else is responsible
  ///   for calling destructors on this object
  /// \param needsGC - true if the slot is potentially located
  ///   somewhere that ObjC GC calls should be emitted for
  static AggValueSlot forAddr(Address addr,
                              Qualifiers quals,
                              IsDestructed_t isDestructed,
                              NeedsGCBarriers_t needsGC,
                              IsAliased_t isAliased,
                              Overlap_t mayOverlap,
                              IsZeroed_t isZeroed = IsNotZeroed,
                       IsSanitizerChecked_t isChecked = IsNotSanitizerChecked) {
    if (addr.isValid())
      addr.setKnownNonNull();
    return AggValueSlot(addr, quals, isDestructed, needsGC, isZeroed, isAliased,
                        mayOverlap, isChecked);
  }

  static AggValueSlot
  forLValue(const LValue &LV, IsDestructed_t isDestructed,
            NeedsGCBarriers_t needsGC, IsAliased_t isAliased,
            Overlap_t mayOverlap, IsZeroed_t isZeroed = IsNotZeroed,
            IsSanitizerChecked_t isChecked = IsNotSanitizerChecked) {
    return forAddr(LV.getAddress(), LV.getQuals(), isDestructed, needsGC,
                   isAliased, mayOverlap, isZeroed, isChecked);
  }

  IsDestructed_t isExternallyDestructed() const {
    return IsDestructed_t(DestructedFlag);
  }
  void setExternallyDestructed(bool destructed = true) {
    DestructedFlag = destructed;
  }

  Qualifiers getQualifiers() const { return Quals; }

  bool isVolatile() const {
    return Quals.hasVolatile();
  }

  void setVolatile(bool flag) {
    if (flag)
      Quals.addVolatile();
    else
      Quals.removeVolatile();
  }

  Qualifiers::ObjCLifetime getObjCLifetime() const {
    return Quals.getObjCLifetime();
  }

  NeedsGCBarriers_t requiresGCollection() const {
    return NeedsGCBarriers_t(ObjCGCFlag);
  }

  llvm::Value *getPointer(QualType PointeeTy, CodeGenFunction &CGF) const;

  llvm::Value *emitRawPointer(CodeGenFunction &CGF) const {
    return Addr.isValid() ? Addr.emitRawPointer(CGF) : nullptr;
  }

  Address getAddress() const {
    return Addr;
  }

  bool isIgnored() const { return !Addr.isValid(); }

  CharUnits getAlignment() const {
    return Addr.getAlignment();
  }

  IsAliased_t isPotentiallyAliased() const {
    return IsAliased_t(AliasedFlag);
  }

  Overlap_t mayOverlap() const {
    return Overlap_t(OverlapFlag);
  }

  bool isSanitizerChecked() const {
    return SanitizerCheckedFlag;
  }

  RValue asRValue() const {
    if (isIgnored()) {
      return RValue::getIgnored();
    } else {
      return RValue::getAggregate(getAddress(), isVolatile());
    }
  }

  void setZeroed(bool V = true) { ZeroedFlag = V; }
  IsZeroed_t isZeroed() const {
    return IsZeroed_t(ZeroedFlag);
  }

  /// Get the preferred size to use when storing a value to this slot. This
  /// is the type size unless that might overlap another object, in which
  /// case it's the dsize.
  CharUnits getPreferredSize(ASTContext &Ctx, QualType Type) const {
    return mayOverlap() ? Ctx.getTypeInfoDataSizeInChars(Type).Width
                        : Ctx.getTypeSizeInChars(Type);
  }
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
