//===-- Type.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_TYPE_H
#define LLDB_SYMBOL_TYPE_H

#include "lldb/Core/Declaration.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>
#include <set>

namespace lldb_private {
class SymbolFileCommon;

/// A SmallBitVector that represents a set of source languages (\p
/// lldb::LanguageType).  Each lldb::LanguageType is represented by
/// the bit with the position of its enumerator. The largest
/// LanguageType is < 64, so this is space-efficient and on 64-bit
/// architectures a LanguageSet can be completely stack-allocated.
struct LanguageSet {
  llvm::SmallBitVector bitvector;
  LanguageSet();

  /// If the set contains a single language only, return it.
  std::optional<lldb::LanguageType> GetSingularLanguage();
  void Insert(lldb::LanguageType language);
  bool Empty() const;
  size_t Size() const;
  bool operator[](unsigned i) const;
};

/// CompilerContext allows an array of these items to be passed to perform
/// detailed lookups in SymbolVendor and SymbolFile functions.
struct CompilerContext {
  CompilerContext(CompilerContextKind t, ConstString n) : kind(t), name(n) {}

  bool operator==(const CompilerContext &rhs) const {
    return kind == rhs.kind && name == rhs.name;
  }
  bool operator!=(const CompilerContext &rhs) const { return !(*this == rhs); }

  void Dump(Stream &s) const;

  CompilerContextKind kind;
  ConstString name;
};
llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const CompilerContext &rhs);

/// Match \p context_chain against \p pattern, which may contain "Any"
/// kinds. The \p context_chain should *not* contain any "Any" kinds.
bool contextMatches(llvm::ArrayRef<CompilerContext> context_chain,
                    llvm::ArrayRef<CompilerContext> pattern);

FLAGS_ENUM(TypeQueryOptions){
    e_none = 0u,
    /// If set, TypeQuery::m_context contains an exact context that must match
    /// the full context. If not set, TypeQuery::m_context can contain a partial
    /// type match where the full context isn't fully specified.
    e_exact_match = (1u << 0),
    /// If set, TypeQuery::m_context is a clang module compiler context. If not
    /// set TypeQuery::m_context is normal type lookup context.
    e_module_search = (1u << 1),
    /// When true, the find types call should stop the query as soon as a single
    /// matching type is found. When false, the type query should find all
    /// matching types.
    e_find_one = (1u << 2),
};
LLDB_MARK_AS_BITMASK_ENUM(TypeQueryOptions)

/// A class that contains all state required for type lookups.
///
/// Using a TypeQuery class for matching types simplifies the internal APIs we
/// need to implement type lookups in LLDB. Type lookups can fully specify the
/// exact typename by filling out a complete or partial CompilerContext array.
/// This technique allows for powerful searches and also allows the SymbolFile
/// classes to use the m_context array to lookup types by basename, then
/// eliminate potential matches without having to resolve types into each
/// TypeSystem. This makes type lookups vastly more efficient and allows the
/// SymbolFile objects to stop looking up types when the type matching is
/// complete, like if we are looking for only a single type in our search.
class TypeQuery {
public:
  TypeQuery() = delete;

