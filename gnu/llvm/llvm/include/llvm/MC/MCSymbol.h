//===- MCSymbol.h - Machine Code Symbols ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCSymbol class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSYMBOL_H
#define LLVM_MC_MCSYMBOL_H

#include "llvm/ADT/StringMapEntry.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCSymbolTableEntry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace llvm {

class MCAsmInfo;
class MCContext;
class MCSection;
class raw_ostream;

/// MCSymbol - Instances of this class represent a symbol name in the MC file,
/// and MCSymbols are created and uniqued by the MCContext class.  MCSymbols
/// should only be constructed with valid names for the object file.
///
/// If the symbol is defined/emitted into the current translation unit, the
/// Section member is set to indicate what section it lives in.  Otherwise, if
/// it is a reference to an external entity, it has a null section.
class MCSymbol {
protected:
  /// The kind of the symbol.  If it is any value other than unset then this
  /// class is actually one of the appropriate subclasses of MCSymbol.
  enum SymbolKind {
    SymbolKindUnset,
    SymbolKindCOFF,
    SymbolKindELF,
    SymbolKindGOFF,
    SymbolKindMachO,
    SymbolKindWasm,
    SymbolKindXCOFF,
  };

  /// A symbol can contain an Offset, or Value, or be Common, but never more
  /// than one of these.
  enum Contents : uint8_t {
    SymContentsUnset,
    SymContentsOffset,
    SymContentsVariable,
    SymContentsCommon,
    SymContentsTargetCommon, // Index stores the section index
  };

  // Special sentinel value for the absolute pseudo fragment.
  static MCFragment *AbsolutePseudoFragment;

  /// If a symbol has a Fragment, the section is implied, so we only need
  /// one pointer.
  /// The special AbsolutePseudoFragment value is for absolute symbols.
  /// If this is a variable symbol, this caches the variable value's fragment.
  /// FIXME: We might be able to simplify this by having the asm streamer create
  /// dummy fragments.
  /// If this is a section, then it gives the symbol is defined in. This is null
  /// for undefined symbols.
  ///
  /// If this is a fragment, then it gives the fragment this symbol's value is
  /// relative to, if any.
  mutable MCFragment *Fragment = nullptr;

  /// True if this symbol is named.  A named symbol will have a pointer to the
  /// name allocated in the bytes immediately prior to the MCSymbol.
  unsigned HasName : 1;

  /// IsTemporary - True if this is an assembler temporary label, which
  /// typically does not survive in the .o file's symbol table.  Usually
  /// "Lfoo" or ".foo".
  unsigned IsTemporary : 1;

  /// True if this symbol can be redefined.
  unsigned IsRedefinable : 1;

  /// IsUsed - True if this symbol has been used.
  mutable unsigned IsUsed : 1;

  mutable unsigned IsRegistered : 1;

  /// True if this symbol is visible outside this translation unit. Note: ELF
  /// uses binding instead of this bit.
  mutable unsigned IsExternal : 1;

  /// Mach-O specific: This symbol is private extern.
  mutable unsigned IsPrivateExtern : 1;

  /// This symbol is weak external.
  mutable unsigned IsWeakExternal : 1;

  /// LLVM RTTI discriminator. This is actually a SymbolKind enumerator, but is
  /// unsigned to avoid sign extension and achieve better bitpacking with MSVC.
  unsigned Kind : 3;

  /// True if we have created a relocation that uses this symbol.
  mutable unsigned IsUsedInReloc : 1;

  /// This is actually a Contents enumerator, but is unsigned to avoid sign
  /// extension and achieve better bitpacking with MSVC.
  unsigned SymbolContents : 3;

  /// The alignment of the symbol if it is 'common'.
  ///
  /// Internally, this is stored as log2(align) + 1.
  /// We reserve 5 bits to encode this value which allows the following values
  /// 0b00000 -> unset
  /// 0b00001 -> 1ULL <<  0 = 1
  /// 0b00010 -> 1ULL <<  1 = 2
  /// 0b00011 -> 1ULL <<  2 = 4
  /// ...
  /// 0b11111 -> 1ULL << 30 = 1 GiB
  enum : unsigned { NumCommonAlignmentBits = 5 };
  unsigned CommonAlignLog2 : NumCommonAlignmentBits;

  /// The Flags field is used by object file implementations to store
  /// additional per symbol information which is not easily classified.
  enum : unsigned { NumFlagsBits = 16 };
  mutable uint32_t Flags : NumFlagsBits;

  /// Index field, for use by the object file implementation.
  mutable uint32_t Index = 0;

  union {
    /// The offset to apply to the fragment address to form this symbol's value.
    uint64_t Offset;

    /// The size of the symbol, if it is 'common'.
    uint64_t CommonSize;

    /// If non-null, the value for a variable symbol.
    const MCExpr *Value;
  };

  // MCContext creates and uniques these.
  friend class MCExpr;
  friend class MCContext;

