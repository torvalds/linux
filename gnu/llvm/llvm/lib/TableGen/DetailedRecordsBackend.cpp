//===- DetailedRecordBackend.cpp - Detailed Records Report      -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This Tablegen backend prints a report that includes all the global 
// variables, classes, and records in complete detail. It includes more
// detail than the default TableGen printer backend.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <map>
#include <memory>
#include <string>
#include <utility>

#define DEBUG_TYPE "detailed-records-backend"

#define NL "\n"

using namespace llvm;

namespace {

class DetailedRecordsEmitter {
private:
  RecordKeeper &Records;

public:
  DetailedRecordsEmitter(RecordKeeper &RK) : Records(RK) {}

  void run(raw_ostream &OS);
  void printReportHeading(raw_ostream &OS);
  void printVariables(raw_ostream &OS);
  void printClasses(raw_ostream &OS);
  void printRecords(raw_ostream &OS);
  void printSectionHeading(StringRef Title, int Count, raw_ostream &OS);
  void printDefms(Record *Rec, raw_ostream &OS);
  void printTemplateArgs(Record *Rec, raw_ostream &OS);
  void printSuperclasses(Record *Rec, raw_ostream &OS);
  void printFields(Record *Rec, raw_ostream &OS);
}; // emitter class

} // anonymous namespace

// Print the report.
void DetailedRecordsEmitter::run(raw_ostream &OS) {
  printReportHeading(OS);
  printVariables(OS);
  printClasses(OS);
  printRecords(OS);
}

// Print the report heading, including the source file name.
void DetailedRecordsEmitter::printReportHeading(raw_ostream &OS) {
  OS << formatv("DETAILED RECORDS for file {0}\n", Records.getInputFilename());
}

// Print the global variables.
void DetailedRecordsEmitter::printVariables(raw_ostream &OS) {
  const auto GlobalList = Records.getGlobals();
  printSectionHeading("Global Variables", GlobalList.size(), OS);

  OS << NL;
  for (const auto &Var : GlobalList) {
    OS << Var.first << " = " << Var.second->getAsString() << NL;
  }
}

// Print the classes, including the template arguments, superclasses,
// and fields.
void DetailedRecordsEmitter::printClasses(raw_ostream &OS) {
  const auto &ClassList = Records.getClasses();
  printSectionHeading("Classes", ClassList.size(), OS);

  for (const auto &ClassPair : ClassList) {
    auto *const Class = ClassPair.second.get();
    OS << formatv("\n{0}  |{1}|\n", Class->getNameInitAsString(),
                  SrcMgr.getFormattedLocationNoOffset(Class->getLoc().front()));
    printTemplateArgs(Class, OS);
    printSuperclasses(Class, OS);
    printFields(Class, OS);
  }
}

// Print the records, including the defm sequences, supercasses,
// and fields.
void DetailedRecordsEmitter::printRecords(raw_ostream &OS) {
  const auto &RecordList = Records.getDefs();
  printSectionHeading("Records", RecordList.size(), OS);

  for (const auto &RecPair : RecordList) {
    auto *const Rec = RecPair.second.get();
    std::string Name = Rec->getNameInitAsString();
    OS << formatv("\n{0}  |{1}|\n", Name.empty() ? "\"\"" : Name,
                  SrcMgr.getFormattedLocationNoOffset(Rec->getLoc().front()));
    printDefms(Rec, OS);
    printSuperclasses(Rec, OS);
    printFields(Rec, OS);
  }
}

// Print a section heading with the name of the section and
// the item count.
void DetailedRecordsEmitter::printSectionHeading(StringRef Title, int Count,
                                                 raw_ostream &OS) {
  OS << formatv("\n{0} {1} ({2}) {0}\n", "--------------------", Title, Count);
}

// Print the record's defm source locations, if any. Note that they
// are stored in the reverse order of their invocation.
void DetailedRecordsEmitter::printDefms(Record *Rec, raw_ostream &OS) {
  const auto &LocList = Rec->getLoc();
  if (LocList.size() < 2)
    return;

  OS << "  Defm sequence:";
  for (unsigned I = LocList.size() - 1; I >= 1; --I) {
    OS << formatv(" |{0}|", SrcMgr.getFormattedLocationNoOffset(LocList[I]));
  }
  OS << NL;
}

// Print the template arguments of a class.
void DetailedRecordsEmitter::printTemplateArgs(Record *Rec,
                                               raw_ostream &OS) {
  ArrayRef<Init *> Args = Rec->getTemplateArgs();
  if (Args.empty()) {
    OS << "  Template args: (none)\n";
    return;
  }

  OS << "  Template args:\n";
  for (const Init *ArgName : Args) {
    const RecordVal *Value = Rec->getValue(ArgName);
    assert(Value && "Template argument value not found.");
    OS << "    ";
    Value->print(OS, false);
    OS << formatv("  |{0}|", SrcMgr.getFormattedLocationNoOffset(Value->getLoc()));
    OS << NL;
  }
}

// Print the superclasses of a class or record. Indirect superclasses
// are enclosed in parentheses.
void DetailedRecordsEmitter::printSuperclasses(Record *Rec, raw_ostream &OS) {
  ArrayRef<std::pair<Record *, SMRange>> Superclasses = Rec->getSuperClasses();
  if (Superclasses.empty()) {
    OS << "  Superclasses: (none)\n";
    return;
  }

  OS << "  Superclasses:";
  for (const auto &SuperclassPair : Superclasses) {
    auto *ClassRec = SuperclassPair.first;
    if (Rec->hasDirectSuperClass(ClassRec))
      OS << formatv(" {0}", ClassRec->getNameInitAsString());
    else
      OS << formatv(" ({0})", ClassRec->getNameInitAsString());
  }
  OS << NL;
}

// Print the fields of a class or record, including their source locations.
void DetailedRecordsEmitter::printFields(Record *Rec, raw_ostream &OS) {
  const auto &ValueList = Rec->getValues();
  if (ValueList.empty()) {
    OS << "  Fields: (none)\n";
    return;
  }

  OS << "  Fields:\n";
  for (const RecordVal &Value : ValueList)
    if (!Rec->isTemplateArg(Value.getNameInit())) {
      OS << "    ";
      Value.print(OS, false);
      OS << formatv("  |{0}|\n",
                    SrcMgr.getFormattedLocationNoOffset(Value.getLoc()));
    }
}

namespace llvm {

// This function is called by TableGen after parsing the files.

void EmitDetailedRecords(RecordKeeper &RK, raw_ostream &OS) {
  // Instantiate the emitter class and invoke run().
  DetailedRecordsEmitter(RK).run(OS);
}

} // namespace llvm
