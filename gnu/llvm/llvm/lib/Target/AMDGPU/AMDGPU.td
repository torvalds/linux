//===-- AMDGPU.td - AMDGPU Tablegen files --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===------------------------------------------------------------===//

include "llvm/TableGen/SearchableTable.td"
include "llvm/Target/Target.td"
include "AMDGPUFeatures.td"
include "AMDGPUPredicateControl.td"

def p0 : PtrValueType<i64, 0>;
def p1 : PtrValueType<i64, 1>;
def p2 : PtrValueType<i32, 2>;
def p3 : PtrValueType<i32, 3>;
def p4 : PtrValueType<i64, 4>;
def p5 : PtrValueType<i32, 5>;
def p6 : PtrValueType<i32, 6>;

//===------------------------------------------------------------===//
// Subtarget Features (device properties)
//===------------------------------------------------------------===//

def FeatureFastFMAF32 : SubtargetFeature<"fast-fmaf",
  "FastFMAF32",
  "true",
  "Assuming f32 fma is at least as fast as mul + add"
>;

def FeatureFastDenormalF32 : SubtargetFeature<"fast-denormal-f32",
  "FastDenormalF32",
  "true",
  "Enabling denormals does not cause f32 instructions to run at f64 rates"
>;

def FeatureMIMG_R128 : SubtargetFeature<"mimg-r128",
  "MIMG_R128",
  "true",
  "Support 128-bit texture resources"
>;

def HalfRate64Ops : SubtargetFeature<"half-rate-64-ops",
  "HalfRate64Ops",
  "true",
  "Most fp64 instructions are half rate instead of quarter"
>;

def FullRate64Ops : SubtargetFeature<"full-rate-64-ops",
  "FullRate64Ops",
  "true",
  "Most fp64 instructions are full rate"
>;

def FeatureFlatAddressSpace : SubtargetFeature<"flat-address-space",
  "FlatAddressSpace",
  "true",
  "Support flat address space"
>;

def FeatureFlatInstOffsets : SubtargetFeature<"flat-inst-offsets",
  "FlatInstOffsets",
  "true",
  "Flat instructions have immediate offset addressing mode"
>;

def FeatureFlatGlobalInsts : SubtargetFeature<"flat-global-insts",
  "FlatGlobalInsts",
  "true",
  "Have global_* flat memory instructions"
>;

def FeatureFlatScratchInsts : SubtargetFeature<"flat-scratch-insts",
  "FlatScratchInsts",
  "true",
  "Have scratch_* flat memory instructions"
>;

def FeatureScalarFlatScratchInsts : SubtargetFeature<"scalar-flat-scratch-insts",
  "ScalarFlatScratchInsts",
  "true",
  "Have s_scratch_* flat memory instructions"
>;

def FeatureEnableFlatScratch : SubtargetFeature<"enable-flat-scratch",
  "EnableFlatScratch",
  "true",
  "Use scratch_* flat memory instructions to access scratch"
>;

def FeatureAddNoCarryInsts : SubtargetFeature<"add-no-carry-insts",
  "AddNoCarryInsts",
  "true",
  "Have VALU add/sub instructions without carry out"
>;

def FeatureUnalignedBufferAccess : SubtargetFeature<"unaligned-buffer-access",
  "UnalignedBufferAccess",
  "true",
  "Hardware supports unaligned global loads and stores"
>;

def FeatureTrapHandler: SubtargetFeature<"trap-handler",
  "TrapHandler",
  "true",
  "Trap handler support"
>;

def FeatureUnalignedScratchAccess : SubtargetFeature<"unaligned-scratch-access",
  "UnalignedScratchAccess",
  "true",
  "Support unaligned scratch loads and stores"
>;

def FeatureUnalignedDSAccess : SubtargetFeature<"unaligned-ds-access",
  "UnalignedDSAccess",
  "true",
  "Hardware supports unaligned local and region loads and stores"
>;

def FeatureApertureRegs : SubtargetFeature<"aperture-regs",
  "HasApertureRegs",
  "true",
  "Has Memory Aperture Base and Size Registers"
>;

def FeatureMadMixInsts : SubtargetFeature<"mad-mix-insts",
  "HasMadMixInsts",
  "true",
  "Has v_mad_mix_f32, v_mad_mixlo_f16, v_mad_mixhi_f16 instructions"
>;

def FeatureFmaMixInsts : SubtargetFeature<"fma-mix-insts",
  "HasFmaMixInsts",
  "true",
  "Has v_fma_mix_f32, v_fma_mixlo_f16, v_fma_mixhi_f16 instructions"
>;

def FeatureSupportsXNACK : SubtargetFeature<"xnack-support",
  "SupportsXNACK",
  "true",
  "Hardware supports XNACK"
>;

// XNACK is disabled if SH_MEM_CONFIG.ADDRESS_MODE = GPUVM on chips that support
// XNACK. The current default kernel driver setting is:
// - graphics ring: XNACK disabled
// - compute ring: XNACK enabled
//
// If XNACK is enabled, the VMEM latency can be worse.
// If XNACK is disabled, the 2 SGPRs can be used for general purposes.
def FeatureXNACK : SubtargetFeature<"xnack",
  "EnableXNACK",
  "true",
  "Enable XNACK support"
>;

def FeatureTgSplit : SubtargetFeature<"tgsplit",
  "EnableTgSplit",
  "true",
  "Enable threadgroup split execution"
>;

def FeatureCuMode : SubtargetFeature<"cumode",
  "EnableCuMode",
  "true",
  "Enable CU wavefront execution mode"
>;

def FeaturePreciseMemory
    : SubtargetFeature<"precise-memory", "EnablePreciseMemory",
                       "true", "Enable precise memory mode">;

def FeatureSGPRInitBug : SubtargetFeature<"sgpr-init-bug",
  "SGPRInitBug",
  "true",
  "VI SGPR initialization bug requiring a fixed SGPR allocation size"
>;

def FeatureUserSGPRInit16Bug : SubtargetFeature<"user-sgpr-init16-bug",
  "UserSGPRInit16Bug",
  "true",
  "Bug requiring at least 16 user+system SGPRs to be enabled"
>;

def FeatureLdsMisalignedBug : SubtargetFeature<"lds-misaligned-bug",
  "LDSMisalignedBug",
  "true",
  "Some GFX10 bug with multi-dword LDS and flat access that is not naturally aligned in WGP mode"
>;

def FeatureMFMAInlineLiteralBug : SubtargetFeature<"mfma-inline-literal-bug",
  "HasMFMAInlineLiteralBug",
  "true",
  "MFMA cannot use inline literal as SrcC"
>;

def FeatureVcmpxPermlaneHazard : SubtargetFeature<"vcmpx-permlane-hazard",
  "HasVcmpxPermlaneHazard",
  "true",
  "TODO: describe me"
>;

def FeatureVMEMtoScalarWriteHazard : SubtargetFeature<"vmem-to-scalar-write-hazard",
  "HasVMEMtoScalarWriteHazard",
  "true",
  "VMEM instruction followed by scalar writing to EXEC mask, M0 or SGPR leads to incorrect execution."
>;

def FeatureSMEMtoVectorWriteHazard : SubtargetFeature<"smem-to-vector-write-hazard",
  "HasSMEMtoVectorWriteHazard",
  "true",
  "s_load_dword followed by v_cmp page faults"
>;

def FeatureInstFwdPrefetchBug : SubtargetFeature<"inst-fwd-prefetch-bug",
  "HasInstFwdPrefetchBug",
  "true",
  "S_INST_PREFETCH instruction causes shader to hang"
>;

def FeatureVcmpxExecWARHazard : SubtargetFeature<"vcmpx-exec-war-hazard",
  "HasVcmpxExecWARHazard",
  "true",
  "V_CMPX WAR hazard on EXEC (V_CMPX issue ONLY)"
>;

def FeatureLdsBranchVmemWARHazard : SubtargetFeature<"lds-branch-vmem-war-hazard",
  "HasLdsBranchVmemWARHazard",
  "true",
  "Switching between LDS and VMEM-tex not waiting VM_VSRC=0"
>;

class FeatureMaxHardClauseLength<int size> : SubtargetFeature<
  "max-hard-clause-length-"#size,
  "MaxHardClauseLength",
  !cast<string>(size),
  "Maximum number of instructions in an explicit S_CLAUSE is "#size
>;

/// Work around a hardware bug on some chips that can be triggered
/// under certain circumstances when clauses are longer than 32 operations.
def FeatureMaxHardClauseLength32 : FeatureMaxHardClauseLength<32>;
/// While the S_CLAUSE instruction permits encoding clause lengths up to 64,
/// hardware documentation for gfx10+ indicates that 63 is the maximum
/// permitted clause length.
def FeatureMaxHardClauseLength63 : FeatureMaxHardClauseLength<63>;

def FeatureNSAtoVMEMBug : SubtargetFeature<"nsa-to-vmem-bug",
  "HasNSAtoVMEMBug",
  "true",
  "MIMG-NSA followed by VMEM fail if EXEC_LO or EXEC_HI equals zero"
>;

def FeatureNSAClauseBug : SubtargetFeature<"nsa-clause-bug",
  "HasNSAClauseBug",
  "true",
  "MIMG-NSA in a hard clause has unpredictable results on GFX10.1"
>;

def FeatureFlatSegmentOffsetBug : SubtargetFeature<"flat-segment-offset-bug",
  "HasFlatSegmentOffsetBug",
  "true",
  "GFX10 bug where inst_offset is ignored when flat instructions access global memory"
>;

def FeatureNegativeScratchOffsetBug : SubtargetFeature<"negative-scratch-offset-bug",
  "NegativeScratchOffsetBug",
  "true",
  "Negative immediate offsets in scratch instructions with an SGPR offset page fault on GFX9"
>;

def FeatureNegativeUnalignedScratchOffsetBug : SubtargetFeature<"negative-unaligned-scratch-offset-bug",
  "NegativeUnalignedScratchOffsetBug",
  "true",
  "Scratch instructions with a VGPR offset and a negative immediate offset that is not a multiple of 4 read wrong memory on GFX10"
>;

def FeatureOffset3fBug : SubtargetFeature<"offset-3f-bug",
  "HasOffset3fBug",
  "true",
  "Branch offset of 3f hardware bug"
>;

def FeatureImageStoreD16Bug : SubtargetFeature<"image-store-d16-bug",
  "HasImageStoreD16Bug",
  "true",
  "Image Store D16 hardware bug"
>;

def FeatureImageGather4D16Bug : SubtargetFeature<"image-gather4-d16-bug",
  "HasImageGather4D16Bug",
  "true",
  "Image Gather4 D16 hardware bug"
>;

def FeatureMADIntraFwdBug : SubtargetFeature<"mad-intra-fwd-bug",
  "HasMADIntraFwdBug",
  "true",
  "MAD_U64/I64 intra instruction forwarding bug"
>;

def FeatureMSAALoadDstSelBug : SubtargetFeature<"msaa-load-dst-sel-bug",
  "HasMSAALoadDstSelBug",
  "true",
  "MSAA loads not honoring dst_sel bug"
>;

def FeaturePrivEnabledTrap2NopBug : SubtargetFeature<"priv-enabled-trap2-nop-bug",
  "HasPrivEnabledTrap2NopBug",
  "true",
  "Hardware that runs with PRIV=1 interpreting 's_trap 2' as a nop bug"
>;

class SubtargetFeatureLDSBankCount <int Value> : SubtargetFeature <
  "ldsbankcount"#Value,
  "LDSBankCount",
  !cast<string>(Value),
  "The number of LDS banks per compute unit."
>;

def FeatureLDSBankCount16 : SubtargetFeatureLDSBankCount<16>;
def FeatureLDSBankCount32 : SubtargetFeatureLDSBankCount<32>;

def FeatureGCN3Encoding : SubtargetFeature<"gcn3-encoding",
  "GCN3Encoding",
  "true",
  "Encoding format for VI"
>;

def FeatureCIInsts : SubtargetFeature<"ci-insts",
  "CIInsts",
  "true",
  "Additional instructions for CI+"
>;

