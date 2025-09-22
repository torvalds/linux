//===-- ManualDWARFIndex.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/SymbolFile/DWARF/ManualDWARFIndex.h"
#include "Plugins/Language/ObjC/ObjCLanguage.h"
#include "Plugins/SymbolFile/DWARF/DWARFDebugInfo.h"
#include "Plugins/SymbolFile/DWARF/DWARFDeclContext.h"
#include "Plugins/SymbolFile/DWARF/LogChannelDWARF.h"
#include "Plugins/SymbolFile/DWARF/SymbolFileDWARFDwo.h"
#include "lldb/Core/DataFileCache.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Progress.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/Timer.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ThreadPool.h"
#include <optional>

using namespace lldb_private;
using namespace lldb;
using namespace lldb_private::dwarf;
using namespace lldb_private::plugin::dwarf;

void ManualDWARFIndex::Index() {
  if (m_indexed)
    return;
  m_indexed = true;

  ElapsedTime elapsed(m_index_time);
  LLDB_SCOPED_TIMERF("%p", static_cast<void *>(m_dwarf));
  if (LoadFromCache()) {
    m_dwarf->SetDebugInfoIndexWasLoadedFromCache();
    return;
  }

  DWARFDebugInfo &main_info = m_dwarf->DebugInfo();
  SymbolFileDWARFDwo *dwp_dwarf = m_dwarf->GetDwpSymbolFile().get();
  DWARFDebugInfo *dwp_info = dwp_dwarf ? &dwp_dwarf->DebugInfo() : nullptr;

  std::vector<DWARFUnit *> units_to_index;
  units_to_index.reserve(main_info.GetNumUnits() +
                         (dwp_info ? dwp_info->GetNumUnits() : 0));

  // Process all units in the main file, as well as any type units in the dwp
  // file. Type units in dwo files are handled when we reach the dwo file in
  // IndexUnit.
  for (size_t U = 0; U < main_info.GetNumUnits(); ++U) {
    DWARFUnit *unit = main_info.GetUnitAtIndex(U);
    if (unit && m_units_to_avoid.count(unit->GetOffset()) == 0)
      units_to_index.push_back(unit);
  }
  if (dwp_info && dwp_info->ContainsTypeUnits()) {
    for (size_t U = 0; U < dwp_info->GetNumUnits(); ++U) {
      if (auto *tu =
              llvm::dyn_cast<DWARFTypeUnit>(dwp_info->GetUnitAtIndex(U))) {
        if (!m_type_sigs_to_avoid.contains(tu->GetTypeHash()))
          units_to_index.push_back(tu);
      }
    }
  }

  if (units_to_index.empty())
    return;

  StreamString module_desc;
  m_module.GetDescription(module_desc.AsRawOstream(),
                          lldb::eDescriptionLevelBrief);

  // Include 2 passes per unit to index for extracting DIEs from the unit and
  // indexing the unit, and then 8 extra entries for finalizing each index set.
  const uint64_t total_progress = units_to_index.size() * 2 + 8;
  Progress progress("Manually indexing DWARF", module_desc.GetData(),
                    total_progress);

  std::vector<IndexSet> sets(units_to_index.size());

  // Keep memory down by clearing DIEs for any units if indexing
  // caused us to load the unit's DIEs.
  std::vector<std::optional<DWARFUnit::ScopedExtractDIEs>> clear_cu_dies(
      units_to_index.size());
  auto parser_fn = [&](size_t cu_idx) {
    IndexUnit(*units_to_index[cu_idx], dwp_dwarf, sets[cu_idx]);
    progress.Increment();
  };

  auto extract_fn = [&](size_t cu_idx) {
    clear_cu_dies[cu_idx] = units_to_index[cu_idx]->ExtractDIEsScoped();
    progress.Increment();
  };

  // Share one thread pool across operations to avoid the overhead of
  // recreating the threads.
  llvm::ThreadPoolTaskGroup task_group(Debugger::GetThreadPool());

  // Create a task runner that extracts dies for each DWARF unit in a
  // separate thread.
  // First figure out which units didn't have their DIEs already
  // parsed and remember this.  If no DIEs were parsed prior to this index
  // function call, we are going to want to clear the CU dies after we are
  // done indexing to make sure we don't pull in all DWARF dies, but we need
  // to wait until all units have been indexed in case a DIE in one
  // unit refers to another and the indexes accesses those DIEs.
  for (size_t i = 0; i < units_to_index.size(); ++i)
    task_group.async(extract_fn, i);
  task_group.wait();

  // Now create a task runner that can index each DWARF unit in a
  // separate thread so we can index quickly.
  for (size_t i = 0; i < units_to_index.size(); ++i)
    task_group.async(parser_fn, i);
  task_group.wait();

  auto finalize_fn = [this, &sets, &progress](NameToDIE(IndexSet::*index)) {
    NameToDIE &result = m_set.*index;
    for (auto &set : sets)
      result.Append(set.*index);
    result.Finalize();
    progress.Increment();
  };

  task_group.async(finalize_fn, &IndexSet::function_basenames);
  task_group.async(finalize_fn, &IndexSet::function_fullnames);
  task_group.async(finalize_fn, &IndexSet::function_methods);
  task_group.async(finalize_fn, &IndexSet::function_selectors);
  task_group.async(finalize_fn, &IndexSet::objc_class_selectors);
  task_group.async(finalize_fn, &IndexSet::globals);
  task_group.async(finalize_fn, &IndexSet::types);
  task_group.async(finalize_fn, &IndexSet::namespaces);
  task_group.wait();

  SaveToCache();
}

