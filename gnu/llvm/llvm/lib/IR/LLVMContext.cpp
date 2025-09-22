//===-- LLVMContext.cpp - Implement LLVMContext ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements LLVMContext, as a wrapper around the opaque
//  class LLVMContextImpl.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/LLVMContext.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/Remarks/RemarkStreamer.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdlib>
#include <string>
#include <utility>

using namespace llvm;

LLVMContext::LLVMContext() : pImpl(new LLVMContextImpl(*this)) {
  // Create the fixed metadata kinds. This is done in the same order as the
  // MD_* enum values so that they correspond.
  std::pair<unsigned, StringRef> MDKinds[] = {
#define LLVM_FIXED_MD_KIND(EnumID, Name, Value) {EnumID, Name},
#include "llvm/IR/FixedMetadataKinds.def"
#undef LLVM_FIXED_MD_KIND
  };

  for (auto &MDKind : MDKinds) {
    unsigned ID = getMDKindID(MDKind.second);
    assert(ID == MDKind.first && "metadata kind id drifted");
    (void)ID;
  }

  auto *DeoptEntry = pImpl->getOrInsertBundleTag("deopt");
  assert(DeoptEntry->second == LLVMContext::OB_deopt &&
         "deopt operand bundle id drifted!");
  (void)DeoptEntry;

  auto *FuncletEntry = pImpl->getOrInsertBundleTag("funclet");
  assert(FuncletEntry->second == LLVMContext::OB_funclet &&
         "funclet operand bundle id drifted!");
  (void)FuncletEntry;

  auto *GCTransitionEntry = pImpl->getOrInsertBundleTag("gc-transition");
  assert(GCTransitionEntry->second == LLVMContext::OB_gc_transition &&
         "gc-transition operand bundle id drifted!");
  (void)GCTransitionEntry;

  auto *CFGuardTargetEntry = pImpl->getOrInsertBundleTag("cfguardtarget");
  assert(CFGuardTargetEntry->second == LLVMContext::OB_cfguardtarget &&
         "cfguardtarget operand bundle id drifted!");
  (void)CFGuardTargetEntry;

  auto *PreallocatedEntry = pImpl->getOrInsertBundleTag("preallocated");
  assert(PreallocatedEntry->second == LLVMContext::OB_preallocated &&
         "preallocated operand bundle id drifted!");
  (void)PreallocatedEntry;

  auto *GCLiveEntry = pImpl->getOrInsertBundleTag("gc-live");
  assert(GCLiveEntry->second == LLVMContext::OB_gc_live &&
         "gc-transition operand bundle id drifted!");
  (void)GCLiveEntry;

  auto *ClangAttachedCall =
      pImpl->getOrInsertBundleTag("clang.arc.attachedcall");
  assert(ClangAttachedCall->second == LLVMContext::OB_clang_arc_attachedcall &&
         "clang.arc.attachedcall operand bundle id drifted!");
  (void)ClangAttachedCall;

  auto *PtrauthEntry = pImpl->getOrInsertBundleTag("ptrauth");
  assert(PtrauthEntry->second == LLVMContext::OB_ptrauth &&
         "ptrauth operand bundle id drifted!");
  (void)PtrauthEntry;

  auto *KCFIEntry = pImpl->getOrInsertBundleTag("kcfi");
  assert(KCFIEntry->second == LLVMContext::OB_kcfi &&
         "kcfi operand bundle id drifted!");
  (void)KCFIEntry;

  auto *ConvergenceCtrlEntry = pImpl->getOrInsertBundleTag("convergencectrl");
  assert(ConvergenceCtrlEntry->second == LLVMContext::OB_convergencectrl &&
         "convergencectrl operand bundle id drifted!");
  (void)ConvergenceCtrlEntry;

  SyncScope::ID SingleThreadSSID =
      pImpl->getOrInsertSyncScopeID("singlethread");
  assert(SingleThreadSSID == SyncScope::SingleThread &&
         "singlethread synchronization scope ID drifted!");
  (void)SingleThreadSSID;

  SyncScope::ID SystemSSID =
      pImpl->getOrInsertSyncScopeID("");
  assert(SystemSSID == SyncScope::System &&
         "system synchronization scope ID drifted!");
  (void)SystemSSID;
}

