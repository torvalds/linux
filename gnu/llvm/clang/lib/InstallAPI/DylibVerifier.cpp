//===- DylibVerifier.cpp ----------------------------------------*- C++--*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/InstallAPI/DylibVerifier.h"
#include "DiagnosticBuilderWrappers.h"
#include "clang/InstallAPI/FrontendRecords.h"
#include "clang/InstallAPI/InstallAPIDiagnostic.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/TextAPI/DylibReader.h"

using namespace llvm::MachO;

namespace clang {
namespace installapi {

/// Metadata stored about a mapping of a declaration to a symbol.
struct DylibVerifier::SymbolContext {
  // Name to use for all querying and verification
  // purposes.
  std::string SymbolName{""};

  // Kind to map symbol type against record.
  EncodeKind Kind = EncodeKind::GlobalSymbol;

  // Frontend Attributes tied to the AST.
  const FrontendAttrs *FA = nullptr;

  // The ObjCInterface symbol type, if applicable.
  ObjCIFSymbolKind ObjCIFKind = ObjCIFSymbolKind::None;

  // Whether Decl is inlined.
  bool Inlined = false;
};

struct DylibVerifier::DWARFContext {
  // Track whether DSYM parsing has already been attempted to avoid re-parsing.
  bool ParsedDSYM{false};

