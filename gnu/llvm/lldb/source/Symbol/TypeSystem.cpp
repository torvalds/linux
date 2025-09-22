//===-- TypeSystem.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/Language.h"

#include "llvm/ADT/DenseSet.h"
#include <optional>

using namespace lldb_private;
using namespace lldb;

/// A 64-bit SmallBitVector is only small up to 64-7 bits, and the
/// setBitsInMask interface wants to write full bytes.
static const size_t g_num_small_bitvector_bits = 64 - 8;
static_assert(eNumLanguageTypes < g_num_small_bitvector_bits,
              "Languages bit vector is no longer small on 64 bit systems");
LanguageSet::LanguageSet() : bitvector(eNumLanguageTypes, false) {}

std::optional<LanguageType> LanguageSet::GetSingularLanguage() {
  if (bitvector.count() == 1)
    return (LanguageType)bitvector.find_first();
  return {};
}

void LanguageSet::Insert(LanguageType language) { bitvector.set(language); }
size_t LanguageSet::Size() const { return bitvector.count(); }
bool LanguageSet::Empty() const { return bitvector.none(); }
bool LanguageSet::operator[](unsigned i) const { return bitvector[i]; }

TypeSystem::TypeSystem() = default;
TypeSystem::~TypeSystem() = default;

static TypeSystemSP CreateInstanceHelper(lldb::LanguageType language,
                                         Module *module, Target *target) {
  uint32_t i = 0;
  TypeSystemCreateInstance create_callback;
  while ((create_callback = PluginManager::GetTypeSystemCreateCallbackAtIndex(
              i++)) != nullptr) {
    if (auto type_system_sp = create_callback(language, module, target))
      return type_system_sp;
  }

  return {};
}

lldb::TypeSystemSP TypeSystem::CreateInstance(lldb::LanguageType language,
                                              Module *module) {
  return CreateInstanceHelper(language, module, nullptr);
}

lldb::TypeSystemSP TypeSystem::CreateInstance(lldb::LanguageType language,
                                              Target *target) {
  return CreateInstanceHelper(language, nullptr, target);
}

#ifndef NDEBUG
bool TypeSystem::Verify(lldb::opaque_compiler_type_t type) { return true; }
#endif

bool TypeSystem::IsAnonymousType(lldb::opaque_compiler_type_t type) {
  return false;
}

CompilerType TypeSystem::GetArrayType(lldb::opaque_compiler_type_t type,
                                      uint64_t size) {
  return CompilerType();
}

