//===- llvm/CodeGen/DwarfDebug.cpp - Dwarf Debug Framework ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing dwarf debug info into asm files.
//
//===----------------------------------------------------------------------===//

#include "DwarfDebug.h"
#include "ByteStreamer.h"
#include "DIEHash.h"
#include "DwarfCompileUnit.h"
#include "DwarfExpression.h"
#include "DwarfUnit.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "dwarfdebug"

STATISTIC(NumCSParams, "Number of dbg call site params created");

static cl::opt<bool> UseDwarfRangesBaseAddressSpecifier(
    "use-dwarf-ranges-base-address-specifier", cl::Hidden,
    cl::desc("Use base address specifiers in debug_ranges"), cl::init(false));

static cl::opt<bool> GenerateARangeSection("generate-arange-section",
                                           cl::Hidden,
                                           cl::desc("Generate dwarf aranges"),
                                           cl::init(false));

static cl::opt<bool>
    GenerateDwarfTypeUnits("generate-type-units", cl::Hidden,
                           cl::desc("Generate DWARF4 type units."),
                           cl::init(false));

static cl::opt<bool> SplitDwarfCrossCuReferences(
    "split-dwarf-cross-cu-references", cl::Hidden,
    cl::desc("Enable cross-cu references in DWO files"), cl::init(false));

enum DefaultOnOff { Default, Enable, Disable };

static cl::opt<DefaultOnOff> UnknownLocations(
    "use-unknown-locations", cl::Hidden,
    cl::desc("Make an absence of debug location information explicit."),
    cl::values(clEnumVal(Default, "At top of block or after label"),
               clEnumVal(Enable, "In all cases"), clEnumVal(Disable, "Never")),
    cl::init(Default));

static cl::opt<AccelTableKind> AccelTables(
    "accel-tables", cl::Hidden, cl::desc("Output dwarf accelerator tables."),
    cl::values(clEnumValN(AccelTableKind::Default, "Default",
                          "Default for platform"),
               clEnumValN(AccelTableKind::None, "Disable", "Disabled."),
               clEnumValN(AccelTableKind::Apple, "Apple", "Apple"),
               clEnumValN(AccelTableKind::Dwarf, "Dwarf", "DWARF")),
    cl::init(AccelTableKind::Default));

static cl::opt<DefaultOnOff>
DwarfInlinedStrings("dwarf-inlined-strings", cl::Hidden,
                 cl::desc("Use inlined strings rather than string section."),
                 cl::values(clEnumVal(Default, "Default for platform"),
                            clEnumVal(Enable, "Enabled"),
                            clEnumVal(Disable, "Disabled")),
                 cl::init(Default));

static cl::opt<bool>
    NoDwarfRangesSection("no-dwarf-ranges-section", cl::Hidden,
                         cl::desc("Disable emission .debug_ranges section."),
                         cl::init(false));

static cl::opt<DefaultOnOff> DwarfSectionsAsReferences(
    "dwarf-sections-as-references", cl::Hidden,
    cl::desc("Use sections+offset as references rather than labels."),
    cl::values(clEnumVal(Default, "Default for platform"),
               clEnumVal(Enable, "Enabled"), clEnumVal(Disable, "Disabled")),
    cl::init(Default));

static cl::opt<bool>
    UseGNUDebugMacro("use-gnu-debug-macro", cl::Hidden,
                     cl::desc("Emit the GNU .debug_macro format with DWARF <5"),
                     cl::init(false));

static cl::opt<DefaultOnOff> DwarfOpConvert(
    "dwarf-op-convert", cl::Hidden,
    cl::desc("Enable use of the DWARFv5 DW_OP_convert operator"),
    cl::values(clEnumVal(Default, "Default for platform"),
               clEnumVal(Enable, "Enabled"), clEnumVal(Disable, "Disabled")),
    cl::init(Default));

enum LinkageNameOption {
  DefaultLinkageNames,
  AllLinkageNames,
  AbstractLinkageNames
};

static cl::opt<LinkageNameOption>
    DwarfLinkageNames("dwarf-linkage-names", cl::Hidden,
                      cl::desc("Which DWARF linkage-name attributes to emit."),
                      cl::values(clEnumValN(DefaultLinkageNames, "Default",
                                            "Default for platform"),
                                 clEnumValN(AllLinkageNames, "All", "All"),
                                 clEnumValN(AbstractLinkageNames, "Abstract",
                                            "Abstract subprograms")),
                      cl::init(DefaultLinkageNames));

static cl::opt<DwarfDebug::MinimizeAddrInV5> MinimizeAddrInV5Option(
    "minimize-addr-in-v5", cl::Hidden,
    cl::desc("Always use DW_AT_ranges in DWARFv5 whenever it could allow more "
             "address pool entry sharing to reduce relocations/object size"),
    cl::values(clEnumValN(DwarfDebug::MinimizeAddrInV5::Default, "Default",
                          "Default address minimization strategy"),
               clEnumValN(DwarfDebug::MinimizeAddrInV5::Ranges, "Ranges",
                          "Use rnglists for contiguous ranges if that allows "
                          "using a pre-existing base address"),
               clEnumValN(DwarfDebug::MinimizeAddrInV5::Expressions,
                          "Expressions",
                          "Use exprloc addrx+offset expressions for any "
                          "address with a prior base address"),
               clEnumValN(DwarfDebug::MinimizeAddrInV5::Form, "Form",
                          "Use addrx+offset extension form for any address "
                          "with a prior base address"),
               clEnumValN(DwarfDebug::MinimizeAddrInV5::Disabled, "Disabled",
                          "Stuff")),
    cl::init(DwarfDebug::MinimizeAddrInV5::Default));

static constexpr unsigned ULEB128PadSize = 4;

void DebugLocDwarfExpression::emitOp(uint8_t Op, const char *Comment) {
  getActiveStreamer().emitInt8(
      Op, Comment ? Twine(Comment) + " " + dwarf::OperationEncodingString(Op)
                  : dwarf::OperationEncodingString(Op));
}

void DebugLocDwarfExpression::emitSigned(int64_t Value) {
  getActiveStreamer().emitSLEB128(Value, Twine(Value));
}

void DebugLocDwarfExpression::emitUnsigned(uint64_t Value) {
  getActiveStreamer().emitULEB128(Value, Twine(Value));
}

void DebugLocDwarfExpression::emitData1(uint8_t Value) {
  getActiveStreamer().emitInt8(Value, Twine(Value));
}

void DebugLocDwarfExpression::emitBaseTypeRef(uint64_t Idx) {
  assert(Idx < (1ULL << (ULEB128PadSize * 7)) && "Idx wont fit");
  getActiveStreamer().emitULEB128(Idx, Twine(Idx), ULEB128PadSize);
}

bool DebugLocDwarfExpression::isFrameRegister(const TargetRegisterInfo &TRI,
                                              llvm::Register MachineReg) {
  // This information is not available while emitting .debug_loc entries.
  return false;
}

void DebugLocDwarfExpression::enableTemporaryBuffer() {
  assert(!IsBuffering && "Already buffering?");
  if (!TmpBuf)
    TmpBuf = std::make_unique<TempBuffer>(OutBS.GenerateComments);
  IsBuffering = true;
}

void DebugLocDwarfExpression::disableTemporaryBuffer() { IsBuffering = false; }

unsigned DebugLocDwarfExpression::getTemporaryBufferSize() {
  return TmpBuf ? TmpBuf->Bytes.size() : 0;
}

void DebugLocDwarfExpression::commitTemporaryBuffer() {
  if (!TmpBuf)
    return;
  for (auto Byte : enumerate(TmpBuf->Bytes)) {
    const char *Comment = (Byte.index() < TmpBuf->Comments.size())
                              ? TmpBuf->Comments[Byte.index()].c_str()
                              : "";
    OutBS.emitInt8(Byte.value(), Comment);
  }
  TmpBuf->Bytes.clear();
  TmpBuf->Comments.clear();
}

const DIType *DbgVariable::getType() const {
  return getVariable()->getType();
}

/// Get .debug_loc entry for the instruction range starting at MI.
static DbgValueLoc getDebugLocValue(const MachineInstr *MI) {
  const DIExpression *Expr = MI->getDebugExpression();
  auto SingleLocExprOpt = DIExpression::convertToNonVariadicExpression(Expr);
  const bool IsVariadic = !SingleLocExprOpt;
  // If we have a variadic debug value instruction that is equivalent to a
  // non-variadic instruction, then convert it to non-variadic form here.
  if (!IsVariadic && !MI->isNonListDebugValue()) {
    assert(MI->getNumDebugOperands() == 1 &&
           "Mismatched DIExpression and debug operands for debug instruction.");
    Expr = *SingleLocExprOpt;
  }
  assert(MI->getNumOperands() >= 3);
  SmallVector<DbgValueLocEntry, 4> DbgValueLocEntries;
  for (const MachineOperand &Op : MI->debug_operands()) {
    if (Op.isReg()) {
      MachineLocation MLoc(Op.getReg(),
                           MI->isNonListDebugValue() && MI->isDebugOffsetImm());
      DbgValueLocEntries.push_back(DbgValueLocEntry(MLoc));
    } else if (Op.isTargetIndex()) {
      DbgValueLocEntries.push_back(
          DbgValueLocEntry(TargetIndexLocation(Op.getIndex(), Op.getOffset())));
    } else if (Op.isImm())
      DbgValueLocEntries.push_back(DbgValueLocEntry(Op.getImm()));
    else if (Op.isFPImm())
      DbgValueLocEntries.push_back(DbgValueLocEntry(Op.getFPImm()));
    else if (Op.isCImm())
      DbgValueLocEntries.push_back(DbgValueLocEntry(Op.getCImm()));
    else
      llvm_unreachable("Unexpected debug operand in DBG_VALUE* instruction!");
  }
  return DbgValueLoc(Expr, DbgValueLocEntries, IsVariadic);
}

static uint64_t getFragmentOffsetInBits(const DIExpression &Expr) {
  std::optional<DIExpression::FragmentInfo> Fragment = Expr.getFragmentInfo();
  return Fragment ? Fragment->OffsetInBits : 0;
}

bool llvm::operator<(const FrameIndexExpr &LHS, const FrameIndexExpr &RHS) {
  return getFragmentOffsetInBits(*LHS.Expr) <
         getFragmentOffsetInBits(*RHS.Expr);
}

bool llvm::operator<(const EntryValueInfo &LHS, const EntryValueInfo &RHS) {
  return getFragmentOffsetInBits(LHS.Expr) < getFragmentOffsetInBits(RHS.Expr);
}

Loc::Single::Single(DbgValueLoc ValueLoc)
    : ValueLoc(std::make_unique<DbgValueLoc>(ValueLoc)),
      Expr(ValueLoc.getExpression()) {
  if (!Expr->getNumElements())
    Expr = nullptr;
}

Loc::Single::Single(const MachineInstr *DbgValue)
    : Single(getDebugLocValue(DbgValue)) {}

const std::set<FrameIndexExpr> &Loc::MMI::getFrameIndexExprs() const {
  return FrameIndexExprs;
}

void Loc::MMI::addFrameIndexExpr(const DIExpression *Expr, int FI) {
  FrameIndexExprs.insert({FI, Expr});
  assert((FrameIndexExprs.size() == 1 ||
          llvm::all_of(FrameIndexExprs,
                       [](const FrameIndexExpr &FIE) {
                         return FIE.Expr && FIE.Expr->isFragment();
                       })) &&
         "conflicting locations for variable");
}

static AccelTableKind computeAccelTableKind(unsigned DwarfVersion,
                                            bool GenerateTypeUnits,
                                            DebuggerKind Tuning,
                                            const Triple &TT) {
  // Honor an explicit request.
  if (AccelTables != AccelTableKind::Default)
    return AccelTables;

  // Generating DWARF5 acceleration table.
  // Currently Split dwarf and non ELF format is not supported.
  if (GenerateTypeUnits && (DwarfVersion < 5 || !TT.isOSBinFormatELF()))
    return AccelTableKind::None;

  // Accelerator tables get emitted if targetting DWARF v5 or LLDB.  DWARF v5
  // always implies debug_names. For lower standard versions we use apple
  // accelerator tables on apple platforms and debug_names elsewhere.
  if (DwarfVersion >= 5)
    return AccelTableKind::Dwarf;
  if (Tuning == DebuggerKind::LLDB)
    return TT.isOSBinFormatMachO() ? AccelTableKind::Apple
                                   : AccelTableKind::Dwarf;
  return AccelTableKind::None;
}

DwarfDebug::DwarfDebug(AsmPrinter *A)
    : DebugHandlerBase(A), DebugLocs(A->OutStreamer->isVerboseAsm()),
      InfoHolder(A, "info_string", DIEValueAllocator),
      SkeletonHolder(A, "skel_string", DIEValueAllocator),
      IsDarwin(A->TM.getTargetTriple().isOSDarwin()) {
  const Triple &TT = Asm->TM.getTargetTriple();

  // Make sure we know our "debugger tuning".  The target option takes
  // precedence; fall back to triple-based defaults.
  if (Asm->TM.Options.DebuggerTuning != DebuggerKind::Default)
    DebuggerTuning = Asm->TM.Options.DebuggerTuning;
  else if (IsDarwin)
    DebuggerTuning = DebuggerKind::LLDB;
  else if (TT.isPS())
    DebuggerTuning = DebuggerKind::SCE;
  else if (TT.isOSAIX())
    DebuggerTuning = DebuggerKind::DBX;
  else
    DebuggerTuning = DebuggerKind::GDB;

  if (DwarfInlinedStrings == Default)
    UseInlineStrings = TT.isNVPTX() || tuneForDBX();
  else
    UseInlineStrings = DwarfInlinedStrings == Enable;

  UseLocSection = !TT.isNVPTX();

  HasAppleExtensionAttributes = tuneForLLDB();

  // Handle split DWARF.
  HasSplitDwarf = !Asm->TM.Options.MCOptions.SplitDwarfFile.empty();

  // SCE defaults to linkage names only for abstract subprograms.
  if (DwarfLinkageNames == DefaultLinkageNames)
    UseAllLinkageNames = !tuneForSCE();
  else
    UseAllLinkageNames = DwarfLinkageNames == AllLinkageNames;

  unsigned DwarfVersionNumber = Asm->TM.Options.MCOptions.DwarfVersion;
  unsigned DwarfVersion = DwarfVersionNumber ? DwarfVersionNumber
                                    : MMI->getModule()->getDwarfVersion();
  // Use dwarf 4 by default if nothing is requested. For NVPTX, use dwarf 2.
  DwarfVersion =
      TT.isNVPTX() ? 2 : (DwarfVersion ? DwarfVersion : dwarf::DWARF_VERSION);

  bool Dwarf64 = DwarfVersion >= 3 && // DWARF64 was introduced in DWARFv3.
                 TT.isArch64Bit();    // DWARF64 requires 64-bit relocations.

  // Support DWARF64
  // 1: For ELF when requested.
  // 2: For XCOFF64: the AIX assembler will fill in debug section lengths
  //    according to the DWARF64 format for 64-bit assembly, so we must use
  //    DWARF64 in the compiler too for 64-bit mode.
  Dwarf64 &=
      ((Asm->TM.Options.MCOptions.Dwarf64 || MMI->getModule()->isDwarf64()) &&
       TT.isOSBinFormatELF()) ||
      TT.isOSBinFormatXCOFF();

  if (!Dwarf64 && TT.isArch64Bit() && TT.isOSBinFormatXCOFF())
    report_fatal_error("XCOFF requires DWARF64 for 64-bit mode!");

  UseRangesSection = !NoDwarfRangesSection && !TT.isNVPTX();

  // Use sections as references. Force for NVPTX.
  if (DwarfSectionsAsReferences == Default)
    UseSectionsAsReferences = TT.isNVPTX();
  else
    UseSectionsAsReferences = DwarfSectionsAsReferences == Enable;

  // Don't generate type units for unsupported object file formats.
  GenerateTypeUnits = (A->TM.getTargetTriple().isOSBinFormatELF() ||
                       A->TM.getTargetTriple().isOSBinFormatWasm()) &&
                      GenerateDwarfTypeUnits;

  TheAccelTableKind = computeAccelTableKind(
      DwarfVersion, GenerateTypeUnits, DebuggerTuning, A->TM.getTargetTriple());

  // Work around a GDB bug. GDB doesn't support the standard opcode;
  // SCE doesn't support GNU's; LLDB prefers the standard opcode, which
  // is defined as of DWARF 3.
  // See GDB bug 11616 - DW_OP_form_tls_address is unimplemented
  // https://sourceware.org/bugzilla/show_bug.cgi?id=11616
  UseGNUTLSOpcode = tuneForGDB() || DwarfVersion < 3;

  UseDWARF2Bitfields = DwarfVersion < 4;

  // The DWARF v5 string offsets table has - possibly shared - contributions
  // from each compile and type unit each preceded by a header. The string
  // offsets table used by the pre-DWARF v5 split-DWARF implementation uses
  // a monolithic string offsets table without any header.
  UseSegmentedStringOffsetsTable = DwarfVersion >= 5;

  // Emit call-site-param debug info for GDB and LLDB, if the target supports
  // the debug entry values feature. It can also be enabled explicitly.
  EmitDebugEntryValues = Asm->TM.Options.ShouldEmitDebugEntryValues();

  // It is unclear if the GCC .debug_macro extension is well-specified
  // for split DWARF. For now, do not allow LLVM to emit it.
  UseDebugMacroSection =
      DwarfVersion >= 5 || (UseGNUDebugMacro && !useSplitDwarf());
  if (DwarfOpConvert == Default)
    EnableOpConvert = !((tuneForGDB() && useSplitDwarf()) || (tuneForLLDB() && !TT.isOSBinFormatMachO()));
  else
    EnableOpConvert = (DwarfOpConvert == Enable);

  // Split DWARF would benefit object size significantly by trading reductions
  // in address pool usage for slightly increased range list encodings.
  if (DwarfVersion >= 5)
    MinimizeAddr = MinimizeAddrInV5Option;

  Asm->OutStreamer->getContext().setDwarfVersion(DwarfVersion);
  Asm->OutStreamer->getContext().setDwarfFormat(Dwarf64 ? dwarf::DWARF64
                                                        : dwarf::DWARF32);
}

// Define out of line so we don't have to include DwarfUnit.h in DwarfDebug.h.
DwarfDebug::~DwarfDebug() = default;

static bool isObjCClass(StringRef Name) {
  return Name.starts_with("+") || Name.starts_with("-");
}

static bool hasObjCCategory(StringRef Name) {
  if (!isObjCClass(Name))
    return false;

  return Name.contains(") ");
}

static void getObjCClassCategory(StringRef In, StringRef &Class,
                                 StringRef &Category) {
  if (!hasObjCCategory(In)) {
    Class = In.slice(In.find('[') + 1, In.find(' '));
    Category = "";
    return;
  }

  Class = In.slice(In.find('[') + 1, In.find('('));
  Category = In.slice(In.find('[') + 1, In.find(' '));
}

static StringRef getObjCMethodName(StringRef In) {
  return In.slice(In.find(' ') + 1, In.find(']'));
}

