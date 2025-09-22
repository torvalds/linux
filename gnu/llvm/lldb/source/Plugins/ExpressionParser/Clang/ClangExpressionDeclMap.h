//===-- ClangExpressionDeclMap.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONDECLMAP_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONDECLMAP_H

#include <csignal>
#include <cstdint>

#include <memory>
#include <vector>

#include "ClangASTSource.h"
#include "ClangExpressionVariable.h"

#include "lldb/Core/Value.h"
#include "lldb/Expression/Materializer.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/TaggedASTType.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/lldb-public.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/DenseMap.h"

namespace lldb_private {

class ClangPersistentVariables;

/// \class ClangExpressionDeclMap ClangExpressionDeclMap.h
/// "lldb/Expression/ClangExpressionDeclMap.h" Manages named entities that are
/// defined in LLDB's debug information.
///
/// The Clang parser uses the ClangASTSource as an interface to request named
/// entities from outside an expression.  The ClangASTSource reports back,
/// listing all possible objects corresponding to a particular name.  But it
/// in turn relies on ClangExpressionDeclMap, which performs several important
/// functions.
///
/// First, it records what variables and functions were looked up and what
/// Decls were returned for them.
///
/// Second, it constructs a struct on behalf of IRForTarget, recording which
/// variables should be placed where and relaying this information back so
/// that IRForTarget can generate context-independent code.
///
/// Third, it "materializes" this struct on behalf of the expression command,
/// finding the current values of each variable and placing them into the
/// struct so that it can be passed to the JITted version of the IR.
///
/// Fourth and finally, it "dematerializes" the struct after the JITted code
/// has executed, placing the new values back where it found the old ones.
class ClangExpressionDeclMap : public ClangASTSource {
public:
  /// Constructor
  ///
  /// Initializes class variables.
  ///
  /// \param[in] keep_result_in_memory
  ///     If true, inhibits the normal deallocation of the memory for
  ///     the result persistent variable, and instead marks the variable
  ///     as persisting.
  ///
  /// \param[in] result_delegate
  ///     If non-NULL, use this delegate to report result values.  This
  ///     allows the client ClangUserExpression to report a result.
  ///
  /// \param[in] target
  ///     The target to use when parsing.
  ///
  /// \param[in] importer
  ///     The ClangASTImporter to use when parsing.
  ///
  /// \param[in] ctx_obj
  ///     If not empty, then expression is evaluated in context of this object.
  ///     See the comment to `UserExpression::Evaluate` for details.
  ClangExpressionDeclMap(
      bool keep_result_in_memory,
      Materializer::PersistentVariableDelegate *result_delegate,
      const lldb::TargetSP &target,
      const std::shared_ptr<ClangASTImporter> &importer, ValueObject *ctx_obj);

  /// Destructor
  ~ClangExpressionDeclMap() override;

  /// Enable the state needed for parsing and IR transformation.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to use when finding types for variables.
  ///     Also used to find a "scratch" AST context to store result types.
  ///
  /// \param[in] materializer
  ///     If non-NULL, the materializer to populate with information about
  ///     the variables to use
  ///
  /// \return
  ///     True if parsing is possible; false if it is unsafe to continue.
  bool WillParse(ExecutionContext &exe_ctx, Materializer *materializer);

  void InstallCodeGenerator(clang::ASTConsumer *code_gen);

  void InstallDiagnosticManager(DiagnosticManager &diag_manager);

  /// Disable the state needed for parsing and IR transformation.
  void DidParse();

  /// [Used by IRForTarget] Add a variable to the list of persistent
  ///     variables for the process.
  ///
  /// \param[in] decl
  ///     The Clang declaration for the persistent variable, used for
  ///     lookup during parsing.
  ///
  /// \param[in] name
  ///     The name of the persistent variable, usually $something.
  ///
  /// \param[in] type
  ///     The type of the variable, in the Clang parser's context.
  ///
  /// \return
  ///     True on success; false otherwise.
  bool AddPersistentVariable(const clang::NamedDecl *decl,
                             ConstString name, TypeFromParser type,
                             bool is_result, bool is_lvalue);

  /// [Used by IRForTarget] Add a variable to the struct that needs to
  ///     be materialized each time the expression runs.
  ///
  /// \param[in] decl
  ///     The Clang declaration for the variable.
  ///
  /// \param[in] name
  ///     The name of the variable.
  ///
  /// \param[in] value
  ///     The LLVM IR value for this variable.
  ///
  /// \param[in] size
  ///     The size of the variable in bytes.
  ///
  /// \param[in] alignment
  ///     The required alignment of the variable in bytes.
  ///
  /// \return
  ///     True on success; false otherwise.
  bool AddValueToStruct(const clang::NamedDecl *decl, ConstString name,
                        llvm::Value *value, size_t size,
                        lldb::offset_t alignment);

