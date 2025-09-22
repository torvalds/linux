//===-- Symtab.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <map>
#include <set>

#include "lldb/Core/DataFileCache.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/RichManglingContext.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/Timer.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DJB.h"

using namespace lldb;
using namespace lldb_private;

Symtab::Symtab(ObjectFile *objfile)
    : m_objfile(objfile), m_symbols(), m_file_addr_to_index(*this),
      m_name_to_symbol_indices(), m_mutex(),
      m_file_addr_to_index_computed(false), m_name_indexes_computed(false),
      m_loaded_from_cache(false), m_saved_to_cache(false) {
  m_name_to_symbol_indices.emplace(std::make_pair(
      lldb::eFunctionNameTypeNone, UniqueCStringMap<uint32_t>()));
  m_name_to_symbol_indices.emplace(std::make_pair(
      lldb::eFunctionNameTypeBase, UniqueCStringMap<uint32_t>()));
  m_name_to_symbol_indices.emplace(std::make_pair(
      lldb::eFunctionNameTypeMethod, UniqueCStringMap<uint32_t>()));
  m_name_to_symbol_indices.emplace(std::make_pair(
      lldb::eFunctionNameTypeSelector, UniqueCStringMap<uint32_t>()));
}

Symtab::~Symtab() = default;

void Symtab::Reserve(size_t count) {
  // Clients should grab the mutex from this symbol table and lock it manually
  // when calling this function to avoid performance issues.
  m_symbols.reserve(count);
}

Symbol *Symtab::Resize(size_t count) {
  // Clients should grab the mutex from this symbol table and lock it manually
  // when calling this function to avoid performance issues.
  m_symbols.resize(count);
  return m_symbols.empty() ? nullptr : &m_symbols[0];
}

uint32_t Symtab::AddSymbol(const Symbol &symbol) {
  // Clients should grab the mutex from this symbol table and lock it manually
  // when calling this function to avoid performance issues.
  uint32_t symbol_idx = m_symbols.size();
  auto &name_to_index = GetNameToSymbolIndexMap(lldb::eFunctionNameTypeNone);
  name_to_index.Clear();
  m_file_addr_to_index.Clear();
  m_symbols.push_back(symbol);
  m_file_addr_to_index_computed = false;
  m_name_indexes_computed = false;
  return symbol_idx;
}

size_t Symtab::GetNumSymbols() const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  return m_symbols.size();
}

void Symtab::SectionFileAddressesChanged() {
  m_file_addr_to_index.Clear();
  m_file_addr_to_index_computed = false;
}

void Symtab::Dump(Stream *s, Target *target, SortOrder sort_order,
                  Mangled::NamePreference name_preference) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  //    s->Printf("%.*p: ", (int)sizeof(void*) * 2, this);
  s->Indent();
  const FileSpec &file_spec = m_objfile->GetFileSpec();
  const char *object_name = nullptr;
  if (m_objfile->GetModule())
    object_name = m_objfile->GetModule()->GetObjectName().GetCString();

  if (file_spec)
    s->Printf("Symtab, file = %s%s%s%s, num_symbols = %" PRIu64,
              file_spec.GetPath().c_str(), object_name ? "(" : "",
              object_name ? object_name : "", object_name ? ")" : "",
              (uint64_t)m_symbols.size());
  else
    s->Printf("Symtab, num_symbols = %" PRIu64 "", (uint64_t)m_symbols.size());

  if (!m_symbols.empty()) {
    switch (sort_order) {
    case eSortOrderNone: {
      s->PutCString(":\n");
      DumpSymbolHeader(s);
      const_iterator begin = m_symbols.begin();
      const_iterator end = m_symbols.end();
      for (const_iterator pos = m_symbols.begin(); pos != end; ++pos) {
        s->Indent();
        pos->Dump(s, target, std::distance(begin, pos), name_preference);
      }
    }
    break;

    case eSortOrderByName: {
      // Although we maintain a lookup by exact name map, the table isn't
      // sorted by name. So we must make the ordered symbol list up ourselves.
      s->PutCString(" (sorted by name):\n");
      DumpSymbolHeader(s);

      std::multimap<llvm::StringRef, const Symbol *> name_map;
      for (const Symbol &symbol : m_symbols)
        name_map.emplace(symbol.GetName().GetStringRef(), &symbol);

      for (const auto &name_to_symbol : name_map) {
        const Symbol *symbol = name_to_symbol.second;
        s->Indent();
        symbol->Dump(s, target, symbol - &m_symbols[0], name_preference);
      }
    } break;

    case eSortOrderBySize: {
      s->PutCString(" (sorted by size):\n");
      DumpSymbolHeader(s);

      std::multimap<size_t, const Symbol *, std::greater<size_t>> size_map;
      for (const Symbol &symbol : m_symbols)
        size_map.emplace(symbol.GetByteSize(), &symbol);

      size_t idx = 0;
      for (const auto &size_to_symbol : size_map) {
        const Symbol *symbol = size_to_symbol.second;
        s->Indent();
        symbol->Dump(s, target, idx++, name_preference);
      }
    } break;

    case eSortOrderByAddress:
      s->PutCString(" (sorted by address):\n");
      DumpSymbolHeader(s);
      if (!m_file_addr_to_index_computed)
        InitAddressIndexes();
      const size_t num_entries = m_file_addr_to_index.GetSize();
      for (size_t i = 0; i < num_entries; ++i) {
        s->Indent();
        const uint32_t symbol_idx = m_file_addr_to_index.GetEntryRef(i).data;
        m_symbols[symbol_idx].Dump(s, target, symbol_idx, name_preference);
      }
      break;
    }
  } else {
    s->PutCString("\n");
  }
}

void Symtab::Dump(Stream *s, Target *target, std::vector<uint32_t> &indexes,
                  Mangled::NamePreference name_preference) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  const size_t num_symbols = GetNumSymbols();
  // s->Printf("%.*p: ", (int)sizeof(void*) * 2, this);
  s->Indent();
  s->Printf("Symtab %" PRIu64 " symbol indexes (%" PRIu64 " symbols total):\n",
            (uint64_t)indexes.size(), (uint64_t)m_symbols.size());
  s->IndentMore();

  if (!indexes.empty()) {
    std::vector<uint32_t>::const_iterator pos;
    std::vector<uint32_t>::const_iterator end = indexes.end();
    DumpSymbolHeader(s);
    for (pos = indexes.begin(); pos != end; ++pos) {
      size_t idx = *pos;
      if (idx < num_symbols) {
        s->Indent();
        m_symbols[idx].Dump(s, target, idx, name_preference);
      }
    }
  }
  s->IndentLess();
}

