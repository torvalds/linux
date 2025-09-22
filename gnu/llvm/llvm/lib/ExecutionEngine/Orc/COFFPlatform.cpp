//===------- COFFPlatform.cpp - Utilities for executing COFF in Orc -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/COFFPlatform.h"
#include "llvm/ExecutionEngine/Orc/DebugUtils.h"
#include "llvm/ExecutionEngine/Orc/LookupAndRecordAddrs.h"
#include "llvm/ExecutionEngine/Orc/ObjectFileInterface.h"
#include "llvm/ExecutionEngine/Orc/Shared/ObjectFormats.h"

#include "llvm/Object/COFF.h"

#include "llvm/ExecutionEngine/Orc/EPCDynamicLibrarySearchGenerator.h"

#include "llvm/ExecutionEngine/JITLink/x86_64.h"

#define DEBUG_TYPE "orc"

using namespace llvm;
using namespace llvm::orc;
using namespace llvm::orc::shared;

namespace llvm {
namespace orc {
namespace shared {

using SPSCOFFJITDylibDepInfo = SPSSequence<SPSExecutorAddr>;
using SPSCOFFJITDylibDepInfoMap =
    SPSSequence<SPSTuple<SPSExecutorAddr, SPSCOFFJITDylibDepInfo>>;
using SPSCOFFObjectSectionsMap =
    SPSSequence<SPSTuple<SPSString, SPSExecutorAddrRange>>;
using SPSCOFFRegisterObjectSectionsArgs =
    SPSArgList<SPSExecutorAddr, SPSCOFFObjectSectionsMap, bool>;
using SPSCOFFDeregisterObjectSectionsArgs =
    SPSArgList<SPSExecutorAddr, SPSCOFFObjectSectionsMap>;

} // namespace shared
} // namespace orc
} // namespace llvm
namespace {

class COFFHeaderMaterializationUnit : public MaterializationUnit {
public:
  COFFHeaderMaterializationUnit(COFFPlatform &CP,
                                const SymbolStringPtr &HeaderStartSymbol)
      : MaterializationUnit(createHeaderInterface(CP, HeaderStartSymbol)),
        CP(CP) {}

  StringRef getName() const override { return "COFFHeaderMU"; }

  void materialize(std::unique_ptr<MaterializationResponsibility> R) override {
    unsigned PointerSize;
    llvm::endianness Endianness;
    const auto &TT = CP.getExecutionSession().getTargetTriple();

    switch (TT.getArch()) {
    case Triple::x86_64:
      PointerSize = 8;
      Endianness = llvm::endianness::little;
      break;
    default:
      llvm_unreachable("Unrecognized architecture");
    }

    auto G = std::make_unique<jitlink::LinkGraph>(
        "<COFFHeaderMU>", TT, PointerSize, Endianness,
        jitlink::getGenericEdgeKindName);
    auto &HeaderSection = G->createSection("__header", MemProt::Read);
    auto &HeaderBlock = createHeaderBlock(*G, HeaderSection);

    // Init symbol is __ImageBase symbol.
    auto &ImageBaseSymbol = G->addDefinedSymbol(
        HeaderBlock, 0, *R->getInitializerSymbol(), HeaderBlock.getSize(),
        jitlink::Linkage::Strong, jitlink::Scope::Default, false, true);

    addImageBaseRelocationEdge(HeaderBlock, ImageBaseSymbol);

    CP.getObjectLinkingLayer().emit(std::move(R), std::move(G));
  }

  void discard(const JITDylib &JD, const SymbolStringPtr &Sym) override {}

private:
  struct HeaderSymbol {
    const char *Name;
    uint64_t Offset;
  };

  struct NTHeader {
    support::ulittle32_t PEMagic;
    object::coff_file_header FileHeader;
    struct PEHeader {
      object::pe32plus_header Header;
      object::data_directory DataDirectory[COFF::NUM_DATA_DIRECTORIES + 1];
    } OptionalHeader;
  };

  struct HeaderBlockContent {
    object::dos_header DOSHeader;
    COFFHeaderMaterializationUnit::NTHeader NTHeader;
  };

