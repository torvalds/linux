//=== DWARFLinkerImpl.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFLinkerImpl.h"
#include "DIEGenerator.h"
#include "DependencyTracker.h"
#include "llvm/DWARFLinker/Utils.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/ThreadPool.h"

using namespace llvm;
using namespace dwarf_linker;
using namespace dwarf_linker::parallel;

DWARFLinkerImpl::DWARFLinkerImpl(MessageHandlerTy ErrorHandler,
                                 MessageHandlerTy WarningHandler)
    : UniqueUnitID(0), DebugStrStrings(GlobalData),
      DebugLineStrStrings(GlobalData), CommonSections(GlobalData) {
  GlobalData.setErrorHandler(ErrorHandler);
  GlobalData.setWarningHandler(WarningHandler);
}

DWARFLinkerImpl::LinkContext::LinkContext(LinkingGlobalData &GlobalData,
                                          DWARFFile &File,
                                          StringMap<uint64_t> &ClangModules,
                                          std::atomic<size_t> &UniqueUnitID)
    : OutputSections(GlobalData), InputDWARFFile(File),
      ClangModules(ClangModules), UniqueUnitID(UniqueUnitID) {

  if (File.Dwarf) {
    if (!File.Dwarf->compile_units().empty())
      CompileUnits.reserve(File.Dwarf->getNumCompileUnits());

    // Set context format&endianness based on the input file.
    Format.Version = File.Dwarf->getMaxVersion();
    Format.AddrSize = File.Dwarf->getCUAddrSize();
    Endianness = File.Dwarf->isLittleEndian() ? llvm::endianness::little
                                              : llvm::endianness::big;
  }
}

DWARFLinkerImpl::LinkContext::RefModuleUnit::RefModuleUnit(
    DWARFFile &File, std::unique_ptr<CompileUnit> Unit)
    : File(File), Unit(std::move(Unit)) {}

DWARFLinkerImpl::LinkContext::RefModuleUnit::RefModuleUnit(
    LinkContext::RefModuleUnit &&Other)
    : File(Other.File), Unit(std::move(Other.Unit)) {}

void DWARFLinkerImpl::LinkContext::addModulesCompileUnit(
    LinkContext::RefModuleUnit &&Unit) {
  ModulesCompileUnits.emplace_back(std::move(Unit));
}

void DWARFLinkerImpl::addObjectFile(DWARFFile &File, ObjFileLoaderTy Loader,
                                    CompileUnitHandlerTy OnCUDieLoaded) {
  ObjectContexts.emplace_back(std::make_unique<LinkContext>(
      GlobalData, File, ClangModules, UniqueUnitID));

  if (ObjectContexts.back()->InputDWARFFile.Dwarf) {
    for (const std::unique_ptr<DWARFUnit> &CU :
         ObjectContexts.back()->InputDWARFFile.Dwarf->compile_units()) {
      DWARFDie CUDie = CU->getUnitDIE();
      OverallNumberOfCU++;

      if (!CUDie)
        continue;

      OnCUDieLoaded(*CU);

      // Register mofule reference.
      if (!GlobalData.getOptions().UpdateIndexTablesOnly)
        ObjectContexts.back()->registerModuleReference(CUDie, Loader,
                                                       OnCUDieLoaded);
    }
  }
}

void DWARFLinkerImpl::setEstimatedObjfilesAmount(unsigned ObjFilesNum) {
  ObjectContexts.reserve(ObjFilesNum);
}

Error DWARFLinkerImpl::link() {
  // reset compile unit unique ID counter.
  UniqueUnitID = 0;

  if (Error Err = validateAndUpdateOptions())
    return Err;

  dwarf::FormParams GlobalFormat = {GlobalData.getOptions().TargetDWARFVersion,
                                    0, dwarf::DwarfFormat::DWARF32};
  llvm::endianness GlobalEndianness = llvm::endianness::native;

  if (std::optional<std::reference_wrapper<const Triple>> CurTriple =
          GlobalData.getTargetTriple()) {
    GlobalEndianness = (*CurTriple).get().isLittleEndian()
                           ? llvm::endianness::little
                           : llvm::endianness::big;
  }
  std::optional<uint16_t> Language;

  for (std::unique_ptr<LinkContext> &Context : ObjectContexts) {
    if (Context->InputDWARFFile.Dwarf == nullptr) {
      Context->setOutputFormat(Context->getFormParams(), GlobalEndianness);
      continue;
    }

    if (GlobalData.getOptions().Verbose) {
      outs() << "DEBUG MAP OBJECT: " << Context->InputDWARFFile.FileName
             << "\n";

      for (const std::unique_ptr<DWARFUnit> &OrigCU :
           Context->InputDWARFFile.Dwarf->compile_units()) {
        outs() << "Input compilation unit:";
        DIDumpOptions DumpOpts;
        DumpOpts.ChildRecurseDepth = 0;
        DumpOpts.Verbose = GlobalData.getOptions().Verbose;
        OrigCU->getUnitDIE().dump(outs(), 0, DumpOpts);
      }
    }

    // Verify input DWARF if requested.
    if (GlobalData.getOptions().VerifyInputDWARF)
      verifyInput(Context->InputDWARFFile);

    if (!GlobalData.getTargetTriple())
      GlobalEndianness = Context->getEndianness();
    GlobalFormat.AddrSize =
        std::max(GlobalFormat.AddrSize, Context->getFormParams().AddrSize);

    Context->setOutputFormat(Context->getFormParams(), GlobalEndianness);

    // FIXME: move creation of CompileUnits into the addObjectFile.
    // This would allow to not scan for context Language and Modules state
    // twice. And then following handling might be removed.
    for (const std::unique_ptr<DWARFUnit> &OrigCU :
         Context->InputDWARFFile.Dwarf->compile_units()) {
      DWARFDie UnitDie = OrigCU->getUnitDIE();

      if (!Language) {
        if (std::optional<DWARFFormValue> Val =
                UnitDie.find(dwarf::DW_AT_language)) {
          uint16_t LangVal = dwarf::toUnsigned(Val, 0);
          if (isODRLanguage(LangVal))
            Language = LangVal;
        }
      }
    }
  }

  if (GlobalFormat.AddrSize == 0) {
    if (std::optional<std::reference_wrapper<const Triple>> TargetTriple =
            GlobalData.getTargetTriple())
      GlobalFormat.AddrSize = (*TargetTriple).get().isArch32Bit() ? 4 : 8;
    else
      GlobalFormat.AddrSize = 8;
  }

  CommonSections.setOutputFormat(GlobalFormat, GlobalEndianness);

  if (!GlobalData.Options.NoODR && Language.has_value()) {
    llvm::parallel::TaskGroup TGroup;
    TGroup.spawn([&]() {
      ArtificialTypeUnit = std::make_unique<TypeUnit>(
          GlobalData, UniqueUnitID++, Language, GlobalFormat, GlobalEndianness);
    });
  }

  // Set parallel options.
  if (GlobalData.getOptions().Threads == 0)
    llvm::parallel::strategy = optimal_concurrency(OverallNumberOfCU);
  else
    llvm::parallel::strategy =
        hardware_concurrency(GlobalData.getOptions().Threads);

  // Link object files.
  if (GlobalData.getOptions().Threads == 1) {
    for (std::unique_ptr<LinkContext> &Context : ObjectContexts) {
      // Link object file.
      if (Error Err = Context->link(ArtificialTypeUnit.get()))
        GlobalData.error(std::move(Err), Context->InputDWARFFile.FileName);

      Context->InputDWARFFile.unload();
    }
  } else {
    DefaultThreadPool Pool(llvm::parallel::strategy);
    for (std::unique_ptr<LinkContext> &Context : ObjectContexts)
      Pool.async([&]() {
        // Link object file.
        if (Error Err = Context->link(ArtificialTypeUnit.get()))
          GlobalData.error(std::move(Err), Context->InputDWARFFile.FileName);

        Context->InputDWARFFile.unload();
      });

    Pool.wait();
  }

  if (ArtificialTypeUnit != nullptr && !ArtificialTypeUnit->getTypePool()
                                            .getRoot()
                                            ->getValue()
                                            .load()
                                            ->Children.empty()) {
    if (GlobalData.getTargetTriple().has_value())
      if (Error Err = ArtificialTypeUnit->finishCloningAndEmit(
              (*GlobalData.getTargetTriple()).get()))
        return Err;
  }

  // At this stage each compile units are cloned to their own set of debug
  // sections. Now, update patches, assign offsets and assemble final file
  // glueing debug tables from each compile unit.
  glueCompileUnitsAndWriteToTheOutput();

  return Error::success();
}

