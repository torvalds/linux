//===- elfnix_platform.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code required to load the rest of the ELF-on-*IX runtime.
//
//===----------------------------------------------------------------------===//

#include "elfnix_platform.h"
#include "common.h"
#include "compiler.h"
#include "error.h"
#include "wrapper_function_utils.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace __orc_rt;
using namespace __orc_rt::elfnix;

// Declare function tags for functions in the JIT process.
ORC_RT_JIT_DISPATCH_TAG(__orc_rt_elfnix_get_initializers_tag)
ORC_RT_JIT_DISPATCH_TAG(__orc_rt_elfnix_get_deinitializers_tag)
ORC_RT_JIT_DISPATCH_TAG(__orc_rt_elfnix_symbol_lookup_tag)

// eh-frame registration functions, made available via aliases
// installed by the Platform
extern "C" void __register_frame(const void *);
extern "C" void __deregister_frame(const void *);

extern "C" void
__unw_add_dynamic_eh_frame_section(const void *) ORC_RT_WEAK_IMPORT;
extern "C" void
__unw_remove_dynamic_eh_frame_section(const void *) ORC_RT_WEAK_IMPORT;

namespace {

Error validatePointerSectionExtent(const char *SectionName,
                                   const ExecutorAddrRange &SE) {
  if (SE.size() % sizeof(uintptr_t)) {
    std::ostringstream ErrMsg;
    ErrMsg << std::hex << "Size of " << SectionName << " 0x"
           << SE.Start.getValue() << " -- 0x" << SE.End.getValue()
           << " is not a pointer multiple";
    return make_error<StringError>(ErrMsg.str());
  }
  return Error::success();
}

Error runInitArray(const std::vector<ExecutorAddrRange> &InitArraySections,
                   const ELFNixJITDylibInitializers &MOJDIs) {

  for (const auto &ModInits : InitArraySections) {
    if (auto Err = validatePointerSectionExtent(".init_array", ModInits))
      return Err;

    using InitFunc = void (*)();
    for (auto *Init : ModInits.toSpan<InitFunc>())
      (*Init)();
  }

  return Error::success();
}

struct TLSInfoEntry {
  unsigned long Key = 0;
  unsigned long DataAddress = 0;
};

struct TLSDescriptor {
  void (*Resolver)(void *);
  TLSInfoEntry *InfoEntry;
};

class ELFNixPlatformRuntimeState {
private:
  struct AtExitEntry {
    void (*Func)(void *);
    void *Arg;
  };

  using AtExitsVector = std::vector<AtExitEntry>;

  struct PerJITDylibState {
    void *Header = nullptr;
    size_t RefCount = 0;
    bool AllowReinitialization = false;
    AtExitsVector AtExits;
  };

public:
  static void initialize(void *DSOHandle);
  static ELFNixPlatformRuntimeState &get();
  static void destroy();

  ELFNixPlatformRuntimeState(void *DSOHandle);

  // Delete copy and move constructors.
  ELFNixPlatformRuntimeState(const ELFNixPlatformRuntimeState &) = delete;
  ELFNixPlatformRuntimeState &
  operator=(const ELFNixPlatformRuntimeState &) = delete;
  ELFNixPlatformRuntimeState(ELFNixPlatformRuntimeState &&) = delete;
  ELFNixPlatformRuntimeState &operator=(ELFNixPlatformRuntimeState &&) = delete;

  Error registerObjectSections(ELFNixPerObjectSectionsToRegister POSR);
  Error deregisterObjectSections(ELFNixPerObjectSectionsToRegister POSR);

  const char *dlerror();
  void *dlopen(std::string_view Name, int Mode);
  int dlclose(void *DSOHandle);
  void *dlsym(void *DSOHandle, std::string_view Symbol);

  int registerAtExit(void (*F)(void *), void *Arg, void *DSOHandle);
  void runAtExits(void *DSOHandle);

  /// Returns the base address of the section containing ThreadData.
  Expected<std::pair<const char *, size_t>>
  getThreadDataSectionFor(const char *ThreadData);

  void *getPlatformJDDSOHandle() { return PlatformJDDSOHandle; }

private:
  PerJITDylibState *getJITDylibStateByHeaderAddr(void *DSOHandle);
  PerJITDylibState *getJITDylibStateByName(std::string_view Path);
  PerJITDylibState &
  getOrCreateJITDylibState(ELFNixJITDylibInitializers &MOJDIs);

  Error registerThreadDataSection(span<const char> ThreadDataSection);

  Expected<ExecutorAddr> lookupSymbolInJITDylib(void *DSOHandle,
                                                std::string_view Symbol);

