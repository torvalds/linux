//===- Debugify.cpp - Check debug info preservation in optimizations ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file In the `synthetic` mode, the `-debugify` attaches synthetic debug info
/// to everything. It can be used to create targeted tests for debug info
/// preservation. In addition, when using the `original` mode, it can check
/// original debug info preservation. The `synthetic` mode is default one.
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/Debugify.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include <optional>

#define DEBUG_TYPE "debugify"

using namespace llvm;

namespace {

cl::opt<bool> Quiet("debugify-quiet",
                    cl::desc("Suppress verbose debugify output"));

cl::opt<uint64_t> DebugifyFunctionsLimit(
    "debugify-func-limit",
    cl::desc("Set max number of processed functions per pass."),
    cl::init(UINT_MAX));

enum class Level {
  Locations,
  LocationsAndVariables
};

cl::opt<Level> DebugifyLevel(
    "debugify-level", cl::desc("Kind of debug info to add"),
    cl::values(clEnumValN(Level::Locations, "locations", "Locations only"),
               clEnumValN(Level::LocationsAndVariables, "location+variables",
                          "Locations and Variables")),
    cl::init(Level::LocationsAndVariables));

raw_ostream &dbg() { return Quiet ? nulls() : errs(); }

uint64_t getAllocSizeInBits(Module &M, Type *Ty) {
  return Ty->isSized() ? M.getDataLayout().getTypeAllocSizeInBits(Ty) : 0;
}

bool isFunctionSkipped(Function &F) {
  return F.isDeclaration() || !F.hasExactDefinition();
}

/// Find the basic block's terminating instruction.
///
/// Special care is needed to handle musttail and deopt calls, as these behave
/// like (but are in fact not) terminators.
Instruction *findTerminatingInstruction(BasicBlock &BB) {
  if (auto *I = BB.getTerminatingMustTailCall())
    return I;
  if (auto *I = BB.getTerminatingDeoptimizeCall())
    return I;
  return BB.getTerminator();
}
} // end anonymous namespace

bool llvm::applyDebugifyMetadata(
    Module &M, iterator_range<Module::iterator> Functions, StringRef Banner,
    std::function<bool(DIBuilder &DIB, Function &F)> ApplyToMF) {
  // Skip modules with debug info.
  if (M.getNamedMetadata("llvm.dbg.cu")) {
    dbg() << Banner << "Skipping module with debug info\n";
    return false;
  }

  DIBuilder DIB(M);
  LLVMContext &Ctx = M.getContext();
  auto *Int32Ty = Type::getInt32Ty(Ctx);

  // Get a DIType which corresponds to Ty.
  DenseMap<uint64_t, DIType *> TypeCache;
  auto getCachedDIType = [&](Type *Ty) -> DIType * {
    uint64_t Size = getAllocSizeInBits(M, Ty);
    DIType *&DTy = TypeCache[Size];
    if (!DTy) {
      std::string Name = "ty" + utostr(Size);
      DTy = DIB.createBasicType(Name, Size, dwarf::DW_ATE_unsigned);
    }
    return DTy;
  };

  unsigned NextLine = 1;
  unsigned NextVar = 1;
  auto File = DIB.createFile(M.getName(), "/");
  auto CU = DIB.createCompileUnit(dwarf::DW_LANG_C, File, "debugify",
                                  /*isOptimized=*/true, "", 0);

  // Visit each instruction.
  for (Function &F : Functions) {
    if (isFunctionSkipped(F))
      continue;

    bool InsertedDbgVal = false;
    auto SPType =
        DIB.createSubroutineType(DIB.getOrCreateTypeArray(std::nullopt));
    DISubprogram::DISPFlags SPFlags =
        DISubprogram::SPFlagDefinition | DISubprogram::SPFlagOptimized;
    if (F.hasPrivateLinkage() || F.hasInternalLinkage())
      SPFlags |= DISubprogram::SPFlagLocalToUnit;
    auto SP = DIB.createFunction(CU, F.getName(), F.getName(), File, NextLine,
                                 SPType, NextLine, DINode::FlagZero, SPFlags);
    F.setSubprogram(SP);

    // Helper that inserts a dbg.value before \p InsertBefore, copying the
    // location (and possibly the type, if it's non-void) from \p TemplateInst.
    auto insertDbgVal = [&](Instruction &TemplateInst,
                            Instruction *InsertBefore) {
      std::string Name = utostr(NextVar++);
      Value *V = &TemplateInst;
      if (TemplateInst.getType()->isVoidTy())
        V = ConstantInt::get(Int32Ty, 0);
      const DILocation *Loc = TemplateInst.getDebugLoc().get();
      auto LocalVar = DIB.createAutoVariable(SP, Name, File, Loc->getLine(),
                                             getCachedDIType(V->getType()),
                                             /*AlwaysPreserve=*/true);
      DIB.insertDbgValueIntrinsic(V, LocalVar, DIB.createExpression(), Loc,
                                  InsertBefore);
    };

    for (BasicBlock &BB : F) {
      // Attach debug locations.
      for (Instruction &I : BB)
        I.setDebugLoc(DILocation::get(Ctx, NextLine++, 1, SP));

      if (DebugifyLevel < Level::LocationsAndVariables)
        continue;

      // Inserting debug values into EH pads can break IR invariants.
      if (BB.isEHPad())
        continue;

      // Find the terminating instruction, after which no debug values are
      // attached.
      Instruction *LastInst = findTerminatingInstruction(BB);
      assert(LastInst && "Expected basic block with a terminator");

      // Maintain an insertion point which can't be invalidated when updates
      // are made.
      BasicBlock::iterator InsertPt = BB.getFirstInsertionPt();
      assert(InsertPt != BB.end() && "Expected to find an insertion point");
      Instruction *InsertBefore = &*InsertPt;

      // Attach debug values.
      for (Instruction *I = &*BB.begin(); I != LastInst; I = I->getNextNode()) {
        // Skip void-valued instructions.
        if (I->getType()->isVoidTy())
          continue;

        // Phis and EH pads must be grouped at the beginning of the block.
        // Only advance the insertion point when we finish visiting these.
        if (!isa<PHINode>(I) && !I->isEHPad())
          InsertBefore = I->getNextNode();

        insertDbgVal(*I, InsertBefore);
        InsertedDbgVal = true;
      }
    }
    // Make sure we emit at least one dbg.value, otherwise MachineDebugify may
    // not have anything to work with as it goes about inserting DBG_VALUEs.
    // (It's common for MIR tests to be written containing skeletal IR with
    // empty functions -- we're still interested in debugifying the MIR within
    // those tests, and this helps with that.)
    if (DebugifyLevel == Level::LocationsAndVariables && !InsertedDbgVal) {
      auto *Term = findTerminatingInstruction(F.getEntryBlock());
      insertDbgVal(*Term, Term);
    }
    if (ApplyToMF)
      ApplyToMF(DIB, F);
    DIB.finalizeSubprogram(SP);
  }
  DIB.finalize();

  // Track the number of distinct lines and variables.
  NamedMDNode *NMD = M.getOrInsertNamedMetadata("llvm.debugify");
  auto addDebugifyOperand = [&](unsigned N) {
    NMD->addOperand(MDNode::get(
        Ctx, ValueAsMetadata::getConstant(ConstantInt::get(Int32Ty, N))));
  };
  addDebugifyOperand(NextLine - 1); // Original number of lines.
  addDebugifyOperand(NextVar - 1);  // Original number of variables.
  assert(NMD->getNumOperands() == 2 &&
         "llvm.debugify should have exactly 2 operands!");

  // Claim that this synthetic debug info is valid.
  StringRef DIVersionKey = "Debug Info Version";
  if (!M.getModuleFlag(DIVersionKey))
    M.addModuleFlag(Module::Warning, DIVersionKey, DEBUG_METADATA_VERSION);

  return true;
}

