//===-- ProfiledBinary.h - Binary decoder -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_PROFGEN_PROFILEDBINARY_H
#define LLVM_TOOLS_LLVM_PROFGEN_PROFILEDBINARY_H

#include "CallContext.h"
#include "ErrorHandling.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCPseudoProbe.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Transforms/IPO/SampleContextTracker.h"
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvm {
extern cl::opt<bool> EnableCSPreInliner;
extern cl::opt<bool> UseContextCostForPreInliner;
} // namespace llvm

using namespace llvm;
using namespace sampleprof;
using namespace llvm::object;

namespace llvm {
namespace sampleprof {

class ProfiledBinary;
class MissingFrameInferrer;

struct InstructionPointer {
  const ProfiledBinary *Binary;
  // Address of the executable segment of the binary.
  uint64_t Address;
  // Index to the sorted code address array of the binary.
  uint64_t Index = 0;
  InstructionPointer(const ProfiledBinary *Binary, uint64_t Address,
                     bool RoundToNext = false);
  bool advance();
  bool backward();
  void update(uint64_t Addr);
};

// The special frame addresses.
enum SpecialFrameAddr {
  // Dummy root of frame trie.
  DummyRoot = 0,
  // Represent all the addresses outside of current binary.
  // This's also used to indicate the call stack should be truncated since this
  // isn't a real call context the compiler will see.
  ExternalAddr = 1,
};

using RangesTy = std::vector<std::pair<uint64_t, uint64_t>>;

struct BinaryFunction {
  StringRef FuncName;
  // End of range is an exclusive bound.
  RangesTy Ranges;

  uint64_t getFuncSize() {
    uint64_t Sum = 0;
    for (auto &R : Ranges) {
      Sum += R.second - R.first;
    }
    return Sum;
  }
};

// Info about function range. A function can be split into multiple
// non-continuous ranges, each range corresponds to one FuncRange.
struct FuncRange {
  uint64_t StartAddress;
  // EndAddress is an exclusive bound.
  uint64_t EndAddress;
  // Function the range belongs to
  BinaryFunction *Func;
  // Whether the start address is the real entry of the function.
  bool IsFuncEntry = false;

  StringRef getFuncName() { return Func->FuncName; }
};

// PrologEpilog address tracker, used to filter out broken stack samples
// Currently we use a heuristic size (two) to infer prolog and epilog
// based on the start address and return address. In the future,
// we will switch to Dwarf CFI based tracker
struct PrologEpilogTracker {
  // A set of prolog and epilog addresses. Used by virtual unwinding.
  std::unordered_set<uint64_t> PrologEpilogSet;
  ProfiledBinary *Binary;
  PrologEpilogTracker(ProfiledBinary *Bin) : Binary(Bin){};

  // Take the two addresses from the start of function as prolog
  void
  inferPrologAddresses(std::map<uint64_t, FuncRange> &FuncStartAddressMap) {
    for (auto I : FuncStartAddressMap) {
      PrologEpilogSet.insert(I.first);
      InstructionPointer IP(Binary, I.first);
      if (!IP.advance())
        break;
      PrologEpilogSet.insert(IP.Address);
    }
  }

  // Take the last two addresses before the return address as epilog
  void inferEpilogAddresses(std::unordered_set<uint64_t> &RetAddrs) {
    for (auto Addr : RetAddrs) {
      PrologEpilogSet.insert(Addr);
      InstructionPointer IP(Binary, Addr);
      if (!IP.backward())
        break;
      PrologEpilogSet.insert(IP.Address);
    }
  }
};

// Track function byte size under different context (outlined version as well as
// various inlined versions). It also provides query support to get function
// size with the best matching context, which is used to help pre-inliner use
// accurate post-optimization size to make decisions.
// TODO: If an inlinee is completely optimized away, ideally we should have zero
// for its context size, currently we would misss such context since it doesn't
// have instructions. To fix this, we need to mark all inlinee with entry probe
// but without instructions as having zero size.
class BinarySizeContextTracker {
public:
  // Add instruction with given size to a context
  void addInstructionForContext(const SampleContextFrameVector &Context,
                                uint32_t InstrSize);

  // Get function size with a specific context. When there's no exact match
  // for the given context, try to retrieve the size of that function from
  // closest matching context.
  uint32_t getFuncSizeForContext(const ContextTrieNode *Context);

  // For inlinees that are full optimized away, we can establish zero size using
  // their remaining probes.
  void trackInlineesOptimizedAway(MCPseudoProbeDecoder &ProbeDecoder);

