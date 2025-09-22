//===--- CGRecordLayoutBuilder.cpp - CGRecordLayout builder  ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Builder implementation for CGRecordLayout objects.
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "CGCXXABI.h"
#include "CGRecordLayout.h"
#include "CodeGenTypes.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/CodeGenOptions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;
using namespace CodeGen;

namespace {
/// The CGRecordLowering is responsible for lowering an ASTRecordLayout to an
/// llvm::Type.  Some of the lowering is straightforward, some is not.  Here we
/// detail some of the complexities and weirdnesses here.
/// * LLVM does not have unions - Unions can, in theory be represented by any
///   llvm::Type with correct size.  We choose a field via a specific heuristic
///   and add padding if necessary.
/// * LLVM does not have bitfields - Bitfields are collected into contiguous
///   runs and allocated as a single storage type for the run.  ASTRecordLayout
///   contains enough information to determine where the runs break.  Microsoft
///   and Itanium follow different rules and use different codepaths.
/// * It is desired that, when possible, bitfields use the appropriate iN type
///   when lowered to llvm types. For example unsigned x : 24 gets lowered to
///   i24.  This isn't always possible because i24 has storage size of 32 bit
///   and if it is possible to use that extra byte of padding we must use [i8 x
///   3] instead of i24. This is computed when accumulating bitfields in
///   accumulateBitfields.
///   C++ examples that require clipping:
///   struct { int a : 24; char b; }; // a must be clipped, b goes at offset 3
///   struct A { int a : 24; ~A(); }; // a must be clipped because:
///   struct B : A { char b; }; // b goes at offset 3
/// * The allocation of bitfield access units is described in more detail in
///   CGRecordLowering::accumulateBitFields.
/// * Clang ignores 0 sized bitfields and 0 sized bases but *not* zero sized
///   fields.  The existing asserts suggest that LLVM assumes that *every* field
///   has an underlying storage type.  Therefore empty structures containing
///   zero sized subobjects such as empty records or zero sized arrays still get
///   a zero sized (empty struct) storage type.
/// * Clang reads the complete type rather than the base type when generating
///   code to access fields.  Bitfields in tail position with tail padding may
///   be clipped in the base class but not the complete class (we may discover
///   that the tail padding is not used in the complete class.) However,
///   because LLVM reads from the complete type it can generate incorrect code
///   if we do not clip the tail padding off of the bitfield in the complete
///   layout.
/// * Itanium allows nearly empty primary virtual bases.  These bases don't get
///   get their own storage because they're laid out as part of another base
///   or at the beginning of the structure.  Determining if a VBase actually
///   gets storage awkwardly involves a walk of all bases.
/// * VFPtrs and VBPtrs do *not* make a record NotZeroInitializable.
struct CGRecordLowering {
  // MemberInfo is a helper structure that contains information about a record
  // member.  In additional to the standard member types, there exists a
  // sentinel member type that ensures correct rounding.
  struct MemberInfo {
    CharUnits Offset;
    enum InfoKind { VFPtr, VBPtr, Field, Base, VBase } Kind;
    llvm::Type *Data;
    union {
      const FieldDecl *FD;
      const CXXRecordDecl *RD;
    };
    MemberInfo(CharUnits Offset, InfoKind Kind, llvm::Type *Data,
               const FieldDecl *FD = nullptr)
      : Offset(Offset), Kind(Kind), Data(Data), FD(FD) {}
    MemberInfo(CharUnits Offset, InfoKind Kind, llvm::Type *Data,
               const CXXRecordDecl *RD)
      : Offset(Offset), Kind(Kind), Data(Data), RD(RD) {}
    // MemberInfos are sorted so we define a < operator.
    bool operator <(const MemberInfo& a) const { return Offset < a.Offset; }
  };
  // The constructor.
  CGRecordLowering(CodeGenTypes &Types, const RecordDecl *D, bool Packed);
  // Short helper routines.
  /// Constructs a MemberInfo instance from an offset and llvm::Type *.
  static MemberInfo StorageInfo(CharUnits Offset, llvm::Type *Data) {
    return MemberInfo(Offset, MemberInfo::Field, Data);
  }

  /// The Microsoft bitfield layout rule allocates discrete storage
  /// units of the field's formal type and only combines adjacent
  /// fields of the same formal type.  We want to emit a layout with
  /// these discrete storage units instead of combining them into a
  /// continuous run.
  bool isDiscreteBitFieldABI() const {
    return Context.getTargetInfo().getCXXABI().isMicrosoft() ||
           D->isMsStruct(Context);
  }

  /// Helper function to check if we are targeting AAPCS.
  bool isAAPCS() const {
    return Context.getTargetInfo().getABI().starts_with("aapcs");
  }

  /// Helper function to check if the target machine is BigEndian.
  bool isBE() const { return Context.getTargetInfo().isBigEndian(); }

  /// The Itanium base layout rule allows virtual bases to overlap
  /// other bases, which complicates layout in specific ways.
  ///
  /// Note specifically that the ms_struct attribute doesn't change this.
  bool isOverlappingVBaseABI() const {
    return !Context.getTargetInfo().getCXXABI().isMicrosoft();
  }

