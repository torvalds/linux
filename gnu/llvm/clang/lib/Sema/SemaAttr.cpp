//===--- SemaAttr.cpp - Semantic Analysis for Attributes ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements semantic analysis for non-trivial attributes and
// pragmas.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/SemaInternal.h"
#include <optional>
using namespace clang;

//===----------------------------------------------------------------------===//
// Pragma 'pack' and 'options align'
//===----------------------------------------------------------------------===//

Sema::PragmaStackSentinelRAII::PragmaStackSentinelRAII(Sema &S,
                                                       StringRef SlotLabel,
                                                       bool ShouldAct)
    : S(S), SlotLabel(SlotLabel), ShouldAct(ShouldAct) {
  if (ShouldAct) {
    S.VtorDispStack.SentinelAction(PSK_Push, SlotLabel);
    S.DataSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.BSSSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.ConstSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.CodeSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.StrictGuardStackCheckStack.SentinelAction(PSK_Push, SlotLabel);
  }
}

Sema::PragmaStackSentinelRAII::~PragmaStackSentinelRAII() {
  if (ShouldAct) {
    S.VtorDispStack.SentinelAction(PSK_Pop, SlotLabel);
    S.DataSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.BSSSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.ConstSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.CodeSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.StrictGuardStackCheckStack.SentinelAction(PSK_Pop, SlotLabel);
  }
}

void Sema::AddAlignmentAttributesForRecord(RecordDecl *RD) {
  AlignPackInfo InfoVal = AlignPackStack.CurrentValue;
  AlignPackInfo::Mode M = InfoVal.getAlignMode();
  bool IsPackSet = InfoVal.IsPackSet();
  bool IsXLPragma = getLangOpts().XLPragmaPack;

  // If we are not under mac68k/natural alignment mode and also there is no pack
  // value, we don't need any attributes.
  if (!IsPackSet && M != AlignPackInfo::Mac68k && M != AlignPackInfo::Natural)
    return;

  if (M == AlignPackInfo::Mac68k && (IsXLPragma || InfoVal.IsAlignAttr())) {
    RD->addAttr(AlignMac68kAttr::CreateImplicit(Context));
  } else if (IsPackSet) {
    // Check to see if we need a max field alignment attribute.
    RD->addAttr(MaxFieldAlignmentAttr::CreateImplicit(
        Context, InfoVal.getPackNumber() * 8));
  }

  if (IsXLPragma && M == AlignPackInfo::Natural)
    RD->addAttr(AlignNaturalAttr::CreateImplicit(Context));

  if (AlignPackIncludeStack.empty())
    return;
  // The #pragma align/pack affected a record in an included file, so Clang
  // should warn when that pragma was written in a file that included the
  // included file.
  for (auto &AlignPackedInclude : llvm::reverse(AlignPackIncludeStack)) {
    if (AlignPackedInclude.CurrentPragmaLocation !=
        AlignPackStack.CurrentPragmaLocation)
      break;
    if (AlignPackedInclude.HasNonDefaultValue)
      AlignPackedInclude.ShouldWarnOnInclude = true;
  }
}

void Sema::AddMsStructLayoutForRecord(RecordDecl *RD) {
  if (MSStructPragmaOn)
    RD->addAttr(MSStructAttr::CreateImplicit(Context));

  // FIXME: We should merge AddAlignmentAttributesForRecord with
  // AddMsStructLayoutForRecord into AddPragmaAttributesForRecord, which takes
  // all active pragmas and applies them as attributes to class definitions.
  if (VtorDispStack.CurrentValue != getLangOpts().getVtorDispMode())
    RD->addAttr(MSVtorDispAttr::CreateImplicit(
        Context, unsigned(VtorDispStack.CurrentValue)));
}

template <typename Attribute>
static void addGslOwnerPointerAttributeIfNotExisting(ASTContext &Context,
                                                     CXXRecordDecl *Record) {
  if (Record->hasAttr<OwnerAttr>() || Record->hasAttr<PointerAttr>())
    return;

  for (Decl *Redecl : Record->redecls())
    Redecl->addAttr(Attribute::CreateImplicit(Context, /*DerefType=*/nullptr));
}

void Sema::inferGslPointerAttribute(NamedDecl *ND,
                                    CXXRecordDecl *UnderlyingRecord) {
  if (!UnderlyingRecord)
    return;

  const auto *Parent = dyn_cast<CXXRecordDecl>(ND->getDeclContext());
  if (!Parent)
    return;

  static const llvm::StringSet<> Containers{
      "array",
      "basic_string",
      "deque",
      "forward_list",
      "vector",
      "list",
      "map",
      "multiset",
      "multimap",
      "priority_queue",
      "queue",
      "set",
      "stack",
      "unordered_set",
      "unordered_map",
      "unordered_multiset",
      "unordered_multimap",
  };

  static const llvm::StringSet<> Iterators{"iterator", "const_iterator",
                                           "reverse_iterator",
                                           "const_reverse_iterator"};

  if (Parent->isInStdNamespace() && Iterators.count(ND->getName()) &&
      Containers.count(Parent->getName()))
    addGslOwnerPointerAttributeIfNotExisting<PointerAttr>(Context,
                                                          UnderlyingRecord);
}

void Sema::inferGslPointerAttribute(TypedefNameDecl *TD) {

  QualType Canonical = TD->getUnderlyingType().getCanonicalType();

  CXXRecordDecl *RD = Canonical->getAsCXXRecordDecl();
  if (!RD) {
    if (auto *TST =
            dyn_cast<TemplateSpecializationType>(Canonical.getTypePtr())) {

      RD = dyn_cast_or_null<CXXRecordDecl>(
          TST->getTemplateName().getAsTemplateDecl()->getTemplatedDecl());
    }
  }

  inferGslPointerAttribute(TD, RD);
}

void Sema::inferGslOwnerPointerAttribute(CXXRecordDecl *Record) {
  static const llvm::StringSet<> StdOwners{
      "any",
      "array",
      "basic_regex",
      "basic_string",
      "deque",
      "forward_list",
      "vector",
      "list",
      "map",
      "multiset",
      "multimap",
      "optional",
      "priority_queue",
      "queue",
      "set",
      "stack",
      "unique_ptr",
      "unordered_set",
      "unordered_map",
      "unordered_multiset",
      "unordered_multimap",
      "variant",
  };
  static const llvm::StringSet<> StdPointers{
      "basic_string_view",
      "reference_wrapper",
      "regex_iterator",
      "span",
  };

  if (!Record->getIdentifier())
    return;

  // Handle classes that directly appear in std namespace.
  if (Record->isInStdNamespace()) {
    if (Record->hasAttr<OwnerAttr>() || Record->hasAttr<PointerAttr>())
      return;

    if (StdOwners.count(Record->getName()))
      addGslOwnerPointerAttributeIfNotExisting<OwnerAttr>(Context, Record);
    else if (StdPointers.count(Record->getName()))
      addGslOwnerPointerAttributeIfNotExisting<PointerAttr>(Context, Record);

    return;
  }

  // Handle nested classes that could be a gsl::Pointer.
  inferGslPointerAttribute(Record, Record);
}

void Sema::inferNullableClassAttribute(CXXRecordDecl *CRD) {
  static const llvm::StringSet<> Nullable{
      "auto_ptr",         "shared_ptr", "unique_ptr",         "exception_ptr",
      "coroutine_handle", "function",   "move_only_function",
  };

  if (CRD->isInStdNamespace() && Nullable.count(CRD->getName()) &&
      !CRD->hasAttr<TypeNullableAttr>())
    for (Decl *Redecl : CRD->redecls())
      Redecl->addAttr(TypeNullableAttr::CreateImplicit(Context));
}

