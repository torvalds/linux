//===- coff_platform.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code required to load the rest of the COFF runtime.
//
//===----------------------------------------------------------------------===//

#define NOMINMAX
#include <windows.h>

#include "coff_platform.h"

#include "debug.h"
#include "error.h"
#include "wrapper_function_utils.h"

#include <array>
#include <list>
#include <map>
#include <mutex>
#include <sstream>
#include <string_view>
#include <vector>

#define DEBUG_TYPE "coff_platform"

using namespace __orc_rt;

namespace __orc_rt {

using COFFJITDylibDepInfo = std::vector<ExecutorAddr>;
using COFFJITDylibDepInfoMap =
    std::unordered_map<ExecutorAddr, COFFJITDylibDepInfo>;

using SPSCOFFObjectSectionsMap =
    SPSSequence<SPSTuple<SPSString, SPSExecutorAddrRange>>;

using SPSCOFFJITDylibDepInfo = SPSSequence<SPSExecutorAddr>;

using SPSCOFFJITDylibDepInfoMap =
    SPSSequence<SPSTuple<SPSExecutorAddr, SPSCOFFJITDylibDepInfo>>;

} // namespace __orc_rt

ORC_RT_JIT_DISPATCH_TAG(__orc_rt_coff_symbol_lookup_tag)
ORC_RT_JIT_DISPATCH_TAG(__orc_rt_coff_push_initializers_tag)

namespace {
class COFFPlatformRuntimeState {
private:
  // Ctor/dtor section.
  // Manage lists of *tor functions sorted by the last character of subsection
  // name.
  struct XtorSection {
    void Register(char SubsectionChar, span<void (*)(void)> Xtors) {
      Subsections[SubsectionChar - 'A'].push_back(Xtors);
      SubsectionsNew[SubsectionChar - 'A'].push_back(Xtors);
    }

    void RegisterNoRun(char SubsectionChar, span<void (*)(void)> Xtors) {
      Subsections[SubsectionChar - 'A'].push_back(Xtors);
    }

    void Reset() { SubsectionsNew = Subsections; }

    void RunAllNewAndFlush();

  private:
    std::array<std::vector<span<void (*)(void)>>, 26> Subsections;
    std::array<std::vector<span<void (*)(void)>>, 26> SubsectionsNew;
  };

  struct JITDylibState {
    std::string Name;
    void *Header = nullptr;
    size_t LinkedAgainstRefCount = 0;
    size_t DlRefCount = 0;
    std::vector<JITDylibState *> Deps;
    std::vector<void (*)(void)> AtExits;
    XtorSection CInitSection;    // XIA~XIZ
    XtorSection CXXInitSection;  // XCA~XCZ
    XtorSection CPreTermSection; // XPA~XPZ
    XtorSection CTermSection;    // XTA~XTZ

    bool referenced() const {
      return LinkedAgainstRefCount != 0 || DlRefCount != 0;
    }
  };

public:
  static void initialize();
  static COFFPlatformRuntimeState &get();
  static bool isInitialized() { return CPS; }
  static void destroy();

  COFFPlatformRuntimeState() = default;

  // Delete copy and move constructors.
  COFFPlatformRuntimeState(const COFFPlatformRuntimeState &) = delete;
  COFFPlatformRuntimeState &
  operator=(const COFFPlatformRuntimeState &) = delete;
  COFFPlatformRuntimeState(COFFPlatformRuntimeState &&) = delete;
  COFFPlatformRuntimeState &operator=(COFFPlatformRuntimeState &&) = delete;

  const char *dlerror();
  void *dlopen(std::string_view Name, int Mode);
  int dlclose(void *Header);
  void *dlsym(void *Header, std::string_view Symbol);

  Error registerJITDylib(std::string Name, void *Header);
  Error deregisterJITDylib(void *Header);

  Error registerAtExit(ExecutorAddr HeaderAddr, void (*AtExit)(void));

