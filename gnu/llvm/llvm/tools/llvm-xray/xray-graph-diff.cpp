//===-- xray-graph-diff.cpp: XRay Function Call Graph Renderer ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generate a DOT file to represent the function call graph encountered in
// the trace.
//
//===----------------------------------------------------------------------===//
#include <cassert>
#include <cmath>
#include <limits>
#include <string>

#include "xray-graph-diff.h"
#include "xray-graph.h"
#include "xray-registry.h"

#include "xray-color-helper.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/XRay/Trace.h"

using namespace llvm;
using namespace xray;

static cl::SubCommand GraphDiff("graph-diff",
                                "Generate diff of function-call graphs");
static cl::opt<std::string> GraphDiffInput1(cl::Positional,
                                            cl::desc("<xray log file 1>"),
                                            cl::Required, cl::sub(GraphDiff));
static cl::opt<std::string> GraphDiffInput2(cl::Positional,
                                            cl::desc("<xray log file 2>"),
                                            cl::Required, cl::sub(GraphDiff));

static cl::opt<bool>
    GraphDiffKeepGoing("keep-going",
                       cl::desc("Keep going on errors encountered"),
                       cl::sub(GraphDiff), cl::init(false));
static cl::alias GraphDiffKeepGoingA("k", cl::aliasopt(GraphDiffKeepGoing),
                                     cl::desc("Alias for -keep-going"));
static cl::opt<bool>
    GraphDiffKeepGoing1("keep-going-1",
                        cl::desc("Keep going on errors encountered in trace 1"),
                        cl::sub(GraphDiff), cl::init(false));
static cl::alias GraphDiffKeepGoing1A("k1", cl::aliasopt(GraphDiffKeepGoing1),
                                      cl::desc("Alias for -keep-going-1"));
static cl::opt<bool>
    GraphDiffKeepGoing2("keep-going-2",
                        cl::desc("Keep going on errors encountered in trace 2"),
                        cl::sub(GraphDiff), cl::init(false));
static cl::alias GraphDiffKeepGoing2A("k2", cl::aliasopt(GraphDiffKeepGoing2),
                                      cl::desc("Alias for -keep-going-2"));

static cl::opt<std::string>
    GraphDiffInstrMap("instr-map",
                      cl::desc("binary with the instrumentation map, or "
                               "a separate instrumentation map for graph"),
                      cl::value_desc("binary with xray_instr_map or yaml"),
                      cl::sub(GraphDiff), cl::init(""));
static cl::alias GraphDiffInstrMapA("m", cl::aliasopt(GraphDiffInstrMap),
                                    cl::desc("Alias for -instr-map"));
static cl::opt<std::string>
    GraphDiffInstrMap1("instr-map-1",
                       cl::desc("binary with the instrumentation map, or "
                                "a separate instrumentation map for graph 1"),
                       cl::value_desc("binary with xray_instr_map or yaml"),
                       cl::sub(GraphDiff), cl::init(""));
static cl::alias GraphDiffInstrMap1A("m1", cl::aliasopt(GraphDiffInstrMap1),
                                     cl::desc("Alias for -instr-map-1"));
static cl::opt<std::string>
    GraphDiffInstrMap2("instr-map-2",
                       cl::desc("binary with the instrumentation map, or "
                                "a separate instrumentation map for graph 2"),
                       cl::value_desc("binary with xray_instr_map or yaml"),
                       cl::sub(GraphDiff), cl::init(""));
static cl::alias GraphDiffInstrMap2A("m2", cl::aliasopt(GraphDiffInstrMap2),
                                     cl::desc("Alias for -instr-map-2"));

static cl::opt<bool> GraphDiffDeduceSiblingCalls(
    "deduce-sibling-calls",
    cl::desc("Deduce sibling calls when unrolling function call stacks"),
    cl::sub(GraphDiff), cl::init(false));
static cl::alias
    GraphDiffDeduceSiblingCallsA("d", cl::aliasopt(GraphDiffDeduceSiblingCalls),
                                 cl::desc("Alias for -deduce-sibling-calls"));
static cl::opt<bool> GraphDiffDeduceSiblingCalls1(
    "deduce-sibling-calls-1",
    cl::desc("Deduce sibling calls when unrolling function call stacks"),
    cl::sub(GraphDiff), cl::init(false));
