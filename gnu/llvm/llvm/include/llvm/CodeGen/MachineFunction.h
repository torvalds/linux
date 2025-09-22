//===- llvm/CodeGen/MachineFunction.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Collect native machine code for a function.  This class contains a list of
// MachineBasicBlock instances that make up the current compiled function.
//
// This class also contains pointers to various classes which hold
// target-specific information about the generated code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEFUNCTION_H
#define LLVM_CODEGEN_MACHINEFUNCTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/iterator.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ArrayRecycler.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Recycler.h"
#include "llvm/Target/TargetOptions.h"
#include <bitset>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace llvm {

class BasicBlock;
class BlockAddress;
class DataLayout;
class DebugLoc;
struct DenormalMode;
class DIExpression;
class DILocalVariable;
class DILocation;
class Function;
class GISelChangeObserver;
class GlobalValue;
class LLVMTargetMachine;
class MachineConstantPool;
class MachineFrameInfo;
class MachineFunction;
class MachineJumpTableInfo;
class MachineModuleInfo;
class MachineRegisterInfo;
class MCContext;
class MCInstrDesc;
class MCSymbol;
class MCSection;
class Pass;
class PseudoSourceValueManager;
class raw_ostream;
class SlotIndexes;
class StringRef;
class TargetRegisterClass;
class TargetSubtargetInfo;
struct WasmEHFuncInfo;
struct WinEHFuncInfo;

template <> struct ilist_alloc_traits<MachineBasicBlock> {
  void deleteNode(MachineBasicBlock *MBB);
};

template <> struct ilist_callback_traits<MachineBasicBlock> {
  void addNodeToList(MachineBasicBlock* N);
  void removeNodeFromList(MachineBasicBlock* N);

  template <class Iterator>
  void transferNodesFromList(ilist_callback_traits &OldList, Iterator, Iterator) {
    assert(this == &OldList && "never transfer MBBs between functions");
  }
};

/// MachineFunctionInfo - This class can be derived from and used by targets to
/// hold private target-specific information for each MachineFunction.  Objects
/// of type are accessed/created with MF::getInfo and destroyed when the
/// MachineFunction is destroyed.
struct MachineFunctionInfo {
  virtual ~MachineFunctionInfo();

  /// Factory function: default behavior is to call new using the
  /// supplied allocator.
  ///
  /// This function can be overridden in a derive class.
  template <typename FuncInfoTy, typename SubtargetTy = TargetSubtargetInfo>
  static FuncInfoTy *create(BumpPtrAllocator &Allocator, const Function &F,
                            const SubtargetTy *STI) {
    return new (Allocator.Allocate<FuncInfoTy>()) FuncInfoTy(F, STI);
  }

  template <typename Ty>
  static Ty *create(BumpPtrAllocator &Allocator, const Ty &MFI) {
    return new (Allocator.Allocate<Ty>()) Ty(MFI);
  }

  /// Make a functionally equivalent copy of this MachineFunctionInfo in \p MF.
  /// This requires remapping MachineBasicBlock references from the original
  /// parent to values in the new function. Targets may assume that virtual
  /// register and frame index values are preserved in the new function.
  virtual MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const {
    return nullptr;
  }
};

/// Properties which a MachineFunction may have at a given point in time.
/// Each of these has checking code in the MachineVerifier, and passes can
/// require that a property be set.
class MachineFunctionProperties {
  // Possible TODO: Allow targets to extend this (perhaps by allowing the
  // constructor to specify the size of the bit vector)
  // Possible TODO: Allow requiring the negative (e.g. VRegsAllocated could be
  // stated as the negative of "has vregs"

public:
  // The properties are stated in "positive" form; i.e. a pass could require
  // that the property hold, but not that it does not hold.

  // Property descriptions:
  // IsSSA: True when the machine function is in SSA form and virtual registers
  //  have a single def.
  // NoPHIs: The machine function does not contain any PHI instruction.
  // TracksLiveness: True when tracking register liveness accurately.
  //  While this property is set, register liveness information in basic block
  //  live-in lists and machine instruction operands (e.g. implicit defs) is
  //  accurate, kill flags are conservatively accurate (kill flag correctly
  //  indicates the last use of a register, an operand without kill flag may or
  //  may not be the last use of a register). This means it can be used to
  //  change the code in ways that affect the values in registers, for example
  //  by the register scavenger.
  //  When this property is cleared at a very late time, liveness is no longer
  //  reliable.
  // NoVRegs: The machine function does not use any virtual registers.
  // Legalized: In GlobalISel: the MachineLegalizer ran and all pre-isel generic
  //  instructions have been legalized; i.e., all instructions are now one of:
  //   - generic and always legal (e.g., COPY)
  //   - target-specific
  //   - legal pre-isel generic instructions.
  // RegBankSelected: In GlobalISel: the RegBankSelect pass ran and all generic
  //  virtual registers have been assigned to a register bank.
  // Selected: In GlobalISel: the InstructionSelect pass ran and all pre-isel
  //  generic instructions have been eliminated; i.e., all instructions are now
  //  target-specific or non-pre-isel generic instructions (e.g., COPY).
  //  Since only pre-isel generic instructions can have generic virtual register
  //  operands, this also means that all generic virtual registers have been
  //  constrained to virtual registers (assigned to register classes) and that
  //  all sizes attached to them have been eliminated.
  // TiedOpsRewritten: The twoaddressinstruction pass will set this flag, it
  //  means that tied-def have been rewritten to meet the RegConstraint.
  // FailsVerification: Means that the function is not expected to pass machine
  //  verification. This can be set by passes that introduce known problems that
  //  have not been fixed yet.
  // TracksDebugUserValues: Without this property enabled, debug instructions
  // such as DBG_VALUE are allowed to reference virtual registers even if those
  // registers do not have a definition. With the property enabled virtual
  // registers must only be used if they have a definition. This property
  // allows earlier passes in the pipeline to skip updates of `DBG_VALUE`
  // instructions to save compile time.
  enum class Property : unsigned {
    IsSSA,
    NoPHIs,
    TracksLiveness,
    NoVRegs,
    FailedISel,
    Legalized,
    RegBankSelected,
    Selected,
    TiedOpsRewritten,
    FailsVerification,
    TracksDebugUserValues,
    LastProperty = TracksDebugUserValues,
  };

  bool hasProperty(Property P) const {
    return Properties[static_cast<unsigned>(P)];
  }

  MachineFunctionProperties &set(Property P) {
    Properties.set(static_cast<unsigned>(P));
    return *this;
  }

  MachineFunctionProperties &reset(Property P) {
    Properties.reset(static_cast<unsigned>(P));
    return *this;
  }

  /// Reset all the properties.
  MachineFunctionProperties &reset() {
    Properties.reset();
    return *this;
  }

  MachineFunctionProperties &set(const MachineFunctionProperties &MFP) {
    Properties |= MFP.Properties;
    return *this;
  }

  MachineFunctionProperties &reset(const MachineFunctionProperties &MFP) {
    Properties &= ~MFP.Properties;
    return *this;
  }

