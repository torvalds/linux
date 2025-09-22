//===-- HTMLLogger.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the HTML logger. Given a directory dir/, we write
// dir/0.html for the first analysis, etc.
// These files contain a visualization that allows inspecting the CFG and the
// state of the analysis at each point.
// Static assets (HTMLLogger.js, HTMLLogger.css) and SVG graphs etc are embedded
// so each output file is self-contained.
//
// VIEWS
//
// The timeline and function view are always shown. These allow selecting basic
// blocks, statements within them, and processing iterations (BBs are visited
// multiple times when e.g. loops are involved).
// These are written directly into the HTML body.
//
// There are also listings of particular basic blocks, and dumps of the state
// at particular analysis points (i.e. BB2 iteration 3 statement 2).
// These are only shown when the relevant BB/analysis point is *selected*.
//
// DATA AND TEMPLATES
//
// The HTML proper is mostly static.
// The analysis data is in a JSON object HTMLLoggerData which is embedded as
// a <script> in the <head>.
// This gets rendered into DOM by a simple template processor which substitutes
// the data into <template> tags embedded in the HTML. (see inflate() in JS).
//
// SELECTION
//
// This is the only real interactive mechanism.
//
// At any given time, there are several named selections, e.g.:
//   bb: B2               (basic block 0 is selected)
//   elt: B2.4            (statement 4 is selected)
//   iter: B2:1           (iteration 1 of the basic block is selected)
//   hover: B3            (hovering over basic block 3)
//
// The selection is updated by mouse events: hover by moving the mouse and
// others by clicking. Elements that are click targets generally have attributes
// (id or data-foo) that define what they should select.
// See watchSelection() in JS for the exact logic.
//
// When the "bb" selection is set to "B2":
//   - sections <section data-selection="bb"> get shown
//   - templates under such sections get re-rendered
//   - elements with class/id "B2" get class "bb-select"
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/DebugSupport.h"
#include "clang/Analysis/FlowSensitive/Logger.h"
#include "clang/Analysis/FlowSensitive/TypeErasedDataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
// Defines assets: HTMLLogger_{html_js,css}
#include "HTMLLogger.inc"

namespace clang::dataflow {
namespace {

// Render a graphviz graph specification to SVG using the `dot` tool.
llvm::Expected<std::string> renderSVG(llvm::StringRef DotGraph);

using StreamFactory = std::function<std::unique_ptr<llvm::raw_ostream>()>;

// Recursively dumps Values/StorageLocations as JSON
class ModelDumper {
public:
  ModelDumper(llvm::json::OStream &JOS, const Environment &Env)
      : JOS(JOS), Env(Env) {}

  void dump(Value &V) {
    JOS.attribute("value_id", llvm::to_string(&V));
    if (!Visited.insert(&V).second)
      return;

    JOS.attribute("kind", debugString(V.getKind()));

    switch (V.getKind()) {
    case Value::Kind::Integer:
    case Value::Kind::TopBool:
    case Value::Kind::AtomicBool:
    case Value::Kind::FormulaBool:
      break;
    case Value::Kind::Pointer:
      JOS.attributeObject(
          "pointee", [&] { dump(cast<PointerValue>(V).getPointeeLoc()); });
      break;
    }

    for (const auto& Prop : V.properties())
      JOS.attributeObject(("p:" + Prop.first()).str(),
                          [&] { dump(*Prop.second); });

    // Running the SAT solver is expensive, but knowing which booleans are
    // guaranteed true/false here is valuable and hard to determine by hand.
    if (auto *B = llvm::dyn_cast<BoolValue>(&V)) {
      JOS.attribute("formula", llvm::to_string(B->formula()));
      JOS.attribute("truth", Env.proves(B->formula()) ? "true"
                             : Env.proves(Env.arena().makeNot(B->formula()))
                                 ? "false"
                                 : "unknown");
    }
  }
  void dump(const StorageLocation &L) {
    JOS.attribute("location", llvm::to_string(&L));
    if (!Visited.insert(&L).second)
      return;

    JOS.attribute("type", L.getType().getAsString());
    if (!L.getType()->isRecordType())
      if (auto *V = Env.getValue(L))
        dump(*V);

    if (auto *RLoc = dyn_cast<RecordStorageLocation>(&L)) {
      for (const auto &Child : RLoc->children())
        JOS.attributeObject("f:" + Child.first->getNameAsString(), [&] {
          if (Child.second)
            if (Value *Val = Env.getValue(*Child.second))
              dump(*Val);
        });

      for (const auto &SyntheticField : RLoc->synthetic_fields())
        JOS.attributeObject(("sf:" + SyntheticField.first()).str(),
                            [&] { dump(*SyntheticField.second); });
    }
  }

