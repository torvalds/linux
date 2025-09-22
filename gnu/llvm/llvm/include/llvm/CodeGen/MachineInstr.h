//===- llvm/CodeGen/MachineInstr.h - MachineInstr class ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MachineInstr class, which is the
// basic representation for all target dependent machine instructions used by
// the back end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEINSTR_H
#define LLVM_CODEGEN_MACHINEINSTR_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerSumType.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ArrayRecycler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TrailingObjects.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>

namespace llvm {

class DILabel;
class Instruction;
class MDNode;
class AAResults;
template <typename T> class ArrayRef;
class DIExpression;
class DILocalVariable;
class MachineBasicBlock;
class MachineFunction;
class MachineRegisterInfo;
class ModuleSlotTracker;
class raw_ostream;
template <typename T> class SmallVectorImpl;
class SmallBitVector;
class StringRef;
class TargetInstrInfo;
class TargetRegisterClass;
class TargetRegisterInfo;

//===----------------------------------------------------------------------===//
/// Representation of each machine instruction.
///
/// This class isn't a POD type, but it must have a trivial destructor. When a
/// MachineFunction is deleted, all the contained MachineInstrs are deallocated
/// without having their destructor called.
///
class MachineInstr
    : public ilist_node_with_parent<MachineInstr, MachineBasicBlock,
                                    ilist_sentinel_tracking<true>> {
public:
  using mmo_iterator = ArrayRef<MachineMemOperand *>::iterator;

  /// Flags to specify different kinds of comments to output in
  /// assembly code.  These flags carry semantic information not
  /// otherwise easily derivable from the IR text.
  ///
  enum CommentFlag {
    ReloadReuse = 0x1,    // higher bits are reserved for target dep comments.
    NoSchedComment = 0x2,
    TAsmComments = 0x4    // Target Asm comments should start from this value.
  };

  enum MIFlag {
    NoFlags = 0,
    FrameSetup = 1 << 0,     // Instruction is used as a part of
                             // function frame setup code.
    FrameDestroy = 1 << 1,   // Instruction is used as a part of
                             // function frame destruction code.
    BundledPred = 1 << 2,    // Instruction has bundled predecessors.
    BundledSucc = 1 << 3,    // Instruction has bundled successors.
    FmNoNans = 1 << 4,       // Instruction does not support Fast
                             // math nan values.
    FmNoInfs = 1 << 5,       // Instruction does not support Fast
                             // math infinity values.
    FmNsz = 1 << 6,          // Instruction is not required to retain
                             // signed zero values.
    FmArcp = 1 << 7,         // Instruction supports Fast math
                             // reciprocal approximations.
    FmContract = 1 << 8,     // Instruction supports Fast math
                             // contraction operations like fma.
    FmAfn = 1 << 9,          // Instruction may map to Fast math
                             // intrinsic approximation.
    FmReassoc = 1 << 10,     // Instruction supports Fast math
                             // reassociation of operand order.
    NoUWrap = 1 << 11,       // Instruction supports binary operator
                             // no unsigned wrap.
    NoSWrap = 1 << 12,       // Instruction supports binary operator
                             // no signed wrap.
    IsExact = 1 << 13,       // Instruction supports division is
                             // known to be exact.
    NoFPExcept = 1 << 14,    // Instruction does not raise
                             // floatint-point exceptions.
    NoMerge = 1 << 15,       // Passes that drop source location info
                             // (e.g. branch folding) should skip
                             // this instruction.
    Unpredictable = 1 << 16, // Instruction with unpredictable condition.
    NoConvergent = 1 << 17,  // Call does not require convergence guarantees.
    NonNeg = 1 << 18,        // The operand is non-negative.
    Disjoint = 1 << 19,      // Each bit is zero in at least one of the inputs.
    NoUSWrap = 1 << 20,      // Instruction supports geps
                             // no unsigned signed wrap.
  };

private:
  const MCInstrDesc *MCID;              // Instruction descriptor.
  MachineBasicBlock *Parent = nullptr;  // Pointer to the owning basic block.

  // Operands are allocated by an ArrayRecycler.
  MachineOperand *Operands = nullptr;   // Pointer to the first operand.

#define LLVM_MI_NUMOPERANDS_BITS 24
#define LLVM_MI_FLAGS_BITS 24
#define LLVM_MI_ASMPRINTERFLAGS_BITS 8

  /// Number of operands on instruction.
  uint32_t NumOperands : LLVM_MI_NUMOPERANDS_BITS;

  // OperandCapacity has uint8_t size, so it should be next to NumOperands
  // to properly pack.
  using OperandCapacity = ArrayRecycler<MachineOperand>::Capacity;
  OperandCapacity CapOperands;          // Capacity of the Operands array.

  /// Various bits of additional information about the machine instruction.
  uint32_t Flags : LLVM_MI_FLAGS_BITS;

  /// Various bits of information used by the AsmPrinter to emit helpful
  /// comments.  This is *not* semantic information.  Do not use this for
  /// anything other than to convey comment information to AsmPrinter.
  uint8_t AsmPrinterFlags : LLVM_MI_ASMPRINTERFLAGS_BITS;

  /// Internal implementation detail class that provides out-of-line storage for
  /// extra info used by the machine instruction when this info cannot be stored
  /// in-line within the instruction itself.
  ///
  /// This has to be defined eagerly due to the implementation constraints of
  /// `PointerSumType` where it is used.
  class ExtraInfo final : TrailingObjects<ExtraInfo, MachineMemOperand *,
                                          MCSymbol *, MDNode *, uint32_t> {
  public:
    static ExtraInfo *create(BumpPtrAllocator &Allocator,
                             ArrayRef<MachineMemOperand *> MMOs,
                             MCSymbol *PreInstrSymbol = nullptr,
                             MCSymbol *PostInstrSymbol = nullptr,
                             MDNode *HeapAllocMarker = nullptr,
                             MDNode *PCSections = nullptr, uint32_t CFIType = 0,
                             MDNode *MMRAs = nullptr) {
      bool HasPreInstrSymbol = PreInstrSymbol != nullptr;
      bool HasPostInstrSymbol = PostInstrSymbol != nullptr;
      bool HasHeapAllocMarker = HeapAllocMarker != nullptr;
      bool HasMMRAs = MMRAs != nullptr;
      bool HasCFIType = CFIType != 0;
      bool HasPCSections = PCSections != nullptr;
      auto *Result = new (Allocator.Allocate(
          totalSizeToAlloc<MachineMemOperand *, MCSymbol *, MDNode *, uint32_t>(
              MMOs.size(), HasPreInstrSymbol + HasPostInstrSymbol,
              HasHeapAllocMarker + HasPCSections + HasMMRAs, HasCFIType),
          alignof(ExtraInfo)))
          ExtraInfo(MMOs.size(), HasPreInstrSymbol, HasPostInstrSymbol,
                    HasHeapAllocMarker, HasPCSections, HasCFIType, HasMMRAs);

      // Copy the actual data into the trailing objects.
      std::copy(MMOs.begin(), MMOs.end(),
                Result->getTrailingObjects<MachineMemOperand *>());

      unsigned MDNodeIdx = 0;

      if (HasPreInstrSymbol)
        Result->getTrailingObjects<MCSymbol *>()[0] = PreInstrSymbol;
      if (HasPostInstrSymbol)
        Result->getTrailingObjects<MCSymbol *>()[HasPreInstrSymbol] =
            PostInstrSymbol;
      if (HasHeapAllocMarker)
        Result->getTrailingObjects<MDNode *>()[MDNodeIdx++] = HeapAllocMarker;
      if (HasPCSections)
        Result->getTrailingObjects<MDNode *>()[MDNodeIdx++] = PCSections;
      if (HasCFIType)
        Result->getTrailingObjects<uint32_t>()[0] = CFIType;
      if (HasMMRAs)
        Result->getTrailingObjects<MDNode *>()[MDNodeIdx++] = MMRAs;

      return Result;
    }

    ArrayRef<MachineMemOperand *> getMMOs() const {
      return ArrayRef(getTrailingObjects<MachineMemOperand *>(), NumMMOs);
    }

    MCSymbol *getPreInstrSymbol() const {
      return HasPreInstrSymbol ? getTrailingObjects<MCSymbol *>()[0] : nullptr;
    }

    MCSymbol *getPostInstrSymbol() const {
      return HasPostInstrSymbol
                 ? getTrailingObjects<MCSymbol *>()[HasPreInstrSymbol]
                 : nullptr;
    }

    MDNode *getHeapAllocMarker() const {
      return HasHeapAllocMarker ? getTrailingObjects<MDNode *>()[0] : nullptr;
    }

    MDNode *getPCSections() const {
      return HasPCSections
                 ? getTrailingObjects<MDNode *>()[HasHeapAllocMarker]
                 : nullptr;
    }

    uint32_t getCFIType() const {
      return HasCFIType ? getTrailingObjects<uint32_t>()[0] : 0;
    }

    MDNode *getMMRAMetadata() const {
      return HasMMRAs ? getTrailingObjects<MDNode *>()[HasHeapAllocMarker +
                                                       HasPCSections]
                      : nullptr;
    }

  private:
    friend TrailingObjects;

    // Description of the extra info, used to interpret the actual optional
    // data appended.
    //
    // Note that this is not terribly space optimized. This leaves a great deal
    // of flexibility to fit more in here later.
    const int NumMMOs;
    const bool HasPreInstrSymbol;
    const bool HasPostInstrSymbol;
    const bool HasHeapAllocMarker;
    const bool HasPCSections;
    const bool HasCFIType;
    const bool HasMMRAs;

    // Implement the `TrailingObjects` internal API.
    size_t numTrailingObjects(OverloadToken<MachineMemOperand *>) const {
      return NumMMOs;
    }
    size_t numTrailingObjects(OverloadToken<MCSymbol *>) const {
      return HasPreInstrSymbol + HasPostInstrSymbol;
    }
    size_t numTrailingObjects(OverloadToken<MDNode *>) const {
      return HasHeapAllocMarker + HasPCSections;
    }
    size_t numTrailingObjects(OverloadToken<uint32_t>) const {
      return HasCFIType;
    }

    // Just a boring constructor to allow us to initialize the sizes. Always use
    // the `create` routine above.
    ExtraInfo(int NumMMOs, bool HasPreInstrSymbol, bool HasPostInstrSymbol,
              bool HasHeapAllocMarker, bool HasPCSections, bool HasCFIType,
              bool HasMMRAs)
        : NumMMOs(NumMMOs), HasPreInstrSymbol(HasPreInstrSymbol),
          HasPostInstrSymbol(HasPostInstrSymbol),
          HasHeapAllocMarker(HasHeapAllocMarker), HasPCSections(HasPCSections),
          HasCFIType(HasCFIType), HasMMRAs(HasMMRAs) {}
  };

  /// Enumeration of the kinds of inline extra info available. It is important
  /// that the `MachineMemOperand` inline kind has a tag value of zero to make
  /// it accessible as an `ArrayRef`.
  enum ExtraInfoInlineKinds {
    EIIK_MMO = 0,
    EIIK_PreInstrSymbol,
    EIIK_PostInstrSymbol,
    EIIK_OutOfLine
  };

  // We store extra information about the instruction here. The common case is
  // expected to be nothing or a single pointer (typically a MMO or a symbol).
  // We work to optimize this common case by storing it inline here rather than
  // requiring a separate allocation, but we fall back to an allocation when
  // multiple pointers are needed.
  PointerSumType<ExtraInfoInlineKinds,
                 PointerSumTypeMember<EIIK_MMO, MachineMemOperand *>,
                 PointerSumTypeMember<EIIK_PreInstrSymbol, MCSymbol *>,
                 PointerSumTypeMember<EIIK_PostInstrSymbol, MCSymbol *>,
                 PointerSumTypeMember<EIIK_OutOfLine, ExtraInfo *>>
      Info;

  DebugLoc DbgLoc; // Source line information.

  /// Unique instruction number. Used by DBG_INSTR_REFs to refer to the values
  /// defined by this instruction.
  unsigned DebugInstrNum;

  /// Cached opcode from MCID.
  uint16_t Opcode;

  // Intrusive list support
  friend struct ilist_traits<MachineInstr>;
  friend struct ilist_callback_traits<MachineBasicBlock>;
  void setParent(MachineBasicBlock *P) { Parent = P; }

  /// This constructor creates a copy of the given
  /// MachineInstr in the given MachineFunction.
  MachineInstr(MachineFunction &, const MachineInstr &);

  /// This constructor create a MachineInstr and add the implicit operands.
  /// It reserves space for number of operands specified by
  /// MCInstrDesc.  An explicit DebugLoc is supplied.
  MachineInstr(MachineFunction &, const MCInstrDesc &TID, DebugLoc DL,
               bool NoImp = false);

  // MachineInstrs are pool-allocated and owned by MachineFunction.
  friend class MachineFunction;

  void
  dumprImpl(const MachineRegisterInfo &MRI, unsigned Depth, unsigned MaxDepth,
            SmallPtrSetImpl<const MachineInstr *> &AlreadySeenInstrs) const;

  static bool opIsRegDef(const MachineOperand &Op) {
    return Op.isReg() && Op.isDef();
  }

  static bool opIsRegUse(const MachineOperand &Op) {
    return Op.isReg() && Op.isUse();
  }

public:
  MachineInstr(const MachineInstr &) = delete;
  MachineInstr &operator=(const MachineInstr &) = delete;
  // Use MachineFunction::DeleteMachineInstr() instead.
  ~MachineInstr() = delete;

  const MachineBasicBlock* getParent() const { return Parent; }
  MachineBasicBlock* getParent() { return Parent; }

  /// Move the instruction before \p MovePos.
  void moveBefore(MachineInstr *MovePos);

  /// Return the function that contains the basic block that this instruction
  /// belongs to.
  ///
  /// Note: this is undefined behaviour if the instruction does not have a
  /// parent.
  const MachineFunction *getMF() const;
  MachineFunction *getMF() {
    return const_cast<MachineFunction *>(
        static_cast<const MachineInstr *>(this)->getMF());
  }

  /// Return the asm printer flags bitvector.
  uint8_t getAsmPrinterFlags() const { return AsmPrinterFlags; }

  /// Clear the AsmPrinter bitvector.
  void clearAsmPrinterFlags() { AsmPrinterFlags = 0; }

  /// Return whether an AsmPrinter flag is set.
  bool getAsmPrinterFlag(CommentFlag Flag) const {
    assert(isUInt<LLVM_MI_ASMPRINTERFLAGS_BITS>(unsigned(Flag)) &&
           "Flag is out of range for the AsmPrinterFlags field");
    return AsmPrinterFlags & Flag;
  }

  /// Set a flag for the AsmPrinter.
  void setAsmPrinterFlag(uint8_t Flag) {
    assert(isUInt<LLVM_MI_ASMPRINTERFLAGS_BITS>(unsigned(Flag)) &&
           "Flag is out of range for the AsmPrinterFlags field");
    AsmPrinterFlags |= Flag;
  }

  /// Clear specific AsmPrinter flags.
  void clearAsmPrinterFlag(CommentFlag Flag) {
    assert(isUInt<LLVM_MI_ASMPRINTERFLAGS_BITS>(unsigned(Flag)) &&
           "Flag is out of range for the AsmPrinterFlags field");
    AsmPrinterFlags &= ~Flag;
  }

  /// Return the MI flags bitvector.
  uint32_t getFlags() const {
    return Flags;
  }

  /// Return whether an MI flag is set.
  bool getFlag(MIFlag Flag) const {
    assert(isUInt<LLVM_MI_FLAGS_BITS>(unsigned(Flag)) &&
           "Flag is out of range for the Flags field");
    return Flags & Flag;
  }

  /// Set a MI flag.
  void setFlag(MIFlag Flag) {
    assert(isUInt<LLVM_MI_FLAGS_BITS>(unsigned(Flag)) &&
           "Flag is out of range for the Flags field");
    Flags |= (uint32_t)Flag;
  }

  void setFlags(unsigned flags) {
    assert(isUInt<LLVM_MI_FLAGS_BITS>(flags) &&
           "flags to be set are out of range for the Flags field");
    // Filter out the automatically maintained flags.
    unsigned Mask = BundledPred | BundledSucc;
    Flags = (Flags & Mask) | (flags & ~Mask);
  }

  /// clearFlag - Clear a MI flag.
  void clearFlag(MIFlag Flag) {
    assert(isUInt<LLVM_MI_FLAGS_BITS>(unsigned(Flag)) &&
           "Flag to clear is out of range for the Flags field");
    Flags &= ~((uint32_t)Flag);
  }

  void clearFlags(unsigned flags) {
    assert(isUInt<LLVM_MI_FLAGS_BITS>(flags) &&
           "flags to be cleared are out of range for the Flags field");
    Flags &= ~flags;
  }

  /// Return true if MI is in a bundle (but not the first MI in a bundle).
  ///
  /// A bundle looks like this before it's finalized:
  ///   ----------------
  ///   |      MI      |
  ///   ----------------
  ///          |
  ///   ----------------
  ///   |      MI    * |
  ///   ----------------
  ///          |
  ///   ----------------
  ///   |      MI    * |
  ///   ----------------
  /// In this case, the first MI starts a bundle but is not inside a bundle, the
  /// next 2 MIs are considered "inside" the bundle.
  ///
  /// After a bundle is finalized, it looks like this:
  ///   ----------------
  ///   |    Bundle    |
  ///   ----------------
  ///          |
  ///   ----------------
  ///   |      MI    * |
  ///   ----------------
  ///          |
  ///   ----------------
  ///   |      MI    * |
  ///   ----------------
  ///          |
  ///   ----------------
  ///   |      MI    * |
  ///   ----------------
  /// The first instruction has the special opcode "BUNDLE". It's not "inside"
  /// a bundle, but the next three MIs are.
  bool isInsideBundle() const {
    return getFlag(BundledPred);
  }

  /// Return true if this instruction part of a bundle. This is true
  /// if either itself or its following instruction is marked "InsideBundle".
  bool isBundled() const {
    return isBundledWithPred() || isBundledWithSucc();
  }

  /// Return true if this instruction is part of a bundle, and it is not the
  /// first instruction in the bundle.
  bool isBundledWithPred() const { return getFlag(BundledPred); }

  /// Return true if this instruction is part of a bundle, and it is not the
  /// last instruction in the bundle.
  bool isBundledWithSucc() const { return getFlag(BundledSucc); }

  /// Bundle this instruction with its predecessor. This can be an unbundled
  /// instruction, or it can be the first instruction in a bundle.
  void bundleWithPred();

  /// Bundle this instruction with its successor. This can be an unbundled
  /// instruction, or it can be the last instruction in a bundle.
  void bundleWithSucc();

  /// Break bundle above this instruction.
  void unbundleFromPred();

  /// Break bundle below this instruction.
  void unbundleFromSucc();

  /// Returns the debug location id of this MachineInstr.
  const DebugLoc &getDebugLoc() const { return DbgLoc; }

  /// Return the operand containing the offset to be used if this DBG_VALUE
  /// instruction is indirect; will be an invalid register if this value is
  /// not indirect, and an immediate with value 0 otherwise.
  const MachineOperand &getDebugOffset() const {
    assert(isNonListDebugValue() && "not a DBG_VALUE");
    return getOperand(1);
  }
  MachineOperand &getDebugOffset() {
    assert(isNonListDebugValue() && "not a DBG_VALUE");
    return getOperand(1);
  }

  /// Return the operand for the debug variable referenced by
  /// this DBG_VALUE instruction.
  const MachineOperand &getDebugVariableOp() const;
  MachineOperand &getDebugVariableOp();

  /// Return the debug variable referenced by
  /// this DBG_VALUE instruction.
  const DILocalVariable *getDebugVariable() const;

  /// Return the operand for the complex address expression referenced by
  /// this DBG_VALUE instruction.
  const MachineOperand &getDebugExpressionOp() const;
  MachineOperand &getDebugExpressionOp();

  /// Return the complex address expression referenced by
  /// this DBG_VALUE instruction.
  const DIExpression *getDebugExpression() const;

  /// Return the debug label referenced by
  /// this DBG_LABEL instruction.
  const DILabel *getDebugLabel() const;

  /// Fetch the instruction number of this MachineInstr. If it does not have
  /// one already, a new and unique number will be assigned.
  unsigned getDebugInstrNum();

  /// Fetch instruction number of this MachineInstr -- but before it's inserted
  /// into \p MF. Needed for transformations that create an instruction but
  /// don't immediately insert them.
  unsigned getDebugInstrNum(MachineFunction &MF);

  /// Examine the instruction number of this MachineInstr. May be zero if
  /// it hasn't been assigned a number yet.
  unsigned peekDebugInstrNum() const { return DebugInstrNum; }

  /// Set instruction number of this MachineInstr. Avoid using unless you're
  /// deserializing this information.
  void setDebugInstrNum(unsigned Num) { DebugInstrNum = Num; }

  /// Drop any variable location debugging information associated with this
  /// instruction. Use when an instruction is modified in such a way that it no
  /// longer defines the value it used to. Variable locations using that value
  /// will be dropped.
  void dropDebugNumber() { DebugInstrNum = 0; }

  /// Emit an error referring to the source location of this instruction.
  /// This should only be used for inline assembly that is somehow
  /// impossible to compile. Other errors should have been handled much
  /// earlier.
  ///
  /// If this method returns, the caller should try to recover from the error.
  void emitError(StringRef Msg) const;

  /// Returns the target instruction descriptor of this MachineInstr.
  const MCInstrDesc &getDesc() const { return *MCID; }

  /// Returns the opcode of this MachineInstr.
  unsigned getOpcode() const { return Opcode; }

  /// Retuns the total number of operands.
  unsigned getNumOperands() const { return NumOperands; }

  /// Returns the total number of operands which are debug locations.
  unsigned getNumDebugOperands() const {
    return std::distance(debug_operands().begin(), debug_operands().end());
  }

  const MachineOperand& getOperand(unsigned i) const {
    assert(i < getNumOperands() && "getOperand() out of range!");
    return Operands[i];
  }
  MachineOperand& getOperand(unsigned i) {
    assert(i < getNumOperands() && "getOperand() out of range!");
    return Operands[i];
  }

  MachineOperand &getDebugOperand(unsigned Index) {
    assert(Index < getNumDebugOperands() && "getDebugOperand() out of range!");
    return *(debug_operands().begin() + Index);
  }
  const MachineOperand &getDebugOperand(unsigned Index) const {
    assert(Index < getNumDebugOperands() && "getDebugOperand() out of range!");
    return *(debug_operands().begin() + Index);
  }

  /// Returns whether this debug value has at least one debug operand with the
  /// register \p Reg.
  bool hasDebugOperandForReg(Register Reg) const {
    return any_of(debug_operands(), [Reg](const MachineOperand &Op) {
      return Op.isReg() && Op.getReg() == Reg;
    });
  }

  /// Returns a range of all of the operands that correspond to a debug use of
  /// \p Reg.
  template <typename Operand, typename Instruction>
  static iterator_range<
      filter_iterator<Operand *, std::function<bool(Operand &Op)>>>
  getDebugOperandsForReg(Instruction *MI, Register Reg) {
    std::function<bool(Operand & Op)> OpUsesReg(
        [Reg](Operand &Op) { return Op.isReg() && Op.getReg() == Reg; });
    return make_filter_range(MI->debug_operands(), OpUsesReg);
  }
  iterator_range<filter_iterator<const MachineOperand *,
                                 std::function<bool(const MachineOperand &Op)>>>
  getDebugOperandsForReg(Register Reg) const {
    return MachineInstr::getDebugOperandsForReg<const MachineOperand,
                                                const MachineInstr>(this, Reg);
  }
  iterator_range<filter_iterator<MachineOperand *,
                                 std::function<bool(MachineOperand &Op)>>>
  getDebugOperandsForReg(Register Reg) {
    return MachineInstr::getDebugOperandsForReg<MachineOperand, MachineInstr>(
        this, Reg);
  }

  bool isDebugOperand(const MachineOperand *Op) const {
    return Op >= adl_begin(debug_operands()) && Op <= adl_end(debug_operands());
  }

  unsigned getDebugOperandIndex(const MachineOperand *Op) const {
    assert(isDebugOperand(Op) && "Expected a debug operand.");
    return std::distance(adl_begin(debug_operands()), Op);
  }

  /// Returns the total number of definitions.
  unsigned getNumDefs() const {
    return getNumExplicitDefs() + MCID->implicit_defs().size();
  }

  /// Returns true if the instruction has implicit definition.
  bool hasImplicitDef() const {
    for (const MachineOperand &MO : implicit_operands())
      if (MO.isDef() && MO.isImplicit())
        return true;
    return false;
  }

  /// Returns the implicit operands number.
  unsigned getNumImplicitOperands() const {
    return getNumOperands() - getNumExplicitOperands();
  }

  /// Return true if operand \p OpIdx is a subregister index.
  bool isOperandSubregIdx(unsigned OpIdx) const {
    assert(getOperand(OpIdx).isImm() && "Expected MO_Immediate operand type.");
    if (isExtractSubreg() && OpIdx == 2)
      return true;
    if (isInsertSubreg() && OpIdx == 3)
      return true;
    if (isRegSequence() && OpIdx > 1 && (OpIdx % 2) == 0)
      return true;
    if (isSubregToReg() && OpIdx == 3)
      return true;
    return false;
  }

  /// Returns the number of non-implicit operands.
  unsigned getNumExplicitOperands() const;

  /// Returns the number of non-implicit definitions.
  unsigned getNumExplicitDefs() const;

  /// iterator/begin/end - Iterate over all operands of a machine instruction.
  using mop_iterator = MachineOperand *;
  using const_mop_iterator = const MachineOperand *;

  mop_iterator operands_begin() { return Operands; }
  mop_iterator operands_end() { return Operands + NumOperands; }

  const_mop_iterator operands_begin() const { return Operands; }
  const_mop_iterator operands_end() const { return Operands + NumOperands; }

  iterator_range<mop_iterator> operands() {
    return make_range(operands_begin(), operands_end());
  }
  iterator_range<const_mop_iterator> operands() const {
    return make_range(operands_begin(), operands_end());
  }
  iterator_range<mop_iterator> explicit_operands() {
    return make_range(operands_begin(),
                      operands_begin() + getNumExplicitOperands());
  }
  iterator_range<const_mop_iterator> explicit_operands() const {
    return make_range(operands_begin(),
                      operands_begin() + getNumExplicitOperands());
  }
  iterator_range<mop_iterator> implicit_operands() {
    return make_range(explicit_operands().end(), operands_end());
  }
  iterator_range<const_mop_iterator> implicit_operands() const {
    return make_range(explicit_operands().end(), operands_end());
  }
  /// Returns a range over all operands that are used to determine the variable
  /// location for this DBG_VALUE instruction.
  iterator_range<mop_iterator> debug_operands() {
    assert((isDebugValueLike()) && "Must be a debug value instruction.");
    return isNonListDebugValue()
               ? make_range(operands_begin(), operands_begin() + 1)
               : make_range(operands_begin() + 2, operands_end());
  }
  /// \copydoc debug_operands()
  iterator_range<const_mop_iterator> debug_operands() const {
    assert((isDebugValueLike()) && "Must be a debug value instruction.");
    return isNonListDebugValue()
               ? make_range(operands_begin(), operands_begin() + 1)
               : make_range(operands_begin() + 2, operands_end());
  }
  /// Returns a range over all explicit operands that are register definitions.
  /// Implicit definition are not included!
  iterator_range<mop_iterator> defs() {
    return make_range(operands_begin(),
                      operands_begin() + getNumExplicitDefs());
  }
  /// \copydoc defs()
  iterator_range<const_mop_iterator> defs() const {
    return make_range(operands_begin(),
                      operands_begin() + getNumExplicitDefs());
  }
  /// Returns a range that includes all operands that are register uses.
  /// This may include unrelated operands which are not register uses.
  iterator_range<mop_iterator> uses() {
    return make_range(operands_begin() + getNumExplicitDefs(), operands_end());
  }
  /// \copydoc uses()
  iterator_range<const_mop_iterator> uses() const {
    return make_range(operands_begin() + getNumExplicitDefs(), operands_end());
  }
  iterator_range<mop_iterator> explicit_uses() {
    return make_range(operands_begin() + getNumExplicitDefs(),
                      operands_begin() + getNumExplicitOperands());
  }
  iterator_range<const_mop_iterator> explicit_uses() const {
    return make_range(operands_begin() + getNumExplicitDefs(),
                      operands_begin() + getNumExplicitOperands());
  }

  using filtered_mop_iterator =
      filter_iterator<mop_iterator, bool (*)(const MachineOperand &)>;
  using filtered_const_mop_iterator =
      filter_iterator<const_mop_iterator, bool (*)(const MachineOperand &)>;

  /// Returns an iterator range over all operands that are (explicit or
  /// implicit) register defs.
  iterator_range<filtered_mop_iterator> all_defs() {
    return make_filter_range(operands(), opIsRegDef);
  }
  /// \copydoc all_defs()
  iterator_range<filtered_const_mop_iterator> all_defs() const {
    return make_filter_range(operands(), opIsRegDef);
  }

  /// Returns an iterator range over all operands that are (explicit or
  /// implicit) register uses.
  iterator_range<filtered_mop_iterator> all_uses() {
    return make_filter_range(uses(), opIsRegUse);
  }
  /// \copydoc all_uses()
  iterator_range<filtered_const_mop_iterator> all_uses() const {
    return make_filter_range(uses(), opIsRegUse);
  }

  /// Returns the number of the operand iterator \p I points to.
  unsigned getOperandNo(const_mop_iterator I) const {
    return I - operands_begin();
  }

  /// Access to memory operands of the instruction. If there are none, that does
  /// not imply anything about whether the function accesses memory. Instead,
  /// the caller must behave conservatively.
  ArrayRef<MachineMemOperand *> memoperands() const {
    if (!Info)
      return {};

    if (Info.is<EIIK_MMO>())
      return ArrayRef(Info.getAddrOfZeroTagPointer(), 1);

    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getMMOs();

    return {};
  }

  /// Access to memory operands of the instruction.
  ///
  /// If `memoperands_begin() == memoperands_end()`, that does not imply
  /// anything about whether the function accesses memory. Instead, the caller
  /// must behave conservatively.
  mmo_iterator memoperands_begin() const { return memoperands().begin(); }

  /// Access to memory operands of the instruction.
  ///
  /// If `memoperands_begin() == memoperands_end()`, that does not imply
  /// anything about whether the function accesses memory. Instead, the caller
  /// must behave conservatively.
  mmo_iterator memoperands_end() const { return memoperands().end(); }

  /// Return true if we don't have any memory operands which described the
  /// memory access done by this instruction.  If this is true, calling code
  /// must be conservative.
  bool memoperands_empty() const { return memoperands().empty(); }

  /// Return true if this instruction has exactly one MachineMemOperand.
  bool hasOneMemOperand() const { return memoperands().size() == 1; }

  /// Return the number of memory operands.
  unsigned getNumMemOperands() const { return memoperands().size(); }

  /// Helper to extract a pre-instruction symbol if one has been added.
  MCSymbol *getPreInstrSymbol() const {
    if (!Info)
      return nullptr;
    if (MCSymbol *S = Info.get<EIIK_PreInstrSymbol>())
      return S;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getPreInstrSymbol();

    return nullptr;
  }

  /// Helper to extract a post-instruction symbol if one has been added.
  MCSymbol *getPostInstrSymbol() const {
    if (!Info)
      return nullptr;
    if (MCSymbol *S = Info.get<EIIK_PostInstrSymbol>())
      return S;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getPostInstrSymbol();

    return nullptr;
  }

  /// Helper to extract a heap alloc marker if one has been added.
  MDNode *getHeapAllocMarker() const {
    if (!Info)
      return nullptr;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getHeapAllocMarker();

    return nullptr;
  }

  /// Helper to extract PCSections metadata target sections.
  MDNode *getPCSections() const {
    if (!Info)
      return nullptr;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getPCSections();

    return nullptr;
  }

  /// Helper to extract mmra.op metadata.
  MDNode *getMMRAMetadata() const {
    if (!Info)
      return nullptr;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getMMRAMetadata();
    return nullptr;
  }

  /// Helper to extract a CFI type hash if one has been added.
  uint32_t getCFIType() const {
    if (!Info)
      return 0;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getCFIType();

    return 0;
  }

  /// API for querying MachineInstr properties. They are the same as MCInstrDesc
  /// queries but they are bundle aware.

  enum QueryType {
    IgnoreBundle,    // Ignore bundles
    AnyInBundle,     // Return true if any instruction in bundle has property
    AllInBundle      // Return true if all instructions in bundle have property
  };

  /// Return true if the instruction (or in the case of a bundle,
  /// the instructions inside the bundle) has the specified property.
  /// The first argument is the property being queried.
  /// The second argument indicates whether the query should look inside
  /// instruction bundles.
  bool hasProperty(unsigned MCFlag, QueryType Type = AnyInBundle) const {
    assert(MCFlag < 64 &&
           "MCFlag out of range for bit mask in getFlags/hasPropertyInBundle.");
    // Inline the fast path for unbundled or bundle-internal instructions.
    if (Type == IgnoreBundle || !isBundled() || isBundledWithPred())
      return getDesc().getFlags() & (1ULL << MCFlag);

    // If this is the first instruction in a bundle, take the slow path.
    return hasPropertyInBundle(1ULL << MCFlag, Type);
  }

  /// Return true if this is an instruction that should go through the usual
  /// legalization steps.
  bool isPreISelOpcode(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::PreISelOpcode, Type);
  }

