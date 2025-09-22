//===- Driver.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The driver drives the entire linking process. It is responsible for
// parsing command line options and doing whatever it is instructed to do.
//
// One notable thing in the LLD's driver when compared to other linkers is
// that the LLD's driver is agnostic on the host operating system.
// Other linkers usually have implicit default values (such as a dynamic
// linker path or library paths) for each host OS.
//
// I don't think implicit default values are useful because they are
// usually explicitly specified by the compiler ctx.driver. They can even
// be harmful when you are doing cross-linking. Therefore, in LLD, we
// simply trust the compiler driver to pass all required options and
// don't try to make effort on our side.
//
//===----------------------------------------------------------------------===//

#include "Driver.h"
#include "Config.h"
#include "ICF.h"
#include "InputFiles.h"
#include "InputSection.h"
#include "LTO.h"
#include "LinkerScript.h"
#include "MarkLive.h"
#include "OutputSections.h"
#include "ScriptParser.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "Writer.h"
#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Driver.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Filesystem.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Remarks/HotnessThresholdParser.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <tuple>
#include <utility>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::sys;
using namespace llvm::support;
using namespace lld;
using namespace lld::elf;

ConfigWrapper elf::config;
Ctx elf::ctx;

static void setConfigs(opt::InputArgList &args);
static void readConfigs(opt::InputArgList &args);

void elf::errorOrWarn(const Twine &msg) {
  if (config->noinhibitExec)
    warn(msg);
  else
    error(msg);
}

void Ctx::reset() {
  driver = LinkerDriver();
  memoryBuffers.clear();
  objectFiles.clear();
  sharedFiles.clear();
  binaryFiles.clear();
  bitcodeFiles.clear();
  lazyBitcodeFiles.clear();
  inputSections.clear();
  ehInputSections.clear();
  duplicates.clear();
  nonPrevailingSyms.clear();
  whyExtractRecords.clear();
  backwardReferences.clear();
  auxiliaryFiles.clear();
  internalFile = nullptr;
  hasSympart.store(false, std::memory_order_relaxed);
  hasTlsIe.store(false, std::memory_order_relaxed);
  needsTlsLd.store(false, std::memory_order_relaxed);
  scriptSymOrderCounter = 1;
  scriptSymOrder.clear();
  ltoAllVtablesHaveTypeInfos = false;
}

llvm::raw_fd_ostream Ctx::openAuxiliaryFile(llvm::StringRef filename,
                                            std::error_code &ec) {
  using namespace llvm::sys::fs;
  OpenFlags flags =
      auxiliaryFiles.insert(filename).second ? OF_None : OF_Append;
  return {filename, ec, flags};
}

namespace lld {
namespace elf {
bool link(ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput) {
  // This driver-specific context will be freed later by unsafeLldMain().
  auto *ctx = new CommonLinkerContext;

  ctx->e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);
  ctx->e.cleanupCallback = []() {
    elf::ctx.reset();
    symtab = SymbolTable();

    outputSections.clear();
    symAux.clear();

    tar = nullptr;
    in.reset();

    partitions.clear();
    partitions.emplace_back();

    SharedFile::vernauxNum = 0;
  };
  ctx->e.logName = args::getFilenameWithoutExe(args[0]);
  ctx->e.errorLimitExceededMsg = "too many errors emitted, stopping now (use "
                                 "--error-limit=0 to see all errors)";

  config = ConfigWrapper();
  script = ScriptWrapper();

  symAux.emplace_back();

  partitions.clear();
  partitions.emplace_back();

  config->progName = args[0];

  elf::ctx.driver.linkerMain(args);

  return errorCount() == 0;
}
} // namespace elf
} // namespace lld

// Parses a linker -m option.
static std::tuple<ELFKind, uint16_t, uint8_t> parseEmulation(StringRef emul) {
  uint8_t osabi = 0;
  StringRef s = emul;
  if (s.ends_with("_fbsd")) {
    s = s.drop_back(5);
    osabi = ELFOSABI_FREEBSD;
  }

  std::pair<ELFKind, uint16_t> ret =
      StringSwitch<std::pair<ELFKind, uint16_t>>(s)
          .Cases("aarch64elf", "aarch64linux", {ELF64LEKind, EM_AARCH64})
          .Cases("aarch64elfb", "aarch64linuxb", {ELF64BEKind, EM_AARCH64})
          .Cases("armelf", "armelf_linux_eabi", {ELF32LEKind, EM_ARM})
          .Cases("armelfb", "armelfb_linux_eabi", {ELF32BEKind, EM_ARM})
          .Case("elf32_x86_64", {ELF32LEKind, EM_X86_64})
          .Cases("elf32btsmip", "elf32btsmipn32", {ELF32BEKind, EM_MIPS})
          .Cases("elf32ltsmip", "elf32ltsmipn32", {ELF32LEKind, EM_MIPS})
          .Case("elf32lriscv", {ELF32LEKind, EM_RISCV})
          .Cases("elf32ppc", "elf32ppclinux", {ELF32BEKind, EM_PPC})
          .Cases("elf32lppc", "elf32lppclinux", {ELF32LEKind, EM_PPC})
          .Case("elf32loongarch", {ELF32LEKind, EM_LOONGARCH})
          .Case("elf64btsmip", {ELF64BEKind, EM_MIPS})
          .Case("elf64ltsmip", {ELF64LEKind, EM_MIPS})
          .Case("elf64lriscv", {ELF64LEKind, EM_RISCV})
          .Case("elf64ppc", {ELF64BEKind, EM_PPC64})
          .Case("elf64lppc", {ELF64LEKind, EM_PPC64})
          .Cases("elf_amd64", "elf_x86_64", {ELF64LEKind, EM_X86_64})
          .Case("elf_i386", {ELF32LEKind, EM_386})
          .Case("elf_iamcu", {ELF32LEKind, EM_IAMCU})
          .Case("elf64_sparc", {ELF64BEKind, EM_SPARCV9})
          .Case("msp430elf", {ELF32LEKind, EM_MSP430})
          .Case("elf64_amdgpu", {ELF64LEKind, EM_AMDGPU})
          .Case("elf64loongarch", {ELF64LEKind, EM_LOONGARCH})
          .Case("elf64_s390", {ELF64BEKind, EM_S390})
          .Case("hexagonelf", {ELF32LEKind, EM_HEXAGON})
          .Default({ELFNoneKind, EM_NONE});

  if (ret.first == ELFNoneKind)
    error("unknown emulation: " + emul);
  if (ret.second == EM_MSP430)
    osabi = ELFOSABI_STANDALONE;
  else if (ret.second == EM_AMDGPU)
    osabi = ELFOSABI_AMDGPU_HSA;
  return std::make_tuple(ret.first, ret.second, osabi);
}

// Returns slices of MB by parsing MB as an archive file.
// Each slice consists of a member file in the archive.
std::vector<std::pair<MemoryBufferRef, uint64_t>> static getArchiveMembers(
    MemoryBufferRef mb) {
  std::unique_ptr<Archive> file =
      CHECK(Archive::create(mb),
            mb.getBufferIdentifier() + ": failed to parse archive");

  std::vector<std::pair<MemoryBufferRef, uint64_t>> v;
  Error err = Error::success();
  bool addToTar = file->isThin() && tar;
  for (const Archive::Child &c : file->children(err)) {
    MemoryBufferRef mbref =
        CHECK(c.getMemoryBufferRef(),
              mb.getBufferIdentifier() +
                  ": could not get the buffer for a child of the archive");
    if (addToTar)
      tar->append(relativeToRoot(check(c.getFullName())), mbref.getBuffer());
    v.push_back(std::make_pair(mbref, c.getChildOffset()));
  }
  if (err)
    fatal(mb.getBufferIdentifier() + ": Archive::children failed: " +
          toString(std::move(err)));

  // Take ownership of memory buffers created for members of thin archives.
  std::vector<std::unique_ptr<MemoryBuffer>> mbs = file->takeThinBuffers();
  std::move(mbs.begin(), mbs.end(), std::back_inserter(ctx.memoryBuffers));

  return v;
}

static bool isBitcode(MemoryBufferRef mb) {
  return identify_magic(mb.getBuffer()) == llvm::file_magic::bitcode;
}

bool LinkerDriver::tryAddFatLTOFile(MemoryBufferRef mb, StringRef archiveName,
                                    uint64_t offsetInArchive, bool lazy) {
  if (!config->fatLTOObjects)
    return false;
  Expected<MemoryBufferRef> fatLTOData =
      IRObjectFile::findBitcodeInMemBuffer(mb);
  if (errorToBool(fatLTOData.takeError()))
    return false;
  files.push_back(
      make<BitcodeFile>(*fatLTOData, archiveName, offsetInArchive, lazy));
  return true;
}

// Opens a file and create a file object. Path has to be resolved already.
void LinkerDriver::addFile(StringRef path, bool withLOption) {
  using namespace sys::fs;

  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return;
  MemoryBufferRef mbref = *buffer;

  if (config->formatBinary) {
    files.push_back(make<BinaryFile>(mbref));
    return;
  }

  switch (identify_magic(mbref.getBuffer())) {
  case file_magic::unknown:
    readLinkerScript(mbref);
    return;
  case file_magic::archive: {
    auto members = getArchiveMembers(mbref);
    if (inWholeArchive) {
      for (const std::pair<MemoryBufferRef, uint64_t> &p : members) {
        if (isBitcode(p.first))
          files.push_back(make<BitcodeFile>(p.first, path, p.second, false));
        else if (!tryAddFatLTOFile(p.first, path, p.second, false))
          files.push_back(createObjFile(p.first, path));
      }
      return;
    }

    archiveFiles.emplace_back(path, members.size());

    // Handle archives and --start-lib/--end-lib using the same code path. This
    // scans all the ELF relocatable object files and bitcode files in the
    // archive rather than just the index file, with the benefit that the
    // symbols are only loaded once. For many projects archives see high
    // utilization rates and it is a net performance win. --start-lib scans
    // symbols in the same order that llvm-ar adds them to the index, so in the
    // common case the semantics are identical. If the archive symbol table was
    // created in a different order, or is incomplete, this strategy has
    // different semantics. Such output differences are considered user error.
    //
    // All files within the archive get the same group ID to allow mutual
    // references for --warn-backrefs.
    bool saved = InputFile::isInGroup;
    InputFile::isInGroup = true;
    for (const std::pair<MemoryBufferRef, uint64_t> &p : members) {
      auto magic = identify_magic(p.first.getBuffer());
      if (magic == file_magic::elf_relocatable) {
        if (!tryAddFatLTOFile(p.first, path, p.second, true))
          files.push_back(createObjFile(p.first, path, true));
      } else if (magic == file_magic::bitcode)
        files.push_back(make<BitcodeFile>(p.first, path, p.second, true));
      else
        warn(path + ": archive member '" + p.first.getBufferIdentifier() +
             "' is neither ET_REL nor LLVM bitcode");
    }
    InputFile::isInGroup = saved;
    if (!saved)
      ++InputFile::nextGroupId;
    return;
  }
  case file_magic::elf_shared_object: {
    if (config->isStatic) {
      error("attempted static link of dynamic object " + path);
      return;
    }

    // Shared objects are identified by soname. soname is (if specified)
    // DT_SONAME and falls back to filename. If a file was specified by -lfoo,
    // the directory part is ignored. Note that path may be a temporary and
    // cannot be stored into SharedFile::soName.
    path = mbref.getBufferIdentifier();
    auto *f =
        make<SharedFile>(mbref, withLOption ? path::filename(path) : path);
    f->init();
    files.push_back(f);
    return;
  }
  case file_magic::bitcode:
    files.push_back(make<BitcodeFile>(mbref, "", 0, inLib));
    break;
  case file_magic::elf_relocatable:
    if (!tryAddFatLTOFile(mbref, "", 0, inLib))
      files.push_back(createObjFile(mbref, "", inLib));
    break;
  default:
    error(path + ": unknown file type");
  }
}

// Add a given library by searching it from input search paths.
void LinkerDriver::addLibrary(StringRef name) {
  if (std::optional<std::string> path = searchLibrary(name))
    addFile(saver().save(*path), /*withLOption=*/true);
  else
    error("unable to find library -l" + name, ErrorTag::LibNotFound, {name});
}

// This function is called on startup. We need this for LTO since
// LTO calls LLVM functions to compile bitcode files to native code.
// Technically this can be delayed until we read bitcode files, but
// we don't bother to do lazily because the initialization is fast.
static void initLLVM() {
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();
}

// Some command line options or some combinations of them are not allowed.
// This function checks for such errors.
static void checkOptions() {
  // The MIPS ABI as of 2016 does not support the GNU-style symbol lookup
  // table which is a relatively new feature.
  if (config->emachine == EM_MIPS && config->gnuHash)
    error("the .gnu.hash section is not compatible with the MIPS target");

  if (config->emachine == EM_ARM) {
    if (!config->cmseImplib) {
      if (!config->cmseInputLib.empty())
        error("--in-implib may not be used without --cmse-implib");
      if (!config->cmseOutputLib.empty())
        error("--out-implib may not be used without --cmse-implib");
    }
  } else {
    if (config->cmseImplib)
      error("--cmse-implib is only supported on ARM targets");
    if (!config->cmseInputLib.empty())
      error("--in-implib is only supported on ARM targets");
    if (!config->cmseOutputLib.empty())
      error("--out-implib is only supported on ARM targets");
  }

  if (config->fixCortexA53Errata843419 && config->emachine != EM_AARCH64)
    error("--fix-cortex-a53-843419 is only supported on AArch64 targets");

  if (config->fixCortexA8 && config->emachine != EM_ARM)
    error("--fix-cortex-a8 is only supported on ARM targets");

  if (config->armBe8 && config->emachine != EM_ARM)
    error("--be8 is only supported on ARM targets");

  if (config->fixCortexA8 && !config->isLE)
    error("--fix-cortex-a8 is not supported on big endian targets");

  if (config->tocOptimize && config->emachine != EM_PPC64)
    error("--toc-optimize is only supported on PowerPC64 targets");

  if (config->pcRelOptimize && config->emachine != EM_PPC64)
    error("--pcrel-optimize is only supported on PowerPC64 targets");

  if (config->relaxGP && config->emachine != EM_RISCV)
    error("--relax-gp is only supported on RISC-V targets");

  if (config->pie && config->shared)
    error("-shared and -pie may not be used together");

  if (!config->shared && !config->filterList.empty())
    error("-F may not be used without -shared");

  if (!config->shared && !config->auxiliaryList.empty())
    error("-f may not be used without -shared");

  if (config->strip == StripPolicy::All && config->emitRelocs)
    error("--strip-all and --emit-relocs may not be used together");

  if (config->zText && config->zIfuncNoplt)
    error("-z text and -z ifunc-noplt may not be used together");

  if (config->relocatable) {
    if (config->shared)
      error("-r and -shared may not be used together");
    if (config->gdbIndex)
      error("-r and --gdb-index may not be used together");
    if (config->icf != ICFLevel::None)
      error("-r and --icf may not be used together");
    if (config->pie)
      error("-r and -pie may not be used together");
    if (config->exportDynamic)
      error("-r and --export-dynamic may not be used together");
    if (config->debugNames)
      error("-r and --debug-names may not be used together");
  }

  if (config->executeOnly) {
    switch (config->emachine) {
    case EM_386:
    case EM_AARCH64:
    case EM_MIPS:
    case EM_PPC:
    case EM_PPC64:
    case EM_RISCV:
    case EM_SPARCV9:
    case EM_X86_64:
      break;
    default:
      error("-execute-only is not supported on this target");
    }

    if (config->singleRoRx && !script->hasSectionsCommand)
      error("--execute-only and --no-rosegment cannot be used together");
  }

  if (config->zRetpolineplt && config->zForceIbt)
    error("-z force-ibt may not be used with -z retpolineplt");

  if (config->emachine != EM_AARCH64) {
    if (config->zPacPlt)
      error("-z pac-plt only supported on AArch64");
    if (config->zForceBti)
      error("-z force-bti only supported on AArch64");
    if (config->zBtiReport != "none")
      error("-z bti-report only supported on AArch64");
    if (config->zPauthReport != "none")
      error("-z pauth-report only supported on AArch64");
    if (config->zGcsReport != "none")
      error("-z gcs-report only supported on AArch64");
    if (config->zGcs != GcsPolicy::Implicit)
      error("-z gcs only supported on AArch64");
  }

  if (config->emachine != EM_386 && config->emachine != EM_X86_64 &&
      config->zCetReport != "none")
    error("-z cet-report only supported on X86 and X86_64");
}

