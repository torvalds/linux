//===-- SystemRuntimeMacOSX.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Process/Utility/HistoryThread.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/ProcessStructReader.h"
#include "lldb/Target/Queue.h"
#include "lldb/Target/QueueList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "SystemRuntimeMacOSX.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(SystemRuntimeMacOSX)

// Create an instance of this class. This function is filled into the plugin
// info class that gets handed out by the plugin factory and allows the lldb to
// instantiate an instance of this class.
SystemRuntime *SystemRuntimeMacOSX::CreateInstance(Process *process) {
  bool create = false;
  if (!create) {
    create = true;
    Module *exe_module = process->GetTarget().GetExecutableModulePointer();
    if (exe_module) {
      ObjectFile *object_file = exe_module->GetObjectFile();
      if (object_file) {
        create = (object_file->GetStrata() == ObjectFile::eStrataUser);
      }
    }

    if (create) {
      const llvm::Triple &triple_ref =
          process->GetTarget().GetArchitecture().GetTriple();
      switch (triple_ref.getOS()) {
      case llvm::Triple::Darwin:
      case llvm::Triple::MacOSX:
      case llvm::Triple::IOS:
      case llvm::Triple::TvOS:
      case llvm::Triple::WatchOS:
      case llvm::Triple::XROS:
      case llvm::Triple::BridgeOS:
        create = triple_ref.getVendor() == llvm::Triple::Apple;
        break;
      default:
        create = false;
        break;
      }
    }
  }

  if (create)
    return new SystemRuntimeMacOSX(process);
  return nullptr;
}

// Constructor
SystemRuntimeMacOSX::SystemRuntimeMacOSX(Process *process)
    : SystemRuntime(process), m_break_id(LLDB_INVALID_BREAK_ID), m_mutex(),
      m_get_queues_handler(process), m_get_pending_items_handler(process),
      m_get_item_info_handler(process), m_get_thread_item_info_handler(process),
      m_page_to_free(LLDB_INVALID_ADDRESS), m_page_to_free_size(0),
      m_lib_backtrace_recording_info(),
      m_dispatch_queue_offsets_addr(LLDB_INVALID_ADDRESS),
      m_libdispatch_offsets(),
      m_libpthread_layout_offsets_addr(LLDB_INVALID_ADDRESS),
      m_libpthread_offsets(), m_dispatch_tsd_indexes_addr(LLDB_INVALID_ADDRESS),
      m_libdispatch_tsd_indexes(),
      m_dispatch_voucher_offsets_addr(LLDB_INVALID_ADDRESS),
      m_libdispatch_voucher_offsets() {}

// Destructor
SystemRuntimeMacOSX::~SystemRuntimeMacOSX() { Clear(true); }

void SystemRuntimeMacOSX::Detach() {
  m_get_queues_handler.Detach();
  m_get_pending_items_handler.Detach();
  m_get_item_info_handler.Detach();
  m_get_thread_item_info_handler.Detach();
}

// Clear out the state of this class.
void SystemRuntimeMacOSX::Clear(bool clear_process) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (m_process->IsAlive() && LLDB_BREAK_ID_IS_VALID(m_break_id))
    m_process->ClearBreakpointSiteByID(m_break_id);

  if (clear_process)
    m_process = nullptr;
  m_break_id = LLDB_INVALID_BREAK_ID;
}

std::string
SystemRuntimeMacOSX::GetQueueNameFromThreadQAddress(addr_t dispatch_qaddr) {
  std::string dispatch_queue_name;
  if (dispatch_qaddr == LLDB_INVALID_ADDRESS || dispatch_qaddr == 0)
    return "";

  ReadLibdispatchOffsets();
  if (m_libdispatch_offsets.IsValid()) {
    // dispatch_qaddr is from a thread_info(THREAD_IDENTIFIER_INFO) call for a
    // thread - deref it to get the address of the dispatch_queue_t structure
    // for this thread's queue.
    Status error;
    addr_t dispatch_queue_addr =
        m_process->ReadPointerFromMemory(dispatch_qaddr, error);
    if (error.Success()) {
      if (m_libdispatch_offsets.dqo_version >= 4) {
        // libdispatch versions 4+, pointer to dispatch name is in the queue
        // structure.
        addr_t pointer_to_label_address =
            dispatch_queue_addr + m_libdispatch_offsets.dqo_label;
        addr_t label_addr =
            m_process->ReadPointerFromMemory(pointer_to_label_address, error);
        if (error.Success()) {
          m_process->ReadCStringFromMemory(label_addr, dispatch_queue_name,
                                           error);
        }
      } else {
        // libdispatch versions 1-3, dispatch name is a fixed width char array
        // in the queue structure.
        addr_t label_addr =
            dispatch_queue_addr + m_libdispatch_offsets.dqo_label;
        dispatch_queue_name.resize(m_libdispatch_offsets.dqo_label_size, '\0');
        size_t bytes_read =
            m_process->ReadMemory(label_addr, &dispatch_queue_name[0],
                                  m_libdispatch_offsets.dqo_label_size, error);
        if (bytes_read < m_libdispatch_offsets.dqo_label_size)
          dispatch_queue_name.erase(bytes_read);
      }
    }
  }
  return dispatch_queue_name;
}

lldb::addr_t SystemRuntimeMacOSX::GetLibdispatchQueueAddressFromThreadQAddress(
    addr_t dispatch_qaddr) {
  addr_t libdispatch_queue_t_address = LLDB_INVALID_ADDRESS;
  Status error;
  libdispatch_queue_t_address =
      m_process->ReadPointerFromMemory(dispatch_qaddr, error);
  if (!error.Success()) {
    libdispatch_queue_t_address = LLDB_INVALID_ADDRESS;
  }
  return libdispatch_queue_t_address;
}

