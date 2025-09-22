//===-- AppleDWARFIndex.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/SymbolFile/DWARF/AppleDWARFIndex.h"
#include "Plugins/SymbolFile/DWARF/DWARFDeclContext.h"
#include "Plugins/SymbolFile/DWARF/DWARFUnit.h"
#include "Plugins/SymbolFile/DWARF/LogChannelDWARF.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/Function.h"
#include "llvm/Support/DJB.h"

using namespace lldb_private;
using namespace lldb;
using namespace lldb_private::dwarf;
using namespace lldb_private::plugin::dwarf;

std::unique_ptr<AppleDWARFIndex> AppleDWARFIndex::Create(
    Module &module, DWARFDataExtractor apple_names,
    DWARFDataExtractor apple_namespaces, DWARFDataExtractor apple_types,
    DWARFDataExtractor apple_objc, DWARFDataExtractor debug_str) {

  llvm::DataExtractor llvm_debug_str = debug_str.GetAsLLVM();

  auto apple_names_table_up = std::make_unique<llvm::AppleAcceleratorTable>(
      apple_names.GetAsLLVMDWARF(), llvm_debug_str);

  auto apple_namespaces_table_up =
      std::make_unique<llvm::AppleAcceleratorTable>(
          apple_namespaces.GetAsLLVMDWARF(), llvm_debug_str);

  auto apple_types_table_up = std::make_unique<llvm::AppleAcceleratorTable>(
      apple_types.GetAsLLVMDWARF(), llvm_debug_str);

  auto apple_objc_table_up = std::make_unique<llvm::AppleAcceleratorTable>(
      apple_objc.GetAsLLVMDWARF(), llvm_debug_str);

  auto extract_and_check = [](auto &TablePtr) {
    if (auto E = TablePtr->extract()) {
      llvm::consumeError(std::move(E));
      TablePtr.reset();
    }
  };

  extract_and_check(apple_names_table_up);
  extract_and_check(apple_namespaces_table_up);
  extract_and_check(apple_types_table_up);
  extract_and_check(apple_objc_table_up);
  assert(apple_names.GetByteSize() == 0 || apple_names.GetSharedDataBuffer());
  assert(apple_namespaces.GetByteSize() == 0 ||
         apple_namespaces.GetSharedDataBuffer());
  assert(apple_types.GetByteSize() == 0 || apple_types.GetSharedDataBuffer());
  assert(apple_objc.GetByteSize() == 0 || apple_objc.GetSharedDataBuffer());

  if (apple_names_table_up || apple_namespaces_table_up ||
      apple_types_table_up || apple_objc_table_up)
    return std::make_unique<AppleDWARFIndex>(
        module, std::move(apple_names_table_up),
        std::move(apple_namespaces_table_up), std::move(apple_types_table_up),
        std::move(apple_objc_table_up), apple_names.GetSharedDataBuffer(),
        apple_namespaces.GetSharedDataBuffer(),
        apple_types.GetSharedDataBuffer(), apple_objc.GetSharedDataBuffer());

  return nullptr;
}

/// Returns true if `tag` is a class_type of structure_type tag.
static bool IsClassOrStruct(dw_tag_t tag) {
  return tag == DW_TAG_class_type || tag == DW_TAG_structure_type;
}

/// Returns true if `entry` has an extractable DW_ATOM_qual_name_hash and it
/// matches `expected_hash`.
static bool
EntryHasMatchingQualhash(const llvm::AppleAcceleratorTable::Entry &entry,
                         uint32_t expected_hash) {
  std::optional<llvm::DWARFFormValue> form_value =
      entry.lookup(dwarf::DW_ATOM_qual_name_hash);
  if (!form_value)
    return false;
  std::optional<uint64_t> hash = form_value->getAsUnsignedConstant();
  return hash && (*hash == expected_hash);
}

