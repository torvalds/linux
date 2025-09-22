//===-- JITLoaderGDB.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "JITLoaderGDB.h"
#include "Plugins/ObjectFile/Mach-O/ObjectFileMachO.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/Support/MathExtras.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(JITLoaderGDB)

namespace {
// Debug Interface Structures
enum jit_actions_t { JIT_NOACTION = 0, JIT_REGISTER_FN, JIT_UNREGISTER_FN };

template <typename ptr_t> struct jit_code_entry {
  ptr_t next_entry;   // pointer
  ptr_t prev_entry;   // pointer
  ptr_t symfile_addr; // pointer
  uint64_t symfile_size;
};

template <typename ptr_t> struct jit_descriptor {
  uint32_t version;
  uint32_t action_flag; // Values are jit_action_t
  ptr_t relevant_entry; // pointer
  ptr_t first_entry;    // pointer
};

enum EnableJITLoaderGDB {
  eEnableJITLoaderGDBDefault,
  eEnableJITLoaderGDBOn,
  eEnableJITLoaderGDBOff,
};

static constexpr OptionEnumValueElement g_enable_jit_loader_gdb_enumerators[] =
    {
        {
            eEnableJITLoaderGDBDefault,
            "default",
            "Enable JIT compilation interface for all platforms except macOS",
        },
        {
            eEnableJITLoaderGDBOn,
            "on",
            "Enable JIT compilation interface",
        },
        {
            eEnableJITLoaderGDBOff,
            "off",
            "Disable JIT compilation interface",
        },
};

#define LLDB_PROPERTIES_jitloadergdb
#include "JITLoaderGDBProperties.inc"

enum {
#define LLDB_PROPERTIES_jitloadergdb
#include "JITLoaderGDBPropertiesEnum.inc"
  ePropertyEnableJITBreakpoint
};

class PluginProperties : public Properties {
public:
  static llvm::StringRef GetSettingName() {
    return JITLoaderGDB::GetPluginNameStatic();
  }

  PluginProperties() {
    m_collection_sp = std::make_shared<OptionValueProperties>(GetSettingName());
    m_collection_sp->Initialize(g_jitloadergdb_properties);
  }

  EnableJITLoaderGDB GetEnable() const {
    return GetPropertyAtIndexAs<EnableJITLoaderGDB>(
        ePropertyEnable,
        static_cast<EnableJITLoaderGDB>(
            g_jitloadergdb_properties[ePropertyEnable].default_uint_value));
  }
};
} // namespace

static PluginProperties &GetGlobalPluginProperties() {
  static PluginProperties g_settings;
  return g_settings;
}

template <typename ptr_t>
static bool ReadJITEntry(const addr_t from_addr, Process *process,
                         jit_code_entry<ptr_t> *entry) {
  lldbassert(from_addr % sizeof(ptr_t) == 0);

  ArchSpec::Core core = process->GetTarget().GetArchitecture().GetCore();
  bool i386_target = ArchSpec::kCore_x86_32_first <= core &&
                     core <= ArchSpec::kCore_x86_32_last;
  uint8_t uint64_align_bytes = i386_target ? 4 : 8;
  const size_t data_byte_size =
      llvm::alignTo(sizeof(ptr_t) * 3, uint64_align_bytes) + sizeof(uint64_t);

  Status error;
  DataBufferHeap data(data_byte_size, 0);
  size_t bytes_read = process->ReadMemory(from_addr, data.GetBytes(),
                                          data.GetByteSize(), error);
  if (bytes_read != data_byte_size || !error.Success())
    return false;

  DataExtractor extractor(data.GetBytes(), data.GetByteSize(),
                          process->GetByteOrder(), sizeof(ptr_t));
  lldb::offset_t offset = 0;
  entry->next_entry = extractor.GetAddress(&offset);
  entry->prev_entry = extractor.GetAddress(&offset);
  entry->symfile_addr = extractor.GetAddress(&offset);
  offset = llvm::alignTo(offset, uint64_align_bytes);
  entry->symfile_size = extractor.GetU64(&offset);

  return true;
}

JITLoaderGDB::JITLoaderGDB(lldb_private::Process *process)
    : JITLoader(process), m_jit_objects(),
      m_jit_break_id(LLDB_INVALID_BREAK_ID),
      m_jit_descriptor_addr(LLDB_INVALID_ADDRESS) {}

