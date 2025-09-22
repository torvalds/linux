//===-- SymbolFileDWARFDebugMap.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolFileDWARFDebugMap.h"
#include "DWARFCompileUnit.h"
#include "DWARFDebugAranges.h"
#include "DWARFDebugInfo.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/Timer.h"

//#define DEBUG_OSO_DMAP // DO NOT CHECKIN WITH THIS NOT COMMENTED OUT

#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/VariableList.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ScopedPrinter.h"

#include "lldb/Target/StackFrame.h"

#include "LogChannelDWARF.h"
#include "SymbolFileDWARF.h"
#include "lldb/lldb-private-enumerations.h"

#include <memory>
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

char SymbolFileDWARFDebugMap::ID;

// Subclass lldb_private::Module so we can intercept the
// "Module::GetObjectFile()" (so we can fixup the object file sections) and
// also for "Module::GetSymbolFile()" (so we can fixup the symbol file id.

const SymbolFileDWARFDebugMap::FileRangeMap &
SymbolFileDWARFDebugMap::CompileUnitInfo::GetFileRangeMap(
    SymbolFileDWARFDebugMap *exe_symfile) {
  if (file_range_map_valid)
    return file_range_map;

  file_range_map_valid = true;

  Module *oso_module = exe_symfile->GetModuleByCompUnitInfo(this);
  if (!oso_module)
    return file_range_map;

  ObjectFile *oso_objfile = oso_module->GetObjectFile();
  if (!oso_objfile)
    return file_range_map;

  Log *log = GetLog(DWARFLog::DebugMap);
  LLDB_LOGF(
      log,
      "%p: SymbolFileDWARFDebugMap::CompileUnitInfo::GetFileRangeMap ('%s')",
      static_cast<void *>(this),
      oso_module->GetSpecificationDescription().c_str());

  std::vector<SymbolFileDWARFDebugMap::CompileUnitInfo *> cu_infos;
  if (exe_symfile->GetCompUnitInfosForModule(oso_module, cu_infos)) {
    for (auto comp_unit_info : cu_infos) {
      Symtab *exe_symtab = exe_symfile->GetObjectFile()->GetSymtab();
      ModuleSP oso_module_sp(oso_objfile->GetModule());
      Symtab *oso_symtab = oso_objfile->GetSymtab();

      /// const uint32_t fun_resolve_flags = SymbolContext::Module |
      /// eSymbolContextCompUnit | eSymbolContextFunction;
      // SectionList *oso_sections = oso_objfile->Sections();
      // Now we need to make sections that map from zero based object file
      // addresses to where things ended up in the main executable.

      assert(comp_unit_info->first_symbol_index != UINT32_MAX);
      // End index is one past the last valid symbol index
      const uint32_t oso_end_idx = comp_unit_info->last_symbol_index + 1;
      for (uint32_t idx = comp_unit_info->first_symbol_index +
                          2; // Skip the N_SO and N_OSO
           idx < oso_end_idx; ++idx) {
        Symbol *exe_symbol = exe_symtab->SymbolAtIndex(idx);
        if (exe_symbol) {
          if (!exe_symbol->IsDebug())
            continue;

          switch (exe_symbol->GetType()) {
          default:
            break;

          case eSymbolTypeCode: {
            // For each N_FUN, or function that we run into in the debug map we
            // make a new section that we add to the sections found in the .o
            // file. This new section has the file address set to what the
            // addresses are in the .o file, and the load address is adjusted
            // to match where it ended up in the final executable! We do this
            // before we parse any dwarf info so that when it goes get parsed
            // all section/offset addresses that get registered will resolve
            // correctly to the new addresses in the main executable.

            // First we find the original symbol in the .o file's symbol table
            Symbol *oso_fun_symbol = oso_symtab->FindFirstSymbolWithNameAndType(
                exe_symbol->GetMangled().GetName(Mangled::ePreferMangled),
                eSymbolTypeCode, Symtab::eDebugNo, Symtab::eVisibilityAny);
            if (oso_fun_symbol) {
              // Add the inverse OSO file address to debug map entry mapping
              exe_symfile->AddOSOFileRange(
                  this, exe_symbol->GetAddressRef().GetFileAddress(),
                  exe_symbol->GetByteSize(),
                  oso_fun_symbol->GetAddressRef().GetFileAddress(),
                  oso_fun_symbol->GetByteSize());
            }
          } break;

          case eSymbolTypeData: {
            // For each N_GSYM we remap the address for the global by making a
            // new section that we add to the sections found in the .o file.
            // This new section has the file address set to what the addresses
            // are in the .o file, and the load address is adjusted to match
            // where it ended up in the final executable! We do this before we
            // parse any dwarf info so that when it goes get parsed all
            // section/offset addresses that get registered will resolve
            // correctly to the new addresses in the main executable. We
            // initially set the section size to be 1 byte, but will need to
            // fix up these addresses further after all globals have been
            // parsed to span the gaps, or we can find the global variable
            // sizes from the DWARF info as we are parsing.

            // Next we find the non-stab entry that corresponds to the N_GSYM
            // in the .o file
            Symbol *oso_gsym_symbol =
                oso_symtab->FindFirstSymbolWithNameAndType(
                    exe_symbol->GetMangled().GetName(Mangled::ePreferMangled),
                    eSymbolTypeData, Symtab::eDebugNo, Symtab::eVisibilityAny);
            if (exe_symbol && oso_gsym_symbol && exe_symbol->ValueIsAddress() &&
                oso_gsym_symbol->ValueIsAddress()) {
              // Add the inverse OSO file address to debug map entry mapping
              exe_symfile->AddOSOFileRange(
                  this, exe_symbol->GetAddressRef().GetFileAddress(),
                  exe_symbol->GetByteSize(),
                  oso_gsym_symbol->GetAddressRef().GetFileAddress(),
                  oso_gsym_symbol->GetByteSize());
            }
          } break;
          }
        }
      }

      exe_symfile->FinalizeOSOFileRanges(this);
      // We don't need the symbols anymore for the .o files
      oso_objfile->ClearSymtab();
    }
  }
  return file_range_map;
}

namespace lldb_private::plugin {
namespace dwarf {
class DebugMapModule : public Module {
public:
  DebugMapModule(const ModuleSP &exe_module_sp, uint32_t cu_idx,
                 const FileSpec &file_spec, const ArchSpec &arch,
                 ConstString object_name, off_t object_offset,
                 const llvm::sys::TimePoint<> object_mod_time)
      : Module(file_spec, arch, object_name, object_offset, object_mod_time),
        m_exe_module_wp(exe_module_sp), m_cu_idx(cu_idx) {}

  ~DebugMapModule() override = default;

  SymbolFile *
  GetSymbolFile(bool can_create = true,
                lldb_private::Stream *feedback_strm = nullptr) override {
    // Scope for locker
    if (m_symfile_up.get() || !can_create)
      return m_symfile_up ? m_symfile_up->GetSymbolFile() : nullptr;

    ModuleSP exe_module_sp(m_exe_module_wp.lock());
    if (exe_module_sp) {
      // Now get the object file outside of a locking scope
      ObjectFile *oso_objfile = GetObjectFile();
      if (oso_objfile) {
        std::lock_guard<std::recursive_mutex> guard(m_mutex);
        if (SymbolFile *symfile =
                Module::GetSymbolFile(can_create, feedback_strm)) {
          // Set a pointer to this class to set our OSO DWARF file know that
          // the DWARF is being used along with a debug map and that it will
          // have the remapped sections that we do below.
          SymbolFileDWARF *oso_symfile =
              SymbolFileDWARFDebugMap::GetSymbolFileAsSymbolFileDWARF(symfile);

          if (!oso_symfile)
            return nullptr;

          ObjectFile *exe_objfile = exe_module_sp->GetObjectFile();
          SymbolFile *exe_symfile = exe_module_sp->GetSymbolFile();

          if (exe_objfile && exe_symfile) {
            oso_symfile->SetDebugMapModule(exe_module_sp);
            // Set the ID of the symbol file DWARF to the index of the OSO
            // shifted left by 32 bits to provide a unique prefix for any
            // UserID's that get created in the symbol file.
            oso_symfile->SetFileIndex((uint64_t)m_cu_idx);
          }
          return symfile;
        }
      }
    }
    return nullptr;
  }

protected:
  ModuleWP m_exe_module_wp;
  const uint32_t m_cu_idx;
};
} // namespace dwarf
} // namespace lldb_private::plugin

