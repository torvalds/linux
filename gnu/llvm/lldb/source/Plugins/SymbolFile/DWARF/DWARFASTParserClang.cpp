//===-- DWARFASTParserClang.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdlib>

#include "DWARFASTParser.h"
#include "DWARFASTParserClang.h"
#include "DWARFDebugInfo.h"
#include "DWARFDeclContext.h"
#include "DWARFDefines.h"
#include "SymbolFileDWARF.h"
#include "SymbolFileDWARFDebugMap.h"
#include "SymbolFileDWARFDwo.h"
#include "UniqueDWARFASTType.h"

#include "Plugins/ExpressionParser/Clang/ClangASTImporter.h"
#include "Plugins/ExpressionParser/Clang/ClangASTMetadata.h"
#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/Language/ObjC/ObjCLanguage.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Demangle/Demangle.h"

#include <map>
#include <memory>
#include <optional>
#include <vector>

//#define ENABLE_DEBUG_PRINTF // COMMENT OUT THIS LINE PRIOR TO CHECKIN

#ifdef ENABLE_DEBUG_PRINTF
#include <cstdio>
#define DEBUG_PRINTF(fmt, ...) printf(fmt, __VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::dwarf;
using namespace lldb_private::plugin::dwarf;

DWARFASTParserClang::DWARFASTParserClang(TypeSystemClang &ast)
    : DWARFASTParser(Kind::DWARFASTParserClang), m_ast(ast),
      m_die_to_decl_ctx(), m_decl_ctx_to_die() {}

DWARFASTParserClang::~DWARFASTParserClang() = default;

static bool DeclKindIsCXXClass(clang::Decl::Kind decl_kind) {
  switch (decl_kind) {
  case clang::Decl::CXXRecord:
  case clang::Decl::ClassTemplateSpecialization:
    return true;
  default:
    break;
  }
  return false;
}


ClangASTImporter &DWARFASTParserClang::GetClangASTImporter() {
  if (!m_clang_ast_importer_up) {
    m_clang_ast_importer_up = std::make_unique<ClangASTImporter>();
  }
  return *m_clang_ast_importer_up;
}

/// Detect a forward declaration that is nested in a DW_TAG_module.
static bool IsClangModuleFwdDecl(const DWARFDIE &Die) {
  if (!Die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0))
    return false;
  auto Parent = Die.GetParent();
  while (Parent.IsValid()) {
    if (Parent.Tag() == DW_TAG_module)
      return true;
    Parent = Parent.GetParent();
  }
  return false;
}

static DWARFDIE GetContainingClangModuleDIE(const DWARFDIE &die) {
  if (die.IsValid()) {
    DWARFDIE top_module_die;
    // Now make sure this DIE is scoped in a DW_TAG_module tag and return true
    // if so
    for (DWARFDIE parent = die.GetParent(); parent.IsValid();
         parent = parent.GetParent()) {
      const dw_tag_t tag = parent.Tag();
      if (tag == DW_TAG_module)
        top_module_die = parent;
      else if (tag == DW_TAG_compile_unit || tag == DW_TAG_partial_unit)
        break;
    }

    return top_module_die;
  }
  return DWARFDIE();
}

static lldb::ModuleSP GetContainingClangModule(const DWARFDIE &die) {
  if (die.IsValid()) {
    DWARFDIE clang_module_die = GetContainingClangModuleDIE(die);

    if (clang_module_die) {
      const char *module_name = clang_module_die.GetName();
      if (module_name)
        return die.GetDWARF()->GetExternalModule(
            lldb_private::ConstString(module_name));
    }
  }
  return lldb::ModuleSP();
}

// Returns true if the given artificial field name should be ignored when
// parsing the DWARF.
static bool ShouldIgnoreArtificialField(llvm::StringRef FieldName) {
  return FieldName.starts_with("_vptr$")
         // gdb emit vtable pointer as "_vptr.classname"
         || FieldName.starts_with("_vptr.");
}

/// Returns true for C++ constructs represented by clang::CXXRecordDecl
static bool TagIsRecordType(dw_tag_t tag) {
  switch (tag) {
  case DW_TAG_class_type:
  case DW_TAG_structure_type:
  case DW_TAG_union_type:
    return true;
  default:
    return false;
  }
}

TypeSP DWARFASTParserClang::ParseTypeFromClangModule(const SymbolContext &sc,
                                                     const DWARFDIE &die,
                                                     Log *log) {
  ModuleSP clang_module_sp = GetContainingClangModule(die);
  if (!clang_module_sp)
    return TypeSP();

  // If this type comes from a Clang module, recursively look in the
  // DWARF section of the .pcm file in the module cache. Clang
  // generates DWO skeleton units as breadcrumbs to find them.
  std::vector<lldb_private::CompilerContext> die_context = die.GetDeclContext();
  TypeQuery query(die_context, TypeQueryOptions::e_module_search |
                                   TypeQueryOptions::e_find_one);
  TypeResults results;

  // The type in the Clang module must have the same language as the current CU.
  query.AddLanguage(SymbolFileDWARF::GetLanguageFamily(*die.GetCU()));
  clang_module_sp->FindTypes(query, results);
  TypeSP pcm_type_sp = results.GetTypeMap().FirstType();
  if (!pcm_type_sp) {
    // Since this type is defined in one of the Clang modules imported
    // by this symbol file, search all of them. Instead of calling
    // sym_file->FindTypes(), which would return this again, go straight
    // to the imported modules.
    auto &sym_file = die.GetCU()->GetSymbolFileDWARF();

    // Well-formed clang modules never form cycles; guard against corrupted
    // ones by inserting the current file.
    results.AlreadySearched(&sym_file);
    sym_file.ForEachExternalModule(
        *sc.comp_unit, results.GetSearchedSymbolFiles(), [&](Module &module) {
          module.FindTypes(query, results);
          pcm_type_sp = results.GetTypeMap().FirstType();
          return (bool)pcm_type_sp;
        });
  }

  if (!pcm_type_sp)
    return TypeSP();

  // We found a real definition for this type in the Clang module, so lets use
  // it and cache the fact that we found a complete type for this die.
  lldb_private::CompilerType pcm_type = pcm_type_sp->GetForwardCompilerType();
  lldb_private::CompilerType type =
      GetClangASTImporter().CopyType(m_ast, pcm_type);

  if (!type)
    return TypeSP();

  // Under normal operation pcm_type is a shallow forward declaration
  // that gets completed later. This is necessary to support cyclic
  // data structures. If, however, pcm_type is already complete (for
  // example, because it was loaded for a different target before),
  // the definition needs to be imported right away, too.
  // Type::ResolveClangType() effectively ignores the ResolveState
  // inside type_sp and only looks at IsDefined(), so it never calls
  // ClangASTImporter::ASTImporterDelegate::ImportDefinitionTo(),
  // which does extra work for Objective-C classes. This would result
  // in only the forward declaration to be visible.
  if (pcm_type.IsDefined())
    GetClangASTImporter().RequireCompleteType(ClangUtil::GetQualType(type));

  SymbolFileDWARF *dwarf = die.GetDWARF();
  auto type_sp = dwarf->MakeType(
      die.GetID(), pcm_type_sp->GetName(), pcm_type_sp->GetByteSize(nullptr),
      nullptr, LLDB_INVALID_UID, Type::eEncodingInvalid,
      &pcm_type_sp->GetDeclaration(), type, Type::ResolveState::Forward,
      TypePayloadClang(GetOwningClangModule(die)));
  clang::TagDecl *tag_decl = TypeSystemClang::GetAsTagDecl(type);
  if (tag_decl) {
    LinkDeclContextToDIE(tag_decl, die);
  } else {
    clang::DeclContext *defn_decl_ctx = GetCachedClangDeclContextForDIE(die);
    if (defn_decl_ctx)
      LinkDeclContextToDIE(defn_decl_ctx, die);
  }

  return type_sp;
}

/// This function ensures we are able to add members (nested types, functions,
/// etc.) to this type. It does so by starting its definition even if one cannot
/// be found in the debug info. This means the type may need to be "forcibly
/// completed" later -- see CompleteTypeFromDWARF).
static void PrepareContextToReceiveMembers(TypeSystemClang &ast,
                                           ClangASTImporter &ast_importer,
                                           clang::DeclContext *decl_ctx,
                                           DWARFDIE die,
                                           const char *type_name_cstr) {
  auto *tag_decl_ctx = clang::dyn_cast<clang::TagDecl>(decl_ctx);
  if (!tag_decl_ctx)
    return; // Non-tag context are always ready.

  // We have already completed the type or it is already prepared.
  if (tag_decl_ctx->isCompleteDefinition() || tag_decl_ctx->isBeingDefined())
    return;

  // If this tag was imported from another AST context (in the gmodules case),
  // we can complete the type by doing a full import.

  // If this type was not imported from an external AST, there's nothing to do.
  CompilerType type = ast.GetTypeForDecl(tag_decl_ctx);
  if (type && ast_importer.CanImport(type)) {
    auto qual_type = ClangUtil::GetQualType(type);
    if (ast_importer.RequireCompleteType(qual_type))
      return;
    die.GetDWARF()->GetObjectFile()->GetModule()->ReportError(
        "Unable to complete the Decl context for DIE {0} at offset "
        "{1:x16}.\nPlease file a bug report.",
        type_name_cstr ? type_name_cstr : "", die.GetOffset());
  }

  // We don't have a type definition and/or the import failed, but we need to
  // add members to it. Start the definition to make that possible. If the type
  // has no external storage we also have to complete the definition. Otherwise,
  // that will happen when we are asked to complete the type
  // (CompleteTypeFromDWARF).
  ast.StartTagDeclarationDefinition(type);
  if (!tag_decl_ctx->hasExternalLexicalStorage()) {
    ast.SetDeclIsForcefullyCompleted(tag_decl_ctx);
    ast.CompleteTagDeclarationDefinition(type);
  }
}

ParsedDWARFTypeAttributes::ParsedDWARFTypeAttributes(const DWARFDIE &die) {
  DWARFAttributes attributes = die.GetAttributes();
  for (size_t i = 0; i < attributes.Size(); ++i) {
    dw_attr_t attr = attributes.AttributeAtIndex(i);
    DWARFFormValue form_value;
    if (!attributes.ExtractFormValueAtIndex(i, form_value))
      continue;
    switch (attr) {
    default:
      break;
    case DW_AT_abstract_origin:
      abstract_origin = form_value;
      break;

    case DW_AT_accessibility:
      accessibility =
          DWARFASTParser::GetAccessTypeFromDWARF(form_value.Unsigned());
      break;

    case DW_AT_artificial:
      is_artificial = form_value.Boolean();
      break;

    case DW_AT_bit_stride:
      bit_stride = form_value.Unsigned();
      break;

    case DW_AT_byte_size:
      byte_size = form_value.Unsigned();
      break;

    case DW_AT_alignment:
      alignment = form_value.Unsigned();
      break;

    case DW_AT_byte_stride:
      byte_stride = form_value.Unsigned();
      break;

    case DW_AT_calling_convention:
      calling_convention = form_value.Unsigned();
      break;

    case DW_AT_containing_type:
      containing_type = form_value;
      break;

    case DW_AT_decl_file:
      // die.GetCU() can differ if DW_AT_specification uses DW_FORM_ref_addr.
      decl.SetFile(
          attributes.CompileUnitAtIndex(i)->GetFile(form_value.Unsigned()));
      break;
    case DW_AT_decl_line:
      decl.SetLine(form_value.Unsigned());
      break;
    case DW_AT_decl_column:
      decl.SetColumn(form_value.Unsigned());
      break;

    case DW_AT_declaration:
      is_forward_declaration = form_value.Boolean();
      break;

    case DW_AT_encoding:
      encoding = form_value.Unsigned();
      break;

    case DW_AT_enum_class:
      is_scoped_enum = form_value.Boolean();
      break;

    case DW_AT_explicit:
      is_explicit = form_value.Boolean();
      break;

    case DW_AT_external:
      if (form_value.Unsigned())
        storage = clang::SC_Extern;
      break;

    case DW_AT_inline:
      is_inline = form_value.Boolean();
      break;

    case DW_AT_linkage_name:
    case DW_AT_MIPS_linkage_name:
      mangled_name = form_value.AsCString();
      break;

    case DW_AT_name:
      name.SetCString(form_value.AsCString());
      break;

    case DW_AT_object_pointer:
      object_pointer = form_value.Reference();
      break;

    case DW_AT_signature:
      signature = form_value;
      break;

    case DW_AT_specification:
      specification = form_value;
      break;

    case DW_AT_type:
      type = form_value;
      break;

    case DW_AT_virtuality:
      is_virtual = form_value.Boolean();
      break;

    case DW_AT_APPLE_objc_complete_type:
      is_complete_objc_class = form_value.Signed();
      break;

    case DW_AT_APPLE_objc_direct:
      is_objc_direct_call = true;
      break;

    case DW_AT_APPLE_runtime_class:
      class_language = (LanguageType)form_value.Signed();
      break;

    case DW_AT_GNU_vector:
      is_vector = form_value.Boolean();
      break;
    case DW_AT_export_symbols:
      exports_symbols = form_value.Boolean();
      break;
    case DW_AT_rvalue_reference:
      ref_qual = clang::RQ_RValue;
      break;
    case DW_AT_reference:
      ref_qual = clang::RQ_LValue;
      break;
    }
  }
}

static std::string GetUnitName(const DWARFDIE &die) {
  if (DWARFUnit *unit = die.GetCU())
    return unit->GetAbsolutePath().GetPath();
  return "<missing DWARF unit path>";
}

TypeSP DWARFASTParserClang::ParseTypeFromDWARF(const SymbolContext &sc,
                                               const DWARFDIE &die,
                                               bool *type_is_new_ptr) {
  if (type_is_new_ptr)
    *type_is_new_ptr = false;

  if (!die)
    return nullptr;

  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);

  SymbolFileDWARF *dwarf = die.GetDWARF();
  if (log) {
    DWARFDIE context_die;
    clang::DeclContext *context =
        GetClangDeclContextContainingDIE(die, &context_die);

    dwarf->GetObjectFile()->GetModule()->LogMessage(
        log,
        "DWARFASTParserClang::ParseTypeFromDWARF "
        "(die = {0:x16}, decl_ctx = {1:p} (die "
        "{2:x16})) {3} ({4}) name = '{5}')",
        die.GetOffset(), static_cast<void *>(context), context_die.GetOffset(),
        DW_TAG_value_to_name(die.Tag()), die.Tag(), die.GetName());
  }

  // Set a bit that lets us know that we are currently parsing this
  if (auto [it, inserted] =
          dwarf->GetDIEToType().try_emplace(die.GetDIE(), DIE_IS_BEING_PARSED);
      !inserted) {
    if (it->getSecond() == nullptr || it->getSecond() == DIE_IS_BEING_PARSED)
      return nullptr;
    return it->getSecond()->shared_from_this();
  }

  ParsedDWARFTypeAttributes attrs(die);

  TypeSP type_sp;
  if (DWARFDIE signature_die = attrs.signature.Reference()) {
    type_sp = ParseTypeFromDWARF(sc, signature_die, type_is_new_ptr);
    if (type_sp) {
      if (clang::DeclContext *decl_ctx =
              GetCachedClangDeclContextForDIE(signature_die))
        LinkDeclContextToDIE(decl_ctx, die);
    }
  } else {
    if (type_is_new_ptr)
      *type_is_new_ptr = true;

    const dw_tag_t tag = die.Tag();

    switch (tag) {
    case DW_TAG_typedef:
    case DW_TAG_base_type:
    case DW_TAG_pointer_type:
    case DW_TAG_reference_type:
    case DW_TAG_rvalue_reference_type:
    case DW_TAG_const_type:
    case DW_TAG_restrict_type:
    case DW_TAG_volatile_type:
    case DW_TAG_LLVM_ptrauth_type:
    case DW_TAG_atomic_type:
    case DW_TAG_unspecified_type:
      type_sp = ParseTypeModifier(sc, die, attrs);
      break;
    case DW_TAG_structure_type:
    case DW_TAG_union_type:
    case DW_TAG_class_type:
      type_sp = ParseStructureLikeDIE(sc, die, attrs);
      break;
    case DW_TAG_enumeration_type:
      type_sp = ParseEnum(sc, die, attrs);
      break;
    case DW_TAG_inlined_subroutine:
    case DW_TAG_subprogram:
    case DW_TAG_subroutine_type:
      type_sp = ParseSubroutine(die, attrs);
      break;
    case DW_TAG_array_type:
      type_sp = ParseArrayType(die, attrs);
      break;
    case DW_TAG_ptr_to_member_type:
      type_sp = ParsePointerToMemberType(die, attrs);
      break;
    default:
      dwarf->GetObjectFile()->GetModule()->ReportError(
          "[{0:x16}]: unhandled type tag {1:x4} ({2}), "
          "please file a bug and "
          "attach the file at the start of this error message",
          die.GetOffset(), tag, DW_TAG_value_to_name(tag));
      break;
    }
    UpdateSymbolContextScopeForType(sc, die, type_sp);
  }
  if (type_sp) {
    dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
  }
  return type_sp;
}

static std::optional<uint32_t>
ExtractDataMemberLocation(DWARFDIE const &die, DWARFFormValue const &form_value,
                          ModuleSP module_sp) {
  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);

  // With DWARF 3 and later, if the value is an integer constant,
  // this form value is the offset in bytes from the beginning of
  // the containing entity.
  if (!form_value.BlockData())
    return form_value.Unsigned();

  Value initialValue(0);
  const DWARFDataExtractor &debug_info_data = die.GetData();
  uint32_t block_length = form_value.Unsigned();
  uint32_t block_offset =
      form_value.BlockData() - debug_info_data.GetDataStart();

  llvm::Expected<Value> memberOffset = DWARFExpression::Evaluate(
      /*ExecutionContext=*/nullptr,
      /*RegisterContext=*/nullptr, module_sp,
      DataExtractor(debug_info_data, block_offset, block_length), die.GetCU(),
      eRegisterKindDWARF, &initialValue, nullptr);
  if (!memberOffset) {
    LLDB_LOG_ERROR(log, memberOffset.takeError(),
                   "ExtractDataMemberLocation failed: {0}");
    return {};
  }

  return memberOffset->ResolveValue(nullptr).UInt();
}