  // Returns true if all properties set in V (i.e. required by a pass) are set
  // in this.
  bool verifyRequiredProperties(const MachineFunctionProperties &V) const {
    return (Properties | ~V.Properties).all();
  }

  /// Print the MachineFunctionProperties in human-readable form.
  void print(raw_ostream &OS) const;

private:
  std::bitset<static_cast<unsigned>(Property::LastProperty) + 1> Properties;
};

struct SEHHandler {
  /// Filter or finally function. Null indicates a catch-all.
  const Function *FilterOrFinally;

  /// Address of block to recover at. Null for a finally handler.
  const BlockAddress *RecoverBA;
};

/// This structure is used to retain landing pad info for the current function.
struct LandingPadInfo {
  MachineBasicBlock *LandingPadBlock;      // Landing pad block.
  SmallVector<MCSymbol *, 1> BeginLabels;  // Labels prior to invoke.
  SmallVector<MCSymbol *, 1> EndLabels;    // Labels after invoke.
  SmallVector<SEHHandler, 1> SEHHandlers;  // SEH handlers active at this lpad.
  MCSymbol *LandingPadLabel = nullptr;     // Label at beginning of landing pad.
  std::vector<int> TypeIds;                // List of type ids (filters negative).

  explicit LandingPadInfo(MachineBasicBlock *MBB)
      : LandingPadBlock(MBB) {}
};

class LLVM_EXTERNAL_VISIBILITY MachineFunction {
  Function &F;
  const LLVMTargetMachine &Target;
  const TargetSubtargetInfo *STI;
  MCContext &Ctx;
  MachineModuleInfo &MMI;

  // RegInfo - Information about each register in use in the function.
  MachineRegisterInfo *RegInfo;

  // Used to keep track of target-specific per-machine-function information for
  // the target implementation.
  MachineFunctionInfo *MFInfo;

  // Keep track of objects allocated on the stack.
  MachineFrameInfo *FrameInfo;

  // Keep track of constants which are spilled to memory
  MachineConstantPool *ConstantPool;

  // Keep track of jump tables for switch instructions
  MachineJumpTableInfo *JumpTableInfo;

  // Keep track of the function section.
  MCSection *Section = nullptr;

  // Catchpad unwind destination info for wasm EH.
  // Keeps track of Wasm exception handling related data. This will be null for
  // functions that aren't using a wasm EH personality.
  WasmEHFuncInfo *WasmEHInfo = nullptr;

  // Keeps track of Windows exception handling related data. This will be null
  // for functions that aren't using a funclet-based EH personality.
  WinEHFuncInfo *WinEHInfo = nullptr;

  // Function-level unique numbering for MachineBasicBlocks.  When a
  // MachineBasicBlock is inserted into a MachineFunction is it automatically
  // numbered and this vector keeps track of the mapping from ID's to MBB's.
  std::vector<MachineBasicBlock*> MBBNumbering;

  // Pool-allocate MachineFunction-lifetime and IR objects.
  BumpPtrAllocator Allocator;

  // Allocation management for instructions in function.
  Recycler<MachineInstr> InstructionRecycler;

  // Allocation management for operand arrays on instructions.
  ArrayRecycler<MachineOperand> OperandRecycler;

  // Allocation management for basic blocks in function.
  Recycler<MachineBasicBlock> BasicBlockRecycler;

  // List of machine basic blocks in function
  using BasicBlockListType = ilist<MachineBasicBlock>;
  BasicBlockListType BasicBlocks;

  /// FunctionNumber - This provides a unique ID for each function emitted in
  /// this translation unit.
  ///
  unsigned FunctionNumber;

  /// Alignment - The alignment of the function.
  Align Alignment;

  /// ExposesReturnsTwice - True if the function calls setjmp or related
  /// functions with attribute "returns twice", but doesn't have
  /// the attribute itself.
  /// This is used to limit optimizations which cannot reason
  /// about the control flow of such functions.
  bool ExposesReturnsTwice = false;

  /// True if the function includes any inline assembly.
  bool HasInlineAsm = false;

  /// True if any WinCFI instruction have been emitted in this function.
  bool HasWinCFI = false;

  /// Current high-level properties of the IR of the function (e.g. is in SSA
  /// form or whether registers have been allocated)
  MachineFunctionProperties Properties;

  // Allocation management for pseudo source values.
  std::unique_ptr<PseudoSourceValueManager> PSVManager;

  /// List of moves done by a function's prolog.  Used to construct frame maps
  /// by debug and exception handling consumers.
  std::vector<MCCFIInstruction> FrameInstructions;

  /// List of basic blocks immediately following calls to _setjmp. Used to
  /// construct a table of valid longjmp targets for Windows Control Flow Guard.
  std::vector<MCSymbol *> LongjmpTargets;

  /// List of basic blocks that are the target of catchrets. Used to construct
  /// a table of valid targets for Windows EHCont Guard.
  std::vector<MCSymbol *> CatchretTargets;

  /// \name Exception Handling
  /// \{

  /// List of LandingPadInfo describing the landing pad information.
  std::vector<LandingPadInfo> LandingPads;

  /// Map a landing pad's EH symbol to the call site indexes.
  DenseMap<MCSymbol*, SmallVector<unsigned, 4>> LPadToCallSiteMap;

  /// Map a landing pad to its index.
  DenseMap<const MachineBasicBlock *, unsigned> WasmLPadToIndexMap;

  /// Map of invoke call site index values to associated begin EH_LABEL.
  DenseMap<MCSymbol*, unsigned> CallSiteMap;

  /// CodeView label annotations.
  std::vector<std::pair<MCSymbol *, MDNode *>> CodeViewAnnotations;

  bool CallsEHReturn = false;
  bool CallsUnwindInit = false;
  bool HasEHCatchret = false;
  bool HasEHScopes = false;
  bool HasEHFunclets = false;
  bool IsOutlined = false;

  /// BBID to assign to the next basic block of this function.
  unsigned NextBBID = 0;

  /// Section Type for basic blocks, only relevant with basic block sections.
  BasicBlockSection BBSectionsType = BasicBlockSection::None;

  /// List of C++ TypeInfo used.
  std::vector<const GlobalValue *> TypeInfos;

  /// List of typeids encoding filters used.
  std::vector<unsigned> FilterIds;

  /// List of the indices in FilterIds corresponding to filter terminators.
  std::vector<unsigned> FilterEnds;

  EHPersonality PersonalityTypeCache = EHPersonality::Unknown;

  /// \}

  /// Clear all the members of this MachineFunction, but the ones used
  /// to initialize again the MachineFunction.
  /// More specifically, this deallocates all the dynamically allocated
  /// objects and get rid of all the XXXInfo data structure, but keep
  /// unchanged the references to Fn, Target, MMI, and FunctionNumber.
  void clear();
  /// Allocate and initialize the different members.
  /// In particular, the XXXInfo data structure.
  /// \pre Fn, Target, MMI, and FunctionNumber are properly set.
  void init();

public:
  /// Description of the location of a variable whose Address is valid and
  /// unchanging during function execution. The Address may be:
  /// * A stack index, which can be negative for fixed stack objects.
  /// * A MCRegister, whose entry value contains the address of the variable.
  class VariableDbgInfo {
    std::variant<int, MCRegister> Address;