  Expected<ELFNixJITDylibInitializerSequence>
  getJITDylibInitializersByName(std::string_view Path);
  Expected<void *> dlopenInitialize(std::string_view Path, int Mode);
  Error initializeJITDylib(ELFNixJITDylibInitializers &MOJDIs);

  static ELFNixPlatformRuntimeState *MOPS;

  void *PlatformJDDSOHandle;

  // Frame registration functions:
  void (*registerEHFrameSection)(const void *) = nullptr;
  void (*deregisterEHFrameSection)(const void *) = nullptr;

  // FIXME: Move to thread-state.
  std::string DLFcnError;

  std::recursive_mutex JDStatesMutex;
  std::unordered_map<void *, PerJITDylibState> JDStates;
  std::unordered_map<std::string, void *> JDNameToHeader;

  std::mutex ThreadDataSectionsMutex;
  std::map<const char *, size_t> ThreadDataSections;
};

ELFNixPlatformRuntimeState *ELFNixPlatformRuntimeState::MOPS = nullptr;

void ELFNixPlatformRuntimeState::initialize(void *DSOHandle) {
  assert(!MOPS && "ELFNixPlatformRuntimeState should be null");
  MOPS = new ELFNixPlatformRuntimeState(DSOHandle);
}

ELFNixPlatformRuntimeState &ELFNixPlatformRuntimeState::get() {
  assert(MOPS && "ELFNixPlatformRuntimeState not initialized");
  return *MOPS;
}

void ELFNixPlatformRuntimeState::destroy() {
  assert(MOPS && "ELFNixPlatformRuntimeState not initialized");
  delete MOPS;
}

ELFNixPlatformRuntimeState::ELFNixPlatformRuntimeState(void *DSOHandle)
    : PlatformJDDSOHandle(DSOHandle) {
  if (__unw_add_dynamic_eh_frame_section &&
      __unw_remove_dynamic_eh_frame_section) {
    registerEHFrameSection = __unw_add_dynamic_eh_frame_section;
    deregisterEHFrameSection = __unw_remove_dynamic_eh_frame_section;
  } else {
    registerEHFrameSection = __register_frame;
    deregisterEHFrameSection = __deregister_frame;
  }
}

Error ELFNixPlatformRuntimeState::registerObjectSections(
    ELFNixPerObjectSectionsToRegister POSR) {
  if (POSR.EHFrameSection.Start)
    registerEHFrameSection(POSR.EHFrameSection.Start.toPtr<const char *>());

  if (POSR.ThreadDataSection.Start) {
    if (auto Err = registerThreadDataSection(
            POSR.ThreadDataSection.toSpan<const char>()))
      return Err;
  }

  return Error::success();
}

Error ELFNixPlatformRuntimeState::deregisterObjectSections(
    ELFNixPerObjectSectionsToRegister POSR) {
  if (POSR.EHFrameSection.Start)
    deregisterEHFrameSection(POSR.EHFrameSection.Start.toPtr<const char *>());

  return Error::success();
}

const char *ELFNixPlatformRuntimeState::dlerror() { return DLFcnError.c_str(); }

void *ELFNixPlatformRuntimeState::dlopen(std::string_view Path, int Mode) {
  std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);

  // Use fast path if all JITDylibs are already loaded and don't require
  // re-running initializers.
  if (auto *JDS = getJITDylibStateByName(Path)) {
    if (!JDS->AllowReinitialization) {
      ++JDS->RefCount;
      return JDS->Header;
    }
  }

  auto H = dlopenInitialize(Path, Mode);
  if (!H) {
    DLFcnError = toString(H.takeError());
    return nullptr;
  }

  return *H;
}

int ELFNixPlatformRuntimeState::dlclose(void *DSOHandle) {
  runAtExits(DSOHandle);
  return 0;
}

void *ELFNixPlatformRuntimeState::dlsym(void *DSOHandle,
                                        std::string_view Symbol) {
  auto Addr = lookupSymbolInJITDylib(DSOHandle, Symbol);
  if (!Addr) {
    DLFcnError = toString(Addr.takeError());
    return 0;
  }

  return Addr->toPtr<void *>();
}

int ELFNixPlatformRuntimeState::registerAtExit(void (*F)(void *), void *Arg,
                                               void *DSOHandle) {
  // FIXME: Handle out-of-memory errors, returning -1 if OOM.
  std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);
  auto *JDS = getJITDylibStateByHeaderAddr(DSOHandle);
  assert(JDS && "JITDylib state not initialized");
  JDS->AtExits.push_back({F, Arg});
  return 0;
}

