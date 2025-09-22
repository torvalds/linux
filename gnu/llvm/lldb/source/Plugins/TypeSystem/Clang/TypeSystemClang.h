//===-- TypeSystemClang.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLANG_TYPESYSTEMCLANG_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLANG_TYPESYSTEMCLANG_H

#include <cstdint>

#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTFwd.h"
#include "clang/AST/Decl.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"

#include "Plugins/ExpressionParser/Clang/ClangPersistentVariables.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Flags.h"
#include "lldb/Utility/Log.h"
#include "lldb/lldb-enumerations.h"

class DWARFASTParserClang;
class PDBASTParser;

namespace clang {
class FileManager;
class HeaderSearch;
class ModuleMap;
} // namespace clang

namespace lldb_private {

class ClangASTMetadata;
class ClangASTSource;
class Declaration;

/// A Clang module ID.
class OptionalClangModuleID {
  unsigned m_id = 0;

public:
  OptionalClangModuleID() = default;
  explicit OptionalClangModuleID(unsigned id) : m_id(id) {}
  bool HasValue() const { return m_id != 0; }
  unsigned GetValue() const { return m_id; }
};

/// The implementation of lldb::Type's m_payload field for TypeSystemClang.
class TypePayloadClang {
  /// The payload is used for typedefs and ptrauth types.
  /// For typedefs, the Layout is as follows:
  /// \verbatim
  /// bit 0..30 ... Owning Module ID.
  /// bit 31 ...... IsCompleteObjCClass.
  /// \endverbatim
  /// For ptrauth types, we store the PointerAuthQualifier as an opaque value.
  Type::Payload m_payload = 0;

public:
  TypePayloadClang() = default;
  explicit TypePayloadClang(OptionalClangModuleID owning_module,
                            bool is_complete_objc_class = false);
  explicit TypePayloadClang(uint32_t opaque_payload) : m_payload(opaque_payload) {}
  operator Type::Payload() { return m_payload; }

  static constexpr unsigned ObjCClassBit = 1 << 31;
  bool IsCompleteObjCClass() { return Flags(m_payload).Test(ObjCClassBit); }
  void SetIsCompleteObjCClass(bool is_complete_objc_class) {
    m_payload = is_complete_objc_class ? Flags(m_payload).Set(ObjCClassBit)
                                       : Flags(m_payload).Clear(ObjCClassBit);
  }
  OptionalClangModuleID GetOwningModule() {
    return OptionalClangModuleID(Flags(m_payload).Clear(ObjCClassBit));
  }
  void SetOwningModule(OptionalClangModuleID id);
  /// \}
};

/// A TypeSystem implementation based on Clang.
///
/// This class uses a single clang::ASTContext as the backend for storing
/// its types and declarations. Every clang::ASTContext should also just have
/// a single associated TypeSystemClang instance that manages it.
///
/// The clang::ASTContext instance can either be created by TypeSystemClang
/// itself or it can adopt an existing clang::ASTContext (for example, when
/// it is necessary to provide a TypeSystem interface for an existing
/// clang::ASTContext that was created by clang::CompilerInstance).
class TypeSystemClang : public TypeSystem {
  // LLVM RTTI support
  static char ID;

public:
  typedef void (*CompleteTagDeclCallback)(void *baton, clang::TagDecl *);
  typedef void (*CompleteObjCInterfaceDeclCallback)(void *baton,
                                                    clang::ObjCInterfaceDecl *);

  // llvm casting support
  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

  /// Constructs a TypeSystemClang with an ASTContext using the given triple.
  ///
  /// \param name The name for the TypeSystemClang (for logging purposes)
  /// \param triple The llvm::Triple used for the ASTContext. The triple defines
  ///               certain characteristics of the ASTContext and its types
  ///               (e.g., whether certain primitive types exist or what their
  ///               signedness is).
  explicit TypeSystemClang(llvm::StringRef name, llvm::Triple triple);

  /// Constructs a TypeSystemClang that uses an existing ASTContext internally.
  /// Useful when having an existing ASTContext created by Clang.
  ///
  /// \param name The name for the TypeSystemClang (for logging purposes)
  /// \param existing_ctxt An existing ASTContext.
  explicit TypeSystemClang(llvm::StringRef name,
                           clang::ASTContext &existing_ctxt);

  ~TypeSystemClang() override;

  void Finalize() override;

  // PluginInterface functions
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  static llvm::StringRef GetPluginNameStatic() { return "clang"; }

  static lldb::TypeSystemSP CreateInstance(lldb::LanguageType language,
                                           Module *module, Target *target);

  static LanguageSet GetSupportedLanguagesForTypes();
  static LanguageSet GetSupportedLanguagesForExpressions();

  static void Initialize();

  static void Terminate();

  static TypeSystemClang *GetASTContext(clang::ASTContext *ast_ctx);

  /// Returns the display name of this TypeSystemClang that indicates what
  /// purpose it serves in LLDB. Used for example in logs.
  llvm::StringRef getDisplayName() const { return m_display_name; }

  /// Returns the clang::ASTContext instance managed by this TypeSystemClang.
  clang::ASTContext &getASTContext() const;

  clang::MangleContext *getMangleContext();

  std::shared_ptr<clang::TargetOptions> &getTargetOptions();

  clang::TargetInfo *getTargetInfo();

  void setSema(clang::Sema *s);
  clang::Sema *getSema() { return m_sema; }

  const char *GetTargetTriple();

  void SetExternalSource(
      llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> &ast_source_up);

  bool GetCompleteDecl(clang::Decl *decl) {
    return TypeSystemClang::GetCompleteDecl(&getASTContext(), decl);
  }

  static void DumpDeclHiearchy(clang::Decl *decl);

  static void DumpDeclContextHiearchy(clang::DeclContext *decl_ctx);

  static bool GetCompleteDecl(clang::ASTContext *ast, clang::Decl *decl);

  void SetMetadataAsUserID(const clang::Decl *decl, lldb::user_id_t user_id);
  void SetMetadataAsUserID(const clang::Type *type, lldb::user_id_t user_id);

  void SetMetadata(const clang::Decl *object, ClangASTMetadata &meta_data);

  void SetMetadata(const clang::Type *object, ClangASTMetadata &meta_data);
  ClangASTMetadata *GetMetadata(const clang::Decl *object);
  ClangASTMetadata *GetMetadata(const clang::Type *object);

