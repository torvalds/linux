//===-- DynamicLoaderMacOSXDYLD.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DynamicLoaderMacOSXDYLD.h"
#include "DynamicLoaderDarwin.h"
#include "DynamicLoaderMacOS.h"
#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

//#define ENABLE_DEBUG_PRINTF // COMMENT THIS LINE OUT PRIOR TO CHECKIN
#ifdef ENABLE_DEBUG_PRINTF
#include <cstdio>
#define DEBUG_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

#ifndef __APPLE__
#include "lldb/Utility/AppleUuidCompatibility.h"
#else
#include <uuid/uuid.h>
#endif

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(DynamicLoaderMacOSXDYLD)

// Create an instance of this class. This function is filled into the plugin
// info class that gets handed out by the plugin factory and allows the lldb to
// instantiate an instance of this class.
DynamicLoader *DynamicLoaderMacOSXDYLD::CreateInstance(Process *process,
                                                       bool force) {
  bool create = force;
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

  if (UseDYLDSPI(process)) {
    create = false;
  }

  if (create)
    return new DynamicLoaderMacOSXDYLD(process);
  return nullptr;
}

// Constructor
DynamicLoaderMacOSXDYLD::DynamicLoaderMacOSXDYLD(Process *process)
    : DynamicLoaderDarwin(process),
      m_dyld_all_image_infos_addr(LLDB_INVALID_ADDRESS),
      m_dyld_all_image_infos(), m_dyld_all_image_infos_stop_id(UINT32_MAX),
      m_break_id(LLDB_INVALID_BREAK_ID), m_mutex(),
      m_process_image_addr_is_all_images_infos(false) {}

// Destructor
DynamicLoaderMacOSXDYLD::~DynamicLoaderMacOSXDYLD() {
  if (LLDB_BREAK_ID_IS_VALID(m_break_id))
    m_process->GetTarget().RemoveBreakpointByID(m_break_id);
}

bool DynamicLoaderMacOSXDYLD::ProcessDidExec() {
  std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());
  bool did_exec = false;
  if (m_process) {
    // If we are stopped after an exec, we will have only one thread...
    if (m_process->GetThreadList().GetSize() == 1) {
      // We know if a process has exec'ed if our "m_dyld_all_image_infos_addr"
      // value differs from the Process' image info address. When a process
      // execs itself it might cause a change if ASLR is enabled.
      const addr_t shlib_addr = m_process->GetImageInfoAddress();
      if (m_process_image_addr_is_all_images_infos &&
          shlib_addr != m_dyld_all_image_infos_addr) {
        // The image info address from the process is the
        // 'dyld_all_image_infos' address and it has changed.
        did_exec = true;
      } else if (!m_process_image_addr_is_all_images_infos &&
                 shlib_addr == m_dyld.address) {
        // The image info address from the process is the mach_header address
        // for dyld and it has changed.
        did_exec = true;
      } else {
        // ASLR might be disabled and dyld could have ended up in the same
        // location. We should try and detect if we are stopped at
        // '_dyld_start'
        ThreadSP thread_sp(m_process->GetThreadList().GetThreadAtIndex(0));
        if (thread_sp) {
          lldb::StackFrameSP frame_sp(thread_sp->GetStackFrameAtIndex(0));
          if (frame_sp) {
            const Symbol *symbol =
                frame_sp->GetSymbolContext(eSymbolContextSymbol).symbol;
            if (symbol) {
              if (symbol->GetName() == "_dyld_start")
                did_exec = true;
            }
          }
        }
      }

      if (did_exec) {
        m_libpthread_module_wp.reset();
        m_pthread_getspecific_addr.Clear();
      }
    }
  }
  return did_exec;
}

// Clear out the state of this class.
void DynamicLoaderMacOSXDYLD::DoClear() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (LLDB_BREAK_ID_IS_VALID(m_break_id))
    m_process->GetTarget().RemoveBreakpointByID(m_break_id);

  m_dyld_all_image_infos_addr = LLDB_INVALID_ADDRESS;
  m_dyld_all_image_infos.Clear();
  m_break_id = LLDB_INVALID_BREAK_ID;
}

// Check if we have found DYLD yet
bool DynamicLoaderMacOSXDYLD::DidSetNotificationBreakpoint() {
  return LLDB_BREAK_ID_IS_VALID(m_break_id);
}

void DynamicLoaderMacOSXDYLD::ClearNotificationBreakpoint() {
  if (LLDB_BREAK_ID_IS_VALID(m_break_id)) {
    m_process->GetTarget().RemoveBreakpointByID(m_break_id);
  }
}

