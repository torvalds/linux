//===--------------- LLJITWithCustomObjectLinkingLayer.cpp ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file shows how to switch LLJIT to use a custom object linking layer (we
// use ObjectLinkingLayer, which is backed by JITLink, as an example).
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringMap.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "../ExampleModules.h"

using namespace llvm;
using namespace llvm::orc;

ExitOnError ExitOnErr;

const llvm::StringRef TestMod =
    R"(
  define i32 @callee() {
  entry:
    ret i32 7
  }

  define i32 @entry() {
  entry:
    %0 = call i32 @callee()
    ret i32 %0
  }
)";

class MyPlugin : public ObjectLinkingLayer::Plugin {
public:
  // The modifyPassConfig callback gives us a chance to inspect the
  // MaterializationResponsibility and target triple for the object being
  // linked, then add any JITLink passes that we would like to run on the
  // link graph. A pass is just a function object that is callable as
  // Error(jitlink::LinkGraph&). In this case we will add two passes
  // defined as lambdas that call the printLinkerGraph method on our
  // plugin: One to run before the linker applies fixups and another to
  // run afterwards.
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &LG,
                        jitlink::PassConfiguration &Config) override {

    outs() << "MyPlugin -- Modifying pass config for " << LG.getName() << " ("
           << LG.getTargetTriple().str() << "):\n";

    // Print sections, symbol names and addresses, and any edges for the
    // associated blocks at the 'PostPrune' phase of JITLink (after
    // dead-stripping, but before addresses are allocated in the target
    // address space. See llvm/docs/JITLink.rst).
    //
    // Experiment with adding the 'printGraph' pass at other points in the
    // pipeline. E.g. PrePrunePasses, PostAllocationPasses, and
    // PostFixupPasses.
    Config.PostPrunePasses.push_back(printGraph);
  }

  void notifyLoaded(MaterializationResponsibility &MR) override {
    outs() << "Loading object defining " << MR.getSymbols() << "\n";
  }

  Error notifyEmitted(MaterializationResponsibility &MR) override {
    outs() << "Emitted object defining " << MR.getSymbols() << "\n";
    return Error::success();
  }

  Error notifyFailed(MaterializationResponsibility &MR) override {
    return Error::success();
  }

  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
    return Error::success();
  }

  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override {}

private:
  static void printBlockContent(jitlink::Block &B) {
    constexpr JITTargetAddress LineWidth = 16;

    if (B.isZeroFill()) {
      outs() << "    " << formatv("{0:x16}", B.getAddress()) << ": "
             << B.getSize() << " bytes of zero-fill.\n";
      return;
    }

    ExecutorAddr InitAddr(B.getAddress().getValue() & ~(LineWidth - 1));
    ExecutorAddr StartAddr = B.getAddress();
    ExecutorAddr EndAddr = B.getAddress() + B.getSize();
    auto *Data = reinterpret_cast<const uint8_t *>(B.getContent().data());

    for (ExecutorAddr CurAddr = InitAddr; CurAddr != EndAddr; ++CurAddr) {
      if (CurAddr % LineWidth == 0)
        outs() << "          " << formatv("{0:x16}", CurAddr.getValue())
               << ": ";
      if (CurAddr < StartAddr)
        outs() << "   ";
      else
        outs() << formatv("{0:x-2}", Data[CurAddr - StartAddr]) << " ";
      if (CurAddr % LineWidth == LineWidth - 1)
        outs() << "\n";
    }
    if (EndAddr % LineWidth != 0)
      outs() << "\n";
  }

  static Error printGraph(jitlink::LinkGraph &G) {

    DenseSet<jitlink::Block *> BlocksAlreadyVisited;

    outs() << "Graph \"" << G.getName() << "\"\n";
    // Loop over all sections...
    for (auto &S : G.sections()) {
      outs() << "  Section " << S.getName() << ":\n";

      // Loop over all symbols in the current section...
      for (auto *Sym : S.symbols()) {

        // Print the symbol's address.
        outs() << "    " << formatv("{0:x16}", Sym->getAddress()) << ": ";

        // Print the symbol's name, or "<anonymous symbol>" if it doesn't have
        // one.
        if (Sym->hasName())
          outs() << Sym->getName() << "\n";
        else
          outs() << "<anonymous symbol>\n";

        // Get the content block for this symbol.
        auto &B = Sym->getBlock();

        if (BlocksAlreadyVisited.count(&B)) {
          outs() << "      Block " << formatv("{0:x16}", B.getAddress())
                 << " already printed.\n";
          continue;
        } else
          outs() << "      Block " << formatv("{0:x16}", B.getAddress())
                 << ":\n";

        outs() << "        Content:\n";
        printBlockContent(B);
        BlocksAlreadyVisited.insert(&B);

        if (!B.edges().empty()) {
          outs() << "        Edges:\n";
          for (auto &E : B.edges()) {
            outs() << "          "
                   << formatv("{0:x16}", B.getAddress() + E.getOffset())
                   << ": kind = " << formatv("{0:d}", E.getKind())
                   << ", addend = " << formatv("{0:x}", E.getAddend())
                   << ", target = ";
            jitlink::Symbol &TargetSym = E.getTarget();
            if (TargetSym.hasName())
              outs() << TargetSym.getName() << "\n";
            else
              outs() << "<anonymous target>\n";
          }
        }
        outs() << "\n";
      }
    }
    return Error::success();
  }
};

