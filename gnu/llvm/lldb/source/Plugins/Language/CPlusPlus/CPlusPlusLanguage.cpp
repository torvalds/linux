//===-- CPlusPlusLanguage.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CPlusPlusLanguage.h"

#include <cctype>
#include <cstring>

#include <functional>
#include <memory>
#include <mutex>
#include <set>

#include "llvm/ADT/StringRef.h"
#include "llvm/Demangle/ItaniumDemangle.h"

#include "lldb/Core/Mangled.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/DataFormatters/CXXFunctionPointer.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/VectorType.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"

#include "BlockPointer.h"
#include "CPlusPlusNameParser.h"
#include "Coroutines.h"
#include "CxxStringTypes.h"
#include "Generic.h"
#include "LibCxx.h"
#include "LibCxxAtomic.h"
#include "LibCxxVariant.h"
#include "LibStdcpp.h"
#include "MSVCUndecoratedNameParser.h"
#include "lldb/lldb-enumerations.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

LLDB_PLUGIN_DEFINE(CPlusPlusLanguage)

void CPlusPlusLanguage::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(), "C++ Language",
                                CreateInstance);
}

void CPlusPlusLanguage::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

bool CPlusPlusLanguage::SymbolNameFitsToLanguage(Mangled mangled) const {
  const char *mangled_name = mangled.GetMangledName().GetCString();
  return mangled_name && CPlusPlusLanguage::IsCPPMangledName(mangled_name);
}

ConstString CPlusPlusLanguage::GetDemangledFunctionNameWithoutArguments(
    Mangled mangled) const {
  const char *mangled_name_cstr = mangled.GetMangledName().GetCString();
  ConstString demangled_name = mangled.GetDemangledName();
  if (demangled_name && mangled_name_cstr && mangled_name_cstr[0]) {
    if (mangled_name_cstr[0] == '_' && mangled_name_cstr[1] == 'Z' &&
        (mangled_name_cstr[2] != 'T' && // avoid virtual table, VTT structure,
                                        // typeinfo structure, and typeinfo
                                        // mangled_name
         mangled_name_cstr[2] != 'G' && // avoid guard variables
         mangled_name_cstr[2] != 'Z'))  // named local entities (if we
                                        // eventually handle eSymbolTypeData,
                                        // we will want this back)
    {
      CPlusPlusLanguage::MethodName cxx_method(demangled_name);
      if (!cxx_method.GetBasename().empty()) {
        std::string shortname;
        if (!cxx_method.GetContext().empty())
          shortname = cxx_method.GetContext().str() + "::";
        shortname += cxx_method.GetBasename().str();
        return ConstString(shortname);
      }
    }
  }
  if (demangled_name)
    return demangled_name;
  return mangled.GetMangledName();
}

// Static Functions

Language *CPlusPlusLanguage::CreateInstance(lldb::LanguageType language) {
  // Use plugin for C++ but not for Objective-C++ (which has its own plugin).
  if (Language::LanguageIsCPlusPlus(language) &&
      language != eLanguageTypeObjC_plus_plus)
    return new CPlusPlusLanguage();
  return nullptr;
}

void CPlusPlusLanguage::MethodName::Clear() {
  m_full.Clear();
  m_basename = llvm::StringRef();
  m_context = llvm::StringRef();
  m_arguments = llvm::StringRef();
  m_qualifiers = llvm::StringRef();
  m_return_type = llvm::StringRef();
  m_parsed = false;
  m_parse_error = false;
}

static bool ReverseFindMatchingChars(const llvm::StringRef &s,
                                     const llvm::StringRef &left_right_chars,
                                     size_t &left_pos, size_t &right_pos,
                                     size_t pos = llvm::StringRef::npos) {
  assert(left_right_chars.size() == 2);
  left_pos = llvm::StringRef::npos;
  const char left_char = left_right_chars[0];
  const char right_char = left_right_chars[1];
  pos = s.find_last_of(left_right_chars, pos);
  if (pos == llvm::StringRef::npos || s[pos] == left_char)
    return false;
  right_pos = pos;
  uint32_t depth = 1;
  while (pos > 0 && depth > 0) {
    pos = s.find_last_of(left_right_chars, pos);
    if (pos == llvm::StringRef::npos)
      return false;
    if (s[pos] == left_char) {
      if (--depth == 0) {
        left_pos = pos;
        return left_pos < right_pos;
      }
    } else if (s[pos] == right_char) {
      ++depth;
    }
  }
  return false;
}

static bool IsTrivialBasename(const llvm::StringRef &basename) {
  // Check that the basename matches with the following regular expression
  // "^~?([A-Za-z_][A-Za-z_0-9]*)$" We are using a hand written implementation
  // because it is significantly more efficient then using the general purpose
  // regular expression library.
  size_t idx = 0;
  if (basename.starts_with('~'))
    idx = 1;

  if (basename.size() <= idx)
    return false; // Empty string or "~"

  if (!std::isalpha(basename[idx]) && basename[idx] != '_')
    return false; // First character (after removing the possible '~'') isn't in
                  // [A-Za-z_]

  // Read all characters matching [A-Za-z_0-9]
  ++idx;
  while (idx < basename.size()) {
    if (!std::isalnum(basename[idx]) && basename[idx] != '_')
      break;
    ++idx;
  }

  // We processed all characters. It is a vaild basename.
  return idx == basename.size();
}

/// Writes out the function name in 'full_name' to 'out_stream'
/// but replaces each argument type with the variable name
/// and the corresponding pretty-printed value
static bool PrettyPrintFunctionNameWithArgs(Stream &out_stream,
                                            char const *full_name,
                                            ExecutionContextScope *exe_scope,
                                            VariableList const &args) {
  CPlusPlusLanguage::MethodName cpp_method{ConstString(full_name)};

  if (!cpp_method.IsValid())
    return false;

  llvm::StringRef return_type = cpp_method.GetReturnType();
  if (!return_type.empty()) {
    out_stream.PutCString(return_type);
    out_stream.PutChar(' ');
  }

  out_stream.PutCString(cpp_method.GetScopeQualifiedName());
  out_stream.PutChar('(');

  FormatEntity::PrettyPrintFunctionArguments(out_stream, args, exe_scope);

  out_stream.PutChar(')');

  llvm::StringRef qualifiers = cpp_method.GetQualifiers();
  if (!qualifiers.empty()) {
    out_stream.PutChar(' ');
    out_stream.PutCString(qualifiers);
  }

  return true;
}

bool CPlusPlusLanguage::MethodName::TrySimplifiedParse() {
  // This method tries to parse simple method definitions which are presumably
  // most comman in user programs. Definitions that can be parsed by this
  // function don't have return types and templates in the name.
  // A::B::C::fun(std::vector<T> &) const
  size_t arg_start, arg_end;
  llvm::StringRef full(m_full.GetCString());
  llvm::StringRef parens("()", 2);
  if (ReverseFindMatchingChars(full, parens, arg_start, arg_end)) {
    m_arguments = full.substr(arg_start, arg_end - arg_start + 1);
    if (arg_end + 1 < full.size())
      m_qualifiers = full.substr(arg_end + 1).ltrim();

    if (arg_start == 0)
      return false;
    size_t basename_end = arg_start;
    size_t context_start = 0;
    size_t context_end = full.rfind(':', basename_end);
    if (context_end == llvm::StringRef::npos)
      m_basename = full.substr(0, basename_end);
    else {
      if (context_start < context_end)
        m_context = full.substr(context_start, context_end - 1 - context_start);
      const size_t basename_begin = context_end + 1;
      m_basename = full.substr(basename_begin, basename_end - basename_begin);
    }

    if (IsTrivialBasename(m_basename)) {
      return true;
    } else {
      // The C++ basename doesn't match our regular expressions so this can't
      // be a valid C++ method, clear everything out and indicate an error
      m_context = llvm::StringRef();
      m_basename = llvm::StringRef();
      m_arguments = llvm::StringRef();
      m_qualifiers = llvm::StringRef();
      m_return_type = llvm::StringRef();
      return false;
    }
  }
  return false;
}