  using ProbeFrameStack = SmallVector<std::pair<StringRef, uint32_t>>;
  void trackInlineesOptimizedAway(MCPseudoProbeDecoder &ProbeDecoder,
                                  MCDecodedPseudoProbeInlineTree &ProbeNode,
                                  ProbeFrameStack &Context);

  void dump() { RootContext.dumpTree(); }

private:
  // Root node for context trie tree, node that this is a reverse context trie
  // with callee as parent and caller as child. This way we can traverse from
  // root to find the best/longest matching context if an exact match does not
  // exist. It gives us the best possible estimate for function's post-inline,
  // post-optimization byte size.
  ContextTrieNode RootContext;
};

using AddressRange = std::pair<uint64_t, uint64_t>;

class ProfiledBinary {
  // Absolute path of the executable binary.
  std::string Path;
  // Path of the debug info binary.
  std::string DebugBinaryPath;
  // The target triple.
  Triple TheTriple;
  // Path of symbolizer path which should be pointed to binary with debug info.
  StringRef SymbolizerPath;
  // Options used to configure the symbolizer
  symbolize::LLVMSymbolizer::Options SymbolizerOpts;
  // The runtime base address that the first executable segment is loaded at.
  uint64_t BaseAddress = 0;
  // The runtime base address that the first loadabe segment is loaded at.
  uint64_t FirstLoadableAddress = 0;
  // The preferred load address of each executable segment.
  std::vector<uint64_t> PreferredTextSegmentAddresses;
  // The file offset of each executable segment.
  std::vector<uint64_t> TextSegmentOffsets;

  // Mutiple MC component info
  std::unique_ptr<const MCRegisterInfo> MRI;
  std::unique_ptr<const MCAsmInfo> AsmInfo;
  std::unique_ptr<const MCSubtargetInfo> STI;
  std::unique_ptr<const MCInstrInfo> MII;
  std::unique_ptr<MCDisassembler> DisAsm;
  std::unique_ptr<const MCInstrAnalysis> MIA;
  std::unique_ptr<MCInstPrinter> IPrinter;
  // A list of text sections sorted by start RVA and size. Used to check
  // if a given RVA is a valid code address.
  std::set<std::pair<uint64_t, uint64_t>> TextSections;

  // A map of mapping function name to BinaryFunction info.
  std::unordered_map<std::string, BinaryFunction> BinaryFunctions;

  // Lookup BinaryFunctions using the function name's MD5 hash. Needed if the
  // profile is using MD5.
  std::unordered_map<uint64_t, BinaryFunction *> HashBinaryFunctions;

  // A list of binary functions that have samples.
  std::unordered_set<const BinaryFunction *> ProfiledFunctions;

  // GUID to Elf symbol start address map
  DenseMap<uint64_t, uint64_t> SymbolStartAddrs;

  // These maps are for temporary use of warning diagnosis.
  DenseSet<int64_t> AddrsWithMultipleSymbols;
  DenseSet<std::pair<uint64_t, uint64_t>> AddrsWithInvalidInstruction;

  // Start address to Elf symbol GUID map
  std::unordered_multimap<uint64_t, uint64_t> StartAddrToSymMap;

  // An ordered map of mapping function's start address to function range
  // relevant info. Currently to determine if the offset of ELF is the start of
  // a real function, we leverage the function range info from DWARF.
  std::map<uint64_t, FuncRange> StartAddrToFuncRangeMap;

  // Address to context location map. Used to expand the context.
  std::unordered_map<uint64_t, SampleContextFrameVector> AddressToLocStackMap;

  // Address to instruction size map. Also used for quick Address lookup.
  std::unordered_map<uint64_t, uint64_t> AddressToInstSizeMap;

  // An array of Addresses of all instructions sorted in increasing order. The
  // sorting is needed to fast advance to the next forward/backward instruction.
  std::vector<uint64_t> CodeAddressVec;
  // A set of call instruction addresses. Used by virtual unwinding.
  std::unordered_set<uint64_t> CallAddressSet;
  // A set of return instruction addresses. Used by virtual unwinding.
  std::unordered_set<uint64_t> RetAddressSet;
  // An ordered set of unconditional branch instruction addresses.
  std::set<uint64_t> UncondBranchAddrSet;
  // A set of branch instruction addresses.
  std::unordered_set<uint64_t> BranchAddressSet;

  // Estimate and track function prolog and epilog ranges.
  PrologEpilogTracker ProEpilogTracker;

