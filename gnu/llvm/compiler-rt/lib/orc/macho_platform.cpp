//===- macho_platform.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code required to load the rest of the MachO runtime.
//
//===----------------------------------------------------------------------===//

#include "macho_platform.h"
#include "bitmask_enum.h"
#include "common.h"
#include "debug.h"
#include "error.h"
#include "interval_map.h"
#include "wrapper_function_utils.h"

#include <algorithm>
#include <ios>
#include <map>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define DEBUG_TYPE "macho_platform"

using namespace __orc_rt;
using namespace __orc_rt::macho;

// Declare function tags for functions in the JIT process.
ORC_RT_JIT_DISPATCH_TAG(__orc_rt_macho_push_initializers_tag)
ORC_RT_JIT_DISPATCH_TAG(__orc_rt_macho_push_symbols_tag)

struct objc_image_info;
struct mach_header;

// Objective-C registration functions.
// These are weakly imported. If the Objective-C runtime has not been loaded
// then code containing Objective-C sections will generate an error.
extern "C" void
_objc_map_images(unsigned count, const char *const paths[],
                 const mach_header *const mhdrs[]) ORC_RT_WEAK_IMPORT;

extern "C" void _objc_load_image(const char *path,
                                 const mach_header *mh) ORC_RT_WEAK_IMPORT;

// Libunwind prototypes.
struct unw_dynamic_unwind_sections {
  uintptr_t dso_base;
  uintptr_t dwarf_section;
  size_t dwarf_section_length;
  uintptr_t compact_unwind_section;
  size_t compact_unwind_section_length;
};

typedef int (*unw_find_dynamic_unwind_sections)(
    uintptr_t addr, struct unw_dynamic_unwind_sections *info);

extern "C" int __unw_add_find_dynamic_unwind_sections(
    unw_find_dynamic_unwind_sections find_dynamic_unwind_sections)
    ORC_RT_WEAK_IMPORT;

extern "C" int __unw_remove_find_dynamic_unwind_sections(
    unw_find_dynamic_unwind_sections find_dynamic_unwind_sections)
    ORC_RT_WEAK_IMPORT;

namespace {

struct MachOJITDylibDepInfo {
  bool Sealed = false;
  std::vector<ExecutorAddr> DepHeaders;
};

using MachOJITDylibDepInfoMap =
    std::unordered_map<ExecutorAddr, MachOJITDylibDepInfo>;

} // anonymous namespace

namespace __orc_rt {

using SPSMachOObjectPlatformSectionsMap =
    SPSSequence<SPSTuple<SPSString, SPSExecutorAddrRange>>;

using SPSMachOJITDylibDepInfo = SPSTuple<bool, SPSSequence<SPSExecutorAddr>>;

using SPSMachOJITDylibDepInfoMap =
    SPSSequence<SPSTuple<SPSExecutorAddr, SPSMachOJITDylibDepInfo>>;

template <>
class SPSSerializationTraits<SPSMachOJITDylibDepInfo, MachOJITDylibDepInfo> {
public:
  static size_t size(const MachOJITDylibDepInfo &JDI) {
    return SPSMachOJITDylibDepInfo::AsArgList::size(JDI.Sealed, JDI.DepHeaders);
  }

  static bool serialize(SPSOutputBuffer &OB, const MachOJITDylibDepInfo &JDI) {
    return SPSMachOJITDylibDepInfo::AsArgList::serialize(OB, JDI.Sealed,
                                                         JDI.DepHeaders);
  }

  static bool deserialize(SPSInputBuffer &IB, MachOJITDylibDepInfo &JDI) {
    return SPSMachOJITDylibDepInfo::AsArgList::deserialize(IB, JDI.Sealed,
                                                           JDI.DepHeaders);
  }
};

struct UnwindSectionInfo {
  std::vector<ExecutorAddrRange> CodeRanges;
  ExecutorAddrRange DwarfSection;
  ExecutorAddrRange CompactUnwindSection;
};

using SPSUnwindSectionInfo =
    SPSTuple<SPSSequence<SPSExecutorAddrRange>, SPSExecutorAddrRange,
             SPSExecutorAddrRange>;

template <>
class SPSSerializationTraits<SPSUnwindSectionInfo, UnwindSectionInfo> {
public:
  static size_t size(const UnwindSectionInfo &USI) {
    return SPSUnwindSectionInfo::AsArgList::size(
        USI.CodeRanges, USI.DwarfSection, USI.CompactUnwindSection);
  }

  static bool serialize(SPSOutputBuffer &OB, const UnwindSectionInfo &USI) {
    return SPSUnwindSectionInfo::AsArgList::serialize(
        OB, USI.CodeRanges, USI.DwarfSection, USI.CompactUnwindSection);
  }

  static bool deserialize(SPSInputBuffer &IB, UnwindSectionInfo &USI) {
    return SPSUnwindSectionInfo::AsArgList::deserialize(
        IB, USI.CodeRanges, USI.DwarfSection, USI.CompactUnwindSection);
  }
};

} // namespace __orc_rt

namespace {
struct TLVDescriptor {
  void *(*Thunk)(TLVDescriptor *) = nullptr;
  unsigned long Key = 0;
  unsigned long DataAddress = 0;
};

class MachOPlatformRuntimeState {
public:
  // Used internally by MachOPlatformRuntimeState, but made public to enable
  // serialization.
  enum class MachOExecutorSymbolFlags : uint8_t {
    None = 0,
    Weak = 1U << 0,
    Callable = 1U << 1,
    ORC_RT_MARK_AS_BITMASK_ENUM(/* LargestValue = */ Callable)
  };

private:
  struct AtExitEntry {
    void (*Func)(void *);
    void *Arg;
  };

  using AtExitsVector = std::vector<AtExitEntry>;