// Add the various names to the Dwarf accelerator table names.
void DwarfDebug::addSubprogramNames(
    const DwarfUnit &Unit,
    const DICompileUnit::DebugNameTableKind NameTableKind,
    const DISubprogram *SP, DIE &Die) {
  if (getAccelTableKind() != AccelTableKind::Apple &&
      NameTableKind != DICompileUnit::DebugNameTableKind::Apple &&
      NameTableKind == DICompileUnit::DebugNameTableKind::None)
    return;

  if (!SP->isDefinition())
    return;

  if (SP->getName() != "")
    addAccelName(Unit, NameTableKind, SP->getName(), Die);

  // If the linkage name is different than the name, go ahead and output that as
  // well into the name table. Only do that if we are going to actually emit
  // that name.
  if (SP->getLinkageName() != "" && SP->getName() != SP->getLinkageName() &&
      (useAllLinkageNames() || InfoHolder.getAbstractScopeDIEs().lookup(SP)))
    addAccelName(Unit, NameTableKind, SP->getLinkageName(), Die);

  // If this is an Objective-C selector name add it to the ObjC accelerator
  // too.
  if (isObjCClass(SP->getName())) {
    StringRef Class, Category;
    getObjCClassCategory(SP->getName(), Class, Category);
    addAccelObjC(Unit, NameTableKind, Class, Die);
    if (Category != "")
      addAccelObjC(Unit, NameTableKind, Category, Die);
    // Also add the base method name to the name table.
    addAccelName(Unit, NameTableKind, getObjCMethodName(SP->getName()), Die);
  }
}

/// Check whether we should create a DIE for the given Scope, return true
/// if we don't create a DIE (the corresponding DIE is null).
bool DwarfDebug::isLexicalScopeDIENull(LexicalScope *Scope) {
  if (Scope->isAbstractScope())
    return false;

  // We don't create a DIE if there is no Range.
  const SmallVectorImpl<InsnRange> &Ranges = Scope->getRanges();
  if (Ranges.empty())
    return true;

  if (Ranges.size() > 1)
    return false;

  // We don't create a DIE if we have a single Range and the end label
  // is null.
  return !getLabelAfterInsn(Ranges.front().second);
}

template <typename Func> static void forBothCUs(DwarfCompileUnit &CU, Func F) {
  F(CU);
  if (auto *SkelCU = CU.getSkeleton())
    if (CU.getCUNode()->getSplitDebugInlining())
      F(*SkelCU);
}

bool DwarfDebug::shareAcrossDWOCUs() const {
  return SplitDwarfCrossCuReferences;
}

void DwarfDebug::constructAbstractSubprogramScopeDIE(DwarfCompileUnit &SrcCU,
                                                     LexicalScope *Scope) {
  assert(Scope && Scope->getScopeNode());
  assert(Scope->isAbstractScope());
  assert(!Scope->getInlinedAt());

  auto *SP = cast<DISubprogram>(Scope->getScopeNode());

  // Find the subprogram's DwarfCompileUnit in the SPMap in case the subprogram
  // was inlined from another compile unit.
  if (useSplitDwarf() && !shareAcrossDWOCUs() && !SP->getUnit()->getSplitDebugInlining())
    // Avoid building the original CU if it won't be used
    SrcCU.constructAbstractSubprogramScopeDIE(Scope);
  else {
    auto &CU = getOrCreateDwarfCompileUnit(SP->getUnit());
    if (auto *SkelCU = CU.getSkeleton()) {
      (shareAcrossDWOCUs() ? CU : SrcCU)
          .constructAbstractSubprogramScopeDIE(Scope);
      if (CU.getCUNode()->getSplitDebugInlining())
        SkelCU->constructAbstractSubprogramScopeDIE(Scope);
    } else
      CU.constructAbstractSubprogramScopeDIE(Scope);
  }
}

/// Represents a parameter whose call site value can be described by applying a
/// debug expression to a register in the forwarded register worklist.
struct FwdRegParamInfo {
  /// The described parameter register.
  unsigned ParamReg;

  /// Debug expression that has been built up when walking through the
  /// instruction chain that produces the parameter's value.
  const DIExpression *Expr;
};

/// Register worklist for finding call site values.
using FwdRegWorklist = MapVector<unsigned, SmallVector<FwdRegParamInfo, 2>>;
/// Container for the set of registers known to be clobbered on the path to a
/// call site.
using ClobberedRegSet = SmallSet<Register, 16>;

/// Append the expression \p Addition to \p Original and return the result.
static const DIExpression *combineDIExpressions(const DIExpression *Original,
                                                const DIExpression *Addition) {
  std::vector<uint64_t> Elts = Addition->getElements().vec();
  // Avoid multiple DW_OP_stack_values.
  if (Original->isImplicit() && Addition->isImplicit())
    llvm::erase(Elts, dwarf::DW_OP_stack_value);
  const DIExpression *CombinedExpr =
      (Elts.size() > 0) ? DIExpression::append(Original, Elts) : Original;
  return CombinedExpr;
}

/// Emit call site parameter entries that are described by the given value and
/// debug expression.
template <typename ValT>
static void finishCallSiteParams(ValT Val, const DIExpression *Expr,
                                 ArrayRef<FwdRegParamInfo> DescribedParams,
                                 ParamSet &Params) {
  for (auto Param : DescribedParams) {
    bool ShouldCombineExpressions = Expr && Param.Expr->getNumElements() > 0;

    // TODO: Entry value operations can currently not be combined with any
    // other expressions, so we can't emit call site entries in those cases.
    if (ShouldCombineExpressions && Expr->isEntryValue())
      continue;

    // If a parameter's call site value is produced by a chain of
    // instructions we may have already created an expression for the
    // parameter when walking through the instructions. Append that to the
    // base expression.
    const DIExpression *CombinedExpr =
        ShouldCombineExpressions ? combineDIExpressions(Expr, Param.Expr)
                                 : Expr;
    assert((!CombinedExpr || CombinedExpr->isValid()) &&
           "Combined debug expression is invalid");

    DbgValueLoc DbgLocVal(CombinedExpr, DbgValueLocEntry(Val));
    DbgCallSiteParam CSParm(Param.ParamReg, DbgLocVal);
    Params.push_back(CSParm);
    ++NumCSParams;
  }
}

/// Add \p Reg to the worklist, if it's not already present, and mark that the
/// given parameter registers' values can (potentially) be described using
/// that register and an debug expression.
static void addToFwdRegWorklist(FwdRegWorklist &Worklist, unsigned Reg,
                                const DIExpression *Expr,
                                ArrayRef<FwdRegParamInfo> ParamsToAdd) {
  auto I = Worklist.insert({Reg, {}});
  auto &ParamsForFwdReg = I.first->second;
  for (auto Param : ParamsToAdd) {
    assert(none_of(ParamsForFwdReg,
                   [Param](const FwdRegParamInfo &D) {
                     return D.ParamReg == Param.ParamReg;
                   }) &&
           "Same parameter described twice by forwarding reg");

    // If a parameter's call site value is produced by a chain of
    // instructions we may have already created an expression for the
    // parameter when walking through the instructions. Append that to the
    // new expression.
    const DIExpression *CombinedExpr = combineDIExpressions(Expr, Param.Expr);
    ParamsForFwdReg.push_back({Param.ParamReg, CombinedExpr});
  }
}

/// Interpret values loaded into registers by \p CurMI.
static void interpretValues(const MachineInstr *CurMI,
                            FwdRegWorklist &ForwardedRegWorklist,
                            ParamSet &Params,
                            ClobberedRegSet &ClobberedRegUnits) {

  const MachineFunction *MF = CurMI->getMF();
  const DIExpression *EmptyExpr =
      DIExpression::get(MF->getFunction().getContext(), {});
  const auto &TRI = *MF->getSubtarget().getRegisterInfo();
  const auto &TII = *MF->getSubtarget().getInstrInfo();
  const auto &TLI = *MF->getSubtarget().getTargetLowering();

  // If an instruction defines more than one item in the worklist, we may run
  // into situations where a worklist register's value is (potentially)
  // described by the previous value of another register that is also defined
  // by that instruction.
  //
  // This can for example occur in cases like this:
  //
  //   $r1 = mov 123
  //   $r0, $r1 = mvrr $r1, 456
  //   call @foo, $r0, $r1
  //
  // When describing $r1's value for the mvrr instruction, we need to make sure
  // that we don't finalize an entry value for $r0, as that is dependent on the
  // previous value of $r1 (123 rather than 456).
  //
  // In order to not have to distinguish between those cases when finalizing
  // entry values, we simply postpone adding new parameter registers to the
  // worklist, by first keeping them in this temporary container until the
  // instruction has been handled.
  FwdRegWorklist TmpWorklistItems;

  // If the MI is an instruction defining one or more parameters' forwarding
  // registers, add those defines.
  ClobberedRegSet NewClobberedRegUnits;
  auto getForwardingRegsDefinedByMI = [&](const MachineInstr &MI,
                                          SmallSetVector<unsigned, 4> &Defs) {
    if (MI.isDebugInstr())
      return;

    for (const MachineOperand &MO : MI.all_defs()) {
      if (MO.getReg().isPhysical()) {
        for (auto &FwdReg : ForwardedRegWorklist)
          if (TRI.regsOverlap(FwdReg.first, MO.getReg()))
            Defs.insert(FwdReg.first);
        for (MCRegUnit Unit : TRI.regunits(MO.getReg()))
          NewClobberedRegUnits.insert(Unit);
      }
    }
  };

  // Set of worklist registers that are defined by this instruction.
  SmallSetVector<unsigned, 4> FwdRegDefs;

  getForwardingRegsDefinedByMI(*CurMI, FwdRegDefs);
  if (FwdRegDefs.empty()) {
    // Any definitions by this instruction will clobber earlier reg movements.
    ClobberedRegUnits.insert(NewClobberedRegUnits.begin(),
                             NewClobberedRegUnits.end());
    return;
  }

  // It's possible that we find a copy from a non-volatile register to the param
  // register, which is clobbered in the meantime. Test for clobbered reg unit
  // overlaps before completing.
  auto IsRegClobberedInMeantime = [&](Register Reg) -> bool {
    for (auto &RegUnit : ClobberedRegUnits)
      if (TRI.hasRegUnit(Reg, RegUnit))
        return true;
    return false;
  };

  for (auto ParamFwdReg : FwdRegDefs) {
    if (auto ParamValue = TII.describeLoadedValue(*CurMI, ParamFwdReg)) {
      if (ParamValue->first.isImm()) {
        int64_t Val = ParamValue->first.getImm();
        finishCallSiteParams(Val, ParamValue->second,
                             ForwardedRegWorklist[ParamFwdReg], Params);
      } else if (ParamValue->first.isReg()) {
        Register RegLoc = ParamValue->first.getReg();
        Register SP = TLI.getStackPointerRegisterToSaveRestore();
        Register FP = TRI.getFrameRegister(*MF);
        bool IsSPorFP = (RegLoc == SP) || (RegLoc == FP);
        if (!IsRegClobberedInMeantime(RegLoc) &&
            (TRI.isCalleeSavedPhysReg(RegLoc, *MF) || IsSPorFP)) {
          MachineLocation MLoc(RegLoc, /*Indirect=*/IsSPorFP);
          finishCallSiteParams(MLoc, ParamValue->second,
                               ForwardedRegWorklist[ParamFwdReg], Params);
        } else {
          // ParamFwdReg was described by the non-callee saved register
          // RegLoc. Mark that the call site values for the parameters are
          // dependent on that register instead of ParamFwdReg. Since RegLoc
          // may be a register that will be handled in this iteration, we
          // postpone adding the items to the worklist, and instead keep them
          // in a temporary container.
          addToFwdRegWorklist(TmpWorklistItems, RegLoc, ParamValue->second,
                              ForwardedRegWorklist[ParamFwdReg]);
        }
      }
    }
  }

  // Remove all registers that this instruction defines from the worklist.
  for (auto ParamFwdReg : FwdRegDefs)
    ForwardedRegWorklist.erase(ParamFwdReg);

  // Any definitions by this instruction will clobber earlier reg movements.
  ClobberedRegUnits.insert(NewClobberedRegUnits.begin(),
                           NewClobberedRegUnits.end());

  // Now that we are done handling this instruction, add items from the
  // temporary worklist to the real one.
  for (auto &New : TmpWorklistItems)
    addToFwdRegWorklist(ForwardedRegWorklist, New.first, EmptyExpr, New.second);
  TmpWorklistItems.clear();
}

static bool interpretNextInstr(const MachineInstr *CurMI,
                               FwdRegWorklist &ForwardedRegWorklist,
                               ParamSet &Params,
                               ClobberedRegSet &ClobberedRegUnits) {
  // Skip bundle headers.
  if (CurMI->isBundle())
    return true;

  // If the next instruction is a call we can not interpret parameter's
  // forwarding registers or we finished the interpretation of all
  // parameters.
  if (CurMI->isCall())
    return false;

  if (ForwardedRegWorklist.empty())
    return false;

  // Avoid NOP description.
  if (CurMI->getNumOperands() == 0)
    return true;

  interpretValues(CurMI, ForwardedRegWorklist, Params, ClobberedRegUnits);

  return true;
}

/// Try to interpret values loaded into registers that forward parameters
/// for \p CallMI. Store parameters with interpreted value into \p Params.
static void collectCallSiteParameters(const MachineInstr *CallMI,
                                      ParamSet &Params) {
  const MachineFunction *MF = CallMI->getMF();
  const auto &CalleesMap = MF->getCallSitesInfo();
  auto CSInfo = CalleesMap.find(CallMI);

  // There is no information for the call instruction.
  if (CSInfo == CalleesMap.end())
    return;

  const MachineBasicBlock *MBB = CallMI->getParent();

  // Skip the call instruction.
  auto I = std::next(CallMI->getReverseIterator());

  FwdRegWorklist ForwardedRegWorklist;

  const DIExpression *EmptyExpr =
      DIExpression::get(MF->getFunction().getContext(), {});

  // Add all the forwarding registers into the ForwardedRegWorklist.
  for (const auto &ArgReg : CSInfo->second.ArgRegPairs) {
    bool InsertedReg =
        ForwardedRegWorklist.insert({ArgReg.Reg, {{ArgReg.Reg, EmptyExpr}}})
            .second;
    assert(InsertedReg && "Single register used to forward two arguments?");
    (void)InsertedReg;
  }

  // Do not emit CSInfo for undef forwarding registers.
  for (const auto &MO : CallMI->uses())
    if (MO.isReg() && MO.isUndef())
      ForwardedRegWorklist.erase(MO.getReg());

  // We erase, from the ForwardedRegWorklist, those forwarding registers for
  // which we successfully describe a loaded value (by using
  // the describeLoadedValue()). For those remaining arguments in the working
  // list, for which we do not describe a loaded value by
  // the describeLoadedValue(), we try to generate an entry value expression
  // for their call site value description, if the call is within the entry MBB.
  // TODO: Handle situations when call site parameter value can be described
  // as the entry value within basic blocks other than the first one.
  bool ShouldTryEmitEntryVals = MBB->getIterator() == MF->begin();

  // Search for a loading value in forwarding registers inside call delay slot.
  ClobberedRegSet ClobberedRegUnits;
  if (CallMI->hasDelaySlot()) {
    auto Suc = std::next(CallMI->getIterator());
    // Only one-instruction delay slot is supported.
    auto BundleEnd = llvm::getBundleEnd(CallMI->getIterator());
    (void)BundleEnd;
    assert(std::next(Suc) == BundleEnd &&
           "More than one instruction in call delay slot");
    // Try to interpret value loaded by instruction.
    if (!interpretNextInstr(&*Suc, ForwardedRegWorklist, Params, ClobberedRegUnits))
      return;
  }

  // Search for a loading value in forwarding registers.
  for (; I != MBB->rend(); ++I) {
    // Try to interpret values loaded by instruction.
    if (!interpretNextInstr(&*I, ForwardedRegWorklist, Params, ClobberedRegUnits))
      return;
  }

  // Emit the call site parameter's value as an entry value.
  if (ShouldTryEmitEntryVals) {
    // Create an expression where the register's entry value is used.
    DIExpression *EntryExpr = DIExpression::get(
        MF->getFunction().getContext(), {dwarf::DW_OP_LLVM_entry_value, 1});
    for (auto &RegEntry : ForwardedRegWorklist) {
      MachineLocation MLoc(RegEntry.first);
      finishCallSiteParams(MLoc, EntryExpr, RegEntry.second, Params);
    }
  }
}

void DwarfDebug::constructCallSiteEntryDIEs(const DISubprogram &SP,
                                            DwarfCompileUnit &CU, DIE &ScopeDIE,
                                            const MachineFunction &MF) {
  // Add a call site-related attribute (DWARF5, Sec. 3.3.1.3). Do this only if
  // the subprogram is required to have one.
  if (!SP.areAllCallsDescribed() || !SP.isDefinition())
    return;

  // Use DW_AT_call_all_calls to express that call site entries are present
  // for both tail and non-tail calls. Don't use DW_AT_call_all_source_calls
  // because one of its requirements is not met: call site entries for
  // optimized-out calls are elided.
  CU.addFlag(ScopeDIE, CU.getDwarf5OrGNUAttr(dwarf::DW_AT_call_all_calls));

  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  assert(TII && "TargetInstrInfo not found: cannot label tail calls");

  // Delay slot support check.
  auto delaySlotSupported = [&](const MachineInstr &MI) {
    if (!MI.isBundledWithSucc())
      return false;
    auto Suc = std::next(MI.getIterator());
    auto CallInstrBundle = getBundleStart(MI.getIterator());
    (void)CallInstrBundle;
    auto DelaySlotBundle = getBundleStart(Suc);
    (void)DelaySlotBundle;
    // Ensure that label after call is following delay slot instruction.
    // Ex. CALL_INSTRUCTION {
    //       DELAY_SLOT_INSTRUCTION }
    //      LABEL_AFTER_CALL
    assert(getLabelAfterInsn(&*CallInstrBundle) ==
               getLabelAfterInsn(&*DelaySlotBundle) &&
           "Call and its successor instruction don't have same label after.");
    return true;
  };

  // Emit call site entries for each call or tail call in the function.
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB.instrs()) {
      // Bundles with call in them will pass the isCall() test below but do not
      // have callee operand information so skip them here. Iterator will
      // eventually reach the call MI.
      if (MI.isBundle())
        continue;

      // Skip instructions which aren't calls. Both calls and tail-calling jump
      // instructions (e.g TAILJMPd64) are classified correctly here.
      if (!MI.isCandidateForCallSiteEntry())
        continue;

      // Skip instructions marked as frame setup, as they are not interesting to
      // the user.
      if (MI.getFlag(MachineInstr::FrameSetup))
        continue;

      // Check if delay slot support is enabled.
      if (MI.hasDelaySlot() && !delaySlotSupported(*&MI))
        return;

      // If this is a direct call, find the callee's subprogram.
      // In the case of an indirect call find the register that holds
      // the callee.
      const MachineOperand &CalleeOp = TII->getCalleeOperand(MI);
      if (!CalleeOp.isGlobal() &&
          (!CalleeOp.isReg() || !CalleeOp.getReg().isPhysical()))
        continue;

      unsigned CallReg = 0;
      const DISubprogram *CalleeSP = nullptr;
      const Function *CalleeDecl = nullptr;
      if (CalleeOp.isReg()) {
        CallReg = CalleeOp.getReg();
        if (!CallReg)
          continue;
      } else {
        CalleeDecl = dyn_cast<Function>(CalleeOp.getGlobal());
        if (!CalleeDecl || !CalleeDecl->getSubprogram())
          continue;
        CalleeSP = CalleeDecl->getSubprogram();
      }

      // TODO: Omit call site entries for runtime calls (objc_msgSend, etc).

      bool IsTail = TII->isTailCall(MI);

      // If MI is in a bundle, the label was created after the bundle since
      // EmitFunctionBody iterates over top-level MIs. Get that top-level MI
      // to search for that label below.
      const MachineInstr *TopLevelCallMI =
          MI.isInsideBundle() ? &*getBundleStart(MI.getIterator()) : &MI;

      // For non-tail calls, the return PC is needed to disambiguate paths in
      // the call graph which could lead to some target function. For tail
      // calls, no return PC information is needed, unless tuning for GDB in
      // DWARF4 mode in which case we fake a return PC for compatibility.
      const MCSymbol *PCAddr =
          (!IsTail || CU.useGNUAnalogForDwarf5Feature())
              ? const_cast<MCSymbol *>(getLabelAfterInsn(TopLevelCallMI))
              : nullptr;

      // For tail calls, it's necessary to record the address of the branch
      // instruction so that the debugger can show where the tail call occurred.
      const MCSymbol *CallAddr =
          IsTail ? getLabelBeforeInsn(TopLevelCallMI) : nullptr;

      assert((IsTail || PCAddr) && "Non-tail call without return PC");

      LLVM_DEBUG(dbgs() << "CallSiteEntry: " << MF.getName() << " -> "
                        << (CalleeDecl ? CalleeDecl->getName()
                                       : StringRef(MF.getSubtarget()
                                                       .getRegisterInfo()
                                                       ->getName(CallReg)))
                        << (IsTail ? " [IsTail]" : "") << "\n");

      DIE &CallSiteDIE = CU.constructCallSiteEntryDIE(
          ScopeDIE, CalleeSP, IsTail, PCAddr, CallAddr, CallReg);

      // Optionally emit call-site-param debug info.
      if (emitDebugEntryValues()) {
        ParamSet Params;
        // Try to interpret values of call site parameters.
        collectCallSiteParameters(&MI, Params);
        CU.constructCallSiteParmEntryDIEs(CallSiteDIE, Params);
      }
    }
  }
}

