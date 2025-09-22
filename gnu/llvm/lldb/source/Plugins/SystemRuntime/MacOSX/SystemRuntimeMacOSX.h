//===-- SystemRuntimeMacOSX.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYSTEMRUNTIME_MACOSX_SYSTEMRUNTIMEMACOSX_H
#define LLDB_SOURCE_PLUGINS_SYSTEMRUNTIME_MACOSX_SYSTEMRUNTIMEMACOSX_H

#include <mutex>
#include <string>
#include <vector>

// Other libraries and framework include
#include "lldb/Core/ModuleList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/QueueItem.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/UUID.h"

#include "AppleGetItemInfoHandler.h"
#include "AppleGetPendingItemsHandler.h"
#include "AppleGetQueuesHandler.h"
#include "AppleGetThreadItemInfoHandler.h"

class SystemRuntimeMacOSX : public lldb_private::SystemRuntime {
public:
  SystemRuntimeMacOSX(lldb_private::Process *process);

  ~SystemRuntimeMacOSX() override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() {
    return "systemruntime-macosx";
  }

  static lldb_private::SystemRuntime *
  CreateInstance(lldb_private::Process *process);

  // instance methods

  void Clear(bool clear_process);

  void Detach() override;

  const std::vector<lldb_private::ConstString> &
  GetExtendedBacktraceTypes() override;

  lldb::ThreadSP
  GetExtendedBacktraceThread(lldb::ThreadSP thread,
                             lldb_private::ConstString type) override;

  lldb::ThreadSP
  GetExtendedBacktraceForQueueItem(lldb::QueueItemSP queue_item_sp,
                                   lldb_private::ConstString type) override;

  lldb::ThreadSP GetExtendedBacktraceFromItemRef(lldb::addr_t item_ref);

  void PopulateQueueList(lldb_private::QueueList &queue_list) override;

  void PopulateQueuesUsingLibBTR(lldb::addr_t queues_buffer,
                                 uint64_t queues_buffer_size, uint64_t count,
                                 lldb_private::QueueList &queue_list);

  void PopulatePendingQueuesUsingLibBTR(lldb::addr_t items_buffer,
                                        uint64_t items_buffer_size,
                                        uint64_t count,
                                        lldb_private::Queue *queue);

  std::string
  GetQueueNameFromThreadQAddress(lldb::addr_t dispatch_qaddr) override;

  lldb::queue_id_t
  GetQueueIDFromThreadQAddress(lldb::addr_t dispatch_qaddr) override;

  lldb::addr_t GetLibdispatchQueueAddressFromThreadQAddress(
      lldb::addr_t dispatch_qaddr) override;

  void PopulatePendingItemsForQueue(lldb_private::Queue *queue) override;

  void CompleteQueueItem(lldb_private::QueueItem *queue_item,
                         lldb::addr_t item_ref) override;

  lldb::QueueKind GetQueueKind(lldb::addr_t dispatch_queue_addr) override;

  void AddThreadExtendedInfoPacketHints(
      lldb_private::StructuredData::ObjectSP dict) override;

  bool SafeToCallFunctionsOnThisThread(lldb::ThreadSP thread_sp) override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  lldb::user_id_t m_break_id;
  mutable std::recursive_mutex m_mutex;

private:
  struct libBacktraceRecording_info {
    uint16_t queue_info_version = 0;
    uint16_t queue_info_data_offset = 0;
    uint16_t item_info_version = 0;
    uint16_t item_info_data_offset = 0;

    libBacktraceRecording_info() = default;
  };

  // A structure which reflects the data recorded in the
  // libBacktraceRecording introspection_dispatch_item_info_s.
  struct ItemInfo {
    lldb::addr_t item_that_enqueued_this;
    lldb::addr_t function_or_block;
    uint64_t enqueuing_thread_id;
    uint64_t enqueuing_queue_serialnum;
    uint64_t target_queue_serialnum;
    uint32_t enqueuing_callstack_frame_count;
    uint32_t stop_id;
    std::vector<lldb::addr_t> enqueuing_callstack;
    std::string enqueuing_thread_label;
    std::string enqueuing_queue_label;
    std::string target_queue_label;
  };

  // The offsets of different fields of the dispatch_queue_t structure in
  // a thread/queue process.
  // Based on libdispatch src/queue_private.h, struct dispatch_queue_offsets_s
  // With dqo_version 1-3, the dqo_label field is a per-queue value and cannot
  // be cached.
  // With dqo_version 4 (Mac OS X 10.9 / iOS 7), dqo_label is a constant value
  // that can be cached.
  struct LibdispatchOffsets {
    uint16_t dqo_version;
    uint16_t dqo_label;
    uint16_t dqo_label_size;
    uint16_t dqo_flags;
    uint16_t dqo_flags_size;
    uint16_t dqo_serialnum;
    uint16_t dqo_serialnum_size;
    uint16_t dqo_width;
    uint16_t dqo_width_size;
    uint16_t dqo_running;
    uint16_t dqo_running_size;