def FeatureGFX8Insts : SubtargetFeature<"gfx8-insts",
  "GFX8Insts",
  "true",
  "Additional instructions for GFX8+"
>;

def FeatureGFX9Insts : SubtargetFeature<"gfx9-insts",
  "GFX9Insts",
  "true",
  "Additional instructions for GFX9+"
>;

def FeatureGFX90AInsts : SubtargetFeature<"gfx90a-insts",
  "GFX90AInsts",
  "true",
  "Additional instructions for GFX90A+"
  // [HasAtomicFMinFMaxF64GlobalInsts, HasAtomicFMinFMaxF64FlatInsts] // TODO
>;

def FeatureGFX940Insts : SubtargetFeature<"gfx940-insts",
  "GFX940Insts",
  "true",
  "Additional instructions for GFX940+"
>;

def FeatureGFX10Insts : SubtargetFeature<"gfx10-insts",
  "GFX10Insts",
  "true",
  "Additional instructions for GFX10+"
>;

def FeatureGFX11Insts : SubtargetFeature<"gfx11-insts",
  "GFX11Insts",
  "true",
  "Additional instructions for GFX11+"
>;

def FeatureGFX12Insts : SubtargetFeature<"gfx12-insts",
  "GFX12Insts",
  "true",
  "Additional instructions for GFX12+"
>;

def FeatureGFX10_3Insts : SubtargetFeature<"gfx10-3-insts",
  "GFX10_3Insts",
  "true",
  "Additional instructions for GFX10.3"
>;

def FeatureGFX7GFX8GFX9Insts : SubtargetFeature<"gfx7-gfx8-gfx9-insts",
  "GFX7GFX8GFX9Insts",
  "true",
  "Instructions shared in GFX7, GFX8, GFX9"
>;

def FeatureSMemRealTime : SubtargetFeature<"s-memrealtime",
  "HasSMemRealTime",
  "true",
  "Has s_memrealtime instruction"
>;

def FeatureInv2PiInlineImm : SubtargetFeature<"inv-2pi-inline-imm",
  "HasInv2PiInlineImm",
  "true",
  "Has 1 / (2 * pi) as inline immediate"
>;

def Feature16BitInsts : SubtargetFeature<"16-bit-insts",
  "Has16BitInsts",
  "true",
  "Has i16/f16 instructions"
>;

def FeatureTrue16BitInsts : SubtargetFeature<"true16",
  "HasTrue16BitInsts",
  "true",
  "True 16-bit operand instructions"
>;

def FeatureRealTrue16Insts : SubtargetFeature<"real-true16",
  "EnableRealTrue16Insts",
  "true",
  "Use true 16-bit registers"
>;

def FeatureVOP3P : SubtargetFeature<"vop3p",
  "HasVOP3PInsts",
  "true",
  "Has VOP3P packed instructions"
>;

def FeatureMovrel : SubtargetFeature<"movrel",
  "HasMovrel",
  "true",
  "Has v_movrel*_b32 instructions"
>;

def FeatureVGPRIndexMode : SubtargetFeature<"vgpr-index-mode",
  "HasVGPRIndexMode",
  "true",
  "Has VGPR mode register indexing"
>;

def FeatureScalarDwordx3Loads : SubtargetFeature<"scalar-dwordx3-loads",
  "HasScalarDwordx3Loads",
  "true",
  "Has 96-bit scalar load instructions"
>;

def FeatureScalarStores : SubtargetFeature<"scalar-stores",
  "HasScalarStores",
  "true",
  "Has store scalar memory instructions"
>;

def FeatureScalarAtomics : SubtargetFeature<"scalar-atomics",
  "HasScalarAtomics",
  "true",
  "Has atomic scalar memory instructions"
>;

def FeatureSDWA : SubtargetFeature<"sdwa",
  "HasSDWA",
  "true",
  "Support SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureSDWAOmod : SubtargetFeature<"sdwa-omod",
  "HasSDWAOmod",
  "true",
  "Support OMod with SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureSDWAScalar : SubtargetFeature<"sdwa-scalar",
  "HasSDWAScalar",
  "true",
  "Support scalar register with SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureSDWASdst : SubtargetFeature<"sdwa-sdst",
  "HasSDWASdst",
  "true",
  "Support scalar dst for VOPC with SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureSDWAMac : SubtargetFeature<"sdwa-mav",
  "HasSDWAMac",
  "true",
  "Support v_mac_f32/f16 with SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureSDWAOutModsVOPC : SubtargetFeature<"sdwa-out-mods-vopc",
  "HasSDWAOutModsVOPC",
  "true",
  "Support clamp for VOPC with SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureDPP : SubtargetFeature<"dpp",
  "HasDPP",
  "true",
  "Support DPP (Data Parallel Primitives) extension"
>;

// DPP8 allows arbitrary cross-lane swizzling within groups of 8 lanes.
def FeatureDPP8 : SubtargetFeature<"dpp8",
  "HasDPP8",
  "true",
  "Support DPP8 (Data Parallel Primitives) extension"
>;

def FeatureDPALU_DPP : SubtargetFeature<"dpp-64bit",
  "HasDPALU_DPP",
  "true",
  "Support DPP (Data Parallel Primitives) extension in DP ALU"
>;

def FeatureDPPSrc1SGPR : SubtargetFeature<"dpp-src1-sgpr",
  "HasDPPSrc1SGPR",
  "true",
  "Support SGPR for Src1 of DPP instructions"
>;

def FeaturePackedFP32Ops : SubtargetFeature<"packed-fp32-ops",
  "HasPackedFP32Ops",
  "true",
  "Support packed fp32 instructions"
>;

def FeatureR128A16 : SubtargetFeature<"r128-a16",
  "HasR128A16",
  "true",
  "Support gfx9-style A16 for 16-bit coordinates/gradients/lod/clamp/mip image operands, where a16 is aliased with r128"
>;

def FeatureA16 : SubtargetFeature<"a16",
  "HasA16",
  "true",
  "Support A16 for 16-bit coordinates/gradients/lod/clamp/mip image operands"
>;

def FeatureG16 : SubtargetFeature<"g16",
  "HasG16",
  "true",
  "Support G16 for 16-bit gradient image operands"
>;

def FeatureNSAEncoding : SubtargetFeature<"nsa-encoding",
  "HasNSAEncoding",
  "true",
  "Support NSA encoding for image instructions"
>;

def FeaturePartialNSAEncoding : SubtargetFeature<"partial-nsa-encoding",
  "HasPartialNSAEncoding",
  "true",
  "Support partial NSA encoding for image instructions"
>;

def FeatureImageInsts : SubtargetFeature<"image-insts",
  "HasImageInsts",
  "true",
  "Support image instructions"
>;

def FeatureExtendedImageInsts : SubtargetFeature<"extended-image-insts",
  "HasExtendedImageInsts",
  "true",
  "Support mips != 0, lod != 0, gather4, and get_lod"
>;

def FeatureGFX10_AEncoding : SubtargetFeature<"gfx10_a-encoding",
  "GFX10_AEncoding",
  "true",
  "Has BVH ray tracing instructions"
>;

def FeatureGFX10_BEncoding : SubtargetFeature<"gfx10_b-encoding",
  "GFX10_BEncoding",
  "true",
  "Encoding format GFX10_B"
>;

def FeatureIntClamp : SubtargetFeature<"int-clamp-insts",
  "HasIntClamp",
  "true",
  "Support clamp for integer destination"
>;

def FeatureUnpackedD16VMem : SubtargetFeature<"unpacked-d16-vmem",
  "HasUnpackedD16VMem",
  "true",
  "Has unpacked d16 vmem instructions"
>;

def FeatureDLInsts : SubtargetFeature<"dl-insts",
  "HasDLInsts",
  "true",
  "Has v_fmac_f32 and v_xnor_b32 instructions"
>;

def FeatureFmacF64Inst : SubtargetFeature<"fmacf64-inst",
  "HasFmacF64Inst",
  "true",
  "Has v_fmac_f64 instruction"
>;

def FeatureDot1Insts : SubtargetFeature<"dot1-insts",
  "HasDot1Insts",
  "true",
  "Has v_dot4_i32_i8 and v_dot8_i32_i4 instructions"
>;

def FeatureDot2Insts : SubtargetFeature<"dot2-insts",
  "HasDot2Insts",
  "true",
  "Has v_dot2_i32_i16, v_dot2_u32_u16 instructions"
>;

def FeatureDot3Insts : SubtargetFeature<"dot3-insts",
  "HasDot3Insts",
  "true",
  "Has v_dot8c_i32_i4 instruction"
>;

def FeatureDot4Insts : SubtargetFeature<"dot4-insts",
  "HasDot4Insts",
  "true",
  "Has v_dot2c_i32_i16 instruction"
>;

def FeatureDot5Insts : SubtargetFeature<"dot5-insts",
  "HasDot5Insts",
  "true",
  "Has v_dot2c_f32_f16 instruction"
>;

def FeatureDot6Insts : SubtargetFeature<"dot6-insts",
  "HasDot6Insts",
  "true",
  "Has v_dot4c_i32_i8 instruction"
>;

def FeatureDot7Insts : SubtargetFeature<"dot7-insts",
  "HasDot7Insts",
  "true",
  "Has v_dot4_u32_u8, v_dot8_u32_u4 instructions"
>;

def FeatureDot8Insts : SubtargetFeature<"dot8-insts",
  "HasDot8Insts",
  "true",
  "Has v_dot4_i32_iu8, v_dot8_i32_iu4 instructions"
>;

def FeatureDot9Insts : SubtargetFeature<"dot9-insts",
  "HasDot9Insts",
  "true",
  "Has v_dot2_f16_f16, v_dot2_bf16_bf16, v_dot2_f32_bf16 instructions"
>;

def FeatureDot10Insts : SubtargetFeature<"dot10-insts",
  "HasDot10Insts",
  "true",
  "Has v_dot2_f32_f16 instruction"
>;

def FeatureDot11Insts : SubtargetFeature<"dot11-insts",
  "HasDot11Insts",
  "true",
  "Has v_dot4_f32_fp8_fp8, v_dot4_f32_fp8_bf8, v_dot4_f32_bf8_fp8, v_dot4_f32_bf8_bf8 instructions"
>;

def FeatureMAIInsts : SubtargetFeature<"mai-insts",
  "HasMAIInsts",
  "true",
  "Has mAI instructions"
>;

def FeatureFP8Insts : SubtargetFeature<"fp8-insts",
  "HasFP8Insts",
  "true",
  "Has fp8 and bf8 instructions"
>;

def FeatureFP8ConversionInsts : SubtargetFeature<"fp8-conversion-insts",
  "HasFP8ConversionInsts",
  "true",
  "Has fp8 and bf8 conversion instructions"
>;

def FeaturePkFmacF16Inst : SubtargetFeature<"pk-fmac-f16-inst",
  "HasPkFmacF16Inst",
  "true",
  "Has v_pk_fmac_f16 instruction"
>;

def FeatureAtomicDsPkAdd16Insts : SubtargetFeature<"atomic-ds-pk-add-16-insts",
  "HasAtomicDsPkAdd16Insts",
  "true",
  "Has ds_pk_add_bf16, ds_pk_add_f16, ds_pk_add_rtn_bf16, "
  "ds_pk_add_rtn_f16 instructions"
>;

def FeatureAtomicFlatPkAdd16Insts : SubtargetFeature<"atomic-flat-pk-add-16-insts",
  "HasAtomicFlatPkAdd16Insts",
  "true",
  "Has flat_atomic_pk_add_f16 and flat_atomic_pk_add_bf16 instructions"
>;

def FeatureAtomicFaddRtnInsts : SubtargetFeature<"atomic-fadd-rtn-insts",
  "HasAtomicFaddRtnInsts",
  "true",
  "Has buffer_atomic_add_f32 and global_atomic_add_f32 instructions that "
  "return original value",
  [FeatureFlatGlobalInsts]
>;

def FeatureAtomicFMinFMaxF32GlobalInsts : SubtargetFeature<"atomic-fmin-fmax-global-f32",
  "HasAtomicFMinFMaxF32GlobalInsts",
  "true",
  "Has global/buffer instructions for atomicrmw fmin/fmax for float"
