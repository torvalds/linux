//===-- PostfixExpression.cpp ---------------------------------------------===//
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

#include "lldb/Symbol/PostfixExpression.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Utility/Stream.h"
#include "llvm/ADT/StringExtras.h"
#include <optional>

using namespace lldb_private;
using namespace lldb_private::postfix;
using namespace lldb_private::dwarf;

static std::optional<BinaryOpNode::OpType>
GetBinaryOpType(llvm::StringRef token) {
  if (token.size() != 1)
    return std::nullopt;
  switch (token[0]) {
  case '@':
    return BinaryOpNode::Align;
  case '-':
    return BinaryOpNode::Minus;
  case '+':
    return BinaryOpNode::Plus;
  }
  return std::nullopt;
}

static std::optional<UnaryOpNode::OpType>
GetUnaryOpType(llvm::StringRef token) {
  if (token == "^")
    return UnaryOpNode::Deref;
  return std::nullopt;
}

Node *postfix::ParseOneExpression(llvm::StringRef expr,
                                  llvm::BumpPtrAllocator &alloc) {
  llvm::SmallVector<Node *, 4> stack;

  llvm::StringRef token;
  while (std::tie(token, expr) = getToken(expr), !token.empty()) {
    if (auto op_type = GetBinaryOpType(token)) {
      // token is binary operator
      if (stack.size() < 2)
        return nullptr;

      Node *right = stack.pop_back_val();
      Node *left = stack.pop_back_val();
      stack.push_back(MakeNode<BinaryOpNode>(alloc, *op_type, *left, *right));
      continue;
    }

    if (auto op_type = GetUnaryOpType(token)) {
      // token is unary operator
      if (stack.empty())
        return nullptr;

      Node *operand = stack.pop_back_val();
      stack.push_back(MakeNode<UnaryOpNode>(alloc, *op_type, *operand));
      continue;
    }

    int64_t value;
    if (to_integer(token, value, 10)) {
      // token is integer literal
      stack.push_back(MakeNode<IntegerNode>(alloc, value));
      continue;
    }

    stack.push_back(MakeNode<SymbolNode>(alloc, token));
  }

  if (stack.size() != 1)
    return nullptr;

  return stack.back();
}

std::vector<std::pair<llvm::StringRef, Node *>>
postfix::ParseFPOProgram(llvm::StringRef prog, llvm::BumpPtrAllocator &alloc) {
  llvm::SmallVector<llvm::StringRef, 4> exprs;
  prog.split(exprs, '=');
  if (exprs.empty() || !exprs.back().trim().empty())
    return {};
  exprs.pop_back();

  std::vector<std::pair<llvm::StringRef, Node *>> result;
  for (llvm::StringRef expr : exprs) {
    llvm::StringRef lhs;
    std::tie(lhs, expr) = getToken(expr);
    Node *rhs = ParseOneExpression(expr, alloc);
    if (!rhs)
      return {};
    result.emplace_back(lhs, rhs);
  }
  return result;
}

namespace {
class SymbolResolver : public Visitor<bool> {
public:
  SymbolResolver(llvm::function_ref<Node *(SymbolNode &symbol)> replacer)
      : m_replacer(replacer) {}

  using Visitor<bool>::Dispatch;

private:
  bool Visit(BinaryOpNode &binary, Node *&) override {
    return Dispatch(binary.Left()) && Dispatch(binary.Right());
  }

  bool Visit(InitialValueNode &, Node *&) override { return true; }
  bool Visit(IntegerNode &, Node *&) override { return true; }
  bool Visit(RegisterNode &, Node *&) override { return true; }

  bool Visit(SymbolNode &symbol, Node *&ref) override {
    if (Node *replacement = m_replacer(symbol)) {
      ref = replacement;
      if (replacement != &symbol)
        return Dispatch(ref);
      return true;
    }
    return false;
  }

  bool Visit(UnaryOpNode &unary, Node *&) override {
    return Dispatch(unary.Operand());
  }

