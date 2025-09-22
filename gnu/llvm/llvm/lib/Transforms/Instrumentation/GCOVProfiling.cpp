//===- GCOVProfiling.cpp - Insert edge counters for gcov profiling --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements GCOV-style profiling. When this pass is run it emits
// "gcno" files next to the existing source, and instruments the code that runs
// to records the edges between blocks that run and emit a complementary "gcda"
// file on exit.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Instrumentation/CFGMST.h"
#include "llvm/Transforms/Instrumentation/GCOVProfiler.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>

using namespace llvm;
namespace endian = llvm::support::endian;

#define DEBUG_TYPE "insert-gcov-profiling"

enum : uint32_t {
  GCOV_ARC_ON_TREE = 1 << 0,

  GCOV_TAG_FUNCTION = 0x01000000,
  GCOV_TAG_BLOCKS = 0x01410000,
  GCOV_TAG_ARCS = 0x01430000,
  GCOV_TAG_LINES = 0x01450000,
};

static cl::opt<std::string> DefaultGCOVVersion("default-gcov-version",
                                               cl::init("408*"), cl::Hidden,
                                               cl::ValueRequired);

static cl::opt<bool> AtomicCounter("gcov-atomic-counter", cl::Hidden,
                                   cl::desc("Make counter updates atomic"));

// Returns the number of words which will be used to represent this string.
static unsigned wordsOfString(StringRef s) {
  // Length + NUL-terminated string + 0~3 padding NULs.
  return (s.size() / 4) + 2;
}

GCOVOptions GCOVOptions::getDefault() {
  GCOVOptions Options;
  Options.EmitNotes = true;
  Options.EmitData = true;
  Options.NoRedZone = false;
  Options.Atomic = AtomicCounter;

  if (DefaultGCOVVersion.size() != 4) {
    llvm::report_fatal_error(Twine("Invalid -default-gcov-version: ") +
                             DefaultGCOVVersion, /*GenCrashDiag=*/false);
  }
  memcpy(Options.Version, DefaultGCOVVersion.c_str(), 4);
  return Options;
}

namespace {
class GCOVFunction;

class GCOVProfiler {
public:
  GCOVProfiler() : GCOVProfiler(GCOVOptions::getDefault()) {}
  GCOVProfiler(const GCOVOptions &Opts) : Options(Opts) {}
  bool
  runOnModule(Module &M, function_ref<BlockFrequencyInfo *(Function &F)> GetBFI,
              function_ref<BranchProbabilityInfo *(Function &F)> GetBPI,
              std::function<const TargetLibraryInfo &(Function &F)> GetTLI);

  void write(uint32_t i) {
    char Bytes[4];
    endian::write32(Bytes, i, Endian);
    os->write(Bytes, 4);
  }
  void writeString(StringRef s) {
    write(wordsOfString(s) - 1);
    os->write(s.data(), s.size());
    os->write_zeros(4 - s.size() % 4);
  }
  void writeBytes(const char *Bytes, int Size) { os->write(Bytes, Size); }

private:
  // Create the .gcno files for the Module based on DebugInfo.
  bool
  emitProfileNotes(NamedMDNode *CUNode, bool HasExecOrFork,
                   function_ref<BlockFrequencyInfo *(Function &F)> GetBFI,
                   function_ref<BranchProbabilityInfo *(Function &F)> GetBPI,
                   function_ref<const TargetLibraryInfo &(Function &F)> GetTLI);

  Function *createInternalFunction(FunctionType *FTy, StringRef Name,
                                   StringRef MangledType = "");
  void emitGlobalConstructor(
      SmallVectorImpl<std::pair<GlobalVariable *, MDNode *>> &CountersBySP);

  bool isFunctionInstrumented(const Function &F);
  std::vector<Regex> createRegexesFromString(StringRef RegexesStr);
  static bool doesFilenameMatchARegex(StringRef Filename,
                                      std::vector<Regex> &Regexes);

  // Get pointers to the functions in the runtime library.
  FunctionCallee getStartFileFunc(const TargetLibraryInfo *TLI);
  FunctionCallee getEmitFunctionFunc(const TargetLibraryInfo *TLI);
  FunctionCallee getEmitArcsFunc(const TargetLibraryInfo *TLI);
  FunctionCallee getSummaryInfoFunc();
  FunctionCallee getEndFileFunc();

  // Add the function to write out all our counters to the global destructor
  // list.
  Function *
  insertCounterWriteout(ArrayRef<std::pair<GlobalVariable *, MDNode *>>);
  Function *insertReset(ArrayRef<std::pair<GlobalVariable *, MDNode *>>);

  bool AddFlushBeforeForkAndExec();

  enum class GCovFileType { GCNO, GCDA };
  std::string mangleName(const DICompileUnit *CU, GCovFileType FileType);

  GCOVOptions Options;
  llvm::endianness Endian;
  raw_ostream *os;

  // Checksum, produced by hash of EdgeDestinations
  SmallVector<uint32_t, 4> FileChecksums;

  Module *M = nullptr;
  std::function<const TargetLibraryInfo &(Function &F)> GetTLI;
  LLVMContext *Ctx = nullptr;
  SmallVector<std::unique_ptr<GCOVFunction>, 16> Funcs;
  std::vector<Regex> FilterRe;
  std::vector<Regex> ExcludeRe;
  DenseSet<const BasicBlock *> ExecBlocks;
  StringMap<bool> InstrumentedFiles;
};

struct BBInfo {
  BBInfo *Group;
  uint32_t Index;
  uint32_t Rank = 0;

  BBInfo(unsigned Index) : Group(this), Index(Index) {}
  std::string infoString() const {
    return (Twine("Index=") + Twine(Index)).str();
  }
};

struct Edge {
  // This class implements the CFG edges. Note the CFG can be a multi-graph.
  // So there might be multiple edges with same SrcBB and DestBB.
  const BasicBlock *SrcBB;
  const BasicBlock *DestBB;
  uint64_t Weight;
  BasicBlock *Place = nullptr;
  uint32_t SrcNumber, DstNumber;
  bool InMST = false;
  bool Removed = false;
  bool IsCritical = false;

  Edge(const BasicBlock *Src, const BasicBlock *Dest, uint64_t W = 1)
      : SrcBB(Src), DestBB(Dest), Weight(W) {}

  // Return the information string of an edge.
  std::string infoString() const {
    return (Twine(Removed ? "-" : " ") + (InMST ? " " : "*") +
            (IsCritical ? "c" : " ") + "  W=" + Twine(Weight))
        .str();
  }
};
}

static StringRef getFunctionName(const DISubprogram *SP) {
  if (!SP->getLinkageName().empty())
    return SP->getLinkageName();
  return SP->getName();
}