static const char *getReproduceOption(opt::InputArgList &args) {
  if (auto *arg = args.getLastArg(OPT_reproduce))
    return arg->getValue();
  return getenv("LLD_REPRODUCE");
}

static bool hasZOption(opt::InputArgList &args, StringRef key) {
  bool ret = false;
  for (auto *arg : args.filtered(OPT_z))
    if (key == arg->getValue()) {
      ret = true;
      arg->claim();
    }
  return ret;
}

static bool getZFlag(opt::InputArgList &args, StringRef k1, StringRef k2,
                     bool defaultValue) {
  for (auto *arg : args.filtered(OPT_z)) {
    StringRef v = arg->getValue();
    if (k1 == v)
      defaultValue = true;
    else if (k2 == v)
      defaultValue = false;
    else
      continue;
    arg->claim();
  }
  return defaultValue;
}

static SeparateSegmentKind getZSeparate(opt::InputArgList &args) {
  auto ret = SeparateSegmentKind::None;
  for (auto *arg : args.filtered(OPT_z)) {
    StringRef v = arg->getValue();
    if (v == "noseparate-code")
      ret = SeparateSegmentKind::None;
    else if (v == "separate-code")
      ret = SeparateSegmentKind::Code;
    else if (v == "separate-loadable-segments")
      ret = SeparateSegmentKind::Loadable;
    else
      continue;
    arg->claim();
  }
  return ret;
}

static GnuStackKind getZGnuStack(opt::InputArgList &args) {
  auto ret = GnuStackKind::NoExec;
  for (auto *arg : args.filtered(OPT_z)) {
    StringRef v = arg->getValue();
    if (v == "execstack")
      ret = GnuStackKind::Exec;
    else if (v == "noexecstack")
      ret = GnuStackKind::NoExec;
    else if (v == "nognustack")
      ret = GnuStackKind::None;
    else
      continue;
    arg->claim();
  }
  return ret;
}

static uint8_t getZStartStopVisibility(opt::InputArgList &args) {
  uint8_t ret = STV_PROTECTED;
  for (auto *arg : args.filtered(OPT_z)) {
    std::pair<StringRef, StringRef> kv = StringRef(arg->getValue()).split('=');
    if (kv.first == "start-stop-visibility") {
      arg->claim();
      if (kv.second == "default")
        ret = STV_DEFAULT;
      else if (kv.second == "internal")
        ret = STV_INTERNAL;
      else if (kv.second == "hidden")
        ret = STV_HIDDEN;
      else if (kv.second == "protected")
        ret = STV_PROTECTED;
      else
        error("unknown -z start-stop-visibility= value: " +
              StringRef(kv.second));
    }
  }
  return ret;
}

static GcsPolicy getZGcs(opt::InputArgList &args) {
  GcsPolicy ret = GcsPolicy::Implicit;
  for (auto *arg : args.filtered(OPT_z)) {
    std::pair<StringRef, StringRef> kv = StringRef(arg->getValue()).split('=');
    if (kv.first == "gcs") {
      arg->claim();
      if (kv.second == "implicit")
        ret = GcsPolicy::Implicit;
      else if (kv.second == "never")
        ret = GcsPolicy::Never;
      else if (kv.second == "always")
        ret = GcsPolicy::Always;
      else
        error("unknown -z gcs= value: " + kv.second);
    }
  }
  return ret;
}

// Report a warning for an unknown -z option.
static void checkZOptions(opt::InputArgList &args) {
  // This function is called before getTarget(), when certain options are not
  // initialized yet. Claim them here.
  args::getZOptionValue(args, OPT_z, "max-page-size", 0);
  args::getZOptionValue(args, OPT_z, "common-page-size", 0);
  getZFlag(args, "rel", "rela", false);
  for (auto *arg : args.filtered(OPT_z))
    if (!arg->isClaimed())
      warn("unknown -z value: " + StringRef(arg->getValue()));
}

constexpr const char *saveTempsValues[] = {
    "resolution", "preopt",     "promote", "internalize",  "import",
    "opt",        "precodegen", "prelink", "combinedindex"};

void LinkerDriver::linkerMain(ArrayRef<const char *> argsArr) {
  ELFOptTable parser;
  opt::InputArgList args = parser.parse(argsArr.slice(1));

  // Interpret these flags early because error()/warn() depend on them.
  errorHandler().errorLimit = args::getInteger(args, OPT_error_limit, 20);
  errorHandler().fatalWarnings =
      args.hasFlag(OPT_fatal_warnings, OPT_no_fatal_warnings, false) &&
      !args.hasArg(OPT_no_warnings);
  errorHandler().suppressWarnings = args.hasArg(OPT_no_warnings);

  // Handle -help
  if (args.hasArg(OPT_help)) {
    printHelp();
    return;
  }

  // Handle -v or -version.
  //
  // A note about "compatible with GNU linkers" message: this is a hack for
  // scripts generated by GNU Libtool up to 2021-10 to recognize LLD as
  // a GNU compatible linker. See
  // <https://lists.gnu.org/archive/html/libtool/2017-01/msg00007.html>.
  //
  // This is somewhat ugly hack, but in reality, we had no choice other
  // than doing this. Considering the very long release cycle of Libtool,
  // it is not easy to improve it to recognize LLD as a GNU compatible
  // linker in a timely manner. Even if we can make it, there are still a
  // lot of "configure" scripts out there that are generated by old version
  // of Libtool. We cannot convince every software developer to migrate to
  // the latest version and re-generate scripts. So we have this hack.
  if (args.hasArg(OPT_v) || args.hasArg(OPT_version))
    message(getLLDVersion() + " (compatible with GNU linkers)");

  if (const char *path = getReproduceOption(args)) {
    // Note that --reproduce is a debug option so you can ignore it
    // if you are trying to understand the whole picture of the code.
    Expected<std::unique_ptr<TarWriter>> errOrWriter =
        TarWriter::create(path, path::stem(path));
    if (errOrWriter) {
      tar = std::move(*errOrWriter);
      tar->append("response.txt", createResponseFile(args));
      tar->append("version.txt", getLLDVersion() + "\n");
      StringRef ltoSampleProfile = args.getLastArgValue(OPT_lto_sample_profile);
      if (!ltoSampleProfile.empty())
        readFile(ltoSampleProfile);
    } else {
      error("--reproduce: " + toString(errOrWriter.takeError()));
    }
  }

  readConfigs(args);
  checkZOptions(args);

  // The behavior of -v or --version is a bit strange, but this is
  // needed for compatibility with GNU linkers.
  if (args.hasArg(OPT_v) && !args.hasArg(OPT_INPUT))
    return;
  if (args.hasArg(OPT_version))
    return;

  // Initialize time trace profiler.
  if (config->timeTraceEnabled)
    timeTraceProfilerInitialize(config->timeTraceGranularity, config->progName);

  {
    llvm::TimeTraceScope timeScope("ExecuteLinker");

    initLLVM();
    createFiles(args);
    if (errorCount())
      return;

    inferMachineType();
    setConfigs(args);
    checkOptions();
    if (errorCount())
      return;

    invokeELFT(link, args);
  }

  if (config->timeTraceEnabled) {
    checkError(timeTraceProfilerWrite(
        args.getLastArgValue(OPT_time_trace_eq).str(), config->outputFile));
    timeTraceProfilerCleanup();
  }
}

static std::string getRpath(opt::InputArgList &args) {
  SmallVector<StringRef, 0> v = args::getStrings(args, OPT_rpath);
  return llvm::join(v.begin(), v.end(), ":");
}

// Determines what we should do if there are remaining unresolved
// symbols after the name resolution.
static void setUnresolvedSymbolPolicy(opt::InputArgList &args) {
  UnresolvedPolicy errorOrWarn = args.hasFlag(OPT_error_unresolved_symbols,
                                              OPT_warn_unresolved_symbols, true)
                                     ? UnresolvedPolicy::ReportError
                                     : UnresolvedPolicy::Warn;
  // -shared implies --unresolved-symbols=ignore-all because missing
  // symbols are likely to be resolved at runtime.
  bool diagRegular = !config->shared, diagShlib = !config->shared;

  for (const opt::Arg *arg : args) {
    switch (arg->getOption().getID()) {
    case OPT_unresolved_symbols: {
      StringRef s = arg->getValue();
      if (s == "ignore-all") {
        diagRegular = false;
        diagShlib = false;
      } else if (s == "ignore-in-object-files") {
        diagRegular = false;
        diagShlib = true;
      } else if (s == "ignore-in-shared-libs") {
        diagRegular = true;
        diagShlib = false;
      } else if (s == "report-all") {
        diagRegular = true;
        diagShlib = true;
      } else {
        error("unknown --unresolved-symbols value: " + s);
      }
      break;
    }
    case OPT_no_undefined:
      diagRegular = true;
      break;
    case OPT_z:
      if (StringRef(arg->getValue()) == "defs")
        diagRegular = true;
      else if (StringRef(arg->getValue()) == "undefs")
        diagRegular = false;
      else
        break;
      arg->claim();
      break;
    case OPT_allow_shlib_undefined:
      diagShlib = false;
      break;
    case OPT_no_allow_shlib_undefined:
      diagShlib = true;
      break;
    }
  }

  config->unresolvedSymbols =
      diagRegular ? errorOrWarn : UnresolvedPolicy::Ignore;
  config->unresolvedSymbolsInShlib =
      diagShlib ? errorOrWarn : UnresolvedPolicy::Ignore;
}

static Target2Policy getTarget2(opt::InputArgList &args) {
  StringRef s = args.getLastArgValue(OPT_target2, "got-rel");
  if (s == "rel")
    return Target2Policy::Rel;
  if (s == "abs")
    return Target2Policy::Abs;
  if (s == "got-rel")
    return Target2Policy::GotRel;
  error("unknown --target2 option: " + s);
  return Target2Policy::GotRel;
}

static bool isOutputFormatBinary(opt::InputArgList &args) {
  StringRef s = args.getLastArgValue(OPT_oformat, "elf");
  if (s == "binary")
    return true;
  if (!s.starts_with("elf"))
    error("unknown --oformat value: " + s);
  return false;
}

static DiscardPolicy getDiscard(opt::InputArgList &args) {
  auto *arg =
      args.getLastArg(OPT_discard_all, OPT_discard_locals, OPT_discard_none);
  if (!arg)
    return DiscardPolicy::Default;
  if (arg->getOption().getID() == OPT_discard_all)
    return DiscardPolicy::All;
  if (arg->getOption().getID() == OPT_discard_locals)
    return DiscardPolicy::Locals;
  return DiscardPolicy::None;
}

static StringRef getDynamicLinker(opt::InputArgList &args) {
  auto *arg = args.getLastArg(OPT_dynamic_linker, OPT_no_dynamic_linker);
  if (!arg)
    return "";
  if (arg->getOption().getID() == OPT_no_dynamic_linker) {
    // --no-dynamic-linker suppresses undefined weak symbols in .dynsym
    config->noDynamicLinker = true;
    return "";
  }
  return arg->getValue();
}

static int getMemtagMode(opt::InputArgList &args) {
  StringRef memtagModeArg = args.getLastArgValue(OPT_android_memtag_mode);
  if (memtagModeArg.empty()) {
    if (config->androidMemtagStack)
      warn("--android-memtag-mode is unspecified, leaving "
           "--android-memtag-stack a no-op");
    else if (config->androidMemtagHeap)
      warn("--android-memtag-mode is unspecified, leaving "
           "--android-memtag-heap a no-op");
    return ELF::NT_MEMTAG_LEVEL_NONE;
  }

  if (memtagModeArg == "sync")
    return ELF::NT_MEMTAG_LEVEL_SYNC;
  if (memtagModeArg == "async")
    return ELF::NT_MEMTAG_LEVEL_ASYNC;
  if (memtagModeArg == "none")
    return ELF::NT_MEMTAG_LEVEL_NONE;

  error("unknown --android-memtag-mode value: \"" + memtagModeArg +
        "\", should be one of {async, sync, none}");
  return ELF::NT_MEMTAG_LEVEL_NONE;
}

static ICFLevel getICF(opt::InputArgList &args) {
  auto *arg = args.getLastArg(OPT_icf_none, OPT_icf_safe, OPT_icf_all);
  if (!arg || arg->getOption().getID() == OPT_icf_none)
    return ICFLevel::None;
  if (arg->getOption().getID() == OPT_icf_safe)
    return ICFLevel::Safe;
  return ICFLevel::All;
}

static StripPolicy getStrip(opt::InputArgList &args) {
  if (args.hasArg(OPT_relocatable))
    return StripPolicy::None;

  auto *arg = args.getLastArg(OPT_strip_all, OPT_strip_debug);
  if (!arg)
    return StripPolicy::None;
  if (arg->getOption().getID() == OPT_strip_all)
    return StripPolicy::All;
  return StripPolicy::Debug;
}

static uint64_t parseSectionAddress(StringRef s, opt::InputArgList &args,
                                    const opt::Arg &arg) {
  uint64_t va = 0;
  if (s.starts_with("0x"))
    s = s.drop_front(2);
  if (!to_integer(s, va, 16))
    error("invalid argument: " + arg.getAsString(args));
  return va;
}

static StringMap<uint64_t> getSectionStartMap(opt::InputArgList &args) {
  StringMap<uint64_t> ret;
  for (auto *arg : args.filtered(OPT_section_start)) {
    StringRef name;
    StringRef addr;
    std::tie(name, addr) = StringRef(arg->getValue()).split('=');
    ret[name] = parseSectionAddress(addr, args, *arg);
  }

  if (auto *arg = args.getLastArg(OPT_Ttext))
    ret[".text"] = parseSectionAddress(arg->getValue(), args, *arg);
  if (auto *arg = args.getLastArg(OPT_Tdata))
    ret[".data"] = parseSectionAddress(arg->getValue(), args, *arg);
  if (auto *arg = args.getLastArg(OPT_Tbss))
    ret[".bss"] = parseSectionAddress(arg->getValue(), args, *arg);
  return ret;
}

static SortSectionPolicy getSortSection(opt::InputArgList &args) {
  StringRef s = args.getLastArgValue(OPT_sort_section);
  if (s == "alignment")
    return SortSectionPolicy::Alignment;
  if (s == "name")
    return SortSectionPolicy::Name;
  if (!s.empty())
    error("unknown --sort-section rule: " + s);
  return SortSectionPolicy::Default;
}

static OrphanHandlingPolicy getOrphanHandling(opt::InputArgList &args) {
  StringRef s = args.getLastArgValue(OPT_orphan_handling, "place");
  if (s == "warn")
    return OrphanHandlingPolicy::Warn;
  if (s == "error")
    return OrphanHandlingPolicy::Error;
  if (s != "place")
    error("unknown --orphan-handling mode: " + s);
  return OrphanHandlingPolicy::Place;
}

// Parse --build-id or --build-id=<style>. We handle "tree" as a
// synonym for "sha1" because all our hash functions including
// --build-id=sha1 are actually tree hashes for performance reasons.
static std::pair<BuildIdKind, SmallVector<uint8_t, 0>>
getBuildId(opt::InputArgList &args) {
  auto *arg = args.getLastArg(OPT_build_id);
  if (!arg)
    return {BuildIdKind::None, {}};

  StringRef s = arg->getValue();
  if (s == "fast")
    return {BuildIdKind::Fast, {}};
  if (s == "md5")
    return {BuildIdKind::Md5, {}};
  if (s == "sha1" || s == "tree")
    return {BuildIdKind::Sha1, {}};
  if (s == "uuid")
    return {BuildIdKind::Uuid, {}};
  if (s.starts_with("0x"))
    return {BuildIdKind::Hexstring, parseHex(s.substr(2))};

  if (s != "none")
    error("unknown --build-id style: " + s);
  return {BuildIdKind::None, {}};
}