void ManualDWARFIndex::IndexUnit(DWARFUnit &unit, SymbolFileDWARFDwo *dwp,
                                 IndexSet &set) {
  Log *log = GetLog(DWARFLog::Lookups);

  if (log) {
    m_module.LogMessage(
        log, "ManualDWARFIndex::IndexUnit for unit at .debug_info[{0:x16}]",
        unit.GetOffset());
  }

  const LanguageType cu_language = SymbolFileDWARF::GetLanguage(unit);

  // First check if the unit has a DWO ID. If it does then we only want to index
  // the .dwo file or nothing at all. If we have a compile unit where we can't
  // locate the .dwo/.dwp file we don't want to index anything from the skeleton
  // compile unit because it is usally has no children unless
  // -fsplit-dwarf-inlining was used at compile time. This option will add a
  // copy of all DW_TAG_subprogram and any contained DW_TAG_inline_subroutine
  // DIEs so that symbolication will still work in the absence of the .dwo/.dwp
  // file, but the functions have no return types and all arguments and locals
  // have been removed. So we don't want to index any of these hacked up
  // function types. Types can still exist in the skeleton compile unit DWARF
  // though as some functions have template parameter types and other things
  // that cause extra copies of types to be included, but we should find these
  // types in the .dwo file only as methods could have return types removed and
  // we don't have to index incomplete types from the skeleton compile unit.
  if (unit.GetDWOId()) {
    // Index the .dwo or dwp instead of the skeleton unit.
    if (SymbolFileDWARFDwo *dwo_symbol_file = unit.GetDwoSymbolFile()) {
      // Type units in a dwp file are indexed separately, so we just need to
      // process the split unit here. However, if the split unit is in a dwo
      // file, then we need to process type units here.
      if (dwo_symbol_file == dwp) {
        IndexUnitImpl(unit.GetNonSkeletonUnit(), cu_language, set);
      } else {
        DWARFDebugInfo &dwo_info = dwo_symbol_file->DebugInfo();
        for (size_t i = 0; i < dwo_info.GetNumUnits(); ++i)
          IndexUnitImpl(*dwo_info.GetUnitAtIndex(i), cu_language, set);
      }
      return;
    }
    // This was a DWARF5 skeleton CU and the .dwo file couldn't be located.
    if (unit.GetVersion() >= 5 && unit.IsSkeletonUnit())
      return;

    // Either this is a DWARF 4 + fission CU with the .dwo file
    // missing, or it's a -gmodules pch or pcm. Try to detect the
    // latter by checking whether the first DIE is a DW_TAG_module.
    // If it's a pch/pcm, continue indexing it.
    if (unit.GetDIE(unit.GetFirstDIEOffset()).GetFirstChild().Tag() !=
        llvm::dwarf::DW_TAG_module)
      return;
  }
  // We have a normal compile unit which we want to index.
  IndexUnitImpl(unit, cu_language, set);
}

void ManualDWARFIndex::IndexUnitImpl(DWARFUnit &unit,
                                     const LanguageType cu_language,
                                     IndexSet &set) {
  for (const DWARFDebugInfoEntry &die : unit.dies()) {
    const dw_tag_t tag = die.Tag();

    switch (tag) {
    case DW_TAG_array_type:
    case DW_TAG_base_type:
    case DW_TAG_class_type:
    case DW_TAG_constant:
    case DW_TAG_enumeration_type:
    case DW_TAG_inlined_subroutine:
    case DW_TAG_namespace:
    case DW_TAG_imported_declaration:
    case DW_TAG_string_type:
    case DW_TAG_structure_type:
    case DW_TAG_subprogram:
    case DW_TAG_subroutine_type:
    case DW_TAG_typedef:
    case DW_TAG_union_type:
    case DW_TAG_unspecified_type:
    case DW_TAG_variable:
      break;

    default:
      continue;
    }

    const char *name = nullptr;
    const char *mangled_cstr = nullptr;
    bool is_declaration = false;
    bool has_address = false;
    bool has_location_or_const_value = false;
    bool is_global_or_static_variable = false;

    DWARFFormValue specification_die_form;
    DWARFAttributes attributes = die.GetAttributes(&unit);
    for (size_t i = 0; i < attributes.Size(); ++i) {
      dw_attr_t attr = attributes.AttributeAtIndex(i);
      DWARFFormValue form_value;
      switch (attr) {
      default:
        break;
      case DW_AT_name:
        if (attributes.ExtractFormValueAtIndex(i, form_value))
          name = form_value.AsCString();
        break;

      case DW_AT_declaration:
        if (attributes.ExtractFormValueAtIndex(i, form_value))
          is_declaration = form_value.Unsigned() != 0;
        break;

      case DW_AT_MIPS_linkage_name:
      case DW_AT_linkage_name:
        if (attributes.ExtractFormValueAtIndex(i, form_value))
          mangled_cstr = form_value.AsCString();
        break;

      case DW_AT_low_pc:
      case DW_AT_high_pc:
      case DW_AT_ranges:
        has_address = true;
        break;

      case DW_AT_entry_pc:
        has_address = true;
        break;

      case DW_AT_location:
      case DW_AT_const_value:
        has_location_or_const_value = true;
        is_global_or_static_variable = die.IsGlobalOrStaticScopeVariable();

        break;

      case DW_AT_specification:
        if (attributes.ExtractFormValueAtIndex(i, form_value))
          specification_die_form = form_value;
        break;
      }
    }

    DIERef ref = *DWARFDIE(&unit, &die).GetDIERef();
    switch (tag) {
    case DW_TAG_inlined_subroutine:
    case DW_TAG_subprogram:
      if (has_address) {
        if (name) {
          bool is_objc_method = false;
          if (cu_language == eLanguageTypeObjC ||
              cu_language == eLanguageTypeObjC_plus_plus) {
            std::optional<const ObjCLanguage::MethodName> objc_method =
                ObjCLanguage::MethodName::Create(name, true);
            if (objc_method) {
              is_objc_method = true;
              ConstString class_name_with_category(
                  objc_method->GetClassNameWithCategory());
              ConstString objc_selector_name(objc_method->GetSelector());
              ConstString objc_fullname_no_category_name(
                  objc_method->GetFullNameWithoutCategory().c_str());
              ConstString class_name_no_category(objc_method->GetClassName());
              set.function_fullnames.Insert(ConstString(name), ref);
              if (class_name_with_category)
                set.objc_class_selectors.Insert(class_name_with_category, ref);
              if (class_name_no_category &&
                  class_name_no_category != class_name_with_category)
                set.objc_class_selectors.Insert(class_name_no_category, ref);
              if (objc_selector_name)
                set.function_selectors.Insert(objc_selector_name, ref);
              if (objc_fullname_no_category_name)
                set.function_fullnames.Insert(objc_fullname_no_category_name,
                                              ref);
            }
          }
          // If we have a mangled name, then the DW_AT_name attribute is
          // usually the method name without the class or any parameters
          bool is_method = DWARFDIE(&unit, &die).IsMethod();

          if (is_method)
            set.function_methods.Insert(ConstString(name), ref);
          else
            set.function_basenames.Insert(ConstString(name), ref);

          if (!is_method && !mangled_cstr && !is_objc_method)
            set.function_fullnames.Insert(ConstString(name), ref);
        }
        if (mangled_cstr) {
          // Make sure our mangled name isn't the same string table entry as
          // our name. If it starts with '_', then it is ok, else compare the
          // string to make sure it isn't the same and we don't end up with
          // duplicate entries
          if (name && name != mangled_cstr &&
              ((mangled_cstr[0] == '_') ||
               (::strcmp(name, mangled_cstr) != 0))) {
            set.function_fullnames.Insert(ConstString(mangled_cstr), ref);
          }
        }
      }
      break;

    case DW_TAG_array_type:
    case DW_TAG_base_type:
    case DW_TAG_class_type:
    case DW_TAG_constant:
    case DW_TAG_enumeration_type:
    case DW_TAG_string_type:
    case DW_TAG_structure_type:
    case DW_TAG_subroutine_type:
    case DW_TAG_typedef:
    case DW_TAG_union_type:
    case DW_TAG_unspecified_type:
      if (name && !is_declaration)
        set.types.Insert(ConstString(name), ref);
      if (mangled_cstr && !is_declaration)
        set.types.Insert(ConstString(mangled_cstr), ref);
      break;

    case DW_TAG_namespace:
    case DW_TAG_imported_declaration:
      if (name)
        set.namespaces.Insert(ConstString(name), ref);
      break;

    case DW_TAG_variable:
      if (name && has_location_or_const_value && is_global_or_static_variable) {
        set.globals.Insert(ConstString(name), ref);
        // Be sure to include variables by their mangled and demangled names if
        // they have any since a variable can have a basename "i", a mangled
        // named "_ZN12_GLOBAL__N_11iE" and a demangled mangled name
        // "(anonymous namespace)::i"...

        // Make sure our mangled name isn't the same string table entry as our
        // name. If it starts with '_', then it is ok, else compare the string
        // to make sure it isn't the same and we don't end up with duplicate
        // entries
        if (mangled_cstr && name != mangled_cstr &&
            ((mangled_cstr[0] == '_') || (::strcmp(name, mangled_cstr) != 0))) {
          set.globals.Insert(ConstString(mangled_cstr), ref);
        }
      }
      break;

    default:
      continue;
    }
  }
}