/// Extract a filename for a DISubprogram.
///
/// Prefer relative paths in the coverage notes. Clang also may split
/// up absolute paths into a directory and filename component. When
/// the relative path doesn't exist, reconstruct the absolute path.
static SmallString<128> getFilename(const DISubprogram *SP) {
  SmallString<128> Path;
  StringRef RelPath = SP->getFilename();
  if (sys::fs::exists(RelPath))
    Path = RelPath;
  else
    sys::path::append(Path, SP->getDirectory(), SP->getFilename());
  return Path;
}

namespace {
  class GCOVRecord {
  protected:
    GCOVProfiler *P;

    GCOVRecord(GCOVProfiler *P) : P(P) {}

    void write(uint32_t i) { P->write(i); }
    void writeString(StringRef s) { P->writeString(s); }
    void writeBytes(const char *Bytes, int Size) { P->writeBytes(Bytes, Size); }
  };

  class GCOVFunction;
  class GCOVBlock;

  // Constructed only by requesting it from a GCOVBlock, this object stores a
  // list of line numbers and a single filename, representing lines that belong
  // to the block.
  class GCOVLines : public GCOVRecord {
   public:
    void addLine(uint32_t Line) {
      assert(Line != 0 && "Line zero is not a valid real line number.");
      Lines.push_back(Line);
    }

    uint32_t length() const {
      return 1 + wordsOfString(Filename) + Lines.size();
    }

    void writeOut() {
      write(0);
      writeString(Filename);
      for (uint32_t L : Lines)
        write(L);
    }

    GCOVLines(GCOVProfiler *P, StringRef F)
        : GCOVRecord(P), Filename(std::string(F)) {}

  private:
    std::string Filename;
    SmallVector<uint32_t, 32> Lines;
  };


  // Represent a basic block in GCOV. Each block has a unique number in the
  // function, number of lines belonging to each block, and a set of edges to
  // other blocks.
  class GCOVBlock : public GCOVRecord {
   public:
    GCOVLines &getFile(StringRef Filename) {
      return LinesByFile.try_emplace(Filename, P, Filename).first->second;
    }

    void addEdge(GCOVBlock &Successor, uint32_t Flags) {
      OutEdges.emplace_back(&Successor, Flags);
    }

    void writeOut() {
      uint32_t Len = 3;
      SmallVector<StringMapEntry<GCOVLines> *, 32> SortedLinesByFile;
      for (auto &I : LinesByFile) {
        Len += I.second.length();
        SortedLinesByFile.push_back(&I);
      }

      write(GCOV_TAG_LINES);
      write(Len);
      write(Number);

      llvm::sort(SortedLinesByFile, [](StringMapEntry<GCOVLines> *LHS,
                                       StringMapEntry<GCOVLines> *RHS) {
        return LHS->getKey() < RHS->getKey();
      });
      for (auto &I : SortedLinesByFile)
        I->getValue().writeOut();
      write(0);
      write(0);
    }

    GCOVBlock(const GCOVBlock &RHS) : GCOVRecord(RHS), Number(RHS.Number) {
      // Only allow copy before edges and lines have been added. After that,
      // there are inter-block pointers (eg: edges) that won't take kindly to
      // blocks being copied or moved around.
      assert(LinesByFile.empty());
      assert(OutEdges.empty());
    }

    uint32_t Number;
    SmallVector<std::pair<GCOVBlock *, uint32_t>, 4> OutEdges;

  private:
    friend class GCOVFunction;

    GCOVBlock(GCOVProfiler *P, uint32_t Number)
        : GCOVRecord(P), Number(Number) {}

    StringMap<GCOVLines> LinesByFile;
  };

  // A function has a unique identifier, a checksum (we leave as zero) and a
  // set of blocks and a map of edges between blocks. This is the only GCOV
  // object users can construct, the blocks and lines will be rooted here.
  class GCOVFunction : public GCOVRecord {
  public:
    GCOVFunction(GCOVProfiler *P, Function *F, const DISubprogram *SP,
                 unsigned EndLine, uint32_t Ident, int Version)
        : GCOVRecord(P), SP(SP), EndLine(EndLine), Ident(Ident),
          Version(Version), EntryBlock(P, 0), ReturnBlock(P, 1) {
      LLVM_DEBUG(dbgs() << "Function: " << getFunctionName(SP) << "\n");
      bool ExitBlockBeforeBody = Version >= 48;
      uint32_t i = ExitBlockBeforeBody ? 2 : 1;
      for (BasicBlock &BB : *F)
        Blocks.insert(std::make_pair(&BB, GCOVBlock(P, i++)));
      if (!ExitBlockBeforeBody)
        ReturnBlock.Number = i;

      std::string FunctionNameAndLine;
      raw_string_ostream FNLOS(FunctionNameAndLine);
      FNLOS << getFunctionName(SP) << SP->getLine();
      FNLOS.flush();
      FuncChecksum = hash_value(FunctionNameAndLine);
    }

    GCOVBlock &getBlock(const BasicBlock *BB) {
      return Blocks.find(const_cast<BasicBlock *>(BB))->second;
    }

    GCOVBlock &getEntryBlock() { return EntryBlock; }
    GCOVBlock &getReturnBlock() {
      return ReturnBlock;
    }

    uint32_t getFuncChecksum() const {
      return FuncChecksum;
    }

    void writeOut(uint32_t CfgChecksum) {
      write(GCOV_TAG_FUNCTION);
      SmallString<128> Filename = getFilename(SP);
      uint32_t BlockLen =
          2 + (Version >= 47) + wordsOfString(getFunctionName(SP));
      if (Version < 80)
        BlockLen += wordsOfString(Filename) + 1;
      else
        BlockLen += 1 + wordsOfString(Filename) + 3 + (Version >= 90);

      write(BlockLen);
      write(Ident);
      write(FuncChecksum);
      if (Version >= 47)
        write(CfgChecksum);
      writeString(getFunctionName(SP));
      if (Version < 80) {
        writeString(Filename);
        write(SP->getLine());
      } else {
        write(SP->isArtificial()); // artificial
        writeString(Filename);
        write(SP->getLine()); // start_line
        write(0);             // start_column
        // EndLine is the last line with !dbg. It is not the } line as in GCC,
        // but good enough.
        write(EndLine);
        if (Version >= 90)
          write(0); // end_column
      }

      // Emit count of blocks.
      write(GCOV_TAG_BLOCKS);
      if (Version < 80) {
        write(Blocks.size() + 2);
        for (int i = Blocks.size() + 2; i; --i)
          write(0);
      } else {
        write(1);
        write(Blocks.size() + 2);
      }
      LLVM_DEBUG(dbgs() << (Blocks.size() + 1) << " blocks\n");

      // Emit edges between blocks.
      const uint32_t Outgoing = EntryBlock.OutEdges.size();
      if (Outgoing) {
        write(GCOV_TAG_ARCS);
        write(Outgoing * 2 + 1);
        write(EntryBlock.Number);
        for (const auto &E : EntryBlock.OutEdges) {
          write(E.first->Number);
          write(E.second);
        }
      }
      for (auto &It : Blocks) {
        const GCOVBlock &Block = It.second;
        if (Block.OutEdges.empty()) continue;

        write(GCOV_TAG_ARCS);
        write(Block.OutEdges.size() * 2 + 1);
        write(Block.Number);
        for (const auto &E : Block.OutEdges) {
          write(E.first->Number);
          write(E.second);
        }
      }

      // Emit lines for each block.
      for (auto &It : Blocks)
        It.second.writeOut();
    }

