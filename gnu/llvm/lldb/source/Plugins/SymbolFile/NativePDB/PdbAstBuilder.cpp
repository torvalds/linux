#include "PdbAstBuilder.h"

#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolRecordHelpers.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/PublicsStream.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Demangle/MicrosoftDemangle.h"

#include "PdbUtil.h"
#include "Plugins/ExpressionParser/Clang/ClangASTMetadata.h"
#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/Language/CPlusPlus/MSVCUndecoratedNameParser.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "SymbolFileNativePDB.h"
#include "UdtRecordCompleter.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/LLDBAssert.h"
#include <optional>
#include <string_view>

using namespace lldb_private;
using namespace lldb_private::npdb;
using namespace llvm::codeview;
using namespace llvm::pdb;

namespace {
struct CreateMethodDecl : public TypeVisitorCallbacks {
  CreateMethodDecl(PdbIndex &m_index, TypeSystemClang &m_clang,
                   TypeIndex func_type_index,
                   clang::FunctionDecl *&function_decl,
                   lldb::opaque_compiler_type_t parent_ty,
                   llvm::StringRef proc_name, CompilerType func_ct)
      : m_index(m_index), m_clang(m_clang), func_type_index(func_type_index),
        function_decl(function_decl), parent_ty(parent_ty),
        proc_name(proc_name), func_ct(func_ct) {}
  PdbIndex &m_index;
  TypeSystemClang &m_clang;
  TypeIndex func_type_index;
  clang::FunctionDecl *&function_decl;
  lldb::opaque_compiler_type_t parent_ty;
  llvm::StringRef proc_name;
  CompilerType func_ct;

  llvm::Error visitKnownMember(CVMemberRecord &cvr,
                               OverloadedMethodRecord &overloaded) override {
    TypeIndex method_list_idx = overloaded.MethodList;

    CVType method_list_type = m_index.tpi().getType(method_list_idx);
    assert(method_list_type.kind() == LF_METHODLIST);

    MethodOverloadListRecord method_list;
    llvm::cantFail(TypeDeserializer::deserializeAs<MethodOverloadListRecord>(
        method_list_type, method_list));

    for (const OneMethodRecord &method : method_list.Methods) {
      if (method.getType().getIndex() == func_type_index.getIndex())
        AddMethod(overloaded.Name, method.getAccess(), method.getOptions(),
                  method.Attrs);
    }

    return llvm::Error::success();
  }

  llvm::Error visitKnownMember(CVMemberRecord &cvr,
                               OneMethodRecord &record) override {
    AddMethod(record.getName(), record.getAccess(), record.getOptions(),
              record.Attrs);
    return llvm::Error::success();
  }

  void AddMethod(llvm::StringRef name, MemberAccess access,
                 MethodOptions options, MemberAttributes attrs) {
    if (name != proc_name || function_decl)
      return;
    lldb::AccessType access_type = TranslateMemberAccess(access);
    bool is_virtual = attrs.isVirtual();
    bool is_static = attrs.isStatic();
    bool is_artificial = (options & MethodOptions::CompilerGenerated) ==
                         MethodOptions::CompilerGenerated;
    function_decl = m_clang.AddMethodToCXXRecordType(
        parent_ty, proc_name,
        /*mangled_name=*/nullptr, func_ct, /*access=*/access_type,
        /*is_virtual=*/is_virtual, /*is_static=*/is_static,
        /*is_inline=*/false, /*is_explicit=*/false,
        /*is_attr_used=*/false, /*is_artificial=*/is_artificial);
  }
};
} // namespace

static clang::TagTypeKind TranslateUdtKind(const TagRecord &cr) {
  switch (cr.Kind) {
  case TypeRecordKind::Class:
    return clang::TagTypeKind::Class;
  case TypeRecordKind::Struct:
    return clang::TagTypeKind::Struct;
  case TypeRecordKind::Union:
    return clang::TagTypeKind::Union;
  case TypeRecordKind::Interface:
    return clang::TagTypeKind::Interface;
  case TypeRecordKind::Enum:
    return clang::TagTypeKind::Enum;
  default:
    lldbassert(false && "Invalid tag record kind!");
    return clang::TagTypeKind::Struct;
  }
}

static bool IsCVarArgsFunction(llvm::ArrayRef<TypeIndex> args) {
  if (args.empty())
    return false;
  return args.back() == TypeIndex::None();
}

static bool
AnyScopesHaveTemplateParams(llvm::ArrayRef<llvm::ms_demangle::Node *> scopes) {
  for (llvm::ms_demangle::Node *n : scopes) {
    auto *idn = static_cast<llvm::ms_demangle::IdentifierNode *>(n);
    if (idn->TemplateParams)
      return true;
  }
  return false;
}

static std::optional<clang::CallingConv>
TranslateCallingConvention(llvm::codeview::CallingConvention conv) {
  using CC = llvm::codeview::CallingConvention;
  switch (conv) {

  case CC::NearC:
  case CC::FarC:
    return clang::CallingConv::CC_C;
  case CC::NearPascal:
  case CC::FarPascal:
    return clang::CallingConv::CC_X86Pascal;
  case CC::NearFast:
  case CC::FarFast:
    return clang::CallingConv::CC_X86FastCall;
  case CC::NearStdCall:
  case CC::FarStdCall:
    return clang::CallingConv::CC_X86StdCall;
  case CC::ThisCall:
    return clang::CallingConv::CC_X86ThisCall;
  case CC::NearVector:
    return clang::CallingConv::CC_X86VectorCall;
  default:
    return std::nullopt;
  }
}

static bool IsAnonymousNamespaceName(llvm::StringRef name) {
  return name == "`anonymous namespace'" || name == "`anonymous-namespace'";
}

PdbAstBuilder::PdbAstBuilder(TypeSystemClang &clang) : m_clang(clang) {}

lldb_private::CompilerDeclContext PdbAstBuilder::GetTranslationUnitDecl() {
  return ToCompilerDeclContext(*m_clang.GetTranslationUnitDecl());
}

