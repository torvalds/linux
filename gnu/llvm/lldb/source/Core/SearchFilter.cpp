//===-- SearchFilter.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/SearchFilter.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"

#include <memory>
#include <mutex>
#include <string>

#include <cinttypes>
#include <cstring>

namespace lldb_private {
class Address;
}
namespace lldb_private {
class Function;
}

using namespace lldb;
using namespace lldb_private;

const char *SearchFilter::g_ty_to_name[] = {"Unconstrained", "Exception",
                                            "Module",        "Modules",
                                            "ModulesAndCU",  "Unknown"};

const char
    *SearchFilter::g_option_names[SearchFilter::OptionNames::LastOptionName] = {
        "ModuleList", "CUList"};

const char *SearchFilter::FilterTyToName(enum FilterTy type) {
  if (type > LastKnownFilterType)
    return g_ty_to_name[UnknownFilter];

  return g_ty_to_name[type];
}

SearchFilter::FilterTy SearchFilter::NameToFilterTy(llvm::StringRef name) {
  for (size_t i = 0; i <= LastKnownFilterType; i++) {
    if (name == g_ty_to_name[i])
      return (FilterTy)i;
  }
  return UnknownFilter;
}

Searcher::Searcher() = default;

Searcher::~Searcher() = default;

void Searcher::GetDescription(Stream *s) {}

SearchFilter::SearchFilter(const TargetSP &target_sp, unsigned char filterType)
    : m_target_sp(target_sp), SubclassID(filterType) {}

SearchFilter::~SearchFilter() = default;

SearchFilterSP SearchFilter::CreateFromStructuredData(
    const lldb::TargetSP& target_sp,
    const StructuredData::Dictionary &filter_dict,
    Status &error) {
  SearchFilterSP result_sp;
  if (!filter_dict.IsValid()) {
    error.SetErrorString("Can't deserialize from an invalid data object.");
    return result_sp;
  }

  llvm::StringRef subclass_name;

  bool success = filter_dict.GetValueForKeyAsString(
      GetSerializationSubclassKey(), subclass_name);
  if (!success) {
    error.SetErrorString("Filter data missing subclass key");
    return result_sp;
  }

  FilterTy filter_type = NameToFilterTy(subclass_name);
  if (filter_type == UnknownFilter) {
    error.SetErrorStringWithFormatv("Unknown filter type: {0}.", subclass_name);
    return result_sp;
  }

  StructuredData::Dictionary *subclass_options = nullptr;
  success = filter_dict.GetValueForKeyAsDictionary(
      GetSerializationSubclassOptionsKey(), subclass_options);
  if (!success || !subclass_options || !subclass_options->IsValid()) {
    error.SetErrorString("Filter data missing subclass options key.");
    return result_sp;
  }

  switch (filter_type) {
  case Unconstrained:
    result_sp = SearchFilterForUnconstrainedSearches::CreateFromStructuredData(
        target_sp, *subclass_options, error);
    break;
  case ByModule:
    result_sp = SearchFilterByModule::CreateFromStructuredData(
        target_sp, *subclass_options, error);
    break;
  case ByModules:
    result_sp = SearchFilterByModuleList::CreateFromStructuredData(
        target_sp, *subclass_options, error);
    break;
  case ByModulesAndCU:
    result_sp = SearchFilterByModuleListAndCU::CreateFromStructuredData(
        target_sp, *subclass_options, error);
    break;
  case Exception:
    error.SetErrorString("Can't serialize exception breakpoints yet.");
    break;
  default:
    llvm_unreachable("Should never get an uresolvable filter type.");
  }

  return result_sp;
}

bool SearchFilter::ModulePasses(const FileSpec &spec) { return true; }

bool SearchFilter::ModulePasses(const ModuleSP &module_sp) { return true; }

bool SearchFilter::AddressPasses(Address &address) { return true; }

bool SearchFilter::CompUnitPasses(FileSpec &fileSpec) { return true; }

bool SearchFilter::CompUnitPasses(CompileUnit &compUnit) { return true; }

bool SearchFilter::FunctionPasses(Function &function) {
  // This is a slightly cheesy job, but since we don't have finer grained 
  // filters yet, just checking that the start address passes is probably
  // good enough for the base class behavior.
  Address addr = function.GetAddressRange().GetBaseAddress();
  return AddressPasses(addr);
}