  Error registerObjectSections(
      ExecutorAddr HeaderAddr,
      std::vector<std::pair<std::string_view, ExecutorAddrRange>> Secs,
      bool RunInitializers);
  Error deregisterObjectSections(
      ExecutorAddr HeaderAddr,
      std::vector<std::pair<std::string_view, ExecutorAddrRange>> Secs);

  void *findJITDylibBaseByPC(uint64_t PC);

private:
  Error registerBlockRange(ExecutorAddr HeaderAddr, ExecutorAddrRange Range);
  Error deregisterBlockRange(ExecutorAddr HeaderAddr, ExecutorAddrRange Range);

  Error registerSEHFrames(ExecutorAddr HeaderAddr,
                          ExecutorAddrRange SEHFrameRange);
  Error deregisterSEHFrames(ExecutorAddr HeaderAddr,
                            ExecutorAddrRange SEHFrameRange);

  Expected<void *> dlopenImpl(std::string_view Path, int Mode);
  Error dlopenFull(JITDylibState &JDS);
  Error dlopenInitialize(JITDylibState &JDS, COFFJITDylibDepInfoMap &DepInfo);

  Error dlcloseImpl(void *DSOHandle);
  Error dlcloseDeinitialize(JITDylibState &JDS);

  JITDylibState *getJITDylibStateByHeader(void *DSOHandle);
  JITDylibState *getJITDylibStateByName(std::string_view Path);
  Expected<ExecutorAddr> lookupSymbolInJITDylib(void *DSOHandle,
                                                std::string_view Symbol);

  static COFFPlatformRuntimeState *CPS;

  std::recursive_mutex JDStatesMutex;
  std::map<void *, JITDylibState> JDStates;
  struct BlockRange {
    void *Header;
    size_t Size;
  };
  std::map<void *, BlockRange> BlockRanges;
  std::unordered_map<std::string_view, void *> JDNameToHeader;
  std::string DLFcnError;
};

} // namespace

COFFPlatformRuntimeState *COFFPlatformRuntimeState::CPS = nullptr;

COFFPlatformRuntimeState::JITDylibState *
COFFPlatformRuntimeState::getJITDylibStateByHeader(void *Header) {
  auto I = JDStates.find(Header);
  if (I == JDStates.end())
    return nullptr;
  return &I->second;
}

COFFPlatformRuntimeState::JITDylibState *
COFFPlatformRuntimeState::getJITDylibStateByName(std::string_view Name) {
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

Error COFFPlatformRuntimeState::registerJITDylib(std::string Name,
                                                 void *Header) {
  ORC_RT_DEBUG({
    printdbg("Registering JITDylib %s: Header = %p\n", Name.c_str(), Header);
  });
  std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);
  if (JDStates.count(Header)) {
    std::ostringstream ErrStream;
    ErrStream << "Duplicate JITDylib registration for header " << Header
              << " (name = " << Name << ")";
    return make_error<StringError>(ErrStream.str());
  }
  if (JDNameToHeader.count(Name)) {
    std::ostringstream ErrStream;
    ErrStream << "Duplicate JITDylib registration for header " << Header
              << " (header = " << Header << ")";
    return make_error<StringError>(ErrStream.str());
  }

  auto &JDS = JDStates[Header];
  JDS.Name = std::move(Name);
  JDS.Header = Header;
  JDNameToHeader[JDS.Name] = Header;
  return Error::success();
}

Error COFFPlatformRuntimeState::deregisterJITDylib(void *Header) {
  std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);
  auto I = JDStates.find(Header);
  if (I == JDStates.end()) {
    std::ostringstream ErrStream;
    ErrStream << "Attempted to deregister unrecognized header " << Header;
    return make_error<StringError>(ErrStream.str());
  }

  // Remove std::string construction once we can use C++20.
  auto J = JDNameToHeader.find(
      std::string(I->second.Name.data(), I->second.Name.size()));
  assert(J != JDNameToHeader.end() &&
         "Missing JDNameToHeader entry for JITDylib");

  ORC_RT_DEBUG({
    printdbg("Deregistering JITDylib %s: Header = %p\n", I->second.Name.c_str(),
             Header);
  });

  JDNameToHeader.erase(J);
  JDStates.erase(I);
  return Error::success();
}