std::pair<clang::DeclContext *, std::string>
PdbAstBuilder::CreateDeclInfoForType(const TagRecord &record, TypeIndex ti) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  // FIXME: Move this to GetDeclContextContainingUID.
  if (!record.hasUniqueName())
    return CreateDeclInfoForUndecoratedName(record.Name);

  llvm::ms_demangle::Demangler demangler;
  std::string_view sv(record.UniqueName.begin(), record.UniqueName.size());
  llvm::ms_demangle::TagTypeNode *ttn = demangler.parseTagUniqueName(sv);
  if (demangler.Error)
    return {m_clang.GetTranslationUnitDecl(), std::string(record.UniqueName)};

  llvm::ms_demangle::IdentifierNode *idn =
      ttn->QualifiedName->getUnqualifiedIdentifier();
  std::string uname = idn->toString(llvm::ms_demangle::OF_NoTagSpecifier);

  llvm::ms_demangle::NodeArrayNode *name_components =
      ttn->QualifiedName->Components;
  llvm::ArrayRef<llvm::ms_demangle::Node *> scopes(name_components->Nodes,
                                                   name_components->Count - 1);

  clang::DeclContext *context = m_clang.GetTranslationUnitDecl();

  // If this type doesn't have a parent type in the debug info, then the best we
  // can do is to say that it's either a series of namespaces (if the scope is
  // non-empty), or the translation unit (if the scope is empty).
  std::optional<TypeIndex> parent_index = pdb->GetParentType(ti);
  if (!parent_index) {
    if (scopes.empty())
      return {context, uname};

    // If there is no parent in the debug info, but some of the scopes have
    // template params, then this is a case of bad debug info.  See, for
    // example, llvm.org/pr39607.  We don't want to create an ambiguity between
    // a NamespaceDecl and a CXXRecordDecl, so instead we create a class at
    // global scope with the fully qualified name.
    if (AnyScopesHaveTemplateParams(scopes))
      return {context, std::string(record.Name)};

    for (llvm::ms_demangle::Node *scope : scopes) {
      auto *nii = static_cast<llvm::ms_demangle::NamedIdentifierNode *>(scope);
      std::string str = nii->toString();
      context = GetOrCreateNamespaceDecl(str.c_str(), *context);
    }
    return {context, uname};
  }

  // Otherwise, all we need to do is get the parent type of this type and
  // recurse into our lazy type creation / AST reconstruction logic to get an
  // LLDB TypeSP for the parent.  This will cause the AST to automatically get
  // the right DeclContext created for any parent.
  clang::QualType parent_qt = GetOrCreateType(*parent_index);
  if (parent_qt.isNull())
    return {nullptr, ""};

  context = clang::TagDecl::castToDeclContext(parent_qt->getAsTagDecl());
  return {context, uname};
}

static bool isLocalVariableType(SymbolKind K) {
  switch (K) {
  case S_REGISTER:
  case S_REGREL32:
  case S_LOCAL:
    return true;
  default:
    break;
  }
  return false;
}

clang::Decl *PdbAstBuilder::GetOrCreateSymbolForId(PdbCompilandSymId id) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol cvs = index.ReadSymbolRecord(id);

  if (isLocalVariableType(cvs.kind())) {
    clang::DeclContext *scope = GetParentDeclContext(id);
    if (!scope)
      return nullptr;
    clang::Decl *scope_decl = clang::Decl::castFromDeclContext(scope);
    PdbCompilandSymId scope_id =
        PdbSymUid(m_decl_to_status[scope_decl].uid).asCompilandSym();
    return GetOrCreateVariableDecl(scope_id, id);
  }

  switch (cvs.kind()) {
  case S_GPROC32:
  case S_LPROC32:
    return GetOrCreateFunctionDecl(id);
  case S_GDATA32:
  case S_LDATA32:
  case S_GTHREAD32:
  case S_CONSTANT:
    // global variable
    return nullptr;
  case S_BLOCK32:
    return GetOrCreateBlockDecl(id);
  case S_INLINESITE:
    return GetOrCreateInlinedFunctionDecl(id);
  default:
    return nullptr;
  }
}

std::optional<CompilerDecl>
PdbAstBuilder::GetOrCreateDeclForUid(PdbSymUid uid) {
  if (clang::Decl *result = TryGetDecl(uid))
    return ToCompilerDecl(*result);

  clang::Decl *result = nullptr;
  switch (uid.kind()) {
  case PdbSymUidKind::CompilandSym:
    result = GetOrCreateSymbolForId(uid.asCompilandSym());
    break;
  case PdbSymUidKind::Type: {
    clang::QualType qt = GetOrCreateType(uid.asTypeSym());
    if (qt.isNull())
      return std::nullopt;
    if (auto *tag = qt->getAsTagDecl()) {
      result = tag;
      break;
    }
    return std::nullopt;
  }
  default:
    return std::nullopt;
  }

  if (!result)
    return std::nullopt;
  m_uid_to_decl[toOpaqueUid(uid)] = result;
  return ToCompilerDecl(*result);
}

clang::DeclContext *PdbAstBuilder::GetOrCreateDeclContextForUid(PdbSymUid uid) {
  if (uid.kind() == PdbSymUidKind::CompilandSym) {
    if (uid.asCompilandSym().offset == 0)
      return FromCompilerDeclContext(GetTranslationUnitDecl());
  }
  auto option = GetOrCreateDeclForUid(uid);
  if (!option)
    return nullptr;
  clang::Decl *decl = FromCompilerDecl(*option);
  if (!decl)
    return nullptr;

  return clang::Decl::castToDeclContext(decl);
}

std::pair<clang::DeclContext *, std::string>
PdbAstBuilder::CreateDeclInfoForUndecoratedName(llvm::StringRef name) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  MSVCUndecoratedNameParser parser(name);
  llvm::ArrayRef<MSVCUndecoratedNameSpecifier> specs = parser.GetSpecifiers();

  auto *context = FromCompilerDeclContext(GetTranslationUnitDecl());

  llvm::StringRef uname = specs.back().GetBaseName();
  specs = specs.drop_back();
  if (specs.empty())
    return {context, std::string(name)};

  llvm::StringRef scope_name = specs.back().GetFullName();

  // It might be a class name, try that first.
  std::vector<TypeIndex> types = index.tpi().findRecordsByName(scope_name);
  while (!types.empty()) {
    clang::QualType qt = GetOrCreateType(types.back());
    if (qt.isNull())
      continue;
    clang::TagDecl *tag = qt->getAsTagDecl();
    if (tag)
      return {clang::TagDecl::castToDeclContext(tag), std::string(uname)};
    types.pop_back();
  }

  // If that fails, treat it as a series of namespaces.
  for (const MSVCUndecoratedNameSpecifier &spec : specs) {
    std::string ns_name = spec.GetBaseName().str();
    context = GetOrCreateNamespaceDecl(ns_name.c_str(), *context);
  }
  return {context, std::string(uname)};
}