  /// Wraps llvm::Type::getIntNTy with some implicit arguments.
  llvm::Type *getIntNType(uint64_t NumBits) const {
    unsigned AlignedBits = llvm::alignTo(NumBits, Context.getCharWidth());
    return llvm::Type::getIntNTy(Types.getLLVMContext(), AlignedBits);
  }
  /// Get the LLVM type sized as one character unit.
  llvm::Type *getCharType() const {
    return llvm::Type::getIntNTy(Types.getLLVMContext(),
                                 Context.getCharWidth());
  }
  /// Gets an llvm type of size NumChars and alignment 1.
  llvm::Type *getByteArrayType(CharUnits NumChars) const {
    assert(!NumChars.isZero() && "Empty byte arrays aren't allowed.");
    llvm::Type *Type = getCharType();
    return NumChars == CharUnits::One() ? Type :
        (llvm::Type *)llvm::ArrayType::get(Type, NumChars.getQuantity());
  }
  /// Gets the storage type for a field decl and handles storage
  /// for itanium bitfields that are smaller than their declared type.
  llvm::Type *getStorageType(const FieldDecl *FD) const {
    llvm::Type *Type = Types.ConvertTypeForMem(FD->getType());
    if (!FD->isBitField()) return Type;
    if (isDiscreteBitFieldABI()) return Type;
    return getIntNType(std::min(FD->getBitWidthValue(Context),
                             (unsigned)Context.toBits(getSize(Type))));
  }
  /// Gets the llvm Basesubobject type from a CXXRecordDecl.
  llvm::Type *getStorageType(const CXXRecordDecl *RD) const {
    return Types.getCGRecordLayout(RD).getBaseSubobjectLLVMType();
  }
  CharUnits bitsToCharUnits(uint64_t BitOffset) const {
    return Context.toCharUnitsFromBits(BitOffset);
  }
  CharUnits getSize(llvm::Type *Type) const {
    return CharUnits::fromQuantity(DataLayout.getTypeAllocSize(Type));
  }
  CharUnits getAlignment(llvm::Type *Type) const {
    return CharUnits::fromQuantity(DataLayout.getABITypeAlign(Type));
  }
  bool isZeroInitializable(const FieldDecl *FD) const {
    return Types.isZeroInitializable(FD->getType());
  }
  bool isZeroInitializable(const RecordDecl *RD) const {
    return Types.isZeroInitializable(RD);
  }
  void appendPaddingBytes(CharUnits Size) {
    if (!Size.isZero())
      FieldTypes.push_back(getByteArrayType(Size));
  }
  uint64_t getFieldBitOffset(const FieldDecl *FD) const {
    return Layout.getFieldOffset(FD->getFieldIndex());
  }
  // Layout routines.
  void setBitFieldInfo(const FieldDecl *FD, CharUnits StartOffset,
                       llvm::Type *StorageType);
  /// Lowers an ASTRecordLayout to a llvm type.
  void lower(bool NonVirtualBaseType);
  void lowerUnion(bool isNoUniqueAddress);
  void accumulateFields(bool isNonVirtualBaseType);
  RecordDecl::field_iterator
  accumulateBitFields(bool isNonVirtualBaseType,
                      RecordDecl::field_iterator Field,
                      RecordDecl::field_iterator FieldEnd);
  void computeVolatileBitfields();
  void accumulateBases();
  void accumulateVPtrs();
  void accumulateVBases();
  /// Recursively searches all of the bases to find out if a vbase is
  /// not the primary vbase of some base class.
  bool hasOwnStorage(const CXXRecordDecl *Decl,
                     const CXXRecordDecl *Query) const;
  void calculateZeroInit();
  CharUnits calculateTailClippingOffset(bool isNonVirtualBaseType) const;
  void checkBitfieldClipping(bool isNonVirtualBaseType) const;
  /// Determines if we need a packed llvm struct.
  void determinePacked(bool NVBaseType);
  /// Inserts padding everywhere it's needed.
  void insertPadding();
  /// Fills out the structures that are ultimately consumed.
  void fillOutputFields();
  // Input memoization fields.
  CodeGenTypes &Types;
  const ASTContext &Context;
  const RecordDecl *D;
  const CXXRecordDecl *RD;
  const ASTRecordLayout &Layout;
  const llvm::DataLayout &DataLayout;
  // Helpful intermediate data-structures.
  std::vector<MemberInfo> Members;
  // Output fields, consumed by CodeGenTypes::ComputeRecordLayout.
  SmallVector<llvm::Type *, 16> FieldTypes;
  llvm::DenseMap<const FieldDecl *, unsigned> Fields;
  llvm::DenseMap<const FieldDecl *, CGBitFieldInfo> BitFields;
  llvm::DenseMap<const CXXRecordDecl *, unsigned> NonVirtualBases;
  llvm::DenseMap<const CXXRecordDecl *, unsigned> VirtualBases;
  bool IsZeroInitializable : 1;
  bool IsZeroInitializableAsBase : 1;
  bool Packed : 1;
private:
  CGRecordLowering(const CGRecordLowering &) = delete;
  void operator =(const CGRecordLowering &) = delete;
};
} // namespace {

CGRecordLowering::CGRecordLowering(CodeGenTypes &Types, const RecordDecl *D,
                                   bool Packed)
    : Types(Types), Context(Types.getContext()), D(D),
      RD(dyn_cast<CXXRecordDecl>(D)),
      Layout(Types.getContext().getASTRecordLayout(D)),
      DataLayout(Types.getDataLayout()), IsZeroInitializable(true),
      IsZeroInitializableAsBase(true), Packed(Packed) {}

void CGRecordLowering::setBitFieldInfo(
    const FieldDecl *FD, CharUnits StartOffset, llvm::Type *StorageType) {
  CGBitFieldInfo &Info = BitFields[FD->getCanonicalDecl()];
  Info.IsSigned = FD->getType()->isSignedIntegerOrEnumerationType();
  Info.Offset = (unsigned)(getFieldBitOffset(FD) - Context.toBits(StartOffset));
  Info.Size = FD->getBitWidthValue(Context);
  Info.StorageSize = (unsigned)DataLayout.getTypeAllocSizeInBits(StorageType);
  Info.StorageOffset = StartOffset;
  if (Info.Size > Info.StorageSize)
    Info.Size = Info.StorageSize;
  // Reverse the bit offsets for big endian machines. Because we represent
  // a bitfield as a single large integer load, we can imagine the bits
  // counting from the most-significant-bit instead of the
  // least-significant-bit.
  if (DataLayout.isBigEndian())
    Info.Offset = Info.StorageSize - (Info.Offset + Info.Size);

  Info.VolatileStorageSize = 0;
  Info.VolatileOffset = 0;
  Info.VolatileStorageOffset = CharUnits::Zero();
}

void CGRecordLowering::lower(bool NVBaseType) {
  // The lowering process implemented in this function takes a variety of
  // carefully ordered phases.
  // 1) Store all members (fields and bases) in a list and sort them by offset.
  // 2) Add a 1-byte capstone member at the Size of the structure.
  // 3) Clip bitfield storages members if their tail padding is or might be
  //    used by another field or base.  The clipping process uses the capstone
  //    by treating it as another object that occurs after the record.
  // 4) Determine if the llvm-struct requires packing.  It's important that this
  //    phase occur after clipping, because clipping changes the llvm type.
  //    This phase reads the offset of the capstone when determining packedness
  //    and updates the alignment of the capstone to be equal of the alignment
  //    of the record after doing so.
  // 5) Insert padding everywhere it is needed.  This phase requires 'Packed' to
  //    have been computed and needs to know the alignment of the record in
  //    order to understand if explicit tail padding is needed.
  // 6) Remove the capstone, we don't need it anymore.
  // 7) Determine if this record can be zero-initialized.  This phase could have
  //    been placed anywhere after phase 1.
  // 8) Format the complete list of members in a way that can be consumed by
  //    CodeGenTypes::ComputeRecordLayout.
  CharUnits Size = NVBaseType ? Layout.getNonVirtualSize() : Layout.getSize();
  if (D->isUnion()) {
    lowerUnion(NVBaseType);
    computeVolatileBitfields();
    return;
  }
  accumulateFields(NVBaseType);
  // RD implies C++.
  if (RD) {
    accumulateVPtrs();
    accumulateBases();
    if (Members.empty()) {
      appendPaddingBytes(Size);
      computeVolatileBitfields();
      return;
    }
    if (!NVBaseType)
      accumulateVBases();
  }
  llvm::stable_sort(Members);
  checkBitfieldClipping(NVBaseType);
  Members.push_back(StorageInfo(Size, getIntNType(8)));
  determinePacked(NVBaseType);
  insertPadding();
  Members.pop_back();
  calculateZeroInit();
  fillOutputFields();
  computeVolatileBitfields();
}