static bool
applyDebugify(Function &F,
              enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
              DebugInfoPerPass *DebugInfoBeforePass = nullptr,
              StringRef NameOfWrappedPass = "") {
  Module &M = *F.getParent();
  auto FuncIt = F.getIterator();
  if (Mode == DebugifyMode::SyntheticDebugInfo)
    return applyDebugifyMetadata(M, make_range(FuncIt, std::next(FuncIt)),
                                 "FunctionDebugify: ", /*ApplyToMF*/ nullptr);
  assert(DebugInfoBeforePass);
  return collectDebugInfoMetadata(M, M.functions(), *DebugInfoBeforePass,
                                  "FunctionDebugify (original debuginfo)",
                                  NameOfWrappedPass);
}

static bool
applyDebugify(Module &M,
              enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
              DebugInfoPerPass *DebugInfoBeforePass = nullptr,
              StringRef NameOfWrappedPass = "") {
  if (Mode == DebugifyMode::SyntheticDebugInfo)
    return applyDebugifyMetadata(M, M.functions(),
                                 "ModuleDebugify: ", /*ApplyToMF*/ nullptr);
  return collectDebugInfoMetadata(M, M.functions(), *DebugInfoBeforePass,
                                  "ModuleDebugify (original debuginfo)",
                                  NameOfWrappedPass);
}

bool llvm::stripDebugifyMetadata(Module &M) {
  bool Changed = false;

  // Remove the llvm.debugify and llvm.mir.debugify module-level named metadata.
  NamedMDNode *DebugifyMD = M.getNamedMetadata("llvm.debugify");
  if (DebugifyMD) {
    M.eraseNamedMetadata(DebugifyMD);
    Changed = true;
  }

  if (auto *MIRDebugifyMD = M.getNamedMetadata("llvm.mir.debugify")) {
    M.eraseNamedMetadata(MIRDebugifyMD);
    Changed = true;
  }

  // Strip out all debug intrinsics and supporting metadata (subprograms, types,
  // variables, etc).
  Changed |= StripDebugInfo(M);

  // Strip out the dead dbg.value prototype.
  Function *DbgValF = M.getFunction("llvm.dbg.value");
  if (DbgValF) {
    assert(DbgValF->isDeclaration() && DbgValF->use_empty() &&
           "Not all debug info stripped?");
    DbgValF->eraseFromParent();
    Changed = true;
  }

  // Strip out the module-level Debug Info Version metadata.
  // FIXME: There must be an easier way to remove an operand from a NamedMDNode.
  NamedMDNode *NMD = M.getModuleFlagsMetadata();
  if (!NMD)
    return Changed;
  SmallVector<MDNode *, 4> Flags(NMD->operands());
  NMD->clearOperands();
  for (MDNode *Flag : Flags) {
    auto *Key = cast<MDString>(Flag->getOperand(1));
    if (Key->getString() == "Debug Info Version") {
      Changed = true;
      continue;
    }
    NMD->addOperand(Flag);
  }
  // If we left it empty we might as well remove it.
  if (NMD->getNumOperands() == 0)
    NMD->eraseFromParent();

  return Changed;
}