  llvm::DenseSet<const void*> Visited;
  llvm::json::OStream &JOS;
  const Environment &Env;
};

class HTMLLogger : public Logger {
  struct Iteration {
    const CFGBlock *Block;
    unsigned Iter;
    bool PostVisit;
    bool Converged;
  };

  StreamFactory Streams;
  std::unique_ptr<llvm::raw_ostream> OS;
  std::string JSON;
  llvm::raw_string_ostream JStringStream{JSON};
  llvm::json::OStream JOS{JStringStream, /*Indent=*/2};

  const AdornedCFG *ACFG;
  // Timeline of iterations of CFG block visitation.
  std::vector<Iteration> Iters;
  // Indexes  in `Iters` of the iterations for each block.
  llvm::DenseMap<const CFGBlock *, llvm::SmallVector<size_t>> BlockIters;
  // For a given block ID, did the block converge (on the last iteration)?
  llvm::BitVector BlockConverged;
  // The messages logged in the current context but not yet written.
  std::string ContextLogs;
  // The number of elements we have visited within the current CFG block.
  unsigned ElementIndex;

public:
  explicit HTMLLogger(StreamFactory Streams) : Streams(std::move(Streams)) {}
  void beginAnalysis(const AdornedCFG &ACFG,
                     TypeErasedDataflowAnalysis &A) override {
    OS = Streams();
    this->ACFG = &ACFG;
    *OS << llvm::StringRef(HTMLLogger_html).split("<?INJECT?>").first;

    BlockConverged.resize(ACFG.getCFG().getNumBlockIDs());

    const auto &D = ACFG.getDecl();
    const auto &SM = A.getASTContext().getSourceManager();
    *OS << "<title>";
    if (const auto *ND = dyn_cast<NamedDecl>(&D))
      *OS << ND->getNameAsString() << " at ";
    *OS << SM.getFilename(D.getLocation()) << ":"
        << SM.getSpellingLineNumber(D.getLocation());
    *OS << "</title>\n";

    *OS << "<style>" << HTMLLogger_css << "</style>\n";
    *OS << "<script>" << HTMLLogger_js << "</script>\n";

    writeCode();
    JOS.objectBegin();
    JOS.attributeBegin("states");
    JOS.objectBegin();
  }
  // Between beginAnalysis() and endAnalysis() we write all the states for
  // particular analysis points into the `timeline` array.
  void endAnalysis() override {
    JOS.objectEnd();
    JOS.attributeEnd();

    JOS.attributeArray("timeline", [&] {
      for (const auto &E : Iters) {
        JOS.object([&] {
          JOS.attribute("block", blockID(E.Block->getBlockID()));
          JOS.attribute("iter", E.Iter);
          JOS.attribute("post_visit", E.PostVisit);
          JOS.attribute("converged", E.Converged);
        });
      }
    });
    JOS.attributeObject("cfg", [&] {
      for (const auto &E : BlockIters)
        writeBlock(*E.first, E.second);
    });

    JOS.objectEnd();

    writeCFG();

    *OS << "<script>var HTMLLoggerData = \n";
    *OS << JSON;
    *OS << ";\n</script>\n";
    *OS << llvm::StringRef(HTMLLogger_html).split("<?INJECT?>").second;
  }

  void enterBlock(const CFGBlock &B, bool PostVisit) override {
    llvm::SmallVector<size_t> &BIter = BlockIters[&B];
    unsigned IterNum = BIter.size() + 1;
    BIter.push_back(Iters.size());
    Iters.push_back({&B, IterNum, PostVisit, /*Converged=*/false});
    if (!PostVisit)
      BlockConverged[B.getBlockID()] = false;
    ElementIndex = 0;
  }
  void enterElement(const CFGElement &E) override {
    ++ElementIndex;
  }

  static std::string blockID(unsigned Block) {
    return llvm::formatv("B{0}", Block);
  }
  static std::string eltID(unsigned Block, unsigned Element) {
    return llvm::formatv("B{0}.{1}", Block, Element);
  }
  static std::string iterID(unsigned Block, unsigned Iter) {
    return llvm::formatv("B{0}:{1}", Block, Iter);
  }
  static std::string elementIterID(unsigned Block, unsigned Iter,
                                   unsigned Element) {
    return llvm::formatv("B{0}:{1}_B{0}.{2}", Block, Iter, Element);
  }