static TypePayloadClang GetPtrAuthMofidierPayload(const DWARFDIE &die) {
  auto getAttr = [&](llvm::dwarf::Attribute Attr, unsigned defaultValue = 0) {
    return die.GetAttributeValueAsUnsigned(Attr, defaultValue);
  };
  const unsigned key = getAttr(DW_AT_LLVM_ptrauth_key);
  const bool addr_disc = getAttr(DW_AT_LLVM_ptrauth_address_discriminated);
  const unsigned extra = getAttr(DW_AT_LLVM_ptrauth_extra_discriminator);
  const bool isapointer = getAttr(DW_AT_LLVM_ptrauth_isa_pointer);
  const bool authenticates_null_values =
      getAttr(DW_AT_LLVM_ptrauth_authenticates_null_values);
  const unsigned authentication_mode_int = getAttr(
      DW_AT_LLVM_ptrauth_authentication_mode,
      static_cast<unsigned>(clang::PointerAuthenticationMode::SignAndAuth));
  clang::PointerAuthenticationMode authentication_mode =
      clang::PointerAuthenticationMode::SignAndAuth;
  if (authentication_mode_int >=
          static_cast<unsigned>(clang::PointerAuthenticationMode::None) &&
      authentication_mode_int <=
          static_cast<unsigned>(
              clang::PointerAuthenticationMode::SignAndAuth)) {
    authentication_mode =
        static_cast<clang::PointerAuthenticationMode>(authentication_mode_int);
  } else {
    die.GetDWARF()->GetObjectFile()->GetModule()->ReportError(
        "[{0:x16}]: invalid pointer authentication mode method {1:x4}",
        die.GetOffset(), authentication_mode_int);
  }
  auto ptr_auth = clang::PointerAuthQualifier::Create(
      key, addr_disc, extra, authentication_mode, isapointer,
      authenticates_null_values);
  return TypePayloadClang(ptr_auth.getAsOpaqueValue());
}

lldb::TypeSP
DWARFASTParserClang::ParseTypeModifier(const SymbolContext &sc,
                                       const DWARFDIE &die,
                                       ParsedDWARFTypeAttributes &attrs) {
  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);
  SymbolFileDWARF *dwarf = die.GetDWARF();
  const dw_tag_t tag = die.Tag();
  LanguageType cu_language = SymbolFileDWARF::GetLanguage(*die.GetCU());
  Type::ResolveState resolve_state = Type::ResolveState::Unresolved;
  Type::EncodingDataType encoding_data_type = Type::eEncodingIsUID;
  TypePayloadClang payload(GetOwningClangModule(die));
  TypeSP type_sp;
  CompilerType clang_type;

  if (tag == DW_TAG_typedef) {
    // DeclContext will be populated when the clang type is materialized in
    // Type::ResolveCompilerType.
    PrepareContextToReceiveMembers(
        m_ast, GetClangASTImporter(),
        GetClangDeclContextContainingDIE(die, nullptr), die,
        attrs.name.GetCString());

    if (attrs.type.IsValid()) {
      // Try to parse a typedef from the (DWARF embedded in the) Clang
      // module file first as modules can contain typedef'ed
      // structures that have no names like:
      //
      //  typedef struct { int a; } Foo;
      //
      // In this case we will have a structure with no name and a
      // typedef named "Foo" that points to this unnamed
      // structure. The name in the typedef is the only identifier for
      // the struct, so always try to get typedefs from Clang modules
      // if possible.
      //
      // The type_sp returned will be empty if the typedef doesn't
      // exist in a module file, so it is cheap to call this function
      // just to check.
      //
      // If we don't do this we end up creating a TypeSP that says
      // this is a typedef to type 0x123 (the DW_AT_type value would
      // be 0x123 in the DW_TAG_typedef), and this is the unnamed
      // structure type. We will have a hard time tracking down an
      // unnammed structure type in the module debug info, so we make
      // sure we don't get into this situation by always resolving
      // typedefs from the module.
      const DWARFDIE encoding_die = attrs.type.Reference();

      // First make sure that the die that this is typedef'ed to _is_
      // just a declaration (DW_AT_declaration == 1), not a full
      // definition since template types can't be represented in
      // modules since only concrete instances of templates are ever
      // emitted and modules won't contain those
      if (encoding_die &&
          encoding_die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0) == 1) {
        type_sp = ParseTypeFromClangModule(sc, die, log);
        if (type_sp)
          return type_sp;
      }
    }
  }

  DEBUG_PRINTF("0x%8.8" PRIx64 ": %s (\"%s\") type => 0x%8.8lx\n", die.GetID(),
               DW_TAG_value_to_name(tag), type_name_cstr,
               encoding_uid.Reference());

  switch (tag) {
  default:
    break;

  case DW_TAG_unspecified_type:
    if (attrs.name == "nullptr_t" || attrs.name == "decltype(nullptr)") {
      resolve_state = Type::ResolveState::Full;
      clang_type = m_ast.GetBasicType(eBasicTypeNullPtr);
      break;
    }
    // Fall through to base type below in case we can handle the type
    // there...
    [[fallthrough]];

  case DW_TAG_base_type:
    resolve_state = Type::ResolveState::Full;
    clang_type = m_ast.GetBuiltinTypeForDWARFEncodingAndBitSize(
        attrs.name.GetStringRef(), attrs.encoding,
        attrs.byte_size.value_or(0) * 8);
    break;

  case DW_TAG_pointer_type:
    encoding_data_type = Type::eEncodingIsPointerUID;
    break;
  case DW_TAG_reference_type:
    encoding_data_type = Type::eEncodingIsLValueReferenceUID;
    break;
  case DW_TAG_rvalue_reference_type:
    encoding_data_type = Type::eEncodingIsRValueReferenceUID;
    break;
  case DW_TAG_typedef:
    encoding_data_type = Type::eEncodingIsTypedefUID;
    break;
  case DW_TAG_const_type:
    encoding_data_type = Type::eEncodingIsConstUID;
    break;
  case DW_TAG_restrict_type:
    encoding_data_type = Type::eEncodingIsRestrictUID;
    break;
  case DW_TAG_volatile_type:
    encoding_data_type = Type::eEncodingIsVolatileUID;
    break;
  case DW_TAG_LLVM_ptrauth_type:
    encoding_data_type = Type::eEncodingIsLLVMPtrAuthUID;
    payload = GetPtrAuthMofidierPayload(die);
    break;
  case DW_TAG_atomic_type:
    encoding_data_type = Type::eEncodingIsAtomicUID;
    break;
  }

  if (!clang_type && (encoding_data_type == Type::eEncodingIsPointerUID ||
                      encoding_data_type == Type::eEncodingIsTypedefUID)) {
    if (tag == DW_TAG_pointer_type) {
      DWARFDIE target_die = die.GetReferencedDIE(DW_AT_type);

      if (target_die.GetAttributeValueAsUnsigned(DW_AT_APPLE_block, 0)) {
        // Blocks have a __FuncPtr inside them which is a pointer to a
        // function of the proper type.

        for (DWARFDIE child_die : target_die.children()) {
          if (!strcmp(child_die.GetAttributeValueAsString(DW_AT_name, ""),
                      "__FuncPtr")) {
            DWARFDIE function_pointer_type =
                child_die.GetReferencedDIE(DW_AT_type);

            if (function_pointer_type) {
              DWARFDIE function_type =
                  function_pointer_type.GetReferencedDIE(DW_AT_type);

              bool function_type_is_new_pointer;
              TypeSP lldb_function_type_sp = ParseTypeFromDWARF(
                  sc, function_type, &function_type_is_new_pointer);

              if (lldb_function_type_sp) {
                clang_type = m_ast.CreateBlockPointerType(
                    lldb_function_type_sp->GetForwardCompilerType());
                encoding_data_type = Type::eEncodingIsUID;
                attrs.type.Clear();
                resolve_state = Type::ResolveState::Full;
              }
            }

            break;
          }
        }
      }
    }

    if (cu_language == eLanguageTypeObjC ||
        cu_language == eLanguageTypeObjC_plus_plus) {
      if (attrs.name) {
        if (attrs.name == "id") {
          if (log)
            dwarf->GetObjectFile()->GetModule()->LogMessage(
                log,
                "SymbolFileDWARF::ParseType (die = {0:x16}) {1} ({2}) '{3}' "
                "is Objective-C 'id' built-in type.",
                die.GetOffset(), DW_TAG_value_to_name(die.Tag()), die.Tag(),
                die.GetName());
          clang_type = m_ast.GetBasicType(eBasicTypeObjCID);
          encoding_data_type = Type::eEncodingIsUID;
          attrs.type.Clear();
          resolve_state = Type::ResolveState::Full;
        } else if (attrs.name == "Class") {
          if (log)
            dwarf->GetObjectFile()->GetModule()->LogMessage(
                log,
                "SymbolFileDWARF::ParseType (die = {0:x16}) {1} ({2}) '{3}' "
                "is Objective-C 'Class' built-in type.",
                die.GetOffset(), DW_TAG_value_to_name(die.Tag()), die.Tag(),
                die.GetName());
          clang_type = m_ast.GetBasicType(eBasicTypeObjCClass);
          encoding_data_type = Type::eEncodingIsUID;
          attrs.type.Clear();
          resolve_state = Type::ResolveState::Full;
        } else if (attrs.name == "SEL") {
          if (log)
            dwarf->GetObjectFile()->GetModule()->LogMessage(
                log,
                "SymbolFileDWARF::ParseType (die = {0:x16}) {1} ({2}) '{3}' "
                "is Objective-C 'selector' built-in type.",
                die.GetOffset(), DW_TAG_value_to_name(die.Tag()), die.Tag(),
                die.GetName());
          clang_type = m_ast.GetBasicType(eBasicTypeObjCSel);
          encoding_data_type = Type::eEncodingIsUID;
          attrs.type.Clear();
          resolve_state = Type::ResolveState::Full;
        }
      } else if (encoding_data_type == Type::eEncodingIsPointerUID &&
                 attrs.type.IsValid()) {
        // Clang sometimes erroneously emits id as objc_object*.  In that
        // case we fix up the type to "id".

        const DWARFDIE encoding_die = attrs.type.Reference();

        if (encoding_die && encoding_die.Tag() == DW_TAG_structure_type) {
          llvm::StringRef struct_name = encoding_die.GetName();
          if (struct_name == "objc_object") {
            if (log)
              dwarf->GetObjectFile()->GetModule()->LogMessage(
                  log,
                  "SymbolFileDWARF::ParseType (die = {0:x16}) {1} ({2}) '{3}' "
                  "is 'objc_object*', which we overrode to 'id'.",
                  die.GetOffset(), DW_TAG_value_to_name(die.Tag()), die.Tag(),
                  die.GetName());
            clang_type = m_ast.GetBasicType(eBasicTypeObjCID);
            encoding_data_type = Type::eEncodingIsUID;
            attrs.type.Clear();
            resolve_state = Type::ResolveState::Full;
          }
        }
      }
    }
  }

  return dwarf->MakeType(die.GetID(), attrs.name, attrs.byte_size, nullptr,
                         attrs.type.Reference().GetID(), encoding_data_type,
                         &attrs.decl, clang_type, resolve_state, payload);
}

std::string
DWARFASTParserClang::GetDIEClassTemplateParams(const DWARFDIE &die) {
  if (llvm::StringRef(die.GetName()).contains("<"))
    return {};

  TypeSystemClang::TemplateParameterInfos template_param_infos;
  if (ParseTemplateParameterInfos(die, template_param_infos))
    return m_ast.PrintTemplateParams(template_param_infos);

  return {};
}

void DWARFASTParserClang::MapDeclDIEToDefDIE(
    const lldb_private::plugin::dwarf::DWARFDIE &decl_die,
    const lldb_private::plugin::dwarf::DWARFDIE &def_die) {
  LinkDeclContextToDIE(GetCachedClangDeclContextForDIE(decl_die), def_die);
  SymbolFileDWARF *dwarf = def_die.GetDWARF();
  ParsedDWARFTypeAttributes decl_attrs(decl_die);
  ParsedDWARFTypeAttributes def_attrs(def_die);
  ConstString unique_typename(decl_attrs.name);
  Declaration decl_declaration(decl_attrs.decl);
  GetUniqueTypeNameAndDeclaration(
      decl_die, SymbolFileDWARF::GetLanguage(*decl_die.GetCU()),
      unique_typename, decl_declaration);
  if (UniqueDWARFASTType *unique_ast_entry_type =
          dwarf->GetUniqueDWARFASTTypeMap().Find(
              unique_typename, decl_die, decl_declaration,
              decl_attrs.byte_size.value_or(0),
              decl_attrs.is_forward_declaration)) {
    unique_ast_entry_type->UpdateToDefDIE(def_die, def_attrs.decl,
                                          def_attrs.byte_size.value_or(0));
  } else if (Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups)) {
    const dw_tag_t tag = decl_die.Tag();
    LLDB_LOG(log,
             "Failed to find {0:x16} {1} ({2}) type \"{3}\" in "
             "UniqueDWARFASTTypeMap",
             decl_die.GetID(), DW_TAG_value_to_name(tag), tag, unique_typename);
  }
}

TypeSP DWARFASTParserClang::ParseEnum(const SymbolContext &sc,
                                      const DWARFDIE &decl_die,
                                      ParsedDWARFTypeAttributes &attrs) {
  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);
  SymbolFileDWARF *dwarf = decl_die.GetDWARF();
  const dw_tag_t tag = decl_die.Tag();

  DWARFDIE def_die;
  if (attrs.is_forward_declaration) {
    if (TypeSP type_sp = ParseTypeFromClangModule(sc, decl_die, log))
      return type_sp;

    def_die = dwarf->FindDefinitionDIE(decl_die);

    if (!def_die) {
      SymbolFileDWARFDebugMap *debug_map_symfile = dwarf->GetDebugMapSymfile();
      if (debug_map_symfile) {
        // We weren't able to find a full declaration in this DWARF,
        // see if we have a declaration anywhere else...
        def_die = debug_map_symfile->FindDefinitionDIE(decl_die);
      }
    }

    if (log) {
      dwarf->GetObjectFile()->GetModule()->LogMessage(
          log,
          "SymbolFileDWARF({0:p}) - {1:x16}}: {2} ({3}) type \"{4}\" is a "
          "forward declaration, complete DIE is {5}",
          static_cast<void *>(this), decl_die.GetID(), DW_TAG_value_to_name(tag),
          tag, attrs.name.GetCString(),
          def_die ? llvm::utohexstr(def_die.GetID()) : "not found");
    }
  }
  if (def_die) {
    if (auto [it, inserted] = dwarf->GetDIEToType().try_emplace(
            def_die.GetDIE(), DIE_IS_BEING_PARSED);
        !inserted) {
      if (it->getSecond() == nullptr || it->getSecond() == DIE_IS_BEING_PARSED)
        return nullptr;
      return it->getSecond()->shared_from_this();
    }
    attrs = ParsedDWARFTypeAttributes(def_die);
  } else {
    // No definition found. Proceed with the declaration die. We can use it to
    // create a forward-declared type.
    def_die = decl_die;
  }

  CompilerType enumerator_clang_type;
  if (attrs.type.IsValid()) {
    Type *enumerator_type =
        dwarf->ResolveTypeUID(attrs.type.Reference(), true);
    if (enumerator_type)
      enumerator_clang_type = enumerator_type->GetFullCompilerType();
  }

  if (!enumerator_clang_type) {
    if (attrs.byte_size) {
      enumerator_clang_type = m_ast.GetBuiltinTypeForDWARFEncodingAndBitSize(
          "", DW_ATE_signed, *attrs.byte_size * 8);
    } else {
      enumerator_clang_type = m_ast.GetBasicType(eBasicTypeInt);
    }
  }

  CompilerType clang_type = m_ast.CreateEnumerationType(
      attrs.name.GetStringRef(), GetClangDeclContextContainingDIE(def_die, nullptr),
      GetOwningClangModule(def_die), attrs.decl, enumerator_clang_type,
      attrs.is_scoped_enum);
  TypeSP type_sp =
      dwarf->MakeType(def_die.GetID(), attrs.name, attrs.byte_size, nullptr,
                      attrs.type.Reference().GetID(), Type::eEncodingIsUID,
                      &attrs.decl, clang_type, Type::ResolveState::Forward,
                      TypePayloadClang(GetOwningClangModule(def_die)));

  clang::DeclContext *type_decl_ctx =
      TypeSystemClang::GetDeclContextForType(clang_type);
  LinkDeclContextToDIE(type_decl_ctx, decl_die);
  if (decl_die != def_die) {
    LinkDeclContextToDIE(type_decl_ctx, def_die);
    dwarf->GetDIEToType()[def_die.GetDIE()] = type_sp.get();
    // Declaration DIE is inserted into the type map in ParseTypeFromDWARF
  }


  if (TypeSystemClang::StartTagDeclarationDefinition(clang_type)) {
    if (def_die.HasChildren()) {
      bool is_signed = false;
      enumerator_clang_type.IsIntegerType(is_signed);
      ParseChildEnumerators(clang_type, is_signed,
                            type_sp->GetByteSize(nullptr).value_or(0), def_die);
    }
    TypeSystemClang::CompleteTagDeclarationDefinition(clang_type);
  } else {
    dwarf->GetObjectFile()->GetModule()->ReportError(
        "DWARF DIE at {0:x16} named \"{1}\" was not able to start its "
        "definition.\nPlease file a bug and attach the file at the "
        "start of this error message",
        def_die.GetOffset(), attrs.name.GetCString());
  }
  return type_sp;
}

