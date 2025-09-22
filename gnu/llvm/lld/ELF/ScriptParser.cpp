//===- ScriptParser.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a recursive-descendent parser for linker scripts.
// Parsed results are stored to Config and Script global objects.
//
//===----------------------------------------------------------------------===//

#include "ScriptParser.h"
#include "Config.h"
#include "Driver.h"
#include "InputFiles.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "ScriptLexer.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Target.h"
#include "lld/Common/CommonLinkerContext.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/TimeProfiler.h"
#include <cassert>
#include <limits>
#include <optional>
#include <vector>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::elf;

namespace {
class ScriptParser final : ScriptLexer {
public:
  ScriptParser(MemoryBufferRef mb) : ScriptLexer(mb) {
    // Initialize IsUnderSysroot
    if (config->sysroot == "")
      return;
    StringRef path = mb.getBufferIdentifier();
    for (; !path.empty(); path = sys::path::parent_path(path)) {
      if (!sys::fs::equivalent(config->sysroot, path))
        continue;
      isUnderSysroot = true;
      return;
    }
  }

  void readLinkerScript();
  void readVersionScript();
  void readDynamicList();
  void readDefsym(StringRef name);

private:
  void addFile(StringRef path);

  void readAsNeeded();
  void readEntry();
  void readExtern();
  void readGroup();
  void readInclude();
  void readInput();
  void readMemory();
  void readOutput();
  void readOutputArch();
  void readOutputFormat();
  void readOverwriteSections();
  void readPhdrs();
  void readRegionAlias();
  void readSearchDir();
  void readSections();
  void readTarget();
  void readVersion();
  void readVersionScriptCommand();
  void readNoCrossRefs(bool to);

  SymbolAssignment *readSymbolAssignment(StringRef name);
  ByteCommand *readByteCommand(StringRef tok);
  std::array<uint8_t, 4> readFill();
  bool readSectionDirective(OutputSection *cmd, StringRef tok);
  void readSectionAddressType(OutputSection *cmd);
  OutputDesc *readOverlaySectionDescription();
  OutputDesc *readOutputSectionDescription(StringRef outSec);
  SmallVector<SectionCommand *, 0> readOverlay();
  SmallVector<StringRef, 0> readOutputSectionPhdrs();
  std::pair<uint64_t, uint64_t> readInputSectionFlags();
  InputSectionDescription *readInputSectionDescription(StringRef tok);
  StringMatcher readFilePatterns();
  SmallVector<SectionPattern, 0> readInputSectionsList();
  InputSectionDescription *readInputSectionRules(StringRef filePattern,
                                                 uint64_t withFlags,
                                                 uint64_t withoutFlags);
  unsigned readPhdrType();
  SortSectionPolicy peekSortKind();
  SortSectionPolicy readSortKind();
  SymbolAssignment *readProvideHidden(bool provide, bool hidden);
  SymbolAssignment *readAssignment(StringRef tok);
  void readSort();
  Expr readAssert();
  Expr readConstant();
  Expr getPageSize();

  Expr readMemoryAssignment(StringRef, StringRef, StringRef);
  void readMemoryAttributes(uint32_t &flags, uint32_t &invFlags,
                            uint32_t &negFlags, uint32_t &negInvFlags);

  Expr combine(StringRef op, Expr l, Expr r);
  Expr readExpr();
  Expr readExpr1(Expr lhs, int minPrec);
  StringRef readParenLiteral();
  Expr readPrimary();
  Expr readTernary(Expr cond);
  Expr readParenExpr();

  // For parsing version script.
  SmallVector<SymbolVersion, 0> readVersionExtern();
  void readAnonymousDeclaration();
  void readVersionDeclaration(StringRef verStr);

  std::pair<SmallVector<SymbolVersion, 0>, SmallVector<SymbolVersion, 0>>
  readSymbols();

  // True if a script being read is in the --sysroot directory.
  bool isUnderSysroot = false;

  // A set to detect an INCLUDE() cycle.
  StringSet<> seen;

  // If we are currently parsing a PROVIDE|PROVIDE_HIDDEN command,
  // then this member is set to the PROVIDE symbol name.
  std::optional<llvm::StringRef> activeProvideSym;
};
} // namespace

static StringRef unquote(StringRef s) {
  if (s.starts_with("\""))
    return s.substr(1, s.size() - 2);
  return s;
}

// Some operations only support one non absolute value. Move the
// absolute one to the right hand side for convenience.
static void moveAbsRight(ExprValue &a, ExprValue &b) {
  if (a.sec == nullptr || (a.forceAbsolute && !b.isAbsolute()))
    std::swap(a, b);
  if (!b.isAbsolute())
    script->recordError(
        a.loc + ": at least one side of the expression must be absolute");
}

static ExprValue add(ExprValue a, ExprValue b) {
  moveAbsRight(a, b);
  return {a.sec, a.forceAbsolute, a.getSectionOffset() + b.getValue(), a.loc};
}

static ExprValue sub(ExprValue a, ExprValue b) {
  // The distance between two symbols in sections is absolute.
  if (!a.isAbsolute() && !b.isAbsolute())
    return a.getValue() - b.getValue();
  return {a.sec, false, a.getSectionOffset() - b.getValue(), a.loc};
}

static ExprValue bitAnd(ExprValue a, ExprValue b) {
  moveAbsRight(a, b);
  return {a.sec, a.forceAbsolute,
          (a.getValue() & b.getValue()) - a.getSecAddr(), a.loc};
}

static ExprValue bitXor(ExprValue a, ExprValue b) {
  moveAbsRight(a, b);
  return {a.sec, a.forceAbsolute,
          (a.getValue() ^ b.getValue()) - a.getSecAddr(), a.loc};
}

static ExprValue bitOr(ExprValue a, ExprValue b) {
  moveAbsRight(a, b);
  return {a.sec, a.forceAbsolute,
          (a.getValue() | b.getValue()) - a.getSecAddr(), a.loc};
}

void ScriptParser::readDynamicList() {
  expect("{");
  SmallVector<SymbolVersion, 0> locals;
  SmallVector<SymbolVersion, 0> globals;
  std::tie(locals, globals) = readSymbols();
  expect(";");

  if (!atEOF()) {
    setError("EOF expected, but got " + next());
    return;
  }
  if (!locals.empty()) {
    setError("\"local:\" scope not supported in --dynamic-list");
    return;
  }

  for (SymbolVersion v : globals)
    config->dynamicList.push_back(v);
}

void ScriptParser::readVersionScript() {
  readVersionScriptCommand();
  if (!atEOF())
    setError("EOF expected, but got " + next());
}

void ScriptParser::readVersionScriptCommand() {
  if (consume("{")) {
    readAnonymousDeclaration();
    return;
  }

  while (!atEOF() && !errorCount() && peek() != "}") {
    StringRef verStr = next();
    if (verStr == "{") {
      setError("anonymous version definition is used in "
               "combination with other version definitions");
      return;
    }
    expect("{");
    readVersionDeclaration(verStr);
  }
}

void ScriptParser::readVersion() {
  expect("{");
  readVersionScriptCommand();
  expect("}");
}

void ScriptParser::readLinkerScript() {
  while (!atEOF()) {
    StringRef tok = next();
    if (tok == ";")
      continue;

    if (tok == "ENTRY") {
      readEntry();
    } else if (tok == "EXTERN") {
      readExtern();
    } else if (tok == "GROUP") {
      readGroup();
    } else if (tok == "INCLUDE") {
      readInclude();
    } else if (tok == "INPUT") {
      readInput();
    } else if (tok == "MEMORY") {
      readMemory();
    } else if (tok == "OUTPUT") {
      readOutput();
    } else if (tok == "OUTPUT_ARCH") {
      readOutputArch();
    } else if (tok == "OUTPUT_FORMAT") {
      readOutputFormat();
    } else if (tok == "OVERWRITE_SECTIONS") {
      readOverwriteSections();
    } else if (tok == "PHDRS") {
      readPhdrs();
    } else if (tok == "REGION_ALIAS") {
      readRegionAlias();
    } else if (tok == "SEARCH_DIR") {
      readSearchDir();
    } else if (tok == "SECTIONS") {
      readSections();
    } else if (tok == "TARGET") {
      readTarget();
    } else if (tok == "VERSION") {
      readVersion();
    } else if (tok == "NOCROSSREFS") {
      readNoCrossRefs(/*to=*/false);
    } else if (tok == "NOCROSSREFS_TO") {
      readNoCrossRefs(/*to=*/true);
    } else if (SymbolAssignment *cmd = readAssignment(tok)) {
      script->sectionCommands.push_back(cmd);
    } else {
      setError("unknown directive: " + tok);
    }
  }
}

