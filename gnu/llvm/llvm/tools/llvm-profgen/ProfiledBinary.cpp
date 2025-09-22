//===-- ProfiledBinary.cpp - Binary decoder ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ProfiledBinary.h"
#include "ErrorHandling.h"
#include "MissingFrameInferrer.h"
#include "ProfileGenerator.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

#define DEBUG_TYPE "load-binary"

using namespace llvm;
using namespace sampleprof;

cl::opt<bool> ShowDisassemblyOnly("show-disassembly-only",
                                  cl::desc("Print disassembled code."));

cl::opt<bool> ShowSourceLocations("show-source-locations",
                                  cl::desc("Print source locations."));

static cl::opt<bool>
    ShowCanonicalFnName("show-canonical-fname",
                        cl::desc("Print canonical function name."));

static cl::opt<bool> ShowPseudoProbe(
    "show-pseudo-probe",
    cl::desc("Print pseudo probe section and disassembled info."));

static cl::opt<bool> UseDwarfCorrelation(
    "use-dwarf-correlation",
    cl::desc("Use dwarf for profile correlation even when binary contains "
             "pseudo probe."));

static cl::opt<std::string>
    DWPPath("dwp", cl::init(""),
            cl::desc("Path of .dwp file. When not specified, it will be "
                     "<binary>.dwp in the same directory as the main binary."));

static cl::list<std::string> DisassembleFunctions(
    "disassemble-functions", cl::CommaSeparated,
    cl::desc("List of functions to print disassembly for. Accept demangled "
             "names only. Only work with show-disassembly-only"));

static cl::opt<bool>
    KernelBinary("kernel",
                 cl::desc("Generate the profile for Linux kernel binary."));

extern cl::opt<bool> ShowDetailedWarning;
extern cl::opt<bool> InferMissingFrames;