void CGRecordLowering::lowerUnion(bool isNoUniqueAddress) {
  CharUnits LayoutSize =
      isNoUniqueAddress ? Layout.getDataSize() : Layout.getSize();
  llvm::Type *StorageType = nullptr;
  bool SeenNamedMember = false;
  // Iterate through the fields setting bitFieldInfo and the Fields array. Also
  // locate the "most appropriate" storage type.  The heuristic for finding the
  // storage type isn't necessary, the first (non-0-length-bitfield) field's
  // type would work fine and be simpler but would be different than what we've
  // been doing and cause lit tests to change.
  for (const auto *Field : D->fields()) {
    if (Field->isBitField()) {
      if (Field->isZeroLengthBitField(Context))
        continue;
      llvm::Type *FieldType = getStorageType(Field);
      if (LayoutSize < getSize(FieldType))
        FieldType = getByteArrayType(LayoutSize);
      setBitFieldInfo(Field, CharUnits::Zero(), FieldType);
    }
    Fields[Field->getCanonicalDecl()] = 0;
    llvm::Type *FieldType = getStorageType(Field);
    // Compute zero-initializable status.
    // This union might not be zero initialized: it may contain a pointer to
    // data member which might have some exotic initialization sequence.
    // If this is the case, then we aught not to try and come up with a "better"
    // type, it might not be very easy to come up with a Constant which
    // correctly initializes it.
    if (!SeenNamedMember) {
      SeenNamedMember = Field->getIdentifier();
      if (!SeenNamedMember)
        if (const auto *FieldRD = Field->getType()->getAsRecordDecl())
          SeenNamedMember = FieldRD->findFirstNamedDataMember();
      if (SeenNamedMember && !isZeroInitializable(Field)) {
        IsZeroInitializable = IsZeroInitializableAsBase = false;
        StorageType = FieldType;
      }
    }
    // Because our union isn't zero initializable, we won't be getting a better
    // storage type.
    if (!IsZeroInitializable)
      continue;
    // Conditionally update our storage type if we've got a new "better" one.
    if (!StorageType ||
        getAlignment(FieldType) >  getAlignment(StorageType) ||
        (getAlignment(FieldType) == getAlignment(StorageType) &&
        getSize(FieldType) > getSize(StorageType)))
      StorageType = FieldType;
  }
  // If we have no storage type just pad to the appropriate size and return.
  if (!StorageType)
    return appendPaddingBytes(LayoutSize);
  // If our storage size was bigger than our required size (can happen in the
  // case of packed bitfields on Itanium) then just use an I8 array.
  if (LayoutSize < getSize(StorageType))
    StorageType = getByteArrayType(LayoutSize);
  FieldTypes.push_back(StorageType);
  appendPaddingBytes(LayoutSize - getSize(StorageType));
  // Set packed if we need it.
  const auto StorageAlignment = getAlignment(StorageType);
  assert((Layout.getSize() % StorageAlignment == 0 ||
          Layout.getDataSize() % StorageAlignment) &&
         "Union's standard layout and no_unique_address layout must agree on "
         "packedness");
  if (Layout.getDataSize() % StorageAlignment)
    Packed = true;
}

void CGRecordLowering::accumulateFields(bool isNonVirtualBaseType) {
  for (RecordDecl::field_iterator Field = D->field_begin(),
                                  FieldEnd = D->field_end();
       Field != FieldEnd;) {
    if (Field->isBitField()) {
      Field = accumulateBitFields(isNonVirtualBaseType, Field, FieldEnd);
      assert((Field == FieldEnd || !Field->isBitField()) &&
             "Failed to accumulate all the bitfields");
    } else if (isEmptyFieldForLayout(Context, *Field)) {
      // Empty fields have no storage.
      ++Field;
    } else {
      // Use base subobject layout for the potentially-overlapping field,
      // as it is done in RecordLayoutBuilder
      Members.push_back(MemberInfo(
          bitsToCharUnits(getFieldBitOffset(*Field)), MemberInfo::Field,
          Field->isPotentiallyOverlapping()
              ? getStorageType(Field->getType()->getAsCXXRecordDecl())
              : getStorageType(*Field),
          *Field));
      ++Field;
    }
  }
}