clang::DeclContext *PdbAstBuilder::GetParentDeclContext(PdbSymUid uid) {
  // We must do this *without* calling GetOrCreate on the current uid, as
  // that would be an infinite recursion.
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex& index = pdb->GetIndex();
  switch (uid.kind()) {
  case PdbSymUidKind::CompilandSym: {
    std::optional<PdbCompilandSymId> scope =
        pdb->FindSymbolScope(uid.asCompilandSym());
    if (scope)
      return GetOrCreateDeclContextForUid(*scope);

    CVSymbol sym = index.ReadSymbolRecord(uid.asCompilandSym());
    return CreateDeclInfoForUndecoratedName(getSymbolName(sym)).first;
  }
  case PdbSymUidKind::Type: {
    // It could be a namespace, class, or global.  We don't support nested
    // functions yet.  Anyway, we just need to consult the parent type map.
    PdbTypeSymId type_id = uid.asTypeSym();
    std::optional<TypeIndex> parent_index = pdb->GetParentType(type_id.index);
    if (!parent_index)
      return FromCompilerDeclContext(GetTranslationUnitDecl());
    return GetOrCreateDeclContextForUid(PdbTypeSymId(*parent_index));
  }
  case PdbSymUidKind::FieldListMember:
    // In this case the parent DeclContext is the one for the class that this
    // member is inside of.
    break;
  case PdbSymUidKind::GlobalSym: {
    // If this refers to a compiland symbol, just recurse in with that symbol.
    // The only other possibilities are S_CONSTANT and S_UDT, in which case we
    // need to parse the undecorated name to figure out the scope, then look
    // that up in the TPI stream.  If it's found, it's a type, othewrise it's
    // a series of namespaces.
    // FIXME: do this.
    CVSymbol global = index.ReadSymbolRecord(uid.asGlobalSym());
    switch (global.kind()) {
    case SymbolKind::S_GDATA32:
    case SymbolKind::S_LDATA32:
      return CreateDeclInfoForUndecoratedName(getSymbolName(global)).first;;
    case SymbolKind::S_PROCREF:
    case SymbolKind::S_LPROCREF: {
      ProcRefSym ref{global.kind()};
      llvm::cantFail(
          SymbolDeserializer::deserializeAs<ProcRefSym>(global, ref));
      PdbCompilandSymId cu_sym_id{ref.modi(), ref.SymOffset};
      return GetParentDeclContext(cu_sym_id);
    }
    case SymbolKind::S_CONSTANT:
    case SymbolKind::S_UDT:
      return CreateDeclInfoForUndecoratedName(getSymbolName(global)).first;
    default:
      break;
    }
    break;
  }
  default:
    break;
  }
  return FromCompilerDeclContext(GetTranslationUnitDecl());
}

bool PdbAstBuilder::CompleteType(clang::QualType qt) {
  if (qt.isNull())
    return false;
  clang::TagDecl *tag = qt->getAsTagDecl();
  if (qt->isArrayType()) {
    const clang::Type *element_type = qt->getArrayElementTypeNoTypeQual();
    tag = element_type->getAsTagDecl();
  }
  if (!tag)
    return false;

  return CompleteTagDecl(*tag);
}

bool PdbAstBuilder::CompleteTagDecl(clang::TagDecl &tag) {
  // If this is not in our map, it's an error.
  auto status_iter = m_decl_to_status.find(&tag);
  lldbassert(status_iter != m_decl_to_status.end());

  // If it's already complete, just return.
  DeclStatus &status = status_iter->second;
  if (status.resolved)
    return true;

  PdbTypeSymId type_id = PdbSymUid(status.uid).asTypeSym();
  PdbIndex &index = static_cast<SymbolFileNativePDB *>(
                        m_clang.GetSymbolFile()->GetBackingSymbolFile())
                        ->GetIndex();
  lldbassert(IsTagRecord(type_id, index.tpi()));

  clang::QualType tag_qt = m_clang.getASTContext().getTypeDeclType(&tag);
  TypeSystemClang::SetHasExternalStorage(tag_qt.getAsOpaquePtr(), false);

  TypeIndex tag_ti = type_id.index;
  CVType cvt = index.tpi().getType(tag_ti);
  if (cvt.kind() == LF_MODIFIER)
    tag_ti = LookThroughModifierRecord(cvt);

  PdbTypeSymId best_ti = GetBestPossibleDecl(tag_ti, index.tpi());
  cvt = index.tpi().getType(best_ti.index);
  lldbassert(IsTagRecord(cvt));

  if (IsForwardRefUdt(cvt)) {
    // If we can't find a full decl for this forward ref anywhere in the debug
    // info, then we have no way to complete it.
    return false;
  }

  TypeIndex field_list_ti = GetFieldListIndex(cvt);
  CVType field_list_cvt = index.tpi().getType(field_list_ti);
  if (field_list_cvt.kind() != LF_FIELDLIST)
    return false;
  FieldListRecord field_list;
  if (llvm::Error error = TypeDeserializer::deserializeAs<FieldListRecord>(
          field_list_cvt, field_list))
    llvm::consumeError(std::move(error));

  // Visit all members of this class, then perform any finalization necessary
  // to complete the class.
  CompilerType ct = ToCompilerType(tag_qt);
  UdtRecordCompleter completer(best_ti, ct, tag, *this, index, m_decl_to_status,
                               m_cxx_record_map);
  llvm::Error error =
      llvm::codeview::visitMemberRecordStream(field_list.Data, completer);
  completer.complete();

  m_decl_to_status[&tag].resolved = true;
  if (error) {
    llvm::consumeError(std::move(error));
    return false;
  }
  return true;
}

clang::QualType PdbAstBuilder::CreateSimpleType(TypeIndex ti) {
  if (ti == TypeIndex::NullptrT())
    return GetBasicType(lldb::eBasicTypeNullPtr);

  if (ti.getSimpleMode() != SimpleTypeMode::Direct) {
    clang::QualType direct_type = GetOrCreateType(ti.makeDirect());
    if (direct_type.isNull())
      return {};
    return m_clang.getASTContext().getPointerType(direct_type);
  }

  if (ti.getSimpleKind() == SimpleTypeKind::NotTranslated)
    return {};

  lldb::BasicType bt = GetCompilerTypeForSimpleKind(ti.getSimpleKind());
  if (bt == lldb::eBasicTypeInvalid)
    return {};

  return GetBasicType(bt);
}

clang::QualType PdbAstBuilder::CreatePointerType(const PointerRecord &pointer) {
  clang::QualType pointee_type = GetOrCreateType(pointer.ReferentType);

  // This can happen for pointers to LF_VTSHAPE records, which we shouldn't
  // create in the AST.
  if (pointee_type.isNull())
    return {};

  if (pointer.isPointerToMember()) {
    MemberPointerInfo mpi = pointer.getMemberInfo();
    clang::QualType class_type = GetOrCreateType(mpi.ContainingType);
    if (class_type.isNull())
      return {};
    if (clang::TagDecl *tag = class_type->getAsTagDecl()) {
      clang::MSInheritanceAttr::Spelling spelling;
      switch (mpi.Representation) {
      case llvm::codeview::PointerToMemberRepresentation::SingleInheritanceData:
      case llvm::codeview::PointerToMemberRepresentation::
          SingleInheritanceFunction:
        spelling =
            clang::MSInheritanceAttr::Spelling::Keyword_single_inheritance;
        break;
      case llvm::codeview::PointerToMemberRepresentation::
          MultipleInheritanceData:
      case llvm::codeview::PointerToMemberRepresentation::
          MultipleInheritanceFunction:
        spelling =
            clang::MSInheritanceAttr::Spelling::Keyword_multiple_inheritance;
        break;
      case llvm::codeview::PointerToMemberRepresentation::
          VirtualInheritanceData:
      case llvm::codeview::PointerToMemberRepresentation::
          VirtualInheritanceFunction:
        spelling =
            clang::MSInheritanceAttr::Spelling::Keyword_virtual_inheritance;
        break;
      case llvm::codeview::PointerToMemberRepresentation::Unknown:
        spelling =
            clang::MSInheritanceAttr::Spelling::Keyword_unspecified_inheritance;
        break;
      default:
        spelling = clang::MSInheritanceAttr::Spelling::SpellingNotCalculated;
        break;
      }
      tag->addAttr(clang::MSInheritanceAttr::CreateImplicit(
          m_clang.getASTContext(), spelling));
    }
    return m_clang.getASTContext().getMemberPointerType(
        pointee_type, class_type.getTypePtr());
  }

  clang::QualType pointer_type;
  if (pointer.getMode() == PointerMode::LValueReference)
    pointer_type = m_clang.getASTContext().getLValueReferenceType(pointee_type);
  else if (pointer.getMode() == PointerMode::RValueReference)
    pointer_type = m_clang.getASTContext().getRValueReferenceType(pointee_type);
  else
    pointer_type = m_clang.getASTContext().getPointerType(pointee_type);

  if ((pointer.getOptions() & PointerOptions::Const) != PointerOptions::None)
    pointer_type.addConst();

  if ((pointer.getOptions() & PointerOptions::Volatile) != PointerOptions::None)
    pointer_type.addVolatile();

  if ((pointer.getOptions() & PointerOptions::Restrict) != PointerOptions::None)
    pointer_type.addRestrict();

  return pointer_type;
}

