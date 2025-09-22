//===-- ClangModulesDeclVendor.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGMODULESDECLVENDOR_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGMODULESDECLVENDOR_H

#include "lldb/Symbol/SourceModule.h"
#include "lldb/Target/Platform.h"

#include "Plugins/ExpressionParser/Clang/ClangDeclVendor.h"

#include <set>
#include <vector>

namespace lldb_private {

class ClangModulesDeclVendor : public ClangDeclVendor {
public:
  // Constructors and Destructors
  ClangModulesDeclVendor();

  ~ClangModulesDeclVendor() override;

  static bool classof(const DeclVendor *vendor) {
    return vendor->GetKind() == eClangModuleDeclVendor;
  }

  static ClangModulesDeclVendor *Create(Target &target);

  typedef std::vector<ConstString> ModulePath;
  typedef uintptr_t ModuleID;
  typedef std::vector<ModuleID> ModuleVector;

  /// Add a module to the list of modules to search.
  ///
  /// \param[in] module
  ///     The path to the exact module to be loaded.  E.g., if the desired
  ///     module is std.io, then this should be { "std", "io" }.
  ///
  /// \param[in] exported_modules
  ///     If non-NULL, a pointer to a vector to populate with the ID of every
  ///     module that is re-exported by the specified module.
  ///
  /// \param[in] error_stream
  ///     A stream to populate with the output of the Clang parser when
  ///     it tries to load the module.
  ///
  /// \return
  ///     True if the module could be loaded; false if not.  If the
  ///     compiler encountered a fatal error during a previous module
  ///     load, then this will always return false for this ModuleImporter.
  virtual bool AddModule(const SourceModule &module,
                         ModuleVector *exported_modules,
                         Stream &error_stream) = 0;

  /// Add all modules referred to in a given compilation unit to the list
  /// of modules to search.
  ///
  /// \param[in] cu
  ///     The compilation unit to scan for imported modules.
  ///
  /// \param[in] exported_modules
  ///     A vector to populate with the ID of each module loaded (directly
  ///     and via re-exports) in this way.
  ///
  /// \param[in] error_stream
  ///     A stream to populate with the output of the Clang parser when
  ///     it tries to load the modules.
  ///
  /// \return
  ///     True if all modules referred to by the compilation unit could be
  ///     loaded; false if one could not be loaded.  If the compiler
  ///     encountered a fatal error during a previous module
  ///     load, then this will always return false for this ModuleImporter.
  virtual bool AddModulesForCompileUnit(CompileUnit &cu,
                                        ModuleVector &exported_modules,
                                        Stream &error_stream) = 0;

  /// Enumerate all the macros that are defined by a given set of modules
  /// that are already imported.
  ///
  /// \param[in] modules
  ///     The unique IDs for all modules to query.  Later modules have higher
  ///     priority, just as if you @imported them in that order.  This matters
  ///     if module A #defines a macro and module B #undefs it.
  ///
  /// \param[in] handler
  ///     A function to call with the identifier of this macro and the text of
  ///     each #define (including the #define directive). #undef directives are
  ///     not included; we simply elide any corresponding #define. If this
  ///     function returns true, we stop the iteration immediately.
  virtual void ForEachMacro(
      const ModuleVector &modules,
      std::function<bool(llvm::StringRef, llvm::StringRef)> handler) = 0;

  /// Query whether Clang supports modules for a particular language.
  /// LLDB uses this to decide whether to try to find the modules loaded
  /// by a given compile unit.
  ///
  /// \param[in] language
  ///     The language to query for.
  ///
  /// \return
  ///     True if Clang has modules for the given language.
  static bool LanguageSupportsClangModules(lldb::LanguageType language);
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGMODULESDECLVENDOR_H
