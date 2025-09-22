//===- LTO.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTO.h"
#include "Config.h"
#include "InputFiles.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Filesystem.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

static std::string getThinLTOOutputFile(StringRef modulePath) {
  return lto::getThinLTOOutputFile(modulePath, config->thinLTOPrefixReplaceOld,
                                   config->thinLTOPrefixReplaceNew);
}

static lto::Config createConfig() {
  lto::Config c;

  // LLD supports the new relocations and address-significance tables.
  c.Options = initTargetOptionsFromCodeGenFlags();
  c.Options.EmitAddrsig = true;
  for (StringRef C : config->mllvmOpts)
    c.MllvmArgs.emplace_back(C.str());

  // Always emit a section per function/datum with LTO.
  c.Options.FunctionSections = true;
  c.Options.DataSections = true;

  c.Options.BBAddrMap = config->ltoBBAddrMap;

  // Check if basic block sections must be used.
  // Allowed values for --lto-basic-block-sections are "all", "labels",
  // "<file name specifying basic block ids>", or none.  This is the equivalent
  // of -fbasic-block-sections= flag in clang.
  if (!config->ltoBasicBlockSections.empty()) {
    if (config->ltoBasicBlockSections == "all") {
      c.Options.BBSections = BasicBlockSection::All;
    } else if (config->ltoBasicBlockSections == "labels") {
      c.Options.BBSections = BasicBlockSection::Labels;
    } else if (config->ltoBasicBlockSections == "none") {
      c.Options.BBSections = BasicBlockSection::None;
    } else {
      ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
          MemoryBuffer::getFile(config->ltoBasicBlockSections.str());
      if (!MBOrErr) {
        error("cannot open " + config->ltoBasicBlockSections + ":" +
              MBOrErr.getError().message());
      } else {
        c.Options.BBSectionsFuncListBuf = std::move(*MBOrErr);
      }
      c.Options.BBSections = BasicBlockSection::List;
    }
  }

  c.Options.UniqueBasicBlockSectionNames =
      config->ltoUniqueBasicBlockSectionNames;

  if (auto relocModel = getRelocModelFromCMModel())
    c.RelocModel = *relocModel;
  else if (config->relocatable)
    c.RelocModel = std::nullopt;
  else if (config->isPic)
    c.RelocModel = Reloc::PIC_;
  else
    c.RelocModel = Reloc::Static;

  c.CodeModel = getCodeModelFromCMModel();
  c.DisableVerify = config->disableVerify;
  c.DiagHandler = diagnosticHandler;
  c.OptLevel = config->ltoo;
  c.CPU = getCPUStr();
  c.MAttrs = getMAttrs();
  c.CGOptLevel = config->ltoCgo;

  c.PTO.LoopVectorization = c.OptLevel > 1;
  c.PTO.SLPVectorization = c.OptLevel > 1;

  // Set up a custom pipeline if we've been asked to.
  c.OptPipeline = std::string(config->ltoNewPmPasses);
  c.AAPipeline = std::string(config->ltoAAPipeline);

  // Set up optimization remarks if we've been asked to.
  c.RemarksFilename = std::string(config->optRemarksFilename);
  c.RemarksPasses = std::string(config->optRemarksPasses);
  c.RemarksWithHotness = config->optRemarksWithHotness;
  c.RemarksHotnessThreshold = config->optRemarksHotnessThreshold;
  c.RemarksFormat = std::string(config->optRemarksFormat);

  // Set up output file to emit statistics.
  c.StatsFile = std::string(config->optStatsFilename);

  c.SampleProfile = std::string(config->ltoSampleProfile);
  for (StringRef pluginFn : config->passPlugins)
    c.PassPlugins.push_back(std::string(pluginFn));
  c.DebugPassManager = config->ltoDebugPassManager;
  c.DwoDir = std::string(config->dwoDir);

  c.HasWholeProgramVisibility = config->ltoWholeProgramVisibility;
  c.ValidateAllVtablesHaveTypeInfos =
      config->ltoValidateAllVtablesHaveTypeInfos;
  c.AllVtablesHaveTypeInfos = ctx.ltoAllVtablesHaveTypeInfos;
  c.AlwaysEmitRegularLTOObj = !config->ltoObjPath.empty();

  for (const llvm::StringRef &name : config->thinLTOModulesToCompile)
    c.ThinLTOModulesToCompile.emplace_back(name);

  c.TimeTraceEnabled = config->timeTraceEnabled;
  c.TimeTraceGranularity = config->timeTraceGranularity;

  c.CSIRProfile = std::string(config->ltoCSProfileFile);
  c.RunCSIRInstr = config->ltoCSProfileGenerate;
  c.PGOWarnMismatch = config->ltoPGOWarnMismatch;

  if (config->emitLLVM) {
    c.PreCodeGenModuleHook = [](size_t task, const Module &m) {
      if (std::unique_ptr<raw_fd_ostream> os =
              openLTOOutputFile(config->outputFile))
        WriteBitcodeToFile(m, *os, false);
      return false;
    };
  }

  if (config->ltoEmitAsm) {
    c.CGFileType = CodeGenFileType::AssemblyFile;
    c.Options.MCOptions.AsmVerbose = true;
  }

  if (!config->saveTempsArgs.empty())
    checkError(c.addSaveTemps(config->outputFile.str() + ".",
                              /*UseInputModulePath*/ true,
                              config->saveTempsArgs));
  return c;
}