uint32_t SearchFilter::GetFilterRequiredItems() {
  return (lldb::SymbolContextItem)0;
}

void SearchFilter::GetDescription(Stream *s) {}

void SearchFilter::Dump(Stream *s) const {}

lldb::SearchFilterSP SearchFilter::CreateCopy(lldb::TargetSP& target_sp) {
  SearchFilterSP ret_sp = DoCreateCopy();
  ret_sp->SetTarget(target_sp);
  return ret_sp;
}

// Helper functions for serialization.

StructuredData::DictionarySP
SearchFilter::WrapOptionsDict(StructuredData::DictionarySP options_dict_sp) {
  if (!options_dict_sp || !options_dict_sp->IsValid())
    return StructuredData::DictionarySP();

  auto type_dict_sp = std::make_shared<StructuredData::Dictionary>();
  type_dict_sp->AddStringItem(GetSerializationSubclassKey(), GetFilterName());
  type_dict_sp->AddItem(GetSerializationSubclassOptionsKey(), options_dict_sp);

  return type_dict_sp;
}

void SearchFilter::SerializeFileSpecList(
    StructuredData::DictionarySP &options_dict_sp, OptionNames name,
    FileSpecList &file_list) {
  size_t num_modules = file_list.GetSize();

  // Don't serialize empty lists.
  if (num_modules == 0)
    return;

  auto module_array_sp = std::make_shared<StructuredData::Array>();
  for (size_t i = 0; i < num_modules; i++) {
    module_array_sp->AddItem(std::make_shared<StructuredData::String>(
        file_list.GetFileSpecAtIndex(i).GetPath()));
  }
  options_dict_sp->AddItem(GetKey(name), module_array_sp);
}

// UTILITY Functions to help iterate down through the elements of the
// SymbolContext.

void SearchFilter::Search(Searcher &searcher) {
  SymbolContext empty_sc;

  if (!m_target_sp)
    return;
  empty_sc.target_sp = m_target_sp;

  if (searcher.GetDepth() == lldb::eSearchDepthTarget) {
    searcher.SearchCallback(*this, empty_sc, nullptr);
    return;
  }

  DoModuleIteration(empty_sc, searcher);
}

void SearchFilter::SearchInModuleList(Searcher &searcher, ModuleList &modules) {
  SymbolContext empty_sc;

  if (!m_target_sp)
    return;
  empty_sc.target_sp = m_target_sp;

  if (searcher.GetDepth() == lldb::eSearchDepthTarget) {
    searcher.SearchCallback(*this, empty_sc, nullptr);
    return;
  }

  for (ModuleSP module_sp : modules.Modules()) {
    if (!ModulePasses(module_sp))
      continue;
    if (DoModuleIteration(module_sp, searcher) == Searcher::eCallbackReturnStop)
      return;
  }
}

Searcher::CallbackReturn
SearchFilter::DoModuleIteration(const lldb::ModuleSP &module_sp,
                                Searcher &searcher) {
  SymbolContext matchingContext(m_target_sp, module_sp);
  return DoModuleIteration(matchingContext, searcher);
}

Searcher::CallbackReturn
SearchFilter::DoModuleIteration(const SymbolContext &context,
                                Searcher &searcher) {
  if (searcher.GetDepth() < lldb::eSearchDepthModule)
    return Searcher::eCallbackReturnContinue;

  if (context.module_sp) {
    if (searcher.GetDepth() != lldb::eSearchDepthModule)
      return DoCUIteration(context.module_sp, context, searcher);

    SymbolContext matchingContext(context.module_sp.get());
    searcher.SearchCallback(*this, matchingContext, nullptr);
    return Searcher::eCallbackReturnContinue;
  }

  for (ModuleSP module_sp : m_target_sp->GetImages().Modules()) {
    // If this is the last level supplied, then call the callback directly,
    // otherwise descend.
    if (!ModulePasses(module_sp))
      continue;

    if (searcher.GetDepth() == lldb::eSearchDepthModule) {
      SymbolContext matchingContext(m_target_sp, module_sp);

      Searcher::CallbackReturn shouldContinue =
          searcher.SearchCallback(*this, matchingContext, nullptr);
      if (shouldContinue == Searcher::eCallbackReturnStop ||
          shouldContinue == Searcher::eCallbackReturnPop)
        return shouldContinue;
    } else {
      Searcher::CallbackReturn shouldContinue =
          DoCUIteration(module_sp, context, searcher);
      if (shouldContinue == Searcher::eCallbackReturnStop)
        return shouldContinue;
      else if (shouldContinue == Searcher::eCallbackReturnPop)
        continue;
    }
  }

  return Searcher::eCallbackReturnContinue;
}