  void SetCXXRecordDeclAccess(const clang::CXXRecordDecl *object,
                              clang::AccessSpecifier access);
  clang::AccessSpecifier
  GetCXXRecordDeclAccess(const clang::CXXRecordDecl *object);

  // Basic Types
  CompilerType GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                                   size_t bit_size) override;

  CompilerType GetBasicType(lldb::BasicType type);

  static lldb::BasicType GetBasicTypeEnumeration(llvm::StringRef name);

  CompilerType
  GetBuiltinTypeForDWARFEncodingAndBitSize(llvm::StringRef type_name,
                                           uint32_t dw_ate, uint32_t bit_size);

  CompilerType GetCStringType(bool is_const);

  static clang::DeclContext *GetDeclContextForType(clang::QualType type);

  static clang::DeclContext *GetDeclContextForType(const CompilerType &type);

  CompilerDeclContext
  GetCompilerDeclContextForType(const CompilerType &type) override;

  uint32_t GetPointerByteSize() override;

  clang::TranslationUnitDecl *GetTranslationUnitDecl() {
    return getASTContext().getTranslationUnitDecl();
  }

  static bool AreTypesSame(CompilerType type1, CompilerType type2,
                           bool ignore_qualifiers = false);

  /// Creates a CompilerType from the given QualType with the current
  /// TypeSystemClang instance as the CompilerType's typesystem.
  /// \param qt The QualType for a type that belongs to the ASTContext of this
  ///           TypeSystemClang.
  /// \return The CompilerType representing the given QualType. If the
  ///         QualType's type pointer is a nullptr then the function returns an
  ///         invalid CompilerType.
  CompilerType GetType(clang::QualType qt) {
    if (qt.getTypePtrOrNull() == nullptr)
      return CompilerType();
    // Check that the type actually belongs to this TypeSystemClang.
    assert(qt->getAsTagDecl() == nullptr ||
           &qt->getAsTagDecl()->getASTContext() == &getASTContext());
    return CompilerType(weak_from_this(), qt.getAsOpaquePtr());
  }

  CompilerType GetTypeForDecl(clang::NamedDecl *decl);

  CompilerType GetTypeForDecl(clang::TagDecl *decl);

  CompilerType GetTypeForDecl(clang::ObjCInterfaceDecl *objc_decl);

  CompilerType GetTypeForDecl(clang::ValueDecl *value_decl);

  template <typename RecordDeclType>
  CompilerType
  GetTypeForIdentifier(llvm::StringRef type_name,
                       clang::DeclContext *decl_context = nullptr) {
    CompilerType compiler_type;
    if (type_name.empty())
      return compiler_type;

    clang::ASTContext &ast = getASTContext();
    if (!decl_context)
      decl_context = ast.getTranslationUnitDecl();

    clang::IdentifierInfo &myIdent = ast.Idents.get(type_name);
    clang::DeclarationName myName =
        ast.DeclarationNames.getIdentifier(&myIdent);
    clang::DeclContext::lookup_result result = decl_context->lookup(myName);
    if (result.empty())
      return compiler_type;

    clang::NamedDecl *named_decl = *result.begin();
    if (const RecordDeclType *record_decl =
            llvm::dyn_cast<RecordDeclType>(named_decl))
      compiler_type = CompilerType(
          weak_from_this(),
          clang::QualType(record_decl->getTypeForDecl(), 0).getAsOpaquePtr());

    return compiler_type;
  }

  CompilerType CreateStructForIdentifier(
      llvm::StringRef type_name,
      const std::initializer_list<std::pair<const char *, CompilerType>>
          &type_fields,
      bool packed = false);

  CompilerType GetOrCreateStructForIdentifier(
      llvm::StringRef type_name,
      const std::initializer_list<std::pair<const char *, CompilerType>>
          &type_fields,
      bool packed = false);

  static bool IsOperator(llvm::StringRef name,
                         clang::OverloadedOperatorKind &op_kind);

  // Structure, Unions, Classes

  static clang::AccessSpecifier
  ConvertAccessTypeToAccessSpecifier(lldb::AccessType access);

  static clang::AccessSpecifier
  UnifyAccessSpecifiers(clang::AccessSpecifier lhs, clang::AccessSpecifier rhs);

  uint32_t GetNumBaseClasses(const clang::CXXRecordDecl *cxx_record_decl,
                             bool omit_empty_base_classes);

  uint32_t GetIndexForRecordChild(const clang::RecordDecl *record_decl,
                                  clang::NamedDecl *canonical_decl,
                                  bool omit_empty_base_classes);

  uint32_t GetIndexForRecordBase(const clang::RecordDecl *record_decl,
                                 const clang::CXXBaseSpecifier *base_spec,
                                 bool omit_empty_base_classes);

  /// Synthesize a clang::Module and return its ID or a default-constructed ID.
  OptionalClangModuleID GetOrCreateClangModule(llvm::StringRef name,
                                               OptionalClangModuleID parent,
                                               bool is_framework = false,
                                               bool is_explicit = false);

  CompilerType CreateRecordType(clang::DeclContext *decl_ctx,
                                OptionalClangModuleID owning_module,
                                lldb::AccessType access_type,
                                llvm::StringRef name, int kind,
                                lldb::LanguageType language,
                                ClangASTMetadata *metadata = nullptr,
                                bool exports_symbols = false);

  class TemplateParameterInfos {
  public:
    TemplateParameterInfos() = default;
    TemplateParameterInfos(llvm::ArrayRef<const char *> names_in,
                           llvm::ArrayRef<clang::TemplateArgument> args_in)
        : names(names_in), args(args_in) {
      assert(names.size() == args_in.size());
    }

    TemplateParameterInfos(TemplateParameterInfos const &) = delete;
    TemplateParameterInfos(TemplateParameterInfos &&) = delete;

    TemplateParameterInfos &operator=(TemplateParameterInfos const &) = delete;
    TemplateParameterInfos &operator=(TemplateParameterInfos &&) = delete;

    ~TemplateParameterInfos() = default;

    bool IsValid() const {
      // Having a pack name but no packed args doesn't make sense, so mark
      // these template parameters as invalid.
      if (pack_name && !packed_args)
        return false;
      return args.size() == names.size() &&
             (!packed_args || !packed_args->packed_args);
    }

    bool IsEmpty() const { return args.empty(); }
    size_t Size() const { return args.size(); }

    llvm::ArrayRef<clang::TemplateArgument> GetArgs() const { return args; }
    llvm::ArrayRef<const char *> GetNames() const { return names; }

