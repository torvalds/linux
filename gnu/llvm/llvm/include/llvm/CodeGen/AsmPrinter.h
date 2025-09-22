//===- llvm/CodeGen/AsmPrinter.h - AsmPrinter Framework ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a class to be used as the base class for target specific
// asm writers.  This class primarily handles common functionality used by
// all asm writers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ASMPRINTER_H
#define LLVM_CODEGEN_ASMPRINTER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class AddrLabelMap;
class AsmPrinterHandler;
class BasicBlock;
class BlockAddress;
class Constant;
class ConstantArray;
class ConstantPtrAuth;
class DataLayout;
class DebugHandlerBase;
class DIE;
class DIEAbbrev;
class DwarfDebug;
class GCMetadataPrinter;
class GCStrategy;
class GlobalAlias;
class GlobalObject;
class GlobalValue;
class GlobalVariable;
class MachineBasicBlock;
class MachineConstantPoolValue;
class MachineDominatorTree;
class MachineFunction;
class MachineInstr;
class MachineJumpTableInfo;
class MachineLoopInfo;
class MachineModuleInfo;
class MachineOptimizationRemarkEmitter;
class MCAsmInfo;
class MCCFIInstruction;
class MCContext;
class MCExpr;
class MCInst;
class MCSection;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbol;
class MCTargetOptions;
class MDNode;
class Module;
class PseudoProbeHandler;
class raw_ostream;
class StringRef;
class TargetLoweringObjectFile;
class TargetMachine;
class Twine;

namespace remarks {
class RemarkStreamer;
}

/// This class is intended to be used as a driving class for all asm writers.
class AsmPrinter : public MachineFunctionPass {
public:
  /// Target machine description.
  TargetMachine &TM;

  /// Target Asm Printer information.
  const MCAsmInfo *MAI = nullptr;

  /// This is the context for the output file that we are streaming. This owns
  /// all of the global MC-related objects for the generated translation unit.
  MCContext &OutContext;

  /// This is the MCStreamer object for the file we are generating. This
  /// contains the transient state for the current translation unit that we are
  /// generating (such as the current section etc).
  std::unique_ptr<MCStreamer> OutStreamer;

  /// The current machine function.
  MachineFunction *MF = nullptr;

  /// This is a pointer to the current MachineModuleInfo.
  MachineModuleInfo *MMI = nullptr;

  /// This is a pointer to the current MachineDominatorTree.
  MachineDominatorTree *MDT = nullptr;

  /// This is a pointer to the current MachineLoopInfo.
  MachineLoopInfo *MLI = nullptr;

  /// Optimization remark emitter.
  MachineOptimizationRemarkEmitter *ORE = nullptr;

  /// The symbol for the entry in __patchable_function_entires.
  MCSymbol *CurrentPatchableFunctionEntrySym = nullptr;

  /// The symbol for the current function. This is recalculated at the beginning
  /// of each call to runOnMachineFunction().
  MCSymbol *CurrentFnSym = nullptr;

  /// The symbol for the current function descriptor on AIX. This is created
  /// at the beginning of each call to SetupMachineFunction().
  MCSymbol *CurrentFnDescSym = nullptr;

  /// The symbol used to represent the start of the current function for the
  /// purpose of calculating its size (e.g. using the .size directive). By
  /// default, this is equal to CurrentFnSym.
  MCSymbol *CurrentFnSymForSize = nullptr;

  /// Map a basic block section ID to the begin and end symbols of that section
  ///  which determine the section's range.
  struct MBBSectionRange {
    MCSymbol *BeginLabel, *EndLabel;
  };

  MapVector<MBBSectionID, MBBSectionRange> MBBSectionRanges;

  /// Map global GOT equivalent MCSymbols to GlobalVariables and keep track of
  /// its number of uses by other globals.
  using GOTEquivUsePair = std::pair<const GlobalVariable *, unsigned>;
  MapVector<const MCSymbol *, GOTEquivUsePair> GlobalGOTEquivs;

  // Flags representing which CFI section is required for a function/module.
  enum class CFISection : unsigned {
    None = 0, ///< Do not emit either .eh_frame or .debug_frame
    EH = 1,   ///< Emit .eh_frame
    Debug = 2 ///< Emit .debug_frame
  };

private:
  MCSymbol *CurrentFnEnd = nullptr;

