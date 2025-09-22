//=== ASTTableGen.cpp - Helper functions for working with AST records -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some helper functions for working with tblegen reocrds
// for the Clang AST: that is, the contents of files such as DeclNodes.td,
// StmtNodes.td, and TypeNodes.td.
//
//===----------------------------------------------------------------------===//

#include "ASTTableGen.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/Error.h"
#include <optional>

using namespace llvm;
using namespace clang;
using namespace clang::tblgen;

llvm::StringRef clang::tblgen::HasProperties::getName() const {
  if (auto node = getAs<ASTNode>()) {
    return node.getName();
  } else if (auto typeCase = getAs<TypeCase>()) {
    return typeCase.getCaseName();
  } else {
    PrintFatalError(getLoc(), "unexpected node declaring properties");
  }
}

static StringRef removeExpectedNodeNameSuffix(Record *node, StringRef suffix) {
  StringRef nodeName = node->getName();
  if (!nodeName.ends_with(suffix)) {
    PrintFatalError(node->getLoc(),
                    Twine("name of node doesn't end in ") + suffix);
  }
  return nodeName.drop_back(suffix.size());
}

// Decl node names don't end in Decl for historical reasons, and it would
// be somewhat annoying to fix now.  Conveniently, this means the ID matches
// is exactly the node name, and the class name is simply that plus Decl.
std::string clang::tblgen::DeclNode::getClassName() const {
  return (Twine(getName()) + "Decl").str();
}
StringRef clang::tblgen::DeclNode::getId() const {
  return getName();
}

// Type nodes are all named ending in Type, just like the corresponding
// C++ class, and the ID just strips this suffix.
StringRef clang::tblgen::TypeNode::getClassName() const {
  return getName();
}
StringRef clang::tblgen::TypeNode::getId() const {
  return removeExpectedNodeNameSuffix(getRecord(), "Type");
}

// Stmt nodes are named the same as the C++ class, which has no regular
// naming convention (all the non-expression statements end in Stmt,
// and *many* expressions end in Expr, but there are also several
// core expression classes like IntegerLiteral and BinaryOperator with
// no standard suffix).  The ID adds "Class" for historical reasons.
StringRef clang::tblgen::StmtNode::getClassName() const {
  return getName();
}
std::string clang::tblgen::StmtNode::getId() const {
  return (Twine(getName()) + "Class").str();
}

/// Emit a string spelling out the C++ value type.
void PropertyType::emitCXXValueTypeName(bool forRead, raw_ostream &out) const {
  if (!isGenericSpecialization()) {
    if (!forRead && isConstWhenWriting())
      out << "const ";
    out << getCXXTypeName();
  } else if (auto elementType = getArrayElementType()) {
    out << "llvm::ArrayRef<";
    elementType.emitCXXValueTypeName(forRead, out);
    out << ">";
  } else if (auto valueType = getOptionalElementType()) {
    out << "std::optional<";
    valueType.emitCXXValueTypeName(forRead, out);
    out << ">";
  } else {
    //PrintFatalError(getLoc(), "unexpected generic property type");
    abort();
  }
}

// A map from a node to each of its child nodes.
using ChildMap = std::multimap<ASTNode, ASTNode>;

static void visitASTNodeRecursive(ASTNode node, ASTNode base,
                                  const ChildMap &map,
                                  ASTNodeHierarchyVisitor<ASTNode> visit) {
  visit(node, base);

  auto i = map.lower_bound(node), e = map.upper_bound(node);
  for (; i != e; ++i) {
    visitASTNodeRecursive(i->second, node, map, visit);
  }
}

static void visitHierarchy(RecordKeeper &records,
                           StringRef nodeClassName,
                           ASTNodeHierarchyVisitor<ASTNode> visit) {
  // Check for the node class, just as a basic correctness check.
  if (!records.getClass(nodeClassName)) {
    PrintFatalError(Twine("cannot find definition for node class ")
                      + nodeClassName);
  }

  // Find all the nodes in the hierarchy.
  auto nodes = records.getAllDerivedDefinitions(nodeClassName);

  // Derive the child map.
  ChildMap hierarchy;
  ASTNode root;
  for (ASTNode node : nodes) {
    if (auto base = node.getBase())
      hierarchy.insert(std::make_pair(base, node));
    else if (root)
      PrintFatalError(node.getLoc(),
                      "multiple root nodes in " + nodeClassName + " hierarchy");
    else
      root = node;
  }
  if (!root)
    PrintFatalError(Twine("no root node in ") + nodeClassName + " hierarchy");

  // Now visit the map recursively, starting at the root node.
  visitASTNodeRecursive(root, ASTNode(), hierarchy, visit);
}

void clang::tblgen::visitASTNodeHierarchyImpl(RecordKeeper &records,
                                              StringRef nodeClassName,
                                      ASTNodeHierarchyVisitor<ASTNode> visit) {
  visitHierarchy(records, nodeClassName, visit);
}
