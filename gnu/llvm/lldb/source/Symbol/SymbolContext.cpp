//===-- SymbolContext.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/SymbolContext.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-enumerations.h"

using namespace lldb;
using namespace lldb_private;

SymbolContext::SymbolContext() : target_sp(), module_sp(), line_entry() {}

SymbolContext::SymbolContext(const ModuleSP &m, CompileUnit *cu, Function *f,
                             Block *b, LineEntry *le, Symbol *s)
    : target_sp(), module_sp(m), comp_unit(cu), function(f), block(b),
      line_entry(), symbol(s) {
  if (le)
    line_entry = *le;
}

SymbolContext::SymbolContext(const TargetSP &t, const ModuleSP &m,
                             CompileUnit *cu, Function *f, Block *b,
                             LineEntry *le, Symbol *s)
    : target_sp(t), module_sp(m), comp_unit(cu), function(f), block(b),
      line_entry(), symbol(s) {
  if (le)
    line_entry = *le;
}

SymbolContext::SymbolContext(SymbolContextScope *sc_scope)
    : target_sp(), module_sp(), line_entry() {
  sc_scope->CalculateSymbolContext(this);
}

SymbolContext::~SymbolContext() = default;

void SymbolContext::Clear(bool clear_target) {
  if (clear_target)
    target_sp.reset();
  module_sp.reset();
  comp_unit = nullptr;
  function = nullptr;
  block = nullptr;
  line_entry.Clear();
  symbol = nullptr;
  variable = nullptr;
}

bool SymbolContext::DumpStopContext(
    Stream *s, ExecutionContextScope *exe_scope, const Address &addr,
    bool show_fullpaths, bool show_module, bool show_inlined_frames,
    bool show_function_arguments, bool show_function_name,
    bool show_function_display_name,
    std::optional<Stream::HighlightSettings> settings) const {
  bool dumped_something = false;
  if (show_module && module_sp) {
    if (show_fullpaths)
      *s << module_sp->GetFileSpec();
    else
      *s << module_sp->GetFileSpec().GetFilename();
    s->PutChar('`');
    dumped_something = true;
  }
  if (function != nullptr) {
    SymbolContext inline_parent_sc;
    Address inline_parent_addr;
    if (!show_function_name) {
      s->Printf("<");
      dumped_something = true;
    } else {
      ConstString name;
      if (!show_function_arguments)
        name = function->GetNameNoArguments();
      if (!name && show_function_display_name)
        name = function->GetDisplayName();
      if (!name)
        name = function->GetName();
      if (name)
        s->PutCStringColorHighlighted(name.GetStringRef(), settings);
    }

    if (addr.IsValid()) {
      const addr_t function_offset =
          addr.GetOffset() -
          function->GetAddressRange().GetBaseAddress().GetOffset();
      if (!show_function_name) {
        // Print +offset even if offset is 0
        dumped_something = true;
        s->Printf("+%" PRIu64 ">", function_offset);
      } else if (function_offset) {
        dumped_something = true;
        s->Printf(" + %" PRIu64, function_offset);
      }
    }

    if (GetParentOfInlinedScope(addr, inline_parent_sc, inline_parent_addr)) {
      dumped_something = true;
      Block *inlined_block = block->GetContainingInlinedBlock();
      const InlineFunctionInfo *inlined_block_info =
          inlined_block->GetInlinedFunctionInfo();
      s->Printf(" [inlined] %s", inlined_block_info->GetName().GetCString());

      lldb_private::AddressRange block_range;
      if (inlined_block->GetRangeContainingAddress(addr, block_range)) {
        const addr_t inlined_function_offset =
            addr.GetOffset() - block_range.GetBaseAddress().GetOffset();
        if (inlined_function_offset) {
          s->Printf(" + %" PRIu64, inlined_function_offset);
        }
      }
      // "line_entry" will always be valid as GetParentOfInlinedScope(...) will
      // fill it in correctly with the calling file and line. Previous code
      // was extracting the calling file and line from inlined_block_info and
      // using it right away which is not correct. On the first call to this
      // function "line_entry" will contain the actual line table entry. On
      // susequent calls "line_entry" will contain the calling file and line
      // from the previous inline info.
      if (line_entry.IsValid()) {
        s->PutCString(" at ");
        line_entry.DumpStopContext(s, show_fullpaths);
      }

      if (show_inlined_frames) {
        s->EOL();
        s->Indent();
        const bool show_function_name = true;
        return inline_parent_sc.DumpStopContext(
            s, exe_scope, inline_parent_addr, show_fullpaths, show_module,
            show_inlined_frames, show_function_arguments, show_function_name,
            show_function_display_name);
      }
    } else {
      if (line_entry.IsValid()) {
        dumped_something = true;
        s->PutCString(" at ");
        if (line_entry.DumpStopContext(s, show_fullpaths))
          dumped_something = true;
      }
    }
  } else if (symbol != nullptr) {
    if (!show_function_name) {
      s->Printf("<");
      dumped_something = true;
    } else if (symbol->GetName()) {
      dumped_something = true;
      if (symbol->GetType() == eSymbolTypeTrampoline)
        s->PutCString("symbol stub for: ");
      ConstString name;
      if (show_function_display_name)
        name = symbol->GetDisplayName();
      if (!name)
        name = symbol->GetName();
      s->PutCStringColorHighlighted(name.GetStringRef(), settings);
    }

    if (addr.IsValid() && symbol->ValueIsAddress()) {
      const addr_t symbol_offset =
          addr.GetOffset() - symbol->GetAddressRef().GetOffset();
      if (!show_function_name) {
        // Print +offset even if offset is 0
        dumped_something = true;
        s->Printf("+%" PRIu64 ">", symbol_offset);
      } else if (symbol_offset) {
        dumped_something = true;
        s->Printf(" + %" PRIu64, symbol_offset);
      }
    }
  } else if (addr.IsValid()) {
    addr.Dump(s, exe_scope, Address::DumpStyleModuleWithFileAddress);
    dumped_something = true;
  }
  return dumped_something;
}