  /// Map a basic block section ID to the exception symbol associated with that
  /// section. Map entries are assigned and looked up via
  /// AsmPrinter::getMBBExceptionSym.
  DenseMap<MBBSectionID, MCSymbol *> MBBSectionExceptionSyms;

  // The symbol used to represent the start of the current BB section of the
  // function. This is used to calculate the size of the BB section.
  MCSymbol *CurrentSectionBeginSym = nullptr;

  /// This map keeps track of which symbol is being used for the specified basic
  /// block's address of label.
  std::unique_ptr<AddrLabelMap> AddrLabelSymbols;

  /// The garbage collection metadata printer table.
  DenseMap<GCStrategy *, std::unique_ptr<GCMetadataPrinter>> GCMetadataPrinters;

  /// Emit comments in assembly output if this is true.
  bool VerboseAsm;

  /// Output stream for the stack usage file (i.e., .su file).
  std::unique_ptr<raw_fd_ostream> StackUsageStream;

  /// List of symbols to be inserted into PC sections.
  DenseMap<const MDNode *, SmallVector<const MCSymbol *>> PCSectionsSymbols;

  static char ID;

protected:
  MCSymbol *CurrentFnBegin = nullptr;

  /// For dso_local functions, the current $local alias for the function.
  MCSymbol *CurrentFnBeginLocal = nullptr;

  /// A vector of all debug/EH info emitters we should use. This vector
  /// maintains ownership of the emitters.
  SmallVector<std::unique_ptr<AsmPrinterHandler>, 2> Handlers;
  size_t NumUserHandlers = 0;

  /// Debuginfo handler. Protected so that targets can add their own.
  SmallVector<std::unique_ptr<DebugHandlerBase>, 1> DebugHandlers;
  size_t NumUserDebugHandlers = 0;

  StackMaps SM;

private:
  /// If generated on the fly this own the instance.
  std::unique_ptr<MachineDominatorTree> OwnedMDT;

  /// If generated on the fly this own the instance.
  std::unique_ptr<MachineLoopInfo> OwnedMLI;

  /// If the target supports dwarf debug info, this pointer is non-null.
  DwarfDebug *DD = nullptr;

  /// A handler that supports pseudo probe emission with embedded inline
  /// context.
  std::unique_ptr<PseudoProbeHandler> PP;

  /// CFISection type the module needs i.e. either .eh_frame or .debug_frame.
  CFISection ModuleCFISection = CFISection::None;

  /// True if the module contains split-stack functions. This is used to
  /// emit .note.GNU-split-stack section as required by the linker for
  /// special handling split-stack function calling no-split-stack function.
  bool HasSplitStack = false;

  /// True if the module contains no-split-stack functions. This is used to emit
  /// .note.GNU-no-split-stack section when it also contains functions without a
  /// split stack prologue.
  bool HasNoSplitStack = false;

protected:
  explicit AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer);

