//===- RemarkSerializer.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides tools for serializing remarks.
//
//===----------------------------------------------------------------------===//

#include "llvm/Remarks/RemarkSerializer.h"
#include "llvm/Remarks/BitstreamRemarkSerializer.h"
#include "llvm/Remarks/YAMLRemarkSerializer.h"

using namespace llvm;
using namespace llvm::remarks;

Expected<std::unique_ptr<RemarkSerializer>>
remarks::createRemarkSerializer(Format RemarksFormat, SerializerMode Mode,
                                raw_ostream &OS) {
  switch (RemarksFormat) {
  case Format::Unknown:
    return createStringError(std::errc::invalid_argument,
                             "Unknown remark serializer format.");
  case Format::YAML:
    return std::make_unique<YAMLRemarkSerializer>(OS, Mode);
  case Format::YAMLStrTab:
    return std::make_unique<YAMLStrTabRemarkSerializer>(OS, Mode);
  case Format::Bitstream:
    return std::make_unique<BitstreamRemarkSerializer>(OS, Mode);
  }
  llvm_unreachable("Unknown remarks::Format enum");
}

Expected<std::unique_ptr<RemarkSerializer>>
remarks::createRemarkSerializer(Format RemarksFormat, SerializerMode Mode,
                                raw_ostream &OS, remarks::StringTable StrTab) {
  switch (RemarksFormat) {
  case Format::Unknown:
    return createStringError(std::errc::invalid_argument,
                             "Unknown remark serializer format.");
  case Format::YAML:
    return std::make_unique<YAMLRemarkSerializer>(OS, Mode, std::move(StrTab));
  case Format::YAMLStrTab:
    return std::make_unique<YAMLStrTabRemarkSerializer>(OS, Mode,
                                                        std::move(StrTab));
  case Format::Bitstream:
    return std::make_unique<BitstreamRemarkSerializer>(OS, Mode,
                                                       std::move(StrTab));
  }
  llvm_unreachable("Unknown remarks::Format enum");
}