LLVMContext::~LLVMContext() { delete pImpl; }

void LLVMContext::addModule(Module *M) {
  pImpl->OwnedModules.insert(M);
}

void LLVMContext::removeModule(Module *M) {
  pImpl->OwnedModules.erase(M);
  pImpl->MachineFunctionNums.erase(M);
}

unsigned LLVMContext::generateMachineFunctionNum(Function &F) {
  Module *M = F.getParent();
  assert(pImpl->OwnedModules.contains(M) && "Unexpected module!");
  return pImpl->MachineFunctionNums[M]++;
}

//===----------------------------------------------------------------------===//
// Recoverable Backend Errors
//===----------------------------------------------------------------------===//

void LLVMContext::setDiagnosticHandlerCallBack(
    DiagnosticHandler::DiagnosticHandlerTy DiagnosticHandler,
    void *DiagnosticContext, bool RespectFilters) {
  pImpl->DiagHandler->DiagHandlerCallback = DiagnosticHandler;
  pImpl->DiagHandler->DiagnosticContext = DiagnosticContext;
  pImpl->RespectDiagnosticFilters = RespectFilters;
}

void LLVMContext::setDiagnosticHandler(std::unique_ptr<DiagnosticHandler> &&DH,
                                      bool RespectFilters) {
  pImpl->DiagHandler = std::move(DH);
  pImpl->RespectDiagnosticFilters = RespectFilters;
}

void LLVMContext::setDiagnosticsHotnessRequested(bool Requested) {
  pImpl->DiagnosticsHotnessRequested = Requested;
}
bool LLVMContext::getDiagnosticsHotnessRequested() const {
  return pImpl->DiagnosticsHotnessRequested;
}

void LLVMContext::setDiagnosticsHotnessThreshold(std::optional<uint64_t> Threshold) {
  pImpl->DiagnosticsHotnessThreshold = Threshold;
}
void LLVMContext::setMisExpectWarningRequested(bool Requested) {
  pImpl->MisExpectWarningRequested = Requested;
}
bool LLVMContext::getMisExpectWarningRequested() const {
  return pImpl->MisExpectWarningRequested;
}
uint64_t LLVMContext::getDiagnosticsHotnessThreshold() const {
  return pImpl->DiagnosticsHotnessThreshold.value_or(UINT64_MAX);
}
void LLVMContext::setDiagnosticsMisExpectTolerance(
    std::optional<uint32_t> Tolerance) {
  pImpl->DiagnosticsMisExpectTolerance = Tolerance;
}
uint32_t LLVMContext::getDiagnosticsMisExpectTolerance() const {
  return pImpl->DiagnosticsMisExpectTolerance.value_or(0);
}

bool LLVMContext::isDiagnosticsHotnessThresholdSetFromPSI() const {
  return !pImpl->DiagnosticsHotnessThreshold.has_value();
}

remarks::RemarkStreamer *LLVMContext::getMainRemarkStreamer() {
  return pImpl->MainRemarkStreamer.get();
}
const remarks::RemarkStreamer *LLVMContext::getMainRemarkStreamer() const {
  return const_cast<LLVMContext *>(this)->getMainRemarkStreamer();
}
void LLVMContext::setMainRemarkStreamer(
    std::unique_ptr<remarks::RemarkStreamer> RemarkStreamer) {
  pImpl->MainRemarkStreamer = std::move(RemarkStreamer);
}

