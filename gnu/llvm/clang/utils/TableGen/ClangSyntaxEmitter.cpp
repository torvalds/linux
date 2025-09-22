//===- ClangSyntaxEmitter.cpp - Generate clang Syntax Tree nodes ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These backends consume the definitions of Syntax Tree nodes.
// See clang/include/clang/Tooling/Syntax/{Syntax,Nodes}.td
//
// The -gen-clang-syntax-node-list backend produces a .inc with macro calls
//   NODE(Kind, BaseKind)
//   ABSTRACT_NODE(Type, Base, FirstKind, LastKind)
// similar to those for AST nodes such as AST/DeclNodes.inc.
//
// The -gen-clang-syntax-node-classes backend produces definitions for the
// syntax::Node subclasses (except those marked as External).
//
// In future, another backend will encode the structure of the various node
// types in tables so their invariants can be checked and enforced.
//
//===----------------------------------------------------------------------===//
#include "TableGenBackends.h"

#include <deque>

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

namespace {
using llvm::formatv;

// The class hierarchy of Node types.
// We assemble this in order to be able to define the NodeKind enum in a
// stable and useful way, where abstract Node subclasses correspond to ranges.
class Hierarchy {
public:
  Hierarchy(const llvm::RecordKeeper &Records) {
    for (llvm::Record *T : Records.getAllDerivedDefinitions("NodeType"))
      add(T);
    for (llvm::Record *Derived : Records.getAllDerivedDefinitions("NodeType"))
      if (llvm::Record *Base = Derived->getValueAsOptionalDef("base"))
        link(Derived, Base);
    for (NodeType &N : AllTypes) {
      llvm::sort(N.Derived, [](const NodeType *L, const NodeType *R) {
        return L->Record->getName() < R->Record->getName();
      });
      // Alternatives nodes must have subclasses, External nodes may do.
      assert(N.Record->isSubClassOf("Alternatives") ||
             N.Record->isSubClassOf("External") || N.Derived.empty());
      assert(!N.Record->isSubClassOf("Alternatives") || !N.Derived.empty());
    }
  }

  struct NodeType {
    const llvm::Record *Record = nullptr;
    const NodeType *Base = nullptr;
    std::vector<const NodeType *> Derived;
    llvm::StringRef name() const { return Record->getName(); }
  };

  NodeType &get(llvm::StringRef Name = "Node") {
    auto NI = ByName.find(Name);
    assert(NI != ByName.end() && "no such node");
    return *NI->second;
  }

  // Traverse the hierarchy in pre-order (base classes before derived).
  void visit(llvm::function_ref<void(const NodeType &)> CB,
             const NodeType *Start = nullptr) {
    if (Start == nullptr)
      Start = &get();
    CB(*Start);
    for (const NodeType *D : Start->Derived)
      visit(CB, D);
  }

private:
  void add(const llvm::Record *R) {
    AllTypes.emplace_back();
    AllTypes.back().Record = R;
    bool Inserted = ByName.try_emplace(R->getName(), &AllTypes.back()).second;
    assert(Inserted && "Duplicate node name");
    (void)Inserted;
  }

  void link(const llvm::Record *Derived, const llvm::Record *Base) {
    auto &CN = get(Derived->getName()), &PN = get(Base->getName());
    assert(CN.Base == nullptr && "setting base twice");
    PN.Derived.push_back(&CN);
    CN.Base = &PN;
  }

  std::deque<NodeType> AllTypes;
  llvm::DenseMap<llvm::StringRef, NodeType *> ByName;
};

const Hierarchy::NodeType &firstConcrete(const Hierarchy::NodeType &N) {
  return N.Derived.empty() ? N : firstConcrete(*N.Derived.front());
}
const Hierarchy::NodeType &lastConcrete(const Hierarchy::NodeType &N) {
  return N.Derived.empty() ? N : lastConcrete(*N.Derived.back());
}

struct SyntaxConstraint {
  SyntaxConstraint(const llvm::Record &R) {
    if (R.isSubClassOf("Optional")) {
      *this = SyntaxConstraint(*R.getValueAsDef("inner"));
    } else if (R.isSubClassOf("AnyToken")) {
      NodeType = "Leaf";
    } else if (R.isSubClassOf("NodeType")) {
      NodeType = R.getName().str();
    } else {
      assert(false && "Unhandled Syntax kind");
    }
  }