  llvm::function_ref<Node *(SymbolNode &symbol)> m_replacer;
};

class DWARFCodegen : public Visitor<> {
public:
  DWARFCodegen(Stream &stream) : m_out_stream(stream) {}

  using Visitor<>::Dispatch;

private:
  void Visit(BinaryOpNode &binary, Node *&) override;

  void Visit(InitialValueNode &val, Node *&) override;

  void Visit(IntegerNode &integer, Node *&) override {
    m_out_stream.PutHex8(DW_OP_consts);
    m_out_stream.PutSLEB128(integer.GetValue());
    ++m_stack_depth;
  }

  void Visit(RegisterNode &reg, Node *&) override;

  void Visit(SymbolNode &symbol, Node *&) override {
    llvm_unreachable("Symbols should have been resolved by now!");
  }

  void Visit(UnaryOpNode &unary, Node *&) override;

  Stream &m_out_stream;

  /// The number keeping track of the evaluation stack depth at any given
  /// moment. Used for implementing InitialValueNodes. We start with
  /// m_stack_depth = 1, assuming that the initial value is already on the
  /// stack. This initial value will be the value of all InitialValueNodes. If
  /// the expression does not contain InitialValueNodes, then m_stack_depth is
  /// not used, and the generated expression will run correctly even without an
  /// initial value.
  size_t m_stack_depth = 1;
};
} // namespace

void DWARFCodegen::Visit(BinaryOpNode &binary, Node *&) {
  Dispatch(binary.Left());
  Dispatch(binary.Right());

  switch (binary.GetOpType()) {
  case BinaryOpNode::Plus:
    m_out_stream.PutHex8(DW_OP_plus);
    // NOTE: can be optimized by using DW_OP_plus_uconst opcpode
    //       if right child node is constant value
    break;
  case BinaryOpNode::Minus:
    m_out_stream.PutHex8(DW_OP_minus);
    break;
  case BinaryOpNode::Align:
    // emit align operator a @ b as
    // a & ~(b - 1)
    // NOTE: implicitly assuming that b is power of 2
    m_out_stream.PutHex8(DW_OP_lit1);
    m_out_stream.PutHex8(DW_OP_minus);
    m_out_stream.PutHex8(DW_OP_not);

    m_out_stream.PutHex8(DW_OP_and);
    break;
  }
  --m_stack_depth; // Two pops, one push.
}

void DWARFCodegen::Visit(InitialValueNode &, Node *&) {
  // We never go below the initial stack, so we can pick the initial value from
  // the bottom of the stack at any moment.
  assert(m_stack_depth >= 1);
  m_out_stream.PutHex8(DW_OP_pick);
  m_out_stream.PutHex8(m_stack_depth - 1);
  ++m_stack_depth;
}

void DWARFCodegen::Visit(RegisterNode &reg, Node *&) {
  uint32_t reg_num = reg.GetRegNum();
  assert(reg_num != LLDB_INVALID_REGNUM);

  if (reg_num > 31) {
    m_out_stream.PutHex8(DW_OP_bregx);
    m_out_stream.PutULEB128(reg_num);
  } else
    m_out_stream.PutHex8(DW_OP_breg0 + reg_num);

  m_out_stream.PutSLEB128(0);
  ++m_stack_depth;
}

void DWARFCodegen::Visit(UnaryOpNode &unary, Node *&) {
  Dispatch(unary.Operand());

  switch (unary.GetOpType()) {
  case UnaryOpNode::Deref:
    m_out_stream.PutHex8(DW_OP_deref);
    break;
  }
  // Stack depth unchanged.
}

bool postfix::ResolveSymbols(
    Node *&node, llvm::function_ref<Node *(SymbolNode &)> replacer) {
  return SymbolResolver(replacer).Dispatch(node);
}

void postfix::ToDWARF(Node &node, Stream &stream) {
  Node *ptr = &node;
  DWARFCodegen(stream).Dispatch(ptr);
}