void Symtab::DumpSymbolHeader(Stream *s) {
  s->Indent("               Debug symbol\n");
  s->Indent("               |Synthetic symbol\n");
  s->Indent("               ||Externally Visible\n");
  s->Indent("               |||\n");
  s->Indent("Index   UserID DSX Type            File Address/Value Load "
            "Address       Size               Flags      Name\n");
  s->Indent("------- ------ --- --------------- ------------------ "
            "------------------ ------------------ ---------- "
            "----------------------------------\n");
}

static int CompareSymbolID(const void *key, const void *p) {
  const user_id_t match_uid = *(const user_id_t *)key;
  const user_id_t symbol_uid = ((const Symbol *)p)->GetID();
  if (match_uid < symbol_uid)
    return -1;
  if (match_uid > symbol_uid)
    return 1;
  return 0;
}

Symbol *Symtab::FindSymbolByID(lldb::user_id_t symbol_uid) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  Symbol *symbol =
      (Symbol *)::bsearch(&symbol_uid, &m_symbols[0], m_symbols.size(),
                          sizeof(m_symbols[0]), CompareSymbolID);
  return symbol;
}

Symbol *Symtab::SymbolAtIndex(size_t idx) {
  // Clients should grab the mutex from this symbol table and lock it manually
  // when calling this function to avoid performance issues.
  if (idx < m_symbols.size())
    return &m_symbols[idx];
  return nullptr;
}

const Symbol *Symtab::SymbolAtIndex(size_t idx) const {
  // Clients should grab the mutex from this symbol table and lock it manually
  // when calling this function to avoid performance issues.
  if (idx < m_symbols.size())
    return &m_symbols[idx];
  return nullptr;
}

static bool lldb_skip_name(llvm::StringRef mangled,
                           Mangled::ManglingScheme scheme) {
  switch (scheme) {
  case Mangled::eManglingSchemeItanium: {
    if (mangled.size() < 3 || !mangled.starts_with("_Z"))
      return true;

    // Avoid the following types of symbols in the index.
    switch (mangled[2]) {
    case 'G': // guard variables
    case 'T': // virtual tables, VTT structures, typeinfo structures + names
    case 'Z': // named local entities (if we eventually handle
              // eSymbolTypeData, we will want this back)
      return true;

    default:
      break;
    }

    // Include this name in the index.
    return false;
  }

  // No filters for this scheme yet. Include all names in indexing.
  case Mangled::eManglingSchemeMSVC:
  case Mangled::eManglingSchemeRustV0:
  case Mangled::eManglingSchemeD:
  case Mangled::eManglingSchemeSwift:
    return false;

  // Don't try and demangle things we can't categorize.
  case Mangled::eManglingSchemeNone:
    return true;
  }
  llvm_unreachable("unknown scheme!");
}

void Symtab::InitNameIndexes() {
  // Protected function, no need to lock mutex...
  if (!m_name_indexes_computed) {
    m_name_indexes_computed = true;
    ElapsedTime elapsed(m_objfile->GetModule()->GetSymtabIndexTime());
    LLDB_SCOPED_TIMER();

    // Collect all loaded language plugins.
    std::vector<Language *> languages;
    Language::ForEach([&languages](Language *l) {
      languages.push_back(l);
      return true;
    });

    auto &name_to_index = GetNameToSymbolIndexMap(lldb::eFunctionNameTypeNone);
    auto &basename_to_index =
        GetNameToSymbolIndexMap(lldb::eFunctionNameTypeBase);
    auto &method_to_index =
        GetNameToSymbolIndexMap(lldb::eFunctionNameTypeMethod);
    auto &selector_to_index =
        GetNameToSymbolIndexMap(lldb::eFunctionNameTypeSelector);
    // Create the name index vector to be able to quickly search by name
    const size_t num_symbols = m_symbols.size();
    name_to_index.Reserve(num_symbols);

    // The "const char *" in "class_contexts" and backlog::value_type::second
    // must come from a ConstString::GetCString()
    std::set<const char *> class_contexts;
    std::vector<std::pair<NameToIndexMap::Entry, const char *>> backlog;
    backlog.reserve(num_symbols / 2);

    // Instantiation of the demangler is expensive, so better use a single one
    // for all entries during batch processing.
    RichManglingContext rmc;
    for (uint32_t value = 0; value < num_symbols; ++value) {
      Symbol *symbol = &m_symbols[value];

      // Don't let trampolines get into the lookup by name map If we ever need
      // the trampoline symbols to be searchable by name we can remove this and
      // then possibly add a new bool to any of the Symtab functions that
      // lookup symbols by name to indicate if they want trampolines. We also
      // don't want any synthetic symbols with auto generated names in the
      // name lookups.
      if (symbol->IsTrampoline() || symbol->IsSyntheticWithAutoGeneratedName())
        continue;

      // If the symbol's name string matched a Mangled::ManglingScheme, it is
      // stored in the mangled field.
      Mangled &mangled = symbol->GetMangled();
      if (ConstString name = mangled.GetMangledName()) {
        name_to_index.Append(name, value);

        if (symbol->ContainsLinkerAnnotations()) {
          // If the symbol has linker annotations, also add the version without
          // the annotations.
          ConstString stripped = ConstString(
              m_objfile->StripLinkerSymbolAnnotations(name.GetStringRef()));
          name_to_index.Append(stripped, value);
        }

        const SymbolType type = symbol->GetType();
        if (type == eSymbolTypeCode || type == eSymbolTypeResolver) {
          if (mangled.GetRichManglingInfo(rmc, lldb_skip_name)) {
            RegisterMangledNameEntry(value, class_contexts, backlog, rmc);
            continue;
          }
        }
      }

      // Symbol name strings that didn't match a Mangled::ManglingScheme, are
      // stored in the demangled field.
      if (ConstString name = mangled.GetDemangledName()) {
        name_to_index.Append(name, value);

        if (symbol->ContainsLinkerAnnotations()) {
          // If the symbol has linker annotations, also add the version without
          // the annotations.
          name = ConstString(
              m_objfile->StripLinkerSymbolAnnotations(name.GetStringRef()));
          name_to_index.Append(name, value);
        }

        // If the demangled name turns out to be an ObjC name, and is a category
        // name, add the version without categories to the index too.
        for (Language *lang : languages) {
          for (auto variant : lang->GetMethodNameVariants(name)) {
            if (variant.GetType() & lldb::eFunctionNameTypeSelector)
              selector_to_index.Append(variant.GetName(), value);
            else if (variant.GetType() & lldb::eFunctionNameTypeFull)
              name_to_index.Append(variant.GetName(), value);
            else if (variant.GetType() & lldb::eFunctionNameTypeMethod)
              method_to_index.Append(variant.GetName(), value);
            else if (variant.GetType() & lldb::eFunctionNameTypeBase)
              basename_to_index.Append(variant.GetName(), value);
          }
        }
      }
    }

    for (const auto &record : backlog) {
      RegisterBacklogEntry(record.first, record.second, class_contexts);
    }

    name_to_index.Sort();
    name_to_index.SizeToFit();
    selector_to_index.Sort();
    selector_to_index.SizeToFit();
    basename_to_index.Sort();
    basename_to_index.SizeToFit();
    method_to_index.Sort();
    method_to_index.SizeToFit();
  }
}

