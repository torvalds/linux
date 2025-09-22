//==-- loop_proto_to_llvm.cpp - Protobuf-C++ conversion
//---------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements functions for converting between protobufs and LLVM IR.
//
//
//===----------------------------------------------------------------------===//

#include "loop_proto_to_llvm.h"
#include "cxx_loop_proto.pb.h"
#include "../handle-llvm/input_arrays.h"

// The following is needed to convert protos in human-readable form
#include <google/protobuf/text_format.h>

#include <ostream>
#include <sstream>

namespace clang_fuzzer {

// Forward decls
std::string BinopToString(std::ostream &os, const BinaryOp &x);
std::string StateSeqToString(std::ostream &os, const StatementSeq &x);

// Counter variable to generate new LLVM IR variable names and wrapper function
static std::string get_var() {
  static int ctr = 0;
  return "%var" + std::to_string(ctr++);
}

static bool inner_loop = false;
class InnerLoop {
  public:
  InnerLoop() {
    inner_loop = true;
  }
  ~InnerLoop() {
    inner_loop = false;
  }
};


// Proto to LLVM.

std::string ConstToString(const Const &x) {
  return std::to_string(x.val());
}
std::string VarRefToString(std::ostream &os, const VarRef &x) {
  std::string which_loop = inner_loop ? "inner" : "outer";
  std::string arr;
  switch(x.arr()) {
  case VarRef::ARR_A:
    arr = "%a";
    break;
  case VarRef::ARR_B:
    arr = "%b";
    break;
  case VarRef::ARR_C:
    arr = "%c";
    break;
  }
  std::string ptr_var = get_var();
  os << ptr_var << " = getelementptr inbounds i32, i32* " << arr
     << ", i64 %" << which_loop << "_ct\n";
  return ptr_var;
}
std::string RvalueToString(std::ostream &os, const Rvalue &x) {
  if(x.has_cons())
    return ConstToString(x.cons());
  if(x.has_binop())
    return BinopToString(os, x.binop());
  if(x.has_varref()) {
    std::string var_ref = VarRefToString(os, x.varref());
    std::string val_var = get_var();
    os << val_var << " = load i32, i32* " << var_ref << "\n";
    return val_var;
  }
  return "1";

}
std::string BinopToString(std::ostream &os, const BinaryOp &x) {
  std::string left = RvalueToString(os, x.left());
  std::string right = RvalueToString(os, x.right());
  std::string op;
  switch (x.op()) {
  case BinaryOp::PLUS:
    op = "add";
    break;
  case BinaryOp::MINUS:
    op = "sub";
    break;
  case BinaryOp::MUL:
    op = "mul";
    break;
  case BinaryOp::XOR:
    op = "xor";
    break;
  case BinaryOp::AND:
    op = "and";
    break;
  case BinaryOp::OR:
    op = "or";
    break;
  // Support for Boolean operators will be added later
  case BinaryOp::EQ:
  case BinaryOp::NE:
  case BinaryOp::LE:
  case BinaryOp::GE:
  case BinaryOp::LT:
  case BinaryOp::GT:
    op = "add";
    break;
  }
  std::string val_var = get_var();
  os << val_var << " = " << op << " i32 " << left << ", " << right << "\n";
  return val_var;
}
std::ostream &operator<<(std::ostream &os, const AssignmentStatement &x) {
  std::string rvalue = RvalueToString(os, x.rvalue());
  std::string var_ref = VarRefToString(os, x.varref());
  return os << "store i32 " << rvalue << ", i32* " << var_ref << "\n";
}
std::ostream &operator<<(std::ostream &os, const Statement &x) {
  return os << x.assignment();
}
std::ostream &operator<<(std::ostream &os, const StatementSeq &x) {
  for (auto &st : x.statements()) {
    os << st;
  }
  return os;
}
void NestedLoopToString(std::ostream &os, const LoopFunction &x) {
  os << "target triple = \"x86_64-unknown-linux-gnu\"\n"
     << "define void @foo(i32* %a, i32* %b, i32* noalias %c, i64 %s) {\n"
     << "outer_loop_start:\n"
     << "%cmp = icmp sgt i64 %s, 0\n"
     << "br i1 %cmp, label %inner_loop_start, label %end\n"
     << "outer_loop:\n"
     << x.outer_statements()
     << "%o_ct_new = add i64 %outer_ct, 1\n"
     << "%jmp_outer = icmp eq i64 %o_ct_new, %s\n"
     << "br i1 %jmp_outer, label %end, label %inner_loop_start\n"
     << "inner_loop_start:\n"
     << "%outer_ct = phi i64 [%o_ct_new, %outer_loop], [0, %outer_loop_start]\n"
     << "br label %inner_loop\n"
     << "inner_loop:\n"
     << "%inner_ct = phi i64 [0, %inner_loop_start], [%i_ct_new, %inner_loop]\n";
  {
    InnerLoop IL;
    os << x.inner_statements();
  }
  os << "%i_ct_new = add i64 %inner_ct, 1\n"
     << "%jmp_inner = icmp eq i64 %i_ct_new, %s\n"
     << "br i1 %jmp_inner, label %outer_loop, label %inner_loop, !llvm.loop !0\n"
     << "end:\n"
     << "ret void\n"
     << "}\n"
     << "!0 = distinct !{!0, !1, !2}\n"
     << "!1 = !{!\"llvm.loop.vectorize.enable\", i1 true}\n"
     << "!2 = !{!\"llvm.loop.vectorize.width\", i32 " << kArraySize << "}\n";
}
void SingleLoopToString(std::ostream &os, const LoopFunction &x) {
  os << "target triple = \"x86_64-unknown-linux-gnu\"\n"
     << "define void @foo(i32* %a, i32* %b, i32* noalias %c, i64 %s) {\n"
     << "%cmp = icmp sgt i64 %s, 0\n"
     << "br i1 %cmp, label %start, label %end\n"
     << "start:\n"
     << "br label %loop\n"
     << "end:\n"
     << "ret void\n"
     << "loop:\n"
     << "%outer_ct = phi i64 [ %ctnew, %loop ], [ 0, %start ]\n"
     << x.outer_statements()
     << "%ctnew = add i64 %outer_ct, 1\n"
     << "%j = icmp eq i64 %ctnew, %s\n"
     << "br i1 %j, label %end, label %loop, !llvm.loop !0\n}\n"
     << "!0 = distinct !{!0, !1, !2}\n"
     << "!1 = !{!\"llvm.loop.vectorize.enable\", i1 true}\n"
     << "!2 = !{!\"llvm.loop.vectorize.width\", i32 " << kArraySize << "}\n";
}
std::ostream &operator<<(std::ostream &os, const LoopFunction &x) {
  if (x.has_inner_statements())
    NestedLoopToString(os, x);
  else
    SingleLoopToString(os, x);
  return os;
}

// ---------------------------------

std::string LoopFunctionToLLVMString(const LoopFunction &input) {
  std::ostringstream os;
  os << input;
  return os.str();
}
std::string LoopProtoToLLVM(const uint8_t *data, size_t size) {
  LoopFunction message;
  if (!message.ParsePartialFromArray(data, size))
    return "#error invalid proto\n";
  return LoopFunctionToLLVMString(message);
}

} // namespace clang_fuzzer
