//===-- LLJITWithOptimizingIRTransform.cpp -- LLJIT with IR optimization --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// In this example we will use an IR transform to optimize a module as it
// passes through LLJIT's IRTransformLayer.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"

#include "../ExampleModules.h"

using namespace llvm;
using namespace llvm::orc;

ExitOnError ExitOnErr;

// Example IR module.
//
// This IR contains a recursive definition of the factorial function:
//
// fac(n) | n == 0    = 1
//        | otherwise = n * fac(n - 1)
//
// It also contains an entry function which calls the factorial function with
// an input value of 5.
//
// We expect the IR optimization transform that we build below to transform
// this into a non-recursive factorial function and an entry function that
// returns a constant value of 5!, or 120.

const llvm::StringRef MainMod =
    R"(

  define i32 @fac(i32 %n) {
  entry:
    %tobool = icmp eq i32 %n, 0
    br i1 %tobool, label %return, label %if.then

  if.then:                                          ; preds = %entry
    %arg = add nsw i32 %n, -1
    %call_result = call i32 @fac(i32 %arg)
    %result = mul nsw i32 %n, %call_result
    br label %return

  return:                                           ; preds = %entry, %if.then
    %final_result = phi i32 [ %result, %if.then ], [ 1, %entry ]
    ret i32 %final_result
  }

  define i32 @entry() {
  entry:
    %result = call i32 @fac(i32 5)
    ret i32 %result
  }

)";

// A function object that creates a simple pass pipeline to apply to each
// module as it passes through the IRTransformLayer.
class MyOptimizationTransform {
public:
  MyOptimizationTransform() : PM(std::make_unique<legacy::PassManager>()) {
    PM->add(createTailCallEliminationPass());
    PM->add(createCFGSimplificationPass());
  }

  Expected<ThreadSafeModule> operator()(ThreadSafeModule TSM,
                                        MaterializationResponsibility &R) {
    TSM.withModuleDo([this](Module &M) {
      dbgs() << "--- BEFORE OPTIMIZATION ---\n" << M << "\n";
      PM->run(M);
      dbgs() << "--- AFTER OPTIMIZATION ---\n" << M << "\n";
    });
    return std::move(TSM);
  }

private:
  std::unique_ptr<legacy::PassManager> PM;
};

int main(int argc, char *argv[]) {
  // Initialize LLVM.
  InitLLVM X(argc, argv);

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  // (1) Create LLJIT instance.
  auto J = ExitOnErr(LLJITBuilder().create());

  // (2) Install transform to optimize modules when they're materialized.
  J->getIRTransformLayer().setTransform(MyOptimizationTransform());

  // (3) Add modules.
  ExitOnErr(J->addIRModule(ExitOnErr(parseExampleModule(MainMod, "MainMod"))));

  // (4) Look up the JIT'd function and call it.
  auto EntryAddr = ExitOnErr(J->lookup("entry"));
  auto *Entry = EntryAddr.toPtr<int()>();

  int Result = Entry();
  outs() << "--- Result ---\n"
         << "entry() = " << Result << "\n";

  return 0;
}