lldb::QueueKind SystemRuntimeMacOSX::GetQueueKind(addr_t dispatch_queue_addr) {
  if (dispatch_queue_addr == LLDB_INVALID_ADDRESS || dispatch_queue_addr == 0)
    return eQueueKindUnknown;

  QueueKind kind = eQueueKindUnknown;
  ReadLibdispatchOffsets();
  if (m_libdispatch_offsets.IsValid() &&
      m_libdispatch_offsets.dqo_version >= 4) {
    Status error;
    uint64_t width = m_process->ReadUnsignedIntegerFromMemory(
        dispatch_queue_addr + m_libdispatch_offsets.dqo_width,
        m_libdispatch_offsets.dqo_width_size, 0, error);
    if (error.Success()) {
      if (width == 1) {
        kind = eQueueKindSerial;
      }
      if (width > 1) {
        kind = eQueueKindConcurrent;
      }
    }
  }
  return kind;
}

void SystemRuntimeMacOSX::AddThreadExtendedInfoPacketHints(
    lldb_private::StructuredData::ObjectSP dict_sp) {
  StructuredData::Dictionary *dict = dict_sp->GetAsDictionary();
  if (dict) {
    ReadLibpthreadOffsets();
    if (m_libpthread_offsets.IsValid()) {
      dict->AddIntegerItem("plo_pthread_tsd_base_offset",
                           m_libpthread_offsets.plo_pthread_tsd_base_offset);
      dict->AddIntegerItem(
          "plo_pthread_tsd_base_address_offset",
          m_libpthread_offsets.plo_pthread_tsd_base_address_offset);
      dict->AddIntegerItem("plo_pthread_tsd_entry_size",
                           m_libpthread_offsets.plo_pthread_tsd_entry_size);
    }

    ReadLibdispatchTSDIndexes();
    if (m_libdispatch_tsd_indexes.IsValid()) {
      dict->AddIntegerItem("dti_queue_index",
                           m_libdispatch_tsd_indexes.dti_queue_index);
      dict->AddIntegerItem("dti_voucher_index",
                           m_libdispatch_tsd_indexes.dti_voucher_index);
      dict->AddIntegerItem("dti_qos_class_index",
                           m_libdispatch_tsd_indexes.dti_qos_class_index);
    }
  }
}

bool SystemRuntimeMacOSX::SafeToCallFunctionsOnThisThread(ThreadSP thread_sp) {
  if (thread_sp && thread_sp->GetFrameWithConcreteFrameIndex(0)) {
    const SymbolContext sym_ctx(
        thread_sp->GetFrameWithConcreteFrameIndex(0)->GetSymbolContext(
            eSymbolContextSymbol));
    static ConstString g_select_symbol("__select");
    if (sym_ctx.GetFunctionName() == g_select_symbol) {
      return false;
    }
  }
  return true;
}

lldb::queue_id_t
SystemRuntimeMacOSX::GetQueueIDFromThreadQAddress(lldb::addr_t dispatch_qaddr) {
  queue_id_t queue_id = LLDB_INVALID_QUEUE_ID;

  if (dispatch_qaddr == LLDB_INVALID_ADDRESS || dispatch_qaddr == 0)
    return queue_id;

  ReadLibdispatchOffsets();
  if (m_libdispatch_offsets.IsValid()) {
    // dispatch_qaddr is from a thread_info(THREAD_IDENTIFIER_INFO) call for a
    // thread - deref it to get the address of the dispatch_queue_t structure
    // for this thread's queue.
    Status error;
    uint64_t dispatch_queue_addr =
        m_process->ReadPointerFromMemory(dispatch_qaddr, error);
    if (error.Success()) {
      addr_t serialnum_address =
          dispatch_queue_addr + m_libdispatch_offsets.dqo_serialnum;
      queue_id_t serialnum = m_process->ReadUnsignedIntegerFromMemory(
          serialnum_address, m_libdispatch_offsets.dqo_serialnum_size,
          LLDB_INVALID_QUEUE_ID, error);
      if (error.Success()) {
        queue_id = serialnum;
      }
    }
  }

  return queue_id;
}

void SystemRuntimeMacOSX::ReadLibdispatchOffsetsAddress() {
  if (m_dispatch_queue_offsets_addr != LLDB_INVALID_ADDRESS)
    return;

  static ConstString g_dispatch_queue_offsets_symbol_name(
      "dispatch_queue_offsets");
  const Symbol *dispatch_queue_offsets_symbol = nullptr;

  // libdispatch symbols were in libSystem.B.dylib up through Mac OS X 10.6
  // ("Snow Leopard")
  ModuleSpec libSystem_module_spec(FileSpec("libSystem.B.dylib"));
  ModuleSP module_sp(m_process->GetTarget().GetImages().FindFirstModule(
      libSystem_module_spec));
  if (module_sp)
    dispatch_queue_offsets_symbol = module_sp->FindFirstSymbolWithNameAndType(
        g_dispatch_queue_offsets_symbol_name, eSymbolTypeData);

  // libdispatch symbols are in their own dylib as of Mac OS X 10.7 ("Lion")
  // and later
  if (dispatch_queue_offsets_symbol == nullptr) {
    ModuleSpec libdispatch_module_spec(FileSpec("libdispatch.dylib"));
    module_sp = m_process->GetTarget().GetImages().FindFirstModule(
        libdispatch_module_spec);
    if (module_sp)
      dispatch_queue_offsets_symbol = module_sp->FindFirstSymbolWithNameAndType(
          g_dispatch_queue_offsets_symbol_name, eSymbolTypeData);
  }
  if (dispatch_queue_offsets_symbol)
    m_dispatch_queue_offsets_addr =
        dispatch_queue_offsets_symbol->GetLoadAddress(&m_process->GetTarget());
}

