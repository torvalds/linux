//===-- Function.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/Function.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "llvm/Support/Casting.h"

using namespace lldb;
using namespace lldb_private;

// Basic function information is contained in the FunctionInfo class. It is
// designed to contain the name, linkage name, and declaration location.
FunctionInfo::FunctionInfo(const char *name, const Declaration *decl_ptr)
    : m_name(name), m_declaration(decl_ptr) {}

FunctionInfo::FunctionInfo(ConstString name, const Declaration *decl_ptr)
    : m_name(name), m_declaration(decl_ptr) {}

FunctionInfo::~FunctionInfo() = default;

void FunctionInfo::Dump(Stream *s, bool show_fullpaths) const {
  if (m_name)
    *s << ", name = \"" << m_name << "\"";
  m_declaration.Dump(s, show_fullpaths);
}

int FunctionInfo::Compare(const FunctionInfo &a, const FunctionInfo &b) {
  int result = ConstString::Compare(a.GetName(), b.GetName());
  if (result)
    return result;

  return Declaration::Compare(a.m_declaration, b.m_declaration);
}

Declaration &FunctionInfo::GetDeclaration() { return m_declaration; }

const Declaration &FunctionInfo::GetDeclaration() const {
  return m_declaration;
}

ConstString FunctionInfo::GetName() const { return m_name; }

size_t FunctionInfo::MemorySize() const {
  return m_name.MemorySize() + m_declaration.MemorySize();
}

InlineFunctionInfo::InlineFunctionInfo(const char *name,
                                       llvm::StringRef mangled,
                                       const Declaration *decl_ptr,
                                       const Declaration *call_decl_ptr)
    : FunctionInfo(name, decl_ptr), m_mangled(mangled),
      m_call_decl(call_decl_ptr) {}

InlineFunctionInfo::InlineFunctionInfo(ConstString name,
                                       const Mangled &mangled,
                                       const Declaration *decl_ptr,
                                       const Declaration *call_decl_ptr)
    : FunctionInfo(name, decl_ptr), m_mangled(mangled),
      m_call_decl(call_decl_ptr) {}

InlineFunctionInfo::~InlineFunctionInfo() = default;

void InlineFunctionInfo::Dump(Stream *s, bool show_fullpaths) const {
  FunctionInfo::Dump(s, show_fullpaths);
  if (m_mangled)
    m_mangled.Dump(s);
}

void InlineFunctionInfo::DumpStopContext(Stream *s) const {
  //    s->Indent("[inlined] ");
  s->Indent();
  if (m_mangled)
    s->PutCString(m_mangled.GetName().AsCString());
  else
    s->PutCString(m_name.AsCString());
}

ConstString InlineFunctionInfo::GetName() const {
  if (m_mangled)
    return m_mangled.GetName();
  return m_name;
}

ConstString InlineFunctionInfo::GetDisplayName() const {
  if (m_mangled)
    return m_mangled.GetDisplayDemangledName();
  return m_name;
}

Declaration &InlineFunctionInfo::GetCallSite() { return m_call_decl; }

const Declaration &InlineFunctionInfo::GetCallSite() const {
  return m_call_decl;
}

Mangled &InlineFunctionInfo::GetMangled() { return m_mangled; }

const Mangled &InlineFunctionInfo::GetMangled() const { return m_mangled; }

size_t InlineFunctionInfo::MemorySize() const {
  return FunctionInfo::MemorySize() + m_mangled.MemorySize();
}

/// @name Call site related structures
/// @{

CallEdge::~CallEdge() = default;

CallEdge::CallEdge(AddrType caller_address_type, lldb::addr_t caller_address,
                   bool is_tail_call, CallSiteParameterArray &&parameters)
    : caller_address(caller_address), caller_address_type(caller_address_type),
      is_tail_call(is_tail_call), parameters(std::move(parameters)) {}

