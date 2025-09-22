//===- LTO.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTO.h"
#include "Config.h"
#include "Driver.h"
#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"

#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Filesystem.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/ObjCARC.h"

using namespace lld;
using namespace lld::macho;
using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::sys;

static std::string getThinLTOOutputFile(StringRef modulePath) {
  return lto::getThinLTOOutputFile(modulePath, config->thinLTOPrefixReplaceOld,
                                   config->thinLTOPrefixReplaceNew);
}

static lto::Config createConfig() {
  lto::Config c;
  c.Options = initTargetOptionsFromCodeGenFlags();
  c.Options.EmitAddrsig = config->icfLevel == ICFLevel::safe;
  for (StringRef C : config->mllvmOpts)
    c.MllvmArgs.emplace_back(C.str());
  c.CodeModel = getCodeModelFromCMModel();
  c.CPU = getCPUStr();
  c.MAttrs = getMAttrs();
  c.DiagHandler = diagnosticHandler;
  c.PreCodeGenPassesHook = [](legacy::PassManager &pm) {
    pm.add(createObjCARCContractPass());
  };

  c.AlwaysEmitRegularLTOObj = !config->ltoObjPath.empty();

  c.TimeTraceEnabled = config->timeTraceEnabled;
  c.TimeTraceGranularity = config->timeTraceGranularity;
  c.DebugPassManager = config->ltoDebugPassManager;
  c.CSIRProfile = std::string(config->csProfilePath);
  c.RunCSIRInstr = config->csProfileGenerate;
  c.PGOWarnMismatch = config->pgoWarnMismatch;
  c.OptLevel = config->ltoo;
  c.CGOptLevel = config->ltoCgo;
  if (config->saveTemps)
    checkError(c.addSaveTemps(config->outputFile.str() + ".",
                              /*UseInputModulePath=*/true));
  return c;
}

// If `originalPath` exists, hardlinks `path` to `originalPath`. If that fails,
// or `originalPath` is not set, saves `buffer` to `path`.
static void saveOrHardlinkBuffer(StringRef buffer, const Twine &path,
                                 std::optional<StringRef> originalPath) {
  if (originalPath) {
    auto err = fs::create_hard_link(*originalPath, path);
    if (!err)
      return;
  }
  saveBuffer(buffer, path);
}

BitcodeCompiler::BitcodeCompiler() {
  // Initialize indexFile.
  if (!config->thinLTOIndexOnlyArg.empty())
    indexFile = openFile(config->thinLTOIndexOnlyArg);

  // Initialize ltoObj.
  lto::ThinBackend backend;
  auto onIndexWrite = [&](StringRef S) { thinIndices.erase(S); };
  if (config->thinLTOIndexOnly) {
    backend = lto::createWriteIndexesThinBackend(
        std::string(config->thinLTOPrefixReplaceOld),
        std::string(config->thinLTOPrefixReplaceNew),
        std::string(config->thinLTOPrefixReplaceNativeObject),
        config->thinLTOEmitImportsFiles, indexFile.get(), onIndexWrite);
  } else {
    backend = lto::createInProcessThinBackend(
        llvm::heavyweight_hardware_concurrency(config->thinLTOJobs),
        onIndexWrite, config->thinLTOEmitIndexFiles,
        config->thinLTOEmitImportsFiles);
  }

  ltoObj = std::make_unique<lto::LTO>(createConfig(), backend);
}

void BitcodeCompiler::add(BitcodeFile &f) {
  lto::InputFile &obj = *f.obj;

  if (config->thinLTOEmitIndexFiles)
    thinIndices.insert(obj.getName());

  ArrayRef<lto::InputFile::Symbol> objSyms = obj.symbols();
  std::vector<lto::SymbolResolution> resols;
  resols.reserve(objSyms.size());

  // Provide a resolution to the LTO API for each symbol.
  bool exportDynamic =
      config->outputType != MH_EXECUTE || config->exportDynamic;
  auto symIt = f.symbols.begin();
  for (const lto::InputFile::Symbol &objSym : objSyms) {
    resols.emplace_back();
    lto::SymbolResolution &r = resols.back();
    Symbol *sym = *symIt++;

    // Ideally we shouldn't check for SF_Undefined but currently IRObjectFile
    // reports two symbols for module ASM defined. Without this check, lld
    // flags an undefined in IR with a definition in ASM as prevailing.
    // Once IRObjectFile is fixed to report only one symbol this hack can
    // be removed.
    r.Prevailing = !objSym.isUndefined() && sym->getFile() == &f;

    if (const auto *defined = dyn_cast<Defined>(sym)) {
      r.ExportDynamic =
          defined->isExternal() && !defined->privateExtern && exportDynamic;
      r.FinalDefinitionInLinkageUnit =
          !defined->isExternalWeakDef() && !defined->interposable;
    } else if (const auto *common = dyn_cast<CommonSymbol>(sym)) {
      r.ExportDynamic = !common->privateExtern && exportDynamic;
      r.FinalDefinitionInLinkageUnit = true;
    }

    r.VisibleToRegularObj =
        sym->isUsedInRegularObj || (r.Prevailing && r.ExportDynamic);

    // Un-define the symbol so that we don't get duplicate symbol errors when we
    // load the ObjFile emitted by LTO compilation.
    if (r.Prevailing)
      replaceSymbol<Undefined>(sym, sym->getName(), sym->getFile(),
                               RefState::Strong, /*wasBitcodeSymbol=*/true);

    // TODO: set the other resolution configs properly
  }
  checkError(ltoObj->add(std::move(f.obj), resols));
  hasFiles = true;
}