void DwarfDebug::addGnuPubAttributes(DwarfCompileUnit &U, DIE &D) const {
  if (!U.hasDwarfPubSections())
    return;

  U.addFlag(D, dwarf::DW_AT_GNU_pubnames);
}

void DwarfDebug::finishUnitAttributes(const DICompileUnit *DIUnit,
                                      DwarfCompileUnit &NewCU) {
  DIE &Die = NewCU.getUnitDie();
  StringRef FN = DIUnit->getFilename();

  StringRef Producer = DIUnit->getProducer();
  StringRef Flags = DIUnit->getFlags();
  if (!Flags.empty() && !useAppleExtensionAttributes()) {
    std::string ProducerWithFlags = Producer.str() + " " + Flags.str();
    NewCU.addString(Die, dwarf::DW_AT_producer, ProducerWithFlags);
  } else
    NewCU.addString(Die, dwarf::DW_AT_producer, Producer);

  NewCU.addUInt(Die, dwarf::DW_AT_language, dwarf::DW_FORM_data2,
                DIUnit->getSourceLanguage());
  NewCU.addString(Die, dwarf::DW_AT_name, FN);
  StringRef SysRoot = DIUnit->getSysRoot();
  if (!SysRoot.empty())
    NewCU.addString(Die, dwarf::DW_AT_LLVM_sysroot, SysRoot);
  StringRef SDK = DIUnit->getSDK();
  if (!SDK.empty())
    NewCU.addString(Die, dwarf::DW_AT_APPLE_sdk, SDK);

  if (!useSplitDwarf()) {
    // Add DW_str_offsets_base to the unit DIE, except for split units.
    if (useSegmentedStringOffsetsTable())
      NewCU.addStringOffsetsStart();

    NewCU.initStmtList();

    // If we're using split dwarf the compilation dir is going to be in the
    // skeleton CU and so we don't need to duplicate it here.
    if (!CompilationDir.empty())
      NewCU.addString(Die, dwarf::DW_AT_comp_dir, CompilationDir);
    addGnuPubAttributes(NewCU, Die);
  }

  if (useAppleExtensionAttributes()) {
    if (DIUnit->isOptimized())
      NewCU.addFlag(Die, dwarf::DW_AT_APPLE_optimized);

    StringRef Flags = DIUnit->getFlags();
    if (!Flags.empty())
      NewCU.addString(Die, dwarf::DW_AT_APPLE_flags, Flags);

    if (unsigned RVer = DIUnit->getRuntimeVersion())
      NewCU.addUInt(Die, dwarf::DW_AT_APPLE_major_runtime_vers,
                    dwarf::DW_FORM_data1, RVer);
  }

  if (DIUnit->getDWOId()) {
    // This CU is either a clang module DWO or a skeleton CU.
    NewCU.addUInt(Die, dwarf::DW_AT_GNU_dwo_id, dwarf::DW_FORM_data8,
                  DIUnit->getDWOId());
    if (!DIUnit->getSplitDebugFilename().empty()) {
      // This is a prefabricated skeleton CU.
      dwarf::Attribute attrDWOName = getDwarfVersion() >= 5
                                         ? dwarf::DW_AT_dwo_name
                                         : dwarf::DW_AT_GNU_dwo_name;
      NewCU.addString(Die, attrDWOName, DIUnit->getSplitDebugFilename());
    }
  }
}
// Create new DwarfCompileUnit for the given metadata node with tag
// DW_TAG_compile_unit.
DwarfCompileUnit &
DwarfDebug::getOrCreateDwarfCompileUnit(const DICompileUnit *DIUnit) {
  if (auto *CU = CUMap.lookup(DIUnit))
    return *CU;

  if (useSplitDwarf() &&
      !shareAcrossDWOCUs() &&
      (!DIUnit->getSplitDebugInlining() ||
       DIUnit->getEmissionKind() == DICompileUnit::FullDebug) &&
      !CUMap.empty()) {
    return *CUMap.begin()->second;
  }
  CompilationDir = DIUnit->getDirectory();

  auto OwnedUnit = std::make_unique<DwarfCompileUnit>(
      InfoHolder.getUnits().size(), DIUnit, Asm, this, &InfoHolder);
  DwarfCompileUnit &NewCU = *OwnedUnit;
  InfoHolder.addUnit(std::move(OwnedUnit));

  // LTO with assembly output shares a single line table amongst multiple CUs.
  // To avoid the compilation directory being ambiguous, let the line table
  // explicitly describe the directory of all files, never relying on the
  // compilation directory.
  if (!Asm->OutStreamer->hasRawTextSupport() || SingleCU)
    Asm->OutStreamer->emitDwarfFile0Directive(
        CompilationDir, DIUnit->getFilename(), getMD5AsBytes(DIUnit->getFile()),
        DIUnit->getSource(), NewCU.getUniqueID());

  if (useSplitDwarf()) {
    NewCU.setSkeleton(constructSkeletonCU(NewCU));
    NewCU.setSection(Asm->getObjFileLowering().getDwarfInfoDWOSection());
  } else {
    finishUnitAttributes(DIUnit, NewCU);
    NewCU.setSection(Asm->getObjFileLowering().getDwarfInfoSection());
  }

  CUMap.insert({DIUnit, &NewCU});
  CUDieMap.insert({&NewCU.getUnitDie(), &NewCU});
  return NewCU;
}

/// Sort and unique GVEs by comparing their fragment offset.
static SmallVectorImpl<DwarfCompileUnit::GlobalExpr> &
sortGlobalExprs(SmallVectorImpl<DwarfCompileUnit::GlobalExpr> &GVEs) {
  llvm::sort(
      GVEs, [](DwarfCompileUnit::GlobalExpr A, DwarfCompileUnit::GlobalExpr B) {
        // Sort order: first null exprs, then exprs without fragment
        // info, then sort by fragment offset in bits.
        // FIXME: Come up with a more comprehensive comparator so
        // the sorting isn't non-deterministic, and so the following
        // std::unique call works correctly.
        if (!A.Expr || !B.Expr)
          return !!B.Expr;
        auto FragmentA = A.Expr->getFragmentInfo();
        auto FragmentB = B.Expr->getFragmentInfo();
        if (!FragmentA || !FragmentB)
          return !!FragmentB;
        return FragmentA->OffsetInBits < FragmentB->OffsetInBits;
      });
  GVEs.erase(llvm::unique(GVEs,
                          [](DwarfCompileUnit::GlobalExpr A,
                             DwarfCompileUnit::GlobalExpr B) {
                            return A.Expr == B.Expr;
                          }),
             GVEs.end());
  return GVEs;
}

// Emit all Dwarf sections that should come prior to the content. Create
// global DIEs and emit initial debug info sections. This is invoked by
// the target AsmPrinter.
void DwarfDebug::beginModule(Module *M) {
  DebugHandlerBase::beginModule(M);

  if (!Asm || !MMI->hasDebugInfo())
    return;

  unsigned NumDebugCUs = std::distance(M->debug_compile_units_begin(),
                                       M->debug_compile_units_end());
  assert(NumDebugCUs > 0 && "Asm unexpectedly initialized");
  assert(MMI->hasDebugInfo() &&
         "DebugInfoAvailabilty unexpectedly not initialized");
  SingleCU = NumDebugCUs == 1;
  DenseMap<DIGlobalVariable *, SmallVector<DwarfCompileUnit::GlobalExpr, 1>>
      GVMap;
  for (const GlobalVariable &Global : M->globals()) {
    SmallVector<DIGlobalVariableExpression *, 1> GVs;
    Global.getDebugInfo(GVs);
    for (auto *GVE : GVs)
      GVMap[GVE->getVariable()].push_back({&Global, GVE->getExpression()});
  }

  // Create the symbol that designates the start of the unit's contribution
  // to the string offsets table. In a split DWARF scenario, only the skeleton
  // unit has the DW_AT_str_offsets_base attribute (and hence needs the symbol).
  if (useSegmentedStringOffsetsTable())
    (useSplitDwarf() ? SkeletonHolder : InfoHolder)
        .setStringOffsetsStartSym(Asm->createTempSymbol("str_offsets_base"));


  // Create the symbols that designates the start of the DWARF v5 range list
  // and locations list tables. They are located past the table headers.
  if (getDwarfVersion() >= 5) {
    DwarfFile &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;
    Holder.setRnglistsTableBaseSym(
        Asm->createTempSymbol("rnglists_table_base"));

    if (useSplitDwarf())
      InfoHolder.setRnglistsTableBaseSym(
          Asm->createTempSymbol("rnglists_dwo_table_base"));
  }

  // Create the symbol that points to the first entry following the debug
  // address table (.debug_addr) header.
  AddrPool.setLabel(Asm->createTempSymbol("addr_table_base"));
  DebugLocs.setSym(Asm->createTempSymbol("loclists_table_base"));

  for (DICompileUnit *CUNode : M->debug_compile_units()) {
    if (CUNode->getImportedEntities().empty() &&
        CUNode->getEnumTypes().empty() && CUNode->getRetainedTypes().empty() &&
        CUNode->getGlobalVariables().empty() && CUNode->getMacros().empty())
      continue;

    DwarfCompileUnit &CU = getOrCreateDwarfCompileUnit(CUNode);

    // Global Variables.
    for (auto *GVE : CUNode->getGlobalVariables()) {
      // Don't bother adding DIGlobalVariableExpressions listed in the CU if we
      // already know about the variable and it isn't adding a constant
      // expression.
      auto &GVMapEntry = GVMap[GVE->getVariable()];
      auto *Expr = GVE->getExpression();
      if (!GVMapEntry.size() || (Expr && Expr->isConstant()))
        GVMapEntry.push_back({nullptr, Expr});
    }

    DenseSet<DIGlobalVariable *> Processed;
    for (auto *GVE : CUNode->getGlobalVariables()) {
      DIGlobalVariable *GV = GVE->getVariable();
      if (Processed.insert(GV).second)
        CU.getOrCreateGlobalVariableDIE(GV, sortGlobalExprs(GVMap[GV]));
    }

    for (auto *Ty : CUNode->getEnumTypes())
      CU.getOrCreateTypeDIE(cast<DIType>(Ty));

    for (auto *Ty : CUNode->getRetainedTypes()) {
      // The retained types array by design contains pointers to
      // MDNodes rather than DIRefs. Unique them here.
      if (DIType *RT = dyn_cast<DIType>(Ty))
        // There is no point in force-emitting a forward declaration.
        CU.getOrCreateTypeDIE(RT);
    }
  }
}

void DwarfDebug::finishEntityDefinitions() {
  for (const auto &Entity : ConcreteEntities) {
    DIE *Die = Entity->getDIE();
    assert(Die);
    // FIXME: Consider the time-space tradeoff of just storing the unit pointer
    // in the ConcreteEntities list, rather than looking it up again here.
    // DIE::getUnit isn't simple - it walks parent pointers, etc.
    DwarfCompileUnit *Unit = CUDieMap.lookup(Die->getUnitDie());
    assert(Unit);
    Unit->finishEntityDefinition(Entity.get());
  }
}

void DwarfDebug::finishSubprogramDefinitions() {
  for (const DISubprogram *SP : ProcessedSPNodes) {
    assert(SP->getUnit()->getEmissionKind() != DICompileUnit::NoDebug);
    forBothCUs(
        getOrCreateDwarfCompileUnit(SP->getUnit()),
        [&](DwarfCompileUnit &CU) { CU.finishSubprogramDefinition(SP); });
  }
}

void DwarfDebug::finalizeModuleInfo() {
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();

  finishSubprogramDefinitions();

  finishEntityDefinitions();

  // Include the DWO file name in the hash if there's more than one CU.
  // This handles ThinLTO's situation where imported CUs may very easily be
  // duplicate with the same CU partially imported into another ThinLTO unit.
  StringRef DWOName;
  if (CUMap.size() > 1)
    DWOName = Asm->TM.Options.MCOptions.SplitDwarfFile;

  bool HasEmittedSplitCU = false;

  // Handle anything that needs to be done on a per-unit basis after
  // all other generation.
  for (const auto &P : CUMap) {
    auto &TheCU = *P.second;
    if (TheCU.getCUNode()->isDebugDirectivesOnly())
      continue;
    // Emit DW_AT_containing_type attribute to connect types with their
    // vtable holding type.
    TheCU.constructContainingTypeDIEs();

    // Add CU specific attributes if we need to add any.
    // If we're splitting the dwarf out now that we've got the entire
    // CU then add the dwo id to it.
    auto *SkCU = TheCU.getSkeleton();

    bool HasSplitUnit = SkCU && !TheCU.getUnitDie().children().empty();

    if (HasSplitUnit) {
      (void)HasEmittedSplitCU;
      assert((shareAcrossDWOCUs() || !HasEmittedSplitCU) &&
             "Multiple CUs emitted into a single dwo file");
      HasEmittedSplitCU = true;
      dwarf::Attribute attrDWOName = getDwarfVersion() >= 5
                                         ? dwarf::DW_AT_dwo_name
                                         : dwarf::DW_AT_GNU_dwo_name;
      finishUnitAttributes(TheCU.getCUNode(), TheCU);
      TheCU.addString(TheCU.getUnitDie(), attrDWOName,
                      Asm->TM.Options.MCOptions.SplitDwarfFile);
      SkCU->addString(SkCU->getUnitDie(), attrDWOName,
                      Asm->TM.Options.MCOptions.SplitDwarfFile);
      // Emit a unique identifier for this CU.
      uint64_t ID =
          DIEHash(Asm, &TheCU).computeCUSignature(DWOName, TheCU.getUnitDie());
      if (getDwarfVersion() >= 5) {
        TheCU.setDWOId(ID);
        SkCU->setDWOId(ID);
      } else {
        TheCU.addUInt(TheCU.getUnitDie(), dwarf::DW_AT_GNU_dwo_id,
                      dwarf::DW_FORM_data8, ID);
        SkCU->addUInt(SkCU->getUnitDie(), dwarf::DW_AT_GNU_dwo_id,
                      dwarf::DW_FORM_data8, ID);
      }

      if (getDwarfVersion() < 5 && !SkeletonHolder.getRangeLists().empty()) {
        const MCSymbol *Sym = TLOF.getDwarfRangesSection()->getBeginSymbol();
        SkCU->addSectionLabel(SkCU->getUnitDie(), dwarf::DW_AT_GNU_ranges_base,
                              Sym, Sym);
      }
    } else if (SkCU) {
      finishUnitAttributes(SkCU->getCUNode(), *SkCU);
    }

    // If we have code split among multiple sections or non-contiguous
    // ranges of code then emit a DW_AT_ranges attribute on the unit that will
    // remain in the .o file, otherwise add a DW_AT_low_pc.
    // FIXME: We should use ranges allow reordering of code ala
    // .subsections_via_symbols in mach-o. This would mean turning on
    // ranges for all subprogram DIEs for mach-o.
    DwarfCompileUnit &U = SkCU ? *SkCU : TheCU;

    if (unsigned NumRanges = TheCU.getRanges().size()) {
      if (NumRanges > 1 && useRangesSection())
        // A DW_AT_low_pc attribute may also be specified in combination with
        // DW_AT_ranges to specify the default base address for use in
        // location lists (see Section 2.6.2) and range lists (see Section
        // 2.17.3).
        U.addUInt(U.getUnitDie(), dwarf::DW_AT_low_pc, dwarf::DW_FORM_addr, 0);
      else
        U.setBaseAddress(TheCU.getRanges().front().Begin);
      U.attachRangesOrLowHighPC(U.getUnitDie(), TheCU.takeRanges());
    }

    // We don't keep track of which addresses are used in which CU so this
    // is a bit pessimistic under LTO.
    if ((HasSplitUnit || getDwarfVersion() >= 5) && !AddrPool.isEmpty())
      U.addAddrTableBase();

    if (getDwarfVersion() >= 5) {
      if (U.hasRangeLists())
        U.addRnglistsBase();

      if (!DebugLocs.getLists().empty() && !useSplitDwarf()) {
        U.addSectionLabel(U.getUnitDie(), dwarf::DW_AT_loclists_base,
                          DebugLocs.getSym(),
                          TLOF.getDwarfLoclistsSection()->getBeginSymbol());
      }
    }

    auto *CUNode = cast<DICompileUnit>(P.first);
    // If compile Unit has macros, emit "DW_AT_macro_info/DW_AT_macros"
    // attribute.
    if (CUNode->getMacros()) {
      if (UseDebugMacroSection) {
        if (useSplitDwarf())
          TheCU.addSectionDelta(
              TheCU.getUnitDie(), dwarf::DW_AT_macros, U.getMacroLabelBegin(),
              TLOF.getDwarfMacroDWOSection()->getBeginSymbol());
        else {
          dwarf::Attribute MacrosAttr = getDwarfVersion() >= 5
                                            ? dwarf::DW_AT_macros
                                            : dwarf::DW_AT_GNU_macros;
          U.addSectionLabel(U.getUnitDie(), MacrosAttr, U.getMacroLabelBegin(),
                            TLOF.getDwarfMacroSection()->getBeginSymbol());
        }
      } else {
        if (useSplitDwarf())
          TheCU.addSectionDelta(
              TheCU.getUnitDie(), dwarf::DW_AT_macro_info,
              U.getMacroLabelBegin(),
              TLOF.getDwarfMacinfoDWOSection()->getBeginSymbol());
        else
          U.addSectionLabel(U.getUnitDie(), dwarf::DW_AT_macro_info,
                            U.getMacroLabelBegin(),
                            TLOF.getDwarfMacinfoSection()->getBeginSymbol());
      }
    }
    }

  // Emit all frontend-produced Skeleton CUs, i.e., Clang modules.
  for (auto *CUNode : MMI->getModule()->debug_compile_units())
    if (CUNode->getDWOId())
      getOrCreateDwarfCompileUnit(CUNode);

  // Compute DIE offsets and sizes.
  InfoHolder.computeSizeAndOffsets();
  if (useSplitDwarf())
    SkeletonHolder.computeSizeAndOffsets();

  // Now that offsets are computed, can replace DIEs in debug_names Entry with
  // an actual offset.
  AccelDebugNames.convertDieToOffset();
}

