//===-- ARMSystemRegister.td - ARM Register defs -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/TableGen/SearchableTable.td"

//===----------------------------------------------------------------------===//
//  Declarations that describe the ARM system-registers
//===----------------------------------------------------------------------===//

// M-Class System Registers.
// 'Mask' bits create unique keys for searches.
//
class MClassSysReg<bits<1> UniqMask1,
                   bits<1> UniqMask2,
                   bits<1> UniqMask3,
                   bits<12> Enc12,
                   string name> : SearchableTable {
  let SearchableFields = ["Name", "M1Encoding12", "M2M3Encoding8", "Encoding"];
  string Name;
  bits<13> M1Encoding12;
  bits<10> M2M3Encoding8;
  bits<12> Encoding;

  let Name = name;
  let EnumValueField = "M1Encoding12";
  let EnumValueField = "M2M3Encoding8";
  let EnumValueField = "Encoding";

  let M1Encoding12{12}    = UniqMask1;
  let M1Encoding12{11-00} = Enc12;
  let Encoding            = Enc12;

  let M2M3Encoding8{9}    = UniqMask2;
  let M2M3Encoding8{8}    = UniqMask3;
  let M2M3Encoding8{7-0}  = Enc12{7-0};
  code Requires           = [{ {} }];
}

// [|i|e|x]apsr_nzcvq has alias [|i|e|x]apsr.
//                 Mask1 Mask2 Mask3 Enc12, Name
let Requires = [{ {ARM::FeatureDSP} }] in {
def : MClassSysReg<0,    0,    0,    0x400, "apsr_g">;
def : MClassSysReg<0,    1,    1,    0xc00, "apsr_nzcvqg">;
def : MClassSysReg<0,    0,    0,    0x401, "iapsr_g">;
def : MClassSysReg<0,    1,    1,    0xc01, "iapsr_nzcvqg">;
def : MClassSysReg<0,    0,    0,    0x402, "eapsr_g">;
def : MClassSysReg<0,    1,    1,    0xc02, "eapsr_nzcvqg">;
def : MClassSysReg<0,    0,    0,    0x403, "xpsr_g">;
def : MClassSysReg<0,    1,    1,    0xc03, "xpsr_nzcvqg">;
}

def : MClassSysReg<0,    0,    1,    0x800, "apsr">;
def : MClassSysReg<1,    1,    0,    0x800, "apsr_nzcvq">;
def : MClassSysReg<0,    0,    1,    0x801, "iapsr">;
def : MClassSysReg<1,    1,    0,    0x801, "iapsr_nzcvq">;
def : MClassSysReg<0,    0,    1,    0x802, "eapsr">;
def : MClassSysReg<1,    1,    0,    0x802, "eapsr_nzcvq">;
def : MClassSysReg<0,    0,    1,    0x803, "xpsr">;
def : MClassSysReg<1,    1,    0,    0x803, "xpsr_nzcvq">;

def : MClassSysReg<0,    0,    1,    0x805, "ipsr">;
def : MClassSysReg<0,    0,    1,    0x806, "epsr">;
def : MClassSysReg<0,    0,    1,    0x807, "iepsr">;
def : MClassSysReg<0,    0,    1,    0x808, "msp">;
def : MClassSysReg<0,    0,    1,    0x809, "psp">;

let Requires = [{ {ARM::HasV8MBaselineOps} }] in {
def : MClassSysReg<0,    0,    1,    0x80a, "msplim">;
def : MClassSysReg<0,    0,    1,    0x80b, "psplim">;
}

def : MClassSysReg<0,    0,    1,    0x810, "primask">;

let Requires = [{ {ARM::HasV7Ops} }] in {
def : MClassSysReg<0,    0,    1,    0x811, "basepri">;
def : MClassSysReg<0,    0,    1,    0x812, "basepri_max">;
def : MClassSysReg<0,    0,    1,    0x813, "faultmask">;
}

def : MClassSysReg<0,    0,    1,    0x814, "control">;

let Requires = [{ {ARM::Feature8MSecExt} }] in {
def : MClassSysReg<0,    0,    1,    0x888, "msp_ns">;
def : MClassSysReg<0,    0,    1,    0x889, "psp_ns">;
}

let Requires = [{ {ARM::Feature8MSecExt, ARM::HasV8MBaselineOps} }] in {
def : MClassSysReg<0,    0,    1,    0x88a, "msplim_ns">;
def : MClassSysReg<0,    0,    1,    0x88b, "psplim_ns">;
}