clang::QualType
PdbAstBuilder::CreateModifierType(const ModifierRecord &modifier) {
  clang::QualType unmodified_type = GetOrCreateType(modifier.ModifiedType);
  if (unmodified_type.isNull())
    return {};

  if ((modifier.Modifiers & ModifierOptions::Const) != ModifierOptions::None)
    unmodified_type.addConst();
  if ((modifier.Modifiers & ModifierOptions::Volatile) != ModifierOptions::None)
    unmodified_type.addVolatile();

  return unmodified_type;
}

clang::QualType PdbAstBuilder::CreateRecordType(PdbTypeSymId id,
                                                const TagRecord &record) {
  clang::DeclContext *context = nullptr;
  std::string uname;
  std::tie(context, uname) = CreateDeclInfoForType(record, id.index);
  if (!context)
    return {};

  clang::TagTypeKind ttk = TranslateUdtKind(record);
  lldb::AccessType access = (ttk == clang::TagTypeKind::Class)
                                ? lldb::eAccessPrivate
                                : lldb::eAccessPublic;

  ClangASTMetadata metadata;
  metadata.SetUserID(toOpaqueUid(id));
  metadata.SetIsDynamicCXXType(false);

  CompilerType ct = m_clang.CreateRecordType(
      context, OptionalClangModuleID(), access, uname, llvm::to_underlying(ttk),
      lldb::eLanguageTypeC_plus_plus, &metadata);

  lldbassert(ct.IsValid());

  TypeSystemClang::StartTagDeclarationDefinition(ct);

  // Even if it's possible, don't complete it at this point. Just mark it
  // forward resolved, and if/when LLDB needs the full definition, it can
  // ask us.
  clang::QualType result =
      clang::QualType::getFromOpaquePtr(ct.GetOpaqueQualType());

  TypeSystemClang::SetHasExternalStorage(result.getAsOpaquePtr(), true);
  return result;
}

clang::Decl *PdbAstBuilder::TryGetDecl(PdbSymUid uid) const {
  auto iter = m_uid_to_decl.find(toOpaqueUid(uid));
  if (iter != m_uid_to_decl.end())
    return iter->second;
  return nullptr;
}

clang::NamespaceDecl *
PdbAstBuilder::GetOrCreateNamespaceDecl(const char *name,
                                        clang::DeclContext &context) {
  return m_clang.GetUniqueNamespaceDeclaration(
      IsAnonymousNamespaceName(name) ? nullptr : name, &context,
      OptionalClangModuleID());
}

clang::BlockDecl *
PdbAstBuilder::GetOrCreateBlockDecl(PdbCompilandSymId block_id) {
  if (clang::Decl *decl = TryGetDecl(block_id))
    return llvm::dyn_cast<clang::BlockDecl>(decl);

  clang::DeclContext *scope = GetParentDeclContext(block_id);

  clang::BlockDecl *block_decl =
      m_clang.CreateBlockDeclaration(scope, OptionalClangModuleID());
  m_uid_to_decl.insert({toOpaqueUid(block_id), block_decl});

  DeclStatus status;
  status.resolved = true;
  status.uid = toOpaqueUid(block_id);
  m_decl_to_status.insert({block_decl, status});

  return block_decl;
}

clang::VarDecl *PdbAstBuilder::CreateVariableDecl(PdbSymUid uid, CVSymbol sym,
                                                  clang::DeclContext &scope) {
  VariableInfo var_info = GetVariableNameInfo(sym);
  clang::QualType qt = GetOrCreateType(var_info.type);
  if (qt.isNull())
    return nullptr;

  clang::VarDecl *var_decl = m_clang.CreateVariableDeclaration(
      &scope, OptionalClangModuleID(), var_info.name.str().c_str(), qt);

  m_uid_to_decl[toOpaqueUid(uid)] = var_decl;
  DeclStatus status;
  status.resolved = true;
  status.uid = toOpaqueUid(uid);
  m_decl_to_status.insert({var_decl, status});
  return var_decl;
}

clang::VarDecl *
PdbAstBuilder::GetOrCreateVariableDecl(PdbCompilandSymId scope_id,
                                       PdbCompilandSymId var_id) {
  if (clang::Decl *decl = TryGetDecl(var_id))
    return llvm::dyn_cast<clang::VarDecl>(decl);

  clang::DeclContext *scope = GetOrCreateDeclContextForUid(scope_id);
  if (!scope)
    return nullptr;

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol sym = index.ReadSymbolRecord(var_id);
  return CreateVariableDecl(PdbSymUid(var_id), sym, *scope);
}

clang::VarDecl *PdbAstBuilder::GetOrCreateVariableDecl(PdbGlobalSymId var_id) {
  if (clang::Decl *decl = TryGetDecl(var_id))
    return llvm::dyn_cast<clang::VarDecl>(decl);

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol sym = index.ReadSymbolRecord(var_id);
  auto context = FromCompilerDeclContext(GetTranslationUnitDecl());
  return CreateVariableDecl(PdbSymUid(var_id), sym, *context);
}