  /// Used to manage sections of fixed-sized metadata records (e.g. pointer
  /// sections, selector refs, etc.)
  template <typename RecordElement> class RecordSectionsTracker {
  public:
    /// Add a section to the "new" list.
    void add(span<RecordElement> Sec) { New.push_back(std::move(Sec)); }

    /// Returns true if there are new sections to process.
    bool hasNewSections() const { return !New.empty(); }

    /// Returns the number of new sections to process.
    size_t numNewSections() const { return New.size(); }

    /// Process all new sections.
    template <typename ProcessSectionFunc>
    std::enable_if_t<std::is_void_v<
        std::invoke_result_t<ProcessSectionFunc, span<RecordElement>>>>
    processNewSections(ProcessSectionFunc &&ProcessSection) {
      for (auto &Sec : New)
        ProcessSection(Sec);
      moveNewToProcessed();
    }

    /// Proces all new sections with a fallible handler.
    ///
    /// Successfully handled sections will be moved to the Processed
    /// list.
    template <typename ProcessSectionFunc>
    std::enable_if_t<
        std::is_same_v<Error, std::invoke_result_t<ProcessSectionFunc,
                                                   span<RecordElement>>>,
        Error>
    processNewSections(ProcessSectionFunc &&ProcessSection) {
      for (size_t I = 0; I != New.size(); ++I) {
        if (auto Err = ProcessSection(New[I])) {
          for (size_t J = 0; J != I; ++J)
            Processed.push_back(New[J]);
          New.erase(New.begin(), New.begin() + I);
          return Err;
        }
      }
      moveNewToProcessed();
      return Error::success();
    }

    /// Move all sections back to New for reprocessing.
    void reset() {
      moveNewToProcessed();
      New = std::move(Processed);
    }

    /// Remove the section with the given range.
    bool removeIfPresent(ExecutorAddrRange R) {
      if (removeIfPresent(New, R))
        return true;
      return removeIfPresent(Processed, R);
    }

  private:
    void moveNewToProcessed() {
      if (Processed.empty())
        Processed = std::move(New);
      else {
        Processed.reserve(Processed.size() + New.size());
        std::copy(New.begin(), New.end(), std::back_inserter(Processed));
        New.clear();
      }
    }

    bool removeIfPresent(std::vector<span<RecordElement>> &V,
                         ExecutorAddrRange R) {
      auto RI = std::find_if(
          V.rbegin(), V.rend(),
          [RS = R.toSpan<RecordElement>()](const span<RecordElement> &E) {
            return E.data() == RS.data();
          });
      if (RI != V.rend()) {
        V.erase(std::next(RI).base());
        return true;
      }
      return false;
    }

    std::vector<span<RecordElement>> Processed;
    std::vector<span<RecordElement>> New;
  };

  struct UnwindSections {
    UnwindSections(const UnwindSectionInfo &USI)
        : DwarfSection(USI.DwarfSection.toSpan<char>()),
          CompactUnwindSection(USI.CompactUnwindSection.toSpan<char>()) {}

    span<char> DwarfSection;
    span<char> CompactUnwindSection;
  };

  using UnwindSectionsMap =
      IntervalMap<char *, UnwindSections, IntervalCoalescing::Disabled>;

  struct JITDylibState {

    using SymbolTableMap =
        std::unordered_map<std::string_view,
                           std::pair<ExecutorAddr, MachOExecutorSymbolFlags>>;

    std::string Name;
    void *Header = nullptr;
    bool Sealed = false;
    size_t LinkedAgainstRefCount = 0;
    size_t DlRefCount = 0;
    SymbolTableMap SymbolTable;
    std::vector<JITDylibState *> Deps;
    AtExitsVector AtExits;
    const objc_image_info *ObjCImageInfo = nullptr;
    std::unordered_map<void *, std::vector<char>> DataSectionContent;
    std::unordered_map<void *, size_t> ZeroInitRanges;
    UnwindSectionsMap UnwindSections;
    RecordSectionsTracker<void (*)()> ModInitsSections;
    RecordSectionsTracker<char> ObjCRuntimeRegistrationObjects;

    bool referenced() const {
      return LinkedAgainstRefCount != 0 || DlRefCount != 0;
    }
  };

public:
  static Error create();
  static MachOPlatformRuntimeState &get();
  static Error destroy();

  MachOPlatformRuntimeState() = default;

  // Delete copy and move constructors.
  MachOPlatformRuntimeState(const MachOPlatformRuntimeState &) = delete;
  MachOPlatformRuntimeState &
  operator=(const MachOPlatformRuntimeState &) = delete;
  MachOPlatformRuntimeState(MachOPlatformRuntimeState &&) = delete;
  MachOPlatformRuntimeState &operator=(MachOPlatformRuntimeState &&) = delete;

  Error initialize();
  Error shutdown();

  Error registerJITDylib(std::string Name, void *Header);
  Error deregisterJITDylib(void *Header);
  Error registerThreadDataSection(span<const char> ThreadDataSection);
  Error deregisterThreadDataSection(span<const char> ThreadDataSection);
  Error registerObjectSymbolTable(
      ExecutorAddr HeaderAddr,
      const std::vector<std::tuple<ExecutorAddr, ExecutorAddr,
                                   MachOExecutorSymbolFlags>> &Entries);
  Error deregisterObjectSymbolTable(
      ExecutorAddr HeaderAddr,
      const std::vector<std::tuple<ExecutorAddr, ExecutorAddr,
                                   MachOExecutorSymbolFlags>> &Entries);
  Error registerObjectPlatformSections(
      ExecutorAddr HeaderAddr, std::optional<UnwindSectionInfo> UnwindSections,
      std::vector<std::pair<std::string_view, ExecutorAddrRange>> Secs);
  Error deregisterObjectPlatformSections(
      ExecutorAddr HeaderAddr, std::optional<UnwindSectionInfo> UnwindSections,
      std::vector<std::pair<std::string_view, ExecutorAddrRange>> Secs);

  const char *dlerror();
  void *dlopen(std::string_view Name, int Mode);
  int dlclose(void *DSOHandle);
  void *dlsym(void *DSOHandle, const char *Symbol);

  int registerAtExit(void (*F)(void *), void *Arg, void *DSOHandle);
  void runAtExits(std::unique_lock<std::mutex> &JDStatesLock,
                  JITDylibState &JDS);
  void runAtExits(void *DSOHandle);

  /// Returns the base address of the section containing ThreadData.
  Expected<std::pair<const char *, size_t>>
  getThreadDataSectionFor(const char *ThreadData);

private:
  JITDylibState *getJITDylibStateByHeader(void *DSOHandle);
  JITDylibState *getJITDylibStateByName(std::string_view Path);

  /// Requests materialization of the given symbols. For each pair, the bool
  /// element indicates whether the symbol is required (true) or weakly
  /// referenced (false).
  Error requestPushSymbols(JITDylibState &JDS,
                           span<std::pair<std::string_view, bool>> Symbols);

  /// Attempts to look up the given symbols locally, requesting a push from the
  /// remote if they're not found. Results are written to the Result span, which
  /// must have the same size as the Symbols span.
  Error
  lookupSymbols(JITDylibState &JDS, std::unique_lock<std::mutex> &JDStatesLock,
                span<std::pair<ExecutorAddr, MachOExecutorSymbolFlags>> Result,
                span<std::pair<std::string_view, bool>> Symbols);

  bool lookupUnwindSections(void *Addr, unw_dynamic_unwind_sections &Info);

  static int findDynamicUnwindSections(uintptr_t addr,
                                       unw_dynamic_unwind_sections *info);
  static Error registerEHFrames(span<const char> EHFrameSection);
  static Error deregisterEHFrames(span<const char> EHFrameSection);

  static Error registerObjCRegistrationObjects(JITDylibState &JDS);
  static Error runModInits(std::unique_lock<std::mutex> &JDStatesLock,
                           JITDylibState &JDS);

  Expected<void *> dlopenImpl(std::string_view Path, int Mode);
  Error dlopenFull(std::unique_lock<std::mutex> &JDStatesLock,
                   JITDylibState &JDS);
  Error dlopenInitialize(std::unique_lock<std::mutex> &JDStatesLock,
                         JITDylibState &JDS, MachOJITDylibDepInfoMap &DepInfo);

  Error dlcloseImpl(void *DSOHandle);
  Error dlcloseDeinitialize(std::unique_lock<std::mutex> &JDStatesLock,
                            JITDylibState &JDS);

