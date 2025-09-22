//===-- Language.h ---------------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_LANGUAGE_H
#define LLDB_TARGET_LANGUAGE_H

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "lldb/Core/Highlighter.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/DataFormatters/DumpValueObjectOptions.h"
#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/DataFormatters/StringPrinter.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/lldb-private.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

class LanguageProperties : public Properties {
public:
  LanguageProperties();

  static llvm::StringRef GetSettingName();

  bool GetEnableFilterForLineBreakpoints() const;
};

class Language : public PluginInterface {
public:
  class TypeScavenger {
  public:
    class Result {
    public:
      virtual bool IsValid() = 0;

      virtual bool DumpToStream(Stream &stream,
                                bool print_help_if_available) = 0;

      virtual ~Result() = default;
    };

    typedef std::set<std::unique_ptr<Result>> ResultSet;

    virtual ~TypeScavenger() = default;

    size_t Find(ExecutionContextScope *exe_scope, const char *key,
                ResultSet &results, bool append = true);

  protected:
    TypeScavenger() = default;

    virtual bool Find_Impl(ExecutionContextScope *exe_scope, const char *key,
                           ResultSet &results) = 0;
  };

  class ImageListTypeScavenger : public TypeScavenger {
    class Result : public Language::TypeScavenger::Result {
    public:
      Result(CompilerType type) : m_compiler_type(type) {}

      bool IsValid() override { return m_compiler_type.IsValid(); }

      bool DumpToStream(Stream &stream, bool print_help_if_available) override {
        if (IsValid()) {
          m_compiler_type.DumpTypeDescription(&stream);
          stream.EOL();
          return true;
        }
        return false;
      }

      ~Result() override = default;

    private:
      CompilerType m_compiler_type;
    };

  protected:
    ImageListTypeScavenger() = default;

    ~ImageListTypeScavenger() override = default;

    // is this type something we should accept? it's usually going to be a
    // filter by language + maybe some sugar tweaking
    // returning an empty type means rejecting this candidate entirely;
    // any other result will be accepted as a valid match
    virtual CompilerType AdjustForInclusion(CompilerType &candidate) = 0;

    bool Find_Impl(ExecutionContextScope *exe_scope, const char *key,
                   ResultSet &results) override;
  };

  template <typename... ScavengerTypes>
  class EitherTypeScavenger : public TypeScavenger {
  public:
    EitherTypeScavenger() : TypeScavenger() {
      for (std::shared_ptr<TypeScavenger> scavenger : { std::shared_ptr<TypeScavenger>(new ScavengerTypes())... }) {
        if (scavenger)
          m_scavengers.push_back(scavenger);
      }
    }
  protected:
    bool Find_Impl(ExecutionContextScope *exe_scope, const char *key,
                   ResultSet &results) override {
      const bool append = false;
      for (auto& scavenger : m_scavengers) {
        if (scavenger && scavenger->Find(exe_scope, key, results, append))
          return true;
      }
      return false;
    }
  private:
    std::vector<std::shared_ptr<TypeScavenger>> m_scavengers;
  };

  template <typename... ScavengerTypes>
  class UnionTypeScavenger : public TypeScavenger {
  public:
    UnionTypeScavenger() : TypeScavenger() {
      for (std::shared_ptr<TypeScavenger> scavenger : { std::shared_ptr<TypeScavenger>(new ScavengerTypes())... }) {
        if (scavenger)
          m_scavengers.push_back(scavenger);
      }
    }
  protected:
    bool Find_Impl(ExecutionContextScope *exe_scope, const char *key,
                   ResultSet &results) override {
      const bool append = true;
      bool success = false;
      for (auto& scavenger : m_scavengers) {
        if (scavenger)
          success = scavenger->Find(exe_scope, key, results, append) || success;
      }
      return success;
    }
  private:
    std::vector<std::shared_ptr<TypeScavenger>> m_scavengers;
  };

  enum class FunctionNameRepresentation {
    eName,
    eNameWithArgs,
    eNameWithNoArgs
  };

  ~Language() override;

  static Language *FindPlugin(lldb::LanguageType language);

  /// Returns the Language associated with the given file path or a nullptr
  /// if there is no known language.
  static Language *FindPlugin(llvm::StringRef file_path);

  static Language *FindPlugin(lldb::LanguageType language,
                              llvm::StringRef file_path);

  // return false from callback to stop iterating
  static void ForEach(std::function<bool(Language *)> callback);

