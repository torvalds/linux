//===-- DynamicLoaderDarwin.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_DYNAMICLOADER_MACOSX_DYLD_DYNAMICLOADERDARWIN_H
#define LLDB_SOURCE_PLUGINS_DYNAMICLOADER_MACOSX_DYLD_DYNAMICLOADERDARWIN_H

#include <map>
#include <mutex>
#include <vector>

#include "lldb/Host/SafeMachO.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/UUID.h"

#include "llvm/TargetParser/Triple.h"

namespace lldb_private {

class DynamicLoaderDarwin : public lldb_private::DynamicLoader {
public:
  DynamicLoaderDarwin(lldb_private::Process *process);

  ~DynamicLoaderDarwin() override;

  /// Called after attaching a process.
  ///
  /// Allow DynamicLoader plug-ins to execute some code after
  /// attaching to a process.
  void DidAttach() override;

  void DidLaunch() override;

  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(lldb_private::Thread &thread,
                                                  bool stop_others) override;

  void FindEquivalentSymbols(
      lldb_private::Symbol *original_symbol,
      lldb_private::ModuleList &module_list,
      lldb_private::SymbolContextList &equivalent_symbols) override;

  lldb::addr_t GetThreadLocalData(const lldb::ModuleSP module,
                                  const lldb::ThreadSP thread,
                                  lldb::addr_t tls_file_addr) override;

  bool AlwaysRelyOnEHUnwindInfo(lldb_private::SymbolContext &sym_ctx) override;

  virtual void DoInitialImageFetch() = 0;

  virtual bool NeedToDoInitialImageFetch() = 0;

  std::optional<lldb_private::Address> GetStartAddress() override;

protected:
  void PrivateInitialize(lldb_private::Process *process);

  void PrivateProcessStateChanged(lldb_private::Process *process,
                                  lldb::StateType state);

  void Clear(bool clear_process);

  // Clear method for classes derived from this one
  virtual void DoClear() = 0;

  void SetDYLDModule(lldb::ModuleSP &dyld_module_sp);

  lldb::ModuleSP GetDYLDModule();

  void ClearDYLDModule();

  class Segment {
  public:
    Segment() : name() {}

    lldb_private::ConstString name;
    lldb::addr_t vmaddr = LLDB_INVALID_ADDRESS;
    lldb::addr_t vmsize = 0;
    lldb::addr_t fileoff = 0;
    lldb::addr_t filesize = 0;
    uint32_t maxprot = 0;
    uint32_t initprot = 0;
    uint32_t nsects = 0;
    uint32_t flags = 0;

    bool operator==(const Segment &rhs) const {
      return name == rhs.name && vmaddr == rhs.vmaddr && vmsize == rhs.vmsize;
    }

    void PutToLog(lldb_private::Log *log, lldb::addr_t slide) const;
  };

  struct ImageInfo {
    /// Address of mach header for this dylib.
    lldb::addr_t address = LLDB_INVALID_ADDRESS;
    /// The amount to slide all segments by if there is a global
    /// slide.
    lldb::addr_t slide = 0;
    /// Resolved path for this dylib.
    lldb_private::FileSpec file_spec;
    /// UUID for this dylib if it has one, else all zeros.
    lldb_private::UUID uuid;
    /// The mach header for this image.
    llvm::MachO::mach_header header;
    /// All segment vmaddr and vmsize pairs for this executable (from
    /// memory of inferior).
    std::vector<Segment> segments;
    /// The process stop ID that the sections for this image were
    /// loaded.
    uint32_t load_stop_id = 0;
    /// LC_VERSION_MIN_... load command os type.
    llvm::Triple::OSType os_type = llvm::Triple::OSType::UnknownOS;
    /// LC_VERSION_MIN_... load command os environment.
    llvm::Triple::EnvironmentType os_env =
        llvm::Triple::EnvironmentType::UnknownEnvironment;
    /// LC_VERSION_MIN_... SDK.
    std::string min_version_os_sdk;

    ImageInfo() = default;

    void Clear(bool load_cmd_data_only) {
      if (!load_cmd_data_only) {
        address = LLDB_INVALID_ADDRESS;
        slide = 0;
        file_spec.Clear();
        ::memset(&header, 0, sizeof(header));
      }
      uuid.Clear();
      segments.clear();
      load_stop_id = 0;
      os_type = llvm::Triple::OSType::UnknownOS;
      os_env = llvm::Triple::EnvironmentType::UnknownEnvironment;
      min_version_os_sdk.clear();
    }

