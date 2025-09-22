//===- Pass.cpp - LLVM Pass Infrastructure Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LLVM Pass infrastructure.  It is primarily
// responsible with ensuring that passes are executed and batched together
// optimally.
//
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassNameParser.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/PassInfo.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

#ifdef EXPENSIVE_CHECKS
#include "llvm/IR/StructuralHash.h"
#endif

using namespace llvm;

#define DEBUG_TYPE "ir"

//===----------------------------------------------------------------------===//
// Pass Implementation
//

// Force out-of-line virtual method.
Pass::~Pass() {
  delete Resolver;
}

// Force out-of-line virtual method.
ModulePass::~ModulePass() = default;

Pass *ModulePass::createPrinterPass(raw_ostream &OS,
                                    const std::string &Banner) const {
  return createPrintModulePass(OS, Banner);
}

PassManagerType ModulePass::getPotentialPassManagerType() const {
  return PMT_ModulePassManager;
}

static std::string getDescription(const Module &M) {
  return "module (" + M.getName().str() + ")";
}

bool ModulePass::skipModule(Module &M) const {
  OptPassGate &Gate = M.getContext().getOptPassGate();
  return Gate.isEnabled() &&
         !Gate.shouldRunPass(this->getPassName(), getDescription(M));
}

bool Pass::mustPreserveAnalysisID(char &AID) const {
  return Resolver->getAnalysisIfAvailable(&AID) != nullptr;
}

// dumpPassStructure - Implement the -debug-pass=Structure option
void Pass::dumpPassStructure(unsigned Offset) {
  dbgs().indent(Offset*2) << getPassName() << "\n";
}

/// getPassName - Return a nice clean name for a pass.  This usually
/// implemented in terms of the name that is registered by one of the
/// Registration templates, but can be overloaded directly.
StringRef Pass::getPassName() const {
  AnalysisID AID =  getPassID();
  const PassInfo *PI = PassRegistry::getPassRegistry()->getPassInfo(AID);
  if (PI)
    return PI->getPassName();
  return "Unnamed pass: implement Pass::getPassName()";
}

void Pass::preparePassManager(PMStack &) {
  // By default, don't do anything.
}

PassManagerType Pass::getPotentialPassManagerType() const {
  // Default implementation.
  return PMT_Unknown;
}

void Pass::getAnalysisUsage(AnalysisUsage &) const {
  // By default, no analysis results are used, all are invalidated.
}

void Pass::releaseMemory() {
  // By default, don't do anything.
}

void Pass::verifyAnalysis() const {
  // By default, don't do anything.
}

void *Pass::getAdjustedAnalysisPointer(AnalysisID AID) {
  return this;
}

ImmutablePass *Pass::getAsImmutablePass() {
  return nullptr;
}

PMDataManager *Pass::getAsPMDataManager() {
  return nullptr;
}

void Pass::setResolver(AnalysisResolver *AR) {
  assert(!Resolver && "Resolver is already set");
  Resolver = AR;
}