static std::pair<bool, bool> getPackDynRelocs(opt::InputArgList &args) {
  StringRef s = args.getLastArgValue(OPT_pack_dyn_relocs, "none");
  if (s == "android")
    return {true, false};
  if (s == "relr")
    return {false, true};
  if (s == "android+relr")
    return {true, true};

  if (s != "none")
    error("unknown --pack-dyn-relocs format: " + s);
  return {false, false};
}

static void readCallGraph(MemoryBufferRef mb) {
  // Build a map from symbol name to section
  DenseMap<StringRef, Symbol *> map;
  for (ELFFileBase *file : ctx.objectFiles)
    for (Symbol *sym : file->getSymbols())
      map[sym->getName()] = sym;

  auto findSection = [&](StringRef name) -> InputSectionBase * {
    Symbol *sym = map.lookup(name);
    if (!sym) {
      if (config->warnSymbolOrdering)
        warn(mb.getBufferIdentifier() + ": no such symbol: " + name);
      return nullptr;
    }
    maybeWarnUnorderableSymbol(sym);

    if (Defined *dr = dyn_cast_or_null<Defined>(sym))
      return dyn_cast_or_null<InputSectionBase>(dr->section);
    return nullptr;
  };

  for (StringRef line : args::getLines(mb)) {
    SmallVector<StringRef, 3> fields;
    line.split(fields, ' ');
    uint64_t count;

    if (fields.size() != 3 || !to_integer(fields[2], count)) {
      error(mb.getBufferIdentifier() + ": parse error");
      return;
    }

    if (InputSectionBase *from = findSection(fields[0]))
      if (InputSectionBase *to = findSection(fields[1]))
        config->callGraphProfile[std::make_pair(from, to)] += count;
  }
}

// If SHT_LLVM_CALL_GRAPH_PROFILE and its relocation section exist, returns
// true and populates cgProfile and symbolIndices.
template <class ELFT>
static bool
processCallGraphRelocations(SmallVector<uint32_t, 32> &symbolIndices,
                            ArrayRef<typename ELFT::CGProfile> &cgProfile,
                            ObjFile<ELFT> *inputObj) {
  if (inputObj->cgProfileSectionIndex == SHN_UNDEF)
    return false;

  ArrayRef<Elf_Shdr_Impl<ELFT>> objSections =
      inputObj->template getELFShdrs<ELFT>();
  symbolIndices.clear();
  const ELFFile<ELFT> &obj = inputObj->getObj();
  cgProfile =
      check(obj.template getSectionContentsAsArray<typename ELFT::CGProfile>(
          objSections[inputObj->cgProfileSectionIndex]));

  for (size_t i = 0, e = objSections.size(); i < e; ++i) {
    const Elf_Shdr_Impl<ELFT> &sec = objSections[i];
    if (sec.sh_info == inputObj->cgProfileSectionIndex) {
      if (sec.sh_type == SHT_CREL) {
        auto crels =
            CHECK(obj.crels(sec), "could not retrieve cg profile rela section");
        for (const auto &rel : crels.first)
          symbolIndices.push_back(rel.getSymbol(false));
        for (const auto &rel : crels.second)
          symbolIndices.push_back(rel.getSymbol(false));
        break;
      }
      if (sec.sh_type == SHT_RELA) {
        ArrayRef<typename ELFT::Rela> relas =
            CHECK(obj.relas(sec), "could not retrieve cg profile rela section");
        for (const typename ELFT::Rela &rel : relas)
          symbolIndices.push_back(rel.getSymbol(config->isMips64EL));
        break;
      }
      if (sec.sh_type == SHT_REL) {
        ArrayRef<typename ELFT::Rel> rels =
            CHECK(obj.rels(sec), "could not retrieve cg profile rel section");
        for (const typename ELFT::Rel &rel : rels)
          symbolIndices.push_back(rel.getSymbol(config->isMips64EL));
        break;
      }
    }
  }
  if (symbolIndices.empty())
    warn("SHT_LLVM_CALL_GRAPH_PROFILE exists, but relocation section doesn't");
  return !symbolIndices.empty();
}

template <class ELFT> static void readCallGraphsFromObjectFiles() {
  SmallVector<uint32_t, 32> symbolIndices;
  ArrayRef<typename ELFT::CGProfile> cgProfile;
  for (auto file : ctx.objectFiles) {
    auto *obj = cast<ObjFile<ELFT>>(file);
    if (!processCallGraphRelocations(symbolIndices, cgProfile, obj))
      continue;

    if (symbolIndices.size() != cgProfile.size() * 2)
      fatal("number of relocations doesn't match Weights");

    for (uint32_t i = 0, size = cgProfile.size(); i < size; ++i) {
      const Elf_CGProfile_Impl<ELFT> &cgpe = cgProfile[i];
      uint32_t fromIndex = symbolIndices[i * 2];
      uint32_t toIndex = symbolIndices[i * 2 + 1];
      auto *fromSym = dyn_cast<Defined>(&obj->getSymbol(fromIndex));
      auto *toSym = dyn_cast<Defined>(&obj->getSymbol(toIndex));
      if (!fromSym || !toSym)
        continue;

      auto *from = dyn_cast_or_null<InputSectionBase>(fromSym->section);
      auto *to = dyn_cast_or_null<InputSectionBase>(toSym->section);
      if (from && to)
        config->callGraphProfile[{from, to}] += cgpe.cgp_weight;
    }
  }
}

template <class ELFT>
static void ltoValidateAllVtablesHaveTypeInfos(opt::InputArgList &args) {
  DenseSet<StringRef> typeInfoSymbols;
  SmallSetVector<StringRef, 0> vtableSymbols;
  auto processVtableAndTypeInfoSymbols = [&](StringRef name) {
    if (name.consume_front("_ZTI"))
      typeInfoSymbols.insert(name);
    else if (name.consume_front("_ZTV"))
      vtableSymbols.insert(name);
  };

  // Examine all native symbol tables.
  for (ELFFileBase *f : ctx.objectFiles) {
    using Elf_Sym = typename ELFT::Sym;
    for (const Elf_Sym &s : f->template getGlobalELFSyms<ELFT>()) {
      if (s.st_shndx != SHN_UNDEF) {
        StringRef name = check(s.getName(f->getStringTable()));
        processVtableAndTypeInfoSymbols(name);
      }
    }
  }

  for (SharedFile *f : ctx.sharedFiles) {
    using Elf_Sym = typename ELFT::Sym;
    for (const Elf_Sym &s : f->template getELFSyms<ELFT>()) {
      if (s.st_shndx != SHN_UNDEF) {
        StringRef name = check(s.getName(f->getStringTable()));
        processVtableAndTypeInfoSymbols(name);
      }
    }
  }

  SmallSetVector<StringRef, 0> vtableSymbolsWithNoRTTI;
  for (StringRef s : vtableSymbols)
    if (!typeInfoSymbols.count(s))
      vtableSymbolsWithNoRTTI.insert(s);

  // Remove known safe symbols.
  for (auto *arg : args.filtered(OPT_lto_known_safe_vtables)) {
    StringRef knownSafeName = arg->getValue();
    if (!knownSafeName.consume_front("_ZTV"))
      error("--lto-known-safe-vtables=: expected symbol to start with _ZTV, "
            "but got " +
            knownSafeName);
    Expected<GlobPattern> pat = GlobPattern::create(knownSafeName);
    if (!pat)
      error("--lto-known-safe-vtables=: " + toString(pat.takeError()));
    vtableSymbolsWithNoRTTI.remove_if(
        [&](StringRef s) { return pat->match(s); });
  }

  ctx.ltoAllVtablesHaveTypeInfos = vtableSymbolsWithNoRTTI.empty();
  // Check for unmatched RTTI symbols
  for (StringRef s : vtableSymbolsWithNoRTTI) {
    message(
        "--lto-validate-all-vtables-have-type-infos: RTTI missing for vtable "
        "_ZTV" +
        s + ", --lto-whole-program-visibility disabled");
  }
}

static CGProfileSortKind getCGProfileSortKind(opt::InputArgList &args) {
  StringRef s = args.getLastArgValue(OPT_call_graph_profile_sort, "cdsort");
  if (s == "hfsort")
    return CGProfileSortKind::Hfsort;
  if (s == "cdsort")
    return CGProfileSortKind::Cdsort;
  if (s != "none")
    error("unknown --call-graph-profile-sort= value: " + s);
  return CGProfileSortKind::None;
}

static DebugCompressionType getCompressionType(StringRef s, StringRef option) {
  DebugCompressionType type = StringSwitch<DebugCompressionType>(s)
                                  .Case("zlib", DebugCompressionType::Zlib)
                                  .Case("zstd", DebugCompressionType::Zstd)
                                  .Default(DebugCompressionType::None);
  if (type == DebugCompressionType::None) {
    if (s != "none")
      error("unknown " + option + " value: " + s);
  } else if (const char *reason = compression::getReasonIfUnsupported(
                 compression::formatFor(type))) {
    error(option + ": " + reason);
  }
  return type;
}

static StringRef getAliasSpelling(opt::Arg *arg) {
  if (const opt::Arg *alias = arg->getAlias())
    return alias->getSpelling();
  return arg->getSpelling();
}

static std::pair<StringRef, StringRef> getOldNewOptions(opt::InputArgList &args,
                                                        unsigned id) {
  auto *arg = args.getLastArg(id);
  if (!arg)
    return {"", ""};

  StringRef s = arg->getValue();
  std::pair<StringRef, StringRef> ret = s.split(';');
  if (ret.second.empty())
    error(getAliasSpelling(arg) + " expects 'old;new' format, but got " + s);
  return ret;
}

// Parse options of the form "old;new[;extra]".
static std::tuple<StringRef, StringRef, StringRef>
getOldNewOptionsExtra(opt::InputArgList &args, unsigned id) {
  auto [oldDir, second] = getOldNewOptions(args, id);
  auto [newDir, extraDir] = second.split(';');
  return {oldDir, newDir, extraDir};
}

// Parse the symbol ordering file and warn for any duplicate entries.
static SmallVector<StringRef, 0> getSymbolOrderingFile(MemoryBufferRef mb) {
  SetVector<StringRef, SmallVector<StringRef, 0>> names;
  for (StringRef s : args::getLines(mb))
    if (!names.insert(s) && config->warnSymbolOrdering)
      warn(mb.getBufferIdentifier() + ": duplicate ordered symbol: " + s);

  return names.takeVector();
}

static bool getIsRela(opt::InputArgList &args) {
  // The psABI specifies the default relocation entry format.
  bool rela = is_contained({EM_AARCH64, EM_AMDGPU, EM_HEXAGON, EM_LOONGARCH,
                            EM_PPC, EM_PPC64, EM_RISCV, EM_S390, EM_X86_64},
                           config->emachine);
  // If -z rel or -z rela is specified, use the last option.
  for (auto *arg : args.filtered(OPT_z)) {
    StringRef s(arg->getValue());
    if (s == "rel")
      rela = false;
    else if (s == "rela")
      rela = true;
    else
      continue;
    arg->claim();
  }
  return rela;
}

static void parseClangOption(StringRef opt, const Twine &msg) {
  std::string err;
  raw_string_ostream os(err);

  const char *argv[] = {config->progName.data(), opt.data()};
  if (cl::ParseCommandLineOptions(2, argv, "", &os))
    return;
  os.flush();
  error(msg + ": " + StringRef(err).trim());
}

// Checks the parameter of the bti-report and cet-report options.
static bool isValidReportString(StringRef arg) {
  return arg == "none" || arg == "warning" || arg == "error";
}

// Process a remap pattern 'from-glob=to-file'.
static bool remapInputs(StringRef line, const Twine &location) {
  SmallVector<StringRef, 0> fields;
  line.split(fields, '=');
  if (fields.size() != 2 || fields[1].empty()) {
    error(location + ": parse error, not 'from-glob=to-file'");
    return true;
  }
  if (!hasWildcard(fields[0]))
    config->remapInputs[fields[0]] = fields[1];
  else if (Expected<GlobPattern> pat = GlobPattern::create(fields[0]))
    config->remapInputsWildcards.emplace_back(std::move(*pat), fields[1]);
  else {
    error(location + ": " + toString(pat.takeError()) + ": " + fields[0]);
    return true;
  }
  return false;
}

