//===-- AppleObjCTrampolineHandler.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCTRAMPOLINEHANDLER_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCTRAMPOLINEHANDLER_H

#include <map>
#include <mutex>
#include <vector>

#include "lldb/Expression/UtilityFunction.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

class AppleObjCTrampolineHandler {
public:
  AppleObjCTrampolineHandler(const lldb::ProcessSP &process_sp,
                             const lldb::ModuleSP &objc_module_sp);

  ~AppleObjCTrampolineHandler();

  lldb::ThreadPlanSP GetStepThroughDispatchPlan(Thread &thread,
                                                bool stop_others);

  FunctionCaller *GetLookupImplementationFunctionCaller();

  bool AddrIsMsgForward(lldb::addr_t addr) const {
    return (addr == m_msg_forward_addr || addr == m_msg_forward_stret_addr);
  }

  struct DispatchFunction {
  public:
    enum FixUpState { eFixUpNone, eFixUpFixed, eFixUpToFix };

    const char *name = nullptr;
    bool stret_return = false;
    bool is_super = false;
    bool is_super2 = false;
    FixUpState fixedup = eFixUpNone;
  };

  lldb::addr_t SetupDispatchFunction(Thread &thread,
                                     ValueList &dispatch_values);
  const DispatchFunction *FindDispatchFunction(lldb::addr_t addr);
  void ForEachDispatchFunction(std::function<void(lldb::addr_t, 
                                                  const DispatchFunction &)>);

private:
  /// These hold the code for the function that finds the implementation of
  /// an ObjC message send given the class & selector and the kind of dispatch.
  /// There are two variants depending on whether the platform uses a separate
  /// _stret passing convention (e.g. Intel) or not (e.g. ARM).  The difference
  /// is only at the very end of the function, so the code is broken into the
  /// common prefix and the suffix, which get composed appropriately before
  /// the function gets compiled.
  /// \{
  static const char *g_lookup_implementation_function_name;
  static const char *g_lookup_implementation_function_common_code;
  static const char *g_lookup_implementation_with_stret_function_code;
  static const char *g_lookup_implementation_no_stret_function_code;
  /// \}

  class AppleObjCVTables {
  public:
    // These come from objc-gdb.h.
    enum VTableFlags {
      eOBJC_TRAMPOLINE_MESSAGE = (1 << 0), // trampoline acts like objc_msgSend
      eOBJC_TRAMPOLINE_STRET = (1 << 1),   // trampoline is struct-returning
      eOBJC_TRAMPOLINE_VTABLE = (1 << 2)   // trampoline is vtable dispatcher
    };

  private:
    struct VTableDescriptor {
      VTableDescriptor(uint32_t in_flags, lldb::addr_t in_code_start)
          : flags(in_flags), code_start(in_code_start) {}

      uint32_t flags;
      lldb::addr_t code_start;
    };

    class VTableRegion {
    public:
      VTableRegion() = default;

      VTableRegion(AppleObjCVTables *owner, lldb::addr_t header_addr);

      void SetUpRegion();

      lldb::addr_t GetNextRegionAddr() { return m_next_region; }

      lldb::addr_t GetCodeStart() { return m_code_start_addr; }

      lldb::addr_t GetCodeEnd() { return m_code_end_addr; }

      uint32_t GetFlagsForVTableAtAddress(lldb::addr_t address) { return 0; }

      bool IsValid() { return m_valid; }

      bool AddressInRegion(lldb::addr_t addr, uint32_t &flags);

      void Dump(Stream &s);

      bool m_valid = false;
      AppleObjCVTables *m_owner = nullptr;
      lldb::addr_t m_header_addr = LLDB_INVALID_ADDRESS;
      lldb::addr_t m_code_start_addr = 0;
      lldb::addr_t m_code_end_addr = 0;
      std::vector<VTableDescriptor> m_descriptors;
      lldb::addr_t m_next_region = 0;
    };

  public:
    AppleObjCVTables(const lldb::ProcessSP &process_sp,
                     const lldb::ModuleSP &objc_module_sp);

    ~AppleObjCVTables();

    bool InitializeVTableSymbols();

    static bool RefreshTrampolines(void *baton,
                                   StoppointCallbackContext *context,
                                   lldb::user_id_t break_id,
                                   lldb::user_id_t break_loc_id);
    bool ReadRegions();

    bool ReadRegions(lldb::addr_t region_addr);

    bool IsAddressInVTables(lldb::addr_t addr, uint32_t &flags);

    lldb::ProcessSP GetProcessSP() { return m_process_wp.lock(); }

  private:
    lldb::ProcessWP m_process_wp;
    typedef std::vector<VTableRegion> region_collection;
    lldb::addr_t m_trampoline_header;
    lldb::break_id_t m_trampolines_changed_bp_id;
    region_collection m_regions;
    lldb::ModuleSP m_objc_module_sp;
  };

  static const DispatchFunction g_dispatch_functions[];
  static const char *g_opt_dispatch_names[];

  using MsgsendMap = std::map<lldb::addr_t, int>; // This table maps an dispatch
                                                  // fn address to the index in
                                                  // g_dispatch_functions
  MsgsendMap m_msgSend_map;
  MsgsendMap m_opt_dispatch_map;
  lldb::ProcessWP m_process_wp;
  lldb::ModuleSP m_objc_module_sp;
  std::string m_lookup_implementation_function_code;
  std::unique_ptr<UtilityFunction> m_impl_code;
  std::mutex m_impl_function_mutex;
  lldb::addr_t m_impl_fn_addr;
  lldb::addr_t m_impl_stret_fn_addr;
  lldb::addr_t m_msg_forward_addr;
  lldb::addr_t m_msg_forward_stret_addr;
  std::unique_ptr<AppleObjCVTables> m_vtables_up;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCTRAMPOLINEHANDLER_H
