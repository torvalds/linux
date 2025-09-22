//===-- lib/DebugInfo/Symbolize/MarkupFilter.cpp -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the implementation of a filter that replaces symbolizer
/// markup with human-readable expressions.
///
/// See https://llvm.org/docs/SymbolizerMarkupFormat.html
///
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/Symbolize/MarkupFilter.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/Symbolize/Markup.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/Debuginfod/Debuginfod.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace llvm;
using namespace llvm::symbolize;

MarkupFilter::MarkupFilter(raw_ostream &OS, LLVMSymbolizer &Symbolizer,
                           std::optional<bool> ColorsEnabled)
    : OS(OS), Symbolizer(Symbolizer),
      ColorsEnabled(
          ColorsEnabled.value_or(WithColor::defaultAutoDetectFunction()(OS))) {}

void MarkupFilter::filter(std::string &&InputLine) {
  Line = std::move(InputLine);
  resetColor();

  Parser.parseLine(Line);
  SmallVector<MarkupNode> DeferredNodes;
  // See if the line is a contextual (i.e. contains a contextual element).
  // In this case, anything after the contextual element is elided, or the whole
  // line may be elided.
  while (std::optional<MarkupNode> Node = Parser.nextNode()) {
    // If this was a contextual line, then summarily stop processing.
    if (tryContextualElement(*Node, DeferredNodes))
      return;
    // This node may yet be part of an elided contextual line.
    DeferredNodes.push_back(*Node);
  }

  // This was not a contextual line, so nothing in it should be elided.
  endAnyModuleInfoLine();
  for (const MarkupNode &Node : DeferredNodes)
    filterNode(Node);
}

void MarkupFilter::finish() {
  Parser.flush();
  while (std::optional<MarkupNode> Node = Parser.nextNode())
    filterNode(*Node);
  endAnyModuleInfoLine();
  resetColor();
  Modules.clear();
  MMaps.clear();
}

// See if the given node is a contextual element and handle it if so. This may
// either output or defer the element; in the former case, it will first emit
// any DeferredNodes.
//
// Returns true if the given element was a contextual element. In this case,
// DeferredNodes should be considered handled and should not be emitted. The
// rest of the containing line must also be ignored in case the element was
// deferred to a following line.
bool MarkupFilter::tryContextualElement(
    const MarkupNode &Node, const SmallVector<MarkupNode> &DeferredNodes) {
  if (tryMMap(Node, DeferredNodes))
    return true;
  if (tryReset(Node, DeferredNodes))
    return true;
  return tryModule(Node, DeferredNodes);
}

bool MarkupFilter::tryMMap(const MarkupNode &Node,
                           const SmallVector<MarkupNode> &DeferredNodes) {
  if (Node.Tag != "mmap")
    return false;
  std::optional<MMap> ParsedMMap = parseMMap(Node);
  if (!ParsedMMap)
    return true;

  if (const MMap *M = getOverlappingMMap(*ParsedMMap)) {
    WithColor::error(errs())
        << formatv("overlapping mmap: #{0:x} [{1:x}-{2:x}]\n", M->Mod->ID,
                   M->Addr, M->Addr + M->Size - 1);
    reportLocation(Node.Fields[0].begin());
    return true;
  }

  auto Res = MMaps.emplace(ParsedMMap->Addr, std::move(*ParsedMMap));
  assert(Res.second && "Overlap check should ensure emplace succeeds.");
  MMap &MMap = Res.first->second;

  if (!MIL || MIL->Mod != MMap.Mod) {
    endAnyModuleInfoLine();
    for (const MarkupNode &Node : DeferredNodes)
      filterNode(Node);
    beginModuleInfoLine(MMap.Mod);
    OS << "; adds";
  }
  MIL->MMaps.push_back(&MMap);
  return true;
}

bool MarkupFilter::tryReset(const MarkupNode &Node,
                            const SmallVector<MarkupNode> &DeferredNodes) {
  if (Node.Tag != "reset")
    return false;
  if (!checkNumFields(Node, 0))
    return true;

  if (!Modules.empty() || !MMaps.empty()) {
    endAnyModuleInfoLine();
    for (const MarkupNode &Node : DeferredNodes)
      filterNode(Node);
    printRawElement(Node);
    OS << lineEnding();

    Modules.clear();
    MMaps.clear();
  }
  return true;
}