// Try and figure out where dyld is by first asking the Process if it knows
// (which currently calls down in the lldb::Process to get the DYLD info
// (available on SnowLeopard only). If that fails, then check in the default
// addresses.
void DynamicLoaderMacOSXDYLD::DoInitialImageFetch() {
  if (m_dyld_all_image_infos_addr == LLDB_INVALID_ADDRESS) {
    // Check the image info addr as it might point to the mach header for dyld,
    // or it might point to the dyld_all_image_infos struct
    const addr_t shlib_addr = m_process->GetImageInfoAddress();
    if (shlib_addr != LLDB_INVALID_ADDRESS) {
      ByteOrder byte_order =
          m_process->GetTarget().GetArchitecture().GetByteOrder();
      uint8_t buf[4];
      DataExtractor data(buf, sizeof(buf), byte_order, 4);
      Status error;
      if (m_process->ReadMemory(shlib_addr, buf, 4, error) == 4) {
        lldb::offset_t offset = 0;
        uint32_t magic = data.GetU32(&offset);
        switch (magic) {
        case llvm::MachO::MH_MAGIC:
        case llvm::MachO::MH_MAGIC_64:
        case llvm::MachO::MH_CIGAM:
        case llvm::MachO::MH_CIGAM_64:
          m_process_image_addr_is_all_images_infos = false;
          ReadDYLDInfoFromMemoryAndSetNotificationCallback(shlib_addr);
          return;

        default:
          break;
        }
      }
      // Maybe it points to the all image infos?
      m_dyld_all_image_infos_addr = shlib_addr;
      m_process_image_addr_is_all_images_infos = true;
    }
  }

  if (m_dyld_all_image_infos_addr != LLDB_INVALID_ADDRESS) {
    if (ReadAllImageInfosStructure()) {
      if (m_dyld_all_image_infos.dyldImageLoadAddress != LLDB_INVALID_ADDRESS)
        ReadDYLDInfoFromMemoryAndSetNotificationCallback(
            m_dyld_all_image_infos.dyldImageLoadAddress);
      else
        ReadDYLDInfoFromMemoryAndSetNotificationCallback(
            m_dyld_all_image_infos_addr & 0xfffffffffff00000ull);
      return;
    }
  }

  // Check some default values
  Module *executable = m_process->GetTarget().GetExecutableModulePointer();

  if (executable) {
    const ArchSpec &exe_arch = executable->GetArchitecture();
    if (exe_arch.GetAddressByteSize() == 8) {
      ReadDYLDInfoFromMemoryAndSetNotificationCallback(0x7fff5fc00000ull);
    } else if (exe_arch.GetMachine() == llvm::Triple::arm ||
               exe_arch.GetMachine() == llvm::Triple::thumb ||
               exe_arch.GetMachine() == llvm::Triple::aarch64 ||
               exe_arch.GetMachine() == llvm::Triple::aarch64_32) {
      ReadDYLDInfoFromMemoryAndSetNotificationCallback(0x2fe00000);
    } else {
      ReadDYLDInfoFromMemoryAndSetNotificationCallback(0x8fe00000);
    }
  }
}

// Assume that dyld is in memory at ADDR and try to parse it's load commands
bool DynamicLoaderMacOSXDYLD::ReadDYLDInfoFromMemoryAndSetNotificationCallback(
    lldb::addr_t addr) {
  std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());
  DataExtractor data; // Load command data
  static ConstString g_dyld_all_image_infos("dyld_all_image_infos");
  static ConstString g_new_dyld_all_image_infos("dyld4::dyld_all_image_infos");
  if (ReadMachHeader(addr, &m_dyld.header, &data)) {
    if (m_dyld.header.filetype == llvm::MachO::MH_DYLINKER) {
      m_dyld.address = addr;
      ModuleSP dyld_module_sp;
      if (ParseLoadCommands(data, m_dyld, &m_dyld.file_spec)) {
        if (m_dyld.file_spec) {
          UpdateDYLDImageInfoFromNewImageInfo(m_dyld);
        }
      }
      dyld_module_sp = GetDYLDModule();

      Target &target = m_process->GetTarget();

      if (m_dyld_all_image_infos_addr == LLDB_INVALID_ADDRESS &&
          dyld_module_sp.get()) {
        const Symbol *symbol = dyld_module_sp->FindFirstSymbolWithNameAndType(
            g_dyld_all_image_infos, eSymbolTypeData);
        if (!symbol) {
          symbol = dyld_module_sp->FindFirstSymbolWithNameAndType(
              g_new_dyld_all_image_infos, eSymbolTypeData);
        }
        if (symbol)
          m_dyld_all_image_infos_addr = symbol->GetLoadAddress(&target);
      }

      if (m_dyld_all_image_infos_addr == LLDB_INVALID_ADDRESS) {
        ConstString g_sect_name("__all_image_info");
        SectionSP dyld_aii_section_sp =
            dyld_module_sp->GetSectionList()->FindSectionByName(g_sect_name);
        if (dyld_aii_section_sp) {
          Address dyld_aii_addr(dyld_aii_section_sp, 0);
          m_dyld_all_image_infos_addr = dyld_aii_addr.GetLoadAddress(&target);
        }
      }

      // Update all image infos
      InitializeFromAllImageInfos();

      // If we didn't have an executable before, but now we do, then the dyld
      // module shared pointer might be unique and we may need to add it again
      // (since Target::SetExecutableModule() will clear the images). So append
      // the dyld module back to the list if it is
      /// unique!
      if (dyld_module_sp) {
        target.GetImages().AppendIfNeeded(dyld_module_sp);

        // At this point we should have read in dyld's module, and so we should
        // set breakpoints in it:
        ModuleList modules;
        modules.Append(dyld_module_sp);
        target.ModulesDidLoad(modules);
        SetDYLDModule(dyld_module_sp);
      }

      return true;
    }
  }
  return false;
}

bool DynamicLoaderMacOSXDYLD::NeedToDoInitialImageFetch() {
  return m_dyld_all_image_infos_addr == LLDB_INVALID_ADDRESS;
}