// Initializes Config members by the command line options.
static void readConfigs(opt::InputArgList &args) {
  errorHandler().verbose = args.hasArg(OPT_verbose);
  errorHandler().vsDiagnostics =
      args.hasArg(OPT_visual_studio_diagnostics_format, false);

  config->allowMultipleDefinition =
      hasZOption(args, "muldefs") ||
      args.hasFlag(OPT_allow_multiple_definition,
                   OPT_no_allow_multiple_definition, false);
  config->androidMemtagHeap =
      args.hasFlag(OPT_android_memtag_heap, OPT_no_android_memtag_heap, false);
  config->androidMemtagStack = args.hasFlag(OPT_android_memtag_stack,
                                            OPT_no_android_memtag_stack, false);
  config->fatLTOObjects =
      args.hasFlag(OPT_fat_lto_objects, OPT_no_fat_lto_objects, false);
  config->androidMemtagMode = getMemtagMode(args);
  config->auxiliaryList = args::getStrings(args, OPT_auxiliary);
  config->armBe8 = args.hasArg(OPT_be8);
  if (opt::Arg *arg = args.getLastArg(
          OPT_Bno_symbolic, OPT_Bsymbolic_non_weak_functions,
          OPT_Bsymbolic_functions, OPT_Bsymbolic_non_weak, OPT_Bsymbolic)) {
    if (arg->getOption().matches(OPT_Bsymbolic_non_weak_functions))
      config->bsymbolic = BsymbolicKind::NonWeakFunctions;
    else if (arg->getOption().matches(OPT_Bsymbolic_functions))
      config->bsymbolic = BsymbolicKind::Functions;
    else if (arg->getOption().matches(OPT_Bsymbolic_non_weak))
      config->bsymbolic = BsymbolicKind::NonWeak;
    else if (arg->getOption().matches(OPT_Bsymbolic))
      config->bsymbolic = BsymbolicKind::All;
  }
  config->callGraphProfileSort = getCGProfileSortKind(args);
  config->checkSections =
      args.hasFlag(OPT_check_sections, OPT_no_check_sections, true);
  config->chroot = args.getLastArgValue(OPT_chroot);
  if (auto *arg = args.getLastArg(OPT_compress_debug_sections)) {
    config->compressDebugSections =
        getCompressionType(arg->getValue(), "--compress-debug-sections");
  }
  config->cref = args.hasArg(OPT_cref);
  config->optimizeBBJumps =
      args.hasFlag(OPT_optimize_bb_jumps, OPT_no_optimize_bb_jumps, false);
  config->debugNames = args.hasFlag(OPT_debug_names, OPT_no_debug_names, false);
  config->demangle = args.hasFlag(OPT_demangle, OPT_no_demangle, true);
  config->dependencyFile = args.getLastArgValue(OPT_dependency_file);
  config->dependentLibraries = args.hasFlag(OPT_dependent_libraries, OPT_no_dependent_libraries, true);
  config->disableVerify = args.hasArg(OPT_disable_verify);
  config->discard = getDiscard(args);
  config->dwoDir = args.getLastArgValue(OPT_plugin_opt_dwo_dir_eq);
  config->dynamicLinker = getDynamicLinker(args);
  config->ehFrameHdr =
      args.hasFlag(OPT_eh_frame_hdr, OPT_no_eh_frame_hdr, false);
  config->emitLLVM = args.hasArg(OPT_lto_emit_llvm);
  config->emitRelocs = args.hasArg(OPT_emit_relocs);
  config->enableNewDtags =
      args.hasFlag(OPT_enable_new_dtags, OPT_disable_new_dtags, true);
  config->enableNonContiguousRegions =
      args.hasArg(OPT_enable_non_contiguous_regions);
  config->entry = args.getLastArgValue(OPT_entry);

  errorHandler().errorHandlingScript =
      args.getLastArgValue(OPT_error_handling_script);

  config->exportDynamic =
      args.hasFlag(OPT_export_dynamic, OPT_no_export_dynamic, false) ||
      args.hasArg(OPT_shared);
  config->filterList = args::getStrings(args, OPT_filter);
  config->fini = args.getLastArgValue(OPT_fini, "_fini");
  config->fixCortexA53Errata843419 = args.hasArg(OPT_fix_cortex_a53_843419) &&
                                     !args.hasArg(OPT_relocatable);
  config->cmseImplib = args.hasArg(OPT_cmse_implib);
  config->cmseInputLib = args.getLastArgValue(OPT_in_implib);
  config->cmseOutputLib = args.getLastArgValue(OPT_out_implib);
  config->fixCortexA8 =
      args.hasArg(OPT_fix_cortex_a8) && !args.hasArg(OPT_relocatable);
  config->fortranCommon =
      args.hasFlag(OPT_fortran_common, OPT_no_fortran_common, false);
  config->gcSections = args.hasFlag(OPT_gc_sections, OPT_no_gc_sections, false);
  config->gnuUnique = args.hasFlag(OPT_gnu_unique, OPT_no_gnu_unique, true);
  config->gdbIndex = args.hasFlag(OPT_gdb_index, OPT_no_gdb_index, false);
  config->icf = getICF(args);
  config->ignoreDataAddressEquality =
      args.hasArg(OPT_ignore_data_address_equality);
#if defined(__OpenBSD__)
  // Needed to allow preemption of protected symbols (e.g. memcpy) on at least i386.
  config->ignoreFunctionAddressEquality =
      args.hasFlag(OPT_ignore_function_address_equality,
                   OPT_no_ignore_function_address_equality, true);
#else
  config->ignoreFunctionAddressEquality =
      args.hasArg(OPT_ignore_function_address_equality);
#endif
  config->init = args.getLastArgValue(OPT_init, "_init");
  config->ltoAAPipeline = args.getLastArgValue(OPT_lto_aa_pipeline);
  config->ltoCSProfileGenerate = args.hasArg(OPT_lto_cs_profile_generate);
  config->ltoCSProfileFile = args.getLastArgValue(OPT_lto_cs_profile_file);
  config->ltoPGOWarnMismatch = args.hasFlag(OPT_lto_pgo_warn_mismatch,
                                            OPT_no_lto_pgo_warn_mismatch, true);
  config->ltoDebugPassManager = args.hasArg(OPT_lto_debug_pass_manager);
  config->ltoEmitAsm = args.hasArg(OPT_lto_emit_asm);
  config->ltoNewPmPasses = args.getLastArgValue(OPT_lto_newpm_passes);
  config->ltoWholeProgramVisibility =
      args.hasFlag(OPT_lto_whole_program_visibility,
                   OPT_no_lto_whole_program_visibility, false);
  config->ltoValidateAllVtablesHaveTypeInfos =
      args.hasFlag(OPT_lto_validate_all_vtables_have_type_infos,
                   OPT_no_lto_validate_all_vtables_have_type_infos, false);
  config->ltoo = args::getInteger(args, OPT_lto_O, 2);
  if (config->ltoo > 3)
    error("invalid optimization level for LTO: " + Twine(config->ltoo));
  unsigned ltoCgo =
      args::getInteger(args, OPT_lto_CGO, args::getCGOptLevel(config->ltoo));
  if (auto level = CodeGenOpt::getLevel(ltoCgo))
    config->ltoCgo = *level;
  else
    error("invalid codegen optimization level for LTO: " + Twine(ltoCgo));
  config->ltoObjPath = args.getLastArgValue(OPT_lto_obj_path_eq);
  config->ltoPartitions = args::getInteger(args, OPT_lto_partitions, 1);
  config->ltoSampleProfile = args.getLastArgValue(OPT_lto_sample_profile);
  config->ltoBBAddrMap =
      args.hasFlag(OPT_lto_basic_block_address_map,
                   OPT_no_lto_basic_block_address_map, false);
  config->ltoBasicBlockSections =
      args.getLastArgValue(OPT_lto_basic_block_sections);
  config->ltoUniqueBasicBlockSectionNames =
      args.hasFlag(OPT_lto_unique_basic_block_section_names,
                   OPT_no_lto_unique_basic_block_section_names, false);
  config->mapFile = args.getLastArgValue(OPT_Map);
  config->mipsGotSize = args::getInteger(args, OPT_mips_got_size, 0xfff0);
  config->mergeArmExidx =
      args.hasFlag(OPT_merge_exidx_entries, OPT_no_merge_exidx_entries, true);
  config->mmapOutputFile =
      args.hasFlag(OPT_mmap_output_file, OPT_no_mmap_output_file, true);
  config->nmagic = args.hasFlag(OPT_nmagic, OPT_no_nmagic, false);
  config->noinhibitExec = args.hasArg(OPT_noinhibit_exec);
  config->nostdlib = args.hasArg(OPT_nostdlib);
  config->oFormatBinary = isOutputFormatBinary(args);
  config->omagic = args.hasFlag(OPT_omagic, OPT_no_omagic, false);
  config->optRemarksFilename = args.getLastArgValue(OPT_opt_remarks_filename);
  config->optStatsFilename = args.getLastArgValue(OPT_plugin_opt_stats_file);

  // Parse remarks hotness threshold. Valid value is either integer or 'auto'.
  if (auto *arg = args.getLastArg(OPT_opt_remarks_hotness_threshold)) {
    auto resultOrErr = remarks::parseHotnessThresholdOption(arg->getValue());
    if (!resultOrErr)
      error(arg->getSpelling() + ": invalid argument '" + arg->getValue() +
            "', only integer or 'auto' is supported");
    else
      config->optRemarksHotnessThreshold = *resultOrErr;
  }

  config->optRemarksPasses = args.getLastArgValue(OPT_opt_remarks_passes);
  config->optRemarksWithHotness = args.hasArg(OPT_opt_remarks_with_hotness);
  config->optRemarksFormat = args.getLastArgValue(OPT_opt_remarks_format);
  config->optimize = args::getInteger(args, OPT_O, 1);
  config->orphanHandling = getOrphanHandling(args);
  config->outputFile = args.getLastArgValue(OPT_o);
  config->packageMetadata = args.getLastArgValue(OPT_package_metadata);
#ifdef __OpenBSD__
  config->pie = args.hasFlag(OPT_pie, OPT_no_pie,
      !args.hasArg(OPT_shared) && !args.hasArg(OPT_relocatable));
#else
  config->pie = args.hasFlag(OPT_pie, OPT_no_pie, false);
#endif
  config->printIcfSections =
      args.hasFlag(OPT_print_icf_sections, OPT_no_print_icf_sections, false);
  config->printGcSections =
      args.hasFlag(OPT_print_gc_sections, OPT_no_print_gc_sections, false);
  config->printMemoryUsage = args.hasArg(OPT_print_memory_usage);
  config->printArchiveStats = args.getLastArgValue(OPT_print_archive_stats);
  config->printSymbolOrder =
      args.getLastArgValue(OPT_print_symbol_order);
  config->rejectMismatch = !args.hasArg(OPT_no_warn_mismatch);
  config->relax = args.hasFlag(OPT_relax, OPT_no_relax, true);
  config->relaxGP = args.hasFlag(OPT_relax_gp, OPT_no_relax_gp, false);
  config->rpath = getRpath(args);
  config->relocatable = args.hasArg(OPT_relocatable);
  config->resolveGroups =
      !args.hasArg(OPT_relocatable) || args.hasArg(OPT_force_group_allocation);

  if (args.hasArg(OPT_save_temps)) {
    // --save-temps implies saving all temps.
    for (const char *s : saveTempsValues)
      config->saveTempsArgs.insert(s);
  } else {
    for (auto *arg : args.filtered(OPT_save_temps_eq)) {
      StringRef s = arg->getValue();
      if (llvm::is_contained(saveTempsValues, s))
        config->saveTempsArgs.insert(s);
      else
        error("unknown --save-temps value: " + s);
    }
  }

  config->searchPaths = args::getStrings(args, OPT_library_path);
  config->sectionStartMap = getSectionStartMap(args);
  config->shared = args.hasArg(OPT_shared);
  config->singleRoRx = !args.hasFlag(OPT_rosegment, OPT_no_rosegment, true);
  config->soName = args.getLastArgValue(OPT_soname);
  config->sortSection = getSortSection(args);
  config->splitStackAdjustSize = args::getInteger(args, OPT_split_stack_adjust_size, 16384);
  config->strip = getStrip(args);
  config->sysroot = args.getLastArgValue(OPT_sysroot);
  config->target1Rel = args.hasFlag(OPT_target1_rel, OPT_target1_abs, false);
  config->target2 = getTarget2(args);
  config->thinLTOCacheDir = args.getLastArgValue(OPT_thinlto_cache_dir);
  config->thinLTOCachePolicy = CHECK(
      parseCachePruningPolicy(args.getLastArgValue(OPT_thinlto_cache_policy)),
      "--thinlto-cache-policy: invalid cache policy");
  config->thinLTOEmitImportsFiles = args.hasArg(OPT_thinlto_emit_imports_files);
  config->thinLTOEmitIndexFiles = args.hasArg(OPT_thinlto_emit_index_files) ||
                                  args.hasArg(OPT_thinlto_index_only) ||
                                  args.hasArg(OPT_thinlto_index_only_eq);
  config->thinLTOIndexOnly = args.hasArg(OPT_thinlto_index_only) ||
                             args.hasArg(OPT_thinlto_index_only_eq);
  config->thinLTOIndexOnlyArg = args.getLastArgValue(OPT_thinlto_index_only_eq);
  config->thinLTOObjectSuffixReplace =
      getOldNewOptions(args, OPT_thinlto_object_suffix_replace_eq);
  std::tie(config->thinLTOPrefixReplaceOld, config->thinLTOPrefixReplaceNew,
           config->thinLTOPrefixReplaceNativeObject) =
      getOldNewOptionsExtra(args, OPT_thinlto_prefix_replace_eq);
  if (config->thinLTOEmitIndexFiles && !config->thinLTOIndexOnly) {
    if (args.hasArg(OPT_thinlto_object_suffix_replace_eq))
      error("--thinlto-object-suffix-replace is not supported with "
            "--thinlto-emit-index-files");
    else if (args.hasArg(OPT_thinlto_prefix_replace_eq))
      error("--thinlto-prefix-replace is not supported with "
            "--thinlto-emit-index-files");
  }
  if (!config->thinLTOPrefixReplaceNativeObject.empty() &&
      config->thinLTOIndexOnlyArg.empty()) {
    error("--thinlto-prefix-replace=old_dir;new_dir;obj_dir must be used with "
          "--thinlto-index-only=");
  }
  config->thinLTOModulesToCompile =
      args::getStrings(args, OPT_thinlto_single_module_eq);
  config->timeTraceEnabled = args.hasArg(OPT_time_trace_eq);
  config->timeTraceGranularity =
      args::getInteger(args, OPT_time_trace_granularity, 500);
  config->trace = args.hasArg(OPT_trace);
  config->undefined = args::getStrings(args, OPT_undefined);
  config->undefinedVersion =
      args.hasFlag(OPT_undefined_version, OPT_no_undefined_version, true);
  config->unique = args.hasArg(OPT_unique);
  config->useAndroidRelrTags = args.hasFlag(
      OPT_use_android_relr_tags, OPT_no_use_android_relr_tags, false);
  config->warnBackrefs =
      args.hasFlag(OPT_warn_backrefs, OPT_no_warn_backrefs, false);
  config->warnCommon = args.hasFlag(OPT_warn_common, OPT_no_warn_common, false);
  config->warnSymbolOrdering =
      args.hasFlag(OPT_warn_symbol_ordering, OPT_no_warn_symbol_ordering, true);
  config->whyExtract = args.getLastArgValue(OPT_why_extract);
  config->zCombreloc = getZFlag(args, "combreloc", "nocombreloc", true);
  config->zCopyreloc = getZFlag(args, "copyreloc", "nocopyreloc", true);
  config->zForceBti = hasZOption(args, "force-bti");
  config->zForceIbt = hasZOption(args, "force-ibt");
  config->zGcs = getZGcs(args);
  config->zGlobal = hasZOption(args, "global");
  config->zGnustack = getZGnuStack(args);
  config->zHazardplt = hasZOption(args, "hazardplt");
  config->zIfuncNoplt = hasZOption(args, "ifunc-noplt");
  config->zInitfirst = hasZOption(args, "initfirst");
  config->zInterpose = hasZOption(args, "interpose");
  config->zKeepTextSectionPrefix = getZFlag(
      args, "keep-text-section-prefix", "nokeep-text-section-prefix", false);
  config->zLrodataAfterBss =
      getZFlag(args, "lrodata-after-bss", "nolrodata-after-bss", false);
  config->zNoBtCfi = hasZOption(args, "nobtcfi");
  config->zNodefaultlib = hasZOption(args, "nodefaultlib");
  config->zNodelete = hasZOption(args, "nodelete");
  config->zNodlopen = hasZOption(args, "nodlopen");
  config->zNow = getZFlag(args, "now", "lazy", false);
  config->zOrigin = hasZOption(args, "origin");
  config->zPacPlt = hasZOption(args, "pac-plt");
  config->zRelro = getZFlag(args, "relro", "norelro", true);
  config->zRetpolineplt = hasZOption(args, "retpolineplt");
  config->zRodynamic = hasZOption(args, "rodynamic");
  config->zSeparate = getZSeparate(args);
  config->zShstk = hasZOption(args, "shstk");
  config->zStackSize = args::getZOptionValue(args, OPT_z, "stack-size", 0);
  config->zStartStopGC =
      getZFlag(args, "start-stop-gc", "nostart-stop-gc", true);
  config->zStartStopVisibility = getZStartStopVisibility(args);
  config->zText = getZFlag(args, "text", "notext", true);
  config->zWxneeded = hasZOption(args, "wxneeded");
  setUnresolvedSymbolPolicy(args);
  config->power10Stubs = args.getLastArgValue(OPT_power10_stubs_eq) != "no";

  if (opt::Arg *arg = args.getLastArg(OPT_eb, OPT_el)) {
    if (arg->getOption().matches(OPT_eb))
      config->optEB = true;
    else
      config->optEL = true;
  }

  for (opt::Arg *arg : args.filtered(OPT_remap_inputs)) {
    StringRef value(arg->getValue());
    remapInputs(value, arg->getSpelling());
  }
  for (opt::Arg *arg : args.filtered(OPT_remap_inputs_file)) {
    StringRef filename(arg->getValue());
    std::optional<MemoryBufferRef> buffer = readFile(filename);
    if (!buffer)
      continue;
    // Parse 'from-glob=to-file' lines, ignoring #-led comments.
    for (auto [lineno, line] : llvm::enumerate(args::getLines(*buffer)))
      if (remapInputs(line, filename + ":" + Twine(lineno + 1)))
        break;
  }

  for (opt::Arg *arg : args.filtered(OPT_shuffle_sections)) {
    constexpr StringRef errPrefix = "--shuffle-sections=: ";
    std::pair<StringRef, StringRef> kv = StringRef(arg->getValue()).split('=');
    if (kv.first.empty() || kv.second.empty()) {
      error(errPrefix + "expected <section_glob>=<seed>, but got '" +
            arg->getValue() + "'");
      continue;
    }
    // Signed so that <section_glob>=-1 is allowed.
    int64_t v;
    if (!to_integer(kv.second, v))
      error(errPrefix + "expected an integer, but got '" + kv.second + "'");
    else if (Expected<GlobPattern> pat = GlobPattern::create(kv.first))
      config->shuffleSections.emplace_back(std::move(*pat), uint32_t(v));
    else
      error(errPrefix + toString(pat.takeError()) + ": " + kv.first);
  }

  auto reports = {std::make_pair("bti-report", &config->zBtiReport),
                  std::make_pair("cet-report", &config->zCetReport),
                  std::make_pair("gcs-report", &config->zGcsReport),
                  std::make_pair("pauth-report", &config->zPauthReport)};
  for (opt::Arg *arg : args.filtered(OPT_z)) {
    std::pair<StringRef, StringRef> option =
        StringRef(arg->getValue()).split('=');
    for (auto reportArg : reports) {
      if (option.first != reportArg.first)
        continue;
      arg->claim();
      if (!isValidReportString(option.second)) {
        error(Twine("-z ") + reportArg.first + "= parameter " + option.second +
              " is not recognized");
        continue;
      }
      *reportArg.second = option.second;
    }
  }

  for (opt::Arg *arg : args.filtered(OPT_compress_sections)) {
    SmallVector<StringRef, 0> fields;
    StringRef(arg->getValue()).split(fields, '=');
    if (fields.size() != 2 || fields[1].empty()) {
      error(arg->getSpelling() +
            ": parse error, not 'section-glob=[none|zlib|zstd]'");
      continue;
    }
    auto [typeStr, levelStr] = fields[1].split(':');
    auto type = getCompressionType(typeStr, arg->getSpelling());
    unsigned level = 0;
    if (fields[1].size() != typeStr.size() &&
        !llvm::to_integer(levelStr, level)) {
      error(arg->getSpelling() +
            ": expected a non-negative integer compression level, but got '" +
            levelStr + "'");
    }
    if (Expected<GlobPattern> pat = GlobPattern::create(fields[0])) {
      config->compressSections.emplace_back(std::move(*pat), type, level);
    } else {
      error(arg->getSpelling() + ": " + toString(pat.takeError()));
      continue;
    }
  }

  for (opt::Arg *arg : args.filtered(OPT_z)) {
    std::pair<StringRef, StringRef> option =
        StringRef(arg->getValue()).split('=');
    if (option.first != "dead-reloc-in-nonalloc")
      continue;
    arg->claim();
    constexpr StringRef errPrefix = "-z dead-reloc-in-nonalloc=: ";
    std::pair<StringRef, StringRef> kv = option.second.split('=');
    if (kv.first.empty() || kv.second.empty()) {
      error(errPrefix + "expected <section_glob>=<value>");
      continue;
    }
    uint64_t v;
    if (!to_integer(kv.second, v))
      error(errPrefix + "expected a non-negative integer, but got '" +
            kv.second + "'");
    else if (Expected<GlobPattern> pat = GlobPattern::create(kv.first))
      config->deadRelocInNonAlloc.emplace_back(std::move(*pat), v);
    else
      error(errPrefix + toString(pat.takeError()) + ": " + kv.first);
  }

  cl::ResetAllOptionOccurrences();

  // Parse LTO options.
  if (auto *arg = args.getLastArg(OPT_plugin_opt_mcpu_eq))
    parseClangOption(saver().save("-mcpu=" + StringRef(arg->getValue())),
                     arg->getSpelling());

  for (opt::Arg *arg : args.filtered(OPT_plugin_opt_eq_minus))
    parseClangOption(std::string("-") + arg->getValue(), arg->getSpelling());

  // GCC collect2 passes -plugin-opt=path/to/lto-wrapper with an absolute or
  // relative path. Just ignore. If not ended with "lto-wrapper" (or
  // "lto-wrapper.exe" for GCC cross-compiled for Windows), consider it an
  // unsupported LLVMgold.so option and error.
  for (opt::Arg *arg : args.filtered(OPT_plugin_opt_eq)) {
    StringRef v(arg->getValue());
    if (!v.ends_with("lto-wrapper") && !v.ends_with("lto-wrapper.exe"))
      error(arg->getSpelling() + ": unknown plugin option '" + arg->getValue() +
            "'");
  }

  config->passPlugins = args::getStrings(args, OPT_load_pass_plugins);

  // Parse -mllvm options.
  for (const auto *arg : args.filtered(OPT_mllvm)) {
    parseClangOption(arg->getValue(), arg->getSpelling());
    config->mllvmOpts.emplace_back(arg->getValue());
  }

  config->ltoKind = LtoKind::Default;
  if (auto *arg = args.getLastArg(OPT_lto)) {
    StringRef s = arg->getValue();
    if (s == "thin")
      config->ltoKind = LtoKind::UnifiedThin;
    else if (s == "full")
      config->ltoKind = LtoKind::UnifiedRegular;
    else if (s == "default")
      config->ltoKind = LtoKind::Default;
    else
      error("unknown LTO mode: " + s);
  }

  // --threads= takes a positive integer and provides the default value for
  // --thinlto-jobs=. If unspecified, cap the number of threads since
  // overhead outweighs optimization for used parallel algorithms for the
  // non-LTO parts.
  if (auto *arg = args.getLastArg(OPT_threads)) {
    StringRef v(arg->getValue());
    unsigned threads = 0;
    if (!llvm::to_integer(v, threads, 0) || threads == 0)
      error(arg->getSpelling() + ": expected a positive integer, but got '" +
            arg->getValue() + "'");
    parallel::strategy = hardware_concurrency(threads);
    config->thinLTOJobs = v;
  } else if (parallel::strategy.compute_thread_count() > 4) {
    log("set maximum concurrency to 4, specify --threads= to change");
    parallel::strategy = hardware_concurrency(4);
  }
  if (auto *arg = args.getLastArg(OPT_thinlto_jobs_eq))
    config->thinLTOJobs = arg->getValue();
  config->threadCount = parallel::strategy.compute_thread_count();

  if (config->ltoPartitions == 0)
    error("--lto-partitions: number of threads must be > 0");
  if (!get_threadpool_strategy(config->thinLTOJobs))
    error("--thinlto-jobs: invalid job count: " + config->thinLTOJobs);

  if (config->splitStackAdjustSize < 0)
    error("--split-stack-adjust-size: size must be >= 0");

  // The text segment is traditionally the first segment, whose address equals
  // the base address. However, lld places the R PT_LOAD first. -Ttext-segment
  // is an old-fashioned option that does not play well with lld's layout.
  // Suggest --image-base as a likely alternative.
  if (args.hasArg(OPT_Ttext_segment))
    error("-Ttext-segment is not supported. Use --image-base if you "
          "intend to set the base address");

  // Parse ELF{32,64}{LE,BE} and CPU type.
  if (auto *arg = args.getLastArg(OPT_m)) {
    StringRef s = arg->getValue();
    std::tie(config->ekind, config->emachine, config->osabi) =
        parseEmulation(s);
    config->mipsN32Abi =
        (s.starts_with("elf32btsmipn32") || s.starts_with("elf32ltsmipn32"));
    config->emulation = s;
  }

  // Parse --hash-style={sysv,gnu,both}.
  if (auto *arg = args.getLastArg(OPT_hash_style)) {
    StringRef s = arg->getValue();
    if (s == "sysv")
      config->sysvHash = true;
    else if (s == "gnu")
      config->gnuHash = true;
    else if (s == "both")
      config->sysvHash = config->gnuHash = true;
    else
      error("unknown --hash-style: " + s);
  }

  if (args.hasArg(OPT_print_map))
    config->mapFile = "-";

  // Page alignment can be disabled by the -n (--nmagic) and -N (--omagic).
  // As PT_GNU_RELRO relies on Paging, do not create it when we have disabled
  // it. Also disable RELRO for -r.
  if (config->nmagic || config->omagic || config->relocatable)
    config->zRelro = false;

  std::tie(config->buildId, config->buildIdVector) = getBuildId(args);

  if (getZFlag(args, "pack-relative-relocs", "nopack-relative-relocs", false)) {
    config->relrGlibc = true;
    config->relrPackDynRelocs = true;
  } else {
    std::tie(config->androidPackDynRelocs, config->relrPackDynRelocs) =
        getPackDynRelocs(args);
  }

  if (auto *arg = args.getLastArg(OPT_symbol_ordering_file)){
    if (args.hasArg(OPT_call_graph_ordering_file))
      error("--symbol-ordering-file and --call-graph-order-file "
            "may not be used together");
    if (std::optional<MemoryBufferRef> buffer = readFile(arg->getValue())) {
      config->symbolOrderingFile = getSymbolOrderingFile(*buffer);
      // Also need to disable CallGraphProfileSort to prevent
      // LLD order symbols with CGProfile
      config->callGraphProfileSort = CGProfileSortKind::None;
    }
  }

  assert(config->versionDefinitions.empty());
  config->versionDefinitions.push_back(
      {"local", (uint16_t)VER_NDX_LOCAL, {}, {}});
  config->versionDefinitions.push_back(
      {"global", (uint16_t)VER_NDX_GLOBAL, {}, {}});

  // If --retain-symbol-file is used, we'll keep only the symbols listed in
  // the file and discard all others.
  if (auto *arg = args.getLastArg(OPT_retain_symbols_file)) {
    config->versionDefinitions[VER_NDX_LOCAL].nonLocalPatterns.push_back(
        {"*", /*isExternCpp=*/false, /*hasWildcard=*/true});
    if (std::optional<MemoryBufferRef> buffer = readFile(arg->getValue()))
      for (StringRef s : args::getLines(*buffer))
        config->versionDefinitions[VER_NDX_GLOBAL].nonLocalPatterns.push_back(
            {s, /*isExternCpp=*/false, /*hasWildcard=*/false});
  }

  for (opt::Arg *arg : args.filtered(OPT_warn_backrefs_exclude)) {
    StringRef pattern(arg->getValue());
    if (Expected<GlobPattern> pat = GlobPattern::create(pattern))
      config->warnBackrefsExclude.push_back(std::move(*pat));
    else
      error(arg->getSpelling() + ": " + toString(pat.takeError()) + ": " +
            pattern);
  }

  // For -no-pie and -pie, --export-dynamic-symbol specifies defined symbols
  // which should be exported. For -shared, references to matched non-local
  // STV_DEFAULT symbols are not bound to definitions within the shared object,
  // even if other options express a symbolic intention: -Bsymbolic,
  // -Bsymbolic-functions (if STT_FUNC), --dynamic-list.
  for (auto *arg : args.filtered(OPT_export_dynamic_symbol))
    config->dynamicList.push_back(
        {arg->getValue(), /*isExternCpp=*/false,
         /*hasWildcard=*/hasWildcard(arg->getValue())});

  // --export-dynamic-symbol-list specifies a list of --export-dynamic-symbol
  // patterns. --dynamic-list is --export-dynamic-symbol-list plus -Bsymbolic
  // like semantics.
  config->symbolic =
      config->bsymbolic == BsymbolicKind::All || args.hasArg(OPT_dynamic_list);
  for (auto *arg :
       args.filtered(OPT_dynamic_list, OPT_export_dynamic_symbol_list))
    if (std::optional<MemoryBufferRef> buffer = readFile(arg->getValue()))
      readDynamicList(*buffer);

  for (auto *arg : args.filtered(OPT_version_script))
    if (std::optional<std::string> path = searchScript(arg->getValue())) {
      if (std::optional<MemoryBufferRef> buffer = readFile(*path))
        readVersionScript(*buffer);
    } else {
      error(Twine("cannot find version script ") + arg->getValue());
    }
}