void ManualDWARFIndex::GetGlobalVariables(
    ConstString basename, llvm::function_ref<bool(DWARFDIE die)> callback) {
  Index();
  m_set.globals.Find(basename,
                     DIERefCallback(callback, basename.GetStringRef()));
}

void ManualDWARFIndex::GetGlobalVariables(
    const RegularExpression &regex,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  Index();
  m_set.globals.Find(regex, DIERefCallback(callback, regex.GetText()));
}

void ManualDWARFIndex::GetGlobalVariables(
    DWARFUnit &unit, llvm::function_ref<bool(DWARFDIE die)> callback) {
  Index();
  m_set.globals.FindAllEntriesForUnit(unit, DIERefCallback(callback));
}

void ManualDWARFIndex::GetObjCMethods(
    ConstString class_name, llvm::function_ref<bool(DWARFDIE die)> callback) {
  Index();
  m_set.objc_class_selectors.Find(
      class_name, DIERefCallback(callback, class_name.GetStringRef()));
}

void ManualDWARFIndex::GetCompleteObjCClass(
    ConstString class_name, bool must_be_implementation,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  Index();
  m_set.types.Find(class_name,
                   DIERefCallback(callback, class_name.GetStringRef()));
}

void ManualDWARFIndex::GetTypes(
    ConstString name, llvm::function_ref<bool(DWARFDIE die)> callback) {
  Index();
  m_set.types.Find(name, DIERefCallback(callback, name.GetStringRef()));
}

