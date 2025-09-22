//===- llvm/TableGen/Parser.h - tblgen parser entry point -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares an entry point into the tablegen parser for use by tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_PARSER_H
#define LLVM_TABLEGEN_PARSER_H

namespace llvm {
class RecordKeeper;
class SourceMgr;

/// Parse the TableGen file defined within the main buffer of the given
/// SourceMgr. On success, populates the provided RecordKeeper with the parsed
/// records and returns false. On failure, returns true.
///
/// NOTE: TableGen currently relies on global state within a given parser
///       invocation, so this function is not thread-safe.
bool TableGenParseFile(SourceMgr &InputSrcMgr, RecordKeeper &Records);

} // end namespace llvm

#endif // LLVM_TABLEGEN_PARSER_H
