//===-- Symbol.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/Symbol.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/Stream.h"
#include "llvm/ADT/StringSwitch.h"

using namespace lldb;
using namespace lldb_private;

Symbol::Symbol()
    : SymbolContextScope(), m_type_data_resolved(false), m_is_synthetic(false),
      m_is_debug(false), m_is_external(false), m_size_is_sibling(false),
      m_size_is_synthesized(false), m_size_is_valid(false),
      m_demangled_is_synthesized(false), m_contains_linker_annotations(false),
      m_is_weak(false), m_type(eSymbolTypeInvalid), m_mangled(),
      m_addr_range() {}

Symbol::Symbol(uint32_t symID, llvm::StringRef name, SymbolType type,
               bool external, bool is_debug, bool is_trampoline,
               bool is_artificial, const lldb::SectionSP &section_sp,
               addr_t offset, addr_t size, bool size_is_valid,
               bool contains_linker_annotations, uint32_t flags)
    : SymbolContextScope(), m_uid(symID), m_type_data_resolved(false),
      m_is_synthetic(is_artificial), m_is_debug(is_debug),
      m_is_external(external), m_size_is_sibling(false),
      m_size_is_synthesized(false), m_size_is_valid(size_is_valid || size > 0),
      m_demangled_is_synthesized(false),
      m_contains_linker_annotations(contains_linker_annotations),
      m_is_weak(false), m_type(type), m_mangled(name),
      m_addr_range(section_sp, offset, size), m_flags(flags) {}

Symbol::Symbol(uint32_t symID, const Mangled &mangled, SymbolType type,
               bool external, bool is_debug, bool is_trampoline,
               bool is_artificial, const AddressRange &range,
               bool size_is_valid, bool contains_linker_annotations,
               uint32_t flags)
    : SymbolContextScope(), m_uid(symID), m_type_data_resolved(false),
      m_is_synthetic(is_artificial), m_is_debug(is_debug),
      m_is_external(external), m_size_is_sibling(false),
      m_size_is_synthesized(false),
      m_size_is_valid(size_is_valid || range.GetByteSize() > 0),
      m_demangled_is_synthesized(false),
      m_contains_linker_annotations(contains_linker_annotations),
      m_is_weak(false), m_type(type), m_mangled(mangled), m_addr_range(range),
      m_flags(flags) {}

Symbol::Symbol(const Symbol &rhs)
    : SymbolContextScope(rhs), m_uid(rhs.m_uid), m_type_data(rhs.m_type_data),
      m_type_data_resolved(rhs.m_type_data_resolved),
      m_is_synthetic(rhs.m_is_synthetic), m_is_debug(rhs.m_is_debug),
      m_is_external(rhs.m_is_external),
      m_size_is_sibling(rhs.m_size_is_sibling), m_size_is_synthesized(false),
      m_size_is_valid(rhs.m_size_is_valid),
      m_demangled_is_synthesized(rhs.m_demangled_is_synthesized),
      m_contains_linker_annotations(rhs.m_contains_linker_annotations),
      m_is_weak(rhs.m_is_weak), m_type(rhs.m_type), m_mangled(rhs.m_mangled),
      m_addr_range(rhs.m_addr_range), m_flags(rhs.m_flags) {}

const Symbol &Symbol::operator=(const Symbol &rhs) {
  if (this != &rhs) {
    SymbolContextScope::operator=(rhs);
    m_uid = rhs.m_uid;
    m_type_data = rhs.m_type_data;
    m_type_data_resolved = rhs.m_type_data_resolved;
    m_is_synthetic = rhs.m_is_synthetic;
    m_is_debug = rhs.m_is_debug;
    m_is_external = rhs.m_is_external;
    m_size_is_sibling = rhs.m_size_is_sibling;
    m_size_is_synthesized = rhs.m_size_is_sibling;
    m_size_is_valid = rhs.m_size_is_valid;
    m_demangled_is_synthesized = rhs.m_demangled_is_synthesized;
    m_contains_linker_annotations = rhs.m_contains_linker_annotations;
    m_is_weak = rhs.m_is_weak;
    m_type = rhs.m_type;
    m_mangled = rhs.m_mangled;
    m_addr_range = rhs.m_addr_range;
    m_flags = rhs.m_flags;
  }
  return *this;
}

