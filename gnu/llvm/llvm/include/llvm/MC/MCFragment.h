//===- MCFragment.h - Fragment type hierarchy -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCFRAGMENT_H
#define LLVM_MC_MCFRAGMENT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>
#include <utility>

namespace llvm {

class MCAssembler;
class MCObjectStreamer;
class MCSection;
class MCSubtargetInfo;
class MCSymbol;

class MCFragment {
  friend class MCAssembler;
  friend class MCObjectStreamer;
  friend class MCSection;

public:
  enum FragmentType : uint8_t {
    FT_Align,
    FT_Data,
    FT_CompactEncodedInst,
    FT_Fill,
    FT_Nops,
    FT_Relaxable,
    FT_Org,
    FT_Dwarf,
    FT_DwarfFrame,
    FT_LEB,
    FT_BoundaryAlign,
    FT_SymbolId,
    FT_CVInlineLines,
    FT_CVDefRange,
    FT_PseudoProbe,
    FT_Dummy
  };

private:
  // The next fragment within the section.
  MCFragment *Next = nullptr;

  /// The data for the section this fragment is in.
  MCSection *Parent = nullptr;

  /// The offset of this fragment in its section.
  uint64_t Offset = 0;

  /// The layout order of this fragment.
  unsigned LayoutOrder = 0;

  FragmentType Kind;

protected:
  bool HasInstructions : 1;
  bool LinkerRelaxable : 1;

  MCFragment(FragmentType Kind, bool HasInstructions);

public:
  MCFragment() = delete;
  MCFragment(const MCFragment &) = delete;
  MCFragment &operator=(const MCFragment &) = delete;

  /// Destroys the current fragment.
  ///
  /// This must be used instead of delete as MCFragment is non-virtual.
  /// This method will dispatch to the appropriate subclass.
  void destroy();

  MCFragment *getNext() const { return Next; }

  FragmentType getKind() const { return Kind; }

  MCSection *getParent() const { return Parent; }
  void setParent(MCSection *Value) { Parent = Value; }

  const MCSymbol *getAtom() const;

  unsigned getLayoutOrder() const { return LayoutOrder; }
  void setLayoutOrder(unsigned Value) { LayoutOrder = Value; }

  /// Does this fragment have instructions emitted into it? By default
  /// this is false, but specific fragment types may set it to true.
  bool hasInstructions() const { return HasInstructions; }

  void dump() const;
};

class MCDummyFragment : public MCFragment {
public:
  explicit MCDummyFragment() : MCFragment(FT_Dummy, false) {}

  static bool classof(const MCFragment *F) { return F->getKind() == FT_Dummy; }
};

/// Interface implemented by fragments that contain encoded instructions and/or
/// data.
///
class MCEncodedFragment : public MCFragment {
  /// Should this fragment be aligned to the end of a bundle?
  bool AlignToBundleEnd = false;

  uint8_t BundlePadding = 0;

protected:
  MCEncodedFragment(MCFragment::FragmentType FType, bool HasInstructions)
      : MCFragment(FType, HasInstructions) {}

  /// The MCSubtargetInfo in effect when the instruction was encoded.
  /// It must be non-null for instructions.
  const MCSubtargetInfo *STI = nullptr;

public:
  static bool classof(const MCFragment *F) {
    MCFragment::FragmentType Kind = F->getKind();
    switch (Kind) {
    default:
      return false;
    case MCFragment::FT_Relaxable:
    case MCFragment::FT_CompactEncodedInst:
    case MCFragment::FT_Data:
    case MCFragment::FT_Dwarf:
    case MCFragment::FT_DwarfFrame:
    case MCFragment::FT_PseudoProbe:
      return true;
    }
  }

  /// Should this fragment be placed at the end of an aligned bundle?
  bool alignToBundleEnd() const { return AlignToBundleEnd; }
  void setAlignToBundleEnd(bool V) { AlignToBundleEnd = V; }

