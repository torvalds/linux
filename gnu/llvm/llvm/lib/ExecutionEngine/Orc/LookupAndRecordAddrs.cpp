//===------- LookupAndRecordAddrs.h - Symbol lookup support utility -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/LookupAndRecordAddrs.h"

#include <future>

namespace llvm {
namespace orc {

void lookupAndRecordAddrs(
    unique_function<void(Error)> OnRecorded, ExecutionSession &ES, LookupKind K,
    const JITDylibSearchOrder &SearchOrder,
    std::vector<std::pair<SymbolStringPtr, ExecutorAddr *>> Pairs,
    SymbolLookupFlags LookupFlags) {

  SymbolLookupSet Symbols;
  for (auto &KV : Pairs)
    Symbols.add(KV.first, LookupFlags);

  ES.lookup(
      K, SearchOrder, std::move(Symbols), SymbolState::Ready,
      [Pairs = std::move(Pairs),
       OnRec = std::move(OnRecorded)](Expected<SymbolMap> Result) mutable {
        if (!Result)
          return OnRec(Result.takeError());
        for (auto &KV : Pairs) {
          auto I = Result->find(KV.first);
          *KV.second =
              I != Result->end() ? I->second.getAddress() : orc::ExecutorAddr();
        }
        OnRec(Error::success());
      },
      NoDependenciesToRegister);
}

Error lookupAndRecordAddrs(
    ExecutionSession &ES, LookupKind K, const JITDylibSearchOrder &SearchOrder,
    std::vector<std::pair<SymbolStringPtr, ExecutorAddr *>> Pairs,
    SymbolLookupFlags LookupFlags) {

  std::promise<MSVCPError> ResultP;
  auto ResultF = ResultP.get_future();
  lookupAndRecordAddrs([&](Error Err) { ResultP.set_value(std::move(Err)); },
                       ES, K, SearchOrder, std::move(Pairs), LookupFlags);
  return ResultF.get();
}

Error lookupAndRecordAddrs(
    ExecutorProcessControl &EPC, tpctypes::DylibHandle H,
    std::vector<std::pair<SymbolStringPtr, ExecutorAddr *>> Pairs,
    SymbolLookupFlags LookupFlags) {

  SymbolLookupSet Symbols;
  for (auto &KV : Pairs)
    Symbols.add(KV.first, LookupFlags);

  ExecutorProcessControl::LookupRequest LR(H, Symbols);
  auto Result = EPC.lookupSymbols(LR);
  if (!Result)
    return Result.takeError();

  if (Result->size() != 1)
    return make_error<StringError>("Error in lookup result",
                                   inconvertibleErrorCode());
  if (Result->front().size() != Pairs.size())
    return make_error<StringError>("Error in lookup result elements",
                                   inconvertibleErrorCode());

  for (unsigned I = 0; I != Pairs.size(); ++I)
    *Pairs[I].second = Result->front()[I].getAddress();

  return Error::success();
}

} // End namespace orc.
} // End namespace llvm.