void COFFPlatformRuntimeState::XtorSection::RunAllNewAndFlush() {
  for (auto &Subsection : SubsectionsNew) {
    for (auto &XtorGroup : Subsection)
      for (auto &Xtor : XtorGroup)
        if (Xtor)
          Xtor();
    Subsection.clear();
  }
}

const char *COFFPlatformRuntimeState::dlerror() { return DLFcnError.c_str(); }

void *COFFPlatformRuntimeState::dlopen(std::string_view Path, int Mode) {
  ORC_RT_DEBUG({
    std::string S(Path.data(), Path.size());
    printdbg("COFFPlatform::dlopen(\"%s\")\n", S.c_str());
  });
  std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);
  if (auto H = dlopenImpl(Path, Mode))
    return *H;
  else {
    // FIXME: Make dlerror thread safe.
    DLFcnError = toString(H.takeError());
    return nullptr;
  }
}

int COFFPlatformRuntimeState::dlclose(void *DSOHandle) {
  ORC_RT_DEBUG({
    auto *JDS = getJITDylibStateByHeader(DSOHandle);
    std::string DylibName;
    if (JDS) {
      std::string S;
      printdbg("COFFPlatform::dlclose(%p) (%s)\n", DSOHandle, S.c_str());
    } else
      printdbg("COFFPlatform::dlclose(%p) (%s)\n", DSOHandle, "invalid handle");
  });
  std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);
  if (auto Err = dlcloseImpl(DSOHandle)) {
    // FIXME: Make dlerror thread safe.
    DLFcnError = toString(std::move(Err));
    return -1;
  }
  return 0;
}

void *COFFPlatformRuntimeState::dlsym(void *Header, std::string_view Symbol) {
  auto Addr = lookupSymbolInJITDylib(Header, Symbol);
  if (!Addr) {
    return 0;
  }

  return Addr->toPtr<void *>();
}

Expected<void *> COFFPlatformRuntimeState::dlopenImpl(std::string_view Path,
                                                      int Mode) {
  // Try to find JITDylib state by name.
  auto *JDS = getJITDylibStateByName(Path);

  if (!JDS)
    return make_error<StringError>("No registered JTIDylib for path " +
                                   std::string(Path.data(), Path.size()));

  if (auto Err = dlopenFull(*JDS))
    return std::move(Err);

  // Bump the ref-count on this dylib.
  ++JDS->DlRefCount;

  // Return the header address.
  return JDS->Header;
}

Error COFFPlatformRuntimeState::dlopenFull(JITDylibState &JDS) {
  // Call back to the JIT to push the initializers.
  Expected<COFFJITDylibDepInfoMap> DepInfoMap((COFFJITDylibDepInfoMap()));
  if (auto Err = WrapperFunction<SPSExpected<SPSCOFFJITDylibDepInfoMap>(
          SPSExecutorAddr)>::call(&__orc_rt_coff_push_initializers_tag,
                                  DepInfoMap,
                                  ExecutorAddr::fromPtr(JDS.Header)))
    return Err;
  if (!DepInfoMap)
    return DepInfoMap.takeError();

  if (auto Err = dlopenInitialize(JDS, *DepInfoMap))
    return Err;

  if (!DepInfoMap->empty()) {
    ORC_RT_DEBUG({
      printdbg("Unrecognized dep-info key headers in dlopen of %s\n",
               JDS.Name.c_str());
    });
    std::ostringstream ErrStream;
    ErrStream << "Encountered unrecognized dep-info key headers "
                 "while processing dlopen of "
              << JDS.Name;
    return make_error<StringError>(ErrStream.str());
  }

  return Error::success();
}