void SystemRuntimeMacOSX::ReadLibdispatchOffsets() {
  if (m_libdispatch_offsets.IsValid())
    return;

  ReadLibdispatchOffsetsAddress();

  uint8_t memory_buffer[sizeof(struct LibdispatchOffsets)];
  DataExtractor data(memory_buffer, sizeof(memory_buffer),
                     m_process->GetByteOrder(),
                     m_process->GetAddressByteSize());

  Status error;
  if (m_process->ReadMemory(m_dispatch_queue_offsets_addr, memory_buffer,
                            sizeof(memory_buffer),
                            error) == sizeof(memory_buffer)) {
    lldb::offset_t data_offset = 0;

    // The struct LibdispatchOffsets is a series of uint16_t's - extract them
    // all in one big go.
    data.GetU16(&data_offset, &m_libdispatch_offsets.dqo_version,
                sizeof(struct LibdispatchOffsets) / sizeof(uint16_t));
  }
}

void SystemRuntimeMacOSX::ReadLibpthreadOffsetsAddress() {
  if (m_libpthread_layout_offsets_addr != LLDB_INVALID_ADDRESS)
    return;

  static ConstString g_libpthread_layout_offsets_symbol_name(
      "pthread_layout_offsets");
  const Symbol *libpthread_layout_offsets_symbol = nullptr;

  ModuleSpec libpthread_module_spec(FileSpec("libsystem_pthread.dylib"));
  ModuleSP module_sp(m_process->GetTarget().GetImages().FindFirstModule(
      libpthread_module_spec));
  if (module_sp) {
    libpthread_layout_offsets_symbol =
        module_sp->FindFirstSymbolWithNameAndType(
            g_libpthread_layout_offsets_symbol_name, eSymbolTypeData);
    if (libpthread_layout_offsets_symbol) {
      m_libpthread_layout_offsets_addr =
          libpthread_layout_offsets_symbol->GetLoadAddress(
              &m_process->GetTarget());
    }
  }
}

void SystemRuntimeMacOSX::ReadLibpthreadOffsets() {
  if (m_libpthread_offsets.IsValid())
    return;

  ReadLibpthreadOffsetsAddress();

  if (m_libpthread_layout_offsets_addr != LLDB_INVALID_ADDRESS) {
    uint8_t memory_buffer[sizeof(struct LibpthreadOffsets)];
    DataExtractor data(memory_buffer, sizeof(memory_buffer),
                       m_process->GetByteOrder(),
                       m_process->GetAddressByteSize());
    Status error;
    if (m_process->ReadMemory(m_libpthread_layout_offsets_addr, memory_buffer,
                              sizeof(memory_buffer),
                              error) == sizeof(memory_buffer)) {
      lldb::offset_t data_offset = 0;

      // The struct LibpthreadOffsets is a series of uint16_t's - extract them
      // all in one big go.
      data.GetU16(&data_offset, &m_libpthread_offsets.plo_version,
                  sizeof(struct LibpthreadOffsets) / sizeof(uint16_t));
    }
  }
}

void SystemRuntimeMacOSX::ReadLibdispatchTSDIndexesAddress() {
  if (m_dispatch_tsd_indexes_addr != LLDB_INVALID_ADDRESS)
    return;

  static ConstString g_libdispatch_tsd_indexes_symbol_name(
      "dispatch_tsd_indexes");
  const Symbol *libdispatch_tsd_indexes_symbol = nullptr;

  ModuleSpec libpthread_module_spec(FileSpec("libdispatch.dylib"));
  ModuleSP module_sp(m_process->GetTarget().GetImages().FindFirstModule(
      libpthread_module_spec));
  if (module_sp) {
    libdispatch_tsd_indexes_symbol = module_sp->FindFirstSymbolWithNameAndType(
        g_libdispatch_tsd_indexes_symbol_name, eSymbolTypeData);
    if (libdispatch_tsd_indexes_symbol) {
      m_dispatch_tsd_indexes_addr =
          libdispatch_tsd_indexes_symbol->GetLoadAddress(
              &m_process->GetTarget());
    }
  }
}

