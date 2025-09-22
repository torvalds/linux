//===- llvm/Support/GraphWriter.h - Write graph to a .dot file --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a simple interface that can be used to print out generic
// LLVM graphs to ".dot" files.  "dot" is a tool that is part of the AT&T
// graphviz package (http://www.research.att.com/sw/tools/graphviz/) which can
// be used to turn the files output by this interface into a variety of
// different graphics formats.
//
// Graphs do not need to implement any interface past what is already required
// by the GraphTraits template, but they can choose to implement specializations
// of the DOTGraphTraits template if they want to customize the graphs output in
// any way.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GRAPHWRITER_H
#define LLVM_SUPPORT_GRAPHWRITER_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

namespace llvm {

namespace DOT {  // Private functions...

std::string EscapeString(const std::string &Label);

/// Get a color string for this node number. Simply round-robin selects
/// from a reasonable number of colors.
StringRef getColorString(unsigned NodeNumber);

} // end namespace DOT

namespace GraphProgram {

enum Name {
  DOT,
  FDP,
  NEATO,
  TWOPI,
  CIRCO
};

} // end namespace GraphProgram

bool DisplayGraph(StringRef Filename, bool wait = true,
                  GraphProgram::Name program = GraphProgram::DOT);

template<typename GraphType>
class GraphWriter {
  raw_ostream &O;
  const GraphType &G;
  bool RenderUsingHTML = false;

  using DOTTraits = DOTGraphTraits<GraphType>;
  using GTraits = GraphTraits<GraphType>;
  using NodeRef = typename GTraits::NodeRef;
  using node_iterator = typename GTraits::nodes_iterator;
  using child_iterator = typename GTraits::ChildIteratorType;
  DOTTraits DTraits;

  static_assert(std::is_pointer_v<NodeRef>,
                "FIXME: Currently GraphWriter requires the NodeRef type to be "
                "a pointer.\nThe pointer usage should be moved to "
                "DOTGraphTraits, and removed from GraphWriter itself.");

  // Writes the edge labels of the node to O and returns true if there are any
  // edge labels not equal to the empty string "".
  bool getEdgeSourceLabels(raw_ostream &O, NodeRef Node) {
    child_iterator EI = GTraits::child_begin(Node);
    child_iterator EE = GTraits::child_end(Node);
    bool hasEdgeSourceLabels = false;

    if (RenderUsingHTML)
      O << "</tr><tr>";

    for (unsigned i = 0; EI != EE && i != 64; ++EI, ++i) {
      std::string label = DTraits.getEdgeSourceLabel(Node, EI);

      if (label.empty())
        continue;

      hasEdgeSourceLabels = true;

      if (RenderUsingHTML)
        O << "<td colspan=\"1\" port=\"s" << i << "\">" << label << "</td>";
      else {
        if (i)
          O << "|";

        O << "<s" << i << ">" << DOT::EscapeString(label);
      }
    }

    if (EI != EE && hasEdgeSourceLabels) {
      if (RenderUsingHTML)
        O << "<td colspan=\"1\" port=\"s64\">truncated...</td>";
      else
        O << "|<s64>truncated...";
    }

    return hasEdgeSourceLabels;
  }

public:
  GraphWriter(raw_ostream &o, const GraphType &g, bool SN) : O(o), G(g) {
    DTraits = DOTTraits(SN);
    RenderUsingHTML = DTraits.renderNodesUsingHTML();
  }

  void writeGraph(const std::string &Title = "") {
    // Output the header for the graph...
    writeHeader(Title);

    // Emit all of the nodes in the graph...
    writeNodes();

    // Output any customizations on the graph
    DOTGraphTraits<GraphType>::addCustomGraphFeatures(G, *this);

    // Output the end of the graph
    writeFooter();
  }

  void writeHeader(const std::string &Title) {
    std::string GraphName(DTraits.getGraphName(G));

    if (!Title.empty())
      O << "digraph \"" << DOT::EscapeString(Title) << "\" {\n";
    else if (!GraphName.empty())
      O << "digraph \"" << DOT::EscapeString(GraphName) << "\" {\n";
    else
      O << "digraph unnamed {\n";

    if (DTraits.renderGraphFromBottomUp())
      O << "\trankdir=\"BT\";\n";

    if (!Title.empty())
      O << "\tlabel=\"" << DOT::EscapeString(Title) << "\";\n";
    else if (!GraphName.empty())
      O << "\tlabel=\"" << DOT::EscapeString(GraphName) << "\";\n";
    O << DTraits.getGraphProperties(G);
    O << "\n";
  }

  void writeFooter() {
    // Finish off the graph
    O << "}\n";
  }

  void writeNodes() {
    // Loop over the graph, printing it out...
    for (const auto Node : nodes<GraphType>(G))
      if (!isNodeHidden(Node))
        writeNode(Node);
  }

