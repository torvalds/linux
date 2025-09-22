//===- LTO.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTO.h"
#include "COFFLinkerContext.h"
#include "Config.h"
#include "InputFiles.h"
#include "Symbols.h"
#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Filesystem.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;
using namespace llvm::object;
using namespace lld;
using namespace lld::coff;

std::string BitcodeCompiler::getThinLTOOutputFile(StringRef path) {
  return lto::getThinLTOOutputFile(path, ctx.config.thinLTOPrefixReplaceOld,
                                   ctx.config.thinLTOPrefixReplaceNew);
}

lto::Config BitcodeCompiler::createConfig() {
  lto::Config c;
  c.Options = initTargetOptionsFromCodeGenFlags();
  c.Options.EmitAddrsig = true;
  for (StringRef C : ctx.config.mllvmOpts)
    c.MllvmArgs.emplace_back(C.str());

  // Always emit a section per function/datum with LTO. LLVM LTO should get most
  // of the benefit of linker GC, but there are still opportunities for ICF.
  c.Options.FunctionSections = true;
  c.Options.DataSections = true;

  // Use static reloc model on 32-bit x86 because it usually results in more
  // compact code, and because there are also known code generation bugs when
  // using the PIC model (see PR34306).
  if (ctx.config.machine == COFF::IMAGE_FILE_MACHINE_I386)
    c.RelocModel = Reloc::Static;
  else
    c.RelocModel = Reloc::PIC_;
#ifndef NDEBUG
  c.DisableVerify = false;
#else
  c.DisableVerify = true;
#endif
  c.DiagHandler = diagnosticHandler;
  c.DwoDir = ctx.config.dwoDir.str();
  c.OptLevel = ctx.config.ltoo;
  c.CPU = getCPUStr();
  c.MAttrs = getMAttrs();
  std::optional<CodeGenOptLevel> optLevelOrNone = CodeGenOpt::getLevel(
      ctx.config.ltoCgo.value_or(args::getCGOptLevel(ctx.config.ltoo)));
  assert(optLevelOrNone && "Invalid optimization level!");
  c.CGOptLevel = *optLevelOrNone;
  c.AlwaysEmitRegularLTOObj = !ctx.config.ltoObjPath.empty();
  c.DebugPassManager = ctx.config.ltoDebugPassManager;
  c.CSIRProfile = std::string(ctx.config.ltoCSProfileFile);
  c.RunCSIRInstr = ctx.config.ltoCSProfileGenerate;
  c.PGOWarnMismatch = ctx.config.ltoPGOWarnMismatch;
  c.SampleProfile = ctx.config.ltoSampleProfileName;
  c.TimeTraceEnabled = ctx.config.timeTraceEnabled;
  c.TimeTraceGranularity = ctx.config.timeTraceGranularity;

  if (ctx.config.emit == EmitKind::LLVM) {
    c.PreCodeGenModuleHook = [this](size_t task, const Module &m) {
      if (std::unique_ptr<raw_fd_ostream> os =
              openLTOOutputFile(ctx.config.outputFile))
        WriteBitcodeToFile(m, *os, false);
      return false;
    };
  } else if (ctx.config.emit == EmitKind::ASM) {
    c.CGFileType = CodeGenFileType::AssemblyFile;
    c.Options.MCOptions.AsmVerbose = true;
  }

  if (ctx.config.saveTemps)
    checkError(c.addSaveTemps(std::string(ctx.config.outputFile) + ".",
                              /*UseInputModulePath*/ true));
  return c;
}

BitcodeCompiler::BitcodeCompiler(COFFLinkerContext &c) : ctx(c) {
  // Initialize indexFile.
  if (!ctx.config.thinLTOIndexOnlyArg.empty())
    indexFile = openFile(ctx.config.thinLTOIndexOnlyArg);

  // Initialize ltoObj.
  lto::ThinBackend backend;
  if (ctx.config.thinLTOIndexOnly) {
    auto OnIndexWrite = [&](StringRef S) { thinIndices.erase(S); };
    backend = lto::createWriteIndexesThinBackend(
        std::string(ctx.config.thinLTOPrefixReplaceOld),
        std::string(ctx.config.thinLTOPrefixReplaceNew),
        std::string(ctx.config.thinLTOPrefixReplaceNativeObject),
        ctx.config.thinLTOEmitImportsFiles, indexFile.get(), OnIndexWrite);
  } else {
    backend = lto::createInProcessThinBackend(
        llvm::heavyweight_hardware_concurrency(ctx.config.thinLTOJobs));
  }

  ltoObj = std::make_unique<lto::LTO>(createConfig(), backend,
                                      ctx.config.ltoPartitions);
}

BitcodeCompiler::~BitcodeCompiler() = default;

static void undefine(Symbol *s) { replaceSymbol<Undefined>(s, s->getName()); }