  /// [Used by IRForTarget] Finalize the struct, laying out the position of
  /// each object in it.
  ///
  /// \return
  ///     True on success; false otherwise.
  bool DoStructLayout();

  /// [Used by IRForTarget] Get general information about the laid-out struct
  /// after DoStructLayout() has been called.
  ///
  /// \param[out] num_elements
  ///     The number of elements in the struct.
  ///
  /// \param[out] size
  ///     The size of the struct, in bytes.
  ///
  /// \param[out] alignment
  ///     The alignment of the struct, in bytes.
  ///
  /// \return
  ///     True if the information could be retrieved; false otherwise.
  bool GetStructInfo(uint32_t &num_elements, size_t &size,
                     lldb::offset_t &alignment);

  /// [Used by IRForTarget] Get specific information about one field of the
  /// laid-out struct after DoStructLayout() has been called.
  ///
  /// \param[out] decl
  ///     The parsed Decl for the field, as generated by ClangASTSource
  ///     on ClangExpressionDeclMap's behalf.  In the case of the result
  ///     value, this will have the name $__lldb_result even if the
  ///     result value ends up having the name $1.  This is an
  ///     implementation detail of IRForTarget.
  ///
  /// \param[out] value
  ///     The IR value for the field (usually a GlobalVariable).  In
  ///     the case of the result value, this will have the correct
  ///     name ($1, for instance).  This is an implementation detail
  ///     of IRForTarget.
  ///
  /// \param[out] offset
  ///     The offset of the field from the beginning of the struct.
  ///     As long as the struct is aligned according to its required
  ///     alignment, this offset will align the field correctly.
  ///
  /// \param[out] name
  ///     The name of the field as used in materialization.
  ///
  /// \param[in] index
  ///     The index of the field about which information is requested.
  ///
  /// \return
  ///     True if the information could be retrieved; false otherwise.
  bool GetStructElement(const clang::NamedDecl *&decl, llvm::Value *&value,
                        lldb::offset_t &offset, ConstString &name,
                        uint32_t index);

  /// [Used by IRForTarget] Get information about a function given its Decl.
  ///
  /// \param[in] decl
  ///     The parsed Decl for the Function, as generated by ClangASTSource
  ///     on ClangExpressionDeclMap's behalf.
  ///
  /// \param[out] ptr
  ///     The absolute address of the function in the target.
  ///
  /// \return
  ///     True if the information could be retrieved; false otherwise.
  bool GetFunctionInfo(const clang::NamedDecl *decl, uint64_t &ptr);

  /// [Used by IRForTarget] Get the address of a symbol given nothing but its
  /// name.
  ///
  /// \param[in] target
  ///     The target to find the symbol in.  If not provided,
  ///     then the current parsing context's Target.
  ///
  /// \param[in] process
  ///     The process to use.  For Objective-C symbols, the process's
  ///     Objective-C language runtime may be queried if the process
  ///     is non-NULL.
  ///
  /// \param[in] name
  ///     The name of the symbol.
  ///
  /// \param[in] module
  ///     The module to limit the search to. This can be NULL
  ///
  /// \return
  ///     Valid load address for the symbol
  lldb::addr_t GetSymbolAddress(Target &target, Process *process,
                                ConstString name, lldb::SymbolType symbol_type,
                                Module *module = nullptr);

  lldb::addr_t GetSymbolAddress(ConstString name,
                                lldb::SymbolType symbol_type);

  struct TargetInfo {
    lldb::ByteOrder byte_order = lldb::eByteOrderInvalid;
    size_t address_byte_size = 0;

    TargetInfo() = default;

    bool IsValid() {
      return (byte_order != lldb::eByteOrderInvalid && address_byte_size != 0);
    }
  };
  TargetInfo GetTargetInfo();

  /// [Used by ClangASTSource] Find all entities matching a given name, using
  /// a NameSearchContext to make Decls for them.
  ///
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  void FindExternalVisibleDecls(NameSearchContext &context) override;