  // Infer missing frames due to compiler optimizations such as tail call
  // elimination.
  std::unique_ptr<MissingFrameInferrer> MissingContextInferrer;

  // Track function sizes under different context
  BinarySizeContextTracker FuncSizeTracker;

  // The symbolizer used to get inline context for an instruction.
  std::unique_ptr<symbolize::LLVMSymbolizer> Symbolizer;

  // String table owning function name strings created from the symbolizer.
  std::unordered_set<std::string> NameStrings;

  // A collection of functions to print disassembly for.
  StringSet<> DisassembleFunctionSet;

  // Pseudo probe decoder
  MCPseudoProbeDecoder ProbeDecoder;

  // Function name to probe frame map for top-level outlined functions.
  StringMap<MCDecodedPseudoProbeInlineTree *> TopLevelProbeFrameMap;

  bool UsePseudoProbes = false;

  bool UseFSDiscriminator = false;

  // Whether we need to symbolize all instructions to get function context size.
  bool TrackFuncContextSize = false;

  // Whether this is a kernel image;
  bool IsKernel = false;

  // Indicate if the base loading address is parsed from the mmap event or uses
  // the preferred address
  bool IsLoadedByMMap = false;
  // Use to avoid redundant warning.
  bool MissingMMapWarned = false;

  bool IsCOFF = false;

  void setPreferredTextSegmentAddresses(const ObjectFile *O);

  template <class ELFT>
  void setPreferredTextSegmentAddresses(const ELFFile<ELFT> &Obj,
                                        StringRef FileName);
  void setPreferredTextSegmentAddresses(const COFFObjectFile *Obj,
                                        StringRef FileName);

  void checkPseudoProbe(const ELFObjectFileBase *Obj);

  void decodePseudoProbe(const ELFObjectFileBase *Obj);

  void
  checkUseFSDiscriminator(const ObjectFile *Obj,
                          std::map<SectionRef, SectionSymbolsTy> &AllSymbols);

  // Set up disassembler and related components.
  void setUpDisassembler(const ObjectFile *Obj);
  symbolize::LLVMSymbolizer::Options getSymbolizerOpts() const;

  // Load debug info of subprograms from DWARF section.
  void loadSymbolsFromDWARF(ObjectFile &Obj);

  // Load debug info from DWARF unit.
  void loadSymbolsFromDWARFUnit(DWARFUnit &CompilationUnit);

  // Create elf symbol to its start address mapping.
  void populateElfSymbolAddressList(const ELFObjectFileBase *O);

  // A function may be spilt into multiple non-continuous address ranges. We use
  // this to set whether start a function range is the real entry of the
  // function and also set false to the non-function label.
  void setIsFuncEntry(FuncRange *FRange, StringRef RangeSymName);

  // Warn if no entry range exists in the function.
  void warnNoFuncEntry();

  /// Dissassemble the text section and build various address maps.
  void disassemble(const ObjectFile *O);

  /// Helper function to dissassemble the symbol and extract info for unwinding
  bool dissassembleSymbol(std::size_t SI, ArrayRef<uint8_t> Bytes,
                          SectionSymbolsTy &Symbols, const SectionRef &Section);
  /// Symbolize a given instruction pointer and return a full call context.
  SampleContextFrameVector symbolize(const InstructionPointer &IP,
                                     bool UseCanonicalFnName = false,
                                     bool UseProbeDiscriminator = false);
  /// Decode the interesting parts of the binary and build internal data
  /// structures. On high level, the parts of interest are:
  ///   1. Text sections, including the main code section and the PLT
  ///   entries that will be used to handle cross-module call transitions.
  ///   2. The .debug_line section, used by Dwarf-based profile generation.
  ///   3. Pseudo probe related sections, used by probe-based profile
  ///   generation.
  void load();

public:
  ProfiledBinary(const StringRef ExeBinPath, const StringRef DebugBinPath);
  ~ProfiledBinary();

  void decodePseudoProbe();

  StringRef getPath() const { return Path; }
  StringRef getName() const { return llvm::sys::path::filename(Path); }
  uint64_t getBaseAddress() const { return BaseAddress; }
  void setBaseAddress(uint64_t Address) { BaseAddress = Address; }

  bool isCOFF() const { return IsCOFF; }