  /// Return true if this instruction can have a variable number of operands.
  /// In this case, the variable operands will be after the normal
  /// operands but before the implicit definitions and uses (if any are
  /// present).
  bool isVariadic(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Variadic, Type);
  }

  /// Set if this instruction has an optional definition, e.g.
  /// ARM instructions which can set condition code if 's' bit is set.
  bool hasOptionalDef(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::HasOptionalDef, Type);
  }

  /// Return true if this is a pseudo instruction that doesn't
  /// correspond to a real machine instruction.
  bool isPseudo(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Pseudo, Type);
  }

  /// Return true if this instruction doesn't produce any output in the form of
  /// executable instructions.
  bool isMetaInstruction(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Meta, Type);
  }

  bool isReturn(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::Return, Type);
  }

  /// Return true if this is an instruction that marks the end of an EH scope,
  /// i.e., a catchpad or a cleanuppad instruction.
  bool isEHScopeReturn(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::EHScopeReturn, Type);
  }

  bool isCall(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::Call, Type);
  }

  /// Return true if this is a call instruction that may have an associated
  /// call site entry in the debug info.
  bool isCandidateForCallSiteEntry(QueryType Type = IgnoreBundle) const;
  /// Return true if copying, moving, or erasing this instruction requires
  /// updating Call Site Info (see \ref copyCallSiteInfo, \ref moveCallSiteInfo,
  /// \ref eraseCallSiteInfo).
  bool shouldUpdateCallSiteInfo() const;

  /// Returns true if the specified instruction stops control flow
  /// from executing the instruction immediately following it.  Examples include
  /// unconditional branches and return instructions.
  bool isBarrier(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::Barrier, Type);
  }

  /// Returns true if this instruction part of the terminator for a basic block.
  /// Typically this is things like return and branch instructions.
  ///
  /// Various passes use this to insert code into the bottom of a basic block,
  /// but before control flow occurs.
  bool isTerminator(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::Terminator, Type);
  }

  /// Returns true if this is a conditional, unconditional, or indirect branch.
  /// Predicates below can be used to discriminate between
  /// these cases, and the TargetInstrInfo::analyzeBranch method can be used to
  /// get more information.
  bool isBranch(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::Branch, Type);
  }

  /// Return true if this is an indirect branch, such as a
  /// branch through a register.
  bool isIndirectBranch(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::IndirectBranch, Type);
  }

  /// Return true if this is a branch which may fall
  /// through to the next instruction or may transfer control flow to some other
  /// block.  The TargetInstrInfo::analyzeBranch method can be used to get more
  /// information about this branch.
  bool isConditionalBranch(QueryType Type = AnyInBundle) const {
    return isBranch(Type) && !isBarrier(Type) && !isIndirectBranch(Type);
  }

  /// Return true if this is a branch which always
  /// transfers control flow to some other block.  The
  /// TargetInstrInfo::analyzeBranch method can be used to get more information
  /// about this branch.
  bool isUnconditionalBranch(QueryType Type = AnyInBundle) const {
    return isBranch(Type) && isBarrier(Type) && !isIndirectBranch(Type);
  }

  /// Return true if this instruction has a predicate operand that
  /// controls execution.  It may be set to 'always', or may be set to other
  /// values.   There are various methods in TargetInstrInfo that can be used to
  /// control and modify the predicate in this instruction.
  bool isPredicable(QueryType Type = AllInBundle) const {
    // If it's a bundle than all bundled instructions must be predicable for this
    // to return true.
    return hasProperty(MCID::Predicable, Type);
  }

  /// Return true if this instruction is a comparison.
  bool isCompare(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Compare, Type);
  }

  /// Return true if this instruction is a move immediate
  /// (including conditional moves) instruction.
  bool isMoveImmediate(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::MoveImm, Type);
  }

  /// Return true if this instruction is a register move.
  /// (including moving values from subreg to reg)
  bool isMoveReg(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::MoveReg, Type);
  }

  /// Return true if this instruction is a bitcast instruction.
  bool isBitcast(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Bitcast, Type);
  }

  /// Return true if this instruction is a select instruction.
  bool isSelect(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Select, Type);
  }

  /// Return true if this instruction cannot be safely duplicated.
  /// For example, if the instruction has a unique labels attached
  /// to it, duplicating it would cause multiple definition errors.
  bool isNotDuplicable(QueryType Type = AnyInBundle) const {
    if (getPreInstrSymbol() || getPostInstrSymbol())
      return true;
    return hasProperty(MCID::NotDuplicable, Type);
  }

  /// Return true if this instruction is convergent.
  /// Convergent instructions can not be made control-dependent on any
  /// additional values.
  bool isConvergent(QueryType Type = AnyInBundle) const {
    if (isInlineAsm()) {
      unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
      if (ExtraInfo & InlineAsm::Extra_IsConvergent)
        return true;
    }
    if (getFlag(NoConvergent))
      return false;
    return hasProperty(MCID::Convergent, Type);
  }

  /// Returns true if the specified instruction has a delay slot
  /// which must be filled by the code generator.
  bool hasDelaySlot(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::DelaySlot, Type);
  }

  /// Return true for instructions that can be folded as
  /// memory operands in other instructions. The most common use for this
  /// is instructions that are simple loads from memory that don't modify
  /// the loaded value in any way, but it can also be used for instructions
  /// that can be expressed as constant-pool loads, such as V_SETALLONES
  /// on x86, to allow them to be folded when it is beneficial.
  /// This should only be set on instructions that return a value in their
  /// only virtual register definition.
  bool canFoldAsLoad(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::FoldableAsLoad, Type);
  }

  /// Return true if this instruction behaves
  /// the same way as the generic REG_SEQUENCE instructions.
  /// E.g., on ARM,
  /// dX VMOVDRR rY, rZ
  /// is equivalent to
  /// dX = REG_SEQUENCE rY, ssub_0, rZ, ssub_1.
  ///
  /// Note that for the optimizers to be able to take advantage of
  /// this property, TargetInstrInfo::getRegSequenceLikeInputs has to be
  /// override accordingly.
  bool isRegSequenceLike(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::RegSequence, Type);
  }

  /// Return true if this instruction behaves
  /// the same way as the generic EXTRACT_SUBREG instructions.
  /// E.g., on ARM,
  /// rX, rY VMOVRRD dZ
  /// is equivalent to two EXTRACT_SUBREG:
  /// rX = EXTRACT_SUBREG dZ, ssub_0
  /// rY = EXTRACT_SUBREG dZ, ssub_1
  ///
  /// Note that for the optimizers to be able to take advantage of
  /// this property, TargetInstrInfo::getExtractSubregLikeInputs has to be
  /// override accordingly.
  bool isExtractSubregLike(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::ExtractSubreg, Type);
  }

  /// Return true if this instruction behaves
  /// the same way as the generic INSERT_SUBREG instructions.
  /// E.g., on ARM,
  /// dX = VSETLNi32 dY, rZ, Imm
  /// is equivalent to a INSERT_SUBREG:
  /// dX = INSERT_SUBREG dY, rZ, translateImmToSubIdx(Imm)
  ///
  /// Note that for the optimizers to be able to take advantage of
  /// this property, TargetInstrInfo::getInsertSubregLikeInputs has to be
  /// override accordingly.
  bool isInsertSubregLike(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::InsertSubreg, Type);
  }

  //===--------------------------------------------------------------------===//
  // Side Effect Analysis
  //===--------------------------------------------------------------------===//

  /// Return true if this instruction could possibly read memory.
  /// Instructions with this flag set are not necessarily simple load
  /// instructions, they may load a value and modify it, for example.
  bool mayLoad(QueryType Type = AnyInBundle) const {
    if (isInlineAsm()) {
      unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
      if (ExtraInfo & InlineAsm::Extra_MayLoad)
        return true;
    }
    return hasProperty(MCID::MayLoad, Type);
  }

  /// Return true if this instruction could possibly modify memory.
  /// Instructions with this flag set are not necessarily simple store
  /// instructions, they may store a modified value based on their operands, or
  /// may not actually modify anything, for example.
  bool mayStore(QueryType Type = AnyInBundle) const {
    if (isInlineAsm()) {
      unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
      if (ExtraInfo & InlineAsm::Extra_MayStore)
        return true;
    }
    return hasProperty(MCID::MayStore, Type);
  }

  /// Return true if this instruction could possibly read or modify memory.
  bool mayLoadOrStore(QueryType Type = AnyInBundle) const {
    return mayLoad(Type) || mayStore(Type);
  }

  /// Return true if this instruction could possibly raise a floating-point
  /// exception.  This is the case if the instruction is a floating-point
  /// instruction that can in principle raise an exception, as indicated
  /// by the MCID::MayRaiseFPException property, *and* at the same time,
  /// the instruction is used in a context where we expect floating-point
  /// exceptions are not disabled, as indicated by the NoFPExcept MI flag.
  bool mayRaiseFPException() const {
    return hasProperty(MCID::MayRaiseFPException) &&
           !getFlag(MachineInstr::MIFlag::NoFPExcept);
  }

  //===--------------------------------------------------------------------===//
  // Flags that indicate whether an instruction can be modified by a method.
  //===--------------------------------------------------------------------===//

  /// Return true if this may be a 2- or 3-address
  /// instruction (of the form "X = op Y, Z, ..."), which produces the same
  /// result if Y and Z are exchanged.  If this flag is set, then the
  /// TargetInstrInfo::commuteInstruction method may be used to hack on the
  /// instruction.
  ///
  /// Note that this flag may be set on instructions that are only commutable
  /// sometimes.  In these cases, the call to commuteInstruction will fail.
  /// Also note that some instructions require non-trivial modification to
  /// commute them.
  bool isCommutable(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Commutable, Type);
  }

  /// Return true if this is a 2-address instruction
  /// which can be changed into a 3-address instruction if needed.  Doing this
  /// transformation can be profitable in the register allocator, because it
  /// means that the instruction can use a 2-address form if possible, but
  /// degrade into a less efficient form if the source and dest register cannot
  /// be assigned to the same register.  For example, this allows the x86
  /// backend to turn a "shl reg, 3" instruction into an LEA instruction, which
  /// is the same speed as the shift but has bigger code size.
  ///
  /// If this returns true, then the target must implement the
  /// TargetInstrInfo::convertToThreeAddress method for this instruction, which
  /// is allowed to fail if the transformation isn't valid for this specific
  /// instruction (e.g. shl reg, 4 on x86).
  ///
  bool isConvertibleTo3Addr(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::ConvertibleTo3Addr, Type);
  }

  /// Return true if this instruction requires
  /// custom insertion support when the DAG scheduler is inserting it into a
  /// machine basic block.  If this is true for the instruction, it basically
  /// means that it is a pseudo instruction used at SelectionDAG time that is
  /// expanded out into magic code by the target when MachineInstrs are formed.
  ///
  /// If this is true, the TargetLoweringInfo::InsertAtEndOfBasicBlock method
  /// is used to insert this into the MachineBasicBlock.
  bool usesCustomInsertionHook(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::UsesCustomInserter, Type);
  }

  /// Return true if this instruction requires *adjustment*
  /// after instruction selection by calling a target hook. For example, this
  /// can be used to fill in ARM 's' optional operand depending on whether
  /// the conditional flag register is used.
  bool hasPostISelHook(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::HasPostISelHook, Type);
  }

  /// Returns true if this instruction is a candidate for remat.
  /// This flag is deprecated, please don't use it anymore.  If this
  /// flag is set, the isReallyTriviallyReMaterializable() method is called to
  /// verify the instruction is really rematerializable.
  bool isRematerializable(QueryType Type = AllInBundle) const {
    // It's only possible to re-mat a bundle if all bundled instructions are
    // re-materializable.
    return hasProperty(MCID::Rematerializable, Type);
  }

  /// Returns true if this instruction has the same cost (or less) than a move
  /// instruction. This is useful during certain types of optimizations
  /// (e.g., remat during two-address conversion or machine licm)
  /// where we would like to remat or hoist the instruction, but not if it costs
  /// more than moving the instruction into the appropriate register. Note, we
  /// are not marking copies from and to the same register class with this flag.
  bool isAsCheapAsAMove(QueryType Type = AllInBundle) const {
    // Only returns true for a bundle if all bundled instructions are cheap.
    return hasProperty(MCID::CheapAsAMove, Type);
  }

  /// Returns true if this instruction source operands
  /// have special register allocation requirements that are not captured by the
  /// operand register classes. e.g. ARM::STRD's two source registers must be an
  /// even / odd pair, ARM::STM registers have to be in ascending order.
  /// Post-register allocation passes should not attempt to change allocations
  /// for sources of instructions with this flag.
  bool hasExtraSrcRegAllocReq(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::ExtraSrcRegAllocReq, Type);
  }

  /// Returns true if this instruction def operands
  /// have special register allocation requirements that are not captured by the
  /// operand register classes. e.g. ARM::LDRD's two def registers must be an
  /// even / odd pair, ARM::LDM registers have to be in ascending order.
  /// Post-register allocation passes should not attempt to change allocations
  /// for definitions of instructions with this flag.
  bool hasExtraDefRegAllocReq(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::ExtraDefRegAllocReq, Type);
  }

  enum MICheckType {
    CheckDefs,      // Check all operands for equality
    CheckKillDead,  // Check all operands including kill / dead markers
    IgnoreDefs,     // Ignore all definitions
    IgnoreVRegDefs  // Ignore virtual register definitions
  };

  /// Return true if this instruction is identical to \p Other.
  /// Two instructions are identical if they have the same opcode and all their
  /// operands are identical (with respect to MachineOperand::isIdenticalTo()).
  /// Note that this means liveness related flags (dead, undef, kill) do not
  /// affect the notion of identical.
  bool isIdenticalTo(const MachineInstr &Other,
                     MICheckType Check = CheckDefs) const;

  /// Returns true if this instruction is a debug instruction that represents an
  /// identical debug value to \p Other.
  /// This function considers these debug instructions equivalent if they have
  /// identical variables, debug locations, and debug operands, and if the
  /// DIExpressions combined with the directness flags are equivalent.
  bool isEquivalentDbgInstr(const MachineInstr &Other) const;

  /// Unlink 'this' from the containing basic block, and return it without
  /// deleting it.
  ///
  /// This function can not be used on bundled instructions, use
  /// removeFromBundle() to remove individual instructions from a bundle.
  MachineInstr *removeFromParent();

  /// Unlink this instruction from its basic block and return it without
  /// deleting it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle remain bundled.
  MachineInstr *removeFromBundle();

  /// Unlink 'this' from the containing basic block and delete it.
  ///
  /// If this instruction is the header of a bundle, the whole bundle is erased.
  /// This function can not be used for instructions inside a bundle, use
  /// eraseFromBundle() to erase individual bundled instructions.
  void eraseFromParent();

  /// Unlink 'this' from its basic block and delete it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle remain bundled.
  void eraseFromBundle();

  bool isEHLabel() const { return getOpcode() == TargetOpcode::EH_LABEL; }
  bool isGCLabel() const { return getOpcode() == TargetOpcode::GC_LABEL; }
  bool isAnnotationLabel() const {
    return getOpcode() == TargetOpcode::ANNOTATION_LABEL;
  }

  /// Returns true if the MachineInstr represents a label.
  bool isLabel() const {
    return isEHLabel() || isGCLabel() || isAnnotationLabel();
  }

  bool isCFIInstruction() const {
    return getOpcode() == TargetOpcode::CFI_INSTRUCTION;
  }

  bool isPseudoProbe() const {
    return getOpcode() == TargetOpcode::PSEUDO_PROBE;
  }

  // True if the instruction represents a position in the function.
  bool isPosition() const { return isLabel() || isCFIInstruction(); }

  bool isNonListDebugValue() const {
    return getOpcode() == TargetOpcode::DBG_VALUE;
  }
  bool isDebugValueList() const {
    return getOpcode() == TargetOpcode::DBG_VALUE_LIST;
  }
  bool isDebugValue() const {
    return isNonListDebugValue() || isDebugValueList();
  }
  bool isDebugLabel() const { return getOpcode() == TargetOpcode::DBG_LABEL; }
  bool isDebugRef() const { return getOpcode() == TargetOpcode::DBG_INSTR_REF; }
  bool isDebugValueLike() const { return isDebugValue() || isDebugRef(); }
  bool isDebugPHI() const { return getOpcode() == TargetOpcode::DBG_PHI; }
  bool isDebugInstr() const {
    return isDebugValue() || isDebugLabel() || isDebugRef() || isDebugPHI();
  }
  bool isDebugOrPseudoInstr() const {
    return isDebugInstr() || isPseudoProbe();
  }

  bool isDebugOffsetImm() const {
    return isNonListDebugValue() && getDebugOffset().isImm();
  }

  /// A DBG_VALUE is indirect iff the location operand is a register and
  /// the offset operand is an immediate.
  bool isIndirectDebugValue() const {
    return isDebugOffsetImm() && getDebugOperand(0).isReg();
  }

  /// A DBG_VALUE is an entry value iff its debug expression contains the
  /// DW_OP_LLVM_entry_value operation.
  bool isDebugEntryValue() const;

  /// Return true if the instruction is a debug value which describes a part of
  /// a variable as unavailable.
  bool isUndefDebugValue() const {
    if (!isDebugValue())
      return false;
    // If any $noreg locations are given, this DV is undef.
    for (const MachineOperand &Op : debug_operands())
      if (Op.isReg() && !Op.getReg().isValid())
        return true;
    return false;
  }

  bool isJumpTableDebugInfo() const {
    return getOpcode() == TargetOpcode::JUMP_TABLE_DEBUG_INFO;
  }

  bool isPHI() const {
    return getOpcode() == TargetOpcode::PHI ||
           getOpcode() == TargetOpcode::G_PHI;
  }
  bool isKill() const { return getOpcode() == TargetOpcode::KILL; }
  bool isImplicitDef() const { return getOpcode()==TargetOpcode::IMPLICIT_DEF; }
  bool isInlineAsm() const {
    return getOpcode() == TargetOpcode::INLINEASM ||
           getOpcode() == TargetOpcode::INLINEASM_BR;
  }
  /// Returns true if the register operand can be folded with a load or store
  /// into a frame index. Does so by checking the InlineAsm::Flag immediate
  /// operand at OpId - 1.
  bool mayFoldInlineAsmRegOp(unsigned OpId) const;

  bool isStackAligningInlineAsm() const;
  InlineAsm::AsmDialect getInlineAsmDialect() const;

  bool isInsertSubreg() const {
    return getOpcode() == TargetOpcode::INSERT_SUBREG;
  }

  bool isSubregToReg() const {
    return getOpcode() == TargetOpcode::SUBREG_TO_REG;
  }

  bool isRegSequence() const {
    return getOpcode() == TargetOpcode::REG_SEQUENCE;
  }

  bool isBundle() const {
    return getOpcode() == TargetOpcode::BUNDLE;
  }

  bool isCopy() const {
    return getOpcode() == TargetOpcode::COPY;
  }

  bool isFullCopy() const {
    return isCopy() && !getOperand(0).getSubReg() && !getOperand(1).getSubReg();
  }

  bool isExtractSubreg() const {
    return getOpcode() == TargetOpcode::EXTRACT_SUBREG;
  }

  /// Return true if the instruction behaves like a copy.
  /// This does not include native copy instructions.
  bool isCopyLike() const {
    return isCopy() || isSubregToReg();
  }

  /// Return true is the instruction is an identity copy.
  bool isIdentityCopy() const {
    return isCopy() && getOperand(0).getReg() == getOperand(1).getReg() &&
      getOperand(0).getSubReg() == getOperand(1).getSubReg();
  }

  /// Return true if this is a transient instruction that is either very likely
  /// to be eliminated during register allocation (such as copy-like
  /// instructions), or if this instruction doesn't have an execution-time cost.
  bool isTransient() const {
    switch (getOpcode()) {
    default:
      return isMetaInstruction();
    // Copy-like instructions are usually eliminated during register allocation.
    case TargetOpcode::PHI:
    case TargetOpcode::G_PHI:
    case TargetOpcode::COPY:
    case TargetOpcode::INSERT_SUBREG:
    case TargetOpcode::SUBREG_TO_REG:
    case TargetOpcode::REG_SEQUENCE:
      return true;
    }
  }

  /// Return the number of instructions inside the MI bundle, excluding the
  /// bundle header.
  ///
  /// This is the number of instructions that MachineBasicBlock::iterator
  /// skips, 0 for unbundled instructions.
  unsigned getBundleSize() const;

  /// Return true if the MachineInstr reads the specified register.
  /// If TargetRegisterInfo is non-null, then it also checks if there
  /// is a read of a super-register.
  /// This does not count partial redefines of virtual registers as reads:
  ///   %reg1024:6 = OP.
  bool readsRegister(Register Reg, const TargetRegisterInfo *TRI) const {
    return findRegisterUseOperandIdx(Reg, TRI, false) != -1;
  }

  /// Return true if the MachineInstr reads the specified virtual register.
  /// Take into account that a partial define is a
  /// read-modify-write operation.
  bool readsVirtualRegister(Register Reg) const {
    return readsWritesVirtualRegister(Reg).first;
  }

  /// Return a pair of bools (reads, writes) indicating if this instruction
  /// reads or writes Reg. This also considers partial defines.
  /// If Ops is not null, all operand indices for Reg are added.
  std::pair<bool,bool> readsWritesVirtualRegister(Register Reg,
                                SmallVectorImpl<unsigned> *Ops = nullptr) const;

  /// Return true if the MachineInstr kills the specified register.
  /// If TargetRegisterInfo is non-null, then it also checks if there is
  /// a kill of a super-register.
  bool killsRegister(Register Reg, const TargetRegisterInfo *TRI) const {
    return findRegisterUseOperandIdx(Reg, TRI, true) != -1;
  }

  /// Return true if the MachineInstr fully defines the specified register.
  /// If TargetRegisterInfo is non-null, then it also checks
  /// if there is a def of a super-register.
  /// NOTE: It's ignoring subreg indices on virtual registers.
  bool definesRegister(Register Reg, const TargetRegisterInfo *TRI) const {
    return findRegisterDefOperandIdx(Reg, TRI, false, false) != -1;
  }

  /// Return true if the MachineInstr modifies (fully define or partially
  /// define) the specified register.
  /// NOTE: It's ignoring subreg indices on virtual registers.
  bool modifiesRegister(Register Reg, const TargetRegisterInfo *TRI) const {
    return findRegisterDefOperandIdx(Reg, TRI, false, true) != -1;
  }

  /// Returns true if the register is dead in this machine instruction.
  /// If TargetRegisterInfo is non-null, then it also checks
  /// if there is a dead def of a super-register.
  bool registerDefIsDead(Register Reg, const TargetRegisterInfo *TRI) const {
    return findRegisterDefOperandIdx(Reg, TRI, true, false) != -1;
  }

  /// Returns true if the MachineInstr has an implicit-use operand of exactly
  /// the given register (not considering sub/super-registers).
  bool hasRegisterImplicitUseOperand(Register Reg) const;

  /// Returns the operand index that is a use of the specific register or -1
  /// if it is not found. It further tightens the search criteria to a use
  /// that kills the register if isKill is true.
  int findRegisterUseOperandIdx(Register Reg, const TargetRegisterInfo *TRI,
                                bool isKill = false) const;

  /// Wrapper for findRegisterUseOperandIdx, it returns
  /// a pointer to the MachineOperand rather than an index.
  MachineOperand *findRegisterUseOperand(Register Reg,
                                         const TargetRegisterInfo *TRI,
                                         bool isKill = false) {
    int Idx = findRegisterUseOperandIdx(Reg, TRI, isKill);
    return (Idx == -1) ? nullptr : &getOperand(Idx);
  }

  const MachineOperand *findRegisterUseOperand(Register Reg,
                                               const TargetRegisterInfo *TRI,
                                               bool isKill = false) const {
    return const_cast<MachineInstr *>(this)->findRegisterUseOperand(Reg, TRI,
                                                                    isKill);
  }

  /// Returns the operand index that is a def of the specified register or
  /// -1 if it is not found. If isDead is true, defs that are not dead are
  /// skipped. If Overlap is true, then it also looks for defs that merely
  /// overlap the specified register. If TargetRegisterInfo is non-null,
  /// then it also checks if there is a def of a super-register.
  /// This may also return a register mask operand when Overlap is true.
  int findRegisterDefOperandIdx(Register Reg, const TargetRegisterInfo *TRI,
                                bool isDead = false,
                                bool Overlap = false) const;

  /// Wrapper for findRegisterDefOperandIdx, it returns
  /// a pointer to the MachineOperand rather than an index.
  MachineOperand *findRegisterDefOperand(Register Reg,
                                         const TargetRegisterInfo *TRI,
                                         bool isDead = false,
                                         bool Overlap = false) {
    int Idx = findRegisterDefOperandIdx(Reg, TRI, isDead, Overlap);
    return (Idx == -1) ? nullptr : &getOperand(Idx);
  }

  const MachineOperand *findRegisterDefOperand(Register Reg,
                                               const TargetRegisterInfo *TRI,
                                               bool isDead = false,
                                               bool Overlap = false) const {
    return const_cast<MachineInstr *>(this)->findRegisterDefOperand(
        Reg, TRI, isDead, Overlap);
  }

  /// Find the index of the first operand in the
  /// operand list that is used to represent the predicate. It returns -1 if
  /// none is found.
  int findFirstPredOperandIdx() const;

  /// Find the index of the flag word operand that
  /// corresponds to operand OpIdx on an inline asm instruction.  Returns -1 if
  /// getOperand(OpIdx) does not belong to an inline asm operand group.
  ///
  /// If GroupNo is not NULL, it will receive the number of the operand group
  /// containing OpIdx.
  int findInlineAsmFlagIdx(unsigned OpIdx, unsigned *GroupNo = nullptr) const;

  /// Compute the static register class constraint for operand OpIdx.
  /// For normal instructions, this is derived from the MCInstrDesc.
  /// For inline assembly it is derived from the flag words.
  ///
  /// Returns NULL if the static register class constraint cannot be
  /// determined.
  const TargetRegisterClass*
  getRegClassConstraint(unsigned OpIdx,
                        const TargetInstrInfo *TII,
                        const TargetRegisterInfo *TRI) const;

  /// Applies the constraints (def/use) implied by this MI on \p Reg to
  /// the given \p CurRC.
  /// If \p ExploreBundle is set and MI is part of a bundle, all the
  /// instructions inside the bundle will be taken into account. In other words,
  /// this method accumulates all the constraints of the operand of this MI and
  /// the related bundle if MI is a bundle or inside a bundle.
  ///
  /// Returns the register class that satisfies both \p CurRC and the
  /// constraints set by MI. Returns NULL if such a register class does not
  /// exist.
  ///
  /// \pre CurRC must not be NULL.
  const TargetRegisterClass *getRegClassConstraintEffectForVReg(
      Register Reg, const TargetRegisterClass *CurRC,
      const TargetInstrInfo *TII, const TargetRegisterInfo *TRI,
      bool ExploreBundle = false) const;

  /// Applies the constraints (def/use) implied by the \p OpIdx operand
  /// to the given \p CurRC.
  ///
  /// Returns the register class that satisfies both \p CurRC and the
  /// constraints set by \p OpIdx MI. Returns NULL if such a register class
  /// does not exist.
  ///
  /// \pre CurRC must not be NULL.
  /// \pre The operand at \p OpIdx must be a register.
  const TargetRegisterClass *
  getRegClassConstraintEffect(unsigned OpIdx, const TargetRegisterClass *CurRC,
                              const TargetInstrInfo *TII,
                              const TargetRegisterInfo *TRI) const;

  /// Add a tie between the register operands at DefIdx and UseIdx.
  /// The tie will cause the register allocator to ensure that the two
  /// operands are assigned the same physical register.
  ///
  /// Tied operands are managed automatically for explicit operands in the
  /// MCInstrDesc. This method is for exceptional cases like inline asm.
  void tieOperands(unsigned DefIdx, unsigned UseIdx);

  /// Given the index of a tied register operand, find the
  /// operand it is tied to. Defs are tied to uses and vice versa. Returns the
  /// index of the tied operand which must exist.
  unsigned findTiedOperandIdx(unsigned OpIdx) const;

  /// Given the index of a register def operand,
  /// check if the register def is tied to a source operand, due to either
  /// two-address elimination or inline assembly constraints. Returns the
  /// first tied use operand index by reference if UseOpIdx is not null.
  bool isRegTiedToUseOperand(unsigned DefOpIdx,
                             unsigned *UseOpIdx = nullptr) const {
    const MachineOperand &MO = getOperand(DefOpIdx);
    if (!MO.isReg() || !MO.isDef() || !MO.isTied())
      return false;
    if (UseOpIdx)
      *UseOpIdx = findTiedOperandIdx(DefOpIdx);
    return true;
  }

  /// Return true if the use operand of the specified index is tied to a def
  /// operand. It also returns the def operand index by reference if DefOpIdx
  /// is not null.
  bool isRegTiedToDefOperand(unsigned UseOpIdx,
                             unsigned *DefOpIdx = nullptr) const {
    const MachineOperand &MO = getOperand(UseOpIdx);
    if (!MO.isReg() || !MO.isUse() || !MO.isTied())
      return false;
    if (DefOpIdx)
      *DefOpIdx = findTiedOperandIdx(UseOpIdx);
    return true;
  }

  /// Clears kill flags on all operands.
  void clearKillInfo();

  /// Replace all occurrences of FromReg with ToReg:SubIdx,
  /// properly composing subreg indices where necessary.
  void substituteRegister(Register FromReg, Register ToReg, unsigned SubIdx,
                          const TargetRegisterInfo &RegInfo);

  /// We have determined MI kills a register. Look for the
  /// operand that uses it and mark it as IsKill. If AddIfNotFound is true,
  /// add a implicit operand if it's not found. Returns true if the operand
  /// exists / is added.
  bool addRegisterKilled(Register IncomingReg,
                         const TargetRegisterInfo *RegInfo,
                         bool AddIfNotFound = false);

  /// Clear all kill flags affecting Reg.  If RegInfo is provided, this includes
  /// all aliasing registers.
  void clearRegisterKills(Register Reg, const TargetRegisterInfo *RegInfo);

  /// We have determined MI defined a register without a use.
  /// Look for the operand that defines it and mark it as IsDead. If
  /// AddIfNotFound is true, add a implicit operand if it's not found. Returns
  /// true if the operand exists / is added.
  bool addRegisterDead(Register Reg, const TargetRegisterInfo *RegInfo,
                       bool AddIfNotFound = false);

  /// Clear all dead flags on operands defining register @p Reg.
  void clearRegisterDeads(Register Reg);

  /// Mark all subregister defs of register @p Reg with the undef flag.
  /// This function is used when we determined to have a subregister def in an
  /// otherwise undefined super register.
  void setRegisterDefReadUndef(Register Reg, bool IsUndef = true);

  /// We have determined MI defines a register. Make sure there is an operand
  /// defining Reg.
  void addRegisterDefined(Register Reg,
                          const TargetRegisterInfo *RegInfo = nullptr);

  /// Mark every physreg used by this instruction as
  /// dead except those in the UsedRegs list.
  ///
  /// On instructions with register mask operands, also add implicit-def
  /// operands for all registers in UsedRegs.
  void setPhysRegsDeadExcept(ArrayRef<Register> UsedRegs,
                             const TargetRegisterInfo &TRI);

  /// Return true if it is safe to move this instruction. If
  /// SawStore is set to true, it means that there is a store (or call) between
  /// the instruction's location and its intended destination.
  bool isSafeToMove(AAResults *AA, bool &SawStore) const;

  /// Returns true if this instruction's memory access aliases the memory
  /// access of Other.
  //
  /// Assumes any physical registers used to compute addresses
  /// have the same value for both instructions.  Returns false if neither
  /// instruction writes to memory.
  ///
  /// @param AA Optional alias analysis, used to compare memory operands.
  /// @param Other MachineInstr to check aliasing against.
  /// @param UseTBAA Whether to pass TBAA information to alias analysis.
  bool mayAlias(AAResults *AA, const MachineInstr &Other, bool UseTBAA) const;

  /// Return true if this instruction may have an ordered
  /// or volatile memory reference, or if the information describing the memory
  /// reference is not available. Return false if it is known to have no
  /// ordered or volatile memory references.
  bool hasOrderedMemoryRef() const;

  /// Return true if this load instruction never traps and points to a memory
  /// location whose value doesn't change during the execution of this function.
  ///
  /// Examples include loading a value from the constant pool or from the
  /// argument area of a function (if it does not change).  If the instruction
  /// does multiple loads, this returns true only if all of the loads are
  /// dereferenceable and invariant.
  bool isDereferenceableInvariantLoad() const;

  /// If the specified instruction is a PHI that always merges together the
  /// same virtual register, return the register, otherwise return 0.
  unsigned isConstantValuePHI() const;

  /// Return true if this instruction has side effects that are not modeled
  /// by mayLoad / mayStore, etc.
  /// For all instructions, the property is encoded in MCInstrDesc::Flags
  /// (see MCInstrDesc::hasUnmodeledSideEffects(). The only exception is
  /// INLINEASM instruction, in which case the side effect property is encoded
  /// in one of its operands (see InlineAsm::Extra_HasSideEffect).
  ///
  bool hasUnmodeledSideEffects() const;

  /// Returns true if it is illegal to fold a load across this instruction.
  bool isLoadFoldBarrier() const;

  /// Return true if all the defs of this instruction are dead.
  bool allDefsAreDead() const;

  /// Return true if all the implicit defs of this instruction are dead.
  bool allImplicitDefsAreDead() const;

  /// Return a valid size if the instruction is a spill instruction.
  std::optional<LocationSize> getSpillSize(const TargetInstrInfo *TII) const;

  /// Return a valid size if the instruction is a folded spill instruction.
  std::optional<LocationSize>
  getFoldedSpillSize(const TargetInstrInfo *TII) const;

  /// Return a valid size if the instruction is a restore instruction.
  std::optional<LocationSize> getRestoreSize(const TargetInstrInfo *TII) const;

  /// Return a valid size if the instruction is a folded restore instruction.
  std::optional<LocationSize>
  getFoldedRestoreSize(const TargetInstrInfo *TII) const;

  /// Copy implicit register operands from specified
  /// instruction to this instruction.
  void copyImplicitOps(MachineFunction &MF, const MachineInstr &MI);

  /// Debugging support
  /// @{
  /// Determine the generic type to be printed (if needed) on uses and defs.
  LLT getTypeToPrint(unsigned OpIdx, SmallBitVector &PrintedTypes,
                     const MachineRegisterInfo &MRI) const;

  /// Return true when an instruction has tied register that can't be determined
  /// by the instruction's descriptor. This is useful for MIR printing, to
  /// determine whether we need to print the ties or not.
  bool hasComplexRegisterTies() const;

  /// Print this MI to \p OS.
  /// Don't print information that can be inferred from other instructions if
  /// \p IsStandalone is false. It is usually true when only a fragment of the
  /// function is printed.
  /// Only print the defs and the opcode if \p SkipOpers is true.
  /// Otherwise, also print operands if \p SkipDebugLoc is true.
  /// Otherwise, also print the debug loc, with a terminating newline.
  /// \p TII is used to print the opcode name.  If it's not present, but the
  /// MI is in a function, the opcode will be printed using the function's TII.
  void print(raw_ostream &OS, bool IsStandalone = true, bool SkipOpers = false,
             bool SkipDebugLoc = false, bool AddNewLine = true,
             const TargetInstrInfo *TII = nullptr) const;
  void print(raw_ostream &OS, ModuleSlotTracker &MST, bool IsStandalone = true,
             bool SkipOpers = false, bool SkipDebugLoc = false,
             bool AddNewLine = true,
             const TargetInstrInfo *TII = nullptr) const;
  void dump() const;
  /// Print on dbgs() the current instruction and the instructions defining its
  /// operands and so on until we reach \p MaxDepth.
  void dumpr(const MachineRegisterInfo &MRI,
             unsigned MaxDepth = UINT_MAX) const;
  /// @}

  //===--------------------------------------------------------------------===//
  // Accessors used to build up machine instructions.

  /// Add the specified operand to the instruction.  If it is an implicit
  /// operand, it is added to the end of the operand list.  If it is an
  /// explicit operand it is added at the end of the explicit operand list
  /// (before the first implicit operand).
  ///
  /// MF must be the machine function that was used to allocate this
  /// instruction.
  ///
  /// MachineInstrBuilder provides a more convenient interface for creating
  /// instructions and adding operands.
  void addOperand(MachineFunction &MF, const MachineOperand &Op);

  /// Add an operand without providing an MF reference. This only works for
  /// instructions that are inserted in a basic block.
  ///
  /// MachineInstrBuilder and the two-argument addOperand(MF, MO) should be
  /// preferred.
  void addOperand(const MachineOperand &Op);

  /// Inserts Ops BEFORE It. Can untie/retie tied operands.
  void insert(mop_iterator InsertBefore, ArrayRef<MachineOperand> Ops);

  /// Replace the instruction descriptor (thus opcode) of
  /// the current instruction with a new one.
  void setDesc(const MCInstrDesc &TID);

  /// Replace current source information with new such.
  /// Avoid using this, the constructor argument is preferable.
  void setDebugLoc(DebugLoc DL) {
    DbgLoc = std::move(DL);
    assert(DbgLoc.hasTrivialDestructor() && "Expected trivial destructor");
  }

  /// Erase an operand from an instruction, leaving it with one
  /// fewer operand than it started with.
  void removeOperand(unsigned OpNo);

  /// Clear this MachineInstr's memory reference descriptor list.  This resets
  /// the memrefs to their most conservative state.  This should be used only
  /// as a last resort since it greatly pessimizes our knowledge of the memory
  /// access performed by the instruction.
  void dropMemRefs(MachineFunction &MF);

  /// Assign this MachineInstr's memory reference descriptor list.
  ///
  /// Unlike other methods, this *will* allocate them into a new array
  /// associated with the provided `MachineFunction`.
  void setMemRefs(MachineFunction &MF, ArrayRef<MachineMemOperand *> MemRefs);

  /// Add a MachineMemOperand to the machine instruction.
  /// This function should be used only occasionally. The setMemRefs function
  /// is the primary method for setting up a MachineInstr's MemRefs list.
  void addMemOperand(MachineFunction &MF, MachineMemOperand *MO);

  /// Clone another MachineInstr's memory reference descriptor list and replace
  /// ours with it.
  ///
  /// Note that `*this` may be the incoming MI!
  ///
  /// Prefer this API whenever possible as it can avoid allocations in common
  /// cases.
  void cloneMemRefs(MachineFunction &MF, const MachineInstr &MI);

  /// Clone the merge of multiple MachineInstrs' memory reference descriptors
  /// list and replace ours with it.
  ///
  /// Note that `*this` may be one of the incoming MIs!
  ///
  /// Prefer this API whenever possible as it can avoid allocations in common
  /// cases.
  void cloneMergedMemRefs(MachineFunction &MF,
                          ArrayRef<const MachineInstr *> MIs);

  /// Set a symbol that will be emitted just prior to the instruction itself.
  ///
  /// Setting this to a null pointer will remove any such symbol.
  ///
  /// FIXME: This is not fully implemented yet.
  void setPreInstrSymbol(MachineFunction &MF, MCSymbol *Symbol);

  /// Set a symbol that will be emitted just after the instruction itself.
  ///
  /// Setting this to a null pointer will remove any such symbol.
  ///
  /// FIXME: This is not fully implemented yet.
  void setPostInstrSymbol(MachineFunction &MF, MCSymbol *Symbol);

  /// Clone another MachineInstr's pre- and post- instruction symbols and
  /// replace ours with it.
  void cloneInstrSymbols(MachineFunction &MF, const MachineInstr &MI);

  /// Set a marker on instructions that denotes where we should create and emit
  /// heap alloc site labels. This waits until after instruction selection and
  /// optimizations to create the label, so it should still work if the
  /// instruction is removed or duplicated.
  void setHeapAllocMarker(MachineFunction &MF, MDNode *MD);

  // Set metadata on instructions that say which sections to emit instruction
  // addresses into.
  void setPCSections(MachineFunction &MF, MDNode *MD);

  void setMMRAMetadata(MachineFunction &MF, MDNode *MMRAs);

  /// Set the CFI type for the instruction.
  void setCFIType(MachineFunction &MF, uint32_t Type);

  /// Return the MIFlags which represent both MachineInstrs. This
  /// should be used when merging two MachineInstrs into one. This routine does
  /// not modify the MIFlags of this MachineInstr.
  uint32_t mergeFlagsWith(const MachineInstr& Other) const;

  static uint32_t copyFlagsFromInstruction(const Instruction &I);

  /// Copy all flags to MachineInst MIFlags
  void copyIRFlags(const Instruction &I);

  /// Break any tie involving OpIdx.
  void untieRegOperand(unsigned OpIdx) {
    MachineOperand &MO = getOperand(OpIdx);
    if (MO.isReg() && MO.isTied()) {
      getOperand(findTiedOperandIdx(OpIdx)).TiedTo = 0;
      MO.TiedTo = 0;
    }
  }

  /// Add all implicit def and use operands to this instruction.
  void addImplicitDefUseOperands(MachineFunction &MF);

  /// Scan instructions immediately following MI and collect any matching
  /// DBG_VALUEs.
  void collectDebugValues(SmallVectorImpl<MachineInstr *> &DbgValues);

  /// Find all DBG_VALUEs that point to the register def in this instruction
  /// and point them to \p Reg instead.
  void changeDebugValuesDefReg(Register Reg);

  /// Sets all register debug operands in this debug value instruction to be
  /// undef.
  void setDebugValueUndef() {
    assert(isDebugValue() && "Must be a debug value instruction.");
    for (MachineOperand &MO : debug_operands()) {
      if (MO.isReg()) {
        MO.setReg(0);
        MO.setSubReg(0);
      }
    }
  }

  std::tuple<Register, Register> getFirst2Regs() const {
    return std::tuple(getOperand(0).getReg(), getOperand(1).getReg());
  }

  std::tuple<Register, Register, Register> getFirst3Regs() const {
    return std::tuple(getOperand(0).getReg(), getOperand(1).getReg(),
                      getOperand(2).getReg());
  }

  std::tuple<Register, Register, Register, Register> getFirst4Regs() const {
    return std::tuple(getOperand(0).getReg(), getOperand(1).getReg(),
                      getOperand(2).getReg(), getOperand(3).getReg());
  }

  std::tuple<Register, Register, Register, Register, Register>
  getFirst5Regs() const {
    return std::tuple(getOperand(0).getReg(), getOperand(1).getReg(),
                      getOperand(2).getReg(), getOperand(3).getReg(),
                      getOperand(4).getReg());
  }

  std::tuple<LLT, LLT> getFirst2LLTs() const;
  std::tuple<LLT, LLT, LLT> getFirst3LLTs() const;
  std::tuple<LLT, LLT, LLT, LLT> getFirst4LLTs() const;
  std::tuple<LLT, LLT, LLT, LLT, LLT> getFirst5LLTs() const;

  std::tuple<Register, LLT, Register, LLT> getFirst2RegLLTs() const;
  std::tuple<Register, LLT, Register, LLT, Register, LLT>
  getFirst3RegLLTs() const;
  std::tuple<Register, LLT, Register, LLT, Register, LLT, Register, LLT>
  getFirst4RegLLTs() const;
  std::tuple<Register, LLT, Register, LLT, Register, LLT, Register, LLT,
             Register, LLT>
  getFirst5RegLLTs() const;

