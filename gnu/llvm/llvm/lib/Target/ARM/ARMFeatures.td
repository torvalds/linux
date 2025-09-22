//===----------------------------------------------------------------------===//
// ARM Subtarget state.
//

// True if compiling for Thumb, false for ARM.
def ModeThumb             : SubtargetFeature<"thumb-mode", "IsThumb",
                                             "true", "Thumb mode">;

// True if we're using software floating point features.
def ModeSoftFloat         : SubtargetFeature<"soft-float","UseSoftFloat",
                                             "true", "Use software floating "
                                             "point features.">;

//===----------------------------------------------------------------------===//
// ARM Subtarget features.
//

// This is currently only used by AArch64, but is required here because ARM and
// AArch64 share a tablegen backend for TargetParser.
class Extension<
  string TargetFeatureName,            // String used for -target-feature.
  string Spelling,                     // The XYZ in HasXYZ and AEK_XYZ.
  string Desc,                         // Description.
  list<SubtargetFeature> Implies = []  // List of dependent features.
> : SubtargetFeature<TargetFeatureName, "Has" # Spelling, "true", Desc, Implies>
{
    string ArchExtKindSpelling = "AEK_" # Spelling; // ArchExtKind enum name.
}

// Floating Point, HW Division and Neon Support

// FP loads/stores/moves, shared between VFP and MVE (even in the integer-only
// version).
def FeatureFPRegs         : SubtargetFeature<"fpregs", "HasFPRegs", "true",
                                             "Enable FP registers">;

// 16-bit FP loads/stores/moves, shared between VFP (with the v8.2A FP16
// extension) and MVE (even in the integer-only version).
def FeatureFPRegs16       : SubtargetFeature<"fpregs16", "HasFPRegs16", "true",
                                             "Enable 16-bit FP registers",
                                             [FeatureFPRegs]>;

def FeatureFPRegs64       : SubtargetFeature<"fpregs64", "HasFPRegs64", "true",
                                             "Enable 64-bit FP registers",
                                             [FeatureFPRegs]>;

// True if the floating point unit supports double precision.
def FeatureFP64           : SubtargetFeature<"fp64", "HasFP64", "true",
                                             "Floating point unit supports "
                                             "double precision",
                                             [FeatureFPRegs64]>;

// True if subtarget has the full 32 double precision FP registers for VFPv3.
def FeatureD32            : SubtargetFeature<"d32", "HasD32", "true",
                                             "Extend FP to 32 double registers">;