// If LazyObjFile has not been added to link, emit empty index files.
// This is needed because this is what GNU gold plugin does and we have a
// distributed build system that depends on that behavior.
static void thinLTOCreateEmptyIndexFiles() {
  DenseSet<StringRef> linkedBitCodeFiles;
  for (InputFile *file : inputFiles)
    if (auto *f = dyn_cast<BitcodeFile>(file))
      if (!f->lazy)
        linkedBitCodeFiles.insert(f->getName());

  for (InputFile *file : inputFiles) {
    if (auto *f = dyn_cast<BitcodeFile>(file)) {
      if (!f->lazy)
        continue;
      if (linkedBitCodeFiles.contains(f->getName()))
        continue;
      std::string path =
          replaceThinLTOSuffix(getThinLTOOutputFile(f->obj->getName()));
      std::unique_ptr<raw_fd_ostream> os = openFile(path + ".thinlto.bc");
      if (!os)
        continue;

      ModuleSummaryIndex m(/*HaveGVs=*/false);
      m.setSkipModuleByDistributedBackend();
      writeIndexToFile(m, *os);
      if (config->thinLTOEmitImportsFiles)
        openFile(path + ".imports");
    }
  }
}

// Merge all the bitcode files we have seen, codegen the result
// and return the resulting ObjectFile(s).
std::vector<ObjFile *> BitcodeCompiler::compile() {
  unsigned maxTasks = ltoObj->getMaxTasks();
  buf.resize(maxTasks);
  files.resize(maxTasks);

  // The -cache_path_lto option specifies the path to a directory in which
  // to cache native object files for ThinLTO incremental builds. If a path was
  // specified, configure LTO to use it as the cache directory.
  FileCache cache;
  if (!config->thinLTOCacheDir.empty())
    cache = check(localCache("ThinLTO", "Thin", config->thinLTOCacheDir,
                             [&](size_t task, const Twine &moduleName,
                                 std::unique_ptr<MemoryBuffer> mb) {
                               files[task] = std::move(mb);
                             }));

  if (hasFiles)
    checkError(ltoObj->run(
        [&](size_t task, const Twine &moduleName) {
          return std::make_unique<CachedFileStream>(
              std::make_unique<raw_svector_ostream>(buf[task]));
        },
        cache));

  // Emit empty index files for non-indexed files
  for (StringRef s : thinIndices) {
    std::string path = getThinLTOOutputFile(s);
    openFile(path + ".thinlto.bc");
    if (config->thinLTOEmitImportsFiles)
      openFile(path + ".imports");
  }

  if (config->thinLTOEmitIndexFiles)
    thinLTOCreateEmptyIndexFiles();

  // In ThinLTO mode, Clang passes a temporary directory in -object_path_lto,
  // while the argument is a single file in FullLTO mode.
  bool objPathIsDir = true;
  if (!config->ltoObjPath.empty()) {
    if (std::error_code ec = fs::create_directories(config->ltoObjPath))
      fatal("cannot create LTO object path " + config->ltoObjPath + ": " +
            ec.message());

    if (!fs::is_directory(config->ltoObjPath)) {
      objPathIsDir = false;
      unsigned objCount =
          count_if(buf, [](const SmallString<0> &b) { return !b.empty(); });
      if (objCount > 1)
        fatal("-object_path_lto must specify a directory when using ThinLTO");
    }
  }

  auto outputFilePath = [objPathIsDir](int i) {
    SmallString<261> filePath("/tmp/lto.tmp");
    if (!config->ltoObjPath.empty()) {
      filePath = config->ltoObjPath;
      if (objPathIsDir)
        path::append(filePath, Twine(i) + "." +
                                   getArchitectureName(config->arch()) +
                                   ".lto.o");
    }
    return filePath;
  };

  // ThinLTO with index only option is required to generate only the index
  // files. After that, we exit from linker and ThinLTO backend runs in a
  // distributed environment.
  if (config->thinLTOIndexOnly) {
    if (!config->ltoObjPath.empty())
      saveBuffer(buf[0], outputFilePath(0));
    if (indexFile)
      indexFile->close();
    return {};
  }

  if (!config->thinLTOCacheDir.empty())
    pruneCache(config->thinLTOCacheDir, config->thinLTOCachePolicy, files);

  std::vector<ObjFile *> ret;
  for (unsigned i = 0; i < maxTasks; ++i) {
    // Get the native object contents either from the cache or from memory.  Do
    // not use the cached MemoryBuffer directly to ensure dsymutil does not
    // race with the cache pruner.
    StringRef objBuf;
    std::optional<StringRef> cachePath;
    if (files[i]) {
      objBuf = files[i]->getBuffer();
      cachePath = files[i]->getBufferIdentifier();
    } else {
      objBuf = buf[i];
    }
    if (objBuf.empty())
      continue;

    // FIXME: should `saveTemps` and `ltoObjPath` use the same file name?
    if (config->saveTemps)
      saveBuffer(objBuf,
                 config->outputFile + ((i == 0) ? "" : Twine(i)) + ".lto.o");

    auto filePath = outputFilePath(i);
    uint32_t modTime = 0;
    if (!config->ltoObjPath.empty()) {
      saveOrHardlinkBuffer(objBuf, filePath, cachePath);
      modTime = getModTime(filePath);
    }
    ret.push_back(make<ObjFile>(
        MemoryBufferRef(objBuf, saver().save(filePath.str())), modTime,
        /*archiveName=*/"", /*lazy=*/false,
        /*forceHidden=*/false, /*compatArch=*/true, /*builtFromBitcode=*/true));
  }

  return ret;
}
