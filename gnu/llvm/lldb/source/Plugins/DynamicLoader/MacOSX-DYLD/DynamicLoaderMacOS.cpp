//===-- DynamicLoaderMacOS.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

#include "DynamicLoaderDarwin.h"
#include "DynamicLoaderMacOS.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

using namespace lldb;
using namespace lldb_private;

// Create an instance of this class. This function is filled into the plugin
// info class that gets handed out by the plugin factory and allows the lldb to
// instantiate an instance of this class.
DynamicLoader *DynamicLoaderMacOS::CreateInstance(Process *process,
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

  if (!UseDYLDSPI(process)) {
    create = false;
  }

  if (create)
    return new DynamicLoaderMacOS(process);
  return nullptr;
}

// Constructor
DynamicLoaderMacOS::DynamicLoaderMacOS(Process *process)
    : DynamicLoaderDarwin(process), m_image_infos_stop_id(UINT32_MAX),
      m_break_id(LLDB_INVALID_BREAK_ID),
      m_dyld_handover_break_id(LLDB_INVALID_BREAK_ID), m_mutex(),
      m_maybe_image_infos_address(LLDB_INVALID_ADDRESS),
      m_libsystem_fully_initalized(false) {}

// Destructor
DynamicLoaderMacOS::~DynamicLoaderMacOS() {
  if (LLDB_BREAK_ID_IS_VALID(m_break_id))
    m_process->GetTarget().RemoveBreakpointByID(m_break_id);
  if (LLDB_BREAK_ID_IS_VALID(m_dyld_handover_break_id))
    m_process->GetTarget().RemoveBreakpointByID(m_dyld_handover_break_id);
}

bool DynamicLoaderMacOS::ProcessDidExec() {
  std::lock_guard<std::recursive_mutex> baseclass_guard(GetMutex());
  bool did_exec = false;
  if (m_process) {
    // If we are stopped after an exec, we will have only one thread...
    if (m_process->GetThreadList().GetSize() == 1) {
      // Maybe we still have an image infos address around?  If so see
      // if that has changed, and if so we have exec'ed.
      if (m_maybe_image_infos_address != LLDB_INVALID_ADDRESS) {
        lldb::addr_t image_infos_address = m_process->GetImageInfoAddress();
        if (image_infos_address != m_maybe_image_infos_address) {
          // We don't really have to reset this here, since we are going to
          // call DoInitialImageFetch right away to handle the exec.  But in
          // case anybody looks at it in the meantime, it can't hurt.
          m_maybe_image_infos_address = image_infos_address;
          did_exec = true;
        }
      }

      if (!did_exec) {
        // See if we are stopped at '_dyld_start'
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
    }
  }

  if (did_exec) {
    m_libpthread_module_wp.reset();
    m_pthread_getspecific_addr.Clear();
    m_libsystem_fully_initalized = false;
  }
  return did_exec;
}

// Clear out the state of this class.
void DynamicLoaderMacOS::DoClear() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (LLDB_BREAK_ID_IS_VALID(m_break_id))
    m_process->GetTarget().RemoveBreakpointByID(m_break_id);
  if (LLDB_BREAK_ID_IS_VALID(m_dyld_handover_break_id))
    m_process->GetTarget().RemoveBreakpointByID(m_dyld_handover_break_id);

  m_break_id = LLDB_INVALID_BREAK_ID;
  m_dyld_handover_break_id = LLDB_INVALID_BREAK_ID;
  m_libsystem_fully_initalized = false;
}