clang::TypedefNameDecl *
PdbAstBuilder::GetOrCreateTypedefDecl(PdbGlobalSymId id) {
  if (clang::Decl *decl = TryGetDecl(id))
    return llvm::dyn_cast<clang::TypedefNameDecl>(decl);

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol sym = index.ReadSymbolRecord(id);
  lldbassert(sym.kind() == S_UDT);
  UDTSym udt = llvm::cantFail(SymbolDeserializer::deserializeAs<UDTSym>(sym));

  clang::DeclContext *scope = GetParentDeclContext(id);

  PdbTypeSymId real_type_id{udt.Type, false};
  clang::QualType qt = GetOrCreateType(real_type_id);
  if (qt.isNull() || !scope)
    return nullptr;

  std::string uname = std::string(DropNameScope(udt.Name));

  CompilerType ct = ToCompilerType(qt).CreateTypedef(
      uname.c_str(), ToCompilerDeclContext(*scope), 0);
  clang::TypedefNameDecl *tnd = m_clang.GetAsTypedefDecl(ct);
  DeclStatus status;
  status.resolved = true;
  status.uid = toOpaqueUid(id);
  m_decl_to_status.insert({tnd, status});
  return tnd;
}

clang::QualType PdbAstBuilder::GetBasicType(lldb::BasicType type) {
  CompilerType ct = m_clang.GetBasicType(type);
  return clang::QualType::getFromOpaquePtr(ct.GetOpaqueQualType());
}

clang::QualType PdbAstBuilder::CreateType(PdbTypeSymId type) {
  if (type.index.isSimple())
    return CreateSimpleType(type.index);

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVType cvt = index.tpi().getType(type.index);

  if (cvt.kind() == LF_MODIFIER) {
    ModifierRecord modifier;
    llvm::cantFail(
        TypeDeserializer::deserializeAs<ModifierRecord>(cvt, modifier));
    return CreateModifierType(modifier);
  }

  if (cvt.kind() == LF_POINTER) {
    PointerRecord pointer;
    llvm::cantFail(
        TypeDeserializer::deserializeAs<PointerRecord>(cvt, pointer));
    return CreatePointerType(pointer);
  }

  if (IsTagRecord(cvt)) {
    CVTagRecord tag = CVTagRecord::create(cvt);
    if (tag.kind() == CVTagRecord::Union)
      return CreateRecordType(type.index, tag.asUnion());
    if (tag.kind() == CVTagRecord::Enum)
      return CreateEnumType(type.index, tag.asEnum());
    return CreateRecordType(type.index, tag.asClass());
  }

  if (cvt.kind() == LF_ARRAY) {
    ArrayRecord ar;
    llvm::cantFail(TypeDeserializer::deserializeAs<ArrayRecord>(cvt, ar));
    return CreateArrayType(ar);
  }

  if (cvt.kind() == LF_PROCEDURE) {
    ProcedureRecord pr;
    llvm::cantFail(TypeDeserializer::deserializeAs<ProcedureRecord>(cvt, pr));
    return CreateFunctionType(pr.ArgumentList, pr.ReturnType, pr.CallConv);
  }

  if (cvt.kind() == LF_MFUNCTION) {
    MemberFunctionRecord mfr;
    llvm::cantFail(
        TypeDeserializer::deserializeAs<MemberFunctionRecord>(cvt, mfr));
    return CreateFunctionType(mfr.ArgumentList, mfr.ReturnType, mfr.CallConv);
  }

  return {};
}

clang::QualType PdbAstBuilder::GetOrCreateType(PdbTypeSymId type) {
  if (type.index.isNoneType())
    return {};

  lldb::user_id_t uid = toOpaqueUid(type);
  auto iter = m_uid_to_type.find(uid);
  if (iter != m_uid_to_type.end())
    return iter->second;

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  PdbTypeSymId best_type = GetBestPossibleDecl(type, index.tpi());

  clang::QualType qt;
  if (best_type.index != type.index) {
    // This is a forward decl.  Call GetOrCreate on the full decl, then map the
    // forward decl id to the full decl QualType.
    clang::QualType qt = GetOrCreateType(best_type);
    if (qt.isNull())
      return {};
    m_uid_to_type[toOpaqueUid(type)] = qt;
    return qt;
  }

  // This is either a full decl, or a forward decl with no matching full decl
  // in the debug info.
  qt = CreateType(type);
  if (qt.isNull())
    return {};

  m_uid_to_type[toOpaqueUid(type)] = qt;
  if (IsTagRecord(type, index.tpi())) {
    clang::TagDecl *tag = qt->getAsTagDecl();
    lldbassert(m_decl_to_status.count(tag) == 0);

    DeclStatus &status = m_decl_to_status[tag];
    status.uid = uid;
    status.resolved = false;
  }
  return qt;
}

clang::FunctionDecl *
PdbAstBuilder::CreateFunctionDecl(PdbCompilandSymId func_id,
                                  llvm::StringRef func_name, TypeIndex func_ti,
                                  CompilerType func_ct, uint32_t param_count,
                                  clang::StorageClass func_storage,
                                  bool is_inline, clang::DeclContext *parent) {
  clang::FunctionDecl *function_decl = nullptr;
  if (parent->isRecord()) {
    SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
        m_clang.GetSymbolFile()->GetBackingSymbolFile());
    PdbIndex &index = pdb->GetIndex();
    clang::QualType parent_qt = llvm::cast<clang::TypeDecl>(parent)
                                    ->getTypeForDecl()
                                    ->getCanonicalTypeInternal();
    lldb::opaque_compiler_type_t parent_opaque_ty =
        ToCompilerType(parent_qt).GetOpaqueQualType();
    // FIXME: Remove this workaround.
    auto iter = m_cxx_record_map.find(parent_opaque_ty);
    if (iter != m_cxx_record_map.end()) {
      if (iter->getSecond().contains({func_name, func_ct})) {
        return nullptr;
      }
    }

    CVType cvt = index.tpi().getType(func_ti);
    MemberFunctionRecord func_record(static_cast<TypeRecordKind>(cvt.kind()));
    llvm::cantFail(TypeDeserializer::deserializeAs<MemberFunctionRecord>(
        cvt, func_record));
    TypeIndex class_index = func_record.getClassType();

    CVType parent_cvt = index.tpi().getType(class_index);
    TagRecord tag_record = CVTagRecord::create(parent_cvt).asTag();
    // If it's a forward reference, try to get the real TypeIndex.
    if (tag_record.isForwardRef()) {
      llvm::Expected<TypeIndex> eti =
          index.tpi().findFullDeclForForwardRef(class_index);
      if (eti) {
        tag_record = CVTagRecord::create(index.tpi().getType(*eti)).asTag();
      }
    }
    if (!tag_record.FieldList.isSimple()) {
      CVType field_list_cvt = index.tpi().getType(tag_record.FieldList);
      FieldListRecord field_list;
      if (llvm::Error error = TypeDeserializer::deserializeAs<FieldListRecord>(
              field_list_cvt, field_list))
        llvm::consumeError(std::move(error));
      CreateMethodDecl process(index, m_clang, func_ti, function_decl,
                               parent_opaque_ty, func_name, func_ct);
      if (llvm::Error err = visitMemberRecordStream(field_list.Data, process))
        llvm::consumeError(std::move(err));
    }

    if (!function_decl) {
      function_decl = m_clang.AddMethodToCXXRecordType(
          parent_opaque_ty, func_name,
          /*mangled_name=*/nullptr, func_ct,
          /*access=*/lldb::AccessType::eAccessPublic,
          /*is_virtual=*/false, /*is_static=*/false,
          /*is_inline=*/false, /*is_explicit=*/false,
          /*is_attr_used=*/false, /*is_artificial=*/false);
    }
    m_cxx_record_map[parent_opaque_ty].insert({func_name, func_ct});
  } else {
    function_decl = m_clang.CreateFunctionDeclaration(
        parent, OptionalClangModuleID(), func_name, func_ct, func_storage,
        is_inline);
    CreateFunctionParameters(func_id, *function_decl, param_count);
  }
  return function_decl;
}