void ScriptParser::readDefsym(StringRef name) {
  if (errorCount())
    return;
  Expr e = readExpr();
  if (!atEOF())
    setError("EOF expected, but got " + next());
  auto *cmd = make<SymbolAssignment>(
      name, e, 0, getCurrentMB().getBufferIdentifier().str());
  script->sectionCommands.push_back(cmd);
}

void ScriptParser::readNoCrossRefs(bool to) {
  expect("(");
  NoCrossRefCommand cmd{{}, to};
  while (!errorCount() && !consume(")"))
    cmd.outputSections.push_back(unquote(next()));
  if (cmd.outputSections.size() < 2)
    warn(getCurrentLocation() + ": ignored with fewer than 2 output sections");
  else
    script->noCrossRefs.push_back(std::move(cmd));
}

void ScriptParser::addFile(StringRef s) {
  if (isUnderSysroot && s.starts_with("/")) {
    SmallString<128> pathData;
    StringRef path = (config->sysroot + s).toStringRef(pathData);
    if (sys::fs::exists(path))
      ctx.driver.addFile(saver().save(path), /*withLOption=*/false);
    else
      setError("cannot find " + s + " inside " + config->sysroot);
    return;
  }

  if (s.starts_with("/")) {
    // Case 1: s is an absolute path. Just open it.
    ctx.driver.addFile(s, /*withLOption=*/false);
  } else if (s.starts_with("=")) {
    // Case 2: relative to the sysroot.
    if (config->sysroot.empty())
      ctx.driver.addFile(s.substr(1), /*withLOption=*/false);
    else
      ctx.driver.addFile(saver().save(config->sysroot + "/" + s.substr(1)),
                         /*withLOption=*/false);
  } else if (s.starts_with("-l")) {
    // Case 3: search in the list of library paths.
    ctx.driver.addLibrary(s.substr(2));
  } else {
    // Case 4: s is a relative path. Search in the directory of the script file.
    std::string filename = std::string(getCurrentMB().getBufferIdentifier());
    StringRef directory = sys::path::parent_path(filename);
    if (!directory.empty()) {
      SmallString<0> path(directory);
      sys::path::append(path, s);
      if (sys::fs::exists(path)) {
        ctx.driver.addFile(path, /*withLOption=*/false);
        return;
      }
    }
    // Then search in the current working directory.
    if (sys::fs::exists(s)) {
      ctx.driver.addFile(s, /*withLOption=*/false);
    } else {
      // Finally, search in the list of library paths.
      if (std::optional<std::string> path = findFromSearchPaths(s))
        ctx.driver.addFile(saver().save(*path), /*withLOption=*/true);
      else
        setError("unable to find " + s);
    }
  }
}

void ScriptParser::readAsNeeded() {
  expect("(");
  bool orig = config->asNeeded;
  config->asNeeded = true;
  while (!errorCount() && !consume(")"))
    addFile(unquote(next()));
  config->asNeeded = orig;
}

void ScriptParser::readEntry() {
  // -e <symbol> takes predecence over ENTRY(<symbol>).
  expect("(");
  StringRef tok = next();
  if (config->entry.empty())
    config->entry = unquote(tok);
  expect(")");
}

void ScriptParser::readExtern() {
  expect("(");
  while (!errorCount() && !consume(")"))
    config->undefined.push_back(unquote(next()));
}

void ScriptParser::readGroup() {
  bool orig = InputFile::isInGroup;
  InputFile::isInGroup = true;
  readInput();
  InputFile::isInGroup = orig;
  if (!orig)
    ++InputFile::nextGroupId;
}

void ScriptParser::readInclude() {
  StringRef tok = unquote(next());

  if (!seen.insert(tok).second) {
    setError("there is a cycle in linker script INCLUDEs");
    return;
  }

  if (std::optional<std::string> path = searchScript(tok)) {
    if (std::optional<MemoryBufferRef> mb = readFile(*path))
      tokenize(*mb);
    return;
  }
  setError("cannot find linker script " + tok);
}

void ScriptParser::readInput() {
  expect("(");
  while (!errorCount() && !consume(")")) {
    if (consume("AS_NEEDED"))
      readAsNeeded();
    else
      addFile(unquote(next()));
  }
}

void ScriptParser::readOutput() {
  // -o <file> takes predecence over OUTPUT(<file>).
  expect("(");
  StringRef tok = next();
  if (config->outputFile.empty())
    config->outputFile = unquote(tok);
  expect(")");
}

void ScriptParser::readOutputArch() {
  // OUTPUT_ARCH is ignored for now.
  expect("(");
  while (!errorCount() && !consume(")"))
    skip();
}

static std::pair<ELFKind, uint16_t> parseBfdName(StringRef s) {
  return StringSwitch<std::pair<ELFKind, uint16_t>>(s)
      .Case("elf32-i386", {ELF32LEKind, EM_386})
      .Case("elf32-avr", {ELF32LEKind, EM_AVR})
      .Case("elf32-iamcu", {ELF32LEKind, EM_IAMCU})
      .Case("elf32-littlearm", {ELF32LEKind, EM_ARM})
      .Case("elf32-bigarm", {ELF32BEKind, EM_ARM})
      .Case("elf32-x86-64", {ELF32LEKind, EM_X86_64})
      .Case("elf64-aarch64", {ELF64LEKind, EM_AARCH64})
      .Case("elf64-littleaarch64", {ELF64LEKind, EM_AARCH64})
      .Case("elf64-bigaarch64", {ELF64BEKind, EM_AARCH64})
      .Case("elf32-powerpc", {ELF32BEKind, EM_PPC})
      .Case("elf32-powerpcle", {ELF32LEKind, EM_PPC})
      .Case("elf64-powerpc", {ELF64BEKind, EM_PPC64})
      .Case("elf64-powerpcle", {ELF64LEKind, EM_PPC64})
      .Case("elf64-x86-64", {ELF64LEKind, EM_X86_64})
      .Cases("elf32-tradbigmips", "elf32-bigmips", {ELF32BEKind, EM_MIPS})
      .Case("elf32-ntradbigmips", {ELF32BEKind, EM_MIPS})
      .Case("elf32-tradlittlemips", {ELF32LEKind, EM_MIPS})
      .Case("elf32-ntradlittlemips", {ELF32LEKind, EM_MIPS})
      .Case("elf64-tradbigmips", {ELF64BEKind, EM_MIPS})
      .Case("elf64-tradlittlemips", {ELF64LEKind, EM_MIPS})
      .Case("elf32-littleriscv", {ELF32LEKind, EM_RISCV})
      .Case("elf64-littleriscv", {ELF64LEKind, EM_RISCV})
      .Case("elf64-sparc", {ELF64BEKind, EM_SPARCV9})
      .Case("elf32-msp430", {ELF32LEKind, EM_MSP430})
      .Case("elf32-loongarch", {ELF32LEKind, EM_LOONGARCH})
      .Case("elf64-loongarch", {ELF64LEKind, EM_LOONGARCH})
      .Case("elf64-s390", {ELF64BEKind, EM_S390})
      .Cases("elf32-hexagon", "elf32-littlehexagon", {ELF32LEKind, EM_HEXAGON})
      .Default({ELFNoneKind, EM_NONE});
}