Searcher::CallbackReturn
SearchFilter::DoCUIteration(const ModuleSP &module_sp,
                            const SymbolContext &context, Searcher &searcher) {
  Searcher::CallbackReturn shouldContinue;
  if (context.comp_unit != nullptr) {
    if (CompUnitPasses(*context.comp_unit)) {
      SymbolContext matchingContext(m_target_sp, module_sp, context.comp_unit);
      return searcher.SearchCallback(*this, matchingContext, nullptr);
    }
    return Searcher::eCallbackReturnContinue;
  }

  const size_t num_comp_units = module_sp->GetNumCompileUnits();
  for (size_t i = 0; i < num_comp_units; i++) {
    CompUnitSP cu_sp(module_sp->GetCompileUnitAtIndex(i));
    if (!cu_sp)
      continue;
    if (!CompUnitPasses(*(cu_sp.get())))
      continue;

    if (searcher.GetDepth() == lldb::eSearchDepthCompUnit) {
      SymbolContext matchingContext(m_target_sp, module_sp, cu_sp.get());

      shouldContinue = searcher.SearchCallback(*this, matchingContext, nullptr);

      if (shouldContinue == Searcher::eCallbackReturnPop)
        return Searcher::eCallbackReturnContinue;
      else if (shouldContinue == Searcher::eCallbackReturnStop)
        return shouldContinue;
      continue;
    }

    // First make sure this compile unit's functions are parsed
    // since CompUnit::ForeachFunction only iterates over already
    // parsed functions.
    SymbolFile *sym_file = module_sp->GetSymbolFile();
    if (!sym_file)
      continue;
    if (!sym_file->ParseFunctions(*cu_sp))
      continue;
    // If we got any functions, use ForeachFunction to do the iteration.
    cu_sp->ForeachFunction([&](const FunctionSP &func_sp) {
      if (!FunctionPasses(*func_sp.get()))
        return false; // Didn't pass the filter, just keep going.
      if (searcher.GetDepth() == lldb::eSearchDepthFunction) {
        SymbolContext matchingContext(m_target_sp, module_sp, cu_sp.get(),
                                      func_sp.get());
        shouldContinue =
            searcher.SearchCallback(*this, matchingContext, nullptr);
      } else {
        shouldContinue = DoFunctionIteration(func_sp.get(), context, searcher);
      }
      return shouldContinue != Searcher::eCallbackReturnContinue;
    });
  }
  return Searcher::eCallbackReturnContinue;
}

Searcher::CallbackReturn SearchFilter::DoFunctionIteration(
    Function *function, const SymbolContext &context, Searcher &searcher) {
  // FIXME: Implement...
  return Searcher::eCallbackReturnContinue;
}

//  SearchFilterForUnconstrainedSearches:
//  Selects a shared library matching a given file spec, consulting the targets
//  "black list".
SearchFilterSP SearchFilterForUnconstrainedSearches::CreateFromStructuredData(
    const lldb::TargetSP& target_sp,
    const StructuredData::Dictionary &data_dict,
    Status &error) {
  // No options for an unconstrained search.
  return std::make_shared<SearchFilterForUnconstrainedSearches>(target_sp);
}

StructuredData::ObjectSP
SearchFilterForUnconstrainedSearches::SerializeToStructuredData() {
  // The options dictionary is an empty dictionary:
  auto result_sp = std::make_shared<StructuredData::Dictionary>();
  return WrapOptionsDict(result_sp);
}

bool SearchFilterForUnconstrainedSearches::ModulePasses(
    const FileSpec &module_spec) {
  return !m_target_sp->ModuleIsExcludedForUnconstrainedSearches(module_spec);
}