bool MarkupFilter::tryModule(const MarkupNode &Node,
                             const SmallVector<MarkupNode> &DeferredNodes) {
  if (Node.Tag != "module")
    return false;
  std::optional<Module> ParsedModule = parseModule(Node);
  if (!ParsedModule)
    return true;

  auto Res = Modules.try_emplace(
      ParsedModule->ID, std::make_unique<Module>(std::move(*ParsedModule)));
  if (!Res.second) {
    WithColor::error(errs()) << "duplicate module ID\n";
    reportLocation(Node.Fields[0].begin());
    return true;
  }
  Module &Module = *Res.first->second;

  endAnyModuleInfoLine();
  for (const MarkupNode &Node : DeferredNodes)
    filterNode(Node);
  beginModuleInfoLine(&Module);
  OS << "; BuildID=";
  printValue(toHex(Module.BuildID, /*LowerCase=*/true));
  return true;
}

void MarkupFilter::beginModuleInfoLine(const Module *M) {
  highlight();
  OS << "[[[ELF module";
  printValue(formatv(" #{0:x} ", M->ID));
  OS << '"';
  printValue(M->Name);
  OS << '"';
  MIL = ModuleInfoLine{M};
}

void MarkupFilter::endAnyModuleInfoLine() {
  if (!MIL)
    return;
  llvm::stable_sort(MIL->MMaps, [](const MMap *A, const MMap *B) {
    return A->Addr < B->Addr;
  });
  for (const MMap *M : MIL->MMaps) {
    OS << (M == MIL->MMaps.front() ? ' ' : ',');
    OS << '[';
    printValue(formatv("{0:x}", M->Addr));
    OS << '-';
    printValue(formatv("{0:x}", M->Addr + M->Size - 1));
    OS << "](";
    printValue(M->Mode);
    OS << ')';
  }
  OS << "]]]" << lineEnding();
  restoreColor();
  MIL.reset();
}

// Handle a node that is known not to be a contextual element.
void MarkupFilter::filterNode(const MarkupNode &Node) {
  if (!checkTag(Node))
    return;
  if (tryPresentation(Node))
    return;
  if (trySGR(Node))
    return;

  OS << Node.Text;
}

bool MarkupFilter::tryPresentation(const MarkupNode &Node) {
  if (trySymbol(Node))
    return true;
  if (tryPC(Node))
    return true;
  if (tryBackTrace(Node))
    return true;
  return tryData(Node);
}

bool MarkupFilter::trySymbol(const MarkupNode &Node) {
  if (Node.Tag != "symbol")
    return false;
  if (!checkNumFields(Node, 1))
    return true;

  highlight();
  OS << llvm::demangle(Node.Fields.front().str());
  restoreColor();
  return true;
}

bool MarkupFilter::tryPC(const MarkupNode &Node) {
  if (Node.Tag != "pc")
    return false;
  if (!checkNumFieldsAtLeast(Node, 1))
    return true;
  warnNumFieldsAtMost(Node, 2);

  std::optional<uint64_t> Addr = parseAddr(Node.Fields[0]);
  if (!Addr)
    return true;

  // PC addresses that aren't part of a backtrace are assumed to be precise code
  // locations.
  PCType Type = PCType::PreciseCode;
  if (Node.Fields.size() == 2) {
    std::optional<PCType> ParsedType = parsePCType(Node.Fields[1]);
    if (!ParsedType)
      return true;
    Type = *ParsedType;
  }
  *Addr = adjustAddr(*Addr, Type);

  const MMap *MMap = getContainingMMap(*Addr);
  if (!MMap) {
    WithColor::error() << "no mmap covers address\n";
    reportLocation(Node.Fields[0].begin());
    printRawElement(Node);
    return true;
  }

  Expected<DILineInfo> LI = Symbolizer.symbolizeCode(
      MMap->Mod->BuildID, {MMap->getModuleRelativeAddr(*Addr)});
  if (!LI) {
    WithColor::defaultErrorHandler(LI.takeError());
    printRawElement(Node);
    return true;
  }
  if (!*LI) {
    printRawElement(Node);
    return true;
  }

  highlight();
  printValue(LI->FunctionName);
  OS << '[';
  printValue(LI->FileName);
  OS << ':';
  printValue(Twine(LI->Line));
  OS << ']';
  restoreColor();
  return true;
}