  /// Construct a type match object using a fully- or partially-qualified name.
  ///
  /// The specified \a type_name will be chopped up and the m_context will be
  /// populated by separating the string by looking for "::". We do this because
  /// symbol files have indexes that contain only the type's basename. This also
  /// allows symbol files to efficiently not realize types that don't match the
  /// specified context. Example of \a type_name values that can be specified
  /// include:
  ///   "foo": Look for any type whose basename matches "foo".
  ///     If \a exact_match is true, then the type can't be contained in any
  ///     declaration context like a namespace, class, or other containing
  ///     scope.
  ///     If \a exact match is false, then we will find all matches including
  ///     ones that are contained in other declaration contexts, including top
  ///     level types.
  ///   "foo::bar": Look for any type whose basename matches "bar" but make sure
  ///     its parent declaration context is any named declaration context
  ///     (namespace, class, struct, etc) whose name matches "foo".
  ///     If \a exact_match is true, then the "foo" declaration context must
  ///     appear at the source file level or inside of a function.
  ///     If \a exact match is false, then the "foo" declaration context can
  ///     be contained in any other declaration contexts.
  ///   "class foo": Only match types that are classes whose basename matches
  ///     "foo".
  ///   "struct foo": Only match types that are structures whose basename
  ///     matches "foo".
  ///   "class foo::bar": Only match types that are classes whose basename
  ///     matches "bar" and that are contained in any named declaration context
  ///     named "foo".
  ///
  /// \param[in] type_name
  ///   A fully- or partially-qualified type name. This name will be parsed and
  ///   broken up and the m_context will be populated with the various parts of
  ///   the name. This typename can be prefixed with "struct ", "class ",
  ///   "union", "enum " or "typedef " before the actual type name to limit the
  ///   results of the types that match. The declaration context can be
  ///   specified with the "::" string. For example, "a::b::my_type".
  ///
  /// \param[in] options A set of boolean enumeration flags from the
  ///   TypeQueryOptions enumerations. \see TypeQueryOptions.
  TypeQuery(llvm::StringRef name, TypeQueryOptions options = e_none);

  /// Construct a type-match object that matches a type basename that exists
  /// in the specified declaration context.
  ///
  /// This allows the m_context to be first populated using a declaration
  /// context to exactly identify the containing declaration context of a type.
  /// This can be used when you have a forward declaration to a type and you
  /// need to search for its complete type.
  ///
  /// \param[in] decl_ctx
  ///   A declaration context object that comes from a TypeSystem plug-in. This
  ///   object will be asked to populate the array of CompilerContext objects
  ///   by adding the top most declaration context first into the array and then
  ///   adding any containing declaration contexts.
  ///
  /// \param[in] type_basename
  ///   The basename of the type to lookup in the specified declaration context.
  ///
  /// \param[in] options A set of boolean enumeration flags from the
  ///   TypeQueryOptions enumerations. \see TypeQueryOptions.
  TypeQuery(const CompilerDeclContext &decl_ctx, ConstString type_basename,
            TypeQueryOptions options = e_none);
  /// Construct a type-match object using a compiler declaration that specifies
  /// a typename and a declaration context to use when doing exact type lookups.
  ///
  /// This allows the m_context to be first populated using a type declaration.
  /// The type declaration might have a declaration context and each TypeSystem
  /// plug-in can populate the declaration context needed to perform an exact
  /// lookup for a type.
  /// This can be used when you have a forward declaration to a type and you
  /// need to search for its complete type.
  ///
  /// \param[in] decl
  ///   A type declaration context object that comes from a TypeSystem plug-in.
  ///   This object will be asked to full the array of CompilerContext objects
  ///   by adding the top most declaration context first into the array and then
  ///   adding any containing declaration contexts, and ending with the exact
  ///   typename and the kind of type it is (class, struct, union, enum, etc).
  ///
  /// \param[in] options A set of boolean enumeration flags from the
  ///   TypeQueryOptions enumerations. \see TypeQueryOptions.
  TypeQuery(const CompilerDecl &decl, TypeQueryOptions options = e_none);

  /// Construct a type-match object using a CompilerContext array.
  ///
  /// Clients can manually create compiler contexts and use these to find
  /// matches when searching for types. There are two types of contexts that
  /// are supported when doing type searchs: type contexts and clang module
  /// contexts. Type contexts have contexts that specify the type and its
  /// containing declaration context like namespaces and classes. Clang module
  /// contexts specify contexts more completely to find exact matches within
  /// clang module debug information. They will include the modules that the
  /// type is included in and any functions that the type might be defined in.
  /// This allows very fine-grained type resolution.
  ///
  /// \param[in] context The compiler context to use when doing the search.
  ///
  /// \param[in] options A set of boolean enumeration flags from the
  ///   TypeQueryOptions enumerations. \see TypeQueryOptions.
  TypeQuery(const llvm::ArrayRef<lldb_private::CompilerContext> &context,
            TypeQueryOptions options = e_none);

