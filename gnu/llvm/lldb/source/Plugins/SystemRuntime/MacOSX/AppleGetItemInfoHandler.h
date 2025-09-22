//===-- AppleGetItemInfoHandler.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYSTEMRUNTIME_MACOSX_APPLEGETITEMINFOHANDLER_H
#define LLDB_SOURCE_PLUGINS_SYSTEMRUNTIME_MACOSX_APPLEGETITEMINFOHANDLER_H

#include <map>
#include <mutex>
#include <vector>

#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-public.h"

// This class will insert a UtilityFunction into the inferior process for
// calling libBacktraceRecording's
// __introspection_dispatch_queue_item_get_info()
// function.  The function in the inferior will return a struct by value
// with these members:
//
//     struct get_item_info_return_values
//     {
//         introspection_dispatch_item_info_ref *item_buffer;
//         uint64_t item_buffer_size;
//     };
//
// The item_buffer pointer is an address in the inferior program's address
// space (item_buffer_size in size) which must be mach_vm_deallocate'd by
// lldb.
//
// The AppleGetItemInfoHandler object should persist so that the UtilityFunction
// can be reused multiple times.

namespace lldb_private {

class AppleGetItemInfoHandler {
public:
  AppleGetItemInfoHandler(lldb_private::Process *process);

  ~AppleGetItemInfoHandler();

  struct GetItemInfoReturnInfo {
    lldb::addr_t item_buffer_ptr = LLDB_INVALID_ADDRESS; /* the address of the
                                     item buffer from libBacktraceRecording */
    lldb::addr_t item_buffer_size = 0; /* the size of the item buffer from
                                      libBacktraceRecording */

    GetItemInfoReturnInfo() = default;
  };

  /// Get the information about a work item by calling
  /// __introspection_dispatch_queue_item_get_info.  If there's a page of
  /// memory that needs to be freed, pass in the address and size and it will
  /// be freed before getting the list of queues.
  ///
  /// \param [in] thread
  ///     The thread to run this plan on.
  ///
  /// \param [in] item
  ///     The introspection_dispatch_item_info_ref value for the item of
  ///     interest.
  ///
  /// \param [in] page_to_free
  ///     An address of an inferior process vm page that needs to be
  ///     deallocated,
  ///     LLDB_INVALID_ADDRESS if this is not needed.
  ///
  /// \param [in] page_to_free_size
  ///     The size of the vm page that needs to be deallocated if an address was
  ///     passed in to page_to_free.
  ///
  /// \param [out] error
  ///     This object will be updated with the error status / error string from
  ///     any failures encountered.
  ///
  /// \returns
  ///     The result of the inferior function call execution.  If there was a
  ///     failure of any kind while getting
  ///     the information, the item_buffer_ptr value will be
  ///     LLDB_INVALID_ADDRESS.
  GetItemInfoReturnInfo GetItemInfo(Thread &thread, lldb::addr_t item,
                                    lldb::addr_t page_to_free,
                                    uint64_t page_to_free_size,
                                    lldb_private::Status &error);

  void Detach();

private:
  lldb::addr_t SetupGetItemInfoFunction(Thread &thread,
                                        ValueList &get_item_info_arglist);

  static const char *g_get_item_info_function_name;
  static const char *g_get_item_info_function_code;

  lldb_private::Process *m_process;
  std::unique_ptr<UtilityFunction> m_get_item_info_impl_code;
  std::mutex m_get_item_info_function_mutex;

  lldb::addr_t m_get_item_info_return_buffer_addr;
  std::mutex m_get_item_info_retbuffer_mutex;
};

} // using namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYSTEMRUNTIME_MACOSX_APPLEGETITEMINFOHANDLER_H