bool MarkupFilter::tryBackTrace(const MarkupNode &Node) {
  if (Node.Tag != "bt")
    return false;
  if (!checkNumFieldsAtLeast(Node, 2))
    return true;
  warnNumFieldsAtMost(Node, 3);

  std::optional<uint64_t> FrameNumber = parseFrameNumber(Node.Fields[0]);
  if (!FrameNumber)
    return true;

  std::optional<uint64_t> Addr = parseAddr(Node.Fields[1]);
  if (!Addr)
    return true;

  // Backtrace addresses are assumed to be return addresses by default.
  PCType Type = PCType::ReturnAddress;
  if (Node.Fields.size() == 3) {
    std::optional<PCType> ParsedType = parsePCType(Node.Fields[2]);
    if (!ParsedType)
      return true;
    Type = *ParsedType;
  }
  *Addr = adjustAddr(*Addr, Type);

  const MMap *MMap = getContainingMMap(*Addr);
  if (!MMap) {
    WithColor::error() << "no mmap covers address\n";
    reportLocation(Node.Fields[0].begin());
    printRawElement(Node);
    return true;
  }
  uint64_t MRA = MMap->getModuleRelativeAddr(*Addr);

  Expected<DIInliningInfo> II =
      Symbolizer.symbolizeInlinedCode(MMap->Mod->BuildID, {MRA});
  if (!II) {
    WithColor::defaultErrorHandler(II.takeError());
    printRawElement(Node);
    return true;
  }

  highlight();
  for (unsigned I = 0, E = II->getNumberOfFrames(); I != E; ++I) {
    auto Header = formatv("{0, +6}", formatv("#{0}", FrameNumber)).sstr<16>();
    // Don't highlight the # sign as a value.
    size_t NumberIdx = Header.find("#") + 1;
    OS << Header.substr(0, NumberIdx);
    printValue(Header.substr(NumberIdx));
    if (I == E - 1) {
      OS << "   ";
    } else {
      OS << '.';
      printValue(formatv("{0, -2}", I + 1));
    }
    printValue(formatv(" {0:x16} ", *Addr));

    DILineInfo LI = II->getFrame(I);
    if (LI) {
      printValue(LI.FunctionName);
      OS << ' ';
      printValue(LI.FileName);
      OS << ':';
      printValue(Twine(LI.Line));
      OS << ':';
      printValue(Twine(LI.Column));
      OS << ' ';
    }
    OS << '(';
    printValue(MMap->Mod->Name);
    OS << "+";
    printValue(formatv("{0:x}", MRA));
    OS << ')';
    if (I != E - 1)
      OS << lineEnding();
  }
  restoreColor();
  return true;
}

bool MarkupFilter::tryData(const MarkupNode &Node) {
  if (Node.Tag != "data")
    return false;
  if (!checkNumFields(Node, 1))
    return true;
  std::optional<uint64_t> Addr = parseAddr(Node.Fields[0]);
  if (!Addr)
    return true;

  const MMap *MMap = getContainingMMap(*Addr);
  if (!MMap) {
    WithColor::error() << "no mmap covers address\n";
    reportLocation(Node.Fields[0].begin());
    printRawElement(Node);
    return true;
  }

  Expected<DIGlobal> Symbol = Symbolizer.symbolizeData(
      MMap->Mod->BuildID, {MMap->getModuleRelativeAddr(*Addr)});
  if (!Symbol) {
    WithColor::defaultErrorHandler(Symbol.takeError());
    printRawElement(Node);
    return true;
  }

  highlight();
  OS << Symbol->Name;
  restoreColor();
  return true;
}