// print - Print out the internal state of the pass.  This is called by Analyze
// to print out the contents of an analysis.  Otherwise it is not necessary to
// implement this method.
void Pass::print(raw_ostream &OS, const Module *) const {
  OS << "Pass::print not implemented for pass: '" << getPassName() << "'!\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
// dump - call print(cerr);
LLVM_DUMP_METHOD void Pass::dump() const {
  print(dbgs(), nullptr);
}
#endif

#ifdef EXPENSIVE_CHECKS
uint64_t Pass::structuralHash(Module &M) const {
  return StructuralHash(M, true);
}

uint64_t Pass::structuralHash(Function &F) const {
  return StructuralHash(F, true);
}
#endif

//===----------------------------------------------------------------------===//
// ImmutablePass Implementation
//
// Force out-of-line virtual method.
ImmutablePass::~ImmutablePass() = default;

void ImmutablePass::initializePass() {
  // By default, don't do anything.
}

//===----------------------------------------------------------------------===//
// FunctionPass Implementation
//

Pass *FunctionPass::createPrinterPass(raw_ostream &OS,
                                      const std::string &Banner) const {
  return createPrintFunctionPass(OS, Banner);
}

PassManagerType FunctionPass::getPotentialPassManagerType() const {
  return PMT_FunctionPassManager;
}

static std::string getDescription(const Function &F) {
  return "function (" + F.getName().str() + ")";
}

bool FunctionPass::skipFunction(const Function &F) const {
  OptPassGate &Gate = F.getContext().getOptPassGate();
  if (Gate.isEnabled() &&
      !Gate.shouldRunPass(this->getPassName(), getDescription(F)))
    return true;

  if (F.hasOptNone()) {
    LLVM_DEBUG(dbgs() << "Skipping pass '" << getPassName() << "' on function "
                      << F.getName() << "\n");
    return true;
  }
  return false;
}

const PassInfo *Pass::lookupPassInfo(const void *TI) {
  return PassRegistry::getPassRegistry()->getPassInfo(TI);
}

const PassInfo *Pass::lookupPassInfo(StringRef Arg) {
  return PassRegistry::getPassRegistry()->getPassInfo(Arg);
}

Pass *Pass::createPass(AnalysisID ID) {
  const PassInfo *PI = PassRegistry::getPassRegistry()->getPassInfo(ID);
  if (!PI)
    return nullptr;
  return PI->createPass();
}

//===----------------------------------------------------------------------===//
//                  Analysis Group Implementation Code
//===----------------------------------------------------------------------===//

// RegisterAGBase implementation

RegisterAGBase::RegisterAGBase(StringRef Name, const void *InterfaceID,
                               const void *PassID, bool isDefault)
    : PassInfo(Name, InterfaceID) {
  PassRegistry::getPassRegistry()->registerAnalysisGroup(InterfaceID, PassID,
                                                         *this, isDefault);
}

//===----------------------------------------------------------------------===//
// PassRegistrationListener implementation
//

// enumeratePasses - Iterate over the registered passes, calling the
// passEnumerate callback on each PassInfo object.
void PassRegistrationListener::enumeratePasses() {
  PassRegistry::getPassRegistry()->enumerateWith(this);
}

PassNameParser::PassNameParser(cl::Option &O)
    : cl::parser<const PassInfo *>(O) {
  PassRegistry::getPassRegistry()->addRegistrationListener(this);
}

// This only gets called during static destruction, in which case the
// PassRegistry will have already been destroyed by llvm_shutdown().  So
// attempting to remove the registration listener is an error.
PassNameParser::~PassNameParser() = default;

//===----------------------------------------------------------------------===//
//   AnalysisUsage Class Implementation
//

namespace {

struct GetCFGOnlyPasses : public PassRegistrationListener {
  using VectorType = AnalysisUsage::VectorType;

  VectorType &CFGOnlyList;

  GetCFGOnlyPasses(VectorType &L) : CFGOnlyList(L) {}

  void passEnumerate(const PassInfo *P) override {
    if (P->isCFGOnlyPass())
      CFGOnlyList.push_back(P->getTypeInfo());
  }
};

} // end anonymous namespace

// setPreservesCFG - This function should be called to by the pass, iff they do
// not:
//
//  1. Add or remove basic blocks from the function
//  2. Modify terminator instructions in any way.
//
// This function annotates the AnalysisUsage info object to say that analyses
// that only depend on the CFG are preserved by this pass.
void AnalysisUsage::setPreservesCFG() {
  // Since this transformation doesn't modify the CFG, it preserves all analyses
  // that only depend on the CFG (like dominators, loop info, etc...)
  GetCFGOnlyPasses(Preserved).enumeratePasses();
}

AnalysisUsage &AnalysisUsage::addPreserved(StringRef Arg) {
  const PassInfo *PI = Pass::lookupPassInfo(Arg);
  // If the pass exists, preserve it. Otherwise silently do nothing.
  if (PI)
    pushUnique(Preserved, PI->getTypeInfo());
  return *this;
}

AnalysisUsage &AnalysisUsage::addRequiredID(const void *ID) {
  pushUnique(Required, ID);
  return *this;
}

AnalysisUsage &AnalysisUsage::addRequiredID(char &ID) {
  pushUnique(Required, &ID);
  return *this;
}

AnalysisUsage &AnalysisUsage::addRequiredTransitiveID(char &ID) {
  pushUnique(Required, &ID);
  pushUnique(RequiredTransitive, &ID);
  return *this;
}