  /// Get the padding size that must be inserted before this fragment.
  /// Used for bundling. By default, no padding is inserted.
  /// Note that padding size is restricted to 8 bits. This is an optimization
  /// to reduce the amount of space used for each fragment. In practice, larger
  /// padding should never be required.
  uint8_t getBundlePadding() const { return BundlePadding; }

  /// Set the padding size for this fragment. By default it's a no-op,
  /// and only some fragments have a meaningful implementation.
  void setBundlePadding(uint8_t N) { BundlePadding = N; }

  /// Retrieve the MCSubTargetInfo in effect when the instruction was encoded.
  /// Guaranteed to be non-null if hasInstructions() == true
  const MCSubtargetInfo *getSubtargetInfo() const { return STI; }

  /// Record that the fragment contains instructions with the MCSubtargetInfo in
  /// effect when the instruction was encoded.
  void setHasInstructions(const MCSubtargetInfo &STI) {
    HasInstructions = true;
    this->STI = &STI;
  }
};

/// Interface implemented by fragments that contain encoded instructions and/or
/// data.
///
template<unsigned ContentsSize>
class MCEncodedFragmentWithContents : public MCEncodedFragment {
  SmallVector<char, ContentsSize> Contents;

protected:
  MCEncodedFragmentWithContents(MCFragment::FragmentType FType,
                                bool HasInstructions)
      : MCEncodedFragment(FType, HasInstructions) {}

public:
  SmallVectorImpl<char> &getContents() { return Contents; }
  const SmallVectorImpl<char> &getContents() const { return Contents; }
};

/// Interface implemented by fragments that contain encoded instructions and/or
/// data and also have fixups registered.
///
template<unsigned ContentsSize, unsigned FixupsSize>
class MCEncodedFragmentWithFixups :
  public MCEncodedFragmentWithContents<ContentsSize> {

  /// The list of fixups in this fragment.
  SmallVector<MCFixup, FixupsSize> Fixups;

protected:
  MCEncodedFragmentWithFixups(MCFragment::FragmentType FType,
                              bool HasInstructions)
      : MCEncodedFragmentWithContents<ContentsSize>(FType, HasInstructions) {}

public:

  using const_fixup_iterator = SmallVectorImpl<MCFixup>::const_iterator;
  using fixup_iterator = SmallVectorImpl<MCFixup>::iterator;

  SmallVectorImpl<MCFixup> &getFixups() { return Fixups; }
  const SmallVectorImpl<MCFixup> &getFixups() const { return Fixups; }

  fixup_iterator fixup_begin() { return Fixups.begin(); }
  const_fixup_iterator fixup_begin() const { return Fixups.begin(); }

  fixup_iterator fixup_end() { return Fixups.end(); }
  const_fixup_iterator fixup_end() const { return Fixups.end(); }

  static bool classof(const MCFragment *F) {
    MCFragment::FragmentType Kind = F->getKind();
    return Kind == MCFragment::FT_Relaxable || Kind == MCFragment::FT_Data ||
           Kind == MCFragment::FT_CVDefRange || Kind == MCFragment::FT_Dwarf ||
           Kind == MCFragment::FT_DwarfFrame;
  }
};

/// Fragment for data and encoded instructions.
///
class MCDataFragment : public MCEncodedFragmentWithFixups<32, 4> {
public:
  MCDataFragment() : MCEncodedFragmentWithFixups<32, 4>(FT_Data, false) {}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Data;
  }

  bool isLinkerRelaxable() const { return LinkerRelaxable; }
  void setLinkerRelaxable() { LinkerRelaxable = true; }
};

/// This is a compact (memory-size-wise) fragment for holding an encoded
/// instruction (non-relaxable) that has no fixups registered. When applicable,
/// it can be used instead of MCDataFragment and lead to lower memory
/// consumption.
///
class MCCompactEncodedInstFragment : public MCEncodedFragmentWithContents<4> {
public:
  MCCompactEncodedInstFragment()
      : MCEncodedFragmentWithContents(FT_CompactEncodedInst, true) {}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_CompactEncodedInst;
  }
};

