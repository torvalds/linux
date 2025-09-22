//===-- Variable.h -----------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_VARIABLE_H
#define LLDB_SYMBOL_VARIABLE_H

#include "lldb/Core/Declaration.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Expression/DWARFExpressionList.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private.h"
#include <memory>
#include <vector>

namespace lldb_private {

class Variable : public UserID, public std::enable_shared_from_this<Variable> {
public:
  typedef RangeVector<lldb::addr_t, lldb::addr_t> RangeList;

  /// Constructors and Destructors.
  ///
  /// \param mangled The mangled or fully qualified name of the variable.
  Variable(lldb::user_id_t uid, const char *name, const char *mangled,
           const lldb::SymbolFileTypeSP &symfile_type_sp, lldb::ValueType scope,
           SymbolContextScope *owner_scope, const RangeList &scope_range,
           Declaration *decl, const DWARFExpressionList &location,
           bool external, bool artificial, bool location_is_constant_data,
           bool static_member = false);

  virtual ~Variable();

  void Dump(Stream *s, bool show_context) const;

  bool DumpDeclaration(Stream *s, bool show_fullpaths, bool show_module);

  const Declaration &GetDeclaration() const { return m_declaration; }

  ConstString GetName() const;

  ConstString GetUnqualifiedName() const;

  SymbolContextScope *GetSymbolContextScope() const { return m_owner_scope; }

  /// Since a variable can have a basename "i" and also a mangled named
  /// "_ZN12_GLOBAL__N_11iE" and a demangled mangled name "(anonymous
  /// namespace)::i", this function will allow a generic match function that can
  /// be called by commands and expression parsers to make sure we match
  /// anything we come across.
  bool NameMatches(ConstString name) const;

  bool NameMatches(const RegularExpression &regex) const;

  Type *GetType();

  lldb::LanguageType GetLanguage() const;

  lldb::ValueType GetScope() const { return m_scope; }

  const RangeList &GetScopeRange() const { return m_scope_range; }

  bool IsExternal() const { return m_external; }

  bool IsArtificial() const { return m_artificial; }

  bool IsStaticMember() const { return m_static_member; }

  DWARFExpressionList &LocationExpressionList() { return m_location_list; }

  const DWARFExpressionList &LocationExpressionList() const {
    return m_location_list;
  }

  // When given invalid address, it dumps all locations. Otherwise it only dumps
  // the location that contains this address.
  bool DumpLocations(Stream *s, const Address &address);

  size_t MemorySize() const;

  void CalculateSymbolContext(SymbolContext *sc);

  bool IsInScope(StackFrame *frame);

  bool LocationIsValidForFrame(StackFrame *frame);

  bool LocationIsValidForAddress(const Address &address);

  bool GetLocationIsConstantValueData() const { return m_loc_is_const_data; }

  void SetLocationIsConstantValueData(bool b) { m_loc_is_const_data = b; }

  typedef size_t (*GetVariableCallback)(void *baton, const char *name,
                                        VariableList &var_list);

  static Status GetValuesForVariableExpressionPath(
      llvm::StringRef variable_expr_path, ExecutionContextScope *scope,
      GetVariableCallback callback, void *baton, VariableList &variable_list,
      ValueObjectList &valobj_list);

  static void AutoComplete(const ExecutionContext &exe_ctx,
                           CompletionRequest &request);

  CompilerDeclContext GetDeclContext();

  CompilerDecl GetDecl();

protected:
  /// The basename of the variable (no namespaces).
  ConstString m_name;
  /// The mangled name of the variable.
  Mangled m_mangled;
  /// The type pointer of the variable (int, struct, class, etc)
  /// global, parameter, local.
  lldb::SymbolFileTypeSP m_symfile_type_sp;
  lldb::ValueType m_scope;
  /// The symbol file scope that this variable was defined in
  SymbolContextScope *m_owner_scope;
  /// The list of ranges inside the owner's scope where this variable
  /// is valid.
  RangeList m_scope_range;
  /// Declaration location for this item.
  Declaration m_declaration;
  /// The location of this variable that can be fed to
  /// DWARFExpression::Evaluate().
  DWARFExpressionList m_location_list;
  /// Visible outside the containing compile unit?
  unsigned m_external : 1;
  /// Non-zero if the variable is not explicitly declared in source.
  unsigned m_artificial : 1;
  /// The m_location expression contains the constant variable value
  /// data, not a DWARF location.
  unsigned m_loc_is_const_data : 1;
  /// Non-zero if variable is static member of a class or struct.
  unsigned m_static_member : 1;

private:
  Variable(const Variable &rhs) = delete;
  Variable &operator=(const Variable &rhs) = delete;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_VARIABLE_H
