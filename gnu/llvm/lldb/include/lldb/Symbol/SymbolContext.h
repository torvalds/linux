//===-- SymbolContext.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_SYMBOLCONTEXT_H
#define LLDB_SYMBOL_SYMBOLCONTEXT_H

#include <memory>
#include <string>
#include <vector>

#include "lldb/Core/Address.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Utility/Iterable.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class SymbolContextScope;

/// \class SymbolContext SymbolContext.h "lldb/Symbol/SymbolContext.h" Defines
/// a symbol context baton that can be handed other debug core functions.
///
/// Many debugger functions require a context when doing lookups. This class
/// provides a common structure that can be used as the result of a query that
/// can contain a single result. Examples of such queries include
///     \li Looking up a load address.
class SymbolContext {
public:
  /// Default constructor.
  ///
  /// Initialize all pointer members to nullptr and all struct members to
  /// their default state.
  SymbolContext();

  /// Construct with an object that knows how to reconstruct its symbol
  /// context.
  ///
  /// \param[in] sc_scope
  ///     A symbol context scope object that knows how to reconstruct
  ///     it's context.
  explicit SymbolContext(SymbolContextScope *sc_scope);

  /// Construct with module, and optional compile unit, function, block, line
  /// table, line entry and symbol.
  ///
  /// Initialize all pointer to the specified values.
  ///
  /// \param[in] module_sp
  ///     A Module pointer to the module for this context.
  ///
  /// \param[in] comp_unit
  ///     A CompileUnit pointer to the compile unit for this context.
  ///
  /// \param[in] function
  ///     A Function pointer to the function for this context.
  ///
  /// \param[in] block
  ///     A Block pointer to the deepest block for this context.
  ///
  /// \param[in] line_entry
  ///     A LineEntry pointer to the line entry for this context.
  ///
  /// \param[in] symbol
  ///     A Symbol pointer to the symbol for this context.
  explicit SymbolContext(const lldb::TargetSP &target_sp,
                         const lldb::ModuleSP &module_sp,
                         CompileUnit *comp_unit = nullptr,
                         Function *function = nullptr, Block *block = nullptr,
                         LineEntry *line_entry = nullptr,
                         Symbol *symbol = nullptr);

  // This version sets the target to a NULL TargetSP if you don't know it.
  explicit SymbolContext(const lldb::ModuleSP &module_sp,
                         CompileUnit *comp_unit = nullptr,
                         Function *function = nullptr, Block *block = nullptr,
                         LineEntry *line_entry = nullptr,
                         Symbol *symbol = nullptr);

  ~SymbolContext();

  /// Clear the object's state.
  ///
  /// Resets all pointer members to nullptr, and clears any class objects to
  /// their default state.
  void Clear(bool clear_target);

  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  void Dump(Stream *s, Target *target) const;

  /// Dump the stop context in this object to a Stream.
  ///
  /// Dump the best description of this object to the stream. The information
  /// displayed depends on the amount and quality of the information in this
  /// context. If a module, function, file and line number are available, they
  /// will be dumped. If only a module and function or symbol name with offset
  /// is available, that will be output. Else just the address at which the
  /// target was stopped will be displayed.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// \param[in] so_addr
  ///     The resolved section offset address.
  ///
  /// \param[in] show_fullpaths
  ///     When printing file paths (with the Module), whether the
  ///     base name of the Module should be printed or the full path.
  ///
  /// \param[in] show_module
  ///     Whether the module name should be printed followed by a
  ///     grave accent "`" character.
  ///
  /// \param[in] show_inlined_frames
  ///     If a given pc is in inlined function(s), whether the inlined
  ///     functions should be printed on separate lines in addition to
  ///     the concrete function containing the pc.
  ///
  /// \param[in] show_function_arguments
  ///     If false, this method will try to elide the function argument
  ///     types when printing the function name.  This may be ambiguous
  ///     for languages that have function overloading - but it may
  ///     make the "function name" too long to include all the argument
  ///     types.
  ///
  /// \param[in] show_function_name
  ///     Normally this should be true - the function/symbol name should
  ///     be printed.  In disassembly formatting, where we want a format
  ///     like "<*+36>", this should be false and "*" will be printed
  ///     instead.
  ///
  /// \param[in] show_inline_callsite_line_info
  ///     When processing an inline block, the line info of the callsite
  ///     is dumped if this flag is \b true, otherwise the line info
  ///     of the actual inlined function is dumped.
  ///
  /// \param[in] pattern
  ///     An optional regex pattern to match against the stop context
  ///     description. If specified, parts of the description matching this
  ///     pattern may be highlighted or processed differently. If this parameter
  ///     is an empty string or not provided, no highlighting is applied.
  ///
  /// \return
  ///     \b true if some text was dumped, \b false otherwise.
  bool DumpStopContext(
      Stream *s, ExecutionContextScope *exe_scope, const Address &so_addr,
      bool show_fullpaths, bool show_module, bool show_inlined_frames,
      bool show_function_arguments, bool show_function_name,
      bool show_function_display_name = false,
      std::optional<Stream::HighlightSettings> settings = std::nullopt) const;

