//===-- Symbol.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_SYMBOL_H
#define LLDB_SYMBOL_SYMBOL_H

#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/SymbolContextScope.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"
#include "llvm/Support/JSON.h"

namespace lldb_private {

struct JSONSymbol {
  std::optional<uint64_t> address;
  std::optional<uint64_t> value;
  std::optional<uint64_t> size;
  std::optional<uint64_t> id;
  std::optional<lldb::SymbolType> type;
  std::string name;
};

class Symbol : public SymbolContextScope {
public:
  // ObjectFile readers can classify their symbol table entries and searches
  // can be made on specific types where the symbol values will have
  // drastically different meanings and sorting requirements.
  Symbol();

  Symbol(uint32_t symID, llvm::StringRef name, lldb::SymbolType type,
         bool external, bool is_debug, bool is_trampoline, bool is_artificial,
         const lldb::SectionSP &section_sp, lldb::addr_t value,
         lldb::addr_t size, bool size_is_valid,
         bool contains_linker_annotations, uint32_t flags);

  Symbol(uint32_t symID, const Mangled &mangled, lldb::SymbolType type,
         bool external, bool is_debug, bool is_trampoline, bool is_artificial,
         const AddressRange &range, bool size_is_valid,
         bool contains_linker_annotations, uint32_t flags);

  Symbol(const Symbol &rhs);

  const Symbol &operator=(const Symbol &rhs);

  static llvm::Expected<Symbol> FromJSON(const JSONSymbol &symbol,
                                         SectionList *section_list);

  void Clear();

  bool Compare(ConstString name, lldb::SymbolType type) const;

  void Dump(Stream *s, Target *target, uint32_t index,
            Mangled::NamePreference name_preference =
                Mangled::ePreferDemangled) const;

  bool ValueIsAddress() const;

  // The GetAddressRef() accessor functions should only be called if you
  // previously call ValueIsAddress() otherwise you might get an reference to
  // an Address object that contains an constant integer value in
  // m_addr_range.m_base_addr.m_offset which could be incorrectly used to
  // represent an absolute address since it has no section.
  Address &GetAddressRef() { return m_addr_range.GetBaseAddress(); }

  const Address &GetAddressRef() const { return m_addr_range.GetBaseAddress(); }

  // Makes sure the symbol's value is an address and returns the file address.
  // Returns LLDB_INVALID_ADDRESS if the symbol's value isn't an address.
  lldb::addr_t GetFileAddress() const;

  // Makes sure the symbol's value is an address and gets the load address
  // using \a target if it is. Returns LLDB_INVALID_ADDRESS if the symbol's
  // value isn't an address or if the section isn't loaded in \a target.
  lldb::addr_t GetLoadAddress(Target *target) const;

  // Access the address value. Do NOT hand out the AddressRange as an object as
  // the byte size of the address range may not be filled in and it should be
  // accessed via GetByteSize().
  Address GetAddress() const {
    // Make sure the our value is an address before we hand a copy out. We use
    // the Address inside m_addr_range to contain the value for symbols that
    // are not address based symbols so we are using it for more than just
    // addresses. For example undefined symbols on MacOSX have a nlist.n_value
    // of 0 (zero) and this will get placed into
    // m_addr_range.m_base_addr.m_offset and it will have no section. So in the
    // GetAddress() accessor, we need to hand out an invalid address if the
    // symbol's value isn't an address.
    if (ValueIsAddress())
      return m_addr_range.GetBaseAddress();
    else
      return Address();
  }

  /// Get the raw value of the symbol from the symbol table.
  ///
  /// If the symbol's value is an address, return the file address, else return
  /// the raw value that is stored in the m_addr_range. If the base address has
  /// no section, then getting the file address will return the correct value
  /// as it will return the offset in the base address which is the value.
  uint64_t GetRawValue() const {
    return m_addr_range.GetBaseAddress().GetFileAddress();
  }

  // When a symbol's value isn't an address, we need to access the raw value.
  // This function will ensure this symbol's value isn't an address and return
  // the integer value if this checks out, otherwise it will return
  // "fail_value" if the symbol is an address value.
  uint64_t GetIntegerValue(uint64_t fail_value = 0) const {
    if (ValueIsAddress()) {
      // This symbol's value is an address. Use Symbol::GetAddress() to get the
      // address.
      return fail_value;
    } else {
      // The value is stored in the base address' offset
      return m_addr_range.GetBaseAddress().GetOffset();
    }
  }

