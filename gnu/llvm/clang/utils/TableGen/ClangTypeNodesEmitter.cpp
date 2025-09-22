//=== ClangTypeNodesEmitter.cpp - Generate type node tables -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tblgen backend emits the node table (the .def file) for Clang
// type nodes.
//
// This file defines the AST type info database. Each type node is
// enumerated by providing its name (e.g., "Builtin" or "Enum") and
// base class (e.g., "Type" or "TagType"). Depending on where in the
// abstract syntax tree the type will show up, the enumeration uses
// one of five different macros:
//
//    TYPE(Class, Base) - A type that can show up anywhere in the AST,
//    and might be dependent, canonical, or non-canonical. All clients
//    will need to understand these types.
//
//    ABSTRACT_TYPE(Class, Base) - An abstract class that shows up in
//    the type hierarchy but has no concrete instances.
//
//    NON_CANONICAL_TYPE(Class, Base) - A type that can show up
//    anywhere in the AST but will never be a part of a canonical
//    type. Clients that only need to deal with canonical types
//    (ignoring, e.g., typedefs and other type aliases used for
//    pretty-printing) can ignore these types.
//
//    DEPENDENT_TYPE(Class, Base) - A type that will only show up
//    within a C++ template that has not been instantiated, e.g., a
//    type that is always dependent. Clients that do not need to deal
//    with uninstantiated C++ templates can ignore these types.
//
//    NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) - A type that
//    is non-canonical unless it is dependent.  Defaults to TYPE because
//    it is neither reliably dependent nor reliably non-canonical.
//
// There is a sixth macro, independent of the others.  Most clients
// will not need to use it.
//
//    LEAF_TYPE(Class) - A type that never has inner types.  Clients
//    which can operate on such types more efficiently may wish to do so.
//
//===----------------------------------------------------------------------===//

#include "ASTTableGen.h"
#include "TableGenBackends.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <set>
#include <string>
#include <vector>

using namespace llvm;
using namespace clang;
using namespace clang::tblgen;

// These are spellings in the generated output.
#define TypeMacroName "TYPE"
#define AbstractTypeMacroName "ABSTRACT_TYPE"
#define DependentTypeMacroName "DEPENDENT_TYPE"
#define NonCanonicalTypeMacroName "NON_CANONICAL_TYPE"
#define NonCanonicalUnlessDependentTypeMacroName "NON_CANONICAL_UNLESS_DEPENDENT_TYPE"
#define TypeMacroArgs "(Class, Base)"
#define LastTypeMacroName "LAST_TYPE"
#define LeafTypeMacroName "LEAF_TYPE"

#define TypeClassName "Type"

namespace {
class TypeNodeEmitter {
  RecordKeeper &Records;
  raw_ostream &Out;
  const std::vector<Record*> Types;
  std::vector<StringRef> MacrosToUndef;

public:
  TypeNodeEmitter(RecordKeeper &records, raw_ostream &out)
    : Records(records), Out(out),
      Types(Records.getAllDerivedDefinitions(TypeNodeClassName)) {
  }

  void emit();

private:
  void emitFallbackDefine(StringRef macroName, StringRef fallbackMacroName,
                          StringRef args);

  void emitNodeInvocations();
  void emitLastNodeInvocation(TypeNode lastType);
  void emitLeafNodeInvocations();

  void addMacroToUndef(StringRef macroName);
  void emitUndefs();
};
}

void TypeNodeEmitter::emit() {
  if (Types.empty())
    PrintFatalError("no Type records in input!");

  emitSourceFileHeader("An x-macro database of Clang type nodes", Out, Records);

  // Preamble
  addMacroToUndef(TypeMacroName);
  addMacroToUndef(AbstractTypeMacroName);
  emitFallbackDefine(AbstractTypeMacroName, TypeMacroName, TypeMacroArgs);
  emitFallbackDefine(NonCanonicalTypeMacroName, TypeMacroName, TypeMacroArgs);
  emitFallbackDefine(DependentTypeMacroName, TypeMacroName, TypeMacroArgs);
  emitFallbackDefine(NonCanonicalUnlessDependentTypeMacroName, TypeMacroName, 
                     TypeMacroArgs);

  // Invocations.
  emitNodeInvocations();
  emitLeafNodeInvocations();

  // Postmatter
  emitUndefs();
}

void TypeNodeEmitter::emitFallbackDefine(StringRef macroName,
                                         StringRef fallbackMacroName,
                                         StringRef args) {
  Out << "#ifndef " << macroName << "\n";
  Out << "#  define " << macroName << args
      << " " << fallbackMacroName << args << "\n";
  Out << "#endif\n";

  addMacroToUndef(macroName);
}

void TypeNodeEmitter::emitNodeInvocations() {
  TypeNode lastType;

  visitASTNodeHierarchy<TypeNode>(Records, [&](TypeNode type, TypeNode base) {
    // If this is the Type node itself, skip it; it can't be handled
    // uniformly by metaprograms because it doesn't have a base.
    if (!base) return;

    // Figure out which macro to use.
    StringRef macroName;
    auto setMacroName = [&](StringRef newName) {
      if (!macroName.empty())
        PrintFatalError(type.getLoc(),
                        Twine("conflict when computing macro name for "
                              "Type node: trying to use both \"")
                          + macroName + "\" and \"" + newName + "\"");
      macroName = newName;
    };
    if (type.isSubClassOf(AlwaysDependentClassName))
      setMacroName(DependentTypeMacroName);
    if (type.isSubClassOf(NeverCanonicalClassName))
      setMacroName(NonCanonicalTypeMacroName);
    if (type.isSubClassOf(NeverCanonicalUnlessDependentClassName))
      setMacroName(NonCanonicalUnlessDependentTypeMacroName);
    if (type.isAbstract())
      setMacroName(AbstractTypeMacroName);
    if (macroName.empty())
      macroName = TypeMacroName;

    // Generate the invocation line.
    Out << macroName << "(" << type.getId() << ", "
        << base.getClassName() << ")\n";

    lastType = type;
  });

  emitLastNodeInvocation(lastType);
}

void TypeNodeEmitter::emitLastNodeInvocation(TypeNode type) {
  // We check that this is non-empty earlier.
  Out << "#ifdef " LastTypeMacroName "\n"
         LastTypeMacroName "(" << type.getId() << ")\n"
         "#undef " LastTypeMacroName "\n"
         "#endif\n";
}

void TypeNodeEmitter::emitLeafNodeInvocations() {
  Out << "#ifdef " LeafTypeMacroName "\n";

  for (TypeNode type : Types) {
    if (!type.isSubClassOf(LeafTypeClassName)) continue;
    Out << LeafTypeMacroName "(" << type.getId() << ")\n";
  }

  Out << "#undef " LeafTypeMacroName "\n"
         "#endif\n";
}

void TypeNodeEmitter::addMacroToUndef(StringRef macroName) {
  MacrosToUndef.push_back(macroName);
}

void TypeNodeEmitter::emitUndefs() {
  for (auto &macroName : MacrosToUndef) {
    Out << "#undef " << macroName << "\n";
  }
}

void clang::EmitClangTypeNodes(RecordKeeper &records, raw_ostream &out) {
  TypeNodeEmitter(records, out).emit();
}