void Sema::ActOnPragmaOptionsAlign(PragmaOptionsAlignKind Kind,
                                   SourceLocation PragmaLoc) {
  PragmaMsStackAction Action = Sema::PSK_Reset;
  AlignPackInfo::Mode ModeVal = AlignPackInfo::Native;

  switch (Kind) {
    // For most of the platforms we support, native and natural are the same.
    // With XL, native is the same as power, natural means something else.
  case POAK_Native:
  case POAK_Power:
    Action = Sema::PSK_Push_Set;
    break;
  case POAK_Natural:
    Action = Sema::PSK_Push_Set;
    ModeVal = AlignPackInfo::Natural;
    break;

    // Note that '#pragma options align=packed' is not equivalent to attribute
    // packed, it has a different precedence relative to attribute aligned.
  case POAK_Packed:
    Action = Sema::PSK_Push_Set;
    ModeVal = AlignPackInfo::Packed;
    break;

  case POAK_Mac68k:
    // Check if the target supports this.
    if (!this->Context.getTargetInfo().hasAlignMac68kSupport()) {
      Diag(PragmaLoc, diag::err_pragma_options_align_mac68k_target_unsupported);
      return;
    }
    Action = Sema::PSK_Push_Set;
    ModeVal = AlignPackInfo::Mac68k;
    break;
  case POAK_Reset:
    // Reset just pops the top of the stack, or resets the current alignment to
    // default.
    Action = Sema::PSK_Pop;
    if (AlignPackStack.Stack.empty()) {
      if (AlignPackStack.CurrentValue.getAlignMode() != AlignPackInfo::Native ||
          AlignPackStack.CurrentValue.IsPackAttr()) {
        Action = Sema::PSK_Reset;
      } else {
        Diag(PragmaLoc, diag::warn_pragma_options_align_reset_failed)
            << "stack empty";
        return;
      }
    }
    break;
  }

  AlignPackInfo Info(ModeVal, getLangOpts().XLPragmaPack);

  AlignPackStack.Act(PragmaLoc, Action, StringRef(), Info);
}

void Sema::ActOnPragmaClangSection(SourceLocation PragmaLoc,
                                   PragmaClangSectionAction Action,
                                   PragmaClangSectionKind SecKind,
                                   StringRef SecName) {
  PragmaClangSection *CSec;
  int SectionFlags = ASTContext::PSF_Read;
  switch (SecKind) {
    case PragmaClangSectionKind::PCSK_BSS:
      CSec = &PragmaClangBSSSection;
      SectionFlags |= ASTContext::PSF_Write | ASTContext::PSF_ZeroInit;
      break;
    case PragmaClangSectionKind::PCSK_Data:
      CSec = &PragmaClangDataSection;
      SectionFlags |= ASTContext::PSF_Write;
      break;
    case PragmaClangSectionKind::PCSK_Rodata:
      CSec = &PragmaClangRodataSection;
      break;
    case PragmaClangSectionKind::PCSK_Relro:
      CSec = &PragmaClangRelroSection;
      break;
    case PragmaClangSectionKind::PCSK_Text:
      CSec = &PragmaClangTextSection;
      SectionFlags |= ASTContext::PSF_Execute;
      break;
    default:
      llvm_unreachable("invalid clang section kind");
  }

  if (Action == PragmaClangSectionAction::PCSA_Clear) {
    CSec->Valid = false;
    return;
  }

  if (llvm::Error E = isValidSectionSpecifier(SecName)) {
    Diag(PragmaLoc, diag::err_pragma_section_invalid_for_target)
        << toString(std::move(E));
    CSec->Valid = false;
    return;
  }

  if (UnifySection(SecName, SectionFlags, PragmaLoc))
    return;

  CSec->Valid = true;
  CSec->SectionName = std::string(SecName);
  CSec->PragmaLocation = PragmaLoc;
}

void Sema::ActOnPragmaPack(SourceLocation PragmaLoc, PragmaMsStackAction Action,
                           StringRef SlotLabel, Expr *alignment) {
  bool IsXLPragma = getLangOpts().XLPragmaPack;
  // XL pragma pack does not support identifier syntax.
  if (IsXLPragma && !SlotLabel.empty()) {
    Diag(PragmaLoc, diag::err_pragma_pack_identifer_not_supported);
    return;
  }

  const AlignPackInfo CurVal = AlignPackStack.CurrentValue;
  Expr *Alignment = static_cast<Expr *>(alignment);

  // If specified then alignment must be a "small" power of two.
  unsigned AlignmentVal = 0;
  AlignPackInfo::Mode ModeVal = CurVal.getAlignMode();

  if (Alignment) {
    std::optional<llvm::APSInt> Val;
    Val = Alignment->getIntegerConstantExpr(Context);

    // pack(0) is like pack(), which just works out since that is what
    // we use 0 for in PackAttr.
    if (Alignment->isTypeDependent() || !Val ||
        !(*Val == 0 || Val->isPowerOf2()) || Val->getZExtValue() > 16) {
      Diag(PragmaLoc, diag::warn_pragma_pack_invalid_alignment);
      return; // Ignore
    }

    if (IsXLPragma && *Val == 0) {
      // pack(0) does not work out with XL.
      Diag(PragmaLoc, diag::err_pragma_pack_invalid_alignment);
      return; // Ignore
    }

    AlignmentVal = (unsigned)Val->getZExtValue();
  }

  if (Action == Sema::PSK_Show) {
    // Show the current alignment, making sure to show the right value
    // for the default.
    // FIXME: This should come from the target.
    AlignmentVal = CurVal.IsPackSet() ? CurVal.getPackNumber() : 8;
    if (ModeVal == AlignPackInfo::Mac68k &&
        (IsXLPragma || CurVal.IsAlignAttr()))
      Diag(PragmaLoc, diag::warn_pragma_pack_show) << "mac68k";
    else
      Diag(PragmaLoc, diag::warn_pragma_pack_show) << AlignmentVal;
  }

  // MSDN, C/C++ Preprocessor Reference > Pragma Directives > pack:
  // "#pragma pack(pop, identifier, n) is undefined"
  if (Action & Sema::PSK_Pop) {
    if (Alignment && !SlotLabel.empty())
      Diag(PragmaLoc, diag::warn_pragma_pack_pop_identifier_and_alignment);
    if (AlignPackStack.Stack.empty()) {
      assert(CurVal.getAlignMode() == AlignPackInfo::Native &&
             "Empty pack stack can only be at Native alignment mode.");
      Diag(PragmaLoc, diag::warn_pragma_pop_failed) << "pack" << "stack empty";
    }
  }

  AlignPackInfo Info(ModeVal, AlignmentVal, IsXLPragma);

  AlignPackStack.Act(PragmaLoc, Action, SlotLabel, Info);
}

