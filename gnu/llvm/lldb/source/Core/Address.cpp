//===-- Address.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Address.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Declaration.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/AnsiTerminal.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <cassert>
#include <cinttypes>
#include <cstring>

namespace lldb_private {
class CompileUnit;
}
namespace lldb_private {
class Function;
}

using namespace lldb;
using namespace lldb_private;

static size_t ReadBytes(ExecutionContextScope *exe_scope,
                        const Address &address, void *dst, size_t dst_len) {
  if (exe_scope == nullptr)
    return 0;

  TargetSP target_sp(exe_scope->CalculateTarget());
  if (target_sp) {
    Status error;
    bool force_live_memory = true;
    return target_sp->ReadMemory(address, dst, dst_len, error,
                                 force_live_memory);
  }
  return 0;
}

static bool GetByteOrderAndAddressSize(ExecutionContextScope *exe_scope,
                                       const Address &address,
                                       ByteOrder &byte_order,
                                       uint32_t &addr_size) {
  byte_order = eByteOrderInvalid;
  addr_size = 0;
  if (exe_scope == nullptr)
    return false;

  TargetSP target_sp(exe_scope->CalculateTarget());
  if (target_sp) {
    byte_order = target_sp->GetArchitecture().GetByteOrder();
    addr_size = target_sp->GetArchitecture().GetAddressByteSize();
  }

  if (byte_order == eByteOrderInvalid || addr_size == 0) {
    ModuleSP module_sp(address.GetModule());
    if (module_sp) {
      byte_order = module_sp->GetArchitecture().GetByteOrder();
      addr_size = module_sp->GetArchitecture().GetAddressByteSize();
    }
  }
  return byte_order != eByteOrderInvalid && addr_size != 0;
}

static uint64_t ReadUIntMax64(ExecutionContextScope *exe_scope,
                              const Address &address, uint32_t byte_size,
                              bool &success) {
  uint64_t uval64 = 0;
  if (exe_scope == nullptr || byte_size > sizeof(uint64_t)) {
    success = false;
    return 0;
  }
  uint64_t buf = 0;

  success = ReadBytes(exe_scope, address, &buf, byte_size) == byte_size;
  if (success) {
    ByteOrder byte_order = eByteOrderInvalid;
    uint32_t addr_size = 0;
    if (GetByteOrderAndAddressSize(exe_scope, address, byte_order, addr_size)) {
      DataExtractor data(&buf, sizeof(buf), byte_order, addr_size);
      lldb::offset_t offset = 0;
      uval64 = data.GetU64(&offset);
    } else
      success = false;
  }
  return uval64;
}

static bool ReadAddress(ExecutionContextScope *exe_scope,
                        const Address &address, uint32_t pointer_size,
                        Address &deref_so_addr) {
  if (exe_scope == nullptr)
    return false;

  bool success = false;
  addr_t deref_addr = ReadUIntMax64(exe_scope, address, pointer_size, success);
  if (success) {
    ExecutionContext exe_ctx;
    exe_scope->CalculateExecutionContext(exe_ctx);
    // If we have any sections that are loaded, try and resolve using the
    // section load list
    Target *target = exe_ctx.GetTargetPtr();
    if (target && !target->GetSectionLoadList().IsEmpty()) {
      if (target->GetSectionLoadList().ResolveLoadAddress(deref_addr,
                                                          deref_so_addr))
        return true;
    } else {
      // If we were not running, yet able to read an integer, we must have a
      // module
      ModuleSP module_sp(address.GetModule());

      assert(module_sp);
      if (module_sp->ResolveFileAddress(deref_addr, deref_so_addr))
        return true;
    }

    // We couldn't make "deref_addr" into a section offset value, but we were
    // able to read the address, so we return a section offset address with no
    // section and "deref_addr" as the offset (address).
    deref_so_addr.SetRawAddress(deref_addr);
    return true;
  }
  return false;
}

static bool DumpUInt(ExecutionContextScope *exe_scope, const Address &address,
                     uint32_t byte_size, Stream *strm) {
  if (exe_scope == nullptr || byte_size == 0)
    return false;
  std::vector<uint8_t> buf(byte_size, 0);

  if (ReadBytes(exe_scope, address, &buf[0], buf.size()) == buf.size()) {
    ByteOrder byte_order = eByteOrderInvalid;
    uint32_t addr_size = 0;
    if (GetByteOrderAndAddressSize(exe_scope, address, byte_order, addr_size)) {
      DataExtractor data(&buf.front(), buf.size(), byte_order, addr_size);

      DumpDataExtractor(data, strm,
                        0,                    // Start offset in "data"
                        eFormatHex,           // Print as characters
                        buf.size(),           // Size of item
                        1,                    // Items count
                        UINT32_MAX,           // num per line
                        LLDB_INVALID_ADDRESS, // base address
                        0,                    // bitfield bit size
                        0);                   // bitfield bit offset

      return true;
    }
  }
  return false;
}