    clang::TemplateArgument const &Front() const {
      assert(!args.empty());
      return args.front();
    }

    void InsertArg(char const *name, clang::TemplateArgument arg) {
      args.emplace_back(std::move(arg));
      names.push_back(name);
    }

    // Parameter pack related

    bool hasParameterPack() const { return static_cast<bool>(packed_args); }

    TemplateParameterInfos const &GetParameterPack() const {
      assert(packed_args != nullptr);
      return *packed_args;
    }

    TemplateParameterInfos &GetParameterPack() {
      assert(packed_args != nullptr);
      return *packed_args;
    }

    llvm::ArrayRef<clang::TemplateArgument> GetParameterPackArgs() const {
      assert(packed_args != nullptr);
      return packed_args->GetArgs();
    }

    bool HasPackName() const { return pack_name && pack_name[0]; }

    llvm::StringRef GetPackName() const {
      assert(HasPackName());
      return pack_name;
    }

    void SetPackName(char const *name) { pack_name = name; }

    void SetParameterPack(std::unique_ptr<TemplateParameterInfos> args) {
      packed_args = std::move(args);
    }

  private:
    /// Element 'names[i]' holds the template argument name
    /// of 'args[i]'
    llvm::SmallVector<const char *, 2> names;
    llvm::SmallVector<clang::TemplateArgument, 2> args;

    const char * pack_name = nullptr;
    std::unique_ptr<TemplateParameterInfos> packed_args;
  };

  clang::FunctionTemplateDecl *CreateFunctionTemplateDecl(
      clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
      clang::FunctionDecl *func_decl, const TemplateParameterInfos &infos);

  void CreateFunctionTemplateSpecializationInfo(
      clang::FunctionDecl *func_decl, clang::FunctionTemplateDecl *Template,
      const TemplateParameterInfos &infos);

  clang::ClassTemplateDecl *CreateClassTemplateDecl(
      clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
      lldb::AccessType access_type, llvm::StringRef class_name, int kind,
      const TemplateParameterInfos &infos);

  clang::TemplateTemplateParmDecl *
  CreateTemplateTemplateParmDecl(const char *template_name);

  clang::ClassTemplateSpecializationDecl *CreateClassTemplateSpecializationDecl(
      clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
      clang::ClassTemplateDecl *class_template_decl, int kind,
      const TemplateParameterInfos &infos);

  CompilerType
  CreateClassTemplateSpecializationType(clang::ClassTemplateSpecializationDecl *
                                            class_template_specialization_decl);

  static clang::DeclContext *
  GetAsDeclContext(clang::FunctionDecl *function_decl);

  static bool CheckOverloadedOperatorKindParameterCount(
      bool is_method, clang::OverloadedOperatorKind op_kind,
      uint32_t num_params);

  bool FieldIsBitfield(clang::FieldDecl *field, uint32_t &bitfield_bit_size);

  bool RecordHasFields(const clang::RecordDecl *record_decl);

  bool BaseSpecifierIsEmpty(const clang::CXXBaseSpecifier *b);

  CompilerType CreateObjCClass(llvm::StringRef name,
                               clang::DeclContext *decl_ctx,
                               OptionalClangModuleID owning_module,
                               bool isForwardDecl, bool isInternal,
                               ClangASTMetadata *metadata = nullptr);

  // Returns a mask containing bits from the TypeSystemClang::eTypeXXX
  // enumerations

  // Namespace Declarations

  clang::NamespaceDecl *
  GetUniqueNamespaceDeclaration(const char *name, clang::DeclContext *decl_ctx,
                                OptionalClangModuleID owning_module,
                                bool is_inline = false);

  // Function Types

  clang::FunctionDecl *CreateFunctionDeclaration(
      clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
      llvm::StringRef name, const CompilerType &function_Type,
      clang::StorageClass storage, bool is_inline);

  CompilerType
  CreateFunctionType(const CompilerType &result_type, const CompilerType *args,
                     unsigned num_args, bool is_variadic, unsigned type_quals,
                     clang::CallingConv cc = clang::CC_C,
                     clang::RefQualifierKind ref_qual = clang::RQ_None);

  clang::ParmVarDecl *
  CreateParameterDeclaration(clang::DeclContext *decl_ctx,
                             OptionalClangModuleID owning_module,
                             const char *name, const CompilerType &param_type,
                             int storage, bool add_decl = false);

  void SetFunctionParameters(clang::FunctionDecl *function_decl,
                             llvm::ArrayRef<clang::ParmVarDecl *> params);

  CompilerType CreateBlockPointerType(const CompilerType &function_type);

  // Array Types

  CompilerType CreateArrayType(const CompilerType &element_type,
                               size_t element_count, bool is_vector);

  // Enumeration Types
  CompilerType CreateEnumerationType(llvm::StringRef name,
                                     clang::DeclContext *decl_ctx,
                                     OptionalClangModuleID owning_module,
                                     const Declaration &decl,
                                     const CompilerType &integer_qual_type,
                                     bool is_scoped);

  // Integer type functions

  CompilerType GetIntTypeFromBitSize(size_t bit_size, bool is_signed);

  CompilerType GetPointerSizedIntType(bool is_signed);

  // Floating point functions

  static CompilerType GetFloatTypeFromBitSize(clang::ASTContext *ast,
                                              size_t bit_size);

  // TypeSystem methods
  plugin::dwarf::DWARFASTParser *GetDWARFParser() override;
  PDBASTParser *GetPDBParser() override;
  npdb::PdbAstBuilder *GetNativePDBParser() override;

  // TypeSystemClang callbacks for external source lookups.
  void CompleteTagDecl(clang::TagDecl *);

  void CompleteObjCInterfaceDecl(clang::ObjCInterfaceDecl *);

  bool LayoutRecordType(
      const clang::RecordDecl *record_decl, uint64_t &size, uint64_t &alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &base_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &vbase_offsets);

  /// Creates a CompilerDecl from the given Decl with the current
  /// TypeSystemClang instance as its typesystem.
  /// The Decl has to come from the ASTContext of this
  /// TypeSystemClang.
  CompilerDecl GetCompilerDecl(clang::Decl *decl) {
    assert(&decl->getASTContext() == &getASTContext() &&
           "CreateCompilerDecl for Decl from wrong ASTContext?");
    return CompilerDecl(this, decl);
  }

