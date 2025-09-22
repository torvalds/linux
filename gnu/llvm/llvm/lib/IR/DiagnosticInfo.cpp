//===- llvm/IR/DiagnosticInfo.cpp - Diagnostic Definitions ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the different classes involved in low level diagnostics.
//
// Diagnostics reporting is still done as part of the LLVMContext.
//===----------------------------------------------------------------------===//

#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/InstructionCost.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include <atomic>
#include <string>

using namespace llvm;

int llvm::getNextAvailablePluginDiagnosticKind() {
  static std::atomic<int> PluginKindID(DK_FirstPluginKind);
  return ++PluginKindID;
}

const char *OptimizationRemarkAnalysis::AlwaysPrint = "";

DiagnosticInfoInlineAsm::DiagnosticInfoInlineAsm(const Instruction &I,
                                                 const Twine &MsgStr,
                                                 DiagnosticSeverity Severity)
    : DiagnosticInfo(DK_InlineAsm, Severity), MsgStr(MsgStr), Instr(&I) {
  if (const MDNode *SrcLoc = I.getMetadata("srcloc")) {
    if (SrcLoc->getNumOperands() != 0)
      if (const auto *CI =
              mdconst::dyn_extract<ConstantInt>(SrcLoc->getOperand(0)))
        LocCookie = CI->getZExtValue();
  }
}

void DiagnosticInfoInlineAsm::print(DiagnosticPrinter &DP) const {
  DP << getMsgStr();
  if (getLocCookie())
    DP << " at line " << getLocCookie();
}

DiagnosticInfoResourceLimit::DiagnosticInfoResourceLimit(
    const Function &Fn, const char *ResourceName, uint64_t ResourceSize,
    uint64_t ResourceLimit, DiagnosticSeverity Severity, DiagnosticKind Kind)
    : DiagnosticInfoWithLocationBase(Kind, Severity, Fn, Fn.getSubprogram()),
      Fn(Fn), ResourceName(ResourceName), ResourceSize(ResourceSize),
      ResourceLimit(ResourceLimit) {}

void DiagnosticInfoResourceLimit::print(DiagnosticPrinter &DP) const {
  DP << getLocationStr() << ": " << getResourceName() << " ("
     << getResourceSize() << ") exceeds limit (" << getResourceLimit()
     << ") in function '" << getFunction() << '\'';
}

void DiagnosticInfoDebugMetadataVersion::print(DiagnosticPrinter &DP) const {
  DP << "ignoring debug info with an invalid version (" << getMetadataVersion()
     << ") in " << getModule();
}

void DiagnosticInfoIgnoringInvalidDebugMetadata::print(
    DiagnosticPrinter &DP) const {
  DP << "ignoring invalid debug info in " << getModule().getModuleIdentifier();
}

void DiagnosticInfoSampleProfile::print(DiagnosticPrinter &DP) const {
  if (!FileName.empty()) {
    DP << getFileName();
    if (LineNum > 0)
      DP << ":" << getLineNum();
    DP << ": ";
  }
  DP << getMsg();
}

void DiagnosticInfoPGOProfile::print(DiagnosticPrinter &DP) const {
  if (getFileName())
    DP << getFileName() << ": ";
  DP << getMsg();
}

void DiagnosticInfo::anchor() {}
void DiagnosticInfoStackSize::anchor() {}
void DiagnosticInfoWithLocationBase::anchor() {}
void DiagnosticInfoIROptimization::anchor() {}

DiagnosticLocation::DiagnosticLocation(const DebugLoc &DL) {
  if (!DL)
    return;
  File = DL->getFile();
  Line = DL->getLine();
  Column = DL->getColumn();
}

DiagnosticLocation::DiagnosticLocation(const DISubprogram *SP) {
  if (!SP)
    return;

  File = SP->getFile();
  Line = SP->getScopeLine();
  Column = 0;
}

StringRef DiagnosticLocation::getRelativePath() const {
  return File->getFilename();
}

std::string DiagnosticLocation::getAbsolutePath() const {
  StringRef Name = File->getFilename();
  if (sys::path::is_absolute(Name))
    return std::string(Name);

  SmallString<128> Path;
  sys::path::append(Path, File->getDirectory(), Name);
  return sys::path::remove_leading_dotslash(Path).str();
}