llvm::Expected<Symbol> Symbol::FromJSON(const JSONSymbol &symbol,
                                        SectionList *section_list) {
  if (!section_list)
    return llvm::createStringError("no section list provided");

  if (!symbol.value && !symbol.address)
    return llvm::createStringError(
        "symbol must contain either a value or an address");

  if (symbol.value && symbol.address)
    return llvm::createStringError(
        "symbol cannot contain both a value and an address");

  const uint64_t size = symbol.size.value_or(0);
  const bool is_artificial = false;
  const bool is_trampoline = false;
  const bool is_debug = false;
  const bool external = false;
  const bool size_is_valid = symbol.size.has_value();
  const bool contains_linker_annotations = false;
  const uint32_t flags = 0;

  if (symbol.address) {
    if (SectionSP section_sp =
            section_list->FindSectionContainingFileAddress(*symbol.address)) {
      const uint64_t offset = *symbol.address - section_sp->GetFileAddress();
      return Symbol(symbol.id.value_or(0), Mangled(symbol.name),
                    symbol.type.value_or(eSymbolTypeAny), external, is_debug,
                    is_trampoline, is_artificial,
                    AddressRange(section_sp, offset, size), size_is_valid,
                    contains_linker_annotations, flags);
    }
    return llvm::createStringError(
        llvm::formatv("no section found for address: {0:x}", *symbol.address));
  }

  // Absolute symbols encode the integer value in the m_offset of the
  // AddressRange object and the section is set to nothing.
  return Symbol(symbol.id.value_or(0), Mangled(symbol.name),
                symbol.type.value_or(eSymbolTypeAny), external, is_debug,
                is_trampoline, is_artificial,
                AddressRange(SectionSP(), *symbol.value, size), size_is_valid,
                contains_linker_annotations, flags);
}

void Symbol::Clear() {
  m_uid = UINT32_MAX;
  m_mangled.Clear();
  m_type_data = 0;
  m_type_data_resolved = false;
  m_is_synthetic = false;
  m_is_debug = false;
  m_is_external = false;
  m_size_is_sibling = false;
  m_size_is_synthesized = false;
  m_size_is_valid = false;
  m_demangled_is_synthesized = false;
  m_contains_linker_annotations = false;
  m_is_weak = false;
  m_type = eSymbolTypeInvalid;
  m_flags = 0;
  m_addr_range.Clear();
}

bool Symbol::ValueIsAddress() const {
  return (bool)m_addr_range.GetBaseAddress().GetSection();
}

ConstString Symbol::GetDisplayName() const {
  return GetMangled().GetDisplayDemangledName();
}

ConstString Symbol::GetReExportedSymbolName() const {
  if (m_type == eSymbolTypeReExported) {
    // For eSymbolTypeReExported, the "const char *" from a ConstString is used
    // as the offset in the address range base address. We can then make this
    // back into a string that is the re-exported name.
    intptr_t str_ptr = m_addr_range.GetBaseAddress().GetOffset();
    if (str_ptr != 0)
      return ConstString((const char *)str_ptr);
    else
      return GetName();
  }
  return ConstString();
}

FileSpec Symbol::GetReExportedSymbolSharedLibrary() const {
  if (m_type == eSymbolTypeReExported) {
    // For eSymbolTypeReExported, the "const char *" from a ConstString is used
    // as the offset in the address range base address. We can then make this
    // back into a string that is the re-exported name.
    intptr_t str_ptr = m_addr_range.GetByteSize();
    if (str_ptr != 0)
      return FileSpec((const char *)str_ptr);
  }
  return FileSpec();
}

void Symbol::SetReExportedSymbolName(ConstString name) {
  SetType(eSymbolTypeReExported);
  // For eSymbolTypeReExported, the "const char *" from a ConstString is used
  // as the offset in the address range base address.
  m_addr_range.GetBaseAddress().SetOffset((uintptr_t)name.GetCString());
}