>;

def FeatureAtomicFMinFMaxF64GlobalInsts : SubtargetFeature<"atomic-fmin-fmax-global-f64",
  "HasAtomicFMinFMaxF64GlobalInsts",
  "true",
  "Has global/buffer instructions for atomicrmw fmin/fmax for float"
>;

def FeatureAtomicFMinFMaxF32FlatInsts : SubtargetFeature<"atomic-fmin-fmax-flat-f32",
  "HasAtomicFMinFMaxF32FlatInsts",
  "true",
  "Has flat memory instructions for atomicrmw fmin/fmax for float"
>;

def FeatureAtomicFMinFMaxF64FlatInsts : SubtargetFeature<"atomic-fmin-fmax-flat-f64",
  "HasAtomicFMinFMaxF64FlatInsts",
  "true",
  "Has flat memory instructions for atomicrmw fmin/fmax for double"
>;

def FeatureAtomicFaddNoRtnInsts : SubtargetFeature<"atomic-fadd-no-rtn-insts",
  "HasAtomicFaddNoRtnInsts",
  "true",
  "Has buffer_atomic_add_f32 and global_atomic_add_f32 instructions that "
  "don't return original value",
  [FeatureFlatGlobalInsts]
>;

def FeatureAtomicBufferGlobalPkAddF16NoRtnInsts
  : SubtargetFeature<"atomic-buffer-global-pk-add-f16-no-rtn-insts",
  "HasAtomicBufferGlobalPkAddF16NoRtnInsts",
  "true",
  "Has buffer_atomic_pk_add_f16 and global_atomic_pk_add_f16 instructions that "
  "don't return original value",
  [FeatureFlatGlobalInsts]
>;

def FeatureAtomicBufferGlobalPkAddF16Insts : SubtargetFeature<"atomic-buffer-global-pk-add-f16-insts",
 "HasAtomicBufferGlobalPkAddF16Insts",
 "true",
 "Has buffer_atomic_pk_add_f16 and global_atomic_pk_add_f16 instructions that "
 "can return original value",
 [FeatureFlatGlobalInsts]
>;

def FeatureAtomicGlobalPkAddBF16Inst : SubtargetFeature<"atomic-global-pk-add-bf16-inst",
 "HasAtomicGlobalPkAddBF16Inst",
 "true",
 "Has global_atomic_pk_add_bf16 instruction",
 [FeatureFlatGlobalInsts]
>;

def FeatureAtomicBufferPkAddBF16Inst : SubtargetFeature<"atomic-buffer-pk-add-bf16-inst",
 "HasAtomicBufferPkAddBF16Inst",
 "true",
 "Has buffer_atomic_pk_add_bf16 instruction"
>;

def FeatureAtomicCSubNoRtnInsts : SubtargetFeature<"atomic-csub-no-rtn-insts",
  "HasAtomicCSubNoRtnInsts",
  "true",
  "Has buffer_atomic_csub and global_atomic_csub instructions that don't "
  "return original value"
>;

def FeatureFlatAtomicFaddF32Inst
  : SubtargetFeature<"flat-atomic-fadd-f32-inst",
  "HasFlatAtomicFaddF32Inst",
  "true",
  "Has flat_atomic_add_f32 instruction"
>;

def FeatureFlatBufferGlobalAtomicFaddF64Inst
  : SubtargetFeature<"flat-buffer-global-fadd-f64-inst",
  "HasFlatBufferGlobalAtomicFaddF64Inst",
  "true",
  "Has flat, buffer, and global instructions for f64 atomic fadd"
>;

def FeatureMemoryAtomicFAddF32DenormalSupport
  : SubtargetFeature<"memory-atomic-fadd-f32-denormal-support",
  "HasMemoryAtomicFaddF32DenormalSupport",
  "true",
  "global/flat/buffer atomic fadd for float supports denormal handling"
>;

def FeatureAgentScopeFineGrainedRemoteMemoryAtomics
  : SubtargetFeature<"agent-scope-fine-grained-remote-memory-atomics",
  "HasAgentScopeFineGrainedRemoteMemoryAtomics",
  "true",
  "Agent (device) scoped atomic operations, excluding those directly "
  "supported by PCIe (i.e. integer atomic add, exchange, and "
  "compare-and-swap), are functional for allocations in host or peer "
  "device memory."
>;

def FeatureDefaultComponentZero : SubtargetFeature<"default-component-zero",
  "HasDefaultComponentZero",
  "true",
  "BUFFER/IMAGE store instructions set unspecified components to zero (before GFX12)"
>;

def FeatureDefaultComponentBroadcast : SubtargetFeature<"default-component-broadcast",
  "HasDefaultComponentBroadcast",
  "true",
  "BUFFER/IMAGE store instructions set unspecified components to x component (GFX12)"
>;

def FeatureSupportsSRAMECC : SubtargetFeature<"sramecc-support",
  "SupportsSRAMECC",
  "true",
  "Hardware supports SRAMECC"
>;

def FeatureSRAMECC : SubtargetFeature<"sramecc",
  "EnableSRAMECC",
  "true",
  "Enable SRAMECC"
>;

def FeatureNoSdstCMPX : SubtargetFeature<"no-sdst-cmpx",
  "HasNoSdstCMPX",
  "true",
  "V_CMPX does not write VCC/SGPR in addition to EXEC"
>;

def FeatureVscnt : SubtargetFeature<"vscnt",
  "HasVscnt",
  "true",
  "Has separate store vscnt counter"
>;

def FeatureGetWaveIdInst : SubtargetFeature<"get-wave-id-inst",
  "HasGetWaveIdInst",
  "true",
  "Has s_get_waveid_in_workgroup instruction"
>;

def FeatureSMemTimeInst : SubtargetFeature<"s-memtime-inst",
  "HasSMemTimeInst",
  "true",
  "Has s_memtime instruction"
>;

def FeatureShaderCyclesRegister : SubtargetFeature<"shader-cycles-register",
  "HasShaderCyclesRegister",
  "true",
  "Has SHADER_CYCLES hardware register"
>;

def FeatureShaderCyclesHiLoRegisters : SubtargetFeature<"shader-cycles-hi-lo-registers",
  "HasShaderCyclesHiLoRegisters",
  "true",
  "Has SHADER_CYCLES_HI/LO hardware registers"
>;

def FeatureMadMacF32Insts : SubtargetFeature<"mad-mac-f32-insts",
  "HasMadMacF32Insts",
  "true",
  "Has v_mad_f32/v_mac_f32/v_madak_f32/v_madmk_f32 instructions"
>;

def FeatureDsSrc2Insts : SubtargetFeature<"ds-src2-insts",
  "HasDsSrc2Insts",
  "true",
  "Has ds_*_src2 instructions"
>;

def FeatureVOP3Literal : SubtargetFeature<"vop3-literal",
  "HasVOP3Literal",
  "true",
  "Can use one literal in VOP3"
>;

def FeatureNoDataDepHazard : SubtargetFeature<"no-data-dep-hazard",
  "HasNoDataDepHazard",
  "true",
  "Does not need SW waitstates"
>;

// Allocate 1536 VGPRs for wave32 and 768 VGPRs for wave64
// with allocation granularity 24 for wave32 and 12 for wave64
def Feature1_5xVGPRs : SubtargetFeature<"allocate1_5xvgprs",
  "Has1_5xVGPRs",
  "true",
  "Has 50% more physical VGPRs and 50% larger allocation granule"
>;


def FeatureVOPD : SubtargetFeature<"vopd",
  "HasVOPDInsts",
  "true",
  "Has VOPD dual issue wave32 instructions"
>;

def FeatureVALUTransUseHazard : SubtargetFeature<"valu-trans-use-hazard",
  "HasVALUTransUseHazard",
  "true",
  "Hazard when TRANS instructions are closely followed by a use of the result"
>;

def FeatureForceStoreSC0SC1 : SubtargetFeature<"force-store-sc0-sc1",
  "HasForceStoreSC0SC1",
  "true",
  "Has SC0 and SC1 on stores"
>;

def FeatureSALUFloatInsts : SubtargetFeature<"salu-float",
  "HasSALUFloatInsts",
  "true",
  "Has SALU floating point instructions"
>;

def FeatureVGPRSingleUseHintInsts : SubtargetFeature<"vgpr-singleuse-hint",
  "HasVGPRSingleUseHintInsts",
  "true",
  "Has single-use VGPR hint instructions"
>;

def FeaturePseudoScalarTrans : SubtargetFeature<"pseudo-scalar-trans",
  "HasPseudoScalarTrans",
  "true",
  "Has Pseudo Scalar Transcendental instructions"
>;

def FeatureHasRestrictedSOffset : SubtargetFeature<"restricted-soffset",
  "HasRestrictedSOffset",
  "true",
  "Has restricted SOffset (immediate not supported)."
>;

def FeatureRequiredExportPriority : SubtargetFeature<"required-export-priority",
  "HasRequiredExportPriority",
  "true",
  "Export priority must be explicitly manipulated on GFX11.5"
>;

def FeatureVmemWriteVgprInOrder : SubtargetFeature<"vmem-write-vgpr-in-order",
  "HasVmemWriteVgprInOrder",
  "true",
  "VMEM instructions of the same type write VGPR results in order"
>;

//===------------------------------------------------------------===//
// Subtarget Features (options and debugging)
//===------------------------------------------------------------===//

class FeatureMaxPrivateElementSize<int size> : SubtargetFeature<
  "max-private-element-size-"#size,
  "MaxPrivateElementSize",
  !cast<string>(size),
  "Maximum private access size may be "#size
>;

def FeatureMaxPrivateElementSize4 : FeatureMaxPrivateElementSize<4>;
def FeatureMaxPrivateElementSize8 : FeatureMaxPrivateElementSize<8>;
def FeatureMaxPrivateElementSize16 : FeatureMaxPrivateElementSize<16>;

def FeatureDumpCode : SubtargetFeature <"DumpCode",
  "DumpCode",
  "true",
  "Dump MachineInstrs in the CodeEmitter"
>;

def FeatureDumpCodeLower : SubtargetFeature <"dumpcode",
  "DumpCode",
  "true",
  "Dump MachineInstrs in the CodeEmitter"
>;

// XXX - This should probably be removed once enabled by default
def FeatureEnableLoadStoreOpt : SubtargetFeature <"load-store-opt",
  "EnableLoadStoreOpt",
  "true",
  "Enable SI load/store optimizer pass"
>;

// Performance debugging feature. Allow using DS instruction immediate
// offsets even if the base pointer can't be proven to be base. On SI,
// base pointer values that won't give the same result as a 16-bit add
// are not safe to fold, but this will override the conservative test
// for the base pointer.
def FeatureEnableUnsafeDSOffsetFolding : SubtargetFeature <
  "unsafe-ds-offset-folding",
  "EnableUnsafeDSOffsetFolding",
  "true",
  "Force using DS instruction immediate offsets on SI"
>;

def FeatureEnableSIScheduler : SubtargetFeature<"si-scheduler",
  "EnableSIScheduler",
  "true",
  "Enable SI Machine Scheduler"
>;

def FeatureEnableDS128 : SubtargetFeature<"enable-ds128",
  "EnableDS128",
  "true",
  "Use ds_{read|write}_b128"
>;

// Sparse texture support requires that all result registers are zeroed when
// PRTStrictNull is set to true. This feature is turned on for all architectures
// but is enabled as a feature in case there are situations where PRTStrictNull
// is disabled by the driver.
def FeatureEnablePRTStrictNull : SubtargetFeature<"enable-prt-strict-null",
  "EnablePRTStrictNull",
  "true",
  "Enable zeroing of result registers for sparse texture fetches"
>;

// Unless +-flat-for-global is specified, turn on FlatForGlobal for
// all OS-es on VI and newer hardware to avoid assertion failures due
// to missing ADDR64 variants of MUBUF instructions.
// FIXME: moveToVALU should be able to handle converting addr64 MUBUF
// instructions.

def FeatureFlatForGlobal : SubtargetFeature<"flat-for-global",
  "FlatForGlobal",
  "true",
  "Force to generate flat instruction for global"
>;