static clang::CallingConv
ConvertDWARFCallingConventionToClang(const ParsedDWARFTypeAttributes &attrs) {
  switch (attrs.calling_convention) {
  case llvm::dwarf::DW_CC_normal:
    return clang::CC_C;
  case llvm::dwarf::DW_CC_BORLAND_stdcall:
    return clang::CC_X86StdCall;
  case llvm::dwarf::DW_CC_BORLAND_msfastcall:
    return clang::CC_X86FastCall;
  case llvm::dwarf::DW_CC_LLVM_vectorcall:
    return clang::CC_X86VectorCall;
  case llvm::dwarf::DW_CC_BORLAND_pascal:
    return clang::CC_X86Pascal;
  case llvm::dwarf::DW_CC_LLVM_Win64:
    return clang::CC_Win64;
  case llvm::dwarf::DW_CC_LLVM_X86_64SysV:
    return clang::CC_X86_64SysV;
  case llvm::dwarf::DW_CC_LLVM_X86RegCall:
    return clang::CC_X86RegCall;
  default:
    break;
  }

  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);
  LLDB_LOG(log, "Unsupported DW_AT_calling_convention value: {0}",
           attrs.calling_convention);
  // Use the default calling convention as a fallback.
  return clang::CC_C;
}

bool DWARFASTParserClang::ParseObjCMethod(
    const ObjCLanguage::MethodName &objc_method, const DWARFDIE &die,
    CompilerType clang_type, const ParsedDWARFTypeAttributes &attrs,
    bool is_variadic) {
  SymbolFileDWARF *dwarf = die.GetDWARF();
  assert(dwarf);

  const auto tag = die.Tag();
  ConstString class_name(objc_method.GetClassName());
  if (!class_name)
    return false;

  TypeSP complete_objc_class_type_sp =
      dwarf->FindCompleteObjCDefinitionTypeForDIE(DWARFDIE(), class_name,
                                                  false);

  if (!complete_objc_class_type_sp)
    return false;

  CompilerType type_clang_forward_type =
      complete_objc_class_type_sp->GetForwardCompilerType();

  if (!type_clang_forward_type)
    return false;

  if (!TypeSystemClang::IsObjCObjectOrInterfaceType(type_clang_forward_type))
    return false;

  clang::ObjCMethodDecl *objc_method_decl = m_ast.AddMethodToObjCObjectType(
      type_clang_forward_type, attrs.name.GetCString(), clang_type,
      attrs.is_artificial, is_variadic, attrs.is_objc_direct_call);

  if (!objc_method_decl) {
    dwarf->GetObjectFile()->GetModule()->ReportError(
        "[{0:x16}]: invalid Objective-C method {1:x4} ({2}), "
        "please file a bug and attach the file at the start of "
        "this error message",
        die.GetOffset(), tag, DW_TAG_value_to_name(tag));
    return false;
  }

  LinkDeclContextToDIE(objc_method_decl, die);
  m_ast.SetMetadataAsUserID(objc_method_decl, die.GetID());

  return true;
}

std::pair<bool, TypeSP> DWARFASTParserClang::ParseCXXMethod(
    const DWARFDIE &die, CompilerType clang_type,
    const ParsedDWARFTypeAttributes &attrs, const DWARFDIE &decl_ctx_die,
    bool is_static, bool &ignore_containing_context) {
  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);
  SymbolFileDWARF *dwarf = die.GetDWARF();
  assert(dwarf);

  Type *class_type = dwarf->ResolveType(decl_ctx_die);
  if (!class_type)
    return {};

  if (class_type->GetID() != decl_ctx_die.GetID() ||
      IsClangModuleFwdDecl(decl_ctx_die)) {

    // We uniqued the parent class of this function to another
    // class so we now need to associate all dies under
    // "decl_ctx_die" to DIEs in the DIE for "class_type"...
    if (DWARFDIE class_type_die = dwarf->GetDIE(class_type->GetID())) {
      std::vector<DWARFDIE> failures;

      CopyUniqueClassMethodTypes(decl_ctx_die, class_type_die, class_type,
                                 failures);

      // FIXME do something with these failures that's
      // smarter than just dropping them on the ground.
      // Unfortunately classes don't like having stuff added
      // to them after their definitions are complete...

      Type *type_ptr = dwarf->GetDIEToType().lookup(die.GetDIE());
      if (type_ptr && type_ptr != DIE_IS_BEING_PARSED)
        return {true, type_ptr->shared_from_this()};
    }
  }

  if (attrs.specification.IsValid()) {
    // We have a specification which we are going to base our
    // function prototype off of, so we need this type to be
    // completed so that the m_die_to_decl_ctx for the method in
    // the specification has a valid clang decl context.
    class_type->GetForwardCompilerType();
    // If we have a specification, then the function type should
    // have been made with the specification and not with this
    // die.
    DWARFDIE spec_die = attrs.specification.Reference();
    clang::DeclContext *spec_clang_decl_ctx =
        GetClangDeclContextForDIE(spec_die);
    if (spec_clang_decl_ctx)
      LinkDeclContextToDIE(spec_clang_decl_ctx, die);
    else
      dwarf->GetObjectFile()->GetModule()->ReportWarning(
          "{0:x8}: DW_AT_specification({1:x16}"
          ") has no decl\n",
          die.GetID(), spec_die.GetOffset());

    return {true, nullptr};
  }

  if (attrs.abstract_origin.IsValid()) {
    // We have a specification which we are going to base our
    // function prototype off of, so we need this type to be
    // completed so that the m_die_to_decl_ctx for the method in
    // the abstract origin has a valid clang decl context.
    class_type->GetForwardCompilerType();

    DWARFDIE abs_die = attrs.abstract_origin.Reference();
    clang::DeclContext *abs_clang_decl_ctx = GetClangDeclContextForDIE(abs_die);
    if (abs_clang_decl_ctx)
      LinkDeclContextToDIE(abs_clang_decl_ctx, die);
    else
      dwarf->GetObjectFile()->GetModule()->ReportWarning(
          "{0:x8}: DW_AT_abstract_origin({1:x16}"
          ") has no decl\n",
          die.GetID(), abs_die.GetOffset());

    return {true, nullptr};
  }

  CompilerType class_opaque_type = class_type->GetForwardCompilerType();
  if (!TypeSystemClang::IsCXXClassType(class_opaque_type))
    return {};

  PrepareContextToReceiveMembers(
      m_ast, GetClangASTImporter(),
      TypeSystemClang::GetDeclContextForType(class_opaque_type), die,
      attrs.name.GetCString());

  // We have a C++ member function with no children (this pointer!) and clang
  // will get mad if we try and make a function that isn't well formed in the
  // DWARF, so we will just skip it...
  if (!is_static && !die.HasChildren())
    return {true, nullptr};

  const bool is_attr_used = false;
  // Neither GCC 4.2 nor clang++ currently set a valid
  // accessibility in the DWARF for C++ methods...
  // Default to public for now...
  const auto accessibility =
      attrs.accessibility == eAccessNone ? eAccessPublic : attrs.accessibility;

  clang::CXXMethodDecl *cxx_method_decl = m_ast.AddMethodToCXXRecordType(
      class_opaque_type.GetOpaqueQualType(), attrs.name.GetCString(),
      attrs.mangled_name, clang_type, accessibility, attrs.is_virtual,
      is_static, attrs.is_inline, attrs.is_explicit, is_attr_used,
      attrs.is_artificial);

  if (cxx_method_decl) {
    LinkDeclContextToDIE(cxx_method_decl, die);

    ClangASTMetadata metadata;
    metadata.SetUserID(die.GetID());

    char const *object_pointer_name =
        attrs.object_pointer ? attrs.object_pointer.GetName() : nullptr;
    if (object_pointer_name) {
      metadata.SetObjectPtrName(object_pointer_name);
      LLDB_LOGF(log, "Setting object pointer name: %s on method object %p.\n",
                object_pointer_name, static_cast<void *>(cxx_method_decl));
    }
    m_ast.SetMetadata(cxx_method_decl, metadata);
  } else {
    ignore_containing_context = true;
  }

  // Artificial methods are always handled even when we
  // don't create a new declaration for them.
  const bool type_handled = cxx_method_decl != nullptr || attrs.is_artificial;

  return {type_handled, nullptr};
}

TypeSP
DWARFASTParserClang::ParseSubroutine(const DWARFDIE &die,
                                     const ParsedDWARFTypeAttributes &attrs) {
  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);

  SymbolFileDWARF *dwarf = die.GetDWARF();
  const dw_tag_t tag = die.Tag();

  bool is_variadic = false;
  bool is_static = false;
  bool has_template_params = false;

  unsigned type_quals = 0;

  DEBUG_PRINTF("0x%8.8" PRIx64 ": %s (\"%s\")\n", die.GetID(),
               DW_TAG_value_to_name(tag), type_name_cstr);

  CompilerType return_clang_type;
  Type *func_type = nullptr;

  if (attrs.type.IsValid())
    func_type = dwarf->ResolveTypeUID(attrs.type.Reference(), true);

  if (func_type)
    return_clang_type = func_type->GetForwardCompilerType();
  else
    return_clang_type = m_ast.GetBasicType(eBasicTypeVoid);

  std::vector<CompilerType> function_param_types;
  std::vector<clang::ParmVarDecl *> function_param_decls;

  // Parse the function children for the parameters

  DWARFDIE decl_ctx_die;
  clang::DeclContext *containing_decl_ctx =
      GetClangDeclContextContainingDIE(die, &decl_ctx_die);
  const clang::Decl::Kind containing_decl_kind =
      containing_decl_ctx->getDeclKind();

  bool is_cxx_method = DeclKindIsCXXClass(containing_decl_kind);
  // Start off static. This will be set to false in
  // ParseChildParameters(...) if we find a "this" parameters as the
  // first parameter
  if (is_cxx_method) {
    is_static = true;
  }

  if (die.HasChildren()) {
    bool skip_artificial = true;
    ParseChildParameters(containing_decl_ctx, die, skip_artificial, is_static,
                         is_variadic, has_template_params,
                         function_param_types, function_param_decls,
                         type_quals);
  }

  bool ignore_containing_context = false;
  // Check for templatized class member functions. If we had any
  // DW_TAG_template_type_parameter or DW_TAG_template_value_parameter
  // the DW_TAG_subprogram DIE, then we can't let this become a method in
  // a class. Why? Because templatized functions are only emitted if one
  // of the templatized methods is used in the current compile unit and
  // we will end up with classes that may or may not include these member
  // functions and this means one class won't match another class
  // definition and it affects our ability to use a class in the clang
  // expression parser. So for the greater good, we currently must not
  // allow any template member functions in a class definition.
  if (is_cxx_method && has_template_params) {
    ignore_containing_context = true;
    is_cxx_method = false;
  }

  clang::CallingConv calling_convention =
      ConvertDWARFCallingConventionToClang(attrs);

  // clang_type will get the function prototype clang type after this
  // call
  CompilerType clang_type =
      m_ast.CreateFunctionType(return_clang_type, function_param_types.data(),
                               function_param_types.size(), is_variadic,
                               type_quals, calling_convention, attrs.ref_qual);

  if (attrs.name) {
    bool type_handled = false;
    if (tag == DW_TAG_subprogram || tag == DW_TAG_inlined_subroutine) {
      if (std::optional<const ObjCLanguage::MethodName> objc_method =
              ObjCLanguage::MethodName::Create(attrs.name.GetStringRef(),
                                               true)) {
        type_handled =
            ParseObjCMethod(*objc_method, die, clang_type, attrs, is_variadic);
      } else if (is_cxx_method) {
        auto [handled, type_sp] =
            ParseCXXMethod(die, clang_type, attrs, decl_ctx_die, is_static,
                           ignore_containing_context);
        if (type_sp)
          return type_sp;

        type_handled = handled;
      }
    }

    if (!type_handled) {
      clang::FunctionDecl *function_decl = nullptr;
      clang::FunctionDecl *template_function_decl = nullptr;

      if (attrs.abstract_origin.IsValid()) {
        DWARFDIE abs_die = attrs.abstract_origin.Reference();

        if (dwarf->ResolveType(abs_die)) {
          function_decl = llvm::dyn_cast_or_null<clang::FunctionDecl>(
              GetCachedClangDeclContextForDIE(abs_die));

          if (function_decl) {
            LinkDeclContextToDIE(function_decl, die);
          }
        }
      }

      if (!function_decl) {
        char *name_buf = nullptr;
        llvm::StringRef name = attrs.name.GetStringRef();

        // We currently generate function templates with template parameters in
        // their name. In order to get closer to the AST that clang generates
        // we want to strip these from the name when creating the AST.
        if (attrs.mangled_name) {
          llvm::ItaniumPartialDemangler D;
          if (!D.partialDemangle(attrs.mangled_name)) {
            name_buf = D.getFunctionBaseName(nullptr, nullptr);
            name = name_buf;
          }
        }

        // We just have a function that isn't part of a class
        function_decl = m_ast.CreateFunctionDeclaration(
            ignore_containing_context ? m_ast.GetTranslationUnitDecl()
                                      : containing_decl_ctx,
            GetOwningClangModule(die), name, clang_type, attrs.storage,
            attrs.is_inline);
        std::free(name_buf);

        if (has_template_params) {
          TypeSystemClang::TemplateParameterInfos template_param_infos;
          ParseTemplateParameterInfos(die, template_param_infos);
          template_function_decl = m_ast.CreateFunctionDeclaration(
              ignore_containing_context ? m_ast.GetTranslationUnitDecl()
                                        : containing_decl_ctx,
              GetOwningClangModule(die), attrs.name.GetStringRef(), clang_type,
              attrs.storage, attrs.is_inline);
          clang::FunctionTemplateDecl *func_template_decl =
              m_ast.CreateFunctionTemplateDecl(
                  containing_decl_ctx, GetOwningClangModule(die),
                  template_function_decl, template_param_infos);
          m_ast.CreateFunctionTemplateSpecializationInfo(
              template_function_decl, func_template_decl, template_param_infos);
        }

        lldbassert(function_decl);

        if (function_decl) {
          // Attach an asm(<mangled_name>) label to the FunctionDecl.
          // This ensures that clang::CodeGen emits function calls
          // using symbols that are mangled according to the DW_AT_linkage_name.
          // If we didn't do this, the external symbols wouldn't exactly
          // match the mangled name LLDB knows about and the IRExecutionUnit
          // would have to fall back to searching object files for
          // approximately matching function names. The motivating
          // example is generating calls to ABI-tagged template functions.
          // This is done separately for member functions in
          // AddMethodToCXXRecordType.
          if (attrs.mangled_name)
            function_decl->addAttr(clang::AsmLabelAttr::CreateImplicit(
                m_ast.getASTContext(), attrs.mangled_name, /*literal=*/false));

          LinkDeclContextToDIE(function_decl, die);

          if (!function_param_decls.empty()) {
            m_ast.SetFunctionParameters(function_decl, function_param_decls);
            if (template_function_decl)
              m_ast.SetFunctionParameters(template_function_decl,
                                          function_param_decls);
          }

          ClangASTMetadata metadata;
          metadata.SetUserID(die.GetID());

          char const *object_pointer_name =
              attrs.object_pointer ? attrs.object_pointer.GetName() : nullptr;
          if (object_pointer_name) {
            metadata.SetObjectPtrName(object_pointer_name);
            LLDB_LOGF(log,
                      "Setting object pointer name: %s on function "
                      "object %p.",
                      object_pointer_name, static_cast<void *>(function_decl));
          }
          m_ast.SetMetadata(function_decl, metadata);
        }
      }
    }
  }
  return dwarf->MakeType(
      die.GetID(), attrs.name, std::nullopt, nullptr, LLDB_INVALID_UID,
      Type::eEncodingIsUID, &attrs.decl, clang_type, Type::ResolveState::Full);
}

TypeSP
DWARFASTParserClang::ParseArrayType(const DWARFDIE &die,
                                    const ParsedDWARFTypeAttributes &attrs) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  DEBUG_PRINTF("0x%8.8" PRIx64 ": %s (\"%s\")\n", die.GetID(),
               DW_TAG_value_to_name(tag), type_name_cstr);

  DWARFDIE type_die = attrs.type.Reference();
  Type *element_type = dwarf->ResolveTypeUID(type_die, true);

  if (!element_type)
    return nullptr;

  std::optional<SymbolFile::ArrayInfo> array_info = ParseChildArrayInfo(die);
  uint32_t byte_stride = attrs.byte_stride;
  uint32_t bit_stride = attrs.bit_stride;
  if (array_info) {
    byte_stride = array_info->byte_stride;
    bit_stride = array_info->bit_stride;
  }
  if (byte_stride == 0 && bit_stride == 0)
    byte_stride = element_type->GetByteSize(nullptr).value_or(0);
  CompilerType array_element_type = element_type->GetForwardCompilerType();
  TypeSystemClang::RequireCompleteType(array_element_type);

  uint64_t array_element_bit_stride = byte_stride * 8 + bit_stride;
  CompilerType clang_type;
  if (array_info && array_info->element_orders.size() > 0) {
    uint64_t num_elements = 0;
    auto end = array_info->element_orders.rend();
    for (auto pos = array_info->element_orders.rbegin(); pos != end; ++pos) {
      num_elements = *pos;
      clang_type = m_ast.CreateArrayType(array_element_type, num_elements,
                                         attrs.is_vector);
      array_element_type = clang_type;
      array_element_bit_stride = num_elements
                                     ? array_element_bit_stride * num_elements
                                     : array_element_bit_stride;
    }
  } else {
    clang_type =
        m_ast.CreateArrayType(array_element_type, 0, attrs.is_vector);
  }
  ConstString empty_name;
  TypeSP type_sp =
      dwarf->MakeType(die.GetID(), empty_name, array_element_bit_stride / 8,
                      nullptr, type_die.GetID(), Type::eEncodingIsUID,
                      &attrs.decl, clang_type, Type::ResolveState::Full);
  type_sp->SetEncodingType(element_type);
  const clang::Type *type = ClangUtil::GetQualType(clang_type).getTypePtr();
  m_ast.SetMetadataAsUserID(type, die.GetID());
  return type_sp;
}