bool SearchFilterForUnconstrainedSearches::ModulePasses(
    const lldb::ModuleSP &module_sp) {
  if (!module_sp)
    return true;
  else if (m_target_sp->ModuleIsExcludedForUnconstrainedSearches(module_sp))
    return false;
  return true;
}

SearchFilterSP SearchFilterForUnconstrainedSearches::DoCreateCopy() {
  return std::make_shared<SearchFilterForUnconstrainedSearches>(*this);
}

//  SearchFilterByModule:
//  Selects a shared library matching a given file spec

SearchFilterByModule::SearchFilterByModule(const lldb::TargetSP &target_sp,
                                           const FileSpec &module)
    : SearchFilter(target_sp, FilterTy::ByModule), m_module_spec(module) {}

SearchFilterByModule::~SearchFilterByModule() = default;

bool SearchFilterByModule::ModulePasses(const ModuleSP &module_sp) {
  return (module_sp &&
          FileSpec::Match(m_module_spec, module_sp->GetFileSpec()));
}

bool SearchFilterByModule::ModulePasses(const FileSpec &spec) {
  return FileSpec::Match(m_module_spec, spec);
}

bool SearchFilterByModule::AddressPasses(Address &address) {
  // FIXME: Not yet implemented
  return true;
}

void SearchFilterByModule::Search(Searcher &searcher) {
  if (!m_target_sp)
    return;

  if (searcher.GetDepth() == lldb::eSearchDepthTarget) {
    SymbolContext empty_sc;
    empty_sc.target_sp = m_target_sp;
    searcher.SearchCallback(*this, empty_sc, nullptr);
  }

  // If the module file spec is a full path, then we can just find the one
  // filespec that passes.  Otherwise, we need to go through all modules and
  // find the ones that match the file name.

  const ModuleList &target_modules = m_target_sp->GetImages();
  std::lock_guard<std::recursive_mutex> guard(target_modules.GetMutex());

  for (ModuleSP module_sp : m_target_sp->GetImages().Modules()) {
    if (FileSpec::Match(m_module_spec, module_sp->GetFileSpec())) {
      SymbolContext matchingContext(m_target_sp, module_sp);
      Searcher::CallbackReturn shouldContinue;

      shouldContinue = DoModuleIteration(matchingContext, searcher);
      if (shouldContinue == Searcher::eCallbackReturnStop)
        return;
    }
  }
}

void SearchFilterByModule::GetDescription(Stream *s) {
  s->PutCString(", module = ");
  s->PutCString(m_module_spec.GetFilename().AsCString("<Unknown>"));
}

uint32_t SearchFilterByModule::GetFilterRequiredItems() {
  return eSymbolContextModule;
}

void SearchFilterByModule::Dump(Stream *s) const {}

SearchFilterSP SearchFilterByModule::DoCreateCopy() {
  return std::make_shared<SearchFilterByModule>(*this);
}

SearchFilterSP SearchFilterByModule::CreateFromStructuredData(
    const lldb::TargetSP& target_sp,
    const StructuredData::Dictionary &data_dict,
    Status &error) {
  StructuredData::Array *modules_array;
  bool success = data_dict.GetValueForKeyAsArray(GetKey(OptionNames::ModList),
                                                 modules_array);
  if (!success) {
    error.SetErrorString("SFBM::CFSD: Could not find the module list key.");
    return nullptr;
  }

  size_t num_modules = modules_array->GetSize();
  if (num_modules > 1) {
    error.SetErrorString(
        "SFBM::CFSD: Only one modules allowed for SearchFilterByModule.");
    return nullptr;
  }

  std::optional<llvm::StringRef> maybe_module =
      modules_array->GetItemAtIndexAsString(0);
  if (!maybe_module) {
    error.SetErrorString("SFBM::CFSD: filter module item not a string.");
    return nullptr;
  }
  FileSpec module_spec(*maybe_module);

  return std::make_shared<SearchFilterByModule>(target_sp, module_spec);
}