void CPlusPlusLanguage::MethodName::Parse() {
  if (!m_parsed && m_full) {
    if (TrySimplifiedParse()) {
      m_parse_error = false;
    } else {
      CPlusPlusNameParser parser(m_full.GetStringRef());
      if (auto function = parser.ParseAsFunctionDefinition()) {
        m_basename = function->name.basename;
        m_context = function->name.context;
        m_arguments = function->arguments;
        m_qualifiers = function->qualifiers;
        m_return_type = function->return_type;
        m_parse_error = false;
      } else {
        m_parse_error = true;
      }
    }
    m_parsed = true;
  }
}

llvm::StringRef CPlusPlusLanguage::MethodName::GetBasename() {
  if (!m_parsed)
    Parse();
  return m_basename;
}

llvm::StringRef CPlusPlusLanguage::MethodName::GetContext() {
  if (!m_parsed)
    Parse();
  return m_context;
}

llvm::StringRef CPlusPlusLanguage::MethodName::GetArguments() {
  if (!m_parsed)
    Parse();
  return m_arguments;
}

llvm::StringRef CPlusPlusLanguage::MethodName::GetQualifiers() {
  if (!m_parsed)
    Parse();
  return m_qualifiers;
}

llvm::StringRef CPlusPlusLanguage::MethodName::GetReturnType() {
  if (!m_parsed)
    Parse();
  return m_return_type;
}

std::string CPlusPlusLanguage::MethodName::GetScopeQualifiedName() {
  if (!m_parsed)
    Parse();
  if (m_context.empty())
    return std::string(m_basename);

  std::string res;
  res += m_context;
  res += "::";
  res += m_basename;
  return res;
}

llvm::StringRef
CPlusPlusLanguage::MethodName::GetBasenameNoTemplateParameters() {
  llvm::StringRef basename = GetBasename();
  size_t arg_start, arg_end;
  llvm::StringRef parens("<>", 2);
  if (ReverseFindMatchingChars(basename, parens, arg_start, arg_end))
    return basename.substr(0, arg_start);

  return basename;
}

bool CPlusPlusLanguage::MethodName::ContainsPath(llvm::StringRef path) {
  if (!m_parsed)
    Parse();

  // If we can't parse the incoming name, then just check that it contains path.
  if (m_parse_error)
    return m_full.GetStringRef().contains(path);

  llvm::StringRef identifier;
  llvm::StringRef context;
  std::string path_str = path.str();
  bool success = CPlusPlusLanguage::ExtractContextAndIdentifier(
      path_str.c_str(), context, identifier);
  if (!success)
    return m_full.GetStringRef().contains(path);

  // Basename may include template arguments.
  // E.g.,
  // GetBaseName(): func<int>
  // identifier   : func
  //
  // ...but we still want to account for identifiers with template parameter
  // lists, e.g., when users set breakpoints on template specializations.
  //
  // E.g.,
  // GetBaseName(): func<uint32_t>
  // identifier   : func<int32_t*>
  //
  // Try to match the basename with or without template parameters.
  if (GetBasename() != identifier &&
      GetBasenameNoTemplateParameters() != identifier)
    return false;

  // Incoming path only had an identifier, so we match.
  if (context.empty())
    return true;
  // Incoming path has context but this method does not, no match.
  if (m_context.empty())
    return false;

  llvm::StringRef haystack = m_context;
  if (!haystack.consume_back(context))
    return false;
  if (haystack.empty() || !isalnum(haystack.back()))
    return true;

  return false;
}

bool CPlusPlusLanguage::IsCPPMangledName(llvm::StringRef name) {
  // FIXME!! we should really run through all the known C++ Language plugins
  // and ask each one if this is a C++ mangled name

  Mangled::ManglingScheme scheme = Mangled::GetManglingScheme(name);

  if (scheme == Mangled::eManglingSchemeNone)
    return false;

  return true;
}

bool CPlusPlusLanguage::DemangledNameContainsPath(llvm::StringRef path,
                                                  ConstString demangled) const {
  MethodName demangled_name(demangled);
  return demangled_name.ContainsPath(path);
}

bool CPlusPlusLanguage::ExtractContextAndIdentifier(
    const char *name, llvm::StringRef &context, llvm::StringRef &identifier) {
  if (MSVCUndecoratedNameParser::IsMSVCUndecoratedName(name))
    return MSVCUndecoratedNameParser::ExtractContextAndIdentifier(name, context,
                                                                  identifier);

  CPlusPlusNameParser parser(name);
  if (auto full_name = parser.ParseAsFullName()) {
    identifier = full_name->basename;
    context = full_name->context;
    return true;
  }
  return false;
}

namespace {
class NodeAllocator {
  llvm::BumpPtrAllocator Alloc;

public:
  void reset() { Alloc.Reset(); }

  template <typename T, typename... Args> T *makeNode(Args &&... args) {
    return new (Alloc.Allocate(sizeof(T), alignof(T)))
        T(std::forward<Args>(args)...);
  }

  void *allocateNodeArray(size_t sz) {
    return Alloc.Allocate(sizeof(llvm::itanium_demangle::Node *) * sz,
                          alignof(llvm::itanium_demangle::Node *));
  }
};

template <typename Derived>
class ManglingSubstitutor
    : public llvm::itanium_demangle::AbstractManglingParser<Derived,
                                                            NodeAllocator> {
  using Base =
      llvm::itanium_demangle::AbstractManglingParser<Derived, NodeAllocator>;

public:
  ManglingSubstitutor() : Base(nullptr, nullptr) {}

  template <typename... Ts>
  ConstString substitute(llvm::StringRef Mangled, Ts &&... Vals) {
    this->getDerived().reset(Mangled, std::forward<Ts>(Vals)...);
    return substituteImpl(Mangled);
  }

protected:
  void reset(llvm::StringRef Mangled) {
    Base::reset(Mangled.begin(), Mangled.end());
    Written = Mangled.begin();
    Result.clear();
    Substituted = false;
  }

  ConstString substituteImpl(llvm::StringRef Mangled) {
    Log *log = GetLog(LLDBLog::Language);
    if (this->parse() == nullptr) {
      LLDB_LOG(log, "Failed to substitute mangling in {0}", Mangled);
      return ConstString();
    }
    if (!Substituted)
      return ConstString();

    // Append any trailing unmodified input.
    appendUnchangedInput();
    LLDB_LOG(log, "Substituted mangling {0} -> {1}", Mangled, Result);
    return ConstString(Result);
  }

  void trySubstitute(llvm::StringRef From, llvm::StringRef To) {
    if (!llvm::StringRef(currentParserPos(), this->numLeft()).starts_with(From))
      return;

    // We found a match. Append unmodified input up to this point.
    appendUnchangedInput();

    // And then perform the replacement.
    Result += To;
    Written += From.size();
    Substituted = true;
  }

private:
  /// Input character until which we have constructed the respective output
  /// already.
  const char *Written = "";

  llvm::SmallString<128> Result;

  /// Whether we have performed any substitutions.
  bool Substituted = false;

  const char *currentParserPos() const { return this->First; }

  void appendUnchangedInput() {
    Result +=
        llvm::StringRef(Written, std::distance(Written, currentParserPos()));
    Written = currentParserPos();
  }
};

/// Given a mangled function `Mangled`, replace all the primitive function type
/// arguments of `Search` with type `Replace`.
class TypeSubstitutor : public ManglingSubstitutor<TypeSubstitutor> {
  llvm::StringRef Search;
  llvm::StringRef Replace;

public:
  void reset(llvm::StringRef Mangled, llvm::StringRef Search,
             llvm::StringRef Replace) {
    ManglingSubstitutor::reset(Mangled);
    this->Search = Search;
    this->Replace = Replace;
  }