  static MachOPlatformRuntimeState *MOPS;

  bool UseCallbackStyleUnwindInfo = false;

  // FIXME: Move to thread-state.
  std::string DLFcnError;

  // APIMutex guards against concurrent entry into key "dyld" API functions
  // (e.g. dlopen, dlclose).
  std::recursive_mutex DyldAPIMutex;

  // JDStatesMutex guards the data structures that hold JITDylib state.
  std::mutex JDStatesMutex;
  std::unordered_map<void *, JITDylibState> JDStates;
  std::unordered_map<std::string_view, void *> JDNameToHeader;

  // ThreadDataSectionsMutex guards thread local data section state.
  std::mutex ThreadDataSectionsMutex;
  std::map<const char *, size_t> ThreadDataSections;
};

} // anonymous namespace

namespace __orc_rt {

class SPSMachOExecutorSymbolFlags;

template <>
class SPSSerializationTraits<
    SPSMachOExecutorSymbolFlags,
    MachOPlatformRuntimeState::MachOExecutorSymbolFlags> {
private:
  using UT = std::underlying_type_t<
      MachOPlatformRuntimeState::MachOExecutorSymbolFlags>;

public:
  static size_t
  size(const MachOPlatformRuntimeState::MachOExecutorSymbolFlags &SF) {
    return sizeof(UT);
  }

  static bool
  serialize(SPSOutputBuffer &OB,
            const MachOPlatformRuntimeState::MachOExecutorSymbolFlags &SF) {
    return SPSArgList<UT>::serialize(OB, static_cast<UT>(SF));
  }

  static bool
  deserialize(SPSInputBuffer &IB,
              MachOPlatformRuntimeState::MachOExecutorSymbolFlags &SF) {
    UT Tmp;
    if (!SPSArgList<UT>::deserialize(IB, Tmp))
      return false;
    SF = static_cast<MachOPlatformRuntimeState::MachOExecutorSymbolFlags>(Tmp);
    return true;
  }
};

} // namespace __orc_rt

