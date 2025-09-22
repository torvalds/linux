//===- CVTypeVisitor.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CVTYPEVISITOR_H
#define LLVM_DEBUGINFO_CODEVIEW_CVTYPEVISITOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class TypeIndex;
class TypeCollection;
class TypeVisitorCallbacks;
struct CVMemberRecord;

enum VisitorDataSource {
  VDS_BytesPresent, // The record bytes are passed into the visitation
                    // function.  The algorithm should first deserialize them
                    // before passing them on through the pipeline.
  VDS_BytesExternal // The record bytes are not present, and it is the
                    // responsibility of the visitor callback interface to
                    // supply the bytes.
};

Error visitTypeRecord(CVType &Record, TypeIndex Index,
                      TypeVisitorCallbacks &Callbacks,
                      VisitorDataSource Source = VDS_BytesPresent);
Error visitTypeRecord(CVType &Record, TypeVisitorCallbacks &Callbacks,
                      VisitorDataSource Source = VDS_BytesPresent);

Error visitMemberRecord(CVMemberRecord Record, TypeVisitorCallbacks &Callbacks,
                        VisitorDataSource Source = VDS_BytesPresent);
Error visitMemberRecord(TypeLeafKind Kind, ArrayRef<uint8_t> Record,
                        TypeVisitorCallbacks &Callbacks);

Error visitMemberRecordStream(ArrayRef<uint8_t> FieldList,
                              TypeVisitorCallbacks &Callbacks);

Error visitTypeStream(const CVTypeArray &Types, TypeVisitorCallbacks &Callbacks,
                      VisitorDataSource Source = VDS_BytesPresent);
Error visitTypeStream(CVTypeRange Types, TypeVisitorCallbacks &Callbacks);
Error visitTypeStream(TypeCollection &Types, TypeVisitorCallbacks &Callbacks);

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_CVTYPEVISITOR_H