void ELFNixPlatformRuntimeState::runAtExits(void *DSOHandle) {
  // FIXME: Should atexits be allowed to run concurrently with access to
  // JDState?
  AtExitsVector V;
  {
    std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);
    auto *JDS = getJITDylibStateByHeaderAddr(DSOHandle);
    assert(JDS && "JITDlybi state not initialized");
    std::swap(V, JDS->AtExits);
  }

  while (!V.empty()) {
    auto &AE = V.back();
    AE.Func(AE.Arg);
    V.pop_back();
  }
}

Expected<std::pair<const char *, size_t>>
ELFNixPlatformRuntimeState::getThreadDataSectionFor(const char *ThreadData) {
  std::lock_guard<std::mutex> Lock(ThreadDataSectionsMutex);
  auto I = ThreadDataSections.upper_bound(ThreadData);
  // Check that we have a valid entry conovering this address.
  if (I == ThreadDataSections.begin())
    return make_error<StringError>("No thread local data section for key");
  I = std::prev(I);
  if (ThreadData >= I->first + I->second)
    return make_error<StringError>("No thread local data section for key");
  return *I;
}

ELFNixPlatformRuntimeState::PerJITDylibState *
ELFNixPlatformRuntimeState::getJITDylibStateByHeaderAddr(void *DSOHandle) {
  auto I = JDStates.find(DSOHandle);
  if (I == JDStates.end())
    return nullptr;
  return &I->second;
}

ELFNixPlatformRuntimeState::PerJITDylibState *
ELFNixPlatformRuntimeState::getJITDylibStateByName(std::string_view Name) {
  // FIXME: Avoid creating string copy here.
  auto I = JDNameToHeader.find(std::string(Name.data(), Name.size()));
  if (I == JDNameToHeader.end())
    return nullptr;
  void *H = I->second;
  auto J = JDStates.find(H);
  assert(J != JDStates.end() &&
         "JITDylib has name map entry but no header map entry");
  return &J->second;
}

ELFNixPlatformRuntimeState::PerJITDylibState &
ELFNixPlatformRuntimeState::getOrCreateJITDylibState(
    ELFNixJITDylibInitializers &MOJDIs) {
  void *Header = MOJDIs.DSOHandleAddress.toPtr<void *>();

  auto &JDS = JDStates[Header];

  // If this entry hasn't been created yet.
  if (!JDS.Header) {
    assert(!JDNameToHeader.count(MOJDIs.Name) &&
           "JITDylib has header map entry but no name map entry");
    JDNameToHeader[MOJDIs.Name] = Header;
    JDS.Header = Header;
  }

  return JDS;
}

Error ELFNixPlatformRuntimeState::registerThreadDataSection(
    span<const char> ThreadDataSection) {
  std::lock_guard<std::mutex> Lock(ThreadDataSectionsMutex);
  auto I = ThreadDataSections.upper_bound(ThreadDataSection.data());
  if (I != ThreadDataSections.begin()) {
    auto J = std::prev(I);
    if (J->first + J->second > ThreadDataSection.data())
      return make_error<StringError>("Overlapping .tdata sections");
  }
  ThreadDataSections.insert(
      I, std::make_pair(ThreadDataSection.data(), ThreadDataSection.size()));
  return Error::success();
}

Expected<ExecutorAddr>
ELFNixPlatformRuntimeState::lookupSymbolInJITDylib(void *DSOHandle,
                                                   std::string_view Sym) {
  Expected<ExecutorAddr> Result((ExecutorAddr()));
  if (auto Err = WrapperFunction<SPSExpected<SPSExecutorAddr>(
          SPSExecutorAddr, SPSString)>::call(&__orc_rt_elfnix_symbol_lookup_tag,
                                             Result,
                                             ExecutorAddr::fromPtr(DSOHandle),
                                             Sym))
    return std::move(Err);
  return Result;
}

Expected<ELFNixJITDylibInitializerSequence>
ELFNixPlatformRuntimeState::getJITDylibInitializersByName(
    std::string_view Path) {
  Expected<ELFNixJITDylibInitializerSequence> Result(
      (ELFNixJITDylibInitializerSequence()));
  std::string PathStr(Path.data(), Path.size());
  if (auto Err =
          WrapperFunction<SPSExpected<SPSELFNixJITDylibInitializerSequence>(
              SPSString)>::call(&__orc_rt_elfnix_get_initializers_tag, Result,
                                Path))
    return std::move(Err);
  return Result;
}