CompilerType
TypeSystem::GetLValueReferenceType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType
TypeSystem::GetRValueReferenceType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType TypeSystem::GetAtomicType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType TypeSystem::AddConstModifier(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType TypeSystem::AddPtrAuthModifier(lldb::opaque_compiler_type_t type,
                                            uint32_t payload) {
  return CompilerType();
}

CompilerType
TypeSystem::AddVolatileModifier(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType
TypeSystem::AddRestrictModifier(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType TypeSystem::CreateTypedef(lldb::opaque_compiler_type_t type,
                                       const char *name,
                                       const CompilerDeclContext &decl_ctx,
                                       uint32_t opaque_payload) {
  return CompilerType();
}

CompilerType TypeSystem::GetBuiltinTypeByName(ConstString name) {
  return CompilerType();
}

CompilerType TypeSystem::GetTypeForFormatters(void *type) {
  return CompilerType(weak_from_this(), type);
}

bool TypeSystem::IsTemplateType(lldb::opaque_compiler_type_t type) {
  return false;
}

size_t TypeSystem::GetNumTemplateArguments(lldb::opaque_compiler_type_t type,
                                           bool expand_pack) {
  return 0;
}

TemplateArgumentKind
TypeSystem::GetTemplateArgumentKind(opaque_compiler_type_t type, size_t idx,
                                    bool expand_pack) {
  return eTemplateArgumentKindNull;
}

CompilerType TypeSystem::GetTypeTemplateArgument(opaque_compiler_type_t type,
                                                 size_t idx, bool expand_pack) {
  return CompilerType();
}

std::optional<CompilerType::IntegralTemplateArgument>
TypeSystem::GetIntegralTemplateArgument(opaque_compiler_type_t type, size_t idx,
                                        bool expand_pack) {
  return std::nullopt;
}

LazyBool TypeSystem::ShouldPrintAsOneLiner(void *type, ValueObject *valobj) {
  return eLazyBoolCalculate;
}

bool TypeSystem::IsMeaninglessWithoutDynamicResolution(void *type) {
  return false;
}

ConstString TypeSystem::DeclGetMangledName(void *opaque_decl) {
  return ConstString();
}

CompilerDeclContext TypeSystem::DeclGetDeclContext(void *opaque_decl) {
  return CompilerDeclContext();
}

CompilerType TypeSystem::DeclGetFunctionReturnType(void *opaque_decl) {
  return CompilerType();
}

size_t TypeSystem::DeclGetFunctionNumArguments(void *opaque_decl) { return 0; }

CompilerType TypeSystem::DeclGetFunctionArgumentType(void *opaque_decl,
                                                     size_t arg_idx) {
  return CompilerType();
}

std::vector<lldb_private::CompilerContext>
TypeSystem::DeclGetCompilerContext(void *opaque_decl) {
  return {};
}

std::vector<lldb_private::CompilerContext>
TypeSystem::DeclContextGetCompilerContext(void *opaque_decl_ctx) {
  return {};
}

std::vector<CompilerDecl>
TypeSystem::DeclContextFindDeclByName(void *opaque_decl_ctx, ConstString name,
                                      bool ignore_imported_decls) {
  return std::vector<CompilerDecl>();
}

std::unique_ptr<UtilityFunction>
TypeSystem::CreateUtilityFunction(std::string text, std::string name) {
  return {};
}

std::optional<llvm::json::Value> TypeSystem::ReportStatistics() {
  return std::nullopt;
}

CompilerDeclContext
TypeSystem::GetCompilerDeclContextForType(const CompilerType &type) {
  return CompilerDeclContext();
}

#pragma mark TypeSystemMap

TypeSystemMap::TypeSystemMap() : m_mutex(), m_map() {}

TypeSystemMap::~TypeSystemMap() = default;

void TypeSystemMap::Clear() {
  collection map;
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    map = m_map;
    m_clear_in_progress = true;
  }
  llvm::DenseSet<TypeSystem *> visited;
  for (auto &pair : map) {
    if (visited.count(pair.second.get()))
      continue;
    visited.insert(pair.second.get());
    if (lldb::TypeSystemSP type_system = pair.second)
      type_system->Finalize();
  }
  map.clear();
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_map.clear();
    m_clear_in_progress = false;
  }
}

void TypeSystemMap::ForEach(
    std::function<bool(lldb::TypeSystemSP)> const &callback) {

  // The callback may call into this function again causing
  // us to lock m_mutex twice if we held it across the callback.
  // Since we just care about guarding access to 'm_map', make
  // a local copy and iterate over that instead.
  collection map_snapshot;
  {
      std::lock_guard<std::mutex> guard(m_mutex);
      map_snapshot = m_map;
  }

  // Use a std::set so we only call the callback once for each unique
  // TypeSystem instance.
  llvm::DenseSet<TypeSystem *> visited;
  for (auto &pair : map_snapshot) {
    TypeSystem *type_system = pair.second.get();
    if (!type_system || visited.count(type_system))
      continue;
    visited.insert(type_system);
    assert(type_system);
    if (!callback(pair.second))
      break;
  }
}

llvm::Expected<lldb::TypeSystemSP> TypeSystemMap::GetTypeSystemForLanguage(
    lldb::LanguageType language,
    std::optional<CreateCallback> create_callback) {
  std::lock_guard<std::mutex> guard(m_mutex);
  if (m_clear_in_progress)
    return llvm::createStringError(
        "Unable to get TypeSystem because TypeSystemMap is being cleared");

  collection::iterator pos = m_map.find(language);
  if (pos != m_map.end()) {
    if (pos->second) {
      assert(!pos->second->weak_from_this().expired());
      return pos->second;
    }
    return llvm::createStringError(
        "TypeSystem for language " +
        llvm::StringRef(Language::GetNameForLanguageType(language)) +
        " doesn't exist");
  }

  for (const auto &pair : m_map) {
    if (pair.second && pair.second->SupportsLanguage(language)) {
      // Add a new mapping for "language" to point to an already existing
      // TypeSystem that supports this language
      m_map[language] = pair.second;
      if (pair.second)
        return pair.second;
      return llvm::createStringError(
          "TypeSystem for language " +
          llvm::StringRef(Language::GetNameForLanguageType(language)) +
          " doesn't exist");
    }
  }

  if (!create_callback)
    return llvm::createStringError(
        "Unable to find type system for language " +
        llvm::StringRef(Language::GetNameForLanguageType(language)));
  // Cache even if we get a shared pointer that contains a null type system
  // back.
  TypeSystemSP type_system_sp = (*create_callback)();
  m_map[language] = type_system_sp;
  if (type_system_sp)
    return type_system_sp;
  return llvm::createStringError(
      "TypeSystem for language " +
      llvm::StringRef(Language::GetNameForLanguageType(language)) +
      " doesn't exist");
}

llvm::Expected<lldb::TypeSystemSP>
TypeSystemMap::GetTypeSystemForLanguage(lldb::LanguageType language,
                                        Module *module, bool can_create) {
  if (can_create) {
    return GetTypeSystemForLanguage(
        language, std::optional<CreateCallback>([language, module]() {
          return TypeSystem::CreateInstance(language, module);
        }));
  }
  return GetTypeSystemForLanguage(language);
}

llvm::Expected<lldb::TypeSystemSP>
TypeSystemMap::GetTypeSystemForLanguage(lldb::LanguageType language,
                                        Target *target, bool can_create) {
  if (can_create) {
    return GetTypeSystemForLanguage(
        language, std::optional<CreateCallback>([language, target]() {
          return TypeSystem::CreateInstance(language, target);
        }));
  }
  return GetTypeSystemForLanguage(language);
}

bool TypeSystem::SupportsLanguageStatic(lldb::LanguageType language) {
  if (language == eLanguageTypeUnknown || language >= eNumLanguageTypes)
    return false;

  LanguageSet languages =
      PluginManager::GetAllTypeSystemSupportedLanguagesForTypes();
  if (languages.Empty())
    return false;
  return languages[language];
}
