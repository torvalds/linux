//===- verify-uselistorder.cpp - The LLVM Modular Optimizer ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Verify that use-list order can be serialized correctly.  After reading the
// provided IR, this tool shuffles the use-lists and then writes and reads to a
// separate Module whose use-list orders are compared to the original.
//
// The shuffles are deterministic, but guarantee that use-lists will change.
// The algorithm per iteration is as follows:
//
//  1. Seed the random number generator.  The seed is different for each
//     shuffle.  Shuffle 0 uses default+0, shuffle 1 uses default+1, and so on.
//
//  2. Visit every Value in a deterministic order.
//
//  3. Assign a random number to each Use in the Value's use-list in order.
//
//  4. If the numbers are already in order, reassign numbers until they aren't.
//
//  5. Sort the use-list using Value::sortUseList(), which is a stable sort.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/UseListOrder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/raw_ostream.h"
#include <random>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "uselistorder"

static cl::OptionCategory Cat("verify-uselistorder Options");

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode file>"),
                                          cl::init("-"),
                                          cl::value_desc("filename"));

static cl::opt<bool> SaveTemps("save-temps", cl::desc("Save temp files"),
                               cl::cat(Cat));

static cl::opt<unsigned>
    NumShuffles("num-shuffles",
                cl::desc("Number of times to shuffle and verify use-lists"),
                cl::init(1), cl::cat(Cat));

extern cl::opt<cl::boolOrDefault> PreserveInputDbgFormat;

namespace {

struct TempFile {
  std::string Filename;
  FileRemover Remover;
  bool init(const std::string &Ext);
  bool writeBitcode(const Module &M) const;
  bool writeAssembly(const Module &M) const;
  std::unique_ptr<Module> readBitcode(LLVMContext &Context) const;
  std::unique_ptr<Module> readAssembly(LLVMContext &Context) const;
};

struct ValueMapping {
  DenseMap<const Value *, unsigned> IDs;
  std::vector<const Value *> Values;

  /// Construct a value mapping for module.
  ///
  /// Creates mapping from every value in \c M to an ID.  This mapping includes
  /// un-referencable values.
  ///
  /// Every \a Value that gets serialized in some way should be represented
  /// here.  The order needs to be deterministic, but it's unnecessary to match
  /// the value-ids in the bitcode writer.
  ///
  /// All constants that are referenced by other values are included in the
  /// mapping, but others -- which wouldn't be serialized -- are not.
  ValueMapping(const Module &M);

  /// Map a value.
  ///
  /// Maps a value.  If it's a constant, maps all of its operands first.
  void map(const Value *V);
  unsigned lookup(const Value *V) const { return IDs.lookup(V); }
};

} // end namespace

bool TempFile::init(const std::string &Ext) {
  SmallVector<char, 64> Vector;
  LLVM_DEBUG(dbgs() << " - create-temp-file\n");
  if (auto EC = sys::fs::createTemporaryFile("uselistorder", Ext, Vector)) {
    errs() << "verify-uselistorder: error: " << EC.message() << "\n";
    return true;
  }
  assert(!Vector.empty());

  Filename.assign(Vector.data(), Vector.data() + Vector.size());
  Remover.setFile(Filename, !SaveTemps);
  if (SaveTemps)
    outs() << " - filename = " << Filename << "\n";
  return false;
}

bool TempFile::writeBitcode(const Module &M) const {
  LLVM_DEBUG(dbgs() << " - write bitcode\n");
  std::error_code EC;
  raw_fd_ostream OS(Filename, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "verify-uselistorder: error: " << EC.message() << "\n";
    return true;
  }

  WriteBitcodeToFile(M, OS, /* ShouldPreserveUseListOrder */ true);
  return false;
}

bool TempFile::writeAssembly(const Module &M) const {
  LLVM_DEBUG(dbgs() << " - write assembly\n");
  std::error_code EC;
  raw_fd_ostream OS(Filename, EC, sys::fs::OF_TextWithCRLF);
  if (EC) {
    errs() << "verify-uselistorder: error: " << EC.message() << "\n";
    return true;
  }

  M.print(OS, nullptr, /* ShouldPreserveUseListOrder */ true);
  return false;
}

std::unique_ptr<Module> TempFile::readBitcode(LLVMContext &Context) const {
  LLVM_DEBUG(dbgs() << " - read bitcode\n");
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOr =
      MemoryBuffer::getFile(Filename);
  if (!BufferOr) {
    errs() << "verify-uselistorder: error: " << BufferOr.getError().message()
           << "\n";
    return nullptr;
  }

  MemoryBuffer *Buffer = BufferOr.get().get();
  Expected<std::unique_ptr<Module>> ModuleOr =
      parseBitcodeFile(Buffer->getMemBufferRef(), Context);
  if (!ModuleOr) {
    logAllUnhandledErrors(ModuleOr.takeError(), errs(),
                          "verify-uselistorder: error: ");
    return nullptr;
  }

  return std::move(ModuleOr.get());
}

