//===-- SymbolFileBreakpad.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/SymbolFile/Breakpad/SymbolFileBreakpad.h"
#include "Plugins/ObjectFile/Breakpad/BreakpadRecords.h"
#include "Plugins/ObjectFile/Breakpad/ObjectFileBreakpad.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/ADT/StringExtras.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::breakpad;

LLDB_PLUGIN_DEFINE(SymbolFileBreakpad)

char SymbolFileBreakpad::ID;

class SymbolFileBreakpad::LineIterator {
public:
  // begin iterator for sections of given type
  LineIterator(ObjectFile &obj, Record::Kind section_type)
      : m_obj(&obj), m_section_type(toString(section_type)),
        m_next_section_idx(0), m_next_line(llvm::StringRef::npos) {
    ++*this;
  }

  // An iterator starting at the position given by the bookmark.
  LineIterator(ObjectFile &obj, Record::Kind section_type, Bookmark bookmark);

  // end iterator
  explicit LineIterator(ObjectFile &obj)
      : m_obj(&obj),
        m_next_section_idx(m_obj->GetSectionList()->GetNumSections(0)),
        m_current_line(llvm::StringRef::npos),
        m_next_line(llvm::StringRef::npos) {}

  friend bool operator!=(const LineIterator &lhs, const LineIterator &rhs) {
    assert(lhs.m_obj == rhs.m_obj);
    if (lhs.m_next_section_idx != rhs.m_next_section_idx)
      return true;
    if (lhs.m_current_line != rhs.m_current_line)
      return true;
    assert(lhs.m_next_line == rhs.m_next_line);
    return false;
  }

  const LineIterator &operator++();
  llvm::StringRef operator*() const {
    return m_section_text.slice(m_current_line, m_next_line);
  }

  Bookmark GetBookmark() const {
    return Bookmark{m_next_section_idx, m_current_line};
  }

private:
  ObjectFile *m_obj;
  ConstString m_section_type;
  uint32_t m_next_section_idx;
  llvm::StringRef m_section_text;
  size_t m_current_line;
  size_t m_next_line;

  void FindNextLine() {
    m_next_line = m_section_text.find('\n', m_current_line);
    if (m_next_line != llvm::StringRef::npos) {
      ++m_next_line;
      if (m_next_line >= m_section_text.size())
        m_next_line = llvm::StringRef::npos;
    }
  }
};

SymbolFileBreakpad::LineIterator::LineIterator(ObjectFile &obj,
                                               Record::Kind section_type,
                                               Bookmark bookmark)
    : m_obj(&obj), m_section_type(toString(section_type)),
      m_next_section_idx(bookmark.section), m_current_line(bookmark.offset) {
  Section &sect =
      *obj.GetSectionList()->GetSectionAtIndex(m_next_section_idx - 1);
  assert(sect.GetName() == m_section_type);

  DataExtractor data;
  obj.ReadSectionData(&sect, data);
  m_section_text = toStringRef(data.GetData());

  assert(m_current_line < m_section_text.size());
  FindNextLine();
}

const SymbolFileBreakpad::LineIterator &
SymbolFileBreakpad::LineIterator::operator++() {
  const SectionList &list = *m_obj->GetSectionList();
  size_t num_sections = list.GetNumSections(0);
  while (m_next_line != llvm::StringRef::npos ||
         m_next_section_idx < num_sections) {
    if (m_next_line != llvm::StringRef::npos) {
      m_current_line = m_next_line;
      FindNextLine();
      return *this;
    }

    Section &sect = *list.GetSectionAtIndex(m_next_section_idx++);
    if (sect.GetName() != m_section_type)
      continue;
    DataExtractor data;
    m_obj->ReadSectionData(&sect, data);
    m_section_text = toStringRef(data.GetData());
    m_next_line = 0;
  }
  // We've reached the end.
  m_current_line = m_next_line;
  return *this;
}

llvm::iterator_range<SymbolFileBreakpad::LineIterator>
SymbolFileBreakpad::lines(Record::Kind section_type) {
  return llvm::make_range(LineIterator(*m_objfile_sp, section_type),
                          LineIterator(*m_objfile_sp));
}

namespace {
// A helper class for constructing the list of support files for a given compile
// unit.
class SupportFileMap {
public:
  // Given a breakpad file ID, return a file ID to be used in the support files
  // for this compile unit.
  size_t operator[](size_t file) {
    return m_map.try_emplace(file, m_map.size() + 1).first->second;
  }

