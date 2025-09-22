//===- DriverUtils.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains utility functions for the driver. Because there
// are so many small functions, we created this separate file to make
// Driver.cpp less cluttered.
//
//===----------------------------------------------------------------------===//

#include "COFFLinkerContext.h"
#include "Driver.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/WindowsManifest/WindowsManifestMerger.h"
#include <limits>
#include <memory>
#include <optional>

using namespace llvm::COFF;
using namespace llvm::object;
using namespace llvm::opt;
using namespace llvm;
using llvm::sys::Process;

namespace lld {
namespace coff {
namespace {

const uint16_t SUBLANG_ENGLISH_US = 0x0409;
const uint16_t RT_MANIFEST = 24;

class Executor {
public:
  explicit Executor(StringRef s) : prog(saver().save(s)) {}
  void add(StringRef s) { args.push_back(saver().save(s)); }
  void add(std::string &s) { args.push_back(saver().save(s)); }
  void add(Twine s) { args.push_back(saver().save(s)); }
  void add(const char *s) { args.push_back(saver().save(s)); }

  void run() {
    ErrorOr<std::string> exeOrErr = sys::findProgramByName(prog);
    if (auto ec = exeOrErr.getError())
      fatal("unable to find " + prog + " in PATH: " + ec.message());
    StringRef exe = saver().save(*exeOrErr);
    args.insert(args.begin(), exe);

    if (sys::ExecuteAndWait(args[0], args) != 0)
      fatal("ExecuteAndWait failed: " +
            llvm::join(args.begin(), args.end(), " "));
  }

private:
  StringRef prog;
  std::vector<StringRef> args;
};

} // anonymous namespace

// Parses a string in the form of "<integer>[,<integer>]".
void LinkerDriver::parseNumbers(StringRef arg, uint64_t *addr, uint64_t *size) {
  auto [s1, s2] = arg.split(',');
  if (s1.getAsInteger(0, *addr))
    fatal("invalid number: " + s1);
  if (size && !s2.empty() && s2.getAsInteger(0, *size))
    fatal("invalid number: " + s2);
}

// Parses a string in the form of "<integer>[.<integer>]".
// If second number is not present, Minor is set to 0.
void LinkerDriver::parseVersion(StringRef arg, uint32_t *major,
                                uint32_t *minor) {
  auto [s1, s2] = arg.split('.');
  if (s1.getAsInteger(10, *major))
    fatal("invalid number: " + s1);
  *minor = 0;
  if (!s2.empty() && s2.getAsInteger(10, *minor))
    fatal("invalid number: " + s2);
}

void LinkerDriver::parseGuard(StringRef fullArg) {
  SmallVector<StringRef, 1> splitArgs;
  fullArg.split(splitArgs, ",");
  for (StringRef arg : splitArgs) {
    if (arg.equals_insensitive("no"))
      ctx.config.guardCF = GuardCFLevel::Off;
    else if (arg.equals_insensitive("nolongjmp"))
      ctx.config.guardCF &= ~GuardCFLevel::LongJmp;
    else if (arg.equals_insensitive("noehcont"))
      ctx.config.guardCF &= ~GuardCFLevel::EHCont;
    else if (arg.equals_insensitive("cf") || arg.equals_insensitive("longjmp"))
      ctx.config.guardCF |= GuardCFLevel::CF | GuardCFLevel::LongJmp;
    else if (arg.equals_insensitive("ehcont"))
      ctx.config.guardCF |= GuardCFLevel::CF | GuardCFLevel::EHCont;
    else
      fatal("invalid argument to /guard: " + arg);
  }
}

// Parses a string in the form of "<subsystem>[,<integer>[.<integer>]]".
void LinkerDriver::parseSubsystem(StringRef arg, WindowsSubsystem *sys,
                                  uint32_t *major, uint32_t *minor,
                                  bool *gotVersion) {
  auto [sysStr, ver] = arg.split(',');
  std::string sysStrLower = sysStr.lower();
  *sys = StringSwitch<WindowsSubsystem>(sysStrLower)
    .Case("boot_application", IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION)
    .Case("console", IMAGE_SUBSYSTEM_WINDOWS_CUI)
    .Case("default", IMAGE_SUBSYSTEM_UNKNOWN)
    .Case("efi_application", IMAGE_SUBSYSTEM_EFI_APPLICATION)
    .Case("efi_boot_service_driver", IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER)
    .Case("efi_rom", IMAGE_SUBSYSTEM_EFI_ROM)
    .Case("efi_runtime_driver", IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER)
    .Case("native", IMAGE_SUBSYSTEM_NATIVE)
    .Case("posix", IMAGE_SUBSYSTEM_POSIX_CUI)
    .Case("windows", IMAGE_SUBSYSTEM_WINDOWS_GUI)
    .Default(IMAGE_SUBSYSTEM_UNKNOWN);
  if (*sys == IMAGE_SUBSYSTEM_UNKNOWN && sysStrLower != "default")
    fatal("unknown subsystem: " + sysStr);
  if (!ver.empty())
    parseVersion(ver, major, minor);
  if (gotVersion)
    *gotVersion = !ver.empty();
}

// Parse a string of the form of "<from>=<to>".
// Results are directly written to Config.
void LinkerDriver::parseAlternateName(StringRef s) {
  auto [from, to] = s.split('=');
  if (from.empty() || to.empty())
    fatal("/alternatename: invalid argument: " + s);
  auto it = ctx.config.alternateNames.find(from);
  if (it != ctx.config.alternateNames.end() && it->second != to)
    fatal("/alternatename: conflicts: " + s);
  ctx.config.alternateNames.insert(it, std::make_pair(from, to));
}

// Parse a string of the form of "<from>=<to>".
// Results are directly written to Config.
void LinkerDriver::parseMerge(StringRef s) {
  auto [from, to] = s.split('=');
  if (from.empty() || to.empty())
    fatal("/merge: invalid argument: " + s);
  if (from == ".rsrc" || to == ".rsrc")
    fatal("/merge: cannot merge '.rsrc' with any section");
  if (from == ".reloc" || to == ".reloc")
    fatal("/merge: cannot merge '.reloc' with any section");
  auto pair = ctx.config.merge.insert(std::make_pair(from, to));
  bool inserted = pair.second;
  if (!inserted) {
    StringRef existing = pair.first->second;
    if (existing != to)
      warn(s + ": already merged into " + existing);
  }
}

void LinkerDriver::parsePDBPageSize(StringRef s) {
  int v;
  if (s.getAsInteger(0, v)) {
    error("/pdbpagesize: invalid argument: " + s);
    return;
  }
  if (v != 4096 && v != 8192 && v != 16384 && v != 32768) {
    error("/pdbpagesize: invalid argument: " + s);
    return;
  }

  ctx.config.pdbPageSize = v;
}

static uint32_t parseSectionAttributes(StringRef s) {
  uint32_t ret = 0;
  for (char c : s.lower()) {
    switch (c) {
    case 'd':
      ret |= IMAGE_SCN_MEM_DISCARDABLE;
      break;
    case 'e':
      ret |= IMAGE_SCN_MEM_EXECUTE;
      break;
    case 'k':
      ret |= IMAGE_SCN_MEM_NOT_CACHED;
      break;
    case 'p':
      ret |= IMAGE_SCN_MEM_NOT_PAGED;
      break;
    case 'r':
      ret |= IMAGE_SCN_MEM_READ;
      break;
    case 's':
      ret |= IMAGE_SCN_MEM_SHARED;
      break;
    case 'w':
      ret |= IMAGE_SCN_MEM_WRITE;
      break;
    default:
      fatal("/section: invalid argument: " + s);
    }
  }
  return ret;
}

// Parses /section option argument.
void LinkerDriver::parseSection(StringRef s) {
  auto [name, attrs] = s.split(',');
  if (name.empty() || attrs.empty())
    fatal("/section: invalid argument: " + s);
  ctx.config.section[name] = parseSectionAttributes(attrs);
}

// Parses /aligncomm option argument.
void LinkerDriver::parseAligncomm(StringRef s) {
  auto [name, align] = s.split(',');
  if (name.empty() || align.empty()) {
    error("/aligncomm: invalid argument: " + s);
    return;
  }
  int v;
  if (align.getAsInteger(0, v)) {
    error("/aligncomm: invalid argument: " + s);
    return;
  }
  ctx.config.alignComm[std::string(name)] =
      std::max(ctx.config.alignComm[std::string(name)], 1 << v);
}

// Parses /functionpadmin option argument.
void LinkerDriver::parseFunctionPadMin(llvm::opt::Arg *a) {
  StringRef arg = a->getNumValues() ? a->getValue() : "";
  if (!arg.empty()) {
    // Optional padding in bytes is given.
    if (arg.getAsInteger(0, ctx.config.functionPadMin))
      error("/functionpadmin: invalid argument: " + arg);
    return;
  }
  // No optional argument given.
  // Set default padding based on machine, similar to link.exe.
  // There is no default padding for ARM platforms.
  if (ctx.config.machine == I386) {
    ctx.config.functionPadMin = 5;
  } else if (ctx.config.machine == AMD64) {
    ctx.config.functionPadMin = 6;
  } else {
    error("/functionpadmin: invalid argument for this machine: " + arg);
  }
}

// Parses /dependentloadflag option argument.
void LinkerDriver::parseDependentLoadFlags(llvm::opt::Arg *a) {
  StringRef arg = a->getNumValues() ? a->getValue() : "";
  if (!arg.empty()) {
    if (arg.getAsInteger(0, ctx.config.dependentLoadFlags))
      error("/dependentloadflag: invalid argument: " + arg);
    return;
  }
  // MSVC linker reports error "no argument specified", although MSDN describes
  // argument as optional.
  error("/dependentloadflag: no argument specified");
}

// Parses a string in the form of "EMBED[,=<integer>]|NO".
// Results are directly written to
// Config.
void LinkerDriver::parseManifest(StringRef arg) {
  if (arg.equals_insensitive("no")) {
    ctx.config.manifest = Configuration::No;
    return;
  }
  if (!arg.starts_with_insensitive("embed"))
    fatal("invalid option " + arg);
  ctx.config.manifest = Configuration::Embed;
  arg = arg.substr(strlen("embed"));
  if (arg.empty())
    return;
  if (!arg.starts_with_insensitive(",id="))
    fatal("invalid option " + arg);
  arg = arg.substr(strlen(",id="));
  if (arg.getAsInteger(0, ctx.config.manifestID))
    fatal("invalid option " + arg);
}

// Parses a string in the form of "level=<string>|uiAccess=<string>|NO".
// Results are directly written to Config.
void LinkerDriver::parseManifestUAC(StringRef arg) {
  if (arg.equals_insensitive("no")) {
    ctx.config.manifestUAC = false;
    return;
  }
  for (;;) {
    arg = arg.ltrim();
    if (arg.empty())
      return;
    if (arg.consume_front_insensitive("level=")) {
      std::tie(ctx.config.manifestLevel, arg) = arg.split(" ");
      continue;
    }
    if (arg.consume_front_insensitive("uiaccess=")) {
      std::tie(ctx.config.manifestUIAccess, arg) = arg.split(" ");
      continue;
    }
    fatal("invalid option " + arg);
  }
}

// Parses a string in the form of "cd|net[,(cd|net)]*"
// Results are directly written to Config.
void LinkerDriver::parseSwaprun(StringRef arg) {
  do {
    auto [swaprun, newArg] = arg.split(',');
    if (swaprun.equals_insensitive("cd"))
      ctx.config.swaprunCD = true;
    else if (swaprun.equals_insensitive("net"))
      ctx.config.swaprunNet = true;
    else if (swaprun.empty())
      error("/swaprun: missing argument");
    else
      error("/swaprun: invalid argument: " + swaprun);
    // To catch trailing commas, e.g. `/spawrun:cd,`
    if (newArg.empty() && arg.ends_with(","))
      error("/swaprun: missing argument");
    arg = newArg;
  } while (!arg.empty());
}

// An RAII temporary file class that automatically removes a temporary file.
namespace {
class TemporaryFile {
public:
  TemporaryFile(StringRef prefix, StringRef extn, StringRef contents = "") {
    SmallString<128> s;
    if (auto ec = sys::fs::createTemporaryFile("lld-" + prefix, extn, s))
      fatal("cannot create a temporary file: " + ec.message());
    path = std::string(s);

    if (!contents.empty()) {
      std::error_code ec;
      raw_fd_ostream os(path, ec, sys::fs::OF_None);
      if (ec)
        fatal("failed to open " + path + ": " + ec.message());
      os << contents;
    }
  }