// Create members for bitfields. Field is a bitfield, and FieldEnd is the end
// iterator of the record. Return the first non-bitfield encountered.  We need
// to know whether this is the base or complete layout, as virtual bases could
// affect the upper bound of bitfield access unit allocation.
RecordDecl::field_iterator
CGRecordLowering::accumulateBitFields(bool isNonVirtualBaseType,
                                      RecordDecl::field_iterator Field,
                                      RecordDecl::field_iterator FieldEnd) {
  if (isDiscreteBitFieldABI()) {
    // Run stores the first element of the current run of bitfields. FieldEnd is
    // used as a special value to note that we don't have a current run. A
    // bitfield run is a contiguous collection of bitfields that can be stored
    // in the same storage block. Zero-sized bitfields and bitfields that would
    // cross an alignment boundary break a run and start a new one.
    RecordDecl::field_iterator Run = FieldEnd;
    // Tail is the offset of the first bit off the end of the current run. It's
    // used to determine if the ASTRecordLayout is treating these two bitfields
    // as contiguous. StartBitOffset is offset of the beginning of the Run.
    uint64_t StartBitOffset, Tail = 0;
    for (; Field != FieldEnd && Field->isBitField(); ++Field) {
      // Zero-width bitfields end runs.
      if (Field->isZeroLengthBitField(Context)) {
        Run = FieldEnd;
        continue;
      }
      uint64_t BitOffset = getFieldBitOffset(*Field);
      llvm::Type *Type = Types.ConvertTypeForMem(Field->getType());
      // If we don't have a run yet, or don't live within the previous run's
      // allocated storage then we allocate some storage and start a new run.
      if (Run == FieldEnd || BitOffset >= Tail) {
        Run = Field;
        StartBitOffset = BitOffset;
        Tail = StartBitOffset + DataLayout.getTypeAllocSizeInBits(Type);
        // Add the storage member to the record.  This must be added to the
        // record before the bitfield members so that it gets laid out before
        // the bitfields it contains get laid out.
        Members.push_back(StorageInfo(bitsToCharUnits(StartBitOffset), Type));
      }
      // Bitfields get the offset of their storage but come afterward and remain
      // there after a stable sort.
      Members.push_back(MemberInfo(bitsToCharUnits(StartBitOffset),
                                   MemberInfo::Field, nullptr, *Field));
    }
    return Field;
  }

  // The SysV ABI can overlap bitfield storage units with both other bitfield
  // storage units /and/ other non-bitfield data members. Accessing a sequence
  // of bitfields mustn't interfere with adjacent non-bitfields -- they're
  // permitted to be accessed in separate threads for instance.

  // We split runs of bit-fields into a sequence of "access units". When we emit
  // a load or store of a bit-field, we'll load/store the entire containing
  // access unit. As mentioned, the standard requires that these loads and
  // stores must not interfere with accesses to other memory locations, and it
  // defines the bit-field's memory location as the current run of
  // non-zero-width bit-fields. So an access unit must never overlap with
  // non-bit-field storage or cross a zero-width bit-field. Otherwise, we're
  // free to draw the lines as we see fit.

  // Drawing these lines well can be complicated. LLVM generally can't modify a
  // program to access memory that it didn't before, so using very narrow access
  // units can prevent the compiler from using optimal access patterns. For
  // example, suppose a run of bit-fields occupies four bytes in a struct. If we
  // split that into four 1-byte access units, then a sequence of assignments
  // that doesn't touch all four bytes may have to be emitted with multiple
  // 8-bit stores instead of a single 32-bit store. On the other hand, if we use
  // very wide access units, we may find ourselves emitting accesses to
  // bit-fields we didn't really need to touch, just because LLVM was unable to
  // clean up after us.

  // It is desirable to have access units be aligned powers of 2 no larger than
  // a register. (On non-strict alignment ISAs, the alignment requirement can be
  // dropped.) A three byte access unit will be accessed using 2-byte and 1-byte
  // accesses and bit manipulation. If no bitfield straddles across the two
  // separate accesses, it is better to have separate 2-byte and 1-byte access
  // units, as then LLVM will not generate unnecessary memory accesses, or bit
  // manipulation. Similarly, on a strict-alignment architecture, it is better
  // to keep access-units naturally aligned, to avoid similar bit
  // manipulation synthesizing larger unaligned accesses.

  // Bitfields that share parts of a single byte are, of necessity, placed in
  // the same access unit. That unit will encompass a consecutive run where
  // adjacent bitfields share parts of a byte. (The first bitfield of such an
  // access unit will start at the beginning of a byte.)

  // We then try and accumulate adjacent access units when the combined unit is
  // naturally sized, no larger than a register, and (on a strict alignment
  // ISA), naturally aligned. Note that this requires lookahead to one or more
  // subsequent access units. For instance, consider a 2-byte access-unit
  // followed by 2 1-byte units. We can merge that into a 4-byte access-unit,
  // but we would not want to merge a 2-byte followed by a single 1-byte (and no
  // available tail padding). We keep track of the best access unit seen so far,
  // and use that when we determine we cannot accumulate any more. Then we start
  // again at the bitfield following that best one.

  // The accumulation is also prevented when:
  // *) it would cross a character-aigned zero-width bitfield, or
  // *) fine-grained bitfield access option is in effect.

  CharUnits RegSize =
      bitsToCharUnits(Context.getTargetInfo().getRegisterWidth());
  unsigned CharBits = Context.getCharWidth();

  // Limit of useable tail padding at end of the record. Computed lazily and
  // cached here.
  CharUnits ScissorOffset = CharUnits::Zero();

  // Data about the start of the span we're accumulating to create an access
  // unit from. Begin is the first bitfield of the span. If Begin is FieldEnd,
  // we've not got a current span. The span starts at the BeginOffset character
  // boundary. BitSizeSinceBegin is the size (in bits) of the span -- this might
  // include padding when we've advanced to a subsequent bitfield run.
  RecordDecl::field_iterator Begin = FieldEnd;
  CharUnits BeginOffset;
  uint64_t BitSizeSinceBegin;

  // The (non-inclusive) end of the largest acceptable access unit we've found
  // since Begin. If this is Begin, we're gathering the initial set of bitfields
  // of a new span. BestEndOffset is the end of that acceptable access unit --
  // it might extend beyond the last character of the bitfield run, using
  // available padding characters.
  RecordDecl::field_iterator BestEnd = Begin;
  CharUnits BestEndOffset;
  bool BestClipped; // Whether the representation must be in a byte array.

  for (;;) {
    // AtAlignedBoundary is true iff Field is the (potential) start of a new
    // span (or the end of the bitfields). When true, LimitOffset is the
    // character offset of that span and Barrier indicates whether the new
    // span cannot be merged into the current one.
    bool AtAlignedBoundary = false;
    bool Barrier = false;

    if (Field != FieldEnd && Field->isBitField()) {
      uint64_t BitOffset = getFieldBitOffset(*Field);
      if (Begin == FieldEnd) {
        // Beginning a new span.
        Begin = Field;
        BestEnd = Begin;

        assert((BitOffset % CharBits) == 0 && "Not at start of char");
        BeginOffset = bitsToCharUnits(BitOffset);
        BitSizeSinceBegin = 0;
      } else if ((BitOffset % CharBits) != 0) {
        // Bitfield occupies the same character as previous bitfield, it must be
        // part of the same span. This can include zero-length bitfields, should
        // the target not align them to character boundaries. Such non-alignment
        // is at variance with the standards, which require zero-length
        // bitfields be a barrier between access units. But of course we can't
        // achieve that in the middle of a character.
        assert(BitOffset == Context.toBits(BeginOffset) + BitSizeSinceBegin &&
               "Concatenating non-contiguous bitfields");
      } else {
        // Bitfield potentially begins a new span. This includes zero-length
        // bitfields on non-aligning targets that lie at character boundaries
        // (those are barriers to merging).
        if (Field->isZeroLengthBitField(Context))
          Barrier = true;
        AtAlignedBoundary = true;
      }
    } else {
      // We've reached the end of the bitfield run. Either we're done, or this
      // is a barrier for the current span.
      if (Begin == FieldEnd)
        break;

      Barrier = true;
      AtAlignedBoundary = true;
    }

    // InstallBest indicates whether we should create an access unit for the
    // current best span: fields [Begin, BestEnd) occupying characters
    // [BeginOffset, BestEndOffset).
    bool InstallBest = false;
    if (AtAlignedBoundary) {
      // Field is the start of a new span or the end of the bitfields. The
      // just-seen span now extends to BitSizeSinceBegin.

      // Determine if we can accumulate that just-seen span into the current
      // accumulation.
      CharUnits AccessSize = bitsToCharUnits(BitSizeSinceBegin + CharBits - 1);
      if (BestEnd == Begin) {
        // This is the initial run at the start of a new span. By definition,
        // this is the best seen so far.
        BestEnd = Field;
        BestEndOffset = BeginOffset + AccessSize;
        // Assume clipped until proven not below.
        BestClipped = true;
        if (!BitSizeSinceBegin)
          // A zero-sized initial span -- this will install nothing and reset
          // for another.
          InstallBest = true;
      } else if (AccessSize > RegSize)
        // Accumulating the just-seen span would create a multi-register access
        // unit, which would increase register pressure.
        InstallBest = true;

      if (!InstallBest) {
        // Determine if accumulating the just-seen span will create an expensive
        // access unit or not.
        llvm::Type *Type = getIntNType(Context.toBits(AccessSize));
        if (!Context.getTargetInfo().hasCheapUnalignedBitFieldAccess()) {
          // Unaligned accesses are expensive. Only accumulate if the new unit
          // is naturally aligned. Otherwise install the best we have, which is
          // either the initial access unit (can't do better), or a naturally
          // aligned accumulation (since we would have already installed it if
          // it wasn't naturally aligned).
          CharUnits Align = getAlignment(Type);
          if (Align > Layout.getAlignment())
            // The alignment required is greater than the containing structure
            // itself.
            InstallBest = true;
          else if (!BeginOffset.isMultipleOf(Align))
            // The access unit is not at a naturally aligned offset within the
            // structure.
            InstallBest = true;

          if (InstallBest && BestEnd == Field)
            // We're installing the first span, whose clipping was presumed
            // above. Compute it correctly.
            if (getSize(Type) == AccessSize)
              BestClipped = false;
        }

        if (!InstallBest) {
          // Find the next used storage offset to determine what the limit of
          // the current span is. That's either the offset of the next field
          // with storage (which might be Field itself) or the end of the
          // non-reusable tail padding.
          CharUnits LimitOffset;
          for (auto Probe = Field; Probe != FieldEnd; ++Probe)
            if (!isEmptyFieldForLayout(Context, *Probe)) {
              // A member with storage sets the limit.
              assert((getFieldBitOffset(*Probe) % CharBits) == 0 &&
                     "Next storage is not byte-aligned");
              LimitOffset = bitsToCharUnits(getFieldBitOffset(*Probe));
              goto FoundLimit;
            }
          // We reached the end of the fields, determine the bounds of useable
          // tail padding. As this can be complex for C++, we cache the result.
          if (ScissorOffset.isZero()) {
            ScissorOffset = calculateTailClippingOffset(isNonVirtualBaseType);
            assert(!ScissorOffset.isZero() && "Tail clipping at zero");
          }

          LimitOffset = ScissorOffset;
        FoundLimit:;

          CharUnits TypeSize = getSize(Type);
          if (BeginOffset + TypeSize <= LimitOffset) {
            // There is space before LimitOffset to create a naturally-sized
            // access unit.
            BestEndOffset = BeginOffset + TypeSize;
            BestEnd = Field;
            BestClipped = false;
          }

          if (Barrier)
            // The next field is a barrier that we cannot merge across.
            InstallBest = true;
          else if (Types.getCodeGenOpts().FineGrainedBitfieldAccesses)
            // Fine-grained access, so no merging of spans.
            InstallBest = true;
          else
            // Otherwise, we're not installing. Update the bit size
            // of the current span to go all the way to LimitOffset, which is
            // the (aligned) offset of next bitfield to consider.
            BitSizeSinceBegin = Context.toBits(LimitOffset - BeginOffset);
        }
      }
    }

    if (InstallBest) {
      assert((Field == FieldEnd || !Field->isBitField() ||
              (getFieldBitOffset(*Field) % CharBits) == 0) &&
             "Installing but not at an aligned bitfield or limit");
      CharUnits AccessSize = BestEndOffset - BeginOffset;
      if (!AccessSize.isZero()) {
        // Add the storage member for the access unit to the record. The
        // bitfields get the offset of their storage but come afterward and
        // remain there after a stable sort.
        llvm::Type *Type;
        if (BestClipped) {
          assert(getSize(getIntNType(Context.toBits(AccessSize))) >
                     AccessSize &&
                 "Clipped access need not be clipped");
          Type = getByteArrayType(AccessSize);
        } else {
          Type = getIntNType(Context.toBits(AccessSize));
          assert(getSize(Type) == AccessSize &&
                 "Unclipped access must be clipped");
        }
        Members.push_back(StorageInfo(BeginOffset, Type));
        for (; Begin != BestEnd; ++Begin)
          if (!Begin->isZeroLengthBitField(Context))
            Members.push_back(
                MemberInfo(BeginOffset, MemberInfo::Field, nullptr, *Begin));
      }
      // Reset to start a new span.
      Field = BestEnd;
      Begin = FieldEnd;
    } else {
      assert(Field != FieldEnd && Field->isBitField() &&
             "Accumulating past end of bitfields");
      assert(!Barrier && "Accumulating across barrier");
      // Accumulate this bitfield into the current (potential) span.
      BitSizeSinceBegin += Field->getBitWidthValue(Context);
      ++Field;
    }
  }

  return Field;
}