  // Construct a FileSpecList containing only the support files relevant for
  // this compile unit (in the correct order).
  FileSpecList translate(const FileSpec &cu_spec,
                         llvm::ArrayRef<FileSpec> all_files);

private:
  llvm::DenseMap<size_t, size_t> m_map;
};
} // namespace

FileSpecList SupportFileMap::translate(const FileSpec &cu_spec,
                                       llvm::ArrayRef<FileSpec> all_files) {
  std::vector<FileSpec> result;
  result.resize(m_map.size() + 1);
  result[0] = cu_spec;
  for (const auto &KV : m_map) {
    if (KV.first < all_files.size())
      result[KV.second] = all_files[KV.first];
  }
  return FileSpecList(std::move(result));
}

void SymbolFileBreakpad::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                DebuggerInitialize);
}

void SymbolFileBreakpad::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

uint32_t SymbolFileBreakpad::CalculateAbilities() {
  if (!m_objfile_sp || !llvm::isa<ObjectFileBreakpad>(*m_objfile_sp))
    return 0;

  return CompileUnits | Functions | LineTables;
}

uint32_t SymbolFileBreakpad::CalculateNumCompileUnits() {
  ParseCUData();
  return m_cu_data->GetSize();
}

CompUnitSP SymbolFileBreakpad::ParseCompileUnitAtIndex(uint32_t index) {
  if (index >= m_cu_data->GetSize())
    return nullptr;

  CompUnitData &data = m_cu_data->GetEntryRef(index).data;

  ParseFileRecords();

  FileSpec spec;

  // The FileSpec of the compile unit will be the file corresponding to the
  // first LINE record.
  LineIterator It(*m_objfile_sp, Record::Func, data.bookmark),
      End(*m_objfile_sp);
  assert(Record::classify(*It) == Record::Func);
  ++It; // Skip FUNC record.
  // Skip INLINE records.
  while (It != End && Record::classify(*It) == Record::Inline)
    ++It;

  if (It != End) {
    auto record = LineRecord::parse(*It);
    if (record && record->FileNum < m_files->size())
      spec = (*m_files)[record->FileNum];
  }

  auto cu_sp = std::make_shared<CompileUnit>(
      m_objfile_sp->GetModule(),
      /*user_data*/ nullptr, std::make_shared<SupportFile>(spec), index,
      eLanguageTypeUnknown,
      /*is_optimized*/ eLazyBoolNo);

  SetCompileUnitAtIndex(index, cu_sp);
  return cu_sp;
}

FunctionSP SymbolFileBreakpad::GetOrCreateFunction(CompileUnit &comp_unit) {
  user_id_t id = comp_unit.GetID();
  if (FunctionSP func_sp = comp_unit.FindFunctionByUID(id))
    return func_sp;

  Log *log = GetLog(LLDBLog::Symbols);
  FunctionSP func_sp;
  addr_t base = GetBaseFileAddress();
  if (base == LLDB_INVALID_ADDRESS) {
    LLDB_LOG(log, "Unable to fetch the base address of object file. Skipping "
                  "symtab population.");
    return func_sp;
  }

  const SectionList *list = comp_unit.GetModule()->GetSectionList();
  CompUnitData &data = m_cu_data->GetEntryRef(id).data;
  LineIterator It(*m_objfile_sp, Record::Func, data.bookmark);
  assert(Record::classify(*It) == Record::Func);

  if (auto record = FuncRecord::parse(*It)) {
    Mangled func_name;
    func_name.SetValue(ConstString(record->Name));
    addr_t address = record->Address + base;
    SectionSP section_sp = list->FindSectionContainingFileAddress(address);
    if (section_sp) {
      AddressRange func_range(
          section_sp, address - section_sp->GetFileAddress(), record->Size);
      // Use the CU's id because every CU has only one function inside.
      func_sp = std::make_shared<Function>(&comp_unit, id, 0, func_name,
                                           nullptr, func_range);
      comp_unit.AddFunction(func_sp);
    }
  }
  return func_sp;
}

size_t SymbolFileBreakpad::ParseFunctions(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  return GetOrCreateFunction(comp_unit) ? 1 : 0;
}

bool SymbolFileBreakpad::ParseLineTable(CompileUnit &comp_unit) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  CompUnitData &data = m_cu_data->GetEntryRef(comp_unit.GetID()).data;

  if (!data.line_table_up)
    ParseLineTableAndSupportFiles(comp_unit, data);

  comp_unit.SetLineTable(data.line_table_up.release());
  return true;
}