// Static callback function that gets called when our DYLD notification
// breakpoint gets hit. We update all of our image infos and then let our super
// class DynamicLoader class decide if we should stop or not (based on global
// preference).
bool DynamicLoaderMacOSXDYLD::NotifyBreakpointHit(
    void *baton, StoppointCallbackContext *context, lldb::user_id_t break_id,
    lldb::user_id_t break_loc_id) {
  // Let the event know that the images have changed
  // DYLD passes three arguments to the notification breakpoint.
  // Arg1: enum dyld_image_mode mode - 0 = adding, 1 = removing Arg2: uint32_t
  // infoCount        - Number of shared libraries added Arg3: dyld_image_info
  // info[]    - Array of structs of the form:
  //                                     const struct mach_header
  //                                     *imageLoadAddress
  //                                     const char               *imageFilePath
  //                                     uintptr_t imageFileModDate (a time_t)

  DynamicLoaderMacOSXDYLD *dyld_instance = (DynamicLoaderMacOSXDYLD *)baton;

  // First step is to see if we've already initialized the all image infos.  If
  // we haven't then this function will do so and return true.  In the course
  // of initializing the all_image_infos it will read the complete current
  // state, so we don't need to figure out what has changed from the data
  // passed in to us.

  ExecutionContext exe_ctx(context->exe_ctx_ref);
  Process *process = exe_ctx.GetProcessPtr();

  // This is a sanity check just in case this dyld_instance is an old dyld
  // plugin's breakpoint still lying around.
  if (process != dyld_instance->m_process)
    return false;

  if (dyld_instance->InitializeFromAllImageInfos())
    return dyld_instance->GetStopWhenImagesChange();

  const lldb::ABISP &abi = process->GetABI();
  if (abi) {
    // Build up the value array to store the three arguments given above, then
    // get the values from the ABI:

    TypeSystemClangSP scratch_ts_sp =
        ScratchTypeSystemClang::GetForTarget(process->GetTarget());
    if (!scratch_ts_sp)
      return false;

    ValueList argument_values;
    Value input_value;

    CompilerType clang_void_ptr_type =
        scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();
    CompilerType clang_uint32_type =
        scratch_ts_sp->GetBuiltinTypeForEncodingAndBitSize(lldb::eEncodingUint,
                                                           32);
    input_value.SetValueType(Value::ValueType::Scalar);
    input_value.SetCompilerType(clang_uint32_type);
    //        input_value.SetContext (Value::eContextTypeClangType,
    //        clang_uint32_type);
    argument_values.PushValue(input_value);
    argument_values.PushValue(input_value);
    input_value.SetCompilerType(clang_void_ptr_type);
    //        input_value.SetContext (Value::eContextTypeClangType,
    //        clang_void_ptr_type);
    argument_values.PushValue(input_value);

    if (abi->GetArgumentValues(exe_ctx.GetThreadRef(), argument_values)) {
      uint32_t dyld_mode =
          argument_values.GetValueAtIndex(0)->GetScalar().UInt(-1);
      if (dyld_mode != static_cast<uint32_t>(-1)) {
        // Okay the mode was right, now get the number of elements, and the
        // array of new elements...
        uint32_t image_infos_count =
            argument_values.GetValueAtIndex(1)->GetScalar().UInt(-1);
        if (image_infos_count != static_cast<uint32_t>(-1)) {
          // Got the number added, now go through the array of added elements,
          // putting out the mach header address, and adding the image. Note,
          // I'm not putting in logging here, since the AddModules &
          // RemoveModules functions do all the logging internally.

          lldb::addr_t image_infos_addr =
              argument_values.GetValueAtIndex(2)->GetScalar().ULongLong();
          if (dyld_mode == 0) {
            // This is add:
            dyld_instance->AddModulesUsingImageInfosAddress(image_infos_addr,
                                                            image_infos_count);
          } else {
            // This is remove:
            dyld_instance->RemoveModulesUsingImageInfosAddress(
                image_infos_addr, image_infos_count);
          }
        }
      }
    }
  } else {
    Target &target = process->GetTarget();
    Debugger::ReportWarning(
        "no ABI plugin located for triple " +
            target.GetArchitecture().GetTriple().getTriple() +
            ": shared libraries will not be registered",
        target.GetDebugger().GetID());
  }

  // Return true to stop the target, false to just let the target run
  return dyld_instance->GetStopWhenImagesChange();
}