  // Canonicalize to use preferred load address as base address.
  uint64_t canonicalizeVirtualAddress(uint64_t Address) {
    return Address - BaseAddress + getPreferredBaseAddress();
  }
  // Return the preferred load address for the first executable segment.
  uint64_t getPreferredBaseAddress() const {
    return PreferredTextSegmentAddresses[0];
  }
  // Return the preferred load address for the first loadable segment.
  uint64_t getFirstLoadableAddress() const { return FirstLoadableAddress; }
  // Return the file offset for the first executable segment.
  uint64_t getTextSegmentOffset() const { return TextSegmentOffsets[0]; }
  const std::vector<uint64_t> &getPreferredTextSegmentAddresses() const {
    return PreferredTextSegmentAddresses;
  }
  const std::vector<uint64_t> &getTextSegmentOffsets() const {
    return TextSegmentOffsets;
  }

  uint64_t getInstSize(uint64_t Address) const {
    auto I = AddressToInstSizeMap.find(Address);
    if (I == AddressToInstSizeMap.end())
      return 0;
    return I->second;
  }

  bool addressIsCode(uint64_t Address) const {
    return AddressToInstSizeMap.find(Address) != AddressToInstSizeMap.end();
  }

  bool addressIsCall(uint64_t Address) const {
    return CallAddressSet.count(Address);
  }
  bool addressIsReturn(uint64_t Address) const {
    return RetAddressSet.count(Address);
  }
  bool addressInPrologEpilog(uint64_t Address) const {
    return ProEpilogTracker.PrologEpilogSet.count(Address);
  }

  bool addressIsTransfer(uint64_t Address) {
    return BranchAddressSet.count(Address) || RetAddressSet.count(Address) ||
           CallAddressSet.count(Address);
  }

  bool rangeCrossUncondBranch(uint64_t Start, uint64_t End) {
    if (Start >= End)
      return false;
    auto R = UncondBranchAddrSet.lower_bound(Start);
    return R != UncondBranchAddrSet.end() && *R < End;
  }

  uint64_t getAddressforIndex(uint64_t Index) const {
    return CodeAddressVec[Index];
  }

  size_t getCodeAddrVecSize() const { return CodeAddressVec.size(); }

  bool usePseudoProbes() const { return UsePseudoProbes; }
  bool useFSDiscriminator() const { return UseFSDiscriminator; }
  bool isKernel() const { return IsKernel; }

  static bool isKernelImageName(StringRef BinaryName) {
    return BinaryName == "[kernel.kallsyms]" ||
           BinaryName == "[kernel.kallsyms]_stext" ||
           BinaryName == "[kernel.kallsyms]_text";
  }

  // Get the index in CodeAddressVec for the address
  // As we might get an address which is not the code
  // here it would round to the next valid code address by
  // using lower bound operation
  uint32_t getIndexForAddr(uint64_t Address) const {
    auto Low = llvm::lower_bound(CodeAddressVec, Address);
    return Low - CodeAddressVec.begin();
  }

  uint64_t getCallAddrFromFrameAddr(uint64_t FrameAddr) const {
    if (FrameAddr == ExternalAddr)
      return ExternalAddr;
    auto I = getIndexForAddr(FrameAddr);
    FrameAddr = I ? getAddressforIndex(I - 1) : 0;
    if (FrameAddr && addressIsCall(FrameAddr))
      return FrameAddr;
    return 0;
  }

  FuncRange *findFuncRangeForStartAddr(uint64_t Address) {
    auto I = StartAddrToFuncRangeMap.find(Address);
    if (I == StartAddrToFuncRangeMap.end())
      return nullptr;
    return &I->second;
  }

  // Binary search the function range which includes the input address.
  FuncRange *findFuncRange(uint64_t Address) {
    auto I = StartAddrToFuncRangeMap.upper_bound(Address);
    if (I == StartAddrToFuncRangeMap.begin())
      return nullptr;
    I--;

    if (Address >= I->second.EndAddress)
      return nullptr;

    return &I->second;
  }

  // Get all ranges of one function.
  RangesTy getRanges(uint64_t Address) {
    auto *FRange = findFuncRange(Address);
    // Ignore the range which falls into plt section or system lib.
    if (!FRange)
      return RangesTy();

    return FRange->Func->Ranges;
  }

  const std::unordered_map<std::string, BinaryFunction> &
  getAllBinaryFunctions() {
    return BinaryFunctions;
  }

  std::unordered_set<const BinaryFunction *> &getProfiledFunctions() {
    return ProfiledFunctions;
  }

  void setProfiledFunctions(std::unordered_set<const BinaryFunction *> &Funcs) {
    ProfiledFunctions = Funcs;
  }
  