  virtual lldb::LanguageType GetLanguageType() const = 0;

  // Implement this function to return the user-defined entry point name
  // for the language.
  virtual llvm::StringRef GetUserEntryPointName() const { return {}; }

  virtual bool IsTopLevelFunction(Function &function);

  virtual bool IsSourceFile(llvm::StringRef file_path) const = 0;

  virtual const Highlighter *GetHighlighter() const { return nullptr; }

  virtual lldb::TypeCategoryImplSP GetFormatters();

  virtual HardcodedFormatters::HardcodedFormatFinder GetHardcodedFormats();

  virtual HardcodedFormatters::HardcodedSummaryFinder GetHardcodedSummaries();

  virtual HardcodedFormatters::HardcodedSyntheticFinder
  GetHardcodedSynthetics();

  virtual std::vector<FormattersMatchCandidate>
  GetPossibleFormattersMatches(ValueObject &valobj,
                               lldb::DynamicValueType use_dynamic);

  virtual std::unique_ptr<TypeScavenger> GetTypeScavenger();

  virtual const char *GetLanguageSpecificTypeLookupHelp();

  class MethodNameVariant {
    ConstString m_name;
    lldb::FunctionNameType m_type;

  public:
    MethodNameVariant(ConstString name, lldb::FunctionNameType type)
        : m_name(name), m_type(type) {}
    ConstString GetName() const { return m_name; }
    lldb::FunctionNameType GetType() const { return m_type; }
  };
  // If a language can have more than one possible name for a method, this
  // function can be used to enumerate them. This is useful when doing name
  // lookups.
  virtual std::vector<Language::MethodNameVariant>
  GetMethodNameVariants(ConstString method_name) const {
    return std::vector<Language::MethodNameVariant>();
  };

  /// Returns true iff the given symbol name is compatible with the mangling
  /// scheme of this language.
  ///
  /// This function should only return true if there is a high confidence
  /// that the name actually belongs to this language.
  virtual bool SymbolNameFitsToLanguage(Mangled name) const { return false; }

  /// An individual data formatter may apply to several types and cross language
  /// boundaries. Each of those languages may want to customize the display of
  /// values of said types by appending proper prefix/suffix information in
  /// language-specific ways. This function returns that prefix and suffix.
  ///
  /// \param[in] type_hint
  ///   A StringRef used to determine what the prefix and suffix should be. It
  ///   is called a hint because some types may have multiple variants for which
  ///   the prefix and/or suffix may vary.
  ///
  /// \return
  ///   A std::pair<StringRef, StringRef>, the first being the prefix and the
  ///   second being the suffix. They may be empty.
  virtual std::pair<llvm::StringRef, llvm::StringRef>
  GetFormatterPrefixSuffix(llvm::StringRef type_hint);

  // When looking up functions, we take a user provided string which may be a
  // partial match to the full demangled name and compare it to the actual
  // demangled name to see if it matches as much as the user specified.  An
  // example of this is if the user provided A::my_function, but the
  // symbol was really B::A::my_function.  We want that to be
  // a match.  But we wouldn't want this to match AnotherA::my_function.  The
  // user is specifying a truncated path, not a truncated set of characters.
  // This function does a language-aware comparison for those purposes.
  virtual bool DemangledNameContainsPath(llvm::StringRef path, 
                                         ConstString demangled) const;

  // if a language has a custom format for printing variable declarations that
  // it wants LLDB to honor it should return an appropriate closure here
  virtual DumpValueObjectOptions::DeclPrintingHelper GetDeclPrintingHelper();

  virtual LazyBool IsLogicalTrue(ValueObject &valobj, Status &error);

  // for a ValueObject of some "reference type", if the value points to the
  // nil/null object, this method returns true
  virtual bool IsNilReference(ValueObject &valobj);

  /// Returns the summary string for ValueObjects for which IsNilReference() is
  /// true.
  virtual llvm::StringRef GetNilReferenceSummaryString() { return {}; }

  // for a ValueObject of some "reference type", if the language provides a
  // technique to decide whether the reference has ever been assigned to some
  // object, this method will return true if such detection is possible, and if
  // the reference has never been assigned
  virtual bool IsUninitializedReference(ValueObject &valobj);

  virtual bool GetFunctionDisplayName(const SymbolContext *sc,
                                      const ExecutionContext *exe_ctx,
                                      FunctionNameRepresentation representation,
                                      Stream &s);