void ManualDWARFIndex::GetTypes(
    const DWARFDeclContext &context,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  Index();
  auto name = context[0].name;
  m_set.types.Find(ConstString(name),
                   DIERefCallback(callback, llvm::StringRef(name)));
}

void ManualDWARFIndex::GetNamespaces(
    ConstString name, llvm::function_ref<bool(DWARFDIE die)> callback) {
  Index();
  m_set.namespaces.Find(name, DIERefCallback(callback, name.GetStringRef()));
}

void ManualDWARFIndex::GetFunctions(
    const Module::LookupInfo &lookup_info, SymbolFileDWARF &dwarf,
    const CompilerDeclContext &parent_decl_ctx,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  Index();
  ConstString name = lookup_info.GetLookupName();
  FunctionNameType name_type_mask = lookup_info.GetNameTypeMask();

  if (name_type_mask & eFunctionNameTypeFull) {
    if (!m_set.function_fullnames.Find(
            name, DIERefCallback(
                      [&](DWARFDIE die) {
                        if (!SymbolFileDWARF::DIEInDeclContext(parent_decl_ctx,
                                                               die))
                          return true;
                        return callback(die);
                      },
                      name.GetStringRef())))
      return;
  }
  if (name_type_mask & eFunctionNameTypeBase) {
    if (!m_set.function_basenames.Find(
            name, DIERefCallback(
                      [&](DWARFDIE die) {
                        if (!SymbolFileDWARF::DIEInDeclContext(parent_decl_ctx,
                                                               die))
                          return true;
                        return callback(die);
                      },
                      name.GetStringRef())))
      return;
  }

  if (name_type_mask & eFunctionNameTypeMethod && !parent_decl_ctx.IsValid()) {
    if (!m_set.function_methods.Find(
            name, DIERefCallback(callback, name.GetStringRef())))
      return;
  }

  if (name_type_mask & eFunctionNameTypeSelector &&
      !parent_decl_ctx.IsValid()) {
    if (!m_set.function_selectors.Find(
            name, DIERefCallback(callback, name.GetStringRef())))
      return;
  }
}

void ManualDWARFIndex::GetFunctions(
    const RegularExpression &regex,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  Index();

  if (!m_set.function_basenames.Find(regex,
                                     DIERefCallback(callback, regex.GetText())))
    return;
  if (!m_set.function_fullnames.Find(regex,
                                     DIERefCallback(callback, regex.GetText())))
    return;
}

void ManualDWARFIndex::Dump(Stream &s) {
  s.Format("Manual DWARF index for ({0}) '{1:F}':",
           m_module.GetArchitecture().GetArchitectureName(),
           m_module.GetObjectFile()->GetFileSpec());
  s.Printf("\nFunction basenames:\n");
  m_set.function_basenames.Dump(&s);
  s.Printf("\nFunction fullnames:\n");
  m_set.function_fullnames.Dump(&s);
  s.Printf("\nFunction methods:\n");
  m_set.function_methods.Dump(&s);
  s.Printf("\nFunction selectors:\n");
  m_set.function_selectors.Dump(&s);
  s.Printf("\nObjective-C class selectors:\n");
  m_set.objc_class_selectors.Dump(&s);
  s.Printf("\nGlobals and statics:\n");
  m_set.globals.Dump(&s);
  s.Printf("\nTypes:\n");
  m_set.types.Dump(&s);
  s.Printf("\nNamespaces:\n");
  m_set.namespaces.Dump(&s);
}

