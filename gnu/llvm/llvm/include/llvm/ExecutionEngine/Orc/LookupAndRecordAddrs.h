//===-- LookupAndRecordAddrs.h - Symbol lookup support utility --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Record the addresses of a set of symbols into ExecutorAddr objects.
//
// This can be used to avoid repeated lookup (via ExecutionSession::lookup) of
// the given symbols.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LOOKUPANDRECORDADDRS_H
#define LLVM_EXECUTIONENGINE_ORC_LOOKUPANDRECORDADDRS_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"

#include <vector>

namespace llvm {
namespace orc {

/// Record addresses of the given symbols in the given ExecutorAddrs.
///
/// Useful for making permanent records of symbol addreses to call or
/// access in the executor (e.g. runtime support functions in Platform
/// subclasses).
///
/// By default the symbols are looked up using
/// SymbolLookupFlags::RequiredSymbol, and an error will be generated if any of
/// the requested symbols are not defined.
///
/// If SymbolLookupFlags::WeaklyReferencedSymbol is used then any missing
/// symbols will have their corresponding address objects set to zero, and
/// this function will never generate an error (the caller will need to check
/// addresses before using them).
///
/// Asynchronous version.
void lookupAndRecordAddrs(
    unique_function<void(Error)> OnRecorded, ExecutionSession &ES, LookupKind K,
    const JITDylibSearchOrder &SearchOrder,
    std::vector<std::pair<SymbolStringPtr, ExecutorAddr *>> Pairs,
    SymbolLookupFlags LookupFlags = SymbolLookupFlags::RequiredSymbol);

/// Record addresses of the given symbols in the given ExecutorAddrs.
///
/// Blocking version.
Error lookupAndRecordAddrs(
    ExecutionSession &ES, LookupKind K, const JITDylibSearchOrder &SearchOrder,
    std::vector<std::pair<SymbolStringPtr, ExecutorAddr *>> Pairs,
    SymbolLookupFlags LookupFlags = SymbolLookupFlags::RequiredSymbol);

/// Record addresses of given symbols in the given ExecutorAddrs.
///
/// ExecutorProcessControl lookup version. Lookups are always implicitly
/// weak.
Error lookupAndRecordAddrs(
    ExecutorProcessControl &EPC, tpctypes::DylibHandle H,
    std::vector<std::pair<SymbolStringPtr, ExecutorAddr *>> Pairs,
    SymbolLookupFlags LookupFlags = SymbolLookupFlags::RequiredSymbol);

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LOOKUPANDRECORDADDRS_H