std::string DiagnosticInfoWithLocationBase::getAbsolutePath() const {
  return Loc.getAbsolutePath();
}

void DiagnosticInfoWithLocationBase::getLocation(StringRef &RelativePath,
                                                 unsigned &Line,
                                                 unsigned &Column) const {
  RelativePath = Loc.getRelativePath();
  Line = Loc.getLine();
  Column = Loc.getColumn();
}

std::string DiagnosticInfoWithLocationBase::getLocationStr() const {
  StringRef Filename("<unknown>");
  unsigned Line = 0;
  unsigned Column = 0;
  if (isLocationAvailable())
    getLocation(Filename, Line, Column);
  return (Filename + ":" + Twine(Line) + ":" + Twine(Column)).str();
}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key,
                                                   const Value *V)
    : Key(std::string(Key)) {
  if (auto *F = dyn_cast<Function>(V)) {
    if (DISubprogram *SP = F->getSubprogram())
      Loc = SP;
  }
  else if (auto *I = dyn_cast<Instruction>(V))
    Loc = I->getDebugLoc();

  // Only include names that correspond to user variables.  FIXME: We should use
  // debug info if available to get the name of the user variable.
  if (isa<llvm::Argument>(V) || isa<GlobalValue>(V))
    Val = std::string(GlobalValue::dropLLVMManglingEscape(V->getName()));
  else if (isa<Constant>(V)) {
    raw_string_ostream OS(Val);
    V->printAsOperand(OS, /*PrintType=*/false);
  } else if (auto *I = dyn_cast<Instruction>(V)) {
    Val = I->getOpcodeName();
  } else if (auto *MD = dyn_cast<MetadataAsValue>(V)) {
    if (auto *S = dyn_cast<MDString>(MD->getMetadata()))
      Val = S->getString();
  }
}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, const Type *T)
    : Key(std::string(Key)) {
  raw_string_ostream OS(Val);
  OS << *T;
}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, StringRef S)
    : Key(std::string(Key)), Val(S.str()) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, int N)
    : Key(std::string(Key)), Val(itostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, float N)
    : Key(std::string(Key)), Val(llvm::to_string(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, long N)
    : Key(std::string(Key)), Val(itostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, long long N)
    : Key(std::string(Key)), Val(itostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, unsigned N)
    : Key(std::string(Key)), Val(utostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key,
                                                   unsigned long N)
    : Key(std::string(Key)), Val(utostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key,
                                                   unsigned long long N)
    : Key(std::string(Key)), Val(utostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key,
                                                   ElementCount EC)
    : Key(std::string(Key)) {
  raw_string_ostream OS(Val);
  EC.print(OS);
}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key,
                                                   InstructionCost C)
    : Key(std::string(Key)) {
  raw_string_ostream OS(Val);
  C.print(OS);
}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, DebugLoc Loc)
    : Key(std::string(Key)), Loc(Loc) {
  if (Loc) {
    Val = (Loc->getFilename() + ":" + Twine(Loc.getLine()) + ":" +
           Twine(Loc.getCol())).str();
  } else {
    Val = "<UNKNOWN LOCATION>";
  }
}

void DiagnosticInfoOptimizationBase::print(DiagnosticPrinter &DP) const {
  DP << getLocationStr() << ": " << getMsg();
  if (Hotness)
    DP << " (hotness: " << *Hotness << ")";
}

OptimizationRemark::OptimizationRemark(const char *PassName,
                                       StringRef RemarkName,
                                       const DiagnosticLocation &Loc,
                                       const Value *CodeRegion)
    : DiagnosticInfoIROptimization(
          DK_OptimizationRemark, DS_Remark, PassName, RemarkName,
          *cast<BasicBlock>(CodeRegion)->getParent(), Loc, CodeRegion) {}

OptimizationRemark::OptimizationRemark(const char *PassName,
                                       StringRef RemarkName,
                                       const Instruction *Inst)
    : DiagnosticInfoIROptimization(DK_OptimizationRemark, DS_Remark, PassName,
                                   RemarkName, *Inst->getParent()->getParent(),
                                   Inst->getDebugLoc(), Inst->getParent()) {}

