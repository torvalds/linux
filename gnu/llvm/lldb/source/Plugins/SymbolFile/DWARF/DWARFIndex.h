//===-- DWARFIndex.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFINDEX_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFINDEX_H

#include "Plugins/SymbolFile/DWARF/DIERef.h"
#include "Plugins/SymbolFile/DWARF/DWARFDIE.h"
#include "Plugins/SymbolFile/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"

#include "lldb/Core/Module.h"
#include "lldb/Target/Statistics.h"

namespace lldb_private::plugin {
namespace dwarf {
class DWARFDeclContext;
class DWARFDIE;

class DWARFIndex {
public:
  DWARFIndex(Module &module) : m_module(module) {}
  virtual ~DWARFIndex();

  virtual void Preload() = 0;

  /// Finds global variables with the given base name. Any additional filtering
  /// (e.g., to only retrieve variables from a given context) should be done by
  /// the consumer.
  virtual void
  GetGlobalVariables(ConstString basename,
                     llvm::function_ref<bool(DWARFDIE die)> callback) = 0;

  virtual void
  GetGlobalVariables(const RegularExpression &regex,
                     llvm::function_ref<bool(DWARFDIE die)> callback) = 0;
  /// \a cu must be the skeleton unit if possible, not GetNonSkeletonUnit().
  virtual void
  GetGlobalVariables(DWARFUnit &cu,
                     llvm::function_ref<bool(DWARFDIE die)> callback) = 0;
  virtual void
  GetObjCMethods(ConstString class_name,
                 llvm::function_ref<bool(DWARFDIE die)> callback) = 0;
  virtual void
  GetCompleteObjCClass(ConstString class_name, bool must_be_implementation,
                       llvm::function_ref<bool(DWARFDIE die)> callback) = 0;
  virtual void GetTypes(ConstString name,
                        llvm::function_ref<bool(DWARFDIE die)> callback) = 0;
  virtual void GetTypes(const DWARFDeclContext &context,
                        llvm::function_ref<bool(DWARFDIE die)> callback) = 0;

  /// Finds all DIEs whose fully qualified name matches `context`. A base
  /// implementation is provided, and it uses the entire CU to check the DIE
  /// parent hierarchy. Specializations should override this if they are able
  /// to provide a faster implementation.
  virtual void
  GetFullyQualifiedType(const DWARFDeclContext &context,
                        llvm::function_ref<bool(DWARFDIE die)> callback);
  virtual void
  GetNamespaces(ConstString name,
                llvm::function_ref<bool(DWARFDIE die)> callback) = 0;
  virtual void
  GetFunctions(const Module::LookupInfo &lookup_info, SymbolFileDWARF &dwarf,
               const CompilerDeclContext &parent_decl_ctx,
               llvm::function_ref<bool(DWARFDIE die)> callback) = 0;
  virtual void
  GetFunctions(const RegularExpression &regex,
               llvm::function_ref<bool(DWARFDIE die)> callback) = 0;

  virtual void Dump(Stream &s) = 0;

  StatsDuration::Duration GetIndexTime() { return m_index_time; }

protected:
  Module &m_module;
  StatsDuration m_index_time;

  /// Helper function implementing common logic for processing function dies. If
  /// the function given by "die" matches search criteria given by
  /// "parent_decl_ctx" and "name_type_mask", it calls the callback with the
  /// given die.
  bool ProcessFunctionDIE(const Module::LookupInfo &lookup_info, DWARFDIE die,
                          const CompilerDeclContext &parent_decl_ctx,
                          llvm::function_ref<bool(DWARFDIE die)> callback);

  class DIERefCallbackImpl {
  public:
    DIERefCallbackImpl(const DWARFIndex &index,
                       llvm::function_ref<bool(DWARFDIE die)> callback,
                       llvm::StringRef name);
    bool operator()(DIERef ref) const;
    bool operator()(const llvm::AppleAcceleratorTable::Entry &entry) const;

  private:
    const DWARFIndex &m_index;
    SymbolFileDWARF &m_dwarf;
    const llvm::function_ref<bool(DWARFDIE die)> m_callback;
    const llvm::StringRef m_name;
  };
  DIERefCallbackImpl
  DIERefCallback(llvm::function_ref<bool(DWARFDIE die)> callback,
                 llvm::StringRef name = {}) const {
    return DIERefCallbackImpl(*this, callback, name);
  }

  void ReportInvalidDIERef(DIERef ref, llvm::StringRef name) const;

  /// Implementation of `GetFullyQualifiedType` to check a single entry,
  /// shareable with derived classes.
  bool
  GetFullyQualifiedTypeImpl(const DWARFDeclContext &context, DWARFDIE die,
                            llvm::function_ref<bool(DWARFDIE die)> callback);
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFINDEX_H
