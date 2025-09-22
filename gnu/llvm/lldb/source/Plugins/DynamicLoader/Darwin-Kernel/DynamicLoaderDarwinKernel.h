//===-- DynamicLoaderDarwinKernel.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_DYNAMICLOADER_DARWIN_KERNEL_DYNAMICLOADERDARWINKERNEL_H
#define LLDB_SOURCE_PLUGINS_DYNAMICLOADER_DARWIN_KERNEL_DYNAMICLOADERDARWINKERNEL_H

#include <mutex>
#include <string>
#include <vector>


#include "lldb/Host/SafeMachO.h"

#include "lldb/Core/Progress.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/UUID.h"

class DynamicLoaderDarwinKernel : public lldb_private::DynamicLoader {
public:
  DynamicLoaderDarwinKernel(lldb_private::Process *process,
                            lldb::addr_t kernel_addr);

  ~DynamicLoaderDarwinKernel() override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "darwin-kernel"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::DynamicLoader *
  CreateInstance(lldb_private::Process *process, bool force);

  static void DebuggerInitialize(lldb_private::Debugger &debugger);

  static lldb::addr_t SearchForDarwinKernel(lldb_private::Process *process);

  /// Called after attaching a process.
  ///
  /// Allow DynamicLoader plug-ins to execute some code after
  /// attaching to a process.
  void DidAttach() override;

  void DidLaunch() override;

  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(lldb_private::Thread &thread,
                                                  bool stop_others) override;

  lldb_private::Status CanLoadImage() override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  void PrivateInitialize(lldb_private::Process *process);

  void PrivateProcessStateChanged(lldb_private::Process *process,
                                  lldb::StateType state);

  void UpdateIfNeeded();

  void LoadKernelModuleIfNeeded();

  void Clear(bool clear_process);

  void PutToLog(lldb_private::Log *log) const;

  static bool
  BreakpointHitCallback(void *baton,
                        lldb_private::StoppointCallbackContext *context,
                        lldb::user_id_t break_id, lldb::user_id_t break_loc_id);

  bool BreakpointHit(lldb_private::StoppointCallbackContext *context,
                     lldb::user_id_t break_id, lldb::user_id_t break_loc_id);
  uint32_t GetAddrByteSize() { return m_kernel.GetAddressByteSize(); }

  static lldb::ByteOrder GetByteOrderFromMagic(uint32_t magic);

  enum {
    KERNEL_MODULE_MAX_NAME = 64u,
    // Versions less than 2 didn't have an entry size,
    // they had a 64 bit name, 16 byte UUID, 8 byte addr,
    // 8 byte size, 8 byte version, 4 byte load tag, and
    // 4 byte flags
    KERNEL_MODULE_ENTRY_SIZE_VERSION_1 = 64u + 16u + 8u + 8u + 8u + 4u + 4u
  };

  // class KextImageInfo represents a single kext or kernel binary image.
  // The class was designed to hold the information from the
  // OSKextLoadedKextSummary
  // structure (in libkern/libkern/OSKextLibPrivate.h from xnu).  The kernel
  // maintains
  // a list of loded kexts in memory (the OSKextLoadedKextSummaryHeader
  // structure,
  // which points to an array of OSKextLoadedKextSummary's).
  //
  // A KextImageInfos may have -
  //
  // 1. The load address, name, UUID, and size of a kext/kernel binary in memory
  //    (read straight out of the kernel's list-of-kexts loaded)
  // 2. A ModuleSP based on a MemoryModule read out of the kernel's memory
  //    (very unlikely to have any symbolic information)
  // 3. A ModuleSP for an on-disk copy of the kext binary, possibly with debug
  // info
  //    or a dSYM
  //
  // For performance reasons, the developer may prefer that lldb not load the
  // kexts out
  // of memory at the start of a kernel session.  But we should build up /
  // maintain a
  // list of kexts that the kernel has told us about so we can relocate a kext
  // module
  // later if the user explicitly adds it to the target.

  class KextImageInfo {
  public:
    KextImageInfo() : m_name(), m_module_sp(), m_memory_module_sp(), m_uuid() {}

    void Clear() {
      m_load_address = LLDB_INVALID_ADDRESS;
      m_size = 0;
      m_name.clear();
      m_uuid.Clear();
      m_module_sp.reset();
      m_memory_module_sp.reset();
      m_load_process_stop_id = UINT32_MAX;
    }

    bool LoadImageAtFileAddress(lldb_private::Process *process);

    bool LoadImageUsingMemoryModule(lldb_private::Process *process,
                                    lldb_private::Progress *progress = nullptr);

    bool IsLoaded() { return m_load_process_stop_id != UINT32_MAX; }

    void SetLoadAddress(
        lldb::addr_t load_addr); // Address of the Mach-O header for this binary

    lldb::addr_t
    GetLoadAddress() const; // Address of the Mach-O header for this binary