void SystemRuntimeMacOSX::ReadLibdispatchTSDIndexes() {
  if (m_libdispatch_tsd_indexes.IsValid())
    return;

  ReadLibdispatchTSDIndexesAddress();

  if (m_dispatch_tsd_indexes_addr != LLDB_INVALID_ADDRESS) {

// We don't need to check the version number right now, it will be at least 2,
// but keep this code around to fetch just the version # for the future where
// we need to fetch alternate versions of the struct.
#if 0
        uint16_t dti_version = 2;
        Address dti_struct_addr;
        if (m_process->GetTarget().ResolveLoadAddress (m_dispatch_tsd_indexes_addr, dti_struct_addr))
        {
            Status error;
            uint16_t version = m_process->GetTarget().ReadUnsignedIntegerFromMemory (dti_struct_addr, false, 2, UINT16_MAX, error);
            if (error.Success() && dti_version != UINT16_MAX)
            {
                dti_version = version;
            }
        }
#endif

    TypeSystemClangSP scratch_ts_sp =
        ScratchTypeSystemClang::GetForTarget(m_process->GetTarget());
    if (m_dispatch_tsd_indexes_addr != LLDB_INVALID_ADDRESS) {
      CompilerType uint16 =
          scratch_ts_sp->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 16);
      CompilerType dispatch_tsd_indexes_s = scratch_ts_sp->CreateRecordType(
          nullptr, OptionalClangModuleID(), lldb::eAccessPublic,
          "__lldb_dispatch_tsd_indexes_s",
          llvm::to_underlying(clang::TagTypeKind::Struct),
          lldb::eLanguageTypeC);

      TypeSystemClang::StartTagDeclarationDefinition(dispatch_tsd_indexes_s);
      TypeSystemClang::AddFieldToRecordType(dispatch_tsd_indexes_s,
                                            "dti_version", uint16,
                                            lldb::eAccessPublic, 0);
      TypeSystemClang::AddFieldToRecordType(dispatch_tsd_indexes_s,
                                            "dti_queue_index", uint16,
                                            lldb::eAccessPublic, 0);
      TypeSystemClang::AddFieldToRecordType(dispatch_tsd_indexes_s,
                                            "dti_voucher_index", uint16,
                                            lldb::eAccessPublic, 0);
      TypeSystemClang::AddFieldToRecordType(dispatch_tsd_indexes_s,
                                            "dti_qos_class_index", uint16,
                                            lldb::eAccessPublic, 0);
      TypeSystemClang::CompleteTagDeclarationDefinition(dispatch_tsd_indexes_s);

      ProcessStructReader struct_reader(m_process, m_dispatch_tsd_indexes_addr,
                                        dispatch_tsd_indexes_s);

      m_libdispatch_tsd_indexes.dti_version =
          struct_reader.GetField<uint16_t>("dti_version");
      m_libdispatch_tsd_indexes.dti_queue_index =
          struct_reader.GetField<uint16_t>("dti_queue_index");
      m_libdispatch_tsd_indexes.dti_voucher_index =
          struct_reader.GetField<uint16_t>("dti_voucher_index");
      m_libdispatch_tsd_indexes.dti_qos_class_index =
          struct_reader.GetField<uint16_t>("dti_qos_class_index");
    }
  }
}

ThreadSP SystemRuntimeMacOSX::GetExtendedBacktraceThread(ThreadSP real_thread,
                                                         ConstString type) {
  ThreadSP originating_thread_sp;
  if (BacktraceRecordingHeadersInitialized() && type == "libdispatch") {
    Status error;

    // real_thread is either an actual, live thread (in which case we need to
    // call into libBacktraceRecording to find its originator) or it is an
    // extended backtrace itself, in which case we get the token from it and
    // call into libBacktraceRecording to find the originator of that token.

    if (real_thread->GetExtendedBacktraceToken() != LLDB_INVALID_ADDRESS) {
      originating_thread_sp = GetExtendedBacktraceFromItemRef(
          real_thread->GetExtendedBacktraceToken());
    } else {
      ThreadSP cur_thread_sp(
          m_process->GetThreadList().GetExpressionExecutionThread());
      AppleGetThreadItemInfoHandler::GetThreadItemInfoReturnInfo ret =
          m_get_thread_item_info_handler.GetThreadItemInfo(
              *cur_thread_sp.get(), real_thread->GetID(), m_page_to_free,
              m_page_to_free_size, error);
      m_page_to_free = LLDB_INVALID_ADDRESS;
      m_page_to_free_size = 0;
      if (ret.item_buffer_ptr != 0 &&
          ret.item_buffer_ptr != LLDB_INVALID_ADDRESS &&
          ret.item_buffer_size > 0) {
        DataBufferHeap data(ret.item_buffer_size, 0);
        if (m_process->ReadMemory(ret.item_buffer_ptr, data.GetBytes(),
                                  ret.item_buffer_size, error) &&
            error.Success()) {
          DataExtractor extractor(data.GetBytes(), data.GetByteSize(),
                                  m_process->GetByteOrder(),
                                  m_process->GetAddressByteSize());
          ItemInfo item = ExtractItemInfoFromBuffer(extractor);
          originating_thread_sp = std::make_shared<HistoryThread>(
              *m_process, item.enqueuing_thread_id, item.enqueuing_callstack);
          originating_thread_sp->SetExtendedBacktraceToken(
              item.item_that_enqueued_this);
          originating_thread_sp->SetQueueName(
              item.enqueuing_queue_label.c_str());
          originating_thread_sp->SetQueueID(item.enqueuing_queue_serialnum);
          //                    originating_thread_sp->SetThreadName
          //                    (item.enqueuing_thread_label.c_str());
        }
        m_page_to_free = ret.item_buffer_ptr;
        m_page_to_free_size = ret.item_buffer_size;
      }
    }
  } else if (type == "Application Specific Backtrace") {
    StructuredData::ObjectSP thread_extended_sp =
        real_thread->GetExtendedInfo();

    if (!thread_extended_sp)
      return {};

    StructuredData::Array *thread_extended_info =
        thread_extended_sp->GetAsArray();

    if (!thread_extended_info || !thread_extended_info->GetSize())
      return {};

    std::vector<addr_t> app_specific_backtrace_pcs;

    auto extract_frame_pc =
        [&app_specific_backtrace_pcs](StructuredData::Object *obj) -> bool {
      if (!obj)
        return false;

      StructuredData::Dictionary *dict = obj->GetAsDictionary();
      if (!dict)
        return false;

      lldb::addr_t pc = LLDB_INVALID_ADDRESS;
      if (!dict->GetValueForKeyAsInteger("pc", pc))
        return false;

      app_specific_backtrace_pcs.push_back(pc);

      return pc != LLDB_INVALID_ADDRESS;
    };

    if (!thread_extended_info->ForEach(extract_frame_pc))
      return {};

    originating_thread_sp =
        std::make_shared<HistoryThread>(*m_process, real_thread->GetIndexID(),
                                        app_specific_backtrace_pcs, true);
    originating_thread_sp->SetQueueName(type.AsCString());
  }
  return originating_thread_sp;
}

