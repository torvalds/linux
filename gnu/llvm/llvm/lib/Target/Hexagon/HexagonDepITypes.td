//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Automatically generated file, do not edit!
//===----------------------------------------------------------------------===//

class IType<bits<7> t> { bits<7> Value = t; }
def TypeALU32_2op : IType<0>;
def TypeALU32_3op : IType<1>;
def TypeALU32_ADDI : IType<2>;
def TypeALU64 : IType<3>;
def TypeCJ : IType<4>;
def TypeCR : IType<5>;
def TypeCVI_4SLOT_MPY : IType<6>;
def TypeCVI_GATHER : IType<7>;
def TypeCVI_GATHER_DV : IType<8>;
def TypeCVI_GATHER_RST : IType<9>;
def TypeCVI_HIST : IType<10>;
def TypeCVI_SCATTER : IType<11>;
def TypeCVI_SCATTER_DV : IType<12>;
def TypeCVI_SCATTER_NEW_RST : IType<13>;
def TypeCVI_SCATTER_NEW_ST : IType<14>;
def TypeCVI_SCATTER_RST : IType<15>;
def TypeCVI_VA : IType<16>;
def TypeCVI_VA_DV : IType<17>;
def TypeCVI_VM_LD : IType<18>;
def TypeCVI_VM_NEW_ST : IType<19>;
def TypeCVI_VM_ST : IType<20>;
def TypeCVI_VM_STU : IType<21>;
def TypeCVI_VM_TMP_LD : IType<22>;
def TypeCVI_VM_VP_LDU : IType<23>;
def TypeCVI_VP : IType<24>;
def TypeCVI_VP_VS : IType<25>;
def TypeCVI_VS : IType<26>;
def TypeCVI_VS_VX : IType<27>;
def TypeCVI_VX : IType<28>;
def TypeCVI_VX_DV : IType<29>;
def TypeCVI_VX_LATE : IType<30>;
def TypeCVI_ZW : IType<31>;
def TypeDUPLEX : IType<32>;
def TypeENDLOOP : IType<33>;
def TypeEXTENDER : IType<34>;
def TypeJ : IType<35>;
def TypeLD : IType<36>;
def TypeM : IType<37>;
def TypeMAPPING : IType<38>;
def TypeNCJ : IType<39>;
def TypePSEUDO : IType<40>;
def TypeST : IType<41>;
def TypeSUBINSN : IType<42>;
def TypeS_2op : IType<43>;
def TypeS_3op : IType<44>;
def TypeV2LDST : IType<47>;
def TypeV4LDST : IType<48>;