bool DynamicLoaderMacOSXDYLD::ReadAllImageInfosStructure() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  // the all image infos is already valid for this process stop ID
  if (m_process->GetStopID() == m_dyld_all_image_infos_stop_id)
    return true;

  m_dyld_all_image_infos.Clear();
  if (m_dyld_all_image_infos_addr != LLDB_INVALID_ADDRESS) {
    ByteOrder byte_order =
        m_process->GetTarget().GetArchitecture().GetByteOrder();
    uint32_t addr_size =
        m_process->GetTarget().GetArchitecture().GetAddressByteSize();

    uint8_t buf[256];
    DataExtractor data(buf, sizeof(buf), byte_order, addr_size);
    lldb::offset_t offset = 0;

    const size_t count_v2 = sizeof(uint32_t) + // version
                            sizeof(uint32_t) + // infoArrayCount
                            addr_size +        // infoArray
                            addr_size +        // notification
                            addr_size + // processDetachedFromSharedRegion +
                                        // libSystemInitialized + pad
                            addr_size;  // dyldImageLoadAddress
    const size_t count_v11 = count_v2 + addr_size +  // jitInfo
                             addr_size +             // dyldVersion
                             addr_size +             // errorMessage
                             addr_size +             // terminationFlags
                             addr_size +             // coreSymbolicationShmPage
                             addr_size +             // systemOrderFlag
                             addr_size +             // uuidArrayCount
                             addr_size +             // uuidArray
                             addr_size +             // dyldAllImageInfosAddress
                             addr_size +             // initialImageCount
                             addr_size +             // errorKind
                             addr_size +             // errorClientOfDylibPath
                             addr_size +             // errorTargetDylibPath
                             addr_size;              // errorSymbol
    const size_t count_v13 = count_v11 + addr_size + // sharedCacheSlide
                             sizeof(uuid_t);         // sharedCacheUUID
    UNUSED_IF_ASSERT_DISABLED(count_v13);
    assert(sizeof(buf) >= count_v13);

    Status error;
    if (m_process->ReadMemory(m_dyld_all_image_infos_addr, buf, 4, error) ==
        4) {
      m_dyld_all_image_infos.version = data.GetU32(&offset);
      // If anything in the high byte is set, we probably got the byte order
      // incorrect (the process might not have it set correctly yet due to
      // attaching to a program without a specified file).
      if (m_dyld_all_image_infos.version & 0xff000000) {
        // We have guessed the wrong byte order. Swap it and try reading the
        // version again.
        if (byte_order == eByteOrderLittle)
          byte_order = eByteOrderBig;
        else
          byte_order = eByteOrderLittle;

        data.SetByteOrder(byte_order);
        offset = 0;
        m_dyld_all_image_infos.version = data.GetU32(&offset);
      }
    } else {
      return false;
    }

    const size_t count =
        (m_dyld_all_image_infos.version >= 11) ? count_v11 : count_v2;

    const size_t bytes_read =
        m_process->ReadMemory(m_dyld_all_image_infos_addr, buf, count, error);
    if (bytes_read == count) {
      offset = 0;
      m_dyld_all_image_infos.version = data.GetU32(&offset);
      m_dyld_all_image_infos.dylib_info_count = data.GetU32(&offset);
      m_dyld_all_image_infos.dylib_info_addr = data.GetAddress(&offset);
      m_dyld_all_image_infos.notification = data.GetAddress(&offset);
      m_dyld_all_image_infos.processDetachedFromSharedRegion =
          data.GetU8(&offset);
      m_dyld_all_image_infos.libSystemInitialized = data.GetU8(&offset);
      // Adjust for padding.
      offset += addr_size - 2;
      m_dyld_all_image_infos.dyldImageLoadAddress = data.GetAddress(&offset);
      if (m_dyld_all_image_infos.version >= 11) {
        offset += addr_size * 8;
        uint64_t dyld_all_image_infos_addr = data.GetAddress(&offset);

        // When we started, we were given the actual address of the
        // all_image_infos struct (probably via TASK_DYLD_INFO) in memory -
        // this address is stored in m_dyld_all_image_infos_addr and is the
        // most accurate address we have.

        // We read the dyld_all_image_infos struct from memory; it contains its
        // own address. If the address in the struct does not match the actual
        // address, the dyld we're looking at has been loaded at a different
        // location (slid) from where it intended to load.  The addresses in
        // the dyld_all_image_infos struct are the original, non-slid
        // addresses, and need to be adjusted.  Most importantly the address of
        // dyld and the notification address need to be adjusted.

        if (dyld_all_image_infos_addr != m_dyld_all_image_infos_addr) {
          uint64_t image_infos_offset =
              dyld_all_image_infos_addr -
              m_dyld_all_image_infos.dyldImageLoadAddress;
          uint64_t notification_offset =
              m_dyld_all_image_infos.notification -
              m_dyld_all_image_infos.dyldImageLoadAddress;
          m_dyld_all_image_infos.dyldImageLoadAddress =
              m_dyld_all_image_infos_addr - image_infos_offset;
          m_dyld_all_image_infos.notification =
              m_dyld_all_image_infos.dyldImageLoadAddress + notification_offset;
        }
      }
      m_dyld_all_image_infos_stop_id = m_process->GetStopID();
      return true;
    }
  }
  return false;
}

bool DynamicLoaderMacOSXDYLD::AddModulesUsingImageInfosAddress(
    lldb::addr_t image_infos_addr, uint32_t image_infos_count) {
  ImageInfo::collection image_infos;
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "Adding %d modules.\n", image_infos_count);

  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());
  if (m_process->GetStopID() == m_dyld_image_infos_stop_id)
    return true;

  StructuredData::ObjectSP image_infos_json_sp =
      m_process->GetLoadedDynamicLibrariesInfos(image_infos_addr,
                                                image_infos_count);
  if (image_infos_json_sp.get() && image_infos_json_sp->GetAsDictionary() &&
      image_infos_json_sp->GetAsDictionary()->HasKey("images") &&
      image_infos_json_sp->GetAsDictionary()
          ->GetValueForKey("images")
          ->GetAsArray() &&
      image_infos_json_sp->GetAsDictionary()
              ->GetValueForKey("images")
              ->GetAsArray()
              ->GetSize() == image_infos_count) {
    bool return_value = false;
    if (JSONImageInformationIntoImageInfo(image_infos_json_sp, image_infos)) {
      UpdateSpecialBinariesFromNewImageInfos(image_infos);
      return_value = AddModulesUsingImageInfos(image_infos);
    }
    m_dyld_image_infos_stop_id = m_process->GetStopID();
    return return_value;
  }

  if (!ReadImageInfos(image_infos_addr, image_infos_count, image_infos))
    return false;

  UpdateImageInfosHeaderAndLoadCommands(image_infos, image_infos_count, false);
  bool return_value = AddModulesUsingImageInfos(image_infos);
  m_dyld_image_infos_stop_id = m_process->GetStopID();
  return return_value;
}