  bool isNodeHidden(NodeRef Node) { return DTraits.isNodeHidden(Node, G); }

  void writeNode(NodeRef Node) {
    std::string NodeAttributes = DTraits.getNodeAttributes(Node, G);

    O << "\tNode" << static_cast<const void *>(Node) << " [shape=";
    if (RenderUsingHTML)
      O << "none,";
    else
      O << "record,";

    if (!NodeAttributes.empty()) O << NodeAttributes << ",";
    O << "label=";

    if (RenderUsingHTML) {
      // Count the numbewr of edges out of the node to determine how
      // many columns to span (max 64)
      unsigned ColSpan = 0;
      child_iterator EI = GTraits::child_begin(Node);
      child_iterator EE = GTraits::child_end(Node);
      for (; EI != EE && ColSpan != 64; ++EI, ++ColSpan)
        ;
      if (ColSpan == 0)
        ColSpan = 1;
      // Include truncated messages when counting.
      if (EI != EE)
        ++ColSpan;
      O << "<<table border=\"0\" cellborder=\"1\" cellspacing=\"0\""
        << " cellpadding=\"0\"><tr><td align=\"text\" colspan=\"" << ColSpan
        << "\">";
    } else
      O << "\"{";

    if (!DTraits.renderGraphFromBottomUp()) {
      if (RenderUsingHTML)
        O << DTraits.getNodeLabel(Node, G) << "</td>";
      else
        O << DOT::EscapeString(DTraits.getNodeLabel(Node, G));

      // If we should include the address of the node in the label, do so now.
      std::string Id = DTraits.getNodeIdentifierLabel(Node, G);
      if (!Id.empty())
        O << "|" << DOT::EscapeString(Id);

      std::string NodeDesc = DTraits.getNodeDescription(Node, G);
      if (!NodeDesc.empty())
        O << "|" << DOT::EscapeString(NodeDesc);
    }

    std::string edgeSourceLabels;
    raw_string_ostream EdgeSourceLabels(edgeSourceLabels);
    bool hasEdgeSourceLabels = getEdgeSourceLabels(EdgeSourceLabels, Node);

    if (hasEdgeSourceLabels) {
      if (!DTraits.renderGraphFromBottomUp())
        if (!RenderUsingHTML)
          O << "|";

      if (RenderUsingHTML)
        O << edgeSourceLabels;
      else
        O << "{" << edgeSourceLabels << "}";

      if (DTraits.renderGraphFromBottomUp())
        if (!RenderUsingHTML)
          O << "|";
    }

    if (DTraits.renderGraphFromBottomUp()) {
      if (RenderUsingHTML)
        O << DTraits.getNodeLabel(Node, G);
      else
        O << DOT::EscapeString(DTraits.getNodeLabel(Node, G));

      // If we should include the address of the node in the label, do so now.
      std::string Id = DTraits.getNodeIdentifierLabel(Node, G);
      if (!Id.empty())
        O << "|" << DOT::EscapeString(Id);

      std::string NodeDesc = DTraits.getNodeDescription(Node, G);
      if (!NodeDesc.empty())
        O << "|" << DOT::EscapeString(NodeDesc);
    }

    if (DTraits.hasEdgeDestLabels()) {
      O << "|{";

      unsigned i = 0, e = DTraits.numEdgeDestLabels(Node);
      for (; i != e && i != 64; ++i) {
        if (i) O << "|";
        O << "<d" << i << ">"
          << DOT::EscapeString(DTraits.getEdgeDestLabel(Node, i));
      }

      if (i != e)
        O << "|<d64>truncated...";
      O << "}";
    }

    if (RenderUsingHTML)
      O << "</tr></table>>";
    else
      O << "}\"";
    O << "];\n"; // Finish printing the "node" line

    // Output all of the edges now
    child_iterator EI = GTraits::child_begin(Node);
    child_iterator EE = GTraits::child_end(Node);
    for (unsigned i = 0; EI != EE && i != 64; ++EI, ++i)
      if (!DTraits.isNodeHidden(*EI, G))
        writeEdge(Node, i, EI);
    for (; EI != EE; ++EI)
      if (!DTraits.isNodeHidden(*EI, G))
        writeEdge(Node, 64, EI);
  }

  void writeEdge(NodeRef Node, unsigned edgeidx, child_iterator EI) {
    if (NodeRef TargetNode = *EI) {
      int DestPort = -1;
      if (DTraits.edgeTargetsEdgeSource(Node, EI)) {
        child_iterator TargetIt = DTraits.getEdgeTarget(Node, EI);

        // Figure out which edge this targets...
        unsigned Offset =
          (unsigned)std::distance(GTraits::child_begin(TargetNode), TargetIt);
        DestPort = static_cast<int>(Offset);
      }

      if (DTraits.getEdgeSourceLabel(Node, EI).empty())
        edgeidx = -1;

      emitEdge(static_cast<const void*>(Node), edgeidx,
               static_cast<const void*>(TargetNode), DestPort,
               DTraits.getEdgeAttributes(Node, EI, G));
    }
  }