  // CompilerDecl override functions
  ConstString DeclGetName(void *opaque_decl) override;

  ConstString DeclGetMangledName(void *opaque_decl) override;

  CompilerDeclContext DeclGetDeclContext(void *opaque_decl) override;

  CompilerType DeclGetFunctionReturnType(void *opaque_decl) override;

  size_t DeclGetFunctionNumArguments(void *opaque_decl) override;

  CompilerType DeclGetFunctionArgumentType(void *opaque_decl,
                                           size_t arg_idx) override;

  std::vector<lldb_private::CompilerContext>
  DeclGetCompilerContext(void *opaque_decl) override;

  Scalar DeclGetConstantValue(void *opaque_decl) override;

  CompilerType GetTypeForDecl(void *opaque_decl) override;

  // CompilerDeclContext override functions

  /// Creates a CompilerDeclContext from the given DeclContext
  /// with the current TypeSystemClang instance as its typesystem.
  /// The DeclContext has to come from the ASTContext of this
  /// TypeSystemClang.
  CompilerDeclContext CreateDeclContext(clang::DeclContext *ctx);

  /// Set the owning module for \p decl.
  static void SetOwningModule(clang::Decl *decl,
                              OptionalClangModuleID owning_module);

  std::vector<CompilerDecl>
  DeclContextFindDeclByName(void *opaque_decl_ctx, ConstString name,
                            const bool ignore_using_decls) override;

  ConstString DeclContextGetName(void *opaque_decl_ctx) override;

  ConstString DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) override;

  bool DeclContextIsClassMethod(void *opaque_decl_ctx) override;

  bool DeclContextIsContainedInLookup(void *opaque_decl_ctx,
                                      void *other_opaque_decl_ctx) override;

  lldb::LanguageType DeclContextGetLanguage(void *opaque_decl_ctx) override;

  std::vector<lldb_private::CompilerContext>
  DeclContextGetCompilerContext(void *opaque_decl_ctx) override;

  // Clang specific clang::DeclContext functions

  static clang::DeclContext *
  DeclContextGetAsDeclContext(const CompilerDeclContext &dc);

  static clang::ObjCMethodDecl *
  DeclContextGetAsObjCMethodDecl(const CompilerDeclContext &dc);

  static clang::CXXMethodDecl *
  DeclContextGetAsCXXMethodDecl(const CompilerDeclContext &dc);

  static clang::FunctionDecl *
  DeclContextGetAsFunctionDecl(const CompilerDeclContext &dc);

  static clang::NamespaceDecl *
  DeclContextGetAsNamespaceDecl(const CompilerDeclContext &dc);

  static ClangASTMetadata *DeclContextGetMetaData(const CompilerDeclContext &dc,
                                                  const clang::Decl *object);

  static clang::ASTContext *
  DeclContextGetTypeSystemClang(const CompilerDeclContext &dc);

  // Tests

#ifndef NDEBUG
  bool Verify(lldb::opaque_compiler_type_t type) override;