void SymbolFileDWARFDebugMap::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void SymbolFileDWARFDebugMap::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef SymbolFileDWARFDebugMap::GetPluginDescriptionStatic() {
  return "DWARF and DWARF3 debug symbol file reader (debug map).";
}

SymbolFile *SymbolFileDWARFDebugMap::CreateInstance(ObjectFileSP objfile_sp) {
  return new SymbolFileDWARFDebugMap(std::move(objfile_sp));
}

SymbolFileDWARFDebugMap::SymbolFileDWARFDebugMap(ObjectFileSP objfile_sp)
    : SymbolFileCommon(std::move(objfile_sp)), m_flags(), m_compile_unit_infos(),
      m_func_indexes(), m_glob_indexes(),
      m_supports_DW_AT_APPLE_objc_complete_type(eLazyBoolCalculate) {}

SymbolFileDWARFDebugMap::~SymbolFileDWARFDebugMap() = default;

void SymbolFileDWARFDebugMap::InitializeObject() {}

void SymbolFileDWARFDebugMap::InitOSO() {
  if (m_flags.test(kHaveInitializedOSOs))
    return;

  m_flags.set(kHaveInitializedOSOs);

  // If the object file has been stripped, there is no sense in looking further
  // as all of the debug symbols for the debug map will not be available
  if (m_objfile_sp->IsStripped())
    return;

  // Also make sure the file type is some sort of executable. Core files, debug
  // info files (dSYM), object files (.o files), and stub libraries all can
  switch (m_objfile_sp->GetType()) {
  case ObjectFile::eTypeInvalid:
  case ObjectFile::eTypeCoreFile:
  case ObjectFile::eTypeDebugInfo:
  case ObjectFile::eTypeObjectFile:
  case ObjectFile::eTypeStubLibrary:
  case ObjectFile::eTypeUnknown:
  case ObjectFile::eTypeJIT:
    return;

  case ObjectFile::eTypeExecutable:
  case ObjectFile::eTypeDynamicLinker:
  case ObjectFile::eTypeSharedLibrary:
    break;
  }

  // In order to get the abilities of this plug-in, we look at the list of
  // N_OSO entries (object files) from the symbol table and make sure that
  // these files exist and also contain valid DWARF. If we get any of that then
  // we return the abilities of the first N_OSO's DWARF.

  Symtab *symtab = m_objfile_sp->GetSymtab();
  if (!symtab)
    return;

  Log *log = GetLog(DWARFLog::DebugMap);

  std::vector<uint32_t> oso_indexes;
  // When a mach-o symbol is encoded, the n_type field is encoded in bits
  // 23:16, and the n_desc field is encoded in bits 15:0.
  //
  // To find all N_OSO entries that are part of the DWARF + debug map we find
  // only object file symbols with the flags value as follows: bits 23:16 ==
  // 0x66 (N_OSO) bits 15: 0 == 0x0001 (specifies this is a debug map object
  // file)
  const uint32_t k_oso_symbol_flags_value = 0x660001u;

  const uint32_t oso_index_count =
      symtab->AppendSymbolIndexesWithTypeAndFlagsValue(
          eSymbolTypeObjectFile, k_oso_symbol_flags_value, oso_indexes);

  if (oso_index_count == 0)
    return;

  symtab->AppendSymbolIndexesWithType(eSymbolTypeCode, Symtab::eDebugYes,
                                      Symtab::eVisibilityAny, m_func_indexes);
  symtab->AppendSymbolIndexesWithType(eSymbolTypeData, Symtab::eDebugYes,
                                      Symtab::eVisibilityAny, m_glob_indexes);

  symtab->SortSymbolIndexesByValue(m_func_indexes, true);
  symtab->SortSymbolIndexesByValue(m_glob_indexes, true);

  for (uint32_t sym_idx :
       llvm::concat<uint32_t>(m_func_indexes, m_glob_indexes)) {
    const Symbol *symbol = symtab->SymbolAtIndex(sym_idx);
    lldb::addr_t file_addr = symbol->GetAddressRef().GetFileAddress();
    lldb::addr_t byte_size = symbol->GetByteSize();
    DebugMap::Entry debug_map_entry(file_addr, byte_size,
                                    OSOEntry(sym_idx, LLDB_INVALID_ADDRESS));
    m_debug_map.Append(debug_map_entry);
  }
  m_debug_map.Sort();

  m_compile_unit_infos.resize(oso_index_count);

  for (uint32_t i = 0; i < oso_index_count; ++i) {
    const uint32_t so_idx = oso_indexes[i] - 1;
    const uint32_t oso_idx = oso_indexes[i];
    const Symbol *so_symbol = symtab->SymbolAtIndex(so_idx);
    const Symbol *oso_symbol = symtab->SymbolAtIndex(oso_idx);
    if (so_symbol && oso_symbol &&
        so_symbol->GetType() == eSymbolTypeSourceFile &&
        oso_symbol->GetType() == eSymbolTypeObjectFile) {
      m_compile_unit_infos[i].so_file.SetFile(so_symbol->GetName().AsCString(),
                                              FileSpec::Style::native);
      m_compile_unit_infos[i].oso_path = oso_symbol->GetName();
      m_compile_unit_infos[i].oso_mod_time =
          llvm::sys::toTimePoint(oso_symbol->GetIntegerValue(0));
      uint32_t sibling_idx = so_symbol->GetSiblingIndex();
      // The sibling index can't be less that or equal to the current index
      // "i"
      if (sibling_idx <= i || sibling_idx == UINT32_MAX) {
        m_objfile_sp->GetModule()->ReportError(
            "N_SO in symbol with UID {0} has invalid sibling in debug "
            "map, "
            "please file a bug and attach the binary listed in this error",
            so_symbol->GetID());
      } else {
        const Symbol *last_symbol = symtab->SymbolAtIndex(sibling_idx - 1);
        m_compile_unit_infos[i].first_symbol_index = so_idx;
        m_compile_unit_infos[i].last_symbol_index = sibling_idx - 1;
        m_compile_unit_infos[i].first_symbol_id = so_symbol->GetID();
        m_compile_unit_infos[i].last_symbol_id = last_symbol->GetID();

        LLDB_LOGF(log, "Initialized OSO 0x%8.8x: file=%s", i,
                  oso_symbol->GetName().GetCString());
      }
    } else {
      if (oso_symbol == nullptr)
        m_objfile_sp->GetModule()->ReportError(
            "N_OSO symbol[{0}] can't be found, please file a bug and "
            "attach "
            "the binary listed in this error",
            oso_idx);
      else if (so_symbol == nullptr)
        m_objfile_sp->GetModule()->ReportError(
            "N_SO not found for N_OSO symbol[{0}], please file a bug and "
            "attach the binary listed in this error",
            oso_idx);
      else if (so_symbol->GetType() != eSymbolTypeSourceFile)
        m_objfile_sp->GetModule()->ReportError(
            "N_SO has incorrect symbol type ({0}) for N_OSO "
            "symbol[{1}], "
            "please file a bug and attach the binary listed in this error",
            so_symbol->GetType(), oso_idx);
      else if (oso_symbol->GetType() != eSymbolTypeSourceFile)
        m_objfile_sp->GetModule()->ReportError(
            "N_OSO has incorrect symbol type ({0}) for N_OSO "
            "symbol[{1}], "
            "please file a bug and attach the binary listed in this error",
            oso_symbol->GetType(), oso_idx);
    }
  }
}

Module *SymbolFileDWARFDebugMap::GetModuleByOSOIndex(uint32_t oso_idx) {
  const uint32_t cu_count = GetNumCompileUnits();
  if (oso_idx < cu_count)
    return GetModuleByCompUnitInfo(&m_compile_unit_infos[oso_idx]);
  return nullptr;
}