void CGRecordLowering::accumulateBases() {
  // If we've got a primary virtual base, we need to add it with the bases.
  if (Layout.isPrimaryBaseVirtual()) {
    const CXXRecordDecl *BaseDecl = Layout.getPrimaryBase();
    Members.push_back(MemberInfo(CharUnits::Zero(), MemberInfo::Base,
                                 getStorageType(BaseDecl), BaseDecl));
  }
  // Accumulate the non-virtual bases.
  for (const auto &Base : RD->bases()) {
    if (Base.isVirtual())
      continue;

    // Bases can be zero-sized even if not technically empty if they
    // contain only a trailing array member.
    const CXXRecordDecl *BaseDecl = Base.getType()->getAsCXXRecordDecl();
    if (!isEmptyRecordForLayout(Context, Base.getType()) &&
        !Context.getASTRecordLayout(BaseDecl).getNonVirtualSize().isZero())
      Members.push_back(MemberInfo(Layout.getBaseClassOffset(BaseDecl),
          MemberInfo::Base, getStorageType(BaseDecl), BaseDecl));
  }
}

/// The AAPCS that defines that, when possible, bit-fields should
/// be accessed using containers of the declared type width:
/// When a volatile bit-field is read, and its container does not overlap with
/// any non-bit-field member or any zero length bit-field member, its container
/// must be read exactly once using the access width appropriate to the type of
/// the container. When a volatile bit-field is written, and its container does
/// not overlap with any non-bit-field member or any zero-length bit-field
/// member, its container must be read exactly once and written exactly once
/// using the access width appropriate to the type of the container. The two
/// accesses are not atomic.
///
/// Enforcing the width restriction can be disabled using
/// -fno-aapcs-bitfield-width.
void CGRecordLowering::computeVolatileBitfields() {
  if (!isAAPCS() || !Types.getCodeGenOpts().AAPCSBitfieldWidth)
    return;

  for (auto &I : BitFields) {
    const FieldDecl *Field = I.first;
    CGBitFieldInfo &Info = I.second;
    llvm::Type *ResLTy = Types.ConvertTypeForMem(Field->getType());
    // If the record alignment is less than the type width, we can't enforce a
    // aligned load, bail out.
    if ((uint64_t)(Context.toBits(Layout.getAlignment())) <
        ResLTy->getPrimitiveSizeInBits())
      continue;
    // CGRecordLowering::setBitFieldInfo() pre-adjusts the bit-field offsets
    // for big-endian targets, but it assumes a container of width
    // Info.StorageSize. Since AAPCS uses a different container size (width
    // of the type), we first undo that calculation here and redo it once
    // the bit-field offset within the new container is calculated.
    const unsigned OldOffset =
        isBE() ? Info.StorageSize - (Info.Offset + Info.Size) : Info.Offset;
    // Offset to the bit-field from the beginning of the struct.
    const unsigned AbsoluteOffset =
        Context.toBits(Info.StorageOffset) + OldOffset;

    // Container size is the width of the bit-field type.
    const unsigned StorageSize = ResLTy->getPrimitiveSizeInBits();
    // Nothing to do if the access uses the desired
    // container width and is naturally aligned.
    if (Info.StorageSize == StorageSize && (OldOffset % StorageSize == 0))
      continue;

    // Offset within the container.
    unsigned Offset = AbsoluteOffset & (StorageSize - 1);
    // Bail out if an aligned load of the container cannot cover the entire
    // bit-field. This can happen for example, if the bit-field is part of a
    // packed struct. AAPCS does not define access rules for such cases, we let
    // clang to follow its own rules.
    if (Offset + Info.Size > StorageSize)
      continue;

    // Re-adjust offsets for big-endian targets.
    if (isBE())
      Offset = StorageSize - (Offset + Info.Size);

    const CharUnits StorageOffset =
        Context.toCharUnitsFromBits(AbsoluteOffset & ~(StorageSize - 1));
    const CharUnits End = StorageOffset +
                          Context.toCharUnitsFromBits(StorageSize) -
                          CharUnits::One();

    const ASTRecordLayout &Layout =
        Context.getASTRecordLayout(Field->getParent());
    // If we access outside memory outside the record, than bail out.
    const CharUnits RecordSize = Layout.getSize();
    if (End >= RecordSize)
      continue;

    // Bail out if performing this load would access non-bit-fields members.
    bool Conflict = false;
    for (const auto *F : D->fields()) {
      // Allow sized bit-fields overlaps.
      if (F->isBitField() && !F->isZeroLengthBitField(Context))
        continue;

      const CharUnits FOffset = Context.toCharUnitsFromBits(
          Layout.getFieldOffset(F->getFieldIndex()));

      // As C11 defines, a zero sized bit-field defines a barrier, so
      // fields after and before it should be race condition free.
      // The AAPCS acknowledges it and imposes no restritions when the
      // natural container overlaps a zero-length bit-field.
      if (F->isZeroLengthBitField(Context)) {
        if (End > FOffset && StorageOffset < FOffset) {
          Conflict = true;
          break;
        }
      }

      const CharUnits FEnd =
          FOffset +
          Context.toCharUnitsFromBits(
              Types.ConvertTypeForMem(F->getType())->getPrimitiveSizeInBits()) -
          CharUnits::One();
      // If no overlap, continue.
      if (End < FOffset || FEnd < StorageOffset)
        continue;

      // The desired load overlaps a non-bit-field member, bail out.
      Conflict = true;
      break;
    }

    if (Conflict)
      continue;
    // Write the new bit-field access parameters.
    // As the storage offset now is defined as the number of elements from the
    // start of the structure, we should divide the Offset by the element size.
    Info.VolatileStorageOffset =
        StorageOffset / Context.toCharUnitsFromBits(StorageSize).getQuantity();
    Info.VolatileStorageSize = StorageSize;
    Info.VolatileOffset = Offset;
  }
}