public:
  ~AsmPrinter() override;

  DwarfDebug *getDwarfDebug() { return DD; }
  DwarfDebug *getDwarfDebug() const { return DD; }

  uint16_t getDwarfVersion() const;
  void setDwarfVersion(uint16_t Version);

  bool isDwarf64() const;

  /// Returns 4 for DWARF32 and 8 for DWARF64.
  unsigned int getDwarfOffsetByteSize() const;

  /// Returns 4 for DWARF32 and 12 for DWARF64.
  unsigned int getUnitLengthFieldByteSize() const;

  /// Returns information about the byte size of DW_FORM values.
  dwarf::FormParams getDwarfFormParams() const;

  bool isPositionIndependent() const;

  /// Return true if assembly output should contain comments.
  bool isVerbose() const { return VerboseAsm; }

  /// Return a unique ID for the current function.
  unsigned getFunctionNumber() const;

  /// Return symbol for the function pseudo stack if the stack frame is not a
  /// register based.
  virtual const MCSymbol *getFunctionFrameSymbol() const { return nullptr; }

  MCSymbol *getFunctionBegin() const { return CurrentFnBegin; }
  MCSymbol *getFunctionEnd() const { return CurrentFnEnd; }

  // Return the exception symbol associated with the MBB section containing a
  // given basic block.
  MCSymbol *getMBBExceptionSym(const MachineBasicBlock &MBB);

  /// Return the symbol to be used for the specified basic block when its
  /// address is taken.  This cannot be its normal LBB label because the block
  /// may be accessed outside its containing function.
  MCSymbol *getAddrLabelSymbol(const BasicBlock *BB) {
    return getAddrLabelSymbolToEmit(BB).front();
  }

  /// Return the symbol to be used for the specified basic block when its
  /// address is taken.  If other blocks were RAUW'd to this one, we may have
  /// to emit them as well, return the whole set.
  ArrayRef<MCSymbol *> getAddrLabelSymbolToEmit(const BasicBlock *BB);

  /// If the specified function has had any references to address-taken blocks
  /// generated, but the block got deleted, return the symbol now so we can
  /// emit it.  This prevents emitting a reference to a symbol that has no
  /// definition.
  void takeDeletedSymbolsForFunction(const Function *F,
                                     std::vector<MCSymbol *> &Result);

  /// Return information about object file lowering.
  const TargetLoweringObjectFile &getObjFileLowering() const;

  /// Return information about data layout.
  const DataLayout &getDataLayout() const;

  /// Return the pointer size from the TargetMachine
  unsigned getPointerSize() const;

  /// Return information about subtarget.
  const MCSubtargetInfo &getSubtargetInfo() const;

  void EmitToStreamer(MCStreamer &S, const MCInst &Inst);

  /// Emits inital debug location directive.
  void emitInitialRawDwarfLocDirective(const MachineFunction &MF);

  /// Return the current section we are emitting to.
  const MCSection *getCurrentSection() const;

  void getNameWithPrefix(SmallVectorImpl<char> &Name,
                         const GlobalValue *GV) const;

  MCSymbol *getSymbol(const GlobalValue *GV) const;

  /// Similar to getSymbol() but preferred for references. On ELF, this uses a
  /// local symbol if a reference to GV is guaranteed to be resolved to the
  /// definition in the same module.
  MCSymbol *getSymbolPreferLocal(const GlobalValue &GV) const;

  bool doesDwarfUseRelocationsAcrossSections() const {
    return DwarfUsesRelocationsAcrossSections;
  }

  void setDwarfUsesRelocationsAcrossSections(bool Enable) {
    DwarfUsesRelocationsAcrossSections = Enable;
  }

  //===------------------------------------------------------------------===//
  // XRay instrumentation implementation.
  //===------------------------------------------------------------------===//
