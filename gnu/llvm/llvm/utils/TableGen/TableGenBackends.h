//===- TableGenBackends.h - Declarations for LLVM TableGen Backends -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations for all of the LLVM TableGen
// backends. A "TableGen backend" is just a function. See below for a
// precise description.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_TABLEGENBACKENDS_H
#define LLVM_UTILS_TABLEGEN_TABLEGENBACKENDS_H

#include <string>

// A TableGen backend is a function that looks like
//
//    EmitFoo(RecordKeeper &RK, raw_ostream &OS /*, anything else you need */ )
//
// What you do inside of that function is up to you, but it will usually
// involve generating C++ code to the provided raw_ostream.
//
// The RecordKeeper is just a top-level container for an in-memory
// representation of the data encoded in the TableGen file. What a TableGen
// backend does is walk around that in-memory representation and generate
// stuff based on the information it contains.
//
// The in-memory representation is a node-graph (think of it like JSON but
// with a richer ontology of types), where the nodes are subclasses of
// Record. The methods `getClass`, `getDef` are the basic interface to
// access the node-graph.  RecordKeeper also provides a handy method
// `getAllDerivedDefinitions`. Consult "include/llvm/TableGen/Record.h" for
// the exact interfaces provided by Record's and RecordKeeper.
//
// A common pattern for TableGen backends is for the EmitFoo function to
// instantiate a class which holds some context for the generation process,
// and then have most of the work happen in that class's methods. This
// pattern partly has historical roots in the previous TableGen backend API
// that involved a class and an invocation like `FooEmitter(RK).run(OS)`.
//
// Remember to wrap private things in an anonymous namespace. For most
// backends, this means that the EmitFoo function is the only thing not in
// the anonymous namespace.

// FIXME: Reorganize TableGen so that build dependencies can be more
// accurately expressed. Currently, touching any of the emitters (or
// anything that they transitively depend on) causes everything dependent
// on TableGen to be rebuilt (this includes all the targets!). Perhaps have
// a standalone TableGen binary and have the backends be loadable modules
// of some sort; then the dependency could be expressed as being on the
// module, and all the modules would have a common dependency on the
// TableGen binary with as few dependencies as possible on the rest of
// LLVM.

namespace llvm {

class raw_ostream;
class RecordKeeper;

void EmitMapTable(RecordKeeper &RK, raw_ostream &OS);

// Defined in DecoderEmitter.cpp
void EmitDecoder(RecordKeeper &RK, raw_ostream &OS,
                 const std::string &PredicateNamespace);

} // namespace llvm

#endif