    uint16_t dqo_suspend_cnt; // version 5 and later, starting with Mac OS X
                              // 10.10/iOS 8
    uint16_t dqo_suspend_cnt_size; // version 5 and later, starting with Mac OS
                                   // X 10.10/iOS 8
    uint16_t dqo_target_queue; // version 5 and later, starting with Mac OS X
                               // 10.10/iOS 8
    uint16_t dqo_target_queue_size; // version 5 and later, starting with Mac OS
                                    // X 10.10/iOS 8
    uint16_t
        dqo_priority; // version 5 and later, starting with Mac OS X 10.10/iOS 8
    uint16_t dqo_priority_size; // version 5 and later, starting with Mac OS X
                                // 10.10/iOS 8

    LibdispatchOffsets() {
      dqo_version = UINT16_MAX;
      dqo_flags = UINT16_MAX;
      dqo_serialnum = UINT16_MAX;
      dqo_label = UINT16_MAX;
      dqo_width = UINT16_MAX;
      dqo_running = UINT16_MAX;
      dqo_suspend_cnt = UINT16_MAX;
      dqo_target_queue = UINT16_MAX;
      dqo_target_queue = UINT16_MAX;
      dqo_priority = UINT16_MAX;
      dqo_label_size = 0;
      dqo_flags_size = 0;
      dqo_serialnum_size = 0;
      dqo_width_size = 0;
      dqo_running_size = 0;
      dqo_suspend_cnt_size = 0;
      dqo_target_queue_size = 0;
      dqo_priority_size = 0;
    }

    bool IsValid() { return dqo_version != UINT16_MAX; }

    bool LabelIsValid() { return dqo_label != UINT16_MAX; }
  };

  struct LibdispatchVoucherOffsets {
    uint16_t vo_version = UINT16_MAX;
    uint16_t vo_activity_ids_count = UINT16_MAX;
    uint16_t vo_activity_ids_count_size = UINT16_MAX;
    uint16_t vo_activity_ids_array = UINT16_MAX;
    uint16_t vo_activity_ids_array_entry_size = UINT16_MAX;

    LibdispatchVoucherOffsets() = default;

    bool IsValid() { return vo_version != UINT16_MAX; }
  };

  struct LibdispatchTSDIndexes {
    uint16_t dti_version = UINT16_MAX;
    uint64_t dti_queue_index = UINT64_MAX;
    uint64_t dti_voucher_index = UINT64_MAX;
    uint64_t dti_qos_class_index = UINT64_MAX;

    LibdispatchTSDIndexes() = default;

    bool IsValid() { return dti_version != UINT16_MAX; }
  };

  struct LibpthreadOffsets {
    uint16_t plo_version = UINT16_MAX;
    uint16_t plo_pthread_tsd_base_offset = UINT16_MAX;
    uint16_t plo_pthread_tsd_base_address_offset = UINT16_MAX;
    uint16_t plo_pthread_tsd_entry_size = UINT16_MAX;

    LibpthreadOffsets() = default;

    bool IsValid() { return plo_version != UINT16_MAX; }
  };

  // The libBacktraceRecording function
  // __introspection_dispatch_queue_get_pending_items has
  // two forms.  It can either return a simple array of item_refs (void *) size
  // or it can return
  // a header with uint32_t version, a uint32_t size of item, and then an array
  // of item_refs (void*)
  // and code addresses (void*) for all the pending blocks.

  struct ItemRefAndCodeAddress {
    lldb::addr_t item_ref;
    lldb::addr_t code_address;
  };

  struct PendingItemsForQueue {
    bool new_style; // new-style means both item_refs and code_addresses avail
                    // old-style means only item_refs is filled in
    std::vector<ItemRefAndCodeAddress> item_refs_and_code_addresses;
  };

  bool BacktraceRecordingHeadersInitialized();

  void ReadLibdispatchOffsetsAddress();

  void ReadLibdispatchOffsets();

  void ReadLibpthreadOffsetsAddress();

  void ReadLibpthreadOffsets();

  void ReadLibdispatchTSDIndexesAddress();

  void ReadLibdispatchTSDIndexes();

  PendingItemsForQueue GetPendingItemRefsForQueue(lldb::addr_t queue);

  ItemInfo ExtractItemInfoFromBuffer(lldb_private::DataExtractor &extractor);

  lldb_private::AppleGetQueuesHandler m_get_queues_handler;
  lldb_private::AppleGetPendingItemsHandler m_get_pending_items_handler;
  lldb_private::AppleGetItemInfoHandler m_get_item_info_handler;
  lldb_private::AppleGetThreadItemInfoHandler m_get_thread_item_info_handler;

  lldb::addr_t m_page_to_free;
  uint64_t m_page_to_free_size;
  libBacktraceRecording_info m_lib_backtrace_recording_info;

  lldb::addr_t m_dispatch_queue_offsets_addr;
  struct LibdispatchOffsets m_libdispatch_offsets;

  lldb::addr_t m_libpthread_layout_offsets_addr;
  struct LibpthreadOffsets m_libpthread_offsets;

  lldb::addr_t m_dispatch_tsd_indexes_addr;
  struct LibdispatchTSDIndexes m_libdispatch_tsd_indexes;

  lldb::addr_t m_dispatch_voucher_offsets_addr;
  struct LibdispatchVoucherOffsets m_libdispatch_voucher_offsets;

  SystemRuntimeMacOSX(const SystemRuntimeMacOSX &) = delete;
  const SystemRuntimeMacOSX &operator=(const SystemRuntimeMacOSX &) = delete;
};

#endif // LLDB_SOURCE_PLUGINS_SYSTEMRUNTIME_MACOSX_SYSTEMRUNTIMEMACOSX_H