bool Sema::ConstantFoldAttrArgs(const AttributeCommonInfo &CI,
                                MutableArrayRef<Expr *> Args) {
  llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
  for (unsigned Idx = 0; Idx < Args.size(); Idx++) {
    Expr *&E = Args.begin()[Idx];
    assert(E && "error are handled before");
    if (E->isValueDependent() || E->isTypeDependent())
      continue;

    // FIXME: Use DefaultFunctionArrayLValueConversion() in place of the logic
    // that adds implicit casts here.
    if (E->getType()->isArrayType())
      E = ImpCastExprToType(E, Context.getPointerType(E->getType()),
                            clang::CK_ArrayToPointerDecay)
              .get();
    if (E->getType()->isFunctionType())
      E = ImplicitCastExpr::Create(Context,
                                   Context.getPointerType(E->getType()),
                                   clang::CK_FunctionToPointerDecay, E, nullptr,
                                   VK_PRValue, FPOptionsOverride());
    if (E->isLValue())
      E = ImplicitCastExpr::Create(Context, E->getType().getNonReferenceType(),
                                   clang::CK_LValueToRValue, E, nullptr,
                                   VK_PRValue, FPOptionsOverride());

    Expr::EvalResult Eval;
    Notes.clear();
    Eval.Diag = &Notes;

    bool Result = E->EvaluateAsConstantExpr(Eval, Context);

    /// Result means the expression can be folded to a constant.
    /// Note.empty() means the expression is a valid constant expression in the
    /// current language mode.
    if (!Result || !Notes.empty()) {
      Diag(E->getBeginLoc(), diag::err_attribute_argument_n_type)
          << CI << (Idx + 1) << AANT_ArgumentConstantExpr;
      for (auto &Note : Notes)
        Diag(Note.first, Note.second);
      return false;
    }
    assert(Eval.Val.hasValue());
    E = ConstantExpr::Create(Context, E, Eval.Val);
  }

  return true;
}

void Sema::DiagnoseNonDefaultPragmaAlignPack(PragmaAlignPackDiagnoseKind Kind,
                                             SourceLocation IncludeLoc) {
  if (Kind == PragmaAlignPackDiagnoseKind::NonDefaultStateAtInclude) {
    SourceLocation PrevLocation = AlignPackStack.CurrentPragmaLocation;
    // Warn about non-default alignment at #includes (without redundant
    // warnings for the same directive in nested includes).
    // The warning is delayed until the end of the file to avoid warnings
    // for files that don't have any records that are affected by the modified
    // alignment.
    bool HasNonDefaultValue =
        AlignPackStack.hasValue() &&
        (AlignPackIncludeStack.empty() ||
         AlignPackIncludeStack.back().CurrentPragmaLocation != PrevLocation);
    AlignPackIncludeStack.push_back(
        {AlignPackStack.CurrentValue,
         AlignPackStack.hasValue() ? PrevLocation : SourceLocation(),
         HasNonDefaultValue, /*ShouldWarnOnInclude*/ false});
    return;
  }

  assert(Kind == PragmaAlignPackDiagnoseKind::ChangedStateAtExit &&
         "invalid kind");
  AlignPackIncludeState PrevAlignPackState =
      AlignPackIncludeStack.pop_back_val();
  // FIXME: AlignPackStack may contain both #pragma align and #pragma pack
  // information, diagnostics below might not be accurate if we have mixed
  // pragmas.
  if (PrevAlignPackState.ShouldWarnOnInclude) {
    // Emit the delayed non-default alignment at #include warning.
    Diag(IncludeLoc, diag::warn_pragma_pack_non_default_at_include);
    Diag(PrevAlignPackState.CurrentPragmaLocation, diag::note_pragma_pack_here);
  }
  // Warn about modified alignment after #includes.
  if (PrevAlignPackState.CurrentValue != AlignPackStack.CurrentValue) {
    Diag(IncludeLoc, diag::warn_pragma_pack_modified_after_include);
    Diag(AlignPackStack.CurrentPragmaLocation, diag::note_pragma_pack_here);
  }
}

void Sema::DiagnoseUnterminatedPragmaAlignPack() {
  if (AlignPackStack.Stack.empty())
    return;
  bool IsInnermost = true;

  // FIXME: AlignPackStack may contain both #pragma align and #pragma pack
  // information, diagnostics below might not be accurate if we have mixed
  // pragmas.
  for (const auto &StackSlot : llvm::reverse(AlignPackStack.Stack)) {
    Diag(StackSlot.PragmaPushLocation, diag::warn_pragma_pack_no_pop_eof);
    // The user might have already reset the alignment, so suggest replacing
    // the reset with a pop.
    if (IsInnermost &&
        AlignPackStack.CurrentValue == AlignPackStack.DefaultValue) {
      auto DB = Diag(AlignPackStack.CurrentPragmaLocation,
                     diag::note_pragma_pack_pop_instead_reset);
      SourceLocation FixItLoc =
          Lexer::findLocationAfterToken(AlignPackStack.CurrentPragmaLocation,
                                        tok::l_paren, SourceMgr, LangOpts,
                                        /*SkipTrailing=*/false);
      if (FixItLoc.isValid())
        DB << FixItHint::CreateInsertion(FixItLoc, "pop");
    }
    IsInnermost = false;
  }
}

void Sema::ActOnPragmaMSStruct(PragmaMSStructKind Kind) {
  MSStructPragmaOn = (Kind == PMSST_ON);
}

void Sema::ActOnPragmaMSComment(SourceLocation CommentLoc,
                                PragmaMSCommentKind Kind, StringRef Arg) {
  auto *PCD = PragmaCommentDecl::Create(
      Context, Context.getTranslationUnitDecl(), CommentLoc, Kind, Arg);
  Context.getTranslationUnitDecl()->addDecl(PCD);
  Consumer.HandleTopLevelDecl(DeclGroupRef(PCD));
}

void Sema::ActOnPragmaDetectMismatch(SourceLocation Loc, StringRef Name,
                                     StringRef Value) {
  auto *PDMD = PragmaDetectMismatchDecl::Create(
      Context, Context.getTranslationUnitDecl(), Loc, Name, Value);
  Context.getTranslationUnitDecl()->addDecl(PDMD);
  Consumer.HandleTopLevelDecl(DeclGroupRef(PDMD));
}

void Sema::ActOnPragmaFPEvalMethod(SourceLocation Loc,
                                   LangOptions::FPEvalMethodKind Value) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  switch (Value) {
  default:
    llvm_unreachable("invalid pragma eval_method kind");
  case LangOptions::FEM_Source:
    NewFPFeatures.setFPEvalMethodOverride(LangOptions::FEM_Source);
    break;
  case LangOptions::FEM_Double:
    NewFPFeatures.setFPEvalMethodOverride(LangOptions::FEM_Double);
    break;
  case LangOptions::FEM_Extended:
    NewFPFeatures.setFPEvalMethodOverride(LangOptions::FEM_Extended);
    break;
  }
  if (getLangOpts().ApproxFunc)
    Diag(Loc, diag::err_setting_eval_method_used_in_unsafe_context) << 0 << 0;
  if (getLangOpts().AllowFPReassoc)
    Diag(Loc, diag::err_setting_eval_method_used_in_unsafe_context) << 0 << 1;
  if (getLangOpts().AllowRecip)
    Diag(Loc, diag::err_setting_eval_method_used_in_unsafe_context) << 0 << 2;
  FpPragmaStack.Act(Loc, PSK_Set, StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
  PP.setCurrentFPEvalMethod(Loc, Value);
}