void SymbolContext::GetDescription(
    Stream *s, lldb::DescriptionLevel level, Target *target,
    std::optional<Stream::HighlightSettings> settings) const {
  if (module_sp) {
    s->Indent("     Module: file = \"");
    module_sp->GetFileSpec().Dump(s->AsRawOstream());
    *s << '"';
    if (module_sp->GetArchitecture().IsValid())
      s->Printf(", arch = \"%s\"",
                module_sp->GetArchitecture().GetArchitectureName());
    s->EOL();
  }

  if (comp_unit != nullptr) {
    s->Indent("CompileUnit: ");
    comp_unit->GetDescription(s, level);
    s->EOL();
  }

  if (function != nullptr) {
    s->Indent("   Function: ");
    function->GetDescription(s, level, target);
    s->EOL();

    Type *func_type = function->GetType();
    if (func_type) {
      s->Indent("   FuncType: ");
      func_type->GetDescription(s, level, false, target);
      s->EOL();
    }
  }

  if (block != nullptr) {
    std::vector<Block *> blocks;
    blocks.push_back(block);
    Block *parent_block = block->GetParent();

    while (parent_block) {
      blocks.push_back(parent_block);
      parent_block = parent_block->GetParent();
    }
    std::vector<Block *>::reverse_iterator pos;
    std::vector<Block *>::reverse_iterator begin = blocks.rbegin();
    std::vector<Block *>::reverse_iterator end = blocks.rend();
    for (pos = begin; pos != end; ++pos) {
      if (pos == begin)
        s->Indent("     Blocks: ");
      else
        s->Indent("             ");
      (*pos)->GetDescription(s, function, level, target);
      s->EOL();
    }
  }

  if (line_entry.IsValid()) {
    s->Indent("  LineEntry: ");
    line_entry.GetDescription(s, level, comp_unit, target, false);
    s->EOL();
  }

  if (symbol != nullptr) {
    s->Indent("     Symbol: ");
    symbol->GetDescription(s, level, target, settings);
    s->EOL();
  }

  if (variable != nullptr) {
    s->Indent("   Variable: ");

    s->Printf("id = {0x%8.8" PRIx64 "}, ", variable->GetID());

    switch (variable->GetScope()) {
    case eValueTypeVariableGlobal:
      s->PutCString("kind = global, ");
      break;

    case eValueTypeVariableStatic:
      s->PutCString("kind = static, ");
      break;

    case eValueTypeVariableArgument:
      s->PutCString("kind = argument, ");
      break;

    case eValueTypeVariableLocal:
      s->PutCString("kind = local, ");
      break;

    case eValueTypeVariableThreadLocal:
      s->PutCString("kind = thread local, ");
      break;

    default:
      break;
    }

    s->Printf("name = \"%s\"\n", variable->GetName().GetCString());
  }
}

uint32_t SymbolContext::GetResolvedMask() const {
  uint32_t resolved_mask = 0;
  if (target_sp)
    resolved_mask |= eSymbolContextTarget;
  if (module_sp)
    resolved_mask |= eSymbolContextModule;
  if (comp_unit)
    resolved_mask |= eSymbolContextCompUnit;
  if (function)
    resolved_mask |= eSymbolContextFunction;
  if (block)
    resolved_mask |= eSymbolContextBlock;
  if (line_entry.IsValid())
    resolved_mask |= eSymbolContextLineEntry;
  if (symbol)
    resolved_mask |= eSymbolContextSymbol;
  if (variable)
    resolved_mask |= eSymbolContextVariable;
  return resolved_mask;
}

void SymbolContext::Dump(Stream *s, Target *target) const {
  *s << this << ": ";
  s->Indent();
  s->PutCString("SymbolContext");
  s->IndentMore();
  s->EOL();
  s->IndentMore();
  s->Indent();
  *s << "Module       = " << module_sp.get() << ' ';
  if (module_sp)
    module_sp->GetFileSpec().Dump(s->AsRawOstream());
  s->EOL();
  s->Indent();
  *s << "CompileUnit  = " << comp_unit;
  if (comp_unit != nullptr)
    s->Format(" {{{0:x-16}} {1}", comp_unit->GetID(),
              comp_unit->GetPrimaryFile());
  s->EOL();
  s->Indent();
  *s << "Function     = " << function;
  if (function != nullptr) {
    s->Format(" {{{0:x-16}} {1}, address-range = ", function->GetID(),
              function->GetType()->GetName());
    function->GetAddressRange().Dump(s, target, Address::DumpStyleLoadAddress,
                                     Address::DumpStyleModuleWithFileAddress);
    s->EOL();
    s->Indent();
    Type *func_type = function->GetType();
    if (func_type) {
      *s << "        Type = ";
      func_type->Dump(s, false);
    }
  }
  s->EOL();
  s->Indent();
  *s << "Block        = " << block;
  if (block != nullptr)
    s->Format(" {{{0:x-16}}", block->GetID());
  s->EOL();
  s->Indent();
  *s << "LineEntry    = ";
  line_entry.Dump(s, target, true, Address::DumpStyleLoadAddress,
                  Address::DumpStyleModuleWithFileAddress, true);
  s->EOL();
  s->Indent();
  *s << "Symbol       = " << symbol;
  if (symbol != nullptr && symbol->GetMangled())
    *s << ' ' << symbol->GetName().AsCString();
  s->EOL();
  *s << "Variable     = " << variable;
  if (variable != nullptr) {
    s->Format(" {{{0:x-16}} {1}", variable->GetID(),
              variable->GetType()->GetName());
    s->EOL();
  }
  s->IndentLess();
  s->IndentLess();
}