Expected<void *>
ELFNixPlatformRuntimeState::dlopenInitialize(std::string_view Path, int Mode) {
  // Either our JITDylib wasn't loaded, or it or one of its dependencies allows
  // reinitialization. We need to call in to the JIT to see if there's any new
  // work pending.
  auto InitSeq = getJITDylibInitializersByName(Path);
  if (!InitSeq)
    return InitSeq.takeError();

  // Init sequences should be non-empty.
  if (InitSeq->empty())
    return make_error<StringError>(
        "__orc_rt_elfnix_get_initializers returned an "
        "empty init sequence");

  // Otherwise register and run initializers for each JITDylib.
  for (auto &MOJDIs : *InitSeq)
    if (auto Err = initializeJITDylib(MOJDIs))
      return std::move(Err);

  // Return the header for the last item in the list.
  auto *JDS = getJITDylibStateByHeaderAddr(
      InitSeq->back().DSOHandleAddress.toPtr<void *>());
  assert(JDS && "Missing state entry for JD");
  return JDS->Header;
}

long getPriority(const std::string &name) {
  auto pos = name.find_last_not_of("0123456789");
  if (pos == name.size() - 1)
    return 65535;
  else
    return std::strtol(name.c_str() + pos + 1, nullptr, 10);
}

Error ELFNixPlatformRuntimeState::initializeJITDylib(
    ELFNixJITDylibInitializers &MOJDIs) {

  auto &JDS = getOrCreateJITDylibState(MOJDIs);
  ++JDS.RefCount;

  using SectionList = std::vector<ExecutorAddrRange>;
  std::sort(MOJDIs.InitSections.begin(), MOJDIs.InitSections.end(),
            [](const std::pair<std::string, SectionList> &LHS,
               const std::pair<std::string, SectionList> &RHS) -> bool {
              return getPriority(LHS.first) < getPriority(RHS.first);
            });
  for (auto &Entry : MOJDIs.InitSections)
    if (auto Err = runInitArray(Entry.second, MOJDIs))
      return Err;

  return Error::success();
}
class ELFNixPlatformRuntimeTLVManager {
public:
  void *getInstance(const char *ThreadData);

private:
  std::unordered_map<const char *, char *> Instances;
  std::unordered_map<const char *, std::unique_ptr<char[]>> AllocatedSections;
};

void *ELFNixPlatformRuntimeTLVManager::getInstance(const char *ThreadData) {
  auto I = Instances.find(ThreadData);
  if (I != Instances.end())
    return I->second;
  auto TDS =
      ELFNixPlatformRuntimeState::get().getThreadDataSectionFor(ThreadData);
  if (!TDS) {
    __orc_rt_log_error(toString(TDS.takeError()).c_str());
    return nullptr;
  }

  auto &Allocated = AllocatedSections[TDS->first];
  if (!Allocated) {
    Allocated = std::make_unique<char[]>(TDS->second);
    memcpy(Allocated.get(), TDS->first, TDS->second);
  }
  size_t ThreadDataDelta = ThreadData - TDS->first;
  assert(ThreadDataDelta <= TDS->second && "ThreadData outside section bounds");

  char *Instance = Allocated.get() + ThreadDataDelta;
  Instances[ThreadData] = Instance;
  return Instance;
}

void destroyELFNixTLVMgr(void *ELFNixTLVMgr) {
  delete static_cast<ELFNixPlatformRuntimeTLVManager *>(ELFNixTLVMgr);
}

} // end anonymous namespace