void DWARFLinkerImpl::verifyInput(const DWARFFile &File) {
  assert(File.Dwarf);

  std::string Buffer;
  raw_string_ostream OS(Buffer);
  DIDumpOptions DumpOpts;
  if (!File.Dwarf->verify(OS, DumpOpts.noImplicitRecursion())) {
    if (GlobalData.getOptions().InputVerificationHandler)
      GlobalData.getOptions().InputVerificationHandler(File, OS.str());
  }
}

Error DWARFLinkerImpl::validateAndUpdateOptions() {
  if (GlobalData.getOptions().TargetDWARFVersion == 0)
    return createStringError(std::errc::invalid_argument,
                             "target DWARF version is not set");

  if (GlobalData.getOptions().Verbose && GlobalData.getOptions().Threads != 1) {
    GlobalData.Options.Threads = 1;
    GlobalData.warn(
        "set number of threads to 1 to make --verbose to work properly.", "");
  }

  // Do not do types deduplication in case --update.
  if (GlobalData.getOptions().UpdateIndexTablesOnly &&
      !GlobalData.Options.NoODR)
    GlobalData.Options.NoODR = true;

  return Error::success();
}

/// Resolve the relative path to a build artifact referenced by DWARF by
/// applying DW_AT_comp_dir.
static void resolveRelativeObjectPath(SmallVectorImpl<char> &Buf, DWARFDie CU) {
  sys::path::append(Buf, dwarf::toString(CU.find(dwarf::DW_AT_comp_dir), ""));
}

static uint64_t getDwoId(const DWARFDie &CUDie) {
  auto DwoId = dwarf::toUnsigned(
      CUDie.find({dwarf::DW_AT_dwo_id, dwarf::DW_AT_GNU_dwo_id}));
  if (DwoId)
    return *DwoId;
  return 0;
}

static std::string
remapPath(StringRef Path,
          const DWARFLinker::ObjectPrefixMapTy &ObjectPrefixMap) {
  if (ObjectPrefixMap.empty())
    return Path.str();

  SmallString<256> p = Path;
  for (const auto &Entry : ObjectPrefixMap)
    if (llvm::sys::path::replace_path_prefix(p, Entry.first, Entry.second))
      break;
  return p.str().str();
}

static std::string getPCMFile(const DWARFDie &CUDie,
                              DWARFLinker::ObjectPrefixMapTy *ObjectPrefixMap) {
  std::string PCMFile = dwarf::toString(
      CUDie.find({dwarf::DW_AT_dwo_name, dwarf::DW_AT_GNU_dwo_name}), "");

  if (PCMFile.empty())
    return PCMFile;

  if (ObjectPrefixMap)
    PCMFile = remapPath(PCMFile, *ObjectPrefixMap);

  return PCMFile;
}

std::pair<bool, bool> DWARFLinkerImpl::LinkContext::isClangModuleRef(
    const DWARFDie &CUDie, std::string &PCMFile, unsigned Indent, bool Quiet) {
  if (PCMFile.empty())
    return std::make_pair(false, false);

  // Clang module DWARF skeleton CUs abuse this for the path to the module.
  uint64_t DwoId = getDwoId(CUDie);

  std::string Name = dwarf::toString(CUDie.find(dwarf::DW_AT_name), "");
  if (Name.empty()) {
    if (!Quiet)
      GlobalData.warn("anonymous module skeleton CU for " + PCMFile + ".",
                      InputDWARFFile.FileName);
    return std::make_pair(true, true);
  }

  if (!Quiet && GlobalData.getOptions().Verbose) {
    outs().indent(Indent);
    outs() << "Found clang module reference " << PCMFile;
  }

  auto Cached = ClangModules.find(PCMFile);
  if (Cached != ClangModules.end()) {
    // FIXME: Until PR27449 (https://llvm.org/bugs/show_bug.cgi?id=27449) is
    // fixed in clang, only warn about DWO_id mismatches in verbose mode.
    // ASTFileSignatures will change randomly when a module is rebuilt.
    if (!Quiet && GlobalData.getOptions().Verbose && (Cached->second != DwoId))
      GlobalData.warn(
          Twine("hash mismatch: this object file was built against a "
                "different version of the module ") +
              PCMFile + ".",
          InputDWARFFile.FileName);
    if (!Quiet && GlobalData.getOptions().Verbose)
      outs() << " [cached].\n";
    return std::make_pair(true, true);
  }

  return std::make_pair(true, false);
}

/// If this compile unit is really a skeleton CU that points to a
/// clang module, register it in ClangModules and return true.
///
/// A skeleton CU is a CU without children, a DW_AT_gnu_dwo_name
/// pointing to the module, and a DW_AT_gnu_dwo_id with the module
/// hash.
bool DWARFLinkerImpl::LinkContext::registerModuleReference(
    const DWARFDie &CUDie, ObjFileLoaderTy Loader,
    CompileUnitHandlerTy OnCUDieLoaded, unsigned Indent) {
  std::string PCMFile =
      getPCMFile(CUDie, GlobalData.getOptions().ObjectPrefixMap);
  std::pair<bool, bool> IsClangModuleRef =
      isClangModuleRef(CUDie, PCMFile, Indent, false);

  if (!IsClangModuleRef.first)
    return false;

  if (IsClangModuleRef.second)
    return true;

  if (GlobalData.getOptions().Verbose)
    outs() << " ...\n";

  // Cyclic dependencies are disallowed by Clang, but we still
  // shouldn't run into an infinite loop, so mark it as processed now.
  ClangModules.insert({PCMFile, getDwoId(CUDie)});

  if (Error E =
          loadClangModule(Loader, CUDie, PCMFile, OnCUDieLoaded, Indent + 2)) {
    consumeError(std::move(E));
    return false;
  }
  return true;
}