Error COFFPlatformRuntimeState::dlopenInitialize(
    JITDylibState &JDS, COFFJITDylibDepInfoMap &DepInfo) {
  ORC_RT_DEBUG({
    printdbg("COFFPlatformRuntimeState::dlopenInitialize(\"%s\")\n",
             JDS.Name.c_str());
  });

  // Skip visited dependency.
  auto I = DepInfo.find(ExecutorAddr::fromPtr(JDS.Header));
  if (I == DepInfo.end())
    return Error::success();

  auto DI = std::move(I->second);
  DepInfo.erase(I);

  // Run initializers of dependencies in proper order by depth-first traversal
  // of dependency graph.
  std::vector<JITDylibState *> OldDeps;
  std::swap(JDS.Deps, OldDeps);
  JDS.Deps.reserve(DI.size());
  for (auto DepHeaderAddr : DI) {
    auto *DepJDS = getJITDylibStateByHeader(DepHeaderAddr.toPtr<void *>());
    if (!DepJDS) {
      std::ostringstream ErrStream;
      ErrStream << "Encountered unrecognized dep header "
                << DepHeaderAddr.toPtr<void *>() << " while initializing "
                << JDS.Name;
      return make_error<StringError>(ErrStream.str());
    }
    ++DepJDS->LinkedAgainstRefCount;
    if (auto Err = dlopenInitialize(*DepJDS, DepInfo))
      return Err;
  }

  // Run static initializers.
  JDS.CInitSection.RunAllNewAndFlush();
  JDS.CXXInitSection.RunAllNewAndFlush();

  // Decrement old deps.
  for (auto *DepJDS : OldDeps) {
    --DepJDS->LinkedAgainstRefCount;
    if (!DepJDS->referenced())
      if (auto Err = dlcloseDeinitialize(*DepJDS))
        return Err;
  }

  return Error::success();
}

Error COFFPlatformRuntimeState::dlcloseImpl(void *DSOHandle) {
  // Try to find JITDylib state by header.
  auto *JDS = getJITDylibStateByHeader(DSOHandle);

  if (!JDS) {
    std::ostringstream ErrStream;
    ErrStream << "No registered JITDylib for " << DSOHandle;
    return make_error<StringError>(ErrStream.str());
  }

  // Bump the ref-count.
  --JDS->DlRefCount;

  if (!JDS->referenced())
    return dlcloseDeinitialize(*JDS);

  return Error::success();
}

Error COFFPlatformRuntimeState::dlcloseDeinitialize(JITDylibState &JDS) {
  ORC_RT_DEBUG({
    printdbg("COFFPlatformRuntimeState::dlcloseDeinitialize(\"%s\")\n",
             JDS.Name.c_str());
  });

  // Run atexits
  for (auto AtExit : JDS.AtExits)
    AtExit();
  JDS.AtExits.clear();

  // Run static terminators.
  JDS.CPreTermSection.RunAllNewAndFlush();
  JDS.CTermSection.RunAllNewAndFlush();

  // Queue all xtors as new again.
  JDS.CInitSection.Reset();
  JDS.CXXInitSection.Reset();
  JDS.CPreTermSection.Reset();
  JDS.CTermSection.Reset();

  // Deinitialize any dependencies.
  for (auto *DepJDS : JDS.Deps) {
    --DepJDS->LinkedAgainstRefCount;
    if (!DepJDS->referenced())
      if (auto Err = dlcloseDeinitialize(*DepJDS))
        return Err;
  }

  return Error::success();
}

Expected<ExecutorAddr>
COFFPlatformRuntimeState::lookupSymbolInJITDylib(void *header,
                                                 std::string_view Sym) {
  Expected<ExecutorAddr> Result((ExecutorAddr()));
  if (auto Err = WrapperFunction<SPSExpected<SPSExecutorAddr>(
          SPSExecutorAddr, SPSString)>::call(&__orc_rt_coff_symbol_lookup_tag,
                                             Result,
                                             ExecutorAddr::fromPtr(header),
                                             Sym))
    return std::move(Err);
  return Result;
}