private:
  /// If this instruction is embedded into a MachineFunction, return the
  /// MachineRegisterInfo object for the current function, otherwise
  /// return null.
  MachineRegisterInfo *getRegInfo();
  const MachineRegisterInfo *getRegInfo() const;

  /// Unlink all of the register operands in this instruction from their
  /// respective use lists.  This requires that the operands already be on their
  /// use lists.
  void removeRegOperandsFromUseLists(MachineRegisterInfo&);

  /// Add all of the register operands in this instruction from their
  /// respective use lists.  This requires that the operands not be on their
  /// use lists yet.
  void addRegOperandsToUseLists(MachineRegisterInfo&);

  /// Slow path for hasProperty when we're dealing with a bundle.
  bool hasPropertyInBundle(uint64_t Mask, QueryType Type) const;

  /// Implements the logic of getRegClassConstraintEffectForVReg for the
  /// this MI and the given operand index \p OpIdx.
  /// If the related operand does not constrained Reg, this returns CurRC.
  const TargetRegisterClass *getRegClassConstraintEffectForVRegImpl(
      unsigned OpIdx, Register Reg, const TargetRegisterClass *CurRC,
      const TargetInstrInfo *TII, const TargetRegisterInfo *TRI) const;

  /// Stores extra instruction information inline or allocates as ExtraInfo
  /// based on the number of pointers.
  void setExtraInfo(MachineFunction &MF, ArrayRef<MachineMemOperand *> MMOs,
                    MCSymbol *PreInstrSymbol, MCSymbol *PostInstrSymbol,
                    MDNode *HeapAllocMarker, MDNode *PCSections,
                    uint32_t CFIType, MDNode *MMRAs);
};

/// Special DenseMapInfo traits to compare MachineInstr* by *value* of the
/// instruction rather than by pointer value.
/// The hashing and equality testing functions ignore definitions so this is
/// useful for CSE, etc.
struct MachineInstrExpressionTrait : DenseMapInfo<MachineInstr*> {
  static inline MachineInstr *getEmptyKey() {
    return nullptr;
  }

  static inline MachineInstr *getTombstoneKey() {
    return reinterpret_cast<MachineInstr*>(-1);
  }

  static unsigned getHashValue(const MachineInstr* const &MI);

  static bool isEqual(const MachineInstr* const &LHS,
                      const MachineInstr* const &RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey() ||
        LHS == getEmptyKey() || LHS == getTombstoneKey())
      return LHS == RHS;
    return LHS->isIdenticalTo(*RHS, MachineInstr::IgnoreVRegDefs);
  }
};

//===----------------------------------------------------------------------===//
// Debugging Support

inline raw_ostream& operator<<(raw_ostream &OS, const MachineInstr &MI) {
  MI.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEINSTR_H