clang::FunctionDecl *
PdbAstBuilder::GetOrCreateInlinedFunctionDecl(PdbCompilandSymId inlinesite_id) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CompilandIndexItem *cii =
      index.compilands().GetCompiland(inlinesite_id.modi);
  CVSymbol sym = cii->m_debug_stream.readSymbolAtOffset(inlinesite_id.offset);
  InlineSiteSym inline_site(static_cast<SymbolRecordKind>(sym.kind()));
  cantFail(SymbolDeserializer::deserializeAs<InlineSiteSym>(sym, inline_site));

  // Inlinee is the id index to the function id record that is inlined.
  PdbTypeSymId func_id(inline_site.Inlinee, true);
  // Look up the function decl by the id index to see if we have created a
  // function decl for a different inlinesite that refers the same function.
  if (clang::Decl *decl = TryGetDecl(func_id))
    return llvm::dyn_cast<clang::FunctionDecl>(decl);
  clang::FunctionDecl *function_decl =
      CreateFunctionDeclFromId(func_id, inlinesite_id);
  if (function_decl == nullptr)
    return nullptr;

  // Use inline site id in m_decl_to_status because it's expected to be a
  // PdbCompilandSymId so that we can parse local variables info after it.
  uint64_t inlinesite_uid = toOpaqueUid(inlinesite_id);
  DeclStatus status;
  status.resolved = true;
  status.uid = inlinesite_uid;
  m_decl_to_status.insert({function_decl, status});
  // Use the index in IPI stream as uid in m_uid_to_decl, because index in IPI
  // stream are unique and there could be multiple inline sites (different ids)
  // referring the same inline function. This avoid creating multiple same
  // inline function delcs.
  uint64_t func_uid = toOpaqueUid(func_id);
  lldbassert(m_uid_to_decl.count(func_uid) == 0);
  m_uid_to_decl[func_uid] = function_decl;
  return function_decl;
}

clang::FunctionDecl *
PdbAstBuilder::CreateFunctionDeclFromId(PdbTypeSymId func_tid,
                                        PdbCompilandSymId func_sid) {
  lldbassert(func_tid.is_ipi);
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVType func_cvt = index.ipi().getType(func_tid.index);
  llvm::StringRef func_name;
  TypeIndex func_ti;
  clang::DeclContext *parent = nullptr;
  switch (func_cvt.kind()) {
  case LF_MFUNC_ID: {
    MemberFuncIdRecord mfr;
    cantFail(
        TypeDeserializer::deserializeAs<MemberFuncIdRecord>(func_cvt, mfr));
    func_name = mfr.getName();
    func_ti = mfr.getFunctionType();
    PdbTypeSymId class_type_id(mfr.ClassType, false);
    parent = GetOrCreateDeclContextForUid(class_type_id);
    break;
  }
  case LF_FUNC_ID: {
    FuncIdRecord fir;
    cantFail(TypeDeserializer::deserializeAs<FuncIdRecord>(func_cvt, fir));
    func_name = fir.getName();
    func_ti = fir.getFunctionType();
    parent = FromCompilerDeclContext(GetTranslationUnitDecl());
    if (!fir.ParentScope.isNoneType()) {
      CVType parent_cvt = index.ipi().getType(fir.ParentScope);
      if (parent_cvt.kind() == LF_STRING_ID) {
        StringIdRecord sir;
        cantFail(
            TypeDeserializer::deserializeAs<StringIdRecord>(parent_cvt, sir));
        parent = GetOrCreateNamespaceDecl(sir.String.data(), *parent);
      }
    }
    break;
  }
  default:
    lldbassert(false && "Invalid function id type!");
  }
  clang::QualType func_qt = GetOrCreateType(func_ti);
  if (func_qt.isNull() || !parent)
    return nullptr;
  CompilerType func_ct = ToCompilerType(func_qt);
  uint32_t param_count =
      llvm::cast<clang::FunctionProtoType>(func_qt)->getNumParams();
  return CreateFunctionDecl(func_sid, func_name, func_ti, func_ct, param_count,
                            clang::SC_None, true, parent);
}

clang::FunctionDecl *
PdbAstBuilder::GetOrCreateFunctionDecl(PdbCompilandSymId func_id) {
  if (clang::Decl *decl = TryGetDecl(func_id))
    return llvm::dyn_cast<clang::FunctionDecl>(decl);

  clang::DeclContext *parent = GetParentDeclContext(PdbSymUid(func_id));
  if (!parent)
    return nullptr;
  std::string context_name;
  if (clang::NamespaceDecl *ns = llvm::dyn_cast<clang::NamespaceDecl>(parent)) {
    context_name = ns->getQualifiedNameAsString();
  } else if (clang::TagDecl *tag = llvm::dyn_cast<clang::TagDecl>(parent)) {
    context_name = tag->getQualifiedNameAsString();
  }

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol cvs = index.ReadSymbolRecord(func_id);
  ProcSym proc(static_cast<SymbolRecordKind>(cvs.kind()));
  llvm::cantFail(SymbolDeserializer::deserializeAs<ProcSym>(cvs, proc));

  PdbTypeSymId type_id(proc.FunctionType);
  clang::QualType qt = GetOrCreateType(type_id);
  if (qt.isNull())
    return nullptr;

  clang::StorageClass storage = clang::SC_None;
  if (proc.Kind == SymbolRecordKind::ProcSym)
    storage = clang::SC_Static;

  const clang::FunctionProtoType *func_type =
      llvm::dyn_cast<clang::FunctionProtoType>(qt);

  CompilerType func_ct = ToCompilerType(qt);

  llvm::StringRef proc_name = proc.Name;
  proc_name.consume_front(context_name);
  proc_name.consume_front("::");
  clang::FunctionDecl *function_decl =
      CreateFunctionDecl(func_id, proc_name, proc.FunctionType, func_ct,
                         func_type->getNumParams(), storage, false, parent);
  if (function_decl == nullptr)
    return nullptr;

  lldbassert(m_uid_to_decl.count(toOpaqueUid(func_id)) == 0);
  m_uid_to_decl[toOpaqueUid(func_id)] = function_decl;
  DeclStatus status;
  status.resolved = true;
  status.uid = toOpaqueUid(func_id);
  m_decl_to_status.insert({function_decl, status});

  return function_decl;
}