BitcodeCompiler::BitcodeCompiler() {
  // Initialize indexFile.
  if (!config->thinLTOIndexOnlyArg.empty())
    indexFile = openFile(config->thinLTOIndexOnlyArg);

  // Initialize ltoObj.
  lto::ThinBackend backend;
  auto onIndexWrite = [&](StringRef s) { thinIndices.erase(s); };
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

  constexpr llvm::lto::LTO::LTOKind ltoModes[3] =
    {llvm::lto::LTO::LTOKind::LTOK_UnifiedThin,
     llvm::lto::LTO::LTOKind::LTOK_UnifiedRegular,
     llvm::lto::LTO::LTOKind::LTOK_Default};
  ltoObj = std::make_unique<lto::LTO>(
      createConfig(), backend, config->ltoPartitions,
      ltoModes[config->ltoKind]);

  // Initialize usedStartStop.
  if (ctx.bitcodeFiles.empty())
    return;
  for (Symbol *sym : symtab.getSymbols()) {
    if (sym->isPlaceholder())
      continue;
    StringRef s = sym->getName();
    for (StringRef prefix : {"__start_", "__stop_"})
      if (s.starts_with(prefix))
        usedStartStop.insert(s.substr(prefix.size()));
  }
}

BitcodeCompiler::~BitcodeCompiler() = default;