  static jitlink::Block &createHeaderBlock(jitlink::LinkGraph &G,
                                           jitlink::Section &HeaderSection) {
    HeaderBlockContent Hdr = {};

    // Set up magic
    Hdr.DOSHeader.Magic[0] = 'M';
    Hdr.DOSHeader.Magic[1] = 'Z';
    Hdr.DOSHeader.AddressOfNewExeHeader =
        offsetof(HeaderBlockContent, NTHeader);
    uint32_t PEMagic = *reinterpret_cast<const uint32_t *>(COFF::PEMagic);
    Hdr.NTHeader.PEMagic = PEMagic;
    Hdr.NTHeader.OptionalHeader.Header.Magic = COFF::PE32Header::PE32_PLUS;

    switch (G.getTargetTriple().getArch()) {
    case Triple::x86_64:
      Hdr.NTHeader.FileHeader.Machine = COFF::IMAGE_FILE_MACHINE_AMD64;
      break;
    default:
      llvm_unreachable("Unrecognized architecture");
    }

    auto HeaderContent = G.allocateContent(
        ArrayRef<char>(reinterpret_cast<const char *>(&Hdr), sizeof(Hdr)));

    return G.createContentBlock(HeaderSection, HeaderContent, ExecutorAddr(), 8,
                                0);
  }

  static void addImageBaseRelocationEdge(jitlink::Block &B,
                                         jitlink::Symbol &ImageBase) {
    auto ImageBaseOffset = offsetof(HeaderBlockContent, NTHeader) +
                           offsetof(NTHeader, OptionalHeader) +
                           offsetof(object::pe32plus_header, ImageBase);
    B.addEdge(jitlink::x86_64::Pointer64, ImageBaseOffset, ImageBase, 0);
  }

  static MaterializationUnit::Interface
  createHeaderInterface(COFFPlatform &MOP,
                        const SymbolStringPtr &HeaderStartSymbol) {
    SymbolFlagsMap HeaderSymbolFlags;

    HeaderSymbolFlags[HeaderStartSymbol] = JITSymbolFlags::Exported;

    return MaterializationUnit::Interface(std::move(HeaderSymbolFlags),
                                          HeaderStartSymbol);
  }

  COFFPlatform &CP;
};

} // end anonymous namespace