  llvm::itanium_demangle::Node *parseType() {
    trySubstitute(Search, Replace);
    return ManglingSubstitutor::parseType();
  }
};

class CtorDtorSubstitutor : public ManglingSubstitutor<CtorDtorSubstitutor> {
public:
  llvm::itanium_demangle::Node *
  parseCtorDtorName(llvm::itanium_demangle::Node *&SoFar, NameState *State) {
    trySubstitute("C1", "C2");
    trySubstitute("D1", "D2");
    return ManglingSubstitutor::parseCtorDtorName(SoFar, State);
  }
};
} // namespace

std::vector<ConstString> CPlusPlusLanguage::GenerateAlternateFunctionManglings(
    const ConstString mangled_name) const {
  std::vector<ConstString> alternates;

  /// Get a basic set of alternative manglings for the given symbol `name`, by
  /// making a few basic possible substitutions on basic types, storage duration
  /// and `const`ness for the given symbol. The output parameter `alternates`
  /// is filled with a best-guess, non-exhaustive set of different manglings
  /// for the given name.

  // Maybe we're looking for a const symbol but the debug info told us it was
  // non-const...
  if (!strncmp(mangled_name.GetCString(), "_ZN", 3) &&
      strncmp(mangled_name.GetCString(), "_ZNK", 4)) {
    std::string fixed_scratch("_ZNK");
    fixed_scratch.append(mangled_name.GetCString() + 3);
    alternates.push_back(ConstString(fixed_scratch));
  }

  // Maybe we're looking for a static symbol but we thought it was global...
  if (!strncmp(mangled_name.GetCString(), "_Z", 2) &&
      strncmp(mangled_name.GetCString(), "_ZL", 3)) {
    std::string fixed_scratch("_ZL");
    fixed_scratch.append(mangled_name.GetCString() + 2);
    alternates.push_back(ConstString(fixed_scratch));
  }

  TypeSubstitutor TS;
  // `char` is implementation defined as either `signed` or `unsigned`.  As a
  // result a char parameter has 3 possible manglings: 'c'-char, 'a'-signed
  // char, 'h'-unsigned char.  If we're looking for symbols with a signed char
  // parameter, try finding matches which have the general case 'c'.
  if (ConstString char_fixup =
          TS.substitute(mangled_name.GetStringRef(), "a", "c"))
    alternates.push_back(char_fixup);

  // long long parameter mangling 'x', may actually just be a long 'l' argument
  if (ConstString long_fixup =
          TS.substitute(mangled_name.GetStringRef(), "x", "l"))
    alternates.push_back(long_fixup);

  // unsigned long long parameter mangling 'y', may actually just be unsigned
  // long 'm' argument
  if (ConstString ulong_fixup =
          TS.substitute(mangled_name.GetStringRef(), "y", "m"))
    alternates.push_back(ulong_fixup);

  if (ConstString ctor_fixup =
          CtorDtorSubstitutor().substitute(mangled_name.GetStringRef()))
    alternates.push_back(ctor_fixup);

  return alternates;
}

ConstString CPlusPlusLanguage::FindBestAlternateFunctionMangledName(
    const Mangled mangled, const SymbolContext &sym_ctx) const {
  ConstString demangled = mangled.GetDemangledName();
  if (!demangled)
    return ConstString();

  CPlusPlusLanguage::MethodName cpp_name(demangled);
  std::string scope_qualified_name = cpp_name.GetScopeQualifiedName();

  if (!scope_qualified_name.size())
    return ConstString();

  if (!sym_ctx.module_sp)
    return ConstString();

  lldb_private::SymbolFile *sym_file = sym_ctx.module_sp->GetSymbolFile();
  if (!sym_file)
    return ConstString();

  std::vector<ConstString> alternates;
  sym_file->GetMangledNamesForFunction(scope_qualified_name, alternates);

  std::vector<ConstString> param_and_qual_matches;
  std::vector<ConstString> param_matches;
  for (size_t i = 0; i < alternates.size(); i++) {
    ConstString alternate_mangled_name = alternates[i];
    Mangled mangled(alternate_mangled_name);
    ConstString demangled = mangled.GetDemangledName();

    CPlusPlusLanguage::MethodName alternate_cpp_name(demangled);
    if (!cpp_name.IsValid())
      continue;

    if (alternate_cpp_name.GetArguments() == cpp_name.GetArguments()) {
      if (alternate_cpp_name.GetQualifiers() == cpp_name.GetQualifiers())
        param_and_qual_matches.push_back(alternate_mangled_name);
      else
        param_matches.push_back(alternate_mangled_name);
    }
  }

  if (param_and_qual_matches.size())
    return param_and_qual_matches[0]; // It is assumed that there will be only
                                      // one!
  else if (param_matches.size())
    return param_matches[0]; // Return one of them as a best match
  else
    return ConstString();
}

