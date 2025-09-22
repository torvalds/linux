//===- lib/MC/MCPseudoProbe.cpp - Pseudo probe encoding support ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCPseudoProbe.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/PseudoProbe.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <sstream>
#include <vector>

#define DEBUG_TYPE "mcpseudoprobe"

using namespace llvm;
using namespace support;

#ifndef NDEBUG
int MCPseudoProbeTable::DdgPrintIndent = 0;
#endif

static const MCExpr *buildSymbolDiff(MCObjectStreamer *MCOS, const MCSymbol *A,
                                     const MCSymbol *B) {
  MCContext &Context = MCOS->getContext();
  MCSymbolRefExpr::VariantKind Variant = MCSymbolRefExpr::VK_None;
  const MCExpr *ARef = MCSymbolRefExpr::create(A, Variant, Context);
  const MCExpr *BRef = MCSymbolRefExpr::create(B, Variant, Context);
  const MCExpr *AddrDelta =
      MCBinaryExpr::create(MCBinaryExpr::Sub, ARef, BRef, Context);
  return AddrDelta;
}

void MCPseudoProbe::emit(MCObjectStreamer *MCOS,
                         const MCPseudoProbe *LastProbe) const {
  bool IsSentinel = isSentinelProbe(getAttributes());
  assert((LastProbe || IsSentinel) &&
         "Last probe should not be null for non-sentinel probes");

  // Emit Index
  MCOS->emitULEB128IntValue(Index);
  // Emit Type and the flag:
  // Type (bit 0 to 3), with bit 4 to 6 for attributes.
  // Flag (bit 7, 0 - code address, 1 - address delta). This indicates whether
  // the following field is a symbolic code address or an address delta.
  // Emit FS discriminator
  assert(Type <= 0xF && "Probe type too big to encode, exceeding 15");
  auto NewAttributes = Attributes;
  if (Discriminator)
    NewAttributes |= (uint32_t)PseudoProbeAttributes::HasDiscriminator;
  assert(NewAttributes <= 0x7 &&
         "Probe attributes too big to encode, exceeding 7");
  uint8_t PackedType = Type | (NewAttributes << 4);
  uint8_t Flag =
      !IsSentinel ? ((int8_t)MCPseudoProbeFlag::AddressDelta << 7) : 0;
  MCOS->emitInt8(Flag | PackedType);

  if (!IsSentinel) {
    // Emit the delta between the address label and LastProbe.
    const MCExpr *AddrDelta =
        buildSymbolDiff(MCOS, Label, LastProbe->getLabel());
    int64_t Delta;
    if (AddrDelta->evaluateAsAbsolute(Delta, MCOS->getAssemblerPtr())) {
      MCOS->emitSLEB128IntValue(Delta);
    } else {
      MCOS->insert(MCOS->getContext().allocFragment<MCPseudoProbeAddrFragment>(
          AddrDelta));
    }
  } else {
    // Emit the GUID of the split function that the sentinel probe represents.
    MCOS->emitInt64(Guid);
  }

  if (Discriminator)
    MCOS->emitULEB128IntValue(Discriminator);

  LLVM_DEBUG({
    dbgs().indent(MCPseudoProbeTable::DdgPrintIndent);
    dbgs() << "Probe: " << Index << "\n";
  });
}

void MCPseudoProbeInlineTree::addPseudoProbe(
    const MCPseudoProbe &Probe, const MCPseudoProbeInlineStack &InlineStack) {
  // The function should not be called on the root.
  assert(isRoot() && "Should only be called on root");

  // When it comes here, the input look like:
  //    Probe: GUID of C, ...
  //    InlineStack: [88, A], [66, B]
  // which means, Function A inlines function B at call site with a probe id of
  // 88, and B inlines C at probe 66. The tri-tree expects a tree path like {[0,
  // A], [88, B], [66, C]} to locate the tree node where the probe should be
  // added. Note that the edge [0, A] means A is the top-level function we are
  // emitting probes for.

  // Make a [0, A] edge.
  // An empty inline stack means the function that the probe originates from
  // is a top-level function.
  InlineSite Top;
  if (InlineStack.empty()) {
    Top = InlineSite(Probe.getGuid(), 0);
  } else {
    Top = InlineSite(std::get<0>(InlineStack.front()), 0);
  }

  auto *Cur = getOrAddNode(Top);

  // Make interior edges by walking the inline stack. Once it's done, Cur should
  // point to the node that the probe originates from.
  if (!InlineStack.empty()) {
    auto Iter = InlineStack.begin();
    auto Index = std::get<1>(*Iter);
    Iter++;
    for (; Iter != InlineStack.end(); Iter++) {
      // Make an edge by using the previous probe id and current GUID.
      Cur = Cur->getOrAddNode(InlineSite(std::get<0>(*Iter), Index));
      Index = std::get<1>(*Iter);
    }
    Cur = Cur->getOrAddNode(InlineSite(Probe.getGuid(), Index));
  }

  Cur->Probes.push_back(Probe);
}