namespace llvm {
namespace orc {

Expected<std::unique_ptr<COFFPlatform>> COFFPlatform::Create(
    ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
    JITDylib &PlatformJD, std::unique_ptr<MemoryBuffer> OrcRuntimeArchiveBuffer,
    LoadDynamicLibrary LoadDynLibrary, bool StaticVCRuntime,
    const char *VCRuntimePath, std::optional<SymbolAliasMap> RuntimeAliases) {

  // If the target is not supported then bail out immediately.
  if (!supportedTarget(ES.getTargetTriple()))
    return make_error<StringError>("Unsupported COFFPlatform triple: " +
                                       ES.getTargetTriple().str(),
                                   inconvertibleErrorCode());

  auto &EPC = ES.getExecutorProcessControl();

  auto GeneratorArchive =
      object::Archive::create(OrcRuntimeArchiveBuffer->getMemBufferRef());
  if (!GeneratorArchive)
    return GeneratorArchive.takeError();

  auto OrcRuntimeArchiveGenerator = StaticLibraryDefinitionGenerator::Create(
      ObjLinkingLayer, nullptr, std::move(*GeneratorArchive));
  if (!OrcRuntimeArchiveGenerator)
    return OrcRuntimeArchiveGenerator.takeError();

  // We need a second instance of the archive (for now) for the Platform. We
  // can `cantFail` this call, since if it were going to fail it would have
  // failed above.
  auto RuntimeArchive = cantFail(
      object::Archive::create(OrcRuntimeArchiveBuffer->getMemBufferRef()));

  // Create default aliases if the caller didn't supply any.
  if (!RuntimeAliases)
    RuntimeAliases = standardPlatformAliases(ES);

  // Define the aliases.
  if (auto Err = PlatformJD.define(symbolAliases(std::move(*RuntimeAliases))))
    return std::move(Err);

  auto &HostFuncJD = ES.createBareJITDylib("$<PlatformRuntimeHostFuncJD>");

  // Add JIT-dispatch function support symbols.
  if (auto Err = HostFuncJD.define(
          absoluteSymbols({{ES.intern("__orc_rt_jit_dispatch"),
                            {EPC.getJITDispatchInfo().JITDispatchFunction,
                             JITSymbolFlags::Exported}},
                           {ES.intern("__orc_rt_jit_dispatch_ctx"),
                            {EPC.getJITDispatchInfo().JITDispatchContext,
                             JITSymbolFlags::Exported}}})))
    return std::move(Err);

  PlatformJD.addToLinkOrder(HostFuncJD);

  // Create the instance.
  Error Err = Error::success();
  auto P = std::unique_ptr<COFFPlatform>(new COFFPlatform(
      ES, ObjLinkingLayer, PlatformJD, std::move(*OrcRuntimeArchiveGenerator),
      std::move(OrcRuntimeArchiveBuffer), std::move(RuntimeArchive),
      std::move(LoadDynLibrary), StaticVCRuntime, VCRuntimePath, Err));
  if (Err)
    return std::move(Err);
  return std::move(P);
}

Expected<std::unique_ptr<COFFPlatform>>
COFFPlatform::Create(ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
                     JITDylib &PlatformJD, const char *OrcRuntimePath,
                     LoadDynamicLibrary LoadDynLibrary, bool StaticVCRuntime,
                     const char *VCRuntimePath,
                     std::optional<SymbolAliasMap> RuntimeAliases) {

  auto ArchiveBuffer = MemoryBuffer::getFile(OrcRuntimePath);
  if (!ArchiveBuffer)
    return createFileError(OrcRuntimePath, ArchiveBuffer.getError());

  return Create(ES, ObjLinkingLayer, PlatformJD, std::move(*ArchiveBuffer),
                std::move(LoadDynLibrary), StaticVCRuntime, VCRuntimePath,
                std::move(RuntimeAliases));
}

Expected<MemoryBufferRef> COFFPlatform::getPerJDObjectFile() {
  auto PerJDObj = OrcRuntimeArchive->findSym("__orc_rt_coff_per_jd_marker");
  if (!PerJDObj)
    return PerJDObj.takeError();

  if (!*PerJDObj)
    return make_error<StringError>("Could not find per jd object file",
                                   inconvertibleErrorCode());

  auto Buffer = (*PerJDObj)->getAsBinary();
  if (!Buffer)
    return Buffer.takeError();

  return (*Buffer)->getMemoryBufferRef();
}

static void addAliases(ExecutionSession &ES, SymbolAliasMap &Aliases,
                       ArrayRef<std::pair<const char *, const char *>> AL) {
  for (auto &KV : AL) {
    auto AliasName = ES.intern(KV.first);
    assert(!Aliases.count(AliasName) && "Duplicate symbol name in alias map");
    Aliases[std::move(AliasName)] = {ES.intern(KV.second),
                                     JITSymbolFlags::Exported};
  }
}

Error COFFPlatform::setupJITDylib(JITDylib &JD) {
  if (auto Err = JD.define(std::make_unique<COFFHeaderMaterializationUnit>(
          *this, COFFHeaderStartSymbol)))
    return Err;

  if (auto Err = ES.lookup({&JD}, COFFHeaderStartSymbol).takeError())
    return Err;

  // Define the CXX aliases.
  SymbolAliasMap CXXAliases;
  addAliases(ES, CXXAliases, requiredCXXAliases());
  if (auto Err = JD.define(symbolAliases(std::move(CXXAliases))))
    return Err;

  auto PerJDObj = getPerJDObjectFile();
  if (!PerJDObj)
    return PerJDObj.takeError();

  auto I = getObjectFileInterface(ES, *PerJDObj);
  if (!I)
    return I.takeError();

  if (auto Err = ObjLinkingLayer.add(
          JD, MemoryBuffer::getMemBuffer(*PerJDObj, false), std::move(*I)))
    return Err;

  if (!Bootstrapping) {
    auto ImportedLibs = StaticVCRuntime
                            ? VCRuntimeBootstrap->loadStaticVCRuntime(JD)
                            : VCRuntimeBootstrap->loadDynamicVCRuntime(JD);
    if (!ImportedLibs)
      return ImportedLibs.takeError();
    for (auto &Lib : *ImportedLibs)
      if (auto Err = LoadDynLibrary(JD, Lib))
        return Err;
    if (StaticVCRuntime)
      if (auto Err = VCRuntimeBootstrap->initializeStaticVCRuntime(JD))
        return Err;
  }

  JD.addGenerator(DLLImportDefinitionGenerator::Create(ES, ObjLinkingLayer));
  return Error::success();
}

Error COFFPlatform::teardownJITDylib(JITDylib &JD) {
  std::lock_guard<std::mutex> Lock(PlatformMutex);
  auto I = JITDylibToHeaderAddr.find(&JD);
  if (I != JITDylibToHeaderAddr.end()) {
    assert(HeaderAddrToJITDylib.count(I->second) &&
           "HeaderAddrToJITDylib missing entry");
    HeaderAddrToJITDylib.erase(I->second);
    JITDylibToHeaderAddr.erase(I);
  }
  return Error::success();
}

Error COFFPlatform::notifyAdding(ResourceTracker &RT,
                                 const MaterializationUnit &MU) {
  auto &JD = RT.getJITDylib();
  const auto &InitSym = MU.getInitializerSymbol();
  if (!InitSym)
    return Error::success();

  RegisteredInitSymbols[&JD].add(InitSym,
                                 SymbolLookupFlags::WeaklyReferencedSymbol);

  LLVM_DEBUG({
    dbgs() << "COFFPlatform: Registered init symbol " << *InitSym << " for MU "
           << MU.getName() << "\n";
  });
  return Error::success();
}

Error COFFPlatform::notifyRemoving(ResourceTracker &RT) {
  llvm_unreachable("Not supported yet");
}

SymbolAliasMap COFFPlatform::standardPlatformAliases(ExecutionSession &ES) {
  SymbolAliasMap Aliases;
  addAliases(ES, Aliases, standardRuntimeUtilityAliases());
  return Aliases;
}

ArrayRef<std::pair<const char *, const char *>>
COFFPlatform::requiredCXXAliases() {
  static const std::pair<const char *, const char *> RequiredCXXAliases[] = {
      {"_CxxThrowException", "__orc_rt_coff_cxx_throw_exception"},
      {"_onexit", "__orc_rt_coff_onexit_per_jd"},
      {"atexit", "__orc_rt_coff_atexit_per_jd"}};

  return ArrayRef<std::pair<const char *, const char *>>(RequiredCXXAliases);
}

ArrayRef<std::pair<const char *, const char *>>
COFFPlatform::standardRuntimeUtilityAliases() {
  static const std::pair<const char *, const char *>
      StandardRuntimeUtilityAliases[] = {
          {"__orc_rt_run_program", "__orc_rt_coff_run_program"},
          {"__orc_rt_jit_dlerror", "__orc_rt_coff_jit_dlerror"},
          {"__orc_rt_jit_dlopen", "__orc_rt_coff_jit_dlopen"},
          {"__orc_rt_jit_dlclose", "__orc_rt_coff_jit_dlclose"},
          {"__orc_rt_jit_dlsym", "__orc_rt_coff_jit_dlsym"},
          {"__orc_rt_log_error", "__orc_rt_log_error_to_stderr"}};

  return ArrayRef<std::pair<const char *, const char *>>(
      StandardRuntimeUtilityAliases);
}

bool COFFPlatform::supportedTarget(const Triple &TT) {
  switch (TT.getArch()) {
  case Triple::x86_64:
    return true;
  default:
    return false;
  }
}

COFFPlatform::COFFPlatform(
    ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
    JITDylib &PlatformJD,
    std::unique_ptr<StaticLibraryDefinitionGenerator> OrcRuntimeGenerator,
    std::unique_ptr<MemoryBuffer> OrcRuntimeArchiveBuffer,
    std::unique_ptr<object::Archive> OrcRuntimeArchive,
    LoadDynamicLibrary LoadDynLibrary, bool StaticVCRuntime,
    const char *VCRuntimePath, Error &Err)
    : ES(ES), ObjLinkingLayer(ObjLinkingLayer),
      LoadDynLibrary(std::move(LoadDynLibrary)),
      OrcRuntimeArchiveBuffer(std::move(OrcRuntimeArchiveBuffer)),
      OrcRuntimeArchive(std::move(OrcRuntimeArchive)),
      StaticVCRuntime(StaticVCRuntime),
      COFFHeaderStartSymbol(ES.intern("__ImageBase")) {
  ErrorAsOutParameter _(&Err);

  Bootstrapping.store(true);
  ObjLinkingLayer.addPlugin(std::make_unique<COFFPlatformPlugin>(*this));

  // Load vc runtime
  auto VCRT =
      COFFVCRuntimeBootstrapper::Create(ES, ObjLinkingLayer, VCRuntimePath);
  if (!VCRT) {
    Err = VCRT.takeError();
    return;
  }
  VCRuntimeBootstrap = std::move(*VCRT);

  for (auto &Lib : OrcRuntimeGenerator->getImportedDynamicLibraries())
    DylibsToPreload.insert(Lib);

  auto ImportedLibs =
      StaticVCRuntime ? VCRuntimeBootstrap->loadStaticVCRuntime(PlatformJD)
                      : VCRuntimeBootstrap->loadDynamicVCRuntime(PlatformJD);
  if (!ImportedLibs) {
    Err = ImportedLibs.takeError();
    return;
  }

  for (auto &Lib : *ImportedLibs)
    DylibsToPreload.insert(Lib);

  PlatformJD.addGenerator(std::move(OrcRuntimeGenerator));

  // PlatformJD hasn't been set up by the platform yet (since we're creating
  // the platform now), so set it up.
  if (auto E2 = setupJITDylib(PlatformJD)) {
    Err = std::move(E2);
    return;
  }

  for (auto& Lib : DylibsToPreload)
    if (auto E2 = this->LoadDynLibrary(PlatformJD, Lib)) {
      Err = std::move(E2);
      return;
    }

  if (StaticVCRuntime)
      if (auto E2 = VCRuntimeBootstrap->initializeStaticVCRuntime(PlatformJD)) {
          Err = std::move(E2);
          return;
      }

  // Associate wrapper function tags with JIT-side function implementations.
  if (auto E2 = associateRuntimeSupportFunctions(PlatformJD)) {
      Err = std::move(E2);
      return;
  }

  // Lookup addresses of runtime functions callable by the platform,
  // call the platform bootstrap function to initialize the platform-state
  // object in the executor.
  if (auto E2 = bootstrapCOFFRuntime(PlatformJD)) {
      Err = std::move(E2);
      return;
  }

  Bootstrapping.store(false);
  JDBootstrapStates.clear();
}

Expected<COFFPlatform::JITDylibDepMap>
COFFPlatform::buildJDDepMap(JITDylib &JD) {
  return ES.runSessionLocked([&]() -> Expected<JITDylibDepMap> {
    JITDylibDepMap JDDepMap;

    SmallVector<JITDylib *, 16> Worklist({&JD});
    while (!Worklist.empty()) {
      auto CurJD = Worklist.back();
      Worklist.pop_back();

      auto &DM = JDDepMap[CurJD];
      CurJD->withLinkOrderDo([&](const JITDylibSearchOrder &O) {
        DM.reserve(O.size());
        for (auto &KV : O) {
          if (KV.first == CurJD)
            continue;
          {
            // Bare jitdylibs not known to the platform
            std::lock_guard<std::mutex> Lock(PlatformMutex);
            if (!JITDylibToHeaderAddr.count(KV.first)) {
              LLVM_DEBUG({
                dbgs() << "JITDylib unregistered to COFFPlatform detected in "
                          "LinkOrder: "
                       << CurJD->getName() << "\n";
              });
              continue;
            }
          }
          DM.push_back(KV.first);
          // Push unvisited entry.
          if (!JDDepMap.count(KV.first)) {
            Worklist.push_back(KV.first);
            JDDepMap[KV.first] = {};
          }
        }
      });
    }
    return std::move(JDDepMap);
  });
}

void COFFPlatform::pushInitializersLoop(PushInitializersSendResultFn SendResult,
                                        JITDylibSP JD,
                                        JITDylibDepMap &JDDepMap) {
  SmallVector<JITDylib *, 16> Worklist({JD.get()});
  DenseSet<JITDylib *> Visited({JD.get()});
  DenseMap<JITDylib *, SymbolLookupSet> NewInitSymbols;
  ES.runSessionLocked([&]() {
    while (!Worklist.empty()) {
      auto CurJD = Worklist.back();
      Worklist.pop_back();

      auto RISItr = RegisteredInitSymbols.find(CurJD);
      if (RISItr != RegisteredInitSymbols.end()) {
        NewInitSymbols[CurJD] = std::move(RISItr->second);
        RegisteredInitSymbols.erase(RISItr);
      }

      for (auto *DepJD : JDDepMap[CurJD])
        if (!Visited.count(DepJD)) {
          Worklist.push_back(DepJD);
          Visited.insert(DepJD);
        }
    }
  });

  // If there are no further init symbols to look up then send the link order
  // (as a list of header addresses) to the caller.
  if (NewInitSymbols.empty()) {
    // Build the dep info map to return.
    COFFJITDylibDepInfoMap DIM;
    DIM.reserve(JDDepMap.size());
    for (auto &KV : JDDepMap) {
      std::lock_guard<std::mutex> Lock(PlatformMutex);
      COFFJITDylibDepInfo DepInfo;
      DepInfo.reserve(KV.second.size());
      for (auto &Dep : KV.second) {
        DepInfo.push_back(JITDylibToHeaderAddr[Dep]);
      }
      auto H = JITDylibToHeaderAddr[KV.first];
      DIM.push_back(std::make_pair(H, std::move(DepInfo)));
    }
    SendResult(DIM);
    return;
  }

  // Otherwise issue a lookup and re-run this phase when it completes.
  lookupInitSymbolsAsync(
      [this, SendResult = std::move(SendResult), &JD,
       JDDepMap = std::move(JDDepMap)](Error Err) mutable {
        if (Err)
          SendResult(std::move(Err));
        else
          pushInitializersLoop(std::move(SendResult), JD, JDDepMap);
      },
      ES, std::move(NewInitSymbols));
}

void COFFPlatform::rt_pushInitializers(PushInitializersSendResultFn SendResult,
                                       ExecutorAddr JDHeaderAddr) {
  JITDylibSP JD;
  {
    std::lock_guard<std::mutex> Lock(PlatformMutex);
    auto I = HeaderAddrToJITDylib.find(JDHeaderAddr);
    if (I != HeaderAddrToJITDylib.end())
      JD = I->second;
  }

  LLVM_DEBUG({
    dbgs() << "COFFPlatform::rt_pushInitializers(" << JDHeaderAddr << ") ";
    if (JD)
      dbgs() << "pushing initializers for " << JD->getName() << "\n";
    else
      dbgs() << "No JITDylib for header address.\n";
  });

  if (!JD) {
    SendResult(make_error<StringError>("No JITDylib with header addr " +
                                           formatv("{0:x}", JDHeaderAddr),
                                       inconvertibleErrorCode()));
    return;
  }

  auto JDDepMap = buildJDDepMap(*JD);
  if (!JDDepMap) {
    SendResult(JDDepMap.takeError());
    return;
  }

  pushInitializersLoop(std::move(SendResult), JD, *JDDepMap);
}

void COFFPlatform::rt_lookupSymbol(SendSymbolAddressFn SendResult,
                                   ExecutorAddr Handle, StringRef SymbolName) {
  LLVM_DEBUG(dbgs() << "COFFPlatform::rt_lookupSymbol(\"" << Handle << "\")\n");

  JITDylib *JD = nullptr;

  {
    std::lock_guard<std::mutex> Lock(PlatformMutex);
    auto I = HeaderAddrToJITDylib.find(Handle);
    if (I != HeaderAddrToJITDylib.end())
      JD = I->second;
  }

  if (!JD) {
    LLVM_DEBUG(dbgs() << "  No JITDylib for handle " << Handle << "\n");
    SendResult(make_error<StringError>("No JITDylib associated with handle " +
                                           formatv("{0:x}", Handle),
                                       inconvertibleErrorCode()));
    return;
  }

  // Use functor class to work around XL build compiler issue on AIX.
  class RtLookupNotifyComplete {
  public:
    RtLookupNotifyComplete(SendSymbolAddressFn &&SendResult)
        : SendResult(std::move(SendResult)) {}
    void operator()(Expected<SymbolMap> Result) {
      if (Result) {
        assert(Result->size() == 1 && "Unexpected result map count");
        SendResult(Result->begin()->second.getAddress());
      } else {
        SendResult(Result.takeError());
      }
    }

  private:
    SendSymbolAddressFn SendResult;
  };

  ES.lookup(
      LookupKind::DLSym, {{JD, JITDylibLookupFlags::MatchExportedSymbolsOnly}},
      SymbolLookupSet(ES.intern(SymbolName)), SymbolState::Ready,
      RtLookupNotifyComplete(std::move(SendResult)), NoDependenciesToRegister);
}

Error COFFPlatform::associateRuntimeSupportFunctions(JITDylib &PlatformJD) {
  ExecutionSession::JITDispatchHandlerAssociationMap WFs;

  using LookupSymbolSPSSig =
      SPSExpected<SPSExecutorAddr>(SPSExecutorAddr, SPSString);
  WFs[ES.intern("__orc_rt_coff_symbol_lookup_tag")] =
      ES.wrapAsyncWithSPS<LookupSymbolSPSSig>(this,
                                              &COFFPlatform::rt_lookupSymbol);
  using PushInitializersSPSSig =
      SPSExpected<SPSCOFFJITDylibDepInfoMap>(SPSExecutorAddr);
  WFs[ES.intern("__orc_rt_coff_push_initializers_tag")] =
      ES.wrapAsyncWithSPS<PushInitializersSPSSig>(
          this, &COFFPlatform::rt_pushInitializers);

  return ES.registerJITDispatchHandlers(PlatformJD, std::move(WFs));
}

Error COFFPlatform::runBootstrapInitializers(JDBootstrapState &BState) {
  llvm::sort(BState.Initializers);
  if (auto Err =
          runBootstrapSubsectionInitializers(BState, ".CRT$XIA", ".CRT$XIZ"))
    return Err;

  if (auto Err = runSymbolIfExists(*BState.JD, "__run_after_c_init"))
    return Err;

  if (auto Err =
          runBootstrapSubsectionInitializers(BState, ".CRT$XCA", ".CRT$XCZ"))
    return Err;
  return Error::success();
}

Error COFFPlatform::runBootstrapSubsectionInitializers(JDBootstrapState &BState,
                                                       StringRef Start,
                                                       StringRef End) {
  for (auto &Initializer : BState.Initializers)
    if (Initializer.first >= Start && Initializer.first <= End &&
        Initializer.second) {
      auto Res =
          ES.getExecutorProcessControl().runAsVoidFunction(Initializer.second);
      if (!Res)
        return Res.takeError();
    }
  return Error::success();
}

Error COFFPlatform::bootstrapCOFFRuntime(JITDylib &PlatformJD) {
  // Lookup of runtime symbols causes the collection of initializers if
  // it's static linking setting.
  if (auto Err = lookupAndRecordAddrs(
          ES, LookupKind::Static, makeJITDylibSearchOrder(&PlatformJD),
          {
              {ES.intern("__orc_rt_coff_platform_bootstrap"),
               &orc_rt_coff_platform_bootstrap},
              {ES.intern("__orc_rt_coff_platform_shutdown"),
               &orc_rt_coff_platform_shutdown},
              {ES.intern("__orc_rt_coff_register_jitdylib"),
               &orc_rt_coff_register_jitdylib},
              {ES.intern("__orc_rt_coff_deregister_jitdylib"),
               &orc_rt_coff_deregister_jitdylib},
              {ES.intern("__orc_rt_coff_register_object_sections"),
               &orc_rt_coff_register_object_sections},
              {ES.intern("__orc_rt_coff_deregister_object_sections"),
               &orc_rt_coff_deregister_object_sections},
          }))
    return Err;

  // Call bootstrap functions
  if (auto Err = ES.callSPSWrapper<void()>(orc_rt_coff_platform_bootstrap))
    return Err;

  // Do the pending jitdylib registration actions that we couldn't do
  // because orc runtime was not linked fully.
  for (auto KV : JDBootstrapStates) {
    auto &JDBState = KV.second;
    if (auto Err = ES.callSPSWrapper<void(SPSString, SPSExecutorAddr)>(
            orc_rt_coff_register_jitdylib, JDBState.JDName,
            JDBState.HeaderAddr))
      return Err;

    for (auto &ObjSectionMap : JDBState.ObjectSectionsMaps)
      if (auto Err = ES.callSPSWrapper<void(SPSExecutorAddr,
                                            SPSCOFFObjectSectionsMap, bool)>(
              orc_rt_coff_register_object_sections, JDBState.HeaderAddr,
              ObjSectionMap, false))
        return Err;
  }

  // Run static initializers collected in bootstrap stage.
  for (auto KV : JDBootstrapStates) {
    auto &JDBState = KV.second;
    if (auto Err = runBootstrapInitializers(JDBState))
      return Err;
  }

  return Error::success();
}

Error COFFPlatform::runSymbolIfExists(JITDylib &PlatformJD,
                                      StringRef SymbolName) {
  ExecutorAddr jit_function;
  auto AfterCLookupErr = lookupAndRecordAddrs(
      ES, LookupKind::Static, makeJITDylibSearchOrder(&PlatformJD),
      {{ES.intern(SymbolName), &jit_function}});
  if (!AfterCLookupErr) {
    auto Res = ES.getExecutorProcessControl().runAsVoidFunction(jit_function);
    if (!Res)
      return Res.takeError();
    return Error::success();
  }
  if (!AfterCLookupErr.isA<SymbolsNotFound>())
    return AfterCLookupErr;
  consumeError(std::move(AfterCLookupErr));
  return Error::success();
}

void COFFPlatform::COFFPlatformPlugin::modifyPassConfig(
    MaterializationResponsibility &MR, jitlink::LinkGraph &LG,
    jitlink::PassConfiguration &Config) {

  bool IsBootstrapping = CP.Bootstrapping.load();

  if (auto InitSymbol = MR.getInitializerSymbol()) {
    if (InitSymbol == CP.COFFHeaderStartSymbol) {
      Config.PostAllocationPasses.push_back(
          [this, &MR, IsBootstrapping](jitlink::LinkGraph &G) {
            return associateJITDylibHeaderSymbol(G, MR, IsBootstrapping);
          });
      return;
    }
    Config.PrePrunePasses.push_back([this, &MR](jitlink::LinkGraph &G) {
      return preserveInitializerSections(G, MR);
    });
  }

  if (!IsBootstrapping)
    Config.PostFixupPasses.push_back(
        [this, &JD = MR.getTargetJITDylib()](jitlink::LinkGraph &G) {
          return registerObjectPlatformSections(G, JD);
        });
  else
    Config.PostFixupPasses.push_back(
        [this, &JD = MR.getTargetJITDylib()](jitlink::LinkGraph &G) {
          return registerObjectPlatformSectionsInBootstrap(G, JD);
        });
}

ObjectLinkingLayer::Plugin::SyntheticSymbolDependenciesMap
COFFPlatform::COFFPlatformPlugin::getSyntheticSymbolDependencies(
    MaterializationResponsibility &MR) {
  std::lock_guard<std::mutex> Lock(PluginMutex);
  auto I = InitSymbolDeps.find(&MR);
  if (I != InitSymbolDeps.end()) {
    SyntheticSymbolDependenciesMap Result;
    Result[MR.getInitializerSymbol()] = std::move(I->second);
    InitSymbolDeps.erase(&MR);
    return Result;
  }
  return SyntheticSymbolDependenciesMap();
}

Error COFFPlatform::COFFPlatformPlugin::associateJITDylibHeaderSymbol(
    jitlink::LinkGraph &G, MaterializationResponsibility &MR,
    bool IsBootstraping) {
  auto I = llvm::find_if(G.defined_symbols(), [this](jitlink::Symbol *Sym) {
    return Sym->getName() == *CP.COFFHeaderStartSymbol;
  });
  assert(I != G.defined_symbols().end() && "Missing COFF header start symbol");

  auto &JD = MR.getTargetJITDylib();
  std::lock_guard<std::mutex> Lock(CP.PlatformMutex);
  auto HeaderAddr = (*I)->getAddress();
  CP.JITDylibToHeaderAddr[&JD] = HeaderAddr;
  CP.HeaderAddrToJITDylib[HeaderAddr] = &JD;
  if (!IsBootstraping) {
    G.allocActions().push_back(
        {cantFail(WrapperFunctionCall::Create<
                  SPSArgList<SPSString, SPSExecutorAddr>>(
             CP.orc_rt_coff_register_jitdylib, JD.getName(), HeaderAddr)),
         cantFail(WrapperFunctionCall::Create<SPSArgList<SPSExecutorAddr>>(
             CP.orc_rt_coff_deregister_jitdylib, HeaderAddr))});
  } else {
    G.allocActions().push_back(
        {{},
         cantFail(WrapperFunctionCall::Create<SPSArgList<SPSExecutorAddr>>(
             CP.orc_rt_coff_deregister_jitdylib, HeaderAddr))});
    JDBootstrapState BState;
    BState.JD = &JD;
    BState.JDName = JD.getName();
    BState.HeaderAddr = HeaderAddr;
    CP.JDBootstrapStates.emplace(&JD, BState);
  }

  return Error::success();
}

Error COFFPlatform::COFFPlatformPlugin::registerObjectPlatformSections(
    jitlink::LinkGraph &G, JITDylib &JD) {
  COFFObjectSectionsMap ObjSecs;
  auto HeaderAddr = CP.JITDylibToHeaderAddr[&JD];
  assert(HeaderAddr && "Must be registered jitdylib");
  for (auto &S : G.sections()) {
    jitlink::SectionRange Range(S);
    if (Range.getSize())
      ObjSecs.push_back(std::make_pair(S.getName().str(), Range.getRange()));
  }

  G.allocActions().push_back(
      {cantFail(WrapperFunctionCall::Create<SPSCOFFRegisterObjectSectionsArgs>(
           CP.orc_rt_coff_register_object_sections, HeaderAddr, ObjSecs, true)),
       cantFail(
           WrapperFunctionCall::Create<SPSCOFFDeregisterObjectSectionsArgs>(
               CP.orc_rt_coff_deregister_object_sections, HeaderAddr,
               ObjSecs))});

  return Error::success();
}

Error COFFPlatform::COFFPlatformPlugin::preserveInitializerSections(
    jitlink::LinkGraph &G, MaterializationResponsibility &MR) {
  JITLinkSymbolSet InitSectionSymbols;
  for (auto &Sec : G.sections())
    if (isCOFFInitializerSection(Sec.getName()))
      for (auto *B : Sec.blocks())
        if (!B->edges_empty())
          InitSectionSymbols.insert(
              &G.addAnonymousSymbol(*B, 0, 0, false, true));

  std::lock_guard<std::mutex> Lock(PluginMutex);
  InitSymbolDeps[&MR] = InitSectionSymbols;
  return Error::success();
}

Error COFFPlatform::COFFPlatformPlugin::
    registerObjectPlatformSectionsInBootstrap(jitlink::LinkGraph &G,
                                              JITDylib &JD) {
  std::lock_guard<std::mutex> Lock(CP.PlatformMutex);
  auto HeaderAddr = CP.JITDylibToHeaderAddr[&JD];
  COFFObjectSectionsMap ObjSecs;
  for (auto &S : G.sections()) {
    jitlink::SectionRange Range(S);
    if (Range.getSize())
      ObjSecs.push_back(std::make_pair(S.getName().str(), Range.getRange()));
  }

  G.allocActions().push_back(
      {{},
       cantFail(
           WrapperFunctionCall::Create<SPSCOFFDeregisterObjectSectionsArgs>(
               CP.orc_rt_coff_deregister_object_sections, HeaderAddr,
               ObjSecs))});

  auto &BState = CP.JDBootstrapStates[&JD];
  BState.ObjectSectionsMaps.push_back(std::move(ObjSecs));

  // Collect static initializers
  for (auto &S : G.sections())
    if (isCOFFInitializerSection(S.getName()))
      for (auto *B : S.blocks()) {
        if (B->edges_empty())
          continue;
        for (auto &E : B->edges())
          BState.Initializers.push_back(std::make_pair(
              S.getName().str(), E.getTarget().getAddress() + E.getAddend()));
      }

  return Error::success();
}

} // End namespace orc.
} // End namespace llvm.