  /// Find all entities matching a given name in a given module/namespace,
  /// using a NameSearchContext to make Decls for them.
  ///
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///
  /// \param[in] module
  ///     If non-NULL, the module to query.
  ///
  /// \param[in] namespace_decl
  ///     If valid and module is non-NULL, the parent namespace.
  void FindExternalVisibleDecls(NameSearchContext &context,
                                lldb::ModuleSP module,
                                const CompilerDeclContext &namespace_decl);

protected:
  /// Retrieves the declaration with the given name from the storage of
  /// persistent declarations.
  ///
  /// \return
  ///     A persistent decl with the given name or a nullptr.
  virtual clang::NamedDecl *GetPersistentDecl(ConstString name);

private:
  ExpressionVariableList
      m_found_entities; ///< All entities that were looked up for the parser.
  ExpressionVariableList
      m_struct_members; ///< All entities that need to be placed in the struct.
  bool m_keep_result_in_memory; ///< True if result persistent variables
                                ///generated by this expression should stay in
                                ///memory.
  Materializer::PersistentVariableDelegate
      *m_result_delegate; ///< If non-NULL, used to report expression results to
                          ///ClangUserExpression.
  ValueObject *m_ctx_obj; ///< If not empty, then expression is
                          ///evaluated in context of this object.
                          ///For details see the comment to
                          ///`UserExpression::Evaluate`.

  /// The following values should not live beyond parsing
  class ParserVars {
  public:
    ParserVars() = default;

    Target *GetTarget() {
      if (m_exe_ctx.GetTargetPtr())
        return m_exe_ctx.GetTargetPtr();
      else if (m_sym_ctx.target_sp)
        return m_sym_ctx.target_sp.get();
      return nullptr;
    }

    ExecutionContext m_exe_ctx; ///< The execution context to use when parsing.
    SymbolContext m_sym_ctx; ///< The symbol context to use in finding variables
                             ///and types.
    ClangPersistentVariables *m_persistent_vars =
        nullptr; ///< The persistent variables for the process.
    bool m_enable_lookups = false; ///< Set to true during parsing if we have
                                   ///found the first "$__lldb" name.
    TargetInfo m_target_info;      ///< Basic information about the target.
    Materializer *m_materializer = nullptr;   ///< If non-NULL, the materializer
                                              ///to use when reporting used
                                              ///variables.
    clang::ASTConsumer *m_code_gen = nullptr; ///< If non-NULL, a code generator
                                              ///that receives new top-level
                                              ///functions.
    DiagnosticManager *m_diagnostics = nullptr;

  private:
    ParserVars(const ParserVars &) = delete;
    const ParserVars &operator=(const ParserVars &) = delete;
  };

  std::unique_ptr<ParserVars> m_parser_vars;

  /// Activate parser-specific variables
  void EnableParserVars() {
    if (!m_parser_vars.get())
      m_parser_vars = std::make_unique<ParserVars>();
  }

  /// Deallocate parser-specific variables
  void DisableParserVars() { m_parser_vars.reset(); }

  /// The following values contain layout information for the materialized
  /// struct, but are not specific to a single materialization
  struct StructVars {
    StructVars() = default;

    lldb::offset_t m_struct_alignment =
        0;                    ///< The alignment of the struct in bytes.
    size_t m_struct_size = 0; ///< The size of the struct in bytes.
    bool m_struct_laid_out =
        false; ///< True if the struct has been laid out and the
               /// layout is valid (that is, no new fields have been
               /// added since).
    ConstString
        m_result_name; ///< The name of the result variable ($1, for example)
  };

  std::unique_ptr<StructVars> m_struct_vars;

  /// Activate struct variables
  void EnableStructVars() {
    if (!m_struct_vars.get())
      m_struct_vars.reset(new struct StructVars);
  }

  /// Deallocate struct variables
  void DisableStructVars() { m_struct_vars.reset(); }

  lldb::TypeSystemClangSP GetScratchContext(Target &target) {
    return ScratchTypeSystemClang::GetForTarget(target,
                                                m_ast_context->getLangOpts());
  }

  /// Get this parser's ID for use in extracting parser- and JIT-specific data
  /// from persistent variables.
  uint64_t GetParserID() { return (uint64_t) this; }

  /// Should be called on all copied functions.
  void MaybeRegisterFunctionBody(clang::FunctionDecl *copied_function_decl);

  /// Searches the persistent decls of the target for entities with the
  /// given name.
  ///
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///
  /// \param[in] name
  ///     The name of the entities that need to be found.
  void SearchPersistenDecls(NameSearchContext &context, const ConstString name);

  /// Handles looking up $__lldb_class which requires special treatment.
  ///
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  void LookUpLldbClass(NameSearchContext &context);