void BitcodeCompiler::add(BitcodeFile &f) {
  lto::InputFile &obj = *f.obj;
  unsigned symNum = 0;
  std::vector<Symbol *> symBodies = f.getSymbols();
  std::vector<lto::SymbolResolution> resols(symBodies.size());

  if (ctx.config.thinLTOIndexOnly)
    thinIndices.insert(obj.getName());

  // Provide a resolution to the LTO API for each symbol.
  for (const lto::InputFile::Symbol &objSym : obj.symbols()) {
    Symbol *sym = symBodies[symNum];
    lto::SymbolResolution &r = resols[symNum];
    ++symNum;

    // Ideally we shouldn't check for SF_Undefined but currently IRObjectFile
    // reports two symbols for module ASM defined. Without this check, lld
    // flags an undefined in IR with a definition in ASM as prevailing.
    // Once IRObjectFile is fixed to report only one symbol this hack can
    // be removed.
    r.Prevailing = !objSym.isUndefined() && sym->getFile() == &f;
    r.VisibleToRegularObj = sym->isUsedInRegularObj;
    if (r.Prevailing)
      undefine(sym);

    // We tell LTO to not apply interprocedural optimization for wrapped
    // (with -wrap) symbols because otherwise LTO would inline them while
    // their values are still not final.
    r.LinkerRedefined = !sym->canInline;
  }
  checkError(ltoObj->add(std::move(f.obj), resols));
}

// Merge all the bitcode files we have seen, codegen the result
// and return the resulting objects.
std::vector<InputFile *> BitcodeCompiler::compile() {
  unsigned maxTasks = ltoObj->getMaxTasks();
  buf.resize(maxTasks);
  files.resize(maxTasks);
  file_names.resize(maxTasks);

  // The /lldltocache option specifies the path to a directory in which to cache
  // native object files for ThinLTO incremental builds. If a path was
  // specified, configure LTO to use it as the cache directory.
  FileCache cache;
  if (!ctx.config.ltoCache.empty())
    cache = check(localCache("ThinLTO", "Thin", ctx.config.ltoCache,
                             [&](size_t task, const Twine &moduleName,
                                 std::unique_ptr<MemoryBuffer> mb) {
                               files[task] = std::move(mb);
                               file_names[task] = moduleName.str();
                             }));

  checkError(ltoObj->run(
      [&](size_t task, const Twine &moduleName) {
        buf[task].first = moduleName.str();
        return std::make_unique<CachedFileStream>(
            std::make_unique<raw_svector_ostream>(buf[task].second));
      },
      cache));

  // Emit empty index files for non-indexed files
  for (StringRef s : thinIndices) {
    std::string path = getThinLTOOutputFile(s);
    openFile(path + ".thinlto.bc");
    if (ctx.config.thinLTOEmitImportsFiles)
      openFile(path + ".imports");
  }

  // ThinLTO with index only option is required to generate only the index
  // files. After that, we exit from linker and ThinLTO backend runs in a
  // distributed environment.
  if (ctx.config.thinLTOIndexOnly) {
    if (!ctx.config.ltoObjPath.empty())
      saveBuffer(buf[0].second, ctx.config.ltoObjPath);
    if (indexFile)
      indexFile->close();
    return {};
  }

  if (!ctx.config.ltoCache.empty())
    pruneCache(ctx.config.ltoCache, ctx.config.ltoCachePolicy, files);

  std::vector<InputFile *> ret;
  bool emitASM = ctx.config.emit == EmitKind::ASM;
  const char *Ext = emitASM ? ".s" : ".obj";
  for (unsigned i = 0; i != maxTasks; ++i) {
    StringRef bitcodeFilePath;
    // Get the native object contents either from the cache or from memory.  Do
    // not use the cached MemoryBuffer directly, or the PDB will not be
    // deterministic.
    StringRef objBuf;
    if (files[i]) {
      objBuf = files[i]->getBuffer();
      bitcodeFilePath = file_names[i];
    } else {
      objBuf = buf[i].second;
      bitcodeFilePath = buf[i].first;
    }
    if (objBuf.empty())
      continue;

    // If the input bitcode file is path/to/a.obj, then the corresponding lto
    // object file name will look something like: path/to/main.exe.lto.a.obj.
    StringRef ltoObjName;
    if (bitcodeFilePath == "ld-temp.o") {
      ltoObjName =
          saver().save(Twine(ctx.config.outputFile) + ".lto" +
                       (i == 0 ? Twine("") : Twine('.') + Twine(i)) + Ext);
    } else {
      StringRef directory = sys::path::parent_path(bitcodeFilePath);
      StringRef baseName = sys::path::stem(bitcodeFilePath);
      StringRef outputFileBaseName = sys::path::filename(ctx.config.outputFile);
      SmallString<64> path;
      sys::path::append(path, directory,
                        outputFileBaseName + ".lto." + baseName + Ext);
      sys::path::remove_dots(path, true);
      ltoObjName = saver().save(path.str());
    }
    if (ctx.config.saveTemps || emitASM)
      saveBuffer(buf[i].second, ltoObjName);
    if (!emitASM)
      ret.push_back(make<ObjFile>(ctx, MemoryBufferRef(objBuf, ltoObjName)));
  }

  return ret;
}