void MCPseudoProbeInlineTree::emit(MCObjectStreamer *MCOS,
                                   const MCPseudoProbe *&LastProbe) {
  LLVM_DEBUG({
    dbgs().indent(MCPseudoProbeTable::DdgPrintIndent);
    dbgs() << "Group [\n";
    MCPseudoProbeTable::DdgPrintIndent += 2;
  });
  assert(!isRoot() && "Root should be handled separately");

  // Emit probes grouped by GUID.
  LLVM_DEBUG({
    dbgs().indent(MCPseudoProbeTable::DdgPrintIndent);
    dbgs() << "GUID: " << Guid << "\n";
  });
  // Emit Guid
  MCOS->emitInt64(Guid);
  // Emit number of probes in this node, including a sentinel probe for
  // top-level functions if needed.
  bool NeedSentinel = false;
  if (Parent->isRoot()) {
    assert(isSentinelProbe(LastProbe->getAttributes()) &&
           "Starting probe of a top-level function should be a sentinel probe");
    // The main body of a split function doesn't need a sentinel probe.
    if (LastProbe->getGuid() != Guid)
      NeedSentinel = true;
  }

  MCOS->emitULEB128IntValue(Probes.size() + NeedSentinel);
  // Emit number of direct inlinees
  MCOS->emitULEB128IntValue(Children.size());
  // Emit sentinel probe for top-level functions
  if (NeedSentinel)
    LastProbe->emit(MCOS, nullptr);

  // Emit probes in this group
  for (const auto &Probe : Probes) {
    Probe.emit(MCOS, LastProbe);
    LastProbe = &Probe;
  }

  // Emit sorted descendant. InlineSite is unique for each pair, so there will
  // be no ordering of Inlinee based on MCPseudoProbeInlineTree*
  using InlineeType = std::pair<InlineSite, MCPseudoProbeInlineTree *>;
  std::vector<InlineeType> Inlinees;
  for (const auto &Child : Children)
    Inlinees.emplace_back(Child.first, Child.second.get());
  llvm::sort(Inlinees, llvm::less_first());

  for (const auto &Inlinee : Inlinees) {
    // Emit probe index
    MCOS->emitULEB128IntValue(std::get<1>(Inlinee.first));
    LLVM_DEBUG({
      dbgs().indent(MCPseudoProbeTable::DdgPrintIndent);
      dbgs() << "InlineSite: " << std::get<1>(Inlinee.first) << "\n";
    });
    // Emit the group
    Inlinee.second->emit(MCOS, LastProbe);
  }

  LLVM_DEBUG({
    MCPseudoProbeTable::DdgPrintIndent -= 2;
    dbgs().indent(MCPseudoProbeTable::DdgPrintIndent);
    dbgs() << "]\n";
  });
}

