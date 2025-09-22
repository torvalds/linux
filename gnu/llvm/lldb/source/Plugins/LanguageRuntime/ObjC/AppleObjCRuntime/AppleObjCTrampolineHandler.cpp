//===-- AppleObjCTrampolineHandler.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AppleObjCTrampolineHandler.h"
#include "AppleThreadPlanStepThroughObjCTrampoline.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

const char *AppleObjCTrampolineHandler::g_lookup_implementation_function_name =
    "__lldb_objc_find_implementation_for_selector";
const char *AppleObjCTrampolineHandler::
    g_lookup_implementation_with_stret_function_code =
        R"(
  if (is_stret) {
    return_struct.impl_addr =
    class_getMethodImplementation_stret (return_struct.class_addr,
                                         return_struct.sel_addr);
  } else {
    return_struct.impl_addr =
        class_getMethodImplementation (return_struct.class_addr,
                                       return_struct.sel_addr);
  }
  if (debug)
    printf ("\n*** Returning implementation: %p.\n",
            return_struct.impl_addr);

  return return_struct.impl_addr;
}
)";
const char *
    AppleObjCTrampolineHandler::g_lookup_implementation_no_stret_function_code =
        R"(
  return_struct.impl_addr =
    class_getMethodImplementation (return_struct.class_addr,
                                   return_struct.sel_addr);
  if (debug)
    printf ("\n*** getMethodImpletation for addr: 0x%p sel: 0x%p result: 0x%p.\n",
            return_struct.class_addr, return_struct.sel_addr, return_struct.impl_addr);

  return return_struct.impl_addr;
}
)";

const char
    *AppleObjCTrampolineHandler::g_lookup_implementation_function_common_code =
        R"(
extern "C"
{
  extern void *class_getMethodImplementation(void *objc_class, void *sel);
  extern void *class_getMethodImplementation_stret(void *objc_class, void *sel);
  extern void * object_getClass (id object);
  extern void * sel_getUid(char *name);
  extern int printf(const char *format, ...);
}
extern "C" void * 
__lldb_objc_find_implementation_for_selector (void *object,
                                              void *sel,
                                              int is_str_ptr,
                                              int is_stret,
                                              int is_super,
                                              int is_super2,
                                              int is_fixup,
                                              int is_fixed,
                                              int debug)
{
  struct __lldb_imp_return_struct {
    void *class_addr;
    void *sel_addr;
    void *impl_addr;
  };

  struct __lldb_objc_class {
    void *isa;
    void *super_ptr;
  };
  struct __lldb_objc_super {
    void *receiver;
    struct __lldb_objc_class *class_ptr;
  };
  struct __lldb_msg_ref {
    void *dont_know;
    void *sel;
  };

  struct __lldb_imp_return_struct return_struct;
                                                                           
  if (debug)
    printf ("\n*** Called with obj: %p sel: %p is_str_ptr: %d "
            "is_stret: %d is_super: %d, "
            "is_super2: %d, is_fixup: %d, is_fixed: %d\n",
             object, sel, is_str_ptr, is_stret,
             is_super, is_super2, is_fixup, is_fixed);

  if (is_str_ptr) {
    if (debug)
      printf("*** Turning string: '%s'", sel);
    sel = sel_getUid((char *)sel);
    if (debug)
      printf("*** into sel to %p", sel);
  }
  if (is_super) {
    if (is_super2) {
      return_struct.class_addr 
          = ((__lldb_objc_super *) object)->class_ptr->super_ptr;
    } else {
      return_struct.class_addr = ((__lldb_objc_super *) object)->class_ptr;
    }
    if (debug)
      printf("*** Super, class addr: %p\n", return_struct.class_addr);
  } else {
    // This code seems a little funny, but has its reasons...
    // The call to [object class] is here because if this is a class, and has 
    // not been called into yet, we need to do something to force the class to 
    // initialize itself.
    // Then the call to object_getClass will actually return the correct class, 
    // either the class if object is a class instance, or the meta-class if it 
    // is a class pointer.
    void *class_ptr = (void *) [(id) object class];
    return_struct.class_addr = (id)  object_getClass((id) object);
    if (debug) {
      if (class_ptr == object) {
        printf ("Found a class object, need to return the meta class %p -> %p\n",
                class_ptr, return_struct.class_addr);
      } else {
         printf ("[object class] returned: %p object_getClass: %p.\n",
                 class_ptr, return_struct.class_addr);
      }
    }
  }

  if (is_fixup) {
    if (is_fixed) {
        return_struct.sel_addr = ((__lldb_msg_ref *) sel)->sel;
    } else {
      char *sel_name = (char *) ((__lldb_msg_ref *) sel)->sel;
      return_struct.sel_addr = sel_getUid (sel_name);
      if (debug)
        printf ("\n*** Got fixed up selector: %p for name %s.\n",
                return_struct.sel_addr, sel_name);
    }
  } else {
    return_struct.sel_addr = sel;
  }
)";

AppleObjCTrampolineHandler::AppleObjCVTables::VTableRegion::VTableRegion(
    AppleObjCVTables *owner, lldb::addr_t header_addr)
    : m_valid(true), m_owner(owner), m_header_addr(header_addr) {
  SetUpRegion();
}

AppleObjCTrampolineHandler::~AppleObjCTrampolineHandler() = default;