bool SymbolFileBreakpad::ParseSupportFiles(CompileUnit &comp_unit,
                                           SupportFileList &support_files) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  CompUnitData &data = m_cu_data->GetEntryRef(comp_unit.GetID()).data;
  if (!data.support_files)
    ParseLineTableAndSupportFiles(comp_unit, data);

  for (auto &fs : *data.support_files)
    support_files.Append(fs);
  return true;
}

size_t SymbolFileBreakpad::ParseBlocksRecursive(Function &func) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  CompileUnit *comp_unit = func.GetCompileUnit();
  lldbassert(comp_unit);
  ParseInlineOriginRecords();
  // A vector of current each level's parent block. For example, when parsing
  // "INLINE 0 ...", the current level is 0 and its parent block is the
  // function block at index 0.
  std::vector<Block *> blocks;
  Block &block = func.GetBlock(false);
  block.AddRange(Block::Range(0, func.GetAddressRange().GetByteSize()));
  blocks.push_back(&block);

  size_t blocks_added = 0;
  addr_t func_base = func.GetAddressRange().GetBaseAddress().GetOffset();
  CompUnitData &data = m_cu_data->GetEntryRef(comp_unit->GetID()).data;
  LineIterator It(*m_objfile_sp, Record::Func, data.bookmark),
      End(*m_objfile_sp);
  ++It; // Skip the FUNC record.
  size_t last_added_nest_level = 0;
  while (It != End && Record::classify(*It) == Record::Inline) {
    if (auto record = InlineRecord::parse(*It)) {
      if (record->InlineNestLevel == 0 ||
          record->InlineNestLevel <= last_added_nest_level + 1) {
        last_added_nest_level = record->InlineNestLevel;
        BlockSP block_sp = std::make_shared<Block>(It.GetBookmark().offset);
        FileSpec callsite_file;
        if (record->CallSiteFileNum < m_files->size())
          callsite_file = (*m_files)[record->CallSiteFileNum];
        llvm::StringRef name;
        if (record->OriginNum < m_inline_origins->size())
          name = (*m_inline_origins)[record->OriginNum];

        Declaration callsite(callsite_file, record->CallSiteLineNum);
        block_sp->SetInlinedFunctionInfo(name.str().c_str(),
                                         /*mangled=*/nullptr,
                                         /*decl_ptr=*/nullptr, &callsite);
        for (const auto &range : record->Ranges) {
          block_sp->AddRange(
              Block::Range(range.first - func_base, range.second));
        }
        block_sp->FinalizeRanges();

        blocks[record->InlineNestLevel]->AddChild(block_sp);
        if (record->InlineNestLevel + 1 >= blocks.size()) {
          blocks.resize(blocks.size() + 1);
        }
        blocks[record->InlineNestLevel + 1] = block_sp.get();
        ++blocks_added;
      }
    }
    ++It;
  }
  return blocks_added;
}

void SymbolFileBreakpad::ParseInlineOriginRecords() {
  if (m_inline_origins)
    return;
  m_inline_origins.emplace();

  Log *log = GetLog(LLDBLog::Symbols);
  for (llvm::StringRef line : lines(Record::InlineOrigin)) {
    auto record = InlineOriginRecord::parse(line);
    if (!record) {
      LLDB_LOG(log, "Failed to parse: {0}. Skipping record.", line);
      continue;
    }

    if (record->Number >= m_inline_origins->size())
      m_inline_origins->resize(record->Number + 1);
    (*m_inline_origins)[record->Number] = record->Name;
  }
}

uint32_t
SymbolFileBreakpad::ResolveSymbolContext(const Address &so_addr,
                                         SymbolContextItem resolve_scope,
                                         SymbolContext &sc) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (!(resolve_scope & (eSymbolContextCompUnit | eSymbolContextLineEntry |
                         eSymbolContextFunction | eSymbolContextBlock)))
    return 0;

  ParseCUData();
  uint32_t idx =
      m_cu_data->FindEntryIndexThatContains(so_addr.GetFileAddress());
  if (idx == UINT32_MAX)
    return 0;

  sc.comp_unit = GetCompileUnitAtIndex(idx).get();
  SymbolContextItem result = eSymbolContextCompUnit;
  if (resolve_scope & eSymbolContextLineEntry) {
    if (sc.comp_unit->GetLineTable()->FindLineEntryByAddress(so_addr,
                                                             sc.line_entry)) {
      result |= eSymbolContextLineEntry;
    }
  }

  if (resolve_scope & (eSymbolContextFunction | eSymbolContextBlock)) {
    FunctionSP func_sp = GetOrCreateFunction(*sc.comp_unit);
    if (func_sp) {
      sc.function = func_sp.get();
      result |= eSymbolContextFunction;
      if (resolve_scope & eSymbolContextBlock) {
        Block &block = func_sp->GetBlock(true);
        sc.block = block.FindInnermostBlockByOffset(
            so_addr.GetFileAddress() -
            sc.function->GetAddressRange().GetBaseAddress().GetFileAddress());
        if (sc.block)
          result |= eSymbolContextBlock;
      }
    }
  }

  return result;
}