TypeSP DWARFASTParserClang::ParsePointerToMemberType(
    const DWARFDIE &die, const ParsedDWARFTypeAttributes &attrs) {
  SymbolFileDWARF *dwarf = die.GetDWARF();
  Type *pointee_type = dwarf->ResolveTypeUID(attrs.type.Reference(), true);
  Type *class_type =
      dwarf->ResolveTypeUID(attrs.containing_type.Reference(), true);

  // Check to make sure pointers are not NULL before attempting to
  // dereference them.
  if ((class_type == nullptr) || (pointee_type == nullptr))
    return nullptr;

  CompilerType pointee_clang_type = pointee_type->GetForwardCompilerType();
  CompilerType class_clang_type = class_type->GetForwardCompilerType();

  CompilerType clang_type = TypeSystemClang::CreateMemberPointerType(
      class_clang_type, pointee_clang_type);

  if (std::optional<uint64_t> clang_type_size =
          clang_type.GetByteSize(nullptr)) {
    return dwarf->MakeType(die.GetID(), attrs.name, *clang_type_size, nullptr,
                           LLDB_INVALID_UID, Type::eEncodingIsUID, nullptr,
                           clang_type, Type::ResolveState::Forward);
  }
  return nullptr;
}

void DWARFASTParserClang::ParseInheritance(
    const DWARFDIE &die, const DWARFDIE &parent_die,
    const CompilerType class_clang_type, const AccessType default_accessibility,
    const lldb::ModuleSP &module_sp,
    std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> &base_classes,
    ClangASTImporter::LayoutInfo &layout_info) {
  auto ast =
      class_clang_type.GetTypeSystem().dyn_cast_or_null<TypeSystemClang>();
  if (ast == nullptr)
    return;

  // TODO: implement DW_TAG_inheritance type parsing.
  DWARFAttributes attributes = die.GetAttributes();
  if (attributes.Size() == 0)
    return;

  DWARFFormValue encoding_form;
  AccessType accessibility = default_accessibility;
  bool is_virtual = false;
  bool is_base_of_class = true;
  off_t member_byte_offset = 0;

  for (uint32_t i = 0; i < attributes.Size(); ++i) {
    const dw_attr_t attr = attributes.AttributeAtIndex(i);
    DWARFFormValue form_value;
    if (attributes.ExtractFormValueAtIndex(i, form_value)) {
      switch (attr) {
      case DW_AT_type:
        encoding_form = form_value;
        break;
      case DW_AT_data_member_location:
        if (auto maybe_offset =
                ExtractDataMemberLocation(die, form_value, module_sp))
          member_byte_offset = *maybe_offset;
        break;

      case DW_AT_accessibility:
        accessibility =
            DWARFASTParser::GetAccessTypeFromDWARF(form_value.Unsigned());
        break;

      case DW_AT_virtuality:
        is_virtual = form_value.Boolean();
        break;

      default:
        break;
      }
    }
  }

  Type *base_class_type = die.ResolveTypeUID(encoding_form.Reference());
  if (base_class_type == nullptr) {
    module_sp->ReportError("{0:x16}: DW_TAG_inheritance failed to "
                           "resolve the base class at {1:x16}"
                           " from enclosing type {2:x16}. \nPlease file "
                           "a bug and attach the file at the start of "
                           "this error message",
                           die.GetOffset(),
                           encoding_form.Reference().GetOffset(),
                           parent_die.GetOffset());
    return;
  }

  CompilerType base_class_clang_type = base_class_type->GetFullCompilerType();
  assert(base_class_clang_type);
  if (TypeSystemClang::IsObjCObjectOrInterfaceType(class_clang_type)) {
    ast->SetObjCSuperClass(class_clang_type, base_class_clang_type);
    return;
  }
  std::unique_ptr<clang::CXXBaseSpecifier> result =
      ast->CreateBaseClassSpecifier(base_class_clang_type.GetOpaqueQualType(),
                                    accessibility, is_virtual,
                                    is_base_of_class);
  if (!result)
    return;

  base_classes.push_back(std::move(result));

  if (is_virtual) {
    // Do not specify any offset for virtual inheritance. The DWARF
    // produced by clang doesn't give us a constant offset, but gives
    // us a DWARF expressions that requires an actual object in memory.
    // the DW_AT_data_member_location for a virtual base class looks
    // like:
    //      DW_AT_data_member_location( DW_OP_dup, DW_OP_deref,
    //      DW_OP_constu(0x00000018), DW_OP_minus, DW_OP_deref,
    //      DW_OP_plus )
    // Given this, there is really no valid response we can give to
    // clang for virtual base class offsets, and this should eventually
    // be removed from LayoutRecordType() in the external
    // AST source in clang.
  } else {
    layout_info.base_offsets.insert(std::make_pair(
        ast->GetAsCXXRecordDecl(base_class_clang_type.GetOpaqueQualType()),
        clang::CharUnits::fromQuantity(member_byte_offset)));
  }
}

TypeSP DWARFASTParserClang::UpdateSymbolContextScopeForType(
    const SymbolContext &sc, const DWARFDIE &die, TypeSP type_sp) {
  if (!type_sp)
    return type_sp;

  DWARFDIE sc_parent_die = SymbolFileDWARF::GetParentSymbolContextDIE(die);
  dw_tag_t sc_parent_tag = sc_parent_die.Tag();

  SymbolContextScope *symbol_context_scope = nullptr;
  if (sc_parent_tag == DW_TAG_compile_unit ||
      sc_parent_tag == DW_TAG_partial_unit) {
    symbol_context_scope = sc.comp_unit;
  } else if (sc.function != nullptr && sc_parent_die) {
    symbol_context_scope =
        sc.function->GetBlock(true).FindBlockByID(sc_parent_die.GetID());
    if (symbol_context_scope == nullptr)
      symbol_context_scope = sc.function;
  } else {
    symbol_context_scope = sc.module_sp.get();
  }

  if (symbol_context_scope != nullptr)
    type_sp->SetSymbolContextScope(symbol_context_scope);
  return type_sp;
}

void DWARFASTParserClang::GetUniqueTypeNameAndDeclaration(
    const lldb_private::plugin::dwarf::DWARFDIE &die,
    lldb::LanguageType language, lldb_private::ConstString &unique_typename,
    lldb_private::Declaration &decl_declaration) {
  // For C++, we rely solely upon the one definition rule that says
  // only one thing can exist at a given decl context. We ignore the
  // file and line that things are declared on.
  if (!die.IsValid() || !Language::LanguageIsCPlusPlus(language) ||
      unique_typename.IsEmpty())
    return;
  decl_declaration.Clear();
  std::string qualified_name;
  DWARFDIE parent_decl_ctx_die = die.GetParentDeclContextDIE();
  // TODO: change this to get the correct decl context parent....
  while (parent_decl_ctx_die) {
    // The name may not contain template parameters due to
    // -gsimple-template-names; we must reconstruct the full name from child
    // template parameter dies via GetDIEClassTemplateParams().
    const dw_tag_t parent_tag = parent_decl_ctx_die.Tag();
    switch (parent_tag) {
    case DW_TAG_namespace: {
      if (const char *namespace_name = parent_decl_ctx_die.GetName()) {
        qualified_name.insert(0, "::");
        qualified_name.insert(0, namespace_name);
      } else {
        qualified_name.insert(0, "(anonymous namespace)::");
      }
      parent_decl_ctx_die = parent_decl_ctx_die.GetParentDeclContextDIE();
      break;
    }

    case DW_TAG_class_type:
    case DW_TAG_structure_type:
    case DW_TAG_union_type: {
      if (const char *class_union_struct_name = parent_decl_ctx_die.GetName()) {
        qualified_name.insert(
            0, GetDIEClassTemplateParams(parent_decl_ctx_die));
        qualified_name.insert(0, "::");
        qualified_name.insert(0, class_union_struct_name);
      }
      parent_decl_ctx_die = parent_decl_ctx_die.GetParentDeclContextDIE();
      break;
    }

    default:
      parent_decl_ctx_die.Clear();
      break;
    }
  }

  if (qualified_name.empty())
    qualified_name.append("::");

  qualified_name.append(unique_typename.GetCString());
  qualified_name.append(GetDIEClassTemplateParams(die));

  unique_typename = ConstString(qualified_name);
}

TypeSP
DWARFASTParserClang::ParseStructureLikeDIE(const SymbolContext &sc,
                                           const DWARFDIE &die,
                                           ParsedDWARFTypeAttributes &attrs) {
  CompilerType clang_type;
  const dw_tag_t tag = die.Tag();
  SymbolFileDWARF *dwarf = die.GetDWARF();
  LanguageType cu_language = SymbolFileDWARF::GetLanguage(*die.GetCU());
  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);

  ConstString unique_typename(attrs.name);
  Declaration unique_decl(attrs.decl);
  uint64_t byte_size = attrs.byte_size.value_or(0);
  if (attrs.byte_size && *attrs.byte_size == 0 && attrs.name &&
      !die.HasChildren() && cu_language == eLanguageTypeObjC) {
    // Work around an issue with clang at the moment where forward
    // declarations for objective C classes are emitted as:
    //  DW_TAG_structure_type [2]
    //  DW_AT_name( "ForwardObjcClass" )
    //  DW_AT_byte_size( 0x00 )
    //  DW_AT_decl_file( "..." )
    //  DW_AT_decl_line( 1 )
    //
    // Note that there is no DW_AT_declaration and there are no children,
    // and the byte size is zero.
    attrs.is_forward_declaration = true;
  }

  if (attrs.name) {
    GetUniqueTypeNameAndDeclaration(die, cu_language, unique_typename,
                                    unique_decl);
    if (UniqueDWARFASTType *unique_ast_entry_type =
            dwarf->GetUniqueDWARFASTTypeMap().Find(
                unique_typename, die, unique_decl, byte_size,
                attrs.is_forward_declaration)) {
      if (TypeSP type_sp = unique_ast_entry_type->m_type_sp) {
        dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
        LinkDeclContextToDIE(
            GetCachedClangDeclContextForDIE(unique_ast_entry_type->m_die), die);
        // If the DIE being parsed in this function is a definition and the
        // entry in the map is a declaration, then we need to update the entry
        // to point to the definition DIE.
        if (!attrs.is_forward_declaration &&
            unique_ast_entry_type->m_is_forward_declaration) {
          unique_ast_entry_type->UpdateToDefDIE(die, unique_decl, byte_size);
          clang_type = type_sp->GetForwardCompilerType();

          CompilerType compiler_type_no_qualifiers =
              ClangUtil::RemoveFastQualifiers(clang_type);
          dwarf->GetForwardDeclCompilerTypeToDIE().insert_or_assign(
              compiler_type_no_qualifiers.GetOpaqueQualType(),
              *die.GetDIERef());
        }
        return type_sp;
      }
    }
  }

  DEBUG_PRINTF("0x%8.8" PRIx64 ": %s (\"%s\")\n", die.GetID(),
               DW_TAG_value_to_name(tag), type_name_cstr);

  int tag_decl_kind = -1;
  AccessType default_accessibility = eAccessNone;
  if (tag == DW_TAG_structure_type) {
    tag_decl_kind = llvm::to_underlying(clang::TagTypeKind::Struct);
    default_accessibility = eAccessPublic;
  } else if (tag == DW_TAG_union_type) {
    tag_decl_kind = llvm::to_underlying(clang::TagTypeKind::Union);
    default_accessibility = eAccessPublic;
  } else if (tag == DW_TAG_class_type) {
    tag_decl_kind = llvm::to_underlying(clang::TagTypeKind::Class);
    default_accessibility = eAccessPrivate;
  }

  if ((attrs.class_language == eLanguageTypeObjC ||
       attrs.class_language == eLanguageTypeObjC_plus_plus) &&
      !attrs.is_complete_objc_class &&
      die.Supports_DW_AT_APPLE_objc_complete_type()) {
    // We have a valid eSymbolTypeObjCClass class symbol whose name
    // matches the current objective C class that we are trying to find
    // and this DIE isn't the complete definition (we checked
    // is_complete_objc_class above and know it is false), so the real
    // definition is in here somewhere
    TypeSP type_sp =
        dwarf->FindCompleteObjCDefinitionTypeForDIE(die, attrs.name, true);

    if (!type_sp) {
      SymbolFileDWARFDebugMap *debug_map_symfile = dwarf->GetDebugMapSymfile();
      if (debug_map_symfile) {
        // We weren't able to find a full declaration in this DWARF,
        // see if we have a declaration anywhere else...
        type_sp = debug_map_symfile->FindCompleteObjCDefinitionTypeForDIE(
            die, attrs.name, true);
      }
    }

    if (type_sp) {
      if (log) {
        dwarf->GetObjectFile()->GetModule()->LogMessage(
            log,
            "SymbolFileDWARF({0:p}) - {1:x16}: {2} ({3}) type \"{4}\" is an "
            "incomplete objc type, complete type is {5:x8}",
            static_cast<void *>(this), die.GetID(), DW_TAG_value_to_name(tag),
            tag, attrs.name.GetCString(), type_sp->GetID());
      }
      return type_sp;
    }
  }

  if (attrs.is_forward_declaration) {
    // See if the type comes from a Clang module and if so, track down
    // that type.
    TypeSP type_sp = ParseTypeFromClangModule(sc, die, log);
    if (type_sp)
      return type_sp;
  }

  assert(tag_decl_kind != -1);
  UNUSED_IF_ASSERT_DISABLED(tag_decl_kind);
  clang::DeclContext *containing_decl_ctx =
      GetClangDeclContextContainingDIE(die, nullptr);

  PrepareContextToReceiveMembers(m_ast, GetClangASTImporter(),
                                 containing_decl_ctx, die,
                                 attrs.name.GetCString());

  if (attrs.accessibility == eAccessNone && containing_decl_ctx) {
    // Check the decl context that contains this class/struct/union. If
    // it is a class we must give it an accessibility.
    const clang::Decl::Kind containing_decl_kind =
        containing_decl_ctx->getDeclKind();
    if (DeclKindIsCXXClass(containing_decl_kind))
      attrs.accessibility = default_accessibility;
  }

  ClangASTMetadata metadata;
  metadata.SetUserID(die.GetID());
  metadata.SetIsDynamicCXXType(dwarf->ClassOrStructIsVirtual(die));

  TypeSystemClang::TemplateParameterInfos template_param_infos;
  if (ParseTemplateParameterInfos(die, template_param_infos)) {
    clang::ClassTemplateDecl *class_template_decl =
        m_ast.ParseClassTemplateDecl(
            containing_decl_ctx, GetOwningClangModule(die), attrs.accessibility,
            attrs.name.GetCString(), tag_decl_kind, template_param_infos);
    if (!class_template_decl) {
      if (log) {
        dwarf->GetObjectFile()->GetModule()->LogMessage(
            log,
            "SymbolFileDWARF({0:p}) - {1:x16}: {2} ({3}) type \"{4}\" "
            "clang::ClassTemplateDecl failed to return a decl.",
            static_cast<void *>(this), die.GetID(), DW_TAG_value_to_name(tag),
            tag, attrs.name.GetCString());
      }
      return TypeSP();
    }

    clang::ClassTemplateSpecializationDecl *class_specialization_decl =
        m_ast.CreateClassTemplateSpecializationDecl(
            containing_decl_ctx, GetOwningClangModule(die), class_template_decl,
            tag_decl_kind, template_param_infos);
    clang_type =
        m_ast.CreateClassTemplateSpecializationType(class_specialization_decl);

    m_ast.SetMetadata(class_template_decl, metadata);
    m_ast.SetMetadata(class_specialization_decl, metadata);
  }

  if (!clang_type) {
    clang_type = m_ast.CreateRecordType(
        containing_decl_ctx, GetOwningClangModule(die), attrs.accessibility,
        attrs.name.GetCString(), tag_decl_kind, attrs.class_language, &metadata,
        attrs.exports_symbols);
  }

  TypeSP type_sp = dwarf->MakeType(
      die.GetID(), attrs.name, attrs.byte_size, nullptr, LLDB_INVALID_UID,
      Type::eEncodingIsUID, &attrs.decl, clang_type,
      Type::ResolveState::Forward,
      TypePayloadClang(OptionalClangModuleID(), attrs.is_complete_objc_class));

  // Store a forward declaration to this class type in case any
  // parameters in any class methods need it for the clang types for
  // function prototypes.
  clang::DeclContext *type_decl_ctx =
      TypeSystemClang::GetDeclContextForType(clang_type);
  LinkDeclContextToDIE(type_decl_ctx, die);

  // UniqueDWARFASTType is large, so don't create a local variables on the
  // stack, put it on the heap. This function is often called recursively and
  // clang isn't good at sharing the stack space for variables in different
  // blocks.
  auto unique_ast_entry_up = std::make_unique<UniqueDWARFASTType>();
  // Add our type to the unique type map so we don't end up creating many
  // copies of the same type over and over in the ASTContext for our
  // module
  unique_ast_entry_up->m_type_sp = type_sp;
  unique_ast_entry_up->m_die = die;
  unique_ast_entry_up->m_declaration = unique_decl;
  unique_ast_entry_up->m_byte_size = byte_size;
  unique_ast_entry_up->m_is_forward_declaration = attrs.is_forward_declaration;
  dwarf->GetUniqueDWARFASTTypeMap().Insert(unique_typename,
                                           *unique_ast_entry_up);

  // Leave this as a forward declaration until we need to know the
  // details of the type. lldb_private::Type will automatically call
  // the SymbolFile virtual function
  // "SymbolFileDWARF::CompleteType(Type *)" When the definition
  // needs to be defined.
  bool inserted =
      dwarf->GetForwardDeclCompilerTypeToDIE()
          .try_emplace(
              ClangUtil::RemoveFastQualifiers(clang_type).GetOpaqueQualType(),
              *die.GetDIERef())
          .second;
  assert(inserted && "Type already in the forward declaration map!");
  (void)inserted;
  m_ast.SetHasExternalStorage(clang_type.GetOpaqueQualType(), true);

  // If we made a clang type, set the trivial abi if applicable: We only
  // do this for pass by value - which implies the Trivial ABI. There
  // isn't a way to assert that something that would normally be pass by
  // value is pass by reference, so we ignore that attribute if set.
  if (attrs.calling_convention == llvm::dwarf::DW_CC_pass_by_value) {
    clang::CXXRecordDecl *record_decl =
        m_ast.GetAsCXXRecordDecl(clang_type.GetOpaqueQualType());
    if (record_decl && record_decl->getDefinition()) {
      record_decl->setHasTrivialSpecialMemberForCall();
    }
  }

  if (attrs.calling_convention == llvm::dwarf::DW_CC_pass_by_reference) {
    clang::CXXRecordDecl *record_decl =
        m_ast.GetAsCXXRecordDecl(clang_type.GetOpaqueQualType());
    if (record_decl)
      record_decl->setArgPassingRestrictions(
          clang::RecordArgPassingKind::CannotPassInRegs);
  }
  return type_sp;
}