  std::string NodeType;
  // optional and leaf types also go here, once we want to use them.
};

} // namespace

void clang::EmitClangSyntaxNodeList(llvm::RecordKeeper &Records,
                                    llvm::raw_ostream &OS) {
  llvm::emitSourceFileHeader("Syntax tree node list", OS, Records);
  Hierarchy H(Records);
  OS << R"cpp(
#ifndef NODE
#define NODE(Kind, Base)
#endif

#ifndef CONCRETE_NODE
#define CONCRETE_NODE(Kind, Base) NODE(Kind, Base)
#endif

#ifndef ABSTRACT_NODE
#define ABSTRACT_NODE(Kind, Base, First, Last) NODE(Kind, Base)
#endif

)cpp";
  H.visit([&](const Hierarchy::NodeType &N) {
    // Don't emit ABSTRACT_NODE for node itself, which has no parent.
    if (N.Base == nullptr)
      return;
    if (N.Derived.empty())
      OS << formatv("CONCRETE_NODE({0},{1})\n", N.name(), N.Base->name());
    else
      OS << formatv("ABSTRACT_NODE({0},{1},{2},{3})\n", N.name(),
                    N.Base->name(), firstConcrete(N).name(),
                    lastConcrete(N).name());
  });
  OS << R"cpp(
#undef NODE
#undef CONCRETE_NODE
#undef ABSTRACT_NODE
)cpp";
}

// Format a documentation string as a C++ comment.
// Trims leading whitespace handling since comments come from a TableGen file:
//    documentation = [{
//      This is a widget. Example:
//        widget.explode()
//    }];
// and should be formatted as:
//    /// This is a widget. Example:
//    ///   widget.explode()
// Leading and trailing whitespace lines are stripped.
// The indentation of the first line is stripped from all lines.
static void printDoc(llvm::StringRef Doc, llvm::raw_ostream &OS) {
  Doc = Doc.rtrim();
  llvm::StringRef Line;
  while (Line.trim().empty() && !Doc.empty())
    std::tie(Line, Doc) = Doc.split('\n');
  llvm::StringRef Indent = Line.take_while(llvm::isSpace);
  for (; !Line.empty() || !Doc.empty(); std::tie(Line, Doc) = Doc.split('\n')) {
    Line.consume_front(Indent);
    OS << "/// " << Line << "\n";
  }
}

void clang::EmitClangSyntaxNodeClasses(llvm::RecordKeeper &Records,
                                       llvm::raw_ostream &OS) {
  llvm::emitSourceFileHeader("Syntax tree node list", OS, Records);
  Hierarchy H(Records);

  OS << "\n// Forward-declare node types so we don't have to carefully "
        "sequence definitions.\n";
  H.visit([&](const Hierarchy::NodeType &N) {
    OS << "class " << N.name() << ";\n";
  });

  OS << "\n// Node definitions\n\n";
  H.visit([&](const Hierarchy::NodeType &N) {
    if (N.Record->isSubClassOf("External"))
      return;
    printDoc(N.Record->getValueAsString("documentation"), OS);
    OS << formatv("class {0}{1} : public {2} {{\n", N.name(),
                  N.Derived.empty() ? " final" : "", N.Base->name());

    // Constructor.
    if (N.Derived.empty())
      OS << formatv("public:\n  {0}() : {1}(NodeKind::{0}) {{}\n", N.name(),
                    N.Base->name());
    else
      OS << formatv("protected:\n  {0}(NodeKind K) : {1}(K) {{}\npublic:\n",
                    N.name(), N.Base->name());

    if (N.Record->isSubClassOf("Sequence")) {
      // Getters for sequence elements.
      for (const auto &C : N.Record->getValueAsListOfDefs("children")) {
        assert(C->isSubClassOf("Role"));
        llvm::StringRef Role = C->getValueAsString("role");
        SyntaxConstraint Constraint(*C->getValueAsDef("syntax"));
        for (const char *Const : {"", "const "})
          OS << formatv(
              "  {2}{1} *get{0}() {2} {{\n"
              "    return llvm::cast_or_null<{1}>(findChild(NodeRole::{0}));\n"
              "  }\n",
              Role, Constraint.NodeType, Const);
      }
    }

    // classof. FIXME: move definition inline once ~all nodes are generated.
    OS << "  static bool classof(const Node *N);\n";

    OS << "};\n\n";
  });
}