Error COFFPlatformRuntimeState::registerObjectSections(
    ExecutorAddr HeaderAddr,
    std::vector<std::pair<std::string_view, ExecutorAddrRange>> Secs,
    bool RunInitializers) {
  std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);
  auto I = JDStates.find(HeaderAddr.toPtr<void *>());
  if (I == JDStates.end()) {
    std::ostringstream ErrStream;
    ErrStream << "Unrecognized header " << HeaderAddr.getValue();
    return make_error<StringError>(ErrStream.str());
  }
  auto &JDState = I->second;
  for (auto &KV : Secs) {
    if (auto Err = registerBlockRange(HeaderAddr, KV.second))
      return Err;
    if (KV.first.empty())
      continue;
    char LastChar = KV.first.data()[KV.first.size() - 1];
    if (KV.first == ".pdata") {
      if (auto Err = registerSEHFrames(HeaderAddr, KV.second))
        return Err;
    } else if (KV.first >= ".CRT$XIA" && KV.first <= ".CRT$XIZ") {
      if (RunInitializers)
        JDState.CInitSection.Register(LastChar,
                                      KV.second.toSpan<void (*)(void)>());
      else
        JDState.CInitSection.RegisterNoRun(LastChar,
                                           KV.second.toSpan<void (*)(void)>());
    } else if (KV.first >= ".CRT$XCA" && KV.first <= ".CRT$XCZ") {
      if (RunInitializers)
        JDState.CXXInitSection.Register(LastChar,
                                        KV.second.toSpan<void (*)(void)>());
      else
        JDState.CXXInitSection.RegisterNoRun(
            LastChar, KV.second.toSpan<void (*)(void)>());
    } else if (KV.first >= ".CRT$XPA" && KV.first <= ".CRT$XPZ")
      JDState.CPreTermSection.Register(LastChar,
                                       KV.second.toSpan<void (*)(void)>());
    else if (KV.first >= ".CRT$XTA" && KV.first <= ".CRT$XTZ")
      JDState.CTermSection.Register(LastChar,
                                    KV.second.toSpan<void (*)(void)>());
  }
  return Error::success();
}

Error COFFPlatformRuntimeState::deregisterObjectSections(
    ExecutorAddr HeaderAddr,
    std::vector<std::pair<std::string_view, ExecutorAddrRange>> Secs) {
  std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);
  auto I = JDStates.find(HeaderAddr.toPtr<void *>());
  if (I == JDStates.end()) {
    std::ostringstream ErrStream;
    ErrStream << "Attempted to deregister unrecognized header "
              << HeaderAddr.getValue();
    return make_error<StringError>(ErrStream.str());
  }
  for (auto &KV : Secs) {
    if (auto Err = deregisterBlockRange(HeaderAddr, KV.second))
      return Err;
    if (KV.first == ".pdata")
      if (auto Err = deregisterSEHFrames(HeaderAddr, KV.second))
        return Err;
  }
  return Error::success();
}

Error COFFPlatformRuntimeState::registerSEHFrames(
    ExecutorAddr HeaderAddr, ExecutorAddrRange SEHFrameRange) {
  int N = (SEHFrameRange.End.getValue() - SEHFrameRange.Start.getValue()) /
          sizeof(RUNTIME_FUNCTION);
  auto Func = SEHFrameRange.Start.toPtr<PRUNTIME_FUNCTION>();
  if (!RtlAddFunctionTable(Func, N,
                           static_cast<DWORD64>(HeaderAddr.getValue())))
    return make_error<StringError>("Failed to register SEH frames");
  return Error::success();
}

Error COFFPlatformRuntimeState::deregisterSEHFrames(
    ExecutorAddr HeaderAddr, ExecutorAddrRange SEHFrameRange) {
  if (!RtlDeleteFunctionTable(SEHFrameRange.Start.toPtr<PRUNTIME_FUNCTION>()))
    return make_error<StringError>("Failed to deregister SEH frames");
  return Error::success();
}

Error COFFPlatformRuntimeState::registerBlockRange(ExecutorAddr HeaderAddr,
                                                   ExecutorAddrRange Range) {
  assert(!BlockRanges.count(Range.Start.toPtr<void *>()) &&
         "Block range address already registered");
  BlockRange B = {HeaderAddr.toPtr<void *>(), Range.size()};
  BlockRanges.emplace(Range.Start.toPtr<void *>(), B);
  return Error::success();
}