void Symtab::RegisterMangledNameEntry(
    uint32_t value, std::set<const char *> &class_contexts,
    std::vector<std::pair<NameToIndexMap::Entry, const char *>> &backlog,
    RichManglingContext &rmc) {
  // Only register functions that have a base name.
  llvm::StringRef base_name = rmc.ParseFunctionBaseName();
  if (base_name.empty())
    return;

  // The base name will be our entry's name.
  NameToIndexMap::Entry entry(ConstString(base_name), value);
  llvm::StringRef decl_context = rmc.ParseFunctionDeclContextName();

  // Register functions with no context.
  if (decl_context.empty()) {
    // This has to be a basename
    auto &basename_to_index =
        GetNameToSymbolIndexMap(lldb::eFunctionNameTypeBase);
    basename_to_index.Append(entry);
    // If there is no context (no namespaces or class scopes that come before
    // the function name) then this also could be a fullname.
    auto &name_to_index = GetNameToSymbolIndexMap(lldb::eFunctionNameTypeNone);
    name_to_index.Append(entry);
    return;
  }

  // Make sure we have a pool-string pointer and see if we already know the
  // context name.
  const char *decl_context_ccstr = ConstString(decl_context).GetCString();
  auto it = class_contexts.find(decl_context_ccstr);

  auto &method_to_index =
      GetNameToSymbolIndexMap(lldb::eFunctionNameTypeMethod);
  // Register constructors and destructors. They are methods and create
  // declaration contexts.
  if (rmc.IsCtorOrDtor()) {
    method_to_index.Append(entry);
    if (it == class_contexts.end())
      class_contexts.insert(it, decl_context_ccstr);
    return;
  }

  // Register regular methods with a known declaration context.
  if (it != class_contexts.end()) {
    method_to_index.Append(entry);
    return;
  }

  // Regular methods in unknown declaration contexts are put to the backlog. We
  // will revisit them once we processed all remaining symbols.
  backlog.push_back(std::make_pair(entry, decl_context_ccstr));
}

void Symtab::RegisterBacklogEntry(
    const NameToIndexMap::Entry &entry, const char *decl_context,
    const std::set<const char *> &class_contexts) {
  auto &method_to_index =
      GetNameToSymbolIndexMap(lldb::eFunctionNameTypeMethod);
  auto it = class_contexts.find(decl_context);
  if (it != class_contexts.end()) {
    method_to_index.Append(entry);
  } else {
    // If we got here, we have something that had a context (was inside
    // a namespace or class) yet we don't know the entry
    method_to_index.Append(entry);
    auto &basename_to_index =
        GetNameToSymbolIndexMap(lldb::eFunctionNameTypeBase);
    basename_to_index.Append(entry);
  }
}

void Symtab::PreloadSymbols() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  InitNameIndexes();
}

void Symtab::AppendSymbolNamesToMap(const IndexCollection &indexes,
                                    bool add_demangled, bool add_mangled,
                                    NameToIndexMap &name_to_index_map) const {
  LLDB_SCOPED_TIMER();
  if (add_demangled || add_mangled) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);

    // Create the name index vector to be able to quickly search by name
    const size_t num_indexes = indexes.size();
    for (size_t i = 0; i < num_indexes; ++i) {
      uint32_t value = indexes[i];
      assert(i < m_symbols.size());
      const Symbol *symbol = &m_symbols[value];

      const Mangled &mangled = symbol->GetMangled();
      if (add_demangled) {
        if (ConstString name = mangled.GetDemangledName())
          name_to_index_map.Append(name, value);
      }

      if (add_mangled) {
        if (ConstString name = mangled.GetMangledName())
          name_to_index_map.Append(name, value);
      }
    }
  }
}

uint32_t Symtab::AppendSymbolIndexesWithType(SymbolType symbol_type,
                                             std::vector<uint32_t> &indexes,
                                             uint32_t start_idx,
                                             uint32_t end_index) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  uint32_t prev_size = indexes.size();

  const uint32_t count = std::min<uint32_t>(m_symbols.size(), end_index);

  for (uint32_t i = start_idx; i < count; ++i) {
    if (symbol_type == eSymbolTypeAny || m_symbols[i].GetType() == symbol_type)
      indexes.push_back(i);
  }

  return indexes.size() - prev_size;
}

uint32_t Symtab::AppendSymbolIndexesWithTypeAndFlagsValue(
    SymbolType symbol_type, uint32_t flags_value,
    std::vector<uint32_t> &indexes, uint32_t start_idx,
    uint32_t end_index) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  uint32_t prev_size = indexes.size();

  const uint32_t count = std::min<uint32_t>(m_symbols.size(), end_index);

  for (uint32_t i = start_idx; i < count; ++i) {
    if ((symbol_type == eSymbolTypeAny ||
         m_symbols[i].GetType() == symbol_type) &&
        m_symbols[i].GetFlags() == flags_value)
      indexes.push_back(i);
  }

  return indexes.size() - prev_size;
}