Error DWARFLinkerImpl::LinkContext::loadClangModule(
    ObjFileLoaderTy Loader, const DWARFDie &CUDie, const std::string &PCMFile,
    CompileUnitHandlerTy OnCUDieLoaded, unsigned Indent) {

  uint64_t DwoId = getDwoId(CUDie);
  std::string ModuleName = dwarf::toString(CUDie.find(dwarf::DW_AT_name), "");

  /// Using a SmallString<0> because loadClangModule() is recursive.
  SmallString<0> Path(GlobalData.getOptions().PrependPath);
  if (sys::path::is_relative(PCMFile))
    resolveRelativeObjectPath(Path, CUDie);
  sys::path::append(Path, PCMFile);
  // Don't use the cached binary holder because we have no thread-safety
  // guarantee and the lifetime is limited.

  if (Loader == nullptr) {
    GlobalData.error("cann't load clang module: loader is not specified.",
                     InputDWARFFile.FileName);
    return Error::success();
  }

  auto ErrOrObj = Loader(InputDWARFFile.FileName, Path);
  if (!ErrOrObj)
    return Error::success();

  std::unique_ptr<CompileUnit> Unit;
  for (const auto &CU : ErrOrObj->Dwarf->compile_units()) {
    OnCUDieLoaded(*CU);
    // Recursively get all modules imported by this one.
    auto ChildCUDie = CU->getUnitDIE();
    if (!ChildCUDie)
      continue;
    if (!registerModuleReference(ChildCUDie, Loader, OnCUDieLoaded, Indent)) {
      if (Unit) {
        std::string Err =
            (PCMFile +
             ": Clang modules are expected to have exactly 1 compile unit.\n");
        GlobalData.error(Err, InputDWARFFile.FileName);
        return make_error<StringError>(Err, inconvertibleErrorCode());
      }
      // FIXME: Until PR27449 (https://llvm.org/bugs/show_bug.cgi?id=27449) is
      // fixed in clang, only warn about DWO_id mismatches in verbose mode.
      // ASTFileSignatures will change randomly when a module is rebuilt.
      uint64_t PCMDwoId = getDwoId(ChildCUDie);
      if (PCMDwoId != DwoId) {
        if (GlobalData.getOptions().Verbose)
          GlobalData.warn(
              Twine("hash mismatch: this object file was built against a "
                    "different version of the module ") +
                  PCMFile + ".",
              InputDWARFFile.FileName);
        // Update the cache entry with the DwoId of the module loaded from disk.
        ClangModules[PCMFile] = PCMDwoId;
      }

      // Empty modules units should not be cloned.
      if (!ChildCUDie.hasChildren())
        continue;

      // Add this module.
      Unit = std::make_unique<CompileUnit>(
          GlobalData, *CU, UniqueUnitID.fetch_add(1), ModuleName, *ErrOrObj,
          getUnitForOffset, CU->getFormParams(), getEndianness());
    }
  }

  if (Unit) {
    ModulesCompileUnits.emplace_back(RefModuleUnit{*ErrOrObj, std::move(Unit)});
    // Preload line table, as it can't be loaded asynchronously.
    ModulesCompileUnits.back().Unit->loadLineTable();
  }

  return Error::success();
}

Error DWARFLinkerImpl::LinkContext::link(TypeUnit *ArtificialTypeUnit) {
  InterCUProcessingStarted = false;
  if (!InputDWARFFile.Dwarf)
    return Error::success();

  // Preload macro tables, as they can't be loaded asynchronously.
  InputDWARFFile.Dwarf->getDebugMacinfo();
  InputDWARFFile.Dwarf->getDebugMacro();

  // Link modules compile units first.
  parallelForEach(ModulesCompileUnits, [&](RefModuleUnit &RefModule) {
    linkSingleCompileUnit(*RefModule.Unit, ArtificialTypeUnit);
  });

  // Check for live relocations. If there is no any live relocation then we
  // can skip entire object file.
  if (!GlobalData.getOptions().UpdateIndexTablesOnly &&
      !InputDWARFFile.Addresses->hasValidRelocs()) {
    if (GlobalData.getOptions().Verbose)
      outs() << "No valid relocations found. Skipping.\n";
    return Error::success();
  }

  OriginalDebugInfoSize = getInputDebugInfoSize();

  // Create CompileUnit structures to keep information about source
  // DWARFUnit`s, load line tables.
  for (const auto &OrigCU : InputDWARFFile.Dwarf->compile_units()) {
    // Load only unit DIE at this stage.
    auto CUDie = OrigCU->getUnitDIE();
    std::string PCMFile =
        getPCMFile(CUDie, GlobalData.getOptions().ObjectPrefixMap);

    // The !isClangModuleRef condition effectively skips over fully resolved
    // skeleton units.
    if (!CUDie || GlobalData.getOptions().UpdateIndexTablesOnly ||
        !isClangModuleRef(CUDie, PCMFile, 0, true).first) {
      CompileUnits.emplace_back(std::make_unique<CompileUnit>(
          GlobalData, *OrigCU, UniqueUnitID.fetch_add(1), "", InputDWARFFile,
          getUnitForOffset, OrigCU->getFormParams(), getEndianness()));

      // Preload line table, as it can't be loaded asynchronously.
      CompileUnits.back()->loadLineTable();
    }
  };

  HasNewInterconnectedCUs = false;

  // Link self-sufficient compile units and discover inter-connected compile
  // units.
  parallelForEach(CompileUnits, [&](std::unique_ptr<CompileUnit> &CU) {
    linkSingleCompileUnit(*CU, ArtificialTypeUnit);
  });

  // Link all inter-connected units.
  if (HasNewInterconnectedCUs) {
    InterCUProcessingStarted = true;

    if (Error Err = finiteLoop([&]() -> Expected<bool> {
          HasNewInterconnectedCUs = false;

          // Load inter-connected units.
          parallelForEach(CompileUnits, [&](std::unique_ptr<CompileUnit> &CU) {
            if (CU->isInterconnectedCU()) {
              CU->maybeResetToLoadedStage();
              linkSingleCompileUnit(*CU, ArtificialTypeUnit,
                                    CompileUnit::Stage::Loaded);
            }
          });

          // Do liveness analysis for inter-connected units.
          parallelForEach(CompileUnits, [&](std::unique_ptr<CompileUnit> &CU) {
            linkSingleCompileUnit(*CU, ArtificialTypeUnit,
                                  CompileUnit::Stage::LivenessAnalysisDone);
          });

          return HasNewInterconnectedCUs.load();
        }))
      return Err;

    // Update dependencies.
    if (Error Err = finiteLoop([&]() -> Expected<bool> {
          HasNewGlobalDependency = false;
          parallelForEach(CompileUnits, [&](std::unique_ptr<CompileUnit> &CU) {
            linkSingleCompileUnit(
                *CU, ArtificialTypeUnit,
                CompileUnit::Stage::UpdateDependenciesCompleteness);
          });
          return HasNewGlobalDependency.load();
        }))
      return Err;
    parallelForEach(CompileUnits, [&](std::unique_ptr<CompileUnit> &CU) {
      if (CU->isInterconnectedCU() &&
          CU->getStage() == CompileUnit::Stage::LivenessAnalysisDone)
        CU->setStage(CompileUnit::Stage::UpdateDependenciesCompleteness);
    });

    // Assign type names.
    parallelForEach(CompileUnits, [&](std::unique_ptr<CompileUnit> &CU) {
      linkSingleCompileUnit(*CU, ArtificialTypeUnit,
                            CompileUnit::Stage::TypeNamesAssigned);
    });

    // Clone inter-connected units.
    parallelForEach(CompileUnits, [&](std::unique_ptr<CompileUnit> &CU) {
      linkSingleCompileUnit(*CU, ArtificialTypeUnit,
                            CompileUnit::Stage::Cloned);
    });

    // Update patches for inter-connected units.
    parallelForEach(CompileUnits, [&](std::unique_ptr<CompileUnit> &CU) {
      linkSingleCompileUnit(*CU, ArtificialTypeUnit,
                            CompileUnit::Stage::PatchesUpdated);
    });

    // Release data.
    parallelForEach(CompileUnits, [&](std::unique_ptr<CompileUnit> &CU) {
      linkSingleCompileUnit(*CU, ArtificialTypeUnit,
                            CompileUnit::Stage::Cleaned);
    });
  }

  if (GlobalData.getOptions().UpdateIndexTablesOnly) {
    // Emit Invariant sections.

    if (Error Err = emitInvariantSections())
      return Err;
  } else if (!CompileUnits.empty()) {
    // Emit .debug_frame section.

    Error ResultErr = Error::success();
    llvm::parallel::TaskGroup TGroup;
    // We use task group here as PerThreadBumpPtrAllocator should be called from
    // the threads created by ThreadPoolExecutor.
    TGroup.spawn([&]() {
      if (Error Err = cloneAndEmitDebugFrame())
        ResultErr = std::move(Err);
    });
    return ResultErr;
  }

  return Error::success();
}

