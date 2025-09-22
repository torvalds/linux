//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Automatically generated file, do not edit!
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONDEPTIMINGCLASSES_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONDEPTIMINGCLASSES_H

#include "HexagonInstrInfo.h"

namespace llvm {

inline bool is_TC1(unsigned SchedClass) {
  switch (SchedClass) {
  case Hexagon::Sched::tc_112d30d6:
  case Hexagon::Sched::tc_151bf368:
  case Hexagon::Sched::tc_1c2c7a4a:
  case Hexagon::Sched::tc_1d41f8b7:
  case Hexagon::Sched::tc_23708a21:
  case Hexagon::Sched::tc_24f426ab:
  case Hexagon::Sched::tc_2f573607:
  case Hexagon::Sched::tc_388f9897:
  case Hexagon::Sched::tc_3d14a17b:
  case Hexagon::Sched::tc_3fbf1042:
  case Hexagon::Sched::tc_407e96f9:
  case Hexagon::Sched::tc_42ff66ba:
  case Hexagon::Sched::tc_4a55d03c:
  case Hexagon::Sched::tc_5502c366:
  case Hexagon::Sched::tc_55b33fda:
  case Hexagon::Sched::tc_56a124a7:
  case Hexagon::Sched::tc_57a55b54:
  case Hexagon::Sched::tc_59a7822c:
  case Hexagon::Sched::tc_5b347363:
  case Hexagon::Sched::tc_5da50c4b:
  case Hexagon::Sched::tc_60e324ff:
  case Hexagon::Sched::tc_651cbe02:
  case Hexagon::Sched::tc_6fc5dbea:
  case Hexagon::Sched::tc_711c805f:
  case Hexagon::Sched::tc_713b66bf:
  case Hexagon::Sched::tc_9124c04f:
  case Hexagon::Sched::tc_9c52f549:
  case Hexagon::Sched::tc_9e27f2f9:
  case Hexagon::Sched::tc_9f6cd987:
  case Hexagon::Sched::tc_a1297125:
  case Hexagon::Sched::tc_a7a13fac:
  case Hexagon::Sched::tc_b837298f:
  case Hexagon::Sched::tc_c57d9f39:
  case Hexagon::Sched::tc_d33e5eee:
  case Hexagon::Sched::tc_decdde8a:
  case Hexagon::Sched::tc_e60def48:
  case Hexagon::Sched::tc_ed03645c:
  case Hexagon::Sched::tc_eeda4109:
  case Hexagon::Sched::tc_ef921005:
  case Hexagon::Sched::tc_f97707c1:
  case Hexagon::Sched::tc_f999c66e:
    return true;
  default:
    return false;
  }
}

inline bool is_TC2(unsigned SchedClass) {
  switch (SchedClass) {
  case Hexagon::Sched::tc_01d44cb2:
  case Hexagon::Sched::tc_0dfac0a7:
  case Hexagon::Sched::tc_1fcb8495:
  case Hexagon::Sched::tc_20131976:
  case Hexagon::Sched::tc_2c13e7f5:
  case Hexagon::Sched::tc_3edca78f:
  case Hexagon::Sched::tc_5e4cf0e8:
  case Hexagon::Sched::tc_65279839:
  case Hexagon::Sched::tc_7401744f:
  case Hexagon::Sched::tc_84a7500d:
  case Hexagon::Sched::tc_8a825db2:
  case Hexagon::Sched::tc_8b5bd4f5:
  case Hexagon::Sched::tc_95a33176:
  case Hexagon::Sched::tc_9b3c0462:
  case Hexagon::Sched::tc_a08b630b:
  case Hexagon::Sched::tc_a4e22bbd:
  case Hexagon::Sched::tc_a7bdb22c:
  case Hexagon::Sched::tc_bb831a7c:
  case Hexagon::Sched::tc_c20701f0:
  case Hexagon::Sched::tc_d3632d88:
  case Hexagon::Sched::tc_d61dfdc3:
  case Hexagon::Sched::tc_e3d699e3:
  case Hexagon::Sched::tc_f098b237:
  case Hexagon::Sched::tc_f34c1c21:
    return true;
  default:
    return false;
  }
}

inline bool is_TC2early(unsigned SchedClass) {
  switch (SchedClass) {
  case Hexagon::Sched::tc_33e7e673:
  case Hexagon::Sched::tc_45f9d1be:
  case Hexagon::Sched::tc_a4ee89db:
    return true;
  default:
    return false;
  }
}

inline bool is_TC3x(unsigned SchedClass) {
  switch (SchedClass) {
  case Hexagon::Sched::tc_01e1be3b:
  case Hexagon::Sched::tc_1248597c:
  case Hexagon::Sched::tc_197dce51:
  case Hexagon::Sched::tc_27106296:
  case Hexagon::Sched::tc_28e55c6f:
  case Hexagon::Sched::tc_2c3e17fc:
  case Hexagon::Sched::tc_38382228:
  case Hexagon::Sched::tc_38e0bae9:
  case Hexagon::Sched::tc_4abdbdc6:
  case Hexagon::Sched::tc_503ce0f3:
  case Hexagon::Sched::tc_556f6577:
  case Hexagon::Sched::tc_5a4b5e58:
  case Hexagon::Sched::tc_6ae3426b:
  case Hexagon::Sched::tc_6d861a95:
  case Hexagon::Sched::tc_788b1d09:
  case Hexagon::Sched::tc_7dc63b5c:
  case Hexagon::Sched::tc_7f8ae742:
  case Hexagon::Sched::tc_9406230a:
  case Hexagon::Sched::tc_a154b476:
  case Hexagon::Sched::tc_a38c45dc:
  case Hexagon::Sched::tc_c21d7447:
  case Hexagon::Sched::tc_d7718fbe:
  case Hexagon::Sched::tc_db596beb:
  case Hexagon::Sched::tc_f0cdeccf:
  case Hexagon::Sched::tc_fae9dfa5:
    return true;
  default:
    return false;
  }
}

inline bool is_TC4x(unsigned SchedClass) {
  switch (SchedClass) {
  case Hexagon::Sched::tc_02fe1c65:
  case Hexagon::Sched::tc_0a195f2c:
  case Hexagon::Sched::tc_7f7f45f5:
  case Hexagon::Sched::tc_9783714b:
  case Hexagon::Sched::tc_9e72dc89:
  case Hexagon::Sched::tc_9edb7c77:
  case Hexagon::Sched::tc_f0e8e832:
  case Hexagon::Sched::tc_f7569068:
    return true;
  default:
    return false;
  }
}
} // namespace llvm

#endif  // LLVM_LIB_TARGET_HEXAGON_HEXAGONDEPTIMINGCLASSES_H
