//===-- PostfixExpression.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements support for postfix expressions found in several symbol
//  file formats, and their conversion to DWARF.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_POSTFIXEXPRESSION_H
#define LLDB_SYMBOL_POSTFIXEXPRESSION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include <vector>

namespace lldb_private {

class Stream;

namespace postfix {

/// The base class for all nodes in the parsed postfix tree.
class Node {
public:
  enum Kind {
    BinaryOp,
    InitialValue,
    Integer,
    Register,
    Symbol,
    UnaryOp,
  };

protected:
  Node(Kind kind) : m_kind(kind) {}

public:
  Kind GetKind() const { return m_kind; }

private:
  Kind m_kind;
};

/// A node representing a binary expression.
class BinaryOpNode : public Node {
public:
  enum OpType {
    Align, // alignDown(a, b)
    Minus, // a - b
    Plus,  // a + b
  };

  BinaryOpNode(OpType op_type, Node &left, Node &right)
      : Node(BinaryOp), m_op_type(op_type), m_left(&left), m_right(&right) {}

  OpType GetOpType() const { return m_op_type; }

  const Node *Left() const { return m_left; }
  Node *&Left() { return m_left; }

  const Node *Right() const { return m_right; }
  Node *&Right() { return m_right; }

  static bool classof(const Node *node) { return node->GetKind() == BinaryOp; }

private:
  OpType m_op_type;
  Node *m_left;
  Node *m_right;
};

/// A node representing the canonical frame address.
class InitialValueNode: public Node {
public:
  InitialValueNode() : Node(InitialValue) {}

  static bool classof(const Node *node) {
    return node->GetKind() == InitialValue;
  }
};

/// A node representing an integer literal.
class IntegerNode : public Node {
public:
  IntegerNode(int64_t value) : Node(Integer), m_value(value) {}

  int64_t GetValue() const { return m_value; }

  static bool classof(const Node *node) { return node->GetKind() == Integer; }

private:
  int64_t m_value;
};

/// A node representing the value of a register with the given register number.
/// The register kind (RegisterKind enum) used for the specifying the register
/// number is implicit and assumed to be the same for all Register nodes in a
/// given tree.
class RegisterNode : public Node {
public:
  RegisterNode(uint32_t reg_num) : Node(Register), m_reg_num(reg_num) {}

  uint32_t GetRegNum() const { return m_reg_num; }

  static bool classof(const Node *node) { return node->GetKind() == Register; }

private:
  uint32_t m_reg_num;
};

/// A node representing a symbolic reference to a named entity. This may be a
/// register, which hasn't yet been resolved to a RegisterNode.
class SymbolNode : public Node {
public:
  SymbolNode(llvm::StringRef name) : Node(Symbol), m_name(name) {}

  llvm::StringRef GetName() const { return m_name; }

  static bool classof(const Node *node) { return node->GetKind() == Symbol; }

private:
  llvm::StringRef m_name;
};

/// A node representing a unary operation.
class UnaryOpNode : public Node {
public:
  enum OpType {
    Deref, // *a
  };

  UnaryOpNode(OpType op_type, Node &operand)
      : Node(UnaryOp), m_op_type(op_type), m_operand(&operand) {}

  OpType GetOpType() const { return m_op_type; }

  const Node *Operand() const { return m_operand; }
  Node *&Operand() { return m_operand; }

  static bool classof(const Node *node) { return node->GetKind() == UnaryOp; }

private:
  OpType m_op_type;
  Node *m_operand;
};

/// A template class implementing a visitor pattern, but with a couple of
/// twists:
/// - It uses type switch instead of virtual double dispatch. This allows the
//    node classes to be vtable-free and trivially destructible.
/// - The Visit functions get an extra Node *& parameter, which refers to the
///   child pointer of the parent of the node we are currently visiting. This
///   allows mutating algorithms, which replace the currently visited node with
///   a different one.
/// - The class is templatized on the return type of the Visit functions, which
///   means it's possible to return values from them.
template <typename ResultT = void> class Visitor {
protected:
  virtual ~Visitor() = default;

  virtual ResultT Visit(BinaryOpNode &binary, Node *&ref) = 0;
  virtual ResultT Visit(InitialValueNode &val, Node *&ref) = 0;
  virtual ResultT Visit(IntegerNode &integer, Node *&) = 0;
  virtual ResultT Visit(RegisterNode &reg, Node *&) = 0;
  virtual ResultT Visit(SymbolNode &symbol, Node *&ref) = 0;
  virtual ResultT Visit(UnaryOpNode &unary, Node *&ref) = 0;

  /// Invoke the correct Visit function based on the dynamic type of the given
  /// node.
  ResultT Dispatch(Node *&node) {
    switch (node->GetKind()) {
    case Node::BinaryOp:
      return Visit(llvm::cast<BinaryOpNode>(*node), node);
    case Node::InitialValue:
      return Visit(llvm::cast<InitialValueNode>(*node), node);
    case Node::Integer:
      return Visit(llvm::cast<IntegerNode>(*node), node);
    case Node::Register:
      return Visit(llvm::cast<RegisterNode>(*node), node);
    case Node::Symbol:
      return Visit(llvm::cast<SymbolNode>(*node), node);
    case Node::UnaryOp:
      return Visit(llvm::cast<UnaryOpNode>(*node), node);
    }
    llvm_unreachable("Fully covered switch!");
  }
};

/// A utility function for "resolving" SymbolNodes. It traverses a tree and
/// calls the callback function for all SymbolNodes it encountered. The
/// replacement function should return the node it wished to replace the current
/// SymbolNode with (this can also be the original node), or nullptr in case of
/// an error. The nodes returned by the callback are inspected and replaced
/// recursively, *except* for the case when the function returns the exact same
/// node as the input one. It returns true if all SymbolNodes were replaced
/// successfully.
bool ResolveSymbols(Node *&node,
                    llvm::function_ref<Node *(SymbolNode &symbol)> replacer);

template <typename T, typename... Args>
inline T *MakeNode(llvm::BumpPtrAllocator &alloc, Args &&... args) {
  static_assert(std::is_trivially_destructible<T>::value,
                "This object will not be destroyed!");
  return new (alloc.Allocate<T>()) T(std::forward<Args>(args)...);
}

/// Parse the given postfix expression. The parsed nodes are placed into the
/// provided allocator.
Node *ParseOneExpression(llvm::StringRef expr, llvm::BumpPtrAllocator &alloc);

std::vector<std::pair<llvm::StringRef, Node *>>
ParseFPOProgram(llvm::StringRef prog, llvm::BumpPtrAllocator &alloc);

/// Serialize the given expression tree as DWARF. The result is written into the
/// given stream. The AST should not contain any SymbolNodes. If the expression
/// contains InitialValueNodes, the generated expression will assume that their
/// value will be provided as the top value of the initial evaluation stack (as
/// is the case with the CFA value in register eh_unwind rules).
void ToDWARF(Node &node, Stream &stream);

} // namespace postfix
} // namespace lldb_private

#endif // LLDB_SYMBOL_POSTFIXEXPRESSION_H