bool lldb_private::operator==(const SymbolContext &lhs,
                              const SymbolContext &rhs) {
  return lhs.function == rhs.function && lhs.symbol == rhs.symbol &&
         lhs.module_sp.get() == rhs.module_sp.get() &&
         lhs.comp_unit == rhs.comp_unit &&
         lhs.target_sp.get() == rhs.target_sp.get() &&
         LineEntry::Compare(lhs.line_entry, rhs.line_entry) == 0 &&
         lhs.variable == rhs.variable;
}

bool lldb_private::operator!=(const SymbolContext &lhs,
                              const SymbolContext &rhs) {
  return !(lhs == rhs);
}

bool SymbolContext::GetAddressRange(uint32_t scope, uint32_t range_idx,
                                    bool use_inline_block_range,
                                    AddressRange &range) const {
  if ((scope & eSymbolContextLineEntry) && line_entry.IsValid()) {
    range = line_entry.range;
    return true;
  }

  if ((scope & eSymbolContextBlock) && (block != nullptr)) {
    if (use_inline_block_range) {
      Block *inline_block = block->GetContainingInlinedBlock();
      if (inline_block)
        return inline_block->GetRangeAtIndex(range_idx, range);
    } else {
      return block->GetRangeAtIndex(range_idx, range);
    }
  }

  if ((scope & eSymbolContextFunction) && (function != nullptr)) {
    if (range_idx == 0) {
      range = function->GetAddressRange();
      return true;
    }
  }

  if ((scope & eSymbolContextSymbol) && (symbol != nullptr)) {
    if (range_idx == 0) {
      if (symbol->ValueIsAddress()) {
        range.GetBaseAddress() = symbol->GetAddressRef();
        range.SetByteSize(symbol->GetByteSize());
        return true;
      }
    }
  }
  range.Clear();
  return false;
}

LanguageType SymbolContext::GetLanguage() const {
  LanguageType lang;
  if (function && (lang = function->GetLanguage()) != eLanguageTypeUnknown) {
    return lang;
  } else if (variable &&
             (lang = variable->GetLanguage()) != eLanguageTypeUnknown) {
    return lang;
  } else if (symbol && (lang = symbol->GetLanguage()) != eLanguageTypeUnknown) {
    return lang;
  } else if (comp_unit &&
             (lang = comp_unit->GetLanguage()) != eLanguageTypeUnknown) {
    return lang;
  } else if (symbol) {
    // If all else fails, try to guess the language from the name.
    return symbol->GetMangled().GuessLanguage();
  }
  return eLanguageTypeUnknown;
}

bool SymbolContext::GetParentOfInlinedScope(const Address &curr_frame_pc,
                                            SymbolContext &next_frame_sc,
                                            Address &next_frame_pc) const {
  next_frame_sc.Clear(false);
  next_frame_pc.Clear();

  if (block) {
    // const addr_t curr_frame_file_addr = curr_frame_pc.GetFileAddress();

    // In order to get the parent of an inlined function we first need to see
    // if we are in an inlined block as "this->block" could be an inlined
    // block, or a parent of "block" could be. So lets check if this block or
    // one of this blocks parents is an inlined function.
    Block *curr_inlined_block = block->GetContainingInlinedBlock();
    if (curr_inlined_block) {
      // "this->block" is contained in an inline function block, so to get the
      // scope above the inlined block, we get the parent of the inlined block
      // itself
      Block *next_frame_block = curr_inlined_block->GetParent();
      // Now calculate the symbol context of the containing block
      next_frame_block->CalculateSymbolContext(&next_frame_sc);

      // If we get here we weren't able to find the return line entry using the
      // nesting of the blocks and the line table.  So just use the call site
      // info from our inlined block.

      AddressRange range;
      if (curr_inlined_block->GetRangeContainingAddress(curr_frame_pc, range)) {
        // To see there this new frame block it, we need to look at the call
        // site information from
        const InlineFunctionInfo *curr_inlined_block_inlined_info =
            curr_inlined_block->GetInlinedFunctionInfo();
        next_frame_pc = range.GetBaseAddress();
        next_frame_sc.line_entry.range.GetBaseAddress() = next_frame_pc;
        next_frame_sc.line_entry.file_sp = std::make_shared<SupportFile>(
            curr_inlined_block_inlined_info->GetCallSite().GetFile());
        next_frame_sc.line_entry.original_file_sp =
            std::make_shared<SupportFile>(
                curr_inlined_block_inlined_info->GetCallSite().GetFile());
        next_frame_sc.line_entry.line =
            curr_inlined_block_inlined_info->GetCallSite().GetLine();
        next_frame_sc.line_entry.column =
            curr_inlined_block_inlined_info->GetCallSite().GetColumn();
        return true;
      } else {
        Log *log = GetLog(LLDBLog::Symbols);

        if (log) {
          LLDB_LOGF(
              log,
              "warning: inlined block 0x%8.8" PRIx64
              " doesn't have a range that contains file address 0x%" PRIx64,
              curr_inlined_block->GetID(), curr_frame_pc.GetFileAddress());
        }
#ifdef LLDB_CONFIGURATION_DEBUG
        else {
          ObjectFile *objfile = nullptr;
          if (module_sp) {
            if (SymbolFile *symbol_file = module_sp->GetSymbolFile())
              objfile = symbol_file->GetObjectFile();
          }
          if (objfile) {
            Debugger::ReportWarning(llvm::formatv(
                "inlined block {0:x} doesn't have a range that contains file "
                "address {1:x} in {2}",
                curr_inlined_block->GetID(), curr_frame_pc.GetFileAddress(),
                objfile->GetFileSpec().GetPath()));
          } else {
            Debugger::ReportWarning(llvm::formatv(
                "inlined block {0:x} doesn't have a range that contains file "
                "address {1:x}",
                curr_inlined_block->GetID(), curr_frame_pc.GetFileAddress()));
          }
        }
#endif
      }
    }
  }

  return false;
}