/// Returns true if `entry` has an extractable DW_ATOM_die_tag and it matches
/// `expected_tag`. We also consider it a match if the tags are different but
/// in the set of {TAG_class_type, TAG_struct_type}.
static bool EntryHasMatchingTag(const llvm::AppleAcceleratorTable::Entry &entry,
                                dw_tag_t expected_tag) {
  std::optional<llvm::DWARFFormValue> form_value =
      entry.lookup(dwarf::DW_ATOM_die_tag);
  if (!form_value)
    return false;
  std::optional<uint64_t> maybe_tag = form_value->getAsUnsignedConstant();
  if (!maybe_tag)
    return false;
  auto tag = static_cast<dw_tag_t>(*maybe_tag);
  return tag == expected_tag ||
         (IsClassOrStruct(tag) && IsClassOrStruct(expected_tag));
}

/// Returns true if `entry` has an extractable DW_ATOM_type_flags and the flag
/// "DW_FLAG_type_implementation" is set.
static bool
HasImplementationFlag(const llvm::AppleAcceleratorTable::Entry &entry) {
  std::optional<llvm::DWARFFormValue> form_value =
      entry.lookup(dwarf::DW_ATOM_type_flags);
  if (!form_value)
    return false;
  std::optional<uint64_t> Flags = form_value->getAsUnsignedConstant();
  return Flags &&
         (*Flags & llvm::dwarf::AcceleratorTable::DW_FLAG_type_implementation);
}

void AppleDWARFIndex::SearchFor(const llvm::AppleAcceleratorTable &table,
                                llvm::StringRef name,
                                llvm::function_ref<bool(DWARFDIE die)> callback,
                                std::optional<dw_tag_t> search_for_tag,
                                std::optional<uint32_t> search_for_qualhash) {
  auto converted_cb = DIERefCallback(callback, name);
  for (const auto &entry : table.equal_range(name)) {
    if (search_for_qualhash &&
        !EntryHasMatchingQualhash(entry, *search_for_qualhash))
      continue;
    if (search_for_tag && !EntryHasMatchingTag(entry, *search_for_tag))
      continue;
    if (!converted_cb(entry))
      break;
  }
}

void AppleDWARFIndex::GetGlobalVariables(
    ConstString basename, llvm::function_ref<bool(DWARFDIE die)> callback) {
  if (!m_apple_names_up)
    return;
  SearchFor(*m_apple_names_up, basename, callback);
}

void AppleDWARFIndex::GetGlobalVariables(
    const RegularExpression &regex,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  if (!m_apple_names_up)
    return;

  DIERefCallbackImpl converted_cb = DIERefCallback(callback, regex.GetText());

  for (const auto &entry : m_apple_names_up->entries())
    if (std::optional<llvm::StringRef> name = entry.readName();
        name && Mangled(*name).NameMatches(regex))
      if (!converted_cb(entry.BaseEntry))
        return;
}

void AppleDWARFIndex::GetGlobalVariables(
    DWARFUnit &cu, llvm::function_ref<bool(DWARFDIE die)> callback) {
  if (!m_apple_names_up)
    return;

  const DWARFUnit &non_skeleton_cu = cu.GetNonSkeletonUnit();
  dw_offset_t lower_bound = non_skeleton_cu.GetOffset();
  dw_offset_t upper_bound = non_skeleton_cu.GetNextUnitOffset();
  auto is_in_range = [lower_bound, upper_bound](std::optional<uint32_t> val) {
    return val.has_value() && *val >= lower_bound && *val < upper_bound;
  };

  DIERefCallbackImpl converted_cb = DIERefCallback(callback);
  for (auto entry : m_apple_names_up->entries()) {
    if (is_in_range(entry.BaseEntry.getDIESectionOffset()))
      if (!converted_cb(entry.BaseEntry))
        return;
  }
}

void AppleDWARFIndex::GetObjCMethods(
    ConstString class_name, llvm::function_ref<bool(DWARFDIE die)> callback) {
  if (!m_apple_objc_up)
    return;
  SearchFor(*m_apple_objc_up, class_name, callback);
}