void Sema::ActOnPragmaFloatControl(SourceLocation Loc,
                                   PragmaMsStackAction Action,
                                   PragmaFloatControlKind Value) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  if ((Action == PSK_Push_Set || Action == PSK_Push || Action == PSK_Pop) &&
      !CurContext->getRedeclContext()->isFileContext()) {
    // Push and pop can only occur at file or namespace scope, or within a
    // language linkage declaration.
    Diag(Loc, diag::err_pragma_fc_pp_scope);
    return;
  }
  switch (Value) {
  default:
    llvm_unreachable("invalid pragma float_control kind");
  case PFC_Precise:
    NewFPFeatures.setFPPreciseEnabled(true);
    FpPragmaStack.Act(Loc, Action, StringRef(), NewFPFeatures);
    break;
  case PFC_NoPrecise:
    if (CurFPFeatures.getExceptionMode() == LangOptions::FPE_Strict)
      Diag(Loc, diag::err_pragma_fc_noprecise_requires_noexcept);
    else if (CurFPFeatures.getAllowFEnvAccess())
      Diag(Loc, diag::err_pragma_fc_noprecise_requires_nofenv);
    else
      NewFPFeatures.setFPPreciseEnabled(false);
    FpPragmaStack.Act(Loc, Action, StringRef(), NewFPFeatures);
    break;
  case PFC_Except:
    if (!isPreciseFPEnabled())
      Diag(Loc, diag::err_pragma_fc_except_requires_precise);
    else
      NewFPFeatures.setSpecifiedExceptionModeOverride(LangOptions::FPE_Strict);
    FpPragmaStack.Act(Loc, Action, StringRef(), NewFPFeatures);
    break;
  case PFC_NoExcept:
    NewFPFeatures.setSpecifiedExceptionModeOverride(LangOptions::FPE_Ignore);
    FpPragmaStack.Act(Loc, Action, StringRef(), NewFPFeatures);
    break;
  case PFC_Push:
    FpPragmaStack.Act(Loc, Sema::PSK_Push_Set, StringRef(), NewFPFeatures);
    break;
  case PFC_Pop:
    if (FpPragmaStack.Stack.empty()) {
      Diag(Loc, diag::warn_pragma_pop_failed) << "float_control"
                                              << "stack empty";
      return;
    }
    FpPragmaStack.Act(Loc, Action, StringRef(), NewFPFeatures);
    NewFPFeatures = FpPragmaStack.CurrentValue;
    break;
  }
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::ActOnPragmaMSPointersToMembers(
    LangOptions::PragmaMSPointersToMembersKind RepresentationMethod,
    SourceLocation PragmaLoc) {
  MSPointerToMemberRepresentationMethod = RepresentationMethod;
  ImplicitMSInheritanceAttrLoc = PragmaLoc;
}

void Sema::ActOnPragmaMSVtorDisp(PragmaMsStackAction Action,
                                 SourceLocation PragmaLoc,
                                 MSVtorDispMode Mode) {
  if (Action & PSK_Pop && VtorDispStack.Stack.empty())
    Diag(PragmaLoc, diag::warn_pragma_pop_failed) << "vtordisp"
                                                  << "stack empty";
  VtorDispStack.Act(PragmaLoc, Action, StringRef(), Mode);
}

template <>
void Sema::PragmaStack<Sema::AlignPackInfo>::Act(SourceLocation PragmaLocation,
                                                 PragmaMsStackAction Action,
                                                 llvm::StringRef StackSlotLabel,
                                                 AlignPackInfo Value) {
  if (Action == PSK_Reset) {
    CurrentValue = DefaultValue;
    CurrentPragmaLocation = PragmaLocation;
    return;
  }
  if (Action & PSK_Push)
    Stack.emplace_back(Slot(StackSlotLabel, CurrentValue, CurrentPragmaLocation,
                            PragmaLocation));
  else if (Action & PSK_Pop) {
    if (!StackSlotLabel.empty()) {
      // If we've got a label, try to find it and jump there.
      auto I = llvm::find_if(llvm::reverse(Stack), [&](const Slot &x) {
        return x.StackSlotLabel == StackSlotLabel;
      });
      // We found the label, so pop from there.
      if (I != Stack.rend()) {
        CurrentValue = I->Value;
        CurrentPragmaLocation = I->PragmaLocation;
        Stack.erase(std::prev(I.base()), Stack.end());
      }
    } else if (Value.IsXLStack() && Value.IsAlignAttr() &&
               CurrentValue.IsPackAttr()) {
      // XL '#pragma align(reset)' would pop the stack until
      // a current in effect pragma align is popped.
      auto I = llvm::find_if(llvm::reverse(Stack), [&](const Slot &x) {
        return x.Value.IsAlignAttr();
      });
      // If we found pragma align so pop from there.
      if (I != Stack.rend()) {
        Stack.erase(std::prev(I.base()), Stack.end());
        if (Stack.empty()) {
          CurrentValue = DefaultValue;
          CurrentPragmaLocation = PragmaLocation;
        } else {
          CurrentValue = Stack.back().Value;
          CurrentPragmaLocation = Stack.back().PragmaLocation;
          Stack.pop_back();
        }
      }
    } else if (!Stack.empty()) {
      // xl '#pragma align' sets the baseline, and `#pragma pack` cannot pop
      // over the baseline.
      if (Value.IsXLStack() && Value.IsPackAttr() && CurrentValue.IsAlignAttr())
        return;

      // We don't have a label, just pop the last entry.
      CurrentValue = Stack.back().Value;
      CurrentPragmaLocation = Stack.back().PragmaLocation;
      Stack.pop_back();
    }
  }
  if (Action & PSK_Set) {
    CurrentValue = Value;
    CurrentPragmaLocation = PragmaLocation;
  }
}

bool Sema::UnifySection(StringRef SectionName, int SectionFlags,
                        NamedDecl *Decl) {
  SourceLocation PragmaLocation;
  if (auto A = Decl->getAttr<SectionAttr>())
    if (A->isImplicit())
      PragmaLocation = A->getLocation();
  auto SectionIt = Context.SectionInfos.find(SectionName);
  if (SectionIt == Context.SectionInfos.end()) {
    Context.SectionInfos[SectionName] =
        ASTContext::SectionInfo(Decl, PragmaLocation, SectionFlags);
    return false;
  }
  // A pre-declared section takes precedence w/o diagnostic.
  const auto &Section = SectionIt->second;
  if (Section.SectionFlags == SectionFlags ||
      ((SectionFlags & ASTContext::PSF_Implicit) &&
       !(Section.SectionFlags & ASTContext::PSF_Implicit)))
    return false;
  Diag(Decl->getLocation(), diag::err_section_conflict) << Decl << Section;
  if (Section.Decl)
    Diag(Section.Decl->getLocation(), diag::note_declared_at)
        << Section.Decl->getName();
  if (PragmaLocation.isValid())
    Diag(PragmaLocation, diag::note_pragma_entered_here);
  if (Section.PragmaSectionLocation.isValid())
    Diag(Section.PragmaSectionLocation, diag::note_pragma_entered_here);
  return true;
}

bool Sema::UnifySection(StringRef SectionName,
                        int SectionFlags,
                        SourceLocation PragmaSectionLocation) {
  auto SectionIt = Context.SectionInfos.find(SectionName);
  if (SectionIt != Context.SectionInfos.end()) {
    const auto &Section = SectionIt->second;
    if (Section.SectionFlags == SectionFlags)
      return false;
    if (!(Section.SectionFlags & ASTContext::PSF_Implicit)) {
      Diag(PragmaSectionLocation, diag::err_section_conflict)
          << "this" << Section;
      if (Section.Decl)
        Diag(Section.Decl->getLocation(), diag::note_declared_at)
            << Section.Decl->getName();
      if (Section.PragmaSectionLocation.isValid())
        Diag(Section.PragmaSectionLocation, diag::note_pragma_entered_here);
      return true;
    }
  }
  Context.SectionInfos[SectionName] =
      ASTContext::SectionInfo(nullptr, PragmaSectionLocation, SectionFlags);
  return false;
}

