//===--------- LLJITRemovableCode.cpp -- LLJIT with Code Removal ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// In this example we will use an the resource management APIs to transfer
// ownership of modules, remove modules from a JITDylib, and then a whole
// JITDylib from the ExecutionSession.
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

// Example IR modules.
//
// We will use a few modules containing no-op functions to demonstrate the code
// removal APIs.

const llvm::StringRef FooMod =
    R"(
  define void @foo() {
  entry:
    ret void
  }
)";

const llvm::StringRef BarMod =
    R"(
  define void @bar() {
  entry:
    ret void
  }
)";

const llvm::StringRef BazMod =
    R"(
  define void @baz() {
  entry:
    ret void
  }
)";

int main(int argc, char *argv[]) {
  // Initialize LLVM.
  InitLLVM X(argc, argv);

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  // (1) Create LLJIT instance.
  auto J = ExitOnErr(LLJITBuilder().create());

  // (2) Create a new JITDylib to use for this example.
  auto &JD = ExitOnErr(J->createJITDylib("JD"));

  // (3) Add the 'foo' module with no explicit resource tracker. The resources
  // for 'foo' will be tracked by the default tracker for JD. We will not be
  // able to free it separately, but its resources will still be freed when we
  // clear or remove JD.
  ExitOnErr(J->addIRModule(JD, ExitOnErr(parseExampleModule(FooMod, "foo"))));

  // (4) Create a tracker for the module 'bar' and use it to add that module.
  auto BarRT = JD.createResourceTracker();
  ExitOnErr(
      J->addIRModule(BarRT, ExitOnErr(parseExampleModule(BarMod, "bar"))));

  // (5) Create a tracker for the module 'baz' and use it to add that module.
  auto BazRT = JD.createResourceTracker();
  ExitOnErr(
      J->addIRModule(BazRT, ExitOnErr(parseExampleModule(BazMod, "baz"))));

  // (6) Print out the symbols in their initial state:
  auto PrintSymbol = [&](StringRef Name) {
    dbgs() << Name << " = ";
    if (auto Sym = J->lookup(JD, Name))
      dbgs() << *Sym << "\n";
    else
      dbgs() << "error: " << toString(Sym.takeError()) << "\n";
  };

  dbgs() << "Initially:\n";
  PrintSymbol("foo");
  PrintSymbol("bar");
  PrintSymbol("baz");

  // (7) Reset BazRT. This will implicitly transfer tracking of module baz to
  // JD's default resource tracker.
  dbgs() << "After implicitly transferring ownership of baz to JD's default "
            "tracker:\n";
  BazRT = nullptr;
  PrintSymbol("foo");
  PrintSymbol("bar");
  PrintSymbol("baz");

  // (8) Remove BarRT. This should remove the bar symbol.
  dbgs() << "After removing bar (lookup for bar should yield a missing symbol "
            "error):\n";
  ExitOnErr(BarRT->remove());
  PrintSymbol("foo");
  PrintSymbol("bar");
  PrintSymbol("baz");

  // (9) Clear JD. This should remove all symbols currently in the JITDylib.
  dbgs() << "After clearing JD (lookup should yield missing symbol errors for "
            "all symbols):\n";
  ExitOnErr(JD.clear());
  PrintSymbol("foo");
  PrintSymbol("bar");
  PrintSymbol("baz");

  // (10) Remove JD from the ExecutionSession. JD can no longer be used.
  dbgs() << "Removing JD.\n";
  ExitOnErr(J->getExecutionSession().removeJITDylib(JD));

  dbgs() << "done.\n";

  return 0;
}
