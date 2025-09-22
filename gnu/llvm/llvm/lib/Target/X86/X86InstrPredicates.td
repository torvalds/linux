//===---X86InstrPredicates.td - X86 Predicate Definitions --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def TruePredicate : Predicate<"true">;

// Intel x86 instructions have three separate encoding spaces: legacy, VEX, and
// EVEX. Not all X86 instructions are extended for EGPR. The following is an
// overview of which instructions are extended and how we implement them.
//
// * Legacy space
//   All instructions in legacy maps 0 and 1 that have explicit GPR or memory
//   operands can use the REX2 prefix to access the EGPR, except XSAVE*/XRSTOR.
//
// * EVEX space
//   All instructions in the EVEX space can access the EGPR in their
//   register/memory operands.
//
// For the above intructions, the only difference in encoding is reflected in
// the REX2/EVEX prefix when EGPR is used, i.e. the opcode and opcode name are
// unchanged. We don’t add new entries in TD, and instead we extend GPR with
// R16-R31 and make them allocatable only when the feature EGPR is available.
//
// Besides, some instructions in legacy space with map 2/3 and VEX space are
// promoted into EVEX space. Encoding space changes after the promotion, opcode
// and opcode map may change too sometimes. For these instructions, we add new
// entries in TD to avoid overcomplicating the assembler and disassembler.
//
// HasEGPR is for the new entries and NoEGPR is for the entries before
// promotion, so that the promoted variant can be selected first to benefit RA.
def HasEGPR      : Predicate<"Subtarget->hasEGPR()">;
def NoEGPR       : Predicate<"!Subtarget->hasEGPR()">;

// APX extends some instructions with a new form that has an extra register
// operand called a new data destination (NDD). In such forms, NDD is the new
// destination register receiving the result of the computation and all other
// operands (including the original destination operand) become read-only source
// operands.
//
// HasNDD is for the new NDD entries and NoNDD is for the legacy 2-address
// entries, so that the NDD variant can be selected first to benefit RA.
def HasNDD       : Predicate<"Subtarget->hasNDD()">;
def NoNDD        : Predicate<"!Subtarget->hasNDD()">;
def HasCF        : Predicate<"Subtarget->hasCF()">;
def HasCMOV      : Predicate<"Subtarget->canUseCMOV()">;
def NoCMOV       : Predicate<"!Subtarget->canUseCMOV()">;
def HasNOPL      : Predicate<"Subtarget->hasNOPL()">;
def HasMMX       : Predicate<"Subtarget->hasMMX()">;
def HasSSE1      : Predicate<"Subtarget->hasSSE1()">;
def UseSSE1      : Predicate<"Subtarget->hasSSE1() && !Subtarget->hasAVX()">;
def HasSSE2      : Predicate<"Subtarget->hasSSE2()">;
def UseSSE2      : Predicate<"Subtarget->hasSSE2() && !Subtarget->hasAVX()">;
def HasSSE3      : Predicate<"Subtarget->hasSSE3()">;
def UseSSE3      : Predicate<"Subtarget->hasSSE3() && !Subtarget->hasAVX()">;
def HasSSSE3     : Predicate<"Subtarget->hasSSSE3()">;
def UseSSSE3     : Predicate<"Subtarget->hasSSSE3() && !Subtarget->hasAVX()">;
def HasSSE41     : Predicate<"Subtarget->hasSSE41()">;
def NoSSE41      : Predicate<"!Subtarget->hasSSE41()">;
def UseSSE41     : Predicate<"Subtarget->hasSSE41() && !Subtarget->hasAVX()">;
def HasSSE42     : Predicate<"Subtarget->hasSSE42()">;
def UseSSE42     : Predicate<"Subtarget->hasSSE42() && !Subtarget->hasAVX()">;
def HasSSE4A     : Predicate<"Subtarget->hasSSE4A()">;
def NoAVX        : Predicate<"!Subtarget->hasAVX()">;
def HasAVX       : Predicate<"Subtarget->hasAVX()">;
def HasAVX2      : Predicate<"Subtarget->hasAVX2()">;
def HasAVX1Only  : Predicate<"Subtarget->hasAVX() && !Subtarget->hasAVX2()">;
def HasEVEX512   : Predicate<"Subtarget->hasEVEX512()">;
def HasAVX10_1   : Predicate<"Subtarget->hasAVX10_1()">;
def HasAVX10_1_512 : Predicate<"Subtarget->hasAVX10_1_512()">;
def HasAVX512    : Predicate<"Subtarget->hasAVX512()">;
def UseAVX       : Predicate<"Subtarget->hasAVX() && !Subtarget->hasAVX512()">;
def UseAVX2      : Predicate<"Subtarget->hasAVX2() && !Subtarget->hasAVX512()">;
def NoAVX512     : Predicate<"!Subtarget->hasAVX512()">;
def HasCDI       : Predicate<"Subtarget->hasCDI()">;
def HasVPOPCNTDQ : Predicate<"Subtarget->hasVPOPCNTDQ()">;
def HasDQI       : Predicate<"Subtarget->hasDQI()">;
def NoDQI        : Predicate<"!Subtarget->hasDQI()">;
def HasBWI       : Predicate<"Subtarget->hasBWI()">;
def NoBWI        : Predicate<"!Subtarget->hasBWI()">;
def HasVLX       : Predicate<"Subtarget->hasVLX()">;
def NoVLX        : Predicate<"!Subtarget->hasVLX()">;
def NoVLX_Or_NoBWI : Predicate<"!Subtarget->hasVLX() || !Subtarget->hasBWI()">;
def NoVLX_Or_NoDQI : Predicate<"!Subtarget->hasVLX() || !Subtarget->hasDQI()">;
def HasPKU       : Predicate<"Subtarget->hasPKU()">;
def HasVNNI      : Predicate<"Subtarget->hasVNNI()">;
def HasVP2INTERSECT : Predicate<"Subtarget->hasVP2INTERSECT()">;
def HasBF16      : Predicate<"Subtarget->hasBF16()">;
def HasFP16      : Predicate<"Subtarget->hasFP16()">;
def HasAVXVNNIINT16 : Predicate<"Subtarget->hasAVXVNNIINT16()">;
def HasAVXVNNIINT8 : Predicate<"Subtarget->hasAVXVNNIINT8()">;
def HasAVXVNNI : Predicate <"Subtarget->hasAVXVNNI()">;
def NoVLX_Or_NoVNNI : Predicate<"!Subtarget->hasVLX() || !Subtarget->hasVNNI()">;