static size_t ReadCStringFromMemory(ExecutionContextScope *exe_scope,
                                    const Address &address, Stream *strm) {
  if (exe_scope == nullptr)
    return 0;
  const size_t k_buf_len = 256;
  char buf[k_buf_len + 1];
  buf[k_buf_len] = '\0'; // NULL terminate

  // Byte order and address size don't matter for C string dumping..
  DataExtractor data(buf, sizeof(buf), endian::InlHostByteOrder(), 4);
  size_t total_len = 0;
  size_t bytes_read;
  Address curr_address(address);
  strm->PutChar('"');
  while ((bytes_read = ReadBytes(exe_scope, curr_address, buf, k_buf_len)) >
         0) {
    size_t len = strlen(buf);
    if (len == 0)
      break;
    if (len > bytes_read)
      len = bytes_read;

    DumpDataExtractor(data, strm,
                      0,                    // Start offset in "data"
                      eFormatChar,          // Print as characters
                      1,                    // Size of item (1 byte for a char!)
                      len,                  // How many bytes to print?
                      UINT32_MAX,           // num per line
                      LLDB_INVALID_ADDRESS, // base address
                      0,                    // bitfield bit size

                      0); // bitfield bit offset

    total_len += bytes_read;

    if (len < k_buf_len)
      break;
    curr_address.SetOffset(curr_address.GetOffset() + bytes_read);
  }
  strm->PutChar('"');
  return total_len;
}

Address::Address(lldb::addr_t abs_addr) : m_section_wp(), m_offset(abs_addr) {}

Address::Address(addr_t address, const SectionList *section_list)
    : m_section_wp() {
  ResolveAddressUsingFileSections(address, section_list);
}

const Address &Address::operator=(const Address &rhs) {
  if (this != &rhs) {
    m_section_wp = rhs.m_section_wp;
    m_offset = rhs.m_offset;
  }
  return *this;
}

bool Address::ResolveAddressUsingFileSections(addr_t file_addr,
                                              const SectionList *section_list) {
  if (section_list) {
    SectionSP section_sp(
        section_list->FindSectionContainingFileAddress(file_addr));
    m_section_wp = section_sp;
    if (section_sp) {
      assert(section_sp->ContainsFileAddress(file_addr));
      m_offset = file_addr - section_sp->GetFileAddress();
      return true; // Successfully transformed addr into a section offset
                   // address
    }
  }
  m_offset = file_addr;
  return false; // Failed to resolve this address to a section offset value
}

/// if "addr_range_ptr" is not NULL, then fill in with the address range of the function.
bool Address::ResolveFunctionScope(SymbolContext &sym_ctx,
                                   AddressRange *addr_range_ptr) {
  constexpr SymbolContextItem resolve_scope =
    eSymbolContextFunction | eSymbolContextSymbol;

  if (!(CalculateSymbolContext(&sym_ctx, resolve_scope) & resolve_scope)) {
    if (addr_range_ptr)
      addr_range_ptr->Clear();
   return false;
  }

  if (!addr_range_ptr)
    return true;

  return sym_ctx.GetAddressRange(resolve_scope, 0, false, *addr_range_ptr);
}

ModuleSP Address::GetModule() const {
  lldb::ModuleSP module_sp;
  SectionSP section_sp(GetSection());
  if (section_sp)
    module_sp = section_sp->GetModule();
  return module_sp;
}

addr_t Address::GetFileAddress() const {
  SectionSP section_sp(GetSection());
  if (section_sp) {
    addr_t sect_file_addr = section_sp->GetFileAddress();
    if (sect_file_addr == LLDB_INVALID_ADDRESS) {
      // Section isn't resolved, we can't return a valid file address
      return LLDB_INVALID_ADDRESS;
    }
    // We have a valid file range, so we can return the file based address by
    // adding the file base address to our offset
    return sect_file_addr + m_offset;
  } else if (SectionWasDeletedPrivate()) {
    // Used to have a valid section but it got deleted so the offset doesn't
    // mean anything without the section
    return LLDB_INVALID_ADDRESS;
  }
  // No section, we just return the offset since it is the value in this case
  return m_offset;
}