static cl::alias GraphDiffDeduceSiblingCalls1A(
    "d1", cl::aliasopt(GraphDiffDeduceSiblingCalls1),
    cl::desc("Alias for -deduce-sibling-calls-1"));
static cl::opt<bool> GraphDiffDeduceSiblingCalls2(
    "deduce-sibling-calls-2",
    cl::desc("Deduce sibling calls when unrolling function call stacks"),
    cl::sub(GraphDiff), cl::init(false));
static cl::alias GraphDiffDeduceSiblingCalls2A(
    "d2", cl::aliasopt(GraphDiffDeduceSiblingCalls2),
    cl::desc("Alias for -deduce-sibling-calls-2"));

static cl::opt<GraphRenderer::StatType> GraphDiffEdgeLabel(
    "edge-label", cl::desc("Output graphs with edges labeled with this field"),
    cl::value_desc("field"), cl::sub(GraphDiff),
    cl::init(GraphRenderer::StatType::NONE),
    cl::values(clEnumValN(GraphRenderer::StatType::NONE, "none",
                          "Do not label Edges"),
               clEnumValN(GraphRenderer::StatType::COUNT, "count",
                          "function call counts"),
               clEnumValN(GraphRenderer::StatType::MIN, "min",
                          "minimum function durations"),
               clEnumValN(GraphRenderer::StatType::MED, "med",
                          "median function durations"),
               clEnumValN(GraphRenderer::StatType::PCT90, "90p",
                          "90th percentile durations"),
               clEnumValN(GraphRenderer::StatType::PCT99, "99p",
                          "99th percentile durations"),
               clEnumValN(GraphRenderer::StatType::MAX, "max",
                          "maximum function durations"),
               clEnumValN(GraphRenderer::StatType::SUM, "sum",
                          "sum of call durations")));
static cl::alias GraphDiffEdgeLabelA("e", cl::aliasopt(GraphDiffEdgeLabel),
                                     cl::desc("Alias for -edge-label"));

static cl::opt<GraphRenderer::StatType> GraphDiffEdgeColor(
    "edge-color", cl::desc("Output graphs with edges colored by this field"),
    cl::value_desc("field"), cl::sub(GraphDiff),
    cl::init(GraphRenderer::StatType::NONE),
    cl::values(clEnumValN(GraphRenderer::StatType::NONE, "none",
                          "Do not color Edges"),
               clEnumValN(GraphRenderer::StatType::COUNT, "count",
                          "function call counts"),
               clEnumValN(GraphRenderer::StatType::MIN, "min",
                          "minimum function durations"),
               clEnumValN(GraphRenderer::StatType::MED, "med",
                          "median function durations"),
               clEnumValN(GraphRenderer::StatType::PCT90, "90p",
                          "90th percentile durations"),
               clEnumValN(GraphRenderer::StatType::PCT99, "99p",
                          "99th percentile durations"),
               clEnumValN(GraphRenderer::StatType::MAX, "max",
                          "maximum function durations"),
               clEnumValN(GraphRenderer::StatType::SUM, "sum",
                          "sum of call durations")));
static cl::alias GraphDiffEdgeColorA("c", cl::aliasopt(GraphDiffEdgeColor),
                                     cl::desc("Alias for -edge-color"));

static cl::opt<GraphRenderer::StatType> GraphDiffVertexLabel(
    "vertex-label",
    cl::desc("Output graphs with vertices labeled with this field"),
    cl::value_desc("field"), cl::sub(GraphDiff),
    cl::init(GraphRenderer::StatType::NONE),
    cl::values(clEnumValN(GraphRenderer::StatType::NONE, "none",
                          "Do not label Vertices"),
               clEnumValN(GraphRenderer::StatType::COUNT, "count",
                          "function call counts"),
               clEnumValN(GraphRenderer::StatType::MIN, "min",
                          "minimum function durations"),
               clEnumValN(GraphRenderer::StatType::MED, "med",
                          "median function durations"),
               clEnumValN(GraphRenderer::StatType::PCT90, "90p",
                          "90th percentile durations"),
               clEnumValN(GraphRenderer::StatType::PCT99, "99p",
                          "99th percentile durations"),
               clEnumValN(GraphRenderer::StatType::MAX, "max",
                          "maximum function durations"),
               clEnumValN(GraphRenderer::StatType::SUM, "sum",
                          "sum of call durations")));
static cl::alias GraphDiffVertexLabelA("v", cl::aliasopt(GraphDiffVertexLabel),
                                       cl::desc("Alias for -vertex-label"));

