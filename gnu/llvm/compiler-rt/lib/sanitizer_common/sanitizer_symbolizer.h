//===-- sanitizer_symbolizer.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Symbolizer is used by sanitizers to map instruction address to a location in
// source code at run-time. Symbolizer either uses __sanitizer_symbolize_*
// defined in the program, or (if they are missing) tries to find and
// launch "llvm-symbolizer" commandline tool in a separate process and
// communicate with it.
//
// Generally we should try to avoid calling system library functions during
// symbolization (and use their replacements from sanitizer_libc.h instead).
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_SYMBOLIZER_H
#define SANITIZER_SYMBOLIZER_H

#include "sanitizer_common.h"
#include "sanitizer_mutex.h"
#include "sanitizer_vector.h"

namespace __sanitizer {

struct AddressInfo {
  // Owns all the string members. Storage for them is
  // (de)allocated using sanitizer internal allocator.
  uptr address;

  char *module;
  uptr module_offset;
  ModuleArch module_arch;
  u8 uuid[kModuleUUIDSize];
  uptr uuid_size;

  static const uptr kUnknown = ~(uptr)0;
  char *function;
  uptr function_offset;

  char *file;
  int line;
  int column;

  AddressInfo();
  // Deletes all strings and resets all fields.
  void Clear();
  void FillModuleInfo(const char *mod_name, uptr mod_offset, ModuleArch arch);
  void FillModuleInfo(const LoadedModule &mod);
  uptr module_base() const { return address - module_offset; }
};

// Linked list of symbolized frames (each frame is described by AddressInfo).
struct SymbolizedStack {
  SymbolizedStack *next;
  AddressInfo info;
  static SymbolizedStack *New(uptr addr);
  // Deletes current, and all subsequent frames in the linked list.
  // The object cannot be accessed after the call to this function.
  void ClearAll();

 private:
  SymbolizedStack();
};

class SymbolizedStackHolder {
  SymbolizedStack *Stack;

  void clear() {
    if (Stack)
      Stack->ClearAll();
  }

 public:
  explicit SymbolizedStackHolder(SymbolizedStack *Stack = nullptr)
      : Stack(Stack) {}
  ~SymbolizedStackHolder() { clear(); }
  void reset(SymbolizedStack *S = nullptr) {
    if (Stack != S)
      clear();
    Stack = S;
  }
  const SymbolizedStack *get() const { return Stack; }
};

// For now, DataInfo is used to describe global variable.
struct DataInfo {
  // Owns all the string members. Storage for them is
  // (de)allocated using sanitizer internal allocator.
  char *module;
  uptr module_offset;
  ModuleArch module_arch;

  char *file;
  uptr line;
  char *name;
  uptr start;
  uptr size;

  DataInfo();
  void Clear();
};

struct LocalInfo {
  char *function_name = nullptr;
  char *name = nullptr;
  char *decl_file = nullptr;
  unsigned decl_line = 0;

  bool has_frame_offset = false;
  bool has_size = false;
  bool has_tag_offset = false;

  sptr frame_offset;
  uptr size;
  uptr tag_offset;

  void Clear();
};

struct FrameInfo {
  char *module;
  uptr module_offset;
  ModuleArch module_arch;

  InternalMmapVector<LocalInfo> locals;
  void Clear();
};

class SymbolizerTool;

class Symbolizer final {
 public:
  /// Initialize and return platform-specific implementation of symbolizer
  /// (if it wasn't already initialized).
  static Symbolizer *GetOrInit();
  static void LateInitialize();
  // Returns a list of symbolized frames for a given address (containing
  // all inlined functions, if necessary).
  SymbolizedStack *SymbolizePC(uptr address);
  bool SymbolizeData(uptr address, DataInfo *info);
  bool SymbolizeFrame(uptr address, FrameInfo *info);

  // The module names Symbolizer returns are stable and unique for every given
  // module.  It is safe to store and compare them as pointers.
  bool GetModuleNameAndOffsetForPC(uptr pc, const char **module_name,
                                   uptr *module_address);
  const char *GetModuleNameForPc(uptr pc) {
    const char *module_name = nullptr;
    uptr unused;
    if (GetModuleNameAndOffsetForPC(pc, &module_name, &unused))
      return module_name;
    return nullptr;
  }

  // Release internal caches (if any).
  void Flush();
  // Attempts to demangle the provided C++ mangled name. Never returns nullptr.
  const char *Demangle(const char *name);

  // Allow user to install hooks that would be called before/after Symbolizer
  // does the actual file/line info fetching. Specific sanitizers may need this
  // to distinguish system library calls made in user code from calls made
  // during in-process symbolization.
  typedef void (*StartSymbolizationHook)();
  typedef void (*EndSymbolizationHook)();
  // May be called at most once.
  void AddHooks(StartSymbolizationHook start_hook,
                EndSymbolizationHook end_hook);

  void RefreshModules();
  const LoadedModule *FindModuleForAddress(uptr address);

  void InvalidateModuleList();

  const ListOfModules &GetRefreshedListOfModules();

 private:
  // GetModuleNameAndOffsetForPC has to return a string to the caller.
  // Since the corresponding module might get unloaded later, we should create
  // our owned copies of the strings that we can safely return.
  // ModuleNameOwner does not provide any synchronization, thus calls to
  // its method should be protected by |mu_|.
  class ModuleNameOwner {
   public:
    explicit ModuleNameOwner(Mutex *synchronized_by)
        : last_match_(nullptr), mu_(synchronized_by) {
      storage_.reserve(kInitialCapacity);
    }
    const char *GetOwnedCopy(const char *str);

   private:
    static const uptr kInitialCapacity = 1000;
    InternalMmapVector<const char*> storage_;
    const char *last_match_;

    Mutex *mu_;
  } module_names_;

  /// Platform-specific function for creating a Symbolizer object.
  static Symbolizer *PlatformInit();

  bool FindModuleNameAndOffsetForAddress(uptr address, const char **module_name,
                                         uptr *module_offset,
                                         ModuleArch *module_arch);
  ListOfModules modules_;
  ListOfModules fallback_modules_;
  // If stale, need to reload the modules before looking up addresses.
  bool modules_fresh_;

  // Platform-specific default demangler, returns nullptr on failure.
  const char *PlatformDemangle(const char *name);

  static Symbolizer *symbolizer_;
  static StaticSpinMutex init_mu_;

  // Mutex locked from public methods of |Symbolizer|, so that the internals
  // (including individual symbolizer tools and platform-specific methods) are
  // always synchronized.
  Mutex mu_;

  IntrusiveList<SymbolizerTool> tools_;

  explicit Symbolizer(IntrusiveList<SymbolizerTool> tools);

  static LowLevelAllocator symbolizer_allocator_;

  StartSymbolizationHook start_hook_;
  EndSymbolizationHook end_hook_;
  class SymbolizerScope {
   public:
    explicit SymbolizerScope(const Symbolizer *sym);
    ~SymbolizerScope();
   private:
    const Symbolizer *sym_;
    int errno_;  // Backup errno in case symbolizer change the value.
  };
};

#ifdef SANITIZER_WINDOWS
void InitializeDbgHelpIfNeeded();
#endif

}  // namespace __sanitizer

#endif  // SANITIZER_SYMBOLIZER_H