JITLoaderGDB::~JITLoaderGDB() {
  if (LLDB_BREAK_ID_IS_VALID(m_jit_break_id))
    m_process->GetTarget().RemoveBreakpointByID(m_jit_break_id);
}

void JITLoaderGDB::DebuggerInitialize(Debugger &debugger) {
  if (!PluginManager::GetSettingForJITLoaderPlugin(
          debugger, PluginProperties::GetSettingName())) {
    const bool is_global_setting = true;
    PluginManager::CreateSettingForJITLoaderPlugin(
        debugger, GetGlobalPluginProperties().GetValueProperties(),
        "Properties for the JIT LoaderGDB plug-in.", is_global_setting);
  }
}

void JITLoaderGDB::DidAttach() {
  Target &target = m_process->GetTarget();
  ModuleList &module_list = target.GetImages();
  SetJITBreakpoint(module_list);
}

void JITLoaderGDB::DidLaunch() {
  Target &target = m_process->GetTarget();
  ModuleList &module_list = target.GetImages();
  SetJITBreakpoint(module_list);
}

void JITLoaderGDB::ModulesDidLoad(ModuleList &module_list) {
  if (!DidSetJITBreakpoint() && m_process->IsAlive())
    SetJITBreakpoint(module_list);
}

// Setup the JIT Breakpoint
void JITLoaderGDB::SetJITBreakpoint(lldb_private::ModuleList &module_list) {
  if (DidSetJITBreakpoint())
    return;

  Log *log = GetLog(LLDBLog::JITLoader);
  LLDB_LOGF(log, "JITLoaderGDB::%s looking for JIT register hook",
            __FUNCTION__);

  addr_t jit_addr = GetSymbolAddress(
      module_list, ConstString("__jit_debug_register_code"), eSymbolTypeCode);
  if (jit_addr == LLDB_INVALID_ADDRESS)
    return;

  m_jit_descriptor_addr = GetSymbolAddress(
      module_list, ConstString("__jit_debug_descriptor"), eSymbolTypeData);
  if (m_jit_descriptor_addr == LLDB_INVALID_ADDRESS) {
    LLDB_LOGF(log, "JITLoaderGDB::%s failed to find JIT descriptor address",
              __FUNCTION__);
    return;
  }

  LLDB_LOGF(log, "JITLoaderGDB::%s setting JIT breakpoint", __FUNCTION__);

  Breakpoint *bp =
      m_process->GetTarget().CreateBreakpoint(jit_addr, true, false).get();
  bp->SetCallback(JITDebugBreakpointHit, this, true);
  bp->SetBreakpointKind("jit-debug-register");
  m_jit_break_id = bp->GetID();

  ReadJITDescriptor(true);
}

bool JITLoaderGDB::JITDebugBreakpointHit(void *baton,
                                         StoppointCallbackContext *context,
                                         user_id_t break_id,
                                         user_id_t break_loc_id) {
  Log *log = GetLog(LLDBLog::JITLoader);
  LLDB_LOGF(log, "JITLoaderGDB::%s hit JIT breakpoint", __FUNCTION__);
  JITLoaderGDB *instance = static_cast<JITLoaderGDB *>(baton);
  return instance->ReadJITDescriptor(false);
}