// Some Config members do not directly correspond to any particular
// command line options, but computed based on other Config values.
// This function initialize such members. See Config.h for the details
// of these values.
static void setConfigs(opt::InputArgList &args) {
  ELFKind k = config->ekind;
  uint16_t m = config->emachine;

  config->copyRelocs = (config->relocatable || config->emitRelocs);
  config->is64 = (k == ELF64LEKind || k == ELF64BEKind);
  config->isLE = (k == ELF32LEKind || k == ELF64LEKind);
  config->endianness = config->isLE ? endianness::little : endianness::big;
  config->isMips64EL = (k == ELF64LEKind && m == EM_MIPS);
  config->isPic = config->pie || config->shared;
  config->picThunk = args.hasArg(OPT_pic_veneer, config->isPic);
  config->wordsize = config->is64 ? 8 : 4;

  // ELF defines two different ways to store relocation addends as shown below:
  //
  //  Rel: Addends are stored to the location where relocations are applied. It
  //  cannot pack the full range of addend values for all relocation types, but
  //  this only affects relocation types that we don't support emitting as
  //  dynamic relocations (see getDynRel).
  //  Rela: Addends are stored as part of relocation entry.
  //
  // In other words, Rela makes it easy to read addends at the price of extra
  // 4 or 8 byte for each relocation entry.
  //
  // We pick the format for dynamic relocations according to the psABI for each
  // processor, but a contrary choice can be made if the dynamic loader
  // supports.
  config->isRela = getIsRela(args);

  // If the output uses REL relocations we must store the dynamic relocation
  // addends to the output sections. We also store addends for RELA relocations
  // if --apply-dynamic-relocs is used.
  // We default to not writing the addends when using RELA relocations since
  // any standard conforming tool can find it in r_addend.
  config->writeAddends = args.hasFlag(OPT_apply_dynamic_relocs,
                                      OPT_no_apply_dynamic_relocs, false) ||
                         !config->isRela;
  // Validation of dynamic relocation addends is on by default for assertions
  // builds and disabled otherwise. This check is enabled when writeAddends is
  // true.
#ifndef NDEBUG
  bool checkDynamicRelocsDefault = true;
#else
  bool checkDynamicRelocsDefault = false;
#endif
  config->checkDynamicRelocs =
      args.hasFlag(OPT_check_dynamic_relocations,
                   OPT_no_check_dynamic_relocations, checkDynamicRelocsDefault);
  config->tocOptimize =
      args.hasFlag(OPT_toc_optimize, OPT_no_toc_optimize, m == EM_PPC64);
  config->pcRelOptimize =
      args.hasFlag(OPT_pcrel_optimize, OPT_no_pcrel_optimize, m == EM_PPC64);

  if (!args.hasArg(OPT_hash_style)) {
    if (config->emachine == EM_MIPS)
      config->sysvHash = true;
    else
      config->sysvHash = config->gnuHash = true;
  }

  // Set default entry point and output file if not specified by command line or
  // linker scripts.
  config->warnMissingEntry =
      (!config->entry.empty() || (!config->shared && !config->relocatable));
  if (config->entry.empty() && !config->relocatable)
    config->entry = config->emachine == EM_MIPS ? "__start" : "_start";
  if (config->outputFile.empty())
    config->outputFile = "a.out";

  // Fail early if the output file or map file is not writable. If a user has a
  // long link, e.g. due to a large LTO link, they do not wish to run it and
  // find that it failed because there was a mistake in their command-line.
  {
    llvm::TimeTraceScope timeScope("Create output files");
    if (auto e = tryCreateFile(config->outputFile))
      error("cannot open output file " + config->outputFile + ": " +
            e.message());
    if (auto e = tryCreateFile(config->mapFile))
      error("cannot open map file " + config->mapFile + ": " + e.message());
    if (auto e = tryCreateFile(config->whyExtract))
      error("cannot open --why-extract= file " + config->whyExtract + ": " +
            e.message());
  }

  config->executeOnly = false;
#ifdef __OpenBSD__
  switch (m) {
  case EM_AARCH64:
  case EM_MIPS:
  case EM_PPC:
  case EM_PPC64:
  case EM_RISCV:
  case EM_SPARCV9:
  case EM_X86_64:
    config->executeOnly = true;
    break;
  }
#endif
  config->executeOnly =
      args.hasFlag(OPT_execute_only, OPT_no_execute_only, config->executeOnly);
}

static bool isFormatBinary(StringRef s) {
  if (s == "binary")
    return true;
  if (s == "elf" || s == "default")
    return false;
  error("unknown --format value: " + s +
        " (supported formats: elf, default, binary)");
  return false;
}