addr_t Address::GetLoadAddress(Target *target) const {
  SectionSP section_sp(GetSection());
  if (section_sp) {
    if (target) {
      addr_t sect_load_addr = section_sp->GetLoadBaseAddress(target);

      if (sect_load_addr != LLDB_INVALID_ADDRESS) {
        // We have a valid file range, so we can return the file based address
        // by adding the file base address to our offset
        return sect_load_addr + m_offset;
      }
    }
  } else if (SectionWasDeletedPrivate()) {
    // Used to have a valid section but it got deleted so the offset doesn't
    // mean anything without the section
    return LLDB_INVALID_ADDRESS;
  } else {
    // We don't have a section so the offset is the load address
    return m_offset;
  }
  // The section isn't resolved or an invalid target was passed in so we can't
  // return a valid load address.
  return LLDB_INVALID_ADDRESS;
}

addr_t Address::GetCallableLoadAddress(Target *target, bool is_indirect) const {
  addr_t code_addr = LLDB_INVALID_ADDRESS;

  if (is_indirect && target) {
    ProcessSP processSP = target->GetProcessSP();
    Status error;
    if (processSP) {
      code_addr = processSP->ResolveIndirectFunction(this, error);
      if (!error.Success())
        code_addr = LLDB_INVALID_ADDRESS;
    }
  } else {
    code_addr = GetLoadAddress(target);
  }

  if (code_addr == LLDB_INVALID_ADDRESS)
    return code_addr;

  if (target)
    return target->GetCallableLoadAddress(code_addr, GetAddressClass());
  return code_addr;
}

bool Address::SetCallableLoadAddress(lldb::addr_t load_addr, Target *target) {
  if (SetLoadAddress(load_addr, target)) {
    if (target)
      m_offset = target->GetCallableLoadAddress(m_offset, GetAddressClass());
    return true;
  }
  return false;
}

addr_t Address::GetOpcodeLoadAddress(Target *target,
                                     AddressClass addr_class) const {
  addr_t code_addr = GetLoadAddress(target);
  if (code_addr != LLDB_INVALID_ADDRESS) {
    if (addr_class == AddressClass::eInvalid)
      addr_class = GetAddressClass();
    code_addr = target->GetOpcodeLoadAddress(code_addr, addr_class);
  }
  return code_addr;
}

bool Address::SetOpcodeLoadAddress(lldb::addr_t load_addr, Target *target,
                                   AddressClass addr_class,
                                   bool allow_section_end) {
  if (SetLoadAddress(load_addr, target, allow_section_end)) {
    if (target) {
      if (addr_class == AddressClass::eInvalid)
        addr_class = GetAddressClass();
      m_offset = target->GetOpcodeLoadAddress(m_offset, addr_class);
    }
    return true;
  }
  return false;
}

bool Address::GetDescription(Stream &s, Target &target,
                             DescriptionLevel level) const {
  assert(level == eDescriptionLevelBrief &&
         "Non-brief descriptions not implemented");
  LineEntry line_entry;
  if (CalculateSymbolContextLineEntry(line_entry)) {
    s.Printf(" (%s:%u:%u)", line_entry.GetFile().GetFilename().GetCString(),
             line_entry.line, line_entry.column);
    return true;
  }
  return false;
}