static void updateSectionLoadAddress(const SectionList &section_list,
                                     Target &target, uint64_t symbolfile_addr,
                                     uint64_t symbolfile_size,
                                     uint64_t &vmaddrheuristic,
                                     uint64_t &min_addr, uint64_t &max_addr) {
  const uint32_t num_sections = section_list.GetSize();
  for (uint32_t i = 0; i < num_sections; ++i) {
    SectionSP section_sp(section_list.GetSectionAtIndex(i));
    if (section_sp) {
      if (section_sp->IsFake()) {
        uint64_t lower = (uint64_t)-1;
        uint64_t upper = 0;
        updateSectionLoadAddress(section_sp->GetChildren(), target,
                                 symbolfile_addr, symbolfile_size,
                                 vmaddrheuristic, lower, upper);
        if (lower < min_addr)
          min_addr = lower;
        if (upper > max_addr)
          max_addr = upper;
        const lldb::addr_t slide_amount = lower - section_sp->GetFileAddress();
        section_sp->Slide(slide_amount, false);
        section_sp->GetChildren().Slide(-slide_amount, false);
        section_sp->SetByteSize(upper - lower);
      } else {
        vmaddrheuristic += 2 << section_sp->GetLog2Align();
        uint64_t lower;
        if (section_sp->GetFileAddress() > vmaddrheuristic)
          lower = section_sp->GetFileAddress();
        else {
          lower = symbolfile_addr + section_sp->GetFileOffset();
          section_sp->SetFileAddress(symbolfile_addr +
                                     section_sp->GetFileOffset());
        }
        target.SetSectionLoadAddress(section_sp, lower, true);
        uint64_t upper = lower + section_sp->GetByteSize();
        if (lower < min_addr)
          min_addr = lower;
        if (upper > max_addr)
          max_addr = upper;
        // This is an upper bound, but a good enough heuristic
        vmaddrheuristic += section_sp->GetByteSize();
      }
    }
  }
}

bool JITLoaderGDB::ReadJITDescriptor(bool all_entries) {
  if (m_process->GetTarget().GetArchitecture().GetAddressByteSize() == 8)
    return ReadJITDescriptorImpl<uint64_t>(all_entries);
  else
    return ReadJITDescriptorImpl<uint32_t>(all_entries);
}

template <typename ptr_t>
bool JITLoaderGDB::ReadJITDescriptorImpl(bool all_entries) {
  if (m_jit_descriptor_addr == LLDB_INVALID_ADDRESS)
    return false;

  Log *log = GetLog(LLDBLog::JITLoader);
  Target &target = m_process->GetTarget();
  ModuleList &module_list = target.GetImages();

  jit_descriptor<ptr_t> jit_desc;
  const size_t jit_desc_size = sizeof(jit_desc);
  Status error;
  size_t bytes_read = m_process->ReadMemory(m_jit_descriptor_addr, &jit_desc,
                                            jit_desc_size, error);
  if (bytes_read != jit_desc_size || !error.Success()) {
    LLDB_LOGF(log, "JITLoaderGDB::%s failed to read JIT descriptor",
              __FUNCTION__);
    return false;
  }

  jit_actions_t jit_action = (jit_actions_t)jit_desc.action_flag;
  addr_t jit_relevant_entry = (addr_t)jit_desc.relevant_entry;
  if (all_entries) {
    jit_action = JIT_REGISTER_FN;
    jit_relevant_entry = (addr_t)jit_desc.first_entry;
  }

  while (jit_relevant_entry != 0) {
    jit_code_entry<ptr_t> jit_entry;
    if (!ReadJITEntry(jit_relevant_entry, m_process, &jit_entry)) {
      LLDB_LOGF(log, "JITLoaderGDB::%s failed to read JIT entry at 0x%" PRIx64,
                __FUNCTION__, jit_relevant_entry);
      return false;
    }

    const addr_t &symbolfile_addr = (addr_t)jit_entry.symfile_addr;
    const size_t &symbolfile_size = (size_t)jit_entry.symfile_size;
    ModuleSP module_sp;

    if (jit_action == JIT_REGISTER_FN) {
      LLDB_LOGF(log,
                "JITLoaderGDB::%s registering JIT entry at 0x%" PRIx64
                " (%" PRIu64 " bytes)",
                __FUNCTION__, symbolfile_addr, (uint64_t)symbolfile_size);

      char jit_name[64];
      snprintf(jit_name, 64, "JIT(0x%" PRIx64 ")", symbolfile_addr);
      module_sp = m_process->ReadModuleFromMemory(
          FileSpec(jit_name), symbolfile_addr, symbolfile_size);

      if (module_sp && module_sp->GetObjectFile()) {
        // Object formats (like ELF) have no representation for a JIT type.
        // We will get it wrong, if we deduce it from the header.
        module_sp->GetObjectFile()->SetType(ObjectFile::eTypeJIT);

        // load the symbol table right away
        module_sp->GetObjectFile()->GetSymtab();

        m_jit_objects.insert(std::make_pair(symbolfile_addr, module_sp));
        if (auto image_object_file =
                llvm::dyn_cast<ObjectFileMachO>(module_sp->GetObjectFile())) {
          const SectionList *section_list = image_object_file->GetSectionList();
          if (section_list) {
            uint64_t vmaddrheuristic = 0;
            uint64_t lower = (uint64_t)-1;
            uint64_t upper = 0;
            updateSectionLoadAddress(*section_list, target, symbolfile_addr,
                                     symbolfile_size, vmaddrheuristic, lower,
                                     upper);
          }
        } else {
          bool changed = false;
          module_sp->SetLoadAddress(target, 0, true, changed);
        }

        module_list.AppendIfNeeded(module_sp);

        ModuleList module_list;
        module_list.Append(module_sp);
        target.ModulesDidLoad(module_list);
      } else {
        LLDB_LOGF(log,
                  "JITLoaderGDB::%s failed to load module for "
                  "JIT entry at 0x%" PRIx64,
                  __FUNCTION__, symbolfile_addr);
      }
    } else if (jit_action == JIT_UNREGISTER_FN) {
      LLDB_LOGF(log, "JITLoaderGDB::%s unregistering JIT entry at 0x%" PRIx64,
                __FUNCTION__, symbolfile_addr);

      JITObjectMap::iterator it = m_jit_objects.find(symbolfile_addr);
      if (it != m_jit_objects.end()) {
        module_sp = it->second;
        ObjectFile *image_object_file = module_sp->GetObjectFile();
        if (image_object_file) {
          const SectionList *section_list = image_object_file->GetSectionList();
          if (section_list) {
            const uint32_t num_sections = section_list->GetSize();
            for (uint32_t i = 0; i < num_sections; ++i) {
              SectionSP section_sp(section_list->GetSectionAtIndex(i));
              if (section_sp) {
                target.GetSectionLoadList().SetSectionUnloaded(section_sp);
              }
            }
          }
        }
        module_list.Remove(module_sp);
        m_jit_objects.erase(it);
      }
    } else if (jit_action == JIT_NOACTION) {
      // Nothing to do
    } else {
      assert(false && "Unknown jit action");
    }

    if (all_entries)
      jit_relevant_entry = (addr_t)jit_entry.next_entry;
    else
      jit_relevant_entry = 0;
  }

  return false; // Continue Running.
}