namespace llvm {
namespace sampleprof {

static const Target *getTarget(const ObjectFile *Obj) {
  Triple TheTriple = Obj->makeTriple();
  std::string Error;
  std::string ArchName;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(ArchName, TheTriple, Error);
  if (!TheTarget)
    exitWithError(Error, Obj->getFileName());
  return TheTarget;
}

void BinarySizeContextTracker::addInstructionForContext(
    const SampleContextFrameVector &Context, uint32_t InstrSize) {
  ContextTrieNode *CurNode = &RootContext;
  bool IsLeaf = true;
  for (const auto &Callsite : reverse(Context)) {
    FunctionId CallerName = Callsite.Func;
    LineLocation CallsiteLoc = IsLeaf ? LineLocation(0, 0) : Callsite.Location;
    CurNode = CurNode->getOrCreateChildContext(CallsiteLoc, CallerName);
    IsLeaf = false;
  }

  CurNode->addFunctionSize(InstrSize);
}

uint32_t
BinarySizeContextTracker::getFuncSizeForContext(const ContextTrieNode *Node) {
  ContextTrieNode *CurrNode = &RootContext;
  ContextTrieNode *PrevNode = nullptr;

  std::optional<uint32_t> Size;

  // Start from top-level context-less function, traverse down the reverse
  // context trie to find the best/longest match for given context, then
  // retrieve the size.
  LineLocation CallSiteLoc(0, 0);
  while (CurrNode && Node->getParentContext() != nullptr) {
    PrevNode = CurrNode;
    CurrNode = CurrNode->getChildContext(CallSiteLoc, Node->getFuncName());
    if (CurrNode && CurrNode->getFunctionSize())
      Size = *CurrNode->getFunctionSize();
    CallSiteLoc = Node->getCallSiteLoc();
    Node = Node->getParentContext();
  }

  // If we traversed all nodes along the path of the context and haven't
  // found a size yet, pivot to look for size from sibling nodes, i.e size
  // of inlinee under different context.
  if (!Size) {
    if (!CurrNode)
      CurrNode = PrevNode;
    while (!Size && CurrNode && !CurrNode->getAllChildContext().empty()) {
      CurrNode = &CurrNode->getAllChildContext().begin()->second;
      if (CurrNode->getFunctionSize())
        Size = *CurrNode->getFunctionSize();
    }
  }

  assert(Size && "We should at least find one context size.");
  return *Size;
}

void BinarySizeContextTracker::trackInlineesOptimizedAway(
    MCPseudoProbeDecoder &ProbeDecoder) {
  ProbeFrameStack ProbeContext;
  for (const auto &Child : ProbeDecoder.getDummyInlineRoot().getChildren())
    trackInlineesOptimizedAway(ProbeDecoder, *Child.second, ProbeContext);
}

void BinarySizeContextTracker::trackInlineesOptimizedAway(
    MCPseudoProbeDecoder &ProbeDecoder,
    MCDecodedPseudoProbeInlineTree &ProbeNode, ProbeFrameStack &ProbeContext) {
  StringRef FuncName =
      ProbeDecoder.getFuncDescForGUID(ProbeNode.Guid)->FuncName;
  ProbeContext.emplace_back(FuncName, 0);

  // This ProbeContext has a probe, so it has code before inlining and
  // optimization. Make sure we mark its size as known.
  if (!ProbeNode.getProbes().empty()) {
    ContextTrieNode *SizeContext = &RootContext;
    for (auto &ProbeFrame : reverse(ProbeContext)) {
      StringRef CallerName = ProbeFrame.first;
      LineLocation CallsiteLoc(ProbeFrame.second, 0);
      SizeContext =
          SizeContext->getOrCreateChildContext(CallsiteLoc,
                                               FunctionId(CallerName));
    }
    // Add 0 size to make known.
    SizeContext->addFunctionSize(0);
  }

  // DFS down the probe inline tree
  for (const auto &ChildNode : ProbeNode.getChildren()) {
    InlineSite Location = ChildNode.first;
    ProbeContext.back().second = std::get<1>(Location);
    trackInlineesOptimizedAway(ProbeDecoder, *ChildNode.second, ProbeContext);
  }

  ProbeContext.pop_back();
}

ProfiledBinary::ProfiledBinary(const StringRef ExeBinPath,
                               const StringRef DebugBinPath)
    : Path(ExeBinPath), DebugBinaryPath(DebugBinPath),
      SymbolizerOpts(getSymbolizerOpts()), ProEpilogTracker(this),
      Symbolizer(std::make_unique<symbolize::LLVMSymbolizer>(SymbolizerOpts)),
      TrackFuncContextSize(EnableCSPreInliner && UseContextCostForPreInliner) {
  // Point to executable binary if debug info binary is not specified.
  SymbolizerPath = DebugBinPath.empty() ? ExeBinPath : DebugBinPath;
  if (InferMissingFrames)
    MissingContextInferrer = std::make_unique<MissingFrameInferrer>(this);
  load();
}

ProfiledBinary::~ProfiledBinary() {}

void ProfiledBinary::warnNoFuncEntry() {
  uint64_t NoFuncEntryNum = 0;
  for (auto &F : BinaryFunctions) {
    if (F.second.Ranges.empty())
      continue;
    bool hasFuncEntry = false;
    for (auto &R : F.second.Ranges) {
      if (FuncRange *FR = findFuncRangeForStartAddr(R.first)) {
        if (FR->IsFuncEntry) {
          hasFuncEntry = true;
          break;
        }
      }
    }

    if (!hasFuncEntry) {
      NoFuncEntryNum++;
      if (ShowDetailedWarning)
        WithColor::warning()
            << "Failed to determine function entry for " << F.first
            << " due to inconsistent name from symbol table and dwarf info.\n";
    }
  }
  emitWarningSummary(NoFuncEntryNum, BinaryFunctions.size(),
                     "of functions failed to determine function entry due to "
                     "inconsistent name from symbol table and dwarf info.");
}

void ProfiledBinary::load() {
  // Attempt to open the binary.
  OwningBinary<Binary> OBinary = unwrapOrError(createBinary(Path), Path);
  Binary &ExeBinary = *OBinary.getBinary();

  IsCOFF = isa<COFFObjectFile>(&ExeBinary);
  if (!isa<ELFObjectFileBase>(&ExeBinary) && !IsCOFF)
    exitWithError("not a valid ELF/COFF image", Path);

  auto *Obj = cast<ObjectFile>(&ExeBinary);
  TheTriple = Obj->makeTriple();

  LLVM_DEBUG(dbgs() << "Loading " << Path << "\n");

  // Mark the binary as a kernel image;
  IsKernel = KernelBinary;

  // Find the preferred load address for text sections.
  setPreferredTextSegmentAddresses(Obj);

  // Load debug info of subprograms from DWARF section.
  // If path of debug info binary is specified, use the debug info from it,
  // otherwise use the debug info from the executable binary.
  if (!DebugBinaryPath.empty()) {
    OwningBinary<Binary> DebugPath =
        unwrapOrError(createBinary(DebugBinaryPath), DebugBinaryPath);
    loadSymbolsFromDWARF(*cast<ObjectFile>(DebugPath.getBinary()));
  } else {
    loadSymbolsFromDWARF(*cast<ObjectFile>(&ExeBinary));
  }

  DisassembleFunctionSet.insert(DisassembleFunctions.begin(),
                                DisassembleFunctions.end());

  if (auto *ELFObj = dyn_cast<ELFObjectFileBase>(Obj)) {
    checkPseudoProbe(ELFObj);
    if (UsePseudoProbes)
      populateElfSymbolAddressList(ELFObj);

    if (ShowDisassemblyOnly)
      decodePseudoProbe(ELFObj);
  }

  // Disassemble the text sections.
  disassemble(Obj);

  // Use function start and return address to infer prolog and epilog
  ProEpilogTracker.inferPrologAddresses(StartAddrToFuncRangeMap);
  ProEpilogTracker.inferEpilogAddresses(RetAddressSet);

  warnNoFuncEntry();

  // TODO: decode other sections.
}

bool ProfiledBinary::inlineContextEqual(uint64_t Address1, uint64_t Address2) {
  const SampleContextFrameVector &Context1 =
      getCachedFrameLocationStack(Address1);
  const SampleContextFrameVector &Context2 =
      getCachedFrameLocationStack(Address2);
  if (Context1.size() != Context2.size())
    return false;
  if (Context1.empty())
    return false;
  // The leaf frame contains location within the leaf, and it
  // needs to be remove that as it's not part of the calling context
  return std::equal(Context1.begin(), Context1.begin() + Context1.size() - 1,
                    Context2.begin(), Context2.begin() + Context2.size() - 1);
}

SampleContextFrameVector
ProfiledBinary::getExpandedContext(const SmallVectorImpl<uint64_t> &Stack,
                                   bool &WasLeafInlined) {
  SampleContextFrameVector ContextVec;
  if (Stack.empty())
    return ContextVec;
  // Process from frame root to leaf
  for (auto Address : Stack) {
    const SampleContextFrameVector &ExpandedContext =
        getCachedFrameLocationStack(Address);
    // An instruction without a valid debug line will be ignored by sample
    // processing
    if (ExpandedContext.empty())
      return SampleContextFrameVector();
    // Set WasLeafInlined to the size of inlined frame count for the last
    // address which is leaf
    WasLeafInlined = (ExpandedContext.size() > 1);
    ContextVec.append(ExpandedContext);
  }

  // Replace with decoded base discriminator
  for (auto &Frame : ContextVec) {
    Frame.Location.Discriminator = ProfileGeneratorBase::getBaseDiscriminator(
        Frame.Location.Discriminator, UseFSDiscriminator);
  }

  assert(ContextVec.size() && "Context length should be at least 1");

  // Compress the context string except for the leaf frame
  auto LeafFrame = ContextVec.back();
  LeafFrame.Location = LineLocation(0, 0);
  ContextVec.pop_back();
  CSProfileGenerator::compressRecursionContext(ContextVec);
  CSProfileGenerator::trimContext(ContextVec);
  ContextVec.push_back(LeafFrame);
  return ContextVec;
}

template <class ELFT>
void ProfiledBinary::setPreferredTextSegmentAddresses(const ELFFile<ELFT> &Obj,
                                                      StringRef FileName) {
  const auto &PhdrRange = unwrapOrError(Obj.program_headers(), FileName);
  // FIXME: This should be the page size of the system running profiling.
  // However such info isn't available at post-processing time, assuming
  // 4K page now. Note that we don't use EXEC_PAGESIZE from <linux/param.h>
  // because we may build the tools on non-linux.
  uint64_t PageSize = 0x1000;
  for (const typename ELFT::Phdr &Phdr : PhdrRange) {
    if (Phdr.p_type == ELF::PT_LOAD) {
      if (!FirstLoadableAddress)
        FirstLoadableAddress = Phdr.p_vaddr & ~(PageSize - 1U);
      if (Phdr.p_flags & ELF::PF_X) {
        // Segments will always be loaded at a page boundary.
        PreferredTextSegmentAddresses.push_back(Phdr.p_vaddr &
                                                ~(PageSize - 1U));
        TextSegmentOffsets.push_back(Phdr.p_offset & ~(PageSize - 1U));
      }
    }
  }

  if (PreferredTextSegmentAddresses.empty())
    exitWithError("no executable segment found", FileName);
}

void ProfiledBinary::setPreferredTextSegmentAddresses(const COFFObjectFile *Obj,
                                                      StringRef FileName) {
  uint64_t ImageBase = Obj->getImageBase();
  if (!ImageBase)
    exitWithError("Not a COFF image", FileName);

  PreferredTextSegmentAddresses.push_back(ImageBase);
  FirstLoadableAddress = ImageBase;

  for (SectionRef Section : Obj->sections()) {
    const coff_section *Sec = Obj->getCOFFSection(Section);
    if (Sec->Characteristics & COFF::IMAGE_SCN_CNT_CODE)
      TextSegmentOffsets.push_back(Sec->VirtualAddress);
  }
}

void ProfiledBinary::setPreferredTextSegmentAddresses(const ObjectFile *Obj) {
  if (const auto *ELFObj = dyn_cast<ELF32LEObjectFile>(Obj))
    setPreferredTextSegmentAddresses(ELFObj->getELFFile(), Obj->getFileName());
  else if (const auto *ELFObj = dyn_cast<ELF32BEObjectFile>(Obj))
    setPreferredTextSegmentAddresses(ELFObj->getELFFile(), Obj->getFileName());
  else if (const auto *ELFObj = dyn_cast<ELF64LEObjectFile>(Obj))
    setPreferredTextSegmentAddresses(ELFObj->getELFFile(), Obj->getFileName());
  else if (const auto *ELFObj = dyn_cast<ELF64BEObjectFile>(Obj))
    setPreferredTextSegmentAddresses(ELFObj->getELFFile(), Obj->getFileName());
  else if (const auto *COFFObj = dyn_cast<COFFObjectFile>(Obj))
    setPreferredTextSegmentAddresses(COFFObj, Obj->getFileName());
  else
    llvm_unreachable("invalid object format");
}

void ProfiledBinary::checkPseudoProbe(const ELFObjectFileBase *Obj) {
  if (UseDwarfCorrelation)
    return;

  bool HasProbeDescSection = false;
  bool HasPseudoProbeSection = false;

  StringRef FileName = Obj->getFileName();
  for (section_iterator SI = Obj->section_begin(), SE = Obj->section_end();
       SI != SE; ++SI) {
    const SectionRef &Section = *SI;
    StringRef SectionName = unwrapOrError(Section.getName(), FileName);
    if (SectionName == ".pseudo_probe_desc") {
      HasProbeDescSection = true;
    } else if (SectionName == ".pseudo_probe") {
      HasPseudoProbeSection = true;
    }
  }

  // set UsePseudoProbes flag, used for PerfReader
  UsePseudoProbes = HasProbeDescSection && HasPseudoProbeSection;
}

void ProfiledBinary::decodePseudoProbe(const ELFObjectFileBase *Obj) {
  if (!UsePseudoProbes)
    return;

  MCPseudoProbeDecoder::Uint64Set GuidFilter;
  MCPseudoProbeDecoder::Uint64Map FuncStartAddresses;
  if (ShowDisassemblyOnly) {
    if (DisassembleFunctionSet.empty()) {
      FuncStartAddresses = SymbolStartAddrs;
    } else {
      for (auto &F : DisassembleFunctionSet) {
        auto GUID = Function::getGUID(F.first());
        if (auto StartAddr = SymbolStartAddrs.lookup(GUID)) {
          FuncStartAddresses[GUID] = StartAddr;
          FuncRange &Range = StartAddrToFuncRangeMap[StartAddr];
          GuidFilter.insert(Function::getGUID(Range.getFuncName()));
        }
      }
    }
  } else {
    for (auto *F : ProfiledFunctions) {
      GuidFilter.insert(Function::getGUID(F->FuncName));
      for (auto &Range : F->Ranges) {
        auto GUIDs = StartAddrToSymMap.equal_range(Range.first);
        for (auto I = GUIDs.first; I != GUIDs.second; ++I)
          FuncStartAddresses[I->second] = I->first;
      }
    }
  }

  StringRef FileName = Obj->getFileName();
  for (section_iterator SI = Obj->section_begin(), SE = Obj->section_end();
       SI != SE; ++SI) {
    const SectionRef &Section = *SI;
    StringRef SectionName = unwrapOrError(Section.getName(), FileName);

    if (SectionName == ".pseudo_probe_desc") {
      StringRef Contents = unwrapOrError(Section.getContents(), FileName);
      if (!ProbeDecoder.buildGUID2FuncDescMap(
              reinterpret_cast<const uint8_t *>(Contents.data()),
              Contents.size()))
        exitWithError(
            "Pseudo Probe decoder fail in .pseudo_probe_desc section");
    } else if (SectionName == ".pseudo_probe") {
      StringRef Contents = unwrapOrError(Section.getContents(), FileName);
      if (!ProbeDecoder.buildAddress2ProbeMap(
              reinterpret_cast<const uint8_t *>(Contents.data()),
              Contents.size(), GuidFilter, FuncStartAddresses))
        exitWithError("Pseudo Probe decoder fail in .pseudo_probe section");
    }
  }

  // Build TopLevelProbeFrameMap to track size for optimized inlinees when probe
  // is available
  if (TrackFuncContextSize) {
    for (const auto &Child : ProbeDecoder.getDummyInlineRoot().getChildren()) {
      auto *Frame = Child.second.get();
      StringRef FuncName =
          ProbeDecoder.getFuncDescForGUID(Frame->Guid)->FuncName;
      TopLevelProbeFrameMap[FuncName] = Frame;
    }
  }

  if (ShowPseudoProbe)
    ProbeDecoder.printGUID2FuncDescMap(outs());
}

void ProfiledBinary::decodePseudoProbe() {
  OwningBinary<Binary> OBinary = unwrapOrError(createBinary(Path), Path);
  Binary &ExeBinary = *OBinary.getBinary();
  auto *Obj = cast<ELFObjectFileBase>(&ExeBinary);
  decodePseudoProbe(Obj);
}

void ProfiledBinary::setIsFuncEntry(FuncRange *FuncRange,
                                    StringRef RangeSymName) {
  // Skip external function symbol.
  if (!FuncRange)
    return;

  // Set IsFuncEntry to ture if there is only one range in the function or the
  // RangeSymName from ELF is equal to its DWARF-based function name.
  if (FuncRange->Func->Ranges.size() == 1 ||
      (!FuncRange->IsFuncEntry && FuncRange->getFuncName() == RangeSymName))
    FuncRange->IsFuncEntry = true;
}

bool ProfiledBinary::dissassembleSymbol(std::size_t SI, ArrayRef<uint8_t> Bytes,
                                        SectionSymbolsTy &Symbols,
                                        const SectionRef &Section) {
  std::size_t SE = Symbols.size();
  uint64_t SectionAddress = Section.getAddress();
  uint64_t SectSize = Section.getSize();
  uint64_t StartAddress = Symbols[SI].Addr;
  uint64_t NextStartAddress =
      (SI + 1 < SE) ? Symbols[SI + 1].Addr : SectionAddress + SectSize;
  FuncRange *FRange = findFuncRange(StartAddress);
  setIsFuncEntry(FRange, FunctionSamples::getCanonicalFnName(Symbols[SI].Name));
  StringRef SymbolName =
      ShowCanonicalFnName
          ? FunctionSamples::getCanonicalFnName(Symbols[SI].Name)
          : Symbols[SI].Name;
  bool ShowDisassembly =
      ShowDisassemblyOnly && (DisassembleFunctionSet.empty() ||
                              DisassembleFunctionSet.count(SymbolName));
  if (ShowDisassembly)
    outs() << '<' << SymbolName << ">:\n";

  uint64_t Address = StartAddress;
  // Size of a consecutive invalid instruction range starting from Address -1
  // backwards.
  uint64_t InvalidInstLength = 0;
  while (Address < NextStartAddress) {
    MCInst Inst;
    uint64_t Size;
    // Disassemble an instruction.
    bool Disassembled = DisAsm->getInstruction(
        Inst, Size, Bytes.slice(Address - SectionAddress), Address, nulls());
    if (Size == 0)
      Size = 1;

    if (ShowDisassembly) {
      if (ShowPseudoProbe) {
        ProbeDecoder.printProbeForAddress(outs(), Address);
      }
      outs() << format("%8" PRIx64 ":", Address);
      size_t Start = outs().tell();
      if (Disassembled)
        IPrinter->printInst(&Inst, Address + Size, "", *STI, outs());
      else
        outs() << "\t<unknown>";
      if (ShowSourceLocations) {
        unsigned Cur = outs().tell() - Start;
        if (Cur < 40)
          outs().indent(40 - Cur);
        InstructionPointer IP(this, Address);
        outs() << getReversedLocWithContext(
            symbolize(IP, ShowCanonicalFnName, ShowPseudoProbe));
      }
      outs() << "\n";
    }

    if (Disassembled) {
      const MCInstrDesc &MCDesc = MII->get(Inst.getOpcode());

      // Record instruction size.
      AddressToInstSizeMap[Address] = Size;

      // Populate address maps.
      CodeAddressVec.push_back(Address);
      if (MCDesc.isCall()) {
        CallAddressSet.insert(Address);
        UncondBranchAddrSet.insert(Address);
      } else if (MCDesc.isReturn()) {
        RetAddressSet.insert(Address);
        UncondBranchAddrSet.insert(Address);
      } else if (MCDesc.isBranch()) {
        if (MCDesc.isUnconditionalBranch())
          UncondBranchAddrSet.insert(Address);
        BranchAddressSet.insert(Address);
      }

      // Record potential call targets for tail frame inference later-on.
      if (InferMissingFrames && FRange) {
        uint64_t Target = 0;
        MIA->evaluateBranch(Inst, Address, Size, Target);
        if (MCDesc.isCall()) {
          // Indirect call targets are unknown at this point. Recording the
          // unknown target (zero) for further LBR-based refinement.
          MissingContextInferrer->CallEdges[Address].insert(Target);
        } else if (MCDesc.isUnconditionalBranch()) {
          assert(Target &&
                 "target should be known for unconditional direct branch");
          // Any inter-function unconditional jump is considered tail call at
          // this point. This is not 100% accurate and could further be
          // optimized based on some source annotation.
          FuncRange *ToFRange = findFuncRange(Target);
          if (ToFRange && ToFRange->Func != FRange->Func)
            MissingContextInferrer->TailCallEdges[Address].insert(Target);
          LLVM_DEBUG({
            dbgs() << "Direct Tail call: " << format("%8" PRIx64 ":", Address);
            IPrinter->printInst(&Inst, Address + Size, "", *STI.get(), dbgs());
            dbgs() << "\n";
          });
        } else if (MCDesc.isIndirectBranch() && MCDesc.isBarrier()) {
          // This is an indirect branch but not necessarily an indirect tail
          // call. The isBarrier check is to filter out conditional branch.
          // Similar with indirect call targets, recording the unknown target
          // (zero) for further LBR-based refinement.
          MissingContextInferrer->TailCallEdges[Address].insert(Target);
          LLVM_DEBUG({
            dbgs() << "Indirect Tail call: "
                   << format("%8" PRIx64 ":", Address);
            IPrinter->printInst(&Inst, Address + Size, "", *STI.get(), dbgs());
            dbgs() << "\n";
          });
        }
      }

      if (InvalidInstLength) {
        AddrsWithInvalidInstruction.insert(
            {Address - InvalidInstLength, Address - 1});
        InvalidInstLength = 0;
      }
    } else {
      InvalidInstLength += Size;
    }

    Address += Size;
  }

  if (InvalidInstLength)
    AddrsWithInvalidInstruction.insert(
        {Address - InvalidInstLength, Address - 1});

  if (ShowDisassembly)
    outs() << "\n";

  return true;
}

void ProfiledBinary::setUpDisassembler(const ObjectFile *Obj) {
  const Target *TheTarget = getTarget(Obj);
  std::string TripleName = TheTriple.getTriple();
  StringRef FileName = Obj->getFileName();

  MRI.reset(TheTarget->createMCRegInfo(TripleName));
  if (!MRI)
    exitWithError("no register info for target " + TripleName, FileName);

  MCTargetOptions MCOptions;
  AsmInfo.reset(TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
  if (!AsmInfo)
    exitWithError("no assembly info for target " + TripleName, FileName);

  Expected<SubtargetFeatures> Features = Obj->getFeatures();
  if (!Features)
    exitWithError(Features.takeError(), FileName);
  STI.reset(
      TheTarget->createMCSubtargetInfo(TripleName, "", Features->getString()));
  if (!STI)
    exitWithError("no subtarget info for target " + TripleName, FileName);

  MII.reset(TheTarget->createMCInstrInfo());
  if (!MII)
    exitWithError("no instruction info for target " + TripleName, FileName);

  MCContext Ctx(Triple(TripleName), AsmInfo.get(), MRI.get(), STI.get());
  std::unique_ptr<MCObjectFileInfo> MOFI(
      TheTarget->createMCObjectFileInfo(Ctx, /*PIC=*/false));
  Ctx.setObjectFileInfo(MOFI.get());
  DisAsm.reset(TheTarget->createMCDisassembler(*STI, Ctx));
  if (!DisAsm)
    exitWithError("no disassembler for target " + TripleName, FileName);

  MIA.reset(TheTarget->createMCInstrAnalysis(MII.get()));

  int AsmPrinterVariant = AsmInfo->getAssemblerDialect();
  IPrinter.reset(TheTarget->createMCInstPrinter(
      Triple(TripleName), AsmPrinterVariant, *AsmInfo, *MII, *MRI));
  IPrinter->setPrintBranchImmAsAddress(true);
}

void ProfiledBinary::disassemble(const ObjectFile *Obj) {
  // Set up disassembler and related components.
  setUpDisassembler(Obj);

  // Create a mapping from virtual address to symbol name. The symbols in text
  // sections are the candidates to dissassemble.
  std::map<SectionRef, SectionSymbolsTy> AllSymbols;
  StringRef FileName = Obj->getFileName();
  for (const SymbolRef &Symbol : Obj->symbols()) {
    const uint64_t Addr = unwrapOrError(Symbol.getAddress(), FileName);
    const StringRef Name = unwrapOrError(Symbol.getName(), FileName);
    section_iterator SecI = unwrapOrError(Symbol.getSection(), FileName);
    if (SecI != Obj->section_end())
      AllSymbols[*SecI].push_back(SymbolInfoTy(Addr, Name, ELF::STT_NOTYPE));
  }

  // Sort all the symbols. Use a stable sort to stabilize the output.
  for (std::pair<const SectionRef, SectionSymbolsTy> &SecSyms : AllSymbols)
    stable_sort(SecSyms.second);

  assert((DisassembleFunctionSet.empty() || ShowDisassemblyOnly) &&
         "Functions to disassemble should be only specified together with "
         "--show-disassembly-only");

  if (ShowDisassemblyOnly)
    outs() << "\nDisassembly of " << FileName << ":\n";

  // Dissassemble a text section.
  for (section_iterator SI = Obj->section_begin(), SE = Obj->section_end();
       SI != SE; ++SI) {
    const SectionRef &Section = *SI;
    if (!Section.isText())
      continue;

    uint64_t ImageLoadAddr = getPreferredBaseAddress();
    uint64_t SectionAddress = Section.getAddress() - ImageLoadAddr;
    uint64_t SectSize = Section.getSize();
    if (!SectSize)
      continue;

    // Register the text section.
    TextSections.insert({SectionAddress, SectSize});

    StringRef SectionName = unwrapOrError(Section.getName(), FileName);

    if (ShowDisassemblyOnly) {
      outs() << "\nDisassembly of section " << SectionName;
      outs() << " [" << format("0x%" PRIx64, Section.getAddress()) << ", "
             << format("0x%" PRIx64, Section.getAddress() + SectSize)
             << "]:\n\n";
    }

    if (isa<ELFObjectFileBase>(Obj) && SectionName == ".plt")
      continue;

    // Get the section data.
    ArrayRef<uint8_t> Bytes =
        arrayRefFromStringRef(unwrapOrError(Section.getContents(), FileName));

    // Get the list of all the symbols in this section.
    SectionSymbolsTy &Symbols = AllSymbols[Section];

    // Disassemble symbol by symbol.
    for (std::size_t SI = 0, SE = Symbols.size(); SI != SE; ++SI) {
      if (!dissassembleSymbol(SI, Bytes, Symbols, Section))
        exitWithError("disassembling error", FileName);
    }
  }

  if (!AddrsWithInvalidInstruction.empty()) {
    if (ShowDetailedWarning) {
      for (auto &Addr : AddrsWithInvalidInstruction) {
        WithColor::warning()
            << "Invalid instructions at " << format("%8" PRIx64, Addr.first)
            << " - " << format("%8" PRIx64, Addr.second) << "\n";
      }
    }
    WithColor::warning() << "Found " << AddrsWithInvalidInstruction.size()
                         << " invalid instructions\n";
    AddrsWithInvalidInstruction.clear();
  }

  // Dissassemble rodata section to check if FS discriminator symbol exists.
  checkUseFSDiscriminator(Obj, AllSymbols);
}

void ProfiledBinary::checkUseFSDiscriminator(
    const ObjectFile *Obj, std::map<SectionRef, SectionSymbolsTy> &AllSymbols) {
  const char *FSDiscriminatorVar = "__llvm_fs_discriminator__";
  for (section_iterator SI = Obj->section_begin(), SE = Obj->section_end();
       SI != SE; ++SI) {
    const SectionRef &Section = *SI;
    if (!Section.isData() || Section.getSize() == 0)
      continue;
    SectionSymbolsTy &Symbols = AllSymbols[Section];

    for (std::size_t SI = 0, SE = Symbols.size(); SI != SE; ++SI) {
      if (Symbols[SI].Name == FSDiscriminatorVar) {
        UseFSDiscriminator = true;
        return;
      }
    }
  }
}

void ProfiledBinary::populateElfSymbolAddressList(
    const ELFObjectFileBase *Obj) {
  // Create a mapping from virtual address to symbol GUID and the other way
  // around.
  StringRef FileName = Obj->getFileName();
  for (const SymbolRef &Symbol : Obj->symbols()) {
    const uint64_t Addr = unwrapOrError(Symbol.getAddress(), FileName);
    const StringRef Name = unwrapOrError(Symbol.getName(), FileName);
    uint64_t GUID = Function::getGUID(Name);
    SymbolStartAddrs[GUID] = Addr;
    StartAddrToSymMap.emplace(Addr, GUID);
  }
}

void ProfiledBinary::loadSymbolsFromDWARFUnit(DWARFUnit &CompilationUnit) {
  for (const auto &DieInfo : CompilationUnit.dies()) {
    llvm::DWARFDie Die(&CompilationUnit, &DieInfo);

    if (!Die.isSubprogramDIE())
      continue;
    auto Name = Die.getName(llvm::DINameKind::LinkageName);
    if (!Name)
      Name = Die.getName(llvm::DINameKind::ShortName);
    if (!Name)
      continue;

    auto RangesOrError = Die.getAddressRanges();
    if (!RangesOrError)
      continue;
    const DWARFAddressRangesVector &Ranges = RangesOrError.get();

    if (Ranges.empty())
      continue;

    // Different DWARF symbols can have same function name, search or create
    // BinaryFunction indexed by the name.
    auto Ret = BinaryFunctions.emplace(Name, BinaryFunction());
    auto &Func = Ret.first->second;
    if (Ret.second)
      Func.FuncName = Ret.first->first;

    for (const auto &Range : Ranges) {
      uint64_t StartAddress = Range.LowPC;
      uint64_t EndAddress = Range.HighPC;

      if (EndAddress <= StartAddress ||
          StartAddress < getPreferredBaseAddress())
        continue;

      // We may want to know all ranges for one function. Here group the
      // ranges and store them into BinaryFunction.
      Func.Ranges.emplace_back(StartAddress, EndAddress);

      auto R = StartAddrToFuncRangeMap.emplace(StartAddress, FuncRange());
      if (R.second) {
        FuncRange &FRange = R.first->second;
        FRange.Func = &Func;
        FRange.StartAddress = StartAddress;
        FRange.EndAddress = EndAddress;
      } else {
        AddrsWithMultipleSymbols.insert(StartAddress);
        if (ShowDetailedWarning)
          WithColor::warning()
              << "Duplicated symbol start address at "
              << format("%8" PRIx64, StartAddress) << " "
              << R.first->second.getFuncName() << " and " << Name << "\n";
      }
    }
  }
}

void ProfiledBinary::loadSymbolsFromDWARF(ObjectFile &Obj) {
  auto DebugContext = llvm::DWARFContext::create(
      Obj, DWARFContext::ProcessDebugRelocations::Process, nullptr, DWPPath);
  if (!DebugContext)
    exitWithError("Error creating the debug info context", Path);

  for (const auto &CompilationUnit : DebugContext->compile_units())
    loadSymbolsFromDWARFUnit(*CompilationUnit);

  // Handles DWO sections that can either be in .o, .dwo or .dwp files.
  uint32_t NumOfDWOMissing = 0;
  for (const auto &CompilationUnit : DebugContext->compile_units()) {
    DWARFUnit *const DwarfUnit = CompilationUnit.get();
    if (DwarfUnit->getDWOId()) {
      DWARFUnit *DWOCU = DwarfUnit->getNonSkeletonUnitDIE(false).getDwarfUnit();
      if (!DWOCU->isDWOUnit()) {
        NumOfDWOMissing++;
        if (ShowDetailedWarning) {
          std::string DWOName = dwarf::toString(
              DwarfUnit->getUnitDIE().find(
                  {dwarf::DW_AT_dwo_name, dwarf::DW_AT_GNU_dwo_name}),
              "");
          WithColor::warning() << "DWO debug information for " << DWOName
                               << " was not loaded.\n";
        }
        continue;
      }
      loadSymbolsFromDWARFUnit(*DWOCU);
    }
  }

  if (NumOfDWOMissing)
    WithColor::warning()
        << " DWO debug information was not loaded for " << NumOfDWOMissing
        << " modules. Please check the .o, .dwo or .dwp path.\n";
  if (BinaryFunctions.empty())
    WithColor::warning() << "Loading of DWARF info completed, but no binary "
                            "functions have been retrieved.\n";
  // Populate the hash binary function map for MD5 function name lookup. This
  // is done after BinaryFunctions are finalized.
  for (auto &BinaryFunction : BinaryFunctions) {
    HashBinaryFunctions[MD5Hash(StringRef(BinaryFunction.first))] =
        &BinaryFunction.second;
  }

  if (!AddrsWithMultipleSymbols.empty()) {
    WithColor::warning() << "Found " << AddrsWithMultipleSymbols.size()
                         << " start addresses with multiple symbols\n";
    AddrsWithMultipleSymbols.clear();
  }
}

void ProfiledBinary::populateSymbolListFromDWARF(
    ProfileSymbolList &SymbolList) {
  for (auto &I : StartAddrToFuncRangeMap)
    SymbolList.add(I.second.getFuncName());
}

symbolize::LLVMSymbolizer::Options ProfiledBinary::getSymbolizerOpts() const {
  symbolize::LLVMSymbolizer::Options SymbolizerOpts;
  SymbolizerOpts.PrintFunctions =
      DILineInfoSpecifier::FunctionNameKind::LinkageName;
  SymbolizerOpts.Demangle = false;
  SymbolizerOpts.DefaultArch = TheTriple.getArchName().str();
  SymbolizerOpts.UseSymbolTable = false;
  SymbolizerOpts.RelativeAddresses = false;
  SymbolizerOpts.DWPName = DWPPath;
  return SymbolizerOpts;
}

SampleContextFrameVector ProfiledBinary::symbolize(const InstructionPointer &IP,
                                                   bool UseCanonicalFnName,
                                                   bool UseProbeDiscriminator) {
  assert(this == IP.Binary &&
         "Binary should only symbolize its own instruction");
  auto Addr = object::SectionedAddress{IP.Address,
                                       object::SectionedAddress::UndefSection};
  DIInliningInfo InlineStack = unwrapOrError(
      Symbolizer->symbolizeInlinedCode(SymbolizerPath.str(), Addr),
      SymbolizerPath);

  SampleContextFrameVector CallStack;
  for (int32_t I = InlineStack.getNumberOfFrames() - 1; I >= 0; I--) {
    const auto &CallerFrame = InlineStack.getFrame(I);
    if (CallerFrame.FunctionName.empty() ||
        (CallerFrame.FunctionName == "<invalid>"))
      break;

    StringRef FunctionName(CallerFrame.FunctionName);
    if (UseCanonicalFnName)
      FunctionName = FunctionSamples::getCanonicalFnName(FunctionName);

    uint32_t Discriminator = CallerFrame.Discriminator;
    uint32_t LineOffset = (CallerFrame.Line - CallerFrame.StartLine) & 0xffff;
    if (UseProbeDiscriminator) {
      LineOffset =
          PseudoProbeDwarfDiscriminator::extractProbeIndex(Discriminator);
      Discriminator = 0;
    }

    LineLocation Line(LineOffset, Discriminator);
    auto It = NameStrings.insert(FunctionName.str());
    CallStack.emplace_back(FunctionId(StringRef(*It.first)), Line);
  }

  return CallStack;
}

void ProfiledBinary::computeInlinedContextSizeForRange(uint64_t RangeBegin,
                                                       uint64_t RangeEnd) {
  InstructionPointer IP(this, RangeBegin, true);

  if (IP.Address != RangeBegin)
    WithColor::warning() << "Invalid start instruction at "
                         << format("%8" PRIx64, RangeBegin) << "\n";

  if (IP.Address >= RangeEnd)
    return;

  do {
    const SampleContextFrameVector SymbolizedCallStack =
        getFrameLocationStack(IP.Address, UsePseudoProbes);
    uint64_t Size = AddressToInstSizeMap[IP.Address];
    // Record instruction size for the corresponding context
    FuncSizeTracker.addInstructionForContext(SymbolizedCallStack, Size);

  } while (IP.advance() && IP.Address < RangeEnd);
}

void ProfiledBinary::computeInlinedContextSizeForFunc(
    const BinaryFunction *Func) {
  // Note that a function can be spilt into multiple ranges, so compute for all
  // ranges of the function.
  for (const auto &Range : Func->Ranges)
    computeInlinedContextSizeForRange(Range.first, Range.second);

  // Track optimized-away inlinee for probed binary. A function inlined and then
  // optimized away should still have their probes left over in places.
  if (usePseudoProbes()) {
    auto I = TopLevelProbeFrameMap.find(Func->FuncName);
    if (I != TopLevelProbeFrameMap.end()) {
      BinarySizeContextTracker::ProbeFrameStack ProbeContext;
      FuncSizeTracker.trackInlineesOptimizedAway(ProbeDecoder, *I->second,
                                                 ProbeContext);
    }
  }
}

void ProfiledBinary::inferMissingFrames(
    const SmallVectorImpl<uint64_t> &Context,
    SmallVectorImpl<uint64_t> &NewContext) {
  MissingContextInferrer->inferMissingFrames(Context, NewContext);
}

InstructionPointer::InstructionPointer(const ProfiledBinary *Binary,
                                       uint64_t Address, bool RoundToNext)
    : Binary(Binary), Address(Address) {
  Index = Binary->getIndexForAddr(Address);
  if (RoundToNext) {
    // we might get address which is not the code
    // it should round to the next valid address
    if (Index >= Binary->getCodeAddrVecSize())
      this->Address = UINT64_MAX;
    else
      this->Address = Binary->getAddressforIndex(Index);
  }
}

bool InstructionPointer::advance() {
  Index++;
  if (Index >= Binary->getCodeAddrVecSize()) {
    Address = UINT64_MAX;
    return false;
  }
  Address = Binary->getAddressforIndex(Index);
  return true;
}

bool InstructionPointer::backward() {
  if (Index == 0) {
    Address = 0;
    return false;
  }
  Index--;
  Address = Binary->getAddressforIndex(Index);
  return true;
}

void InstructionPointer::update(uint64_t Addr) {
  Address = Addr;
  Index = Binary->getIndexForAddr(Address);
}

} // end namespace sampleprof
} // end namespace llvm