def : MClassSysReg<0,    0,    1,    0x890, "primask_ns">;

let Requires = [{ {ARM::Feature8MSecExt, ARM::HasV7Ops} }] in {
def : MClassSysReg<0,    0,    1,    0x891, "basepri_ns">;
def : MClassSysReg<0,    0,    1,    0x893, "faultmask_ns">;
}

let Requires = [{ {ARM::Feature8MSecExt} }] in {
def : MClassSysReg<0,    0,    1,    0x894, "control_ns">;
def : MClassSysReg<0,    0,    1,    0x898, "sp_ns">;
}

let Requires = [{ {ARM::FeaturePACBTI} }] in {
def : MClassSysReg<0,    0,    1,    0x820, "pac_key_p_0">;
def : MClassSysReg<0,    0,    1,    0x821, "pac_key_p_1">;
def : MClassSysReg<0,    0,    1,    0x822, "pac_key_p_2">;
def : MClassSysReg<0,    0,    1,    0x823, "pac_key_p_3">;
def : MClassSysReg<0,    0,    1,    0x824, "pac_key_u_0">;
def : MClassSysReg<0,    0,    1,    0x825, "pac_key_u_1">;
def : MClassSysReg<0,    0,    1,    0x826, "pac_key_u_2">;
def : MClassSysReg<0,    0,    1,    0x827, "pac_key_u_3">;
def : MClassSysReg<0,    0,    1,    0x8a0, "pac_key_p_0_ns">;
def : MClassSysReg<0,    0,    1,    0x8a1, "pac_key_p_1_ns">;
def : MClassSysReg<0,    0,    1,    0x8a2, "pac_key_p_2_ns">;
def : MClassSysReg<0,    0,    1,    0x8a3, "pac_key_p_3_ns">;
def : MClassSysReg<0,    0,    1,    0x8a4, "pac_key_u_0_ns">;
def : MClassSysReg<0,    0,    1,    0x8a5, "pac_key_u_1_ns">;
def : MClassSysReg<0,    0,    1,    0x8a6, "pac_key_u_2_ns">;
def : MClassSysReg<0,    0,    1,    0x8a7, "pac_key_u_3_ns">;
}

// Banked Registers
//
class BankedReg<string name,  bits<8> enc>
               : SearchableTable {
  string Name;
  bits<8> Encoding;
  let Name = name;
  let Encoding = enc;
  let SearchableFields = ["Name", "Encoding"];
}

// The values here come from B9.2.3 of the ARM ARM, where bits 4-0 are SysM
// and bit 5 is R.
def : BankedReg<"r8_usr",   0x00>;
def : BankedReg<"r9_usr",   0x01>;
def : BankedReg<"r10_usr",  0x02>;
def : BankedReg<"r11_usr",  0x03>;
def : BankedReg<"r12_usr",  0x04>;
def : BankedReg<"sp_usr",   0x05>;
def : BankedReg<"lr_usr",   0x06>;
def : BankedReg<"r8_fiq",   0x08>;
def : BankedReg<"r9_fiq",   0x09>;
def : BankedReg<"r10_fiq",  0x0a>;
def : BankedReg<"r11_fiq",  0x0b>;
def : BankedReg<"r12_fiq",  0x0c>;
def : BankedReg<"sp_fiq",   0x0d>;
def : BankedReg<"lr_fiq",   0x0e>;
def : BankedReg<"lr_irq",   0x10>;
def : BankedReg<"sp_irq",   0x11>;
def : BankedReg<"lr_svc",   0x12>;
def : BankedReg<"sp_svc",   0x13>;
def : BankedReg<"lr_abt",   0x14>;
def : BankedReg<"sp_abt",   0x15>;
def : BankedReg<"lr_und",   0x16>;
def : BankedReg<"sp_und",   0x17>;
def : BankedReg<"lr_mon",   0x1c>;
def : BankedReg<"sp_mon",   0x1d>;
def : BankedReg<"elr_hyp",  0x1e>;
def : BankedReg<"sp_hyp",   0x1f>;
def : BankedReg<"spsr_fiq", 0x2e>;
def : BankedReg<"spsr_irq", 0x30>;
def : BankedReg<"spsr_svc", 0x32>;
def : BankedReg<"spsr_abt", 0x34>;
def : BankedReg<"spsr_und", 0x36>;
def : BankedReg<"spsr_mon", 0x3c>;
def : BankedReg<"spsr_hyp", 0x3e>;