void BitcodeCompiler::add(BitcodeFile &f) {
  lto::InputFile &obj = *f.obj;
  bool isExec = !config->shared && !config->relocatable;

  if (config->thinLTOEmitIndexFiles)
    thinIndices.insert(obj.getName());

  ArrayRef<Symbol *> syms = f.getSymbols();
  ArrayRef<lto::InputFile::Symbol> objSyms = obj.symbols();
  std::vector<lto::SymbolResolution> resols(syms.size());

  // Provide a resolution to the LTO API for each symbol.
  for (size_t i = 0, e = syms.size(); i != e; ++i) {
    Symbol *sym = syms[i];
    const lto::InputFile::Symbol &objSym = objSyms[i];
    lto::SymbolResolution &r = resols[i];

    // Ideally we shouldn't check for SF_Undefined but currently IRObjectFile
    // reports two symbols for module ASM defined. Without this check, lld
    // flags an undefined in IR with a definition in ASM as prevailing.
    // Once IRObjectFile is fixed to report only one symbol this hack can
    // be removed.
    r.Prevailing = !objSym.isUndefined() && sym->file == &f;

    // We ask LTO to preserve following global symbols:
    // 1) All symbols when doing relocatable link, so that them can be used
    //    for doing final link.
    // 2) Symbols that are used in regular objects.
    // 3) C named sections if we have corresponding __start_/__stop_ symbol.
    // 4) Symbols that are defined in bitcode files and used for dynamic
    //    linking.
    // 5) Symbols that will be referenced after linker wrapping is performed.
    r.VisibleToRegularObj = config->relocatable || sym->isUsedInRegularObj ||
                            sym->referencedAfterWrap ||
                            (r.Prevailing && sym->includeInDynsym()) ||
                            usedStartStop.count(objSym.getSectionName());
    // Identify symbols exported dynamically, and that therefore could be
    // referenced by a shared library not visible to the linker.
    r.ExportDynamic =
        sym->computeBinding() != STB_LOCAL &&
        (config->exportDynamic || sym->exportDynamic || sym->inDynamicList);
    const auto *dr = dyn_cast<Defined>(sym);
    r.FinalDefinitionInLinkageUnit =
        (isExec || sym->visibility() != STV_DEFAULT) && dr &&
        // Skip absolute symbols from ELF objects, otherwise PC-rel relocations
        // will be generated by for them, triggering linker errors.
        // Symbol section is always null for bitcode symbols, hence the check
        // for isElf(). Skip linker script defined symbols as well: they have
        // no File defined.
        !(dr->section == nullptr &&
          (sym->file->isInternal() || sym->file->isElf()));

    if (r.Prevailing)
      Undefined(ctx.internalFile, StringRef(), STB_GLOBAL, STV_DEFAULT,
                sym->type)
          .overwrite(*sym);

    // We tell LTO to not apply interprocedural optimization for wrapped
    // (with --wrap) symbols because otherwise LTO would inline them while
    // their values are still not final.
    r.LinkerRedefined = sym->scriptDefined;
  }
  checkError(ltoObj->add(std::move(f.obj), resols));
}

// If LazyObjFile has not been added to link, emit empty index files.
// This is needed because this is what GNU gold plugin does and we have a
// distributed build system that depends on that behavior.
static void thinLTOCreateEmptyIndexFiles() {
  DenseSet<StringRef> linkedBitCodeFiles;
  for (BitcodeFile *f : ctx.bitcodeFiles)
    linkedBitCodeFiles.insert(f->getName());

  for (BitcodeFile *f : ctx.lazyBitcodeFiles) {
    if (!f->lazy)
      continue;
    if (linkedBitCodeFiles.contains(f->getName()))
      continue;
    std::string path =
        replaceThinLTOSuffix(getThinLTOOutputFile(f->obj->getName()));
    std::unique_ptr<raw_fd_ostream> os = openFile(path + ".thinlto.bc");
    if (!os)
      continue;

    ModuleSummaryIndex m(/*HaveGVs*/ false);
    m.setSkipModuleByDistributedBackend();
    writeIndexToFile(m, *os);
    if (config->thinLTOEmitImportsFiles)
      openFile(path + ".imports");
  }
}