  /// Construct a type-match object that duplicates all matching criterea,
  /// but not any searched symbol files or the type map for matches. This allows
  /// the m_context to be modified prior to performing another search.
  TypeQuery(const TypeQuery &rhs) = default;
  /// Assign a type-match object that duplicates all matching criterea,
  /// but not any searched symbol files or the type map for matches. This allows
  /// the m_context to be modified prior to performing another search.
  TypeQuery &operator=(const TypeQuery &rhs) = default;

  /// Check of a CompilerContext array from matching type from a symbol file
  /// matches the \a m_context.
  ///
  /// \param[in] context
  ///   A fully qualified CompilerContext array for a potential match that is
  ///   created by the symbol file prior to trying to actually resolve a type.
  ///
  /// \returns
  ///   True if the context matches, false if it doesn't. If e_exact_match
  ///   is set in m_options, then \a context must exactly match \a m_context. If
  ///   e_exact_match is not set, then the bottom m_context.size() objects in
  ///   \a context must match. This allows SymbolFile objects the fill in a
  ///   potential type basename match from the index into \a context, and see if
  ///   it matches prior to having to resolve a lldb_private::Type object for
  ///   the type from the index. This allows type parsing to be as efficient as
  ///   possible and only realize the types that match the query.
  bool
  ContextMatches(llvm::ArrayRef<lldb_private::CompilerContext> context) const;

  /// Get the type basename to use when searching the type indexes in each
  /// SymbolFile object.
  ///
  /// Debug information indexes often contain indexes that track the basename
  /// of types only, not a fully qualified path. This allows the indexes to be
  /// smaller and allows for efficient lookups.
  ///
  /// \returns
  ///   The type basename to use when doing lookups as a constant string.
  ConstString GetTypeBasename() const;

  /// Returns true if any matching languages have been specified in this type
  /// matching object.
  bool HasLanguage() const { return m_languages.has_value(); }

  /// Add a language family to the list of languages that should produce a
  /// match.
  void AddLanguage(lldb::LanguageType language);

  /// Set the list of languages that should produce a match to only the ones
  /// specified in \ref languages.
  void SetLanguages(LanguageSet languages);

  /// Check if the language matches any languages that have been added to this
  /// match object.
  ///
  /// \returns
  ///   True if no language have been specified, or if some language have been
  ///   added using AddLanguage(...) and they match. False otherwise.
  bool LanguageMatches(lldb::LanguageType language) const;

  bool GetExactMatch() const { return (m_options & e_exact_match) != 0; }
  /// The \a m_context can be used in two ways: normal types searching with
  /// the context containing a stanadard declaration context for a type, or
  /// with the context being more complete for exact matches in clang modules.
  /// Set this to true if you wish to search for a type in clang module.
  bool GetModuleSearch() const { return (m_options & e_module_search) != 0; }

  /// Returns true if the type query is supposed to find only a single matching
  /// type. Returns false if the type query should find all matches.
  bool GetFindOne() const { return (m_options & e_find_one) != 0; }
  void SetFindOne(bool b) {
    if (b)
      m_options |= e_find_one;
    else
      m_options &= (e_exact_match | e_find_one);
  }

  /// Access the internal compiler context array.
  ///
  /// Clients can use this to populate the context manually.
  std::vector<lldb_private::CompilerContext> &GetContextRef() {
    return m_context;
  }

protected:
  /// A full or partial compiler context array where the parent declaration
  /// contexts appear at the top of the array starting at index zero and the
  /// last entry contains the type and name of the type we are looking for.
  std::vector<lldb_private::CompilerContext> m_context;
  /// An options bitmask that contains enabled options for the type query.
  /// \see TypeQueryOptions.
  TypeQueryOptions m_options;
  /// If this variable has a value, then the language family must match at least
  /// one of the specified languages. If this variable has no value, then the
  /// language of the type doesn't need to match any types that are searched.
  std::optional<LanguageSet> m_languages;
};