  public:
    const DISubprogram *SP;
    unsigned EndLine;
    uint32_t Ident;
    uint32_t FuncChecksum;
    int Version;
    MapVector<BasicBlock *, GCOVBlock> Blocks;
    GCOVBlock EntryBlock;
    GCOVBlock ReturnBlock;
  };
}

// RegexesStr is a string containing differents regex separated by a semi-colon.
// For example "foo\..*$;bar\..*$".
std::vector<Regex> GCOVProfiler::createRegexesFromString(StringRef RegexesStr) {
  std::vector<Regex> Regexes;
  while (!RegexesStr.empty()) {
    std::pair<StringRef, StringRef> HeadTail = RegexesStr.split(';');
    if (!HeadTail.first.empty()) {
      Regex Re(HeadTail.first);
      std::string Err;
      if (!Re.isValid(Err)) {
        Ctx->emitError(Twine("Regex ") + HeadTail.first +
                       " is not valid: " + Err);
      }
      Regexes.emplace_back(std::move(Re));
    }
    RegexesStr = HeadTail.second;
  }
  return Regexes;
}

bool GCOVProfiler::doesFilenameMatchARegex(StringRef Filename,
                                           std::vector<Regex> &Regexes) {
  for (Regex &Re : Regexes)
    if (Re.match(Filename))
      return true;
  return false;
}

bool GCOVProfiler::isFunctionInstrumented(const Function &F) {
  if (FilterRe.empty() && ExcludeRe.empty()) {
    return true;
  }
  SmallString<128> Filename = getFilename(F.getSubprogram());
  auto It = InstrumentedFiles.find(Filename);
  if (It != InstrumentedFiles.end()) {
    return It->second;
  }

  SmallString<256> RealPath;
  StringRef RealFilename;

  // Path can be
  // /usr/lib/gcc/x86_64-linux-gnu/8/../../../../include/c++/8/bits/*.h so for
  // such a case we must get the real_path.
  if (sys::fs::real_path(Filename, RealPath)) {
    // real_path can fail with path like "foo.c".
    RealFilename = Filename;
  } else {
    RealFilename = RealPath;
  }

  bool ShouldInstrument;
  if (FilterRe.empty()) {
    ShouldInstrument = !doesFilenameMatchARegex(RealFilename, ExcludeRe);
  } else if (ExcludeRe.empty()) {
    ShouldInstrument = doesFilenameMatchARegex(RealFilename, FilterRe);
  } else {
    ShouldInstrument = doesFilenameMatchARegex(RealFilename, FilterRe) &&
                       !doesFilenameMatchARegex(RealFilename, ExcludeRe);
  }
  InstrumentedFiles[Filename] = ShouldInstrument;
  return ShouldInstrument;
}

std::string GCOVProfiler::mangleName(const DICompileUnit *CU,
                                     GCovFileType OutputType) {
  bool Notes = OutputType == GCovFileType::GCNO;

  if (NamedMDNode *GCov = M->getNamedMetadata("llvm.gcov")) {
    for (int i = 0, e = GCov->getNumOperands(); i != e; ++i) {
      MDNode *N = GCov->getOperand(i);
      bool ThreeElement = N->getNumOperands() == 3;
      if (!ThreeElement && N->getNumOperands() != 2)
        continue;
      if (dyn_cast<MDNode>(N->getOperand(ThreeElement ? 2 : 1)) != CU)
        continue;

      if (ThreeElement) {
        // These nodes have no mangling to apply, it's stored mangled in the
        // bitcode.
        MDString *NotesFile = dyn_cast<MDString>(N->getOperand(0));
        MDString *DataFile = dyn_cast<MDString>(N->getOperand(1));
        if (!NotesFile || !DataFile)
          continue;
        return std::string(Notes ? NotesFile->getString()
                                 : DataFile->getString());
      }

      MDString *GCovFile = dyn_cast<MDString>(N->getOperand(0));
      if (!GCovFile)
        continue;

      SmallString<128> Filename = GCovFile->getString();
      sys::path::replace_extension(Filename, Notes ? "gcno" : "gcda");
      return std::string(Filename);
    }
  }

  SmallString<128> Filename = CU->getFilename();
  sys::path::replace_extension(Filename, Notes ? "gcno" : "gcda");
  StringRef FName = sys::path::filename(Filename);
  SmallString<128> CurPath;
  if (sys::fs::current_path(CurPath))
    return std::string(FName);
  sys::path::append(CurPath, FName);
  return std::string(CurPath);
}

bool GCOVProfiler::runOnModule(
    Module &M, function_ref<BlockFrequencyInfo *(Function &F)> GetBFI,
    function_ref<BranchProbabilityInfo *(Function &F)> GetBPI,
    std::function<const TargetLibraryInfo &(Function &F)> GetTLI) {
  this->M = &M;
  this->GetTLI = std::move(GetTLI);
  Ctx = &M.getContext();

  NamedMDNode *CUNode = M.getNamedMetadata("llvm.dbg.cu");
  if (!CUNode || (!Options.EmitNotes && !Options.EmitData))
    return false;

  bool HasExecOrFork = AddFlushBeforeForkAndExec();

  FilterRe = createRegexesFromString(Options.Filter);
  ExcludeRe = createRegexesFromString(Options.Exclude);
  emitProfileNotes(CUNode, HasExecOrFork, GetBFI, GetBPI, this->GetTLI);
  return true;
}