  /// Get the address range contained within a symbol context.
  ///
  /// Address range priority is as follows:
  ///     - line_entry address range if line_entry is valid and
  ///     eSymbolContextLineEntry is set in \a scope
  ///     - block address range if block is not nullptr and eSymbolContextBlock
  ///     is set in \a scope
  ///     - function address range if function is not nullptr and
  ///     eSymbolContextFunction is set in \a scope
  ///     - symbol address range if symbol is not nullptr and
  ///     eSymbolContextSymbol is set in \a scope
  ///
  /// \param[in] scope
  ///     A mask of symbol context bits telling this function which
  ///     address ranges it can use when trying to extract one from
  ///     the valid (non-nullptr) symbol context classes.
  ///
  /// \param[in] range_idx
  ///     The address range index to grab. Since many functions and
  ///     blocks are not always contiguous, they may have more than
  ///     one address range.
  ///
  /// \param[in] use_inline_block_range
  ///     If \a scope has the eSymbolContextBlock bit set, and there
  ///     is a valid block in the symbol context, return the block
  ///     address range for the containing inline function block, not
  ///     the deepest most block. This allows us to extract information
  ///     for the address range of the inlined function block, not
  ///     the deepest lexical block.
  ///
  /// \param[out] range
  ///     An address range object that will be filled in if \b true
  ///     is returned.
  ///
  /// \return
  ///     \b True if this symbol context contains items that describe
  ///     an address range, \b false otherwise.
  bool GetAddressRange(uint32_t scope, uint32_t range_idx,
                       bool use_inline_block_range, AddressRange &range) const;

  bool GetAddressRangeFromHereToEndLine(uint32_t end_line, AddressRange &range,
                                        Status &error);

  /// Find the best global data symbol visible from this context.
  ///
  /// Symbol priority is:
  ///     - extern symbol in the current module if there is one
  ///     - non-extern symbol in the current module if there is one
  ///     - extern symbol in the target
  ///     - non-extern symbol in the target
  /// It is an error if the highest-priority result is ambiguous.
  ///
  /// \param[in] name
  ///     The name of the symbol to search for.
  ///
  /// \param[out] error
  ///     An error that will be populated with a message if there was an
  ///     ambiguous result.  The error will not be populated if no result
  ///     was found.
  ///
  /// \return
  ///     The symbol that was found, or \b nullptr if none was found.
  const Symbol *FindBestGlobalDataSymbol(ConstString name, Status &error);

  void GetDescription(
      Stream *s, lldb::DescriptionLevel level, Target *target,
      std::optional<Stream::HighlightSettings> settings = std::nullopt) const;

  uint32_t GetResolvedMask() const;

  lldb::LanguageType GetLanguage() const;