  /// Handles looking up $__lldb_objc_class which requires special treatment.
  ///
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  void LookUpLldbObjCClass(NameSearchContext &context);

  /// Handles looking up the synthetic namespace that contains our local
  /// variables for the current frame.
  ///
  /// \param[in] sym_ctx
  ///     The current SymbolContext of this frame.
  ///
  /// \param[in] name_context
  ///     The NameSearchContext that can construct Decls for this name.
  void LookupLocalVarNamespace(SymbolContext &sym_ctx,
                               NameSearchContext &name_context);

  /// Lookup entities in the ClangModulesDeclVendor.
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///
  /// \param[in] name
  ///     The name of the entities that need to be found.
  void LookupInModulesDeclVendor(NameSearchContext &context, ConstString name);

  /// Looks up a local variable.
  ///
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///
  /// \param[in] name
  ///     The name of the entities that need to be found.
  ///
  /// \param[in] sym_ctx
  ///     The current SymbolContext of this frame.
  ///
  /// \param[in] namespace_decl
  ///     The parent namespace if there is one.
  ///
  /// \return
  ///    True iff a local variable was found.
  bool LookupLocalVariable(NameSearchContext &context, ConstString name,
                           SymbolContext &sym_ctx,
                           const CompilerDeclContext &namespace_decl);

  /// Searches for functions in the given SymbolContextList.
  ///
  /// \param[in] sc_list
  ///     The SymbolContextList to search.
  ///
  /// \param[in] frame_decl_context
  ///     The current DeclContext of the current frame.
  ///
  /// \return
  ///     A SymbolContextList with any found functions in the front and
  ///     any unknown SymbolContexts which are not functions in the back.
  ///     The SymbolContexts for the functions are ordered by how close they are
  ///     to the DeclContext for the given frame DeclContext.
  SymbolContextList SearchFunctionsInSymbolContexts(
      const SymbolContextList &sc_list,
      const CompilerDeclContext &frame_decl_context);

  /// Looks up a function.
  ///
  /// \param[in] context
  ///     The NameSearchContext that can construct Decls for this name.
  ///
  /// \param[in] module_sp
  ///     If non-NULL, the module to query.
  ///
  /// \param[in] name
  ///     The name of the function that should be find.
  ///
  /// \param[in] namespace_decl
  ///     If valid and module is non-NULL, the parent namespace.
  void LookupFunction(NameSearchContext &context, lldb::ModuleSP module_sp,
                      ConstString name,
                      const CompilerDeclContext &namespace_decl);

  /// Given a target, find a variable that matches the given name and type.
  ///
  /// \param[in] target
  ///     The target to use as a basis for finding the variable.
  ///
  /// \param[in] module
  ///     If non-NULL, the module to search.
  ///
  /// \param[in] name
  ///     The name as a plain C string.
  ///
  /// \param[in] namespace_decl
  ///     If non-NULL and module is non-NULL, the parent namespace.
  ///
  /// \return
  ///     The LLDB Variable found, or NULL if none was found.
  lldb::VariableSP
  FindGlobalVariable(Target &target, lldb::ModuleSP &module, ConstString name,
                     const CompilerDeclContext &namespace_decl);

  /// Get the value of a variable in a given execution context and return the
  /// associated Types if needed.
  ///
  /// \param[in] var
  ///     The variable to evaluate.
  ///
  /// \param[out] var_location
  ///     The variable location value to fill in
  ///
  /// \param[out] found_type
  ///     The type of the found value, as it was found in the user process.
  ///     This is only useful when the variable is being inspected on behalf
  ///     of the parser, hence the default.
  ///
  /// \param[out] parser_type
  ///     The type of the found value, as it was copied into the parser's
  ///     AST context.  This is only useful when the variable is being
  ///     inspected on behalf of the parser, hence the default.
  ///
  /// \return
  ///     Return true if the value was successfully filled in.
  bool GetVariableValue(lldb::VariableSP &var,
                        lldb_private::Value &var_location,
                        TypeFromUser *found_type = nullptr,
                        TypeFromParser *parser_type = nullptr);

  /// Use the NameSearchContext to generate a Decl for the given LLDB
  /// ValueObject, and put it in the list of found entities.
  ///
  /// Helper function used by the other AddOneVariable APIs.
  ///
  /// \param[in,out] context
  ///     The NameSearchContext to use when constructing the Decl.
  ///
  /// \param[in] pt
  ///     The CompilerType of the variable we're adding a Decl for.
  ///
  /// \param[in] var
  ///     The LLDB ValueObject that needs a Decl.
  ClangExpressionVariable::ParserVars *
  AddExpressionVariable(NameSearchContext &context, TypeFromParser const &pt,
                        lldb::ValueObjectSP valobj);