static void LoadLibCxxFormatters(lldb::TypeCategoryImplSP cpp_category_sp) {
  if (!cpp_category_sp)
    return;

  TypeSummaryImpl::Flags stl_summary_flags;
  stl_summary_flags.SetCascades(true)
      .SetSkipPointers(false)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(true)
      .SetShowMembersOneLiner(false)
      .SetHideItemNames(false);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStringSummaryProviderASCII,
                "std::string summary provider", "^std::__[[:alnum:]]+::string$",
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStringSummaryProviderASCII,
                "std::string summary provider",
                "^std::__[[:alnum:]]+::basic_string<char, "
                "std::__[[:alnum:]]+::char_traits<char>, "
                "std::__[[:alnum:]]+::allocator<char> >$",
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStringSummaryProviderASCII,
                "std::string summary provider",
                "^std::__[[:alnum:]]+::basic_string<unsigned char, "
                "std::__[[:alnum:]]+::char_traits<unsigned char>, "
                "std::__[[:alnum:]]+::allocator<unsigned char> >$",
                stl_summary_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStringSummaryProviderUTF16,
                "std::u16string summary provider",
                "^std::__[[:alnum:]]+::basic_string<char16_t, "
                "std::__[[:alnum:]]+::char_traits<char16_t>, "
                "std::__[[:alnum:]]+::allocator<char16_t> >$",
                stl_summary_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStringSummaryProviderUTF32,
                "std::u32string summary provider",
                "^std::__[[:alnum:]]+::basic_string<char32_t, "
                "std::__[[:alnum:]]+::char_traits<char32_t>, "
                "std::__[[:alnum:]]+::allocator<char32_t> >$",
                stl_summary_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxWStringSummaryProvider,
                "std::wstring summary provider",
                "^std::__[[:alnum:]]+::wstring$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxWStringSummaryProvider,
                "std::wstring summary provider",
                "^std::__[[:alnum:]]+::basic_string<wchar_t, "
                "std::__[[:alnum:]]+::char_traits<wchar_t>, "
                "std::__[[:alnum:]]+::allocator<wchar_t> >$",
                stl_summary_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStringViewSummaryProviderASCII,
                "std::string_view summary provider",
                "^std::__[[:alnum:]]+::string_view$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStringViewSummaryProviderASCII,
                "std::string_view summary provider",
                "^std::__[[:alnum:]]+::basic_string_view<char, "
                "std::__[[:alnum:]]+::char_traits<char> >$",
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStringViewSummaryProviderASCII,
                "std::string_view summary provider",
                "^std::__[[:alnum:]]+::basic_string_view<unsigned char, "
                "std::__[[:alnum:]]+::char_traits<unsigned char> >$",
                stl_summary_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStringViewSummaryProviderUTF16,
                "std::u16string_view summary provider",
                "^std::__[[:alnum:]]+::basic_string_view<char16_t, "
                "std::__[[:alnum:]]+::char_traits<char16_t> >$",
                stl_summary_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStringViewSummaryProviderUTF32,
                "std::u32string_view summary provider",
                "^std::__[[:alnum:]]+::basic_string_view<char32_t, "
                "std::__[[:alnum:]]+::char_traits<char32_t> >$",
                stl_summary_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxWStringViewSummaryProvider,
                "std::wstring_view summary provider",
                "^std::__[[:alnum:]]+::wstring_view$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxWStringViewSummaryProvider,
                "std::wstring_view summary provider",
                "^std::__[[:alnum:]]+::basic_string_view<wchar_t, "
                "std::__[[:alnum:]]+::char_traits<wchar_t> >$",
                stl_summary_flags, true);

  SyntheticChildren::Flags stl_synth_flags;
  stl_synth_flags.SetCascades(true).SetSkipPointers(false).SetSkipReferences(
      false);
  SyntheticChildren::Flags stl_deref_flags = stl_synth_flags;
  stl_deref_flags.SetFrontEndWantsDereference();

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxBitsetSyntheticFrontEndCreator,
      "libc++ std::bitset synthetic children",
      "^std::__[[:alnum:]]+::bitset<.+>$", stl_deref_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdVectorSyntheticFrontEndCreator,
      "libc++ std::vector synthetic children",
      "^std::__[[:alnum:]]+::vector<.+>$", stl_deref_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdValarraySyntheticFrontEndCreator,
      "libc++ std::valarray synthetic children",
      "^std::__[[:alnum:]]+::valarray<.+>$", stl_deref_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdSliceArraySyntheticFrontEndCreator,
      "libc++ std::slice_array synthetic children",
      "^std::__[[:alnum:]]+::slice_array<.+>$", stl_deref_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdProxyArraySyntheticFrontEndCreator,
      "libc++ synthetic children for the valarray proxy arrays",
      "^std::__[[:alnum:]]+::(gslice|mask|indirect)_array<.+>$",
      stl_deref_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdForwardListSyntheticFrontEndCreator,
      "libc++ std::forward_list synthetic children",
      "^std::__[[:alnum:]]+::forward_list<.+>$", stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdListSyntheticFrontEndCreator,
      "libc++ std::list synthetic children",
      // A POSIX variant of: "^std::__(?!cxx11:)[[:alnum:]]+::list<.+>$"
      // so that it does not clash with: "^std::(__cxx11::)?list<.+>$"
      "^std::__([A-Zabd-z0-9]|cx?[A-Za-wyz0-9]|cxx1?[A-Za-z02-9]|"
      "cxx11[[:alnum:]])[[:alnum:]]*::list<.+>$",
      stl_deref_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator,
      "libc++ std::map synthetic children", "^std::__[[:alnum:]]+::map<.+> >$",
      stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator,
      "libc++ std::set synthetic children", "^std::__[[:alnum:]]+::set<.+> >$",
      stl_deref_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator,
      "libc++ std::multiset synthetic children",
      "^std::__[[:alnum:]]+::multiset<.+> >$", stl_deref_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator,
      "libc++ std::multimap synthetic children",
      "^std::__[[:alnum:]]+::multimap<.+> >$", stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEndCreator,
      "libc++ std::unordered containers synthetic children",
      "^std::__[[:alnum:]]+::unordered_(multi)?(map|set)<.+> >$",
      stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxInitializerListSyntheticFrontEndCreator,
      "libc++ std::initializer_list synthetic children",
      "^std::initializer_list<.+>$", stl_synth_flags, true);
  AddCXXSynthetic(cpp_category_sp, LibcxxQueueFrontEndCreator,
                  "libc++ std::queue synthetic children",
                  "^std::__[[:alnum:]]+::queue<.+>$", stl_synth_flags, true);
  AddCXXSynthetic(cpp_category_sp, LibcxxTupleFrontEndCreator,
                  "libc++ std::tuple synthetic children",
                  "^std::__[[:alnum:]]+::tuple<.*>$", stl_synth_flags, true);
  AddCXXSynthetic(cpp_category_sp, LibcxxOptionalSyntheticFrontEndCreator,
                  "libc++ std::optional synthetic children",
                  "^std::__[[:alnum:]]+::optional<.+>$", stl_synth_flags, true);
  AddCXXSynthetic(cpp_category_sp, LibcxxVariantFrontEndCreator,
                  "libc++ std::variant synthetic children",
                  "^std::__[[:alnum:]]+::variant<.+>$", stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxAtomicSyntheticFrontEndCreator,
      "libc++ std::atomic synthetic children",
      "^std::__[[:alnum:]]+::atomic<.+>$", stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdSpanSyntheticFrontEndCreator,
      "libc++ std::span synthetic children", "^std::__[[:alnum:]]+::span<.+>$",
      stl_deref_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxStdRangesRefViewSyntheticFrontEndCreator,
      "libc++ std::ranges::ref_view synthetic children",
      "^std::__[[:alnum:]]+::ranges::ref_view<.+>$", stl_deref_flags, true);

  cpp_category_sp->AddTypeSynthetic(
      "^std::__[[:alnum:]]+::deque<.+>$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_synth_flags,
          "lldb.formatters.cpp.libcxx.stddeque_SynthProvider")));

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEndCreator,
      "shared_ptr synthetic children", "^std::__[[:alnum:]]+::shared_ptr<.+>$",
      stl_synth_flags, true);

  static constexpr const char *const libcxx_std_unique_ptr_regex =
      "^std::__[[:alnum:]]+::unique_ptr<.+>$";
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxUniquePtrSyntheticFrontEndCreator,
      "unique_ptr synthetic children", libcxx_std_unique_ptr_regex,
      stl_synth_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibcxxSharedPtrSyntheticFrontEndCreator,
      "weak_ptr synthetic children", "^std::__[[:alnum:]]+::weak_ptr<.+>$",
      stl_synth_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxFunctionSummaryProvider,
                "libc++ std::function summary provider",
                "^std::__[[:alnum:]]+::function<.+>$", stl_summary_flags, true);

  static constexpr const char *const libcxx_std_coroutine_handle_regex =
      "^std::__[[:alnum:]]+::coroutine_handle<.+>$";
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::StdlibCoroutineHandleSyntheticFrontEndCreator,
      "coroutine_handle synthetic children", libcxx_std_coroutine_handle_regex,
      stl_deref_flags, true);

  stl_summary_flags.SetDontShowChildren(false);
  stl_summary_flags.SetSkipPointers(false);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::bitset summary provider",
                "^std::__[[:alnum:]]+::bitset<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::vector summary provider",
                "^std::__[[:alnum:]]+::vector<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::valarray summary provider",
                "^std::__[[:alnum:]]+::valarray<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxStdSliceArraySummaryProvider,
                "libc++ std::slice_array summary provider",
                "^std::__[[:alnum:]]+::slice_array<.+>$", stl_summary_flags,
                true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ summary provider for the valarray proxy arrays",
                "^std::__[[:alnum:]]+::(gslice|mask|indirect)_array<.+>$",
                stl_summary_flags, true);
  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::LibcxxContainerSummaryProvider,
      "libc++ std::list summary provider",
      "^std::__[[:alnum:]]+::forward_list<.+>$", stl_summary_flags, true);
  AddCXXSummary(
      cpp_category_sp, lldb_private::formatters::LibcxxContainerSummaryProvider,
      "libc++ std::list summary provider",
      // A POSIX variant of: "^std::__(?!cxx11:)[[:alnum:]]+::list<.+>$"
      // so that it does not clash with: "^std::(__cxx11::)?list<.+>$"
      "^std::__([A-Zabd-z0-9]|cx?[A-Za-wyz0-9]|cxx1?[A-Za-z02-9]|"
      "cxx11[[:alnum:]])[[:alnum:]]*::list<.+>$",
      stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::map summary provider",
                "^std::__[[:alnum:]]+::map<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::deque summary provider",
                "^std::__[[:alnum:]]+::deque<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::queue summary provider",
                "^std::__[[:alnum:]]+::queue<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::set summary provider",
                "^std::__[[:alnum:]]+::set<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::multiset summary provider",
                "^std::__[[:alnum:]]+::multiset<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::multimap summary provider",
                "^std::__[[:alnum:]]+::multimap<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::unordered containers summary provider",
                "^std::__[[:alnum:]]+::unordered_(multi)?(map|set)<.+> >$",
                stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp, LibcxxContainerSummaryProvider,
                "libc++ std::tuple summary provider",
                "^std::__[[:alnum:]]+::tuple<.*>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibCxxAtomicSummaryProvider,
                "libc++ std::atomic summary provider",
                "^std::__[[:alnum:]]+::atomic<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::GenericOptionalSummaryProvider,
                "libc++ std::optional summary provider",
                "^std::__[[:alnum:]]+::optional<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxVariantSummaryProvider,
                "libc++ std::variant summary provider",
                "^std::__[[:alnum:]]+::variant<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxContainerSummaryProvider,
                "libc++ std::span summary provider",
                "^std::__[[:alnum:]]+::span<.+>$", stl_summary_flags, true);

  stl_summary_flags.SetSkipPointers(true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxSmartPointerSummaryProvider,
                "libc++ std::shared_ptr summary provider",
                "^std::__[[:alnum:]]+::shared_ptr<.+>$", stl_summary_flags,
                true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxSmartPointerSummaryProvider,
                "libc++ std::weak_ptr summary provider",
                "^std::__[[:alnum:]]+::weak_ptr<.+>$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxUniquePointerSummaryProvider,
                "libc++ std::unique_ptr summary provider",
                libcxx_std_unique_ptr_regex, stl_summary_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::StdlibCoroutineHandleSummaryProvider,
                "libc++ std::coroutine_handle summary provider",
                libcxx_std_coroutine_handle_regex, stl_summary_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibCxxVectorIteratorSyntheticFrontEndCreator,
      "std::vector iterator synthetic children",
      "^std::__[[:alnum:]]+::__wrap_iter<.+>$", stl_synth_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEndCreator,
      "std::map iterator synthetic children",
      "^std::__[[:alnum:]]+::__map_(const_)?iterator<.+>$", stl_synth_flags,
      true);

  AddCXXSynthetic(cpp_category_sp,
                  lldb_private::formatters::
                      LibCxxUnorderedMapIteratorSyntheticFrontEndCreator,
                  "std::unordered_map iterator synthetic children",
                  "^std::__[[:alnum:]]+::__hash_map_(const_)?iterator<.+>$",
                  stl_synth_flags, true);
  // Chrono duration typedefs
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::nanoseconds", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(
          eTypeOptionHideChildren | eTypeOptionHideValue, "${var.__rep_} ns")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::microseconds", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(
          eTypeOptionHideChildren | eTypeOptionHideValue, "${var.__rep_} µs")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::milliseconds", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(
          eTypeOptionHideChildren | eTypeOptionHideValue, "${var.__rep_} ms")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::seconds", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(
          eTypeOptionHideChildren | eTypeOptionHideValue, "${var.__rep_} s")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::minutes", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__rep_} min")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::hours", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(
          eTypeOptionHideChildren | eTypeOptionHideValue, "${var.__rep_} h")));

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::days", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__rep_} days")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::weeks", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__rep_} weeks")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::months", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__rep_} months")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::years", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__rep_} years")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::seconds", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(
          eTypeOptionHideChildren | eTypeOptionHideValue, "${var.__rep_} s")));

  // Chrono time point types

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxChronoSysSecondsSummaryProvider,
                "libc++ std::chrono::sys_seconds summary provider",
                "^std::__[[:alnum:]]+::chrono::time_point<"
                "std::__[[:alnum:]]+::chrono::system_clock, "
                "std::__[[:alnum:]]+::chrono::duration<.*, "
                "std::__[[:alnum:]]+::ratio<1, 1> "
                "> >$",
                eTypeOptionHideChildren | eTypeOptionHideValue |
                    eTypeOptionCascade,
                true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxChronoSysDaysSummaryProvider,
                "libc++ std::chrono::sys_seconds summary provider",
                "^std::__[[:alnum:]]+::chrono::time_point<"
                "std::__[[:alnum:]]+::chrono::system_clock, "
                "std::__[[:alnum:]]+::chrono::duration<int, "
                "std::__[[:alnum:]]+::ratio<86400, 1> "
                "> >$",
                eTypeOptionHideChildren | eTypeOptionHideValue |
                    eTypeOptionCascade,
                true);

  AddCXXSummary(
      cpp_category_sp,
      lldb_private::formatters::LibcxxChronoLocalSecondsSummaryProvider,
      "libc++ std::chrono::local_seconds summary provider",
      "^std::__[[:alnum:]]+::chrono::time_point<"
      "std::__[[:alnum:]]+::chrono::local_t, "
      "std::__[[:alnum:]]+::chrono::duration<.*, "
      "std::__[[:alnum:]]+::ratio<1, 1> "
      "> >$",
      eTypeOptionHideChildren | eTypeOptionHideValue | eTypeOptionCascade,
      true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxChronoLocalDaysSummaryProvider,
                "libc++ std::chrono::local_seconds summary provider",
                "^std::__[[:alnum:]]+::chrono::time_point<"
                "std::__[[:alnum:]]+::chrono::local_t, "
                "std::__[[:alnum:]]+::chrono::duration<int, "
                "std::__[[:alnum:]]+::ratio<86400, 1> "
                "> >$",
                eTypeOptionHideChildren | eTypeOptionHideValue |
                    eTypeOptionCascade,
                true);

  // Chrono calendar types

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::day$", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "day=${var.__d_%u}")));

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxChronoMonthSummaryProvider,
                "libc++ std::chrono::month summary provider",
                "^std::__[[:alnum:]]+::chrono::month$",
                eTypeOptionHideChildren | eTypeOptionHideValue, true);

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::year$", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(
          eTypeOptionHideChildren | eTypeOptionHideValue, "year=${var.__y_}")));

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibcxxChronoWeekdaySummaryProvider,
                "libc++ std::chrono::weekday summary provider",
                "^std::__[[:alnum:]]+::chrono::weekday$",
                eTypeOptionHideChildren | eTypeOptionHideValue, true);

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::weekday_indexed$", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(
          eTypeOptionHideChildren | eTypeOptionHideValue,
          "${var.__wd_} index=${var.__idx_%u}")));

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::weekday_last$", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__wd_} index=last")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::month_day$", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__m_} ${var.__d_}")));
  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::month_day_last$", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__m_} day=last")));

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::month_weekday$", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__m_} ${var.__wdi_}")));

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::month_weekday_last$", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__m_} ${var.__wdl_}")));

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::year_month$", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__y_} ${var.__m_}")));

  AddCXXSummary(
      cpp_category_sp,
      lldb_private::formatters::LibcxxChronoYearMonthDaySummaryProvider,
      "libc++ std::chrono::year_month_day summary provider",
      "^std::__[[:alnum:]]+::chrono::year_month_day$",
      eTypeOptionHideChildren | eTypeOptionHideValue, true);

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::year_month_day_last$",
      eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(eTypeOptionHideChildren |
                                                    eTypeOptionHideValue,
                                                "${var.__y_} ${var.__mdl_}")));

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::year_month_weekday$", eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(
          eTypeOptionHideChildren | eTypeOptionHideValue,
          "${var.__y_} ${var.__m_} ${var.__wdi_}")));

  cpp_category_sp->AddTypeSummary(
      "^std::__[[:alnum:]]+::chrono::year_month_weekday_last$",
      eFormatterMatchRegex,
      TypeSummaryImplSP(new StringSummaryFormat(
          eTypeOptionHideChildren | eTypeOptionHideValue,
          "${var.__y_} ${var.__m_} ${var.__wdl_}")));
}