lldb::addr_t CallEdge::GetLoadAddress(lldb::addr_t unresolved_pc,
                                      Function &caller, Target &target) {
  Log *log = GetLog(LLDBLog::Step);

  const Address &caller_start_addr = caller.GetAddressRange().GetBaseAddress();

  ModuleSP caller_module_sp = caller_start_addr.GetModule();
  if (!caller_module_sp) {
    LLDB_LOG(log, "GetLoadAddress: cannot get Module for caller");
    return LLDB_INVALID_ADDRESS;
  }

  SectionList *section_list = caller_module_sp->GetSectionList();
  if (!section_list) {
    LLDB_LOG(log, "GetLoadAddress: cannot get SectionList for Module");
    return LLDB_INVALID_ADDRESS;
  }

  Address the_addr = Address(unresolved_pc, section_list);
  lldb::addr_t load_addr = the_addr.GetLoadAddress(&target);
  return load_addr;
}

lldb::addr_t CallEdge::GetReturnPCAddress(Function &caller,
                                          Target &target) const {
  return GetLoadAddress(GetUnresolvedReturnPCAddress(), caller, target);
}

void DirectCallEdge::ParseSymbolFileAndResolve(ModuleList &images) {
  if (resolved)
    return;

  Log *log = GetLog(LLDBLog::Step);
  LLDB_LOG(log, "DirectCallEdge: Lazily parsing the call graph for {0}",
           lazy_callee.symbol_name);

  auto resolve_lazy_callee = [&]() -> Function * {
    ConstString callee_name{lazy_callee.symbol_name};
    SymbolContextList sc_list;
    images.FindFunctionSymbols(callee_name, eFunctionNameTypeAuto, sc_list);
    size_t num_matches = sc_list.GetSize();
    if (num_matches == 0 || !sc_list[0].symbol) {
      LLDB_LOG(log,
               "DirectCallEdge: Found no symbols for {0}, cannot resolve it",
               callee_name);
      return nullptr;
    }
    Address callee_addr = sc_list[0].symbol->GetAddress();
    if (!callee_addr.IsValid()) {
      LLDB_LOG(log, "DirectCallEdge: Invalid symbol address");
      return nullptr;
    }
    Function *f = callee_addr.CalculateSymbolContextFunction();
    if (!f) {
      LLDB_LOG(log, "DirectCallEdge: Could not find complete function");
      return nullptr;
    }
    return f;
  };
  lazy_callee.def = resolve_lazy_callee();
  resolved = true;
}

DirectCallEdge::DirectCallEdge(const char *symbol_name,
                               AddrType caller_address_type,
                               lldb::addr_t caller_address, bool is_tail_call,
                               CallSiteParameterArray &&parameters)
    : CallEdge(caller_address_type, caller_address, is_tail_call,
               std::move(parameters)) {
  lazy_callee.symbol_name = symbol_name;
}

Function *DirectCallEdge::GetCallee(ModuleList &images, ExecutionContext &) {
  ParseSymbolFileAndResolve(images);
  assert(resolved && "Did not resolve lazy callee");
  return lazy_callee.def;
}

IndirectCallEdge::IndirectCallEdge(DWARFExpressionList call_target,
                                   AddrType caller_address_type,
                                   lldb::addr_t caller_address,
                                   bool is_tail_call,
                                   CallSiteParameterArray &&parameters)
    : CallEdge(caller_address_type, caller_address, is_tail_call,
               std::move(parameters)),
      call_target(std::move(call_target)) {}

Function *IndirectCallEdge::GetCallee(ModuleList &images,
                                      ExecutionContext &exe_ctx) {
  Log *log = GetLog(LLDBLog::Step);
  Status error;
  llvm::Expected<Value> callee_addr_val = call_target.Evaluate(
      &exe_ctx, exe_ctx.GetRegisterContext(), LLDB_INVALID_ADDRESS,
      /*initial_value_ptr=*/nullptr,
      /*object_address_ptr=*/nullptr);
  if (!callee_addr_val) {
    LLDB_LOG_ERROR(log, callee_addr_val.takeError(),
                   "IndirectCallEdge: Could not evaluate expression: {0}");
    return nullptr;
  }

  addr_t raw_addr =
      callee_addr_val->GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
  if (raw_addr == LLDB_INVALID_ADDRESS) {
    LLDB_LOG(log, "IndirectCallEdge: Could not extract address from scalar");
    return nullptr;
  }

  Address callee_addr;
  if (!exe_ctx.GetTargetPtr()->ResolveLoadAddress(raw_addr, callee_addr)) {
    LLDB_LOG(log, "IndirectCallEdge: Could not resolve callee's load address");
    return nullptr;
  }

  Function *f = callee_addr.CalculateSymbolContextFunction();
  if (!f) {
    LLDB_LOG(log, "IndirectCallEdge: Could not find complete function");
    return nullptr;
  }

  return f;
}