bool Symbol::SetReExportedSymbolSharedLibrary(const FileSpec &fspec) {
  if (m_type == eSymbolTypeReExported) {
    // For eSymbolTypeReExported, the "const char *" from a ConstString is used
    // as the offset in the address range base address.
    m_addr_range.SetByteSize(
        (uintptr_t)ConstString(fspec.GetPath().c_str()).GetCString());
    return true;
  }
  return false;
}

uint32_t Symbol::GetSiblingIndex() const {
  return m_size_is_sibling ? m_addr_range.GetByteSize() : UINT32_MAX;
}

bool Symbol::IsTrampoline() const { return m_type == eSymbolTypeTrampoline; }

bool Symbol::IsIndirect() const { return m_type == eSymbolTypeResolver; }

void Symbol::GetDescription(
    Stream *s, lldb::DescriptionLevel level, Target *target,
    std::optional<Stream::HighlightSettings> settings) const {
  s->Printf("id = {0x%8.8x}", m_uid);

  if (m_addr_range.GetBaseAddress().GetSection()) {
    if (ValueIsAddress()) {
      const lldb::addr_t byte_size = GetByteSize();
      if (byte_size > 0) {
        s->PutCString(", range = ");
        m_addr_range.Dump(s, target, Address::DumpStyleLoadAddress,
                          Address::DumpStyleFileAddress);
      } else {
        s->PutCString(", address = ");
        m_addr_range.GetBaseAddress().Dump(s, target,
                                           Address::DumpStyleLoadAddress,
                                           Address::DumpStyleFileAddress);
      }
    } else
      s->Printf(", value = 0x%16.16" PRIx64,
                m_addr_range.GetBaseAddress().GetOffset());
  } else {
    if (m_size_is_sibling)
      s->Printf(", sibling = %5" PRIu64,
                m_addr_range.GetBaseAddress().GetOffset());
    else
      s->Printf(", value = 0x%16.16" PRIx64,
                m_addr_range.GetBaseAddress().GetOffset());
  }
  if (ConstString demangled = m_mangled.GetDemangledName()) {
    s->PutCString(", name=\"");
    s->PutCStringColorHighlighted(demangled.GetStringRef(), settings);
    s->PutCString("\"");
  }
  if (ConstString mangled_name = m_mangled.GetMangledName()) {
    s->PutCString(", mangled=\"");
    s->PutCStringColorHighlighted(mangled_name.GetStringRef(), settings);
    s->PutCString("\"");
  }
}

void Symbol::Dump(Stream *s, Target *target, uint32_t index,
                  Mangled::NamePreference name_preference) const {
  s->Printf("[%5u] %6u %c%c%c %-15s ", index, GetID(), m_is_debug ? 'D' : ' ',
            m_is_synthetic ? 'S' : ' ', m_is_external ? 'X' : ' ',
            GetTypeAsString());

  // Make sure the size of the symbol is up to date before dumping
  GetByteSize();

  ConstString name = GetMangled().GetName(name_preference);
  if (ValueIsAddress()) {
    if (!m_addr_range.GetBaseAddress().Dump(s, nullptr,
                                            Address::DumpStyleFileAddress))
      s->Printf("%*s", 18, "");

    s->PutChar(' ');

    if (!m_addr_range.GetBaseAddress().Dump(s, target,
                                            Address::DumpStyleLoadAddress))
      s->Printf("%*s", 18, "");

    const char *format = m_size_is_sibling ? " Sibling -> [%5llu] 0x%8.8x %s\n"
                                           : " 0x%16.16" PRIx64 " 0x%8.8x %s\n";
    s->Printf(format, GetByteSize(), m_flags, name.AsCString(""));
  } else if (m_type == eSymbolTypeReExported) {
    s->Printf(
        "                                                         0x%8.8x %s",
        m_flags, name.AsCString(""));

    ConstString reexport_name = GetReExportedSymbolName();
    intptr_t shlib = m_addr_range.GetByteSize();
    if (shlib)
      s->Printf(" -> %s`%s\n", (const char *)shlib, reexport_name.GetCString());
    else
      s->Printf(" -> %s\n", reexport_name.GetCString());
  } else {
    const char *format =
        m_size_is_sibling
            ? "0x%16.16" PRIx64
              "                    Sibling -> [%5llu] 0x%8.8x %s\n"
            : "0x%16.16" PRIx64 "                    0x%16.16" PRIx64
              " 0x%8.8x %s\n";
    s->Printf(format, m_addr_range.GetBaseAddress().GetOffset(), GetByteSize(),
              m_flags, name.AsCString(""));
  }
}