void PdbAstBuilder::CreateFunctionParameters(PdbCompilandSymId func_id,
                                             clang::FunctionDecl &function_decl,
                                             uint32_t param_count) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CompilandIndexItem *cii = index.compilands().GetCompiland(func_id.modi);
  CVSymbolArray scope =
      cii->m_debug_stream.getSymbolArrayForScope(func_id.offset);

  scope.drop_front();
  auto begin = scope.begin();
  auto end = scope.end();
  std::vector<clang::ParmVarDecl *> params;
  for (uint32_t i = 0; i < param_count && begin != end;) {
    uint32_t record_offset = begin.offset();
    CVSymbol sym = *begin++;

    TypeIndex param_type;
    llvm::StringRef param_name;
    switch (sym.kind()) {
    case S_REGREL32: {
      RegRelativeSym reg(SymbolRecordKind::RegRelativeSym);
      cantFail(SymbolDeserializer::deserializeAs<RegRelativeSym>(sym, reg));
      param_type = reg.Type;
      param_name = reg.Name;
      break;
    }
    case S_REGISTER: {
      RegisterSym reg(SymbolRecordKind::RegisterSym);
      cantFail(SymbolDeserializer::deserializeAs<RegisterSym>(sym, reg));
      param_type = reg.Index;
      param_name = reg.Name;
      break;
    }
    case S_LOCAL: {
      LocalSym local(SymbolRecordKind::LocalSym);
      cantFail(SymbolDeserializer::deserializeAs<LocalSym>(sym, local));
      if ((local.Flags & LocalSymFlags::IsParameter) == LocalSymFlags::None)
        continue;
      param_type = local.Type;
      param_name = local.Name;
      break;
    }
    case S_BLOCK32:
    case S_INLINESITE:
    case S_INLINESITE2:
      // All parameters should come before the first block/inlinesite.  If that
      // isn't the case, then perhaps this is bad debug info that doesn't
      // contain information about all parameters.
      return;
    default:
      continue;
    }

    PdbCompilandSymId param_uid(func_id.modi, record_offset);
    clang::QualType qt = GetOrCreateType(param_type);
    if (qt.isNull())
      return;

    CompilerType param_type_ct = m_clang.GetType(qt);
    clang::ParmVarDecl *param = m_clang.CreateParameterDeclaration(
        &function_decl, OptionalClangModuleID(), param_name.str().c_str(),
        param_type_ct, clang::SC_None, true);
    lldbassert(m_uid_to_decl.count(toOpaqueUid(param_uid)) == 0);

    m_uid_to_decl[toOpaqueUid(param_uid)] = param;
    params.push_back(param);
    ++i;
  }

  if (!params.empty() && params.size() == param_count)
    m_clang.SetFunctionParameters(&function_decl, params);
}

clang::QualType PdbAstBuilder::CreateEnumType(PdbTypeSymId id,
                                              const EnumRecord &er) {
  clang::DeclContext *decl_context = nullptr;
  std::string uname;
  std::tie(decl_context, uname) = CreateDeclInfoForType(er, id.index);
  if (!decl_context)
    return {};

  clang::QualType underlying_type = GetOrCreateType(er.UnderlyingType);
  if (underlying_type.isNull())
    return {};

  Declaration declaration;
  CompilerType enum_ct = m_clang.CreateEnumerationType(
      uname, decl_context, OptionalClangModuleID(), declaration,
      ToCompilerType(underlying_type), er.isScoped());

  TypeSystemClang::StartTagDeclarationDefinition(enum_ct);
  TypeSystemClang::SetHasExternalStorage(enum_ct.GetOpaqueQualType(), true);

  return clang::QualType::getFromOpaquePtr(enum_ct.GetOpaqueQualType());
}

clang::QualType PdbAstBuilder::CreateArrayType(const ArrayRecord &ar) {
  clang::QualType element_type = GetOrCreateType(ar.ElementType);

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  uint64_t element_size = GetSizeOfType({ar.ElementType}, index.tpi());
  if (element_type.isNull() || element_size == 0)
    return {};
  uint64_t element_count = ar.Size / element_size;

  CompilerType array_ct = m_clang.CreateArrayType(ToCompilerType(element_type),
                                                  element_count, false);
  return clang::QualType::getFromOpaquePtr(array_ct.GetOpaqueQualType());
}

clang::QualType PdbAstBuilder::CreateFunctionType(
    TypeIndex args_type_idx, TypeIndex return_type_idx,
    llvm::codeview::CallingConvention calling_convention) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  TpiStream &stream = index.tpi();
  CVType args_cvt = stream.getType(args_type_idx);
  ArgListRecord args;
  llvm::cantFail(
      TypeDeserializer::deserializeAs<ArgListRecord>(args_cvt, args));

  llvm::ArrayRef<TypeIndex> arg_indices = llvm::ArrayRef(args.ArgIndices);
  bool is_variadic = IsCVarArgsFunction(arg_indices);
  if (is_variadic)
    arg_indices = arg_indices.drop_back();

  std::vector<CompilerType> arg_types;
  arg_types.reserve(arg_indices.size());

  for (TypeIndex arg_index : arg_indices) {
    clang::QualType arg_type = GetOrCreateType(arg_index);
    if (arg_type.isNull())
      continue;
    arg_types.push_back(ToCompilerType(arg_type));
  }

  clang::QualType return_type = GetOrCreateType(return_type_idx);
  if (return_type.isNull())
    return {};

  std::optional<clang::CallingConv> cc =
      TranslateCallingConvention(calling_convention);
  if (!cc)
    return {};

  CompilerType return_ct = ToCompilerType(return_type);
  CompilerType func_sig_ast_type = m_clang.CreateFunctionType(
      return_ct, arg_types.data(), arg_types.size(), is_variadic, 0, *cc);

  return clang::QualType::getFromOpaquePtr(
      func_sig_ast_type.GetOpaqueQualType());
}

static bool isTagDecl(clang::DeclContext &context) {
  return llvm::isa<clang::TagDecl>(&context);
}

static bool isFunctionDecl(clang::DeclContext &context) {
  return llvm::isa<clang::FunctionDecl>(&context);
}

static bool isBlockDecl(clang::DeclContext &context) {
  return llvm::isa<clang::BlockDecl>(&context);
}