uint32_t Symtab::AppendSymbolIndexesWithType(SymbolType symbol_type,
                                             Debug symbol_debug_type,
                                             Visibility symbol_visibility,
                                             std::vector<uint32_t> &indexes,
                                             uint32_t start_idx,
                                             uint32_t end_index) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  uint32_t prev_size = indexes.size();

  const uint32_t count = std::min<uint32_t>(m_symbols.size(), end_index);

  for (uint32_t i = start_idx; i < count; ++i) {
    if (symbol_type == eSymbolTypeAny ||
        m_symbols[i].GetType() == symbol_type) {
      if (CheckSymbolAtIndex(i, symbol_debug_type, symbol_visibility))
        indexes.push_back(i);
    }
  }

  return indexes.size() - prev_size;
}

uint32_t Symtab::GetIndexForSymbol(const Symbol *symbol) const {
  if (!m_symbols.empty()) {
    const Symbol *first_symbol = &m_symbols[0];
    if (symbol >= first_symbol && symbol < first_symbol + m_symbols.size())
      return symbol - first_symbol;
  }
  return UINT32_MAX;
}

struct SymbolSortInfo {
  const bool sort_by_load_addr;
  const Symbol *symbols;
};

namespace {
struct SymbolIndexComparator {
  const std::vector<Symbol> &symbols;
  std::vector<lldb::addr_t> &addr_cache;

  // Getting from the symbol to the Address to the File Address involves some
  // work. Since there are potentially many symbols here, and we're using this
  // for sorting so we're going to be computing the address many times, cache
  // that in addr_cache. The array passed in has to be the same size as the
  // symbols array passed into the member variable symbols, and should be
  // initialized with LLDB_INVALID_ADDRESS.
  // NOTE: You have to make addr_cache externally and pass it in because
  // std::stable_sort
  // makes copies of the comparator it is initially passed in, and you end up
  // spending huge amounts of time copying this array...

  SymbolIndexComparator(const std::vector<Symbol> &s,
                        std::vector<lldb::addr_t> &a)
      : symbols(s), addr_cache(a) {
    assert(symbols.size() == addr_cache.size());
  }
  bool operator()(uint32_t index_a, uint32_t index_b) {
    addr_t value_a = addr_cache[index_a];
    if (value_a == LLDB_INVALID_ADDRESS) {
      value_a = symbols[index_a].GetAddressRef().GetFileAddress();
      addr_cache[index_a] = value_a;
    }

    addr_t value_b = addr_cache[index_b];
    if (value_b == LLDB_INVALID_ADDRESS) {
      value_b = symbols[index_b].GetAddressRef().GetFileAddress();
      addr_cache[index_b] = value_b;
    }

    if (value_a == value_b) {
      // The if the values are equal, use the original symbol user ID
      lldb::user_id_t uid_a = symbols[index_a].GetID();
      lldb::user_id_t uid_b = symbols[index_b].GetID();
      if (uid_a < uid_b)
        return true;
      if (uid_a > uid_b)
        return false;
      return false;
    } else if (value_a < value_b)
      return true;

    return false;
  }
};
}

void Symtab::SortSymbolIndexesByValue(std::vector<uint32_t> &indexes,
                                      bool remove_duplicates) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  LLDB_SCOPED_TIMER();
  // No need to sort if we have zero or one items...
  if (indexes.size() <= 1)
    return;

  // Sort the indexes in place using std::stable_sort.
  // NOTE: The use of std::stable_sort instead of llvm::sort here is strictly
  // for performance, not correctness.  The indexes vector tends to be "close"
  // to sorted, which the stable sort handles better.

  std::vector<lldb::addr_t> addr_cache(m_symbols.size(), LLDB_INVALID_ADDRESS);

  SymbolIndexComparator comparator(m_symbols, addr_cache);
  std::stable_sort(indexes.begin(), indexes.end(), comparator);

  // Remove any duplicates if requested
  if (remove_duplicates) {
    auto last = std::unique(indexes.begin(), indexes.end());
    indexes.erase(last, indexes.end());
  }
}

uint32_t Symtab::GetNameIndexes(ConstString symbol_name,
                                std::vector<uint32_t> &indexes) {
  auto &name_to_index = GetNameToSymbolIndexMap(lldb::eFunctionNameTypeNone);
  const uint32_t count = name_to_index.GetValues(symbol_name, indexes);
  if (count)
    return count;
  // Synthetic symbol names are not added to the name indexes, but they start
  // with a prefix and end with a the symbol UserID. This allows users to find
  // these symbols without having to add them to the name indexes. These
  // queries will not happen very often since the names don't mean anything, so
  // performance is not paramount in this case.
  llvm::StringRef name = symbol_name.GetStringRef();
  // String the synthetic prefix if the name starts with it.
  if (!name.consume_front(Symbol::GetSyntheticSymbolPrefix()))
    return 0; // Not a synthetic symbol name

  // Extract the user ID from the symbol name
  unsigned long long uid = 0;
  if (getAsUnsignedInteger(name, /*Radix=*/10, uid))
    return 0; // Failed to extract the user ID as an integer
  Symbol *symbol = FindSymbolByID(uid);
  if (symbol == nullptr)
    return 0;
  const uint32_t symbol_idx = GetIndexForSymbol(symbol);
  if (symbol_idx == UINT32_MAX)
    return 0;
  indexes.push_back(symbol_idx);
  return 1;
}

uint32_t Symtab::AppendSymbolIndexesWithName(ConstString symbol_name,
                                             std::vector<uint32_t> &indexes) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (symbol_name) {
    if (!m_name_indexes_computed)
      InitNameIndexes();

    return GetNameIndexes(symbol_name, indexes);
  }
  return 0;
}

uint32_t Symtab::AppendSymbolIndexesWithName(ConstString symbol_name,
                                             Debug symbol_debug_type,
                                             Visibility symbol_visibility,
                                             std::vector<uint32_t> &indexes) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  LLDB_SCOPED_TIMER();
  if (symbol_name) {
    const size_t old_size = indexes.size();
    if (!m_name_indexes_computed)
      InitNameIndexes();

    std::vector<uint32_t> all_name_indexes;
    const size_t name_match_count =
        GetNameIndexes(symbol_name, all_name_indexes);
    for (size_t i = 0; i < name_match_count; ++i) {
      if (CheckSymbolAtIndex(all_name_indexes[i], symbol_debug_type,
                             symbol_visibility))
        indexes.push_back(all_name_indexes[i]);
    }
    return indexes.size() - old_size;
  }
  return 0;
}