void DWARFLinkerImpl::LinkContext::linkSingleCompileUnit(
    CompileUnit &CU, TypeUnit *ArtificialTypeUnit,
    enum CompileUnit::Stage DoUntilStage) {
  if (InterCUProcessingStarted != CU.isInterconnectedCU())
    return;

  if (Error Err = finiteLoop([&]() -> Expected<bool> {
        if (CU.getStage() >= DoUntilStage)
          return false;

        switch (CU.getStage()) {
        case CompileUnit::Stage::CreatedNotLoaded: {
          // Load input compilation unit DIEs.
          // Analyze properties of DIEs.
          if (!CU.loadInputDIEs()) {
            // We do not need to do liveness analysis for invalid compilation
            // unit.
            CU.setStage(CompileUnit::Stage::Skipped);
          } else {
            CU.analyzeDWARFStructure();

            // The registerModuleReference() condition effectively skips
            // over fully resolved skeleton units. This second pass of
            // registerModuleReferences doesn't do any new work, but it
            // will collect top-level errors, which are suppressed. Module
            // warnings were already displayed in the first iteration.
            if (registerModuleReference(
                    CU.getOrigUnit().getUnitDIE(), nullptr,
                    [](const DWARFUnit &) {}, 0))
              CU.setStage(CompileUnit::Stage::PatchesUpdated);
            else
              CU.setStage(CompileUnit::Stage::Loaded);
          }
        } break;

        case CompileUnit::Stage::Loaded: {
          // Mark all the DIEs that need to be present in the generated output.
          // If ODR requested, build type names.
          if (!CU.resolveDependenciesAndMarkLiveness(InterCUProcessingStarted,
                                                     HasNewInterconnectedCUs)) {
            assert(HasNewInterconnectedCUs &&
                   "Flag indicating new inter-connections is not set");
            return false;
          }

          CU.setStage(CompileUnit::Stage::LivenessAnalysisDone);
        } break;

        case CompileUnit::Stage::LivenessAnalysisDone: {
          if (InterCUProcessingStarted) {
            if (CU.updateDependenciesCompleteness())
              HasNewGlobalDependency = true;
            return false;
          } else {
            if (Error Err = finiteLoop([&]() -> Expected<bool> {
                  return CU.updateDependenciesCompleteness();
                }))
              return std::move(Err);

            CU.setStage(CompileUnit::Stage::UpdateDependenciesCompleteness);
          }
        } break;

        case CompileUnit::Stage::UpdateDependenciesCompleteness:
#ifndef NDEBUG
          CU.verifyDependencies();
#endif

          if (ArtificialTypeUnit) {
            if (Error Err =
                    CU.assignTypeNames(ArtificialTypeUnit->getTypePool()))
              return std::move(Err);
          }
          CU.setStage(CompileUnit::Stage::TypeNamesAssigned);
          break;

        case CompileUnit::Stage::TypeNamesAssigned:
          // Clone input compile unit.
          if (CU.isClangModule() ||
              GlobalData.getOptions().UpdateIndexTablesOnly ||
              CU.getContaingFile().Addresses->hasValidRelocs()) {
            if (Error Err = CU.cloneAndEmit(GlobalData.getTargetTriple(),
                                            ArtificialTypeUnit))
              return std::move(Err);
          }

          CU.setStage(CompileUnit::Stage::Cloned);
          break;

        case CompileUnit::Stage::Cloned:
          // Update DIEs referencies.
          CU.updateDieRefPatchesWithClonedOffsets();
          CU.setStage(CompileUnit::Stage::PatchesUpdated);
          break;

        case CompileUnit::Stage::PatchesUpdated:
          // Cleanup resources.
          CU.cleanupDataAfterClonning();
          CU.setStage(CompileUnit::Stage::Cleaned);
          break;

        case CompileUnit::Stage::Cleaned:
          assert(false);
          break;

        case CompileUnit::Stage::Skipped:
          // Nothing to do.
          break;
        }

        return true;
      })) {
    CU.error(std::move(Err));
    CU.cleanupDataAfterClonning();
    CU.setStage(CompileUnit::Stage::Skipped);
  }
}

Error DWARFLinkerImpl::LinkContext::emitInvariantSections() {
  if (!GlobalData.getTargetTriple().has_value())
    return Error::success();

  getOrCreateSectionDescriptor(DebugSectionKind::DebugLoc).OS
      << InputDWARFFile.Dwarf->getDWARFObj().getLocSection().Data;
  getOrCreateSectionDescriptor(DebugSectionKind::DebugLocLists).OS
      << InputDWARFFile.Dwarf->getDWARFObj().getLoclistsSection().Data;
  getOrCreateSectionDescriptor(DebugSectionKind::DebugRange).OS
      << InputDWARFFile.Dwarf->getDWARFObj().getRangesSection().Data;
  getOrCreateSectionDescriptor(DebugSectionKind::DebugRngLists).OS
      << InputDWARFFile.Dwarf->getDWARFObj().getRnglistsSection().Data;
  getOrCreateSectionDescriptor(DebugSectionKind::DebugARanges).OS
      << InputDWARFFile.Dwarf->getDWARFObj().getArangesSection();
  getOrCreateSectionDescriptor(DebugSectionKind::DebugFrame).OS
      << InputDWARFFile.Dwarf->getDWARFObj().getFrameSection().Data;
  getOrCreateSectionDescriptor(DebugSectionKind::DebugAddr).OS
      << InputDWARFFile.Dwarf->getDWARFObj().getAddrSection().Data;

  return Error::success();
}