bool Address::Dump(Stream *s, ExecutionContextScope *exe_scope, DumpStyle style,
                   DumpStyle fallback_style, uint32_t addr_size,
                   bool all_ranges,
                   std::optional<Stream::HighlightSettings> settings) const {
  // If the section was nullptr, only load address is going to work unless we
  // are trying to deref a pointer
  SectionSP section_sp(GetSection());
  if (!section_sp && style != DumpStyleResolvedPointerDescription)
    style = DumpStyleLoadAddress;

  ExecutionContext exe_ctx(exe_scope);
  Target *target = exe_ctx.GetTargetPtr();
  // If addr_byte_size is UINT32_MAX, then determine the correct address byte
  // size for the process or default to the size of addr_t
  if (addr_size == UINT32_MAX) {
    if (target)
      addr_size = target->GetArchitecture().GetAddressByteSize();
    else
      addr_size = sizeof(addr_t);
  }

  Address so_addr;
  switch (style) {
  case DumpStyleInvalid:
    return false;

  case DumpStyleSectionNameOffset:
    if (section_sp) {
      section_sp->DumpName(s->AsRawOstream());
      s->Printf(" + %" PRIu64, m_offset);
    } else {
      DumpAddress(s->AsRawOstream(), m_offset, addr_size);
    }
    break;

  case DumpStyleSectionPointerOffset:
    s->Printf("(Section *)%p + ", static_cast<void *>(section_sp.get()));
    DumpAddress(s->AsRawOstream(), m_offset, addr_size);
    break;

  case DumpStyleModuleWithFileAddress:
    if (section_sp) {
      ModuleSP module_sp = section_sp->GetModule();
      if (module_sp)
        s->Printf("%s[", module_sp->GetFileSpec().GetFilename().AsCString(
                             "<Unknown>"));
      else
        s->Printf("%s[", "<Unknown>");
    }
    [[fallthrough]];
  case DumpStyleFileAddress: {
    addr_t file_addr = GetFileAddress();
    if (file_addr == LLDB_INVALID_ADDRESS) {
      if (fallback_style != DumpStyleInvalid)
        return Dump(s, exe_scope, fallback_style, DumpStyleInvalid, addr_size);
      return false;
    }
    DumpAddress(s->AsRawOstream(), file_addr, addr_size);
    if (style == DumpStyleModuleWithFileAddress && section_sp)
      s->PutChar(']');
  } break;

  case DumpStyleLoadAddress: {
    addr_t load_addr = GetLoadAddress(target);

    /*
     * MIPS:
     * Display address in compressed form for MIPS16 or microMIPS
     * if the address belongs to AddressClass::eCodeAlternateISA.
    */
    if (target) {
      const llvm::Triple::ArchType llvm_arch =
          target->GetArchitecture().GetMachine();
      if (llvm_arch == llvm::Triple::mips ||
          llvm_arch == llvm::Triple::mipsel ||
          llvm_arch == llvm::Triple::mips64 ||
          llvm_arch == llvm::Triple::mips64el)
        load_addr = GetCallableLoadAddress(target);
    }

    if (load_addr == LLDB_INVALID_ADDRESS) {
      if (fallback_style != DumpStyleInvalid)
        return Dump(s, exe_scope, fallback_style, DumpStyleInvalid, addr_size);
      return false;
    }
    DumpAddress(s->AsRawOstream(), load_addr, addr_size);
  } break;

  case DumpStyleResolvedDescription:
  case DumpStyleResolvedDescriptionNoModule:
  case DumpStyleResolvedDescriptionNoFunctionArguments:
  case DumpStyleNoFunctionName:
    if (IsSectionOffset()) {
      uint32_t pointer_size = 4;
      ModuleSP module_sp(GetModule());
      if (target)
        pointer_size = target->GetArchitecture().GetAddressByteSize();
      else if (module_sp)
        pointer_size = module_sp->GetArchitecture().GetAddressByteSize();
      bool showed_info = false;
      if (section_sp) {
        SectionType sect_type = section_sp->GetType();
        switch (sect_type) {
        case eSectionTypeData:
          if (module_sp) {
            if (Symtab *symtab = module_sp->GetSymtab()) {
              const addr_t file_Addr = GetFileAddress();
              Symbol *symbol =
                  symtab->FindSymbolContainingFileAddress(file_Addr);
              if (symbol) {
                const char *symbol_name = symbol->GetName().AsCString();
                if (symbol_name) {
                  s->PutCStringColorHighlighted(symbol_name, settings);
                  addr_t delta =
                      file_Addr - symbol->GetAddressRef().GetFileAddress();
                  if (delta)
                    s->Printf(" + %" PRIu64, delta);
                  showed_info = true;
                }
              }
            }
          }
          break;

        case eSectionTypeDataCString:
          // Read the C string from memory and display it
          showed_info = true;
          ReadCStringFromMemory(exe_scope, *this, s);
          break;

        case eSectionTypeDataCStringPointers:
          if (ReadAddress(exe_scope, *this, pointer_size, so_addr)) {
#if VERBOSE_OUTPUT
            s->PutCString("(char *)");
            so_addr.Dump(s, exe_scope, DumpStyleLoadAddress,
                         DumpStyleFileAddress);
            s->PutCString(": ");
#endif
            showed_info = true;
            ReadCStringFromMemory(exe_scope, so_addr, s);
          }
          break;

        case eSectionTypeDataObjCMessageRefs:
          if (ReadAddress(exe_scope, *this, pointer_size, so_addr)) {
            if (target && so_addr.IsSectionOffset()) {
              SymbolContext func_sc;
              target->GetImages().ResolveSymbolContextForAddress(
                  so_addr, eSymbolContextEverything, func_sc);
              if (func_sc.function != nullptr || func_sc.symbol != nullptr) {
                showed_info = true;
#if VERBOSE_OUTPUT
                s->PutCString("(objc_msgref *) -> { (func*)");
                so_addr.Dump(s, exe_scope, DumpStyleLoadAddress,
                             DumpStyleFileAddress);
#else
                s->PutCString("{ ");
#endif
                Address cstr_addr(*this);
                cstr_addr.SetOffset(cstr_addr.GetOffset() + pointer_size);
                func_sc.DumpStopContext(s, exe_scope, so_addr, true, true,
                                        false, true, true);
                if (ReadAddress(exe_scope, cstr_addr, pointer_size, so_addr)) {
#if VERBOSE_OUTPUT
                  s->PutCString("), (char *)");
                  so_addr.Dump(s, exe_scope, DumpStyleLoadAddress,
                               DumpStyleFileAddress);
                  s->PutCString(" (");
#else
                  s->PutCString(", ");
#endif
                  ReadCStringFromMemory(exe_scope, so_addr, s);
                }
#if VERBOSE_OUTPUT
                s->PutCString(") }");
#else
                s->PutCString(" }");
#endif
              }
            }
          }
          break;

        case eSectionTypeDataObjCCFStrings: {
          Address cfstring_data_addr(*this);
          cfstring_data_addr.SetOffset(cfstring_data_addr.GetOffset() +
                                       (2 * pointer_size));
          if (ReadAddress(exe_scope, cfstring_data_addr, pointer_size,
                          so_addr)) {
#if VERBOSE_OUTPUT
            s->PutCString("(CFString *) ");
            cfstring_data_addr.Dump(s, exe_scope, DumpStyleLoadAddress,
                                    DumpStyleFileAddress);
            s->PutCString(" -> @");
#else
            s->PutChar('@');
#endif
            if (so_addr.Dump(s, exe_scope, DumpStyleResolvedDescription))
              showed_info = true;
          }
        } break;

        case eSectionTypeData4:
          // Read the 4 byte data and display it
          showed_info = true;
          s->PutCString("(uint32_t) ");
          DumpUInt(exe_scope, *this, 4, s);
          break;

        case eSectionTypeData8:
          // Read the 8 byte data and display it
          showed_info = true;
          s->PutCString("(uint64_t) ");
          DumpUInt(exe_scope, *this, 8, s);
          break;

        case eSectionTypeData16:
          // Read the 16 byte data and display it
          showed_info = true;
          s->PutCString("(uint128_t) ");
          DumpUInt(exe_scope, *this, 16, s);
          break;

        case eSectionTypeDataPointers:
          // Read the pointer data and display it
          if (ReadAddress(exe_scope, *this, pointer_size, so_addr)) {
            s->PutCString("(void *)");
            so_addr.Dump(s, exe_scope, DumpStyleLoadAddress,
                         DumpStyleFileAddress);

            showed_info = true;
            if (so_addr.IsSectionOffset()) {
              SymbolContext pointer_sc;
              if (target) {
                target->GetImages().ResolveSymbolContextForAddress(
                    so_addr, eSymbolContextEverything, pointer_sc);
                if (pointer_sc.function != nullptr ||
                    pointer_sc.symbol != nullptr) {
                  s->PutCString(": ");
                  pointer_sc.DumpStopContext(s, exe_scope, so_addr, true, false,
                                             false, true, true, false,
                                             settings);
                }
              }
            }
          }
          break;

        default:
          break;
        }
      }

      if (!showed_info) {
        if (module_sp) {
          SymbolContext sc;
          module_sp->ResolveSymbolContextForAddress(
              *this, eSymbolContextEverything, sc);
          if (sc.function || sc.symbol) {
            bool show_stop_context = true;
            const bool show_module = (style == DumpStyleResolvedDescription);
            const bool show_fullpaths = false;
            const bool show_inlined_frames = true;
            const bool show_function_arguments =
                (style != DumpStyleResolvedDescriptionNoFunctionArguments);
            const bool show_function_name = (style != DumpStyleNoFunctionName);
            if (sc.function == nullptr && sc.symbol != nullptr) {
              // If we have just a symbol make sure it is in the right section
              if (sc.symbol->ValueIsAddress()) {
                if (sc.symbol->GetAddressRef().GetSection() != GetSection()) {
                  // don't show the module if the symbol is a trampoline symbol
                  show_stop_context = false;
                }
              }
            }
            if (show_stop_context) {
              // We have a function or a symbol from the same sections as this
              // address.
              sc.DumpStopContext(s, exe_scope, *this, show_fullpaths,
                                 show_module, show_inlined_frames,
                                 show_function_arguments, show_function_name,
                                 false, settings);
            } else {
              // We found a symbol but it was in a different section so it
              // isn't the symbol we should be showing, just show the section
              // name + offset
              Dump(s, exe_scope, DumpStyleSectionNameOffset, DumpStyleInvalid,
                   UINT32_MAX, false, settings);
            }
          }
        }
      }
    } else {
      if (fallback_style != DumpStyleInvalid)
        return Dump(s, exe_scope, fallback_style, DumpStyleInvalid, addr_size,
                    false, settings);
      return false;
    }
    break;

  case DumpStyleDetailedSymbolContext:
    if (IsSectionOffset()) {
      ModuleSP module_sp(GetModule());
      if (module_sp) {
        SymbolContext sc;
        module_sp->ResolveSymbolContextForAddress(
            *this, eSymbolContextEverything | eSymbolContextVariable, sc);
        if (sc.symbol) {
          // If we have just a symbol make sure it is in the same section as
          // our address. If it isn't, then we might have just found the last
          // symbol that came before the address that we are looking up that
          // has nothing to do with our address lookup.
          if (sc.symbol->ValueIsAddress() &&
              sc.symbol->GetAddressRef().GetSection() != GetSection())
            sc.symbol = nullptr;
        }
        sc.GetDescription(s, eDescriptionLevelBrief, target, settings);

        if (sc.block) {
          bool can_create = true;
          bool get_parent_variables = true;
          bool stop_if_block_is_inlined_function = false;
          VariableList variable_list;
          addr_t file_addr = GetFileAddress();
          sc.block->AppendVariables(
              can_create, get_parent_variables,
              stop_if_block_is_inlined_function,
              [&](Variable *var) {
                return var && var->LocationIsValidForAddress(*this);
              },
              &variable_list);
          ABISP abi =
              ABI::FindPlugin(ProcessSP(), module_sp->GetArchitecture());
          for (const VariableSP &var_sp : variable_list) {
            s->Indent();
            s->Printf("   Variable: id = {0x%8.8" PRIx64 "}, name = \"%s\"",
                      var_sp->GetID(), var_sp->GetName().GetCString());
            Type *type = var_sp->GetType();
            if (type)
              s->Printf(", type = \"%s\"", type->GetName().GetCString());
            else
              s->PutCString(", type = <unknown>");
            s->PutCString(", valid ranges = ");
            if (var_sp->GetScopeRange().IsEmpty())
              s->PutCString("<block>");
            else if (all_ranges) {
              for (auto range : var_sp->GetScopeRange())
                DumpAddressRange(s->AsRawOstream(), range.GetRangeBase(),
                                 range.GetRangeEnd(), addr_size);
            } else if (auto *range =
                           var_sp->GetScopeRange().FindEntryThatContains(
                               file_addr))
              DumpAddressRange(s->AsRawOstream(), range->GetRangeBase(),
                               range->GetRangeEnd(), addr_size);
            s->PutCString(", location = ");
            var_sp->DumpLocations(s, all_ranges ? LLDB_INVALID_ADDRESS : *this);
            s->PutCString(", decl = ");
            var_sp->GetDeclaration().DumpStopContext(s, false);
            s->EOL();
          }
        }
      }
    } else {
      if (fallback_style != DumpStyleInvalid)
        return Dump(s, exe_scope, fallback_style, DumpStyleInvalid, addr_size,
                    false, settings);
      return false;
    }
    break;

  case DumpStyleResolvedPointerDescription: {
    Process *process = exe_ctx.GetProcessPtr();
    if (process) {
      addr_t load_addr = GetLoadAddress(target);
      if (load_addr != LLDB_INVALID_ADDRESS) {
        Status memory_error;
        addr_t dereferenced_load_addr =
            process->ReadPointerFromMemory(load_addr, memory_error);
        if (dereferenced_load_addr != LLDB_INVALID_ADDRESS) {
          Address dereferenced_addr;
          if (dereferenced_addr.SetLoadAddress(dereferenced_load_addr,
                                               target)) {
            StreamString strm;
            if (dereferenced_addr.Dump(&strm, exe_scope,
                                       DumpStyleResolvedDescription,
                                       DumpStyleInvalid, addr_size)) {
              DumpAddress(s->AsRawOstream(), dereferenced_load_addr, addr_size,
                          " -> ", " ");
              s->Write(strm.GetString().data(), strm.GetSize());
              return true;
            }
          }
        }
      }
    }
    if (fallback_style != DumpStyleInvalid)
      return Dump(s, exe_scope, fallback_style, DumpStyleInvalid, addr_size);
    return false;
  } break;
  }

  return true;
}