/// @}

//
Function::Function(CompileUnit *comp_unit, lldb::user_id_t func_uid,
                   lldb::user_id_t type_uid, const Mangled &mangled, Type *type,
                   const AddressRange &range)
    : UserID(func_uid), m_comp_unit(comp_unit), m_type_uid(type_uid),
      m_type(type), m_mangled(mangled), m_block(func_uid), m_range(range),
      m_frame_base(), m_flags(), m_prologue_byte_size(0) {
  m_block.SetParentScope(this);
  assert(comp_unit != nullptr);
}

Function::~Function() = default;

void Function::GetStartLineSourceInfo(FileSpec &source_file,
                                      uint32_t &line_no) {
  line_no = 0;
  source_file.Clear();

  if (m_comp_unit == nullptr)
    return;

  // Initialize m_type if it hasn't been initialized already
  GetType();

  if (m_type != nullptr && m_type->GetDeclaration().GetLine() != 0) {
    source_file = m_type->GetDeclaration().GetFile();
    line_no = m_type->GetDeclaration().GetLine();
  } else {
    LineTable *line_table = m_comp_unit->GetLineTable();
    if (line_table == nullptr)
      return;

    LineEntry line_entry;
    if (line_table->FindLineEntryByAddress(GetAddressRange().GetBaseAddress(),
                                           line_entry, nullptr)) {
      line_no = line_entry.line;
      source_file = line_entry.GetFile();
    }
  }
}

void Function::GetEndLineSourceInfo(FileSpec &source_file, uint32_t &line_no) {
  line_no = 0;
  source_file.Clear();

  // The -1 is kind of cheesy, but I want to get the last line entry for the
  // given function, not the first entry of the next.
  Address scratch_addr(GetAddressRange().GetBaseAddress());
  scratch_addr.SetOffset(scratch_addr.GetOffset() +
                         GetAddressRange().GetByteSize() - 1);

  LineTable *line_table = m_comp_unit->GetLineTable();
  if (line_table == nullptr)
    return;

  LineEntry line_entry;
  if (line_table->FindLineEntryByAddress(scratch_addr, line_entry, nullptr)) {
    line_no = line_entry.line;
    source_file = line_entry.GetFile();
  }
}

llvm::ArrayRef<std::unique_ptr<CallEdge>> Function::GetCallEdges() {
  std::lock_guard<std::mutex> guard(m_call_edges_lock);

  if (m_call_edges_resolved)
    return m_call_edges;

  Log *log = GetLog(LLDBLog::Step);
  LLDB_LOG(log, "GetCallEdges: Attempting to parse call site info for {0}",
           GetDisplayName());

  m_call_edges_resolved = true;

  // Find the SymbolFile which provided this function's definition.
  Block &block = GetBlock(/*can_create*/true);
  SymbolFile *sym_file = block.GetSymbolFile();
  if (!sym_file)
    return std::nullopt;

  // Lazily read call site information from the SymbolFile.
  m_call_edges = sym_file->ParseCallEdgesInFunction(GetID());

  // Sort the call edges to speed up return_pc lookups.
  llvm::sort(m_call_edges, [](const std::unique_ptr<CallEdge> &LHS,
                              const std::unique_ptr<CallEdge> &RHS) {
    return LHS->GetSortKey() < RHS->GetSortKey();
  });

  return m_call_edges;
}

llvm::ArrayRef<std::unique_ptr<CallEdge>> Function::GetTailCallingEdges() {
  // Tail calling edges are sorted at the end of the list. Find them by dropping
  // all non-tail-calls.
  return GetCallEdges().drop_until(
      [](const std::unique_ptr<CallEdge> &edge) { return edge->IsTailCall(); });
}