Module *SymbolFileDWARFDebugMap::GetModuleByCompUnitInfo(
    CompileUnitInfo *comp_unit_info) {
  if (!comp_unit_info->oso_sp) {
    auto pos = m_oso_map.find(
        {comp_unit_info->oso_path, comp_unit_info->oso_mod_time});
    if (pos != m_oso_map.end()) {
      comp_unit_info->oso_sp = pos->second;
    } else {
      ObjectFile *obj_file = GetObjectFile();
      comp_unit_info->oso_sp = std::make_shared<OSOInfo>();
      m_oso_map[{comp_unit_info->oso_path, comp_unit_info->oso_mod_time}] =
          comp_unit_info->oso_sp;
      const char *oso_path = comp_unit_info->oso_path.GetCString();
      FileSpec oso_file(oso_path);
      ConstString oso_object;
      if (FileSystem::Instance().Exists(oso_file)) {
        // The modification time returned by the FS can have a higher precision
        // than the one from the CU.
        auto oso_mod_time = std::chrono::time_point_cast<std::chrono::seconds>(
            FileSystem::Instance().GetModificationTime(oso_file));
        // A timestamp of 0 means that the linker was in deterministic mode. In
        // that case, we should skip the check against the filesystem last
        // modification timestamp, since it will never match.
        if (comp_unit_info->oso_mod_time != llvm::sys::TimePoint<>() &&
            oso_mod_time != comp_unit_info->oso_mod_time) {
          comp_unit_info->oso_load_error.SetErrorStringWithFormat(
              "debug map object file \"%s\" changed (actual: 0x%8.8x, debug "
              "map: 0x%8.8x) since this executable was linked, debug info "
              "will not be loaded", oso_file.GetPath().c_str(),
              (uint32_t)llvm::sys::toTimeT(oso_mod_time),
              (uint32_t)llvm::sys::toTimeT(comp_unit_info->oso_mod_time));
          obj_file->GetModule()->ReportError(
              "{0}", comp_unit_info->oso_load_error.AsCString());
          return nullptr;
        }

      } else {
        const bool must_exist = true;

        if (!ObjectFile::SplitArchivePathWithObject(oso_path, oso_file,
                                                    oso_object, must_exist)) {
          comp_unit_info->oso_load_error.SetErrorStringWithFormat(
              "debug map object file \"%s\" containing debug info does not "
              "exist, debug info will not be loaded",
              comp_unit_info->oso_path.GetCString());
          return nullptr;
        }
      }
      // Always create a new module for .o files. Why? Because we use the debug
      // map, to add new sections to each .o file and even though a .o file
      // might not have changed, the sections that get added to the .o file can
      // change.
      ArchSpec oso_arch;
      // Only adopt the architecture from the module (not the vendor or OS)
      // since .o files for "i386-apple-ios" will historically show up as "i386
      // -apple-macosx" due to the lack of a LC_VERSION_MIN_MACOSX or
      // LC_VERSION_MIN_IPHONEOS load command...
      oso_arch.SetTriple(m_objfile_sp->GetModule()
                             ->GetArchitecture()
                             .GetTriple()
                             .getArchName()
                             .str()
                             .c_str());
      comp_unit_info->oso_sp->module_sp = std::make_shared<DebugMapModule>(
          obj_file->GetModule(), GetCompUnitInfoIndex(comp_unit_info), oso_file,
          oso_arch, oso_object, 0,
          oso_object ? comp_unit_info->oso_mod_time : llvm::sys::TimePoint<>());

      if (oso_object && !comp_unit_info->oso_sp->module_sp->GetObjectFile() &&
          FileSystem::Instance().Exists(oso_file)) {
        // If we are loading a .o file from a .a file the "oso_object" will
        // have a valid value name and if the .a file exists, either the .o
        // file didn't exist in the .a file or the mod time didn't match.
        comp_unit_info->oso_load_error.SetErrorStringWithFormat(
            "\"%s\" object from the \"%s\" archive: "
            "either the .o file doesn't exist in the archive or the "
            "modification time (0x%8.8x) of the .o file doesn't match",
            oso_object.AsCString(), oso_file.GetPath().c_str(),
            (uint32_t)llvm::sys::toTimeT(comp_unit_info->oso_mod_time));
      }
    }
  }
  if (comp_unit_info->oso_sp)
    return comp_unit_info->oso_sp->module_sp.get();
  return nullptr;
}

bool SymbolFileDWARFDebugMap::GetFileSpecForSO(uint32_t oso_idx,
                                               FileSpec &file_spec) {
  if (oso_idx < m_compile_unit_infos.size()) {
    if (m_compile_unit_infos[oso_idx].so_file) {
      file_spec = m_compile_unit_infos[oso_idx].so_file;
      return true;
    }
  }
  return false;
}

ObjectFile *SymbolFileDWARFDebugMap::GetObjectFileByOSOIndex(uint32_t oso_idx) {
  Module *oso_module = GetModuleByOSOIndex(oso_idx);
  if (oso_module)
    return oso_module->GetObjectFile();
  return nullptr;
}

SymbolFileDWARF *
SymbolFileDWARFDebugMap::GetSymbolFile(const SymbolContext &sc) {
  return GetSymbolFile(*sc.comp_unit);
}

SymbolFileDWARF *
SymbolFileDWARFDebugMap::GetSymbolFile(const CompileUnit &comp_unit) {
  CompileUnitInfo *comp_unit_info = GetCompUnitInfo(comp_unit);
  if (comp_unit_info)
    return GetSymbolFileByCompUnitInfo(comp_unit_info);
  return nullptr;
}

ObjectFile *SymbolFileDWARFDebugMap::GetObjectFileByCompUnitInfo(
    CompileUnitInfo *comp_unit_info) {
  Module *oso_module = GetModuleByCompUnitInfo(comp_unit_info);
  if (oso_module)
    return oso_module->GetObjectFile();
  return nullptr;
}

uint32_t SymbolFileDWARFDebugMap::GetCompUnitInfoIndex(
    const CompileUnitInfo *comp_unit_info) {
  if (!m_compile_unit_infos.empty()) {
    const CompileUnitInfo *first_comp_unit_info = &m_compile_unit_infos.front();
    const CompileUnitInfo *last_comp_unit_info = &m_compile_unit_infos.back();
    if (first_comp_unit_info <= comp_unit_info &&
        comp_unit_info <= last_comp_unit_info)
      return comp_unit_info - first_comp_unit_info;
  }
  return UINT32_MAX;
}

SymbolFileDWARF *
SymbolFileDWARFDebugMap::GetSymbolFileByOSOIndex(uint32_t oso_idx) {
  unsigned size = m_compile_unit_infos.size();
  if (oso_idx < size)
    return GetSymbolFileByCompUnitInfo(&m_compile_unit_infos[oso_idx]);
  return nullptr;
}

SymbolFileDWARF *
SymbolFileDWARFDebugMap::GetSymbolFileAsSymbolFileDWARF(SymbolFile *sym_file) {
  if (sym_file &&
      sym_file->GetPluginName() == SymbolFileDWARF::GetPluginNameStatic())
    return static_cast<SymbolFileDWARF *>(sym_file);
  return nullptr;
}

SymbolFileDWARF *SymbolFileDWARFDebugMap::GetSymbolFileByCompUnitInfo(
    CompileUnitInfo *comp_unit_info) {
  if (Module *oso_module = GetModuleByCompUnitInfo(comp_unit_info))
    return GetSymbolFileAsSymbolFileDWARF(oso_module->GetSymbolFile());
  return nullptr;
}

uint32_t SymbolFileDWARFDebugMap::CalculateAbilities() {
  // In order to get the abilities of this plug-in, we look at the list of
  // N_OSO entries (object files) from the symbol table and make sure that
  // these files exist and also contain valid DWARF. If we get any of that then
  // we return the abilities of the first N_OSO's DWARF.

  const uint32_t oso_index_count = GetNumCompileUnits();
  if (oso_index_count > 0) {
    InitOSO();
    if (!m_compile_unit_infos.empty()) {
      return SymbolFile::CompileUnits | SymbolFile::Functions |
             SymbolFile::Blocks | SymbolFile::GlobalVariables |
             SymbolFile::LocalVariables | SymbolFile::VariableTypes |
             SymbolFile::LineTables;
    }
  }
  return 0;
}