bool MarkupFilter::trySGR(const MarkupNode &Node) {
  if (Node.Text == "\033[0m") {
    resetColor();
    return true;
  }
  if (Node.Text == "\033[1m") {
    Bold = true;
    if (ColorsEnabled)
      OS.changeColor(raw_ostream::Colors::SAVEDCOLOR, Bold);
    return true;
  }
  auto SGRColor = StringSwitch<std::optional<raw_ostream::Colors>>(Node.Text)
                      .Case("\033[30m", raw_ostream::Colors::BLACK)
                      .Case("\033[31m", raw_ostream::Colors::RED)
                      .Case("\033[32m", raw_ostream::Colors::GREEN)
                      .Case("\033[33m", raw_ostream::Colors::YELLOW)
                      .Case("\033[34m", raw_ostream::Colors::BLUE)
                      .Case("\033[35m", raw_ostream::Colors::MAGENTA)
                      .Case("\033[36m", raw_ostream::Colors::CYAN)
                      .Case("\033[37m", raw_ostream::Colors::WHITE)
                      .Default(std::nullopt);
  if (SGRColor) {
    Color = *SGRColor;
    if (ColorsEnabled)
      OS.changeColor(*Color);
    return true;
  }

  return false;
}

// Begin highlighting text by picking a different color than the current color
// state.
void MarkupFilter::highlight() {
  if (!ColorsEnabled)
    return;
  OS.changeColor(Color == raw_ostream::Colors::BLUE ? raw_ostream::Colors::CYAN
                                                    : raw_ostream::Colors::BLUE,
                 Bold);
}

// Begin highlighting a field within a highlighted markup string.
void MarkupFilter::highlightValue() {
  if (!ColorsEnabled)
    return;
  OS.changeColor(raw_ostream::Colors::GREEN, Bold);
}

// Set the output stream's color to the current color and bold state of the SGR
// abstract machine.
void MarkupFilter::restoreColor() {
  if (!ColorsEnabled)
    return;
  if (Color) {
    OS.changeColor(*Color, Bold);
  } else {
    OS.resetColor();
    if (Bold)
      OS.changeColor(raw_ostream::Colors::SAVEDCOLOR, Bold);
  }
}

// Set the SGR and output stream's color and bold states back to the default.
void MarkupFilter::resetColor() {
  if (!Color && !Bold)
    return;
  Color.reset();
  Bold = false;
  if (ColorsEnabled)
    OS.resetColor();
}

void MarkupFilter::printRawElement(const MarkupNode &Element) {
  highlight();
  OS << "[[[";
  printValue(Element.Tag);
  for (StringRef Field : Element.Fields) {
    OS << ':';
    printValue(Field);
  }
  OS << "]]]";
  restoreColor();
}

void MarkupFilter::printValue(Twine Value) {
  highlightValue();
  OS << Value;
  highlight();
}