bool Address::SectionWasDeleted() const {
  if (GetSection())
    return false;
  return SectionWasDeletedPrivate();
}

bool Address::SectionWasDeletedPrivate() const {
  lldb::SectionWP empty_section_wp;

  // If either call to "std::weak_ptr::owner_before(...) value returns true,
  // this indicates that m_section_wp once contained (possibly still does) a
  // reference to a valid shared pointer. This helps us know if we had a valid
  // reference to a section which is now invalid because the module it was in
  // was unloaded/deleted, or if the address doesn't have a valid reference to
  // a section.
  return empty_section_wp.owner_before(m_section_wp) ||
         m_section_wp.owner_before(empty_section_wp);
}

uint32_t
Address::CalculateSymbolContext(SymbolContext *sc,
                                SymbolContextItem resolve_scope) const {
  sc->Clear(false);
  // Absolute addresses don't have enough information to reconstruct even their
  // target.

  SectionSP section_sp(GetSection());
  if (section_sp) {
    ModuleSP module_sp(section_sp->GetModule());
    if (module_sp) {
      sc->module_sp = module_sp;
      if (sc->module_sp)
        return sc->module_sp->ResolveSymbolContextForAddress(
            *this, resolve_scope, *sc);
    }
  }
  return 0;
}

ModuleSP Address::CalculateSymbolContextModule() const {
  SectionSP section_sp(GetSection());
  if (section_sp)
    return section_sp->GetModule();
  return ModuleSP();
}