  lldb::addr_t ResolveCallableAddress(Target &target) const;

  ConstString GetName() const;

  ConstString GetNameNoArguments() const;

  ConstString GetDisplayName() const;

  uint32_t GetID() const { return m_uid; }

  lldb::LanguageType GetLanguage() const {
    // TODO: See if there is a way to determine the language for a symbol
    // somehow, for now just return our best guess
    return GetMangled().GuessLanguage();
  }

  void SetID(uint32_t uid) { m_uid = uid; }

  Mangled &GetMangled() {
    SynthesizeNameIfNeeded();
    return m_mangled;
  }

  const Mangled &GetMangled() const {
    SynthesizeNameIfNeeded();
    return m_mangled;
  }

  ConstString GetReExportedSymbolName() const;

  FileSpec GetReExportedSymbolSharedLibrary() const;

  void SetReExportedSymbolName(ConstString name);

  bool SetReExportedSymbolSharedLibrary(const FileSpec &fspec);

  Symbol *ResolveReExportedSymbol(Target &target) const;

  uint32_t GetSiblingIndex() const;

  lldb::SymbolType GetType() const { return (lldb::SymbolType)m_type; }

  void SetType(lldb::SymbolType type) { m_type = (lldb::SymbolType)type; }

  const char *GetTypeAsString() const;

  uint32_t GetFlags() const { return m_flags; }

  void SetFlags(uint32_t flags) { m_flags = flags; }

  void GetDescription(
      Stream *s, lldb::DescriptionLevel level, Target *target,
      std::optional<Stream::HighlightSettings> settings = std::nullopt) const;

  bool IsSynthetic() const { return m_is_synthetic; }

  bool IsSyntheticWithAutoGeneratedName() const;

  void SetIsSynthetic(bool b) { m_is_synthetic = b; }

  bool GetSizeIsSynthesized() const { return m_size_is_synthesized; }

  void SetSizeIsSynthesized(bool b) { m_size_is_synthesized = b; }

  bool IsDebug() const { return m_is_debug; }

  void SetDebug(bool b) { m_is_debug = b; }

  bool IsExternal() const { return m_is_external; }

  void SetExternal(bool b) { m_is_external = b; }

  bool IsTrampoline() const;

  bool IsIndirect() const;

  bool IsWeak() const { return m_is_weak; }

  void SetIsWeak(bool b) { m_is_weak = b; }

  bool GetByteSizeIsValid() const { return m_size_is_valid; }

  lldb::addr_t GetByteSize() const;

  void SetByteSize(lldb::addr_t size) {
    m_size_is_valid = size > 0;
    m_addr_range.SetByteSize(size);
  }

  bool GetSizeIsSibling() const { return m_size_is_sibling; }

  void SetSizeIsSibling(bool b) { m_size_is_sibling = b; }

  // If m_type is "Code" or "Function" then this will return the prologue size
  // in bytes, else it will return zero.
  uint32_t GetPrologueByteSize();

  bool GetDemangledNameIsSynthesized() const {
    return m_demangled_is_synthesized;
  }

  void SetDemangledNameIsSynthesized(bool b) { m_demangled_is_synthesized = b; }

  bool ContainsLinkerAnnotations() const {
    return m_contains_linker_annotations;
  }
  void SetContainsLinkerAnnotations(bool b) {
    m_contains_linker_annotations = b;
  }
  /// \copydoc SymbolContextScope::CalculateSymbolContext(SymbolContext*)
  ///
  /// \see SymbolContextScope
  void CalculateSymbolContext(SymbolContext *sc) override;

  lldb::ModuleSP CalculateSymbolContextModule() override;

  Symbol *CalculateSymbolContextSymbol() override;

  /// \copydoc SymbolContextScope::DumpSymbolContext(Stream*)
  ///
  /// \see SymbolContextScope
  void DumpSymbolContext(Stream *s) override;

  lldb::DisassemblerSP GetInstructions(const ExecutionContext &exe_ctx,
                                       const char *flavor,
                                       bool prefer_file_cache);