void AppleObjCTrampolineHandler::AppleObjCVTables::VTableRegion::SetUpRegion() {
  // The header looks like:
  //
  //   uint16_t headerSize
  //   uint16_t descSize
  //   uint32_t descCount
  //   void * next
  //
  // First read in the header:

  char memory_buffer[16];
  ProcessSP process_sp = m_owner->GetProcessSP();
  if (!process_sp)
    return;
  DataExtractor data(memory_buffer, sizeof(memory_buffer),
                     process_sp->GetByteOrder(),
                     process_sp->GetAddressByteSize());
  size_t actual_size = 8 + process_sp->GetAddressByteSize();
  Status error;
  size_t bytes_read =
      process_sp->ReadMemory(m_header_addr, memory_buffer, actual_size, error);
  if (bytes_read != actual_size) {
    m_valid = false;
    return;
  }

  lldb::offset_t offset = 0;
  const uint16_t header_size = data.GetU16(&offset);
  const uint16_t descriptor_size = data.GetU16(&offset);
  const size_t num_descriptors = data.GetU32(&offset);

  m_next_region = data.GetAddress(&offset);

  // If the header size is 0, that means we've come in too early before this
  // data is set up.
  // Set ourselves as not valid, and continue.
  if (header_size == 0 || num_descriptors == 0) {
    m_valid = false;
    return;
  }

  // Now read in all the descriptors:
  // The descriptor looks like:
  //
  // uint32_t offset
  // uint32_t flags
  //
  // Where offset is either 0 - in which case it is unused, or it is
  // the offset of the vtable code from the beginning of the
  // descriptor record.  Below, we'll convert that into an absolute
  // code address, since I don't want to have to compute it over and
  // over.

  // Ingest the whole descriptor array:
  const lldb::addr_t desc_ptr = m_header_addr + header_size;
  const size_t desc_array_size = num_descriptors * descriptor_size;
  WritableDataBufferSP data_sp(new DataBufferHeap(desc_array_size, '\0'));
  uint8_t *dst = (uint8_t *)data_sp->GetBytes();

  DataExtractor desc_extractor(dst, desc_array_size, process_sp->GetByteOrder(),
                               process_sp->GetAddressByteSize());
  bytes_read = process_sp->ReadMemory(desc_ptr, dst, desc_array_size, error);
  if (bytes_read != desc_array_size) {
    m_valid = false;
    return;
  }

  // The actual code for the vtables will be laid out consecutively, so I also
  // compute the start and end of the whole code block.

  offset = 0;
  m_code_start_addr = 0;
  m_code_end_addr = 0;

  for (size_t i = 0; i < num_descriptors; i++) {
    lldb::addr_t start_offset = offset;
    uint32_t voffset = desc_extractor.GetU32(&offset);
    uint32_t flags = desc_extractor.GetU32(&offset);
    lldb::addr_t code_addr = desc_ptr + start_offset + voffset;
    m_descriptors.push_back(VTableDescriptor(flags, code_addr));

    if (m_code_start_addr == 0 || code_addr < m_code_start_addr)
      m_code_start_addr = code_addr;
    if (code_addr > m_code_end_addr)
      m_code_end_addr = code_addr;

    offset = start_offset + descriptor_size;
  }
  // Finally, a little bird told me that all the vtable code blocks
  // are the same size.  Let's compute the blocks and if they are all
  // the same add the size to the code end address:
  lldb::addr_t code_size = 0;
  bool all_the_same = true;
  for (size_t i = 0; i < num_descriptors - 1; i++) {
    lldb::addr_t this_size =
        m_descriptors[i + 1].code_start - m_descriptors[i].code_start;
    if (code_size == 0)
      code_size = this_size;
    else {
      if (this_size != code_size)
        all_the_same = false;
      if (this_size > code_size)
        code_size = this_size;
    }
  }
  if (all_the_same)
    m_code_end_addr += code_size;
}

bool AppleObjCTrampolineHandler::AppleObjCVTables::VTableRegion::
    AddressInRegion(lldb::addr_t addr, uint32_t &flags) {
  if (!IsValid())
    return false;

  if (addr < m_code_start_addr || addr > m_code_end_addr)
    return false;

  std::vector<VTableDescriptor>::iterator pos, end = m_descriptors.end();
  for (pos = m_descriptors.begin(); pos != end; pos++) {
    if (addr <= (*pos).code_start) {
      flags = (*pos).flags;
      return true;
    }
  }
  return false;
}

void AppleObjCTrampolineHandler::AppleObjCVTables::VTableRegion::Dump(
    Stream &s) {
  s.Printf("Header addr: 0x%" PRIx64 " Code start: 0x%" PRIx64
           " Code End: 0x%" PRIx64 " Next: 0x%" PRIx64 "\n",
           m_header_addr, m_code_start_addr, m_code_end_addr, m_next_region);
  size_t num_elements = m_descriptors.size();
  for (size_t i = 0; i < num_elements; i++) {
    s.Indent();
    s.Printf("Code start: 0x%" PRIx64 " Flags: %d\n",
             m_descriptors[i].code_start, m_descriptors[i].flags);
  }
}

AppleObjCTrampolineHandler::AppleObjCVTables::AppleObjCVTables(
    const ProcessSP &process_sp, const ModuleSP &objc_module_sp)
    : m_process_wp(), m_trampoline_header(LLDB_INVALID_ADDRESS),
      m_trampolines_changed_bp_id(LLDB_INVALID_BREAK_ID),
      m_objc_module_sp(objc_module_sp) {
  if (process_sp)
    m_process_wp = process_sp;
}

AppleObjCTrampolineHandler::AppleObjCVTables::~AppleObjCVTables() {
  ProcessSP process_sp = GetProcessSP();
  if (process_sp) {
    if (m_trampolines_changed_bp_id != LLDB_INVALID_BREAK_ID)
      process_sp->GetTarget().RemoveBreakpointByID(m_trampolines_changed_bp_id);
  }
}