  public:
    const DILocalVariable *Var;
    const DIExpression *Expr;
    const DILocation *Loc;

    VariableDbgInfo(const DILocalVariable *Var, const DIExpression *Expr,
                    int Slot, const DILocation *Loc)
        : Address(Slot), Var(Var), Expr(Expr), Loc(Loc) {}

    VariableDbgInfo(const DILocalVariable *Var, const DIExpression *Expr,
                    MCRegister EntryValReg, const DILocation *Loc)
        : Address(EntryValReg), Var(Var), Expr(Expr), Loc(Loc) {}

    /// Return true if this variable is in a stack slot.
    bool inStackSlot() const { return std::holds_alternative<int>(Address); }

    /// Return true if this variable is in the entry value of a register.
    bool inEntryValueRegister() const {
      return std::holds_alternative<MCRegister>(Address);
    }

    /// Returns the stack slot of this variable, assuming `inStackSlot()` is
    /// true.
    int getStackSlot() const { return std::get<int>(Address); }

    /// Returns the MCRegister of this variable, assuming
    /// `inEntryValueRegister()` is true.
    MCRegister getEntryValueRegister() const {
      return std::get<MCRegister>(Address);
    }

    /// Updates the stack slot of this variable, assuming `inStackSlot()` is
    /// true.
    void updateStackSlot(int NewSlot) {
      assert(inStackSlot());
      Address = NewSlot;
    }
  };

  class Delegate {
    virtual void anchor();

  public:
    virtual ~Delegate() = default;
    /// Callback after an insertion. This should not modify the MI directly.
    virtual void MF_HandleInsertion(MachineInstr &MI) = 0;
    /// Callback before a removal. This should not modify the MI directly.
    virtual void MF_HandleRemoval(MachineInstr &MI) = 0;
    /// Callback before changing MCInstrDesc. This should not modify the MI
    /// directly.
    virtual void MF_HandleChangeDesc(MachineInstr &MI, const MCInstrDesc &TID) {
      return;
    }
  };

  /// Structure used to represent pair of argument number after call lowering
  /// and register used to transfer that argument.
  /// For now we support only cases when argument is transferred through one
  /// register.
  struct ArgRegPair {
    Register Reg;
    uint16_t ArgNo;
    ArgRegPair(Register R, unsigned Arg) : Reg(R), ArgNo(Arg) {
      assert(Arg < (1 << 16) && "Arg out of range");
    }
  };

  struct CallSiteInfo {
    /// Vector of call argument and its forwarding register.
    SmallVector<ArgRegPair, 1> ArgRegPairs;
  };

private:
  Delegate *TheDelegate = nullptr;
  GISelChangeObserver *Observer = nullptr;

  using CallSiteInfoMap = DenseMap<const MachineInstr *, CallSiteInfo>;
  /// Map a call instruction to call site arguments forwarding info.
  CallSiteInfoMap CallSitesInfo;

  /// A helper function that returns call site info for a give call
  /// instruction if debug entry value support is enabled.
  CallSiteInfoMap::iterator getCallSiteInfo(const MachineInstr *MI);

  // Callbacks for insertion and removal.
  void handleInsertion(MachineInstr &MI);
  void handleRemoval(MachineInstr &MI);
  friend struct ilist_traits<MachineInstr>;

public:
  // Need to be accessed from MachineInstr::setDesc.
  void handleChangeDesc(MachineInstr &MI, const MCInstrDesc &TID);

  using VariableDbgInfoMapTy = SmallVector<VariableDbgInfo, 4>;
  VariableDbgInfoMapTy VariableDbgInfos;

  /// A count of how many instructions in the function have had numbers
  /// assigned to them. Used for debug value tracking, to determine the
  /// next instruction number.
  unsigned DebugInstrNumberingCount = 0;

  /// Set value of DebugInstrNumberingCount field. Avoid using this unless
  /// you're deserializing this data.
  void setDebugInstrNumberingCount(unsigned Num);

  /// Pair of instruction number and operand number.
  using DebugInstrOperandPair = std::pair<unsigned, unsigned>;

  /// Replacement definition for a debug instruction reference. Made up of a
  /// source instruction / operand pair, destination pair, and a qualifying
  /// subregister indicating what bits in the operand make up the substitution.
  // For example, a debug user
  /// of %1:
  ///    %0:gr32 = someinst, debug-instr-number 1
  ///    %1:gr16 = %0.some_16_bit_subreg, debug-instr-number 2
  /// Would receive the substitution {{2, 0}, {1, 0}, $subreg}, where $subreg is
  /// the subregister number for some_16_bit_subreg.
  class DebugSubstitution {
  public:
    DebugInstrOperandPair Src;  ///< Source instruction / operand pair.
    DebugInstrOperandPair Dest; ///< Replacement instruction / operand pair.
    unsigned Subreg;            ///< Qualifier for which part of Dest is read.

    DebugSubstitution(const DebugInstrOperandPair &Src,
                      const DebugInstrOperandPair &Dest, unsigned Subreg)
        : Src(Src), Dest(Dest), Subreg(Subreg) {}

    /// Order only by source instruction / operand pair: there should never
    /// be duplicate entries for the same source in any collection.
    bool operator<(const DebugSubstitution &Other) const {
      return Src < Other.Src;
    }
  };

  /// Debug value substitutions: a collection of DebugSubstitution objects,
  /// recording changes in where a value is defined. For example, when one
  /// instruction is substituted for another. Keeping a record allows recovery
  /// of variable locations after compilation finishes.
  SmallVector<DebugSubstitution, 8> DebugValueSubstitutions;

  /// Location of a PHI instruction that is also a debug-info variable value,
  /// for the duration of register allocation. Loaded by the PHI-elimination
  /// pass, and emitted as DBG_PHI instructions during VirtRegRewriter, with
  /// maintenance applied by intermediate passes that edit registers (such as
  /// coalescing and the allocator passes).
  class DebugPHIRegallocPos {
  public:
    MachineBasicBlock *MBB; ///< Block where this PHI was originally located.
    Register Reg;           ///< VReg where the control-flow-merge happens.
    unsigned SubReg;        ///< Optional subreg qualifier within Reg.
    DebugPHIRegallocPos(MachineBasicBlock *MBB, Register Reg, unsigned SubReg)
        : MBB(MBB), Reg(Reg), SubReg(SubReg) {}
  };

  /// Map of debug instruction numbers to the position of their PHI instructions
  /// during register allocation. See DebugPHIRegallocPos.
  DenseMap<unsigned, DebugPHIRegallocPos> DebugPHIPositions;

  /// Flag for whether this function contains DBG_VALUEs (false) or
  /// DBG_INSTR_REF (true).
  bool UseDebugInstrRef = false;

  /// Create a substitution between one <instr,operand> value to a different,
  /// new value.
  void makeDebugValueSubstitution(DebugInstrOperandPair, DebugInstrOperandPair,
                                  unsigned SubReg = 0);