uint32_t SymbolFileDWARFDebugMap::CalculateNumCompileUnits() {
  InitOSO();
  return m_compile_unit_infos.size();
}

CompUnitSP SymbolFileDWARFDebugMap::ParseCompileUnitAtIndex(uint32_t cu_idx) {
  CompUnitSP comp_unit_sp;
  const uint32_t cu_count = GetNumCompileUnits();

  if (cu_idx < cu_count) {
    auto &cu_info = m_compile_unit_infos[cu_idx];
    Module *oso_module = GetModuleByCompUnitInfo(&cu_info);
    if (oso_module) {
      FileSpec so_file_spec;
      if (GetFileSpecForSO(cu_idx, so_file_spec)) {
        // User zero as the ID to match the compile unit at offset zero in each
        // .o file.
        lldb::user_id_t cu_id = 0;
        cu_info.compile_units_sps.push_back(std::make_shared<CompileUnit>(
            m_objfile_sp->GetModule(), nullptr,
            std::make_shared<SupportFile>(so_file_spec), cu_id,
            eLanguageTypeUnknown, eLazyBoolCalculate));
        cu_info.id_to_index_map.insert({0, 0});
        SetCompileUnitAtIndex(cu_idx, cu_info.compile_units_sps[0]);
        // If there's a symbol file also register all the extra compile units.
        if (SymbolFileDWARF *oso_symfile =
                GetSymbolFileByCompUnitInfo(&cu_info)) {
          auto num_dwarf_units = oso_symfile->DebugInfo().GetNumUnits();
          for (size_t i = 0; i < num_dwarf_units; ++i) {
            auto *dwarf_unit = oso_symfile->DebugInfo().GetUnitAtIndex(i);
            if (auto *dwarf_cu = llvm::dyn_cast<DWARFCompileUnit>(dwarf_unit)) {
              // The "main" one was already registered.
              if (dwarf_cu->GetID() == 0)
                continue;
              cu_info.compile_units_sps.push_back(std::make_shared<CompileUnit>(
                  m_objfile_sp->GetModule(), nullptr,
                  std::make_shared<SupportFile>(so_file_spec),
                  dwarf_cu->GetID(), eLanguageTypeUnknown, eLazyBoolCalculate));
              cu_info.id_to_index_map.insert(
                  {dwarf_cu->GetID(), cu_info.compile_units_sps.size() - 1});
            }
          }
        }
      }
    }
    if (!cu_info.compile_units_sps.empty())
      comp_unit_sp = cu_info.compile_units_sps[0];
  }

  return comp_unit_sp;
}

SymbolFileDWARFDebugMap::CompileUnitInfo *
SymbolFileDWARFDebugMap::GetCompUnitInfo(const SymbolContext &sc) {
  return GetCompUnitInfo(*sc.comp_unit);
}

SymbolFileDWARFDebugMap::CompileUnitInfo *
SymbolFileDWARFDebugMap::GetCompUnitInfo(const CompileUnit &comp_unit) {
  const uint32_t cu_count = GetNumCompileUnits();
  for (uint32_t i = 0; i < cu_count; ++i) {
    auto &id_to_index_map = m_compile_unit_infos[i].id_to_index_map;

    auto it = id_to_index_map.find(comp_unit.GetID());
    if (it != id_to_index_map.end() &&
        &comp_unit ==
            m_compile_unit_infos[i].compile_units_sps[it->getSecond()].get())
      return &m_compile_unit_infos[i];
  }
  return nullptr;
}

size_t SymbolFileDWARFDebugMap::GetCompUnitInfosForModule(
    const lldb_private::Module *module,
    std::vector<CompileUnitInfo *> &cu_infos) {
  const uint32_t cu_count = GetNumCompileUnits();
  for (uint32_t i = 0; i < cu_count; ++i) {
    if (module == GetModuleByCompUnitInfo(&m_compile_unit_infos[i]))
      cu_infos.push_back(&m_compile_unit_infos[i]);
  }
  return cu_infos.size();
}

lldb::LanguageType
SymbolFileDWARFDebugMap::ParseLanguage(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(comp_unit);
  if (oso_dwarf)
    return oso_dwarf->ParseLanguage(comp_unit);
  return eLanguageTypeUnknown;
}

XcodeSDK SymbolFileDWARFDebugMap::ParseXcodeSDK(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(comp_unit);
  if (oso_dwarf)
    return oso_dwarf->ParseXcodeSDK(comp_unit);
  return {};
}

llvm::SmallSet<lldb::LanguageType, 4>
SymbolFileDWARFDebugMap::ParseAllLanguages(
    lldb_private::CompileUnit &comp_unit) {
  llvm::SmallSet<lldb::LanguageType, 4> langs;
  auto *info = GetCompUnitInfo(comp_unit);
  for (auto &comp_unit : info->compile_units_sps) {
    langs.insert(comp_unit->GetLanguage());
  }
  return langs;
}

size_t SymbolFileDWARFDebugMap::ParseFunctions(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(comp_unit);
  if (oso_dwarf)
    return oso_dwarf->ParseFunctions(comp_unit);
  return 0;
}

bool SymbolFileDWARFDebugMap::ParseLineTable(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(comp_unit);
  if (oso_dwarf)
    return oso_dwarf->ParseLineTable(comp_unit);
  return false;
}

bool SymbolFileDWARFDebugMap::ParseDebugMacros(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(comp_unit);
  if (oso_dwarf)
    return oso_dwarf->ParseDebugMacros(comp_unit);
  return false;
}

bool SymbolFileDWARFDebugMap::ForEachExternalModule(
    CompileUnit &comp_unit,
    llvm::DenseSet<lldb_private::SymbolFile *> &visited_symbol_files,
    llvm::function_ref<bool(Module &)> f) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(comp_unit);
  if (oso_dwarf)
    return oso_dwarf->ForEachExternalModule(comp_unit, visited_symbol_files, f);
  return false;
}

bool SymbolFileDWARFDebugMap::ParseSupportFiles(
    CompileUnit &comp_unit, SupportFileList &support_files) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(comp_unit);
  if (oso_dwarf)
    return oso_dwarf->ParseSupportFiles(comp_unit, support_files);
  return false;
}

bool SymbolFileDWARFDebugMap::ParseIsOptimized(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(comp_unit);
  if (oso_dwarf)
    return oso_dwarf->ParseIsOptimized(comp_unit);
  return false;
}

bool SymbolFileDWARFDebugMap::ParseImportedModules(
    const SymbolContext &sc, std::vector<SourceModule> &imported_modules) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(sc);
  if (oso_dwarf)
    return oso_dwarf->ParseImportedModules(sc, imported_modules);
  return false;
}

size_t SymbolFileDWARFDebugMap::ParseBlocksRecursive(Function &func) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  CompileUnit *comp_unit = func.GetCompileUnit();
  if (!comp_unit)
    return 0;

  SymbolFileDWARF *oso_dwarf = GetSymbolFile(*comp_unit);
  if (oso_dwarf)
    return oso_dwarf->ParseBlocksRecursive(func);
  return 0;
}

size_t SymbolFileDWARFDebugMap::ParseTypes(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(comp_unit);
  if (oso_dwarf)
    return oso_dwarf->ParseTypes(comp_unit);
  return 0;
}

size_t
SymbolFileDWARFDebugMap::ParseVariablesForContext(const SymbolContext &sc) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARF *oso_dwarf = GetSymbolFile(sc);
  if (oso_dwarf)
    return oso_dwarf->ParseVariablesForContext(sc);
  return 0;
}

Type *SymbolFileDWARFDebugMap::ResolveTypeUID(lldb::user_id_t type_uid) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  const uint64_t oso_idx = GetOSOIndexFromUserID(type_uid);
  SymbolFileDWARF *oso_dwarf = GetSymbolFileByOSOIndex(oso_idx);
  if (oso_dwarf)
    return oso_dwarf->ResolveTypeUID(type_uid);
  return nullptr;
}