static cl::opt<GraphRenderer::StatType> GraphDiffVertexColor(
    "vertex-color",
    cl::desc("Output graphs with vertices colored by this field"),
    cl::value_desc("field"), cl::sub(GraphDiff),
    cl::init(GraphRenderer::StatType::NONE),
    cl::values(clEnumValN(GraphRenderer::StatType::NONE, "none",
                          "Do not color Vertices"),
               clEnumValN(GraphRenderer::StatType::COUNT, "count",
                          "function call counts"),
               clEnumValN(GraphRenderer::StatType::MIN, "min",
                          "minimum function durations"),
               clEnumValN(GraphRenderer::StatType::MED, "med",
                          "median function durations"),
               clEnumValN(GraphRenderer::StatType::PCT90, "90p",
                          "90th percentile durations"),
               clEnumValN(GraphRenderer::StatType::PCT99, "99p",
                          "99th percentile durations"),
               clEnumValN(GraphRenderer::StatType::MAX, "max",
                          "maximum function durations"),
               clEnumValN(GraphRenderer::StatType::SUM, "sum",
                          "sum of call durations")));
static cl::alias GraphDiffVertexColorA("b", cl::aliasopt(GraphDiffVertexColor),
                                       cl::desc("Alias for -vertex-color"));

static cl::opt<int> GraphDiffVertexLabelTrunc(
    "vertex-label-trun", cl::desc("What length to truncate vertex labels to "),
    cl::sub(GraphDiff), cl::init(40));
static cl::alias
    GraphDiffVertexLabelTrunc1("t", cl::aliasopt(GraphDiffVertexLabelTrunc),
                               cl::desc("Alias for -vertex-label-trun"));

static cl::opt<std::string>
    GraphDiffOutput("output", cl::value_desc("Output file"), cl::init("-"),
                    cl::desc("output file; use '-' for stdout"),
                    cl::sub(GraphDiff));
static cl::alias GraphDiffOutputA("o", cl::aliasopt(GraphDiffOutput),
                                  cl::desc("Alias for -output"));

Expected<GraphDiffRenderer> GraphDiffRenderer::Factory::getGraphDiffRenderer() {
  GraphDiffRenderer R;

  for (int i = 0; i < N; ++i) {
    const auto &G = this->G[i].get();
    for (const auto &V : G.vertices()) {
      const auto &VAttr = V.second;
      R.G[VAttr.SymbolName].CorrVertexPtr[i] = &V;
    }
    for (const auto &E : G.edges()) {
      auto &EdgeTailID = E.first.first;
      auto &EdgeHeadID = E.first.second;
      auto EdgeTailAttrOrErr = G.at(EdgeTailID);
      auto EdgeHeadAttrOrErr = G.at(EdgeHeadID);
      if (!EdgeTailAttrOrErr)
        return EdgeTailAttrOrErr.takeError();
      if (!EdgeHeadAttrOrErr)
        return EdgeHeadAttrOrErr.takeError();
      GraphT::EdgeIdentifier ID{EdgeTailAttrOrErr->SymbolName,
                                EdgeHeadAttrOrErr->SymbolName};
      R.G[ID].CorrEdgePtr[i] = &E;
    }
  }

  return R;
}
// Returns the Relative change With respect to LeftStat between LeftStat
// and RightStat.
static double statRelDiff(const GraphDiffRenderer::TimeStat &LeftStat,
                          const GraphDiffRenderer::TimeStat &RightStat,
                          GraphDiffRenderer::StatType T) {
  double LeftAttr = LeftStat.getDouble(T);
  double RightAttr = RightStat.getDouble(T);

  return RightAttr / LeftAttr - 1.0;
}

static std::string getColor(const GraphDiffRenderer::GraphT::EdgeValueType &E,
                            const GraphDiffRenderer::GraphT &G, ColorHelper H,
                            GraphDiffRenderer::StatType T) {
  auto &EdgeAttr = E.second;
  if (EdgeAttr.CorrEdgePtr[0] == nullptr)
    return H.getColorString(2.0); // A number greater than 1.0
  if (EdgeAttr.CorrEdgePtr[1] == nullptr)
    return H.getColorString(-2.0); // A number less than -1.0

  if (T == GraphDiffRenderer::StatType::NONE)
    return H.getDefaultColorString();

  const auto &LeftStat = EdgeAttr.CorrEdgePtr[0]->second.S;
  const auto &RightStat = EdgeAttr.CorrEdgePtr[1]->second.S;

  double RelDiff = statRelDiff(LeftStat, RightStat, T);
  double CappedRelDiff = std::clamp(RelDiff, -1.0, 1.0);

  return H.getColorString(CappedRelDiff);
}