bool llvm::collectDebugInfoMetadata(Module &M,
                                    iterator_range<Module::iterator> Functions,
                                    DebugInfoPerPass &DebugInfoBeforePass,
                                    StringRef Banner,
                                    StringRef NameOfWrappedPass) {
  LLVM_DEBUG(dbgs() << Banner << ": (before) " << NameOfWrappedPass << '\n');

  if (!M.getNamedMetadata("llvm.dbg.cu")) {
    dbg() << Banner << ": Skipping module without debug info\n";
    return false;
  }

  uint64_t FunctionsCnt = DebugInfoBeforePass.DIFunctions.size();
  // Visit each instruction.
  for (Function &F : Functions) {
    // Use DI collected after previous Pass (when -debugify-each is used).
    if (DebugInfoBeforePass.DIFunctions.count(&F))
      continue;

    if (isFunctionSkipped(F))
      continue;

    // Stop collecting DI if the Functions number reached the limit.
    if (++FunctionsCnt >= DebugifyFunctionsLimit)
      break;
    // Collect the DISubprogram.
    auto *SP = F.getSubprogram();
    DebugInfoBeforePass.DIFunctions.insert({&F, SP});
    if (SP) {
      LLVM_DEBUG(dbgs() << "  Collecting subprogram: " << *SP << '\n');
      for (const DINode *DN : SP->getRetainedNodes()) {
        if (const auto *DV = dyn_cast<DILocalVariable>(DN)) {
          DebugInfoBeforePass.DIVariables[DV] = 0;
        }
      }
    }

    for (BasicBlock &BB : F) {
      // Collect debug locations (!dbg) and debug variable intrinsics.
      for (Instruction &I : BB) {
        // Skip PHIs.
        if (isa<PHINode>(I))
          continue;

        // Cllect dbg.values and dbg.declare.
        if (DebugifyLevel > Level::Locations) {
          auto HandleDbgVariable = [&](auto *DbgVar) {
            if (!SP)
              return;
            // Skip inlined variables.
            if (DbgVar->getDebugLoc().getInlinedAt())
              return;
            // Skip undef values.
            if (DbgVar->isKillLocation())
              return;

            auto *Var = DbgVar->getVariable();
            DebugInfoBeforePass.DIVariables[Var]++;
          };
          for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
            HandleDbgVariable(&DVR);
          if (auto *DVI = dyn_cast<DbgVariableIntrinsic>(&I))
            HandleDbgVariable(DVI);
        }

        // Skip debug instructions other than dbg.value and dbg.declare.
        if (isa<DbgInfoIntrinsic>(&I))
          continue;

        LLVM_DEBUG(dbgs() << "  Collecting info for inst: " << I << '\n');
        DebugInfoBeforePass.InstToDelete.insert({&I, &I});

        const DILocation *Loc = I.getDebugLoc().get();
        bool HasLoc = Loc != nullptr;
        DebugInfoBeforePass.DILocations.insert({&I, HasLoc});
      }
    }
  }

  return true;
}

// This checks the preservation of original debug info attached to functions.
static bool checkFunctions(const DebugFnMap &DIFunctionsBefore,
                           const DebugFnMap &DIFunctionsAfter,
                           StringRef NameOfWrappedPass,
                           StringRef FileNameFromCU, bool ShouldWriteIntoJSON,
                           llvm::json::Array &Bugs) {
  bool Preserved = true;
  for (const auto &F : DIFunctionsAfter) {
    if (F.second)
      continue;
    auto SPIt = DIFunctionsBefore.find(F.first);
    if (SPIt == DIFunctionsBefore.end()) {
      if (ShouldWriteIntoJSON)
        Bugs.push_back(llvm::json::Object({{"metadata", "DISubprogram"},
                                           {"name", F.first->getName()},
                                           {"action", "not-generate"}}));
      else
        dbg() << "ERROR: " << NameOfWrappedPass
              << " did not generate DISubprogram for " << F.first->getName()
              << " from " << FileNameFromCU << '\n';
      Preserved = false;
    } else {
      auto SP = SPIt->second;
      if (!SP)
        continue;
      // If the function had the SP attached before the pass, consider it as
      // a debug info bug.
      if (ShouldWriteIntoJSON)
        Bugs.push_back(llvm::json::Object({{"metadata", "DISubprogram"},
                                           {"name", F.first->getName()},
                                           {"action", "drop"}}));
      else
        dbg() << "ERROR: " << NameOfWrappedPass << " dropped DISubprogram of "
              << F.first->getName() << " from " << FileNameFromCU << '\n';
      Preserved = false;
    }
  }

  return Preserved;
}