uint32_t Symbol::GetPrologueByteSize() {
  if (m_type == eSymbolTypeCode || m_type == eSymbolTypeResolver) {
    if (!m_type_data_resolved) {
      m_type_data_resolved = true;

      const Address &base_address = m_addr_range.GetBaseAddress();
      Function *function = base_address.CalculateSymbolContextFunction();
      if (function) {
        // Functions have line entries which can also potentially have end of
        // prologue information. So if this symbol points to a function, use
        // the prologue information from there.
        m_type_data = function->GetPrologueByteSize();
      } else {
        ModuleSP module_sp(base_address.GetModule());
        SymbolContext sc;
        if (module_sp) {
          uint32_t resolved_flags = module_sp->ResolveSymbolContextForAddress(
              base_address, eSymbolContextLineEntry, sc);
          if (resolved_flags & eSymbolContextLineEntry) {
            // Default to the end of the first line entry.
            m_type_data = sc.line_entry.range.GetByteSize();

            // Set address for next line.
            Address addr(base_address);
            addr.Slide(m_type_data);

            // Check the first few instructions and look for one that has a
            // line number that is different than the first entry. This is also
            // done in Function::GetPrologueByteSize().
            uint16_t total_offset = m_type_data;
            for (int idx = 0; idx < 6; ++idx) {
              SymbolContext sc_temp;
              resolved_flags = module_sp->ResolveSymbolContextForAddress(
                  addr, eSymbolContextLineEntry, sc_temp);
              // Make sure we got line number information...
              if (!(resolved_flags & eSymbolContextLineEntry))
                break;

              // If this line number is different than our first one, use it
              // and we're done.
              if (sc_temp.line_entry.line != sc.line_entry.line) {
                m_type_data = total_offset;
                break;
              }

              // Slide addr up to the next line address.
              addr.Slide(sc_temp.line_entry.range.GetByteSize());
              total_offset += sc_temp.line_entry.range.GetByteSize();
              // If we've gone too far, bail out.
              if (total_offset >= m_addr_range.GetByteSize())
                break;
            }

            // Sanity check - this may be a function in the middle of code that
            // has debug information, but not for this symbol.  So the line
            // entries surrounding us won't lie inside our function. In that
            // case, the line entry will be bigger than we are, so we do that
            // quick check and if that is true, we just return 0.
            if (m_type_data >= m_addr_range.GetByteSize())
              m_type_data = 0;
          } else {
            // TODO: expose something in Process to figure out the
            // size of a function prologue.
            m_type_data = 0;
          }
        }
      }
    }
    return m_type_data;
  }
  return 0;
}

bool Symbol::Compare(ConstString name, SymbolType type) const {
  if (type == eSymbolTypeAny || m_type == type) {
    const Mangled &mangled = GetMangled();
    return mangled.GetMangledName() == name ||
           mangled.GetDemangledName() == name;
  }
  return false;
}

#define ENUM_TO_CSTRING(x)                                                     \
  case eSymbolType##x:                                                         \
    return #x;

const char *Symbol::GetTypeAsString() const {
  switch (m_type) {
    ENUM_TO_CSTRING(Invalid);
    ENUM_TO_CSTRING(Absolute);
    ENUM_TO_CSTRING(Code);
    ENUM_TO_CSTRING(Resolver);
    ENUM_TO_CSTRING(Data);
    ENUM_TO_CSTRING(Trampoline);
    ENUM_TO_CSTRING(Runtime);
    ENUM_TO_CSTRING(Exception);
    ENUM_TO_CSTRING(SourceFile);
    ENUM_TO_CSTRING(HeaderFile);
    ENUM_TO_CSTRING(ObjectFile);
    ENUM_TO_CSTRING(CommonBlock);
    ENUM_TO_CSTRING(Block);
    ENUM_TO_CSTRING(Local);
    ENUM_TO_CSTRING(Param);
    ENUM_TO_CSTRING(Variable);
    ENUM_TO_CSTRING(VariableType);
    ENUM_TO_CSTRING(LineEntry);
    ENUM_TO_CSTRING(LineHeader);
    ENUM_TO_CSTRING(ScopeBegin);
    ENUM_TO_CSTRING(ScopeEnd);
    ENUM_TO_CSTRING(Additional);
    ENUM_TO_CSTRING(Compiler);
    ENUM_TO_CSTRING(Instrumentation);
    ENUM_TO_CSTRING(Undefined);
    ENUM_TO_CSTRING(ObjCClass);
    ENUM_TO_CSTRING(ObjCMetaClass);
    ENUM_TO_CSTRING(ObjCIVar);
    ENUM_TO_CSTRING(ReExported);
  default:
    break;
  }
  return "<unknown SymbolType>";
}