    bool operator==(const ImageInfo &rhs) const {
      return address == rhs.address && slide == rhs.slide &&
             file_spec == rhs.file_spec && uuid == rhs.uuid &&
             memcmp(&header, &rhs.header, sizeof(header)) == 0 &&
             segments == rhs.segments && os_type == rhs.os_type &&
             os_env == rhs.os_env;
    }

    bool UUIDValid() const { return uuid.IsValid(); }

    uint32_t GetAddressByteSize() {
      if (header.cputype) {
        if (header.cputype & llvm::MachO::CPU_ARCH_ABI64)
          return 8;
        else
          return 4;
      }
      return 0;
    }

    lldb_private::ArchSpec GetArchitecture() const;

    const Segment *FindSegment(lldb_private::ConstString name) const;

    void PutToLog(lldb_private::Log *log) const;

    typedef std::vector<ImageInfo> collection;
    typedef collection::iterator iterator;
    typedef collection::const_iterator const_iterator;
  };

  bool UpdateImageLoadAddress(lldb_private::Module *module, ImageInfo &info);

  bool UnloadModuleSections(lldb_private::Module *module, ImageInfo &info);

  lldb::ModuleSP FindTargetModuleForImageInfo(ImageInfo &image_info,
                                              bool can_create,
                                              bool *did_create_ptr);

  void UnloadImages(const std::vector<lldb::addr_t> &solib_addresses);

  void UnloadAllImages();

  virtual bool SetNotificationBreakpoint() = 0;

  virtual void ClearNotificationBreakpoint() = 0;

  virtual bool DidSetNotificationBreakpoint() = 0;

  typedef std::map<uint64_t, lldb::addr_t> PthreadKeyToTLSMap;
  typedef std::map<lldb::user_id_t, PthreadKeyToTLSMap> ThreadIDToTLSMap;

  std::recursive_mutex &GetMutex() const { return m_mutex; }

  lldb::ModuleSP GetPThreadLibraryModule();

  lldb_private::Address GetPthreadSetSpecificAddress();

  bool JSONImageInformationIntoImageInfo(
      lldb_private::StructuredData::ObjectSP image_details,
      ImageInfo::collection &image_infos);

  // If image_infos contains / may contain dyld or executable image, call this
  // method
  // to keep our internal record keeping of the special binaries up-to-date.
  void
  UpdateSpecialBinariesFromNewImageInfos(ImageInfo::collection &image_infos);

  // if image_info is a dyld binary, call this method
  void UpdateDYLDImageInfoFromNewImageInfo(ImageInfo &image_info);

  // If image_infos contains / may contain executable image, call this method
  // to keep our internal record keeping of the special dyld binary up-to-date.
  void AddExecutableModuleIfInImageInfos(ImageInfo::collection &image_infos);

  bool AddModulesUsingImageInfos(ImageInfo::collection &image_infos);

  // Whether we should use the new dyld SPI to get shared library information,
  // or read
  // it directly out of the dyld_all_image_infos.  Whether we use the (newer)
  // DynamicLoaderMacOS
  // plugin or the (older) DynamicLoaderMacOSX plugin.
  static bool UseDYLDSPI(lldb_private::Process *process);

  lldb::ModuleWP m_dyld_module_wp; // the dyld whose file type (mac, ios, etc)
                                   // matches the process
  lldb::ModuleWP m_libpthread_module_wp;
  lldb_private::Address m_pthread_getspecific_addr;
  ThreadIDToTLSMap m_tid_to_tls_map;
  ImageInfo::collection
      m_dyld_image_infos;              // Current shared libraries information
  uint32_t m_dyld_image_infos_stop_id; // The process stop ID that
                                       // "m_dyld_image_infos" is valid for
  ImageInfo m_dyld;
  mutable std::recursive_mutex m_mutex;

private:
  DynamicLoaderDarwin(const DynamicLoaderDarwin &) = delete;
  const DynamicLoaderDarwin &operator=(const DynamicLoaderDarwin &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_DYNAMICLOADER_MACOSX_DYLD_DYNAMICLOADERDARWIN_H
