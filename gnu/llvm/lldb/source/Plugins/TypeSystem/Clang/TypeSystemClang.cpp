//===-- TypeSystemClang.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemClang.h"

#include "clang/AST/DeclBase.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"

#include <mutex>
#include <memory>
#include <string>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTImporter.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/AST/VTableBuilder.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Sema/Sema.h"

#include "llvm/Support/Signals.h"
#include "llvm/Support/Threading.h"

#include "Plugins/ExpressionParser/Clang/ClangASTImporter.h"
#include "Plugins/ExpressionParser/Clang/ClangASTMetadata.h"
#include "Plugins/ExpressionParser/Clang/ClangExternalASTSourceCallbacks.h"
#include "Plugins/ExpressionParser/Clang/ClangFunctionCaller.h"
#include "Plugins/ExpressionParser/Clang/ClangPersistentVariables.h"
#include "Plugins/ExpressionParser/Clang/ClangUserExpression.h"
#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/ExpressionParser/Clang/ClangUtilityFunction.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Flags.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/ThreadSafeDenseMap.h"

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"
#include "Plugins/SymbolFile/DWARF/DWARFASTParserClang.h"
#include "Plugins/SymbolFile/PDB/PDBASTParser.h"
#include "Plugins/SymbolFile/NativePDB/PdbAstBuilder.h"

#include <cstdio>

#include <mutex>
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::dwarf;
using namespace lldb_private::plugin::dwarf;
using namespace clang;
using llvm::StringSwitch;

LLDB_PLUGIN_DEFINE(TypeSystemClang)

namespace {
static void VerifyDecl(clang::Decl *decl) {
  assert(decl && "VerifyDecl called with nullptr?");
#ifndef NDEBUG
  // We don't care about the actual access value here but only want to trigger
  // that Clang calls its internal Decl::AccessDeclContextCheck validation.
  decl->getAccess();
#endif
}

static inline bool
TypeSystemClangSupportsLanguage(lldb::LanguageType language) {
  return language == eLanguageTypeUnknown || // Clang is the default type system
         lldb_private::Language::LanguageIsC(language) ||
         lldb_private::Language::LanguageIsCPlusPlus(language) ||
         lldb_private::Language::LanguageIsObjC(language) ||
         lldb_private::Language::LanguageIsPascal(language) ||
         // Use Clang for Rust until there is a proper language plugin for it
         language == eLanguageTypeRust ||
         // Use Clang for D until there is a proper language plugin for it
         language == eLanguageTypeD ||
         // Open Dylan compiler debug info is designed to be Clang-compatible
         language == eLanguageTypeDylan;
}

// Checks whether m1 is an overload of m2 (as opposed to an override). This is
// called by addOverridesForMethod to distinguish overrides (which share a
// vtable entry) from overloads (which require distinct entries).
bool isOverload(clang::CXXMethodDecl *m1, clang::CXXMethodDecl *m2) {
  // FIXME: This should detect covariant return types, but currently doesn't.
  lldbassert(&m1->getASTContext() == &m2->getASTContext() &&
             "Methods should have the same AST context");
  clang::ASTContext &context = m1->getASTContext();

  const auto *m1Type = llvm::cast<clang::FunctionProtoType>(
      context.getCanonicalType(m1->getType()));

  const auto *m2Type = llvm::cast<clang::FunctionProtoType>(
      context.getCanonicalType(m2->getType()));

  auto compareArgTypes = [&context](const clang::QualType &m1p,
                                    const clang::QualType &m2p) {
    return context.hasSameType(m1p.getUnqualifiedType(),
                               m2p.getUnqualifiedType());
  };

  // FIXME: In C++14 and later, we can just pass m2Type->param_type_end()
  //        as a fourth parameter to std::equal().
  return (m1->getNumParams() != m2->getNumParams()) ||
         !std::equal(m1Type->param_type_begin(), m1Type->param_type_end(),
                     m2Type->param_type_begin(), compareArgTypes);
}

// If decl is a virtual method, walk the base classes looking for methods that
// decl overrides. This table of overridden methods is used by IRGen to
// determine the vtable layout for decl's parent class.
void addOverridesForMethod(clang::CXXMethodDecl *decl) {
  if (!decl->isVirtual())
    return;

  clang::CXXBasePaths paths;
  llvm::SmallVector<clang::NamedDecl *, 4> decls;

  auto find_overridden_methods =
      [&decls, decl](const clang::CXXBaseSpecifier *specifier,
                     clang::CXXBasePath &path) {
        if (auto *base_record = llvm::dyn_cast<clang::CXXRecordDecl>(
                specifier->getType()->castAs<clang::RecordType>()->getDecl())) {

          clang::DeclarationName name = decl->getDeclName();

          // If this is a destructor, check whether the base class destructor is
          // virtual.
          if (name.getNameKind() == clang::DeclarationName::CXXDestructorName)
            if (auto *baseDtorDecl = base_record->getDestructor()) {
              if (baseDtorDecl->isVirtual()) {
                decls.push_back(baseDtorDecl);
                return true;
              } else
                return false;
            }

          // Otherwise, search for name in the base class.
          for (path.Decls = base_record->lookup(name).begin();
               path.Decls != path.Decls.end(); ++path.Decls) {
            if (auto *method_decl =
                    llvm::dyn_cast<clang::CXXMethodDecl>(*path.Decls))
              if (method_decl->isVirtual() && !isOverload(decl, method_decl)) {
                decls.push_back(method_decl);
                return true;
              }
          }
        }

        return false;
      };

  if (decl->getParent()->lookupInBases(find_overridden_methods, paths)) {
    for (auto *overridden_decl : decls)
      decl->addOverriddenMethod(
          llvm::cast<clang::CXXMethodDecl>(overridden_decl));
  }
}
}

static lldb::addr_t GetVTableAddress(Process &process,
                                     VTableContextBase &vtable_ctx,
                                     ValueObject &valobj,
                                     const ASTRecordLayout &record_layout) {
  // Retrieve type info
  CompilerType pointee_type;
  CompilerType this_type(valobj.GetCompilerType());
  uint32_t type_info = this_type.GetTypeInfo(&pointee_type);
  if (!type_info)
    return LLDB_INVALID_ADDRESS;

  // Check if it's a pointer or reference
  bool ptr_or_ref = false;
  if (type_info & (eTypeIsPointer | eTypeIsReference)) {
    ptr_or_ref = true;
    type_info = pointee_type.GetTypeInfo();
  }

  // We process only C++ classes
  const uint32_t cpp_class = eTypeIsClass | eTypeIsCPlusPlus;
  if ((type_info & cpp_class) != cpp_class)
    return LLDB_INVALID_ADDRESS;

  // Calculate offset to VTable pointer
  lldb::offset_t vbtable_ptr_offset =
      vtable_ctx.isMicrosoft() ? record_layout.getVBPtrOffset().getQuantity()
                               : 0;

  if (ptr_or_ref) {
    // We have a pointer / ref to object, so read
    // VTable pointer from process memory

    if (valobj.GetAddressTypeOfChildren() != eAddressTypeLoad)
      return LLDB_INVALID_ADDRESS;

    auto vbtable_ptr_addr = valobj.GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
    if (vbtable_ptr_addr == LLDB_INVALID_ADDRESS)
      return LLDB_INVALID_ADDRESS;

    vbtable_ptr_addr += vbtable_ptr_offset;

    Status err;
    return process.ReadPointerFromMemory(vbtable_ptr_addr, err);
  }

  // We have an object already read from process memory,
  // so just extract VTable pointer from it

  DataExtractor data;
  Status err;
  auto size = valobj.GetData(data, err);
  if (err.Fail() || vbtable_ptr_offset + data.GetAddressByteSize() > size)
    return LLDB_INVALID_ADDRESS;

  return data.GetAddress(&vbtable_ptr_offset);
}

static int64_t ReadVBaseOffsetFromVTable(Process &process,
                                         VTableContextBase &vtable_ctx,
                                         lldb::addr_t vtable_ptr,
                                         const CXXRecordDecl *cxx_record_decl,
                                         const CXXRecordDecl *base_class_decl) {
  if (vtable_ctx.isMicrosoft()) {
    clang::MicrosoftVTableContext &msoft_vtable_ctx =
        static_cast<clang::MicrosoftVTableContext &>(vtable_ctx);

    // Get the index into the virtual base table. The
    // index is the index in uint32_t from vbtable_ptr
    const unsigned vbtable_index =
        msoft_vtable_ctx.getVBTableIndex(cxx_record_decl, base_class_decl);
    const lldb::addr_t base_offset_addr = vtable_ptr + vbtable_index * 4;
    Status err;
    return process.ReadSignedIntegerFromMemory(base_offset_addr, 4, INT64_MAX,
                                               err);
  }

  clang::ItaniumVTableContext &itanium_vtable_ctx =
      static_cast<clang::ItaniumVTableContext &>(vtable_ctx);

  clang::CharUnits base_offset_offset =
      itanium_vtable_ctx.getVirtualBaseOffsetOffset(cxx_record_decl,
                                                    base_class_decl);
  const lldb::addr_t base_offset_addr =
      vtable_ptr + base_offset_offset.getQuantity();
  const uint32_t base_offset_size = process.GetAddressByteSize();
  Status err;
  return process.ReadSignedIntegerFromMemory(base_offset_addr, base_offset_size,
                                             INT64_MAX, err);
}

static bool GetVBaseBitOffset(VTableContextBase &vtable_ctx,
                              ValueObject &valobj,
                              const ASTRecordLayout &record_layout,
                              const CXXRecordDecl *cxx_record_decl,
                              const CXXRecordDecl *base_class_decl,
                              int32_t &bit_offset) {
  ExecutionContext exe_ctx(valobj.GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();
  if (!process)
    return false;

  lldb::addr_t vtable_ptr =
      GetVTableAddress(*process, vtable_ctx, valobj, record_layout);
  if (vtable_ptr == LLDB_INVALID_ADDRESS)
    return false;

  auto base_offset = ReadVBaseOffsetFromVTable(
      *process, vtable_ctx, vtable_ptr, cxx_record_decl, base_class_decl);
  if (base_offset == INT64_MAX)
    return false;

  bit_offset = base_offset * 8;

  return true;
}

typedef lldb_private::ThreadSafeDenseMap<clang::ASTContext *, TypeSystemClang *>
    ClangASTMap;

static ClangASTMap &GetASTMap() {
  static ClangASTMap *g_map_ptr = nullptr;
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    g_map_ptr = new ClangASTMap(); // leaked on purpose to avoid spins
  });
  return *g_map_ptr;
}

TypePayloadClang::TypePayloadClang(OptionalClangModuleID owning_module,
                                   bool is_complete_objc_class)
    : m_payload(owning_module.GetValue()) {
  SetIsCompleteObjCClass(is_complete_objc_class);
}

void TypePayloadClang::SetOwningModule(OptionalClangModuleID id) {
  assert(id.GetValue() < ObjCClassBit);
  bool is_complete = IsCompleteObjCClass();
  m_payload = id.GetValue();
  SetIsCompleteObjCClass(is_complete);
}

static void SetMemberOwningModule(clang::Decl *member,
                                  const clang::Decl *parent) {
  if (!member || !parent)
    return;

  OptionalClangModuleID id(parent->getOwningModuleID());
  if (!id.HasValue())
    return;

  member->setFromASTFile();
  member->setOwningModuleID(id.GetValue());
  member->setModuleOwnershipKind(clang::Decl::ModuleOwnershipKind::Visible);
  if (llvm::isa<clang::NamedDecl>(member))
    if (auto *dc = llvm::dyn_cast<clang::DeclContext>(parent)) {
      dc->setHasExternalVisibleStorage(true);
      // This triggers ExternalASTSource::FindExternalVisibleDeclsByName() to be
      // called when searching for members.
      dc->setHasExternalLexicalStorage(true);
    }
}

char TypeSystemClang::ID;

bool TypeSystemClang::IsOperator(llvm::StringRef name,
                                 clang::OverloadedOperatorKind &op_kind) {
  // All operators have to start with "operator".
  if (!name.consume_front("operator"))
    return false;

  // Remember if there was a space after "operator". This is necessary to
  // check for collisions with strangely named functions like "operatorint()".
  bool space_after_operator = name.consume_front(" ");

  op_kind = StringSwitch<clang::OverloadedOperatorKind>(name)
                .Case("+", clang::OO_Plus)
                .Case("+=", clang::OO_PlusEqual)
                .Case("++", clang::OO_PlusPlus)
                .Case("-", clang::OO_Minus)
                .Case("-=", clang::OO_MinusEqual)
                .Case("--", clang::OO_MinusMinus)
                .Case("->", clang::OO_Arrow)
                .Case("->*", clang::OO_ArrowStar)
                .Case("*", clang::OO_Star)
                .Case("*=", clang::OO_StarEqual)
                .Case("/", clang::OO_Slash)
                .Case("/=", clang::OO_SlashEqual)
                .Case("%", clang::OO_Percent)
                .Case("%=", clang::OO_PercentEqual)
                .Case("^", clang::OO_Caret)
                .Case("^=", clang::OO_CaretEqual)
                .Case("&", clang::OO_Amp)
                .Case("&=", clang::OO_AmpEqual)
                .Case("&&", clang::OO_AmpAmp)
                .Case("|", clang::OO_Pipe)
                .Case("|=", clang::OO_PipeEqual)
                .Case("||", clang::OO_PipePipe)
                .Case("~", clang::OO_Tilde)
                .Case("!", clang::OO_Exclaim)
                .Case("!=", clang::OO_ExclaimEqual)
                .Case("=", clang::OO_Equal)
                .Case("==", clang::OO_EqualEqual)
                .Case("<", clang::OO_Less)
                .Case("<=>", clang::OO_Spaceship)
                .Case("<<", clang::OO_LessLess)
                .Case("<<=", clang::OO_LessLessEqual)
                .Case("<=", clang::OO_LessEqual)
                .Case(">", clang::OO_Greater)
                .Case(">>", clang::OO_GreaterGreater)
                .Case(">>=", clang::OO_GreaterGreaterEqual)
                .Case(">=", clang::OO_GreaterEqual)
                .Case("()", clang::OO_Call)
                .Case("[]", clang::OO_Subscript)
                .Case(",", clang::OO_Comma)
                .Default(clang::NUM_OVERLOADED_OPERATORS);

  // We found a fitting operator, so we can exit now.
  if (op_kind != clang::NUM_OVERLOADED_OPERATORS)
    return true;

  // After the "operator " or "operator" part is something unknown. This means
  // it's either one of the named operators (new/delete), a conversion operator
  // (e.g. operator bool) or a function which name starts with "operator"
  // (e.g. void operatorbool).

  // If it's a function that starts with operator it can't have a space after
  // "operator" because identifiers can't contain spaces.
  // E.g. "operator int" (conversion operator)
  //  vs. "operatorint" (function with colliding name).
  if (!space_after_operator)
    return false; // not an operator.

  // Now the operator is either one of the named operators or a conversion
  // operator.
  op_kind = StringSwitch<clang::OverloadedOperatorKind>(name)
                .Case("new", clang::OO_New)
                .Case("new[]", clang::OO_Array_New)
                .Case("delete", clang::OO_Delete)
                .Case("delete[]", clang::OO_Array_Delete)
                // conversion operators hit this case.
                .Default(clang::NUM_OVERLOADED_OPERATORS);

  return true;
}

clang::AccessSpecifier
TypeSystemClang::ConvertAccessTypeToAccessSpecifier(AccessType access) {
  switch (access) {
  default:
    break;
  case eAccessNone:
    return AS_none;
  case eAccessPublic:
    return AS_public;
  case eAccessPrivate:
    return AS_private;
  case eAccessProtected:
    return AS_protected;
  }
  return AS_none;
}

static void ParseLangArgs(LangOptions &Opts, ArchSpec arch) {
  // FIXME: Cleanup per-file based stuff.

  std::vector<std::string> Includes;
  LangOptions::setLangDefaults(Opts, clang::Language::ObjCXX, arch.GetTriple(),
                               Includes, clang::LangStandard::lang_gnucxx98);

  Opts.setValueVisibilityMode(DefaultVisibility);

  // Mimicing gcc's behavior, trigraphs are only enabled if -trigraphs is
  // specified, or -std is set to a conforming mode.
  Opts.Trigraphs = !Opts.GNUMode;
  Opts.CharIsSigned = arch.CharIsSignedByDefault();
  Opts.OptimizeSize = 0;

  // FIXME: Eliminate this dependency.
  //    unsigned Opt =
  //    Args.hasArg(OPT_Os) ? 2 : getLastArgIntValue(Args, OPT_O, 0, Diags);
  //    Opts.Optimize = Opt != 0;
  unsigned Opt = 0;

  // This is the __NO_INLINE__ define, which just depends on things like the
  // optimization level and -fno-inline, not actually whether the backend has
  // inlining enabled.
  //
  // FIXME: This is affected by other options (-fno-inline).
  Opts.NoInlineDefine = !Opt;

  // This is needed to allocate the extra space for the owning module
  // on each decl.
  Opts.ModulesLocalVisibility = 1;
}

TypeSystemClang::TypeSystemClang(llvm::StringRef name,
                                 llvm::Triple target_triple) {
  m_display_name = name.str();
  if (!target_triple.str().empty())
    SetTargetTriple(target_triple.str());
  // The caller didn't pass an ASTContext so create a new one for this
  // TypeSystemClang.
  CreateASTContext();

  LogCreation();
}

TypeSystemClang::TypeSystemClang(llvm::StringRef name,
                                 ASTContext &existing_ctxt) {
  m_display_name = name.str();
  SetTargetTriple(existing_ctxt.getTargetInfo().getTriple().str());

  m_ast_up.reset(&existing_ctxt);
  GetASTMap().Insert(&existing_ctxt, this);

  LogCreation();
}

// Destructor
TypeSystemClang::~TypeSystemClang() { Finalize(); }

lldb::TypeSystemSP TypeSystemClang::CreateInstance(lldb::LanguageType language,
                                                   lldb_private::Module *module,
                                                   Target *target) {
  if (!TypeSystemClangSupportsLanguage(language))
    return lldb::TypeSystemSP();
  ArchSpec arch;
  if (module)
    arch = module->GetArchitecture();
  else if (target)
    arch = target->GetArchitecture();

  if (!arch.IsValid())
    return lldb::TypeSystemSP();

  llvm::Triple triple = arch.GetTriple();
  // LLVM wants this to be set to iOS or MacOSX; if we're working on
  // a bare-boards type image, change the triple for llvm's benefit.
  if (triple.getVendor() == llvm::Triple::Apple &&
      triple.getOS() == llvm::Triple::UnknownOS) {
    if (triple.getArch() == llvm::Triple::arm ||
        triple.getArch() == llvm::Triple::aarch64 ||
        triple.getArch() == llvm::Triple::aarch64_32 ||
        triple.getArch() == llvm::Triple::thumb) {
      triple.setOS(llvm::Triple::IOS);
    } else {
      triple.setOS(llvm::Triple::MacOSX);
    }
  }

  if (module) {
    std::string ast_name =
        "ASTContext for '" + module->GetFileSpec().GetPath() + "'";
    return std::make_shared<TypeSystemClang>(ast_name, triple);
  } else if (target && target->IsValid())
    return std::make_shared<ScratchTypeSystemClang>(*target, triple);
  return lldb::TypeSystemSP();
}

LanguageSet TypeSystemClang::GetSupportedLanguagesForTypes() {
  LanguageSet languages;
  languages.Insert(lldb::eLanguageTypeC89);
  languages.Insert(lldb::eLanguageTypeC);
  languages.Insert(lldb::eLanguageTypeC11);
  languages.Insert(lldb::eLanguageTypeC_plus_plus);
  languages.Insert(lldb::eLanguageTypeC99);
  languages.Insert(lldb::eLanguageTypeObjC);
  languages.Insert(lldb::eLanguageTypeObjC_plus_plus);
  languages.Insert(lldb::eLanguageTypeC_plus_plus_03);
  languages.Insert(lldb::eLanguageTypeC_plus_plus_11);
  languages.Insert(lldb::eLanguageTypeC11);
  languages.Insert(lldb::eLanguageTypeC_plus_plus_14);
  languages.Insert(lldb::eLanguageTypeC_plus_plus_17);
  languages.Insert(lldb::eLanguageTypeC_plus_plus_20);
  return languages;
}

LanguageSet TypeSystemClang::GetSupportedLanguagesForExpressions() {
  LanguageSet languages;
  languages.Insert(lldb::eLanguageTypeC_plus_plus);
  languages.Insert(lldb::eLanguageTypeObjC_plus_plus);
  languages.Insert(lldb::eLanguageTypeC_plus_plus_03);
  languages.Insert(lldb::eLanguageTypeC_plus_plus_11);
  languages.Insert(lldb::eLanguageTypeC_plus_plus_14);
  languages.Insert(lldb::eLanguageTypeC_plus_plus_17);
  languages.Insert(lldb::eLanguageTypeC_plus_plus_20);
  return languages;
}

void TypeSystemClang::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "clang base AST context plug-in", CreateInstance,
      GetSupportedLanguagesForTypes(), GetSupportedLanguagesForExpressions());
}

void TypeSystemClang::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

void TypeSystemClang::Finalize() {
  assert(m_ast_up);
  GetASTMap().Erase(m_ast_up.get());
  if (!m_ast_owned)
    m_ast_up.release();

  m_builtins_up.reset();
  m_selector_table_up.reset();
  m_identifier_table_up.reset();
  m_target_info_up.reset();
  m_target_options_rp.reset();
  m_diagnostics_engine_up.reset();
  m_source_manager_up.reset();
  m_language_options_up.reset();
}

void TypeSystemClang::setSema(Sema *s) {
  // Ensure that the new sema actually belongs to our ASTContext.
  assert(s == nullptr || &s->getASTContext() == m_ast_up.get());
  m_sema = s;
}

const char *TypeSystemClang::GetTargetTriple() {
  return m_target_triple.c_str();
}

void TypeSystemClang::SetTargetTriple(llvm::StringRef target_triple) {
  m_target_triple = target_triple.str();
}

void TypeSystemClang::SetExternalSource(
    llvm::IntrusiveRefCntPtr<ExternalASTSource> &ast_source_up) {
  ASTContext &ast = getASTContext();
  ast.getTranslationUnitDecl()->setHasExternalLexicalStorage(true);
  ast.setExternalSource(ast_source_up);
}

ASTContext &TypeSystemClang::getASTContext() const {
  assert(m_ast_up);
  return *m_ast_up;
}

class NullDiagnosticConsumer : public DiagnosticConsumer {
public:
  NullDiagnosticConsumer() { m_log = GetLog(LLDBLog::Expressions); }

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const clang::Diagnostic &info) override {
    if (m_log) {
      llvm::SmallVector<char, 32> diag_str(10);
      info.FormatDiagnostic(diag_str);
      diag_str.push_back('\0');
      LLDB_LOGF(m_log, "Compiler diagnostic: %s\n", diag_str.data());
    }
  }

  DiagnosticConsumer *clone(DiagnosticsEngine &Diags) const {
    return new NullDiagnosticConsumer();
  }

private:
  Log *m_log;
};

void TypeSystemClang::CreateASTContext() {
  assert(!m_ast_up);
  m_ast_owned = true;

  m_language_options_up = std::make_unique<LangOptions>();
  ParseLangArgs(*m_language_options_up, ArchSpec(GetTargetTriple()));

  m_identifier_table_up =
      std::make_unique<IdentifierTable>(*m_language_options_up, nullptr);
  m_builtins_up = std::make_unique<Builtin::Context>();

  m_selector_table_up = std::make_unique<SelectorTable>();

  clang::FileSystemOptions file_system_options;
  m_file_manager_up = std::make_unique<clang::FileManager>(
      file_system_options, FileSystem::Instance().GetVirtualFileSystem());

  llvm::IntrusiveRefCntPtr<DiagnosticIDs> diag_id_sp(new DiagnosticIDs());
  m_diagnostics_engine_up =
      std::make_unique<DiagnosticsEngine>(diag_id_sp, new DiagnosticOptions());

  m_source_manager_up = std::make_unique<clang::SourceManager>(
      *m_diagnostics_engine_up, *m_file_manager_up);
  m_ast_up = std::make_unique<ASTContext>(
      *m_language_options_up, *m_source_manager_up, *m_identifier_table_up,
      *m_selector_table_up, *m_builtins_up, TU_Complete);

  m_diagnostic_consumer_up = std::make_unique<NullDiagnosticConsumer>();
  m_ast_up->getDiagnostics().setClient(m_diagnostic_consumer_up.get(), false);

  // This can be NULL if we don't know anything about the architecture or if
  // the target for an architecture isn't enabled in the llvm/clang that we
  // built
  TargetInfo *target_info = getTargetInfo();
  if (target_info)
    m_ast_up->InitBuiltinTypes(*target_info);

  GetASTMap().Insert(m_ast_up.get(), this);

  llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> ast_source_up(
      new ClangExternalASTSourceCallbacks(*this));
  SetExternalSource(ast_source_up);
}

TypeSystemClang *TypeSystemClang::GetASTContext(clang::ASTContext *ast) {
  TypeSystemClang *clang_ast = GetASTMap().Lookup(ast);
  return clang_ast;
}

clang::MangleContext *TypeSystemClang::getMangleContext() {
  if (m_mangle_ctx_up == nullptr)
    m_mangle_ctx_up.reset(getASTContext().createMangleContext());
  return m_mangle_ctx_up.get();
}

std::shared_ptr<clang::TargetOptions> &TypeSystemClang::getTargetOptions() {
  if (m_target_options_rp == nullptr && !m_target_triple.empty()) {
    m_target_options_rp = std::make_shared<clang::TargetOptions>();
    if (m_target_options_rp != nullptr)
      m_target_options_rp->Triple = m_target_triple;
  }
  return m_target_options_rp;
}

TargetInfo *TypeSystemClang::getTargetInfo() {
  // target_triple should be something like "x86_64-apple-macosx"
  if (m_target_info_up == nullptr && !m_target_triple.empty())
    m_target_info_up.reset(TargetInfo::CreateTargetInfo(
        getASTContext().getDiagnostics(), getTargetOptions()));
  return m_target_info_up.get();
}

#pragma mark Basic Types

static inline bool QualTypeMatchesBitSize(const uint64_t bit_size,
                                          ASTContext &ast, QualType qual_type) {
  uint64_t qual_type_bit_size = ast.getTypeSize(qual_type);
  return qual_type_bit_size == bit_size;
}

CompilerType
TypeSystemClang::GetBuiltinTypeForEncodingAndBitSize(Encoding encoding,
                                                     size_t bit_size) {
  ASTContext &ast = getASTContext();
  switch (encoding) {
  case eEncodingInvalid:
    if (QualTypeMatchesBitSize(bit_size, ast, ast.VoidPtrTy))
      return GetType(ast.VoidPtrTy);
    break;

  case eEncodingUint:
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedCharTy))
      return GetType(ast.UnsignedCharTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedShortTy))
      return GetType(ast.UnsignedShortTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedIntTy))
      return GetType(ast.UnsignedIntTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedLongTy))
      return GetType(ast.UnsignedLongTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedLongLongTy))
      return GetType(ast.UnsignedLongLongTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedInt128Ty))
      return GetType(ast.UnsignedInt128Ty);
    break;

  case eEncodingSint:
    if (QualTypeMatchesBitSize(bit_size, ast, ast.SignedCharTy))
      return GetType(ast.SignedCharTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.ShortTy))
      return GetType(ast.ShortTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.IntTy))
      return GetType(ast.IntTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.LongTy))
      return GetType(ast.LongTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.LongLongTy))
      return GetType(ast.LongLongTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.Int128Ty))
      return GetType(ast.Int128Ty);
    break;

  case eEncodingIEEE754:
    if (QualTypeMatchesBitSize(bit_size, ast, ast.FloatTy))
      return GetType(ast.FloatTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.DoubleTy))
      return GetType(ast.DoubleTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.LongDoubleTy))
      return GetType(ast.LongDoubleTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.HalfTy))
      return GetType(ast.HalfTy);
    break;

  case eEncodingVector:
    // Sanity check that bit_size is a multiple of 8's.
    if (bit_size && !(bit_size & 0x7u))
      return GetType(ast.getExtVectorType(ast.UnsignedCharTy, bit_size / 8));
    break;
  }

  return CompilerType();
}

lldb::BasicType TypeSystemClang::GetBasicTypeEnumeration(llvm::StringRef name) {
  static const llvm::StringMap<lldb::BasicType> g_type_map = {
      // "void"
      {"void", eBasicTypeVoid},

      // "char"
      {"char", eBasicTypeChar},
      {"signed char", eBasicTypeSignedChar},
      {"unsigned char", eBasicTypeUnsignedChar},
      {"wchar_t", eBasicTypeWChar},
      {"signed wchar_t", eBasicTypeSignedWChar},
      {"unsigned wchar_t", eBasicTypeUnsignedWChar},

      // "short"
      {"short", eBasicTypeShort},
      {"short int", eBasicTypeShort},
      {"unsigned short", eBasicTypeUnsignedShort},
      {"unsigned short int", eBasicTypeUnsignedShort},

      // "int"
      {"int", eBasicTypeInt},
      {"signed int", eBasicTypeInt},
      {"unsigned int", eBasicTypeUnsignedInt},
      {"unsigned", eBasicTypeUnsignedInt},

      // "long"
      {"long", eBasicTypeLong},
      {"long int", eBasicTypeLong},
      {"unsigned long", eBasicTypeUnsignedLong},
      {"unsigned long int", eBasicTypeUnsignedLong},

      // "long long"
      {"long long", eBasicTypeLongLong},
      {"long long int", eBasicTypeLongLong},
      {"unsigned long long", eBasicTypeUnsignedLongLong},
      {"unsigned long long int", eBasicTypeUnsignedLongLong},

      // "int128"
      {"__int128_t", eBasicTypeInt128},
      {"__uint128_t", eBasicTypeUnsignedInt128},

      // "bool"
      {"bool", eBasicTypeBool},
      {"_Bool", eBasicTypeBool},

      // Miscellaneous
      {"float", eBasicTypeFloat},
      {"double", eBasicTypeDouble},
      {"long double", eBasicTypeLongDouble},
      {"id", eBasicTypeObjCID},
      {"SEL", eBasicTypeObjCSel},
      {"nullptr", eBasicTypeNullPtr},
  };

  auto iter = g_type_map.find(name);
  if (iter == g_type_map.end())
    return eBasicTypeInvalid;

  return iter->second;
}

uint32_t TypeSystemClang::GetPointerByteSize() {
  if (m_pointer_byte_size == 0)
    if (auto size = GetBasicType(lldb::eBasicTypeVoid)
                        .GetPointerType()
                        .GetByteSize(nullptr))
      m_pointer_byte_size = *size;
  return m_pointer_byte_size;
}

CompilerType TypeSystemClang::GetBasicType(lldb::BasicType basic_type) {
  clang::ASTContext &ast = getASTContext();

  lldb::opaque_compiler_type_t clang_type =
      GetOpaqueCompilerType(&ast, basic_type);

  if (clang_type)
    return CompilerType(weak_from_this(), clang_type);
  return CompilerType();
}

CompilerType TypeSystemClang::GetBuiltinTypeForDWARFEncodingAndBitSize(
    llvm::StringRef type_name, uint32_t dw_ate, uint32_t bit_size) {
  ASTContext &ast = getASTContext();

  switch (dw_ate) {
  default:
    break;

  case DW_ATE_address:
    if (QualTypeMatchesBitSize(bit_size, ast, ast.VoidPtrTy))
      return GetType(ast.VoidPtrTy);
    break;

  case DW_ATE_boolean:
    if (QualTypeMatchesBitSize(bit_size, ast, ast.BoolTy))
      return GetType(ast.BoolTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedCharTy))
      return GetType(ast.UnsignedCharTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedShortTy))
      return GetType(ast.UnsignedShortTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedIntTy))
      return GetType(ast.UnsignedIntTy);
    break;

  case DW_ATE_lo_user:
    // This has been seen to mean DW_AT_complex_integer
    if (type_name.contains("complex")) {
      CompilerType complex_int_clang_type =
          GetBuiltinTypeForDWARFEncodingAndBitSize("int", DW_ATE_signed,
                                                   bit_size / 2);
      return GetType(
          ast.getComplexType(ClangUtil::GetQualType(complex_int_clang_type)));
    }
    break;

  case DW_ATE_complex_float: {
    CanQualType FloatComplexTy = ast.getComplexType(ast.FloatTy);
    if (QualTypeMatchesBitSize(bit_size, ast, FloatComplexTy))
      return GetType(FloatComplexTy);

    CanQualType DoubleComplexTy = ast.getComplexType(ast.DoubleTy);
    if (QualTypeMatchesBitSize(bit_size, ast, DoubleComplexTy))
      return GetType(DoubleComplexTy);

    CanQualType LongDoubleComplexTy = ast.getComplexType(ast.LongDoubleTy);
    if (QualTypeMatchesBitSize(bit_size, ast, LongDoubleComplexTy))
      return GetType(LongDoubleComplexTy);

    CompilerType complex_float_clang_type =
        GetBuiltinTypeForDWARFEncodingAndBitSize("float", DW_ATE_float,
                                                 bit_size / 2);
    return GetType(
        ast.getComplexType(ClangUtil::GetQualType(complex_float_clang_type)));
  }

  case DW_ATE_float:
    if (type_name == "float" &&
        QualTypeMatchesBitSize(bit_size, ast, ast.FloatTy))
      return GetType(ast.FloatTy);
    if (type_name == "double" &&
        QualTypeMatchesBitSize(bit_size, ast, ast.DoubleTy))
      return GetType(ast.DoubleTy);
    if (type_name == "long double" &&
        QualTypeMatchesBitSize(bit_size, ast, ast.LongDoubleTy))
      return GetType(ast.LongDoubleTy);
    // Fall back to not requiring a name match
    if (QualTypeMatchesBitSize(bit_size, ast, ast.FloatTy))
      return GetType(ast.FloatTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.DoubleTy))
      return GetType(ast.DoubleTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.LongDoubleTy))
      return GetType(ast.LongDoubleTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.HalfTy))
      return GetType(ast.HalfTy);
    break;

  case DW_ATE_signed:
    if (!type_name.empty()) {
      if (type_name == "wchar_t" &&
          QualTypeMatchesBitSize(bit_size, ast, ast.WCharTy) &&
          (getTargetInfo() &&
           TargetInfo::isTypeSigned(getTargetInfo()->getWCharType())))
        return GetType(ast.WCharTy);
      if (type_name == "void" &&
          QualTypeMatchesBitSize(bit_size, ast, ast.VoidTy))
        return GetType(ast.VoidTy);
      if (type_name.contains("long long") &&
          QualTypeMatchesBitSize(bit_size, ast, ast.LongLongTy))
        return GetType(ast.LongLongTy);
      if (type_name.contains("long") &&
          QualTypeMatchesBitSize(bit_size, ast, ast.LongTy))
        return GetType(ast.LongTy);
      if (type_name.contains("short") &&
          QualTypeMatchesBitSize(bit_size, ast, ast.ShortTy))
        return GetType(ast.ShortTy);
      if (type_name.contains("char")) {
        if (QualTypeMatchesBitSize(bit_size, ast, ast.CharTy))
          return GetType(ast.CharTy);
        if (QualTypeMatchesBitSize(bit_size, ast, ast.SignedCharTy))
          return GetType(ast.SignedCharTy);
      }
      if (type_name.contains("int")) {
        if (QualTypeMatchesBitSize(bit_size, ast, ast.IntTy))
          return GetType(ast.IntTy);
        if (QualTypeMatchesBitSize(bit_size, ast, ast.Int128Ty))
          return GetType(ast.Int128Ty);
      }
    }
    // We weren't able to match up a type name, just search by size
    if (QualTypeMatchesBitSize(bit_size, ast, ast.CharTy))
      return GetType(ast.CharTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.ShortTy))
      return GetType(ast.ShortTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.IntTy))
      return GetType(ast.IntTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.LongTy))
      return GetType(ast.LongTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.LongLongTy))
      return GetType(ast.LongLongTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.Int128Ty))
      return GetType(ast.Int128Ty);
    break;

  case DW_ATE_signed_char:
    if (type_name == "char") {
      if (QualTypeMatchesBitSize(bit_size, ast, ast.CharTy))
        return GetType(ast.CharTy);
    }
    if (QualTypeMatchesBitSize(bit_size, ast, ast.SignedCharTy))
      return GetType(ast.SignedCharTy);
    break;

  case DW_ATE_unsigned:
    if (!type_name.empty()) {
      if (type_name == "wchar_t") {
        if (QualTypeMatchesBitSize(bit_size, ast, ast.WCharTy)) {
          if (!(getTargetInfo() &&
                TargetInfo::isTypeSigned(getTargetInfo()->getWCharType())))
            return GetType(ast.WCharTy);
        }
      }
      if (type_name.contains("long long")) {
        if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedLongLongTy))
          return GetType(ast.UnsignedLongLongTy);
      } else if (type_name.contains("long")) {
        if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedLongTy))
          return GetType(ast.UnsignedLongTy);
      } else if (type_name.contains("short")) {
        if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedShortTy))
          return GetType(ast.UnsignedShortTy);
      } else if (type_name.contains("char")) {
        if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedCharTy))
          return GetType(ast.UnsignedCharTy);
      } else if (type_name.contains("int")) {
        if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedIntTy))
          return GetType(ast.UnsignedIntTy);
        if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedInt128Ty))
          return GetType(ast.UnsignedInt128Ty);
      }
    }
    // We weren't able to match up a type name, just search by size
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedCharTy))
      return GetType(ast.UnsignedCharTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedShortTy))
      return GetType(ast.UnsignedShortTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedIntTy))
      return GetType(ast.UnsignedIntTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedLongTy))
      return GetType(ast.UnsignedLongTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedLongLongTy))
      return GetType(ast.UnsignedLongLongTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedInt128Ty))
      return GetType(ast.UnsignedInt128Ty);
    break;

  case DW_ATE_unsigned_char:
    if (type_name == "char") {
      if (QualTypeMatchesBitSize(bit_size, ast, ast.CharTy))
        return GetType(ast.CharTy);
    }
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedCharTy))
      return GetType(ast.UnsignedCharTy);
    if (QualTypeMatchesBitSize(bit_size, ast, ast.UnsignedShortTy))
      return GetType(ast.UnsignedShortTy);
    break;

  case DW_ATE_imaginary_float:
    break;

  case DW_ATE_UTF:
    switch (bit_size) {
    case 8:
      return GetType(ast.Char8Ty);
    case 16:
      return GetType(ast.Char16Ty);
    case 32:
      return GetType(ast.Char32Ty);
    default:
      if (!type_name.empty()) {
        if (type_name == "char16_t")
          return GetType(ast.Char16Ty);
        if (type_name == "char32_t")
          return GetType(ast.Char32Ty);
        if (type_name == "char8_t")
          return GetType(ast.Char8Ty);
      }
    }
    break;
  }

  Log *log = GetLog(LLDBLog::Types);
  LLDB_LOG(log,
           "error: need to add support for DW_TAG_base_type '{0}' "
           "encoded with DW_ATE = {1:x}, bit_size = {2}",
           type_name, dw_ate, bit_size);
  return CompilerType();
}

CompilerType TypeSystemClang::GetCStringType(bool is_const) {
  ASTContext &ast = getASTContext();
  QualType char_type(ast.CharTy);

  if (is_const)
    char_type.addConst();

  return GetType(ast.getPointerType(char_type));
}

bool TypeSystemClang::AreTypesSame(CompilerType type1, CompilerType type2,
                                   bool ignore_qualifiers) {
  auto ast = type1.GetTypeSystem().dyn_cast_or_null<TypeSystemClang>();
  if (!ast || type1.GetTypeSystem() != type2.GetTypeSystem())
    return false;

  if (type1.GetOpaqueQualType() == type2.GetOpaqueQualType())
    return true;

  QualType type1_qual = ClangUtil::GetQualType(type1);
  QualType type2_qual = ClangUtil::GetQualType(type2);

  if (ignore_qualifiers) {
    type1_qual = type1_qual.getUnqualifiedType();
    type2_qual = type2_qual.getUnqualifiedType();
  }

  return ast->getASTContext().hasSameType(type1_qual, type2_qual);
}

CompilerType TypeSystemClang::GetTypeForDecl(void *opaque_decl) {
  if (!opaque_decl)
    return CompilerType();

  clang::Decl *decl = static_cast<clang::Decl *>(opaque_decl);
  if (auto *named_decl = llvm::dyn_cast<clang::NamedDecl>(decl))
    return GetTypeForDecl(named_decl);
  return CompilerType();
}

CompilerDeclContext TypeSystemClang::CreateDeclContext(DeclContext *ctx) {
  // Check that the DeclContext actually belongs to this ASTContext.
  assert(&ctx->getParentASTContext() == &getASTContext());
  return CompilerDeclContext(this, ctx);
}

CompilerType TypeSystemClang::GetTypeForDecl(clang::NamedDecl *decl) {
  if (clang::ObjCInterfaceDecl *interface_decl =
      llvm::dyn_cast<clang::ObjCInterfaceDecl>(decl))
    return GetTypeForDecl(interface_decl);
  if (clang::TagDecl *tag_decl = llvm::dyn_cast<clang::TagDecl>(decl))
    return GetTypeForDecl(tag_decl);
  if (clang::ValueDecl *value_decl = llvm::dyn_cast<clang::ValueDecl>(decl))
    return GetTypeForDecl(value_decl);
  return CompilerType();
}

CompilerType TypeSystemClang::GetTypeForDecl(TagDecl *decl) {
  return GetType(getASTContext().getTagDeclType(decl));
}

CompilerType TypeSystemClang::GetTypeForDecl(ObjCInterfaceDecl *decl) {
  return GetType(getASTContext().getObjCInterfaceType(decl));
}

CompilerType TypeSystemClang::GetTypeForDecl(clang::ValueDecl *value_decl) {
  return GetType(value_decl->getType());
}

#pragma mark Structure, Unions, Classes

void TypeSystemClang::SetOwningModule(clang::Decl *decl,
                                      OptionalClangModuleID owning_module) {
  if (!decl || !owning_module.HasValue())
    return;

  decl->setFromASTFile();
  decl->setOwningModuleID(owning_module.GetValue());
  decl->setModuleOwnershipKind(clang::Decl::ModuleOwnershipKind::Visible);
}

OptionalClangModuleID
TypeSystemClang::GetOrCreateClangModule(llvm::StringRef name,
                                        OptionalClangModuleID parent,
                                        bool is_framework, bool is_explicit) {
  // Get the external AST source which holds the modules.
  auto *ast_source = llvm::dyn_cast_or_null<ClangExternalASTSourceCallbacks>(
      getASTContext().getExternalSource());
  assert(ast_source && "external ast source was lost");
  if (!ast_source)
    return {};

  // Lazily initialize the module map.
  if (!m_header_search_up) {
    auto HSOpts = std::make_shared<clang::HeaderSearchOptions>();
    m_header_search_up = std::make_unique<clang::HeaderSearch>(
        HSOpts, *m_source_manager_up, *m_diagnostics_engine_up,
        *m_language_options_up, m_target_info_up.get());
    m_module_map_up = std::make_unique<clang::ModuleMap>(
        *m_source_manager_up, *m_diagnostics_engine_up, *m_language_options_up,
        m_target_info_up.get(), *m_header_search_up);
  }

  // Get or create the module context.
  bool created;
  clang::Module *module;
  auto parent_desc = ast_source->getSourceDescriptor(parent.GetValue());
  std::tie(module, created) = m_module_map_up->findOrCreateModule(
      name, parent_desc ? parent_desc->getModuleOrNull() : nullptr,
      is_framework, is_explicit);
  if (!created)
    return ast_source->GetIDForModule(module);

  return ast_source->RegisterModule(module);
}

CompilerType TypeSystemClang::CreateRecordType(
    clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
    AccessType access_type, llvm::StringRef name, int kind,
    LanguageType language, ClangASTMetadata *metadata, bool exports_symbols) {
  ASTContext &ast = getASTContext();

  if (decl_ctx == nullptr)
    decl_ctx = ast.getTranslationUnitDecl();

  if (language == eLanguageTypeObjC ||
      language == eLanguageTypeObjC_plus_plus) {
    bool isForwardDecl = true;
    bool isInternal = false;
    return CreateObjCClass(name, decl_ctx, owning_module, isForwardDecl,
                           isInternal, metadata);
  }

  // NOTE: Eventually CXXRecordDecl will be merged back into RecordDecl and
  // we will need to update this code. I was told to currently always use the
  // CXXRecordDecl class since we often don't know from debug information if
  // something is struct or a class, so we default to always use the more
  // complete definition just in case.

  bool has_name = !name.empty();
  CXXRecordDecl *decl = CXXRecordDecl::CreateDeserialized(ast, GlobalDeclID());
  decl->setTagKind(static_cast<TagDecl::TagKind>(kind));
  decl->setDeclContext(decl_ctx);
  if (has_name)
    decl->setDeclName(&ast.Idents.get(name));
  SetOwningModule(decl, owning_module);

  if (!has_name) {
    // In C++ a lambda is also represented as an unnamed class. This is
    // different from an *anonymous class* that the user wrote:
    //
    // struct A {
    //  // anonymous class (GNU/MSVC extension)
    //  struct {
    //    int x;
    //  };
    //  // unnamed class within a class
    //  struct {
    //    int y;
    //  } B;
    // };
    //
    // void f() {
    //    // unammed class outside of a class
    //    struct {
    //      int z;
    //    } C;
    // }
    //
    // Anonymous classes is a GNU/MSVC extension that clang supports. It
    // requires the anonymous class be embedded within a class. So the new
    // heuristic verifies this condition.
    if (isa<CXXRecordDecl>(decl_ctx) && exports_symbols)
      decl->setAnonymousStructOrUnion(true);
  }

  if (metadata)
    SetMetadata(decl, *metadata);

  if (access_type != eAccessNone)
    decl->setAccess(ConvertAccessTypeToAccessSpecifier(access_type));

  if (decl_ctx)
    decl_ctx->addDecl(decl);

  return GetType(ast.getTagDeclType(decl));
}

namespace {
/// Returns true iff the given TemplateArgument should be represented as an
/// NonTypeTemplateParmDecl in the AST.
bool IsValueParam(const clang::TemplateArgument &argument) {
  return argument.getKind() == TemplateArgument::Integral;
}

void AddAccessSpecifierDecl(clang::CXXRecordDecl *cxx_record_decl,
                            ASTContext &ct,
                            clang::AccessSpecifier previous_access,
                            clang::AccessSpecifier access_specifier) {
  if (!cxx_record_decl->isClass() && !cxx_record_decl->isStruct())
    return;
  if (previous_access != access_specifier) {
    // For struct, don't add AS_public if it's the first AccessSpecDecl.
    // For class, don't add AS_private if it's the first AccessSpecDecl.
    if ((cxx_record_decl->isStruct() &&
         previous_access == clang::AccessSpecifier::AS_none &&
         access_specifier == clang::AccessSpecifier::AS_public) ||
        (cxx_record_decl->isClass() &&
         previous_access == clang::AccessSpecifier::AS_none &&
         access_specifier == clang::AccessSpecifier::AS_private)) {
      return;
    }
    cxx_record_decl->addDecl(
        AccessSpecDecl::Create(ct, access_specifier, cxx_record_decl,
                               SourceLocation(), SourceLocation()));
  }
}
} // namespace

static TemplateParameterList *CreateTemplateParameterList(
    ASTContext &ast,
    const TypeSystemClang::TemplateParameterInfos &template_param_infos,
    llvm::SmallVector<NamedDecl *, 8> &template_param_decls) {
  const bool parameter_pack = false;
  const bool is_typename = false;
  const unsigned depth = 0;
  const size_t num_template_params = template_param_infos.Size();
  DeclContext *const decl_context =
      ast.getTranslationUnitDecl(); // Is this the right decl context?,

  auto const &args = template_param_infos.GetArgs();
  auto const &names = template_param_infos.GetNames();
  for (size_t i = 0; i < num_template_params; ++i) {
    const char *name = names[i];

    IdentifierInfo *identifier_info = nullptr;
    if (name && name[0])
      identifier_info = &ast.Idents.get(name);
    TemplateArgument const &targ = args[i];
    if (IsValueParam(targ)) {
      QualType template_param_type = targ.getIntegralType();
      template_param_decls.push_back(NonTypeTemplateParmDecl::Create(
          ast, decl_context, SourceLocation(), SourceLocation(), depth, i,
          identifier_info, template_param_type, parameter_pack,
          ast.getTrivialTypeSourceInfo(template_param_type)));
    } else {
      template_param_decls.push_back(TemplateTypeParmDecl::Create(
          ast, decl_context, SourceLocation(), SourceLocation(), depth, i,
          identifier_info, is_typename, parameter_pack));
    }
  }

  if (template_param_infos.hasParameterPack()) {
    IdentifierInfo *identifier_info = nullptr;
    if (template_param_infos.HasPackName())
      identifier_info = &ast.Idents.get(template_param_infos.GetPackName());
    const bool parameter_pack_true = true;

    if (!template_param_infos.GetParameterPack().IsEmpty() &&
        IsValueParam(template_param_infos.GetParameterPack().Front())) {
      QualType template_param_type =
          template_param_infos.GetParameterPack().Front().getIntegralType();
      template_param_decls.push_back(NonTypeTemplateParmDecl::Create(
          ast, decl_context, SourceLocation(), SourceLocation(), depth,
          num_template_params, identifier_info, template_param_type,
          parameter_pack_true,
          ast.getTrivialTypeSourceInfo(template_param_type)));
    } else {
      template_param_decls.push_back(TemplateTypeParmDecl::Create(
          ast, decl_context, SourceLocation(), SourceLocation(), depth,
          num_template_params, identifier_info, is_typename,
          parameter_pack_true));
    }
  }
  clang::Expr *const requires_clause = nullptr; // TODO: Concepts
  TemplateParameterList *template_param_list = TemplateParameterList::Create(
      ast, SourceLocation(), SourceLocation(), template_param_decls,
      SourceLocation(), requires_clause);
  return template_param_list;
}

std::string TypeSystemClang::PrintTemplateParams(
    const TemplateParameterInfos &template_param_infos) {
  llvm::SmallVector<NamedDecl *, 8> ignore;
  clang::TemplateParameterList *template_param_list =
      CreateTemplateParameterList(getASTContext(), template_param_infos,
                                  ignore);
  llvm::SmallVector<clang::TemplateArgument, 2> args(
      template_param_infos.GetArgs());
  if (template_param_infos.hasParameterPack()) {
    llvm::ArrayRef<TemplateArgument> pack_args =
        template_param_infos.GetParameterPackArgs();
    args.append(pack_args.begin(), pack_args.end());
  }
  std::string str;
  llvm::raw_string_ostream os(str);
  clang::printTemplateArgumentList(os, args, GetTypePrintingPolicy(),
                                   template_param_list);
  return str;
}

clang::FunctionTemplateDecl *TypeSystemClang::CreateFunctionTemplateDecl(
    clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
    clang::FunctionDecl *func_decl,
    const TemplateParameterInfos &template_param_infos) {
  //    /// Create a function template node.
  ASTContext &ast = getASTContext();

  llvm::SmallVector<NamedDecl *, 8> template_param_decls;
  TemplateParameterList *template_param_list = CreateTemplateParameterList(
      ast, template_param_infos, template_param_decls);
  FunctionTemplateDecl *func_tmpl_decl =
      FunctionTemplateDecl::CreateDeserialized(ast, GlobalDeclID());
  func_tmpl_decl->setDeclContext(decl_ctx);
  func_tmpl_decl->setLocation(func_decl->getLocation());
  func_tmpl_decl->setDeclName(func_decl->getDeclName());
  func_tmpl_decl->setTemplateParameters(template_param_list);
  func_tmpl_decl->init(func_decl);
  SetOwningModule(func_tmpl_decl, owning_module);

  for (size_t i = 0, template_param_decl_count = template_param_decls.size();
       i < template_param_decl_count; ++i) {
    // TODO: verify which decl context we should put template_param_decls into..
    template_param_decls[i]->setDeclContext(func_decl);
  }
  // Function templates inside a record need to have an access specifier.
  // It doesn't matter what access specifier we give the template as LLDB
  // anyway allows accessing everything inside a record.
  if (decl_ctx->isRecord())
    func_tmpl_decl->setAccess(clang::AccessSpecifier::AS_public);

  return func_tmpl_decl;
}

void TypeSystemClang::CreateFunctionTemplateSpecializationInfo(
    FunctionDecl *func_decl, clang::FunctionTemplateDecl *func_tmpl_decl,
    const TemplateParameterInfos &infos) {
  TemplateArgumentList *template_args_ptr = TemplateArgumentList::CreateCopy(
      func_decl->getASTContext(), infos.GetArgs());

  func_decl->setFunctionTemplateSpecialization(func_tmpl_decl,
                                               template_args_ptr, nullptr);
}

/// Returns true if the given template parameter can represent the given value.
/// For example, `typename T` can represent `int` but not integral values such
/// as `int I = 3`.
static bool TemplateParameterAllowsValue(NamedDecl *param,
                                         const TemplateArgument &value) {
  if (llvm::isa<TemplateTypeParmDecl>(param)) {
    // Compare the argument kind, i.e. ensure that <typename> != <int>.
    if (value.getKind() != TemplateArgument::Type)
      return false;
  } else if (auto *type_param =
                 llvm::dyn_cast<NonTypeTemplateParmDecl>(param)) {
    // Compare the argument kind, i.e. ensure that <typename> != <int>.
    if (!IsValueParam(value))
      return false;
    // Compare the integral type, i.e. ensure that <int> != <char>.
    if (type_param->getType() != value.getIntegralType())
      return false;
  } else {
    // There is no way to create other parameter decls at the moment, so we
    // can't reach this case during normal LLDB usage. Log that this happened
    // and assert.
    Log *log = GetLog(LLDBLog::Expressions);
    LLDB_LOG(log,
             "Don't know how to compare template parameter to passed"
             " value. Decl kind of parameter is: {0}",
             param->getDeclKindName());
    lldbassert(false && "Can't compare this TemplateParmDecl subclass");
    // In release builds just fall back to marking the parameter as not
    // accepting the value so that we don't try to fit an instantiation to a
    // template that doesn't fit. E.g., avoid that `S<1>` is being connected to
    // `template<typename T> struct S;`.
    return false;
  }
  return true;
}

/// Returns true if the given class template declaration could produce an
/// instantiation with the specified values.
/// For example, `<typename T>` allows the arguments `float`, but not for
/// example `bool, float` or `3` (as an integer parameter value).
static bool ClassTemplateAllowsToInstantiationArgs(
    ClassTemplateDecl *class_template_decl,
    const TypeSystemClang::TemplateParameterInfos &instantiation_values) {

  TemplateParameterList &params = *class_template_decl->getTemplateParameters();

  // Save some work by iterating only once over the found parameters and
  // calculate the information related to parameter packs.

  // Contains the first pack parameter (or non if there are none).
  std::optional<NamedDecl *> pack_parameter;
  // Contains the number of non-pack parameters.
  size_t non_pack_params = params.size();
  for (size_t i = 0; i < params.size(); ++i) {
    NamedDecl *param = params.getParam(i);
    if (param->isParameterPack()) {
      pack_parameter = param;
      non_pack_params = i;
      break;
    }
  }

  // The found template needs to have compatible non-pack template arguments.
  // E.g., ensure that <typename, typename> != <typename>.
  // The pack parameters are compared later.
  if (non_pack_params != instantiation_values.Size())
    return false;

  // Ensure that <typename...> != <typename>.
  if (pack_parameter.has_value() != instantiation_values.hasParameterPack())
    return false;

  // Compare the first pack parameter that was found with the first pack
  // parameter value. The special case of having an empty parameter pack value
  // always fits to a pack parameter.
  // E.g., ensure that <int...> != <typename...>.
  if (pack_parameter && !instantiation_values.GetParameterPack().IsEmpty() &&
      !TemplateParameterAllowsValue(
          *pack_parameter, instantiation_values.GetParameterPack().Front()))
    return false;

  // Compare all the non-pack parameters now.
  // E.g., ensure that <int> != <long>.
  for (const auto pair :
       llvm::zip_first(instantiation_values.GetArgs(), params)) {
    const TemplateArgument &passed_arg = std::get<0>(pair);
    NamedDecl *found_param = std::get<1>(pair);
    if (!TemplateParameterAllowsValue(found_param, passed_arg))
      return false;
  }

  return class_template_decl;
}

ClassTemplateDecl *TypeSystemClang::CreateClassTemplateDecl(
    DeclContext *decl_ctx, OptionalClangModuleID owning_module,
    lldb::AccessType access_type, llvm::StringRef class_name, int kind,
    const TemplateParameterInfos &template_param_infos) {
  ASTContext &ast = getASTContext();

  ClassTemplateDecl *class_template_decl = nullptr;
  if (decl_ctx == nullptr)
    decl_ctx = ast.getTranslationUnitDecl();

  IdentifierInfo &identifier_info = ast.Idents.get(class_name);
  DeclarationName decl_name(&identifier_info);

  // Search the AST for an existing ClassTemplateDecl that could be reused.
  clang::DeclContext::lookup_result result = decl_ctx->lookup(decl_name);
  for (NamedDecl *decl : result) {
    class_template_decl = dyn_cast<clang::ClassTemplateDecl>(decl);
    if (!class_template_decl)
      continue;
    // The class template has to be able to represents the instantiation
    // values we received. Without this we might end up putting an instantiation
    // with arguments such as <int, int> to a template such as:
    //     template<typename T> struct S;
    // Connecting the instantiation to an incompatible template could cause
    // problems later on.
    if (!ClassTemplateAllowsToInstantiationArgs(class_template_decl,
                                                template_param_infos))
      continue;
    return class_template_decl;
  }

  llvm::SmallVector<NamedDecl *, 8> template_param_decls;

  TemplateParameterList *template_param_list = CreateTemplateParameterList(
      ast, template_param_infos, template_param_decls);

  CXXRecordDecl *template_cxx_decl =
      CXXRecordDecl::CreateDeserialized(ast, GlobalDeclID());
  template_cxx_decl->setTagKind(static_cast<TagDecl::TagKind>(kind));
  // What decl context do we use here? TU? The actual decl context?
  template_cxx_decl->setDeclContext(decl_ctx);
  template_cxx_decl->setDeclName(decl_name);
  SetOwningModule(template_cxx_decl, owning_module);

  for (size_t i = 0, template_param_decl_count = template_param_decls.size();
       i < template_param_decl_count; ++i) {
    template_param_decls[i]->setDeclContext(template_cxx_decl);
  }

  // With templated classes, we say that a class is templated with
  // specializations, but that the bare class has no functions.
  // template_cxx_decl->startDefinition();
  // template_cxx_decl->completeDefinition();

  class_template_decl =
      ClassTemplateDecl::CreateDeserialized(ast, GlobalDeclID());
  // What decl context do we use here? TU? The actual decl context?
  class_template_decl->setDeclContext(decl_ctx);
  class_template_decl->setDeclName(decl_name);
  class_template_decl->setTemplateParameters(template_param_list);
  class_template_decl->init(template_cxx_decl);
  template_cxx_decl->setDescribedClassTemplate(class_template_decl);
  SetOwningModule(class_template_decl, owning_module);

  if (access_type != eAccessNone)
    class_template_decl->setAccess(
        ConvertAccessTypeToAccessSpecifier(access_type));

  decl_ctx->addDecl(class_template_decl);

  VerifyDecl(class_template_decl);

  return class_template_decl;
}

TemplateTemplateParmDecl *
TypeSystemClang::CreateTemplateTemplateParmDecl(const char *template_name) {
  ASTContext &ast = getASTContext();

  auto *decl_ctx = ast.getTranslationUnitDecl();

  IdentifierInfo &identifier_info = ast.Idents.get(template_name);
  llvm::SmallVector<NamedDecl *, 8> template_param_decls;

  TypeSystemClang::TemplateParameterInfos template_param_infos;
  TemplateParameterList *template_param_list = CreateTemplateParameterList(
      ast, template_param_infos, template_param_decls);

  // LLDB needs to create those decls only to be able to display a
  // type that includes a template template argument. Only the name matters for
  // this purpose, so we use dummy values for the other characteristics of the
  // type.
  return TemplateTemplateParmDecl::Create(ast, decl_ctx, SourceLocation(),
                                          /*Depth=*/0, /*Position=*/0,
                                          /*IsParameterPack=*/false,
                                          &identifier_info, /*Typename=*/false,
                                          template_param_list);
}

ClassTemplateSpecializationDecl *
TypeSystemClang::CreateClassTemplateSpecializationDecl(
    DeclContext *decl_ctx, OptionalClangModuleID owning_module,
    ClassTemplateDecl *class_template_decl, int kind,
    const TemplateParameterInfos &template_param_infos) {
  ASTContext &ast = getASTContext();
  llvm::SmallVector<clang::TemplateArgument, 2> args(
      template_param_infos.Size() +
      (template_param_infos.hasParameterPack() ? 1 : 0));

  auto const &orig_args = template_param_infos.GetArgs();
  std::copy(orig_args.begin(), orig_args.end(), args.begin());
  if (template_param_infos.hasParameterPack()) {
    args[args.size() - 1] = TemplateArgument::CreatePackCopy(
        ast, template_param_infos.GetParameterPackArgs());
  }
  ClassTemplateSpecializationDecl *class_template_specialization_decl =
      ClassTemplateSpecializationDecl::CreateDeserialized(ast, GlobalDeclID());
  class_template_specialization_decl->setTagKind(
      static_cast<TagDecl::TagKind>(kind));
  class_template_specialization_decl->setDeclContext(decl_ctx);
  class_template_specialization_decl->setInstantiationOf(class_template_decl);
  class_template_specialization_decl->setTemplateArgs(
      TemplateArgumentList::CreateCopy(ast, args));
  ast.getTypeDeclType(class_template_specialization_decl, nullptr);
  class_template_specialization_decl->setDeclName(
      class_template_decl->getDeclName());
  SetOwningModule(class_template_specialization_decl, owning_module);
  decl_ctx->addDecl(class_template_specialization_decl);

  class_template_specialization_decl->setSpecializationKind(
      TSK_ExplicitSpecialization);

  return class_template_specialization_decl;
}

CompilerType TypeSystemClang::CreateClassTemplateSpecializationType(
    ClassTemplateSpecializationDecl *class_template_specialization_decl) {
  if (class_template_specialization_decl) {
    ASTContext &ast = getASTContext();
    return GetType(ast.getTagDeclType(class_template_specialization_decl));
  }
  return CompilerType();
}

static inline bool check_op_param(bool is_method,
                                  clang::OverloadedOperatorKind op_kind,
                                  bool unary, bool binary,
                                  uint32_t num_params) {
  // Special-case call since it can take any number of operands
  if (op_kind == OO_Call)
    return true;

  // The parameter count doesn't include "this"
  if (is_method)
    ++num_params;
  if (num_params == 1)
    return unary;
  if (num_params == 2)
    return binary;
  else
    return false;
}

bool TypeSystemClang::CheckOverloadedOperatorKindParameterCount(
    bool is_method, clang::OverloadedOperatorKind op_kind,
    uint32_t num_params) {
  switch (op_kind) {
  default:
    break;
  // C++ standard allows any number of arguments to new/delete
  case OO_New:
  case OO_Array_New:
  case OO_Delete:
  case OO_Array_Delete:
    return true;
  }

#define OVERLOADED_OPERATOR(Name, Spelling, Token, Unary, Binary, MemberOnly)  \
  case OO_##Name:                                                              \
    return check_op_param(is_method, op_kind, Unary, Binary, num_params);
  switch (op_kind) {
#include "clang/Basic/OperatorKinds.def"
  default:
    break;
  }
  return false;
}

clang::AccessSpecifier
TypeSystemClang::UnifyAccessSpecifiers(clang::AccessSpecifier lhs,
                                       clang::AccessSpecifier rhs) {
  // Make the access equal to the stricter of the field and the nested field's
  // access
  if (lhs == AS_none || rhs == AS_none)
    return AS_none;
  if (lhs == AS_private || rhs == AS_private)
    return AS_private;
  if (lhs == AS_protected || rhs == AS_protected)
    return AS_protected;
  return AS_public;
}

bool TypeSystemClang::FieldIsBitfield(FieldDecl *field,
                                      uint32_t &bitfield_bit_size) {
  ASTContext &ast = getASTContext();
  if (field == nullptr)
    return false;

  if (field->isBitField()) {
    Expr *bit_width_expr = field->getBitWidth();
    if (bit_width_expr) {
      if (std::optional<llvm::APSInt> bit_width_apsint =
              bit_width_expr->getIntegerConstantExpr(ast)) {
        bitfield_bit_size = bit_width_apsint->getLimitedValue(UINT32_MAX);
        return true;
      }
    }
  }
  return false;
}

bool TypeSystemClang::RecordHasFields(const RecordDecl *record_decl) {
  if (record_decl == nullptr)
    return false;

  if (!record_decl->field_empty())
    return true;

  // No fields, lets check this is a CXX record and check the base classes
  const CXXRecordDecl *cxx_record_decl = dyn_cast<CXXRecordDecl>(record_decl);
  if (cxx_record_decl) {
    CXXRecordDecl::base_class_const_iterator base_class, base_class_end;
    for (base_class = cxx_record_decl->bases_begin(),
        base_class_end = cxx_record_decl->bases_end();
         base_class != base_class_end; ++base_class) {
      const CXXRecordDecl *base_class_decl = cast<CXXRecordDecl>(
          base_class->getType()->getAs<RecordType>()->getDecl());
      if (RecordHasFields(base_class_decl))
        return true;
    }
  }

  // We always want forcefully completed types to show up so we can print a
  // message in the summary that indicates that the type is incomplete.
  // This will help users know when they are running into issues with
  // -flimit-debug-info instead of just seeing nothing if this is a base class
  // (since we were hiding empty base classes), or nothing when you turn open
  // an valiable whose type was incomplete.
  ClangASTMetadata *meta_data = GetMetadata(record_decl);
  if (meta_data && meta_data->IsForcefullyCompleted())
    return true;

  return false;
}

#pragma mark Objective-C Classes

CompilerType TypeSystemClang::CreateObjCClass(
    llvm::StringRef name, clang::DeclContext *decl_ctx,
    OptionalClangModuleID owning_module, bool isForwardDecl, bool isInternal,
    ClangASTMetadata *metadata) {
  ASTContext &ast = getASTContext();
  assert(!name.empty());
  if (!decl_ctx)
    decl_ctx = ast.getTranslationUnitDecl();

  ObjCInterfaceDecl *decl =
      ObjCInterfaceDecl::CreateDeserialized(ast, GlobalDeclID());
  decl->setDeclContext(decl_ctx);
  decl->setDeclName(&ast.Idents.get(name));
  /*isForwardDecl,*/
  decl->setImplicit(isInternal);
  SetOwningModule(decl, owning_module);

  if (metadata)
    SetMetadata(decl, *metadata);

  return GetType(ast.getObjCInterfaceType(decl));
}

bool TypeSystemClang::BaseSpecifierIsEmpty(const CXXBaseSpecifier *b) {
  return !TypeSystemClang::RecordHasFields(b->getType()->getAsCXXRecordDecl());
}

uint32_t
TypeSystemClang::GetNumBaseClasses(const CXXRecordDecl *cxx_record_decl,
                                   bool omit_empty_base_classes) {
  uint32_t num_bases = 0;
  if (cxx_record_decl) {
    if (omit_empty_base_classes) {
      CXXRecordDecl::base_class_const_iterator base_class, base_class_end;
      for (base_class = cxx_record_decl->bases_begin(),
          base_class_end = cxx_record_decl->bases_end();
           base_class != base_class_end; ++base_class) {
        // Skip empty base classes
        if (BaseSpecifierIsEmpty(base_class))
          continue;
        ++num_bases;
      }
    } else
      num_bases = cxx_record_decl->getNumBases();
  }
  return num_bases;
}

#pragma mark Namespace Declarations

NamespaceDecl *TypeSystemClang::GetUniqueNamespaceDeclaration(
    const char *name, clang::DeclContext *decl_ctx,
    OptionalClangModuleID owning_module, bool is_inline) {
  NamespaceDecl *namespace_decl = nullptr;
  ASTContext &ast = getASTContext();
  TranslationUnitDecl *translation_unit_decl = ast.getTranslationUnitDecl();
  if (!decl_ctx)
    decl_ctx = translation_unit_decl;

  if (name) {
    IdentifierInfo &identifier_info = ast.Idents.get(name);
    DeclarationName decl_name(&identifier_info);
    clang::DeclContext::lookup_result result = decl_ctx->lookup(decl_name);
    for (NamedDecl *decl : result) {
      namespace_decl = dyn_cast<clang::NamespaceDecl>(decl);
      if (namespace_decl)
        return namespace_decl;
    }

    namespace_decl = NamespaceDecl::Create(ast, decl_ctx, is_inline,
                                           SourceLocation(), SourceLocation(),
                                           &identifier_info, nullptr, false);

    decl_ctx->addDecl(namespace_decl);
  } else {
    if (decl_ctx == translation_unit_decl) {
      namespace_decl = translation_unit_decl->getAnonymousNamespace();
      if (namespace_decl)
        return namespace_decl;

      namespace_decl =
          NamespaceDecl::Create(ast, decl_ctx, false, SourceLocation(),
                                SourceLocation(), nullptr, nullptr, false);
      translation_unit_decl->setAnonymousNamespace(namespace_decl);
      translation_unit_decl->addDecl(namespace_decl);
      assert(namespace_decl == translation_unit_decl->getAnonymousNamespace());
    } else {
      NamespaceDecl *parent_namespace_decl = cast<NamespaceDecl>(decl_ctx);
      if (parent_namespace_decl) {
        namespace_decl = parent_namespace_decl->getAnonymousNamespace();
        if (namespace_decl)
          return namespace_decl;
        namespace_decl =
            NamespaceDecl::Create(ast, decl_ctx, false, SourceLocation(),
                                  SourceLocation(), nullptr, nullptr, false);
        parent_namespace_decl->setAnonymousNamespace(namespace_decl);
        parent_namespace_decl->addDecl(namespace_decl);
        assert(namespace_decl ==
               parent_namespace_decl->getAnonymousNamespace());
      } else {
        assert(false && "GetUniqueNamespaceDeclaration called with no name and "
                        "no namespace as decl_ctx");
      }
    }
  }
  // Note: namespaces can span multiple modules, so perhaps this isn't a good
  // idea.
  SetOwningModule(namespace_decl, owning_module);

  VerifyDecl(namespace_decl);
  return namespace_decl;
}

clang::BlockDecl *
TypeSystemClang::CreateBlockDeclaration(clang::DeclContext *ctx,
                                        OptionalClangModuleID owning_module) {
  if (ctx) {
    clang::BlockDecl *decl =
        clang::BlockDecl::CreateDeserialized(getASTContext(), GlobalDeclID());
    decl->setDeclContext(ctx);
    ctx->addDecl(decl);
    SetOwningModule(decl, owning_module);
    return decl;
  }
  return nullptr;
}

clang::DeclContext *FindLCABetweenDecls(clang::DeclContext *left,
                                        clang::DeclContext *right,
                                        clang::DeclContext *root) {
  if (root == nullptr)
    return nullptr;

  std::set<clang::DeclContext *> path_left;
  for (clang::DeclContext *d = left; d != nullptr; d = d->getParent())
    path_left.insert(d);

  for (clang::DeclContext *d = right; d != nullptr; d = d->getParent())
    if (path_left.find(d) != path_left.end())
      return d;

  return nullptr;
}

clang::UsingDirectiveDecl *TypeSystemClang::CreateUsingDirectiveDeclaration(
    clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
    clang::NamespaceDecl *ns_decl) {
  if (decl_ctx && ns_decl) {
    auto *translation_unit = getASTContext().getTranslationUnitDecl();
    clang::UsingDirectiveDecl *using_decl = clang::UsingDirectiveDecl::Create(
          getASTContext(), decl_ctx, clang::SourceLocation(),
          clang::SourceLocation(), clang::NestedNameSpecifierLoc(),
          clang::SourceLocation(), ns_decl,
          FindLCABetweenDecls(decl_ctx, ns_decl,
                              translation_unit));
      decl_ctx->addDecl(using_decl);
      SetOwningModule(using_decl, owning_module);
      return using_decl;
  }
  return nullptr;
}

clang::UsingDecl *
TypeSystemClang::CreateUsingDeclaration(clang::DeclContext *current_decl_ctx,
                                        OptionalClangModuleID owning_module,
                                        clang::NamedDecl *target) {
  if (current_decl_ctx && target) {
    clang::UsingDecl *using_decl = clang::UsingDecl::Create(
        getASTContext(), current_decl_ctx, clang::SourceLocation(),
        clang::NestedNameSpecifierLoc(), clang::DeclarationNameInfo(), false);
    SetOwningModule(using_decl, owning_module);
    clang::UsingShadowDecl *shadow_decl = clang::UsingShadowDecl::Create(
        getASTContext(), current_decl_ctx, clang::SourceLocation(),
        target->getDeclName(), using_decl, target);
    SetOwningModule(shadow_decl, owning_module);
    using_decl->addShadowDecl(shadow_decl);
    current_decl_ctx->addDecl(using_decl);
    return using_decl;
  }
  return nullptr;
}

clang::VarDecl *TypeSystemClang::CreateVariableDeclaration(
    clang::DeclContext *decl_context, OptionalClangModuleID owning_module,
    const char *name, clang::QualType type) {
  if (decl_context) {
    clang::VarDecl *var_decl =
        clang::VarDecl::CreateDeserialized(getASTContext(), GlobalDeclID());
    var_decl->setDeclContext(decl_context);
    if (name && name[0])
      var_decl->setDeclName(&getASTContext().Idents.getOwn(name));
    var_decl->setType(type);
    SetOwningModule(var_decl, owning_module);
    var_decl->setAccess(clang::AS_public);
    decl_context->addDecl(var_decl);
    return var_decl;
  }
  return nullptr;
}

lldb::opaque_compiler_type_t
TypeSystemClang::GetOpaqueCompilerType(clang::ASTContext *ast,
                                       lldb::BasicType basic_type) {
  switch (basic_type) {
  case eBasicTypeVoid:
    return ast->VoidTy.getAsOpaquePtr();
  case eBasicTypeChar:
    return ast->CharTy.getAsOpaquePtr();
  case eBasicTypeSignedChar:
    return ast->SignedCharTy.getAsOpaquePtr();
  case eBasicTypeUnsignedChar:
    return ast->UnsignedCharTy.getAsOpaquePtr();
  case eBasicTypeWChar:
    return ast->getWCharType().getAsOpaquePtr();
  case eBasicTypeSignedWChar:
    return ast->getSignedWCharType().getAsOpaquePtr();
  case eBasicTypeUnsignedWChar:
    return ast->getUnsignedWCharType().getAsOpaquePtr();
  case eBasicTypeChar8:
    return ast->Char8Ty.getAsOpaquePtr();
  case eBasicTypeChar16:
    return ast->Char16Ty.getAsOpaquePtr();
  case eBasicTypeChar32:
    return ast->Char32Ty.getAsOpaquePtr();
  case eBasicTypeShort:
    return ast->ShortTy.getAsOpaquePtr();
  case eBasicTypeUnsignedShort:
    return ast->UnsignedShortTy.getAsOpaquePtr();
  case eBasicTypeInt:
    return ast->IntTy.getAsOpaquePtr();
  case eBasicTypeUnsignedInt:
    return ast->UnsignedIntTy.getAsOpaquePtr();
  case eBasicTypeLong:
    return ast->LongTy.getAsOpaquePtr();
  case eBasicTypeUnsignedLong:
    return ast->UnsignedLongTy.getAsOpaquePtr();
  case eBasicTypeLongLong:
    return ast->LongLongTy.getAsOpaquePtr();
  case eBasicTypeUnsignedLongLong:
    return ast->UnsignedLongLongTy.getAsOpaquePtr();
  case eBasicTypeInt128:
    return ast->Int128Ty.getAsOpaquePtr();
  case eBasicTypeUnsignedInt128:
    return ast->UnsignedInt128Ty.getAsOpaquePtr();
  case eBasicTypeBool:
    return ast->BoolTy.getAsOpaquePtr();
  case eBasicTypeHalf:
    return ast->HalfTy.getAsOpaquePtr();
  case eBasicTypeFloat:
    return ast->FloatTy.getAsOpaquePtr();
  case eBasicTypeDouble:
    return ast->DoubleTy.getAsOpaquePtr();
  case eBasicTypeLongDouble:
    return ast->LongDoubleTy.getAsOpaquePtr();
  case eBasicTypeFloatComplex:
    return ast->getComplexType(ast->FloatTy).getAsOpaquePtr();
  case eBasicTypeDoubleComplex:
    return ast->getComplexType(ast->DoubleTy).getAsOpaquePtr();
  case eBasicTypeLongDoubleComplex:
    return ast->getComplexType(ast->LongDoubleTy).getAsOpaquePtr();
  case eBasicTypeObjCID:
    return ast->getObjCIdType().getAsOpaquePtr();
  case eBasicTypeObjCClass:
    return ast->getObjCClassType().getAsOpaquePtr();
  case eBasicTypeObjCSel:
    return ast->getObjCSelType().getAsOpaquePtr();
  case eBasicTypeNullPtr:
    return ast->NullPtrTy.getAsOpaquePtr();
  default:
    return nullptr;
  }
}

#pragma mark Function Types

clang::DeclarationName
TypeSystemClang::GetDeclarationName(llvm::StringRef name,
                                    const CompilerType &function_clang_type) {
  clang::OverloadedOperatorKind op_kind = clang::NUM_OVERLOADED_OPERATORS;
  if (!IsOperator(name, op_kind) || op_kind == clang::NUM_OVERLOADED_OPERATORS)
    return DeclarationName(&getASTContext().Idents.get(
        name)); // Not operator, but a regular function.

  // Check the number of operator parameters. Sometimes we have seen bad DWARF
  // that doesn't correctly describe operators and if we try to create a method
  // and add it to the class, clang will assert and crash, so we need to make
  // sure things are acceptable.
  clang::QualType method_qual_type(ClangUtil::GetQualType(function_clang_type));
  const clang::FunctionProtoType *function_type =
      llvm::dyn_cast<clang::FunctionProtoType>(method_qual_type.getTypePtr());
  if (function_type == nullptr)
    return clang::DeclarationName();

  const bool is_method = false;
  const unsigned int num_params = function_type->getNumParams();
  if (!TypeSystemClang::CheckOverloadedOperatorKindParameterCount(
          is_method, op_kind, num_params))
    return clang::DeclarationName();

  return getASTContext().DeclarationNames.getCXXOperatorName(op_kind);
}

PrintingPolicy TypeSystemClang::GetTypePrintingPolicy() {
  clang::PrintingPolicy printing_policy(getASTContext().getPrintingPolicy());
  printing_policy.SuppressTagKeyword = true;
  // Inline namespaces are important for some type formatters (e.g., libc++
  // and libstdc++ are differentiated by their inline namespaces).
  printing_policy.SuppressInlineNamespace = false;
  printing_policy.SuppressUnwrittenScope = false;
  // Default arguments are also always important for type formatters. Otherwise
  // we would need to always specify two type names for the setups where we do
  // know the default arguments and where we don't know default arguments.
  //
  // For example, without this we would need to have formatters for both:
  //   std::basic_string<char>
  // and
  //   std::basic_string<char, std::char_traits<char>, std::allocator<char> >
  // to support setups where LLDB was able to reconstruct default arguments
  // (and we then would have suppressed them from the type name) and also setups
  // where LLDB wasn't able to reconstruct the default arguments.
  printing_policy.SuppressDefaultTemplateArgs = false;
  return printing_policy;
}

std::string TypeSystemClang::GetTypeNameForDecl(const NamedDecl *named_decl,
                                                bool qualified) {
  clang::PrintingPolicy printing_policy = GetTypePrintingPolicy();
  std::string result;
  llvm::raw_string_ostream os(result);
  named_decl->getNameForDiagnostic(os, printing_policy, qualified);
  return result;
}

FunctionDecl *TypeSystemClang::CreateFunctionDeclaration(
    clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
    llvm::StringRef name, const CompilerType &function_clang_type,
    clang::StorageClass storage, bool is_inline) {
  FunctionDecl *func_decl = nullptr;
  ASTContext &ast = getASTContext();
  if (!decl_ctx)
    decl_ctx = ast.getTranslationUnitDecl();

  const bool hasWrittenPrototype = true;
  const bool isConstexprSpecified = false;

  clang::DeclarationName declarationName =
      GetDeclarationName(name, function_clang_type);
  func_decl = FunctionDecl::CreateDeserialized(ast, GlobalDeclID());
  func_decl->setDeclContext(decl_ctx);
  func_decl->setDeclName(declarationName);
  func_decl->setType(ClangUtil::GetQualType(function_clang_type));
  func_decl->setStorageClass(storage);
  func_decl->setInlineSpecified(is_inline);
  func_decl->setHasWrittenPrototype(hasWrittenPrototype);
  func_decl->setConstexprKind(isConstexprSpecified
                                  ? ConstexprSpecKind::Constexpr
                                  : ConstexprSpecKind::Unspecified);
  SetOwningModule(func_decl, owning_module);
  decl_ctx->addDecl(func_decl);

  VerifyDecl(func_decl);

  return func_decl;
}

CompilerType TypeSystemClang::CreateFunctionType(
    const CompilerType &result_type, const CompilerType *args,
    unsigned num_args, bool is_variadic, unsigned type_quals,
    clang::CallingConv cc, clang::RefQualifierKind ref_qual) {
  if (!result_type || !ClangUtil::IsClangType(result_type))
    return CompilerType(); // invalid return type

  std::vector<QualType> qual_type_args;
  if (num_args > 0 && args == nullptr)
    return CompilerType(); // invalid argument array passed in

  // Verify that all arguments are valid and the right type
  for (unsigned i = 0; i < num_args; ++i) {
    if (args[i]) {
      // Make sure we have a clang type in args[i] and not a type from another
      // language whose name might match
      const bool is_clang_type = ClangUtil::IsClangType(args[i]);
      lldbassert(is_clang_type);
      if (is_clang_type)
        qual_type_args.push_back(ClangUtil::GetQualType(args[i]));
      else
        return CompilerType(); //  invalid argument type (must be a clang type)
    } else
      return CompilerType(); // invalid argument type (empty)
  }

  // TODO: Detect calling convention in DWARF?
  FunctionProtoType::ExtProtoInfo proto_info;
  proto_info.ExtInfo = cc;
  proto_info.Variadic = is_variadic;
  proto_info.ExceptionSpec = EST_None;
  proto_info.TypeQuals = clang::Qualifiers::fromFastMask(type_quals);
  proto_info.RefQualifier = ref_qual;

  return GetType(getASTContext().getFunctionType(
      ClangUtil::GetQualType(result_type), qual_type_args, proto_info));
}

ParmVarDecl *TypeSystemClang::CreateParameterDeclaration(
    clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
    const char *name, const CompilerType &param_type, int storage,
    bool add_decl) {
  ASTContext &ast = getASTContext();
  auto *decl = ParmVarDecl::CreateDeserialized(ast, GlobalDeclID());
  decl->setDeclContext(decl_ctx);
  if (name && name[0])
    decl->setDeclName(&ast.Idents.get(name));
  decl->setType(ClangUtil::GetQualType(param_type));
  decl->setStorageClass(static_cast<clang::StorageClass>(storage));
  SetOwningModule(decl, owning_module);
  if (add_decl)
    decl_ctx->addDecl(decl);

  return decl;
}

void TypeSystemClang::SetFunctionParameters(
    FunctionDecl *function_decl, llvm::ArrayRef<ParmVarDecl *> params) {
  if (function_decl)
    function_decl->setParams(params);
}

CompilerType
TypeSystemClang::CreateBlockPointerType(const CompilerType &function_type) {
  QualType block_type = m_ast_up->getBlockPointerType(
      clang::QualType::getFromOpaquePtr(function_type.GetOpaqueQualType()));

  return GetType(block_type);
}

#pragma mark Array Types

CompilerType TypeSystemClang::CreateArrayType(const CompilerType &element_type,
                                              size_t element_count,
                                              bool is_vector) {
  if (element_type.IsValid()) {
    ASTContext &ast = getASTContext();

    if (is_vector) {
      return GetType(ast.getExtVectorType(ClangUtil::GetQualType(element_type),
                                          element_count));
    } else {

      llvm::APInt ap_element_count(64, element_count);
      if (element_count == 0) {
        return GetType(
            ast.getIncompleteArrayType(ClangUtil::GetQualType(element_type),
                                       clang::ArraySizeModifier::Normal, 0));
      } else {
        return GetType(ast.getConstantArrayType(
            ClangUtil::GetQualType(element_type), ap_element_count, nullptr,
            clang::ArraySizeModifier::Normal, 0));
      }
    }
  }
  return CompilerType();
}

CompilerType TypeSystemClang::CreateStructForIdentifier(
    llvm::StringRef type_name,
    const std::initializer_list<std::pair<const char *, CompilerType>>
        &type_fields,
    bool packed) {
  CompilerType type;
  if (!type_name.empty() &&
      (type = GetTypeForIdentifier<clang::CXXRecordDecl>(type_name))
          .IsValid()) {
    lldbassert(0 && "Trying to create a type for an existing name");
    return type;
  }

  type = CreateRecordType(
      nullptr, OptionalClangModuleID(), lldb::eAccessPublic, type_name,
      llvm::to_underlying(clang::TagTypeKind::Struct), lldb::eLanguageTypeC);
  StartTagDeclarationDefinition(type);
  for (const auto &field : type_fields)
    AddFieldToRecordType(type, field.first, field.second, lldb::eAccessPublic,
                         0);
  if (packed)
    SetIsPacked(type);
  CompleteTagDeclarationDefinition(type);
  return type;
}

CompilerType TypeSystemClang::GetOrCreateStructForIdentifier(
    llvm::StringRef type_name,
    const std::initializer_list<std::pair<const char *, CompilerType>>
        &type_fields,
    bool packed) {
  CompilerType type;
  if ((type = GetTypeForIdentifier<clang::CXXRecordDecl>(type_name)).IsValid())
    return type;

  return CreateStructForIdentifier(type_name, type_fields, packed);
}

#pragma mark Enumeration Types

CompilerType TypeSystemClang::CreateEnumerationType(
    llvm::StringRef name, clang::DeclContext *decl_ctx,
    OptionalClangModuleID owning_module, const Declaration &decl,
    const CompilerType &integer_clang_type, bool is_scoped) {
  // TODO: Do something intelligent with the Declaration object passed in
  // like maybe filling in the SourceLocation with it...
  ASTContext &ast = getASTContext();

  // TODO: ask about these...
  //    const bool IsFixed = false;
  EnumDecl *enum_decl = EnumDecl::CreateDeserialized(ast, GlobalDeclID());
  enum_decl->setDeclContext(decl_ctx);
  if (!name.empty())
    enum_decl->setDeclName(&ast.Idents.get(name));
  enum_decl->setScoped(is_scoped);
  enum_decl->setScopedUsingClassTag(is_scoped);
  enum_decl->setFixed(false);
  SetOwningModule(enum_decl, owning_module);
  if (decl_ctx)
    decl_ctx->addDecl(enum_decl);

  // TODO: check if we should be setting the promotion type too?
  enum_decl->setIntegerType(ClangUtil::GetQualType(integer_clang_type));

  enum_decl->setAccess(AS_public); // TODO respect what's in the debug info

  return GetType(ast.getTagDeclType(enum_decl));
}

CompilerType TypeSystemClang::GetIntTypeFromBitSize(size_t bit_size,
                                                    bool is_signed) {
  clang::ASTContext &ast = getASTContext();

  if (is_signed) {
    if (bit_size == ast.getTypeSize(ast.SignedCharTy))
      return GetType(ast.SignedCharTy);

    if (bit_size == ast.getTypeSize(ast.ShortTy))
      return GetType(ast.ShortTy);

    if (bit_size == ast.getTypeSize(ast.IntTy))
      return GetType(ast.IntTy);

    if (bit_size == ast.getTypeSize(ast.LongTy))
      return GetType(ast.LongTy);

    if (bit_size == ast.getTypeSize(ast.LongLongTy))
      return GetType(ast.LongLongTy);

    if (bit_size == ast.getTypeSize(ast.Int128Ty))
      return GetType(ast.Int128Ty);
  } else {
    if (bit_size == ast.getTypeSize(ast.UnsignedCharTy))
      return GetType(ast.UnsignedCharTy);

    if (bit_size == ast.getTypeSize(ast.UnsignedShortTy))
      return GetType(ast.UnsignedShortTy);

    if (bit_size == ast.getTypeSize(ast.UnsignedIntTy))
      return GetType(ast.UnsignedIntTy);

    if (bit_size == ast.getTypeSize(ast.UnsignedLongTy))
      return GetType(ast.UnsignedLongTy);

    if (bit_size == ast.getTypeSize(ast.UnsignedLongLongTy))
      return GetType(ast.UnsignedLongLongTy);

    if (bit_size == ast.getTypeSize(ast.UnsignedInt128Ty))
      return GetType(ast.UnsignedInt128Ty);
  }
  return CompilerType();
}

CompilerType TypeSystemClang::GetPointerSizedIntType(bool is_signed) {
  return GetIntTypeFromBitSize(
      getASTContext().getTypeSize(getASTContext().VoidPtrTy), is_signed);
}

void TypeSystemClang::DumpDeclContextHiearchy(clang::DeclContext *decl_ctx) {
  if (decl_ctx) {
    DumpDeclContextHiearchy(decl_ctx->getParent());

    clang::NamedDecl *named_decl = llvm::dyn_cast<clang::NamedDecl>(decl_ctx);
    if (named_decl) {
      printf("%20s: %s\n", decl_ctx->getDeclKindName(),
             named_decl->getDeclName().getAsString().c_str());
    } else {
      printf("%20s\n", decl_ctx->getDeclKindName());
    }
  }
}

void TypeSystemClang::DumpDeclHiearchy(clang::Decl *decl) {
  if (decl == nullptr)
    return;
  DumpDeclContextHiearchy(decl->getDeclContext());

  clang::RecordDecl *record_decl = llvm::dyn_cast<clang::RecordDecl>(decl);
  if (record_decl) {
    printf("%20s: %s%s\n", decl->getDeclKindName(),
           record_decl->getDeclName().getAsString().c_str(),
           record_decl->isInjectedClassName() ? " (injected class name)" : "");

  } else {
    clang::NamedDecl *named_decl = llvm::dyn_cast<clang::NamedDecl>(decl);
    if (named_decl) {
      printf("%20s: %s\n", decl->getDeclKindName(),
             named_decl->getDeclName().getAsString().c_str());
    } else {
      printf("%20s\n", decl->getDeclKindName());
    }
  }
}

bool TypeSystemClang::GetCompleteDecl(clang::ASTContext *ast,
                                      clang::Decl *decl) {
  if (!decl)
    return false;

  ExternalASTSource *ast_source = ast->getExternalSource();

  if (!ast_source)
    return false;

  if (clang::TagDecl *tag_decl = llvm::dyn_cast<clang::TagDecl>(decl)) {
    if (tag_decl->isCompleteDefinition())
      return true;

    if (!tag_decl->hasExternalLexicalStorage())
      return false;

    ast_source->CompleteType(tag_decl);

    return !tag_decl->getTypeForDecl()->isIncompleteType();
  } else if (clang::ObjCInterfaceDecl *objc_interface_decl =
                 llvm::dyn_cast<clang::ObjCInterfaceDecl>(decl)) {
    if (objc_interface_decl->getDefinition())
      return true;

    if (!objc_interface_decl->hasExternalLexicalStorage())
      return false;

    ast_source->CompleteType(objc_interface_decl);

    return !objc_interface_decl->getTypeForDecl()->isIncompleteType();
  } else {
    return false;
  }
}

void TypeSystemClang::SetMetadataAsUserID(const clang::Decl *decl,
                                          user_id_t user_id) {
  ClangASTMetadata meta_data;
  meta_data.SetUserID(user_id);
  SetMetadata(decl, meta_data);
}

void TypeSystemClang::SetMetadataAsUserID(const clang::Type *type,
                                          user_id_t user_id) {
  ClangASTMetadata meta_data;
  meta_data.SetUserID(user_id);
  SetMetadata(type, meta_data);
}

void TypeSystemClang::SetMetadata(const clang::Decl *object,
                                  ClangASTMetadata &metadata) {
  m_decl_metadata[object] = metadata;
}

void TypeSystemClang::SetMetadata(const clang::Type *object,
                                  ClangASTMetadata &metadata) {
  m_type_metadata[object] = metadata;
}

ClangASTMetadata *TypeSystemClang::GetMetadata(const clang::Decl *object) {
  auto It = m_decl_metadata.find(object);
  if (It != m_decl_metadata.end())
    return &It->second;
  return nullptr;
}

ClangASTMetadata *TypeSystemClang::GetMetadata(const clang::Type *object) {
  auto It = m_type_metadata.find(object);
  if (It != m_type_metadata.end())
    return &It->second;
  return nullptr;
}

void TypeSystemClang::SetCXXRecordDeclAccess(const clang::CXXRecordDecl *object,
                                             clang::AccessSpecifier access) {
  if (access == clang::AccessSpecifier::AS_none)
    m_cxx_record_decl_access.erase(object);
  else
    m_cxx_record_decl_access[object] = access;
}

clang::AccessSpecifier
TypeSystemClang::GetCXXRecordDeclAccess(const clang::CXXRecordDecl *object) {
  auto It = m_cxx_record_decl_access.find(object);
  if (It != m_cxx_record_decl_access.end())
    return It->second;
  return clang::AccessSpecifier::AS_none;
}

clang::DeclContext *
TypeSystemClang::GetDeclContextForType(const CompilerType &type) {
  return GetDeclContextForType(ClangUtil::GetQualType(type));
}

CompilerDeclContext
TypeSystemClang::GetCompilerDeclContextForType(const CompilerType &type) {
  if (auto *decl_context = GetDeclContextForType(type))
    return CreateDeclContext(decl_context);
  return CompilerDeclContext();
}

/// Aggressively desugar the provided type, skipping past various kinds of
/// syntactic sugar and other constructs one typically wants to ignore.
/// The \p mask argument allows one to skip certain kinds of simplifications,
/// when one wishes to handle a certain kind of type directly.
static QualType
RemoveWrappingTypes(QualType type, ArrayRef<clang::Type::TypeClass> mask = {}) {
  while (true) {
    if (find(mask, type->getTypeClass()) != mask.end())
      return type;
    switch (type->getTypeClass()) {
    // This is not fully correct as _Atomic is more than sugar, but it is
    // sufficient for the purposes we care about.
    case clang::Type::Atomic:
      type = cast<clang::AtomicType>(type)->getValueType();
      break;
    case clang::Type::Auto:
    case clang::Type::Decltype:
    case clang::Type::Elaborated:
    case clang::Type::Paren:
    case clang::Type::SubstTemplateTypeParm:
    case clang::Type::TemplateSpecialization:
    case clang::Type::Typedef:
    case clang::Type::TypeOf:
    case clang::Type::TypeOfExpr:
    case clang::Type::Using:
      type = type->getLocallyUnqualifiedSingleStepDesugaredType();
      break;
    default:
      return type;
    }
  }
}

clang::DeclContext *
TypeSystemClang::GetDeclContextForType(clang::QualType type) {
  if (type.isNull())
    return nullptr;

  clang::QualType qual_type = RemoveWrappingTypes(type.getCanonicalType());
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::ObjCInterface:
    return llvm::cast<clang::ObjCObjectType>(qual_type.getTypePtr())
        ->getInterface();
  case clang::Type::ObjCObjectPointer:
    return GetDeclContextForType(
        llvm::cast<clang::ObjCObjectPointerType>(qual_type.getTypePtr())
            ->getPointeeType());
  case clang::Type::Record:
    return llvm::cast<clang::RecordType>(qual_type)->getDecl();
  case clang::Type::Enum:
    return llvm::cast<clang::EnumType>(qual_type)->getDecl();
  default:
    break;
  }
  // No DeclContext in this type...
  return nullptr;
}

/// Returns the clang::RecordType of the specified \ref qual_type. This
/// function will try to complete the type if necessary (and allowed
/// by the specified \ref allow_completion). If we fail to return a *complete*
/// type, returns nullptr.
static const clang::RecordType *GetCompleteRecordType(clang::ASTContext *ast,
                                                      clang::QualType qual_type,
                                                      bool allow_completion) {
  assert(qual_type->isRecordType());

  const auto *tag_type = llvm::cast<clang::RecordType>(qual_type.getTypePtr());

  clang::CXXRecordDecl *cxx_record_decl = qual_type->getAsCXXRecordDecl();

  // RecordType with no way of completing it, return the plain
  // TagType.
  if (!cxx_record_decl || !cxx_record_decl->hasExternalLexicalStorage())
    return tag_type;

  const bool is_complete = cxx_record_decl->isCompleteDefinition();
  const bool fields_loaded =
      cxx_record_decl->hasLoadedFieldsFromExternalStorage();

  // Already completed this type, nothing to be done.
  if (is_complete && fields_loaded)
    return tag_type;

  if (!allow_completion)
    return nullptr;

  // Call the field_begin() accessor to for it to use the external source
  // to load the fields...
  //
  // TODO: if we need to complete the type but have no external source,
  // shouldn't we error out instead?
  clang::ExternalASTSource *external_ast_source = ast->getExternalSource();
  if (external_ast_source) {
    external_ast_source->CompleteType(cxx_record_decl);
    if (cxx_record_decl->isCompleteDefinition()) {
      cxx_record_decl->field_begin();
      cxx_record_decl->setHasLoadedFieldsFromExternalStorage(true);
    }
  }

  return tag_type;
}

/// Returns the clang::EnumType of the specified \ref qual_type. This
/// function will try to complete the type if necessary (and allowed
/// by the specified \ref allow_completion). If we fail to return a *complete*
/// type, returns nullptr.
static const clang::EnumType *GetCompleteEnumType(clang::ASTContext *ast,
                                                  clang::QualType qual_type,
                                                  bool allow_completion) {
  assert(qual_type->isEnumeralType());
  assert(ast);

  const clang::EnumType *enum_type =
      llvm::cast<clang::EnumType>(qual_type.getTypePtr());

  auto *tag_decl = enum_type->getAsTagDecl();
  assert(tag_decl);

  // Already completed, nothing to be done.
  if (tag_decl->getDefinition())
    return enum_type;

  if (!allow_completion)
    return nullptr;

  // No definition but can't complete it, error out.
  if (!tag_decl->hasExternalLexicalStorage())
    return nullptr;

  // We can't complete the type without an external source.
  clang::ExternalASTSource *external_ast_source = ast->getExternalSource();
  if (!external_ast_source)
    return nullptr;

  external_ast_source->CompleteType(tag_decl);
  return enum_type;
}

/// Returns the clang::ObjCObjectType of the specified \ref qual_type. This
/// function will try to complete the type if necessary (and allowed
/// by the specified \ref allow_completion). If we fail to return a *complete*
/// type, returns nullptr.
static const clang::ObjCObjectType *
GetCompleteObjCObjectType(clang::ASTContext *ast, QualType qual_type,
                          bool allow_completion) {
  assert(qual_type->isObjCObjectType());
  assert(ast);

  const clang::ObjCObjectType *objc_class_type =
      llvm::cast<clang::ObjCObjectType>(qual_type);

  clang::ObjCInterfaceDecl *class_interface_decl =
      objc_class_type->getInterface();
  // We currently can't complete objective C types through the newly added
  // ASTContext because it only supports TagDecl objects right now...
  if (!class_interface_decl)
    return objc_class_type;

  // Already complete, nothing to be done.
  if (class_interface_decl->getDefinition())
    return objc_class_type;

  if (!allow_completion)
    return nullptr;

  // No definition but can't complete it, error out.
  if (!class_interface_decl->hasExternalLexicalStorage())
    return nullptr;

  // We can't complete the type without an external source.
  clang::ExternalASTSource *external_ast_source = ast->getExternalSource();
  if (!external_ast_source)
    return nullptr;

  external_ast_source->CompleteType(class_interface_decl);
  return objc_class_type;
}

static bool GetCompleteQualType(clang::ASTContext *ast,
                                clang::QualType qual_type,
                                bool allow_completion = true) {
  qual_type = RemoveWrappingTypes(qual_type);
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::ConstantArray:
  case clang::Type::IncompleteArray:
  case clang::Type::VariableArray: {
    const clang::ArrayType *array_type =
        llvm::dyn_cast<clang::ArrayType>(qual_type.getTypePtr());

    if (array_type)
      return GetCompleteQualType(ast, array_type->getElementType(),
                                 allow_completion);
  } break;
  case clang::Type::Record: {
    if (const auto *RT =
            GetCompleteRecordType(ast, qual_type, allow_completion))
      return !RT->isIncompleteType();

    return false;
  } break;

  case clang::Type::Enum: {
    if (const auto *ET = GetCompleteEnumType(ast, qual_type, allow_completion))
      return !ET->isIncompleteType();

    return false;
  } break;
  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface: {
    if (const auto *OT =
            GetCompleteObjCObjectType(ast, qual_type, allow_completion))
      return !OT->isIncompleteType();

    return false;
  } break;

  case clang::Type::Attributed:
    return GetCompleteQualType(
        ast, llvm::cast<clang::AttributedType>(qual_type)->getModifiedType(),
        allow_completion);

  default:
    break;
  }

  return true;
}

static clang::ObjCIvarDecl::AccessControl
ConvertAccessTypeToObjCIvarAccessControl(AccessType access) {
  switch (access) {
  case eAccessNone:
    return clang::ObjCIvarDecl::None;
  case eAccessPublic:
    return clang::ObjCIvarDecl::Public;
  case eAccessPrivate:
    return clang::ObjCIvarDecl::Private;
  case eAccessProtected:
    return clang::ObjCIvarDecl::Protected;
  case eAccessPackage:
    return clang::ObjCIvarDecl::Package;
  }
  return clang::ObjCIvarDecl::None;
}

// Tests

#ifndef NDEBUG
bool TypeSystemClang::Verify(lldb::opaque_compiler_type_t type) {
  return !type || llvm::isa<clang::Type>(GetQualType(type).getTypePtr());
}
#endif

bool TypeSystemClang::IsAggregateType(lldb::opaque_compiler_type_t type) {
  clang::QualType qual_type(RemoveWrappingTypes(GetCanonicalQualType(type)));

  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::IncompleteArray:
  case clang::Type::VariableArray:
  case clang::Type::ConstantArray:
  case clang::Type::ExtVector:
  case clang::Type::Vector:
  case clang::Type::Record:
  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface:
    return true;
  default:
    break;
  }
  // The clang type does have a value
  return false;
}

bool TypeSystemClang::IsAnonymousType(lldb::opaque_compiler_type_t type) {
  clang::QualType qual_type(RemoveWrappingTypes(GetCanonicalQualType(type)));

  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record: {
    if (const clang::RecordType *record_type =
            llvm::dyn_cast_or_null<clang::RecordType>(
                qual_type.getTypePtrOrNull())) {
      if (const clang::RecordDecl *record_decl = record_type->getDecl()) {
        return record_decl->isAnonymousStructOrUnion();
      }
    }
    break;
  }
  default:
    break;
  }
  // The clang type does have a value
  return false;
}

bool TypeSystemClang::IsArrayType(lldb::opaque_compiler_type_t type,
                                  CompilerType *element_type_ptr,
                                  uint64_t *size, bool *is_incomplete) {
  clang::QualType qual_type(RemoveWrappingTypes(GetCanonicalQualType(type)));

  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  default:
    break;

  case clang::Type::ConstantArray:
    if (element_type_ptr)
      element_type_ptr->SetCompilerType(
          weak_from_this(), llvm::cast<clang::ConstantArrayType>(qual_type)
                                ->getElementType()
                                .getAsOpaquePtr());
    if (size)
      *size = llvm::cast<clang::ConstantArrayType>(qual_type)
                  ->getSize()
                  .getLimitedValue(ULLONG_MAX);
    if (is_incomplete)
      *is_incomplete = false;
    return true;

  case clang::Type::IncompleteArray:
    if (element_type_ptr)
      element_type_ptr->SetCompilerType(
          weak_from_this(), llvm::cast<clang::IncompleteArrayType>(qual_type)
                                ->getElementType()
                                .getAsOpaquePtr());
    if (size)
      *size = 0;
    if (is_incomplete)
      *is_incomplete = true;
    return true;

  case clang::Type::VariableArray:
    if (element_type_ptr)
      element_type_ptr->SetCompilerType(
          weak_from_this(), llvm::cast<clang::VariableArrayType>(qual_type)
                                ->getElementType()
                                .getAsOpaquePtr());
    if (size)
      *size = 0;
    if (is_incomplete)
      *is_incomplete = false;
    return true;

  case clang::Type::DependentSizedArray:
    if (element_type_ptr)
      element_type_ptr->SetCompilerType(
          weak_from_this(),
          llvm::cast<clang::DependentSizedArrayType>(qual_type)
              ->getElementType()
              .getAsOpaquePtr());
    if (size)
      *size = 0;
    if (is_incomplete)
      *is_incomplete = false;
    return true;
  }
  if (element_type_ptr)
    element_type_ptr->Clear();
  if (size)
    *size = 0;
  if (is_incomplete)
    *is_incomplete = false;
  return false;
}

bool TypeSystemClang::IsVectorType(lldb::opaque_compiler_type_t type,
                                   CompilerType *element_type, uint64_t *size) {
  clang::QualType qual_type(GetCanonicalQualType(type));

  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Vector: {
    const clang::VectorType *vector_type =
        qual_type->getAs<clang::VectorType>();
    if (vector_type) {
      if (size)
        *size = vector_type->getNumElements();
      if (element_type)
        *element_type = GetType(vector_type->getElementType());
    }
    return true;
  } break;
  case clang::Type::ExtVector: {
    const clang::ExtVectorType *ext_vector_type =
        qual_type->getAs<clang::ExtVectorType>();
    if (ext_vector_type) {
      if (size)
        *size = ext_vector_type->getNumElements();
      if (element_type)
        *element_type =
            CompilerType(weak_from_this(),
                         ext_vector_type->getElementType().getAsOpaquePtr());
    }
    return true;
  }
  default:
    break;
  }
  return false;
}

bool TypeSystemClang::IsRuntimeGeneratedType(
    lldb::opaque_compiler_type_t type) {
  clang::DeclContext *decl_ctx = GetDeclContextForType(GetQualType(type));
  if (!decl_ctx)
    return false;

  if (!llvm::isa<clang::ObjCInterfaceDecl>(decl_ctx))
    return false;

  clang::ObjCInterfaceDecl *result_iface_decl =
      llvm::dyn_cast<clang::ObjCInterfaceDecl>(decl_ctx);

  ClangASTMetadata *ast_metadata = GetMetadata(result_iface_decl);
  if (!ast_metadata)
    return false;
  return (ast_metadata->GetISAPtr() != 0);
}

bool TypeSystemClang::IsCharType(lldb::opaque_compiler_type_t type) {
  return GetQualType(type).getUnqualifiedType()->isCharType();
}

bool TypeSystemClang::IsCompleteType(lldb::opaque_compiler_type_t type) {
  // If the type hasn't been lazily completed yet, complete it now so that we
  // can give the caller an accurate answer whether the type actually has a
  // definition. Without completing the type now we would just tell the user
  // the current (internal) completeness state of the type and most users don't
  // care (or even know) about this behavior.
  const bool allow_completion = true;
  return GetCompleteQualType(&getASTContext(), GetQualType(type),
                             allow_completion);
}

bool TypeSystemClang::IsConst(lldb::opaque_compiler_type_t type) {
  return GetQualType(type).isConstQualified();
}

bool TypeSystemClang::IsCStringType(lldb::opaque_compiler_type_t type,
                                    uint32_t &length) {
  CompilerType pointee_or_element_clang_type;
  length = 0;
  Flags type_flags(GetTypeInfo(type, &pointee_or_element_clang_type));

  if (!pointee_or_element_clang_type.IsValid())
    return false;

  if (type_flags.AnySet(eTypeIsArray | eTypeIsPointer)) {
    if (pointee_or_element_clang_type.IsCharType()) {
      if (type_flags.Test(eTypeIsArray)) {
        // We know the size of the array and it could be a C string since it is
        // an array of characters
        length = llvm::cast<clang::ConstantArrayType>(
                     GetCanonicalQualType(type).getTypePtr())
                     ->getSize()
                     .getLimitedValue();
      }
      return true;
    }
  }
  return false;
}

unsigned TypeSystemClang::GetPtrAuthKey(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType qual_type(GetCanonicalQualType(type));
    if (auto pointer_auth = qual_type.getPointerAuth())
      return pointer_auth.getKey();
  }
  return 0;
}

unsigned
TypeSystemClang::GetPtrAuthDiscriminator(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType qual_type(GetCanonicalQualType(type));
    if (auto pointer_auth = qual_type.getPointerAuth())
      return pointer_auth.getExtraDiscriminator();
  }
  return 0;
}

bool TypeSystemClang::GetPtrAuthAddressDiversity(
    lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType qual_type(GetCanonicalQualType(type));
    if (auto pointer_auth = qual_type.getPointerAuth())
      return pointer_auth.isAddressDiscriminated();
  }
  return false;
}

bool TypeSystemClang::IsFunctionType(lldb::opaque_compiler_type_t type) {
  auto isFunctionType = [&](clang::QualType qual_type) {
    return qual_type->isFunctionType();
  };

  return IsTypeImpl(type, isFunctionType);
}

// Used to detect "Homogeneous Floating-point Aggregates"
uint32_t
TypeSystemClang::IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                        CompilerType *base_type_ptr) {
  if (!type)
    return 0;

  clang::QualType qual_type(RemoveWrappingTypes(GetCanonicalQualType(type)));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record:
    if (GetCompleteType(type)) {
      const clang::CXXRecordDecl *cxx_record_decl =
          qual_type->getAsCXXRecordDecl();
      if (cxx_record_decl) {
        if (cxx_record_decl->getNumBases() || cxx_record_decl->isDynamicClass())
          return 0;
      }
      const clang::RecordType *record_type =
          llvm::cast<clang::RecordType>(qual_type.getTypePtr());
      if (record_type) {
        const clang::RecordDecl *record_decl = record_type->getDecl();
        if (record_decl) {
          // We are looking for a structure that contains only floating point
          // types
          clang::RecordDecl::field_iterator field_pos,
              field_end = record_decl->field_end();
          uint32_t num_fields = 0;
          bool is_hva = false;
          bool is_hfa = false;
          clang::QualType base_qual_type;
          uint64_t base_bitwidth = 0;
          for (field_pos = record_decl->field_begin(); field_pos != field_end;
               ++field_pos) {
            clang::QualType field_qual_type = field_pos->getType();
            uint64_t field_bitwidth = getASTContext().getTypeSize(qual_type);
            if (field_qual_type->isFloatingType()) {
              if (field_qual_type->isComplexType())
                return 0;
              else {
                if (num_fields == 0)
                  base_qual_type = field_qual_type;
                else {
                  if (is_hva)
                    return 0;
                  is_hfa = true;
                  if (field_qual_type.getTypePtr() !=
                      base_qual_type.getTypePtr())
                    return 0;
                }
              }
            } else if (field_qual_type->isVectorType() ||
                       field_qual_type->isExtVectorType()) {
              if (num_fields == 0) {
                base_qual_type = field_qual_type;
                base_bitwidth = field_bitwidth;
              } else {
                if (is_hfa)
                  return 0;
                is_hva = true;
                if (base_bitwidth != field_bitwidth)
                  return 0;
                if (field_qual_type.getTypePtr() != base_qual_type.getTypePtr())
                  return 0;
              }
            } else
              return 0;
            ++num_fields;
          }
          if (base_type_ptr)
            *base_type_ptr =
                CompilerType(weak_from_this(), base_qual_type.getAsOpaquePtr());
          return num_fields;
        }
      }
    }
    break;

  default:
    break;
  }
  return 0;
}

size_t TypeSystemClang::GetNumberOfFunctionArguments(
    lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType qual_type(GetCanonicalQualType(type));
    const clang::FunctionProtoType *func =
        llvm::dyn_cast<clang::FunctionProtoType>(qual_type.getTypePtr());
    if (func)
      return func->getNumParams();
  }
  return 0;
}

CompilerType
TypeSystemClang::GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                            const size_t index) {
  if (type) {
    clang::QualType qual_type(GetQualType(type));
    const clang::FunctionProtoType *func =
        llvm::dyn_cast<clang::FunctionProtoType>(qual_type.getTypePtr());
    if (func) {
      if (index < func->getNumParams())
        return CompilerType(weak_from_this(), func->getParamType(index).getAsOpaquePtr());
    }
  }
  return CompilerType();
}

bool TypeSystemClang::IsTypeImpl(
    lldb::opaque_compiler_type_t type,
    llvm::function_ref<bool(clang::QualType)> predicate) const {
  if (type) {
    clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));

    if (predicate(qual_type))
      return true;

    const clang::Type::TypeClass type_class = qual_type->getTypeClass();
    switch (type_class) {
    default:
      break;

    case clang::Type::LValueReference:
    case clang::Type::RValueReference: {
      const clang::ReferenceType *reference_type =
          llvm::cast<clang::ReferenceType>(qual_type.getTypePtr());
      if (reference_type)
        return IsTypeImpl(reference_type->getPointeeType().getAsOpaquePtr(), predicate);
    } break;
    }
  }
  return false;
}

bool TypeSystemClang::IsMemberFunctionPointerType(
    lldb::opaque_compiler_type_t type) {
  auto isMemberFunctionPointerType = [](clang::QualType qual_type) {
    return qual_type->isMemberFunctionPointerType();
  };

  return IsTypeImpl(type, isMemberFunctionPointerType);
}

bool TypeSystemClang::IsFunctionPointerType(lldb::opaque_compiler_type_t type) {
  auto isFunctionPointerType = [](clang::QualType qual_type) {
    return qual_type->isFunctionPointerType();
  };

  return IsTypeImpl(type, isFunctionPointerType);
}

bool TypeSystemClang::IsBlockPointerType(
    lldb::opaque_compiler_type_t type,
    CompilerType *function_pointer_type_ptr) {
  auto isBlockPointerType = [&](clang::QualType qual_type) {
    if (qual_type->isBlockPointerType()) {
      if (function_pointer_type_ptr) {
        const clang::BlockPointerType *block_pointer_type =
            qual_type->castAs<clang::BlockPointerType>();
        QualType pointee_type = block_pointer_type->getPointeeType();
        QualType function_pointer_type = m_ast_up->getPointerType(pointee_type);
        *function_pointer_type_ptr = CompilerType(
            weak_from_this(), function_pointer_type.getAsOpaquePtr());
      }
      return true;
    }

    return false;
  };

  return IsTypeImpl(type, isBlockPointerType);
}

bool TypeSystemClang::IsIntegerType(lldb::opaque_compiler_type_t type,
                                    bool &is_signed) {
  if (!type)
    return false;

  clang::QualType qual_type(GetCanonicalQualType(type));
  const clang::BuiltinType *builtin_type =
      llvm::dyn_cast<clang::BuiltinType>(qual_type->getCanonicalTypeInternal());

  if (builtin_type) {
    if (builtin_type->isInteger()) {
      is_signed = builtin_type->isSignedInteger();
      return true;
    }
  }

  return false;
}

bool TypeSystemClang::IsEnumerationType(lldb::opaque_compiler_type_t type,
                                        bool &is_signed) {
  if (type) {
    const clang::EnumType *enum_type = llvm::dyn_cast<clang::EnumType>(
        GetCanonicalQualType(type)->getCanonicalTypeInternal());

    if (enum_type) {
      IsIntegerType(enum_type->getDecl()->getIntegerType().getAsOpaquePtr(),
                    is_signed);
      return true;
    }
  }

  return false;
}

bool TypeSystemClang::IsScopedEnumerationType(
    lldb::opaque_compiler_type_t type) {
  if (type) {
    const clang::EnumType *enum_type = llvm::dyn_cast<clang::EnumType>(
        GetCanonicalQualType(type)->getCanonicalTypeInternal());

    if (enum_type) {
      return enum_type->isScopedEnumeralType();
    }
  }

  return false;
}

bool TypeSystemClang::IsPointerType(lldb::opaque_compiler_type_t type,
                                    CompilerType *pointee_type) {
  if (type) {
    clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
    const clang::Type::TypeClass type_class = qual_type->getTypeClass();
    switch (type_class) {
    case clang::Type::Builtin:
      switch (llvm::cast<clang::BuiltinType>(qual_type)->getKind()) {
      default:
        break;
      case clang::BuiltinType::ObjCId:
      case clang::BuiltinType::ObjCClass:
        return true;
      }
      return false;
    case clang::Type::ObjCObjectPointer:
      if (pointee_type)
        pointee_type->SetCompilerType(
            weak_from_this(),
            llvm::cast<clang::ObjCObjectPointerType>(qual_type)
                ->getPointeeType()
                .getAsOpaquePtr());
      return true;
    case clang::Type::BlockPointer:
      if (pointee_type)
        pointee_type->SetCompilerType(
            weak_from_this(), llvm::cast<clang::BlockPointerType>(qual_type)
                                  ->getPointeeType()
                                  .getAsOpaquePtr());
      return true;
    case clang::Type::Pointer:
      if (pointee_type)
        pointee_type->SetCompilerType(weak_from_this(),
                                      llvm::cast<clang::PointerType>(qual_type)
                                          ->getPointeeType()
                                          .getAsOpaquePtr());
      return true;
    case clang::Type::MemberPointer:
      if (pointee_type)
        pointee_type->SetCompilerType(
            weak_from_this(), llvm::cast<clang::MemberPointerType>(qual_type)
                                  ->getPointeeType()
                                  .getAsOpaquePtr());
      return true;
    default:
      break;
    }
  }
  if (pointee_type)
    pointee_type->Clear();
  return false;
}

bool TypeSystemClang::IsPointerOrReferenceType(
    lldb::opaque_compiler_type_t type, CompilerType *pointee_type) {
  if (type) {
    clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
    const clang::Type::TypeClass type_class = qual_type->getTypeClass();
    switch (type_class) {
    case clang::Type::Builtin:
      switch (llvm::cast<clang::BuiltinType>(qual_type)->getKind()) {
      default:
        break;
      case clang::BuiltinType::ObjCId:
      case clang::BuiltinType::ObjCClass:
        return true;
      }
      return false;
    case clang::Type::ObjCObjectPointer:
      if (pointee_type)
        pointee_type->SetCompilerType(
            weak_from_this(),
            llvm::cast<clang::ObjCObjectPointerType>(qual_type)
                ->getPointeeType()
                .getAsOpaquePtr());
      return true;
    case clang::Type::BlockPointer:
      if (pointee_type)
        pointee_type->SetCompilerType(
            weak_from_this(), llvm::cast<clang::BlockPointerType>(qual_type)
                                  ->getPointeeType()
                                  .getAsOpaquePtr());
      return true;
    case clang::Type::Pointer:
      if (pointee_type)
        pointee_type->SetCompilerType(weak_from_this(),
                                      llvm::cast<clang::PointerType>(qual_type)
                                          ->getPointeeType()
                                          .getAsOpaquePtr());
      return true;
    case clang::Type::MemberPointer:
      if (pointee_type)
        pointee_type->SetCompilerType(
            weak_from_this(), llvm::cast<clang::MemberPointerType>(qual_type)
                                  ->getPointeeType()
                                  .getAsOpaquePtr());
      return true;
    case clang::Type::LValueReference:
      if (pointee_type)
        pointee_type->SetCompilerType(
            weak_from_this(), llvm::cast<clang::LValueReferenceType>(qual_type)
                                  ->desugar()
                                  .getAsOpaquePtr());
      return true;
    case clang::Type::RValueReference:
      if (pointee_type)
        pointee_type->SetCompilerType(
            weak_from_this(), llvm::cast<clang::RValueReferenceType>(qual_type)
                                  ->desugar()
                                  .getAsOpaquePtr());
      return true;
    default:
      break;
    }
  }
  if (pointee_type)
    pointee_type->Clear();
  return false;
}

bool TypeSystemClang::IsReferenceType(lldb::opaque_compiler_type_t type,
                                      CompilerType *pointee_type,
                                      bool *is_rvalue) {
  if (type) {
    clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
    const clang::Type::TypeClass type_class = qual_type->getTypeClass();

    switch (type_class) {
    case clang::Type::LValueReference:
      if (pointee_type)
        pointee_type->SetCompilerType(
            weak_from_this(), llvm::cast<clang::LValueReferenceType>(qual_type)
                                  ->desugar()
                                  .getAsOpaquePtr());
      if (is_rvalue)
        *is_rvalue = false;
      return true;
    case clang::Type::RValueReference:
      if (pointee_type)
        pointee_type->SetCompilerType(
            weak_from_this(), llvm::cast<clang::RValueReferenceType>(qual_type)
                                  ->desugar()
                                  .getAsOpaquePtr());
      if (is_rvalue)
        *is_rvalue = true;
      return true;

    default:
      break;
    }
  }
  if (pointee_type)
    pointee_type->Clear();
  return false;
}

bool TypeSystemClang::IsFloatingPointType(lldb::opaque_compiler_type_t type,
                                          uint32_t &count, bool &is_complex) {
  if (type) {
    clang::QualType qual_type(GetCanonicalQualType(type));

    if (const clang::BuiltinType *BT = llvm::dyn_cast<clang::BuiltinType>(
            qual_type->getCanonicalTypeInternal())) {
      clang::BuiltinType::Kind kind = BT->getKind();
      if (kind >= clang::BuiltinType::Float &&
          kind <= clang::BuiltinType::LongDouble) {
        count = 1;
        is_complex = false;
        return true;
      }
    } else if (const clang::ComplexType *CT =
                   llvm::dyn_cast<clang::ComplexType>(
                       qual_type->getCanonicalTypeInternal())) {
      if (IsFloatingPointType(CT->getElementType().getAsOpaquePtr(), count,
                              is_complex)) {
        count = 2;
        is_complex = true;
        return true;
      }
    } else if (const clang::VectorType *VT = llvm::dyn_cast<clang::VectorType>(
                   qual_type->getCanonicalTypeInternal())) {
      if (IsFloatingPointType(VT->getElementType().getAsOpaquePtr(), count,
                              is_complex)) {
        count = VT->getNumElements();
        is_complex = false;
        return true;
      }
    }
  }
  count = 0;
  is_complex = false;
  return false;
}

bool TypeSystemClang::IsDefined(lldb::opaque_compiler_type_t type) {
  if (!type)
    return false;

  clang::QualType qual_type(GetQualType(type));
  const clang::TagType *tag_type =
      llvm::dyn_cast<clang::TagType>(qual_type.getTypePtr());
  if (tag_type) {
    clang::TagDecl *tag_decl = tag_type->getDecl();
    if (tag_decl)
      return tag_decl->isCompleteDefinition();
    return false;
  } else {
    const clang::ObjCObjectType *objc_class_type =
        llvm::dyn_cast<clang::ObjCObjectType>(qual_type);
    if (objc_class_type) {
      clang::ObjCInterfaceDecl *class_interface_decl =
          objc_class_type->getInterface();
      if (class_interface_decl)
        return class_interface_decl->getDefinition() != nullptr;
      return false;
    }
  }
  return true;
}

bool TypeSystemClang::IsObjCClassType(const CompilerType &type) {
  if (ClangUtil::IsClangType(type)) {
    clang::QualType qual_type(ClangUtil::GetCanonicalQualType(type));

    const clang::ObjCObjectPointerType *obj_pointer_type =
        llvm::dyn_cast<clang::ObjCObjectPointerType>(qual_type);

    if (obj_pointer_type)
      return obj_pointer_type->isObjCClassType();
  }
  return false;
}

bool TypeSystemClang::IsObjCObjectOrInterfaceType(const CompilerType &type) {
  if (ClangUtil::IsClangType(type))
    return ClangUtil::GetCanonicalQualType(type)->isObjCObjectOrInterfaceType();
  return false;
}

bool TypeSystemClang::IsClassType(lldb::opaque_compiler_type_t type) {
  if (!type)
    return false;
  clang::QualType qual_type(GetCanonicalQualType(type));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  return (type_class == clang::Type::Record);
}

bool TypeSystemClang::IsEnumType(lldb::opaque_compiler_type_t type) {
  if (!type)
    return false;
  clang::QualType qual_type(GetCanonicalQualType(type));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  return (type_class == clang::Type::Enum);
}

bool TypeSystemClang::IsPolymorphicClass(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType qual_type(GetCanonicalQualType(type));
    const clang::Type::TypeClass type_class = qual_type->getTypeClass();
    switch (type_class) {
    case clang::Type::Record:
      if (GetCompleteType(type)) {
        const clang::RecordType *record_type =
            llvm::cast<clang::RecordType>(qual_type.getTypePtr());
        const clang::RecordDecl *record_decl = record_type->getDecl();
        if (record_decl) {
          const clang::CXXRecordDecl *cxx_record_decl =
              llvm::dyn_cast<clang::CXXRecordDecl>(record_decl);
          if (cxx_record_decl) {
            // We can't just call is isPolymorphic() here because that just
            // means the current class has virtual functions, it doesn't check
            // if any inherited classes have virtual functions. The doc string
            // in SBType::IsPolymorphicClass() says it is looking for both
            // if the class has virtual methods or if any bases do, so this
            // should be more correct.
            return cxx_record_decl->isDynamicClass();
          }
        }
      }
      break;

    default:
      break;
    }
  }
  return false;
}

bool TypeSystemClang::IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                                            CompilerType *dynamic_pointee_type,
                                            bool check_cplusplus,
                                            bool check_objc) {
  clang::QualType pointee_qual_type;
  if (type) {
    clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
    bool success = false;
    const clang::Type::TypeClass type_class = qual_type->getTypeClass();
    switch (type_class) {
    case clang::Type::Builtin:
      if (check_objc &&
          llvm::cast<clang::BuiltinType>(qual_type)->getKind() ==
              clang::BuiltinType::ObjCId) {
        if (dynamic_pointee_type)
          dynamic_pointee_type->SetCompilerType(weak_from_this(), type);
        return true;
      }
      break;

    case clang::Type::ObjCObjectPointer:
      if (check_objc) {
        if (const auto *objc_pointee_type =
                qual_type->getPointeeType().getTypePtrOrNull()) {
          if (const auto *objc_object_type =
                  llvm::dyn_cast_or_null<clang::ObjCObjectType>(
                      objc_pointee_type)) {
            if (objc_object_type->isObjCClass())
              return false;
          }
        }
        if (dynamic_pointee_type)
          dynamic_pointee_type->SetCompilerType(
              weak_from_this(),
              llvm::cast<clang::ObjCObjectPointerType>(qual_type)
                  ->getPointeeType()
                  .getAsOpaquePtr());
        return true;
      }
      break;

    case clang::Type::Pointer:
      pointee_qual_type =
          llvm::cast<clang::PointerType>(qual_type)->getPointeeType();
      success = true;
      break;

    case clang::Type::LValueReference:
    case clang::Type::RValueReference:
      pointee_qual_type =
          llvm::cast<clang::ReferenceType>(qual_type)->getPointeeType();
      success = true;
      break;

    default:
      break;
    }

    if (success) {
      // Check to make sure what we are pointing too is a possible dynamic C++
      // type We currently accept any "void *" (in case we have a class that
      // has been watered down to an opaque pointer) and virtual C++ classes.
      const clang::Type::TypeClass pointee_type_class =
          pointee_qual_type.getCanonicalType()->getTypeClass();
      switch (pointee_type_class) {
      case clang::Type::Builtin:
        switch (llvm::cast<clang::BuiltinType>(pointee_qual_type)->getKind()) {
        case clang::BuiltinType::UnknownAny:
        case clang::BuiltinType::Void:
          if (dynamic_pointee_type)
            dynamic_pointee_type->SetCompilerType(
                weak_from_this(), pointee_qual_type.getAsOpaquePtr());
          return true;
        default:
          break;
        }
        break;

      case clang::Type::Record:
        if (check_cplusplus) {
          clang::CXXRecordDecl *cxx_record_decl =
              pointee_qual_type->getAsCXXRecordDecl();
          if (cxx_record_decl) {
            bool is_complete = cxx_record_decl->isCompleteDefinition();

            if (is_complete)
              success = cxx_record_decl->isDynamicClass();
            else {
              ClangASTMetadata *metadata = GetMetadata(cxx_record_decl);
              if (metadata)
                success = metadata->GetIsDynamicCXXType();
              else {
                is_complete = GetType(pointee_qual_type).GetCompleteType();
                if (is_complete)
                  success = cxx_record_decl->isDynamicClass();
                else
                  success = false;
              }
            }

            if (success) {
              if (dynamic_pointee_type)
                dynamic_pointee_type->SetCompilerType(
                    weak_from_this(), pointee_qual_type.getAsOpaquePtr());
              return true;
            }
          }
        }
        break;

      case clang::Type::ObjCObject:
      case clang::Type::ObjCInterface:
        if (check_objc) {
          if (dynamic_pointee_type)
            dynamic_pointee_type->SetCompilerType(
                weak_from_this(), pointee_qual_type.getAsOpaquePtr());
          return true;
        }
        break;

      default:
        break;
      }
    }
  }
  if (dynamic_pointee_type)
    dynamic_pointee_type->Clear();
  return false;
}

bool TypeSystemClang::IsScalarType(lldb::opaque_compiler_type_t type) {
  if (!type)
    return false;

  return (GetTypeInfo(type, nullptr) & eTypeIsScalar) != 0;
}

bool TypeSystemClang::IsTypedefType(lldb::opaque_compiler_type_t type) {
  if (!type)
    return false;
  return RemoveWrappingTypes(GetQualType(type), {clang::Type::Typedef})
             ->getTypeClass() == clang::Type::Typedef;
}

bool TypeSystemClang::IsVoidType(lldb::opaque_compiler_type_t type) {
  if (!type)
    return false;
  return GetCanonicalQualType(type)->isVoidType();
}

bool TypeSystemClang::CanPassInRegisters(const CompilerType &type) {
  if (auto *record_decl =
      TypeSystemClang::GetAsRecordDecl(type)) {
    return record_decl->canPassInRegisters();
  }
  return false;
}

bool TypeSystemClang::SupportsLanguage(lldb::LanguageType language) {
  return TypeSystemClangSupportsLanguage(language);
}

std::optional<std::string>
TypeSystemClang::GetCXXClassName(const CompilerType &type) {
  if (!type)
    return std::nullopt;

  clang::QualType qual_type(ClangUtil::GetCanonicalQualType(type));
  if (qual_type.isNull())
    return std::nullopt;

  clang::CXXRecordDecl *cxx_record_decl = qual_type->getAsCXXRecordDecl();
  if (!cxx_record_decl)
    return std::nullopt;

  return std::string(cxx_record_decl->getIdentifier()->getNameStart());
}

bool TypeSystemClang::IsCXXClassType(const CompilerType &type) {
  if (!type)
    return false;

  clang::QualType qual_type(ClangUtil::GetCanonicalQualType(type));
  return !qual_type.isNull() && qual_type->getAsCXXRecordDecl() != nullptr;
}

bool TypeSystemClang::IsBeingDefined(lldb::opaque_compiler_type_t type) {
  if (!type)
    return false;
  clang::QualType qual_type(GetCanonicalQualType(type));
  const clang::TagType *tag_type = llvm::dyn_cast<clang::TagType>(qual_type);
  if (tag_type)
    return tag_type->isBeingDefined();
  return false;
}

bool TypeSystemClang::IsObjCObjectPointerType(const CompilerType &type,
                                              CompilerType *class_type_ptr) {
  if (!ClangUtil::IsClangType(type))
    return false;

  clang::QualType qual_type(ClangUtil::GetCanonicalQualType(type));

  if (!qual_type.isNull() && qual_type->isObjCObjectPointerType()) {
    if (class_type_ptr) {
      if (!qual_type->isObjCClassType() && !qual_type->isObjCIdType()) {
        const clang::ObjCObjectPointerType *obj_pointer_type =
            llvm::dyn_cast<clang::ObjCObjectPointerType>(qual_type);
        if (obj_pointer_type == nullptr)
          class_type_ptr->Clear();
        else
          class_type_ptr->SetCompilerType(
              type.GetTypeSystem(),
              clang::QualType(obj_pointer_type->getInterfaceType(), 0)
                  .getAsOpaquePtr());
      }
    }
    return true;
  }
  if (class_type_ptr)
    class_type_ptr->Clear();
  return false;
}

// Type Completion

bool TypeSystemClang::GetCompleteType(lldb::opaque_compiler_type_t type) {
  if (!type)
    return false;
  const bool allow_completion = true;
  return GetCompleteQualType(&getASTContext(), GetQualType(type),
                             allow_completion);
}

ConstString TypeSystemClang::GetTypeName(lldb::opaque_compiler_type_t type,
                                         bool base_only) {
  if (!type)
    return ConstString();

  clang::QualType qual_type(GetQualType(type));

  // Remove certain type sugar from the name. Sugar such as elaborated types
  // or template types which only serve to improve diagnostics shouldn't
  // act as their own types from the user's perspective (e.g., formatter
  // shouldn't format a variable differently depending on how the ser has
  // specified the type. '::Type' and 'Type' should behave the same).
  // Typedefs and atomic derived types are not removed as they are actually
  // useful for identifiying specific types.
  qual_type = RemoveWrappingTypes(qual_type,
                                  {clang::Type::Typedef, clang::Type::Atomic});

  // For a typedef just return the qualified name.
  if (const auto *typedef_type = qual_type->getAs<clang::TypedefType>()) {
    const clang::TypedefNameDecl *typedef_decl = typedef_type->getDecl();
    return ConstString(GetTypeNameForDecl(typedef_decl));
  }

  // For consistency, this follows the same code path that clang uses to emit
  // debug info. This also handles when we don't want any scopes preceding the
  // name.
  if (auto *named_decl = qual_type->getAsTagDecl())
    return ConstString(GetTypeNameForDecl(named_decl, !base_only));

  return ConstString(qual_type.getAsString(GetTypePrintingPolicy()));
}

ConstString
TypeSystemClang::GetDisplayTypeName(lldb::opaque_compiler_type_t type) {
  if (!type)
    return ConstString();

  clang::QualType qual_type(GetQualType(type));
  clang::PrintingPolicy printing_policy(getASTContext().getPrintingPolicy());
  printing_policy.SuppressTagKeyword = true;
  printing_policy.SuppressScope = false;
  printing_policy.SuppressUnwrittenScope = true;
  printing_policy.SuppressInlineNamespace = true;
  return ConstString(qual_type.getAsString(printing_policy));
}

uint32_t
TypeSystemClang::GetTypeInfo(lldb::opaque_compiler_type_t type,
                             CompilerType *pointee_or_element_clang_type) {
  if (!type)
    return 0;

  if (pointee_or_element_clang_type)
    pointee_or_element_clang_type->Clear();

  clang::QualType qual_type =
      RemoveWrappingTypes(GetQualType(type), {clang::Type::Typedef});

  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Attributed:
    return GetTypeInfo(qual_type->castAs<clang::AttributedType>()
                           ->getModifiedType()
                           .getAsOpaquePtr(),
                       pointee_or_element_clang_type);
  case clang::Type::Builtin: {
    const clang::BuiltinType *builtin_type =
        llvm::cast<clang::BuiltinType>(qual_type->getCanonicalTypeInternal());

    uint32_t builtin_type_flags = eTypeIsBuiltIn | eTypeHasValue;
    switch (builtin_type->getKind()) {
    case clang::BuiltinType::ObjCId:
    case clang::BuiltinType::ObjCClass:
      if (pointee_or_element_clang_type)
        pointee_or_element_clang_type->SetCompilerType(
            weak_from_this(),
            getASTContext().ObjCBuiltinClassTy.getAsOpaquePtr());
      builtin_type_flags |= eTypeIsPointer | eTypeIsObjC;
      break;

    case clang::BuiltinType::ObjCSel:
      if (pointee_or_element_clang_type)
        pointee_or_element_clang_type->SetCompilerType(
            weak_from_this(), getASTContext().CharTy.getAsOpaquePtr());
      builtin_type_flags |= eTypeIsPointer | eTypeIsObjC;
      break;

    case clang::BuiltinType::Bool:
    case clang::BuiltinType::Char_U:
    case clang::BuiltinType::UChar:
    case clang::BuiltinType::WChar_U:
    case clang::BuiltinType::Char16:
    case clang::BuiltinType::Char32:
    case clang::BuiltinType::UShort:
    case clang::BuiltinType::UInt:
    case clang::BuiltinType::ULong:
    case clang::BuiltinType::ULongLong:
    case clang::BuiltinType::UInt128:
    case clang::BuiltinType::Char_S:
    case clang::BuiltinType::SChar:
    case clang::BuiltinType::WChar_S:
    case clang::BuiltinType::Short:
    case clang::BuiltinType::Int:
    case clang::BuiltinType::Long:
    case clang::BuiltinType::LongLong:
    case clang::BuiltinType::Int128:
    case clang::BuiltinType::Float:
    case clang::BuiltinType::Double:
    case clang::BuiltinType::LongDouble:
      builtin_type_flags |= eTypeIsScalar;
      if (builtin_type->isInteger()) {
        builtin_type_flags |= eTypeIsInteger;
        if (builtin_type->isSignedInteger())
          builtin_type_flags |= eTypeIsSigned;
      } else if (builtin_type->isFloatingPoint())
        builtin_type_flags |= eTypeIsFloat;
      break;
    default:
      break;
    }
    return builtin_type_flags;
  }

  case clang::Type::BlockPointer:
    if (pointee_or_element_clang_type)
      pointee_or_element_clang_type->SetCompilerType(
          weak_from_this(), qual_type->getPointeeType().getAsOpaquePtr());
    return eTypeIsPointer | eTypeHasChildren | eTypeIsBlock;

  case clang::Type::Complex: {
    uint32_t complex_type_flags =
        eTypeIsBuiltIn | eTypeHasValue | eTypeIsComplex;
    const clang::ComplexType *complex_type = llvm::dyn_cast<clang::ComplexType>(
        qual_type->getCanonicalTypeInternal());
    if (complex_type) {
      clang::QualType complex_element_type(complex_type->getElementType());
      if (complex_element_type->isIntegerType())
        complex_type_flags |= eTypeIsFloat;
      else if (complex_element_type->isFloatingType())
        complex_type_flags |= eTypeIsInteger;
    }
    return complex_type_flags;
  } break;

  case clang::Type::ConstantArray:
  case clang::Type::DependentSizedArray:
  case clang::Type::IncompleteArray:
  case clang::Type::VariableArray:
    if (pointee_or_element_clang_type)
      pointee_or_element_clang_type->SetCompilerType(
          weak_from_this(), llvm::cast<clang::ArrayType>(qual_type.getTypePtr())
                                ->getElementType()
                                .getAsOpaquePtr());
    return eTypeHasChildren | eTypeIsArray;

  case clang::Type::DependentName:
    return 0;
  case clang::Type::DependentSizedExtVector:
    return eTypeHasChildren | eTypeIsVector;
  case clang::Type::DependentTemplateSpecialization:
    return eTypeIsTemplate;

  case clang::Type::Enum:
    if (pointee_or_element_clang_type)
      pointee_or_element_clang_type->SetCompilerType(
          weak_from_this(), llvm::cast<clang::EnumType>(qual_type)
                                ->getDecl()
                                ->getIntegerType()
                                .getAsOpaquePtr());
    return eTypeIsEnumeration | eTypeHasValue;

  case clang::Type::FunctionProto:
    return eTypeIsFuncPrototype | eTypeHasValue;
  case clang::Type::FunctionNoProto:
    return eTypeIsFuncPrototype | eTypeHasValue;
  case clang::Type::InjectedClassName:
    return 0;

  case clang::Type::LValueReference:
  case clang::Type::RValueReference:
    if (pointee_or_element_clang_type)
      pointee_or_element_clang_type->SetCompilerType(
          weak_from_this(),
          llvm::cast<clang::ReferenceType>(qual_type.getTypePtr())
              ->getPointeeType()
              .getAsOpaquePtr());
    return eTypeHasChildren | eTypeIsReference | eTypeHasValue;

  case clang::Type::MemberPointer:
    return eTypeIsPointer | eTypeIsMember | eTypeHasValue;

  case clang::Type::ObjCObjectPointer:
    if (pointee_or_element_clang_type)
      pointee_or_element_clang_type->SetCompilerType(
          weak_from_this(), qual_type->getPointeeType().getAsOpaquePtr());
    return eTypeHasChildren | eTypeIsObjC | eTypeIsClass | eTypeIsPointer |
           eTypeHasValue;

  case clang::Type::ObjCObject:
    return eTypeHasChildren | eTypeIsObjC | eTypeIsClass;
  case clang::Type::ObjCInterface:
    return eTypeHasChildren | eTypeIsObjC | eTypeIsClass;

  case clang::Type::Pointer:
    if (pointee_or_element_clang_type)
      pointee_or_element_clang_type->SetCompilerType(
          weak_from_this(), qual_type->getPointeeType().getAsOpaquePtr());
    return eTypeHasChildren | eTypeIsPointer | eTypeHasValue;

  case clang::Type::Record:
    if (qual_type->getAsCXXRecordDecl())
      return eTypeHasChildren | eTypeIsClass | eTypeIsCPlusPlus;
    else
      return eTypeHasChildren | eTypeIsStructUnion;
    break;
  case clang::Type::SubstTemplateTypeParm:
    return eTypeIsTemplate;
  case clang::Type::TemplateTypeParm:
    return eTypeIsTemplate;
  case clang::Type::TemplateSpecialization:
    return eTypeIsTemplate;

  case clang::Type::Typedef:
    return eTypeIsTypedef | GetType(llvm::cast<clang::TypedefType>(qual_type)
                                        ->getDecl()
                                        ->getUnderlyingType())
                                .GetTypeInfo(pointee_or_element_clang_type);
  case clang::Type::UnresolvedUsing:
    return 0;

  case clang::Type::ExtVector:
  case clang::Type::Vector: {
    uint32_t vector_type_flags = eTypeHasChildren | eTypeIsVector;
    const clang::VectorType *vector_type = llvm::dyn_cast<clang::VectorType>(
        qual_type->getCanonicalTypeInternal());
    if (vector_type) {
      if (vector_type->isIntegerType())
        vector_type_flags |= eTypeIsFloat;
      else if (vector_type->isFloatingType())
        vector_type_flags |= eTypeIsInteger;
    }
    return vector_type_flags;
  }
  default:
    return 0;
  }
  return 0;
}

lldb::LanguageType
TypeSystemClang::GetMinimumLanguage(lldb::opaque_compiler_type_t type) {
  if (!type)
    return lldb::eLanguageTypeC;

  // If the type is a reference, then resolve it to what it refers to first:
  clang::QualType qual_type(GetCanonicalQualType(type).getNonReferenceType());
  if (qual_type->isAnyPointerType()) {
    if (qual_type->isObjCObjectPointerType())
      return lldb::eLanguageTypeObjC;
    if (qual_type->getPointeeCXXRecordDecl())
      return lldb::eLanguageTypeC_plus_plus;

    clang::QualType pointee_type(qual_type->getPointeeType());
    if (pointee_type->getPointeeCXXRecordDecl())
      return lldb::eLanguageTypeC_plus_plus;
    if (pointee_type->isObjCObjectOrInterfaceType())
      return lldb::eLanguageTypeObjC;
    if (pointee_type->isObjCClassType())
      return lldb::eLanguageTypeObjC;
    if (pointee_type.getTypePtr() ==
        getASTContext().ObjCBuiltinIdTy.getTypePtr())
      return lldb::eLanguageTypeObjC;
  } else {
    if (qual_type->isObjCObjectOrInterfaceType())
      return lldb::eLanguageTypeObjC;
    if (qual_type->getAsCXXRecordDecl())
      return lldb::eLanguageTypeC_plus_plus;
    switch (qual_type->getTypeClass()) {
    default:
      break;
    case clang::Type::Builtin:
      switch (llvm::cast<clang::BuiltinType>(qual_type)->getKind()) {
      default:
      case clang::BuiltinType::Void:
      case clang::BuiltinType::Bool:
      case clang::BuiltinType::Char_U:
      case clang::BuiltinType::UChar:
      case clang::BuiltinType::WChar_U:
      case clang::BuiltinType::Char16:
      case clang::BuiltinType::Char32:
      case clang::BuiltinType::UShort:
      case clang::BuiltinType::UInt:
      case clang::BuiltinType::ULong:
      case clang::BuiltinType::ULongLong:
      case clang::BuiltinType::UInt128:
      case clang::BuiltinType::Char_S:
      case clang::BuiltinType::SChar:
      case clang::BuiltinType::WChar_S:
      case clang::BuiltinType::Short:
      case clang::BuiltinType::Int:
      case clang::BuiltinType::Long:
      case clang::BuiltinType::LongLong:
      case clang::BuiltinType::Int128:
      case clang::BuiltinType::Float:
      case clang::BuiltinType::Double:
      case clang::BuiltinType::LongDouble:
        break;

      case clang::BuiltinType::NullPtr:
        return eLanguageTypeC_plus_plus;

      case clang::BuiltinType::ObjCId:
      case clang::BuiltinType::ObjCClass:
      case clang::BuiltinType::ObjCSel:
        return eLanguageTypeObjC;

      case clang::BuiltinType::Dependent:
      case clang::BuiltinType::Overload:
      case clang::BuiltinType::BoundMember:
      case clang::BuiltinType::UnknownAny:
        break;
      }
      break;
    case clang::Type::Typedef:
      return GetType(llvm::cast<clang::TypedefType>(qual_type)
                         ->getDecl()
                         ->getUnderlyingType())
          .GetMinimumLanguage();
    }
  }
  return lldb::eLanguageTypeC;
}

lldb::TypeClass
TypeSystemClang::GetTypeClass(lldb::opaque_compiler_type_t type) {
  if (!type)
    return lldb::eTypeClassInvalid;

  clang::QualType qual_type =
      RemoveWrappingTypes(GetQualType(type), {clang::Type::Typedef});

  switch (qual_type->getTypeClass()) {
  case clang::Type::Atomic:
  case clang::Type::Auto:
  case clang::Type::CountAttributed:
  case clang::Type::Decltype:
  case clang::Type::Elaborated:
  case clang::Type::Paren:
  case clang::Type::TypeOf:
  case clang::Type::TypeOfExpr:
  case clang::Type::Using:
    llvm_unreachable("Handled in RemoveWrappingTypes!");
  case clang::Type::UnaryTransform:
    break;
  case clang::Type::FunctionNoProto:
    return lldb::eTypeClassFunction;
  case clang::Type::FunctionProto:
    return lldb::eTypeClassFunction;
  case clang::Type::IncompleteArray:
    return lldb::eTypeClassArray;
  case clang::Type::VariableArray:
    return lldb::eTypeClassArray;
  case clang::Type::ConstantArray:
    return lldb::eTypeClassArray;
  case clang::Type::DependentSizedArray:
    return lldb::eTypeClassArray;
  case clang::Type::ArrayParameter:
    return lldb::eTypeClassArray;
  case clang::Type::DependentSizedExtVector:
    return lldb::eTypeClassVector;
  case clang::Type::DependentVector:
    return lldb::eTypeClassVector;
  case clang::Type::ExtVector:
    return lldb::eTypeClassVector;
  case clang::Type::Vector:
    return lldb::eTypeClassVector;
  case clang::Type::Builtin:
  // Ext-Int is just an integer type.
  case clang::Type::BitInt:
  case clang::Type::DependentBitInt:
    return lldb::eTypeClassBuiltin;
  case clang::Type::ObjCObjectPointer:
    return lldb::eTypeClassObjCObjectPointer;
  case clang::Type::BlockPointer:
    return lldb::eTypeClassBlockPointer;
  case clang::Type::Pointer:
    return lldb::eTypeClassPointer;
  case clang::Type::LValueReference:
    return lldb::eTypeClassReference;
  case clang::Type::RValueReference:
    return lldb::eTypeClassReference;
  case clang::Type::MemberPointer:
    return lldb::eTypeClassMemberPointer;
  case clang::Type::Complex:
    if (qual_type->isComplexType())
      return lldb::eTypeClassComplexFloat;
    else
      return lldb::eTypeClassComplexInteger;
  case clang::Type::ObjCObject:
    return lldb::eTypeClassObjCObject;
  case clang::Type::ObjCInterface:
    return lldb::eTypeClassObjCInterface;
  case clang::Type::Record: {
    const clang::RecordType *record_type =
        llvm::cast<clang::RecordType>(qual_type.getTypePtr());
    const clang::RecordDecl *record_decl = record_type->getDecl();
    if (record_decl->isUnion())
      return lldb::eTypeClassUnion;
    else if (record_decl->isStruct())
      return lldb::eTypeClassStruct;
    else
      return lldb::eTypeClassClass;
  } break;
  case clang::Type::Enum:
    return lldb::eTypeClassEnumeration;
  case clang::Type::Typedef:
    return lldb::eTypeClassTypedef;
  case clang::Type::UnresolvedUsing:
    break;

  case clang::Type::Attributed:
  case clang::Type::BTFTagAttributed:
    break;
  case clang::Type::TemplateTypeParm:
    break;
  case clang::Type::SubstTemplateTypeParm:
    break;
  case clang::Type::SubstTemplateTypeParmPack:
    break;
  case clang::Type::InjectedClassName:
    break;
  case clang::Type::DependentName:
    break;
  case clang::Type::DependentTemplateSpecialization:
    break;
  case clang::Type::PackExpansion:
    break;

  case clang::Type::TemplateSpecialization:
    break;
  case clang::Type::DeducedTemplateSpecialization:
    break;
  case clang::Type::Pipe:
    break;

  // pointer type decayed from an array or function type.
  case clang::Type::Decayed:
    break;
  case clang::Type::Adjusted:
    break;
  case clang::Type::ObjCTypeParam:
    break;

  case clang::Type::DependentAddressSpace:
    break;
  case clang::Type::MacroQualified:
    break;

  // Matrix types that we're not sure how to display at the moment.
  case clang::Type::ConstantMatrix:
  case clang::Type::DependentSizedMatrix:
    break;

  // We don't handle pack indexing yet
  case clang::Type::PackIndexing:
    break;
  }
  // We don't know hot to display this type...
  return lldb::eTypeClassOther;
}

unsigned TypeSystemClang::GetTypeQualifiers(lldb::opaque_compiler_type_t type) {
  if (type)
    return GetQualType(type).getQualifiers().getCVRQualifiers();
  return 0;
}

// Creating related types

CompilerType
TypeSystemClang::GetArrayElementType(lldb::opaque_compiler_type_t type,
                                     ExecutionContextScope *exe_scope) {
  if (type) {
    clang::QualType qual_type(GetQualType(type));

    const clang::Type *array_eletype =
        qual_type.getTypePtr()->getArrayElementTypeNoTypeQual();

    if (!array_eletype)
      return CompilerType();

    return GetType(clang::QualType(array_eletype, 0));
  }
  return CompilerType();
}

CompilerType TypeSystemClang::GetArrayType(lldb::opaque_compiler_type_t type,
                                           uint64_t size) {
  if (type) {
    clang::QualType qual_type(GetCanonicalQualType(type));
    clang::ASTContext &ast_ctx = getASTContext();
    if (size != 0)
      return GetType(ast_ctx.getConstantArrayType(
          qual_type, llvm::APInt(64, size), nullptr,
          clang::ArraySizeModifier::Normal, 0));
    else
      return GetType(ast_ctx.getIncompleteArrayType(
          qual_type, clang::ArraySizeModifier::Normal, 0));
  }

  return CompilerType();
}

CompilerType
TypeSystemClang::GetCanonicalType(lldb::opaque_compiler_type_t type) {
  if (type)
    return GetType(GetCanonicalQualType(type));
  return CompilerType();
}

static clang::QualType GetFullyUnqualifiedType_Impl(clang::ASTContext *ast,
                                                    clang::QualType qual_type) {
  if (qual_type->isPointerType())
    qual_type = ast->getPointerType(
        GetFullyUnqualifiedType_Impl(ast, qual_type->getPointeeType()));
  else if (const ConstantArrayType *arr =
               ast->getAsConstantArrayType(qual_type)) {
    qual_type = ast->getConstantArrayType(
        GetFullyUnqualifiedType_Impl(ast, arr->getElementType()),
        arr->getSize(), arr->getSizeExpr(), arr->getSizeModifier(),
        arr->getIndexTypeQualifiers().getAsOpaqueValue());
  } else
    qual_type = qual_type.getUnqualifiedType();
  qual_type.removeLocalConst();
  qual_type.removeLocalRestrict();
  qual_type.removeLocalVolatile();
  return qual_type;
}

CompilerType
TypeSystemClang::GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) {
  if (type)
    return GetType(
        GetFullyUnqualifiedType_Impl(&getASTContext(), GetQualType(type)));
  return CompilerType();
}

CompilerType
TypeSystemClang::GetEnumerationIntegerType(lldb::opaque_compiler_type_t type) {
  if (type)
    return GetEnumerationIntegerType(GetType(GetCanonicalQualType(type)));
  return CompilerType();
}

int TypeSystemClang::GetFunctionArgumentCount(
    lldb::opaque_compiler_type_t type) {
  if (type) {
    const clang::FunctionProtoType *func =
        llvm::dyn_cast<clang::FunctionProtoType>(GetCanonicalQualType(type));
    if (func)
      return func->getNumParams();
  }
  return -1;
}

CompilerType TypeSystemClang::GetFunctionArgumentTypeAtIndex(
    lldb::opaque_compiler_type_t type, size_t idx) {
  if (type) {
    const clang::FunctionProtoType *func =
        llvm::dyn_cast<clang::FunctionProtoType>(GetQualType(type));
    if (func) {
      const uint32_t num_args = func->getNumParams();
      if (idx < num_args)
        return GetType(func->getParamType(idx));
    }
  }
  return CompilerType();
}

CompilerType
TypeSystemClang::GetFunctionReturnType(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType qual_type(GetQualType(type));
    const clang::FunctionProtoType *func =
        llvm::dyn_cast<clang::FunctionProtoType>(qual_type.getTypePtr());
    if (func)
      return GetType(func->getReturnType());
  }
  return CompilerType();
}

size_t
TypeSystemClang::GetNumMemberFunctions(lldb::opaque_compiler_type_t type) {
  size_t num_functions = 0;
  if (type) {
    clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
    switch (qual_type->getTypeClass()) {
    case clang::Type::Record:
      if (GetCompleteQualType(&getASTContext(), qual_type)) {
        const clang::RecordType *record_type =
            llvm::cast<clang::RecordType>(qual_type.getTypePtr());
        const clang::RecordDecl *record_decl = record_type->getDecl();
        assert(record_decl);
        const clang::CXXRecordDecl *cxx_record_decl =
            llvm::dyn_cast<clang::CXXRecordDecl>(record_decl);
        if (cxx_record_decl)
          num_functions = std::distance(cxx_record_decl->method_begin(),
                                        cxx_record_decl->method_end());
      }
      break;

    case clang::Type::ObjCObjectPointer: {
      const clang::ObjCObjectPointerType *objc_class_type =
          qual_type->castAs<clang::ObjCObjectPointerType>();
      const clang::ObjCInterfaceType *objc_interface_type =
          objc_class_type->getInterfaceType();
      if (objc_interface_type &&
          GetCompleteType(static_cast<lldb::opaque_compiler_type_t>(
              const_cast<clang::ObjCInterfaceType *>(objc_interface_type)))) {
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_interface_type->getDecl();
        if (class_interface_decl) {
          num_functions = std::distance(class_interface_decl->meth_begin(),
                                        class_interface_decl->meth_end());
        }
      }
      break;
    }

    case clang::Type::ObjCObject:
    case clang::Type::ObjCInterface:
      if (GetCompleteType(type)) {
        const clang::ObjCObjectType *objc_class_type =
            llvm::dyn_cast<clang::ObjCObjectType>(qual_type.getTypePtr());
        if (objc_class_type) {
          clang::ObjCInterfaceDecl *class_interface_decl =
              objc_class_type->getInterface();
          if (class_interface_decl)
            num_functions = std::distance(class_interface_decl->meth_begin(),
                                          class_interface_decl->meth_end());
        }
      }
      break;

    default:
      break;
    }
  }
  return num_functions;
}

TypeMemberFunctionImpl
TypeSystemClang::GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t idx) {
  std::string name;
  MemberFunctionKind kind(MemberFunctionKind::eMemberFunctionKindUnknown);
  CompilerType clang_type;
  CompilerDecl clang_decl;
  if (type) {
    clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
    switch (qual_type->getTypeClass()) {
    case clang::Type::Record:
      if (GetCompleteQualType(&getASTContext(), qual_type)) {
        const clang::RecordType *record_type =
            llvm::cast<clang::RecordType>(qual_type.getTypePtr());
        const clang::RecordDecl *record_decl = record_type->getDecl();
        assert(record_decl);
        const clang::CXXRecordDecl *cxx_record_decl =
            llvm::dyn_cast<clang::CXXRecordDecl>(record_decl);
        if (cxx_record_decl) {
          auto method_iter = cxx_record_decl->method_begin();
          auto method_end = cxx_record_decl->method_end();
          if (idx <
              static_cast<size_t>(std::distance(method_iter, method_end))) {
            std::advance(method_iter, idx);
            clang::CXXMethodDecl *cxx_method_decl =
                method_iter->getCanonicalDecl();
            if (cxx_method_decl) {
              name = cxx_method_decl->getDeclName().getAsString();
              if (cxx_method_decl->isStatic())
                kind = lldb::eMemberFunctionKindStaticMethod;
              else if (llvm::isa<clang::CXXConstructorDecl>(cxx_method_decl))
                kind = lldb::eMemberFunctionKindConstructor;
              else if (llvm::isa<clang::CXXDestructorDecl>(cxx_method_decl))
                kind = lldb::eMemberFunctionKindDestructor;
              else
                kind = lldb::eMemberFunctionKindInstanceMethod;
              clang_type = GetType(cxx_method_decl->getType());
              clang_decl = GetCompilerDecl(cxx_method_decl);
            }
          }
        }
      }
      break;

    case clang::Type::ObjCObjectPointer: {
      const clang::ObjCObjectPointerType *objc_class_type =
          qual_type->castAs<clang::ObjCObjectPointerType>();
      const clang::ObjCInterfaceType *objc_interface_type =
          objc_class_type->getInterfaceType();
      if (objc_interface_type &&
          GetCompleteType(static_cast<lldb::opaque_compiler_type_t>(
              const_cast<clang::ObjCInterfaceType *>(objc_interface_type)))) {
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_interface_type->getDecl();
        if (class_interface_decl) {
          auto method_iter = class_interface_decl->meth_begin();
          auto method_end = class_interface_decl->meth_end();
          if (idx <
              static_cast<size_t>(std::distance(method_iter, method_end))) {
            std::advance(method_iter, idx);
            clang::ObjCMethodDecl *objc_method_decl =
                method_iter->getCanonicalDecl();
            if (objc_method_decl) {
              clang_decl = GetCompilerDecl(objc_method_decl);
              name = objc_method_decl->getSelector().getAsString();
              if (objc_method_decl->isClassMethod())
                kind = lldb::eMemberFunctionKindStaticMethod;
              else
                kind = lldb::eMemberFunctionKindInstanceMethod;
            }
          }
        }
      }
      break;
    }

    case clang::Type::ObjCObject:
    case clang::Type::ObjCInterface:
      if (GetCompleteType(type)) {
        const clang::ObjCObjectType *objc_class_type =
            llvm::dyn_cast<clang::ObjCObjectType>(qual_type.getTypePtr());
        if (objc_class_type) {
          clang::ObjCInterfaceDecl *class_interface_decl =
              objc_class_type->getInterface();
          if (class_interface_decl) {
            auto method_iter = class_interface_decl->meth_begin();
            auto method_end = class_interface_decl->meth_end();
            if (idx <
                static_cast<size_t>(std::distance(method_iter, method_end))) {
              std::advance(method_iter, idx);
              clang::ObjCMethodDecl *objc_method_decl =
                  method_iter->getCanonicalDecl();
              if (objc_method_decl) {
                clang_decl = GetCompilerDecl(objc_method_decl);
                name = objc_method_decl->getSelector().getAsString();
                if (objc_method_decl->isClassMethod())
                  kind = lldb::eMemberFunctionKindStaticMethod;
                else
                  kind = lldb::eMemberFunctionKindInstanceMethod;
              }
            }
          }
        }
      }
      break;

    default:
      break;
    }
  }

  if (kind == eMemberFunctionKindUnknown)
    return TypeMemberFunctionImpl();
  else
    return TypeMemberFunctionImpl(clang_type, clang_decl, name, kind);
}

CompilerType
TypeSystemClang::GetNonReferenceType(lldb::opaque_compiler_type_t type) {
  if (type)
    return GetType(GetQualType(type).getNonReferenceType());
  return CompilerType();
}

CompilerType
TypeSystemClang::GetPointeeType(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType qual_type(GetQualType(type));
    return GetType(qual_type.getTypePtr()->getPointeeType());
  }
  return CompilerType();
}

CompilerType
TypeSystemClang::GetPointerType(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType qual_type(GetQualType(type));

    switch (qual_type.getDesugaredType(getASTContext())->getTypeClass()) {
    case clang::Type::ObjCObject:
    case clang::Type::ObjCInterface:
      return GetType(getASTContext().getObjCObjectPointerType(qual_type));

    default:
      return GetType(getASTContext().getPointerType(qual_type));
    }
  }
  return CompilerType();
}

CompilerType
TypeSystemClang::GetLValueReferenceType(lldb::opaque_compiler_type_t type) {
  if (type)
    return GetType(getASTContext().getLValueReferenceType(GetQualType(type)));
  else
    return CompilerType();
}

CompilerType
TypeSystemClang::GetRValueReferenceType(lldb::opaque_compiler_type_t type) {
  if (type)
    return GetType(getASTContext().getRValueReferenceType(GetQualType(type)));
  else
    return CompilerType();
}

CompilerType TypeSystemClang::GetAtomicType(lldb::opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  return GetType(getASTContext().getAtomicType(GetQualType(type)));
}

CompilerType
TypeSystemClang::AddConstModifier(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType result(GetQualType(type));
    result.addConst();
    return GetType(result);
  }
  return CompilerType();
}

CompilerType
TypeSystemClang::AddPtrAuthModifier(lldb::opaque_compiler_type_t type,
                                    uint32_t payload) {
  if (type) {
    clang::ASTContext &clang_ast = getASTContext();
    auto pauth = PointerAuthQualifier::fromOpaqueValue(payload);
    clang::QualType result =
        clang_ast.getPointerAuthType(GetQualType(type), pauth);
    return GetType(result);
  }
  return CompilerType();
}

CompilerType
TypeSystemClang::AddVolatileModifier(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType result(GetQualType(type));
    result.addVolatile();
    return GetType(result);
  }
  return CompilerType();
}

CompilerType
TypeSystemClang::AddRestrictModifier(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType result(GetQualType(type));
    result.addRestrict();
    return GetType(result);
  }
  return CompilerType();
}

CompilerType TypeSystemClang::CreateTypedef(
    lldb::opaque_compiler_type_t type, const char *typedef_name,
    const CompilerDeclContext &compiler_decl_ctx, uint32_t payload) {
  if (type && typedef_name && typedef_name[0]) {
    clang::ASTContext &clang_ast = getASTContext();
    clang::QualType qual_type(GetQualType(type));

    clang::DeclContext *decl_ctx =
        TypeSystemClang::DeclContextGetAsDeclContext(compiler_decl_ctx);
    if (!decl_ctx)
      decl_ctx = getASTContext().getTranslationUnitDecl();

    clang::TypedefDecl *decl =
        clang::TypedefDecl::CreateDeserialized(clang_ast, GlobalDeclID());
    decl->setDeclContext(decl_ctx);
    decl->setDeclName(&clang_ast.Idents.get(typedef_name));
    decl->setTypeSourceInfo(clang_ast.getTrivialTypeSourceInfo(qual_type));
    decl_ctx->addDecl(decl);
    SetOwningModule(decl, TypePayloadClang(payload).GetOwningModule());

    clang::TagDecl *tdecl = nullptr;
    if (!qual_type.isNull()) {
      if (const clang::RecordType *rt = qual_type->getAs<clang::RecordType>())
        tdecl = rt->getDecl();
      if (const clang::EnumType *et = qual_type->getAs<clang::EnumType>())
        tdecl = et->getDecl();
    }

    // Check whether this declaration is an anonymous struct, union, or enum,
    // hidden behind a typedef. If so, we try to check whether we have a
    // typedef tag to attach to the original record declaration
    if (tdecl && !tdecl->getIdentifier() && !tdecl->getTypedefNameForAnonDecl())
      tdecl->setTypedefNameForAnonDecl(decl);

    decl->setAccess(clang::AS_public); // TODO respect proper access specifier

    // Get a uniqued clang::QualType for the typedef decl type
    return GetType(clang_ast.getTypedefType(decl));
  }
  return CompilerType();
}

CompilerType
TypeSystemClang::GetTypedefedType(lldb::opaque_compiler_type_t type) {
  if (type) {
    const clang::TypedefType *typedef_type = llvm::dyn_cast<clang::TypedefType>(
        RemoveWrappingTypes(GetQualType(type), {clang::Type::Typedef}));
    if (typedef_type)
      return GetType(typedef_type->getDecl()->getUnderlyingType());
  }
  return CompilerType();
}

// Create related types using the current type's AST

CompilerType TypeSystemClang::GetBasicTypeFromAST(lldb::BasicType basic_type) {
  return TypeSystemClang::GetBasicType(basic_type);
}

CompilerType TypeSystemClang::CreateGenericFunctionPrototype() {
  clang::ASTContext &ast = getASTContext();
  const FunctionType::ExtInfo generic_ext_info(
    /*noReturn=*/false,
    /*hasRegParm=*/false,
    /*regParm=*/0,
    CallingConv::CC_C,
    /*producesResult=*/false,
    /*noCallerSavedRegs=*/false,
    /*NoCfCheck=*/false,
    /*cmseNSCall=*/false);
  QualType func_type = ast.getFunctionNoProtoType(ast.VoidTy, generic_ext_info);
  return GetType(func_type);
}
// Exploring the type

const llvm::fltSemantics &
TypeSystemClang::GetFloatTypeSemantics(size_t byte_size) {
  clang::ASTContext &ast = getASTContext();
  const size_t bit_size = byte_size * 8;
  if (bit_size == ast.getTypeSize(ast.FloatTy))
    return ast.getFloatTypeSemantics(ast.FloatTy);
  else if (bit_size == ast.getTypeSize(ast.DoubleTy))
    return ast.getFloatTypeSemantics(ast.DoubleTy);
  else if (bit_size == ast.getTypeSize(ast.LongDoubleTy) ||
           bit_size == llvm::APFloat::semanticsSizeInBits(
                           ast.getFloatTypeSemantics(ast.LongDoubleTy)))
    return ast.getFloatTypeSemantics(ast.LongDoubleTy);
  else if (bit_size == ast.getTypeSize(ast.HalfTy))
    return ast.getFloatTypeSemantics(ast.HalfTy);
  return llvm::APFloatBase::Bogus();
}

std::optional<uint64_t>
TypeSystemClang::GetBitSize(lldb::opaque_compiler_type_t type,
                            ExecutionContextScope *exe_scope) {
  if (GetCompleteType(type)) {
    clang::QualType qual_type(GetCanonicalQualType(type));
    const clang::Type::TypeClass type_class = qual_type->getTypeClass();
    switch (type_class) {
    case clang::Type::Record:
      if (GetCompleteType(type))
        return getASTContext().getTypeSize(qual_type);
      else
        return std::nullopt;
      break;

    case clang::Type::ObjCInterface:
    case clang::Type::ObjCObject: {
      ExecutionContext exe_ctx(exe_scope);
      Process *process = exe_ctx.GetProcessPtr();
      if (process) {
        if (ObjCLanguageRuntime *objc_runtime =
                ObjCLanguageRuntime::Get(*process)) {
          if (std::optional<uint64_t> bit_size =
                  objc_runtime->GetTypeBitSize(GetType(qual_type)))
            return *bit_size;
        }
      } else {
        static bool g_printed = false;
        if (!g_printed) {
          StreamString s;
          DumpTypeDescription(type, s);

          llvm::outs() << "warning: trying to determine the size of type ";
          llvm::outs() << s.GetString() << "\n";
          llvm::outs() << "without a valid ExecutionContext. this is not "
                          "reliable. please file a bug against LLDB.\n";
          llvm::outs() << "backtrace:\n";
          llvm::sys::PrintStackTrace(llvm::outs());
          llvm::outs() << "\n";
          g_printed = true;
        }
      }
    }
      [[fallthrough]];
    default:
      const uint32_t bit_size = getASTContext().getTypeSize(qual_type);
      if (bit_size == 0) {
        if (qual_type->isIncompleteArrayType())
          return getASTContext().getTypeSize(
              qual_type->getArrayElementTypeNoTypeQual()
                  ->getCanonicalTypeUnqualified());
      }
      if (qual_type->isObjCObjectOrInterfaceType())
        return bit_size +
               getASTContext().getTypeSize(getASTContext().ObjCBuiltinClassTy);
      // Function types actually have a size of 0, that's not an error.
      if (qual_type->isFunctionProtoType())
        return bit_size;
      if (bit_size)
        return bit_size;
    }
  }
  return std::nullopt;
}

std::optional<size_t>
TypeSystemClang::GetTypeBitAlign(lldb::opaque_compiler_type_t type,
                                 ExecutionContextScope *exe_scope) {
  if (GetCompleteType(type))
    return getASTContext().getTypeAlign(GetQualType(type));
  return {};
}

lldb::Encoding TypeSystemClang::GetEncoding(lldb::opaque_compiler_type_t type,
                                            uint64_t &count) {
  if (!type)
    return lldb::eEncodingInvalid;

  count = 1;
  clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));

  switch (qual_type->getTypeClass()) {
  case clang::Type::Atomic:
  case clang::Type::Auto:
  case clang::Type::CountAttributed:
  case clang::Type::Decltype:
  case clang::Type::Elaborated:
  case clang::Type::Paren:
  case clang::Type::Typedef:
  case clang::Type::TypeOf:
  case clang::Type::TypeOfExpr:
  case clang::Type::Using:
    llvm_unreachable("Handled in RemoveWrappingTypes!");

  case clang::Type::UnaryTransform:
    break;

  case clang::Type::FunctionNoProto:
  case clang::Type::FunctionProto:
    return lldb::eEncodingUint;

  case clang::Type::IncompleteArray:
  case clang::Type::VariableArray:
  case clang::Type::ArrayParameter:
    break;

  case clang::Type::ConstantArray:
    break;

  case clang::Type::DependentVector:
  case clang::Type::ExtVector:
  case clang::Type::Vector:
    // TODO: Set this to more than one???
    break;

  case clang::Type::BitInt:
  case clang::Type::DependentBitInt:
    return qual_type->isUnsignedIntegerType() ? lldb::eEncodingUint
                                              : lldb::eEncodingSint;

  case clang::Type::Builtin:
    switch (llvm::cast<clang::BuiltinType>(qual_type)->getKind()) {
    case clang::BuiltinType::Void:
      break;

    case clang::BuiltinType::Char_S:
    case clang::BuiltinType::SChar:
    case clang::BuiltinType::WChar_S:
    case clang::BuiltinType::Short:
    case clang::BuiltinType::Int:
    case clang::BuiltinType::Long:
    case clang::BuiltinType::LongLong:
    case clang::BuiltinType::Int128:
      return lldb::eEncodingSint;

    case clang::BuiltinType::Bool:
    case clang::BuiltinType::Char_U:
    case clang::BuiltinType::UChar:
    case clang::BuiltinType::WChar_U:
    case clang::BuiltinType::Char8:
    case clang::BuiltinType::Char16:
    case clang::BuiltinType::Char32:
    case clang::BuiltinType::UShort:
    case clang::BuiltinType::UInt:
    case clang::BuiltinType::ULong:
    case clang::BuiltinType::ULongLong:
    case clang::BuiltinType::UInt128:
      return lldb::eEncodingUint;

    // Fixed point types. Note that they are currently ignored.
    case clang::BuiltinType::ShortAccum:
    case clang::BuiltinType::Accum:
    case clang::BuiltinType::LongAccum:
    case clang::BuiltinType::UShortAccum:
    case clang::BuiltinType::UAccum:
    case clang::BuiltinType::ULongAccum:
    case clang::BuiltinType::ShortFract:
    case clang::BuiltinType::Fract:
    case clang::BuiltinType::LongFract:
    case clang::BuiltinType::UShortFract:
    case clang::BuiltinType::UFract:
    case clang::BuiltinType::ULongFract:
    case clang::BuiltinType::SatShortAccum:
    case clang::BuiltinType::SatAccum:
    case clang::BuiltinType::SatLongAccum:
    case clang::BuiltinType::SatUShortAccum:
    case clang::BuiltinType::SatUAccum:
    case clang::BuiltinType::SatULongAccum:
    case clang::BuiltinType::SatShortFract:
    case clang::BuiltinType::SatFract:
    case clang::BuiltinType::SatLongFract:
    case clang::BuiltinType::SatUShortFract:
    case clang::BuiltinType::SatUFract:
    case clang::BuiltinType::SatULongFract:
      break;

    case clang::BuiltinType::Half:
    case clang::BuiltinType::Float:
    case clang::BuiltinType::Float16:
    case clang::BuiltinType::Float128:
    case clang::BuiltinType::Double:
    case clang::BuiltinType::LongDouble:
    case clang::BuiltinType::BFloat16:
    case clang::BuiltinType::Ibm128:
      return lldb::eEncodingIEEE754;

    case clang::BuiltinType::ObjCClass:
    case clang::BuiltinType::ObjCId:
    case clang::BuiltinType::ObjCSel:
      return lldb::eEncodingUint;

    case clang::BuiltinType::NullPtr:
      return lldb::eEncodingUint;

    case clang::BuiltinType::Kind::ARCUnbridgedCast:
    case clang::BuiltinType::Kind::BoundMember:
    case clang::BuiltinType::Kind::BuiltinFn:
    case clang::BuiltinType::Kind::Dependent:
    case clang::BuiltinType::Kind::OCLClkEvent:
    case clang::BuiltinType::Kind::OCLEvent:
    case clang::BuiltinType::Kind::OCLImage1dRO:
    case clang::BuiltinType::Kind::OCLImage1dWO:
    case clang::BuiltinType::Kind::OCLImage1dRW:
    case clang::BuiltinType::Kind::OCLImage1dArrayRO:
    case clang::BuiltinType::Kind::OCLImage1dArrayWO:
    case clang::BuiltinType::Kind::OCLImage1dArrayRW:
    case clang::BuiltinType::Kind::OCLImage1dBufferRO:
    case clang::BuiltinType::Kind::OCLImage1dBufferWO:
    case clang::BuiltinType::Kind::OCLImage1dBufferRW:
    case clang::BuiltinType::Kind::OCLImage2dRO:
    case clang::BuiltinType::Kind::OCLImage2dWO:
    case clang::BuiltinType::Kind::OCLImage2dRW:
    case clang::BuiltinType::Kind::OCLImage2dArrayRO:
    case clang::BuiltinType::Kind::OCLImage2dArrayWO:
    case clang::BuiltinType::Kind::OCLImage2dArrayRW:
    case clang::BuiltinType::Kind::OCLImage2dArrayDepthRO:
    case clang::BuiltinType::Kind::OCLImage2dArrayDepthWO:
    case clang::BuiltinType::Kind::OCLImage2dArrayDepthRW:
    case clang::BuiltinType::Kind::OCLImage2dArrayMSAARO:
    case clang::BuiltinType::Kind::OCLImage2dArrayMSAAWO:
    case clang::BuiltinType::Kind::OCLImage2dArrayMSAARW:
    case clang::BuiltinType::Kind::OCLImage2dArrayMSAADepthRO:
    case clang::BuiltinType::Kind::OCLImage2dArrayMSAADepthWO:
    case clang::BuiltinType::Kind::OCLImage2dArrayMSAADepthRW:
    case clang::BuiltinType::Kind::OCLImage2dDepthRO:
    case clang::BuiltinType::Kind::OCLImage2dDepthWO:
    case clang::BuiltinType::Kind::OCLImage2dDepthRW:
    case clang::BuiltinType::Kind::OCLImage2dMSAARO:
    case clang::BuiltinType::Kind::OCLImage2dMSAAWO:
    case clang::BuiltinType::Kind::OCLImage2dMSAARW:
    case clang::BuiltinType::Kind::OCLImage2dMSAADepthRO:
    case clang::BuiltinType::Kind::OCLImage2dMSAADepthWO:
    case clang::BuiltinType::Kind::OCLImage2dMSAADepthRW:
    case clang::BuiltinType::Kind::OCLImage3dRO:
    case clang::BuiltinType::Kind::OCLImage3dWO:
    case clang::BuiltinType::Kind::OCLImage3dRW:
    case clang::BuiltinType::Kind::OCLQueue:
    case clang::BuiltinType::Kind::OCLReserveID:
    case clang::BuiltinType::Kind::OCLSampler:
    case clang::BuiltinType::Kind::ArraySection:
    case clang::BuiltinType::Kind::OMPArrayShaping:
    case clang::BuiltinType::Kind::OMPIterator:
    case clang::BuiltinType::Kind::Overload:
    case clang::BuiltinType::Kind::PseudoObject:
    case clang::BuiltinType::Kind::UnknownAny:
      break;

    case clang::BuiltinType::OCLIntelSubgroupAVCMcePayload:
    case clang::BuiltinType::OCLIntelSubgroupAVCImePayload:
    case clang::BuiltinType::OCLIntelSubgroupAVCRefPayload:
    case clang::BuiltinType::OCLIntelSubgroupAVCSicPayload:
    case clang::BuiltinType::OCLIntelSubgroupAVCMceResult:
    case clang::BuiltinType::OCLIntelSubgroupAVCImeResult:
    case clang::BuiltinType::OCLIntelSubgroupAVCRefResult:
    case clang::BuiltinType::OCLIntelSubgroupAVCSicResult:
    case clang::BuiltinType::OCLIntelSubgroupAVCImeResultSingleReferenceStreamout:
    case clang::BuiltinType::OCLIntelSubgroupAVCImeResultDualReferenceStreamout:
    case clang::BuiltinType::OCLIntelSubgroupAVCImeSingleReferenceStreamin:
    case clang::BuiltinType::OCLIntelSubgroupAVCImeDualReferenceStreamin:
      break;

    // PowerPC -- Matrix Multiply Assist
    case clang::BuiltinType::VectorPair:
    case clang::BuiltinType::VectorQuad:
      break;

    // ARM -- Scalable Vector Extension
    case clang::BuiltinType::SveBool:
    case clang::BuiltinType::SveBoolx2:
    case clang::BuiltinType::SveBoolx4:
    case clang::BuiltinType::SveCount:
    case clang::BuiltinType::SveInt8:
    case clang::BuiltinType::SveInt8x2:
    case clang::BuiltinType::SveInt8x3:
    case clang::BuiltinType::SveInt8x4:
    case clang::BuiltinType::SveInt16:
    case clang::BuiltinType::SveInt16x2:
    case clang::BuiltinType::SveInt16x3:
    case clang::BuiltinType::SveInt16x4:
    case clang::BuiltinType::SveInt32:
    case clang::BuiltinType::SveInt32x2:
    case clang::BuiltinType::SveInt32x3:
    case clang::BuiltinType::SveInt32x4:
    case clang::BuiltinType::SveInt64:
    case clang::BuiltinType::SveInt64x2:
    case clang::BuiltinType::SveInt64x3:
    case clang::BuiltinType::SveInt64x4:
    case clang::BuiltinType::SveUint8:
    case clang::BuiltinType::SveUint8x2:
    case clang::BuiltinType::SveUint8x3:
    case clang::BuiltinType::SveUint8x4:
    case clang::BuiltinType::SveUint16:
    case clang::BuiltinType::SveUint16x2:
    case clang::BuiltinType::SveUint16x3:
    case clang::BuiltinType::SveUint16x4:
    case clang::BuiltinType::SveUint32:
    case clang::BuiltinType::SveUint32x2:
    case clang::BuiltinType::SveUint32x3:
    case clang::BuiltinType::SveUint32x4:
    case clang::BuiltinType::SveUint64:
    case clang::BuiltinType::SveUint64x2:
    case clang::BuiltinType::SveUint64x3:
    case clang::BuiltinType::SveUint64x4:
    case clang::BuiltinType::SveFloat16:
    case clang::BuiltinType::SveBFloat16:
    case clang::BuiltinType::SveBFloat16x2:
    case clang::BuiltinType::SveBFloat16x3:
    case clang::BuiltinType::SveBFloat16x4:
    case clang::BuiltinType::SveFloat16x2:
    case clang::BuiltinType::SveFloat16x3:
    case clang::BuiltinType::SveFloat16x4:
    case clang::BuiltinType::SveFloat32:
    case clang::BuiltinType::SveFloat32x2:
    case clang::BuiltinType::SveFloat32x3:
    case clang::BuiltinType::SveFloat32x4:
    case clang::BuiltinType::SveFloat64:
    case clang::BuiltinType::SveFloat64x2:
    case clang::BuiltinType::SveFloat64x3:
    case clang::BuiltinType::SveFloat64x4:
      break;

    // RISC-V V builtin types.
#define RVV_TYPE(Name, Id, SingletonId) case clang::BuiltinType::Id:
#include "clang/Basic/RISCVVTypes.def"
      break;

    // WebAssembly builtin types.
    case clang::BuiltinType::WasmExternRef:
      break;

    case clang::BuiltinType::IncompleteMatrixIdx:
      break;

    case clang::BuiltinType::UnresolvedTemplate:
      break;

    // AMD GPU builtin types.
#define AMDGPU_TYPE(Name, Id, SingletonId) case clang::BuiltinType::Id:
#include "clang/Basic/AMDGPUTypes.def"
      break;
    }
    break;
  // All pointer types are represented as unsigned integer encodings. We may
  // nee to add a eEncodingPointer if we ever need to know the difference
  case clang::Type::ObjCObjectPointer:
  case clang::Type::BlockPointer:
  case clang::Type::Pointer:
  case clang::Type::LValueReference:
  case clang::Type::RValueReference:
  case clang::Type::MemberPointer:
    return lldb::eEncodingUint;
  case clang::Type::Complex: {
    lldb::Encoding encoding = lldb::eEncodingIEEE754;
    if (qual_type->isComplexType())
      encoding = lldb::eEncodingIEEE754;
    else {
      const clang::ComplexType *complex_type =
          qual_type->getAsComplexIntegerType();
      if (complex_type)
        encoding = GetType(complex_type->getElementType()).GetEncoding(count);
      else
        encoding = lldb::eEncodingSint;
    }
    count = 2;
    return encoding;
  }

  case clang::Type::ObjCInterface:
    break;
  case clang::Type::Record:
    break;
  case clang::Type::Enum:
    return qual_type->isUnsignedIntegerOrEnumerationType()
               ? lldb::eEncodingUint
               : lldb::eEncodingSint;
  case clang::Type::DependentSizedArray:
  case clang::Type::DependentSizedExtVector:
  case clang::Type::UnresolvedUsing:
  case clang::Type::Attributed:
  case clang::Type::BTFTagAttributed:
  case clang::Type::TemplateTypeParm:
  case clang::Type::SubstTemplateTypeParm:
  case clang::Type::SubstTemplateTypeParmPack:
  case clang::Type::InjectedClassName:
  case clang::Type::DependentName:
  case clang::Type::DependentTemplateSpecialization:
  case clang::Type::PackExpansion:
  case clang::Type::ObjCObject:

  case clang::Type::TemplateSpecialization:
  case clang::Type::DeducedTemplateSpecialization:
  case clang::Type::Adjusted:
  case clang::Type::Pipe:
    break;

  // pointer type decayed from an array or function type.
  case clang::Type::Decayed:
    break;
  case clang::Type::ObjCTypeParam:
    break;

  case clang::Type::DependentAddressSpace:
    break;
  case clang::Type::MacroQualified:
    break;

  case clang::Type::ConstantMatrix:
  case clang::Type::DependentSizedMatrix:
    break;

  // We don't handle pack indexing yet
  case clang::Type::PackIndexing:
    break;
  }
  count = 0;
  return lldb::eEncodingInvalid;
}

lldb::Format TypeSystemClang::GetFormat(lldb::opaque_compiler_type_t type) {
  if (!type)
    return lldb::eFormatDefault;

  clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));

  switch (qual_type->getTypeClass()) {
  case clang::Type::Atomic:
  case clang::Type::Auto:
  case clang::Type::CountAttributed:
  case clang::Type::Decltype:
  case clang::Type::Elaborated:
  case clang::Type::Paren:
  case clang::Type::Typedef:
  case clang::Type::TypeOf:
  case clang::Type::TypeOfExpr:
  case clang::Type::Using:
    llvm_unreachable("Handled in RemoveWrappingTypes!");
  case clang::Type::UnaryTransform:
    break;

  case clang::Type::FunctionNoProto:
  case clang::Type::FunctionProto:
    break;

  case clang::Type::IncompleteArray:
  case clang::Type::VariableArray:
  case clang::Type::ArrayParameter:
    break;

  case clang::Type::ConstantArray:
    return lldb::eFormatVoid; // no value

  case clang::Type::DependentVector:
  case clang::Type::ExtVector:
  case clang::Type::Vector:
    break;

  case clang::Type::BitInt:
  case clang::Type::DependentBitInt:
    return qual_type->isUnsignedIntegerType() ? lldb::eFormatUnsigned
                                              : lldb::eFormatDecimal;

  case clang::Type::Builtin:
    switch (llvm::cast<clang::BuiltinType>(qual_type)->getKind()) {
    case clang::BuiltinType::UnknownAny:
    case clang::BuiltinType::Void:
    case clang::BuiltinType::BoundMember:
      break;

    case clang::BuiltinType::Bool:
      return lldb::eFormatBoolean;
    case clang::BuiltinType::Char_S:
    case clang::BuiltinType::SChar:
    case clang::BuiltinType::WChar_S:
    case clang::BuiltinType::Char_U:
    case clang::BuiltinType::UChar:
    case clang::BuiltinType::WChar_U:
      return lldb::eFormatChar;
    case clang::BuiltinType::Char8:
      return lldb::eFormatUnicode8;
    case clang::BuiltinType::Char16:
      return lldb::eFormatUnicode16;
    case clang::BuiltinType::Char32:
      return lldb::eFormatUnicode32;
    case clang::BuiltinType::UShort:
      return lldb::eFormatUnsigned;
    case clang::BuiltinType::Short:
      return lldb::eFormatDecimal;
    case clang::BuiltinType::UInt:
      return lldb::eFormatUnsigned;
    case clang::BuiltinType::Int:
      return lldb::eFormatDecimal;
    case clang::BuiltinType::ULong:
      return lldb::eFormatUnsigned;
    case clang::BuiltinType::Long:
      return lldb::eFormatDecimal;
    case clang::BuiltinType::ULongLong:
      return lldb::eFormatUnsigned;
    case clang::BuiltinType::LongLong:
      return lldb::eFormatDecimal;
    case clang::BuiltinType::UInt128:
      return lldb::eFormatUnsigned;
    case clang::BuiltinType::Int128:
      return lldb::eFormatDecimal;
    case clang::BuiltinType::Half:
    case clang::BuiltinType::Float:
    case clang::BuiltinType::Double:
    case clang::BuiltinType::LongDouble:
      return lldb::eFormatFloat;
    default:
      return lldb::eFormatHex;
    }
    break;
  case clang::Type::ObjCObjectPointer:
    return lldb::eFormatHex;
  case clang::Type::BlockPointer:
    return lldb::eFormatHex;
  case clang::Type::Pointer:
    return lldb::eFormatHex;
  case clang::Type::LValueReference:
  case clang::Type::RValueReference:
    return lldb::eFormatHex;
  case clang::Type::MemberPointer:
    return lldb::eFormatHex;
  case clang::Type::Complex: {
    if (qual_type->isComplexType())
      return lldb::eFormatComplex;
    else
      return lldb::eFormatComplexInteger;
  }
  case clang::Type::ObjCInterface:
    break;
  case clang::Type::Record:
    break;
  case clang::Type::Enum:
    return lldb::eFormatEnum;
  case clang::Type::DependentSizedArray:
  case clang::Type::DependentSizedExtVector:
  case clang::Type::UnresolvedUsing:
  case clang::Type::Attributed:
  case clang::Type::BTFTagAttributed:
  case clang::Type::TemplateTypeParm:
  case clang::Type::SubstTemplateTypeParm:
  case clang::Type::SubstTemplateTypeParmPack:
  case clang::Type::InjectedClassName:
  case clang::Type::DependentName:
  case clang::Type::DependentTemplateSpecialization:
  case clang::Type::PackExpansion:
  case clang::Type::ObjCObject:

  case clang::Type::TemplateSpecialization:
  case clang::Type::DeducedTemplateSpecialization:
  case clang::Type::Adjusted:
  case clang::Type::Pipe:
    break;

  // pointer type decayed from an array or function type.
  case clang::Type::Decayed:
    break;
  case clang::Type::ObjCTypeParam:
    break;

  case clang::Type::DependentAddressSpace:
    break;
  case clang::Type::MacroQualified:
    break;

  // Matrix types we're not sure how to display yet.
  case clang::Type::ConstantMatrix:
  case clang::Type::DependentSizedMatrix:
    break;

  // We don't handle pack indexing yet
  case clang::Type::PackIndexing:
    break;
  }
  // We don't know hot to display this type...
  return lldb::eFormatBytes;
}

static bool ObjCDeclHasIVars(clang::ObjCInterfaceDecl *class_interface_decl,
                             bool check_superclass) {
  while (class_interface_decl) {
    if (class_interface_decl->ivar_size() > 0)
      return true;

    if (check_superclass)
      class_interface_decl = class_interface_decl->getSuperClass();
    else
      break;
  }
  return false;
}

static std::optional<SymbolFile::ArrayInfo>
GetDynamicArrayInfo(TypeSystemClang &ast, SymbolFile *sym_file,
                    clang::QualType qual_type,
                    const ExecutionContext *exe_ctx) {
  if (qual_type->isIncompleteArrayType())
    if (auto *metadata = ast.GetMetadata(qual_type.getTypePtr()))
      return sym_file->GetDynamicArrayInfoForUID(metadata->GetUserID(),
                                                 exe_ctx);
  return std::nullopt;
}

llvm::Expected<uint32_t>
TypeSystemClang::GetNumChildren(lldb::opaque_compiler_type_t type,
                                bool omit_empty_base_classes,
                                const ExecutionContext *exe_ctx) {
  if (!type)
    return llvm::createStringError("invalid clang type");

  uint32_t num_children = 0;
  clang::QualType qual_type(RemoveWrappingTypes(GetQualType(type)));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Builtin:
    switch (llvm::cast<clang::BuiltinType>(qual_type)->getKind()) {
    case clang::BuiltinType::ObjCId:    // child is Class
    case clang::BuiltinType::ObjCClass: // child is Class
      num_children = 1;
      break;

    default:
      break;
    }
    break;

  case clang::Type::Complex:
    return 0;
  case clang::Type::Record:
    if (GetCompleteQualType(&getASTContext(), qual_type)) {
      const clang::RecordType *record_type =
          llvm::cast<clang::RecordType>(qual_type.getTypePtr());
      const clang::RecordDecl *record_decl = record_type->getDecl();
      assert(record_decl);
      const clang::CXXRecordDecl *cxx_record_decl =
          llvm::dyn_cast<clang::CXXRecordDecl>(record_decl);
      if (cxx_record_decl) {
        if (omit_empty_base_classes) {
          // Check each base classes to see if it or any of its base classes
          // contain any fields. This can help limit the noise in variable
          // views by not having to show base classes that contain no members.
          clang::CXXRecordDecl::base_class_const_iterator base_class,
              base_class_end;
          for (base_class = cxx_record_decl->bases_begin(),
              base_class_end = cxx_record_decl->bases_end();
               base_class != base_class_end; ++base_class) {
            const clang::CXXRecordDecl *base_class_decl =
                llvm::cast<clang::CXXRecordDecl>(
                    base_class->getType()
                        ->getAs<clang::RecordType>()
                        ->getDecl());

            // Skip empty base classes
            if (!TypeSystemClang::RecordHasFields(base_class_decl))
              continue;

            num_children++;
          }
        } else {
          // Include all base classes
          num_children += cxx_record_decl->getNumBases();
        }
      }
      num_children += std::distance(record_decl->field_begin(),
                               record_decl->field_end());
    } else
      return llvm::createStringError(
          "incomplete type \"" + GetDisplayTypeName(type).GetString() + "\"");
    break;
  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface:
    if (GetCompleteQualType(&getASTContext(), qual_type)) {
      const clang::ObjCObjectType *objc_class_type =
          llvm::dyn_cast<clang::ObjCObjectType>(qual_type.getTypePtr());
      assert(objc_class_type);
      if (objc_class_type) {
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_class_type->getInterface();

        if (class_interface_decl) {

          clang::ObjCInterfaceDecl *superclass_interface_decl =
              class_interface_decl->getSuperClass();
          if (superclass_interface_decl) {
            if (omit_empty_base_classes) {
              if (ObjCDeclHasIVars(superclass_interface_decl, true))
                ++num_children;
            } else
              ++num_children;
          }

          num_children += class_interface_decl->ivar_size();
        }
      }
    }
    break;

  case clang::Type::LValueReference:
  case clang::Type::RValueReference:
  case clang::Type::ObjCObjectPointer: {
    CompilerType pointee_clang_type(GetPointeeType(type));

    uint32_t num_pointee_children = 0;
    if (pointee_clang_type.IsAggregateType()) {
      auto num_children_or_err =
          pointee_clang_type.GetNumChildren(omit_empty_base_classes, exe_ctx);
      if (!num_children_or_err)
        return num_children_or_err;
      num_pointee_children = *num_children_or_err;
    }
    // If this type points to a simple type, then it has 1 child
    if (num_pointee_children == 0)
      num_children = 1;
    else
      num_children = num_pointee_children;
  } break;

  case clang::Type::Vector:
  case clang::Type::ExtVector:
    num_children =
        llvm::cast<clang::VectorType>(qual_type.getTypePtr())->getNumElements();
    break;

  case clang::Type::ConstantArray:
    num_children = llvm::cast<clang::ConstantArrayType>(qual_type.getTypePtr())
                       ->getSize()
                       .getLimitedValue();
    break;
  case clang::Type::IncompleteArray:
    if (auto array_info =
            GetDynamicArrayInfo(*this, GetSymbolFile(), qual_type, exe_ctx))
      // Only 1-dimensional arrays are supported.
      num_children = array_info->element_orders.size()
                         ? array_info->element_orders.back()
                         : 0;
    break;

  case clang::Type::Pointer: {
    const clang::PointerType *pointer_type =
        llvm::cast<clang::PointerType>(qual_type.getTypePtr());
    clang::QualType pointee_type(pointer_type->getPointeeType());
    CompilerType pointee_clang_type(GetType(pointee_type));
    uint32_t num_pointee_children = 0;
    if (pointee_clang_type.IsAggregateType()) {
      auto num_children_or_err =
          pointee_clang_type.GetNumChildren(omit_empty_base_classes, exe_ctx);
      if (!num_children_or_err)
        return num_children_or_err;
      num_pointee_children = *num_children_or_err;
    }
    if (num_pointee_children == 0) {
      // We have a pointer to a pointee type that claims it has no children. We
      // will want to look at
      num_children = GetNumPointeeChildren(pointee_type);
    } else
      num_children = num_pointee_children;
  } break;

  default:
    break;
  }
  return num_children;
}

CompilerType TypeSystemClang::GetBuiltinTypeByName(ConstString name) {
  return GetBasicType(GetBasicTypeEnumeration(name));
}

lldb::BasicType
TypeSystemClang::GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType qual_type(GetQualType(type));
    const clang::Type::TypeClass type_class = qual_type->getTypeClass();
    if (type_class == clang::Type::Builtin) {
      switch (llvm::cast<clang::BuiltinType>(qual_type)->getKind()) {
      case clang::BuiltinType::Void:
        return eBasicTypeVoid;
      case clang::BuiltinType::Bool:
        return eBasicTypeBool;
      case clang::BuiltinType::Char_S:
        return eBasicTypeSignedChar;
      case clang::BuiltinType::Char_U:
        return eBasicTypeUnsignedChar;
      case clang::BuiltinType::Char8:
        return eBasicTypeChar8;
      case clang::BuiltinType::Char16:
        return eBasicTypeChar16;
      case clang::BuiltinType::Char32:
        return eBasicTypeChar32;
      case clang::BuiltinType::UChar:
        return eBasicTypeUnsignedChar;
      case clang::BuiltinType::SChar:
        return eBasicTypeSignedChar;
      case clang::BuiltinType::WChar_S:
        return eBasicTypeSignedWChar;
      case clang::BuiltinType::WChar_U:
        return eBasicTypeUnsignedWChar;
      case clang::BuiltinType::Short:
        return eBasicTypeShort;
      case clang::BuiltinType::UShort:
        return eBasicTypeUnsignedShort;
      case clang::BuiltinType::Int:
        return eBasicTypeInt;
      case clang::BuiltinType::UInt:
        return eBasicTypeUnsignedInt;
      case clang::BuiltinType::Long:
        return eBasicTypeLong;
      case clang::BuiltinType::ULong:
        return eBasicTypeUnsignedLong;
      case clang::BuiltinType::LongLong:
        return eBasicTypeLongLong;
      case clang::BuiltinType::ULongLong:
        return eBasicTypeUnsignedLongLong;
      case clang::BuiltinType::Int128:
        return eBasicTypeInt128;
      case clang::BuiltinType::UInt128:
        return eBasicTypeUnsignedInt128;

      case clang::BuiltinType::Half:
        return eBasicTypeHalf;
      case clang::BuiltinType::Float:
        return eBasicTypeFloat;
      case clang::BuiltinType::Double:
        return eBasicTypeDouble;
      case clang::BuiltinType::LongDouble:
        return eBasicTypeLongDouble;

      case clang::BuiltinType::NullPtr:
        return eBasicTypeNullPtr;
      case clang::BuiltinType::ObjCId:
        return eBasicTypeObjCID;
      case clang::BuiltinType::ObjCClass:
        return eBasicTypeObjCClass;
      case clang::BuiltinType::ObjCSel:
        return eBasicTypeObjCSel;
      default:
        return eBasicTypeOther;
      }
    }
  }
  return eBasicTypeInvalid;
}

void TypeSystemClang::ForEachEnumerator(
    lldb::opaque_compiler_type_t type,
    std::function<bool(const CompilerType &integer_type,
                       ConstString name,
                       const llvm::APSInt &value)> const &callback) {
  const clang::EnumType *enum_type =
      llvm::dyn_cast<clang::EnumType>(GetCanonicalQualType(type));
  if (enum_type) {
    const clang::EnumDecl *enum_decl = enum_type->getDecl();
    if (enum_decl) {
      CompilerType integer_type = GetType(enum_decl->getIntegerType());

      clang::EnumDecl::enumerator_iterator enum_pos, enum_end_pos;
      for (enum_pos = enum_decl->enumerator_begin(),
          enum_end_pos = enum_decl->enumerator_end();
           enum_pos != enum_end_pos; ++enum_pos) {
        ConstString name(enum_pos->getNameAsString().c_str());
        if (!callback(integer_type, name, enum_pos->getInitVal()))
          break;
      }
    }
  }
}

#pragma mark Aggregate Types

uint32_t TypeSystemClang::GetNumFields(lldb::opaque_compiler_type_t type) {
  if (!type)
    return 0;

  uint32_t count = 0;
  clang::QualType qual_type(RemoveWrappingTypes(GetCanonicalQualType(type)));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record:
    if (GetCompleteType(type)) {
      const clang::RecordType *record_type =
          llvm::dyn_cast<clang::RecordType>(qual_type.getTypePtr());
      if (record_type) {
        clang::RecordDecl *record_decl = record_type->getDecl();
        if (record_decl) {
          count = std::distance(record_decl->field_begin(),
                                record_decl->field_end());
        }
      }
    }
    break;

  case clang::Type::ObjCObjectPointer: {
    const clang::ObjCObjectPointerType *objc_class_type =
        qual_type->castAs<clang::ObjCObjectPointerType>();
    const clang::ObjCInterfaceType *objc_interface_type =
        objc_class_type->getInterfaceType();
    if (objc_interface_type &&
        GetCompleteType(static_cast<lldb::opaque_compiler_type_t>(
            const_cast<clang::ObjCInterfaceType *>(objc_interface_type)))) {
      clang::ObjCInterfaceDecl *class_interface_decl =
          objc_interface_type->getDecl();
      if (class_interface_decl) {
        count = class_interface_decl->ivar_size();
      }
    }
    break;
  }

  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface:
    if (GetCompleteType(type)) {
      const clang::ObjCObjectType *objc_class_type =
          llvm::dyn_cast<clang::ObjCObjectType>(qual_type.getTypePtr());
      if (objc_class_type) {
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_class_type->getInterface();

        if (class_interface_decl)
          count = class_interface_decl->ivar_size();
      }
    }
    break;

  default:
    break;
  }
  return count;
}

static lldb::opaque_compiler_type_t
GetObjCFieldAtIndex(clang::ASTContext *ast,
                    clang::ObjCInterfaceDecl *class_interface_decl, size_t idx,
                    std::string &name, uint64_t *bit_offset_ptr,
                    uint32_t *bitfield_bit_size_ptr, bool *is_bitfield_ptr) {
  if (class_interface_decl) {
    if (idx < (class_interface_decl->ivar_size())) {
      clang::ObjCInterfaceDecl::ivar_iterator ivar_pos,
          ivar_end = class_interface_decl->ivar_end();
      uint32_t ivar_idx = 0;

      for (ivar_pos = class_interface_decl->ivar_begin(); ivar_pos != ivar_end;
           ++ivar_pos, ++ivar_idx) {
        if (ivar_idx == idx) {
          const clang::ObjCIvarDecl *ivar_decl = *ivar_pos;

          clang::QualType ivar_qual_type(ivar_decl->getType());

          name.assign(ivar_decl->getNameAsString());

          if (bit_offset_ptr) {
            const clang::ASTRecordLayout &interface_layout =
                ast->getASTObjCInterfaceLayout(class_interface_decl);
            *bit_offset_ptr = interface_layout.getFieldOffset(ivar_idx);
          }

          const bool is_bitfield = ivar_pos->isBitField();

          if (bitfield_bit_size_ptr) {
            *bitfield_bit_size_ptr = 0;

            if (is_bitfield && ast) {
              clang::Expr *bitfield_bit_size_expr = ivar_pos->getBitWidth();
              clang::Expr::EvalResult result;
              if (bitfield_bit_size_expr &&
                  bitfield_bit_size_expr->EvaluateAsInt(result, *ast)) {
                llvm::APSInt bitfield_apsint = result.Val.getInt();
                *bitfield_bit_size_ptr = bitfield_apsint.getLimitedValue();
              }
            }
          }
          if (is_bitfield_ptr)
            *is_bitfield_ptr = is_bitfield;

          return ivar_qual_type.getAsOpaquePtr();
        }
      }
    }
  }
  return nullptr;
}

CompilerType TypeSystemClang::GetFieldAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx, std::string &name,
                                              uint64_t *bit_offset_ptr,
                                              uint32_t *bitfield_bit_size_ptr,
                                              bool *is_bitfield_ptr) {
  if (!type)
    return CompilerType();

  clang::QualType qual_type(RemoveWrappingTypes(GetCanonicalQualType(type)));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record:
    if (GetCompleteType(type)) {
      const clang::RecordType *record_type =
          llvm::cast<clang::RecordType>(qual_type.getTypePtr());
      const clang::RecordDecl *record_decl = record_type->getDecl();
      uint32_t field_idx = 0;
      clang::RecordDecl::field_iterator field, field_end;
      for (field = record_decl->field_begin(),
          field_end = record_decl->field_end();
           field != field_end; ++field, ++field_idx) {
        if (idx == field_idx) {
          // Print the member type if requested
          // Print the member name and equal sign
          name.assign(field->getNameAsString());

          // Figure out the type byte size (field_type_info.first) and
          // alignment (field_type_info.second) from the AST context.
          if (bit_offset_ptr) {
            const clang::ASTRecordLayout &record_layout =
                getASTContext().getASTRecordLayout(record_decl);
            *bit_offset_ptr = record_layout.getFieldOffset(field_idx);
          }

          const bool is_bitfield = field->isBitField();

          if (bitfield_bit_size_ptr) {
            *bitfield_bit_size_ptr = 0;

            if (is_bitfield) {
              clang::Expr *bitfield_bit_size_expr = field->getBitWidth();
              clang::Expr::EvalResult result;
              if (bitfield_bit_size_expr &&
                  bitfield_bit_size_expr->EvaluateAsInt(result,
                                                        getASTContext())) {
                llvm::APSInt bitfield_apsint = result.Val.getInt();
                *bitfield_bit_size_ptr = bitfield_apsint.getLimitedValue();
              }
            }
          }
          if (is_bitfield_ptr)
            *is_bitfield_ptr = is_bitfield;

          return GetType(field->getType());
        }
      }
    }
    break;

  case clang::Type::ObjCObjectPointer: {
    const clang::ObjCObjectPointerType *objc_class_type =
        qual_type->castAs<clang::ObjCObjectPointerType>();
    const clang::ObjCInterfaceType *objc_interface_type =
        objc_class_type->getInterfaceType();
    if (objc_interface_type &&
        GetCompleteType(static_cast<lldb::opaque_compiler_type_t>(
            const_cast<clang::ObjCInterfaceType *>(objc_interface_type)))) {
      clang::ObjCInterfaceDecl *class_interface_decl =
          objc_interface_type->getDecl();
      if (class_interface_decl) {
        return CompilerType(
            weak_from_this(),
            GetObjCFieldAtIndex(&getASTContext(), class_interface_decl, idx,
                                name, bit_offset_ptr, bitfield_bit_size_ptr,
                                is_bitfield_ptr));
      }
    }
    break;
  }

  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface:
    if (GetCompleteType(type)) {
      const clang::ObjCObjectType *objc_class_type =
          llvm::dyn_cast<clang::ObjCObjectType>(qual_type.getTypePtr());
      assert(objc_class_type);
      if (objc_class_type) {
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_class_type->getInterface();
        return CompilerType(
            weak_from_this(),
            GetObjCFieldAtIndex(&getASTContext(), class_interface_decl, idx,
                                name, bit_offset_ptr, bitfield_bit_size_ptr,
                                is_bitfield_ptr));
      }
    }
    break;

  default:
    break;
  }
  return CompilerType();
}

uint32_t
TypeSystemClang::GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) {
  uint32_t count = 0;
  clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record:
    if (GetCompleteType(type)) {
      const clang::CXXRecordDecl *cxx_record_decl =
          qual_type->getAsCXXRecordDecl();
      if (cxx_record_decl)
        count = cxx_record_decl->getNumBases();
    }
    break;

  case clang::Type::ObjCObjectPointer:
    count = GetPointeeType(type).GetNumDirectBaseClasses();
    break;

  case clang::Type::ObjCObject:
    if (GetCompleteType(type)) {
      const clang::ObjCObjectType *objc_class_type =
          qual_type->getAsObjCQualifiedInterfaceType();
      if (objc_class_type) {
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_class_type->getInterface();

        if (class_interface_decl && class_interface_decl->getSuperClass())
          count = 1;
      }
    }
    break;
  case clang::Type::ObjCInterface:
    if (GetCompleteType(type)) {
      const clang::ObjCInterfaceType *objc_interface_type =
          qual_type->getAs<clang::ObjCInterfaceType>();
      if (objc_interface_type) {
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_interface_type->getInterface();

        if (class_interface_decl && class_interface_decl->getSuperClass())
          count = 1;
      }
    }
    break;

  default:
    break;
  }
  return count;
}

uint32_t
TypeSystemClang::GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) {
  uint32_t count = 0;
  clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record:
    if (GetCompleteType(type)) {
      const clang::CXXRecordDecl *cxx_record_decl =
          qual_type->getAsCXXRecordDecl();
      if (cxx_record_decl)
        count = cxx_record_decl->getNumVBases();
    }
    break;

  default:
    break;
  }
  return count;
}

CompilerType TypeSystemClang::GetDirectBaseClassAtIndex(
    lldb::opaque_compiler_type_t type, size_t idx, uint32_t *bit_offset_ptr) {
  clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record:
    if (GetCompleteType(type)) {
      const clang::CXXRecordDecl *cxx_record_decl =
          qual_type->getAsCXXRecordDecl();
      if (cxx_record_decl) {
        uint32_t curr_idx = 0;
        clang::CXXRecordDecl::base_class_const_iterator base_class,
            base_class_end;
        for (base_class = cxx_record_decl->bases_begin(),
            base_class_end = cxx_record_decl->bases_end();
             base_class != base_class_end; ++base_class, ++curr_idx) {
          if (curr_idx == idx) {
            if (bit_offset_ptr) {
              const clang::ASTRecordLayout &record_layout =
                  getASTContext().getASTRecordLayout(cxx_record_decl);
              const clang::CXXRecordDecl *base_class_decl =
                  llvm::cast<clang::CXXRecordDecl>(
                      base_class->getType()
                          ->castAs<clang::RecordType>()
                          ->getDecl());
              if (base_class->isVirtual())
                *bit_offset_ptr =
                    record_layout.getVBaseClassOffset(base_class_decl)
                        .getQuantity() *
                    8;
              else
                *bit_offset_ptr =
                    record_layout.getBaseClassOffset(base_class_decl)
                        .getQuantity() *
                    8;
            }
            return GetType(base_class->getType());
          }
        }
      }
    }
    break;

  case clang::Type::ObjCObjectPointer:
    return GetPointeeType(type).GetDirectBaseClassAtIndex(idx, bit_offset_ptr);

  case clang::Type::ObjCObject:
    if (idx == 0 && GetCompleteType(type)) {
      const clang::ObjCObjectType *objc_class_type =
          qual_type->getAsObjCQualifiedInterfaceType();
      if (objc_class_type) {
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_class_type->getInterface();

        if (class_interface_decl) {
          clang::ObjCInterfaceDecl *superclass_interface_decl =
              class_interface_decl->getSuperClass();
          if (superclass_interface_decl) {
            if (bit_offset_ptr)
              *bit_offset_ptr = 0;
            return GetType(getASTContext().getObjCInterfaceType(
                superclass_interface_decl));
          }
        }
      }
    }
    break;
  case clang::Type::ObjCInterface:
    if (idx == 0 && GetCompleteType(type)) {
      const clang::ObjCObjectType *objc_interface_type =
          qual_type->getAs<clang::ObjCInterfaceType>();
      if (objc_interface_type) {
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_interface_type->getInterface();

        if (class_interface_decl) {
          clang::ObjCInterfaceDecl *superclass_interface_decl =
              class_interface_decl->getSuperClass();
          if (superclass_interface_decl) {
            if (bit_offset_ptr)
              *bit_offset_ptr = 0;
            return GetType(getASTContext().getObjCInterfaceType(
                superclass_interface_decl));
          }
        }
      }
    }
    break;

  default:
    break;
  }
  return CompilerType();
}

CompilerType TypeSystemClang::GetVirtualBaseClassAtIndex(
    lldb::opaque_compiler_type_t type, size_t idx, uint32_t *bit_offset_ptr) {
  clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record:
    if (GetCompleteType(type)) {
      const clang::CXXRecordDecl *cxx_record_decl =
          qual_type->getAsCXXRecordDecl();
      if (cxx_record_decl) {
        uint32_t curr_idx = 0;
        clang::CXXRecordDecl::base_class_const_iterator base_class,
            base_class_end;
        for (base_class = cxx_record_decl->vbases_begin(),
            base_class_end = cxx_record_decl->vbases_end();
             base_class != base_class_end; ++base_class, ++curr_idx) {
          if (curr_idx == idx) {
            if (bit_offset_ptr) {
              const clang::ASTRecordLayout &record_layout =
                  getASTContext().getASTRecordLayout(cxx_record_decl);
              const clang::CXXRecordDecl *base_class_decl =
                  llvm::cast<clang::CXXRecordDecl>(
                      base_class->getType()
                          ->castAs<clang::RecordType>()
                          ->getDecl());
              *bit_offset_ptr =
                  record_layout.getVBaseClassOffset(base_class_decl)
                      .getQuantity() *
                  8;
            }
            return GetType(base_class->getType());
          }
        }
      }
    }
    break;

  default:
    break;
  }
  return CompilerType();
}

CompilerDecl
TypeSystemClang::GetStaticFieldWithName(lldb::opaque_compiler_type_t type,
                                        llvm::StringRef name) {
  clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
  switch (qual_type->getTypeClass()) {
  case clang::Type::Record: {
    if (!GetCompleteType(type))
      return CompilerDecl();

    const clang::RecordType *record_type =
        llvm::cast<clang::RecordType>(qual_type.getTypePtr());
    const clang::RecordDecl *record_decl = record_type->getDecl();

    clang::DeclarationName decl_name(&getASTContext().Idents.get(name));
    for (NamedDecl *decl : record_decl->lookup(decl_name)) {
      auto *var_decl = dyn_cast<clang::VarDecl>(decl);
      if (!var_decl || var_decl->getStorageClass() != clang::SC_Static)
        continue;

      return CompilerDecl(this, var_decl);
    }
    break;
  }

  default:
    break;
  }
  return CompilerDecl();
}

// If a pointer to a pointee type (the clang_type arg) says that it has no
// children, then we either need to trust it, or override it and return a
// different result. For example, an "int *" has one child that is an integer,
// but a function pointer doesn't have any children. Likewise if a Record type
// claims it has no children, then there really is nothing to show.
uint32_t TypeSystemClang::GetNumPointeeChildren(clang::QualType type) {
  if (type.isNull())
    return 0;

  clang::QualType qual_type = RemoveWrappingTypes(type.getCanonicalType());
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Builtin:
    switch (llvm::cast<clang::BuiltinType>(qual_type)->getKind()) {
    case clang::BuiltinType::UnknownAny:
    case clang::BuiltinType::Void:
    case clang::BuiltinType::NullPtr:
    case clang::BuiltinType::OCLEvent:
    case clang::BuiltinType::OCLImage1dRO:
    case clang::BuiltinType::OCLImage1dWO:
    case clang::BuiltinType::OCLImage1dRW:
    case clang::BuiltinType::OCLImage1dArrayRO:
    case clang::BuiltinType::OCLImage1dArrayWO:
    case clang::BuiltinType::OCLImage1dArrayRW:
    case clang::BuiltinType::OCLImage1dBufferRO:
    case clang::BuiltinType::OCLImage1dBufferWO:
    case clang::BuiltinType::OCLImage1dBufferRW:
    case clang::BuiltinType::OCLImage2dRO:
    case clang::BuiltinType::OCLImage2dWO:
    case clang::BuiltinType::OCLImage2dRW:
    case clang::BuiltinType::OCLImage2dArrayRO:
    case clang::BuiltinType::OCLImage2dArrayWO:
    case clang::BuiltinType::OCLImage2dArrayRW:
    case clang::BuiltinType::OCLImage3dRO:
    case clang::BuiltinType::OCLImage3dWO:
    case clang::BuiltinType::OCLImage3dRW:
    case clang::BuiltinType::OCLSampler:
      return 0;
    case clang::BuiltinType::Bool:
    case clang::BuiltinType::Char_U:
    case clang::BuiltinType::UChar:
    case clang::BuiltinType::WChar_U:
    case clang::BuiltinType::Char16:
    case clang::BuiltinType::Char32:
    case clang::BuiltinType::UShort:
    case clang::BuiltinType::UInt:
    case clang::BuiltinType::ULong:
    case clang::BuiltinType::ULongLong:
    case clang::BuiltinType::UInt128:
    case clang::BuiltinType::Char_S:
    case clang::BuiltinType::SChar:
    case clang::BuiltinType::WChar_S:
    case clang::BuiltinType::Short:
    case clang::BuiltinType::Int:
    case clang::BuiltinType::Long:
    case clang::BuiltinType::LongLong:
    case clang::BuiltinType::Int128:
    case clang::BuiltinType::Float:
    case clang::BuiltinType::Double:
    case clang::BuiltinType::LongDouble:
    case clang::BuiltinType::Dependent:
    case clang::BuiltinType::Overload:
    case clang::BuiltinType::ObjCId:
    case clang::BuiltinType::ObjCClass:
    case clang::BuiltinType::ObjCSel:
    case clang::BuiltinType::BoundMember:
    case clang::BuiltinType::Half:
    case clang::BuiltinType::ARCUnbridgedCast:
    case clang::BuiltinType::PseudoObject:
    case clang::BuiltinType::BuiltinFn:
    case clang::BuiltinType::ArraySection:
      return 1;
    default:
      return 0;
    }
    break;

  case clang::Type::Complex:
    return 1;
  case clang::Type::Pointer:
    return 1;
  case clang::Type::BlockPointer:
    return 0; // If block pointers don't have debug info, then no children for
              // them
  case clang::Type::LValueReference:
    return 1;
  case clang::Type::RValueReference:
    return 1;
  case clang::Type::MemberPointer:
    return 0;
  case clang::Type::ConstantArray:
    return 0;
  case clang::Type::IncompleteArray:
    return 0;
  case clang::Type::VariableArray:
    return 0;
  case clang::Type::DependentSizedArray:
    return 0;
  case clang::Type::DependentSizedExtVector:
    return 0;
  case clang::Type::Vector:
    return 0;
  case clang::Type::ExtVector:
    return 0;
  case clang::Type::FunctionProto:
    return 0; // When we function pointers, they have no children...
  case clang::Type::FunctionNoProto:
    return 0; // When we function pointers, they have no children...
  case clang::Type::UnresolvedUsing:
    return 0;
  case clang::Type::Record:
    return 0;
  case clang::Type::Enum:
    return 1;
  case clang::Type::TemplateTypeParm:
    return 1;
  case clang::Type::SubstTemplateTypeParm:
    return 1;
  case clang::Type::TemplateSpecialization:
    return 1;
  case clang::Type::InjectedClassName:
    return 0;
  case clang::Type::DependentName:
    return 1;
  case clang::Type::DependentTemplateSpecialization:
    return 1;
  case clang::Type::ObjCObject:
    return 0;
  case clang::Type::ObjCInterface:
    return 0;
  case clang::Type::ObjCObjectPointer:
    return 1;
  default:
    break;
  }
  return 0;
}

llvm::Expected<CompilerType> TypeSystemClang::GetChildCompilerTypeAtIndex(
    lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
    bool transparent_pointers, bool omit_empty_base_classes,
    bool ignore_array_bounds, std::string &child_name,
    uint32_t &child_byte_size, int32_t &child_byte_offset,
    uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
    bool &child_is_base_class, bool &child_is_deref_of_parent,
    ValueObject *valobj, uint64_t &language_flags) {
  if (!type)
    return CompilerType();

  auto get_exe_scope = [&exe_ctx]() {
    return exe_ctx ? exe_ctx->GetBestExecutionContextScope() : nullptr;
  };

  clang::QualType parent_qual_type(
      RemoveWrappingTypes(GetCanonicalQualType(type)));
  const clang::Type::TypeClass parent_type_class =
      parent_qual_type->getTypeClass();
  child_bitfield_bit_size = 0;
  child_bitfield_bit_offset = 0;
  child_is_base_class = false;
  language_flags = 0;

  auto num_children_or_err =
      GetNumChildren(type, omit_empty_base_classes, exe_ctx);
  if (!num_children_or_err)
    return num_children_or_err.takeError();

  const bool idx_is_valid = idx < *num_children_or_err;
  int32_t bit_offset;
  switch (parent_type_class) {
  case clang::Type::Builtin:
    if (idx_is_valid) {
      switch (llvm::cast<clang::BuiltinType>(parent_qual_type)->getKind()) {
      case clang::BuiltinType::ObjCId:
      case clang::BuiltinType::ObjCClass:
        child_name = "isa";
        child_byte_size =
            getASTContext().getTypeSize(getASTContext().ObjCBuiltinClassTy) /
            CHAR_BIT;
        return GetType(getASTContext().ObjCBuiltinClassTy);

      default:
        break;
      }
    }
    break;

  case clang::Type::Record:
    if (idx_is_valid && GetCompleteType(type)) {
      const clang::RecordType *record_type =
          llvm::cast<clang::RecordType>(parent_qual_type.getTypePtr());
      const clang::RecordDecl *record_decl = record_type->getDecl();
      assert(record_decl);
      const clang::ASTRecordLayout &record_layout =
          getASTContext().getASTRecordLayout(record_decl);
      uint32_t child_idx = 0;

      const clang::CXXRecordDecl *cxx_record_decl =
          llvm::dyn_cast<clang::CXXRecordDecl>(record_decl);
      if (cxx_record_decl) {
        // We might have base classes to print out first
        clang::CXXRecordDecl::base_class_const_iterator base_class,
            base_class_end;
        for (base_class = cxx_record_decl->bases_begin(),
            base_class_end = cxx_record_decl->bases_end();
             base_class != base_class_end; ++base_class) {
          const clang::CXXRecordDecl *base_class_decl = nullptr;

          // Skip empty base classes
          if (omit_empty_base_classes) {
            base_class_decl = llvm::cast<clang::CXXRecordDecl>(
                base_class->getType()->getAs<clang::RecordType>()->getDecl());
            if (!TypeSystemClang::RecordHasFields(base_class_decl))
              continue;
          }

          if (idx == child_idx) {
            if (base_class_decl == nullptr)
              base_class_decl = llvm::cast<clang::CXXRecordDecl>(
                  base_class->getType()->getAs<clang::RecordType>()->getDecl());

            if (base_class->isVirtual()) {
              bool handled = false;
              if (valobj) {
                clang::VTableContextBase *vtable_ctx =
                    getASTContext().getVTableContext();
                if (vtable_ctx)
                  handled = GetVBaseBitOffset(*vtable_ctx, *valobj,
                                              record_layout, cxx_record_decl,
                                              base_class_decl, bit_offset);
              }
              if (!handled)
                bit_offset = record_layout.getVBaseClassOffset(base_class_decl)
                                 .getQuantity() *
                             8;
            } else
              bit_offset = record_layout.getBaseClassOffset(base_class_decl)
                               .getQuantity() *
                           8;

            // Base classes should be a multiple of 8 bits in size
            child_byte_offset = bit_offset / 8;
            CompilerType base_class_clang_type = GetType(base_class->getType());
            child_name = base_class_clang_type.GetTypeName().AsCString("");
            std::optional<uint64_t> size =
                base_class_clang_type.GetBitSize(get_exe_scope());
            if (!size)
              return llvm::createStringError("no size info for base class");

            uint64_t base_class_clang_type_bit_size = *size;

            // Base classes bit sizes should be a multiple of 8 bits in size
            assert(base_class_clang_type_bit_size % 8 == 0);
            child_byte_size = base_class_clang_type_bit_size / 8;
            child_is_base_class = true;
            return base_class_clang_type;
          }
          // We don't increment the child index in the for loop since we might
          // be skipping empty base classes
          ++child_idx;
        }
      }
      // Make sure index is in range...
      uint32_t field_idx = 0;
      clang::RecordDecl::field_iterator field, field_end;
      for (field = record_decl->field_begin(),
          field_end = record_decl->field_end();
           field != field_end; ++field, ++field_idx, ++child_idx) {
        if (idx == child_idx) {
          // Print the member type if requested
          // Print the member name and equal sign
          child_name.assign(field->getNameAsString());

          // Figure out the type byte size (field_type_info.first) and
          // alignment (field_type_info.second) from the AST context.
          CompilerType field_clang_type = GetType(field->getType());
          assert(field_idx < record_layout.getFieldCount());
          std::optional<uint64_t> size =
              field_clang_type.GetByteSize(get_exe_scope());
          if (!size)
            return llvm::createStringError("no size info for field");

          child_byte_size = *size;
          const uint32_t child_bit_size = child_byte_size * 8;

          // Figure out the field offset within the current struct/union/class
          // type
          bit_offset = record_layout.getFieldOffset(field_idx);
          if (FieldIsBitfield(*field, child_bitfield_bit_size)) {
            child_bitfield_bit_offset = bit_offset % child_bit_size;
            const uint32_t child_bit_offset =
                bit_offset - child_bitfield_bit_offset;
            child_byte_offset = child_bit_offset / 8;
          } else {
            child_byte_offset = bit_offset / 8;
          }

          return field_clang_type;
        }
      }
    }
    break;

  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface:
    if (idx_is_valid && GetCompleteType(type)) {
      const clang::ObjCObjectType *objc_class_type =
          llvm::dyn_cast<clang::ObjCObjectType>(parent_qual_type.getTypePtr());
      assert(objc_class_type);
      if (objc_class_type) {
        uint32_t child_idx = 0;
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_class_type->getInterface();

        if (class_interface_decl) {

          const clang::ASTRecordLayout &interface_layout =
              getASTContext().getASTObjCInterfaceLayout(class_interface_decl);
          clang::ObjCInterfaceDecl *superclass_interface_decl =
              class_interface_decl->getSuperClass();
          if (superclass_interface_decl) {
            if (omit_empty_base_classes) {
              CompilerType base_class_clang_type =
                  GetType(getASTContext().getObjCInterfaceType(
                      superclass_interface_decl));
              if (llvm::expectedToStdOptional(
                      base_class_clang_type.GetNumChildren(
                          omit_empty_base_classes, exe_ctx))
                      .value_or(0) > 0) {
                if (idx == 0) {
                  clang::QualType ivar_qual_type(
                      getASTContext().getObjCInterfaceType(
                          superclass_interface_decl));

                  child_name.assign(
                      superclass_interface_decl->getNameAsString());

                  clang::TypeInfo ivar_type_info =
                      getASTContext().getTypeInfo(ivar_qual_type.getTypePtr());

                  child_byte_size = ivar_type_info.Width / 8;
                  child_byte_offset = 0;
                  child_is_base_class = true;

                  return GetType(ivar_qual_type);
                }

                ++child_idx;
              }
            } else
              ++child_idx;
          }

          const uint32_t superclass_idx = child_idx;

          if (idx < (child_idx + class_interface_decl->ivar_size())) {
            clang::ObjCInterfaceDecl::ivar_iterator ivar_pos,
                ivar_end = class_interface_decl->ivar_end();

            for (ivar_pos = class_interface_decl->ivar_begin();
                 ivar_pos != ivar_end; ++ivar_pos) {
              if (child_idx == idx) {
                clang::ObjCIvarDecl *ivar_decl = *ivar_pos;

                clang::QualType ivar_qual_type(ivar_decl->getType());

                child_name.assign(ivar_decl->getNameAsString());

                clang::TypeInfo ivar_type_info =
                    getASTContext().getTypeInfo(ivar_qual_type.getTypePtr());

                child_byte_size = ivar_type_info.Width / 8;

                // Figure out the field offset within the current
                // struct/union/class type For ObjC objects, we can't trust the
                // bit offset we get from the Clang AST, since that doesn't
                // account for the space taken up by unbacked properties, or
                // from the changing size of base classes that are newer than
                // this class. So if we have a process around that we can ask
                // about this object, do so.
                child_byte_offset = LLDB_INVALID_IVAR_OFFSET;
                Process *process = nullptr;
                if (exe_ctx)
                  process = exe_ctx->GetProcessPtr();
                if (process) {
                  ObjCLanguageRuntime *objc_runtime =
                      ObjCLanguageRuntime::Get(*process);
                  if (objc_runtime != nullptr) {
                    CompilerType parent_ast_type = GetType(parent_qual_type);
                    child_byte_offset = objc_runtime->GetByteOffsetForIvar(
                        parent_ast_type, ivar_decl->getNameAsString().c_str());
                  }
                }

                // Setting this to INT32_MAX to make sure we don't compute it
                // twice...
                bit_offset = INT32_MAX;

                if (child_byte_offset ==
                    static_cast<int32_t>(LLDB_INVALID_IVAR_OFFSET)) {
                  bit_offset = interface_layout.getFieldOffset(child_idx -
                                                               superclass_idx);
                  child_byte_offset = bit_offset / 8;
                }

                // Note, the ObjC Ivar Byte offset is just that, it doesn't
                // account for the bit offset of a bitfield within its
                // containing object.  So regardless of where we get the byte
                // offset from, we still need to get the bit offset for
                // bitfields from the layout.

                if (FieldIsBitfield(ivar_decl, child_bitfield_bit_size)) {
                  if (bit_offset == INT32_MAX)
                    bit_offset = interface_layout.getFieldOffset(
                        child_idx - superclass_idx);

                  child_bitfield_bit_offset = bit_offset % 8;
                }
                return GetType(ivar_qual_type);
              }
              ++child_idx;
            }
          }
        }
      }
    }
    break;

  case clang::Type::ObjCObjectPointer:
    if (idx_is_valid) {
      CompilerType pointee_clang_type(GetPointeeType(type));

      if (transparent_pointers && pointee_clang_type.IsAggregateType()) {
        child_is_deref_of_parent = false;
        bool tmp_child_is_deref_of_parent = false;
        return pointee_clang_type.GetChildCompilerTypeAtIndex(
            exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
            ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
            child_bitfield_bit_size, child_bitfield_bit_offset,
            child_is_base_class, tmp_child_is_deref_of_parent, valobj,
            language_flags);
      } else {
        child_is_deref_of_parent = true;
        const char *parent_name =
            valobj ? valobj->GetName().GetCString() : nullptr;
        if (parent_name) {
          child_name.assign(1, '*');
          child_name += parent_name;
        }

        // We have a pointer to an simple type
        if (idx == 0 && pointee_clang_type.GetCompleteType()) {
          if (std::optional<uint64_t> size =
                  pointee_clang_type.GetByteSize(get_exe_scope())) {
            child_byte_size = *size;
            child_byte_offset = 0;
            return pointee_clang_type;
          }
        }
      }
    }
    break;

  case clang::Type::Vector:
  case clang::Type::ExtVector:
    if (idx_is_valid) {
      const clang::VectorType *array =
          llvm::cast<clang::VectorType>(parent_qual_type.getTypePtr());
      if (array) {
        CompilerType element_type = GetType(array->getElementType());
        if (element_type.GetCompleteType()) {
          char element_name[64];
          ::snprintf(element_name, sizeof(element_name), "[%" PRIu64 "]",
                     static_cast<uint64_t>(idx));
          child_name.assign(element_name);
          if (std::optional<uint64_t> size =
                  element_type.GetByteSize(get_exe_scope())) {
            child_byte_size = *size;
            child_byte_offset = (int32_t)idx * (int32_t)child_byte_size;
            return element_type;
          }
        }
      }
    }
    break;

  case clang::Type::ConstantArray:
  case clang::Type::IncompleteArray:
    if (ignore_array_bounds || idx_is_valid) {
      const clang::ArrayType *array = GetQualType(type)->getAsArrayTypeUnsafe();
      if (array) {
        CompilerType element_type = GetType(array->getElementType());
        if (element_type.GetCompleteType()) {
          child_name = std::string(llvm::formatv("[{0}]", idx));
          if (std::optional<uint64_t> size =
                  element_type.GetByteSize(get_exe_scope())) {
            child_byte_size = *size;
            child_byte_offset = (int32_t)idx * (int32_t)child_byte_size;
            return element_type;
          }
        }
      }
    }
    break;

  case clang::Type::Pointer: {
    CompilerType pointee_clang_type(GetPointeeType(type));

    // Don't dereference "void *" pointers
    if (pointee_clang_type.IsVoidType())
      return CompilerType();

    if (transparent_pointers && pointee_clang_type.IsAggregateType()) {
      child_is_deref_of_parent = false;
      bool tmp_child_is_deref_of_parent = false;
      return pointee_clang_type.GetChildCompilerTypeAtIndex(
          exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
          ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
          child_bitfield_bit_size, child_bitfield_bit_offset,
          child_is_base_class, tmp_child_is_deref_of_parent, valobj,
          language_flags);
    } else {
      child_is_deref_of_parent = true;

      const char *parent_name =
          valobj ? valobj->GetName().GetCString() : nullptr;
      if (parent_name) {
        child_name.assign(1, '*');
        child_name += parent_name;
      }

      // We have a pointer to an simple type
      if (idx == 0) {
        if (std::optional<uint64_t> size =
                pointee_clang_type.GetByteSize(get_exe_scope())) {
          child_byte_size = *size;
          child_byte_offset = 0;
          return pointee_clang_type;
        }
      }
    }
    break;
  }

  case clang::Type::LValueReference:
  case clang::Type::RValueReference:
    if (idx_is_valid) {
      const clang::ReferenceType *reference_type =
          llvm::cast<clang::ReferenceType>(
              RemoveWrappingTypes(GetQualType(type)).getTypePtr());
      CompilerType pointee_clang_type =
          GetType(reference_type->getPointeeType());
      if (transparent_pointers && pointee_clang_type.IsAggregateType()) {
        child_is_deref_of_parent = false;
        bool tmp_child_is_deref_of_parent = false;
        return pointee_clang_type.GetChildCompilerTypeAtIndex(
            exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
            ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
            child_bitfield_bit_size, child_bitfield_bit_offset,
            child_is_base_class, tmp_child_is_deref_of_parent, valobj,
            language_flags);
      } else {
        const char *parent_name =
            valobj ? valobj->GetName().GetCString() : nullptr;
        if (parent_name) {
          child_name.assign(1, '&');
          child_name += parent_name;
        }

        // We have a pointer to an simple type
        if (idx == 0) {
          if (std::optional<uint64_t> size =
                  pointee_clang_type.GetByteSize(get_exe_scope())) {
            child_byte_size = *size;
            child_byte_offset = 0;
            return pointee_clang_type;
          }
        }
      }
    }
    break;

  default:
    break;
  }
  return CompilerType();
}

uint32_t TypeSystemClang::GetIndexForRecordBase(
    const clang::RecordDecl *record_decl,
    const clang::CXXBaseSpecifier *base_spec,
    bool omit_empty_base_classes) {
  uint32_t child_idx = 0;

  const clang::CXXRecordDecl *cxx_record_decl =
      llvm::dyn_cast<clang::CXXRecordDecl>(record_decl);

  if (cxx_record_decl) {
    clang::CXXRecordDecl::base_class_const_iterator base_class, base_class_end;
    for (base_class = cxx_record_decl->bases_begin(),
        base_class_end = cxx_record_decl->bases_end();
         base_class != base_class_end; ++base_class) {
      if (omit_empty_base_classes) {
        if (BaseSpecifierIsEmpty(base_class))
          continue;
      }

      if (base_class == base_spec)
        return child_idx;
      ++child_idx;
    }
  }

  return UINT32_MAX;
}

uint32_t TypeSystemClang::GetIndexForRecordChild(
    const clang::RecordDecl *record_decl, clang::NamedDecl *canonical_decl,
    bool omit_empty_base_classes) {
  uint32_t child_idx = TypeSystemClang::GetNumBaseClasses(
      llvm::dyn_cast<clang::CXXRecordDecl>(record_decl),
      omit_empty_base_classes);

  clang::RecordDecl::field_iterator field, field_end;
  for (field = record_decl->field_begin(), field_end = record_decl->field_end();
       field != field_end; ++field, ++child_idx) {
    if (field->getCanonicalDecl() == canonical_decl)
      return child_idx;
  }

  return UINT32_MAX;
}

// Look for a child member (doesn't include base classes, but it does include
// their members) in the type hierarchy. Returns an index path into
// "clang_type" on how to reach the appropriate member.
//
//    class A
//    {
//    public:
//        int m_a;
//        int m_b;
//    };
//
//    class B
//    {
//    };
//
//    class C :
//        public B,
//        public A
//    {
//    };
//
// If we have a clang type that describes "class C", and we wanted to looked
// "m_b" in it:
//
// With omit_empty_base_classes == false we would get an integer array back
// with: { 1,  1 } The first index 1 is the child index for "class A" within
// class C The second index 1 is the child index for "m_b" within class A
//
// With omit_empty_base_classes == true we would get an integer array back
// with: { 0,  1 } The first index 0 is the child index for "class A" within
// class C (since class B doesn't have any members it doesn't count) The second
// index 1 is the child index for "m_b" within class A

size_t TypeSystemClang::GetIndexOfChildMemberWithName(
    lldb::opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, std::vector<uint32_t> &child_indexes) {
  if (type && !name.empty()) {
    clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
    const clang::Type::TypeClass type_class = qual_type->getTypeClass();
    switch (type_class) {
    case clang::Type::Record:
      if (GetCompleteType(type)) {
        const clang::RecordType *record_type =
            llvm::cast<clang::RecordType>(qual_type.getTypePtr());
        const clang::RecordDecl *record_decl = record_type->getDecl();

        assert(record_decl);
        uint32_t child_idx = 0;

        const clang::CXXRecordDecl *cxx_record_decl =
            llvm::dyn_cast<clang::CXXRecordDecl>(record_decl);

        // Try and find a field that matches NAME
        clang::RecordDecl::field_iterator field, field_end;
        for (field = record_decl->field_begin(),
            field_end = record_decl->field_end();
             field != field_end; ++field, ++child_idx) {
          llvm::StringRef field_name = field->getName();
          if (field_name.empty()) {
            CompilerType field_type = GetType(field->getType());
            child_indexes.push_back(child_idx);
            if (field_type.GetIndexOfChildMemberWithName(
                    name, omit_empty_base_classes, child_indexes))
              return child_indexes.size();
            child_indexes.pop_back();

          } else if (field_name == name) {
            // We have to add on the number of base classes to this index!
            child_indexes.push_back(
                child_idx + TypeSystemClang::GetNumBaseClasses(
                                cxx_record_decl, omit_empty_base_classes));
            return child_indexes.size();
          }
        }

        if (cxx_record_decl) {
          const clang::RecordDecl *parent_record_decl = cxx_record_decl;

          // Didn't find things easily, lets let clang do its thang...
          clang::IdentifierInfo &ident_ref = getASTContext().Idents.get(name);
          clang::DeclarationName decl_name(&ident_ref);

          clang::CXXBasePaths paths;
          if (cxx_record_decl->lookupInBases(
                  [decl_name](const clang::CXXBaseSpecifier *specifier,
                              clang::CXXBasePath &path) {
                    CXXRecordDecl *record =
                      specifier->getType()->getAsCXXRecordDecl();
                    auto r = record->lookup(decl_name);
                    path.Decls = r.begin();
                    return !r.empty();
                  },
                  paths)) {
            clang::CXXBasePaths::const_paths_iterator path,
                path_end = paths.end();
            for (path = paths.begin(); path != path_end; ++path) {
              const size_t num_path_elements = path->size();
              for (size_t e = 0; e < num_path_elements; ++e) {
                clang::CXXBasePathElement elem = (*path)[e];

                child_idx = GetIndexForRecordBase(parent_record_decl, elem.Base,
                                                  omit_empty_base_classes);
                if (child_idx == UINT32_MAX) {
                  child_indexes.clear();
                  return 0;
                } else {
                  child_indexes.push_back(child_idx);
                  parent_record_decl = llvm::cast<clang::RecordDecl>(
                      elem.Base->getType()
                          ->castAs<clang::RecordType>()
                          ->getDecl());
                }
              }
              for (clang::DeclContext::lookup_iterator I = path->Decls, E;
                   I != E; ++I) {
                child_idx = GetIndexForRecordChild(
                    parent_record_decl, *I, omit_empty_base_classes);
                if (child_idx == UINT32_MAX) {
                  child_indexes.clear();
                  return 0;
                } else {
                  child_indexes.push_back(child_idx);
                }
              }
            }
            return child_indexes.size();
          }
        }
      }
      break;

    case clang::Type::ObjCObject:
    case clang::Type::ObjCInterface:
      if (GetCompleteType(type)) {
        llvm::StringRef name_sref(name);
        const clang::ObjCObjectType *objc_class_type =
            llvm::dyn_cast<clang::ObjCObjectType>(qual_type.getTypePtr());
        assert(objc_class_type);
        if (objc_class_type) {
          uint32_t child_idx = 0;
          clang::ObjCInterfaceDecl *class_interface_decl =
              objc_class_type->getInterface();

          if (class_interface_decl) {
            clang::ObjCInterfaceDecl::ivar_iterator ivar_pos,
                ivar_end = class_interface_decl->ivar_end();
            clang::ObjCInterfaceDecl *superclass_interface_decl =
                class_interface_decl->getSuperClass();

            for (ivar_pos = class_interface_decl->ivar_begin();
                 ivar_pos != ivar_end; ++ivar_pos, ++child_idx) {
              const clang::ObjCIvarDecl *ivar_decl = *ivar_pos;

              if (ivar_decl->getName() == name_sref) {
                if ((!omit_empty_base_classes && superclass_interface_decl) ||
                    (omit_empty_base_classes &&
                     ObjCDeclHasIVars(superclass_interface_decl, true)))
                  ++child_idx;

                child_indexes.push_back(child_idx);
                return child_indexes.size();
              }
            }

            if (superclass_interface_decl) {
              // The super class index is always zero for ObjC classes, so we
              // push it onto the child indexes in case we find an ivar in our
              // superclass...
              child_indexes.push_back(0);

              CompilerType superclass_clang_type =
                  GetType(getASTContext().getObjCInterfaceType(
                      superclass_interface_decl));
              if (superclass_clang_type.GetIndexOfChildMemberWithName(
                      name, omit_empty_base_classes, child_indexes)) {
                // We did find an ivar in a superclass so just return the
                // results!
                return child_indexes.size();
              }

              // We didn't find an ivar matching "name" in our superclass, pop
              // the superclass zero index that we pushed on above.
              child_indexes.pop_back();
            }
          }
        }
      }
      break;

    case clang::Type::ObjCObjectPointer: {
      CompilerType objc_object_clang_type = GetType(
          llvm::cast<clang::ObjCObjectPointerType>(qual_type.getTypePtr())
              ->getPointeeType());
      return objc_object_clang_type.GetIndexOfChildMemberWithName(
          name, omit_empty_base_classes, child_indexes);
    } break;

    case clang::Type::ConstantArray: {
      //                const clang::ConstantArrayType *array =
      //                llvm::cast<clang::ConstantArrayType>(parent_qual_type.getTypePtr());
      //                const uint64_t element_count =
      //                array->getSize().getLimitedValue();
      //
      //                if (idx < element_count)
      //                {
      //                    std::pair<uint64_t, unsigned> field_type_info =
      //                    ast->getTypeInfo(array->getElementType());
      //
      //                    char element_name[32];
      //                    ::snprintf (element_name, sizeof (element_name),
      //                    "%s[%u]", parent_name ? parent_name : "", idx);
      //
      //                    child_name.assign(element_name);
      //                    assert(field_type_info.first % 8 == 0);
      //                    child_byte_size = field_type_info.first / 8;
      //                    child_byte_offset = idx * child_byte_size;
      //                    return array->getElementType().getAsOpaquePtr();
      //                }
    } break;

    //        case clang::Type::MemberPointerType:
    //            {
    //                MemberPointerType *mem_ptr_type =
    //                llvm::cast<MemberPointerType>(qual_type.getTypePtr());
    //                clang::QualType pointee_type =
    //                mem_ptr_type->getPointeeType();
    //
    //                if (TypeSystemClang::IsAggregateType
    //                (pointee_type.getAsOpaquePtr()))
    //                {
    //                    return GetIndexOfChildWithName (ast,
    //                                                    mem_ptr_type->getPointeeType().getAsOpaquePtr(),
    //                                                    name);
    //                }
    //            }
    //            break;
    //
    case clang::Type::LValueReference:
    case clang::Type::RValueReference: {
      const clang::ReferenceType *reference_type =
          llvm::cast<clang::ReferenceType>(qual_type.getTypePtr());
      clang::QualType pointee_type(reference_type->getPointeeType());
      CompilerType pointee_clang_type = GetType(pointee_type);

      if (pointee_clang_type.IsAggregateType()) {
        return pointee_clang_type.GetIndexOfChildMemberWithName(
            name, omit_empty_base_classes, child_indexes);
      }
    } break;

    case clang::Type::Pointer: {
      CompilerType pointee_clang_type(GetPointeeType(type));

      if (pointee_clang_type.IsAggregateType()) {
        return pointee_clang_type.GetIndexOfChildMemberWithName(
            name, omit_empty_base_classes, child_indexes);
      }
    } break;

    default:
      break;
    }
  }
  return 0;
}

// Get the index of the child of "clang_type" whose name matches. This function
// doesn't descend into the children, but only looks one level deep and name
// matches can include base class names.

uint32_t
TypeSystemClang::GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                                         llvm::StringRef name,
                                         bool omit_empty_base_classes) {
  if (type && !name.empty()) {
    clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));

    const clang::Type::TypeClass type_class = qual_type->getTypeClass();

    switch (type_class) {
    case clang::Type::Record:
      if (GetCompleteType(type)) {
        const clang::RecordType *record_type =
            llvm::cast<clang::RecordType>(qual_type.getTypePtr());
        const clang::RecordDecl *record_decl = record_type->getDecl();

        assert(record_decl);
        uint32_t child_idx = 0;

        const clang::CXXRecordDecl *cxx_record_decl =
            llvm::dyn_cast<clang::CXXRecordDecl>(record_decl);

        if (cxx_record_decl) {
          clang::CXXRecordDecl::base_class_const_iterator base_class,
              base_class_end;
          for (base_class = cxx_record_decl->bases_begin(),
              base_class_end = cxx_record_decl->bases_end();
               base_class != base_class_end; ++base_class) {
            // Skip empty base classes
            clang::CXXRecordDecl *base_class_decl =
                llvm::cast<clang::CXXRecordDecl>(
                    base_class->getType()
                        ->castAs<clang::RecordType>()
                        ->getDecl());
            if (omit_empty_base_classes &&
                !TypeSystemClang::RecordHasFields(base_class_decl))
              continue;

            CompilerType base_class_clang_type = GetType(base_class->getType());
            std::string base_class_type_name(
                base_class_clang_type.GetTypeName().AsCString(""));
            if (base_class_type_name == name)
              return child_idx;
            ++child_idx;
          }
        }

        // Try and find a field that matches NAME
        clang::RecordDecl::field_iterator field, field_end;
        for (field = record_decl->field_begin(),
            field_end = record_decl->field_end();
             field != field_end; ++field, ++child_idx) {
          if (field->getName() == name)
            return child_idx;
        }
      }
      break;

    case clang::Type::ObjCObject:
    case clang::Type::ObjCInterface:
      if (GetCompleteType(type)) {
        const clang::ObjCObjectType *objc_class_type =
            llvm::dyn_cast<clang::ObjCObjectType>(qual_type.getTypePtr());
        assert(objc_class_type);
        if (objc_class_type) {
          uint32_t child_idx = 0;
          clang::ObjCInterfaceDecl *class_interface_decl =
              objc_class_type->getInterface();

          if (class_interface_decl) {
            clang::ObjCInterfaceDecl::ivar_iterator ivar_pos,
                ivar_end = class_interface_decl->ivar_end();
            clang::ObjCInterfaceDecl *superclass_interface_decl =
                class_interface_decl->getSuperClass();

            for (ivar_pos = class_interface_decl->ivar_begin();
                 ivar_pos != ivar_end; ++ivar_pos, ++child_idx) {
              const clang::ObjCIvarDecl *ivar_decl = *ivar_pos;

              if (ivar_decl->getName() == name) {
                if ((!omit_empty_base_classes && superclass_interface_decl) ||
                    (omit_empty_base_classes &&
                     ObjCDeclHasIVars(superclass_interface_decl, true)))
                  ++child_idx;

                return child_idx;
              }
            }

            if (superclass_interface_decl) {
              if (superclass_interface_decl->getName() == name)
                return 0;
            }
          }
        }
      }
      break;

    case clang::Type::ObjCObjectPointer: {
      CompilerType pointee_clang_type = GetType(
          llvm::cast<clang::ObjCObjectPointerType>(qual_type.getTypePtr())
              ->getPointeeType());
      return pointee_clang_type.GetIndexOfChildWithName(
          name, omit_empty_base_classes);
    } break;

    case clang::Type::ConstantArray: {
      //                const clang::ConstantArrayType *array =
      //                llvm::cast<clang::ConstantArrayType>(parent_qual_type.getTypePtr());
      //                const uint64_t element_count =
      //                array->getSize().getLimitedValue();
      //
      //                if (idx < element_count)
      //                {
      //                    std::pair<uint64_t, unsigned> field_type_info =
      //                    ast->getTypeInfo(array->getElementType());
      //
      //                    char element_name[32];
      //                    ::snprintf (element_name, sizeof (element_name),
      //                    "%s[%u]", parent_name ? parent_name : "", idx);
      //
      //                    child_name.assign(element_name);
      //                    assert(field_type_info.first % 8 == 0);
      //                    child_byte_size = field_type_info.first / 8;
      //                    child_byte_offset = idx * child_byte_size;
      //                    return array->getElementType().getAsOpaquePtr();
      //                }
    } break;

    //        case clang::Type::MemberPointerType:
    //            {
    //                MemberPointerType *mem_ptr_type =
    //                llvm::cast<MemberPointerType>(qual_type.getTypePtr());
    //                clang::QualType pointee_type =
    //                mem_ptr_type->getPointeeType();
    //
    //                if (TypeSystemClang::IsAggregateType
    //                (pointee_type.getAsOpaquePtr()))
    //                {
    //                    return GetIndexOfChildWithName (ast,
    //                                                    mem_ptr_type->getPointeeType().getAsOpaquePtr(),
    //                                                    name);
    //                }
    //            }
    //            break;
    //
    case clang::Type::LValueReference:
    case clang::Type::RValueReference: {
      const clang::ReferenceType *reference_type =
          llvm::cast<clang::ReferenceType>(qual_type.getTypePtr());
      CompilerType pointee_type = GetType(reference_type->getPointeeType());

      if (pointee_type.IsAggregateType()) {
        return pointee_type.GetIndexOfChildWithName(name,
                                                    omit_empty_base_classes);
      }
    } break;

    case clang::Type::Pointer: {
      const clang::PointerType *pointer_type =
          llvm::cast<clang::PointerType>(qual_type.getTypePtr());
      CompilerType pointee_type = GetType(pointer_type->getPointeeType());

      if (pointee_type.IsAggregateType()) {
        return pointee_type.GetIndexOfChildWithName(name,
                                                    omit_empty_base_classes);
      } else {
        //                    if (parent_name)
        //                    {
        //                        child_name.assign(1, '*');
        //                        child_name += parent_name;
        //                    }
        //
        //                    // We have a pointer to an simple type
        //                    if (idx == 0)
        //                    {
        //                        std::pair<uint64_t, unsigned> clang_type_info
        //                        = ast->getTypeInfo(pointee_type);
        //                        assert(clang_type_info.first % 8 == 0);
        //                        child_byte_size = clang_type_info.first / 8;
        //                        child_byte_offset = 0;
        //                        return pointee_type.getAsOpaquePtr();
        //                    }
      }
    } break;

    default:
      break;
    }
  }
  return UINT32_MAX;
}

CompilerType
TypeSystemClang::GetDirectNestedTypeWithName(lldb::opaque_compiler_type_t type,
                                             llvm::StringRef name) {
  if (!type || name.empty())
    return CompilerType();

  clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();

  switch (type_class) {
  case clang::Type::Record: {
    if (!GetCompleteType(type))
      return CompilerType();
    const clang::RecordType *record_type =
        llvm::cast<clang::RecordType>(qual_type.getTypePtr());
    const clang::RecordDecl *record_decl = record_type->getDecl();

    clang::DeclarationName decl_name(&getASTContext().Idents.get(name));
    for (NamedDecl *decl : record_decl->lookup(decl_name)) {
      if (auto *tag_decl = dyn_cast<clang::TagDecl>(decl))
        return GetType(getASTContext().getTagDeclType(tag_decl));
      if (auto *typedef_decl = dyn_cast<clang::TypedefNameDecl>(decl))
        return GetType(getASTContext().getTypedefType(typedef_decl));
    }
    break;
  }
  default:
    break;
  }
  return CompilerType();
}

bool TypeSystemClang::IsTemplateType(lldb::opaque_compiler_type_t type) {
  if (!type)
    return false;
  CompilerType ct(weak_from_this(), type);
  const clang::Type *clang_type = ClangUtil::GetQualType(ct).getTypePtr();
  if (auto *cxx_record_decl = dyn_cast<clang::TagType>(clang_type))
    return isa<clang::ClassTemplateSpecializationDecl>(
        cxx_record_decl->getDecl());
  return false;
}

size_t
TypeSystemClang::GetNumTemplateArguments(lldb::opaque_compiler_type_t type,
                                         bool expand_pack) {
  if (!type)
    return 0;

  clang::QualType qual_type = RemoveWrappingTypes(GetCanonicalQualType(type));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record:
    if (GetCompleteType(type)) {
      const clang::CXXRecordDecl *cxx_record_decl =
          qual_type->getAsCXXRecordDecl();
      if (cxx_record_decl) {
        const clang::ClassTemplateSpecializationDecl *template_decl =
            llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(
                cxx_record_decl);
        if (template_decl) {
          const auto &template_arg_list = template_decl->getTemplateArgs();
          size_t num_args = template_arg_list.size();
          assert(num_args && "template specialization without any args");
          if (expand_pack && num_args) {
            const auto &pack = template_arg_list[num_args - 1];
            if (pack.getKind() == clang::TemplateArgument::Pack)
              num_args += pack.pack_size() - 1;
          }
          return num_args;
        }
      }
    }
    break;

  default:
    break;
  }

  return 0;
}

const clang::ClassTemplateSpecializationDecl *
TypeSystemClang::GetAsTemplateSpecialization(
    lldb::opaque_compiler_type_t type) {
  if (!type)
    return nullptr;

  clang::QualType qual_type(RemoveWrappingTypes(GetCanonicalQualType(type)));
  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record: {
    if (! GetCompleteType(type))
      return nullptr;
    const clang::CXXRecordDecl *cxx_record_decl =
        qual_type->getAsCXXRecordDecl();
    if (!cxx_record_decl)
      return nullptr;
    return llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(
        cxx_record_decl);
  }

  default:
    return nullptr;
  }
}

const TemplateArgument *
GetNthTemplateArgument(const clang::ClassTemplateSpecializationDecl *decl,
                       size_t idx, bool expand_pack) {
  const auto &args = decl->getTemplateArgs();
  const size_t args_size = args.size();

  assert(args_size && "template specialization without any args");
  if (!args_size)
    return nullptr;

  const size_t last_idx = args_size - 1;

  // We're asked for a template argument that can't be a parameter pack, so
  // return it without worrying about 'expand_pack'.
  if (idx < last_idx)
    return &args[idx];

  // We're asked for the last template argument but we don't want/need to
  // expand it.
  if (!expand_pack || args[last_idx].getKind() != clang::TemplateArgument::Pack)
    return idx >= args.size() ? nullptr : &args[idx];

  // Index into the expanded pack.
  // Note that 'idx' counts from the beginning of all template arguments
  // (including the ones preceding the parameter pack).
  const auto &pack = args[last_idx];
  const size_t pack_idx = idx - last_idx;
  if (pack_idx >= pack.pack_size())
    return nullptr;
  return &pack.pack_elements()[pack_idx];
}

lldb::TemplateArgumentKind
TypeSystemClang::GetTemplateArgumentKind(lldb::opaque_compiler_type_t type,
                                         size_t arg_idx, bool expand_pack) {
  const clang::ClassTemplateSpecializationDecl *template_decl =
      GetAsTemplateSpecialization(type);
  if (!template_decl)
    return eTemplateArgumentKindNull;

  const auto *arg = GetNthTemplateArgument(template_decl, arg_idx, expand_pack);
  if (!arg)
    return eTemplateArgumentKindNull;

  switch (arg->getKind()) {
  case clang::TemplateArgument::Null:
    return eTemplateArgumentKindNull;

  case clang::TemplateArgument::NullPtr:
    return eTemplateArgumentKindNullPtr;

  case clang::TemplateArgument::Type:
    return eTemplateArgumentKindType;

  case clang::TemplateArgument::Declaration:
    return eTemplateArgumentKindDeclaration;

  case clang::TemplateArgument::Integral:
    return eTemplateArgumentKindIntegral;

  case clang::TemplateArgument::Template:
    return eTemplateArgumentKindTemplate;

  case clang::TemplateArgument::TemplateExpansion:
    return eTemplateArgumentKindTemplateExpansion;

  case clang::TemplateArgument::Expression:
    return eTemplateArgumentKindExpression;

  case clang::TemplateArgument::Pack:
    return eTemplateArgumentKindPack;

  case clang::TemplateArgument::StructuralValue:
    return eTemplateArgumentKindStructuralValue;
  }
  llvm_unreachable("Unhandled clang::TemplateArgument::ArgKind");
}

CompilerType
TypeSystemClang::GetTypeTemplateArgument(lldb::opaque_compiler_type_t type,
                                         size_t idx, bool expand_pack) {
  const clang::ClassTemplateSpecializationDecl *template_decl =
      GetAsTemplateSpecialization(type);
  if (!template_decl)
    return CompilerType();

  const auto *arg = GetNthTemplateArgument(template_decl, idx, expand_pack);
  if (!arg || arg->getKind() != clang::TemplateArgument::Type)
    return CompilerType();

  return GetType(arg->getAsType());
}

std::optional<CompilerType::IntegralTemplateArgument>
TypeSystemClang::GetIntegralTemplateArgument(lldb::opaque_compiler_type_t type,
                                             size_t idx, bool expand_pack) {
  const clang::ClassTemplateSpecializationDecl *template_decl =
      GetAsTemplateSpecialization(type);
  if (!template_decl)
    return std::nullopt;

  const auto *arg = GetNthTemplateArgument(template_decl, idx, expand_pack);
  if (!arg || arg->getKind() != clang::TemplateArgument::Integral)
    return std::nullopt;

  return {{arg->getAsIntegral(), GetType(arg->getIntegralType())}};
}

CompilerType TypeSystemClang::GetTypeForFormatters(void *type) {
  if (type)
    return ClangUtil::RemoveFastQualifiers(CompilerType(weak_from_this(), type));
  return CompilerType();
}

clang::EnumDecl *TypeSystemClang::GetAsEnumDecl(const CompilerType &type) {
  const clang::EnumType *enutype =
      llvm::dyn_cast<clang::EnumType>(ClangUtil::GetCanonicalQualType(type));
  if (enutype)
    return enutype->getDecl();
  return nullptr;
}

clang::RecordDecl *TypeSystemClang::GetAsRecordDecl(const CompilerType &type) {
  const clang::RecordType *record_type =
      llvm::dyn_cast<clang::RecordType>(ClangUtil::GetCanonicalQualType(type));
  if (record_type)
    return record_type->getDecl();
  return nullptr;
}

clang::TagDecl *TypeSystemClang::GetAsTagDecl(const CompilerType &type) {
  return ClangUtil::GetAsTagDecl(type);
}

clang::TypedefNameDecl *
TypeSystemClang::GetAsTypedefDecl(const CompilerType &type) {
  const clang::TypedefType *typedef_type =
      llvm::dyn_cast<clang::TypedefType>(ClangUtil::GetQualType(type));
  if (typedef_type)
    return typedef_type->getDecl();
  return nullptr;
}

clang::CXXRecordDecl *
TypeSystemClang::GetAsCXXRecordDecl(lldb::opaque_compiler_type_t type) {
  return GetCanonicalQualType(type)->getAsCXXRecordDecl();
}

clang::ObjCInterfaceDecl *
TypeSystemClang::GetAsObjCInterfaceDecl(const CompilerType &type) {
  const clang::ObjCObjectType *objc_class_type =
      llvm::dyn_cast<clang::ObjCObjectType>(
          ClangUtil::GetCanonicalQualType(type));
  if (objc_class_type)
    return objc_class_type->getInterface();
  return nullptr;
}

clang::FieldDecl *TypeSystemClang::AddFieldToRecordType(
    const CompilerType &type, llvm::StringRef name,
    const CompilerType &field_clang_type, AccessType access,
    uint32_t bitfield_bit_size) {
  if (!type.IsValid() || !field_clang_type.IsValid())
    return nullptr;
  auto ts = type.GetTypeSystem();
  auto ast = ts.dyn_cast_or_null<TypeSystemClang>();
  if (!ast)
    return nullptr;
  clang::ASTContext &clang_ast = ast->getASTContext();
  clang::IdentifierInfo *ident = nullptr;
  if (!name.empty())
    ident = &clang_ast.Idents.get(name);

  clang::FieldDecl *field = nullptr;

  clang::Expr *bit_width = nullptr;
  if (bitfield_bit_size != 0) {
    llvm::APInt bitfield_bit_size_apint(clang_ast.getTypeSize(clang_ast.IntTy),
                                        bitfield_bit_size);
    bit_width = new (clang_ast)
        clang::IntegerLiteral(clang_ast, bitfield_bit_size_apint,
                              clang_ast.IntTy, clang::SourceLocation());
  }

  clang::RecordDecl *record_decl = ast->GetAsRecordDecl(type);
  if (record_decl) {
    field = clang::FieldDecl::CreateDeserialized(clang_ast, GlobalDeclID());
    field->setDeclContext(record_decl);
    field->setDeclName(ident);
    field->setType(ClangUtil::GetQualType(field_clang_type));
    if (bit_width)
      field->setBitWidth(bit_width);
    SetMemberOwningModule(field, record_decl);

    if (name.empty()) {
      // Determine whether this field corresponds to an anonymous struct or
      // union.
      if (const clang::TagType *TagT =
              field->getType()->getAs<clang::TagType>()) {
        if (clang::RecordDecl *Rec =
                llvm::dyn_cast<clang::RecordDecl>(TagT->getDecl()))
          if (!Rec->getDeclName()) {
            Rec->setAnonymousStructOrUnion(true);
            field->setImplicit();
          }
      }
    }

    if (field) {
      clang::AccessSpecifier access_specifier =
          TypeSystemClang::ConvertAccessTypeToAccessSpecifier(access);
      field->setAccess(access_specifier);

      if (clang::CXXRecordDecl *cxx_record_decl =
              llvm::dyn_cast<CXXRecordDecl>(record_decl)) {
        AddAccessSpecifierDecl(cxx_record_decl, ast->getASTContext(),
                               ast->GetCXXRecordDeclAccess(cxx_record_decl),
                               access_specifier);
        ast->SetCXXRecordDeclAccess(cxx_record_decl, access_specifier);
      }
      record_decl->addDecl(field);

      VerifyDecl(field);
    }
  } else {
    clang::ObjCInterfaceDecl *class_interface_decl =
        ast->GetAsObjCInterfaceDecl(type);

    if (class_interface_decl) {
      const bool is_synthesized = false;

      field_clang_type.GetCompleteType();

      auto *ivar =
          clang::ObjCIvarDecl::CreateDeserialized(clang_ast, GlobalDeclID());
      ivar->setDeclContext(class_interface_decl);
      ivar->setDeclName(ident);
      ivar->setType(ClangUtil::GetQualType(field_clang_type));
      ivar->setAccessControl(ConvertAccessTypeToObjCIvarAccessControl(access));
      if (bit_width)
        ivar->setBitWidth(bit_width);
      ivar->setSynthesize(is_synthesized);
      field = ivar;
      SetMemberOwningModule(field, class_interface_decl);

      if (field) {
        class_interface_decl->addDecl(field);

        VerifyDecl(field);
      }
    }
  }
  return field;
}

void TypeSystemClang::BuildIndirectFields(const CompilerType &type) {
  if (!type)
    return;

  auto ts = type.GetTypeSystem();
  auto ast = ts.dyn_cast_or_null<TypeSystemClang>();
  if (!ast)
    return;

  clang::RecordDecl *record_decl = ast->GetAsRecordDecl(type);

  if (!record_decl)
    return;

  typedef llvm::SmallVector<clang::IndirectFieldDecl *, 1> IndirectFieldVector;

  IndirectFieldVector indirect_fields;
  clang::RecordDecl::field_iterator field_pos;
  clang::RecordDecl::field_iterator field_end_pos = record_decl->field_end();
  clang::RecordDecl::field_iterator last_field_pos = field_end_pos;
  for (field_pos = record_decl->field_begin(); field_pos != field_end_pos;
       last_field_pos = field_pos++) {
    if (field_pos->isAnonymousStructOrUnion()) {
      clang::QualType field_qual_type = field_pos->getType();

      const clang::RecordType *field_record_type =
          field_qual_type->getAs<clang::RecordType>();

      if (!field_record_type)
        continue;

      clang::RecordDecl *field_record_decl = field_record_type->getDecl();

      if (!field_record_decl)
        continue;

      for (clang::RecordDecl::decl_iterator
               di = field_record_decl->decls_begin(),
               de = field_record_decl->decls_end();
           di != de; ++di) {
        if (clang::FieldDecl *nested_field_decl =
                llvm::dyn_cast<clang::FieldDecl>(*di)) {
          clang::NamedDecl **chain =
              new (ast->getASTContext()) clang::NamedDecl *[2];
          chain[0] = *field_pos;
          chain[1] = nested_field_decl;
          clang::IndirectFieldDecl *indirect_field =
              clang::IndirectFieldDecl::Create(
                  ast->getASTContext(), record_decl, clang::SourceLocation(),
                  nested_field_decl->getIdentifier(),
                  nested_field_decl->getType(), {chain, 2});
          SetMemberOwningModule(indirect_field, record_decl);

          indirect_field->setImplicit();

          indirect_field->setAccess(TypeSystemClang::UnifyAccessSpecifiers(
              field_pos->getAccess(), nested_field_decl->getAccess()));

          indirect_fields.push_back(indirect_field);
        } else if (clang::IndirectFieldDecl *nested_indirect_field_decl =
                       llvm::dyn_cast<clang::IndirectFieldDecl>(*di)) {
          size_t nested_chain_size =
              nested_indirect_field_decl->getChainingSize();
          clang::NamedDecl **chain = new (ast->getASTContext())
              clang::NamedDecl *[nested_chain_size + 1];
          chain[0] = *field_pos;

          int chain_index = 1;
          for (clang::IndirectFieldDecl::chain_iterator
                   nci = nested_indirect_field_decl->chain_begin(),
                   nce = nested_indirect_field_decl->chain_end();
               nci < nce; ++nci) {
            chain[chain_index] = *nci;
            chain_index++;
          }

          clang::IndirectFieldDecl *indirect_field =
              clang::IndirectFieldDecl::Create(
                  ast->getASTContext(), record_decl, clang::SourceLocation(),
                  nested_indirect_field_decl->getIdentifier(),
                  nested_indirect_field_decl->getType(),
                  {chain, nested_chain_size + 1});
          SetMemberOwningModule(indirect_field, record_decl);

          indirect_field->setImplicit();

          indirect_field->setAccess(TypeSystemClang::UnifyAccessSpecifiers(
              field_pos->getAccess(), nested_indirect_field_decl->getAccess()));

          indirect_fields.push_back(indirect_field);
        }
      }
    }
  }

  // Check the last field to see if it has an incomplete array type as its last
  // member and if it does, the tell the record decl about it
  if (last_field_pos != field_end_pos) {
    if (last_field_pos->getType()->isIncompleteArrayType())
      record_decl->hasFlexibleArrayMember();
  }

  for (IndirectFieldVector::iterator ifi = indirect_fields.begin(),
                                     ife = indirect_fields.end();
       ifi < ife; ++ifi) {
    record_decl->addDecl(*ifi);
  }
}

void TypeSystemClang::SetIsPacked(const CompilerType &type) {
  if (type) {
    auto ts = type.GetTypeSystem();
    auto ast = ts.dyn_cast_or_null<TypeSystemClang>();
    if (ast) {
      clang::RecordDecl *record_decl = GetAsRecordDecl(type);

      if (!record_decl)
        return;

      record_decl->addAttr(
          clang::PackedAttr::CreateImplicit(ast->getASTContext()));
    }
  }
}

clang::VarDecl *TypeSystemClang::AddVariableToRecordType(
    const CompilerType &type, llvm::StringRef name,
    const CompilerType &var_type, AccessType access) {
  if (!type.IsValid() || !var_type.IsValid())
    return nullptr;

  auto ts = type.GetTypeSystem();
  auto ast = ts.dyn_cast_or_null<TypeSystemClang>();
  if (!ast)
    return nullptr;

  clang::RecordDecl *record_decl = ast->GetAsRecordDecl(type);
  if (!record_decl)
    return nullptr;

  clang::VarDecl *var_decl = nullptr;
  clang::IdentifierInfo *ident = nullptr;
  if (!name.empty())
    ident = &ast->getASTContext().Idents.get(name);

  var_decl =
      clang::VarDecl::CreateDeserialized(ast->getASTContext(), GlobalDeclID());
  var_decl->setDeclContext(record_decl);
  var_decl->setDeclName(ident);
  var_decl->setType(ClangUtil::GetQualType(var_type));
  var_decl->setStorageClass(clang::SC_Static);
  SetMemberOwningModule(var_decl, record_decl);
  if (!var_decl)
    return nullptr;

  var_decl->setAccess(
      TypeSystemClang::ConvertAccessTypeToAccessSpecifier(access));
  record_decl->addDecl(var_decl);

  VerifyDecl(var_decl);

  return var_decl;
}

void TypeSystemClang::SetIntegerInitializerForVariable(
    VarDecl *var, const llvm::APInt &init_value) {
  assert(!var->hasInit() && "variable already initialized");

  clang::ASTContext &ast = var->getASTContext();
  QualType qt = var->getType();
  assert(qt->isIntegralOrEnumerationType() &&
         "only integer or enum types supported");
  // If the variable is an enum type, take the underlying integer type as
  // the type of the integer literal.
  if (const EnumType *enum_type = qt->getAs<EnumType>()) {
    const EnumDecl *enum_decl = enum_type->getDecl();
    qt = enum_decl->getIntegerType();
  }
  // Bools are handled separately because the clang AST printer handles bools
  // separately from other integral types.
  if (qt->isSpecificBuiltinType(BuiltinType::Bool)) {
    var->setInit(CXXBoolLiteralExpr::Create(
        ast, !init_value.isZero(), qt.getUnqualifiedType(), SourceLocation()));
  } else {
    var->setInit(IntegerLiteral::Create(
        ast, init_value, qt.getUnqualifiedType(), SourceLocation()));
  }
}

void TypeSystemClang::SetFloatingInitializerForVariable(
    clang::VarDecl *var, const llvm::APFloat &init_value) {
  assert(!var->hasInit() && "variable already initialized");

  clang::ASTContext &ast = var->getASTContext();
  QualType qt = var->getType();
  assert(qt->isFloatingType() && "only floating point types supported");
  var->setInit(FloatingLiteral::Create(
      ast, init_value, true, qt.getUnqualifiedType(), SourceLocation()));
}

clang::CXXMethodDecl *TypeSystemClang::AddMethodToCXXRecordType(
    lldb::opaque_compiler_type_t type, llvm::StringRef name,
    const char *mangled_name, const CompilerType &method_clang_type,
    lldb::AccessType access, bool is_virtual, bool is_static, bool is_inline,
    bool is_explicit, bool is_attr_used, bool is_artificial) {
  if (!type || !method_clang_type.IsValid() || name.empty())
    return nullptr;

  clang::QualType record_qual_type(GetCanonicalQualType(type));

  clang::CXXRecordDecl *cxx_record_decl =
      record_qual_type->getAsCXXRecordDecl();

  if (cxx_record_decl == nullptr)
    return nullptr;

  clang::QualType method_qual_type(ClangUtil::GetQualType(method_clang_type));

  clang::CXXMethodDecl *cxx_method_decl = nullptr;

  clang::DeclarationName decl_name(&getASTContext().Idents.get(name));

  const clang::FunctionType *function_type =
      llvm::dyn_cast<clang::FunctionType>(method_qual_type.getTypePtr());

  if (function_type == nullptr)
    return nullptr;

  const clang::FunctionProtoType *method_function_prototype(
      llvm::dyn_cast<clang::FunctionProtoType>(function_type));

  if (!method_function_prototype)
    return nullptr;

  unsigned int num_params = method_function_prototype->getNumParams();

  clang::CXXDestructorDecl *cxx_dtor_decl(nullptr);
  clang::CXXConstructorDecl *cxx_ctor_decl(nullptr);

  if (is_artificial)
    return nullptr; // skip everything artificial

  const clang::ExplicitSpecifier explicit_spec(
      nullptr /*expr*/, is_explicit ? clang::ExplicitSpecKind::ResolvedTrue
                                    : clang::ExplicitSpecKind::ResolvedFalse);

  if (name.starts_with("~")) {
    cxx_dtor_decl = clang::CXXDestructorDecl::CreateDeserialized(
        getASTContext(), GlobalDeclID());
    cxx_dtor_decl->setDeclContext(cxx_record_decl);
    cxx_dtor_decl->setDeclName(
        getASTContext().DeclarationNames.getCXXDestructorName(
            getASTContext().getCanonicalType(record_qual_type)));
    cxx_dtor_decl->setType(method_qual_type);
    cxx_dtor_decl->setImplicit(is_artificial);
    cxx_dtor_decl->setInlineSpecified(is_inline);
    cxx_dtor_decl->setConstexprKind(ConstexprSpecKind::Unspecified);
    cxx_method_decl = cxx_dtor_decl;
  } else if (decl_name == cxx_record_decl->getDeclName()) {
    cxx_ctor_decl = clang::CXXConstructorDecl::CreateDeserialized(
        getASTContext(), GlobalDeclID(), 0);
    cxx_ctor_decl->setDeclContext(cxx_record_decl);
    cxx_ctor_decl->setDeclName(
        getASTContext().DeclarationNames.getCXXConstructorName(
            getASTContext().getCanonicalType(record_qual_type)));
    cxx_ctor_decl->setType(method_qual_type);
    cxx_ctor_decl->setImplicit(is_artificial);
    cxx_ctor_decl->setInlineSpecified(is_inline);
    cxx_ctor_decl->setConstexprKind(ConstexprSpecKind::Unspecified);
    cxx_ctor_decl->setNumCtorInitializers(0);
    cxx_ctor_decl->setExplicitSpecifier(explicit_spec);
    cxx_method_decl = cxx_ctor_decl;
  } else {
    clang::StorageClass SC = is_static ? clang::SC_Static : clang::SC_None;
    clang::OverloadedOperatorKind op_kind = clang::NUM_OVERLOADED_OPERATORS;

    if (IsOperator(name, op_kind)) {
      if (op_kind != clang::NUM_OVERLOADED_OPERATORS) {
        // Check the number of operator parameters. Sometimes we have seen bad
        // DWARF that doesn't correctly describe operators and if we try to
        // create a method and add it to the class, clang will assert and
        // crash, so we need to make sure things are acceptable.
        const bool is_method = true;
        if (!TypeSystemClang::CheckOverloadedOperatorKindParameterCount(
                is_method, op_kind, num_params))
          return nullptr;
        cxx_method_decl = clang::CXXMethodDecl::CreateDeserialized(
            getASTContext(), GlobalDeclID());
        cxx_method_decl->setDeclContext(cxx_record_decl);
        cxx_method_decl->setDeclName(
            getASTContext().DeclarationNames.getCXXOperatorName(op_kind));
        cxx_method_decl->setType(method_qual_type);
        cxx_method_decl->setStorageClass(SC);
        cxx_method_decl->setInlineSpecified(is_inline);
        cxx_method_decl->setConstexprKind(ConstexprSpecKind::Unspecified);
      } else if (num_params == 0) {
        // Conversion operators don't take params...
        auto *cxx_conversion_decl =
            clang::CXXConversionDecl::CreateDeserialized(getASTContext(),
                                                         GlobalDeclID());
        cxx_conversion_decl->setDeclContext(cxx_record_decl);
        cxx_conversion_decl->setDeclName(
            getASTContext().DeclarationNames.getCXXConversionFunctionName(
                getASTContext().getCanonicalType(
                    function_type->getReturnType())));
        cxx_conversion_decl->setType(method_qual_type);
        cxx_conversion_decl->setInlineSpecified(is_inline);
        cxx_conversion_decl->setExplicitSpecifier(explicit_spec);
        cxx_conversion_decl->setConstexprKind(ConstexprSpecKind::Unspecified);
        cxx_method_decl = cxx_conversion_decl;
      }
    }

    if (cxx_method_decl == nullptr) {
      cxx_method_decl = clang::CXXMethodDecl::CreateDeserialized(
          getASTContext(), GlobalDeclID());
      cxx_method_decl->setDeclContext(cxx_record_decl);
      cxx_method_decl->setDeclName(decl_name);
      cxx_method_decl->setType(method_qual_type);
      cxx_method_decl->setInlineSpecified(is_inline);
      cxx_method_decl->setStorageClass(SC);
      cxx_method_decl->setConstexprKind(ConstexprSpecKind::Unspecified);
    }
  }
  SetMemberOwningModule(cxx_method_decl, cxx_record_decl);

  clang::AccessSpecifier access_specifier =
      TypeSystemClang::ConvertAccessTypeToAccessSpecifier(access);

  cxx_method_decl->setAccess(access_specifier);
  cxx_method_decl->setVirtualAsWritten(is_virtual);

  if (is_attr_used)
    cxx_method_decl->addAttr(clang::UsedAttr::CreateImplicit(getASTContext()));

  if (mangled_name != nullptr) {
    cxx_method_decl->addAttr(clang::AsmLabelAttr::CreateImplicit(
        getASTContext(), mangled_name, /*literal=*/false));
  }

  // Populate the method decl with parameter decls

  llvm::SmallVector<clang::ParmVarDecl *, 12> params;

  for (unsigned param_index = 0; param_index < num_params; ++param_index) {
    params.push_back(clang::ParmVarDecl::Create(
        getASTContext(), cxx_method_decl, clang::SourceLocation(),
        clang::SourceLocation(),
        nullptr, // anonymous
        method_function_prototype->getParamType(param_index), nullptr,
        clang::SC_None, nullptr));
  }

  cxx_method_decl->setParams(llvm::ArrayRef<clang::ParmVarDecl *>(params));

  AddAccessSpecifierDecl(cxx_record_decl, getASTContext(),
                         GetCXXRecordDeclAccess(cxx_record_decl),
                         access_specifier);
  SetCXXRecordDeclAccess(cxx_record_decl, access_specifier);

  cxx_record_decl->addDecl(cxx_method_decl);

  // Sometimes the debug info will mention a constructor (default/copy/move),
  // destructor, or assignment operator (copy/move) but there won't be any
  // version of this in the code. So we check if the function was artificially
  // generated and if it is trivial and this lets the compiler/backend know
  // that it can inline the IR for these when it needs to and we can avoid a
  // "missing function" error when running expressions.

  if (is_artificial) {
    if (cxx_ctor_decl && ((cxx_ctor_decl->isDefaultConstructor() &&
                           cxx_record_decl->hasTrivialDefaultConstructor()) ||
                          (cxx_ctor_decl->isCopyConstructor() &&
                           cxx_record_decl->hasTrivialCopyConstructor()) ||
                          (cxx_ctor_decl->isMoveConstructor() &&
                           cxx_record_decl->hasTrivialMoveConstructor()))) {
      cxx_ctor_decl->setDefaulted();
      cxx_ctor_decl->setTrivial(true);
    } else if (cxx_dtor_decl) {
      if (cxx_record_decl->hasTrivialDestructor()) {
        cxx_dtor_decl->setDefaulted();
        cxx_dtor_decl->setTrivial(true);
      }
    } else if ((cxx_method_decl->isCopyAssignmentOperator() &&
                cxx_record_decl->hasTrivialCopyAssignment()) ||
               (cxx_method_decl->isMoveAssignmentOperator() &&
                cxx_record_decl->hasTrivialMoveAssignment())) {
      cxx_method_decl->setDefaulted();
      cxx_method_decl->setTrivial(true);
    }
  }

  VerifyDecl(cxx_method_decl);

  return cxx_method_decl;
}

void TypeSystemClang::AddMethodOverridesForCXXRecordType(
    lldb::opaque_compiler_type_t type) {
  if (auto *record = GetAsCXXRecordDecl(type))
    for (auto *method : record->methods())
      addOverridesForMethod(method);
}

#pragma mark C++ Base Classes

std::unique_ptr<clang::CXXBaseSpecifier>
TypeSystemClang::CreateBaseClassSpecifier(lldb::opaque_compiler_type_t type,
                                          AccessType access, bool is_virtual,
                                          bool base_of_class) {
  if (!type)
    return nullptr;

  return std::make_unique<clang::CXXBaseSpecifier>(
      clang::SourceRange(), is_virtual, base_of_class,
      TypeSystemClang::ConvertAccessTypeToAccessSpecifier(access),
      getASTContext().getTrivialTypeSourceInfo(GetQualType(type)),
      clang::SourceLocation());
}

bool TypeSystemClang::TransferBaseClasses(
    lldb::opaque_compiler_type_t type,
    std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> bases) {
  if (!type)
    return false;
  clang::CXXRecordDecl *cxx_record_decl = GetAsCXXRecordDecl(type);
  if (!cxx_record_decl)
    return false;
  std::vector<clang::CXXBaseSpecifier *> raw_bases;
  raw_bases.reserve(bases.size());

  // Clang will make a copy of them, so it's ok that we pass pointers that we're
  // about to destroy.
  for (auto &b : bases)
    raw_bases.push_back(b.get());
  cxx_record_decl->setBases(raw_bases.data(), raw_bases.size());
  return true;
}

bool TypeSystemClang::SetObjCSuperClass(
    const CompilerType &type, const CompilerType &superclass_clang_type) {
  auto ts = type.GetTypeSystem();
  auto ast = ts.dyn_cast_or_null<TypeSystemClang>();
  if (!ast)
    return false;
  clang::ASTContext &clang_ast = ast->getASTContext();

  if (type && superclass_clang_type.IsValid() &&
      superclass_clang_type.GetTypeSystem() == type.GetTypeSystem()) {
    clang::ObjCInterfaceDecl *class_interface_decl =
        GetAsObjCInterfaceDecl(type);
    clang::ObjCInterfaceDecl *super_interface_decl =
        GetAsObjCInterfaceDecl(superclass_clang_type);
    if (class_interface_decl && super_interface_decl) {
      class_interface_decl->setSuperClass(clang_ast.getTrivialTypeSourceInfo(
          clang_ast.getObjCInterfaceType(super_interface_decl)));
      return true;
    }
  }
  return false;
}

bool TypeSystemClang::AddObjCClassProperty(
    const CompilerType &type, const char *property_name,
    const CompilerType &property_clang_type, clang::ObjCIvarDecl *ivar_decl,
    const char *property_setter_name, const char *property_getter_name,
    uint32_t property_attributes, ClangASTMetadata *metadata) {
  if (!type || !property_clang_type.IsValid() || property_name == nullptr ||
      property_name[0] == '\0')
    return false;
  auto ts = type.GetTypeSystem();
  auto ast = ts.dyn_cast_or_null<TypeSystemClang>();
  if (!ast)
    return false;
  clang::ASTContext &clang_ast = ast->getASTContext();

  clang::ObjCInterfaceDecl *class_interface_decl = GetAsObjCInterfaceDecl(type);
  if (!class_interface_decl)
    return false;

  CompilerType property_clang_type_to_access;

  if (property_clang_type.IsValid())
    property_clang_type_to_access = property_clang_type;
  else if (ivar_decl)
    property_clang_type_to_access = ast->GetType(ivar_decl->getType());

  if (!class_interface_decl || !property_clang_type_to_access.IsValid())
    return false;

  clang::TypeSourceInfo *prop_type_source;
  if (ivar_decl)
    prop_type_source = clang_ast.getTrivialTypeSourceInfo(ivar_decl->getType());
  else
    prop_type_source = clang_ast.getTrivialTypeSourceInfo(
        ClangUtil::GetQualType(property_clang_type));

  clang::ObjCPropertyDecl *property_decl =
      clang::ObjCPropertyDecl::CreateDeserialized(clang_ast, GlobalDeclID());
  property_decl->setDeclContext(class_interface_decl);
  property_decl->setDeclName(&clang_ast.Idents.get(property_name));
  property_decl->setType(ivar_decl
                             ? ivar_decl->getType()
                             : ClangUtil::GetQualType(property_clang_type),
                         prop_type_source);
  SetMemberOwningModule(property_decl, class_interface_decl);

  if (!property_decl)
    return false;

  if (metadata)
    ast->SetMetadata(property_decl, *metadata);

  class_interface_decl->addDecl(property_decl);

  clang::Selector setter_sel, getter_sel;

  if (property_setter_name) {
    std::string property_setter_no_colon(property_setter_name,
                                         strlen(property_setter_name) - 1);
    const clang::IdentifierInfo *setter_ident =
        &clang_ast.Idents.get(property_setter_no_colon);
    setter_sel = clang_ast.Selectors.getSelector(1, &setter_ident);
  } else if (!(property_attributes & DW_APPLE_PROPERTY_readonly)) {
    std::string setter_sel_string("set");
    setter_sel_string.push_back(::toupper(property_name[0]));
    setter_sel_string.append(&property_name[1]);
    const clang::IdentifierInfo *setter_ident =
        &clang_ast.Idents.get(setter_sel_string);
    setter_sel = clang_ast.Selectors.getSelector(1, &setter_ident);
  }
  property_decl->setSetterName(setter_sel);
  property_decl->setPropertyAttributes(ObjCPropertyAttribute::kind_setter);

  if (property_getter_name != nullptr) {
    const clang::IdentifierInfo *getter_ident =
        &clang_ast.Idents.get(property_getter_name);
    getter_sel = clang_ast.Selectors.getSelector(0, &getter_ident);
  } else {
    const clang::IdentifierInfo *getter_ident =
        &clang_ast.Idents.get(property_name);
    getter_sel = clang_ast.Selectors.getSelector(0, &getter_ident);
  }
  property_decl->setGetterName(getter_sel);
  property_decl->setPropertyAttributes(ObjCPropertyAttribute::kind_getter);

  if (ivar_decl)
    property_decl->setPropertyIvarDecl(ivar_decl);

  if (property_attributes & DW_APPLE_PROPERTY_readonly)
    property_decl->setPropertyAttributes(ObjCPropertyAttribute::kind_readonly);
  if (property_attributes & DW_APPLE_PROPERTY_readwrite)
    property_decl->setPropertyAttributes(ObjCPropertyAttribute::kind_readwrite);
  if (property_attributes & DW_APPLE_PROPERTY_assign)
    property_decl->setPropertyAttributes(ObjCPropertyAttribute::kind_assign);
  if (property_attributes & DW_APPLE_PROPERTY_retain)
    property_decl->setPropertyAttributes(ObjCPropertyAttribute::kind_retain);
  if (property_attributes & DW_APPLE_PROPERTY_copy)
    property_decl->setPropertyAttributes(ObjCPropertyAttribute::kind_copy);
  if (property_attributes & DW_APPLE_PROPERTY_nonatomic)
    property_decl->setPropertyAttributes(ObjCPropertyAttribute::kind_nonatomic);
  if (property_attributes & ObjCPropertyAttribute::kind_nullability)
    property_decl->setPropertyAttributes(
        ObjCPropertyAttribute::kind_nullability);
  if (property_attributes & ObjCPropertyAttribute::kind_null_resettable)
    property_decl->setPropertyAttributes(
        ObjCPropertyAttribute::kind_null_resettable);
  if (property_attributes & ObjCPropertyAttribute::kind_class)
    property_decl->setPropertyAttributes(ObjCPropertyAttribute::kind_class);

  const bool isInstance =
      (property_attributes & ObjCPropertyAttribute::kind_class) == 0;

  clang::ObjCMethodDecl *getter = nullptr;
  if (!getter_sel.isNull())
    getter = isInstance ? class_interface_decl->lookupInstanceMethod(getter_sel)
                        : class_interface_decl->lookupClassMethod(getter_sel);
  if (!getter_sel.isNull() && !getter) {
    const bool isVariadic = false;
    const bool isPropertyAccessor = true;
    const bool isSynthesizedAccessorStub = false;
    const bool isImplicitlyDeclared = true;
    const bool isDefined = false;
    const clang::ObjCImplementationControl impControl =
        clang::ObjCImplementationControl::None;
    const bool HasRelatedResultType = false;

    getter =
        clang::ObjCMethodDecl::CreateDeserialized(clang_ast, GlobalDeclID());
    getter->setDeclName(getter_sel);
    getter->setReturnType(ClangUtil::GetQualType(property_clang_type_to_access));
    getter->setDeclContext(class_interface_decl);
    getter->setInstanceMethod(isInstance);
    getter->setVariadic(isVariadic);
    getter->setPropertyAccessor(isPropertyAccessor);
    getter->setSynthesizedAccessorStub(isSynthesizedAccessorStub);
    getter->setImplicit(isImplicitlyDeclared);
    getter->setDefined(isDefined);
    getter->setDeclImplementation(impControl);
    getter->setRelatedResultType(HasRelatedResultType);
    SetMemberOwningModule(getter, class_interface_decl);

    if (getter) {
      if (metadata)
        ast->SetMetadata(getter, *metadata);

      getter->setMethodParams(clang_ast, llvm::ArrayRef<clang::ParmVarDecl *>(),
                              llvm::ArrayRef<clang::SourceLocation>());
      class_interface_decl->addDecl(getter);
    }
  }
  if (getter) {
    getter->setPropertyAccessor(true);
    property_decl->setGetterMethodDecl(getter);
  }

  clang::ObjCMethodDecl *setter = nullptr;
    setter = isInstance ? class_interface_decl->lookupInstanceMethod(setter_sel)
                        : class_interface_decl->lookupClassMethod(setter_sel);
  if (!setter_sel.isNull() && !setter) {
    clang::QualType result_type = clang_ast.VoidTy;
    const bool isVariadic = false;
    const bool isPropertyAccessor = true;
    const bool isSynthesizedAccessorStub = false;
    const bool isImplicitlyDeclared = true;
    const bool isDefined = false;
    const clang::ObjCImplementationControl impControl =
        clang::ObjCImplementationControl::None;
    const bool HasRelatedResultType = false;

    setter =
        clang::ObjCMethodDecl::CreateDeserialized(clang_ast, GlobalDeclID());
    setter->setDeclName(setter_sel);
    setter->setReturnType(result_type);
    setter->setDeclContext(class_interface_decl);
    setter->setInstanceMethod(isInstance);
    setter->setVariadic(isVariadic);
    setter->setPropertyAccessor(isPropertyAccessor);
    setter->setSynthesizedAccessorStub(isSynthesizedAccessorStub);
    setter->setImplicit(isImplicitlyDeclared);
    setter->setDefined(isDefined);
    setter->setDeclImplementation(impControl);
    setter->setRelatedResultType(HasRelatedResultType);
    SetMemberOwningModule(setter, class_interface_decl);

    if (setter) {
      if (metadata)
        ast->SetMetadata(setter, *metadata);

      llvm::SmallVector<clang::ParmVarDecl *, 1> params;
      params.push_back(clang::ParmVarDecl::Create(
          clang_ast, setter, clang::SourceLocation(), clang::SourceLocation(),
          nullptr, // anonymous
          ClangUtil::GetQualType(property_clang_type_to_access), nullptr,
          clang::SC_Auto, nullptr));

      setter->setMethodParams(clang_ast,
                              llvm::ArrayRef<clang::ParmVarDecl *>(params),
                              llvm::ArrayRef<clang::SourceLocation>());

      class_interface_decl->addDecl(setter);
    }
  }
  if (setter) {
    setter->setPropertyAccessor(true);
    property_decl->setSetterMethodDecl(setter);
  }

  return true;
}

bool TypeSystemClang::IsObjCClassTypeAndHasIVars(const CompilerType &type,
                                                 bool check_superclass) {
  clang::ObjCInterfaceDecl *class_interface_decl = GetAsObjCInterfaceDecl(type);
  if (class_interface_decl)
    return ObjCDeclHasIVars(class_interface_decl, check_superclass);
  return false;
}

clang::ObjCMethodDecl *TypeSystemClang::AddMethodToObjCObjectType(
    const CompilerType &type,
    const char *name, // the full symbol name as seen in the symbol table
                      // (lldb::opaque_compiler_type_t type, "-[NString
                      // stringWithCString:]")
    const CompilerType &method_clang_type, bool is_artificial, bool is_variadic,
    bool is_objc_direct_call) {
  if (!type || !method_clang_type.IsValid())
    return nullptr;

  clang::ObjCInterfaceDecl *class_interface_decl = GetAsObjCInterfaceDecl(type);

  if (class_interface_decl == nullptr)
    return nullptr;
  auto ts = type.GetTypeSystem();
  auto lldb_ast = ts.dyn_cast_or_null<TypeSystemClang>();
  if (lldb_ast == nullptr)
    return nullptr;
  clang::ASTContext &ast = lldb_ast->getASTContext();

  const char *selector_start = ::strchr(name, ' ');
  if (selector_start == nullptr)
    return nullptr;

  selector_start++;
  llvm::SmallVector<const clang::IdentifierInfo *, 12> selector_idents;

  size_t len = 0;
  const char *start;

  unsigned num_selectors_with_args = 0;
  for (start = selector_start; start && *start != '\0' && *start != ']';
       start += len) {
    len = ::strcspn(start, ":]");
    bool has_arg = (start[len] == ':');
    if (has_arg)
      ++num_selectors_with_args;
    selector_idents.push_back(&ast.Idents.get(llvm::StringRef(start, len)));
    if (has_arg)
      len += 1;
  }

  if (selector_idents.size() == 0)
    return nullptr;

  clang::Selector method_selector = ast.Selectors.getSelector(
      num_selectors_with_args ? selector_idents.size() : 0,
      selector_idents.data());

  clang::QualType method_qual_type(ClangUtil::GetQualType(method_clang_type));

  // Populate the method decl with parameter decls
  const clang::Type *method_type(method_qual_type.getTypePtr());

  if (method_type == nullptr)
    return nullptr;

  const clang::FunctionProtoType *method_function_prototype(
      llvm::dyn_cast<clang::FunctionProtoType>(method_type));

  if (!method_function_prototype)
    return nullptr;

  const bool isInstance = (name[0] == '-');
  const bool isVariadic = is_variadic;
  const bool isPropertyAccessor = false;
  const bool isSynthesizedAccessorStub = false;
  /// Force this to true because we don't have source locations.
  const bool isImplicitlyDeclared = true;
  const bool isDefined = false;
  const clang::ObjCImplementationControl impControl =
      clang::ObjCImplementationControl::None;
  const bool HasRelatedResultType = false;

  const unsigned num_args = method_function_prototype->getNumParams();

  if (num_args != num_selectors_with_args)
    return nullptr; // some debug information is corrupt.  We are not going to
                    // deal with it.

  auto *objc_method_decl =
      clang::ObjCMethodDecl::CreateDeserialized(ast, GlobalDeclID());
  objc_method_decl->setDeclName(method_selector);
  objc_method_decl->setReturnType(method_function_prototype->getReturnType());
  objc_method_decl->setDeclContext(
      lldb_ast->GetDeclContextForType(ClangUtil::GetQualType(type)));
  objc_method_decl->setInstanceMethod(isInstance);
  objc_method_decl->setVariadic(isVariadic);
  objc_method_decl->setPropertyAccessor(isPropertyAccessor);
  objc_method_decl->setSynthesizedAccessorStub(isSynthesizedAccessorStub);
  objc_method_decl->setImplicit(isImplicitlyDeclared);
  objc_method_decl->setDefined(isDefined);
  objc_method_decl->setDeclImplementation(impControl);
  objc_method_decl->setRelatedResultType(HasRelatedResultType);
  SetMemberOwningModule(objc_method_decl, class_interface_decl);

  if (objc_method_decl == nullptr)
    return nullptr;

  if (num_args > 0) {
    llvm::SmallVector<clang::ParmVarDecl *, 12> params;

    for (unsigned param_index = 0; param_index < num_args; ++param_index) {
      params.push_back(clang::ParmVarDecl::Create(
          ast, objc_method_decl, clang::SourceLocation(),
          clang::SourceLocation(),
          nullptr, // anonymous
          method_function_prototype->getParamType(param_index), nullptr,
          clang::SC_Auto, nullptr));
    }

    objc_method_decl->setMethodParams(
        ast, llvm::ArrayRef<clang::ParmVarDecl *>(params),
        llvm::ArrayRef<clang::SourceLocation>());
  }

  if (is_objc_direct_call) {
    // Add a the objc_direct attribute to the declaration we generate that
    // we generate a direct method call for this ObjCMethodDecl.
    objc_method_decl->addAttr(
        clang::ObjCDirectAttr::CreateImplicit(ast, SourceLocation()));
    // Usually Sema is creating implicit parameters (e.g., self) when it
    // parses the method. We don't have a parsing Sema when we build our own
    // AST here so we manually need to create these implicit parameters to
    // make the direct call code generation happy.
    objc_method_decl->createImplicitParams(ast, class_interface_decl);
  }

  class_interface_decl->addDecl(objc_method_decl);

  VerifyDecl(objc_method_decl);

  return objc_method_decl;
}

bool TypeSystemClang::SetHasExternalStorage(lldb::opaque_compiler_type_t type,
                                            bool has_extern) {
  if (!type)
    return false;

  clang::QualType qual_type(RemoveWrappingTypes(GetCanonicalQualType(type)));

  const clang::Type::TypeClass type_class = qual_type->getTypeClass();
  switch (type_class) {
  case clang::Type::Record: {
    clang::CXXRecordDecl *cxx_record_decl = qual_type->getAsCXXRecordDecl();
    if (cxx_record_decl) {
      cxx_record_decl->setHasExternalLexicalStorage(has_extern);
      cxx_record_decl->setHasExternalVisibleStorage(has_extern);
      return true;
    }
  } break;

  case clang::Type::Enum: {
    clang::EnumDecl *enum_decl =
        llvm::cast<clang::EnumType>(qual_type)->getDecl();
    if (enum_decl) {
      enum_decl->setHasExternalLexicalStorage(has_extern);
      enum_decl->setHasExternalVisibleStorage(has_extern);
      return true;
    }
  } break;

  case clang::Type::ObjCObject:
  case clang::Type::ObjCInterface: {
    const clang::ObjCObjectType *objc_class_type =
        llvm::dyn_cast<clang::ObjCObjectType>(qual_type.getTypePtr());
    assert(objc_class_type);
    if (objc_class_type) {
      clang::ObjCInterfaceDecl *class_interface_decl =
          objc_class_type->getInterface();

      if (class_interface_decl) {
        class_interface_decl->setHasExternalLexicalStorage(has_extern);
        class_interface_decl->setHasExternalVisibleStorage(has_extern);
        return true;
      }
    }
  } break;

  default:
    break;
  }
  return false;
}

#pragma mark TagDecl

bool TypeSystemClang::StartTagDeclarationDefinition(const CompilerType &type) {
  clang::QualType qual_type(ClangUtil::GetQualType(type));
  if (!qual_type.isNull()) {
    const clang::TagType *tag_type = qual_type->getAs<clang::TagType>();
    if (tag_type) {
      clang::TagDecl *tag_decl = tag_type->getDecl();
      if (tag_decl) {
        tag_decl->startDefinition();
        return true;
      }
    }

    const clang::ObjCObjectType *object_type =
        qual_type->getAs<clang::ObjCObjectType>();
    if (object_type) {
      clang::ObjCInterfaceDecl *interface_decl = object_type->getInterface();
      if (interface_decl) {
        interface_decl->startDefinition();
        return true;
      }
    }
  }
  return false;
}

bool TypeSystemClang::CompleteTagDeclarationDefinition(
    const CompilerType &type) {
  clang::QualType qual_type(ClangUtil::GetQualType(type));
  if (qual_type.isNull())
    return false;

  auto ts = type.GetTypeSystem();
  auto lldb_ast = ts.dyn_cast_or_null<TypeSystemClang>();
  if (lldb_ast == nullptr)
    return false;

  // Make sure we use the same methodology as
  // TypeSystemClang::StartTagDeclarationDefinition() as to how we start/end
  // the definition.
  const clang::TagType *tag_type = qual_type->getAs<clang::TagType>();
  if (tag_type) {
    clang::TagDecl *tag_decl = tag_type->getDecl();

    if (auto *cxx_record_decl = llvm::dyn_cast<CXXRecordDecl>(tag_decl)) {
      // If we have a move constructor declared but no copy constructor we
      // need to explicitly mark it as deleted. Usually Sema would do this for
      // us in Sema::DeclareImplicitCopyConstructor but we don't have a Sema
      // when building an AST from debug information.
      // See also:
      // C++11 [class.copy]p7, p18:
      //  If the class definition declares a move constructor or move assignment
      //  operator, an implicitly declared copy constructor or copy assignment
      //  operator is defined as deleted.
      if (cxx_record_decl->hasUserDeclaredMoveConstructor() ||
          cxx_record_decl->hasUserDeclaredMoveAssignment()) {
        if (cxx_record_decl->needsImplicitCopyConstructor())
          cxx_record_decl->setImplicitCopyConstructorIsDeleted();
        if (cxx_record_decl->needsImplicitCopyAssignment())
          cxx_record_decl->setImplicitCopyAssignmentIsDeleted();
      }

      if (!cxx_record_decl->isCompleteDefinition())
        cxx_record_decl->completeDefinition();
      cxx_record_decl->setHasLoadedFieldsFromExternalStorage(true);
      cxx_record_decl->setHasExternalLexicalStorage(false);
      cxx_record_decl->setHasExternalVisibleStorage(false);
      lldb_ast->SetCXXRecordDeclAccess(cxx_record_decl,
                                       clang::AccessSpecifier::AS_none);
      return true;
    }
  }

  const clang::EnumType *enutype = qual_type->getAs<clang::EnumType>();

  if (!enutype)
    return false;
  clang::EnumDecl *enum_decl = enutype->getDecl();

  if (enum_decl->isCompleteDefinition())
    return true;

  clang::ASTContext &ast = lldb_ast->getASTContext();

  /// TODO This really needs to be fixed.

  QualType integer_type(enum_decl->getIntegerType());
  if (!integer_type.isNull()) {
    unsigned NumPositiveBits = 1;
    unsigned NumNegativeBits = 0;

    clang::QualType promotion_qual_type;
    // If the enum integer type is less than an integer in bit width,
    // then we must promote it to an integer size.
    if (ast.getTypeSize(enum_decl->getIntegerType()) <
        ast.getTypeSize(ast.IntTy)) {
      if (enum_decl->getIntegerType()->isSignedIntegerType())
        promotion_qual_type = ast.IntTy;
      else
        promotion_qual_type = ast.UnsignedIntTy;
    } else
      promotion_qual_type = enum_decl->getIntegerType();

    enum_decl->completeDefinition(enum_decl->getIntegerType(),
                                  promotion_qual_type, NumPositiveBits,
                                  NumNegativeBits);
  }
  return true;
}

clang::EnumConstantDecl *TypeSystemClang::AddEnumerationValueToEnumerationType(
    const CompilerType &enum_type, const Declaration &decl, const char *name,
    const llvm::APSInt &value) {

  if (!enum_type || ConstString(name).IsEmpty())
    return nullptr;

  lldbassert(enum_type.GetTypeSystem().GetSharedPointer().get() ==
             static_cast<TypeSystem *>(this));

  lldb::opaque_compiler_type_t enum_opaque_compiler_type =
      enum_type.GetOpaqueQualType();

  if (!enum_opaque_compiler_type)
    return nullptr;

  clang::QualType enum_qual_type(
      GetCanonicalQualType(enum_opaque_compiler_type));

  const clang::Type *clang_type = enum_qual_type.getTypePtr();

  if (!clang_type)
    return nullptr;

  const clang::EnumType *enutype = llvm::dyn_cast<clang::EnumType>(clang_type);

  if (!enutype)
    return nullptr;

  clang::EnumConstantDecl *enumerator_decl =
      clang::EnumConstantDecl::CreateDeserialized(getASTContext(),
                                                  GlobalDeclID());
  enumerator_decl->setDeclContext(enutype->getDecl());
  if (name && name[0])
    enumerator_decl->setDeclName(&getASTContext().Idents.get(name));
  enumerator_decl->setType(clang::QualType(enutype, 0));
  enumerator_decl->setInitVal(getASTContext(), value);
  SetMemberOwningModule(enumerator_decl, enutype->getDecl());

  if (!enumerator_decl)
    return nullptr;

  enutype->getDecl()->addDecl(enumerator_decl);

  VerifyDecl(enumerator_decl);
  return enumerator_decl;
}

clang::EnumConstantDecl *TypeSystemClang::AddEnumerationValueToEnumerationType(
    const CompilerType &enum_type, const Declaration &decl, const char *name,
    int64_t enum_value, uint32_t enum_value_bit_size) {
  CompilerType underlying_type = GetEnumerationIntegerType(enum_type);
  bool is_signed = false;
  underlying_type.IsIntegerType(is_signed);

  llvm::APSInt value(enum_value_bit_size, is_signed);
  value = enum_value;

  return AddEnumerationValueToEnumerationType(enum_type, decl, name, value);
}

CompilerType TypeSystemClang::GetEnumerationIntegerType(CompilerType type) {
  clang::QualType qt(ClangUtil::GetQualType(type));
  const clang::Type *clang_type = qt.getTypePtrOrNull();
  const auto *enum_type = llvm::dyn_cast_or_null<clang::EnumType>(clang_type);
  if (!enum_type)
    return CompilerType();

  return GetType(enum_type->getDecl()->getIntegerType());
}

CompilerType
TypeSystemClang::CreateMemberPointerType(const CompilerType &type,
                                         const CompilerType &pointee_type) {
  if (type && pointee_type.IsValid() &&
      type.GetTypeSystem() == pointee_type.GetTypeSystem()) {
    auto ts = type.GetTypeSystem();
    auto ast = ts.dyn_cast_or_null<TypeSystemClang>();
    if (!ast)
      return CompilerType();
    return ast->GetType(ast->getASTContext().getMemberPointerType(
        ClangUtil::GetQualType(pointee_type),
        ClangUtil::GetQualType(type).getTypePtr()));
  }
  return CompilerType();
}

// Dumping types
#define DEPTH_INCREMENT 2

#ifndef NDEBUG
LLVM_DUMP_METHOD void
TypeSystemClang::dump(lldb::opaque_compiler_type_t type) const {
  if (!type)
    return;
  clang::QualType qual_type(GetQualType(type));
  qual_type.dump();
}
#endif

void TypeSystemClang::Dump(llvm::raw_ostream &output) {
  GetTranslationUnitDecl()->dump(output);
}

void TypeSystemClang::DumpFromSymbolFile(Stream &s,
                                         llvm::StringRef symbol_name) {
  SymbolFile *symfile = GetSymbolFile();

  if (!symfile)
    return;

  lldb_private::TypeList type_list;
  symfile->GetTypes(nullptr, eTypeClassAny, type_list);
  size_t ntypes = type_list.GetSize();

  for (size_t i = 0; i < ntypes; ++i) {
    TypeSP type = type_list.GetTypeAtIndex(i);

    if (!symbol_name.empty())
      if (symbol_name != type->GetName().GetStringRef())
        continue;

    s << type->GetName().AsCString() << "\n";

    CompilerType full_type = type->GetFullCompilerType();
    if (clang::TagDecl *tag_decl = GetAsTagDecl(full_type)) {
      tag_decl->dump(s.AsRawOstream());
      continue;
    }
    if (clang::TypedefNameDecl *typedef_decl = GetAsTypedefDecl(full_type)) {
      typedef_decl->dump(s.AsRawOstream());
      continue;
    }
    if (auto *objc_obj = llvm::dyn_cast<clang::ObjCObjectType>(
            ClangUtil::GetQualType(full_type).getTypePtr())) {
      if (clang::ObjCInterfaceDecl *interface_decl = objc_obj->getInterface()) {
        interface_decl->dump(s.AsRawOstream());
        continue;
      }
    }
    GetCanonicalQualType(full_type.GetOpaqueQualType())
        .dump(s.AsRawOstream(), getASTContext());
  }
}

static bool DumpEnumValue(const clang::QualType &qual_type, Stream &s,
                          const DataExtractor &data, lldb::offset_t byte_offset,
                          size_t byte_size, uint32_t bitfield_bit_offset,
                          uint32_t bitfield_bit_size) {
  const clang::EnumType *enutype =
      llvm::cast<clang::EnumType>(qual_type.getTypePtr());
  const clang::EnumDecl *enum_decl = enutype->getDecl();
  assert(enum_decl);
  lldb::offset_t offset = byte_offset;
  bool qual_type_is_signed = qual_type->isSignedIntegerOrEnumerationType();
  const uint64_t enum_svalue =
      qual_type_is_signed
          ? data.GetMaxS64Bitfield(&offset, byte_size, bitfield_bit_size,
                                   bitfield_bit_offset)
          : data.GetMaxU64Bitfield(&offset, byte_size, bitfield_bit_size,
                                   bitfield_bit_offset);
  bool can_be_bitfield = true;
  uint64_t covered_bits = 0;
  int num_enumerators = 0;

  // Try to find an exact match for the value.
  // At the same time, we're applying a heuristic to determine whether we want
  // to print this enum as a bitfield. We're likely dealing with a bitfield if
  // every enumerator is either a one bit value or a superset of the previous
  // enumerators. Also 0 doesn't make sense when the enumerators are used as
  // flags.
  clang::EnumDecl::enumerator_range enumerators = enum_decl->enumerators();
  if (enumerators.empty())
    can_be_bitfield = false;
  else {
    for (auto *enumerator : enumerators) {
      llvm::APSInt init_val = enumerator->getInitVal();
      uint64_t val = qual_type_is_signed ? init_val.getSExtValue()
                                         : init_val.getZExtValue();
      if (qual_type_is_signed)
        val = llvm::SignExtend64(val, 8 * byte_size);
      if (llvm::popcount(val) != 1 && (val & ~covered_bits) != 0)
        can_be_bitfield = false;
      covered_bits |= val;
      ++num_enumerators;
      if (val == enum_svalue) {
        // Found an exact match, that's all we need to do.
        s.PutCString(enumerator->getNameAsString());
        return true;
      }
    }
  }

  // Unsigned values make more sense for flags.
  offset = byte_offset;
  const uint64_t enum_uvalue = data.GetMaxU64Bitfield(
      &offset, byte_size, bitfield_bit_size, bitfield_bit_offset);

  // No exact match, but we don't think this is a bitfield. Print the value as
  // decimal.
  if (!can_be_bitfield) {
    if (qual_type_is_signed)
      s.Printf("%" PRIi64, enum_svalue);
    else
      s.Printf("%" PRIu64, enum_uvalue);
    return true;
  }

  if (!enum_uvalue) {
    // This is a bitfield enum, but the value is 0 so we know it won't match
    // with any of the enumerators.
    s.Printf("0x%" PRIx64, enum_uvalue);
    return true;
  }

  uint64_t remaining_value = enum_uvalue;
  std::vector<std::pair<uint64_t, llvm::StringRef>> values;
  values.reserve(num_enumerators);
  for (auto *enumerator : enum_decl->enumerators())
    if (auto val = enumerator->getInitVal().getZExtValue())
      values.emplace_back(val, enumerator->getName());

  // Sort in reverse order of the number of the population count,  so that in
  // `enum {A, B, ALL = A|B }` we visit ALL first. Use a stable sort so that
  // A | C where A is declared before C is displayed in this order.
  std::stable_sort(values.begin(), values.end(),
                   [](const auto &a, const auto &b) {
                     return llvm::popcount(a.first) > llvm::popcount(b.first);
                   });

  for (const auto &val : values) {
    if ((remaining_value & val.first) != val.first)
      continue;
    remaining_value &= ~val.first;
    s.PutCString(val.second);
    if (remaining_value)
      s.PutCString(" | ");
  }

  // If there is a remainder that is not covered by the value, print it as
  // hex.
  if (remaining_value)
    s.Printf("0x%" PRIx64, remaining_value);

  return true;
}

bool TypeSystemClang::DumpTypeValue(
    lldb::opaque_compiler_type_t type, Stream &s, lldb::Format format,
    const lldb_private::DataExtractor &data, lldb::offset_t byte_offset,
    size_t byte_size, uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
    ExecutionContextScope *exe_scope) {
  if (!type)
    return false;
  if (IsAggregateType(type)) {
    return false;
  } else {
    clang::QualType qual_type(GetQualType(type));

    const clang::Type::TypeClass type_class = qual_type->getTypeClass();

    if (type_class == clang::Type::Elaborated) {
      qual_type = llvm::cast<clang::ElaboratedType>(qual_type)->getNamedType();
      return DumpTypeValue(qual_type.getAsOpaquePtr(), s, format, data, byte_offset, byte_size,
                           bitfield_bit_size, bitfield_bit_offset, exe_scope);
    }

    switch (type_class) {
    case clang::Type::Typedef: {
      clang::QualType typedef_qual_type =
          llvm::cast<clang::TypedefType>(qual_type)
              ->getDecl()
              ->getUnderlyingType();
      CompilerType typedef_clang_type = GetType(typedef_qual_type);
      if (format == eFormatDefault)
        format = typedef_clang_type.GetFormat();
      clang::TypeInfo typedef_type_info =
          getASTContext().getTypeInfo(typedef_qual_type);
      uint64_t typedef_byte_size = typedef_type_info.Width / 8;

      return typedef_clang_type.DumpTypeValue(
          &s,
          format,            // The format with which to display the element
          data,              // Data buffer containing all bytes for this type
          byte_offset,       // Offset into "data" where to grab value from
          typedef_byte_size, // Size of this type in bytes
          bitfield_bit_size, // Size in bits of a bitfield value, if zero don't
                             // treat as a bitfield
          bitfield_bit_offset, // Offset in bits of a bitfield value if
                               // bitfield_bit_size != 0
          exe_scope);
    } break;

    case clang::Type::Enum:
      // If our format is enum or default, show the enumeration value as its
      // enumeration string value, else just display it as requested.
      if ((format == eFormatEnum || format == eFormatDefault) &&
          GetCompleteType(type))
        return DumpEnumValue(qual_type, s, data, byte_offset, byte_size,
                             bitfield_bit_offset, bitfield_bit_size);
      // format was not enum, just fall through and dump the value as
      // requested....
      [[fallthrough]];

    default:
      // We are down to a scalar type that we just need to display.
      {
        uint32_t item_count = 1;
        // A few formats, we might need to modify our size and count for
        // depending
        // on how we are trying to display the value...
        switch (format) {
        default:
        case eFormatBoolean:
        case eFormatBinary:
        case eFormatComplex:
        case eFormatCString: // NULL terminated C strings
        case eFormatDecimal:
        case eFormatEnum:
        case eFormatHex:
        case eFormatHexUppercase:
        case eFormatFloat:
        case eFormatOctal:
        case eFormatOSType:
        case eFormatUnsigned:
        case eFormatPointer:
        case eFormatVectorOfChar:
        case eFormatVectorOfSInt8:
        case eFormatVectorOfUInt8:
        case eFormatVectorOfSInt16:
        case eFormatVectorOfUInt16:
        case eFormatVectorOfSInt32:
        case eFormatVectorOfUInt32:
        case eFormatVectorOfSInt64:
        case eFormatVectorOfUInt64:
        case eFormatVectorOfFloat32:
        case eFormatVectorOfFloat64:
        case eFormatVectorOfUInt128:
          break;

        case eFormatChar:
        case eFormatCharPrintable:
        case eFormatCharArray:
        case eFormatBytes:
        case eFormatUnicode8:
        case eFormatBytesWithASCII:
          item_count = byte_size;
          byte_size = 1;
          break;

        case eFormatUnicode16:
          item_count = byte_size / 2;
          byte_size = 2;
          break;

        case eFormatUnicode32:
          item_count = byte_size / 4;
          byte_size = 4;
          break;
        }
        return DumpDataExtractor(data, &s, byte_offset, format, byte_size,
                                 item_count, UINT32_MAX, LLDB_INVALID_ADDRESS,
                                 bitfield_bit_size, bitfield_bit_offset,
                                 exe_scope);
      }
      break;
    }
  }
  return false;
}

void TypeSystemClang::DumpTypeDescription(lldb::opaque_compiler_type_t type,
                                          lldb::DescriptionLevel level) {
  StreamFile s(stdout, false);
  DumpTypeDescription(type, s, level);

  CompilerType ct(weak_from_this(), type);
  const clang::Type *clang_type = ClangUtil::GetQualType(ct).getTypePtr();
  ClangASTMetadata *metadata = GetMetadata(clang_type);
  if (metadata) {
    metadata->Dump(&s);
  }
}

void TypeSystemClang::DumpTypeDescription(lldb::opaque_compiler_type_t type,
                                          Stream &s,
                                          lldb::DescriptionLevel level) {
  if (type) {
    clang::QualType qual_type =
        RemoveWrappingTypes(GetQualType(type), {clang::Type::Typedef});

    llvm::SmallVector<char, 1024> buf;
    llvm::raw_svector_ostream llvm_ostrm(buf);

    const clang::Type::TypeClass type_class = qual_type->getTypeClass();
    switch (type_class) {
    case clang::Type::ObjCObject:
    case clang::Type::ObjCInterface: {
      GetCompleteType(type);

      auto *objc_class_type =
          llvm::dyn_cast<clang::ObjCObjectType>(qual_type.getTypePtr());
      assert(objc_class_type);
      if (!objc_class_type)
        break;
      clang::ObjCInterfaceDecl *class_interface_decl =
            objc_class_type->getInterface();
      if (!class_interface_decl)
        break;
      if (level == eDescriptionLevelVerbose)
        class_interface_decl->dump(llvm_ostrm);
      else
        class_interface_decl->print(llvm_ostrm,
                                    getASTContext().getPrintingPolicy(),
                                    s.GetIndentLevel());
    } break;

    case clang::Type::Typedef: {
      auto *typedef_type = qual_type->getAs<clang::TypedefType>();
      if (!typedef_type)
        break;
      const clang::TypedefNameDecl *typedef_decl = typedef_type->getDecl();
      if (level == eDescriptionLevelVerbose)
        typedef_decl->dump(llvm_ostrm);
      else {
        std::string clang_typedef_name(GetTypeNameForDecl(typedef_decl));
        if (!clang_typedef_name.empty()) {
          s.PutCString("typedef ");
          s.PutCString(clang_typedef_name);
        }
      }
    } break;

    case clang::Type::Record: {
      GetCompleteType(type);

      auto *record_type = llvm::cast<clang::RecordType>(qual_type.getTypePtr());
      const clang::RecordDecl *record_decl = record_type->getDecl();
      if (level == eDescriptionLevelVerbose)
        record_decl->dump(llvm_ostrm);
      else {
        record_decl->print(llvm_ostrm, getASTContext().getPrintingPolicy(),
                           s.GetIndentLevel());
      }
    } break;

    default: {
      if (auto *tag_type =
              llvm::dyn_cast<clang::TagType>(qual_type.getTypePtr())) {
        if (clang::TagDecl *tag_decl = tag_type->getDecl()) {
          if (level == eDescriptionLevelVerbose)
            tag_decl->dump(llvm_ostrm);
          else
            tag_decl->print(llvm_ostrm, 0);
        }
      } else {
        if (level == eDescriptionLevelVerbose)
          qual_type->dump(llvm_ostrm, getASTContext());
        else {
          std::string clang_type_name(qual_type.getAsString());
          if (!clang_type_name.empty())
            s.PutCString(clang_type_name);
        }
      }
    }
    }

    if (buf.size() > 0) {
      s.Write(buf.data(), buf.size());
    }
}
}

void TypeSystemClang::DumpTypeName(const CompilerType &type) {
  if (ClangUtil::IsClangType(type)) {
    clang::QualType qual_type(
        ClangUtil::GetCanonicalQualType(ClangUtil::RemoveFastQualifiers(type)));

    const clang::Type::TypeClass type_class = qual_type->getTypeClass();
    switch (type_class) {
    case clang::Type::Record: {
      const clang::CXXRecordDecl *cxx_record_decl =
          qual_type->getAsCXXRecordDecl();
      if (cxx_record_decl)
        printf("class %s", cxx_record_decl->getName().str().c_str());
    } break;

    case clang::Type::Enum: {
      clang::EnumDecl *enum_decl =
          llvm::cast<clang::EnumType>(qual_type)->getDecl();
      if (enum_decl) {
        printf("enum %s", enum_decl->getName().str().c_str());
      }
    } break;

    case clang::Type::ObjCObject:
    case clang::Type::ObjCInterface: {
      const clang::ObjCObjectType *objc_class_type =
          llvm::dyn_cast<clang::ObjCObjectType>(qual_type);
      if (objc_class_type) {
        clang::ObjCInterfaceDecl *class_interface_decl =
            objc_class_type->getInterface();
        // We currently can't complete objective C types through the newly
        // added ASTContext because it only supports TagDecl objects right
        // now...
        if (class_interface_decl)
          printf("@class %s", class_interface_decl->getName().str().c_str());
      }
    } break;

    case clang::Type::Typedef:
      printf("typedef %s", llvm::cast<clang::TypedefType>(qual_type)
                               ->getDecl()
                               ->getName()
                               .str()
                               .c_str());
      break;

    case clang::Type::Auto:
      printf("auto ");
      return DumpTypeName(CompilerType(type.GetTypeSystem(),
                                       llvm::cast<clang::AutoType>(qual_type)
                                           ->getDeducedType()
                                           .getAsOpaquePtr()));

    case clang::Type::Elaborated:
      printf("elaborated ");
      return DumpTypeName(CompilerType(
          type.GetTypeSystem(), llvm::cast<clang::ElaboratedType>(qual_type)
                                    ->getNamedType()
                                    .getAsOpaquePtr()));

    case clang::Type::Paren:
      printf("paren ");
      return DumpTypeName(CompilerType(
          type.GetTypeSystem(),
          llvm::cast<clang::ParenType>(qual_type)->desugar().getAsOpaquePtr()));

    default:
      printf("TypeSystemClang::DumpTypeName() type_class = %u", type_class);
      break;
    }
  }
}

clang::ClassTemplateDecl *TypeSystemClang::ParseClassTemplateDecl(
    clang::DeclContext *decl_ctx, OptionalClangModuleID owning_module,
    lldb::AccessType access_type, const char *parent_name, int tag_decl_kind,
    const TypeSystemClang::TemplateParameterInfos &template_param_infos) {
  if (template_param_infos.IsValid()) {
    std::string template_basename(parent_name);
    // With -gsimple-template-names we may omit template parameters in the name.
    if (auto i = template_basename.find('<'); i != std::string::npos)
      template_basename.erase(i);

    return CreateClassTemplateDecl(decl_ctx, owning_module, access_type,
                                   template_basename.c_str(), tag_decl_kind,
                                   template_param_infos);
  }
  return nullptr;
}

void TypeSystemClang::CompleteTagDecl(clang::TagDecl *decl) {
  SymbolFile *sym_file = GetSymbolFile();
  if (sym_file) {
    CompilerType clang_type = GetTypeForDecl(decl);
    if (clang_type)
      sym_file->CompleteType(clang_type);
  }
}

void TypeSystemClang::CompleteObjCInterfaceDecl(
    clang::ObjCInterfaceDecl *decl) {
  SymbolFile *sym_file = GetSymbolFile();
  if (sym_file) {
    CompilerType clang_type = GetTypeForDecl(decl);
    if (clang_type)
      sym_file->CompleteType(clang_type);
  }
}

DWARFASTParser *TypeSystemClang::GetDWARFParser() {
  if (!m_dwarf_ast_parser_up)
    m_dwarf_ast_parser_up = std::make_unique<DWARFASTParserClang>(*this);
  return m_dwarf_ast_parser_up.get();
}

PDBASTParser *TypeSystemClang::GetPDBParser() {
  if (!m_pdb_ast_parser_up)
    m_pdb_ast_parser_up = std::make_unique<PDBASTParser>(*this);
  return m_pdb_ast_parser_up.get();
}

npdb::PdbAstBuilder *TypeSystemClang::GetNativePDBParser() {
  if (!m_native_pdb_ast_parser_up)
    m_native_pdb_ast_parser_up = std::make_unique<npdb::PdbAstBuilder>(*this);
  return m_native_pdb_ast_parser_up.get();
}

bool TypeSystemClang::LayoutRecordType(
    const clang::RecordDecl *record_decl, uint64_t &bit_size,
    uint64_t &alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &base_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &vbase_offsets) {
  lldb_private::ClangASTImporter *importer = nullptr;
  if (m_dwarf_ast_parser_up)
    importer = &m_dwarf_ast_parser_up->GetClangASTImporter();
  if (!importer && m_pdb_ast_parser_up)
    importer = &m_pdb_ast_parser_up->GetClangASTImporter();
  if (!importer && m_native_pdb_ast_parser_up)
    importer = &m_native_pdb_ast_parser_up->GetClangASTImporter();
  if (!importer)
    return false;

  return importer->LayoutRecordType(record_decl, bit_size, alignment,
                                    field_offsets, base_offsets, vbase_offsets);
}

// CompilerDecl override functions

ConstString TypeSystemClang::DeclGetName(void *opaque_decl) {
  if (opaque_decl) {
    clang::NamedDecl *nd =
        llvm::dyn_cast<NamedDecl>((clang::Decl *)opaque_decl);
    if (nd != nullptr)
      return ConstString(nd->getDeclName().getAsString());
  }
  return ConstString();
}

ConstString TypeSystemClang::DeclGetMangledName(void *opaque_decl) {
  if (opaque_decl) {
    clang::NamedDecl *nd =
        llvm::dyn_cast<clang::NamedDecl>((clang::Decl *)opaque_decl);
    if (nd != nullptr && !llvm::isa<clang::ObjCMethodDecl>(nd)) {
      clang::MangleContext *mc = getMangleContext();
      if (mc && mc->shouldMangleCXXName(nd)) {
        llvm::SmallVector<char, 1024> buf;
        llvm::raw_svector_ostream llvm_ostrm(buf);
        if (llvm::isa<clang::CXXConstructorDecl>(nd)) {
          mc->mangleName(
              clang::GlobalDecl(llvm::dyn_cast<clang::CXXConstructorDecl>(nd),
                                Ctor_Complete),
              llvm_ostrm);
        } else if (llvm::isa<clang::CXXDestructorDecl>(nd)) {
          mc->mangleName(
              clang::GlobalDecl(llvm::dyn_cast<clang::CXXDestructorDecl>(nd),
                                Dtor_Complete),
              llvm_ostrm);
        } else {
          mc->mangleName(nd, llvm_ostrm);
        }
        if (buf.size() > 0)
          return ConstString(buf.data(), buf.size());
      }
    }
  }
  return ConstString();
}

CompilerDeclContext TypeSystemClang::DeclGetDeclContext(void *opaque_decl) {
  if (opaque_decl)
    return CreateDeclContext(((clang::Decl *)opaque_decl)->getDeclContext());
  return CompilerDeclContext();
}

CompilerType TypeSystemClang::DeclGetFunctionReturnType(void *opaque_decl) {
  if (clang::FunctionDecl *func_decl =
          llvm::dyn_cast<clang::FunctionDecl>((clang::Decl *)opaque_decl))
    return GetType(func_decl->getReturnType());
  if (clang::ObjCMethodDecl *objc_method =
          llvm::dyn_cast<clang::ObjCMethodDecl>((clang::Decl *)opaque_decl))
    return GetType(objc_method->getReturnType());
  else
    return CompilerType();
}

size_t TypeSystemClang::DeclGetFunctionNumArguments(void *opaque_decl) {
  if (clang::FunctionDecl *func_decl =
          llvm::dyn_cast<clang::FunctionDecl>((clang::Decl *)opaque_decl))
    return func_decl->param_size();
  if (clang::ObjCMethodDecl *objc_method =
          llvm::dyn_cast<clang::ObjCMethodDecl>((clang::Decl *)opaque_decl))
    return objc_method->param_size();
  else
    return 0;
}

static CompilerContextKind GetCompilerKind(clang::Decl::Kind clang_kind,
                                           clang::DeclContext const *decl_ctx) {
  switch (clang_kind) {
  case Decl::TranslationUnit:
    return CompilerContextKind::TranslationUnit;
  case Decl::Namespace:
    return CompilerContextKind::Namespace;
  case Decl::Var:
    return CompilerContextKind::Variable;
  case Decl::Enum:
    return CompilerContextKind::Enum;
  case Decl::Typedef:
    return CompilerContextKind::Typedef;
  default:
    // Many other kinds have multiple values
    if (decl_ctx) {
      if (decl_ctx->isFunctionOrMethod())
        return CompilerContextKind::Function;
      if (decl_ctx->isRecord())
        return CompilerContextKind::ClassOrStruct | CompilerContextKind::Union;
    }
    break;
  }
  return CompilerContextKind::Any;
}

static void
InsertCompilerContext(TypeSystemClang *ts, clang::DeclContext *decl_ctx,
                      std::vector<lldb_private::CompilerContext> &context) {
  if (decl_ctx == nullptr)
    return;
  InsertCompilerContext(ts, decl_ctx->getParent(), context);
  clang::Decl::Kind clang_kind = decl_ctx->getDeclKind();
  if (clang_kind == Decl::TranslationUnit)
    return; // Stop at the translation unit.
  const CompilerContextKind compiler_kind =
      GetCompilerKind(clang_kind, decl_ctx);
  ConstString decl_ctx_name = ts->DeclContextGetName(decl_ctx);
  context.push_back({compiler_kind, decl_ctx_name});
}

std::vector<lldb_private::CompilerContext>
TypeSystemClang::DeclGetCompilerContext(void *opaque_decl) {
  std::vector<lldb_private::CompilerContext> context;
  ConstString decl_name = DeclGetName(opaque_decl);
  if (decl_name) {
    clang::Decl *decl = (clang::Decl *)opaque_decl;
    // Add the entire decl context first
    clang::DeclContext *decl_ctx = decl->getDeclContext();
    InsertCompilerContext(this, decl_ctx, context);
    // Now add the decl information
    auto compiler_kind =
        GetCompilerKind(decl->getKind(), dyn_cast<DeclContext>(decl));
    context.push_back({compiler_kind, decl_name});
  }
  return context;
}

CompilerType TypeSystemClang::DeclGetFunctionArgumentType(void *opaque_decl,
                                                          size_t idx) {
  if (clang::FunctionDecl *func_decl =
          llvm::dyn_cast<clang::FunctionDecl>((clang::Decl *)opaque_decl)) {
    if (idx < func_decl->param_size()) {
      ParmVarDecl *var_decl = func_decl->getParamDecl(idx);
      if (var_decl)
        return GetType(var_decl->getOriginalType());
    }
  } else if (clang::ObjCMethodDecl *objc_method =
                 llvm::dyn_cast<clang::ObjCMethodDecl>(
                     (clang::Decl *)opaque_decl)) {
    if (idx < objc_method->param_size())
      return GetType(objc_method->parameters()[idx]->getOriginalType());
  }
  return CompilerType();
}

Scalar TypeSystemClang::DeclGetConstantValue(void *opaque_decl) {
  clang::Decl *decl = static_cast<clang::Decl *>(opaque_decl);
  clang::VarDecl *var_decl = llvm::dyn_cast<clang::VarDecl>(decl);
  if (!var_decl)
    return Scalar();
  clang::Expr *init_expr = var_decl->getInit();
  if (!init_expr)
    return Scalar();
  std::optional<llvm::APSInt> value =
      init_expr->getIntegerConstantExpr(getASTContext());
  if (!value)
    return Scalar();
  return Scalar(*value);
}

// CompilerDeclContext functions

std::vector<CompilerDecl> TypeSystemClang::DeclContextFindDeclByName(
    void *opaque_decl_ctx, ConstString name, const bool ignore_using_decls) {
  std::vector<CompilerDecl> found_decls;
  SymbolFile *symbol_file = GetSymbolFile();
  if (opaque_decl_ctx && symbol_file) {
    DeclContext *root_decl_ctx = (DeclContext *)opaque_decl_ctx;
    std::set<DeclContext *> searched;
    std::multimap<DeclContext *, DeclContext *> search_queue;

    for (clang::DeclContext *decl_context = root_decl_ctx;
         decl_context != nullptr && found_decls.empty();
         decl_context = decl_context->getParent()) {
      search_queue.insert(std::make_pair(decl_context, decl_context));

      for (auto it = search_queue.find(decl_context); it != search_queue.end();
           it++) {
        if (!searched.insert(it->second).second)
          continue;
        symbol_file->ParseDeclsForContext(
            CreateDeclContext(it->second));

        for (clang::Decl *child : it->second->decls()) {
          if (clang::UsingDirectiveDecl *ud =
                  llvm::dyn_cast<clang::UsingDirectiveDecl>(child)) {
            if (ignore_using_decls)
              continue;
            clang::DeclContext *from = ud->getCommonAncestor();
            if (searched.find(ud->getNominatedNamespace()) == searched.end())
              search_queue.insert(
                  std::make_pair(from, ud->getNominatedNamespace()));
          } else if (clang::UsingDecl *ud =
                         llvm::dyn_cast<clang::UsingDecl>(child)) {
            if (ignore_using_decls)
              continue;
            for (clang::UsingShadowDecl *usd : ud->shadows()) {
              clang::Decl *target = usd->getTargetDecl();
              if (clang::NamedDecl *nd =
                      llvm::dyn_cast<clang::NamedDecl>(target)) {
                IdentifierInfo *ii = nd->getIdentifier();
                if (ii != nullptr && ii->getName() == name.AsCString(nullptr))
                  found_decls.push_back(GetCompilerDecl(nd));
              }
            }
          } else if (clang::NamedDecl *nd =
                         llvm::dyn_cast<clang::NamedDecl>(child)) {
            IdentifierInfo *ii = nd->getIdentifier();
            if (ii != nullptr && ii->getName() == name.AsCString(nullptr))
              found_decls.push_back(GetCompilerDecl(nd));
          }
        }
      }
    }
  }
  return found_decls;
}

// Look for child_decl_ctx's lookup scope in frame_decl_ctx and its parents,
// and return the number of levels it took to find it, or
// LLDB_INVALID_DECL_LEVEL if not found.  If the decl was imported via a using
// declaration, its name and/or type, if set, will be used to check that the
// decl found in the scope is a match.
//
// The optional name is required by languages (like C++) to handle using
// declarations like:
//
//     void poo();
//     namespace ns {
//         void foo();
//         void goo();
//     }
//     void bar() {
//         using ns::foo;
//         // CountDeclLevels returns 0 for 'foo', 1 for 'poo', and
//         // LLDB_INVALID_DECL_LEVEL for 'goo'.
//     }
//
// The optional type is useful in the case that there's a specific overload
// that we're looking for that might otherwise be shadowed, like:
//
//     void foo(int);
//     namespace ns {
//         void foo();
//     }
//     void bar() {
//         using ns::foo;
//         // CountDeclLevels returns 0 for { 'foo', void() },
//         // 1 for { 'foo', void(int) }, and
//         // LLDB_INVALID_DECL_LEVEL for { 'foo', void(int, int) }.
//     }
//
// NOTE: Because file statics are at the TranslationUnit along with globals, a
// function at file scope will return the same level as a function at global
// scope. Ideally we'd like to treat the file scope as an additional scope just
// below the global scope.  More work needs to be done to recognise that, if
// the decl we're trying to look up is static, we should compare its source
// file with that of the current scope and return a lower number for it.
uint32_t TypeSystemClang::CountDeclLevels(clang::DeclContext *frame_decl_ctx,
                                          clang::DeclContext *child_decl_ctx,
                                          ConstString *child_name,
                                          CompilerType *child_type) {
  SymbolFile *symbol_file = GetSymbolFile();
  if (frame_decl_ctx && symbol_file) {
    std::set<DeclContext *> searched;
    std::multimap<DeclContext *, DeclContext *> search_queue;

    // Get the lookup scope for the decl we're trying to find.
    clang::DeclContext *parent_decl_ctx = child_decl_ctx->getParent();

    // Look for it in our scope's decl context and its parents.
    uint32_t level = 0;
    for (clang::DeclContext *decl_ctx = frame_decl_ctx; decl_ctx != nullptr;
         decl_ctx = decl_ctx->getParent()) {
      if (!decl_ctx->isLookupContext())
        continue;
      if (decl_ctx == parent_decl_ctx)
        // Found it!
        return level;
      search_queue.insert(std::make_pair(decl_ctx, decl_ctx));
      for (auto it = search_queue.find(decl_ctx); it != search_queue.end();
           it++) {
        if (searched.find(it->second) != searched.end())
          continue;

        // Currently DWARF has one shared translation unit for all Decls at top
        // level, so this would erroneously find using statements anywhere.  So
        // don't look at the top-level translation unit.
        // TODO fix this and add a testcase that depends on it.

        if (llvm::isa<clang::TranslationUnitDecl>(it->second))
          continue;

        searched.insert(it->second);
        symbol_file->ParseDeclsForContext(
            CreateDeclContext(it->second));

        for (clang::Decl *child : it->second->decls()) {
          if (clang::UsingDirectiveDecl *ud =
                  llvm::dyn_cast<clang::UsingDirectiveDecl>(child)) {
            clang::DeclContext *ns = ud->getNominatedNamespace();
            if (ns == parent_decl_ctx)
              // Found it!
              return level;
            clang::DeclContext *from = ud->getCommonAncestor();
            if (searched.find(ns) == searched.end())
              search_queue.insert(std::make_pair(from, ns));
          } else if (child_name) {
            if (clang::UsingDecl *ud =
                    llvm::dyn_cast<clang::UsingDecl>(child)) {
              for (clang::UsingShadowDecl *usd : ud->shadows()) {
                clang::Decl *target = usd->getTargetDecl();
                clang::NamedDecl *nd = llvm::dyn_cast<clang::NamedDecl>(target);
                if (!nd)
                  continue;
                // Check names.
                IdentifierInfo *ii = nd->getIdentifier();
                if (ii == nullptr ||
                    ii->getName() != child_name->AsCString(nullptr))
                  continue;
                // Check types, if one was provided.
                if (child_type) {
                  CompilerType clang_type = GetTypeForDecl(nd);
                  if (!AreTypesSame(clang_type, *child_type,
                                    /*ignore_qualifiers=*/true))
                    continue;
                }
                // Found it!
                return level;
              }
            }
          }
        }
      }
      ++level;
    }
  }
  return LLDB_INVALID_DECL_LEVEL;
}

ConstString TypeSystemClang::DeclContextGetName(void *opaque_decl_ctx) {
  if (opaque_decl_ctx) {
    clang::NamedDecl *named_decl =
        llvm::dyn_cast<clang::NamedDecl>((clang::DeclContext *)opaque_decl_ctx);
    if (named_decl) {
      std::string name;
      llvm::raw_string_ostream stream{name};
      auto policy = GetTypePrintingPolicy();
      policy.AlwaysIncludeTypeForTemplateArgument = true;
      named_decl->getNameForDiagnostic(stream, policy, /*qualified=*/false);
      return ConstString(name);
    }
  }
  return ConstString();
}

ConstString
TypeSystemClang::DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) {
  if (opaque_decl_ctx) {
    clang::NamedDecl *named_decl =
        llvm::dyn_cast<clang::NamedDecl>((clang::DeclContext *)opaque_decl_ctx);
    if (named_decl)
      return ConstString(GetTypeNameForDecl(named_decl));
  }
  return ConstString();
}

bool TypeSystemClang::DeclContextIsClassMethod(void *opaque_decl_ctx) {
  if (!opaque_decl_ctx)
    return false;

  clang::DeclContext *decl_ctx = (clang::DeclContext *)opaque_decl_ctx;
  if (llvm::isa<clang::ObjCMethodDecl>(decl_ctx)) {
    return true;
  } else if (llvm::isa<clang::CXXMethodDecl>(decl_ctx)) {
    return true;
  } else if (clang::FunctionDecl *fun_decl =
                 llvm::dyn_cast<clang::FunctionDecl>(decl_ctx)) {
    if (ClangASTMetadata *metadata = GetMetadata(fun_decl))
      return metadata->HasObjectPtr();
  }

  return false;
}

std::vector<lldb_private::CompilerContext>
TypeSystemClang::DeclContextGetCompilerContext(void *opaque_decl_ctx) {
  auto *decl_ctx = (clang::DeclContext *)opaque_decl_ctx;
  std::vector<lldb_private::CompilerContext> context;
  InsertCompilerContext(this, decl_ctx, context);
  return context;
}

bool TypeSystemClang::DeclContextIsContainedInLookup(
    void *opaque_decl_ctx, void *other_opaque_decl_ctx) {
  auto *decl_ctx = (clang::DeclContext *)opaque_decl_ctx;
  auto *other = (clang::DeclContext *)other_opaque_decl_ctx;

  // If we have an inline or anonymous namespace, then the lookup of the
  // parent context also includes those namespace contents.
  auto is_transparent_lookup_allowed = [](clang::DeclContext *DC) {
    if (DC->isInlineNamespace())
      return true;

    if (auto const *NS = dyn_cast<NamespaceDecl>(DC))
      return NS->isAnonymousNamespace();

    return false;
  };

  do {
    // A decl context always includes its own contents in its lookup.
    if (decl_ctx == other)
      return true;
  } while (is_transparent_lookup_allowed(other) &&
           (other = other->getParent()));

  return false;
}

lldb::LanguageType
TypeSystemClang::DeclContextGetLanguage(void *opaque_decl_ctx) {
  if (!opaque_decl_ctx)
    return eLanguageTypeUnknown;

  auto *decl_ctx = (clang::DeclContext *)opaque_decl_ctx;
  if (llvm::isa<clang::ObjCMethodDecl>(decl_ctx)) {
    return eLanguageTypeObjC;
  } else if (llvm::isa<clang::CXXMethodDecl>(decl_ctx)) {
    return eLanguageTypeC_plus_plus;
  } else if (auto *fun_decl = llvm::dyn_cast<clang::FunctionDecl>(decl_ctx)) {
    if (ClangASTMetadata *metadata = GetMetadata(fun_decl))
      return metadata->GetObjectPtrLanguage();
  }

  return eLanguageTypeUnknown;
}

static bool IsClangDeclContext(const CompilerDeclContext &dc) {
  return dc.IsValid() && isa<TypeSystemClang>(dc.GetTypeSystem());
}

clang::DeclContext *
TypeSystemClang::DeclContextGetAsDeclContext(const CompilerDeclContext &dc) {
  if (IsClangDeclContext(dc))
    return (clang::DeclContext *)dc.GetOpaqueDeclContext();
  return nullptr;
}

ObjCMethodDecl *
TypeSystemClang::DeclContextGetAsObjCMethodDecl(const CompilerDeclContext &dc) {
  if (IsClangDeclContext(dc))
    return llvm::dyn_cast<clang::ObjCMethodDecl>(
        (clang::DeclContext *)dc.GetOpaqueDeclContext());
  return nullptr;
}

CXXMethodDecl *
TypeSystemClang::DeclContextGetAsCXXMethodDecl(const CompilerDeclContext &dc) {
  if (IsClangDeclContext(dc))
    return llvm::dyn_cast<clang::CXXMethodDecl>(
        (clang::DeclContext *)dc.GetOpaqueDeclContext());
  return nullptr;
}

clang::FunctionDecl *
TypeSystemClang::DeclContextGetAsFunctionDecl(const CompilerDeclContext &dc) {
  if (IsClangDeclContext(dc))
    return llvm::dyn_cast<clang::FunctionDecl>(
        (clang::DeclContext *)dc.GetOpaqueDeclContext());
  return nullptr;
}

clang::NamespaceDecl *
TypeSystemClang::DeclContextGetAsNamespaceDecl(const CompilerDeclContext &dc) {
  if (IsClangDeclContext(dc))
    return llvm::dyn_cast<clang::NamespaceDecl>(
        (clang::DeclContext *)dc.GetOpaqueDeclContext());
  return nullptr;
}

ClangASTMetadata *
TypeSystemClang::DeclContextGetMetaData(const CompilerDeclContext &dc,
                                        const Decl *object) {
  TypeSystemClang *ast = llvm::cast<TypeSystemClang>(dc.GetTypeSystem());
  return ast->GetMetadata(object);
}

clang::ASTContext *
TypeSystemClang::DeclContextGetTypeSystemClang(const CompilerDeclContext &dc) {
  TypeSystemClang *ast =
      llvm::dyn_cast_or_null<TypeSystemClang>(dc.GetTypeSystem());
  if (ast)
    return &ast->getASTContext();
  return nullptr;
}

void TypeSystemClang::RequireCompleteType(CompilerType type) {
  // Technically, enums can be incomplete too, but we don't handle those as they
  // are emitted even under -flimit-debug-info.
  if (!TypeSystemClang::IsCXXClassType(type))
    return;

  if (type.GetCompleteType())
    return;

  // No complete definition in this module.  Mark the class as complete to
  // satisfy local ast invariants, but make a note of the fact that
  // it is not _really_ complete so we can later search for a definition in a
  // different module.
  // Since we provide layout assistance, layouts of types containing this class
  // will be correct even if we  are not able to find the definition elsewhere.
  bool started = TypeSystemClang::StartTagDeclarationDefinition(type);
  lldbassert(started && "Unable to start a class type definition.");
  TypeSystemClang::CompleteTagDeclarationDefinition(type);
  const clang::TagDecl *td = ClangUtil::GetAsTagDecl(type);
  auto ts = type.GetTypeSystem().dyn_cast_or_null<TypeSystemClang>();
  if (ts)
    ts->SetDeclIsForcefullyCompleted(td);
}

namespace {
/// A specialized scratch AST used within ScratchTypeSystemClang.
/// These are the ASTs backing the different IsolatedASTKinds. They behave
/// like a normal ScratchTypeSystemClang but they don't own their own
/// persistent  storage or target reference.
class SpecializedScratchAST : public TypeSystemClang {
public:
  /// \param name The display name of the TypeSystemClang instance.
  /// \param triple The triple used for the TypeSystemClang instance.
  /// \param ast_source The ClangASTSource that should be used to complete
  ///                   type information.
  SpecializedScratchAST(llvm::StringRef name, llvm::Triple triple,
                        std::unique_ptr<ClangASTSource> ast_source)
      : TypeSystemClang(name, triple),
        m_scratch_ast_source_up(std::move(ast_source)) {
    // Setup the ClangASTSource to complete this AST.
    m_scratch_ast_source_up->InstallASTContext(*this);
    llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> proxy_ast_source(
        m_scratch_ast_source_up->CreateProxy());
    SetExternalSource(proxy_ast_source);
  }

  /// The ExternalASTSource that performs lookups and completes types.
  std::unique_ptr<ClangASTSource> m_scratch_ast_source_up;
};
} // namespace

char ScratchTypeSystemClang::ID;
const std::nullopt_t ScratchTypeSystemClang::DefaultAST = std::nullopt;

ScratchTypeSystemClang::ScratchTypeSystemClang(Target &target,
                                               llvm::Triple triple)
    : TypeSystemClang("scratch ASTContext", triple), m_triple(triple),
      m_target_wp(target.shared_from_this()),
      m_persistent_variables(
          new ClangPersistentVariables(target.shared_from_this())) {
  m_scratch_ast_source_up = CreateASTSource();
  m_scratch_ast_source_up->InstallASTContext(*this);
  llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> proxy_ast_source(
      m_scratch_ast_source_up->CreateProxy());
  SetExternalSource(proxy_ast_source);
}

void ScratchTypeSystemClang::Finalize() {
  TypeSystemClang::Finalize();
  m_scratch_ast_source_up.reset();
}

TypeSystemClangSP
ScratchTypeSystemClang::GetForTarget(Target &target,
                                     std::optional<IsolatedASTKind> ast_kind,
                                     bool create_on_demand) {
  auto type_system_or_err = target.GetScratchTypeSystemForLanguage(
      lldb::eLanguageTypeC, create_on_demand);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Target), std::move(err),
                   "Couldn't get scratch TypeSystemClang");
    return nullptr;
  }
  auto ts_sp = *type_system_or_err;
  ScratchTypeSystemClang *scratch_ast =
      llvm::dyn_cast_or_null<ScratchTypeSystemClang>(ts_sp.get());
  if (!scratch_ast)
    return nullptr;
  // If no dedicated sub-AST was requested, just return the main AST.
  if (ast_kind == DefaultAST)
    return std::static_pointer_cast<TypeSystemClang>(ts_sp);
  // Search the sub-ASTs.
  return std::static_pointer_cast<TypeSystemClang>(
      scratch_ast->GetIsolatedAST(*ast_kind).shared_from_this());
}

/// Returns a human-readable name that uniquely identifiers the sub-AST kind.
static llvm::StringRef
GetNameForIsolatedASTKind(ScratchTypeSystemClang::IsolatedASTKind kind) {
  switch (kind) {
  case ScratchTypeSystemClang::IsolatedASTKind::CppModules:
    return "C++ modules";
  }
  llvm_unreachable("Unimplemented IsolatedASTKind?");
}

void ScratchTypeSystemClang::Dump(llvm::raw_ostream &output) {
  // First dump the main scratch AST.
  output << "State of scratch Clang type system:\n";
  TypeSystemClang::Dump(output);

  // Now sort the isolated sub-ASTs.
  typedef std::pair<IsolatedASTKey, TypeSystem *> KeyAndTS;
  std::vector<KeyAndTS> sorted_typesystems;
  for (const auto &a : m_isolated_asts)
    sorted_typesystems.emplace_back(a.first, a.second.get());
  llvm::stable_sort(sorted_typesystems, llvm::less_first());

  // Dump each sub-AST too.
  for (const auto &a : sorted_typesystems) {
    IsolatedASTKind kind =
        static_cast<ScratchTypeSystemClang::IsolatedASTKind>(a.first);
    output << "State of scratch Clang type subsystem "
           << GetNameForIsolatedASTKind(kind) << ":\n";
    a.second->Dump(output);
  }
}

UserExpression *ScratchTypeSystemClang::GetUserExpression(
    llvm::StringRef expr, llvm::StringRef prefix, SourceLanguage language,
    Expression::ResultType desired_type,
    const EvaluateExpressionOptions &options, ValueObject *ctx_obj) {
  TargetSP target_sp = m_target_wp.lock();
  if (!target_sp)
    return nullptr;

  return new ClangUserExpression(*target_sp.get(), expr, prefix, language,
                                 desired_type, options, ctx_obj);
}

FunctionCaller *ScratchTypeSystemClang::GetFunctionCaller(
    const CompilerType &return_type, const Address &function_address,
    const ValueList &arg_value_list, const char *name) {
  TargetSP target_sp = m_target_wp.lock();
  if (!target_sp)
    return nullptr;

  Process *process = target_sp->GetProcessSP().get();
  if (!process)
    return nullptr;

  return new ClangFunctionCaller(*process, return_type, function_address,
                                 arg_value_list, name);
}

std::unique_ptr<UtilityFunction>
ScratchTypeSystemClang::CreateUtilityFunction(std::string text,
                                              std::string name) {
  TargetSP target_sp = m_target_wp.lock();
  if (!target_sp)
    return {};

  return std::make_unique<ClangUtilityFunction>(
      *target_sp.get(), std::move(text), std::move(name),
      target_sp->GetDebugUtilityExpression());
}

PersistentExpressionState *
ScratchTypeSystemClang::GetPersistentExpressionState() {
  return m_persistent_variables.get();
}

void ScratchTypeSystemClang::ForgetSource(ASTContext *src_ctx,
                                          ClangASTImporter &importer) {
  // Remove it as a source from the main AST.
  importer.ForgetSource(&getASTContext(), src_ctx);
  // Remove it as a source from all created sub-ASTs.
  for (const auto &a : m_isolated_asts)
    importer.ForgetSource(&a.second->getASTContext(), src_ctx);
}

std::unique_ptr<ClangASTSource> ScratchTypeSystemClang::CreateASTSource() {
  return std::make_unique<ClangASTSource>(
      m_target_wp.lock()->shared_from_this(),
      m_persistent_variables->GetClangASTImporter());
}

static llvm::StringRef
GetSpecializedASTName(ScratchTypeSystemClang::IsolatedASTKind feature) {
  switch (feature) {
  case ScratchTypeSystemClang::IsolatedASTKind::CppModules:
    return "scratch ASTContext for C++ module types";
  }
  llvm_unreachable("Unimplemented ASTFeature kind?");
}

TypeSystemClang &ScratchTypeSystemClang::GetIsolatedAST(
    ScratchTypeSystemClang::IsolatedASTKind feature) {
  auto found_ast = m_isolated_asts.find(feature);
  if (found_ast != m_isolated_asts.end())
    return *found_ast->second;

  // Couldn't find the requested sub-AST, so create it now.
  std::shared_ptr<TypeSystemClang> new_ast_sp =
      std::make_shared<SpecializedScratchAST>(GetSpecializedASTName(feature),
                                              m_triple, CreateASTSource());
  m_isolated_asts.insert({feature, new_ast_sp});
  return *new_ast_sp;
}

bool TypeSystemClang::IsForcefullyCompleted(lldb::opaque_compiler_type_t type) {
  if (type) {
    clang::QualType qual_type(GetQualType(type));
    const clang::RecordType *record_type =
        llvm::dyn_cast<clang::RecordType>(qual_type.getTypePtr());
    if (record_type) {
      const clang::RecordDecl *record_decl = record_type->getDecl();
      assert(record_decl);
      ClangASTMetadata *metadata = GetMetadata(record_decl);
      if (metadata)
        return metadata->IsForcefullyCompleted();
    }
  }
  return false;
}

bool TypeSystemClang::SetDeclIsForcefullyCompleted(const clang::TagDecl *td) {
  if (td == nullptr)
    return false;
  ClangASTMetadata *metadata = GetMetadata(td);
  if (metadata == nullptr)
    return false;
  m_has_forcefully_completed_types = true;
  metadata->SetIsForcefullyCompleted();
  return true;
}

void TypeSystemClang::LogCreation() const {
  if (auto *log = GetLog(LLDBLog::Expressions))
    LLDB_LOG(log, "Created new TypeSystem for (ASTContext*){0:x} '{1}'",
             &getASTContext(), getDisplayName());
}