// Merge all the bitcode files we have seen, codegen the result
// and return the resulting ObjectFile(s).
std::vector<InputFile *> BitcodeCompiler::compile() {
  unsigned maxTasks = ltoObj->getMaxTasks();
  buf.resize(maxTasks);
  files.resize(maxTasks);
  filenames.resize(maxTasks);

  // The --thinlto-cache-dir option specifies the path to a directory in which
  // to cache native object files for ThinLTO incremental builds. If a path was
  // specified, configure LTO to use it as the cache directory.
  FileCache cache;
  if (!config->thinLTOCacheDir.empty())
    cache = check(localCache("ThinLTO", "Thin", config->thinLTOCacheDir,
                             [&](size_t task, const Twine &moduleName,
                                 std::unique_ptr<MemoryBuffer> mb) {
                               files[task] = std::move(mb);
                               filenames[task] = moduleName.str();
                             }));

  if (!ctx.bitcodeFiles.empty())
    checkError(ltoObj->run(
        [&](size_t task, const Twine &moduleName) {
          buf[task].first = moduleName.str();
          return std::make_unique<CachedFileStream>(
              std::make_unique<raw_svector_ostream>(buf[task].second));
        },
        cache));

  // Emit empty index files for non-indexed files but not in single-module mode.
  if (config->thinLTOModulesToCompile.empty()) {
    for (StringRef s : thinIndices) {
      std::string path = getThinLTOOutputFile(s);
      openFile(path + ".thinlto.bc");
      if (config->thinLTOEmitImportsFiles)
        openFile(path + ".imports");
    }
  }

  if (config->thinLTOEmitIndexFiles)
    thinLTOCreateEmptyIndexFiles();

  if (config->thinLTOIndexOnly) {
    if (!config->ltoObjPath.empty())
      saveBuffer(buf[0].second, config->ltoObjPath);

    // ThinLTO with index only option is required to generate only the index
    // files. After that, we exit from linker and ThinLTO backend runs in a
    // distributed environment.
    if (indexFile)
      indexFile->close();
    return {};
  }

  if (!config->thinLTOCacheDir.empty())
    pruneCache(config->thinLTOCacheDir, config->thinLTOCachePolicy, files);

  if (!config->ltoObjPath.empty()) {
    saveBuffer(buf[0].second, config->ltoObjPath);
    for (unsigned i = 1; i != maxTasks; ++i)
      saveBuffer(buf[i].second, config->ltoObjPath + Twine(i));
  }

  bool savePrelink = config->saveTempsArgs.contains("prelink");
  std::vector<InputFile *> ret;
  const char *ext = config->ltoEmitAsm ? ".s" : ".o";
  for (unsigned i = 0; i != maxTasks; ++i) {
    StringRef bitcodeFilePath;
    StringRef objBuf;
    if (files[i]) {
      // When files[i] is not null, we get the native relocatable file from the
      // cache. filenames[i] contains the original BitcodeFile's identifier.
      objBuf = files[i]->getBuffer();
      bitcodeFilePath = filenames[i];
    } else {
      // Get the native relocatable file after in-process LTO compilation.
      objBuf = buf[i].second;
      bitcodeFilePath = buf[i].first;
    }
    if (objBuf.empty())
      continue;

    // If the input bitcode file is path/to/x.o and -o specifies a.out, the
    // corresponding native relocatable file path will look like:
    // path/to/a.out.lto.x.o.
    StringRef ltoObjName;
    if (bitcodeFilePath == "ld-temp.o") {
      ltoObjName =
          saver().save(Twine(config->outputFile) + ".lto" +
                       (i == 0 ? Twine("") : Twine('.') + Twine(i)) + ext);
    } else {
      StringRef directory = sys::path::parent_path(bitcodeFilePath);
      // For an archive member, which has an identifier like "d/a.a(coll.o at
      // 8)" (see BitcodeFile::BitcodeFile), use the filename; otherwise, use
      // the stem (d/a.o => a).
      StringRef baseName = bitcodeFilePath.ends_with(")")
                               ? sys::path::filename(bitcodeFilePath)
                               : sys::path::stem(bitcodeFilePath);
      StringRef outputFileBaseName = sys::path::filename(config->outputFile);
      SmallString<256> path;
      sys::path::append(path, directory,
                        outputFileBaseName + ".lto." + baseName + ext);
      sys::path::remove_dots(path, true);
      ltoObjName = saver().save(path.str());
    }
    if (savePrelink || config->ltoEmitAsm)
      saveBuffer(buf[i].second, ltoObjName);
    if (!config->ltoEmitAsm)
      ret.push_back(createObjFile(MemoryBufferRef(objBuf, ltoObjName)));
  }
  return ret;
}