std::unique_ptr<Module> TempFile::readAssembly(LLVMContext &Context) const {
  LLVM_DEBUG(dbgs() << " - read assembly\n");
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyFile(Filename, Err, Context);
  if (!M)
    Err.print("verify-uselistorder", errs());
  return M;
}

ValueMapping::ValueMapping(const Module &M) {
  // Every value should be mapped, including things like void instructions and
  // basic blocks that are kept out of the ValueEnumerator.
  //
  // The current mapping order makes it easier to debug the tables.  It happens
  // to be similar to the ID mapping when writing ValueEnumerator, but they
  // aren't (and needn't be) in sync.

  // Globals.
  for (const GlobalVariable &G : M.globals())
    map(&G);
  for (const GlobalAlias &A : M.aliases())
    map(&A);
  for (const GlobalIFunc &IF : M.ifuncs())
    map(&IF);
  for (const Function &F : M)
    map(&F);

  // Constants used by globals.
  for (const GlobalVariable &G : M.globals())
    if (G.hasInitializer())
      map(G.getInitializer());
  for (const GlobalAlias &A : M.aliases())
    map(A.getAliasee());
  for (const GlobalIFunc &IF : M.ifuncs())
    map(IF.getResolver());
  for (const Function &F : M)
    for (Value *Op : F.operands())
      map(Op);

  // Function bodies.
  for (const Function &F : M) {
    for (const Argument &A : F.args())
      map(&A);
    for (const BasicBlock &BB : F)
      map(&BB);
    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB)
        map(&I);

    // Constants used by instructions.
    for (const BasicBlock &BB : F) {
      for (const Instruction &I : BB) {
        for (const DbgVariableRecord &DVR :
             filterDbgVars(I.getDbgRecordRange())) {
          for (Value *Op : DVR.location_ops())
            map(Op);
          if (DVR.isDbgAssign())
            map(DVR.getAddress());
        }
        for (const Value *Op : I.operands()) {
          // Look through a metadata wrapper.
          if (const auto *MAV = dyn_cast<MetadataAsValue>(Op))
            if (const auto *VAM = dyn_cast<ValueAsMetadata>(MAV->getMetadata()))
              Op = VAM->getValue();

          if ((isa<Constant>(Op) && !isa<GlobalValue>(*Op)) ||
              isa<InlineAsm>(Op))
            map(Op);
        }
      }
    }
  }
}

void ValueMapping::map(const Value *V) {
  if (IDs.lookup(V))
    return;

  if (auto *C = dyn_cast<Constant>(V))
    if (!isa<GlobalValue>(C))
      for (const Value *Op : C->operands())
        map(Op);

  Values.push_back(V);
  IDs[V] = Values.size();
}

#ifndef NDEBUG
static void dumpMapping(const ValueMapping &VM) {
  dbgs() << "value-mapping (size = " << VM.Values.size() << "):\n";
  for (unsigned I = 0, E = VM.Values.size(); I != E; ++I) {
    dbgs() << " - id = " << I << ", value = ";
    VM.Values[I]->dump();
  }
}

static void debugValue(const ValueMapping &M, unsigned I, StringRef Desc) {
  const Value *V = M.Values[I];
  dbgs() << " - " << Desc << " value = ";
  V->dump();
  for (const Use &U : V->uses()) {
    dbgs() << "   => use: op = " << U.getOperandNo()
           << ", user-id = " << M.IDs.lookup(U.getUser()) << ", user = ";
    U.getUser()->dump();
  }
}

static void debugUserMismatch(const ValueMapping &L, const ValueMapping &R,
                              unsigned I) {
  dbgs() << " - fail: user mismatch: ID = " << I << "\n";
  debugValue(L, I, "LHS");
  debugValue(R, I, "RHS");

  dbgs() << "\nlhs-";
  dumpMapping(L);
  dbgs() << "\nrhs-";
  dumpMapping(R);
}

static void debugSizeMismatch(const ValueMapping &L, const ValueMapping &R) {
  dbgs() << " - fail: map size: " << L.Values.size()
         << " != " << R.Values.size() << "\n";
  dbgs() << "\nlhs-";
  dumpMapping(L);
  dbgs() << "\nrhs-";
  dumpMapping(R);
}
#endif

