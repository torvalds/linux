//===-- LoongArchTargetParser - Parser for LoongArch features --*- C++ -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise LoongArch hardware features
// such as CPU/ARCH and extension names.
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/LoongArchTargetParser.h"

using namespace llvm;
using namespace llvm::LoongArch;

const FeatureInfo AllFeatures[] = {
#define LOONGARCH_FEATURE(NAME, KIND) {NAME, KIND},
#include "llvm/TargetParser/LoongArchTargetParser.def"
};

const ArchInfo AllArchs[] = {
#define LOONGARCH_ARCH(NAME, KIND, FEATURES)                                   \
  {NAME, LoongArch::ArchKind::KIND, FEATURES},
#include "llvm/TargetParser/LoongArchTargetParser.def"
};

bool LoongArch::isValidArchName(StringRef Arch) {
  for (const auto A : AllArchs)
    if (A.Name == Arch)
      return true;
  return false;
}

bool LoongArch::getArchFeatures(StringRef Arch,
                                std::vector<StringRef> &Features) {
  for (const auto A : AllArchs) {
    if (A.Name == Arch) {
      for (const auto F : AllFeatures)
        if ((A.Features & F.Kind) == F.Kind)
          Features.push_back(F.Name);
      return true;
    }
  }

  if (Arch == "la64v1.0" || Arch == "la64v1.1") {
    Features.push_back("+64bit");
    Features.push_back("+d");
    Features.push_back("+lsx");
    Features.push_back("+ual");
    if (Arch == "la64v1.1")
      Features.push_back("+frecipe");
    return true;
  }

  return false;
}

bool LoongArch::isValidCPUName(StringRef Name) { return isValidArchName(Name); }

void LoongArch::fillValidCPUList(SmallVectorImpl<StringRef> &Values) {
  for (const auto A : AllArchs)
    Values.emplace_back(A.Name);
}

StringRef LoongArch::getDefaultArch(bool Is64Bit) {
  // TODO: use a real 32-bit arch name.
  return Is64Bit ? "loongarch64" : "";
}
