//===- AArch64GlobalsTagging.cpp - Global tagging in IR -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <set>

using namespace llvm;

static const Align kTagGranuleSize = Align(16);

static bool shouldTagGlobal(GlobalVariable &G) {
  if (!G.isTagged())
    return false;

  assert(G.hasSanitizerMetadata() &&
         "Missing sanitizer metadata, but symbol is apparently tagged.");
  GlobalValue::SanitizerMetadata Meta = G.getSanitizerMetadata();

  // For now, don't instrument constant data, as it'll be in .rodata anyway. It
  // may be worth instrumenting these in future to stop them from being used as
  // gadgets.
  if (G.getName().starts_with("llvm.") || G.isThreadLocal() || G.isConstant()) {
    Meta.Memtag = false;
    G.setSanitizerMetadata(Meta);
    return false;
  }

  // Globals can be placed implicitly or explicitly in sections. There's two
  // different types of globals that meet this criteria that cause problems:
  //  1. Function pointers that are going into various init arrays (either
  //     explicitly through `__attribute__((section(<foo>)))` or implicitly
  //     through `__attribute__((constructor)))`, such as ".(pre)init(_array)",
  //     ".fini(_array)", ".ctors", and ".dtors". These function pointers end up
  //     overaligned and overpadded, making iterating over them problematic, and
  //     each function pointer is individually tagged (so the iteration over
  //     them causes SIGSEGV/MTE[AS]ERR).
  //  2. Global variables put into an explicit section, where the section's name
  //     is a valid C-style identifier. The linker emits a `__start_<name>` and
  //     `__stop_<na,e>` symbol for the section, so that you can iterate over
  //     globals within this section. Unfortunately, again, these globals would
  //     be tagged and so iteration causes SIGSEGV/MTE[AS]ERR.
  //
  // To mitigate both these cases, and because specifying a section is rare
  // outside of these two cases, disable MTE protection for globals in any
  // section.
  if (G.hasSection()) {
    Meta.Memtag = false;
    G.setSanitizerMetadata(Meta);
    return false;
  }

  return true;
}

// Technically, due to ELF symbol interposition semantics, we can't change the
// alignment or size of symbols. If we increase the alignment or size of a
// symbol, the compiler may make optimisations based on this new alignment or
// size. If the symbol is interposed, this optimisation could lead to
// alignment-related or OOB read/write crashes.
//
// This is handled in the linker. When the linker sees multiple declarations of
// a global variable, and some are tagged, and some are untagged, it resolves it
// to be an untagged definition - but preserves the tag-granule-rounded size and
// tag-granule-alignment. This should prevent these kind of crashes intra-DSO.
// For cross-DSO, it's been a reasonable contract that if you're interposing a
// sanitizer-instrumented global, then the interposer also needs to be
// sanitizer-instrumented.
//
// FIXME: In theory, this can be fixed by splitting the size/alignment of
// globals into two uses: an "output alignment" that's emitted to the ELF file,
// and an "optimisation alignment" that's used for optimisation. Thus, we could
// adjust the output alignment only, and still optimise based on the pessimistic
// pre-tagging size/alignment.
static void tagGlobalDefinition(Module &M, GlobalVariable *G) {
  Constant *Initializer = G->getInitializer();
  uint64_t SizeInBytes =
      M.getDataLayout().getTypeAllocSize(Initializer->getType());

  uint64_t NewSize = alignTo(SizeInBytes, kTagGranuleSize);
  if (SizeInBytes != NewSize) {
    // Pad the initializer out to the next multiple of 16 bytes.
    llvm::SmallVector<uint8_t> Init(NewSize - SizeInBytes, 0);
    Constant *Padding = ConstantDataArray::get(M.getContext(), Init);
    Initializer = ConstantStruct::getAnon({Initializer, Padding});
    auto *NewGV = new GlobalVariable(
        M, Initializer->getType(), G->isConstant(), G->getLinkage(),
        Initializer, "", G, G->getThreadLocalMode(), G->getAddressSpace());
    NewGV->copyAttributesFrom(G);
    NewGV->setComdat(G->getComdat());
    NewGV->copyMetadata(G, 0);

    NewGV->takeName(G);
    G->replaceAllUsesWith(NewGV);
    G->eraseFromParent();
    G = NewGV;
  }

  G->setAlignment(std::max(G->getAlign().valueOrOne(), kTagGranuleSize));

  // Ensure that tagged globals don't get merged by ICF - as they should have
  // different tags at runtime.
  G->setUnnamedAddr(GlobalValue::UnnamedAddr::None);
}

namespace {
class AArch64GlobalsTagging : public ModulePass {
public:
  static char ID;

  explicit AArch64GlobalsTagging() : ModulePass(ID) {
    initializeAArch64GlobalsTaggingPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override;

  StringRef getPassName() const override { return "AArch64 Globals Tagging"; }

private:
  std::set<GlobalVariable *> GlobalsToTag;
};
} // anonymous namespace

char AArch64GlobalsTagging::ID = 0;

bool AArch64GlobalsTagging::runOnModule(Module &M) {
  // No mutating the globals in-place, or iterator invalidation occurs.
  std::vector<GlobalVariable *> GlobalsToTag;
  for (GlobalVariable &G : M.globals()) {
    if (G.isDeclaration() || !shouldTagGlobal(G))
      continue;
    GlobalsToTag.push_back(&G);
  }

  for (GlobalVariable *G : GlobalsToTag) {
    tagGlobalDefinition(M, G);
  }

  return true;
}

INITIALIZE_PASS_BEGIN(AArch64GlobalsTagging, "aarch64-globals-tagging",
                      "AArch64 Globals Tagging Pass", false, false)
INITIALIZE_PASS_END(AArch64GlobalsTagging, "aarch64-globals-tagging",
                    "AArch64 Globals Tagging Pass", false, false)

ModulePass *llvm::createAArch64GlobalsTaggingPass() {
  return new AArch64GlobalsTagging();
}
