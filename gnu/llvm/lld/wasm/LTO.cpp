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
#include "Symbols.h"
#include "lld/Common/Args.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
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

namespace lld::wasm {
static std::unique_ptr<lto::LTO> createLTO() {
  lto::Config c;
  c.Options = initTargetOptionsFromCodeGenFlags();

  // Always emit a section per function/data with LTO.
  c.Options.FunctionSections = true;
  c.Options.DataSections = true;

  c.DisableVerify = config->disableVerify;
  c.DiagHandler = diagnosticHandler;
  c.OptLevel = config->ltoo;
  c.MAttrs = getMAttrs();
  c.CGOptLevel = config->ltoCgo;
  c.DebugPassManager = config->ltoDebugPassManager;

  if (config->relocatable)
    c.RelocModel = std::nullopt;
  else if (ctx.isPic)
    c.RelocModel = Reloc::PIC_;
  else
    c.RelocModel = Reloc::Static;

  if (config->saveTemps)
    checkError(c.addSaveTemps(config->outputFile.str() + ".",
                              /*UseInputModulePath*/ true));
  lto::ThinBackend backend = lto::createInProcessThinBackend(
      llvm::heavyweight_hardware_concurrency(config->thinLTOJobs));
  return std::make_unique<lto::LTO>(std::move(c), backend,
                                     config->ltoPartitions);
}

BitcodeCompiler::BitcodeCompiler() : ltoObj(createLTO()) {}

BitcodeCompiler::~BitcodeCompiler() = default;

static void undefine(Symbol *s) {
  if (auto f = dyn_cast<DefinedFunction>(s))
    replaceSymbol<UndefinedFunction>(f, f->getName(), std::nullopt,
                                     std::nullopt, 0, f->getFile(),
                                     f->signature);
  else if (isa<DefinedData>(s))
    replaceSymbol<UndefinedData>(s, s->getName(), 0, s->getFile());
  else
    llvm_unreachable("unexpected symbol kind");
}

void BitcodeCompiler::add(BitcodeFile &f) {
  lto::InputFile &obj = *f.obj;
  unsigned symNum = 0;
  ArrayRef<Symbol *> syms = f.getSymbols();
  std::vector<lto::SymbolResolution> resols(syms.size());

  // Provide a resolution to the LTO API for each symbol.
  for (const lto::InputFile::Symbol &objSym : obj.symbols()) {
    Symbol *sym = syms[symNum];
    lto::SymbolResolution &r = resols[symNum];
    ++symNum;

    // Ideally we shouldn't check for SF_Undefined but currently IRObjectFile
    // reports two symbols for module ASM defined. Without this check, lld
    // flags an undefined in IR with a definition in ASM as prevailing.
    // Once IRObjectFile is fixed to report only one symbol this hack can
    // be removed.
    r.Prevailing = !objSym.isUndefined() && sym->getFile() == &f;
    r.VisibleToRegularObj = config->relocatable || sym->isUsedInRegularObj ||
                            sym->isNoStrip() ||
                            (r.Prevailing && sym->isExported());
    if (r.Prevailing)
      undefine(sym);

    // We tell LTO to not apply interprocedural optimization for wrapped
    // (with --wrap) symbols because otherwise LTO would inline them while
    // their values are still not final.
    r.LinkerRedefined = !sym->canInline;
  }
  checkError(ltoObj->add(std::move(f.obj), resols));
}

// Merge all the bitcode files we have seen, codegen the result
// and return the resulting objects.
std::vector<StringRef> BitcodeCompiler::compile() {
  unsigned maxTasks = ltoObj->getMaxTasks();
  buf.resize(maxTasks);
  files.resize(maxTasks);

  // The --thinlto-cache-dir option specifies the path to a directory in which
  // to cache native object files for ThinLTO incremental builds. If a path was
  // specified, configure LTO to use it as the cache directory.
  FileCache cache;
  if (!config->thinLTOCacheDir.empty())
    cache = check(localCache("ThinLTO", "Thin", config->thinLTOCacheDir,
                             [&](size_t task, const Twine &moduleName,
                                 std::unique_ptr<MemoryBuffer> mb) {
                               files[task] = std::move(mb);
                             }));

  checkError(ltoObj->run(
      [&](size_t task, const Twine &moduleName) {
        return std::make_unique<CachedFileStream>(
            std::make_unique<raw_svector_ostream>(buf[task]));
      },
      cache));

  if (!config->thinLTOCacheDir.empty())
    pruneCache(config->thinLTOCacheDir, config->thinLTOCachePolicy, files);

  std::vector<StringRef> ret;
  for (unsigned i = 0; i != maxTasks; ++i) {
    if (buf[i].empty())
      continue;
    if (config->saveTemps) {
      if (i == 0)
        saveBuffer(buf[i], config->outputFile + ".lto.o");
      else
        saveBuffer(buf[i], config->outputFile + Twine(i) + ".lto.o");
    }
    ret.emplace_back(buf[i].data(), buf[i].size());
  }

  for (std::unique_ptr<MemoryBuffer> &file : files)
    if (file)
      ret.push_back(file->getBuffer());

  return ret;
}

} // namespace lld::wasm