    lldb_private::UUID GetUUID() const;

    void SetUUID(const lldb_private::UUID &uuid);

    void SetName(const char *);

    std::string GetName() const;

    void SetModule(lldb::ModuleSP module);

    lldb::ModuleSP GetModule();

    // try to fill in m_memory_module_sp from memory based on the m_load_address
    bool ReadMemoryModule(lldb_private::Process *process);

    bool IsKernel()
        const; // true if this is the mach_kernel; false if this is a kext

    void SetIsKernel(bool is_kernel);

    uint64_t GetSize() const;

    void SetSize(uint64_t size);

    uint32_t
    GetProcessStopId() const; // the stop-id when this binary was first noticed

    void SetProcessStopId(uint32_t stop_id);

    bool operator==(const KextImageInfo &rhs) const;

    uint32_t GetAddressByteSize(); // as determined by Mach-O header

    lldb::ByteOrder GetByteOrder(); // as determined by Mach-O header

    lldb_private::ArchSpec
    GetArchitecture() const; // as determined by Mach-O header

    void PutToLog(lldb_private::Log *log) const;

    typedef std::vector<KextImageInfo> collection;
    typedef collection::iterator iterator;
    typedef collection::const_iterator const_iterator;

  private:
    std::string m_name;
    lldb::ModuleSP m_module_sp;
    lldb::ModuleSP m_memory_module_sp;
    uint32_t m_load_process_stop_id =
        UINT32_MAX; // the stop-id when this module was added
                    // to the Target
    lldb_private::UUID
        m_uuid; // UUID for this dylib if it has one, else all zeros
    lldb::addr_t m_load_address = LLDB_INVALID_ADDRESS;
    uint64_t m_size = 0;
    bool m_kernel_image =
        false; // true if this is the kernel, false if this is a kext
  };

  struct OSKextLoadedKextSummaryHeader {
    uint32_t version = 0;
    uint32_t entry_size = 0;
    uint32_t entry_count = 0;
    lldb::addr_t image_infos_addr = LLDB_INVALID_ADDRESS;

    OSKextLoadedKextSummaryHeader() = default;

    uint32_t GetSize() {
      switch (version) {
      case 0:
        return 0; // Can't know the size without a valid version
      case 1:
        return 8; // Version 1 only had a version + entry_count
      default:
        break;
      }
      // Version 2 and above has version, entry_size, entry_count, and reserved
      return 16;
    }

    void Clear() {
      version = 0;
      entry_size = 0;
      entry_count = 0;
      image_infos_addr = LLDB_INVALID_ADDRESS;
    }

    bool IsValid() const { return version >= 1 && version <= 2; }
  };

  void RegisterNotificationCallbacks();

  void UnregisterNotificationCallbacks();

  void SetNotificationBreakpointIfNeeded();

  bool ReadAllKextSummaries();

  bool ReadKextSummaryHeader();

  bool ParseKextSummaries(const lldb_private::Address &kext_summary_addr,
                          uint32_t count);

  void
  UpdateImageInfosHeaderAndLoadCommands(KextImageInfo::collection &image_infos,
                                        uint32_t infos_count,
                                        bool update_executable);

  uint32_t ReadKextSummaries(const lldb_private::Address &kext_summary_addr,
                             uint32_t image_infos_count,
                             KextImageInfo::collection &image_infos);

  static lldb::addr_t
  SearchForKernelAtSameLoadAddr(lldb_private::Process *process);

  static lldb::addr_t
  SearchForKernelWithDebugHints(lldb_private::Process *process);

  static lldb::addr_t SearchForKernelNearPC(lldb_private::Process *process);

  static lldb::addr_t
  SearchForKernelViaExhaustiveSearch(lldb_private::Process *process);

  static bool
  ReadMachHeader(lldb::addr_t addr, lldb_private::Process *process, llvm::MachO::mach_header &mh,
                 bool *read_error = nullptr);

  static lldb_private::UUID
  CheckForKernelImageAtAddress(lldb::addr_t addr,
                               lldb_private::Process *process,
                               bool *read_error = nullptr);

  lldb::addr_t m_kernel_load_address;
  KextImageInfo m_kernel; // Info about the current kernel image being used

  lldb_private::Address m_kext_summary_header_ptr_addr;
  lldb_private::Address m_kext_summary_header_addr;
  OSKextLoadedKextSummaryHeader m_kext_summary_header;
  KextImageInfo::collection m_known_kexts;
  mutable std::recursive_mutex m_mutex;
  lldb::user_id_t m_break_id;

private:
  DynamicLoaderDarwinKernel(const DynamicLoaderDarwinKernel &) = delete;
  const DynamicLoaderDarwinKernel &
  operator=(const DynamicLoaderDarwinKernel &) = delete;
};

#endif // LLDB_SOURCE_PLUGINS_DYNAMICLOADER_DARWIN_KERNEL_DYNAMICLOADERDARWINKERNEL_H