bool DynamicLoaderMacOSXDYLD::RemoveModulesUsingImageInfosAddress(
    lldb::addr_t image_infos_addr, uint32_t image_infos_count) {
  ImageInfo::collection image_infos;
  Log *log = GetLog(LLDBLog::DynamicLoader);

  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());
  if (m_process->GetStopID() == m_dyld_image_infos_stop_id)
    return true;

  // First read in the image_infos for the removed modules, and their headers &
  // load commands.
  if (!ReadImageInfos(image_infos_addr, image_infos_count, image_infos)) {
    if (log)
      log->PutCString("Failed reading image infos array.");
    return false;
  }

  LLDB_LOGF(log, "Removing %d modules.", image_infos_count);

  ModuleList unloaded_module_list;
  for (uint32_t idx = 0; idx < image_infos.size(); ++idx) {
    if (log) {
      LLDB_LOGF(log, "Removing module at address=0x%16.16" PRIx64 ".",
                image_infos[idx].address);
      image_infos[idx].PutToLog(log);
    }

    // Remove this image_infos from the m_all_image_infos.  We do the
    // comparison by address rather than by file spec because we can have many
    // modules with the same "file spec" in the case that they are modules
    // loaded from memory.
    //
    // Also copy over the uuid from the old entry to the removed entry so we
    // can use it to lookup the module in the module list.

    bool found = false;

    for (ImageInfo::collection::iterator pos = m_dyld_image_infos.begin();
         pos != m_dyld_image_infos.end(); pos++) {
      if (image_infos[idx].address == (*pos).address) {
        image_infos[idx].uuid = (*pos).uuid;

        // Add the module from this image_info to the "unloaded_module_list".
        // We'll remove them all at one go later on.

        ModuleSP unload_image_module_sp(
            FindTargetModuleForImageInfo(image_infos[idx], false, nullptr));
        if (unload_image_module_sp.get()) {
          // When we unload, be sure to use the image info from the old list,
          // since that has sections correctly filled in.
          UnloadModuleSections(unload_image_module_sp.get(), *pos);
          unloaded_module_list.AppendIfNeeded(unload_image_module_sp);
        } else {
          if (log) {
            LLDB_LOGF(log, "Could not find module for unloading info entry:");
            image_infos[idx].PutToLog(log);
          }
        }

        // Then remove it from the m_dyld_image_infos:

        m_dyld_image_infos.erase(pos);
        found = true;
        break;
      }
    }

    if (!found) {
      if (log) {
        LLDB_LOGF(log, "Could not find image_info entry for unloading image:");
        image_infos[idx].PutToLog(log);
      }
    }
  }
  if (unloaded_module_list.GetSize() > 0) {
    if (log) {
      log->PutCString("Unloaded:");
      unloaded_module_list.LogUUIDAndPaths(
          log, "DynamicLoaderMacOSXDYLD::ModulesDidUnload");
    }
    m_process->GetTarget().GetImages().Remove(unloaded_module_list);
  }
  m_dyld_image_infos_stop_id = m_process->GetStopID();
  return true;
}

bool DynamicLoaderMacOSXDYLD::ReadImageInfos(
    lldb::addr_t image_infos_addr, uint32_t image_infos_count,
    ImageInfo::collection &image_infos) {
  std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());
  const ByteOrder endian = GetByteOrderFromMagic(m_dyld.header.magic);
  const uint32_t addr_size = m_dyld.GetAddressByteSize();

  image_infos.resize(image_infos_count);
  const size_t count = image_infos.size() * 3 * addr_size;
  DataBufferHeap info_data(count, 0);
  Status error;
  const size_t bytes_read = m_process->ReadMemory(
      image_infos_addr, info_data.GetBytes(), info_data.GetByteSize(), error);
  if (bytes_read == count) {
    lldb::offset_t info_data_offset = 0;
    DataExtractor info_data_ref(info_data.GetBytes(), info_data.GetByteSize(),
                                endian, addr_size);
    for (size_t i = 0;
         i < image_infos.size() && info_data_ref.ValidOffset(info_data_offset);
         i++) {
      image_infos[i].address = info_data_ref.GetAddress(&info_data_offset);
      lldb::addr_t path_addr = info_data_ref.GetAddress(&info_data_offset);
      info_data_ref.GetAddress(&info_data_offset); // mod_date, unused */

      char raw_path[PATH_MAX];
      m_process->ReadCStringFromMemory(path_addr, raw_path, sizeof(raw_path),
                                       error);
      // don't resolve the path
      if (error.Success()) {
        image_infos[i].file_spec.SetFile(raw_path, FileSpec::Style::native);
      }
    }
    return true;
  } else {
    return false;
  }
}

// If we have found where the "_dyld_all_image_infos" lives in memory, read the
// current info from it, and then update all image load addresses (or lack
// thereof).  Only do this if this is the first time we're reading the dyld
// infos.  Return true if we actually read anything, and false otherwise.
bool DynamicLoaderMacOSXDYLD::InitializeFromAllImageInfos() {
  Log *log = GetLog(LLDBLog::DynamicLoader);

  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());
  if (m_process->GetStopID() == m_dyld_image_infos_stop_id ||
      m_dyld_image_infos.size() != 0)
    return false;

  if (ReadAllImageInfosStructure()) {
    // Nothing to load or unload?
    if (m_dyld_all_image_infos.dylib_info_count == 0)
      return true;

    if (m_dyld_all_image_infos.dylib_info_addr == 0) {
      // DYLD is updating the images now.  So we should say we have no images,
      // and then we'll
      // figure it out when we hit the added breakpoint.
      return false;
    } else {
      if (!AddModulesUsingImageInfosAddress(
              m_dyld_all_image_infos.dylib_info_addr,
              m_dyld_all_image_infos.dylib_info_count)) {
        DEBUG_PRINTF("%s", "unable to read all data for all_dylib_infos.");
        m_dyld_image_infos.clear();
      }
    }

    // Now we have one more bit of business.  If there is a library left in the
    // images for our target that doesn't have a load address, then it must be
    // something that we were expecting to load (for instance we read a load
    // command for it) but it didn't in fact load - probably because
    // DYLD_*_PATH pointed to an equivalent version.  We don't want it to stay
    // in the target's module list or it will confuse us, so unload it here.
    Target &target = m_process->GetTarget();
    ModuleList not_loaded_modules;
    for (ModuleSP module_sp : target.GetImages().Modules()) {
      if (!module_sp->IsLoadedInTarget(&target)) {
        if (log) {
          StreamString s;
          module_sp->GetDescription(s.AsRawOstream());
          LLDB_LOGF(log, "Unloading pre-run module: %s.", s.GetData());
        }
        not_loaded_modules.Append(module_sp);
      }
    }

    if (not_loaded_modules.GetSize() != 0) {
      target.GetImages().Remove(not_loaded_modules);
    }

    return true;
  } else
    return false;
}