void LinkerDriver::createFiles(opt::InputArgList &args) {
  llvm::TimeTraceScope timeScope("Load input files");
  // For --{push,pop}-state.
  std::vector<std::tuple<bool, bool, bool>> stack;

  // -r implies -Bstatic and has precedence over -Bdynamic.
  config->isStatic = config->relocatable;

  // Iterate over argv to process input files and positional arguments.
  std::optional<MemoryBufferRef> defaultScript;
  InputFile::isInGroup = false;
  bool hasInput = false, hasScript = false;
  for (auto *arg : args) {
    switch (arg->getOption().getID()) {
    case OPT_library:
      addLibrary(arg->getValue());
      hasInput = true;
      break;
    case OPT_INPUT:
      addFile(arg->getValue(), /*withLOption=*/false);
      hasInput = true;
      break;
    case OPT_defsym: {
      StringRef from;
      StringRef to;
      std::tie(from, to) = StringRef(arg->getValue()).split('=');
      if (from.empty() || to.empty())
        error("--defsym: syntax error: " + StringRef(arg->getValue()));
      else
        readDefsym(from, MemoryBufferRef(to, "--defsym"));
      break;
    }
    case OPT_script:
    case OPT_default_script:
      if (std::optional<std::string> path = searchScript(arg->getValue())) {
        if (std::optional<MemoryBufferRef> mb = readFile(*path)) {
          if (arg->getOption().matches(OPT_default_script)) {
            defaultScript = mb;
          } else {
            readLinkerScript(*mb);
            hasScript = true;
          }
        }
        break;
      }
      error(Twine("cannot find linker script ") + arg->getValue());
      break;
    case OPT_as_needed:
      config->asNeeded = true;
      break;
    case OPT_format:
      config->formatBinary = isFormatBinary(arg->getValue());
      break;
    case OPT_no_as_needed:
      config->asNeeded = false;
      break;
    case OPT_Bstatic:
    case OPT_omagic:
    case OPT_nmagic:
      config->isStatic = true;
      break;
    case OPT_Bdynamic:
      if (!config->relocatable)
        config->isStatic = false;
      break;
    case OPT_whole_archive:
      inWholeArchive = true;
      break;
    case OPT_no_whole_archive:
      inWholeArchive = false;
      break;
    case OPT_just_symbols:
      if (std::optional<MemoryBufferRef> mb = readFile(arg->getValue())) {
        files.push_back(createObjFile(*mb));
        files.back()->justSymbols = true;
      }
      break;
    case OPT_in_implib:
      if (armCmseImpLib)
        error("multiple CMSE import libraries not supported");
      else if (std::optional<MemoryBufferRef> mb = readFile(arg->getValue()))
        armCmseImpLib = createObjFile(*mb);
      break;
    case OPT_start_group:
      if (InputFile::isInGroup)
        error("nested --start-group");
      InputFile::isInGroup = true;
      break;
    case OPT_end_group:
      if (!InputFile::isInGroup)
        error("stray --end-group");
      InputFile::isInGroup = false;
      ++InputFile::nextGroupId;
      break;
    case OPT_start_lib:
      if (inLib)
        error("nested --start-lib");
      if (InputFile::isInGroup)
        error("may not nest --start-lib in --start-group");
      inLib = true;
      InputFile::isInGroup = true;
      break;
    case OPT_end_lib:
      if (!inLib)
        error("stray --end-lib");
      inLib = false;
      InputFile::isInGroup = false;
      ++InputFile::nextGroupId;
      break;
    case OPT_push_state:
      stack.emplace_back(config->asNeeded, config->isStatic, inWholeArchive);
      break;
    case OPT_pop_state:
      if (stack.empty()) {
        error("unbalanced --push-state/--pop-state");
        break;
      }
      std::tie(config->asNeeded, config->isStatic, inWholeArchive) = stack.back();
      stack.pop_back();
      break;
    }
  }

  if (defaultScript && !hasScript)
    readLinkerScript(*defaultScript);
  if (files.empty() && !hasInput && errorCount() == 0)
    error("no input files");
}

// If -m <machine_type> was not given, infer it from object files.
void LinkerDriver::inferMachineType() {
  if (config->ekind != ELFNoneKind)
    return;

  bool inferred = false;
  for (InputFile *f : files) {
    if (f->ekind == ELFNoneKind)
      continue;
    if (!inferred) {
      inferred = true;
      config->ekind = f->ekind;
      config->emachine = f->emachine;
      config->mipsN32Abi = config->emachine == EM_MIPS && isMipsN32Abi(f);
    }
    config->osabi = f->osabi;
    if (f->osabi != ELFOSABI_NONE)
      return;
  }
  if (!inferred)
    error("target emulation unknown: -m or at least one .o file required");
}

// Parse -z max-page-size=<value>. The default value is defined by
// each target. Is set to 1 if given nmagic or omagic.
static uint64_t getMaxPageSize(opt::InputArgList &args) {
  uint64_t val = args::getZOptionValue(args, OPT_z, "max-page-size",
                                       target->defaultMaxPageSize);
  if (!isPowerOf2_64(val)) {
    error("max-page-size: value isn't a power of 2");
    return target->defaultMaxPageSize;
  }
  if (config->nmagic || config->omagic) {
    if (val != target->defaultMaxPageSize)
      warn("-z max-page-size set, but paging disabled by omagic or nmagic");
    return 1;
  }
  return val;
}

// Parse -z common-page-size=<value>. The default value is defined by
// each target. Is set to 1 if given nmagic or omagic.
static uint64_t getCommonPageSize(opt::InputArgList &args) {
  uint64_t val = args::getZOptionValue(args, OPT_z, "common-page-size",
                                       target->defaultCommonPageSize);
  if (!isPowerOf2_64(val)) {
    error("common-page-size: value isn't a power of 2");
    return target->defaultCommonPageSize;
  }
  if (config->nmagic || config->omagic) {
    if (val != target->defaultCommonPageSize)
      warn("-z common-page-size set, but paging disabled by omagic or nmagic");
    return 1;
  }
  // commonPageSize can't be larger than maxPageSize.
  if (val > config->maxPageSize)
    val = config->maxPageSize;
  return val;
}

// Parse -z max-page-size=<value>. The default value is defined by
// each target.
static uint64_t getRealMaxPageSize(opt::InputArgList &args) {
  uint64_t val = args::getZOptionValue(args, OPT_z, "max-page-size",
                                       target->defaultMaxPageSize);
  if (!isPowerOf2_64(val))
    error("max-page-size: value isn't a power of 2");
  return val;
}

// Parses --image-base option.
static std::optional<uint64_t> getImageBase(opt::InputArgList &args) {
  // Because we are using "Config->maxPageSize" here, this function has to be
  // called after the variable is initialized.
  auto *arg = args.getLastArg(OPT_image_base);
  if (!arg)
    return std::nullopt;

  StringRef s = arg->getValue();
  uint64_t v;
  if (!to_integer(s, v)) {
    error("--image-base: number expected, but got " + s);
    return 0;
  }
  if ((v % config->maxPageSize) != 0)
    warn("--image-base: address isn't multiple of page size: " + s);
  return v;
}

// Parses `--exclude-libs=lib,lib,...`.
// The library names may be delimited by commas or colons.
static DenseSet<StringRef> getExcludeLibs(opt::InputArgList &args) {
  DenseSet<StringRef> ret;
  for (auto *arg : args.filtered(OPT_exclude_libs)) {
    StringRef s = arg->getValue();
    for (;;) {
      size_t pos = s.find_first_of(",:");
      if (pos == StringRef::npos)
        break;
      ret.insert(s.substr(0, pos));
      s = s.substr(pos + 1);
    }
    ret.insert(s);
  }
  return ret;
}

// Handles the --exclude-libs option. If a static library file is specified
// by the --exclude-libs option, all public symbols from the archive become
// private unless otherwise specified by version scripts or something.
// A special library name "ALL" means all archive files.
//
// This is not a popular option, but some programs such as bionic libc use it.
static void excludeLibs(opt::InputArgList &args) {
  DenseSet<StringRef> libs = getExcludeLibs(args);
  bool all = libs.count("ALL");

  auto visit = [&](InputFile *file) {
    if (file->archiveName.empty() ||
        !(all || libs.count(path::filename(file->archiveName))))
      return;
    ArrayRef<Symbol *> symbols = file->getSymbols();
    if (isa<ELFFileBase>(file))
      symbols = cast<ELFFileBase>(file)->getGlobalSymbols();
    for (Symbol *sym : symbols)
      if (!sym->isUndefined() && sym->file == file)
        sym->versionId = VER_NDX_LOCAL;
  };

  for (ELFFileBase *file : ctx.objectFiles)
    visit(file);

  for (BitcodeFile *file : ctx.bitcodeFiles)
    visit(file);
}

// Force Sym to be entered in the output.
static void handleUndefined(Symbol *sym, const char *option) {
  // Since a symbol may not be used inside the program, LTO may
  // eliminate it. Mark the symbol as "used" to prevent it.
  sym->isUsedInRegularObj = true;

  if (!sym->isLazy())
    return;
  sym->extract();
  if (!config->whyExtract.empty())
    ctx.whyExtractRecords.emplace_back(option, sym->file, *sym);
}

// As an extension to GNU linkers, lld supports a variant of `-u`
// which accepts wildcard patterns. All symbols that match a given
// pattern are handled as if they were given by `-u`.
static void handleUndefinedGlob(StringRef arg) {
  Expected<GlobPattern> pat = GlobPattern::create(arg);
  if (!pat) {
    error("--undefined-glob: " + toString(pat.takeError()) + ": " + arg);
    return;
  }

  // Calling sym->extract() in the loop is not safe because it may add new
  // symbols to the symbol table, invalidating the current iterator.
  SmallVector<Symbol *, 0> syms;
  for (Symbol *sym : symtab.getSymbols())
    if (!sym->isPlaceholder() && pat->match(sym->getName()))
      syms.push_back(sym);

  for (Symbol *sym : syms)
    handleUndefined(sym, "--undefined-glob");
}

static void handleLibcall(StringRef name) {
  Symbol *sym = symtab.find(name);
  if (sym && sym->isLazy() && isa<BitcodeFile>(sym->file)) {
    if (!config->whyExtract.empty())
      ctx.whyExtractRecords.emplace_back("<libcall>", sym->file, *sym);
    sym->extract();
  }
}

static void writeArchiveStats() {
  if (config->printArchiveStats.empty())
    return;

  std::error_code ec;
  raw_fd_ostream os = ctx.openAuxiliaryFile(config->printArchiveStats, ec);
  if (ec) {
    error("--print-archive-stats=: cannot open " + config->printArchiveStats +
          ": " + ec.message());
    return;
  }

  os << "members\textracted\tarchive\n";

  SmallVector<StringRef, 0> archives;
  DenseMap<CachedHashStringRef, unsigned> all, extracted;
  for (ELFFileBase *file : ctx.objectFiles)
    if (file->archiveName.size())
      ++extracted[CachedHashStringRef(file->archiveName)];
  for (BitcodeFile *file : ctx.bitcodeFiles)
    if (file->archiveName.size())
      ++extracted[CachedHashStringRef(file->archiveName)];
  for (std::pair<StringRef, unsigned> f : ctx.driver.archiveFiles) {
    unsigned &v = extracted[CachedHashString(f.first)];
    os << f.second << '\t' << v << '\t' << f.first << '\n';
    // If the archive occurs multiple times, other instances have a count of 0.
    v = 0;
  }
}

static void writeWhyExtract() {
  if (config->whyExtract.empty())
    return;

  std::error_code ec;
  raw_fd_ostream os = ctx.openAuxiliaryFile(config->whyExtract, ec);
  if (ec) {
    error("cannot open --why-extract= file " + config->whyExtract + ": " +
          ec.message());
    return;
  }

  os << "reference\textracted\tsymbol\n";
  for (auto &entry : ctx.whyExtractRecords) {
    os << std::get<0>(entry) << '\t' << toString(std::get<1>(entry)) << '\t'
       << toString(std::get<2>(entry)) << '\n';
  }
}

static void reportBackrefs() {
  for (auto &ref : ctx.backwardReferences) {
    const Symbol &sym = *ref.first;
    std::string to = toString(ref.second.second);
    // Some libraries have known problems and can cause noise. Filter them out
    // with --warn-backrefs-exclude=. The value may look like (for --start-lib)
    // *.o or (archive member) *.a(*.o).
    bool exclude = false;
    for (const llvm::GlobPattern &pat : config->warnBackrefsExclude)
      if (pat.match(to)) {
        exclude = true;
        break;
      }
    if (!exclude)
      warn("backward reference detected: " + sym.getName() + " in " +
           toString(ref.second.first) + " refers to " + to);
  }
}

// Handle --dependency-file=<path>. If that option is given, lld creates a
// file at a given path with the following contents:
//
//   <output-file>: <input-file> ...
//
//   <input-file>:
//
// where <output-file> is a pathname of an output file and <input-file>
// ... is a list of pathnames of all input files. `make` command can read a
// file in the above format and interpret it as a dependency info. We write
// phony targets for every <input-file> to avoid an error when that file is
// removed.
//
// This option is useful if you want to make your final executable to depend
// on all input files including system libraries. Here is why.
//
// When you write a Makefile, you usually write it so that the final
// executable depends on all user-generated object files. Normally, you
// don't make your executable to depend on system libraries (such as libc)
// because you don't know the exact paths of libraries, even though system
// libraries that are linked to your executable statically are technically a
// part of your program. By using --dependency-file option, you can make
// lld to dump dependency info so that you can maintain exact dependencies
// easily.
static void writeDependencyFile() {
  std::error_code ec;
  raw_fd_ostream os = ctx.openAuxiliaryFile(config->dependencyFile, ec);
  if (ec) {
    error("cannot open " + config->dependencyFile + ": " + ec.message());
    return;
  }

  // We use the same escape rules as Clang/GCC which are accepted by Make/Ninja:
  // * A space is escaped by a backslash which itself must be escaped.
  // * A hash sign is escaped by a single backslash.
  // * $ is escapes as $$.
  auto printFilename = [](raw_fd_ostream &os, StringRef filename) {
    llvm::SmallString<256> nativePath;
    llvm::sys::path::native(filename.str(), nativePath);
    llvm::sys::path::remove_dots(nativePath, /*remove_dot_dot=*/true);
    for (unsigned i = 0, e = nativePath.size(); i != e; ++i) {
      if (nativePath[i] == '#') {
        os << '\\';
      } else if (nativePath[i] == ' ') {
        os << '\\';
        unsigned j = i;
        while (j > 0 && nativePath[--j] == '\\')
          os << '\\';
      } else if (nativePath[i] == '$') {
        os << '$';
      }
      os << nativePath[i];
    }
  };

  os << config->outputFile << ":";
  for (StringRef path : config->dependencyFiles) {
    os << " \\\n ";
    printFilename(os, path);
  }
  os << "\n";

  for (StringRef path : config->dependencyFiles) {
    os << "\n";
    printFilename(os, path);
    os << ":\n";
  }
}

// Replaces common symbols with defined symbols reside in .bss sections.
// This function is called after all symbol names are resolved. As a
// result, the passes after the symbol resolution won't see any
// symbols of type CommonSymbol.
static void replaceCommonSymbols() {
  llvm::TimeTraceScope timeScope("Replace common symbols");
  for (ELFFileBase *file : ctx.objectFiles) {
    if (!file->hasCommonSyms)
      continue;
    for (Symbol *sym : file->getGlobalSymbols()) {
      auto *s = dyn_cast<CommonSymbol>(sym);
      if (!s)
        continue;

      auto *bss = make<BssSection>("COMMON", s->size, s->alignment);
      bss->file = s->file;
      ctx.inputSections.push_back(bss);
      Defined(s->file, StringRef(), s->binding, s->stOther, s->type,
              /*value=*/0, s->size, bss)
          .overwrite(*s);
    }
  }
}

// The section referred to by `s` is considered address-significant. Set the
// keepUnique flag on the section if appropriate.
static void markAddrsig(Symbol *s) {
  if (auto *d = dyn_cast_or_null<Defined>(s))
    if (d->section)
      // We don't need to keep text sections unique under --icf=all even if they
      // are address-significant.
      if (config->icf == ICFLevel::Safe || !(d->section->flags & SHF_EXECINSTR))
        d->section->keepUnique = true;
}

// Record sections that define symbols mentioned in --keep-unique <symbol>
// and symbols referred to by address-significance tables. These sections are
// ineligible for ICF.
template <class ELFT>
static void findKeepUniqueSections(opt::InputArgList &args) {
  for (auto *arg : args.filtered(OPT_keep_unique)) {
    StringRef name = arg->getValue();
    auto *d = dyn_cast_or_null<Defined>(symtab.find(name));
    if (!d || !d->section) {
      warn("could not find symbol " + name + " to keep unique");
      continue;
    }
    d->section->keepUnique = true;
  }

  // --icf=all --ignore-data-address-equality means that we can ignore
  // the dynsym and address-significance tables entirely.
  if (config->icf == ICFLevel::All && config->ignoreDataAddressEquality)
    return;

  // Symbols in the dynsym could be address-significant in other executables
  // or DSOs, so we conservatively mark them as address-significant.
  for (Symbol *sym : symtab.getSymbols())
    if (sym->includeInDynsym())
      markAddrsig(sym);

  // Visit the address-significance table in each object file and mark each
  // referenced symbol as address-significant.
  for (InputFile *f : ctx.objectFiles) {
    auto *obj = cast<ObjFile<ELFT>>(f);
    ArrayRef<Symbol *> syms = obj->getSymbols();
    if (obj->addrsigSec) {
      ArrayRef<uint8_t> contents =
          check(obj->getObj().getSectionContents(*obj->addrsigSec));
      const uint8_t *cur = contents.begin();
      while (cur != contents.end()) {
        unsigned size;
        const char *err = nullptr;
        uint64_t symIndex = decodeULEB128(cur, &size, contents.end(), &err);
        if (err)
          fatal(toString(f) + ": could not decode addrsig section: " + err);
        markAddrsig(syms[symIndex]);
        cur += size;
      }
    } else {
      // If an object file does not have an address-significance table,
      // conservatively mark all of its symbols as address-significant.
      for (Symbol *s : syms)
        markAddrsig(s);
    }
  }
}