public:
  // This describes the kind of sled we're storing in the XRay table.
  enum class SledKind : uint8_t {
    FUNCTION_ENTER = 0,
    FUNCTION_EXIT = 1,
    TAIL_CALL = 2,
    LOG_ARGS_ENTER = 3,
    CUSTOM_EVENT = 4,
    TYPED_EVENT = 5,
  };

  // The table will contain these structs that point to the sled, the function
  // containing the sled, and what kind of sled (and whether they should always
  // be instrumented). We also use a version identifier that the runtime can use
  // to decide what to do with the sled, depending on the version of the sled.
  struct XRayFunctionEntry {
    const MCSymbol *Sled;
    const MCSymbol *Function;
    SledKind Kind;
    bool AlwaysInstrument;
    const class Function *Fn;
    uint8_t Version;

    void emit(int, MCStreamer *) const;
  };

  // All the sleds to be emitted.
  SmallVector<XRayFunctionEntry, 4> Sleds;

  // Helper function to record a given XRay sled.
  void recordSled(MCSymbol *Sled, const MachineInstr &MI, SledKind Kind,
                  uint8_t Version = 0);

  /// Emit a table with all XRay instrumentation points.
  void emitXRayTable();

  void emitPatchableFunctionEntries();

  //===------------------------------------------------------------------===//
  // MachineFunctionPass Implementation.
  //===------------------------------------------------------------------===//

  /// Record analysis usage.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Set up the AsmPrinter when we are working on a new module. If your pass
  /// overrides this, it must make sure to explicitly call this implementation.
  bool doInitialization(Module &M) override;

  /// Shut down the asmprinter. If you override this in your pass, you must make
  /// sure to call it explicitly.
  bool doFinalization(Module &M) override;

  /// Emit the specified function out to the OutStreamer.
  bool runOnMachineFunction(MachineFunction &MF) override {
    SetupMachineFunction(MF);
    emitFunctionBody();
    return false;
  }

  //===------------------------------------------------------------------===//
  // Coarse grained IR lowering routines.
  //===------------------------------------------------------------------===//

  /// This should be called when a new MachineFunction is being processed from
  /// runOnMachineFunction.
  virtual void SetupMachineFunction(MachineFunction &MF);

  /// This method emits the body and trailer for a function.
  void emitFunctionBody();

  void emitCFIInstruction(const MachineInstr &MI);

  void emitFrameAlloc(const MachineInstr &MI);

  void emitStackSizeSection(const MachineFunction &MF);

  void emitStackUsage(const MachineFunction &MF);

  void emitBBAddrMapSection(const MachineFunction &MF);

  void emitKCFITrapEntry(const MachineFunction &MF, const MCSymbol *Symbol);
  virtual void emitKCFITypeId(const MachineFunction &MF);

  void emitPseudoProbe(const MachineInstr &MI);

  void emitRemarksSection(remarks::RemarkStreamer &RS);

  /// Emits a label as reference for PC sections.
  void emitPCSectionsLabel(const MachineFunction &MF, const MDNode &MD);

  /// Emits the PC sections collected from instructions.
  void emitPCSections(const MachineFunction &MF);

  /// Get the CFISection type for a function.
  CFISection getFunctionCFISectionType(const Function &F) const;

  /// Get the CFISection type for a function.
  CFISection getFunctionCFISectionType(const MachineFunction &MF) const;

  /// Get the CFISection type for the module.
  CFISection getModuleCFISectionType() const { return ModuleCFISection; }

  bool needsSEHMoves();

  /// Since emitting CFI unwind information is entangled with supporting the
  /// exceptions, this returns true for platforms which use CFI unwind
  /// information for other purposes (debugging, sanitizers, ...) when
  /// `MCAsmInfo::ExceptionsType == ExceptionHandling::None`.
  bool usesCFIWithoutEH() const;

  /// Print to the current output stream assembly representations of the
  /// constants in the constant pool MCP. This is used to print out constants
  /// which have been "spilled to memory" by the code generator.
  virtual void emitConstantPool();

  /// Print assembly representations of the jump tables used by the current
  /// function to the current output stream.
  virtual void emitJumpTableInfo();

  /// Emit the specified global variable to the .s file.
  virtual void emitGlobalVariable(const GlobalVariable *GV);

  /// Check to see if the specified global is a special global used by LLVM. If
  /// so, emit it and return true, otherwise do nothing and return false.
  bool emitSpecialLLVMGlobal(const GlobalVariable *GV);

  /// `llvm.global_ctors` and `llvm.global_dtors` are arrays of Structor
  /// structs.
  ///
  /// Priority - init priority
  /// Func - global initialization or global clean-up function
  /// ComdatKey - associated data
  struct Structor {
    int Priority = 0;
    Constant *Func = nullptr;
    GlobalValue *ComdatKey = nullptr;

    Structor() = default;
  };

  /// This method gathers an array of Structors and then sorts them out by
  /// Priority.
  /// @param List The initializer of `llvm.global_ctors` or `llvm.global_dtors`
  /// array.
  /// @param[out] Structors Sorted Structor structs by Priority.
  void preprocessXXStructorList(const DataLayout &DL, const Constant *List,
                                SmallVector<Structor, 8> &Structors);

  /// This method emits `llvm.global_ctors` or `llvm.global_dtors` list.
  virtual void emitXXStructorList(const DataLayout &DL, const Constant *List,
                                  bool IsCtor);

  /// Emit an alignment directive to the specified power of two boundary. If a
  /// global value is specified, and if that global has an explicit alignment
  /// requested, it will override the alignment request if required for
  /// correctness.
  void emitAlignment(Align Alignment, const GlobalObject *GV = nullptr,
                     unsigned MaxBytesToEmit = 0) const;

  /// Emit an alignment directive to the specified power of two boundary,
  /// like emitAlignment, but call emitTrapToAlignment to fill with
  /// trap instructions instead of NOPs.
  void emitTrapAlignment(Align Alignment, const GlobalObject *GO = nullptr) const;

  /// Lower the specified LLVM Constant to an MCExpr.
  virtual const MCExpr *lowerConstant(const Constant *CV);

  /// Print a general LLVM constant to the .s file.
  /// On AIX, when an alias refers to a sub-element of a global variable, the
  /// label of that alias needs to be emitted before the corresponding element.
  using AliasMapTy = DenseMap<uint64_t, SmallVector<const GlobalAlias *, 1>>;
  void emitGlobalConstant(const DataLayout &DL, const Constant *CV,
                          AliasMapTy *AliasList = nullptr);

  /// Unnamed constant global variables solely contaning a pointer to
  /// another globals variable act like a global variable "proxy", or GOT
  /// equivalents, i.e., it's only used to hold the address of the latter. One
  /// optimization is to replace accesses to these proxies by using the GOT
  /// entry for the final global instead. Hence, we select GOT equivalent
  /// candidates among all the module global variables, avoid emitting them
  /// unnecessarily and finally replace references to them by pc relative
  /// accesses to GOT entries.
  void computeGlobalGOTEquivs(Module &M);

  /// Constant expressions using GOT equivalent globals may not be
  /// eligible for PC relative GOT entry conversion, in such cases we need to
  /// emit the proxies we previously omitted in EmitGlobalVariable.
  void emitGlobalGOTEquivs();

  /// Emit the stack maps.
  void emitStackMaps();

  //===------------------------------------------------------------------===//
  // Overridable Hooks
  //===------------------------------------------------------------------===//

  void addAsmPrinterHandler(std::unique_ptr<AsmPrinterHandler> Handler);

  void addDebugHandler(std::unique_ptr<DebugHandlerBase> Handler);

  // Targets can, or in the case of EmitInstruction, must implement these to
  // customize output.

  /// This virtual method can be overridden by targets that want to emit
  /// something at the start of their file.
  virtual void emitStartOfAsmFile(Module &) {}

  /// This virtual method can be overridden by targets that want to emit
  /// something at the end of their file.
  virtual void emitEndOfAsmFile(Module &) {}

  /// Targets can override this to emit stuff before the first basic block in
  /// the function.
  virtual void emitFunctionBodyStart() {}

  /// Targets can override this to emit stuff after the last basic block in the
  /// function.
  virtual void emitFunctionBodyEnd() {}

  /// Targets can override this to emit stuff at the start of a basic block.
  /// By default, this method prints the label for the specified
  /// MachineBasicBlock, an alignment (if present) and a comment describing it
  /// if appropriate.
  virtual void emitBasicBlockStart(const MachineBasicBlock &MBB);

  /// Targets can override this to emit stuff at the end of a basic block.
  virtual void emitBasicBlockEnd(const MachineBasicBlock &MBB);

  /// Targets should implement this to emit instructions.
  virtual void emitInstruction(const MachineInstr *) {
    llvm_unreachable("EmitInstruction not implemented");
  }

  /// Emit an alignment directive to the specified power
  /// of two boundary, but use Trap instructions for alignment
  /// sections that should never be executed.
  virtual void emitTrapToAlignment(Align Alignment) const;

  /// Return the symbol for the specified constant pool entry.
  virtual MCSymbol *GetCPISymbol(unsigned CPID) const;

  virtual void emitFunctionEntryLabel();

  virtual void emitFunctionDescriptor() {
    llvm_unreachable("Function descriptor is target-specific.");
  }

  virtual void emitMachineConstantPoolValue(MachineConstantPoolValue *MCPV);

  /// Targets can override this to change how global constants that are part of
  /// a C++ static/global constructor list are emitted.
  virtual void emitXXStructor(const DataLayout &DL, const Constant *CV) {
    emitGlobalConstant(DL, CV);
  }

  virtual const MCExpr *lowerConstantPtrAuth(const ConstantPtrAuth &CPA) {
    report_fatal_error("ptrauth constant lowering not implemented");
  }

  /// Lower the specified BlockAddress to an MCExpr.
  virtual const MCExpr *lowerBlockAddressConstant(const BlockAddress &BA);

  /// Return true if the basic block has exactly one predecessor and the control
  /// transfer mechanism between the predecessor and this block is a
  /// fall-through.
  virtual bool
  isBlockOnlyReachableByFallthrough(const MachineBasicBlock *MBB) const;

  /// Targets can override this to customize the output of IMPLICIT_DEF
  /// instructions in verbose mode.
  virtual void emitImplicitDef(const MachineInstr *MI) const;

  /// getSubtargetInfo() cannot be used where this is needed because we don't
  /// have a MachineFunction when we're lowering a GlobalIFunc, and
  /// getSubtargetInfo requires one. Override the implementation in targets
  /// that support the Mach-O IFunc lowering.
  virtual const MCSubtargetInfo *getIFuncMCSubtargetInfo() const {
    return nullptr;
  }

  virtual void emitMachOIFuncStubBody(Module &M, const GlobalIFunc &GI,
                                      MCSymbol *LazyPointer) {
    llvm_unreachable(
        "Mach-O IFunc lowering is not yet supported on this target");
  }

  virtual void emitMachOIFuncStubHelperBody(Module &M, const GlobalIFunc &GI,
                                            MCSymbol *LazyPointer) {
    llvm_unreachable(
        "Mach-O IFunc lowering is not yet supported on this target");
  }

  /// Emit N NOP instructions.
  void emitNops(unsigned N);

  //===------------------------------------------------------------------===//
  // Symbol Lowering Routines.
  //===------------------------------------------------------------------===//

  MCSymbol *createTempSymbol(const Twine &Name) const;

  /// Return the MCSymbol for a private symbol with global value name as its
  /// base, with the specified suffix.
  MCSymbol *getSymbolWithGlobalValueBase(const GlobalValue *GV,
                                         StringRef Suffix) const;

  /// Return the MCSymbol for the specified ExternalSymbol.
  MCSymbol *GetExternalSymbolSymbol(Twine Sym) const;

  /// Return the symbol for the specified jump table entry.
  MCSymbol *GetJTISymbol(unsigned JTID, bool isLinkerPrivate = false) const;

  /// Return the symbol for the specified jump table .set
  /// FIXME: privatize to AsmPrinter.
  MCSymbol *GetJTSetSymbol(unsigned UID, unsigned MBBID) const;

  /// Return the MCSymbol used to satisfy BlockAddress uses of the specified
  /// basic block.
  MCSymbol *GetBlockAddressSymbol(const BlockAddress *BA) const;
  MCSymbol *GetBlockAddressSymbol(const BasicBlock *BB) const;

  //===------------------------------------------------------------------===//
  // Emission Helper Routines.
  //===------------------------------------------------------------------===//

  /// This is just convenient handler for printing offsets.
  void printOffset(int64_t Offset, raw_ostream &OS) const;

  /// Emit a byte directive and value.
  void emitInt8(int Value) const;

  /// Emit a short directive and value.
  void emitInt16(int Value) const;

  /// Emit a long directive and value.
  void emitInt32(int Value) const;

  /// Emit a long long directive and value.
  void emitInt64(uint64_t Value) const;

  /// Emit the specified signed leb128 value.
  void emitSLEB128(int64_t Value, const char *Desc = nullptr) const;

  /// Emit the specified unsigned leb128 value.
  void emitULEB128(uint64_t Value, const char *Desc = nullptr,
                   unsigned PadTo = 0) const;

  /// Emit something like ".long Hi-Lo" where the size in bytes of the directive
  /// is specified by Size and Hi/Lo specify the labels.  This implicitly uses
  /// .set if it is available.
  void emitLabelDifference(const MCSymbol *Hi, const MCSymbol *Lo,
                           unsigned Size) const;

  /// Emit something like ".uleb128 Hi-Lo".
  void emitLabelDifferenceAsULEB128(const MCSymbol *Hi,
                                    const MCSymbol *Lo) const;

  /// Emit something like ".long Label+Offset" where the size in bytes of the
  /// directive is specified by Size and Label specifies the label.  This
  /// implicitly uses .set if it is available.
  void emitLabelPlusOffset(const MCSymbol *Label, uint64_t Offset,
                           unsigned Size, bool IsSectionRelative = false) const;

  /// Emit something like ".long Label" where the size in bytes of the directive
  /// is specified by Size and Label specifies the label.
  void emitLabelReference(const MCSymbol *Label, unsigned Size,
                          bool IsSectionRelative = false) const {
    emitLabelPlusOffset(Label, 0, Size, IsSectionRelative);
  }

  //===------------------------------------------------------------------===//
  // Dwarf Emission Helper Routines
  //===------------------------------------------------------------------===//

  /// Emit a .byte 42 directive that corresponds to an encoding.  If verbose
  /// assembly output is enabled, we output comments describing the encoding.
  /// Desc is a string saying what the encoding is specifying (e.g. "LSDA").
  void emitEncodingByte(unsigned Val, const char *Desc = nullptr) const;

  /// Return the size of the encoding in bytes.
  unsigned GetSizeOfEncodedValue(unsigned Encoding) const;

  /// Emit reference to a ttype global with a specified encoding.
  virtual void emitTTypeReference(const GlobalValue *GV, unsigned Encoding);

  /// Emit a reference to a symbol for use in dwarf. Different object formats
  /// represent this in different ways. Some use a relocation others encode
  /// the label offset in its section.
  void emitDwarfSymbolReference(const MCSymbol *Label,
                                bool ForceOffset = false) const;

  /// Emit the 4- or 8-byte offset of a string from the start of its section.
  ///
  /// When possible, emit a DwarfStringPool section offset without any
  /// relocations, and without using the symbol.  Otherwise, defers to \a
  /// emitDwarfSymbolReference().
  ///
  /// The length of the emitted value depends on the DWARF format.
  void emitDwarfStringOffset(DwarfStringPoolEntry S) const;

  /// Emit the 4-or 8-byte offset of a string from the start of its section.
  void emitDwarfStringOffset(DwarfStringPoolEntryRef S) const {
    emitDwarfStringOffset(S.getEntry());
  }

  /// Emit something like ".long Label + Offset" or ".quad Label + Offset"
  /// depending on the DWARF format.
  void emitDwarfOffset(const MCSymbol *Label, uint64_t Offset) const;

  /// Emit 32- or 64-bit value depending on the DWARF format.
  void emitDwarfLengthOrOffset(uint64_t Value) const;

  /// Emit a unit length field. The actual format, DWARF32 or DWARF64, is chosen
  /// according to the settings.
  void emitDwarfUnitLength(uint64_t Length, const Twine &Comment) const;

  /// Emit a unit length field. The actual format, DWARF32 or DWARF64, is chosen
  /// according to the settings.
  /// Return the end symbol generated inside, the caller needs to emit it.
  MCSymbol *emitDwarfUnitLength(const Twine &Prefix,
                                const Twine &Comment) const;

  /// Emit reference to a call site with a specified encoding
  void emitCallSiteOffset(const MCSymbol *Hi, const MCSymbol *Lo,
                          unsigned Encoding) const;
  /// Emit an integer value corresponding to the call site encoding
  void emitCallSiteValue(uint64_t Value, unsigned Encoding) const;

  /// Get the value for DW_AT_APPLE_isa. Zero if no isa encoding specified.
  virtual unsigned getISAEncoding() { return 0; }

  /// Emit the directive and value for debug thread local expression
  ///
  /// \p Value - The value to emit.
  /// \p Size - The size of the integer (in bytes) to emit.
  virtual void emitDebugValue(const MCExpr *Value, unsigned Size) const;

  //===------------------------------------------------------------------===//
  // Dwarf Lowering Routines
  //===------------------------------------------------------------------===//

  /// Emit frame instruction to describe the layout of the frame.
  void emitCFIInstruction(const MCCFIInstruction &Inst) const;

  /// Emit Dwarf abbreviation table.
  template <typename T> void emitDwarfAbbrevs(const T &Abbrevs) const {
    // For each abbreviation.
    for (const auto &Abbrev : Abbrevs)
      emitDwarfAbbrev(*Abbrev);

    // Mark end of abbreviations.
    emitULEB128(0, "EOM(3)");
  }

  void emitDwarfAbbrev(const DIEAbbrev &Abbrev) const;

  /// Recursively emit Dwarf DIE tree.
  void emitDwarfDIE(const DIE &Die) const;

  //===------------------------------------------------------------------===//
  // CodeView Helper Routines
  //===------------------------------------------------------------------===//

  /// Gets information required to create a CodeView debug symbol for a jump
  /// table.
  /// Return value is <Base Address, Base Offset, Branch Address, Entry Size>
  virtual std::tuple<const MCSymbol *, uint64_t, const MCSymbol *,
                     codeview::JumpTableEntrySize>
  getCodeViewJumpTableInfo(int JTI, const MachineInstr *BranchInstr,
                           const MCSymbol *BranchLabel) const;

  //===------------------------------------------------------------------===//
  // Inline Asm Support
  //===------------------------------------------------------------------===//

  // These are hooks that targets can override to implement inline asm
  // support.  These should probably be moved out of AsmPrinter someday.

  /// Print information related to the specified machine instr that is
  /// independent of the operand, and may be independent of the instr itself.
  /// This can be useful for portably encoding the comment character or other
  /// bits of target-specific knowledge into the asmstrings.  The syntax used is
  /// ${:comment}.  Targets can override this to add support for their own
  /// strange codes.
  virtual void PrintSpecial(const MachineInstr *MI, raw_ostream &OS,
                            StringRef Code) const;

  /// Print the MachineOperand as a symbol. Targets with complex handling of
  /// symbol references should override the base implementation.
  virtual void PrintSymbolOperand(const MachineOperand &MO, raw_ostream &OS);

  /// Print the specified operand of MI, an INLINEASM instruction, using the
  /// specified assembler variant.  Targets should override this to format as
  /// appropriate.  This method can return true if the operand is erroneous.
  virtual bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                               const char *ExtraCode, raw_ostream &OS);

  /// Print the specified operand of MI, an INLINEASM instruction, using the
  /// specified assembler variant as an address. Targets should override this to
  /// format as appropriate.  This method can return true if the operand is
  /// erroneous.
  virtual bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                     const char *ExtraCode, raw_ostream &OS);

  /// Let the target do anything it needs to do before emitting inlineasm.
  /// \p StartInfo - the subtarget info before parsing inline asm
  virtual void emitInlineAsmStart() const;

  /// Let the target do anything it needs to do after emitting inlineasm.
  /// This callback can be used restore the original mode in case the
  /// inlineasm contains directives to switch modes.
  /// \p StartInfo - the original subtarget info before inline asm
  /// \p EndInfo   - the final subtarget info after parsing the inline asm,
  ///                or NULL if the value is unknown.
  virtual void emitInlineAsmEnd(const MCSubtargetInfo &StartInfo,
                                const MCSubtargetInfo *EndInfo) const;

  /// This emits visibility information about symbol, if this is supported by
  /// the target.
  void emitVisibility(MCSymbol *Sym, unsigned Visibility,
                      bool IsDefinition = true) const;

  /// This emits linkage information about \p GVSym based on \p GV, if this is
  /// supported by the target.
  virtual void emitLinkage(const GlobalValue *GV, MCSymbol *GVSym) const;

  /// Return the alignment for the specified \p GV.
  static Align getGVAlignment(const GlobalObject *GV, const DataLayout &DL,
                              Align InAlign = Align(1));