/// Called on well formed \#pragma bss_seg().
void Sema::ActOnPragmaMSSeg(SourceLocation PragmaLocation,
                            PragmaMsStackAction Action,
                            llvm::StringRef StackSlotLabel,
                            StringLiteral *SegmentName,
                            llvm::StringRef PragmaName) {
  PragmaStack<StringLiteral *> *Stack =
    llvm::StringSwitch<PragmaStack<StringLiteral *> *>(PragmaName)
        .Case("data_seg", &DataSegStack)
        .Case("bss_seg", &BSSSegStack)
        .Case("const_seg", &ConstSegStack)
        .Case("code_seg", &CodeSegStack);
  if (Action & PSK_Pop && Stack->Stack.empty())
    Diag(PragmaLocation, diag::warn_pragma_pop_failed) << PragmaName
        << "stack empty";
  if (SegmentName) {
    if (!checkSectionName(SegmentName->getBeginLoc(), SegmentName->getString()))
      return;

    if (SegmentName->getString() == ".drectve" &&
        Context.getTargetInfo().getCXXABI().isMicrosoft())
      Diag(PragmaLocation, diag::warn_attribute_section_drectve) << PragmaName;
  }

  Stack->Act(PragmaLocation, Action, StackSlotLabel, SegmentName);
}

/// Called on well formed \#pragma strict_gs_check().
void Sema::ActOnPragmaMSStrictGuardStackCheck(SourceLocation PragmaLocation,
                                              PragmaMsStackAction Action,
                                              bool Value) {
  if (Action & PSK_Pop && StrictGuardStackCheckStack.Stack.empty())
    Diag(PragmaLocation, diag::warn_pragma_pop_failed) << "strict_gs_check"
                                                       << "stack empty";

  StrictGuardStackCheckStack.Act(PragmaLocation, Action, StringRef(), Value);
}

/// Called on well formed \#pragma bss_seg().
void Sema::ActOnPragmaMSSection(SourceLocation PragmaLocation,
                                int SectionFlags, StringLiteral *SegmentName) {
  UnifySection(SegmentName->getString(), SectionFlags, PragmaLocation);
}

void Sema::ActOnPragmaMSInitSeg(SourceLocation PragmaLocation,
                                StringLiteral *SegmentName) {
  // There's no stack to maintain, so we just have a current section.  When we
  // see the default section, reset our current section back to null so we stop
  // tacking on unnecessary attributes.
  CurInitSeg = SegmentName->getString() == ".CRT$XCU" ? nullptr : SegmentName;
  CurInitSegLoc = PragmaLocation;
}

void Sema::ActOnPragmaMSAllocText(
    SourceLocation PragmaLocation, StringRef Section,
    const SmallVector<std::tuple<IdentifierInfo *, SourceLocation>>
        &Functions) {
  if (!CurContext->getRedeclContext()->isFileContext()) {
    Diag(PragmaLocation, diag::err_pragma_expected_file_scope) << "alloc_text";
    return;
  }

  for (auto &Function : Functions) {
    IdentifierInfo *II;
    SourceLocation Loc;
    std::tie(II, Loc) = Function;

    DeclarationName DN(II);
    NamedDecl *ND = LookupSingleName(TUScope, DN, Loc, LookupOrdinaryName);
    if (!ND) {
      Diag(Loc, diag::err_undeclared_use) << II->getName();
      return;
    }

    auto *FD = dyn_cast<FunctionDecl>(ND->getCanonicalDecl());
    if (!FD) {
      Diag(Loc, diag::err_pragma_alloc_text_not_function);
      return;
    }

    if (getLangOpts().CPlusPlus && !FD->isInExternCContext()) {
      Diag(Loc, diag::err_pragma_alloc_text_c_linkage);
      return;
    }

    FunctionToSectionMap[II->getName()] = std::make_tuple(Section, Loc);
  }
}

void Sema::ActOnPragmaUnused(const Token &IdTok, Scope *curScope,
                             SourceLocation PragmaLoc) {

  IdentifierInfo *Name = IdTok.getIdentifierInfo();
  LookupResult Lookup(*this, Name, IdTok.getLocation(), LookupOrdinaryName);
  LookupName(Lookup, curScope, /*AllowBuiltinCreation=*/true);

  if (Lookup.empty()) {
    Diag(PragmaLoc, diag::warn_pragma_unused_undeclared_var)
      << Name << SourceRange(IdTok.getLocation());
    return;
  }

  VarDecl *VD = Lookup.getAsSingle<VarDecl>();
  if (!VD) {
    Diag(PragmaLoc, diag::warn_pragma_unused_expected_var_arg)
      << Name << SourceRange(IdTok.getLocation());
    return;
  }

  // Warn if this was used before being marked unused.
  if (VD->isUsed())
    Diag(PragmaLoc, diag::warn_used_but_marked_unused) << Name;

  VD->addAttr(UnusedAttr::CreateImplicit(Context, IdTok.getLocation(),
                                         UnusedAttr::GNU_unused));
}

namespace {

std::optional<attr::SubjectMatchRule>
getParentAttrMatcherRule(attr::SubjectMatchRule Rule) {
  using namespace attr;
  switch (Rule) {
  default:
    return std::nullopt;
#define ATTR_MATCH_RULE(Value, Spelling, IsAbstract)
#define ATTR_MATCH_SUB_RULE(Value, Spelling, IsAbstract, Parent, IsNegated)    \
  case Value:                                                                  \
    return Parent;
#include "clang/Basic/AttrSubMatchRulesList.inc"
  }
}

bool isNegatedAttrMatcherSubRule(attr::SubjectMatchRule Rule) {
  using namespace attr;
  switch (Rule) {
  default:
    return false;
#define ATTR_MATCH_RULE(Value, Spelling, IsAbstract)
#define ATTR_MATCH_SUB_RULE(Value, Spelling, IsAbstract, Parent, IsNegated)    \
  case Value:                                                                  \
    return IsNegated;
#include "clang/Basic/AttrSubMatchRulesList.inc"
  }
}

CharSourceRange replacementRangeForListElement(const Sema &S,
                                               SourceRange Range) {
  // Make sure that the ',' is removed as well.
  SourceLocation AfterCommaLoc = Lexer::findLocationAfterToken(
      Range.getEnd(), tok::comma, S.getSourceManager(), S.getLangOpts(),
      /*SkipTrailingWhitespaceAndNewLine=*/false);
  if (AfterCommaLoc.isValid())
    return CharSourceRange::getCharRange(Range.getBegin(), AfterCommaLoc);
  else
    return CharSourceRange::getTokenRange(Range);
}

std::string
attrMatcherRuleListToString(ArrayRef<attr::SubjectMatchRule> Rules) {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  for (const auto &I : llvm::enumerate(Rules)) {
    if (I.index())
      OS << (I.index() == Rules.size() - 1 ? ", and " : ", ");
    OS << "'" << attr::getSubjectMatchRuleSpelling(I.value()) << "'";
  }
  return Result;
}

} // end anonymous namespace