CallEdge *Function::GetCallEdgeForReturnAddress(addr_t return_pc,
                                                Target &target) {
  auto edges = GetCallEdges();
  auto edge_it =
      llvm::partition_point(edges, [&](const std::unique_ptr<CallEdge> &edge) {
        return std::make_pair(edge->IsTailCall(),
                              edge->GetReturnPCAddress(*this, target)) <
               std::make_pair(false, return_pc);
      });
  if (edge_it == edges.end() ||
      edge_it->get()->GetReturnPCAddress(*this, target) != return_pc)
    return nullptr;
  return edge_it->get();
}

Block &Function::GetBlock(bool can_create) {
  if (!m_block.BlockInfoHasBeenParsed() && can_create) {
    ModuleSP module_sp = CalculateSymbolContextModule();
    if (module_sp) {
      module_sp->GetSymbolFile()->ParseBlocksRecursive(*this);
    } else {
      Debugger::ReportError(llvm::formatv(
          "unable to find module shared pointer for function '{0}' in {1}",
          GetName().GetCString(), m_comp_unit->GetPrimaryFile().GetPath()));
    }
    m_block.SetBlockInfoHasBeenParsed(true, true);
  }
  return m_block;
}

CompileUnit *Function::GetCompileUnit() { return m_comp_unit; }

const CompileUnit *Function::GetCompileUnit() const { return m_comp_unit; }

void Function::GetDescription(Stream *s, lldb::DescriptionLevel level,
                              Target *target) {
  ConstString name = GetName();
  ConstString mangled = m_mangled.GetMangledName();

  *s << "id = " << (const UserID &)*this;
  if (name)
    s->AsRawOstream() << ", name = \"" << name << '"';
  if (mangled)
    s->AsRawOstream() << ", mangled = \"" << mangled << '"';
  if (level == eDescriptionLevelVerbose) {
    *s << ", decl_context = {";
    auto decl_context = GetCompilerContext();
    // Drop the function itself from the context chain.
    if (decl_context.size())
      decl_context.pop_back();
    llvm::interleaveComma(decl_context, *s, [&](auto &ctx) { ctx.Dump(*s); });
    *s << "}";
  }
  *s << ", range = ";
  Address::DumpStyle fallback_style;
  if (level == eDescriptionLevelVerbose)
    fallback_style = Address::DumpStyleModuleWithFileAddress;
  else
    fallback_style = Address::DumpStyleFileAddress;
  GetAddressRange().Dump(s, target, Address::DumpStyleLoadAddress,
                         fallback_style);
}

void Function::Dump(Stream *s, bool show_context) const {
  s->Printf("%p: ", static_cast<const void *>(this));
  s->Indent();
  *s << "Function" << static_cast<const UserID &>(*this);

  m_mangled.Dump(s);

  if (m_type)
    s->Printf(", type = %p", static_cast<void *>(m_type));
  else if (m_type_uid != LLDB_INVALID_UID)
    s->Printf(", type_uid = 0x%8.8" PRIx64, m_type_uid);

  s->EOL();
  // Dump the root object
  if (m_block.BlockInfoHasBeenParsed())
    m_block.Dump(s, m_range.GetBaseAddress().GetFileAddress(), INT_MAX,
                 show_context);
}

void Function::CalculateSymbolContext(SymbolContext *sc) {
  sc->function = this;
  m_comp_unit->CalculateSymbolContext(sc);
}

ModuleSP Function::CalculateSymbolContextModule() {
  SectionSP section_sp(m_range.GetBaseAddress().GetSection());
  if (section_sp)
    return section_sp->GetModule();

  return this->GetCompileUnit()->GetModule();
}

CompileUnit *Function::CalculateSymbolContextCompileUnit() {
  return this->GetCompileUnit();
}

Function *Function::CalculateSymbolContextFunction() { return this; }