// This checks the preservation of the original debug info attached to
// instructions.
static bool checkInstructions(const DebugInstMap &DILocsBefore,
                              const DebugInstMap &DILocsAfter,
                              const WeakInstValueMap &InstToDelete,
                              StringRef NameOfWrappedPass,
                              StringRef FileNameFromCU,
                              bool ShouldWriteIntoJSON,
                              llvm::json::Array &Bugs) {
  bool Preserved = true;
  for (const auto &L : DILocsAfter) {
    if (L.second)
      continue;
    auto Instr = L.first;

    // In order to avoid pointer reuse/recycling, skip the values that might
    // have been deleted during a pass.
    auto WeakInstrPtr = InstToDelete.find(Instr);
    if (WeakInstrPtr != InstToDelete.end() && !WeakInstrPtr->second)
      continue;

    auto FnName = Instr->getFunction()->getName();
    auto BB = Instr->getParent();
    auto BBName = BB->hasName() ? BB->getName() : "no-name";
    auto InstName = Instruction::getOpcodeName(Instr->getOpcode());

    auto InstrIt = DILocsBefore.find(Instr);
    if (InstrIt == DILocsBefore.end()) {
      if (ShouldWriteIntoJSON)
        Bugs.push_back(llvm::json::Object({{"metadata", "DILocation"},
                                           {"fn-name", FnName.str()},
                                           {"bb-name", BBName.str()},
                                           {"instr", InstName},
                                           {"action", "not-generate"}}));
      else
        dbg() << "WARNING: " << NameOfWrappedPass
              << " did not generate DILocation for " << *Instr
              << " (BB: " << BBName << ", Fn: " << FnName
              << ", File: " << FileNameFromCU << ")\n";
      Preserved = false;
    } else {
      if (!InstrIt->second)
        continue;
      // If the instr had the !dbg attached before the pass, consider it as
      // a debug info issue.
      if (ShouldWriteIntoJSON)
        Bugs.push_back(llvm::json::Object({{"metadata", "DILocation"},
                                           {"fn-name", FnName.str()},
                                           {"bb-name", BBName.str()},
                                           {"instr", InstName},
                                           {"action", "drop"}}));
      else
        dbg() << "WARNING: " << NameOfWrappedPass << " dropped DILocation of "
              << *Instr << " (BB: " << BBName << ", Fn: " << FnName
              << ", File: " << FileNameFromCU << ")\n";
      Preserved = false;
    }
  }

  return Preserved;
}

// This checks the preservation of original debug variable intrinsics.
static bool checkVars(const DebugVarMap &DIVarsBefore,
                      const DebugVarMap &DIVarsAfter,
                      StringRef NameOfWrappedPass, StringRef FileNameFromCU,
                      bool ShouldWriteIntoJSON, llvm::json::Array &Bugs) {
  bool Preserved = true;
  for (const auto &V : DIVarsBefore) {
    auto VarIt = DIVarsAfter.find(V.first);
    if (VarIt == DIVarsAfter.end())
      continue;

    unsigned NumOfDbgValsAfter = VarIt->second;

    if (V.second > NumOfDbgValsAfter) {
      if (ShouldWriteIntoJSON)
        Bugs.push_back(llvm::json::Object(
            {{"metadata", "dbg-var-intrinsic"},
             {"name", V.first->getName()},
             {"fn-name", V.first->getScope()->getSubprogram()->getName()},
             {"action", "drop"}}));
      else
        dbg() << "WARNING: " << NameOfWrappedPass
              << " drops dbg.value()/dbg.declare() for " << V.first->getName()
              << " from "
              << "function " << V.first->getScope()->getSubprogram()->getName()
              << " (file " << FileNameFromCU << ")\n";
      Preserved = false;
    }
  }

  return Preserved;
}

// Write the json data into the specifed file.
static void writeJSON(StringRef OrigDIVerifyBugsReportFilePath,
                      StringRef FileNameFromCU, StringRef NameOfWrappedPass,
                      llvm::json::Array &Bugs) {
  std::error_code EC;
  raw_fd_ostream OS_FILE{OrigDIVerifyBugsReportFilePath, EC,
                         sys::fs::OF_Append | sys::fs::OF_TextWithCRLF};
  if (EC) {
    errs() << "Could not open file: " << EC.message() << ", "
           << OrigDIVerifyBugsReportFilePath << '\n';
    return;
  }

  if (auto L = OS_FILE.lock()) {
    OS_FILE << "{\"file\":\"" << FileNameFromCU << "\", ";

    StringRef PassName =
        NameOfWrappedPass != "" ? NameOfWrappedPass : "no-name";
    OS_FILE << "\"pass\":\"" << PassName << "\", ";

    llvm::json::Value BugsToPrint{std::move(Bugs)};
    OS_FILE << "\"bugs\": " << BugsToPrint;

    OS_FILE << "}\n";
  }
  OS_FILE.close();
}