#endif

  bool IsArrayType(lldb::opaque_compiler_type_t type,
                   CompilerType *element_type, uint64_t *size,
                   bool *is_incomplete) override;

  bool IsVectorType(lldb::opaque_compiler_type_t type,
                    CompilerType *element_type, uint64_t *size) override;

  bool IsAggregateType(lldb::opaque_compiler_type_t type) override;

  bool IsAnonymousType(lldb::opaque_compiler_type_t type) override;

  bool IsBeingDefined(lldb::opaque_compiler_type_t type) override;

  bool IsCharType(lldb::opaque_compiler_type_t type) override;

  bool IsCompleteType(lldb::opaque_compiler_type_t type) override;

  bool IsConst(lldb::opaque_compiler_type_t type) override;

  bool IsCStringType(lldb::opaque_compiler_type_t type, uint32_t &length);

  static bool IsCXXClassType(const CompilerType &type);

  bool IsDefined(lldb::opaque_compiler_type_t type) override;

  bool IsFloatingPointType(lldb::opaque_compiler_type_t type, uint32_t &count,
                           bool &is_complex) override;

  unsigned GetPtrAuthKey(lldb::opaque_compiler_type_t type) override;
  unsigned GetPtrAuthDiscriminator(lldb::opaque_compiler_type_t type) override;
  bool GetPtrAuthAddressDiversity(lldb::opaque_compiler_type_t type) override;

  bool IsFunctionType(lldb::opaque_compiler_type_t type) override;

  uint32_t IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                  CompilerType *base_type_ptr) override;

  size_t
  GetNumberOfFunctionArguments(lldb::opaque_compiler_type_t type) override;

  CompilerType GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                          const size_t index) override;

  bool IsFunctionPointerType(lldb::opaque_compiler_type_t type) override;

  bool IsMemberFunctionPointerType(lldb::opaque_compiler_type_t type) override;

  bool IsBlockPointerType(lldb::opaque_compiler_type_t type,
                          CompilerType *function_pointer_type_ptr) override;

  bool IsIntegerType(lldb::opaque_compiler_type_t type,
                     bool &is_signed) override;

  bool IsEnumerationType(lldb::opaque_compiler_type_t type,
                         bool &is_signed) override;

  bool IsScopedEnumerationType(lldb::opaque_compiler_type_t type) override;

  static bool IsObjCClassType(const CompilerType &type);

  static bool IsObjCClassTypeAndHasIVars(const CompilerType &type,
                                         bool check_superclass);

  static bool IsObjCObjectOrInterfaceType(const CompilerType &type);

  static bool IsObjCObjectPointerType(const CompilerType &type,
                                      CompilerType *target_type = nullptr);

  bool IsPolymorphicClass(lldb::opaque_compiler_type_t type) override;

  static bool IsClassType(lldb::opaque_compiler_type_t type);

  static bool IsEnumType(lldb::opaque_compiler_type_t type);

  bool IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                             CompilerType *target_type, // Can pass nullptr
                             bool check_cplusplus, bool check_objc) override;

  bool IsRuntimeGeneratedType(lldb::opaque_compiler_type_t type) override;

  bool IsPointerType(lldb::opaque_compiler_type_t type,
                     CompilerType *pointee_type) override;

  bool IsPointerOrReferenceType(lldb::opaque_compiler_type_t type,
                                CompilerType *pointee_type) override;

  bool IsReferenceType(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_type, bool *is_rvalue) override;

  bool IsScalarType(lldb::opaque_compiler_type_t type) override;

  bool IsTypedefType(lldb::opaque_compiler_type_t type) override;

  bool IsVoidType(lldb::opaque_compiler_type_t type) override;

  bool CanPassInRegisters(const CompilerType &type) override;

  bool SupportsLanguage(lldb::LanguageType language) override;

  static std::optional<std::string> GetCXXClassName(const CompilerType &type);

  // Type Completion

  bool GetCompleteType(lldb::opaque_compiler_type_t type) override;

  bool IsForcefullyCompleted(lldb::opaque_compiler_type_t type) override;

  // Accessors

  ConstString GetTypeName(lldb::opaque_compiler_type_t type,
                          bool base_only) override;

  ConstString GetDisplayTypeName(lldb::opaque_compiler_type_t type) override;

  uint32_t GetTypeInfo(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_or_element_compiler_type) override;

  lldb::LanguageType
  GetMinimumLanguage(lldb::opaque_compiler_type_t type) override;

  lldb::TypeClass GetTypeClass(lldb::opaque_compiler_type_t type) override;

  unsigned GetTypeQualifiers(lldb::opaque_compiler_type_t type) override;

  // Creating related types

  CompilerType GetArrayElementType(lldb::opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) override;

  CompilerType GetArrayType(lldb::opaque_compiler_type_t type,
                            uint64_t size) override;

  CompilerType GetCanonicalType(lldb::opaque_compiler_type_t type) override;

  CompilerType
  GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) override;

  CompilerType
  GetEnumerationIntegerType(lldb::opaque_compiler_type_t type) override;

  // Returns -1 if this isn't a function of if the function doesn't have a
  // prototype Returns a value >= 0 if there is a prototype.
  int GetFunctionArgumentCount(lldb::opaque_compiler_type_t type) override;

  CompilerType GetFunctionArgumentTypeAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx) override;

  CompilerType
  GetFunctionReturnType(lldb::opaque_compiler_type_t type) override;

  size_t GetNumMemberFunctions(lldb::opaque_compiler_type_t type) override;

  TypeMemberFunctionImpl
  GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type,
                           size_t idx) override;

  CompilerType GetNonReferenceType(lldb::opaque_compiler_type_t type) override;

  CompilerType GetPointeeType(lldb::opaque_compiler_type_t type) override;

  CompilerType GetPointerType(lldb::opaque_compiler_type_t type) override;

  CompilerType
  GetLValueReferenceType(lldb::opaque_compiler_type_t type) override;

  CompilerType
  GetRValueReferenceType(lldb::opaque_compiler_type_t type) override;

  CompilerType GetAtomicType(lldb::opaque_compiler_type_t type) override;

  CompilerType AddConstModifier(lldb::opaque_compiler_type_t type) override;

  CompilerType AddPtrAuthModifier(lldb::opaque_compiler_type_t type,
                                  uint32_t payload) override;

  CompilerType AddVolatileModifier(lldb::opaque_compiler_type_t type) override;

  CompilerType AddRestrictModifier(lldb::opaque_compiler_type_t type) override;

  /// Using the current type, create a new typedef to that type using
  /// "typedef_name" as the name and "decl_ctx" as the decl context.
  /// \param opaque_payload is an opaque TypePayloadClang.
  CompilerType CreateTypedef(lldb::opaque_compiler_type_t type,
                             const char *name,
                             const CompilerDeclContext &decl_ctx,
                             uint32_t opaque_payload) override;

  // If the current object represents a typedef type, get the underlying type
  CompilerType GetTypedefedType(lldb::opaque_compiler_type_t type) override;

  // Create related types using the current type's AST
  CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) override;

  // Create a generic function prototype that can be used in ValuObject types
  // to correctly display a function pointer with the right value and summary.
  CompilerType CreateGenericFunctionPrototype() override;

  // Exploring the type

  const llvm::fltSemantics &GetFloatTypeSemantics(size_t byte_size) override;

  std::optional<uint64_t> GetByteSize(lldb::opaque_compiler_type_t type,
                                      ExecutionContextScope *exe_scope) {
    if (std::optional<uint64_t> bit_size = GetBitSize(type, exe_scope))
      return (*bit_size + 7) / 8;
    return std::nullopt;
  }

  std::optional<uint64_t> GetBitSize(lldb::opaque_compiler_type_t type,
                                     ExecutionContextScope *exe_scope) override;

  lldb::Encoding GetEncoding(lldb::opaque_compiler_type_t type,
                             uint64_t &count) override;

  lldb::Format GetFormat(lldb::opaque_compiler_type_t type) override;

  std::optional<size_t>
  GetTypeBitAlign(lldb::opaque_compiler_type_t type,
                  ExecutionContextScope *exe_scope) override;

  llvm::Expected<uint32_t>
  GetNumChildren(lldb::opaque_compiler_type_t type,
                 bool omit_empty_base_classes,
                 const ExecutionContext *exe_ctx) override;

  CompilerType GetBuiltinTypeByName(ConstString name) override;

  lldb::BasicType
  GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) override;

  void ForEachEnumerator(
      lldb::opaque_compiler_type_t type,
      std::function<bool(const CompilerType &integer_type,
                         ConstString name,
                         const llvm::APSInt &value)> const &callback) override;

  uint32_t GetNumFields(lldb::opaque_compiler_type_t type) override;

  CompilerType GetFieldAtIndex(lldb::opaque_compiler_type_t type, size_t idx,
                               std::string &name, uint64_t *bit_offset_ptr,
                               uint32_t *bitfield_bit_size_ptr,
                               bool *is_bitfield_ptr) override;

  uint32_t GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) override;

  uint32_t GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) override;

  CompilerType GetDirectBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                         size_t idx,
                                         uint32_t *bit_offset_ptr) override;

  CompilerType GetVirtualBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t idx,
                                          uint32_t *bit_offset_ptr) override;

  CompilerDecl GetStaticFieldWithName(lldb::opaque_compiler_type_t type,
                                      llvm::StringRef name) override;

  static uint32_t GetNumPointeeChildren(clang::QualType type);

  llvm::Expected<CompilerType> GetChildCompilerTypeAtIndex(
      lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint32_t &child_byte_size, int32_t &child_byte_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags) override;

  // Lookup a child given a name. This function will match base class names and
  // member member names in "clang_type" only, not descendants.
  uint32_t GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                                   llvm::StringRef name,
                                   bool omit_empty_base_classes) override;

  // Lookup a child member given a name. This function will match member names
  // only and will descend into "clang_type" children in search for the first
  // member in this class, or any base class that matches "name".
  // TODO: Return all matches for a given name by returning a
  // vector<vector<uint32_t>>
  // so we catch all names that match a given child name, not just the first.
  size_t
  GetIndexOfChildMemberWithName(lldb::opaque_compiler_type_t type,
                                llvm::StringRef name,
                                bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indexes) override;

  CompilerType GetDirectNestedTypeWithName(lldb::opaque_compiler_type_t type,
                                           llvm::StringRef name) override;

  bool IsTemplateType(lldb::opaque_compiler_type_t type) override;

  size_t GetNumTemplateArguments(lldb::opaque_compiler_type_t type,
                                 bool expand_pack) override;

  lldb::TemplateArgumentKind
  GetTemplateArgumentKind(lldb::opaque_compiler_type_t type, size_t idx,
                          bool expand_pack) override;
  CompilerType GetTypeTemplateArgument(lldb::opaque_compiler_type_t type,
                                       size_t idx, bool expand_pack) override;
  std::optional<CompilerType::IntegralTemplateArgument>
  GetIntegralTemplateArgument(lldb::opaque_compiler_type_t type, size_t idx,
                              bool expand_pack) override;

  CompilerType GetTypeForFormatters(void *type) override;