def HasBITALG    : Predicate<"Subtarget->hasBITALG()">;
def HasPOPCNT    : Predicate<"Subtarget->hasPOPCNT()">;
def HasAES       : Predicate<"Subtarget->hasAES()">;
def HasVAES      : Predicate<"Subtarget->hasVAES()">;
def NoVLX_Or_NoVAES : Predicate<"!Subtarget->hasVLX() || !Subtarget->hasVAES()">;
def HasFXSR      : Predicate<"Subtarget->hasFXSR()">;
def HasX87       : Predicate<"Subtarget->hasX87()">;
def HasXSAVE     : Predicate<"Subtarget->hasXSAVE()">;
def HasXSAVEOPT  : Predicate<"Subtarget->hasXSAVEOPT()">;
def HasXSAVEC    : Predicate<"Subtarget->hasXSAVEC()">;
def HasXSAVES    : Predicate<"Subtarget->hasXSAVES()">;
def HasPCLMUL    : Predicate<"Subtarget->hasPCLMUL()">;
def NoVLX_Or_NoVPCLMULQDQ :
                    Predicate<"!Subtarget->hasVLX() || !Subtarget->hasVPCLMULQDQ()">;
def HasVPCLMULQDQ : Predicate<"Subtarget->hasVPCLMULQDQ()">;
def HasGFNI      : Predicate<"Subtarget->hasGFNI()">;
def HasFMA       : Predicate<"Subtarget->hasFMA()">;
def HasFMA4      : Predicate<"Subtarget->hasFMA4()">;
def NoFMA4       : Predicate<"!Subtarget->hasFMA4()">;
def HasXOP       : Predicate<"Subtarget->hasXOP()">;
def HasTBM       : Predicate<"Subtarget->hasTBM()">;
def NoTBM        : Predicate<"!Subtarget->hasTBM()">;
def HasLWP       : Predicate<"Subtarget->hasLWP()">;
def HasMOVBE     : Predicate<"Subtarget->hasMOVBE()">;
def NoNDD_Or_NoMOVBE     : Predicate<"!Subtarget->hasNDD() || !Subtarget->hasMOVBE()">;
def HasRDRAND    : Predicate<"Subtarget->hasRDRAND()">;
def HasF16C      : Predicate<"Subtarget->hasF16C()">;
def HasFSGSBase  : Predicate<"Subtarget->hasFSGSBase()">;
def HasLZCNT     : Predicate<"Subtarget->hasLZCNT()">;
def HasBMI       : Predicate<"Subtarget->hasBMI()">;
def HasBMI2      : Predicate<"Subtarget->hasBMI2()">;
def NoBMI2       : Predicate<"!Subtarget->hasBMI2()">;
def HasVBMI      : Predicate<"Subtarget->hasVBMI()">;
def HasVBMI2     : Predicate<"Subtarget->hasVBMI2()">;
def HasIFMA      : Predicate<"Subtarget->hasIFMA()">;
def HasAVXIFMA   : Predicate<"Subtarget->hasAVXIFMA()">;
def NoVLX_Or_NoIFMA : Predicate<"!Subtarget->hasVLX() || !Subtarget->hasIFMA()">;
def HasRTM       : Predicate<"Subtarget->hasRTM()">;
def HasSHA       : Predicate<"Subtarget->hasSHA()">;
def HasSHA512    : Predicate<"Subtarget->hasSHA512()">;
def HasSGX       : Predicate<"Subtarget->hasSGX()">;
def HasSM3       : Predicate<"Subtarget->hasSM3()">;
def HasRDSEED    : Predicate<"Subtarget->hasRDSEED()">;
def HasSSEPrefetch : Predicate<"Subtarget->hasSSEPrefetch()">;
def HasPRFCHW    : Predicate<"Subtarget->hasPRFCHW()">;
def HasPREFETCHI : Predicate<"Subtarget->hasPREFETCHI()">;
def HasPrefetchW : Predicate<"Subtarget->hasPrefetchW()">;
def HasLAHFSAHF  : Predicate<"Subtarget->hasLAHFSAHF()">;
def HasLAHFSAHF64 : Predicate<"Subtarget->hasLAHFSAHF64()">;
def HasMWAITX    : Predicate<"Subtarget->hasMWAITX()">;
def HasCLZERO    : Predicate<"Subtarget->hasCLZERO()">;
def HasCLDEMOTE  : Predicate<"Subtarget->hasCLDEMOTE()">;
def HasMOVDIRI   : Predicate<"Subtarget->hasMOVDIRI()">;
def HasMOVDIR64B : Predicate<"Subtarget->hasMOVDIR64B()">;
def HasPTWRITE   : Predicate<"Subtarget->hasPTWRITE()">;
def FPStackf32   : Predicate<"!Subtarget->hasSSE1()">;
def FPStackf64   : Predicate<"!Subtarget->hasSSE2()">;
def HasSHSTK     : Predicate<"Subtarget->hasSHSTK()">;
def HasSM4       : Predicate<"Subtarget->hasSM4()">;
def HasCLFLUSH   : Predicate<"Subtarget->hasCLFLUSH()">;
def HasCLFLUSHOPT : Predicate<"Subtarget->hasCLFLUSHOPT()">;
def HasCLWB      : Predicate<"Subtarget->hasCLWB()">;
def HasWBNOINVD  : Predicate<"Subtarget->hasWBNOINVD()">;
def HasRDPID     : Predicate<"Subtarget->hasRDPID()">;
def HasRDPRU     : Predicate<"Subtarget->hasRDPRU()">;
def HasWAITPKG   : Predicate<"Subtarget->hasWAITPKG()">;
def HasINVPCID   : Predicate<"Subtarget->hasINVPCID()">;
def HasCX8       : Predicate<"Subtarget->hasCX8()">;
def HasCX16      : Predicate<"Subtarget->hasCX16()">;
def HasPCONFIG   : Predicate<"Subtarget->hasPCONFIG()">;
def HasENQCMD    : Predicate<"Subtarget->hasENQCMD()">;
def HasAMXFP16   : Predicate<"Subtarget->hasAMXFP16()">;
def HasCMPCCXADD : Predicate<"Subtarget->hasCMPCCXADD()">;
def HasAVXNECONVERT : Predicate<"Subtarget->hasAVXNECONVERT()">;
def HasKL        : Predicate<"Subtarget->hasKL()">;
def HasRAOINT    : Predicate<"Subtarget->hasRAOINT()">;
def HasWIDEKL    : Predicate<"Subtarget->hasWIDEKL()">;
def HasHRESET    : Predicate<"Subtarget->hasHRESET()">;
def HasSERIALIZE : Predicate<"Subtarget->hasSERIALIZE()">;
def HasTSXLDTRK  : Predicate<"Subtarget->hasTSXLDTRK()">;
def HasAMXTILE   : Predicate<"Subtarget->hasAMXTILE()">;
def HasAMXBF16   : Predicate<"Subtarget->hasAMXBF16()">;
def HasAMXINT8   : Predicate<"Subtarget->hasAMXINT8()">;
def HasAMXCOMPLEX : Predicate<"Subtarget->hasAMXCOMPLEX()">;
def HasUINTR     : Predicate<"Subtarget->hasUINTR()">;
def HasUSERMSR   : Predicate<"Subtarget->hasUSERMSR()">;
def HasCRC32     : Predicate<"Subtarget->hasCRC32()">;