Error DWARFLinkerImpl::LinkContext::cloneAndEmitDebugFrame() {
  if (!GlobalData.getTargetTriple().has_value())
    return Error::success();

  if (InputDWARFFile.Dwarf == nullptr)
    return Error::success();

  const DWARFObject &InputDWARFObj = InputDWARFFile.Dwarf->getDWARFObj();

  StringRef OrigFrameData = InputDWARFObj.getFrameSection().Data;
  if (OrigFrameData.empty())
    return Error::success();

  RangesTy AllUnitsRanges;
  for (std::unique_ptr<CompileUnit> &Unit : CompileUnits) {
    for (auto CurRange : Unit->getFunctionRanges())
      AllUnitsRanges.insert(CurRange.Range, CurRange.Value);
  }

  unsigned SrcAddrSize = InputDWARFObj.getAddressSize();

  SectionDescriptor &OutSection =
      getOrCreateSectionDescriptor(DebugSectionKind::DebugFrame);

  DataExtractor Data(OrigFrameData, InputDWARFObj.isLittleEndian(), 0);
  uint64_t InputOffset = 0;

  // Store the data of the CIEs defined in this object, keyed by their
  // offsets.
  DenseMap<uint64_t, StringRef> LocalCIES;

  /// The CIEs that have been emitted in the output section. The actual CIE
  /// data serves a the key to this StringMap.
  StringMap<uint32_t> EmittedCIEs;

  while (Data.isValidOffset(InputOffset)) {
    uint64_t EntryOffset = InputOffset;
    uint32_t InitialLength = Data.getU32(&InputOffset);
    if (InitialLength == 0xFFFFFFFF)
      return createFileError(InputDWARFObj.getFileName(),
                             createStringError(std::errc::invalid_argument,
                                               "Dwarf64 bits no supported"));

    uint32_t CIEId = Data.getU32(&InputOffset);
    if (CIEId == 0xFFFFFFFF) {
      // This is a CIE, store it.
      StringRef CIEData = OrigFrameData.substr(EntryOffset, InitialLength + 4);
      LocalCIES[EntryOffset] = CIEData;
      // The -4 is to account for the CIEId we just read.
      InputOffset += InitialLength - 4;
      continue;
    }

    uint64_t Loc = Data.getUnsigned(&InputOffset, SrcAddrSize);

    // Some compilers seem to emit frame info that doesn't start at
    // the function entry point, thus we can't just lookup the address
    // in the debug map. Use the AddressInfo's range map to see if the FDE
    // describes something that we can relocate.
    std::optional<AddressRangeValuePair> Range =
        AllUnitsRanges.getRangeThatContains(Loc);
    if (!Range) {
      // The +4 is to account for the size of the InitialLength field itself.
      InputOffset = EntryOffset + InitialLength + 4;
      continue;
    }

    // This is an FDE, and we have a mapping.
    // Have we already emitted a corresponding CIE?
    StringRef CIEData = LocalCIES[CIEId];
    if (CIEData.empty())
      return createFileError(
          InputDWARFObj.getFileName(),
          createStringError(std::errc::invalid_argument,
                            "Inconsistent debug_frame content. Dropping."));

    uint64_t OffsetToCIERecord = OutSection.OS.tell();

    // Look if we already emitted a CIE that corresponds to the
    // referenced one (the CIE data is the key of that lookup).
    auto IteratorInserted =
        EmittedCIEs.insert(std::make_pair(CIEData, OffsetToCIERecord));
    OffsetToCIERecord = IteratorInserted.first->getValue();

    // Emit CIE for this ID if it is not emitted yet.
    if (IteratorInserted.second)
      OutSection.OS << CIEData;

    // Remember offset to the FDE record, so that we might update
    // field referencing CIE record(containing OffsetToCIERecord),
    // when final offsets are known. OffsetToCIERecord(which is written later)
    // is local to the current .debug_frame section, it should be updated
    // with final offset of the .debug_frame section.
    OutSection.notePatch(
        DebugOffsetPatch{OutSection.OS.tell() + 4, &OutSection, true});

    // Emit the FDE with updated address and CIE pointer.
    // (4 + AddrSize) is the size of the CIEId + initial_location
    // fields that will get reconstructed by emitFDE().
    unsigned FDERemainingBytes = InitialLength - (4 + SrcAddrSize);
    emitFDE(OffsetToCIERecord, SrcAddrSize, Loc + Range->Value,
            OrigFrameData.substr(InputOffset, FDERemainingBytes), OutSection);
    InputOffset += FDERemainingBytes;
  }

  return Error::success();
}

/// Emit a FDE into the debug_frame section. \p FDEBytes
/// contains the FDE data without the length, CIE offset and address
/// which will be replaced with the parameter values.
void DWARFLinkerImpl::LinkContext::emitFDE(uint32_t CIEOffset,
                                           uint32_t AddrSize, uint64_t Address,
                                           StringRef FDEBytes,
                                           SectionDescriptor &Section) {
  Section.emitIntVal(FDEBytes.size() + 4 + AddrSize, 4);
  Section.emitIntVal(CIEOffset, 4);
  Section.emitIntVal(Address, AddrSize);
  Section.OS.write(FDEBytes.data(), FDEBytes.size());
}

void DWARFLinkerImpl::glueCompileUnitsAndWriteToTheOutput() {
  if (!GlobalData.getTargetTriple().has_value())
    return;
  assert(SectionHandler);

  // Go through all object files, all compile units and assign
  // offsets to them.
  assignOffsets();

  // Patch size/offsets fields according to the assigned CU offsets.
  patchOffsetsAndSizes();

  // Emit common sections and write debug tables from all object files/compile
  // units into the resulting file.
  emitCommonSectionsAndWriteCompileUnitsToTheOutput();

  if (ArtificialTypeUnit != nullptr)
    ArtificialTypeUnit.reset();

  // Write common debug sections into the resulting file.
  writeCommonSectionsToTheOutput();

  // Cleanup data.
  cleanupDataAfterDWARFOutputIsWritten();

  if (GlobalData.getOptions().Statistics)
    printStatistic();
}