  /// The name for a symbol.
  /// MCSymbol contains a uint64_t so is probably aligned to 8.  On a 32-bit
  /// system, the name is a pointer so isn't going to satisfy the 8 byte
  /// alignment of uint64_t.  Account for that here.
  using NameEntryStorageTy = union {
    const MCSymbolTableEntry *NameEntry;
    uint64_t AlignmentPadding;
  };

  MCSymbol(SymbolKind Kind, const MCSymbolTableEntry *Name, bool isTemporary)
      : IsTemporary(isTemporary), IsRedefinable(false), IsUsed(false),
        IsRegistered(false), IsExternal(false), IsPrivateExtern(false),
        IsWeakExternal(false), Kind(Kind), IsUsedInReloc(false),
        SymbolContents(SymContentsUnset), CommonAlignLog2(0), Flags(0) {
    Offset = 0;
    HasName = !!Name;
    if (Name)
      getNameEntryPtr() = Name;
  }

  // Provide custom new/delete as we will only allocate space for a name
  // if we need one.
  void *operator new(size_t s, const MCSymbolTableEntry *Name, MCContext &Ctx);

private:
  void operator delete(void *);
  /// Placement delete - required by std, but never called.
  void operator delete(void*, unsigned) {
    llvm_unreachable("Constructor throws?");
  }
  /// Placement delete - required by std, but never called.
  void operator delete(void*, unsigned, bool) {
    llvm_unreachable("Constructor throws?");
  }

  /// Get a reference to the name field.  Requires that we have a name
  const MCSymbolTableEntry *&getNameEntryPtr() {
    assert(HasName && "Name is required");
    NameEntryStorageTy *Name = reinterpret_cast<NameEntryStorageTy *>(this);
    return (*(Name - 1)).NameEntry;
  }
  const MCSymbolTableEntry *&getNameEntryPtr() const {
    return const_cast<MCSymbol*>(this)->getNameEntryPtr();
  }

public:
  MCSymbol(const MCSymbol &) = delete;
  MCSymbol &operator=(const MCSymbol &) = delete;

  /// getName - Get the symbol name.
  StringRef getName() const {
    if (!HasName)
      return StringRef();

    return getNameEntryPtr()->first();
  }

  bool isRegistered() const { return IsRegistered; }
  void setIsRegistered(bool Value) const { IsRegistered = Value; }

  void setUsedInReloc() const { IsUsedInReloc = true; }
  bool isUsedInReloc() const { return IsUsedInReloc; }

  /// \name Accessors
  /// @{

  /// isTemporary - Check if this is an assembler temporary symbol.
  bool isTemporary() const { return IsTemporary; }

  /// isUsed - Check if this is used.
  bool isUsed() const { return IsUsed; }

  /// Check if this symbol is redefinable.
  bool isRedefinable() const { return IsRedefinable; }
  /// Mark this symbol as redefinable.
  void setRedefinable(bool Value) { IsRedefinable = Value; }
  /// Prepare this symbol to be redefined.
  void redefineIfPossible() {
    if (IsRedefinable) {
      if (SymbolContents == SymContentsVariable) {
        Value = nullptr;
        SymbolContents = SymContentsUnset;
      }
      setUndefined();
      IsRedefinable = false;
    }
  }

  /// @}
  /// \name Associated Sections
  /// @{

  /// isDefined - Check if this symbol is defined (i.e., it has an address).
  ///
  /// Defined symbols are either absolute or in some section.
  bool isDefined() const { return !isUndefined(); }

  /// isInSection - Check if this symbol is defined in some section (i.e., it
  /// is defined but not absolute).
  bool isInSection() const {
    return isDefined() && !isAbsolute();
  }

  /// isUndefined - Check if this symbol undefined (i.e., implicitly defined).
  bool isUndefined(bool SetUsed = true) const {
    return getFragment(SetUsed) == nullptr;
  }

  /// isAbsolute - Check if this is an absolute symbol.
  bool isAbsolute() const {
    return getFragment() == AbsolutePseudoFragment;
  }

  /// Get the section associated with a defined, non-absolute symbol.
  MCSection &getSection() const {
    assert(isInSection() && "Invalid accessor!");
    return *getFragment()->getParent();
  }

  /// Mark the symbol as defined in the fragment \p F.
  void setFragment(MCFragment *F) const {
    assert(!isVariable() && "Cannot set fragment of variable");
    Fragment = F;
  }

  /// Mark the symbol as undefined.
  void setUndefined() { Fragment = nullptr; }

  bool isELF() const { return Kind == SymbolKindELF; }

  bool isCOFF() const { return Kind == SymbolKindCOFF; }

  bool isGOFF() const { return Kind == SymbolKindGOFF; }

  bool isMachO() const { return Kind == SymbolKindMachO; }

  bool isWasm() const { return Kind == SymbolKindWasm; }