std::optional<SymbolFile::ArrayInfo>
SymbolFileDWARFDebugMap::GetDynamicArrayInfoForUID(
    lldb::user_id_t type_uid, const lldb_private::ExecutionContext *exe_ctx) {
  const uint64_t oso_idx = GetOSOIndexFromUserID(type_uid);
  SymbolFileDWARF *oso_dwarf = GetSymbolFileByOSOIndex(oso_idx);
  if (oso_dwarf)
    return oso_dwarf->GetDynamicArrayInfoForUID(type_uid, exe_ctx);
  return std::nullopt;
}

bool SymbolFileDWARFDebugMap::CompleteType(CompilerType &compiler_type) {
  bool success = false;
  if (compiler_type) {
    ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
      if (oso_dwarf->HasForwardDeclForCompilerType(compiler_type)) {
        oso_dwarf->CompleteType(compiler_type);
        success = true;
        return IterationAction::Stop;
      }
      return IterationAction::Continue;
    });
  }
  return success;
}

uint32_t
SymbolFileDWARFDebugMap::ResolveSymbolContext(const Address &exe_so_addr,
                                              SymbolContextItem resolve_scope,
                                              SymbolContext &sc) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  uint32_t resolved_flags = 0;
  Symtab *symtab = m_objfile_sp->GetSymtab();
  if (symtab) {
    const addr_t exe_file_addr = exe_so_addr.GetFileAddress();

    const DebugMap::Entry *debug_map_entry =
        m_debug_map.FindEntryThatContains(exe_file_addr);
    if (debug_map_entry) {

      sc.symbol =
          symtab->SymbolAtIndex(debug_map_entry->data.GetExeSymbolIndex());

      if (sc.symbol != nullptr) {
        resolved_flags |= eSymbolContextSymbol;

        uint32_t oso_idx = 0;
        CompileUnitInfo *comp_unit_info =
            GetCompileUnitInfoForSymbolWithID(sc.symbol->GetID(), &oso_idx);
        if (comp_unit_info) {
          comp_unit_info->GetFileRangeMap(this);
          Module *oso_module = GetModuleByCompUnitInfo(comp_unit_info);
          if (oso_module) {
            lldb::addr_t oso_file_addr =
                exe_file_addr - debug_map_entry->GetRangeBase() +
                debug_map_entry->data.GetOSOFileAddress();
            Address oso_so_addr;
            if (oso_module->ResolveFileAddress(oso_file_addr, oso_so_addr)) {
              if (SymbolFile *sym_file = oso_module->GetSymbolFile()) {
                resolved_flags |= sym_file->ResolveSymbolContext(
                    oso_so_addr, resolve_scope, sc);
              } else {
                ObjectFile *obj_file = GetObjectFile();
                LLDB_LOG(GetLog(DWARFLog::DebugMap),
                         "Failed to get symfile for OSO: {0} in module: {1}",
                         oso_module->GetFileSpec(),
                         obj_file ? obj_file->GetFileSpec()
                                  : FileSpec("unknown"));
              }
            }
          }
        }
      }
    }
  }
  return resolved_flags;
}

uint32_t SymbolFileDWARFDebugMap::ResolveSymbolContext(
    const SourceLocationSpec &src_location_spec,
    SymbolContextItem resolve_scope, SymbolContextList &sc_list) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  const uint32_t initial = sc_list.GetSize();
  const uint32_t cu_count = GetNumCompileUnits();

  for (uint32_t i = 0; i < cu_count; ++i) {
    // If we are checking for inlines, then we need to look through all compile
    // units no matter if "file_spec" matches.
    bool resolve = src_location_spec.GetCheckInlines();

    if (!resolve) {
      FileSpec so_file_spec;
      if (GetFileSpecForSO(i, so_file_spec))
        resolve =
            FileSpec::Match(src_location_spec.GetFileSpec(), so_file_spec);
    }
    if (resolve) {
      SymbolFileDWARF *oso_dwarf = GetSymbolFileByOSOIndex(i);
      if (oso_dwarf)
        oso_dwarf->ResolveSymbolContext(src_location_spec, resolve_scope,
                                        sc_list);
    }
  }
  return sc_list.GetSize() - initial;
}

void SymbolFileDWARFDebugMap::PrivateFindGlobalVariables(
    ConstString name, const CompilerDeclContext &parent_decl_ctx,
    const std::vector<uint32_t>
        &indexes, // Indexes into the symbol table that match "name"
    uint32_t max_matches, VariableList &variables) {
  const size_t match_count = indexes.size();
  for (size_t i = 0; i < match_count; ++i) {
    uint32_t oso_idx;
    CompileUnitInfo *comp_unit_info =
        GetCompileUnitInfoForSymbolWithIndex(indexes[i], &oso_idx);
    if (comp_unit_info) {
      SymbolFileDWARF *oso_dwarf = GetSymbolFileByOSOIndex(oso_idx);
      if (oso_dwarf) {
        oso_dwarf->FindGlobalVariables(name, parent_decl_ctx, max_matches,
                                       variables);
        if (variables.GetSize() > max_matches)
          break;
      }
    }
  }
}

void SymbolFileDWARFDebugMap::FindGlobalVariables(
    ConstString name, const CompilerDeclContext &parent_decl_ctx,
    uint32_t max_matches, VariableList &variables) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  uint32_t total_matches = 0;

  ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
    const uint32_t old_size = variables.GetSize();
    oso_dwarf->FindGlobalVariables(name, parent_decl_ctx, max_matches,
                                   variables);
    const uint32_t oso_matches = variables.GetSize() - old_size;
    if (oso_matches > 0) {
      total_matches += oso_matches;

      // Are we getting all matches?
      if (max_matches == UINT32_MAX)
        return IterationAction::Continue; // Yep, continue getting everything

      // If we have found enough matches, lets get out
      if (max_matches >= total_matches)
        return IterationAction::Stop;

      // Update the max matches for any subsequent calls to find globals in any
      // other object files with DWARF
      max_matches -= oso_matches;
    }

    return IterationAction::Continue;
  });
}

void SymbolFileDWARFDebugMap::FindGlobalVariables(
    const RegularExpression &regex, uint32_t max_matches,
    VariableList &variables) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  uint32_t total_matches = 0;
  ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
    const uint32_t old_size = variables.GetSize();
    oso_dwarf->FindGlobalVariables(regex, max_matches, variables);

    const uint32_t oso_matches = variables.GetSize() - old_size;
    if (oso_matches > 0) {
      total_matches += oso_matches;

      // Are we getting all matches?
      if (max_matches == UINT32_MAX)
        return IterationAction::Continue; // Yep, continue getting everything

      // If we have found enough matches, lets get out
      if (max_matches >= total_matches)
        return IterationAction::Stop;

      // Update the max matches for any subsequent calls to find globals in any
      // other object files with DWARF
      max_matches -= oso_matches;
    }

    return IterationAction::Continue;
  });
}

int SymbolFileDWARFDebugMap::SymbolContainsSymbolWithIndex(
    uint32_t *symbol_idx_ptr, const CompileUnitInfo *comp_unit_info) {
  const uint32_t symbol_idx = *symbol_idx_ptr;

  if (symbol_idx < comp_unit_info->first_symbol_index)
    return -1;

  if (symbol_idx <= comp_unit_info->last_symbol_index)
    return 0;

  return 1;
}

int SymbolFileDWARFDebugMap::SymbolContainsSymbolWithID(
    user_id_t *symbol_idx_ptr, const CompileUnitInfo *comp_unit_info) {
  const user_id_t symbol_id = *symbol_idx_ptr;

  if (symbol_id < comp_unit_info->first_symbol_id)
    return -1;

  if (symbol_id <= comp_unit_info->last_symbol_id)
    return 0;

  return 1;
}

SymbolFileDWARFDebugMap::CompileUnitInfo *
SymbolFileDWARFDebugMap::GetCompileUnitInfoForSymbolWithIndex(
    uint32_t symbol_idx, uint32_t *oso_idx_ptr) {
  const uint32_t oso_index_count = m_compile_unit_infos.size();
  CompileUnitInfo *comp_unit_info = nullptr;
  if (oso_index_count) {
    comp_unit_info = (CompileUnitInfo *)bsearch(
        &symbol_idx, &m_compile_unit_infos[0], m_compile_unit_infos.size(),
        sizeof(CompileUnitInfo),
        (ComparisonFunction)SymbolContainsSymbolWithIndex);
  }

  if (oso_idx_ptr) {
    if (comp_unit_info != nullptr)
      *oso_idx_ptr = comp_unit_info - &m_compile_unit_infos[0];
    else
      *oso_idx_ptr = UINT32_MAX;
  }
  return comp_unit_info;
}