/// Versions of the VFP flags restricted to single precision, or to
/// 16 d-registers, or both.
multiclass VFPver<string name, string query, string description,
                  list<SubtargetFeature> prev,
                  list<SubtargetFeature> otherimplies,
                  list<SubtargetFeature> vfp2prev = []> {
  def _D16_SP: SubtargetFeature<
    name#"d16sp", query#"D16SP", "true",
    description#" with only 16 d-registers and no double precision",
    !foreach(v, prev, !cast<SubtargetFeature>(v # "_D16_SP")) #
      !foreach(v, vfp2prev, !cast<SubtargetFeature>(v # "_SP")) #
      otherimplies>;
  def _SP: SubtargetFeature<
    name#"sp", query#"SP", "true",
    description#" with no double precision",
    !foreach(v, prev, !cast<SubtargetFeature>(v # "_SP")) #
      otherimplies # [FeatureD32, !cast<SubtargetFeature>(NAME # "_D16_SP")]>;
  def _D16: SubtargetFeature<
    name#"d16", query#"D16", "true",
    description#" with only 16 d-registers",
    !foreach(v, prev, !cast<SubtargetFeature>(v # "_D16")) #
      vfp2prev #
      otherimplies # [FeatureFP64, !cast<SubtargetFeature>(NAME # "_D16_SP")]>;
  def "": SubtargetFeature<
    name, query, "true", description,
    prev # otherimplies # [
        !cast<SubtargetFeature>(NAME # "_D16"),
        !cast<SubtargetFeature>(NAME # "_SP")]>;
}

def FeatureVFP2_SP        : SubtargetFeature<"vfp2sp", "HasVFPv2SP", "true",
                                             "Enable VFP2 instructions with "
                                             "no double precision",
                                             [FeatureFPRegs]>;

def FeatureVFP2           : SubtargetFeature<"vfp2", "HasVFPv2", "true",
                                             "Enable VFP2 instructions",
                                             [FeatureFP64, FeatureVFP2_SP]>;

defm FeatureVFP3: VFPver<"vfp3", "HasVFPv3", "Enable VFP3 instructions",
                         [], [], [FeatureVFP2]>;

def FeatureNEON           : SubtargetFeature<"neon", "HasNEON", "true",
                                             "Enable NEON instructions",
                                             [FeatureVFP3]>;

// True if subtarget supports half-precision FP conversions.
def FeatureFP16           : SubtargetFeature<"fp16", "HasFP16", "true",
                                             "Enable half-precision "
                                             "floating point">;

defm FeatureVFP4: VFPver<"vfp4", "HasVFPv4", "Enable VFP4 instructions",
                         [FeatureVFP3], [FeatureFP16]>;

defm FeatureFPARMv8: VFPver<"fp-armv8", "HasFPARMv8", "Enable ARMv8 FP",
                         [FeatureVFP4], []>;

// True if subtarget supports half-precision FP operations.
def FeatureFullFP16       : SubtargetFeature<"fullfp16", "HasFullFP16", "true",
                                             "Enable full half-precision "
                                             "floating point",
                                             [FeatureFPARMv8_D16_SP, FeatureFPRegs16]>;

// True if subtarget supports half-precision FP fml operations.
def FeatureFP16FML        : SubtargetFeature<"fp16fml", "HasFP16FML", "true",
                                             "Enable full half-precision "
                                             "floating point fml instructions",
                                             [FeatureFullFP16]>;

// True if subtarget supports [su]div in Thumb mode.
def FeatureHWDivThumb     : SubtargetFeature<"hwdiv",
                                             "HasDivideInThumbMode", "true",
                                             "Enable divide instructions in Thumb">;

// True if subtarget supports [su]div in ARM mode.
def FeatureHWDivARM       : SubtargetFeature<"hwdiv-arm",
                                             "HasDivideInARMMode", "true",
                                             "Enable divide instructions in ARM mode">;

// Atomic Support

// True if the subtarget supports DMB / DSB data barrier instructions.
def FeatureDB             : SubtargetFeature<"db", "HasDataBarrier", "true",
                                             "Has data barrier (dmb/dsb) instructions">;

// True if the subtarget supports CLREX instructions.
def FeatureV7Clrex        : SubtargetFeature<"v7clrex", "HasV7Clrex", "true",
                                             "Has v7 clrex instruction">;

// True if the subtarget supports DFB data barrier instruction.
def FeatureDFB  : SubtargetFeature<"dfb", "HasFullDataBarrier", "true",
                                   "Has full data barrier (dfb) instruction">;

// True if the subtarget supports v8 atomics (LDA/LDAEX etc) instructions.
def FeatureAcquireRelease : SubtargetFeature<"acquire-release",
                                             "HasAcquireRelease", "true",
                                             "Has v8 acquire/release (lda/ldaex "
                                             " etc) instructions">;


// True if floating point compare + branch is slow.
def FeatureSlowFPBrcc     : SubtargetFeature<"slow-fp-brcc", "IsFPBrccSlow", "true",
                                             "FP compare + branch is slow">;

// True if the processor supports the Performance Monitor Extensions. These
// include a generic cycle-counter as well as more fine-grained (often
// implementation-specific) events.
def FeaturePerfMon        : SubtargetFeature<"perfmon", "HasPerfMon", "true",
                                             "Enable support for Performance "
                                             "Monitor extensions">;


// TrustZone Security Extensions

// True if processor supports TrustZone security extensions.
def FeatureTrustZone      : SubtargetFeature<"trustzone", "HasTrustZone", "true",
                                             "Enable support for TrustZone "
                                             "security extensions">;

// True if processor supports ARMv8-M Security Extensions.
def Feature8MSecExt       : SubtargetFeature<"8msecext", "Has8MSecExt", "true",
                                             "Enable support for ARMv8-M "
                                             "Security Extensions">;

// True if processor supports SHA1 and SHA256.
def FeatureSHA2           : SubtargetFeature<"sha2", "HasSHA2", "true",
                                             "Enable SHA1 and SHA256 support", [FeatureNEON]>;

def FeatureAES            : SubtargetFeature<"aes", "HasAES", "true",
                                             "Enable AES support", [FeatureNEON]>;

// True if processor supports Cryptography extensions.
def FeatureCrypto         : SubtargetFeature<"crypto", "HasCrypto", "true",
                                             "Enable support for "
                                             "Cryptography extensions",
                                             [FeatureNEON, FeatureSHA2, FeatureAES]>;

// True if processor supports CRC instructions.
def FeatureCRC            : SubtargetFeature<"crc", "HasCRC", "true",
                                             "Enable support for CRC instructions">;

// True if the ARMv8.2A dot product instructions are supported.
def FeatureDotProd        : SubtargetFeature<"dotprod", "HasDotProd", "true",
                                             "Enable support for dot product instructions",
                                             [FeatureNEON]>;

// True if the processor supports RAS extensions.
// Not to be confused with FeatureHasRetAddrStack (return address stack).
def FeatureRAS            : SubtargetFeature<"ras", "HasRAS", "true",
                                             "Enable Reliability, Availability "
                                             "and Serviceability extensions">;

// Fast computation of non-negative address offsets.
// True if processor does positive address offset computation faster.
def FeatureFPAO           : SubtargetFeature<"fpao", "HasFPAO", "true",
                                             "Enable fast computation of "
                                             "positive address offsets">;

// Fast execution of AES crypto operations.
// True if processor executes back to back AES instruction pairs faster.
def FeatureFuseAES        : SubtargetFeature<"fuse-aes", "HasFuseAES", "true",
                                             "CPU fuses AES crypto operations">;

// Fast execution of bottom and top halves of literal generation.
// True if processor executes back to back bottom and top halves of literal generation faster.
def FeatureFuseLiterals   : SubtargetFeature<"fuse-literals", "HasFuseLiterals", "true",
                                             "CPU fuses literal generation operations">;

// Choice of hardware register to use as the thread pointer, if any.
def FeatureReadTpTPIDRURW :  SubtargetFeature<"read-tp-tpidrurw", "IsReadTPTPIDRURW", "true",
                                      "Reading thread pointer from TPIDRURW register">;
def FeatureReadTpTPIDRURO :  SubtargetFeature<"read-tp-tpidruro", "IsReadTPTPIDRURO", "true",
                                      "Reading thread pointer from TPIDRURO register">;
def FeatureReadTpTPIDRPRW :  SubtargetFeature<"read-tp-tpidrprw", "IsReadTPTPIDRPRW", "true",
                                      "Reading thread pointer from TPIDRPRW register">;

// Cyclone can zero VFP registers in 0 cycles.
// True if the instructions "vmov.i32 d0, #0" and "vmov.i32 q0, #0" are
// particularly effective at zeroing a VFP register.
def FeatureZCZeroing      : SubtargetFeature<"zcz", "HasZeroCycleZeroing", "true",
                                             "Has zero-cycle zeroing instructions">;

// Whether it is profitable to unpredicate certain instructions during if-conversion.
// True if if conversion may decide to leave some instructions unpredicated.
def FeatureProfUnpredicate : SubtargetFeature<"prof-unpr",
                                              "IsProfitableToUnpredicate", "true",
                                              "Is profitable to unpredicate">;

// Some targets (e.g. Swift) have microcoded VGETLNi32.
// True if VMOV will be favored over VGETLNi32.
def FeatureSlowVGETLNi32  : SubtargetFeature<"slow-vgetlni32",
                                             "HasSlowVGETLNi32", "true",
                                             "Has slow VGETLNi32 - prefer VMOV">;

// Some targets (e.g. Swift) have microcoded VDUP32.
// True if VMOV will be favored over VDUP.
def FeatureSlowVDUP32     : SubtargetFeature<"slow-vdup32", "HasSlowVDUP32",
                                             "true",
                                             "Has slow VDUP32 - prefer VMOV">;

// Some targets (e.g. Cortex-A9) prefer VMOVSR to VMOVDRR even when using NEON
// for scalar FP, as this allows more effective execution domain optimization.
// True if VMOVSR will be favored over VMOVDRR.
def FeaturePreferVMOVSR   : SubtargetFeature<"prefer-vmovsr", "PreferVMOVSR",
                                             "true", "Prefer VMOVSR">;

// Swift has ISHST barriers compatible with Atomic Release semantics but weaker
// than ISH.
// True if ISHST barriers will be used for Release semantics.
def FeaturePrefISHSTBarrier : SubtargetFeature<"prefer-ishst", "PreferISHSTBarriers",
                                               "true", "Prefer ISHST barriers">;

// Some targets (e.g. Cortex-A9) have muxed AGU and NEON/FPU.
// True if the AGU and NEON/FPU units are multiplexed.
def FeatureMuxedUnits     : SubtargetFeature<"muxed-units", "HasMuxedUnits",
                                             "true",
                                             "Has muxed AGU and NEON/FPU">;

// Whether VLDM/VSTM starting with odd register number need more microops
// than single VLDRS.
// True if a VLDM/VSTM starting with an odd register number is considered to
// take more microops than single VLDRS/VSTRS.
def FeatureSlowOddRegister : SubtargetFeature<"slow-odd-reg", "HasSlowOddRegister",
                                              "true", "VLDM/VSTM starting "
                                              "with an odd register is slow">;

// Some targets have a renaming dependency when loading into D subregisters.
// True if loading into a D subregister will be penalized.
def FeatureSlowLoadDSubreg : SubtargetFeature<"slow-load-D-subreg",
                                              "HasSlowLoadDSubregister", "true",
                                              "Loading into D subregs is slow">;

// True if use a wider stride when allocating VFP registers.
def FeatureUseWideStrideVFP : SubtargetFeature<"wide-stride-vfp",
                                               "UseWideStrideVFP", "true",
                                               "Use a wide stride when allocating VFP registers">;

// Some targets (e.g. Cortex-A15) never want VMOVS to be widened to VMOVD.
// True if VMOVS will never be widened to VMOVD.
def FeatureDontWidenVMOVS : SubtargetFeature<"dont-widen-vmovs",
                                             "DontWidenVMOVS", "true",
                                             "Don't widen VMOVS to VMOVD">;

// Some targets (e.g. Cortex-A15) prefer to avoid mixing operations on different
// VFP register widths.
// True if splat a register between VFP and NEON instructions.
def FeatureSplatVFPToNeon : SubtargetFeature<"splat-vfp-neon",
                                             "UseSplatVFPToNeon", "true",
                                             "Splat register from VFP to NEON",
                                             [FeatureDontWidenVMOVS]>;

// Whether or not it is profitable to expand VFP/NEON MLA/MLS instructions.
// True if run the MLx expansion pass.
def FeatureExpandMLx      : SubtargetFeature<"expand-fp-mlx",
                                             "ExpandMLx", "true",
                                             "Expand VFP/NEON MLA/MLS instructions">;

// Some targets have special RAW hazards for VFP/NEON VMLA/VMLS.
// True if VFP/NEON VMLA/VMLS have special RAW hazards.
def FeatureHasVMLxHazards : SubtargetFeature<"vmlx-hazards", "HasVMLxHazards",
                                             "true", "Has VMLx hazards">;

// Some targets (e.g. Cortex-A9) want to convert VMOVRS, VMOVSR and VMOVS from
// VFP to NEON, as an execution domain optimization.
// True if VMOVRS, VMOVSR and VMOVS will be converted from VFP to NEON.
def FeatureNEONForFPMovs  : SubtargetFeature<"neon-fpmovs",
                                             "UseNEONForFPMovs", "true",
                                             "Convert VMOVSR, VMOVRS, "
                                             "VMOVS to NEON">;

// Some processors benefit from using NEON instructions for scalar
// single-precision FP operations. This affects instruction selection and should
// only be enabled if the handling of denormals is not important.
// Use the method useNEONForSinglePrecisionFP() to determine if NEON should actually be used.
def FeatureNEONForFP      : SubtargetFeature<"neonfp",
                                             "HasNEONForFP",
                                             "true",
                                             "Use NEON for single precision FP">;

// On some processors, VLDn instructions that access unaligned data take one
// extra cycle. Take that into account when computing operand latencies.
// True if VLDn instructions take an extra cycle for unaligned accesses.
def FeatureCheckVLDnAlign : SubtargetFeature<"vldn-align", "CheckVLDnAccessAlignment",
                                             "true",
                                             "Check for VLDn unaligned access">;

// Some processors have a nonpipelined VFP coprocessor.
// True if VFP instructions are not pipelined.
def FeatureNonpipelinedVFP : SubtargetFeature<"nonpipelined-vfp",
                                              "NonpipelinedVFP", "true",
                                              "VFP instructions are not pipelined">;

// Some processors have FP multiply-accumulate instructions that don't
// play nicely with other VFP / NEON instructions, and it's generally better
// to just not use them.
// If the VFP2 / NEON instructions are available, indicates
// whether the FP VML[AS] instructions are slow (if so, don't use them).
def FeatureHasSlowFPVMLx  : SubtargetFeature<"slowfpvmlx", "SlowFPVMLx", "true",
                                             "Disable VFP / NEON MAC instructions">;

// VFPv4 added VFMA instructions that can similarly be fast or slow.
// If the VFP4 / NEON instructions are available, indicates
// whether the FP VFM[AS] instructions are slow (if so, don't use them).
def FeatureHasSlowFPVFMx  : SubtargetFeature<"slowfpvfmx", "SlowFPVFMx", "true",
                                             "Disable VFP / NEON FMA instructions">;

// Cortex-A8 / A9 Advanced SIMD has multiplier accumulator forwarding.
/// True if NEON has special multiplier accumulator
/// forwarding to allow mul + mla being issued back to back.
def FeatureVMLxForwarding : SubtargetFeature<"vmlx-forwarding",
                                             "HasVMLxForwarding", "true",
                                             "Has multiplier accumulator forwarding">;

// Disable 32-bit to 16-bit narrowing for experimentation.
// True if codegen would prefer 32-bit Thumb instructions over 16-bit ones.
def FeaturePref32BitThumb : SubtargetFeature<"32bit", "Prefers32BitThumb", "true",
                                             "Prefer 32-bit Thumb instrs">;

def FeaturePrefLoopAlign32 : SubtargetFeature<"loop-align", "PrefLoopLogAlignment","2",
                                              "Prefer 32-bit alignment for loops">;

def FeatureMVEVectorCostFactor1 : SubtargetFeature<"mve1beat", "MVEVectorCostFactor", "4",
                        "Model MVE instructions as a 1 beat per tick architecture">;

def FeatureMVEVectorCostFactor2 : SubtargetFeature<"mve2beat", "MVEVectorCostFactor", "2",
                        "Model MVE instructions as a 2 beats per tick architecture">;

def FeatureMVEVectorCostFactor4 : SubtargetFeature<"mve4beat", "MVEVectorCostFactor", "1",
                        "Model MVE instructions as a 4 beats per tick architecture">;

/// Some instructions update CPSR partially, which can add false dependency for
/// out-of-order implementation, e.g. Cortex-A9, unless each individual bit is
/// mapped to a separate physical register. Avoid partial CPSR update for these
/// processors.
/// True if codegen would avoid using instructions
/// that partially update CPSR and add false dependency on the previous
/// CPSR setting instruction.
def FeatureAvoidPartialCPSR : SubtargetFeature<"avoid-partial-cpsr",
                                               "AvoidCPSRPartialUpdate", "true",
                                 "Avoid CPSR partial update for OOO execution">;

/// Disable +1 predication cost for instructions updating CPSR.
/// Enabled for Cortex-A57.
/// True if disable +1 predication cost for instructions updating CPSR. Enabled for Cortex-A57.
def FeatureCheapPredicableCPSR : SubtargetFeature<"cheap-predicable-cpsr",
                                                  "CheapPredicableCPSRDef",
                                                  "true",
                  "Disable +1 predication cost for instructions updating CPSR">;

// True if codegen should avoid using flag setting movs with shifter operand (i.e. asr, lsl, lsr).
def FeatureAvoidMOVsShOp  : SubtargetFeature<"avoid-movs-shop",
                                             "AvoidMOVsShifterOperand", "true",
                                             "Avoid movs instructions with "
                                             "shifter operand">;

// Some processors perform return stack prediction. CodeGen should avoid issue
// "normal" call instructions to callees which do not return.
def FeatureHasRetAddrStack : SubtargetFeature<"ret-addr-stack",
                                              "HasRetAddrStack", "true",
                                              "Has return address stack">;

// Some processors have no branch predictor, which changes the expected cost of
// taking a branch which affects the choice of whether to use predicated
// instructions.
// True if the subtarget has a branch predictor. Having
// a branch predictor or not changes the expected cost of taking a branch
// which affects the choice of whether to use predicated instructions.
def FeatureHasNoBranchPredictor : SubtargetFeature<"no-branch-predictor",
                                                   "HasBranchPredictor", "false",
                                                   "Has no branch predictor">;

/// DSP extension.
/// True if the subtarget supports the DSP (saturating arith and such) instructions.
def FeatureDSP            : SubtargetFeature<"dsp", "HasDSP", "true",
                                             "Supports DSP instructions in "
                                             "ARM and/or Thumb2">;

// True if the subtarget supports Multiprocessing extension (ARMv7 only).
def FeatureMP             : SubtargetFeature<"mp", "HasMPExtension", "true",
                                        "Supports Multiprocessing extension">;

// Virtualization extension - requires HW divide (ARMv7-AR ARMARM - 4.4.8).
def FeatureVirtualization : SubtargetFeature<"virtualization",
                                             "HasVirtualization", "true",
                                             "Supports Virtualization extension",
                                             [FeatureHWDivThumb, FeatureHWDivARM]>;

// Special TRAP encoding for NaCl, which looks like a TRAP in Thumb too.
// See ARMInstrInfo.td for details.
// True if NaCl TRAP instruction is generated instead of the regular TRAP.
def FeatureNaClTrap       : SubtargetFeature<"nacl-trap", "UseNaClTrap", "true",
                                             "NaCl trap">;

// True if the subtarget disallows unaligned memory
// accesses for some types.  For details, see
// ARMTargetLowering::allowsMisalignedMemoryAccesses().
def FeatureStrictAlign    : SubtargetFeature<"strict-align",
                                             "StrictAlign", "true",
                                             "Disallow all unaligned memory "
                                             "access">;

// Generate calls via indirect call instructions.
def FeatureLongCalls      : SubtargetFeature<"long-calls", "GenLongCalls", "true",
                                             "Generate calls via indirect call "
                                             "instructions">;

// Generate code that does not contain data access to code sections.
def FeatureExecuteOnly    : SubtargetFeature<"execute-only",
                                             "GenExecuteOnly", "true",
                                             "Enable the generation of "
                                             "execute only code.">;

// True if R9 is not available as a general purpose register.
def FeatureReserveR9      : SubtargetFeature<"reserve-r9", "ReserveR9", "true",
                                             "Reserve R9, making it unavailable"
                                             " as GPR">;

// True if MOVT / MOVW pairs are not used for materialization of
// 32-bit imms (including global addresses).
def FeatureNoMovt         : SubtargetFeature<"no-movt", "NoMovt", "true",
                                             "Don't use movt/movw pairs for "
                                             "32-bit imms">;

/// Implicitly convert an instruction to a different one if its immediates
/// cannot be encoded. For example, ADD r0, r1, #FFFFFFFF -> SUB r0, r1, #1.
def FeatureNoNegativeImmediates
                          : SubtargetFeature<"no-neg-immediates",
                                             "NegativeImmediates", "false",
                                             "Convert immediates and instructions "
                                             "to their negated or complemented "
                                             "equivalent when the immediate does "
                                             "not fit in the encoding.">;

// Use the MachineScheduler for instruction scheduling for the subtarget.
def FeatureUseMISched: SubtargetFeature<"use-misched", "UseMISched", "true",
                                        "Use the MachineScheduler">;

// Use the MachinePipeliner for instruction scheduling for the subtarget.
def FeatureUseMIPipeliner: SubtargetFeature<"use-mipipeliner", "UseMIPipeliner", "true",
                                            "Use the MachinePipeliner">;

// False if scheduling should happen again after register allocation.
def FeatureNoPostRASched : SubtargetFeature<"disable-postra-scheduler",
    "DisablePostRAScheduler", "true",
    "Don't schedule again after register allocation">;

// Armv8.5-A extensions

// Has speculation barrier.
def FeatureSB       : SubtargetFeature<"sb", "HasSB", "true",
  "Enable v8.5a Speculation Barrier" >;

// Armv8.6-A extensions

// True if subtarget supports BFloat16 floating point operations.
def FeatureBF16     : SubtargetFeature<"bf16", "HasBF16", "true",
  "Enable support for BFloat16 instructions",  [FeatureNEON]>;

// True if subtarget supports 8-bit integer matrix multiply.
def FeatureMatMulInt8 : SubtargetFeature<"i8mm", "HasMatMulInt8",
    "true", "Enable Matrix Multiply Int8 Extension", [FeatureNEON]>;

// Armv8.1-M extensions

// True if the processor supports the Low Overhead Branch extension.
def FeatureLOB            : SubtargetFeature<"lob", "HasLOB", "true",
                                             "Enable Low Overhead Branch "
                                             "extensions">;

// Mitigate against the cve-2021-35465 security vulnurability.
def FeatureFixCMSE_CVE_2021_35465 : SubtargetFeature<"fix-cmse-cve-2021-35465",
                                        "FixCMSE_CVE_2021_35465", "true",
                                        "Mitigate against the cve-2021-35465 "
                                        "security vulnurability">;

def FeaturePACBTI         : SubtargetFeature<"pacbti", "HasPACBTI", "true",
                                             "Enable Pointer Authentication and Branch "
                                             "Target Identification">;

/// Don't place a BTI instruction after return-twice constructs (setjmp).
def FeatureNoBTIAtReturnTwice : SubtargetFeature<"no-bti-at-return-twice",
                                                 "NoBTIAtReturnTwice", "true",
                                                 "Don't place a BTI instruction "
                                                 "after a return-twice">;

// Armv8.9-A/Armv9.4-A 2022 Architecture Extensions
def FeatureCLRBHB : SubtargetFeature<"clrbhb", "HasCLRBHB", "true",
                                     "Enable Clear BHB instruction">;


def FeatureFixCortexA57AES1742098 : SubtargetFeature<"fix-cortex-a57-aes-1742098",
  "FixCortexA57AES1742098", "true",
  "Work around Cortex-A57 Erratum 1742098 / Cortex-A72 Erratum 1655431 (AES)">;

// If frame pointers are in use, they must follow the AAPCS definition, which
// always uses R11 as the frame pointer. If this is not set, we can use R7 as
// the frame pointer for Thumb1-only code, which is more efficient, but less
// compatible. Note that this feature does not control whether frame pointers
// are emitted, that is controlled by the "frame-pointer" function attribute.
def FeatureAAPCSFrameChain : SubtargetFeature<"aapcs-frame-chain",
                                              "CreateAAPCSFrameChain", "true",
                                              "Create an AAPCS compliant frame chain">;

// Assume that lock-free 32-bit atomics are available, even if the target
// and operating system combination would not usually provide them. The user
// is responsible for providing any necessary __sync implementations. Code
// built with this feature is not ABI-compatible with code built without this
// feature, if atomic variables are exposed across the ABI boundary.
def FeatureAtomics32 : SubtargetFeature<
    "atomics-32", "HasForced32BitAtomics", "true",
    "Assume that lock-free 32-bit atomics are available">;

//===----------------------------------------------------------------------===//
// ARM architecture class
//

// A-series ISA
def FeatureAClass : SubtargetFeature<"aclass", "ARMProcClass", "AClass",
                                     "Is application profile ('A' series)">;

// R-series ISA
def FeatureRClass : SubtargetFeature<"rclass", "ARMProcClass", "RClass",
                                     "Is realtime profile ('R' series)">;

// M-series ISA
def FeatureMClass : SubtargetFeature<"mclass", "ARMProcClass", "MClass",
                                     "Is microcontroller profile ('M' series)">;

// True if Thumb2 instructions are supported.
def FeatureThumb2 : SubtargetFeature<"thumb2", "HasThumb2", "true",
                                     "Enable Thumb2 instructions">;

// True if subtarget does not support ARM mode execution.
def FeatureNoARM  : SubtargetFeature<"noarm", "NoARM", "true",
                                     "Does not support ARM mode execution">;

//===----------------------------------------------------------------------===//
// ARM ISAa.
//
// Specify whether target support specific ARM ISA variants.

def HasV4TOps   : SubtargetFeature<"v4t", "HasV4TOps", "true",
                                   "Support ARM v4T instructions">;

def HasV5TOps   : SubtargetFeature<"v5t", "HasV5TOps", "true",
                                   "Support ARM v5T instructions",
                                   [HasV4TOps]>;

def HasV5TEOps  : SubtargetFeature<"v5te", "HasV5TEOps", "true",
                                   "Support ARM v5TE, v5TEj, and "
                                   "v5TExp instructions",
                                   [HasV5TOps]>;

def HasV6Ops    : SubtargetFeature<"v6", "HasV6Ops", "true",
                                   "Support ARM v6 instructions",
                                   [HasV5TEOps]>;

def HasV6MOps   : SubtargetFeature<"v6m", "HasV6MOps", "true",
                                   "Support ARM v6M instructions",
                                   [HasV6Ops]>;

def HasV8MBaselineOps : SubtargetFeature<"v8m", "HasV8MBaselineOps", "true",
                                         "Support ARM v8M Baseline instructions",
                                         [HasV6MOps]>;

def HasV6KOps   : SubtargetFeature<"v6k", "HasV6KOps", "true",
                                   "Support ARM v6k instructions",
                                   [HasV6Ops]>;

def HasV6T2Ops  : SubtargetFeature<"v6t2", "HasV6T2Ops", "true",
                                   "Support ARM v6t2 instructions",
                                   [HasV8MBaselineOps, HasV6KOps, FeatureThumb2]>;

def HasV7Ops    : SubtargetFeature<"v7", "HasV7Ops", "true",
                                   "Support ARM v7 instructions",
                                   [HasV6T2Ops, FeatureV7Clrex]>;

def HasV8MMainlineOps :
                  SubtargetFeature<"v8m.main", "HasV8MMainlineOps", "true",
                                   "Support ARM v8M Mainline instructions",
                                   [HasV7Ops]>;

def HasV8Ops    : SubtargetFeature<"v8", "HasV8Ops", "true",
                                   "Support ARM v8 instructions",
                                   [HasV7Ops, FeaturePerfMon, FeatureAcquireRelease]>;

def HasV8_1aOps : SubtargetFeature<"v8.1a", "HasV8_1aOps", "true",
                                   "Support ARM v8.1a instructions",
                                   [HasV8Ops]>;

def HasV8_2aOps : SubtargetFeature<"v8.2a", "HasV8_2aOps", "true",
                                   "Support ARM v8.2a instructions",
                                   [HasV8_1aOps]>;

def HasV8_3aOps   : SubtargetFeature<"v8.3a", "HasV8_3aOps", "true",
                                   "Support ARM v8.3a instructions",
                                   [HasV8_2aOps]>;

def HasV8_4aOps   : SubtargetFeature<"v8.4a", "HasV8_4aOps", "true",
                                   "Support ARM v8.4a instructions",
                                   [HasV8_3aOps, FeatureDotProd]>;

def HasV8_5aOps   : SubtargetFeature<"v8.5a", "HasV8_5aOps", "true",
                                   "Support ARM v8.5a instructions",
                                   [HasV8_4aOps, FeatureSB]>;

def HasV8_6aOps   : SubtargetFeature<"v8.6a", "HasV8_6aOps", "true",
                                   "Support ARM v8.6a instructions",
                                   [HasV8_5aOps, FeatureBF16,
                                    FeatureMatMulInt8]>;

def HasV8_7aOps   : SubtargetFeature<"v8.7a", "HasV8_7aOps", "true",
                                   "Support ARM v8.7a instructions",
                                   [HasV8_6aOps]>;

def HasV8_8aOps   : SubtargetFeature<"v8.8a", "HasV8_8aOps", "true",
                                   "Support ARM v8.8a instructions",
                                   [HasV8_7aOps]>;

def HasV8_9aOps   : SubtargetFeature<"v8.9a", "HasV8_9aOps", "true",
                                   "Support ARM v8.9a instructions",
                                   [HasV8_8aOps, FeatureCLRBHB]>;

def HasV9_0aOps   : SubtargetFeature<"v9a", "HasV9_0aOps", "true",
                                   "Support ARM v9a instructions",
                                   [HasV8_5aOps]>;

def HasV9_1aOps   : SubtargetFeature<"v9.1a", "HasV9_1aOps", "true",
                                   "Support ARM v9.1a instructions",
                                   [HasV8_6aOps, HasV9_0aOps]>;

def HasV9_2aOps   : SubtargetFeature<"v9.2a", "HasV9_2aOps", "true",
                                   "Support ARM v9.2a instructions",
                                   [HasV8_7aOps, HasV9_1aOps]>;

def HasV9_3aOps   : SubtargetFeature<"v9.3a", "HasV9_3aOps", "true",
                                   "Support ARM v9.3a instructions",
                                   [HasV8_8aOps, HasV9_2aOps]>;

def HasV9_4aOps   : SubtargetFeature<"v9.4a", "HasV9_4aOps", "true",
                                   "Support ARM v9.4a instructions",
                                   [HasV8_9aOps, HasV9_3aOps]>;

// Armv9.5-A is a v9-only architecture. From v9.5-A onwards there's no mapping
// to an equivalent v8.x version.
def HasV9_5aOps   : SubtargetFeature<"v9.5a", "HasV9_5aOps", "true",
                                   "Support ARM v9.5a instructions",
                                   [HasV9_4aOps]>;

def HasV8_1MMainlineOps : SubtargetFeature<
               "v8.1m.main", "HasV8_1MMainlineOps", "true",
               "Support ARM v8-1M Mainline instructions",
               [HasV8MMainlineOps]>;
def HasMVEIntegerOps : SubtargetFeature<
               "mve", "HasMVEIntegerOps", "true",
               "Support M-Class Vector Extension with integer ops",
               [HasV8_1MMainlineOps, FeatureDSP, FeatureFPRegs16, FeatureFPRegs64]>;
def HasMVEFloatOps : SubtargetFeature<
               "mve.fp", "HasMVEFloatOps", "true",
               "Support M-Class Vector Extension with integer and floating ops",
               [HasMVEIntegerOps, FeatureFPARMv8_D16_SP, FeatureFullFP16]>;

def HasCDEOps : SubtargetFeature<"cde", "HasCDEOps", "true",
                                 "Support CDE instructions",
                                 [HasV8MMainlineOps]>;

foreach i = {0-7} in
    def FeatureCoprocCDE#i : SubtargetFeature<"cdecp"#i,
                                              "CoprocCDE["#i#"]", "true",
                                              "Coprocessor "#i#" ISA is CDEv1",
                                              [HasCDEOps]>;

//===----------------------------------------------------------------------===//
// Control codegen mitigation against Straight Line Speculation vulnerability.
//===----------------------------------------------------------------------===//

/// Harden against Straight Line Speculation for Returns and Indirect Branches.
def FeatureHardenSlsRetBr : SubtargetFeature<"harden-sls-retbr",
  "HardenSlsRetBr", "true",
  "Harden against straight line speculation across RETurn and BranchRegister "
  "instructions">;
/// Harden against Straight Line Speculation for indirect calls.
def FeatureHardenSlsBlr : SubtargetFeature<"harden-sls-blr",
  "HardenSlsBlr", "true",
  "Harden against straight line speculation across indirect calls">;
/// Generate thunk code for SLS mitigation in the normal text section.
def FeatureHardenSlsNoComdat : SubtargetFeature<"harden-sls-nocomdat",
  "HardenSlsNoComdat", "true",
  "Generate thunk code for SLS mitigation in the normal text section">;

//===----------------------------------------------------------------------===//
// Endianness of instruction encodings in memory.
//
// In the current Arm architecture, this is usually little-endian regardless of
// data endianness. But before Armv7 it was typical for instruction endianness
// to match data endianness, so that a big-endian system was consistently big-
// endian. And Armv7-R can be configured to use big-endian instructions.
//
// Additionally, even when targeting Armv7-A, big-endian instructions can be
// found in relocatable object files, because the Arm ABI specifies that the
// linker byte-reverses them depending on the target architecture.
//
// So we have a feature here to indicate that instructions are stored big-
// endian, which you can set when instantiating an MCDisassembler.
def ModeBigEndianInstructions : SubtargetFeature<"big-endian-instructions",
    "BigEndianInstructions", "true",
     "Expect instructions to be stored big-endian.">;