static bool matches(const ValueMapping &LM, const ValueMapping &RM) {
  LLVM_DEBUG(dbgs() << "compare value maps\n");
  if (LM.Values.size() != RM.Values.size()) {
    LLVM_DEBUG(debugSizeMismatch(LM, RM));
    return false;
  }

  // This mapping doesn't include dangling constant users, since those don't
  // get serialized.  However, checking if users are constant and calling
  // isConstantUsed() on every one is very expensive.  Instead, just check if
  // the user is mapped.
  auto skipUnmappedUsers =
      [&](Value::const_use_iterator &U, Value::const_use_iterator E,
          const ValueMapping &M) {
    while (U != E && !M.lookup(U->getUser()))
      ++U;
  };

  // Iterate through all values, and check that both mappings have the same
  // users.
  for (unsigned I = 0, E = LM.Values.size(); I != E; ++I) {
    const Value *L = LM.Values[I];
    const Value *R = RM.Values[I];
    auto LU = L->use_begin(), LE = L->use_end();
    auto RU = R->use_begin(), RE = R->use_end();
    skipUnmappedUsers(LU, LE, LM);
    skipUnmappedUsers(RU, RE, RM);

    while (LU != LE) {
      if (RU == RE) {
        LLVM_DEBUG(debugUserMismatch(LM, RM, I));
        return false;
      }
      if (LM.lookup(LU->getUser()) != RM.lookup(RU->getUser())) {
        LLVM_DEBUG(debugUserMismatch(LM, RM, I));
        return false;
      }
      if (LU->getOperandNo() != RU->getOperandNo()) {
        LLVM_DEBUG(debugUserMismatch(LM, RM, I));
        return false;
      }
      skipUnmappedUsers(++LU, LE, LM);
      skipUnmappedUsers(++RU, RE, RM);
    }
    if (RU != RE) {
      LLVM_DEBUG(debugUserMismatch(LM, RM, I));
      return false;
    }
  }

  return true;
}

static void verifyAfterRoundTrip(const Module &M,
                                 std::unique_ptr<Module> OtherM) {
  if (!OtherM)
    report_fatal_error("parsing failed");
  if (verifyModule(*OtherM, &errs()))
    report_fatal_error("verification failed");
  if (!matches(ValueMapping(M), ValueMapping(*OtherM)))
    report_fatal_error("use-list order changed");
}

static void verifyBitcodeUseListOrder(const Module &M) {
  TempFile F;
  if (F.init("bc"))
    report_fatal_error("failed to initialize bitcode file");

  if (F.writeBitcode(M))
    report_fatal_error("failed to write bitcode");

  LLVMContext Context;
  verifyAfterRoundTrip(M, F.readBitcode(Context));
}

static void verifyAssemblyUseListOrder(const Module &M) {
  TempFile F;
  if (F.init("ll"))
    report_fatal_error("failed to initialize assembly file");

  if (F.writeAssembly(M))
    report_fatal_error("failed to write assembly");

  LLVMContext Context;
  verifyAfterRoundTrip(M, F.readAssembly(Context));
}

static void verifyUseListOrder(const Module &M) {
  outs() << "verify bitcode\n";
  verifyBitcodeUseListOrder(M);
  outs() << "verify assembly\n";
  verifyAssemblyUseListOrder(M);
}

static void shuffleValueUseLists(Value *V, std::minstd_rand0 &Gen,
                                 DenseSet<Value *> &Seen) {
  if (!Seen.insert(V).second)
    return;

  if (auto *C = dyn_cast<Constant>(V))
    if (!isa<GlobalValue>(C))
      for (Value *Op : C->operands())
        shuffleValueUseLists(Op, Gen, Seen);

  if (V->use_empty() || std::next(V->use_begin()) == V->use_end())
    // Nothing to shuffle for 0 or 1 users.
    return;

  // Generate random numbers between 10 and 99, which will line up nicely in
  // debug output.  We're not worried about collisions here.
  LLVM_DEBUG(dbgs() << "V = "; V->dump());
  std::uniform_int_distribution<short> Dist(10, 99);
  SmallDenseMap<const Use *, short, 16> Order;
  auto compareUses =
      [&Order](const Use &L, const Use &R) { return Order[&L] < Order[&R]; };
  do {
    for (const Use &U : V->uses()) {
      auto I = Dist(Gen);
      Order[&U] = I;
      LLVM_DEBUG(dbgs() << " - order: " << I << ", op = " << U.getOperandNo()
                        << ", U = ";
                 U.getUser()->dump());
    }
  } while (std::is_sorted(V->use_begin(), V->use_end(), compareUses));

  LLVM_DEBUG(dbgs() << " => shuffle\n");
  V->sortUseList(compareUses);

  LLVM_DEBUG({
    for (const Use &U : V->uses()) {
      dbgs() << " - order: " << Order.lookup(&U)
             << ", op = " << U.getOperandNo() << ", U = ";
      U.getUser()->dump();
    }
  });
}