// PluginInterface protocol
JITLoaderSP JITLoaderGDB::CreateInstance(Process *process, bool force) {
  JITLoaderSP jit_loader_sp;
  bool enable;
  switch (GetGlobalPluginProperties().GetEnable()) {
    case EnableJITLoaderGDB::eEnableJITLoaderGDBOn:
      enable = true;
      break;
    case EnableJITLoaderGDB::eEnableJITLoaderGDBOff:
      enable = false;
      break;
    case EnableJITLoaderGDB::eEnableJITLoaderGDBDefault:
      ArchSpec arch(process->GetTarget().GetArchitecture());
      enable = arch.GetTriple().getVendor() != llvm::Triple::Apple;
      break;
  }
  if (enable)
    jit_loader_sp = std::make_shared<JITLoaderGDB>(process);
  return jit_loader_sp;
}

llvm::StringRef JITLoaderGDB::GetPluginDescriptionStatic() {
  return "JIT loader plug-in that watches for JIT events using the GDB "
         "interface.";
}

void JITLoaderGDB::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                DebuggerInitialize);
}

void JITLoaderGDB::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

bool JITLoaderGDB::DidSetJITBreakpoint() const {
  return LLDB_BREAK_ID_IS_VALID(m_jit_break_id);
}

addr_t JITLoaderGDB::GetSymbolAddress(ModuleList &module_list,
                                      ConstString name,
                                      SymbolType symbol_type) const {
  SymbolContextList target_symbols;
  Target &target = m_process->GetTarget();

  module_list.FindSymbolsWithNameAndType(name, symbol_type, target_symbols);
  if (target_symbols.IsEmpty())
    return LLDB_INVALID_ADDRESS;

  SymbolContext sym_ctx;
  target_symbols.GetContextAtIndex(0, sym_ctx);

  const Address jit_descriptor_addr = sym_ctx.symbol->GetAddress();
  if (!jit_descriptor_addr.IsValid())
    return LLDB_INVALID_ADDRESS;

  const addr_t jit_addr = jit_descriptor_addr.GetLoadAddress(&target);
  return jit_addr;
}