def HasX86_64    : Predicate<"Subtarget->hasX86_64()">;
def Not64BitMode : Predicate<"!Subtarget->is64Bit()">,
                             AssemblerPredicate<(all_of (not Is64Bit)), "Not 64-bit mode">;
def In64BitMode  : Predicate<"Subtarget->is64Bit()">,
                             AssemblerPredicate<(all_of Is64Bit), "64-bit mode">;
def IsLP64  : Predicate<"Subtarget->isTarget64BitLP64()">;
def NotLP64 : Predicate<"!Subtarget->isTarget64BitLP64()">;
def In16BitMode  : Predicate<"Subtarget->is16Bit()">,
                             AssemblerPredicate<(all_of Is16Bit), "16-bit mode">;
def Not16BitMode : Predicate<"!Subtarget->is16Bit()">,
                             AssemblerPredicate<(all_of (not Is16Bit)), "Not 16-bit mode">;
def In32BitMode  : Predicate<"Subtarget->is32Bit()">,
                             AssemblerPredicate<(all_of Is32Bit), "32-bit mode">;
def IsWin64      : Predicate<"Subtarget->isTargetWin64()">;
def NotWin64     : Predicate<"!Subtarget->isTargetWin64()">;
def NotWin64WithoutFP : Predicate<"!Subtarget->isTargetWin64() ||"
                                  "Subtarget->getFrameLowering()->hasFP(*MF)"> {
  let RecomputePerFunction = 1;
}
def IsPS         : Predicate<"Subtarget->isTargetPS()">;
def NotPS        : Predicate<"!Subtarget->isTargetPS()">;
def IsNaCl       : Predicate<"Subtarget->isTargetNaCl()">;
def NotNaCl      : Predicate<"!Subtarget->isTargetNaCl()">;
def SmallCode    : Predicate<"TM.getCodeModel() == CodeModel::Small">;
def KernelCode   : Predicate<"TM.getCodeModel() == CodeModel::Kernel">;
def NearData     : Predicate<"TM.getCodeModel() == CodeModel::Small ||"
                             "TM.getCodeModel() == CodeModel::Kernel">;
