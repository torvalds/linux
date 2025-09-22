//===-- AssemblyAnnotationWriter.h - Annotation .ll files -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Clients of the assembly writer can use this interface to add their own
// special-purpose annotations to LLVM assembly language printouts.  Note that
// the assembly parser won't be able to parse these, in general, so
// implementations are advised to print stuff as LLVM comments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_ASSEMBLYANNOTATIONWRITER_H
#define LLVM_IR_ASSEMBLYANNOTATIONWRITER_H

namespace llvm {

class Function;
class BasicBlock;
class Instruction;
class Value;
class formatted_raw_ostream;

class AssemblyAnnotationWriter {
public:
  virtual ~AssemblyAnnotationWriter();

  /// emitFunctionAnnot - This may be implemented to emit a string right before
  /// the start of a function.
  virtual void emitFunctionAnnot(const Function *,
                                 formatted_raw_ostream &) {}

  /// emitBasicBlockStartAnnot - This may be implemented to emit a string right
  /// after the basic block label, but before the first instruction in the
  /// block.
  virtual void emitBasicBlockStartAnnot(const BasicBlock *,
                                        formatted_raw_ostream &) {
  }

  /// emitBasicBlockEndAnnot - This may be implemented to emit a string right
  /// after the basic block.
  virtual void emitBasicBlockEndAnnot(const BasicBlock *,
                                      formatted_raw_ostream &) {
  }

  /// emitInstructionAnnot - This may be implemented to emit a string right
  /// before an instruction is emitted.
  virtual void emitInstructionAnnot(const Instruction *,
                                    formatted_raw_ostream &) {}

  /// printInfoComment - This may be implemented to emit a comment to the
  /// right of an instruction or global value.
  virtual void printInfoComment(const Value &, formatted_raw_ostream &) {}
};

} // End llvm namespace

#endif