void DWARFLinkerImpl::printStatistic() {

  // For each object file map how many bytes were emitted.
  StringMap<DebugInfoSize> SizeByObject;

  for (const std::unique_ptr<LinkContext> &Context : ObjectContexts) {
    uint64_t AllDebugInfoSectionsSize = 0;

    for (std::unique_ptr<CompileUnit> &CU : Context->CompileUnits)
      if (std::optional<SectionDescriptor *> DebugInfo =
              CU->tryGetSectionDescriptor(DebugSectionKind::DebugInfo))
        AllDebugInfoSectionsSize += (*DebugInfo)->getContents().size();

    SizeByObject[Context->InputDWARFFile.FileName].Input =
        Context->OriginalDebugInfoSize;
    SizeByObject[Context->InputDWARFFile.FileName].Output =
        AllDebugInfoSectionsSize;
  }

  // Create a vector sorted in descending order by output size.
  std::vector<std::pair<StringRef, DebugInfoSize>> Sorted;
  for (auto &E : SizeByObject)
    Sorted.emplace_back(E.first(), E.second);
  llvm::sort(Sorted, [](auto &LHS, auto &RHS) {
    return LHS.second.Output > RHS.second.Output;
  });

  auto ComputePercentange = [](int64_t Input, int64_t Output) -> float {
    const float Difference = Output - Input;
    const float Sum = Input + Output;
    if (Sum == 0)
      return 0;
    return (Difference / (Sum / 2));
  };

  int64_t InputTotal = 0;
  int64_t OutputTotal = 0;
  const char *FormatStr = "{0,-45} {1,10}b  {2,10}b {3,8:P}\n";

  // Print header.
  outs() << ".debug_info section size (in bytes)\n";
  outs() << "----------------------------------------------------------------"
            "---------------\n";
  outs() << "Filename                                           Object       "
            "  dSYM   Change\n";
  outs() << "----------------------------------------------------------------"
            "---------------\n";

  // Print body.
  for (auto &E : Sorted) {
    InputTotal += E.second.Input;
    OutputTotal += E.second.Output;
    llvm::outs() << formatv(
        FormatStr, sys::path::filename(E.first).take_back(45), E.second.Input,
        E.second.Output, ComputePercentange(E.second.Input, E.second.Output));
  }
  // Print total and footer.
  outs() << "----------------------------------------------------------------"
            "---------------\n";
  llvm::outs() << formatv(FormatStr, "Total", InputTotal, OutputTotal,
                          ComputePercentange(InputTotal, OutputTotal));
  outs() << "----------------------------------------------------------------"
            "---------------\n\n";
}

void DWARFLinkerImpl::assignOffsets() {
  llvm::parallel::TaskGroup TGroup;
  TGroup.spawn([&]() { assignOffsetsToStrings(); });
  TGroup.spawn([&]() { assignOffsetsToSections(); });
}

void DWARFLinkerImpl::assignOffsetsToStrings() {
  size_t CurDebugStrIndex = 1; // start from 1 to take into account zero entry.
  uint64_t CurDebugStrOffset =
      1; // start from 1 to take into account zero entry.
  size_t CurDebugLineStrIndex = 0;
  uint64_t CurDebugLineStrOffset = 0;

  // Enumerates all strings, add them into the DwarfStringPoolEntry map,
  // assign offset and index to the string if it is not indexed yet.
  forEachOutputString([&](StringDestinationKind Kind,
                          const StringEntry *String) {
    switch (Kind) {
    case StringDestinationKind::DebugStr: {
      DwarfStringPoolEntryWithExtString *Entry = DebugStrStrings.add(String);
      assert(Entry != nullptr);

      if (!Entry->isIndexed()) {
        Entry->Offset = CurDebugStrOffset;
        CurDebugStrOffset += Entry->String.size() + 1;
        Entry->Index = CurDebugStrIndex++;
      }
    } break;
    case StringDestinationKind::DebugLineStr: {
      DwarfStringPoolEntryWithExtString *Entry =
          DebugLineStrStrings.add(String);
      assert(Entry != nullptr);

      if (!Entry->isIndexed()) {
        Entry->Offset = CurDebugLineStrOffset;
        CurDebugLineStrOffset += Entry->String.size() + 1;
        Entry->Index = CurDebugLineStrIndex++;
      }
    } break;
    }
  });
}

void DWARFLinkerImpl::assignOffsetsToSections() {
  std::array<uint64_t, SectionKindsNum> SectionSizesAccumulator = {0};

  forEachObjectSectionsSet([&](OutputSections &UnitSections) {
    UnitSections.assignSectionsOffsetAndAccumulateSize(SectionSizesAccumulator);
  });
}

void DWARFLinkerImpl::forEachOutputString(
    function_ref<void(StringDestinationKind Kind, const StringEntry *String)>
        StringHandler) {
  // To save space we do not create any separate string table.
  // We use already allocated string patches and accelerator entries:
  // enumerate them in natural order and assign offsets.
  // ASSUMPTION: strings should be stored into .debug_str/.debug_line_str
  // sections in the same order as they were assigned offsets.
  forEachCompileUnit([&](CompileUnit *CU) {
    CU->forEach([&](SectionDescriptor &OutSection) {
      OutSection.ListDebugStrPatch.forEach([&](DebugStrPatch &Patch) {
        StringHandler(StringDestinationKind::DebugStr, Patch.String);
      });

      OutSection.ListDebugLineStrPatch.forEach([&](DebugLineStrPatch &Patch) {
        StringHandler(StringDestinationKind::DebugLineStr, Patch.String);
      });
    });

    CU->forEachAcceleratorRecord([&](DwarfUnit::AccelInfo &Info) {
      StringHandler(DebugStr, Info.String);
    });
  });

  if (ArtificialTypeUnit != nullptr) {
    ArtificialTypeUnit->forEach([&](SectionDescriptor &OutSection) {
      OutSection.ListDebugStrPatch.forEach([&](DebugStrPatch &Patch) {
        StringHandler(StringDestinationKind::DebugStr, Patch.String);
      });

      OutSection.ListDebugLineStrPatch.forEach([&](DebugLineStrPatch &Patch) {
        StringHandler(StringDestinationKind::DebugLineStr, Patch.String);
      });

      OutSection.ListDebugTypeStrPatch.forEach([&](DebugTypeStrPatch &Patch) {
        if (Patch.Die == nullptr)
          return;

        StringHandler(StringDestinationKind::DebugStr, Patch.String);
      });

      OutSection.ListDebugTypeLineStrPatch.forEach(
          [&](DebugTypeLineStrPatch &Patch) {
            if (Patch.Die == nullptr)
              return;

            StringHandler(StringDestinationKind::DebugStr, Patch.String);
          });
    });
  }
}

void DWARFLinkerImpl::forEachObjectSectionsSet(
    function_ref<void(OutputSections &)> SectionsSetHandler) {
  // Handle artificial type unit first.
  if (ArtificialTypeUnit != nullptr)
    SectionsSetHandler(*ArtificialTypeUnit);

  // Then all modules(before regular compilation units).
  for (const std::unique_ptr<LinkContext> &Context : ObjectContexts)
    for (LinkContext::RefModuleUnit &ModuleUnit : Context->ModulesCompileUnits)
      if (ModuleUnit.Unit->getStage() != CompileUnit::Stage::Skipped)
        SectionsSetHandler(*ModuleUnit.Unit);

  // Finally all compilation units.
  for (const std::unique_ptr<LinkContext> &Context : ObjectContexts) {
    // Handle object file common sections.
    SectionsSetHandler(*Context);

    // Handle compilation units.
    for (std::unique_ptr<CompileUnit> &CU : Context->CompileUnits)
      if (CU->getStage() != CompileUnit::Stage::Skipped)
        SectionsSetHandler(*CU);
  }
}