lldb::DisassemblerSP Function::GetInstructions(const ExecutionContext &exe_ctx,
                                               const char *flavor,
                                               bool prefer_file_cache) {
  ModuleSP module_sp(GetAddressRange().GetBaseAddress().GetModule());
  if (module_sp && exe_ctx.HasTargetScope()) {
    return Disassembler::DisassembleRange(module_sp->GetArchitecture(), nullptr,
                                          flavor, exe_ctx.GetTargetRef(),
                                          GetAddressRange(), !prefer_file_cache);
  }
  return lldb::DisassemblerSP();
}

bool Function::GetDisassembly(const ExecutionContext &exe_ctx,
                              const char *flavor, Stream &strm,
                              bool prefer_file_cache) {
  lldb::DisassemblerSP disassembler_sp =
      GetInstructions(exe_ctx, flavor, prefer_file_cache);
  if (disassembler_sp) {
    const bool show_address = true;
    const bool show_bytes = false;
    const bool show_control_flow_kind = false;
    disassembler_sp->GetInstructionList().Dump(
        &strm, show_address, show_bytes, show_control_flow_kind, &exe_ctx);
    return true;
  }
  return false;
}

// Symbol *
// Function::CalculateSymbolContextSymbol ()
//{
//    return // TODO: find the symbol for the function???
//}

void Function::DumpSymbolContext(Stream *s) {
  m_comp_unit->DumpSymbolContext(s);
  s->Printf(", Function{0x%8.8" PRIx64 "}", GetID());
}

size_t Function::MemorySize() const {
  size_t mem_size = sizeof(Function) + m_block.MemorySize();
  return mem_size;
}

bool Function::GetIsOptimized() {
  bool result = false;

  // Currently optimization is only indicted by the vendor extension
  // DW_AT_APPLE_optimized which is set on a compile unit level.
  if (m_comp_unit) {
    result = m_comp_unit->GetIsOptimized();
  }
  return result;
}

bool Function::IsTopLevelFunction() {
  bool result = false;

  if (Language *language = Language::FindPlugin(GetLanguage()))
    result = language->IsTopLevelFunction(*this);

  return result;
}

ConstString Function::GetDisplayName() const {
  return m_mangled.GetDisplayDemangledName();
}

CompilerDeclContext Function::GetDeclContext() {
  if (ModuleSP module_sp = CalculateSymbolContextModule())
    if (SymbolFile *sym_file = module_sp->GetSymbolFile())
      return sym_file->GetDeclContextForUID(GetID());
  return {};
}

std::vector<CompilerContext> Function::GetCompilerContext() {
  if (ModuleSP module_sp = CalculateSymbolContextModule())
    if (SymbolFile *sym_file = module_sp->GetSymbolFile())
      return sym_file->GetCompilerContextForUID(GetID());
  return {};
}

Type *Function::GetType() {
  if (m_type == nullptr) {
    SymbolContext sc;

    CalculateSymbolContext(&sc);

    if (!sc.module_sp)
      return nullptr;

    SymbolFile *sym_file = sc.module_sp->GetSymbolFile();

    if (sym_file == nullptr)
      return nullptr;

    m_type = sym_file->ResolveTypeUID(m_type_uid);
  }
  return m_type;
}

const Type *Function::GetType() const { return m_type; }

CompilerType Function::GetCompilerType() {
  Type *function_type = GetType();
  if (function_type)
    return function_type->GetFullCompilerType();
  return CompilerType();
}