// Emit all Dwarf sections that should come after the content.
void DwarfDebug::endModule() {
  // Terminate the pending line table.
  if (PrevCU)
    terminateLineTable(PrevCU);
  PrevCU = nullptr;
  assert(CurFn == nullptr);
  assert(CurMI == nullptr);

  for (const auto &P : CUMap) {
    const auto *CUNode = cast<DICompileUnit>(P.first);
    DwarfCompileUnit *CU = &*P.second;

    // Emit imported entities.
    for (auto *IE : CUNode->getImportedEntities()) {
      assert(!isa_and_nonnull<DILocalScope>(IE->getScope()) &&
             "Unexpected function-local entity in 'imports' CU field.");
      CU->getOrCreateImportedEntityDIE(IE);
    }
    for (const auto *D : CU->getDeferredLocalDecls()) {
      if (auto *IE = dyn_cast<DIImportedEntity>(D))
        CU->getOrCreateImportedEntityDIE(IE);
      else
        llvm_unreachable("Unexpected local retained node!");
    }

    // Emit base types.
    CU->createBaseTypeDIEs();
  }

  // If we aren't actually generating debug info (check beginModule -
  // conditionalized on the presence of the llvm.dbg.cu metadata node)
  if (!Asm || !MMI->hasDebugInfo())
    return;

  // Finalize the debug info for the module.
  finalizeModuleInfo();

  if (useSplitDwarf())
    // Emit debug_loc.dwo/debug_loclists.dwo section.
    emitDebugLocDWO();
  else
    // Emit debug_loc/debug_loclists section.
    emitDebugLoc();

  // Corresponding abbreviations into a abbrev section.
  emitAbbreviations();

  // Emit all the DIEs into a debug info section.
  emitDebugInfo();

  // Emit info into a debug aranges section.
  if (GenerateARangeSection)
    emitDebugARanges();

  // Emit info into a debug ranges section.
  emitDebugRanges();

  if (useSplitDwarf())
  // Emit info into a debug macinfo.dwo section.
    emitDebugMacinfoDWO();
  else
    // Emit info into a debug macinfo/macro section.
    emitDebugMacinfo();

  emitDebugStr();

  if (useSplitDwarf()) {
    emitDebugStrDWO();
    emitDebugInfoDWO();
    emitDebugAbbrevDWO();
    emitDebugLineDWO();
    emitDebugRangesDWO();
  }

  emitDebugAddr();

  // Emit info into the dwarf accelerator table sections.
  switch (getAccelTableKind()) {
  case AccelTableKind::Apple:
    emitAccelNames();
    emitAccelObjC();
    emitAccelNamespaces();
    emitAccelTypes();
    break;
  case AccelTableKind::Dwarf:
    emitAccelDebugNames();
    break;
  case AccelTableKind::None:
    break;
  case AccelTableKind::Default:
    llvm_unreachable("Default should have already been resolved.");
  }

  // Emit the pubnames and pubtypes sections if requested.
  emitDebugPubSections();

  // clean up.
  // FIXME: AbstractVariables.clear();
}

void DwarfDebug::ensureAbstractEntityIsCreatedIfScoped(DwarfCompileUnit &CU,
    const DINode *Node, const MDNode *ScopeNode) {
  if (CU.getExistingAbstractEntity(Node))
    return;

  if (LexicalScope *Scope =
          LScopes.findAbstractScope(cast_or_null<DILocalScope>(ScopeNode)))
    CU.createAbstractEntity(Node, Scope);
}

static const DILocalScope *getRetainedNodeScope(const MDNode *N) {
  const DIScope *S;
  if (const auto *LV = dyn_cast<DILocalVariable>(N))
    S = LV->getScope();
  else if (const auto *L = dyn_cast<DILabel>(N))
    S = L->getScope();
  else if (const auto *IE = dyn_cast<DIImportedEntity>(N))
    S = IE->getScope();
  else
    llvm_unreachable("Unexpected retained node!");

  // Ensure the scope is not a DILexicalBlockFile.
  return cast<DILocalScope>(S)->getNonLexicalBlockFileScope();
}

// Collect variable information from side table maintained by MF.
void DwarfDebug::collectVariableInfoFromMFTable(
    DwarfCompileUnit &TheCU, DenseSet<InlinedEntity> &Processed) {
  SmallDenseMap<InlinedEntity, DbgVariable *> MFVars;
  LLVM_DEBUG(dbgs() << "DwarfDebug: collecting variables from MF side table\n");
  for (const auto &VI : Asm->MF->getVariableDbgInfo()) {
    if (!VI.Var)
      continue;
    assert(VI.Var->isValidLocationForIntrinsic(VI.Loc) &&
           "Expected inlined-at fields to agree");

    InlinedEntity Var(VI.Var, VI.Loc->getInlinedAt());
    Processed.insert(Var);
    LexicalScope *Scope = LScopes.findLexicalScope(VI.Loc);

    // If variable scope is not found then skip this variable.
    if (!Scope) {
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << VI.Var->getName()
                        << ", no variable scope found\n");
      continue;
    }

    ensureAbstractEntityIsCreatedIfScoped(TheCU, Var.first, Scope->getScopeNode());

    // If we have already seen information for this variable, add to what we
    // already know.
    if (DbgVariable *PreviousLoc = MFVars.lookup(Var)) {
      auto *PreviousMMI = std::get_if<Loc::MMI>(PreviousLoc);
      auto *PreviousEntryValue = std::get_if<Loc::EntryValue>(PreviousLoc);
      // Previous and new locations are both stack slots (MMI).
      if (PreviousMMI && VI.inStackSlot())
        PreviousMMI->addFrameIndexExpr(VI.Expr, VI.getStackSlot());
      // Previous and new locations are both entry values.
      else if (PreviousEntryValue && VI.inEntryValueRegister())
        PreviousEntryValue->addExpr(VI.getEntryValueRegister(), *VI.Expr);
      else {
        // Locations differ, this should (rarely) happen in optimized async
        // coroutines.
        // Prefer whichever location has an EntryValue.
        if (PreviousLoc->holds<Loc::MMI>())
          PreviousLoc->emplace<Loc::EntryValue>(VI.getEntryValueRegister(),
                                                *VI.Expr);
        LLVM_DEBUG(dbgs() << "Dropping debug info for " << VI.Var->getName()
                          << ", conflicting fragment location types\n");
      }
      continue;
    }

    auto RegVar = std::make_unique<DbgVariable>(
                    cast<DILocalVariable>(Var.first), Var.second);
    if (VI.inStackSlot())
      RegVar->emplace<Loc::MMI>(VI.Expr, VI.getStackSlot());
    else
      RegVar->emplace<Loc::EntryValue>(VI.getEntryValueRegister(), *VI.Expr);
    LLVM_DEBUG(dbgs() << "Created DbgVariable for " << VI.Var->getName()
                      << "\n");
    InfoHolder.addScopeVariable(Scope, RegVar.get());
    MFVars.insert({Var, RegVar.get()});
    ConcreteEntities.push_back(std::move(RegVar));
  }
}

/// Determine whether a *singular* DBG_VALUE is valid for the entirety of its
/// enclosing lexical scope. The check ensures there are no other instructions
/// in the same lexical scope preceding the DBG_VALUE and that its range is
/// either open or otherwise rolls off the end of the scope.
static bool validThroughout(LexicalScopes &LScopes,
                            const MachineInstr *DbgValue,
                            const MachineInstr *RangeEnd,
                            const InstructionOrdering &Ordering) {
  assert(DbgValue->getDebugLoc() && "DBG_VALUE without a debug location");
  auto MBB = DbgValue->getParent();
  auto DL = DbgValue->getDebugLoc();
  auto *LScope = LScopes.findLexicalScope(DL);
  // Scope doesn't exist; this is a dead DBG_VALUE.
  if (!LScope)
    return false;
  auto &LSRange = LScope->getRanges();
  if (LSRange.size() == 0)
    return false;

  const MachineInstr *LScopeBegin = LSRange.front().first;
  // If the scope starts before the DBG_VALUE then we may have a negative
  // result. Otherwise the location is live coming into the scope and we
  // can skip the following checks.
  if (!Ordering.isBefore(DbgValue, LScopeBegin)) {
    // Exit if the lexical scope begins outside of the current block.
    if (LScopeBegin->getParent() != MBB)
      return false;

    MachineBasicBlock::const_reverse_iterator Pred(DbgValue);
    for (++Pred; Pred != MBB->rend(); ++Pred) {
      if (Pred->getFlag(MachineInstr::FrameSetup))
        break;
      auto PredDL = Pred->getDebugLoc();
      if (!PredDL || Pred->isMetaInstruction())
        continue;
      // Check whether the instruction preceding the DBG_VALUE is in the same
      // (sub)scope as the DBG_VALUE.
      if (DL->getScope() == PredDL->getScope())
        return false;
      auto *PredScope = LScopes.findLexicalScope(PredDL);
      if (!PredScope || LScope->dominates(PredScope))
        return false;
    }
  }

  // If the range of the DBG_VALUE is open-ended, report success.
  if (!RangeEnd)
    return true;

  // Single, constant DBG_VALUEs in the prologue are promoted to be live
  // throughout the function. This is a hack, presumably for DWARF v2 and not
  // necessarily correct. It would be much better to use a dbg.declare instead
  // if we know the constant is live throughout the scope.
  if (MBB->pred_empty() &&
      all_of(DbgValue->debug_operands(),
             [](const MachineOperand &Op) { return Op.isImm(); }))
    return true;

  // Test if the location terminates before the end of the scope.
  const MachineInstr *LScopeEnd = LSRange.back().second;
  if (Ordering.isBefore(RangeEnd, LScopeEnd))
    return false;

  // There's a single location which starts at the scope start, and ends at or
  // after the scope end.
  return true;
}

/// Build the location list for all DBG_VALUEs in the function that
/// describe the same variable. The resulting DebugLocEntries will have
/// strict monotonically increasing begin addresses and will never
/// overlap. If the resulting list has only one entry that is valid
/// throughout variable's scope return true.
//
// See the definition of DbgValueHistoryMap::Entry for an explanation of the
// different kinds of history map entries. One thing to be aware of is that if
// a debug value is ended by another entry (rather than being valid until the
// end of the function), that entry's instruction may or may not be included in
// the range, depending on if the entry is a clobbering entry (it has an
// instruction that clobbers one or more preceding locations), or if it is an
// (overlapping) debug value entry. This distinction can be seen in the example
// below. The first debug value is ended by the clobbering entry 2, and the
// second and third debug values are ended by the overlapping debug value entry
// 4.
//
// Input:
//
//   History map entries [type, end index, mi]
//
// 0 |      [DbgValue, 2, DBG_VALUE $reg0, [...] (fragment 0, 32)]
// 1 | |    [DbgValue, 4, DBG_VALUE $reg1, [...] (fragment 32, 32)]
// 2 | |    [Clobber, $reg0 = [...], -, -]
// 3   | |  [DbgValue, 4, DBG_VALUE 123, [...] (fragment 64, 32)]
// 4        [DbgValue, ~0, DBG_VALUE @g, [...] (fragment 0, 96)]
//
// Output [start, end) [Value...]:
//
// [0-1)    [(reg0, fragment 0, 32)]
// [1-3)    [(reg0, fragment 0, 32), (reg1, fragment 32, 32)]
// [3-4)    [(reg1, fragment 32, 32), (123, fragment 64, 32)]
// [4-)     [(@g, fragment 0, 96)]
bool DwarfDebug::buildLocationList(SmallVectorImpl<DebugLocEntry> &DebugLoc,
                                   const DbgValueHistoryMap::Entries &Entries) {
  using OpenRange =
      std::pair<DbgValueHistoryMap::EntryIndex, DbgValueLoc>;
  SmallVector<OpenRange, 4> OpenRanges;
  bool isSafeForSingleLocation = true;
  const MachineInstr *StartDebugMI = nullptr;
  const MachineInstr *EndMI = nullptr;

  for (auto EB = Entries.begin(), EI = EB, EE = Entries.end(); EI != EE; ++EI) {
    const MachineInstr *Instr = EI->getInstr();

    // Remove all values that are no longer live.
    size_t Index = std::distance(EB, EI);
    erase_if(OpenRanges, [&](OpenRange &R) { return R.first <= Index; });

    // If we are dealing with a clobbering entry, this iteration will result in
    // a location list entry starting after the clobbering instruction.
    const MCSymbol *StartLabel =
        EI->isClobber() ? getLabelAfterInsn(Instr) : getLabelBeforeInsn(Instr);
    assert(StartLabel &&
           "Forgot label before/after instruction starting a range!");

    const MCSymbol *EndLabel;
    if (std::next(EI) == Entries.end()) {
      const MachineBasicBlock &EndMBB = Asm->MF->back();
      EndLabel = Asm->MBBSectionRanges[EndMBB.getSectionID()].EndLabel;
      if (EI->isClobber())
        EndMI = EI->getInstr();
    }
    else if (std::next(EI)->isClobber())
      EndLabel = getLabelAfterInsn(std::next(EI)->getInstr());
    else
      EndLabel = getLabelBeforeInsn(std::next(EI)->getInstr());
    assert(EndLabel && "Forgot label after instruction ending a range!");

    if (EI->isDbgValue())
      LLVM_DEBUG(dbgs() << "DotDebugLoc: " << *Instr << "\n");

    // If this history map entry has a debug value, add that to the list of
    // open ranges and check if its location is valid for a single value
    // location.
    if (EI->isDbgValue()) {
      // Do not add undef debug values, as they are redundant information in
      // the location list entries. An undef debug results in an empty location
      // description. If there are any non-undef fragments then padding pieces
      // with empty location descriptions will automatically be inserted, and if
      // all fragments are undef then the whole location list entry is
      // redundant.
      if (!Instr->isUndefDebugValue()) {
        auto Value = getDebugLocValue(Instr);
        OpenRanges.emplace_back(EI->getEndIndex(), Value);

        // TODO: Add support for single value fragment locations.
        if (Instr->getDebugExpression()->isFragment())
          isSafeForSingleLocation = false;

        if (!StartDebugMI)
          StartDebugMI = Instr;
      } else {
        isSafeForSingleLocation = false;
      }
    }

    // Location list entries with empty location descriptions are redundant
    // information in DWARF, so do not emit those.
    if (OpenRanges.empty())
      continue;

    // Omit entries with empty ranges as they do not have any effect in DWARF.
    if (StartLabel == EndLabel) {
      LLVM_DEBUG(dbgs() << "Omitting location list entry with empty range.\n");
      continue;
    }

    SmallVector<DbgValueLoc, 4> Values;
    for (auto &R : OpenRanges)
      Values.push_back(R.second);

    // With Basic block sections, it is posssible that the StartLabel and the
    // Instr are not in the same section.  This happens when the StartLabel is
    // the function begin label and the dbg value appears in a basic block
    // that is not the entry.  In this case, the range needs to be split to
    // span each individual section in the range from StartLabel to EndLabel.
    if (Asm->MF->hasBBSections() && StartLabel == Asm->getFunctionBegin() &&
        !Instr->getParent()->sameSection(&Asm->MF->front())) {
      const MCSymbol *BeginSectionLabel = StartLabel;

      for (const MachineBasicBlock &MBB : *Asm->MF) {
        if (MBB.isBeginSection() && &MBB != &Asm->MF->front())
          BeginSectionLabel = MBB.getSymbol();

        if (MBB.sameSection(Instr->getParent())) {
          DebugLoc.emplace_back(BeginSectionLabel, EndLabel, Values);
          break;
        }
        if (MBB.isEndSection())
          DebugLoc.emplace_back(BeginSectionLabel, MBB.getEndSymbol(), Values);
      }
    } else {
      DebugLoc.emplace_back(StartLabel, EndLabel, Values);
    }

    // Attempt to coalesce the ranges of two otherwise identical
    // DebugLocEntries.
    auto CurEntry = DebugLoc.rbegin();
    LLVM_DEBUG({
      dbgs() << CurEntry->getValues().size() << " Values:\n";
      for (auto &Value : CurEntry->getValues())
        Value.dump();
      dbgs() << "-----\n";
    });

    auto PrevEntry = std::next(CurEntry);
    if (PrevEntry != DebugLoc.rend() && PrevEntry->MergeRanges(*CurEntry))
      DebugLoc.pop_back();
  }

  if (!isSafeForSingleLocation ||
      !validThroughout(LScopes, StartDebugMI, EndMI, getInstOrdering()))
    return false;

  if (DebugLoc.size() == 1)
    return true;

  if (!Asm->MF->hasBBSections())
    return false;

  // Check here to see if loclist can be merged into a single range. If not,
  // we must keep the split loclists per section.  This does exactly what
  // MergeRanges does without sections.  We don't actually merge the ranges
  // as the split ranges must be kept intact if this cannot be collapsed
  // into a single range.
  const MachineBasicBlock *RangeMBB = nullptr;
  if (DebugLoc[0].getBeginSym() == Asm->getFunctionBegin())
    RangeMBB = &Asm->MF->front();
  else
    RangeMBB = Entries.begin()->getInstr()->getParent();
  auto *CurEntry = DebugLoc.begin();
  auto *NextEntry = std::next(CurEntry);
  while (NextEntry != DebugLoc.end()) {
    // Get the last machine basic block of this section.
    while (!RangeMBB->isEndSection())
      RangeMBB = RangeMBB->getNextNode();
    if (!RangeMBB->getNextNode())
      return false;
    // CurEntry should end the current section and NextEntry should start
    // the next section and the Values must match for these two ranges to be
    // merged.
    if (CurEntry->getEndSym() != RangeMBB->getEndSymbol() ||
        NextEntry->getBeginSym() != RangeMBB->getNextNode()->getSymbol() ||
        CurEntry->getValues() != NextEntry->getValues())
      return false;
    RangeMBB = RangeMBB->getNextNode();
    CurEntry = NextEntry;
    NextEntry = std::next(CurEntry);
  }
  return true;
}

DbgEntity *DwarfDebug::createConcreteEntity(DwarfCompileUnit &TheCU,
                                            LexicalScope &Scope,
                                            const DINode *Node,
                                            const DILocation *Location,
                                            const MCSymbol *Sym) {
  ensureAbstractEntityIsCreatedIfScoped(TheCU, Node, Scope.getScopeNode());
  if (isa<const DILocalVariable>(Node)) {
    ConcreteEntities.push_back(
        std::make_unique<DbgVariable>(cast<const DILocalVariable>(Node),
                                       Location));
    InfoHolder.addScopeVariable(&Scope,
        cast<DbgVariable>(ConcreteEntities.back().get()));
  } else if (isa<const DILabel>(Node)) {
    ConcreteEntities.push_back(
        std::make_unique<DbgLabel>(cast<const DILabel>(Node),
                                    Location, Sym));
    InfoHolder.addScopeLabel(&Scope,
        cast<DbgLabel>(ConcreteEntities.back().get()));
  }
  return ConcreteEntities.back().get();
}