private:
  /// Private state for PrintSpecial()
  // Assign a unique ID to this machine instruction.
  mutable const MachineInstr *LastMI = nullptr;
  mutable unsigned LastFn = 0;
  mutable unsigned Counter = ~0U;

  bool DwarfUsesRelocationsAcrossSections = false;

  /// This method emits the header for the current function.
  virtual void emitFunctionHeader();

  /// This method emits a comment next to header for the current function.
  virtual void emitFunctionHeaderComment();

  /// This method emits prefix-like data before the current function.
  void emitFunctionPrefix(ArrayRef<const Constant *> Prefix);

  /// Emit a blob of inline asm to the output streamer.
  void
  emitInlineAsm(StringRef Str, const MCSubtargetInfo &STI,
                const MCTargetOptions &MCOptions,
                const MDNode *LocMDNode = nullptr,
                InlineAsm::AsmDialect AsmDialect = InlineAsm::AD_ATT) const;

  /// This method formats and emits the specified machine instruction that is an
  /// inline asm.
  void emitInlineAsm(const MachineInstr *MI) const;

  /// Add inline assembly info to the diagnostics machinery, so we can
  /// emit file and position info. Returns SrcMgr memory buffer position.
  unsigned addInlineAsmDiagBuffer(StringRef AsmStr,
                                  const MDNode *LocMDNode) const;

  //===------------------------------------------------------------------===//
  // Internal Implementation Details
  //===------------------------------------------------------------------===//

  void emitJumpTableEntry(const MachineJumpTableInfo *MJTI,
                          const MachineBasicBlock *MBB, unsigned uid) const;
  void emitLLVMUsedList(const ConstantArray *InitList);
  /// Emit llvm.ident metadata in an '.ident' directive.
  void emitModuleIdents(Module &M);
  /// Emit bytes for llvm.commandline metadata.
  virtual void emitModuleCommandLines(Module &M);

  GCMetadataPrinter *getOrCreateGCPrinter(GCStrategy &S);
  void emitGlobalIFunc(Module &M, const GlobalIFunc &GI);

private:
  /// This method decides whether the specified basic block requires a label.
  bool shouldEmitLabelForBasicBlock(const MachineBasicBlock &MBB) const;

protected:
  virtual void emitGlobalAlias(const Module &M, const GlobalAlias &GA);
  virtual bool shouldEmitWeakSwiftAsyncExtendedFramePointerFlags() const {
    return false;
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_ASMPRINTER_H
