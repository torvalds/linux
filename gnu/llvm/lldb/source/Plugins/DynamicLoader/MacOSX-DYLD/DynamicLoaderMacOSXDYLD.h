//===-- DynamicLoaderMacOSXDYLD.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// This is the DynamicLoader plugin for Darwin (macOS / iPhoneOS / tvOS /
// watchOS / BridgeOS)
// platforms earlier than 2016, where lldb would read the "dyld_all_image_infos"
// dyld internal structure to understand where things were loaded and the
// solib loaded/unloaded notification function we put a breakpoint on gives us
// an array of (load address, mod time, file path) tuples.
//
// As of late 2016, the new DynamicLoaderMacOS plugin should be used, which uses
// dyld SPI functions to get the same information without reading internal dyld
// data structures.

#ifndef LLDB_SOURCE_PLUGINS_DYNAMICLOADER_MACOSX_DYLD_DYNAMICLOADERMACOSXDYLD_H
#define LLDB_SOURCE_PLUGINS_DYNAMICLOADER_MACOSX_DYLD_DYNAMICLOADERMACOSXDYLD_H

#include <mutex>
#include <vector>

#include "lldb/Host/SafeMachO.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/UUID.h"

#include "DynamicLoaderDarwin.h"

class DynamicLoaderMacOSXDYLD : public lldb_private::DynamicLoaderDarwin {
public:
  DynamicLoaderMacOSXDYLD(lldb_private::Process *process);

  ~DynamicLoaderMacOSXDYLD() override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "macosx-dyld"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::DynamicLoader *
  CreateInstance(lldb_private::Process *process, bool force);

  /// Called after attaching a process.
  ///
  /// Allow DynamicLoader plug-ins to execute some code after
  /// attaching to a process.
  bool ProcessDidExec() override;

  lldb_private::Status CanLoadImage() override;

  bool GetSharedCacheInformation(
      lldb::addr_t &base_address, lldb_private::UUID &uuid,
      lldb_private::LazyBool &using_shared_cache,
      lldb_private::LazyBool &private_shared_cache) override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  bool IsFullyInitialized() override;

protected:
  void PutToLog(lldb_private::Log *log) const;

  void DoInitialImageFetch() override;

  bool NeedToDoInitialImageFetch() override;

  bool DidSetNotificationBreakpoint() override;

  void DoClear() override;

  bool ReadDYLDInfoFromMemoryAndSetNotificationCallback(lldb::addr_t addr);

  static bool
  NotifyBreakpointHit(void *baton,
                      lldb_private::StoppointCallbackContext *context,
                      lldb::user_id_t break_id, lldb::user_id_t break_loc_id);

  uint32_t AddrByteSize();

  bool ReadMachHeader(lldb::addr_t addr, llvm::MachO::mach_header *header,
                      lldb_private::DataExtractor *load_command_data);

  uint32_t ParseLoadCommands(const lldb_private::DataExtractor &data,
                             ImageInfo &dylib_info,
                             lldb_private::FileSpec *lc_id_dylinker);

  struct DYLDAllImageInfos {
    uint32_t version = 0;
    uint32_t dylib_info_count = 0;                            // Version >= 1
    lldb::addr_t dylib_info_addr = LLDB_INVALID_ADDRESS;      // Version >= 1
    lldb::addr_t notification = LLDB_INVALID_ADDRESS;         // Version >= 1
    bool processDetachedFromSharedRegion = false;             // Version >= 1
    bool libSystemInitialized = false;                        // Version >= 2
    lldb::addr_t dyldImageLoadAddress = LLDB_INVALID_ADDRESS; // Version >= 2

    DYLDAllImageInfos() = default;

    void Clear() {
      version = 0;
      dylib_info_count = 0;
      dylib_info_addr = LLDB_INVALID_ADDRESS;
      notification = LLDB_INVALID_ADDRESS;
      processDetachedFromSharedRegion = false;
      libSystemInitialized = false;
      dyldImageLoadAddress = LLDB_INVALID_ADDRESS;
    }

    bool IsValid() const { return version >= 1 && version <= 6; }
  };

  static lldb::ByteOrder GetByteOrderFromMagic(uint32_t magic);

  bool SetNotificationBreakpoint() override;

  void ClearNotificationBreakpoint() override;

  // There is a little tricky bit where you might initially attach while dyld is
  // updating
  // the all_image_infos, and you can't read the infos, so you have to continue
  // and pick it
  // up when you hit the update breakpoint.  At that point, you need to run this
  // initialize
  // function, but when you do it that way you DON'T need to do the extra work
  // you would at
  // the breakpoint.
  // So this function will only do actual work if the image infos haven't been
  // read yet.
  // If it does do any work, then it will return true, and false otherwise.
  // That way you can
  // call it in the breakpoint action, and if it returns true you're done.
  bool InitializeFromAllImageInfos();

  bool ReadAllImageInfosStructure();

  bool AddModulesUsingImageInfosAddress(lldb::addr_t image_infos_addr,
                                        uint32_t image_infos_count);

  bool RemoveModulesUsingImageInfosAddress(lldb::addr_t image_infos_addr,
                                           uint32_t image_infos_count);

  void UpdateImageInfosHeaderAndLoadCommands(ImageInfo::collection &image_infos,
                                             uint32_t infos_count,
                                             bool update_executable);

  bool ReadImageInfos(lldb::addr_t image_infos_addr, uint32_t image_infos_count,
                      ImageInfo::collection &image_infos);

  lldb::addr_t m_dyld_all_image_infos_addr;
  DYLDAllImageInfos m_dyld_all_image_infos;
  uint32_t m_dyld_all_image_infos_stop_id;
  lldb::user_id_t m_break_id;
  mutable std::recursive_mutex m_mutex;
  bool m_process_image_addr_is_all_images_infos;

private:
  DynamicLoaderMacOSXDYLD(const DynamicLoaderMacOSXDYLD &) = delete;
  const DynamicLoaderMacOSXDYLD &
  operator=(const DynamicLoaderMacOSXDYLD &) = delete;
};

#endif // LLDB_SOURCE_PLUGINS_DYNAMICLOADER_MACOSX_DYLD_DYNAMICLOADERMACOSXDYLD_H