// Read a mach_header at ADDR into HEADER, and also fill in the load command
// data into LOAD_COMMAND_DATA if it is non-NULL.
//
// Returns true if we succeed, false if we fail for any reason.
bool DynamicLoaderMacOSXDYLD::ReadMachHeader(lldb::addr_t addr,
                                             llvm::MachO::mach_header *header,
                                             DataExtractor *load_command_data) {
  DataBufferHeap header_bytes(sizeof(llvm::MachO::mach_header), 0);
  Status error;
  size_t bytes_read = m_process->ReadMemory(addr, header_bytes.GetBytes(),
                                            header_bytes.GetByteSize(), error);
  if (bytes_read == sizeof(llvm::MachO::mach_header)) {
    lldb::offset_t offset = 0;
    ::memset(header, 0, sizeof(llvm::MachO::mach_header));

    // Get the magic byte unswapped so we can figure out what we are dealing
    // with
    DataExtractor data(header_bytes.GetBytes(), header_bytes.GetByteSize(),
                       endian::InlHostByteOrder(), 4);
    header->magic = data.GetU32(&offset);
    lldb::addr_t load_cmd_addr = addr;
    data.SetByteOrder(
        DynamicLoaderMacOSXDYLD::GetByteOrderFromMagic(header->magic));
    switch (header->magic) {
    case llvm::MachO::MH_MAGIC:
    case llvm::MachO::MH_CIGAM:
      data.SetAddressByteSize(4);
      load_cmd_addr += sizeof(llvm::MachO::mach_header);
      break;

    case llvm::MachO::MH_MAGIC_64:
    case llvm::MachO::MH_CIGAM_64:
      data.SetAddressByteSize(8);
      load_cmd_addr += sizeof(llvm::MachO::mach_header_64);
      break;

    default:
      return false;
    }

    // Read the rest of dyld's mach header
    if (data.GetU32(&offset, &header->cputype,
                    (sizeof(llvm::MachO::mach_header) / sizeof(uint32_t)) -
                        1)) {
      if (load_command_data == nullptr)
        return true; // We were able to read the mach_header and weren't asked
                     // to read the load command bytes

      WritableDataBufferSP load_cmd_data_sp(
          new DataBufferHeap(header->sizeofcmds, 0));

      size_t load_cmd_bytes_read =
          m_process->ReadMemory(load_cmd_addr, load_cmd_data_sp->GetBytes(),
                                load_cmd_data_sp->GetByteSize(), error);

      if (load_cmd_bytes_read == header->sizeofcmds) {
        // Set the load command data and also set the correct endian swap
        // settings and the correct address size
        load_command_data->SetData(load_cmd_data_sp, 0, header->sizeofcmds);
        load_command_data->SetByteOrder(data.GetByteOrder());
        load_command_data->SetAddressByteSize(data.GetAddressByteSize());
        return true; // We successfully read the mach_header and the load
                     // command data
      }

      return false; // We weren't able to read the load command data
    }
  }
  return false; // We failed the read the mach_header
}

// Parse the load commands for an image
uint32_t DynamicLoaderMacOSXDYLD::ParseLoadCommands(const DataExtractor &data,
                                                    ImageInfo &dylib_info,
                                                    FileSpec *lc_id_dylinker) {
  lldb::offset_t offset = 0;
  uint32_t cmd_idx;
  Segment segment;
  dylib_info.Clear(true);

  for (cmd_idx = 0; cmd_idx < dylib_info.header.ncmds; cmd_idx++) {
    // Clear out any load command specific data from DYLIB_INFO since we are
    // about to read it.

    if (data.ValidOffsetForDataOfSize(offset,
                                      sizeof(llvm::MachO::load_command))) {
      llvm::MachO::load_command load_cmd;
      lldb::offset_t load_cmd_offset = offset;
      load_cmd.cmd = data.GetU32(&offset);
      load_cmd.cmdsize = data.GetU32(&offset);
      switch (load_cmd.cmd) {
      case llvm::MachO::LC_SEGMENT: {
        segment.name.SetTrimmedCStringWithLength(
            (const char *)data.GetData(&offset, 16), 16);
        // We are putting 4 uint32_t values 4 uint64_t values so we have to use
        // multiple 32 bit gets below.
        segment.vmaddr = data.GetU32(&offset);
        segment.vmsize = data.GetU32(&offset);
        segment.fileoff = data.GetU32(&offset);
        segment.filesize = data.GetU32(&offset);
        // Extract maxprot, initprot, nsects and flags all at once
        data.GetU32(&offset, &segment.maxprot, 4);
        dylib_info.segments.push_back(segment);
      } break;

      case llvm::MachO::LC_SEGMENT_64: {
        segment.name.SetTrimmedCStringWithLength(
            (const char *)data.GetData(&offset, 16), 16);
        // Extract vmaddr, vmsize, fileoff, and filesize all at once
        data.GetU64(&offset, &segment.vmaddr, 4);
        // Extract maxprot, initprot, nsects and flags all at once
        data.GetU32(&offset, &segment.maxprot, 4);
        dylib_info.segments.push_back(segment);
      } break;

      case llvm::MachO::LC_ID_DYLINKER:
        if (lc_id_dylinker) {
          const lldb::offset_t name_offset =
              load_cmd_offset + data.GetU32(&offset);
          const char *path = data.PeekCStr(name_offset);
          lc_id_dylinker->SetFile(path, FileSpec::Style::native);
          FileSystem::Instance().Resolve(*lc_id_dylinker);
        }
        break;

      case llvm::MachO::LC_UUID:
        dylib_info.uuid = UUID(data.GetData(&offset, 16), 16);
        break;

      default:
        break;
      }
      // Set offset to be the beginning of the next load command.
      offset = load_cmd_offset + load_cmd.cmdsize;
    }
  }

  // All sections listed in the dyld image info structure will all either be
  // fixed up already, or they will all be off by a single slide amount that is
  // determined by finding the first segment that is at file offset zero which
  // also has bytes (a file size that is greater than zero) in the object file.

  // Determine the slide amount (if any)
  const size_t num_sections = dylib_info.segments.size();
  for (size_t i = 0; i < num_sections; ++i) {
    // Iterate through the object file sections to find the first section that
    // starts of file offset zero and that has bytes in the file...
    if ((dylib_info.segments[i].fileoff == 0 &&
         dylib_info.segments[i].filesize > 0) ||
        (dylib_info.segments[i].name == "__TEXT")) {
      dylib_info.slide = dylib_info.address - dylib_info.segments[i].vmaddr;
      // We have found the slide amount, so we can exit this for loop.
      break;
    }
  }
  return cmd_idx;
}

