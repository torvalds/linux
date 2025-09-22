//=------ AArch64FMV.td - Describe AArch64 FMV Features ------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Function MultiVersioning (FMV) properties. FMV features are accepted by the
// attributes target_version and target_clones, and they correspond to a mapping
// from the FMV feature name to:
//  - A bit in the FMV ABI, as defined by the ACLE.
//  - The FMV priority, as defined by the ACLE.
//  - A list of backend features.
// 
// The list of backend features is not a set of dependencies; it is specific to
// LLVM and indicates how to do codegen when the FMV feature is present.
// 
// Therefore FMVExtensions are separated from regular AArch64 Extensions, which
// encode dependencies between themselves and other SubtargetFeatures.
//===----------------------------------------------------------------------===//


// Something you can add to target_version or target_clones.
class FMVExtension<string n, string b, string f, int p> {
    // Name, as spelled in target_version or target_clones. e.g. "memtag".
    string Name = n;

    // A C++ expression giving the number of the bit in the FMV ABI.
    // Currently this is given as a value from the enum "CPUFeatures".
    string Bit = b;

    // SubtargetFeatures enabled for codegen when this FMV feature is present.
    string BackendFeatures = f;

    // The FMV priority.
    int Priority = p;
}

def : FMVExtension<"aes", "FEAT_AES", "+fp-armv8,+neon", 150>;
def : FMVExtension<"bf16", "FEAT_BF16", "+bf16", 280>;
def : FMVExtension<"bti", "FEAT_BTI", "+bti", 510>;
def : FMVExtension<"crc", "FEAT_CRC", "+crc", 110>;
def : FMVExtension<"dgh", "FEAT_DGH", "", 260>;
def : FMVExtension<"dit", "FEAT_DIT", "+dit", 180>;
def : FMVExtension<"dotprod", "FEAT_DOTPROD", "+dotprod,+fp-armv8,+neon", 104>;
def : FMVExtension<"dpb", "FEAT_DPB", "+ccpp", 190>;
def : FMVExtension<"dpb2", "FEAT_DPB2", "+ccpp,+ccdp", 200>;
def : FMVExtension<"ebf16", "FEAT_EBF16", "+bf16", 290>;
def : FMVExtension<"f32mm", "FEAT_SVE_F32MM", "+sve,+f32mm,+fullfp16,+fp-armv8,+neon", 350>;
def : FMVExtension<"f64mm", "FEAT_SVE_F64MM", "+sve,+f64mm,+fullfp16,+fp-armv8,+neon", 360>;
def : FMVExtension<"fcma", "FEAT_FCMA", "+fp-armv8,+neon,+complxnum", 220>;
def : FMVExtension<"flagm", "FEAT_FLAGM", "+flagm", 20>;
def : FMVExtension<"flagm2", "FEAT_FLAGM2", "+flagm,+altnzcv", 30>;
def : FMVExtension<"fp", "FEAT_FP", "+fp-armv8,+neon", 90>;
def : FMVExtension<"fp16", "FEAT_FP16", "+fullfp16,+fp-armv8,+neon", 170>;
def : FMVExtension<"fp16fml", "FEAT_FP16FML", "+fp16fml,+fullfp16,+fp-armv8,+neon", 175>;
def : FMVExtension<"frintts", "FEAT_FRINTTS", "+fptoint", 250>;
def : FMVExtension<"i8mm", "FEAT_I8MM", "+i8mm", 270>;
def : FMVExtension<"jscvt", "FEAT_JSCVT", "+fp-armv8,+neon,+jsconv", 210>;
def : FMVExtension<"ls64", "FEAT_LS64", "", 520>;
def : FMVExtension<"ls64_accdata", "FEAT_LS64_ACCDATA", "+ls64", 540>;
def : FMVExtension<"ls64_v", "FEAT_LS64_V", "", 530>;
def : FMVExtension<"lse", "FEAT_LSE", "+lse", 80>;
def : FMVExtension<"memtag", "FEAT_MEMTAG", "", 440>;
def : FMVExtension<"memtag2", "FEAT_MEMTAG2", "+mte", 450>;
def : FMVExtension<"memtag3", "FEAT_MEMTAG3", "+mte", 460>;
def : FMVExtension<"mops", "FEAT_MOPS", "+mops", 650>;
def : FMVExtension<"pmull", "FEAT_PMULL", "+aes,+fp-armv8,+neon", 160>;
def : FMVExtension<"predres", "FEAT_PREDRES", "+predres", 480>;
def : FMVExtension<"rcpc", "FEAT_RCPC", "+rcpc", 230>;
def : FMVExtension<"rcpc2", "FEAT_RCPC2", "+rcpc", 240>;
def : FMVExtension<"rcpc3", "FEAT_RCPC3", "+rcpc,+rcpc3", 241>;
def : FMVExtension<"rdm", "FEAT_RDM", "+rdm,+fp-armv8,+neon", 108>;
def : FMVExtension<"rng", "FEAT_RNG", "+rand", 10>;
def : FMVExtension<"rpres", "FEAT_RPRES", "", 300>;
def : FMVExtension<"sb", "FEAT_SB", "+sb", 470>;
def : FMVExtension<"sha1", "FEAT_SHA1", "+fp-armv8,+neon", 120>;
def : FMVExtension<"sha2", "FEAT_SHA2", "+sha2,+fp-armv8,+neon", 130>;
def : FMVExtension<"sha3", "FEAT_SHA3", "+sha3,+sha2,+fp-armv8,+neon", 140>;
def : FMVExtension<"simd", "FEAT_SIMD", "+fp-armv8,+neon", 100>;
def : FMVExtension<"sm4", "FEAT_SM4", "+sm4,+fp-armv8,+neon", 106>;
def : FMVExtension<"sme", "FEAT_SME", "+sme,+bf16", 430>;
def : FMVExtension<"sme-f64f64", "FEAT_SME_F64", "+sme,+sme-f64f64,+bf16", 560>;
def : FMVExtension<"sme-i16i64", "FEAT_SME_I64", "+sme,+sme-i16i64,+bf16", 570>;
def : FMVExtension<"sme2", "FEAT_SME2", "+sme2,+sme,+bf16", 580>;
def : FMVExtension<"ssbs", "FEAT_SSBS", "", 490>;
def : FMVExtension<"ssbs2", "FEAT_SSBS2", "+ssbs", 500>;
def : FMVExtension<"sve", "FEAT_SVE", "+sve,+fullfp16,+fp-armv8,+neon", 310>;
def : FMVExtension<"sve-bf16", "FEAT_SVE_BF16", "+sve,+bf16,+fullfp16,+fp-armv8,+neon", 320>;
def : FMVExtension<"sve-ebf16", "FEAT_SVE_EBF16", "+sve,+bf16,+fullfp16,+fp-armv8,+neon", 330>;
def : FMVExtension<"sve-i8mm", "FEAT_SVE_I8MM", "+sve,+i8mm,+fullfp16,+fp-armv8,+neon", 340>;
def : FMVExtension<"sve2", "FEAT_SVE2", "+sve2,+sve,+fullfp16,+fp-armv8,+neon", 370>;
def : FMVExtension<"sve2-aes", "FEAT_SVE_AES", "+sve2,+sve,+sve2-aes,+fullfp16,+fp-armv8,+neon", 380>;
def : FMVExtension<"sve2-bitperm", "FEAT_SVE_BITPERM", "+sve2,+sve,+sve2-bitperm,+fullfp16,+fp-armv8,+neon", 400>;
def : FMVExtension<"sve2-pmull128", "FEAT_SVE_PMULL128", "+sve2,+sve,+sve2-aes,+fullfp16,+fp-armv8,+neon", 390>;
def : FMVExtension<"sve2-sha3", "FEAT_SVE_SHA3", "+sve2,+sve,+sve2-sha3,+fullfp16,+fp-armv8,+neon", 410>;
def : FMVExtension<"sve2-sm4", "FEAT_SVE_SM4", "+sve2,+sve,+sve2-sm4,+fullfp16,+fp-armv8,+neon", 420>;
def : FMVExtension<"wfxt", "FEAT_WFXT", "+wfxt", 550>;