CompileUnit *Address::CalculateSymbolContextCompileUnit() const {
  SectionSP section_sp(GetSection());
  if (section_sp) {
    SymbolContext sc;
    sc.module_sp = section_sp->GetModule();
    if (sc.module_sp) {
      sc.module_sp->ResolveSymbolContextForAddress(*this,
                                                   eSymbolContextCompUnit, sc);
      return sc.comp_unit;
    }
  }
  return nullptr;
}

Function *Address::CalculateSymbolContextFunction() const {
  SectionSP section_sp(GetSection());
  if (section_sp) {
    SymbolContext sc;
    sc.module_sp = section_sp->GetModule();
    if (sc.module_sp) {
      sc.module_sp->ResolveSymbolContextForAddress(*this,
                                                   eSymbolContextFunction, sc);
      return sc.function;
    }
  }
  return nullptr;
}

Block *Address::CalculateSymbolContextBlock() const {
  SectionSP section_sp(GetSection());
  if (section_sp) {
    SymbolContext sc;
    sc.module_sp = section_sp->GetModule();
    if (sc.module_sp) {
      sc.module_sp->ResolveSymbolContextForAddress(*this, eSymbolContextBlock,
                                                   sc);
      return sc.block;
    }
  }
  return nullptr;
}

Symbol *Address::CalculateSymbolContextSymbol() const {
  SectionSP section_sp(GetSection());
  if (section_sp) {
    SymbolContext sc;
    sc.module_sp = section_sp->GetModule();
    if (sc.module_sp) {
      sc.module_sp->ResolveSymbolContextForAddress(*this, eSymbolContextSymbol,
                                                   sc);
      return sc.symbol;
    }
  }
  return nullptr;
}

