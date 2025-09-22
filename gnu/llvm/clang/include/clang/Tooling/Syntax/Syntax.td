//===- Syntax.td - TableGen metamodel for syntax::Node hierarchy ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The tree representation of the is C++ syntax is quite regular.
//
// There are 4 archetypes of nodes in the syntax tree:
//  - Leaves, owning exactly one token. (syntax::Leaf)
//  - Sequences, with a fixed list of children that should appear in order.
//    The concrete node defines a Role sequence which identifies the children.
//    The type of child in each role is also constrained.
//  - Lists, with children in alternating Element/Delimiter roles. (syntax::List)
//    The concrete node defines the element type, delimiters are always leaves.
//  - Alternatives, where several different node types are allowed.
//    These are modeled as abstract types with inheritance (e.g. Declaration).
//
// This file defines TableGen classes modelling these archetypes.
// The concrete nodes are defined in terms of these classes in Nodes.td.
//
// The C++ classes for the archetypes themselves are written by hand, and the
// concrete node classes will be generated. Migration to TableGen is not
// complete, so currently there is a mix of generated and hand-authored code.
//
//===----------------------------------------------------------------------===//

// Syntax is any constraint on constructs that can appear somewhere.
class Syntax;
class Optional<Syntax inner_> : Syntax { Syntax inner = inner_; }
class AnyToken<list<string> kinds_> : Syntax { list<string> kinds = kinds_; }
class Token<string kind_> : AnyToken<[kind_]>;
class Keyword<string kw> : Token<!strconcat("kw_", kw)>;

// Defs derived from NodeType correspond to syntax tree node types.
// NodeType is also a syntax constraint: one node of this type.
class NodeType : Syntax {
  // The NodeType that this node is derived from in the Node class hierarchy.
  NodeType base = ?;
  // Documentation for this Node subclass.
  string documentation;
}

// A node type which is defined in Nodes.h rather than by generated code.
// We merely specify the inheritance hierarchy here.
class External<NodeType base_> : NodeType { let base = base_; }

// Special nodes defined here.
def Node : External<?> {}
def Leaf : External<Node> {}
def Tree : External<Node> {}

// An abstract node type which merely serves as a base for more specific types.
//
// This corresponds to an alternative rule in the grammar, such as:
//   Statement = IfStatement | ForStatement | ...
// Statement is modeled using Alternatives, and IfStatement.base is Statement.
class Alternatives<NodeType base_ = Tree> : NodeType { let base = base_; }

// A node type which may contain anything and has no specific accessors.
// These are generally placeholders for a more precise implementation.
class Unconstrained<NodeType base_ = Tree> : NodeType { let base = base_; }

class Role<string role_, Syntax syntax_> {
  string role = role_;
  Syntax syntax = syntax_;
}

// A node which contains a fixed sequence of children in a particular order.
//
// Each child is characterized by a role (unique within the sequence), and
// has an allowed base type for the node.
// The role sequence and role/type match are enforced invariants of the class.
//
// We also record whether the child is required to be present, and which tokens
// are permitted (for Leaf nodes). These invariants are not enforced.
class Sequence<NodeType base_ = Tree> : NodeType {
  let base = base_;
  // Children must be Role or have a default role derived from the NodeType.
  list<Role> children;
}

// FIXME: add list archetype.
