//===- llvm/MC/MCRelocationInfo.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCRelocationInfo class, which provides methods to
// create MCExprs from relocations, either found in an object::ObjectFile
// (object::RelocationRef), or provided through the C API.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDISASSEMBLER_MCRELOCATIONINFO_H
#define LLVM_MC_MCDISASSEMBLER_MCRELOCATIONINFO_H

namespace llvm {

class MCContext;
class MCExpr;

/// Create MCExprs from relocations found in an object file.
class MCRelocationInfo {
protected:
  MCContext &Ctx;

public:
  MCRelocationInfo(MCContext &Ctx);
  MCRelocationInfo(const MCRelocationInfo &) = delete;
  MCRelocationInfo &operator=(const MCRelocationInfo &) = delete;
  virtual ~MCRelocationInfo();

  /// Create an MCExpr for the target-specific \p VariantKind.
  /// The VariantKinds are defined in llvm-c/Disassembler.h.
  /// Used by MCExternalSymbolizer.
  /// \returns If possible, an MCExpr corresponding to VariantKind, else 0.
  virtual const MCExpr *createExprForCAPIVariantKind(const MCExpr *SubExpr,
                                                     unsigned VariantKind);
};

} // end namespace llvm

#endif // LLVM_MC_MCDISASSEMBLER_MCRELOCATIONINFO_H