  /// Create substitutions for any tracked values in \p Old, to point at
  /// \p New. Needed when we re-create an instruction during optimization,
  /// which has the same signature (i.e., def operands in the same place) but
  /// a modified instruction type, flags, or otherwise. An example: X86 moves
  /// are sometimes transformed into equivalent LEAs.
  /// If the two instructions are not the same opcode, limit which operands to
  /// examine for substitutions to the first N operands by setting
  /// \p MaxOperand.
  void substituteDebugValuesForInst(const MachineInstr &Old, MachineInstr &New,
                                    unsigned MaxOperand = UINT_MAX);

  /// Find the underlying  defining instruction / operand for a COPY instruction
  /// while in SSA form. Copies do not actually define values -- they move them
  /// between registers. Labelling a COPY-like instruction with an instruction
  /// number is to be avoided as it makes value numbers non-unique later in
  /// compilation. This method follows the definition chain for any sequence of
  /// COPY-like instructions to find whatever non-COPY-like instruction defines
  /// the copied value; or for parameters, creates a DBG_PHI on entry.
  /// May insert instructions into the entry block!
  /// \p MI The copy-like instruction to salvage.
  /// \p DbgPHICache A container to cache already-solved COPYs.
  /// \returns An instruction/operand pair identifying the defining value.
  DebugInstrOperandPair
  salvageCopySSA(MachineInstr &MI,
                 DenseMap<Register, DebugInstrOperandPair> &DbgPHICache);

  DebugInstrOperandPair salvageCopySSAImpl(MachineInstr &MI);

  /// Finalise any partially emitted debug instructions. These are DBG_INSTR_REF
  /// instructions where we only knew the vreg of the value they use, not the
  /// instruction that defines that vreg. Once isel finishes, we should have
  /// enough information for every DBG_INSTR_REF to point at an instruction
  /// (or DBG_PHI).
  void finalizeDebugInstrRefs();

  /// Determine whether, in the current machine configuration, we should use
  /// instruction referencing or not.
  bool shouldUseDebugInstrRef() const;

  /// Returns true if the function's variable locations are tracked with
  /// instruction referencing.
  bool useDebugInstrRef() const;

  /// Set whether this function will use instruction referencing or not.
  void setUseDebugInstrRef(bool UseInstrRef);

  /// A reserved operand number representing the instructions memory operand,
  /// for instructions that have a stack spill fused into them.
  const static unsigned int DebugOperandMemNumber;

  MachineFunction(Function &F, const LLVMTargetMachine &Target,
                  const TargetSubtargetInfo &STI, unsigned FunctionNum,
                  MachineModuleInfo &MMI);
  MachineFunction(const MachineFunction &) = delete;
  MachineFunction &operator=(const MachineFunction &) = delete;
  ~MachineFunction();

  /// Reset the instance as if it was just created.
  void reset() {
    clear();
    init();
  }

  /// Reset the currently registered delegate - otherwise assert.
  void resetDelegate(Delegate *delegate) {
    assert(TheDelegate == delegate &&
           "Only the current delegate can perform reset!");
    TheDelegate = nullptr;
  }

  /// Set the delegate. resetDelegate must be called before attempting
  /// to set.
  void setDelegate(Delegate *delegate) {
    assert(delegate && !TheDelegate &&
           "Attempted to set delegate to null, or to change it without "
           "first resetting it!");

    TheDelegate = delegate;
  }

  void setObserver(GISelChangeObserver *O) { Observer = O; }

  GISelChangeObserver *getObserver() const { return Observer; }

  MachineModuleInfo &getMMI() const { return MMI; }
  MCContext &getContext() const { return Ctx; }

  /// Returns the Section this function belongs to.
  MCSection *getSection() const { return Section; }

  /// Indicates the Section this function belongs to.
  void setSection(MCSection *S) { Section = S; }

  PseudoSourceValueManager &getPSVManager() const { return *PSVManager; }

  /// Return the DataLayout attached to the Module associated to this MF.
  const DataLayout &getDataLayout() const;

  /// Return the LLVM function that this machine code represents
  Function &getFunction() { return F; }

  /// Return the LLVM function that this machine code represents
  const Function &getFunction() const { return F; }

  /// getName - Return the name of the corresponding LLVM function.
  StringRef getName() const;

  /// getFunctionNumber - Return a unique ID for the current function.
  unsigned getFunctionNumber() const { return FunctionNumber; }

  /// Returns true if this function has basic block sections enabled.
  bool hasBBSections() const {
    return (BBSectionsType == BasicBlockSection::All ||
            BBSectionsType == BasicBlockSection::List ||
            BBSectionsType == BasicBlockSection::Preset);
  }

  /// Returns true if basic block labels are to be generated for this function.
  bool hasBBLabels() const {
    return BBSectionsType == BasicBlockSection::Labels;
  }

  void setBBSectionsType(BasicBlockSection V) { BBSectionsType = V; }

  /// Assign IsBeginSection IsEndSection fields for basic blocks in this
  /// function.
  void assignBeginEndSections();

  /// getTarget - Return the target machine this machine code is compiled with
  const LLVMTargetMachine &getTarget() const { return Target; }

  /// getSubtarget - Return the subtarget for which this machine code is being
  /// compiled.
  const TargetSubtargetInfo &getSubtarget() const { return *STI; }

  /// getSubtarget - This method returns a pointer to the specified type of
  /// TargetSubtargetInfo.  In debug builds, it verifies that the object being
  /// returned is of the correct type.
  template<typename STC> const STC &getSubtarget() const {
    return *static_cast<const STC *>(STI);
  }

  /// getRegInfo - Return information about the registers currently in use.
  MachineRegisterInfo &getRegInfo() { return *RegInfo; }
  const MachineRegisterInfo &getRegInfo() const { return *RegInfo; }

  /// getFrameInfo - Return the frame info object for the current function.
  /// This object contains information about objects allocated on the stack
  /// frame of the current function in an abstract way.
  MachineFrameInfo &getFrameInfo() { return *FrameInfo; }
  const MachineFrameInfo &getFrameInfo() const { return *FrameInfo; }

  /// getJumpTableInfo - Return the jump table info object for the current
  /// function.  This object contains information about jump tables in the
  /// current function.  If the current function has no jump tables, this will
  /// return null.
  const MachineJumpTableInfo *getJumpTableInfo() const { return JumpTableInfo; }
  MachineJumpTableInfo *getJumpTableInfo() { return JumpTableInfo; }

  /// getOrCreateJumpTableInfo - Get the JumpTableInfo for this function, if it
  /// does already exist, allocate one.
  MachineJumpTableInfo *getOrCreateJumpTableInfo(unsigned JTEntryKind);

  /// getConstantPool - Return the constant pool object for the current
  /// function.
  MachineConstantPool *getConstantPool() { return ConstantPool; }
  const MachineConstantPool *getConstantPool() const { return ConstantPool; }

  /// getWasmEHFuncInfo - Return information about how the current function uses
  /// Wasm exception handling. Returns null for functions that don't use wasm
  /// exception handling.
  const WasmEHFuncInfo *getWasmEHFuncInfo() const { return WasmEHInfo; }
  WasmEHFuncInfo *getWasmEHFuncInfo() { return WasmEHInfo; }