SymbolFileDWARFDebugMap::CompileUnitInfo *
SymbolFileDWARFDebugMap::GetCompileUnitInfoForSymbolWithID(
    user_id_t symbol_id, uint32_t *oso_idx_ptr) {
  const uint32_t oso_index_count = m_compile_unit_infos.size();
  CompileUnitInfo *comp_unit_info = nullptr;
  if (oso_index_count) {
    comp_unit_info = (CompileUnitInfo *)::bsearch(
        &symbol_id, &m_compile_unit_infos[0], m_compile_unit_infos.size(),
        sizeof(CompileUnitInfo),
        (ComparisonFunction)SymbolContainsSymbolWithID);
  }

  if (oso_idx_ptr) {
    if (comp_unit_info != nullptr)
      *oso_idx_ptr = comp_unit_info - &m_compile_unit_infos[0];
    else
      *oso_idx_ptr = UINT32_MAX;
  }
  return comp_unit_info;
}

static void RemoveFunctionsWithModuleNotEqualTo(const ModuleSP &module_sp,
                                                SymbolContextList &sc_list,
                                                uint32_t start_idx) {
  // We found functions in .o files. Not all functions in the .o files will
  // have made it into the final output file. The ones that did make it into
  // the final output file will have a section whose module matches the module
  // from the ObjectFile for this SymbolFile. When the modules don't match,
  // then we have something that was in a .o file, but doesn't map to anything
  // in the final executable.
  uint32_t i = start_idx;
  while (i < sc_list.GetSize()) {
    SymbolContext sc;
    sc_list.GetContextAtIndex(i, sc);
    if (sc.function) {
      const SectionSP section_sp(
          sc.function->GetAddressRange().GetBaseAddress().GetSection());
      if (section_sp->GetModule() != module_sp) {
        sc_list.RemoveContextAtIndex(i);
        continue;
      }
    }
    ++i;
  }
}

void SymbolFileDWARFDebugMap::FindFunctions(
    const Module::LookupInfo &lookup_info,
    const CompilerDeclContext &parent_decl_ctx, bool include_inlines,
    SymbolContextList &sc_list) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  LLDB_SCOPED_TIMERF("SymbolFileDWARFDebugMap::FindFunctions (name = %s)",
                     lookup_info.GetLookupName().GetCString());

  ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
    uint32_t sc_idx = sc_list.GetSize();
    oso_dwarf->FindFunctions(lookup_info, parent_decl_ctx, include_inlines,
                             sc_list);
    if (!sc_list.IsEmpty()) {
      RemoveFunctionsWithModuleNotEqualTo(m_objfile_sp->GetModule(), sc_list,
                                          sc_idx);
    }
    return IterationAction::Continue;
  });
}

void SymbolFileDWARFDebugMap::FindFunctions(const RegularExpression &regex,
                                            bool include_inlines,
                                            SymbolContextList &sc_list) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  LLDB_SCOPED_TIMERF("SymbolFileDWARFDebugMap::FindFunctions (regex = '%s')",
                     regex.GetText().str().c_str());

  ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
    uint32_t sc_idx = sc_list.GetSize();

    oso_dwarf->FindFunctions(regex, include_inlines, sc_list);
    if (!sc_list.IsEmpty()) {
      RemoveFunctionsWithModuleNotEqualTo(m_objfile_sp->GetModule(), sc_list,
                                          sc_idx);
    }
    return IterationAction::Continue;
  });
}

void SymbolFileDWARFDebugMap::GetTypes(SymbolContextScope *sc_scope,
                                       lldb::TypeClass type_mask,
                                       TypeList &type_list) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  LLDB_SCOPED_TIMERF("SymbolFileDWARFDebugMap::GetTypes (type_mask = 0x%8.8x)",
                     type_mask);

  SymbolFileDWARF *oso_dwarf = nullptr;
  if (sc_scope) {
    SymbolContext sc;
    sc_scope->CalculateSymbolContext(&sc);

    CompileUnitInfo *cu_info = GetCompUnitInfo(sc);
    if (cu_info) {
      oso_dwarf = GetSymbolFileByCompUnitInfo(cu_info);
      if (oso_dwarf)
        oso_dwarf->GetTypes(sc_scope, type_mask, type_list);
    }
  } else {
    ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
      oso_dwarf->GetTypes(sc_scope, type_mask, type_list);
      return IterationAction::Continue;
    });
  }
}

std::vector<std::unique_ptr<lldb_private::CallEdge>>
SymbolFileDWARFDebugMap::ParseCallEdgesInFunction(
    lldb_private::UserID func_id) {
  uint32_t oso_idx = GetOSOIndexFromUserID(func_id.GetID());
  SymbolFileDWARF *oso_dwarf = GetSymbolFileByOSOIndex(oso_idx);
  if (oso_dwarf)
    return oso_dwarf->ParseCallEdgesInFunction(func_id);
  return {};
}

DWARFDIE SymbolFileDWARFDebugMap::FindDefinitionDIE(const DWARFDIE &die) {
  DWARFDIE result;
  ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
    result = oso_dwarf->FindDefinitionDIE(die);
    return result ? IterationAction::Stop : IterationAction::Continue;
  });
  return result;
}

bool SymbolFileDWARFDebugMap::Supports_DW_AT_APPLE_objc_complete_type(
    SymbolFileDWARF *skip_dwarf_oso) {
  if (m_supports_DW_AT_APPLE_objc_complete_type == eLazyBoolCalculate) {
    m_supports_DW_AT_APPLE_objc_complete_type = eLazyBoolNo;
    ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
      if (skip_dwarf_oso != oso_dwarf &&
          oso_dwarf->Supports_DW_AT_APPLE_objc_complete_type(nullptr)) {
        m_supports_DW_AT_APPLE_objc_complete_type = eLazyBoolYes;
        return IterationAction::Stop;
      }
      return IterationAction::Continue;
    });
  }
  return m_supports_DW_AT_APPLE_objc_complete_type == eLazyBoolYes;
}

TypeSP SymbolFileDWARFDebugMap::FindCompleteObjCDefinitionTypeForDIE(
    const DWARFDIE &die, ConstString type_name,
    bool must_be_implementation) {
  // If we have a debug map, we will have an Objective-C symbol whose name is
  // the type name and whose type is eSymbolTypeObjCClass. If we can find that
  // symbol and find its containing parent, we can locate the .o file that will
  // contain the implementation definition since it will be scoped inside the
  // N_SO and we can then locate the SymbolFileDWARF that corresponds to that
  // N_SO.
  SymbolFileDWARF *oso_dwarf = nullptr;
  TypeSP type_sp;
  ObjectFile *module_objfile = m_objfile_sp->GetModule()->GetObjectFile();
  if (module_objfile) {
    Symtab *symtab = module_objfile->GetSymtab();
    if (symtab) {
      Symbol *objc_class_symbol = symtab->FindFirstSymbolWithNameAndType(
          type_name, eSymbolTypeObjCClass, Symtab::eDebugAny,
          Symtab::eVisibilityAny);
      if (objc_class_symbol) {
        // Get the N_SO symbol that contains the objective C class symbol as
        // this should be the .o file that contains the real definition...
        const Symbol *source_file_symbol = symtab->GetParent(objc_class_symbol);

        if (source_file_symbol &&
            source_file_symbol->GetType() == eSymbolTypeSourceFile) {
          const uint32_t source_file_symbol_idx =
              symtab->GetIndexForSymbol(source_file_symbol);
          if (source_file_symbol_idx != UINT32_MAX) {
            CompileUnitInfo *compile_unit_info =
                GetCompileUnitInfoForSymbolWithIndex(source_file_symbol_idx,
                                                     nullptr);
            if (compile_unit_info) {
              oso_dwarf = GetSymbolFileByCompUnitInfo(compile_unit_info);
              if (oso_dwarf) {
                TypeSP type_sp(oso_dwarf->FindCompleteObjCDefinitionTypeForDIE(
                    die, type_name, must_be_implementation));
                if (type_sp) {
                  return type_sp;
                }
              }
            }
          }
        }
      }
    }
  }

  // Only search all .o files for the definition if we don't need the
  // implementation because otherwise, with a valid debug map we should have
  // the ObjC class symbol and the code above should have found it.
  if (!must_be_implementation) {
    TypeSP type_sp;

    ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
      type_sp = oso_dwarf->FindCompleteObjCDefinitionTypeForDIE(
          die, type_name, must_be_implementation);
      return type_sp ? IterationAction::Stop : IterationAction::Continue;
    });

    return type_sp;
  }
  return TypeSP();
}

