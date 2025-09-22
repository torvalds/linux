//===- FrontendOptions.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/FrontendOptions.h"
#include "clang/Basic/LangStandard.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;

InputKind FrontendOptions::getInputKindForExtension(StringRef Extension) {
  return llvm::StringSwitch<InputKind>(Extension)
      .Cases("ast", "pcm", InputKind(Language::Unknown, InputKind::Precompiled))
      .Case("c", Language::C)
      .Cases("S", "s", Language::Asm)
      .Case("i", InputKind(Language::C).getPreprocessed())
      .Case("ii", InputKind(Language::CXX).getPreprocessed())
      .Case("cui", InputKind(Language::CUDA).getPreprocessed())
      .Case("m", Language::ObjC)
      .Case("mi", InputKind(Language::ObjC).getPreprocessed())
      .Cases("mm", "M", Language::ObjCXX)
      .Case("mii", InputKind(Language::ObjCXX).getPreprocessed())
      .Cases("C", "cc", "cp", Language::CXX)
      .Cases("cpp", "CPP", "c++", "cxx", "hpp", "hxx", Language::CXX)
      .Case("cppm", Language::CXX)
      .Cases("iim", "iih", InputKind(Language::CXX).getPreprocessed())
      .Case("cl", Language::OpenCL)
      .Case("clcpp", Language::OpenCLCXX)
      .Cases("cu", "cuh", Language::CUDA)
      .Case("hip", Language::HIP)
      .Cases("ll", "bc", Language::LLVM_IR)
      .Case("hlsl", Language::HLSL)
      .Case("cir", Language::CIR)
      .Default(Language::Unknown);
}