  /// getWinEHFuncInfo - Return information about how the current function uses
  /// Windows exception handling. Returns null for functions that don't use
  /// funclets for exception handling.
  const WinEHFuncInfo *getWinEHFuncInfo() const { return WinEHInfo; }
  WinEHFuncInfo *getWinEHFuncInfo() { return WinEHInfo; }

  /// getAlignment - Return the alignment of the function.
  Align getAlignment() const { return Alignment; }

  /// setAlignment - Set the alignment of the function.
  void setAlignment(Align A) { Alignment = A; }

  /// ensureAlignment - Make sure the function is at least A bytes aligned.
  void ensureAlignment(Align A) {
    if (Alignment < A)
      Alignment = A;
  }

  /// exposesReturnsTwice - Returns true if the function calls setjmp or
  /// any other similar functions with attribute "returns twice" without
  /// having the attribute itself.
  bool exposesReturnsTwice() const {
    return ExposesReturnsTwice;
  }

  /// setCallsSetJmp - Set a flag that indicates if there's a call to
  /// a "returns twice" function.
  void setExposesReturnsTwice(bool B) {
    ExposesReturnsTwice = B;
  }

  /// Returns true if the function contains any inline assembly.
  bool hasInlineAsm() const {
    return HasInlineAsm;
  }

  /// Set a flag that indicates that the function contains inline assembly.
  void setHasInlineAsm(bool B) {
    HasInlineAsm = B;
  }

  bool hasWinCFI() const {
    return HasWinCFI;
  }
  void setHasWinCFI(bool v) { HasWinCFI = v; }

  /// True if this function needs frame moves for debug or exceptions.
  bool needsFrameMoves() const;

  /// Get the function properties
  const MachineFunctionProperties &getProperties() const { return Properties; }
  MachineFunctionProperties &getProperties() { return Properties; }

  /// getInfo - Keep track of various per-function pieces of information for
  /// backends that would like to do so.
  ///
  template<typename Ty>
  Ty *getInfo() {
    return static_cast<Ty*>(MFInfo);
  }

  template<typename Ty>
  const Ty *getInfo() const {
    return static_cast<const Ty *>(MFInfo);
  }

  template <typename Ty> Ty *cloneInfo(const Ty &Old) {
    assert(!MFInfo);
    MFInfo = Ty::template create<Ty>(Allocator, Old);
    return static_cast<Ty *>(MFInfo);
  }

  /// Initialize the target specific MachineFunctionInfo
  void initTargetMachineFunctionInfo(const TargetSubtargetInfo &STI);

  MachineFunctionInfo *cloneInfoFrom(
      const MachineFunction &OrigMF,
      const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB) {
    assert(!MFInfo && "new function already has MachineFunctionInfo");
    if (!OrigMF.MFInfo)
      return nullptr;
    return OrigMF.MFInfo->clone(Allocator, *this, Src2DstMBB);
  }

  /// Returns the denormal handling type for the default rounding mode of the
  /// function.
  DenormalMode getDenormalMode(const fltSemantics &FPType) const;

  /// getBlockNumbered - MachineBasicBlocks are automatically numbered when they
  /// are inserted into the machine function.  The block number for a machine
  /// basic block can be found by using the MBB::getNumber method, this method
  /// provides the inverse mapping.
  MachineBasicBlock *getBlockNumbered(unsigned N) const {
    assert(N < MBBNumbering.size() && "Illegal block number");
    assert(MBBNumbering[N] && "Block was removed from the machine function!");
    return MBBNumbering[N];
  }

  /// Should we be emitting segmented stack stuff for the function
  bool shouldSplitStack() const;

  /// getNumBlockIDs - Return the number of MBB ID's allocated.
  unsigned getNumBlockIDs() const { return (unsigned)MBBNumbering.size(); }

  /// RenumberBlocks - This discards all of the MachineBasicBlock numbers and
  /// recomputes them.  This guarantees that the MBB numbers are sequential,
  /// dense, and match the ordering of the blocks within the function.  If a
  /// specific MachineBasicBlock is specified, only that block and those after
  /// it are renumbered.
  void RenumberBlocks(MachineBasicBlock *MBBFrom = nullptr);

  /// print - Print out the MachineFunction in a format suitable for debugging
  /// to the specified stream.
  void print(raw_ostream &OS, const SlotIndexes* = nullptr) const;

  /// viewCFG - This function is meant for use from the debugger.  You can just
  /// say 'call F->viewCFG()' and a ghostview window should pop up from the
  /// program, displaying the CFG of the current function with the code for each
  /// basic block inside.  This depends on there being a 'dot' and 'gv' program
  /// in your path.
  void viewCFG() const;

  /// viewCFGOnly - This function is meant for use from the debugger.  It works
  /// just like viewCFG, but it does not include the contents of basic blocks
  /// into the nodes, just the label.  If you are only interested in the CFG
  /// this can make the graph smaller.
  ///
  void viewCFGOnly() const;

  /// dump - Print the current MachineFunction to cerr, useful for debugger use.
  void dump() const;

  /// Run the current MachineFunction through the machine code verifier, useful
  /// for debugger use.
  /// \returns true if no problems were found.
  bool verify(Pass *p = nullptr, const char *Banner = nullptr,
              bool AbortOnError = true) const;

  /// Run the current MachineFunction through the machine code verifier, useful
  /// for debugger use.
  /// \returns true if no problems were found.
  bool verify(LiveIntervals *LiveInts, SlotIndexes *Indexes,
              const char *Banner = nullptr, bool AbortOnError = true) const;

  // Provide accessors for the MachineBasicBlock list...
  using iterator = BasicBlockListType::iterator;
  using const_iterator = BasicBlockListType::const_iterator;
  using const_reverse_iterator = BasicBlockListType::const_reverse_iterator;
  using reverse_iterator = BasicBlockListType::reverse_iterator;

  /// Support for MachineBasicBlock::getNextNode().
  static BasicBlockListType MachineFunction::*
  getSublistAccess(MachineBasicBlock *) {
    return &MachineFunction::BasicBlocks;
  }

  /// addLiveIn - Add the specified physical register as a live-in value and
  /// create a corresponding virtual register for it.
  Register addLiveIn(MCRegister PReg, const TargetRegisterClass *RC);

  //===--------------------------------------------------------------------===//
  // BasicBlock accessor functions.
  //
  iterator                 begin()       { return BasicBlocks.begin(); }
  const_iterator           begin() const { return BasicBlocks.begin(); }
  iterator                 end  ()       { return BasicBlocks.end();   }
  const_iterator           end  () const { return BasicBlocks.end();   }

  reverse_iterator        rbegin()       { return BasicBlocks.rbegin(); }
  const_reverse_iterator  rbegin() const { return BasicBlocks.rbegin(); }
  reverse_iterator        rend  ()       { return BasicBlocks.rend();   }
  const_reverse_iterator  rend  () const { return BasicBlocks.rend();   }