  /// Use the NameSearchContext to generate a Decl for the given LLDB
  /// Variable, and put it in the Tuple list.
  ///
  /// \param[in] context
  ///     The NameSearchContext to use when constructing the Decl.
  ///
  /// \param[in] var
  ///     The LLDB Variable that needs a Decl.
  ///
  /// \param[in] valobj
  ///     The LLDB ValueObject for that variable.
  void AddOneVariable(NameSearchContext &context, lldb::VariableSP var,
                      lldb::ValueObjectSP valobj);

  /// Use the NameSearchContext to generate a Decl for the given ValueObject
  /// and put it in the list of found entities.
  ///
  /// \param[in,out] context
  ///     The NameSearchContext to use when constructing the Decl.
  ///
  /// \param[in] valobj
  ///     The ValueObject that needs a Decl.
  ///
  /// \param[in] valobj_provider Callback that fetches a ValueObjectSP
  ///            from the specified frame
  void AddOneVariable(NameSearchContext &context, lldb::ValueObjectSP valobj,
                      ValueObjectProviderTy valobj_provider);

  /// Use the NameSearchContext to generate a Decl for the given persistent
  /// variable, and put it in the list of found entities.
  ///
  /// \param[in] context
  ///     The NameSearchContext to use when constructing the Decl.
  ///
  /// \param[in] pvar_sp
  ///     The persistent variable that needs a Decl.
  void AddOneVariable(NameSearchContext &context,
                      lldb::ExpressionVariableSP &pvar_sp);

  /// Use the NameSearchContext to generate a Decl for the given LLDB symbol
  /// (treated as a variable), and put it in the list of found entities.
  void AddOneGenericVariable(NameSearchContext &context, const Symbol &symbol);

  /// Use the NameSearchContext to generate a Decl for the given function.
  /// (Functions are not placed in the Tuple list.)  Can handle both fully
  /// typed functions and generic functions.
  ///
  /// \param[in] context
  ///     The NameSearchContext to use when constructing the Decl.
  ///
  /// \param[in] fun
  ///     The Function that needs to be created.  If non-NULL, this is
  ///     a fully-typed function.
  ///
  /// \param[in] sym
  ///     The Symbol that corresponds to a function that needs to be
  ///     created with generic type (unitptr_t foo(...)).
  void AddOneFunction(NameSearchContext &context, Function *fun, Symbol *sym);

  /// Use the NameSearchContext to generate a Decl for the given register.
  ///
  /// \param[in] context
  ///     The NameSearchContext to use when constructing the Decl.
  ///
  /// \param[in] reg_info
  ///     The information corresponding to that register.
  void AddOneRegister(NameSearchContext &context, const RegisterInfo *reg_info);

  /// Use the NameSearchContext to generate a Decl for the given type.  (Types
  /// are not placed in the Tuple list.)
  ///
  /// \param[in] context
  ///     The NameSearchContext to use when constructing the Decl.
  ///
  /// \param[in] type
  ///     The type that needs to be created.
  void AddOneType(NameSearchContext &context, const TypeFromUser &type);

  /// Adds the class in which the expression is evaluated to the lookup and
  /// prepares the class to be used as a context for expression evaluation (for
  /// example, it creates a fake member function that will contain the
  /// expression LLDB is trying to evaluate).
  ///
  /// \param[in] context
  ///     The NameSearchContext to which the class should be added as a lookup
  ///     result.
  ///
  /// \param[in] type
  ///     The type of the class that serves as the evaluation context.
  void AddContextClassType(NameSearchContext &context,
                           const TypeFromUser &type);

  /// Move a type out of the current ASTContext into another, but make sure to
  /// export all components of the type also.
  ///
  /// \param[in] target
  ///     The TypeSystemClang to move to.
  /// \param[in] source
  ///     The TypeSystemClang to move from.  This is assumed to be going away.
  /// \param[in] parser_type
  ///     The type as it appears in the source context.
  ///
  /// \return
  ///     Returns the moved type, or an empty type if there was a problem.
  TypeFromUser DeportType(TypeSystemClang &target, TypeSystemClang &source,
                          TypeFromParser parser_type);

  TypeSystemClang *GetTypeSystemClang();
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONDECLMAP_H