// Parse OUTPUT_FORMAT(bfdname) or OUTPUT_FORMAT(default, big, little). Choose
// big if -EB is specified, little if -EL is specified, or default if neither is
// specified.
void ScriptParser::readOutputFormat() {
  expect("(");

  StringRef s = unquote(next());
  if (!consume(")")) {
    expect(",");
    StringRef tmp = unquote(next());
    if (config->optEB)
      s = tmp;
    expect(",");
    tmp = unquote(next());
    if (config->optEL)
      s = tmp;
    consume(")");
  }
  // If more than one OUTPUT_FORMAT is specified, only the first is checked.
  if (!config->bfdname.empty())
    return;
  config->bfdname = s;

  if (s == "binary") {
    config->oFormatBinary = true;
    return;
  }

  if (s.consume_back("-freebsd"))
    config->osabi = ELFOSABI_FREEBSD;

  std::tie(config->ekind, config->emachine) = parseBfdName(s);
  if (config->emachine == EM_NONE)
    setError("unknown output format name: " + config->bfdname);
  if (s == "elf32-ntradlittlemips" || s == "elf32-ntradbigmips")
    config->mipsN32Abi = true;
  if (config->emachine == EM_MSP430)
    config->osabi = ELFOSABI_STANDALONE;
}

void ScriptParser::readPhdrs() {
  expect("{");

  while (!errorCount() && !consume("}")) {
    PhdrsCommand cmd;
    cmd.name = next();
    cmd.type = readPhdrType();

    while (!errorCount() && !consume(";")) {
      if (consume("FILEHDR"))
        cmd.hasFilehdr = true;
      else if (consume("PHDRS"))
        cmd.hasPhdrs = true;
      else if (consume("AT"))
        cmd.lmaExpr = readParenExpr();
      else if (consume("FLAGS"))
        cmd.flags = readParenExpr()().getValue();
      else
        setError("unexpected header attribute: " + next());
    }

    script->phdrsCommands.push_back(cmd);
  }
}

void ScriptParser::readRegionAlias() {
  expect("(");
  StringRef alias = unquote(next());
  expect(",");
  StringRef name = next();
  expect(")");

  if (script->memoryRegions.count(alias))
    setError("redefinition of memory region '" + alias + "'");
  if (!script->memoryRegions.count(name))
    setError("memory region '" + name + "' is not defined");
  script->memoryRegions.insert({alias, script->memoryRegions[name]});
}

void ScriptParser::readSearchDir() {
  expect("(");
  StringRef tok = next();
  if (!config->nostdlib)
    config->searchPaths.push_back(unquote(tok));
  expect(")");
}

// This reads an overlay description. Overlays are used to describe output
// sections that use the same virtual memory range and normally would trigger
// linker's sections sanity check failures.
// https://sourceware.org/binutils/docs/ld/Overlay-Description.html#Overlay-Description
SmallVector<SectionCommand *, 0> ScriptParser::readOverlay() {
  Expr addrExpr;
  if (consume(":")) {
    addrExpr = [] { return script->getDot(); };
  } else {
    addrExpr = readExpr();
    expect(":");
  }
  // When AT is omitted, LMA should equal VMA. script->getDot() when evaluating
  // lmaExpr will ensure this, even if the start address is specified.
  Expr lmaExpr =
      consume("AT") ? readParenExpr() : [] { return script->getDot(); };
  expect("{");

  SmallVector<SectionCommand *, 0> v;
  OutputSection *prev = nullptr;
  while (!errorCount() && !consume("}")) {
    // VA is the same for all sections. The LMAs are consecutive in memory
    // starting from the base load address specified.
    OutputDesc *osd = readOverlaySectionDescription();
    osd->osec.addrExpr = addrExpr;
    if (prev) {
      osd->osec.lmaExpr = [=] { return prev->getLMA() + prev->size; };
    } else {
      osd->osec.lmaExpr = lmaExpr;
      // Use first section address for subsequent sections as initial addrExpr
      // can be DOT. Ensure the first section, even if empty, is not discarded.
      osd->osec.usedInExpression = true;
      addrExpr = [=]() -> ExprValue { return {&osd->osec, false, 0, ""}; };
    }
    v.push_back(osd);
    prev = &osd->osec;
  }

  // According to the specification, at the end of the overlay, the location
  // counter should be equal to the overlay base address plus size of the
  // largest section seen in the overlay.
  // Here we want to create the Dot assignment command to achieve that.
  Expr moveDot = [=] {
    uint64_t max = 0;
    for (SectionCommand *cmd : v)
      max = std::max(max, cast<OutputDesc>(cmd)->osec.size);
    return addrExpr().getValue() + max;
  };
  v.push_back(make<SymbolAssignment>(".", moveDot, 0, getCurrentLocation()));
  return v;
}

void ScriptParser::readOverwriteSections() {
  expect("{");
  while (!errorCount() && !consume("}"))
    script->overwriteSections.push_back(readOutputSectionDescription(next()));
}

void ScriptParser::readSections() {
  expect("{");
  SmallVector<SectionCommand *, 0> v;
  while (!errorCount() && !consume("}")) {
    StringRef tok = next();
    if (tok == "OVERLAY") {
      for (SectionCommand *cmd : readOverlay())
        v.push_back(cmd);
      continue;
    } else if (tok == "INCLUDE") {
      readInclude();
      continue;
    }

    if (SectionCommand *cmd = readAssignment(tok))
      v.push_back(cmd);
    else
      v.push_back(readOutputSectionDescription(tok));
  }

  // If DATA_SEGMENT_RELRO_END is absent, for sections after DATA_SEGMENT_ALIGN,
  // the relro fields should be cleared.
  if (!script->seenRelroEnd)
    for (SectionCommand *cmd : v)
      if (auto *osd = dyn_cast<OutputDesc>(cmd))
        osd->osec.relro = false;

  script->sectionCommands.insert(script->sectionCommands.end(), v.begin(),
                                 v.end());

  if (atEOF() || !consume("INSERT")) {
    script->hasSectionsCommand = true;
    return;
  }

  bool isAfter = false;
  if (consume("AFTER"))
    isAfter = true;
  else if (!consume("BEFORE"))
    setError("expected AFTER/BEFORE, but got '" + next() + "'");
  StringRef where = next();
  SmallVector<StringRef, 0> names;
  for (SectionCommand *cmd : v)
    if (auto *os = dyn_cast<OutputDesc>(cmd))
      names.push_back(os->osec.name);
  if (!names.empty())
    script->insertCommands.push_back({std::move(names), isAfter, where});
}

void ScriptParser::readTarget() {
  // TARGET(foo) is an alias for "--format foo". Unlike GNU linkers,
  // we accept only a limited set of BFD names (i.e. "elf" or "binary")
  // for --format. We recognize only /^elf/ and "binary" in the linker
  // script as well.
  expect("(");
  StringRef tok = unquote(next());
  expect(")");

  if (tok.starts_with("elf"))
    config->formatBinary = false;
  else if (tok == "binary")
    config->formatBinary = true;
  else
    setError("unknown target: " + tok);
}

static int precedence(StringRef op) {
  return StringSwitch<int>(op)
      .Cases("*", "/", "%", 11)
      .Cases("+", "-", 10)
      .Cases("<<", ">>", 9)
      .Cases("<", "<=", ">", ">=", 8)
      .Cases("==", "!=", 7)
      .Case("&", 6)
      .Case("^", 5)
      .Case("|", 4)
      .Case("&&", 3)
      .Case("||", 2)
      .Case("?", 1)
      .Default(-1);
}

StringMatcher ScriptParser::readFilePatterns() {
  StringMatcher Matcher;

  while (!errorCount() && !consume(")"))
    Matcher.addPattern(SingleStringMatcher(next()));
  return Matcher;
}

SortSectionPolicy ScriptParser::peekSortKind() {
  return StringSwitch<SortSectionPolicy>(peek())
      .Case("REVERSE", SortSectionPolicy::Reverse)
      .Cases("SORT", "SORT_BY_NAME", SortSectionPolicy::Name)
      .Case("SORT_BY_ALIGNMENT", SortSectionPolicy::Alignment)
      .Case("SORT_BY_INIT_PRIORITY", SortSectionPolicy::Priority)
      .Case("SORT_NONE", SortSectionPolicy::None)
      .Default(SortSectionPolicy::Default);
}