LLVMRemarkStreamer *LLVMContext::getLLVMRemarkStreamer() {
  return pImpl->LLVMRS.get();
}
const LLVMRemarkStreamer *LLVMContext::getLLVMRemarkStreamer() const {
  return const_cast<LLVMContext *>(this)->getLLVMRemarkStreamer();
}
void LLVMContext::setLLVMRemarkStreamer(
    std::unique_ptr<LLVMRemarkStreamer> RemarkStreamer) {
  pImpl->LLVMRS = std::move(RemarkStreamer);
}

DiagnosticHandler::DiagnosticHandlerTy
LLVMContext::getDiagnosticHandlerCallBack() const {
  return pImpl->DiagHandler->DiagHandlerCallback;
}

void *LLVMContext::getDiagnosticContext() const {
  return pImpl->DiagHandler->DiagnosticContext;
}

void LLVMContext::setYieldCallback(YieldCallbackTy Callback, void *OpaqueHandle)
{
  pImpl->YieldCallback = Callback;
  pImpl->YieldOpaqueHandle = OpaqueHandle;
}

void LLVMContext::yield() {
  if (pImpl->YieldCallback)
    pImpl->YieldCallback(this, pImpl->YieldOpaqueHandle);
}

void LLVMContext::emitError(const Twine &ErrorStr) {
  diagnose(DiagnosticInfoInlineAsm(ErrorStr));
}

void LLVMContext::emitError(const Instruction *I, const Twine &ErrorStr) {
  assert (I && "Invalid instruction");
  diagnose(DiagnosticInfoInlineAsm(*I, ErrorStr));
}

static bool isDiagnosticEnabled(const DiagnosticInfo &DI) {
  // Optimization remarks are selective. They need to check whether the regexp
  // pattern, passed via one of the -pass-remarks* flags, matches the name of
  // the pass that is emitting the diagnostic. If there is no match, ignore the
  // diagnostic and return.
  //
  // Also noisy remarks are only enabled if we have hotness information to sort
  // them.
  if (auto *Remark = dyn_cast<DiagnosticInfoOptimizationBase>(&DI))
    return Remark->isEnabled() &&
           (!Remark->isVerbose() || Remark->getHotness());

  return true;
}

const char *
LLVMContext::getDiagnosticMessagePrefix(DiagnosticSeverity Severity) {
  switch (Severity) {
  case DS_Error:
    return "error";
  case DS_Warning:
    return "warning";
  case DS_Remark:
    return "remark";
  case DS_Note:
    return "note";
  }
  llvm_unreachable("Unknown DiagnosticSeverity");
}

void LLVMContext::diagnose(const DiagnosticInfo &DI) {
  if (auto *OptDiagBase = dyn_cast<DiagnosticInfoOptimizationBase>(&DI))
    if (LLVMRemarkStreamer *RS = getLLVMRemarkStreamer())
      RS->emit(*OptDiagBase);

  // If there is a report handler, use it.
  if (pImpl->DiagHandler) {
    if (DI.getSeverity() == DS_Error)
      pImpl->DiagHandler->HasErrors = true;
    if ((!pImpl->RespectDiagnosticFilters || isDiagnosticEnabled(DI)) &&
        pImpl->DiagHandler->handleDiagnostics(DI))
      return;
  }

  if (!isDiagnosticEnabled(DI))
    return;

  // Otherwise, print the message with a prefix based on the severity.
  DiagnosticPrinterRawOStream DP(errs());
  errs() << getDiagnosticMessagePrefix(DI.getSeverity()) << ": ";
  DI.print(DP);
  errs() << "\n";
  if (DI.getSeverity() == DS_Error)
    exit(1);
}

void LLVMContext::emitError(uint64_t LocCookie, const Twine &ErrorStr) {
  diagnose(DiagnosticInfoInlineAsm(LocCookie, ErrorStr));
}

//===----------------------------------------------------------------------===//
// Metadata Kind Uniquing
//===----------------------------------------------------------------------===//