  // Lookup table for source locations by symbol name.
  DylibReader::SymbolToSourceLocMap SourceLocs{};
};

static bool isCppMangled(StringRef Name) {
  // InstallAPI currently only supports itanium manglings.
  return (Name.starts_with("_Z") || Name.starts_with("__Z") ||
          Name.starts_with("___Z"));
}

static std::string demangle(StringRef Name) {
  // InstallAPI currently only supports itanium manglings.
  if (!isCppMangled(Name))
    return Name.str();
  char *Result = llvm::itaniumDemangle(Name);
  if (!Result)
    return Name.str();

  std::string Demangled(Result);
  free(Result);
  return Demangled;
}

std::string DylibVerifier::getAnnotatedName(const Record *R,
                                            SymbolContext &SymCtx,
                                            bool ValidSourceLoc) {
  assert(!SymCtx.SymbolName.empty() && "Expected symbol name");

  const StringRef SymbolName = SymCtx.SymbolName;
  std::string PrettyName =
      (Demangle && (SymCtx.Kind == EncodeKind::GlobalSymbol))
          ? demangle(SymbolName)
          : SymbolName.str();

  std::string Annotation;
  if (R->isWeakDefined())
    Annotation += "(weak-def) ";
  if (R->isWeakReferenced())
    Annotation += "(weak-ref) ";
  if (R->isThreadLocalValue())
    Annotation += "(tlv) ";

  // Check if symbol represents only part of a @interface declaration.
  switch (SymCtx.ObjCIFKind) {
  default:
    break;
  case ObjCIFSymbolKind::EHType:
    return Annotation + "Exception Type of " + PrettyName;
  case ObjCIFSymbolKind::MetaClass:
    return Annotation + "Metaclass of " + PrettyName;
  case ObjCIFSymbolKind::Class:
    return Annotation + "Class of " + PrettyName;
  }

  // Only print symbol type prefix or leading "_" if there is no source location
  // tied to it. This can only ever happen when the location has to come from
  // debug info.
  if (ValidSourceLoc) {
    StringRef PrettyNameRef(PrettyName);
    if ((SymCtx.Kind == EncodeKind::GlobalSymbol) &&
        !isCppMangled(SymbolName) && PrettyNameRef.starts_with("_"))
      return Annotation + PrettyNameRef.drop_front(1).str();
    return Annotation + PrettyName;
  }

  switch (SymCtx.Kind) {
  case EncodeKind::GlobalSymbol:
    return Annotation + PrettyName;
  case EncodeKind::ObjectiveCInstanceVariable:
    return Annotation + "(ObjC IVar) " + PrettyName;
  case EncodeKind::ObjectiveCClass:
    return Annotation + "(ObjC Class) " + PrettyName;
  case EncodeKind::ObjectiveCClassEHType:
    return Annotation + "(ObjC Class EH) " + PrettyName;
  }

  llvm_unreachable("unexpected case for EncodeKind");
}

static DylibVerifier::Result updateResult(const DylibVerifier::Result Prev,
                                          const DylibVerifier::Result Curr) {
  if (Prev == Curr)
    return Prev;

  // Never update from invalid or noverify state.
  if ((Prev == DylibVerifier::Result::Invalid) ||
      (Prev == DylibVerifier::Result::NoVerify))
    return Prev;

  // Don't let an ignored verification remove a valid one.
  if (Prev == DylibVerifier::Result::Valid &&
      Curr == DylibVerifier::Result::Ignore)
    return Prev;

  return Curr;
}
// __private_extern__ is a deprecated specifier that clang does not
// respect in all contexts, it should just be considered hidden for InstallAPI.
static bool shouldIgnorePrivateExternAttr(const Decl *D) {
  if (const FunctionDecl *FD = cast<FunctionDecl>(D))
    return FD->getStorageClass() == StorageClass::SC_PrivateExtern;
  if (const VarDecl *VD = cast<VarDecl>(D))
    return VD->getStorageClass() == StorageClass::SC_PrivateExtern;

  return false;
}

Record *findRecordFromSlice(const RecordsSlice *Slice, StringRef Name,
                            EncodeKind Kind) {
  switch (Kind) {
  case EncodeKind::GlobalSymbol:
    return Slice->findGlobal(Name);
  case EncodeKind::ObjectiveCInstanceVariable:
    return Slice->findObjCIVar(Name.contains('.'), Name);
  case EncodeKind::ObjectiveCClass:
  case EncodeKind::ObjectiveCClassEHType:
    return Slice->findObjCInterface(Name);
  }
  llvm_unreachable("unexpected end when finding record");
}

void DylibVerifier::updateState(Result State) {
  Ctx.FrontendState = updateResult(Ctx.FrontendState, State);
}

void DylibVerifier::addSymbol(const Record *R, SymbolContext &SymCtx,
                              TargetList &&Targets) {
  if (Targets.empty())
    Targets = {Ctx.Target};

  Exports->addGlobal(SymCtx.Kind, SymCtx.SymbolName, R->getFlags(), Targets);
}

bool DylibVerifier::shouldIgnoreObsolete(const Record *R, SymbolContext &SymCtx,
                                         const Record *DR) {
  if (!SymCtx.FA->Avail.isObsoleted())
    return false;

  if (Zippered)
    DeferredZipperedSymbols[SymCtx.SymbolName].emplace_back(ZipperedDeclSource{
        SymCtx.FA, &Ctx.Diag->getSourceManager(), Ctx.Target});
  return true;
}

bool DylibVerifier::shouldIgnoreReexport(const Record *R,
                                         SymbolContext &SymCtx) const {
  StringRef SymName = SymCtx.SymbolName;
  // Linker directive symbols can never be ignored.
  if (SymName.starts_with("$ld$"))
    return false;

  if (Reexports.empty())
    return false;

  for (const InterfaceFile &Lib : Reexports) {
    if (!Lib.hasTarget(Ctx.Target))
      continue;
    if (auto Sym = Lib.getSymbol(SymCtx.Kind, SymName, SymCtx.ObjCIFKind))
      if ((*Sym)->hasTarget(Ctx.Target))
        return true;
  }
  return false;
}

bool DylibVerifier::shouldIgnoreInternalZipperedSymbol(
    const Record *R, const SymbolContext &SymCtx) const {
  if (!Zippered)
    return false;

  return Exports->findSymbol(SymCtx.Kind, SymCtx.SymbolName,
                             SymCtx.ObjCIFKind) != nullptr;
}

bool DylibVerifier::shouldIgnoreZipperedAvailability(const Record *R,
                                                     SymbolContext &SymCtx) {
  if (!(Zippered && SymCtx.FA->Avail.isUnavailable()))
    return false;

  // Collect source location incase there is an exported symbol to diagnose
  // during `verifyRemainingSymbols`.
  DeferredZipperedSymbols[SymCtx.SymbolName].emplace_back(
      ZipperedDeclSource{SymCtx.FA, SourceManagers.back().get(), Ctx.Target});

  return true;
}

bool DylibVerifier::compareObjCInterfaceSymbols(const Record *R,
                                                SymbolContext &SymCtx,
                                                const ObjCInterfaceRecord *DR) {
  const bool IsDeclVersionComplete =
      ((SymCtx.ObjCIFKind & ObjCIFSymbolKind::Class) ==
       ObjCIFSymbolKind::Class) &&
      ((SymCtx.ObjCIFKind & ObjCIFSymbolKind::MetaClass) ==
       ObjCIFSymbolKind::MetaClass);

  const bool IsDylibVersionComplete = DR->isCompleteInterface();

  // The common case, a complete ObjCInterface.
  if (IsDeclVersionComplete && IsDylibVersionComplete)
    return true;

  auto PrintDiagnostic = [&](auto SymLinkage, const Record *Record,
                             StringRef SymName, bool PrintAsWarning = false) {
    if (SymLinkage == RecordLinkage::Unknown)
      Ctx.emitDiag([&]() {
        Ctx.Diag->Report(SymCtx.FA->Loc, PrintAsWarning
                                             ? diag::warn_library_missing_symbol
                                             : diag::err_library_missing_symbol)
            << SymName;
      });
    else
      Ctx.emitDiag([&]() {
        Ctx.Diag->Report(SymCtx.FA->Loc, PrintAsWarning
                                             ? diag::warn_library_hidden_symbol
                                             : diag::err_library_hidden_symbol)
            << SymName;
      });
  };

  if (IsDeclVersionComplete) {
    // The decl represents a complete ObjCInterface, but the symbols in the
    // dylib do not. Determine which symbol is missing. To keep older projects
    // building, treat this as a warning.
    if (!DR->isExportedSymbol(ObjCIFSymbolKind::Class)) {
      SymCtx.ObjCIFKind = ObjCIFSymbolKind::Class;
      PrintDiagnostic(DR->getLinkageForSymbol(ObjCIFSymbolKind::Class), R,
                      getAnnotatedName(R, SymCtx),
                      /*PrintAsWarning=*/true);
    }
    if (!DR->isExportedSymbol(ObjCIFSymbolKind::MetaClass)) {
      SymCtx.ObjCIFKind = ObjCIFSymbolKind::MetaClass;
      PrintDiagnostic(DR->getLinkageForSymbol(ObjCIFSymbolKind::MetaClass), R,
                      getAnnotatedName(R, SymCtx),
                      /*PrintAsWarning=*/true);
    }
    return true;
  }

  if (DR->isExportedSymbol(SymCtx.ObjCIFKind)) {
    if (!IsDylibVersionComplete) {
      // Both the declaration and dylib have a non-complete interface.
      SymCtx.Kind = EncodeKind::GlobalSymbol;
      SymCtx.SymbolName = R->getName();
    }
    return true;
  }

  // At this point that means there was not a matching class symbol
  // to represent the one discovered as a declaration.
  PrintDiagnostic(DR->getLinkageForSymbol(SymCtx.ObjCIFKind), R,
                  SymCtx.SymbolName);
  return false;
}

DylibVerifier::Result DylibVerifier::compareVisibility(const Record *R,
                                                       SymbolContext &SymCtx,
                                                       const Record *DR) {

  if (R->isExported()) {
    if (!DR) {
      Ctx.emitDiag([&]() {
        Ctx.Diag->Report(SymCtx.FA->Loc, diag::err_library_missing_symbol)
            << getAnnotatedName(R, SymCtx);
      });
      return Result::Invalid;
    }
    if (DR->isInternal()) {
      Ctx.emitDiag([&]() {
        Ctx.Diag->Report(SymCtx.FA->Loc, diag::err_library_hidden_symbol)
            << getAnnotatedName(R, SymCtx);
      });
      return Result::Invalid;
    }
  }

  // Emit a diagnostic for hidden declarations with external symbols, except
  // when theres an inlined attribute.
  if ((R->isInternal() && !SymCtx.Inlined) && DR && DR->isExported()) {

    if (Mode == VerificationMode::ErrorsOnly)
      return Result::Ignore;

    if (shouldIgnorePrivateExternAttr(SymCtx.FA->D))
      return Result::Ignore;

    if (shouldIgnoreInternalZipperedSymbol(R, SymCtx))
      return Result::Ignore;

    unsigned ID;
    Result Outcome;
    if (Mode == VerificationMode::ErrorsAndWarnings) {
      ID = diag::warn_header_hidden_symbol;
      Outcome = Result::Ignore;
    } else {
      ID = diag::err_header_hidden_symbol;
      Outcome = Result::Invalid;
    }
    Ctx.emitDiag([&]() {
      Ctx.Diag->Report(SymCtx.FA->Loc, ID) << getAnnotatedName(R, SymCtx);
    });
    return Outcome;
  }

  if (R->isInternal())
    return Result::Ignore;

  return Result::Valid;
}

DylibVerifier::Result DylibVerifier::compareAvailability(const Record *R,
                                                         SymbolContext &SymCtx,
                                                         const Record *DR) {
  if (!SymCtx.FA->Avail.isUnavailable())
    return Result::Valid;

  if (shouldIgnoreZipperedAvailability(R, SymCtx))
    return Result::Ignore;

  const bool IsDeclAvailable = SymCtx.FA->Avail.isUnavailable();

  switch (Mode) {
  case VerificationMode::ErrorsAndWarnings:
    Ctx.emitDiag([&]() {
      Ctx.Diag->Report(SymCtx.FA->Loc, diag::warn_header_availability_mismatch)
          << getAnnotatedName(R, SymCtx) << IsDeclAvailable << IsDeclAvailable;
    });
    return Result::Ignore;
  case VerificationMode::Pedantic:
    Ctx.emitDiag([&]() {
      Ctx.Diag->Report(SymCtx.FA->Loc, diag::err_header_availability_mismatch)
          << getAnnotatedName(R, SymCtx) << IsDeclAvailable << IsDeclAvailable;
    });
    return Result::Invalid;
  case VerificationMode::ErrorsOnly:
    return Result::Ignore;
  case VerificationMode::Invalid:
    llvm_unreachable("Unexpected verification mode symbol verification");
  }
  llvm_unreachable("Unexpected verification mode symbol verification");
}

bool DylibVerifier::compareSymbolFlags(const Record *R, SymbolContext &SymCtx,
                                       const Record *DR) {
  if (DR->isThreadLocalValue() && !R->isThreadLocalValue()) {
    Ctx.emitDiag([&]() {
      Ctx.Diag->Report(SymCtx.FA->Loc, diag::err_dylib_symbol_flags_mismatch)
          << getAnnotatedName(DR, SymCtx) << DR->isThreadLocalValue();
    });
    return false;
  }
  if (!DR->isThreadLocalValue() && R->isThreadLocalValue()) {
    Ctx.emitDiag([&]() {
      Ctx.Diag->Report(SymCtx.FA->Loc, diag::err_header_symbol_flags_mismatch)
          << getAnnotatedName(R, SymCtx) << R->isThreadLocalValue();
    });
    return false;
  }

  if (DR->isWeakDefined() && !R->isWeakDefined()) {
    Ctx.emitDiag([&]() {
      Ctx.Diag->Report(SymCtx.FA->Loc, diag::err_dylib_symbol_flags_mismatch)
          << getAnnotatedName(DR, SymCtx) << R->isWeakDefined();
    });
    return false;
  }
  if (!DR->isWeakDefined() && R->isWeakDefined()) {
    Ctx.emitDiag([&]() {
      Ctx.Diag->Report(SymCtx.FA->Loc, diag::err_header_symbol_flags_mismatch)
          << getAnnotatedName(R, SymCtx) << R->isWeakDefined();
    });
    return false;
  }

  return true;
}

DylibVerifier::Result DylibVerifier::verifyImpl(Record *R,
                                                SymbolContext &SymCtx) {
  R->setVerify();
  if (!canVerify()) {
    // Accumulate symbols when not in verifying against dylib.
    if (R->isExported() && !SymCtx.FA->Avail.isUnavailable() &&
        !SymCtx.FA->Avail.isObsoleted()) {
      addSymbol(R, SymCtx);
    }
    return Ctx.FrontendState;
  }

  if (shouldIgnoreReexport(R, SymCtx)) {
    updateState(Result::Ignore);
    return Ctx.FrontendState;
  }

  Record *DR =
      findRecordFromSlice(Ctx.DylibSlice, SymCtx.SymbolName, SymCtx.Kind);
  if (DR)
    DR->setVerify();

  if (shouldIgnoreObsolete(R, SymCtx, DR)) {
    updateState(Result::Ignore);
    return Ctx.FrontendState;
  }

  // Unavailable declarations don't need matching symbols.
  if (SymCtx.FA->Avail.isUnavailable() && (!DR || DR->isInternal())) {
    updateState(Result::Valid);
    return Ctx.FrontendState;
  }

  Result VisibilityCheck = compareVisibility(R, SymCtx, DR);
  if (VisibilityCheck != Result::Valid) {
    updateState(VisibilityCheck);
    return Ctx.FrontendState;
  }

  // All missing symbol cases to diagnose have been handled now.
  if (!DR) {
    updateState(Result::Ignore);
    return Ctx.FrontendState;
  }

  // Check for mismatching ObjC interfaces.
  if (SymCtx.ObjCIFKind != ObjCIFSymbolKind::None) {
    if (!compareObjCInterfaceSymbols(
            R, SymCtx, Ctx.DylibSlice->findObjCInterface(DR->getName()))) {
      updateState(Result::Invalid);
      return Ctx.FrontendState;
    }
  }

  Result AvailabilityCheck = compareAvailability(R, SymCtx, DR);
  if (AvailabilityCheck != Result::Valid) {
    updateState(AvailabilityCheck);
    return Ctx.FrontendState;
  }

  if (!compareSymbolFlags(R, SymCtx, DR)) {
    updateState(Result::Invalid);
    return Ctx.FrontendState;
  }

  addSymbol(R, SymCtx);
  updateState(Result::Valid);
  return Ctx.FrontendState;
}

bool DylibVerifier::canVerify() {
  return Ctx.FrontendState != Result::NoVerify;
}

void DylibVerifier::assignSlice(const Target &T) {
  assert(T == Ctx.Target && "Active targets should match.");
  if (Dylib.empty())
    return;

  // Note: there are no reexport slices with binaries, as opposed to TBD files,
  // so it can be assumed that the target match is the active top-level library.
  auto It = find_if(
      Dylib, [&T](const auto &Slice) { return T == Slice->getTarget(); });

  assert(It != Dylib.end() && "Target slice should always exist.");
  Ctx.DylibSlice = It->get();
}

void DylibVerifier::setTarget(const Target &T) {
  Ctx.Target = T;
  Ctx.DiscoveredFirstError = false;
  if (Dylib.empty()) {
    updateState(Result::NoVerify);
    return;
  }
  updateState(Result::Ignore);
  assignSlice(T);
}

void DylibVerifier::setSourceManager(
    IntrusiveRefCntPtr<SourceManager> SourceMgr) {
  if (!Ctx.Diag)
    return;
  SourceManagers.push_back(std::move(SourceMgr));
  Ctx.Diag->setSourceManager(SourceManagers.back().get());
}

DylibVerifier::Result DylibVerifier::verify(ObjCIVarRecord *R,
                                            const FrontendAttrs *FA,
                                            const StringRef SuperClass) {
  if (R->isVerified())
    return getState();

  std::string FullName =
      ObjCIVarRecord::createScopedName(SuperClass, R->getName());
  SymbolContext SymCtx{FullName, EncodeKind::ObjectiveCInstanceVariable, FA};
  return verifyImpl(R, SymCtx);
}

static ObjCIFSymbolKind assignObjCIFSymbolKind(const ObjCInterfaceRecord *R) {
  ObjCIFSymbolKind Result = ObjCIFSymbolKind::None;
  if (R->getLinkageForSymbol(ObjCIFSymbolKind::Class) != RecordLinkage::Unknown)
    Result |= ObjCIFSymbolKind::Class;
  if (R->getLinkageForSymbol(ObjCIFSymbolKind::MetaClass) !=
      RecordLinkage::Unknown)
    Result |= ObjCIFSymbolKind::MetaClass;
  if (R->getLinkageForSymbol(ObjCIFSymbolKind::EHType) !=
      RecordLinkage::Unknown)
    Result |= ObjCIFSymbolKind::EHType;
  return Result;
}

DylibVerifier::Result DylibVerifier::verify(ObjCInterfaceRecord *R,
                                            const FrontendAttrs *FA) {
  if (R->isVerified())
    return getState();
  SymbolContext SymCtx;
  SymCtx.SymbolName = R->getName();
  SymCtx.ObjCIFKind = assignObjCIFSymbolKind(R);

  SymCtx.Kind = R->hasExceptionAttribute() ? EncodeKind::ObjectiveCClassEHType
                                           : EncodeKind::ObjectiveCClass;
  SymCtx.FA = FA;

  return verifyImpl(R, SymCtx);
}

DylibVerifier::Result DylibVerifier::verify(GlobalRecord *R,
                                            const FrontendAttrs *FA) {
  if (R->isVerified())
    return getState();

  // Global classifications could be obfusciated with `asm`.
  SimpleSymbol Sym = parseSymbol(R->getName());
  SymbolContext SymCtx;
  SymCtx.SymbolName = Sym.Name;
  SymCtx.Kind = Sym.Kind;
  SymCtx.FA = FA;
  SymCtx.Inlined = R->isInlined();
  return verifyImpl(R, SymCtx);
}

void DylibVerifier::VerifierContext::emitDiag(llvm::function_ref<void()> Report,
                                              RecordLoc *Loc) {
  if (!DiscoveredFirstError) {
    Diag->Report(diag::warn_target)
        << (PrintArch ? getArchitectureName(Target.Arch)
                      : getTargetTripleName(Target));
    DiscoveredFirstError = true;
  }
  if (Loc && Loc->isValid())
    llvm::errs() << Loc->File << ":" << Loc->Line << ":" << 0 << ": ";

  Report();
}

// The existence of weak-defined RTTI can not always be inferred from the
// header files because they can be generated as part of an implementation
// file.
// InstallAPI doesn't warn about weak-defined RTTI, because this doesn't affect
// static linking and so can be ignored for text-api files.
static bool shouldIgnoreCpp(StringRef Name, bool IsWeakDef) {
  return (IsWeakDef &&
          (Name.starts_with("__ZTI") || Name.starts_with("__ZTS")));
}
void DylibVerifier::visitSymbolInDylib(const Record &R, SymbolContext &SymCtx) {
  // Undefined symbols should not be in InstallAPI generated text-api files.
  if (R.isUndefined()) {
    updateState(Result::Valid);
    return;
  }

  // Internal symbols should not be in InstallAPI generated text-api files.
  if (R.isInternal()) {
    updateState(Result::Valid);
    return;
  }

  // Allow zippered symbols with potentially mismatching availability
  // between macOS and macCatalyst in the final text-api file.
  const StringRef SymbolName(SymCtx.SymbolName);
  if (const Symbol *Sym = Exports->findSymbol(SymCtx.Kind, SymCtx.SymbolName,
                                              SymCtx.ObjCIFKind)) {
    if (Sym->hasArchitecture(Ctx.Target.Arch)) {
      updateState(Result::Ignore);
      return;
    }
  }

  const bool IsLinkerSymbol = SymbolName.starts_with("$ld$");

  if (R.isVerified()) {
    // Check for unavailable symbols.
    // This should only occur in the zippered case where we ignored
    // availability until all headers have been parsed.
    auto It = DeferredZipperedSymbols.find(SymCtx.SymbolName);
    if (It == DeferredZipperedSymbols.end()) {
      updateState(Result::Valid);
      return;
    }

    ZipperedDeclSources Locs;
    for (const ZipperedDeclSource &ZSource : It->second) {
      if (ZSource.FA->Avail.isObsoleted()) {
        updateState(Result::Ignore);
        return;
      }
      if (ZSource.T.Arch != Ctx.Target.Arch)
        continue;
      Locs.emplace_back(ZSource);
    }
    assert(Locs.size() == 2 && "Expected two decls for zippered symbol");

    // Print violating declarations per platform.
    for (const ZipperedDeclSource &ZSource : Locs) {
      unsigned DiagID = 0;
      if (Mode == VerificationMode::Pedantic || IsLinkerSymbol) {
        updateState(Result::Invalid);
        DiagID = diag::err_header_availability_mismatch;
      } else if (Mode == VerificationMode::ErrorsAndWarnings) {
        updateState(Result::Ignore);
        DiagID = diag::warn_header_availability_mismatch;
      } else {
        updateState(Result::Ignore);
        return;
      }
      // Bypass emitDiag banner and print the target everytime.
      Ctx.Diag->setSourceManager(ZSource.SrcMgr);
      Ctx.Diag->Report(diag::warn_target) << getTargetTripleName(ZSource.T);
      Ctx.Diag->Report(ZSource.FA->Loc, DiagID)
          << getAnnotatedName(&R, SymCtx) << ZSource.FA->Avail.isUnavailable()
          << ZSource.FA->Avail.isUnavailable();
    }
    return;
  }

  if (shouldIgnoreCpp(SymbolName, R.isWeakDefined())) {
    updateState(Result::Valid);
    return;
  }

  if (Aliases.count({SymbolName.str(), SymCtx.Kind})) {
    updateState(Result::Valid);
    return;
  }

  // All checks at this point classify as some kind of violation.
  // The different verification modes dictate whether they are reported to the
  // user.
  if (IsLinkerSymbol || (Mode > VerificationMode::ErrorsOnly))
    accumulateSrcLocForDylibSymbols();
  RecordLoc Loc = DWARFCtx->SourceLocs.lookup(SymCtx.SymbolName);

  // Regardless of verification mode, error out on mismatched special linker
  // symbols.
  if (IsLinkerSymbol) {
    Ctx.emitDiag(
        [&]() {
          Ctx.Diag->Report(diag::err_header_symbol_missing)
              << getAnnotatedName(&R, SymCtx, Loc.isValid());
        },
        &Loc);
    updateState(Result::Invalid);
    return;
  }

  // Missing declarations for exported symbols are hard errors on Pedantic mode.
  if (Mode == VerificationMode::Pedantic) {
    Ctx.emitDiag(
        [&]() {
          Ctx.Diag->Report(diag::err_header_symbol_missing)
              << getAnnotatedName(&R, SymCtx, Loc.isValid());
        },
        &Loc);
    updateState(Result::Invalid);
    return;
  }

  // Missing declarations for exported symbols are warnings on ErrorsAndWarnings
  // mode.
  if (Mode == VerificationMode::ErrorsAndWarnings) {
    Ctx.emitDiag(
        [&]() {
          Ctx.Diag->Report(diag::warn_header_symbol_missing)
              << getAnnotatedName(&R, SymCtx, Loc.isValid());
        },
        &Loc);
    updateState(Result::Ignore);
    return;
  }

  // Missing declarations are dropped for ErrorsOnly mode. It is the last
  // remaining mode.
  updateState(Result::Ignore);
  return;
}

void DylibVerifier::visitGlobal(const GlobalRecord &R) {
  SymbolContext SymCtx;
  SimpleSymbol Sym = parseSymbol(R.getName());
  SymCtx.SymbolName = Sym.Name;
  SymCtx.Kind = Sym.Kind;
  visitSymbolInDylib(R, SymCtx);
}

void DylibVerifier::visitObjCIVar(const ObjCIVarRecord &R,
                                  const StringRef Super) {
  SymbolContext SymCtx;
  SymCtx.SymbolName = ObjCIVarRecord::createScopedName(Super, R.getName());
  SymCtx.Kind = EncodeKind::ObjectiveCInstanceVariable;
  visitSymbolInDylib(R, SymCtx);
}

void DylibVerifier::accumulateSrcLocForDylibSymbols() {
  if (DSYMPath.empty())
    return;

  assert(DWARFCtx != nullptr && "Expected an initialized DWARFContext");
  if (DWARFCtx->ParsedDSYM)
    return;
  DWARFCtx->ParsedDSYM = true;
  DWARFCtx->SourceLocs =
      DylibReader::accumulateSourceLocFromDSYM(DSYMPath, Ctx.Target);
}

void DylibVerifier::visitObjCInterface(const ObjCInterfaceRecord &R) {
  SymbolContext SymCtx;
  SymCtx.SymbolName = R.getName();
  SymCtx.ObjCIFKind = assignObjCIFSymbolKind(&R);
  if (SymCtx.ObjCIFKind > ObjCIFSymbolKind::EHType) {
    if (R.hasExceptionAttribute()) {
      SymCtx.Kind = EncodeKind::ObjectiveCClassEHType;
      visitSymbolInDylib(R, SymCtx);
    }
    SymCtx.Kind = EncodeKind::ObjectiveCClass;
    visitSymbolInDylib(R, SymCtx);
  } else {
    SymCtx.Kind = R.hasExceptionAttribute() ? EncodeKind::ObjectiveCClassEHType
                                            : EncodeKind::ObjectiveCClass;
    visitSymbolInDylib(R, SymCtx);
  }

  for (const ObjCIVarRecord *IV : R.getObjCIVars())
    visitObjCIVar(*IV, R.getName());
}

void DylibVerifier::visitObjCCategory(const ObjCCategoryRecord &R) {
  for (const ObjCIVarRecord *IV : R.getObjCIVars())
    visitObjCIVar(*IV, R.getSuperClassName());
}

DylibVerifier::Result DylibVerifier::verifyRemainingSymbols() {
  if (getState() == Result::NoVerify)
    return Result::NoVerify;
  assert(!Dylib.empty() && "No binary to verify against");

  DWARFContext DWARFInfo;
  DWARFCtx = &DWARFInfo;
  Ctx.Target = Target(Architecture::AK_unknown, PlatformType::PLATFORM_UNKNOWN);
  for (std::shared_ptr<RecordsSlice> Slice : Dylib) {
    if (Ctx.Target.Arch == Slice->getTarget().Arch)
      continue;
    Ctx.DiscoveredFirstError = false;
    Ctx.PrintArch = true;
    Ctx.Target = Slice->getTarget();
    Ctx.DylibSlice = Slice.get();
    Slice->visit(*this);
  }
  return getState();
}

bool DylibVerifier::verifyBinaryAttrs(const ArrayRef<Target> ProvidedTargets,
                                      const BinaryAttrs &ProvidedBA,
                                      const LibAttrs &ProvidedReexports,
                                      const LibAttrs &ProvidedClients,
                                      const LibAttrs &ProvidedRPaths,
                                      const FileType &FT) {
  assert(!Dylib.empty() && "Need dylib to verify.");

  // Pickup any load commands that can differ per slice to compare.
  TargetList DylibTargets;
  LibAttrs DylibReexports;
  LibAttrs DylibClients;
  LibAttrs DylibRPaths;
  for (const std::shared_ptr<RecordsSlice> &RS : Dylib) {
    DylibTargets.push_back(RS->getTarget());
    const BinaryAttrs &BinInfo = RS->getBinaryAttrs();
    for (const StringRef LibName : BinInfo.RexportedLibraries)
      DylibReexports[LibName].set(DylibTargets.back().Arch);
    for (const StringRef LibName : BinInfo.AllowableClients)
      DylibClients[LibName].set(DylibTargets.back().Arch);
    // Compare attributes that are only representable in >= TBD_V5.
    if (FT >= FileType::TBD_V5)
      for (const StringRef Name : BinInfo.RPaths)
        DylibRPaths[Name].set(DylibTargets.back().Arch);
  }

  // Check targets first.
  ArchitectureSet ProvidedArchs = mapToArchitectureSet(ProvidedTargets);
  ArchitectureSet DylibArchs = mapToArchitectureSet(DylibTargets);
  if (ProvidedArchs != DylibArchs) {
    Ctx.Diag->Report(diag::err_architecture_mismatch)
        << ProvidedArchs << DylibArchs;
    return false;
  }
  auto ProvidedPlatforms = mapToPlatformVersionSet(ProvidedTargets);
  auto DylibPlatforms = mapToPlatformVersionSet(DylibTargets);
  if (ProvidedPlatforms != DylibPlatforms) {
    const bool DiffMinOS =
        mapToPlatformSet(ProvidedTargets) == mapToPlatformSet(DylibTargets);
    if (DiffMinOS)
      Ctx.Diag->Report(diag::warn_platform_mismatch)
          << ProvidedPlatforms << DylibPlatforms;
    else {
      Ctx.Diag->Report(diag::err_platform_mismatch)
          << ProvidedPlatforms << DylibPlatforms;
      return false;
    }
  }

  // Because InstallAPI requires certain attributes to match across architecture
  // slices, take the first one to compare those with.
  const BinaryAttrs &DylibBA = (*Dylib.begin())->getBinaryAttrs();

  if (ProvidedBA.InstallName != DylibBA.InstallName) {
    Ctx.Diag->Report(diag::err_install_name_mismatch)
        << ProvidedBA.InstallName << DylibBA.InstallName;
    return false;
  }

  if (ProvidedBA.CurrentVersion != DylibBA.CurrentVersion) {
    Ctx.Diag->Report(diag::err_current_version_mismatch)
        << ProvidedBA.CurrentVersion << DylibBA.CurrentVersion;
    return false;
  }

  if (ProvidedBA.CompatVersion != DylibBA.CompatVersion) {
    Ctx.Diag->Report(diag::err_compatibility_version_mismatch)
        << ProvidedBA.CompatVersion << DylibBA.CompatVersion;
    return false;
  }

  if (ProvidedBA.AppExtensionSafe != DylibBA.AppExtensionSafe) {
    Ctx.Diag->Report(diag::err_appextension_safe_mismatch)
        << (ProvidedBA.AppExtensionSafe ? "true" : "false")
        << (DylibBA.AppExtensionSafe ? "true" : "false");
    return false;
  }

  if (!DylibBA.TwoLevelNamespace) {
    Ctx.Diag->Report(diag::err_no_twolevel_namespace);
    return false;
  }

  if (ProvidedBA.OSLibNotForSharedCache != DylibBA.OSLibNotForSharedCache) {
    Ctx.Diag->Report(diag::err_shared_cache_eligiblity_mismatch)
        << (ProvidedBA.OSLibNotForSharedCache ? "true" : "false")
        << (DylibBA.OSLibNotForSharedCache ? "true" : "false");
    return false;
  }

  if (ProvidedBA.ParentUmbrella.empty() && !DylibBA.ParentUmbrella.empty()) {
    Ctx.Diag->Report(diag::err_parent_umbrella_missing)
        << "installAPI option" << DylibBA.ParentUmbrella;
    return false;
  }

  if (!ProvidedBA.ParentUmbrella.empty() && DylibBA.ParentUmbrella.empty()) {
    Ctx.Diag->Report(diag::err_parent_umbrella_missing)
        << "binary file" << ProvidedBA.ParentUmbrella;
    return false;
  }

  if ((!ProvidedBA.ParentUmbrella.empty()) &&
      (ProvidedBA.ParentUmbrella != DylibBA.ParentUmbrella)) {
    Ctx.Diag->Report(diag::err_parent_umbrella_mismatch)
        << ProvidedBA.ParentUmbrella << DylibBA.ParentUmbrella;
    return false;
  }

  auto CompareLibraries = [&](const LibAttrs &Provided, const LibAttrs &Dylib,
                              unsigned DiagID_missing, unsigned DiagID_mismatch,
                              bool Fatal = true) {
    if (Provided == Dylib)
      return true;

    for (const llvm::StringMapEntry<ArchitectureSet> &PAttr : Provided) {
      const auto DAttrIt = Dylib.find(PAttr.getKey());
      if (DAttrIt == Dylib.end()) {
        Ctx.Diag->Report(DiagID_missing) << "binary file" << PAttr;
        if (Fatal)
          return false;
      }

      if (PAttr.getValue() != DAttrIt->getValue()) {
        Ctx.Diag->Report(DiagID_mismatch) << PAttr << *DAttrIt;
        if (Fatal)
          return false;
      }
    }

    for (const llvm::StringMapEntry<ArchitectureSet> &DAttr : Dylib) {
      const auto PAttrIt = Provided.find(DAttr.getKey());
      if (PAttrIt == Provided.end()) {
        Ctx.Diag->Report(DiagID_missing) << "installAPI option" << DAttr;
        if (!Fatal)
          continue;
        return false;
      }

      if (PAttrIt->getValue() != DAttr.getValue()) {
        if (Fatal)
          llvm_unreachable("this case was already covered above.");
      }
    }
    return true;
  };

  if (!CompareLibraries(ProvidedReexports, DylibReexports,
                        diag::err_reexported_libraries_missing,
                        diag::err_reexported_libraries_mismatch))
    return false;

  if (!CompareLibraries(ProvidedClients, DylibClients,
                        diag::err_allowable_clients_missing,
                        diag::err_allowable_clients_mismatch))
    return false;

  if (FT >= FileType::TBD_V5) {
    // Ignore rpath differences if building an asan variant, since the
    //   compiler injects additional paths.
    // FIXME: Building with sanitizers does not always change the install
    //   name, so this is not a foolproof solution.
    if (!ProvidedBA.InstallName.ends_with("_asan")) {
      if (!CompareLibraries(ProvidedRPaths, DylibRPaths,
                            diag::warn_rpaths_missing,
                            diag::warn_rpaths_mismatch,
                            /*Fatal=*/false))
        return true;
    }
  }

  return true;
}

std::unique_ptr<SymbolSet> DylibVerifier::takeExports() {
  for (const auto &[Alias, Base] : Aliases) {
    TargetList Targets;
    SymbolFlags Flags = SymbolFlags::None;
    if (const Symbol *Sym = Exports->findSymbol(Base.second, Base.first)) {
      Flags = Sym->getFlags();
      Targets = {Sym->targets().begin(), Sym->targets().end()};
    }

    Record R(Alias.first, RecordLinkage::Exported, Flags);
    SymbolContext SymCtx;
    SymCtx.SymbolName = Alias.first;
    SymCtx.Kind = Alias.second;
    addSymbol(&R, SymCtx, std::move(Targets));
  }

  return std::move(Exports);
}

} // namespace installapi
} // namespace clang