bool DynamicLoaderMacOS::IsFullyInitialized() {
  if (m_libsystem_fully_initalized)
    return true;

  StructuredData::ObjectSP process_state_sp(
      m_process->GetDynamicLoaderProcessState());
  if (!process_state_sp)
    return true;
  if (process_state_sp->GetAsDictionary()->HasKey("error"))
    return true;
  if (!process_state_sp->GetAsDictionary()->HasKey("process_state string"))
    return true;
  std::string proc_state = process_state_sp->GetAsDictionary()
                               ->GetValueForKey("process_state string")
                               ->GetAsString()
                               ->GetValue()
                               .str();
  if (proc_state == "dyld_process_state_not_started" ||
      proc_state == "dyld_process_state_dyld_initialized" ||
      proc_state == "dyld_process_state_terminated_before_inits") {
    return false;
  }
  m_libsystem_fully_initalized = true;
  return true;
}

// Check if we have found DYLD yet
bool DynamicLoaderMacOS::DidSetNotificationBreakpoint() {
  return LLDB_BREAK_ID_IS_VALID(m_break_id);
}

void DynamicLoaderMacOS::ClearNotificationBreakpoint() {
  if (LLDB_BREAK_ID_IS_VALID(m_break_id)) {
    m_process->GetTarget().RemoveBreakpointByID(m_break_id);
    m_break_id = LLDB_INVALID_BREAK_ID;
  }
}

// Try and figure out where dyld is by first asking the Process if it knows
// (which currently calls down in the lldb::Process to get the DYLD info
// (available on SnowLeopard only). If that fails, then check in the default
// addresses.
void DynamicLoaderMacOS::DoInitialImageFetch() {
  Log *log = GetLog(LLDBLog::DynamicLoader);

  // Remove any binaries we pre-loaded in the Target before
  // launching/attaching. If the same binaries are present in the process,
  // we'll get them from the shared module cache, we won't need to re-load them
  // from disk.
  UnloadAllImages();

  StructuredData::ObjectSP all_image_info_json_sp(
      m_process->GetLoadedDynamicLibrariesInfos());
  ImageInfo::collection image_infos;
  if (all_image_info_json_sp.get() &&
      all_image_info_json_sp->GetAsDictionary() &&
      all_image_info_json_sp->GetAsDictionary()->HasKey("images") &&
      all_image_info_json_sp->GetAsDictionary()
          ->GetValueForKey("images")
          ->GetAsArray()) {
    if (JSONImageInformationIntoImageInfo(all_image_info_json_sp,
                                          image_infos)) {
      LLDB_LOGF(log, "Initial module fetch:  Adding %" PRId64 " modules.\n",
                (uint64_t)image_infos.size());

      UpdateSpecialBinariesFromNewImageInfos(image_infos);
      AddModulesUsingImageInfos(image_infos);
    }
  }

  m_dyld_image_infos_stop_id = m_process->GetStopID();
  m_maybe_image_infos_address = m_process->GetImageInfoAddress();
}

bool DynamicLoaderMacOS::NeedToDoInitialImageFetch() { return true; }