uint32_t
Symtab::AppendSymbolIndexesWithNameAndType(ConstString symbol_name,
                                           SymbolType symbol_type,
                                           std::vector<uint32_t> &indexes) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (AppendSymbolIndexesWithName(symbol_name, indexes) > 0) {
    std::vector<uint32_t>::iterator pos = indexes.begin();
    while (pos != indexes.end()) {
      if (symbol_type == eSymbolTypeAny ||
          m_symbols[*pos].GetType() == symbol_type)
        ++pos;
      else
        pos = indexes.erase(pos);
    }
  }
  return indexes.size();
}

uint32_t Symtab::AppendSymbolIndexesWithNameAndType(
    ConstString symbol_name, SymbolType symbol_type,
    Debug symbol_debug_type, Visibility symbol_visibility,
    std::vector<uint32_t> &indexes) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (AppendSymbolIndexesWithName(symbol_name, symbol_debug_type,
                                  symbol_visibility, indexes) > 0) {
    std::vector<uint32_t>::iterator pos = indexes.begin();
    while (pos != indexes.end()) {
      if (symbol_type == eSymbolTypeAny ||
          m_symbols[*pos].GetType() == symbol_type)
        ++pos;
      else
        pos = indexes.erase(pos);
    }
  }
  return indexes.size();
}

uint32_t Symtab::AppendSymbolIndexesMatchingRegExAndType(
    const RegularExpression &regexp, SymbolType symbol_type,
    std::vector<uint32_t> &indexes, Mangled::NamePreference name_preference) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  uint32_t prev_size = indexes.size();
  uint32_t sym_end = m_symbols.size();

  for (uint32_t i = 0; i < sym_end; i++) {
    if (symbol_type == eSymbolTypeAny ||
        m_symbols[i].GetType() == symbol_type) {
      const char *name =
          m_symbols[i].GetMangled().GetName(name_preference).AsCString();
      if (name) {
        if (regexp.Execute(name))
          indexes.push_back(i);
      }
    }
  }
  return indexes.size() - prev_size;
}

uint32_t Symtab::AppendSymbolIndexesMatchingRegExAndType(
    const RegularExpression &regexp, SymbolType symbol_type,
    Debug symbol_debug_type, Visibility symbol_visibility,
    std::vector<uint32_t> &indexes, Mangled::NamePreference name_preference) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  uint32_t prev_size = indexes.size();
  uint32_t sym_end = m_symbols.size();

  for (uint32_t i = 0; i < sym_end; i++) {
    if (symbol_type == eSymbolTypeAny ||
        m_symbols[i].GetType() == symbol_type) {
      if (!CheckSymbolAtIndex(i, symbol_debug_type, symbol_visibility))
        continue;

      const char *name =
          m_symbols[i].GetMangled().GetName(name_preference).AsCString();
      if (name) {
        if (regexp.Execute(name))
          indexes.push_back(i);
      }
    }
  }
  return indexes.size() - prev_size;
}

Symbol *Symtab::FindSymbolWithType(SymbolType symbol_type,
                                   Debug symbol_debug_type,
                                   Visibility symbol_visibility,
                                   uint32_t &start_idx) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  const size_t count = m_symbols.size();
  for (size_t idx = start_idx; idx < count; ++idx) {
    if (symbol_type == eSymbolTypeAny ||
        m_symbols[idx].GetType() == symbol_type) {
      if (CheckSymbolAtIndex(idx, symbol_debug_type, symbol_visibility)) {
        start_idx = idx;
        return &m_symbols[idx];
      }
    }
  }
  return nullptr;
}

void
Symtab::FindAllSymbolsWithNameAndType(ConstString name,
                                      SymbolType symbol_type,
                                      std::vector<uint32_t> &symbol_indexes) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  // Initialize all of the lookup by name indexes before converting NAME to a
  // uniqued string NAME_STR below.
  if (!m_name_indexes_computed)
    InitNameIndexes();

  if (name) {
    // The string table did have a string that matched, but we need to check
    // the symbols and match the symbol_type if any was given.
    AppendSymbolIndexesWithNameAndType(name, symbol_type, symbol_indexes);
  }
}

void Symtab::FindAllSymbolsWithNameAndType(
    ConstString name, SymbolType symbol_type, Debug symbol_debug_type,
    Visibility symbol_visibility, std::vector<uint32_t> &symbol_indexes) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  LLDB_SCOPED_TIMER();
  // Initialize all of the lookup by name indexes before converting NAME to a
  // uniqued string NAME_STR below.
  if (!m_name_indexes_computed)
    InitNameIndexes();

  if (name) {
    // The string table did have a string that matched, but we need to check
    // the symbols and match the symbol_type if any was given.
    AppendSymbolIndexesWithNameAndType(name, symbol_type, symbol_debug_type,
                                       symbol_visibility, symbol_indexes);
  }
}

void Symtab::FindAllSymbolsMatchingRexExAndType(
    const RegularExpression &regex, SymbolType symbol_type,
    Debug symbol_debug_type, Visibility symbol_visibility,
    std::vector<uint32_t> &symbol_indexes,
    Mangled::NamePreference name_preference) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  AppendSymbolIndexesMatchingRegExAndType(regex, symbol_type, symbol_debug_type,
                                          symbol_visibility, symbol_indexes,
                                          name_preference);
}

Symbol *Symtab::FindFirstSymbolWithNameAndType(ConstString name,
                                               SymbolType symbol_type,
                                               Debug symbol_debug_type,
                                               Visibility symbol_visibility) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  LLDB_SCOPED_TIMER();
  if (!m_name_indexes_computed)
    InitNameIndexes();

  if (name) {
    std::vector<uint32_t> matching_indexes;
    // The string table did have a string that matched, but we need to check
    // the symbols and match the symbol_type if any was given.
    if (AppendSymbolIndexesWithNameAndType(name, symbol_type, symbol_debug_type,
                                           symbol_visibility,
                                           matching_indexes)) {
      std::vector<uint32_t>::const_iterator pos, end = matching_indexes.end();
      for (pos = matching_indexes.begin(); pos != end; ++pos) {
        Symbol *symbol = SymbolAtIndex(*pos);

        if (symbol->Compare(name, symbol_type))
          return symbol;
      }
    }
  }
  return nullptr;
}