bool llvm::checkDebugInfoMetadata(Module &M,
                                  iterator_range<Module::iterator> Functions,
                                  DebugInfoPerPass &DebugInfoBeforePass,
                                  StringRef Banner, StringRef NameOfWrappedPass,
                                  StringRef OrigDIVerifyBugsReportFilePath) {
  LLVM_DEBUG(dbgs() << Banner << ": (after) " << NameOfWrappedPass << '\n');

  if (!M.getNamedMetadata("llvm.dbg.cu")) {
    dbg() << Banner << ": Skipping module without debug info\n";
    return false;
  }

  // Map the debug info holding DIs after a pass.
  DebugInfoPerPass DebugInfoAfterPass;

  // Visit each instruction.
  for (Function &F : Functions) {
    if (isFunctionSkipped(F))
      continue;

    // Don't process functions without DI collected before the Pass.
    if (!DebugInfoBeforePass.DIFunctions.count(&F))
      continue;
    // TODO: Collect metadata other than DISubprograms.
    // Collect the DISubprogram.
    auto *SP = F.getSubprogram();
    DebugInfoAfterPass.DIFunctions.insert({&F, SP});

    if (SP) {
      LLVM_DEBUG(dbgs() << "  Collecting subprogram: " << *SP << '\n');
      for (const DINode *DN : SP->getRetainedNodes()) {
        if (const auto *DV = dyn_cast<DILocalVariable>(DN)) {
          DebugInfoAfterPass.DIVariables[DV] = 0;
        }
      }
    }

    for (BasicBlock &BB : F) {
      // Collect debug locations (!dbg) and debug variable intrinsics.
      for (Instruction &I : BB) {
        // Skip PHIs.
        if (isa<PHINode>(I))
          continue;

        // Collect dbg.values and dbg.declares.
        if (DebugifyLevel > Level::Locations) {
          auto HandleDbgVariable = [&](auto *DbgVar) {
            if (!SP)
              return;
            // Skip inlined variables.
            if (DbgVar->getDebugLoc().getInlinedAt())
              return;
            // Skip undef values.
            if (DbgVar->isKillLocation())
              return;

            auto *Var = DbgVar->getVariable();
            DebugInfoAfterPass.DIVariables[Var]++;
          };
          for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
            HandleDbgVariable(&DVR);
          if (auto *DVI = dyn_cast<DbgVariableIntrinsic>(&I))
            HandleDbgVariable(DVI);
        }

        // Skip debug instructions other than dbg.value and dbg.declare.
        if (isa<DbgInfoIntrinsic>(&I))
          continue;

        LLVM_DEBUG(dbgs() << "  Collecting info for inst: " << I << '\n');

        const DILocation *Loc = I.getDebugLoc().get();
        bool HasLoc = Loc != nullptr;

        DebugInfoAfterPass.DILocations.insert({&I, HasLoc});
      }
    }
  }

  // TODO: The name of the module could be read better?
  StringRef FileNameFromCU =
      (cast<DICompileUnit>(M.getNamedMetadata("llvm.dbg.cu")->getOperand(0)))
          ->getFilename();

  auto DIFunctionsBefore = DebugInfoBeforePass.DIFunctions;
  auto DIFunctionsAfter = DebugInfoAfterPass.DIFunctions;

  auto DILocsBefore = DebugInfoBeforePass.DILocations;
  auto DILocsAfter = DebugInfoAfterPass.DILocations;

  auto InstToDelete = DebugInfoBeforePass.InstToDelete;

  auto DIVarsBefore = DebugInfoBeforePass.DIVariables;
  auto DIVarsAfter = DebugInfoAfterPass.DIVariables;

  bool ShouldWriteIntoJSON = !OrigDIVerifyBugsReportFilePath.empty();
  llvm::json::Array Bugs;

  bool ResultForFunc =
      checkFunctions(DIFunctionsBefore, DIFunctionsAfter, NameOfWrappedPass,
                     FileNameFromCU, ShouldWriteIntoJSON, Bugs);
  bool ResultForInsts = checkInstructions(
      DILocsBefore, DILocsAfter, InstToDelete, NameOfWrappedPass,
      FileNameFromCU, ShouldWriteIntoJSON, Bugs);

  bool ResultForVars = checkVars(DIVarsBefore, DIVarsAfter, NameOfWrappedPass,
                                 FileNameFromCU, ShouldWriteIntoJSON, Bugs);

  bool Result = ResultForFunc && ResultForInsts && ResultForVars;

  StringRef ResultBanner = NameOfWrappedPass != "" ? NameOfWrappedPass : Banner;
  if (ShouldWriteIntoJSON && !Bugs.empty())
    writeJSON(OrigDIVerifyBugsReportFilePath, FileNameFromCU, NameOfWrappedPass,
              Bugs);

  if (Result)
    dbg() << ResultBanner << ": PASS\n";
  else
    dbg() << ResultBanner << ": FAIL\n";

  // In the case of the `debugify-each`, no need to go over all the instructions
  // again in the collectDebugInfoMetadata(), since as an input we can use
  // the debugging information from the previous pass.
  DebugInfoBeforePass = DebugInfoAfterPass;

  LLVM_DEBUG(dbgs() << "\n\n");
  return Result;
}

