//=- ClangSACheckersEmitter.cpp - Generate Clang SA checkers tables -*- C++ -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits Clang Static Analyzer checkers tables.
//
//===----------------------------------------------------------------------===//

#include "TableGenBackends.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <map>
#include <string>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Static Analyzer Checkers Tables generation
//===----------------------------------------------------------------------===//

static std::string getPackageFullName(const Record *R, StringRef Sep = ".");

static std::string getParentPackageFullName(const Record *R,
                                            StringRef Sep = ".") {
  std::string name;
  if (DefInit *DI = dyn_cast<DefInit>(R->getValueInit("ParentPackage")))
    name = getPackageFullName(DI->getDef(), Sep);
  return name;
}

static std::string getPackageFullName(const Record *R, StringRef Sep) {
  std::string name = getParentPackageFullName(R, Sep);
  if (!name.empty())
    name += Sep;
  assert(!R->getValueAsString("PackageName").empty());
  name += R->getValueAsString("PackageName");
  return name;
}

static std::string getCheckerFullName(const Record *R, StringRef Sep = ".") {
  std::string name = getParentPackageFullName(R, Sep);
  if (!name.empty())
    name += Sep;
  assert(!R->getValueAsString("CheckerName").empty());
  name += R->getValueAsString("CheckerName");
  return name;
}

static std::string getStringValue(const Record &R, StringRef field) {
  if (StringInit *SI = dyn_cast<StringInit>(R.getValueInit(field)))
    return std::string(SI->getValue());
  return std::string();
}

// Calculates the integer value representing the BitsInit object
static inline uint64_t getValueFromBitsInit(const BitsInit *B, const Record &R) {
  assert(B->getNumBits() <= sizeof(uint64_t) * 8 && "BitInits' too long!");

  uint64_t Value = 0;
  for (unsigned i = 0, e = B->getNumBits(); i != e; ++i) {
    const auto *Bit = dyn_cast<BitInit>(B->getBit(i));
    if (Bit)
      Value |= uint64_t(Bit->getValue()) << i;
    else
      PrintFatalError(R.getLoc(),
                      "missing Documentation for " + getCheckerFullName(&R));
  }
  return Value;
}

static std::string getCheckerDocs(const Record &R) {
  const BitsInit *BI = R.getValueAsBitsInit("Documentation");
  if (!BI)
    PrintFatalError(R.getLoc(), "missing Documentation<...> member for " +
                                    getCheckerFullName(&R));

  // Ignore 'Documentation<NotDocumented>' checkers.
  if (getValueFromBitsInit(BI, R) == 0)
    return "";

  std::string CheckerFullName = StringRef(getCheckerFullName(&R, "-")).lower();
  return (llvm::Twine("https://clang.llvm.org/docs/analyzer/checkers.html#") +
          CheckerFullName)
      .str();
}

/// Retrieves the type from a CmdOptionTypeEnum typed Record object. Note that
/// the class itself has to be modified for adding a new option type in
/// CheckerBase.td.
static std::string getCheckerOptionType(const Record &R) {
  if (BitsInit *BI = R.getValueAsBitsInit("Type")) {
    switch(getValueFromBitsInit(BI, R)) {
    case 0:
      return "int";
    case 1:
      return "string";
    case 2:
      return "bool";
    }
  }
  PrintFatalError(R.getLoc(),
                  "unable to parse command line option type for "
                  + getCheckerFullName(&R));
  return "";
}

static std::string getDevelopmentStage(const Record &R) {
  if (BitsInit *BI = R.getValueAsBitsInit("DevelopmentStage")) {
    switch(getValueFromBitsInit(BI, R)) {
    case 0:
      return "alpha";
    case 1:
      return "released";
    }
  }

  PrintFatalError(R.getLoc(),
                  "unable to parse command line option type for "
                  + getCheckerFullName(&R));
  return "";
}

static bool isHidden(const Record *R) {
  if (R->getValueAsBit("Hidden"))
    return true;

  // Not declared as hidden, check the parent package if it is hidden.
  if (DefInit *DI = dyn_cast<DefInit>(R->getValueInit("ParentPackage")))
    return isHidden(DI->getDef());

  return false;
}