StructuredData::ObjectSP SearchFilterByModule::SerializeToStructuredData() {
  auto options_dict_sp = std::make_shared<StructuredData::Dictionary>();
  auto module_array_sp = std::make_shared<StructuredData::Array>();
  module_array_sp->AddItem(
      std::make_shared<StructuredData::String>(m_module_spec.GetPath()));
  options_dict_sp->AddItem(GetKey(OptionNames::ModList), module_array_sp);
  return WrapOptionsDict(options_dict_sp);
}

//  SearchFilterByModuleList:
//  Selects a shared library matching a given file spec

SearchFilterByModuleList::SearchFilterByModuleList(
    const lldb::TargetSP &target_sp, const FileSpecList &module_list)
    : SearchFilter(target_sp, FilterTy::ByModules),
      m_module_spec_list(module_list) {}

SearchFilterByModuleList::SearchFilterByModuleList(
    const lldb::TargetSP &target_sp, const FileSpecList &module_list,
    enum FilterTy filter_ty)
    : SearchFilter(target_sp, filter_ty), m_module_spec_list(module_list) {}

SearchFilterByModuleList::~SearchFilterByModuleList() = default;

bool SearchFilterByModuleList::ModulePasses(const ModuleSP &module_sp) {
  if (m_module_spec_list.GetSize() == 0)
    return true;

  return module_sp && m_module_spec_list.FindFileIndex(
                          0, module_sp->GetFileSpec(), false) != UINT32_MAX;
}

bool SearchFilterByModuleList::ModulePasses(const FileSpec &spec) {
  if (m_module_spec_list.GetSize() == 0)
    return true;

  return m_module_spec_list.FindFileIndex(0, spec, true) != UINT32_MAX;
}

bool SearchFilterByModuleList::AddressPasses(Address &address) {
  // FIXME: Not yet implemented
  return true;
}

void SearchFilterByModuleList::Search(Searcher &searcher) {
  if (!m_target_sp)
    return;

  if (searcher.GetDepth() == lldb::eSearchDepthTarget) {
    SymbolContext empty_sc;
    empty_sc.target_sp = m_target_sp;
    searcher.SearchCallback(*this, empty_sc, nullptr);
  }

  // If the module file spec is a full path, then we can just find the one
  // filespec that passes.  Otherwise, we need to go through all modules and
  // find the ones that match the file name.
  for (ModuleSP module_sp : m_target_sp->GetImages().Modules()) {
    if (m_module_spec_list.FindFileIndex(0, module_sp->GetFileSpec(), false) ==
        UINT32_MAX)
      continue;
    SymbolContext matchingContext(m_target_sp, module_sp);
    Searcher::CallbackReturn shouldContinue;

    shouldContinue = DoModuleIteration(matchingContext, searcher);
    if (shouldContinue == Searcher::eCallbackReturnStop)
      return;
  }
}

void SearchFilterByModuleList::GetDescription(Stream *s) {
  size_t num_modules = m_module_spec_list.GetSize();
  if (num_modules == 1) {
    s->Printf(", module = ");
    s->PutCString(
        m_module_spec_list.GetFileSpecAtIndex(0).GetFilename().AsCString(
            "<Unknown>"));
    return;
  }

  s->Printf(", modules(%" PRIu64 ") = ", (uint64_t)num_modules);
  for (size_t i = 0; i < num_modules; i++) {
    s->PutCString(
        m_module_spec_list.GetFileSpecAtIndex(i).GetFilename().AsCString(
            "<Unknown>"));
    if (i != num_modules - 1)
      s->PutCString(", ");
  }
}

uint32_t SearchFilterByModuleList::GetFilterRequiredItems() {
  return eSymbolContextModule;
}

void SearchFilterByModuleList::Dump(Stream *s) const {}

lldb::SearchFilterSP SearchFilterByModuleList::DoCreateCopy() {
  return std::make_shared<SearchFilterByModuleList>(*this);
}

