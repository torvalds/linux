//===- FDRRecordConsumer.h - XRay Flight Data Recorder Mode Records -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_FDRRECORDCONSUMER_H
#define LLVM_XRAY_FDRRECORDCONSUMER_H

#include "llvm/Support/Error.h"
#include "llvm/XRay/FDRRecords.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace llvm {
namespace xray {

class RecordConsumer {
public:
  virtual Error consume(std::unique_ptr<Record> R) = 0;
  virtual ~RecordConsumer() = default;
};

// This consumer will collect all the records into a vector of records, in
// arrival order.
class LogBuilderConsumer : public RecordConsumer {
  std::vector<std::unique_ptr<Record>> &Records;

public:
  explicit LogBuilderConsumer(std::vector<std::unique_ptr<Record>> &R)
      : Records(R) {}

  Error consume(std::unique_ptr<Record> R) override;
};

// A PipelineConsumer applies a set of visitors to every consumed Record, in the
// order by which the visitors are added to the pipeline in the order of
// appearance.
class PipelineConsumer : public RecordConsumer {
  std::vector<RecordVisitor *> Visitors;

public:
  PipelineConsumer(std::initializer_list<RecordVisitor *> V) : Visitors(V) {}

  Error consume(std::unique_ptr<Record> R) override;
};

} // namespace xray
} // namespace llvm

#endif // LLVM_XRAY_FDRRECORDCONSUMER_H