static std::string getColor(const GraphDiffRenderer::GraphT::VertexValueType &V,
                            const GraphDiffRenderer::GraphT &G, ColorHelper H,
                            GraphDiffRenderer::StatType T) {
  auto &VertexAttr = V.second;
  if (VertexAttr.CorrVertexPtr[0] == nullptr)
    return H.getColorString(2.0); // A number greater than 1.0
  if (VertexAttr.CorrVertexPtr[1] == nullptr)
    return H.getColorString(-2.0); // A number less than -1.0

  if (T == GraphDiffRenderer::StatType::NONE)
    return H.getDefaultColorString();

  const auto &LeftStat = VertexAttr.CorrVertexPtr[0]->second.S;
  const auto &RightStat = VertexAttr.CorrVertexPtr[1]->second.S;

  double RelDiff = statRelDiff(LeftStat, RightStat, T);
  double CappedRelDiff = std::clamp(RelDiff, -1.0, 1.0);

  return H.getColorString(CappedRelDiff);
}

static Twine truncateString(const StringRef &S, size_t n) {
  return (S.size() > n) ? Twine(S.substr(0, n)) + "..." : Twine(S);
}

template <typename T> static bool containsNullptr(const T &Collection) {
  return llvm::is_contained(Collection, nullptr);
}

static std::string getLabel(const GraphDiffRenderer::GraphT::EdgeValueType &E,
                            GraphDiffRenderer::StatType EL) {
  auto &EdgeAttr = E.second;
  switch (EL) {
  case GraphDiffRenderer::StatType::NONE:
    return "";
  default:
    if (containsNullptr(EdgeAttr.CorrEdgePtr))
      return "";

    const auto &LeftStat = EdgeAttr.CorrEdgePtr[0]->second.S;
    const auto &RightStat = EdgeAttr.CorrEdgePtr[1]->second.S;

    double RelDiff = statRelDiff(LeftStat, RightStat, EL);
    return std::string(formatv(R"({0:P})", RelDiff));
  }
}

static std::string getLabel(const GraphDiffRenderer::GraphT::VertexValueType &V,
                            GraphDiffRenderer::StatType VL, int TrunLen) {
  const auto &VertexId = V.first;
  const auto &VertexAttr = V.second;
  switch (VL) {
  case GraphDiffRenderer::StatType::NONE:
    return std::string(
        formatv(R"({0})", truncateString(VertexId, TrunLen).str()));
  default:
    if (containsNullptr(VertexAttr.CorrVertexPtr))
      return std::string(
          formatv(R"({0})", truncateString(VertexId, TrunLen).str()));

    const auto &LeftStat = VertexAttr.CorrVertexPtr[0]->second.S;
    const auto &RightStat = VertexAttr.CorrVertexPtr[1]->second.S;

    double RelDiff = statRelDiff(LeftStat, RightStat, VL);
    return std::string(formatv(
        R"({{{0}|{1:P}})", truncateString(VertexId, TrunLen).str(), RelDiff));
  }
}

static double getLineWidth(const GraphDiffRenderer::GraphT::EdgeValueType &E,
                           GraphDiffRenderer::StatType EL) {
  auto &EdgeAttr = E.second;
  switch (EL) {
  case GraphDiffRenderer::StatType::NONE:
    return 1.0;
  default:
    if (containsNullptr(EdgeAttr.CorrEdgePtr))
      return 1.0;

    const auto &LeftStat = EdgeAttr.CorrEdgePtr[0]->second.S;
    const auto &RightStat = EdgeAttr.CorrEdgePtr[1]->second.S;

    double RelDiff = statRelDiff(LeftStat, RightStat, EL);
    return (RelDiff > 1.0) ? RelDiff : 1.0;
  }
}