// Find variables for each lexical scope.
void DwarfDebug::collectEntityInfo(DwarfCompileUnit &TheCU,
                                   const DISubprogram *SP,
                                   DenseSet<InlinedEntity> &Processed) {
  // Grab the variable info that was squirreled away in the MMI side-table.
  collectVariableInfoFromMFTable(TheCU, Processed);

  for (const auto &I : DbgValues) {
    InlinedEntity IV = I.first;
    if (Processed.count(IV))
      continue;

    // Instruction ranges, specifying where IV is accessible.
    const auto &HistoryMapEntries = I.second;

    // Try to find any non-empty variable location. Do not create a concrete
    // entity if there are no locations.
    if (!DbgValues.hasNonEmptyLocation(HistoryMapEntries))
      continue;

    LexicalScope *Scope = nullptr;
    const DILocalVariable *LocalVar = cast<DILocalVariable>(IV.first);
    if (const DILocation *IA = IV.second)
      Scope = LScopes.findInlinedScope(LocalVar->getScope(), IA);
    else
      Scope = LScopes.findLexicalScope(LocalVar->getScope());
    // If variable scope is not found then skip this variable.
    if (!Scope)
      continue;

    Processed.insert(IV);
    DbgVariable *RegVar = cast<DbgVariable>(createConcreteEntity(TheCU,
                                            *Scope, LocalVar, IV.second));

    const MachineInstr *MInsn = HistoryMapEntries.front().getInstr();
    assert(MInsn->isDebugValue() && "History must begin with debug value");

    // Check if there is a single DBG_VALUE, valid throughout the var's scope.
    // If the history map contains a single debug value, there may be an
    // additional entry which clobbers the debug value.
    size_t HistSize = HistoryMapEntries.size();
    bool SingleValueWithClobber =
        HistSize == 2 && HistoryMapEntries[1].isClobber();
    if (HistSize == 1 || SingleValueWithClobber) {
      const auto *End =
          SingleValueWithClobber ? HistoryMapEntries[1].getInstr() : nullptr;
      if (validThroughout(LScopes, MInsn, End, getInstOrdering())) {
        RegVar->emplace<Loc::Single>(MInsn);
        continue;
      }
    }

    // Do not emit location lists if .debug_loc secton is disabled.
    if (!useLocSection())
      continue;

    // Handle multiple DBG_VALUE instructions describing one variable.
    DebugLocStream::ListBuilder List(DebugLocs, TheCU, *Asm, *RegVar);

    // Build the location list for this variable.
    SmallVector<DebugLocEntry, 8> Entries;
    bool isValidSingleLocation = buildLocationList(Entries, HistoryMapEntries);

    // Check whether buildLocationList managed to merge all locations to one
    // that is valid throughout the variable's scope. If so, produce single
    // value location.
    if (isValidSingleLocation) {
      RegVar->emplace<Loc::Single>(Entries[0].getValues()[0]);
      continue;
    }

    // If the variable has a DIBasicType, extract it.  Basic types cannot have
    // unique identifiers, so don't bother resolving the type with the
    // identifier map.
    const DIBasicType *BT = dyn_cast<DIBasicType>(
        static_cast<const Metadata *>(LocalVar->getType()));

    // Finalize the entry by lowering it into a DWARF bytestream.
    for (auto &Entry : Entries)
      Entry.finalize(*Asm, List, BT, TheCU);
  }

  // For each InlinedEntity collected from DBG_LABEL instructions, convert to
  // DWARF-related DbgLabel.
  for (const auto &I : DbgLabels) {
    InlinedEntity IL = I.first;
    const MachineInstr *MI = I.second;
    if (MI == nullptr)
      continue;

    LexicalScope *Scope = nullptr;
    const DILabel *Label = cast<DILabel>(IL.first);
    // The scope could have an extra lexical block file.
    const DILocalScope *LocalScope =
        Label->getScope()->getNonLexicalBlockFileScope();
    // Get inlined DILocation if it is inlined label.
    if (const DILocation *IA = IL.second)
      Scope = LScopes.findInlinedScope(LocalScope, IA);
    else
      Scope = LScopes.findLexicalScope(LocalScope);
    // If label scope is not found then skip this label.
    if (!Scope)
      continue;

    Processed.insert(IL);
    /// At this point, the temporary label is created.
    /// Save the temporary label to DbgLabel entity to get the
    /// actually address when generating Dwarf DIE.
    MCSymbol *Sym = getLabelBeforeInsn(MI);
    createConcreteEntity(TheCU, *Scope, Label, IL.second, Sym);
  }

  // Collect info for retained nodes.
  for (const DINode *DN : SP->getRetainedNodes()) {
    const auto *LS = getRetainedNodeScope(DN);
    if (isa<DILocalVariable>(DN) || isa<DILabel>(DN)) {
      if (!Processed.insert(InlinedEntity(DN, nullptr)).second)
        continue;
      LexicalScope *LexS = LScopes.findLexicalScope(LS);
      if (LexS)
        createConcreteEntity(TheCU, *LexS, DN, nullptr);
    } else {
      LocalDeclsPerLS[LS].insert(DN);
    }
  }
}

// Process beginning of an instruction.
void DwarfDebug::beginInstruction(const MachineInstr *MI) {
  const MachineFunction &MF = *MI->getMF();
  const auto *SP = MF.getFunction().getSubprogram();
  bool NoDebug =
      !SP || SP->getUnit()->getEmissionKind() == DICompileUnit::NoDebug;

  // Delay slot support check.
  auto delaySlotSupported = [](const MachineInstr &MI) {
    if (!MI.isBundledWithSucc())
      return false;
    auto Suc = std::next(MI.getIterator());
    (void)Suc;
    // Ensure that delay slot instruction is successor of the call instruction.
    // Ex. CALL_INSTRUCTION {
    //        DELAY_SLOT_INSTRUCTION }
    assert(Suc->isBundledWithPred() &&
           "Call bundle instructions are out of order");
    return true;
  };

  // When describing calls, we need a label for the call instruction.
  if (!NoDebug && SP->areAllCallsDescribed() &&
      MI->isCandidateForCallSiteEntry(MachineInstr::AnyInBundle) &&
      (!MI->hasDelaySlot() || delaySlotSupported(*MI))) {
    const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
    bool IsTail = TII->isTailCall(*MI);
    // For tail calls, we need the address of the branch instruction for
    // DW_AT_call_pc.
    if (IsTail)
      requestLabelBeforeInsn(MI);
    // For non-tail calls, we need the return address for the call for
    // DW_AT_call_return_pc. Under GDB tuning, this information is needed for
    // tail calls as well.
    requestLabelAfterInsn(MI);
  }

  DebugHandlerBase::beginInstruction(MI);
  if (!CurMI)
    return;

  if (NoDebug)
    return;

  // Check if source location changes, but ignore DBG_VALUE and CFI locations.
  // If the instruction is part of the function frame setup code, do not emit
  // any line record, as there is no correspondence with any user code.
  if (MI->isMetaInstruction() || MI->getFlag(MachineInstr::FrameSetup))
    return;
  const DebugLoc &DL = MI->getDebugLoc();
  unsigned Flags = 0;

  if (MI->getFlag(MachineInstr::FrameDestroy) && DL) {
    const MachineBasicBlock *MBB = MI->getParent();
    if (MBB && (MBB != EpilogBeginBlock)) {
      // First time FrameDestroy has been seen in this basic block
      EpilogBeginBlock = MBB;
      Flags |= DWARF2_FLAG_EPILOGUE_BEGIN;
    }
  }

  // When we emit a line-0 record, we don't update PrevInstLoc; so look at
  // the last line number actually emitted, to see if it was line 0.
  unsigned LastAsmLine =
      Asm->OutStreamer->getContext().getCurrentDwarfLoc().getLine();

  bool PrevInstInSameSection =
      (!PrevInstBB ||
       PrevInstBB->getSectionID() == MI->getParent()->getSectionID());
  if (DL == PrevInstLoc && PrevInstInSameSection) {
    // If we have an ongoing unspecified location, nothing to do here.
    if (!DL)
      return;
    // We have an explicit location, same as the previous location.
    // But we might be coming back to it after a line 0 record.
    if ((LastAsmLine == 0 && DL.getLine() != 0) || Flags) {
      // Reinstate the source location but not marked as a statement.
      const MDNode *Scope = DL.getScope();
      recordSourceLine(DL.getLine(), DL.getCol(), Scope, Flags);
    }
    return;
  }

  if (!DL) {
    // We have an unspecified location, which might want to be line 0.
    // If we have already emitted a line-0 record, don't repeat it.
    if (LastAsmLine == 0)
      return;
    // If user said Don't Do That, don't do that.
    if (UnknownLocations == Disable)
      return;
    // See if we have a reason to emit a line-0 record now.
    // Reasons to emit a line-0 record include:
    // - User asked for it (UnknownLocations).
    // - Instruction has a label, so it's referenced from somewhere else,
    //   possibly debug information; we want it to have a source location.
    // - Instruction is at the top of a block; we don't want to inherit the
    //   location from the physically previous (maybe unrelated) block.
    if (UnknownLocations == Enable || PrevLabel ||
        (PrevInstBB && PrevInstBB != MI->getParent())) {
      // Preserve the file and column numbers, if we can, to save space in
      // the encoded line table.
      // Do not update PrevInstLoc, it remembers the last non-0 line.
      const MDNode *Scope = nullptr;
      unsigned Column = 0;
      if (PrevInstLoc) {
        Scope = PrevInstLoc.getScope();
        Column = PrevInstLoc.getCol();
      }
      recordSourceLine(/*Line=*/0, Column, Scope, /*Flags=*/0);
    }
    return;
  }

  // We have an explicit location, different from the previous location.
  // Don't repeat a line-0 record, but otherwise emit the new location.
  // (The new location might be an explicit line 0, which we do emit.)
  if (DL.getLine() == 0 && LastAsmLine == 0)
    return;
  if (DL == PrologEndLoc) {
    Flags |= DWARF2_FLAG_PROLOGUE_END | DWARF2_FLAG_IS_STMT;
    PrologEndLoc = DebugLoc();
  }
  // If the line changed, we call that a new statement; unless we went to
  // line 0 and came back, in which case it is not a new statement.
  unsigned OldLine = PrevInstLoc ? PrevInstLoc.getLine() : LastAsmLine;
  if (DL.getLine() && DL.getLine() != OldLine)
    Flags |= DWARF2_FLAG_IS_STMT;

  const MDNode *Scope = DL.getScope();
  recordSourceLine(DL.getLine(), DL.getCol(), Scope, Flags);

  // If we're not at line 0, remember this location.
  if (DL.getLine())
    PrevInstLoc = DL;
}

static std::pair<DebugLoc, bool> findPrologueEndLoc(const MachineFunction *MF) {
  // First known non-DBG_VALUE and non-frame setup location marks
  // the beginning of the function body.
  DebugLoc LineZeroLoc;
  const Function &F = MF->getFunction();

  // Some instructions may be inserted into prologue after this function. Must
  // keep prologue for these cases.
  bool IsEmptyPrologue =
      !(F.hasPrologueData() || F.getMetadata(LLVMContext::MD_func_sanitize));
  for (const auto &MBB : *MF) {
    for (const auto &MI : MBB) {
      if (!MI.isMetaInstruction()) {
        if (!MI.getFlag(MachineInstr::FrameSetup) && MI.getDebugLoc()) {
          // Scan forward to try to find a non-zero line number. The
          // prologue_end marks the first breakpoint in the function after the
          // frame setup, and a compiler-generated line 0 location is not a
          // meaningful breakpoint. If none is found, return the first
          // location after the frame setup.
          if (MI.getDebugLoc().getLine())
            return std::make_pair(MI.getDebugLoc(), IsEmptyPrologue);

          LineZeroLoc = MI.getDebugLoc();
        }
        IsEmptyPrologue = false;
      }
    }
  }
  return std::make_pair(LineZeroLoc, IsEmptyPrologue);
}

/// Register a source line with debug info. Returns the  unique label that was
/// emitted and which provides correspondence to the source line list.
static void recordSourceLine(AsmPrinter &Asm, unsigned Line, unsigned Col,
                             const MDNode *S, unsigned Flags, unsigned CUID,
                             uint16_t DwarfVersion,
                             ArrayRef<std::unique_ptr<DwarfCompileUnit>> DCUs) {
  StringRef Fn;
  unsigned FileNo = 1;
  unsigned Discriminator = 0;
  if (auto *Scope = cast_or_null<DIScope>(S)) {
    Fn = Scope->getFilename();
    if (Line != 0 && DwarfVersion >= 4)
      if (auto *LBF = dyn_cast<DILexicalBlockFile>(Scope))
        Discriminator = LBF->getDiscriminator();

    FileNo = static_cast<DwarfCompileUnit &>(*DCUs[CUID])
                 .getOrCreateSourceID(Scope->getFile());
  }
  Asm.OutStreamer->emitDwarfLocDirective(FileNo, Line, Col, Flags, 0,
                                         Discriminator, Fn);
}

DebugLoc DwarfDebug::emitInitialLocDirective(const MachineFunction &MF,
                                             unsigned CUID) {
  std::pair<DebugLoc, bool> PrologEnd = findPrologueEndLoc(&MF);
  DebugLoc PrologEndLoc = PrologEnd.first;
  bool IsEmptyPrologue = PrologEnd.second;

  // Get beginning of function.
  if (PrologEndLoc) {
    // If the prolog is empty, no need to generate scope line for the proc.
    if (IsEmptyPrologue)
      return PrologEndLoc;

    // Ensure the compile unit is created if the function is called before
    // beginFunction().
    (void)getOrCreateDwarfCompileUnit(
        MF.getFunction().getSubprogram()->getUnit());
    // We'd like to list the prologue as "not statements" but GDB behaves
    // poorly if we do that. Revisit this with caution/GDB (7.5+) testing.
    const DISubprogram *SP = PrologEndLoc->getInlinedAtScope()->getSubprogram();
    ::recordSourceLine(*Asm, SP->getScopeLine(), 0, SP, DWARF2_FLAG_IS_STMT,
                       CUID, getDwarfVersion(), getUnits());
    return PrologEndLoc;
  }
  return DebugLoc();
}

// Gather pre-function debug information.  Assumes being called immediately
// after the function entry point has been emitted.
void DwarfDebug::beginFunctionImpl(const MachineFunction *MF) {
  CurFn = MF;

  auto *SP = MF->getFunction().getSubprogram();
  assert(LScopes.empty() || SP == LScopes.getCurrentFunctionScope()->getScopeNode());
  if (SP->getUnit()->getEmissionKind() == DICompileUnit::NoDebug)
    return;

  DwarfCompileUnit &CU = getOrCreateDwarfCompileUnit(SP->getUnit());

  Asm->OutStreamer->getContext().setDwarfCompileUnitID(
      getDwarfCompileUnitIDForLineTable(CU));

  // Record beginning of function.
  PrologEndLoc = emitInitialLocDirective(
      *MF, Asm->OutStreamer->getContext().getDwarfCompileUnitID());
}

unsigned
DwarfDebug::getDwarfCompileUnitIDForLineTable(const DwarfCompileUnit &CU) {
  // Set DwarfDwarfCompileUnitID in MCContext to the Compile Unit this function
  // belongs to so that we add to the correct per-cu line table in the
  // non-asm case.
  if (Asm->OutStreamer->hasRawTextSupport())
    // Use a single line table if we are generating assembly.
    return 0;
  else
    return CU.getUniqueID();
}

void DwarfDebug::terminateLineTable(const DwarfCompileUnit *CU) {
  const auto &CURanges = CU->getRanges();
  auto &LineTable = Asm->OutStreamer->getContext().getMCDwarfLineTable(
      getDwarfCompileUnitIDForLineTable(*CU));
  // Add the last range label for the given CU.
  LineTable.getMCLineSections().addEndEntry(
      const_cast<MCSymbol *>(CURanges.back().End));
}

void DwarfDebug::skippedNonDebugFunction() {
  // If we don't have a subprogram for this function then there will be a hole
  // in the range information. Keep note of this by setting the previously used
  // section to nullptr.
  // Terminate the pending line table.
  if (PrevCU)
    terminateLineTable(PrevCU);
  PrevCU = nullptr;
  CurFn = nullptr;
}

// Gather and emit post-function debug information.
void DwarfDebug::endFunctionImpl(const MachineFunction *MF) {
  const DISubprogram *SP = MF->getFunction().getSubprogram();

  assert(CurFn == MF &&
      "endFunction should be called with the same function as beginFunction");

  // Set DwarfDwarfCompileUnitID in MCContext to default value.
  Asm->OutStreamer->getContext().setDwarfCompileUnitID(0);

  LexicalScope *FnScope = LScopes.getCurrentFunctionScope();
  assert(!FnScope || SP == FnScope->getScopeNode());
  DwarfCompileUnit &TheCU = getOrCreateDwarfCompileUnit(SP->getUnit());
  if (TheCU.getCUNode()->isDebugDirectivesOnly()) {
    PrevLabel = nullptr;
    CurFn = nullptr;
    return;
  }

  DenseSet<InlinedEntity> Processed;
  collectEntityInfo(TheCU, SP, Processed);

  // Add the range of this function to the list of ranges for the CU.
  // With basic block sections, add ranges for all basic block sections.
  for (const auto &R : Asm->MBBSectionRanges)
    TheCU.addRange({R.second.BeginLabel, R.second.EndLabel});

  // Under -gmlt, skip building the subprogram if there are no inlined
  // subroutines inside it. But with -fdebug-info-for-profiling, the subprogram
  // is still needed as we need its source location.
  if (!TheCU.getCUNode()->getDebugInfoForProfiling() &&
      TheCU.getCUNode()->getEmissionKind() == DICompileUnit::LineTablesOnly &&
      LScopes.getAbstractScopesList().empty() && !IsDarwin) {
    for (const auto &R : Asm->MBBSectionRanges)
      addArangeLabel(SymbolCU(&TheCU, R.second.BeginLabel));

    assert(InfoHolder.getScopeVariables().empty());
    PrevLabel = nullptr;
    CurFn = nullptr;
    return;
  }

#ifndef NDEBUG
  size_t NumAbstractSubprograms = LScopes.getAbstractScopesList().size();
#endif
  for (LexicalScope *AScope : LScopes.getAbstractScopesList()) {
    const auto *SP = cast<DISubprogram>(AScope->getScopeNode());
    for (const DINode *DN : SP->getRetainedNodes()) {
      const auto *LS = getRetainedNodeScope(DN);
      // Ensure LexicalScope is created for the scope of this node.
      auto *LexS = LScopes.getOrCreateAbstractScope(LS);
      assert(LexS && "Expected the LexicalScope to be created.");
      if (isa<DILocalVariable>(DN) || isa<DILabel>(DN)) {
        // Collect info for variables/labels that were optimized out.
        if (!Processed.insert(InlinedEntity(DN, nullptr)).second ||
            TheCU.getExistingAbstractEntity(DN))
          continue;
        TheCU.createAbstractEntity(DN, LexS);
      } else {
        // Remember the node if this is a local declarations.
        LocalDeclsPerLS[LS].insert(DN);
      }
      assert(
          LScopes.getAbstractScopesList().size() == NumAbstractSubprograms &&
          "getOrCreateAbstractScope() inserted an abstract subprogram scope");
    }
    constructAbstractSubprogramScopeDIE(TheCU, AScope);
  }

  ProcessedSPNodes.insert(SP);
  DIE &ScopeDIE = TheCU.constructSubprogramScopeDIE(SP, FnScope);
  if (auto *SkelCU = TheCU.getSkeleton())
    if (!LScopes.getAbstractScopesList().empty() &&
        TheCU.getCUNode()->getSplitDebugInlining())
      SkelCU->constructSubprogramScopeDIE(SP, FnScope);

  // Construct call site entries.
  constructCallSiteEntryDIEs(*SP, TheCU, ScopeDIE, *MF);

  // Clear debug info
  // Ownership of DbgVariables is a bit subtle - ScopeVariables owns all the
  // DbgVariables except those that are also in AbstractVariables (since they
  // can be used cross-function)
  InfoHolder.getScopeVariables().clear();
  InfoHolder.getScopeLabels().clear();
  LocalDeclsPerLS.clear();
  PrevLabel = nullptr;
  CurFn = nullptr;
}