// This function reads a symbol partition specification section. These sections
// are used to control which partition a symbol is allocated to. See
// https://lld.llvm.org/Partitions.html for more details on partitions.
template <typename ELFT>
static void readSymbolPartitionSection(InputSectionBase *s) {
  // Read the relocation that refers to the partition's entry point symbol.
  Symbol *sym;
  const RelsOrRelas<ELFT> rels = s->template relsOrRelas<ELFT>();
  if (rels.areRelocsRel())
    sym = &s->file->getRelocTargetSym(rels.rels[0]);
  else
    sym = &s->file->getRelocTargetSym(rels.relas[0]);
  if (!isa<Defined>(sym) || !sym->includeInDynsym())
    return;

  StringRef partName = reinterpret_cast<const char *>(s->content().data());
  for (Partition &part : partitions) {
    if (part.name == partName) {
      sym->partition = part.getNumber();
      return;
    }
  }

  // Forbid partitions from being used on incompatible targets, and forbid them
  // from being used together with various linker features that assume a single
  // set of output sections.
  if (script->hasSectionsCommand)
    error(toString(s->file) +
          ": partitions cannot be used with the SECTIONS command");
  if (script->hasPhdrsCommands())
    error(toString(s->file) +
          ": partitions cannot be used with the PHDRS command");
  if (!config->sectionStartMap.empty())
    error(toString(s->file) + ": partitions cannot be used with "
                              "--section-start, -Ttext, -Tdata or -Tbss");
  if (config->emachine == EM_MIPS)
    error(toString(s->file) + ": partitions cannot be used on this target");

  // Impose a limit of no more than 254 partitions. This limit comes from the
  // sizes of the Partition fields in InputSectionBase and Symbol, as well as
  // the amount of space devoted to the partition number in RankFlags.
  if (partitions.size() == 254)
    fatal("may not have more than 254 partitions");

  partitions.emplace_back();
  Partition &newPart = partitions.back();
  newPart.name = partName;
  sym->partition = newPart.getNumber();
}

static void markBuffersAsDontNeed(bool skipLinkedOutput) {
  // With --thinlto-index-only, all buffers are nearly unused from now on
  // (except symbol/section names used by infrequent passes). Mark input file
  // buffers as MADV_DONTNEED so that these pages can be reused by the expensive
  // thin link, saving memory.
  if (skipLinkedOutput) {
    for (MemoryBuffer &mb : llvm::make_pointee_range(ctx.memoryBuffers))
      mb.dontNeedIfMmap();
    return;
  }

  // Otherwise, just mark MemoryBuffers backing BitcodeFiles.
  DenseSet<const char *> bufs;
  for (BitcodeFile *file : ctx.bitcodeFiles)
    bufs.insert(file->mb.getBufferStart());
  for (BitcodeFile *file : ctx.lazyBitcodeFiles)
    bufs.insert(file->mb.getBufferStart());
  for (MemoryBuffer &mb : llvm::make_pointee_range(ctx.memoryBuffers))
    if (bufs.count(mb.getBufferStart()))
      mb.dontNeedIfMmap();
}

// This function is where all the optimizations of link-time
// optimization takes place. When LTO is in use, some input files are
// not in native object file format but in the LLVM bitcode format.
// This function compiles bitcode files into a few big native files
// using LLVM functions and replaces bitcode symbols with the results.
// Because all bitcode files that the program consists of are passed to
// the compiler at once, it can do a whole-program optimization.
template <class ELFT>
void LinkerDriver::compileBitcodeFiles(bool skipLinkedOutput) {
  llvm::TimeTraceScope timeScope("LTO");
  // Compile bitcode files and replace bitcode symbols.
  lto.reset(new BitcodeCompiler);
  for (BitcodeFile *file : ctx.bitcodeFiles)
    lto->add(*file);

  if (!ctx.bitcodeFiles.empty())
    markBuffersAsDontNeed(skipLinkedOutput);

  for (InputFile *file : lto->compile()) {
    auto *obj = cast<ObjFile<ELFT>>(file);
    obj->parse(/*ignoreComdats=*/true);

    // Parse '@' in symbol names for non-relocatable output.
    if (!config->relocatable)
      for (Symbol *sym : obj->getGlobalSymbols())
        if (sym->hasVersionSuffix)
          sym->parseSymbolVersion();
    ctx.objectFiles.push_back(obj);
  }
}

// The --wrap option is a feature to rename symbols so that you can write
// wrappers for existing functions. If you pass `--wrap=foo`, all
// occurrences of symbol `foo` are resolved to `__wrap_foo` (so, you are
// expected to write `__wrap_foo` function as a wrapper). The original
// symbol becomes accessible as `__real_foo`, so you can call that from your
// wrapper.
//
// This data structure is instantiated for each --wrap option.
struct WrappedSymbol {
  Symbol *sym;
  Symbol *real;
  Symbol *wrap;
};

// Handles --wrap option.
//
// This function instantiates wrapper symbols. At this point, they seem
// like they are not being used at all, so we explicitly set some flags so
// that LTO won't eliminate them.
static std::vector<WrappedSymbol> addWrappedSymbols(opt::InputArgList &args) {
  std::vector<WrappedSymbol> v;
  DenseSet<StringRef> seen;

  for (auto *arg : args.filtered(OPT_wrap)) {
    StringRef name = arg->getValue();
    if (!seen.insert(name).second)
      continue;

    Symbol *sym = symtab.find(name);
    if (!sym)
      continue;

    Symbol *wrap =
        symtab.addUnusedUndefined(saver().save("__wrap_" + name), sym->binding);

    // If __real_ is referenced, pull in the symbol if it is lazy. Do this after
    // processing __wrap_ as that may have referenced __real_.
    StringRef realName = saver().save("__real_" + name);
    if (Symbol *real = symtab.find(realName)) {
      symtab.addUnusedUndefined(name, sym->binding);
      // Update sym's binding, which will replace real's later in
      // SymbolTable::wrap.
      sym->binding = real->binding;
    }

    Symbol *real = symtab.addUnusedUndefined(realName);
    v.push_back({sym, real, wrap});

    // We want to tell LTO not to inline symbols to be overwritten
    // because LTO doesn't know the final symbol contents after renaming.
    real->scriptDefined = true;
    sym->scriptDefined = true;

    // If a symbol is referenced in any object file, bitcode file or shared
    // object, mark its redirection target (foo for __real_foo and __wrap_foo
    // for foo) as referenced after redirection, which will be used to tell LTO
    // to not eliminate the redirection target. If the object file defining the
    // symbol also references it, we cannot easily distinguish the case from
    // cases where the symbol is not referenced. Retain the redirection target
    // in this case because we choose to wrap symbol references regardless of
    // whether the symbol is defined
    // (https://sourceware.org/bugzilla/show_bug.cgi?id=26358).
    if (real->referenced || real->isDefined())
      sym->referencedAfterWrap = true;
    if (sym->referenced || sym->isDefined())
      wrap->referencedAfterWrap = true;
  }
  return v;
}

static void combineVersionedSymbol(Symbol &sym,
                                   DenseMap<Symbol *, Symbol *> &map) {
  const char *suffix1 = sym.getVersionSuffix();
  if (suffix1[0] != '@' || suffix1[1] == '@')
    return;

  // Check the existing symbol foo. We have two special cases to handle:
  //
  // * There is a definition of foo@v1 and foo@@v1.
  // * There is a definition of foo@v1 and foo.
  Defined *sym2 = dyn_cast_or_null<Defined>(symtab.find(sym.getName()));
  if (!sym2)
    return;
  const char *suffix2 = sym2->getVersionSuffix();
  if (suffix2[0] == '@' && suffix2[1] == '@' &&
      strcmp(suffix1 + 1, suffix2 + 2) == 0) {
    // foo@v1 and foo@@v1 should be merged, so redirect foo@v1 to foo@@v1.
    map.try_emplace(&sym, sym2);
    // If both foo@v1 and foo@@v1 are defined and non-weak, report a
    // duplicate definition error.
    if (sym.isDefined()) {
      sym2->checkDuplicate(cast<Defined>(sym));
      sym2->resolve(cast<Defined>(sym));
    } else if (sym.isUndefined()) {
      sym2->resolve(cast<Undefined>(sym));
    } else {
      sym2->resolve(cast<SharedSymbol>(sym));
    }
    // Eliminate foo@v1 from the symbol table.
    sym.symbolKind = Symbol::PlaceholderKind;
    sym.isUsedInRegularObj = false;
  } else if (auto *sym1 = dyn_cast<Defined>(&sym)) {
    if (sym2->versionId > VER_NDX_GLOBAL
            ? config->versionDefinitions[sym2->versionId].name == suffix1 + 1
            : sym1->section == sym2->section && sym1->value == sym2->value) {
      // Due to an assembler design flaw, if foo is defined, .symver foo,
      // foo@v1 defines both foo and foo@v1. Unless foo is bound to a
      // different version, GNU ld makes foo@v1 canonical and eliminates
      // foo. Emulate its behavior, otherwise we would have foo or foo@@v1
      // beside foo@v1. foo@v1 and foo combining does not apply if they are
      // not defined in the same place.
      map.try_emplace(sym2, &sym);
      sym2->symbolKind = Symbol::PlaceholderKind;
      sym2->isUsedInRegularObj = false;
    }
  }
}

// Do renaming for --wrap and foo@v1 by updating pointers to symbols.
//
// When this function is executed, only InputFiles and symbol table
// contain pointers to symbol objects. We visit them to replace pointers,
// so that wrapped symbols are swapped as instructed by the command line.
static void redirectSymbols(ArrayRef<WrappedSymbol> wrapped) {
  llvm::TimeTraceScope timeScope("Redirect symbols");
  DenseMap<Symbol *, Symbol *> map;
  for (const WrappedSymbol &w : wrapped) {
    map[w.sym] = w.wrap;
    map[w.real] = w.sym;
  }

  // If there are version definitions (versionDefinitions.size() > 2), enumerate
  // symbols with a non-default version (foo@v1) and check whether it should be
  // combined with foo or foo@@v1.
  if (config->versionDefinitions.size() > 2)
    for (Symbol *sym : symtab.getSymbols())
      if (sym->hasVersionSuffix)
        combineVersionedSymbol(*sym, map);

  if (map.empty())
    return;

  // Update pointers in input files.
  parallelForEach(ctx.objectFiles, [&](ELFFileBase *file) {
    for (Symbol *&sym : file->getMutableGlobalSymbols())
      if (Symbol *s = map.lookup(sym))
        sym = s;
  });

  // Update pointers in the symbol table.
  for (const WrappedSymbol &w : wrapped)
    symtab.wrap(w.sym, w.real, w.wrap);
}

static void reportMissingFeature(StringRef config, const Twine &report) {
  if (config == "error")
    error(report);
  else if (config == "warning")
    warn(report);
}

static void checkAndReportMissingFeature(StringRef config, uint32_t features,
                                         uint32_t mask, const Twine &report) {
  if (!(features & mask))
    reportMissingFeature(config, report);
}

// To enable CET (x86's hardware-assisted control flow enforcement), each
// source file must be compiled with -fcf-protection. Object files compiled
// with the flag contain feature flags indicating that they are compatible
// with CET. We enable the feature only when all object files are compatible
// with CET.
//
// This is also the case with AARCH64's BTI and PAC which use the similar
// GNU_PROPERTY_AARCH64_FEATURE_1_AND mechanism.
//
// For AArch64 PAuth-enabled object files, the core info of all of them must
// match. Missing info for some object files with matching info for remaining
// ones can be allowed (see -z pauth-report).
static void readSecurityNotes() {
  if (config->emachine != EM_386 && config->emachine != EM_X86_64 &&
      config->emachine != EM_AARCH64)
    return;

  config->andFeatures = -1;

  StringRef referenceFileName;
  if (config->emachine == EM_AARCH64) {
    auto it = llvm::find_if(ctx.objectFiles, [](const ELFFileBase *f) {
      return !f->aarch64PauthAbiCoreInfo.empty();
    });
    if (it != ctx.objectFiles.end()) {
      ctx.aarch64PauthAbiCoreInfo = (*it)->aarch64PauthAbiCoreInfo;
      referenceFileName = (*it)->getName();
    }
  }

  for (ELFFileBase *f : ctx.objectFiles) {
    uint32_t features = f->andFeatures;

    checkAndReportMissingFeature(
        config->zBtiReport, features, GNU_PROPERTY_AARCH64_FEATURE_1_BTI,
        toString(f) + ": -z bti-report: file does not have "
                      "GNU_PROPERTY_AARCH64_FEATURE_1_BTI property");

    checkAndReportMissingFeature(
        config->zGcsReport, features, GNU_PROPERTY_AARCH64_FEATURE_1_GCS,
        toString(f) + ": -z gcs-report: file does not have "
                      "GNU_PROPERTY_AARCH64_FEATURE_1_GCS property");

    checkAndReportMissingFeature(
        config->zCetReport, features, GNU_PROPERTY_X86_FEATURE_1_IBT,
        toString(f) + ": -z cet-report: file does not have "
                      "GNU_PROPERTY_X86_FEATURE_1_IBT property");

    checkAndReportMissingFeature(
        config->zCetReport, features, GNU_PROPERTY_X86_FEATURE_1_SHSTK,
        toString(f) + ": -z cet-report: file does not have "
                      "GNU_PROPERTY_X86_FEATURE_1_SHSTK property");

    if (config->zForceBti && !(features & GNU_PROPERTY_AARCH64_FEATURE_1_BTI)) {
      features |= GNU_PROPERTY_AARCH64_FEATURE_1_BTI;
      if (config->zBtiReport == "none")
        warn(toString(f) + ": -z force-bti: file does not have "
                           "GNU_PROPERTY_AARCH64_FEATURE_1_BTI property");
    } else if (config->zForceIbt &&
               !(features & GNU_PROPERTY_X86_FEATURE_1_IBT)) {
      if (config->zCetReport == "none")
        warn(toString(f) + ": -z force-ibt: file does not have "
                           "GNU_PROPERTY_X86_FEATURE_1_IBT property");
      features |= GNU_PROPERTY_X86_FEATURE_1_IBT;
    }
    if (config->zPacPlt && !(features & GNU_PROPERTY_AARCH64_FEATURE_1_PAC)) {
      warn(toString(f) + ": -z pac-plt: file does not have "
                         "GNU_PROPERTY_AARCH64_FEATURE_1_PAC property");
      features |= GNU_PROPERTY_AARCH64_FEATURE_1_PAC;
    }
    config->andFeatures &= features;

    if (ctx.aarch64PauthAbiCoreInfo.empty())
      continue;

    if (f->aarch64PauthAbiCoreInfo.empty()) {
      reportMissingFeature(config->zPauthReport,
                           toString(f) +
                               ": -z pauth-report: file does not have AArch64 "
                               "PAuth core info while '" +
                               referenceFileName + "' has one");
      continue;
    }

    if (ctx.aarch64PauthAbiCoreInfo != f->aarch64PauthAbiCoreInfo)
      errorOrWarn("incompatible values of AArch64 PAuth core info found\n>>> " +
                  referenceFileName + ": 0x" +
                  toHex(ctx.aarch64PauthAbiCoreInfo, /*LowerCase=*/true) +
                  "\n>>> " + toString(f) + ": 0x" +
                  toHex(f->aarch64PauthAbiCoreInfo, /*LowerCase=*/true));
  }

  // Force enable Shadow Stack.
  if (config->zShstk)
    config->andFeatures |= GNU_PROPERTY_X86_FEATURE_1_SHSTK;

  // Force enable/disable GCS
  if (config->zGcs == GcsPolicy::Always)
    config->andFeatures |= GNU_PROPERTY_AARCH64_FEATURE_1_GCS;
  else if (config->zGcs == GcsPolicy::Never)
    config->andFeatures &= ~GNU_PROPERTY_AARCH64_FEATURE_1_GCS;
}