void PdbAstBuilder::ParseNamespace(clang::DeclContext &context) {
  clang::NamespaceDecl *ns = llvm::dyn_cast<clang::NamespaceDecl>(&context);
  if (m_parsed_namespaces.contains(ns))
    return;
  std::string qname = ns->getQualifiedNameAsString();
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  TypeIndex ti{index.tpi().TypeIndexBegin()};
  for (const CVType &cvt : index.tpi().typeArray()) {
    PdbTypeSymId tid{ti};
    ++ti;

    if (!IsTagRecord(cvt))
      continue;

    CVTagRecord tag = CVTagRecord::create(cvt);

    // Call CreateDeclInfoForType unconditionally so that the namespace info
    // gets created.  But only call CreateRecordType if the namespace name
    // matches.
    clang::DeclContext *context = nullptr;
    std::string uname;
    std::tie(context, uname) = CreateDeclInfoForType(tag.asTag(), tid.index);
    if (!context || !context->isNamespace())
      continue;

    clang::NamespaceDecl *ns = llvm::cast<clang::NamespaceDecl>(context);
    llvm::StringRef ns_name = ns->getName();
    if (ns_name.starts_with(qname)) {
      ns_name = ns_name.drop_front(qname.size());
      if (ns_name.starts_with("::"))
        GetOrCreateType(tid);
    }
  }
  ParseAllFunctionsAndNonLocalVars();
  m_parsed_namespaces.insert(ns);
}

void PdbAstBuilder::ParseAllTypes() {
  llvm::call_once(m_parse_all_types, [this]() {
    SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
        m_clang.GetSymbolFile()->GetBackingSymbolFile());
    PdbIndex &index = pdb->GetIndex();
    TypeIndex ti{index.tpi().TypeIndexBegin()};
    for (const CVType &cvt : index.tpi().typeArray()) {
      PdbTypeSymId tid{ti};
      ++ti;

      if (!IsTagRecord(cvt))
        continue;

      GetOrCreateType(tid);
    }
  });
}

void PdbAstBuilder::ParseAllFunctionsAndNonLocalVars() {
  llvm::call_once(m_parse_functions_and_non_local_vars, [this]() {
    SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
        m_clang.GetSymbolFile()->GetBackingSymbolFile());
    PdbIndex &index = pdb->GetIndex();
    uint32_t module_count = index.dbi().modules().getModuleCount();
    for (uint16_t modi = 0; modi < module_count; ++modi) {
      CompilandIndexItem &cii = index.compilands().GetOrCreateCompiland(modi);
      const CVSymbolArray &symbols = cii.m_debug_stream.getSymbolArray();
      auto iter = symbols.begin();
      while (iter != symbols.end()) {
        PdbCompilandSymId sym_id{modi, iter.offset()};

        switch (iter->kind()) {
        case S_GPROC32:
        case S_LPROC32:
          GetOrCreateFunctionDecl(sym_id);
          iter = symbols.at(getScopeEndOffset(*iter));
          break;
        case S_GDATA32:
        case S_GTHREAD32:
        case S_LDATA32:
        case S_LTHREAD32:
          GetOrCreateVariableDecl(PdbCompilandSymId(modi, 0), sym_id);
          ++iter;
          break;
        default:
          ++iter;
          continue;
        }
      }
    }
  });
}

static CVSymbolArray skipFunctionParameters(clang::Decl &decl,
                                            const CVSymbolArray &symbols) {
  clang::FunctionDecl *func_decl = llvm::dyn_cast<clang::FunctionDecl>(&decl);
  if (!func_decl)
    return symbols;
  unsigned int params = func_decl->getNumParams();
  if (params == 0)
    return symbols;

  CVSymbolArray result = symbols;

  while (!result.empty()) {
    if (params == 0)
      return result;

    CVSymbol sym = *result.begin();
    result.drop_front();

    if (!isLocalVariableType(sym.kind()))
      continue;

    --params;
  }
  return result;
}

void PdbAstBuilder::ParseBlockChildren(PdbCompilandSymId block_id) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol sym = index.ReadSymbolRecord(block_id);
  lldbassert(sym.kind() == S_GPROC32 || sym.kind() == S_LPROC32 ||
             sym.kind() == S_BLOCK32 || sym.kind() == S_INLINESITE);
  CompilandIndexItem &cii =
      index.compilands().GetOrCreateCompiland(block_id.modi);
  CVSymbolArray symbols =
      cii.m_debug_stream.getSymbolArrayForScope(block_id.offset);

  // Function parameters should already have been created when the function was
  // parsed.
  if (sym.kind() == S_GPROC32 || sym.kind() == S_LPROC32)
    symbols =
        skipFunctionParameters(*m_uid_to_decl[toOpaqueUid(block_id)], symbols);

  symbols.drop_front();
  auto begin = symbols.begin();
  while (begin != symbols.end()) {
    PdbCompilandSymId child_sym_id(block_id.modi, begin.offset());
    GetOrCreateSymbolForId(child_sym_id);
    if (begin->kind() == S_BLOCK32 || begin->kind() == S_INLINESITE) {
      ParseBlockChildren(child_sym_id);
      begin = symbols.at(getScopeEndOffset(*begin));
    }
    ++begin;
  }
}

void PdbAstBuilder::ParseDeclsForSimpleContext(clang::DeclContext &context) {

  clang::Decl *decl = clang::Decl::castFromDeclContext(&context);
  lldbassert(decl);

  auto iter = m_decl_to_status.find(decl);
  lldbassert(iter != m_decl_to_status.end());

  if (auto *tag = llvm::dyn_cast<clang::TagDecl>(&context)) {
    CompleteTagDecl(*tag);
    return;
  }

  if (isFunctionDecl(context) || isBlockDecl(context)) {
    PdbCompilandSymId block_id = PdbSymUid(iter->second.uid).asCompilandSym();
    ParseBlockChildren(block_id);
  }
}

void PdbAstBuilder::ParseDeclsForContext(clang::DeclContext &context) {
  // Namespaces aren't explicitly represented in the debug info, and the only
  // way to parse them is to parse all type info, demangling every single type
  // and trying to reconstruct the DeclContext hierarchy this way.  Since this
  // is an expensive operation, we have to special case it so that we do other
  // work (such as parsing the items that appear within the namespaces) at the
  // same time.
  if (context.isTranslationUnit()) {
    ParseAllTypes();
    ParseAllFunctionsAndNonLocalVars();
    return;
  }

  if (context.isNamespace()) {
    ParseNamespace(context);
    return;
  }

  if (isTagDecl(context) || isFunctionDecl(context) || isBlockDecl(context)) {
    ParseDeclsForSimpleContext(context);
    return;
  }
}

CompilerDecl PdbAstBuilder::ToCompilerDecl(clang::Decl &decl) {
  return m_clang.GetCompilerDecl(&decl);
}

CompilerType PdbAstBuilder::ToCompilerType(clang::QualType qt) {
  return {m_clang.weak_from_this(), qt.getAsOpaquePtr()};
}

CompilerDeclContext
PdbAstBuilder::ToCompilerDeclContext(clang::DeclContext &context) {
  return m_clang.CreateDeclContext(&context);
}

clang::Decl * PdbAstBuilder::FromCompilerDecl(CompilerDecl decl) {
  return ClangUtil::GetDecl(decl);
}

clang::DeclContext *
PdbAstBuilder::FromCompilerDeclContext(CompilerDeclContext context) {
  return static_cast<clang::DeclContext *>(context.GetOpaqueDeclContext());
}

void PdbAstBuilder::Dump(Stream &stream) {
  m_clang.Dump(stream.AsRawOstream());
}