SortSectionPolicy ScriptParser::readSortKind() {
  SortSectionPolicy ret = peekSortKind();
  if (ret != SortSectionPolicy::Default)
    skip();
  return ret;
}

// Reads SECTIONS command contents in the following form:
//
// <contents> ::= <elem>*
// <elem>     ::= <exclude>? <glob-pattern>
// <exclude>  ::= "EXCLUDE_FILE" "(" <glob-pattern>+ ")"
//
// For example,
//
// *(.foo EXCLUDE_FILE (a.o) .bar EXCLUDE_FILE (b.o) .baz)
//
// is parsed as ".foo", ".bar" with "a.o", and ".baz" with "b.o".
// The semantics of that is section .foo in any file, section .bar in
// any file but a.o, and section .baz in any file but b.o.
SmallVector<SectionPattern, 0> ScriptParser::readInputSectionsList() {
  SmallVector<SectionPattern, 0> ret;
  while (!errorCount() && peek() != ")") {
    StringMatcher excludeFilePat;
    if (consume("EXCLUDE_FILE")) {
      expect("(");
      excludeFilePat = readFilePatterns();
    }

    StringMatcher SectionMatcher;
    // Break if the next token is ), EXCLUDE_FILE, or SORT*.
    while (!errorCount() && peekSortKind() == SortSectionPolicy::Default) {
      StringRef s = peek();
      if (s == ")" || s == "EXCLUDE_FILE")
        break;
      // Detect common mistakes when certain non-wildcard meta characters are
      // used without a closing ')'.
      if (!s.empty() && strchr("(){}", s[0])) {
        skip();
        setError("section pattern is expected");
        break;
      }
      SectionMatcher.addPattern(unquote(next()));
    }

    if (!SectionMatcher.empty())
      ret.push_back({std::move(excludeFilePat), std::move(SectionMatcher)});
    else if (excludeFilePat.empty())
      break;
    else
      setError("section pattern is expected");
  }
  return ret;
}

// Reads contents of "SECTIONS" directive. That directive contains a
// list of glob patterns for input sections. The grammar is as follows.
//
// <patterns> ::= <section-list>
//              | <sort> "(" <section-list> ")"
//              | <sort> "(" <sort> "(" <section-list> ")" ")"
//
// <sort>     ::= "SORT" | "SORT_BY_NAME" | "SORT_BY_ALIGNMENT"
//              | "SORT_BY_INIT_PRIORITY" | "SORT_NONE"
//
// <section-list> is parsed by readInputSectionsList().
InputSectionDescription *
ScriptParser::readInputSectionRules(StringRef filePattern, uint64_t withFlags,
                                    uint64_t withoutFlags) {
  auto *cmd =
      make<InputSectionDescription>(filePattern, withFlags, withoutFlags);
  expect("(");

  while (!errorCount() && !consume(")")) {
    SortSectionPolicy outer = readSortKind();
    SortSectionPolicy inner = SortSectionPolicy::Default;
    SmallVector<SectionPattern, 0> v;
    if (outer != SortSectionPolicy::Default) {
      expect("(");
      inner = readSortKind();
      if (inner != SortSectionPolicy::Default) {
        expect("(");
        v = readInputSectionsList();
        expect(")");
      } else {
        v = readInputSectionsList();
      }
      expect(")");
    } else {
      v = readInputSectionsList();
    }

    for (SectionPattern &pat : v) {
      pat.sortInner = inner;
      pat.sortOuter = outer;
    }

    std::move(v.begin(), v.end(), std::back_inserter(cmd->sectionPatterns));
  }
  return cmd;
}

InputSectionDescription *
ScriptParser::readInputSectionDescription(StringRef tok) {
  // Input section wildcard can be surrounded by KEEP.
  // https://sourceware.org/binutils/docs/ld/Input-Section-Keep.html#Input-Section-Keep
  uint64_t withFlags = 0;
  uint64_t withoutFlags = 0;
  if (tok == "KEEP") {
    expect("(");
    if (consume("INPUT_SECTION_FLAGS"))
      std::tie(withFlags, withoutFlags) = readInputSectionFlags();
    InputSectionDescription *cmd =
        readInputSectionRules(next(), withFlags, withoutFlags);
    expect(")");
    script->keptSections.push_back(cmd);
    return cmd;
  }
  if (tok == "INPUT_SECTION_FLAGS") {
    std::tie(withFlags, withoutFlags) = readInputSectionFlags();
    tok = next();
  }
  return readInputSectionRules(tok, withFlags, withoutFlags);
}

void ScriptParser::readSort() {
  expect("(");
  expect("CONSTRUCTORS");
  expect(")");
}

Expr ScriptParser::readAssert() {
  expect("(");
  Expr e = readExpr();
  expect(",");
  StringRef msg = unquote(next());
  expect(")");

  return [=] {
    if (!e().getValue())
      errorOrWarn(msg);
    return script->getDot();
  };
}