static void initSectionsAndLocalSyms(ELFFileBase *file, bool ignoreComdats) {
  switch (file->ekind) {
  case ELF32LEKind:
    cast<ObjFile<ELF32LE>>(file)->initSectionsAndLocalSyms(ignoreComdats);
    break;
  case ELF32BEKind:
    cast<ObjFile<ELF32BE>>(file)->initSectionsAndLocalSyms(ignoreComdats);
    break;
  case ELF64LEKind:
    cast<ObjFile<ELF64LE>>(file)->initSectionsAndLocalSyms(ignoreComdats);
    break;
  case ELF64BEKind:
    cast<ObjFile<ELF64BE>>(file)->initSectionsAndLocalSyms(ignoreComdats);
    break;
  default:
    llvm_unreachable("");
  }
}

static void postParseObjectFile(ELFFileBase *file) {
  switch (file->ekind) {
  case ELF32LEKind:
    cast<ObjFile<ELF32LE>>(file)->postParse();
    break;
  case ELF32BEKind:
    cast<ObjFile<ELF32BE>>(file)->postParse();
    break;
  case ELF64LEKind:
    cast<ObjFile<ELF64LE>>(file)->postParse();
    break;
  case ELF64BEKind:
    cast<ObjFile<ELF64BE>>(file)->postParse();
    break;
  default:
    llvm_unreachable("");
  }
}

// Do actual linking. Note that when this function is called,
// all linker scripts have already been parsed.
template <class ELFT> void LinkerDriver::link(opt::InputArgList &args) {
  llvm::TimeTraceScope timeScope("Link", StringRef("LinkerDriver::Link"));

  // Handle --trace-symbol.
  for (auto *arg : args.filtered(OPT_trace_symbol))
    symtab.insert(arg->getValue())->traced = true;

  ctx.internalFile = createInternalFile("<internal>");

  // Handle -u/--undefined before input files. If both a.a and b.so define foo,
  // -u foo a.a b.so will extract a.a.
  for (StringRef name : config->undefined)
    symtab.addUnusedUndefined(name)->referenced = true;

  parseFiles(files, armCmseImpLib);

  // Create dynamic sections for dynamic linking and static PIE.
  config->hasDynSymTab = !ctx.sharedFiles.empty() || config->isPic;

  // If an entry symbol is in a static archive, pull out that file now.
  if (Symbol *sym = symtab.find(config->entry))
    handleUndefined(sym, "--entry");

  // Handle the `--undefined-glob <pattern>` options.
  for (StringRef pat : args::getStrings(args, OPT_undefined_glob))
    handleUndefinedGlob(pat);

  // After potential archive member extraction involving ENTRY and
  // -u/--undefined-glob, check whether PROVIDE symbols should be defined (the
  // RHS may refer to definitions in just extracted object files).
  script->addScriptReferencedSymbolsToSymTable();

  // Prevent LTO from removing any definition referenced by -u.
  for (StringRef name : config->undefined)
    if (Defined *sym = dyn_cast_or_null<Defined>(symtab.find(name)))
      sym->isUsedInRegularObj = true;

  // Mark -init and -fini symbols so that the LTO doesn't eliminate them.
  if (Symbol *sym = dyn_cast_or_null<Defined>(symtab.find(config->init)))
    sym->isUsedInRegularObj = true;
  if (Symbol *sym = dyn_cast_or_null<Defined>(symtab.find(config->fini)))
    sym->isUsedInRegularObj = true;

  // If any of our inputs are bitcode files, the LTO code generator may create
  // references to certain library functions that might not be explicit in the
  // bitcode file's symbol table. If any of those library functions are defined
  // in a bitcode file in an archive member, we need to arrange to use LTO to
  // compile those archive members by adding them to the link beforehand.
  //
  // However, adding all libcall symbols to the link can have undesired
  // consequences. For example, the libgcc implementation of
  // __sync_val_compare_and_swap_8 on 32-bit ARM pulls in an .init_array entry
  // that aborts the program if the Linux kernel does not support 64-bit
  // atomics, which would prevent the program from running even if it does not
  // use 64-bit atomics.
  //
  // Therefore, we only add libcall symbols to the link before LTO if we have
  // to, i.e. if the symbol's definition is in bitcode. Any other required
  // libcall symbols will be added to the link after LTO when we add the LTO
  // object file to the link.
  if (!ctx.bitcodeFiles.empty()) {
    llvm::Triple TT(ctx.bitcodeFiles.front()->obj->getTargetTriple());
    for (auto *s : lto::LTO::getRuntimeLibcallSymbols(TT))
      handleLibcall(s);
  }

  // Archive members defining __wrap symbols may be extracted.
  std::vector<WrappedSymbol> wrapped = addWrappedSymbols(args);

  // No more lazy bitcode can be extracted at this point. Do post parse work
  // like checking duplicate symbols.
  parallelForEach(ctx.objectFiles, [](ELFFileBase *file) {
    initSectionsAndLocalSyms(file, /*ignoreComdats=*/false);
  });
  parallelForEach(ctx.objectFiles, postParseObjectFile);
  parallelForEach(ctx.bitcodeFiles,
                  [](BitcodeFile *file) { file->postParse(); });
  for (auto &it : ctx.nonPrevailingSyms) {
    Symbol &sym = *it.first;
    Undefined(sym.file, sym.getName(), sym.binding, sym.stOther, sym.type,
              it.second)
        .overwrite(sym);
    cast<Undefined>(sym).nonPrevailing = true;
  }
  ctx.nonPrevailingSyms.clear();
  for (const DuplicateSymbol &d : ctx.duplicates)
    reportDuplicate(*d.sym, d.file, d.section, d.value);
  ctx.duplicates.clear();

  // Return if there were name resolution errors.
  if (errorCount())
    return;

  // We want to declare linker script's symbols early,
  // so that we can version them.
  // They also might be exported if referenced by DSOs.
  script->declareSymbols();

  // Handle --exclude-libs. This is before scanVersionScript() due to a
  // workaround for Android ndk: for a defined versioned symbol in an archive
  // without a version node in the version script, Android does not expect a
  // 'has undefined version' error in -shared --exclude-libs=ALL mode (PR36295).
  // GNU ld errors in this case.
  if (args.hasArg(OPT_exclude_libs))
    excludeLibs(args);

  // Create elfHeader early. We need a dummy section in
  // addReservedSymbols to mark the created symbols as not absolute.
  Out::elfHeader = make<OutputSection>("", 0, SHF_ALLOC);

  // We need to create some reserved symbols such as _end. Create them.
  if (!config->relocatable)
    addReservedSymbols();

  // Apply version scripts.
  //
  // For a relocatable output, version scripts don't make sense, and
  // parsing a symbol version string (e.g. dropping "@ver1" from a symbol
  // name "foo@ver1") rather do harm, so we don't call this if -r is given.
  if (!config->relocatable) {
    llvm::TimeTraceScope timeScope("Process symbol versions");
    symtab.scanVersionScript();
  }

  // Skip the normal linked output if some LTO options are specified.
  //
  // For --thinlto-index-only, index file creation is performed in
  // compileBitcodeFiles, so we are done afterwards. --plugin-opt=emit-llvm and
  // --plugin-opt=emit-asm create output files in bitcode or assembly code,
  // respectively. When only certain thinLTO modules are specified for
  // compilation, the intermediate object file are the expected output.
  const bool skipLinkedOutput = config->thinLTOIndexOnly || config->emitLLVM ||
                                config->ltoEmitAsm ||
                                !config->thinLTOModulesToCompile.empty();

  // Handle --lto-validate-all-vtables-have-type-infos.
  if (config->ltoValidateAllVtablesHaveTypeInfos)
    ltoValidateAllVtablesHaveTypeInfos<ELFT>(args);

  // Do link-time optimization if given files are LLVM bitcode files.
  // This compiles bitcode files into real object files.
  //
  // With this the symbol table should be complete. After this, no new names
  // except a few linker-synthesized ones will be added to the symbol table.
  const size_t numObjsBeforeLTO = ctx.objectFiles.size();
  const size_t numInputFilesBeforeLTO = ctx.driver.files.size();
  compileBitcodeFiles<ELFT>(skipLinkedOutput);

  // Symbol resolution finished. Report backward reference problems,
  // --print-archive-stats=, and --why-extract=.
  reportBackrefs();
  writeArchiveStats();
  writeWhyExtract();
  if (errorCount())
    return;

  // Bail out if normal linked output is skipped due to LTO.
  if (skipLinkedOutput)
    return;

  // compileBitcodeFiles may have produced lto.tmp object files. After this, no
  // more file will be added.
  auto newObjectFiles = ArrayRef(ctx.objectFiles).slice(numObjsBeforeLTO);
  parallelForEach(newObjectFiles, [](ELFFileBase *file) {
    initSectionsAndLocalSyms(file, /*ignoreComdats=*/true);
  });
  parallelForEach(newObjectFiles, postParseObjectFile);
  for (const DuplicateSymbol &d : ctx.duplicates)
    reportDuplicate(*d.sym, d.file, d.section, d.value);

  // ELF dependent libraries may have introduced new input files after LTO has
  // completed. This is an error if the files haven't already been parsed, since
  // changing the symbol table could break the semantic assumptions of LTO.
  auto newInputFiles = ArrayRef(ctx.driver.files).slice(numInputFilesBeforeLTO);
  if (!newInputFiles.empty()) {
    DenseSet<StringRef> oldFilenames;
    for (InputFile *f :
         ArrayRef(ctx.driver.files).slice(0, numInputFilesBeforeLTO))
      oldFilenames.insert(f->getName());
    for (InputFile *newFile : newInputFiles)
      if (!oldFilenames.contains(newFile->getName()))
        errorOrWarn("input file '" + newFile->getName() + "' added after LTO");
  }

  // Handle --exclude-libs again because lto.tmp may reference additional
  // libcalls symbols defined in an excluded archive. This may override
  // versionId set by scanVersionScript().
  if (args.hasArg(OPT_exclude_libs))
    excludeLibs(args);

  // Record [__acle_se_<sym>, <sym>] pairs for later processing.
  processArmCmseSymbols();

  // Apply symbol renames for --wrap and combine foo@v1 and foo@@v1.
  redirectSymbols(wrapped);

  // Replace common symbols with regular symbols.
  replaceCommonSymbols();

  {
    llvm::TimeTraceScope timeScope("Aggregate sections");
    // Now that we have a complete list of input files.
    // Beyond this point, no new files are added.
    // Aggregate all input sections into one place.
    for (InputFile *f : ctx.objectFiles) {
      for (InputSectionBase *s : f->getSections()) {
        if (!s || s == &InputSection::discarded)
          continue;
        if (LLVM_UNLIKELY(isa<EhInputSection>(s)))
          ctx.ehInputSections.push_back(cast<EhInputSection>(s));
        else
          ctx.inputSections.push_back(s);
      }
    }
    for (BinaryFile *f : ctx.binaryFiles)
      for (InputSectionBase *s : f->getSections())
        ctx.inputSections.push_back(cast<InputSection>(s));
  }

  {
    llvm::TimeTraceScope timeScope("Strip sections");
    if (ctx.hasSympart.load(std::memory_order_relaxed)) {
      llvm::erase_if(ctx.inputSections, [](InputSectionBase *s) {
        if (s->type != SHT_LLVM_SYMPART)
          return false;
        readSymbolPartitionSection<ELFT>(s);
        return true;
      });
    }
    // We do not want to emit debug sections if --strip-all
    // or --strip-debug are given.
    if (config->strip != StripPolicy::None) {
      llvm::erase_if(ctx.inputSections, [](InputSectionBase *s) {
        if (isDebugSection(*s))
          return true;
        if (auto *isec = dyn_cast<InputSection>(s))
          if (InputSectionBase *rel = isec->getRelocatedSection())
            if (isDebugSection(*rel))
              return true;

        return false;
      });
    }
  }

  // Since we now have a complete set of input files, we can create
  // a .d file to record build dependencies.
  if (!config->dependencyFile.empty())
    writeDependencyFile();

  // Now that the number of partitions is fixed, save a pointer to the main
  // partition.
  mainPart = &partitions[0];

  // Read .note.gnu.property sections from input object files which
  // contain a hint to tweak linker's and loader's behaviors.
  readSecurityNotes();

  // The Target instance handles target-specific stuff, such as applying
  // relocations or writing a PLT section. It also contains target-dependent
  // values such as a default image base address.
  target = getTarget();

  config->eflags = target->calcEFlags();
  // maxPageSize (sometimes called abi page size) is the maximum page size that
  // the output can be run on. For example if the OS can use 4k or 64k page
  // sizes then maxPageSize must be 64k for the output to be useable on both.
  // All important alignment decisions must use this value.
  config->maxPageSize = getMaxPageSize(args);
  // commonPageSize is the most common page size that the output will be run on.
  // For example if an OS can use 4k or 64k page sizes and 4k is more common
  // than 64k then commonPageSize is set to 4k. commonPageSize can be used for
  // optimizations such as DATA_SEGMENT_ALIGN in linker scripts. LLD's use of it
  // is limited to writing trap instructions on the last executable segment.
  config->commonPageSize = getCommonPageSize(args);
  // textAlignPageSize is the alignment page size to use when aligning PT_LOAD
  // sections. This is the same as maxPageSize except under -omagic, where data
  // sections are non-aligned (maxPageSize set to 1) but text sections are aligned
  // to the target page size.
  config->textAlignPageSize = config->omagic ? getRealMaxPageSize(args) : config->maxPageSize;

  config->imageBase = getImageBase(args);

  // This adds a .comment section containing a version string.
  if (!config->relocatable)
    ctx.inputSections.push_back(createCommentSection());

  // Split SHF_MERGE and .eh_frame sections into pieces in preparation for garbage collection.
  splitSections<ELFT>();

  // Garbage collection and removal of shared symbols from unused shared objects.
  markLive<ELFT>();

  // Make copies of any input sections that need to be copied into each
  // partition.
  copySectionsIntoPartitions();

  if (canHaveMemtagGlobals()) {
    llvm::TimeTraceScope timeScope("Process memory tagged symbols");
    createTaggedSymbols(ctx.objectFiles);
  }

  // Create synthesized sections such as .got and .plt. This is called before
  // processSectionCommands() so that they can be placed by SECTIONS commands.
  createSyntheticSections<ELFT>();

  // Some input sections that are used for exception handling need to be moved
  // into synthetic sections. Do that now so that they aren't assigned to
  // output sections in the usual way.
  if (!config->relocatable)
    combineEhSections();

  // Merge .riscv.attributes sections.
  if (config->emachine == EM_RISCV)
    mergeRISCVAttributesSections();

  {
    llvm::TimeTraceScope timeScope("Assign sections");

    // Create output sections described by SECTIONS commands.
    script->processSectionCommands();

    // Linker scripts control how input sections are assigned to output
    // sections. Input sections that were not handled by scripts are called
    // "orphans", and they are assigned to output sections by the default rule.
    // Process that.
    script->addOrphanSections();
  }

  {
    llvm::TimeTraceScope timeScope("Merge/finalize input sections");

    // Migrate InputSectionDescription::sectionBases to sections. This includes
    // merging MergeInputSections into a single MergeSyntheticSection. From this
    // point onwards InputSectionDescription::sections should be used instead of
    // sectionBases.
    for (SectionCommand *cmd : script->sectionCommands)
      if (auto *osd = dyn_cast<OutputDesc>(cmd))
        osd->osec.finalizeInputSections(&script.s);
  }

  // Two input sections with different output sections should not be folded.
  // ICF runs after processSectionCommands() so that we know the output sections.
  if (config->icf != ICFLevel::None) {
    findKeepUniqueSections<ELFT>(args);
    doIcf<ELFT>();
  }

  // Read the callgraph now that we know what was gced or icfed
  if (config->callGraphProfileSort != CGProfileSortKind::None) {
    if (auto *arg = args.getLastArg(OPT_call_graph_ordering_file))
      if (std::optional<MemoryBufferRef> buffer = readFile(arg->getValue()))
        readCallGraph(*buffer);
    readCallGraphsFromObjectFiles<ELFT>();
  }

  // Write the result to the file.
  writeResult<ELFT>();
}