Block *SymbolContext::GetFunctionBlock() {
  if (function) {
    if (block) {
      // If this symbol context has a block, check to see if this block is
      // itself, or is contained within a block with inlined function
      // information. If so, then the inlined block is the block that defines
      // the function.
      Block *inlined_block = block->GetContainingInlinedBlock();
      if (inlined_block)
        return inlined_block;

      // The block in this symbol context is not inside an inlined block, so
      // the block that defines the function is the function's top level block,
      // which is returned below.
    }

    // There is no block information in this symbol context, so we must assume
    // that the block that is desired is the top level block of the function
    // itself.
    return &function->GetBlock(true);
  }
  return nullptr;
}

llvm::StringRef SymbolContext::GetInstanceVariableName() {
  LanguageType lang_type = eLanguageTypeUnknown;

  if (Block *function_block = GetFunctionBlock())
    if (CompilerDeclContext decl_ctx = function_block->GetDeclContext())
      lang_type = decl_ctx.GetLanguage();

  if (lang_type == eLanguageTypeUnknown)
    lang_type = GetLanguage();

  if (auto *lang = Language::FindPlugin(lang_type))
    return lang->GetInstanceVariableName();

  return {};
}

void SymbolContext::SortTypeList(TypeMap &type_map, TypeList &type_list) const {
  Block *curr_block = block;
  bool isInlinedblock = false;
  if (curr_block != nullptr &&
      curr_block->GetContainingInlinedBlock() != nullptr)
    isInlinedblock = true;

  // Find all types that match the current block if we have one and put them
  // first in the list. Keep iterating up through all blocks.
  while (curr_block != nullptr && !isInlinedblock) {
    type_map.ForEach(
        [curr_block, &type_list](const lldb::TypeSP &type_sp) -> bool {
          SymbolContextScope *scs = type_sp->GetSymbolContextScope();
          if (scs && curr_block == scs->CalculateSymbolContextBlock())
            type_list.Insert(type_sp);
          return true; // Keep iterating
        });

    // Remove any entries that are now in "type_list" from "type_map" since we
    // can't remove from type_map while iterating
    type_list.ForEach([&type_map](const lldb::TypeSP &type_sp) -> bool {
      type_map.Remove(type_sp);
      return true; // Keep iterating
    });
    curr_block = curr_block->GetParent();
  }
  // Find all types that match the current function, if we have onem, and put
  // them next in the list.
  if (function != nullptr && !type_map.Empty()) {
    const size_t old_type_list_size = type_list.GetSize();
    type_map.ForEach([this, &type_list](const lldb::TypeSP &type_sp) -> bool {
      SymbolContextScope *scs = type_sp->GetSymbolContextScope();
      if (scs && function == scs->CalculateSymbolContextFunction())
        type_list.Insert(type_sp);
      return true; // Keep iterating
    });

    // Remove any entries that are now in "type_list" from "type_map" since we
    // can't remove from type_map while iterating
    const size_t new_type_list_size = type_list.GetSize();
    if (new_type_list_size > old_type_list_size) {
      for (size_t i = old_type_list_size; i < new_type_list_size; ++i)
        type_map.Remove(type_list.GetTypeAtIndex(i));
    }
  }
  // Find all types that match the current compile unit, if we have one, and
  // put them next in the list.
  if (comp_unit != nullptr && !type_map.Empty()) {
    const size_t old_type_list_size = type_list.GetSize();

    type_map.ForEach([this, &type_list](const lldb::TypeSP &type_sp) -> bool {
      SymbolContextScope *scs = type_sp->GetSymbolContextScope();
      if (scs && comp_unit == scs->CalculateSymbolContextCompileUnit())
        type_list.Insert(type_sp);
      return true; // Keep iterating
    });

    // Remove any entries that are now in "type_list" from "type_map" since we
    // can't remove from type_map while iterating
    const size_t new_type_list_size = type_list.GetSize();
    if (new_type_list_size > old_type_list_size) {
      for (size_t i = old_type_list_size; i < new_type_list_size; ++i)
        type_map.Remove(type_list.GetTypeAtIndex(i));
    }
  }
  // Find all types that match the current module, if we have one, and put them
  // next in the list.
  if (module_sp && !type_map.Empty()) {
    const size_t old_type_list_size = type_list.GetSize();
    type_map.ForEach([this, &type_list](const lldb::TypeSP &type_sp) -> bool {
      SymbolContextScope *scs = type_sp->GetSymbolContextScope();
      if (scs && module_sp == scs->CalculateSymbolContextModule())
        type_list.Insert(type_sp);
      return true; // Keep iterating
    });
    // Remove any entries that are now in "type_list" from "type_map" since we
    // can't remove from type_map while iterating
    const size_t new_type_list_size = type_list.GetSize();
    if (new_type_list_size > old_type_list_size) {
      for (size_t i = old_type_list_size; i < new_type_list_size; ++i)
        type_map.Remove(type_list.GetTypeAtIndex(i));
    }
  }
  // Any types that are left get copied into the list an any order.
  if (!type_map.Empty()) {
    type_map.ForEach([&type_list](const lldb::TypeSP &type_sp) -> bool {
      type_list.Insert(type_sp);
      return true; // Keep iterating
    });
  }
}

