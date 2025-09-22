//===-- WebAssemblyUtilities - WebAssembly Utility Functions ---*- C++ -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the WebAssembly-specific
/// utility functions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_UTILS_WEBASSEMBLYUTILITIES_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_UTILS_WEBASSEMBLYUTILITIES_H

#include "llvm/Support/CommandLine.h"

namespace llvm {

class MachineBasicBlock;
class MachineInstr;
class MachineOperand;
class MCContext;
class MCSymbolWasm;
class TargetRegisterClass;
class WebAssemblyFunctionInfo;
class WebAssemblySubtarget;

namespace WebAssembly {

bool isChild(const MachineInstr &MI, const WebAssemblyFunctionInfo &MFI);
bool mayThrow(const MachineInstr &MI);

// Exception-related function names
extern const char *const ClangCallTerminateFn;
extern const char *const CxaBeginCatchFn;
extern const char *const CxaRethrowFn;
extern const char *const StdTerminateFn;
extern const char *const PersonalityWrapperFn;

/// Returns the operand number of a callee, assuming the argument is a call
/// instruction.
const MachineOperand &getCalleeOp(const MachineInstr &MI);

/// Returns the __indirect_function_table, for use in call_indirect and in
/// function bitcasts.
MCSymbolWasm *
getOrCreateFunctionTableSymbol(MCContext &Ctx,
                               const WebAssemblySubtarget *Subtarget);

/// Returns the __funcref_call_table, for use in funcref calls when lowered to
/// table.set + call_indirect.
MCSymbolWasm *
getOrCreateFuncrefCallTableSymbol(MCContext &Ctx,
                                  const WebAssemblySubtarget *Subtarget);

/// Find a catch instruction from an EH pad. Returns null if no catch
/// instruction found or the catch is in an invalid location.
MachineInstr *findCatch(MachineBasicBlock *EHPad);

/// Returns the appropriate copy opcode for the given register class.
unsigned getCopyOpcodeForRegClass(const TargetRegisterClass *RC);

/// Returns true if multivalue returns of a function can be lowered directly,
/// i.e., not indirectly via a pointer parameter that points to the value in
/// memory.
bool canLowerMultivalueReturn(const WebAssemblySubtarget *Subtarget);

/// Returns true if the function's return value(s) can be lowered directly,
/// i.e., not indirectly via a pointer parameter that points to the value in
/// memory.
bool canLowerReturn(size_t ResultSize, const WebAssemblySubtarget *Subtarget);

} // end namespace WebAssembly

} // end namespace llvm

#endif