uint32_t SymbolFileBreakpad::ResolveSymbolContext(
    const SourceLocationSpec &src_location_spec,
    lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (!(resolve_scope & eSymbolContextCompUnit))
    return 0;

  uint32_t old_size = sc_list.GetSize();
  for (size_t i = 0, size = GetNumCompileUnits(); i < size; ++i) {
    CompileUnit &cu = *GetCompileUnitAtIndex(i);
    cu.ResolveSymbolContext(src_location_spec, resolve_scope, sc_list);
  }
  return sc_list.GetSize() - old_size;
}

void SymbolFileBreakpad::FindFunctions(
    const Module::LookupInfo &lookup_info,
    const CompilerDeclContext &parent_decl_ctx, bool include_inlines,
    SymbolContextList &sc_list) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  // TODO: Implement this with supported FunctionNameType.

  ConstString name = lookup_info.GetLookupName();
  for (uint32_t i = 0; i < GetNumCompileUnits(); ++i) {
    CompUnitSP cu_sp = GetCompileUnitAtIndex(i);
    FunctionSP func_sp = GetOrCreateFunction(*cu_sp);
    if (func_sp && name == func_sp->GetNameNoArguments()) {
      SymbolContext sc;
      sc.comp_unit = cu_sp.get();
      sc.function = func_sp.get();
      sc.module_sp = func_sp->CalculateSymbolContextModule();
      sc_list.Append(sc);
    }
  }
}

void SymbolFileBreakpad::FindFunctions(const RegularExpression &regex,
                                       bool include_inlines,
                                       SymbolContextList &sc_list) {
  // TODO
}

void SymbolFileBreakpad::AddSymbols(Symtab &symtab) {
  Log *log = GetLog(LLDBLog::Symbols);
  Module &module = *m_objfile_sp->GetModule();
  addr_t base = GetBaseFileAddress();
  if (base == LLDB_INVALID_ADDRESS) {
    LLDB_LOG(log, "Unable to fetch the base address of object file. Skipping "
                  "symtab population.");
    return;
  }

  const SectionList &list = *module.GetSectionList();
  llvm::DenseSet<addr_t> found_symbol_addresses;
  std::vector<Symbol> symbols;
  auto add_symbol = [&](addr_t address, std::optional<addr_t> size,
                        llvm::StringRef name) {
    address += base;
    SectionSP section_sp = list.FindSectionContainingFileAddress(address);
    if (!section_sp) {
      LLDB_LOG(log,
               "Ignoring symbol {0}, whose address ({1}) is outside of the "
               "object file. Mismatched symbol file?",
               name, address);
      return;
    }
    // Keep track of what addresses were already added so far and only add
    // the symbol with the first address.
    if (!found_symbol_addresses.insert(address).second)
      return;
    symbols.emplace_back(
        /*symID*/ 0, Mangled(name), eSymbolTypeCode,
        /*is_global*/ true, /*is_debug*/ false,
        /*is_trampoline*/ false, /*is_artificial*/ false,
        AddressRange(section_sp, address - section_sp->GetFileAddress(),
                     size.value_or(0)),
        size.has_value(), /*contains_linker_annotations*/ false, /*flags*/ 0);
  };

  for (llvm::StringRef line : lines(Record::Public)) {
    if (auto record = PublicRecord::parse(line))
      add_symbol(record->Address, std::nullopt, record->Name);
    else
      LLDB_LOG(log, "Failed to parse: {0}. Skipping record.", line);
  }

  for (Symbol &symbol : symbols)
    symtab.AddSymbol(std::move(symbol));
  symtab.Finalize();
}