  unsigned                  size() const { return (unsigned)BasicBlocks.size();}
  bool                     empty() const { return BasicBlocks.empty(); }
  const MachineBasicBlock &front() const { return BasicBlocks.front(); }
        MachineBasicBlock &front()       { return BasicBlocks.front(); }
  const MachineBasicBlock & back() const { return BasicBlocks.back(); }
        MachineBasicBlock & back()       { return BasicBlocks.back(); }

  void push_back (MachineBasicBlock *MBB) { BasicBlocks.push_back (MBB); }
  void push_front(MachineBasicBlock *MBB) { BasicBlocks.push_front(MBB); }
  void insert(iterator MBBI, MachineBasicBlock *MBB) {
    BasicBlocks.insert(MBBI, MBB);
  }
  void splice(iterator InsertPt, iterator MBBI) {
    BasicBlocks.splice(InsertPt, BasicBlocks, MBBI);
  }
  void splice(iterator InsertPt, MachineBasicBlock *MBB) {
    BasicBlocks.splice(InsertPt, BasicBlocks, MBB);
  }
  void splice(iterator InsertPt, iterator MBBI, iterator MBBE) {
    BasicBlocks.splice(InsertPt, BasicBlocks, MBBI, MBBE);
  }

  void remove(iterator MBBI) { BasicBlocks.remove(MBBI); }
  void remove(MachineBasicBlock *MBBI) { BasicBlocks.remove(MBBI); }
  void erase(iterator MBBI) { BasicBlocks.erase(MBBI); }
  void erase(MachineBasicBlock *MBBI) { BasicBlocks.erase(MBBI); }

  template <typename Comp>
  void sort(Comp comp) {
    BasicBlocks.sort(comp);
  }

  /// Return the number of \p MachineInstrs in this \p MachineFunction.
  unsigned getInstructionCount() const {
    unsigned InstrCount = 0;
    for (const MachineBasicBlock &MBB : BasicBlocks)
      InstrCount += MBB.size();
    return InstrCount;
  }

  //===--------------------------------------------------------------------===//
  // Internal functions used to automatically number MachineBasicBlocks

  /// Adds the MBB to the internal numbering. Returns the unique number
  /// assigned to the MBB.
  unsigned addToMBBNumbering(MachineBasicBlock *MBB) {
    MBBNumbering.push_back(MBB);
    return (unsigned)MBBNumbering.size()-1;
  }

  /// removeFromMBBNumbering - Remove the specific machine basic block from our
  /// tracker, this is only really to be used by the MachineBasicBlock
  /// implementation.
  void removeFromMBBNumbering(unsigned N) {
    assert(N < MBBNumbering.size() && "Illegal basic block #");
    MBBNumbering[N] = nullptr;
  }

  /// CreateMachineInstr - Allocate a new MachineInstr. Use this instead
  /// of `new MachineInstr'.
  MachineInstr *CreateMachineInstr(const MCInstrDesc &MCID, DebugLoc DL,
                                   bool NoImplicit = false);

  /// Create a new MachineInstr which is a copy of \p Orig, identical in all
  /// ways except the instruction has no parent, prev, or next. Bundling flags
  /// are reset.
  ///
  /// Note: Clones a single instruction, not whole instruction bundles.
  /// Does not perform target specific adjustments; consider using
  /// TargetInstrInfo::duplicate() instead.
  MachineInstr *CloneMachineInstr(const MachineInstr *Orig);

  /// Clones instruction or the whole instruction bundle \p Orig and insert
  /// into \p MBB before \p InsertBefore.
  ///
  /// Note: Does not perform target specific adjustments; consider using
  /// TargetInstrInfo::duplicate() intead.
  MachineInstr &
  cloneMachineInstrBundle(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator InsertBefore,
                          const MachineInstr &Orig);

  /// DeleteMachineInstr - Delete the given MachineInstr.
  void deleteMachineInstr(MachineInstr *MI);

  /// CreateMachineBasicBlock - Allocate a new MachineBasicBlock. Use this
  /// instead of `new MachineBasicBlock'. Sets `MachineBasicBlock::BBID` if
  /// basic-block-sections is enabled for the function.
  MachineBasicBlock *
  CreateMachineBasicBlock(const BasicBlock *BB = nullptr,
                          std::optional<UniqueBBID> BBID = std::nullopt);

  /// DeleteMachineBasicBlock - Delete the given MachineBasicBlock.
  void deleteMachineBasicBlock(MachineBasicBlock *MBB);

  /// getMachineMemOperand - Allocate a new MachineMemOperand.
  /// MachineMemOperands are owned by the MachineFunction and need not be
  /// explicitly deallocated.
  MachineMemOperand *getMachineMemOperand(
      MachinePointerInfo PtrInfo, MachineMemOperand::Flags f, LLT MemTy,
      Align base_alignment, const AAMDNodes &AAInfo = AAMDNodes(),
      const MDNode *Ranges = nullptr, SyncScope::ID SSID = SyncScope::System,
      AtomicOrdering Ordering = AtomicOrdering::NotAtomic,
      AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic);
  MachineMemOperand *getMachineMemOperand(
      MachinePointerInfo PtrInfo, MachineMemOperand::Flags F, LocationSize Size,
      Align BaseAlignment, const AAMDNodes &AAInfo = AAMDNodes(),
      const MDNode *Ranges = nullptr, SyncScope::ID SSID = SyncScope::System,
      AtomicOrdering Ordering = AtomicOrdering::NotAtomic,
      AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic);
  MachineMemOperand *getMachineMemOperand(
      MachinePointerInfo PtrInfo, MachineMemOperand::Flags F, uint64_t Size,
      Align BaseAlignment, const AAMDNodes &AAInfo = AAMDNodes(),
      const MDNode *Ranges = nullptr, SyncScope::ID SSID = SyncScope::System,
      AtomicOrdering Ordering = AtomicOrdering::NotAtomic,
      AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic) {
    return getMachineMemOperand(PtrInfo, F, LocationSize::precise(Size),
                                BaseAlignment, AAInfo, Ranges, SSID, Ordering,
                                FailureOrdering);
  }