static const BasicBlock *getFirstFunctionBlock(const Function *Func) {
  return Func->empty() ? nullptr : &Func->front();
}

OptimizationRemark::OptimizationRemark(const char *PassName,
                                       StringRef RemarkName,
                                       const Function *Func)
    : DiagnosticInfoIROptimization(DK_OptimizationRemark, DS_Remark, PassName,
                                   RemarkName, *Func, Func->getSubprogram(),
                                   getFirstFunctionBlock(Func)) {}

bool OptimizationRemark::isEnabled() const {
  const Function &Fn = getFunction();
  LLVMContext &Ctx = Fn.getContext();
  return Ctx.getDiagHandlerPtr()->isPassedOptRemarkEnabled(getPassName());
}

OptimizationRemarkMissed::OptimizationRemarkMissed(
    const char *PassName, StringRef RemarkName, const DiagnosticLocation &Loc,
    const Value *CodeRegion)
    : DiagnosticInfoIROptimization(
          DK_OptimizationRemarkMissed, DS_Remark, PassName, RemarkName,
          *cast<BasicBlock>(CodeRegion)->getParent(), Loc, CodeRegion) {}

OptimizationRemarkMissed::OptimizationRemarkMissed(const char *PassName,
                                                   StringRef RemarkName,
                                                   const Instruction *Inst)
    : DiagnosticInfoIROptimization(DK_OptimizationRemarkMissed, DS_Remark,
                                   PassName, RemarkName,
                                   *Inst->getParent()->getParent(),
                                   Inst->getDebugLoc(), Inst->getParent()) {}

OptimizationRemarkMissed::OptimizationRemarkMissed(const char *PassName,
                                                   StringRef RemarkName,
                                                   const Function *Func)
    : DiagnosticInfoIROptimization(
          DK_OptimizationRemarkMissed, DS_Remark, PassName, RemarkName, *Func,
          Func->getSubprogram(), getFirstFunctionBlock(Func)) {}

bool OptimizationRemarkMissed::isEnabled() const {
  const Function &Fn = getFunction();
  LLVMContext &Ctx = Fn.getContext();
  return Ctx.getDiagHandlerPtr()->isMissedOptRemarkEnabled(getPassName());
}

OptimizationRemarkAnalysis::OptimizationRemarkAnalysis(
    const char *PassName, StringRef RemarkName, const DiagnosticLocation &Loc,
    const Value *CodeRegion)
    : DiagnosticInfoIROptimization(
          DK_OptimizationRemarkAnalysis, DS_Remark, PassName, RemarkName,
          *cast<BasicBlock>(CodeRegion)->getParent(), Loc, CodeRegion) {}

OptimizationRemarkAnalysis::OptimizationRemarkAnalysis(const char *PassName,
                                                       StringRef RemarkName,
                                                       const Instruction *Inst)
    : DiagnosticInfoIROptimization(DK_OptimizationRemarkAnalysis, DS_Remark,
                                   PassName, RemarkName,
                                   *Inst->getParent()->getParent(),
                                   Inst->getDebugLoc(), Inst->getParent()) {}

OptimizationRemarkAnalysis::OptimizationRemarkAnalysis(
    enum DiagnosticKind Kind, const char *PassName, StringRef RemarkName,
    const DiagnosticLocation &Loc, const Value *CodeRegion)
    : DiagnosticInfoIROptimization(Kind, DS_Remark, PassName, RemarkName,
                                   *cast<BasicBlock>(CodeRegion)->getParent(),
                                   Loc, CodeRegion) {}

OptimizationRemarkAnalysis::OptimizationRemarkAnalysis(const char *PassName,
                                                       StringRef RemarkName,
                                                       const Function *Func)
    : DiagnosticInfoIROptimization(
          DK_OptimizationRemarkAnalysis, DS_Remark, PassName, RemarkName, *Func,
          Func->getSubprogram(), getFirstFunctionBlock(Func)) {}

bool OptimizationRemarkAnalysis::isEnabled() const {
  const Function &Fn = getFunction();
  LLVMContext &Ctx = Fn.getContext();
  return Ctx.getDiagHandlerPtr()->isAnalysisRemarkEnabled(getPassName()) ||
         shouldAlwaysPrint();
}