void Sema::ActOnPragmaAttributeAttribute(
    ParsedAttr &Attribute, SourceLocation PragmaLoc,
    attr::ParsedSubjectMatchRuleSet Rules) {
  Attribute.setIsPragmaClangAttribute();
  SmallVector<attr::SubjectMatchRule, 4> SubjectMatchRules;
  // Gather the subject match rules that are supported by the attribute.
  SmallVector<std::pair<attr::SubjectMatchRule, bool>, 4>
      StrictSubjectMatchRuleSet;
  Attribute.getMatchRules(LangOpts, StrictSubjectMatchRuleSet);

  // Figure out which subject matching rules are valid.
  if (StrictSubjectMatchRuleSet.empty()) {
    // Check for contradicting match rules. Contradicting match rules are
    // either:
    //  - a top-level rule and one of its sub-rules. E.g. variable and
    //    variable(is_parameter).
    //  - a sub-rule and a sibling that's negated. E.g.
    //    variable(is_thread_local) and variable(unless(is_parameter))
    llvm::SmallDenseMap<int, std::pair<int, SourceRange>, 2>
        RulesToFirstSpecifiedNegatedSubRule;
    for (const auto &Rule : Rules) {
      attr::SubjectMatchRule MatchRule = attr::SubjectMatchRule(Rule.first);
      std::optional<attr::SubjectMatchRule> ParentRule =
          getParentAttrMatcherRule(MatchRule);
      if (!ParentRule)
        continue;
      auto It = Rules.find(*ParentRule);
      if (It != Rules.end()) {
        // A sub-rule contradicts a parent rule.
        Diag(Rule.second.getBegin(),
             diag::err_pragma_attribute_matcher_subrule_contradicts_rule)
            << attr::getSubjectMatchRuleSpelling(MatchRule)
            << attr::getSubjectMatchRuleSpelling(*ParentRule) << It->second
            << FixItHint::CreateRemoval(
                   replacementRangeForListElement(*this, Rule.second));
        // Keep going without removing this rule as it won't change the set of
        // declarations that receive the attribute.
        continue;
      }
      if (isNegatedAttrMatcherSubRule(MatchRule))
        RulesToFirstSpecifiedNegatedSubRule.insert(
            std::make_pair(*ParentRule, Rule));
    }
    bool IgnoreNegatedSubRules = false;
    for (const auto &Rule : Rules) {
      attr::SubjectMatchRule MatchRule = attr::SubjectMatchRule(Rule.first);
      std::optional<attr::SubjectMatchRule> ParentRule =
          getParentAttrMatcherRule(MatchRule);
      if (!ParentRule)
        continue;
      auto It = RulesToFirstSpecifiedNegatedSubRule.find(*ParentRule);
      if (It != RulesToFirstSpecifiedNegatedSubRule.end() &&
          It->second != Rule) {
        // Negated sub-rule contradicts another sub-rule.
        Diag(
            It->second.second.getBegin(),
            diag::
                err_pragma_attribute_matcher_negated_subrule_contradicts_subrule)
            << attr::getSubjectMatchRuleSpelling(
                   attr::SubjectMatchRule(It->second.first))
            << attr::getSubjectMatchRuleSpelling(MatchRule) << Rule.second
            << FixItHint::CreateRemoval(
                   replacementRangeForListElement(*this, It->second.second));
        // Keep going but ignore all of the negated sub-rules.
        IgnoreNegatedSubRules = true;
        RulesToFirstSpecifiedNegatedSubRule.erase(It);
      }
    }

    if (!IgnoreNegatedSubRules) {
      for (const auto &Rule : Rules)
        SubjectMatchRules.push_back(attr::SubjectMatchRule(Rule.first));
    } else {
      for (const auto &Rule : Rules) {
        if (!isNegatedAttrMatcherSubRule(attr::SubjectMatchRule(Rule.first)))
          SubjectMatchRules.push_back(attr::SubjectMatchRule(Rule.first));
      }
    }
    Rules.clear();
  } else {
    // Each rule in Rules must be a strict subset of the attribute's
    // SubjectMatch rules.  I.e. we're allowed to use
    // `apply_to=variables(is_global)` on an attrubute with SubjectList<[Var]>,
    // but should not allow `apply_to=variables` on an attribute which has
    // `SubjectList<[GlobalVar]>`.
    for (const auto &StrictRule : StrictSubjectMatchRuleSet) {
      // First, check for exact match.
      if (Rules.erase(StrictRule.first)) {
        // Add the rule to the set of attribute receivers only if it's supported
        // in the current language mode.
        if (StrictRule.second)
          SubjectMatchRules.push_back(StrictRule.first);
      }
    }
    // Check remaining rules for subset matches.
    auto RulesToCheck = Rules;
    for (const auto &Rule : RulesToCheck) {
      attr::SubjectMatchRule MatchRule = attr::SubjectMatchRule(Rule.first);
      if (auto ParentRule = getParentAttrMatcherRule(MatchRule)) {
        if (llvm::any_of(StrictSubjectMatchRuleSet,
                         [ParentRule](const auto &StrictRule) {
                           return StrictRule.first == *ParentRule &&
                                  StrictRule.second; // IsEnabled
                         })) {
          SubjectMatchRules.push_back(MatchRule);
          Rules.erase(MatchRule);
        }
      }
    }
  }

  if (!Rules.empty()) {
    auto Diagnostic =
        Diag(PragmaLoc, diag::err_pragma_attribute_invalid_matchers)
        << Attribute;
    SmallVector<attr::SubjectMatchRule, 2> ExtraRules;
    for (const auto &Rule : Rules) {
      ExtraRules.push_back(attr::SubjectMatchRule(Rule.first));
      Diagnostic << FixItHint::CreateRemoval(
          replacementRangeForListElement(*this, Rule.second));
    }
    Diagnostic << attrMatcherRuleListToString(ExtraRules);
  }

  if (PragmaAttributeStack.empty()) {
    Diag(PragmaLoc, diag::err_pragma_attr_attr_no_push);
    return;
  }

  PragmaAttributeStack.back().Entries.push_back(
      {PragmaLoc, &Attribute, std::move(SubjectMatchRules), /*IsUsed=*/false});
}

void Sema::ActOnPragmaAttributeEmptyPush(SourceLocation PragmaLoc,
                                         const IdentifierInfo *Namespace) {
  PragmaAttributeStack.emplace_back();
  PragmaAttributeStack.back().Loc = PragmaLoc;
  PragmaAttributeStack.back().Namespace = Namespace;
}

void Sema::ActOnPragmaAttributePop(SourceLocation PragmaLoc,
                                   const IdentifierInfo *Namespace) {
  if (PragmaAttributeStack.empty()) {
    Diag(PragmaLoc, diag::err_pragma_attribute_stack_mismatch) << 1;
    return;
  }

  // Dig back through the stack trying to find the most recently pushed group
  // that in Namespace. Note that this works fine if no namespace is present,
  // think of push/pops without namespaces as having an implicit "nullptr"
  // namespace.
  for (size_t Index = PragmaAttributeStack.size(); Index;) {
    --Index;
    if (PragmaAttributeStack[Index].Namespace == Namespace) {
      for (const PragmaAttributeEntry &Entry :
           PragmaAttributeStack[Index].Entries) {
        if (!Entry.IsUsed) {
          assert(Entry.Attribute && "Expected an attribute");
          Diag(Entry.Attribute->getLoc(), diag::warn_pragma_attribute_unused)
              << *Entry.Attribute;
          Diag(PragmaLoc, diag::note_pragma_attribute_region_ends_here);
        }
      }
      PragmaAttributeStack.erase(PragmaAttributeStack.begin() + Index);
      return;
    }
  }

  if (Namespace)
    Diag(PragmaLoc, diag::err_pragma_attribute_stack_mismatch)
        << 0 << Namespace->getName();
  else
    Diag(PragmaLoc, diag::err_pragma_attribute_stack_mismatch) << 1;
}