ThreadSP
SystemRuntimeMacOSX::GetExtendedBacktraceFromItemRef(lldb::addr_t item_ref) {
  ThreadSP return_thread_sp;

  AppleGetItemInfoHandler::GetItemInfoReturnInfo ret;
  ThreadSP cur_thread_sp(
      m_process->GetThreadList().GetExpressionExecutionThread());
  Status error;
  ret = m_get_item_info_handler.GetItemInfo(*cur_thread_sp.get(), item_ref,
                                            m_page_to_free, m_page_to_free_size,
                                            error);
  m_page_to_free = LLDB_INVALID_ADDRESS;
  m_page_to_free_size = 0;
  if (ret.item_buffer_ptr != 0 && ret.item_buffer_ptr != LLDB_INVALID_ADDRESS &&
      ret.item_buffer_size > 0) {
    DataBufferHeap data(ret.item_buffer_size, 0);
    if (m_process->ReadMemory(ret.item_buffer_ptr, data.GetBytes(),
                              ret.item_buffer_size, error) &&
        error.Success()) {
      DataExtractor extractor(data.GetBytes(), data.GetByteSize(),
                              m_process->GetByteOrder(),
                              m_process->GetAddressByteSize());
      ItemInfo item = ExtractItemInfoFromBuffer(extractor);
      return_thread_sp = std::make_shared<HistoryThread>(
          *m_process, item.enqueuing_thread_id, item.enqueuing_callstack);
      return_thread_sp->SetExtendedBacktraceToken(item.item_that_enqueued_this);
      return_thread_sp->SetQueueName(item.enqueuing_queue_label.c_str());
      return_thread_sp->SetQueueID(item.enqueuing_queue_serialnum);
      //            return_thread_sp->SetThreadName
      //            (item.enqueuing_thread_label.c_str());

      m_page_to_free = ret.item_buffer_ptr;
      m_page_to_free_size = ret.item_buffer_size;
    }
  }
  return return_thread_sp;
}

ThreadSP
SystemRuntimeMacOSX::GetExtendedBacktraceForQueueItem(QueueItemSP queue_item_sp,
                                                      ConstString type) {
  ThreadSP extended_thread_sp;
  if (type != "libdispatch")
    return extended_thread_sp;

  extended_thread_sp = std::make_shared<HistoryThread>(
      *m_process, queue_item_sp->GetEnqueueingThreadID(),
      queue_item_sp->GetEnqueueingBacktrace());
  extended_thread_sp->SetExtendedBacktraceToken(
      queue_item_sp->GetItemThatEnqueuedThis());
  extended_thread_sp->SetQueueName(queue_item_sp->GetQueueLabel().c_str());
  extended_thread_sp->SetQueueID(queue_item_sp->GetEnqueueingQueueID());
  //    extended_thread_sp->SetThreadName
  //    (queue_item_sp->GetThreadLabel().c_str());

  return extended_thread_sp;
}

/* Returns true if we were able to get the version / offset information
 * out of libBacktraceRecording.  false means we were unable to retrieve
 * this; the queue_info_version field will be 0.
 */

