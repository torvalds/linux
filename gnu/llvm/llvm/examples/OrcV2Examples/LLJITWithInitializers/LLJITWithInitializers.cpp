//===----- LLJITDumpObjects.cpp - How to dump JIT'd objects with LLJIT ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// This example demonstrates how to use LLJIT to:
///   - Add absolute symbol definitions.
///   - Run static constructors for a JITDylib.
///   - Run static destructors for a JITDylib.
///
/// This example does not call any functions (e.g. main or equivalent)  between
/// running the static constructors and running the static destructors.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringMap.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "../ExampleModules.h"

using namespace llvm;
using namespace llvm::orc;

ExitOnError ExitOnErr;

const llvm::StringRef ModuleWithInitializer =
    R"(

  @InitializersRunFlag = external global i32
  @DeinitializersRunFlag = external global i32

  declare i32 @__cxa_atexit(void (i8*)*, i8*, i8*)
  @__dso_handle = external hidden global i8

  @llvm.global_ctors =
    appending global [1 x { i32, void ()*, i8* }]
      [{ i32, void ()*, i8* } { i32 65535, void ()* @init_func, i8* null }]

  define internal void @init_func() {
  entry:
    store i32 1, i32* @InitializersRunFlag
    %0 = call i32 @__cxa_atexit(void (i8*)* @deinit_func, i8* null,
                                i8* @__dso_handle)
    ret void
  }

  define internal void @deinit_func(i8* %0) {
    store i32 1, i32* @DeinitializersRunFlag
    ret void
  }

)";

int main(int argc, char *argv[]) {
  // Initialize LLVM.
  InitLLVM X(argc, argv);

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  cl::ParseCommandLineOptions(argc, argv, "LLJITWithInitializers");
  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  auto J = ExitOnErr(LLJITBuilder().create());
  auto M = ExitOnErr(parseExampleModule(ModuleWithInitializer, "M"));

  // Load the module.
  ExitOnErr(J->addIRModule(std::move(M)));

  int32_t InitializersRunFlag = 0;
  int32_t DeinitializersRunFlag = 0;

  ExitOnErr(J->getMainJITDylib().define(
      absoluteSymbols({{J->mangleAndIntern("InitializersRunFlag"),
                        {ExecutorAddr::fromPtr(&InitializersRunFlag),
                         JITSymbolFlags::Exported}},
                       {J->mangleAndIntern("DeinitializersRunFlag"),
                        {ExecutorAddr::fromPtr(&DeinitializersRunFlag),
                         JITSymbolFlags::Exported}}})));

  // Run static initializers.
  ExitOnErr(J->initialize(J->getMainJITDylib()));

  // Run deinitializers.
  ExitOnErr(J->deinitialize(J->getMainJITDylib()));

  outs() << "InitializerRanFlag = " << InitializersRunFlag << "\n"
         << "DeinitializersRunFlag = " << DeinitializersRunFlag << "\n";

  return 0;
}