// Register a source line with debug info. Returns the  unique label that was
// emitted and which provides correspondence to the source line list.
void DwarfDebug::recordSourceLine(unsigned Line, unsigned Col, const MDNode *S,
                                  unsigned Flags) {
  ::recordSourceLine(*Asm, Line, Col, S, Flags,
                     Asm->OutStreamer->getContext().getDwarfCompileUnitID(),
                     getDwarfVersion(), getUnits());
}

//===----------------------------------------------------------------------===//
// Emit Methods
//===----------------------------------------------------------------------===//

// Emit the debug info section.
void DwarfDebug::emitDebugInfo() {
  DwarfFile &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;
  Holder.emitUnits(/* UseOffsets */ false);
}

// Emit the abbreviation section.
void DwarfDebug::emitAbbreviations() {
  DwarfFile &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;

  Holder.emitAbbrevs(Asm->getObjFileLowering().getDwarfAbbrevSection());
}

void DwarfDebug::emitStringOffsetsTableHeader() {
  DwarfFile &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;
  Holder.getStringPool().emitStringOffsetsTableHeader(
      *Asm, Asm->getObjFileLowering().getDwarfStrOffSection(),
      Holder.getStringOffsetsStartSym());
}

template <typename AccelTableT>
void DwarfDebug::emitAccel(AccelTableT &Accel, MCSection *Section,
                           StringRef TableName) {
  Asm->OutStreamer->switchSection(Section);

  // Emit the full data.
  emitAppleAccelTable(Asm, Accel, TableName, Section->getBeginSymbol());
}

void DwarfDebug::emitAccelDebugNames() {
  // Don't emit anything if we have no compilation units to index.
  if (getUnits().empty())
    return;

  emitDWARF5AccelTable(Asm, AccelDebugNames, *this, getUnits());
}

// Emit visible names into a hashed accelerator table section.
void DwarfDebug::emitAccelNames() {
  emitAccel(AccelNames, Asm->getObjFileLowering().getDwarfAccelNamesSection(),
            "Names");
}

// Emit objective C classes and categories into a hashed accelerator table
// section.
void DwarfDebug::emitAccelObjC() {
  emitAccel(AccelObjC, Asm->getObjFileLowering().getDwarfAccelObjCSection(),
            "ObjC");
}

// Emit namespace dies into a hashed accelerator table.
void DwarfDebug::emitAccelNamespaces() {
  emitAccel(AccelNamespace,
            Asm->getObjFileLowering().getDwarfAccelNamespaceSection(),
            "namespac");
}

// Emit type dies into a hashed accelerator table.
void DwarfDebug::emitAccelTypes() {
  emitAccel(AccelTypes, Asm->getObjFileLowering().getDwarfAccelTypesSection(),
            "types");
}

// Public name handling.
// The format for the various pubnames:
//
// dwarf pubnames - offset/name pairs where the offset is the offset into the CU
// for the DIE that is named.
//
// gnu pubnames - offset/index value/name tuples where the offset is the offset
// into the CU and the index value is computed according to the type of value
// for the DIE that is named.
//
// For type units the offset is the offset of the skeleton DIE. For split dwarf
// it's the offset within the debug_info/debug_types dwo section, however, the
// reference in the pubname header doesn't change.

/// computeIndexValue - Compute the gdb index value for the DIE and CU.
static dwarf::PubIndexEntryDescriptor computeIndexValue(DwarfUnit *CU,
                                                        const DIE *Die) {
  // Entities that ended up only in a Type Unit reference the CU instead (since
  // the pub entry has offsets within the CU there's no real offset that can be
  // provided anyway). As it happens all such entities (namespaces and types,
  // types only in C++ at that) are rendered as TYPE+EXTERNAL. If this turns out
  // not to be true it would be necessary to persist this information from the
  // point at which the entry is added to the index data structure - since by
  // the time the index is built from that, the original type/namespace DIE in a
  // type unit has already been destroyed so it can't be queried for properties
  // like tag, etc.
  if (Die->getTag() == dwarf::DW_TAG_compile_unit)
    return dwarf::PubIndexEntryDescriptor(dwarf::GIEK_TYPE,
                                          dwarf::GIEL_EXTERNAL);
  dwarf::GDBIndexEntryLinkage Linkage = dwarf::GIEL_STATIC;

  // We could have a specification DIE that has our most of our knowledge,
  // look for that now.
  if (DIEValue SpecVal = Die->findAttribute(dwarf::DW_AT_specification)) {
    DIE &SpecDIE = SpecVal.getDIEEntry().getEntry();
    if (SpecDIE.findAttribute(dwarf::DW_AT_external))
      Linkage = dwarf::GIEL_EXTERNAL;
  } else if (Die->findAttribute(dwarf::DW_AT_external))
    Linkage = dwarf::GIEL_EXTERNAL;

  switch (Die->getTag()) {
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_enumeration_type:
    return dwarf::PubIndexEntryDescriptor(
        dwarf::GIEK_TYPE,
        dwarf::isCPlusPlus((dwarf::SourceLanguage)CU->getLanguage())
            ? dwarf::GIEL_EXTERNAL
            : dwarf::GIEL_STATIC);
  case dwarf::DW_TAG_typedef:
  case dwarf::DW_TAG_base_type:
  case dwarf::DW_TAG_subrange_type:
  case dwarf::DW_TAG_template_alias:
    return dwarf::PubIndexEntryDescriptor(dwarf::GIEK_TYPE, dwarf::GIEL_STATIC);
  case dwarf::DW_TAG_namespace:
    return dwarf::GIEK_TYPE;
  case dwarf::DW_TAG_subprogram:
    return dwarf::PubIndexEntryDescriptor(dwarf::GIEK_FUNCTION, Linkage);
  case dwarf::DW_TAG_variable:
    return dwarf::PubIndexEntryDescriptor(dwarf::GIEK_VARIABLE, Linkage);
  case dwarf::DW_TAG_enumerator:
    return dwarf::PubIndexEntryDescriptor(dwarf::GIEK_VARIABLE,
                                          dwarf::GIEL_STATIC);
  default:
    return dwarf::GIEK_NONE;
  }
}

/// emitDebugPubSections - Emit visible names and types into debug pubnames and
/// pubtypes sections.
void DwarfDebug::emitDebugPubSections() {
  for (const auto &NU : CUMap) {
    DwarfCompileUnit *TheU = NU.second;
    if (!TheU->hasDwarfPubSections())
      continue;

    bool GnuStyle = TheU->getCUNode()->getNameTableKind() ==
                    DICompileUnit::DebugNameTableKind::GNU;

    Asm->OutStreamer->switchSection(
        GnuStyle ? Asm->getObjFileLowering().getDwarfGnuPubNamesSection()
                 : Asm->getObjFileLowering().getDwarfPubNamesSection());
    emitDebugPubSection(GnuStyle, "Names", TheU, TheU->getGlobalNames());

    Asm->OutStreamer->switchSection(
        GnuStyle ? Asm->getObjFileLowering().getDwarfGnuPubTypesSection()
                 : Asm->getObjFileLowering().getDwarfPubTypesSection());
    emitDebugPubSection(GnuStyle, "Types", TheU, TheU->getGlobalTypes());
  }
}

void DwarfDebug::emitSectionReference(const DwarfCompileUnit &CU) {
  if (useSectionsAsReferences())
    Asm->emitDwarfOffset(CU.getSection()->getBeginSymbol(),
                         CU.getDebugSectionOffset());
  else
    Asm->emitDwarfSymbolReference(CU.getLabelBegin());
}

void DwarfDebug::emitDebugPubSection(bool GnuStyle, StringRef Name,
                                     DwarfCompileUnit *TheU,
                                     const StringMap<const DIE *> &Globals) {
  if (auto *Skeleton = TheU->getSkeleton())
    TheU = Skeleton;

  // Emit the header.
  MCSymbol *EndLabel = Asm->emitDwarfUnitLength(
      "pub" + Name, "Length of Public " + Name + " Info");

  Asm->OutStreamer->AddComment("DWARF Version");
  Asm->emitInt16(dwarf::DW_PUBNAMES_VERSION);

  Asm->OutStreamer->AddComment("Offset of Compilation Unit Info");
  emitSectionReference(*TheU);

  Asm->OutStreamer->AddComment("Compilation Unit Length");
  Asm->emitDwarfLengthOrOffset(TheU->getLength());

  // Emit the pubnames for this compilation unit.
  SmallVector<std::pair<StringRef, const DIE *>, 0> Vec;
  for (const auto &GI : Globals)
    Vec.emplace_back(GI.first(), GI.second);
  llvm::sort(Vec, [](auto &A, auto &B) {
    return A.second->getOffset() < B.second->getOffset();
  });
  for (const auto &[Name, Entity] : Vec) {
    Asm->OutStreamer->AddComment("DIE offset");
    Asm->emitDwarfLengthOrOffset(Entity->getOffset());

    if (GnuStyle) {
      dwarf::PubIndexEntryDescriptor Desc = computeIndexValue(TheU, Entity);
      Asm->OutStreamer->AddComment(
          Twine("Attributes: ") + dwarf::GDBIndexEntryKindString(Desc.Kind) +
          ", " + dwarf::GDBIndexEntryLinkageString(Desc.Linkage));
      Asm->emitInt8(Desc.toBits());
    }

    Asm->OutStreamer->AddComment("External Name");
    Asm->OutStreamer->emitBytes(StringRef(Name.data(), Name.size() + 1));
  }

  Asm->OutStreamer->AddComment("End Mark");
  Asm->emitDwarfLengthOrOffset(0);
  Asm->OutStreamer->emitLabel(EndLabel);
}

/// Emit null-terminated strings into a debug str section.
void DwarfDebug::emitDebugStr() {
  MCSection *StringOffsetsSection = nullptr;
  if (useSegmentedStringOffsetsTable()) {
    emitStringOffsetsTableHeader();
    StringOffsetsSection = Asm->getObjFileLowering().getDwarfStrOffSection();
  }
  DwarfFile &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;
  Holder.emitStrings(Asm->getObjFileLowering().getDwarfStrSection(),
                     StringOffsetsSection, /* UseRelativeOffsets = */ true);
}

void DwarfDebug::emitDebugLocEntry(ByteStreamer &Streamer,
                                   const DebugLocStream::Entry &Entry,
                                   const DwarfCompileUnit *CU) {
  auto &&Comments = DebugLocs.getComments(Entry);
  auto Comment = Comments.begin();
  auto End = Comments.end();

  // The expressions are inserted into a byte stream rather early (see
  // DwarfExpression::addExpression) so for those ops (e.g. DW_OP_convert) that
  // need to reference a base_type DIE the offset of that DIE is not yet known.
  // To deal with this we instead insert a placeholder early and then extract
  // it here and replace it with the real reference.
  unsigned PtrSize = Asm->MAI->getCodePointerSize();
  DWARFDataExtractor Data(StringRef(DebugLocs.getBytes(Entry).data(),
                                    DebugLocs.getBytes(Entry).size()),
                          Asm->getDataLayout().isLittleEndian(), PtrSize);
  DWARFExpression Expr(Data, PtrSize, Asm->OutContext.getDwarfFormat());

  using Encoding = DWARFExpression::Operation::Encoding;
  uint64_t Offset = 0;
  for (const auto &Op : Expr) {
    assert(Op.getCode() != dwarf::DW_OP_const_type &&
           "3 operand ops not yet supported");
    assert(!Op.getSubCode() && "SubOps not yet supported");
    Streamer.emitInt8(Op.getCode(), Comment != End ? *(Comment++) : "");
    Offset++;
    for (unsigned I = 0; I < Op.getDescription().Op.size(); ++I) {
      if (Op.getDescription().Op[I] == Encoding::BaseTypeRef) {
        unsigned Length =
          Streamer.emitDIERef(*CU->ExprRefedBaseTypes[Op.getRawOperand(I)].Die);
        // Make sure comments stay aligned.
        for (unsigned J = 0; J < Length; ++J)
          if (Comment != End)
            Comment++;
      } else {
        for (uint64_t J = Offset; J < Op.getOperandEndOffset(I); ++J)
          Streamer.emitInt8(Data.getData()[J], Comment != End ? *(Comment++) : "");
      }
      Offset = Op.getOperandEndOffset(I);
    }
    assert(Offset == Op.getEndOffset());
  }
}

void DwarfDebug::emitDebugLocValue(const AsmPrinter &AP, const DIBasicType *BT,
                                   const DbgValueLoc &Value,
                                   DwarfExpression &DwarfExpr) {
  auto *DIExpr = Value.getExpression();
  DIExpressionCursor ExprCursor(DIExpr);
  DwarfExpr.addFragmentOffset(DIExpr);

  // If the DIExpr is an Entry Value, we want to follow the same code path
  // regardless of whether the DBG_VALUE is variadic or not.
  if (DIExpr && DIExpr->isEntryValue()) {
    // Entry values can only be a single register with no additional DIExpr,
    // so just add it directly.
    assert(Value.getLocEntries().size() == 1);
    assert(Value.getLocEntries()[0].isLocation());
    MachineLocation Location = Value.getLocEntries()[0].getLoc();
    DwarfExpr.setLocation(Location, DIExpr);

    DwarfExpr.beginEntryValueExpression(ExprCursor);

    const TargetRegisterInfo &TRI = *AP.MF->getSubtarget().getRegisterInfo();
    if (!DwarfExpr.addMachineRegExpression(TRI, ExprCursor, Location.getReg()))
      return;
    return DwarfExpr.addExpression(std::move(ExprCursor));
  }

  // Regular entry.
  auto EmitValueLocEntry = [&DwarfExpr, &BT,
                            &AP](const DbgValueLocEntry &Entry,
                                 DIExpressionCursor &Cursor) -> bool {
    if (Entry.isInt()) {
      if (BT && (BT->getEncoding() == dwarf::DW_ATE_signed ||
                 BT->getEncoding() == dwarf::DW_ATE_signed_char))
        DwarfExpr.addSignedConstant(Entry.getInt());
      else
        DwarfExpr.addUnsignedConstant(Entry.getInt());
    } else if (Entry.isLocation()) {
      MachineLocation Location = Entry.getLoc();
      if (Location.isIndirect())
        DwarfExpr.setMemoryLocationKind();

      const TargetRegisterInfo &TRI = *AP.MF->getSubtarget().getRegisterInfo();
      if (!DwarfExpr.addMachineRegExpression(TRI, Cursor, Location.getReg()))
        return false;
    } else if (Entry.isTargetIndexLocation()) {
      TargetIndexLocation Loc = Entry.getTargetIndexLocation();
      // TODO TargetIndexLocation is a target-independent. Currently only the
      // WebAssembly-specific encoding is supported.
      assert(AP.TM.getTargetTriple().isWasm());
      DwarfExpr.addWasmLocation(Loc.Index, static_cast<uint64_t>(Loc.Offset));
    } else if (Entry.isConstantFP()) {
      if (AP.getDwarfVersion() >= 4 && !AP.getDwarfDebug()->tuneForSCE() &&
          !Cursor) {
        DwarfExpr.addConstantFP(Entry.getConstantFP()->getValueAPF(), AP);
      } else if (Entry.getConstantFP()
                     ->getValueAPF()
                     .bitcastToAPInt()
                     .getBitWidth() <= 64 /*bits*/) {
        DwarfExpr.addUnsignedConstant(
            Entry.getConstantFP()->getValueAPF().bitcastToAPInt());
      } else {
        LLVM_DEBUG(
            dbgs() << "Skipped DwarfExpression creation for ConstantFP of size"
                   << Entry.getConstantFP()
                          ->getValueAPF()
                          .bitcastToAPInt()
                          .getBitWidth()
                   << " bits\n");
        return false;
      }
    }
    return true;
  };

  if (!Value.isVariadic()) {
    if (!EmitValueLocEntry(Value.getLocEntries()[0], ExprCursor))
      return;
    DwarfExpr.addExpression(std::move(ExprCursor));
    return;
  }

  // If any of the location entries are registers with the value 0, then the
  // location is undefined.
  if (any_of(Value.getLocEntries(), [](const DbgValueLocEntry &Entry) {
        return Entry.isLocation() && !Entry.getLoc().getReg();
      }))
    return;

  DwarfExpr.addExpression(
      std::move(ExprCursor),
      [EmitValueLocEntry, &Value](unsigned Idx,
                                  DIExpressionCursor &Cursor) -> bool {
        return EmitValueLocEntry(Value.getLocEntries()[Idx], Cursor);
      });
}

void DebugLocEntry::finalize(const AsmPrinter &AP,
                             DebugLocStream::ListBuilder &List,
                             const DIBasicType *BT,
                             DwarfCompileUnit &TheCU) {
  assert(!Values.empty() &&
         "location list entries without values are redundant");
  assert(Begin != End && "unexpected location list entry with empty range");
  DebugLocStream::EntryBuilder Entry(List, Begin, End);
  BufferByteStreamer Streamer = Entry.getStreamer();
  DebugLocDwarfExpression DwarfExpr(AP.getDwarfVersion(), Streamer, TheCU);
  const DbgValueLoc &Value = Values[0];
  if (Value.isFragment()) {
    // Emit all fragments that belong to the same variable and range.
    assert(llvm::all_of(Values, [](DbgValueLoc P) {
          return P.isFragment();
        }) && "all values are expected to be fragments");
    assert(llvm::is_sorted(Values) && "fragments are expected to be sorted");

    for (const auto &Fragment : Values)
      DwarfDebug::emitDebugLocValue(AP, BT, Fragment, DwarfExpr);

  } else {
    assert(Values.size() == 1 && "only fragments may have >1 value");
    DwarfDebug::emitDebugLocValue(AP, BT, Value, DwarfExpr);
  }
  DwarfExpr.finalize();
  if (DwarfExpr.TagOffset)
    List.setTagOffset(*DwarfExpr.TagOffset);
}

void DwarfDebug::emitDebugLocEntryLocation(const DebugLocStream::Entry &Entry,
                                           const DwarfCompileUnit *CU) {
  // Emit the size.
  Asm->OutStreamer->AddComment("Loc expr size");
  if (getDwarfVersion() >= 5)
    Asm->emitULEB128(DebugLocs.getBytes(Entry).size());
  else if (DebugLocs.getBytes(Entry).size() <= std::numeric_limits<uint16_t>::max())
    Asm->emitInt16(DebugLocs.getBytes(Entry).size());
  else {
    // The entry is too big to fit into 16 bit, drop it as there is nothing we
    // can do.
    Asm->emitInt16(0);
    return;
  }
  // Emit the entry.
  APByteStreamer Streamer(*Asm);
  emitDebugLocEntry(Streamer, Entry, CU);
}