void MCPseudoProbeSections::emit(MCObjectStreamer *MCOS) {
  MCContext &Ctx = MCOS->getContext();
  SmallVector<std::pair<MCSymbol *, MCPseudoProbeInlineTree *>> Vec;
  Vec.reserve(MCProbeDivisions.size());
  for (auto &ProbeSec : MCProbeDivisions)
    Vec.emplace_back(ProbeSec.first, &ProbeSec.second);
  for (auto I : llvm::enumerate(MCOS->getAssembler()))
    I.value().setOrdinal(I.index());
  llvm::sort(Vec, [](auto A, auto B) {
    return A.first->getSection().getOrdinal() <
           B.first->getSection().getOrdinal();
  });
  for (auto [FuncSym, RootPtr] : Vec) {
    const auto &Root = *RootPtr;
    if (auto *S = Ctx.getObjectFileInfo()->getPseudoProbeSection(
            FuncSym->getSection())) {
      // Switch to the .pseudoprobe section or a comdat group.
      MCOS->switchSection(S);
      // Emit probes grouped by GUID.
      // Emit sorted descendant. InlineSite is unique for each pair, so there
      // will be no ordering of Inlinee based on MCPseudoProbeInlineTree*
      using InlineeType = std::pair<InlineSite, MCPseudoProbeInlineTree *>;
      std::vector<InlineeType> Inlinees;
      for (const auto &Child : Root.getChildren())
        Inlinees.emplace_back(Child.first, Child.second.get());
      llvm::sort(Inlinees, llvm::less_first());

      for (const auto &Inlinee : Inlinees) {
        // Emit the group guarded by a sentinel probe.
        MCPseudoProbe SentinelProbe(
            const_cast<MCSymbol *>(FuncSym), MD5Hash(FuncSym->getName()),
            (uint32_t)PseudoProbeReservedId::Invalid,
            (uint32_t)PseudoProbeType::Block,
            (uint32_t)PseudoProbeAttributes::Sentinel, 0);
        const MCPseudoProbe *Probe = &SentinelProbe;
        Inlinee.second->emit(MCOS, Probe);
      }
    }
  }
}

//
// This emits the pseudo probe tables.
//
void MCPseudoProbeTable::emit(MCObjectStreamer *MCOS) {
  MCContext &Ctx = MCOS->getContext();
  auto &ProbeTable = Ctx.getMCPseudoProbeTable();

  // Bail out early so we don't switch to the pseudo_probe section needlessly
  // and in doing so create an unnecessary (if empty) section.
  auto &ProbeSections = ProbeTable.getProbeSections();
  if (ProbeSections.empty())
    return;

  LLVM_DEBUG(MCPseudoProbeTable::DdgPrintIndent = 0);

  // Put out the probe.
  ProbeSections.emit(MCOS);
}

static StringRef getProbeFNameForGUID(const GUIDProbeFunctionMap &GUID2FuncMAP,
                                      uint64_t GUID) {
  auto It = GUID2FuncMAP.find(GUID);
  assert(It != GUID2FuncMAP.end() &&
         "Probe function must exist for a valid GUID");
  return It->second.FuncName;
}

void MCPseudoProbeFuncDesc::print(raw_ostream &OS) {
  OS << "GUID: " << FuncGUID << " Name: " << FuncName << "\n";
  OS << "Hash: " << FuncHash << "\n";
}

void MCDecodedPseudoProbe::getInlineContext(
    SmallVectorImpl<MCPseudoProbeFrameLocation> &ContextStack,
    const GUIDProbeFunctionMap &GUID2FuncMAP) const {
  uint32_t Begin = ContextStack.size();
  MCDecodedPseudoProbeInlineTree *Cur = InlineTree;
  // It will add the string of each node's inline site during iteration.
  // Note that it won't include the probe's belonging function(leaf location)
  while (Cur->hasInlineSite()) {
    StringRef FuncName = getProbeFNameForGUID(GUID2FuncMAP, Cur->Parent->Guid);
    ContextStack.emplace_back(
        MCPseudoProbeFrameLocation(FuncName, std::get<1>(Cur->ISite)));
    Cur = static_cast<MCDecodedPseudoProbeInlineTree *>(Cur->Parent);
  }
  // Make the ContextStack in caller-callee order
  std::reverse(ContextStack.begin() + Begin, ContextStack.end());
}

std::string MCDecodedPseudoProbe::getInlineContextStr(
    const GUIDProbeFunctionMap &GUID2FuncMAP) const {
  std::ostringstream OContextStr;
  SmallVector<MCPseudoProbeFrameLocation, 16> ContextStack;
  getInlineContext(ContextStack, GUID2FuncMAP);
  for (auto &Cxt : ContextStack) {
    if (OContextStr.str().size())
      OContextStr << " @ ";
    OContextStr << Cxt.first.str() << ":" << Cxt.second;
  }
  return OContextStr.str();
}

static const char *PseudoProbeTypeStr[3] = {"Block", "IndirectCall",
                                            "DirectCall"};