ConstString
SymbolContext::GetFunctionName(Mangled::NamePreference preference) const {
  if (function) {
    if (block) {
      Block *inlined_block = block->GetContainingInlinedBlock();

      if (inlined_block) {
        const InlineFunctionInfo *inline_info =
            inlined_block->GetInlinedFunctionInfo();
        if (inline_info)
          return inline_info->GetName();
      }
    }
    return function->GetMangled().GetName(preference);
  } else if (symbol && symbol->ValueIsAddress()) {
    return symbol->GetMangled().GetName(preference);
  } else {
    // No function, return an empty string.
    return ConstString();
  }
}

LineEntry SymbolContext::GetFunctionStartLineEntry() const {
  LineEntry line_entry;
  Address start_addr;
  if (block) {
    Block *inlined_block = block->GetContainingInlinedBlock();
    if (inlined_block) {
      if (inlined_block->GetStartAddress(start_addr)) {
        if (start_addr.CalculateSymbolContextLineEntry(line_entry))
          return line_entry;
      }
      return LineEntry();
    }
  }

  if (function) {
    if (function->GetAddressRange()
            .GetBaseAddress()
            .CalculateSymbolContextLineEntry(line_entry))
      return line_entry;
  }
  return LineEntry();
}

bool SymbolContext::GetAddressRangeFromHereToEndLine(uint32_t end_line,
                                                     AddressRange &range,
                                                     Status &error) {
  if (!line_entry.IsValid()) {
    error.SetErrorString("Symbol context has no line table.");
    return false;
  }

  range = line_entry.range;
  if (line_entry.line > end_line) {
    error.SetErrorStringWithFormat(
        "end line option %d must be after the current line: %d", end_line,
        line_entry.line);
    return false;
  }

  uint32_t line_index = 0;
  bool found = false;
  while (true) {
    LineEntry this_line;
    line_index = comp_unit->FindLineEntry(line_index, line_entry.line, nullptr,
                                          false, &this_line);
    if (line_index == UINT32_MAX)
      break;
    if (LineEntry::Compare(this_line, line_entry) == 0) {
      found = true;
      break;
    }
  }

  LineEntry end_entry;
  if (!found) {
    // Can't find the index of the SymbolContext's line entry in the
    // SymbolContext's CompUnit.
    error.SetErrorString(
        "Can't find the current line entry in the CompUnit - can't process "
        "the end-line option");
    return false;
  }

  line_index = comp_unit->FindLineEntry(line_index, end_line, nullptr, false,
                                        &end_entry);
  if (line_index == UINT32_MAX) {
    error.SetErrorStringWithFormat(
        "could not find a line table entry corresponding "
        "to end line number %d",
        end_line);
    return false;
  }

  Block *func_block = GetFunctionBlock();
  if (func_block && func_block->GetRangeIndexContainingAddress(
                        end_entry.range.GetBaseAddress()) == UINT32_MAX) {
    error.SetErrorStringWithFormat(
        "end line number %d is not contained within the current function.",
        end_line);
    return false;
  }

  lldb::addr_t range_size = end_entry.range.GetBaseAddress().GetFileAddress() -
                            range.GetBaseAddress().GetFileAddress();
  range.SetByteSize(range_size);
  return true;
}