/// This class tracks the state and results of a \ref TypeQuery.
///
/// Any mutable state required for type lookups and the results are tracked in
/// this object.
class TypeResults {
public:
  /// Construct a type results object
  TypeResults() = default;

  /// When types that match a TypeQuery are found, this API is used to insert
  /// the matching types.
  ///
  /// \return
  ///   True if the type was added, false if the \a type_sp was already in the
  ///   results.
  bool InsertUnique(const lldb::TypeSP &type_sp);

  /// Check if the type matching has found all of the matches that it needs.
  bool Done(const TypeQuery &query) const;

  /// Check if a SymbolFile object has already been searched by this type match
  /// object.
  ///
  /// This function will add \a sym_file to the set of SymbolFile objects if it
  /// isn't already in the set and return \a false. Returns true if \a sym_file
  /// was already in the set and doesn't need to be searched.
  ///
  /// Any clients that search for types should first check that the symbol file
  /// has not already been searched. If this function returns true, the type
  /// search function should early return to avoid duplicating type searchihng
  /// efforts.
  ///
  /// \param[in] sym_file
  ///   A SymbolFile pointer that will be used to track which symbol files have
  ///   already been searched.
  ///
  /// \returns
  ///   True if the symbol file has been search already, false otherwise.
  bool AlreadySearched(lldb_private::SymbolFile *sym_file);

  /// Access the set of searched symbol files.
  llvm::DenseSet<lldb_private::SymbolFile *> &GetSearchedSymbolFiles() {
    return m_searched_symbol_files;
  }

  lldb::TypeSP GetFirstType() const { return m_type_map.FirstType(); }
  TypeMap &GetTypeMap() { return m_type_map; }
  const TypeMap &GetTypeMap() const { return m_type_map; }

private:
  /// Matching types get added to this map as type search continues.
  TypeMap m_type_map;
  /// This set is used to track and make sure we only perform lookups in a
  /// symbol file one time.
  llvm::DenseSet<lldb_private::SymbolFile *> m_searched_symbol_files;
};

class SymbolFileType : public std::enable_shared_from_this<SymbolFileType>,
                       public UserID {
public:
  SymbolFileType(SymbolFile &symbol_file, lldb::user_id_t uid)
      : UserID(uid), m_symbol_file(symbol_file) {}

  SymbolFileType(SymbolFile &symbol_file, const lldb::TypeSP &type_sp);

  ~SymbolFileType() = default;

  Type *operator->() { return GetType(); }

  Type *GetType();
  SymbolFile &GetSymbolFile() const { return m_symbol_file; }

protected:
  SymbolFile &m_symbol_file;
  lldb::TypeSP m_type_sp;
};

class Type : public std::enable_shared_from_this<Type>, public UserID {
public:
  enum EncodingDataType {
    /// Invalid encoding.
    eEncodingInvalid,
    /// This type is the type whose UID is m_encoding_uid.
    eEncodingIsUID,
    /// This type is the type whose UID is m_encoding_uid with the const
    /// qualifier added.
    eEncodingIsConstUID,
    /// This type is the type whose UID is m_encoding_uid with the restrict
    /// qualifier added.
    eEncodingIsRestrictUID,
    /// This type is the type whose UID is m_encoding_uid with the volatile
    /// qualifier added.
    eEncodingIsVolatileUID,
    /// This type is alias to a type whose UID is m_encoding_uid.
    eEncodingIsTypedefUID,
    /// This type is pointer to a type whose UID is m_encoding_uid.
    eEncodingIsPointerUID,
    /// This type is L value reference to a type whose UID is m_encoding_uid.
    eEncodingIsLValueReferenceUID,
    /// This type is R value reference to a type whose UID is m_encoding_uid.
    eEncodingIsRValueReferenceUID,
    /// This type is the type whose UID is m_encoding_uid as an atomic type.
    eEncodingIsAtomicUID,
    /// This type is the synthetic type whose UID is m_encoding_uid.
    eEncodingIsSyntheticUID,
    /// This type is a signed pointer.
    eEncodingIsLLVMPtrAuthUID
  };