static void printChecker(llvm::raw_ostream &OS, const Record &R) {
  OS << "CHECKER(" << "\"";
  OS.write_escaped(getCheckerFullName(&R)) << "\", ";
  OS << R.getName() << ", ";
  OS << "\"";
  OS.write_escaped(getStringValue(R, "HelpText")) << "\", ";
  OS << "\"";
  OS.write_escaped(getCheckerDocs(R));
  OS << "\", ";

  if (!isHidden(&R))
    OS << "false";
  else
    OS << "true";

  OS << ")\n";
}

static void printOption(llvm::raw_ostream &OS, StringRef FullName,
                        const Record &R) {
  OS << "\"";
  OS.write_escaped(getCheckerOptionType(R)) << "\", \"";
  OS.write_escaped(FullName) << "\", ";
  OS << '\"' << getStringValue(R, "CmdFlag") << "\", ";
  OS << '\"';
  OS.write_escaped(getStringValue(R, "Desc")) << "\", ";
  OS << '\"';
  OS.write_escaped(getStringValue(R, "DefaultVal")) << "\", ";
  OS << '\"';
  OS << getDevelopmentStage(R) << "\", ";

  if (!R.getValueAsBit("Hidden"))
    OS << "false";
  else
    OS << "true";
}

void clang::EmitClangSACheckers(RecordKeeper &Records, raw_ostream &OS) {
  std::vector<Record*> checkers = Records.getAllDerivedDefinitions("Checker");
  std::vector<Record*> packages = Records.getAllDerivedDefinitions("Package");

  using SortedRecords = llvm::StringMap<const Record *>;

  OS << "// This file is automatically generated. Do not edit this file by "
        "hand.\n";

  // Emit packages.
  //
  // PACKAGE(PACKAGENAME)
  //   - PACKAGENAME: The name of the package.
  OS << "\n"
        "#ifdef GET_PACKAGES\n";
  {
    SortedRecords sortedPackages;
    for (unsigned i = 0, e = packages.size(); i != e; ++i)
      sortedPackages[getPackageFullName(packages[i])] = packages[i];
  
    for (SortedRecords::iterator
           I = sortedPackages.begin(), E = sortedPackages.end(); I != E; ++I) {
      const Record &R = *I->second;
  
      OS << "PACKAGE(" << "\"";
      OS.write_escaped(getPackageFullName(&R)) << '\"';
      OS << ")\n";
    }
  }
  OS << "#endif // GET_PACKAGES\n"
        "\n";

  // Emit a package option.
  //
  // PACKAGE_OPTION(OPTIONTYPE, PACKAGENAME, OPTIONNAME, DESCRIPTION, DEFAULT)
  //   - OPTIONTYPE: Type of the option, whether it's integer or boolean etc.
  //                 This is important for validating user input. Note that
  //                 it's a string, rather than an actual type: since we can
  //                 load checkers runtime, we can't use template hackery for
  //                 sorting this out compile-time.
  //   - PACKAGENAME: Name of the package.
  //   - OPTIONNAME: Name of the option.
  //   - DESCRIPTION
  //   - DEFAULT: The default value for this option.
  //
  // The full option can be specified in the command like this:
  //   -analyzer-config PACKAGENAME:OPTIONNAME=VALUE
  OS << "\n"
        "#ifdef GET_PACKAGE_OPTIONS\n";
  for (const Record *Package : packages) {

    if (Package->isValueUnset("PackageOptions"))
      continue;

    std::vector<Record *> PackageOptions = Package
                                       ->getValueAsListOfDefs("PackageOptions");
    for (Record *PackageOpt : PackageOptions) {
      OS << "PACKAGE_OPTION(";
      printOption(OS, getPackageFullName(Package), *PackageOpt);
      OS << ")\n";
    }
  }
  OS << "#endif // GET_PACKAGE_OPTIONS\n"
        "\n";

  // Emit checkers.
  //
  // CHECKER(FULLNAME, CLASS, HELPTEXT)
  //   - FULLNAME: The full name of the checker, including packages, e.g.:
  //               alpha.cplusplus.UninitializedObject
  //   - CLASS: The name of the checker, with "Checker" appended, e.g.:
  //            UninitializedObjectChecker
  //   - HELPTEXT: The description of the checker.
  OS << "\n"
        "#ifdef GET_CHECKERS\n"
        "\n";
  for (const Record *checker : checkers) {
    printChecker(OS, *checker);
  }
  OS << "\n"
        "#endif // GET_CHECKERS\n"
        "\n";

  // Emit dependencies.
  //
  // CHECKER_DEPENDENCY(FULLNAME, DEPENDENCY)
  //   - FULLNAME: The full name of the checker that depends on another checker.
  //   - DEPENDENCY: The full name of the checker FULLNAME depends on.
  OS << "\n"
        "#ifdef GET_CHECKER_DEPENDENCIES\n";
  for (const Record *Checker : checkers) {
    if (Checker->isValueUnset("Dependencies"))
      continue;

    for (const Record *Dependency :
                            Checker->getValueAsListOfDefs("Dependencies")) {
      OS << "CHECKER_DEPENDENCY(";
      OS << '\"';
      OS.write_escaped(getCheckerFullName(Checker)) << "\", ";
      OS << '\"';
      OS.write_escaped(getCheckerFullName(Dependency)) << '\"';
      OS << ")\n";
    }
  }
  OS << "\n"
        "#endif // GET_CHECKER_DEPENDENCIES\n";

  // Emit weak dependencies.
  //
  // CHECKER_DEPENDENCY(FULLNAME, DEPENDENCY)
  //   - FULLNAME: The full name of the checker that is supposed to be
  //     registered first.
  //   - DEPENDENCY: The full name of the checker FULLNAME weak depends on.
  OS << "\n"
        "#ifdef GET_CHECKER_WEAK_DEPENDENCIES\n";
  for (const Record *Checker : checkers) {
    if (Checker->isValueUnset("WeakDependencies"))
      continue;

    for (const Record *Dependency :
         Checker->getValueAsListOfDefs("WeakDependencies")) {
      OS << "CHECKER_WEAK_DEPENDENCY(";
      OS << '\"';
      OS.write_escaped(getCheckerFullName(Checker)) << "\", ";
      OS << '\"';
      OS.write_escaped(getCheckerFullName(Dependency)) << '\"';
      OS << ")\n";
    }
  }
  OS << "\n"
        "#endif // GET_CHECKER_WEAK_DEPENDENCIES\n";

  // Emit a package option.
  //
  // CHECKER_OPTION(OPTIONTYPE, CHECKERNAME, OPTIONNAME, DESCRIPTION, DEFAULT)
  //   - OPTIONTYPE: Type of the option, whether it's integer or boolean etc.
  //                 This is important for validating user input. Note that
  //                 it's a string, rather than an actual type: since we can
  //                 load checkers runtime, we can't use template hackery for
  //                 sorting this out compile-time.
  //   - CHECKERNAME: Name of the package.
  //   - OPTIONNAME: Name of the option.
  //   - DESCRIPTION
  //   - DEFAULT: The default value for this option.
  //
  // The full option can be specified in the command like this:
  //   -analyzer-config CHECKERNAME:OPTIONNAME=VALUE
  OS << "\n"
        "#ifdef GET_CHECKER_OPTIONS\n";
  for (const Record *Checker : checkers) {

    if (Checker->isValueUnset("CheckerOptions"))
      continue;

    std::vector<Record *> CheckerOptions = Checker
                                       ->getValueAsListOfDefs("CheckerOptions");
    for (Record *CheckerOpt : CheckerOptions) {
      OS << "CHECKER_OPTION(";
      printOption(OS, getCheckerFullName(Checker), *CheckerOpt);
      OS << ")\n";
    }
  }
  OS << "#endif // GET_CHECKER_OPTIONS\n"
        "\n";
}