llvm::Expected<lldb::addr_t>
SymbolFileBreakpad::GetParameterStackSize(Symbol &symbol) {
  ParseUnwindData();
  if (auto *entry = m_unwind_data->win.FindEntryThatContains(
          symbol.GetAddress().GetFileAddress())) {
    auto record = StackWinRecord::parse(
        *LineIterator(*m_objfile_sp, Record::StackWin, entry->data));
    assert(record);
    return record->ParameterSize;
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "Parameter size unknown.");
}

static std::optional<std::pair<llvm::StringRef, llvm::StringRef>>
GetRule(llvm::StringRef &unwind_rules) {
  // Unwind rules are of the form
  //   register1: expression1 register2: expression2 ...
  // We assume none of the tokens in expression<n> end with a colon.

  llvm::StringRef lhs, rest;
  std::tie(lhs, rest) = getToken(unwind_rules);
  if (!lhs.consume_back(":"))
    return std::nullopt;

  // Seek forward to the next register: expression pair
  llvm::StringRef::size_type pos = rest.find(": ");
  if (pos == llvm::StringRef::npos) {
    // No pair found, this means the rest of the string is a single expression.
    unwind_rules = llvm::StringRef();
    return std::make_pair(lhs, rest);
  }

  // Go back one token to find the end of the current rule.
  pos = rest.rfind(' ', pos);
  if (pos == llvm::StringRef::npos)
    return std::nullopt;

  llvm::StringRef rhs = rest.take_front(pos);
  unwind_rules = rest.drop_front(pos);
  return std::make_pair(lhs, rhs);
}

static const RegisterInfo *
ResolveRegister(const llvm::Triple &triple,
                const SymbolFile::RegisterInfoResolver &resolver,
                llvm::StringRef name) {
  if (triple.isX86() || triple.isMIPS()) {
    // X86 and MIPS registers have '$' in front of their register names. Arm and
    // AArch64 don't.
    if (!name.consume_front("$"))
      return nullptr;
  }
  return resolver.ResolveName(name);
}