void Symbol::CalculateSymbolContext(SymbolContext *sc) {
  // Symbols can reconstruct the symbol and the module in the symbol context
  sc->symbol = this;
  if (ValueIsAddress())
    sc->module_sp = GetAddressRef().GetModule();
  else
    sc->module_sp.reset();
}

ModuleSP Symbol::CalculateSymbolContextModule() {
  if (ValueIsAddress())
    return GetAddressRef().GetModule();
  return ModuleSP();
}

Symbol *Symbol::CalculateSymbolContextSymbol() { return this; }

void Symbol::DumpSymbolContext(Stream *s) {
  bool dumped_module = false;
  if (ValueIsAddress()) {
    ModuleSP module_sp(GetAddressRef().GetModule());
    if (module_sp) {
      dumped_module = true;
      module_sp->DumpSymbolContext(s);
    }
  }
  if (dumped_module)
    s->PutCString(", ");

  s->Printf("Symbol{0x%8.8x}", GetID());
}

lldb::addr_t Symbol::GetByteSize() const { return m_addr_range.GetByteSize(); }

Symbol *Symbol::ResolveReExportedSymbolInModuleSpec(
    Target &target, ConstString &reexport_name, ModuleSpec &module_spec,
    ModuleList &seen_modules) const {
  ModuleSP module_sp;
  if (module_spec.GetFileSpec()) {
    // Try searching for the module file spec first using the full path
    module_sp = target.GetImages().FindFirstModule(module_spec);
    if (!module_sp) {
      // Next try and find the module by basename in case environment variables
      // or other runtime trickery causes shared libraries to be loaded from
      // alternate paths
      module_spec.GetFileSpec().ClearDirectory();
      module_sp = target.GetImages().FindFirstModule(module_spec);
    }
  }

  if (module_sp) {
    // There should not be cycles in the reexport list, but we don't want to
    // crash if there are so make sure we haven't seen this before:
    if (!seen_modules.AppendIfNeeded(module_sp))
      return nullptr;

    lldb_private::SymbolContextList sc_list;
    module_sp->FindSymbolsWithNameAndType(reexport_name, eSymbolTypeAny,
                                          sc_list);
    for (const SymbolContext &sc : sc_list) {
      if (sc.symbol->IsExternal())
        return sc.symbol;
    }
    // If we didn't find the symbol in this module, it may be because this
    // module re-exports some whole other library.  We have to search those as
    // well:
    seen_modules.Append(module_sp);

    FileSpecList reexported_libraries =
        module_sp->GetObjectFile()->GetReExportedLibraries();
    size_t num_reexported_libraries = reexported_libraries.GetSize();
    for (size_t idx = 0; idx < num_reexported_libraries; idx++) {
      ModuleSpec reexported_module_spec;
      reexported_module_spec.GetFileSpec() =
          reexported_libraries.GetFileSpecAtIndex(idx);
      Symbol *result_symbol = ResolveReExportedSymbolInModuleSpec(
          target, reexport_name, reexported_module_spec, seen_modules);
      if (result_symbol)
        return result_symbol;
    }
  }
  return nullptr;
}