SearchFilterSP SearchFilterByModuleList::CreateFromStructuredData(
    const lldb::TargetSP& target_sp,
    const StructuredData::Dictionary &data_dict,
    Status &error) {
  StructuredData::Array *modules_array;
  bool success = data_dict.GetValueForKeyAsArray(GetKey(OptionNames::ModList),
                                                 modules_array);

  if (!success)
    return std::make_shared<SearchFilterByModuleList>(target_sp,
                                                      FileSpecList{});
  FileSpecList modules;
  size_t num_modules = modules_array->GetSize();
  for (size_t i = 0; i < num_modules; i++) {
    std::optional<llvm::StringRef> maybe_module =
        modules_array->GetItemAtIndexAsString(i);
    if (!maybe_module) {
      error.SetErrorStringWithFormat(
          "SFBM::CFSD: filter module item %zu not a string.", i);
      return nullptr;
    }
    modules.EmplaceBack(*maybe_module);
  }
  return std::make_shared<SearchFilterByModuleList>(target_sp, modules);
}

void SearchFilterByModuleList::SerializeUnwrapped(
    StructuredData::DictionarySP &options_dict_sp) {
  SerializeFileSpecList(options_dict_sp, OptionNames::ModList,
                        m_module_spec_list);
}

StructuredData::ObjectSP SearchFilterByModuleList::SerializeToStructuredData() {
  auto options_dict_sp = std::make_shared<StructuredData::Dictionary>();
  SerializeUnwrapped(options_dict_sp);
  return WrapOptionsDict(options_dict_sp);
}

//  SearchFilterByModuleListAndCU:
//  Selects a shared library matching a given file spec

SearchFilterByModuleListAndCU::SearchFilterByModuleListAndCU(
    const lldb::TargetSP &target_sp, const FileSpecList &module_list,
    const FileSpecList &cu_list)
    : SearchFilterByModuleList(target_sp, module_list,
                               FilterTy::ByModulesAndCU),
      m_cu_spec_list(cu_list) {}

SearchFilterByModuleListAndCU::~SearchFilterByModuleListAndCU() = default;

lldb::SearchFilterSP SearchFilterByModuleListAndCU::CreateFromStructuredData(
    const lldb::TargetSP& target_sp,
    const StructuredData::Dictionary &data_dict,
    Status &error) {
  StructuredData::Array *modules_array = nullptr;
  SearchFilterSP result_sp;
  bool success = data_dict.GetValueForKeyAsArray(GetKey(OptionNames::ModList),
                                                 modules_array);
  FileSpecList modules;
  if (success) {
    size_t num_modules = modules_array->GetSize();
    for (size_t i = 0; i < num_modules; i++) {
      std::optional<llvm::StringRef> maybe_module =
          modules_array->GetItemAtIndexAsString(i);
      if (!maybe_module) {
        error.SetErrorStringWithFormat(
            "SFBM::CFSD: filter module item %zu not a string.", i);
        return result_sp;
      }
      modules.EmplaceBack(*maybe_module);
    }
  }

  StructuredData::Array *cus_array = nullptr;
  success =
      data_dict.GetValueForKeyAsArray(GetKey(OptionNames::CUList), cus_array);
  if (!success) {
    error.SetErrorString("SFBM::CFSD: Could not find the CU list key.");
    return result_sp;
  }

  size_t num_cus = cus_array->GetSize();
  FileSpecList cus;
  for (size_t i = 0; i < num_cus; i++) {
    std::optional<llvm::StringRef> maybe_cu =
        cus_array->GetItemAtIndexAsString(i);
    if (!maybe_cu) {
      error.SetErrorStringWithFormat(
          "SFBM::CFSD: filter CU item %zu not a string.", i);
      return nullptr;
    }
    cus.EmplaceBack(*maybe_cu);
  }

  return std::make_shared<SearchFilterByModuleListAndCU>(
      target_sp, modules, cus);
}

StructuredData::ObjectSP
SearchFilterByModuleListAndCU::SerializeToStructuredData() {
  auto options_dict_sp = std::make_shared<StructuredData::Dictionary>();
  SearchFilterByModuleList::SerializeUnwrapped(options_dict_sp);
  SerializeFileSpecList(options_dict_sp, OptionNames::CUList, m_cu_spec_list);
  return WrapOptionsDict(options_dict_sp);
}

bool SearchFilterByModuleListAndCU::AddressPasses(Address &address) {
  SymbolContext sym_ctx;
  address.CalculateSymbolContext(&sym_ctx, eSymbolContextEverything);
  if (!sym_ctx.comp_unit) {
    if (m_cu_spec_list.GetSize() != 0)
      return false; // Has no comp_unit so can't pass the file check.
  }
  FileSpec cu_spec;
  if (sym_ctx.comp_unit)
    cu_spec = sym_ctx.comp_unit->GetPrimaryFile();
  if (m_cu_spec_list.FindFileIndex(0, cu_spec, false) == UINT32_MAX)
    return false; // Fails the file check
  return SearchFilterByModuleList::ModulePasses(sym_ctx.module_sp); 
}