// DWARF parsing functions

class DWARFASTParserClang::DelayedAddObjCClassProperty {
public:
  DelayedAddObjCClassProperty(
      const CompilerType &class_opaque_type, const char *property_name,
      const CompilerType &property_opaque_type, // The property type is only
                                                // required if you don't have an
                                                // ivar decl
      const char *property_setter_name, const char *property_getter_name,
      uint32_t property_attributes, const ClangASTMetadata *metadata)
      : m_class_opaque_type(class_opaque_type), m_property_name(property_name),
        m_property_opaque_type(property_opaque_type),
        m_property_setter_name(property_setter_name),
        m_property_getter_name(property_getter_name),
        m_property_attributes(property_attributes) {
    if (metadata != nullptr) {
      m_metadata_up = std::make_unique<ClangASTMetadata>();
      *m_metadata_up = *metadata;
    }
  }

  DelayedAddObjCClassProperty(const DelayedAddObjCClassProperty &rhs) {
    *this = rhs;
  }

  DelayedAddObjCClassProperty &
  operator=(const DelayedAddObjCClassProperty &rhs) {
    m_class_opaque_type = rhs.m_class_opaque_type;
    m_property_name = rhs.m_property_name;
    m_property_opaque_type = rhs.m_property_opaque_type;
    m_property_setter_name = rhs.m_property_setter_name;
    m_property_getter_name = rhs.m_property_getter_name;
    m_property_attributes = rhs.m_property_attributes;

    if (rhs.m_metadata_up) {
      m_metadata_up = std::make_unique<ClangASTMetadata>();
      *m_metadata_up = *rhs.m_metadata_up;
    }
    return *this;
  }

  bool Finalize() {
    return TypeSystemClang::AddObjCClassProperty(
        m_class_opaque_type, m_property_name, m_property_opaque_type,
        /*ivar_decl=*/nullptr, m_property_setter_name, m_property_getter_name,
        m_property_attributes, m_metadata_up.get());
  }

private:
  CompilerType m_class_opaque_type;
  const char *m_property_name;
  CompilerType m_property_opaque_type;
  const char *m_property_setter_name;
  const char *m_property_getter_name;
  uint32_t m_property_attributes;
  std::unique_ptr<ClangASTMetadata> m_metadata_up;
};

bool DWARFASTParserClang::ParseTemplateDIE(
    const DWARFDIE &die,
    TypeSystemClang::TemplateParameterInfos &template_param_infos) {
  const dw_tag_t tag = die.Tag();
  bool is_template_template_argument = false;

  switch (tag) {
  case DW_TAG_GNU_template_parameter_pack: {
    template_param_infos.SetParameterPack(
        std::make_unique<TypeSystemClang::TemplateParameterInfos>());
    for (DWARFDIE child_die : die.children()) {
      if (!ParseTemplateDIE(child_die, template_param_infos.GetParameterPack()))
        return false;
    }
    if (const char *name = die.GetName()) {
      template_param_infos.SetPackName(name);
    }
    return true;
  }
  case DW_TAG_GNU_template_template_param:
    is_template_template_argument = true;
    [[fallthrough]];
  case DW_TAG_template_type_parameter:
  case DW_TAG_template_value_parameter: {
    DWARFAttributes attributes = die.GetAttributes();
    if (attributes.Size() == 0)
      return true;

    const char *name = nullptr;
    const char *template_name = nullptr;
    CompilerType clang_type;
    uint64_t uval64 = 0;
    bool uval64_valid = false;
    bool is_default_template_arg = false;
    DWARFFormValue form_value;
    for (size_t i = 0; i < attributes.Size(); ++i) {
      const dw_attr_t attr = attributes.AttributeAtIndex(i);

      switch (attr) {
      case DW_AT_name:
        if (attributes.ExtractFormValueAtIndex(i, form_value))
          name = form_value.AsCString();
        break;

      case DW_AT_GNU_template_name:
        if (attributes.ExtractFormValueAtIndex(i, form_value))
          template_name = form_value.AsCString();
        break;

      case DW_AT_type:
        if (attributes.ExtractFormValueAtIndex(i, form_value)) {
          Type *lldb_type = die.ResolveTypeUID(form_value.Reference());
          if (lldb_type)
            clang_type = lldb_type->GetForwardCompilerType();
        }
        break;

      case DW_AT_const_value:
        if (attributes.ExtractFormValueAtIndex(i, form_value)) {
          uval64_valid = true;
          uval64 = form_value.Unsigned();
        }
        break;
      case DW_AT_default_value:
        if (attributes.ExtractFormValueAtIndex(i, form_value))
          is_default_template_arg = form_value.Boolean();
        break;
      default:
        break;
      }
    }

    clang::ASTContext &ast = m_ast.getASTContext();
    if (!clang_type)
      clang_type = m_ast.GetBasicType(eBasicTypeVoid);

    if (!is_template_template_argument) {
      bool is_signed = false;
      // Get the signed value for any integer or enumeration if available
      clang_type.IsIntegerOrEnumerationType(is_signed);

      if (name && !name[0])
        name = nullptr;

      if (tag == DW_TAG_template_value_parameter && uval64_valid) {
        std::optional<uint64_t> size = clang_type.GetBitSize(nullptr);
        if (!size)
          return false;
        llvm::APInt apint(*size, uval64, is_signed);
        template_param_infos.InsertArg(
            name, clang::TemplateArgument(ast, llvm::APSInt(apint, !is_signed),
                                          ClangUtil::GetQualType(clang_type),
                                          is_default_template_arg));
      } else {
        template_param_infos.InsertArg(
            name, clang::TemplateArgument(ClangUtil::GetQualType(clang_type),
                                          /*isNullPtr*/ false,
                                          is_default_template_arg));
      }
    } else {
      auto *tplt_type = m_ast.CreateTemplateTemplateParmDecl(template_name);
      template_param_infos.InsertArg(
          name, clang::TemplateArgument(clang::TemplateName(tplt_type),
                                        is_default_template_arg));
    }
  }
    return true;

  default:
    break;
  }
  return false;
}

bool DWARFASTParserClang::ParseTemplateParameterInfos(
    const DWARFDIE &parent_die,
    TypeSystemClang::TemplateParameterInfos &template_param_infos) {

  if (!parent_die)
    return false;

  for (DWARFDIE die : parent_die.children()) {
    const dw_tag_t tag = die.Tag();

    switch (tag) {
    case DW_TAG_template_type_parameter:
    case DW_TAG_template_value_parameter:
    case DW_TAG_GNU_template_parameter_pack:
    case DW_TAG_GNU_template_template_param:
      ParseTemplateDIE(die, template_param_infos);
      break;

    default:
      break;
    }
  }

  return !template_param_infos.IsEmpty() ||
         template_param_infos.hasParameterPack();
}

bool DWARFASTParserClang::CompleteRecordType(const DWARFDIE &die,
                                             lldb_private::Type *type,
                                             CompilerType &clang_type) {
  const dw_tag_t tag = die.Tag();
  SymbolFileDWARF *dwarf = die.GetDWARF();

  ClangASTImporter::LayoutInfo layout_info;
  std::vector<DWARFDIE> contained_type_dies;

  if (die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0))
    return false; // No definition, cannot complete.

  // Start the definition if the type is not being defined already. This can
  // happen (e.g.) when adding nested types to a class type -- see
  // PrepareContextToReceiveMembers.
  if (!clang_type.IsBeingDefined())
    TypeSystemClang::StartTagDeclarationDefinition(clang_type);

  AccessType default_accessibility = eAccessNone;
  if (tag == DW_TAG_structure_type) {
    default_accessibility = eAccessPublic;
  } else if (tag == DW_TAG_union_type) {
    default_accessibility = eAccessPublic;
  } else if (tag == DW_TAG_class_type) {
    default_accessibility = eAccessPrivate;
  }

  std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> bases;
  // Parse members and base classes first
  std::vector<DWARFDIE> member_function_dies;

  DelayedPropertyList delayed_properties;
  ParseChildMembers(die, clang_type, bases, member_function_dies,
                    contained_type_dies, delayed_properties,
                    default_accessibility, layout_info);

  // Now parse any methods if there were any...
  for (const DWARFDIE &die : member_function_dies)
    dwarf->ResolveType(die);

  if (TypeSystemClang::IsObjCObjectOrInterfaceType(clang_type)) {
    ConstString class_name(clang_type.GetTypeName());
    if (class_name) {
      dwarf->GetObjCMethods(class_name, [&](DWARFDIE method_die) {
        method_die.ResolveType();
        return true;
      });

      for (DelayedAddObjCClassProperty &property : delayed_properties)
        property.Finalize();
    }
  }

  if (!bases.empty()) {
    // Make sure all base classes refer to complete types and not forward
    // declarations. If we don't do this, clang will crash with an
    // assertion in the call to clang_type.TransferBaseClasses()
    for (const auto &base_class : bases) {
      clang::TypeSourceInfo *type_source_info = base_class->getTypeSourceInfo();
      if (type_source_info)
        TypeSystemClang::RequireCompleteType(
            m_ast.GetType(type_source_info->getType()));
    }

    m_ast.TransferBaseClasses(clang_type.GetOpaqueQualType(), std::move(bases));
  }

  m_ast.AddMethodOverridesForCXXRecordType(clang_type.GetOpaqueQualType());
  TypeSystemClang::BuildIndirectFields(clang_type);
  TypeSystemClang::CompleteTagDeclarationDefinition(clang_type);

  if (type)
    layout_info.bit_size = type->GetByteSize(nullptr).value_or(0) * 8;
  if (layout_info.bit_size == 0)
    layout_info.bit_size =
        die.GetAttributeValueAsUnsigned(DW_AT_byte_size, 0) * 8;
  if (layout_info.alignment == 0)
    layout_info.alignment =
        die.GetAttributeValueAsUnsigned(llvm::dwarf::DW_AT_alignment, 0) * 8;

  clang::CXXRecordDecl *record_decl =
      m_ast.GetAsCXXRecordDecl(clang_type.GetOpaqueQualType());
  if (record_decl)
    GetClangASTImporter().SetRecordLayout(record_decl, layout_info);

  // Now parse all contained types inside of the class. We make forward
  // declarations to all classes, but we need the CXXRecordDecl to have decls
  // for all contained types because we don't get asked for them via the
  // external AST support.
  for (const DWARFDIE &die : contained_type_dies)
    dwarf->ResolveType(die);

  return (bool)clang_type;
}

bool DWARFASTParserClang::CompleteEnumType(const DWARFDIE &die,
                                           lldb_private::Type *type,
                                           CompilerType &clang_type) {
  if (TypeSystemClang::StartTagDeclarationDefinition(clang_type)) {
    if (die.HasChildren()) {
      bool is_signed = false;
      clang_type.IsIntegerType(is_signed);
      ParseChildEnumerators(clang_type, is_signed,
                            type->GetByteSize(nullptr).value_or(0), die);
    }
    TypeSystemClang::CompleteTagDeclarationDefinition(clang_type);
  }
  return (bool)clang_type;
}

bool DWARFASTParserClang::CompleteTypeFromDWARF(const DWARFDIE &die,
                                                lldb_private::Type *type,
                                                CompilerType &clang_type) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  std::lock_guard<std::recursive_mutex> guard(
      dwarf->GetObjectFile()->GetModule()->GetMutex());

  // Disable external storage for this type so we don't get anymore
  // clang::ExternalASTSource queries for this type.
  m_ast.SetHasExternalStorage(clang_type.GetOpaqueQualType(), false);

  if (!die)
    return false;

  const dw_tag_t tag = die.Tag();

  assert(clang_type);
  switch (tag) {
  case DW_TAG_structure_type:
  case DW_TAG_union_type:
  case DW_TAG_class_type:
    CompleteRecordType(die, type, clang_type);
    break;
  case DW_TAG_enumeration_type:
    CompleteEnumType(die, type, clang_type);
    break;
  default:
    assert(false && "not a forward clang type decl!");
    break;
  }

  // If the type is still not fully defined at this point, it means we weren't
  // able to find its definition. We must forcefully complete it to preserve
  // clang AST invariants.
  if (clang_type.IsBeingDefined()) {
    TypeSystemClang::CompleteTagDeclarationDefinition(clang_type);
    m_ast.SetDeclIsForcefullyCompleted(ClangUtil::GetAsTagDecl(clang_type));
  }

  return true;
}

void DWARFASTParserClang::EnsureAllDIEsInDeclContextHaveBeenParsed(
    lldb_private::CompilerDeclContext decl_context) {
  auto opaque_decl_ctx =
      (clang::DeclContext *)decl_context.GetOpaqueDeclContext();
  for (auto it = m_decl_ctx_to_die.find(opaque_decl_ctx);
       it != m_decl_ctx_to_die.end() && it->first == opaque_decl_ctx;
       it = m_decl_ctx_to_die.erase(it))
    for (DWARFDIE decl : it->second.children())
      GetClangDeclForDIE(decl);
}

CompilerDecl DWARFASTParserClang::GetDeclForUIDFromDWARF(const DWARFDIE &die) {
  clang::Decl *clang_decl = GetClangDeclForDIE(die);
  if (clang_decl != nullptr)
    return m_ast.GetCompilerDecl(clang_decl);
  return {};
}

CompilerDeclContext
DWARFASTParserClang::GetDeclContextForUIDFromDWARF(const DWARFDIE &die) {
  clang::DeclContext *clang_decl_ctx = GetClangDeclContextForDIE(die);
  if (clang_decl_ctx)
    return m_ast.CreateDeclContext(clang_decl_ctx);
  return {};
}

CompilerDeclContext
DWARFASTParserClang::GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) {
  clang::DeclContext *clang_decl_ctx =
      GetClangDeclContextContainingDIE(die, nullptr);
  if (clang_decl_ctx)
    return m_ast.CreateDeclContext(clang_decl_ctx);
  return {};
}

size_t DWARFASTParserClang::ParseChildEnumerators(
    lldb_private::CompilerType &clang_type, bool is_signed,
    uint32_t enumerator_byte_size, const DWARFDIE &parent_die) {
  if (!parent_die)
    return 0;

  size_t enumerators_added = 0;

  for (DWARFDIE die : parent_die.children()) {
    const dw_tag_t tag = die.Tag();
    if (tag != DW_TAG_enumerator)
      continue;

    DWARFAttributes attributes = die.GetAttributes();
    if (attributes.Size() == 0)
      continue;

    const char *name = nullptr;
    bool got_value = false;
    int64_t enum_value = 0;
    Declaration decl;

    for (size_t i = 0; i < attributes.Size(); ++i) {
      const dw_attr_t attr = attributes.AttributeAtIndex(i);
      DWARFFormValue form_value;
      if (attributes.ExtractFormValueAtIndex(i, form_value)) {
        switch (attr) {
        case DW_AT_const_value:
          got_value = true;
          if (is_signed)
            enum_value = form_value.Signed();
          else
            enum_value = form_value.Unsigned();
          break;

        case DW_AT_name:
          name = form_value.AsCString();
          break;

        case DW_AT_description:
        default:
        case DW_AT_decl_file:
          decl.SetFile(
              attributes.CompileUnitAtIndex(i)->GetFile(form_value.Unsigned()));
          break;
        case DW_AT_decl_line:
          decl.SetLine(form_value.Unsigned());
          break;
        case DW_AT_decl_column:
          decl.SetColumn(form_value.Unsigned());
          break;
        case DW_AT_sibling:
          break;
        }
      }
    }

    if (name && name[0] && got_value) {
      m_ast.AddEnumerationValueToEnumerationType(
          clang_type, decl, name, enum_value, enumerator_byte_size * 8);
      ++enumerators_added;
    }
  }
  return enumerators_added;
}

ConstString
DWARFASTParserClang::ConstructDemangledNameFromDWARF(const DWARFDIE &die) {
  bool is_static = false;
  bool is_variadic = false;
  bool has_template_params = false;
  unsigned type_quals = 0;
  std::vector<CompilerType> param_types;
  std::vector<clang::ParmVarDecl *> param_decls;
  StreamString sstr;

  DWARFDeclContext decl_ctx = die.GetDWARFDeclContext();
  sstr << decl_ctx.GetQualifiedName();

  clang::DeclContext *containing_decl_ctx =
      GetClangDeclContextContainingDIE(die, nullptr);
  ParseChildParameters(containing_decl_ctx, die, true, is_static, is_variadic,
                       has_template_params, param_types, param_decls,
                       type_quals);
  sstr << "(";
  for (size_t i = 0; i < param_types.size(); i++) {
    if (i > 0)
      sstr << ", ";
    sstr << param_types[i].GetTypeName();
  }
  if (is_variadic)
    sstr << ", ...";
  sstr << ")";
  if (type_quals & clang::Qualifiers::Const)
    sstr << " const";

  return ConstString(sstr.GetString());
}