def FeatureAutoWaitcntBeforeBarrier : SubtargetFeature <
  "auto-waitcnt-before-barrier",
  "AutoWaitcntBeforeBarrier",
  "true",
  "Hardware automatically inserts waitcnt before barrier"
>;

def FeatureBackOffBarrier : SubtargetFeature <"back-off-barrier",
  "BackOffBarrier",
  "true",
  "Hardware supports backing off s_barrier if an exception occurs"
>;

def FeatureTrigReducedRange : SubtargetFeature<"trig-reduced-range",
  "HasTrigReducedRange",
  "true",
  "Requires use of fract on arguments to trig instructions"
>;

def FeatureKernargPreload : SubtargetFeature <"kernarg-preload",
  "KernargPreload",
  "true",
  "Hardware supports preloading of kernel arguments in user SGPRs."
>;

// Alignment enforcement is controlled by a configuration register:
// SH_MEM_CONFIG.alignment_mode
def FeatureUnalignedAccessMode : SubtargetFeature<"unaligned-access-mode",
  "UnalignedAccessMode",
  "true",
  "Enable unaligned global, local and region loads and stores if the hardware"
  " supports it"
>;

def FeaturePackedTID : SubtargetFeature<"packed-tid",
  "HasPackedTID",
  "true",
  "Workitem IDs are packed into v0 at kernel launch"
>;

def FeatureArchitectedFlatScratch : SubtargetFeature<"architected-flat-scratch",
  "HasArchitectedFlatScratch",
  "true",
  "Flat Scratch register is a readonly SPI initialized architected register"
>;

def FeatureArchitectedSGPRs : SubtargetFeature<"architected-sgprs",
  "HasArchitectedSGPRs",
  "true",
  "Enable the architected SGPRs"
>;

def FeatureGDS : SubtargetFeature<"gds",
  "HasGDS",
  "true",
  "Has Global Data Share"
>;

def FeatureGWS : SubtargetFeature<"gws",
  "HasGWS",
  "true",
  "Has Global Wave Sync"
>;

def FeatureRequiresCOV6 : SubtargetFeature<"requires-cov6",
  "RequiresCOV6",
  "true",
  "Target Requires Code Object V6"
>;

// Dummy feature used to disable assembler instructions.
def FeatureDisable : SubtargetFeature<"",
  "FeatureDisable","true",
  "Dummy feature to disable assembler instructions"
>;

//===----------------------------------------------------------------------===//

class GCNSubtargetFeatureGeneration <string Value,
                                     string FeatureName,
                                     list<SubtargetFeature> Implies> :
        SubtargetFeatureGeneration <Value, FeatureName, "GCNSubtarget", Implies>;

def FeatureSouthernIslands : GCNSubtargetFeatureGeneration<"SOUTHERN_ISLANDS",
    "southern-islands",
  [FeatureFP64, FeatureLocalMemorySize32768, FeatureMIMG_R128,
  FeatureWavefrontSize64, FeatureSMemTimeInst, FeatureMadMacF32Insts,
  FeatureDsSrc2Insts, FeatureLDSBankCount32, FeatureMovrel,
  FeatureTrigReducedRange, FeatureExtendedImageInsts, FeatureImageInsts,
  FeatureGDS, FeatureGWS, FeatureDefaultComponentZero,
  FeatureAtomicFMinFMaxF32GlobalInsts, FeatureAtomicFMinFMaxF64GlobalInsts,
  FeatureVmemWriteVgprInOrder
  ]
>;

def FeatureSeaIslands : GCNSubtargetFeatureGeneration<"SEA_ISLANDS",
    "sea-islands",
  [FeatureFP64, FeatureLocalMemorySize65536, FeatureMIMG_R128,
  FeatureWavefrontSize64, FeatureFlatAddressSpace,
  FeatureCIInsts, FeatureMovrel, FeatureTrigReducedRange,
  FeatureGFX7GFX8GFX9Insts, FeatureSMemTimeInst, FeatureMadMacF32Insts,
  FeatureDsSrc2Insts, FeatureExtendedImageInsts, FeatureUnalignedBufferAccess,
  FeatureImageInsts, FeatureGDS, FeatureGWS, FeatureDefaultComponentZero,
  FeatureAtomicFMinFMaxF32GlobalInsts, FeatureAtomicFMinFMaxF64GlobalInsts,
  FeatureAtomicFMinFMaxF32FlatInsts, FeatureAtomicFMinFMaxF64FlatInsts,
  FeatureVmemWriteVgprInOrder
  ]
>;

def FeatureVolcanicIslands : GCNSubtargetFeatureGeneration<"VOLCANIC_ISLANDS",
  "volcanic-islands",
  [FeatureFP64, FeatureLocalMemorySize65536, FeatureMIMG_R128,
   FeatureWavefrontSize64, FeatureFlatAddressSpace,
   FeatureGCN3Encoding, FeatureCIInsts, Feature16BitInsts,
   FeatureSMemRealTime, FeatureVGPRIndexMode, FeatureMovrel,
   FeatureScalarStores, FeatureInv2PiInlineImm,
   FeatureSDWA, FeatureSDWAOutModsVOPC, FeatureSDWAMac, FeatureDPP,
   FeatureIntClamp, FeatureTrigReducedRange, FeatureGFX8Insts,
   FeatureGFX7GFX8GFX9Insts, FeatureSMemTimeInst, FeatureMadMacF32Insts,
   FeatureDsSrc2Insts, FeatureExtendedImageInsts, FeatureFastDenormalF32,
   FeatureUnalignedBufferAccess, FeatureImageInsts, FeatureGDS, FeatureGWS,
   FeatureDefaultComponentZero, FeatureVmemWriteVgprInOrder
  ]
>;

def FeatureGFX9 : GCNSubtargetFeatureGeneration<"GFX9",
  "gfx9",
  [FeatureFP64, FeatureLocalMemorySize65536,
   FeatureWavefrontSize64, FeatureFlatAddressSpace,
   FeatureGCN3Encoding, FeatureCIInsts, Feature16BitInsts,
   FeatureSMemRealTime, FeatureScalarStores, FeatureInv2PiInlineImm,
   FeatureApertureRegs, FeatureGFX9Insts, FeatureVOP3P, FeatureVGPRIndexMode,
   FeatureFastFMAF32, FeatureDPP, FeatureIntClamp,
   FeatureSDWA, FeatureSDWAOmod, FeatureSDWAScalar, FeatureSDWASdst,
   FeatureFlatInstOffsets, FeatureFlatGlobalInsts, FeatureFlatScratchInsts,
   FeatureAddNoCarryInsts, FeatureGFX8Insts, FeatureGFX7GFX8GFX9Insts,
   FeatureScalarFlatScratchInsts, FeatureScalarAtomics, FeatureR128A16,
   FeatureA16, FeatureSMemTimeInst, FeatureFastDenormalF32, FeatureSupportsXNACK,
   FeatureUnalignedBufferAccess, FeatureUnalignedDSAccess,
   FeatureNegativeScratchOffsetBug, FeatureGWS, FeatureDefaultComponentZero,
   FeatureVmemWriteVgprInOrder
  ]
>;

def FeatureGFX10 : GCNSubtargetFeatureGeneration<"GFX10",
  "gfx10",
  [FeatureFP64, FeatureLocalMemorySize65536, FeatureMIMG_R128,
   FeatureFlatAddressSpace,
   FeatureCIInsts, Feature16BitInsts,
   FeatureSMemRealTime, FeatureInv2PiInlineImm,
   FeatureApertureRegs, FeatureGFX9Insts, FeatureGFX10Insts, FeatureVOP3P,
   FeatureMovrel, FeatureFastFMAF32, FeatureDPP, FeatureIntClamp,
   FeatureSDWA, FeatureSDWAOmod, FeatureSDWAScalar, FeatureSDWASdst,
   FeatureFlatInstOffsets, FeatureFlatGlobalInsts, FeatureFlatScratchInsts,
   FeatureAddNoCarryInsts, FeatureFmaMixInsts, FeatureGFX8Insts,
   FeatureNoSdstCMPX, FeatureVscnt,
   FeatureVOP3Literal, FeatureDPP8, FeatureExtendedImageInsts,
   FeatureNoDataDepHazard, FeaturePkFmacF16Inst,
   FeatureA16, FeatureSMemTimeInst, FeatureFastDenormalF32, FeatureG16,
   FeatureUnalignedBufferAccess, FeatureUnalignedDSAccess, FeatureImageInsts,
   FeatureGDS, FeatureGWS, FeatureDefaultComponentZero,
   FeatureMaxHardClauseLength63,
   FeatureAtomicFMinFMaxF32GlobalInsts, FeatureAtomicFMinFMaxF64GlobalInsts,
   FeatureAtomicFMinFMaxF32FlatInsts, FeatureAtomicFMinFMaxF64FlatInsts,
   FeatureVmemWriteVgprInOrder
  ]
>;

def FeatureGFX11 : GCNSubtargetFeatureGeneration<"GFX11",
  "gfx11",
  [FeatureFP64, FeatureLocalMemorySize65536, FeatureMIMG_R128,
   FeatureFlatAddressSpace, Feature16BitInsts,
   FeatureInv2PiInlineImm, FeatureApertureRegs,
   FeatureCIInsts, FeatureGFX8Insts, FeatureGFX9Insts, FeatureGFX10Insts,
   FeatureGFX10_AEncoding, FeatureGFX10_BEncoding, FeatureGFX10_3Insts,
   FeatureGFX11Insts, FeatureVOP3P, FeatureVOPD, FeatureTrue16BitInsts,
   FeatureMovrel, FeatureFastFMAF32, FeatureDPP, FeatureIntClamp,
   FeatureFlatInstOffsets, FeatureFlatGlobalInsts, FeatureFlatScratchInsts,
   FeatureAddNoCarryInsts, FeatureFmaMixInsts,
   FeatureNoSdstCMPX, FeatureVscnt,
   FeatureVOP3Literal, FeatureDPP8, FeatureExtendedImageInsts,
   FeatureNoDataDepHazard, FeaturePkFmacF16Inst,
   FeatureA16, FeatureFastDenormalF32, FeatureG16,
   FeatureUnalignedBufferAccess, FeatureUnalignedDSAccess, FeatureGDS,
   FeatureGWS, FeatureDefaultComponentZero,
   FeatureMaxHardClauseLength32,
   FeatureAtomicFMinFMaxF32GlobalInsts, FeatureAtomicFMinFMaxF32FlatInsts,
   FeatureVmemWriteVgprInOrder
  ]
>;

def FeatureGFX12 : GCNSubtargetFeatureGeneration<"GFX12",
  "gfx12",
  [FeatureFP64, FeatureLocalMemorySize65536, FeatureMIMG_R128,
   FeatureFlatAddressSpace, Feature16BitInsts,
   FeatureInv2PiInlineImm, FeatureApertureRegs,
   FeatureCIInsts, FeatureGFX8Insts, FeatureGFX9Insts, FeatureGFX10Insts,
   FeatureGFX10_AEncoding, FeatureGFX10_BEncoding, FeatureGFX10_3Insts,
   FeatureGFX11Insts, FeatureGFX12Insts, FeatureVOP3P, FeatureVOPD,
   FeatureMovrel, FeatureFastFMAF32, FeatureDPP, FeatureIntClamp,
   FeatureFlatInstOffsets, FeatureFlatGlobalInsts, FeatureFlatScratchInsts,
   FeatureAddNoCarryInsts, FeatureFmaMixInsts,
   FeatureNoSdstCMPX, FeatureVscnt,
   FeatureVOP3Literal, FeatureDPP8,
   FeatureNoDataDepHazard, FeaturePkFmacF16Inst,
   FeatureA16, FeatureFastDenormalF32, FeatureG16,
   FeatureUnalignedBufferAccess, FeatureUnalignedDSAccess,
   FeatureTrue16BitInsts, FeatureDefaultComponentBroadcast,
   FeatureMaxHardClauseLength32,
   FeatureAtomicFMinFMaxF32GlobalInsts, FeatureAtomicFMinFMaxF32FlatInsts,
   FeatureAgentScopeFineGrainedRemoteMemoryAtomics
  ]