// Read the mach_header and load commands for each image that the
// _dyld_all_image_infos structure points to and cache the results.

void DynamicLoaderMacOSXDYLD::UpdateImageInfosHeaderAndLoadCommands(
    ImageInfo::collection &image_infos, uint32_t infos_count,
    bool update_executable) {
  uint32_t exe_idx = UINT32_MAX;
  // Read any UUID values that we can get
  for (uint32_t i = 0; i < infos_count; i++) {
    if (!image_infos[i].UUIDValid()) {
      DataExtractor data; // Load command data
      if (!ReadMachHeader(image_infos[i].address, &image_infos[i].header,
                          &data))
        continue;

      ParseLoadCommands(data, image_infos[i], nullptr);

      if (image_infos[i].header.filetype == llvm::MachO::MH_EXECUTE)
        exe_idx = i;
    }
  }

  Target &target = m_process->GetTarget();

  if (exe_idx < image_infos.size()) {
    const bool can_create = true;
    ModuleSP exe_module_sp(FindTargetModuleForImageInfo(image_infos[exe_idx],
                                                        can_create, nullptr));

    if (exe_module_sp) {
      UpdateImageLoadAddress(exe_module_sp.get(), image_infos[exe_idx]);

      if (exe_module_sp.get() != target.GetExecutableModulePointer()) {
        // Don't load dependent images since we are in dyld where we will know
        // and find out about all images that are loaded. Also when setting the
        // executable module, it will clear the targets module list, and if we
        // have an in memory dyld module, it will get removed from the list so
        // we will need to add it back after setting the executable module, so
        // we first try and see if we already have a weak pointer to the dyld
        // module, make it into a shared pointer, then add the executable, then
        // re-add it back to make sure it is always in the list.
        ModuleSP dyld_module_sp(GetDYLDModule());

        m_process->GetTarget().SetExecutableModule(exe_module_sp,
                                                   eLoadDependentsNo);

        if (dyld_module_sp) {
          if (target.GetImages().AppendIfNeeded(dyld_module_sp)) {
            std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());

            // Also add it to the section list.
            UpdateImageLoadAddress(dyld_module_sp.get(), m_dyld);
          }
        }
      }
    }
  }
}

// Dump the _dyld_all_image_infos members and all current image infos that we
// have parsed to the file handle provided.
void DynamicLoaderMacOSXDYLD::PutToLog(Log *log) const {
  if (log == nullptr)
    return;

  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());
  LLDB_LOGF(log,
            "dyld_all_image_infos = { version=%d, count=%d, addr=0x%8.8" PRIx64
            ", notify=0x%8.8" PRIx64 " }",
            m_dyld_all_image_infos.version,
            m_dyld_all_image_infos.dylib_info_count,
            (uint64_t)m_dyld_all_image_infos.dylib_info_addr,
            (uint64_t)m_dyld_all_image_infos.notification);
  size_t i;
  const size_t count = m_dyld_image_infos.size();
  if (count > 0) {
    log->PutCString("Loaded:");
    for (i = 0; i < count; i++)
      m_dyld_image_infos[i].PutToLog(log);
  }
}

bool DynamicLoaderMacOSXDYLD::SetNotificationBreakpoint() {
  DEBUG_PRINTF("DynamicLoaderMacOSXDYLD::%s() process state = %s\n",
               __FUNCTION__, StateAsCString(m_process->GetState()));
  if (m_break_id == LLDB_INVALID_BREAK_ID) {
    if (m_dyld_all_image_infos.notification != LLDB_INVALID_ADDRESS) {
      Address so_addr;
      // Set the notification breakpoint and install a breakpoint callback
      // function that will get called each time the breakpoint gets hit. We
      // will use this to track when shared libraries get loaded/unloaded.
      bool resolved = m_process->GetTarget().ResolveLoadAddress(
          m_dyld_all_image_infos.notification, so_addr);
      if (!resolved) {
        ModuleSP dyld_module_sp = GetDYLDModule();
        if (dyld_module_sp) {
          std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());

          UpdateImageLoadAddress(dyld_module_sp.get(), m_dyld);
          resolved = m_process->GetTarget().ResolveLoadAddress(
              m_dyld_all_image_infos.notification, so_addr);
        }
      }

      if (resolved) {
        Breakpoint *dyld_break =
            m_process->GetTarget().CreateBreakpoint(so_addr, true, false).get();
        dyld_break->SetCallback(DynamicLoaderMacOSXDYLD::NotifyBreakpointHit,
                                this, true);
        dyld_break->SetBreakpointKind("shared-library-event");
        m_break_id = dyld_break->GetID();
      }
    }
  }
  return m_break_id != LLDB_INVALID_BREAK_ID;
}

