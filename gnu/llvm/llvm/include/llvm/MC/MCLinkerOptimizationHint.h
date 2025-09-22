//===- MCLinkerOptimizationHint.h - LOH interface ---------------*- C++ -*-===//
//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares some helpers classes to handle Linker Optimization Hint
// (LOH).
//
// FIXME: LOH interface supports only MachO format at the moment.
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCLINKEROPTIMIZATIONHINT_H
#define LLVM_MC_MCLINKEROPTIMIZATIONHINT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class MachObjectWriter;
class MCAssembler;
class MCSymbol;
class raw_ostream;

/// Linker Optimization Hint Type.
enum MCLOHType {
  MCLOH_AdrpAdrp = 0x1u,      ///< Adrp xY, _v1@PAGE -> Adrp xY, _v2@PAGE.
  MCLOH_AdrpLdr = 0x2u,       ///< Adrp _v@PAGE -> Ldr _v@PAGEOFF.
  MCLOH_AdrpAddLdr = 0x3u,    ///< Adrp _v@PAGE -> Add _v@PAGEOFF -> Ldr.
  MCLOH_AdrpLdrGotLdr = 0x4u, ///< Adrp _v@GOTPAGE -> Ldr _v@GOTPAGEOFF -> Ldr.
  MCLOH_AdrpAddStr = 0x5u,    ///< Adrp _v@PAGE -> Add _v@PAGEOFF -> Str.
  MCLOH_AdrpLdrGotStr = 0x6u, ///< Adrp _v@GOTPAGE -> Ldr _v@GOTPAGEOFF -> Str.
  MCLOH_AdrpAdd = 0x7u,       ///< Adrp _v@PAGE -> Add _v@PAGEOFF.
  MCLOH_AdrpLdrGot = 0x8u     ///< Adrp _v@GOTPAGE -> Ldr _v@GOTPAGEOFF.
};

static inline StringRef MCLOHDirectiveName() {
  return StringRef(".loh");
}

static inline bool isValidMCLOHType(unsigned Kind) {
  return Kind >= MCLOH_AdrpAdrp && Kind <= MCLOH_AdrpLdrGot;
}

static inline int MCLOHNameToId(StringRef Name) {
#define MCLOHCaseNameToId(Name)     .Case(#Name, MCLOH_ ## Name)
  return StringSwitch<int>(Name)
    MCLOHCaseNameToId(AdrpAdrp)
    MCLOHCaseNameToId(AdrpLdr)
    MCLOHCaseNameToId(AdrpAddLdr)
    MCLOHCaseNameToId(AdrpLdrGotLdr)
    MCLOHCaseNameToId(AdrpAddStr)
    MCLOHCaseNameToId(AdrpLdrGotStr)
    MCLOHCaseNameToId(AdrpAdd)
    MCLOHCaseNameToId(AdrpLdrGot)
    .Default(-1);
#undef MCLOHCaseNameToId
}

static inline StringRef MCLOHIdToName(MCLOHType Kind) {
#define MCLOHCaseIdToName(Name)      case MCLOH_ ## Name: return StringRef(#Name);
  switch (Kind) {
    MCLOHCaseIdToName(AdrpAdrp);
    MCLOHCaseIdToName(AdrpLdr);
    MCLOHCaseIdToName(AdrpAddLdr);
    MCLOHCaseIdToName(AdrpLdrGotLdr);
    MCLOHCaseIdToName(AdrpAddStr);
    MCLOHCaseIdToName(AdrpLdrGotStr);
    MCLOHCaseIdToName(AdrpAdd);
    MCLOHCaseIdToName(AdrpLdrGot);
  }
  return StringRef();
#undef MCLOHCaseIdToName
}

static inline int MCLOHIdToNbArgs(MCLOHType Kind) {
  switch (Kind) {
    // LOH with two arguments
  case MCLOH_AdrpAdrp:
  case MCLOH_AdrpLdr:
  case MCLOH_AdrpAdd:
  case MCLOH_AdrpLdrGot:
    return 2;
    // LOH with three arguments
  case MCLOH_AdrpAddLdr:
  case MCLOH_AdrpLdrGotLdr:
  case MCLOH_AdrpAddStr:
  case MCLOH_AdrpLdrGotStr:
    return 3;
  }
  return -1;
}

/// Store Linker Optimization Hint information (LOH).
class MCLOHDirective {
  MCLOHType Kind;

  /// Arguments of this directive. Order matters.
  SmallVector<MCSymbol *, 3> Args;

  /// Emit this directive in \p OutStream using the information available
  /// in the given \p ObjWriter and \p Layout to get the address of the
  /// arguments within the object file.
  void emit_impl(const MCAssembler &Asm, raw_ostream &OutStream,
                 const MachObjectWriter &ObjWriter) const;

public:
  using LOHArgs = SmallVectorImpl<MCSymbol *>;

  MCLOHDirective(MCLOHType Kind, const LOHArgs &Args)
      : Kind(Kind), Args(Args.begin(), Args.end()) {
    assert(isValidMCLOHType(Kind) && "Invalid LOH directive type!");
  }

  MCLOHType getKind() const { return Kind; }

  const LOHArgs &getArgs() const { return Args; }

  /// Emit this directive as:
  /// <kind, numArgs, addr1, ..., addrN>
  void emit(const MCAssembler &Asm, MachObjectWriter &ObjWriter) const;

  /// Get the size in bytes of this directive if emitted in \p ObjWriter with
  /// the given \p Layout.
  uint64_t getEmitSize(const MCAssembler &Asm,
                       const MachObjectWriter &ObjWriter) const;
};

class MCLOHContainer {
  /// Keep track of the emit size of all the LOHs.
  mutable uint64_t EmitSize = 0;

  /// Keep track of all LOH directives.
  SmallVector<MCLOHDirective, 32> Directives;

public:
  using LOHDirectives = SmallVectorImpl<MCLOHDirective>;

  MCLOHContainer() = default;

  /// Const accessor to the directives.
  const LOHDirectives &getDirectives() const {
    return Directives;
  }

  /// Add the directive of the given kind \p Kind with the given arguments
  /// \p Args to the container.
  void addDirective(MCLOHType Kind, const MCLOHDirective::LOHArgs &Args) {
    Directives.push_back(MCLOHDirective(Kind, Args));
  }

  /// Get the size of the directives if emitted.
  uint64_t getEmitSize(const MCAssembler &Asm,
                       const MachObjectWriter &ObjWriter) const {
    if (!EmitSize) {
      for (const MCLOHDirective &D : Directives)
        EmitSize += D.getEmitSize(Asm, ObjWriter);
    }
    return EmitSize;
  }

  /// Emit all Linker Optimization Hint in one big table.
  /// Each line of the table is emitted by LOHDirective::emit.
  void emit(const MCAssembler &Asm, MachObjectWriter &ObjWriter) const {
    for (const MCLOHDirective &D : Directives)
      D.emit(Asm, ObjWriter);
  }

  void reset() {
    Directives.clear();
    EmitSize = 0;
  }
};

// Add types for specialized template using MCSymbol.
using MCLOHArgs = MCLOHDirective::LOHArgs;
using MCLOHDirectives = MCLOHContainer::LOHDirectives;

} // end namespace llvm

#endif // LLVM_MC_MCLINKEROPTIMIZATIONHINT_H