void GraphDiffRenderer::exportGraphAsDOT(raw_ostream &OS, StatType EdgeLabel,
                                         StatType EdgeColor,
                                         StatType VertexLabel,
                                         StatType VertexColor, int TruncLen) {
  // Get numbering of vertices for dot output.
  StringMap<int32_t> VertexNo;

  int i = 0;
  for (const auto &V : G.vertices()) {
    VertexNo[V.first] = i++;
  }

  ColorHelper H(ColorHelper::DivergingScheme::PiYG);

  OS << "digraph xrayDiff {\n";

  if (VertexLabel != StatType::NONE)
    OS << "node [shape=record]\n";

  for (const auto &E : G.edges()) {
    const auto &HeadId = E.first.first;
    const auto &TailId = E.first.second;
    OS << formatv(R"(F{0} -> F{1} [tooltip="{2} -> {3}" label="{4}" )"
                  R"(color="{5}" labelfontcolor="{5}" penwidth={6}])"
                  "\n",
                  VertexNo[HeadId], VertexNo[TailId],
                  HeadId.empty() ? static_cast<StringRef>("F0") : HeadId,
                  TailId, getLabel(E, EdgeLabel), getColor(E, G, H, EdgeColor),
                  getLineWidth(E, EdgeColor));
  }

  for (const auto &V : G.vertices()) {
    const auto &VertexId = V.first;
    if (VertexId.empty()) {
      OS << formatv(R"(F{0} [label="F0"])"
                    "\n",
                    VertexNo[VertexId]);
      continue;
    }
    OS << formatv(R"(F{0} [label="{1}" color="{2}"])"
                  "\n",
                  VertexNo[VertexId], getLabel(V, VertexLabel, TruncLen),
                  getColor(V, G, H, VertexColor));
  }

  OS << "}\n";
}

template <typename T> static T &ifSpecified(T &A, cl::alias &AA, T &B) {
  if (A.getPosition() == 0 && AA.getPosition() == 0)
    return B;

  return A;
}

static CommandRegistration Unused(&GraphDiff, []() -> Error {
  std::array<GraphRenderer::Factory, 2> Factories{
      {{ifSpecified(GraphDiffKeepGoing1, GraphDiffKeepGoing1A,
                    GraphDiffKeepGoing),
        ifSpecified(GraphDiffDeduceSiblingCalls1, GraphDiffDeduceSiblingCalls1A,
                    GraphDiffDeduceSiblingCalls),
        ifSpecified(GraphDiffInstrMap1, GraphDiffInstrMap1A, GraphDiffInstrMap),
        Trace()},
       {ifSpecified(GraphDiffKeepGoing2, GraphDiffKeepGoing2A,
                    GraphDiffKeepGoing),
        ifSpecified(GraphDiffDeduceSiblingCalls2, GraphDiffDeduceSiblingCalls2A,
                    GraphDiffDeduceSiblingCalls),
        ifSpecified(GraphDiffInstrMap2, GraphDiffInstrMap2A, GraphDiffInstrMap),
        Trace()}}};

  std::array<std::string, 2> Inputs{{GraphDiffInput1, GraphDiffInput2}};

  std::array<GraphRenderer::GraphT, 2> Graphs;

  for (int i = 0; i < 2; i++) {
    auto TraceOrErr = loadTraceFile(Inputs[i], true);
    if (!TraceOrErr)
      return make_error<StringError>(
          Twine("Failed Loading Input File '") + Inputs[i] + "'",
          make_error_code(llvm::errc::invalid_argument));
    Factories[i].Trace = std::move(*TraceOrErr);

    auto GraphRendererOrErr = Factories[i].getGraphRenderer();

    if (!GraphRendererOrErr)
      return GraphRendererOrErr.takeError();

    auto GraphRenderer = *GraphRendererOrErr;

    Graphs[i] = GraphRenderer.getGraph();
  }

  GraphDiffRenderer::Factory DGF(Graphs[0], Graphs[1]);

  auto GDROrErr = DGF.getGraphDiffRenderer();
  if (!GDROrErr)
    return GDROrErr.takeError();

  auto &GDR = *GDROrErr;

  std::error_code EC;
  raw_fd_ostream OS(GraphDiffOutput, EC, sys::fs::OpenFlags::OF_TextWithCRLF);
  if (EC)
    return make_error<StringError>(
        Twine("Cannot open file '") + GraphDiffOutput + "' for writing.", EC);

  GDR.exportGraphAsDOT(OS, GraphDiffEdgeLabel, GraphDiffEdgeColor,
                       GraphDiffVertexLabel, GraphDiffVertexColor,
                       GraphDiffVertexLabelTrunc);

  return Error::success();
});