namespace {
/// Return true if a mis-sized diagnostic is issued for \p DbgVal.
template <typename DbgValTy>
bool diagnoseMisSizedDbgValue(Module &M, DbgValTy *DbgVal) {
  // The size of a dbg.value's value operand should match the size of the
  // variable it corresponds to.
  //
  // TODO: This, along with a check for non-null value operands, should be
  // promoted to verifier failures.

  // For now, don't try to interpret anything more complicated than an empty
  // DIExpression. Eventually we should try to handle OP_deref and fragments.
  if (DbgVal->getExpression()->getNumElements())
    return false;

  Value *V = DbgVal->getVariableLocationOp(0);
  if (!V)
    return false;

  Type *Ty = V->getType();
  uint64_t ValueOperandSize = getAllocSizeInBits(M, Ty);
  std::optional<uint64_t> DbgVarSize = DbgVal->getFragmentSizeInBits();
  if (!ValueOperandSize || !DbgVarSize)
    return false;

  bool HasBadSize = false;
  if (Ty->isIntegerTy()) {
    auto Signedness = DbgVal->getVariable()->getSignedness();
    if (Signedness && *Signedness == DIBasicType::Signedness::Signed)
      HasBadSize = ValueOperandSize < *DbgVarSize;
  } else {
    HasBadSize = ValueOperandSize != *DbgVarSize;
  }

  if (HasBadSize) {
    dbg() << "ERROR: dbg.value operand has size " << ValueOperandSize
          << ", but its variable has size " << *DbgVarSize << ": ";
    DbgVal->print(dbg());
    dbg() << "\n";
  }
  return HasBadSize;
}

bool checkDebugifyMetadata(Module &M,
                           iterator_range<Module::iterator> Functions,
                           StringRef NameOfWrappedPass, StringRef Banner,
                           bool Strip, DebugifyStatsMap *StatsMap) {
  // Skip modules without debugify metadata.
  NamedMDNode *NMD = M.getNamedMetadata("llvm.debugify");
  if (!NMD) {
    dbg() << Banner << ": Skipping module without debugify metadata\n";
    return false;
  }

  auto getDebugifyOperand = [&](unsigned Idx) -> unsigned {
    return mdconst::extract<ConstantInt>(NMD->getOperand(Idx)->getOperand(0))
        ->getZExtValue();
  };
  assert(NMD->getNumOperands() == 2 &&
         "llvm.debugify should have exactly 2 operands!");
  unsigned OriginalNumLines = getDebugifyOperand(0);
  unsigned OriginalNumVars = getDebugifyOperand(1);
  bool HasErrors = false;

  // Track debug info loss statistics if able.
  DebugifyStatistics *Stats = nullptr;
  if (StatsMap && !NameOfWrappedPass.empty())
    Stats = &StatsMap->operator[](NameOfWrappedPass);

  BitVector MissingLines{OriginalNumLines, true};
  BitVector MissingVars{OriginalNumVars, true};
  for (Function &F : Functions) {
    if (isFunctionSkipped(F))
      continue;

    // Find missing lines.
    for (Instruction &I : instructions(F)) {
      if (isa<DbgValueInst>(&I))
        continue;

      auto DL = I.getDebugLoc();
      if (DL && DL.getLine() != 0) {
        MissingLines.reset(DL.getLine() - 1);
        continue;
      }

      if (!isa<PHINode>(&I) && !DL) {
        dbg() << "WARNING: Instruction with empty DebugLoc in function ";
        dbg() << F.getName() << " --";
        I.print(dbg());
        dbg() << "\n";
      }
    }

    // Find missing variables and mis-sized debug values.
    auto CheckForMisSized = [&](auto *DbgVal) {
      unsigned Var = ~0U;
      (void)to_integer(DbgVal->getVariable()->getName(), Var, 10);
      assert(Var <= OriginalNumVars && "Unexpected name for DILocalVariable");
      bool HasBadSize = diagnoseMisSizedDbgValue(M, DbgVal);
      if (!HasBadSize)
        MissingVars.reset(Var - 1);
      HasErrors |= HasBadSize;
    };
    for (Instruction &I : instructions(F)) {
      for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
        if (DVR.isDbgValue() || DVR.isDbgAssign())
          CheckForMisSized(&DVR);
      auto *DVI = dyn_cast<DbgValueInst>(&I);
      if (!DVI)
        continue;
      CheckForMisSized(DVI);
    }
  }

  // Print the results.
  for (unsigned Idx : MissingLines.set_bits())
    dbg() << "WARNING: Missing line " << Idx + 1 << "\n";

  for (unsigned Idx : MissingVars.set_bits())
    dbg() << "WARNING: Missing variable " << Idx + 1 << "\n";

  // Update DI loss statistics.
  if (Stats) {
    Stats->NumDbgLocsExpected += OriginalNumLines;
    Stats->NumDbgLocsMissing += MissingLines.count();
    Stats->NumDbgValuesExpected += OriginalNumVars;
    Stats->NumDbgValuesMissing += MissingVars.count();
  }

  dbg() << Banner;
  if (!NameOfWrappedPass.empty())
    dbg() << " [" << NameOfWrappedPass << "]";
  dbg() << ": " << (HasErrors ? "FAIL" : "PASS") << '\n';

  // Strip debugify metadata if required.
  bool Ret = false;
  if (Strip)
    Ret = stripDebugifyMetadata(M);

  return Ret;
}

/// ModulePass for attaching synthetic debug info to everything, used with the
/// legacy module pass manager.
struct DebugifyModulePass : public ModulePass {
  bool runOnModule(Module &M) override {
    bool Result =
        applyDebugify(M, Mode, DebugInfoBeforePass, NameOfWrappedPass);
    return Result;
  }

  DebugifyModulePass(enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
                     StringRef NameOfWrappedPass = "",
                     DebugInfoPerPass *DebugInfoBeforePass = nullptr)
      : ModulePass(ID), NameOfWrappedPass(NameOfWrappedPass),
        DebugInfoBeforePass(DebugInfoBeforePass), Mode(Mode) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  static char ID; // Pass identification.

private:
  StringRef NameOfWrappedPass;
  DebugInfoPerPass *DebugInfoBeforePass;
  enum DebugifyMode Mode;
};

/// FunctionPass for attaching synthetic debug info to instructions within a
/// single function, used with the legacy module pass manager.
struct DebugifyFunctionPass : public FunctionPass {
  bool runOnFunction(Function &F) override {
    bool Result =
        applyDebugify(F, Mode, DebugInfoBeforePass, NameOfWrappedPass);
    return Result;
  }

  DebugifyFunctionPass(
      enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
      StringRef NameOfWrappedPass = "",
      DebugInfoPerPass *DebugInfoBeforePass = nullptr)
      : FunctionPass(ID), NameOfWrappedPass(NameOfWrappedPass),
        DebugInfoBeforePass(DebugInfoBeforePass), Mode(Mode) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  static char ID; // Pass identification.

private:
  StringRef NameOfWrappedPass;
  DebugInfoPerPass *DebugInfoBeforePass;
  enum DebugifyMode Mode;
};

/// ModulePass for checking debug info inserted by -debugify, used with the
/// legacy module pass manager.
struct CheckDebugifyModulePass : public ModulePass {
  bool runOnModule(Module &M) override {
    bool Result;
    if (Mode == DebugifyMode::SyntheticDebugInfo)
      Result = checkDebugifyMetadata(M, M.functions(), NameOfWrappedPass,
                                   "CheckModuleDebugify", Strip, StatsMap);
    else
      Result = checkDebugInfoMetadata(
        M, M.functions(), *DebugInfoBeforePass,
        "CheckModuleDebugify (original debuginfo)", NameOfWrappedPass,
        OrigDIVerifyBugsReportFilePath);

    return Result;
  }