  enum class ResolveState : unsigned char {
    Unresolved = 0,
    Forward = 1,
    Layout = 2,
    Full = 3
  };

  void Dump(Stream *s, bool show_context,
            lldb::DescriptionLevel level = lldb::eDescriptionLevelFull);

  void DumpTypeName(Stream *s);

  /// Since Type instances only keep a "SymbolFile *" internally, other classes
  /// like TypeImpl need make sure the module is still around before playing
  /// with
  /// Type instances. They can store a weak pointer to the Module;
  lldb::ModuleSP GetModule();

  /// GetModule may return module for compile unit's object file.
  /// GetExeModule returns module for executable object file that contains
  /// compile unit where type was actually defined.
  /// GetModule and GetExeModule may return the same value.
  lldb::ModuleSP GetExeModule();

  void GetDescription(Stream *s, lldb::DescriptionLevel level, bool show_name,
                      ExecutionContextScope *exe_scope);

  SymbolFile *GetSymbolFile() { return m_symbol_file; }
  const SymbolFile *GetSymbolFile() const { return m_symbol_file; }

  ConstString GetName();

  ConstString GetBaseName();

  std::optional<uint64_t> GetByteSize(ExecutionContextScope *exe_scope);

  llvm::Expected<uint32_t> GetNumChildren(bool omit_empty_base_classes);

  bool IsAggregateType();

  // Returns if the type is a templated decl. Does not look through typedefs.
  bool IsTemplateType();

  bool IsValidType() { return m_encoding_uid_type != eEncodingInvalid; }

  bool IsTypedef() { return m_encoding_uid_type == eEncodingIsTypedefUID; }

  lldb::TypeSP GetTypedefType();

  ConstString GetName() const { return m_name; }

  ConstString GetQualifiedName();

  bool ReadFromMemory(ExecutionContext *exe_ctx, lldb::addr_t address,
                      AddressType address_type, DataExtractor &data);

  bool WriteToMemory(ExecutionContext *exe_ctx, lldb::addr_t address,
                     AddressType address_type, DataExtractor &data);

  lldb::Format GetFormat();

  lldb::Encoding GetEncoding(uint64_t &count);

  SymbolContextScope *GetSymbolContextScope() { return m_context; }
  const SymbolContextScope *GetSymbolContextScope() const { return m_context; }
  void SetSymbolContextScope(SymbolContextScope *context) {
    m_context = context;
  }

  const lldb_private::Declaration &GetDeclaration() const;

  // Get the clang type, and resolve definitions for any
  // class/struct/union/enum types completely.
  CompilerType GetFullCompilerType();

  // Get the clang type, and resolve definitions enough so that the type could
  // have layout performed. This allows ptrs and refs to
  // class/struct/union/enum types remain forward declarations.
  CompilerType GetLayoutCompilerType();

  // Get the clang type and leave class/struct/union/enum types as forward
  // declarations if they haven't already been fully defined.
  CompilerType GetForwardCompilerType();

  static int Compare(const Type &a, const Type &b);

  // Represents a parsed type name coming out of GetTypeScopeAndBasename. The
  // structure holds StringRefs pointing to portions of the original name, and
  // so must not be used after the name is destroyed.
  struct ParsedName {
    lldb::TypeClass type_class = lldb::eTypeClassAny;

