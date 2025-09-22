//===- Error.cpp - tblgen error handling helper routines --------*- C++ -*-===//
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

#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/WithColor.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <cstdlib>

namespace llvm {

SourceMgr SrcMgr;
unsigned ErrorsPrinted = 0;

static void PrintMessage(ArrayRef<SMLoc> Loc, SourceMgr::DiagKind Kind,
                         const Twine &Msg) {
  // Count the total number of errors printed.
  // This is used to exit with an error code if there were any errors.
  if (Kind == SourceMgr::DK_Error)
    ++ErrorsPrinted;

  SMLoc NullLoc;
  if (Loc.empty())
    Loc = NullLoc;
  SrcMgr.PrintMessage(Loc.front(), Kind, Msg);
  for (unsigned i = 1; i < Loc.size(); ++i)
    SrcMgr.PrintMessage(Loc[i], SourceMgr::DK_Note,
                        "instantiated from multiclass");
}

// Functions to print notes.

void PrintNote(const Twine &Msg) {
  WithColor::note() << Msg << "\n";
}

void PrintNote(ArrayRef<SMLoc> NoteLoc, const Twine &Msg) {
  PrintMessage(NoteLoc, SourceMgr::DK_Note, Msg);
}

// Functions to print fatal notes.

void PrintFatalNote(const Twine &Msg) {
  PrintNote(Msg);
  // The following call runs the file cleanup handlers.
  sys::RunInterruptHandlers();
  std::exit(1);
}

void PrintFatalNote(ArrayRef<SMLoc> NoteLoc, const Twine &Msg) {
  PrintNote(NoteLoc, Msg);
  // The following call runs the file cleanup handlers.
  sys::RunInterruptHandlers();
  std::exit(1);
}

// This method takes a Record and uses the source location
// stored in it.
void PrintFatalNote(const Record *Rec, const Twine &Msg) {
  PrintNote(Rec->getLoc(), Msg);
  // The following call runs the file cleanup handlers.
  sys::RunInterruptHandlers();
  std::exit(1);
}

// This method takes a RecordVal and uses the source location
// stored in it.
void PrintFatalNote(const RecordVal *RecVal, const Twine &Msg) {
  PrintNote(RecVal->getLoc(), Msg);
  // The following call runs the file cleanup handlers.
  sys::RunInterruptHandlers();
  std::exit(1);
}

// Functions to print warnings.

void PrintWarning(const Twine &Msg) { WithColor::warning() << Msg << "\n"; }

void PrintWarning(ArrayRef<SMLoc> WarningLoc, const Twine &Msg) {
  PrintMessage(WarningLoc, SourceMgr::DK_Warning, Msg);
}

void PrintWarning(const char *Loc, const Twine &Msg) {
  SrcMgr.PrintMessage(SMLoc::getFromPointer(Loc), SourceMgr::DK_Warning, Msg);
}

// Functions to print errors.

void PrintError(const Twine &Msg) { WithColor::error() << Msg << "\n"; }

void PrintError(ArrayRef<SMLoc> ErrorLoc, const Twine &Msg) {
  PrintMessage(ErrorLoc, SourceMgr::DK_Error, Msg);
}

void PrintError(const char *Loc, const Twine &Msg) {
  SrcMgr.PrintMessage(SMLoc::getFromPointer(Loc), SourceMgr::DK_Error, Msg);
}

// This method takes a Record and uses the source location
// stored in it.
void PrintError(const Record *Rec, const Twine &Msg) {
  PrintMessage(Rec->getLoc(), SourceMgr::DK_Error, Msg);
}

// This method takes a RecordVal and uses the source location
// stored in it.
void PrintError(const RecordVal *RecVal, const Twine &Msg) {
  PrintMessage(RecVal->getLoc(), SourceMgr::DK_Error, Msg);
}

// Functions to print fatal errors.

void PrintFatalError(const Twine &Msg) {
  PrintError(Msg);
  // The following call runs the file cleanup handlers.
  sys::RunInterruptHandlers();
  std::exit(1);
}

void PrintFatalError(ArrayRef<SMLoc> ErrorLoc, const Twine &Msg) {
  PrintError(ErrorLoc, Msg);
  // The following call runs the file cleanup handlers.
  sys::RunInterruptHandlers();
  std::exit(1);
}

// This method takes a Record and uses the source location
// stored in it.
void PrintFatalError(const Record *Rec, const Twine &Msg) {
  PrintError(Rec->getLoc(), Msg);
  // The following call runs the file cleanup handlers.
  sys::RunInterruptHandlers();
  std::exit(1);
}

// This method takes a RecordVal and uses the source location
// stored in it.
void PrintFatalError(const RecordVal *RecVal, const Twine &Msg) {
  PrintError(RecVal->getLoc(), Msg);
  // The following call runs the file cleanup handlers.
  sys::RunInterruptHandlers();
  std::exit(1);
}

// Check an assertion: Obtain the condition value and be sure it is true.
// If not, print a nonfatal error along with the message.
void CheckAssert(SMLoc Loc, Init *Condition, Init *Message) {
  auto *CondValue = dyn_cast_or_null<IntInit>(Condition->convertInitializerTo(
      IntRecTy::get(Condition->getRecordKeeper())));
  if (!CondValue)
    PrintError(Loc, "assert condition must of type bit, bits, or int.");
  else if (!CondValue->getValue()) {
    PrintError(Loc, "assertion failed");
    if (auto *MessageInit = dyn_cast<StringInit>(Message))
      PrintNote(MessageInit->getValue());
    else
      PrintNote("(assert message is not a string)");
  }
}

// Dump a message to stderr.
void dumpMessage(SMLoc Loc, Init *Message) {
  auto *MessageInit = dyn_cast<StringInit>(Message);
  assert(MessageInit && "no debug message to print");
  PrintNote(Loc, MessageInit->getValue());
}

} // end namespace llvm