constexpr llvm::StringLiteral kIdentifierManualDWARFIndex("DIDX");
// Define IDs for the different tables when encoding and decoding the
// ManualDWARFIndex NameToDIE objects so we can avoid saving any empty maps.
enum DataID {
  kDataIDFunctionBasenames = 1u,
  kDataIDFunctionFullnames,
  kDataIDFunctionMethods,
  kDataIDFunctionSelectors,
  kDataIDFunctionObjcClassSelectors,
  kDataIDGlobals,
  kDataIDTypes,
  kDataIDNamespaces,
  kDataIDEnd = 255u,

};

// Version 2 changes the encoding of DIERef objects used in the DWARF manual
// index name tables. See DIERef class for details.
constexpr uint32_t CURRENT_CACHE_VERSION = 2;

bool ManualDWARFIndex::IndexSet::Decode(const DataExtractor &data,
                                        lldb::offset_t *offset_ptr) {
  StringTableReader strtab;
  // We now decode the string table for all strings in the data cache file.
  if (!strtab.Decode(data, offset_ptr))
    return false;

  llvm::StringRef identifier((const char *)data.GetData(offset_ptr, 4), 4);
  if (identifier != kIdentifierManualDWARFIndex)
    return false;
  const uint32_t version = data.GetU32(offset_ptr);
  if (version != CURRENT_CACHE_VERSION)
    return false;

  bool done = false;
  while (!done) {
    switch (data.GetU8(offset_ptr)) {
    default:
      // If we got here, this is not expected, we expect the data IDs to match
      // one of the values from the DataID enumeration.
      return false;
    case kDataIDFunctionBasenames:
      if (!function_basenames.Decode(data, offset_ptr, strtab))
        return false;
      break;
    case kDataIDFunctionFullnames:
      if (!function_fullnames.Decode(data, offset_ptr, strtab))
        return false;
      break;
    case kDataIDFunctionMethods:
      if (!function_methods.Decode(data, offset_ptr, strtab))
        return false;
      break;
    case kDataIDFunctionSelectors:
      if (!function_selectors.Decode(data, offset_ptr, strtab))
        return false;
      break;
    case kDataIDFunctionObjcClassSelectors:
      if (!objc_class_selectors.Decode(data, offset_ptr, strtab))
        return false;
      break;
    case kDataIDGlobals:
      if (!globals.Decode(data, offset_ptr, strtab))
        return false;
      break;
    case kDataIDTypes:
      if (!types.Decode(data, offset_ptr, strtab))
        return false;
      break;
    case kDataIDNamespaces:
      if (!namespaces.Decode(data, offset_ptr, strtab))
        return false;
      break;
    case kDataIDEnd:
      // We got to the end of our NameToDIE encodings.
      done = true;
      break;
    }
  }
  // Success!
  return true;
}

void ManualDWARFIndex::IndexSet::Encode(DataEncoder &encoder) const {
  ConstStringTable strtab;

  // Encoder the DWARF index into a separate encoder first. This allows us
  // gather all of the strings we willl need in "strtab" as we will need to
  // write the string table out before the symbol table.
  DataEncoder index_encoder(encoder.GetByteOrder(),
                            encoder.GetAddressByteSize());

  index_encoder.AppendData(kIdentifierManualDWARFIndex);
  // Encode the data version.
  index_encoder.AppendU32(CURRENT_CACHE_VERSION);

  if (!function_basenames.IsEmpty()) {
    index_encoder.AppendU8(kDataIDFunctionBasenames);
    function_basenames.Encode(index_encoder, strtab);
  }
  if (!function_fullnames.IsEmpty()) {
    index_encoder.AppendU8(kDataIDFunctionFullnames);
    function_fullnames.Encode(index_encoder, strtab);
  }
  if (!function_methods.IsEmpty()) {
    index_encoder.AppendU8(kDataIDFunctionMethods);
    function_methods.Encode(index_encoder, strtab);
  }
  if (!function_selectors.IsEmpty()) {
    index_encoder.AppendU8(kDataIDFunctionSelectors);
    function_selectors.Encode(index_encoder, strtab);
  }
  if (!objc_class_selectors.IsEmpty()) {
    index_encoder.AppendU8(kDataIDFunctionObjcClassSelectors);
    objc_class_selectors.Encode(index_encoder, strtab);
  }
  if (!globals.IsEmpty()) {
    index_encoder.AppendU8(kDataIDGlobals);
    globals.Encode(index_encoder, strtab);
  }
  if (!types.IsEmpty()) {
    index_encoder.AppendU8(kDataIDTypes);
    types.Encode(index_encoder, strtab);
  }
  if (!namespaces.IsEmpty()) {
    index_encoder.AppendU8(kDataIDNamespaces);
    namespaces.Encode(index_encoder, strtab);
  }
  index_encoder.AppendU8(kDataIDEnd);

  // Now that all strings have been gathered, we will emit the string table.
  strtab.Encode(encoder);
  // Followed by the symbol table data.
  encoder.AppendData(index_encoder.GetData());
}