void DWARFLinkerImpl::forEachCompileAndTypeUnit(
    function_ref<void(DwarfUnit *CU)> UnitHandler) {
  if (ArtificialTypeUnit != nullptr)
    UnitHandler(ArtificialTypeUnit.get());

  // Enumerate module units.
  for (const std::unique_ptr<LinkContext> &Context : ObjectContexts)
    for (LinkContext::RefModuleUnit &ModuleUnit : Context->ModulesCompileUnits)
      if (ModuleUnit.Unit->getStage() != CompileUnit::Stage::Skipped)
        UnitHandler(ModuleUnit.Unit.get());

  // Enumerate compile units.
  for (const std::unique_ptr<LinkContext> &Context : ObjectContexts)
    for (std::unique_ptr<CompileUnit> &CU : Context->CompileUnits)
      if (CU->getStage() != CompileUnit::Stage::Skipped)
        UnitHandler(CU.get());
}

void DWARFLinkerImpl::forEachCompileUnit(
    function_ref<void(CompileUnit *CU)> UnitHandler) {
  // Enumerate module units.
  for (const std::unique_ptr<LinkContext> &Context : ObjectContexts)
    for (LinkContext::RefModuleUnit &ModuleUnit : Context->ModulesCompileUnits)
      if (ModuleUnit.Unit->getStage() != CompileUnit::Stage::Skipped)
        UnitHandler(ModuleUnit.Unit.get());

  // Enumerate compile units.
  for (const std::unique_ptr<LinkContext> &Context : ObjectContexts)
    for (std::unique_ptr<CompileUnit> &CU : Context->CompileUnits)
      if (CU->getStage() != CompileUnit::Stage::Skipped)
        UnitHandler(CU.get());
}

void DWARFLinkerImpl::patchOffsetsAndSizes() {
  forEachObjectSectionsSet([&](OutputSections &SectionsSet) {
    SectionsSet.forEach([&](SectionDescriptor &OutSection) {
      SectionsSet.applyPatches(OutSection, DebugStrStrings, DebugLineStrStrings,
                               ArtificialTypeUnit.get());
    });
  });
}

void DWARFLinkerImpl::emitCommonSectionsAndWriteCompileUnitsToTheOutput() {
  llvm::parallel::TaskGroup TG;

  // Create section descriptors ahead if they are not exist at the moment.
  // SectionDescriptors container is not thread safe. Thus we should be sure
  // that descriptors would not be created in following parallel tasks.

  CommonSections.getOrCreateSectionDescriptor(DebugSectionKind::DebugStr);
  CommonSections.getOrCreateSectionDescriptor(DebugSectionKind::DebugLineStr);

  if (llvm::is_contained(GlobalData.Options.AccelTables,
                         AccelTableKind::Apple)) {
    CommonSections.getOrCreateSectionDescriptor(DebugSectionKind::AppleNames);
    CommonSections.getOrCreateSectionDescriptor(
        DebugSectionKind::AppleNamespaces);
    CommonSections.getOrCreateSectionDescriptor(DebugSectionKind::AppleObjC);
    CommonSections.getOrCreateSectionDescriptor(DebugSectionKind::AppleTypes);
  }

  if (llvm::is_contained(GlobalData.Options.AccelTables,
                         AccelTableKind::DebugNames))
    CommonSections.getOrCreateSectionDescriptor(DebugSectionKind::DebugNames);

  // Emit .debug_str and .debug_line_str sections.
  TG.spawn([&]() { emitStringSections(); });

  if (llvm::is_contained(GlobalData.Options.AccelTables,
                         AccelTableKind::Apple)) {
    // Emit apple accelerator sections.
    TG.spawn([&]() {
      emitAppleAcceleratorSections((*GlobalData.getTargetTriple()).get());
    });
  }

  if (llvm::is_contained(GlobalData.Options.AccelTables,
                         AccelTableKind::DebugNames)) {
    // Emit .debug_names section.
    TG.spawn([&]() {
      emitDWARFv5DebugNamesSection((*GlobalData.getTargetTriple()).get());
    });
  }

  // Write compile units to the output file.
  TG.spawn([&]() { writeCompileUnitsToTheOutput(); });
}

void DWARFLinkerImpl::emitStringSections() {
  uint64_t DebugStrNextOffset = 0;
  uint64_t DebugLineStrNextOffset = 0;

  // Emit zero length string. Accelerator tables does not work correctly
  // if the first string is not zero length string.
  CommonSections.getSectionDescriptor(DebugSectionKind::DebugStr)
      .emitInplaceString("");
  DebugStrNextOffset++;

  forEachOutputString(
      [&](StringDestinationKind Kind, const StringEntry *String) {
        switch (Kind) {
        case StringDestinationKind::DebugStr: {
          DwarfStringPoolEntryWithExtString *StringToEmit =
              DebugStrStrings.getExistingEntry(String);
          assert(StringToEmit->isIndexed());

          // Strings may be repeated. Use accumulated DebugStrNextOffset
          // to understand whether corresponding string is already emitted.
          // Skip string if its offset less than accumulated offset.
          if (StringToEmit->Offset >= DebugStrNextOffset) {
            DebugStrNextOffset =
                StringToEmit->Offset + StringToEmit->String.size() + 1;
            // Emit the string itself.
            CommonSections.getSectionDescriptor(DebugSectionKind::DebugStr)
                .emitInplaceString(StringToEmit->String);
          }
        } break;
        case StringDestinationKind::DebugLineStr: {
          DwarfStringPoolEntryWithExtString *StringToEmit =
              DebugLineStrStrings.getExistingEntry(String);
          assert(StringToEmit->isIndexed());

          // Strings may be repeated. Use accumulated DebugLineStrStrings
          // to understand whether corresponding string is already emitted.
          // Skip string if its offset less than accumulated offset.
          if (StringToEmit->Offset >= DebugLineStrNextOffset) {
            DebugLineStrNextOffset =
                StringToEmit->Offset + StringToEmit->String.size() + 1;
            // Emit the string itself.
            CommonSections.getSectionDescriptor(DebugSectionKind::DebugLineStr)
                .emitInplaceString(StringToEmit->String);
          }
        } break;
        }
      });
}