bool Address::CalculateSymbolContextLineEntry(LineEntry &line_entry) const {
  SectionSP section_sp(GetSection());
  if (section_sp) {
    SymbolContext sc;
    sc.module_sp = section_sp->GetModule();
    if (sc.module_sp) {
      sc.module_sp->ResolveSymbolContextForAddress(*this,
                                                   eSymbolContextLineEntry, sc);
      if (sc.line_entry.IsValid()) {
        line_entry = sc.line_entry;
        return true;
      }
    }
  }
  line_entry.Clear();
  return false;
}

int Address::CompareFileAddress(const Address &a, const Address &b) {
  addr_t a_file_addr = a.GetFileAddress();
  addr_t b_file_addr = b.GetFileAddress();
  if (a_file_addr < b_file_addr)
    return -1;
  if (a_file_addr > b_file_addr)
    return +1;
  return 0;
}

int Address::CompareLoadAddress(const Address &a, const Address &b,
                                Target *target) {
  assert(target != nullptr);
  addr_t a_load_addr = a.GetLoadAddress(target);
  addr_t b_load_addr = b.GetLoadAddress(target);
  if (a_load_addr < b_load_addr)
    return -1;
  if (a_load_addr > b_load_addr)
    return +1;
  return 0;
}

int Address::CompareModulePointerAndOffset(const Address &a, const Address &b) {
  ModuleSP a_module_sp(a.GetModule());
  ModuleSP b_module_sp(b.GetModule());
  Module *a_module = a_module_sp.get();
  Module *b_module = b_module_sp.get();
  if (a_module < b_module)
    return -1;
  if (a_module > b_module)
    return +1;
  // Modules are the same, just compare the file address since they should be
  // unique
  addr_t a_file_addr = a.GetFileAddress();
  addr_t b_file_addr = b.GetFileAddress();
  if (a_file_addr < b_file_addr)
    return -1;
  if (a_file_addr > b_file_addr)
    return +1;
  return 0;
}