void SymbolFileDWARFDebugMap::FindTypes(const TypeQuery &query,
                                        TypeResults &results) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
    oso_dwarf->FindTypes(query, results);
    return results.Done(query) ? IterationAction::Stop
                               : IterationAction::Continue;
  });
}

CompilerDeclContext SymbolFileDWARFDebugMap::FindNamespace(
    lldb_private::ConstString name, const CompilerDeclContext &parent_decl_ctx,
    bool only_root_namespaces) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  CompilerDeclContext matching_namespace;

  ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
    matching_namespace =
        oso_dwarf->FindNamespace(name, parent_decl_ctx, only_root_namespaces);

    return matching_namespace ? IterationAction::Stop
                              : IterationAction::Continue;
  });

  return matching_namespace;
}

void SymbolFileDWARFDebugMap::DumpClangAST(Stream &s) {
  ForEachSymbolFile([&s](SymbolFileDWARF *oso_dwarf) {
    oso_dwarf->DumpClangAST(s);
    // The underlying assumption is that DumpClangAST(...) will obtain the
    // AST from the underlying TypeSystem and therefore we only need to do
    // this once and can stop after the first iteration hence we return true.
    return IterationAction::Stop;
  });
}

bool SymbolFileDWARFDebugMap::GetSeparateDebugInfo(
    lldb_private::StructuredData::Dictionary &d, bool errors_only) {
  StructuredData::Array separate_debug_info_files;
  const uint32_t cu_count = GetNumCompileUnits();
  for (uint32_t cu_idx = 0; cu_idx < cu_count; ++cu_idx) {
    const auto &info = m_compile_unit_infos[cu_idx];
    StructuredData::DictionarySP oso_data =
        std::make_shared<StructuredData::Dictionary>();
    oso_data->AddStringItem("so_file", info.so_file.GetPath());
    oso_data->AddStringItem("oso_path", info.oso_path);
    oso_data->AddIntegerItem("oso_mod_time",
                             (uint32_t)llvm::sys::toTimeT(info.oso_mod_time));

    bool loaded_successfully = false;
    if (GetModuleByOSOIndex(cu_idx)) {
      // If we have a valid pointer to the module, we successfully
      // loaded the oso if there are no load errors.
      if (!info.oso_load_error.Fail()) {
        loaded_successfully = true;
      }
    }
    if (!loaded_successfully) {
      oso_data->AddStringItem("error", info.oso_load_error.AsCString());
    }
    oso_data->AddBooleanItem("loaded", loaded_successfully);
    if (!errors_only || oso_data->HasKey("error"))
      separate_debug_info_files.AddItem(oso_data);
  }

  d.AddStringItem("type", "oso");
  d.AddStringItem("symfile", GetMainObjectFile()->GetFileSpec().GetPath());
  d.AddItem("separate-debug-info-files",
            std::make_shared<StructuredData::Array>(
                std::move(separate_debug_info_files)));
  return true;
}

lldb::CompUnitSP
SymbolFileDWARFDebugMap::GetCompileUnit(SymbolFileDWARF *oso_dwarf, DWARFCompileUnit &dwarf_cu) {
  if (oso_dwarf) {
    const uint32_t cu_count = GetNumCompileUnits();
    for (uint32_t cu_idx = 0; cu_idx < cu_count; ++cu_idx) {
      SymbolFileDWARF *oso_symfile =
          GetSymbolFileByCompUnitInfo(&m_compile_unit_infos[cu_idx]);
      if (oso_symfile == oso_dwarf) {
        if (m_compile_unit_infos[cu_idx].compile_units_sps.empty())
          ParseCompileUnitAtIndex(cu_idx);

        auto &id_to_index_map = m_compile_unit_infos[cu_idx].id_to_index_map;
        auto it = id_to_index_map.find(dwarf_cu.GetID());
        if (it != id_to_index_map.end())
          return m_compile_unit_infos[cu_idx]
              .compile_units_sps[it->getSecond()];
      }
    }
  }
  llvm_unreachable("this shouldn't happen");
}

SymbolFileDWARFDebugMap::CompileUnitInfo *
SymbolFileDWARFDebugMap::GetCompileUnitInfo(SymbolFileDWARF *oso_dwarf) {
  if (oso_dwarf) {
    const uint32_t cu_count = GetNumCompileUnits();
    for (uint32_t cu_idx = 0; cu_idx < cu_count; ++cu_idx) {
      SymbolFileDWARF *oso_symfile =
          GetSymbolFileByCompUnitInfo(&m_compile_unit_infos[cu_idx]);
      if (oso_symfile == oso_dwarf) {
        return &m_compile_unit_infos[cu_idx];
      }
    }
  }
  return nullptr;
}

void SymbolFileDWARFDebugMap::SetCompileUnit(SymbolFileDWARF *oso_dwarf,
                                             const CompUnitSP &cu_sp) {
  if (oso_dwarf) {
    const uint32_t cu_count = GetNumCompileUnits();
    for (uint32_t cu_idx = 0; cu_idx < cu_count; ++cu_idx) {
      SymbolFileDWARF *oso_symfile =
          GetSymbolFileByCompUnitInfo(&m_compile_unit_infos[cu_idx]);
      if (oso_symfile == oso_dwarf) {
        if (!m_compile_unit_infos[cu_idx].compile_units_sps.empty()) {
          assert(m_compile_unit_infos[cu_idx].compile_units_sps[0].get() ==
                 cu_sp.get());
        } else {
          assert(cu_sp->GetID() == 0 &&
                 "Setting first compile unit but with id different than 0!");
          auto &compile_units_sps = m_compile_unit_infos[cu_idx].compile_units_sps;
          compile_units_sps.push_back(cu_sp);
          m_compile_unit_infos[cu_idx].id_to_index_map.insert(
              {cu_sp->GetID(), compile_units_sps.size() - 1});

          SetCompileUnitAtIndex(cu_idx, cu_sp);
        }
      }
    }
  }
}

CompilerDeclContext
SymbolFileDWARFDebugMap::GetDeclContextForUID(lldb::user_id_t type_uid) {
  const uint64_t oso_idx = GetOSOIndexFromUserID(type_uid);
  if (SymbolFileDWARF *oso_dwarf = GetSymbolFileByOSOIndex(oso_idx))
    return oso_dwarf->GetDeclContextForUID(type_uid);
  return {};
}

CompilerDeclContext
SymbolFileDWARFDebugMap::GetDeclContextContainingUID(lldb::user_id_t type_uid) {
  const uint64_t oso_idx = GetOSOIndexFromUserID(type_uid);
  if (SymbolFileDWARF *oso_dwarf = GetSymbolFileByOSOIndex(oso_idx))
    return oso_dwarf->GetDeclContextContainingUID(type_uid);
  return {};
}

std::vector<CompilerContext>
SymbolFileDWARFDebugMap::GetCompilerContextForUID(lldb::user_id_t type_uid) {
  const uint64_t oso_idx = GetOSOIndexFromUserID(type_uid);
  if (SymbolFileDWARF *oso_dwarf = GetSymbolFileByOSOIndex(oso_idx))
    return oso_dwarf->GetCompilerContextForUID(type_uid);
  return {};
}

void SymbolFileDWARFDebugMap::ParseDeclsForContext(
    lldb_private::CompilerDeclContext decl_ctx) {
  ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
    oso_dwarf->ParseDeclsForContext(decl_ctx);
    return IterationAction::Continue;
  });
}