  BinaryFunction *getBinaryFunction(FunctionId FName) {
    if (FName.isStringRef()) {
      auto I = BinaryFunctions.find(FName.str());
      if (I == BinaryFunctions.end())
        return nullptr;
      return &I->second;
    }
    auto I = HashBinaryFunctions.find(FName.getHashCode());
    if (I == HashBinaryFunctions.end())
      return nullptr;
    return I->second;
  }

  uint32_t getFuncSizeForContext(const ContextTrieNode *ContextNode) {
    return FuncSizeTracker.getFuncSizeForContext(ContextNode);
  }

  void inferMissingFrames(const SmallVectorImpl<uint64_t> &Context,
                          SmallVectorImpl<uint64_t> &NewContext);

  // Load the symbols from debug table and populate into symbol list.
  void populateSymbolListFromDWARF(ProfileSymbolList &SymbolList);

  SampleContextFrameVector
  getFrameLocationStack(uint64_t Address, bool UseProbeDiscriminator = false) {
    InstructionPointer IP(this, Address);
    return symbolize(IP, SymbolizerOpts.UseSymbolTable, UseProbeDiscriminator);
  }

  const SampleContextFrameVector &
  getCachedFrameLocationStack(uint64_t Address,
                              bool UseProbeDiscriminator = false) {
    auto I = AddressToLocStackMap.emplace(Address, SampleContextFrameVector());
    if (I.second) {
      I.first->second = getFrameLocationStack(Address, UseProbeDiscriminator);
    }
    return I.first->second;
  }

  std::optional<SampleContextFrame> getInlineLeafFrameLoc(uint64_t Address) {
    const auto &Stack = getCachedFrameLocationStack(Address);
    if (Stack.empty())
      return {};
    return Stack.back();
  }

  void flushSymbolizer() { Symbolizer.reset(); }

  MissingFrameInferrer *getMissingContextInferrer() {
    return MissingContextInferrer.get();
  }

  // Compare two addresses' inline context
  bool inlineContextEqual(uint64_t Add1, uint64_t Add2);

  // Get the full context of the current stack with inline context filled in.
  // It will search the disassembling info stored in AddressToLocStackMap. This
  // is used as the key of function sample map
  SampleContextFrameVector
  getExpandedContext(const SmallVectorImpl<uint64_t> &Stack,
                     bool &WasLeafInlined);
  // Go through instructions among the given range and record its size for the
  // inline context.
  void computeInlinedContextSizeForRange(uint64_t StartAddress,
                                         uint64_t EndAddress);

  void computeInlinedContextSizeForFunc(const BinaryFunction *Func);

  const MCDecodedPseudoProbe *getCallProbeForAddr(uint64_t Address) const {
    return ProbeDecoder.getCallProbeForAddr(Address);
  }

  void getInlineContextForProbe(const MCDecodedPseudoProbe *Probe,
                                SampleContextFrameVector &InlineContextStack,
                                bool IncludeLeaf = false) const {
    SmallVector<MCPseudoProbeFrameLocation, 16> ProbeInlineContext;
    ProbeDecoder.getInlineContextForProbe(Probe, ProbeInlineContext,
                                          IncludeLeaf);
    for (uint32_t I = 0; I < ProbeInlineContext.size(); I++) {
      auto &Callsite = ProbeInlineContext[I];
      // Clear the current context for an unknown probe.
      if (Callsite.second == 0 && I != ProbeInlineContext.size() - 1) {
        InlineContextStack.clear();
        continue;
      }
      InlineContextStack.emplace_back(FunctionId(Callsite.first),
                                      LineLocation(Callsite.second, 0));
    }
  }
  const AddressProbesMap &getAddress2ProbesMap() const {
    return ProbeDecoder.getAddress2ProbesMap();
  }
  const MCPseudoProbeFuncDesc *getFuncDescForGUID(uint64_t GUID) {
    return ProbeDecoder.getFuncDescForGUID(GUID);
  }

  const MCPseudoProbeFuncDesc *
  getInlinerDescForProbe(const MCDecodedPseudoProbe *Probe) {
    return ProbeDecoder.getInlinerDescForProbe(Probe);
  }

  bool getTrackFuncContextSize() { return TrackFuncContextSize; }

  bool getIsLoadedByMMap() { return IsLoadedByMMap; }

  void setIsLoadedByMMap(bool Value) { IsLoadedByMMap = Value; }

  bool getMissingMMapWarned() { return MissingMMapWarned; }

  void setMissingMMapWarned(bool Value) { MissingMMapWarned = Value; }
};

} // end namespace sampleprof
} // end namespace llvm

#endif