// Emit the header of a DWARF 5 range list table list table. Returns the symbol
// that designates the end of the table for the caller to emit when the table is
// complete.
static MCSymbol *emitRnglistsTableHeader(AsmPrinter *Asm,
                                         const DwarfFile &Holder) {
  MCSymbol *TableEnd = mcdwarf::emitListsTableHeaderStart(*Asm->OutStreamer);

  Asm->OutStreamer->AddComment("Offset entry count");
  Asm->emitInt32(Holder.getRangeLists().size());
  Asm->OutStreamer->emitLabel(Holder.getRnglistsTableBaseSym());

  for (const RangeSpanList &List : Holder.getRangeLists())
    Asm->emitLabelDifference(List.Label, Holder.getRnglistsTableBaseSym(),
                             Asm->getDwarfOffsetByteSize());

  return TableEnd;
}

// Emit the header of a DWARF 5 locations list table. Returns the symbol that
// designates the end of the table for the caller to emit when the table is
// complete.
static MCSymbol *emitLoclistsTableHeader(AsmPrinter *Asm,
                                         const DwarfDebug &DD) {
  MCSymbol *TableEnd = mcdwarf::emitListsTableHeaderStart(*Asm->OutStreamer);

  const auto &DebugLocs = DD.getDebugLocs();

  Asm->OutStreamer->AddComment("Offset entry count");
  Asm->emitInt32(DebugLocs.getLists().size());
  Asm->OutStreamer->emitLabel(DebugLocs.getSym());

  for (const auto &List : DebugLocs.getLists())
    Asm->emitLabelDifference(List.Label, DebugLocs.getSym(),
                             Asm->getDwarfOffsetByteSize());

  return TableEnd;
}

template <typename Ranges, typename PayloadEmitter>
static void emitRangeList(
    DwarfDebug &DD, AsmPrinter *Asm, MCSymbol *Sym, const Ranges &R,
    const DwarfCompileUnit &CU, unsigned BaseAddressx, unsigned OffsetPair,
    unsigned StartxLength, unsigned EndOfList,
    StringRef (*StringifyEnum)(unsigned),
    bool ShouldUseBaseAddress,
    PayloadEmitter EmitPayload) {

  auto Size = Asm->MAI->getCodePointerSize();
  bool UseDwarf5 = DD.getDwarfVersion() >= 5;

  // Emit our symbol so we can find the beginning of the range.
  Asm->OutStreamer->emitLabel(Sym);

  // Gather all the ranges that apply to the same section so they can share
  // a base address entry.
  MapVector<const MCSection *, std::vector<decltype(&*R.begin())>> SectionRanges;

  for (const auto &Range : R)
    SectionRanges[&Range.Begin->getSection()].push_back(&Range);

  const MCSymbol *CUBase = CU.getBaseAddress();
  bool BaseIsSet = false;
  for (const auto &P : SectionRanges) {
    auto *Base = CUBase;
    if (!Base && ShouldUseBaseAddress) {
      const MCSymbol *Begin = P.second.front()->Begin;
      const MCSymbol *NewBase = DD.getSectionLabel(&Begin->getSection());
      if (!UseDwarf5) {
        Base = NewBase;
        BaseIsSet = true;
        Asm->OutStreamer->emitIntValue(-1, Size);
        Asm->OutStreamer->AddComment("  base address");
        Asm->OutStreamer->emitSymbolValue(Base, Size);
      } else if (NewBase != Begin || P.second.size() > 1) {
        // Only use a base address if
        //  * the existing pool address doesn't match (NewBase != Begin)
        //  * or, there's more than one entry to share the base address
        Base = NewBase;
        BaseIsSet = true;
        Asm->OutStreamer->AddComment(StringifyEnum(BaseAddressx));
        Asm->emitInt8(BaseAddressx);
        Asm->OutStreamer->AddComment("  base address index");
        Asm->emitULEB128(DD.getAddressPool().getIndex(Base));
      }
    } else if (BaseIsSet && !UseDwarf5) {
      BaseIsSet = false;
      assert(!Base);
      Asm->OutStreamer->emitIntValue(-1, Size);
      Asm->OutStreamer->emitIntValue(0, Size);
    }

    for (const auto *RS : P.second) {
      const MCSymbol *Begin = RS->Begin;
      const MCSymbol *End = RS->End;
      assert(Begin && "Range without a begin symbol?");
      assert(End && "Range without an end symbol?");
      if (Base) {
        if (UseDwarf5) {
          // Emit offset_pair when we have a base.
          Asm->OutStreamer->AddComment(StringifyEnum(OffsetPair));
          Asm->emitInt8(OffsetPair);
          Asm->OutStreamer->AddComment("  starting offset");
          Asm->emitLabelDifferenceAsULEB128(Begin, Base);
          Asm->OutStreamer->AddComment("  ending offset");
          Asm->emitLabelDifferenceAsULEB128(End, Base);
        } else {
          Asm->emitLabelDifference(Begin, Base, Size);
          Asm->emitLabelDifference(End, Base, Size);
        }
      } else if (UseDwarf5) {
        Asm->OutStreamer->AddComment(StringifyEnum(StartxLength));
        Asm->emitInt8(StartxLength);
        Asm->OutStreamer->AddComment("  start index");
        Asm->emitULEB128(DD.getAddressPool().getIndex(Begin));
        Asm->OutStreamer->AddComment("  length");
        Asm->emitLabelDifferenceAsULEB128(End, Begin);
      } else {
        Asm->OutStreamer->emitSymbolValue(Begin, Size);
        Asm->OutStreamer->emitSymbolValue(End, Size);
      }
      EmitPayload(*RS);
    }
  }

  if (UseDwarf5) {
    Asm->OutStreamer->AddComment(StringifyEnum(EndOfList));
    Asm->emitInt8(EndOfList);
  } else {
    // Terminate the list with two 0 values.
    Asm->OutStreamer->emitIntValue(0, Size);
    Asm->OutStreamer->emitIntValue(0, Size);
  }
}

// Handles emission of both debug_loclist / debug_loclist.dwo
static void emitLocList(DwarfDebug &DD, AsmPrinter *Asm, const DebugLocStream::List &List) {
  emitRangeList(DD, Asm, List.Label, DD.getDebugLocs().getEntries(List),
                *List.CU, dwarf::DW_LLE_base_addressx,
                dwarf::DW_LLE_offset_pair, dwarf::DW_LLE_startx_length,
                dwarf::DW_LLE_end_of_list, llvm::dwarf::LocListEncodingString,
                /* ShouldUseBaseAddress */ true,
                [&](const DebugLocStream::Entry &E) {
                  DD.emitDebugLocEntryLocation(E, List.CU);
                });
}

void DwarfDebug::emitDebugLocImpl(MCSection *Sec) {
  if (DebugLocs.getLists().empty())
    return;

  Asm->OutStreamer->switchSection(Sec);

  MCSymbol *TableEnd = nullptr;
  if (getDwarfVersion() >= 5)
    TableEnd = emitLoclistsTableHeader(Asm, *this);

  for (const auto &List : DebugLocs.getLists())
    emitLocList(*this, Asm, List);

  if (TableEnd)
    Asm->OutStreamer->emitLabel(TableEnd);
}

// Emit locations into the .debug_loc/.debug_loclists section.
void DwarfDebug::emitDebugLoc() {
  emitDebugLocImpl(
      getDwarfVersion() >= 5
          ? Asm->getObjFileLowering().getDwarfLoclistsSection()
          : Asm->getObjFileLowering().getDwarfLocSection());
}

// Emit locations into the .debug_loc.dwo/.debug_loclists.dwo section.
void DwarfDebug::emitDebugLocDWO() {
  if (getDwarfVersion() >= 5) {
    emitDebugLocImpl(
        Asm->getObjFileLowering().getDwarfLoclistsDWOSection());

    return;
  }

  for (const auto &List : DebugLocs.getLists()) {
    Asm->OutStreamer->switchSection(
        Asm->getObjFileLowering().getDwarfLocDWOSection());
    Asm->OutStreamer->emitLabel(List.Label);

    for (const auto &Entry : DebugLocs.getEntries(List)) {
      // GDB only supports startx_length in pre-standard split-DWARF.
      // (in v5 standard loclists, it currently* /only/ supports base_address +
      // offset_pair, so the implementations can't really share much since they
      // need to use different representations)
      // * as of October 2018, at least
      //
      // In v5 (see emitLocList), this uses SectionLabels to reuse existing
      // addresses in the address pool to minimize object size/relocations.
      Asm->emitInt8(dwarf::DW_LLE_startx_length);
      unsigned idx = AddrPool.getIndex(Entry.Begin);
      Asm->emitULEB128(idx);
      // Also the pre-standard encoding is slightly different, emitting this as
      // an address-length entry here, but its a ULEB128 in DWARFv5 loclists.
      Asm->emitLabelDifference(Entry.End, Entry.Begin, 4);
      emitDebugLocEntryLocation(Entry, List.CU);
    }
    Asm->emitInt8(dwarf::DW_LLE_end_of_list);
  }
}

struct ArangeSpan {
  const MCSymbol *Start, *End;
};

// Emit a debug aranges section, containing a CU lookup for any
// address we can tie back to a CU.
void DwarfDebug::emitDebugARanges() {
  if (ArangeLabels.empty())
    return;

  // Provides a unique id per text section.
  MapVector<MCSection *, SmallVector<SymbolCU, 8>> SectionMap;

  // Filter labels by section.
  for (const SymbolCU &SCU : ArangeLabels) {
    if (SCU.Sym->isInSection()) {
      // Make a note of this symbol and it's section.
      MCSection *Section = &SCU.Sym->getSection();
      SectionMap[Section].push_back(SCU);
    } else {
      // Some symbols (e.g. common/bss on mach-o) can have no section but still
      // appear in the output. This sucks as we rely on sections to build
      // arange spans. We can do it without, but it's icky.
      SectionMap[nullptr].push_back(SCU);
    }
  }

  DenseMap<DwarfCompileUnit *, std::vector<ArangeSpan>> Spans;

  for (auto &I : SectionMap) {
    MCSection *Section = I.first;
    SmallVector<SymbolCU, 8> &List = I.second;
    assert(!List.empty());

    // If we have no section (e.g. common), just write out
    // individual spans for each symbol.
    if (!Section) {
      for (const SymbolCU &Cur : List) {
        ArangeSpan Span;
        Span.Start = Cur.Sym;
        Span.End = nullptr;
        assert(Cur.CU);
        Spans[Cur.CU].push_back(Span);
      }
      continue;
    }

    // Insert a final terminator.
    List.push_back(SymbolCU(nullptr, Asm->OutStreamer->endSection(Section)));

    // Build spans between each label.
    const MCSymbol *StartSym = List[0].Sym;
    for (size_t n = 1, e = List.size(); n < e; n++) {
      const SymbolCU &Prev = List[n - 1];
      const SymbolCU &Cur = List[n];

      // Try and build the longest span we can within the same CU.
      if (Cur.CU != Prev.CU) {
        ArangeSpan Span;
        Span.Start = StartSym;
        Span.End = Cur.Sym;
        assert(Prev.CU);
        Spans[Prev.CU].push_back(Span);
        StartSym = Cur.Sym;
      }
    }
  }

  // Start the dwarf aranges section.
  Asm->OutStreamer->switchSection(
      Asm->getObjFileLowering().getDwarfARangesSection());

  unsigned PtrSize = Asm->MAI->getCodePointerSize();

  // Build a list of CUs used.
  std::vector<DwarfCompileUnit *> CUs;
  for (const auto &it : Spans) {
    DwarfCompileUnit *CU = it.first;
    CUs.push_back(CU);
  }

  // Sort the CU list (again, to ensure consistent output order).
  llvm::sort(CUs, [](const DwarfCompileUnit *A, const DwarfCompileUnit *B) {
    return A->getUniqueID() < B->getUniqueID();
  });

  // Emit an arange table for each CU we used.
  for (DwarfCompileUnit *CU : CUs) {
    std::vector<ArangeSpan> &List = Spans[CU];

    // Describe the skeleton CU's offset and length, not the dwo file's.
    if (auto *Skel = CU->getSkeleton())
      CU = Skel;

    // Emit size of content not including length itself.
    unsigned ContentSize =
        sizeof(int16_t) +               // DWARF ARange version number
        Asm->getDwarfOffsetByteSize() + // Offset of CU in the .debug_info
                                        // section
        sizeof(int8_t) +                // Pointer Size (in bytes)
        sizeof(int8_t);                 // Segment Size (in bytes)

    unsigned TupleSize = PtrSize * 2;

    // 7.20 in the Dwarf specs requires the table to be aligned to a tuple.
    unsigned Padding = offsetToAlignment(
        Asm->getUnitLengthFieldByteSize() + ContentSize, Align(TupleSize));

    ContentSize += Padding;
    ContentSize += (List.size() + 1) * TupleSize;

    // For each compile unit, write the list of spans it covers.
    Asm->emitDwarfUnitLength(ContentSize, "Length of ARange Set");
    Asm->OutStreamer->AddComment("DWARF Arange version number");
    Asm->emitInt16(dwarf::DW_ARANGES_VERSION);
    Asm->OutStreamer->AddComment("Offset Into Debug Info Section");
    emitSectionReference(*CU);
    Asm->OutStreamer->AddComment("Address Size (in bytes)");
    Asm->emitInt8(PtrSize);
    Asm->OutStreamer->AddComment("Segment Size (in bytes)");
    Asm->emitInt8(0);

    Asm->OutStreamer->emitFill(Padding, 0xff);

    for (const ArangeSpan &Span : List) {
      Asm->emitLabelReference(Span.Start, PtrSize);

      // Calculate the size as being from the span start to its end.
      //
      // If the size is zero, then round it up to one byte. The DWARF
      // specification requires that entries in this table have nonzero
      // lengths.
      auto SizeRef = SymSize.find(Span.Start);
      if ((SizeRef == SymSize.end() || SizeRef->second != 0) && Span.End) {
        Asm->emitLabelDifference(Span.End, Span.Start, PtrSize);
      } else {
        // For symbols without an end marker (e.g. common), we
        // write a single arange entry containing just that one symbol.
        uint64_t Size;
        if (SizeRef == SymSize.end() || SizeRef->second == 0)
          Size = 1;
        else
          Size = SizeRef->second;

        Asm->OutStreamer->emitIntValue(Size, PtrSize);
      }
    }

    Asm->OutStreamer->AddComment("ARange terminator");
    Asm->OutStreamer->emitIntValue(0, PtrSize);
    Asm->OutStreamer->emitIntValue(0, PtrSize);
  }
}

/// Emit a single range list. We handle both DWARF v5 and earlier.
static void emitRangeList(DwarfDebug &DD, AsmPrinter *Asm,
                          const RangeSpanList &List) {
  emitRangeList(DD, Asm, List.Label, List.Ranges, *List.CU,
                dwarf::DW_RLE_base_addressx, dwarf::DW_RLE_offset_pair,
                dwarf::DW_RLE_startx_length, dwarf::DW_RLE_end_of_list,
                llvm::dwarf::RangeListEncodingString,
                List.CU->getCUNode()->getRangesBaseAddress() ||
                    DD.getDwarfVersion() >= 5,
                [](auto) {});
}

void DwarfDebug::emitDebugRangesImpl(const DwarfFile &Holder, MCSection *Section) {
  if (Holder.getRangeLists().empty())
    return;

  assert(useRangesSection());
  assert(!CUMap.empty());
  assert(llvm::any_of(CUMap, [](const decltype(CUMap)::value_type &Pair) {
    return !Pair.second->getCUNode()->isDebugDirectivesOnly();
  }));

  Asm->OutStreamer->switchSection(Section);

  MCSymbol *TableEnd = nullptr;
  if (getDwarfVersion() >= 5)
    TableEnd = emitRnglistsTableHeader(Asm, Holder);

  for (const RangeSpanList &List : Holder.getRangeLists())
    emitRangeList(*this, Asm, List);

  if (TableEnd)
    Asm->OutStreamer->emitLabel(TableEnd);
}

/// Emit address ranges into the .debug_ranges section or into the DWARF v5
/// .debug_rnglists section.
void DwarfDebug::emitDebugRanges() {
  const auto &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;

  emitDebugRangesImpl(Holder,
                      getDwarfVersion() >= 5
                          ? Asm->getObjFileLowering().getDwarfRnglistsSection()
                          : Asm->getObjFileLowering().getDwarfRangesSection());
}

void DwarfDebug::emitDebugRangesDWO() {
  emitDebugRangesImpl(InfoHolder,
                      Asm->getObjFileLowering().getDwarfRnglistsDWOSection());
}

