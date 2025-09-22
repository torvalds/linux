//===- HLSLRuntime.h - HLSL Runtime -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines helper utilities for supporting the HLSL runtime environment.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_BASIC_HLSLRUNTIME_H
#define CLANG_BASIC_HLSLRUNTIME_H

#include "clang/Basic/LangOptions.h"
#include <cstdint>

namespace clang {
namespace hlsl {

constexpr ShaderStage
getStageFromEnvironment(const llvm::Triple::EnvironmentType &E) {
  uint32_t Pipeline =
      static_cast<uint32_t>(E) - static_cast<uint32_t>(llvm::Triple::Pixel);

  if (Pipeline > (uint32_t)ShaderStage::Invalid)
    return ShaderStage::Invalid;
  return static_cast<ShaderStage>(Pipeline);
}

#define ENUM_COMPARE_ASSERT(Value)                                             \
  static_assert(                                                               \
      getStageFromEnvironment(llvm::Triple::Value) == ShaderStage::Value,      \
      "Mismatch between llvm::Triple and clang::ShaderStage for " #Value);

ENUM_COMPARE_ASSERT(Pixel)
ENUM_COMPARE_ASSERT(Vertex)
ENUM_COMPARE_ASSERT(Geometry)
ENUM_COMPARE_ASSERT(Hull)
ENUM_COMPARE_ASSERT(Domain)
ENUM_COMPARE_ASSERT(Compute)
ENUM_COMPARE_ASSERT(Library)
ENUM_COMPARE_ASSERT(RayGeneration)
ENUM_COMPARE_ASSERT(Intersection)
ENUM_COMPARE_ASSERT(AnyHit)
ENUM_COMPARE_ASSERT(ClosestHit)
ENUM_COMPARE_ASSERT(Miss)
ENUM_COMPARE_ASSERT(Callable)
ENUM_COMPARE_ASSERT(Mesh)
ENUM_COMPARE_ASSERT(Amplification)

static_assert(getStageFromEnvironment(llvm::Triple::UnknownEnvironment) ==
                  ShaderStage::Invalid,
              "Mismatch between llvm::Triple and "
              "clang::ShaderStage for Invalid");
static_assert(getStageFromEnvironment(llvm::Triple::MSVC) ==
                  ShaderStage::Invalid,
              "Mismatch between llvm::Triple and "
              "clang::ShaderStage for Invalid");

} // namespace hlsl
} // namespace clang

#endif // CLANG_BASIC_HLSLRUNTIME_H