  /// getMachineMemOperand - Allocate a new MachineMemOperand by copying
  /// an existing one, adjusting by an offset and using the given size.
  /// MachineMemOperands are owned by the MachineFunction and need not be
  /// explicitly deallocated.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          int64_t Offset, LLT Ty);
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          int64_t Offset, LocationSize Size) {
    return getMachineMemOperand(
        MMO, Offset,
        !Size.hasValue() ? LLT()
        : Size.isScalable()
            ? LLT::scalable_vector(1, 8 * Size.getValue().getKnownMinValue())
            : LLT::scalar(8 * Size.getValue().getKnownMinValue()));
  }
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          int64_t Offset, uint64_t Size) {
    return getMachineMemOperand(MMO, Offset, LocationSize::precise(Size));
  }

  /// getMachineMemOperand - Allocate a new MachineMemOperand by copying
  /// an existing one, replacing only the MachinePointerInfo and size.
  /// MachineMemOperands are owned by the MachineFunction and need not be
  /// explicitly deallocated.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          const MachinePointerInfo &PtrInfo,
                                          LocationSize Size);
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          const MachinePointerInfo &PtrInfo,
                                          LLT Ty);
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          const MachinePointerInfo &PtrInfo,
                                          uint64_t Size) {
    return getMachineMemOperand(MMO, PtrInfo, LocationSize::precise(Size));
  }

  /// Allocate a new MachineMemOperand by copying an existing one,
  /// replacing only AliasAnalysis information. MachineMemOperands are owned
  /// by the MachineFunction and need not be explicitly deallocated.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          const AAMDNodes &AAInfo);

  /// Allocate a new MachineMemOperand by copying an existing one,
  /// replacing the flags. MachineMemOperands are owned
  /// by the MachineFunction and need not be explicitly deallocated.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          MachineMemOperand::Flags Flags);

  using OperandCapacity = ArrayRecycler<MachineOperand>::Capacity;

  /// Allocate an array of MachineOperands. This is only intended for use by
  /// internal MachineInstr functions.
  MachineOperand *allocateOperandArray(OperandCapacity Cap) {
    return OperandRecycler.allocate(Cap, Allocator);
  }

  /// Dellocate an array of MachineOperands and recycle the memory. This is
  /// only intended for use by internal MachineInstr functions.
  /// Cap must be the same capacity that was used to allocate the array.
  void deallocateOperandArray(OperandCapacity Cap, MachineOperand *Array) {
    OperandRecycler.deallocate(Cap, Array);
  }

  /// Allocate and initialize a register mask with @p NumRegister bits.
  uint32_t *allocateRegMask();

  ArrayRef<int> allocateShuffleMask(ArrayRef<int> Mask);

  /// Allocate and construct an extra info structure for a `MachineInstr`.
  ///
  /// This is allocated on the function's allocator and so lives the life of
  /// the function.
  MachineInstr::ExtraInfo *createMIExtraInfo(
      ArrayRef<MachineMemOperand *> MMOs, MCSymbol *PreInstrSymbol = nullptr,
      MCSymbol *PostInstrSymbol = nullptr, MDNode *HeapAllocMarker = nullptr,
      MDNode *PCSections = nullptr, uint32_t CFIType = 0,
      MDNode *MMRAs = nullptr);

  /// Allocate a string and populate it with the given external symbol name.
  const char *createExternalSymbolName(StringRef Name);

  //===--------------------------------------------------------------------===//
  // Label Manipulation.

  /// getJTISymbol - Return the MCSymbol for the specified non-empty jump table.
  /// If isLinkerPrivate is specified, an 'l' label is returned, otherwise a
  /// normal 'L' label is returned.
  MCSymbol *getJTISymbol(unsigned JTI, MCContext &Ctx,
                         bool isLinkerPrivate = false) const;

  /// getPICBaseSymbol - Return a function-local symbol to represent the PIC
  /// base.
  MCSymbol *getPICBaseSymbol() const;

  /// Returns a reference to a list of cfi instructions in the function's
  /// prologue.  Used to construct frame maps for debug and exception handling
  /// comsumers.
  const std::vector<MCCFIInstruction> &getFrameInstructions() const {
    return FrameInstructions;
  }

  [[nodiscard]] unsigned addFrameInst(const MCCFIInstruction &Inst);

  /// Returns a reference to a list of symbols immediately following calls to
  /// _setjmp in the function. Used to construct the longjmp target table used
  /// by Windows Control Flow Guard.
  const std::vector<MCSymbol *> &getLongjmpTargets() const {
    return LongjmpTargets;
  }

  /// Add the specified symbol to the list of valid longjmp targets for Windows
  /// Control Flow Guard.
  void addLongjmpTarget(MCSymbol *Target) { LongjmpTargets.push_back(Target); }

  /// Returns a reference to a list of symbols that we have catchrets.
  /// Used to construct the catchret target table used by Windows EHCont Guard.
  const std::vector<MCSymbol *> &getCatchretTargets() const {
    return CatchretTargets;
  }

  /// Add the specified symbol to the list of valid catchret targets for Windows
  /// EHCont Guard.
  void addCatchretTarget(MCSymbol *Target) {
    CatchretTargets.push_back(Target);
  }

  /// \name Exception Handling
  /// \{

  bool callsEHReturn() const { return CallsEHReturn; }
  void setCallsEHReturn(bool b) { CallsEHReturn = b; }

  bool callsUnwindInit() const { return CallsUnwindInit; }
  void setCallsUnwindInit(bool b) { CallsUnwindInit = b; }

  bool hasEHCatchret() const { return HasEHCatchret; }
  void setHasEHCatchret(bool V) { HasEHCatchret = V; }

  bool hasEHScopes() const { return HasEHScopes; }
  void setHasEHScopes(bool V) { HasEHScopes = V; }

  bool hasEHFunclets() const { return HasEHFunclets; }
  void setHasEHFunclets(bool V) { HasEHFunclets = V; }

  bool isOutlined() const { return IsOutlined; }
  void setIsOutlined(bool V) { IsOutlined = V; }

  /// Find or create an LandingPadInfo for the specified MachineBasicBlock.
  LandingPadInfo &getOrCreateLandingPadInfo(MachineBasicBlock *LandingPad);

  /// Return a reference to the landing pad info for the current function.
  const std::vector<LandingPadInfo> &getLandingPads() const {
    return LandingPads;
  }

  /// Provide the begin and end labels of an invoke style call and associate it
  /// with a try landing pad block.
  void addInvoke(MachineBasicBlock *LandingPad,
                 MCSymbol *BeginLabel, MCSymbol *EndLabel);

  /// Add a new panding pad, and extract the exception handling information from
  /// the landingpad instruction. Returns the label ID for the landing pad
  /// entry.
  MCSymbol *addLandingPad(MachineBasicBlock *LandingPad);

  /// Return the type id for the specified typeinfo.  This is function wide.
  unsigned getTypeIDFor(const GlobalValue *TI);

  /// Return the id of the filter encoded by TyIds.  This is function wide.
  int getFilterIDFor(ArrayRef<unsigned> TyIds);

  /// Map the landing pad's EH symbol to the call site indexes.
  void setCallSiteLandingPad(MCSymbol *Sym, ArrayRef<unsigned> Sites);

  /// Return if there is any wasm exception handling.
  bool hasAnyWasmLandingPadIndex() const {
    return !WasmLPadToIndexMap.empty();
  }

  /// Map the landing pad to its index. Used for Wasm exception handling.
  void setWasmLandingPadIndex(const MachineBasicBlock *LPad, unsigned Index) {
    WasmLPadToIndexMap[LPad] = Index;
  }

  /// Returns true if the landing pad has an associate index in wasm EH.
  bool hasWasmLandingPadIndex(const MachineBasicBlock *LPad) const {
    return WasmLPadToIndexMap.count(LPad);
  }

  /// Get the index in wasm EH for a given landing pad.
  unsigned getWasmLandingPadIndex(const MachineBasicBlock *LPad) const {
    assert(hasWasmLandingPadIndex(LPad));
    return WasmLPadToIndexMap.lookup(LPad);
  }

  bool hasAnyCallSiteLandingPad() const {
    return !LPadToCallSiteMap.empty();
  }

  /// Get the call site indexes for a landing pad EH symbol.
  SmallVectorImpl<unsigned> &getCallSiteLandingPad(MCSymbol *Sym) {
    assert(hasCallSiteLandingPad(Sym) &&
           "missing call site number for landing pad!");
    return LPadToCallSiteMap[Sym];
  }

  /// Return true if the landing pad Eh symbol has an associated call site.
  bool hasCallSiteLandingPad(MCSymbol *Sym) {
    return !LPadToCallSiteMap[Sym].empty();
  }

  bool hasAnyCallSiteLabel() const {
    return !CallSiteMap.empty();
  }

  /// Map the begin label for a call site.
  void setCallSiteBeginLabel(MCSymbol *BeginLabel, unsigned Site) {
    CallSiteMap[BeginLabel] = Site;
  }

  /// Get the call site number for a begin label.
  unsigned getCallSiteBeginLabel(MCSymbol *BeginLabel) const {
    assert(hasCallSiteBeginLabel(BeginLabel) &&
           "Missing call site number for EH_LABEL!");
    return CallSiteMap.lookup(BeginLabel);
  }

  /// Return true if the begin label has a call site number associated with it.
  bool hasCallSiteBeginLabel(MCSymbol *BeginLabel) const {
    return CallSiteMap.count(BeginLabel);
  }

  /// Record annotations associated with a particular label.
  void addCodeViewAnnotation(MCSymbol *Label, MDNode *MD) {
    CodeViewAnnotations.push_back({Label, MD});
  }

  ArrayRef<std::pair<MCSymbol *, MDNode *>> getCodeViewAnnotations() const {
    return CodeViewAnnotations;
  }

  /// Return a reference to the C++ typeinfo for the current function.
  const std::vector<const GlobalValue *> &getTypeInfos() const {
    return TypeInfos;
  }

  /// Return a reference to the typeids encoding filters used in the current
  /// function.
  const std::vector<unsigned> &getFilterIds() const {
    return FilterIds;
  }

  /// \}

  /// Collect information used to emit debugging information of a variable in a
  /// stack slot.
  void setVariableDbgInfo(const DILocalVariable *Var, const DIExpression *Expr,
                          int Slot, const DILocation *Loc) {
    VariableDbgInfos.emplace_back(Var, Expr, Slot, Loc);
  }

  /// Collect information used to emit debugging information of a variable in
  /// the entry value of a register.
  void setVariableDbgInfo(const DILocalVariable *Var, const DIExpression *Expr,
                          MCRegister Reg, const DILocation *Loc) {
    VariableDbgInfos.emplace_back(Var, Expr, Reg, Loc);
  }

  VariableDbgInfoMapTy &getVariableDbgInfo() { return VariableDbgInfos; }
  const VariableDbgInfoMapTy &getVariableDbgInfo() const {
    return VariableDbgInfos;
  }

  /// Returns the collection of variables for which we have debug info and that
  /// have been assigned a stack slot.
  auto getInStackSlotVariableDbgInfo() {
    return make_filter_range(getVariableDbgInfo(), [](auto &VarInfo) {
      return VarInfo.inStackSlot();
    });
  }

  /// Returns the collection of variables for which we have debug info and that
  /// have been assigned a stack slot.
  auto getInStackSlotVariableDbgInfo() const {
    return make_filter_range(getVariableDbgInfo(), [](const auto &VarInfo) {
      return VarInfo.inStackSlot();
    });
  }

  /// Returns the collection of variables for which we have debug info and that
  /// have been assigned an entry value register.
  auto getEntryValueVariableDbgInfo() const {
    return make_filter_range(getVariableDbgInfo(), [](const auto &VarInfo) {
      return VarInfo.inEntryValueRegister();
    });
  }

  /// Start tracking the arguments passed to the call \p CallI.
  void addCallSiteInfo(const MachineInstr *CallI, CallSiteInfo &&CallInfo) {
    assert(CallI->isCandidateForCallSiteEntry());
    bool Inserted =
        CallSitesInfo.try_emplace(CallI, std::move(CallInfo)).second;
    (void)Inserted;
    assert(Inserted && "Call site info not unique");
  }

  const CallSiteInfoMap &getCallSitesInfo() const {
    return CallSitesInfo;
  }

  /// Following functions update call site info. They should be called before
  /// removing, replacing or copying call instruction.

  /// Erase the call site info for \p MI. It is used to remove a call
  /// instruction from the instruction stream.
  void eraseCallSiteInfo(const MachineInstr *MI);
  /// Copy the call site info from \p Old to \ New. Its usage is when we are
  /// making a copy of the instruction that will be inserted at different point
  /// of the instruction stream.
  void copyCallSiteInfo(const MachineInstr *Old,
                        const MachineInstr *New);

  /// Move the call site info from \p Old to \New call site info. This function
  /// is used when we are replacing one call instruction with another one to
  /// the same callee.
  void moveCallSiteInfo(const MachineInstr *Old,
                        const MachineInstr *New);

  unsigned getNewDebugInstrNum() {
    return ++DebugInstrNumberingCount;
  }
};

