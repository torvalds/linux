//===- FDRRecordConsumer.h - XRay Flight Data Recorder Mode Records -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/FDRRecordConsumer.h"

namespace llvm {
namespace xray {

Error LogBuilderConsumer::consume(std::unique_ptr<Record> R) {
  if (!R)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Must not call RecordConsumer::consume() with a null pointer.");
  Records.push_back(std::move(R));
  return Error::success();
}

Error PipelineConsumer::consume(std::unique_ptr<Record> R) {
  if (!R)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Must not call RecordConsumer::consume() with a null pointer.");

  // We apply all of the visitors in order, and concatenate errors
  // appropriately.
  Error Result = Error::success();
  for (auto *V : Visitors)
    Result = joinErrors(std::move(Result), R->apply(*V));
  return Result;
}

} // namespace xray
} // namespace llvm