static void LoadLibStdcppFormatters(lldb::TypeCategoryImplSP cpp_category_sp) {
  if (!cpp_category_sp)
    return;

  TypeSummaryImpl::Flags stl_summary_flags;
  stl_summary_flags.SetCascades(true)
      .SetSkipPointers(false)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(true)
      .SetShowMembersOneLiner(false)
      .SetHideItemNames(false);

  lldb::TypeSummaryImplSP std_string_summary_sp(
      new StringSummaryFormat(stl_summary_flags, "${var._M_dataplus._M_p}"));

  lldb::TypeSummaryImplSP cxx11_string_summary_sp(new CXXFunctionSummaryFormat(
      stl_summary_flags, LibStdcppStringSummaryProvider,
      "libstdc++ c++11 std::string summary provider"));
  lldb::TypeSummaryImplSP cxx11_wstring_summary_sp(new CXXFunctionSummaryFormat(
      stl_summary_flags, LibStdcppWStringSummaryProvider,
      "libstdc++ c++11 std::wstring summary provider"));

  cpp_category_sp->AddTypeSummary("std::string", eFormatterMatchExact,
                                  std_string_summary_sp);
  cpp_category_sp->AddTypeSummary("std::basic_string<char>",
                                  eFormatterMatchExact, std_string_summary_sp);
  cpp_category_sp->AddTypeSummary(
      "std::basic_string<char,std::char_traits<char>,std::allocator<char> >",
      eFormatterMatchExact, std_string_summary_sp);
  cpp_category_sp->AddTypeSummary(
      "std::basic_string<char, std::char_traits<char>, std::allocator<char> >",
      eFormatterMatchExact, std_string_summary_sp);

  cpp_category_sp->AddTypeSummary("std::__cxx11::string", eFormatterMatchExact,
                                  cxx11_string_summary_sp);
  cpp_category_sp->AddTypeSummary(
      "std::__cxx11::basic_string<char, std::char_traits<char>, "
      "std::allocator<char> >",
      eFormatterMatchExact, cxx11_string_summary_sp);
  cpp_category_sp->AddTypeSummary("std::__cxx11::basic_string<unsigned char, "
                                  "std::char_traits<unsigned char>, "
                                  "std::allocator<unsigned char> >",
                                  eFormatterMatchExact,
                                  cxx11_string_summary_sp);

  // making sure we force-pick the summary for printing wstring (_M_p is a
  // wchar_t*)
  lldb::TypeSummaryImplSP std_wstring_summary_sp(
      new StringSummaryFormat(stl_summary_flags, "${var._M_dataplus._M_p%S}"));

  cpp_category_sp->AddTypeSummary("std::wstring", eFormatterMatchExact,
                                  std_wstring_summary_sp);
  cpp_category_sp->AddTypeSummary("std::basic_string<wchar_t>",
                                  eFormatterMatchExact, std_wstring_summary_sp);
  cpp_category_sp->AddTypeSummary("std::basic_string<wchar_t,std::char_traits<"
                                  "wchar_t>,std::allocator<wchar_t> >",
                                  eFormatterMatchExact, std_wstring_summary_sp);
  cpp_category_sp->AddTypeSummary(
      "std::basic_string<wchar_t, std::char_traits<wchar_t>, "
      "std::allocator<wchar_t> >",
      eFormatterMatchExact, std_wstring_summary_sp);

  cpp_category_sp->AddTypeSummary("std::__cxx11::wstring", eFormatterMatchExact,
                                  cxx11_wstring_summary_sp);
  cpp_category_sp->AddTypeSummary(
      "std::__cxx11::basic_string<wchar_t, std::char_traits<wchar_t>, "
      "std::allocator<wchar_t> >",
      eFormatterMatchExact, cxx11_wstring_summary_sp);

  SyntheticChildren::Flags stl_synth_flags;
  stl_synth_flags.SetCascades(true).SetSkipPointers(false).SetSkipReferences(
      false);
  SyntheticChildren::Flags stl_deref_flags = stl_synth_flags;
  stl_deref_flags.SetFrontEndWantsDereference();

  cpp_category_sp->AddTypeSynthetic(
      "^std::vector<.+>(( )?&)?$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_synth_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdVectorSynthProvider")));
  cpp_category_sp->AddTypeSynthetic(
      "^std::map<.+> >(( )?&)?$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_synth_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdMapLikeSynthProvider")));
  cpp_category_sp->AddTypeSynthetic(
      "^std::deque<.+>(( )?&)?$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_deref_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdDequeSynthProvider")));
  cpp_category_sp->AddTypeSynthetic(
      "^std::set<.+> >(( )?&)?$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_deref_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdMapLikeSynthProvider")));
  cpp_category_sp->AddTypeSynthetic(
      "^std::multimap<.+> >(( )?&)?$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_deref_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdMapLikeSynthProvider")));
  cpp_category_sp->AddTypeSynthetic(
      "^std::multiset<.+> >(( )?&)?$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_deref_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdMapLikeSynthProvider")));
  cpp_category_sp->AddTypeSynthetic(
      "^std::unordered_(multi)?(map|set)<.+> >$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_deref_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdUnorderedMapSynthProvider")));
  cpp_category_sp->AddTypeSynthetic(
      "^std::(__cxx11::)?list<.+>(( )?&)?$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_deref_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdListSynthProvider")));
  cpp_category_sp->AddTypeSynthetic(
      "^std::(__cxx11::)?forward_list<.+>(( )?&)?$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_synth_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.StdForwardListSynthProvider")));
  cpp_category_sp->AddTypeSynthetic(
      "^std::variant<.+>$", eFormatterMatchRegex,
      SyntheticChildrenSP(new ScriptedSyntheticChildren(
          stl_synth_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.VariantSynthProvider")));

  stl_summary_flags.SetDontShowChildren(false);
  stl_summary_flags.SetSkipPointers(false);
  cpp_category_sp->AddTypeSummary("^std::bitset<.+>(( )?&)?$",
                                  eFormatterMatchRegex,
                                  TypeSummaryImplSP(new StringSummaryFormat(
                                      stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->AddTypeSummary("^std::vector<.+>(( )?&)?$",
                                  eFormatterMatchRegex,
                                  TypeSummaryImplSP(new StringSummaryFormat(
                                      stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->AddTypeSummary("^std::map<.+> >(( )?&)?$",
                                  eFormatterMatchRegex,
                                  TypeSummaryImplSP(new StringSummaryFormat(
                                      stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->AddTypeSummary("^std::set<.+> >(( )?&)?$",
                                  eFormatterMatchRegex,
                                  TypeSummaryImplSP(new StringSummaryFormat(
                                      stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->AddTypeSummary("^std::deque<.+>(( )?&)?$",
                                  eFormatterMatchRegex,
                                  TypeSummaryImplSP(new StringSummaryFormat(
                                      stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->AddTypeSummary("^std::multimap<.+> >(( )?&)?$",
                                  eFormatterMatchRegex,
                                  TypeSummaryImplSP(new StringSummaryFormat(
                                      stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->AddTypeSummary("^std::multiset<.+> >(( )?&)?$",
                                  eFormatterMatchRegex,
                                  TypeSummaryImplSP(new StringSummaryFormat(
                                      stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->AddTypeSummary("^std::unordered_(multi)?(map|set)<.+> >$",
                                  eFormatterMatchRegex,
                                  TypeSummaryImplSP(new StringSummaryFormat(
                                      stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->AddTypeSummary("^std::(__cxx11::)?list<.+>(( )?&)?$",
                                  eFormatterMatchRegex,
                                  TypeSummaryImplSP(new StringSummaryFormat(
                                      stl_summary_flags, "size=${svar%#}")));
  cpp_category_sp->AddTypeSummary(
      "^std::(__cxx11::)?forward_list<.+>(( )?&)?$", eFormatterMatchRegex,
      TypeSummaryImplSP(new ScriptSummaryFormat(
          stl_summary_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.ForwardListSummaryProvider")));
  cpp_category_sp->AddTypeSummary(
      "^std::variant<.+>$", eFormatterMatchRegex,
      TypeSummaryImplSP(new ScriptSummaryFormat(
          stl_summary_flags,
          "lldb.formatters.cpp.gnu_libstdcpp.VariantSummaryProvider")));

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppVectorIteratorSyntheticFrontEndCreator,
      "std::vector iterator synthetic children",
      "^__gnu_cxx::__normal_iterator<.+>$", stl_synth_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibstdcppMapIteratorSyntheticFrontEndCreator,
      "std::map iterator synthetic children", "^std::_Rb_tree_iterator<.+>$",
      stl_synth_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppUniquePtrSyntheticFrontEndCreator,
      "std::unique_ptr synthetic children", "^std::unique_ptr<.+>(( )?&)?$",
      stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppSharedPtrSyntheticFrontEndCreator,
      "std::shared_ptr synthetic children", "^std::shared_ptr<.+>(( )?&)?$",
      stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppSharedPtrSyntheticFrontEndCreator,
      "std::weak_ptr synthetic children", "^std::weak_ptr<.+>(( )?&)?$",
      stl_synth_flags, true);
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppTupleSyntheticFrontEndCreator,
      "std::tuple synthetic children", "^std::tuple<.+>(( )?&)?$",
      stl_synth_flags, true);

  static constexpr const char *const libstdcpp_std_coroutine_handle_regex =
      "^std::coroutine_handle<.+>(( )?&)?$";
  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::StdlibCoroutineHandleSyntheticFrontEndCreator,
      "std::coroutine_handle synthetic children",
      libstdcpp_std_coroutine_handle_regex, stl_deref_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppBitsetSyntheticFrontEndCreator,
      "std::bitset synthetic child", "^std::bitset<.+>(( )?&)?$",
      stl_deref_flags, true);

  AddCXXSynthetic(
      cpp_category_sp,
      lldb_private::formatters::LibStdcppOptionalSyntheticFrontEndCreator,
      "std::optional synthetic child", "^std::optional<.+>(( )?&)?$",
      stl_deref_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibStdcppUniquePointerSummaryProvider,
                "libstdc++ std::unique_ptr summary provider",
                "^std::unique_ptr<.+>(( )?&)?$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibStdcppSmartPointerSummaryProvider,
                "libstdc++ std::shared_ptr summary provider",
                "^std::shared_ptr<.+>(( )?&)?$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::LibStdcppSmartPointerSummaryProvider,
                "libstdc++ std::weak_ptr summary provider",
                "^std::weak_ptr<.+>(( )?&)?$", stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::StdlibCoroutineHandleSummaryProvider,
                "libstdc++ std::coroutine_handle summary provider",
                libstdcpp_std_coroutine_handle_regex, stl_summary_flags, true);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::GenericOptionalSummaryProvider,
                "libstd++ std::optional summary provider",
                "^std::optional<.+>(( )?&)?$", stl_summary_flags, true);
}

static void LoadSystemFormatters(lldb::TypeCategoryImplSP cpp_category_sp) {
  if (!cpp_category_sp)
    return;

  TypeSummaryImpl::Flags string_flags;
  string_flags.SetCascades(true)
      .SetSkipPointers(true)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(false)
      .SetShowMembersOneLiner(false)
      .SetHideItemNames(false);

  TypeSummaryImpl::Flags string_array_flags;
  string_array_flags.SetCascades(true)
      .SetSkipPointers(true)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(true)
      .SetShowMembersOneLiner(false)
      .SetHideItemNames(false);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char8StringSummaryProvider,
                "char8_t * summary provider", "char8_t *", string_flags);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char8StringSummaryProvider,
                "char8_t [] summary provider", "char8_t ?\\[[0-9]+\\]",
                string_array_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char16StringSummaryProvider,
                "char16_t * summary provider", "char16_t *", string_flags);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char16StringSummaryProvider,
                "char16_t [] summary provider", "char16_t ?\\[[0-9]+\\]",
                string_array_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char32StringSummaryProvider,
                "char32_t * summary provider", "char32_t *", string_flags);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char32StringSummaryProvider,
                "char32_t [] summary provider", "char32_t ?\\[[0-9]+\\]",
                string_array_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::WCharStringSummaryProvider,
                "wchar_t * summary provider", "wchar_t *", string_flags);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::WCharStringSummaryProvider,
                "wchar_t * summary provider", "wchar_t ?\\[[0-9]+\\]",
                string_array_flags, true);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char16StringSummaryProvider,
                "unichar * summary provider", "unichar *", string_flags);

  TypeSummaryImpl::Flags widechar_flags;
  widechar_flags.SetDontShowValue(true)
      .SetSkipPointers(true)
      .SetSkipReferences(false)
      .SetCascades(true)
      .SetDontShowChildren(true)
      .SetHideItemNames(true)
      .SetShowMembersOneLiner(false);

  AddCXXSummary(cpp_category_sp, lldb_private::formatters::Char8SummaryProvider,
                "char8_t summary provider", "char8_t", widechar_flags);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char16SummaryProvider,
                "char16_t summary provider", "char16_t", widechar_flags);
  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char32SummaryProvider,
                "char32_t summary provider", "char32_t", widechar_flags);
  AddCXXSummary(cpp_category_sp, lldb_private::formatters::WCharSummaryProvider,
                "wchar_t summary provider", "wchar_t", widechar_flags);

  AddCXXSummary(cpp_category_sp,
                lldb_private::formatters::Char16SummaryProvider,
                "unichar summary provider", "unichar", widechar_flags);
}

std::unique_ptr<Language::TypeScavenger> CPlusPlusLanguage::GetTypeScavenger() {
  class CPlusPlusTypeScavenger : public Language::ImageListTypeScavenger {
  public:
    CompilerType AdjustForInclusion(CompilerType &candidate) override {
      LanguageType lang_type(candidate.GetMinimumLanguage());
      if (!Language::LanguageIsC(lang_type) &&
          !Language::LanguageIsCPlusPlus(lang_type))
        return CompilerType();
      if (candidate.IsTypedefType())
        return candidate.GetTypedefedType();
      return candidate;
    }
  };

  return std::unique_ptr<TypeScavenger>(new CPlusPlusTypeScavenger());
}

lldb::TypeCategoryImplSP CPlusPlusLanguage::GetFormatters() {
  static llvm::once_flag g_initialize;
  static TypeCategoryImplSP g_category;

  llvm::call_once(g_initialize, [this]() -> void {
    DataVisualization::Categories::GetCategory(ConstString(GetPluginName()),
                                               g_category);
    if (g_category) {
      LoadLibStdcppFormatters(g_category);
      LoadLibCxxFormatters(g_category);
      LoadSystemFormatters(g_category);
    }
  });
  return g_category;
}

HardcodedFormatters::HardcodedSummaryFinder
CPlusPlusLanguage::GetHardcodedSummaries() {
  static llvm::once_flag g_initialize;
  static ConstString g_vectortypes("VectorTypes");
  static HardcodedFormatters::HardcodedSummaryFinder g_formatters;

  llvm::call_once(g_initialize, []() -> void {
    g_formatters.push_back(
        [](lldb_private::ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &) -> TypeSummaryImpl::SharedPointer {
          static CXXFunctionSummaryFormat::SharedPointer formatter_sp(
              new CXXFunctionSummaryFormat(
                  TypeSummaryImpl::Flags(),
                  lldb_private::formatters::CXXFunctionPointerSummaryProvider,
                  "Function pointer summary provider"));
          if (CompilerType CT = valobj.GetCompilerType();
              CT.IsFunctionPointerType() || CT.IsMemberFunctionPointerType() ||
              valobj.GetValueType() == lldb::eValueTypeVTableEntry) {
            return formatter_sp;
          }
          return nullptr;
        });
    g_formatters.push_back(
        [](lldb_private::ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &fmt_mgr) -> TypeSummaryImpl::SharedPointer {
          static CXXFunctionSummaryFormat::SharedPointer formatter_sp(
              new CXXFunctionSummaryFormat(
                  TypeSummaryImpl::Flags()
                      .SetCascades(true)
                      .SetDontShowChildren(true)
                      .SetHideItemNames(true)
                      .SetShowMembersOneLiner(true)
                      .SetSkipPointers(true)
                      .SetSkipReferences(false),
                  lldb_private::formatters::VectorTypeSummaryProvider,
                  "vector_type pointer summary provider"));
          if (valobj.GetCompilerType().IsVectorType()) {
            if (fmt_mgr.GetCategory(g_vectortypes)->IsEnabled())
              return formatter_sp;
          }
          return nullptr;
        });
    g_formatters.push_back(
        [](lldb_private::ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &fmt_mgr) -> TypeSummaryImpl::SharedPointer {
          static CXXFunctionSummaryFormat::SharedPointer formatter_sp(
              new CXXFunctionSummaryFormat(
                  TypeSummaryImpl::Flags()
                      .SetCascades(true)
                      .SetDontShowChildren(true)
                      .SetHideItemNames(true)
                      .SetShowMembersOneLiner(true)
                      .SetSkipPointers(true)
                      .SetSkipReferences(false),
                  lldb_private::formatters::BlockPointerSummaryProvider,
                  "block pointer summary provider"));
          if (valobj.GetCompilerType().IsBlockPointerType()) {
            return formatter_sp;
          }
          return nullptr;
        });
  });

  return g_formatters;
}

HardcodedFormatters::HardcodedSyntheticFinder
CPlusPlusLanguage::GetHardcodedSynthetics() {
  static llvm::once_flag g_initialize;
  static ConstString g_vectortypes("VectorTypes");
  static HardcodedFormatters::HardcodedSyntheticFinder g_formatters;

  llvm::call_once(g_initialize, []() -> void {
    g_formatters.push_back([](lldb_private::ValueObject &valobj,
                              lldb::DynamicValueType, FormatManager &fmt_mgr)
                               -> SyntheticChildren::SharedPointer {
      static CXXSyntheticChildren::SharedPointer formatter_sp(
          new CXXSyntheticChildren(
              SyntheticChildren::Flags()
                  .SetCascades(true)
                  .SetSkipPointers(true)
                  .SetSkipReferences(true)
                  .SetNonCacheable(true),
              "vector_type synthetic children",
              lldb_private::formatters::VectorTypeSyntheticFrontEndCreator));
      if (valobj.GetCompilerType().IsVectorType()) {
        if (fmt_mgr.GetCategory(g_vectortypes)->IsEnabled())
          return formatter_sp;
      }
      return nullptr;
    });
    g_formatters.push_back([](lldb_private::ValueObject &valobj,
                              lldb::DynamicValueType, FormatManager &fmt_mgr)
                               -> SyntheticChildren::SharedPointer {
      static CXXSyntheticChildren::SharedPointer formatter_sp(
          new CXXSyntheticChildren(
              SyntheticChildren::Flags()
                  .SetCascades(true)
                  .SetSkipPointers(true)
                  .SetSkipReferences(true)
                  .SetNonCacheable(true),
              "block pointer synthetic children",
              lldb_private::formatters::BlockPointerSyntheticFrontEndCreator));
      if (valobj.GetCompilerType().IsBlockPointerType()) {
        return formatter_sp;
      }
      return nullptr;
    });
  });

  return g_formatters;
}

bool CPlusPlusLanguage::IsNilReference(ValueObject &valobj) {
  if (!Language::LanguageIsCPlusPlus(valobj.GetObjectRuntimeLanguage()) ||
      !valobj.IsPointerType())
    return false;
  bool canReadValue = true;
  bool isZero = valobj.GetValueAsUnsigned(0, &canReadValue) == 0;
  return canReadValue && isZero;
}

bool CPlusPlusLanguage::IsSourceFile(llvm::StringRef file_path) const {
  const auto suffixes = {".cpp", ".cxx", ".c++", ".cc",  ".c",
                         ".h",   ".hh",  ".hpp", ".hxx", ".h++"};
  for (auto suffix : suffixes) {
    if (file_path.ends_with_insensitive(suffix))
      return true;
  }

  // Check if we're in a STL path (where the files usually have no extension
  // that we could check for.
  return file_path.contains("/usr/include/c++/");
}

bool CPlusPlusLanguage::GetFunctionDisplayName(
    const SymbolContext *sc, const ExecutionContext *exe_ctx,
    FunctionNameRepresentation representation, Stream &s) {
  switch (representation) {
  case FunctionNameRepresentation::eNameWithArgs: {
    // Print the function name with arguments in it
    if (sc->function) {
      ExecutionContextScope *exe_scope =
          exe_ctx ? exe_ctx->GetBestExecutionContextScope() : nullptr;
      const char *cstr = sc->function->GetName().AsCString(nullptr);
      if (cstr) {
        const InlineFunctionInfo *inline_info = nullptr;
        VariableListSP variable_list_sp;
        bool get_function_vars = true;
        if (sc->block) {
          Block *inline_block = sc->block->GetContainingInlinedBlock();

          if (inline_block) {
            get_function_vars = false;
            inline_info = inline_block->GetInlinedFunctionInfo();
            if (inline_info)
              variable_list_sp = inline_block->GetBlockVariableList(true);
          }
        }

        if (get_function_vars) {
          variable_list_sp =
              sc->function->GetBlock(true).GetBlockVariableList(true);
        }

        if (inline_info) {
          s.PutCString(cstr);
          s.PutCString(" [inlined] ");
          cstr = inline_info->GetName().GetCString();
        }

        VariableList args;
        if (variable_list_sp)
          variable_list_sp->AppendVariablesWithScope(eValueTypeVariableArgument,
                                                     args);
        if (args.GetSize() > 0) {
          if (!PrettyPrintFunctionNameWithArgs(s, cstr, exe_scope, args))
            return false;
        } else {
          s.PutCString(cstr);
        }
        return true;
      }
    } else if (sc->symbol) {
      const char *cstr = sc->symbol->GetName().AsCString(nullptr);
      if (cstr) {
        s.PutCString(cstr);
        return true;
      }
    }
  } break;
  default:
    return false;
  }

  return false;
}