    // Scopes of the type, starting with the outermost. Absolute type references
    // have a "::" as the first scope.
    llvm::SmallVector<llvm::StringRef> scope;

    llvm::StringRef basename;

    friend bool operator==(const ParsedName &lhs, const ParsedName &rhs) {
      return lhs.type_class == rhs.type_class && lhs.scope == rhs.scope &&
             lhs.basename == rhs.basename;
    }

    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const ParsedName &name) {
      return os << llvm::formatv(
                 "Type::ParsedName({0:x}, [{1}], {2})",
                 llvm::to_underlying(name.type_class),
                 llvm::make_range(name.scope.begin(), name.scope.end()),
                 name.basename);
    }
  };
  // From a fully qualified typename, split the type into the type basename and
  // the remaining type scope (namespaces/classes).
  static std::optional<ParsedName>
  GetTypeScopeAndBasename(llvm::StringRef name);

  void SetEncodingType(Type *encoding_type) { m_encoding_type = encoding_type; }

  uint32_t GetEncodingMask();

  typedef uint32_t Payload;
  /// Return the language-specific payload.
  Payload GetPayload() { return m_payload; }
  /// Return the language-specific payload.
  void SetPayload(Payload opaque_payload) { m_payload = opaque_payload; }

protected:
  ConstString m_name;
  SymbolFile *m_symbol_file = nullptr;
  /// The symbol context in which this type is defined.
  SymbolContextScope *m_context = nullptr;
  Type *m_encoding_type = nullptr;
  lldb::user_id_t m_encoding_uid = LLDB_INVALID_UID;
  EncodingDataType m_encoding_uid_type = eEncodingInvalid;
  uint64_t m_byte_size : 63;
  uint64_t m_byte_size_has_value : 1;
  Declaration m_decl;
  CompilerType m_compiler_type;
  ResolveState m_compiler_type_resolve_state = ResolveState::Unresolved;
  /// Language-specific flags.
  Payload m_payload;

  Type *GetEncodingType();

  bool ResolveCompilerType(ResolveState compiler_type_resolve_state);
private:
  /// Only allow Symbol File to create types, as they should own them by keeping
  /// them in their TypeList. \see SymbolFileCommon::MakeType() reference in the
  /// header documentation here so users will know what function to use if the
  /// get a compile error.
  friend class lldb_private::SymbolFileCommon;

  Type(lldb::user_id_t uid, SymbolFile *symbol_file, ConstString name,
       std::optional<uint64_t> byte_size, SymbolContextScope *context,
       lldb::user_id_t encoding_uid, EncodingDataType encoding_uid_type,
       const Declaration &decl, const CompilerType &compiler_qual_type,
       ResolveState compiler_type_resolve_state, uint32_t opaque_payload = 0);

  // This makes an invalid type.  Used for functions that return a Type when
  // they get an error.
  Type();

  Type(Type &t) = default;

  Type(Type &&t) = default;

  Type &operator=(const Type &t) = default;

  Type &operator=(Type &&t) = default;
};

// the two classes here are used by the public API as a backend to the SBType
// and SBTypeList classes

class TypeImpl {
public:
  TypeImpl() = default;

  ~TypeImpl() = default;

  TypeImpl(const lldb::TypeSP &type_sp);

  TypeImpl(const CompilerType &compiler_type);

  TypeImpl(const lldb::TypeSP &type_sp, const CompilerType &dynamic);

  TypeImpl(const CompilerType &compiler_type, const CompilerType &dynamic);

  void SetType(const lldb::TypeSP &type_sp);

  void SetType(const CompilerType &compiler_type);

  void SetType(const lldb::TypeSP &type_sp, const CompilerType &dynamic);

  void SetType(const CompilerType &compiler_type, const CompilerType &dynamic);

  bool operator==(const TypeImpl &rhs) const;

  bool operator!=(const TypeImpl &rhs) const;

  bool IsValid() const;

  explicit operator bool() const;