  // Write the analysis state associated with a particular analysis point.
  // FIXME: this dump is fairly opaque. We should show:
  //  - values associated with the current Stmt
  //  - values associated with its children
  //  - meaningful names for values
  //  - which boolean values are implied true/false by the flow condition
  void recordState(TypeErasedDataflowAnalysisState &State) override {
    unsigned Block = Iters.back().Block->getBlockID();
    unsigned Iter = Iters.back().Iter;
    bool PostVisit = Iters.back().PostVisit;
    JOS.attributeObject(elementIterID(Block, Iter, ElementIndex), [&] {
      JOS.attribute("block", blockID(Block));
      JOS.attribute("iter", Iter);
      JOS.attribute("post_visit", PostVisit);
      JOS.attribute("element", ElementIndex);

      // If this state immediately follows an Expr, show its built-in model.
      if (ElementIndex > 0) {
        auto S =
            Iters.back().Block->Elements[ElementIndex - 1].getAs<CFGStmt>();
        if (const Expr *E = S ? llvm::dyn_cast<Expr>(S->getStmt()) : nullptr) {
          if (E->isPRValue()) {
            if (!E->getType()->isRecordType())
              if (auto *V = State.Env.getValue(*E))
                JOS.attributeObject(
                    "value", [&] { ModelDumper(JOS, State.Env).dump(*V); });
          } else {
            if (auto *Loc = State.Env.getStorageLocation(*E))
              JOS.attributeObject(
                  "value", [&] { ModelDumper(JOS, State.Env).dump(*Loc); });
          }
        }
      }
      if (!ContextLogs.empty()) {
        JOS.attribute("logs", ContextLogs);
        ContextLogs.clear();
      }
      {
        std::string BuiltinLattice;
        llvm::raw_string_ostream BuiltinLatticeS(BuiltinLattice);
        State.Env.dump(BuiltinLatticeS);
        JOS.attribute("builtinLattice", BuiltinLattice);
      }
    });
  }
  void blockConverged() override {
    Iters.back().Converged = true;
    BlockConverged[Iters.back().Block->getBlockID()] = true;
  }

  void logText(llvm::StringRef S) override {
    ContextLogs.append(S.begin(), S.end());
    ContextLogs.push_back('\n');
  }

private:
  // Write the CFG block details.
  // Currently this is just the list of elements in execution order.
  // FIXME: an AST dump would be a useful view, too.
  void writeBlock(const CFGBlock &B, llvm::ArrayRef<size_t> ItersForB) {
    JOS.attributeObject(blockID(B.getBlockID()), [&] {
      JOS.attributeArray("iters", [&] {
        for (size_t IterIdx : ItersForB) {
          const Iteration &Iter = Iters[IterIdx];
          JOS.object([&] {
            JOS.attribute("iter", Iter.Iter);
            JOS.attribute("post_visit", Iter.PostVisit);
            JOS.attribute("converged", Iter.Converged);
          });
        }
      });
      JOS.attributeArray("elements", [&] {
        for (const auto &Elt : B.Elements) {
          std::string Dump;
          llvm::raw_string_ostream DumpS(Dump);
          Elt.dumpToStream(DumpS);
          JOS.value(Dump);
        }
      });
    });
  }