>;

//===----------------------------------------------------------------------===//

class FeatureSet<list<SubtargetFeature> Features_> {
  list<SubtargetFeature> Features = Features_;
}

def FeatureISAVersion6_0_0 : FeatureSet<[FeatureSouthernIslands,
   FeatureFastFMAF32,
   HalfRate64Ops,
   FeatureLDSBankCount32]>;

def FeatureISAVersion6_0_1 : FeatureSet<
  [FeatureSouthernIslands,
   FeatureLDSBankCount32]>;

def FeatureISAVersion6_0_2 : FeatureSet<
  [FeatureSouthernIslands,
   FeatureLDSBankCount32]>;

def FeatureISAVersion7_0_0 : FeatureSet<
  [FeatureSeaIslands,
   FeatureLDSBankCount32]>;

def FeatureISAVersion7_0_1 : FeatureSet<
  [FeatureSeaIslands,
   HalfRate64Ops,
   FeatureLDSBankCount32,
   FeatureFastFMAF32]>;

def FeatureISAVersion7_0_2 : FeatureSet<
  [FeatureSeaIslands,
   FeatureLDSBankCount16,
   FeatureFastFMAF32]>;

def FeatureISAVersion7_0_3 : FeatureSet<
  [FeatureSeaIslands,
   FeatureLDSBankCount16]>;

def FeatureISAVersion7_0_4 : FeatureSet<
  [FeatureSeaIslands,
   FeatureLDSBankCount32]>;

def FeatureISAVersion7_0_5 : FeatureSet<
  [FeatureSeaIslands,
   FeatureLDSBankCount16]>;

def FeatureISAVersion8_0_Common : FeatureSet<
  [FeatureVolcanicIslands,
   FeatureLDSBankCount32,
   FeatureUnpackedD16VMem]>;

def FeatureISAVersion8_0_1 : FeatureSet<
  !listconcat(FeatureISAVersion8_0_Common.Features,
    [FeatureFastFMAF32,
     HalfRate64Ops,
     FeatureSupportsXNACK])>;

def FeatureISAVersion8_0_2 : FeatureSet<
  !listconcat(FeatureISAVersion8_0_Common.Features,
    [FeatureSGPRInitBug])>;

def FeatureISAVersion8_0_3 : FeatureSet<
  !listconcat(FeatureISAVersion8_0_Common.Features,
    [])>;

def FeatureISAVersion8_0_5 : FeatureSet<
  !listconcat(FeatureISAVersion8_0_Common.Features,
    [FeatureSGPRInitBug])>;

def FeatureISAVersion8_1_0 : FeatureSet<
  [FeatureVolcanicIslands,
   FeatureLDSBankCount16,
   FeatureSupportsXNACK,
   FeatureImageStoreD16Bug,
   FeatureImageGather4D16Bug]>;

def FeatureISAVersion9_0_Common : FeatureSet<
  [FeatureGFX9,
   FeatureLDSBankCount32,
   FeatureImageInsts,
   FeatureMadMacF32Insts]>;

def FeatureISAVersion9_0_Consumer_Common : FeatureSet<
  !listconcat(FeatureISAVersion9_0_Common.Features,
    [FeatureImageGather4D16Bug,
     FeatureDsSrc2Insts,
     FeatureExtendedImageInsts,
     FeatureGDS])>;

def FeatureISAVersion9_Generic : FeatureSet<
  !listconcat(FeatureISAVersion9_0_Consumer_Common.Features,
    [FeatureRequiresCOV6])>;

def FeatureISAVersion9_0_MI_Common : FeatureSet<
  !listconcat(FeatureISAVersion9_0_Common.Features,
    [FeatureFmaMixInsts,
     FeatureDLInsts,
     FeatureDot1Insts,
     FeatureDot2Insts,
     FeatureDot3Insts,
     FeatureDot4Insts,
     FeatureDot5Insts,
     FeatureDot6Insts,
     FeatureDot7Insts,
     FeatureDot10Insts,
     FeatureMAIInsts,
     FeaturePkFmacF16Inst,
     FeatureAtomicFaddNoRtnInsts,
     FeatureSupportsSRAMECC])>;

def FeatureISAVersion9_0_0 : FeatureSet<
  !listconcat(FeatureISAVersion9_0_Consumer_Common.Features,
    [FeatureMadMixInsts])>;

def FeatureISAVersion9_0_2 : FeatureSet<
  !listconcat(FeatureISAVersion9_0_Consumer_Common.Features,
    [FeatureMadMixInsts])>;

def FeatureISAVersion9_0_4 : FeatureSet<
  !listconcat(FeatureISAVersion9_0_Consumer_Common.Features,
    [FeatureFmaMixInsts])>;

def FeatureISAVersion9_0_6 : FeatureSet<
  !listconcat(FeatureISAVersion9_0_Consumer_Common.Features,
    [HalfRate64Ops,
     FeatureFmaMixInsts,
     FeatureDLInsts,
     FeatureDot1Insts,
     FeatureDot2Insts,
     FeatureDot7Insts,
     FeatureDot10Insts,
     FeatureSupportsSRAMECC])>;

def FeatureISAVersion9_0_8 : FeatureSet<
  !listconcat(FeatureISAVersion9_0_MI_Common.Features,
    [FeatureGDS,
     HalfRate64Ops,
     FeatureDsSrc2Insts,
     FeatureExtendedImageInsts,
     FeatureAtomicBufferGlobalPkAddF16NoRtnInsts,
     FeatureMFMAInlineLiteralBug,
     FeatureImageGather4D16Bug])>;

def FeatureISAVersion9_0_9 : FeatureSet<
  !listconcat(FeatureISAVersion9_0_Consumer_Common.Features,
    [FeatureMadMixInsts,
     FeatureImageInsts])>;

def FeatureISAVersion9_0_A : FeatureSet<
  !listconcat(FeatureISAVersion9_0_MI_Common.Features,
    [FeatureGFX90AInsts,
     FeatureFmacF64Inst,
     FeatureDPALU_DPP,
     FeaturePackedFP32Ops,
     FeatureAtomicFaddRtnInsts,
     FeatureAtomicBufferGlobalPkAddF16Insts,
     FeaturePackedTID,
     FullRate64Ops,
     FeatureBackOffBarrier,
     FeatureKernargPreload,
     FeatureAtomicFMinFMaxF64GlobalInsts,
     FeatureAtomicFMinFMaxF64FlatInsts,
     FeatureFlatBufferGlobalAtomicFaddF64Inst
     ])>;

def FeatureISAVersion9_0_C : FeatureSet<
  !listconcat(FeatureISAVersion9_0_Consumer_Common.Features,
    [FeatureMadMixInsts])>;

def FeatureISAVersion9_4_Common : FeatureSet<
  [FeatureGFX9,
   FeatureGFX90AInsts,
   FeatureGFX940Insts,
   FeatureFmaMixInsts,
   FeatureLDSBankCount32,
   FeatureDLInsts,
   FeatureFmacF64Inst,
   FeatureDot1Insts,
   FeatureDot2Insts,
   FeatureDot3Insts,
   FeatureDot4Insts,
   FeatureDot5Insts,
   FeatureDot6Insts,
   FeatureDot7Insts,
   FeatureDot10Insts,
   FeatureAtomicDsPkAdd16Insts,
   FeatureAtomicFlatPkAdd16Insts,
   FeatureDPALU_DPP,
   FeaturePackedFP32Ops,
   FeatureMAIInsts,
   FeatureFP8Insts,
   FeatureFP8ConversionInsts,
   FeaturePkFmacF16Inst,
   FeatureAtomicFaddRtnInsts,
   FeatureAtomicFaddNoRtnInsts,
   FeatureAtomicBufferGlobalPkAddF16Insts,
   FeatureAtomicGlobalPkAddBF16Inst,
   FeatureFlatAtomicFaddF32Inst,
   FeatureSupportsSRAMECC,
   FeaturePackedTID,
   FeatureArchitectedFlatScratch,
   FullRate64Ops,
   FeatureBackOffBarrier,
   FeatureKernargPreload,
   FeatureAtomicFMinFMaxF64GlobalInsts,
   FeatureAtomicFMinFMaxF64FlatInsts,
   FeatureAgentScopeFineGrainedRemoteMemoryAtomics,
   FeatureMemoryAtomicFAddF32DenormalSupport,
   FeatureFlatBufferGlobalAtomicFaddF64Inst
   ]>;

def FeatureISAVersion9_4_0 : FeatureSet<
  !listconcat(FeatureISAVersion9_4_Common.Features,
    [FeatureForceStoreSC0SC1])>;

def FeatureISAVersion9_4_1 : FeatureSet<
  !listconcat(FeatureISAVersion9_4_Common.Features,
    [FeatureForceStoreSC0SC1])>;

def FeatureISAVersion9_4_2 : FeatureSet<
  !listconcat(FeatureISAVersion9_4_Common.Features,
    [])>;

def FeatureISAVersion10_Common : FeatureSet<
  [FeatureGFX10,
   FeatureLDSBankCount32,
   FeatureDLInsts,
   FeatureNSAEncoding,
   FeatureBackOffBarrier]>;

def FeatureISAVersion10_1_Common : FeatureSet<
  !listconcat(FeatureISAVersion10_Common.Features,
    [FeatureScalarStores,
     FeatureScalarAtomics,
     FeatureScalarFlatScratchInsts,
     FeatureGetWaveIdInst,
     FeatureMadMacF32Insts,
     FeatureDsSrc2Insts,
     FeatureLdsMisalignedBug,
     FeatureSupportsXNACK,
     // gfx101x bugs
     FeatureVcmpxPermlaneHazard,
     FeatureVMEMtoScalarWriteHazard,
     FeatureSMEMtoVectorWriteHazard,
     FeatureInstFwdPrefetchBug,
     FeatureVcmpxExecWARHazard,
     FeatureLdsBranchVmemWARHazard,
     FeatureNSAtoVMEMBug,
     FeatureNSAClauseBug,
     FeatureOffset3fBug,
     FeatureFlatSegmentOffsetBug,
     FeatureNegativeUnalignedScratchOffsetBug])>;

def FeatureISAVersion10_1_Generic : FeatureSet<
  !listconcat(FeatureISAVersion10_1_Common.Features,
    [FeatureRequiresCOV6])>;

def FeatureISAVersion10_1_0 : FeatureSet<
  !listconcat(FeatureISAVersion10_1_Common.Features,
    [])>;

def FeatureISAVersion10_1_1 : FeatureSet<
  !listconcat(FeatureISAVersion10_1_Common.Features,
    [FeatureDot1Insts,
     FeatureDot2Insts,
     FeatureDot5Insts,
     FeatureDot6Insts,
     FeatureDot7Insts,
     FeatureDot10Insts])>;

def FeatureISAVersion10_1_2 : FeatureSet<
  !listconcat(FeatureISAVersion10_1_Common.Features,
    [FeatureDot1Insts,
     FeatureDot2Insts,
     FeatureDot5Insts,
     FeatureDot6Insts,
     FeatureDot7Insts,
     FeatureDot10Insts])>;

def FeatureISAVersion10_1_3 : FeatureSet<
  !listconcat(FeatureISAVersion10_1_Common.Features,
    [FeatureGFX10_AEncoding])>;

def FeatureISAVersion10_3_0 : FeatureSet<
  !listconcat(FeatureISAVersion10_Common.Features,
    [FeatureGFX10_AEncoding,
     FeatureGFX10_BEncoding,
     FeatureGFX10_3Insts,
     FeatureDot1Insts,
     FeatureDot2Insts,
     FeatureDot5Insts,
     FeatureDot6Insts,
     FeatureDot7Insts,
     FeatureDot10Insts,
     FeatureShaderCyclesRegister])>;

def FeatureISAVersion10_3_Generic: FeatureSet<
  !listconcat(FeatureISAVersion10_3_0.Features,
    [FeatureRequiresCOV6])>;