//===--------------------------------------------------------------------===//
// GraphTraits specializations for function basic block graphs (CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a
// machine function as a graph of machine basic blocks... these are
// the same as the machine basic block iterators, except that the root
// node is implicitly the first node of the function.
//
template <> struct GraphTraits<MachineFunction*> :
  public GraphTraits<MachineBasicBlock*> {
  static NodeRef getEntryNode(MachineFunction *F) { return &F->front(); }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<MachineFunction::iterator>;

  static nodes_iterator nodes_begin(MachineFunction *F) {
    return nodes_iterator(F->begin());
  }

  static nodes_iterator nodes_end(MachineFunction *F) {
    return nodes_iterator(F->end());
  }

  static unsigned       size       (MachineFunction *F) { return F->size(); }
};
template <> struct GraphTraits<const MachineFunction*> :
  public GraphTraits<const MachineBasicBlock*> {
  static NodeRef getEntryNode(const MachineFunction *F) { return &F->front(); }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<MachineFunction::const_iterator>;

  static nodes_iterator nodes_begin(const MachineFunction *F) {
    return nodes_iterator(F->begin());
  }

  static nodes_iterator nodes_end  (const MachineFunction *F) {
    return nodes_iterator(F->end());
  }

  static unsigned       size       (const MachineFunction *F)  {
    return F->size();
  }
};

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... and to walk it in inverse order.  Inverse order for
// a function is considered to be when traversing the predecessor edges of a BB
// instead of the successor edges.
//
template <> struct GraphTraits<Inverse<MachineFunction*>> :
  public GraphTraits<Inverse<MachineBasicBlock*>> {
  static NodeRef getEntryNode(Inverse<MachineFunction *> G) {
    return &G.Graph->front();
  }
};
template <> struct GraphTraits<Inverse<const MachineFunction*>> :
  public GraphTraits<Inverse<const MachineBasicBlock*>> {
  static NodeRef getEntryNode(Inverse<const MachineFunction *> G) {
    return &G.Graph->front();
  }
};

void verifyMachineFunction(const std::string &Banner,
                           const MachineFunction &MF);

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEFUNCTION_H