// Static callback function that gets called when our DYLD notification
// breakpoint gets hit. We update all of our image infos and then let our super
// class DynamicLoader class decide if we should stop or not (based on global
// preference).
bool DynamicLoaderMacOS::NotifyBreakpointHit(void *baton,
                                             StoppointCallbackContext *context,
                                             lldb::user_id_t break_id,
                                             lldb::user_id_t break_loc_id) {
  //
  // Our breakpoint on
  //
  // void lldb_image_notifier(enum dyld_image_mode mode, uint32_t infoCount,
  // const dyld_image_info info[])
  //
  // has been hit.  We need to read the arguments.

  DynamicLoaderMacOS *dyld_instance = (DynamicLoaderMacOS *)baton;

  ExecutionContext exe_ctx(context->exe_ctx_ref);
  Process *process = exe_ctx.GetProcessPtr();

  // This is a sanity check just in case this dyld_instance is an old dyld
  // plugin's breakpoint still lying around.
  if (process != dyld_instance->m_process)
    return false;

  if (dyld_instance->m_image_infos_stop_id != UINT32_MAX &&
      process->GetStopID() < dyld_instance->m_image_infos_stop_id) {
    return false;
  }

  const lldb::ABISP &abi = process->GetABI();
  if (abi) {
    // Build up the value array to store the three arguments given above, then
    // get the values from the ABI:

    TypeSystemClangSP scratch_ts_sp =
        ScratchTypeSystemClang::GetForTarget(process->GetTarget());
    if (!scratch_ts_sp)
      return false;

    ValueList argument_values;

    Value mode_value;    // enum dyld_notify_mode { dyld_notify_adding=0,
                         // dyld_notify_removing=1, dyld_notify_remove_all=2,
                         // dyld_notify_dyld_moved=3 };
    Value count_value;   // uint32_t
    Value headers_value; // struct dyld_image_info machHeaders[]

    CompilerType clang_void_ptr_type =
        scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();
    CompilerType clang_uint32_type =
        scratch_ts_sp->GetBuiltinTypeForEncodingAndBitSize(lldb::eEncodingUint,
                                                           32);
    CompilerType clang_uint64_type =
        scratch_ts_sp->GetBuiltinTypeForEncodingAndBitSize(lldb::eEncodingUint,
                                                           32);

    mode_value.SetValueType(Value::ValueType::Scalar);
    mode_value.SetCompilerType(clang_uint32_type);

    count_value.SetValueType(Value::ValueType::Scalar);
    count_value.SetCompilerType(clang_uint32_type);

    headers_value.SetValueType(Value::ValueType::Scalar);
    headers_value.SetCompilerType(clang_void_ptr_type);

    argument_values.PushValue(mode_value);
    argument_values.PushValue(count_value);
    argument_values.PushValue(headers_value);

    if (abi->GetArgumentValues(exe_ctx.GetThreadRef(), argument_values)) {
      uint32_t dyld_mode =
          argument_values.GetValueAtIndex(0)->GetScalar().UInt(-1);
      if (dyld_mode != static_cast<uint32_t>(-1)) {
        // Okay the mode was right, now get the number of elements, and the
        // array of new elements...
        uint32_t image_infos_count =
            argument_values.GetValueAtIndex(1)->GetScalar().UInt(-1);
        if (image_infos_count != static_cast<uint32_t>(-1)) {
          addr_t header_array =
              argument_values.GetValueAtIndex(2)->GetScalar().ULongLong(-1);
          if (header_array != static_cast<uint64_t>(-1)) {
            std::vector<addr_t> image_load_addresses;
            // header_array points to an array of image_infos_count elements,
            // each is
            // struct dyld_image_info {
            //   const struct mach_header* imageLoadAddress;
            //   const char*               imageFilePath;
            //   uintptr_t                 imageFileModDate;
            // };
            //
            // and we only need the imageLoadAddress fields.

            const int addrsize =
                process->GetTarget().GetArchitecture().GetAddressByteSize();
            for (uint64_t i = 0; i < image_infos_count; i++) {
              Status error;
              addr_t dyld_image_info = header_array + (addrsize * 3 * i);
              addr_t addr =
                  process->ReadPointerFromMemory(dyld_image_info, error);
              if (error.Success()) {
                image_load_addresses.push_back(addr);
              } else {
                Debugger::ReportWarning(
                    "DynamicLoaderMacOS::NotifyBreakpointHit unable "
                    "to read binary mach-o load address at 0x%" PRIx64,
                    addr);
              }
            }
            if (dyld_mode == 0) {
              // dyld_notify_adding
              if (process->GetTarget().GetImages().GetSize() == 0) {
                // When all images have been removed, we're doing the
                // dyld handover from a launch-dyld to a shared-cache-dyld,
                // and we've just hit our one-shot address breakpoint in
                // the sc-dyld.  Note that the image addresses passed to
                // this function are inferior sizeof(void*) not uint64_t's
                // like our normal notification, so don't even look at
                // image_load_addresses.

                dyld_instance->ClearDYLDHandoverBreakpoint();

                dyld_instance->DoInitialImageFetch();
                dyld_instance->SetNotificationBreakpoint();
              } else {
                dyld_instance->AddBinaries(image_load_addresses);
              }
            } else if (dyld_mode == 1) {
              // dyld_notify_removing
              dyld_instance->UnloadImages(image_load_addresses);
            } else if (dyld_mode == 2) {
              // dyld_notify_remove_all
              dyld_instance->UnloadAllImages();
            } else if (dyld_mode == 3 && image_infos_count == 1) {
              // dyld_image_dyld_moved

              dyld_instance->ClearNotificationBreakpoint();
              dyld_instance->UnloadAllImages();
              dyld_instance->ClearDYLDModule();
              process->GetTarget().GetImages().Clear();
              process->GetTarget().GetSectionLoadList().Clear();

              addr_t all_image_infos = process->GetImageInfoAddress();
              int addr_size =
                  process->GetTarget().GetArchitecture().GetAddressByteSize();
              addr_t notification_location = all_image_infos + 4 + // version
                                             4 +        // infoArrayCount
                                             addr_size; // infoArray
              Status error;
              addr_t notification_addr =
                  process->ReadPointerFromMemory(notification_location, error);
              if (!error.Success()) {
                Debugger::ReportWarning(
                    "DynamicLoaderMacOS::NotifyBreakpointHit unable "
                    "to read address of dyld-handover notification function at "
                    "0x%" PRIx64,
                    notification_location);
              } else {
                notification_addr = process->FixCodeAddress(notification_addr);
                dyld_instance->SetDYLDHandoverBreakpoint(notification_addr);
              }
            }
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

void DynamicLoaderMacOS::AddBinaries(
    const std::vector<lldb::addr_t> &load_addresses) {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  ImageInfo::collection image_infos;

  LLDB_LOGF(log, "Adding %" PRId64 " modules.",
            (uint64_t)load_addresses.size());
  StructuredData::ObjectSP binaries_info_sp =
      m_process->GetLoadedDynamicLibrariesInfos(load_addresses);
  if (binaries_info_sp.get() && binaries_info_sp->GetAsDictionary() &&
      binaries_info_sp->GetAsDictionary()->HasKey("images") &&
      binaries_info_sp->GetAsDictionary()
          ->GetValueForKey("images")
          ->GetAsArray() &&
      binaries_info_sp->GetAsDictionary()
              ->GetValueForKey("images")
              ->GetAsArray()
              ->GetSize() == load_addresses.size()) {
    if (JSONImageInformationIntoImageInfo(binaries_info_sp, image_infos)) {
      UpdateSpecialBinariesFromNewImageInfos(image_infos);
      AddModulesUsingImageInfos(image_infos);
    }
    m_dyld_image_infos_stop_id = m_process->GetStopID();
  }
}

// Dump the _dyld_all_image_infos members and all current image infos that we
// have parsed to the file handle provided.
void DynamicLoaderMacOS::PutToLog(Log *log) const {
  if (log == nullptr)
    return;
}

// Look in dyld's dyld_all_image_infos structure for the address
// of the notification function.
// We can find the address of dyld_all_image_infos by a system
// call, even if we don't have a dyld binary registered in lldb's
// image list.
// At process launch time - before dyld has executed any instructions -
// the address of the notification function is not a resolved vm address
// yet.  dyld_all_image_infos also has a field with its own address
// in it, and this will also be unresolved when we're at this state.
// So we can compare the address of the object with this field and if
// they differ, dyld hasn't started executing yet and we can't get the
// notification address this way.
addr_t DynamicLoaderMacOS::GetNotificationFuncAddrFromImageInfos() {
  addr_t notification_addr = LLDB_INVALID_ADDRESS;
  if (!m_process)
    return notification_addr;

  addr_t all_image_infos_addr = m_process->GetImageInfoAddress();
  if (all_image_infos_addr == LLDB_INVALID_ADDRESS)
    return notification_addr;

  const uint32_t addr_size =
      m_process->GetTarget().GetArchitecture().GetAddressByteSize();
  offset_t registered_infos_addr_offset =
      sizeof(uint32_t) + // version
      sizeof(uint32_t) + // infoArrayCount
      addr_size +        // infoArray
      addr_size +        // notification
      addr_size +        // processDetachedFromSharedRegion +
                         // libSystemInitialized + pad
      addr_size +        // dyldImageLoadAddress
      addr_size +        // jitInfo
      addr_size +        // dyldVersion
      addr_size +        // errorMessage
      addr_size +        // terminationFlags
      addr_size +        // coreSymbolicationShmPage
      addr_size +        // systemOrderFlag
      addr_size +        // uuidArrayCount
      addr_size;         // uuidArray
                         // dyldAllImageInfosAddress

  // If the dyldAllImageInfosAddress does not match
  // the actual address of this struct, dyld has not started
  // executing yet.  The 'notification' field can't be used by
  // lldb until it's resolved to an actual address.
  Status error;
  addr_t registered_infos_addr = m_process->ReadPointerFromMemory(
      all_image_infos_addr + registered_infos_addr_offset, error);
  if (!error.Success())
    return notification_addr;
  if (registered_infos_addr != all_image_infos_addr)
    return notification_addr;

  offset_t notification_fptr_offset = sizeof(uint32_t) + // version
                                      sizeof(uint32_t) + // infoArrayCount
                                      addr_size;         // infoArray

  addr_t notification_fptr = m_process->ReadPointerFromMemory(
      all_image_infos_addr + notification_fptr_offset, error);
  if (error.Success())
    notification_addr = m_process->FixCodeAddress(notification_fptr);
  return notification_addr;
}

// We want to put a breakpoint on dyld's lldb_image_notifier()
// but we may have attached to the process during the
// transition from on-disk-dyld to shared-cache-dyld, so there's
// officially no dyld binary loaded in the process (libdyld will
// report none when asked), but the kernel can find the dyld_all_image_infos
// struct and the function pointer for lldb_image_notifier is in
// that struct.
bool DynamicLoaderMacOS::SetNotificationBreakpoint() {

  // First try to find the notification breakpoint function by name
  if (m_break_id == LLDB_INVALID_BREAK_ID) {
    ModuleSP dyld_sp(GetDYLDModule());
    if (dyld_sp) {
      bool internal = true;
      bool hardware = false;
      LazyBool skip_prologue = eLazyBoolNo;
      FileSpecList *source_files = nullptr;
      FileSpecList dyld_filelist;
      dyld_filelist.Append(dyld_sp->GetFileSpec());

      Breakpoint *breakpoint =
          m_process->GetTarget()
              .CreateBreakpoint(&dyld_filelist, source_files,
                                "lldb_image_notifier", eFunctionNameTypeFull,
                                eLanguageTypeUnknown, 0, skip_prologue,
                                internal, hardware)
              .get();
      breakpoint->SetCallback(DynamicLoaderMacOS::NotifyBreakpointHit, this,
                              true);
      breakpoint->SetBreakpointKind("shared-library-event");
      if (breakpoint->HasResolvedLocations())
        m_break_id = breakpoint->GetID();
      else
        m_process->GetTarget().RemoveBreakpointByID(breakpoint->GetID());

      if (m_break_id == LLDB_INVALID_BREAK_ID) {
        Breakpoint *breakpoint =
            m_process->GetTarget()
                .CreateBreakpoint(&dyld_filelist, source_files,
                                  "gdb_image_notifier", eFunctionNameTypeFull,
                                  eLanguageTypeUnknown, 0, skip_prologue,
                                  internal, hardware)
                .get();
        breakpoint->SetCallback(DynamicLoaderMacOS::NotifyBreakpointHit, this,
                                true);
        breakpoint->SetBreakpointKind("shared-library-event");
        if (breakpoint->HasResolvedLocations())
          m_break_id = breakpoint->GetID();
        else
          m_process->GetTarget().RemoveBreakpointByID(breakpoint->GetID());
      }
    }
  }

  // Failing that, find dyld_all_image_infos struct in memory,
  // read the notification function pointer at the offset.
  if (m_break_id == LLDB_INVALID_BREAK_ID) {
    addr_t notification_addr = GetNotificationFuncAddrFromImageInfos();
    if (notification_addr != LLDB_INVALID_ADDRESS) {
      Address so_addr;
      // We may not have a dyld binary mapped to this address yet;
      // don't try to express the Address object as section+offset,
      // only as a raw load address.
      so_addr.SetRawAddress(notification_addr);
      Breakpoint *dyld_break =
          m_process->GetTarget().CreateBreakpoint(so_addr, true, false).get();
      dyld_break->SetCallback(DynamicLoaderMacOS::NotifyBreakpointHit, this,
                              true);
      dyld_break->SetBreakpointKind("shared-library-event");
      if (dyld_break->HasResolvedLocations())
        m_break_id = dyld_break->GetID();
      else
        m_process->GetTarget().RemoveBreakpointByID(dyld_break->GetID());
    }
  }
  return m_break_id != LLDB_INVALID_BREAK_ID;
}

bool DynamicLoaderMacOS::SetDYLDHandoverBreakpoint(
    addr_t notification_address) {
  if (m_dyld_handover_break_id == LLDB_INVALID_BREAK_ID) {
    BreakpointSP dyld_handover_bp = m_process->GetTarget().CreateBreakpoint(
        notification_address, true, false);
    dyld_handover_bp->SetCallback(DynamicLoaderMacOS::NotifyBreakpointHit, this,
                                  true);
    dyld_handover_bp->SetOneShot(true);
    m_dyld_handover_break_id = dyld_handover_bp->GetID();
    return true;
  }
  return false;
}

void DynamicLoaderMacOS::ClearDYLDHandoverBreakpoint() {
  if (LLDB_BREAK_ID_IS_VALID(m_dyld_handover_break_id))
    m_process->GetTarget().RemoveBreakpointByID(m_dyld_handover_break_id);
  m_dyld_handover_break_id = LLDB_INVALID_BREAK_ID;
}

addr_t
DynamicLoaderMacOS::GetDyldLockVariableAddressFromModule(Module *module) {
  SymbolContext sc;
  Target &target = m_process->GetTarget();
  if (Symtab *symtab = module->GetSymtab()) {
    std::vector<uint32_t> match_indexes;
    ConstString g_symbol_name("_dyld_global_lock_held");
    uint32_t num_matches = 0;
    num_matches =
        symtab->AppendSymbolIndexesWithName(g_symbol_name, match_indexes);
    if (num_matches == 1) {
      Symbol *symbol = symtab->SymbolAtIndex(match_indexes[0]);
      if (symbol &&
          (symbol->ValueIsAddress() || symbol->GetAddressRef().IsValid())) {
        return symbol->GetAddressRef().GetOpcodeLoadAddress(&target);
      }
    }
  }
  return LLDB_INVALID_ADDRESS;
}

//  Look for this symbol:
//
//  int __attribute__((visibility("hidden")))           _dyld_global_lock_held =
//  0;
//
//  in libdyld.dylib.
Status DynamicLoaderMacOS::CanLoadImage() {
  Status error;
  addr_t symbol_address = LLDB_INVALID_ADDRESS;
  ConstString g_libdyld_name("libdyld.dylib");
  Target &target = m_process->GetTarget();
  const ModuleList &target_modules = target.GetImages();
  std::lock_guard<std::recursive_mutex> guard(target_modules.GetMutex());

  // Find any modules named "libdyld.dylib" and look for the symbol there first
  for (ModuleSP module_sp : target.GetImages().ModulesNoLocking()) {
    if (module_sp) {
      if (module_sp->GetFileSpec().GetFilename() == g_libdyld_name) {
        symbol_address = GetDyldLockVariableAddressFromModule(module_sp.get());
        if (symbol_address != LLDB_INVALID_ADDRESS)
          break;
      }
    }
  }

  // Search through all modules looking for the symbol in them
  if (symbol_address == LLDB_INVALID_ADDRESS) {
    for (ModuleSP module_sp : target.GetImages().Modules()) {
      if (module_sp) {
        addr_t symbol_address =
            GetDyldLockVariableAddressFromModule(module_sp.get());
        if (symbol_address != LLDB_INVALID_ADDRESS)
          break;
      }
    }
  }

  // Default assumption is that it is OK to load images. Only say that we
  // cannot load images if we find the symbol in libdyld and it indicates that
  // we cannot.

  if (symbol_address != LLDB_INVALID_ADDRESS) {
    {
      int lock_held =
          m_process->ReadUnsignedIntegerFromMemory(symbol_address, 4, 0, error);
      if (lock_held != 0) {
        error.SetErrorString("dyld lock held - unsafe to load images.");
      }
    }
  } else {
    // If we were unable to find _dyld_global_lock_held in any modules, or it
    // is not loaded into memory yet, we may be at process startup (sitting  at
    // _dyld_start) - so we should not allow dlopen calls. But if we found more
    // than one module then we are clearly past _dyld_start so in that case
    // we'll default to "it's safe".
    if (target.GetImages().GetSize() <= 1)
      error.SetErrorString("could not find the dyld library or "
                           "the dyld lock symbol");
  }
  return error;
}

bool DynamicLoaderMacOS::GetSharedCacheInformation(
    lldb::addr_t &base_address, UUID &uuid, LazyBool &using_shared_cache,
    LazyBool &private_shared_cache) {
  base_address = LLDB_INVALID_ADDRESS;
  uuid.Clear();
  using_shared_cache = eLazyBoolCalculate;
  private_shared_cache = eLazyBoolCalculate;

  if (m_process) {
    StructuredData::ObjectSP info = m_process->GetSharedCacheInfo();
    StructuredData::Dictionary *info_dict = nullptr;
    if (info.get() && info->GetAsDictionary()) {
      info_dict = info->GetAsDictionary();
    }

    // {"shared_cache_base_address":140735683125248,"shared_cache_uuid
    // ":"DDB8D70C-
    // C9A2-3561-B2C8-BE48A4F33F96","no_shared_cache":false,"shared_cache_private_cache":false}

    if (info_dict && info_dict->HasKey("shared_cache_uuid") &&
        info_dict->HasKey("no_shared_cache") &&
        info_dict->HasKey("shared_cache_base_address")) {
      base_address = info_dict->GetValueForKey("shared_cache_base_address")
                         ->GetUnsignedIntegerValue(LLDB_INVALID_ADDRESS);
      std::string uuid_str = std::string(
          info_dict->GetValueForKey("shared_cache_uuid")->GetStringValue());
      if (!uuid_str.empty())
        uuid.SetFromStringRef(uuid_str);
      if (!info_dict->GetValueForKey("no_shared_cache")->GetBooleanValue())
        using_shared_cache = eLazyBoolYes;
      else
        using_shared_cache = eLazyBoolNo;
      if (info_dict->GetValueForKey("shared_cache_private_cache")
              ->GetBooleanValue())
        private_shared_cache = eLazyBoolYes;
      else
        private_shared_cache = eLazyBoolNo;

      return true;
    }
  }
  return false;
}

void DynamicLoaderMacOS::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void DynamicLoaderMacOS::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef DynamicLoaderMacOS::GetPluginDescriptionStatic() {
  return "Dynamic loader plug-in that watches for shared library loads/unloads "
         "in MacOSX user processes.";
}