const Symbol *SymbolContext::FindBestGlobalDataSymbol(ConstString name,
                                                      Status &error) {
  error.Clear();

  if (!target_sp) {
    return nullptr;
  }

  Target &target = *target_sp;
  Module *module = module_sp.get();

  auto ProcessMatches = [this, &name, &target,
                         module](const SymbolContextList &sc_list,
                                 Status &error) -> const Symbol * {
    llvm::SmallVector<const Symbol *, 1> external_symbols;
    llvm::SmallVector<const Symbol *, 1> internal_symbols;
    for (const SymbolContext &sym_ctx : sc_list) {
      if (sym_ctx.symbol) {
        const Symbol *symbol = sym_ctx.symbol;
        const Address sym_address = symbol->GetAddress();

        if (sym_address.IsValid()) {
          switch (symbol->GetType()) {
          case eSymbolTypeData:
          case eSymbolTypeRuntime:
          case eSymbolTypeAbsolute:
          case eSymbolTypeObjCClass:
          case eSymbolTypeObjCMetaClass:
          case eSymbolTypeObjCIVar:
            if (symbol->GetDemangledNameIsSynthesized()) {
              // If the demangled name was synthesized, then don't use it for
              // expressions. Only let the symbol match if the mangled named
              // matches for these symbols.
              if (symbol->GetMangled().GetMangledName() != name)
                break;
            }
            if (symbol->IsExternal()) {
              external_symbols.push_back(symbol);
            } else {
              internal_symbols.push_back(symbol);
            }
            break;
          case eSymbolTypeReExported: {
            ConstString reexport_name = symbol->GetReExportedSymbolName();
            if (reexport_name) {
              ModuleSP reexport_module_sp;
              ModuleSpec reexport_module_spec;
              reexport_module_spec.GetPlatformFileSpec() =
                  symbol->GetReExportedSymbolSharedLibrary();
              if (reexport_module_spec.GetPlatformFileSpec()) {
                reexport_module_sp =
                    target.GetImages().FindFirstModule(reexport_module_spec);
                if (!reexport_module_sp) {
                  reexport_module_spec.GetPlatformFileSpec().ClearDirectory();
                  reexport_module_sp =
                      target.GetImages().FindFirstModule(reexport_module_spec);
                }
              }
              // Don't allow us to try and resolve a re-exported symbol if it
              // is the same as the current symbol
              if (name == symbol->GetReExportedSymbolName() &&
                  module == reexport_module_sp.get())
                return nullptr;

              return FindBestGlobalDataSymbol(symbol->GetReExportedSymbolName(),
                                              error);
            }
          } break;

          case eSymbolTypeCode: // We already lookup functions elsewhere
          case eSymbolTypeVariable:
          case eSymbolTypeLocal:
          case eSymbolTypeParam:
          case eSymbolTypeTrampoline:
          case eSymbolTypeInvalid:
          case eSymbolTypeException:
          case eSymbolTypeSourceFile:
          case eSymbolTypeHeaderFile:
          case eSymbolTypeObjectFile:
          case eSymbolTypeCommonBlock:
          case eSymbolTypeBlock:
          case eSymbolTypeVariableType:
          case eSymbolTypeLineEntry:
          case eSymbolTypeLineHeader:
          case eSymbolTypeScopeBegin:
          case eSymbolTypeScopeEnd:
          case eSymbolTypeAdditional:
          case eSymbolTypeCompiler:
          case eSymbolTypeInstrumentation:
          case eSymbolTypeUndefined:
          case eSymbolTypeResolver:
            break;
          }
        }
      }
    }

    if (external_symbols.size() > 1) {
      StreamString ss;
      ss.Printf("Multiple external symbols found for '%s'\n", name.AsCString());
      for (const Symbol *symbol : external_symbols) {
        symbol->GetDescription(&ss, eDescriptionLevelFull, &target);
      }
      ss.PutChar('\n');
      error.SetErrorString(ss.GetData());
      return nullptr;
    } else if (external_symbols.size()) {
      return external_symbols[0];
    } else if (internal_symbols.size() > 1) {
      StreamString ss;
      ss.Printf("Multiple internal symbols found for '%s'\n", name.AsCString());
      for (const Symbol *symbol : internal_symbols) {
        symbol->GetDescription(&ss, eDescriptionLevelVerbose, &target);
        ss.PutChar('\n');
      }
      error.SetErrorString(ss.GetData());
      return nullptr;
    } else if (internal_symbols.size()) {
      return internal_symbols[0];
    } else {
      return nullptr;
    }
  };

  if (module) {
    SymbolContextList sc_list;
    module->FindSymbolsWithNameAndType(name, eSymbolTypeAny, sc_list);
    const Symbol *const module_symbol = ProcessMatches(sc_list, error);

    if (!error.Success()) {
      return nullptr;
    } else if (module_symbol) {
      return module_symbol;
    }
  }

  {
    SymbolContextList sc_list;
    target.GetImages().FindSymbolsWithNameAndType(name, eSymbolTypeAny,
                                                  sc_list);
    const Symbol *const target_symbol = ProcessMatches(sc_list, error);

    if (!error.Success()) {
      return nullptr;
    } else if (target_symbol) {
      return target_symbol;
    }
  }

  return nullptr; // no error; we just didn't find anything
}

//
//  SymbolContextSpecifier
//

SymbolContextSpecifier::SymbolContextSpecifier(const TargetSP &target_sp)
    : m_target_sp(target_sp), m_module_spec(), m_module_sp(), m_file_spec_up(),
      m_start_line(0), m_end_line(0), m_function_spec(), m_class_name(),
      m_address_range_up(), m_type(eNothingSpecified) {}

SymbolContextSpecifier::~SymbolContextSpecifier() = default;

bool SymbolContextSpecifier::AddLineSpecification(uint32_t line_no,
                                                  SpecificationType type) {
  bool return_value = true;
  switch (type) {
  case eNothingSpecified:
    Clear();
    break;
  case eLineStartSpecified:
    m_start_line = line_no;
    m_type |= eLineStartSpecified;
    break;
  case eLineEndSpecified:
    m_end_line = line_no;
    m_type |= eLineEndSpecified;
    break;
  default:
    return_value = false;
    break;
  }
  return return_value;
}

bool SymbolContextSpecifier::AddSpecification(const char *spec_string,
                                              SpecificationType type) {
  bool return_value = true;
  switch (type) {
  case eNothingSpecified:
    Clear();
    break;
  case eModuleSpecified: {
    // See if we can find the Module, if so stick it in the SymbolContext.
    FileSpec module_file_spec(spec_string);
    ModuleSpec module_spec(module_file_spec);
    lldb::ModuleSP module_sp =
        m_target_sp ? m_target_sp->GetImages().FindFirstModule(module_spec)
                    : nullptr;
    m_type |= eModuleSpecified;
    if (module_sp)
      m_module_sp = module_sp;
    else
      m_module_spec.assign(spec_string);
  } break;
  case eFileSpecified:
    // CompUnits can't necessarily be resolved here, since an inlined function
    // might show up in a number of CompUnits.  Instead we just convert to a
    // FileSpec and store it away.
    m_file_spec_up = std::make_unique<FileSpec>(spec_string);
    m_type |= eFileSpecified;
    break;
  case eLineStartSpecified:
    if ((return_value = llvm::to_integer(spec_string, m_start_line)))
      m_type |= eLineStartSpecified;
    break;
  case eLineEndSpecified:
    if ((return_value = llvm::to_integer(spec_string, m_end_line)))
      m_type |= eLineEndSpecified;
    break;
  case eFunctionSpecified:
    m_function_spec.assign(spec_string);
    m_type |= eFunctionSpecified;
    break;
  case eClassOrNamespaceSpecified:
    Clear();
    m_class_name.assign(spec_string);
    m_type = eClassOrNamespaceSpecified;
    break;
  case eAddressRangeSpecified:
    // Not specified yet...
    break;
  }

  return return_value;
}