namespace {

MachOPlatformRuntimeState *MachOPlatformRuntimeState::MOPS = nullptr;

Error MachOPlatformRuntimeState::create() {
  assert(!MOPS && "MachOPlatformRuntimeState should be null");
  MOPS = new MachOPlatformRuntimeState();
  return MOPS->initialize();
}

MachOPlatformRuntimeState &MachOPlatformRuntimeState::get() {
  assert(MOPS && "MachOPlatformRuntimeState not initialized");
  return *MOPS;
}

Error MachOPlatformRuntimeState::destroy() {
  assert(MOPS && "MachOPlatformRuntimeState not initialized");
  auto Err = MOPS->shutdown();
  delete MOPS;
  return Err;
}

Error MachOPlatformRuntimeState::initialize() {
  UseCallbackStyleUnwindInfo = __unw_add_find_dynamic_unwind_sections &&
                               __unw_remove_find_dynamic_unwind_sections;
  if (UseCallbackStyleUnwindInfo) {
    ORC_RT_DEBUG({
      printdbg("__unw_add/remove_find_dynamic_unwind_sections available."
               " Using callback-based frame info lookup.\n");
    });
    if (__unw_add_find_dynamic_unwind_sections(&findDynamicUnwindSections))
      return make_error<StringError>(
          "Could not register findDynamicUnwindSections");
  } else {
    ORC_RT_DEBUG({
      printdbg("__unw_add/remove_find_dynamic_unwind_sections not available."
               " Using classic frame info registration.\n");
    });
  }
  return Error::success();
}

Error MachOPlatformRuntimeState::shutdown() {
  if (UseCallbackStyleUnwindInfo) {
    if (__unw_remove_find_dynamic_unwind_sections(&findDynamicUnwindSections)) {
      ORC_RT_DEBUG(
          { printdbg("__unw_remove_find_dynamic_unwind_sections failed.\n"); });
    }
  }
  return Error::success();
}

Error MachOPlatformRuntimeState::registerJITDylib(std::string Name,
                                                  void *Header) {
  ORC_RT_DEBUG({
    printdbg("Registering JITDylib %s: Header = %p\n", Name.c_str(), Header);
  });
  std::lock_guard<std::mutex> Lock(JDStatesMutex);
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

Error MachOPlatformRuntimeState::deregisterJITDylib(void *Header) {
  std::lock_guard<std::mutex> Lock(JDStatesMutex);
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

Error MachOPlatformRuntimeState::registerThreadDataSection(
    span<const char> ThreadDataSection) {
  std::lock_guard<std::mutex> Lock(ThreadDataSectionsMutex);
  auto I = ThreadDataSections.upper_bound(ThreadDataSection.data());
  if (I != ThreadDataSections.begin()) {
    auto J = std::prev(I);
    if (J->first + J->second > ThreadDataSection.data())
      return make_error<StringError>("Overlapping __thread_data sections");
  }
  ThreadDataSections.insert(
      I, std::make_pair(ThreadDataSection.data(), ThreadDataSection.size()));
  return Error::success();
}

Error MachOPlatformRuntimeState::deregisterThreadDataSection(
    span<const char> ThreadDataSection) {
  std::lock_guard<std::mutex> Lock(ThreadDataSectionsMutex);
  auto I = ThreadDataSections.find(ThreadDataSection.data());
  if (I == ThreadDataSections.end())
    return make_error<StringError>("Attempt to deregister unknown thread data "
                                   "section");
  ThreadDataSections.erase(I);
  return Error::success();
}

Error MachOPlatformRuntimeState::registerObjectSymbolTable(
    ExecutorAddr HeaderAddr,
    const std::vector<std::tuple<ExecutorAddr, ExecutorAddr,
                                 MachOExecutorSymbolFlags>> &Entries) {

  std::lock_guard<std::mutex> Lock(JDStatesMutex);
  auto *JDS = getJITDylibStateByHeader(HeaderAddr.toPtr<void *>());
  if (!JDS) {
    std::ostringstream ErrStream;
    ErrStream << "Could not register object platform sections for "
                 "unrecognized header "
              << HeaderAddr.toPtr<void *>();
    return make_error<StringError>(ErrStream.str());
  }

  for (auto &[NameAddr, SymAddr, Flags] : Entries)
    JDS->SymbolTable[NameAddr.toPtr<const char *>()] = {SymAddr, Flags};

  return Error::success();
}

Error MachOPlatformRuntimeState::deregisterObjectSymbolTable(
    ExecutorAddr HeaderAddr,
    const std::vector<std::tuple<ExecutorAddr, ExecutorAddr,
                                 MachOExecutorSymbolFlags>> &Entries) {

  std::lock_guard<std::mutex> Lock(JDStatesMutex);
  auto *JDS = getJITDylibStateByHeader(HeaderAddr.toPtr<void *>());
  if (!JDS) {
    std::ostringstream ErrStream;
    ErrStream << "Could not register object platform sections for "
                 "unrecognized header "
              << HeaderAddr.toPtr<void *>();
    return make_error<StringError>(ErrStream.str());
  }

  for (auto &[NameAddr, SymAddr, Flags] : Entries)
    JDS->SymbolTable.erase(NameAddr.toPtr<const char *>());

  return Error::success();
}

Error MachOPlatformRuntimeState::registerObjectPlatformSections(
    ExecutorAddr HeaderAddr, std::optional<UnwindSectionInfo> UnwindInfo,
    std::vector<std::pair<std::string_view, ExecutorAddrRange>> Secs) {

  // FIXME: Reject platform section registration after the JITDylib is
  // sealed?

  ORC_RT_DEBUG({
    printdbg("MachOPlatform: Registering object sections for %p.\n",
             HeaderAddr.toPtr<void *>());
  });

  std::lock_guard<std::mutex> Lock(JDStatesMutex);
  auto *JDS = getJITDylibStateByHeader(HeaderAddr.toPtr<void *>());
  if (!JDS) {
    std::ostringstream ErrStream;
    ErrStream << "Could not register object platform sections for "
                 "unrecognized header "
              << HeaderAddr.toPtr<void *>();
    return make_error<StringError>(ErrStream.str());
  }

  if (UnwindInfo && UseCallbackStyleUnwindInfo) {
    ORC_RT_DEBUG({
      printdbg("  Registering new-style unwind info for:\n"
               "    DWARF: %p -- %p\n"
               "    Compact-unwind: %p -- %p\n"
               "  for:\n",
               UnwindInfo->DwarfSection.Start.toPtr<void *>(),
               UnwindInfo->DwarfSection.End.toPtr<void *>(),
               UnwindInfo->CompactUnwindSection.Start.toPtr<void *>(),
               UnwindInfo->CompactUnwindSection.End.toPtr<void *>());
    });
    for (auto &CodeRange : UnwindInfo->CodeRanges) {
      JDS->UnwindSections.insert(CodeRange.Start.toPtr<char *>(),
                                 CodeRange.End.toPtr<char *>(), *UnwindInfo);
      ORC_RT_DEBUG({
        printdbg("    [ %p -- %p ]\n", CodeRange.Start.toPtr<void *>(),
                 CodeRange.End.toPtr<void *>());
      });
    }
  }

  for (auto &KV : Secs) {
    // FIXME: Validate section ranges?
    if (KV.first == "__TEXT,__eh_frame") {
      if (!UseCallbackStyleUnwindInfo) {
        // Use classic libunwind registration.
        if (auto Err = registerEHFrames(KV.second.toSpan<const char>()))
          return Err;
      }
    } else if (KV.first == "__DATA,__data") {
      assert(!JDS->DataSectionContent.count(KV.second.Start.toPtr<char *>()) &&
             "Address already registered.");
      auto S = KV.second.toSpan<char>();
      JDS->DataSectionContent[KV.second.Start.toPtr<char *>()] =
          std::vector<char>(S.begin(), S.end());
    } else if (KV.first == "__DATA,__common") {
      JDS->ZeroInitRanges[KV.second.Start.toPtr<char *>()] = KV.second.size();
    } else if (KV.first == "__DATA,__thread_data") {
      if (auto Err = registerThreadDataSection(KV.second.toSpan<const char>()))
        return Err;
    } else if (KV.first == "__llvm_jitlink_ObjCRuntimeRegistrationObject")
      JDS->ObjCRuntimeRegistrationObjects.add(KV.second.toSpan<char>());
    else if (KV.first == "__DATA,__mod_init_func")
      JDS->ModInitsSections.add(KV.second.toSpan<void (*)()>());
    else {
      // Should this be a warning instead?
      return make_error<StringError>(
          "Encountered unexpected section " +
          std::string(KV.first.data(), KV.first.size()) +
          " while registering object platform sections");
    }
  }

  return Error::success();
}

Error MachOPlatformRuntimeState::deregisterObjectPlatformSections(
    ExecutorAddr HeaderAddr, std::optional<UnwindSectionInfo> UnwindInfo,
    std::vector<std::pair<std::string_view, ExecutorAddrRange>> Secs) {
  // TODO: Make this more efficient? (maybe unnecessary if removal is rare?)
  // TODO: Add a JITDylib prepare-for-teardown operation that clears all
  //       registered sections, causing this function to take the fast-path.
  ORC_RT_DEBUG({
    printdbg("MachOPlatform: Deregistering object sections for %p.\n",
             HeaderAddr.toPtr<void *>());
  });

  std::lock_guard<std::mutex> Lock(JDStatesMutex);
  auto *JDS = getJITDylibStateByHeader(HeaderAddr.toPtr<void *>());
  if (!JDS) {
    std::ostringstream ErrStream;
    ErrStream << "Could not register object platform sections for unrecognized "
                 "header "
              << HeaderAddr.toPtr<void *>();
    return make_error<StringError>(ErrStream.str());
  }

  // FIXME: Implement faster-path by returning immediately if JDS is being
  // torn down entirely?

  // TODO: Make library permanent (i.e. not able to be dlclosed) if it contains
  // any Swift or ObjC. Once this happens we can clear (and no longer record)
  // data section content, as the library could never be re-initialized.

  if (UnwindInfo && UseCallbackStyleUnwindInfo) {
    ORC_RT_DEBUG({
      printdbg("  Deregistering new-style unwind info for:\n"
               "    DWARF: %p -- %p\n"
               "    Compact-unwind: %p -- %p\n"
               "  for:\n",
               UnwindInfo->DwarfSection.Start.toPtr<void *>(),
               UnwindInfo->DwarfSection.End.toPtr<void *>(),
               UnwindInfo->CompactUnwindSection.Start.toPtr<void *>(),
               UnwindInfo->CompactUnwindSection.End.toPtr<void *>());
    });
    for (auto &CodeRange : UnwindInfo->CodeRanges) {
      JDS->UnwindSections.erase(CodeRange.Start.toPtr<char *>(),
                                CodeRange.End.toPtr<char *>());
      ORC_RT_DEBUG({
        printdbg("    [ %p -- %p ]\n", CodeRange.Start.toPtr<void *>(),
                 CodeRange.End.toPtr<void *>());
      });
    }
  }

  for (auto &KV : Secs) {
    // FIXME: Validate section ranges?
    if (KV.first == "__TEXT,__eh_frame") {
      if (!UseCallbackStyleUnwindInfo) {
        // Use classic libunwind registration.
        if (auto Err = deregisterEHFrames(KV.second.toSpan<const char>()))
          return Err;
      }
    } else if (KV.first == "__DATA,__data") {
      JDS->DataSectionContent.erase(KV.second.Start.toPtr<char *>());
    } else if (KV.first == "__DATA,__common") {
      JDS->ZeroInitRanges.erase(KV.second.Start.toPtr<char *>());
    } else if (KV.first == "__DATA,__thread_data") {
      if (auto Err =
              deregisterThreadDataSection(KV.second.toSpan<const char>()))
        return Err;
    } else if (KV.first == "__llvm_jitlink_ObjCRuntimeRegistrationObject")
      JDS->ObjCRuntimeRegistrationObjects.removeIfPresent(KV.second);
    else if (KV.first == "__DATA,__mod_init_func")
      JDS->ModInitsSections.removeIfPresent(KV.second);
    else {
      // Should this be a warning instead?
      return make_error<StringError>(
          "Encountered unexpected section " +
          std::string(KV.first.data(), KV.first.size()) +
          " while deregistering object platform sections");
    }
  }
  return Error::success();
}

const char *MachOPlatformRuntimeState::dlerror() { return DLFcnError.c_str(); }

void *MachOPlatformRuntimeState::dlopen(std::string_view Path, int Mode) {
  ORC_RT_DEBUG({
    std::string S(Path.data(), Path.size());
    printdbg("MachOPlatform::dlopen(\"%s\")\n", S.c_str());
  });
  std::lock_guard<std::recursive_mutex> Lock(DyldAPIMutex);
  if (auto H = dlopenImpl(Path, Mode))
    return *H;
  else {
    // FIXME: Make dlerror thread safe.
    DLFcnError = toString(H.takeError());
    return nullptr;
  }
}

int MachOPlatformRuntimeState::dlclose(void *DSOHandle) {
  ORC_RT_DEBUG({
    auto *JDS = getJITDylibStateByHeader(DSOHandle);
    std::string DylibName;
    if (JDS) {
      std::string S;
      printdbg("MachOPlatform::dlclose(%p) (%s)\n", DSOHandle, S.c_str());
    } else
      printdbg("MachOPlatform::dlclose(%p) (%s)\n", DSOHandle,
               "invalid handle");
  });
  std::lock_guard<std::recursive_mutex> Lock(DyldAPIMutex);
  if (auto Err = dlcloseImpl(DSOHandle)) {
    // FIXME: Make dlerror thread safe.
    DLFcnError = toString(std::move(Err));
    return -1;
  }
  return 0;
}

void *MachOPlatformRuntimeState::dlsym(void *DSOHandle, const char *Symbol) {
  std::unique_lock<std::mutex> Lock(JDStatesMutex);
  auto *JDS = getJITDylibStateByHeader(DSOHandle);
  if (!JDS) {
    std::ostringstream ErrStream;
    ErrStream << "In call to dlsym, unrecognized header address " << DSOHandle;
    DLFcnError = ErrStream.str();
    return nullptr;
  }

  std::string MangledName = std::string("_") + Symbol;
  std::pair<std::string_view, bool> Lookup(MangledName, false);
  std::pair<ExecutorAddr, MachOExecutorSymbolFlags> Result;

  if (auto Err = lookupSymbols(*JDS, Lock, {&Result, 1}, {&Lookup, 1})) {
    DLFcnError = toString(std::move(Err));
    return nullptr;
  }

  // Sign callable symbols as functions, to match dyld.
  if ((Result.second & MachOExecutorSymbolFlags::Callable) ==
      MachOExecutorSymbolFlags::Callable)
    return reinterpret_cast<void *>(Result.first.toPtr<void(void)>());
  return Result.first.toPtr<void *>();
}

int MachOPlatformRuntimeState::registerAtExit(void (*F)(void *), void *Arg,
                                              void *DSOHandle) {
  // FIXME: Handle out-of-memory errors, returning -1 if OOM.
  std::lock_guard<std::mutex> Lock(JDStatesMutex);
  auto *JDS = getJITDylibStateByHeader(DSOHandle);
  if (!JDS) {
    ORC_RT_DEBUG({
      printdbg("MachOPlatformRuntimeState::registerAtExit called with "
               "unrecognized dso handle %p\n",
               DSOHandle);
    });
    return -1;
  }
  JDS->AtExits.push_back({F, Arg});
  return 0;
}

void MachOPlatformRuntimeState::runAtExits(
    std::unique_lock<std::mutex> &JDStatesLock, JITDylibState &JDS) {
  auto AtExits = std::move(JDS.AtExits);

  // Unlock while running atexits, as they may trigger operations that modify
  // JDStates.
  JDStatesLock.unlock();
  while (!AtExits.empty()) {
    auto &AE = AtExits.back();
    AE.Func(AE.Arg);
    AtExits.pop_back();
  }
  JDStatesLock.lock();
}

void MachOPlatformRuntimeState::runAtExits(void *DSOHandle) {
  std::unique_lock<std::mutex> Lock(JDStatesMutex);
  auto *JDS = getJITDylibStateByHeader(DSOHandle);
  ORC_RT_DEBUG({
    printdbg("MachOPlatformRuntimeState::runAtExits called on unrecognized "
             "dso_handle %p\n",
             DSOHandle);
  });
  if (JDS)
    runAtExits(Lock, *JDS);
}

Expected<std::pair<const char *, size_t>>
MachOPlatformRuntimeState::getThreadDataSectionFor(const char *ThreadData) {
  std::lock_guard<std::mutex> Lock(ThreadDataSectionsMutex);
  auto I = ThreadDataSections.upper_bound(ThreadData);
  // Check that we have a valid entry covering this address.
  if (I == ThreadDataSections.begin())
    return make_error<StringError>("No thread local data section for key");
  I = std::prev(I);
  if (ThreadData >= I->first + I->second)
    return make_error<StringError>("No thread local data section for key");
  return *I;
}

MachOPlatformRuntimeState::JITDylibState *
MachOPlatformRuntimeState::getJITDylibStateByHeader(void *DSOHandle) {
  auto I = JDStates.find(DSOHandle);
  if (I == JDStates.end()) {
    I = JDStates.insert(std::make_pair(DSOHandle, JITDylibState())).first;
    I->second.Header = DSOHandle;
  }
  return &I->second;
}

MachOPlatformRuntimeState::JITDylibState *
MachOPlatformRuntimeState::getJITDylibStateByName(std::string_view Name) {
  // FIXME: Avoid creating string once we have C++20.
  auto I = JDNameToHeader.find(std::string(Name.data(), Name.size()));
  if (I != JDNameToHeader.end())
    return getJITDylibStateByHeader(I->second);
  return nullptr;
}

Error MachOPlatformRuntimeState::requestPushSymbols(
    JITDylibState &JDS, span<std::pair<std::string_view, bool>> Symbols) {
  Error OpErr = Error::success();
  if (auto Err = WrapperFunction<SPSError(
          SPSExecutorAddr, SPSSequence<SPSTuple<SPSString, bool>>)>::
          call(&__orc_rt_macho_push_symbols_tag, OpErr,
               ExecutorAddr::fromPtr(JDS.Header), Symbols)) {
    cantFail(std::move(OpErr));
    return std::move(Err);
  }
  return OpErr;
}

Error MachOPlatformRuntimeState::lookupSymbols(
    JITDylibState &JDS, std::unique_lock<std::mutex> &JDStatesLock,
    span<std::pair<ExecutorAddr, MachOExecutorSymbolFlags>> Result,
    span<std::pair<std::string_view, bool>> Symbols) {
  assert(JDStatesLock.owns_lock() &&
         "JDStatesLock should be locked at call-site");
  assert(Result.size() == Symbols.size() &&
         "Results and Symbols span sizes should match");

  // Make an initial pass over the local symbol table.
  std::vector<size_t> MissingSymbolIndexes;
  for (size_t Idx = 0; Idx != Symbols.size(); ++Idx) {
    auto I = JDS.SymbolTable.find(Symbols[Idx].first);
    if (I != JDS.SymbolTable.end())
      Result[Idx] = I->second;
    else
      MissingSymbolIndexes.push_back(Idx);
  }

  // If everything has been resolved already then bail out early.
  if (MissingSymbolIndexes.empty())
    return Error::success();

  // Otherwise call back to the controller to try to request that the symbol
  // be materialized.
  std::vector<std::pair<std::string_view, bool>> MissingSymbols;
  MissingSymbols.reserve(MissingSymbolIndexes.size());
  ORC_RT_DEBUG({
    printdbg("requesting push of %i missing symbols...\n",
             MissingSymbolIndexes.size());
  });
  for (auto MissingIdx : MissingSymbolIndexes)
    MissingSymbols.push_back(Symbols[MissingIdx]);

  JDStatesLock.unlock();
  if (auto Err = requestPushSymbols(
          JDS, {MissingSymbols.data(), MissingSymbols.size()}))
    return Err;
  JDStatesLock.lock();

  // Try to resolve the previously missing symbols locally.
  std::vector<size_t> MissingRequiredSymbols;
  for (auto MissingIdx : MissingSymbolIndexes) {
    auto I = JDS.SymbolTable.find(Symbols[MissingIdx].first);
    if (I != JDS.SymbolTable.end())
      Result[MissingIdx] = I->second;
    else {
      if (Symbols[MissingIdx].second)
        MissingRequiredSymbols.push_back(MissingIdx);
      else
        Result[MissingIdx] = {ExecutorAddr(), {}};
    }
  }

  // Error out if any missing symbols could not be resolved.
  if (!MissingRequiredSymbols.empty()) {
    std::ostringstream ErrStream;
    ErrStream << "Lookup could not find required symbols: [ ";
    for (auto MissingIdx : MissingRequiredSymbols)
      ErrStream << "\"" << Symbols[MissingIdx].first << "\" ";
    ErrStream << "]";
    return make_error<StringError>(ErrStream.str());
  }

  return Error::success();
}

// eh-frame registration functions.
// We expect these to be available for all processes.
extern "C" void __register_frame(const void *);
extern "C" void __deregister_frame(const void *);

template <typename HandleFDEFn>
void walkEHFrameSection(span<const char> EHFrameSection,
                        HandleFDEFn HandleFDE) {
  const char *CurCFIRecord = EHFrameSection.data();
  uint64_t Size = *reinterpret_cast<const uint32_t *>(CurCFIRecord);

  while (CurCFIRecord != EHFrameSection.end() && Size != 0) {
    const char *OffsetField = CurCFIRecord + (Size == 0xffffffff ? 12 : 4);
    if (Size == 0xffffffff)
      Size = *reinterpret_cast<const uint64_t *>(CurCFIRecord + 4) + 12;
    else
      Size += 4;
    uint32_t Offset = *reinterpret_cast<const uint32_t *>(OffsetField);

    if (Offset != 0)
      HandleFDE(CurCFIRecord);

    CurCFIRecord += Size;
    Size = *reinterpret_cast<const uint32_t *>(CurCFIRecord);
  }
}

bool MachOPlatformRuntimeState::lookupUnwindSections(
    void *Addr, unw_dynamic_unwind_sections &Info) {
  ORC_RT_DEBUG(
      { printdbg("Tried to lookup unwind-info via new lookup call.\n"); });
  std::lock_guard<std::mutex> Lock(JDStatesMutex);
  for (auto &KV : JDStates) {
    auto &JD = KV.second;
    auto I = JD.UnwindSections.find(reinterpret_cast<char *>(Addr));
    if (I != JD.UnwindSections.end()) {
      Info.dso_base = reinterpret_cast<uintptr_t>(JD.Header);
      Info.dwarf_section =
          reinterpret_cast<uintptr_t>(I->second.DwarfSection.data());
      Info.dwarf_section_length = I->second.DwarfSection.size();
      Info.compact_unwind_section =
          reinterpret_cast<uintptr_t>(I->second.CompactUnwindSection.data());
      Info.compact_unwind_section_length =
          I->second.CompactUnwindSection.size();
      return true;
    }
  }
  return false;
}

int MachOPlatformRuntimeState::findDynamicUnwindSections(
    uintptr_t addr, unw_dynamic_unwind_sections *info) {
  if (!info)
    return 0;
  return MachOPlatformRuntimeState::get().lookupUnwindSections((void *)addr,
                                                               *info);
}

Error MachOPlatformRuntimeState::registerEHFrames(
    span<const char> EHFrameSection) {
  walkEHFrameSection(EHFrameSection, __register_frame);
  return Error::success();
}

Error MachOPlatformRuntimeState::deregisterEHFrames(
    span<const char> EHFrameSection) {
  walkEHFrameSection(EHFrameSection, __deregister_frame);
  return Error::success();
}

Error MachOPlatformRuntimeState::registerObjCRegistrationObjects(
    JITDylibState &JDS) {
  ORC_RT_DEBUG(printdbg("Registering Objective-C / Swift metadata.\n"));

  std::vector<char *> RegObjBases;
  JDS.ObjCRuntimeRegistrationObjects.processNewSections(
      [&](span<char> RegObj) { RegObjBases.push_back(RegObj.data()); });

  if (RegObjBases.empty())
    return Error::success();

  if (!_objc_map_images || !_objc_load_image)
    return make_error<StringError>(
        "Could not register Objective-C / Swift metadata: _objc_map_images / "
        "_objc_load_image not found");

  std::vector<char *> Paths;
  Paths.resize(RegObjBases.size());
  _objc_map_images(RegObjBases.size(), Paths.data(),
                   reinterpret_cast<mach_header **>(RegObjBases.data()));

  for (void *RegObjBase : RegObjBases)
    _objc_load_image(nullptr, reinterpret_cast<mach_header *>(RegObjBase));

  return Error::success();
}

Error MachOPlatformRuntimeState::runModInits(
    std::unique_lock<std::mutex> &JDStatesLock, JITDylibState &JDS) {
  std::vector<span<void (*)()>> InitSections;
  InitSections.reserve(JDS.ModInitsSections.numNewSections());

  // Copy initializer sections: If the JITDylib is unsealed then the
  // initializers could reach back into the JIT and cause more initializers to
  // be added.
  // FIXME: Skip unlock and run in-place on sealed JITDylibs?
  JDS.ModInitsSections.processNewSections(
      [&](span<void (*)()> Inits) { InitSections.push_back(Inits); });

  JDStatesLock.unlock();
  for (auto InitSec : InitSections)
    for (auto *Init : InitSec)
      Init();
  JDStatesLock.lock();

  return Error::success();
}

Expected<void *> MachOPlatformRuntimeState::dlopenImpl(std::string_view Path,
                                                       int Mode) {
  std::unique_lock<std::mutex> Lock(JDStatesMutex);

  // Try to find JITDylib state by name.
  auto *JDS = getJITDylibStateByName(Path);

  if (!JDS)
    return make_error<StringError>("No registered JTIDylib for path " +
                                   std::string(Path.data(), Path.size()));

  // If this JITDylib is unsealed, or this is the first dlopen then run
  // full dlopen path (update deps, push and run initializers, update ref
  // counts on all JITDylibs in the dep tree).
  if (!JDS->referenced() || !JDS->Sealed) {
    if (auto Err = dlopenFull(Lock, *JDS))
      return std::move(Err);
  }

  // Bump the ref-count on this dylib.
  ++JDS->DlRefCount;

  // Return the header address.
  return JDS->Header;
}

Error MachOPlatformRuntimeState::dlopenFull(
    std::unique_lock<std::mutex> &JDStatesLock, JITDylibState &JDS) {
  // Call back to the JIT to push the initializers.
  Expected<MachOJITDylibDepInfoMap> DepInfo((MachOJITDylibDepInfoMap()));
  // Unlock so that we can accept the initializer update.
  JDStatesLock.unlock();
  if (auto Err = WrapperFunction<SPSExpected<SPSMachOJITDylibDepInfoMap>(
          SPSExecutorAddr)>::call(&__orc_rt_macho_push_initializers_tag,
                                  DepInfo, ExecutorAddr::fromPtr(JDS.Header)))
    return Err;
  JDStatesLock.lock();

  if (!DepInfo)
    return DepInfo.takeError();

  if (auto Err = dlopenInitialize(JDStatesLock, JDS, *DepInfo))
    return Err;

  if (!DepInfo->empty()) {
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

Error MachOPlatformRuntimeState::dlopenInitialize(
    std::unique_lock<std::mutex> &JDStatesLock, JITDylibState &JDS,
    MachOJITDylibDepInfoMap &DepInfo) {
  ORC_RT_DEBUG({
    printdbg("MachOPlatformRuntimeState::dlopenInitialize(\"%s\")\n",
             JDS.Name.c_str());
  });

  // If the header is not present in the dep map then assume that we
  // already processed it earlier in the dlopenInitialize traversal and
  // return.
  // TODO: Keep a visited set instead so that we can error out on missing
  //       entries?
  auto I = DepInfo.find(ExecutorAddr::fromPtr(JDS.Header));
  if (I == DepInfo.end())
    return Error::success();

  auto DI = std::move(I->second);
  DepInfo.erase(I);

  // We don't need to re-initialize sealed JITDylibs that have already been
  // initialized. Just check that their dep-map entry is empty as expected.
  if (JDS.Sealed) {
    if (!DI.DepHeaders.empty()) {
      std::ostringstream ErrStream;
      ErrStream << "Sealed JITDylib " << JDS.Header
                << " already has registered dependencies";
      return make_error<StringError>(ErrStream.str());
    }
    if (JDS.referenced())
      return Error::success();
  } else
    JDS.Sealed = DI.Sealed;

  // This is an unsealed or newly sealed JITDylib. Run initializers.
  std::vector<JITDylibState *> OldDeps;
  std::swap(JDS.Deps, OldDeps);
  JDS.Deps.reserve(DI.DepHeaders.size());
  for (auto DepHeaderAddr : DI.DepHeaders) {
    auto *DepJDS = getJITDylibStateByHeader(DepHeaderAddr.toPtr<void *>());
    if (!DepJDS) {
      std::ostringstream ErrStream;
      ErrStream << "Encountered unrecognized dep header "
                << DepHeaderAddr.toPtr<void *>() << " while initializing "
                << JDS.Name;
      return make_error<StringError>(ErrStream.str());
    }
    ++DepJDS->LinkedAgainstRefCount;
    if (auto Err = dlopenInitialize(JDStatesLock, *DepJDS, DepInfo))
      return Err;
  }

  // Initialize this JITDylib.
  if (auto Err = registerObjCRegistrationObjects(JDS))
    return Err;
  if (auto Err = runModInits(JDStatesLock, JDS))
    return Err;

  // Decrement old deps.
  // FIXME: We should probably continue and just report deinitialize errors
  // here.
  for (auto *DepJDS : OldDeps) {
    --DepJDS->LinkedAgainstRefCount;
    if (!DepJDS->referenced())
      if (auto Err = dlcloseDeinitialize(JDStatesLock, *DepJDS))
        return Err;
  }

  return Error::success();
}

Error MachOPlatformRuntimeState::dlcloseImpl(void *DSOHandle) {
  std::unique_lock<std::mutex> Lock(JDStatesMutex);

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
    return dlcloseDeinitialize(Lock, *JDS);

  return Error::success();
}

Error MachOPlatformRuntimeState::dlcloseDeinitialize(
    std::unique_lock<std::mutex> &JDStatesLock, JITDylibState &JDS) {

  ORC_RT_DEBUG({
    printdbg("MachOPlatformRuntimeState::dlcloseDeinitialize(\"%s\")\n",
             JDS.Name.c_str());
  });

  runAtExits(JDStatesLock, JDS);

  // Reset mod-inits
  JDS.ModInitsSections.reset();

  // Reset data section contents.
  for (auto &KV : JDS.DataSectionContent)
    memcpy(KV.first, KV.second.data(), KV.second.size());
  for (auto &KV : JDS.ZeroInitRanges)
    memset(KV.first, 0, KV.second);

  // Deinitialize any dependencies.
  for (auto *DepJDS : JDS.Deps) {
    --DepJDS->LinkedAgainstRefCount;
    if (!DepJDS->referenced())
      if (auto Err = dlcloseDeinitialize(JDStatesLock, *DepJDS))
        return Err;
  }

  return Error::success();
}

class MachOPlatformRuntimeTLVManager {
public:
  void *getInstance(const char *ThreadData);

private:
  std::unordered_map<const char *, char *> Instances;
  std::unordered_map<const char *, std::unique_ptr<char[]>> AllocatedSections;
};

void *MachOPlatformRuntimeTLVManager::getInstance(const char *ThreadData) {
  auto I = Instances.find(ThreadData);
  if (I != Instances.end())
    return I->second;

  auto TDS =
      MachOPlatformRuntimeState::get().getThreadDataSectionFor(ThreadData);
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

void destroyMachOTLVMgr(void *MachOTLVMgr) {
  delete static_cast<MachOPlatformRuntimeTLVManager *>(MachOTLVMgr);
}

Error runWrapperFunctionCalls(std::vector<WrapperFunctionCall> WFCs) {
  for (auto &WFC : WFCs)
    if (auto Err = WFC.runWithSPSRet<void>())
      return Err;
  return Error::success();
}

} // end anonymous namespace

//------------------------------------------------------------------------------
//                             JIT entry points
//------------------------------------------------------------------------------

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_macho_platform_bootstrap(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError()>::handle(
             ArgData, ArgSize,
             []() { return MachOPlatformRuntimeState::create(); })
      .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_macho_platform_shutdown(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError()>::handle(
             ArgData, ArgSize,
             []() { return MachOPlatformRuntimeState::destroy(); })
      .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_macho_register_jitdylib(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError(SPSString, SPSExecutorAddr)>::handle(
             ArgData, ArgSize,
             [](std::string &Name, ExecutorAddr HeaderAddr) {
               return MachOPlatformRuntimeState::get().registerJITDylib(
                   std::move(Name), HeaderAddr.toPtr<void *>());
             })
      .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_macho_deregister_jitdylib(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError(SPSExecutorAddr)>::handle(
             ArgData, ArgSize,
             [](ExecutorAddr HeaderAddr) {
               return MachOPlatformRuntimeState::get().deregisterJITDylib(
                   HeaderAddr.toPtr<void *>());
             })
      .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_macho_register_object_platform_sections(char *ArgData,
                                                 size_t ArgSize) {
  return WrapperFunction<SPSError(SPSExecutorAddr,
                                  SPSOptional<SPSUnwindSectionInfo>,
                                  SPSMachOObjectPlatformSectionsMap)>::
      handle(ArgData, ArgSize,
             [](ExecutorAddr HeaderAddr, std::optional<UnwindSectionInfo> USI,
                std::vector<std::pair<std::string_view, ExecutorAddrRange>>
                    &Secs) {
               return MachOPlatformRuntimeState::get()
                   .registerObjectPlatformSections(HeaderAddr, std::move(USI),
                                                   std::move(Secs));
             })
          .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_macho_register_object_symbol_table(char *ArgData, size_t ArgSize) {
  using SymtabContainer = std::vector<
      std::tuple<ExecutorAddr, ExecutorAddr,
                 MachOPlatformRuntimeState::MachOExecutorSymbolFlags>>;
  return WrapperFunction<SPSError(
      SPSExecutorAddr, SPSSequence<SPSTuple<SPSExecutorAddr, SPSExecutorAddr,
                                            SPSMachOExecutorSymbolFlags>>)>::
      handle(ArgData, ArgSize,
             [](ExecutorAddr HeaderAddr, SymtabContainer &Symbols) {
               return MachOPlatformRuntimeState::get()
                   .registerObjectSymbolTable(HeaderAddr, Symbols);
             })
          .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_macho_deregister_object_symbol_table(char *ArgData, size_t ArgSize) {
  using SymtabContainer = std::vector<
      std::tuple<ExecutorAddr, ExecutorAddr,
                 MachOPlatformRuntimeState::MachOExecutorSymbolFlags>>;
  return WrapperFunction<SPSError(
      SPSExecutorAddr, SPSSequence<SPSTuple<SPSExecutorAddr, SPSExecutorAddr,
                                            SPSMachOExecutorSymbolFlags>>)>::
      handle(ArgData, ArgSize,
             [](ExecutorAddr HeaderAddr, SymtabContainer &Symbols) {
               return MachOPlatformRuntimeState::get()
                   .deregisterObjectSymbolTable(HeaderAddr, Symbols);
             })
          .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_macho_deregister_object_platform_sections(char *ArgData,
                                                   size_t ArgSize) {
  return WrapperFunction<SPSError(SPSExecutorAddr,
                                  SPSOptional<SPSUnwindSectionInfo>,
                                  SPSMachOObjectPlatformSectionsMap)>::
      handle(ArgData, ArgSize,
             [](ExecutorAddr HeaderAddr, std::optional<UnwindSectionInfo> USI,
                std::vector<std::pair<std::string_view, ExecutorAddrRange>>
                    &Secs) {
               return MachOPlatformRuntimeState::get()
                   .deregisterObjectPlatformSections(HeaderAddr, std::move(USI),
                                                     std::move(Secs));
             })
          .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_macho_run_wrapper_function_calls(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSError(SPSSequence<SPSWrapperFunctionCall>)>::handle(
             ArgData, ArgSize, runWrapperFunctionCalls)
      .release();
}

//------------------------------------------------------------------------------
//                            TLV support
//------------------------------------------------------------------------------

ORC_RT_INTERFACE void *__orc_rt_macho_tlv_get_addr_impl(TLVDescriptor *D) {
  auto *TLVMgr = static_cast<MachOPlatformRuntimeTLVManager *>(
      pthread_getspecific(D->Key));
  if (!TLVMgr) {
    TLVMgr = new MachOPlatformRuntimeTLVManager();
    if (pthread_setspecific(D->Key, TLVMgr)) {
      __orc_rt_log_error("Call to pthread_setspecific failed");
      return nullptr;
    }
  }

  return TLVMgr->getInstance(
      reinterpret_cast<char *>(static_cast<uintptr_t>(D->DataAddress)));
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_macho_create_pthread_key(char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSExpected<uint64_t>(void)>::handle(
             ArgData, ArgSize,
             []() -> Expected<uint64_t> {
               pthread_key_t Key;
               if (int Err = pthread_key_create(&Key, destroyMachOTLVMgr)) {
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

int __orc_rt_macho_cxa_atexit(void (*func)(void *), void *arg,
                              void *dso_handle) {
  return MachOPlatformRuntimeState::get().registerAtExit(func, arg, dso_handle);
}

void __orc_rt_macho_cxa_finalize(void *dso_handle) {
  MachOPlatformRuntimeState::get().runAtExits(dso_handle);
}

//------------------------------------------------------------------------------
//                        JIT'd dlfcn alternatives.
//------------------------------------------------------------------------------

const char *__orc_rt_macho_jit_dlerror() {
  return MachOPlatformRuntimeState::get().dlerror();
}

void *__orc_rt_macho_jit_dlopen(const char *path, int mode) {
  return MachOPlatformRuntimeState::get().dlopen(path, mode);
}

int __orc_rt_macho_jit_dlclose(void *dso_handle) {
  return MachOPlatformRuntimeState::get().dlclose(dso_handle);
}

void *__orc_rt_macho_jit_dlsym(void *dso_handle, const char *symbol) {
  return MachOPlatformRuntimeState::get().dlsym(dso_handle, symbol);
}

//------------------------------------------------------------------------------
//                             MachO Run Program
//------------------------------------------------------------------------------

ORC_RT_INTERFACE int64_t __orc_rt_macho_run_program(const char *JITDylibName,
                                                    const char *EntrySymbolName,
                                                    int argc, char *argv[]) {
  using MainTy = int (*)(int, char *[]);

  void *H = __orc_rt_macho_jit_dlopen(JITDylibName,
                                      __orc_rt::macho::ORC_RT_RTLD_LAZY);
  if (!H) {
    __orc_rt_log_error(__orc_rt_macho_jit_dlerror());
    return -1;
  }

  auto *Main =
      reinterpret_cast<MainTy>(__orc_rt_macho_jit_dlsym(H, EntrySymbolName));

  if (!Main) {
    __orc_rt_log_error(__orc_rt_macho_jit_dlerror());
    return -1;
  }

  int Result = Main(argc, argv);

  if (__orc_rt_macho_jit_dlclose(H) == -1)
    __orc_rt_log_error(__orc_rt_macho_jit_dlerror());

  return Result;
}