  void Clear();

  lldb::ModuleSP GetModule() const;

  ConstString GetName() const;

  ConstString GetDisplayTypeName() const;

  TypeImpl GetPointerType() const;

  TypeImpl GetPointeeType() const;

  TypeImpl GetReferenceType() const;

  TypeImpl GetTypedefedType() const;

  TypeImpl GetDereferencedType() const;

  TypeImpl GetUnqualifiedType() const;

  TypeImpl GetCanonicalType() const;

  CompilerType GetCompilerType(bool prefer_dynamic);

  CompilerType::TypeSystemSPWrapper GetTypeSystem(bool prefer_dynamic);

  bool GetDescription(lldb_private::Stream &strm,
                      lldb::DescriptionLevel description_level);

  CompilerType FindDirectNestedType(llvm::StringRef name);

private:
  bool CheckModule(lldb::ModuleSP &module_sp) const;
  bool CheckExeModule(lldb::ModuleSP &module_sp) const;
  bool CheckModuleCommon(const lldb::ModuleWP &input_module_wp,
                         lldb::ModuleSP &module_sp) const;

  lldb::ModuleWP m_module_wp;
  lldb::ModuleWP m_exe_module_wp;
  CompilerType m_static_type;
  CompilerType m_dynamic_type;
};

class TypeListImpl {
public:
  TypeListImpl() = default;

  void Append(const lldb::TypeImplSP &type) { m_content.push_back(type); }

  class AppendVisitor {
  public:
    AppendVisitor(TypeListImpl &type_list) : m_type_list(type_list) {}

    void operator()(const lldb::TypeImplSP &type) { m_type_list.Append(type); }

  private:
    TypeListImpl &m_type_list;
  };

  void Append(const lldb_private::TypeList &type_list);

  lldb::TypeImplSP GetTypeAtIndex(size_t idx) {
    lldb::TypeImplSP type_sp;
    if (idx < GetSize())
      type_sp = m_content[idx];
    return type_sp;
  }

  size_t GetSize() { return m_content.size(); }

private:
  std::vector<lldb::TypeImplSP> m_content;
};

class TypeMemberImpl {
public:
  TypeMemberImpl() = default;

  TypeMemberImpl(const lldb::TypeImplSP &type_impl_sp, uint64_t bit_offset,
                 ConstString name, uint32_t bitfield_bit_size = 0,
                 bool is_bitfield = false)
      : m_type_impl_sp(type_impl_sp), m_bit_offset(bit_offset), m_name(name),
        m_bitfield_bit_size(bitfield_bit_size), m_is_bitfield(is_bitfield) {}

  TypeMemberImpl(const lldb::TypeImplSP &type_impl_sp, uint64_t bit_offset)
      : m_type_impl_sp(type_impl_sp), m_bit_offset(bit_offset),
        m_bitfield_bit_size(0), m_is_bitfield(false) {
    if (m_type_impl_sp)
      m_name = m_type_impl_sp->GetName();
  }

  const lldb::TypeImplSP &GetTypeImpl() { return m_type_impl_sp; }

  ConstString GetName() const { return m_name; }

  uint64_t GetBitOffset() const { return m_bit_offset; }

  uint32_t GetBitfieldBitSize() const { return m_bitfield_bit_size; }

  void SetBitfieldBitSize(uint32_t bitfield_bit_size) {
    m_bitfield_bit_size = bitfield_bit_size;
  }

  bool GetIsBitfield() const { return m_is_bitfield; }

  void SetIsBitfield(bool is_bitfield) { m_is_bitfield = is_bitfield; }

protected:
  lldb::TypeImplSP m_type_impl_sp;
  uint64_t m_bit_offset = 0;
  ConstString m_name;
  uint32_t m_bitfield_bit_size = 0; // Bit size for bitfield members only
  bool m_is_bitfield = false;
};