uint32_t Function::GetPrologueByteSize() {
  if (m_prologue_byte_size == 0 &&
      m_flags.IsClear(flagsCalculatedPrologueSize)) {
    m_flags.Set(flagsCalculatedPrologueSize);
    LineTable *line_table = m_comp_unit->GetLineTable();
    uint32_t prologue_end_line_idx = 0;

    if (line_table) {
      LineEntry first_line_entry;
      uint32_t first_line_entry_idx = UINT32_MAX;
      if (line_table->FindLineEntryByAddress(GetAddressRange().GetBaseAddress(),
                                             first_line_entry,
                                             &first_line_entry_idx)) {
        // Make sure the first line entry isn't already the end of the prologue
        addr_t prologue_end_file_addr = LLDB_INVALID_ADDRESS;
        addr_t line_zero_end_file_addr = LLDB_INVALID_ADDRESS;

        if (first_line_entry.is_prologue_end) {
          prologue_end_file_addr =
              first_line_entry.range.GetBaseAddress().GetFileAddress();
          prologue_end_line_idx = first_line_entry_idx;
        } else {
          // Check the first few instructions and look for one that has
          // is_prologue_end set to true.
          const uint32_t last_line_entry_idx = first_line_entry_idx + 6;
          for (uint32_t idx = first_line_entry_idx + 1;
               idx < last_line_entry_idx; ++idx) {
            LineEntry line_entry;
            if (line_table->GetLineEntryAtIndex(idx, line_entry)) {
              if (line_entry.is_prologue_end) {
                prologue_end_file_addr =
                    line_entry.range.GetBaseAddress().GetFileAddress();
                prologue_end_line_idx = idx;
                break;
              }
            }
          }
        }

        // If we didn't find the end of the prologue in the line tables, then
        // just use the end address of the first line table entry
        if (prologue_end_file_addr == LLDB_INVALID_ADDRESS) {
          // Check the first few instructions and look for one that has a line
          // number that's different than the first entry.
          uint32_t last_line_entry_idx = first_line_entry_idx + 6;
          for (uint32_t idx = first_line_entry_idx + 1;
               idx < last_line_entry_idx; ++idx) {
            LineEntry line_entry;
            if (line_table->GetLineEntryAtIndex(idx, line_entry)) {
              if (line_entry.line != first_line_entry.line) {
                prologue_end_file_addr =
                    line_entry.range.GetBaseAddress().GetFileAddress();
                prologue_end_line_idx = idx;
                break;
              }
            }
          }

          if (prologue_end_file_addr == LLDB_INVALID_ADDRESS) {
            prologue_end_file_addr =
                first_line_entry.range.GetBaseAddress().GetFileAddress() +
                first_line_entry.range.GetByteSize();
            prologue_end_line_idx = first_line_entry_idx;
          }
        }

        const addr_t func_start_file_addr =
            m_range.GetBaseAddress().GetFileAddress();
        const addr_t func_end_file_addr =
            func_start_file_addr + m_range.GetByteSize();

        // Now calculate the offset to pass the subsequent line 0 entries.
        uint32_t first_non_zero_line = prologue_end_line_idx;
        while (true) {
          LineEntry line_entry;
          if (line_table->GetLineEntryAtIndex(first_non_zero_line,
                                              line_entry)) {
            if (line_entry.line != 0)
              break;
          }
          if (line_entry.range.GetBaseAddress().GetFileAddress() >=
              func_end_file_addr)
            break;

          first_non_zero_line++;
        }

        if (first_non_zero_line > prologue_end_line_idx) {
          LineEntry first_non_zero_entry;
          if (line_table->GetLineEntryAtIndex(first_non_zero_line,
                                              first_non_zero_entry)) {
            line_zero_end_file_addr =
                first_non_zero_entry.range.GetBaseAddress().GetFileAddress();
          }
        }

        // Verify that this prologue end file address in the function's address
        // range just to be sure
        if (func_start_file_addr < prologue_end_file_addr &&
            prologue_end_file_addr < func_end_file_addr) {
          m_prologue_byte_size = prologue_end_file_addr - func_start_file_addr;
        }

        if (prologue_end_file_addr < line_zero_end_file_addr &&
            line_zero_end_file_addr < func_end_file_addr) {
          m_prologue_byte_size +=
              line_zero_end_file_addr - prologue_end_file_addr;
        }
      }
    }
  }

  return m_prologue_byte_size;
}

lldb::LanguageType Function::GetLanguage() const {
  lldb::LanguageType lang = m_mangled.GuessLanguage();
  if (lang != lldb::eLanguageTypeUnknown)
    return lang;

  if (m_comp_unit)
    return m_comp_unit->GetLanguage();

  return lldb::eLanguageTypeUnknown;
}

ConstString Function::GetName() const {
  return m_mangled.GetName();
}

ConstString Function::GetNameNoArguments() const {
  return m_mangled.GetName(Mangled::ePreferDemangledWithoutArguments);
}