static const RegisterInfo *
ResolveRegisterOrRA(const llvm::Triple &triple,
                    const SymbolFile::RegisterInfoResolver &resolver,
                    llvm::StringRef name) {
  if (name == ".ra")
    return resolver.ResolveNumber(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  return ResolveRegister(triple, resolver, name);
}

llvm::ArrayRef<uint8_t> SymbolFileBreakpad::SaveAsDWARF(postfix::Node &node) {
  ArchSpec arch = m_objfile_sp->GetArchitecture();
  StreamString dwarf(Stream::eBinary, arch.GetAddressByteSize(),
                     arch.GetByteOrder());
  ToDWARF(node, dwarf);
  uint8_t *saved = m_allocator.Allocate<uint8_t>(dwarf.GetSize());
  std::memcpy(saved, dwarf.GetData(), dwarf.GetSize());
  return {saved, dwarf.GetSize()};
}

bool SymbolFileBreakpad::ParseCFIUnwindRow(llvm::StringRef unwind_rules,
                                        const RegisterInfoResolver &resolver,
                                        UnwindPlan::Row &row) {
  Log *log = GetLog(LLDBLog::Symbols);

  llvm::BumpPtrAllocator node_alloc;
  llvm::Triple triple = m_objfile_sp->GetArchitecture().GetTriple();
  while (auto rule = GetRule(unwind_rules)) {
    node_alloc.Reset();
    llvm::StringRef lhs = rule->first;
    postfix::Node *rhs = postfix::ParseOneExpression(rule->second, node_alloc);
    if (!rhs) {
      LLDB_LOG(log, "Could not parse `{0}` as unwind rhs.", rule->second);
      return false;
    }

    bool success = postfix::ResolveSymbols(
        rhs, [&](postfix::SymbolNode &symbol) -> postfix::Node * {
          llvm::StringRef name = symbol.GetName();
          if (name == ".cfa" && lhs != ".cfa")
            return postfix::MakeNode<postfix::InitialValueNode>(node_alloc);

          if (const RegisterInfo *info =
                  ResolveRegister(triple, resolver, name)) {
            return postfix::MakeNode<postfix::RegisterNode>(
                node_alloc, info->kinds[eRegisterKindLLDB]);
          }
          return nullptr;
        });

    if (!success) {
      LLDB_LOG(log, "Resolving symbols in `{0}` failed.", rule->second);
      return false;
    }

    llvm::ArrayRef<uint8_t> saved = SaveAsDWARF(*rhs);
    if (lhs == ".cfa") {
      row.GetCFAValue().SetIsDWARFExpression(saved.data(), saved.size());
    } else if (const RegisterInfo *info =
                   ResolveRegisterOrRA(triple, resolver, lhs)) {
      UnwindPlan::Row::RegisterLocation loc;
      loc.SetIsDWARFExpression(saved.data(), saved.size());
      row.SetRegisterInfo(info->kinds[eRegisterKindLLDB], loc);
    } else
      LLDB_LOG(log, "Invalid register `{0}` in unwind rule.", lhs);
  }
  if (unwind_rules.empty())
    return true;

  LLDB_LOG(log, "Could not parse `{0}` as an unwind rule.", unwind_rules);
  return false;
}

UnwindPlanSP
SymbolFileBreakpad::GetUnwindPlan(const Address &address,
                                  const RegisterInfoResolver &resolver) {
  ParseUnwindData();
  if (auto *entry =
          m_unwind_data->cfi.FindEntryThatContains(address.GetFileAddress()))
    return ParseCFIUnwindPlan(entry->data, resolver);
  if (auto *entry =
          m_unwind_data->win.FindEntryThatContains(address.GetFileAddress()))
    return ParseWinUnwindPlan(entry->data, resolver);
  return nullptr;
}

UnwindPlanSP
SymbolFileBreakpad::ParseCFIUnwindPlan(const Bookmark &bookmark,
                                       const RegisterInfoResolver &resolver) {
  addr_t base = GetBaseFileAddress();
  if (base == LLDB_INVALID_ADDRESS)
    return nullptr;

  LineIterator It(*m_objfile_sp, Record::StackCFI, bookmark),
      End(*m_objfile_sp);
  std::optional<StackCFIRecord> init_record = StackCFIRecord::parse(*It);
  assert(init_record && init_record->Size &&
         "Record already parsed successfully in ParseUnwindData!");

  auto plan_sp = std::make_shared<UnwindPlan>(lldb::eRegisterKindLLDB);
  plan_sp->SetSourceName("breakpad STACK CFI");
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  plan_sp->SetUnwindPlanForSignalTrap(eLazyBoolNo);
  plan_sp->SetSourcedFromCompiler(eLazyBoolYes);
  plan_sp->SetPlanValidAddressRange(
      AddressRange(base + init_record->Address, *init_record->Size,
                   m_objfile_sp->GetModule()->GetSectionList()));

  auto row_sp = std::make_shared<UnwindPlan::Row>();
  row_sp->SetOffset(0);
  if (!ParseCFIUnwindRow(init_record->UnwindRules, resolver, *row_sp))
    return nullptr;
  plan_sp->AppendRow(row_sp);
  for (++It; It != End; ++It) {
    std::optional<StackCFIRecord> record = StackCFIRecord::parse(*It);
    if (!record)
      return nullptr;
    if (record->Size)
      break;

    row_sp = std::make_shared<UnwindPlan::Row>(*row_sp);
    row_sp->SetOffset(record->Address - init_record->Address);
    if (!ParseCFIUnwindRow(record->UnwindRules, resolver, *row_sp))
      return nullptr;
    plan_sp->AppendRow(row_sp);
  }
  return plan_sp;
}

UnwindPlanSP
SymbolFileBreakpad::ParseWinUnwindPlan(const Bookmark &bookmark,
                                       const RegisterInfoResolver &resolver) {
  Log *log = GetLog(LLDBLog::Symbols);
  addr_t base = GetBaseFileAddress();
  if (base == LLDB_INVALID_ADDRESS)
    return nullptr;

  LineIterator It(*m_objfile_sp, Record::StackWin, bookmark);
  std::optional<StackWinRecord> record = StackWinRecord::parse(*It);
  assert(record && "Record already parsed successfully in ParseUnwindData!");

  auto plan_sp = std::make_shared<UnwindPlan>(lldb::eRegisterKindLLDB);
  plan_sp->SetSourceName("breakpad STACK WIN");
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  plan_sp->SetUnwindPlanForSignalTrap(eLazyBoolNo);
  plan_sp->SetSourcedFromCompiler(eLazyBoolYes);
  plan_sp->SetPlanValidAddressRange(
      AddressRange(base + record->RVA, record->CodeSize,
                   m_objfile_sp->GetModule()->GetSectionList()));

  auto row_sp = std::make_shared<UnwindPlan::Row>();
  row_sp->SetOffset(0);

  llvm::BumpPtrAllocator node_alloc;
  std::vector<std::pair<llvm::StringRef, postfix::Node *>> program =
      postfix::ParseFPOProgram(record->ProgramString, node_alloc);

  if (program.empty()) {
    LLDB_LOG(log, "Invalid unwind rule: {0}.", record->ProgramString);
    return nullptr;
  }
  auto it = program.begin();
  llvm::Triple triple = m_objfile_sp->GetArchitecture().GetTriple();
  const auto &symbol_resolver =
      [&](postfix::SymbolNode &symbol) -> postfix::Node * {
    llvm::StringRef name = symbol.GetName();
    for (const auto &rule : llvm::make_range(program.begin(), it)) {
      if (rule.first == name)
        return rule.second;
    }
    if (const RegisterInfo *info = ResolveRegister(triple, resolver, name))
      return postfix::MakeNode<postfix::RegisterNode>(
          node_alloc, info->kinds[eRegisterKindLLDB]);
    return nullptr;
  };

  // We assume the first value will be the CFA. It is usually called T0, but
  // clang will use T1, if it needs to realign the stack.
  auto *symbol = llvm::dyn_cast<postfix::SymbolNode>(it->second);
  if (symbol && symbol->GetName() == ".raSearch") {
    row_sp->GetCFAValue().SetRaSearch(record->LocalSize +
                                      record->SavedRegisterSize);
  } else {
    if (!postfix::ResolveSymbols(it->second, symbol_resolver)) {
      LLDB_LOG(log, "Resolving symbols in `{0}` failed.",
               record->ProgramString);
      return nullptr;
    }
    llvm::ArrayRef<uint8_t> saved  = SaveAsDWARF(*it->second);
    row_sp->GetCFAValue().SetIsDWARFExpression(saved.data(), saved.size());
  }

  // Replace the node value with InitialValueNode, so that subsequent
  // expressions refer to the CFA value instead of recomputing the whole
  // expression.
  it->second = postfix::MakeNode<postfix::InitialValueNode>(node_alloc);


  // Now process the rest of the assignments.
  for (++it; it != program.end(); ++it) {
    const RegisterInfo *info = ResolveRegister(triple, resolver, it->first);
    // It is not an error if the resolution fails because the program may
    // contain temporary variables.
    if (!info)
      continue;
    if (!postfix::ResolveSymbols(it->second, symbol_resolver)) {
      LLDB_LOG(log, "Resolving symbols in `{0}` failed.",
               record->ProgramString);
      return nullptr;
    }

    llvm::ArrayRef<uint8_t> saved = SaveAsDWARF(*it->second);
    UnwindPlan::Row::RegisterLocation loc;
    loc.SetIsDWARFExpression(saved.data(), saved.size());
    row_sp->SetRegisterInfo(info->kinds[eRegisterKindLLDB], loc);
  }

  plan_sp->AppendRow(row_sp);
  return plan_sp;
}

addr_t SymbolFileBreakpad::GetBaseFileAddress() {
  return m_objfile_sp->GetModule()
      ->GetObjectFile()
      ->GetBaseAddress()
      .GetFileAddress();
}

// Parse out all the FILE records from the breakpad file. These will be needed
// when constructing the support file lists for individual compile units.
void SymbolFileBreakpad::ParseFileRecords() {
  if (m_files)
    return;
  m_files.emplace();

  Log *log = GetLog(LLDBLog::Symbols);
  for (llvm::StringRef line : lines(Record::File)) {
    auto record = FileRecord::parse(line);
    if (!record) {
      LLDB_LOG(log, "Failed to parse: {0}. Skipping record.", line);
      continue;
    }

    if (record->Number >= m_files->size())
      m_files->resize(record->Number + 1);
    FileSpec::Style style = FileSpec::GuessPathStyle(record->Name)
                                .value_or(FileSpec::Style::native);
    (*m_files)[record->Number] = FileSpec(record->Name, style);
  }
}

void SymbolFileBreakpad::ParseCUData() {
  if (m_cu_data)
    return;

  m_cu_data.emplace();
  Log *log = GetLog(LLDBLog::Symbols);
  addr_t base = GetBaseFileAddress();
  if (base == LLDB_INVALID_ADDRESS) {
    LLDB_LOG(log, "SymbolFile parsing failed: Unable to fetch the base address "
                  "of object file.");
  }

  // We shall create one compile unit for each FUNC record. So, count the number
  // of FUNC records, and store them in m_cu_data, together with their ranges.
  for (LineIterator It(*m_objfile_sp, Record::Func), End(*m_objfile_sp);
       It != End; ++It) {
    if (auto record = FuncRecord::parse(*It)) {
      m_cu_data->Append(CompUnitMap::Entry(base + record->Address, record->Size,
                                           CompUnitData(It.GetBookmark())));
    } else
      LLDB_LOG(log, "Failed to parse: {0}. Skipping record.", *It);
  }
  m_cu_data->Sort();
}

// Construct the list of support files and line table entries for the given
// compile unit.
void SymbolFileBreakpad::ParseLineTableAndSupportFiles(CompileUnit &cu,
                                                       CompUnitData &data) {
  addr_t base = GetBaseFileAddress();
  assert(base != LLDB_INVALID_ADDRESS &&
         "How did we create compile units without a base address?");

  SupportFileMap map;
  std::vector<std::unique_ptr<LineSequence>> sequences;
  std::unique_ptr<LineSequence> line_seq_up =
      LineTable::CreateLineSequenceContainer();
  std::optional<addr_t> next_addr;
  auto finish_sequence = [&]() {
    LineTable::AppendLineEntryToSequence(
        line_seq_up.get(), *next_addr, /*line=*/0, /*column=*/0,
        /*file_idx=*/0, /*is_start_of_statement=*/false,
        /*is_start_of_basic_block=*/false, /*is_prologue_end=*/false,
        /*is_epilogue_begin=*/false, /*is_terminal_entry=*/true);
    sequences.push_back(std::move(line_seq_up));
    line_seq_up = LineTable::CreateLineSequenceContainer();
  };

  LineIterator It(*m_objfile_sp, Record::Func, data.bookmark),
      End(*m_objfile_sp);
  assert(Record::classify(*It) == Record::Func);
  for (++It; It != End; ++It) {
    // Skip INLINE records
    if (Record::classify(*It) == Record::Inline)
      continue;

    auto record = LineRecord::parse(*It);
    if (!record)
      break;

    record->Address += base;

    if (next_addr && *next_addr != record->Address) {
      // Discontiguous entries. Finish off the previous sequence and reset.
      finish_sequence();
    }
    LineTable::AppendLineEntryToSequence(
        line_seq_up.get(), record->Address, record->LineNum, /*column=*/0,
        map[record->FileNum], /*is_start_of_statement=*/true,
        /*is_start_of_basic_block=*/false, /*is_prologue_end=*/false,
        /*is_epilogue_begin=*/false, /*is_terminal_entry=*/false);
    next_addr = record->Address + record->Size;
  }
  if (next_addr)
    finish_sequence();
  data.line_table_up = std::make_unique<LineTable>(&cu, std::move(sequences));
  data.support_files = map.translate(cu.GetPrimaryFile(), *m_files);
}

void SymbolFileBreakpad::ParseUnwindData() {
  if (m_unwind_data)
    return;
  m_unwind_data.emplace();

  Log *log = GetLog(LLDBLog::Symbols);
  addr_t base = GetBaseFileAddress();
  if (base == LLDB_INVALID_ADDRESS) {
    LLDB_LOG(log, "SymbolFile parsing failed: Unable to fetch the base address "
                  "of object file.");
  }

  for (LineIterator It(*m_objfile_sp, Record::StackCFI), End(*m_objfile_sp);
       It != End; ++It) {
    if (auto record = StackCFIRecord::parse(*It)) {
      if (record->Size)
        m_unwind_data->cfi.Append(UnwindMap::Entry(
            base + record->Address, *record->Size, It.GetBookmark()));
    } else
      LLDB_LOG(log, "Failed to parse: {0}. Skipping record.", *It);
  }
  m_unwind_data->cfi.Sort();

  for (LineIterator It(*m_objfile_sp, Record::StackWin), End(*m_objfile_sp);
       It != End; ++It) {
    if (auto record = StackWinRecord::parse(*It)) {
      m_unwind_data->win.Append(UnwindMap::Entry(
          base + record->RVA, record->CodeSize, It.GetBookmark()));
    } else
      LLDB_LOG(log, "Failed to parse: {0}. Skipping record.", *It);
  }
  m_unwind_data->win.Sort();
}

uint64_t SymbolFileBreakpad::GetDebugInfoSize(bool load_all_debug_info) {
  // Breakpad files are all debug info.
  return m_objfile_sp->GetByteSize();
}
