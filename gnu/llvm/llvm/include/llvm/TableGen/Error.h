//===- llvm/TableGen/Error.h - tblgen error handling helpers ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains error handling helper routines to pretty-print diagnostic
// messages from tblgen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_ERROR_H
#define LLVM_TABLEGEN_ERROR_H

#include "llvm/Support/SourceMgr.h"
#include "llvm/TableGen/Record.h"

namespace llvm {

void PrintNote(const Twine &Msg);
void PrintNote(ArrayRef<SMLoc> NoteLoc, const Twine &Msg);

[[noreturn]] void PrintFatalNote(const Twine &Msg);
[[noreturn]] void PrintFatalNote(ArrayRef<SMLoc> ErrorLoc, const Twine &Msg);
[[noreturn]] void PrintFatalNote(const Record *Rec, const Twine &Msg);
[[noreturn]] void PrintFatalNote(const RecordVal *RecVal, const Twine &Msg);

void PrintWarning(const Twine &Msg);
void PrintWarning(ArrayRef<SMLoc> WarningLoc, const Twine &Msg);
void PrintWarning(const char *Loc, const Twine &Msg);

void PrintError(const Twine &Msg);
void PrintError(ArrayRef<SMLoc> ErrorLoc, const Twine &Msg);
void PrintError(const char *Loc, const Twine &Msg);
void PrintError(const Record *Rec, const Twine &Msg);
void PrintError(const RecordVal *RecVal, const Twine &Msg);

[[noreturn]] void PrintFatalError(const Twine &Msg);
[[noreturn]] void PrintFatalError(ArrayRef<SMLoc> ErrorLoc, const Twine &Msg);
[[noreturn]] void PrintFatalError(const Record *Rec, const Twine &Msg);
[[noreturn]] void PrintFatalError(const RecordVal *RecVal, const Twine &Msg);

void CheckAssert(SMLoc Loc, Init *Condition, Init *Message);
void dumpMessage(SMLoc Loc, Init *Message);

extern SourceMgr SrcMgr;
extern unsigned ErrorsPrinted;

} // end namespace llvm

#endif