void CGRecordLowering::accumulateVPtrs() {
  if (Layout.hasOwnVFPtr())
    Members.push_back(
        MemberInfo(CharUnits::Zero(), MemberInfo::VFPtr,
                   llvm::PointerType::getUnqual(Types.getLLVMContext())));
  if (Layout.hasOwnVBPtr())
    Members.push_back(
        MemberInfo(Layout.getVBPtrOffset(), MemberInfo::VBPtr,
                   llvm::PointerType::getUnqual(Types.getLLVMContext())));
}

CharUnits
CGRecordLowering::calculateTailClippingOffset(bool isNonVirtualBaseType) const {
  if (!RD)
    return Layout.getDataSize();

  CharUnits ScissorOffset = Layout.getNonVirtualSize();
  // In the itanium ABI, it's possible to place a vbase at a dsize that is
  // smaller than the nvsize.  Here we check to see if such a base is placed
  // before the nvsize and set the scissor offset to that, instead of the
  // nvsize.
  if (!isNonVirtualBaseType && isOverlappingVBaseABI())
    for (const auto &Base : RD->vbases()) {
      const CXXRecordDecl *BaseDecl = Base.getType()->getAsCXXRecordDecl();
      if (isEmptyRecordForLayout(Context, Base.getType()))
        continue;
      // If the vbase is a primary virtual base of some base, then it doesn't
      // get its own storage location but instead lives inside of that base.
      if (Context.isNearlyEmpty(BaseDecl) && !hasOwnStorage(RD, BaseDecl))
        continue;
      ScissorOffset = std::min(ScissorOffset,
                               Layout.getVBaseClassOffset(BaseDecl));
    }

  return ScissorOffset;
}

void CGRecordLowering::accumulateVBases() {
  for (const auto &Base : RD->vbases()) {
    const CXXRecordDecl *BaseDecl = Base.getType()->getAsCXXRecordDecl();
    if (isEmptyRecordForLayout(Context, Base.getType()))
      continue;
    CharUnits Offset = Layout.getVBaseClassOffset(BaseDecl);
    // If the vbase is a primary virtual base of some base, then it doesn't
    // get its own storage location but instead lives inside of that base.
    if (isOverlappingVBaseABI() &&
        Context.isNearlyEmpty(BaseDecl) &&
        !hasOwnStorage(RD, BaseDecl)) {
      Members.push_back(MemberInfo(Offset, MemberInfo::VBase, nullptr,
                                   BaseDecl));
      continue;
    }
    // If we've got a vtordisp, add it as a storage type.
    if (Layout.getVBaseOffsetsMap().find(BaseDecl)->second.hasVtorDisp())
      Members.push_back(StorageInfo(Offset - CharUnits::fromQuantity(4),
                                    getIntNType(32)));
    Members.push_back(MemberInfo(Offset, MemberInfo::VBase,
                                 getStorageType(BaseDecl), BaseDecl));
  }
}

bool CGRecordLowering::hasOwnStorage(const CXXRecordDecl *Decl,
                                     const CXXRecordDecl *Query) const {
  const ASTRecordLayout &DeclLayout = Context.getASTRecordLayout(Decl);
  if (DeclLayout.isPrimaryBaseVirtual() && DeclLayout.getPrimaryBase() == Query)
    return false;
  for (const auto &Base : Decl->bases())
    if (!hasOwnStorage(Base.getType()->getAsCXXRecordDecl(), Query))
      return false;
  return true;
}