bool SystemRuntimeMacOSX::BacktraceRecordingHeadersInitialized() {
  if (m_lib_backtrace_recording_info.queue_info_version != 0)
    return true;

  addr_t queue_info_version_address = LLDB_INVALID_ADDRESS;
  addr_t queue_info_data_offset_address = LLDB_INVALID_ADDRESS;
  addr_t item_info_version_address = LLDB_INVALID_ADDRESS;
  addr_t item_info_data_offset_address = LLDB_INVALID_ADDRESS;
  Target &target = m_process->GetTarget();

  static ConstString introspection_dispatch_queue_info_version(
      "__introspection_dispatch_queue_info_version");
  SymbolContextList sc_list;
  m_process->GetTarget().GetImages().FindSymbolsWithNameAndType(
      introspection_dispatch_queue_info_version, eSymbolTypeData, sc_list);
  if (!sc_list.IsEmpty()) {
    SymbolContext sc;
    sc_list.GetContextAtIndex(0, sc);
    AddressRange addr_range;
    sc.GetAddressRange(eSymbolContextSymbol, 0, false, addr_range);
    queue_info_version_address =
        addr_range.GetBaseAddress().GetLoadAddress(&target);
  }
  sc_list.Clear();

  static ConstString introspection_dispatch_queue_info_data_offset(
      "__introspection_dispatch_queue_info_data_offset");
  m_process->GetTarget().GetImages().FindSymbolsWithNameAndType(
      introspection_dispatch_queue_info_data_offset, eSymbolTypeData, sc_list);
  if (!sc_list.IsEmpty()) {
    SymbolContext sc;
    sc_list.GetContextAtIndex(0, sc);
    AddressRange addr_range;
    sc.GetAddressRange(eSymbolContextSymbol, 0, false, addr_range);
    queue_info_data_offset_address =
        addr_range.GetBaseAddress().GetLoadAddress(&target);
  }
  sc_list.Clear();

  static ConstString introspection_dispatch_item_info_version(
      "__introspection_dispatch_item_info_version");
  m_process->GetTarget().GetImages().FindSymbolsWithNameAndType(
      introspection_dispatch_item_info_version, eSymbolTypeData, sc_list);
  if (!sc_list.IsEmpty()) {
    SymbolContext sc;
    sc_list.GetContextAtIndex(0, sc);
    AddressRange addr_range;
    sc.GetAddressRange(eSymbolContextSymbol, 0, false, addr_range);
    item_info_version_address =
        addr_range.GetBaseAddress().GetLoadAddress(&target);
  }
  sc_list.Clear();

  static ConstString introspection_dispatch_item_info_data_offset(
      "__introspection_dispatch_item_info_data_offset");
  m_process->GetTarget().GetImages().FindSymbolsWithNameAndType(
      introspection_dispatch_item_info_data_offset, eSymbolTypeData, sc_list);
  if (!sc_list.IsEmpty()) {
    SymbolContext sc;
    sc_list.GetContextAtIndex(0, sc);
    AddressRange addr_range;
    sc.GetAddressRange(eSymbolContextSymbol, 0, false, addr_range);
    item_info_data_offset_address =
        addr_range.GetBaseAddress().GetLoadAddress(&target);
  }

  if (queue_info_version_address != LLDB_INVALID_ADDRESS &&
      queue_info_data_offset_address != LLDB_INVALID_ADDRESS &&
      item_info_version_address != LLDB_INVALID_ADDRESS &&
      item_info_data_offset_address != LLDB_INVALID_ADDRESS) {
    Status error;
    m_lib_backtrace_recording_info.queue_info_version =
        m_process->ReadUnsignedIntegerFromMemory(queue_info_version_address, 2,
                                                 0, error);
    if (error.Success()) {
      m_lib_backtrace_recording_info.queue_info_data_offset =
          m_process->ReadUnsignedIntegerFromMemory(
              queue_info_data_offset_address, 2, 0, error);
      if (error.Success()) {
        m_lib_backtrace_recording_info.item_info_version =
            m_process->ReadUnsignedIntegerFromMemory(item_info_version_address,
                                                     2, 0, error);
        if (error.Success()) {
          m_lib_backtrace_recording_info.item_info_data_offset =
              m_process->ReadUnsignedIntegerFromMemory(
                  item_info_data_offset_address, 2, 0, error);
          if (!error.Success()) {
            m_lib_backtrace_recording_info.queue_info_version = 0;
          }
        } else {
          m_lib_backtrace_recording_info.queue_info_version = 0;
        }
      } else {
        m_lib_backtrace_recording_info.queue_info_version = 0;
      }
    }
  }

  return m_lib_backtrace_recording_info.queue_info_version != 0;
}

const std::vector<ConstString> &
SystemRuntimeMacOSX::GetExtendedBacktraceTypes() {
  if (m_types.size() == 0) {
    m_types.push_back(ConstString("libdispatch"));
    m_types.push_back(ConstString("Application Specific Backtrace"));
    // We could have pthread as another type in the future if we have a way of
    // gathering that information & it's useful to distinguish between them.
  }
  return m_types;
}

void SystemRuntimeMacOSX::PopulateQueueList(
    lldb_private::QueueList &queue_list) {
  if (BacktraceRecordingHeadersInitialized()) {
    AppleGetQueuesHandler::GetQueuesReturnInfo queue_info_pointer;
    ThreadSP cur_thread_sp(
        m_process->GetThreadList().GetExpressionExecutionThread());
    if (cur_thread_sp) {
      Status error;
      queue_info_pointer = m_get_queues_handler.GetCurrentQueues(
          *cur_thread_sp.get(), m_page_to_free, m_page_to_free_size, error);
      m_page_to_free = LLDB_INVALID_ADDRESS;
      m_page_to_free_size = 0;
      if (error.Success()) {

        if (queue_info_pointer.count > 0 &&
            queue_info_pointer.queues_buffer_size > 0 &&
            queue_info_pointer.queues_buffer_ptr != 0 &&
            queue_info_pointer.queues_buffer_ptr != LLDB_INVALID_ADDRESS) {
          PopulateQueuesUsingLibBTR(queue_info_pointer.queues_buffer_ptr,
                                    queue_info_pointer.queues_buffer_size,
                                    queue_info_pointer.count, queue_list);
        }
      }
    }
  }

  // We either didn't have libBacktraceRecording (and need to create the queues
  // list based on threads) or we did get the queues list from
  // libBacktraceRecording but some special queues may not be included in its
  // information.  This is needed because libBacktraceRecording will only list
  // queues with pending or running items by default - but the magic com.apple
  // .main-thread queue on thread 1 is always around.

  for (ThreadSP thread_sp : m_process->Threads()) {
    if (thread_sp->GetAssociatedWithLibdispatchQueue() != eLazyBoolNo) {
      if (thread_sp->GetQueueID() != LLDB_INVALID_QUEUE_ID) {
        if (queue_list.FindQueueByID(thread_sp->GetQueueID()).get() ==
            nullptr) {
          QueueSP queue_sp(new Queue(m_process->shared_from_this(),
                                     thread_sp->GetQueueID(),
                                     thread_sp->GetQueueName()));
          if (thread_sp->ThreadHasQueueInformation()) {
            queue_sp->SetKind(thread_sp->GetQueueKind());
            queue_sp->SetLibdispatchQueueAddress(
                thread_sp->GetQueueLibdispatchQueueAddress());
            queue_list.AddQueue(queue_sp);
          } else {
            queue_sp->SetKind(
                GetQueueKind(thread_sp->GetQueueLibdispatchQueueAddress()));
            queue_sp->SetLibdispatchQueueAddress(
                thread_sp->GetQueueLibdispatchQueueAddress());
            queue_list.AddQueue(queue_sp);
          }
        }
      }
    }
  }
}