bool ManualDWARFIndex::Decode(const DataExtractor &data,
                              lldb::offset_t *offset_ptr,
                              bool &signature_mismatch) {
  signature_mismatch = false;
  CacheSignature signature;
  if (!signature.Decode(data, offset_ptr))
    return false;
  if (CacheSignature(m_dwarf->GetObjectFile()) != signature) {
    signature_mismatch = true;
    return false;
  }
  IndexSet set;
  if (!set.Decode(data, offset_ptr))
    return false;
  m_set = std::move(set);
  return true;
}

bool ManualDWARFIndex::Encode(DataEncoder &encoder) const {
  CacheSignature signature(m_dwarf->GetObjectFile());
  if (!signature.Encode(encoder))
    return false;
  m_set.Encode(encoder);
  return true;
}

std::string ManualDWARFIndex::GetCacheKey() {
  std::string key;
  llvm::raw_string_ostream strm(key);
  // DWARF Index can come from different object files for the same module. A
  // module can have one object file as the main executable and might have
  // another object file in a separate symbol file, or we might have a .dwo file
  // that claims its module is the main executable.
  ObjectFile *objfile = m_dwarf->GetObjectFile();
  strm << objfile->GetModule()->GetCacheKey() << "-dwarf-index-"
      << llvm::format_hex(objfile->GetCacheHash(), 10);
  return strm.str();
}

bool ManualDWARFIndex::LoadFromCache() {
  DataFileCache *cache = Module::GetIndexCache();
  if (!cache)
    return false;
  ObjectFile *objfile = m_dwarf->GetObjectFile();
  if (!objfile)
    return false;
  std::unique_ptr<llvm::MemoryBuffer> mem_buffer_up =
      cache->GetCachedData(GetCacheKey());
  if (!mem_buffer_up)
    return false;
  DataExtractor data(mem_buffer_up->getBufferStart(),
                     mem_buffer_up->getBufferSize(),
                     endian::InlHostByteOrder(),
                     objfile->GetAddressByteSize());
  bool signature_mismatch = false;
  lldb::offset_t offset = 0;
  const bool result = Decode(data, &offset, signature_mismatch);
  if (signature_mismatch)
    cache->RemoveCacheFile(GetCacheKey());
  return result;
}

void ManualDWARFIndex::SaveToCache() {
  DataFileCache *cache = Module::GetIndexCache();
  if (!cache)
    return; // Caching is not enabled.
  ObjectFile *objfile = m_dwarf->GetObjectFile();
  if (!objfile)
    return;
  DataEncoder file(endian::InlHostByteOrder(), objfile->GetAddressByteSize());
  // Encode will return false if the object file doesn't have anything to make
  // a signature from.
  if (Encode(file)) {
    if (cache->SetCachedData(GetCacheKey(), file.GetData()))
      m_dwarf->SetDebugInfoIndexWasSavedToCache();
  }
}