  /// Find a block that defines the function represented by this symbol
  /// context.
  ///
  /// If this symbol context points to a block that is an inlined function, or
  /// is contained within an inlined function, the block that defines the
  /// inlined function is returned.
  ///
  /// If this symbol context has no block in it, or the block is not itself an
  /// inlined function block or contained within one, we return the top level
  /// function block.
  ///
  /// This is a handy function to call when you want to get the block whose
  /// variable list will include the arguments for the function that is
  /// represented by this symbol context (whether the function is an inline
  /// function or not).
  ///
  /// \return
  ///     The block object pointer that defines the function that is
  ///     represented by this symbol context object, nullptr otherwise.
  Block *GetFunctionBlock();

  /// Determines the name of the instance variable for the this decl context.
  ///
  /// For C++ the name is "this", for Objective-C the name is "self".
  ///
  /// \return
  ///     Returns a StringRef for the name of the instance variable.
  llvm::StringRef GetInstanceVariableName();

  /// Sorts the types in TypeMap according to SymbolContext to TypeList
  ///
  void SortTypeList(TypeMap &type_map, TypeList &type_list) const;

  /// Find a name of the innermost function for the symbol context.
  ///
  /// For instance, if the symbol context contains an inlined block, it will
  /// return the inlined function name.
  ///
  /// \return
  ///     The name of the function represented by this symbol context.
  ConstString GetFunctionName(
      Mangled::NamePreference preference = Mangled::ePreferDemangled) const;

  /// Get the line entry that corresponds to the function.
  ///
  /// If the symbol context contains an inlined block, the line entry for the
  /// start address of the inlined function will be returned, otherwise the
  /// line entry for the start address of the function will be returned. This
  /// can be used after doing a Module::FindFunctions(...) or
  /// ModuleList::FindFunctions(...) call in order to get the correct line
  /// table information for the symbol context. it will return the inlined
  /// function name.
  LineEntry GetFunctionStartLineEntry() const;

  /// Find the block containing the inlined block that contains this block.
  ///
  /// For instance, if the symbol context contains an inlined block, it will
  /// return the inlined function name.
  ///
  /// \param[in] curr_frame_pc
  ///    The address within the block of this object.
  ///
  /// \param[out] next_frame_sc
  ///     A new symbol context that does what the title says it does.
  ///
  /// \param[out] inlined_frame_addr
  ///     This is what you should report as the PC in \a next_frame_sc.
  ///
  /// \return
  ///     \b true if this SymbolContext specifies a block contained in an
  ///     inlined block.  If this returns \b true, \a next_frame_sc and
  ///     \a inlined_frame_addr will be filled in correctly.
  bool GetParentOfInlinedScope(const Address &curr_frame_pc,
                               SymbolContext &next_frame_sc,
                               Address &inlined_frame_addr) const;

  // Member variables
  lldb::TargetSP target_sp; ///< The Target for a given query
  lldb::ModuleSP module_sp; ///< The Module for a given query
  CompileUnit *comp_unit = nullptr; ///< The CompileUnit for a given query
  Function *function = nullptr;     ///< The Function for a given query
  Block *block = nullptr;           ///< The Block for a given query
  LineEntry line_entry;     ///< The LineEntry for a given query
  Symbol *symbol = nullptr; ///< The Symbol for a given query
  Variable *variable =
      nullptr; ///< The global variable matching the given query
};

class SymbolContextSpecifier {
public:
  enum SpecificationType {
    eNothingSpecified = 0,
    eModuleSpecified = 1 << 0,
    eFileSpecified = 1 << 1,
    eLineStartSpecified = 1 << 2,
    eLineEndSpecified = 1 << 3,
    eFunctionSpecified = 1 << 4,
    eClassOrNamespaceSpecified = 1 << 5,
    eAddressRangeSpecified = 1 << 6
  };

  // This one produces a specifier that matches everything...
  SymbolContextSpecifier(const lldb::TargetSP &target_sp);

  ~SymbolContextSpecifier();

  bool AddSpecification(const char *spec_string, SpecificationType type);

  bool AddLineSpecification(uint32_t line_no, SpecificationType type);

  void Clear();

  bool SymbolContextMatches(const SymbolContext &sc);

  bool AddressMatches(lldb::addr_t addr);

