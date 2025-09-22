//===-- ObjectContainer.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/ObjectContainer.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;

ObjectContainer::ObjectContainer(const lldb::ModuleSP &module_sp,
                                 const FileSpec *file,
                                 lldb::offset_t file_offset,
                                 lldb::offset_t length,
                                 lldb::DataBufferSP data_sp,
                                 lldb::offset_t data_offset)
    : ModuleChild(module_sp),
      m_file(), // This file can be different than the module's file spec
      m_offset(file_offset), m_length(length) {
  if (file)
    m_file = *file;
  if (data_sp)
    m_data.SetData(data_sp, data_offset, length);
}

ObjectContainerSP ObjectContainer::FindPlugin(const lldb::ModuleSP &module_sp,
                                              const ProcessSP &process_sp,
                                              lldb::addr_t header_addr,
                                              WritableDataBufferSP data_sp) {
  if (!module_sp)
    return {};

  LLDB_SCOPED_TIMERF("ObjectContainer::FindPlugin (module = "
                     "%s, process = %p, header_addr = "
                     "0x%" PRIx64 ")",
                     module_sp->GetFileSpec().GetPath().c_str(),
                     static_cast<void *>(process_sp.get()), header_addr);

  ObjectContainerCreateMemoryInstance create_callback;
  for (size_t idx = 0;
       (create_callback =
            PluginManager::GetObjectContainerCreateMemoryCallbackAtIndex(
                idx)) != nullptr;
       ++idx) {
    ObjectContainerSP object_container_sp(
        create_callback(module_sp, data_sp, process_sp, header_addr));
    if (object_container_sp)
      return object_container_sp;
  }

  return {};
}