size_t Address::MemorySize() const {
  // Noting special for the memory size of a single Address object, it is just
  // the size of itself.
  return sizeof(Address);
}

// NOTE: Be careful using this operator. It can correctly compare two
// addresses from the same Module correctly. It can't compare two addresses
// from different modules in any meaningful way, but it will compare the module
// pointers.
//
// To sum things up:
// - works great for addresses within the same module - it works for addresses
// across multiple modules, but don't expect the
//   address results to make much sense
//
// This basically lets Address objects be used in ordered collection classes.

bool lldb_private::operator<(const Address &lhs, const Address &rhs) {
  ModuleSP lhs_module_sp(lhs.GetModule());
  ModuleSP rhs_module_sp(rhs.GetModule());
  Module *lhs_module = lhs_module_sp.get();
  Module *rhs_module = rhs_module_sp.get();
  if (lhs_module == rhs_module) {
    // Addresses are in the same module, just compare the file addresses
    return lhs.GetFileAddress() < rhs.GetFileAddress();
  } else {
    // The addresses are from different modules, just use the module pointer
    // value to get consistent ordering
    return lhs_module < rhs_module;
  }
}

bool lldb_private::operator>(const Address &lhs, const Address &rhs) {
  ModuleSP lhs_module_sp(lhs.GetModule());
  ModuleSP rhs_module_sp(rhs.GetModule());
  Module *lhs_module = lhs_module_sp.get();
  Module *rhs_module = rhs_module_sp.get();
  if (lhs_module == rhs_module) {
    // Addresses are in the same module, just compare the file addresses
    return lhs.GetFileAddress() > rhs.GetFileAddress();
  } else {
    // The addresses are from different modules, just use the module pointer
    // value to get consistent ordering
    return lhs_module > rhs_module;
  }
}

// The operator == checks for exact equality only (same section, same offset)
bool lldb_private::operator==(const Address &a, const Address &rhs) {
  return a.GetOffset() == rhs.GetOffset() && a.GetSection() == rhs.GetSection();
}

// The operator != checks for exact inequality only (differing section, or
// different offset)
bool lldb_private::operator!=(const Address &a, const Address &rhs) {
  return a.GetOffset() != rhs.GetOffset() || a.GetSection() != rhs.GetSection();
}

AddressClass Address::GetAddressClass() const {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    ObjectFile *obj_file = module_sp->GetObjectFile();
    if (obj_file) {
      // Give the symbol file a chance to add to the unified section list
      // and to the symtab.
      module_sp->GetSymtab();
      return obj_file->GetAddressClass(GetFileAddress());
    }
  }
  return AddressClass::eUnknown;
}

bool Address::SetLoadAddress(lldb::addr_t load_addr, Target *target,
                             bool allow_section_end) {
  if (target && target->GetSectionLoadList().ResolveLoadAddress(
                    load_addr, *this, allow_section_end))
    return true;
  m_section_wp.reset();
  m_offset = load_addr;
  return false;
}