void DiagnosticInfoMIRParser::print(DiagnosticPrinter &DP) const {
  DP << Diagnostic;
}

void DiagnosticInfoSrcMgr::print(DiagnosticPrinter &DP) const {
  DP << Diagnostic;
}

DiagnosticInfoOptimizationFailure::DiagnosticInfoOptimizationFailure(
    const char *PassName, StringRef RemarkName, const DiagnosticLocation &Loc,
    const Value *CodeRegion)
    : DiagnosticInfoIROptimization(
          DK_OptimizationFailure, DS_Warning, PassName, RemarkName,
          *cast<BasicBlock>(CodeRegion)->getParent(), Loc, CodeRegion) {}

bool DiagnosticInfoOptimizationFailure::isEnabled() const {
  // Only print warnings.
  return getSeverity() == DS_Warning;
}

void DiagnosticInfoUnsupported::print(DiagnosticPrinter &DP) const {
  std::string Str;
  raw_string_ostream OS(Str);

  OS << getLocationStr() << ": in function " << getFunction().getName() << ' '
     << *getFunction().getFunctionType() << ": " << Msg << '\n';
  OS.flush();
  DP << Str;
}

void DiagnosticInfoISelFallback::print(DiagnosticPrinter &DP) const {
  DP << "Instruction selection used fallback path for " << getFunction();
}

void DiagnosticInfoOptimizationBase::insert(StringRef S) {
  Args.emplace_back(S);
}

void DiagnosticInfoOptimizationBase::insert(Argument A) {
  Args.push_back(std::move(A));
}

void DiagnosticInfoOptimizationBase::insert(setIsVerbose V) {
  IsVerbose = true;
}

void DiagnosticInfoOptimizationBase::insert(setExtraArgs EA) {
  FirstExtraArgIndex = Args.size();
}

std::string DiagnosticInfoOptimizationBase::getMsg() const {
  std::string Str;
  raw_string_ostream OS(Str);
  for (const DiagnosticInfoOptimizationBase::Argument &Arg :
       make_range(Args.begin(), FirstExtraArgIndex == -1
                                    ? Args.end()
                                    : Args.begin() + FirstExtraArgIndex))
    OS << Arg.Val;
  return Str;
}

DiagnosticInfoMisExpect::DiagnosticInfoMisExpect(const Instruction *Inst,
                                                 Twine &Msg)
    : DiagnosticInfoWithLocationBase(DK_MisExpect, DS_Warning,
                                     *Inst->getParent()->getParent(),
                                     Inst->getDebugLoc()),
      Msg(Msg) {}

void DiagnosticInfoMisExpect::print(DiagnosticPrinter &DP) const {
  DP << getLocationStr() << ": " << getMsg();
}

void OptimizationRemarkAnalysisFPCommute::anchor() {}
void OptimizationRemarkAnalysisAliasing::anchor() {}

void llvm::diagnoseDontCall(const CallInst &CI) {
  const auto *F =
      dyn_cast<Function>(CI.getCalledOperand()->stripPointerCasts());

  if (!F)
    return;

  for (int i = 0; i != 2; ++i) {
    auto AttrName = i == 0 ? "dontcall-error" : "dontcall-warn";
    auto Sev = i == 0 ? DS_Error : DS_Warning;

    if (F->hasFnAttribute(AttrName)) {
      uint64_t LocCookie = 0;
      auto A = F->getFnAttribute(AttrName);
      if (MDNode *MD = CI.getMetadata("srcloc"))
        LocCookie =
            mdconst::extract<ConstantInt>(MD->getOperand(0))->getZExtValue();
      DiagnosticInfoDontCall D(F->getName(), A.getValueAsString(), Sev,
                               LocCookie);
      F->getContext().diagnose(D);
    }
  }
}

void DiagnosticInfoDontCall::print(DiagnosticPrinter &DP) const {
  DP << "call to " << demangle(getFunctionName()) << " marked \"dontcall-";
  if (getSeverity() == DiagnosticSeverity::DS_Error)
    DP << "error\"";
  else
    DP << "warn\"";
  if (!getNote().empty())
    DP << ": " << getNote();
}