/// A relaxable fragment holds on to its MCInst, since it may need to be
/// relaxed during the assembler layout and relaxation stage.
///
class MCRelaxableFragment : public MCEncodedFragmentWithFixups<8, 1> {

  /// The instruction this is a fragment for.
  MCInst Inst;
  /// Can we auto pad the instruction?
  bool AllowAutoPadding = false;

public:
  MCRelaxableFragment(const MCInst &Inst, const MCSubtargetInfo &STI)
      : MCEncodedFragmentWithFixups(FT_Relaxable, true), Inst(Inst) {
    this->STI = &STI;
  }

  const MCInst &getInst() const { return Inst; }
  void setInst(const MCInst &Value) { Inst = Value; }

  bool getAllowAutoPadding() const { return AllowAutoPadding; }
  void setAllowAutoPadding(bool V) { AllowAutoPadding = V; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Relaxable;
  }
};

class MCAlignFragment : public MCFragment {
  /// The alignment to ensure, in bytes.
  Align Alignment;

  /// Flag to indicate that (optimal) NOPs should be emitted instead
  /// of using the provided value. The exact interpretation of this flag is
  /// target dependent.
  bool EmitNops : 1;

  /// Value to use for filling padding bytes.
  int64_t Value;

  /// The size of the integer (in bytes) of \p Value.
  unsigned ValueSize;

  /// The maximum number of bytes to emit; if the alignment
  /// cannot be satisfied in this width then this fragment is ignored.
  unsigned MaxBytesToEmit;

  /// When emitting Nops some subtargets have specific nop encodings.
  const MCSubtargetInfo *STI = nullptr;

public:
  MCAlignFragment(Align Alignment, int64_t Value, unsigned ValueSize,
                  unsigned MaxBytesToEmit)
      : MCFragment(FT_Align, false), Alignment(Alignment), EmitNops(false),
        Value(Value), ValueSize(ValueSize), MaxBytesToEmit(MaxBytesToEmit) {}

  Align getAlignment() const { return Alignment; }

  int64_t getValue() const { return Value; }

  unsigned getValueSize() const { return ValueSize; }

  unsigned getMaxBytesToEmit() const { return MaxBytesToEmit; }

  bool hasEmitNops() const { return EmitNops; }
  void setEmitNops(bool Value, const MCSubtargetInfo *STI) {
    EmitNops = Value;
    this->STI = STI;
  }

  const MCSubtargetInfo *getSubtargetInfo() const { return STI; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Align;
  }
};

class MCFillFragment : public MCFragment {
  uint8_t ValueSize;
  /// Value to use for filling bytes.
  uint64_t Value;
  /// The number of bytes to insert.
  const MCExpr &NumValues;

  /// Source location of the directive that this fragment was created for.
  SMLoc Loc;

public:
  MCFillFragment(uint64_t Value, uint8_t VSize, const MCExpr &NumValues,
                 SMLoc Loc)
      : MCFragment(FT_Fill, false), ValueSize(VSize), Value(Value),
        NumValues(NumValues), Loc(Loc) {}

  uint64_t getValue() const { return Value; }
  uint8_t getValueSize() const { return ValueSize; }
  const MCExpr &getNumValues() const { return NumValues; }

  SMLoc getLoc() const { return Loc; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Fill;
  }
};

class MCNopsFragment : public MCFragment {
  /// The number of bytes to insert.
  int64_t Size;
  /// Maximum number of bytes allowed in each NOP instruction.
  int64_t ControlledNopLength;

  /// Source location of the directive that this fragment was created for.
  SMLoc Loc;

  /// When emitting Nops some subtargets have specific nop encodings.
  const MCSubtargetInfo &STI;

public:
  MCNopsFragment(int64_t NumBytes, int64_t ControlledNopLength, SMLoc L,
                 const MCSubtargetInfo &STI)
      : MCFragment(FT_Nops, false), Size(NumBytes),
        ControlledNopLength(ControlledNopLength), Loc(L), STI(STI) {}