void Sema::AddPragmaAttributes(Scope *S, Decl *D) {
  if (PragmaAttributeStack.empty())
    return;
  for (auto &Group : PragmaAttributeStack) {
    for (auto &Entry : Group.Entries) {
      ParsedAttr *Attribute = Entry.Attribute;
      assert(Attribute && "Expected an attribute");
      assert(Attribute->isPragmaClangAttribute() &&
             "expected #pragma clang attribute");

      // Ensure that the attribute can be applied to the given declaration.
      bool Applies = false;
      for (const auto &Rule : Entry.MatchRules) {
        if (Attribute->appliesToDecl(D, Rule)) {
          Applies = true;
          break;
        }
      }
      if (!Applies)
        continue;
      Entry.IsUsed = true;
      PragmaAttributeCurrentTargetDecl = D;
      ParsedAttributesView Attrs;
      Attrs.addAtEnd(Attribute);
      ProcessDeclAttributeList(S, D, Attrs);
      PragmaAttributeCurrentTargetDecl = nullptr;
    }
  }
}

void Sema::PrintPragmaAttributeInstantiationPoint() {
  assert(PragmaAttributeCurrentTargetDecl && "Expected an active declaration");
  Diags.Report(PragmaAttributeCurrentTargetDecl->getBeginLoc(),
               diag::note_pragma_attribute_applied_decl_here);
}

void Sema::DiagnoseUnterminatedPragmaAttribute() {
  if (PragmaAttributeStack.empty())
    return;
  Diag(PragmaAttributeStack.back().Loc, diag::err_pragma_attribute_no_pop_eof);
}

void Sema::ActOnPragmaOptimize(bool On, SourceLocation PragmaLoc) {
  if(On)
    OptimizeOffPragmaLocation = SourceLocation();
  else
    OptimizeOffPragmaLocation = PragmaLoc;
}

void Sema::ActOnPragmaMSOptimize(SourceLocation Loc, bool IsOn) {
  if (!CurContext->getRedeclContext()->isFileContext()) {
    Diag(Loc, diag::err_pragma_expected_file_scope) << "optimize";
    return;
  }

  MSPragmaOptimizeIsOn = IsOn;
}

void Sema::ActOnPragmaMSFunction(
    SourceLocation Loc, const llvm::SmallVectorImpl<StringRef> &NoBuiltins) {
  if (!CurContext->getRedeclContext()->isFileContext()) {
    Diag(Loc, diag::err_pragma_expected_file_scope) << "function";
    return;
  }

  MSFunctionNoBuiltins.insert(NoBuiltins.begin(), NoBuiltins.end());
}

void Sema::AddRangeBasedOptnone(FunctionDecl *FD) {
  // In the future, check other pragmas if they're implemented (e.g. pragma
  // optimize 0 will probably map to this functionality too).
  if(OptimizeOffPragmaLocation.isValid())
    AddOptnoneAttributeIfNoConflicts(FD, OptimizeOffPragmaLocation);
}

void Sema::AddSectionMSAllocText(FunctionDecl *FD) {
  if (!FD->getIdentifier())
    return;

  StringRef Name = FD->getName();
  auto It = FunctionToSectionMap.find(Name);
  if (It != FunctionToSectionMap.end()) {
    StringRef Section;
    SourceLocation Loc;
    std::tie(Section, Loc) = It->second;

    if (!FD->hasAttr<SectionAttr>())
      FD->addAttr(SectionAttr::CreateImplicit(Context, Section));
  }
}

void Sema::ModifyFnAttributesMSPragmaOptimize(FunctionDecl *FD) {
  // Don't modify the function attributes if it's "on". "on" resets the
  // optimizations to the ones listed on the command line
  if (!MSPragmaOptimizeIsOn)
    AddOptnoneAttributeIfNoConflicts(FD, FD->getBeginLoc());
}

void Sema::AddOptnoneAttributeIfNoConflicts(FunctionDecl *FD,
                                            SourceLocation Loc) {
  // Don't add a conflicting attribute. No diagnostic is needed.
  if (FD->hasAttr<MinSizeAttr>() || FD->hasAttr<AlwaysInlineAttr>())
    return;

  // Add attributes only if required. Optnone requires noinline as well, but if
  // either is already present then don't bother adding them.
  if (!FD->hasAttr<OptimizeNoneAttr>())
    FD->addAttr(OptimizeNoneAttr::CreateImplicit(Context, Loc));
  if (!FD->hasAttr<NoInlineAttr>())
    FD->addAttr(NoInlineAttr::CreateImplicit(Context, Loc));
}

void Sema::AddImplicitMSFunctionNoBuiltinAttr(FunctionDecl *FD) {
  SmallVector<StringRef> V(MSFunctionNoBuiltins.begin(),
                           MSFunctionNoBuiltins.end());
  if (!MSFunctionNoBuiltins.empty())
    FD->addAttr(NoBuiltinAttr::CreateImplicit(Context, V.data(), V.size()));
}

typedef std::vector<std::pair<unsigned, SourceLocation> > VisStack;
enum : unsigned { NoVisibility = ~0U };

void Sema::AddPushedVisibilityAttribute(Decl *D) {
  if (!VisContext)
    return;

  NamedDecl *ND = dyn_cast<NamedDecl>(D);
  if (ND && ND->getExplicitVisibility(NamedDecl::VisibilityForValue))
    return;

  VisStack *Stack = static_cast<VisStack*>(VisContext);
  unsigned rawType = Stack->back().first;
  if (rawType == NoVisibility) return;

  VisibilityAttr::VisibilityType type
    = (VisibilityAttr::VisibilityType) rawType;
  SourceLocation loc = Stack->back().second;

  D->addAttr(VisibilityAttr::CreateImplicit(Context, type, loc));
}

void Sema::FreeVisContext() {
  delete static_cast<VisStack*>(VisContext);
  VisContext = nullptr;
}

static void PushPragmaVisibility(Sema &S, unsigned type, SourceLocation loc) {
  // Put visibility on stack.
  if (!S.VisContext)
    S.VisContext = new VisStack;

  VisStack *Stack = static_cast<VisStack*>(S.VisContext);
  Stack->push_back(std::make_pair(type, loc));
}

void Sema::ActOnPragmaVisibility(const IdentifierInfo* VisType,
                                 SourceLocation PragmaLoc) {
  if (VisType) {
    // Compute visibility to use.
    VisibilityAttr::VisibilityType T;
    if (!VisibilityAttr::ConvertStrToVisibilityType(VisType->getName(), T)) {
      Diag(PragmaLoc, diag::warn_attribute_unknown_visibility) << VisType;
      return;
    }
    PushPragmaVisibility(*this, T, PragmaLoc);
  } else {
    PopPragmaVisibility(false, PragmaLoc);
  }
}