void SymbolContextSpecifier::Clear() {
  m_module_spec.clear();
  m_file_spec_up.reset();
  m_function_spec.clear();
  m_class_name.clear();
  m_start_line = 0;
  m_end_line = 0;
  m_address_range_up.reset();

  m_type = eNothingSpecified;
}

bool SymbolContextSpecifier::SymbolContextMatches(const SymbolContext &sc) {
  if (m_type == eNothingSpecified)
    return true;

  // Only compare targets if this specifier has one and it's not the Dummy
  // target.  Otherwise if a specifier gets made in the dummy target and
  // copied over we'll artificially fail the comparision.
  if (m_target_sp && !m_target_sp->IsDummyTarget() &&
      m_target_sp != sc.target_sp)
    return false;

  if (m_type & eModuleSpecified) {
    if (sc.module_sp) {
      if (m_module_sp.get() != nullptr) {
        if (m_module_sp.get() != sc.module_sp.get())
          return false;
      } else {
        FileSpec module_file_spec(m_module_spec);
        if (!FileSpec::Match(module_file_spec, sc.module_sp->GetFileSpec()))
          return false;
      }
    }
  }
  if (m_type & eFileSpecified) {
    if (m_file_spec_up) {
      // If we don't have a block or a comp_unit, then we aren't going to match
      // a source file.
      if (sc.block == nullptr && sc.comp_unit == nullptr)
        return false;

      // Check if the block is present, and if so is it inlined:
      bool was_inlined = false;
      if (sc.block != nullptr) {
        const InlineFunctionInfo *inline_info =
            sc.block->GetInlinedFunctionInfo();
        if (inline_info != nullptr) {
          was_inlined = true;
          if (!FileSpec::Match(*m_file_spec_up,
                               inline_info->GetDeclaration().GetFile()))
            return false;
        }
      }

      // Next check the comp unit, but only if the SymbolContext was not
      // inlined.
      if (!was_inlined && sc.comp_unit != nullptr) {
        if (!FileSpec::Match(*m_file_spec_up, sc.comp_unit->GetPrimaryFile()))
          return false;
      }
    }
  }
  if (m_type & eLineStartSpecified || m_type & eLineEndSpecified) {
    if (sc.line_entry.line < m_start_line || sc.line_entry.line > m_end_line)
      return false;
  }

  if (m_type & eFunctionSpecified) {
    // First check the current block, and if it is inlined, get the inlined
    // function name:
    bool was_inlined = false;
    ConstString func_name(m_function_spec.c_str());

    if (sc.block != nullptr) {
      const InlineFunctionInfo *inline_info =
          sc.block->GetInlinedFunctionInfo();
      if (inline_info != nullptr) {
        was_inlined = true;
        const Mangled &name = inline_info->GetMangled();
        if (!name.NameMatches(func_name))
          return false;
      }
    }
    //  If it wasn't inlined, check the name in the function or symbol:
    if (!was_inlined) {
      if (sc.function != nullptr) {
        if (!sc.function->GetMangled().NameMatches(func_name))
          return false;
      } else if (sc.symbol != nullptr) {
        if (!sc.symbol->GetMangled().NameMatches(func_name))
          return false;
      }
    }
  }

  return true;
}

bool SymbolContextSpecifier::AddressMatches(lldb::addr_t addr) {
  if (m_type & eAddressRangeSpecified) {

  } else {
    Address match_address(addr, nullptr);
    SymbolContext sc;
    m_target_sp->GetImages().ResolveSymbolContextForAddress(
        match_address, eSymbolContextEverything, sc);
    return SymbolContextMatches(sc);
  }
  return true;
}

void SymbolContextSpecifier::GetDescription(
    Stream *s, lldb::DescriptionLevel level) const {
  char path_str[PATH_MAX + 1];

  if (m_type == eNothingSpecified) {
    s->Printf("Nothing specified.\n");
  }

  if (m_type == eModuleSpecified) {
    s->Indent();
    if (m_module_sp) {
      m_module_sp->GetFileSpec().GetPath(path_str, PATH_MAX);
      s->Printf("Module: %s\n", path_str);
    } else
      s->Printf("Module: %s\n", m_module_spec.c_str());
  }

  if (m_type == eFileSpecified && m_file_spec_up != nullptr) {
    m_file_spec_up->GetPath(path_str, PATH_MAX);
    s->Indent();
    s->Printf("File: %s", path_str);
    if (m_type == eLineStartSpecified) {
      s->Printf(" from line %" PRIu64 "", (uint64_t)m_start_line);
      if (m_type == eLineEndSpecified)
        s->Printf("to line %" PRIu64 "", (uint64_t)m_end_line);
      else
        s->Printf("to end");
    } else if (m_type == eLineEndSpecified) {
      s->Printf(" from start to line %" PRIu64 "", (uint64_t)m_end_line);
    }
    s->Printf(".\n");
  }

  if (m_type == eLineStartSpecified) {
    s->Indent();
    s->Printf("From line %" PRIu64 "", (uint64_t)m_start_line);
    if (m_type == eLineEndSpecified)
      s->Printf("to line %" PRIu64 "", (uint64_t)m_end_line);
    else
      s->Printf("to end");
    s->Printf(".\n");
  } else if (m_type == eLineEndSpecified) {
    s->Printf("From start to line %" PRIu64 ".\n", (uint64_t)m_end_line);
  }

  if (m_type == eFunctionSpecified) {
    s->Indent();
    s->Printf("Function: %s.\n", m_function_spec.c_str());
  }

  if (m_type == eClassOrNamespaceSpecified) {
    s->Indent();
    s->Printf("Class name: %s.\n", m_class_name.c_str());
  }

  if (m_type == eAddressRangeSpecified && m_address_range_up != nullptr) {
    s->Indent();
    s->PutCString("Address range: ");
    m_address_range_up->Dump(s, m_target_sp.get(),
                             Address::DumpStyleLoadAddress,
                             Address::DumpStyleFileAddress);
    s->PutCString("\n");
  }
}