/// Emit the header of a DWARF 5 macro section, or the GNU extension for
/// DWARF 4.
static void emitMacroHeader(AsmPrinter *Asm, const DwarfDebug &DD,
                            const DwarfCompileUnit &CU, uint16_t DwarfVersion) {
  enum HeaderFlagMask {
#define HANDLE_MACRO_FLAG(ID, NAME) MACRO_FLAG_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  };
  Asm->OutStreamer->AddComment("Macro information version");
  Asm->emitInt16(DwarfVersion >= 5 ? DwarfVersion : 4);
  // We emit the line offset flag unconditionally here, since line offset should
  // be mostly present.
  if (Asm->isDwarf64()) {
    Asm->OutStreamer->AddComment("Flags: 64 bit, debug_line_offset present");
    Asm->emitInt8(MACRO_FLAG_OFFSET_SIZE | MACRO_FLAG_DEBUG_LINE_OFFSET);
  } else {
    Asm->OutStreamer->AddComment("Flags: 32 bit, debug_line_offset present");
    Asm->emitInt8(MACRO_FLAG_DEBUG_LINE_OFFSET);
  }
  Asm->OutStreamer->AddComment("debug_line_offset");
  if (DD.useSplitDwarf())
    Asm->emitDwarfLengthOrOffset(0);
  else
    Asm->emitDwarfSymbolReference(CU.getLineTableStartSym());
}

void DwarfDebug::handleMacroNodes(DIMacroNodeArray Nodes, DwarfCompileUnit &U) {
  for (auto *MN : Nodes) {
    if (auto *M = dyn_cast<DIMacro>(MN))
      emitMacro(*M);
    else if (auto *F = dyn_cast<DIMacroFile>(MN))
      emitMacroFile(*F, U);
    else
      llvm_unreachable("Unexpected DI type!");
  }
}

void DwarfDebug::emitMacro(DIMacro &M) {
  StringRef Name = M.getName();
  StringRef Value = M.getValue();

  // There should be one space between the macro name and the macro value in
  // define entries. In undef entries, only the macro name is emitted.
  std::string Str = Value.empty() ? Name.str() : (Name + " " + Value).str();

  if (UseDebugMacroSection) {
    if (getDwarfVersion() >= 5) {
      unsigned Type = M.getMacinfoType() == dwarf::DW_MACINFO_define
                          ? dwarf::DW_MACRO_define_strx
                          : dwarf::DW_MACRO_undef_strx;
      Asm->OutStreamer->AddComment(dwarf::MacroString(Type));
      Asm->emitULEB128(Type);
      Asm->OutStreamer->AddComment("Line Number");
      Asm->emitULEB128(M.getLine());
      Asm->OutStreamer->AddComment("Macro String");
      Asm->emitULEB128(
          InfoHolder.getStringPool().getIndexedEntry(*Asm, Str).getIndex());
    } else {
      unsigned Type = M.getMacinfoType() == dwarf::DW_MACINFO_define
                          ? dwarf::DW_MACRO_GNU_define_indirect
                          : dwarf::DW_MACRO_GNU_undef_indirect;
      Asm->OutStreamer->AddComment(dwarf::GnuMacroString(Type));
      Asm->emitULEB128(Type);
      Asm->OutStreamer->AddComment("Line Number");
      Asm->emitULEB128(M.getLine());
      Asm->OutStreamer->AddComment("Macro String");
      Asm->emitDwarfSymbolReference(
          InfoHolder.getStringPool().getEntry(*Asm, Str).getSymbol());
    }
  } else {
    Asm->OutStreamer->AddComment(dwarf::MacinfoString(M.getMacinfoType()));
    Asm->emitULEB128(M.getMacinfoType());
    Asm->OutStreamer->AddComment("Line Number");
    Asm->emitULEB128(M.getLine());
    Asm->OutStreamer->AddComment("Macro String");
    Asm->OutStreamer->emitBytes(Str);
    Asm->emitInt8('\0');
  }
}

void DwarfDebug::emitMacroFileImpl(
    DIMacroFile &MF, DwarfCompileUnit &U, unsigned StartFile, unsigned EndFile,
    StringRef (*MacroFormToString)(unsigned Form)) {

  Asm->OutStreamer->AddComment(MacroFormToString(StartFile));
  Asm->emitULEB128(StartFile);
  Asm->OutStreamer->AddComment("Line Number");
  Asm->emitULEB128(MF.getLine());
  Asm->OutStreamer->AddComment("File Number");
  DIFile &F = *MF.getFile();
  if (useSplitDwarf())
    Asm->emitULEB128(getDwoLineTable(U)->getFile(
        F.getDirectory(), F.getFilename(), getMD5AsBytes(&F),
        Asm->OutContext.getDwarfVersion(), F.getSource()));
  else
    Asm->emitULEB128(U.getOrCreateSourceID(&F));
  handleMacroNodes(MF.getElements(), U);
  Asm->OutStreamer->AddComment(MacroFormToString(EndFile));
  Asm->emitULEB128(EndFile);
}

void DwarfDebug::emitMacroFile(DIMacroFile &F, DwarfCompileUnit &U) {
  // DWARFv5 macro and DWARFv4 macinfo share some common encodings,
  // so for readibility/uniformity, We are explicitly emitting those.
  assert(F.getMacinfoType() == dwarf::DW_MACINFO_start_file);
  if (UseDebugMacroSection)
    emitMacroFileImpl(
        F, U, dwarf::DW_MACRO_start_file, dwarf::DW_MACRO_end_file,
        (getDwarfVersion() >= 5) ? dwarf::MacroString : dwarf::GnuMacroString);
  else
    emitMacroFileImpl(F, U, dwarf::DW_MACINFO_start_file,
                      dwarf::DW_MACINFO_end_file, dwarf::MacinfoString);
}

void DwarfDebug::emitDebugMacinfoImpl(MCSection *Section) {
  for (const auto &P : CUMap) {
    auto &TheCU = *P.second;
    auto *SkCU = TheCU.getSkeleton();
    DwarfCompileUnit &U = SkCU ? *SkCU : TheCU;
    auto *CUNode = cast<DICompileUnit>(P.first);
    DIMacroNodeArray Macros = CUNode->getMacros();
    if (Macros.empty())
      continue;
    Asm->OutStreamer->switchSection(Section);
    Asm->OutStreamer->emitLabel(U.getMacroLabelBegin());
    if (UseDebugMacroSection)
      emitMacroHeader(Asm, *this, U, getDwarfVersion());
    handleMacroNodes(Macros, U);
    Asm->OutStreamer->AddComment("End Of Macro List Mark");
    Asm->emitInt8(0);
  }
}

/// Emit macros into a debug macinfo/macro section.
void DwarfDebug::emitDebugMacinfo() {
  auto &ObjLower = Asm->getObjFileLowering();
  emitDebugMacinfoImpl(UseDebugMacroSection
                           ? ObjLower.getDwarfMacroSection()
                           : ObjLower.getDwarfMacinfoSection());
}

void DwarfDebug::emitDebugMacinfoDWO() {
  auto &ObjLower = Asm->getObjFileLowering();
  emitDebugMacinfoImpl(UseDebugMacroSection
                           ? ObjLower.getDwarfMacroDWOSection()
                           : ObjLower.getDwarfMacinfoDWOSection());
}

// DWARF5 Experimental Separate Dwarf emitters.

void DwarfDebug::initSkeletonUnit(const DwarfUnit &U, DIE &Die,
                                  std::unique_ptr<DwarfCompileUnit> NewU) {

  if (!CompilationDir.empty())
    NewU->addString(Die, dwarf::DW_AT_comp_dir, CompilationDir);
  addGnuPubAttributes(*NewU, Die);

  SkeletonHolder.addUnit(std::move(NewU));
}

DwarfCompileUnit &DwarfDebug::constructSkeletonCU(const DwarfCompileUnit &CU) {

  auto OwnedUnit = std::make_unique<DwarfCompileUnit>(
      CU.getUniqueID(), CU.getCUNode(), Asm, this, &SkeletonHolder,
      UnitKind::Skeleton);
  DwarfCompileUnit &NewCU = *OwnedUnit;
  NewCU.setSection(Asm->getObjFileLowering().getDwarfInfoSection());

  NewCU.initStmtList();

  if (useSegmentedStringOffsetsTable())
    NewCU.addStringOffsetsStart();

  initSkeletonUnit(CU, NewCU.getUnitDie(), std::move(OwnedUnit));

  return NewCU;
}

// Emit the .debug_info.dwo section for separated dwarf. This contains the
// compile units that would normally be in debug_info.
void DwarfDebug::emitDebugInfoDWO() {
  assert(useSplitDwarf() && "No split dwarf debug info?");
  // Don't emit relocations into the dwo file.
  InfoHolder.emitUnits(/* UseOffsets */ true);
}

// Emit the .debug_abbrev.dwo section for separated dwarf. This contains the
// abbreviations for the .debug_info.dwo section.
void DwarfDebug::emitDebugAbbrevDWO() {
  assert(useSplitDwarf() && "No split dwarf?");
  InfoHolder.emitAbbrevs(Asm->getObjFileLowering().getDwarfAbbrevDWOSection());
}

void DwarfDebug::emitDebugLineDWO() {
  assert(useSplitDwarf() && "No split dwarf?");
  SplitTypeUnitFileTable.Emit(
      *Asm->OutStreamer, MCDwarfLineTableParams(),
      Asm->getObjFileLowering().getDwarfLineDWOSection());
}

void DwarfDebug::emitStringOffsetsTableHeaderDWO() {
  assert(useSplitDwarf() && "No split dwarf?");
  InfoHolder.getStringPool().emitStringOffsetsTableHeader(
      *Asm, Asm->getObjFileLowering().getDwarfStrOffDWOSection(),
      InfoHolder.getStringOffsetsStartSym());
}

// Emit the .debug_str.dwo section for separated dwarf. This contains the
// string section and is identical in format to traditional .debug_str
// sections.
void DwarfDebug::emitDebugStrDWO() {
  if (useSegmentedStringOffsetsTable())
    emitStringOffsetsTableHeaderDWO();
  assert(useSplitDwarf() && "No split dwarf?");
  MCSection *OffSec = Asm->getObjFileLowering().getDwarfStrOffDWOSection();
  InfoHolder.emitStrings(Asm->getObjFileLowering().getDwarfStrDWOSection(),
                         OffSec, /* UseRelativeOffsets = */ false);
}

// Emit address pool.
void DwarfDebug::emitDebugAddr() {
  AddrPool.emit(*Asm, Asm->getObjFileLowering().getDwarfAddrSection());
}

MCDwarfDwoLineTable *DwarfDebug::getDwoLineTable(const DwarfCompileUnit &CU) {
  if (!useSplitDwarf())
    return nullptr;
  const DICompileUnit *DIUnit = CU.getCUNode();
  SplitTypeUnitFileTable.maybeSetRootFile(
      DIUnit->getDirectory(), DIUnit->getFilename(),
      getMD5AsBytes(DIUnit->getFile()), DIUnit->getSource());
  return &SplitTypeUnitFileTable;
}

uint64_t DwarfDebug::makeTypeSignature(StringRef Identifier) {
  MD5 Hash;
  Hash.update(Identifier);
  // ... take the least significant 8 bytes and return those. Our MD5
  // implementation always returns its results in little endian, so we actually
  // need the "high" word.
  MD5::MD5Result Result;
  Hash.final(Result);
  return Result.high();
}

void DwarfDebug::addDwarfTypeUnitType(DwarfCompileUnit &CU,
                                      StringRef Identifier, DIE &RefDie,
                                      const DICompositeType *CTy) {
  // Fast path if we're building some type units and one has already used the
  // address pool we know we're going to throw away all this work anyway, so
  // don't bother building dependent types.
  if (!TypeUnitsUnderConstruction.empty() && AddrPool.hasBeenUsed())
    return;

  auto Ins = TypeSignatures.insert(std::make_pair(CTy, 0));
  if (!Ins.second) {
    CU.addDIETypeSignature(RefDie, Ins.first->second);
    return;
  }

  setCurrentDWARF5AccelTable(DWARF5AccelTableKind::TU);
  bool TopLevelType = TypeUnitsUnderConstruction.empty();
  AddrPool.resetUsedFlag();

  auto OwnedUnit = std::make_unique<DwarfTypeUnit>(
      CU, Asm, this, &InfoHolder, NumTypeUnitsCreated++, getDwoLineTable(CU));
  DwarfTypeUnit &NewTU = *OwnedUnit;
  DIE &UnitDie = NewTU.getUnitDie();
  TypeUnitsUnderConstruction.emplace_back(std::move(OwnedUnit), CTy);

  NewTU.addUInt(UnitDie, dwarf::DW_AT_language, dwarf::DW_FORM_data2,
                CU.getLanguage());

  uint64_t Signature = makeTypeSignature(Identifier);
  NewTU.setTypeSignature(Signature);
  Ins.first->second = Signature;

  if (useSplitDwarf()) {
    // Although multiple type units can have the same signature, they are not
    // guranteed to be bit identical. When LLDB uses .debug_names it needs to
    // know from which CU a type unit came from. These two attrbutes help it to
    // figure that out.
    if (getDwarfVersion() >= 5) {
      if (!CompilationDir.empty())
        NewTU.addString(UnitDie, dwarf::DW_AT_comp_dir, CompilationDir);
      NewTU.addString(UnitDie, dwarf::DW_AT_dwo_name,
                      Asm->TM.Options.MCOptions.SplitDwarfFile);
    }
    MCSection *Section =
        getDwarfVersion() <= 4
            ? Asm->getObjFileLowering().getDwarfTypesDWOSection()
            : Asm->getObjFileLowering().getDwarfInfoDWOSection();
    NewTU.setSection(Section);
  } else {
    MCSection *Section =
        getDwarfVersion() <= 4
            ? Asm->getObjFileLowering().getDwarfTypesSection(Signature)
            : Asm->getObjFileLowering().getDwarfInfoSection(Signature);
    NewTU.setSection(Section);
    // Non-split type units reuse the compile unit's line table.
    CU.applyStmtList(UnitDie);
  }

  // Add DW_AT_str_offsets_base to the type unit DIE, but not for split type
  // units.
  if (useSegmentedStringOffsetsTable() && !useSplitDwarf())
    NewTU.addStringOffsetsStart();

  NewTU.setType(NewTU.createTypeDIE(CTy));

  if (TopLevelType) {
    auto TypeUnitsToAdd = std::move(TypeUnitsUnderConstruction);
    TypeUnitsUnderConstruction.clear();

    // Types referencing entries in the address table cannot be placed in type
    // units.
    if (AddrPool.hasBeenUsed()) {
      AccelTypeUnitsDebugNames.clear();
      // Remove all the types built while building this type.
      // This is pessimistic as some of these types might not be dependent on
      // the type that used an address.
      for (const auto &TU : TypeUnitsToAdd)
        TypeSignatures.erase(TU.second);

      // Construct this type in the CU directly.
      // This is inefficient because all the dependent types will be rebuilt
      // from scratch, including building them in type units, discovering that
      // they depend on addresses, throwing them out and rebuilding them.
      setCurrentDWARF5AccelTable(DWARF5AccelTableKind::CU);
      CU.constructTypeDIE(RefDie, cast<DICompositeType>(CTy));
      return;
    }

    // If the type wasn't dependent on fission addresses, finish adding the type
    // and all its dependent types.
    for (auto &TU : TypeUnitsToAdd) {
      InfoHolder.computeSizeAndOffsetsForUnit(TU.first.get());
      InfoHolder.emitUnit(TU.first.get(), useSplitDwarf());
      if (getDwarfVersion() >= 5 &&
          getAccelTableKind() == AccelTableKind::Dwarf) {
        if (useSplitDwarf())
          AccelDebugNames.addTypeUnitSignature(*TU.first);
        else
          AccelDebugNames.addTypeUnitSymbol(*TU.first);
      }
    }
    AccelTypeUnitsDebugNames.convertDieToOffset();
    AccelDebugNames.addTypeEntries(AccelTypeUnitsDebugNames);
    AccelTypeUnitsDebugNames.clear();
    setCurrentDWARF5AccelTable(DWARF5AccelTableKind::CU);
  }
  CU.addDIETypeSignature(RefDie, Signature);
}

// Add the Name along with its companion DIE to the appropriate accelerator
// table (for AccelTableKind::Dwarf it's always AccelDebugNames, for
// AccelTableKind::Apple, we use the table we got as an argument). If
// accelerator tables are disabled, this function does nothing.
template <typename DataT>
void DwarfDebug::addAccelNameImpl(
    const DwarfUnit &Unit,
    const DICompileUnit::DebugNameTableKind NameTableKind,
    AccelTable<DataT> &AppleAccel, StringRef Name, const DIE &Die) {
  if (getAccelTableKind() == AccelTableKind::None ||
      Unit.getUnitDie().getTag() == dwarf::DW_TAG_skeleton_unit || Name.empty())
    return;

  if (getAccelTableKind() != AccelTableKind::Apple &&
      NameTableKind != DICompileUnit::DebugNameTableKind::Apple &&
      NameTableKind != DICompileUnit::DebugNameTableKind::Default)
    return;

  DwarfFile &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;
  DwarfStringPoolEntryRef Ref = Holder.getStringPool().getEntry(*Asm, Name);

  switch (getAccelTableKind()) {
  case AccelTableKind::Apple:
    AppleAccel.addName(Ref, Die);
    break;
  case AccelTableKind::Dwarf: {
    DWARF5AccelTable &Current = getCurrentDWARF5AccelTable();
    assert(((&Current == &AccelTypeUnitsDebugNames) ||
            ((&Current == &AccelDebugNames) &&
             (Unit.getUnitDie().getTag() != dwarf::DW_TAG_type_unit))) &&
               "Kind is CU but TU is being processed.");
    assert(((&Current == &AccelDebugNames) ||
            ((&Current == &AccelTypeUnitsDebugNames) &&
             (Unit.getUnitDie().getTag() == dwarf::DW_TAG_type_unit))) &&
               "Kind is TU but CU is being processed.");
    // The type unit can be discarded, so need to add references to final
    // acceleration table once we know it's complete and we emit it.
    Current.addName(Ref, Die, Unit.getUniqueID(),
                    Unit.getUnitDie().getTag() == dwarf::DW_TAG_type_unit);
    break;
  }
  case AccelTableKind::Default:
    llvm_unreachable("Default should have already been resolved.");
  case AccelTableKind::None:
    llvm_unreachable("None handled above");
  }
}

void DwarfDebug::addAccelName(
    const DwarfUnit &Unit,
    const DICompileUnit::DebugNameTableKind NameTableKind, StringRef Name,
    const DIE &Die) {
  addAccelNameImpl(Unit, NameTableKind, AccelNames, Name, Die);
}

void DwarfDebug::addAccelObjC(
    const DwarfUnit &Unit,
    const DICompileUnit::DebugNameTableKind NameTableKind, StringRef Name,
    const DIE &Die) {
  // ObjC names go only into the Apple accelerator tables.
  if (getAccelTableKind() == AccelTableKind::Apple)
    addAccelNameImpl(Unit, NameTableKind, AccelObjC, Name, Die);
}

void DwarfDebug::addAccelNamespace(
    const DwarfUnit &Unit,
    const DICompileUnit::DebugNameTableKind NameTableKind, StringRef Name,
    const DIE &Die) {
  addAccelNameImpl(Unit, NameTableKind, AccelNamespace, Name, Die);
}

void DwarfDebug::addAccelType(
    const DwarfUnit &Unit,
    const DICompileUnit::DebugNameTableKind NameTableKind, StringRef Name,
    const DIE &Die, char Flags) {
  addAccelNameImpl(Unit, NameTableKind, AccelTypes, Name, Die);
}

uint16_t DwarfDebug::getDwarfVersion() const {
  return Asm->OutStreamer->getContext().getDwarfVersion();
}

dwarf::Form DwarfDebug::getDwarfSectionOffsetForm() const {
  if (Asm->getDwarfVersion() >= 4)
    return dwarf::Form::DW_FORM_sec_offset;
  assert((!Asm->isDwarf64() || (Asm->getDwarfVersion() == 3)) &&
         "DWARF64 is not defined prior DWARFv3");
  return Asm->isDwarf64() ? dwarf::Form::DW_FORM_data8
                          : dwarf::Form::DW_FORM_data4;
}

const MCSymbol *DwarfDebug::getSectionLabel(const MCSection *S) {
  return SectionLabels.lookup(S);
}

void DwarfDebug::insertSectionLabel(const MCSymbol *S) {
  if (SectionLabels.insert(std::make_pair(&S->getSection(), S)).second)
    if (useSplitDwarf() || getDwarfVersion() >= 5)
      AddrPool.getIndex(S);
}

std::optional<MD5::MD5Result>
DwarfDebug::getMD5AsBytes(const DIFile *File) const {
  assert(File);
  if (getDwarfVersion() < 5)
    return std::nullopt;
  std::optional<DIFile::ChecksumInfo<StringRef>> Checksum = File->getChecksum();
  if (!Checksum || Checksum->Kind != DIFile::CSK_MD5)
    return std::nullopt;

  // Convert the string checksum to an MD5Result for the streamer.
  // The verifier validates the checksum so we assume it's okay.
  // An MD5 checksum is 16 bytes.
  std::string ChecksumString = fromHex(Checksum->Value);
  MD5::MD5Result CKMem;
  std::copy(ChecksumString.begin(), ChecksumString.end(), CKMem.data());
  return CKMem;
}

bool DwarfDebug::alwaysUseRanges(const DwarfCompileUnit &CU) const {
  if (MinimizeAddr == MinimizeAddrInV5::Ranges)
    return true;
  if (MinimizeAddr != MinimizeAddrInV5::Default)
    return false;
  if (useSplitDwarf())
    return true;
  return false;
}