  // Write the code of function being examined.
  // We want to overlay the code with <span>s that mark which BB particular
  // tokens are associated with, and even which BB element (so that clicking
  // can select the right element).
  void writeCode() {
    const auto &AST = ACFG->getDecl().getASTContext();
    bool Invalid = false;

    // Extract the source code from the original file.
    // Pretty-printing from the AST would probably be nicer (no macros or
    // indentation to worry about), but we need the boundaries of particular
    // AST nodes and the printer doesn't provide this.
    auto Range = clang::Lexer::makeFileCharRange(
        CharSourceRange::getTokenRange(ACFG->getDecl().getSourceRange()),
        AST.getSourceManager(), AST.getLangOpts());
    if (Range.isInvalid())
      return;
    llvm::StringRef Code = clang::Lexer::getSourceText(
        Range, AST.getSourceManager(), AST.getLangOpts(), &Invalid);
    if (Invalid)
      return;

    // TokenInfo stores the BB and set of elements that a token is part of.
    struct TokenInfo {
      enum : unsigned { Missing = static_cast<unsigned>(-1) };

      // The basic block this is part of.
      // This is the BB of the stmt with the smallest containing range.
      unsigned BB = Missing;
      unsigned BBPriority = 0;
      // The most specific stmt this is part of (smallest range).
      unsigned Elt = Missing;
      unsigned EltPriority = 0;
      // All stmts this is part of.
      SmallVector<unsigned> Elts;

      // Mark this token as being part of BB.Elt.
      // RangeLen is the character length of the element's range, used to
      // distinguish inner vs outer statements.
      // For example in `a==0`, token "a" is part of the stmts "a" and "a==0".
      // However "a" has a smaller range, so is more specific. Clicking on the
      // token "a" should select the stmt "a".
      void assign(unsigned BB, unsigned Elt, unsigned RangeLen) {
        // A worse BB (larger range) => ignore.
        if (this->BB != Missing && BB != this->BB && BBPriority <= RangeLen)
          return;
        if (BB != this->BB) {
          this->BB = BB;
          Elts.clear();
          BBPriority = RangeLen;
        }
        BBPriority = std::min(BBPriority, RangeLen);
        Elts.push_back(Elt);
        if (this->Elt == Missing || EltPriority > RangeLen)
          this->Elt = Elt;
      }
      bool operator==(const TokenInfo &Other) const {
        return std::tie(BB, Elt, Elts) ==
               std::tie(Other.BB, Other.Elt, Other.Elts);
      }
      // Write the attributes for the <span> on this token.
      void write(llvm::raw_ostream &OS) const {
        OS << "class='c";
        if (BB != Missing)
          OS << " " << blockID(BB);
        for (unsigned Elt : Elts)
          OS << " " << eltID(BB, Elt);
        OS << "'";

        if (Elt != Missing)
          OS << " data-elt='" << eltID(BB, Elt) << "'";
        if (BB != Missing)
          OS << " data-bb='" << blockID(BB) << "'";
      }
    };

    // Construct one TokenInfo per character in a flat array.
    // This is inefficient (chars in a token all have the same info) but simple.
    std::vector<TokenInfo> State(Code.size());
    for (const auto *Block : ACFG->getCFG()) {
      unsigned EltIndex = 0;
      for (const auto& Elt : *Block) {
        ++EltIndex;
        if (const auto S = Elt.getAs<CFGStmt>()) {
          auto EltRange = clang::Lexer::makeFileCharRange(
              CharSourceRange::getTokenRange(S->getStmt()->getSourceRange()),
              AST.getSourceManager(), AST.getLangOpts());
          if (EltRange.isInvalid())
            continue;
          if (EltRange.getBegin() < Range.getBegin() ||
              EltRange.getEnd() >= Range.getEnd() ||
              EltRange.getEnd() < Range.getBegin() ||
              EltRange.getEnd() >= Range.getEnd())
            continue;
          unsigned Off = EltRange.getBegin().getRawEncoding() -
                         Range.getBegin().getRawEncoding();
          unsigned Len = EltRange.getEnd().getRawEncoding() -
                         EltRange.getBegin().getRawEncoding();
          for (unsigned I = 0; I < Len; ++I)
            State[Off + I].assign(Block->getBlockID(), EltIndex, Len);
        }
      }
    }

    // Finally, write the code with the correct <span>s.
    unsigned Line =
        AST.getSourceManager().getSpellingLineNumber(Range.getBegin());
    *OS << "<template data-copy='code'>\n";
    *OS << "<code class='filename'>";
    llvm::printHTMLEscaped(
        llvm::sys::path::filename(
            AST.getSourceManager().getFilename(Range.getBegin())),
        *OS);
    *OS << "</code>";
    *OS << "<code class='line' data-line='" << Line++ << "'>";
    for (unsigned I = 0; I < Code.size(); ++I) {
      // Don't actually write a <span> around each character, only break spans
      // when the TokenInfo changes.
      bool NeedOpen = I == 0 || !(State[I] == State[I-1]);
      bool NeedClose = I + 1 == Code.size() || !(State[I] == State[I + 1]);
      if (NeedOpen) {
        *OS << "<span ";
        State[I].write(*OS);
        *OS << ">";
      }
      if (Code[I] == '\n')
        *OS << "</code>\n<code class='line' data-line='" << Line++ << "'>";
      else
        llvm::printHTMLEscaped(Code.substr(I, 1), *OS);
      if (NeedClose) *OS << "</span>";
    }
    *OS << "</code>\n";
    *OS << "</template>";
  }