  bool GetDisassembly(const ExecutionContext &exe_ctx, const char *flavor,
                      bool prefer_file_cache, Stream &strm);

  bool ContainsFileAddress(lldb::addr_t file_addr) const;

  static llvm::StringRef GetSyntheticSymbolPrefix() {
    return "___lldb_unnamed_symbol";
  }

  /// Decode a serialized version of this object from data.
  ///
  /// \param data
  ///   The decoder object that references the serialized data.
  ///
  /// \param offset_ptr
  ///   A pointer that contains the offset from which the data will be decoded
  ///   from that gets updated as data gets decoded.
  ///
  /// \param section_list
  ///   A section list that allows lldb_private::Address objects to be filled
  ///   in. The address information for symbols are serilized as file addresses
  ///   and must be converted into Address objects with the right section and
  ///   offset.
  ///
  /// \param strtab
  ///   All strings in cache files are put into string tables for efficiency
  ///   and cache file size reduction. Strings are stored as uint32_t string
  ///   table offsets in the cache data.
  ///
  /// \return
  ///   True if the symbol is successfully decoded, false otherwise.
  bool Decode(const DataExtractor &data, lldb::offset_t *offset_ptr,
              const SectionList *section_list, const StringTableReader &strtab);

  /// Encode this object into a data encoder object.
  ///
  /// This allows this object to be serialized to disk.
  ///
  /// \param encoder
  ///   A data encoder object that serialized bytes will be encoded into.
  ///
  /// \param strtab
  ///   All strings in cache files are put into string tables for efficiency
  ///   and cache file size reduction. Strings are stored as uint32_t string
  ///   table offsets in the cache data.
  void Encode(DataEncoder &encoder, ConstStringTable &strtab) const;

  bool operator==(const Symbol &rhs) const;

protected:
  // This is the internal guts of ResolveReExportedSymbol, it assumes
  // reexport_name is not null, and that module_spec is valid.  We track the
  // modules we've already seen to make sure we don't get caught in a cycle.

  Symbol *ResolveReExportedSymbolInModuleSpec(
      Target &target, ConstString &reexport_name,
      lldb_private::ModuleSpec &module_spec,
      lldb_private::ModuleList &seen_modules) const;

  void SynthesizeNameIfNeeded() const;

  uint32_t m_uid =
      UINT32_MAX;           // User ID (usually the original symbol table index)
  uint16_t m_type_data = 0; // data specific to m_type
  uint16_t m_type_data_resolved : 1, // True if the data in m_type_data has
                                     // already been calculated
      m_is_synthetic : 1, // non-zero if this symbol is not actually in the
                          // symbol table, but synthesized from other info in
                          // the object file.
      m_is_debug : 1,     // non-zero if this symbol is debug information in a
                          // symbol
      m_is_external : 1,  // non-zero if this symbol is globally visible
      m_size_is_sibling : 1,     // m_size contains the index of this symbol's
                                 // sibling
      m_size_is_synthesized : 1, // non-zero if this symbol's size was
                                 // calculated using a delta between this
                                 // symbol and the next
      m_size_is_valid : 1,
      m_demangled_is_synthesized : 1, // The demangled name was created should
                                      // not be used for expressions or other
                                      // lookups
      m_contains_linker_annotations : 1, // The symbol name contains linker
                                         // annotations, which are optional when
                                         // doing name lookups
      m_is_weak : 1,
      m_type : 6;            // Values from the lldb::SymbolType enum.
  mutable Mangled m_mangled; // uniqued symbol name/mangled name pair
  AddressRange m_addr_range; // Contains the value, or the section offset
                             // address when the value is an address in a
                             // section, and the size (if any)
  uint32_t m_flags = 0; // A copy of the flags from the original symbol table,
                        // the ObjectFile plug-in can interpret these
};

} // namespace lldb_private

namespace llvm {
namespace json {

bool fromJSON(const llvm::json::Value &value, lldb_private::JSONSymbol &symbol,
              llvm::json::Path path);

bool fromJSON(const llvm::json::Value &value, lldb::SymbolType &type,
              llvm::json::Path path);

} // namespace json
} // namespace llvm

#endif // LLDB_SYMBOL_SYMBOL_H