#define ECase(X)                                                               \
  { #X, X }
constexpr std::pair<const char *, unsigned> typeMap[] = {
    ECase(SHT_PROGBITS),   ECase(SHT_NOTE),       ECase(SHT_NOBITS),
    ECase(SHT_INIT_ARRAY), ECase(SHT_FINI_ARRAY), ECase(SHT_PREINIT_ARRAY),
};
#undef ECase

// Tries to read the special directive for an output section definition which
// can be one of following: "(NOLOAD)", "(COPY)", "(INFO)", "(OVERLAY)", and
// "(TYPE=<value>)".
bool ScriptParser::readSectionDirective(OutputSection *cmd, StringRef tok) {
  if (tok != "NOLOAD" && tok != "COPY" && tok != "INFO" && tok != "OVERLAY" &&
      tok != "TYPE")
    return false;

  if (consume("NOLOAD")) {
    cmd->type = SHT_NOBITS;
    cmd->typeIsSet = true;
  } else if (consume("TYPE")) {
    expect("=");
    StringRef value = peek();
    auto it = llvm::find_if(typeMap, [=](auto e) { return e.first == value; });
    if (it != std::end(typeMap)) {
      // The value is a recognized literal SHT_*.
      cmd->type = it->second;
      skip();
    } else if (value.starts_with("SHT_")) {
      setError("unknown section type " + value);
    } else {
      // Otherwise, read an expression.
      cmd->type = readExpr()().getValue();
    }
    cmd->typeIsSet = true;
  } else {
    skip(); // This is "COPY", "INFO" or "OVERLAY".
    cmd->nonAlloc = true;
  }
  expect(")");
  return true;
}

// Reads an expression and/or the special directive for an output
// section definition. Directive is one of following: "(NOLOAD)",
// "(COPY)", "(INFO)" or "(OVERLAY)".
//
// An output section name can be followed by an address expression
// and/or directive. This grammar is not LL(1) because "(" can be
// interpreted as either the beginning of some expression or beginning
// of directive.
//
// https://sourceware.org/binutils/docs/ld/Output-Section-Address.html
// https://sourceware.org/binutils/docs/ld/Output-Section-Type.html
void ScriptParser::readSectionAddressType(OutputSection *cmd) {
  if (consume("(")) {
    // Temporarily set inExpr to support TYPE=<value> without spaces.
    SaveAndRestore saved(inExpr, true);
    if (readSectionDirective(cmd, peek()))
      return;
    cmd->addrExpr = readExpr();
    expect(")");
  } else {
    cmd->addrExpr = readExpr();
  }

  if (consume("(")) {
    SaveAndRestore saved(inExpr, true);
    StringRef tok = peek();
    if (!readSectionDirective(cmd, tok))
      setError("unknown section directive: " + tok);
  }
}

static Expr checkAlignment(Expr e, std::string &loc) {
  return [=] {
    uint64_t alignment = std::max((uint64_t)1, e().getValue());
    if (!isPowerOf2_64(alignment)) {
      error(loc + ": alignment must be power of 2");
      return (uint64_t)1; // Return a dummy value.
    }
    return alignment;
  };
}

OutputDesc *ScriptParser::readOverlaySectionDescription() {
  OutputDesc *osd = script->createOutputSection(next(), getCurrentLocation());
  osd->osec.inOverlay = true;
  expect("{");
  while (!errorCount() && !consume("}")) {
    uint64_t withFlags = 0;
    uint64_t withoutFlags = 0;
    if (consume("INPUT_SECTION_FLAGS"))
      std::tie(withFlags, withoutFlags) = readInputSectionFlags();
    osd->osec.commands.push_back(
        readInputSectionRules(next(), withFlags, withoutFlags));
  }
  osd->osec.phdrs = readOutputSectionPhdrs();
  return osd;
}

OutputDesc *ScriptParser::readOutputSectionDescription(StringRef outSec) {
  OutputDesc *cmd =
      script->createOutputSection(unquote(outSec), getCurrentLocation());
  OutputSection *osec = &cmd->osec;
  // Maybe relro. Will reset to false if DATA_SEGMENT_RELRO_END is absent.
  osec->relro = script->seenDataAlign && !script->seenRelroEnd;

  size_t symbolsReferenced = script->referencedSymbols.size();

  if (peek() != ":")
    readSectionAddressType(osec);
  expect(":");

  std::string location = getCurrentLocation();
  if (consume("AT"))
    osec->lmaExpr = readParenExpr();
  if (consume("ALIGN"))
    osec->alignExpr = checkAlignment(readParenExpr(), location);
  if (consume("SUBALIGN"))
    osec->subalignExpr = checkAlignment(readParenExpr(), location);

  // Parse constraints.
  if (consume("ONLY_IF_RO"))
    osec->constraint = ConstraintKind::ReadOnly;
  if (consume("ONLY_IF_RW"))
    osec->constraint = ConstraintKind::ReadWrite;
  expect("{");

  while (!errorCount() && !consume("}")) {
    StringRef tok = next();
    if (tok == ";") {
      // Empty commands are allowed. Do nothing here.
    } else if (SymbolAssignment *assign = readAssignment(tok)) {
      osec->commands.push_back(assign);
    } else if (ByteCommand *data = readByteCommand(tok)) {
      osec->commands.push_back(data);
    } else if (tok == "CONSTRUCTORS") {
      // CONSTRUCTORS is a keyword to make the linker recognize C++ ctors/dtors
      // by name. This is for very old file formats such as ECOFF/XCOFF.
      // For ELF, we should ignore.
    } else if (tok == "FILL") {
      // We handle the FILL command as an alias for =fillexp section attribute,
      // which is different from what GNU linkers do.
      // https://sourceware.org/binutils/docs/ld/Output-Section-Data.html
      if (peek() != "(")
        setError("( expected, but got " + peek());
      osec->filler = readFill();
    } else if (tok == "SORT") {
      readSort();
    } else if (tok == "INCLUDE") {
      readInclude();
    } else if (tok == "(" || tok == ")") {
      setError("expected filename pattern");
    } else if (peek() == "(") {
      osec->commands.push_back(readInputSectionDescription(tok));
    } else {
      // We have a file name and no input sections description. It is not a
      // commonly used syntax, but still acceptable. In that case, all sections
      // from the file will be included.
      // FIXME: GNU ld permits INPUT_SECTION_FLAGS to be used here. We do not
      // handle this case here as it will already have been matched by the
      // case above.
      auto *isd = make<InputSectionDescription>(tok);
      isd->sectionPatterns.push_back({{}, StringMatcher("*")});
      osec->commands.push_back(isd);
    }
  }

  if (consume(">"))
    osec->memoryRegionName = std::string(next());

  if (consume("AT")) {
    expect(">");
    osec->lmaRegionName = std::string(next());
  }

  if (osec->lmaExpr && !osec->lmaRegionName.empty())
    error("section can't have both LMA and a load region");

  osec->phdrs = readOutputSectionPhdrs();

  if (peek() == "=" || peek().starts_with("=")) {
    inExpr = true;
    consume("=");
    osec->filler = readFill();
    inExpr = false;
  }

  // Consume optional comma following output section command.
  consume(",");

  if (script->referencedSymbols.size() > symbolsReferenced)
    osec->expressionsUseSymbols = true;
  return cmd;
}

// Reads a `=<fillexp>` expression and returns its value as a big-endian number.
// https://sourceware.org/binutils/docs/ld/Output-Section-Fill.html
// We do not support using symbols in such expressions.
//
// When reading a hexstring, ld.bfd handles it as a blob of arbitrary
// size, while ld.gold always handles it as a 32-bit big-endian number.
// We are compatible with ld.gold because it's easier to implement.
// Also, we require that expressions with operators must be wrapped into
// round brackets. We did it to resolve the ambiguity when parsing scripts like:
// SECTIONS { .foo : { ... } =120+3 /DISCARD/ : { ... } }
std::array<uint8_t, 4> ScriptParser::readFill() {
  uint64_t value = readPrimary()().val;
  if (value > UINT32_MAX)
    setError("filler expression result does not fit 32-bit: 0x" +
             Twine::utohexstr(value));

  std::array<uint8_t, 4> buf;
  write32be(buf.data(), (uint32_t)value);
  return buf;
}

SymbolAssignment *ScriptParser::readProvideHidden(bool provide, bool hidden) {
  expect("(");
  StringRef name = next(), eq = peek();
  if (eq != "=") {
    setError("= expected, but got " + next());
    while (!atEOF() && next() != ")")
      ;
    return nullptr;
  }
  llvm::SaveAndRestore saveActiveProvideSym(activeProvideSym);
  if (provide)
    activeProvideSym = name;
  SymbolAssignment *cmd = readSymbolAssignment(name);
  cmd->provide = provide;
  cmd->hidden = hidden;
  expect(")");
  return cmd;
}

SymbolAssignment *ScriptParser::readAssignment(StringRef tok) {
  // Assert expression returns Dot, so this is equal to ".=."
  if (tok == "ASSERT")
    return make<SymbolAssignment>(".", readAssert(), 0, getCurrentLocation());

  size_t oldPos = pos;
  SymbolAssignment *cmd = nullptr;
  bool savedSeenRelroEnd = script->seenRelroEnd;
  const StringRef op = peek();
  if (op.starts_with("=")) {
    // Support = followed by an expression without whitespace.
    SaveAndRestore saved(inExpr, true);
    cmd = readSymbolAssignment(tok);
  } else if ((op.size() == 2 && op[1] == '=' && strchr("*/+-&^|", op[0])) ||
             op == "<<=" || op == ">>=") {
    cmd = readSymbolAssignment(tok);
  } else if (tok == "PROVIDE") {
    SaveAndRestore saved(inExpr, true);
    cmd = readProvideHidden(true, false);
  } else if (tok == "HIDDEN") {
    SaveAndRestore saved(inExpr, true);
    cmd = readProvideHidden(false, true);
  } else if (tok == "PROVIDE_HIDDEN") {
    SaveAndRestore saved(inExpr, true);
    cmd = readProvideHidden(true, true);
  }

  if (cmd) {
    cmd->dataSegmentRelroEnd = !savedSeenRelroEnd && script->seenRelroEnd;
    cmd->commandString =
        tok.str() + " " +
        llvm::join(tokens.begin() + oldPos, tokens.begin() + pos, " ");
    expect(";");
  }
  return cmd;
}

SymbolAssignment *ScriptParser::readSymbolAssignment(StringRef name) {
  name = unquote(name);
  StringRef op = next();
  assert(op == "=" || op == "*=" || op == "/=" || op == "+=" || op == "-=" ||
         op == "&=" || op == "^=" || op == "|=" || op == "<<=" || op == ">>=");
  // Note: GNU ld does not support %=.
  Expr e = readExpr();
  if (op != "=") {
    std::string loc = getCurrentLocation();
    e = [=, c = op[0]]() -> ExprValue {
      ExprValue lhs = script->getSymbolValue(name, loc);
      switch (c) {
      case '*':
        return lhs.getValue() * e().getValue();
      case '/':
        if (uint64_t rv = e().getValue())
          return lhs.getValue() / rv;
        error(loc + ": division by zero");
        return 0;
      case '+':
        return add(lhs, e());
      case '-':
        return sub(lhs, e());
      case '<':
        return lhs.getValue() << e().getValue() % 64;
      case '>':
        return lhs.getValue() >> e().getValue() % 64;
      case '&':
        return lhs.getValue() & e().getValue();
      case '^':
        return lhs.getValue() ^ e().getValue();
      case '|':
        return lhs.getValue() | e().getValue();
      default:
        llvm_unreachable("");
      }
    };
  }
  return make<SymbolAssignment>(name, e, ctx.scriptSymOrderCounter++,
                                getCurrentLocation());
}

// This is an operator-precedence parser to parse a linker
// script expression.
Expr ScriptParser::readExpr() {
  // Our lexer is context-aware. Set the in-expression bit so that
  // they apply different tokenization rules.
  SaveAndRestore saved(inExpr, true);
  Expr e = readExpr1(readPrimary(), 0);
  return e;
}

Expr ScriptParser::combine(StringRef op, Expr l, Expr r) {
  if (op == "+")
    return [=] { return add(l(), r()); };
  if (op == "-")
    return [=] { return sub(l(), r()); };
  if (op == "*")
    return [=] { return l().getValue() * r().getValue(); };
  if (op == "/") {
    std::string loc = getCurrentLocation();
    return [=]() -> uint64_t {
      if (uint64_t rv = r().getValue())
        return l().getValue() / rv;
      error(loc + ": division by zero");
      return 0;
    };
  }
  if (op == "%") {
    std::string loc = getCurrentLocation();
    return [=]() -> uint64_t {
      if (uint64_t rv = r().getValue())
        return l().getValue() % rv;
      error(loc + ": modulo by zero");
      return 0;
    };
  }
  if (op == "<<")
    return [=] { return l().getValue() << r().getValue() % 64; };
  if (op == ">>")
    return [=] { return l().getValue() >> r().getValue() % 64; };
  if (op == "<")
    return [=] { return l().getValue() < r().getValue(); };
  if (op == ">")
    return [=] { return l().getValue() > r().getValue(); };
  if (op == ">=")
    return [=] { return l().getValue() >= r().getValue(); };
  if (op == "<=")
    return [=] { return l().getValue() <= r().getValue(); };
  if (op == "==")
    return [=] { return l().getValue() == r().getValue(); };
  if (op == "!=")
    return [=] { return l().getValue() != r().getValue(); };
  if (op == "||")
    return [=] { return l().getValue() || r().getValue(); };
  if (op == "&&")
    return [=] { return l().getValue() && r().getValue(); };
  if (op == "&")
    return [=] { return bitAnd(l(), r()); };
  if (op == "^")
    return [=] { return bitXor(l(), r()); };
  if (op == "|")
    return [=] { return bitOr(l(), r()); };
  llvm_unreachable("invalid operator");
}

// This is a part of the operator-precedence parser. This function
// assumes that the remaining token stream starts with an operator.
Expr ScriptParser::readExpr1(Expr lhs, int minPrec) {
  while (!atEOF() && !errorCount()) {
    // Read an operator and an expression.
    StringRef op1 = peek();
    if (precedence(op1) < minPrec)
      break;
    skip();
    if (op1 == "?")
      return readTernary(lhs);
    Expr rhs = readPrimary();

    // Evaluate the remaining part of the expression first if the
    // next operator has greater precedence than the previous one.
    // For example, if we have read "+" and "3", and if the next
    // operator is "*", then we'll evaluate 3 * ... part first.
    while (!atEOF()) {
      StringRef op2 = peek();
      if (precedence(op2) <= precedence(op1))
        break;
      rhs = readExpr1(rhs, precedence(op2));
    }

    lhs = combine(op1, lhs, rhs);
  }
  return lhs;
}

Expr ScriptParser::getPageSize() {
  std::string location = getCurrentLocation();
  return [=]() -> uint64_t {
    if (target)
      return config->commonPageSize;
    error(location + ": unable to calculate page size");
    return 4096; // Return a dummy value.
  };
}

Expr ScriptParser::readConstant() {
  StringRef s = readParenLiteral();
  if (s == "COMMONPAGESIZE")
    return getPageSize();
  if (s == "MAXPAGESIZE")
    return [] { return config->maxPageSize; };
  setError("unknown constant: " + s);
  return [] { return 0; };
}

// Parses Tok as an integer. It recognizes hexadecimal (prefixed with
// "0x" or suffixed with "H") and decimal numbers. Decimal numbers may
// have "K" (Ki) or "M" (Mi) suffixes.
static std::optional<uint64_t> parseInt(StringRef tok) {
  // Hexadecimal
  uint64_t val;
  if (tok.starts_with_insensitive("0x")) {
    if (!to_integer(tok.substr(2), val, 16))
      return std::nullopt;
    return val;
  }
  if (tok.ends_with_insensitive("H")) {
    if (!to_integer(tok.drop_back(), val, 16))
      return std::nullopt;
    return val;
  }

  // Decimal
  if (tok.ends_with_insensitive("K")) {
    if (!to_integer(tok.drop_back(), val, 10))
      return std::nullopt;
    return val * 1024;
  }
  if (tok.ends_with_insensitive("M")) {
    if (!to_integer(tok.drop_back(), val, 10))
      return std::nullopt;
    return val * 1024 * 1024;
  }
  if (!to_integer(tok, val, 10))
    return std::nullopt;
  return val;
}

ByteCommand *ScriptParser::readByteCommand(StringRef tok) {
  int size = StringSwitch<int>(tok)
                 .Case("BYTE", 1)
                 .Case("SHORT", 2)
                 .Case("LONG", 4)
                 .Case("QUAD", 8)
                 .Default(-1);
  if (size == -1)
    return nullptr;

  size_t oldPos = pos;
  Expr e = readParenExpr();
  std::string commandString =
      tok.str() + " " +
      llvm::join(tokens.begin() + oldPos, tokens.begin() + pos, " ");
  return make<ByteCommand>(e, size, commandString);
}

static std::optional<uint64_t> parseFlag(StringRef tok) {
  if (std::optional<uint64_t> asInt = parseInt(tok))
    return asInt;
#define CASE_ENT(enum) #enum, ELF::enum
  return StringSwitch<std::optional<uint64_t>>(tok)
      .Case(CASE_ENT(SHF_WRITE))
      .Case(CASE_ENT(SHF_ALLOC))
      .Case(CASE_ENT(SHF_EXECINSTR))
      .Case(CASE_ENT(SHF_MERGE))
      .Case(CASE_ENT(SHF_STRINGS))
      .Case(CASE_ENT(SHF_INFO_LINK))
      .Case(CASE_ENT(SHF_LINK_ORDER))
      .Case(CASE_ENT(SHF_OS_NONCONFORMING))
      .Case(CASE_ENT(SHF_GROUP))
      .Case(CASE_ENT(SHF_TLS))
      .Case(CASE_ENT(SHF_COMPRESSED))
      .Case(CASE_ENT(SHF_EXCLUDE))
      .Case(CASE_ENT(SHF_ARM_PURECODE))
      .Default(std::nullopt);
#undef CASE_ENT
}

// Reads the '(' <flags> ')' list of section flags in
// INPUT_SECTION_FLAGS '(' <flags> ')' in the
// following form:
// <flags> ::= <flag>
//           | <flags> & flag
// <flag>  ::= Recognized Flag Name, or Integer value of flag.
// If the first character of <flag> is a ! then this means without flag,
// otherwise with flag.
// Example: SHF_EXECINSTR & !SHF_WRITE means with flag SHF_EXECINSTR and
// without flag SHF_WRITE.
std::pair<uint64_t, uint64_t> ScriptParser::readInputSectionFlags() {
   uint64_t withFlags = 0;
   uint64_t withoutFlags = 0;
   expect("(");
   while (!errorCount()) {
    StringRef tok = unquote(next());
    bool without = tok.consume_front("!");
    if (std::optional<uint64_t> flag = parseFlag(tok)) {
      if (without)
        withoutFlags |= *flag;
      else
        withFlags |= *flag;
    } else {
      setError("unrecognised flag: " + tok);
    }
    if (consume(")"))
      break;
    if (!consume("&")) {
      next();
      setError("expected & or )");
    }
  }
  return std::make_pair(withFlags, withoutFlags);
}

StringRef ScriptParser::readParenLiteral() {
  expect("(");
  bool orig = inExpr;
  inExpr = false;
  StringRef tok = next();
  inExpr = orig;
  expect(")");
  return tok;
}

static void checkIfExists(const OutputSection &osec, StringRef location) {
  if (osec.location.empty() && script->errorOnMissingSection)
    script->recordError(location + ": undefined section " + osec.name);
}

static bool isValidSymbolName(StringRef s) {
  auto valid = [](char c) {
    return isAlnum(c) || c == '$' || c == '.' || c == '_';
  };
  return !s.empty() && !isDigit(s[0]) && llvm::all_of(s, valid);
}

Expr ScriptParser::readPrimary() {
  if (peek() == "(")
    return readParenExpr();

  if (consume("~")) {
    Expr e = readPrimary();
    return [=] { return ~e().getValue(); };
  }
  if (consume("!")) {
    Expr e = readPrimary();
    return [=] { return !e().getValue(); };
  }
  if (consume("-")) {
    Expr e = readPrimary();
    return [=] { return -e().getValue(); };
  }

  StringRef tok = next();
  std::string location = getCurrentLocation();

  // Built-in functions are parsed here.
  // https://sourceware.org/binutils/docs/ld/Builtin-Functions.html.
  if (tok == "ABSOLUTE") {
    Expr inner = readParenExpr();
    return [=] {
      ExprValue i = inner();
      i.forceAbsolute = true;
      return i;
    };
  }
  if (tok == "ADDR") {
    StringRef name = unquote(readParenLiteral());
    OutputSection *osec = &script->getOrCreateOutputSection(name)->osec;
    osec->usedInExpression = true;
    return [=]() -> ExprValue {
      checkIfExists(*osec, location);
      return {osec, false, 0, location};
    };
  }
  if (tok == "ALIGN") {
    expect("(");
    Expr e = readExpr();
    if (consume(")")) {
      e = checkAlignment(e, location);
      return [=] { return alignToPowerOf2(script->getDot(), e().getValue()); };
    }
    expect(",");
    Expr e2 = checkAlignment(readExpr(), location);
    expect(")");
    return [=] {
      ExprValue v = e();
      v.alignment = e2().getValue();
      return v;
    };
  }
  if (tok == "ALIGNOF") {
    StringRef name = unquote(readParenLiteral());
    OutputSection *osec = &script->getOrCreateOutputSection(name)->osec;
    return [=] {
      checkIfExists(*osec, location);
      return osec->addralign;
    };
  }
  if (tok == "ASSERT")
    return readAssert();
  if (tok == "CONSTANT")
    return readConstant();
  if (tok == "DATA_SEGMENT_ALIGN") {
    expect("(");
    Expr e = readExpr();
    expect(",");
    readExpr();
    expect(")");
    script->seenDataAlign = true;
    return [=] {
      uint64_t align = std::max(uint64_t(1), e().getValue());
      return (script->getDot() + align - 1) & -align;
    };
  }
  if (tok == "DATA_SEGMENT_END") {
    expect("(");
    expect(".");
    expect(")");
    return [] { return script->getDot(); };
  }
  if (tok == "DATA_SEGMENT_RELRO_END") {
    // GNU linkers implements more complicated logic to handle
    // DATA_SEGMENT_RELRO_END. We instead ignore the arguments and
    // just align to the next page boundary for simplicity.
    expect("(");
    readExpr();
    expect(",");
    readExpr();
    expect(")");
    script->seenRelroEnd = true;
    return [=] { return alignToPowerOf2(script->getDot(), config->maxPageSize); };
  }
  if (tok == "DEFINED") {
    StringRef name = unquote(readParenLiteral());
    // Return 1 if s is defined. If the definition is only found in a linker
    // script, it must happen before this DEFINED.
    auto order = ctx.scriptSymOrderCounter++;
    return [=] {
      Symbol *s = symtab.find(name);
      return s && s->isDefined() && ctx.scriptSymOrder.lookup(s) < order ? 1
                                                                         : 0;
    };
  }
  if (tok == "LENGTH") {
    StringRef name = readParenLiteral();
    if (script->memoryRegions.count(name) == 0) {
      setError("memory region not defined: " + name);
      return [] { return 0; };
    }
    return script->memoryRegions[name]->length;
  }
  if (tok == "LOADADDR") {
    StringRef name = unquote(readParenLiteral());
    OutputSection *osec = &script->getOrCreateOutputSection(name)->osec;
    osec->usedInExpression = true;
    return [=] {
      checkIfExists(*osec, location);
      return osec->getLMA();
    };
  }
  if (tok == "LOG2CEIL") {
    expect("(");
    Expr a = readExpr();
    expect(")");
    return [=] {
      // LOG2CEIL(0) is defined to be 0.
      return llvm::Log2_64_Ceil(std::max(a().getValue(), UINT64_C(1)));
    };
  }
  if (tok == "MAX" || tok == "MIN") {
    expect("(");
    Expr a = readExpr();
    expect(",");
    Expr b = readExpr();
    expect(")");
    if (tok == "MIN")
      return [=] { return std::min(a().getValue(), b().getValue()); };
    return [=] { return std::max(a().getValue(), b().getValue()); };
  }
  if (tok == "ORIGIN") {
    StringRef name = readParenLiteral();
    if (script->memoryRegions.count(name) == 0) {
      setError("memory region not defined: " + name);
      return [] { return 0; };
    }
    return script->memoryRegions[name]->origin;
  }
  if (tok == "SEGMENT_START") {
    expect("(");
    skip();
    expect(",");
    Expr e = readExpr();
    expect(")");
    return [=] { return e(); };
  }
  if (tok == "SIZEOF") {
    StringRef name = unquote(readParenLiteral());
    OutputSection *cmd = &script->getOrCreateOutputSection(name)->osec;
    // Linker script does not create an output section if its content is empty.
    // We want to allow SIZEOF(.foo) where .foo is a section which happened to
    // be empty.
    return [=] { return cmd->size; };
  }
  if (tok == "SIZEOF_HEADERS")
    return [=] { return elf::getHeaderSize(); };

  // Tok is the dot.
  if (tok == ".")
    return [=] { return script->getSymbolValue(tok, location); };

  // Tok is a literal number.
  if (std::optional<uint64_t> val = parseInt(tok))
    return [=] { return *val; };

  // Tok is a symbol name.
  if (tok.starts_with("\""))
    tok = unquote(tok);
  else if (!isValidSymbolName(tok))
    setError("malformed number: " + tok);
  if (activeProvideSym)
    script->provideMap[*activeProvideSym].push_back(tok);
  else
    script->referencedSymbols.push_back(tok);
  return [=] { return script->getSymbolValue(tok, location); };
}

Expr ScriptParser::readTernary(Expr cond) {
  Expr l = readExpr();
  expect(":");
  Expr r = readExpr();
  return [=] { return cond().getValue() ? l() : r(); };
}

Expr ScriptParser::readParenExpr() {
  expect("(");
  Expr e = readExpr();
  expect(")");
  return e;
}

SmallVector<StringRef, 0> ScriptParser::readOutputSectionPhdrs() {
  SmallVector<StringRef, 0> phdrs;
  while (!errorCount() && peek().starts_with(":")) {
    StringRef tok = next();
    phdrs.push_back((tok.size() == 1) ? next() : tok.substr(1));
  }
  return phdrs;
}

// Read a program header type name. The next token must be a
// name of a program header type or a constant (e.g. "0x3").
unsigned ScriptParser::readPhdrType() {
  StringRef tok = next();
  if (std::optional<uint64_t> val = parseInt(tok))
    return *val;

  unsigned ret = StringSwitch<unsigned>(tok)
                     .Case("PT_NULL", PT_NULL)
                     .Case("PT_LOAD", PT_LOAD)
                     .Case("PT_DYNAMIC", PT_DYNAMIC)
                     .Case("PT_INTERP", PT_INTERP)
                     .Case("PT_NOTE", PT_NOTE)
                     .Case("PT_SHLIB", PT_SHLIB)
                     .Case("PT_PHDR", PT_PHDR)
                     .Case("PT_TLS", PT_TLS)
                     .Case("PT_GNU_EH_FRAME", PT_GNU_EH_FRAME)
                     .Case("PT_GNU_STACK", PT_GNU_STACK)
                     .Case("PT_GNU_RELRO", PT_GNU_RELRO)
                     .Case("PT_OPENBSD_MUTABLE", PT_OPENBSD_MUTABLE)
                     .Case("PT_OPENBSD_RANDOMIZE", PT_OPENBSD_RANDOMIZE)
                     .Case("PT_OPENBSD_SYSCALLS", PT_OPENBSD_SYSCALLS)
                     .Case("PT_OPENBSD_WXNEEDED", PT_OPENBSD_WXNEEDED)
                     .Case("PT_OPENBSD_BOOTDATA", PT_OPENBSD_BOOTDATA)
                     .Default(-1);

  if (ret == (unsigned)-1) {
    setError("invalid program header type: " + tok);
    return PT_NULL;
  }
  return ret;
}

// Reads an anonymous version declaration.
void ScriptParser::readAnonymousDeclaration() {
  SmallVector<SymbolVersion, 0> locals;
  SmallVector<SymbolVersion, 0> globals;
  std::tie(locals, globals) = readSymbols();
  for (const SymbolVersion &pat : locals)
    config->versionDefinitions[VER_NDX_LOCAL].localPatterns.push_back(pat);
  for (const SymbolVersion &pat : globals)
    config->versionDefinitions[VER_NDX_GLOBAL].nonLocalPatterns.push_back(pat);

  expect(";");
}

// Reads a non-anonymous version definition,
// e.g. "VerStr { global: foo; bar; local: *; };".
void ScriptParser::readVersionDeclaration(StringRef verStr) {
  // Read a symbol list.
  SmallVector<SymbolVersion, 0> locals;
  SmallVector<SymbolVersion, 0> globals;
  std::tie(locals, globals) = readSymbols();

  // Create a new version definition and add that to the global symbols.
  VersionDefinition ver;
  ver.name = verStr;
  ver.nonLocalPatterns = std::move(globals);
  ver.localPatterns = std::move(locals);
  ver.id = config->versionDefinitions.size();
  config->versionDefinitions.push_back(ver);

  // Each version may have a parent version. For example, "Ver2"
  // defined as "Ver2 { global: foo; local: *; } Ver1;" has "Ver1"
  // as a parent. This version hierarchy is, probably against your
  // instinct, purely for hint; the runtime doesn't care about it
  // at all. In LLD, we simply ignore it.
  if (next() != ";")
    expect(";");
}

bool elf::hasWildcard(StringRef s) {
  return s.find_first_of("?*[") != StringRef::npos;
}

// Reads a list of symbols, e.g. "{ global: foo; bar; local: *; };".
std::pair<SmallVector<SymbolVersion, 0>, SmallVector<SymbolVersion, 0>>
ScriptParser::readSymbols() {
  SmallVector<SymbolVersion, 0> locals;
  SmallVector<SymbolVersion, 0> globals;
  SmallVector<SymbolVersion, 0> *v = &globals;

  while (!errorCount()) {
    if (consume("}"))
      break;
    if (consumeLabel("local")) {
      v = &locals;
      continue;
    }
    if (consumeLabel("global")) {
      v = &globals;
      continue;
    }

    if (consume("extern")) {
      SmallVector<SymbolVersion, 0> ext = readVersionExtern();
      v->insert(v->end(), ext.begin(), ext.end());
    } else {
      StringRef tok = next();
      v->push_back({unquote(tok), false, hasWildcard(tok)});
    }
    expect(";");
  }
  return {locals, globals};
}

// Reads an "extern C++" directive, e.g.,
// "extern "C++" { ns::*; "f(int, double)"; };"
//
// The last semicolon is optional. E.g. this is OK:
// "extern "C++" { ns::*; "f(int, double)" };"
SmallVector<SymbolVersion, 0> ScriptParser::readVersionExtern() {
  StringRef tok = next();
  bool isCXX = tok == "\"C++\"";
  if (!isCXX && tok != "\"C\"")
    setError("Unknown language");
  expect("{");

  SmallVector<SymbolVersion, 0> ret;
  while (!errorCount() && peek() != "}") {
    StringRef tok = next();
    ret.push_back(
        {unquote(tok), isCXX, !tok.starts_with("\"") && hasWildcard(tok)});
    if (consume("}"))
      return ret;
    expect(";");
  }

  expect("}");
  return ret;
}

Expr ScriptParser::readMemoryAssignment(StringRef s1, StringRef s2,
                                        StringRef s3) {
  if (!consume(s1) && !consume(s2) && !consume(s3)) {
    setError("expected one of: " + s1 + ", " + s2 + ", or " + s3);
    return [] { return 0; };
  }
  expect("=");
  return readExpr();
}

// Parse the MEMORY command as specified in:
// https://sourceware.org/binutils/docs/ld/MEMORY.html
//
// MEMORY { name [(attr)] : ORIGIN = origin, LENGTH = len ... }
void ScriptParser::readMemory() {
  expect("{");
  while (!errorCount() && !consume("}")) {
    StringRef tok = next();
    if (tok == "INCLUDE") {
      readInclude();
      continue;
    }

    uint32_t flags = 0;
    uint32_t invFlags = 0;
    uint32_t negFlags = 0;
    uint32_t negInvFlags = 0;
    if (consume("(")) {
      readMemoryAttributes(flags, invFlags, negFlags, negInvFlags);
      expect(")");
    }
    expect(":");

    Expr origin = readMemoryAssignment("ORIGIN", "org", "o");
    expect(",");
    Expr length = readMemoryAssignment("LENGTH", "len", "l");

    // Add the memory region to the region map.
    MemoryRegion *mr = make<MemoryRegion>(tok, origin, length, flags, invFlags,
                                          negFlags, negInvFlags);
    if (!script->memoryRegions.insert({tok, mr}).second)
      setError("region '" + tok + "' already defined");
  }
}

// This function parses the attributes used to match against section
// flags when placing output sections in a memory region. These flags
// are only used when an explicit memory region name is not used.
void ScriptParser::readMemoryAttributes(uint32_t &flags, uint32_t &invFlags,
                                        uint32_t &negFlags,
                                        uint32_t &negInvFlags) {
  bool invert = false;

  for (char c : next().lower()) {
    if (c == '!') {
      invert = !invert;
      std::swap(flags, negFlags);
      std::swap(invFlags, negInvFlags);
      continue;
    }
    if (c == 'w')
      flags |= SHF_WRITE;
    else if (c == 'x')
      flags |= SHF_EXECINSTR;
    else if (c == 'a')
      flags |= SHF_ALLOC;
    else if (c == 'r')
      invFlags |= SHF_WRITE;
    else
      setError("invalid memory region attribute");
  }

  if (invert) {
    std::swap(flags, negFlags);
    std::swap(invFlags, negInvFlags);
  }
}

void elf::readLinkerScript(MemoryBufferRef mb) {
  llvm::TimeTraceScope timeScope("Read linker script",
                                 mb.getBufferIdentifier());
  ScriptParser(mb).readLinkerScript();
}

void elf::readVersionScript(MemoryBufferRef mb) {
  llvm::TimeTraceScope timeScope("Read version script",
                                 mb.getBufferIdentifier());
  ScriptParser(mb).readVersionScript();
}

void elf::readDynamicList(MemoryBufferRef mb) {
  llvm::TimeTraceScope timeScope("Read dynamic list", mb.getBufferIdentifier());
  ScriptParser(mb).readDynamicList();
}

void elf::readDefsym(StringRef name, MemoryBufferRef mb) {
  llvm::TimeTraceScope timeScope("Read defsym input", name);
  ScriptParser(mb).readDefsym(name);
}