#define LLDB_INVALID_DECL_LEVEL UINT32_MAX
  // LLDB_INVALID_DECL_LEVEL is returned by CountDeclLevels if child_decl_ctx
  // could not be found in decl_ctx.
  uint32_t CountDeclLevels(clang::DeclContext *frame_decl_ctx,
                           clang::DeclContext *child_decl_ctx,
                           ConstString *child_name = nullptr,
                           CompilerType *child_type = nullptr);

  // Modifying RecordType
  static clang::FieldDecl *AddFieldToRecordType(const CompilerType &type,
                                                llvm::StringRef name,
                                                const CompilerType &field_type,
                                                lldb::AccessType access,
                                                uint32_t bitfield_bit_size);

  static void BuildIndirectFields(const CompilerType &type);

  static void SetIsPacked(const CompilerType &type);

  static clang::VarDecl *AddVariableToRecordType(const CompilerType &type,
                                                 llvm::StringRef name,
                                                 const CompilerType &var_type,
                                                 lldb::AccessType access);

  /// Initializes a variable with an integer value.
  /// \param var The variable to initialize. Must not already have an
  ///            initializer and must have an integer or enum type.
  /// \param init_value The integer value that the variable should be
  ///                   initialized to. Has to match the bit width of the
  ///                   variable type.
  static void SetIntegerInitializerForVariable(clang::VarDecl *var,
                                               const llvm::APInt &init_value);

  /// Initializes a variable with a floating point value.
  /// \param var The variable to initialize. Must not already have an
  ///            initializer and must have a floating point type.
  /// \param init_value The float value that the variable should be
  ///                   initialized to.
  static void
  SetFloatingInitializerForVariable(clang::VarDecl *var,
                                    const llvm::APFloat &init_value);

  clang::CXXMethodDecl *AddMethodToCXXRecordType(
      lldb::opaque_compiler_type_t type, llvm::StringRef name,
      const char *mangled_name, const CompilerType &method_type,
      lldb::AccessType access, bool is_virtual, bool is_static, bool is_inline,
      bool is_explicit, bool is_attr_used, bool is_artificial);

  void AddMethodOverridesForCXXRecordType(lldb::opaque_compiler_type_t type);

  // C++ Base Classes
  std::unique_ptr<clang::CXXBaseSpecifier>
  CreateBaseClassSpecifier(lldb::opaque_compiler_type_t type,
                           lldb::AccessType access, bool is_virtual,
                           bool base_of_class);

  bool TransferBaseClasses(
      lldb::opaque_compiler_type_t type,
      std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> bases);

  static bool SetObjCSuperClass(const CompilerType &type,
                                const CompilerType &superclass_compiler_type);

  static bool AddObjCClassProperty(const CompilerType &type,
                                   const char *property_name,
                                   const CompilerType &property_compiler_type,
                                   clang::ObjCIvarDecl *ivar_decl,
                                   const char *property_setter_name,
                                   const char *property_getter_name,
                                   uint32_t property_attributes,
                                   ClangASTMetadata *metadata);

  static clang::ObjCMethodDecl *AddMethodToObjCObjectType(
      const CompilerType &type,
      const char *name, // the full symbol name as seen in the symbol table
                        // (lldb::opaque_compiler_type_t type, "-[NString
                        // stringWithCString:]")
      const CompilerType &method_compiler_type, bool is_artificial,
      bool is_variadic, bool is_objc_direct_call);

  static bool SetHasExternalStorage(lldb::opaque_compiler_type_t type,
                                    bool has_extern);

  // Tag Declarations
  static bool StartTagDeclarationDefinition(const CompilerType &type);

  static bool CompleteTagDeclarationDefinition(const CompilerType &type);

  // Modifying Enumeration types
  clang::EnumConstantDecl *AddEnumerationValueToEnumerationType(
      const CompilerType &enum_type, const Declaration &decl, const char *name,
      int64_t enum_value, uint32_t enum_value_bit_size);
  clang::EnumConstantDecl *AddEnumerationValueToEnumerationType(
      const CompilerType &enum_type, const Declaration &decl, const char *name,
      const llvm::APSInt &value);

  /// Returns the underlying integer type for an enum type. If the given type
  /// is invalid or not an enum-type, the function returns an invalid
  /// CompilerType.
  CompilerType GetEnumerationIntegerType(CompilerType type);

  // Pointers & References

  // Call this function using the class type when you want to make a member
  // pointer type to pointee_type.
  static CompilerType CreateMemberPointerType(const CompilerType &type,
                                              const CompilerType &pointee_type);

  // Dumping types
#ifndef NDEBUG
  /// Convenience LLVM-style dump method for use in the debugger only.
  /// In contrast to the other \p Dump() methods this directly invokes
  /// \p clang::QualType::dump().
  LLVM_DUMP_METHOD void dump(lldb::opaque_compiler_type_t type) const override;