typedef struct {
  const Symtab *symtab;
  const addr_t file_addr;
  Symbol *match_symbol;
  const uint32_t *match_index_ptr;
  addr_t match_offset;
} SymbolSearchInfo;

// Add all the section file start address & size to the RangeVector, recusively
// adding any children sections.
static void AddSectionsToRangeMap(SectionList *sectlist,
                                  RangeVector<addr_t, addr_t> &section_ranges) {
  const int num_sections = sectlist->GetNumSections(0);
  for (int i = 0; i < num_sections; i++) {
    SectionSP sect_sp = sectlist->GetSectionAtIndex(i);
    if (sect_sp) {
      SectionList &child_sectlist = sect_sp->GetChildren();

      // If this section has children, add the children to the RangeVector.
      // Else add this section to the RangeVector.
      if (child_sectlist.GetNumSections(0) > 0) {
        AddSectionsToRangeMap(&child_sectlist, section_ranges);
      } else {
        size_t size = sect_sp->GetByteSize();
        if (size > 0) {
          addr_t base_addr = sect_sp->GetFileAddress();
          RangeVector<addr_t, addr_t>::Entry entry;
          entry.SetRangeBase(base_addr);
          entry.SetByteSize(size);
          section_ranges.Append(entry);
        }
      }
    }
  }
}

void Symtab::InitAddressIndexes() {
  // Protected function, no need to lock mutex...
  if (!m_file_addr_to_index_computed && !m_symbols.empty()) {
    m_file_addr_to_index_computed = true;

    FileRangeToIndexMap::Entry entry;
    const_iterator begin = m_symbols.begin();
    const_iterator end = m_symbols.end();
    for (const_iterator pos = m_symbols.begin(); pos != end; ++pos) {
      if (pos->ValueIsAddress()) {
        entry.SetRangeBase(pos->GetAddressRef().GetFileAddress());
        entry.SetByteSize(pos->GetByteSize());
        entry.data = std::distance(begin, pos);
        m_file_addr_to_index.Append(entry);
      }
    }
    const size_t num_entries = m_file_addr_to_index.GetSize();
    if (num_entries > 0) {
      m_file_addr_to_index.Sort();

      // Create a RangeVector with the start & size of all the sections for
      // this objfile.  We'll need to check this for any FileRangeToIndexMap
      // entries with an uninitialized size, which could potentially be a large
      // number so reconstituting the weak pointer is busywork when it is
      // invariant information.
      SectionList *sectlist = m_objfile->GetSectionList();
      RangeVector<addr_t, addr_t> section_ranges;
      if (sectlist) {
        AddSectionsToRangeMap(sectlist, section_ranges);
        section_ranges.Sort();
      }

      // Iterate through the FileRangeToIndexMap and fill in the size for any
      // entries that didn't already have a size from the Symbol (e.g. if we
      // have a plain linker symbol with an address only, instead of debug info
      // where we get an address and a size and a type, etc.)
      for (size_t i = 0; i < num_entries; i++) {
        FileRangeToIndexMap::Entry *entry =
            m_file_addr_to_index.GetMutableEntryAtIndex(i);
        if (entry->GetByteSize() == 0) {
          addr_t curr_base_addr = entry->GetRangeBase();
          const RangeVector<addr_t, addr_t>::Entry *containing_section =
              section_ranges.FindEntryThatContains(curr_base_addr);

          // Use the end of the section as the default max size of the symbol
          addr_t sym_size = 0;
          if (containing_section) {
            sym_size =
                containing_section->GetByteSize() -
                (entry->GetRangeBase() - containing_section->GetRangeBase());
          }

          for (size_t j = i; j < num_entries; j++) {
            FileRangeToIndexMap::Entry *next_entry =
                m_file_addr_to_index.GetMutableEntryAtIndex(j);
            addr_t next_base_addr = next_entry->GetRangeBase();
            if (next_base_addr > curr_base_addr) {
              addr_t size_to_next_symbol = next_base_addr - curr_base_addr;

              // Take the difference between this symbol and the next one as
              // its size, if it is less than the size of the section.
              if (sym_size == 0 || size_to_next_symbol < sym_size) {
                sym_size = size_to_next_symbol;
              }
              break;
            }
          }

          if (sym_size > 0) {
            entry->SetByteSize(sym_size);
            Symbol &symbol = m_symbols[entry->data];
            symbol.SetByteSize(sym_size);
            symbol.SetSizeIsSynthesized(true);
          }
        }
      }

      // Sort again in case the range size changes the ordering
      m_file_addr_to_index.Sort();
    }
  }
}

void Symtab::Finalize() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  // Calculate the size of symbols inside InitAddressIndexes.
  InitAddressIndexes();
  // Shrink to fit the symbols so we don't waste memory
  m_symbols.shrink_to_fit();
  SaveToCache();
}

Symbol *Symtab::FindSymbolAtFileAddress(addr_t file_addr) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (!m_file_addr_to_index_computed)
    InitAddressIndexes();

  const FileRangeToIndexMap::Entry *entry =
      m_file_addr_to_index.FindEntryStartsAt(file_addr);
  if (entry) {
    Symbol *symbol = SymbolAtIndex(entry->data);
    if (symbol->GetFileAddress() == file_addr)
      return symbol;
  }
  return nullptr;
}

Symbol *Symtab::FindSymbolContainingFileAddress(addr_t file_addr) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (!m_file_addr_to_index_computed)
    InitAddressIndexes();

  const FileRangeToIndexMap::Entry *entry =
      m_file_addr_to_index.FindEntryThatContains(file_addr);
  if (entry) {
    Symbol *symbol = SymbolAtIndex(entry->data);
    if (symbol->ContainsFileAddress(file_addr))
      return symbol;
  }
  return nullptr;
}