// Returns either an array of introspection_dispatch_item_info_ref's for the
// pending items on a queue or an array introspection_dispatch_item_info_ref's
// and code addresses for the pending items on a queue.  The information about
// each of these pending items then needs to be fetched individually by passing
// the ref to libBacktraceRecording.

SystemRuntimeMacOSX::PendingItemsForQueue
SystemRuntimeMacOSX::GetPendingItemRefsForQueue(lldb::addr_t queue) {
  PendingItemsForQueue pending_item_refs = {};
  AppleGetPendingItemsHandler::GetPendingItemsReturnInfo pending_items_pointer;
  ThreadSP cur_thread_sp(
      m_process->GetThreadList().GetExpressionExecutionThread());
  if (cur_thread_sp) {
    Status error;
    pending_items_pointer = m_get_pending_items_handler.GetPendingItems(
        *cur_thread_sp.get(), queue, m_page_to_free, m_page_to_free_size,
        error);
    m_page_to_free = LLDB_INVALID_ADDRESS;
    m_page_to_free_size = 0;
    if (error.Success()) {
      if (pending_items_pointer.count > 0 &&
          pending_items_pointer.items_buffer_size > 0 &&
          pending_items_pointer.items_buffer_ptr != 0 &&
          pending_items_pointer.items_buffer_ptr != LLDB_INVALID_ADDRESS) {
        DataBufferHeap data(pending_items_pointer.items_buffer_size, 0);
        if (m_process->ReadMemory(
                pending_items_pointer.items_buffer_ptr, data.GetBytes(),
                pending_items_pointer.items_buffer_size, error)) {
          DataExtractor extractor(data.GetBytes(), data.GetByteSize(),
                                  m_process->GetByteOrder(),
                                  m_process->GetAddressByteSize());

          // We either have an array of
          //    void* item_ref
          // (old style) or we have a structure returned which looks like
          //
          // struct introspection_dispatch_pending_item_info_s {
          //   void *item_ref;
          //   void *function_or_block;
          // };
          //
          // struct introspection_dispatch_pending_items_array_s {
          //   uint32_t version;
          //   uint32_t size_of_item_info;
          //   introspection_dispatch_pending_item_info_s items[];
          //   }

          offset_t offset = 0;
          uint64_t i = 0;
          uint32_t version = extractor.GetU32(&offset);
          if (version == 1) {
            pending_item_refs.new_style = true;
            uint32_t item_size = extractor.GetU32(&offset);
            uint32_t start_of_array_offset = offset;
            while (offset < pending_items_pointer.items_buffer_size &&
                   i < pending_items_pointer.count) {
              offset = start_of_array_offset + (i * item_size);
              ItemRefAndCodeAddress item;
              item.item_ref = extractor.GetAddress(&offset);
              item.code_address = extractor.GetAddress(&offset);
              pending_item_refs.item_refs_and_code_addresses.push_back(item);
              i++;
            }
          } else {
            offset = 0;
            pending_item_refs.new_style = false;
            while (offset < pending_items_pointer.items_buffer_size &&
                   i < pending_items_pointer.count) {
              ItemRefAndCodeAddress item;
              item.item_ref = extractor.GetAddress(&offset);
              item.code_address = LLDB_INVALID_ADDRESS;
              pending_item_refs.item_refs_and_code_addresses.push_back(item);
              i++;
            }
          }
        }
        m_page_to_free = pending_items_pointer.items_buffer_ptr;
        m_page_to_free_size = pending_items_pointer.items_buffer_size;
      }
    }
  }
  return pending_item_refs;
}

void SystemRuntimeMacOSX::PopulatePendingItemsForQueue(Queue *queue) {
  if (BacktraceRecordingHeadersInitialized()) {
    PendingItemsForQueue pending_item_refs =
        GetPendingItemRefsForQueue(queue->GetLibdispatchQueueAddress());
    for (ItemRefAndCodeAddress pending_item :
         pending_item_refs.item_refs_and_code_addresses) {
      Address addr;
      m_process->GetTarget().ResolveLoadAddress(pending_item.code_address,
                                                addr);
      QueueItemSP queue_item_sp(new QueueItem(queue->shared_from_this(),
                                              m_process->shared_from_this(),
                                              pending_item.item_ref, addr));
      queue->PushPendingQueueItem(queue_item_sp);
    }
  }
}