  bool isXCOFF() const { return Kind == SymbolKindXCOFF; }

  /// @}
  /// \name Variable Symbols
  /// @{

  /// isVariable - Check if this is a variable symbol.
  bool isVariable() const {
    return SymbolContents == SymContentsVariable;
  }

  /// getVariableValue - Get the value for variable symbols.
  const MCExpr *getVariableValue(bool SetUsed = true) const {
    assert(isVariable() && "Invalid accessor!");
    IsUsed |= SetUsed;
    return Value;
  }

  void setVariableValue(const MCExpr *Value);

  /// @}

  /// Get the (implementation defined) index.
  uint32_t getIndex() const {
    return Index;
  }

  /// Set the (implementation defined) index.
  void setIndex(uint32_t Value) const {
    Index = Value;
  }

  bool isUnset() const { return SymbolContents == SymContentsUnset; }

  uint64_t getOffset() const {
    assert((SymbolContents == SymContentsUnset ||
            SymbolContents == SymContentsOffset) &&
           "Cannot get offset for a common/variable symbol");
    return Offset;
  }
  void setOffset(uint64_t Value) {
    assert((SymbolContents == SymContentsUnset ||
            SymbolContents == SymContentsOffset) &&
           "Cannot set offset for a common/variable symbol");
    Offset = Value;
    SymbolContents = SymContentsOffset;
  }

  /// Return the size of a 'common' symbol.
  uint64_t getCommonSize() const {
    assert(isCommon() && "Not a 'common' symbol!");
    return CommonSize;
  }

  /// Mark this symbol as being 'common'.
  ///
  /// \param Size - The size of the symbol.
  /// \param Alignment - The alignment of the symbol.
  /// \param Target - Is the symbol a target-specific common-like symbol.
  void setCommon(uint64_t Size, Align Alignment, bool Target = false) {
    assert(getOffset() == 0);
    CommonSize = Size;
    SymbolContents = Target ? SymContentsTargetCommon : SymContentsCommon;

    unsigned Log2Align = encode(Alignment);
    assert(Log2Align < (1U << NumCommonAlignmentBits) &&
           "Out of range alignment");
    CommonAlignLog2 = Log2Align;
  }

  ///  Return the alignment of a 'common' symbol.
  MaybeAlign getCommonAlignment() const {
    assert(isCommon() && "Not a 'common' symbol!");
    return decodeMaybeAlign(CommonAlignLog2);
  }

  /// Declare this symbol as being 'common'.
  ///
  /// \param Size - The size of the symbol.
  /// \param Alignment - The alignment of the symbol.
  /// \param Target - Is the symbol a target-specific common-like symbol.
  /// \return True if symbol was already declared as a different type
  bool declareCommon(uint64_t Size, Align Alignment, bool Target = false) {
    assert(isCommon() || getOffset() == 0);
    if(isCommon()) {
      if (CommonSize != Size || getCommonAlignment() != Alignment ||
          isTargetCommon() != Target)
        return true;
    } else
      setCommon(Size, Alignment, Target);
    return false;
  }

  /// Is this a 'common' symbol.
  bool isCommon() const {
    return SymbolContents == SymContentsCommon ||
           SymbolContents == SymContentsTargetCommon;
  }

  /// Is this a target-specific common-like symbol.
  bool isTargetCommon() const {
    return SymbolContents == SymContentsTargetCommon;
  }

  MCFragment *getFragment(bool SetUsed = true) const {
    if (Fragment || !isVariable() || isWeakExternal())
      return Fragment;
    // If the symbol is a non-weak alias, get information about
    // the aliasee. (Don't try to resolve weak aliases.)
    Fragment = getVariableValue(SetUsed)->findAssociatedFragment();
    return Fragment;
  }

  // For ELF, use MCSymbolELF::setBinding instead.
  bool isExternal() const { return IsExternal; }
  void setExternal(bool Value) const { IsExternal = Value; }

  // COFF-specific
  bool isWeakExternal() const { return IsWeakExternal; }

  /// print - Print the value to the stream \p OS.
  void print(raw_ostream &OS, const MCAsmInfo *MAI) const;

  /// dump - Print the value to stderr.
  void dump() const;

protected:
  /// Get the (implementation defined) symbol flags.
  uint32_t getFlags() const { return Flags; }

  /// Set the (implementation defined) symbol flags.
  void setFlags(uint32_t Value) const {
    assert(Value < (1U << NumFlagsBits) && "Out of range flags");
    Flags = Value;
  }

  /// Modify the flags via a mask
  void modifyFlags(uint32_t Value, uint32_t Mask) const {
    assert(Value < (1U << NumFlagsBits) && "Out of range flags");
    Flags = (Flags & ~Mask) | Value;
  }
};

inline raw_ostream &operator<<(raw_ostream &OS, const MCSymbol &Sym) {
  Sym.print(OS, nullptr);
  return OS;
}

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOL_H