Symbol *Symbol::ResolveReExportedSymbol(Target &target) const {
  ConstString reexport_name(GetReExportedSymbolName());
  if (reexport_name) {
    ModuleSpec module_spec;
    ModuleList seen_modules;
    module_spec.GetFileSpec() = GetReExportedSymbolSharedLibrary();
    if (module_spec.GetFileSpec()) {
      return ResolveReExportedSymbolInModuleSpec(target, reexport_name,
                                                 module_spec, seen_modules);
    }
  }
  return nullptr;
}

lldb::addr_t Symbol::GetFileAddress() const {
  if (ValueIsAddress())
    return GetAddressRef().GetFileAddress();
  else
    return LLDB_INVALID_ADDRESS;
}

lldb::addr_t Symbol::GetLoadAddress(Target *target) const {
  if (ValueIsAddress())
    return GetAddressRef().GetLoadAddress(target);
  else
    return LLDB_INVALID_ADDRESS;
}

ConstString Symbol::GetName() const { return GetMangled().GetName(); }

ConstString Symbol::GetNameNoArguments() const {
  return GetMangled().GetName(Mangled::ePreferDemangledWithoutArguments);
}

lldb::addr_t Symbol::ResolveCallableAddress(Target &target) const {
  if (GetType() == lldb::eSymbolTypeUndefined)
    return LLDB_INVALID_ADDRESS;

  Address func_so_addr;

  bool is_indirect = IsIndirect();
  if (GetType() == eSymbolTypeReExported) {
    Symbol *reexported_symbol = ResolveReExportedSymbol(target);
    if (reexported_symbol) {
      func_so_addr = reexported_symbol->GetAddress();
      is_indirect = reexported_symbol->IsIndirect();
    }
  } else {
    func_so_addr = GetAddress();
    is_indirect = IsIndirect();
  }

  if (func_so_addr.IsValid()) {
    if (!target.GetProcessSP() && is_indirect) {
      // can't resolve indirect symbols without calling a function...
      return LLDB_INVALID_ADDRESS;
    }

    lldb::addr_t load_addr =
        func_so_addr.GetCallableLoadAddress(&target, is_indirect);

    if (load_addr != LLDB_INVALID_ADDRESS) {
      return load_addr;
    }
  }

  return LLDB_INVALID_ADDRESS;
}

lldb::DisassemblerSP Symbol::GetInstructions(const ExecutionContext &exe_ctx,
                                             const char *flavor,
                                             bool prefer_file_cache) {
  ModuleSP module_sp(m_addr_range.GetBaseAddress().GetModule());
  if (module_sp && exe_ctx.HasTargetScope()) {
    return Disassembler::DisassembleRange(module_sp->GetArchitecture(), nullptr,
                                          flavor, exe_ctx.GetTargetRef(),
                                          m_addr_range, !prefer_file_cache);
  }
  return lldb::DisassemblerSP();
}