void Symtab::ForEachSymbolContainingFileAddress(
    addr_t file_addr, std::function<bool(Symbol *)> const &callback) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (!m_file_addr_to_index_computed)
    InitAddressIndexes();

  std::vector<uint32_t> all_addr_indexes;

  // Get all symbols with file_addr
  const size_t addr_match_count =
      m_file_addr_to_index.FindEntryIndexesThatContain(file_addr,
                                                       all_addr_indexes);

  for (size_t i = 0; i < addr_match_count; ++i) {
    Symbol *symbol = SymbolAtIndex(all_addr_indexes[i]);
    if (symbol->ContainsFileAddress(file_addr)) {
      if (!callback(symbol))
        break;
    }
  }
}

void Symtab::SymbolIndicesToSymbolContextList(
    std::vector<uint32_t> &symbol_indexes, SymbolContextList &sc_list) {
  // No need to protect this call using m_mutex all other method calls are
  // already thread safe.

  const bool merge_symbol_into_function = true;
  size_t num_indices = symbol_indexes.size();
  if (num_indices > 0) {
    SymbolContext sc;
    sc.module_sp = m_objfile->GetModule();
    for (size_t i = 0; i < num_indices; i++) {
      sc.symbol = SymbolAtIndex(symbol_indexes[i]);
      if (sc.symbol)
        sc_list.AppendIfUnique(sc, merge_symbol_into_function);
    }
  }
}

void Symtab::FindFunctionSymbols(ConstString name, uint32_t name_type_mask,
                                 SymbolContextList &sc_list) {
  std::vector<uint32_t> symbol_indexes;

  // eFunctionNameTypeAuto should be pre-resolved by a call to
  // Module::LookupInfo::LookupInfo()
  assert((name_type_mask & eFunctionNameTypeAuto) == 0);

  if (name_type_mask & (eFunctionNameTypeBase | eFunctionNameTypeFull)) {
    std::vector<uint32_t> temp_symbol_indexes;
    FindAllSymbolsWithNameAndType(name, eSymbolTypeAny, temp_symbol_indexes);

    unsigned temp_symbol_indexes_size = temp_symbol_indexes.size();
    if (temp_symbol_indexes_size > 0) {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      for (unsigned i = 0; i < temp_symbol_indexes_size; i++) {
        SymbolContext sym_ctx;
        sym_ctx.symbol = SymbolAtIndex(temp_symbol_indexes[i]);
        if (sym_ctx.symbol) {
          switch (sym_ctx.symbol->GetType()) {
          case eSymbolTypeCode:
          case eSymbolTypeResolver:
          case eSymbolTypeReExported:
          case eSymbolTypeAbsolute:
            symbol_indexes.push_back(temp_symbol_indexes[i]);
            break;
          default:
            break;
          }
        }
      }
    }
  }

  if (!m_name_indexes_computed)
    InitNameIndexes();

  for (lldb::FunctionNameType type :
       {lldb::eFunctionNameTypeBase, lldb::eFunctionNameTypeMethod,
        lldb::eFunctionNameTypeSelector}) {
    if (name_type_mask & type) {
      auto map = GetNameToSymbolIndexMap(type);

      const UniqueCStringMap<uint32_t>::Entry *match;
      for (match = map.FindFirstValueForName(name); match != nullptr;
           match = map.FindNextValueForName(match)) {
        symbol_indexes.push_back(match->value);
      }
    }
  }

  if (!symbol_indexes.empty()) {
    llvm::sort(symbol_indexes);
    symbol_indexes.erase(
        std::unique(symbol_indexes.begin(), symbol_indexes.end()),
        symbol_indexes.end());
    SymbolIndicesToSymbolContextList(symbol_indexes, sc_list);
  }
}

const Symbol *Symtab::GetParent(Symbol *child_symbol) const {
  uint32_t child_idx = GetIndexForSymbol(child_symbol);
  if (child_idx != UINT32_MAX && child_idx > 0) {
    for (uint32_t idx = child_idx - 1; idx != UINT32_MAX; --idx) {
      const Symbol *symbol = SymbolAtIndex(idx);
      const uint32_t sibling_idx = symbol->GetSiblingIndex();
      if (sibling_idx != UINT32_MAX && sibling_idx > child_idx)
        return symbol;
    }
  }
  return nullptr;
}

std::string Symtab::GetCacheKey() {
  std::string key;
  llvm::raw_string_ostream strm(key);
  // Symbol table can come from different object files for the same module. A
  // module can have one object file as the main executable and might have
  // another object file in a separate symbol file.
  strm << m_objfile->GetModule()->GetCacheKey() << "-symtab-"
      << llvm::format_hex(m_objfile->GetCacheHash(), 10);
  return strm.str();
}

void Symtab::SaveToCache() {
  DataFileCache *cache = Module::GetIndexCache();
  if (!cache)
    return; // Caching is not enabled.
  InitNameIndexes(); // Init the name indexes so we can cache them as well.
  const auto byte_order = endian::InlHostByteOrder();
  DataEncoder file(byte_order, /*addr_size=*/8);
  // Encode will return false if the symbol table's object file doesn't have
  // anything to make a signature from.
  if (Encode(file))
    if (cache->SetCachedData(GetCacheKey(), file.GetData()))
      SetWasSavedToCache();
}

constexpr llvm::StringLiteral kIdentifierCStrMap("CMAP");

static void EncodeCStrMap(DataEncoder &encoder, ConstStringTable &strtab,
                          const UniqueCStringMap<uint32_t> &cstr_map) {
  encoder.AppendData(kIdentifierCStrMap);
  encoder.AppendU32(cstr_map.GetSize());
  for (const auto &entry: cstr_map) {
    // Make sure there are no empty strings.
    assert((bool)entry.cstring);
    encoder.AppendU32(strtab.Add(entry.cstring));
    encoder.AppendU32(entry.value);
  }
}