#endif

  /// \see lldb_private::TypeSystem::Dump
  void Dump(llvm::raw_ostream &output) override;

  /// Dump clang AST types from the symbol file.
  ///
  /// \param[in] s
  ///       A stream to send the dumped AST node(s) to
  /// \param[in] symbol_name
  ///       The name of the symbol to dump, if it is empty dump all the symbols
  void DumpFromSymbolFile(Stream &s, llvm::StringRef symbol_name);

  bool DumpTypeValue(lldb::opaque_compiler_type_t type, Stream &s,
                     lldb::Format format, const DataExtractor &data,
                     lldb::offset_t data_offset, size_t data_byte_size,
                     uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                     ExecutionContextScope *exe_scope) override;

  void DumpTypeDescription(
      lldb::opaque_compiler_type_t type,
      lldb::DescriptionLevel level = lldb::eDescriptionLevelFull) override;

  void DumpTypeDescription(
      lldb::opaque_compiler_type_t type, Stream &s,
      lldb::DescriptionLevel level = lldb::eDescriptionLevelFull) override;

  static void DumpTypeName(const CompilerType &type);

  static clang::EnumDecl *GetAsEnumDecl(const CompilerType &type);

  static clang::RecordDecl *GetAsRecordDecl(const CompilerType &type);

  static clang::TagDecl *GetAsTagDecl(const CompilerType &type);

  static clang::TypedefNameDecl *GetAsTypedefDecl(const CompilerType &type);

  static clang::CXXRecordDecl *
  GetAsCXXRecordDecl(lldb::opaque_compiler_type_t type);

  static clang::ObjCInterfaceDecl *
  GetAsObjCInterfaceDecl(const CompilerType &type);

  clang::ClassTemplateDecl *ParseClassTemplateDecl(
      clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
      lldb::AccessType access_type, const char *parent_name, int tag_decl_kind,
      const TypeSystemClang::TemplateParameterInfos &template_param_infos);

  clang::BlockDecl *CreateBlockDeclaration(clang::DeclContext *ctx,
                                           OptionalClangModuleID owning_module);

  clang::UsingDirectiveDecl *
  CreateUsingDirectiveDeclaration(clang::DeclContext *decl_ctx,
                                  OptionalClangModuleID owning_module,
                                  clang::NamespaceDecl *ns_decl);

  clang::UsingDecl *CreateUsingDeclaration(clang::DeclContext *current_decl_ctx,
                                           OptionalClangModuleID owning_module,
                                           clang::NamedDecl *target);

  clang::VarDecl *CreateVariableDeclaration(clang::DeclContext *decl_context,
                                            OptionalClangModuleID owning_module,
                                            const char *name,
                                            clang::QualType type);

  static lldb::opaque_compiler_type_t
  GetOpaqueCompilerType(clang::ASTContext *ast, lldb::BasicType basic_type);

  static clang::QualType GetQualType(lldb::opaque_compiler_type_t type) {
    if (type)
      return clang::QualType::getFromOpaquePtr(type);
    return clang::QualType();
  }

  static clang::QualType
  GetCanonicalQualType(lldb::opaque_compiler_type_t type) {
    if (type)
      return clang::QualType::getFromOpaquePtr(type).getCanonicalType();
    return clang::QualType();
  }

  clang::DeclarationName
  GetDeclarationName(llvm::StringRef name,
                     const CompilerType &function_clang_type);

  clang::LangOptions *GetLangOpts() const {
    return m_language_options_up.get();
  }
  clang::SourceManager *GetSourceMgr() const {
    return m_source_manager_up.get();
  }

  /// Complete a type from debug info, or mark it as forcefully completed if
  /// there is no definition of the type in the current Module. Call this
  /// function in contexts where the usual C++ rules require a type to be
  /// complete (base class, member, etc.).
  static void RequireCompleteType(CompilerType type);

  bool SetDeclIsForcefullyCompleted(const clang::TagDecl *td);

  /// Return the template parameters (including surrounding <>) in string form.
  std::string
  PrintTemplateParams(const TemplateParameterInfos &template_param_infos);

private:
  /// Returns the PrintingPolicy used when generating the internal type names.
  /// These type names are mostly used for the formatter selection.
  clang::PrintingPolicy GetTypePrintingPolicy();
  /// Returns the internal type name for the given NamedDecl using the
  /// type printing policy.
  std::string GetTypeNameForDecl(const clang::NamedDecl *named_decl,
                                 bool qualified = true);

  const clang::ClassTemplateSpecializationDecl *
  GetAsTemplateSpecialization(lldb::opaque_compiler_type_t type);

  bool IsTypeImpl(lldb::opaque_compiler_type_t type,
                  llvm::function_ref<bool(clang::QualType)> predicate) const;

  /// Emits information about this TypeSystem into the expression log.
  ///
  /// Helper method that is used in \ref TypeSystemClang::TypeSystemClang
  /// on creation of a new instance.
  void LogCreation() const;

  // Classes that inherit from TypeSystemClang can see and modify these
  std::string m_target_triple;
  std::unique_ptr<clang::ASTContext> m_ast_up;
  std::unique_ptr<clang::LangOptions> m_language_options_up;
  std::unique_ptr<clang::FileManager> m_file_manager_up;
  std::unique_ptr<clang::SourceManager> m_source_manager_up;
  std::unique_ptr<clang::DiagnosticsEngine> m_diagnostics_engine_up;
  std::unique_ptr<clang::DiagnosticConsumer> m_diagnostic_consumer_up;
  std::shared_ptr<clang::TargetOptions> m_target_options_rp;
  std::unique_ptr<clang::TargetInfo> m_target_info_up;
  std::unique_ptr<clang::IdentifierTable> m_identifier_table_up;
  std::unique_ptr<clang::SelectorTable> m_selector_table_up;
  std::unique_ptr<clang::Builtin::Context> m_builtins_up;
  std::unique_ptr<clang::HeaderSearch> m_header_search_up;
  std::unique_ptr<clang::ModuleMap> m_module_map_up;
  std::unique_ptr<DWARFASTParserClang> m_dwarf_ast_parser_up;
  std::unique_ptr<PDBASTParser> m_pdb_ast_parser_up;
  std::unique_ptr<npdb::PdbAstBuilder> m_native_pdb_ast_parser_up;
  std::unique_ptr<clang::MangleContext> m_mangle_ctx_up;
  uint32_t m_pointer_byte_size = 0;
  bool m_ast_owned = false;
  /// A string describing what this TypeSystemClang represents (e.g.,
  /// AST for debug information, an expression, some other utility ClangAST).
  /// Useful for logging and debugging.
  std::string m_display_name;

  typedef llvm::DenseMap<const clang::Decl *, ClangASTMetadata> DeclMetadataMap;
  /// Maps Decls to their associated ClangASTMetadata.
  DeclMetadataMap m_decl_metadata;

  typedef llvm::DenseMap<const clang::Type *, ClangASTMetadata> TypeMetadataMap;
  /// Maps Types to their associated ClangASTMetadata.
  TypeMetadataMap m_type_metadata;

  typedef llvm::DenseMap<const clang::CXXRecordDecl *, clang::AccessSpecifier>
      CXXRecordDeclAccessMap;
  /// Maps CXXRecordDecl to their most recent added method/field's
  /// AccessSpecifier.
  CXXRecordDeclAccessMap m_cxx_record_decl_access;

  /// The sema associated that is currently used to build this ASTContext.
  /// May be null if we are already done parsing this ASTContext or the
  /// ASTContext wasn't created by parsing source code.
  clang::Sema *m_sema = nullptr;

  // For TypeSystemClang only
  TypeSystemClang(const TypeSystemClang &);
  const TypeSystemClang &operator=(const TypeSystemClang &);
  /// Creates the internal ASTContext.
  void CreateASTContext();
  void SetTargetTriple(llvm::StringRef target_triple);
};