bool AppleObjCTrampolineHandler::AppleObjCVTables::InitializeVTableSymbols() {
  if (m_trampoline_header != LLDB_INVALID_ADDRESS)
    return true;

  ProcessSP process_sp = GetProcessSP();
  if (process_sp) {
    Target &target = process_sp->GetTarget();

    if (!m_objc_module_sp) {
      for (ModuleSP module_sp : target.GetImages().Modules()) {
        if (ObjCLanguageRuntime::Get(*process_sp)
                ->IsModuleObjCLibrary(module_sp)) {
          m_objc_module_sp = module_sp;
          break;
        }
      }
    }

    if (m_objc_module_sp) {
      ConstString trampoline_name("gdb_objc_trampolines");
      const Symbol *trampoline_symbol =
          m_objc_module_sp->FindFirstSymbolWithNameAndType(trampoline_name,
                                                           eSymbolTypeData);
      if (trampoline_symbol != nullptr) {
        m_trampoline_header = trampoline_symbol->GetLoadAddress(&target);
        if (m_trampoline_header == LLDB_INVALID_ADDRESS)
          return false;

        // Next look up the "changed" symbol and set a breakpoint on that...
        ConstString changed_name("gdb_objc_trampolines_changed");
        const Symbol *changed_symbol =
            m_objc_module_sp->FindFirstSymbolWithNameAndType(changed_name,
                                                             eSymbolTypeCode);
        if (changed_symbol != nullptr) {
          const Address changed_symbol_addr = changed_symbol->GetAddress();
          if (!changed_symbol_addr.IsValid())
            return false;

          lldb::addr_t changed_addr =
              changed_symbol_addr.GetOpcodeLoadAddress(&target);
          if (changed_addr != LLDB_INVALID_ADDRESS) {
            BreakpointSP trampolines_changed_bp_sp =
                target.CreateBreakpoint(changed_addr, true, false);
            if (trampolines_changed_bp_sp) {
              m_trampolines_changed_bp_id = trampolines_changed_bp_sp->GetID();
              trampolines_changed_bp_sp->SetCallback(RefreshTrampolines, this,
                                                     true);
              trampolines_changed_bp_sp->SetBreakpointKind(
                  "objc-trampolines-changed");
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

bool AppleObjCTrampolineHandler::AppleObjCVTables::RefreshTrampolines(
    void *baton, StoppointCallbackContext *context, lldb::user_id_t break_id,
    lldb::user_id_t break_loc_id) {
  AppleObjCVTables *vtable_handler = (AppleObjCVTables *)baton;
  if (vtable_handler->InitializeVTableSymbols()) {
    // The Update function is called with the address of an added region.  So we
    // grab that address, and
    // feed it into ReadRegions.  Of course, our friend the ABI will get the
    // values for us.
    ExecutionContext exe_ctx(context->exe_ctx_ref);
    Process *process = exe_ctx.GetProcessPtr();
    const ABI *abi = process->GetABI().get();

    TypeSystemClangSP scratch_ts_sp =
        ScratchTypeSystemClang::GetForTarget(process->GetTarget());
    if (!scratch_ts_sp)
      return false;

    ValueList argument_values;
    Value input_value;
    CompilerType clang_void_ptr_type =
        scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();

    input_value.SetValueType(Value::ValueType::Scalar);
    // input_value.SetContext (Value::eContextTypeClangType,
    // clang_void_ptr_type);
    input_value.SetCompilerType(clang_void_ptr_type);
    argument_values.PushValue(input_value);

    bool success =
        abi->GetArgumentValues(exe_ctx.GetThreadRef(), argument_values);
    if (!success)
      return false;

    // Now get a pointer value from the zeroth argument.
    Status error;
    DataExtractor data;
    error = argument_values.GetValueAtIndex(0)->GetValueAsData(&exe_ctx, data,
                                                               nullptr);
    lldb::offset_t offset = 0;
    lldb::addr_t region_addr = data.GetAddress(&offset);

    if (region_addr != 0)
      vtable_handler->ReadRegions(region_addr);
  }
  return false;
}

bool AppleObjCTrampolineHandler::AppleObjCVTables::ReadRegions() {
  // The no argument version reads the  start region from the value of
  // the gdb_regions_header, and gets started from there.

  m_regions.clear();
  if (!InitializeVTableSymbols())
    return false;
  Status error;
  ProcessSP process_sp = GetProcessSP();
  if (process_sp) {
    lldb::addr_t region_addr =
        process_sp->ReadPointerFromMemory(m_trampoline_header, error);
    if (error.Success())
      return ReadRegions(region_addr);
  }
  return false;
}

bool AppleObjCTrampolineHandler::AppleObjCVTables::ReadRegions(
    lldb::addr_t region_addr) {
  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return false;

  Log *log = GetLog(LLDBLog::Step);

  // We aren't starting at the trampoline symbol.
  InitializeVTableSymbols();
  lldb::addr_t next_region = region_addr;

  // Read in the sizes of the headers.
  while (next_region != 0) {
    m_regions.push_back(VTableRegion(this, next_region));
    if (!m_regions.back().IsValid()) {
      m_regions.clear();
      return false;
    }
    if (log) {
      StreamString s;
      m_regions.back().Dump(s);
      LLDB_LOGF(log, "Read vtable region: \n%s", s.GetData());
    }

    next_region = m_regions.back().GetNextRegionAddr();
  }

  return true;
}

bool AppleObjCTrampolineHandler::AppleObjCVTables::IsAddressInVTables(
    lldb::addr_t addr, uint32_t &flags) {
  region_collection::iterator pos, end = m_regions.end();
  for (pos = m_regions.begin(); pos != end; pos++) {
    if ((*pos).AddressInRegion(addr, flags))
      return true;
  }
  return false;
}

const AppleObjCTrampolineHandler::DispatchFunction
    AppleObjCTrampolineHandler::g_dispatch_functions[] = {
        // NAME                              STRET  SUPER  SUPER2  FIXUP TYPE
        {"objc_msgSend", false, false, false, DispatchFunction::eFixUpNone},
        {"objc_msgSend_fixup", false, false, false,
         DispatchFunction::eFixUpToFix},
        {"objc_msgSend_fixedup", false, false, false,
         DispatchFunction::eFixUpFixed},
        {"objc_msgSend_stret", true, false, false,
         DispatchFunction::eFixUpNone},
        {"objc_msgSend_stret_fixup", true, false, false,
         DispatchFunction::eFixUpToFix},
        {"objc_msgSend_stret_fixedup", true, false, false,
         DispatchFunction::eFixUpFixed},
        {"objc_msgSend_fpret", false, false, false,
         DispatchFunction::eFixUpNone},
        {"objc_msgSend_fpret_fixup", false, false, false,
         DispatchFunction::eFixUpToFix},
        {"objc_msgSend_fpret_fixedup", false, false, false,
         DispatchFunction::eFixUpFixed},
        {"objc_msgSend_fp2ret", false, false, true,
         DispatchFunction::eFixUpNone},
        {"objc_msgSend_fp2ret_fixup", false, false, true,
         DispatchFunction::eFixUpToFix},
        {"objc_msgSend_fp2ret_fixedup", false, false, true,
         DispatchFunction::eFixUpFixed},
        {"objc_msgSendSuper", false, true, false, DispatchFunction::eFixUpNone},
        {"objc_msgSendSuper_stret", true, true, false,
         DispatchFunction::eFixUpNone},
        {"objc_msgSendSuper2", false, true, true, DispatchFunction::eFixUpNone},
        {"objc_msgSendSuper2_fixup", false, true, true,
         DispatchFunction::eFixUpToFix},
        {"objc_msgSendSuper2_fixedup", false, true, true,
         DispatchFunction::eFixUpFixed},
        {"objc_msgSendSuper2_stret", true, true, true,
         DispatchFunction::eFixUpNone},
        {"objc_msgSendSuper2_stret_fixup", true, true, true,
         DispatchFunction::eFixUpToFix},
        {"objc_msgSendSuper2_stret_fixedup", true, true, true,
         DispatchFunction::eFixUpFixed},
};

// This is the table of ObjC "accelerated dispatch" functions.  They are a set
// of objc methods that are "seldom overridden" and so the compiler replaces the
// objc_msgSend with a call to one of the dispatch functions.  That will check
// whether the method has been overridden, and directly call the Foundation 
// implementation if not.  
// This table is supposed to be complete.  If ones get added in the future, we
// will have to add them to the table.
const char *AppleObjCTrampolineHandler::g_opt_dispatch_names[] = {
    "objc_alloc",
    "objc_autorelease",
    "objc_release",
    "objc_retain",
    "objc_alloc_init",
    "objc_allocWithZone",
    "objc_opt_class",
    "objc_opt_isKindOfClass",
    "objc_opt_new",
    "objc_opt_respondsToSelector",
    "objc_opt_self",
};

AppleObjCTrampolineHandler::AppleObjCTrampolineHandler(
    const ProcessSP &process_sp, const ModuleSP &objc_module_sp)
    : m_process_wp(), m_objc_module_sp(objc_module_sp),
      m_impl_fn_addr(LLDB_INVALID_ADDRESS),
      m_impl_stret_fn_addr(LLDB_INVALID_ADDRESS),
      m_msg_forward_addr(LLDB_INVALID_ADDRESS),
      m_msg_forward_stret_addr(LLDB_INVALID_ADDRESS) {
  if (process_sp)
    m_process_wp = process_sp;
  // Look up the known resolution functions:

  ConstString get_impl_name("class_getMethodImplementation");
  ConstString get_impl_stret_name("class_getMethodImplementation_stret");
  ConstString msg_forward_name("_objc_msgForward");
  ConstString msg_forward_stret_name("_objc_msgForward_stret");

  Target *target = process_sp ? &process_sp->GetTarget() : nullptr;
  const Symbol *class_getMethodImplementation =
      m_objc_module_sp->FindFirstSymbolWithNameAndType(get_impl_name,
                                                       eSymbolTypeCode);
  const Symbol *class_getMethodImplementation_stret =
      m_objc_module_sp->FindFirstSymbolWithNameAndType(get_impl_stret_name,
                                                       eSymbolTypeCode);
  const Symbol *msg_forward = m_objc_module_sp->FindFirstSymbolWithNameAndType(
      msg_forward_name, eSymbolTypeCode);
  const Symbol *msg_forward_stret =
      m_objc_module_sp->FindFirstSymbolWithNameAndType(msg_forward_stret_name,
                                                       eSymbolTypeCode);

  if (class_getMethodImplementation)
    m_impl_fn_addr =
        class_getMethodImplementation->GetAddress().GetOpcodeLoadAddress(
            target);
  if (class_getMethodImplementation_stret)
    m_impl_stret_fn_addr =
        class_getMethodImplementation_stret->GetAddress().GetOpcodeLoadAddress(
            target);
  if (msg_forward)
    m_msg_forward_addr = msg_forward->GetAddress().GetOpcodeLoadAddress(target);
  if (msg_forward_stret)
    m_msg_forward_stret_addr =
        msg_forward_stret->GetAddress().GetOpcodeLoadAddress(target);

  // FIXME: Do some kind of logging here.
  if (m_impl_fn_addr == LLDB_INVALID_ADDRESS) {
    // If we can't even find the ordinary get method implementation function,
    // then we aren't going to be able to
    // step through any method dispatches.  Warn to that effect and get out of
    // here.
    if (process_sp->CanJIT()) {
      process_sp->GetTarget().GetDebugger().GetErrorStream().Printf(
          "Could not find implementation lookup function \"%s\""
          " step in through ObjC method dispatch will not work.\n",
          get_impl_name.AsCString());
    }
    return;
  }
  
  // We will either set the implementation to the _stret or non_stret version,
  // so either way it's safe to start filling the m_lookup_..._code here.
  m_lookup_implementation_function_code.assign(
          g_lookup_implementation_function_common_code);

  if (m_impl_stret_fn_addr == LLDB_INVALID_ADDRESS) {
    // It there is no stret return lookup function, assume that it is the same
    // as the straight lookup:
    m_impl_stret_fn_addr = m_impl_fn_addr;
    // Also we will use the version of the lookup code that doesn't rely on the
    // stret version of the function.
    m_lookup_implementation_function_code.append(
        g_lookup_implementation_no_stret_function_code);
  } else {
    m_lookup_implementation_function_code.append(
        g_lookup_implementation_with_stret_function_code);
  }

  // Look up the addresses for the objc dispatch functions and cache
  // them.  For now I'm inspecting the symbol names dynamically to
  // figure out how to dispatch to them.  If it becomes more
  // complicated than this we can turn the g_dispatch_functions char *
  // array into a template table, and populate the DispatchFunction
  // map from there.

  for (size_t i = 0; i != std::size(g_dispatch_functions); i++) {
    ConstString name_const_str(g_dispatch_functions[i].name);
    const Symbol *msgSend_symbol =
        m_objc_module_sp->FindFirstSymbolWithNameAndType(name_const_str,
                                                         eSymbolTypeCode);
    if (msgSend_symbol && msgSend_symbol->ValueIsAddress()) {
      // FIXME: Make g_dispatch_functions static table of
      // DispatchFunctions, and have the map be address->index.
      // Problem is we also need to lookup the dispatch function.  For
      // now we could have a side table of stret & non-stret dispatch
      // functions.  If that's as complex as it gets, we're fine.

      lldb::addr_t sym_addr =
          msgSend_symbol->GetAddressRef().GetOpcodeLoadAddress(target);

      m_msgSend_map.insert(std::pair<lldb::addr_t, int>(sym_addr, i));
    }
  }

  // Similarly, cache the addresses of the "optimized dispatch" function.
  for (size_t i = 0; i != std::size(g_opt_dispatch_names); i++) {
    ConstString name_const_str(g_opt_dispatch_names[i]);
    const Symbol *msgSend_symbol =
        m_objc_module_sp->FindFirstSymbolWithNameAndType(name_const_str,
                                                         eSymbolTypeCode);
    if (msgSend_symbol && msgSend_symbol->ValueIsAddress()) {
      lldb::addr_t sym_addr =
          msgSend_symbol->GetAddressRef().GetOpcodeLoadAddress(target);

      m_opt_dispatch_map.emplace(sym_addr, i);
    }
  }

  // Build our vtable dispatch handler here:
  m_vtables_up =
      std::make_unique<AppleObjCVTables>(process_sp, m_objc_module_sp);
  if (m_vtables_up)
    m_vtables_up->ReadRegions();
}

lldb::addr_t
AppleObjCTrampolineHandler::SetupDispatchFunction(Thread &thread,
                                                  ValueList &dispatch_values) {
  ThreadSP thread_sp(thread.shared_from_this());
  ExecutionContext exe_ctx(thread_sp);
  Log *log = GetLog(LLDBLog::Step);

  lldb::addr_t args_addr = LLDB_INVALID_ADDRESS;
  FunctionCaller *impl_function_caller = nullptr;

  // Scope for mutex locker:
  {
    std::lock_guard<std::mutex> guard(m_impl_function_mutex);

    // First stage is to make the ClangUtility to hold our injected function:

    if (!m_impl_code) {
      if (!m_lookup_implementation_function_code.empty()) {
        auto utility_fn_or_error = exe_ctx.GetTargetRef().CreateUtilityFunction(
            m_lookup_implementation_function_code,
            g_lookup_implementation_function_name, eLanguageTypeC, exe_ctx);
        if (!utility_fn_or_error) {
          LLDB_LOG_ERROR(
              log, utility_fn_or_error.takeError(),
              "Failed to get Utility Function for implementation lookup: {0}.");
          return args_addr;
        }
        m_impl_code = std::move(*utility_fn_or_error);
      } else {
        LLDB_LOGF(log, "No method lookup implementation code.");
        return LLDB_INVALID_ADDRESS;
      }

      // Next make the runner function for our implementation utility function.
      TypeSystemClangSP scratch_ts_sp = ScratchTypeSystemClang::GetForTarget(
          thread.GetProcess()->GetTarget());
      if (!scratch_ts_sp)
        return LLDB_INVALID_ADDRESS;

      CompilerType clang_void_ptr_type =
          scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();
      Status error;

      impl_function_caller = m_impl_code->MakeFunctionCaller(
          clang_void_ptr_type, dispatch_values, thread_sp, error);
      if (error.Fail()) {
        LLDB_LOGF(log,
                  "Error getting function caller for dispatch lookup: \"%s\".",
                  error.AsCString());
        return args_addr;
      }
    } else {
      impl_function_caller = m_impl_code->GetFunctionCaller();
    }
  }

  // Now write down the argument values for this particular call.
  // This looks like it might be a race condition if other threads
  // were calling into here, but actually it isn't because we allocate
  // a new args structure for this call by passing args_addr =
  // LLDB_INVALID_ADDRESS...

  DiagnosticManager diagnostics;
  if (!impl_function_caller->WriteFunctionArguments(
          exe_ctx, args_addr, dispatch_values, diagnostics)) {
    if (log) {
      LLDB_LOGF(log, "Error writing function arguments.");
      diagnostics.Dump(log);
    }
    return args_addr;
  }

  return args_addr;
}

const AppleObjCTrampolineHandler::DispatchFunction *
AppleObjCTrampolineHandler::FindDispatchFunction(lldb::addr_t addr) {
  MsgsendMap::iterator pos;
  pos = m_msgSend_map.find(addr);
  if (pos != m_msgSend_map.end()) {
    return &g_dispatch_functions[(*pos).second];
  }
  return nullptr;
}

void AppleObjCTrampolineHandler::ForEachDispatchFunction(
    std::function<void(lldb::addr_t, const DispatchFunction &)> callback) {
  for (auto elem : m_msgSend_map) {
    callback(elem.first, g_dispatch_functions[elem.second]);
  }
}

ThreadPlanSP
AppleObjCTrampolineHandler::GetStepThroughDispatchPlan(Thread &thread,
                                                       bool stop_others) {
  ThreadPlanSP ret_plan_sp;
  lldb::addr_t curr_pc = thread.GetRegisterContext()->GetPC();

  DispatchFunction vtable_dispatch = {"vtable", false, false, false,
                                      DispatchFunction::eFixUpFixed};
  // The selector specific stubs are a wrapper for objc_msgSend.  They don't get
  // passed a SEL, but instead the selector string is encoded in the stub
  // name, in the form:
  //   objc_msgSend$SelectorName
  // and the stub figures out the uniqued selector.  If we find ourselves in
  // one of these stubs, we strip off the selector string and pass that to the
  // implementation finder function, which looks up the SEL (you have to do this
  // in process) and passes that to the runtime lookup function.
  DispatchFunction sel_stub_dispatch = {"sel-specific-stub", false, false,
                                        false, DispatchFunction::eFixUpNone};

  // First step is to see if we're in a selector-specific dispatch stub.
  // Those are of the form _objc_msgSend$<SELECTOR>, so see if the current
  // function has that name:
  Address func_addr;
  Target &target = thread.GetProcess()->GetTarget();
  llvm::StringRef sym_name;
  const DispatchFunction *this_dispatch = nullptr;

  if (target.ResolveLoadAddress(curr_pc, func_addr)) {
    Symbol *curr_sym = func_addr.CalculateSymbolContextSymbol();
    if (curr_sym)
      sym_name = curr_sym->GetName().GetStringRef();

    if (!sym_name.empty() && !sym_name.consume_front("objc_msgSend$"))
      sym_name = {};
    else
      this_dispatch = &sel_stub_dispatch;
  }
  bool in_selector_stub = !sym_name.empty();
  // Second step is to look and see if we are in one of the known ObjC
  // dispatch functions.  We've already compiled a table of same, so
  // consult it.

  if (!in_selector_stub)
    this_dispatch = FindDispatchFunction(curr_pc);

  // Next check to see if we are in a vtable region:

  if (!this_dispatch && m_vtables_up) {
    uint32_t flags;
    if (m_vtables_up->IsAddressInVTables(curr_pc, flags)) {
      vtable_dispatch.stret_return =
          (flags & AppleObjCVTables::eOBJC_TRAMPOLINE_STRET) ==
          AppleObjCVTables::eOBJC_TRAMPOLINE_STRET;
      this_dispatch = &vtable_dispatch;
    }
  }

  // Since we set this_dispatch in both the vtable & sel specific stub cases
  // this if will be used for all three of those cases.
  if (this_dispatch) {
    Log *log = GetLog(LLDBLog::Step);

    // We are decoding a method dispatch.  First job is to pull the
    // arguments out.  If we are in a regular stub, we get self & selector,
    // but if we are in a selector-specific stub, we'll have to get that from
    // the string sym_name.

    lldb::StackFrameSP thread_cur_frame = thread.GetStackFrameAtIndex(0);

    const ABI *abi = nullptr;
    ProcessSP process_sp(thread.CalculateProcess());
    if (process_sp)
      abi = process_sp->GetABI().get();
    if (abi == nullptr)
      return ret_plan_sp;

    TargetSP target_sp(thread.CalculateTarget());

    TypeSystemClangSP scratch_ts_sp =
        ScratchTypeSystemClang::GetForTarget(*target_sp);
    if (!scratch_ts_sp)
      return ret_plan_sp;

    ValueList argument_values;
    Value void_ptr_value;
    CompilerType clang_void_ptr_type =
        scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();
    void_ptr_value.SetValueType(Value::ValueType::Scalar);
    // void_ptr_value.SetContext (Value::eContextTypeClangType,
    // clang_void_ptr_type);
    void_ptr_value.SetCompilerType(clang_void_ptr_type);

    int obj_index;
    int sel_index;

    // If this is a selector-specific stub then just push one value, 'cause
    // we only get the object.
    // If this is a struct return dispatch, then the first argument is
    // the return struct pointer, and the object is the second, and
    // the selector is the third.
    // Otherwise the object is the first and the selector the second.
    if (in_selector_stub) {
      obj_index = 0;
      sel_index = 1;
      argument_values.PushValue(void_ptr_value);
    } else if (this_dispatch->stret_return) {
      obj_index = 1;
      sel_index = 2;
      argument_values.PushValue(void_ptr_value);
      argument_values.PushValue(void_ptr_value);
      argument_values.PushValue(void_ptr_value);
    } else {
      obj_index = 0;
      sel_index = 1;
      argument_values.PushValue(void_ptr_value);
      argument_values.PushValue(void_ptr_value);
    }

    bool success = abi->GetArgumentValues(thread, argument_values);
    if (!success)
      return ret_plan_sp;

    lldb::addr_t obj_addr =
        argument_values.GetValueAtIndex(obj_index)->GetScalar().ULongLong();
    if (obj_addr == 0x0) {
      LLDB_LOGF(
          log,
          "Asked to step to dispatch to nil object, returning empty plan.");
      return ret_plan_sp;
    }

    ExecutionContext exe_ctx(thread.shared_from_this());
    // isa_addr will store the class pointer that the method is being
    // dispatched to - so either the class directly or the super class
    // if this is one of the objc_msgSendSuper flavors.  That's mostly
    // used to look up the class/selector pair in our cache.

    lldb::addr_t isa_addr = LLDB_INVALID_ADDRESS;
    lldb::addr_t sel_addr = LLDB_INVALID_ADDRESS;
    // If we are not in a selector stub, get the sel address from the arguments.
    if (!in_selector_stub)
      sel_addr =
          argument_values.GetValueAtIndex(sel_index)->GetScalar().ULongLong();

    // Figure out the class this is being dispatched to and see if
    // we've already cached this method call, If so we can push a
    // run-to-address plan directly.  Otherwise we have to figure out
    // where the implementation lives.

    if (this_dispatch->is_super) {
      if (this_dispatch->is_super2) {
        // In the objc_msgSendSuper2 case, we don't get the object
        // directly, we get a structure containing the object and the
        // class to which the super message is being sent.  So we need
        // to dig the super out of the class and use that.

        Value super_value(*(argument_values.GetValueAtIndex(obj_index)));
        super_value.GetScalar() += process_sp->GetAddressByteSize();
        super_value.ResolveValue(&exe_ctx);

        if (super_value.GetScalar().IsValid()) {

          // isa_value now holds the class pointer.  The second word of the
          // class pointer is the super-class pointer:
          super_value.GetScalar() += process_sp->GetAddressByteSize();
          super_value.ResolveValue(&exe_ctx);
          if (super_value.GetScalar().IsValid())
            isa_addr = super_value.GetScalar().ULongLong();
          else {
            LLDB_LOGF(log, "Failed to extract the super class value from the "
                           "class in objc_super.");
          }
        } else {
          LLDB_LOGF(log, "Failed to extract the class value from objc_super.");
        }
      } else {
        // In the objc_msgSendSuper case, we don't get the object
        // directly, we get a two element structure containing the
        // object and the super class to which the super message is
        // being sent.  So the class we want is the second element of
        // this structure.

        Value super_value(*(argument_values.GetValueAtIndex(obj_index)));
        super_value.GetScalar() += process_sp->GetAddressByteSize();
        super_value.ResolveValue(&exe_ctx);

        if (super_value.GetScalar().IsValid()) {
          isa_addr = super_value.GetScalar().ULongLong();
        } else {
          LLDB_LOGF(log, "Failed to extract the class value from objc_super.");
        }
      }
    } else {
      // In the direct dispatch case, the object->isa is the class pointer we
      // want.

      // This is a little cheesy, but since object->isa is the first field,
      // making the object value a load address value and resolving it will get
      // the pointer sized data pointed to by that value...

      // Note, it isn't a fatal error not to be able to get the
      // address from the object, since this might be a "tagged
      // pointer" which isn't a real object, but rather some word
      // length encoded dingus.

      Value isa_value(*(argument_values.GetValueAtIndex(obj_index)));

      isa_value.SetValueType(Value::ValueType::LoadAddress);
      isa_value.ResolveValue(&exe_ctx);
      if (isa_value.GetScalar().IsValid()) {
        isa_addr = isa_value.GetScalar().ULongLong();
      } else {
        LLDB_LOGF(log, "Failed to extract the isa value from object.");
      }
    }

    // Okay, we've got the address of the class for which we're resolving this,
    // let's see if it's in our cache:
    lldb::addr_t impl_addr = LLDB_INVALID_ADDRESS;
    // If this is a regular dispatch, look up the sel in our addr to sel cache:
    if (isa_addr != LLDB_INVALID_ADDRESS) {
      ObjCLanguageRuntime *objc_runtime =
          ObjCLanguageRuntime::Get(*thread.GetProcess());
      assert(objc_runtime != nullptr);
      if (!in_selector_stub) {
        LLDB_LOG(log, "Resolving call for class - {0} and selector - {1}",
                 isa_addr, sel_addr);
        impl_addr = objc_runtime->LookupInMethodCache(isa_addr, sel_addr);
      } else {
        LLDB_LOG(log, "Resolving call for class - {0} and selector - {1}",
                 isa_addr, sym_name);
        impl_addr = objc_runtime->LookupInMethodCache(isa_addr, sym_name);
      }
    }
    // If it is a selector-specific stub dispatch, look in the string cache:

    if (impl_addr != LLDB_INVALID_ADDRESS) {
      // Yup, it was in the cache, so we can run to that address directly.

      LLDB_LOGF(log, "Found implementation address in cache: 0x%" PRIx64,
                impl_addr);

      ret_plan_sp = std::make_shared<ThreadPlanRunToAddress>(thread, impl_addr,
                                                             stop_others);
    } else {
      // We haven't seen this class/selector pair yet.  Look it up.
      StreamString errors;
      Address impl_code_address;

      ValueList dispatch_values;

      // We've will inject a little function in the target that takes the
      // object, selector/selector string and some flags,
      // and figures out the implementation.  Looks like:
      //      void *__lldb_objc_find_implementation_for_selector (void *object,
      //                                                          void *sel,
      //                                                          int
      //                                                          is_str_ptr,
      //                                                          int is_stret,
      //                                                          int is_super,
      //                                                          int is_super2,
      //                                                          int is_fixup,
      //                                                          int is_fixed,
      //                                                          int debug)
      // If we don't have an actual SEL, but rather a string version of the
      // selector WE injected, set is_str_ptr to true, and sel to the address
      // of the string.
      // So set up the arguments for that call.

      dispatch_values.PushValue(*(argument_values.GetValueAtIndex(obj_index)));
      lldb::addr_t sel_str_addr = LLDB_INVALID_ADDRESS;
      if (!in_selector_stub) {
        // If we don't have a selector string, push the selector from arguments.
        dispatch_values.PushValue(
            *(argument_values.GetValueAtIndex(sel_index)));
      } else {
        // Otherwise, inject the string into the target, and push that value for
        // the sel argument.
        Status error;
        sel_str_addr = process_sp->AllocateMemory(
            sym_name.size() + 1, ePermissionsReadable | ePermissionsWritable,
            error);
        if (sel_str_addr == LLDB_INVALID_ADDRESS || error.Fail()) {
          LLDB_LOG(log,
                   "Could not allocate memory for selector string {0}: {1}",
                   sym_name, error);
          return ret_plan_sp;
        }
        process_sp->WriteMemory(sel_str_addr, sym_name.str().c_str(),
                                sym_name.size() + 1, error);
        if (error.Fail()) {
          LLDB_LOG(log, "Could not write string to address {0}", sel_str_addr);
          return ret_plan_sp;
        }
        Value sel_ptr_value(void_ptr_value);
        sel_ptr_value.GetScalar() = sel_str_addr;
        dispatch_values.PushValue(sel_ptr_value);
      }

      Value flag_value;
      CompilerType clang_int_type =
          scratch_ts_sp->GetBuiltinTypeForEncodingAndBitSize(
              lldb::eEncodingSint, 32);
      flag_value.SetValueType(Value::ValueType::Scalar);
      // flag_value.SetContext (Value::eContextTypeClangType, clang_int_type);
      flag_value.SetCompilerType(clang_int_type);

      if (in_selector_stub)
        flag_value.GetScalar() = 1;
      else
        flag_value.GetScalar() = 0;
      dispatch_values.PushValue(flag_value);

      if (this_dispatch->stret_return)
        flag_value.GetScalar() = 1;
      else
        flag_value.GetScalar() = 0;
      dispatch_values.PushValue(flag_value);

      if (this_dispatch->is_super)
        flag_value.GetScalar() = 1;
      else
        flag_value.GetScalar() = 0;
      dispatch_values.PushValue(flag_value);

      if (this_dispatch->is_super2)
        flag_value.GetScalar() = 1;
      else
        flag_value.GetScalar() = 0;
      dispatch_values.PushValue(flag_value);

      switch (this_dispatch->fixedup) {
      case DispatchFunction::eFixUpNone:
        flag_value.GetScalar() = 0;
        dispatch_values.PushValue(flag_value);
        dispatch_values.PushValue(flag_value);
        break;
      case DispatchFunction::eFixUpFixed:
        flag_value.GetScalar() = 1;
        dispatch_values.PushValue(flag_value);
        flag_value.GetScalar() = 1;
        dispatch_values.PushValue(flag_value);
        break;
      case DispatchFunction::eFixUpToFix:
        flag_value.GetScalar() = 1;
        dispatch_values.PushValue(flag_value);
        flag_value.GetScalar() = 0;
        dispatch_values.PushValue(flag_value);
        break;
      }
      if (log && log->GetVerbose())
        flag_value.GetScalar() = 1;
      else
        flag_value.GetScalar() = 0; // FIXME - Set to 0 when debugging is done.
      dispatch_values.PushValue(flag_value);

      ret_plan_sp = std::make_shared<AppleThreadPlanStepThroughObjCTrampoline>(
          thread, *this, dispatch_values, isa_addr, sel_addr, sel_str_addr,
          sym_name);
      if (log) {
        StreamString s;
        ret_plan_sp->GetDescription(&s, eDescriptionLevelFull);
        LLDB_LOGF(log, "Using ObjC step plan: %s.\n", s.GetData());
      }
    }
  }
  
  // Finally, check if we have hit an "optimized dispatch" function.  This will
  // either directly call the base implementation or dispatch an objc_msgSend
  // if the method has been overridden.  So we just do a "step in/step out",
  // setting a breakpoint on objc_msgSend, and if we hit the msgSend, we 
  // will automatically step in again.  That's the job of the 
  // AppleThreadPlanStepThroughDirectDispatch.
  if (!this_dispatch && !ret_plan_sp) {
    MsgsendMap::iterator pos;
    pos = m_opt_dispatch_map.find(curr_pc);
    if (pos != m_opt_dispatch_map.end()) {
      const char *opt_name = g_opt_dispatch_names[(*pos).second];
      ret_plan_sp = std::make_shared<AppleThreadPlanStepThroughDirectDispatch>(
          thread, *this, opt_name);
    }
  }

  return ret_plan_sp;
}

FunctionCaller *
AppleObjCTrampolineHandler::GetLookupImplementationFunctionCaller() {
  return m_impl_code->GetFunctionCaller();
}
