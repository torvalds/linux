//===--- arm_sve.td - ARM SVE compiler interface ------------------------===//
//
//  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
//  See https://llvm.org/LICENSE.txt for license information.
//  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TableGen definitions from which the ARM SVE header
//  file will be generated.  See:
//
//      https://developer.arm.com/architectures/system-architectures/software-standards/acle
//
//===----------------------------------------------------------------------===//

include "arm_sve_sme_incl.td"

////////////////////////////////////////////////////////////////////////////////
// Loads

// Load one vector (scalar base)
def SVLD1   : MInst<"svld1[_{2}]", "dPc", "csilUcUsUiUlhfd", [IsLoad, VerifyRuntimeMode],               MemEltTyDefault, "aarch64_sve_ld1">;
def SVLD1SB : MInst<"svld1sb_{d}", "dPS", "silUsUiUl",       [IsLoad, VerifyRuntimeMode],               MemEltTyInt8,    "aarch64_sve_ld1">;
def SVLD1UB : MInst<"svld1ub_{d}", "dPW", "silUsUiUl",       [IsLoad, IsZExtReturn, VerifyRuntimeMode], MemEltTyInt8,    "aarch64_sve_ld1">;
def SVLD1SH : MInst<"svld1sh_{d}", "dPT", "ilUiUl",          [IsLoad, VerifyRuntimeMode],               MemEltTyInt16,   "aarch64_sve_ld1">;
def SVLD1UH : MInst<"svld1uh_{d}", "dPX", "ilUiUl",          [IsLoad, IsZExtReturn, VerifyRuntimeMode], MemEltTyInt16,   "aarch64_sve_ld1">;
def SVLD1SW : MInst<"svld1sw_{d}", "dPU", "lUl",             [IsLoad, VerifyRuntimeMode],               MemEltTyInt32,   "aarch64_sve_ld1">;
def SVLD1UW : MInst<"svld1uw_{d}", "dPY", "lUl",             [IsLoad, IsZExtReturn, VerifyRuntimeMode], MemEltTyInt32,   "aarch64_sve_ld1">;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  def SVLD1_BF      : MInst<"svld1[_{2}]",      "dPc",  "b", [IsLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_ld1">;
  def SVLD1_VNUM_BF : MInst<"svld1_vnum[_{2}]", "dPcl", "b", [IsLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_ld1">;
}

// Load one vector (scalar base, VL displacement)
def SVLD1_VNUM   : MInst<"svld1_vnum[_{2}]", "dPcl", "csilUcUsUiUlhfd", [IsLoad, VerifyRuntimeMode],               MemEltTyDefault, "aarch64_sve_ld1">;
def SVLD1SB_VNUM : MInst<"svld1sb_vnum_{d}", "dPSl", "silUsUiUl",       [IsLoad, VerifyRuntimeMode],               MemEltTyInt8,    "aarch64_sve_ld1">;
def SVLD1UB_VNUM : MInst<"svld1ub_vnum_{d}", "dPWl", "silUsUiUl",       [IsLoad, IsZExtReturn, VerifyRuntimeMode], MemEltTyInt8,    "aarch64_sve_ld1">;
def SVLD1SH_VNUM : MInst<"svld1sh_vnum_{d}", "dPTl", "ilUiUl",          [IsLoad, VerifyRuntimeMode],               MemEltTyInt16,   "aarch64_sve_ld1">;
def SVLD1UH_VNUM : MInst<"svld1uh_vnum_{d}", "dPXl", "ilUiUl",          [IsLoad, IsZExtReturn, VerifyRuntimeMode], MemEltTyInt16,   "aarch64_sve_ld1">;
def SVLD1SW_VNUM : MInst<"svld1sw_vnum_{d}", "dPUl", "lUl",             [IsLoad, VerifyRuntimeMode],               MemEltTyInt32,   "aarch64_sve_ld1">;
def SVLD1UW_VNUM : MInst<"svld1uw_vnum_{d}", "dPYl", "lUl",             [IsLoad, IsZExtReturn, VerifyRuntimeMode], MemEltTyInt32,   "aarch64_sve_ld1">;

let SVETargetGuard = "sve", SMETargetGuard = InvalidMode in {
// Load one vector (vector base)
def SVLD1_GATHER_BASES_U   : MInst<"svld1_gather[_{2}base]_{d}",   "dPu", "ilUiUlfd", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1SB_GATHER_BASES_U : MInst<"svld1sb_gather[_{2}base]_{d}", "dPu", "ilUiUl",   [IsGatherLoad],               MemEltTyInt8,    "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1UB_GATHER_BASES_U : MInst<"svld1ub_gather[_{2}base]_{d}", "dPu", "ilUiUl",   [IsGatherLoad, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1SH_GATHER_BASES_U : MInst<"svld1sh_gather[_{2}base]_{d}", "dPu", "ilUiUl",   [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1UH_GATHER_BASES_U : MInst<"svld1uh_gather[_{2}base]_{d}", "dPu", "ilUiUl",   [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1SW_GATHER_BASES_U : MInst<"svld1sw_gather[_{2}base]_{d}", "dPu", "lUl",      [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1UW_GATHER_BASES_U : MInst<"svld1uw_gather[_{2}base]_{d}", "dPu", "lUl",      [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ld1_gather_scalar_offset">;

// Load one vector (scalar base, signed vector offset in bytes)
def SVLD1_GATHER_64B_OFFSETS_S   : MInst<"svld1_gather_[{3}]offset[_{d}]", "dPcx", "lUld", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ld1_gather">;
def SVLD1SB_GATHER_64B_OFFSETS_S : MInst<"svld1sb_gather_[{3}]offset_{d}", "dPSx", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ld1_gather">;
def SVLD1UB_GATHER_64B_OFFSETS_S : MInst<"svld1ub_gather_[{3}]offset_{d}", "dPWx", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ld1_gather">;
def SVLD1SH_GATHER_64B_OFFSETS_S : MInst<"svld1sh_gather_[{3}]offset_{d}", "dPTx", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ld1_gather">;
def SVLD1UH_GATHER_64B_OFFSETS_S : MInst<"svld1uh_gather_[{3}]offset_{d}", "dPXx", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather">;
def SVLD1SW_GATHER_64B_OFFSETS_S : MInst<"svld1sw_gather_[{3}]offset_{d}", "dPUx", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt32,   "aarch64_sve_ld1_gather">;
def SVLD1UW_GATHER_64B_OFFSETS_S : MInst<"svld1uw_gather_[{3}]offset_{d}", "dPYx", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ld1_gather">;

def SVLD1_GATHER_32B_OFFSETS_S   : MInst<"svld1_gather_[{3}]offset[_{d}]", "dPcx", "iUif", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ld1_gather_sxtw">;
def SVLD1SB_GATHER_32B_OFFSETS_S : MInst<"svld1sb_gather_[{3}]offset_{d}", "dPSx", "iUi",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ld1_gather_sxtw">;
def SVLD1UB_GATHER_32B_OFFSETS_S : MInst<"svld1ub_gather_[{3}]offset_{d}", "dPWx", "iUi",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ld1_gather_sxtw">;
def SVLD1SH_GATHER_32B_OFFSETS_S : MInst<"svld1sh_gather_[{3}]offset_{d}", "dPTx", "iUi",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ld1_gather_sxtw">;
def SVLD1UH_GATHER_32B_OFFSETS_S : MInst<"svld1uh_gather_[{3}]offset_{d}", "dPXx", "iUi",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather_sxtw">;

// Load one vector (scalar base, unsigned vector offset in bytes)
def SVLD1_GATHER_64B_OFFSETS_U   : MInst<"svld1_gather_[{3}]offset[_{d}]", "dPcu", "lUld", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ld1_gather">;
def SVLD1SB_GATHER_64B_OFFSETS_U : MInst<"svld1sb_gather_[{3}]offset_{d}", "dPSu", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ld1_gather">;
def SVLD1UB_GATHER_64B_OFFSETS_U : MInst<"svld1ub_gather_[{3}]offset_{d}", "dPWu", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ld1_gather">;
def SVLD1SH_GATHER_64B_OFFSETS_U : MInst<"svld1sh_gather_[{3}]offset_{d}", "dPTu", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ld1_gather">;
def SVLD1UH_GATHER_64B_OFFSETS_U : MInst<"svld1uh_gather_[{3}]offset_{d}", "dPXu", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather">;
def SVLD1SW_GATHER_64B_OFFSETS_U : MInst<"svld1sw_gather_[{3}]offset_{d}", "dPUu", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt32,   "aarch64_sve_ld1_gather">;
def SVLD1UW_GATHER_64B_OFFSETS_U : MInst<"svld1uw_gather_[{3}]offset_{d}", "dPYu", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ld1_gather">;

def SVLD1_GATHER_32B_OFFSETS_U   : MInst<"svld1_gather_[{3}]offset[_{d}]", "dPcu", "iUif", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ld1_gather_uxtw">;
def SVLD1SB_GATHER_32B_OFFSETS_U : MInst<"svld1sb_gather_[{3}]offset_{d}", "dPSu", "iUi",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ld1_gather_uxtw">;
def SVLD1UB_GATHER_32B_OFFSETS_U : MInst<"svld1ub_gather_[{3}]offset_{d}", "dPWu", "iUi",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ld1_gather_uxtw">;
def SVLD1SH_GATHER_32B_OFFSETS_U : MInst<"svld1sh_gather_[{3}]offset_{d}", "dPTu", "iUi",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ld1_gather_uxtw">;
def SVLD1UH_GATHER_32B_OFFSETS_U : MInst<"svld1uh_gather_[{3}]offset_{d}", "dPXu", "iUi",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather_uxtw">;

// Load one vector (vector base, signed scalar offset in bytes)
def SVLD1_GATHER_OFFSET_S   : MInst<"svld1_gather[_{2}base]_offset_{d}",   "dPul", "ilUiUlfd", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1SB_GATHER_OFFSET_S : MInst<"svld1sb_gather[_{2}base]_offset_{d}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1UB_GATHER_OFFSET_S : MInst<"svld1ub_gather[_{2}base]_offset_{d}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1SH_GATHER_OFFSET_S : MInst<"svld1sh_gather[_{2}base]_offset_{d}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1UH_GATHER_OFFSET_S : MInst<"svld1uh_gather[_{2}base]_offset_{d}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1SW_GATHER_OFFSET_S : MInst<"svld1sw_gather[_{2}base]_offset_{d}", "dPul", "lUl",      [IsGatherLoad, IsByteIndexed],               MemEltTyInt32,   "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1UW_GATHER_OFFSET_S : MInst<"svld1uw_gather[_{2}base]_offset_{d}", "dPul", "lUl",      [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ld1_gather_scalar_offset">;

// Load one vector (scalar base, signed vector index)
def SVLD1_GATHER_64B_INDICES_S   : MInst<"svld1_gather_[{3}]index[_{d}]", "dPcx", "lUld", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ld1_gather_index">;
def SVLD1SH_GATHER_64B_INDICES_S : MInst<"svld1sh_gather_[{3}]index_{d}", "dPTx", "lUl",  [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ld1_gather_index">;
def SVLD1UH_GATHER_64B_INDICES_S : MInst<"svld1uh_gather_[{3}]index_{d}", "dPXx", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather_index">;
def SVLD1SW_GATHER_64B_INDICES_S : MInst<"svld1sw_gather_[{3}]index_{d}", "dPUx", "lUl",  [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ld1_gather_index">;
def SVLD1UW_GATHER_64B_INDICES_S : MInst<"svld1uw_gather_[{3}]index_{d}", "dPYx", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ld1_gather_index">;

def SVLD1_GATHER_32B_INDICES_S   : MInst<"svld1_gather_[{3}]index[_{d}]", "dPcx", "iUif", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ld1_gather_sxtw_index">;
def SVLD1SH_GATHER_32B_INDICES_S : MInst<"svld1sh_gather_[{3}]index_{d}", "dPTx", "iUi",  [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ld1_gather_sxtw_index">;
def SVLD1UH_GATHER_32B_INDICES_S : MInst<"svld1uh_gather_[{3}]index_{d}", "dPXx", "iUi",  [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather_sxtw_index">;

// Load one vector (scalar base, unsigned vector index)
def SVLD1_GATHER_64B_INDICES_U   : MInst<"svld1_gather_[{3}]index[_{d}]", "dPcu", "lUld", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ld1_gather_index">;
def SVLD1SH_GATHER_64B_INDICES_U : MInst<"svld1sh_gather_[{3}]index_{d}", "dPTu", "lUl",  [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ld1_gather_index">;
def SVLD1UH_GATHER_64B_INDICES_U : MInst<"svld1uh_gather_[{3}]index_{d}", "dPXu", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather_index">;
def SVLD1SW_GATHER_64B_INDICES_U : MInst<"svld1sw_gather_[{3}]index_{d}", "dPUu", "lUl",  [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ld1_gather_index">;
def SVLD1UW_GATHER_64B_INDICES_U : MInst<"svld1uw_gather_[{3}]index_{d}", "dPYu", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ld1_gather_index">;

def SVLD1_GATHER_32B_INDICES_U   : MInst<"svld1_gather_[{3}]index[_{d}]", "dPcu", "iUif", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ld1_gather_uxtw_index">;
def SVLD1SH_GATHER_32B_INDICES_U : MInst<"svld1sh_gather_[{3}]index_{d}", "dPTu", "iUi",  [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ld1_gather_uxtw_index">;
def SVLD1UH_GATHER_32B_INDICES_U : MInst<"svld1uh_gather_[{3}]index_{d}", "dPXu", "iUi",  [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather_uxtw_index">;

// Load one vector (vector base, signed scalar index)
def SVLD1_GATHER_INDEX_S     : MInst<"svld1_gather[_{2}base]_index_{d}",   "dPul", "ilUiUlfd", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1SH_GATHER_INDEX_S   : MInst<"svld1sh_gather[_{2}base]_index_{d}", "dPul", "ilUiUl",   [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1UH_GATHER_INDEX_S   : MInst<"svld1uh_gather[_{2}base]_index_{d}", "dPul", "ilUiUl",   [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1SW_GATHER_INDEX_S   : MInst<"svld1sw_gather[_{2}base]_index_{d}", "dPul", "lUl",      [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ld1_gather_scalar_offset">;
def SVLD1UW_GATHER_INDEX_S   : MInst<"svld1uw_gather[_{2}base]_index_{d}", "dPul", "lUl",      [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ld1_gather_scalar_offset">;


// First-faulting load one vector (scalar base)
def SVLDFF1   : MInst<"svldff1[_{2}]", "dPc", "csilUcUsUiUlhfd", [IsLoad],               MemEltTyDefault, "aarch64_sve_ldff1">;
def SVLDFF1SB : MInst<"svldff1sb_{d}", "dPS", "silUsUiUl",       [IsLoad],               MemEltTyInt8,    "aarch64_sve_ldff1">;
def SVLDFF1UB : MInst<"svldff1ub_{d}", "dPW", "silUsUiUl",       [IsLoad, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldff1">;
def SVLDFF1SH : MInst<"svldff1sh_{d}", "dPT", "ilUiUl",          [IsLoad],               MemEltTyInt16,   "aarch64_sve_ldff1">;
def SVLDFF1UH : MInst<"svldff1uh_{d}", "dPX", "ilUiUl",          [IsLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1">;
def SVLDFF1SW : MInst<"svldff1sw_{d}", "dPU", "lUl",             [IsLoad],               MemEltTyInt32,   "aarch64_sve_ldff1">;
def SVLDFF1UW : MInst<"svldff1uw_{d}", "dPY", "lUl",             [IsLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldff1">;

// First-faulting load one vector (scalar base, VL displacement)
def SVLDFF1_VNUM   : MInst<"svldff1_vnum[_{2}]", "dPcl", "csilUcUsUiUlhfd", [IsLoad],               MemEltTyDefault, "aarch64_sve_ldff1">;
def SVLDFF1SB_VNUM : MInst<"svldff1sb_vnum_{d}", "dPSl", "silUsUiUl",       [IsLoad],               MemEltTyInt8,    "aarch64_sve_ldff1">;
def SVLDFF1UB_VNUM : MInst<"svldff1ub_vnum_{d}", "dPWl", "silUsUiUl",       [IsLoad, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldff1">;
def SVLDFF1SH_VNUM : MInst<"svldff1sh_vnum_{d}", "dPTl", "ilUiUl",          [IsLoad],               MemEltTyInt16,   "aarch64_sve_ldff1">;
def SVLDFF1UH_VNUM : MInst<"svldff1uh_vnum_{d}", "dPXl", "ilUiUl",          [IsLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1">;
def SVLDFF1SW_VNUM : MInst<"svldff1sw_vnum_{d}", "dPUl", "lUl",             [IsLoad],               MemEltTyInt32,   "aarch64_sve_ldff1">;
def SVLDFF1UW_VNUM : MInst<"svldff1uw_vnum_{d}", "dPYl", "lUl",             [IsLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldff1">;
} //  let SVETargetGuard = "sve", SMETargetGuard = InvalidMode

let SVETargetGuard = "sve,bf16", SMETargetGuard = InvalidMode in {
  def SVLDFF1_BF      : MInst<"svldff1[_{2}]",      "dPc",  "b", [IsLoad], MemEltTyDefault, "aarch64_sve_ldff1">;
  def SVLDFF1_VNUM_BF : MInst<"svldff1_vnum[_{2}]", "dPcl", "b", [IsLoad], MemEltTyDefault, "aarch64_sve_ldff1">;
}

let SVETargetGuard = "sve", SMETargetGuard = InvalidMode in {
// First-faulting load one vector (vector base)
def SVLDFF1_GATHER_BASES_U   : MInst<"svldff1_gather[_{2}base]_{d}",   "dPu", "ilUiUlfd", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1SB_GATHER_BASES_U : MInst<"svldff1sb_gather[_{2}base]_{d}", "dPu", "ilUiUl",   [IsGatherLoad],               MemEltTyInt8,    "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1UB_GATHER_BASES_U : MInst<"svldff1ub_gather[_{2}base]_{d}", "dPu", "ilUiUl",   [IsGatherLoad, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1SH_GATHER_BASES_U : MInst<"svldff1sh_gather[_{2}base]_{d}", "dPu", "ilUiUl",   [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1UH_GATHER_BASES_U : MInst<"svldff1uh_gather[_{2}base]_{d}", "dPu", "ilUiUl",   [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1SW_GATHER_BASES_U : MInst<"svldff1sw_gather[_{2}base]_{d}", "dPu", "lUl",      [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1UW_GATHER_BASES_U : MInst<"svldff1uw_gather[_{2}base]_{d}", "dPu", "lUl",      [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldff1_gather_scalar_offset">;

// First-faulting load one vector (scalar base, signed vector offset in bytes)
def SVLDFF1_GATHER_64B_OFFSETS_S   : MInst<"svldff1_gather_[{3}]offset[_{d}]", "dPcx", "lUld", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ldff1_gather">;
def SVLDFF1SB_GATHER_64B_OFFSETS_S : MInst<"svldff1sb_gather_[{3}]offset_{d}", "dPSx", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ldff1_gather">;
def SVLDFF1UB_GATHER_64B_OFFSETS_S : MInst<"svldff1ub_gather_[{3}]offset_{d}", "dPWx", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldff1_gather">;
def SVLDFF1SH_GATHER_64B_OFFSETS_S : MInst<"svldff1sh_gather_[{3}]offset_{d}", "dPTx", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ldff1_gather">;
def SVLDFF1UH_GATHER_64B_OFFSETS_S : MInst<"svldff1uh_gather_[{3}]offset_{d}", "dPXx", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather">;
def SVLDFF1SW_GATHER_64B_OFFSETS_S : MInst<"svldff1sw_gather_[{3}]offset_{d}", "dPUx", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt32,   "aarch64_sve_ldff1_gather">;
def SVLDFF1UW_GATHER_64B_OFFSETS_S : MInst<"svldff1uw_gather_[{3}]offset_{d}", "dPYx", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldff1_gather">;

def SVLDFF1_GATHER_32B_OFFSETS_S   : MInst<"svldff1_gather_[{3}]offset[_{d}]", "dPcx", "iUif", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ldff1_gather_sxtw">;
def SVLDFF1SB_GATHER_32B_OFFSETS_S : MInst<"svldff1sb_gather_[{3}]offset_{d}", "dPSx", "iUi",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ldff1_gather_sxtw">;
def SVLDFF1UB_GATHER_32B_OFFSETS_S : MInst<"svldff1ub_gather_[{3}]offset_{d}", "dPWx", "iUi",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldff1_gather_sxtw">;
def SVLDFF1SH_GATHER_32B_OFFSETS_S : MInst<"svldff1sh_gather_[{3}]offset_{d}", "dPTx", "iUi",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ldff1_gather_sxtw">;
def SVLDFF1UH_GATHER_32B_OFFSETS_S : MInst<"svldff1uh_gather_[{3}]offset_{d}", "dPXx", "iUi",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather_sxtw">;

// First-faulting load one vector (scalar base, unsigned vector offset in bytes)
def SVLDFF1_GATHER_64B_OFFSETS_U   : MInst<"svldff1_gather_[{3}]offset[_{d}]", "dPcu", "lUld", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ldff1_gather">;
def SVLDFF1SB_GATHER_64B_OFFSETS_U : MInst<"svldff1sb_gather_[{3}]offset_{d}", "dPSu", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ldff1_gather">;
def SVLDFF1UB_GATHER_64B_OFFSETS_U : MInst<"svldff1ub_gather_[{3}]offset_{d}", "dPWu", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldff1_gather">;
def SVLDFF1SH_GATHER_64B_OFFSETS_U : MInst<"svldff1sh_gather_[{3}]offset_{d}", "dPTu", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ldff1_gather">;
def SVLDFF1UH_GATHER_64B_OFFSETS_U : MInst<"svldff1uh_gather_[{3}]offset_{d}", "dPXu", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather">;
def SVLDFF1SW_GATHER_64B_OFFSETS_U : MInst<"svldff1sw_gather_[{3}]offset_{d}", "dPUu", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt32,   "aarch64_sve_ldff1_gather">;
def SVLDFF1UW_GATHER_64B_OFFSETS_U : MInst<"svldff1uw_gather_[{3}]offset_{d}", "dPYu", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldff1_gather">;

def SVLDFF1_GATHER_32B_OFFSETS_U   : MInst<"svldff1_gather_[{3}]offset[_{d}]", "dPcu", "iUif", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ldff1_gather_uxtw">;
def SVLDFF1SB_GATHER_32B_OFFSETS_U : MInst<"svldff1sb_gather_[{3}]offset_{d}", "dPSu", "iUi",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ldff1_gather_uxtw">;
def SVLDFF1UB_GATHER_32B_OFFSETS_U : MInst<"svldff1ub_gather_[{3}]offset_{d}", "dPWu", "iUi",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldff1_gather_uxtw">;
def SVLDFF1SH_GATHER_32B_OFFSETS_U : MInst<"svldff1sh_gather_[{3}]offset_{d}", "dPTu", "iUi",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ldff1_gather_uxtw">;
def SVLDFF1UH_GATHER_32B_OFFSETS_U : MInst<"svldff1uh_gather_[{3}]offset_{d}", "dPXu", "iUi",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather_uxtw">;

// First-faulting load one vector (vector base, signed scalar offset in bytes)
def SVLDFF1_GATHER_OFFSET_S   : MInst<"svldff1_gather[_{2}base]_offset_{d}",   "dPul", "ilUiUlfd", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1SB_GATHER_OFFSET_S : MInst<"svldff1sb_gather[_{2}base]_offset_{d}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1UB_GATHER_OFFSET_S : MInst<"svldff1ub_gather[_{2}base]_offset_{d}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1SH_GATHER_OFFSET_S : MInst<"svldff1sh_gather[_{2}base]_offset_{d}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1UH_GATHER_OFFSET_S : MInst<"svldff1uh_gather[_{2}base]_offset_{d}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1SW_GATHER_OFFSET_S : MInst<"svldff1sw_gather[_{2}base]_offset_{d}", "dPul", "lUl",      [IsGatherLoad, IsByteIndexed],               MemEltTyInt32,   "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1UW_GATHER_OFFSET_S : MInst<"svldff1uw_gather[_{2}base]_offset_{d}", "dPul", "lUl",      [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldff1_gather_scalar_offset">;

// First-faulting load one vector (scalar base, signed vector index)
def SVLDFF1_GATHER_64B_INDICES_S   : MInst<"svldff1_gather_[{3}]index[_{d}]", "dPcx", "lUld", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ldff1_gather_index">;
def SVLDFF1SH_GATHER_64B_INDICES_S : MInst<"svldff1sh_gather_[{3}]index_{d}", "dPTx", "lUl",  [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ldff1_gather_index">;
def SVLDFF1UH_GATHER_64B_INDICES_S : MInst<"svldff1uh_gather_[{3}]index_{d}", "dPXx", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather_index">;
def SVLDFF1SW_GATHER_64B_INDICES_S : MInst<"svldff1sw_gather_[{3}]index_{d}", "dPUx", "lUl",  [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ldff1_gather_index">;
def SVLDFF1UW_GATHER_64B_INDICES_S : MInst<"svldff1uw_gather_[{3}]index_{d}", "dPYx", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldff1_gather_index">;

def SVLDFF1_GATHER_32B_INDICES_S   : MInst<"svldff1_gather_[{3}]index[_{d}]", "dPcx", "iUif", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ldff1_gather_sxtw_index">;
def SVLDFF1SH_GATHER_32B_INDICES_S : MInst<"svldff1sh_gather_[{3}]index_{d}", "dPTx", "iUi",  [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ldff1_gather_sxtw_index">;
def SVLDFF1UH_GATHER_32B_INDICES_S : MInst<"svldff1uh_gather_[{3}]index_{d}", "dPXx", "iUi",  [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather_sxtw_index">;

// First-faulting load one vector (scalar base, unsigned vector index)
def SVLDFF1_GATHER_64B_INDICES_U   : MInst<"svldff1_gather_[{3}]index[_{d}]", "dPcu", "lUld", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ldff1_gather_index">;
def SVLDFF1SH_GATHER_64B_INDICES_U : MInst<"svldff1sh_gather_[{3}]index_{d}", "dPTu", "lUl",  [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ldff1_gather_index">;
def SVLDFF1UH_GATHER_64B_INDICES_U : MInst<"svldff1uh_gather_[{3}]index_{d}", "dPXu", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather_index">;
def SVLDFF1SW_GATHER_64B_INDICES_U : MInst<"svldff1sw_gather_[{3}]index_{d}", "dPUu", "lUl",  [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ldff1_gather_index">;
def SVLDFF1UW_GATHER_64B_INDICES_U : MInst<"svldff1uw_gather_[{3}]index_{d}", "dPYu", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldff1_gather_index">;

def SVLDFF1_GATHER_32B_INDICES_U   : MInst<"svldff1_gather_[{3}]index[_{d}]", "dPcu", "iUif", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ldff1_gather_uxtw_index">;
def SVLDFF1SH_GATHER_32B_INDICES_U : MInst<"svldff1sh_gather_[{3}]index_{d}", "dPTu", "iUi",  [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ldff1_gather_uxtw_index">;
def SVLDFF1UH_GATHER_32B_INDICES_U : MInst<"svldff1uh_gather_[{3}]index_{d}", "dPXu", "iUi",  [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather_uxtw_index">;

// First-faulting load one vector (vector base, signed scalar index)
def SVLDFF1_GATHER_INDEX_S   : MInst<"svldff1_gather[_{2}base]_index_{d}",   "dPul", "ilUiUlfd", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1SH_GATHER_INDEX_S : MInst<"svldff1sh_gather[_{2}base]_index_{d}", "dPul", "ilUiUl",   [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1UH_GATHER_INDEX_S : MInst<"svldff1uh_gather[_{2}base]_index_{d}", "dPul", "ilUiUl",   [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1SW_GATHER_INDEX_S : MInst<"svldff1sw_gather[_{2}base]_index_{d}", "dPul", "lUl",      [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ldff1_gather_scalar_offset">;
def SVLDFF1UW_GATHER_INDEX_S : MInst<"svldff1uw_gather[_{2}base]_index_{d}", "dPul", "lUl",      [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldff1_gather_scalar_offset">;

// Non-faulting load one vector (scalar base)
def SVLDNF1   : MInst<"svldnf1[_{2}]", "dPc", "csilUcUsUiUlhfd", [IsLoad],               MemEltTyDefault, "aarch64_sve_ldnf1">;
def SVLDNF1SB : MInst<"svldnf1sb_{d}", "dPS", "silUsUiUl",       [IsLoad],               MemEltTyInt8,    "aarch64_sve_ldnf1">;
def SVLDNF1UB : MInst<"svldnf1ub_{d}", "dPW", "silUsUiUl",       [IsLoad, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldnf1">;
def SVLDNF1SH : MInst<"svldnf1sh_{d}", "dPT", "ilUiUl",          [IsLoad],               MemEltTyInt16,   "aarch64_sve_ldnf1">;
def SVLDNF1UH : MInst<"svldnf1uh_{d}", "dPX", "ilUiUl",          [IsLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldnf1">;
def SVLDNF1SW : MInst<"svldnf1sw_{d}", "dPU", "lUl",             [IsLoad],               MemEltTyInt32,   "aarch64_sve_ldnf1">;
def SVLDNF1UW : MInst<"svldnf1uw_{d}", "dPY", "lUl",             [IsLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldnf1">;

// Non-faulting load one vector (scalar base, VL displacement)
def SVLDNF1_VNUM   : MInst<"svldnf1_vnum[_{2}]", "dPcl", "csilUcUsUiUlhfd", [IsLoad],               MemEltTyDefault, "aarch64_sve_ldnf1">;
def SVLDNF1SB_VNUM : MInst<"svldnf1sb_vnum_{d}", "dPSl", "silUsUiUl",       [IsLoad],               MemEltTyInt8,    "aarch64_sve_ldnf1">;
def SVLDNF1UB_VNUM : MInst<"svldnf1ub_vnum_{d}", "dPWl", "silUsUiUl",       [IsLoad, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldnf1">;
def SVLDNF1SH_VNUM : MInst<"svldnf1sh_vnum_{d}", "dPTl", "ilUiUl",          [IsLoad],               MemEltTyInt16,   "aarch64_sve_ldnf1">;
def SVLDNF1UH_VNUM : MInst<"svldnf1uh_vnum_{d}", "dPXl", "ilUiUl",          [IsLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldnf1">;
def SVLDNF1SW_VNUM : MInst<"svldnf1sw_vnum_{d}", "dPUl", "lUl",             [IsLoad],               MemEltTyInt32,   "aarch64_sve_ldnf1">;
def SVLDNF1UW_VNUM : MInst<"svldnf1uw_vnum_{d}", "dPYl", "lUl",             [IsLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldnf1">;
} //  let SVETargetGuard = "sve", SMETargetGuard = InvalidMode

let SVETargetGuard = "sve,bf16", SMETargetGuard = InvalidMode in {
  def SVLDNF1_BF      : MInst<"svldnf1[_{2}]",      "dPc",  "b", [IsLoad], MemEltTyDefault, "aarch64_sve_ldnf1">;
  def SVLDNF1_VNUM_BF : MInst<"svldnf1_vnum[_{2}]", "dPcl", "b", [IsLoad], MemEltTyDefault, "aarch64_sve_ldnf1">;
}

// Load one vector, unextended load, non-temporal (scalar base)
def SVLDNT1 : MInst<"svldnt1[_{2}]", "dPc", "csilUcUsUiUlhfd", [IsLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_ldnt1">;

// Load one vector, unextended load, non-temporal (scalar base, VL displacement)
def SVLDNT1_VNUM : MInst<"svldnt1_vnum[_{2}]", "dPcl", "csilUcUsUiUlhfd", [IsLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_ldnt1">;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  def SVLDNT1_BF      : MInst<"svldnt1[_{2}]",      "dPc",  "b", [IsLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_ldnt1">;
  def SVLDNT1_VNUM_BF : MInst<"svldnt1_vnum[_{2}]", "dPcl", "b", [IsLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_ldnt1">;
}

// Load one quadword and replicate (scalar base)
def SVLD1RQ : SInst<"svld1rq[_{2}]", "dPc", "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_ld1rq", [VerifyRuntimeMode]>;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  def SVLD1RQ_BF : SInst<"svld1rq[_{2}]", "dPc",  "b", MergeNone, "aarch64_sve_ld1rq", [VerifyRuntimeMode]>;
}

multiclass StructLoad<string name, string proto, string i> {
  def : SInst<name, proto, "csilUcUsUiUlhfd", MergeNone, i, [IsStructLoad, VerifyRuntimeMode]>;
  let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
    def: SInst<name, proto, "b", MergeNone, i, [IsStructLoad, VerifyRuntimeMode]>;
  }
}

// Load N-element structure into N vectors (scalar base)
defm SVLD2 : StructLoad<"svld2[_{2}]", "2Pc", "aarch64_sve_ld2_sret">;
defm SVLD3 : StructLoad<"svld3[_{2}]", "3Pc", "aarch64_sve_ld3_sret">;
defm SVLD4 : StructLoad<"svld4[_{2}]", "4Pc", "aarch64_sve_ld4_sret">;

// Load N-element structure into N vectors (scalar base, VL displacement)
defm SVLD2_VNUM : StructLoad<"svld2_vnum[_{2}]", "2Pcl", "aarch64_sve_ld2_sret">;
defm SVLD3_VNUM : StructLoad<"svld3_vnum[_{2}]", "3Pcl", "aarch64_sve_ld3_sret">;
defm SVLD4_VNUM : StructLoad<"svld4_vnum[_{2}]", "4Pcl", "aarch64_sve_ld4_sret">;

// Load one octoword and replicate (scalar base)
let SVETargetGuard = "sve,f64mm", SMETargetGuard = InvalidMode in {
  def SVLD1RO : SInst<"svld1ro[_{2}]", "dPc", "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_ld1ro">;
}
let SVETargetGuard = "sve,f64mm,bf16", SMETargetGuard = InvalidMode in {
  def SVLD1RO_BF16 : SInst<"svld1ro[_{2}]", "dPc", "b", MergeNone, "aarch64_sve_ld1ro">;
}

let SVETargetGuard = "sve,bf16", SMETargetGuard = InvalidMode in {
  def SVBFMMLA       : SInst<"svbfmmla[_{0}]",       "MMdd",  "b", MergeNone, "aarch64_sve_bfmmla",       [IsOverloadNone]>;
}

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  def SVBFDOT        : SInst<"svbfdot[_{0}]",        "MMdd",  "b", MergeNone, "aarch64_sve_bfdot",        [IsOverloadNone, VerifyRuntimeMode]>;
  def SVBFMLALB      : SInst<"svbfmlalb[_{0}]",      "MMdd",  "b", MergeNone, "aarch64_sve_bfmlalb",      [IsOverloadNone, VerifyRuntimeMode]>;
  def SVBFMLALT      : SInst<"svbfmlalt[_{0}]",      "MMdd",  "b", MergeNone, "aarch64_sve_bfmlalt",      [IsOverloadNone, VerifyRuntimeMode]>;
  def SVBFDOT_N      : SInst<"svbfdot[_n_{0}]",      "MMda",  "b", MergeNone, "aarch64_sve_bfdot",        [IsOverloadNone, VerifyRuntimeMode]>;
  def SVBFMLALB_N    : SInst<"svbfmlalb[_n_{0}]",    "MMda",  "b", MergeNone, "aarch64_sve_bfmlalb",      [IsOverloadNone, VerifyRuntimeMode]>;
  def SVBFMLALT_N    : SInst<"svbfmlalt[_n_{0}]",    "MMda",  "b", MergeNone, "aarch64_sve_bfmlalt",      [IsOverloadNone, VerifyRuntimeMode]>;
  def SVBFDOT_LANE   : SInst<"svbfdot_lane[_{0}]",   "MMddi", "b", MergeNone, "aarch64_sve_bfdot_lane_v2",   [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<3, ImmCheck0_3>]>;
  def SVBFMLALB_LANE : SInst<"svbfmlalb_lane[_{0}]", "MMddi", "b", MergeNone, "aarch64_sve_bfmlalb_lane_v2", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<3, ImmCheck0_7>]>;
  def SVBFMLALT_LANE : SInst<"svbfmlalt_lane[_{0}]", "MMddi", "b", MergeNone, "aarch64_sve_bfmlalt_lane_v2", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<3, ImmCheck0_7>]>;
} // let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16"

let SVETargetGuard = "sve2p1", SMETargetGuard = InvalidMode in {
  // Contiguous zero-extending load to quadword (single vector).
  def SVLD1UWQ      : MInst<"svld1uwq[_{d}]", "dPc",  "iUif", [IsLoad], MemEltTyInt32, "aarch64_sve_ld1uwq">;
  def SVLD1UWQ_VNUM : MInst<"svld1uwq_vnum[_{d}]", "dPcl", "iUif", [IsLoad], MemEltTyInt32, "aarch64_sve_ld1uwq">;

  def SVLD1UDQ      : MInst<"svld1udq[_{d}]", "dPc",  "lUld", [IsLoad], MemEltTyInt64, "aarch64_sve_ld1udq">;
  def SVLD1UDQ_VNUM : MInst<"svld1udq_vnum[_{d}]", "dPcl", "lUld", [IsLoad], MemEltTyInt64, "aarch64_sve_ld1udq">;

  // Load one vector (vector base + scalar offset)
  def SVLD1Q_GATHER_U64BASE_OFFSET : MInst<"svld1q_gather[_{2}base]_offset_{d}", "dPgl", "cUcsUsiUilUlfhdb", [IsGatherLoad, IsByteIndexed], MemEltTyDefault, "aarch64_sve_ld1q_gather_scalar_offset">;
  def SVLD1Q_GATHER_U64BASE : MInst<"svld1q_gather[_{2}base]_{d}", "dPg", "cUcsUsiUilUlfhdb", [IsGatherLoad, IsByteIndexed], MemEltTyDefault, "aarch64_sve_ld1q_gather_scalar_offset">;

  // Load one vector (scalar base + vector offset)
  def SVLD1Q_GATHER_U64OFFSET : MInst<"svld1q_gather_[{3}]offset[_{d}]", "dPcg", "cUcsUsiUilUlfhdb", [IsGatherLoad, IsByteIndexed], MemEltTyDefault, "aarch64_sve_ld1q_gather_vector_offset">;

  // Load N-element structure into N vectors (scalar base)
  defm SVLD2Q : StructLoad<"svld2q[_{2}]", "2Pc", "aarch64_sve_ld2q_sret">;
  defm SVLD3Q : StructLoad<"svld3q[_{2}]", "3Pc", "aarch64_sve_ld3q_sret">;
  defm SVLD4Q : StructLoad<"svld4q[_{2}]", "4Pc", "aarch64_sve_ld4q_sret">;

  // Load N-element structure into N vectors (scalar base, VL displacement)
  defm SVLD2Q_VNUM : StructLoad<"svld2q_vnum[_{2}]", "2Pcl", "aarch64_sve_ld2q_sret">;
  defm SVLD3Q_VNUM : StructLoad<"svld3q_vnum[_{2}]", "3Pcl", "aarch64_sve_ld3q_sret">;
  defm SVLD4Q_VNUM : StructLoad<"svld4q_vnum[_{2}]", "4Pcl", "aarch64_sve_ld4q_sret">;

  // Load quadwords (scalar base + vector index)
  def SVLD1Q_GATHER_INDICES_U : MInst<"svld1q_gather_[{3}]index[_{d}]", "dPcg", "sUsiUilUlbhfd", [IsGatherLoad], MemEltTyDefault, "aarch64_sve_ld1q_gather_index">;

  // Load quadwords (vector base + scalar index)
  def SVLD1Q_GATHER_INDEX_S   : MInst<"svld1q_gather[_{2}base]_index_{d}", "dPgl", "sUsiUilUlbhfd", [IsGatherLoad], MemEltTyDefault, "aarch64_sve_ld1q_gather_scalar_offset">;
}

////////////////////////////////////////////////////////////////////////////////
// Stores

// Store one vector (scalar base)
def SVST1    : MInst<"svst1[_{d}]",  "vPpd", "csilUcUsUiUlhfd", [IsStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_st1">;
def SVST1B_S : MInst<"svst1b[_{d}]", "vPAd", "sil",             [IsStore, VerifyRuntimeMode], MemEltTyInt8,    "aarch64_sve_st1">;
def SVST1B_U : MInst<"svst1b[_{d}]", "vPEd", "UsUiUl",          [IsStore, VerifyRuntimeMode], MemEltTyInt8,    "aarch64_sve_st1">;
def SVST1H_S : MInst<"svst1h[_{d}]", "vPBd", "il",              [IsStore, VerifyRuntimeMode], MemEltTyInt16,   "aarch64_sve_st1">;
def SVST1H_U : MInst<"svst1h[_{d}]", "vPFd", "UiUl",            [IsStore, VerifyRuntimeMode], MemEltTyInt16,   "aarch64_sve_st1">;
def SVST1W_S : MInst<"svst1w[_{d}]", "vPCd", "l",               [IsStore, VerifyRuntimeMode], MemEltTyInt32,   "aarch64_sve_st1">;
def SVST1W_U : MInst<"svst1w[_{d}]", "vPGd", "Ul",              [IsStore, VerifyRuntimeMode], MemEltTyInt32,   "aarch64_sve_st1">;

// Store one vector (scalar base, VL displacement)
def SVST1_VNUM    : MInst<"svst1_vnum[_{d}]",  "vPpld", "csilUcUsUiUlhfd", [IsStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_st1">;
def SVST1B_VNUM_S : MInst<"svst1b_vnum[_{d}]", "vPAld", "sil",             [IsStore, VerifyRuntimeMode], MemEltTyInt8,    "aarch64_sve_st1">;
def SVST1B_VNUM_U : MInst<"svst1b_vnum[_{d}]", "vPEld", "UsUiUl",          [IsStore, VerifyRuntimeMode], MemEltTyInt8,    "aarch64_sve_st1">;
def SVST1H_VNUM_S : MInst<"svst1h_vnum[_{d}]", "vPBld", "il",              [IsStore, VerifyRuntimeMode], MemEltTyInt16,   "aarch64_sve_st1">;
def SVST1H_VNUM_U : MInst<"svst1h_vnum[_{d}]", "vPFld", "UiUl",            [IsStore, VerifyRuntimeMode], MemEltTyInt16,   "aarch64_sve_st1">;
def SVST1W_VNUM_S : MInst<"svst1w_vnum[_{d}]", "vPCld", "l",               [IsStore, VerifyRuntimeMode], MemEltTyInt32,   "aarch64_sve_st1">;
def SVST1W_VNUM_U : MInst<"svst1w_vnum[_{d}]", "vPGld", "Ul",              [IsStore, VerifyRuntimeMode], MemEltTyInt32,   "aarch64_sve_st1">;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  def SVST1_BF      : MInst<"svst1[_{d}]",      "vPpd",  "b", [IsStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_st1">;
  def SVST1_VNUM_BF : MInst<"svst1_vnum[_{d}]", "vPpld", "b", [IsStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_st1">;
}

let SVETargetGuard = "sve", SMETargetGuard = InvalidMode in {
// Store one vector (vector base)
def SVST1_SCATTER_BASES_U     : MInst<"svst1_scatter[_{2}base_{d}]",  "vPud",  "ilUiUlfd", [IsScatterStore], MemEltTyDefault, "aarch64_sve_st1_scatter_scalar_offset">;
def SVST1B_SCATTER_BASES_U    : MInst<"svst1b_scatter[_{2}base_{d}]", "vPud",  "ilUiUl",   [IsScatterStore], MemEltTyInt8,    "aarch64_sve_st1_scatter_scalar_offset">;
def SVST1H_SCATTER_BASES_U    : MInst<"svst1h_scatter[_{2}base_{d}]", "vPud",  "ilUiUl",   [IsScatterStore], MemEltTyInt16,   "aarch64_sve_st1_scatter_scalar_offset">;
def SVST1W_SCATTER_BASES_U    : MInst<"svst1w_scatter[_{2}base_{d}]", "vPud",  "lUl",      [IsScatterStore], MemEltTyInt32,   "aarch64_sve_st1_scatter_scalar_offset">;

// Store one vector (scalar base, signed vector offset in bytes)
def SVST1_SCATTER_64B_OFFSETS_S   : MInst<"svst1_scatter_[{3}]offset[_{d}]",  "vPpxd", "lUld", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_st1_scatter">;
def SVST1B_SCATTER_64B_OFFSETS_SS : MInst<"svst1b_scatter_[{3}]offset[_{d}]", "vPAxd", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_st1_scatter">;
def SVST1B_SCATTER_64B_OFFSETS_SU : MInst<"svst1b_scatter_[{3}]offset[_{d}]", "vPExd", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_st1_scatter">;
def SVST1H_SCATTER_64B_OFFSETS_SS : MInst<"svst1h_scatter_[{3}]offset[_{d}]", "vPBxd", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_st1_scatter">;
def SVST1H_SCATTER_64B_OFFSETS_SU : MInst<"svst1h_scatter_[{3}]offset[_{d}]", "vPFxd", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_st1_scatter">;
def SVST1W_SCATTER_64B_OFFSETS_SS : MInst<"svst1w_scatter_[{3}]offset[_{d}]", "vPCxd", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt32,   "aarch64_sve_st1_scatter">;
def SVST1W_SCATTER_64B_OFFSETS_SU : MInst<"svst1w_scatter_[{3}]offset[_{d}]", "vPGxd", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt32,   "aarch64_sve_st1_scatter">;

def SVST1_SCATTER_32B_OFFSETS_S   : MInst<"svst1_scatter_[{3}]offset[_{d}]",  "vPpxd", "iUif", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_st1_scatter_sxtw">;
def SVST1B_SCATTER_32B_OFFSETS_SS : MInst<"svst1b_scatter_[{3}]offset[_{d}]", "vPAxd", "i",    [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_st1_scatter_sxtw">;
def SVST1B_SCATTER_32B_OFFSETS_SU : MInst<"svst1b_scatter_[{3}]offset[_{d}]", "vPExd", "Ui",   [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_st1_scatter_sxtw">;
def SVST1H_SCATTER_32B_OFFSETS_SS : MInst<"svst1h_scatter_[{3}]offset[_{d}]", "vPBxd", "i",    [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_st1_scatter_sxtw">;
def SVST1H_SCATTER_32B_OFFSETS_SU : MInst<"svst1h_scatter_[{3}]offset[_{d}]", "vPFxd", "Ui",   [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_st1_scatter_sxtw">;

// Store one vector (scalar base, unsigned vector offset in bytes)
def SVST1_SCATTER_64B_OFFSETS_U   : MInst<"svst1_scatter_[{3}]offset[_{d}]",  "vPpud", "lUld", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_st1_scatter">;
def SVST1B_SCATTER_64B_OFFSETS_US : MInst<"svst1b_scatter_[{3}]offset[_{d}]", "vPAud", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_st1_scatter">;
def SVST1B_SCATTER_64B_OFFSETS_UU : MInst<"svst1b_scatter_[{3}]offset[_{d}]", "vPEud", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_st1_scatter">;
def SVST1H_SCATTER_64B_OFFSETS_US : MInst<"svst1h_scatter_[{3}]offset[_{d}]", "vPBud", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_st1_scatter">;
def SVST1H_SCATTER_64B_OFFSETS_UU : MInst<"svst1h_scatter_[{3}]offset[_{d}]", "vPFud", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_st1_scatter">;
def SVST1W_SCATTER_64B_OFFSETS_US : MInst<"svst1w_scatter_[{3}]offset[_{d}]", "vPCud", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt32,   "aarch64_sve_st1_scatter">;
def SVST1W_SCATTER_64B_OFFSETS_UU : MInst<"svst1w_scatter_[{3}]offset[_{d}]", "vPGud", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt32,   "aarch64_sve_st1_scatter">;

def SVST1_SCATTER_32B_OFFSETS_U   : MInst<"svst1_scatter_[{3}]offset[_{d}]",  "vPpud", "iUif", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_st1_scatter_uxtw">;
def SVST1B_SCATTER_32B_OFFSETS_US : MInst<"svst1b_scatter_[{3}]offset[_{d}]", "vPAud", "i",    [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_st1_scatter_uxtw">;
def SVST1B_SCATTER_32B_OFFSETS_UU : MInst<"svst1b_scatter_[{3}]offset[_{d}]", "vPEud", "Ui",   [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_st1_scatter_uxtw">;
def SVST1H_SCATTER_32B_OFFSETS_US : MInst<"svst1h_scatter_[{3}]offset[_{d}]", "vPBud", "i",    [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_st1_scatter_uxtw">;
def SVST1H_SCATTER_32B_OFFSETS_UU : MInst<"svst1h_scatter_[{3}]offset[_{d}]", "vPFud", "Ui",   [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_st1_scatter_uxtw">;

// Store one vector (vector base, signed scalar offset in bytes)
def SVST1_SCATTER_OFFSET_S    : MInst<"svst1_scatter[_{2}base]_offset[_{d}]",  "vPuld", "ilUiUlfd", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_st1_scatter_scalar_offset">;
def SVST1B_SCATTER_OFFSET_S   : MInst<"svst1b_scatter[_{2}base]_offset[_{d}]", "vPuld", "ilUiUl",   [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_st1_scatter_scalar_offset">;
def SVST1H_SCATTER_OFFSET_S   : MInst<"svst1h_scatter[_{2}base]_offset[_{d}]", "vPuld", "ilUiUl",   [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_st1_scatter_scalar_offset">;
def SVST1W_SCATTER_OFFSET_S   : MInst<"svst1w_scatter[_{2}base]_offset[_{d}]", "vPuld", "lUl",      [IsScatterStore, IsByteIndexed], MemEltTyInt32,   "aarch64_sve_st1_scatter_scalar_offset">;

// Store one vector (scalar base, signed vector index)
def SVST1_SCATTER_64B_INDICES_S   : MInst<"svst1_scatter_[{3}]index[_{d}]",  "vPpxd", "lUld", [IsScatterStore], MemEltTyDefault, "aarch64_sve_st1_scatter_index">;
def SVST1H_SCATTER_64B_INDICES_SS : MInst<"svst1h_scatter_[{3}]index[_{d}]", "vPBxd", "l",    [IsScatterStore], MemEltTyInt16,   "aarch64_sve_st1_scatter_index">;
def SVST1H_SCATTER_64B_INDICES_SU : MInst<"svst1h_scatter_[{3}]index[_{d}]", "vPFxd", "Ul",   [IsScatterStore], MemEltTyInt16,   "aarch64_sve_st1_scatter_index">;
def SVST1W_SCATTER_64B_INDICES_SS : MInst<"svst1w_scatter_[{3}]index[_{d}]", "vPCxd", "l",    [IsScatterStore], MemEltTyInt32,   "aarch64_sve_st1_scatter_index">;
def SVST1W_SCATTER_64B_INDICES_SU : MInst<"svst1w_scatter_[{3}]index[_{d}]", "vPGxd", "Ul",   [IsScatterStore], MemEltTyInt32,   "aarch64_sve_st1_scatter_index">;

def SVST1_SCATTER_32B_INDICES_S   : MInst<"svst1_scatter_[{3}]index[_{d}]",  "vPpxd", "iUif", [IsScatterStore], MemEltTyDefault, "aarch64_sve_st1_scatter_sxtw_index">;
def SVST1H_SCATTER_32B_INDICES_SS : MInst<"svst1h_scatter_[{3}]index[_{d}]", "vPBxd", "i",    [IsScatterStore], MemEltTyInt16,   "aarch64_sve_st1_scatter_sxtw_index">;
def SVST1H_SCATTER_32B_INDICES_SU : MInst<"svst1h_scatter_[{3}]index[_{d}]", "vPFxd", "Ui",   [IsScatterStore], MemEltTyInt16,   "aarch64_sve_st1_scatter_sxtw_index">;

// Store one vector (scalar base, unsigned vector index)
def SVST1_SCATTER_64B_INDICES_U   : MInst<"svst1_scatter_[{3}]index[_{d}]",  "vPpud", "lUld", [IsScatterStore], MemEltTyDefault, "aarch64_sve_st1_scatter_index">;
def SVST1H_SCATTER_64B_INDICES_US : MInst<"svst1h_scatter_[{3}]index[_{d}]", "vPBud", "l",    [IsScatterStore], MemEltTyInt16,   "aarch64_sve_st1_scatter_index">;
def SVST1H_SCATTER_64B_INDICES_UU : MInst<"svst1h_scatter_[{3}]index[_{d}]", "vPFud", "Ul",   [IsScatterStore], MemEltTyInt16,   "aarch64_sve_st1_scatter_index">;
def SVST1W_SCATTER_64B_INDICES_US : MInst<"svst1w_scatter_[{3}]index[_{d}]", "vPCud", "l",    [IsScatterStore], MemEltTyInt32,   "aarch64_sve_st1_scatter_index">;
def SVST1W_SCATTER_64B_INDICES_UU : MInst<"svst1w_scatter_[{3}]index[_{d}]", "vPGud", "Ul",   [IsScatterStore], MemEltTyInt32,   "aarch64_sve_st1_scatter_index">;

def SVST1_SCATTER_32B_INDICES_U   : MInst<"svst1_scatter_[{3}]index[_{d}]",  "vPpud", "iUif", [IsScatterStore], MemEltTyDefault, "aarch64_sve_st1_scatter_uxtw_index">;
def SVST1H_SCATTER_32B_INDICES_US : MInst<"svst1h_scatter_[{3}]index[_{d}]", "vPBud", "i",    [IsScatterStore], MemEltTyInt16,   "aarch64_sve_st1_scatter_uxtw_index">;
def SVST1H_SCATTER_32B_INDICES_UU : MInst<"svst1h_scatter_[{3}]index[_{d}]", "vPFud", "Ui",   [IsScatterStore], MemEltTyInt16,   "aarch64_sve_st1_scatter_uxtw_index">;

// Store one vector (vector base, signed scalar index)
def SVST1_SCATTER_INDEX_S     : MInst<"svst1_scatter[_{2}base]_index[_{d}]",  "vPuld", "ilUiUlfd", [IsScatterStore], MemEltTyDefault, "aarch64_sve_st1_scatter_scalar_offset">;
def SVST1H_SCATTER_INDEX_S    : MInst<"svst1h_scatter[_{2}base]_index[_{d}]", "vPuld", "ilUiUl",   [IsScatterStore], MemEltTyInt16,   "aarch64_sve_st1_scatter_scalar_offset">;
def SVST1W_SCATTER_INDEX_S    : MInst<"svst1w_scatter[_{2}base]_index[_{d}]", "vPuld", "lUl",      [IsScatterStore], MemEltTyInt32,   "aarch64_sve_st1_scatter_scalar_offset">;
} // let SVETargetGuard = "sve"

multiclass StructStore<string name, string proto, string i> {
  def : SInst<name, proto, "csilUcUsUiUlhfd", MergeNone, i, [IsStructStore, VerifyRuntimeMode]>;
  let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
    def: SInst<name, proto, "b", MergeNone, i, [IsStructStore, VerifyRuntimeMode]>;
  }
}
// Store N vectors into N-element structure (scalar base)
defm SVST2 : StructStore<"svst2[_{d}]", "vPp2", "aarch64_sve_st2">;
defm SVST3 : StructStore<"svst3[_{d}]", "vPp3", "aarch64_sve_st3">;
defm SVST4 : StructStore<"svst4[_{d}]", "vPp4", "aarch64_sve_st4">;

// Store N vectors into N-element structure (scalar base, VL displacement)
defm SVST2_VNUM : StructStore<"svst2_vnum[_{d}]", "vPpl2", "aarch64_sve_st2">;
defm SVST3_VNUM : StructStore<"svst3_vnum[_{d}]", "vPpl3", "aarch64_sve_st3">;
defm SVST4_VNUM : StructStore<"svst4_vnum[_{d}]", "vPpl4", "aarch64_sve_st4">;

// Store one vector, with no truncation, non-temporal (scalar base)
def SVSTNT1 : MInst<"svstnt1[_{d}]", "vPpd", "csilUcUsUiUlhfd", [IsStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_stnt1">;

// Store one vector, with no truncation, non-temporal (scalar base, VL displacement)
def SVSTNT1_VNUM : MInst<"svstnt1_vnum[_{d}]", "vPpld", "csilUcUsUiUlhfd", [IsStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_stnt1">;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  def SVSTNT1_BF      : MInst<"svstnt1[_{d}]",      "vPpd",  "b", [IsStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_stnt1">;
  def SVSTNT1_VNUM_BF : MInst<"svstnt1_vnum[_{d}]", "vPpld", "b", [IsStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_stnt1">;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = InvalidMode in {
  // Contiguous truncating store from quadword (single vector).
  def SVST1UWQ      : MInst<"svst1wq[_{d}]", "vPcd", "iUif",  [IsStore], MemEltTyInt32, "aarch64_sve_st1wq">;
  def SVST1UWQ_VNUM : MInst<"svst1wq_vnum[_{d}]", "vPcld", "iUif", [IsStore], MemEltTyInt32, "aarch64_sve_st1wq">;

  def SVST1UDQ      : MInst<"svst1dq[_{d}]", "vPcd", "lUld",  [IsStore], MemEltTyInt64, "aarch64_sve_st1dq">;
  def SVST1UDQ_VNUM : MInst<"svst1dq_vnum[_{d}]", "vPcld", "lUld", [IsStore], MemEltTyInt64, "aarch64_sve_st1dq">;

  // Store one vector (vector base + scalar offset)
  def SVST1Q_SCATTER_U64BASE_OFFSET : MInst<"svst1q_scatter[_{2}base]_offset[_{d}]",  "vPgld", "cUcsUsiUilUlfhdb", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_st1q_scatter_scalar_offset">;
  def SVST1Q_SCATTER_U64BASE : MInst<"svst1q_scatter[_{2}base][_{d}]",  "vPgd", "cUcsUsiUilUlfhdb", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_st1q_scatter_scalar_offset">;

  // Store one vector (scalar base + vector offset)
  def SVST1Q_SCATTER_U64OFFSET : MInst<"svst1q_scatter_[{3}]offset[_{d}]", "vPpgd", "cUcsUsiUilUlfhdb", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_st1q_scatter_vector_offset">;

  // Store N vectors into N-element structure (scalar base)
  defm SVST2Q : StructStore<"svst2q[_{d}]", "vPc2", "aarch64_sve_st2q">;
  defm SVST3Q : StructStore<"svst3q[_{d}]", "vPc3", "aarch64_sve_st3q">;
  defm SVST4Q : StructStore<"svst4q[_{d}]", "vPc4", "aarch64_sve_st4q">;

  // Store N vectors into N-element structure (scalar base, VL displacement)
  defm SVST2Q_VNUM : StructStore<"svst2q_vnum[_{d}]", "vPcl2", "aarch64_sve_st2q">;
  defm SVST3Q_VNUM : StructStore<"svst3q_vnum[_{d}]", "vPcl3", "aarch64_sve_st3q">;
  defm SVST4Q_VNUM : StructStore<"svst4q_vnum[_{d}]", "vPcl4", "aarch64_sve_st4q">;

  // Scatter store quadwords (scalar base + vector index)
  def SVST1Q_SCATTER_INDICES_U : MInst<"svst1q_scatter_[{3}]index[_{d}]", "vPpgd", "sUsiUilUlbhfd", [IsScatterStore], MemEltTyDefault, "aarch64_sve_st1q_scatter_index">;

  // Scatter store quadwords (vector base + scalar index)
  def SVST1Q_SCATTER_INDEX_S   : MInst<"svst1q_scatter[_{2}base]_index[_{d}]", "vPgld", "sUsiUilUlbhfd", [IsScatterStore], MemEltTyDefault, "aarch64_sve_st1q_scatter_scalar_offset">;
}

////////////////////////////////////////////////////////////////////////////////
// Prefetches

// Prefetch (Scalar base)
def SVPRFB : MInst<"svprfb", "vPQJ", "c", [IsPrefetch, VerifyRuntimeMode], MemEltTyInt8,  "aarch64_sve_prf">;
def SVPRFH : MInst<"svprfh", "vPQJ", "s", [IsPrefetch, VerifyRuntimeMode], MemEltTyInt16, "aarch64_sve_prf">;
def SVPRFW : MInst<"svprfw", "vPQJ", "i", [IsPrefetch, VerifyRuntimeMode], MemEltTyInt32, "aarch64_sve_prf">;
def SVPRFD : MInst<"svprfd", "vPQJ", "l", [IsPrefetch, VerifyRuntimeMode], MemEltTyInt64, "aarch64_sve_prf">;

// Prefetch (Scalar base, VL displacement)
def SVPRFB_VNUM : MInst<"svprfb_vnum", "vPQlJ", "c", [IsPrefetch, VerifyRuntimeMode], MemEltTyInt8,  "aarch64_sve_prf">;
def SVPRFH_VNUM : MInst<"svprfh_vnum", "vPQlJ", "s", [IsPrefetch, VerifyRuntimeMode], MemEltTyInt16, "aarch64_sve_prf">;
def SVPRFW_VNUM : MInst<"svprfw_vnum", "vPQlJ", "i", [IsPrefetch, VerifyRuntimeMode], MemEltTyInt32, "aarch64_sve_prf">;
def SVPRFD_VNUM : MInst<"svprfd_vnum", "vPQlJ", "l", [IsPrefetch, VerifyRuntimeMode], MemEltTyInt64, "aarch64_sve_prf">;

let SVETargetGuard = "sve", SMETargetGuard = InvalidMode in {
// Prefetch (Vector bases)
def SVPRFB_GATHER_BASES : MInst<"svprfb_gather[_{2}base]", "vPdJ", "UiUl", [IsGatherPrefetch], MemEltTyInt8,  "aarch64_sve_prfb_gather_scalar_offset">;
def SVPRFH_GATHER_BASES : MInst<"svprfh_gather[_{2}base]", "vPdJ", "UiUl", [IsGatherPrefetch], MemEltTyInt16, "aarch64_sve_prfh_gather_scalar_offset">;
def SVPRFW_GATHER_BASES : MInst<"svprfw_gather[_{2}base]", "vPdJ", "UiUl", [IsGatherPrefetch], MemEltTyInt32, "aarch64_sve_prfw_gather_scalar_offset">;
def SVPRFD_GATHER_BASES : MInst<"svprfd_gather[_{2}base]", "vPdJ", "UiUl", [IsGatherPrefetch], MemEltTyInt64, "aarch64_sve_prfd_gather_scalar_offset">;

// Prefetch (Scalar base, Vector offsets)
def SVPRFB_GATHER_32B_OFFSETS_S : MInst<"svprfb_gather_[{3}]offset", "vPQdJ", "i",  [IsGatherPrefetch], MemEltTyInt8,  "aarch64_sve_prfb_gather_sxtw_index">;
def SVPRFH_GATHER_32B_OFFSETS_S : MInst<"svprfh_gather_[{3}]index",  "vPQdJ", "i",  [IsGatherPrefetch], MemEltTyInt16, "aarch64_sve_prfh_gather_sxtw_index">;
def SVPRFW_GATHER_32B_OFFSETS_S : MInst<"svprfw_gather_[{3}]index",  "vPQdJ", "i",  [IsGatherPrefetch], MemEltTyInt32, "aarch64_sve_prfw_gather_sxtw_index">;
def SVPRFD_GATHER_32B_OFFSETS_S : MInst<"svprfd_gather_[{3}]index",  "vPQdJ", "i",  [IsGatherPrefetch], MemEltTyInt64, "aarch64_sve_prfd_gather_sxtw_index">;

def SVPRFB_GATHER_64B_OFFSETS_S : MInst<"svprfb_gather_[{3}]offset", "vPQdJ", "l",  [IsGatherPrefetch], MemEltTyInt8,  "aarch64_sve_prfb_gather_index">;
def SVPRFH_GATHER_64B_OFFSETS_S : MInst<"svprfh_gather_[{3}]index",  "vPQdJ", "l",  [IsGatherPrefetch], MemEltTyInt16, "aarch64_sve_prfh_gather_index">;
def SVPRFW_GATHER_64B_OFFSETS_S : MInst<"svprfw_gather_[{3}]index",  "vPQdJ", "l",  [IsGatherPrefetch], MemEltTyInt32, "aarch64_sve_prfw_gather_index">;
def SVPRFD_GATHER_64B_OFFSETS_S : MInst<"svprfd_gather_[{3}]index",  "vPQdJ", "l",  [IsGatherPrefetch], MemEltTyInt64, "aarch64_sve_prfd_gather_index">;

def SVPRFB_GATHER_32B_OFFSETS_U : MInst<"svprfb_gather_[{3}]offset", "vPQdJ", "Ui", [IsGatherPrefetch], MemEltTyInt8,  "aarch64_sve_prfb_gather_uxtw_index">;
def SVPRFH_GATHER_32B_OFFSETS_U : MInst<"svprfh_gather_[{3}]index",  "vPQdJ", "Ui", [IsGatherPrefetch], MemEltTyInt16, "aarch64_sve_prfh_gather_uxtw_index">;
def SVPRFW_GATHER_32B_OFFSETS_U : MInst<"svprfw_gather_[{3}]index",  "vPQdJ", "Ui", [IsGatherPrefetch], MemEltTyInt32, "aarch64_sve_prfw_gather_uxtw_index">;
def SVPRFD_GATHER_32B_OFFSETS_U : MInst<"svprfd_gather_[{3}]index",  "vPQdJ", "Ui", [IsGatherPrefetch], MemEltTyInt64, "aarch64_sve_prfd_gather_uxtw_index">;

def SVPRFB_GATHER_64B_OFFSETS_U : MInst<"svprfb_gather_[{3}]offset", "vPQdJ", "Ul", [IsGatherPrefetch], MemEltTyInt8,  "aarch64_sve_prfb_gather_index">;
def SVPRFH_GATHER_64B_OFFSETS_U : MInst<"svprfh_gather_[{3}]index",  "vPQdJ", "Ul", [IsGatherPrefetch], MemEltTyInt16, "aarch64_sve_prfh_gather_index">;
def SVPRFW_GATHER_64B_OFFSETS_U : MInst<"svprfw_gather_[{3}]index",  "vPQdJ", "Ul", [IsGatherPrefetch], MemEltTyInt32, "aarch64_sve_prfw_gather_index">;
def SVPRFD_GATHER_64B_OFFSETS_U : MInst<"svprfd_gather_[{3}]index",  "vPQdJ", "Ul", [IsGatherPrefetch], MemEltTyInt64, "aarch64_sve_prfd_gather_index">;

// Prefetch (Vector bases, scalar offset)
def SVPRFB_GATHER_BASES_OFFSET : MInst<"svprfb_gather[_{2}base]_offset", "vPdlJ", "UiUl", [IsGatherPrefetch], MemEltTyInt8,  "aarch64_sve_prfb_gather_scalar_offset">;
def SVPRFH_GATHER_BASES_OFFSET : MInst<"svprfh_gather[_{2}base]_index",  "vPdlJ", "UiUl", [IsGatherPrefetch], MemEltTyInt16, "aarch64_sve_prfh_gather_scalar_offset">;
def SVPRFW_GATHER_BASES_OFFSET : MInst<"svprfw_gather[_{2}base]_index",  "vPdlJ", "UiUl", [IsGatherPrefetch], MemEltTyInt32, "aarch64_sve_prfw_gather_scalar_offset">;
def SVPRFD_GATHER_BASES_OFFSET : MInst<"svprfd_gather[_{2}base]_index",  "vPdlJ", "UiUl", [IsGatherPrefetch], MemEltTyInt64, "aarch64_sve_prfd_gather_scalar_offset">;
} // let SVETargetGuard = "sve"

////////////////////////////////////////////////////////////////////////////////
// Address calculations

let SVETargetGuard = "sve", SMETargetGuard = InvalidMode in {
def SVADRB : SInst<"svadrb[_{0}base]_[{2}]offset", "uud", "ilUiUl", MergeNone, "aarch64_sve_adrb">;
def SVADRH : SInst<"svadrh[_{0}base]_[{2}]index",  "uud", "ilUiUl", MergeNone, "aarch64_sve_adrh">;
def SVADRW : SInst<"svadrw[_{0}base]_[{2}]index",  "uud", "ilUiUl", MergeNone, "aarch64_sve_adrw">;
def SVADRD : SInst<"svadrd[_{0}base]_[{2}]index",  "uud", "ilUiUl", MergeNone, "aarch64_sve_adrd">;
} // let SVETargetGuard = "sve"

////////////////////////////////////////////////////////////////////////////////
// Scalar to vector

def SVDUPQ_8  : SInst<"svdupq[_n]_{d}", "dssssssssssssssss",  "cUc", MergeNone, "", [VerifyRuntimeMode]>;
def SVDUPQ_16 : SInst<"svdupq[_n]_{d}", "dssssssss",  "sUsh", MergeNone, "", [VerifyRuntimeMode]>;
let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  def SVDUPQ_BF16 : SInst<"svdupq[_n]_{d}", "dssssssss",  "b", MergeNone, "", [VerifyRuntimeMode]>;
}
def SVDUPQ_32 : SInst<"svdupq[_n]_{d}", "dssss",  "iUif", MergeNone, "", [VerifyRuntimeMode]>;
def SVDUPQ_64 : SInst<"svdupq[_n]_{d}", "dss",  "lUld", MergeNone, "", [VerifyRuntimeMode]>;

multiclass svdup_base<string n, string p, MergeType mt, string i> {
  def NAME : SInst<n, p, "csilUcUsUiUlhfd", mt, i, [VerifyRuntimeMode]>;
  let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
    def _BF16: SInst<n, p, "b", mt, i, [VerifyRuntimeMode]>;
  }
}

defm SVDUP   : svdup_base<"svdup[_n]_{d}", "ds",   MergeNone,    "aarch64_sve_dup_x">;
defm SVDUP_M : svdup_base<"svdup[_n]_{d}", "ddPs", MergeOp1,     "aarch64_sve_dup">;
defm SVDUP_X : svdup_base<"svdup[_n]_{d}", "dPs",  MergeAnyExp,  "aarch64_sve_dup">;
defm SVDUP_Z : svdup_base<"svdup[_n]_{d}", "dPs",  MergeZeroExp, "aarch64_sve_dup">;

def SVINDEX : SInst<"svindex_{d}",   "dss",  "csilUcUsUiUl",    MergeNone,    "aarch64_sve_index", [VerifyRuntimeMode]>;

// Integer arithmetic

multiclass SInstZPZ<string name, string types, string intrinsic> {
  def _M : SInst<name # "[_{d}]", "ddPd", types, MergeOp1,     intrinsic, [VerifyRuntimeMode]>;
  def _X : SInst<name # "[_{d}]", "dPd",  types, MergeAnyExp,  intrinsic, [VerifyRuntimeMode]>;
  def _Z : SInst<name # "[_{d}]", "dPd",  types, MergeZeroExp, intrinsic, [VerifyRuntimeMode]>;
}

defm SVABS : SInstZPZ<"svabs", "csil", "aarch64_sve_abs">;
defm SVNEG : SInstZPZ<"svneg", "csil", "aarch64_sve_neg">;

//------------------------------------------------------------------------------

multiclass SInstZPZZ<string name, string types, string m_intrinsic, string x_intrinsic, list<FlagType> flags=[]> {
  def _M   : SInst<name # "[_{d}]",   "dPdd", types, MergeOp1,  m_intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;
  def _X   : SInst<name # "[_{d}]",   "dPdd", types, MergeAny,  x_intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;
  def _Z   : SInst<name # "[_{d}]",   "dPdd", types, MergeZero, m_intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;

  def _N_M : SInst<name # "[_n_{d}]", "dPda", types, MergeOp1,  m_intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;
  def _N_X : SInst<name # "[_n_{d}]", "dPda", types, MergeAny,  x_intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;
  def _N_Z : SInst<name # "[_n_{d}]", "dPda", types, MergeZero, m_intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;
}

defm SVABD_S  : SInstZPZZ<"svabd",  "csil",         "aarch64_sve_sabd",  "aarch64_sve_sabd_u">;
defm SVABD_U  : SInstZPZZ<"svabd",  "UcUsUiUl",     "aarch64_sve_uabd",  "aarch64_sve_uabd_u">;
defm SVADD    : SInstZPZZ<"svadd",  "csilUcUsUiUl", "aarch64_sve_add",   "aarch64_sve_add_u">;
defm SVDIV_S  : SInstZPZZ<"svdiv",  "il",           "aarch64_sve_sdiv",  "aarch64_sve_sdiv_u">;
defm SVDIV_U  : SInstZPZZ<"svdiv",  "UiUl",         "aarch64_sve_udiv",  "aarch64_sve_udiv_u">;
defm SVDIVR_S : SInstZPZZ<"svdivr", "il",           "aarch64_sve_sdivr", "aarch64_sve_sdiv_u", [ReverseMergeAnyBinOp]>;
defm SVDIVR_U : SInstZPZZ<"svdivr", "UiUl",         "aarch64_sve_udivr", "aarch64_sve_udiv_u", [ReverseMergeAnyBinOp]>;
defm SVMAX_S  : SInstZPZZ<"svmax",  "csil",         "aarch64_sve_smax",  "aarch64_sve_smax_u">;
defm SVMAX_U  : SInstZPZZ<"svmax",  "UcUsUiUl",     "aarch64_sve_umax",  "aarch64_sve_umax_u">;
defm SVMIN_S  : SInstZPZZ<"svmin",  "csil",         "aarch64_sve_smin",  "aarch64_sve_smin_u">;
defm SVMIN_U  : SInstZPZZ<"svmin",  "UcUsUiUl",     "aarch64_sve_umin",  "aarch64_sve_umin_u">;
defm SVMUL    : SInstZPZZ<"svmul",  "csilUcUsUiUl", "aarch64_sve_mul",   "aarch64_sve_mul_u">;
defm SVMULH_S : SInstZPZZ<"svmulh", "csil",         "aarch64_sve_smulh", "aarch64_sve_smulh_u">;
defm SVMULH_U : SInstZPZZ<"svmulh", "UcUsUiUl",     "aarch64_sve_umulh", "aarch64_sve_umulh_u">;
defm SVSUB    : SInstZPZZ<"svsub",  "csilUcUsUiUl", "aarch64_sve_sub",   "aarch64_sve_sub_u">;
defm SVSUBR   : SInstZPZZ<"svsubr", "csilUcUsUiUl", "aarch64_sve_subr",  "aarch64_sve_sub_u", [ReverseMergeAnyBinOp]>;

//------------------------------------------------------------------------------

multiclass SInstZPZZZ<string name, string types, string m_intrinsic, string x_intrinsic, list<FlagType> flags=[]> {
  def _M   : SInst<name # "[_{d}]",   "dPddd", types, MergeOp1,  m_intrinsic, flags>;
  def _X   : SInst<name # "[_{d}]",   "dPddd", types, MergeAny,  x_intrinsic, flags>;
  def _Z   : SInst<name # "[_{d}]",   "dPddd", types, MergeZero, m_intrinsic, flags>;

  def _N_M : SInst<name # "[_n_{d}]", "dPdda", types, MergeOp1,  m_intrinsic, flags>;
  def _N_X : SInst<name # "[_n_{d}]", "dPdda", types, MergeAny,  x_intrinsic, flags>;
  def _N_Z : SInst<name # "[_n_{d}]", "dPdda", types, MergeZero, m_intrinsic, flags>;
}

defm SVMAD : SInstZPZZZ<"svmad", "csilUcUsUiUl", "aarch64_sve_mad", "aarch64_sve_mla_u", [ReverseMergeAnyAccOp, VerifyRuntimeMode]>;
defm SVMLA : SInstZPZZZ<"svmla", "csilUcUsUiUl", "aarch64_sve_mla", "aarch64_sve_mla_u", [VerifyRuntimeMode]>;
defm SVMLS : SInstZPZZZ<"svmls", "csilUcUsUiUl", "aarch64_sve_mls", "aarch64_sve_mls_u", [VerifyRuntimeMode]>;
defm SVMSB : SInstZPZZZ<"svmsb", "csilUcUsUiUl", "aarch64_sve_msb", "aarch64_sve_mls_u", [ReverseMergeAnyAccOp, VerifyRuntimeMode]>;

//------------------------------------------------------------------------------

def SVDOT_S    : SInst<"svdot[_{0}]",    "ddqq", "il",       MergeNone, "aarch64_sve_sdot", [VerifyRuntimeMode]>;
def SVDOT_U    : SInst<"svdot[_{0}]",    "ddqq", "UiUl",     MergeNone, "aarch64_sve_udot", [VerifyRuntimeMode]>;
def SVQADD_S   : SInst<"svqadd[_{d}]",   "ddd",  "csil",     MergeNone, "aarch64_sve_sqadd_x", [VerifyRuntimeMode]>;
def SVQADD_U   : SInst<"svqadd[_{d}]",   "ddd",  "UcUsUiUl", MergeNone, "aarch64_sve_uqadd_x", [VerifyRuntimeMode]>;
def SVQSUB_S   : SInst<"svqsub[_{d}]",   "ddd",  "csil",     MergeNone, "aarch64_sve_sqsub_x", [VerifyRuntimeMode]>;
def SVQSUB_U   : SInst<"svqsub[_{d}]",   "ddd",  "UcUsUiUl", MergeNone, "aarch64_sve_uqsub_x", [VerifyRuntimeMode]>;

def SVDOT_N_S  : SInst<"svdot[_n_{0}]",  "ddqr", "il",       MergeNone, "aarch64_sve_sdot", [VerifyRuntimeMode]>;
def SVDOT_N_U  : SInst<"svdot[_n_{0}]",  "ddqr", "UiUl",     MergeNone, "aarch64_sve_udot", [VerifyRuntimeMode]>;
def SVQADD_N_S : SInst<"svqadd[_n_{d}]", "dda",  "csil",     MergeNone, "aarch64_sve_sqadd_x", [VerifyRuntimeMode]>;
def SVQADD_N_U : SInst<"svqadd[_n_{d}]", "dda",  "UcUsUiUl", MergeNone, "aarch64_sve_uqadd_x", [VerifyRuntimeMode]>;
def SVQSUB_N_S : SInst<"svqsub[_n_{d}]", "dda",  "csil",     MergeNone, "aarch64_sve_sqsub_x", [VerifyRuntimeMode]>;
def SVQSUB_N_U : SInst<"svqsub[_n_{d}]", "dda",  "UcUsUiUl", MergeNone, "aarch64_sve_uqsub_x", [VerifyRuntimeMode]>;

def SVDOT_LANE_S : SInst<"svdot_lane[_{d}]",  "ddqqi",  "il",   MergeNone, "aarch64_sve_sdot_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndexDot, 2>]>;
def SVDOT_LANE_U : SInst<"svdot_lane[_{d}]",  "ddqqi",  "UiUl", MergeNone, "aarch64_sve_udot_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndexDot, 2>]>;

////////////////////////////////////////////////////////////////////////////////
// Logical operations

defm SVAND  : SInstZPZZ<"svand", "csilUcUsUiUl", "aarch64_sve_and", "aarch64_sve_and_u">;
defm SVBIC  : SInstZPZZ<"svbic", "csilUcUsUiUl", "aarch64_sve_bic", "aarch64_sve_bic_u">;
defm SVEOR  : SInstZPZZ<"sveor", "csilUcUsUiUl", "aarch64_sve_eor", "aarch64_sve_eor_u">;
defm SVORR  : SInstZPZZ<"svorr", "csilUcUsUiUl", "aarch64_sve_orr", "aarch64_sve_orr_u">;

defm SVCNOT : SInstZPZ<"svcnot", "csilUcUsUiUl", "aarch64_sve_cnot">;
defm SVNOT  : SInstZPZ<"svnot",  "csilUcUsUiUl", "aarch64_sve_not">;

////////////////////////////////////////////////////////////////////////////////
// Shifts

multiclass SInst_SHIFT<string name, string intrinsic, string ts, string wide_ts> {
  def _M : SInst<name # "[_{d}]", "dPdu", ts, MergeOp1,  intrinsic, [VerifyRuntimeMode]>;
  def _X : SInst<name # "[_{d}]", "dPdu", ts, MergeAny,  intrinsic # _u, [VerifyRuntimeMode]>;
  def _Z : SInst<name # "[_{d}]", "dPdu", ts, MergeZero, intrinsic, [VerifyRuntimeMode]>;

  def _N_M : SInst<name # "[_n_{d}]", "dPdL", ts, MergeOp1,  intrinsic, [VerifyRuntimeMode]>;
  def _N_X : SInst<name # "[_n_{d}]", "dPdL", ts, MergeAny,  intrinsic # _u, [VerifyRuntimeMode]>;
  def _N_Z : SInst<name # "[_n_{d}]", "dPdL", ts, MergeZero, intrinsic, [VerifyRuntimeMode]>;

  def _WIDE_M : SInst<name # _wide # "[_{d}]", "dPdg", wide_ts, MergeOp1,  intrinsic # _wide, [VerifyRuntimeMode]>;
  def _WIDE_X : SInst<name # _wide # "[_{d}]", "dPdg", wide_ts, MergeAny,  intrinsic # _wide, [VerifyRuntimeMode]>;
  def _WIDE_Z : SInst<name # _wide # "[_{d}]", "dPdg", wide_ts, MergeZero, intrinsic # _wide, [VerifyRuntimeMode]>;

  def _WIDE_N_M : SInst<name # _wide # "[_n_{d}]", "dPdf", wide_ts, MergeOp1,  intrinsic # _wide, [VerifyRuntimeMode]>;
  def _WIDE_N_X : SInst<name # _wide # "[_n_{d}]", "dPdf", wide_ts, MergeAny,  intrinsic # _wide, [VerifyRuntimeMode]>;
  def _WIDE_N_Z : SInst<name # _wide # "[_n_{d}]", "dPdf", wide_ts, MergeZero, intrinsic # _wide, [VerifyRuntimeMode]>;
}

defm SVASR : SInst_SHIFT<"svasr", "aarch64_sve_asr", "csil", "csi">;
defm SVLSL : SInst_SHIFT<"svlsl", "aarch64_sve_lsl", "csilUcUsUiUl", "csiUcUsUi">;
defm SVLSR : SInst_SHIFT<"svlsr", "aarch64_sve_lsr", "UcUsUiUl", "UcUsUi">;

def SVASRD_M : SInst<"svasrd[_n_{d}]", "dPdi", "csil",            MergeOp1,  "aarch64_sve_asrd", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVASRD_X : SInst<"svasrd[_n_{d}]", "dPdi", "csil",            MergeAny,  "aarch64_sve_asrd", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVASRD_Z : SInst<"svasrd[_n_{d}]", "dPdi", "csil",            MergeZero, "aarch64_sve_asrd", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;

def SVINSR : SInst<"svinsr[_n_{d}]", "dds", "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_insr", [VerifyRuntimeMode]>;
let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  def SVINSR_BF16 : SInst<"svinsr[_n_{d}]", "dds",  "b", MergeNone, "aarch64_sve_insr", [VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// Integer reductions

def SVADDV_S : SInst<"svaddv[_{d}]", "lPd", "csil",         MergeNone, "aarch64_sve_saddv", [VerifyRuntimeMode]>;
def SVADDV_U : SInst<"svaddv[_{d}]", "nPd", "UcUsUiUl",     MergeNone, "aarch64_sve_uaddv", [VerifyRuntimeMode]>;
def SVANDV   : SInst<"svandv[_{d}]", "sPd", "csilUcUsUiUl", MergeNone, "aarch64_sve_andv", [VerifyRuntimeMode]>;
def SVEORV   : SInst<"sveorv[_{d}]", "sPd", "csilUcUsUiUl", MergeNone, "aarch64_sve_eorv", [VerifyRuntimeMode]>;
def SVMAXV_S : SInst<"svmaxv[_{d}]", "sPd", "csil",         MergeNone, "aarch64_sve_smaxv", [VerifyRuntimeMode]>;
def SVMAXV_U : SInst<"svmaxv[_{d}]", "sPd", "UcUsUiUl",     MergeNone, "aarch64_sve_umaxv", [VerifyRuntimeMode]>;
def SVMINV_S : SInst<"svminv[_{d}]", "sPd", "csil",         MergeNone, "aarch64_sve_sminv", [VerifyRuntimeMode]>;
def SVMINV_U : SInst<"svminv[_{d}]", "sPd", "UcUsUiUl",     MergeNone, "aarch64_sve_uminv", [VerifyRuntimeMode]>;
def SVORV    : SInst<"svorv[_{d}]",  "sPd", "csilUcUsUiUl", MergeNone, "aarch64_sve_orv", [VerifyRuntimeMode]>;

////////////////////////////////////////////////////////////////////////////////
// Integer comparisons

def SVCMPEQ : SInst<"svcmpeq[_{d}]", "PPdd", "csilUcUsUiUl", MergeNone, "aarch64_sve_cmpeq", [VerifyRuntimeMode]>;
def SVCMPNE : SInst<"svcmpne[_{d}]", "PPdd", "csilUcUsUiUl", MergeNone, "aarch64_sve_cmpne", [VerifyRuntimeMode]>;
def SVCMPGE : SInst<"svcmpge[_{d}]", "PPdd", "csil",         MergeNone, "aarch64_sve_cmpge", [VerifyRuntimeMode]>;
def SVCMPGT : SInst<"svcmpgt[_{d}]", "PPdd", "csil",         MergeNone, "aarch64_sve_cmpgt", [VerifyRuntimeMode]>;
def SVCMPLE : SInst<"svcmple[_{d}]", "PPdd", "csil",         MergeNone, "aarch64_sve_cmpge", [ReverseCompare, VerifyRuntimeMode]>;
def SVCMPLT : SInst<"svcmplt[_{d}]", "PPdd", "csil",         MergeNone, "aarch64_sve_cmpgt", [ReverseCompare, VerifyRuntimeMode]>;
def SVCMPHI : SInst<"svcmpgt[_{d}]", "PPdd", "UcUsUiUl",     MergeNone, "aarch64_sve_cmphi", [VerifyRuntimeMode]>;
def SVCMPHS : SInst<"svcmpge[_{d}]", "PPdd", "UcUsUiUl",     MergeNone, "aarch64_sve_cmphs", [VerifyRuntimeMode]>;
def SVCMPLO : SInst<"svcmplt[_{d}]", "PPdd", "UcUsUiUl",     MergeNone, "aarch64_sve_cmphi", [ReverseCompare, VerifyRuntimeMode]>;
def SVCMPLS : SInst<"svcmple[_{d}]", "PPdd", "UcUsUiUl",     MergeNone, "aarch64_sve_cmphs", [ReverseCompare, VerifyRuntimeMode]>;

def SVCMPEQ_N : SInst<"svcmpeq[_n_{d}]", "PPda", "csilUcUsUiUl", MergeNone, "aarch64_sve_cmpeq", [VerifyRuntimeMode]>;
def SVCMPNE_N : SInst<"svcmpne[_n_{d}]", "PPda", "csilUcUsUiUl", MergeNone, "aarch64_sve_cmpne", [VerifyRuntimeMode]>;
def SVCMPGE_N : SInst<"svcmpge[_n_{d}]", "PPda", "csil",         MergeNone, "aarch64_sve_cmpge", [VerifyRuntimeMode]>;
def SVCMPGT_N : SInst<"svcmpgt[_n_{d}]", "PPda", "csil",         MergeNone, "aarch64_sve_cmpgt", [VerifyRuntimeMode]>;
def SVCMPLE_N : SInst<"svcmple[_n_{d}]", "PPda", "csil",         MergeNone, "aarch64_sve_cmpge", [ReverseCompare, VerifyRuntimeMode]>;
def SVCMPLT_N : SInst<"svcmplt[_n_{d}]", "PPda", "csil",         MergeNone, "aarch64_sve_cmpgt", [ReverseCompare, VerifyRuntimeMode]>;
def SVCMPHS_N : SInst<"svcmpge[_n_{d}]", "PPda", "UcUsUiUl",     MergeNone, "aarch64_sve_cmphs", [VerifyRuntimeMode]>;
def SVCMPHI_N : SInst<"svcmpgt[_n_{d}]", "PPda", "UcUsUiUl",     MergeNone, "aarch64_sve_cmphi", [VerifyRuntimeMode]>;
def SVCMPLS_N : SInst<"svcmple[_n_{d}]", "PPda", "UcUsUiUl",     MergeNone, "aarch64_sve_cmphs", [ReverseCompare, VerifyRuntimeMode]>;
def SVCMPLO_N : SInst<"svcmplt[_n_{d}]", "PPda", "UcUsUiUl",     MergeNone, "aarch64_sve_cmphi", [ReverseCompare, VerifyRuntimeMode]>;

def SVCMPEQ_WIDE : SInst<"svcmpeq_wide[_{d}]", "PPdw", "csi",    MergeNone, "aarch64_sve_cmpeq_wide", [VerifyRuntimeMode]>;
def SVCMPNE_WIDE : SInst<"svcmpne_wide[_{d}]", "PPdw", "csi",    MergeNone, "aarch64_sve_cmpne_wide", [VerifyRuntimeMode]>;
def SVCMPGE_WIDE : SInst<"svcmpge_wide[_{d}]", "PPdw", "csi",    MergeNone, "aarch64_sve_cmpge_wide", [VerifyRuntimeMode]>;
def SVCMPGT_WIDE : SInst<"svcmpgt_wide[_{d}]", "PPdw", "csi",    MergeNone, "aarch64_sve_cmpgt_wide", [VerifyRuntimeMode]>;
def SVCMPLE_WIDE : SInst<"svcmple_wide[_{d}]", "PPdw", "csi",    MergeNone, "aarch64_sve_cmple_wide", [VerifyRuntimeMode]>;
def SVCMPLT_WIDE : SInst<"svcmplt_wide[_{d}]", "PPdw", "csi",    MergeNone, "aarch64_sve_cmplt_wide", [VerifyRuntimeMode]>;
def SVCMPHI_WIDE : SInst<"svcmpgt_wide[_{d}]", "PPdw", "UcUsUi", MergeNone, "aarch64_sve_cmphi_wide", [VerifyRuntimeMode]>;
def SVCMPHS_WIDE : SInst<"svcmpge_wide[_{d}]", "PPdw", "UcUsUi", MergeNone, "aarch64_sve_cmphs_wide", [VerifyRuntimeMode]>;
def SVCMPLO_WIDE : SInst<"svcmplt_wide[_{d}]", "PPdw", "UcUsUi", MergeNone, "aarch64_sve_cmplo_wide", [VerifyRuntimeMode]>;
def SVCMPLS_WIDE : SInst<"svcmple_wide[_{d}]", "PPdw", "UcUsUi", MergeNone, "aarch64_sve_cmpls_wide", [VerifyRuntimeMode]>;

def SVCMPEQ_WIDE_N : SInst<"svcmpeq_wide[_n_{d}]", "PPdj", "csi",    MergeNone, "aarch64_sve_cmpeq_wide", [VerifyRuntimeMode]>;
def SVCMPNE_WIDE_N : SInst<"svcmpne_wide[_n_{d}]", "PPdj", "csi",    MergeNone, "aarch64_sve_cmpne_wide", [VerifyRuntimeMode]>;
def SVCMPGE_WIDE_N : SInst<"svcmpge_wide[_n_{d}]", "PPdj", "csi",    MergeNone, "aarch64_sve_cmpge_wide", [VerifyRuntimeMode]>;
def SVCMPGT_WIDE_N : SInst<"svcmpgt_wide[_n_{d}]", "PPdj", "csi",    MergeNone, "aarch64_sve_cmpgt_wide", [VerifyRuntimeMode]>;
def SVCMPLE_WIDE_N : SInst<"svcmple_wide[_n_{d}]", "PPdj", "csi",    MergeNone, "aarch64_sve_cmple_wide", [VerifyRuntimeMode]>;
def SVCMPLT_WIDE_N : SInst<"svcmplt_wide[_n_{d}]", "PPdj", "csi",    MergeNone, "aarch64_sve_cmplt_wide", [VerifyRuntimeMode]>;
def SVCMPHS_WIDE_N : SInst<"svcmpge_wide[_n_{d}]", "PPdj", "UcUsUi", MergeNone, "aarch64_sve_cmphs_wide", [VerifyRuntimeMode]>;
def SVCMPHI_WIDE_N : SInst<"svcmpgt_wide[_n_{d}]", "PPdj", "UcUsUi", MergeNone, "aarch64_sve_cmphi_wide", [VerifyRuntimeMode]>;
def SVCMPLO_WIDE_N : SInst<"svcmplt_wide[_n_{d}]", "PPdj", "UcUsUi", MergeNone, "aarch64_sve_cmplo_wide", [VerifyRuntimeMode]>;
def SVCMPLS_WIDE_N : SInst<"svcmple_wide[_n_{d}]", "PPdj", "UcUsUi", MergeNone, "aarch64_sve_cmpls_wide", [VerifyRuntimeMode]>;

////////////////////////////////////////////////////////////////////////////////
// While comparisons

def SVWHILELE_S32 : SInst<"svwhilele_{d}[_{1}]", "Pkk", "PcPsPiPl",     MergeNone, "aarch64_sve_whilele", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILELE_S64 : SInst<"svwhilele_{d}[_{1}]", "Pll", "PcPsPiPl",     MergeNone, "aarch64_sve_whilele", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILELO_U32 : SInst<"svwhilelt_{d}[_{1}]", "Pmm", "PUcPUsPUiPUl", MergeNone, "aarch64_sve_whilelo", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILELO_U64 : SInst<"svwhilelt_{d}[_{1}]", "Pnn", "PUcPUsPUiPUl", MergeNone, "aarch64_sve_whilelo", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILELS_U32 : SInst<"svwhilele_{d}[_{1}]", "Pmm", "PUcPUsPUiPUl", MergeNone, "aarch64_sve_whilels", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILELS_U64 : SInst<"svwhilele_{d}[_{1}]", "Pnn", "PUcPUsPUiPUl", MergeNone, "aarch64_sve_whilels", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILELT_S32 : SInst<"svwhilelt_{d}[_{1}]", "Pkk", "PcPsPiPl",     MergeNone, "aarch64_sve_whilelt", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILELT_S64 : SInst<"svwhilelt_{d}[_{1}]", "Pll", "PcPsPiPl",     MergeNone, "aarch64_sve_whilelt", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;

////////////////////////////////////////////////////////////////////////////////
// Counting bit

multiclass SInstCLS<string name, string types, string intrinsic, list<FlagType> flags=[]> {
  def _M : SInst<name # "[_{d}]", "uuPd", types, MergeOp1,     intrinsic, flags>;
  def _X : SInst<name # "[_{d}]", "uPd",  types, MergeAnyExp,  intrinsic, flags>;
  def _Z : SInst<name # "[_{d}]", "uPd",  types, MergeZeroExp, intrinsic, flags>;
}

defm SVCLS : SInstCLS<"svcls", "csil",            "aarch64_sve_cls", [VerifyRuntimeMode]>;
defm SVCLZ : SInstCLS<"svclz", "csilUcUsUiUl",    "aarch64_sve_clz", [VerifyRuntimeMode]>;
defm SVCNT : SInstCLS<"svcnt", "csilUcUsUiUlhfd", "aarch64_sve_cnt", [VerifyRuntimeMode]>;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  defm SVCNT_BF16 : SInstCLS<"svcnt", "b", "aarch64_sve_cnt", [VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// Conversion

defm SVEXTB_S : SInstZPZ<"svextb", "sil",    "aarch64_sve_sxtb">;
defm SVEXTB_U : SInstZPZ<"svextb", "UsUiUl", "aarch64_sve_uxtb">;
defm SVEXTH_S : SInstZPZ<"svexth", "il",     "aarch64_sve_sxth">;
defm SVEXTH_U : SInstZPZ<"svexth", "UiUl",   "aarch64_sve_uxth">;
defm SVEXTW_S : SInstZPZ<"svextw", "l",      "aarch64_sve_sxtw">;
defm SVEXTW_U : SInstZPZ<"svextw", "Ul",     "aarch64_sve_uxtw">;

////////////////////////////////////////////////////////////////////////////////
// Reversal

defm SVRBIT : SInstZPZ<"svrbit", "csilUcUsUiUl", "aarch64_sve_rbit">;
defm SVREVB : SInstZPZ<"svrevb", "silUsUiUl",    "aarch64_sve_revb">;
defm SVREVH : SInstZPZ<"svrevh", "ilUiUl",       "aarch64_sve_revh">;
defm SVREVW : SInstZPZ<"svrevw", "lUl",          "aarch64_sve_revw">;

////////////////////////////////////////////////////////////////////////////////
// Floating-point arithmetic

defm SVABS_F : SInstZPZ<"svabs", "hfd", "aarch64_sve_fabs">;
defm SVNEG_F : SInstZPZ<"svneg", "hfd", "aarch64_sve_fneg">;

defm SVABD_F  : SInstZPZZ<"svabd",  "hfd", "aarch64_sve_fabd",   "aarch64_sve_fabd_u">;
defm SVADD_F  : SInstZPZZ<"svadd",  "hfd", "aarch64_sve_fadd",   "aarch64_sve_fadd_u">;
defm SVDIV_F  : SInstZPZZ<"svdiv",  "hfd", "aarch64_sve_fdiv",   "aarch64_sve_fdiv_u">;
defm SVDIVR_F : SInstZPZZ<"svdivr", "hfd", "aarch64_sve_fdivr",  "aarch64_sve_fdiv_u", [ReverseMergeAnyBinOp]>;
defm SVMAX_F  : SInstZPZZ<"svmax",  "hfd", "aarch64_sve_fmax",   "aarch64_sve_fmax_u">;
defm SVMAXNM  : SInstZPZZ<"svmaxnm","hfd", "aarch64_sve_fmaxnm", "aarch64_sve_fmaxnm_u">;
defm SVMIN_F  : SInstZPZZ<"svmin",  "hfd", "aarch64_sve_fmin",   "aarch64_sve_fmin_u">;
defm SVMINNM  : SInstZPZZ<"svminnm","hfd", "aarch64_sve_fminnm", "aarch64_sve_fminnm_u">;
defm SVMUL_F  : SInstZPZZ<"svmul",  "hfd", "aarch64_sve_fmul",   "aarch64_sve_fmul_u">;
defm SVMULX   : SInstZPZZ<"svmulx", "hfd", "aarch64_sve_fmulx",  "aarch64_sve_fmulx_u">;
defm SVSUB_F  : SInstZPZZ<"svsub",  "hfd", "aarch64_sve_fsub",   "aarch64_sve_fsub_u">;
defm SVSUBR_F : SInstZPZZ<"svsubr", "hfd", "aarch64_sve_fsubr",  "aarch64_sve_fsub_u", [ReverseMergeAnyBinOp]>;

defm SVRECPX : SInstZPZ<"svrecpx", "hfd", "aarch64_sve_frecpx">;
defm SVRINTA : SInstZPZ<"svrinta", "hfd", "aarch64_sve_frinta">;
defm SVRINTI : SInstZPZ<"svrinti", "hfd", "aarch64_sve_frinti">;
defm SVRINTM : SInstZPZ<"svrintm", "hfd", "aarch64_sve_frintm">;
defm SVRINTN : SInstZPZ<"svrintn", "hfd", "aarch64_sve_frintn">;
defm SVRINTP : SInstZPZ<"svrintp", "hfd", "aarch64_sve_frintp">;
defm SVRINTX : SInstZPZ<"svrintx", "hfd", "aarch64_sve_frintx">;
defm SVRINTZ : SInstZPZ<"svrintz", "hfd", "aarch64_sve_frintz">;
defm SVSQRT  : SInstZPZ<"svsqrt",  "hfd", "aarch64_sve_fsqrt">;

let SVETargetGuard = "sve", SMETargetGuard = InvalidMode in {
def SVEXPA  : SInst<"svexpa[_{d}]",  "du",   "hfd", MergeNone, "aarch64_sve_fexpa_x">;
def SVTMAD  : SInst<"svtmad[_{d}]",  "dddi", "hfd", MergeNone, "aarch64_sve_ftmad_x", [], [ImmCheck<2, ImmCheck0_7>]>;
def SVTSMUL : SInst<"svtsmul[_{d}]", "ddu",  "hfd", MergeNone, "aarch64_sve_ftsmul_x">;
def SVTSSEL : SInst<"svtssel[_{d}]", "ddu",  "hfd", MergeNone, "aarch64_sve_ftssel_x">;
}

def SVSCALE_M   : SInst<"svscale[_{d}]",   "dPdx", "hfd", MergeOp1,  "aarch64_sve_fscale", [VerifyRuntimeMode]>;
def SVSCALE_X   : SInst<"svscale[_{d}]",   "dPdx", "hfd", MergeAny,  "aarch64_sve_fscale", [VerifyRuntimeMode]>;
def SVSCALE_Z   : SInst<"svscale[_{d}]",   "dPdx", "hfd", MergeZero, "aarch64_sve_fscale", [VerifyRuntimeMode]>;

def SVSCALE_N_M : SInst<"svscale[_n_{d}]", "dPdK", "hfd", MergeOp1,  "aarch64_sve_fscale", [VerifyRuntimeMode]>;
def SVSCALE_N_X : SInst<"svscale[_n_{d}]", "dPdK", "hfd", MergeAny,  "aarch64_sve_fscale", [VerifyRuntimeMode]>;
def SVSCALE_N_Z : SInst<"svscale[_n_{d}]", "dPdK", "hfd", MergeZero, "aarch64_sve_fscale", [VerifyRuntimeMode]>;

defm SVMAD_F  : SInstZPZZZ<"svmad",  "hfd", "aarch64_sve_fmad",  "aarch64_sve_fmla_u",  [VerifyRuntimeMode, ReverseMergeAnyAccOp]>;
defm SVMLA_F  : SInstZPZZZ<"svmla",  "hfd", "aarch64_sve_fmla",  "aarch64_sve_fmla_u", [VerifyRuntimeMode]>;
defm SVMLS_F  : SInstZPZZZ<"svmls",  "hfd", "aarch64_sve_fmls",  "aarch64_sve_fmls_u", [VerifyRuntimeMode]>;
defm SVMSB_F  : SInstZPZZZ<"svmsb",  "hfd", "aarch64_sve_fmsb",  "aarch64_sve_fmls_u",  [VerifyRuntimeMode, ReverseMergeAnyAccOp]>;
defm SVNMAD_F : SInstZPZZZ<"svnmad", "hfd", "aarch64_sve_fnmad", "aarch64_sve_fnmla_u", [VerifyRuntimeMode, ReverseMergeAnyAccOp]>;
defm SVNMLA_F : SInstZPZZZ<"svnmla", "hfd", "aarch64_sve_fnmla", "aarch64_sve_fnmla_u", [VerifyRuntimeMode]>;
defm SVNMLS_F : SInstZPZZZ<"svnmls", "hfd", "aarch64_sve_fnmls", "aarch64_sve_fnmls_u", [VerifyRuntimeMode]>;
defm SVNMSB_F : SInstZPZZZ<"svnmsb", "hfd", "aarch64_sve_fnmsb", "aarch64_sve_fnmls_u", [VerifyRuntimeMode, ReverseMergeAnyAccOp]>;

def SVCADD_M : SInst<"svcadd[_{d}]", "dPddi",  "hfd", MergeOp1,  "aarch64_sve_fcadd", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckComplexRot90_270>]>;
def SVCADD_X : SInst<"svcadd[_{d}]", "dPddi",  "hfd", MergeAny,  "aarch64_sve_fcadd", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckComplexRot90_270>]>;
def SVCADD_Z : SInst<"svcadd[_{d}]", "dPddi",  "hfd", MergeZero, "aarch64_sve_fcadd", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckComplexRot90_270>]>;
def SVCMLA_M : SInst<"svcmla[_{d}]", "dPdddi", "hfd", MergeOp1,  "aarch64_sve_fcmla", [VerifyRuntimeMode], [ImmCheck<4, ImmCheckComplexRotAll90>]>;
def SVCMLA_X : SInst<"svcmla[_{d}]", "dPdddi", "hfd", MergeAny,  "aarch64_sve_fcmla", [VerifyRuntimeMode], [ImmCheck<4, ImmCheckComplexRotAll90>]>;
def SVCMLA_Z : SInst<"svcmla[_{d}]", "dPdddi", "hfd", MergeZero, "aarch64_sve_fcmla", [VerifyRuntimeMode], [ImmCheck<4, ImmCheckComplexRotAll90>]>;

def SVCMLA_LANE : SInst<"svcmla_lane[_{d}]", "ddddii", "hf",  MergeNone, "aarch64_sve_fcmla_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndexCompRotate, 2>,
                                                                                                        ImmCheck<4, ImmCheckComplexRotAll90>]>;
def SVMLA_LANE  : SInst<"svmla_lane[_{d}]",  "ddddi",  "hfd", MergeNone, "aarch64_sve_fmla_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLS_LANE  : SInst<"svmls_lane[_{d}]",  "ddddi",  "hfd", MergeNone, "aarch64_sve_fmls_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMUL_LANE  : SInst<"svmul_lane[_{d}]",  "dddi",   "hfd", MergeNone, "aarch64_sve_fmul_lane", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;

def SVRECPE  : SInst<"svrecpe[_{d}]",  "dd",  "hfd", MergeNone, "aarch64_sve_frecpe_x", [VerifyRuntimeMode]>;
def SVRECPS  : SInst<"svrecps[_{d}]",  "ddd", "hfd", MergeNone, "aarch64_sve_frecps_x", [VerifyRuntimeMode]>;
def SVRSQRTE : SInst<"svrsqrte[_{d}]", "dd",  "hfd", MergeNone, "aarch64_sve_frsqrte_x", [VerifyRuntimeMode]>;
def SVRSQRTS : SInst<"svrsqrts[_{d}]", "ddd", "hfd", MergeNone, "aarch64_sve_frsqrts_x", [VerifyRuntimeMode]>;

////////////////////////////////////////////////////////////////////////////////
// Floating-point reductions

def SVFADDA   : SInst<"svadda[_{d}]",   "sPsd", "hfd", MergeNone, "aarch64_sve_fadda", [VerifyRuntimeMode]>;
def SVFADDV   : SInst<"svaddv[_{d}]",   "sPd",  "hfd", MergeNone, "aarch64_sve_faddv", [VerifyRuntimeMode]>;
def SVFMAXV   : SInst<"svmaxv[_{d}]",   "sPd",  "hfd", MergeNone, "aarch64_sve_fmaxv", [VerifyRuntimeMode]>;
def SVFMAXNMV : SInst<"svmaxnmv[_{d}]", "sPd",  "hfd", MergeNone, "aarch64_sve_fmaxnmv", [VerifyRuntimeMode]>;
def SVFMINV   : SInst<"svminv[_{d}]",   "sPd",  "hfd", MergeNone, "aarch64_sve_fminv", [VerifyRuntimeMode]>;
def SVFMINNMV : SInst<"svminnmv[_{d}]", "sPd",  "hfd", MergeNone, "aarch64_sve_fminnmv", [VerifyRuntimeMode]>;

////////////////////////////////////////////////////////////////////////////////
// Floating-point comparisons

def SVACGE  : SInst<"svacge[_{d}]",  "PPdd", "hfd", MergeNone, "aarch64_sve_facge", [VerifyRuntimeMode]>;
def SVACGT  : SInst<"svacgt[_{d}]",  "PPdd", "hfd", MergeNone, "aarch64_sve_facgt", [VerifyRuntimeMode]>;
def SVACLE  : SInst<"svacle[_{d}]",  "PPdd", "hfd", MergeNone, "aarch64_sve_facge", [ReverseCompare, VerifyRuntimeMode]>;
def SVACLT  : SInst<"svaclt[_{d}]",  "PPdd", "hfd", MergeNone, "aarch64_sve_facgt", [ReverseCompare, VerifyRuntimeMode]>;
def SVCMPUO : SInst<"svcmpuo[_{d}]", "PPdd", "hfd", MergeNone, "aarch64_sve_fcmpuo", [VerifyRuntimeMode]>;

def SVACGE_N  : SInst<"svacge[_n_{d}]",  "PPda", "hfd", MergeNone, "aarch64_sve_facge", [VerifyRuntimeMode]>;
def SVACGT_N  : SInst<"svacgt[_n_{d}]",  "PPda", "hfd", MergeNone, "aarch64_sve_facgt", [VerifyRuntimeMode]>;
def SVACLE_N  : SInst<"svacle[_n_{d}]",  "PPda", "hfd", MergeNone, "aarch64_sve_facge", [ReverseCompare, VerifyRuntimeMode]>;
def SVACLT_N  : SInst<"svaclt[_n_{d}]",  "PPda", "hfd", MergeNone, "aarch64_sve_facgt", [ReverseCompare, VerifyRuntimeMode]>;
def SVCMPUO_N : SInst<"svcmpuo[_n_{d}]", "PPda", "hfd", MergeNone, "aarch64_sve_fcmpuo", [VerifyRuntimeMode]>;

def SVCMPEQ_F : SInst<"svcmpeq[_{d}]", "PPdd", "hfd", MergeNone, "aarch64_sve_fcmpeq", [VerifyRuntimeMode]>;
def SVCMPNE_F : SInst<"svcmpne[_{d}]", "PPdd", "hfd", MergeNone, "aarch64_sve_fcmpne", [VerifyRuntimeMode]>;
def SVCMPGE_F : SInst<"svcmpge[_{d}]", "PPdd", "hfd", MergeNone, "aarch64_sve_fcmpge", [VerifyRuntimeMode]>;
def SVCMPGT_F : SInst<"svcmpgt[_{d}]", "PPdd", "hfd", MergeNone, "aarch64_sve_fcmpgt", [VerifyRuntimeMode]>;
def SVCMPLE_F : SInst<"svcmple[_{d}]", "PPdd", "hfd", MergeNone, "aarch64_sve_fcmpge", [ReverseCompare, VerifyRuntimeMode]>;
def SVCMPLT_F : SInst<"svcmplt[_{d}]", "PPdd", "hfd", MergeNone, "aarch64_sve_fcmpgt", [ReverseCompare, VerifyRuntimeMode]>;

def SVCMPEQ_F_N : SInst<"svcmpeq[_n_{d}]", "PPda", "hfd", MergeNone, "aarch64_sve_fcmpeq", [VerifyRuntimeMode]>;
def SVCMPNE_F_N : SInst<"svcmpne[_n_{d}]", "PPda", "hfd", MergeNone, "aarch64_sve_fcmpne", [VerifyRuntimeMode]>;
def SVCMPGE_F_N : SInst<"svcmpge[_n_{d}]", "PPda", "hfd", MergeNone, "aarch64_sve_fcmpge", [VerifyRuntimeMode]>;
def SVCMPGT_F_N : SInst<"svcmpgt[_n_{d}]", "PPda", "hfd", MergeNone, "aarch64_sve_fcmpgt", [VerifyRuntimeMode]>;
def SVCMPLE_F_N : SInst<"svcmple[_n_{d}]", "PPda", "hfd", MergeNone, "aarch64_sve_fcmpge", [ReverseCompare, VerifyRuntimeMode]>;
def SVCMPLT_F_N : SInst<"svcmplt[_n_{d}]", "PPda", "hfd", MergeNone, "aarch64_sve_fcmpgt", [ReverseCompare, VerifyRuntimeMode]>;

////////////////////////////////////////////////////////////////////////////////
// Floating-point conversions

multiclass SInstCvtMXZ<
    string name, string m_types, string xz_types, string types,
    string intrinsic, list<FlagType> flags = [IsOverloadNone]> {
  def _M : SInst<name, m_types,  types, MergeOp1,     intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;
  def _X : SInst<name, xz_types, types, MergeAnyExp,  intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;
  def _Z : SInst<name, xz_types, types, MergeZeroExp, intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;
}

multiclass SInstCvtMX<string name, string m_types, string xz_types,
                      string types, string intrinsic,
                      list<FlagType> flags = [IsOverloadNone]> {
  def _M : SInst<name, m_types,  types, MergeOp1,     intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;
  def _X : SInst<name, xz_types, types, MergeAnyExp,  intrinsic, !listconcat(flags, [VerifyRuntimeMode])>;
}

// svcvt_s##_f16
defm SVFCVTZS_S16_F16 : SInstCvtMXZ<"svcvt_s16[_f16]", "ddPO", "dPO", "s",  "aarch64_sve_fcvtzs", [IsOverloadCvt]>;
defm SVFCVTZS_S32_F16 : SInstCvtMXZ<"svcvt_s32[_f16]", "ddPO", "dPO", "i",  "aarch64_sve_fcvtzs_i32f16">;
defm SVFCVTZS_S64_F16 : SInstCvtMXZ<"svcvt_s64[_f16]", "ddPO", "dPO", "l",  "aarch64_sve_fcvtzs_i64f16">;

// svcvt_s##_f32
defm SVFCVTZS_S32_F32 : SInstCvtMXZ<"svcvt_s32[_f32]", "ddPM", "dPM", "i",  "aarch64_sve_fcvtzs", [IsOverloadCvt]>;
defm SVFCVTZS_S64_F32 : SInstCvtMXZ<"svcvt_s64[_f32]", "ddPM", "dPM", "l",  "aarch64_sve_fcvtzs_i64f32">;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  defm SVCVT_BF16_F32   : SInstCvtMXZ<"svcvt_bf16[_f32]",  "ddPM", "dPM", "b",  "aarch64_sve_fcvt_bf16f32">;
  def SVCVTNT_BF16_F32 : SInst<"svcvtnt_bf16[_f32]", "ddPM", "b",  MergeOp1, "aarch64_sve_fcvtnt_bf16f32", [IsOverloadNone, VerifyRuntimeMode]>;
}

// svcvt_s##_f64
defm SVFCVTZS_S32_F64 : SInstCvtMXZ<"svcvt_s32[_f64]", "ttPd", "tPd", "d",  "aarch64_sve_fcvtzs_i32f64">;
defm SVFCVTZS_S64_F64 : SInstCvtMXZ<"svcvt_s64[_f64]", "ddPN", "dPN", "l",  "aarch64_sve_fcvtzs", [IsOverloadCvt]>;

// svcvt_u##_f16
defm SVFCVTZU_U16_F16 : SInstCvtMXZ<"svcvt_u16[_f16]", "ddPO", "dPO", "Us", "aarch64_sve_fcvtzu", [IsOverloadCvt]>;
defm SVFCVTZU_U32_F16 : SInstCvtMXZ<"svcvt_u32[_f16]", "ddPO", "dPO", "Ui", "aarch64_sve_fcvtzu_i32f16">;
defm SVFCVTZU_U64_F16 : SInstCvtMXZ<"svcvt_u64[_f16]", "ddPO", "dPO", "Ul", "aarch64_sve_fcvtzu_i64f16">;

// svcvt_u##_f32
defm SVFCVTZU_U32_F32 : SInstCvtMXZ<"svcvt_u32[_f32]", "ddPM", "dPM", "Ui", "aarch64_sve_fcvtzu", [IsOverloadCvt]>;
defm SVFCVTZU_U64_F32 : SInstCvtMXZ<"svcvt_u64[_f32]", "ddPM", "dPM", "Ul", "aarch64_sve_fcvtzu_i64f32">;

// svcvt_u##_f64
defm SVFCVTZU_U32_F64 : SInstCvtMXZ<"svcvt_u32[_f64]", "zzPd", "zPd", "d",  "aarch64_sve_fcvtzu_i32f64">;
defm SVFCVTZU_U64_F64 : SInstCvtMXZ<"svcvt_u64[_f64]", "ddPN", "dPN", "Ul", "aarch64_sve_fcvtzu", [IsOverloadCvt]>;

// svcvt_f16_s##
defm SVFCVTZS_F16_S16 : SInstCvtMXZ<"svcvt_f16[_s16]", "OOPd", "OPd", "s",  "aarch64_sve_scvtf", [IsOverloadCvt]>;
defm SVFCVTZS_F16_S32 : SInstCvtMXZ<"svcvt_f16[_s32]", "OOPd", "OPd", "i",  "aarch64_sve_scvtf_f16i32">;
defm SVFCVTZS_F16_S64 : SInstCvtMXZ<"svcvt_f16[_s64]", "OOPd", "OPd", "l",  "aarch64_sve_scvtf_f16i64">;

// svcvt_f32_s##
defm SVFCVTZS_F32_S32 : SInstCvtMXZ<"svcvt_f32[_s32]", "MMPd", "MPd", "i",  "aarch64_sve_scvtf", [IsOverloadCvt]>;
defm SVFCVTZS_F32_S64 : SInstCvtMXZ<"svcvt_f32[_s64]", "MMPd", "MPd", "l",  "aarch64_sve_scvtf_f32i64">;

// svcvt_f64_s##
defm SVFCVTZS_F64_S32 : SInstCvtMXZ<"svcvt_f64[_s32]", "ddPt", "dPt", "d",  "aarch64_sve_scvtf_f64i32">;
defm SVFCVTZS_F64_S64 : SInstCvtMXZ<"svcvt_f64[_s64]", "NNPd", "NPd", "l",  "aarch64_sve_scvtf", [IsOverloadCvt]>;

// svcvt_f16_u##
defm SVFCVTZU_F16_U16 : SInstCvtMXZ<"svcvt_f16[_u16]", "OOPd", "OPd", "Us", "aarch64_sve_ucvtf", [IsOverloadCvt]>;
defm SVFCVTZU_F16_U32 : SInstCvtMXZ<"svcvt_f16[_u32]", "OOPd", "OPd", "Ui", "aarch64_sve_ucvtf_f16i32">;
defm SVFCVTZU_F16_U64 : SInstCvtMXZ<"svcvt_f16[_u64]", "OOPd", "OPd", "Ul", "aarch64_sve_ucvtf_f16i64">;

// svcvt_f32_u##
defm SVFCVTZU_F32_U32 : SInstCvtMXZ<"svcvt_f32[_u32]", "MMPd", "MPd", "Ui", "aarch64_sve_ucvtf", [IsOverloadCvt]>;
defm SVFCVTZU_F32_U64 : SInstCvtMXZ<"svcvt_f32[_u64]", "MMPd", "MPd", "Ul", "aarch64_sve_ucvtf_f32i64">;

// svcvt_f64_u##
defm SVFCVTZU_F64_U32 : SInstCvtMXZ<"svcvt_f64[_u32]", "ddPz", "dPz", "d",  "aarch64_sve_ucvtf_f64i32">;
defm SVFCVTZU_F64_U64 : SInstCvtMXZ<"svcvt_f64[_u64]", "NNPd", "NPd", "Ul", "aarch64_sve_ucvtf", [IsOverloadCvt]>;

// svcvt_f16_f##
defm SVFCVT_F16_F32   : SInstCvtMXZ<"svcvt_f16[_f32]", "OOPd", "OPd", "f", "aarch64_sve_fcvt_f16f32">;
defm SVFCVT_F16_F64   : SInstCvtMXZ<"svcvt_f16[_f64]", "OOPd", "OPd", "d", "aarch64_sve_fcvt_f16f64">;

// svcvt_f32_f##
defm SVFCVT_F32_F16   : SInstCvtMXZ<"svcvt_f32[_f16]", "ddPO", "dPO", "f", "aarch64_sve_fcvt_f32f16">;
defm SVFCVT_F32_F64   : SInstCvtMXZ<"svcvt_f32[_f64]", "MMPd", "MPd", "d", "aarch64_sve_fcvt_f32f64">;

// svcvt_f64_f##
defm SVFCVT_F64_F16   : SInstCvtMXZ<"svcvt_f64[_f16]", "ddPO", "dPO", "d", "aarch64_sve_fcvt_f64f16">;
defm SVFCVT_F64_F32   : SInstCvtMXZ<"svcvt_f64[_f32]", "ddPM", "dPM", "d", "aarch64_sve_fcvt_f64f32">;

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
defm SVCVTLT_F32    : SInstCvtMX<"svcvtlt_f32[_f16]",  "ddPh", "dPh", "f", "aarch64_sve_fcvtlt_f32f16">;
defm SVCVTLT_F64    : SInstCvtMX<"svcvtlt_f64[_f32]",  "ddPh", "dPh", "d", "aarch64_sve_fcvtlt_f64f32">;

defm SVCVTX_F32     : SInstCvtMXZ<"svcvtx_f32[_f64]",  "MMPd", "MPd", "d", "aarch64_sve_fcvtx_f32f64">;

def SVCVTNT_F32     : SInst<"svcvtnt_f16[_f32]",  "hhPd", "f", MergeOp1, "aarch64_sve_fcvtnt_f16f32", [IsOverloadNone, VerifyRuntimeMode]>;
def SVCVTNT_F64     : SInst<"svcvtnt_f32[_f64]",  "hhPd", "d", MergeOp1, "aarch64_sve_fcvtnt_f32f64", [IsOverloadNone, VerifyRuntimeMode]>;
//  SVCVTNT_X       : Implemented as macro by SveEmitter.cpp

def SVCVTXNT_F32    : SInst<"svcvtxnt_f32[_f64]", "MMPd", "d", MergeOp1, "aarch64_sve_fcvtxnt_f32f64", [IsOverloadNone, VerifyRuntimeMode]>;
//  SVCVTXNT_X_F32  : Implemented as macro by SveEmitter.cpp

}

////////////////////////////////////////////////////////////////////////////////
// Permutations and selection

multiclass SVEPerm<string name, string proto, string i> {
  def : SInst<name, proto, "csilUcUsUiUlhfd", MergeNone, i, [VerifyRuntimeMode]>;
  let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
    def: SInst<name, proto, "b", MergeNone, i, [VerifyRuntimeMode]>;
  }
}

defm SVCLASTA    : SVEPerm<"svclasta[_{d}]",   "dPdd", "aarch64_sve_clasta">;
defm SVCLASTA_N  : SVEPerm<"svclasta[_n_{d}]", "sPsd", "aarch64_sve_clasta_n">;
defm SVCLASTB    : SVEPerm<"svclastb[_{d}]",   "dPdd", "aarch64_sve_clastb">;
defm SVCLASTB_N  : SVEPerm<"svclastb[_n_{d}]", "sPsd", "aarch64_sve_clastb_n">;

let SVETargetGuard = "sve", SMETargetGuard = InvalidMode in {
def SVCOMPACT    : SInst<"svcompact[_{d}]",   "dPd",  "ilUiUlfd",        MergeNone, "aarch64_sve_compact">;
}

// Note: svdup_lane is implemented using the intrinsic for TBL to represent a
// splat of any possible lane. It is upto LLVM to pick a more efficient
// instruction such as DUP (indexed) if the lane index fits the range of the
// instruction's immediate.
def SVDUP_LANE   : SInst<"svdup_lane[_{d}]",  "ddL",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_tbl", [VerifyRuntimeMode]>;
let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
def SVDUP_LANE_BF16 :
                   SInst<"svdup_lane[_{d}]",  "ddL",  "b",               MergeNone, "aarch64_sve_tbl", [VerifyRuntimeMode]>;
}

def SVDUPQ_LANE  : SInst<"svdupq_lane[_{d}]", "ddn",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_dupq_lane", [VerifyRuntimeMode]>;
let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  def SVDUPQ_LANE_BF16  : SInst<"svdupq_lane[_{d}]", "ddn",  "b", MergeNone, "aarch64_sve_dupq_lane", [VerifyRuntimeMode]>;
}
def SVEXT        : SInst<"svext[_{d}]",       "dddi", "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_ext", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckExtract, 1>]>;
defm SVLASTA     : SVEPerm<"svlasta[_{d}]",   "sPd",  "aarch64_sve_lasta">;
defm SVLASTB     : SVEPerm<"svlastb[_{d}]",   "sPd",  "aarch64_sve_lastb">;
def SVREV        : SInst<"svrev[_{d}]",       "dd",   "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_rev", [VerifyRuntimeMode]>;
def SVSEL        : SInst<"svsel[_{d}]",       "dPdd", "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_sel", [VerifyRuntimeMode]>;
def SVSPLICE     : SInst<"svsplice[_{d}]",    "dPdd", "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_splice", [VerifyRuntimeMode]>;
def SVTBL        : SInst<"svtbl[_{d}]",       "ddu",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_tbl", [VerifyRuntimeMode]>;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
  def SVTBL_BF16 : SInst<"svtbl[_{d}]",       "ddu",  "b",               MergeNone, "aarch64_sve_tbl", [VerifyRuntimeMode]>;
}

def SVTRN1       : SInst<"svtrn1[_{d}]",      "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_trn1", [VerifyRuntimeMode]>;
def SVTRN2       : SInst<"svtrn2[_{d}]",      "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_trn2", [VerifyRuntimeMode]>;
def SVUNPKHI_S   : SInst<"svunpkhi[_{d}]",    "dh",   "sil",             MergeNone, "aarch64_sve_sunpkhi", [VerifyRuntimeMode]>;
def SVUNPKHI_U   : SInst<"svunpkhi[_{d}]",    "dh",   "UsUiUl",          MergeNone, "aarch64_sve_uunpkhi", [VerifyRuntimeMode]>;
def SVUNPKLO_S   : SInst<"svunpklo[_{d}]",    "dh",   "sil",             MergeNone, "aarch64_sve_sunpklo", [VerifyRuntimeMode]>;
def SVUNPKLO_U   : SInst<"svunpklo[_{d}]",    "dh",   "UsUiUl",          MergeNone, "aarch64_sve_uunpklo", [VerifyRuntimeMode]>;
def SVUZP1       : SInst<"svuzp1[_{d}]",      "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_uzp1", [VerifyRuntimeMode]>;
def SVUZP2       : SInst<"svuzp2[_{d}]",      "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_uzp2", [VerifyRuntimeMode]>;
def SVZIP1       : SInst<"svzip1[_{d}]",      "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_zip1", [VerifyRuntimeMode]>;
def SVZIP2       : SInst<"svzip2[_{d}]",      "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_zip2", [VerifyRuntimeMode]>;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
def SVEXT_BF16    : SInst<"svext[_{d}]",    "dddi", "b", MergeNone, "aarch64_sve_ext", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckExtract, 1>]>;
def SVREV_BF16    : SInst<"svrev[_{d}]",    "dd",   "b", MergeNone, "aarch64_sve_rev", [VerifyRuntimeMode]>;
def SVSEL_BF16    : SInst<"svsel[_{d}]",    "dPdd", "b", MergeNone, "aarch64_sve_sel", [VerifyRuntimeMode]>;
def SVSPLICE_BF16 : SInst<"svsplice[_{d}]", "dPdd", "b", MergeNone, "aarch64_sve_splice", [VerifyRuntimeMode]>;
def SVTRN1_BF16   : SInst<"svtrn1[_{d}]",   "ddd",  "b", MergeNone, "aarch64_sve_trn1", [VerifyRuntimeMode]>;
def SVTRN2_BF16   : SInst<"svtrn2[_{d}]",   "ddd",  "b", MergeNone, "aarch64_sve_trn2", [VerifyRuntimeMode]>;
def SVUZP1_BF16   : SInst<"svuzp1[_{d}]",   "ddd",  "b", MergeNone, "aarch64_sve_uzp1", [VerifyRuntimeMode]>;
def SVUZP2_BF16   : SInst<"svuzp2[_{d}]",   "ddd",  "b", MergeNone, "aarch64_sve_uzp2", [VerifyRuntimeMode]>;
def SVZIP1_BF16   : SInst<"svzip1[_{d}]",   "ddd",  "b", MergeNone, "aarch64_sve_zip1", [VerifyRuntimeMode]>;
def SVZIP2_BF16   : SInst<"svzip2[_{d}]",   "ddd",  "b", MergeNone, "aarch64_sve_zip2", [VerifyRuntimeMode]>;
}

def SVREV_B8   : SInst<"svrev_b8",     "PP",   "Pc", MergeNone, "aarch64_sve_rev", [VerifyRuntimeMode]>;
def SVREV_B16  : SInst<"svrev_b16",    "PP",   "Pc", MergeNone, "aarch64_sve_rev_b16",  [IsOverloadNone, VerifyRuntimeMode]>;
def SVREV_B32  : SInst<"svrev_b32",    "PP",   "Pc", MergeNone, "aarch64_sve_rev_b32",  [IsOverloadNone, VerifyRuntimeMode]>;
def SVREV_B64  : SInst<"svrev_b64",    "PP",   "Pc", MergeNone, "aarch64_sve_rev_b64",  [IsOverloadNone, VerifyRuntimeMode]>;
def SVSEL_B    : SInst<"svsel[_b]",    "PPPP", "Pc", MergeNone, "aarch64_sve_sel", [VerifyRuntimeMode]>;
def SVTRN1_B8  : SInst<"svtrn1_b8",    "PPP",  "Pc", MergeNone, "aarch64_sve_trn1", [VerifyRuntimeMode]>;
def SVTRN1_B16 : SInst<"svtrn1_b16",   "PPP",  "Pc", MergeNone, "aarch64_sve_trn1_b16", [IsOverloadNone, VerifyRuntimeMode]>;
def SVTRN1_B32 : SInst<"svtrn1_b32",   "PPP",  "Pc", MergeNone, "aarch64_sve_trn1_b32", [IsOverloadNone, VerifyRuntimeMode]>;
def SVTRN1_B64 : SInst<"svtrn1_b64",   "PPP",  "Pc", MergeNone, "aarch64_sve_trn1_b64", [IsOverloadNone, VerifyRuntimeMode]>;
def SVTRN2_B8  : SInst<"svtrn2_b8",    "PPP",  "Pc", MergeNone, "aarch64_sve_trn2", [VerifyRuntimeMode]>;
def SVTRN2_B16 : SInst<"svtrn2_b16",   "PPP",  "Pc", MergeNone, "aarch64_sve_trn2_b16", [IsOverloadNone, VerifyRuntimeMode]>;
def SVTRN2_B32 : SInst<"svtrn2_b32",   "PPP",  "Pc", MergeNone, "aarch64_sve_trn2_b32", [IsOverloadNone, VerifyRuntimeMode]>;
def SVTRN2_B64 : SInst<"svtrn2_b64",   "PPP",  "Pc", MergeNone, "aarch64_sve_trn2_b64", [IsOverloadNone, VerifyRuntimeMode]>;
def SVPUNPKHI  : SInst<"svunpkhi[_b]", "PP",   "Pc", MergeNone, "aarch64_sve_punpkhi", [VerifyRuntimeMode]>;
def SVPUNPKLO  : SInst<"svunpklo[_b]", "PP",   "Pc", MergeNone, "aarch64_sve_punpklo", [VerifyRuntimeMode]>;
def SVUZP1_B8  : SInst<"svuzp1_b8",    "PPP",  "Pc", MergeNone, "aarch64_sve_uzp1", [VerifyRuntimeMode]>;
def SVUZP1_B16 : SInst<"svuzp1_b16",   "PPP",  "Pc", MergeNone, "aarch64_sve_uzp1_b16", [IsOverloadNone, VerifyRuntimeMode]>;
def SVUZP1_B32 : SInst<"svuzp1_b32",   "PPP",  "Pc", MergeNone, "aarch64_sve_uzp1_b32", [IsOverloadNone, VerifyRuntimeMode]>;
def SVUZP1_B64 : SInst<"svuzp1_b64",   "PPP",  "Pc", MergeNone, "aarch64_sve_uzp1_b64", [IsOverloadNone, VerifyRuntimeMode]>;
def SVUZP2_B8  : SInst<"svuzp2_b8",    "PPP",  "Pc", MergeNone, "aarch64_sve_uzp2", [VerifyRuntimeMode]>;
def SVUZP2_B16 : SInst<"svuzp2_b16",   "PPP",  "Pc", MergeNone, "aarch64_sve_uzp2_b16", [IsOverloadNone, VerifyRuntimeMode]>;
def SVUZP2_B32 : SInst<"svuzp2_b32",   "PPP",  "Pc", MergeNone, "aarch64_sve_uzp2_b32", [IsOverloadNone, VerifyRuntimeMode]>;
def SVUZP2_B64 : SInst<"svuzp2_b64",   "PPP",  "Pc", MergeNone, "aarch64_sve_uzp2_b64", [IsOverloadNone, VerifyRuntimeMode]>;
def SVZIP1_B8  : SInst<"svzip1_b8",    "PPP",  "Pc", MergeNone, "aarch64_sve_zip1", [VerifyRuntimeMode]>;
def SVZIP1_B16 : SInst<"svzip1_b16",   "PPP",  "Pc", MergeNone, "aarch64_sve_zip1_b16", [IsOverloadNone, VerifyRuntimeMode]>;
def SVZIP1_B32 : SInst<"svzip1_b32",   "PPP",  "Pc", MergeNone, "aarch64_sve_zip1_b32", [IsOverloadNone, VerifyRuntimeMode]>;
def SVZIP1_B64 : SInst<"svzip1_b64",   "PPP",  "Pc", MergeNone, "aarch64_sve_zip1_b64", [IsOverloadNone, VerifyRuntimeMode]>;
def SVZIP2_B   : SInst<"svzip2_b8",    "PPP",  "Pc", MergeNone, "aarch64_sve_zip2", [VerifyRuntimeMode]>;
def SVZIP2_B16 : SInst<"svzip2_b16",   "PPP",  "Pc", MergeNone, "aarch64_sve_zip2_b16", [IsOverloadNone, VerifyRuntimeMode]>;
def SVZIP2_B32 : SInst<"svzip2_b32",   "PPP",  "Pc", MergeNone, "aarch64_sve_zip2_b32", [IsOverloadNone, VerifyRuntimeMode]>;
def SVZIP2_B64 : SInst<"svzip2_b64",   "PPP",  "Pc", MergeNone, "aarch64_sve_zip2_b64", [IsOverloadNone, VerifyRuntimeMode]>;

////////////////////////////////////////////////////////////////////////////////
// Predicate creation

def SVPFALSE : SInst<"svpfalse[_b]", "Pv", "", MergeNone, "", [IsOverloadNone, VerifyRuntimeMode]>;
def SVPTRUE_PAT : SInst<"svptrue_pat_{d}", "PI", "PcPsPiPl", MergeNone, "aarch64_sve_ptrue", [VerifyRuntimeMode]>;
def SVPTRUE     : SInst<"svptrue_{d}",     "Pv",  "PcPsPiPl", MergeNone, "aarch64_sve_ptrue", [IsAppendSVALL, VerifyRuntimeMode]>;

def SVDUPQ_B8      : SInst<"svdupq[_n]_{d}",  "Pssssssssssssssss",  "Pc", MergeNone, "", [VerifyRuntimeMode]>;
def SVDUPQ_B16     : SInst<"svdupq[_n]_{d}", "Pssssssss",  "Ps", MergeNone, "", [VerifyRuntimeMode]>;
def SVDUPQ_B32     : SInst<"svdupq[_n]_{d}", "Pssss",  "Pi", MergeNone, "", [VerifyRuntimeMode]>;
def SVDUPQ_B64     : SInst<"svdupq[_n]_{d}", "Pss",  "Pl", MergeNone, "", [VerifyRuntimeMode]>;
def SVDUP_N_B      : SInst<"svdup[_n]_{d}",  "Ps", "PcPsPiPl", MergeNone, "", [VerifyRuntimeMode]>;


////////////////////////////////////////////////////////////////////////////////
// Predicate operations

def SVAND_B_Z  : SInst<"svand[_b]_z",  "PPPP", "Pc", MergeNone, "aarch64_sve_and_z", [VerifyRuntimeMode]>;
def SVBIC_B_Z  : SInst<"svbic[_b]_z",  "PPPP", "Pc", MergeNone, "aarch64_sve_bic_z", [VerifyRuntimeMode]>;
def SVEOR_B_Z  : SInst<"sveor[_b]_z",  "PPPP", "Pc", MergeNone, "aarch64_sve_eor_z", [VerifyRuntimeMode]>;
def SVMOV_B_Z  : SInst<"svmov[_b]_z",  "PPP",  "Pc", MergeNone, "", [VerifyRuntimeMode]>; // Uses custom expansion
def SVNAND_B_Z : SInst<"svnand[_b]_z", "PPPP", "Pc", MergeNone, "aarch64_sve_nand_z", [VerifyRuntimeMode]>;
def SVNOR_B_Z  : SInst<"svnor[_b]_z",  "PPPP", "Pc", MergeNone, "aarch64_sve_nor_z", [VerifyRuntimeMode]>;
def SVNOT_B_Z  : SInst<"svnot[_b]_z",  "PPP",  "Pc", MergeNone, "", [VerifyRuntimeMode]>; // Uses custom expansion
def SVORN_B_Z  : SInst<"svorn[_b]_z",  "PPPP", "Pc", MergeNone, "aarch64_sve_orn_z", [VerifyRuntimeMode]>;
def SVORR_B_Z  : SInst<"svorr[_b]_z",  "PPPP", "Pc", MergeNone, "aarch64_sve_orr_z", [VerifyRuntimeMode]>;

def SVBRKA    : SInst<"svbrka[_b]_m",  "PPPP", "Pc", MergeNone, "aarch64_sve_brka", [VerifyRuntimeMode]>;
def SVBRKA_Z  : SInst<"svbrka[_b]_z",  "PPP",  "Pc", MergeNone, "aarch64_sve_brka_z", [VerifyRuntimeMode]>;
def SVBRKB    : SInst<"svbrkb[_b]_m",  "PPPP", "Pc", MergeNone, "aarch64_sve_brkb", [VerifyRuntimeMode]>;
def SVBRKB_Z  : SInst<"svbrkb[_b]_z",  "PPP",  "Pc", MergeNone, "aarch64_sve_brkb_z", [VerifyRuntimeMode]>;
def SVBRKN_Z  : SInst<"svbrkn[_b]_z",  "PPPP", "Pc", MergeNone, "aarch64_sve_brkn_z", [VerifyRuntimeMode]>;
def SVBRKPA_Z : SInst<"svbrkpa[_b]_z", "PPPP", "Pc", MergeNone, "aarch64_sve_brkpa_z", [VerifyRuntimeMode]>;
def SVBRKPB_Z : SInst<"svbrkpb[_b]_z", "PPPP", "Pc", MergeNone, "aarch64_sve_brkpb_z", [VerifyRuntimeMode]>;

def SVPFIRST : SInst<"svpfirst[_b]", "PPP", "Pc",       MergeNone, "aarch64_sve_pfirst", [VerifyRuntimeMode]>;
def SVPNEXT  : SInst<"svpnext_{d}",    "PPP", "PcPsPiPl", MergeNone, "aarch64_sve_pnext", [VerifyRuntimeMode]>;

////////////////////////////////////////////////////////////////////////////////
// Testing predicates

def SVPTEST_ANY   : SInst<"svptest_any",   "sPP", "Pc", MergeNone, "aarch64_sve_ptest_any", [VerifyRuntimeMode]>;
def SVPTEST_FIRST : SInst<"svptest_first", "sPP", "Pc", MergeNone, "aarch64_sve_ptest_first", [VerifyRuntimeMode]>;
def SVPTEST_LAST  : SInst<"svptest_last",  "sPP", "Pc", MergeNone, "aarch64_sve_ptest_last", [VerifyRuntimeMode]>;

////////////////////////////////////////////////////////////////////////////////
// FFR manipulation

let SVETargetGuard = "sve", SMETargetGuard = InvalidMode in {
def SVRDFFR   : SInst<"svrdffr",   "Pv",  "Pc", MergeNone, "", [IsOverloadNone]>;
def SVRDFFR_Z : SInst<"svrdffr_z", "PP", "Pc", MergeNone, "", [IsOverloadNone]>;
def SVSETFFR  : SInst<"svsetffr",  "vv",  "",   MergeNone, "", [IsOverloadNone]>;
def SVWRFFR   : SInst<"svwrffr",   "vP", "Pc", MergeNone, "", [IsOverloadNone]>;
}

////////////////////////////////////////////////////////////////////////////////
// Counting elements

def SVCNTB_PAT : SInst<"svcntb_pat", "nI", "", MergeNone, "aarch64_sve_cntb", [IsOverloadNone, VerifyRuntimeMode]>;
def SVCNTH_PAT : SInst<"svcnth_pat", "nI", "", MergeNone, "aarch64_sve_cnth", [IsOverloadNone, VerifyRuntimeMode]>;
def SVCNTW_PAT : SInst<"svcntw_pat", "nI", "", MergeNone, "aarch64_sve_cntw", [IsOverloadNone, VerifyRuntimeMode]>;
def SVCNTD_PAT : SInst<"svcntd_pat", "nI", "", MergeNone, "aarch64_sve_cntd", [IsOverloadNone, VerifyRuntimeMode]>;

def SVCNTB : SInst<"svcntb", "nv", "", MergeNone, "aarch64_sve_cntb", [IsAppendSVALL, IsOverloadNone, VerifyRuntimeMode]>;
def SVCNTH : SInst<"svcnth", "nv", "", MergeNone, "aarch64_sve_cnth", [IsAppendSVALL, IsOverloadNone, VerifyRuntimeMode]>;
def SVCNTW : SInst<"svcntw", "nv", "", MergeNone, "aarch64_sve_cntw", [IsAppendSVALL, IsOverloadNone, VerifyRuntimeMode]>;
def SVCNTD : SInst<"svcntd", "nv", "", MergeNone, "aarch64_sve_cntd", [IsAppendSVALL, IsOverloadNone, VerifyRuntimeMode]>;

def SVCNTP : SInst<"svcntp_{d}",  "nPP", "PcPsPiPl",        MergeNone, "aarch64_sve_cntp", [VerifyRuntimeMode]>;
def SVLEN  : SInst<"svlen[_{d}]", "nd",  "csilUcUsUiUlhfd", MergeNone, "", [VerifyRuntimeMode]>;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
def SVLEN_BF16 : SInst<"svlen[_{d}]", "nd", "b", MergeNone, "", [VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// Saturating scalar arithmetic

class sat_type<string u, string t> { string U = u; string T = t; }
def SignedByte         : sat_type<"",  "c">;
def SignedHalf         : sat_type<"",  "s">;
def SignedWord         : sat_type<"",  "i">;
def SignedDoubleWord   : sat_type<"",  "l">;
def UnsignedByte       : sat_type<"U", "Uc">;
def UnsignedHalf       : sat_type<"U", "Us">;
def UnsignedWord       : sat_type<"U", "Ui">;
def UnsignedDoubleWord : sat_type<"U", "Ul">;

multiclass SInst_SAT1<string name, string intrinsic, sat_type type> {
  def _N32     : SInst<name # "_pat[_n_{d}]", "ssIi", type.U # "i", MergeNone, intrinsic # "_n32", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck1_16>]>;
  def _N64     : SInst<name # "_pat[_n_{d}]", "ssIi", type.U # "l", MergeNone, intrinsic # "_n64", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck1_16>]>;
  def _N32_ALL : SInst<name # "[_n_{d}]",     "ssi",  type.U # "i", MergeNone, intrinsic # "_n32", [IsOverloadNone, IsInsertOp1SVALL, VerifyRuntimeMode], [ImmCheck<1, ImmCheck1_16>]>;
  def _N64_ALL : SInst<name # "[_n_{d}]",     "ssi",  type.U # "l", MergeNone, intrinsic # "_n64", [IsOverloadNone, IsInsertOp1SVALL, VerifyRuntimeMode], [ImmCheck<1, ImmCheck1_16>]>;
}

multiclass SInst_SAT2<string name, string intrinsic, sat_type type> {
  def ""       : SInst<name # "_pat[_{d}]",   "ddIi", type.T,       MergeNone, intrinsic, [VerifyRuntimeMode], [ImmCheck<2, ImmCheck1_16>]>;
  def _ALL     : SInst<name # "[_{d}]",       "ddi",  type.T,       MergeNone, intrinsic, [IsInsertOp1SVALL, VerifyRuntimeMode], [ImmCheck<1, ImmCheck1_16>]>;

  def _N32     : SInst<name # "_pat[_n_{d}]", "ssIi", type.U # "i", MergeNone, intrinsic # "_n32", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck1_16>]>;
  def _N64     : SInst<name # "_pat[_n_{d}]", "ssIi", type.U # "l", MergeNone, intrinsic # "_n64", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck1_16>]>;
  def _N32_ALL : SInst<name # "[_n_{d}]",     "ssi",  type.U # "i", MergeNone, intrinsic # "_n32", [IsOverloadNone, IsInsertOp1SVALL, VerifyRuntimeMode], [ImmCheck<1, ImmCheck1_16>]>;
  def _N64_ALL : SInst<name # "[_n_{d}]",     "ssi",  type.U # "l", MergeNone, intrinsic # "_n64", [IsOverloadNone, IsInsertOp1SVALL, VerifyRuntimeMode], [ImmCheck<1, ImmCheck1_16>]>;
}

defm SVQDECB_S : SInst_SAT1<"svqdecb", "aarch64_sve_sqdecb", SignedByte>;
defm SVQDECB_U : SInst_SAT1<"svqdecb", "aarch64_sve_uqdecb", UnsignedByte>;
defm SVQDECH_S : SInst_SAT2<"svqdech", "aarch64_sve_sqdech", SignedHalf>;
defm SVQDECH_U : SInst_SAT2<"svqdech", "aarch64_sve_uqdech", UnsignedHalf>;
defm SVQDECW_S : SInst_SAT2<"svqdecw", "aarch64_sve_sqdecw", SignedWord>;
defm SVQDECW_U : SInst_SAT2<"svqdecw", "aarch64_sve_uqdecw", UnsignedWord>;
defm SVQDECD_S : SInst_SAT2<"svqdecd", "aarch64_sve_sqdecd", SignedDoubleWord>;
defm SVQDECD_U : SInst_SAT2<"svqdecd", "aarch64_sve_uqdecd", UnsignedDoubleWord>;

defm SVQINCB_S : SInst_SAT1<"svqincb", "aarch64_sve_sqincb", SignedByte>;
defm SVQINCB_U : SInst_SAT1<"svqincb", "aarch64_sve_uqincb", UnsignedByte>;
defm SVQINCH_S : SInst_SAT2<"svqinch", "aarch64_sve_sqinch", SignedHalf>;
defm SVQINCH_U : SInst_SAT2<"svqinch", "aarch64_sve_uqinch", UnsignedHalf>;
defm SVQINCW_S : SInst_SAT2<"svqincw", "aarch64_sve_sqincw", SignedWord>;
defm SVQINCW_U : SInst_SAT2<"svqincw", "aarch64_sve_uqincw", UnsignedWord>;
defm SVQINCD_S : SInst_SAT2<"svqincd", "aarch64_sve_sqincd", SignedDoubleWord>;
defm SVQINCD_U : SInst_SAT2<"svqincd", "aarch64_sve_uqincd", UnsignedDoubleWord>;

def SVQDECP_S : SInst<"svqdecp[_{d}]", "ddP", "sil",    MergeNone, "aarch64_sve_sqdecp", [VerifyRuntimeMode]>;
def SVQDECP_U : SInst<"svqdecp[_{d}]", "ddP", "UsUiUl", MergeNone, "aarch64_sve_uqdecp", [VerifyRuntimeMode]>;
def SVQINCP_S : SInst<"svqincp[_{d}]", "ddP", "sil",    MergeNone, "aarch64_sve_sqincp", [VerifyRuntimeMode]>;
def SVQINCP_U : SInst<"svqincp[_{d}]", "ddP", "UsUiUl", MergeNone, "aarch64_sve_uqincp", [VerifyRuntimeMode]>;

def SVQDECP_N_S32 : SInst<"svqdecp[_n_s32]_{d}", "kkP", "PcPsPiPl", MergeNone, "aarch64_sve_sqdecp_n32", [VerifyRuntimeMode]>;
def SVQDECP_N_S64 : SInst<"svqdecp[_n_s64]_{d}", "llP", "PcPsPiPl", MergeNone, "aarch64_sve_sqdecp_n64", [VerifyRuntimeMode]>;
def SVQDECP_N_U32 : SInst<"svqdecp[_n_u32]_{d}", "mmP", "PcPsPiPl", MergeNone, "aarch64_sve_uqdecp_n32", [VerifyRuntimeMode]>;
def SVQDECP_N_U64 : SInst<"svqdecp[_n_u64]_{d}", "nnP", "PcPsPiPl", MergeNone, "aarch64_sve_uqdecp_n64", [VerifyRuntimeMode]>;
def SVQINCP_N_S32 : SInst<"svqincp[_n_s32]_{d}", "kkP", "PcPsPiPl", MergeNone, "aarch64_sve_sqincp_n32", [VerifyRuntimeMode]>;
def SVQINCP_N_S64 : SInst<"svqincp[_n_s64]_{d}", "llP", "PcPsPiPl", MergeNone, "aarch64_sve_sqincp_n64", [VerifyRuntimeMode]>;
def SVQINCP_N_U32 : SInst<"svqincp[_n_u32]_{d}", "mmP", "PcPsPiPl", MergeNone, "aarch64_sve_uqincp_n32", [VerifyRuntimeMode]>;
def SVQINCP_N_U64 : SInst<"svqincp[_n_u64]_{d}", "nnP", "PcPsPiPl", MergeNone, "aarch64_sve_uqincp_n64", [VerifyRuntimeMode]>;

let SVETargetGuard = "sve,i8mm", SMETargetGuard = InvalidMode in {
def SVMLLA_S32   : SInst<"svmmla[_s32]",   "ddqq","i",  MergeNone, "aarch64_sve_smmla">;
def SVMLLA_U32   : SInst<"svmmla[_u32]",   "ddqq","Ui", MergeNone, "aarch64_sve_ummla">;
def SVUSMLLA_S32 : SInst<"svusmmla[_s32]", "ddbq","i",  MergeNone, "aarch64_sve_usmmla">;
}

let SVETargetGuard = "sve,i8mm", SMETargetGuard = "sme,i8mm"in {
def SVUSDOT_S    : SInst<"svusdot[_s32]",    "ddbq", "i",       MergeNone, "aarch64_sve_usdot", [VerifyRuntimeMode]>;
def SVUSDOT_N_S  : SInst<"svusdot[_n_s32]",  "ddbr", "i",       MergeNone, "aarch64_sve_usdot", [VerifyRuntimeMode]>;
def SVSUDOT_S    : SInst<"svsudot[_s32]",    "ddqb", "i",       MergeNone, "aarch64_sve_usdot", [ReverseUSDOT, VerifyRuntimeMode]>;
def SVSUDOT_N_S  : SInst<"svsudot[_n_s32]",  "ddq@", "i",       MergeNone, "aarch64_sve_usdot", [ReverseUSDOT, VerifyRuntimeMode]>;

def SVUSDOT_LANE_S : SInst<"svusdot_lane[_s32]",  "ddbqi",  "i",   MergeNone, "aarch64_sve_usdot_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndexDot, 2>]>;
def SVSUDOT_LANE_S : SInst<"svsudot_lane[_s32]",  "ddqbi",  "i",   MergeNone, "aarch64_sve_sudot_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndexDot, 2>]>;
}

let SVETargetGuard = "sve,f32mm", SMETargetGuard = InvalidMode in {
def SVMLLA_F32 : SInst<"svmmla[_f32]", "dddd","f", MergeNone, "aarch64_sve_fmmla">;
}

let SVETargetGuard = "sve,f64mm", SMETargetGuard = InvalidMode in {
def SVMLLA_F64 : SInst<"svmmla[_f64]", "dddd","d", MergeNone, "aarch64_sve_fmmla">;
def SVTRN1Q      : SInst<"svtrn1q[_{d}]",     "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_trn1q">;
def SVTRN2Q      : SInst<"svtrn2q[_{d}]",     "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_trn2q">;
def SVUZP1Q      : SInst<"svuzp1q[_{d}]",     "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_uzp1q">;
def SVUZP2Q      : SInst<"svuzp2q[_{d}]",     "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_uzp2q">;
def SVZIP1Q      : SInst<"svzip1q[_{d}]",     "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_zip1q">;
def SVZIP2Q      : SInst<"svzip2q[_{d}]",     "ddd",  "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_zip2q">;
}

let SVETargetGuard = "sve,bf16,f64mm", SMETargetGuard = InvalidMode in {
def SVTRN1Q_BF16      : SInst<"svtrn1q[_{d}]",     "ddd",  "b", MergeNone, "aarch64_sve_trn1q">;
def SVTRN2Q_BF16      : SInst<"svtrn2q[_{d}]",     "ddd",  "b", MergeNone, "aarch64_sve_trn2q">;
def SVUZP1Q_BF16      : SInst<"svuzp1q[_{d}]",     "ddd",  "b", MergeNone, "aarch64_sve_uzp1q">;
def SVUZP2Q_BF16      : SInst<"svuzp2q[_{d}]",     "ddd",  "b", MergeNone, "aarch64_sve_uzp2q">;
def SVZIP1Q_BF16      : SInst<"svzip1q[_{d}]",     "ddd",  "b", MergeNone, "aarch64_sve_zip1q">;
def SVZIP2Q_BF16      : SInst<"svzip2q[_{d}]",     "ddd",  "b", MergeNone, "aarch64_sve_zip2q">;
}

////////////////////////////////////////////////////////////////////////////////
// Vector creation
def SVUNDEF_1 : SInst<"svundef_{d}",  "dv", "csilUcUsUiUlhfd", MergeNone, "", [IsUndef, VerifyRuntimeMode]>;
def SVUNDEF_2 : SInst<"svundef2_{d}", "2v", "csilUcUsUiUlhfd", MergeNone, "", [IsUndef, VerifyRuntimeMode]>;
def SVUNDEF_3 : SInst<"svundef3_{d}", "3v", "csilUcUsUiUlhfd", MergeNone, "", [IsUndef, VerifyRuntimeMode]>;
def SVUNDEF_4 : SInst<"svundef4_{d}", "4v", "csilUcUsUiUlhfd", MergeNone, "", [IsUndef, VerifyRuntimeMode]>;

def SVCREATE_2 : SInst<"svcreate2[_{d}]", "2dd",   "csilUcUsUiUlhfd", MergeNone, "", [IsTupleCreate, VerifyRuntimeMode]>;
def SVCREATE_3 : SInst<"svcreate3[_{d}]", "3ddd",  "csilUcUsUiUlhfd", MergeNone, "", [IsTupleCreate, VerifyRuntimeMode]>;
def SVCREATE_4 : SInst<"svcreate4[_{d}]", "4dddd", "csilUcUsUiUlhfd", MergeNone, "", [IsTupleCreate, VerifyRuntimeMode]>;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
def SVUNDEF_1_BF16 : SInst<"svundef_{d}",  "dv", "b", MergeNone, "", [IsUndef, VerifyRuntimeMode]>;
def SVUNDEF_2_BF16 : SInst<"svundef2_{d}", "2v", "b", MergeNone, "", [IsUndef, VerifyRuntimeMode]>;
def SVUNDEF_3_BF16 : SInst<"svundef3_{d}", "3v", "b", MergeNone, "", [IsUndef, VerifyRuntimeMode]>;
def SVUNDEF_4_BF16 : SInst<"svundef4_{d}", "4v", "b", MergeNone, "", [IsUndef, VerifyRuntimeMode]>;

def SVCREATE_2_BF16 : SInst<"svcreate2[_{d}]", "2dd",   "b", MergeNone, "", [IsTupleCreate, VerifyRuntimeMode]>;
def SVCREATE_3_BF16 : SInst<"svcreate3[_{d}]", "3ddd",  "b", MergeNone, "", [IsTupleCreate, VerifyRuntimeMode]>;
def SVCREATE_4_BF16 : SInst<"svcreate4[_{d}]", "4dddd", "b", MergeNone, "", [IsTupleCreate, VerifyRuntimeMode]>;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
  def SVCREATE_2_B : SInst<"svcreate2[_b]", "2dd",   "Pc", MergeNone, "", [IsTupleCreate, VerifyRuntimeMode]>;
  def SVCREATE_4_B : SInst<"svcreate4[_b]", "4dddd", "Pc", MergeNone, "", [IsTupleCreate, VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// Vector insertion and extraction
def SVGET_2 : SInst<"svget2[_{d}]", "d2i", "csilUcUsUiUlhfd", MergeNone, "", [IsTupleGet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_1>]>;
def SVGET_3 : SInst<"svget3[_{d}]", "d3i", "csilUcUsUiUlhfd", MergeNone, "", [IsTupleGet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_2>]>;
def SVGET_4 : SInst<"svget4[_{d}]", "d4i", "csilUcUsUiUlhfd", MergeNone, "", [IsTupleGet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_3>]>;

def SVSET_2 : SInst<"svset2[_{d}]", "22id", "csilUcUsUiUlhfd", MergeNone, "", [IsTupleSet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_1>]>;
def SVSET_3 : SInst<"svset3[_{d}]", "33id", "csilUcUsUiUlhfd", MergeNone, "", [IsTupleSet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_2>]>;
def SVSET_4 : SInst<"svset4[_{d}]", "44id", "csilUcUsUiUlhfd", MergeNone, "", [IsTupleSet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_3>]>;

let SVETargetGuard = "sve,bf16", SMETargetGuard = "sme,bf16" in {
def SVGET_2_BF16 : SInst<"svget2[_{d}]", "d2i", "b", MergeNone, "", [IsTupleGet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_1>]>;
def SVGET_3_BF16 : SInst<"svget3[_{d}]", "d3i", "b", MergeNone, "", [IsTupleGet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_2>]>;
def SVGET_4_BF16 : SInst<"svget4[_{d}]", "d4i", "b", MergeNone, "", [IsTupleGet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_3>]>;

def SVSET_2_BF16 : SInst<"svset2[_{d}]", "22id", "b", MergeNone, "", [IsTupleSet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_1>]>;
def SVSET_3_BF16 : SInst<"svset3[_{d}]", "33id", "b", MergeNone, "", [IsTupleSet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_2>]>;
def SVSET_4_BF16 : SInst<"svset4[_{d}]", "44id", "b", MergeNone, "", [IsTupleSet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_3>]>;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
  def SVGET_2_B : SInst<"svget2[_b]", "d2i", "Pc", MergeNone, "", [IsTupleGet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_1>]>;
  def SVGET_4_B : SInst<"svget4[_b]", "d4i", "Pc", MergeNone, "", [IsTupleGet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_3>]>;

  def SVSET_2_B : SInst<"svset2[_b]", "22id", "Pc", MergeNone, "", [IsTupleSet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_1>]>;
  def SVSET_4_B : SInst<"svset4[_b]", "44id", "Pc", MergeNone, "", [IsTupleSet, VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_3>]>;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
  def SVUNDEF_2_B: Inst<"svundef2_b", "2", "Pc", MergeNone, "", [IsUndef, VerifyRuntimeMode], []>;
  def SVUNDEF_4_B: Inst<"svundef4_b", "4", "Pc", MergeNone, "", [IsUndef, VerifyRuntimeMode], []>;
}
////////////////////////////////////////////////////////////////////////////////
// SVE2 WhileGE/GT
let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVWHILEGE_S32 : SInst<"svwhilege_{d}[_{1}]", "Pkk", "PcPsPiPl",     MergeNone, "aarch64_sve_whilege", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILEGE_S64 : SInst<"svwhilege_{d}[_{1}]", "Pll", "PcPsPiPl",     MergeNone, "aarch64_sve_whilege", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILEGT_S32 : SInst<"svwhilegt_{d}[_{1}]", "Pkk", "PcPsPiPl",     MergeNone, "aarch64_sve_whilegt", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILEGT_S64 : SInst<"svwhilegt_{d}[_{1}]", "Pll", "PcPsPiPl",     MergeNone, "aarch64_sve_whilegt", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILEHI_U32 : SInst<"svwhilegt_{d}[_{1}]", "Pmm", "PUcPUsPUiPUl", MergeNone, "aarch64_sve_whilehi", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILEHI_U64 : SInst<"svwhilegt_{d}[_{1}]", "Pnn", "PUcPUsPUiPUl", MergeNone, "aarch64_sve_whilehi", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILEHS_U32 : SInst<"svwhilege_{d}[_{1}]", "Pmm", "PUcPUsPUiPUl", MergeNone, "aarch64_sve_whilehs", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
def SVWHILEHS_U64 : SInst<"svwhilege_{d}[_{1}]", "Pnn", "PUcPUsPUiPUl", MergeNone, "aarch64_sve_whilehs", [IsOverloadWhileOrMultiVecCvt, VerifyRuntimeMode]>;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2"  in {
  def SVWHILEGE_S64_X2 : SInst<"svwhilege_{d}[_{1}]_x2", "2ll", "PcPsPiPl",     MergeNone, "aarch64_sve_whilege_x2", [VerifyRuntimeMode]>;
  def SVWHILEGT_S64_X2 : SInst<"svwhilegt_{d}[_{1}]_x2", "2ll", "PcPsPiPl",     MergeNone, "aarch64_sve_whilegt_x2", [VerifyRuntimeMode]>;
  def SVWHILEHI_U64_X2 : SInst<"svwhilegt_{d}[_{1}]_x2", "2nn", "PcPsPiPl",     MergeNone, "aarch64_sve_whilehi_x2", [VerifyRuntimeMode]>;
  def SVWHILEHS_U64_X2 : SInst<"svwhilege_{d}[_{1}]_x2", "2nn", "PcPsPiPl",     MergeNone, "aarch64_sve_whilehs_x2", [VerifyRuntimeMode]>;
  def SVWHILELE_S64_X2 : SInst<"svwhilele_{d}[_{1}]_x2", "2ll", "PcPsPiPl",     MergeNone, "aarch64_sve_whilele_x2", [VerifyRuntimeMode]>;
  def SVWHILELT_S64_X2 : SInst<"svwhilelt_{d}[_{1}]_x2", "2ll", "PcPsPiPl",     MergeNone, "aarch64_sve_whilelt_x2", [VerifyRuntimeMode]>;
  def SVWHILELO_U64_X2 : SInst<"svwhilelt_{d}[_{1}]_x2", "2nn", "PcPsPiPl",     MergeNone, "aarch64_sve_whilelo_x2", [VerifyRuntimeMode]>;
  def SVWHILELS_U64_X2 : SInst<"svwhilele_{d}[_{1}]_x2", "2nn", "PcPsPiPl",     MergeNone, "aarch64_sve_whilels_x2", [VerifyRuntimeMode]>;

}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Uniform DSP operations

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
defm SVQADD_S  : SInstZPZZ<"svqadd",  "csli",     "aarch64_sve_sqadd",  "aarch64_sve_sqadd">;
defm SVQADD_U  : SInstZPZZ<"svqadd",  "UcUsUiUl", "aarch64_sve_uqadd",  "aarch64_sve_uqadd">;
defm SVHADD_S  : SInstZPZZ<"svhadd",  "csli",     "aarch64_sve_shadd",  "aarch64_sve_shadd">;
defm SVHADD_U  : SInstZPZZ<"svhadd",  "UcUsUiUl", "aarch64_sve_uhadd",  "aarch64_sve_uhadd">;
defm SVRHADD_S : SInstZPZZ<"svrhadd", "csli",     "aarch64_sve_srhadd", "aarch64_sve_srhadd">;
defm SVRHADD_U : SInstZPZZ<"svrhadd", "UcUsUiUl", "aarch64_sve_urhadd", "aarch64_sve_urhadd">;

defm SVQSUB_S  : SInstZPZZ<"svqsub",  "csli",     "aarch64_sve_sqsub",  "aarch64_sve_sqsub_u">;
defm SVQSUB_U  : SInstZPZZ<"svqsub",  "UcUsUiUl", "aarch64_sve_uqsub",  "aarch64_sve_uqsub_u">;
defm SVQSUBR_S : SInstZPZZ<"svqsubr", "csli",     "aarch64_sve_sqsubr", "aarch64_sve_sqsub_u", [ReverseMergeAnyBinOp]>;
defm SVQSUBR_U : SInstZPZZ<"svqsubr", "UcUsUiUl", "aarch64_sve_uqsubr", "aarch64_sve_uqsub_u", [ReverseMergeAnyBinOp]>;
defm SVHSUB_S  : SInstZPZZ<"svhsub",  "csli",     "aarch64_sve_shsub",  "aarch64_sve_shsub">;
defm SVHSUB_U  : SInstZPZZ<"svhsub",  "UcUsUiUl", "aarch64_sve_uhsub",  "aarch64_sve_uhsub">;
defm SVHSUBR_S : SInstZPZZ<"svhsubr", "csli",     "aarch64_sve_shsubr", "aarch64_sve_shsubr">;
defm SVHSUBR_U : SInstZPZZ<"svhsubr", "UcUsUiUl", "aarch64_sve_uhsubr", "aarch64_sve_uhsubr">;

defm SVQABS   : SInstZPZ<"svqabs",   "csil", "aarch64_sve_sqabs">;
defm SVQNEG   : SInstZPZ<"svqneg",   "csil", "aarch64_sve_sqneg">;
defm SVRECPE  : SInstZPZ<"svrecpe",  "Ui",   "aarch64_sve_urecpe">;
defm SVRSQRTE : SInstZPZ<"svrsqrte", "Ui",   "aarch64_sve_ursqrte">;
}

//------------------------------------------------------------------------------

multiclass SInstZPZxZ<string name, string types, string pat_v, string pat_n, string intrinsic, list<FlagType> flags=[]> {
  def _M   : SInst<name # "[_{d}]", pat_v, types, MergeOp1,  intrinsic, flags>;
  def _X   : SInst<name # "[_{d}]", pat_v, types, MergeAny,  intrinsic, flags>;
  def _Z   : SInst<name # "[_{d}]", pat_v, types, MergeZero, intrinsic, flags>;

  def _N_M : SInst<name # "[_n_{d}]", pat_n, types, MergeOp1,  intrinsic, flags>;
  def _N_X : SInst<name # "[_n_{d}]", pat_n, types, MergeAny,  intrinsic, flags>;
  def _N_Z : SInst<name # "[_n_{d}]", pat_n, types, MergeZero, intrinsic, flags>;
}

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
defm SVQRSHL_S : SInstZPZxZ<"svqrshl", "csil",     "dPdx", "dPdK", "aarch64_sve_sqrshl", [VerifyRuntimeMode]>;
defm SVQRSHL_U : SInstZPZxZ<"svqrshl", "UcUsUiUl", "dPdx", "dPdK", "aarch64_sve_uqrshl", [VerifyRuntimeMode]>;
defm SVQSHL_S  : SInstZPZxZ<"svqshl",  "csil",     "dPdx", "dPdK", "aarch64_sve_sqshl", [VerifyRuntimeMode]>;
defm SVQSHL_U  : SInstZPZxZ<"svqshl",  "UcUsUiUl", "dPdx", "dPdK", "aarch64_sve_uqshl", [VerifyRuntimeMode]>;
defm SVRSHL_S  : SInstZPZxZ<"svrshl",  "csil",     "dPdx", "dPdK", "aarch64_sve_srshl", [VerifyRuntimeMode]>;
defm SVRSHL_U  : SInstZPZxZ<"svrshl",  "UcUsUiUl", "dPdx", "dPdK", "aarch64_sve_urshl", [VerifyRuntimeMode]>;
defm SVSQADD   : SInstZPZxZ<"svsqadd", "UcUsUiUl", "dPdx", "dPdK", "aarch64_sve_usqadd", [VerifyRuntimeMode]>;
defm SVUQADD   : SInstZPZxZ<"svuqadd", "csil",     "dPdu", "dPdL", "aarch64_sve_suqadd", [VerifyRuntimeMode]>;

def SVABA_S        : SInst<"svaba[_{d}]",     "dddd", "csil"    , MergeNone, "aarch64_sve_saba", [VerifyRuntimeMode]>;
def SVABA_U        : SInst<"svaba[_{d}]",     "dddd", "UcUsUiUl", MergeNone, "aarch64_sve_uaba", [VerifyRuntimeMode]>;
def SVQDMULH       : SInst<"svqdmulh[_{d}]",  "ddd",  "csil",     MergeNone, "aarch64_sve_sqdmulh", [VerifyRuntimeMode]>;
def SVQRDMULH      : SInst<"svqrdmulh[_{d}]", "ddd",  "csil",     MergeNone, "aarch64_sve_sqrdmulh", [VerifyRuntimeMode]>;
def SVQRDMLAH      : SInst<"svqrdmlah[_{d}]", "dddd", "csil",     MergeNone, "aarch64_sve_sqrdmlah", [VerifyRuntimeMode]>;
def SVQRDMLSH      : SInst<"svqrdmlsh[_{d}]", "dddd", "csil",     MergeNone, "aarch64_sve_sqrdmlsh", [VerifyRuntimeMode]>;

def SVABA_S_N      : SInst<"svaba[_n_{d}]",     "ddda", "csil",     MergeNone, "aarch64_sve_saba", [VerifyRuntimeMode]>;
def SVABA_U_N      : SInst<"svaba[_n_{d}]",     "ddda", "UcUsUiUl", MergeNone, "aarch64_sve_uaba", [VerifyRuntimeMode]>;
def SVQDMULH_N     : SInst<"svqdmulh[_n_{d}]",  "dda",  "csil",     MergeNone, "aarch64_sve_sqdmulh", [VerifyRuntimeMode]>;
def SVQRDMULH_N    : SInst<"svqrdmulh[_n_{d}]", "dda",  "csil",     MergeNone, "aarch64_sve_sqrdmulh", [VerifyRuntimeMode]>;
def SVQRDMLAH_N    : SInst<"svqrdmlah[_n_{d}]", "ddda", "csil",     MergeNone, "aarch64_sve_sqrdmlah", [VerifyRuntimeMode]>;
def SVQRDMLSH_N    : SInst<"svqrdmlsh[_n_{d}]", "ddda", "csil",     MergeNone, "aarch64_sve_sqrdmlsh", [VerifyRuntimeMode]>;

def SVQDMULH_LANE  : SInst<"svqdmulh_lane[_{d}]",  "dddi",  "sil", MergeNone, "aarch64_sve_sqdmulh_lane",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;
def SVQRDMULH_LANE : SInst<"svqrdmulh_lane[_{d}]", "dddi",  "sil", MergeNone, "aarch64_sve_sqrdmulh_lane", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;
def SVQRDMLAH_LANE : SInst<"svqrdmlah_lane[_{d}]", "ddddi", "sil", MergeNone, "aarch64_sve_sqrdmlah_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVQRDMLSH_LANE : SInst<"svqrdmlsh_lane[_{d}]", "ddddi", "sil", MergeNone, "aarch64_sve_sqrdmlsh_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;

def SVQSHLU_M  : SInst<"svqshlu[_n_{d}]", "uPdi", "csil",         MergeOp1,  "aarch64_sve_sqshlu", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftLeft,  1>]>;
def SVQSHLU_X  : SInst<"svqshlu[_n_{d}]", "uPdi", "csil",         MergeAny,  "aarch64_sve_sqshlu", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftLeft,  1>]>;
def SVQSHLU_Z  : SInst<"svqshlu[_n_{d}]", "uPdi", "csil",         MergeZero, "aarch64_sve_sqshlu", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftLeft,  1>]>;
def SVRSHR_M_S : SInst<"svrshr[_n_{d}]",  "dPdi", "csil",         MergeOp1,  "aarch64_sve_srshr",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVRSHR_M_U : SInst<"svrshr[_n_{d}]",  "dPdi", "UcUsUiUl",     MergeOp1,  "aarch64_sve_urshr",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVRSHR_X_S : SInst<"svrshr[_n_{d}]",  "dPdi", "csil",         MergeAny,  "aarch64_sve_srshr",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVRSHR_X_U : SInst<"svrshr[_n_{d}]",  "dPdi", "UcUsUiUl",     MergeAny,  "aarch64_sve_urshr",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVRSHR_Z_S : SInst<"svrshr[_n_{d}]",  "dPdi", "csil",         MergeZero, "aarch64_sve_srshr",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVRSHR_Z_U : SInst<"svrshr[_n_{d}]",  "dPdi", "UcUsUiUl",     MergeZero, "aarch64_sve_urshr",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVRSRA_S   : SInst<"svrsra[_n_{d}]",  "dddi", "csil",         MergeNone, "aarch64_sve_srsra",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVRSRA_U   : SInst<"svrsra[_n_{d}]",  "dddi", "UcUsUiUl",     MergeNone, "aarch64_sve_ursra",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVSLI      : SInst<"svsli[_n_{d}]",   "dddi", "csilUcUsUiUl", MergeNone, "aarch64_sve_sli",    [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftLeft,  1>]>;
def SVSRA_S    : SInst<"svsra[_n_{d}]",   "dddi", "csil",         MergeNone, "aarch64_sve_ssra",   [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVSRA_U    : SInst<"svsra[_n_{d}]",   "dddi", "UcUsUiUl",     MergeNone, "aarch64_sve_usra",   [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
def SVSRI      : SInst<"svsri[_n_{d}]",   "dddi", "csilUcUsUiUl", MergeNone, "aarch64_sve_sri",    [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Non-widening pairwise arithmetic

multiclass SInstPairwise<string name, string types, string intrinsic, list<FlagType> flags=[]> {
  def _M   : SInst<name # "[_{d}]", "dPdd", types, MergeOp1, intrinsic, flags>;
  def _X   : SInst<name # "[_{d}]", "dPdd", types, MergeAny, intrinsic, flags>;
}

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
defm SVADDP   : SInstPairwise<"svaddp",   "csliUcUsUiUl", "aarch64_sve_addp", [VerifyRuntimeMode]>;
defm SVADDP_F : SInstPairwise<"svaddp",   "hfd",          "aarch64_sve_faddp", [VerifyRuntimeMode]>;
defm SVMAXNMP : SInstPairwise<"svmaxnmp", "hfd",          "aarch64_sve_fmaxnmp", [VerifyRuntimeMode]>;
defm SVMAXP_F : SInstPairwise<"svmaxp",   "hfd",          "aarch64_sve_fmaxp", [VerifyRuntimeMode]>;
defm SVMAXP_S : SInstPairwise<"svmaxp",   "csli",         "aarch64_sve_smaxp", [VerifyRuntimeMode]>;
defm SVMAXP_U : SInstPairwise<"svmaxp",   "UcUsUiUl",     "aarch64_sve_umaxp", [VerifyRuntimeMode]>;
defm SVMINNMP : SInstPairwise<"svminnmp", "hfd",          "aarch64_sve_fminnmp", [VerifyRuntimeMode]>;
defm SVMINP_F : SInstPairwise<"svminp",   "hfd",          "aarch64_sve_fminp", [VerifyRuntimeMode]>;
defm SVMINP_S : SInstPairwise<"svminp",   "csli",         "aarch64_sve_sminp", [VerifyRuntimeMode]>;
defm SVMINP_U : SInstPairwise<"svminp",   "UcUsUiUl",     "aarch64_sve_uminp", [VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Widening pairwise arithmetic

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVADALP_S_M : SInst<"svadalp[_{d}]", "dPdh", "sil",    MergeOp1,  "aarch64_sve_sadalp", [VerifyRuntimeMode]>;
def SVADALP_S_X : SInst<"svadalp[_{d}]", "dPdh", "sil",    MergeAny,  "aarch64_sve_sadalp", [VerifyRuntimeMode]>;
def SVADALP_S_Z : SInst<"svadalp[_{d}]", "dPdh", "sil",    MergeZero, "aarch64_sve_sadalp", [VerifyRuntimeMode]>;

def SVADALP_U_M : SInst<"svadalp[_{d}]", "dPdh", "UsUiUl", MergeOp1,  "aarch64_sve_uadalp", [VerifyRuntimeMode]>;
def SVADALP_U_X : SInst<"svadalp[_{d}]", "dPdh", "UsUiUl", MergeAny,  "aarch64_sve_uadalp", [VerifyRuntimeMode]>;
def SVADALP_U_Z : SInst<"svadalp[_{d}]", "dPdh", "UsUiUl", MergeZero, "aarch64_sve_uadalp", [VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Bitwise ternary logical instructions
//

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVBCAX  : SInst<"svbcax[_{d}]",  "dddd", "csilUcUsUiUl", MergeNone, "aarch64_sve_bcax", [VerifyRuntimeMode]>;
def SVBSL   : SInst<"svbsl[_{d}]",   "dddd", "csilUcUsUiUl", MergeNone, "aarch64_sve_bsl", [VerifyRuntimeMode]>;
def SVBSL1N : SInst<"svbsl1n[_{d}]", "dddd", "csilUcUsUiUl", MergeNone, "aarch64_sve_bsl1n", [VerifyRuntimeMode]>;
def SVBSL2N : SInst<"svbsl2n[_{d}]", "dddd", "csilUcUsUiUl", MergeNone, "aarch64_sve_bsl2n", [VerifyRuntimeMode]>;
def SVEOR3  : SInst<"sveor3[_{d}]",  "dddd", "csilUcUsUiUl", MergeNone, "aarch64_sve_eor3", [VerifyRuntimeMode]>;
def SVNBSL  : SInst<"svnbsl[_{d}]",  "dddd", "csilUcUsUiUl", MergeNone, "aarch64_sve_nbsl", [VerifyRuntimeMode]>;

def SVBCAX_N  : SInst<"svbcax[_n_{d}]",  "ddda", "csilUcUsUiUl", MergeNone, "aarch64_sve_bcax", [VerifyRuntimeMode]>;
def SVBSL_N   : SInst<"svbsl[_n_{d}]",   "ddda", "csilUcUsUiUl", MergeNone, "aarch64_sve_bsl", [VerifyRuntimeMode]>;
def SVBSL1N_N : SInst<"svbsl1n[_n_{d}]", "ddda", "csilUcUsUiUl", MergeNone, "aarch64_sve_bsl1n", [VerifyRuntimeMode]>;
def SVBSL2N_N : SInst<"svbsl2n[_n_{d}]", "ddda", "csilUcUsUiUl", MergeNone, "aarch64_sve_bsl2n", [VerifyRuntimeMode]>;
def SVEOR3_N  : SInst<"sveor3[_n_{d}]",  "ddda", "csilUcUsUiUl", MergeNone, "aarch64_sve_eor3", [VerifyRuntimeMode]>;
def SVNBSL_N  : SInst<"svnbsl[_n_{d}]",  "ddda", "csilUcUsUiUl", MergeNone, "aarch64_sve_nbsl", [VerifyRuntimeMode]>;
def SVXAR_N   : SInst<"svxar[_n_{d}]",   "dddi", "csilUcUsUiUl", MergeNone, "aarch64_sve_xar", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRight, 1>]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Large integer arithmetic

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVADCLB : SInst<"svadclb[_{d}]", "dddd", "UiUl", MergeNone, "aarch64_sve_adclb", [VerifyRuntimeMode]>;
def SVADCLT : SInst<"svadclt[_{d}]", "dddd", "UiUl", MergeNone, "aarch64_sve_adclt", [VerifyRuntimeMode]>;
def SVSBCLB : SInst<"svsbclb[_{d}]", "dddd", "UiUl", MergeNone, "aarch64_sve_sbclb", [VerifyRuntimeMode]>;
def SVSBCLT : SInst<"svsbclt[_{d}]", "dddd", "UiUl", MergeNone, "aarch64_sve_sbclt", [VerifyRuntimeMode]>;

def SVADCLB_N : SInst<"svadclb[_n_{d}]", "ddda", "UiUl", MergeNone, "aarch64_sve_adclb", [VerifyRuntimeMode]>;
def SVADCLT_N : SInst<"svadclt[_n_{d}]", "ddda", "UiUl", MergeNone, "aarch64_sve_adclt", [VerifyRuntimeMode]>;
def SVSBCLB_N : SInst<"svsbclb[_n_{d}]", "ddda", "UiUl", MergeNone, "aarch64_sve_sbclb", [VerifyRuntimeMode]>;
def SVSBCLT_N : SInst<"svsbclt[_n_{d}]", "ddda", "UiUl", MergeNone, "aarch64_sve_sbclt", [VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Multiplication by indexed elements

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVMLA_LANE_2 : SInst<"svmla_lane[_{d}]", "ddddi", "silUsUiUl", MergeNone, "aarch64_sve_mla_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLS_LANE_2 : SInst<"svmls_lane[_{d}]", "ddddi", "silUsUiUl", MergeNone, "aarch64_sve_mls_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMUL_LANE_2 : SInst<"svmul_lane[_{d}]", "dddi",  "silUsUiUl", MergeNone, "aarch64_sve_mul_lane", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Uniform complex integer arithmetic
let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVCADD             : SInst<"svcadd[_{d}]",          "dddi",   "csilUcUsUiUl", MergeNone, "aarch64_sve_cadd_x",           [VerifyRuntimeMode], [ImmCheck<2, ImmCheckComplexRot90_270>]>;
def SVSQCADD           : SInst<"svqcadd[_{d}]",         "dddi",   "csil",         MergeNone, "aarch64_sve_sqcadd_x",         [VerifyRuntimeMode], [ImmCheck<2, ImmCheckComplexRot90_270>]>;
def SVCMLA             : SInst<"svcmla[_{d}]",          "ddddi",  "csilUcUsUiUl", MergeNone, "aarch64_sve_cmla_x",           [VerifyRuntimeMode], [ImmCheck<3, ImmCheckComplexRotAll90>]>;
def SVCMLA_LANE_X      : SInst<"svcmla_lane[_{d}]",     "ddddii", "siUsUi",       MergeNone, "aarch64_sve_cmla_lane_x",      [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndexCompRotate, 2>,
                                                                                                                                  ImmCheck<4, ImmCheckComplexRotAll90>]>;
def SVSQRDCMLAH_X      : SInst<"svqrdcmlah[_{d}]",      "ddddi",  "csil",         MergeNone, "aarch64_sve_sqrdcmlah_x",      [VerifyRuntimeMode], [ImmCheck<3, ImmCheckComplexRotAll90>]>;
def SVSQRDCMLAH_LANE_X : SInst<"svqrdcmlah_lane[_{d}]", "ddddii", "si",           MergeNone, "aarch64_sve_sqrdcmlah_lane_x", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndexCompRotate, 2>,
                                                                                                                                  ImmCheck<4, ImmCheckComplexRotAll90>]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Widening DSP operations

multiclass SInstWideDSPAcc<string name, string types, string intrinsic> {
  def    : SInst<name # "[_{d}]",   "ddhh", types, MergeNone, intrinsic, [VerifyRuntimeMode]>;
  def _N : SInst<name # "[_n_{d}]", "ddhR", types, MergeNone, intrinsic, [VerifyRuntimeMode]>;
}

multiclass SInstWideDSPLong<string name, string types, string intrinsic> {
  def    : SInst<name # "[_{d}]",   "dhh", types, MergeNone, intrinsic, [VerifyRuntimeMode]>;
  def _N : SInst<name # "[_n_{d}]", "dhR", types, MergeNone, intrinsic, [VerifyRuntimeMode]>;
}

multiclass SInstWideDSPWide<string name, string types, string intrinsic> {
  def    : SInst<name # "[_{d}]",   "ddh", types, MergeNone, intrinsic, [VerifyRuntimeMode]>;
  def _N : SInst<name # "[_n_{d}]", "ddR", types, MergeNone, intrinsic, [VerifyRuntimeMode]>;
}

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
defm SVABALB_S : SInstWideDSPAcc<"svabalb",   "sil",    "aarch64_sve_sabalb">;
defm SVABALB_U : SInstWideDSPAcc<"svabalb",   "UsUiUl", "aarch64_sve_uabalb">;
defm SVABALT_S : SInstWideDSPAcc<"svabalt",   "sil",    "aarch64_sve_sabalt">;
defm SVABALT_U : SInstWideDSPAcc<"svabalt",   "UsUiUl", "aarch64_sve_uabalt">;
defm SVMLALB_S : SInstWideDSPAcc<"svmlalb",   "sil",    "aarch64_sve_smlalb">;
defm SVMLALB_U : SInstWideDSPAcc<"svmlalb",   "UsUiUl", "aarch64_sve_umlalb">;
defm SVMLALT_S : SInstWideDSPAcc<"svmlalt",   "sil",    "aarch64_sve_smlalt">;
defm SVMLALT_U : SInstWideDSPAcc<"svmlalt",   "UsUiUl", "aarch64_sve_umlalt">;
defm SVMLSLB_S : SInstWideDSPAcc<"svmlslb",   "sil",    "aarch64_sve_smlslb">;
defm SVMLSLB_U : SInstWideDSPAcc<"svmlslb",   "UsUiUl", "aarch64_sve_umlslb">;
defm SVMLSLT_S : SInstWideDSPAcc<"svmlslt",   "sil",    "aarch64_sve_smlslt">;
defm SVMLSLT_U : SInstWideDSPAcc<"svmlslt",   "UsUiUl", "aarch64_sve_umlslt">;
defm SVQDMLALB : SInstWideDSPAcc<"svqdmlalb", "sil",    "aarch64_sve_sqdmlalb">;
defm SVQDMLALT : SInstWideDSPAcc<"svqdmlalt", "sil",    "aarch64_sve_sqdmlalt">;
defm SVQDMLSLB : SInstWideDSPAcc<"svqdmlslb", "sil",    "aarch64_sve_sqdmlslb">;
defm SVQDMLSLT : SInstWideDSPAcc<"svqdmlslt", "sil",    "aarch64_sve_sqdmlslt">;

defm SVABDLB_S : SInstWideDSPLong<"svabdlb",   "sil",    "aarch64_sve_sabdlb">;
defm SVABDLB_U : SInstWideDSPLong<"svabdlb",   "UsUiUl", "aarch64_sve_uabdlb">;
defm SVABDLT_S : SInstWideDSPLong<"svabdlt",   "sil",    "aarch64_sve_sabdlt">;
defm SVABDLT_U : SInstWideDSPLong<"svabdlt",   "UsUiUl", "aarch64_sve_uabdlt">;
defm SVADDLB_S : SInstWideDSPLong<"svaddlb",   "sil",    "aarch64_sve_saddlb">;
defm SVADDLB_U : SInstWideDSPLong<"svaddlb",   "UsUiUl", "aarch64_sve_uaddlb">;
defm SVADDLT_S : SInstWideDSPLong<"svaddlt",   "sil",    "aarch64_sve_saddlt">;
defm SVADDLT_U : SInstWideDSPLong<"svaddlt",   "UsUiUl", "aarch64_sve_uaddlt">;
defm SVMULLB_S : SInstWideDSPLong<"svmullb",   "sil",    "aarch64_sve_smullb">;
defm SVMULLB_U : SInstWideDSPLong<"svmullb",   "UsUiUl", "aarch64_sve_umullb">;
defm SVMULLT_S : SInstWideDSPLong<"svmullt",   "sil",    "aarch64_sve_smullt">;
defm SVMULLT_U : SInstWideDSPLong<"svmullt",   "UsUiUl", "aarch64_sve_umullt">;
defm SVQDMULLB : SInstWideDSPLong<"svqdmullb", "sil",    "aarch64_sve_sqdmullb">;
defm SVQDMULLT : SInstWideDSPLong<"svqdmullt", "sil",    "aarch64_sve_sqdmullt">;
defm SVSUBLB_S : SInstWideDSPLong<"svsublb",   "sil",    "aarch64_sve_ssublb">;
defm SVSUBLB_U : SInstWideDSPLong<"svsublb",   "UsUiUl", "aarch64_sve_usublb">;
defm SVSUBLT_S : SInstWideDSPLong<"svsublt",   "sil",    "aarch64_sve_ssublt">;
defm SVSUBLT_U : SInstWideDSPLong<"svsublt",   "UsUiUl", "aarch64_sve_usublt">;

defm SVADDWB_S : SInstWideDSPWide<"svaddwb", "sil",    "aarch64_sve_saddwb">;
defm SVADDWB_U : SInstWideDSPWide<"svaddwb", "UsUiUl", "aarch64_sve_uaddwb">;
defm SVADDWT_S : SInstWideDSPWide<"svaddwt", "sil",    "aarch64_sve_saddwt">;
defm SVADDWT_U : SInstWideDSPWide<"svaddwt", "UsUiUl", "aarch64_sve_uaddwt">;
defm SVSUBWB_S : SInstWideDSPWide<"svsubwb", "sil",    "aarch64_sve_ssubwb">;
defm SVSUBWB_U : SInstWideDSPWide<"svsubwb", "UsUiUl", "aarch64_sve_usubwb">;
defm SVSUBWT_S : SInstWideDSPWide<"svsubwt", "sil",    "aarch64_sve_ssubwt">;
defm SVSUBWT_U : SInstWideDSPWide<"svsubwt", "UsUiUl", "aarch64_sve_usubwt">;

def SVSHLLB_S_N : SInst<"svshllb[_n_{d}]", "dhi", "sil",    MergeNone, "aarch64_sve_sshllb", [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftLeft,  0>]>;
def SVSHLLB_U_N : SInst<"svshllb[_n_{d}]", "dhi", "UsUiUl", MergeNone, "aarch64_sve_ushllb", [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftLeft,  0>]>;
def SVSHLLT_S_N : SInst<"svshllt[_n_{d}]", "dhi", "sil",    MergeNone, "aarch64_sve_sshllt", [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftLeft,  0>]>;
def SVSHLLT_U_N : SInst<"svshllt[_n_{d}]", "dhi", "UsUiUl", MergeNone, "aarch64_sve_ushllt", [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftLeft,  0>]>;

def SVMOVLB_S_N : SInst<"svmovlb[_{d}]", "dh",  "sil",    MergeNone, "", [VerifyRuntimeMode]>;
def SVMOVLB_U_N : SInst<"svmovlb[_{d}]", "dh",  "UsUiUl", MergeNone, "", [VerifyRuntimeMode]>;
def SVMOVLT_S_N : SInst<"svmovlt[_{d}]", "dh",  "sil",    MergeNone, "", [VerifyRuntimeMode]>;
def SVMOVLT_U_N : SInst<"svmovlt[_{d}]", "dh",  "UsUiUl", MergeNone, "", [VerifyRuntimeMode]>;

def SVMLALB_S_LANE : SInst<"svmlalb_lane[_{d}]",   "ddhhi", "il",   MergeNone, "aarch64_sve_smlalb_lane",   [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLALB_U_LANE : SInst<"svmlalb_lane[_{d}]",   "ddhhi", "UiUl", MergeNone, "aarch64_sve_umlalb_lane",   [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLALT_S_LANE : SInst<"svmlalt_lane[_{d}]",   "ddhhi", "il",   MergeNone, "aarch64_sve_smlalt_lane",   [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLALT_U_LANE : SInst<"svmlalt_lane[_{d}]",   "ddhhi", "UiUl", MergeNone, "aarch64_sve_umlalt_lane",   [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLSLB_S_LANE : SInst<"svmlslb_lane[_{d}]",   "ddhhi", "il",   MergeNone, "aarch64_sve_smlslb_lane",   [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLSLB_U_LANE : SInst<"svmlslb_lane[_{d}]",   "ddhhi", "UiUl", MergeNone, "aarch64_sve_umlslb_lane",   [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLSLT_S_LANE : SInst<"svmlslt_lane[_{d}]",   "ddhhi", "il",   MergeNone, "aarch64_sve_smlslt_lane",   [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLSLT_U_LANE : SInst<"svmlslt_lane[_{d}]",   "ddhhi", "UiUl", MergeNone, "aarch64_sve_umlslt_lane",   [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMULLB_S_LANE : SInst<"svmullb_lane[_{d}]",   "dhhi",  "il",   MergeNone, "aarch64_sve_smullb_lane",   [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;
def SVMULLB_U_LANE : SInst<"svmullb_lane[_{d}]",   "dhhi",  "UiUl", MergeNone, "aarch64_sve_umullb_lane",   [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;
def SVMULLT_S_LANE : SInst<"svmullt_lane[_{d}]",   "dhhi",  "il",   MergeNone, "aarch64_sve_smullt_lane",   [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;
def SVMULLT_U_LANE : SInst<"svmullt_lane[_{d}]",   "dhhi",  "UiUl", MergeNone, "aarch64_sve_umullt_lane",   [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;
def SVQDMLALB_LANE : SInst<"svqdmlalb_lane[_{d}]", "ddhhi", "il",   MergeNone, "aarch64_sve_sqdmlalb_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVQDMLALT_LANE : SInst<"svqdmlalt_lane[_{d}]", "ddhhi", "il",   MergeNone, "aarch64_sve_sqdmlalt_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVQDMLSLB_LANE : SInst<"svqdmlslb_lane[_{d}]", "ddhhi", "il",   MergeNone, "aarch64_sve_sqdmlslb_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVQDMLSLT_LANE : SInst<"svqdmlslt_lane[_{d}]", "ddhhi", "il",   MergeNone, "aarch64_sve_sqdmlslt_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVQDMULLB_LANE : SInst<"svqdmullb_lane[_{d}]", "dhhi",  "il",   MergeNone, "aarch64_sve_sqdmullb_lane", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;
def SVQDMULLT_LANE : SInst<"svqdmullt_lane[_{d}]", "dhhi",  "il",   MergeNone, "aarch64_sve_sqdmullt_lane", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Narrowing DSP operations

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVADDHNB   : SInst<"svaddhnb[_{d}]",     "hdd",  "silUsUiUl", MergeNone, "aarch64_sve_addhnb", [VerifyRuntimeMode]>;
def SVADDHNT   : SInst<"svaddhnt[_{d}]",     "hhdd", "silUsUiUl", MergeNone, "aarch64_sve_addhnt", [VerifyRuntimeMode]>;
def SVRADDHNB  : SInst<"svraddhnb[_{d}]",    "hdd",  "silUsUiUl", MergeNone, "aarch64_sve_raddhnb", [VerifyRuntimeMode]>;
def SVRADDHNT  : SInst<"svraddhnt[_{d}]",    "hhdd", "silUsUiUl", MergeNone, "aarch64_sve_raddhnt", [VerifyRuntimeMode]>;
def SVRSUBHNB  : SInst<"svrsubhnb[_{d}]",    "hdd",  "silUsUiUl", MergeNone, "aarch64_sve_rsubhnb", [VerifyRuntimeMode]>;
def SVRSUBHNT  : SInst<"svrsubhnt[_{d}]",    "hhdd", "silUsUiUl", MergeNone, "aarch64_sve_rsubhnt", [VerifyRuntimeMode]>;
def SVSUBHNB   : SInst<"svsubhnb[_{d}]",     "hdd",  "silUsUiUl", MergeNone, "aarch64_sve_subhnb", [VerifyRuntimeMode]>;
def SVSUBHNT   : SInst<"svsubhnt[_{d}]",     "hhdd", "silUsUiUl", MergeNone, "aarch64_sve_subhnt", [VerifyRuntimeMode]>;

def SVADDHNB_N  : SInst<"svaddhnb[_n_{d}]",  "hda",  "silUsUiUl", MergeNone, "aarch64_sve_addhnb", [VerifyRuntimeMode]>;
def SVADDHNT_N  : SInst<"svaddhnt[_n_{d}]",  "hhda", "silUsUiUl", MergeNone, "aarch64_sve_addhnt", [VerifyRuntimeMode]>;
def SVRADDHNB_N : SInst<"svraddhnb[_n_{d}]", "hda",  "silUsUiUl", MergeNone, "aarch64_sve_raddhnb", [VerifyRuntimeMode]>;
def SVRADDHNT_N : SInst<"svraddhnt[_n_{d}]", "hhda", "silUsUiUl", MergeNone, "aarch64_sve_raddhnt", [VerifyRuntimeMode]>;
def SVRSUBHNB_N : SInst<"svrsubhnb[_n_{d}]", "hda",  "silUsUiUl", MergeNone, "aarch64_sve_rsubhnb", [VerifyRuntimeMode]>;
def SVRSUBHNT_N : SInst<"svrsubhnt[_n_{d}]", "hhda", "silUsUiUl", MergeNone, "aarch64_sve_rsubhnt", [VerifyRuntimeMode]>;
def SVSUBHNB_N  : SInst<"svsubhnb[_n_{d}]",  "hda",  "silUsUiUl", MergeNone, "aarch64_sve_subhnb", [VerifyRuntimeMode]>;
def SVSUBHNT_N  : SInst<"svsubhnt[_n_{d}]",  "hhda", "silUsUiUl", MergeNone, "aarch64_sve_subhnt", [VerifyRuntimeMode]>;

def SVSHRNB      : SInst<"svshrnb[_n_{d}]",    "hdi",  "silUsUiUl", MergeNone, "aarch64_sve_shrnb",     [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftRightNarrow, 0>]>;
def SVRSHRNB     : SInst<"svrshrnb[_n_{d}]",   "hdi",  "silUsUiUl", MergeNone, "aarch64_sve_rshrnb",    [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftRightNarrow, 0>]>;
def SVQSHRUNB    : SInst<"svqshrunb[_n_{d}]",  "edi",  "sil",       MergeNone, "aarch64_sve_sqshrunb",  [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftRightNarrow, 0>]>;
def SVQRSHRUNB   : SInst<"svqrshrunb[_n_{d}]", "edi",  "sil",       MergeNone, "aarch64_sve_sqrshrunb", [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftRightNarrow, 0>]>;
def SVQSHRNB_S   : SInst<"svqshrnb[_n_{d}]",   "hdi",  "sil",       MergeNone, "aarch64_sve_sqshrnb",   [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftRightNarrow, 0>]>;
def SVQSHRNB_U   : SInst<"svqshrnb[_n_{d}]",   "hdi",  "UsUiUl",    MergeNone, "aarch64_sve_uqshrnb",   [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftRightNarrow, 0>]>;
def SVQRSHRNB_S  : SInst<"svqrshrnb[_n_{d}]",  "hdi",  "sil",       MergeNone, "aarch64_sve_sqrshrnb",  [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftRightNarrow, 0>]>;
def SVQRSHRNB_U  : SInst<"svqrshrnb[_n_{d}]",  "hdi",  "UsUiUl",    MergeNone, "aarch64_sve_uqrshrnb",  [VerifyRuntimeMode], [ImmCheck<1, ImmCheckShiftRightNarrow, 0>]>;

def SVSHRNT      : SInst<"svshrnt[_n_{d}]",    "hhdi", "silUsUiUl", MergeNone, "aarch64_sve_shrnt",     [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRightNarrow, 1>]>;
def SVRSHRNT     : SInst<"svrshrnt[_n_{d}]",   "hhdi", "silUsUiUl", MergeNone, "aarch64_sve_rshrnt",    [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRightNarrow, 1>]>;
def SVQSHRUNT    : SInst<"svqshrunt[_n_{d}]",  "eedi", "sil",       MergeNone, "aarch64_sve_sqshrunt",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRightNarrow, 1>]>;
def SVQRSHRUNT   : SInst<"svqrshrunt[_n_{d}]", "eedi", "sil",       MergeNone, "aarch64_sve_sqrshrunt", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRightNarrow, 1>]>;
def SVQSHRNT_S   : SInst<"svqshrnt[_n_{d}]",   "hhdi", "sil",       MergeNone, "aarch64_sve_sqshrnt",   [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRightNarrow, 1>]>;
def SVQSHRNT_U   : SInst<"svqshrnt[_n_{d}]",   "hhdi", "UsUiUl",    MergeNone, "aarch64_sve_uqshrnt",   [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRightNarrow, 1>]>;
def SVQRSHRNT_S  : SInst<"svqrshrnt[_n_{d}]",  "hhdi", "sil",       MergeNone, "aarch64_sve_sqrshrnt",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRightNarrow, 1>]>;
def SVQRSHRNT_U  : SInst<"svqrshrnt[_n_{d}]",  "hhdi", "UsUiUl",    MergeNone, "aarch64_sve_uqrshrnt",  [VerifyRuntimeMode], [ImmCheck<2, ImmCheckShiftRightNarrow, 1>]>;
}
////////////////////////////////////////////////////////////////////////////////
// SVE2 - Unary narrowing operations

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVQXTNB_S  : SInst<"svqxtnb[_{d}]",  "hd",  "sil",     MergeNone, "aarch64_sve_sqxtnb", [VerifyRuntimeMode]>;
def SVQXTNB_U  : SInst<"svqxtnb[_{d}]",  "hd",  "UsUiUl",  MergeNone, "aarch64_sve_uqxtnb", [VerifyRuntimeMode]>;
def SVQXTUNB_S : SInst<"svqxtunb[_{d}]", "ed",  "sil",     MergeNone, "aarch64_sve_sqxtunb", [VerifyRuntimeMode]>;

def SVQXTNT_S  : SInst<"svqxtnt[_{d}]",  "hhd", "sil",     MergeNone, "aarch64_sve_sqxtnt", [VerifyRuntimeMode]>;
def SVQXTNT_U  : SInst<"svqxtnt[_{d}]",  "hhd", "UsUiUl",  MergeNone, "aarch64_sve_uqxtnt", [VerifyRuntimeMode]>;
def SVQXTUNT_S : SInst<"svqxtunt[_{d}]", "eed", "sil",     MergeNone, "aarch64_sve_sqxtunt", [VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Widening complex integer arithmetic

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
defm SVADDLBT : SInstWideDSPLong<"svaddlbt", "sil", "aarch64_sve_saddlbt">;
defm SVSUBLBT : SInstWideDSPLong<"svsublbt", "sil", "aarch64_sve_ssublbt">;
defm SVSUBLTB : SInstWideDSPLong<"svsubltb", "sil", "aarch64_sve_ssubltb">;

defm SVQDMLALBT : SInstWideDSPAcc<"svqdmlalbt", "sil", "aarch64_sve_sqdmlalbt">;
defm SVQDMLSLBT : SInstWideDSPAcc<"svqdmlslbt", "sil", "aarch64_sve_sqdmlslbt">;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Non-temporal gather/scatter
let SVETargetGuard = "sve2", SMETargetGuard = InvalidMode in {
// Non-temporal gather load one vector (vector base)
def SVLDNT1_GATHER_BASES_U   : MInst<"svldnt1_gather[_{2}base]_{0}",   "dPu", "ilUiUlfd", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1SB_GATHER_BASES_U : MInst<"svldnt1sb_gather[_{2}base]_{0}", "dPu", "ilUiUl",   [IsGatherLoad],               MemEltTyInt8,    "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1UB_GATHER_BASES_U : MInst<"svldnt1ub_gather[_{2}base]_{0}", "dPu", "ilUiUl",   [IsGatherLoad, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1SH_GATHER_BASES_U : MInst<"svldnt1sh_gather[_{2}base]_{0}", "dPu", "ilUiUl",   [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1UH_GATHER_BASES_U : MInst<"svldnt1uh_gather[_{2}base]_{0}", "dPu", "ilUiUl",   [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1SW_GATHER_BASES_U : MInst<"svldnt1sw_gather[_{2}base]_{0}", "dPu", "lUl",      [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1UW_GATHER_BASES_U : MInst<"svldnt1uw_gather[_{2}base]_{0}", "dPu", "lUl",      [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldnt1_gather_scalar_offset">;

// Non-temporal gather load one vector (scalar base, signed vector offset in bytes)
def SVLDNT1_GATHER_64B_OFFSETS_S   : MInst<"svldnt1_gather_[{3}]offset[_{0}]", "dPcx", "lUld", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ldnt1_gather">;
def SVLDNT1SB_GATHER_64B_OFFSETS_S : MInst<"svldnt1sb_gather_[{3}]offset_{0}", "dPSx", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ldnt1_gather">;
def SVLDNT1UB_GATHER_64B_OFFSETS_S : MInst<"svldnt1ub_gather_[{3}]offset_{0}", "dPWx", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldnt1_gather">;
def SVLDNT1SH_GATHER_64B_OFFSETS_S : MInst<"svldnt1sh_gather_[{3}]offset_{0}", "dPTx", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ldnt1_gather">;
def SVLDNT1UH_GATHER_64B_OFFSETS_S : MInst<"svldnt1uh_gather_[{3}]offset_{0}", "dPXx", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldnt1_gather">;
def SVLDNT1SW_GATHER_64B_OFFSETS_S : MInst<"svldnt1sw_gather_[{3}]offset_{0}", "dPUx", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt32,   "aarch64_sve_ldnt1_gather">;
def SVLDNT1UW_GATHER_64B_OFFSETS_S : MInst<"svldnt1uw_gather_[{3}]offset_{0}", "dPYx", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldnt1_gather">;

// Non-temporal gather load one vector (scalar base, unsigned vector offset in bytes)
def SVLDNT1_GATHER_64B_OFFSETS_U   : MInst<"svldnt1_gather_[{3}]offset[_{0}]", "dPcu", "lUld", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ldnt1_gather">;
def SVLDNT1SB_GATHER_64B_OFFSETS_U : MInst<"svldnt1sb_gather_[{3}]offset_{0}", "dPSu", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ldnt1_gather">;
def SVLDNT1UB_GATHER_64B_OFFSETS_U : MInst<"svldnt1ub_gather_[{3}]offset_{0}", "dPWu", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldnt1_gather">;
def SVLDNT1SH_GATHER_64B_OFFSETS_U : MInst<"svldnt1sh_gather_[{3}]offset_{0}", "dPTu", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ldnt1_gather">;
def SVLDNT1UH_GATHER_64B_OFFSETS_U : MInst<"svldnt1uh_gather_[{3}]offset_{0}", "dPXu", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldnt1_gather">;
def SVLDNT1SW_GATHER_64B_OFFSETS_U : MInst<"svldnt1sw_gather_[{3}]offset_{0}", "dPUu", "lUl",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt32,   "aarch64_sve_ldnt1_gather">;
def SVLDNT1UW_GATHER_64B_OFFSETS_U : MInst<"svldnt1uw_gather_[{3}]offset_{0}", "dPYu", "lUl",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldnt1_gather">;

def SVLDNT1_GATHER_32B_OFFSETS_U   : MInst<"svldnt1_gather_[{3}]offset[_{0}]", "dPcu", "iUif", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ldnt1_gather_uxtw">;
def SVLDNT1SB_GATHER_32B_OFFSETS_U : MInst<"svldnt1sb_gather_[{3}]offset_{0}", "dPSu", "iUi",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ldnt1_gather_uxtw">;
def SVLDNT1UB_GATHER_32B_OFFSETS_U : MInst<"svldnt1ub_gather_[{3}]offset_{0}", "dPWu", "iUi",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldnt1_gather_uxtw">;
def SVLDNT1SH_GATHER_32B_OFFSETS_U : MInst<"svldnt1sh_gather_[{3}]offset_{0}", "dPTu", "iUi",  [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ldnt1_gather_uxtw">;
def SVLDNT1UH_GATHER_32B_OFFSETS_U : MInst<"svldnt1uh_gather_[{3}]offset_{0}", "dPXu", "iUi",  [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldnt1_gather_uxtw">;

// Non-temporal gather load one vector (vector base, scalar offset in bytes)
def SVLDNT1_GATHER_OFFSET_S   : MInst<"svldnt1_gather[_{2}base]_offset_{0}",   "dPul", "ilUiUlfd", [IsGatherLoad, IsByteIndexed],               MemEltTyDefault, "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1SB_GATHER_OFFSET_S : MInst<"svldnt1sb_gather[_{2}base]_offset_{0}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed],               MemEltTyInt8,    "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1UB_GATHER_OFFSET_S : MInst<"svldnt1ub_gather[_{2}base]_offset_{0}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt8,    "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1SH_GATHER_OFFSET_S : MInst<"svldnt1sh_gather[_{2}base]_offset_{0}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed],               MemEltTyInt16,   "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1UH_GATHER_OFFSET_S : MInst<"svldnt1uh_gather[_{2}base]_offset_{0}", "dPul", "ilUiUl",   [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1SW_GATHER_OFFSET_S : MInst<"svldnt1sw_gather[_{2}base]_offset_{0}", "dPul", "lUl",      [IsGatherLoad, IsByteIndexed],               MemEltTyInt32,   "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1UW_GATHER_OFFSET_S : MInst<"svldnt1uw_gather[_{2}base]_offset_{0}", "dPul", "lUl",      [IsGatherLoad, IsByteIndexed, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldnt1_gather_scalar_offset">;

// Non-temporal gather load one vector (scalar base, signed vector index)
def SVLDNT1_GATHER_64B_INDICES_S   : MInst<"svldnt1_gather_[{3}]index[_{0}]", "dPcx", "lUld", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ldnt1_gather_index">;
def SVLDNT1SH_GATHER_64B_INDICES_S : MInst<"svldnt1sh_gather_[{3}]index_{0}", "dPTx", "lUl",  [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ldnt1_gather_index">;
def SVLDNT1UH_GATHER_64B_INDICES_S : MInst<"svldnt1uh_gather_[{3}]index_{0}", "dPXx", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldnt1_gather_index">;
def SVLDNT1SW_GATHER_64B_INDICES_S : MInst<"svldnt1sw_gather_[{3}]index_{0}", "dPUx", "lUl",  [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ldnt1_gather_index">;
def SVLDNT1UW_GATHER_64B_INDICES_S : MInst<"svldnt1uw_gather_[{3}]index_{0}", "dPYx", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldnt1_gather_index">;

// Non temporal gather load one vector (scalar base, unsigned vector index)
def SVLDNT1_GATHER_64B_INDICES_U   : MInst<"svldnt1_gather_[{3}]index[_{0}]", "dPcu", "lUld", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ldnt1_gather_index">;
def SVLDNT1SH_GATHER_64B_INDICES_U : MInst<"svldnt1sh_gather_[{3}]index_{0}", "dPTu", "lUl",  [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ldnt1_gather_index">;
def SVLDNT1UH_GATHER_64B_INDICES_U : MInst<"svldnt1uh_gather_[{3}]index_{0}", "dPXu", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldnt1_gather_index">;
def SVLDNT1SW_GATHER_64B_INDICES_U : MInst<"svldnt1sw_gather_[{3}]index_{0}", "dPUu", "lUl",  [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ldnt1_gather_index">;
def SVLDNT1UW_GATHER_64B_INDICES_U : MInst<"svldnt1uw_gather_[{3}]index_{0}", "dPYu", "lUl",  [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldnt1_gather_index">;

// Non-temporal gather load one vector (vector base, signed scalar index)
def SVLDNT1_GATHER_INDEX_S   : MInst<"svldnt1_gather[_{2}base]_index_{0}",   "dPul", "ilUiUlfd", [IsGatherLoad],               MemEltTyDefault, "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1SH_GATHER_INDEX_S : MInst<"svldnt1sh_gather[_{2}base]_index_{0}", "dPul", "ilUiUl",   [IsGatherLoad],               MemEltTyInt16,   "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1UH_GATHER_INDEX_S : MInst<"svldnt1uh_gather[_{2}base]_index_{0}", "dPul", "ilUiUl",   [IsGatherLoad, IsZExtReturn], MemEltTyInt16,   "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1SW_GATHER_INDEX_S : MInst<"svldnt1sw_gather[_{2}base]_index_{0}", "dPul", "lUl",      [IsGatherLoad],               MemEltTyInt32,   "aarch64_sve_ldnt1_gather_scalar_offset">;
def SVLDNT1UW_GATHER_INDEX_S : MInst<"svldnt1uw_gather[_{2}base]_index_{0}", "dPul", "lUl",      [IsGatherLoad, IsZExtReturn], MemEltTyInt32,   "aarch64_sve_ldnt1_gather_scalar_offset">;

// Non-temporal scatter store one vector (vector base)
def SVSTNT1_SCATTER_BASES_U  : MInst<"svstnt1_scatter[_{2}base_{d}]",  "vPud", "ilUiUlfd", [IsScatterStore], MemEltTyDefault, "aarch64_sve_stnt1_scatter_scalar_offset">;
def SVSTNT1B_SCATTER_BASES_U : MInst<"svstnt1b_scatter[_{2}base_{d}]", "vPud", "ilUiUl",   [IsScatterStore], MemEltTyInt8,    "aarch64_sve_stnt1_scatter_scalar_offset">;
def SVSTNT1H_SCATTER_BASES_U : MInst<"svstnt1h_scatter[_{2}base_{d}]", "vPud", "ilUiUl",   [IsScatterStore], MemEltTyInt16,   "aarch64_sve_stnt1_scatter_scalar_offset">;
def SVSTNT1W_SCATTER_BASES_U : MInst<"svstnt1w_scatter[_{2}base_{d}]", "vPud", "lUl",      [IsScatterStore], MemEltTyInt32,   "aarch64_sve_stnt1_scatter_scalar_offset">;

// Non-temporal scatter store one vector (scalar base, signed vector offset in bytes)
def SVSTNT1_SCATTER_64B_OFFSETS_S   : MInst<"svstnt1_scatter_[{3}]offset[_{d}]",  "vPpxd", "lUld", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_stnt1_scatter">;
def SVSTNT1B_SCATTER_64B_OFFSETS_SS : MInst<"svstnt1b_scatter_[{3}]offset[_{d}]", "vPAxd", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_stnt1_scatter">;
def SVSTNT1B_SCATTER_64B_OFFSETS_SU : MInst<"svstnt1b_scatter_[{3}]offset[_{d}]", "vPExd", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_stnt1_scatter">;
def SVSTNT1H_SCATTER_64B_OFFSETS_SS : MInst<"svstnt1h_scatter_[{3}]offset[_{d}]", "vPBxd", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_stnt1_scatter">;
def SVSTNT1H_SCATTER_64B_OFFSETS_SU : MInst<"svstnt1h_scatter_[{3}]offset[_{d}]", "vPFxd", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_stnt1_scatter">;
def SVSTNT1W_SCATTER_64B_OFFSETS_SS : MInst<"svstnt1w_scatter_[{3}]offset[_{d}]", "vPCxd", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt32,   "aarch64_sve_stnt1_scatter">;
def SVSTNT1W_SCATTER_64B_OFFSETS_SU : MInst<"svstnt1w_scatter_[{3}]offset[_{d}]", "vPGxd", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt32,   "aarch64_sve_stnt1_scatter">;

// Non-temporal scatter store one vector (scalar base, unsigned vector offset in bytes)
def SVSTNT1_SCATTER_64B_OFFSETS_U   : MInst<"svstnt1_scatter_[{3}]offset[_{d}]",  "vPpud", "lUld", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_stnt1_scatter">;
def SVSTNT1B_SCATTER_64B_OFFSETS_US : MInst<"svstnt1b_scatter_[{3}]offset[_{d}]", "vPAud", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_stnt1_scatter">;
def SVSTNT1B_SCATTER_64B_OFFSETS_UU : MInst<"svstnt1b_scatter_[{3}]offset[_{d}]", "vPEud", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_stnt1_scatter">;
def SVSTNT1H_SCATTER_64B_OFFSETS_US : MInst<"svstnt1h_scatter_[{3}]offset[_{d}]", "vPBud", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_stnt1_scatter">;
def SVSTNT1H_SCATTER_64B_OFFSETS_UU : MInst<"svstnt1h_scatter_[{3}]offset[_{d}]", "vPFud", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_stnt1_scatter">;
def SVSTNT1W_SCATTER_64B_OFFSETS_US : MInst<"svstnt1w_scatter_[{3}]offset[_{d}]", "vPCud", "l",    [IsScatterStore, IsByteIndexed], MemEltTyInt32,   "aarch64_sve_stnt1_scatter">;
def SVSTNT1W_SCATTER_64B_OFFSETS_UU : MInst<"svstnt1w_scatter_[{3}]offset[_{d}]", "vPGud", "Ul",   [IsScatterStore, IsByteIndexed], MemEltTyInt32,   "aarch64_sve_stnt1_scatter">;

def SVSTNT1_SCATTER_32B_OFFSETS_U   : MInst<"svstnt1_scatter_[{3}]offset[_{d}]",  "vPpud", "iUif", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_stnt1_scatter_uxtw">;
def SVSTNT1B_SCATTER_32B_OFFSETS_US : MInst<"svstnt1b_scatter_[{3}]offset[_{d}]", "vPAud", "i",    [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_stnt1_scatter_uxtw">;
def SVSTNT1B_SCATTER_32B_OFFSETS_UU : MInst<"svstnt1b_scatter_[{3}]offset[_{d}]", "vPEud", "Ui",   [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_stnt1_scatter_uxtw">;
def SVSTNT1H_SCATTER_32B_OFFSETS_US : MInst<"svstnt1h_scatter_[{3}]offset[_{d}]", "vPBud", "i",    [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_stnt1_scatter_uxtw">;
def SVSTNT1H_SCATTER_32B_OFFSETS_UU : MInst<"svstnt1h_scatter_[{3}]offset[_{d}]", "vPFud", "Ui",   [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_stnt1_scatter_uxtw">;

// Non-temporal scatter store one vector (vector base, scalar offset in bytes)
def SVSTNT1_SCATTER_OFFSET_S  : MInst<"svstnt1_scatter[_{2}base]_offset[_{d}]",  "vPuld", "ilUiUlfd", [IsScatterStore, IsByteIndexed], MemEltTyDefault, "aarch64_sve_stnt1_scatter_scalar_offset">;
def SVSTNT1B_SCATTER_OFFSET_S : MInst<"svstnt1b_scatter[_{2}base]_offset[_{d}]", "vPuld", "ilUiUl",   [IsScatterStore, IsByteIndexed], MemEltTyInt8,    "aarch64_sve_stnt1_scatter_scalar_offset">;
def SVSTNT1H_SCATTER_OFFSET_S : MInst<"svstnt1h_scatter[_{2}base]_offset[_{d}]", "vPuld", "ilUiUl",   [IsScatterStore, IsByteIndexed], MemEltTyInt16,   "aarch64_sve_stnt1_scatter_scalar_offset">;
def SVSTNT1W_SCATTER_OFFSET_S : MInst<"svstnt1w_scatter[_{2}base]_offset[_{d}]", "vPuld", "lUl",      [IsScatterStore, IsByteIndexed], MemEltTyInt32,   "aarch64_sve_stnt1_scatter_scalar_offset">;

// Non-temporal scatter store one vector (scalar base, signed vector index)
def SVSTNT1_SCATTER_INDICES_S   : MInst<"svstnt1_scatter_[{3}]index[_{d}]",  "vPpxd", "lUld", [IsScatterStore], MemEltTyDefault, "aarch64_sve_stnt1_scatter_index">;
def SVSTNT1H_SCATTER_INDICES_SS : MInst<"svstnt1h_scatter_[{3}]index[_{d}]", "vPBxd", "l",    [IsScatterStore], MemEltTyInt16,   "aarch64_sve_stnt1_scatter_index">;
def SVSTNT1H_SCATTER_INDICES_SU : MInst<"svstnt1h_scatter_[{3}]index[_{d}]", "vPFxd", "Ul",   [IsScatterStore], MemEltTyInt16,   "aarch64_sve_stnt1_scatter_index">;
def SVSTNT1W_SCATTER_INDICES_SS : MInst<"svstnt1w_scatter_[{3}]index[_{d}]", "vPCxd", "l",    [IsScatterStore], MemEltTyInt32,   "aarch64_sve_stnt1_scatter_index">;
def SVSTNT1W_SCATTER_INDICES_SU : MInst<"svstnt1w_scatter_[{3}]index[_{d}]", "vPGxd", "Ul",   [IsScatterStore], MemEltTyInt32,   "aarch64_sve_stnt1_scatter_index">;

// Non-temporal scatter store one vector (scalar base, unsigned vector index)
def SVSTNT1_SCATTER_INDICES_U   : MInst<"svstnt1_scatter_[{3}]index[_{d}]",  "vPpud", "lUld", [IsScatterStore], MemEltTyDefault, "aarch64_sve_stnt1_scatter_index">;
def SVSTNT1H_SCATTER_INDICES_US : MInst<"svstnt1h_scatter_[{3}]index[_{d}]", "vPBud", "l",    [IsScatterStore], MemEltTyInt16,   "aarch64_sve_stnt1_scatter_index">;
def SVSTNT1H_SCATTER_INDICES_UU : MInst<"svstnt1h_scatter_[{3}]index[_{d}]", "vPFud", "Ul",   [IsScatterStore], MemEltTyInt16,   "aarch64_sve_stnt1_scatter_index">;
def SVSTNT1W_SCATTER_INDICES_US : MInst<"svstnt1w_scatter_[{3}]index[_{d}]", "vPCud", "l",    [IsScatterStore], MemEltTyInt32,   "aarch64_sve_stnt1_scatter_index">;
def SVSTNT1W_SCATTER_INDICES_UU : MInst<"svstnt1w_scatter_[{3}]index[_{d}]", "vPGud", "Ul",   [IsScatterStore], MemEltTyInt32,   "aarch64_sve_stnt1_scatter_index">;

// Non-temporal scatter store one vector (vector base, signed scalar index)
def SVSTNT1_SCATTER_INDEX_S  : MInst<"svstnt1_scatter[_{2}base]_index[_{d}]",  "vPuld", "ilUiUlfd", [IsScatterStore], MemEltTyDefault, "aarch64_sve_stnt1_scatter_scalar_offset">;
def SVSTNT1H_SCATTER_INDEX_S : MInst<"svstnt1h_scatter[_{2}base]_index[_{d}]", "vPuld", "ilUiUl",   [IsScatterStore], MemEltTyInt16,   "aarch64_sve_stnt1_scatter_scalar_offset">;
def SVSTNT1W_SCATTER_INDEX_S : MInst<"svstnt1w_scatter[_{2}base]_index[_{d}]", "vPuld", "lUl",      [IsScatterStore], MemEltTyInt32,   "aarch64_sve_stnt1_scatter_scalar_offset">;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Polynomial arithmetic

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVEORBT         : SInst<"sveorbt[_{d}]",         "dddd", "csilUcUsUiUl", MergeNone, "aarch64_sve_eorbt", [VerifyRuntimeMode]>;
def SVEORBT_N       : SInst<"sveorbt[_n_{d}]",       "ddda", "csilUcUsUiUl", MergeNone, "aarch64_sve_eorbt", [VerifyRuntimeMode]>;
def SVEORTB         : SInst<"sveortb[_{d}]",         "dddd", "csilUcUsUiUl", MergeNone, "aarch64_sve_eortb", [VerifyRuntimeMode]>;
def SVEORTB_N       : SInst<"sveortb[_n_{d}]",       "ddda", "csilUcUsUiUl", MergeNone, "aarch64_sve_eortb", [VerifyRuntimeMode]>;
def SVPMUL          : SInst<"svpmul[_{d}]",          "ddd",  "Uc",           MergeNone, "aarch64_sve_pmul", [VerifyRuntimeMode]>;
def SVPMUL_N        : SInst<"svpmul[_n_{d}]",        "dda",  "Uc",           MergeNone, "aarch64_sve_pmul", [VerifyRuntimeMode]>;
def SVPMULLB        : SInst<"svpmullb[_{d}]",        "dhh",  "UsUl",         MergeNone, "", [VerifyRuntimeMode]>;
def SVPMULLB_N      : SInst<"svpmullb[_n_{d}]",      "dhR",  "UsUl",         MergeNone, "", [VerifyRuntimeMode]>;
def SVPMULLB_PAIR   : SInst<"svpmullb_pair[_{d}]",   "ddd",  "UcUi",         MergeNone, "aarch64_sve_pmullb_pair", [VerifyRuntimeMode]>;
def SVPMULLB_PAIR_N : SInst<"svpmullb_pair[_n_{d}]", "dda",  "UcUi",         MergeNone, "aarch64_sve_pmullb_pair", [VerifyRuntimeMode]>;
def SVPMULLT        : SInst<"svpmullt[_{d}]",        "dhh",  "UsUl",         MergeNone, "", [VerifyRuntimeMode]>;
def SVPMULLT_N      : SInst<"svpmullt[_n_{d}]",      "dhR",  "UsUl",         MergeNone, "", [VerifyRuntimeMode]>;
def SVPMULLT_PAIR   : SInst<"svpmullt_pair[_{d}]",   "ddd",  "UcUi",         MergeNone, "aarch64_sve_pmullt_pair", [VerifyRuntimeMode]>;
def SVPMULLT_PAIR_N : SInst<"svpmullt_pair[_n_{d}]", "dda",  "UcUi",         MergeNone, "aarch64_sve_pmullt_pair", [VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Complex integer dot product

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVCDOT      : SInst<"svcdot[_{d}]",      "ddqqi",  "il",   MergeNone, "aarch64_sve_cdot",      [VerifyRuntimeMode], [ImmCheck<3, ImmCheckComplexRotAll90>]>;
def SVCDOT_LANE : SInst<"svcdot_lane[_{d}]", "ddqqii", "il",   MergeNone, "aarch64_sve_cdot_lane", [VerifyRuntimeMode], [ImmCheck<4, ImmCheckComplexRotAll90>,
                                                                                                        ImmCheck<3, ImmCheckLaneIndexDot, 2>]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Floating-point widening multiply-accumulate

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVMLALB_F      : SInst<"svmlalb[_{d}]",      "ddhh",  "f",   MergeNone, "aarch64_sve_fmlalb", [VerifyRuntimeMode]>;
def SVMLALB_F_N    : SInst<"svmlalb[_n_{d}]",    "ddhR",  "f",   MergeNone, "aarch64_sve_fmlalb", [VerifyRuntimeMode]>;
def SVMLALB_F_LANE : SInst<"svmlalb_lane[_{d}]", "ddhhi", "f",   MergeNone, "aarch64_sve_fmlalb_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLALT_F      : SInst<"svmlalt[_{d}]",      "ddhh",  "f",   MergeNone, "aarch64_sve_fmlalt", [VerifyRuntimeMode]>;
def SVMLALT_F_N    : SInst<"svmlalt[_n_{d}]",    "ddhR",  "f",   MergeNone, "aarch64_sve_fmlalt", [VerifyRuntimeMode]>;
def SVMLALT_F_LANE : SInst<"svmlalt_lane[_{d}]", "ddhhi", "f",   MergeNone, "aarch64_sve_fmlalt_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLSLB_F      : SInst<"svmlslb[_{d}]",      "ddhh",  "f",   MergeNone, "aarch64_sve_fmlslb", [VerifyRuntimeMode]>;
def SVMLSLB_F_N    : SInst<"svmlslb[_n_{d}]",    "ddhR",  "f",   MergeNone, "aarch64_sve_fmlslb", [VerifyRuntimeMode]>;
def SVMLSLB_F_LANE : SInst<"svmlslb_lane[_{d}]", "ddhhi", "f",   MergeNone, "aarch64_sve_fmlslb_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLSLT_F      : SInst<"svmlslt[_{d}]",      "ddhh",  "f",   MergeNone, "aarch64_sve_fmlslt", [VerifyRuntimeMode]>;
def SVMLSLT_F_N    : SInst<"svmlslt[_n_{d}]",    "ddhR",  "f",   MergeNone, "aarch64_sve_fmlslt", [VerifyRuntimeMode]>;
def SVMLSLT_F_LANE : SInst<"svmlslt_lane[_{d}]", "ddhhi", "f",   MergeNone, "aarch64_sve_fmlslt_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Floating-point integer binary logarithm

let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVLOGB_M  : SInst<"svlogb[_{d}]", "xxPd", "hfd", MergeOp1,     "aarch64_sve_flogb", [VerifyRuntimeMode]>;
def SVLOGB_X  : SInst<"svlogb[_{d}]", "xPd",  "hfd", MergeAnyExp,  "aarch64_sve_flogb", [VerifyRuntimeMode]>;
def SVLOGB_Z  : SInst<"svlogb[_{d}]", "xPd",  "hfd", MergeZeroExp, "aarch64_sve_flogb", [VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Vector Histogram count

let SVETargetGuard = "sve2", SMETargetGuard = InvalidMode in {
def SVHISTCNT  : SInst<"svhistcnt[_{d}]_z", "uPdd", "ilUiUl", MergeNone, "aarch64_sve_histcnt">;
def SVHISTSEG  : SInst<"svhistseg[_{d}]",   "udd",  "cUc",    MergeNone, "aarch64_sve_histseg">;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Character match

let SVETargetGuard = "sve2", SMETargetGuard = InvalidMode in {
def SVMATCH  : SInst<"svmatch[_{d}]",  "PPdd", "csUcUs", MergeNone, "aarch64_sve_match">;
def SVNMATCH : SInst<"svnmatch[_{d}]", "PPdd", "csUcUs", MergeNone, "aarch64_sve_nmatch">;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Contiguous conflict detection
let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVWHILERW_B : SInst<"svwhilerw[_{1}]", "Pcc", "cUc",  MergeNone, "aarch64_sve_whilerw_b", [IsOverloadWhileRW, VerifyRuntimeMode]>;
def SVWHILERW_H : SInst<"svwhilerw[_{1}]", "Pcc", "sUsh", MergeNone, "aarch64_sve_whilerw_h", [IsOverloadWhileRW, VerifyRuntimeMode]>;
def SVWHILERW_S : SInst<"svwhilerw[_{1}]", "Pcc", "iUif", MergeNone, "aarch64_sve_whilerw_s", [IsOverloadWhileRW, VerifyRuntimeMode]>;
def SVWHILERW_D : SInst<"svwhilerw[_{1}]", "Pcc", "lUld", MergeNone, "aarch64_sve_whilerw_d", [IsOverloadWhileRW, VerifyRuntimeMode]>;

def SVWHILEWR_B : SInst<"svwhilewr[_{1}]", "Pcc", "cUc",  MergeNone, "aarch64_sve_whilewr_b", [IsOverloadWhileRW, VerifyRuntimeMode]>;
def SVWHILEWR_H : SInst<"svwhilewr[_{1}]", "Pcc", "sUsh", MergeNone, "aarch64_sve_whilewr_h", [IsOverloadWhileRW, VerifyRuntimeMode]>;
def SVWHILEWR_S : SInst<"svwhilewr[_{1}]", "Pcc", "iUif", MergeNone, "aarch64_sve_whilewr_s", [IsOverloadWhileRW, VerifyRuntimeMode]>;
def SVWHILEWR_D : SInst<"svwhilewr[_{1}]", "Pcc", "lUld", MergeNone, "aarch64_sve_whilewr_d", [IsOverloadWhileRW, VerifyRuntimeMode]>;
}

let SVETargetGuard = "sve2,bf16", SMETargetGuard = "sme,bf16" in {
def SVWHILERW_H_BF16 : SInst<"svwhilerw[_{1}]", "Pcc", "b", MergeNone, "aarch64_sve_whilerw_h", [IsOverloadWhileRW, VerifyRuntimeMode]>;
def SVWHILEWR_H_BF16 : SInst<"svwhilewr[_{1}]", "Pcc", "b", MergeNone, "aarch64_sve_whilewr_h", [IsOverloadWhileRW, VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Extended table lookup/permute
let SVETargetGuard = "sve2", SMETargetGuard = "sme" in {
def SVTBL2 : SInst<"svtbl2[_{d}]", "d2u",  "csilUcUsUiUlhfd", MergeNone, "", [VerifyRuntimeMode]>;
def SVTBX  : SInst<"svtbx[_{d}]",  "dddu", "csilUcUsUiUlhfd", MergeNone, "aarch64_sve_tbx", [VerifyRuntimeMode]>;
}

let SVETargetGuard = "sve2,bf16", SMETargetGuard = "sme,bf16" in {
def SVTBL2_BF16 : SInst<"svtbl2[_{d}]", "d2u",  "b", MergeNone, "", [VerifyRuntimeMode]>;
def SVTBX_BF16  : SInst<"svtbx[_{d}]",  "dddu", "b", MergeNone, "aarch64_sve_tbx", [VerifyRuntimeMode]>;
}

////////////////////////////////////////////////////////////////////////////////
// SVE2 - Optional

let SVETargetGuard = "sve2-aes", SMETargetGuard = InvalidMode in {
def SVAESD   : SInst<"svaesd[_{d}]",   "ddd", "Uc", MergeNone, "aarch64_sve_aesd", [IsOverloadNone]>;
def SVAESIMC : SInst<"svaesimc[_{d}]", "dd",  "Uc", MergeNone, "aarch64_sve_aesimc", [IsOverloadNone]>;
def SVAESE   : SInst<"svaese[_{d}]",   "ddd", "Uc", MergeNone, "aarch64_sve_aese", [IsOverloadNone]>;
def SVAESMC  : SInst<"svaesmc[_{d}]",  "dd",  "Uc", MergeNone, "aarch64_sve_aesmc", [IsOverloadNone]>;

def SVPMULLB_PAIR_U64   : SInst<"svpmullb_pair[_{d}]",   "ddd", "Ul", MergeNone, "aarch64_sve_pmullb_pair">;
def SVPMULLB_PAIR_N_U64 : SInst<"svpmullb_pair[_n_{d}]", "dda", "Ul", MergeNone, "aarch64_sve_pmullb_pair">;

def SVPMULLT_PAIR_U64   : SInst<"svpmullt_pair[_{d}]",   "ddd", "Ul", MergeNone, "aarch64_sve_pmullt_pair">;
def SVPMULLT_PAIR_N_U64 : SInst<"svpmullt_pair[_n_{d}]", "dda", "Ul", MergeNone, "aarch64_sve_pmullt_pair">;
}

let SVETargetGuard = "sve2-sha3", SMETargetGuard = InvalidMode in { // FIXME: valid from sme2p1.
def SVRAX1   : SInst<"svrax1[_{d}]",   "ddd", "lUl", MergeNone, "aarch64_sve_rax1", [IsOverloadNone]>;
}

let SVETargetGuard = "sve2-sm4", SMETargetGuard = InvalidMode in {
def SVSM4E    : SInst<"svsm4e[_{d}]",    "ddd", "Ui", MergeNone, "aarch64_sve_sm4e", [IsOverloadNone]>;
def SVSM4EKEY : SInst<"svsm4ekey[_{d}]", "ddd", "Ui", MergeNone, "aarch64_sve_sm4ekey", [IsOverloadNone]>;
}

let SVETargetGuard = "sve2-bitperm", SMETargetGuard = InvalidMode in {
def SVBDEP   : SInst<"svbdep[_{d}]",   "ddd", "UcUsUiUl", MergeNone, "aarch64_sve_bdep_x">;
def SVBDEP_N : SInst<"svbdep[_n_{d}]", "dda", "UcUsUiUl", MergeNone, "aarch64_sve_bdep_x">;
def SVBEXT   : SInst<"svbext[_{d}]",   "ddd", "UcUsUiUl", MergeNone, "aarch64_sve_bext_x">;
def SVBEXT_N : SInst<"svbext[_n_{d}]", "dda", "UcUsUiUl", MergeNone, "aarch64_sve_bext_x">;
def SVBGRP   : SInst<"svbgrp[_{d}]",   "ddd", "UcUsUiUl", MergeNone, "aarch64_sve_bgrp_x">;
def SVBGRP_N : SInst<"svbgrp[_n_{d}]", "dda", "UcUsUiUl", MergeNone, "aarch64_sve_bgrp_x">;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme" in {
def SVPSEL_B : SInst<"svpsel_lane_b8",  "PPPm", "Pc", MergeNone, "", [VerifyRuntimeMode], []>;
def SVPSEL_H : SInst<"svpsel_lane_b16", "PPPm", "Ps", MergeNone, "", [VerifyRuntimeMode], []>;
def SVPSEL_S : SInst<"svpsel_lane_b32", "PPPm", "Pi", MergeNone, "", [VerifyRuntimeMode], []>;
def SVPSEL_D : SInst<"svpsel_lane_b64", "PPPm", "Pl", MergeNone, "", [VerifyRuntimeMode], []>;
}

// Standalone sve2.1 builtins
let SVETargetGuard = "sve2p1", SMETargetGuard = InvalidMode in {
def SVORQV   : SInst<"svorqv[_{d}]",  "{Pd", "csilUcUsUiUl", MergeNone, "aarch64_sve_orqv",   [IsReductionQV]>;
def SVEORQV  : SInst<"sveorqv[_{d}]", "{Pd", "csilUcUsUiUl", MergeNone, "aarch64_sve_eorqv",  [IsReductionQV]>;
def SVADDQV  : SInst<"svaddqv[_{d}]", "{Pd", "csilUcUsUiUl", MergeNone, "aarch64_sve_addqv",  [IsReductionQV]>;
def SVANDQV  : SInst<"svandqv[_{d}]", "{Pd", "csilUcUsUiUl", MergeNone, "aarch64_sve_andqv",  [IsReductionQV]>;
def SVSMAXQV : SInst<"svmaxqv[_{d}]", "{Pd", "csil",         MergeNone, "aarch64_sve_smaxqv", [IsReductionQV]>;
def SVUMAXQV : SInst<"svmaxqv[_{d}]", "{Pd", "UcUsUiUl",     MergeNone, "aarch64_sve_umaxqv", [IsReductionQV]>;
def SVSMINQV : SInst<"svminqv[_{d}]", "{Pd", "csil",         MergeNone, "aarch64_sve_sminqv", [IsReductionQV]>;
def SVUMINQV : SInst<"svminqv[_{d}]", "{Pd", "UcUsUiUl",     MergeNone, "aarch64_sve_uminqv", [IsReductionQV]>;

def SVFADDQV   : SInst<"svaddqv[_{d}]",   "{Pd", "hfd", MergeNone, "aarch64_sve_faddqv",   [IsReductionQV]>;
def SVFMAXNMQV : SInst<"svmaxnmqv[_{d}]", "{Pd", "hfd", MergeNone, "aarch64_sve_fmaxnmqv", [IsReductionQV]>;
def SVFMINNMQV : SInst<"svminnmqv[_{d}]", "{Pd", "hfd", MergeNone, "aarch64_sve_fminnmqv", [IsReductionQV]>;
def SVFMAXQV   : SInst<"svmaxqv[_{d}]",   "{Pd", "hfd", MergeNone, "aarch64_sve_fmaxqv",   [IsReductionQV]>;
def SVFMINQV   : SInst<"svminqv[_{d}]",   "{Pd", "hfd", MergeNone, "aarch64_sve_fminqv",   [IsReductionQV]>;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
def SVPEXT_SINGLE : SInst<"svpext_lane_{d}", "P}i", "QcQsQiQl", MergeNone, "aarch64_sve_pext", [VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_3>]>;
def SVPEXT_X2     : SInst<"svpext_lane_{d}_x2", "2.P}i", "QcQsQiQl", MergeNone, "aarch64_sve_pext_x2", [VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_1>]>;

def SVPSEL_COUNT_ALIAS_B : SInst<"svpsel_lane_c8",  "}}Pm", "Pc", MergeNone, "", [VerifyRuntimeMode], []>;
def SVPSEL_COUNT_ALIAS_H : SInst<"svpsel_lane_c16", "}}Pm", "Ps", MergeNone, "", [VerifyRuntimeMode], []>;
def SVPSEL_COUNT_ALIAS_S : SInst<"svpsel_lane_c32", "}}Pm", "Pi", MergeNone, "", [VerifyRuntimeMode], []>;
def SVPSEL_COUNT_ALIAS_D : SInst<"svpsel_lane_c64", "}}Pm", "Pl", MergeNone, "", [VerifyRuntimeMode], []>;

def SVWHILEGE_COUNT  : SInst<"svwhilege_{d}[_{1}]",  "}lli", "QcQsQiQl", MergeNone, "aarch64_sve_whilege_{d}", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck2_4_Mul2>]>;
def SVWHILEGT_COUNT  : SInst<"svwhilegt_{d}[_{1}]",  "}lli", "QcQsQiQl", MergeNone, "aarch64_sve_whilegt_{d}", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck2_4_Mul2>]>;
def SVWHILELE_COUNT  : SInst<"svwhilele_{d}[_{1}]",  "}lli", "QcQsQiQl", MergeNone, "aarch64_sve_whilele_{d}", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck2_4_Mul2>]>;
def SVWHILELT_COUNT  : SInst<"svwhilelt_{d}[_{1}]",  "}lli", "QcQsQiQl", MergeNone, "aarch64_sve_whilelt_{d}", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck2_4_Mul2>]>;
def SVWHILELO_COUNT  : SInst<"svwhilelt_{d}[_{1}]",  "}nni", "QcQsQiQl", MergeNone, "aarch64_sve_whilelo_{d}", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck2_4_Mul2>]>;
def SVWHILELS_COUNT  : SInst<"svwhilele_{d}[_{1}]",  "}nni", "QcQsQiQl", MergeNone, "aarch64_sve_whilels_{d}", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck2_4_Mul2>]>;
def SVWHILEHI_COUNT  : SInst<"svwhilegt_{d}[_{1}]",  "}nni", "QcQsQiQl", MergeNone, "aarch64_sve_whilehi_{d}", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck2_4_Mul2>]>;
def SVWHILEHS_COUNT  : SInst<"svwhilege_{d}[_{1}]",  "}nni", "QcQsQiQl", MergeNone, "aarch64_sve_whilehs_{d}", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<2, ImmCheck2_4_Mul2>]>;
}

multiclass MultiVecLoad<string i> {
  def SV # NAME # B_X2 : MInst<"sv" # i # "[_{2}]_x2", "2}c", "cUc",   [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # H_X2 : MInst<"sv" # i # "[_{2}]_x2", "2}c", "sUshb", [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # W_X2 : MInst<"sv" # i # "[_{2}]_x2", "2}c", "iUif",  [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # D_X2 : MInst<"sv" # i # "[_{2}]_x2", "2}c", "lUld",  [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # B_X4 : MInst<"sv" # i # "[_{2}]_x4", "4}c", "cUc",   [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # H_X4 : MInst<"sv" # i # "[_{2}]_x4", "4}c", "sUshb", [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # W_X4 : MInst<"sv" # i # "[_{2}]_x4", "4}c", "iUif",  [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # D_X4 : MInst<"sv" # i # "[_{2}]_x4", "4}c", "lUld",  [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;

  def SV # NAME # B_VNUM_X2 : MInst<"sv" # i # "_vnum" # "[_{2}]_x2", "2}cl", "cUc",   [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # H_VNUM_X2 : MInst<"sv" # i # "_vnum" # "[_{2}]_x2", "2}cl", "sUshb", [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # W_VNUM_X2 : MInst<"sv" # i # "_vnum" # "[_{2}]_x2", "2}cl", "iUif",  [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # D_VNUM_X2 : MInst<"sv" # i # "_vnum" # "[_{2}]_x2", "2}cl", "lUld",  [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # B_VNUM_X4 : MInst<"sv" # i # "_vnum" # "[_{2}]_x4", "4}cl", "cUc",   [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # H_VNUM_X4 : MInst<"sv" # i # "_vnum" # "[_{2}]_x4", "4}cl", "sUshb", [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # W_VNUM_X4 : MInst<"sv" # i # "_vnum" # "[_{2}]_x4", "4}cl", "iUif",  [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # D_VNUM_X4 : MInst<"sv" # i # "_vnum" # "[_{2}]_x4", "4}cl", "lUld",  [IsStructLoad, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
  defm LD1   : MultiVecLoad<"ld1">;
  defm LDNT1 : MultiVecLoad<"ldnt1">;
}

multiclass MultiVecStore<string i> {
  def SV # NAME # B_X2 : MInst<"sv" # i # "[_{2}_x2]", "v}p2", "cUc",   [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # H_X2 : MInst<"sv" # i # "[_{2}_x2]", "v}p2", "sUshb", [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # W_X2 : MInst<"sv" # i # "[_{2}_x2]", "v}p2", "iUif",  [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # D_X2 : MInst<"sv" # i # "[_{2}_x2]", "v}p2", "lUld",  [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # B_X4 : MInst<"sv" # i # "[_{2}_x4]", "v}p4", "cUc",   [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # H_X4 : MInst<"sv" # i # "[_{2}_x4]", "v}p4", "sUshb", [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # W_X4 : MInst<"sv" # i # "[_{2}_x4]", "v}p4", "iUif",  [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # D_X4 : MInst<"sv" # i # "[_{2}_x4]", "v}p4", "lUld",  [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;

  def SV # NAME # B_VNUM_X2 : MInst<"sv" # i # "_vnum" # "[_{2}_x2]", "v}pl2", "cUc",   [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # H_VNUM_X2 : MInst<"sv" # i # "_vnum" # "[_{2}_x2]", "v}pl2", "sUshb", [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # W_VNUM_X2 : MInst<"sv" # i # "_vnum" # "[_{2}_x2]", "v}pl2", "iUif",  [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # D_VNUM_X2 : MInst<"sv" # i # "_vnum" # "[_{2}_x2]", "v}pl2", "lUld",  [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x2">;
  def SV # NAME # B_VNUM_X4 : MInst<"sv" # i # "_vnum" # "[_{2}_x4]", "v}pl4", "cUc",   [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # H_VNUM_X4 : MInst<"sv" # i # "_vnum" # "[_{2}_x4]", "v}pl4", "sUshb", [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # W_VNUM_X4 : MInst<"sv" # i # "_vnum" # "[_{2}_x4]", "v}pl4", "iUif",  [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
  def SV # NAME # D_VNUM_X4 : MInst<"sv" # i # "_vnum" # "[_{2}_x4]", "v}pl4", "lUld",  [IsStructStore, VerifyRuntimeMode], MemEltTyDefault, "aarch64_sve_" # i # "_pn_x4">;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
  defm ST1   : MultiVecStore<"st1">;
  defm STNT1 : MultiVecStore<"stnt1">;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
def SVDOT_X2_S : SInst<"svdot[_{d}_{2}]", "ddhh", "i",  MergeNone, "aarch64_sve_sdot_x2", [VerifyRuntimeMode], []>;
def SVDOT_X2_U : SInst<"svdot[_{d}_{2}]", "ddhh", "Ui", MergeNone, "aarch64_sve_udot_x2", [VerifyRuntimeMode], []>;
def SVDOT_X2_F : SInst<"svdot[_{d}_{2}]", "ddhh", "f",  MergeNone, "aarch64_sve_fdot_x2", [VerifyRuntimeMode], []>;
def SVDOT_LANE_X2_S : SInst<"svdot_lane[_{d}_{2}]", "ddhhi", "i",  MergeNone, "aarch64_sve_sdot_lane_x2", [VerifyRuntimeMode], [ImmCheck<3, ImmCheck0_3>]>;
def SVDOT_LANE_X2_U : SInst<"svdot_lane[_{d}_{2}]", "ddhhi", "Ui", MergeNone, "aarch64_sve_udot_lane_x2", [VerifyRuntimeMode], [ImmCheck<3, ImmCheck0_3>]>;
def SVDOT_LANE_X2_F : SInst<"svdot_lane[_{d}_{2}]", "ddhhi", "f",  MergeNone, "aarch64_sve_fdot_lane_x2", [VerifyRuntimeMode], [ImmCheck<3, ImmCheck0_3>]>;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme" in {
def SVSCLAMP : SInst<"svclamp[_{d}]", "dddd", "csil",     MergeNone, "aarch64_sve_sclamp", [VerifyRuntimeMode], []>;
def SVUCLAMP : SInst<"svclamp[_{d}]", "dddd", "UcUsUiUl", MergeNone, "aarch64_sve_uclamp", [VerifyRuntimeMode], []>;

defm SVREVD : SInstZPZ<"svrevd", "csilUcUsUiUlbhfd", "aarch64_sve_revd">;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
  def SVPTRUE_COUNT  : SInst<"svptrue_{d}", "}v", "QcQsQiQl", MergeNone, "aarch64_sve_ptrue_{d}", [IsOverloadNone, VerifyRuntimeMode], []>;

  def SVPFALSE_COUNT_ALIAS : SInst<"svpfalse_c", "}v", "", MergeNone, "", [IsOverloadNone, VerifyRuntimeMode]>;

  def SVFCLAMP   : SInst<"svclamp[_{d}]", "dddd", "hfd", MergeNone, "aarch64_sve_fclamp", [VerifyRuntimeMode], []>;
  def SVCNTP_COUNT : SInst<"svcntp_{d}", "n}i", "QcQsQiQl", MergeNone, "aarch64_sve_cntp_{d}", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<1, ImmCheck2_4_Mul2>]>;
}

let SVETargetGuard = "sve2,b16b16", SMETargetGuard = "sme2,b16b16" in {
defm SVMUL_BF  : SInstZPZZ<"svmul",  "b", "aarch64_sve_fmul",   "aarch64_sve_fmul_u", [VerifyRuntimeMode]>;
defm SVADD_BF  : SInstZPZZ<"svadd",  "b", "aarch64_sve_fadd",   "aarch64_sve_fadd_u", [VerifyRuntimeMode]>;
defm SVSUB_BF  : SInstZPZZ<"svsub",  "b", "aarch64_sve_fsub",   "aarch64_sve_fsub_u", [VerifyRuntimeMode]>;
defm SVMAXNM_BF  : SInstZPZZ<"svmaxnm","b", "aarch64_sve_fmaxnm", "aarch64_sve_fmaxnm_u", [VerifyRuntimeMode]>;
defm SVMINNM_BF  : SInstZPZZ<"svminnm","b", "aarch64_sve_fminnm", "aarch64_sve_fminnm_u", [VerifyRuntimeMode]>;
defm SVMAX_BF    : SInstZPZZ<"svmax",  "b", "aarch64_sve_fmax",   "aarch64_sve_fmax_u", [VerifyRuntimeMode]>;
defm SVMIN_BF    : SInstZPZZ<"svmin",  "b", "aarch64_sve_fmin",   "aarch64_sve_fmin_u", [VerifyRuntimeMode]>;
defm SVMLA_BF  : SInstZPZZZ<"svmla",  "b", "aarch64_sve_fmla",  "aarch64_sve_fmla_u", [VerifyRuntimeMode]>;
defm SVMLS_BF  : SInstZPZZZ<"svmls",  "b", "aarch64_sve_fmls",  "aarch64_sve_fmls_u", [VerifyRuntimeMode]>;
def SVMLA_LANE_BF  : SInst<"svmla_lane[_{d}]",  "ddddi",  "b", MergeNone, "aarch64_sve_fmla_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMLS_LANE_BF  : SInst<"svmls_lane[_{d}]",  "ddddi",  "b", MergeNone, "aarch64_sve_fmls_lane", [VerifyRuntimeMode], [ImmCheck<3, ImmCheckLaneIndex, 2>]>;
def SVMUL_LANE_BF  : SInst<"svmul_lane[_{d}]", "dddi", "b", MergeNone, "aarch64_sve_fmul_lane", [VerifyRuntimeMode], [ImmCheck<2, ImmCheckLaneIndex, 1>]>;
def SVFCLAMP_BF   : SInst<"svclamp[_{d}]", "dddd", "b", MergeNone, "aarch64_sve_fclamp", [VerifyRuntimeMode], []>;
}

// SME2

// SME intrinsics which operate only on vectors and do not require ZA should be added here,
// as they could possibly become SVE instructions in the future.

multiclass MinMaxIntr<string i, string zm, string mul, string t> {
  def SVS # NAME : SInst<"sv" # i # "[" # zm # "_{d}_" # mul # "]", t, "csil",     MergeNone, "aarch64_sve_s" # i # zm # "_" # mul, [IsStreaming], []>;
  def SVU # NAME : SInst<"sv" # i # "[" # zm # "_{d}_" # mul # "]", t, "UcUsUiUl", MergeNone, "aarch64_sve_u" # i # zm # "_" # mul, [IsStreaming], []>;
  def SVF # NAME : SInst<"sv" # i # "[" # zm # "_{d}_" # mul # "]", t, "hfd",      MergeNone, "aarch64_sve_f" # i # zm # "_" # mul, [IsStreaming], []>;
}

let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
// == SMAX / UMAX / FMAX ==
  defm MAX_SINGLE_X2 : MinMaxIntr<"max", "_single", "x2", "22d">;
  defm MAX_MULTI_X2  : MinMaxIntr<"max", "",        "x2", "222">;
  defm MAX_SINGLE_X4 : MinMaxIntr<"max", "_single", "x4", "44d">;
  defm MAX_MULTI_X4  : MinMaxIntr<"max", "",        "x4", "444">;

// == SMIN / UMIN / FMIN ==
  defm MIN_SINGLE_X2 : MinMaxIntr<"min", "_single", "x2", "22d">;
  defm MIN_MULTI_X2  : MinMaxIntr<"min", "",        "x2", "222">;
  defm MIN_SINGLE_X4 : MinMaxIntr<"min", "_single", "x4", "44d">;
  defm MIN_MULTI_X4  : MinMaxIntr<"min", "",        "x4", "444">;
}

multiclass SInstMinMaxByVector<string name> {
  def NAME # _SINGLE_X2 : SInst<"sv" # name # "nm[_single_{d}_x2]", "22d", "hfd", MergeNone, "aarch64_sve_f" # name # "nm_single_x2", [IsStreaming], []>;
  def NAME # _SINGLE_X4 : SInst<"sv" # name # "nm[_single_{d}_x4]", "44d", "hfd", MergeNone, "aarch64_sve_f" # name # "nm_single_x4", [IsStreaming], []>;

  def NAME # _X2 : SInst<"sv" # name # "nm[_{d}_x2]", "222", "hfd", MergeNone, "aarch64_sve_f" # name # "nm_x2", [IsStreaming], []>;
  def NAME # _X4 : SInst<"sv" # name # "nm[_{d}_x4]", "444", "hfd", MergeNone, "aarch64_sve_f" # name # "nm_x4", [IsStreaming], []>;
}

let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
// == FMINNM / FMAXNM ==
  defm SVMINNM : SInstMinMaxByVector<"min">;
  defm SVMAXNM : SInstMinMaxByVector<"max">;
}

let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
  // FRINTA / FRINTM / FRINTN / FRINTP
  def SVRINTA_X2 : SInst<"svrinta[_{d}_x2]", "22", "f", MergeNone, "aarch64_sve_frinta_x2", [IsStreaming], []>;
  def SVRINTA_X4 : SInst<"svrinta[_{d}_x4]", "44", "f", MergeNone, "aarch64_sve_frinta_x4", [IsStreaming], []>;

  def SVRINTM_X2 : SInst<"svrintm[_{d}_x2]", "22", "f", MergeNone, "aarch64_sve_frintm_x2", [IsStreaming], []>;
  def SVRINTM_X4 : SInst<"svrintm[_{d}_x4]", "44", "f", MergeNone, "aarch64_sve_frintm_x4", [IsStreaming], []>;

  def SVRINTN_X2 : SInst<"svrintn[_{d}_x2]", "22", "f", MergeNone, "aarch64_sve_frintn_x2", [IsStreaming], []>;
  def SVRINTN_X4 : SInst<"svrintn[_{d}_x4]", "44", "f", MergeNone, "aarch64_sve_frintn_x4", [IsStreaming], []>;

  def SVRINTP_X2 : SInst<"svrintp[_{d}_x2]", "22", "f", MergeNone, "aarch64_sve_frintp_x2", [IsStreaming], []>;
  def SVRINTP_X4 : SInst<"svrintp[_{d}_x4]", "44", "f", MergeNone, "aarch64_sve_frintp_x4", [IsStreaming], []>;
}

let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
  def SVSCLAMP_X2 : SInst<"svclamp[_single_{d}_x2]",  "22dd",   "csil",     MergeNone, "aarch64_sve_sclamp_single_x2",  [IsStreaming], []>;
  def SVUCLAMP_X2 : SInst<"svclamp[_single_{d}_x2]",  "22dd",   "UcUsUiUl", MergeNone, "aarch64_sve_uclamp_single_x2",  [IsStreaming], []>;
  def SVFCLAMP_X2 : SInst<"svclamp[_single_{d}_x2]",  "22dd",   "hfd",      MergeNone, "aarch64_sve_fclamp_single_x2",  [IsStreaming], []>;

  def SVSCLAMP_X4 : SInst<"svclamp[_single_{d}_x4]",  "44dd",   "csil",     MergeNone, "aarch64_sve_sclamp_single_x4",  [IsStreaming], []>;
  def SVUCLAMP_X4 : SInst<"svclamp[_single_{d}_x4]",  "44dd",   "UcUsUiUl", MergeNone, "aarch64_sve_uclamp_single_x4",  [IsStreaming], []>;
  def SVFCLAMP_X4 : SInst<"svclamp[_single_{d}_x4]",  "44dd",   "hfd",      MergeNone, "aarch64_sve_fclamp_single_x4",  [IsStreaming], []>;
}

multiclass BfSingleMultiVector<string name> {
  def NAME # _SINGLE_X2 : SInst<"sv" # name # "[_single_{d}_x2]", "22d", "b", MergeNone, "aarch64_sve_f" # name # "_single_x2", [IsStreaming], []>;
  def NAME # _SINGLE_X4 : SInst<"sv" # name # "[_single_{d}_x4]", "44d", "b", MergeNone, "aarch64_sve_f" # name # "_single_x4", [IsStreaming], []>;

  def NAME # _X2 : SInst<"sv" # name # "[_{d}_x2]", "222", "b", MergeNone, "aarch64_sve_f" # name # "_x2", [IsStreaming], []>;
  def NAME # _X4 : SInst<"sv" # name # "[_{d}_x4]", "444", "b", MergeNone, "aarch64_sve_f" # name # "_x4", [IsStreaming], []>;
}

let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2,b16b16"in {
  def SVBFCLAMP_X2 : SInst<"svclamp[_single_{d}_x2]",  "22dd",   "b",      MergeNone, "aarch64_sve_bfclamp_single_x2",  [IsStreaming], []>;
  def SVBFCLAMP_X4 : SInst<"svclamp[_single_{d}_x4]",  "44dd",   "b",      MergeNone, "aarch64_sve_bfclamp_single_x4",  [IsStreaming], []>;

  // bfmin, bfmax (single, multi)
  defm SVBFMIN : BfSingleMultiVector<"min">;
  defm SVBFMAX : BfSingleMultiVector<"max">;

  // bfminnm, bfmaxnm (single, multi)
  defm SVBFMINNM : BfSingleMultiVector<"minnm">;
  defm SVBFMAXNM : BfSingleMultiVector<"maxnm">;
}

let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
// == ADD (vectors) ==
  def SVADD_SINGLE_X2 : SInst<"svadd[_single_{d}_x2]", "22d", "cUcsUsiUilUl", MergeNone, "aarch64_sve_add_single_x2", [IsStreaming], []>;
  def SVADD_SINGLE_X4 : SInst<"svadd[_single_{d}_x4]", "44d", "cUcsUsiUilUl", MergeNone, "aarch64_sve_add_single_x4", [IsStreaming], []>;

  // 2-way and 4-way selects
  def SVSEL_X2  : SInst<"svsel[_{d}_x2]", "2}22", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_sel_x2", [IsStreaming], []>;
  def SVSEL_X4  : SInst<"svsel[_{d}_x4]", "4}44", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_sel_x4", [IsStreaming], []>;

  // SRSHL / URSHL
  def SVSRSHL_SINGLE_X2 : SInst<"svrshl[_single_{d}_x2]", "22d", "csil",     MergeNone, "aarch64_sve_srshl_single_x2", [IsStreaming], []>;
  def SVURSHL_SINGLE_X2 : SInst<"svrshl[_single_{d}_x2]", "22d", "UcUsUiUl", MergeNone, "aarch64_sve_urshl_single_x2", [IsStreaming], []>;
  def SVSRSHL_SINGLE_X4 : SInst<"svrshl[_single_{d}_x4]", "44d", "csil",     MergeNone, "aarch64_sve_srshl_single_x4", [IsStreaming], []>;
  def SVURSHL_SINGLE_X4 : SInst<"svrshl[_single_{d}_x4]", "44d", "UcUsUiUl", MergeNone, "aarch64_sve_urshl_single_x4", [IsStreaming], []>;

  def SVSRSHL_X2 : SInst<"svrshl[_{d}_x2]", "222", "csil",     MergeNone, "aarch64_sve_srshl_x2", [IsStreaming], []>;
  def SVURSHL_X2 : SInst<"svrshl[_{d}_x2]", "222", "UcUsUiUl", MergeNone, "aarch64_sve_urshl_x2", [IsStreaming], []>;
  def SVSRSHL_X4 : SInst<"svrshl[_{d}_x4]", "444", "csil",     MergeNone, "aarch64_sve_srshl_x4", [IsStreaming], []>;
  def SVURSHL_X4 : SInst<"svrshl[_{d}_x4]", "444", "UcUsUiUl", MergeNone, "aarch64_sve_urshl_x4", [IsStreaming], []>;

  def SVQRSHRN_X4   : SInst<"svqrshrn[_n]_{0}[_{d}_x4]", "q4i", "il",   MergeNone, "aarch64_sve_sqrshrn_x4", [IsStreaming], [ImmCheck<1, ImmCheckShiftRight, 0>]>;
  def SVUQRSHRN_X4  : SInst<"svqrshrn[_n]_{0}[_{d}_x4]", "b4i", "UiUl", MergeNone, "aarch64_sve_uqrshrn_x4", [IsStreaming], [ImmCheck<1, ImmCheckShiftRight, 0>]>;

  // SQRSHR / UQRSHR
  def SVQRSHR_X2  : SInst<"svqrshr[_n]_{0}[_{d}_x2]", "h2i", "i",    MergeNone, "aarch64_sve_sqrshr_x2", [IsStreaming], [ImmCheck<1, ImmCheck1_16>]>;
  def SVUQRSHR_X2 : SInst<"svqrshr[_n]_{0}[_{d}_x2]", "e2i", "Ui",   MergeNone, "aarch64_sve_uqrshr_x2", [IsStreaming], [ImmCheck<1, ImmCheck1_16>]>;
  def SVQRSHR_X4  : SInst<"svqrshr[_n]_{0}[_{d}_x4]", "q4i", "il",   MergeNone, "aarch64_sve_sqrshr_x4", [IsStreaming], [ImmCheck<1, ImmCheckShiftRight, 0>]>;
  def SVUQRSHR_X4 : SInst<"svqrshr[_n]_{0}[_{d}_x4]", "b4i", "UiUl", MergeNone, "aarch64_sve_uqrshr_x4", [IsStreaming], [ImmCheck<1, ImmCheckShiftRight, 0>]>;

  // SQRSHRU
  def SVSQRSHRU_X2 : SInst<"svqrshru[_n]_{0}[_{d}_x2]", "e2i", "i",  MergeNone, "aarch64_sve_sqrshru_x2", [IsStreaming], [ImmCheck<1, ImmCheck1_16>]>;
  def SVSQRSHRU_X4 : SInst<"svqrshru[_n]_{0}[_{d}_x4]", "b4i", "il", MergeNone, "aarch64_sve_sqrshru_x4", [IsStreaming], [ImmCheck<1, ImmCheckShiftRight, 0>]>;

  def SVSQRSHRUN_X4 : SInst<"svqrshrun[_n]_{0}[_{d}_x4]", "b4i", "il", MergeNone, "aarch64_sve_sqrshrun_x4", [IsStreaming], [ImmCheck<1, ImmCheckShiftRight, 0>]>;

  // SQDMULH
  def SVSQDMULH_SINGLE_X2 : SInst<"svqdmulh[_single_{d}_x2]", "22d", "csil", MergeNone, "aarch64_sve_sqdmulh_single_vgx2", [IsStreaming], []>;
  def SVSQDMULH_SINGLE_X4 : SInst<"svqdmulh[_single_{d}_x4]", "44d", "csil", MergeNone, "aarch64_sve_sqdmulh_single_vgx4", [IsStreaming], []>;
  def SVSQDMULH_X2        : SInst<"svqdmulh[_{d}_x2]",        "222", "csil", MergeNone, "aarch64_sve_sqdmulh_vgx2",        [IsStreaming], []>;
  def SVSQDMULH_X4        : SInst<"svqdmulh[_{d}_x4]",        "444", "csil", MergeNone, "aarch64_sve_sqdmulh_vgx4",        [IsStreaming], []>;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
  def REINTERPRET_SVBOOL_TO_SVCOUNT : Inst<"svreinterpret[_c]", "}P", "Pc", MergeNone, "", [VerifyRuntimeMode], []>;
  def REINTERPRET_SVCOUNT_TO_SVBOOL : Inst<"svreinterpret[_b]", "P}", "Pc", MergeNone, "", [VerifyRuntimeMode], []>;

  // SQRSHRN / UQRSHRN
  def SVQRSHRN_X2   : SInst<"svqrshrn[_n]_{0}[_{d}_x2]", "h2i", "i",    MergeNone, "aarch64_sve_sqrshrn_x2", [VerifyRuntimeMode], [ImmCheck<1, ImmCheck1_16>]>;
  def SVUQRSHRN_X2  : SInst<"svqrshrn[_n]_{0}[_{d}_x2]", "e2i", "Ui",   MergeNone, "aarch64_sve_uqrshrn_x2", [VerifyRuntimeMode], [ImmCheck<1, ImmCheck1_16>]>;

  // SQRSHRUN
  def SVSQRSHRUN_X2 : SInst<"svqrshrun[_n]_{0}[_{d}_x2]", "e2i", "i",  MergeNone, "aarch64_sve_sqrshrun_x2", [VerifyRuntimeMode], [ImmCheck<1, ImmCheck1_16>]>;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = InvalidMode in {
  // ZIPQ1, ZIPQ2, UZPQ1, UZPQ2
  def SVZIPQ1 : SInst<"svzipq1[_{d}]", "ddd", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_zipq1", [], []>;
  def SVZIPQ2 : SInst<"svzipq2[_{d}]", "ddd", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_zipq2", [], []>;
  def SVUZPQ1 : SInst<"svuzpq1[_{d}]", "ddd", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_uzpq1", [], []>;
  def SVUZPQ2 : SInst<"svuzpq2[_{d}]", "ddd", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_uzpq2", [], []>;
  // TBLQ, TBXQ
  def SVTBLQ : SInst<"svtblq[_{d}]", "ddu", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_tblq">;
  def SVTBXQ : SInst<"svtbxq[_{d}]", "dddu", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_tbxq">;
  // EXTQ
  def EXTQ : SInst<"svextq[_{d}]", "dddk", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_extq", [], [ImmCheck<2, ImmCheck0_15>]>;

  // PMOV
  // Move to Pred
  multiclass PMOV_TO_PRED<string name, string types, string intrinsic, list<FlagType> flags=[], ImmCheckType immCh > {
    def _LANE      : Inst<name # "_lane[_{d}]", "Pdi", types, MergeNone, intrinsic, flags, [ImmCheck<1, immCh>]>;
    def _LANE_ZERO : SInst<name # "[_{d}]", "Pd", types, MergeNone, intrinsic # "_zero", flags, []>;
  }
  defm SVPMOV_B_TO_PRED : PMOV_TO_PRED<"svpmov", "cUc", "aarch64_sve_pmov_to_pred_lane", [], ImmCheck0_0>;
  defm SVPMOV_H_TO_PRED : PMOV_TO_PRED<"svpmov", "sUs", "aarch64_sve_pmov_to_pred_lane", [], ImmCheck0_1>;
  defm SVPMOV_S_TO_PRED : PMOV_TO_PRED<"svpmov", "iUi", "aarch64_sve_pmov_to_pred_lane", [], ImmCheck0_3>;
  defm SVPMOV_D_TO_PRED : PMOV_TO_PRED<"svpmov", "lUl", "aarch64_sve_pmov_to_pred_lane", [], ImmCheck0_7>;

  // Move to Vector
  multiclass PMOV_TO_VEC<string name, string types, string intrinsic, list<FlagType> flags=[], ImmCheckType immCh > {
    def _M : SInst<name # "_lane[_{d}]", "ddPi", types, MergeOp1, intrinsic # "_merging", flags, [ImmCheck<2, immCh>]>;
    def _Z : SInst<name # "_{d}_z", "dP",  types, MergeNone, intrinsic # "_zeroing", flags, []>;
  }
  def SVPMOV_TO_VEC_LANE_B : SInst<"svpmov_{d}_z", "dP",  "cUc", MergeNone, "aarch64_sve_pmov_to_vector_lane_zeroing", [], []>;
  defm SVPMOV_TO_VEC_LANE_H : PMOV_TO_VEC<"svpmov", "sUs", "aarch64_sve_pmov_to_vector_lane", [], ImmCheck1_1>;
  defm SVPMOV_TO_VEC_LANE_S : PMOV_TO_VEC<"svpmov", "iUi", "aarch64_sve_pmov_to_vector_lane", [], ImmCheck1_3>;
  defm SVPMOV_TO_VEC_LANE_D : PMOV_TO_VEC<"svpmov", "lUl", "aarch64_sve_pmov_to_vector_lane" ,[], ImmCheck1_7>;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2p1" in {
  // DUPQ
  def SVDUP_LANEQ_B  : SInst<"svdup_laneq[_{d}]", "ddi",  "cUc", MergeNone, "aarch64_sve_dup_laneq", [VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_15>]>;
  def SVDUP_LANEQ_H  : SInst<"svdup_laneq[_{d}]", "ddi",  "sUsh", MergeNone, "aarch64_sve_dup_laneq", [VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_7>]>;
  def SVDUP_LANEQ_S  : SInst<"svdup_laneq[_{d}]", "ddi",  "iUif", MergeNone, "aarch64_sve_dup_laneq", [VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_3>]>;
  def SVDUP_LANEQ_D  : SInst<"svdup_laneq[_{d}]", "ddi",  "lUld", MergeNone, "aarch64_sve_dup_laneq", [VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_1>]>;
}

let SVETargetGuard = "sve2p1,bf16", SMETargetGuard = "sme2p1,bf16" in {
  def SVDUP_LANEQ_BF16  : SInst<"svdup_laneq[_{d}]", "ddi",  "b", MergeNone, "aarch64_sve_dup_laneq", [VerifyRuntimeMode], [ImmCheck<1, ImmCheck0_7>]>;
}

//
// Multi-vector convert to/from floating-point.
//
let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
  def SVCVT_F16_X2  : SInst<"svcvt_f16[_f32_x2]", "e2", "f", MergeNone, "aarch64_sve_fcvt_x2", [IsStreaming],[]>;
  def SVCVT_BF16_X2 : SInst<"svcvt_bf16[_f32_x2]", "$2", "f", MergeNone, "aarch64_sve_bfcvt_x2", [IsOverloadNone, IsStreaming],[]>;

  def SVCVT_F32_U32_X2 : SInst<"svcvt_{d}[_u32_x2]", "2.d2.u", "f",  MergeNone, "aarch64_sve_ucvtf_x2",  [IsStreaming, IsOverloadWhileOrMultiVecCvt], []>;
  def SVCVT_U32_F32_X2 : SInst<"svcvt_{d}[_f32_x2]", "2.d2.M", "Ui", MergeNone, "aarch64_sve_fcvtzu_x2", [IsStreaming, IsOverloadWhileOrMultiVecCvt], []>;
  def SVCVT_F32_S32_X2 : SInst<"svcvt_{d}[_s32_x2]", "2.d2.x", "f",  MergeNone, "aarch64_sve_scvtf_x2",  [IsStreaming, IsOverloadWhileOrMultiVecCvt], []>;
  def SVCVT_S32_F32_X2 : SInst<"svcvt_{d}[_f32_x2]", "2.d2.M", "i",  MergeNone, "aarch64_sve_fcvtzs_x2", [IsStreaming, IsOverloadWhileOrMultiVecCvt], []>;

  def SVCVT_F32_U32_X4 : SInst<"svcvt_{d}[_u32_x4]", "4.d4.u", "f",  MergeNone, "aarch64_sve_ucvtf_x4",  [IsStreaming, IsOverloadWhileOrMultiVecCvt], []>;
  def SVCVT_U32_F32_X4 : SInst<"svcvt_{d}[_f32_x4]", "4.d4.M", "Ui", MergeNone, "aarch64_sve_fcvtzu_x4", [IsStreaming, IsOverloadWhileOrMultiVecCvt], []>;
  def SVCVT_F32_S32_X4 : SInst<"svcvt_{d}[_s32_x4]", "4.d4.x", "f",  MergeNone, "aarch64_sve_scvtf_x4",  [IsStreaming, IsOverloadWhileOrMultiVecCvt], []>;
  def SVCVT_S32_F32_X4 : SInst<"svcvt_{d}[_f32_x4]", "4.d4.M", "i",  MergeNone, "aarch64_sve_fcvtzs_x4", [IsStreaming, IsOverloadWhileOrMultiVecCvt], []>;
}

let SVETargetGuard = InvalidMode, SMETargetGuard = "sme-f16f16" in {
  def SVCVT_F32_X2 : SInst<"svcvt_{d}[_f16_x2]", "2h", "f", MergeNone, "aarch64_sve_fcvt_widen_x2", [ IsStreaming],[]>;
}

//
// Multi-vector floating-point convert from single-precision to interleaved half-precision/BFloat16
//
let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
  def SVCVTN_F16_X2  : SInst<"svcvtn_f16[_f32_x2]", "e2", "f", MergeNone, "aarch64_sve_fcvtn_x2", [IsStreaming],[]>;
  def SVCVTN_BF16_X2 : SInst<"svcvtn_bf16[_f32_x2]", "$2", "f", MergeNone, "aarch64_sve_bfcvtn_x2", [IsOverloadNone, IsStreaming],[]>;
}

//
//Multi-vector floating-point convert from half-precision to deinterleaved single-precision.
//
let SVETargetGuard = InvalidMode, SMETargetGuard = "sme-f16f16" in {
  def SVCVTL_F32_X2 : SInst<"svcvtl_f32[_f16_x2]", "2h", "f", MergeNone, "aarch64_sve_fcvtl_widen_x2", [ IsStreaming],[]>;
}

//
// Multi-vector saturating extract narrow
//
let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
 def SVQCVT_S16_S32_X2 : SInst<"svqcvt_s16[_{d}_x2]", "h2.d", "i", MergeNone, "aarch64_sve_sqcvt_x2", [IsStreaming], []>;
 def SVQCVT_U16_U32_X2 : SInst<"svqcvt_u16[_{d}_x2]", "e2.d", "Ui", MergeNone, "aarch64_sve_uqcvt_x2", [IsStreaming], []>;
 def SVQCVT_U16_S32_X2 : SInst<"svqcvt_u16[_{d}_x2]", "e2.d", "i", MergeNone, "aarch64_sve_sqcvtu_x2", [IsStreaming], []>;

  def SVQCVT_S8_S32_X4 : SInst<"svqcvt_s8[_{d}_x4]", "q4.d", "i", MergeNone, "aarch64_sve_sqcvt_x4", [IsStreaming], []>;
  def SVQCVT_U8_U32_X4 : SInst<"svqcvt_u8[_{d}_x4]", "b4.d", "Ui", MergeNone, "aarch64_sve_uqcvt_x4", [IsStreaming], []>;
  def SVQCVT_U8_S32_X4 : SInst<"svqcvt_u8[_{d}_x4]", "b4.d", "i", MergeNone, "aarch64_sve_sqcvtu_x4", [IsStreaming], []>;

  def SVQCVT_S16_S64_X4 : SInst<"svqcvt_s16[_{d}_x4]", "q4.d", "l", MergeNone, "aarch64_sve_sqcvt_x4", [IsStreaming], []>;
  def SVQCVT_U16_U64_X4 : SInst<"svqcvt_u16[_{d}_x4]", "b4.d", "Ul", MergeNone, "aarch64_sve_uqcvt_x4", [IsStreaming], []>;
  def SVQCVT_U16_S64_X4 : SInst<"svqcvt_u16[_{d}_x4]", "b4.d", "l", MergeNone, "aarch64_sve_sqcvtu_x4", [IsStreaming], []>;
}

//
// Multi-vector saturating extract narrow and interleave
//
let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
  def SVQCVTN_S16_S32_X2 : SInst<"svqcvtn_s16[_{d}_x2]", "h2.d", "i", MergeNone, "aarch64_sve_sqcvtn_x2", [VerifyRuntimeMode], []>;
  def SVQCVTN_U16_U32_X2 : SInst<"svqcvtn_u16[_{d}_x2]", "e2.d", "Ui", MergeNone, "aarch64_sve_uqcvtn_x2", [VerifyRuntimeMode], []>;
  def SVQCVTN_U16_S32_X2 : SInst<"svqcvtn_u16[_{d}_x2]", "e2.d", "i", MergeNone, "aarch64_sve_sqcvtun_x2", [VerifyRuntimeMode], []>;
}

let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
  def SVQCVTN_S8_S32_X4 : SInst<"svqcvtn_s8[_{d}_x4]", "q4.d", "i", MergeNone, "aarch64_sve_sqcvtn_x4", [IsStreaming], []>;
  def SVQCVTN_U8_U32_X4 : SInst<"svqcvtn_u8[_{d}_x4]", "b4.d", "Ui", MergeNone, "aarch64_sve_uqcvtn_x4", [IsStreaming], []>;
  def SVQCVTN_U8_S32_X4 : SInst<"svqcvtn_u8[_{d}_x4]", "b4.d", "i", MergeNone, "aarch64_sve_sqcvtun_x4", [IsStreaming], []>;

  def SVQCVTN_S16_S64_X4 : SInst<"svqcvtn_s16[_{d}_x4]", "q4.d", "l", MergeNone, "aarch64_sve_sqcvtn_x4", [IsStreaming], []>;
  def SVQCVTN_U16_U64_X4 : SInst<"svqcvtn_u16[_{d}_x4]", "b4.d", "Ul", MergeNone, "aarch64_sve_uqcvtn_x4", [IsStreaming], []>;
  def SVQCVTN_U16_S64_X4 : SInst<"svqcvtn_u16[_{d}_x4]", "b4.d", "l", MergeNone, "aarch64_sve_sqcvtun_x4", [IsStreaming], []>;
}

//
// Multi-vector zip/unzip
//

let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
  def SVZIP_X2  : SInst<"svzip[_{d}_x2]",  "22", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_zip_x2",  [IsStreaming], []>;
  def SVZIPQ_X2 : SInst<"svzipq[_{d}_x2]", "22", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_zipq_x2", [IsStreaming], []>;
  def SVZIP_X4  : SInst<"svzip[_{d}_x4]",  "44", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_zip_x4",  [IsStreaming], []>;
  def SVZIPQ_X4 : SInst<"svzipq[_{d}_x4]", "44", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_zipq_x4", [IsStreaming], []>;

  def SVUZP_X2  : SInst<"svuzp[_{d}_x2]",  "22", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_uzp_x2",  [IsStreaming], []>;
  def SVUZPQ_X2 : SInst<"svuzpq[_{d}_x2]", "22", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_uzpq_x2", [IsStreaming], []>;
  def SVUZP_X4  : SInst<"svuzp[_{d}_x4]",  "44", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_uzp_x4",  [IsStreaming], []>;
  def SVUZPQ_X4 : SInst<"svuzpq[_{d}_x4]", "44", "cUcsUsiUilUlbhfd", MergeNone, "aarch64_sve_uzpq_x4", [IsStreaming], []>;
}

//
// Multi-vector unpack
//

let SVETargetGuard = InvalidMode, SMETargetGuard = "sme2" in {
  def SVSUNPK_X2 : SInst<"svunpk_{d}[_{1}_x2]", "2h",   "sil",    MergeNone, "aarch64_sve_sunpk_x2", [IsStreaming], []>;
  def SVUUNPK_X2 : SInst<"svunpk_{d}[_{1}_x2]", "2h",   "UsUiUl", MergeNone, "aarch64_sve_uunpk_x2", [IsStreaming], []>;
  def SVSUNPK_X4 : SInst<"svunpk_{d}[_{3}_x4]", "42.h", "sil",    MergeNone, "aarch64_sve_sunpk_x4", [IsStreaming], []>;
  def SVUUNPK_X4 : SInst<"svunpk_{d}[_{3}_x4]", "42.h", "UsUiUl", MergeNone, "aarch64_sve_uunpk_x4", [IsStreaming], []>;
}

let SVETargetGuard = "sve2p1", SMETargetGuard = "sme2" in {
// == BFloat16 multiply-subtract ==
  def SVBFMLSLB : SInst<"svbfmlslb[_{d}]", "dd$$", "f", MergeNone, "aarch64_sve_bfmlslb", [IsOverloadNone, VerifyRuntimeMode], []>;
  def SVBFMLSLT : SInst<"svbfmlslt[_{d}]", "dd$$", "f", MergeNone, "aarch64_sve_bfmlslt", [IsOverloadNone, VerifyRuntimeMode], []>;

  def SVBFMLSLB_LANE : SInst<"svbfmlslb_lane[_{d}]", "dd$$i", "f", MergeNone, "aarch64_sve_bfmlslb_lane", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<3, ImmCheck0_7>]>;
  def SVBFMLSLT_LANE : SInst<"svbfmlslt_lane[_{d}]", "dd$$i", "f", MergeNone, "aarch64_sve_bfmlslt_lane", [IsOverloadNone, VerifyRuntimeMode], [ImmCheck<3, ImmCheck0_7>]>;
}