Error COFFPlatformRuntimeState::deregisterBlockRange(ExecutorAddr HeaderAddr,
                                                     ExecutorAddrRange Range) {
  assert(BlockRanges.count(Range.Start.toPtr<void *>()) &&
         "Block range address not registered");
  BlockRanges.erase(Range.Start.toPtr<void *>());
  return Error::success();
}

Error COFFPlatformRuntimeState::registerAtExit(ExecutorAddr HeaderAddr,
                                               void (*AtExit)(void)) {
  std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);
  auto I = JDStates.find(HeaderAddr.toPtr<void *>());
  if (I == JDStates.end()) {
    std::ostringstream ErrStream;
    ErrStream << "Unrecognized header " << HeaderAddr.getValue();
    return make_error<StringError>(ErrStream.str());
  }
  I->second.AtExits.push_back(AtExit);
  return Error::success();
}

void COFFPlatformRuntimeState::initialize() {
  assert(!CPS && "COFFPlatformRuntimeState should be null");
  CPS = new COFFPlatformRuntimeState();
}

COFFPlatformRuntimeState &COFFPlatformRuntimeState::get() {
  assert(CPS && "COFFPlatformRuntimeState not initialized");
  return *CPS;
}

void COFFPlatformRuntimeState::destroy() {
  assert(CPS && "COFFPlatformRuntimeState not initialized");
  delete CPS;
}