def FeatureISAVersion11_Common : FeatureSet<
  [FeatureGFX11,
   FeatureLDSBankCount32,
   FeatureDLInsts,
   FeatureDot5Insts,
   FeatureDot7Insts,
   FeatureDot8Insts,
   FeatureDot9Insts,
   FeatureDot10Insts,
   FeatureNSAEncoding,
   FeaturePartialNSAEncoding,
   FeatureShaderCyclesRegister,
   FeatureArchitectedFlatScratch,
   FeatureAtomicFaddRtnInsts,
   FeatureAtomicFaddNoRtnInsts,
   FeatureFlatAtomicFaddF32Inst,
   FeatureImageInsts,
   FeaturePackedTID,
   FeatureVcmpxPermlaneHazard,
   FeatureMemoryAtomicFAddF32DenormalSupport]>;

// There are few workarounds that need to be
// added to all targets. This pessimizes codegen
// a bit on the generic GFX11 target.
def FeatureISAVersion11_Generic: FeatureSet<
  !listconcat(FeatureISAVersion11_Common.Features,
    [FeatureMSAALoadDstSelBug,
     FeatureVALUTransUseHazard,
     FeatureUserSGPRInit16Bug,
     FeatureMADIntraFwdBug,
     FeaturePrivEnabledTrap2NopBug,
     FeatureRequiresCOV6,
     FeatureRequiredExportPriority])>;

def FeatureISAVersion11_0_Common : FeatureSet<
  !listconcat(FeatureISAVersion11_Common.Features,
    [FeatureMSAALoadDstSelBug,
     FeatureVALUTransUseHazard,
     FeatureMADIntraFwdBug,
     FeaturePrivEnabledTrap2NopBug])>;

def FeatureISAVersion11_0_0 : FeatureSet<
  !listconcat(FeatureISAVersion11_0_Common.Features,
    [Feature1_5xVGPRs,
     FeatureUserSGPRInit16Bug])>;

def FeatureISAVersion11_0_1 : FeatureSet<
  !listconcat(FeatureISAVersion11_0_Common.Features,
    [Feature1_5xVGPRs])>;

def FeatureISAVersion11_0_2 : FeatureSet<
  !listconcat(FeatureISAVersion11_0_Common.Features,
    [FeatureUserSGPRInit16Bug])>;

def FeatureISAVersion11_0_3 : FeatureSet<
  !listconcat(FeatureISAVersion11_0_Common.Features,
    [])>;

def FeatureISAVersion11_5_0 : FeatureSet<
  !listconcat(FeatureISAVersion11_Common.Features,
    [FeatureSALUFloatInsts,
     FeatureDPPSrc1SGPR,
     FeatureVGPRSingleUseHintInsts,
     FeatureRequiredExportPriority])>;

def FeatureISAVersion11_5_1 : FeatureSet<
  !listconcat(FeatureISAVersion11_Common.Features,
    [FeatureSALUFloatInsts,
     FeatureDPPSrc1SGPR,
     FeatureVGPRSingleUseHintInsts,
     Feature1_5xVGPRs,
     FeatureRequiredExportPriority])>;

def FeatureISAVersion11_5_2 : FeatureSet<
  !listconcat(FeatureISAVersion11_Common.Features,
    [FeatureSALUFloatInsts,
     FeatureDPPSrc1SGPR,
     FeatureVGPRSingleUseHintInsts,
     FeatureRequiredExportPriority])>;

def FeatureISAVersion12 : FeatureSet<
  [FeatureGFX12,
   FeatureLDSBankCount32,
   FeatureDLInsts,
   FeatureDot7Insts,
   FeatureDot8Insts,
   FeatureDot9Insts,
   FeatureDot10Insts,
   FeatureDot11Insts,
   FeatureNSAEncoding,
   FeaturePartialNSAEncoding,
   FeatureShaderCyclesHiLoRegisters,
   FeatureArchitectedFlatScratch,
   FeatureArchitectedSGPRs,
   FeatureAtomicFaddRtnInsts,
   FeatureAtomicFaddNoRtnInsts,
   FeatureAtomicDsPkAdd16Insts,
   FeatureAtomicFlatPkAdd16Insts,
   FeatureAtomicBufferGlobalPkAddF16Insts,
   FeatureAtomicGlobalPkAddBF16Inst,
   FeatureAtomicBufferPkAddBF16Inst,
   FeatureFlatAtomicFaddF32Inst,
   FeatureImageInsts,
   FeatureExtendedImageInsts,
   FeatureFP8ConversionInsts,
   FeaturePackedTID,
   FeatureVcmpxPermlaneHazard,
   FeatureSALUFloatInsts,
   FeaturePseudoScalarTrans,
   FeatureHasRestrictedSOffset,
   FeatureVGPRSingleUseHintInsts,
   FeatureScalarDwordx3Loads,
   FeatureDPPSrc1SGPR,
   FeatureMaxHardClauseLength32,
   Feature1_5xVGPRs,
   FeatureMemoryAtomicFAddF32DenormalSupport
   ]>;

def FeatureISAVersion12_Generic: FeatureSet<
  !listconcat(FeatureISAVersion12.Features,
    [FeatureRequiresCOV6])>;

//===----------------------------------------------------------------------===//

def AMDGPUInstrInfo : InstrInfo {
  let guessInstructionProperties = 1;
}

def AMDGPUAsmParser : AsmParser {
  // Some of the R600 registers have the same name, so this crashes.
  // For example T0_XYZW and T0_XY both have the asm name T0.
  let ShouldEmitMatchRegisterName = 0;

  // Call the custom operand parser for all operands.
  let OperandParserMethod = "parseCustomOperand";
  let CallCustomParserForAllOperands = true;
}

def AMDGPUAsmWriter : AsmWriter {
  int PassSubtarget = 1;
}

def AMDGPUAsmVariants {
  string Default = "Default";
  int Default_ID = 0;
  string VOP3 = "VOP3";
  int VOP3_ID = 1;
  string SDWA = "SDWA";
  int SDWA_ID = 2;
  string SDWA9 = "SDWA9";
  int SDWA9_ID = 3;
  string DPP = "DPP";
  int DPP_ID = 4;
  string VOP3_DPP = "VOP3_DPP";
  int VOP3_DPP_ID = 5;
  string Disable = "Disable";
  int Disable_ID = 6;
}

def DefaultAMDGPUAsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.Default_ID;
  let Name = AMDGPUAsmVariants.Default;
}

def VOP3AsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.VOP3_ID;
  let Name = AMDGPUAsmVariants.VOP3;
}

def SDWAAsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.SDWA_ID;
  let Name = AMDGPUAsmVariants.SDWA;
}

def SDWA9AsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.SDWA9_ID;
  let Name = AMDGPUAsmVariants.SDWA9;
}

def DPPAsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.DPP_ID;
  let Name = AMDGPUAsmVariants.DPP;
}

def VOP3_DPPAsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.VOP3_DPP_ID;
  let Name = AMDGPUAsmVariants.VOP3_DPP;
}

def AMDGPU : Target {
  // Pull in Instruction Info:
  let InstructionSet = AMDGPUInstrInfo;
  let AssemblyParsers = [AMDGPUAsmParser];
  let AssemblyParserVariants = [DefaultAMDGPUAsmParserVariant,
                                VOP3AsmParserVariant,
                                SDWAAsmParserVariant,
                                SDWA9AsmParserVariant,
                                DPPAsmParserVariant,
                                VOP3_DPPAsmParserVariant];
  let AssemblyWriters = [AMDGPUAsmWriter];
  let AllowRegisterRenaming = 1;
}

// Dummy Instruction itineraries for pseudo instructions
def ALU_NULL : FuncUnit;
def NullALU : InstrItinClass;

//===----------------------------------------------------------------------===//
// Predicate helper class
//===----------------------------------------------------------------------===//

def isGFX6 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SOUTHERN_ISLANDS">,
  AssemblerPredicate<(all_of FeatureSouthernIslands)>;

def isGFX6GFX7 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SOUTHERN_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS">,
  AssemblerPredicate<(all_of (not FeatureGCN3Encoding), (not FeatureGFX10Insts))>;

def isGFX6GFX7GFX10 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SOUTHERN_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX10">,
  AssemblerPredicate<(all_of (not FeatureGCN3Encoding), (not FeatureGFX11Insts))>;

def isGFX6GFX7GFX10Plus :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SOUTHERN_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS ||"
            "Subtarget->getGeneration() >= AMDGPUSubtarget::GFX10">,
  AssemblerPredicate<(all_of (not FeatureGCN3Encoding))>;

def isGFX7Only :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS">,
  AssemblerPredicate<(all_of (not FeatureGCN3Encoding), FeatureCIInsts, (not FeatureGFX10Insts))>;

def isGFX7GFX10 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX10">,
  AssemblerPredicate<(all_of (not FeatureGCN3Encoding), FeatureCIInsts, (not FeatureGFX11Insts))>;

def isGFX7GFX10GFX11 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX10 ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX11">,
  AssemblerPredicate<(all_of (not FeatureGCN3Encoding), FeatureCIInsts)>;

def isGFX7GFX8GFX9 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX9">,
  AssemblerPredicate<(all_of FeatureGFX7GFX8GFX9Insts)>;

def isGFX6GFX7GFX8GFX9 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SOUTHERN_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX9">,
  AssemblerPredicate<(all_of (not FeatureGFX10Insts))>;

def isGFX6GFX7GFX8GFX9NotGFX90A :
  Predicate<"!Subtarget->hasGFX90AInsts() &&"
            "(Subtarget->getGeneration() == AMDGPUSubtarget::SOUTHERN_ISLANDS ||"
            " Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS ||"
            " Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS ||"
            " Subtarget->getGeneration() == AMDGPUSubtarget::GFX9)">,
  AssemblerPredicate<(all_of (not FeatureGFX10Insts), (not FeatureGFX90AInsts))>;

def isGFX6GFX7GFX8GFX9GFX10 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SOUTHERN_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX9 ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX10">,
  AssemblerPredicate<(all_of (not FeatureGFX11Insts))>;

def isNotGFX12Plus :
  Predicate<"Subtarget->getGeneration() <= AMDGPUSubtarget::GFX11">,
  AssemblerPredicate<(all_of (not FeatureGFX12Insts))>;

def isGFX7GFX8GFX9GFX10 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX9 ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX10">,
  AssemblerPredicate<(all_of FeatureCIInsts, (not FeatureGFX11Insts))>;

def isGFX8GFX9GFX10GFX11 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX9 ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX10 ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX11">,
  AssemblerPredicate<(all_of FeatureGFX8Insts, (not FeatureGFX12Insts))>;

def isGFX7Plus :
  Predicate<"Subtarget->getGeneration() >= AMDGPUSubtarget::SEA_ISLANDS">,
  AssemblerPredicate<(all_of FeatureCIInsts)>;

def isGFX8Plus :
  Predicate<"Subtarget->getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS">,
  AssemblerPredicate<(all_of FeatureGFX8Insts)>;

def isGFX8Only : Predicate<"Subtarget->getGeneration() =="
                           "AMDGPUSubtarget::VOLCANIC_ISLANDS">,
  AssemblerPredicate <(all_of FeatureVolcanicIslands)>;

def isGFX9Plus :
  Predicate<"Subtarget->getGeneration() >= AMDGPUSubtarget::GFX9">,
  AssemblerPredicate<(all_of FeatureGFX9Insts)>;

def isNotGFX9Plus :
  Predicate<"Subtarget->getGeneration() < AMDGPUSubtarget::GFX9">;

def isGFX9Only : Predicate <
  "Subtarget->getGeneration() == AMDGPUSubtarget::GFX9">,
  AssemblerPredicate<(all_of FeatureGCN3Encoding, FeatureGFX9Insts)>;

def isGCN3ExcludingGFX90A :
  Predicate<"Subtarget->isGCN3Encoding() && !Subtarget->hasGFX90AInsts()">,
  AssemblerPredicate<(all_of FeatureGCN3Encoding, (not FeatureGFX90AInsts))>;

def isGFX90APlus :
  Predicate<"Subtarget->hasGFX90AInsts()">,
  AssemblerPredicate<(all_of FeatureGFX90AInsts)>;

def isNotGFX90APlus :
  Predicate<"!Subtarget->hasGFX90AInsts()">,
  AssemblerPredicate<(all_of (not FeatureGFX90AInsts))>;