// This macro helps reduce the amount of indirection done through Optional
// below, since the usual case upon returning a std::nullopt Optional is to
// return std::nullopt.
#define ASSIGN_OR_RETURN_NONE(TYPE, NAME, EXPR)                                \
  auto NAME##Opt = (EXPR);                                                     \
  if (!NAME##Opt)                                                              \
    return std::nullopt;                                                       \
  TYPE NAME = std::move(*NAME##Opt)

std::optional<MarkupFilter::Module>
MarkupFilter::parseModule(const MarkupNode &Element) const {
  if (!checkNumFieldsAtLeast(Element, 3))
    return std::nullopt;
  ASSIGN_OR_RETURN_NONE(uint64_t, ID, parseModuleID(Element.Fields[0]));
  StringRef Name = Element.Fields[1];
  StringRef Type = Element.Fields[2];
  if (Type != "elf") {
    WithColor::error() << "unknown module type\n";
    reportLocation(Type.begin());
    return std::nullopt;
  }
  if (!checkNumFields(Element, 4))
    return std::nullopt;
  SmallVector<uint8_t> BuildID = parseBuildID(Element.Fields[3]);
  if (BuildID.empty())
    return std::nullopt;
  return Module{ID, Name.str(), std::move(BuildID)};
}

std::optional<MarkupFilter::MMap>
MarkupFilter::parseMMap(const MarkupNode &Element) const {
  if (!checkNumFieldsAtLeast(Element, 3))
    return std::nullopt;
  ASSIGN_OR_RETURN_NONE(uint64_t, Addr, parseAddr(Element.Fields[0]));
  ASSIGN_OR_RETURN_NONE(uint64_t, Size, parseSize(Element.Fields[1]));
  StringRef Type = Element.Fields[2];
  if (Type != "load") {
    WithColor::error() << "unknown mmap type\n";
    reportLocation(Type.begin());
    return std::nullopt;
  }
  if (!checkNumFields(Element, 6))
    return std::nullopt;
  ASSIGN_OR_RETURN_NONE(uint64_t, ID, parseModuleID(Element.Fields[3]));
  ASSIGN_OR_RETURN_NONE(std::string, Mode, parseMode(Element.Fields[4]));
  auto It = Modules.find(ID);
  if (It == Modules.end()) {
    WithColor::error() << "unknown module ID\n";
    reportLocation(Element.Fields[3].begin());
    return std::nullopt;
  }
  ASSIGN_OR_RETURN_NONE(uint64_t, ModuleRelativeAddr,
                        parseAddr(Element.Fields[5]));
  return MMap{Addr, Size, It->second.get(), std::move(Mode),
              ModuleRelativeAddr};
}

// Parse an address (%p in the spec).
std::optional<uint64_t> MarkupFilter::parseAddr(StringRef Str) const {
  if (Str.empty()) {
    reportTypeError(Str, "address");
    return std::nullopt;
  }
  if (all_of(Str, [](char C) { return C == '0'; }))
    return 0;
  if (!Str.starts_with("0x")) {
    reportTypeError(Str, "address");
    return std::nullopt;
  }
  uint64_t Addr;
  if (Str.drop_front(2).getAsInteger(16, Addr)) {
    reportTypeError(Str, "address");
    return std::nullopt;
  }
  return Addr;
}

// Parse a module ID (%i in the spec).
std::optional<uint64_t> MarkupFilter::parseModuleID(StringRef Str) const {
  uint64_t ID;
  if (Str.getAsInteger(0, ID)) {
    reportTypeError(Str, "module ID");
    return std::nullopt;
  }
  return ID;
}

// Parse a size (%i in the spec).
std::optional<uint64_t> MarkupFilter::parseSize(StringRef Str) const {
  uint64_t ID;
  if (Str.getAsInteger(0, ID)) {
    reportTypeError(Str, "size");
    return std::nullopt;
  }
  return ID;
}

// Parse a frame number (%i in the spec).
std::optional<uint64_t> MarkupFilter::parseFrameNumber(StringRef Str) const {
  uint64_t ID;
  if (Str.getAsInteger(10, ID)) {
    reportTypeError(Str, "frame number");
    return std::nullopt;
  }
  return ID;
}

// Parse a build ID (%x in the spec).
object::BuildID MarkupFilter::parseBuildID(StringRef Str) const {
  object::BuildID BID = llvm::object::parseBuildID(Str);
  if (BID.empty())
    reportTypeError(Str, "build ID");
  return BID;
}

// Parses the mode string for an mmap element.
std::optional<std::string> MarkupFilter::parseMode(StringRef Str) const {
  if (Str.empty()) {
    reportTypeError(Str, "mode");
    return std::nullopt;
  }

  // Pop off each of r/R, w/W, and x/X from the front, in that order.
  StringRef Remainder = Str;
  Remainder.consume_front_insensitive("r");
  Remainder.consume_front_insensitive("w");
  Remainder.consume_front_insensitive("x");

  // If anything remains, then the string wasn't a mode.
  if (!Remainder.empty()) {
    reportTypeError(Str, "mode");
    return std::nullopt;
  }

  // Normalize the mode.
  return Str.lower();
}

std::optional<MarkupFilter::PCType>
MarkupFilter::parsePCType(StringRef Str) const {
  std::optional<MarkupFilter::PCType> Type =
      StringSwitch<std::optional<MarkupFilter::PCType>>(Str)
          .Case("ra", MarkupFilter::PCType::ReturnAddress)
          .Case("pc", MarkupFilter::PCType::PreciseCode)
          .Default(std::nullopt);
  if (!Type)
    reportTypeError(Str, "PC type");
  return Type;
}

bool MarkupFilter::checkTag(const MarkupNode &Node) const {
  if (any_of(Node.Tag, [](char C) { return C < 'a' || C > 'z'; })) {
    WithColor::error(errs()) << "tags must be all lowercase characters\n";
    reportLocation(Node.Tag.begin());
    return false;
  }
  return true;
}

bool MarkupFilter::checkNumFields(const MarkupNode &Element,
                                  size_t Size) const {
  if (Element.Fields.size() != Size) {
    bool Warn = Element.Fields.size() > Size;
    WithColor(errs(), Warn ? HighlightColor::Warning : HighlightColor::Error)
        << (Warn ? "warning: " : "error: ") << "expected " << Size
        << " field(s); found " << Element.Fields.size() << "\n";
    reportLocation(Element.Tag.end());
    return Warn;
  }
  return true;
}

bool MarkupFilter::checkNumFieldsAtLeast(const MarkupNode &Element,
                                         size_t Size) const {
  if (Element.Fields.size() < Size) {
    WithColor::error(errs())
        << "expected at least " << Size << " field(s); found "
        << Element.Fields.size() << "\n";
    reportLocation(Element.Tag.end());
    return false;
  }
  return true;
}

void MarkupFilter::warnNumFieldsAtMost(const MarkupNode &Element,
                                       size_t Size) const {
  if (Element.Fields.size() <= Size)
    return;
  WithColor::warning(errs())
      << "expected at most " << Size << " field(s); found "
      << Element.Fields.size() << "\n";
  reportLocation(Element.Tag.end());
}

void MarkupFilter::reportTypeError(StringRef Str, StringRef TypeName) const {
  WithColor::error(errs()) << "expected " << TypeName << "; found '" << Str
                           << "'\n";
  reportLocation(Str.begin());
}

// Prints two lines that point out the given location in the current Line using
// a caret. The iterator must be within the bounds of the most recent line
// passed to beginLine().
void MarkupFilter::reportLocation(StringRef::iterator Loc) const {
  errs() << Line;
  WithColor(errs().indent(Loc - StringRef(Line).begin()),
            HighlightColor::String)
      << '^';
  errs() << '\n';
}

// Checks for an existing mmap that overlaps the given one and returns a
// pointer to one of them.
const MarkupFilter::MMap *
MarkupFilter::getOverlappingMMap(const MMap &Map) const {
  // If the given map contains the start of another mmap, they overlap.
  auto I = MMaps.upper_bound(Map.Addr);
  if (I != MMaps.end() && Map.contains(I->second.Addr))
    return &I->second;

  // If no element starts inside the given mmap, the only possible overlap would
  // be if the preceding mmap contains the start point of the given mmap.
  if (I != MMaps.begin()) {
    --I;
    if (I->second.contains(Map.Addr))
      return &I->second;
  }
  return nullptr;
}

// Returns the MMap that contains the given address or nullptr if none.
const MarkupFilter::MMap *MarkupFilter::getContainingMMap(uint64_t Addr) const {
  // Find the first mmap starting >= Addr.
  auto I = MMaps.lower_bound(Addr);
  if (I != MMaps.end() && I->second.contains(Addr))
    return &I->second;

  // The previous mmap is the last one starting < Addr.
  if (I == MMaps.begin())
    return nullptr;
  --I;
  return I->second.contains(Addr) ? &I->second : nullptr;
}

uint64_t MarkupFilter::adjustAddr(uint64_t Addr, PCType Type) const {
  // Decrementing return addresses by one moves them into the call instruction.
  // The address doesn't have to be the start of the call instruction, just some
  // byte on the inside. Subtracting one avoids needing detailed instruction
  // length information here.
  return Type == MarkupFilter::PCType::ReturnAddress ? Addr - 1 : Addr;
}

StringRef MarkupFilter::lineEnding() const {
  return StringRef(Line).ends_with("\r\n") ? "\r\n" : "\n";
}

bool MarkupFilter::MMap::contains(uint64_t Addr) const {
  return this->Addr <= Addr && Addr < this->Addr + Size;
}

// Returns the module-relative address for a given virtual address.
uint64_t MarkupFilter::MMap::getModuleRelativeAddr(uint64_t Addr) const {
  return Addr - this->Addr + ModuleRelativeAddr;
}