bool SearchFilterByModuleListAndCU::CompUnitPasses(FileSpec &fileSpec) {
  return m_cu_spec_list.FindFileIndex(0, fileSpec, false) != UINT32_MAX;
}

bool SearchFilterByModuleListAndCU::CompUnitPasses(CompileUnit &compUnit) {
  bool in_cu_list = m_cu_spec_list.FindFileIndex(0, compUnit.GetPrimaryFile(),
                                                 false) != UINT32_MAX;
  if (!in_cu_list)
    return false;

  ModuleSP module_sp(compUnit.GetModule());
  if (!module_sp)
    return true;

  return SearchFilterByModuleList::ModulePasses(module_sp);
}

void SearchFilterByModuleListAndCU::Search(Searcher &searcher) {
  if (!m_target_sp)
    return;

  if (searcher.GetDepth() == lldb::eSearchDepthTarget) {
    SymbolContext empty_sc;
    empty_sc.target_sp = m_target_sp;
    searcher.SearchCallback(*this, empty_sc, nullptr);
  }

  // If the module file spec is a full path, then we can just find the one
  // filespec that passes.  Otherwise, we need to go through all modules and
  // find the ones that match the file name.

  ModuleList matching_modules;

  bool no_modules_in_filter = m_module_spec_list.GetSize() == 0;
  for (ModuleSP module_sp : m_target_sp->GetImages().Modules()) {
    if (!no_modules_in_filter &&
        m_module_spec_list.FindFileIndex(0, module_sp->GetFileSpec(), false) ==
            UINT32_MAX)
      continue;

    SymbolContext matchingContext(m_target_sp, module_sp);
    Searcher::CallbackReturn shouldContinue;

    if (searcher.GetDepth() == lldb::eSearchDepthModule) {
      shouldContinue = DoModuleIteration(matchingContext, searcher);
      if (shouldContinue == Searcher::eCallbackReturnStop)
        return;
      continue;
    }

    const size_t num_cu = module_sp->GetNumCompileUnits();
    for (size_t cu_idx = 0; cu_idx < num_cu; cu_idx++) {
      CompUnitSP cu_sp = module_sp->GetCompileUnitAtIndex(cu_idx);
      matchingContext.comp_unit = cu_sp.get();
      if (!matchingContext.comp_unit)
        continue;
      if (m_cu_spec_list.FindFileIndex(
              0, matchingContext.comp_unit->GetPrimaryFile(), false) ==
          UINT32_MAX)
        continue;
      shouldContinue = DoCUIteration(module_sp, matchingContext, searcher);
      if (shouldContinue == Searcher::eCallbackReturnStop)
        return;
    }
  }
}

void SearchFilterByModuleListAndCU::GetDescription(Stream *s) {
  size_t num_modules = m_module_spec_list.GetSize();
  if (num_modules == 1) {
    s->Printf(", module = ");
    s->PutCString(
        m_module_spec_list.GetFileSpecAtIndex(0).GetFilename().AsCString(
            "<Unknown>"));
  } else if (num_modules > 0) {
    s->Printf(", modules(%" PRIu64 ") = ", static_cast<uint64_t>(num_modules));
    for (size_t i = 0; i < num_modules; i++) {
      s->PutCString(
          m_module_spec_list.GetFileSpecAtIndex(i).GetFilename().AsCString(
              "<Unknown>"));
      if (i != num_modules - 1)
        s->PutCString(", ");
    }
  }
}

uint32_t SearchFilterByModuleListAndCU::GetFilterRequiredItems() {
  return eSymbolContextModule | eSymbolContextCompUnit;
}

void SearchFilterByModuleListAndCU::Dump(Stream *s) const {}

SearchFilterSP SearchFilterByModuleListAndCU::DoCreateCopy() {
  return std::make_shared<SearchFilterByModuleListAndCU>(*this);
}
