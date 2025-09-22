//===- ReturnValueChecker - Check methods always returning true -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines ReturnValueChecker, which models a very specific coding
// convention within the LLVM/Clang codebase: there several classes that have
// Error() methods which always return true.
// This checker was introduced to eliminate false positives caused by this
// peculiar "always returns true" invariant. (Normally, the analyzer assumes
// that a function returning `bool` can return both `true` and `false`, because
// otherwise it could've been a `void` function.)
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FormatVariadic.h"
#include <optional>

using namespace clang;
using namespace ento;
using llvm::formatv;

namespace {
class ReturnValueChecker : public Checker<check::PostCall> {
public:
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;

private:
  const CallDescriptionSet Methods = {
      // These are known in the LLVM project: 'Error()'
      {CDM::CXXMethod, {"ARMAsmParser", "Error"}},
      {CDM::CXXMethod, {"HexagonAsmParser", "Error"}},
      {CDM::CXXMethod, {"LLLexer", "Error"}},
      {CDM::CXXMethod, {"LLParser", "Error"}},
      {CDM::CXXMethod, {"MCAsmParser", "Error"}},
      {CDM::CXXMethod, {"MCAsmParserExtension", "Error"}},
      {CDM::CXXMethod, {"TGParser", "Error"}},
      {CDM::CXXMethod, {"X86AsmParser", "Error"}},
      // 'TokError()'
      {CDM::CXXMethod, {"LLParser", "TokError"}},
      {CDM::CXXMethod, {"MCAsmParser", "TokError"}},
      {CDM::CXXMethod, {"MCAsmParserExtension", "TokError"}},
      {CDM::CXXMethod, {"TGParser", "TokError"}},
      // 'error()'
      {CDM::CXXMethod, {"MIParser", "error"}},
      {CDM::CXXMethod, {"WasmAsmParser", "error"}},
      {CDM::CXXMethod, {"WebAssemblyAsmParser", "error"}},
      // Other
      {CDM::CXXMethod, {"AsmParser", "printError"}}};
};
} // namespace

static std::string getName(const CallEvent &Call) {
  std::string Name;
  if (const auto *MD = dyn_cast<CXXMethodDecl>(Call.getDecl()))
    if (const CXXRecordDecl *RD = MD->getParent())
      Name += RD->getNameAsString() + "::";

  Name += Call.getCalleeIdentifier()->getName();
  return Name;
}

void ReturnValueChecker::checkPostCall(const CallEvent &Call,
                                       CheckerContext &C) const {
  if (!Methods.contains(Call))
    return;

  auto ReturnV = Call.getReturnValue().getAs<DefinedOrUnknownSVal>();

  if (!ReturnV)
    return;

  ProgramStateRef State = C.getState();
  if (ProgramStateRef StTrue = State->assume(*ReturnV, true)) {
    // The return value can be true, so transition to a state where it's true.
    std::string Msg =
        formatv("'{0}' returns true (by convention)", getName(Call));
    C.addTransition(StTrue, C.getNoteTag(Msg, /*IsPrunable=*/true));
    return;
  }
  // Paranoia: if the return value is known to be false (which is highly
  // unlikely, it's easy to ensure that the method always returns true), then
  // produce a note that highlights that this unusual situation.
  // Note that this checker is 'hidden' so it cannot produce a bug report.
  std::string Msg = formatv("'{0}' returned false, breaking the convention "
                            "that it always returns true",
                            getName(Call));
  C.addTransition(State, C.getNoteTag(Msg, /*IsPrunable=*/true));
}

void ento::registerReturnValueChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<ReturnValueChecker>();
}

bool ento::shouldRegisterReturnValueChecker(const CheckerManager &mgr) {
  return true;
}