void SystemRuntimeMacOSX::CompleteQueueItem(QueueItem *queue_item,
                                            addr_t item_ref) {
  AppleGetItemInfoHandler::GetItemInfoReturnInfo ret;

  ThreadSP cur_thread_sp(
      m_process->GetThreadList().GetExpressionExecutionThread());
  Status error;
  ret = m_get_item_info_handler.GetItemInfo(*cur_thread_sp.get(), item_ref,
                                            m_page_to_free, m_page_to_free_size,
                                            error);
  m_page_to_free = LLDB_INVALID_ADDRESS;
  m_page_to_free_size = 0;
  if (ret.item_buffer_ptr != 0 && ret.item_buffer_ptr != LLDB_INVALID_ADDRESS &&
      ret.item_buffer_size > 0) {
    DataBufferHeap data(ret.item_buffer_size, 0);
    if (m_process->ReadMemory(ret.item_buffer_ptr, data.GetBytes(),
                              ret.item_buffer_size, error) &&
        error.Success()) {
      DataExtractor extractor(data.GetBytes(), data.GetByteSize(),
                              m_process->GetByteOrder(),
                              m_process->GetAddressByteSize());
      ItemInfo item = ExtractItemInfoFromBuffer(extractor);
      queue_item->SetItemThatEnqueuedThis(item.item_that_enqueued_this);
      queue_item->SetEnqueueingThreadID(item.enqueuing_thread_id);
      queue_item->SetEnqueueingQueueID(item.enqueuing_queue_serialnum);
      queue_item->SetStopID(item.stop_id);
      queue_item->SetEnqueueingBacktrace(item.enqueuing_callstack);
      queue_item->SetThreadLabel(item.enqueuing_thread_label);
      queue_item->SetQueueLabel(item.enqueuing_queue_label);
      queue_item->SetTargetQueueLabel(item.target_queue_label);
    }
    m_page_to_free = ret.item_buffer_ptr;
    m_page_to_free_size = ret.item_buffer_size;
  }
}

void SystemRuntimeMacOSX::PopulateQueuesUsingLibBTR(
    lldb::addr_t queues_buffer, uint64_t queues_buffer_size, uint64_t count,
    lldb_private::QueueList &queue_list) {
  Status error;
  DataBufferHeap data(queues_buffer_size, 0);
  Log *log = GetLog(LLDBLog::SystemRuntime);
  if (m_process->ReadMemory(queues_buffer, data.GetBytes(), queues_buffer_size,
                            error) == queues_buffer_size &&
      error.Success()) {
    // We've read the information out of inferior memory; free it on the next
    // call we make
    m_page_to_free = queues_buffer;
    m_page_to_free_size = queues_buffer_size;

    DataExtractor extractor(data.GetBytes(), data.GetByteSize(),
                            m_process->GetByteOrder(),
                            m_process->GetAddressByteSize());
    offset_t offset = 0;
    uint64_t queues_read = 0;

    // The information about the queues is stored in this format (v1): typedef
    // struct introspection_dispatch_queue_info_s {
    //     uint32_t offset_to_next;
    //     dispatch_queue_t queue;
    //     uint64_t serialnum;     // queue's serialnum in the process, as
    //     provided by libdispatch
    //     uint32_t running_work_items_count;
    //     uint32_t pending_work_items_count;
    //
    //     char data[];     // Starting here, we have variable-length data:
    //     // char queue_label[];
    // } introspection_dispatch_queue_info_s;

    while (queues_read < count && offset < queues_buffer_size) {
      offset_t start_of_this_item = offset;

      uint32_t offset_to_next = extractor.GetU32(&offset);

      offset += 4; // Skip over the 4 bytes of reserved space
      addr_t queue = extractor.GetAddress(&offset);
      uint64_t serialnum = extractor.GetU64(&offset);
      uint32_t running_work_items_count = extractor.GetU32(&offset);
      uint32_t pending_work_items_count = extractor.GetU32(&offset);

      // Read the first field of the variable length data
      offset = start_of_this_item +
               m_lib_backtrace_recording_info.queue_info_data_offset;
      const char *queue_label = extractor.GetCStr(&offset);
      if (queue_label == nullptr)
        queue_label = "";

      offset_t start_of_next_item = start_of_this_item + offset_to_next;
      offset = start_of_next_item;

      LLDB_LOGF(log,
                "SystemRuntimeMacOSX::PopulateQueuesUsingLibBTR added "
                "queue with dispatch_queue_t 0x%" PRIx64
                ", serial number 0x%" PRIx64
                ", running items %d, pending items %d, name '%s'",
                queue, serialnum, running_work_items_count,
                pending_work_items_count, queue_label);

      QueueSP queue_sp(
          new Queue(m_process->shared_from_this(), serialnum, queue_label));
      queue_sp->SetNumRunningWorkItems(running_work_items_count);
      queue_sp->SetNumPendingWorkItems(pending_work_items_count);
      queue_sp->SetLibdispatchQueueAddress(queue);
      queue_sp->SetKind(GetQueueKind(queue));
      queue_list.AddQueue(queue_sp);
      queues_read++;
    }
  }
}

SystemRuntimeMacOSX::ItemInfo SystemRuntimeMacOSX::ExtractItemInfoFromBuffer(
    lldb_private::DataExtractor &extractor) {
  ItemInfo item;

  offset_t offset = 0;

  item.item_that_enqueued_this = extractor.GetAddress(&offset);
  item.function_or_block = extractor.GetAddress(&offset);
  item.enqueuing_thread_id = extractor.GetU64(&offset);
  item.enqueuing_queue_serialnum = extractor.GetU64(&offset);
  item.target_queue_serialnum = extractor.GetU64(&offset);
  item.enqueuing_callstack_frame_count = extractor.GetU32(&offset);
  item.stop_id = extractor.GetU32(&offset);

  offset = m_lib_backtrace_recording_info.item_info_data_offset;

  for (uint32_t i = 0; i < item.enqueuing_callstack_frame_count; i++) {
    item.enqueuing_callstack.push_back(extractor.GetAddress(&offset));
  }
  item.enqueuing_thread_label = extractor.GetCStr(&offset);
  item.enqueuing_queue_label = extractor.GetCStr(&offset);
  item.target_queue_label = extractor.GetCStr(&offset);

  return item;
}

void SystemRuntimeMacOSX::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(),
      "System runtime plugin for Mac OS X native libraries.", CreateInstance);
}

void SystemRuntimeMacOSX::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