  // Write the CFG diagram, a graph of basic blocks.
  // Laying out graphs is hard, so we construct a graphviz description and shell
  // out to `dot` to turn it into an SVG.
  void writeCFG() {
    *OS << "<template data-copy='cfg'>\n";
    if (auto SVG = renderSVG(buildCFGDot(ACFG->getCFG())))
      *OS << *SVG;
    else
      *OS << "Can't draw CFG: " << toString(SVG.takeError());
    *OS << "</template>\n";
  }

  // Produce a graphviz description of a CFG.
  std::string buildCFGDot(const clang::CFG &CFG) {
    std::string Graph;
    llvm::raw_string_ostream GraphS(Graph);
    // Graphviz likes to add unhelpful tooltips everywhere, " " suppresses.
    GraphS << R"(digraph {
      tooltip=" "
      node[class=bb, shape=square, fontname="sans-serif", tooltip=" "]
      edge[tooltip = " "]
)";
    for (unsigned I = 0; I < CFG.getNumBlockIDs(); ++I) {
      std::string Name = blockID(I);
      // Rightwards arrow, vertical line
      const char *ConvergenceMarker = (const char *)u8"\\n\u2192\u007c";
      if (BlockConverged[I])
        Name += ConvergenceMarker;
      GraphS << "  " << blockID(I) << " [id=" << blockID(I) << " label=\""
             << Name << "\"]\n";
    }
    for (const auto *Block : CFG) {
      for (const auto &Succ : Block->succs()) {
        if (Succ.getReachableBlock())
          GraphS << "  " << blockID(Block->getBlockID()) << " -> "
                 << blockID(Succ.getReachableBlock()->getBlockID()) << "\n";
      }
    }
    GraphS << "}\n";
    return Graph;
  }
};

// Nothing interesting here, just subprocess/temp-file plumbing.
llvm::Expected<std::string> renderSVG(llvm::StringRef DotGraph) {
  std::string DotPath;
  if (const auto *FromEnv = ::getenv("GRAPHVIZ_DOT"))
    DotPath = FromEnv;
  else {
    auto FromPath = llvm::sys::findProgramByName("dot");
    if (!FromPath)
      return llvm::createStringError(FromPath.getError(),
                                     "'dot' not found on PATH");
    DotPath = FromPath.get();
  }

  // Create input and output files for `dot` subprocess.
  // (We create the output file as empty, to reserve the temp filename).
  llvm::SmallString<256> Input, Output;
  int InputFD;
  if (auto EC = llvm::sys::fs::createTemporaryFile("analysis", ".dot", InputFD,
                                                   Input))
    return llvm::createStringError(EC, "failed to create `dot` temp input");
  llvm::raw_fd_ostream(InputFD, /*shouldClose=*/true) << DotGraph;
  auto DeleteInput =
      llvm::make_scope_exit([&] { llvm::sys::fs::remove(Input); });
  if (auto EC = llvm::sys::fs::createTemporaryFile("analysis", ".svg", Output))
    return llvm::createStringError(EC, "failed to create `dot` temp output");
  auto DeleteOutput =
      llvm::make_scope_exit([&] { llvm::sys::fs::remove(Output); });

  std::vector<std::optional<llvm::StringRef>> Redirects = {
      Input, Output,
      /*stderr=*/std::nullopt};
  std::string ErrMsg;
  int Code = llvm::sys::ExecuteAndWait(
      DotPath, {"dot", "-Tsvg"}, /*Env=*/std::nullopt, Redirects,
      /*SecondsToWait=*/0, /*MemoryLimit=*/0, &ErrMsg);
  if (!ErrMsg.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "'dot' failed: " + ErrMsg);
  if (Code != 0)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "'dot' failed (" + llvm::Twine(Code) + ")");

  auto Buf = llvm::MemoryBuffer::getFile(Output);
  if (!Buf)
    return llvm::createStringError(Buf.getError(), "Can't read `dot` output");

  // Output has <?xml> prefix we don't want. Skip to <svg> tag.
  llvm::StringRef Result = Buf.get()->getBuffer();
  auto Pos = Result.find("<svg");
  if (Pos == llvm::StringRef::npos)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Can't find <svg> tag in `dot` output");
  return Result.substr(Pos).str();
}

} // namespace

std::unique_ptr<Logger>
Logger::html(std::function<std::unique_ptr<llvm::raw_ostream>()> Streams) {
  return std::make_unique<HTMLLogger>(std::move(Streams));
}

} // namespace clang::dataflow