  CheckDebugifyModulePass(
      bool Strip = false, StringRef NameOfWrappedPass = "",
      DebugifyStatsMap *StatsMap = nullptr,
      enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
      DebugInfoPerPass *DebugInfoBeforePass = nullptr,
      StringRef OrigDIVerifyBugsReportFilePath = "")
      : ModulePass(ID), NameOfWrappedPass(NameOfWrappedPass),
        OrigDIVerifyBugsReportFilePath(OrigDIVerifyBugsReportFilePath),
        StatsMap(StatsMap), DebugInfoBeforePass(DebugInfoBeforePass), Mode(Mode),
        Strip(Strip) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  static char ID; // Pass identification.

private:
  StringRef NameOfWrappedPass;
  StringRef OrigDIVerifyBugsReportFilePath;
  DebugifyStatsMap *StatsMap;
  DebugInfoPerPass *DebugInfoBeforePass;
  enum DebugifyMode Mode;
  bool Strip;
};

/// FunctionPass for checking debug info inserted by -debugify-function, used
/// with the legacy module pass manager.
struct CheckDebugifyFunctionPass : public FunctionPass {
  bool runOnFunction(Function &F) override {
    Module &M = *F.getParent();
    auto FuncIt = F.getIterator();
    bool Result;
    if (Mode == DebugifyMode::SyntheticDebugInfo)
      Result = checkDebugifyMetadata(M, make_range(FuncIt, std::next(FuncIt)),
                                   NameOfWrappedPass, "CheckFunctionDebugify",
                                   Strip, StatsMap);
    else
      Result = checkDebugInfoMetadata(
        M, make_range(FuncIt, std::next(FuncIt)), *DebugInfoBeforePass,
        "CheckFunctionDebugify (original debuginfo)", NameOfWrappedPass,
        OrigDIVerifyBugsReportFilePath);

    return Result;
  }

  CheckDebugifyFunctionPass(
      bool Strip = false, StringRef NameOfWrappedPass = "",
      DebugifyStatsMap *StatsMap = nullptr,
      enum DebugifyMode Mode = DebugifyMode::SyntheticDebugInfo,
      DebugInfoPerPass *DebugInfoBeforePass = nullptr,
      StringRef OrigDIVerifyBugsReportFilePath = "")
      : FunctionPass(ID), NameOfWrappedPass(NameOfWrappedPass),
        OrigDIVerifyBugsReportFilePath(OrigDIVerifyBugsReportFilePath),
        StatsMap(StatsMap), DebugInfoBeforePass(DebugInfoBeforePass), Mode(Mode),
        Strip(Strip) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  static char ID; // Pass identification.

private:
  StringRef NameOfWrappedPass;
  StringRef OrigDIVerifyBugsReportFilePath;
  DebugifyStatsMap *StatsMap;
  DebugInfoPerPass *DebugInfoBeforePass;
  enum DebugifyMode Mode;
  bool Strip;
};

} // end anonymous namespace

void llvm::exportDebugifyStats(StringRef Path, const DebugifyStatsMap &Map) {
  std::error_code EC;
  raw_fd_ostream OS{Path, EC};
  if (EC) {
    errs() << "Could not open file: " << EC.message() << ", " << Path << '\n';
    return;
  }

  OS << "Pass Name" << ',' << "# of missing debug values" << ','
     << "# of missing locations" << ',' << "Missing/Expected value ratio" << ','
     << "Missing/Expected location ratio" << '\n';
  for (const auto &Entry : Map) {
    StringRef Pass = Entry.first;
    DebugifyStatistics Stats = Entry.second;

    OS << Pass << ',' << Stats.NumDbgValuesMissing << ','
       << Stats.NumDbgLocsMissing << ',' << Stats.getMissingValueRatio() << ','
       << Stats.getEmptyLocationRatio() << '\n';
  }
}

ModulePass *createDebugifyModulePass(enum DebugifyMode Mode,
                                     llvm::StringRef NameOfWrappedPass,
                                     DebugInfoPerPass *DebugInfoBeforePass) {
  if (Mode == DebugifyMode::SyntheticDebugInfo)
    return new DebugifyModulePass();
  assert(Mode == DebugifyMode::OriginalDebugInfo && "Must be original mode");
  return new DebugifyModulePass(Mode, NameOfWrappedPass, DebugInfoBeforePass);
}

FunctionPass *
createDebugifyFunctionPass(enum DebugifyMode Mode,
                           llvm::StringRef NameOfWrappedPass,
                           DebugInfoPerPass *DebugInfoBeforePass) {
  if (Mode == DebugifyMode::SyntheticDebugInfo)
    return new DebugifyFunctionPass();
  assert(Mode == DebugifyMode::OriginalDebugInfo && "Must be original mode");
  return new DebugifyFunctionPass(Mode, NameOfWrappedPass, DebugInfoBeforePass);
}