  /// emitSimpleNode - Outputs a simple (non-record) node
  void emitSimpleNode(const void *ID, const std::string &Attr,
                   const std::string &Label, unsigned NumEdgeSources = 0,
                   const std::vector<std::string> *EdgeSourceLabels = nullptr) {
    O << "\tNode" << ID << "[ ";
    if (!Attr.empty())
      O << Attr << ",";
    O << " label =\"";
    if (NumEdgeSources) O << "{";
    O << DOT::EscapeString(Label);
    if (NumEdgeSources) {
      O << "|{";

      for (unsigned i = 0; i != NumEdgeSources; ++i) {
        if (i) O << "|";
        O << "<s" << i << ">";
        if (EdgeSourceLabels) O << DOT::EscapeString((*EdgeSourceLabels)[i]);
      }
      O << "}}";
    }
    O << "\"];\n";
  }

  /// emitEdge - Output an edge from a simple node into the graph...
  void emitEdge(const void *SrcNodeID, int SrcNodePort,
                const void *DestNodeID, int DestNodePort,
                const std::string &Attrs) {
    if (SrcNodePort  > 64) return;             // Eminating from truncated part?
    if (DestNodePort > 64) DestNodePort = 64;  // Targeting the truncated part?

    O << "\tNode" << SrcNodeID;
    if (SrcNodePort >= 0)
      O << ":s" << SrcNodePort;
    O << " -> Node" << DestNodeID;
    if (DestNodePort >= 0 && DTraits.hasEdgeDestLabels())
      O << ":d" << DestNodePort;

    if (!Attrs.empty())
      O << "[" << Attrs << "]";
    O << ";\n";
  }

  /// getOStream - Get the raw output stream into the graph file. Useful to
  /// write fancy things using addCustomGraphFeatures().
  raw_ostream &getOStream() {
    return O;
  }
};

template<typename GraphType>
raw_ostream &WriteGraph(raw_ostream &O, const GraphType &G,
                        bool ShortNames = false,
                        const Twine &Title = "") {
  // Start the graph emission process...
  GraphWriter<GraphType> W(O, G, ShortNames);

  // Emit the graph.
  W.writeGraph(Title.str());

  return O;
}

std::string createGraphFilename(const Twine &Name, int &FD);

/// Writes graph into a provided @c Filename.
/// If @c Filename is empty, generates a random one.
/// \return The resulting filename, or an empty string if writing
/// failed.
template <typename GraphType>
std::string WriteGraph(const GraphType &G, const Twine &Name,
                       bool ShortNames = false,
                       const Twine &Title = "",
                       std::string Filename = "") {
  int FD;
  if (Filename.empty()) {
    Filename = createGraphFilename(Name.str(), FD);
  } else {
    std::error_code EC = sys::fs::openFileForWrite(
        Filename, FD, sys::fs::CD_CreateAlways, sys::fs::OF_Text);

    // Writing over an existing file is not considered an error.
    if (EC == std::errc::file_exists) {
      errs() << "file exists, overwriting" << "\n";
    } else if (EC) {
      errs() << "error writing into file" << "\n";
      return "";
    } else {
      errs() << "writing to the newly created file " << Filename << "\n";
    }
  }
  raw_fd_ostream O(FD, /*shouldClose=*/ true);

  if (FD == -1) {
    errs() << "error opening file '" << Filename << "' for writing!\n";
    return "";
  }

  llvm::WriteGraph(O, G, ShortNames, Title);
  errs() << " done. \n";

  return Filename;
}

/// DumpDotGraph - Just dump a dot graph to the user-provided file name.
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
template <typename GraphType>
LLVM_DUMP_METHOD void
dumpDotGraphToFile(const GraphType &G, const Twine &FileName,
                   const Twine &Title, bool ShortNames = false,
                   const Twine &Name = "") {
  llvm::WriteGraph(G, Name, ShortNames, Title, FileName.str());
}
#endif

/// ViewGraph - Emit a dot graph, run 'dot', run gv on the postscript file,
/// then cleanup.  For use from the debugger.
///
template<typename GraphType>
void ViewGraph(const GraphType &G, const Twine &Name,
               bool ShortNames = false, const Twine &Title = "",
               GraphProgram::Name Program = GraphProgram::DOT) {
  std::string Filename = llvm::WriteGraph(G, Name, ShortNames, Title);

  if (Filename.empty())
    return;

  DisplayGraph(Filename, false, Program);
}

} // end namespace llvm

#endif // LLVM_SUPPORT_GRAPHWRITER_H