void *COFFPlatformRuntimeState::findJITDylibBaseByPC(uint64_t PC) {
  std::lock_guard<std::recursive_mutex> Lock(JDStatesMutex);
  auto It = BlockRanges.upper_bound(reinterpret_cast<void *>(PC));
  if (It == BlockRanges.begin())
    return nullptr;
  --It;
  auto &Range = It->second;
  if (PC >= reinterpret_cast<uint64_t>(It->first) + Range.Size)
    return nullptr;
  return Range.Header;
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_coff_platform_bootstrap(char *ArgData, size_t ArgSize) {
  COFFPlatformRuntimeState::initialize();
  return WrapperFunctionResult().release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_coff_platform_shutdown(char *ArgData, size_t ArgSize) {
  COFFPlatformRuntimeState::destroy();
  return WrapperFunctionResult().release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_coff_register_jitdylib(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError(SPSString, SPSExecutorAddr)>::handle(
             ArgData, ArgSize,
             [](std::string &Name, ExecutorAddr HeaderAddr) {
               return COFFPlatformRuntimeState::get().registerJITDylib(
                   std::move(Name), HeaderAddr.toPtr<void *>());
             })
      .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_coff_deregister_jitdylib(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError(SPSExecutorAddr)>::handle(
             ArgData, ArgSize,
             [](ExecutorAddr HeaderAddr) {
               return COFFPlatformRuntimeState::get().deregisterJITDylib(
                   HeaderAddr.toPtr<void *>());
             })
      .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_coff_register_object_sections(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError(SPSExecutorAddr, SPSCOFFObjectSectionsMap,
                                  bool)>::
      handle(ArgData, ArgSize,
             [](ExecutorAddr HeaderAddr,
                std::vector<std::pair<std::string_view, ExecutorAddrRange>>
                    &Secs,
                bool RunInitializers) {
               return COFFPlatformRuntimeState::get().registerObjectSections(
                   HeaderAddr, std::move(Secs), RunInitializers);
             })
          .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_coff_deregister_object_sections(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError(SPSExecutorAddr, SPSCOFFObjectSectionsMap)>::
      handle(ArgData, ArgSize,
             [](ExecutorAddr HeaderAddr,
                std::vector<std::pair<std::string_view, ExecutorAddrRange>>
                    &Secs) {
               return COFFPlatformRuntimeState::get().deregisterObjectSections(
                   HeaderAddr, std::move(Secs));
             })
          .release();
}
//------------------------------------------------------------------------------
//                        JIT'd dlfcn alternatives.
//------------------------------------------------------------------------------

const char *__orc_rt_coff_jit_dlerror() {
  return COFFPlatformRuntimeState::get().dlerror();
}

void *__orc_rt_coff_jit_dlopen(const char *path, int mode) {
  return COFFPlatformRuntimeState::get().dlopen(path, mode);
}

int __orc_rt_coff_jit_dlclose(void *header) {
  return COFFPlatformRuntimeState::get().dlclose(header);
}

void *__orc_rt_coff_jit_dlsym(void *header, const char *symbol) {
  return COFFPlatformRuntimeState::get().dlsym(header, symbol);
}

//------------------------------------------------------------------------------
//                        COFF SEH exception support
//------------------------------------------------------------------------------

struct ThrowInfo {
  uint32_t attributes;
  void *data;
};

ORC_RT_INTERFACE void __stdcall __orc_rt_coff_cxx_throw_exception(
    void *pExceptionObject, ThrowInfo *pThrowInfo) {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmultichar"
#endif
  constexpr uint32_t EH_EXCEPTION_NUMBER = 'msc' | 0xE0000000;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  constexpr uint32_t EH_MAGIC_NUMBER1 = 0x19930520;
  auto BaseAddr = COFFPlatformRuntimeState::get().findJITDylibBaseByPC(
      reinterpret_cast<uint64_t>(pThrowInfo));
  if (!BaseAddr) {
    // This is not from JIT'd region.
    // FIXME: Use the default implementation like below when alias api is
    // capable. _CxxThrowException(pExceptionObject, pThrowInfo);
    fprintf(stderr, "Throwing exception from compiled callback into JIT'd "
                    "exception handler not supported yet.\n");
    abort();
    return;
  }
  const ULONG_PTR parameters[] = {
      EH_MAGIC_NUMBER1,
      reinterpret_cast<ULONG_PTR>(pExceptionObject),
      reinterpret_cast<ULONG_PTR>(pThrowInfo),
      reinterpret_cast<ULONG_PTR>(BaseAddr),
  };
  RaiseException(EH_EXCEPTION_NUMBER, EXCEPTION_NONCONTINUABLE,
                 _countof(parameters), parameters);
}

//------------------------------------------------------------------------------
//                             COFF atexits
//------------------------------------------------------------------------------

typedef int (*OnExitFunction)(void);
typedef void (*AtExitFunction)(void);

ORC_RT_INTERFACE OnExitFunction __orc_rt_coff_onexit(void *Header,
                                                     OnExitFunction Func) {
  if (auto Err = COFFPlatformRuntimeState::get().registerAtExit(
          ExecutorAddr::fromPtr(Header), (void (*)(void))Func)) {
    consumeError(std::move(Err));
    return nullptr;
  }
  return Func;
}

ORC_RT_INTERFACE int __orc_rt_coff_atexit(void *Header, AtExitFunction Func) {
  if (auto Err = COFFPlatformRuntimeState::get().registerAtExit(
          ExecutorAddr::fromPtr(Header), (void (*)(void))Func)) {
    consumeError(std::move(Err));
    return -1;
  }
  return 0;
}

//------------------------------------------------------------------------------
//                             COFF Run Program
//------------------------------------------------------------------------------

ORC_RT_INTERFACE int64_t __orc_rt_coff_run_program(const char *JITDylibName,
                                                   const char *EntrySymbolName,
                                                   int argc, char *argv[]) {
  using MainTy = int (*)(int, char *[]);

  void *H =
      __orc_rt_coff_jit_dlopen(JITDylibName, __orc_rt::coff::ORC_RT_RTLD_LAZY);
  if (!H) {
    __orc_rt_log_error(__orc_rt_coff_jit_dlerror());
    return -1;
  }

  auto *Main =
      reinterpret_cast<MainTy>(__orc_rt_coff_jit_dlsym(H, EntrySymbolName));

  if (!Main) {
    __orc_rt_log_error(__orc_rt_coff_jit_dlerror());
    return -1;
  }

  int Result = Main(argc, argv);

  if (__orc_rt_coff_jit_dlclose(H) == -1)
    __orc_rt_log_error(__orc_rt_coff_jit_dlerror());

  return Result;
}