static void reverseValueUseLists(Value *V, DenseSet<Value *> &Seen) {
  if (!Seen.insert(V).second)
    return;

  if (auto *C = dyn_cast<Constant>(V))
    if (!isa<GlobalValue>(C))
      for (Value *Op : C->operands())
        reverseValueUseLists(Op, Seen);

  if (V->use_empty() || std::next(V->use_begin()) == V->use_end())
    // Nothing to shuffle for 0 or 1 users.
    return;

  LLVM_DEBUG({
    dbgs() << "V = ";
    V->dump();
    for (const Use &U : V->uses()) {
      dbgs() << " - order: op = " << U.getOperandNo() << ", U = ";
      U.getUser()->dump();
    }
    dbgs() << " => reverse\n";
  });

  V->reverseUseList();

  LLVM_DEBUG({
    for (const Use &U : V->uses()) {
      dbgs() << " - order: op = " << U.getOperandNo() << ", U = ";
      U.getUser()->dump();
    }
  });
}

template <class Changer>
static void changeUseLists(Module &M, Changer changeValueUseList) {
  // Visit every value that would be serialized to an IR file.
  //
  // Globals.
  for (GlobalVariable &G : M.globals())
    changeValueUseList(&G);
  for (GlobalAlias &A : M.aliases())
    changeValueUseList(&A);
  for (GlobalIFunc &IF : M.ifuncs())
    changeValueUseList(&IF);
  for (Function &F : M)
    changeValueUseList(&F);

  // Constants used by globals.
  for (GlobalVariable &G : M.globals())
    if (G.hasInitializer())
      changeValueUseList(G.getInitializer());
  for (GlobalAlias &A : M.aliases())
    changeValueUseList(A.getAliasee());
  for (GlobalIFunc &IF : M.ifuncs())
    changeValueUseList(IF.getResolver());
  for (Function &F : M)
    for (Value *Op : F.operands())
      changeValueUseList(Op);

  // Function bodies.
  for (Function &F : M) {
    for (Argument &A : F.args())
      changeValueUseList(&A);
    for (BasicBlock &BB : F)
      changeValueUseList(&BB);
    for (BasicBlock &BB : F)
      for (Instruction &I : BB)
        changeValueUseList(&I);

    // Constants used by instructions.
    for (BasicBlock &BB : F)
      for (Instruction &I : BB)
        for (Value *Op : I.operands()) {
          // Look through a metadata wrapper.
          if (auto *MAV = dyn_cast<MetadataAsValue>(Op))
            if (auto *VAM = dyn_cast<ValueAsMetadata>(MAV->getMetadata()))
              Op = VAM->getValue();
          if ((isa<Constant>(Op) && !isa<GlobalValue>(*Op)) ||
              isa<InlineAsm>(Op))
            changeValueUseList(Op);
        }
  }

  if (verifyModule(M, &errs()))
    report_fatal_error("verification failed");
}

static void shuffleUseLists(Module &M, unsigned SeedOffset) {
  std::minstd_rand0 Gen(std::minstd_rand0::default_seed + SeedOffset);
  DenseSet<Value *> Seen;
  changeUseLists(M, [&](Value *V) { shuffleValueUseLists(V, Gen, Seen); });
  LLVM_DEBUG(dbgs() << "\n");
}

static void reverseUseLists(Module &M) {
  DenseSet<Value *> Seen;
  changeUseLists(M, [&](Value *V) { reverseValueUseLists(V, Seen); });
  LLVM_DEBUG(dbgs() << "\n");
}

int main(int argc, char **argv) {
  PreserveInputDbgFormat = cl::boolOrDefault::BOU_TRUE;
  InitLLVM X(argc, argv);

  // Enable debug stream buffering.
  EnableDebugBuffering = true;

  cl::HideUnrelatedOptions(Cat);
  cl::ParseCommandLineOptions(argc, argv,
                              "llvm tool to verify use-list order\n");

  LLVMContext Context;
  SMDiagnostic Err;

  // Load the input module...
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);

  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }
  if (verifyModule(*M, &errs())) {
    errs() << argv[0] << ": " << InputFilename
           << ": error: input module is broken!\n";
    return 1;
  }

  // Verify the use lists now and after reversing them.
  outs() << "*** verify-uselistorder ***\n";
  verifyUseListOrder(*M);
  outs() << "reverse\n";
  reverseUseLists(*M);
  verifyUseListOrder(*M);

  for (unsigned I = 0, E = NumShuffles; I != E; ++I) {
    outs() << "\n";

    // Shuffle with a different (deterministic) seed each time.
    outs() << "shuffle (" << I + 1 << " of " << E << ")\n";
    shuffleUseLists(*M, I);

    // Verify again before and after reversing.
    verifyUseListOrder(*M);
    outs() << "reverse\n";
    reverseUseLists(*M);
    verifyUseListOrder(*M);
  }

  return 0;
}
