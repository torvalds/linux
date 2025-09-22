//===-- ObjCLanguage.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_OBJCLANGUAGE_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_OBJCLANGUAGE_H

#include <cstring>
#include <vector>

#include "Plugins/Language/ClangCommon/ClangHighlighter.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class ObjCLanguage : public Language {
  ClangHighlighter m_highlighter;

public:
  class MethodName {
  public:
    /// The static factory method for creating a MethodName.
    ///
    /// \param[in] name
    ///   The name of the method.
    ///
    /// \param[in] strict
    ///   Control whether or not the name parser is strict about +/- in the
    ///   front of the name.
    ///
    /// \return If the name failed to parse as a valid Objective-C method name,
    /// returns std::nullopt. Otherwise returns a const MethodName.
    static std::optional<const MethodName> Create(llvm::StringRef name,
                                                  bool strict);

    /// Determines if this method is a class method
    ///
    /// \return Returns true if the method is a class method. False otherwise.
    bool IsClassMethod() const { return m_type == eTypeClassMethod; }

    /// Determines if this method is an instance method
    ///
    /// \return Returns true if the method is an instance method. False
    /// otherwise.
    bool IsInstanceMethod() const { return m_type == eTypeInstanceMethod; }

    /// Returns the full name of the method.
    ///
    /// This includes the class name, the category name (if applicable), and the
    /// selector name.
    ///
    /// \return The name of the method in the form of a const std::string
    /// reference.
    const std::string &GetFullName() const { return m_full; }

    /// Creates a variation of this method without the category.
    /// If this method has no category, it returns an empty string.
    ///
    /// Example:
    ///   Full name: "+[NSString(my_additions) myStringWithCString:]"
    ///   becomes "+[NSString myStringWithCString:]"
    ///
    /// \return The method name without the category or an empty string if there
    /// was no category to begin with.
    std::string GetFullNameWithoutCategory() const;

    /// Returns a reference to the class name.
    ///
    /// Example:
    ///   Full name: "+[NSString(my_additions) myStringWithCString:]"
    ///   will give you "NSString"
    ///
    /// \return A StringRef to the class name of this method.
    llvm::StringRef GetClassName() const;

    /// Returns a reference to the class name with the category.
    ///
    /// Example:
    ///   Full name: "+[NSString(my_additions) myStringWithCString:]"
    ///   will give you "NSString(my_additions)"
    ///
    /// Note: If your method has no category, this will give the same output as
    /// `GetClassName`.
    ///
    /// \return A StringRef to the class name (including the category) of this
    /// method. If there was no category, returns the same as `GetClassName`.
    llvm::StringRef GetClassNameWithCategory() const;

    /// Returns a reference to the category name.
    ///
    /// Example:
    ///   Full name: "+[NSString(my_additions) myStringWithCString:]"
    ///   will give you "my_additions"
    /// \return A StringRef to the category name of this method. If no category
    /// is present, the StringRef is empty.
    llvm::StringRef GetCategory() const;

    /// Returns a reference to the selector name.
    ///
    /// Example:
    ///   Full name: "+[NSString(my_additions) myStringWithCString:]"
    ///   will give you "myStringWithCString:"
    /// \return A StringRef to the selector of this method.
    llvm::StringRef GetSelector() const;

  protected:
    enum Type { eTypeUnspecified, eTypeClassMethod, eTypeInstanceMethod };

    MethodName(llvm::StringRef name, Type type)
        : m_full(name.str()), m_type(type) {}

    const std::string m_full;
    Type m_type;
  };

  ObjCLanguage() = default;

  ~ObjCLanguage() override = default;

  lldb::LanguageType GetLanguageType() const override {
    return lldb::eLanguageTypeObjC;
  }

  llvm::StringRef GetUserEntryPointName() const override { return "main"; }

  // Get all possible names for a method. Examples:
  // If method_name is "+[NSString(my_additions) myStringWithCString:]"
  //   variant_names[0] => "+[NSString myStringWithCString:]"
  // If name is specified without the leading '+' or '-' like
  // "[NSString(my_additions) myStringWithCString:]"
  //  variant_names[0] => "+[NSString(my_additions) myStringWithCString:]"
  //  variant_names[1] => "-[NSString(my_additions) myStringWithCString:]"
  //  variant_names[2] => "+[NSString myStringWithCString:]"
  //  variant_names[3] => "-[NSString myStringWithCString:]"
  // Also returns the FunctionNameType of each possible name.
  std::vector<Language::MethodNameVariant>
  GetMethodNameVariants(ConstString method_name) const override;

  bool SymbolNameFitsToLanguage(Mangled mangled) const override;

  lldb::TypeCategoryImplSP GetFormatters() override;

  std::vector<FormattersMatchCandidate>
  GetPossibleFormattersMatches(ValueObject &valobj,
                               lldb::DynamicValueType use_dynamic) override;

  std::unique_ptr<TypeScavenger> GetTypeScavenger() override;

  std::pair<llvm::StringRef, llvm::StringRef>
  GetFormatterPrefixSuffix(llvm::StringRef type_hint) override;

  bool IsNilReference(ValueObject &valobj) override;

  llvm::StringRef GetNilReferenceSummaryString() override { return "nil"; }

  bool IsSourceFile(llvm::StringRef file_path) const override;

  const Highlighter *GetHighlighter() const override { return &m_highlighter; }

  // Static Functions
  static void Initialize();

  static void Terminate();

  static lldb_private::Language *CreateInstance(lldb::LanguageType language);

  static llvm::StringRef GetPluginNameStatic() { return "objc"; }

  static bool IsPossibleObjCMethodName(const char *name) {
    if (!name)
      return false;
    bool starts_right = (name[0] == '+' || name[0] == '-') && name[1] == '[';
    bool ends_right = (name[strlen(name) - 1] == ']');
    return (starts_right && ends_right);
  }

  static bool IsPossibleObjCSelector(const char *name) {
    if (!name)
      return false;

    if (strchr(name, ':') == nullptr)
      return true;
    else if (name[strlen(name) - 1] == ':')
      return true;
    else
      return false;
  }

  llvm::StringRef GetInstanceVariableName() override { return "self"; }

  bool SupportsExceptionBreakpointsOnThrow() const override { return true; }

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_OBJCLANGUAGE_H