void MCDecodedPseudoProbe::print(raw_ostream &OS,
                                 const GUIDProbeFunctionMap &GUID2FuncMAP,
                                 bool ShowName) const {
  OS << "FUNC: ";
  if (ShowName) {
    StringRef FuncName = getProbeFNameForGUID(GUID2FuncMAP, Guid);
    OS << FuncName.str() << " ";
  } else {
    OS << Guid << " ";
  }
  OS << "Index: " << Index << "  ";
  if (Discriminator)
    OS << "Discriminator: " << Discriminator << "  ";
  OS << "Type: " << PseudoProbeTypeStr[static_cast<uint8_t>(Type)] << "  ";
  std::string InlineContextStr = getInlineContextStr(GUID2FuncMAP);
  if (InlineContextStr.size()) {
    OS << "Inlined: @ ";
    OS << InlineContextStr;
  }
  OS << "\n";
}

template <typename T> ErrorOr<T> MCPseudoProbeDecoder::readUnencodedNumber() {
  if (Data + sizeof(T) > End) {
    return std::error_code();
  }
  T Val = endian::readNext<T, llvm::endianness::little>(Data);
  return ErrorOr<T>(Val);
}

template <typename T> ErrorOr<T> MCPseudoProbeDecoder::readUnsignedNumber() {
  unsigned NumBytesRead = 0;
  uint64_t Val = decodeULEB128(Data, &NumBytesRead);
  if (Val > std::numeric_limits<T>::max() || (Data + NumBytesRead > End)) {
    return std::error_code();
  }
  Data += NumBytesRead;
  return ErrorOr<T>(static_cast<T>(Val));
}

template <typename T> ErrorOr<T> MCPseudoProbeDecoder::readSignedNumber() {
  unsigned NumBytesRead = 0;
  int64_t Val = decodeSLEB128(Data, &NumBytesRead);
  if (Val > std::numeric_limits<T>::max() || (Data + NumBytesRead > End)) {
    return std::error_code();
  }
  Data += NumBytesRead;
  return ErrorOr<T>(static_cast<T>(Val));
}

ErrorOr<StringRef> MCPseudoProbeDecoder::readString(uint32_t Size) {
  StringRef Str(reinterpret_cast<const char *>(Data), Size);
  if (Data + Size > End) {
    return std::error_code();
  }
  Data += Size;
  return ErrorOr<StringRef>(Str);
}

bool MCPseudoProbeDecoder::buildGUID2FuncDescMap(const uint8_t *Start,
                                                 std::size_t Size) {
  // The pseudo_probe_desc section has a format like:
  // .section .pseudo_probe_desc,"",@progbits
  // .quad -5182264717993193164   // GUID
  // .quad 4294967295             // Hash
  // .uleb 3                      // Name size
  // .ascii "foo"                 // Name
  // .quad -2624081020897602054
  // .quad 174696971957
  // .uleb 34
  // .ascii "main"

  Data = Start;
  End = Data + Size;

  while (Data < End) {
    auto ErrorOrGUID = readUnencodedNumber<uint64_t>();
    if (!ErrorOrGUID)
      return false;

    auto ErrorOrHash = readUnencodedNumber<uint64_t>();
    if (!ErrorOrHash)
      return false;

    auto ErrorOrNameSize = readUnsignedNumber<uint32_t>();
    if (!ErrorOrNameSize)
      return false;
    uint32_t NameSize = std::move(*ErrorOrNameSize);

    auto ErrorOrName = readString(NameSize);
    if (!ErrorOrName)
      return false;

    uint64_t GUID = std::move(*ErrorOrGUID);
    uint64_t Hash = std::move(*ErrorOrHash);
    StringRef Name = std::move(*ErrorOrName);

    // Initialize PseudoProbeFuncDesc and populate it into GUID2FuncDescMap
    GUID2FuncDescMap.emplace(GUID, MCPseudoProbeFuncDesc(GUID, Hash, Name));
  }
  assert(Data == End && "Have unprocessed data in pseudo_probe_desc section");
  return true;
}

bool MCPseudoProbeDecoder::buildAddress2ProbeMap(
    MCDecodedPseudoProbeInlineTree *Cur, uint64_t &LastAddr,
    const Uint64Set &GuidFilter, const Uint64Map &FuncStartAddrs) {
  // The pseudo_probe section encodes an inline forest and each tree has a
  // format defined in MCPseudoProbe.h

  uint32_t Index = 0;
  bool IsTopLevelFunc = Cur == &DummyInlineRoot;
  if (IsTopLevelFunc) {
    // Use a sequential id for top level inliner.
    Index = Cur->getChildren().size();
  } else {
    // Read inline site for inlinees
    auto ErrorOrIndex = readUnsignedNumber<uint32_t>();
    if (!ErrorOrIndex)
      return false;
    Index = std::move(*ErrorOrIndex);
  }

  // Read guid
  auto ErrorOrCurGuid = readUnencodedNumber<uint64_t>();
  if (!ErrorOrCurGuid)
    return false;
  uint64_t Guid = std::move(*ErrorOrCurGuid);

  // Decide if top-level node should be disgarded.
  if (IsTopLevelFunc && !GuidFilter.empty() && !GuidFilter.count(Guid))
    Cur = nullptr;

  // If the incoming node is null, all its children nodes should be disgarded.
  if (Cur) {
    // Switch/add to a new tree node(inlinee)
    Cur = Cur->getOrAddNode(std::make_tuple(Guid, Index));
    Cur->Guid = Guid;
    if (IsTopLevelFunc && !EncodingIsAddrBased) {
      if (auto V = FuncStartAddrs.lookup(Guid))
        LastAddr = V;
    }
  }

  // Read number of probes in the current node.
  auto ErrorOrNodeCount = readUnsignedNumber<uint32_t>();
  if (!ErrorOrNodeCount)
    return false;
  uint32_t NodeCount = std::move(*ErrorOrNodeCount);
  // Read number of direct inlinees
  auto ErrorOrCurChildrenToProcess = readUnsignedNumber<uint32_t>();
  if (!ErrorOrCurChildrenToProcess)
    return false;
  // Read all probes in this node
  for (std::size_t I = 0; I < NodeCount; I++) {
    // Read index
    auto ErrorOrIndex = readUnsignedNumber<uint32_t>();
    if (!ErrorOrIndex)
      return false;
    uint32_t Index = std::move(*ErrorOrIndex);
    // Read type | flag.
    auto ErrorOrValue = readUnencodedNumber<uint8_t>();
    if (!ErrorOrValue)
      return false;
    uint8_t Value = std::move(*ErrorOrValue);
    uint8_t Kind = Value & 0xf;
    uint8_t Attr = (Value & 0x70) >> 4;
    // Read address
    uint64_t Addr = 0;
    if (Value & 0x80) {
      auto ErrorOrOffset = readSignedNumber<int64_t>();
      if (!ErrorOrOffset)
        return false;
      int64_t Offset = std::move(*ErrorOrOffset);
      Addr = LastAddr + Offset;
    } else {
      auto ErrorOrAddr = readUnencodedNumber<int64_t>();
      if (!ErrorOrAddr)
        return false;
      Addr = std::move(*ErrorOrAddr);
      if (isSentinelProbe(Attr)) {
        // For sentinel probe, the addr field actually stores the GUID of the
        // split function. Convert it to the real address.
        if (auto V = FuncStartAddrs.lookup(Addr))
          Addr = V;
      } else {
        // For now we assume all probe encoding should be either based on
        // leading probe address or function start address.
        // The scheme is for downwards compatibility.
        // TODO: retire this scheme once compatibility is no longer an issue.
        EncodingIsAddrBased = true;
      }
    }

    uint32_t Discriminator = 0;
    if (hasDiscriminator(Attr)) {
      auto ErrorOrDiscriminator = readUnsignedNumber<uint32_t>();
      if (!ErrorOrDiscriminator)
        return false;
      Discriminator = std::move(*ErrorOrDiscriminator);
    }

    if (Cur && !isSentinelProbe(Attr)) {
      // Populate Address2ProbesMap
      auto &Probes = Address2ProbesMap[Addr];
      Probes.emplace_back(Addr, Cur->Guid, Index, PseudoProbeType(Kind), Attr,
                          Discriminator, Cur);
      Cur->addProbes(&Probes.back());
    }
    LastAddr = Addr;
  }

  uint32_t ChildrenToProcess = std::move(*ErrorOrCurChildrenToProcess);
  for (uint32_t I = 0; I < ChildrenToProcess; I++) {
    buildAddress2ProbeMap(Cur, LastAddr, GuidFilter, FuncStartAddrs);
  }

  return true;
}