PreservedAnalyses NewPMDebugifyPass::run(Module &M, ModuleAnalysisManager &) {
  if (Mode == DebugifyMode::SyntheticDebugInfo)
    applyDebugifyMetadata(M, M.functions(),
                          "ModuleDebugify: ", /*ApplyToMF*/ nullptr);
  else
    collectDebugInfoMetadata(M, M.functions(), *DebugInfoBeforePass,
                             "ModuleDebugify (original debuginfo)",
                              NameOfWrappedPass);

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

ModulePass *createCheckDebugifyModulePass(
    bool Strip, StringRef NameOfWrappedPass, DebugifyStatsMap *StatsMap,
    enum DebugifyMode Mode, DebugInfoPerPass *DebugInfoBeforePass,
    StringRef OrigDIVerifyBugsReportFilePath) {
  if (Mode == DebugifyMode::SyntheticDebugInfo)
    return new CheckDebugifyModulePass(Strip, NameOfWrappedPass, StatsMap);
  assert(Mode == DebugifyMode::OriginalDebugInfo && "Must be original mode");
  return new CheckDebugifyModulePass(false, NameOfWrappedPass, nullptr, Mode,
                                     DebugInfoBeforePass,
                                     OrigDIVerifyBugsReportFilePath);
}

FunctionPass *createCheckDebugifyFunctionPass(
    bool Strip, StringRef NameOfWrappedPass, DebugifyStatsMap *StatsMap,
    enum DebugifyMode Mode, DebugInfoPerPass *DebugInfoBeforePass,
    StringRef OrigDIVerifyBugsReportFilePath) {
  if (Mode == DebugifyMode::SyntheticDebugInfo)
    return new CheckDebugifyFunctionPass(Strip, NameOfWrappedPass, StatsMap);
  assert(Mode == DebugifyMode::OriginalDebugInfo && "Must be original mode");
  return new CheckDebugifyFunctionPass(false, NameOfWrappedPass, nullptr, Mode,
                                       DebugInfoBeforePass,
                                       OrigDIVerifyBugsReportFilePath);
}

PreservedAnalyses NewPMCheckDebugifyPass::run(Module &M,
                                              ModuleAnalysisManager &) {
  if (Mode == DebugifyMode::SyntheticDebugInfo)
    checkDebugifyMetadata(M, M.functions(), NameOfWrappedPass,
                                   "CheckModuleDebugify", Strip, StatsMap);
  else
    checkDebugInfoMetadata(
      M, M.functions(), *DebugInfoBeforePass,
      "CheckModuleDebugify (original debuginfo)", NameOfWrappedPass,
      OrigDIVerifyBugsReportFilePath);

  return PreservedAnalyses::all();
}

static bool isIgnoredPass(StringRef PassID) {
  return isSpecialPass(PassID, {"PassManager", "PassAdaptor",
                                "AnalysisManagerProxy", "PrintFunctionPass",
                                "PrintModulePass", "BitcodeWriterPass",
                                "ThinLTOBitcodeWriterPass", "VerifierPass"});
}

void DebugifyEachInstrumentation::registerCallbacks(
    PassInstrumentationCallbacks &PIC, ModuleAnalysisManager &MAM) {
  PIC.registerBeforeNonSkippedPassCallback([this, &MAM](StringRef P, Any IR) {
    if (isIgnoredPass(P))
      return;
    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    if (const auto **CF = llvm::any_cast<const Function *>(&IR)) {
      Function &F = *const_cast<Function *>(*CF);
      applyDebugify(F, Mode, DebugInfoBeforePass, P);
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(*F.getParent())
          .getManager()
          .invalidate(F, PA);
    } else if (const auto **CM = llvm::any_cast<const Module *>(&IR)) {
      Module &M = *const_cast<Module *>(*CM);
      applyDebugify(M, Mode, DebugInfoBeforePass, P);
      MAM.invalidate(M, PA);
    }
  });
  PIC.registerAfterPassCallback(
      [this, &MAM](StringRef P, Any IR, const PreservedAnalyses &PassPA) {
        if (isIgnoredPass(P))
          return;
        PreservedAnalyses PA;
        PA.preserveSet<CFGAnalyses>();
        if (const auto **CF = llvm::any_cast<const Function *>(&IR)) {
          auto &F = *const_cast<Function *>(*CF);
          Module &M = *F.getParent();
          auto It = F.getIterator();
          if (Mode == DebugifyMode::SyntheticDebugInfo)
            checkDebugifyMetadata(M, make_range(It, std::next(It)), P,
                                  "CheckFunctionDebugify", /*Strip=*/true,
                                  DIStatsMap);
          else
            checkDebugInfoMetadata(M, make_range(It, std::next(It)),
                                   *DebugInfoBeforePass,
                                   "CheckModuleDebugify (original debuginfo)",
                                   P, OrigDIVerifyBugsReportFilePath);
          MAM.getResult<FunctionAnalysisManagerModuleProxy>(*F.getParent())
              .getManager()
              .invalidate(F, PA);
        } else if (const auto **CM = llvm::any_cast<const Module *>(&IR)) {
          Module &M = *const_cast<Module *>(*CM);
          if (Mode == DebugifyMode::SyntheticDebugInfo)
            checkDebugifyMetadata(M, M.functions(), P, "CheckModuleDebugify",
                                  /*Strip=*/true, DIStatsMap);
          else
            checkDebugInfoMetadata(M, M.functions(), *DebugInfoBeforePass,
                                   "CheckModuleDebugify (original debuginfo)",
                                   P, OrigDIVerifyBugsReportFilePath);
          MAM.invalidate(M, PA);
        }
      });
}

char DebugifyModulePass::ID = 0;
static RegisterPass<DebugifyModulePass> DM("debugify",
                                           "Attach debug info to everything");

char CheckDebugifyModulePass::ID = 0;
static RegisterPass<CheckDebugifyModulePass>
    CDM("check-debugify", "Check debug info from -debugify");

char DebugifyFunctionPass::ID = 0;
static RegisterPass<DebugifyFunctionPass> DF("debugify-function",
                                             "Attach debug info to a function");

char CheckDebugifyFunctionPass::ID = 0;
static RegisterPass<CheckDebugifyFunctionPass>
    CDF("check-debugify-function", "Check debug info from -debugify-function");