///
/// Sometimes you can find the name of the type corresponding to an object, but
/// we don't have debug
/// information for it.  If that is the case, you can return one of these
/// objects, and then if it
/// has a full type, you can use that, but if not at least you can print the
/// name for informational
/// purposes.
///

class TypeAndOrName {
public:
  TypeAndOrName() = default;
  TypeAndOrName(lldb::TypeSP &type_sp);
  TypeAndOrName(const CompilerType &compiler_type);
  TypeAndOrName(const char *type_str);
  TypeAndOrName(ConstString &type_const_string);

  bool operator==(const TypeAndOrName &other) const;

  bool operator!=(const TypeAndOrName &other) const;

  ConstString GetName() const;

  CompilerType GetCompilerType() const { return m_compiler_type; }

  void SetName(ConstString type_name);

  void SetName(const char *type_name_cstr);

  void SetName(llvm::StringRef name);

  void SetTypeSP(lldb::TypeSP type_sp);

  void SetCompilerType(CompilerType compiler_type);

  bool IsEmpty() const;

  bool HasName() const;

  bool HasCompilerType() const;

  bool HasType() const { return HasCompilerType(); }

  void Clear();

  explicit operator bool() { return !IsEmpty(); }

private:
  CompilerType m_compiler_type;
  ConstString m_type_name;
};

class TypeMemberFunctionImpl {
public:
  TypeMemberFunctionImpl() = default;

  TypeMemberFunctionImpl(const CompilerType &type, const CompilerDecl &decl,
                         const std::string &name,
                         const lldb::MemberFunctionKind &kind)
      : m_type(type), m_decl(decl), m_name(name), m_kind(kind) {}

  bool IsValid();

  ConstString GetName() const;

  ConstString GetMangledName() const;

  CompilerType GetType() const;

  CompilerType GetReturnType() const;

  size_t GetNumArguments() const;

  CompilerType GetArgumentAtIndex(size_t idx) const;

  lldb::MemberFunctionKind GetKind() const;

  bool GetDescription(Stream &stream);

protected:
  std::string GetPrintableTypeName();

private:
  CompilerType m_type;
  CompilerDecl m_decl;
  ConstString m_name;
  lldb::MemberFunctionKind m_kind = lldb::eMemberFunctionKindUnknown;
};

class TypeEnumMemberImpl {
public:
  TypeEnumMemberImpl() : m_name("<invalid>") {}

  TypeEnumMemberImpl(const lldb::TypeImplSP &integer_type_sp, ConstString name,
                     const llvm::APSInt &value);

  TypeEnumMemberImpl(const TypeEnumMemberImpl &rhs) = default;

  TypeEnumMemberImpl &operator=(const TypeEnumMemberImpl &rhs);

  bool IsValid() { return m_valid; }

  ConstString GetName() const { return m_name; }

  const lldb::TypeImplSP &GetIntegerType() const { return m_integer_type_sp; }

  uint64_t GetValueAsUnsigned() const { return m_value.getZExtValue(); }

  int64_t GetValueAsSigned() const { return m_value.getSExtValue(); }

protected:
  lldb::TypeImplSP m_integer_type_sp;
  ConstString m_name;
  llvm::APSInt m_value;
  bool m_valid = false;
};

class TypeEnumMemberListImpl {
public:
  TypeEnumMemberListImpl() = default;

  void Append(const lldb::TypeEnumMemberImplSP &type) {
    m_content.push_back(type);
  }

  void Append(const lldb_private::TypeEnumMemberListImpl &type_list);

  lldb::TypeEnumMemberImplSP GetTypeEnumMemberAtIndex(size_t idx) {
    lldb::TypeEnumMemberImplSP enum_member;
    if (idx < GetSize())
      enum_member = m_content[idx];
    return enum_member;
  }

  size_t GetSize() { return m_content.size(); }

private:
  std::vector<lldb::TypeEnumMemberImplSP> m_content;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_TYPE_H