PreservedAnalyses GCOVProfilerPass::run(Module &M,
                                        ModuleAnalysisManager &AM) {

  GCOVProfiler Profiler(GCOVOpts);
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  auto GetBFI = [&FAM](Function &F) {
    return &FAM.getResult<BlockFrequencyAnalysis>(F);
  };
  auto GetBPI = [&FAM](Function &F) {
    return &FAM.getResult<BranchProbabilityAnalysis>(F);
  };
  auto GetTLI = [&FAM](Function &F) -> const TargetLibraryInfo & {
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };

  if (!Profiler.runOnModule(M, GetBFI, GetBPI, GetTLI))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

static bool functionHasLines(const Function &F, unsigned &EndLine) {
  // Check whether this function actually has any source lines. Not only
  // do these waste space, they also can crash gcov.
  EndLine = 0;
  for (const auto &BB : F) {
    for (const auto &I : BB) {
      // Debug intrinsic locations correspond to the location of the
      // declaration, not necessarily any statements or expressions.
      if (isa<DbgInfoIntrinsic>(&I)) continue;

      const DebugLoc &Loc = I.getDebugLoc();
      if (!Loc)
        continue;

      // Artificial lines such as calls to the global constructors.
      if (Loc.getLine() == 0) continue;
      EndLine = std::max(EndLine, Loc.getLine());

      return true;
    }
  }
  return false;
}

static bool isUsingScopeBasedEH(Function &F) {
  if (!F.hasPersonalityFn()) return false;

  EHPersonality Personality = classifyEHPersonality(F.getPersonalityFn());
  return isScopedEHPersonality(Personality);
}

bool GCOVProfiler::AddFlushBeforeForkAndExec() {
  const TargetLibraryInfo *TLI = nullptr;
  SmallVector<CallInst *, 2> Forks;
  SmallVector<CallInst *, 2> Execs;
  for (auto &F : M->functions()) {
    TLI = TLI == nullptr ? &GetTLI(F) : TLI;
    for (auto &I : instructions(F)) {
      if (CallInst *CI = dyn_cast<CallInst>(&I)) {
        if (Function *Callee = CI->getCalledFunction()) {
          LibFunc LF;
          if (TLI->getLibFunc(*Callee, LF)) {
            if (LF == LibFunc_fork) {
#if !defined(_WIN32)
              Forks.push_back(CI);
#endif
            } else if (LF == LibFunc_execl || LF == LibFunc_execle ||
                       LF == LibFunc_execlp || LF == LibFunc_execv ||
                       LF == LibFunc_execvp || LF == LibFunc_execve ||
                       LF == LibFunc_execvpe || LF == LibFunc_execvP) {
              Execs.push_back(CI);
            }
          }
        }
      }
    }
  }

  for (auto *F : Forks) {
    IRBuilder<> Builder(F);
    BasicBlock *Parent = F->getParent();
    auto NextInst = ++F->getIterator();

    // We've a fork so just reset the counters in the child process
    FunctionType *FTy = FunctionType::get(Builder.getInt32Ty(), {}, false);
    FunctionCallee GCOVFork = M->getOrInsertFunction(
        "__gcov_fork", FTy,
        TLI->getAttrList(Ctx, {}, /*Signed=*/true, /*Ret=*/true));
    F->setCalledFunction(GCOVFork);

    // We split just after the fork to have a counter for the lines after
    // Anyway there's a bug:
    // void foo() { fork(); }
    // void bar() { foo(); blah(); }
    // then "blah();" will be called 2 times but showed as 1
    // because "blah()" belongs to the same block as "foo();"
    Parent->splitBasicBlock(NextInst);

    // back() is a br instruction with a debug location
    // equals to the one from NextAfterFork
    // So to avoid to have two debug locs on two blocks just change it
    DebugLoc Loc = F->getDebugLoc();
    Parent->back().setDebugLoc(Loc);
  }

  for (auto *E : Execs) {
    IRBuilder<> Builder(E);
    BasicBlock *Parent = E->getParent();
    auto NextInst = ++E->getIterator();

    // Since the process is replaced by a new one we need to write out gcdas
    // No need to reset the counters since they'll be lost after the exec**
    FunctionType *FTy = FunctionType::get(Builder.getVoidTy(), {}, false);
    FunctionCallee WriteoutF =
        M->getOrInsertFunction("llvm_writeout_files", FTy);
    Builder.CreateCall(WriteoutF);

    DebugLoc Loc = E->getDebugLoc();
    Builder.SetInsertPoint(&*NextInst);
    // If the exec** fails we must reset the counters since they've been
    // dumped
    FunctionCallee ResetF = M->getOrInsertFunction("llvm_reset_counters", FTy);
    Builder.CreateCall(ResetF)->setDebugLoc(Loc);
    ExecBlocks.insert(Parent);
    Parent->splitBasicBlock(NextInst);
    Parent->back().setDebugLoc(Loc);
  }

  return !Forks.empty() || !Execs.empty();
}

static BasicBlock *getInstrBB(CFGMST<Edge, BBInfo> &MST, Edge &E,
                              const DenseSet<const BasicBlock *> &ExecBlocks) {
  if (E.InMST || E.Removed)
    return nullptr;

  BasicBlock *SrcBB = const_cast<BasicBlock *>(E.SrcBB);
  BasicBlock *DestBB = const_cast<BasicBlock *>(E.DestBB);
  // For a fake edge, instrument the real BB.
  if (SrcBB == nullptr)
    return DestBB;
  if (DestBB == nullptr)
    return SrcBB;

  auto CanInstrument = [](BasicBlock *BB) -> BasicBlock * {
    // There are basic blocks (such as catchswitch) cannot be instrumented.
    // If the returned first insertion point is the end of BB, skip this BB.
    if (BB->getFirstInsertionPt() == BB->end())
      return nullptr;
    return BB;
  };

  // Instrument the SrcBB if it has a single successor,
  // otherwise, the DestBB if this is not a critical edge.
  Instruction *TI = SrcBB->getTerminator();
  if (TI->getNumSuccessors() <= 1 && !ExecBlocks.count(SrcBB))
    return CanInstrument(SrcBB);
  if (!E.IsCritical)
    return CanInstrument(DestBB);

  // Some IndirectBr critical edges cannot be split by the previous
  // SplitIndirectBrCriticalEdges call. Bail out.
  const unsigned SuccNum = GetSuccessorNumber(SrcBB, DestBB);
  BasicBlock *InstrBB =
      isa<IndirectBrInst>(TI) ? nullptr : SplitCriticalEdge(TI, SuccNum);
  if (!InstrBB)
    return nullptr;

  MST.addEdge(SrcBB, InstrBB, 0);
  MST.addEdge(InstrBB, DestBB, 0).InMST = true;
  E.Removed = true;

  return CanInstrument(InstrBB);
}

#ifndef NDEBUG
static void dumpEdges(CFGMST<Edge, BBInfo> &MST, GCOVFunction &GF) {
  size_t ID = 0;
  for (const auto &E : make_pointee_range(MST.allEdges())) {
    GCOVBlock &Src = E.SrcBB ? GF.getBlock(E.SrcBB) : GF.getEntryBlock();
    GCOVBlock &Dst = E.DestBB ? GF.getBlock(E.DestBB) : GF.getReturnBlock();
    dbgs() << "  Edge " << ID++ << ": " << Src.Number << "->" << Dst.Number
           << E.infoString() << "\n";
  }
}
#endif

bool GCOVProfiler::emitProfileNotes(
    NamedMDNode *CUNode, bool HasExecOrFork,
    function_ref<BlockFrequencyInfo *(Function &F)> GetBFI,
    function_ref<BranchProbabilityInfo *(Function &F)> GetBPI,
    function_ref<const TargetLibraryInfo &(Function &F)> GetTLI) {
  int Version;
  {
    uint8_t c3 = Options.Version[0];
    uint8_t c2 = Options.Version[1];
    uint8_t c1 = Options.Version[2];
    Version = c3 >= 'A' ? (c3 - 'A') * 100 + (c2 - '0') * 10 + c1 - '0'
                        : (c3 - '0') * 10 + c1 - '0';
  }

  bool EmitGCDA = Options.EmitData;
  for (unsigned i = 0, e = CUNode->getNumOperands(); i != e; ++i) {
    // Each compile unit gets its own .gcno file. This means that whether we run
    // this pass over the original .o's as they're produced, or run it after
    // LTO, we'll generate the same .gcno files.

    auto *CU = cast<DICompileUnit>(CUNode->getOperand(i));

    // Skip module skeleton (and module) CUs.
    if (CU->getDWOId())
      continue;

    std::vector<uint8_t> EdgeDestinations;
    SmallVector<std::pair<GlobalVariable *, MDNode *>, 8> CountersBySP;

    Endian = M->getDataLayout().isLittleEndian() ? llvm::endianness::little
                                                 : llvm::endianness::big;
    unsigned FunctionIdent = 0;
    for (auto &F : M->functions()) {
      DISubprogram *SP = F.getSubprogram();
      unsigned EndLine;
      if (!SP) continue;
      if (!functionHasLines(F, EndLine) || !isFunctionInstrumented(F))
        continue;
      // TODO: Functions using scope-based EH are currently not supported.
      if (isUsingScopeBasedEH(F)) continue;
      if (F.hasFnAttribute(llvm::Attribute::NoProfile))
        continue;
      if (F.hasFnAttribute(llvm::Attribute::SkipProfile))
        continue;

      // Add the function line number to the lines of the entry block
      // to have a counter for the function definition.
      uint32_t Line = SP->getLine();
      auto Filename = getFilename(SP);

      BranchProbabilityInfo *BPI = GetBPI(F);
      BlockFrequencyInfo *BFI = GetBFI(F);

      // Split indirectbr critical edges here before computing the MST rather
      // than later in getInstrBB() to avoid invalidating it.
      SplitIndirectBrCriticalEdges(F, /*IgnoreBlocksWithoutPHI=*/false, BPI,
                                   BFI);

      CFGMST<Edge, BBInfo> MST(F, /*InstrumentFuncEntry_=*/false, BPI, BFI);

      // getInstrBB can split basic blocks and push elements to AllEdges.
      for (size_t I : llvm::seq<size_t>(0, MST.numEdges())) {
        auto &E = *MST.allEdges()[I];
        // For now, disable spanning tree optimization when fork or exec* is
        // used.
        if (HasExecOrFork)
          E.InMST = false;
        E.Place = getInstrBB(MST, E, ExecBlocks);
      }
      // Basic blocks in F are finalized at this point.
      BasicBlock &EntryBlock = F.getEntryBlock();
      Funcs.push_back(std::make_unique<GCOVFunction>(this, &F, SP, EndLine,
                                                     FunctionIdent++, Version));
      GCOVFunction &Func = *Funcs.back();

      // Some non-tree edges are IndirectBr which cannot be split. Ignore them
      // as well.
      llvm::erase_if(MST.allEdges(), [](std::unique_ptr<Edge> &E) {
        return E->Removed || (!E->InMST && !E->Place);
      });
      const size_t Measured =
          std::stable_partition(
              MST.allEdges().begin(), MST.allEdges().end(),
              [](std::unique_ptr<Edge> &E) { return E->Place; }) -
          MST.allEdges().begin();
      for (size_t I : llvm::seq<size_t>(0, Measured)) {
        Edge &E = *MST.allEdges()[I];
        GCOVBlock &Src =
            E.SrcBB ? Func.getBlock(E.SrcBB) : Func.getEntryBlock();
        GCOVBlock &Dst =
            E.DestBB ? Func.getBlock(E.DestBB) : Func.getReturnBlock();
        E.SrcNumber = Src.Number;
        E.DstNumber = Dst.Number;
      }
      std::stable_sort(
          MST.allEdges().begin(), MST.allEdges().begin() + Measured,
          [](const std::unique_ptr<Edge> &L, const std::unique_ptr<Edge> &R) {
            return L->SrcNumber != R->SrcNumber ? L->SrcNumber < R->SrcNumber
                                                : L->DstNumber < R->DstNumber;
          });

      for (const Edge &E : make_pointee_range(MST.allEdges())) {
        GCOVBlock &Src =
            E.SrcBB ? Func.getBlock(E.SrcBB) : Func.getEntryBlock();
        GCOVBlock &Dst =
            E.DestBB ? Func.getBlock(E.DestBB) : Func.getReturnBlock();
        Src.addEdge(Dst, E.Place ? 0 : uint32_t(GCOV_ARC_ON_TREE));
      }

      // Artificial functions such as global initializers
      if (!SP->isArtificial())
        Func.getBlock(&EntryBlock).getFile(Filename).addLine(Line);

      LLVM_DEBUG(dumpEdges(MST, Func));

      for (auto &GB : Func.Blocks) {
        const BasicBlock &BB = *GB.first;
        auto &Block = GB.second;
        for (auto Succ : Block.OutEdges) {
          uint32_t Idx = Succ.first->Number;
          do EdgeDestinations.push_back(Idx & 255);
          while ((Idx >>= 8) > 0);
        }

        for (const auto &I : BB) {
          // Debug intrinsic locations correspond to the location of the
          // declaration, not necessarily any statements or expressions.
          if (isa<DbgInfoIntrinsic>(&I)) continue;

          const DebugLoc &Loc = I.getDebugLoc();
          if (!Loc)
            continue;

          // Artificial lines such as calls to the global constructors.
          if (Loc.getLine() == 0 || Loc.isImplicitCode())
            continue;

          if (Line == Loc.getLine()) continue;
          Line = Loc.getLine();
          MDNode *Scope = Loc.getScope();
          // TODO: Handle blocks from another file due to #line, #include, etc.
          if (isa<DILexicalBlockFile>(Scope) || SP != getDISubprogram(Scope))
            continue;

          GCOVLines &Lines = Block.getFile(Filename);
          Lines.addLine(Loc.getLine());
        }
        Line = 0;
      }
      if (EmitGCDA) {
        DISubprogram *SP = F.getSubprogram();
        ArrayType *CounterTy = ArrayType::get(Type::getInt64Ty(*Ctx), Measured);
        GlobalVariable *Counters = new GlobalVariable(
            *M, CounterTy, false, GlobalValue::InternalLinkage,
            Constant::getNullValue(CounterTy), "__llvm_gcov_ctr");
        CountersBySP.emplace_back(Counters, SP);

        for (size_t I : llvm::seq<size_t>(0, Measured)) {
          const Edge &E = *MST.allEdges()[I];
          IRBuilder<> Builder(E.Place, E.Place->getFirstInsertionPt());
          Value *V = Builder.CreateConstInBoundsGEP2_64(
              Counters->getValueType(), Counters, 0, I);
          // Disable sanitizers to decrease size bloat. We don't expect
          // sanitizers to catch interesting issues.
          Instruction *Inst;
          if (Options.Atomic) {
            Inst = Builder.CreateAtomicRMW(AtomicRMWInst::Add, V,
                                           Builder.getInt64(1), MaybeAlign(),
                                           AtomicOrdering::Monotonic);
          } else {
            LoadInst *OldCount =
                Builder.CreateLoad(Builder.getInt64Ty(), V, "gcov_ctr");
            OldCount->setNoSanitizeMetadata();
            Value *NewCount = Builder.CreateAdd(OldCount, Builder.getInt64(1));
            Inst = Builder.CreateStore(NewCount, V);
          }
          Inst->setNoSanitizeMetadata();
        }
      }
    }

    char Tmp[4];
    JamCRC JC;
    JC.update(EdgeDestinations);
    uint32_t Stamp = JC.getCRC();
    FileChecksums.push_back(Stamp);

    if (Options.EmitNotes) {
      std::error_code EC;
      raw_fd_ostream out(mangleName(CU, GCovFileType::GCNO), EC,
                         sys::fs::OF_None);
      if (EC) {
        Ctx->emitError(
            Twine("failed to open coverage notes file for writing: ") +
            EC.message());
        continue;
      }
      os = &out;
      if (Endian == llvm::endianness::big) {
        out.write("gcno", 4);
        out.write(Options.Version, 4);
      } else {
        out.write("oncg", 4);
        std::reverse_copy(Options.Version, Options.Version + 4, Tmp);
        out.write(Tmp, 4);
      }
      write(Stamp);
      if (Version >= 90)
        writeString(""); // unuseful current_working_directory
      if (Version >= 80)
        write(0); // unuseful has_unexecuted_blocks

      for (auto &Func : Funcs)
        Func->writeOut(Stamp);

      write(0);
      write(0);
      out.close();
    }

    if (EmitGCDA) {
      emitGlobalConstructor(CountersBySP);
      EmitGCDA = false;
    }
  }
  return true;
}

Function *GCOVProfiler::createInternalFunction(FunctionType *FTy,
                                               StringRef Name,
                                               StringRef MangledType /*=""*/) {
  Function *F = Function::createWithDefaultAttr(
      FTy, GlobalValue::InternalLinkage, 0, Name, M);
  F->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  F->addFnAttr(Attribute::NoUnwind);
  if (Options.NoRedZone)
    F->addFnAttr(Attribute::NoRedZone);
  if (!MangledType.empty())
    setKCFIType(*M, *F, MangledType);
  return F;
}

void GCOVProfiler::emitGlobalConstructor(
    SmallVectorImpl<std::pair<GlobalVariable *, MDNode *>> &CountersBySP) {
  Function *WriteoutF = insertCounterWriteout(CountersBySP);
  Function *ResetF = insertReset(CountersBySP);

  // Create a small bit of code that registers the "__llvm_gcov_writeout" to
  // be executed at exit and the "__llvm_gcov_reset" function to be executed
  // when "__gcov_flush" is called.
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
  Function *F = createInternalFunction(FTy, "__llvm_gcov_init", "_ZTSFvvE");
  F->addFnAttr(Attribute::NoInline);

  BasicBlock *BB = BasicBlock::Create(*Ctx, "entry", F);
  IRBuilder<> Builder(BB);

  FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
  auto *PFTy = PointerType::get(FTy, 0);
  FTy = FunctionType::get(Builder.getVoidTy(), {PFTy, PFTy}, false);

  // Initialize the environment and register the local writeout, flush and
  // reset functions.
  FunctionCallee GCOVInit = M->getOrInsertFunction("llvm_gcov_init", FTy);
  Builder.CreateCall(GCOVInit, {WriteoutF, ResetF});
  Builder.CreateRetVoid();

  appendToGlobalCtors(*M, F, 0);
}

FunctionCallee GCOVProfiler::getStartFileFunc(const TargetLibraryInfo *TLI) {
  Type *Args[] = {
      PointerType::getUnqual(*Ctx), // const char *orig_filename
      Type::getInt32Ty(*Ctx),       // uint32_t version
      Type::getInt32Ty(*Ctx),       // uint32_t checksum
  };
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx), Args, false);
  return M->getOrInsertFunction("llvm_gcda_start_file", FTy,
                                TLI->getAttrList(Ctx, {1, 2}, /*Signed=*/false));
}