def isGFX8GFX9NotGFX90A :
  Predicate<"!Subtarget->hasGFX90AInsts() &&"
            "(Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS ||"
            " Subtarget->getGeneration() == AMDGPUSubtarget::GFX9)">,
  AssemblerPredicate<(all_of FeatureGFX8Insts, FeatureGCN3Encoding, (not FeatureGFX90AInsts))>;

def isGFX90AOnly :
  Predicate<"Subtarget->hasGFX90AInsts() && !Subtarget->hasGFX940Insts()">,
  AssemblerPredicate<(all_of FeatureGFX90AInsts, (not FeatureGFX940Insts))>;

def isGFX908orGFX90A :
  Predicate<"Subtarget->hasMAIInsts() && !Subtarget->hasGFX940Insts()">,
  AssemblerPredicate<(all_of FeatureMAIInsts, (not FeatureGFX940Insts))>;

def isGFX940Plus :
  Predicate<"Subtarget->hasGFX940Insts()">,
  AssemblerPredicate<(all_of FeatureGFX940Insts)>;

def isGFX8GFX9NotGFX940 :
  Predicate<"!Subtarget->hasGFX940Insts() &&"
            "(Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS ||"
            " Subtarget->getGeneration() == AMDGPUSubtarget::GFX9)">,
  AssemblerPredicate<(all_of FeatureGFX8Insts, FeatureGCN3Encoding, (not FeatureGFX940Insts))>;

def isGFX8GFX9 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX9">,
  AssemblerPredicate<(all_of FeatureGFX8Insts, FeatureGCN3Encoding)>;

def isGFX10Only :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::GFX10">,
  AssemblerPredicate<(all_of FeatureGFX10Insts, (not FeatureGFX11Insts))>;

def isGFX10Plus :
  Predicate<"Subtarget->getGeneration() >= AMDGPUSubtarget::GFX10">,
  AssemblerPredicate<(all_of FeatureGFX10Insts)>;

def isGFX10GFX11 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::GFX10 ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX11">,
  AssemblerPredicate<(all_of FeatureGFX10Insts, (not FeatureGFX12Insts))>;

def isGFX10Before1030 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::GFX10 &&"
            "!Subtarget->hasGFX10_3Insts()">,
  AssemblerPredicate<(all_of FeatureGFX10Insts,(not FeatureGFX10_3Insts))>;

def isGFX9GFX10 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::GFX9 ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX10">,
  AssemblerPredicate<(all_of FeatureGFX9Insts, (not FeatureGFX11Insts))>;

def isGFX8GFX9GFX10 :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX9 ||"
            "Subtarget->getGeneration() == AMDGPUSubtarget::GFX10">,
  AssemblerPredicate<(all_of FeatureGFX8Insts, (not FeatureGFX11Insts))>;

def isGFX11Only :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::GFX11">,
  AssemblerPredicate<(all_of FeatureGFX11Insts, (not FeatureGFX12Insts))>;

def isGFX11Plus :
  Predicate<"Subtarget->getGeneration() >= AMDGPUSubtarget::GFX11">,
  AssemblerPredicate<(all_of FeatureGFX11Insts)>;

def isGFX12Only :
  Predicate<"Subtarget->getGeneration() == AMDGPUSubtarget::GFX12">,
  AssemblerPredicate<(all_of FeatureGFX12Insts)>;

def isGFX12Plus :
  Predicate<"Subtarget->getGeneration() >= AMDGPUSubtarget::GFX12">,
  AssemblerPredicate<(all_of FeatureGFX12Insts)>;

def HasFlatAddressSpace : Predicate<"Subtarget->hasFlatAddressSpace()">,
  AssemblerPredicate<(all_of FeatureFlatAddressSpace)>;

def HasFlatBufferGlobalAtomicFaddF64Inst :
  Predicate<"Subtarget->hasFlatBufferGlobalAtomicFaddF64Inst()">,
  AssemblerPredicate<(any_of FeatureFlatBufferGlobalAtomicFaddF64Inst)>;

def HasAtomicFMinFMaxF32GlobalInsts :
  Predicate<"Subtarget->hasAtomicFMinFMaxF32GlobalInsts()">,
  AssemblerPredicate<(any_of FeatureAtomicFMinFMaxF32GlobalInsts)>;

def HasAtomicFMinFMaxF64GlobalInsts :
  Predicate<"Subtarget->hasAtomicFMinFMaxF64GlobalInsts()">,
  AssemblerPredicate<(any_of FeatureAtomicFMinFMaxF64GlobalInsts)>;

def HasAtomicFMinFMaxF32FlatInsts :
  Predicate<"Subtarget->hasAtomicFMinFMaxF32FlatInsts()">,
  AssemblerPredicate<(any_of FeatureAtomicFMinFMaxF32FlatInsts)>;

def HasAtomicFMinFMaxF64FlatInsts :
  Predicate<"Subtarget->hasAtomicFMinFMaxF64FlatInsts()">,
  AssemblerPredicate<(any_of FeatureAtomicFMinFMaxF64FlatInsts)>;

def HasLdsAtomicAddF64 :
  Predicate<"Subtarget->hasLdsAtomicAddF64()">,
  AssemblerPredicate<(any_of FeatureGFX90AInsts)>;

def HasFlatGlobalInsts : Predicate<"Subtarget->hasFlatGlobalInsts()">,
  AssemblerPredicate<(all_of FeatureFlatGlobalInsts)>;
def HasFlatScratchInsts : Predicate<"Subtarget->hasFlatScratchInsts()">,
  AssemblerPredicate<(all_of FeatureFlatScratchInsts)>;
def HasScalarFlatScratchInsts : Predicate<"Subtarget->hasScalarFlatScratchInsts()">,
  AssemblerPredicate<(all_of FeatureScalarFlatScratchInsts)>;
def HasD16LoadStore : Predicate<"Subtarget->hasD16LoadStore()">,
  AssemblerPredicate<(all_of FeatureGFX9Insts)>;

def HasFlatScratchSTMode : Predicate<"Subtarget->hasFlatScratchSTMode()">,
  AssemblerPredicate<(any_of FeatureGFX10_3Insts, FeatureGFX940Insts)>;
def HasFlatScratchSVSMode : Predicate<"Subtarget->hasFlatScratchSVSMode()">,
  AssemblerPredicate<(any_of FeatureGFX940Insts, FeatureGFX11Insts)>;

def HasGFX10_AEncoding : Predicate<"Subtarget->hasGFX10_AEncoding()">,
  AssemblerPredicate<(all_of FeatureGFX10_AEncoding)>;

def HasGFX10_BEncoding : Predicate<"Subtarget->hasGFX10_BEncoding()">,
  AssemblerPredicate<(all_of FeatureGFX10_BEncoding)>;

def HasUnpackedD16VMem : Predicate<"Subtarget->hasUnpackedD16VMem()">,
  AssemblerPredicate<(all_of FeatureUnpackedD16VMem)>;
def HasPackedD16VMem : Predicate<"!Subtarget->hasUnpackedD16VMem()">,
  AssemblerPredicate<(all_of (not FeatureUnpackedD16VMem))>;

def HasRestrictedSOffset : Predicate<"Subtarget->hasRestrictedSOffset()">,
  AssemblerPredicate<(all_of FeatureHasRestrictedSOffset)>;
def HasUnrestrictedSOffset : Predicate<"!Subtarget->hasRestrictedSOffset()">,
  AssemblerPredicate<(all_of (not FeatureHasRestrictedSOffset))>;

def D16PreservesUnusedBits :
  Predicate<"Subtarget->d16PreservesUnusedBits()">,
  AssemblerPredicate<(all_of FeatureGFX9Insts, (not FeatureSRAMECC))>;

def LDSRequiresM0Init : Predicate<"Subtarget->ldsRequiresM0Init()">;
def NotLDSRequiresM0Init : Predicate<"!Subtarget->ldsRequiresM0Init()">;

def HasExportInsts : Predicate<"Subtarget->hasExportInsts()">,
  AssemblerPredicate<(all_of (not FeatureGFX90AInsts))>;

def HasVINTERPEncoding : Predicate<"Subtarget->hasVINTERPEncoding()">,
  AssemblerPredicate<(all_of FeatureGFX11Insts)>;

def HasDSAddTid : Predicate<"Subtarget->getGeneration() >= AMDGPUSubtarget::GFX9">,
  AssemblerPredicate<(all_of FeatureGFX9Insts)>;

def HasLDSFPAtomicAddF32 : Predicate<"Subtarget->hasLDSFPAtomicAddF32()">,
  AssemblerPredicate<(all_of FeatureGFX8Insts)>;

def HasAddNoCarryInsts : Predicate<"Subtarget->hasAddNoCarry()">,
  AssemblerPredicate<(all_of FeatureAddNoCarryInsts)>;

def NotHasAddNoCarryInsts : Predicate<"!Subtarget->hasAddNoCarry()">;

def HasXNACKEnabled : Predicate<"Subtarget->isXNACKEnabled()">;

def Has16BitInsts : Predicate<"Subtarget->has16BitInsts()">,
  AssemblerPredicate<(all_of Feature16BitInsts)>;

def HasTrue16BitInsts : Predicate<"Subtarget->hasTrue16BitInsts()">,
  AssemblerPredicate<(all_of FeatureTrue16BitInsts)>;
def NotHasTrue16BitInsts : True16PredicateClass<"!Subtarget->hasTrue16BitInsts()">,
  AssemblerPredicate<(all_of (not FeatureTrue16BitInsts))>;

// Control use of True16 instructions. The real True16 instructions are
// True16 instructions as they are defined in the ISA. Fake True16
// instructions have the same encoding as real ones but syntactically
// only allow 32-bit registers in operands and use low halves thereof.
def UseRealTrue16Insts : True16PredicateClass<"Subtarget->useRealTrue16Insts()">,
  AssemblerPredicate<(all_of FeatureTrue16BitInsts, FeatureRealTrue16Insts)>;
def UseFakeTrue16Insts : True16PredicateClass<"Subtarget->hasTrue16BitInsts() && "
                                              "!Subtarget->useRealTrue16Insts()">,
  AssemblerPredicate<(all_of FeatureTrue16BitInsts)>;
  // FIXME When we default to RealTrue16 instead of Fake, change the line as follows.
  // AssemblerPredicate<(all_of FeatureTrue16BitInsts, (not FeatureRealTrue16Insts))>;

def HasVOP3PInsts : Predicate<"Subtarget->hasVOP3PInsts()">,
  AssemblerPredicate<(all_of FeatureVOP3P)>;

def NotHasMed3_16 : Predicate<"!Subtarget->hasMed3_16()">;
def HasMed3_16 : Predicate<"Subtarget->hasMed3_16()">;

def HasMinMaxDenormModes : Predicate<"Subtarget->supportsMinMaxDenormModes()">;
def NotHasMinMaxDenormModes : Predicate<"!Subtarget->supportsMinMaxDenormModes()">;

def HasFminFmaxLegacy : Predicate<"Subtarget->hasFminFmaxLegacy()">;

def HasSDWA : Predicate<"Subtarget->hasSDWA()">,
  AssemblerPredicate<(all_of FeatureSDWA, FeatureVolcanicIslands)>;

def HasSDWA9 :
  Predicate<"Subtarget->hasSDWA()">,
  AssemblerPredicate<(all_of FeatureGCN3Encoding, FeatureGFX9Insts,FeatureSDWA)>;

def HasSDWA10 :
  Predicate<"Subtarget->hasSDWA()">,
  AssemblerPredicate<(all_of (not FeatureGCN3Encoding), FeatureGFX10Insts, FeatureSDWA)>;

def HasDPP : Predicate<"Subtarget->hasDPP()">,
  AssemblerPredicate<(all_of FeatureGCN3Encoding, FeatureDPP)>;

def HasDPP8 : Predicate<"Subtarget->hasDPP8()">,
  AssemblerPredicate<(all_of (not FeatureGCN3Encoding), FeatureGFX10Insts, FeatureDPP8)>;

def HasDPALU_DPP : Predicate<"Subtarget->hasDPALU_DPP()">,
  AssemblerPredicate<(all_of FeatureDPALU_DPP)>;

