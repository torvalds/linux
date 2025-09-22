//===-- test_helpers.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a function call tracing system.
//
//===----------------------------------------------------------------------===//
#include "test_helpers.h"
#include "xray/xray_records.h"
#include "xray_buffer_queue.h"
#include "xray_fdr_log_writer.h"
#include <type_traits>

// TODO: Move these to llvm/include/Testing/XRay/...
namespace llvm {
namespace xray {

std::string RecordTypeAsString(RecordTypes T) {
  switch (T) {
  case RecordTypes::ENTER:
    return "llvm::xray::RecordTypes::ENTER";
  case RecordTypes::EXIT:
    return "llvm::xray::RecordTypes::EXIT";
  case RecordTypes::TAIL_EXIT:
    return "llvm::xray::RecordTypes::TAIL_EXIT";
  case RecordTypes::ENTER_ARG:
    return "llvm::xray::RecordTypes::ENTER_ARG";
  case RecordTypes::CUSTOM_EVENT:
    return "llvm::xray::RecordTypes::CUSTOM_EVENT";
  case RecordTypes::TYPED_EVENT:
    return "llvm::xray::RecordTypes::TYPED_EVENT";
  }
  return "<UNKNOWN>";
}

void PrintTo(RecordTypes T, std::ostream *OS) {
  *OS << RecordTypeAsString(T);
}

void PrintTo(const XRayRecord &R, std::ostream *OS) {
  *OS << "XRayRecord { CPU = " << R.CPU
      << "; Type = " << RecordTypeAsString(R.Type) << "; FuncId = " << R.FuncId
      << "; TSC = " << R.TSC << "; TId = " << R.TId << "; PId = " << R.PId
      << " Args = " << ::testing::PrintToString(R.CallArgs) << " }";
}

void PrintTo(const Trace &T, std::ostream *OS) {
  const auto &H = T.getFileHeader();
  *OS << "XRay Trace:\nHeader: { Version = " << H.Version
      << "; Type = " << H.Type
      << "; ConstantTSC = " << ::testing::PrintToString(H.ConstantTSC)
      << "; NonstopTSC = " << ::testing::PrintToString(H.NonstopTSC)
      << "; CycleFrequency = " << H.CycleFrequency << "; FreeFormData = '"
      << ::testing::PrintToString(H.FreeFormData) << "' }\n";
  for (const auto &R : T) {
    PrintTo(R, OS);
    *OS << "\n";
  }
}

} // namespace xray
} // namespace llvm

namespace __xray {

std::string serialize(BufferQueue &Buffers, int32_t Version) {
  std::string Serialized;
  alignas(XRayFileHeader) std::byte HeaderStorage[sizeof(XRayFileHeader)];
  auto *Header = reinterpret_cast<XRayFileHeader *>(&HeaderStorage);
  new (Header) XRayFileHeader();
  Header->Version = Version;
  Header->Type = FileTypes::FDR_LOG;
  Header->CycleFrequency = 3e9;
  Header->ConstantTSC = 1;
  Header->NonstopTSC = 1;
  Serialized.append(reinterpret_cast<const char *>(&HeaderStorage),
                    sizeof(XRayFileHeader));
  Buffers.apply([&](const BufferQueue::Buffer &B) {
    auto Size = atomic_load_relaxed(B.Extents);
    auto Extents =
        createMetadataRecord<MetadataRecord::RecordKinds::BufferExtents>(Size);
    Serialized.append(reinterpret_cast<const char *>(&Extents),
                      sizeof(Extents));
    Serialized.append(reinterpret_cast<const char *>(B.Data), Size);
  });
  return Serialized;
}

} // namespace __xray
