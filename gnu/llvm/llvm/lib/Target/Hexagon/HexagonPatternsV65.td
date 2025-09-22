//==- HexagonPatternsV65.td -------------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

multiclass vgathermh<RegisterClass RC> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1,
  mayStore = 1, addrMode = BaseImmOffset, accessSize = HalfWordAccess in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins IntRegs:$_dst_, s4_0Imm:$Ii,
                                IntRegs:$Rt, ModRegs:$Mu, RC:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

multiclass vgathermw<RegisterClass RC> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1,
  mayStore = 1, addrMode = BaseImmOffset, accessSize = WordAccess in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins IntRegs:$_dst_, s4_0Imm:$Ii,
                                IntRegs:$Rt, ModRegs:$Mu, RC:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

multiclass vgathermhw<RegisterClass RC> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1,
   mayStore = 1, addrMode = BaseImmOffset, accessSize = HalfWordAccess in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins IntRegs:$_dst_, s4_0Imm:$Ii,
                                IntRegs:$Rt, ModRegs:$Mu, RC:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

defm V6_vgathermh_pseudo  : vgathermh<HvxVR>;
defm V6_vgathermw_pseudo  : vgathermw<HvxVR>;
defm V6_vgathermhw_pseudo  : vgathermhw<HvxWR>;

multiclass vgathermhq<RegisterClass RC1, RegisterClass RC2> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1,
  mayStore = 1, addrMode = BaseImmOffset, accessSize = HalfWordAccess in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins IntRegs:$_dst_, s4_0Imm:$Ii,
                                RC2:$Vq, IntRegs:$Rt, ModRegs:$Mu,
                                RC1:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

multiclass vgathermwq<RegisterClass RC1, RegisterClass RC2> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1,
  mayStore = 1, addrMode = BaseImmOffset, accessSize = WordAccess in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins IntRegs:$_dst_, s4_0Imm:$Ii,
                                RC2:$Vq, IntRegs:$Rt, ModRegs:$Mu,
                                RC1:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

multiclass vgathermhwq<RegisterClass RC1, RegisterClass RC2> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1,
  mayStore = 1, addrMode = BaseImmOffset, accessSize = HalfWordAccess  in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins IntRegs:$_dst_, s4_0Imm:$Ii,
                              RC2:$Vq, IntRegs:$Rt, ModRegs:$Mu,
                              RC1:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

defm V6_vgathermhq_pseudo  : vgathermhq<HvxVR, HvxQR>;
defm V6_vgathermwq_pseudo  : vgathermwq<HvxVR, HvxQR>;
defm V6_vgathermhwq_pseudo  : vgathermhwq<HvxWR, HvxQR>;