  int64_t getNumBytes() const { return Size; }
  int64_t getControlledNopLength() const { return ControlledNopLength; }

  SMLoc getLoc() const { return Loc; }

  const MCSubtargetInfo *getSubtargetInfo() const { return &STI; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Nops;
  }
};

class MCOrgFragment : public MCFragment {
  /// Value to use for filling bytes.
  int8_t Value;

  /// The offset this fragment should start at.
  const MCExpr *Offset;

  /// Source location of the directive that this fragment was created for.
  SMLoc Loc;

public:
  MCOrgFragment(const MCExpr &Offset, int8_t Value, SMLoc Loc)
      : MCFragment(FT_Org, false), Value(Value), Offset(&Offset), Loc(Loc) {}

  const MCExpr &getOffset() const { return *Offset; }

  uint8_t getValue() const { return Value; }

  SMLoc getLoc() const { return Loc; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Org;
  }
};

class MCLEBFragment final : public MCEncodedFragmentWithFixups<8, 0> {
  /// True if this is a sleb128, false if uleb128.
  bool IsSigned;

  /// The value this fragment should contain.
  const MCExpr *Value;

public:
  MCLEBFragment(const MCExpr &Value, bool IsSigned)
      : MCEncodedFragmentWithFixups<8, 0>(FT_LEB, false), IsSigned(IsSigned),
        Value(&Value) {
    getContents().push_back(0);
  }

  const MCExpr &getValue() const { return *Value; }
  void setValue(const MCExpr *Expr) { Value = Expr; }

  bool isSigned() const { return IsSigned; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_LEB;
  }
};

class MCDwarfLineAddrFragment : public MCEncodedFragmentWithFixups<8, 1> {
  /// The value of the difference between the two line numbers
  /// between two .loc dwarf directives.
  int64_t LineDelta;

  /// The expression for the difference of the two symbols that
  /// make up the address delta between two .loc dwarf directives.
  const MCExpr *AddrDelta;

public:
  MCDwarfLineAddrFragment(int64_t LineDelta, const MCExpr &AddrDelta)
      : MCEncodedFragmentWithFixups<8, 1>(FT_Dwarf, false),
        LineDelta(LineDelta), AddrDelta(&AddrDelta) {}

  int64_t getLineDelta() const { return LineDelta; }

  const MCExpr &getAddrDelta() const { return *AddrDelta; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Dwarf;
  }
};

class MCDwarfCallFrameFragment : public MCEncodedFragmentWithFixups<8, 1> {
  /// The expression for the difference of the two symbols that
  /// make up the address delta between two .cfi_* dwarf directives.
  const MCExpr *AddrDelta;

public:
  MCDwarfCallFrameFragment(const MCExpr &AddrDelta)
      : MCEncodedFragmentWithFixups<8, 1>(FT_DwarfFrame, false),
        AddrDelta(&AddrDelta) {}

  const MCExpr &getAddrDelta() const { return *AddrDelta; }
  void setAddrDelta(const MCExpr *E) { AddrDelta = E; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_DwarfFrame;
  }
};

/// Represents a symbol table index fragment.
class MCSymbolIdFragment : public MCFragment {
  const MCSymbol *Sym;

public:
  MCSymbolIdFragment(const MCSymbol *Sym)
      : MCFragment(FT_SymbolId, false), Sym(Sym) {}

  const MCSymbol *getSymbol() { return Sym; }
  const MCSymbol *getSymbol() const { return Sym; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_SymbolId;
  }
};

/// Fragment representing the binary annotations produced by the
/// .cv_inline_linetable directive.
class MCCVInlineLineTableFragment : public MCFragment {
  unsigned SiteFuncId;
  unsigned StartFileId;
  unsigned StartLineNum;
  const MCSymbol *FnStartSym;
  const MCSymbol *FnEndSym;
  SmallString<8> Contents;