FunctionCallee GCOVProfiler::getEmitFunctionFunc(const TargetLibraryInfo *TLI) {
  Type *Args[] = {
    Type::getInt32Ty(*Ctx),    // uint32_t ident
    Type::getInt32Ty(*Ctx),    // uint32_t func_checksum
    Type::getInt32Ty(*Ctx),    // uint32_t cfg_checksum
  };
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx), Args, false);
  return M->getOrInsertFunction("llvm_gcda_emit_function", FTy,
                             TLI->getAttrList(Ctx, {0, 1, 2}, /*Signed=*/false));
}

FunctionCallee GCOVProfiler::getEmitArcsFunc(const TargetLibraryInfo *TLI) {
  Type *Args[] = {
      Type::getInt32Ty(*Ctx),       // uint32_t num_counters
      PointerType::getUnqual(*Ctx), // uint64_t *counters
  };
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx), Args, false);
  return M->getOrInsertFunction("llvm_gcda_emit_arcs", FTy,
                                TLI->getAttrList(Ctx, {0}, /*Signed=*/false));
}

FunctionCallee GCOVProfiler::getSummaryInfoFunc() {
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
  return M->getOrInsertFunction("llvm_gcda_summary_info", FTy);
}

FunctionCallee GCOVProfiler::getEndFileFunc() {
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
  return M->getOrInsertFunction("llvm_gcda_end_file", FTy);
}