  TemporaryFile(TemporaryFile &&obj) noexcept { std::swap(path, obj.path); }

  ~TemporaryFile() {
    if (path.empty())
      return;
    if (sys::fs::remove(path))
      fatal("failed to remove " + path);
  }

  // Returns a memory buffer of this temporary file.
  // Note that this function does not leave the file open,
  // so it is safe to remove the file immediately after this function
  // is called (you cannot remove an opened file on Windows.)
  std::unique_ptr<MemoryBuffer> getMemoryBuffer() {
    // IsVolatile=true forces MemoryBuffer to not use mmap().
    return CHECK(MemoryBuffer::getFile(path, /*IsText=*/false,
                                       /*RequiresNullTerminator=*/false,
                                       /*IsVolatile=*/true),
                 "could not open " + path);
  }

  std::string path;
};
}

std::string LinkerDriver::createDefaultXml() {
  std::string ret;
  raw_string_ostream os(ret);

  // Emit the XML. Note that we do *not* verify that the XML attributes are
  // syntactically correct. This is intentional for link.exe compatibility.
  os << "<?xml version=\"1.0\" standalone=\"yes\"?>\n"
     << "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\"\n"
     << "          manifestVersion=\"1.0\">\n";
  if (ctx.config.manifestUAC) {
    os << "  <trustInfo>\n"
       << "    <security>\n"
       << "      <requestedPrivileges>\n"
       << "         <requestedExecutionLevel level=" << ctx.config.manifestLevel
       << " uiAccess=" << ctx.config.manifestUIAccess << "/>\n"
       << "      </requestedPrivileges>\n"
       << "    </security>\n"
       << "  </trustInfo>\n";
  }
  for (auto manifestDependency : ctx.config.manifestDependencies) {
    os << "  <dependency>\n"
       << "    <dependentAssembly>\n"
       << "      <assemblyIdentity " << manifestDependency << " />\n"
       << "    </dependentAssembly>\n"
       << "  </dependency>\n";
  }
  os << "</assembly>\n";
  return os.str();
}

std::string
LinkerDriver::createManifestXmlWithInternalMt(StringRef defaultXml) {
  std::unique_ptr<MemoryBuffer> defaultXmlCopy =
      MemoryBuffer::getMemBufferCopy(defaultXml);

  windows_manifest::WindowsManifestMerger merger;
  if (auto e = merger.merge(*defaultXmlCopy.get()))
    fatal("internal manifest tool failed on default xml: " +
          toString(std::move(e)));

  for (StringRef filename : ctx.config.manifestInput) {
    std::unique_ptr<MemoryBuffer> manifest =
        check(MemoryBuffer::getFile(filename));
    // Call takeBuffer to include in /reproduce: output if applicable.
    if (auto e = merger.merge(takeBuffer(std::move(manifest))))
      fatal("internal manifest tool failed on file " + filename + ": " +
            toString(std::move(e)));
  }

  return std::string(merger.getMergedManifest().get()->getBuffer());
}

std::string
LinkerDriver::createManifestXmlWithExternalMt(StringRef defaultXml) {
  // Create the default manifest file as a temporary file.
  TemporaryFile Default("defaultxml", "manifest");
  std::error_code ec;
  raw_fd_ostream os(Default.path, ec, sys::fs::OF_TextWithCRLF);
  if (ec)
    fatal("failed to open " + Default.path + ": " + ec.message());
  os << defaultXml;
  os.close();

  // Merge user-supplied manifests if they are given.  Since libxml2 is not
  // enabled, we must shell out to Microsoft's mt.exe tool.
  TemporaryFile user("user", "manifest");

  Executor e("mt.exe");
  e.add("/manifest");
  e.add(Default.path);
  for (StringRef filename : ctx.config.manifestInput) {
    e.add("/manifest");
    e.add(filename);

    // Manually add the file to the /reproduce: tar if needed.
    if (tar)
      if (auto mbOrErr = MemoryBuffer::getFile(filename))
        takeBuffer(std::move(*mbOrErr));
  }
  e.add("/nologo");
  e.add("/out:" + StringRef(user.path));
  e.run();

  return std::string(
      CHECK(MemoryBuffer::getFile(user.path), "could not open " + user.path)
          .get()
          ->getBuffer());
}

std::string LinkerDriver::createManifestXml() {
  std::string defaultXml = createDefaultXml();
  if (ctx.config.manifestInput.empty())
    return defaultXml;

  if (windows_manifest::isAvailable())
    return createManifestXmlWithInternalMt(defaultXml);

  return createManifestXmlWithExternalMt(defaultXml);
}

std::unique_ptr<WritableMemoryBuffer>
LinkerDriver::createMemoryBufferForManifestRes(size_t manifestSize) {
  size_t resSize = alignTo(
      object::WIN_RES_MAGIC_SIZE + object::WIN_RES_NULL_ENTRY_SIZE +
          sizeof(object::WinResHeaderPrefix) + sizeof(object::WinResIDs) +
          sizeof(object::WinResHeaderSuffix) + manifestSize,
      object::WIN_RES_DATA_ALIGNMENT);
  return WritableMemoryBuffer::getNewMemBuffer(resSize, ctx.config.outputFile +
                                                            ".manifest.res");
}

static void writeResFileHeader(char *&buf) {
  memcpy(buf, COFF::WinResMagic, sizeof(COFF::WinResMagic));
  buf += sizeof(COFF::WinResMagic);
  memset(buf, 0, object::WIN_RES_NULL_ENTRY_SIZE);
  buf += object::WIN_RES_NULL_ENTRY_SIZE;
}

static void writeResEntryHeader(char *&buf, size_t manifestSize,
                                int manifestID) {
  // Write the prefix.
  auto *prefix = reinterpret_cast<object::WinResHeaderPrefix *>(buf);
  prefix->DataSize = manifestSize;
  prefix->HeaderSize = sizeof(object::WinResHeaderPrefix) +
                       sizeof(object::WinResIDs) +
                       sizeof(object::WinResHeaderSuffix);
  buf += sizeof(object::WinResHeaderPrefix);

  // Write the Type/Name IDs.
  auto *iDs = reinterpret_cast<object::WinResIDs *>(buf);
  iDs->setType(RT_MANIFEST);
  iDs->setName(manifestID);
  buf += sizeof(object::WinResIDs);

  // Write the suffix.
  auto *suffix = reinterpret_cast<object::WinResHeaderSuffix *>(buf);
  suffix->DataVersion = 0;
  suffix->MemoryFlags = object::WIN_RES_PURE_MOVEABLE;
  suffix->Language = SUBLANG_ENGLISH_US;
  suffix->Version = 0;
  suffix->Characteristics = 0;
  buf += sizeof(object::WinResHeaderSuffix);
}

// Create a resource file containing a manifest XML.
std::unique_ptr<MemoryBuffer> LinkerDriver::createManifestRes() {
  std::string manifest = createManifestXml();

  std::unique_ptr<WritableMemoryBuffer> res =
      createMemoryBufferForManifestRes(manifest.size());

  char *buf = res->getBufferStart();
  writeResFileHeader(buf);
  writeResEntryHeader(buf, manifest.size(), ctx.config.manifestID);

  // Copy the manifest data into the .res file.
  std::copy(manifest.begin(), manifest.end(), buf);
  return std::move(res);
}

void LinkerDriver::createSideBySideManifest() {
  std::string path = std::string(ctx.config.manifestFile);
  if (path == "")
    path = ctx.config.outputFile + ".manifest";
  std::error_code ec;
  raw_fd_ostream out(path, ec, sys::fs::OF_TextWithCRLF);
  if (ec)
    fatal("failed to create manifest: " + ec.message());
  out << createManifestXml();
}

// Parse a string in the form of
// "<name>[=<internalname>][,@ordinal[,NONAME]][,DATA][,PRIVATE]"
// or "<name>=<dllname>.<name>".
// Used for parsing /export arguments.
Export LinkerDriver::parseExport(StringRef arg) {
  Export e;
  e.source = ExportSource::Export;

  StringRef rest;
  std::tie(e.name, rest) = arg.split(",");
  if (e.name.empty())
    goto err;

  if (e.name.contains('=')) {
    auto [x, y] = e.name.split("=");

    // If "<name>=<dllname>.<name>".
    if (y.contains(".")) {
      e.name = x;
      e.forwardTo = y;
    } else {
      e.extName = x;
      e.name = y;
      if (e.name.empty())
        goto err;
    }
  }

  // Optional parameters
  // "[,@ordinal[,NONAME]][,DATA][,PRIVATE][,EXPORTAS,exportname]"
  while (!rest.empty()) {
    StringRef tok;
    std::tie(tok, rest) = rest.split(",");
    if (tok.equals_insensitive("noname")) {
      if (e.ordinal == 0)
        goto err;
      e.noname = true;
      continue;
    }
    if (tok.equals_insensitive("data")) {
      e.data = true;
      continue;
    }
    if (tok.equals_insensitive("constant")) {
      e.constant = true;
      continue;
    }
    if (tok.equals_insensitive("private")) {
      e.isPrivate = true;
      continue;
    }
    if (tok.equals_insensitive("exportas")) {
      if (!rest.empty() && !rest.contains(','))
        e.exportAs = rest;
      else
        error("invalid EXPORTAS value: " + rest);
      break;
    }
    if (tok.starts_with("@")) {
      int32_t ord;
      if (tok.substr(1).getAsInteger(0, ord))
        goto err;
      if (ord <= 0 || 65535 < ord)
        goto err;
      e.ordinal = ord;
      continue;
    }
    goto err;
  }
  return e;

err:
  fatal("invalid /export: " + arg);
}

// Convert stdcall/fastcall style symbols into unsuffixed symbols,
// with or without a leading underscore. (MinGW specific.)
static StringRef killAt(StringRef sym, bool prefix) {
  if (sym.empty())
    return sym;
  // Strip any trailing stdcall suffix
  sym = sym.substr(0, sym.find('@', 1));
  if (!sym.starts_with("@")) {
    if (prefix && !sym.starts_with("_"))
      return saver().save("_" + sym);
    return sym;
  }
  // For fastcall, remove the leading @ and replace it with an
  // underscore, if prefixes are used.
  sym = sym.substr(1);
  if (prefix)
    sym = saver().save("_" + sym);
  return sym;
}

static StringRef exportSourceName(ExportSource s) {
  switch (s) {
  case ExportSource::Directives:
    return "source file (directives)";
  case ExportSource::Export:
    return "/export";
  case ExportSource::ModuleDefinition:
    return "/def";
  default:
    llvm_unreachable("unknown ExportSource");
  }
}

// Performs error checking on all /export arguments.
// It also sets ordinals.
void LinkerDriver::fixupExports() {
  llvm::TimeTraceScope timeScope("Fixup exports");
  // Symbol ordinals must be unique.
  std::set<uint16_t> ords;
  for (Export &e : ctx.config.exports) {
    if (e.ordinal == 0)
      continue;
    if (!ords.insert(e.ordinal).second)
      fatal("duplicate export ordinal: " + e.name);
  }

  for (Export &e : ctx.config.exports) {
    if (!e.exportAs.empty()) {
      e.exportName = e.exportAs;
      continue;
    }

    StringRef sym =
        !e.forwardTo.empty() || e.extName.empty() ? e.name : e.extName;
    if (ctx.config.machine == I386 && sym.starts_with("_")) {
      // In MSVC mode, a fully decorated stdcall function is exported
      // as-is with the leading underscore (with type IMPORT_NAME).
      // In MinGW mode, a decorated stdcall function gets the underscore
      // removed, just like normal cdecl functions.
      if (ctx.config.mingw || !sym.contains('@')) {
        e.exportName = sym.substr(1);
        continue;
      }
    }
    if (isArm64EC(ctx.config.machine) && !e.data && !e.constant) {
      if (std::optional<std::string> demangledName =
              getArm64ECDemangledFunctionName(sym)) {
        e.exportName = saver().save(*demangledName);
        continue;
      }
    }
    e.exportName = sym;
  }

  if (ctx.config.killAt && ctx.config.machine == I386) {
    for (Export &e : ctx.config.exports) {
      e.name = killAt(e.name, true);
      e.exportName = killAt(e.exportName, false);
      e.extName = killAt(e.extName, true);
      e.symbolName = killAt(e.symbolName, true);
    }
  }

  // Uniquefy by name.
  DenseMap<StringRef, std::pair<Export *, unsigned>> map(
      ctx.config.exports.size());
  std::vector<Export> v;
  for (Export &e : ctx.config.exports) {
    auto pair = map.insert(std::make_pair(e.exportName, std::make_pair(&e, 0)));
    bool inserted = pair.second;
    if (inserted) {
      pair.first->second.second = v.size();
      v.push_back(e);
      continue;
    }
    Export *existing = pair.first->second.first;
    if (e == *existing || e.name != existing->name)
      continue;
    // If the existing export comes from .OBJ directives, we are allowed to
    // overwrite it with /DEF: or /EXPORT without any warning, as MSVC link.exe
    // does.
    if (existing->source == ExportSource::Directives) {
      *existing = e;
      v[pair.first->second.second] = e;
      continue;
    }
    if (existing->source == e.source) {
      warn(Twine("duplicate ") + exportSourceName(existing->source) +
           " option: " + e.name);
    } else {
      warn("duplicate export: " + e.name +
           Twine(" first seen in " + exportSourceName(existing->source) +
                 Twine(", now in " + exportSourceName(e.source))));
    }
  }
  ctx.config.exports = std::move(v);

  // Sort by name.
  llvm::sort(ctx.config.exports, [](const Export &a, const Export &b) {
    return a.exportName < b.exportName;
  });
}

void LinkerDriver::assignExportOrdinals() {
  // Assign unique ordinals if default (= 0).
  uint32_t max = 0;
  for (Export &e : ctx.config.exports)
    max = std::max(max, (uint32_t)e.ordinal);
  for (Export &e : ctx.config.exports)
    if (e.ordinal == 0)
      e.ordinal = ++max;
  if (max > std::numeric_limits<uint16_t>::max())
    fatal("too many exported symbols (got " + Twine(max) + ", max " +
          Twine(std::numeric_limits<uint16_t>::max()) + ")");
}

// Parses a string in the form of "key=value" and check
// if value matches previous values for the same key.
void LinkerDriver::checkFailIfMismatch(StringRef arg, InputFile *source) {
  auto [k, v] = arg.split('=');
  if (k.empty() || v.empty())
    fatal("/failifmismatch: invalid argument: " + arg);
  std::pair<StringRef, InputFile *> existing = ctx.config.mustMatch[k];
  if (!existing.first.empty() && v != existing.first) {
    std::string sourceStr = source ? toString(source) : "cmd-line";
    std::string existingStr =
        existing.second ? toString(existing.second) : "cmd-line";
    fatal("/failifmismatch: mismatch detected for '" + k + "':\n>>> " +
          existingStr + " has value " + existing.first + "\n>>> " + sourceStr +
          " has value " + v);
  }
  ctx.config.mustMatch[k] = {v, source};
}

// Convert Windows resource files (.res files) to a .obj file.
// Does what cvtres.exe does, but in-process and cross-platform.
MemoryBufferRef LinkerDriver::convertResToCOFF(ArrayRef<MemoryBufferRef> mbs,
                                               ArrayRef<ObjFile *> objs) {
  object::WindowsResourceParser parser(/* MinGW */ ctx.config.mingw);

  std::vector<std::string> duplicates;
  for (MemoryBufferRef mb : mbs) {
    std::unique_ptr<object::Binary> bin = check(object::createBinary(mb));
    object::WindowsResource *rf = dyn_cast<object::WindowsResource>(bin.get());
    if (!rf)
      fatal("cannot compile non-resource file as resource");

    if (auto ec = parser.parse(rf, duplicates))
      fatal(toString(std::move(ec)));
  }

  // Note: This processes all .res files before all objs. Ideally they'd be
  // handled in the same order they were linked (to keep the right one, if
  // there are duplicates that are tolerated due to forceMultipleRes).
  for (ObjFile *f : objs) {
    object::ResourceSectionRef rsf;
    if (auto ec = rsf.load(f->getCOFFObj()))
      fatal(toString(f) + ": " + toString(std::move(ec)));

    if (auto ec = parser.parse(rsf, f->getName(), duplicates))
      fatal(toString(std::move(ec)));
  }

  if (ctx.config.mingw)
    parser.cleanUpManifests(duplicates);

  for (const auto &dupeDiag : duplicates)
    if (ctx.config.forceMultipleRes)
      warn(dupeDiag);
    else
      error(dupeDiag);

  Expected<std::unique_ptr<MemoryBuffer>> e =
      llvm::object::writeWindowsResourceCOFF(ctx.config.machine, parser,
                                             ctx.config.timestamp);
  if (!e)
    fatal("failed to write .res to COFF: " + toString(e.takeError()));

  MemoryBufferRef mbref = **e;
  make<std::unique_ptr<MemoryBuffer>>(std::move(*e)); // take ownership
  return mbref;
}

// Create OptTable

// Create prefix string literals used in Options.td
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr llvm::StringLiteral NAME##_init[] = VALUE;                  \
  static constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                   \
      NAME##_init, std::size(NAME##_init) - 1);
#include "Options.inc"
#undef PREFIX

// Create table mapping all options defined in Options.td
static constexpr llvm::opt::OptTable::Info infoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

COFFOptTable::COFFOptTable() : GenericOptTable(infoTable, true) {}

// Set color diagnostics according to --color-diagnostics={auto,always,never}
// or --no-color-diagnostics flags.
static void handleColorDiagnostics(opt::InputArgList &args) {
  auto *arg = args.getLastArg(OPT_color_diagnostics, OPT_color_diagnostics_eq,
                              OPT_no_color_diagnostics);
  if (!arg)
    return;
  if (arg->getOption().getID() == OPT_color_diagnostics) {
    lld::errs().enable_colors(true);
  } else if (arg->getOption().getID() == OPT_no_color_diagnostics) {
    lld::errs().enable_colors(false);
  } else {
    StringRef s = arg->getValue();
    if (s == "always")
      lld::errs().enable_colors(true);
    else if (s == "never")
      lld::errs().enable_colors(false);
    else if (s != "auto")
      error("unknown option: --color-diagnostics=" + s);
  }
}

static cl::TokenizerCallback getQuotingStyle(opt::InputArgList &args) {
  if (auto *arg = args.getLastArg(OPT_rsp_quoting)) {
    StringRef s = arg->getValue();
    if (s != "windows" && s != "posix")
      error("invalid response file quoting: " + s);
    if (s == "windows")
      return cl::TokenizeWindowsCommandLine;
    return cl::TokenizeGNUCommandLine;
  }
  // The COFF linker always defaults to Windows quoting.
  return cl::TokenizeWindowsCommandLine;
}

ArgParser::ArgParser(COFFLinkerContext &c) : ctx(c) {}

// Parses a given list of options.
opt::InputArgList ArgParser::parse(ArrayRef<const char *> argv) {
  // Make InputArgList from string vectors.
  unsigned missingIndex;
  unsigned missingCount;

  // We need to get the quoting style for response files before parsing all
  // options so we parse here before and ignore all the options but
  // --rsp-quoting and /lldignoreenv.
  // (This means --rsp-quoting can't be added through %LINK%.)
  opt::InputArgList args =
      ctx.optTable.ParseArgs(argv, missingIndex, missingCount);

  // Expand response files (arguments in the form of @<filename>) and insert
  // flags from %LINK% and %_LINK_%, and then parse the argument again.
  SmallVector<const char *, 256> expandedArgv(argv.data(),
                                              argv.data() + argv.size());
  if (!args.hasArg(OPT_lldignoreenv))
    addLINK(expandedArgv);
  cl::ExpandResponseFiles(saver(), getQuotingStyle(args), expandedArgv);
  args = ctx.optTable.ParseArgs(ArrayRef(expandedArgv).drop_front(),
                                missingIndex, missingCount);

  // Print the real command line if response files are expanded.
  if (args.hasArg(OPT_verbose) && argv.size() != expandedArgv.size()) {
    std::string msg = "Command line:";
    for (const char *s : expandedArgv)
      msg += " " + std::string(s);
    message(msg);
  }

  // Save the command line after response file expansion so we can write it to
  // the PDB if necessary. Mimic MSVC, which skips input files.
  ctx.config.argv = {argv[0]};
  for (opt::Arg *arg : args) {
    if (arg->getOption().getKind() != opt::Option::InputClass) {
      ctx.config.argv.emplace_back(args.getArgString(arg->getIndex()));
    }
  }

  // Handle /WX early since it converts missing argument warnings to errors.
  errorHandler().fatalWarnings = args.hasFlag(OPT_WX, OPT_WX_no, false);

  if (missingCount)
    fatal(Twine(args.getArgString(missingIndex)) + ": missing argument");

  handleColorDiagnostics(args);

  for (opt::Arg *arg : args.filtered(OPT_UNKNOWN)) {
    std::string nearest;
    if (ctx.optTable.findNearest(arg->getAsString(args), nearest) > 1)
      warn("ignoring unknown argument '" + arg->getAsString(args) + "'");
    else
      warn("ignoring unknown argument '" + arg->getAsString(args) +
           "', did you mean '" + nearest + "'");
  }

  if (args.hasArg(OPT_lib))
    warn("ignoring /lib since it's not the first argument");

  return args;
}

// Tokenizes and parses a given string as command line in .drective section.
ParsedDirectives ArgParser::parseDirectives(StringRef s) {
  ParsedDirectives result;
  SmallVector<const char *, 16> rest;

  // Handle /EXPORT and /INCLUDE in a fast path. These directives can appear for
  // potentially every symbol in the object, so they must be handled quickly.
  SmallVector<StringRef, 16> tokens;
  cl::TokenizeWindowsCommandLineNoCopy(s, saver(), tokens);
  for (StringRef tok : tokens) {
    if (tok.starts_with_insensitive("/export:") ||
        tok.starts_with_insensitive("-export:"))
      result.exports.push_back(tok.substr(strlen("/export:")));
    else if (tok.starts_with_insensitive("/include:") ||
             tok.starts_with_insensitive("-include:"))
      result.includes.push_back(tok.substr(strlen("/include:")));
    else if (tok.starts_with_insensitive("/exclude-symbols:") ||
             tok.starts_with_insensitive("-exclude-symbols:"))
      result.excludes.push_back(tok.substr(strlen("/exclude-symbols:")));
    else {
      // Copy substrings that are not valid C strings. The tokenizer may have
      // already copied quoted arguments for us, so those do not need to be
      // copied again.
      bool HasNul = tok.end() != s.end() && tok.data()[tok.size()] == '\0';
      rest.push_back(HasNul ? tok.data() : saver().save(tok).data());
    }
  }

  // Make InputArgList from unparsed string vectors.
  unsigned missingIndex;
  unsigned missingCount;

  result.args = ctx.optTable.ParseArgs(rest, missingIndex, missingCount);

  if (missingCount)
    fatal(Twine(result.args.getArgString(missingIndex)) + ": missing argument");
  for (auto *arg : result.args.filtered(OPT_UNKNOWN))
    warn("ignoring unknown argument: " + arg->getAsString(result.args));
  return result;
}

// link.exe has an interesting feature. If LINK or _LINK_ environment
// variables exist, their contents are handled as command line strings.
// So you can pass extra arguments using them.
void ArgParser::addLINK(SmallVector<const char *, 256> &argv) {
  // Concatenate LINK env and command line arguments, and then parse them.
  if (std::optional<std::string> s = Process::GetEnv("LINK")) {
    std::vector<const char *> v = tokenize(*s);
    argv.insert(std::next(argv.begin()), v.begin(), v.end());
  }
  if (std::optional<std::string> s = Process::GetEnv("_LINK_")) {
    std::vector<const char *> v = tokenize(*s);
    argv.insert(std::next(argv.begin()), v.begin(), v.end());
  }
}

std::vector<const char *> ArgParser::tokenize(StringRef s) {
  SmallVector<const char *, 16> tokens;
  cl::TokenizeWindowsCommandLine(s, saver(), tokens);
  return std::vector<const char *>(tokens.begin(), tokens.end());
}

void LinkerDriver::printHelp(const char *argv0) {
  ctx.optTable.printHelp(lld::outs(),
                         (std::string(argv0) + " [options] file...").c_str(),
                         "LLVM Linker", false);
}

} // namespace coff
} // namespace lld
