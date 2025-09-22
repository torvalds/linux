//===- llvm/Remarks/RemarkStreamer.cpp - Remark Streamer -*- C++ --------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the main remark streamer.
//
//===----------------------------------------------------------------------===//

#include "llvm/Remarks/RemarkStreamer.h"
#include "llvm/Support/CommandLine.h"
#include <optional>

using namespace llvm;
using namespace llvm::remarks;

static cl::opt<cl::boolOrDefault> EnableRemarksSection(
    "remarks-section",
    cl::desc(
        "Emit a section containing remark diagnostics metadata. By default, "
        "this is enabled for the following formats: yaml-strtab, bitstream."),
    cl::init(cl::BOU_UNSET), cl::Hidden);

RemarkStreamer::RemarkStreamer(
    std::unique_ptr<remarks::RemarkSerializer> RemarkSerializer,
    std::optional<StringRef> FilenameIn)
    : RemarkSerializer(std::move(RemarkSerializer)),
      Filename(FilenameIn ? std::optional<std::string>(FilenameIn->str())
                          : std::nullopt) {}

Error RemarkStreamer::setFilter(StringRef Filter) {
  Regex R = Regex(Filter);
  std::string RegexError;
  if (!R.isValid(RegexError))
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             RegexError.data());
  PassFilter = std::move(R);
  return Error::success();
}

bool RemarkStreamer::matchesFilter(StringRef Str) {
  if (PassFilter)
    return PassFilter->match(Str);
  // No filter means all strings pass.
  return true;
}

bool RemarkStreamer::needsSection() const {
  if (EnableRemarksSection == cl::BOU_TRUE)
    return true;

  if (EnableRemarksSection == cl::BOU_FALSE)
    return false;

  assert(EnableRemarksSection == cl::BOU_UNSET);

  // We only need a section if we're in separate mode.
  if (RemarkSerializer->Mode != remarks::SerializerMode::Separate)
    return false;

  // Only some formats need a section:
  // * bitstream
  // * yaml-strtab
  switch (RemarkSerializer->SerializerFormat) {
  case remarks::Format::YAMLStrTab:
  case remarks::Format::Bitstream:
    return true;
  default:
    return false;
  }
}
