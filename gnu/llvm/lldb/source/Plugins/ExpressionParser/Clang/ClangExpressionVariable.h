//===-- ClangExpressionVariable.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONVARIABLE_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONVARIABLE_H

#include <csignal>
#include <cstdint>
#include <cstring>

#include <map>
#include <string>
#include <vector>

#include "llvm/Support/Casting.h"

#include "lldb/Core/Value.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Symbol/TaggedASTType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-public.h"

namespace llvm {
class Value;
}

namespace clang {
class NamedDecl;
}

namespace lldb_private {

class ValueObjectConstResult;

/// \class ClangExpressionVariable ClangExpressionVariable.h
/// "lldb/Expression/ClangExpressionVariable.h" Encapsulates one variable for
/// the expression parser.
///
/// The expression parser uses variables in three different contexts:
///
/// First, it stores persistent variables along with the process for use in
/// expressions.  These persistent variables contain their own data and are
/// typed.
///
/// Second, in an interpreted expression, it stores the local variables for
/// the expression along with the expression.  These variables contain their
/// own data and are typed.
///
/// Third, in a JIT-compiled expression, it stores the variables that the
/// expression needs to have materialized and dematerialized at each
/// execution.  These do not contain their own data but are named and typed.
///
/// This class supports all of these use cases using simple type polymorphism,
/// and provides necessary support methods.  Its interface is RTTI-neutral.
class ClangExpressionVariable
    : public llvm::RTTIExtends<ClangExpressionVariable, ExpressionVariable> {
public:
  // LLVM RTTI support
  static char ID;

  ClangExpressionVariable(ExecutionContextScope *exe_scope,
                          lldb::ByteOrder byte_order, uint32_t addr_byte_size);

  ClangExpressionVariable(ExecutionContextScope *exe_scope, Value &value,
                          ConstString name, uint16_t flags = EVNone);

  ClangExpressionVariable(const lldb::ValueObjectSP &valobj_sp);

  ClangExpressionVariable(ExecutionContextScope *exe_scope,
                          ConstString name,
                          const TypeFromUser &user_type,
                          lldb::ByteOrder byte_order, uint32_t addr_byte_size);

  /// Utility functions for dealing with ExpressionVariableLists in Clang-
  /// specific ways

  /// Finds a variable by NamedDecl in the list.
  ///
  /// \return
  ///     The variable requested, or NULL if that variable is not in the list.
  static ClangExpressionVariable *
  FindVariableInList(ExpressionVariableList &list, const clang::NamedDecl *decl,
                     uint64_t parser_id) {
    lldb::ExpressionVariableSP var_sp;
    for (size_t index = 0, size = list.GetSize(); index < size; ++index) {
      var_sp = list.GetVariableAtIndex(index);

      if (ClangExpressionVariable *clang_var =
              llvm::dyn_cast<ClangExpressionVariable>(var_sp.get())) {
        ClangExpressionVariable::ParserVars *parser_vars =
            clang_var->GetParserVars(parser_id);

        if (parser_vars && parser_vars->m_named_decl == decl)
          return clang_var;
      }
    }
    return nullptr;
  }

  /// If the variable contains its own data, make a Value point at it. If \a
  /// exe_ctx in not NULL, the value will be resolved in with that execution
  /// context.
  ///
  /// \param[in] value
  ///     The value to point at the data.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to use to resolve \a value.
  ///
  /// \return
  ///     True on success; false otherwise (in particular, if this variable
  ///     does not contain its own data).
  bool PointValueAtData(Value &value, ExecutionContext *exe_ctx);

  /// The following values should not live beyond parsing
  class ParserVars {
  public:
    ParserVars() = default;

    const clang::NamedDecl *m_named_decl =
        nullptr; ///< The Decl corresponding to this variable
    llvm::Value *m_llvm_value =
        nullptr; ///< The IR value corresponding to this variable;
                 /// usually a GlobalValue
    lldb_private::Value
        m_lldb_value;            ///< The value found in LLDB for this variable
    lldb::VariableSP m_lldb_var; ///< The original variable for this variable
    const lldb_private::Symbol *m_lldb_sym =
        nullptr; ///< The original symbol for this
                 /// variable, if it was a symbol

    /// Callback that provides a ValueObject for the
    /// specified frame. Used by the materializer for
    /// re-fetching ValueObjects when materializing
    /// ivars.
    ValueObjectProviderTy m_lldb_valobj_provider;
  };

private:
  typedef std::map<uint64_t, ParserVars> ParserVarMap;
  ParserVarMap m_parser_vars;

public:
  /// Make this variable usable by the parser by allocating space for parser-
  /// specific variables
  void EnableParserVars(uint64_t parser_id) {
    m_parser_vars.insert(std::make_pair(parser_id, ParserVars()));
  }

  /// Deallocate parser-specific variables
  void DisableParserVars(uint64_t parser_id) { m_parser_vars.erase(parser_id); }

  /// Access parser-specific variables
  ParserVars *GetParserVars(uint64_t parser_id) {
    ParserVarMap::iterator i = m_parser_vars.find(parser_id);

    if (i == m_parser_vars.end())
      return nullptr;
    else
      return &i->second;
  }

  /// The following values are valid if the variable is used by JIT code
  struct JITVars {
    JITVars() = default;

    lldb::offset_t m_alignment =
        0;             ///< The required alignment of the variable, in bytes
    size_t m_size = 0; ///< The space required for the variable, in bytes
    lldb::offset_t m_offset =
        0; ///< The offset of the variable in the struct, in bytes
  };

private:
  typedef std::map<uint64_t, JITVars> JITVarMap;
  JITVarMap m_jit_vars;

public:
  /// Make this variable usable for materializing for the JIT by allocating
  /// space for JIT-specific variables
  void EnableJITVars(uint64_t parser_id) {
    m_jit_vars.insert(std::make_pair(parser_id, JITVars()));
  }

  /// Deallocate JIT-specific variables
  void DisableJITVars(uint64_t parser_id) { m_jit_vars.erase(parser_id); }

  JITVars *GetJITVars(uint64_t parser_id) {
    JITVarMap::iterator i = m_jit_vars.find(parser_id);

    if (i == m_jit_vars.end())
      return nullptr;
    else
      return &i->second;
  }

  TypeFromUser GetTypeFromUser();

  /// Members
  ClangExpressionVariable(const ClangExpressionVariable &) = delete;
  const ClangExpressionVariable &
  operator=(const ClangExpressionVariable &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONVARIABLE_H