Function *
DWARFASTParserClang::ParseFunctionFromDWARF(CompileUnit &comp_unit,
                                            const DWARFDIE &die,
                                            const AddressRange &func_range) {
  assert(func_range.GetBaseAddress().IsValid());
  DWARFRangeList func_ranges;
  const char *name = nullptr;
  const char *mangled = nullptr;
  std::optional<int> decl_file;
  std::optional<int> decl_line;
  std::optional<int> decl_column;
  std::optional<int> call_file;
  std::optional<int> call_line;
  std::optional<int> call_column;
  DWARFExpressionList frame_base;

  const dw_tag_t tag = die.Tag();

  if (tag != DW_TAG_subprogram)
    return nullptr;

  if (die.GetDIENamesAndRanges(name, mangled, func_ranges, decl_file, decl_line,
                               decl_column, call_file, call_line, call_column,
                               &frame_base)) {
    Mangled func_name;
    if (mangled)
      func_name.SetValue(ConstString(mangled));
    else if ((die.GetParent().Tag() == DW_TAG_compile_unit ||
              die.GetParent().Tag() == DW_TAG_partial_unit) &&
             Language::LanguageIsCPlusPlus(
                 SymbolFileDWARF::GetLanguage(*die.GetCU())) &&
             !Language::LanguageIsObjC(
                 SymbolFileDWARF::GetLanguage(*die.GetCU())) &&
             name && strcmp(name, "main") != 0) {
      // If the mangled name is not present in the DWARF, generate the
      // demangled name using the decl context. We skip if the function is
      // "main" as its name is never mangled.
      func_name.SetValue(ConstructDemangledNameFromDWARF(die));
    } else
      func_name.SetValue(ConstString(name));

    FunctionSP func_sp;
    std::unique_ptr<Declaration> decl_up;
    if (decl_file || decl_line || decl_column)
      decl_up = std::make_unique<Declaration>(
          die.GetCU()->GetFile(decl_file ? *decl_file : 0),
          decl_line ? *decl_line : 0, decl_column ? *decl_column : 0);

    SymbolFileDWARF *dwarf = die.GetDWARF();
    // Supply the type _only_ if it has already been parsed
    Type *func_type = dwarf->GetDIEToType().lookup(die.GetDIE());

    assert(func_type == nullptr || func_type != DIE_IS_BEING_PARSED);

    const user_id_t func_user_id = die.GetID();
    func_sp =
        std::make_shared<Function>(&comp_unit,
                                   func_user_id, // UserID is the DIE offset
                                   func_user_id, func_name, func_type,
                                   func_range); // first address range

    if (func_sp.get() != nullptr) {
      if (frame_base.IsValid())
        func_sp->GetFrameBaseExpression() = frame_base;
      comp_unit.AddFunction(func_sp);
      return func_sp.get();
    }
  }
  return nullptr;
}

namespace {
/// Parsed form of all attributes that are relevant for parsing Objective-C
/// properties.
struct PropertyAttributes {
  explicit PropertyAttributes(const DWARFDIE &die);
  const char *prop_name = nullptr;
  const char *prop_getter_name = nullptr;
  const char *prop_setter_name = nullptr;
  /// \see clang::ObjCPropertyAttribute
  uint32_t prop_attributes = 0;
};

struct DiscriminantValue {
  explicit DiscriminantValue(const DWARFDIE &die, ModuleSP module_sp);

  uint32_t byte_offset;
  uint32_t byte_size;
  DWARFFormValue type_ref;
};

struct VariantMember {
  explicit VariantMember(DWARFDIE &die, ModuleSP module_sp);
  bool IsDefault() const;

  std::optional<uint32_t> discr_value;
  DWARFFormValue type_ref;
  ConstString variant_name;
  uint32_t byte_offset;
  ConstString GetName() const;
};

struct VariantPart {
  explicit VariantPart(const DWARFDIE &die, const DWARFDIE &parent_die,
                       ModuleSP module_sp);

  std::vector<VariantMember> &members();

  DiscriminantValue &discriminant();

private:
  std::vector<VariantMember> _members;
  DiscriminantValue _discriminant;
};

} // namespace

ConstString VariantMember::GetName() const { return this->variant_name; }

bool VariantMember::IsDefault() const { return !discr_value; }

VariantMember::VariantMember(DWARFDIE &die, lldb::ModuleSP module_sp) {
  assert(die.Tag() == llvm::dwarf::DW_TAG_variant);
  this->discr_value =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_discr_value);

  for (auto child_die : die.children()) {
    switch (child_die.Tag()) {
    case llvm::dwarf::DW_TAG_member: {
      DWARFAttributes attributes = child_die.GetAttributes();
      for (std::size_t i = 0; i < attributes.Size(); ++i) {
        DWARFFormValue form_value;
        const dw_attr_t attr = attributes.AttributeAtIndex(i);
        if (attributes.ExtractFormValueAtIndex(i, form_value)) {
          switch (attr) {
          case DW_AT_name:
            variant_name = ConstString(form_value.AsCString());
            break;
          case DW_AT_type:
            type_ref = form_value;
            break;

          case DW_AT_data_member_location:
            if (auto maybe_offset =
                    ExtractDataMemberLocation(die, form_value, module_sp))
              byte_offset = *maybe_offset;
            break;

          default:
            break;
          }
        }
      }
      break;
    }
    default:
      break;
    }
    break;
  }
}

DiscriminantValue::DiscriminantValue(const DWARFDIE &die, ModuleSP module_sp) {
  auto referenced_die = die.GetReferencedDIE(DW_AT_discr);
  DWARFAttributes attributes = referenced_die.GetAttributes();
  for (std::size_t i = 0; i < attributes.Size(); ++i) {
    const dw_attr_t attr = attributes.AttributeAtIndex(i);
    DWARFFormValue form_value;
    if (attributes.ExtractFormValueAtIndex(i, form_value)) {
      switch (attr) {
      case DW_AT_type:
        type_ref = form_value;
        break;
      case DW_AT_data_member_location:
        if (auto maybe_offset =
                ExtractDataMemberLocation(die, form_value, module_sp))
          byte_offset = *maybe_offset;
        break;
      default:
        break;
      }
    }
  }
}

VariantPart::VariantPart(const DWARFDIE &die, const DWARFDIE &parent_die,
                         lldb::ModuleSP module_sp)
    : _members(), _discriminant(die, module_sp) {

  for (auto child : die.children()) {
    if (child.Tag() == llvm::dwarf::DW_TAG_variant) {
      _members.push_back(VariantMember(child, module_sp));
    }
  }
}

std::vector<VariantMember> &VariantPart::members() { return this->_members; }

DiscriminantValue &VariantPart::discriminant() { return this->_discriminant; }

DWARFASTParserClang::MemberAttributes::MemberAttributes(
    const DWARFDIE &die, const DWARFDIE &parent_die, ModuleSP module_sp) {
  DWARFAttributes attributes = die.GetAttributes();
  for (size_t i = 0; i < attributes.Size(); ++i) {
    const dw_attr_t attr = attributes.AttributeAtIndex(i);
    DWARFFormValue form_value;
    if (attributes.ExtractFormValueAtIndex(i, form_value)) {
      switch (attr) {
      case DW_AT_name:
        name = form_value.AsCString();
        break;
      case DW_AT_type:
        encoding_form = form_value;
        break;
      case DW_AT_bit_offset:
        bit_offset = form_value.Signed();
        break;
      case DW_AT_bit_size:
        bit_size = form_value.Unsigned();
        break;
      case DW_AT_byte_size:
        byte_size = form_value.Unsigned();
        break;
      case DW_AT_const_value:
        const_value_form = form_value;
        break;
      case DW_AT_data_bit_offset:
        data_bit_offset = form_value.Unsigned();
        break;
      case DW_AT_data_member_location:
        if (auto maybe_offset =
                ExtractDataMemberLocation(die, form_value, module_sp))
          member_byte_offset = *maybe_offset;
        break;

      case DW_AT_accessibility:
        accessibility =
            DWARFASTParser::GetAccessTypeFromDWARF(form_value.Unsigned());
        break;
      case DW_AT_artificial:
        is_artificial = form_value.Boolean();
        break;
      case DW_AT_declaration:
        is_declaration = form_value.Boolean();
        break;
      default:
        break;
      }
    }
  }

  // Clang has a DWARF generation bug where sometimes it represents
  // fields that are references with bad byte size and bit size/offset
  // information such as:
  //
  //  DW_AT_byte_size( 0x00 )
  //  DW_AT_bit_size( 0x40 )
  //  DW_AT_bit_offset( 0xffffffffffffffc0 )
  //
  // So check the bit offset to make sure it is sane, and if the values
  // are not sane, remove them. If we don't do this then we will end up
  // with a crash if we try to use this type in an expression when clang
  // becomes unhappy with its recycled debug info.
  if (byte_size.value_or(0) == 0 && bit_offset < 0) {
    bit_size = 0;
    bit_offset = 0;
  }
}

PropertyAttributes::PropertyAttributes(const DWARFDIE &die) {

  DWARFAttributes attributes = die.GetAttributes();
  for (size_t i = 0; i < attributes.Size(); ++i) {
    const dw_attr_t attr = attributes.AttributeAtIndex(i);
    DWARFFormValue form_value;
    if (attributes.ExtractFormValueAtIndex(i, form_value)) {
      switch (attr) {
      case DW_AT_APPLE_property_name:
        prop_name = form_value.AsCString();
        break;
      case DW_AT_APPLE_property_getter:
        prop_getter_name = form_value.AsCString();
        break;
      case DW_AT_APPLE_property_setter:
        prop_setter_name = form_value.AsCString();
        break;
      case DW_AT_APPLE_property_attribute:
        prop_attributes = form_value.Unsigned();
        break;
      default:
        break;
      }
    }
  }

  if (!prop_name)
    return;
  ConstString fixed_setter;

  // Check if the property getter/setter were provided as full names.
  // We want basenames, so we extract them.
  if (prop_getter_name && prop_getter_name[0] == '-') {
    std::optional<const ObjCLanguage::MethodName> prop_getter_method =
        ObjCLanguage::MethodName::Create(prop_getter_name, true);
    if (prop_getter_method)
      prop_getter_name =
          ConstString(prop_getter_method->GetSelector()).GetCString();
  }

  if (prop_setter_name && prop_setter_name[0] == '-') {
    std::optional<const ObjCLanguage::MethodName> prop_setter_method =
        ObjCLanguage::MethodName::Create(prop_setter_name, true);
    if (prop_setter_method)
      prop_setter_name =
          ConstString(prop_setter_method->GetSelector()).GetCString();
  }

  // If the names haven't been provided, they need to be filled in.
  if (!prop_getter_name)
    prop_getter_name = prop_name;
  if (!prop_setter_name && prop_name[0] &&
      !(prop_attributes & DW_APPLE_PROPERTY_readonly)) {
    StreamString ss;

    ss.Printf("set%c%s:", toupper(prop_name[0]), &prop_name[1]);

    fixed_setter.SetString(ss.GetString());
    prop_setter_name = fixed_setter.GetCString();
  }
}

void DWARFASTParserClang::ParseObjCProperty(
    const DWARFDIE &die, const DWARFDIE &parent_die,
    const lldb_private::CompilerType &class_clang_type,
    DelayedPropertyList &delayed_properties) {
  // This function can only parse DW_TAG_APPLE_property.
  assert(die.Tag() == DW_TAG_APPLE_property);

  ModuleSP module_sp = parent_die.GetDWARF()->GetObjectFile()->GetModule();

  const MemberAttributes attrs(die, parent_die, module_sp);
  const PropertyAttributes propAttrs(die);

  if (!propAttrs.prop_name) {
    module_sp->ReportError("{0:x8}: DW_TAG_APPLE_property has no name.",
                           die.GetID());
    return;
  }

  Type *member_type = die.ResolveTypeUID(attrs.encoding_form.Reference());
  if (!member_type) {
    module_sp->ReportError(
        "{0:x8}: DW_TAG_APPLE_property '{1}' refers to type {2:x16}"
        " which was unable to be parsed",
        die.GetID(), propAttrs.prop_name,
        attrs.encoding_form.Reference().GetOffset());
    return;
  }

  ClangASTMetadata metadata;
  metadata.SetUserID(die.GetID());
  delayed_properties.push_back(DelayedAddObjCClassProperty(
      class_clang_type, propAttrs.prop_name,
      member_type->GetLayoutCompilerType(), propAttrs.prop_setter_name,
      propAttrs.prop_getter_name, propAttrs.prop_attributes, &metadata));
}

llvm::Expected<llvm::APInt> DWARFASTParserClang::ExtractIntFromFormValue(
    const CompilerType &int_type, const DWARFFormValue &form_value) const {
  clang::QualType qt = ClangUtil::GetQualType(int_type);
  assert(qt->isIntegralOrEnumerationType());
  auto ts_ptr = int_type.GetTypeSystem().dyn_cast_or_null<TypeSystemClang>();
  if (!ts_ptr)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "TypeSystem not clang");
  TypeSystemClang &ts = *ts_ptr;
  clang::ASTContext &ast = ts.getASTContext();

  const unsigned type_bits = ast.getIntWidth(qt);
  const bool is_unsigned = qt->isUnsignedIntegerType();

  // The maximum int size supported at the moment by this function. Limited
  // by the uint64_t return type of DWARFFormValue::Signed/Unsigned.
  constexpr std::size_t max_bit_size = 64;

  // For values bigger than 64 bit (e.g. __int128_t values),
  // DWARFFormValue's Signed/Unsigned functions will return wrong results so
  // emit an error for now.
  if (type_bits > max_bit_size) {
    auto msg = llvm::formatv("Can only parse integers with up to {0} bits, but "
                             "given integer has {1} bits.",
                             max_bit_size, type_bits);
    return llvm::createStringError(llvm::inconvertibleErrorCode(), msg.str());
  }

  // Construct an APInt with the maximum bit size and the given integer.
  llvm::APInt result(max_bit_size, form_value.Unsigned(), !is_unsigned);

  // Calculate how many bits are required to represent the input value.
  // For unsigned types, take the number of active bits in the APInt.
  // For signed types, ask APInt how many bits are required to represent the
  // signed integer.
  const unsigned required_bits =
      is_unsigned ? result.getActiveBits() : result.getSignificantBits();

  // If the input value doesn't fit into the integer type, return an error.
  if (required_bits > type_bits) {
    std::string value_as_str = is_unsigned
                                   ? std::to_string(form_value.Unsigned())
                                   : std::to_string(form_value.Signed());
    auto msg = llvm::formatv("Can't store {0} value {1} in integer with {2} "
                             "bits.",
                             (is_unsigned ? "unsigned" : "signed"),
                             value_as_str, type_bits);
    return llvm::createStringError(llvm::inconvertibleErrorCode(), msg.str());
  }

  // Trim the result to the bit width our the int type.
  if (result.getBitWidth() > type_bits)
    result = result.trunc(type_bits);
  return result;
}

void DWARFASTParserClang::CreateStaticMemberVariable(
    const DWARFDIE &die, const MemberAttributes &attrs,
    const lldb_private::CompilerType &class_clang_type) {
  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);
  assert(die.Tag() == DW_TAG_member || die.Tag() == DW_TAG_variable);

  Type *var_type = die.ResolveTypeUID(attrs.encoding_form.Reference());

  if (!var_type)
    return;

  auto accessibility =
      attrs.accessibility == eAccessNone ? eAccessPublic : attrs.accessibility;

  CompilerType ct = var_type->GetForwardCompilerType();
  clang::VarDecl *v = TypeSystemClang::AddVariableToRecordType(
      class_clang_type, attrs.name, ct, accessibility);
  if (!v) {
    LLDB_LOG(log, "Failed to add variable to the record type");
    return;
  }

  bool unused;
  // TODO: Support float/double static members as well.
  if (!ct.IsIntegerOrEnumerationType(unused) || !attrs.const_value_form)
    return;

  llvm::Expected<llvm::APInt> const_value_or_err =
      ExtractIntFromFormValue(ct, *attrs.const_value_form);
  if (!const_value_or_err) {
    LLDB_LOG_ERROR(log, const_value_or_err.takeError(),
                   "Failed to add const value to variable {1}: {0}",
                   v->getQualifiedNameAsString());
    return;
  }

  TypeSystemClang::SetIntegerInitializerForVariable(v, *const_value_or_err);
}