Function *GCOVProfiler::insertCounterWriteout(
    ArrayRef<std::pair<GlobalVariable *, MDNode *> > CountersBySP) {
  FunctionType *WriteoutFTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
  Function *WriteoutF = M->getFunction("__llvm_gcov_writeout");
  if (!WriteoutF)
    WriteoutF =
        createInternalFunction(WriteoutFTy, "__llvm_gcov_writeout", "_ZTSFvvE");
  WriteoutF->addFnAttr(Attribute::NoInline);

  BasicBlock *BB = BasicBlock::Create(*Ctx, "entry", WriteoutF);
  IRBuilder<> Builder(BB);

  auto *TLI = &GetTLI(*WriteoutF);

  FunctionCallee StartFile = getStartFileFunc(TLI);
  FunctionCallee EmitFunction = getEmitFunctionFunc(TLI);
  FunctionCallee EmitArcs = getEmitArcsFunc(TLI);
  FunctionCallee SummaryInfo = getSummaryInfoFunc();
  FunctionCallee EndFile = getEndFileFunc();

  NamedMDNode *CUNodes = M->getNamedMetadata("llvm.dbg.cu");
  if (!CUNodes) {
    Builder.CreateRetVoid();
    return WriteoutF;
  }

  // Collect the relevant data into a large constant data structure that we can
  // walk to write out everything.
  StructType *StartFileCallArgsTy = StructType::create(
      {Builder.getPtrTy(), Builder.getInt32Ty(), Builder.getInt32Ty()},
      "start_file_args_ty");
  StructType *EmitFunctionCallArgsTy = StructType::create(
      {Builder.getInt32Ty(), Builder.getInt32Ty(), Builder.getInt32Ty()},
      "emit_function_args_ty");
  auto *PtrTy = Builder.getPtrTy();
  StructType *EmitArcsCallArgsTy =
      StructType::create({Builder.getInt32Ty(), PtrTy}, "emit_arcs_args_ty");
  StructType *FileInfoTy = StructType::create(
      {StartFileCallArgsTy, Builder.getInt32Ty(), PtrTy, PtrTy}, "file_info");

  Constant *Zero32 = Builder.getInt32(0);
  // Build an explicit array of two zeros for use in ConstantExpr GEP building.
  Constant *TwoZero32s[] = {Zero32, Zero32};

  SmallVector<Constant *, 8> FileInfos;
  for (int i : llvm::seq<int>(0, CUNodes->getNumOperands())) {
    auto *CU = cast<DICompileUnit>(CUNodes->getOperand(i));

    // Skip module skeleton (and module) CUs.
    if (CU->getDWOId())
      continue;

    std::string FilenameGcda = mangleName(CU, GCovFileType::GCDA);
    uint32_t CfgChecksum = FileChecksums.empty() ? 0 : FileChecksums[i];
    auto *StartFileCallArgs = ConstantStruct::get(
        StartFileCallArgsTy,
        {Builder.CreateGlobalStringPtr(FilenameGcda),
         Builder.getInt32(endian::read32be(Options.Version)),
         Builder.getInt32(CfgChecksum)});

    SmallVector<Constant *, 8> EmitFunctionCallArgsArray;
    SmallVector<Constant *, 8> EmitArcsCallArgsArray;
    for (int j : llvm::seq<int>(0, CountersBySP.size())) {
      uint32_t FuncChecksum = Funcs.empty() ? 0 : Funcs[j]->getFuncChecksum();
      EmitFunctionCallArgsArray.push_back(ConstantStruct::get(
          EmitFunctionCallArgsTy,
          {Builder.getInt32(j),
           Builder.getInt32(FuncChecksum),
           Builder.getInt32(CfgChecksum)}));

      GlobalVariable *GV = CountersBySP[j].first;
      unsigned Arcs = cast<ArrayType>(GV->getValueType())->getNumElements();
      EmitArcsCallArgsArray.push_back(ConstantStruct::get(
          EmitArcsCallArgsTy,
          {Builder.getInt32(Arcs), ConstantExpr::getInBoundsGetElementPtr(
                                       GV->getValueType(), GV, TwoZero32s)}));
    }
    // Create global arrays for the two emit calls.
    int CountersSize = CountersBySP.size();
    assert(CountersSize == (int)EmitFunctionCallArgsArray.size() &&
           "Mismatched array size!");
    assert(CountersSize == (int)EmitArcsCallArgsArray.size() &&
           "Mismatched array size!");
    auto *EmitFunctionCallArgsArrayTy =
        ArrayType::get(EmitFunctionCallArgsTy, CountersSize);
    auto *EmitFunctionCallArgsArrayGV = new GlobalVariable(
        *M, EmitFunctionCallArgsArrayTy, /*isConstant*/ true,
        GlobalValue::InternalLinkage,
        ConstantArray::get(EmitFunctionCallArgsArrayTy,
                           EmitFunctionCallArgsArray),
        Twine("__llvm_internal_gcov_emit_function_args.") + Twine(i));
    auto *EmitArcsCallArgsArrayTy =
        ArrayType::get(EmitArcsCallArgsTy, CountersSize);
    EmitFunctionCallArgsArrayGV->setUnnamedAddr(
        GlobalValue::UnnamedAddr::Global);
    auto *EmitArcsCallArgsArrayGV = new GlobalVariable(
        *M, EmitArcsCallArgsArrayTy, /*isConstant*/ true,
        GlobalValue::InternalLinkage,
        ConstantArray::get(EmitArcsCallArgsArrayTy, EmitArcsCallArgsArray),
        Twine("__llvm_internal_gcov_emit_arcs_args.") + Twine(i));
    EmitArcsCallArgsArrayGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

    FileInfos.push_back(ConstantStruct::get(
        FileInfoTy,
        {StartFileCallArgs, Builder.getInt32(CountersSize),
         ConstantExpr::getInBoundsGetElementPtr(EmitFunctionCallArgsArrayTy,
                                                EmitFunctionCallArgsArrayGV,
                                                TwoZero32s),
         ConstantExpr::getInBoundsGetElementPtr(
             EmitArcsCallArgsArrayTy, EmitArcsCallArgsArrayGV, TwoZero32s)}));
  }

  // If we didn't find anything to actually emit, bail on out.
  if (FileInfos.empty()) {
    Builder.CreateRetVoid();
    return WriteoutF;
  }

  // To simplify code, we cap the number of file infos we write out to fit
  // easily in a 32-bit signed integer. This gives consistent behavior between
  // 32-bit and 64-bit systems without requiring (potentially very slow) 64-bit
  // operations on 32-bit systems. It also seems unreasonable to try to handle
  // more than 2 billion files.
  if ((int64_t)FileInfos.size() > (int64_t)INT_MAX)
    FileInfos.resize(INT_MAX);

  // Create a global for the entire data structure so we can walk it more
  // easily.
  auto *FileInfoArrayTy = ArrayType::get(FileInfoTy, FileInfos.size());
  auto *FileInfoArrayGV = new GlobalVariable(
      *M, FileInfoArrayTy, /*isConstant*/ true, GlobalValue::InternalLinkage,
      ConstantArray::get(FileInfoArrayTy, FileInfos),
      "__llvm_internal_gcov_emit_file_info");
  FileInfoArrayGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  // Create the CFG for walking this data structure.
  auto *FileLoopHeader =
      BasicBlock::Create(*Ctx, "file.loop.header", WriteoutF);
  auto *CounterLoopHeader =
      BasicBlock::Create(*Ctx, "counter.loop.header", WriteoutF);
  auto *FileLoopLatch = BasicBlock::Create(*Ctx, "file.loop.latch", WriteoutF);
  auto *ExitBB = BasicBlock::Create(*Ctx, "exit", WriteoutF);

  // We always have at least one file, so just branch to the header.
  Builder.CreateBr(FileLoopHeader);

  // The index into the files structure is our loop induction variable.
  Builder.SetInsertPoint(FileLoopHeader);
  PHINode *IV = Builder.CreatePHI(Builder.getInt32Ty(), /*NumReservedValues*/ 2,
                                  "file_idx");
  IV->addIncoming(Builder.getInt32(0), BB);
  auto *FileInfoPtr = Builder.CreateInBoundsGEP(
      FileInfoArrayTy, FileInfoArrayGV, {Builder.getInt32(0), IV});
  auto *StartFileCallArgsPtr =
      Builder.CreateStructGEP(FileInfoTy, FileInfoPtr, 0, "start_file_args");
  auto *StartFileCall = Builder.CreateCall(
      StartFile,
      {Builder.CreateLoad(StartFileCallArgsTy->getElementType(0),
                          Builder.CreateStructGEP(StartFileCallArgsTy,
                                                  StartFileCallArgsPtr, 0),
                          "filename"),
       Builder.CreateLoad(StartFileCallArgsTy->getElementType(1),
                          Builder.CreateStructGEP(StartFileCallArgsTy,
                                                  StartFileCallArgsPtr, 1),
                          "version"),
       Builder.CreateLoad(StartFileCallArgsTy->getElementType(2),
                          Builder.CreateStructGEP(StartFileCallArgsTy,
                                                  StartFileCallArgsPtr, 2),
                          "stamp")});
  if (auto AK = TLI->getExtAttrForI32Param(false))
    StartFileCall->addParamAttr(2, AK);
  auto *NumCounters = Builder.CreateLoad(
      FileInfoTy->getElementType(1),
      Builder.CreateStructGEP(FileInfoTy, FileInfoPtr, 1), "num_ctrs");
  auto *EmitFunctionCallArgsArray =
      Builder.CreateLoad(FileInfoTy->getElementType(2),
                         Builder.CreateStructGEP(FileInfoTy, FileInfoPtr, 2),
                         "emit_function_args");
  auto *EmitArcsCallArgsArray = Builder.CreateLoad(
      FileInfoTy->getElementType(3),
      Builder.CreateStructGEP(FileInfoTy, FileInfoPtr, 3), "emit_arcs_args");
  auto *EnterCounterLoopCond =
      Builder.CreateICmpSLT(Builder.getInt32(0), NumCounters);
  Builder.CreateCondBr(EnterCounterLoopCond, CounterLoopHeader, FileLoopLatch);

  Builder.SetInsertPoint(CounterLoopHeader);
  auto *JV = Builder.CreatePHI(Builder.getInt32Ty(), /*NumReservedValues*/ 2,
                               "ctr_idx");
  JV->addIncoming(Builder.getInt32(0), FileLoopHeader);
  auto *EmitFunctionCallArgsPtr = Builder.CreateInBoundsGEP(
      EmitFunctionCallArgsTy, EmitFunctionCallArgsArray, JV);
  auto *EmitFunctionCall = Builder.CreateCall(
      EmitFunction,
      {Builder.CreateLoad(EmitFunctionCallArgsTy->getElementType(0),
                          Builder.CreateStructGEP(EmitFunctionCallArgsTy,
                                                  EmitFunctionCallArgsPtr, 0),
                          "ident"),
       Builder.CreateLoad(EmitFunctionCallArgsTy->getElementType(1),
                          Builder.CreateStructGEP(EmitFunctionCallArgsTy,
                                                  EmitFunctionCallArgsPtr, 1),
                          "func_checkssum"),
       Builder.CreateLoad(EmitFunctionCallArgsTy->getElementType(2),
                          Builder.CreateStructGEP(EmitFunctionCallArgsTy,
                                                  EmitFunctionCallArgsPtr, 2),
                          "cfg_checksum")});
  if (auto AK = TLI->getExtAttrForI32Param(false)) {
    EmitFunctionCall->addParamAttr(0, AK);
    EmitFunctionCall->addParamAttr(1, AK);
    EmitFunctionCall->addParamAttr(2, AK);
  }
  auto *EmitArcsCallArgsPtr =
      Builder.CreateInBoundsGEP(EmitArcsCallArgsTy, EmitArcsCallArgsArray, JV);
  auto *EmitArcsCall = Builder.CreateCall(
      EmitArcs,
      {Builder.CreateLoad(
           EmitArcsCallArgsTy->getElementType(0),
           Builder.CreateStructGEP(EmitArcsCallArgsTy, EmitArcsCallArgsPtr, 0),
           "num_counters"),
       Builder.CreateLoad(
           EmitArcsCallArgsTy->getElementType(1),
           Builder.CreateStructGEP(EmitArcsCallArgsTy, EmitArcsCallArgsPtr, 1),
           "counters")});
  if (auto AK = TLI->getExtAttrForI32Param(false))
    EmitArcsCall->addParamAttr(0, AK);
  auto *NextJV = Builder.CreateAdd(JV, Builder.getInt32(1));
  auto *CounterLoopCond = Builder.CreateICmpSLT(NextJV, NumCounters);
  Builder.CreateCondBr(CounterLoopCond, CounterLoopHeader, FileLoopLatch);
  JV->addIncoming(NextJV, CounterLoopHeader);

  Builder.SetInsertPoint(FileLoopLatch);
  Builder.CreateCall(SummaryInfo, {});
  Builder.CreateCall(EndFile, {});
  auto *NextIV = Builder.CreateAdd(IV, Builder.getInt32(1), "next_file_idx");
  auto *FileLoopCond =
      Builder.CreateICmpSLT(NextIV, Builder.getInt32(FileInfos.size()));
  Builder.CreateCondBr(FileLoopCond, FileLoopHeader, ExitBB);
  IV->addIncoming(NextIV, FileLoopLatch);

  Builder.SetInsertPoint(ExitBB);
  Builder.CreateRetVoid();

  return WriteoutF;
}

