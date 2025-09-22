//===- LoongArchMatInt.cpp - Immediate materialisation ---------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LoongArchMatInt.h"
#include "MCTargetDesc/LoongArchMCTargetDesc.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

LoongArchMatInt::InstSeq LoongArchMatInt::generateInstSeq(int64_t Val) {
  // Val:
  // |            hi32              |              lo32            |
  // +-----------+------------------+------------------+-----------+
  // | Highest12 |    Higher20      |       Hi20       |    Lo12   |
  // +-----------+------------------+------------------+-----------+
  // 63        52 51              32 31              12 11         0
  //
  const int64_t Highest12 = Val >> 52 & 0xFFF;
  const int64_t Higher20 = Val >> 32 & 0xFFFFF;
  const int64_t Hi20 = Val >> 12 & 0xFFFFF;
  const int64_t Lo12 = Val & 0xFFF;
  InstSeq Insts;

  if (Highest12 != 0 && SignExtend64<52>(Val) == 0) {
    Insts.push_back(Inst(LoongArch::LU52I_D, SignExtend64<12>(Highest12)));
    return Insts;
  }

  if (Hi20 == 0)
    Insts.push_back(Inst(LoongArch::ORI, Lo12));
  else if (SignExtend32<1>(Lo12 >> 11) == SignExtend32<20>(Hi20))
    Insts.push_back(Inst(LoongArch::ADDI_W, SignExtend64<12>(Lo12)));
  else {
    Insts.push_back(Inst(LoongArch::LU12I_W, SignExtend64<20>(Hi20)));
    if (Lo12 != 0)
      Insts.push_back(Inst(LoongArch::ORI, Lo12));
  }

  if (SignExtend32<1>(Hi20 >> 19) != SignExtend32<20>(Higher20))
    Insts.push_back(Inst(LoongArch::LU32I_D, SignExtend64<20>(Higher20)));

  if (SignExtend32<1>(Higher20 >> 19) != SignExtend32<12>(Highest12))
    Insts.push_back(Inst(LoongArch::LU52I_D, SignExtend64<12>(Highest12)));

  return Insts;
}