  void GetDescription(Stream *s, lldb::DescriptionLevel level) const;

private:
  lldb::TargetSP m_target_sp;
  std::string m_module_spec;
  lldb::ModuleSP m_module_sp;
  std::unique_ptr<FileSpec> m_file_spec_up;
  size_t m_start_line;
  size_t m_end_line;
  std::string m_function_spec;
  std::string m_class_name;
  std::unique_ptr<AddressRange> m_address_range_up;
  uint32_t m_type; // Or'ed bits from SpecificationType
};

/// \class SymbolContextList SymbolContext.h "lldb/Symbol/SymbolContext.h"
/// Defines a list of symbol context objects.
///
/// This class provides a common structure that can be used to contain the
/// result of a query that can contain a multiple results. Examples of such
/// queries include:
///     \li Looking up a function by name.
///     \li Finding all addresses for a specified file and line number.
class SymbolContextList {
public:
  /// Default constructor.
  ///
  /// Initialize with an empty list.
  SymbolContextList();

  /// Destructor.
  ~SymbolContextList();

  /// Append a new symbol context to the list.
  ///
  /// \param[in] sc
  ///     A symbol context to append to the list.
  void Append(const SymbolContext &sc);

  void Append(const SymbolContextList &sc_list);

  bool AppendIfUnique(const SymbolContext &sc, bool merge_symbol_into_function);

  uint32_t AppendIfUnique(const SymbolContextList &sc_list,
                          bool merge_symbol_into_function);

  /// Clear the object's state.
  ///
  /// Clears the symbol context list.
  void Clear();

  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of each symbol context in the list to
  /// the supplied stream \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  void Dump(Stream *s, Target *target) const;

  /// Get accessor for a symbol context at index \a idx.
  ///
  /// Dump a description of the contents of each symbol context in the list to
  /// the supplied stream \a s.
  ///
  /// \param[in] idx
  ///     The zero based index into the symbol context list.
  ///
  /// \param[out] sc
  ///     A reference to the symbol context to fill in.
  ///
  /// \return
  ///     Returns \b true if \a idx was a valid index into this
  ///     symbol context list and \a sc was filled in, \b false
  ///     otherwise.
  bool GetContextAtIndex(size_t idx, SymbolContext &sc) const;

  /// Direct reference accessor for a symbol context at index \a idx.
  ///
  /// The index \a idx must be a valid index, no error checking will be done
  /// to ensure that it is valid.
  ///
  /// \param[in] idx
  ///     The zero based index into the symbol context list.
  ///
  /// \return
  ///     A const reference to the symbol context to fill in.
  SymbolContext &operator[](size_t idx) { return m_symbol_contexts[idx]; }

  const SymbolContext &operator[](size_t idx) const {
    return m_symbol_contexts[idx];
  }

  bool RemoveContextAtIndex(size_t idx);

  /// Get accessor for a symbol context list size.
  ///
  /// \return
  ///     Returns the number of symbol context objects in the list.
  uint32_t GetSize() const;

  bool IsEmpty() const;

  uint32_t NumLineEntriesWithLine(uint32_t line) const;

  void GetDescription(Stream *s, lldb::DescriptionLevel level,
                      Target *target) const;

protected:
  typedef std::vector<SymbolContext>
      collection; ///< The collection type for the list.
  typedef collection::const_iterator const_iterator;

  // Member variables.
  collection m_symbol_contexts; ///< The list of symbol contexts.

public:
  const_iterator begin() const { return m_symbol_contexts.begin(); }
  const_iterator end() const { return m_symbol_contexts.end(); }

  typedef AdaptedIterable<collection, SymbolContext, vector_adapter>
      SymbolContextIterable;
  SymbolContextIterable SymbolContexts() {
    return SymbolContextIterable(m_symbol_contexts);
  }
};

bool operator==(const SymbolContext &lhs, const SymbolContext &rhs);
bool operator!=(const SymbolContext &lhs, const SymbolContext &rhs);

bool operator==(const SymbolContextList &lhs, const SymbolContextList &rhs);
bool operator!=(const SymbolContextList &lhs, const SymbolContextList &rhs);

} // namespace lldb_private

#endif // LLDB_SYMBOL_SYMBOLCONTEXT_H
