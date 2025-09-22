//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONDEPARCH_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONDEPARCH_H

#include "llvm/ADT/StringSwitch.h"
#include <optional>

namespace llvm {
namespace Hexagon {
enum class ArchEnum {
  NoArch,
  Generic,
  V5,
  V55,
  V60,
  V62,
  V65,
  V66,
  V67,
  V68,
  V69,
  V71,
  V73
};

inline std::optional<Hexagon::ArchEnum> getCpu(StringRef CPU) {
  return StringSwitch<std::optional<Hexagon::ArchEnum>>(CPU)
      .Case("generic", Hexagon::ArchEnum::V5)
      .Case("hexagonv5", Hexagon::ArchEnum::V5)
      .Case("hexagonv55", Hexagon::ArchEnum::V55)
      .Case("hexagonv60", Hexagon::ArchEnum::V60)
      .Case("hexagonv62", Hexagon::ArchEnum::V62)
      .Case("hexagonv65", Hexagon::ArchEnum::V65)
      .Case("hexagonv66", Hexagon::ArchEnum::V66)
      .Case("hexagonv67", Hexagon::ArchEnum::V67)
      .Case("hexagonv67t", Hexagon::ArchEnum::V67)
      .Case("hexagonv68", Hexagon::ArchEnum::V68)
      .Case("hexagonv69", Hexagon::ArchEnum::V69)
      .Case("hexagonv71", Hexagon::ArchEnum::V71)
      .Case("hexagonv71t", Hexagon::ArchEnum::V71)
      .Case("hexagonv73", Hexagon::ArchEnum::V73)
      .Default(std::nullopt);
}
} // namespace Hexagon
} // namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONDEPARCH_H