def HasPackedFP32Ops : Predicate<"Subtarget->hasPackedFP32Ops()">,
  AssemblerPredicate<(all_of FeaturePackedFP32Ops)>;

def HasPkMovB32 : Predicate<"Subtarget->hasPkMovB32()">,
  AssemblerPredicate<(all_of FeatureGFX90AInsts)>;

def HasFmaakFmamkF32Insts :
  Predicate<"Subtarget->hasFmaakFmamkF32Insts()">,
  AssemblerPredicate<(any_of FeatureGFX10Insts, FeatureGFX940Insts)>;

def HasImageInsts : Predicate<"Subtarget->hasImageInsts()">,
  AssemblerPredicate<(all_of FeatureImageInsts)>;

def HasExtendedImageInsts : Predicate<"Subtarget->hasExtendedImageInsts()">,
  AssemblerPredicate<(all_of FeatureExtendedImageInsts)>;

def HasR128A16 : Predicate<"Subtarget->hasR128A16()">,
  AssemblerPredicate<(all_of FeatureR128A16)>;

def HasA16 : Predicate<"Subtarget->hasA16()">,
  AssemblerPredicate<(all_of FeatureA16)>;

def HasG16 : Predicate<"Subtarget->hasG16()">,
  AssemblerPredicate<(all_of FeatureG16)>;

def HasDPP16 : Predicate<"Subtarget->hasDPP()">,
  AssemblerPredicate<(all_of (not FeatureGCN3Encoding), FeatureGFX10Insts, FeatureDPP)>;

def HasIntClamp : Predicate<"Subtarget->hasIntClamp()">,
  AssemblerPredicate<(all_of FeatureIntClamp)>;

def HasMadMixInsts : Predicate<"Subtarget->hasMadMixInsts()">,
  AssemblerPredicate<(all_of FeatureMadMixInsts)>;

def HasScalarStores : Predicate<"Subtarget->hasScalarStores()">,
  AssemblerPredicate<(all_of FeatureScalarStores)>;

def HasScalarAtomics : Predicate<"Subtarget->hasScalarAtomics()">,
  AssemblerPredicate<(all_of FeatureScalarAtomics)>;

def HasNoSdstCMPX : Predicate<"Subtarget->hasNoSdstCMPX()">,
  AssemblerPredicate<(all_of FeatureNoSdstCMPX)>;

def HasSdstCMPX : Predicate<"!Subtarget->hasNoSdstCMPX()">,
  AssemblerPredicate<(all_of (not FeatureNoSdstCMPX))>;

def has16BankLDS : Predicate<"Subtarget->getLDSBankCount() == 16">;
def has32BankLDS : Predicate<"Subtarget->getLDSBankCount() == 32">;
def HasVGPRIndexMode : Predicate<"Subtarget->hasVGPRIndexMode()">,
                      AssemblerPredicate<(all_of FeatureVGPRIndexMode)>;
def HasMovrel : Predicate<"Subtarget->hasMovrel()">,
                AssemblerPredicate<(all_of FeatureMovrel)>;

def HasFmaMixInsts : Predicate<"Subtarget->hasFmaMixInsts()">,
  AssemblerPredicate<(all_of FeatureFmaMixInsts)>;

def HasDLInsts : Predicate<"Subtarget->hasDLInsts()">,
  AssemblerPredicate<(all_of FeatureDLInsts)>;

def HasFmacF64Inst : Predicate<"Subtarget->hasFmacF64Inst()">,
  AssemblerPredicate<(all_of FeatureFmacF64Inst)>;

def HasDot1Insts : Predicate<"Subtarget->hasDot1Insts()">,
  AssemblerPredicate<(all_of FeatureDot1Insts)>;

def HasDot2Insts : Predicate<"Subtarget->hasDot2Insts()">,
  AssemblerPredicate<(all_of FeatureDot2Insts)>;

def HasDot3Insts : Predicate<"Subtarget->hasDot3Insts()">,
  AssemblerPredicate<(all_of FeatureDot3Insts)>;

def HasDot4Insts : Predicate<"Subtarget->hasDot4Insts()">,
  AssemblerPredicate<(all_of FeatureDot4Insts)>;

def HasDot5Insts : Predicate<"Subtarget->hasDot5Insts()">,
  AssemblerPredicate<(all_of FeatureDot5Insts)>;

def HasDot6Insts : Predicate<"Subtarget->hasDot6Insts()">,
  AssemblerPredicate<(all_of FeatureDot6Insts)>;

def HasDot7Insts : Predicate<"Subtarget->hasDot7Insts()">,
  AssemblerPredicate<(all_of FeatureDot7Insts)>;

def HasDot8Insts : Predicate<"Subtarget->hasDot8Insts()">,
  AssemblerPredicate<(all_of FeatureDot8Insts)>;

def HasDot9Insts : Predicate<"Subtarget->hasDot9Insts()">,
  AssemblerPredicate<(all_of FeatureDot9Insts)>;

def HasDot10Insts : Predicate<"Subtarget->hasDot10Insts()">,
  AssemblerPredicate<(all_of FeatureDot10Insts)>;

def HasDot11Insts : Predicate<"Subtarget->hasDot11Insts()">,
  AssemblerPredicate<(all_of FeatureDot11Insts)>;

def HasGetWaveIdInst : Predicate<"Subtarget->hasGetWaveIdInst()">,
  AssemblerPredicate<(all_of FeatureGetWaveIdInst)>;

def HasMAIInsts : Predicate<"Subtarget->hasMAIInsts()">,
  AssemblerPredicate<(all_of FeatureMAIInsts)>;

def HasSMemRealTime : Predicate<"Subtarget->hasSMemRealTime()">,
  AssemblerPredicate<(all_of FeatureSMemRealTime)>;

def HasSMemTimeInst : Predicate<"Subtarget->hasSMemTimeInst()">,
  AssemblerPredicate<(all_of FeatureSMemTimeInst)>;

def HasShaderCyclesRegister : Predicate<"Subtarget->hasShaderCyclesRegister()">,
  AssemblerPredicate<(all_of FeatureShaderCyclesRegister)>;

def HasShaderCyclesHiLoRegisters : Predicate<"Subtarget->hasShaderCyclesHiLoRegisters()">;

def HasFP8Insts : Predicate<"Subtarget->hasFP8Insts()">,
  AssemblerPredicate<(all_of FeatureFP8Insts)>;

def HasFP8ConversionInsts : Predicate<"Subtarget->hasFP8ConversionInsts()">,
  AssemblerPredicate<(all_of FeatureFP8ConversionInsts)>;

def HasPkFmacF16Inst : Predicate<"Subtarget->hasPkFmacF16Inst()">,
  AssemblerPredicate<(all_of FeaturePkFmacF16Inst)>;

def HasMadMacF32Insts : Predicate<"Subtarget->hasMadMacF32Insts()">,
  AssemblerPredicate<(all_of FeatureMadMacF32Insts)>;

def HasFmaLegacy32 : Predicate<"Subtarget->hasGFX10_3Insts()">,
  AssemblerPredicate<(any_of FeatureGFX10_3Insts)>;

def HasAtomicDsPkAdd16Insts : Predicate<"Subtarget->hasAtomicDsPkAdd16Insts()">,
  AssemblerPredicate<(any_of FeatureAtomicDsPkAdd16Insts)>;

def HasAtomicFlatPkAdd16Insts : Predicate<"Subtarget->hasAtomicFlatPkAdd16Insts()">,
  AssemblerPredicate<(any_of FeatureAtomicFlatPkAdd16Insts)>;

def HasAtomicFaddRtnInsts : Predicate<"Subtarget->hasAtomicFaddRtnInsts()">,
  AssemblerPredicate<(all_of FeatureAtomicFaddRtnInsts)>;
def HasAtomicFaddNoRtnInsts : Predicate<"Subtarget->hasAtomicFaddNoRtnInsts()">,
  AssemblerPredicate<(all_of FeatureAtomicFaddNoRtnInsts)>;
def HasAtomicBufferGlobalPkAddF16NoRtnInsts
  : Predicate<"Subtarget->hasAtomicBufferGlobalPkAddF16NoRtnInsts() || Subtarget->hasAtomicBufferGlobalPkAddF16Insts()">,
  AssemblerPredicate<(any_of FeatureAtomicBufferGlobalPkAddF16NoRtnInsts, FeatureAtomicBufferGlobalPkAddF16Insts)>;
def HasAtomicBufferGlobalPkAddF16Insts
  : Predicate<"Subtarget->hasAtomicBufferGlobalPkAddF16Insts()">,
  AssemblerPredicate<(all_of FeatureAtomicBufferGlobalPkAddF16Insts)>;
def HasAtomicGlobalPkAddBF16Inst
  : Predicate<"Subtarget->hasAtomicGlobalPkAddBF16Inst()">,
    AssemblerPredicate<(all_of FeatureAtomicGlobalPkAddBF16Inst)>;
def HasAtomicBufferPkAddBF16Inst
  : Predicate<"Subtarget->hasAtomicBufferPkAddBF16Inst()">,
    AssemblerPredicate<(all_of FeatureAtomicBufferPkAddBF16Inst)>;
def HasFlatAtomicFaddF32Inst
  : Predicate<"Subtarget->hasFlatAtomicFaddF32Inst()">,
  AssemblerPredicate<(all_of FeatureFlatAtomicFaddF32Inst)>;

def HasDefaultComponentZero
  : Predicate<"Subtarget->hasDefaultComponentZero()">,
  AssemblerPredicate<(all_of FeatureDefaultComponentZero)>;
def HasDefaultComponentBroadcast
  : Predicate<"Subtarget->hasDefaultComponentBroadcast()">,
  AssemblerPredicate<(all_of FeatureDefaultComponentBroadcast)>;

def HasDsSrc2Insts : Predicate<"!Subtarget->hasDsSrc2Insts()">,
  AssemblerPredicate<(all_of FeatureDsSrc2Insts)>;

def EnableLateCFGStructurize : Predicate<
  "EnableLateStructurizeCFG">;

def EnableFlatScratch : Predicate<"Subtarget->enableFlatScratch()">;

def DisableFlatScratch : Predicate<"!Subtarget->enableFlatScratch()">;

def HasUnalignedAccessMode : Predicate<"Subtarget->hasUnalignedAccessMode()">,
  AssemblerPredicate<(all_of FeatureUnalignedAccessMode)>;

def HasMADIntraFwdBug : Predicate<"Subtarget->hasMADIntraFwdBug()">;

def HasNotMADIntraFwdBug : Predicate<"!Subtarget->hasMADIntraFwdBug()">;

def HasSALUFloatInsts : Predicate<"Subtarget->hasSALUFloatInsts()">,
  AssemblerPredicate<(all_of FeatureSALUFloatInsts)>;

def HasVGPRSingleUseHintInsts : Predicate<"Subtarget->hasVGPRSingleUseHintInsts()">,
  AssemblerPredicate<(all_of FeatureVGPRSingleUseHintInsts)>;

def HasPseudoScalarTrans : Predicate<"Subtarget->hasPseudoScalarTrans()">,
  AssemblerPredicate<(all_of FeaturePseudoScalarTrans)>;

def HasGDS : Predicate<"Subtarget->hasGDS()">;

def HasGWS : Predicate<"Subtarget->hasGWS()">;

def HasCvtFP8VOP1Bug : Predicate<"Subtarget->hasCvtFP8VOP1Bug()">;
def HasNoCvtFP8VOP1Bug : Predicate<"!Subtarget->hasCvtFP8VOP1Bug()">;

def HasAtomicCSubNoRtnInsts : Predicate<"Subtarget->hasAtomicCSubNoRtnInsts()">;

def HasScalarDwordx3Loads : Predicate<"Subtarget->hasScalarDwordx3Loads()">;

// Include AMDGPU TD files
include "SISchedule.td"
include "GCNProcessors.td"
include "AMDGPUInstrInfo.td"
include "SIRegisterInfo.td"
include "AMDGPURegisterBanks.td"
include "AMDGPUInstructions.td"
include "SIInstrInfo.td"
include "AMDGPUCallingConv.td"
include "AMDGPUSearchableTables.td"