bool MCPseudoProbeDecoder::buildAddress2ProbeMap(
    const uint8_t *Start, std::size_t Size, const Uint64Set &GuidFilter,
    const Uint64Map &FuncStartAddrs) {
  Data = Start;
  End = Data + Size;
  uint64_t LastAddr = 0;
  while (Data < End)
    buildAddress2ProbeMap(&DummyInlineRoot, LastAddr, GuidFilter,
                          FuncStartAddrs);
  assert(Data == End && "Have unprocessed data in pseudo_probe section");
  return true;
}

void MCPseudoProbeDecoder::printGUID2FuncDescMap(raw_ostream &OS) {
  OS << "Pseudo Probe Desc:\n";
  // Make the output deterministic
  std::map<uint64_t, MCPseudoProbeFuncDesc> OrderedMap(GUID2FuncDescMap.begin(),
                                                       GUID2FuncDescMap.end());
  for (auto &I : OrderedMap) {
    I.second.print(OS);
  }
}

void MCPseudoProbeDecoder::printProbeForAddress(raw_ostream &OS,
                                                uint64_t Address) {
  auto It = Address2ProbesMap.find(Address);
  if (It != Address2ProbesMap.end()) {
    for (auto &Probe : It->second) {
      OS << " [Probe]:\t";
      Probe.print(OS, GUID2FuncDescMap, true);
    }
  }
}

void MCPseudoProbeDecoder::printProbesForAllAddresses(raw_ostream &OS) {
  auto Entries = make_first_range(Address2ProbesMap);
  SmallVector<uint64_t, 0> Addresses(Entries.begin(), Entries.end());
  llvm::sort(Addresses);
  for (auto K : Addresses) {
    OS << "Address:\t";
    OS << K;
    OS << "\n";
    printProbeForAddress(OS, K);
  }
}

const MCDecodedPseudoProbe *
MCPseudoProbeDecoder::getCallProbeForAddr(uint64_t Address) const {
  auto It = Address2ProbesMap.find(Address);
  if (It == Address2ProbesMap.end())
    return nullptr;
  const auto &Probes = It->second;

  const MCDecodedPseudoProbe *CallProbe = nullptr;
  for (const auto &Probe : Probes) {
    if (Probe.isCall()) {
      // Disabling the assert and returning first call probe seen so far.
      // Subsequent call probes, if any, are ignored. Due to the the way
      // .pseudo_probe section is decoded, probes of the same-named independent
      // static functions are merged thus multiple call probes may be seen for a
      // callsite. This should only happen to compiler-generated statics, with
      // -funique-internal-linkage-names where user statics get unique names.
      //
      // TODO: re-enable or narrow down the assert to static functions only.
      //
      // assert(!CallProbe &&
      //        "There should be only one call probe corresponding to address "
      //        "which is a callsite.");
      CallProbe = &Probe;
      break;
    }
  }
  return CallProbe;
}

const MCPseudoProbeFuncDesc *
MCPseudoProbeDecoder::getFuncDescForGUID(uint64_t GUID) const {
  auto It = GUID2FuncDescMap.find(GUID);
  assert(It != GUID2FuncDescMap.end() && "Function descriptor doesn't exist");
  return &It->second;
}

void MCPseudoProbeDecoder::getInlineContextForProbe(
    const MCDecodedPseudoProbe *Probe,
    SmallVectorImpl<MCPseudoProbeFrameLocation> &InlineContextStack,
    bool IncludeLeaf) const {
  Probe->getInlineContext(InlineContextStack, GUID2FuncDescMap);
  if (!IncludeLeaf)
    return;
  // Note that the context from probe doesn't include leaf frame,
  // hence we need to retrieve and prepend leaf if requested.
  const auto *FuncDesc = getFuncDescForGUID(Probe->getGuid());
  InlineContextStack.emplace_back(
      MCPseudoProbeFrameLocation(FuncDesc->FuncName, Probe->getIndex()));
}

const MCPseudoProbeFuncDesc *MCPseudoProbeDecoder::getInlinerDescForProbe(
    const MCDecodedPseudoProbe *Probe) const {
  MCDecodedPseudoProbeInlineTree *InlinerNode = Probe->getInlineTreeNode();
  if (!InlinerNode->hasInlineSite())
    return nullptr;
  return getFuncDescForGUID(InlinerNode->Parent->Guid);
}