//------------------------------------------------------------------------------
//                             JIT entry points
//------------------------------------------------------------------------------

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_elfnix_platform_bootstrap(char *ArgData, size_t ArgSize) {
  return WrapperFunction<void(uint64_t)>::handle(
             ArgData, ArgSize,
             [](uint64_t &DSOHandle) {
               ELFNixPlatformRuntimeState::initialize(
                   reinterpret_cast<void *>(DSOHandle));
             })
      .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_elfnix_platform_shutdown(char *ArgData, size_t ArgSize) {
  ELFNixPlatformRuntimeState::destroy();
  return WrapperFunctionResult().release();
}

/// Wrapper function for registering metadata on a per-object basis.
ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_elfnix_register_object_sections(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError(SPSELFNixPerObjectSectionsToRegister)>::
      handle(ArgData, ArgSize,
             [](ELFNixPerObjectSectionsToRegister &POSR) {
               return ELFNixPlatformRuntimeState::get().registerObjectSections(
                   std::move(POSR));
             })
          .release();
}

/// Wrapper for releasing per-object metadat.
ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_elfnix_deregister_object_sections(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError(SPSELFNixPerObjectSectionsToRegister)>::
      handle(ArgData, ArgSize,
             [](ELFNixPerObjectSectionsToRegister &POSR) {
               return ELFNixPlatformRuntimeState::get()
                   .deregisterObjectSections(std::move(POSR));
             })
          .release();
}

//------------------------------------------------------------------------------
//                           TLV support
//------------------------------------------------------------------------------

ORC_RT_INTERFACE void *__orc_rt_elfnix_tls_get_addr_impl(TLSInfoEntry *D) {
  auto *TLVMgr = static_cast<ELFNixPlatformRuntimeTLVManager *>(
      pthread_getspecific(D->Key));
  if (!TLVMgr)
    TLVMgr = new ELFNixPlatformRuntimeTLVManager();
  if (pthread_setspecific(D->Key, TLVMgr)) {
    __orc_rt_log_error("Call to pthread_setspecific failed");
    return nullptr;
  }

  return TLVMgr->getInstance(
      reinterpret_cast<char *>(static_cast<uintptr_t>(D->DataAddress)));
}

ORC_RT_INTERFACE ptrdiff_t ___orc_rt_elfnix_tlsdesc_resolver_impl(
    TLSDescriptor *D, const char *ThreadPointer) {
  const char *TLVPtr = reinterpret_cast<const char *>(
      __orc_rt_elfnix_tls_get_addr_impl(D->InfoEntry));
  return TLVPtr - ThreadPointer;
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_elfnix_create_pthread_key(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSExpected<uint64_t>(void)>::handle(
             ArgData, ArgSize,
             []() -> Expected<uint64_t> {
               pthread_key_t Key;
               if (int Err = pthread_key_create(&Key, destroyELFNixTLVMgr)) {
                 __orc_rt_log_error("Call to pthread_key_create failed");
                 return make_error<StringError>(strerror(Err));
               }
               return static_cast<uint64_t>(Key);
             })
      .release();
}

//------------------------------------------------------------------------------
//                           cxa_atexit support
//------------------------------------------------------------------------------

int __orc_rt_elfnix_cxa_atexit(void (*func)(void *), void *arg,
                               void *dso_handle) {
  return ELFNixPlatformRuntimeState::get().registerAtExit(func, arg,
                                                          dso_handle);
}

int __orc_rt_elfnix_atexit(void (*func)(void *)) {
  auto &PlatformRTState = ELFNixPlatformRuntimeState::get();
  return ELFNixPlatformRuntimeState::get().registerAtExit(
      func, NULL, PlatformRTState.getPlatformJDDSOHandle());
}

void __orc_rt_elfnix_cxa_finalize(void *dso_handle) {
  ELFNixPlatformRuntimeState::get().runAtExits(dso_handle);
}

//------------------------------------------------------------------------------
//                        JIT'd dlfcn alternatives.
//------------------------------------------------------------------------------

const char *__orc_rt_elfnix_jit_dlerror() {
  return ELFNixPlatformRuntimeState::get().dlerror();
}

void *__orc_rt_elfnix_jit_dlopen(const char *path, int mode) {
  return ELFNixPlatformRuntimeState::get().dlopen(path, mode);
}

int __orc_rt_elfnix_jit_dlclose(void *dso_handle) {
  return ELFNixPlatformRuntimeState::get().dlclose(dso_handle);
}

void *__orc_rt_elfnix_jit_dlsym(void *dso_handle, const char *symbol) {
  return ELFNixPlatformRuntimeState::get().dlsym(dso_handle, symbol);
}

//------------------------------------------------------------------------------
//                             ELFNix Run Program
//------------------------------------------------------------------------------

ORC_RT_INTERFACE int64_t __orc_rt_elfnix_run_program(
    const char *JITDylibName, const char *EntrySymbolName, int argc,
    char *argv[]) {
  using MainTy = int (*)(int, char *[]);

  void *H = __orc_rt_elfnix_jit_dlopen(JITDylibName,
                                       __orc_rt::elfnix::ORC_RT_RTLD_LAZY);
  if (!H) {
    __orc_rt_log_error(__orc_rt_elfnix_jit_dlerror());
    return -1;
  }

  auto *Main =
      reinterpret_cast<MainTy>(__orc_rt_elfnix_jit_dlsym(H, EntrySymbolName));

  if (!Main) {
    __orc_rt_log_error(__orc_rt_elfnix_jit_dlerror());
    return -1;
  }

  int Result = Main(argc, argv);

  if (__orc_rt_elfnix_jit_dlclose(H) == -1)
    __orc_rt_log_error(__orc_rt_elfnix_jit_dlerror());

  return Result;
}