void DWARFLinkerImpl::emitAppleAcceleratorSections(const Triple &TargetTriple) {
  AccelTable<AppleAccelTableStaticOffsetData> AppleNamespaces;
  AccelTable<AppleAccelTableStaticOffsetData> AppleNames;
  AccelTable<AppleAccelTableStaticOffsetData> AppleObjC;
  AccelTable<AppleAccelTableStaticTypeData> AppleTypes;

  forEachCompileAndTypeUnit([&](DwarfUnit *CU) {
    CU->forEachAcceleratorRecord([&](const DwarfUnit::AccelInfo &Info) {
      uint64_t OutOffset = Info.OutOffset;
      switch (Info.Type) {
      case DwarfUnit::AccelType::None: {
        llvm_unreachable("Unknown accelerator record");
      } break;
      case DwarfUnit::AccelType::Namespace: {
        AppleNamespaces.addName(
            *DebugStrStrings.getExistingEntry(Info.String),
            CU->getSectionDescriptor(DebugSectionKind::DebugInfo).StartOffset +
                OutOffset);
      } break;
      case DwarfUnit::AccelType::Name: {
        AppleNames.addName(
            *DebugStrStrings.getExistingEntry(Info.String),
            CU->getSectionDescriptor(DebugSectionKind::DebugInfo).StartOffset +
                OutOffset);
      } break;
      case DwarfUnit::AccelType::ObjC: {
        AppleObjC.addName(
            *DebugStrStrings.getExistingEntry(Info.String),
            CU->getSectionDescriptor(DebugSectionKind::DebugInfo).StartOffset +
                OutOffset);
      } break;
      case DwarfUnit::AccelType::Type: {
        AppleTypes.addName(
            *DebugStrStrings.getExistingEntry(Info.String),
            CU->getSectionDescriptor(DebugSectionKind::DebugInfo).StartOffset +
                OutOffset,
            Info.Tag,
            Info.ObjcClassImplementation ? dwarf::DW_FLAG_type_implementation
                                         : 0,
            Info.QualifiedNameHash);
      } break;
      }
    });
  });

  {
    // FIXME: we use AsmPrinter to emit accelerator sections.
    // It might be beneficial to directly emit accelerator data
    // to the raw_svector_ostream.
    SectionDescriptor &OutSection =
        CommonSections.getSectionDescriptor(DebugSectionKind::AppleNamespaces);
    DwarfEmitterImpl Emitter(DWARFLinker::OutputFileType::Object,
                             OutSection.OS);
    if (Error Err = Emitter.init(TargetTriple, "__DWARF")) {
      consumeError(std::move(Err));
      return;
    }

    // Emit table.
    Emitter.emitAppleNamespaces(AppleNamespaces);
    Emitter.finish();

    // Set start offset and size for output section.
    OutSection.setSizesForSectionCreatedByAsmPrinter();
  }

  {
    // FIXME: we use AsmPrinter to emit accelerator sections.
    // It might be beneficial to directly emit accelerator data
    // to the raw_svector_ostream.
    SectionDescriptor &OutSection =
        CommonSections.getSectionDescriptor(DebugSectionKind::AppleNames);
    DwarfEmitterImpl Emitter(DWARFLinker::OutputFileType::Object,
                             OutSection.OS);
    if (Error Err = Emitter.init(TargetTriple, "__DWARF")) {
      consumeError(std::move(Err));
      return;
    }

    // Emit table.
    Emitter.emitAppleNames(AppleNames);
    Emitter.finish();

    // Set start offset ans size for output section.
    OutSection.setSizesForSectionCreatedByAsmPrinter();
  }

  {
    // FIXME: we use AsmPrinter to emit accelerator sections.
    // It might be beneficial to directly emit accelerator data
    // to the raw_svector_ostream.
    SectionDescriptor &OutSection =
        CommonSections.getSectionDescriptor(DebugSectionKind::AppleObjC);
    DwarfEmitterImpl Emitter(DWARFLinker::OutputFileType::Object,
                             OutSection.OS);
    if (Error Err = Emitter.init(TargetTriple, "__DWARF")) {
      consumeError(std::move(Err));
      return;
    }

    // Emit table.
    Emitter.emitAppleObjc(AppleObjC);
    Emitter.finish();

    // Set start offset ans size for output section.
    OutSection.setSizesForSectionCreatedByAsmPrinter();
  }

  {
    // FIXME: we use AsmPrinter to emit accelerator sections.
    // It might be beneficial to directly emit accelerator data
    // to the raw_svector_ostream.
    SectionDescriptor &OutSection =
        CommonSections.getSectionDescriptor(DebugSectionKind::AppleTypes);
    DwarfEmitterImpl Emitter(DWARFLinker::OutputFileType::Object,
                             OutSection.OS);
    if (Error Err = Emitter.init(TargetTriple, "__DWARF")) {
      consumeError(std::move(Err));
      return;
    }

    // Emit table.
    Emitter.emitAppleTypes(AppleTypes);
    Emitter.finish();

    // Set start offset ans size for output section.
    OutSection.setSizesForSectionCreatedByAsmPrinter();
  }
}

void DWARFLinkerImpl::emitDWARFv5DebugNamesSection(const Triple &TargetTriple) {
  std::unique_ptr<DWARF5AccelTable> DebugNames;

  DebugNamesUnitsOffsets CompUnits;
  CompUnitIDToIdx CUidToIdx;

  unsigned Id = 0;

  forEachCompileAndTypeUnit([&](DwarfUnit *CU) {
    bool HasRecords = false;
    CU->forEachAcceleratorRecord([&](const DwarfUnit::AccelInfo &Info) {
      if (DebugNames == nullptr)
        DebugNames = std::make_unique<DWARF5AccelTable>();

      HasRecords = true;
      switch (Info.Type) {
      case DwarfUnit::AccelType::Name:
      case DwarfUnit::AccelType::Namespace:
      case DwarfUnit::AccelType::Type: {
        DebugNames->addName(*DebugStrStrings.getExistingEntry(Info.String),
                            Info.OutOffset, std::nullopt /*ParentDIEOffset*/,
                            Info.Tag, CU->getUniqueID(),
                            CU->getTag() == dwarf::DW_TAG_type_unit);
      } break;

      default:
        break; // Nothing to do.
      };
    });

    if (HasRecords) {
      CompUnits.push_back(
          CU->getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo)
              .StartOffset);
      CUidToIdx[CU->getUniqueID()] = Id++;
    }
  });

  if (DebugNames != nullptr) {
    // FIXME: we use AsmPrinter to emit accelerator sections.
    // It might be beneficial to directly emit accelerator data
    // to the raw_svector_ostream.
    SectionDescriptor &OutSection =
        CommonSections.getSectionDescriptor(DebugSectionKind::DebugNames);
    DwarfEmitterImpl Emitter(DWARFLinker::OutputFileType::Object,
                             OutSection.OS);
    if (Error Err = Emitter.init(TargetTriple, "__DWARF")) {
      consumeError(std::move(Err));
      return;
    }

    // Emit table.
    Emitter.emitDebugNames(*DebugNames, CompUnits, CUidToIdx);
    Emitter.finish();

    // Set start offset ans size for output section.
    OutSection.setSizesForSectionCreatedByAsmPrinter();
  }
}

void DWARFLinkerImpl::cleanupDataAfterDWARFOutputIsWritten() {
  GlobalData.getStringPool().clear();
  DebugStrStrings.clear();
  DebugLineStrStrings.clear();
}

void DWARFLinkerImpl::writeCompileUnitsToTheOutput() {
  // Enumerate all sections and store them into the final emitter.
  forEachObjectSectionsSet([&](OutputSections &Sections) {
    Sections.forEach([&](std::shared_ptr<SectionDescriptor> OutSection) {
      // Emit section content.
      SectionHandler(OutSection);
    });
  });
}

void DWARFLinkerImpl::writeCommonSectionsToTheOutput() {
  CommonSections.forEach([&](std::shared_ptr<SectionDescriptor> OutSection) {
    SectionHandler(OutSection);
  });
}
