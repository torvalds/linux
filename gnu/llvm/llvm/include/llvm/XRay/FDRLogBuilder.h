//===- FDRLogBuilder.h - XRay FDR Log Building Utility --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_FDRLOGBUILDER_H
#define LLVM_XRAY_FDRLOGBUILDER_H

#include "llvm/XRay/FDRRecords.h"

namespace llvm {
namespace xray {

/// The LogBuilder class allows for creating ad-hoc collections of records
/// through the `add<...>(...)` function. An example use of this API is in
/// crafting arbitrary sequences of records:
///
///   auto Records = LogBuilder()
///       .add<BufferExtents>(256)
///       .add<NewBufferRecord>(1)
///       .consume();
///
class LogBuilder {
  std::vector<std::unique_ptr<Record>> Records;

public:
  template <class R, class... T> LogBuilder &add(T &&... A) {
    Records.emplace_back(new R(std::forward<T>(A)...));
    return *this;
  }

  std::vector<std::unique_ptr<Record>> consume() { return std::move(Records); }
};

} // namespace xray
} // namespace llvm

#endif // LLVM_XRAY_FDRLOGBUILDER_H