void DWARFASTParserClang::ParseSingleMember(
    const DWARFDIE &die, const DWARFDIE &parent_die,
    const lldb_private::CompilerType &class_clang_type,
    lldb::AccessType default_accessibility,
    lldb_private::ClangASTImporter::LayoutInfo &layout_info,
    FieldInfo &last_field_info) {
  // This function can only parse DW_TAG_member.
  assert(die.Tag() == DW_TAG_member);

  ModuleSP module_sp = parent_die.GetDWARF()->GetObjectFile()->GetModule();
  const dw_tag_t tag = die.Tag();
  // Get the parent byte size so we can verify any members will fit
  const uint64_t parent_byte_size =
      parent_die.GetAttributeValueAsUnsigned(DW_AT_byte_size, UINT64_MAX);
  const uint64_t parent_bit_size =
      parent_byte_size == UINT64_MAX ? UINT64_MAX : parent_byte_size * 8;

  const MemberAttributes attrs(die, parent_die, module_sp);

  // Handle static members, which are typically members without
  // locations. However, GCC doesn't emit DW_AT_data_member_location
  // for any union members (regardless of linkage).
  // Non-normative text pre-DWARFv5 recommends marking static
  // data members with an DW_AT_external flag. Clang emits this consistently
  // whereas GCC emits it only for static data members if not part of an
  // anonymous namespace. The flag that is consistently emitted for static
  // data members is DW_AT_declaration, so we check it instead.
  // The following block is only necessary to support DWARFv4 and earlier.
  // Starting with DWARFv5, static data members are marked DW_AT_variable so we
  // can consistently detect them on both GCC and Clang without below heuristic.
  if (attrs.member_byte_offset == UINT32_MAX &&
      attrs.data_bit_offset == UINT64_MAX && attrs.is_declaration) {
    CreateStaticMemberVariable(die, attrs, class_clang_type);
    return;
  }

  Type *member_type = die.ResolveTypeUID(attrs.encoding_form.Reference());
  if (!member_type) {
    if (attrs.name)
      module_sp->ReportError(
          "{0:x8}: DW_TAG_member '{1}' refers to type {2:x16}"
          " which was unable to be parsed",
          die.GetID(), attrs.name, attrs.encoding_form.Reference().GetOffset());
    else
      module_sp->ReportError("{0:x8}: DW_TAG_member refers to type {1:x16}"
                             " which was unable to be parsed",
                             die.GetID(),
                             attrs.encoding_form.Reference().GetOffset());
    return;
  }

  const uint64_t character_width = 8;
  const uint64_t word_width = 32;
  CompilerType member_clang_type = member_type->GetLayoutCompilerType();

  const auto accessibility = attrs.accessibility == eAccessNone
                                 ? default_accessibility
                                 : attrs.accessibility;

  uint64_t field_bit_offset = (attrs.member_byte_offset == UINT32_MAX
                                   ? 0
                                   : (attrs.member_byte_offset * 8ULL));

  if (attrs.bit_size > 0) {
    FieldInfo this_field_info;
    this_field_info.bit_offset = field_bit_offset;
    this_field_info.bit_size = attrs.bit_size;

    if (attrs.data_bit_offset != UINT64_MAX) {
      this_field_info.bit_offset = attrs.data_bit_offset;
    } else {
      auto byte_size = attrs.byte_size;
      if (!byte_size)
        byte_size = member_type->GetByteSize(nullptr);

      ObjectFile *objfile = die.GetDWARF()->GetObjectFile();
      if (objfile->GetByteOrder() == eByteOrderLittle) {
        this_field_info.bit_offset += byte_size.value_or(0) * 8;
        this_field_info.bit_offset -= (attrs.bit_offset + attrs.bit_size);
      } else {
        this_field_info.bit_offset += attrs.bit_offset;
      }
    }

    // The ObjC runtime knows the byte offset but we still need to provide
    // the bit-offset in the layout. It just means something different then
    // what it does in C and C++. So we skip this check for ObjC types.
    //
    // We also skip this for fields of a union since they will all have a
    // zero offset.
    if (!TypeSystemClang::IsObjCObjectOrInterfaceType(class_clang_type) &&
        !(parent_die.Tag() == DW_TAG_union_type &&
          this_field_info.bit_offset == 0) &&
        ((this_field_info.bit_offset >= parent_bit_size) ||
         (last_field_info.IsBitfield() &&
          !last_field_info.NextBitfieldOffsetIsValid(
              this_field_info.bit_offset)))) {
      ObjectFile *objfile = die.GetDWARF()->GetObjectFile();
      objfile->GetModule()->ReportWarning(
          "{0:x16}: {1} ({2}) bitfield named \"{3}\" has invalid "
          "bit offset ({4:x8}) member will be ignored. Please file a bug "
          "against the "
          "compiler and include the preprocessed output for {5}\n",
          die.GetID(), DW_TAG_value_to_name(tag), tag, attrs.name,
          this_field_info.bit_offset, GetUnitName(parent_die).c_str());
      return;
    }

    // Update the field bit offset we will report for layout
    field_bit_offset = this_field_info.bit_offset;

    // Objective-C has invalid DW_AT_bit_offset values in older
    // versions of clang, so we have to be careful and only insert
    // unnamed bitfields if we have a new enough clang.
    bool detect_unnamed_bitfields = true;

    if (TypeSystemClang::IsObjCObjectOrInterfaceType(class_clang_type))
      detect_unnamed_bitfields =
          die.GetCU()->Supports_unnamed_objc_bitfields();

    if (detect_unnamed_bitfields) {
      std::optional<FieldInfo> unnamed_field_info;
      uint64_t last_field_end =
          last_field_info.bit_offset + last_field_info.bit_size;

      if (!last_field_info.IsBitfield()) {
        // The last field was not a bit-field...
        // but if it did take up the entire word then we need to extend
        // last_field_end so the bit-field does not step into the last
        // fields padding.
        if (last_field_end != 0 && ((last_field_end % word_width) != 0))
          last_field_end += word_width - (last_field_end % word_width);
      }

      if (ShouldCreateUnnamedBitfield(last_field_info, last_field_end,
                                      this_field_info, layout_info)) {
        unnamed_field_info = FieldInfo{};
        unnamed_field_info->bit_size =
            this_field_info.bit_offset - last_field_end;
        unnamed_field_info->bit_offset = last_field_end;
      }

      if (unnamed_field_info) {
        clang::FieldDecl *unnamed_bitfield_decl =
            TypeSystemClang::AddFieldToRecordType(
                class_clang_type, llvm::StringRef(),
                m_ast.GetBuiltinTypeForEncodingAndBitSize(eEncodingSint,
                                                          word_width),
                accessibility, unnamed_field_info->bit_size);

        layout_info.field_offsets.insert(std::make_pair(
            unnamed_bitfield_decl, unnamed_field_info->bit_offset));
      }
    }

    last_field_info = this_field_info;
    last_field_info.SetIsBitfield(true);
  } else {
    last_field_info.bit_offset = field_bit_offset;

    if (std::optional<uint64_t> clang_type_size =
            member_type->GetByteSize(nullptr)) {
      last_field_info.bit_size = *clang_type_size * character_width;
    }

    last_field_info.SetIsBitfield(false);
  }

  // Don't turn artificial members such as vtable pointers into real FieldDecls
  // in our AST. Clang will re-create those articial members and they would
  // otherwise just overlap in the layout with the FieldDecls we add here.
  // This needs to be done after updating FieldInfo which keeps track of where
  // field start/end so we don't later try to fill the space of this
  // artificial member with (unnamed bitfield) padding.
  if (attrs.is_artificial && ShouldIgnoreArtificialField(attrs.name)) {
    last_field_info.SetIsArtificial(true);
    return;
  }

  if (!member_clang_type.IsCompleteType())
    member_clang_type.GetCompleteType();

  {
    // Older versions of clang emit the same DWARF for array[0] and array[1]. If
    // the current field is at the end of the structure, then there is
    // definitely no room for extra elements and we override the type to
    // array[0]. This was fixed by f454dfb6b5af.
    CompilerType member_array_element_type;
    uint64_t member_array_size;
    bool member_array_is_incomplete;

    if (member_clang_type.IsArrayType(&member_array_element_type,
                                      &member_array_size,
                                      &member_array_is_incomplete) &&
        !member_array_is_incomplete) {
      uint64_t parent_byte_size =
          parent_die.GetAttributeValueAsUnsigned(DW_AT_byte_size, UINT64_MAX);

      if (attrs.member_byte_offset >= parent_byte_size) {
        if (member_array_size != 1 &&
            (member_array_size != 0 ||
             attrs.member_byte_offset > parent_byte_size)) {
          module_sp->ReportError(
              "{0:x8}: DW_TAG_member '{1}' refers to type {2:x16}"
              " which extends beyond the bounds of {3:x8}",
              die.GetID(), attrs.name,
              attrs.encoding_form.Reference().GetOffset(), parent_die.GetID());
        }

        member_clang_type =
            m_ast.CreateArrayType(member_array_element_type, 0, false);
      }
    }
  }

  TypeSystemClang::RequireCompleteType(member_clang_type);

  clang::FieldDecl *field_decl = TypeSystemClang::AddFieldToRecordType(
      class_clang_type, attrs.name, member_clang_type, accessibility,
      attrs.bit_size);

  m_ast.SetMetadataAsUserID(field_decl, die.GetID());

  layout_info.field_offsets.insert(
      std::make_pair(field_decl, field_bit_offset));
}

bool DWARFASTParserClang::ParseChildMembers(
    const DWARFDIE &parent_die, CompilerType &class_clang_type,
    std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> &base_classes,
    std::vector<DWARFDIE> &member_function_dies,
    std::vector<DWARFDIE> &contained_type_dies,
    DelayedPropertyList &delayed_properties,
    const AccessType default_accessibility,
    ClangASTImporter::LayoutInfo &layout_info) {
  if (!parent_die)
    return false;

  FieldInfo last_field_info;

  ModuleSP module_sp = parent_die.GetDWARF()->GetObjectFile()->GetModule();
  auto ts = class_clang_type.GetTypeSystem();
  auto ast = ts.dyn_cast_or_null<TypeSystemClang>();
  if (ast == nullptr)
    return false;

  for (DWARFDIE die : parent_die.children()) {
    dw_tag_t tag = die.Tag();

    switch (tag) {
    case DW_TAG_APPLE_property:
      ParseObjCProperty(die, parent_die, class_clang_type, delayed_properties);
      break;

    case DW_TAG_variant_part:
      if (die.GetCU()->GetDWARFLanguageType() == eLanguageTypeRust) {
        ParseRustVariantPart(die, parent_die, class_clang_type,
                             default_accessibility, layout_info);
      }
      break;

    case DW_TAG_variable: {
      const MemberAttributes attrs(die, parent_die, module_sp);
      CreateStaticMemberVariable(die, attrs, class_clang_type);
    } break;
    case DW_TAG_member:
      ParseSingleMember(die, parent_die, class_clang_type,
                        default_accessibility, layout_info, last_field_info);
      break;

    case DW_TAG_subprogram:
      // Let the type parsing code handle this one for us.
      member_function_dies.push_back(die);
      break;

    case DW_TAG_inheritance:
      ParseInheritance(die, parent_die, class_clang_type, default_accessibility,
                       module_sp, base_classes, layout_info);
      break;

    default:
      if (llvm::dwarf::isType(tag))
        contained_type_dies.push_back(die);
      break;
    }
  }

  return true;
}

size_t DWARFASTParserClang::ParseChildParameters(
    clang::DeclContext *containing_decl_ctx, const DWARFDIE &parent_die,
    bool skip_artificial, bool &is_static, bool &is_variadic,
    bool &has_template_params, std::vector<CompilerType> &function_param_types,
    std::vector<clang::ParmVarDecl *> &function_param_decls,
    unsigned &type_quals) {
  if (!parent_die)
    return 0;

  size_t arg_idx = 0;
  for (DWARFDIE die : parent_die.children()) {
    const dw_tag_t tag = die.Tag();
    switch (tag) {
    case DW_TAG_formal_parameter: {
      DWARFAttributes attributes = die.GetAttributes();
      if (attributes.Size() == 0) {
        arg_idx++;
        break;
      }

      const char *name = nullptr;
      DWARFFormValue param_type_die_form;
      bool is_artificial = false;
      // one of None, Auto, Register, Extern, Static, PrivateExtern

      clang::StorageClass storage = clang::SC_None;
      uint32_t i;
      for (i = 0; i < attributes.Size(); ++i) {
        const dw_attr_t attr = attributes.AttributeAtIndex(i);
        DWARFFormValue form_value;
        if (attributes.ExtractFormValueAtIndex(i, form_value)) {
          switch (attr) {
          case DW_AT_name:
            name = form_value.AsCString();
            break;
          case DW_AT_type:
            param_type_die_form = form_value;
            break;
          case DW_AT_artificial:
            is_artificial = form_value.Boolean();
            break;
          case DW_AT_location:
          case DW_AT_const_value:
          case DW_AT_default_value:
          case DW_AT_description:
          case DW_AT_endianity:
          case DW_AT_is_optional:
          case DW_AT_segment:
          case DW_AT_variable_parameter:
          default:
          case DW_AT_abstract_origin:
          case DW_AT_sibling:
            break;
          }
        }
      }

      bool skip = false;
      if (skip_artificial && is_artificial) {
        // In order to determine if a C++ member function is "const" we
        // have to look at the const-ness of "this"...
        if (arg_idx == 0 &&
            DeclKindIsCXXClass(containing_decl_ctx->getDeclKind()) &&
            // Often times compilers omit the "this" name for the
            // specification DIEs, so we can't rely upon the name being in
            // the formal parameter DIE...
            (name == nullptr || ::strcmp(name, "this") == 0)) {
          Type *this_type = die.ResolveTypeUID(param_type_die_form.Reference());
          if (this_type) {
            uint32_t encoding_mask = this_type->GetEncodingMask();
            if (encoding_mask & Type::eEncodingIsPointerUID) {
              is_static = false;

              if (encoding_mask & (1u << Type::eEncodingIsConstUID))
                type_quals |= clang::Qualifiers::Const;
              if (encoding_mask & (1u << Type::eEncodingIsVolatileUID))
                type_quals |= clang::Qualifiers::Volatile;
            }
          }
        }
        skip = true;
      }

      if (!skip) {
        Type *type = die.ResolveTypeUID(param_type_die_form.Reference());
        if (type) {
          function_param_types.push_back(type->GetForwardCompilerType());

          clang::ParmVarDecl *param_var_decl = m_ast.CreateParameterDeclaration(
              containing_decl_ctx, GetOwningClangModule(die), name,
              type->GetForwardCompilerType(), storage);
          assert(param_var_decl);
          function_param_decls.push_back(param_var_decl);

          m_ast.SetMetadataAsUserID(param_var_decl, die.GetID());
        }
      }
      arg_idx++;
    } break;

    case DW_TAG_unspecified_parameters:
      is_variadic = true;
      break;

    case DW_TAG_template_type_parameter:
    case DW_TAG_template_value_parameter:
    case DW_TAG_GNU_template_parameter_pack:
      // The one caller of this was never using the template_param_infos, and
      // the local variable was taking up a large amount of stack space in
      // SymbolFileDWARF::ParseType() so this was removed. If we ever need the
      // template params back, we can add them back.
      // ParseTemplateDIE (dwarf_cu, die, template_param_infos);
      has_template_params = true;
      break;

    default:
      break;
    }
  }
  return arg_idx;
}

clang::Decl *DWARFASTParserClang::GetClangDeclForDIE(const DWARFDIE &die) {
  if (!die)
    return nullptr;

  switch (die.Tag()) {
  case DW_TAG_constant:
  case DW_TAG_formal_parameter:
  case DW_TAG_imported_declaration:
  case DW_TAG_imported_module:
    break;
  case DW_TAG_variable:
    // This means 'die' is a C++ static data member.
    // We don't want to create decls for such members
    // here.
    if (auto parent = die.GetParent();
        parent.IsValid() && TagIsRecordType(parent.Tag()))
      return nullptr;
    break;
  default:
    return nullptr;
  }

  DIEToDeclMap::iterator cache_pos = m_die_to_decl.find(die.GetDIE());
  if (cache_pos != m_die_to_decl.end())
    return cache_pos->second;

  if (DWARFDIE spec_die = die.GetReferencedDIE(DW_AT_specification)) {
    clang::Decl *decl = GetClangDeclForDIE(spec_die);
    m_die_to_decl[die.GetDIE()] = decl;
    return decl;
  }

  if (DWARFDIE abstract_origin_die =
          die.GetReferencedDIE(DW_AT_abstract_origin)) {
    clang::Decl *decl = GetClangDeclForDIE(abstract_origin_die);
    m_die_to_decl[die.GetDIE()] = decl;
    return decl;
  }

  clang::Decl *decl = nullptr;
  switch (die.Tag()) {
  case DW_TAG_variable:
  case DW_TAG_constant:
  case DW_TAG_formal_parameter: {
    SymbolFileDWARF *dwarf = die.GetDWARF();
    Type *type = GetTypeForDIE(die);
    if (dwarf && type) {
      const char *name = die.GetName();
      clang::DeclContext *decl_context =
          TypeSystemClang::DeclContextGetAsDeclContext(
              dwarf->GetDeclContextContainingUID(die.GetID()));
      decl = m_ast.CreateVariableDeclaration(
          decl_context, GetOwningClangModule(die), name,
          ClangUtil::GetQualType(type->GetForwardCompilerType()));
    }
    break;
  }
  case DW_TAG_imported_declaration: {
    SymbolFileDWARF *dwarf = die.GetDWARF();
    DWARFDIE imported_uid = die.GetAttributeValueAsReferenceDIE(DW_AT_import);
    if (imported_uid) {
      CompilerDecl imported_decl = SymbolFileDWARF::GetDecl(imported_uid);
      if (imported_decl) {
        clang::DeclContext *decl_context =
            TypeSystemClang::DeclContextGetAsDeclContext(
                dwarf->GetDeclContextContainingUID(die.GetID()));
        if (clang::NamedDecl *clang_imported_decl =
                llvm::dyn_cast<clang::NamedDecl>(
                    (clang::Decl *)imported_decl.GetOpaqueDecl()))
          decl = m_ast.CreateUsingDeclaration(
              decl_context, OptionalClangModuleID(), clang_imported_decl);
      }
    }
    break;
  }
  case DW_TAG_imported_module: {
    SymbolFileDWARF *dwarf = die.GetDWARF();
    DWARFDIE imported_uid = die.GetAttributeValueAsReferenceDIE(DW_AT_import);

    if (imported_uid) {
      CompilerDeclContext imported_decl_ctx =
          SymbolFileDWARF::GetDeclContext(imported_uid);
      if (imported_decl_ctx) {
        clang::DeclContext *decl_context =
            TypeSystemClang::DeclContextGetAsDeclContext(
                dwarf->GetDeclContextContainingUID(die.GetID()));
        if (clang::NamespaceDecl *ns_decl =
                TypeSystemClang::DeclContextGetAsNamespaceDecl(
                    imported_decl_ctx))
          decl = m_ast.CreateUsingDirectiveDeclaration(
              decl_context, OptionalClangModuleID(), ns_decl);
      }
    }
    break;
  }
  default:
    break;
  }

  m_die_to_decl[die.GetDIE()] = decl;

  return decl;
}

clang::DeclContext *
DWARFASTParserClang::GetClangDeclContextForDIE(const DWARFDIE &die) {
  if (die) {
    clang::DeclContext *decl_ctx = GetCachedClangDeclContextForDIE(die);
    if (decl_ctx)
      return decl_ctx;

    bool try_parsing_type = true;
    switch (die.Tag()) {
    case DW_TAG_compile_unit:
    case DW_TAG_partial_unit:
      decl_ctx = m_ast.GetTranslationUnitDecl();
      try_parsing_type = false;
      break;

    case DW_TAG_namespace:
      decl_ctx = ResolveNamespaceDIE(die);
      try_parsing_type = false;
      break;

    case DW_TAG_imported_declaration:
      decl_ctx = ResolveImportedDeclarationDIE(die);
      try_parsing_type = false;
      break;

    case DW_TAG_lexical_block:
      decl_ctx = GetDeclContextForBlock(die);
      try_parsing_type = false;
      break;

    default:
      break;
    }

    if (decl_ctx == nullptr && try_parsing_type) {
      Type *type = die.GetDWARF()->ResolveType(die);
      if (type)
        decl_ctx = GetCachedClangDeclContextForDIE(die);
    }

    if (decl_ctx) {
      LinkDeclContextToDIE(decl_ctx, die);
      return decl_ctx;
    }
  }
  return nullptr;
}