  virtual ConstString
  GetDemangledFunctionNameWithoutArguments(Mangled mangled) const {
    if (ConstString demangled = mangled.GetDemangledName())
      return demangled;

    return mangled.GetMangledName();
  }

  virtual ConstString GetDisplayDemangledName(Mangled mangled) const {
    return mangled.GetDemangledName();
  }

  virtual void GetExceptionResolverDescription(bool catch_on, bool throw_on,
                                               Stream &s);

  static void GetDefaultExceptionResolverDescription(bool catch_on,
                                                     bool throw_on, Stream &s);

  // These are accessors for general information about the Languages lldb knows
  // about:

  static lldb::LanguageType
  GetLanguageTypeFromString(const char *string) = delete;
  static lldb::LanguageType GetLanguageTypeFromString(llvm::StringRef string);

  static const char *GetNameForLanguageType(lldb::LanguageType language);

  static void PrintAllLanguages(Stream &s, const char *prefix,
                                const char *suffix);

  /// Prints to the specified stream 's' each language type that the
  /// current target supports for expression evaluation.
  ///
  /// \param[out] s      Stream to which the language types are written.
  /// \param[in]  prefix String that is prepended to the language type.
  /// \param[in]  suffix String that is appended to the language type.
  static void PrintSupportedLanguagesForExpressions(Stream &s,
                                                    llvm::StringRef prefix,
                                                    llvm::StringRef suffix);

  // return false from callback to stop iterating
  static void ForAllLanguages(std::function<bool(lldb::LanguageType)> callback);

  static bool LanguageIsCPlusPlus(lldb::LanguageType language);

  static bool LanguageIsObjC(lldb::LanguageType language);

  static bool LanguageIsC(lldb::LanguageType language);

  /// Equivalent to \c LanguageIsC||LanguageIsObjC||LanguageIsCPlusPlus.
  static bool LanguageIsCFamily(lldb::LanguageType language);

  static bool LanguageIsPascal(lldb::LanguageType language);

  // return the primary language, so if LanguageIsC(l), return eLanguageTypeC,
  // etc.
  static lldb::LanguageType GetPrimaryLanguage(lldb::LanguageType language);

  static std::set<lldb::LanguageType> GetSupportedLanguages();

  static LanguageSet GetLanguagesSupportingTypeSystems();
  static LanguageSet GetLanguagesSupportingTypeSystemsForExpressions();
  static LanguageSet GetLanguagesSupportingREPLs();

  static LanguageProperties &GetGlobalLanguageProperties();

  // Given a mangled function name, calculates some alternative manglings since
  // the compiler mangling may not line up with the symbol we are expecting.
  virtual std::vector<ConstString>
  GenerateAlternateFunctionManglings(const ConstString mangled) const {
    return std::vector<ConstString>();
  }

  virtual ConstString
  FindBestAlternateFunctionMangledName(const Mangled mangled,
                                       const SymbolContext &sym_ctx) const {
    return ConstString();
  }

  virtual llvm::StringRef GetInstanceVariableName() { return {}; }

  /// Returns true if this SymbolContext should be ignored when setting
  /// breakpoints by line (number or regex). Helpful for languages that create
  /// artificial functions without meaningful user code associated with them
  /// (e.g. code that gets expanded in late compilation stages, like by
  /// CoroSplitter).
  virtual bool IgnoreForLineBreakpoints(const SymbolContext &) const {
    return false;
  }

  /// Returns true if this Language supports exception breakpoints on throw via
  /// a corresponding LanguageRuntime plugin.
  virtual bool SupportsExceptionBreakpointsOnThrow() const { return false; }

  /// Returns true if this Language supports exception breakpoints on catch via
  /// a corresponding LanguageRuntime plugin.
  virtual bool SupportsExceptionBreakpointsOnCatch() const { return false; }

  /// Returns the keyword used for throw statements in this language, e.g.
  /// Python uses \b raise. Defaults to \b throw.
  virtual llvm::StringRef GetThrowKeyword() const { return "throw"; }

  /// Returns the keyword used for catch statements in this language, e.g.
  /// Python uses \b except. Defaults to \b catch.
  virtual llvm::StringRef GetCatchKeyword() const { return "catch"; }

protected:
  // Classes that inherit from Language can see and modify these

  Language();

private:
  Language(const Language &) = delete;
  const Language &operator=(const Language &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_LANGUAGE_H