Status DynamicLoaderMacOSXDYLD::CanLoadImage() {
  Status error;
  // In order for us to tell if we can load a shared library we verify that the
  // dylib_info_addr isn't zero (which means no shared libraries have been set
  // yet, or dyld is currently mucking with the shared library list).
  if (ReadAllImageInfosStructure()) {
    // TODO: also check the _dyld_global_lock_held variable in
    // libSystem.B.dylib?
    // TODO: check the malloc lock?
    // TODO: check the objective C lock?
    if (m_dyld_all_image_infos.dylib_info_addr != 0)
      return error; // Success
  }

  error.SetErrorString("unsafe to load or unload shared libraries");
  return error;
}

bool DynamicLoaderMacOSXDYLD::GetSharedCacheInformation(
    lldb::addr_t &base_address, UUID &uuid, LazyBool &using_shared_cache,
    LazyBool &private_shared_cache) {
  base_address = LLDB_INVALID_ADDRESS;
  uuid.Clear();
  using_shared_cache = eLazyBoolCalculate;
  private_shared_cache = eLazyBoolCalculate;

  if (m_process) {
    addr_t all_image_infos = m_process->GetImageInfoAddress();

    // The address returned by GetImageInfoAddress may be the address of dyld
    // (don't want) or it may be the address of the dyld_all_image_infos
    // structure (want). The first four bytes will be either the version field
    // (all_image_infos) or a Mach-O file magic constant. Version 13 and higher
    // of dyld_all_image_infos is required to get the sharedCacheUUID field.

    Status err;
    uint32_t version_or_magic =
        m_process->ReadUnsignedIntegerFromMemory(all_image_infos, 4, -1, err);
    if (version_or_magic != static_cast<uint32_t>(-1) &&
        version_or_magic != llvm::MachO::MH_MAGIC &&
        version_or_magic != llvm::MachO::MH_CIGAM &&
        version_or_magic != llvm::MachO::MH_MAGIC_64 &&
        version_or_magic != llvm::MachO::MH_CIGAM_64 &&
        version_or_magic >= 13) {
      addr_t sharedCacheUUID_address = LLDB_INVALID_ADDRESS;
      int wordsize = m_process->GetAddressByteSize();
      if (wordsize == 8) {
        sharedCacheUUID_address =
            all_image_infos + 160; // sharedCacheUUID <mach-o/dyld_images.h>
      }
      if (wordsize == 4) {
        sharedCacheUUID_address =
            all_image_infos + 84; // sharedCacheUUID <mach-o/dyld_images.h>
      }
      if (sharedCacheUUID_address != LLDB_INVALID_ADDRESS) {
        uuid_t shared_cache_uuid;
        if (m_process->ReadMemory(sharedCacheUUID_address, shared_cache_uuid,
                                  sizeof(uuid_t), err) == sizeof(uuid_t)) {
          uuid = UUID(shared_cache_uuid, 16);
          if (uuid.IsValid()) {
            using_shared_cache = eLazyBoolYes;
          }
        }

        if (version_or_magic >= 15) {
          // The sharedCacheBaseAddress field is the next one in the
          // dyld_all_image_infos struct.
          addr_t sharedCacheBaseAddr_address = sharedCacheUUID_address + 16;
          Status error;
          base_address = m_process->ReadUnsignedIntegerFromMemory(
              sharedCacheBaseAddr_address, wordsize, LLDB_INVALID_ADDRESS,
              error);
          if (error.Fail())
            base_address = LLDB_INVALID_ADDRESS;
        }

        return true;
      }

      //
      // add
      // NB: sharedCacheBaseAddress is the next field in dyld_all_image_infos
      // after
      // sharedCacheUUID -- that is, 16 bytes after it, if we wanted to fetch
      // it.
    }
  }
  return false;
}

bool DynamicLoaderMacOSXDYLD::IsFullyInitialized() {
  if (ReadAllImageInfosStructure())
    return m_dyld_all_image_infos.libSystemInitialized;
  return false;
}

void DynamicLoaderMacOSXDYLD::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
  DynamicLoaderMacOS::Initialize();
}

void DynamicLoaderMacOSXDYLD::Terminate() {
  DynamicLoaderMacOS::Terminate();
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef DynamicLoaderMacOSXDYLD::GetPluginDescriptionStatic() {
  return "Dynamic loader plug-in that watches for shared library loads/unloads "
         "in MacOSX user processes.";
}

uint32_t DynamicLoaderMacOSXDYLD::AddrByteSize() {
  std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());

  switch (m_dyld.header.magic) {
  case llvm::MachO::MH_MAGIC:
  case llvm::MachO::MH_CIGAM:
    return 4;

  case llvm::MachO::MH_MAGIC_64:
  case llvm::MachO::MH_CIGAM_64:
    return 8;

  default:
    break;
  }
  return 0;
}

lldb::ByteOrder DynamicLoaderMacOSXDYLD::GetByteOrderFromMagic(uint32_t magic) {
  switch (magic) {
  case llvm::MachO::MH_MAGIC:
  case llvm::MachO::MH_MAGIC_64:
    return endian::InlHostByteOrder();

  case llvm::MachO::MH_CIGAM:
  case llvm::MachO::MH_CIGAM_64:
    if (endian::InlHostByteOrder() == lldb::eByteOrderBig)
      return lldb::eByteOrderLittle;
    else
      return lldb::eByteOrderBig;

  default:
    break;
  }
  return lldb::eByteOrderInvalid;
}