OptionalClangModuleID
DWARFASTParserClang::GetOwningClangModule(const DWARFDIE &die) {
  if (!die.IsValid())
    return {};

  for (DWARFDIE parent = die.GetParent(); parent.IsValid();
       parent = parent.GetParent()) {
    const dw_tag_t tag = parent.Tag();
    if (tag == DW_TAG_module) {
      DWARFDIE module_die = parent;
      auto it = m_die_to_module.find(module_die.GetDIE());
      if (it != m_die_to_module.end())
        return it->second;
      const char *name =
          module_die.GetAttributeValueAsString(DW_AT_name, nullptr);
      if (!name)
        return {};

      OptionalClangModuleID id =
          m_ast.GetOrCreateClangModule(name, GetOwningClangModule(module_die));
      m_die_to_module.insert({module_die.GetDIE(), id});
      return id;
    }
  }
  return {};
}

static bool IsSubroutine(const DWARFDIE &die) {
  switch (die.Tag()) {
  case DW_TAG_subprogram:
  case DW_TAG_inlined_subroutine:
    return true;
  default:
    return false;
  }
}

static DWARFDIE GetContainingFunctionWithAbstractOrigin(const DWARFDIE &die) {
  for (DWARFDIE candidate = die; candidate; candidate = candidate.GetParent()) {
    if (IsSubroutine(candidate)) {
      if (candidate.GetReferencedDIE(DW_AT_abstract_origin)) {
        return candidate;
      } else {
        return DWARFDIE();
      }
    }
  }
  assert(0 && "Shouldn't call GetContainingFunctionWithAbstractOrigin on "
              "something not in a function");
  return DWARFDIE();
}

static DWARFDIE FindAnyChildWithAbstractOrigin(const DWARFDIE &context) {
  for (DWARFDIE candidate : context.children()) {
    if (candidate.GetReferencedDIE(DW_AT_abstract_origin)) {
      return candidate;
    }
  }
  return DWARFDIE();
}

static DWARFDIE FindFirstChildWithAbstractOrigin(const DWARFDIE &block,
                                                 const DWARFDIE &function) {
  assert(IsSubroutine(function));
  for (DWARFDIE context = block; context != function.GetParent();
       context = context.GetParent()) {
    assert(!IsSubroutine(context) || context == function);
    if (DWARFDIE child = FindAnyChildWithAbstractOrigin(context)) {
      return child;
    }
  }
  return DWARFDIE();
}

clang::DeclContext *
DWARFASTParserClang::GetDeclContextForBlock(const DWARFDIE &die) {
  assert(die.Tag() == DW_TAG_lexical_block);
  DWARFDIE containing_function_with_abstract_origin =
      GetContainingFunctionWithAbstractOrigin(die);
  if (!containing_function_with_abstract_origin) {
    return (clang::DeclContext *)ResolveBlockDIE(die);
  }
  DWARFDIE child = FindFirstChildWithAbstractOrigin(
      die, containing_function_with_abstract_origin);
  CompilerDeclContext decl_context =
      GetDeclContextContainingUIDFromDWARF(child);
  return (clang::DeclContext *)decl_context.GetOpaqueDeclContext();
}

clang::BlockDecl *DWARFASTParserClang::ResolveBlockDIE(const DWARFDIE &die) {
  if (die && die.Tag() == DW_TAG_lexical_block) {
    clang::BlockDecl *decl =
        llvm::cast_or_null<clang::BlockDecl>(m_die_to_decl_ctx[die.GetDIE()]);

    if (!decl) {
      DWARFDIE decl_context_die;
      clang::DeclContext *decl_context =
          GetClangDeclContextContainingDIE(die, &decl_context_die);
      decl =
          m_ast.CreateBlockDeclaration(decl_context, GetOwningClangModule(die));

      if (decl)
        LinkDeclContextToDIE((clang::DeclContext *)decl, die);
    }

    return decl;
  }
  return nullptr;
}

clang::NamespaceDecl *
DWARFASTParserClang::ResolveNamespaceDIE(const DWARFDIE &die) {
  if (die && die.Tag() == DW_TAG_namespace) {
    // See if we already parsed this namespace DIE and associated it with a
    // uniqued namespace declaration
    clang::NamespaceDecl *namespace_decl =
        static_cast<clang::NamespaceDecl *>(m_die_to_decl_ctx[die.GetDIE()]);
    if (namespace_decl)
      return namespace_decl;
    else {
      const char *namespace_name = die.GetName();
      clang::DeclContext *containing_decl_ctx =
          GetClangDeclContextContainingDIE(die, nullptr);
      bool is_inline =
          die.GetAttributeValueAsUnsigned(DW_AT_export_symbols, 0) != 0;

      namespace_decl = m_ast.GetUniqueNamespaceDeclaration(
          namespace_name, containing_decl_ctx, GetOwningClangModule(die),
          is_inline);

      if (namespace_decl)
        LinkDeclContextToDIE((clang::DeclContext *)namespace_decl, die);
      return namespace_decl;
    }
  }
  return nullptr;
}

clang::NamespaceDecl *
DWARFASTParserClang::ResolveImportedDeclarationDIE(const DWARFDIE &die) {
  assert(die && die.Tag() == DW_TAG_imported_declaration);

  // See if we cached a NamespaceDecl for this imported declaration
  // already
  auto it = m_die_to_decl_ctx.find(die.GetDIE());
  if (it != m_die_to_decl_ctx.end())
    return static_cast<clang::NamespaceDecl *>(it->getSecond());

  clang::NamespaceDecl *namespace_decl = nullptr;

  const DWARFDIE imported_uid =
      die.GetAttributeValueAsReferenceDIE(DW_AT_import);
  if (!imported_uid)
    return nullptr;

  switch (imported_uid.Tag()) {
  case DW_TAG_imported_declaration:
    namespace_decl = ResolveImportedDeclarationDIE(imported_uid);
    break;
  case DW_TAG_namespace:
    namespace_decl = ResolveNamespaceDIE(imported_uid);
    break;
  default:
    return nullptr;
  }

  if (!namespace_decl)
    return nullptr;

  LinkDeclContextToDIE(namespace_decl, die);

  return namespace_decl;
}

clang::DeclContext *DWARFASTParserClang::GetClangDeclContextContainingDIE(
    const DWARFDIE &die, DWARFDIE *decl_ctx_die_copy) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  DWARFDIE decl_ctx_die = dwarf->GetDeclContextDIEContainingDIE(die);

  if (decl_ctx_die_copy)
    *decl_ctx_die_copy = decl_ctx_die;

  if (decl_ctx_die) {
    clang::DeclContext *clang_decl_ctx =
        GetClangDeclContextForDIE(decl_ctx_die);
    if (clang_decl_ctx)
      return clang_decl_ctx;
  }
  return m_ast.GetTranslationUnitDecl();
}

clang::DeclContext *
DWARFASTParserClang::GetCachedClangDeclContextForDIE(const DWARFDIE &die) {
  if (die) {
    DIEToDeclContextMap::iterator pos = m_die_to_decl_ctx.find(die.GetDIE());
    if (pos != m_die_to_decl_ctx.end())
      return pos->second;
  }
  return nullptr;
}

void DWARFASTParserClang::LinkDeclContextToDIE(clang::DeclContext *decl_ctx,
                                               const DWARFDIE &die) {
  m_die_to_decl_ctx[die.GetDIE()] = decl_ctx;
  // There can be many DIEs for a single decl context
  // m_decl_ctx_to_die[decl_ctx].insert(die.GetDIE());
  m_decl_ctx_to_die.insert(std::make_pair(decl_ctx, die));
}

bool DWARFASTParserClang::CopyUniqueClassMethodTypes(
    const DWARFDIE &src_class_die, const DWARFDIE &dst_class_die,
    lldb_private::Type *class_type, std::vector<DWARFDIE> &failures) {
  if (!class_type || !src_class_die || !dst_class_die)
    return false;
  if (src_class_die.Tag() != dst_class_die.Tag())
    return false;

  // We need to complete the class type so we can get all of the method types
  // parsed so we can then unique those types to their equivalent counterparts
  // in "dst_cu" and "dst_class_die"
  class_type->GetFullCompilerType();

  auto gather = [](DWARFDIE die, UniqueCStringMap<DWARFDIE> &map,
                   UniqueCStringMap<DWARFDIE> &map_artificial) {
    if (die.Tag() != DW_TAG_subprogram)
      return;
    // Make sure this is a declaration and not a concrete instance by looking
    // for DW_AT_declaration set to 1. Sometimes concrete function instances are
    // placed inside the class definitions and shouldn't be included in the list
    // of things that are tracking here.
    if (die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0) != 1)
      return;

    if (const char *name = die.GetMangledName()) {
      ConstString const_name(name);
      if (die.GetAttributeValueAsUnsigned(DW_AT_artificial, 0))
        map_artificial.Append(const_name, die);
      else
        map.Append(const_name, die);
    }
  };

  UniqueCStringMap<DWARFDIE> src_name_to_die;
  UniqueCStringMap<DWARFDIE> dst_name_to_die;
  UniqueCStringMap<DWARFDIE> src_name_to_die_artificial;
  UniqueCStringMap<DWARFDIE> dst_name_to_die_artificial;
  for (DWARFDIE src_die = src_class_die.GetFirstChild(); src_die.IsValid();
       src_die = src_die.GetSibling()) {
    gather(src_die, src_name_to_die, src_name_to_die_artificial);
  }
  for (DWARFDIE dst_die = dst_class_die.GetFirstChild(); dst_die.IsValid();
       dst_die = dst_die.GetSibling()) {
    gather(dst_die, dst_name_to_die, dst_name_to_die_artificial);
  }
  const uint32_t src_size = src_name_to_die.GetSize();
  const uint32_t dst_size = dst_name_to_die.GetSize();

  // Is everything kosher so we can go through the members at top speed?
  bool fast_path = true;

  if (src_size != dst_size)
    fast_path = false;

  uint32_t idx;

  if (fast_path) {
    for (idx = 0; idx < src_size; ++idx) {
      DWARFDIE src_die = src_name_to_die.GetValueAtIndexUnchecked(idx);
      DWARFDIE dst_die = dst_name_to_die.GetValueAtIndexUnchecked(idx);

      if (src_die.Tag() != dst_die.Tag())
        fast_path = false;

      const char *src_name = src_die.GetMangledName();
      const char *dst_name = dst_die.GetMangledName();

      // Make sure the names match
      if (src_name == dst_name || (strcmp(src_name, dst_name) == 0))
        continue;

      fast_path = false;
    }
  }

  DWARFASTParserClang *src_dwarf_ast_parser =
      static_cast<DWARFASTParserClang *>(
          SymbolFileDWARF::GetDWARFParser(*src_class_die.GetCU()));
  DWARFASTParserClang *dst_dwarf_ast_parser =
      static_cast<DWARFASTParserClang *>(
          SymbolFileDWARF::GetDWARFParser(*dst_class_die.GetCU()));
  auto link = [&](DWARFDIE src, DWARFDIE dst) {
    SymbolFileDWARF::DIEToTypePtr &die_to_type =
        dst_class_die.GetDWARF()->GetDIEToType();
    clang::DeclContext *dst_decl_ctx =
        dst_dwarf_ast_parser->m_die_to_decl_ctx[dst.GetDIE()];
    if (dst_decl_ctx)
      src_dwarf_ast_parser->LinkDeclContextToDIE(dst_decl_ctx, src);

    if (Type *src_child_type = die_to_type.lookup(src.GetDIE()))
      die_to_type[dst.GetDIE()] = src_child_type;
  };

  // Now do the work of linking the DeclContexts and Types.
  if (fast_path) {
    // We can do this quickly.  Just run across the tables index-for-index
    // since we know each node has matching names and tags.
    for (idx = 0; idx < src_size; ++idx) {
      link(src_name_to_die.GetValueAtIndexUnchecked(idx),
           dst_name_to_die.GetValueAtIndexUnchecked(idx));
    }
  } else {
    // We must do this slowly.  For each member of the destination, look up a
    // member in the source with the same name, check its tag, and unique them
    // if everything matches up.  Report failures.

    if (!src_name_to_die.IsEmpty() && !dst_name_to_die.IsEmpty()) {
      src_name_to_die.Sort();

      for (idx = 0; idx < dst_size; ++idx) {
        ConstString dst_name = dst_name_to_die.GetCStringAtIndex(idx);
        DWARFDIE dst_die = dst_name_to_die.GetValueAtIndexUnchecked(idx);
        DWARFDIE src_die = src_name_to_die.Find(dst_name, DWARFDIE());

        if (src_die && (src_die.Tag() == dst_die.Tag()))
          link(src_die, dst_die);
        else
          failures.push_back(dst_die);
      }
    }
  }

  const uint32_t src_size_artificial = src_name_to_die_artificial.GetSize();
  const uint32_t dst_size_artificial = dst_name_to_die_artificial.GetSize();

  if (src_size_artificial && dst_size_artificial) {
    dst_name_to_die_artificial.Sort();

    for (idx = 0; idx < src_size_artificial; ++idx) {
      ConstString src_name_artificial =
          src_name_to_die_artificial.GetCStringAtIndex(idx);
      DWARFDIE src_die =
          src_name_to_die_artificial.GetValueAtIndexUnchecked(idx);
      DWARFDIE dst_die =
          dst_name_to_die_artificial.Find(src_name_artificial, DWARFDIE());

      // Both classes have the artificial types, link them
      if (dst_die)
        link(src_die, dst_die);
    }
  }

  if (dst_size_artificial) {
    for (idx = 0; idx < dst_size_artificial; ++idx) {
      failures.push_back(
          dst_name_to_die_artificial.GetValueAtIndexUnchecked(idx));
    }
  }

  return !failures.empty();
}

bool DWARFASTParserClang::ShouldCreateUnnamedBitfield(
    FieldInfo const &last_field_info, uint64_t last_field_end,
    FieldInfo const &this_field_info,
    lldb_private::ClangASTImporter::LayoutInfo const &layout_info) const {
  // If we have a gap between the last_field_end and the current
  // field we have an unnamed bit-field.
  if (this_field_info.bit_offset <= last_field_end)
    return false;

  // If we have a base class, we assume there is no unnamed
  // bit-field if either of the following is true:
  // (a) this is the first field since the gap can be
  // attributed to the members from the base class.
  // FIXME: This assumption is not correct if the first field of
  // the derived class is indeed an unnamed bit-field. We currently
  // do not have the machinary to track the offset of the last field
  // of classes we have seen before, so we are not handling this case.
  // (b) Or, the first member of the derived class was a vtable pointer.
  // In this case we don't want to create an unnamed bitfield either
  // since those will be inserted by clang later.
  const bool have_base = layout_info.base_offsets.size() != 0;
  const bool this_is_first_field =
      last_field_info.bit_offset == 0 && last_field_info.bit_size == 0;
  const bool first_field_is_vptr =
      last_field_info.bit_offset == 0 && last_field_info.IsArtificial();

  if (have_base && (this_is_first_field || first_field_is_vptr))
    return false;

  return true;
}

void DWARFASTParserClang::ParseRustVariantPart(
    DWARFDIE &die, const DWARFDIE &parent_die, CompilerType &class_clang_type,
    const lldb::AccessType default_accesibility,
    ClangASTImporter::LayoutInfo &layout_info) {
  assert(die.Tag() == llvm::dwarf::DW_TAG_variant_part);
  assert(SymbolFileDWARF::GetLanguage(*die.GetCU()) ==
         LanguageType::eLanguageTypeRust);

  ModuleSP module_sp = parent_die.GetDWARF()->GetObjectFile()->GetModule();

  VariantPart variants(die, parent_die, module_sp);

  auto discriminant_type =
      die.ResolveTypeUID(variants.discriminant().type_ref.Reference());

  auto decl_context = m_ast.GetDeclContextForType(class_clang_type);

  auto inner_holder = m_ast.CreateRecordType(
      decl_context, OptionalClangModuleID(), lldb::eAccessPublic,
      std::string(
          llvm::formatv("{0}$Inner", class_clang_type.GetTypeName(false))),
      llvm::to_underlying(clang::TagTypeKind::Union), lldb::eLanguageTypeRust);
  m_ast.StartTagDeclarationDefinition(inner_holder);
  m_ast.SetIsPacked(inner_holder);

  for (auto member : variants.members()) {

    auto has_discriminant = !member.IsDefault();

    auto member_type = die.ResolveTypeUID(member.type_ref.Reference());

    auto field_type = m_ast.CreateRecordType(
        m_ast.GetDeclContextForType(inner_holder), OptionalClangModuleID(),
        lldb::eAccessPublic,
        std::string(llvm::formatv("{0}$Variant", member.GetName())),
        llvm::to_underlying(clang::TagTypeKind::Struct),
        lldb::eLanguageTypeRust);

    m_ast.StartTagDeclarationDefinition(field_type);
    auto offset = member.byte_offset;

    if (has_discriminant) {
      m_ast.AddFieldToRecordType(
          field_type, "$discr$", discriminant_type->GetFullCompilerType(),
          lldb::eAccessPublic, variants.discriminant().byte_offset);
      offset += discriminant_type->GetByteSize(nullptr).value_or(0);
    }

    m_ast.AddFieldToRecordType(field_type, "value",
                               member_type->GetFullCompilerType(),
                               lldb::eAccessPublic, offset * 8);

    m_ast.CompleteTagDeclarationDefinition(field_type);

    auto name = has_discriminant
                    ? llvm::formatv("$variant${0}", member.discr_value.value())
                    : std::string("$variant$");

    auto variant_decl =
        m_ast.AddFieldToRecordType(inner_holder, llvm::StringRef(name),
                                   field_type, default_accesibility, 0);

    layout_info.field_offsets.insert({variant_decl, 0});
  }

  auto inner_field = m_ast.AddFieldToRecordType(class_clang_type,
                                                llvm::StringRef("$variants$"),
                                                inner_holder, eAccessPublic, 0);

  m_ast.CompleteTagDeclarationDefinition(inner_holder);

  layout_info.field_offsets.insert({inner_field, 0});
}