static cl::opt<std::string>
    EntryPointName("entry", cl::desc("Symbol to call as main entry point"),
                   cl::init("entry"));

static cl::list<std::string> InputObjects(cl::Positional,
                                          cl::desc("input objects"));

int main(int argc, char *argv[]) {
  // Initialize LLVM.
  InitLLVM X(argc, argv);

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  cl::ParseCommandLineOptions(argc, argv, "LLJITWithObjectLinkingLayerPlugin");
  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  // Detect the host and set code model to small.
  auto JTMB = ExitOnErr(JITTargetMachineBuilder::detectHost());
  JTMB.setCodeModel(CodeModel::Small);

  // Create an LLJIT instance with an ObjectLinkingLayer as the base layer.
  // We attach our plugin in to the newly created ObjectLinkingLayer before
  // returning it.
  auto J = ExitOnErr(
      LLJITBuilder()
          .setJITTargetMachineBuilder(std::move(JTMB))
          .setObjectLinkingLayerCreator(
              [&](ExecutionSession &ES, const Triple &TT) {
                // Create ObjectLinkingLayer.
                auto ObjLinkingLayer = std::make_unique<ObjectLinkingLayer>(
                    ES, ExitOnErr(jitlink::InProcessMemoryManager::Create()));
                // Add an instance of our plugin.
                ObjLinkingLayer->addPlugin(std::make_unique<MyPlugin>());
                return ObjLinkingLayer;
              })
          .create());

  if (!InputObjects.empty()) {
    // Load the input objects.
    for (auto InputObject : InputObjects) {
      auto ObjBuffer =
          ExitOnErr(errorOrToExpected(MemoryBuffer::getFile(InputObject)));
      ExitOnErr(J->addObjectFile(std::move(ObjBuffer)));
    }
  } else {
    auto M = ExitOnErr(parseExampleModule(TestMod, "test-module"));
    M.withModuleDo([](Module &MP) {
      outs() << "No input objects specified. Using demo module:\n"
             << MP << "\n";
    });
    ExitOnErr(J->addIRModule(std::move(M)));
  }

  // Look up the JIT'd function, cast it to a function pointer, then call it.
  auto EntryAddr = ExitOnErr(J->lookup(EntryPointName));
  auto *Entry = EntryAddr.toPtr<int()>();

  int Result = Entry();
  outs() << "---Result---\n"
         << EntryPointName << "() = " << Result << "\n";

  return 0;
}
