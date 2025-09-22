//===-- AMDGPUAsmUtils.h - AsmParser/InstPrinter common ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_UTILS_AMDGPUASMUTILS_H
#define LLVM_LIB_TARGET_AMDGPU_UTILS_AMDGPUASMUTILS_H

#include "SIDefines.h"

#include "llvm/ADT/StringRef.h"

namespace llvm {

class StringLiteral;
class MCSubtargetInfo;

namespace AMDGPU {

const int OPR_ID_UNKNOWN = -1;
const int OPR_ID_UNSUPPORTED = -2;
const int OPR_ID_DUPLICATE = -3;
const int OPR_VAL_INVALID = -4;

struct CustomOperand {
  StringLiteral Name;
  unsigned Encoding = 0;
  bool (*Cond)(const MCSubtargetInfo &STI) = nullptr;
};

struct CustomOperandVal {
  StringLiteral Name;
  unsigned Max;
  unsigned Default;
  unsigned Shift;
  unsigned Width;
  bool (*Cond)(const MCSubtargetInfo &STI) = nullptr;
  unsigned Mask = (1 << Width) - 1;

  unsigned decode(unsigned Code) const { return (Code >> Shift) & Mask; }

  unsigned encode(unsigned Val) const { return (Val & Mask) << Shift; }

  unsigned getMask() const { return Mask << Shift; }

  bool isValid(unsigned Val) const { return Val <= Max; }

  bool isSupported(const MCSubtargetInfo &STI) const {
    return !Cond || Cond(STI);
  }
};

namespace DepCtr {

extern const CustomOperandVal DepCtrInfo[];
extern const int DEP_CTR_SIZE;

} // namespace DepCtr

// Symbolic names for the sendmsg(msg_id, operation, stream) syntax.
namespace SendMsg {

/// Map from a symbolic name for a msg_id to the message portion of the
/// immediate encoding. A negative return value indicates that the Name was
/// unknown or unsupported on this target.
int64_t getMsgId(StringRef Name, const MCSubtargetInfo &STI);

/// Map from an encoding to the symbolic name for a msg_id immediate. This is
/// doing opposite of getMsgId().
StringRef getMsgName(uint64_t Encoding, const MCSubtargetInfo &STI);

/// Map from a symbolic name for a sendmsg operation to the operation portion of
/// the immediate encoding. A negative return value indicates that the Name was
/// unknown or unsupported on this target.
int64_t getMsgOpId(int64_t MsgId, StringRef Name, const MCSubtargetInfo &STI);

/// Map from an encoding to the symbolic name for a sendmsg operation. This is
/// doing opposite of getMsgOpId().
StringRef getMsgOpName(int64_t MsgId, uint64_t Encoding,
                       const MCSubtargetInfo &STI);

} // namespace SendMsg

namespace Hwreg { // Symbolic names for the hwreg(...) syntax.

int64_t getHwregId(StringRef Name, const MCSubtargetInfo &STI);
StringRef getHwreg(uint64_t Encoding, const MCSubtargetInfo &STI);

} // namespace Hwreg

namespace MTBUFFormat {

extern StringLiteral const DfmtSymbolic[];
extern StringLiteral const NfmtSymbolicGFX10[];
extern StringLiteral const NfmtSymbolicSICI[];
extern StringLiteral const NfmtSymbolicVI[];
extern StringLiteral const UfmtSymbolicGFX10[];
extern StringLiteral const UfmtSymbolicGFX11[];
extern unsigned const DfmtNfmt2UFmtGFX10[];
extern unsigned const DfmtNfmt2UFmtGFX11[];

} // namespace MTBUFFormat

namespace Swizzle { // Symbolic names for the swizzle(...) syntax.

extern const char* const IdSymbolic[];

} // namespace Swizzle

namespace VGPRIndexMode { // Symbolic names for the gpr_idx(...) syntax.

extern const char* const IdSymbolic[];

} // namespace VGPRIndexMode

namespace UCVersion {

struct GFXVersion {
  StringLiteral Symbol;
  unsigned Code;
};

ArrayRef<GFXVersion> getGFXVersions();

} // namespace UCVersion

} // namespace AMDGPU
} // namespace llvm

#endif