  /// CodeViewContext has the real knowledge about this format, so let it access
  /// our members.
  friend class CodeViewContext;

public:
  MCCVInlineLineTableFragment(unsigned SiteFuncId, unsigned StartFileId,
                              unsigned StartLineNum, const MCSymbol *FnStartSym,
                              const MCSymbol *FnEndSym)
      : MCFragment(FT_CVInlineLines, false), SiteFuncId(SiteFuncId),
        StartFileId(StartFileId), StartLineNum(StartLineNum),
        FnStartSym(FnStartSym), FnEndSym(FnEndSym) {}

  const MCSymbol *getFnStartSym() const { return FnStartSym; }
  const MCSymbol *getFnEndSym() const { return FnEndSym; }

  SmallString<8> &getContents() { return Contents; }
  const SmallString<8> &getContents() const { return Contents; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_CVInlineLines;
  }
};

/// Fragment representing the .cv_def_range directive.
class MCCVDefRangeFragment : public MCEncodedFragmentWithFixups<32, 4> {
  SmallVector<std::pair<const MCSymbol *, const MCSymbol *>, 2> Ranges;
  SmallString<32> FixedSizePortion;

  /// CodeViewContext has the real knowledge about this format, so let it access
  /// our members.
  friend class CodeViewContext;

public:
  MCCVDefRangeFragment(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      StringRef FixedSizePortion)
      : MCEncodedFragmentWithFixups<32, 4>(FT_CVDefRange, false),
        Ranges(Ranges.begin(), Ranges.end()),
        FixedSizePortion(FixedSizePortion) {}

  ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> getRanges() const {
    return Ranges;
  }

  StringRef getFixedSizePortion() const { return FixedSizePortion.str(); }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_CVDefRange;
  }
};

/// Represents required padding such that a particular other set of fragments
/// does not cross a particular power-of-two boundary. The other fragments must
/// follow this one within the same section.
class MCBoundaryAlignFragment : public MCFragment {
  /// The alignment requirement of the branch to be aligned.
  Align AlignBoundary;
  /// The last fragment in the set of fragments to be aligned.
  const MCFragment *LastFragment = nullptr;
  /// The size of the fragment.  The size is lazily set during relaxation, and
  /// is not meaningful before that.
  uint64_t Size = 0;

  /// When emitting Nops some subtargets have specific nop encodings.
  const MCSubtargetInfo &STI;

public:
  MCBoundaryAlignFragment(Align AlignBoundary, const MCSubtargetInfo &STI)
      : MCFragment(FT_BoundaryAlign, false), AlignBoundary(AlignBoundary),
        STI(STI) {}

  uint64_t getSize() const { return Size; }
  void setSize(uint64_t Value) { Size = Value; }

  Align getAlignment() const { return AlignBoundary; }
  void setAlignment(Align Value) { AlignBoundary = Value; }

  const MCFragment *getLastFragment() const { return LastFragment; }
  void setLastFragment(const MCFragment *F) {
    assert(!F || getParent() == F->getParent());
    LastFragment = F;
  }

  const MCSubtargetInfo *getSubtargetInfo() const { return &STI; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_BoundaryAlign;
  }
};

class MCPseudoProbeAddrFragment : public MCEncodedFragmentWithFixups<8, 1> {
  /// The expression for the difference of the two symbols that
  /// make up the address delta between two .pseudoprobe directives.
  const MCExpr *AddrDelta;

public:
  MCPseudoProbeAddrFragment(const MCExpr *AddrDelta)
      : MCEncodedFragmentWithFixups<8, 1>(FT_PseudoProbe, false),
        AddrDelta(AddrDelta) {}

  const MCExpr &getAddrDelta() const { return *AddrDelta; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_PseudoProbe;
  }
};
} // end namespace llvm

#endif // LLVM_MC_MCFRAGMENT_H