/// The TypeSystemClang instance used for the scratch ASTContext in a
/// lldb::Target.
class ScratchTypeSystemClang : public TypeSystemClang {
  /// LLVM RTTI support
  static char ID;

public:
  ScratchTypeSystemClang(Target &target, llvm::Triple triple);

  ~ScratchTypeSystemClang() override = default;

  void Finalize() override;

  /// The different kinds of isolated ASTs within the scratch TypeSystem.
  ///
  /// These ASTs are isolated from the main scratch AST and are each
  /// dedicated to a special language option/feature that makes the contained
  /// AST nodes incompatible with other AST nodes.
  enum IsolatedASTKind {
    /// The isolated AST for declarations/types from expressions that imported
    /// type information from a C++ module. The templates from a C++ module
    /// often conflict with the templates we generate from debug information,
    /// so we put these types in their own AST.
    CppModules
  };

  /// Alias for requesting the default scratch TypeSystemClang in GetForTarget.
  // This isn't constexpr as gtest/std::optional comparison logic is trying
  // to get the address of this for pretty-printing.
  static const std::nullopt_t DefaultAST;

  /// Infers the appropriate sub-AST from Clang's LangOptions.
  static std::optional<IsolatedASTKind>
  InferIsolatedASTKindFromLangOpts(const clang::LangOptions &l) {
    // If modules are activated we want the dedicated C++ module AST.
    // See IsolatedASTKind::CppModules for more info.
    if (l.Modules)
      return IsolatedASTKind::CppModules;
    return DefaultAST;
  }

  /// Returns the scratch TypeSystemClang for the given target.
  /// \param target The Target which scratch TypeSystemClang should be returned.
  /// \param ast_kind Allows requesting a specific sub-AST instead of the
  ///                 default scratch AST. See also `IsolatedASTKind`.
  /// \param create_on_demand If the scratch TypeSystemClang instance can be
  /// created by this call if it doesn't exist yet. If it doesn't exist yet and
  /// this parameter is false, this function returns a nullptr.
  /// \return The scratch type system of the target or a nullptr in case an
  ///         error occurred.
  static lldb::TypeSystemClangSP
  GetForTarget(Target &target,
               std::optional<IsolatedASTKind> ast_kind = DefaultAST,
               bool create_on_demand = true);

  /// Returns the scratch TypeSystemClang for the given target. The returned
  /// TypeSystemClang will be the scratch AST or a sub-AST, depending on which
  /// fits best to the passed LangOptions.
  /// \param target The Target which scratch TypeSystemClang should be returned.
  /// \param lang_opts The LangOptions of a clang ASTContext that the caller
  ///                  wants to export type information from. This is used to
  ///                  find the best matching sub-AST that will be returned.
  static lldb::TypeSystemClangSP
  GetForTarget(Target &target, const clang::LangOptions &lang_opts) {
    return GetForTarget(target, InferIsolatedASTKindFromLangOpts(lang_opts));
  }

  /// \see lldb_private::TypeSystem::Dump
  void Dump(llvm::raw_ostream &output) override;

  UserExpression *GetUserExpression(llvm::StringRef expr,
                                    llvm::StringRef prefix,
                                    SourceLanguage language,
                                    Expression::ResultType desired_type,
                                    const EvaluateExpressionOptions &options,
                                    ValueObject *ctx_obj) override;

  FunctionCaller *GetFunctionCaller(const CompilerType &return_type,
                                    const Address &function_address,
                                    const ValueList &arg_value_list,
                                    const char *name) override;

  std::unique_ptr<UtilityFunction>
  CreateUtilityFunction(std::string text, std::string name) override;

  PersistentExpressionState *GetPersistentExpressionState() override;

  /// Unregisters the given ASTContext as a source from the scratch AST (and
  /// all sub-ASTs).
  /// \see ClangASTImporter::ForgetSource
  void ForgetSource(clang::ASTContext *src_ctx, ClangASTImporter &importer);

  // llvm casting support
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || TypeSystemClang::isA(ClassID);
  }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

private:
  std::unique_ptr<ClangASTSource> CreateASTSource();
  /// Returns the requested sub-AST.
  /// Will lazily create the sub-AST if it hasn't been created before.
  TypeSystemClang &GetIsolatedAST(IsolatedASTKind feature);

  /// The target triple.
  /// This was potentially adjusted and might not be identical to the triple
  /// of `m_target_wp`.
  llvm::Triple m_triple;
  lldb::TargetWP m_target_wp;
  /// The persistent variables associated with this process for the expression
  /// parser.
  std::unique_ptr<ClangPersistentVariables> m_persistent_variables;
  /// The ExternalASTSource that performs lookups and completes minimally
  /// imported types.
  std::unique_ptr<ClangASTSource> m_scratch_ast_source_up;

  // FIXME: GCC 5.x doesn't support enum as map keys.
  typedef int IsolatedASTKey;

  /// Map from IsolatedASTKind to their actual TypeSystemClang instance.
  /// This map is lazily filled with sub-ASTs and should be accessed via
  /// `GetSubAST` (which lazily fills this map).
  llvm::DenseMap<IsolatedASTKey, std::shared_ptr<TypeSystemClang>>
      m_isolated_asts;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLANG_TYPESYSTEMCLANG_H