bool Symbol::GetDisassembly(const ExecutionContext &exe_ctx, const char *flavor,
                            bool prefer_file_cache, Stream &strm) {
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

bool Symbol::ContainsFileAddress(lldb::addr_t file_addr) const {
  return m_addr_range.ContainsFileAddress(file_addr);
}

bool Symbol::IsSyntheticWithAutoGeneratedName() const {
  if (!IsSynthetic())
    return false;
  if (!m_mangled)
    return true;
  ConstString demangled = m_mangled.GetDemangledName();
  return demangled.GetStringRef().starts_with(GetSyntheticSymbolPrefix());
}

void Symbol::SynthesizeNameIfNeeded() const {
  if (m_is_synthetic && !m_mangled) {
    // Synthetic symbol names don't mean anything, but they do uniquely
    // identify individual symbols so we give them a unique name. The name
    // starts with the synthetic symbol prefix, followed by a unique number.
    // Typically the UserID of a real symbol is the symbol table index of the
    // symbol in the object file's symbol table(s), so it will be the same
    // every time you read in the object file. We want the same persistence for
    // synthetic symbols so that users can identify them across multiple debug
    // sessions, to understand crashes in those symbols and to reliably set
    // breakpoints on them.
    llvm::SmallString<256> name;
    llvm::raw_svector_ostream os(name);
    os << GetSyntheticSymbolPrefix() << GetID();
    m_mangled.SetDemangledName(ConstString(os.str()));
  }
}

bool Symbol::Decode(const DataExtractor &data, lldb::offset_t *offset_ptr,
                    const SectionList *section_list,
                    const StringTableReader &strtab) {
  if (!data.ValidOffsetForDataOfSize(*offset_ptr, 8))
    return false;
  m_uid = data.GetU32(offset_ptr);
  m_type_data = data.GetU16(offset_ptr);
  const uint16_t bitfields = data.GetU16(offset_ptr);
  m_type_data_resolved = (1u << 15 & bitfields) != 0;
  m_is_synthetic = (1u << 14 & bitfields) != 0;
  m_is_debug = (1u << 13 & bitfields) != 0;
  m_is_external = (1u << 12 & bitfields) != 0;
  m_size_is_sibling = (1u << 11 & bitfields) != 0;
  m_size_is_synthesized = (1u << 10 & bitfields) != 0;
  m_size_is_valid = (1u << 9 & bitfields) != 0;
  m_demangled_is_synthesized = (1u << 8 & bitfields) != 0;
  m_contains_linker_annotations = (1u << 7 & bitfields) != 0;
  m_is_weak = (1u << 6 & bitfields) != 0;
  m_type = bitfields & 0x003f;
  if (!m_mangled.Decode(data, offset_ptr, strtab))
    return false;
  if (!data.ValidOffsetForDataOfSize(*offset_ptr, 20))
    return false;
  const bool is_addr = data.GetU8(offset_ptr) != 0;
  const uint64_t value = data.GetU64(offset_ptr);
  if (is_addr) {
    m_addr_range.GetBaseAddress().ResolveAddressUsingFileSections(value,
                                                                  section_list);
  } else {
    m_addr_range.GetBaseAddress().Clear();
    m_addr_range.GetBaseAddress().SetOffset(value);
  }
  m_addr_range.SetByteSize(data.GetU64(offset_ptr));
  m_flags = data.GetU32(offset_ptr);
  return true;
}

/// The encoding format for the symbol is as follows:
///
/// uint32_t m_uid;
/// uint16_t m_type_data;
/// uint16_t bitfield_data;
/// Mangled mangled;
/// uint8_t is_addr;
/// uint64_t file_addr_or_value;
/// uint64_t size;
/// uint32_t flags;
///
/// The only tricky thing in this encoding is encoding all of the bits in the
/// bitfields. We use a trick to store all bitfields as a 16 bit value and we
/// do the same thing when decoding the symbol. There are test that ensure this
/// encoding works for each individual bit. Everything else is very easy to
/// store.
void Symbol::Encode(DataEncoder &file, ConstStringTable &strtab) const {
  file.AppendU32(m_uid);
  file.AppendU16(m_type_data);
  uint16_t bitfields = m_type;
  if (m_type_data_resolved)
    bitfields |= 1u << 15;
  if (m_is_synthetic)
    bitfields |= 1u << 14;
  if (m_is_debug)
    bitfields |= 1u << 13;
  if (m_is_external)
    bitfields |= 1u << 12;
  if (m_size_is_sibling)
    bitfields |= 1u << 11;
  if (m_size_is_synthesized)
    bitfields |= 1u << 10;
  if (m_size_is_valid)
    bitfields |= 1u << 9;
  if (m_demangled_is_synthesized)
    bitfields |= 1u << 8;
  if (m_contains_linker_annotations)
    bitfields |= 1u << 7;
  if (m_is_weak)
    bitfields |= 1u << 6;
  file.AppendU16(bitfields);
  m_mangled.Encode(file, strtab);
  // A symbol's value might be an address, or it might be a constant. If the
  // symbol's base address doesn't have a section, then it is a constant value.
  // If it does have a section, we will encode the file address and re-resolve
  // the address when we decode it.
  bool is_addr = m_addr_range.GetBaseAddress().GetSection().get() != nullptr;
  file.AppendU8(is_addr);
  file.AppendU64(m_addr_range.GetBaseAddress().GetFileAddress());
  file.AppendU64(m_addr_range.GetByteSize());
  file.AppendU32(m_flags);
}

bool Symbol::operator==(const Symbol &rhs) const {
  if (m_uid != rhs.m_uid)
    return false;
  if (m_type_data != rhs.m_type_data)
    return false;
  if (m_type_data_resolved != rhs.m_type_data_resolved)
    return false;
  if (m_is_synthetic != rhs.m_is_synthetic)
    return false;
  if (m_is_debug != rhs.m_is_debug)
    return false;
  if (m_is_external != rhs.m_is_external)
    return false;
  if (m_size_is_sibling != rhs.m_size_is_sibling)
    return false;
  if (m_size_is_synthesized != rhs.m_size_is_synthesized)
    return false;
  if (m_size_is_valid != rhs.m_size_is_valid)
    return false;
  if (m_demangled_is_synthesized != rhs.m_demangled_is_synthesized)
    return false;
  if (m_contains_linker_annotations != rhs.m_contains_linker_annotations)
    return false;
  if (m_is_weak != rhs.m_is_weak)
    return false;
  if (m_type != rhs.m_type)
    return false;
  if (m_mangled != rhs.m_mangled)
    return false;
  if (m_addr_range.GetBaseAddress() != rhs.m_addr_range.GetBaseAddress())
    return false;
  if (m_addr_range.GetByteSize() != rhs.m_addr_range.GetByteSize())
    return false;
  if (m_flags != rhs.m_flags)
    return false;
  return true;
}

namespace llvm {
namespace json {

bool fromJSON(const llvm::json::Value &value, lldb_private::JSONSymbol &symbol,
              llvm::json::Path path) {
  llvm::json::ObjectMapper o(value, path);
  const bool mapped = o && o.map("value", symbol.value) &&
                      o.map("address", symbol.address) &&
                      o.map("size", symbol.size) && o.map("id", symbol.id) &&
                      o.map("type", symbol.type) && o.map("name", symbol.name);

  if (!mapped)
    return false;

  if (!symbol.value && !symbol.address) {
    path.report("symbol must have either a value or an address");
    return false;
  }

  if (symbol.value && symbol.address) {
    path.report("symbol cannot have both a value and an address");
    return false;
  }

  return true;
}

bool fromJSON(const llvm::json::Value &value, lldb::SymbolType &type,
              llvm::json::Path path) {
  if (auto str = value.getAsString()) {
    type = llvm::StringSwitch<lldb::SymbolType>(*str)
               .Case("absolute", eSymbolTypeAbsolute)
               .Case("code", eSymbolTypeCode)
               .Case("resolver", eSymbolTypeResolver)
               .Case("data", eSymbolTypeData)
               .Case("trampoline", eSymbolTypeTrampoline)
               .Case("runtime", eSymbolTypeRuntime)
               .Case("exception", eSymbolTypeException)
               .Case("sourcefile", eSymbolTypeSourceFile)
               .Case("headerfile", eSymbolTypeHeaderFile)
               .Case("objectfile", eSymbolTypeObjectFile)
               .Case("commonblock", eSymbolTypeCommonBlock)
               .Case("block", eSymbolTypeBlock)
               .Case("local", eSymbolTypeLocal)
               .Case("param", eSymbolTypeParam)
               .Case("variable", eSymbolTypeVariable)
               .Case("variableType", eSymbolTypeVariableType)
               .Case("lineentry", eSymbolTypeLineEntry)
               .Case("lineheader", eSymbolTypeLineHeader)
               .Case("scopebegin", eSymbolTypeScopeBegin)
               .Case("scopeend", eSymbolTypeScopeEnd)
               .Case("additional,", eSymbolTypeAdditional)
               .Case("compiler", eSymbolTypeCompiler)
               .Case("instrumentation", eSymbolTypeInstrumentation)
               .Case("undefined", eSymbolTypeUndefined)
               .Case("objcclass", eSymbolTypeObjCClass)
               .Case("objcmetaClass", eSymbolTypeObjCMetaClass)
               .Case("objcivar", eSymbolTypeObjCIVar)
               .Case("reexporte", eSymbolTypeReExported)
               .Default(eSymbolTypeInvalid);

    if (type == eSymbolTypeInvalid) {
      path.report("invalid symbol type");
      return false;
    }

    return true;
  }
  path.report("expected string");
  return false;
}
} // namespace json
} // namespace llvm