/// Return a unique non-zero ID for the specified metadata kind.
unsigned LLVMContext::getMDKindID(StringRef Name) const {
  // If this is new, assign it its ID.
  return pImpl->CustomMDKindNames.insert(
                                     std::make_pair(
                                         Name, pImpl->CustomMDKindNames.size()))
      .first->second;
}

/// getHandlerNames - Populate client-supplied smallvector using custom
/// metadata name and ID.
void LLVMContext::getMDKindNames(SmallVectorImpl<StringRef> &Names) const {
  Names.resize(pImpl->CustomMDKindNames.size());
  for (StringMap<unsigned>::const_iterator I = pImpl->CustomMDKindNames.begin(),
       E = pImpl->CustomMDKindNames.end(); I != E; ++I)
    Names[I->second] = I->first();
}

void LLVMContext::getOperandBundleTags(SmallVectorImpl<StringRef> &Tags) const {
  pImpl->getOperandBundleTags(Tags);
}

StringMapEntry<uint32_t> *
LLVMContext::getOrInsertBundleTag(StringRef TagName) const {
  return pImpl->getOrInsertBundleTag(TagName);
}

uint32_t LLVMContext::getOperandBundleTagID(StringRef Tag) const {
  return pImpl->getOperandBundleTagID(Tag);
}

SyncScope::ID LLVMContext::getOrInsertSyncScopeID(StringRef SSN) {
  return pImpl->getOrInsertSyncScopeID(SSN);
}

void LLVMContext::getSyncScopeNames(SmallVectorImpl<StringRef> &SSNs) const {
  pImpl->getSyncScopeNames(SSNs);
}

void LLVMContext::setGC(const Function &Fn, std::string GCName) {
  auto It = pImpl->GCNames.find(&Fn);

  if (It == pImpl->GCNames.end()) {
    pImpl->GCNames.insert(std::make_pair(&Fn, std::move(GCName)));
    return;
  }
  It->second = std::move(GCName);
}

const std::string &LLVMContext::getGC(const Function &Fn) {
  return pImpl->GCNames[&Fn];
}

void LLVMContext::deleteGC(const Function &Fn) {
  pImpl->GCNames.erase(&Fn);
}

bool LLVMContext::shouldDiscardValueNames() const {
  return pImpl->DiscardValueNames;
}

bool LLVMContext::isODRUniquingDebugTypes() const { return !!pImpl->DITypeMap; }

void LLVMContext::enableDebugTypeODRUniquing() {
  if (pImpl->DITypeMap)
    return;

  pImpl->DITypeMap.emplace();
}

void LLVMContext::disableDebugTypeODRUniquing() { pImpl->DITypeMap.reset(); }

void LLVMContext::setDiscardValueNames(bool Discard) {
  pImpl->DiscardValueNames = Discard;
}

OptPassGate &LLVMContext::getOptPassGate() const {
  return pImpl->getOptPassGate();
}

void LLVMContext::setOptPassGate(OptPassGate& OPG) {
  pImpl->setOptPassGate(OPG);
}

const DiagnosticHandler *LLVMContext::getDiagHandlerPtr() const {
  return pImpl->DiagHandler.get();
}

std::unique_ptr<DiagnosticHandler> LLVMContext::getDiagnosticHandler() {
  return std::move(pImpl->DiagHandler);
}

void LLVMContext::setOpaquePointers(bool Enable) const {
  assert(Enable && "Cannot disable opaque pointers");
}

bool LLVMContext::supportsTypedPointers() const {
  return false;
}

StringRef LLVMContext::getDefaultTargetCPU() {
  return pImpl->DefaultTargetCPU;
}

void LLVMContext::setDefaultTargetCPU(StringRef CPU) {
  pImpl->DefaultTargetCPU = CPU;
}

StringRef LLVMContext::getDefaultTargetFeatures() {
  return pImpl->DefaultTargetFeatures;
}

void LLVMContext::setDefaultTargetFeatures(StringRef Features) {
  pImpl->DefaultTargetFeatures = Features;
}