void CGRecordLowering::calculateZeroInit() {
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       IsZeroInitializableAsBase && Member != MemberEnd; ++Member) {
    if (Member->Kind == MemberInfo::Field) {
      if (!Member->FD || isZeroInitializable(Member->FD))
        continue;
      IsZeroInitializable = IsZeroInitializableAsBase = false;
    } else if (Member->Kind == MemberInfo::Base ||
               Member->Kind == MemberInfo::VBase) {
      if (isZeroInitializable(Member->RD))
        continue;
      IsZeroInitializable = false;
      if (Member->Kind == MemberInfo::Base)
        IsZeroInitializableAsBase = false;
    }
  }
}

// Verify accumulateBitfields computed the correct storage representations.
void CGRecordLowering::checkBitfieldClipping(bool IsNonVirtualBaseType) const {
#ifndef NDEBUG
  auto ScissorOffset = calculateTailClippingOffset(IsNonVirtualBaseType);
  auto Tail = CharUnits::Zero();
  for (const auto &M : Members) {
    // Only members with data could possibly overlap.
    if (!M.Data)
      continue;

    assert(M.Offset >= Tail && "Bitfield access unit is not clipped");
    Tail = M.Offset + getSize(M.Data);
    assert((Tail <= ScissorOffset || M.Offset >= ScissorOffset) &&
           "Bitfield straddles scissor offset");
  }
#endif
}

void CGRecordLowering::determinePacked(bool NVBaseType) {
  if (Packed)
    return;
  CharUnits Alignment = CharUnits::One();
  CharUnits NVAlignment = CharUnits::One();
  CharUnits NVSize =
      !NVBaseType && RD ? Layout.getNonVirtualSize() : CharUnits::Zero();
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    if (!Member->Data)
      continue;
    // If any member falls at an offset that it not a multiple of its alignment,
    // then the entire record must be packed.
    if (Member->Offset % getAlignment(Member->Data))
      Packed = true;
    if (Member->Offset < NVSize)
      NVAlignment = std::max(NVAlignment, getAlignment(Member->Data));
    Alignment = std::max(Alignment, getAlignment(Member->Data));
  }
  // If the size of the record (the capstone's offset) is not a multiple of the
  // record's alignment, it must be packed.
  if (Members.back().Offset % Alignment)
    Packed = true;
  // If the non-virtual sub-object is not a multiple of the non-virtual
  // sub-object's alignment, it must be packed.  We cannot have a packed
  // non-virtual sub-object and an unpacked complete object or vise versa.
  if (NVSize % NVAlignment)
    Packed = true;
  // Update the alignment of the sentinel.
  if (!Packed)
    Members.back().Data = getIntNType(Context.toBits(Alignment));
}

void CGRecordLowering::insertPadding() {
  std::vector<std::pair<CharUnits, CharUnits> > Padding;
  CharUnits Size = CharUnits::Zero();
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    if (!Member->Data)
      continue;
    CharUnits Offset = Member->Offset;
    assert(Offset >= Size);
    // Insert padding if we need to.
    if (Offset !=
        Size.alignTo(Packed ? CharUnits::One() : getAlignment(Member->Data)))
      Padding.push_back(std::make_pair(Size, Offset - Size));
    Size = Offset + getSize(Member->Data);
  }
  if (Padding.empty())
    return;
  // Add the padding to the Members list and sort it.
  for (std::vector<std::pair<CharUnits, CharUnits> >::const_iterator
        Pad = Padding.begin(), PadEnd = Padding.end();
        Pad != PadEnd; ++Pad)
    Members.push_back(StorageInfo(Pad->first, getByteArrayType(Pad->second)));
  llvm::stable_sort(Members);
}

void CGRecordLowering::fillOutputFields() {
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    if (Member->Data)
      FieldTypes.push_back(Member->Data);
    if (Member->Kind == MemberInfo::Field) {
      if (Member->FD)
        Fields[Member->FD->getCanonicalDecl()] = FieldTypes.size() - 1;
      // A field without storage must be a bitfield.
      if (!Member->Data)
        setBitFieldInfo(Member->FD, Member->Offset, FieldTypes.back());
    } else if (Member->Kind == MemberInfo::Base)
      NonVirtualBases[Member->RD] = FieldTypes.size() - 1;
    else if (Member->Kind == MemberInfo::VBase)
      VirtualBases[Member->RD] = FieldTypes.size() - 1;
  }
}

CGBitFieldInfo CGBitFieldInfo::MakeInfo(CodeGenTypes &Types,
                                        const FieldDecl *FD,
                                        uint64_t Offset, uint64_t Size,
                                        uint64_t StorageSize,
                                        CharUnits StorageOffset) {
  // This function is vestigial from CGRecordLayoutBuilder days but is still
  // used in GCObjCRuntime.cpp.  That usage has a "fixme" attached to it that
  // when addressed will allow for the removal of this function.
  llvm::Type *Ty = Types.ConvertTypeForMem(FD->getType());
  CharUnits TypeSizeInBytes =
    CharUnits::fromQuantity(Types.getDataLayout().getTypeAllocSize(Ty));
  uint64_t TypeSizeInBits = Types.getContext().toBits(TypeSizeInBytes);

  bool IsSigned = FD->getType()->isSignedIntegerOrEnumerationType();

  if (Size > TypeSizeInBits) {
    // We have a wide bit-field. The extra bits are only used for padding, so
    // if we have a bitfield of type T, with size N:
    //
    // T t : N;
    //
    // We can just assume that it's:
    //
    // T t : sizeof(T);
    //
    Size = TypeSizeInBits;
  }

  // Reverse the bit offsets for big endian machines. Because we represent
  // a bitfield as a single large integer load, we can imagine the bits
  // counting from the most-significant-bit instead of the
  // least-significant-bit.
  if (Types.getDataLayout().isBigEndian()) {
    Offset = StorageSize - (Offset + Size);
  }

  return CGBitFieldInfo(Offset, Size, IsSigned, StorageSize, StorageOffset);
}