bool DecodeCStrMap(const DataExtractor &data, lldb::offset_t *offset_ptr,
                   const StringTableReader &strtab,
                   UniqueCStringMap<uint32_t> &cstr_map) {
  llvm::StringRef identifier((const char *)data.GetData(offset_ptr, 4), 4);
  if (identifier != kIdentifierCStrMap)
    return false;
  const uint32_t count = data.GetU32(offset_ptr);
  cstr_map.Reserve(count);
  for (uint32_t i=0; i<count; ++i)
  {
    llvm::StringRef str(strtab.Get(data.GetU32(offset_ptr)));
    uint32_t value = data.GetU32(offset_ptr);
    // No empty strings in the name indexes in Symtab
    if (str.empty())
      return false;
    cstr_map.Append(ConstString(str), value);
  }
  // We must sort the UniqueCStringMap after decoding it since it is a vector
  // of UniqueCStringMap::Entry objects which contain a ConstString and type T.
  // ConstString objects are sorted by "const char *" and then type T and
  // the "const char *" are point values that will depend on the order in which
  // ConstString objects are created and in which of the 256 string pools they
  // are created in. So after we decode all of the entries, we must sort the
  // name map to ensure name lookups succeed. If we encode and decode within
  // the same process we wouldn't need to sort, so unit testing didn't catch
  // this issue when first checked in.
  cstr_map.Sort();
  return true;
}

constexpr llvm::StringLiteral kIdentifierSymbolTable("SYMB");
constexpr uint32_t CURRENT_CACHE_VERSION = 1;

/// The encoding format for the symbol table is as follows:
///
/// Signature signature;
/// ConstStringTable strtab;
/// Identifier four character code: 'SYMB'
/// uint32_t version;
/// uint32_t num_symbols;
/// Symbol symbols[num_symbols];
/// uint8_t num_cstr_maps;
/// UniqueCStringMap<uint32_t> cstr_maps[num_cstr_maps]
bool Symtab::Encode(DataEncoder &encoder) const {
  // Name indexes must be computed before calling this function.
  assert(m_name_indexes_computed);

  // Encode the object file's signature
  CacheSignature signature(m_objfile);
  if (!signature.Encode(encoder))
    return false;
  ConstStringTable strtab;

  // Encoder the symbol table into a separate encoder first. This allows us
  // gather all of the strings we willl need in "strtab" as we will need to
  // write the string table out before the symbol table.
  DataEncoder symtab_encoder(encoder.GetByteOrder(),
                              encoder.GetAddressByteSize());
  symtab_encoder.AppendData(kIdentifierSymbolTable);
  // Encode the symtab data version.
  symtab_encoder.AppendU32(CURRENT_CACHE_VERSION);
  // Encode the number of symbols.
  symtab_encoder.AppendU32(m_symbols.size());
  // Encode the symbol data for all symbols.
  for (const auto &symbol: m_symbols)
    symbol.Encode(symtab_encoder, strtab);

  // Emit a byte for how many C string maps we emit. We will fix this up after
  // we emit the C string maps since we skip emitting C string maps if they are
  // empty.
  size_t num_cmaps_offset = symtab_encoder.GetByteSize();
  uint8_t num_cmaps = 0;
  symtab_encoder.AppendU8(0);
  for (const auto &pair: m_name_to_symbol_indices) {
    if (pair.second.IsEmpty())
      continue;
    ++num_cmaps;
    symtab_encoder.AppendU8(pair.first);
    EncodeCStrMap(symtab_encoder, strtab, pair.second);
  }
  if (num_cmaps > 0)
    symtab_encoder.PutU8(num_cmaps_offset, num_cmaps);

  // Now that all strings have been gathered, we will emit the string table.
  strtab.Encode(encoder);
  // Followed by the symbol table data.
  encoder.AppendData(symtab_encoder.GetData());
  return true;
}

bool Symtab::Decode(const DataExtractor &data, lldb::offset_t *offset_ptr,
                    bool &signature_mismatch) {
  signature_mismatch = false;
  CacheSignature signature;
  StringTableReader strtab;
  { // Scope for "elapsed" object below so it can measure the time parse.
    ElapsedTime elapsed(m_objfile->GetModule()->GetSymtabParseTime());
    if (!signature.Decode(data, offset_ptr))
      return false;
    if (CacheSignature(m_objfile) != signature) {
      signature_mismatch = true;
      return false;
    }
    // We now decode the string table for all strings in the data cache file.
    if (!strtab.Decode(data, offset_ptr))
      return false;

    // And now we can decode the symbol table with string table we just decoded.
    llvm::StringRef identifier((const char *)data.GetData(offset_ptr, 4), 4);
    if (identifier != kIdentifierSymbolTable)
      return false;
    const uint32_t version = data.GetU32(offset_ptr);
    if (version != CURRENT_CACHE_VERSION)
      return false;
    const uint32_t num_symbols = data.GetU32(offset_ptr);
    if (num_symbols == 0)
      return true;
    m_symbols.resize(num_symbols);
    SectionList *sections = m_objfile->GetModule()->GetSectionList();
    for (uint32_t i=0; i<num_symbols; ++i) {
      if (!m_symbols[i].Decode(data, offset_ptr, sections, strtab))
        return false;
    }
  }

  { // Scope for "elapsed" object below so it can measure the time to index.
    ElapsedTime elapsed(m_objfile->GetModule()->GetSymtabIndexTime());
    const uint8_t num_cstr_maps = data.GetU8(offset_ptr);
    for (uint8_t i=0; i<num_cstr_maps; ++i) {
      uint8_t type = data.GetU8(offset_ptr);
      UniqueCStringMap<uint32_t> &cstr_map =
          GetNameToSymbolIndexMap((lldb::FunctionNameType)type);
      if (!DecodeCStrMap(data, offset_ptr, strtab, cstr_map))
        return false;
    }
    m_name_indexes_computed = true;
  }
  return true;
}

bool Symtab::LoadFromCache() {
  DataFileCache *cache = Module::GetIndexCache();
  if (!cache)
    return false;

  std::unique_ptr<llvm::MemoryBuffer> mem_buffer_up =
      cache->GetCachedData(GetCacheKey());
  if (!mem_buffer_up)
    return false;
  DataExtractor data(mem_buffer_up->getBufferStart(),
                     mem_buffer_up->getBufferSize(),
                     m_objfile->GetByteOrder(),
                     m_objfile->GetAddressByteSize());
  bool signature_mismatch = false;
  lldb::offset_t offset = 0;
  const bool result = Decode(data, &offset, signature_mismatch);
  if (signature_mismatch)
    cache->RemoveCacheFile(GetCacheKey());
  if (result)
    SetWasLoadedFromCache();
  return result;
}
