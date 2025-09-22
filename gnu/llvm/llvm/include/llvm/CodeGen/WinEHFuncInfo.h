//===- llvm/CodeGen/WinEHFuncInfo.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data structures and associated state for Windows exception handling schemes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_WINEHFUNCINFO_H
#define LLVM_CODEGEN_WINEHFUNCINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <limits>
#include <utility>

namespace llvm {

class AllocaInst;
class BasicBlock;
class FuncletPadInst;
class Function;
class GlobalVariable;
class Instruction;
class InvokeInst;
class MachineBasicBlock;
class MCSymbol;

// The following structs respresent the .xdata tables for various
// Windows-related EH personalities.

using MBBOrBasicBlock = PointerUnion<const BasicBlock *, MachineBasicBlock *>;

struct CxxUnwindMapEntry {
  int ToState;
  MBBOrBasicBlock Cleanup;
};

/// Similar to CxxUnwindMapEntry, but supports SEH filters.
struct SEHUnwindMapEntry {
  /// If unwinding continues through this handler, transition to the handler at
  /// this state. This indexes into SEHUnwindMap.
  int ToState = -1;

  bool IsFinally = false;

  /// Holds the filter expression function.
  const Function *Filter = nullptr;

  /// Holds the __except or __finally basic block.
  MBBOrBasicBlock Handler;
};

struct WinEHHandlerType {
  int Adjectives;
  /// The CatchObj starts out life as an LLVM alloca and is eventually turned
  /// frame index.
  union {
    const AllocaInst *Alloca;
    int FrameIndex;
  } CatchObj = {};
  GlobalVariable *TypeDescriptor;
  MBBOrBasicBlock Handler;
};

struct WinEHTryBlockMapEntry {
  int TryLow = -1;
  int TryHigh = -1;
  int CatchHigh = -1;
  SmallVector<WinEHHandlerType, 1> HandlerArray;
};

enum class ClrHandlerType { Catch, Finally, Fault, Filter };

struct ClrEHUnwindMapEntry {
  MBBOrBasicBlock Handler;
  uint32_t TypeToken;
  int HandlerParentState; ///< Outer handler enclosing this entry's handler
  int TryParentState; ///< Outer try region enclosing this entry's try region,
                      ///< treating later catches on same try as "outer"
  ClrHandlerType HandlerType;
};

struct WinEHFuncInfo {
  DenseMap<const Instruction *, int> EHPadStateMap;
  DenseMap<const FuncletPadInst *, int> FuncletBaseStateMap;
  DenseMap<const InvokeInst *, int> InvokeStateMap;
  DenseMap<MCSymbol *, std::pair<int, MCSymbol *>> LabelToStateMap;
  DenseMap<const BasicBlock *, int> BlockToStateMap; // for AsynchEH
  SmallVector<CxxUnwindMapEntry, 4> CxxUnwindMap;
  SmallVector<WinEHTryBlockMapEntry, 4> TryBlockMap;
  SmallVector<SEHUnwindMapEntry, 4> SEHUnwindMap;
  SmallVector<ClrEHUnwindMapEntry, 4> ClrEHUnwindMap;
  int UnwindHelpFrameIdx = std::numeric_limits<int>::max();
  int PSPSymFrameIdx = std::numeric_limits<int>::max();

  int getLastStateNumber() const { return CxxUnwindMap.size() - 1; }

  void addIPToStateRange(const InvokeInst *II, MCSymbol *InvokeBegin,
                         MCSymbol *InvokeEnd);

  void addIPToStateRange(int State, MCSymbol *InvokeBegin, MCSymbol *InvokeEnd);

  int EHRegNodeFrameIndex = std::numeric_limits<int>::max();
  int EHRegNodeEndOffset = std::numeric_limits<int>::max();
  int EHGuardFrameIndex = std::numeric_limits<int>::max();
  int SEHSetFrameOffset = std::numeric_limits<int>::max();

  WinEHFuncInfo();
};

/// Analyze the IR in ParentFn and it's handlers to build WinEHFuncInfo, which
/// describes the state numbers and tables used by __CxxFrameHandler3. This
/// analysis assumes that WinEHPrepare has already been run.
void calculateWinCXXEHStateNumbers(const Function *ParentFn,
                                   WinEHFuncInfo &FuncInfo);

void calculateSEHStateNumbers(const Function *ParentFn,
                              WinEHFuncInfo &FuncInfo);

void calculateClrEHStateNumbers(const Function *Fn, WinEHFuncInfo &FuncInfo);

// For AsynchEH (VC++ option -EHa)
void calculateCXXStateForAsynchEH(const BasicBlock *BB, int State,
                                  WinEHFuncInfo &FuncInfo);
void calculateSEHStateForAsynchEH(const BasicBlock *BB, int State,
                                  WinEHFuncInfo &FuncInfo);

} // end namespace llvm

#endif // LLVM_CODEGEN_WINEHFUNCINFO_H
