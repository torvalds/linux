//===- AMDGPUDelayedMCExpr.cpp - Delayed MCExpr resolve ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUDelayedMCExpr.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCValue.h"

using namespace llvm;

static msgpack::DocNode getNode(msgpack::DocNode DN, msgpack::Type Type,
                                MCValue Val) {
  msgpack::Document *Doc = DN.getDocument();
  switch (Type) {
  default:
    return Doc->getEmptyNode();
  case msgpack::Type::Int:
    return Doc->getNode(static_cast<int64_t>(Val.getConstant()));
  case msgpack::Type::UInt:
    return Doc->getNode(static_cast<uint64_t>(Val.getConstant()));
  case msgpack::Type::Boolean:
    return Doc->getNode(static_cast<bool>(Val.getConstant()));
  }
}

void DelayedMCExprs::assignDocNode(msgpack::DocNode &DN, msgpack::Type Type,
                                   const MCExpr *ExprValue) {
  MCValue Res;
  if (ExprValue->evaluateAsRelocatable(Res, nullptr, nullptr)) {
    if (Res.isAbsolute()) {
      DN = getNode(DN, Type, Res);
      return;
    }
  }

  DelayedExprs.emplace_back(DN, Type, ExprValue);
}

bool DelayedMCExprs::resolveDelayedExpressions() {
  while (!DelayedExprs.empty()) {
    Expr DE = DelayedExprs.front();
    MCValue Res;

    if (!DE.ExprValue->evaluateAsRelocatable(Res, nullptr, nullptr) ||
        !Res.isAbsolute())
      return false;

    DelayedExprs.pop_front();
    DE.DN = getNode(DE.DN, DE.Type, Res);
  }

  return true;
}

void DelayedMCExprs::clear() { DelayedExprs.clear(); }

bool DelayedMCExprs::empty() { return DelayedExprs.empty(); }