bool SymbolFileDWARFDebugMap::AddOSOFileRange(CompileUnitInfo *cu_info,
                                              lldb::addr_t exe_file_addr,
                                              lldb::addr_t exe_byte_size,
                                              lldb::addr_t oso_file_addr,
                                              lldb::addr_t oso_byte_size) {
  const uint32_t debug_map_idx =
      m_debug_map.FindEntryIndexThatContains(exe_file_addr);
  if (debug_map_idx != UINT32_MAX) {
    DebugMap::Entry *debug_map_entry =
        m_debug_map.FindEntryThatContains(exe_file_addr);
    debug_map_entry->data.SetOSOFileAddress(oso_file_addr);
    addr_t range_size = std::min<addr_t>(exe_byte_size, oso_byte_size);
    if (range_size == 0) {
      range_size = std::max<addr_t>(exe_byte_size, oso_byte_size);
      if (range_size == 0)
        range_size = 1;
    }
    cu_info->file_range_map.Append(
        FileRangeMap::Entry(oso_file_addr, range_size, exe_file_addr));
    return true;
  }
  return false;
}

void SymbolFileDWARFDebugMap::FinalizeOSOFileRanges(CompileUnitInfo *cu_info) {
  cu_info->file_range_map.Sort();
#if defined(DEBUG_OSO_DMAP)
  const FileRangeMap &oso_file_range_map = cu_info->GetFileRangeMap(this);
  const size_t n = oso_file_range_map.GetSize();
  printf("SymbolFileDWARFDebugMap::FinalizeOSOFileRanges (cu_info = %p) %s\n",
         cu_info, cu_info->oso_sp->module_sp->GetFileSpec().GetPath().c_str());
  for (size_t i = 0; i < n; ++i) {
    const FileRangeMap::Entry &entry = oso_file_range_map.GetEntryRef(i);
    printf("oso [0x%16.16" PRIx64 " - 0x%16.16" PRIx64
           ") ==> exe [0x%16.16" PRIx64 " - 0x%16.16" PRIx64 ")\n",
           entry.GetRangeBase(), entry.GetRangeEnd(), entry.data,
           entry.data + entry.GetByteSize());
  }
#endif
}

lldb::addr_t
SymbolFileDWARFDebugMap::LinkOSOFileAddress(SymbolFileDWARF *oso_symfile,
                                            lldb::addr_t oso_file_addr) {
  CompileUnitInfo *cu_info = GetCompileUnitInfo(oso_symfile);
  if (cu_info) {
    const FileRangeMap::Entry *oso_range_entry =
        cu_info->GetFileRangeMap(this).FindEntryThatContains(oso_file_addr);
    if (oso_range_entry) {
      const DebugMap::Entry *debug_map_entry =
          m_debug_map.FindEntryThatContains(oso_range_entry->data);
      if (debug_map_entry) {
        const lldb::addr_t offset =
            oso_file_addr - oso_range_entry->GetRangeBase();
        const lldb::addr_t exe_file_addr =
            debug_map_entry->GetRangeBase() + offset;
        return exe_file_addr;
      }
    }
  }
  return LLDB_INVALID_ADDRESS;
}

bool SymbolFileDWARFDebugMap::LinkOSOAddress(Address &addr) {
  // Make sure this address hasn't been fixed already
  Module *exe_module = GetObjectFile()->GetModule().get();
  Module *addr_module = addr.GetModule().get();
  if (addr_module == exe_module)
    return true; // Address is already in terms of the main executable module

  CompileUnitInfo *cu_info = GetCompileUnitInfo(
      GetSymbolFileAsSymbolFileDWARF(addr_module->GetSymbolFile()));
  if (cu_info) {
    const lldb::addr_t oso_file_addr = addr.GetFileAddress();
    const FileRangeMap::Entry *oso_range_entry =
        cu_info->GetFileRangeMap(this).FindEntryThatContains(oso_file_addr);
    if (oso_range_entry) {
      const DebugMap::Entry *debug_map_entry =
          m_debug_map.FindEntryThatContains(oso_range_entry->data);
      if (debug_map_entry) {
        const lldb::addr_t offset =
            oso_file_addr - oso_range_entry->GetRangeBase();
        const lldb::addr_t exe_file_addr =
            debug_map_entry->GetRangeBase() + offset;
        return exe_module->ResolveFileAddress(exe_file_addr, addr);
      }
    }
  }
  return true;
}

LineTable *SymbolFileDWARFDebugMap::LinkOSOLineTable(SymbolFileDWARF *oso_dwarf,
                                                     LineTable *line_table) {
  CompileUnitInfo *cu_info = GetCompileUnitInfo(oso_dwarf);
  if (cu_info)
    return line_table->LinkLineTable(cu_info->GetFileRangeMap(this));
  return nullptr;
}

size_t
SymbolFileDWARFDebugMap::AddOSOARanges(SymbolFileDWARF *dwarf2Data,
                                       DWARFDebugAranges *debug_aranges) {
  size_t num_line_entries_added = 0;
  if (debug_aranges && dwarf2Data) {
    CompileUnitInfo *compile_unit_info = GetCompileUnitInfo(dwarf2Data);
    if (compile_unit_info) {
      const FileRangeMap &file_range_map =
          compile_unit_info->GetFileRangeMap(this);
      for (size_t idx = 0; idx < file_range_map.GetSize(); idx++) {
        const FileRangeMap::Entry *entry = file_range_map.GetEntryAtIndex(idx);
        if (entry) {
          debug_aranges->AppendRange(*dwarf2Data->GetFileIndex(),
                                     entry->GetRangeBase(),
                                     entry->GetRangeEnd());
          num_line_entries_added++;
        }
      }
    }
  }
  return num_line_entries_added;
}

ModuleList SymbolFileDWARFDebugMap::GetDebugInfoModules() {
  ModuleList oso_modules;
  ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
    ObjectFile *oso_objfile = oso_dwarf->GetObjectFile();
    if (oso_objfile) {
      ModuleSP module_sp = oso_objfile->GetModule();
      if (module_sp)
        oso_modules.Append(module_sp);
    }
    return IterationAction::Continue;
  });
  return oso_modules;
}

Status SymbolFileDWARFDebugMap::CalculateFrameVariableError(StackFrame &frame) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());

  // We need to make sure that our PC value from the frame matches the module
  // for this object file since we will lookup the PC file address in the debug
  // map below.
  Address pc_addr = frame.GetFrameCodeAddress();
  if (pc_addr.GetModule() == m_objfile_sp->GetModule()) {
    Symtab *symtab = m_objfile_sp->GetSymtab();
    if (symtab) {
      const DebugMap::Entry *debug_map_entry =
          m_debug_map.FindEntryThatContains(pc_addr.GetFileAddress());
      if (debug_map_entry) {
        Symbol *symbol =
            symtab->SymbolAtIndex(debug_map_entry->data.GetExeSymbolIndex());
        if (symbol) {
          uint32_t oso_idx = 0;
          CompileUnitInfo *comp_unit_info =
              GetCompileUnitInfoForSymbolWithID(symbol->GetID(), &oso_idx);
          if (comp_unit_info) {
            Module *oso_module = GetModuleByCompUnitInfo(comp_unit_info);
            if (oso_module) {
              // Check the .o file's DWARF in case it has an error to display.
              SymbolFile *oso_sym_file = oso_module->GetSymbolFile();
              if (oso_sym_file)
                return oso_sym_file->GetFrameVariableError(frame);
            }
            // If we don't have a valid OSO module here, then something went
            // wrong as we have a symbol for the address in the debug map, but
            // we weren't able to open the .o file. Display an appropriate
            // error
            if (comp_unit_info->oso_load_error.Fail())
              return comp_unit_info->oso_load_error;
            else
              return Status("unable to load debug map object file \"%s\" "
                            "exist, debug info will not be loaded",
                            comp_unit_info->oso_path.GetCString());
          }
        }
      }
    }
  }
  return Status();
}

void SymbolFileDWARFDebugMap::GetCompileOptions(
    std::unordered_map<lldb::CompUnitSP, lldb_private::Args> &args) {

  ForEachSymbolFile([&](SymbolFileDWARF *oso_dwarf) {
    oso_dwarf->GetCompileOptions(args);
    return IterationAction::Continue;
  });
}