std::unique_ptr<CGRecordLayout>
CodeGenTypes::ComputeRecordLayout(const RecordDecl *D, llvm::StructType *Ty) {
  CGRecordLowering Builder(*this, D, /*Packed=*/false);

  Builder.lower(/*NonVirtualBaseType=*/false);

  // If we're in C++, compute the base subobject type.
  llvm::StructType *BaseTy = nullptr;
  if (isa<CXXRecordDecl>(D)) {
    BaseTy = Ty;
    if (Builder.Layout.getNonVirtualSize() != Builder.Layout.getSize()) {
      CGRecordLowering BaseBuilder(*this, D, /*Packed=*/Builder.Packed);
      BaseBuilder.lower(/*NonVirtualBaseType=*/true);
      BaseTy = llvm::StructType::create(
          getLLVMContext(), BaseBuilder.FieldTypes, "", BaseBuilder.Packed);
      addRecordTypeName(D, BaseTy, ".base");
      // BaseTy and Ty must agree on their packedness for getLLVMFieldNo to work
      // on both of them with the same index.
      assert(Builder.Packed == BaseBuilder.Packed &&
             "Non-virtual and complete types must agree on packedness");
    }
  }

  // Fill in the struct *after* computing the base type.  Filling in the body
  // signifies that the type is no longer opaque and record layout is complete,
  // but we may need to recursively layout D while laying D out as a base type.
  Ty->setBody(Builder.FieldTypes, Builder.Packed);

  auto RL = std::make_unique<CGRecordLayout>(
      Ty, BaseTy, (bool)Builder.IsZeroInitializable,
      (bool)Builder.IsZeroInitializableAsBase);

  RL->NonVirtualBases.swap(Builder.NonVirtualBases);
  RL->CompleteObjectVirtualBases.swap(Builder.VirtualBases);

  // Add all the field numbers.
  RL->FieldInfo.swap(Builder.Fields);

  // Add bitfield info.
  RL->BitFields.swap(Builder.BitFields);

  // Dump the layout, if requested.
  if (getContext().getLangOpts().DumpRecordLayouts) {
    llvm::outs() << "\n*** Dumping IRgen Record Layout\n";
    llvm::outs() << "Record: ";
    D->dump(llvm::outs());
    llvm::outs() << "\nLayout: ";
    RL->print(llvm::outs());
  }

#ifndef NDEBUG
  // Verify that the computed LLVM struct size matches the AST layout size.
  const ASTRecordLayout &Layout = getContext().getASTRecordLayout(D);

  uint64_t TypeSizeInBits = getContext().toBits(Layout.getSize());
  assert(TypeSizeInBits == getDataLayout().getTypeAllocSizeInBits(Ty) &&
         "Type size mismatch!");

  if (BaseTy) {
    CharUnits NonVirtualSize  = Layout.getNonVirtualSize();

    uint64_t AlignedNonVirtualTypeSizeInBits =
      getContext().toBits(NonVirtualSize);

    assert(AlignedNonVirtualTypeSizeInBits ==
           getDataLayout().getTypeAllocSizeInBits(BaseTy) &&
           "Type size mismatch!");
  }

  // Verify that the LLVM and AST field offsets agree.
  llvm::StructType *ST = RL->getLLVMType();
  const llvm::StructLayout *SL = getDataLayout().getStructLayout(ST);

  const ASTRecordLayout &AST_RL = getContext().getASTRecordLayout(D);
  RecordDecl::field_iterator it = D->field_begin();
  for (unsigned i = 0, e = AST_RL.getFieldCount(); i != e; ++i, ++it) {
    const FieldDecl *FD = *it;

    // Ignore zero-sized fields.
    if (isEmptyFieldForLayout(getContext(), FD))
      continue;

    // For non-bit-fields, just check that the LLVM struct offset matches the
    // AST offset.
    if (!FD->isBitField()) {
      unsigned FieldNo = RL->getLLVMFieldNo(FD);
      assert(AST_RL.getFieldOffset(i) == SL->getElementOffsetInBits(FieldNo) &&
             "Invalid field offset!");
      continue;
    }

    // Ignore unnamed bit-fields.
    if (!FD->getDeclName())
      continue;

    const CGBitFieldInfo &Info = RL->getBitFieldInfo(FD);
    llvm::Type *ElementTy = ST->getTypeAtIndex(RL->getLLVMFieldNo(FD));

    // Unions have overlapping elements dictating their layout, but for
    // non-unions we can verify that this section of the layout is the exact
    // expected size.
    if (D->isUnion()) {
      // For unions we verify that the start is zero and the size
      // is in-bounds. However, on BE systems, the offset may be non-zero, but
      // the size + offset should match the storage size in that case as it
      // "starts" at the back.
      if (getDataLayout().isBigEndian())
        assert(static_cast<unsigned>(Info.Offset + Info.Size) ==
               Info.StorageSize &&
               "Big endian union bitfield does not end at the back");
      else
        assert(Info.Offset == 0 &&
               "Little endian union bitfield with a non-zero offset");
      assert(Info.StorageSize <= SL->getSizeInBits() &&
             "Union not large enough for bitfield storage");
    } else {
      assert((Info.StorageSize ==
                  getDataLayout().getTypeAllocSizeInBits(ElementTy) ||
              Info.VolatileStorageSize ==
                  getDataLayout().getTypeAllocSizeInBits(ElementTy)) &&
             "Storage size does not match the element type size");
    }
    assert(Info.Size > 0 && "Empty bitfield!");
    assert(static_cast<unsigned>(Info.Offset) + Info.Size <= Info.StorageSize &&
           "Bitfield outside of its allocated storage");
  }
#endif

  return RL;
}

void CGRecordLayout::print(raw_ostream &OS) const {
  OS << "<CGRecordLayout\n";
  OS << "  LLVMType:" << *CompleteObjectType << "\n";
  if (BaseSubobjectType)
    OS << "  NonVirtualBaseLLVMType:" << *BaseSubobjectType << "\n";
  OS << "  IsZeroInitializable:" << IsZeroInitializable << "\n";
  OS << "  BitFields:[\n";

  // Print bit-field infos in declaration order.
  std::vector<std::pair<unsigned, const CGBitFieldInfo*> > BFIs;
  for (llvm::DenseMap<const FieldDecl*, CGBitFieldInfo>::const_iterator
         it = BitFields.begin(), ie = BitFields.end();
       it != ie; ++it) {
    const RecordDecl *RD = it->first->getParent();
    unsigned Index = 0;
    for (RecordDecl::field_iterator
           it2 = RD->field_begin(); *it2 != it->first; ++it2)
      ++Index;
    BFIs.push_back(std::make_pair(Index, &it->second));
  }
  llvm::array_pod_sort(BFIs.begin(), BFIs.end());
  for (unsigned i = 0, e = BFIs.size(); i != e; ++i) {
    OS.indent(4);
    BFIs[i].second->print(OS);
    OS << "\n";
  }

  OS << "]>\n";
}

LLVM_DUMP_METHOD void CGRecordLayout::dump() const {
  print(llvm::errs());
}

void CGBitFieldInfo::print(raw_ostream &OS) const {
  OS << "<CGBitFieldInfo"
     << " Offset:" << Offset << " Size:" << Size << " IsSigned:" << IsSigned
     << " StorageSize:" << StorageSize
     << " StorageOffset:" << StorageOffset.getQuantity()
     << " VolatileOffset:" << VolatileOffset
     << " VolatileStorageSize:" << VolatileStorageSize
     << " VolatileStorageOffset:" << VolatileStorageOffset.getQuantity() << ">";
}

LLVM_DUMP_METHOD void CGBitFieldInfo::dump() const {
  print(llvm::errs());
}