Function *GCOVProfiler::insertReset(
    ArrayRef<std::pair<GlobalVariable *, MDNode *>> CountersBySP) {
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
  Function *ResetF = M->getFunction("__llvm_gcov_reset");
  if (!ResetF)
    ResetF = createInternalFunction(FTy, "__llvm_gcov_reset", "_ZTSFvvE");
  ResetF->addFnAttr(Attribute::NoInline);

  BasicBlock *Entry = BasicBlock::Create(*Ctx, "entry", ResetF);
  IRBuilder<> Builder(Entry);
  LLVMContext &C = Entry->getContext();

  // Zero out the counters.
  for (const auto &I : CountersBySP) {
    GlobalVariable *GV = I.first;
    auto *GVTy = cast<ArrayType>(GV->getValueType());
    Builder.CreateMemSet(GV, Constant::getNullValue(Type::getInt8Ty(C)),
                         GVTy->getNumElements() *
                             GVTy->getElementType()->getScalarSizeInBits() / 8,
                         GV->getAlign());
  }

  Type *RetTy = ResetF->getReturnType();
  if (RetTy->isVoidTy())
    Builder.CreateRetVoid();
  else if (RetTy->isIntegerTy())
    // Used if __llvm_gcov_reset was implicitly declared.
    Builder.CreateRet(ConstantInt::get(RetTy, 0));
  else
    report_fatal_error("invalid return type for __llvm_gcov_reset");

  return ResetF;
}