void AppleDWARFIndex::GetCompleteObjCClass(
    ConstString class_name, bool must_be_implementation,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  if (!m_apple_types_up)
    return;

  llvm::SmallVector<DIERef> decl_dies;
  auto converted_cb = DIERefCallback(callback, class_name);

  for (const auto &entry : m_apple_types_up->equal_range(class_name)) {
    if (HasImplementationFlag(entry)) {
      converted_cb(entry);
      return;
    }

    decl_dies.emplace_back(std::nullopt, DIERef::Section::DebugInfo,
                           *entry.getDIESectionOffset());
  }

  if (must_be_implementation)
    return;
  for (DIERef ref : decl_dies)
    if (!converted_cb(ref))
      return;
}

void AppleDWARFIndex::GetTypes(
    ConstString name, llvm::function_ref<bool(DWARFDIE die)> callback) {
  if (!m_apple_types_up)
    return;
  SearchFor(*m_apple_types_up, name, callback);
}

void AppleDWARFIndex::GetTypes(
    const DWARFDeclContext &context,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  if (!m_apple_types_up)
    return;

  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);
  const bool entries_have_tag =
      m_apple_types_up->containsAtomType(DW_ATOM_die_tag);
  const bool entries_have_qual_hash =
      m_apple_types_up->containsAtomType(DW_ATOM_qual_name_hash);

  llvm::StringRef expected_name = context[0].name;

  if (entries_have_tag && entries_have_qual_hash) {
    const dw_tag_t expected_tag = context[0].tag;
    const uint32_t expected_qualname_hash =
        llvm::djbHash(context.GetQualifiedName());
    if (log)
      m_module.LogMessage(log, "FindByNameAndTagAndQualifiedNameHash()");
    SearchFor(*m_apple_types_up, expected_name, callback, expected_tag,
               expected_qualname_hash);
    return;
  }

  // Historically, if there are no tags, we also ignore qual_hash (why?)
  if (!entries_have_tag) {
    SearchFor(*m_apple_names_up, expected_name, callback);
    return;
  }

  // We have a tag but no qual hash.

  // When searching for a scoped type (for example,
  // "std::vector<int>::const_iterator") searching for the innermost
  // name alone ("const_iterator") could yield many false
  // positives. By searching for the parent type ("vector<int>")
  // first we can avoid extracting type DIEs from object files that
  // would fail the filter anyway.
  if ((context.GetSize() > 1) && IsClassOrStruct(context[1].tag))
    if (m_apple_types_up->equal_range(context[1].name).empty())
      return;

  if (log)
    m_module.LogMessage(log, "FindByNameAndTag()");
  const dw_tag_t expected_tag = context[0].tag;
  SearchFor(*m_apple_types_up, expected_name, callback, expected_tag);
  return;
}

void AppleDWARFIndex::GetNamespaces(
    ConstString name, llvm::function_ref<bool(DWARFDIE die)> callback) {
  if (!m_apple_namespaces_up)
    return;
  SearchFor(*m_apple_namespaces_up, name, callback);
}

void AppleDWARFIndex::GetFunctions(
    const Module::LookupInfo &lookup_info, SymbolFileDWARF &dwarf,
    const CompilerDeclContext &parent_decl_ctx,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  if (!m_apple_names_up)
    return;

  ConstString name = lookup_info.GetLookupName();
  for (const auto &entry : m_apple_names_up->equal_range(name)) {
    DIERef die_ref(std::nullopt, DIERef::Section::DebugInfo,
                   *entry.getDIESectionOffset());
    DWARFDIE die = dwarf.GetDIE(die_ref);
    if (!die) {
      ReportInvalidDIERef(die_ref, name);
      continue;
    }
    if (!ProcessFunctionDIE(lookup_info, die, parent_decl_ctx, callback))
      return;
  }
}

void AppleDWARFIndex::GetFunctions(
    const RegularExpression &regex,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  return GetGlobalVariables(regex, callback);
}

void AppleDWARFIndex::Dump(Stream &s) {
  if (m_apple_names_up)
    s.PutCString(".apple_names index present\n");
  if (m_apple_namespaces_up)
    s.PutCString(".apple_namespaces index present\n");
  if (m_apple_types_up)
    s.PutCString(".apple_types index present\n");
  if (m_apple_objc_up)
    s.PutCString(".apple_objc index present\n");
  // TODO: Dump index contents
}