//
//  SymbolContextList
//

SymbolContextList::SymbolContextList() : m_symbol_contexts() {}

SymbolContextList::~SymbolContextList() = default;

void SymbolContextList::Append(const SymbolContext &sc) {
  m_symbol_contexts.push_back(sc);
}

void SymbolContextList::Append(const SymbolContextList &sc_list) {
  collection::const_iterator pos, end = sc_list.m_symbol_contexts.end();
  for (pos = sc_list.m_symbol_contexts.begin(); pos != end; ++pos)
    m_symbol_contexts.push_back(*pos);
}

uint32_t SymbolContextList::AppendIfUnique(const SymbolContextList &sc_list,
                                           bool merge_symbol_into_function) {
  uint32_t unique_sc_add_count = 0;
  collection::const_iterator pos, end = sc_list.m_symbol_contexts.end();
  for (pos = sc_list.m_symbol_contexts.begin(); pos != end; ++pos) {
    if (AppendIfUnique(*pos, merge_symbol_into_function))
      ++unique_sc_add_count;
  }
  return unique_sc_add_count;
}

bool SymbolContextList::AppendIfUnique(const SymbolContext &sc,
                                       bool merge_symbol_into_function) {
  collection::iterator pos, end = m_symbol_contexts.end();
  for (pos = m_symbol_contexts.begin(); pos != end; ++pos) {
    if (*pos == sc)
      return false;
  }
  if (merge_symbol_into_function && sc.symbol != nullptr &&
      sc.comp_unit == nullptr && sc.function == nullptr &&
      sc.block == nullptr && !sc.line_entry.IsValid()) {
    if (sc.symbol->ValueIsAddress()) {
      for (pos = m_symbol_contexts.begin(); pos != end; ++pos) {
        // Don't merge symbols into inlined function symbol contexts
        if (pos->block && pos->block->GetContainingInlinedBlock())
          continue;

        if (pos->function) {
          if (pos->function->GetAddressRange().GetBaseAddress() ==
              sc.symbol->GetAddressRef()) {
            // Do we already have a function with this symbol?
            if (pos->symbol == sc.symbol)
              return false;
            if (pos->symbol == nullptr) {
              pos->symbol = sc.symbol;
              return false;
            }
          }
        }
      }
    }
  }
  m_symbol_contexts.push_back(sc);
  return true;
}

void SymbolContextList::Clear() { m_symbol_contexts.clear(); }

void SymbolContextList::Dump(Stream *s, Target *target) const {

  *s << this << ": ";
  s->Indent();
  s->PutCString("SymbolContextList");
  s->EOL();
  s->IndentMore();

  collection::const_iterator pos, end = m_symbol_contexts.end();
  for (pos = m_symbol_contexts.begin(); pos != end; ++pos) {
    // pos->Dump(s, target);
    pos->GetDescription(s, eDescriptionLevelVerbose, target);
  }
  s->IndentLess();
}

bool SymbolContextList::GetContextAtIndex(size_t idx, SymbolContext &sc) const {
  if (idx < m_symbol_contexts.size()) {
    sc = m_symbol_contexts[idx];
    return true;
  }
  return false;
}

bool SymbolContextList::RemoveContextAtIndex(size_t idx) {
  if (idx < m_symbol_contexts.size()) {
    m_symbol_contexts.erase(m_symbol_contexts.begin() + idx);
    return true;
  }
  return false;
}

uint32_t SymbolContextList::GetSize() const { return m_symbol_contexts.size(); }

bool SymbolContextList::IsEmpty() const { return m_symbol_contexts.empty(); }

uint32_t SymbolContextList::NumLineEntriesWithLine(uint32_t line) const {
  uint32_t match_count = 0;
  const size_t size = m_symbol_contexts.size();
  for (size_t idx = 0; idx < size; ++idx) {
    if (m_symbol_contexts[idx].line_entry.line == line)
      ++match_count;
  }
  return match_count;
}

void SymbolContextList::GetDescription(Stream *s, lldb::DescriptionLevel level,
                                       Target *target) const {
  const size_t size = m_symbol_contexts.size();
  for (size_t idx = 0; idx < size; ++idx)
    m_symbol_contexts[idx].GetDescription(s, level, target);
}

bool lldb_private::operator==(const SymbolContextList &lhs,
                              const SymbolContextList &rhs) {
  const uint32_t size = lhs.GetSize();
  if (size != rhs.GetSize())
    return false;

  SymbolContext lhs_sc;
  SymbolContext rhs_sc;
  for (uint32_t i = 0; i < size; ++i) {
    lhs.GetContextAtIndex(i, lhs_sc);
    rhs.GetContextAtIndex(i, rhs_sc);
    if (lhs_sc != rhs_sc)
      return false;
  }
  return true;
}

bool lldb_private::operator!=(const SymbolContextList &lhs,
                              const SymbolContextList &rhs) {
  return !(lhs == rhs);
}