def IsNotPIC     : Predicate<"!TM.isPositionIndependent()">;

// We could compute these on a per-module basis but doing so requires accessing
// the Function object through the <Target>Subtarget and objections were raised
// to that (see post-commit review comments for r301750).
let RecomputePerFunction = 1 in {
  def OptForSize   : Predicate<"shouldOptForSize(MF)">;
  def OptForMinSize : Predicate<"MF->getFunction().hasMinSize()">;
  def OptForSpeed  : Predicate<"!shouldOptForSize(MF)">;
  def UseIncDec : Predicate<"!Subtarget->slowIncDec() || "
                            "shouldOptForSize(MF)">;
  def NoSSE41_Or_OptForSize : Predicate<"shouldOptForSize(MF) || "
                                        "!Subtarget->hasSSE41()">;
}

def CallImmAddr  : Predicate<"Subtarget->isLegalToCallImmediateAddr()">;
def FavorMemIndirectCall  : Predicate<"!Subtarget->slowTwoMemOps()">;
def HasFastMem32 : Predicate<"!Subtarget->isUnalignedMem32Slow()">;
def HasFastLZCNT : Predicate<"Subtarget->hasFastLZCNT()">;
def HasFastSHLDRotate : Predicate<"Subtarget->hasFastSHLDRotate()">;
def HasERMSB : Predicate<"Subtarget->hasERMSB()">;
def HasFSRM : Predicate<"Subtarget->hasFSRM()">;
def HasMFence    : Predicate<"Subtarget->hasMFence()">;
def HasFastDPWSSD: Predicate<"Subtarget->hasFastDPWSSD()">;
def UseIndirectThunkCalls : Predicate<"Subtarget->useIndirectThunkCalls()">;
def NotUseIndirectThunkCalls : Predicate<"!Subtarget->useIndirectThunkCalls()">;