void Sema::ActOnPragmaFPContract(SourceLocation Loc,
                                 LangOptions::FPModeKind FPC) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  switch (FPC) {
  case LangOptions::FPM_On:
    NewFPFeatures.setAllowFPContractWithinStatement();
    break;
  case LangOptions::FPM_Fast:
    NewFPFeatures.setAllowFPContractAcrossStatement();
    break;
  case LangOptions::FPM_Off:
    NewFPFeatures.setDisallowFPContract();
    break;
  case LangOptions::FPM_FastHonorPragmas:
    llvm_unreachable("Should not happen");
  }
  FpPragmaStack.Act(Loc, Sema::PSK_Set, StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::ActOnPragmaFPValueChangingOption(SourceLocation Loc,
                                            PragmaFPKind Kind, bool IsEnabled) {
  if (IsEnabled) {
    // For value unsafe context, combining this pragma with eval method
    // setting is not recommended. See comment in function FixupInvocation#506.
    int Reason = -1;
    if (getLangOpts().getFPEvalMethod() != LangOptions::FEM_UnsetOnCommandLine)
      // Eval method set using the option 'ffp-eval-method'.
      Reason = 1;
    if (PP.getLastFPEvalPragmaLocation().isValid())
      // Eval method set using the '#pragma clang fp eval_method'.
      // We could have both an option and a pragma used to the set the eval
      // method. The pragma overrides the option in the command line. The Reason
      // of the diagnostic is overriden too.
      Reason = 0;
    if (Reason != -1)
      Diag(Loc, diag::err_setting_eval_method_used_in_unsafe_context)
          << Reason << (Kind == PFK_Reassociate ? 4 : 5);
  }

  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  switch (Kind) {
  case PFK_Reassociate:
    NewFPFeatures.setAllowFPReassociateOverride(IsEnabled);
    break;
  case PFK_Reciprocal:
    NewFPFeatures.setAllowReciprocalOverride(IsEnabled);
    break;
  default:
    llvm_unreachable("unhandled value changing pragma fp");
  }

  FpPragmaStack.Act(Loc, PSK_Set, StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::ActOnPragmaFEnvRound(SourceLocation Loc, llvm::RoundingMode FPR) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  NewFPFeatures.setConstRoundingModeOverride(FPR);
  FpPragmaStack.Act(Loc, PSK_Set, StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::setExceptionMode(SourceLocation Loc,
                            LangOptions::FPExceptionModeKind FPE) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  NewFPFeatures.setSpecifiedExceptionModeOverride(FPE);
  FpPragmaStack.Act(Loc, PSK_Set, StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::ActOnPragmaFEnvAccess(SourceLocation Loc, bool IsEnabled) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  if (IsEnabled) {
    // Verify Microsoft restriction:
    // You can't enable fenv_access unless precise semantics are enabled.
    // Precise semantics can be enabled either by the float_control
    // pragma, or by using the /fp:precise or /fp:strict compiler options
    if (!isPreciseFPEnabled())
      Diag(Loc, diag::err_pragma_fenv_requires_precise);
  }
  NewFPFeatures.setAllowFEnvAccessOverride(IsEnabled);
  NewFPFeatures.setRoundingMathOverride(IsEnabled);
  FpPragmaStack.Act(Loc, PSK_Set, StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::ActOnPragmaCXLimitedRange(SourceLocation Loc,
                                     LangOptions::ComplexRangeKind Range) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  NewFPFeatures.setComplexRangeOverride(Range);
  FpPragmaStack.Act(Loc, PSK_Set, StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::ActOnPragmaFPExceptions(SourceLocation Loc,
                                   LangOptions::FPExceptionModeKind FPE) {
  setExceptionMode(Loc, FPE);
}

void Sema::PushNamespaceVisibilityAttr(const VisibilityAttr *Attr,
                                       SourceLocation Loc) {
  // Visibility calculations will consider the namespace's visibility.
  // Here we just want to note that we're in a visibility context
  // which overrides any enclosing #pragma context, but doesn't itself
  // contribute visibility.
  PushPragmaVisibility(*this, NoVisibility, Loc);
}

void Sema::PopPragmaVisibility(bool IsNamespaceEnd, SourceLocation EndLoc) {
  if (!VisContext) {
    Diag(EndLoc, diag::err_pragma_pop_visibility_mismatch);
    return;
  }

  // Pop visibility from stack
  VisStack *Stack = static_cast<VisStack*>(VisContext);

  const std::pair<unsigned, SourceLocation> *Back = &Stack->back();
  bool StartsWithPragma = Back->first != NoVisibility;
  if (StartsWithPragma && IsNamespaceEnd) {
    Diag(Back->second, diag::err_pragma_push_visibility_mismatch);
    Diag(EndLoc, diag::note_surrounding_namespace_ends_here);

    // For better error recovery, eat all pushes inside the namespace.
    do {
      Stack->pop_back();
      Back = &Stack->back();
      StartsWithPragma = Back->first != NoVisibility;
    } while (StartsWithPragma);
  } else if (!StartsWithPragma && !IsNamespaceEnd) {
    Diag(EndLoc, diag::err_pragma_pop_visibility_mismatch);
    Diag(Back->second, diag::note_surrounding_namespace_starts_here);
    return;
  }

  Stack->pop_back();
  // To simplify the implementation, never keep around an empty stack.
  if (Stack->empty())
    FreeVisContext();
}

template <typename Ty>
static bool checkCommonAttributeFeatures(Sema &S, const Ty *Node,
                                         const ParsedAttr &A,
                                         bool SkipArgCountCheck) {
  // Several attributes carry different semantics than the parsing requires, so
  // those are opted out of the common argument checks.
  //
  // We also bail on unknown and ignored attributes because those are handled
  // as part of the target-specific handling logic.
  if (A.getKind() == ParsedAttr::UnknownAttribute)
    return false;
  // Check whether the attribute requires specific language extensions to be
  // enabled.
  if (!A.diagnoseLangOpts(S))
    return true;
  // Check whether the attribute appertains to the given subject.
  if (!A.diagnoseAppertainsTo(S, Node))
    return true;
  // Check whether the attribute is mutually exclusive with other attributes
  // that have already been applied to the declaration.
  if (!A.diagnoseMutualExclusion(S, Node))
    return true;
  // Check whether the attribute exists in the target architecture.
  if (S.CheckAttrTarget(A))
    return true;

  if (A.hasCustomParsing())
    return false;

  if (!SkipArgCountCheck) {
    if (A.getMinArgs() == A.getMaxArgs()) {
      // If there are no optional arguments, then checking for the argument
      // count is trivial.
      if (!A.checkExactlyNumArgs(S, A.getMinArgs()))
        return true;
    } else {
      // There are optional arguments, so checking is slightly more involved.
      if (A.getMinArgs() && !A.checkAtLeastNumArgs(S, A.getMinArgs()))
        return true;
      else if (!A.hasVariadicArg() && A.getMaxArgs() &&
               !A.checkAtMostNumArgs(S, A.getMaxArgs()))
        return true;
    }
  }

  return false;
}

bool Sema::checkCommonAttributeFeatures(const Decl *D, const ParsedAttr &A,
                                        bool SkipArgCountCheck) {
  return ::checkCommonAttributeFeatures(*this, D, A, SkipArgCountCheck);
}
bool Sema::checkCommonAttributeFeatures(const Stmt *S, const ParsedAttr &A,
                                        bool SkipArgCountCheck) {
  return ::checkCommonAttributeFeatures(*this, S, A, SkipArgCountCheck);
}
