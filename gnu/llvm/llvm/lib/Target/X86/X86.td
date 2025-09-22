//===-- X86.td - Target definition file for the Intel X86 --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a target description file for the Intel i386 architecture, referred
// to here as the "X86" architecture.
//
//===----------------------------------------------------------------------===//

// Get the target-independent interfaces which we are implementing...
//
include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// X86 Subtarget state
//
// disregarding specific ABI / programming model
def Is64Bit : SubtargetFeature<"64bit-mode", "Is64Bit", "true",
                               "64-bit mode (x86_64)">;
def Is32Bit : SubtargetFeature<"32bit-mode", "Is32Bit", "true",
                               "32-bit mode (80386)">;
def Is16Bit : SubtargetFeature<"16bit-mode", "Is16Bit", "true",
                               "16-bit mode (i8086)">;

//===----------------------------------------------------------------------===//
// X86 Subtarget ISA features
//===----------------------------------------------------------------------===//

def FeatureX87     : SubtargetFeature<"x87","HasX87", "true",
                                      "Enable X87 float instructions">;

def FeatureNOPL    : SubtargetFeature<"nopl", "HasNOPL", "true",
                                      "Enable NOPL instruction (generally pentium pro+)">;

def FeatureCMOV    : SubtargetFeature<"cmov","HasCMOV", "true",
                                      "Enable conditional move instructions">;

def FeatureCX8     : SubtargetFeature<"cx8", "HasCX8", "true",
                                      "Support CMPXCHG8B instructions">;

def FeatureCRC32   : SubtargetFeature<"crc32", "HasCRC32", "true",
                                      "Enable SSE 4.2 CRC32 instruction (used when SSE4.2 is supported but function is GPR only)">;

def FeaturePOPCNT   : SubtargetFeature<"popcnt", "HasPOPCNT", "true",
                                       "Support POPCNT instruction">;

def FeatureFXSR    : SubtargetFeature<"fxsr", "HasFXSR", "true",
                                      "Support fxsave/fxrestore instructions">;

def FeatureXSAVE   : SubtargetFeature<"xsave", "HasXSAVE", "true",
                                       "Support xsave instructions">;

def FeatureXSAVEOPT: SubtargetFeature<"xsaveopt", "HasXSAVEOPT", "true",
                                       "Support xsaveopt instructions",
                                       [FeatureXSAVE]>;

def FeatureXSAVEC  : SubtargetFeature<"xsavec", "HasXSAVEC", "true",
                                       "Support xsavec instructions",
                                       [FeatureXSAVE]>;

def FeatureXSAVES  : SubtargetFeature<"xsaves", "HasXSAVES", "true",
                                       "Support xsaves instructions",
                                       [FeatureXSAVE]>;

def FeatureSSE1    : SubtargetFeature<"sse", "X86SSELevel", "SSE1",
                                      "Enable SSE instructions">;
def FeatureSSE2    : SubtargetFeature<"sse2", "X86SSELevel", "SSE2",
                                      "Enable SSE2 instructions",
                                      [FeatureSSE1]>;
def FeatureSSE3    : SubtargetFeature<"sse3", "X86SSELevel", "SSE3",
                                      "Enable SSE3 instructions",
                                      [FeatureSSE2]>;
def FeatureSSSE3   : SubtargetFeature<"ssse3", "X86SSELevel", "SSSE3",
                                      "Enable SSSE3 instructions",
                                      [FeatureSSE3]>;
def FeatureSSE41   : SubtargetFeature<"sse4.1", "X86SSELevel", "SSE41",
                                      "Enable SSE 4.1 instructions",
                                      [FeatureSSSE3]>;
def FeatureSSE42   : SubtargetFeature<"sse4.2", "X86SSELevel", "SSE42",
                                      "Enable SSE 4.2 instructions",
                                      [FeatureSSE41]>;
// The MMX subtarget feature is separate from the rest of the SSE features
// because it's important (for odd compatibility reasons) to be able to
// turn it off explicitly while allowing SSE+ to be on.
def FeatureMMX     : SubtargetFeature<"mmx","HasMMX", "true",
                                      "Enable MMX instructions">;
// All x86-64 hardware has SSE2, but we don't mark SSE2 as an implied
// feature, because SSE2 can be disabled (e.g. for compiling OS kernels)
// without disabling 64-bit mode. Nothing should imply this feature bit. It
// is used to enforce that only 64-bit capable CPUs are used in 64-bit mode.
def FeatureX86_64   : SubtargetFeature<"64bit", "HasX86_64", "true",
                                      "Support 64-bit instructions">;
def FeatureCX16     : SubtargetFeature<"cx16", "HasCX16", "true",
                                       "64-bit with cmpxchg16b (this is true for most x86-64 chips, but not the first AMD chips)",
                                       [FeatureCX8]>;
def FeatureSSE4A   : SubtargetFeature<"sse4a", "HasSSE4A", "true",
                                      "Support SSE 4a instructions",
                                      [FeatureSSE3]>;

def FeatureAVX     : SubtargetFeature<"avx", "X86SSELevel", "AVX",
                                      "Enable AVX instructions",
                                      [FeatureSSE42]>;
def FeatureAVX2    : SubtargetFeature<"avx2", "X86SSELevel", "AVX2",
                                      "Enable AVX2 instructions",
                                      [FeatureAVX]>;
def FeatureFMA     : SubtargetFeature<"fma", "HasFMA", "true",
                                      "Enable three-operand fused multiple-add",
                                      [FeatureAVX]>;
def FeatureF16C    : SubtargetFeature<"f16c", "HasF16C", "true",
                       "Support 16-bit floating point conversion instructions",
                       [FeatureAVX]>;
def FeatureEVEX512  : SubtargetFeature<"evex512", "HasEVEX512", "true",
                        "Support ZMM and 64-bit mask instructions">;
def FeatureAVX512   : SubtargetFeature<"avx512f", "X86SSELevel", "AVX512",
                                      "Enable AVX-512 instructions",
                                      [FeatureAVX2, FeatureFMA, FeatureF16C]>;
def FeatureCDI      : SubtargetFeature<"avx512cd", "HasCDI", "true",
                      "Enable AVX-512 Conflict Detection Instructions",
                                      [FeatureAVX512]>;
def FeatureVPOPCNTDQ : SubtargetFeature<"avx512vpopcntdq", "HasVPOPCNTDQ",
                       "true", "Enable AVX-512 Population Count Instructions",
                                      [FeatureAVX512]>;
def FeaturePREFETCHI  : SubtargetFeature<"prefetchi", "HasPREFETCHI",
                                   "true",
                                   "Prefetch instruction with T0 or T1 Hint">;
def FeatureDQI     : SubtargetFeature<"avx512dq", "HasDQI", "true",
                      "Enable AVX-512 Doubleword and Quadword Instructions",
                                      [FeatureAVX512]>;
def FeatureBWI     : SubtargetFeature<"avx512bw", "HasBWI", "true",
                      "Enable AVX-512 Byte and Word Instructions",
                                      [FeatureAVX512]>;
def FeatureVLX     : SubtargetFeature<"avx512vl", "HasVLX", "true",
                      "Enable AVX-512 Vector Length eXtensions",
                                      [FeatureAVX512]>;
def FeatureVBMI     : SubtargetFeature<"avx512vbmi", "HasVBMI", "true",
                      "Enable AVX-512 Vector Byte Manipulation Instructions",
                                      [FeatureBWI]>;
def FeatureVBMI2    : SubtargetFeature<"avx512vbmi2", "HasVBMI2", "true",
                      "Enable AVX-512 further Vector Byte Manipulation Instructions",
                                      [FeatureBWI]>;
def FeatureAVXIFMA    : SubtargetFeature<"avxifma", "HasAVXIFMA", "true",
                           "Enable AVX-IFMA",
                           [FeatureAVX2]>;
def FeatureIFMA     : SubtargetFeature<"avx512ifma", "HasIFMA", "true",
                      "Enable AVX-512 Integer Fused Multiple-Add",
                                      [FeatureAVX512]>;
def FeaturePKU   : SubtargetFeature<"pku", "HasPKU", "true",
                      "Enable protection keys">;
def FeatureVNNI    : SubtargetFeature<"avx512vnni", "HasVNNI", "true",
                          "Enable AVX-512 Vector Neural Network Instructions",
                                      [FeatureAVX512]>;
def FeatureAVXVNNI    : SubtargetFeature<"avxvnni", "HasAVXVNNI", "true",
                           "Support AVX_VNNI encoding",
                                      [FeatureAVX2]>;
def FeatureBF16    : SubtargetFeature<"avx512bf16", "HasBF16", "true",
                           "Support bfloat16 floating point",
                                      [FeatureBWI]>;
def FeatureBITALG  : SubtargetFeature<"avx512bitalg", "HasBITALG", "true",
                       "Enable AVX-512 Bit Algorithms",
                        [FeatureBWI]>;
def FeatureVP2INTERSECT  : SubtargetFeature<"avx512vp2intersect",
                                            "HasVP2INTERSECT", "true",
                                            "Enable AVX-512 vp2intersect",
                                            [FeatureAVX512]>;
// FIXME: FP16 scalar intrinsics use the type v8f16, which is supposed to be
// guarded under condition hasVLX. So we imply it in FeatureFP16 currently.
// FIXME: FP16 conversion between f16 and i64 customize type v8i64, which is
// supposed to be guarded under condition hasDQI. So we imply it in FeatureFP16
// currently.
def FeatureFP16    : SubtargetFeature<"avx512fp16", "HasFP16", "true",
                           "Support 16-bit floating point",
                           [FeatureBWI, FeatureVLX, FeatureDQI]>;
def FeatureAVXVNNIINT8  : SubtargetFeature<"avxvnniint8",
                             "HasAVXVNNIINT8", "true",
                             "Enable AVX-VNNI-INT8",
                             [FeatureAVX2]>;
def FeatureAVXVNNIINT16 : SubtargetFeature<"avxvnniint16",
                             "HasAVXVNNIINT16", "true",
                             "Enable AVX-VNNI-INT16",
                             [FeatureAVX2]>;
def FeaturePCLMUL  : SubtargetFeature<"pclmul", "HasPCLMUL", "true",
                         "Enable packed carry-less multiplication instructions",
                               [FeatureSSE2]>;
def FeatureGFNI    : SubtargetFeature<"gfni", "HasGFNI", "true",
                         "Enable Galois Field Arithmetic Instructions",
                               [FeatureSSE2]>;
def FeatureVPCLMULQDQ : SubtargetFeature<"vpclmulqdq", "HasVPCLMULQDQ", "true",
                                         "Enable vpclmulqdq instructions",
                                         [FeatureAVX, FeaturePCLMUL]>;
def FeatureFMA4    : SubtargetFeature<"fma4", "HasFMA4", "true",
                                      "Enable four-operand fused multiple-add",
                                      [FeatureAVX, FeatureSSE4A]>;
def FeatureXOP     : SubtargetFeature<"xop", "HasXOP", "true",
                                      "Enable XOP instructions",
                                      [FeatureFMA4]>;
def FeatureSSEUnalignedMem : SubtargetFeature<"sse-unaligned-mem",
                                          "HasSSEUnalignedMem", "true",
                      "Allow unaligned memory operands with SSE instructions (this may require setting a configuration bit in the processor)">;
def FeatureAES     : SubtargetFeature<"aes", "HasAES", "true",
                                      "Enable AES instructions",
                                      [FeatureSSE2]>;
def FeatureVAES    : SubtargetFeature<"vaes", "HasVAES", "true",
                       "Promote selected AES instructions to AVX512/AVX registers",
                        [FeatureAVX2, FeatureAES]>;
def FeatureTBM     : SubtargetFeature<"tbm", "HasTBM", "true",
                                      "Enable TBM instructions">;
def FeatureLWP     : SubtargetFeature<"lwp", "HasLWP", "true",
                                      "Enable LWP instructions">;
def FeatureMOVBE   : SubtargetFeature<"movbe", "HasMOVBE", "true",
                                      "Support MOVBE instruction">;
def FeatureRDRAND  : SubtargetFeature<"rdrnd", "HasRDRAND", "true",
                                      "Support RDRAND instruction">;
def FeatureFSGSBase : SubtargetFeature<"fsgsbase", "HasFSGSBase", "true",
                                       "Support FS/GS Base instructions">;
def FeatureLZCNT   : SubtargetFeature<"lzcnt", "HasLZCNT", "true",
                                      "Support LZCNT instruction">;
def FeatureBMI     : SubtargetFeature<"bmi", "HasBMI", "true",
                                      "Support BMI instructions">;
def FeatureBMI2    : SubtargetFeature<"bmi2", "HasBMI2", "true",
                                      "Support BMI2 instructions">;
def FeatureRTM     : SubtargetFeature<"rtm", "HasRTM", "true",
                                      "Support RTM instructions">;
def FeatureADX     : SubtargetFeature<"adx", "HasADX", "true",
                                      "Support ADX instructions">;
def FeatureSHA     : SubtargetFeature<"sha", "HasSHA", "true",
                                      "Enable SHA instructions",
                                      [FeatureSSE2]>;
def FeatureSHA512  : SubtargetFeature<"sha512", "HasSHA512", "true",
                                      "Support SHA512 instructions",
                                      [FeatureAVX2]>;
// Processor supports CET SHSTK - Control-Flow Enforcement Technology
// using Shadow Stack
def FeatureSHSTK   : SubtargetFeature<"shstk", "HasSHSTK", "true",
                       "Support CET Shadow-Stack instructions">;
def FeatureSM3     : SubtargetFeature<"sm3", "HasSM3", "true",
                                      "Support SM3 instructions",
                                      [FeatureAVX]>;
def FeatureSM4     : SubtargetFeature<"sm4", "HasSM4", "true",
                                      "Support SM4 instructions",
                                      [FeatureAVX2]>;
def FeaturePRFCHW  : SubtargetFeature<"prfchw", "HasPRFCHW", "true",
                                      "Support PRFCHW instructions">;
def FeatureRDSEED  : SubtargetFeature<"rdseed", "HasRDSEED", "true",
                                      "Support RDSEED instruction">;
def FeatureLAHFSAHF64 : SubtargetFeature<"sahf", "HasLAHFSAHF64", "true",
                           "Support LAHF and SAHF instructions in 64-bit mode">;
def FeatureMWAITX  : SubtargetFeature<"mwaitx", "HasMWAITX", "true",
                                      "Enable MONITORX/MWAITX timer functionality">;
def FeatureCLZERO  : SubtargetFeature<"clzero", "HasCLZERO", "true",
                                      "Enable Cache Line Zero">;
def FeatureCLDEMOTE  : SubtargetFeature<"cldemote", "HasCLDEMOTE", "true",
                                      "Enable Cache Line Demote">;
def FeaturePTWRITE  : SubtargetFeature<"ptwrite", "HasPTWRITE", "true",
                                      "Support ptwrite instruction">;
def FeatureAMXTILE     : SubtargetFeature<"amx-tile", "HasAMXTILE", "true",
                                      "Support AMX-TILE instructions">;
def FeatureAMXINT8     : SubtargetFeature<"amx-int8", "HasAMXINT8", "true",
                                      "Support AMX-INT8 instructions",
                                      [FeatureAMXTILE]>;
def FeatureAMXBF16     : SubtargetFeature<"amx-bf16", "HasAMXBF16", "true",
                                      "Support AMX-BF16 instructions",
                                      [FeatureAMXTILE]>;
def FeatureAMXFP16     : SubtargetFeature<"amx-fp16", "HasAMXFP16", "true",
                                      "Support AMX amx-fp16 instructions",
                                      [FeatureAMXTILE]>;
def FeatureAMXCOMPLEX : SubtargetFeature<"amx-complex", "HasAMXCOMPLEX", "true",
                                         "Support AMX-COMPLEX instructions",
                                         [FeatureAMXTILE]>;
def FeatureCMPCCXADD : SubtargetFeature<"cmpccxadd", "HasCMPCCXADD", "true",
                                        "Support CMPCCXADD instructions">;
def FeatureRAOINT : SubtargetFeature<"raoint", "HasRAOINT", "true",
                                     "Support RAO-INT instructions",
                                     []>;
def FeatureAVXNECONVERT : SubtargetFeature<"avxneconvert", "HasAVXNECONVERT", "true",
                                           "Support AVX-NE-CONVERT instructions",
                                           [FeatureAVX2]>;
def FeatureINVPCID : SubtargetFeature<"invpcid", "HasINVPCID", "true",
                                      "Invalidate Process-Context Identifier">;
def FeatureSGX     : SubtargetFeature<"sgx", "HasSGX", "true",
                                      "Enable Software Guard Extensions">;
def FeatureCLFLUSHOPT : SubtargetFeature<"clflushopt", "HasCLFLUSHOPT", "true",
                                      "Flush A Cache Line Optimized">;
def FeatureCLWB    : SubtargetFeature<"clwb", "HasCLWB", "true",
                                      "Cache Line Write Back">;
def FeatureWBNOINVD    : SubtargetFeature<"wbnoinvd", "HasWBNOINVD", "true",
                                      "Write Back No Invalidate">;
def FeatureRDPID : SubtargetFeature<"rdpid", "HasRDPID", "true",
                                    "Support RDPID instructions">;
def FeatureRDPRU : SubtargetFeature<"rdpru", "HasRDPRU", "true",
                                    "Support RDPRU instructions">;
def FeatureWAITPKG  : SubtargetFeature<"waitpkg", "HasWAITPKG", "true",
                                      "Wait and pause enhancements">;
def FeatureENQCMD : SubtargetFeature<"enqcmd", "HasENQCMD", "true",
                                     "Has ENQCMD instructions">;
def FeatureKL  : SubtargetFeature<"kl", "HasKL", "true",
                                  "Support Key Locker kl Instructions",
                                  [FeatureSSE2]>;
def FeatureWIDEKL  : SubtargetFeature<"widekl", "HasWIDEKL", "true",
                                      "Support Key Locker wide Instructions",
                                      [FeatureKL]>;
def FeatureHRESET : SubtargetFeature<"hreset", "HasHRESET", "true",
                                      "Has hreset instruction">;
def FeatureSERIALIZE : SubtargetFeature<"serialize", "HasSERIALIZE", "true",
                                        "Has serialize instruction">;
def FeatureTSXLDTRK : SubtargetFeature<"tsxldtrk", "HasTSXLDTRK", "true",
                                       "Support TSXLDTRK instructions">;
def FeatureUINTR : SubtargetFeature<"uintr", "HasUINTR", "true",
                                    "Has UINTR Instructions">;
def FeatureUSERMSR : SubtargetFeature<"usermsr", "HasUSERMSR", "true",
                                      "Support USERMSR instructions">;
def FeaturePCONFIG : SubtargetFeature<"pconfig", "HasPCONFIG", "true",
                                      "platform configuration instruction">;
def FeatureMOVDIRI  : SubtargetFeature<"movdiri", "HasMOVDIRI", "true",
                                       "Support movdiri instruction (direct store integer)">;
def FeatureMOVDIR64B : SubtargetFeature<"movdir64b", "HasMOVDIR64B", "true",
                                        "Support movdir64b instruction (direct store 64 bytes)">;
def FeatureAVX10_1 : SubtargetFeature<"avx10.1-256", "HasAVX10_1", "true",
                                      "Support AVX10.1 up to 256-bit instruction",
                                      [FeatureCDI, FeatureVBMI, FeatureIFMA, FeatureVNNI,
                                       FeatureBF16, FeatureVPOPCNTDQ, FeatureVBMI2, FeatureBITALG,
                                       FeatureVAES, FeatureVPCLMULQDQ, FeatureFP16]>;
def FeatureAVX10_1_512 : SubtargetFeature<"avx10.1-512", "HasAVX10_1_512", "true",
                                          "Support AVX10.1 up to 512-bit instruction",
                                          [FeatureAVX10_1, FeatureEVEX512]>;
def FeatureEGPR : SubtargetFeature<"egpr", "HasEGPR", "true",
                                   "Support extended general purpose register">;
def FeaturePush2Pop2 : SubtargetFeature<"push2pop2", "HasPush2Pop2", "true",
                                        "Support PUSH2/POP2 instructions">;
def FeaturePPX : SubtargetFeature<"ppx", "HasPPX", "true",
                                  "Support Push-Pop Acceleration">;
def FeatureNDD : SubtargetFeature<"ndd", "HasNDD", "true",
                                  "Support non-destructive destination">;
def FeatureCCMP : SubtargetFeature<"ccmp", "HasCCMP", "true",
                                   "Support conditional cmp & test instructions">;
def FeatureNF : SubtargetFeature<"nf", "HasNF", "true",
                                 "Support status flags update suppression">;
def FeatureCF : SubtargetFeature<"cf", "HasCF", "true",
                                 "Support conditional faulting">;
def FeatureZU : SubtargetFeature<"zu", "HasZU", "true",
                                 "Support zero-upper SETcc/IMUL">;
def FeatureUseGPR32InInlineAsm
    : SubtargetFeature<"inline-asm-use-gpr32", "UseInlineAsmGPR32", "true",
                       "Enable use of GPR32 in inline assembly for APX">;

// Ivy Bridge and newer processors have enhanced REP MOVSB and STOSB (aka
// "string operations"). See "REP String Enhancement" in the Intel Software
// Development Manual. This feature essentially means that REP MOVSB will copy
// using the largest available size instead of copying bytes one by one, making
// it at least as fast as REPMOVS{W,D,Q}.
def FeatureERMSB
    : SubtargetFeature<
          "ermsb", "HasERMSB", "true",
          "REP MOVS/STOS are fast">;

// Icelake and newer processors have Fast Short REP MOV.
def FeatureFSRM
    : SubtargetFeature<
          "fsrm", "HasFSRM", "true",
          "REP MOVSB of short lengths is faster">;

def FeatureSoftFloat
    : SubtargetFeature<"soft-float", "UseSoftFloat", "true",
                       "Use software floating point features">;

//===----------------------------------------------------------------------===//
// X86 Subtarget Security Mitigation features
//===----------------------------------------------------------------------===//

// Lower indirect calls using a special construct called a `retpoline` to
// mitigate potential Spectre v2 attacks against them.
def FeatureRetpolineIndirectCalls
    : SubtargetFeature<
          "retpoline-indirect-calls", "UseRetpolineIndirectCalls", "true",
          "Remove speculation of indirect calls from the generated code">;

// Lower indirect branches and switches either using conditional branch trees
// or using a special construct called a `retpoline` to mitigate potential
// Spectre v2 attacks against them.
def FeatureRetpolineIndirectBranches
    : SubtargetFeature<
          "retpoline-indirect-branches", "UseRetpolineIndirectBranches", "true",
          "Remove speculation of indirect branches from the generated code">;

// Deprecated umbrella feature for enabling both `retpoline-indirect-calls` and
// `retpoline-indirect-branches` above.
def FeatureRetpoline
    : SubtargetFeature<"retpoline", "DeprecatedUseRetpoline", "true",
                       "Remove speculation of indirect branches from the "
                       "generated code, either by avoiding them entirely or "
                       "lowering them with a speculation blocking construct",
                       [FeatureRetpolineIndirectCalls,
                        FeatureRetpolineIndirectBranches]>;

// Rely on external thunks for the emitted retpoline calls. This allows users
// to provide their own custom thunk definitions in highly specialized
// environments such as a kernel that does boot-time hot patching.
def FeatureRetpolineExternalThunk
    : SubtargetFeature<
          "retpoline-external-thunk", "UseRetpolineExternalThunk", "true",
          "When lowering an indirect call or branch using a `retpoline`, rely "
          "on the specified user provided thunk rather than emitting one "
          "ourselves. Only has effect when combined with some other retpoline "
          "feature", [FeatureRetpolineIndirectCalls]>;

// Mitigate LVI attacks against indirect calls/branches and call returns
def FeatureLVIControlFlowIntegrity
    : SubtargetFeature<
          "lvi-cfi", "UseLVIControlFlowIntegrity", "true",
          "Prevent indirect calls/branches from using a memory operand, and "
          "precede all indirect calls/branches from a register with an "
          "LFENCE instruction to serialize control flow. Also decompose RET "
          "instructions into a POP+LFENCE+JMP sequence.">;

// Enable SESES to mitigate speculative execution attacks
def FeatureSpeculativeExecutionSideEffectSuppression
    : SubtargetFeature<
          "seses", "UseSpeculativeExecutionSideEffectSuppression", "true",
          "Prevent speculative execution side channel timing attacks by "
          "inserting a speculation barrier before memory reads, memory writes, "
          "and conditional branches. Implies LVI Control Flow integrity.",
          [FeatureLVIControlFlowIntegrity]>;

// Mitigate LVI attacks against data loads
def FeatureLVILoadHardening
    : SubtargetFeature<
          "lvi-load-hardening", "UseLVILoadHardening", "true",
          "Insert LFENCE instructions to prevent data speculatively injected "
          "into loads from being used maliciously.">;

def FeatureTaggedGlobals
    : SubtargetFeature<
          "tagged-globals", "AllowTaggedGlobals", "true",
          "Use an instruction sequence for taking the address of a global "
          "that allows a memory tag in the upper address bits.">;

// Control codegen mitigation against Straight Line Speculation vulnerability.
def FeatureHardenSlsRet
    : SubtargetFeature<
          "harden-sls-ret", "HardenSlsRet", "true",
          "Harden against straight line speculation across RET instructions.">;

def FeatureHardenSlsIJmp
    : SubtargetFeature<
          "harden-sls-ijmp", "HardenSlsIJmp", "true",
          "Harden against straight line speculation across indirect JMP instructions.">;

//===----------------------------------------------------------------------===//
// X86 Subtarget Tuning features
//===----------------------------------------------------------------------===//
def TuningPreferMovmskOverVTest : SubtargetFeature<"prefer-movmsk-over-vtest",
                                       "PreferMovmskOverVTest", "true",
                                       "Prefer movmsk over vtest instruction">;

def TuningSlowSHLD : SubtargetFeature<"slow-shld", "IsSHLDSlow", "true",
                                       "SHLD instruction is slow">;

def TuningSlowPMULLD : SubtargetFeature<"slow-pmulld", "IsPMULLDSlow", "true",
                                        "PMULLD instruction is slow (compared to PMULLW/PMULHW and PMULUDQ)">;

def TuningSlowPMADDWD : SubtargetFeature<"slow-pmaddwd", "IsPMADDWDSlow",
                                          "true",
                                          "PMADDWD is slower than PMULLD">;

// FIXME: This should not apply to CPUs that do not have SSE.
def TuningSlowUAMem16 : SubtargetFeature<"slow-unaligned-mem-16",
                                "IsUnalignedMem16Slow", "true",
                                "Slow unaligned 16-byte memory access">;

def TuningSlowUAMem32 : SubtargetFeature<"slow-unaligned-mem-32",
                                "IsUnalignedMem32Slow", "true",
                                "Slow unaligned 32-byte memory access">;

def TuningLEAForSP : SubtargetFeature<"lea-sp", "UseLeaForSP", "true",
                                     "Use LEA for adjusting the stack pointer (this is an optimization for Intel Atom processors)">;

// True if 8-bit divisions are significantly faster than
// 32-bit divisions and should be used when possible.
def TuningSlowDivide32 : SubtargetFeature<"idivl-to-divb",
                                     "HasSlowDivide32", "true",
                                     "Use 8-bit divide for positive values less than 256">;

// True if 32-bit divides are significantly faster than
// 64-bit divisions and should be used when possible.
def TuningSlowDivide64 : SubtargetFeature<"idivq-to-divl",
                                     "HasSlowDivide64", "true",
                                     "Use 32-bit divide for positive values less than 2^32">;

def TuningPadShortFunctions : SubtargetFeature<"pad-short-functions",
                                     "PadShortFunctions", "true",
                                     "Pad short functions (to prevent a stall when returning too early)">;

// On some processors, instructions that implicitly take two memory operands are
// slow. In practice, this means that CALL, PUSH, and POP with memory operands
// should be avoided in favor of a MOV + register CALL/PUSH/POP.
def TuningSlowTwoMemOps : SubtargetFeature<"slow-two-mem-ops",
                                     "SlowTwoMemOps", "true",
                                     "Two memory operand instructions are slow">;

// True if the LEA instruction inputs have to be ready at address generation
// (AG) time.
def TuningLEAUsesAG : SubtargetFeature<"lea-uses-ag", "LeaUsesAG", "true",
                                   "LEA instruction needs inputs at AG stage">;

def TuningSlowLEA : SubtargetFeature<"slow-lea", "SlowLEA", "true",
                                   "LEA instruction with certain arguments is slow">;

// True if the LEA instruction has all three source operands: base, index,
// and offset or if the LEA instruction uses base and index registers where
// the base is EBP, RBP,or R13
def TuningSlow3OpsLEA : SubtargetFeature<"slow-3ops-lea", "Slow3OpsLEA", "true",
                                   "LEA instruction with 3 ops or certain registers is slow">;

// True if INC and DEC instructions are slow when writing to flags
def TuningSlowIncDec : SubtargetFeature<"slow-incdec", "SlowIncDec", "true",
                                   "INC and DEC instructions are slower than ADD and SUB">;

def TuningPOPCNTFalseDeps : SubtargetFeature<"false-deps-popcnt",
                                     "HasPOPCNTFalseDeps", "true",
                                     "POPCNT has a false dependency on dest register">;

def TuningLZCNTFalseDeps : SubtargetFeature<"false-deps-lzcnt-tzcnt",
                                     "HasLZCNTFalseDeps", "true",
                                     "LZCNT/TZCNT have a false dependency on dest register">;

def FeatureSaveArgs
    : SubtargetFeature<"save-args", "SaveArgs", "true",
                       "Save register arguments on the stack.">;

def TuningMULCFalseDeps : SubtargetFeature<"false-deps-mulc",
                               "HasMULCFalseDeps", "true",
                               "VF[C]MULCPH/SH has a false dependency on dest register">;

def TuningPERMFalseDeps : SubtargetFeature<"false-deps-perm",
                               "HasPERMFalseDeps", "true",
                               "VPERMD/Q/PS/PD has a false dependency on dest register">;

def TuningRANGEFalseDeps : SubtargetFeature<"false-deps-range",
                               "HasRANGEFalseDeps", "true",
                               "VRANGEPD/PS/SD/SS has a false dependency on dest register">;

def TuningGETMANTFalseDeps : SubtargetFeature<"false-deps-getmant",
                               "HasGETMANTFalseDeps", "true",
                               "VGETMANTSS/SD/SH and VGETMANDPS/PD(memory version) has a"
                               " false dependency on dest register">;

def TuningMULLQFalseDeps : SubtargetFeature<"false-deps-mullq",
                               "HasMULLQFalseDeps", "true",
                               "VPMULLQ has a false dependency on dest register">;

def TuningSBBDepBreaking : SubtargetFeature<"sbb-dep-breaking",
                                     "HasSBBDepBreaking", "true",
                                     "SBB with same register has no source dependency">;

// On recent X86 (port bound) processors, its preferable to combine to a single shuffle
// using a variable mask over multiple fixed shuffles.
def TuningFastVariableCrossLaneShuffle
    : SubtargetFeature<"fast-variable-crosslane-shuffle",
                       "HasFastVariableCrossLaneShuffle",
                       "true", "Cross-lane shuffles with variable masks are fast">;
def TuningFastVariablePerLaneShuffle
    : SubtargetFeature<"fast-variable-perlane-shuffle",
                       "HasFastVariablePerLaneShuffle",
                       "true", "Per-lane shuffles with variable masks are fast">;

// Goldmont / Tremont (atom in general) has no bypass delay
def TuningNoDomainDelay : SubtargetFeature<"no-bypass-delay",
                                   "NoDomainDelay","true",
                                   "Has no bypass delay when using the 'wrong' domain">;

// Many processors (Nehalem+ on Intel) have no bypass delay when
// using the wrong mov type.
def TuningNoDomainDelayMov : SubtargetFeature<"no-bypass-delay-mov",
                                   "NoDomainDelayMov","true",
                                   "Has no bypass delay when using the 'wrong' mov type">;

// Newer processors (Skylake+ on Intel) have no bypass delay when
// using the wrong blend type.
def TuningNoDomainDelayBlend : SubtargetFeature<"no-bypass-delay-blend",
                                   "NoDomainDelayBlend","true",
                                   "Has no bypass delay when using the 'wrong' blend type">;

// Newer processors (Haswell+ on Intel) have no bypass delay when
// using the wrong shuffle type.
def TuningNoDomainDelayShuffle : SubtargetFeature<"no-bypass-delay-shuffle",
                                   "NoDomainDelayShuffle","true",
                                   "Has no bypass delay when using the 'wrong' shuffle type">;

// Prefer lowering shuffles on AVX512 targets (e.g. Skylake Server) to
// imm shifts/rotate if they can use more ports than regular shuffles.
def TuningPreferShiftShuffle : SubtargetFeature<"faster-shift-than-shuffle",
                                   "PreferLowerShuffleAsShift", "true",
                                   "Shifts are faster (or as fast) as shuffle">;

def TuningFastImmVectorShift : SubtargetFeature<"tuning-fast-imm-vector-shift",
                                   "FastImmVectorShift", "true",
                                   "Vector shifts are fast (2/cycle) as opposed to slow (1/cycle)">;

// On some X86 processors, a vzeroupper instruction should be inserted after
// using ymm/zmm registers before executing code that may use SSE instructions.
def TuningInsertVZEROUPPER
    : SubtargetFeature<"vzeroupper",
                       "InsertVZEROUPPER",
                       "true", "Should insert vzeroupper instructions">;

// TuningFastScalarFSQRT should be enabled if scalar FSQRT has shorter latency
// than the corresponding NR code. TuningFastVectorFSQRT should be enabled if
// vector FSQRT has higher throughput than the corresponding NR code.
// The idea is that throughput bound code is likely to be vectorized, so for
// vectorized code we should care about the throughput of SQRT operations.
// But if the code is scalar that probably means that the code has some kind of
// dependency and we should care more about reducing the latency.

// True if hardware SQRTSS instruction is at least as fast (latency) as
// RSQRTSS followed by a Newton-Raphson iteration.
def TuningFastScalarFSQRT
    : SubtargetFeature<"fast-scalar-fsqrt", "HasFastScalarFSQRT",
                       "true", "Scalar SQRT is fast (disable Newton-Raphson)">;
// True if hardware SQRTPS/VSQRTPS instructions are at least as fast
// (throughput) as RSQRTPS/VRSQRTPS followed by a Newton-Raphson iteration.
def TuningFastVectorFSQRT
    : SubtargetFeature<"fast-vector-fsqrt", "HasFastVectorFSQRT",
                       "true", "Vector SQRT is fast (disable Newton-Raphson)">;

// If lzcnt has equivalent latency/throughput to most simple integer ops, it can
// be used to replace test/set sequences.
def TuningFastLZCNT
    : SubtargetFeature<
          "fast-lzcnt", "HasFastLZCNT", "true",
          "LZCNT instructions are as fast as most simple integer ops">;

// If the target can efficiently decode NOPs upto 7-bytes in length.
def TuningFast7ByteNOP
    : SubtargetFeature<
          "fast-7bytenop", "HasFast7ByteNOP", "true",
          "Target can quickly decode up to 7 byte NOPs">;

// If the target can efficiently decode NOPs upto 11-bytes in length.
def TuningFast11ByteNOP
    : SubtargetFeature<
          "fast-11bytenop", "HasFast11ByteNOP", "true",
          "Target can quickly decode up to 11 byte NOPs">;

// If the target can efficiently decode NOPs upto 15-bytes in length.
def TuningFast15ByteNOP
    : SubtargetFeature<
          "fast-15bytenop", "HasFast15ByteNOP", "true",
          "Target can quickly decode up to 15 byte NOPs">;

// Sandy Bridge and newer processors can use SHLD with the same source on both
// inputs to implement rotate to avoid the partial flag update of the normal
// rotate instructions.
def TuningFastSHLDRotate
    : SubtargetFeature<
          "fast-shld-rotate", "HasFastSHLDRotate", "true",
          "SHLD can be used as a faster rotate">;

// Bulldozer and newer processors can merge CMP/TEST (but not other
// instructions) with conditional branches.
def TuningBranchFusion
    : SubtargetFeature<"branchfusion", "HasBranchFusion", "true",
                 "CMP/TEST can be fused with conditional branches">;

// Sandy Bridge and newer processors have many instructions that can be
// fused with conditional branches and pass through the CPU as a single
// operation.
def TuningMacroFusion
    : SubtargetFeature<"macrofusion", "HasMacroFusion", "true",
                 "Various instructions can be fused with conditional branches">;

// Gather is available since Haswell (AVX2 set). So technically, we can
// generate Gathers on all AVX2 processors. But the overhead on HSW is high.
// Skylake Client processor has faster Gathers than HSW and performance is
// similar to Skylake Server (AVX-512).
def TuningFastGather
    : SubtargetFeature<"fast-gather", "HasFastGather", "true",
                       "Indicates if gather is reasonably fast (this is true for Skylake client and all AVX-512 CPUs)">;

// Generate vpdpwssd instead of vpmaddwd+vpaddd sequence.
def TuningFastDPWSSD
    : SubtargetFeature<
          "fast-dpwssd", "HasFastDPWSSD", "true",
          "Prefer vpdpwssd instruction over vpmaddwd+vpaddd instruction sequence">;

def TuningPreferNoGather
    : SubtargetFeature<"prefer-no-gather", "PreferGather", "false",
                       "Prefer no gather instructions">;
def TuningPreferNoScatter
    : SubtargetFeature<"prefer-no-scatter", "PreferScatter", "false",
                       "Prefer no scatter instructions">;

def TuningPrefer128Bit
    : SubtargetFeature<"prefer-128-bit", "Prefer128Bit", "true",
                       "Prefer 128-bit AVX instructions">;

def TuningPrefer256Bit
    : SubtargetFeature<"prefer-256-bit", "Prefer256Bit", "true",
                       "Prefer 256-bit AVX instructions">;

def TuningAllowLight256Bit
    : SubtargetFeature<"allow-light-256-bit", "AllowLight256Bit", "true",
                       "Enable generation of 256-bit load/stores even if we prefer 128-bit">;

def TuningPreferMaskRegisters
    : SubtargetFeature<"prefer-mask-registers", "PreferMaskRegisters", "true",
                       "Prefer AVX512 mask registers over PTEST/MOVMSK">;

def TuningFastBEXTR : SubtargetFeature<"fast-bextr", "HasFastBEXTR", "true",
          "Indicates that the BEXTR instruction is implemented as a single uop "
          "with good throughput">;

// Combine vector math operations with shuffles into horizontal math
// instructions if a CPU implements horizontal operations (introduced with
// SSE3) with better latency/throughput than the alternative sequence.
def TuningFastHorizontalOps
    : SubtargetFeature<
        "fast-hops", "HasFastHorizontalOps", "true",
        "Prefer horizontal vector math instructions (haddp, phsub, etc.) over "
        "normal vector instructions with shuffles">;

def TuningFastScalarShiftMasks
    : SubtargetFeature<
        "fast-scalar-shift-masks", "HasFastScalarShiftMasks", "true",
        "Prefer a left/right scalar logical shift pair over a shift+and pair">;

def TuningFastVectorShiftMasks
    : SubtargetFeature<
        "fast-vector-shift-masks", "HasFastVectorShiftMasks", "true",
        "Prefer a left/right vector logical shift pair over a shift+and pair">;

def TuningFastMOVBE
    : SubtargetFeature<"fast-movbe", "HasFastMOVBE", "true",
    "Prefer a movbe over a single-use load + bswap / single-use bswap + store">;

def TuningFastImm16
    : SubtargetFeature<"fast-imm16", "HasFastImm16", "true",
    "Prefer a i16 instruction with i16 immediate over extension to i32">;

def TuningUseSLMArithCosts
    : SubtargetFeature<"use-slm-arith-costs", "UseSLMArithCosts", "true",
        "Use Silvermont specific arithmetic costs">;

def TuningUseGLMDivSqrtCosts
    : SubtargetFeature<"use-glm-div-sqrt-costs", "UseGLMDivSqrtCosts", "true",
        "Use Goldmont specific floating point div/sqrt costs">;

// Starting with Redwood Cove architecture, the branch has branch taken hint
// (i.e., instruction prefix 3EH).
def TuningBranchHint: SubtargetFeature<"branch-hint", "HasBranchHint", "true",
                                        "Target has branch hint feature">;

//===----------------------------------------------------------------------===//
// X86 CPU Families
// TODO: Remove these - use general tuning features to determine codegen.
//===----------------------------------------------------------------------===//

// Bonnell
def ProcIntelAtom : SubtargetFeature<"", "IsAtom", "true", "Is Intel Atom processor">;

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "X86RegisterInfo.td"
include "X86RegisterBanks.td"

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

include "X86Schedule.td"
include "X86InstrInfo.td"
include "X86SchedPredicates.td"

def X86InstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// X86 Scheduler Models
//===----------------------------------------------------------------------===//

include "X86ScheduleAtom.td"
include "X86SchedSandyBridge.td"
include "X86SchedHaswell.td"
include "X86SchedBroadwell.td"
include "X86ScheduleSLM.td"
include "X86ScheduleZnver1.td"
include "X86ScheduleZnver2.td"
include "X86ScheduleZnver3.td"
include "X86ScheduleZnver4.td"
include "X86ScheduleBdVer2.td"
include "X86ScheduleBtVer2.td"
include "X86SchedSkylakeClient.td"
include "X86SchedSkylakeServer.td"
include "X86SchedIceLake.td"
include "X86SchedAlderlakeP.td"
include "X86SchedSapphireRapids.td"

//===----------------------------------------------------------------------===//
// X86 Processor Feature Lists
//===----------------------------------------------------------------------===//

def ProcessorFeatures {
  // x86-64 micro-architecture levels: x86-64 and x86-64-v[234]
  list<SubtargetFeature> X86_64V1Features = [
    FeatureX87, FeatureCX8, FeatureCMOV, FeatureMMX, FeatureSSE2,
    FeatureFXSR, FeatureNOPL, FeatureX86_64,
  ];
  list<SubtargetFeature> X86_64V1Tuning = [
    TuningMacroFusion,
    TuningSlow3OpsLEA,
    TuningSlowDivide64,
    TuningSlowIncDec,
    TuningInsertVZEROUPPER
  ];

  list<SubtargetFeature> X86_64V2Features = !listconcat(X86_64V1Features, [
    FeatureCX16, FeatureLAHFSAHF64, FeatureCRC32, FeaturePOPCNT,
    FeatureSSE42
  ]);
  list<SubtargetFeature> X86_64V2Tuning = [
    TuningMacroFusion,
    TuningSlow3OpsLEA,
    TuningSlowDivide64,
    TuningSlowUAMem32,
    TuningFastScalarFSQRT,
    TuningFastSHLDRotate,
    TuningFast15ByteNOP,
    TuningPOPCNTFalseDeps,
    TuningInsertVZEROUPPER
  ];

  list<SubtargetFeature> X86_64V3Features = !listconcat(X86_64V2Features, [
    FeatureAVX2, FeatureBMI, FeatureBMI2, FeatureF16C, FeatureFMA, FeatureLZCNT,
    FeatureMOVBE, FeatureXSAVE
  ]);
  list<SubtargetFeature> X86_64V3Tuning = [
    TuningMacroFusion,
    TuningSlow3OpsLEA,
    TuningSlowDivide64,
    TuningFastScalarFSQRT,
    TuningFastSHLDRotate,
    TuningFast15ByteNOP,
    TuningFastVariableCrossLaneShuffle,
    TuningFastVariablePerLaneShuffle,
    TuningPOPCNTFalseDeps,
    TuningLZCNTFalseDeps,
    TuningInsertVZEROUPPER,
    TuningAllowLight256Bit
  ];

  list<SubtargetFeature> X86_64V4Features = !listconcat(X86_64V3Features, [
    FeatureEVEX512,
    FeatureBWI,
    FeatureCDI,
    FeatureDQI,
    FeatureVLX,
  ]);
  list<SubtargetFeature> X86_64V4Tuning = [
    TuningMacroFusion,
    TuningSlow3OpsLEA,
    TuningSlowDivide64,
    TuningFastScalarFSQRT,
    TuningFastVectorFSQRT,
    TuningFastSHLDRotate,
    TuningFast15ByteNOP,
    TuningFastVariableCrossLaneShuffle,
    TuningFastVariablePerLaneShuffle,
    TuningPrefer256Bit,
    TuningFastGather,
    TuningPOPCNTFalseDeps,
    TuningInsertVZEROUPPER,
    TuningAllowLight256Bit
  ];

  // Nehalem
  list<SubtargetFeature> NHMFeatures = X86_64V2Features;
  list<SubtargetFeature> NHMTuning = [TuningMacroFusion,
                                      TuningSlowDivide64,
                                      TuningInsertVZEROUPPER,
                                      TuningNoDomainDelayMov];

  // Westmere
  list<SubtargetFeature> WSMAdditionalFeatures = [FeaturePCLMUL];
  list<SubtargetFeature> WSMTuning = NHMTuning;
  list<SubtargetFeature> WSMFeatures =
    !listconcat(NHMFeatures, WSMAdditionalFeatures);

  // Sandybridge
  list<SubtargetFeature> SNBAdditionalFeatures = [FeatureAVX,
                                                  FeatureXSAVE,
                                                  FeatureXSAVEOPT];
  list<SubtargetFeature> SNBTuning = [TuningMacroFusion,
                                      TuningSlow3OpsLEA,
                                      TuningSlowDivide64,
                                      TuningSlowUAMem32,
                                      TuningFastScalarFSQRT,
                                      TuningFastSHLDRotate,
                                      TuningFast15ByteNOP,
                                      TuningPOPCNTFalseDeps,
                                      TuningInsertVZEROUPPER,
                                      TuningNoDomainDelayMov];
  list<SubtargetFeature> SNBFeatures =
    !listconcat(WSMFeatures, SNBAdditionalFeatures);

  // Ivybridge
  list<SubtargetFeature> IVBAdditionalFeatures = [FeatureRDRAND,
                                                  FeatureF16C,
                                                  FeatureFSGSBase];
  list<SubtargetFeature> IVBTuning = SNBTuning;
  list<SubtargetFeature> IVBFeatures =
    !listconcat(SNBFeatures, IVBAdditionalFeatures);

  // Haswell
  list<SubtargetFeature> HSWAdditionalFeatures = [FeatureAVX2,
                                                  FeatureBMI,
                                                  FeatureBMI2,
                                                  FeatureERMSB,
                                                  FeatureFMA,
                                                  FeatureINVPCID,
                                                  FeatureLZCNT,
                                                  FeatureMOVBE];
  list<SubtargetFeature> HSWTuning = [TuningMacroFusion,
                                      TuningSlow3OpsLEA,
                                      TuningSlowDivide64,
                                      TuningFastScalarFSQRT,
                                      TuningFastSHLDRotate,
                                      TuningFast15ByteNOP,
                                      TuningFastVariableCrossLaneShuffle,
                                      TuningFastVariablePerLaneShuffle,
                                      TuningPOPCNTFalseDeps,
                                      TuningLZCNTFalseDeps,
                                      TuningInsertVZEROUPPER,
                                      TuningAllowLight256Bit,
                                      TuningNoDomainDelayMov,
                                      TuningNoDomainDelayShuffle];
  list<SubtargetFeature> HSWFeatures =
    !listconcat(IVBFeatures, HSWAdditionalFeatures);

  // Broadwell
  list<SubtargetFeature> BDWAdditionalFeatures = [FeatureADX,
                                                  FeatureRDSEED,
                                                  FeaturePRFCHW];
  list<SubtargetFeature> BDWTuning = HSWTuning;
  list<SubtargetFeature> BDWFeatures =
    !listconcat(HSWFeatures, BDWAdditionalFeatures);

  // Skylake
  list<SubtargetFeature> SKLAdditionalFeatures = [FeatureAES,
                                                  FeatureXSAVEC,
                                                  FeatureXSAVES,
                                                  FeatureCLFLUSHOPT];
  list<SubtargetFeature> SKLTuning = [TuningFastGather,
                                      TuningMacroFusion,
                                      TuningSlow3OpsLEA,
                                      TuningSlowDivide64,
                                      TuningFastScalarFSQRT,
                                      TuningFastVectorFSQRT,
                                      TuningFastSHLDRotate,
                                      TuningFast15ByteNOP,
                                      TuningFastVariableCrossLaneShuffle,
                                      TuningFastVariablePerLaneShuffle,
                                      TuningPOPCNTFalseDeps,
                                      TuningInsertVZEROUPPER,
                                      TuningAllowLight256Bit,
                                      TuningNoDomainDelayMov,
                                      TuningNoDomainDelayShuffle,
                                      TuningNoDomainDelayBlend];
  list<SubtargetFeature> SKLFeatures =
    !listconcat(BDWFeatures, SKLAdditionalFeatures);

  // Skylake-AVX512
  list<SubtargetFeature> SKXAdditionalFeatures = [FeatureAES,
                                                  FeatureXSAVEC,
                                                  FeatureXSAVES,
                                                  FeatureCLFLUSHOPT,
                                                  FeatureAVX512,
                                                  FeatureEVEX512,
                                                  FeatureCDI,
                                                  FeatureDQI,
                                                  FeatureBWI,
                                                  FeatureVLX,
                                                  FeaturePKU,
                                                  FeatureCLWB];
  list<SubtargetFeature> SKXTuning = [TuningFastGather,
                                      TuningMacroFusion,
                                      TuningSlow3OpsLEA,
                                      TuningSlowDivide64,
                                      TuningFastScalarFSQRT,
                                      TuningFastVectorFSQRT,
                                      TuningFastSHLDRotate,
                                      TuningFast15ByteNOP,
                                      TuningFastVariableCrossLaneShuffle,
                                      TuningFastVariablePerLaneShuffle,
                                      TuningPrefer256Bit,
                                      TuningPOPCNTFalseDeps,
                                      TuningInsertVZEROUPPER,
                                      TuningAllowLight256Bit,
                                      TuningPreferShiftShuffle,
                                      TuningNoDomainDelayMov,
                                      TuningNoDomainDelayShuffle,
                                      TuningNoDomainDelayBlend,
                                      TuningFastImmVectorShift];
  list<SubtargetFeature> SKXFeatures =
    !listconcat(BDWFeatures, SKXAdditionalFeatures);

  // Cascadelake
  list<SubtargetFeature> CLXAdditionalFeatures = [FeatureVNNI];
  list<SubtargetFeature> CLXTuning = SKXTuning;
  list<SubtargetFeature> CLXFeatures =
    !listconcat(SKXFeatures, CLXAdditionalFeatures);

  // Cooperlake
  list<SubtargetFeature> CPXAdditionalFeatures = [FeatureBF16];
  list<SubtargetFeature> CPXTuning = SKXTuning;
  list<SubtargetFeature> CPXFeatures =
    !listconcat(CLXFeatures, CPXAdditionalFeatures);

  // Cannonlake
  list<SubtargetFeature> CNLAdditionalFeatures = [FeatureAVX512,
                                                  FeatureEVEX512,
                                                  FeatureCDI,
                                                  FeatureDQI,
                                                  FeatureBWI,
                                                  FeatureVLX,
                                                  FeaturePKU,
                                                  FeatureVBMI,
                                                  FeatureIFMA,
                                                  FeatureSHA];
  list<SubtargetFeature> CNLTuning = [TuningFastGather,
                                      TuningMacroFusion,
                                      TuningSlow3OpsLEA,
                                      TuningSlowDivide64,
                                      TuningFastScalarFSQRT,
                                      TuningFastVectorFSQRT,
                                      TuningFastSHLDRotate,
                                      TuningFast15ByteNOP,
                                      TuningFastVariableCrossLaneShuffle,
                                      TuningFastVariablePerLaneShuffle,
                                      TuningPrefer256Bit,
                                      TuningInsertVZEROUPPER,
                                      TuningAllowLight256Bit,
                                      TuningNoDomainDelayMov,
                                      TuningNoDomainDelayShuffle,
                                      TuningNoDomainDelayBlend,
                                      TuningFastImmVectorShift];
  list<SubtargetFeature> CNLFeatures =
    !listconcat(SKLFeatures, CNLAdditionalFeatures);

  // Icelake
  list<SubtargetFeature> ICLAdditionalFeatures = [FeatureBITALG,
                                                  FeatureVAES,
                                                  FeatureVBMI2,
                                                  FeatureVNNI,
                                                  FeatureVPCLMULQDQ,
                                                  FeatureVPOPCNTDQ,
                                                  FeatureGFNI,
                                                  FeatureRDPID,
                                                  FeatureFSRM];
  list<SubtargetFeature> ICLTuning = [TuningFastGather,
                                      TuningMacroFusion,
                                      TuningSlowDivide64,
                                      TuningFastScalarFSQRT,
                                      TuningFastVectorFSQRT,
                                      TuningFastSHLDRotate,
                                      TuningFast15ByteNOP,
                                      TuningFastVariableCrossLaneShuffle,
                                      TuningFastVariablePerLaneShuffle,
                                      TuningPrefer256Bit,
                                      TuningInsertVZEROUPPER,
                                      TuningAllowLight256Bit,
                                      TuningNoDomainDelayMov,
                                      TuningNoDomainDelayShuffle,
                                      TuningNoDomainDelayBlend,
                                      TuningFastImmVectorShift];
  list<SubtargetFeature> ICLFeatures =
    !listconcat(CNLFeatures, ICLAdditionalFeatures);

  // Icelake Server
  list<SubtargetFeature> ICXAdditionalFeatures = [FeaturePCONFIG,
                                                  FeatureCLWB,
                                                  FeatureWBNOINVD];
  list<SubtargetFeature> ICXTuning = ICLTuning;
  list<SubtargetFeature> ICXFeatures =
    !listconcat(ICLFeatures, ICXAdditionalFeatures);

  // Tigerlake
  list<SubtargetFeature> TGLAdditionalFeatures = [FeatureVP2INTERSECT,
                                                  FeatureCLWB,
                                                  FeatureMOVDIRI,
                                                  FeatureMOVDIR64B,
                                                  FeatureSHSTK];
  list<SubtargetFeature> TGLTuning = ICLTuning;
  list<SubtargetFeature> TGLFeatures =
    !listconcat(ICLFeatures, TGLAdditionalFeatures );

  // Sapphirerapids
  list<SubtargetFeature> SPRAdditionalFeatures = [FeatureAMXTILE,
                                                  FeatureAMXINT8,
                                                  FeatureAMXBF16,
                                                  FeatureBF16,
                                                  FeatureSERIALIZE,
                                                  FeatureCLDEMOTE,
                                                  FeatureWAITPKG,
                                                  FeaturePTWRITE,
                                                  FeatureFP16,
                                                  FeatureAVXVNNI,
                                                  FeatureTSXLDTRK,
                                                  FeatureENQCMD,
                                                  FeatureSHSTK,
                                                  FeatureMOVDIRI,
                                                  FeatureMOVDIR64B,
                                                  FeatureUINTR];
  list<SubtargetFeature> SPRAdditionalTuning = [TuningMULCFalseDeps,
                                                TuningPERMFalseDeps,
                                                TuningRANGEFalseDeps,
                                                TuningGETMANTFalseDeps,
                                                TuningMULLQFalseDeps];
  list<SubtargetFeature> SPRTuning = !listconcat(ICXTuning, SPRAdditionalTuning);
  list<SubtargetFeature> SPRFeatures =
    !listconcat(ICXFeatures, SPRAdditionalFeatures);

  // Graniterapids
  list<SubtargetFeature> GNRAdditionalFeatures = [FeatureAMXFP16,
                                                  FeaturePREFETCHI];
  list<SubtargetFeature> GNRFeatures =
    !listconcat(SPRFeatures, GNRAdditionalFeatures);
  list<SubtargetFeature> GNRAdditionalTuning = [TuningBranchHint];
  list<SubtargetFeature> GNRTuning = !listconcat(SPRTuning, GNRAdditionalTuning);

  // Graniterapids D
  list<SubtargetFeature> GNRDAdditionalFeatures = [FeatureAMXCOMPLEX];
  list<SubtargetFeature> GNRDFeatures =
    !listconcat(GNRFeatures, GNRDAdditionalFeatures);

  // Atom
  list<SubtargetFeature> AtomFeatures = [FeatureX87,
                                         FeatureCX8,
                                         FeatureCMOV,
                                         FeatureMMX,
                                         FeatureSSSE3,
                                         FeatureFXSR,
                                         FeatureNOPL,
                                         FeatureX86_64,
                                         FeatureCX16,
                                         FeatureMOVBE,
                                         FeatureLAHFSAHF64];
  list<SubtargetFeature> AtomTuning = [ProcIntelAtom,
                                       TuningSlowUAMem16,
                                       TuningLEAForSP,
                                       TuningSlowDivide32,
                                       TuningSlowDivide64,
                                       TuningSlowTwoMemOps,
                                       TuningFastImm16,
                                       TuningLEAUsesAG,
                                       TuningPadShortFunctions,
                                       TuningInsertVZEROUPPER,
                                       TuningNoDomainDelay];

  // Silvermont
  list<SubtargetFeature> SLMAdditionalFeatures = [FeatureSSE42,
                                                  FeatureCRC32,
                                                  FeaturePOPCNT,
                                                  FeaturePCLMUL,
                                                  FeaturePRFCHW,
                                                  FeatureRDRAND];
  list<SubtargetFeature> SLMTuning = [TuningUseSLMArithCosts,
                                      TuningSlowTwoMemOps,
                                      TuningSlowLEA,
                                      TuningSlowIncDec,
                                      TuningSlowDivide64,
                                      TuningSlowPMULLD,
                                      TuningFast7ByteNOP,
                                      TuningFastMOVBE,
                                      TuningFastImm16,
                                      TuningPOPCNTFalseDeps,
                                      TuningInsertVZEROUPPER,
                                      TuningNoDomainDelay];
  list<SubtargetFeature> SLMFeatures =
    !listconcat(AtomFeatures, SLMAdditionalFeatures);

  // Goldmont
  list<SubtargetFeature> GLMAdditionalFeatures = [FeatureAES,
                                                  FeatureSHA,
                                                  FeatureRDSEED,
                                                  FeatureXSAVE,
                                                  FeatureXSAVEOPT,
                                                  FeatureXSAVEC,
                                                  FeatureXSAVES,
                                                  FeatureCLFLUSHOPT,
                                                  FeatureFSGSBase];
  list<SubtargetFeature> GLMTuning = [TuningUseGLMDivSqrtCosts,
                                      TuningSlowTwoMemOps,
                                      TuningSlowLEA,
                                      TuningSlowIncDec,
                                      TuningFastMOVBE,
                                      TuningFastImm16,
                                      TuningPOPCNTFalseDeps,
                                      TuningInsertVZEROUPPER,
                                      TuningNoDomainDelay];
  list<SubtargetFeature> GLMFeatures =
    !listconcat(SLMFeatures, GLMAdditionalFeatures);

  // Goldmont Plus
  list<SubtargetFeature> GLPAdditionalFeatures = [FeaturePTWRITE,
                                                  FeatureRDPID];
  list<SubtargetFeature> GLPTuning = [TuningUseGLMDivSqrtCosts,
                                      TuningSlowTwoMemOps,
                                      TuningSlowLEA,
                                      TuningSlowIncDec,
                                      TuningFastMOVBE,
                                      TuningFastImm16,
                                      TuningInsertVZEROUPPER,
                                      TuningNoDomainDelay];
  list<SubtargetFeature> GLPFeatures =
    !listconcat(GLMFeatures, GLPAdditionalFeatures);

  // Tremont
  list<SubtargetFeature> TRMAdditionalFeatures = [FeatureCLWB,
                                                  FeatureGFNI];
  list<SubtargetFeature> TRMTuning = GLPTuning;
  list<SubtargetFeature> TRMFeatures =
    !listconcat(GLPFeatures, TRMAdditionalFeatures);

  // Alderlake
  list<SubtargetFeature> ADLAdditionalFeatures = [FeatureSERIALIZE,
                                                  FeaturePCONFIG,
                                                  FeatureSHSTK,
                                                  FeatureWIDEKL,
                                                  FeatureINVPCID,
                                                  FeatureADX,
                                                  FeatureFMA,
                                                  FeatureVAES,
                                                  FeatureVPCLMULQDQ,
                                                  FeatureF16C,
                                                  FeatureBMI,
                                                  FeatureBMI2,
                                                  FeatureLZCNT,
                                                  FeatureAVXVNNI,
                                                  FeaturePKU,
                                                  FeatureHRESET,
                                                  FeatureCLDEMOTE,
                                                  FeatureMOVDIRI,
                                                  FeatureMOVDIR64B,
                                                  FeatureWAITPKG];
  list<SubtargetFeature> ADLAdditionalTuning = [TuningPERMFalseDeps,
                                                TuningPreferMovmskOverVTest,
                                                TuningFastImmVectorShift];
  list<SubtargetFeature> ADLTuning = !listconcat(SKLTuning, ADLAdditionalTuning);
  list<SubtargetFeature> ADLFeatures =
    !listconcat(TRMFeatures, ADLAdditionalFeatures);

  // Gracemont
  list<SubtargetFeature> GRTTuning = [TuningMacroFusion,
                                      TuningSlow3OpsLEA,
                                      TuningFastScalarFSQRT,
                                      TuningFastVectorFSQRT,
                                      TuningFast15ByteNOP,
                                      TuningFastVariablePerLaneShuffle,
                                      TuningPOPCNTFalseDeps,
                                      TuningInsertVZEROUPPER];

  // Sierraforest
  list<SubtargetFeature> SRFAdditionalFeatures = [FeatureCMPCCXADD,
                                                  FeatureAVXIFMA,
                                                  FeatureAVXNECONVERT,
                                                  FeatureENQCMD,
                                                  FeatureUINTR,
                                                  FeatureAVXVNNIINT8];
  list<SubtargetFeature> SRFFeatures =
    !listconcat(ADLFeatures, SRFAdditionalFeatures);

  // Arrowlake S
  list<SubtargetFeature> ARLSAdditionalFeatures = [FeatureAVXVNNIINT16,
                                                   FeatureSHA512,
                                                   FeatureSM3,
                                                   FeatureSM4];
  list<SubtargetFeature> ARLSFeatures =
    !listconcat(SRFFeatures, ARLSAdditionalFeatures);

  // Pantherlake
  list<SubtargetFeature> PTLAdditionalFeatures = [FeaturePREFETCHI];
  list<SubtargetFeature> PTLFeatures =
    !listconcat(ARLSFeatures, PTLAdditionalFeatures);


  // Clearwaterforest
  list<SubtargetFeature> CWFAdditionalFeatures = [FeaturePREFETCHI,
                                                  FeatureUSERMSR];
  list<SubtargetFeature> CWFFeatures =
    !listconcat(ARLSFeatures, CWFAdditionalFeatures);

  // Knights Landing
  list<SubtargetFeature> KNLFeatures = [FeatureX87,
                                        FeatureCX8,
                                        FeatureCMOV,
                                        FeatureMMX,
                                        FeatureFXSR,
                                        FeatureNOPL,
                                        FeatureX86_64,
                                        FeatureCX16,
                                        FeatureCRC32,
                                        FeaturePOPCNT,
                                        FeaturePCLMUL,
                                        FeatureXSAVE,
                                        FeatureXSAVEOPT,
                                        FeatureLAHFSAHF64,
                                        FeatureAES,
                                        FeatureRDRAND,
                                        FeatureF16C,
                                        FeatureFSGSBase,
                                        FeatureAVX512,
                                        FeatureEVEX512,
                                        FeatureCDI,
                                        FeatureADX,
                                        FeatureRDSEED,
                                        FeatureMOVBE,
                                        FeatureLZCNT,
                                        FeatureBMI,
                                        FeatureBMI2,
                                        FeatureFMA,
                                        FeaturePRFCHW];
  list<SubtargetFeature> KNLTuning = [TuningSlowDivide64,
                                      TuningSlow3OpsLEA,
                                      TuningSlowIncDec,
                                      TuningSlowTwoMemOps,
                                      TuningPreferMaskRegisters,
                                      TuningFastGather,
                                      TuningFastMOVBE,
                                      TuningFastImm16,
                                      TuningSlowPMADDWD];
  // TODO Add AVX5124FMAPS/AVX5124VNNIW features
  list<SubtargetFeature> KNMFeatures =
    !listconcat(KNLFeatures, [FeatureVPOPCNTDQ]);

  // Barcelona
  list<SubtargetFeature> BarcelonaFeatures = [FeatureX87,
                                              FeatureCX8,
                                              FeatureSSE4A,
                                              FeatureFXSR,
                                              FeatureNOPL,
                                              FeatureCX16,
                                              FeaturePRFCHW,
                                              FeatureLZCNT,
                                              FeaturePOPCNT,
                                              FeatureLAHFSAHF64,
                                              FeatureCMOV,
                                              FeatureX86_64];
  list<SubtargetFeature> BarcelonaTuning = [TuningFastScalarShiftMasks,
                                            TuningSlowDivide64,
                                            TuningSlowSHLD,
                                            TuningSBBDepBreaking,
                                            TuningInsertVZEROUPPER];

  // Bobcat
  list<SubtargetFeature> BtVer1Features = [FeatureX87,
                                           FeatureCX8,
                                           FeatureCMOV,
                                           FeatureMMX,
                                           FeatureSSSE3,
                                           FeatureSSE4A,
                                           FeatureFXSR,
                                           FeatureNOPL,
                                           FeatureX86_64,
                                           FeatureCX16,
                                           FeaturePRFCHW,
                                           FeatureLZCNT,
                                           FeaturePOPCNT,
                                           FeatureLAHFSAHF64];
  list<SubtargetFeature> BtVer1Tuning = [TuningFast15ByteNOP,
                                         TuningFastScalarShiftMasks,
                                         TuningFastVectorShiftMasks,
                                         TuningSlowDivide64,
                                         TuningSlowSHLD,
                                         TuningFastImm16,
                                         TuningSBBDepBreaking,
                                         TuningInsertVZEROUPPER];

  // Jaguar
  list<SubtargetFeature> BtVer2AdditionalFeatures = [FeatureAVX,
                                                     FeatureAES,
                                                     FeatureCRC32,
                                                     FeaturePCLMUL,
                                                     FeatureBMI,
                                                     FeatureF16C,
                                                     FeatureMOVBE,
                                                     FeatureXSAVE,
                                                     FeatureXSAVEOPT];
  list<SubtargetFeature> BtVer2Tuning = [TuningFastLZCNT,
                                         TuningFastBEXTR,
                                         TuningFastHorizontalOps,
                                         TuningFast15ByteNOP,
                                         TuningFastScalarShiftMasks,
                                         TuningFastVectorShiftMasks,
                                         TuningFastMOVBE,
                                         TuningFastImm16,
                                         TuningSBBDepBreaking,
                                         TuningSlowDivide64,
                                         TuningSlowSHLD];
  list<SubtargetFeature> BtVer2Features =
    !listconcat(BtVer1Features, BtVer2AdditionalFeatures);

  // Bulldozer
  list<SubtargetFeature> BdVer1Features = [FeatureX87,
                                           FeatureCX8,
                                           FeatureCMOV,
                                           FeatureXOP,
                                           FeatureX86_64,
                                           FeatureCX16,
                                           FeatureAES,
                                           FeatureCRC32,
                                           FeaturePRFCHW,
                                           FeaturePCLMUL,
                                           FeatureMMX,
                                           FeatureFXSR,
                                           FeatureNOPL,
                                           FeatureLZCNT,
                                           FeaturePOPCNT,
                                           FeatureXSAVE,
                                           FeatureLWP,
                                           FeatureLAHFSAHF64];
  list<SubtargetFeature> BdVer1Tuning = [TuningSlowSHLD,
                                         TuningSlowDivide64,
                                         TuningFast11ByteNOP,
                                         TuningFastScalarShiftMasks,
                                         TuningBranchFusion,
                                         TuningSBBDepBreaking,
                                         TuningInsertVZEROUPPER];

  // PileDriver
  list<SubtargetFeature> BdVer2AdditionalFeatures = [FeatureF16C,
                                                     FeatureBMI,
                                                     FeatureTBM,
                                                     FeatureFMA];
  list<SubtargetFeature> BdVer2AdditionalTuning = [TuningFastBEXTR,
                                                   TuningFastMOVBE];
  list<SubtargetFeature> BdVer2Tuning =
    !listconcat(BdVer1Tuning, BdVer2AdditionalTuning);
  list<SubtargetFeature> BdVer2Features =
    !listconcat(BdVer1Features, BdVer2AdditionalFeatures);

  // Steamroller
  list<SubtargetFeature> BdVer3AdditionalFeatures = [FeatureXSAVEOPT,
                                                     FeatureFSGSBase];
  list<SubtargetFeature> BdVer3Tuning = BdVer2Tuning;
  list<SubtargetFeature> BdVer3Features =
    !listconcat(BdVer2Features, BdVer3AdditionalFeatures);

  // Excavator
  list<SubtargetFeature> BdVer4AdditionalFeatures = [FeatureAVX2,
                                                     FeatureBMI2,
                                                     FeatureMOVBE,
                                                     FeatureRDRAND,
                                                     FeatureMWAITX];
  list<SubtargetFeature> BdVer4Tuning = BdVer3Tuning;
  list<SubtargetFeature> BdVer4Features =
    !listconcat(BdVer3Features, BdVer4AdditionalFeatures);


  // AMD Zen Processors common ISAs
  list<SubtargetFeature> ZNFeatures = [FeatureADX,
                                       FeatureAES,
                                       FeatureAVX2,
                                       FeatureBMI,
                                       FeatureBMI2,
                                       FeatureCLFLUSHOPT,
                                       FeatureCLZERO,
                                       FeatureCMOV,
                                       FeatureX86_64,
                                       FeatureCX16,
                                       FeatureCRC32,
                                       FeatureF16C,
                                       FeatureFMA,
                                       FeatureFSGSBase,
                                       FeatureFXSR,
                                       FeatureNOPL,
                                       FeatureLAHFSAHF64,
                                       FeatureLZCNT,
                                       FeatureMMX,
                                       FeatureMOVBE,
                                       FeatureMWAITX,
                                       FeaturePCLMUL,
                                       FeaturePOPCNT,
                                       FeaturePRFCHW,
                                       FeatureRDRAND,
                                       FeatureRDSEED,
                                       FeatureSHA,
                                       FeatureSSE4A,
                                       FeatureX87,
                                       FeatureXSAVE,
                                       FeatureXSAVEC,
                                       FeatureXSAVEOPT,
                                       FeatureXSAVES];
  list<SubtargetFeature> ZNTuning = [TuningFastLZCNT,
                                     TuningFastBEXTR,
                                     TuningFast15ByteNOP,
                                     TuningBranchFusion,
                                     TuningFastScalarFSQRT,
                                     TuningFastVectorFSQRT,
                                     TuningFastScalarShiftMasks,
                                     TuningFastVariablePerLaneShuffle,
                                     TuningFastMOVBE,
                                     TuningFastImm16,
                                     TuningSlowDivide64,
                                     TuningSlowSHLD,
                                     TuningSBBDepBreaking,
                                     TuningInsertVZEROUPPER,
                                     TuningAllowLight256Bit];
  list<SubtargetFeature> ZN2AdditionalFeatures = [FeatureCLWB,
                                                  FeatureRDPID,
                                                  FeatureRDPRU,
                                                  FeatureWBNOINVD];
  list<SubtargetFeature> ZN2Tuning = ZNTuning;
  list<SubtargetFeature> ZN2Features =
    !listconcat(ZNFeatures, ZN2AdditionalFeatures);
  list<SubtargetFeature> ZN3AdditionalFeatures = [FeatureFSRM,
                                                  FeatureINVPCID,
                                                  FeaturePKU,
                                                  FeatureVAES,
                                                  FeatureVPCLMULQDQ];
  list<SubtargetFeature> ZN3AdditionalTuning = [TuningMacroFusion];
  list<SubtargetFeature> ZN3Tuning =
    !listconcat(ZN2Tuning, ZN3AdditionalTuning);
  list<SubtargetFeature> ZN3Features =
    !listconcat(ZN2Features, ZN3AdditionalFeatures);


  list<SubtargetFeature> ZN4AdditionalTuning = [TuningFastDPWSSD];
  list<SubtargetFeature> ZN4Tuning =
    !listconcat(ZN3Tuning, ZN4AdditionalTuning);
  list<SubtargetFeature> ZN4AdditionalFeatures = [FeatureAVX512,
                                                  FeatureEVEX512,
                                                  FeatureCDI,
                                                  FeatureDQI,
                                                  FeatureBWI,
                                                  FeatureVLX,
                                                  FeatureVBMI,
                                                  FeatureVBMI2,
                                                  FeatureIFMA,
                                                  FeatureVNNI,
                                                  FeatureBITALG,
                                                  FeatureGFNI,
                                                  FeatureBF16,
                                                  FeatureSHSTK,
                                                  FeatureVPOPCNTDQ];
  list<SubtargetFeature> ZN4Features =
    !listconcat(ZN3Features, ZN4AdditionalFeatures);


  list<SubtargetFeature> ZN5Tuning = ZN4Tuning;
  list<SubtargetFeature> ZN5AdditionalFeatures = [FeatureVNNI,
                                                  FeatureMOVDIRI,
                                                  FeatureMOVDIR64B,
                                                  FeatureVP2INTERSECT,
                                                  FeaturePREFETCHI,
                                                  FeatureAVXVNNI
                                                  ];
  list<SubtargetFeature> ZN5Features =
    !listconcat(ZN4Features, ZN5AdditionalFeatures);

}

//===----------------------------------------------------------------------===//
// X86 processors supported.
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features,
           list<SubtargetFeature> TuneFeatures>
 : ProcessorModel<Name, GenericModel, Features, TuneFeatures>;

class ProcModel<string Name, SchedMachineModel Model,
                list<SubtargetFeature> Features,
                list<SubtargetFeature> TuneFeatures>
 : ProcessorModel<Name, Model, Features, TuneFeatures>;

// NOTE: CMPXCHG8B is here for legacy compatibility so that it is only disabled
// if i386/i486 is specifically requested.
// NOTE: 64Bit is here as "generic" is the default llc CPU. The X86Subtarget
// constructor checks that any CPU used in 64-bit mode has FeatureX86_64
// enabled. It has no effect on code generation.
// NOTE: As a default tuning, "generic" aims to produce code optimized for the
// most common X86 processors. The tunings might be changed over time. It is
// recommended to use "tune-cpu"="x86-64" in function attribute for consistency.
def : ProcModel<"generic", SandyBridgeModel,
                [FeatureX87, FeatureCX8, FeatureX86_64],
                [TuningSlow3OpsLEA,
                 TuningSlowDivide64,
                 TuningMacroFusion,
                 TuningFastScalarFSQRT,
                 TuningFast15ByteNOP,
                 TuningInsertVZEROUPPER]>;

def : Proc<"i386",            [FeatureX87],
                              [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
def : Proc<"i486",            [FeatureX87],
                              [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
def : Proc<"i586",            [FeatureX87, FeatureCX8],
                              [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
def : Proc<"pentium",         [FeatureX87, FeatureCX8],
                              [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
foreach P = ["pentium-mmx", "pentium_mmx"] in {
  def : Proc<P, [FeatureX87, FeatureCX8, FeatureMMX],
                [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
}
def : Proc<"i686", [FeatureX87, FeatureCX8, FeatureCMOV],
                   [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
foreach P = ["pentiumpro", "pentium_pro"] in {
  def : Proc<P, [FeatureX87, FeatureCX8, FeatureCMOV, FeatureNOPL],
                [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
}
foreach P = ["pentium2", "pentium_ii"] in {
  def : Proc<P, [FeatureX87, FeatureCX8, FeatureMMX, FeatureCMOV,
                          FeatureFXSR, FeatureNOPL],
                        [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
}
foreach P = ["pentium3", "pentium3m", "pentium_iii_no_xmm_regs", "pentium_iii"] in {
  def : Proc<P, [FeatureX87, FeatureCX8, FeatureMMX,
                 FeatureSSE1, FeatureFXSR, FeatureNOPL, FeatureCMOV],
                [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
}

// Enable the PostRAScheduler for SSE2 and SSE3 class cpus.
// The intent is to enable it for pentium4 which is the current default
// processor in a vanilla 32-bit clang compilation when no specific
// architecture is specified.  This generally gives a nice performance
// increase on silvermont, with largely neutral behavior on other
// contemporary large core processors.
// pentium-m, pentium4m, prescott and nocona are included as a preventative
// measure to avoid performance surprises, in case clang's default cpu
// changes slightly.

foreach P = ["pentium_m", "pentium-m"] in {
def : ProcModel<P, GenericPostRAModel,
                [FeatureX87, FeatureCX8, FeatureMMX, FeatureSSE2,
                FeatureFXSR, FeatureNOPL, FeatureCMOV],
                [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
}

foreach P = ["pentium4", "pentium4m", "pentium_4"] in {
  def : ProcModel<P, GenericPostRAModel,
                  [FeatureX87, FeatureCX8, FeatureMMX, FeatureSSE2,
                   FeatureFXSR, FeatureNOPL, FeatureCMOV],
                  [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
}

// Intel Quark.
def : Proc<"lakemont", [FeatureCX8],
                       [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;

// Intel Core Duo.
def : ProcModel<"yonah", SandyBridgeModel,
                [FeatureX87, FeatureCX8, FeatureMMX, FeatureSSE3,
                 FeatureFXSR, FeatureNOPL, FeatureCMOV],
                [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;

// NetBurst.
foreach P = ["prescott", "pentium_4_sse3"] in {
  def : ProcModel<P, GenericPostRAModel,
                  [FeatureX87, FeatureCX8, FeatureMMX, FeatureSSE3,
                  FeatureFXSR, FeatureNOPL, FeatureCMOV],
                  [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
}
def : ProcModel<"nocona", GenericPostRAModel, [
  FeatureX87,
  FeatureCX8,
  FeatureCMOV,
  FeatureMMX,
  FeatureSSE3,
  FeatureFXSR,
  FeatureNOPL,
  FeatureX86_64,
  FeatureCX16,
],
[
  TuningSlowUAMem16,
  TuningInsertVZEROUPPER
]>;

// Intel Core 2 Solo/Duo.
foreach P = ["core2", "core_2_duo_ssse3"] in {
def : ProcModel<P, SandyBridgeModel, [
  FeatureX87,
  FeatureCX8,
  FeatureCMOV,
  FeatureMMX,
  FeatureSSSE3,
  FeatureFXSR,
  FeatureNOPL,
  FeatureX86_64,
  FeatureCX16,
  FeatureLAHFSAHF64
],
[
  TuningMacroFusion,
  TuningSlowUAMem16,
  TuningInsertVZEROUPPER
]>;
}
foreach P = ["penryn", "core_2_duo_sse4_1"] in {
def : ProcModel<P, SandyBridgeModel, [
  FeatureX87,
  FeatureCX8,
  FeatureCMOV,
  FeatureMMX,
  FeatureSSE41,
  FeatureFXSR,
  FeatureNOPL,
  FeatureX86_64,
  FeatureCX16,
  FeatureLAHFSAHF64
],
[
  TuningMacroFusion,
  TuningSlowUAMem16,
  TuningInsertVZEROUPPER
]>;
}

// Atom CPUs.
foreach P = ["bonnell", "atom"] in {
  def : ProcModel<P, AtomModel, ProcessorFeatures.AtomFeatures,
                  ProcessorFeatures.AtomTuning>;
}

foreach P = ["silvermont", "slm", "atom_sse4_2"] in {
  def : ProcModel<P, SLMModel, ProcessorFeatures.SLMFeatures,
                  ProcessorFeatures.SLMTuning>;
}

def : ProcModel<"atom_sse4_2_movbe", SLMModel, ProcessorFeatures.GLMFeatures,
                ProcessorFeatures.SLMTuning>;
def : ProcModel<"goldmont", SLMModel, ProcessorFeatures.GLMFeatures,
                ProcessorFeatures.GLMTuning>;
foreach P = ["goldmont_plus", "goldmont-plus"] in {
  def : ProcModel<P, SLMModel, ProcessorFeatures.GLPFeatures,
                  ProcessorFeatures.GLPTuning>;
}
def : ProcModel<"tremont", SLMModel, ProcessorFeatures.TRMFeatures,
                ProcessorFeatures.TRMTuning>;

// "Arrandale" along with corei3 and corei5
foreach P = ["nehalem", "corei7", "core_i7_sse4_2"] in {
  def : ProcModel<P, SandyBridgeModel, ProcessorFeatures.NHMFeatures,
                  ProcessorFeatures.NHMTuning>;
}

// Westmere is the corei3/i5/i7 path from nehalem to sandybridge
foreach P = ["westmere", "core_aes_pclmulqdq"] in {
  def : ProcModel<P, SandyBridgeModel, ProcessorFeatures.WSMFeatures,
                  ProcessorFeatures.WSMTuning>;
}

foreach P = ["sandybridge", "corei7-avx", "core_2nd_gen_avx"] in {
  def : ProcModel<P, SandyBridgeModel, ProcessorFeatures.SNBFeatures,
                  ProcessorFeatures.SNBTuning>;
}

foreach P = ["ivybridge", "core-avx-i", "core_3rd_gen_avx"] in {
  def : ProcModel<P, SandyBridgeModel, ProcessorFeatures.IVBFeatures,
                  ProcessorFeatures.IVBTuning>;
}

foreach P = ["haswell", "core-avx2", "core_4th_gen_avx", "core_4th_gen_avx_tsx"] in {
  def : ProcModel<P, HaswellModel, ProcessorFeatures.HSWFeatures,
                  ProcessorFeatures.HSWTuning>;
}

foreach P = ["broadwell", "core_5th_gen_avx", "core_5th_gen_avx_tsx"] in {
  def : ProcModel<P, BroadwellModel, ProcessorFeatures.BDWFeatures,
                  ProcessorFeatures.BDWTuning>;
}

def : ProcModel<"skylake", SkylakeClientModel, ProcessorFeatures.SKLFeatures,
                ProcessorFeatures.SKLTuning>;

// FIXME: define KNL scheduler model
foreach P = ["knl", "mic_avx512"] in {
  def : ProcModel<P, HaswellModel, ProcessorFeatures.KNLFeatures,
                  ProcessorFeatures.KNLTuning>;
}
def : ProcModel<"knm", HaswellModel, ProcessorFeatures.KNMFeatures,
                ProcessorFeatures.KNLTuning>;

foreach P = ["skylake-avx512", "skx", "skylake_avx512"] in {
  def : ProcModel<P, SkylakeServerModel, ProcessorFeatures.SKXFeatures,
                  ProcessorFeatures.SKXTuning>;
}

def : ProcModel<"cascadelake", SkylakeServerModel,
                ProcessorFeatures.CLXFeatures, ProcessorFeatures.CLXTuning>;
def : ProcModel<"cooperlake", SkylakeServerModel,
                ProcessorFeatures.CPXFeatures, ProcessorFeatures.CPXTuning>;
def : ProcModel<"cannonlake", SkylakeServerModel,
                ProcessorFeatures.CNLFeatures, ProcessorFeatures.CNLTuning>;
foreach P = ["icelake-client", "icelake_client"] in {
def : ProcModel<P, IceLakeModel,
                ProcessorFeatures.ICLFeatures, ProcessorFeatures.ICLTuning>;
}
def : ProcModel<"rocketlake", IceLakeModel,
                ProcessorFeatures.ICLFeatures, ProcessorFeatures.ICLTuning>;
foreach P = ["icelake-server", "icelake_server"] in {
def : ProcModel<P, IceLakeModel,
                ProcessorFeatures.ICXFeatures, ProcessorFeatures.ICXTuning>;
}
def : ProcModel<"tigerlake", IceLakeModel,
                ProcessorFeatures.TGLFeatures, ProcessorFeatures.TGLTuning>;
def : ProcModel<"sapphirerapids", SapphireRapidsModel,
                ProcessorFeatures.SPRFeatures, ProcessorFeatures.SPRTuning>;
def : ProcModel<"alderlake", AlderlakePModel,
                ProcessorFeatures.ADLFeatures, ProcessorFeatures.ADLTuning>;
// FIXME: Use Gracemont Schedule Model when it is ready.
def : ProcModel<"gracemont", AlderlakePModel,
                ProcessorFeatures.ADLFeatures, ProcessorFeatures.GRTTuning>;
foreach P = ["sierraforest", "grandridge"] in {
  def : ProcModel<P, AlderlakePModel, ProcessorFeatures.SRFFeatures,
                ProcessorFeatures.GRTTuning>;
}
def : ProcModel<"raptorlake", AlderlakePModel,
                ProcessorFeatures.ADLFeatures, ProcessorFeatures.ADLTuning>;
def : ProcModel<"meteorlake", AlderlakePModel,
                ProcessorFeatures.ADLFeatures, ProcessorFeatures.ADLTuning>;
def : ProcModel<"arrowlake", AlderlakePModel,
                ProcessorFeatures.SRFFeatures, ProcessorFeatures.ADLTuning>;
foreach P = ["arrowlake-s", "arrowlake_s", "lunarlake"] in {
def : ProcModel<P, AlderlakePModel,
                ProcessorFeatures.ARLSFeatures, ProcessorFeatures.ADLTuning>;
}
def : ProcModel<"pantherlake", AlderlakePModel,
                ProcessorFeatures.PTLFeatures, ProcessorFeatures.ADLTuning>;
def : ProcModel<"clearwaterforest", AlderlakePModel,
                ProcessorFeatures.CWFFeatures, ProcessorFeatures.ADLTuning>;
def : ProcModel<"emeraldrapids", SapphireRapidsModel,
                ProcessorFeatures.SPRFeatures, ProcessorFeatures.SPRTuning>;
def : ProcModel<"graniterapids", SapphireRapidsModel,
                ProcessorFeatures.GNRFeatures, ProcessorFeatures.GNRTuning>;
foreach P = ["graniterapids-d", "graniterapids_d"] in {
def : ProcModel<P, SapphireRapidsModel,
                ProcessorFeatures.GNRDFeatures, ProcessorFeatures.GNRTuning>;
}

// AMD CPUs.

def : Proc<"k6",   [FeatureX87, FeatureCX8, FeatureMMX],
                   [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
def : Proc<"k6-2", [FeatureX87, FeatureCX8, FeatureMMX, FeaturePRFCHW],
                   [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
def : Proc<"k6-3", [FeatureX87, FeatureCX8, FeatureMMX, FeaturePRFCHW],
                   [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;

foreach P = ["athlon", "athlon-tbird"] in {
  def : Proc<P, [FeatureX87, FeatureCX8, FeatureCMOV, FeatureMMX, FeaturePRFCHW,
                 FeatureNOPL],
                [TuningSlowSHLD, TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
}

foreach P = ["athlon-4", "athlon-xp", "athlon-mp"] in {
  def : Proc<P, [FeatureX87, FeatureCX8, FeatureCMOV,
                 FeatureSSE1, FeatureMMX, FeaturePRFCHW, FeatureFXSR, FeatureNOPL],
                [TuningSlowSHLD, TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
}

foreach P = ["k8", "opteron", "athlon64", "athlon-fx"] in {
  def : Proc<P, [FeatureX87, FeatureCX8, FeatureSSE2, FeatureMMX, FeaturePRFCHW,
                 FeatureFXSR, FeatureNOPL, FeatureX86_64, FeatureCMOV],
                [TuningFastScalarShiftMasks, TuningSlowSHLD, TuningSlowUAMem16,
                 TuningSBBDepBreaking, TuningInsertVZEROUPPER]>;
}

foreach P = ["k8-sse3", "opteron-sse3", "athlon64-sse3"] in {
  def : Proc<P, [FeatureX87, FeatureCX8, FeatureSSE3, FeatureMMX, FeaturePRFCHW,
                 FeatureFXSR, FeatureNOPL, FeatureCX16, FeatureCMOV,
                 FeatureX86_64],
                [TuningFastScalarShiftMasks, TuningSlowSHLD, TuningSlowUAMem16,
                 TuningSBBDepBreaking, TuningInsertVZEROUPPER]>;
}

foreach P = ["amdfam10", "barcelona"] in {
  def : Proc<P, ProcessorFeatures.BarcelonaFeatures,
             ProcessorFeatures.BarcelonaTuning>;
}

// Bobcat
def : Proc<"btver1", ProcessorFeatures.BtVer1Features,
           ProcessorFeatures.BtVer1Tuning>;
// Jaguar
def : ProcModel<"btver2", BtVer2Model, ProcessorFeatures.BtVer2Features,
                ProcessorFeatures.BtVer2Tuning>;

// Bulldozer
def : ProcModel<"bdver1", BdVer2Model, ProcessorFeatures.BdVer1Features,
                ProcessorFeatures.BdVer1Tuning>;
// Piledriver
def : ProcModel<"bdver2", BdVer2Model, ProcessorFeatures.BdVer2Features,
                ProcessorFeatures.BdVer2Tuning>;
// Steamroller
def : Proc<"bdver3", ProcessorFeatures.BdVer3Features,
           ProcessorFeatures.BdVer3Tuning>;
// Excavator
def : Proc<"bdver4", ProcessorFeatures.BdVer4Features,
           ProcessorFeatures.BdVer4Tuning>;

def : ProcModel<"znver1", Znver1Model, ProcessorFeatures.ZNFeatures,
                ProcessorFeatures.ZNTuning>;
def : ProcModel<"znver2", Znver2Model, ProcessorFeatures.ZN2Features,
                ProcessorFeatures.ZN2Tuning>;
def : ProcModel<"znver3", Znver3Model, ProcessorFeatures.ZN3Features,
                ProcessorFeatures.ZN3Tuning>;
def : ProcModel<"znver4", Znver4Model, ProcessorFeatures.ZN4Features,
           ProcessorFeatures.ZN4Tuning>;
def : ProcModel<"znver5", Znver4Model, ProcessorFeatures.ZN5Features,
                ProcessorFeatures.ZN5Tuning>;

def : Proc<"geode",           [FeatureX87, FeatureCX8, FeatureMMX, FeaturePRFCHW],
                              [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;

def : Proc<"winchip-c6",      [FeatureX87, FeatureMMX],
                              [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
def : Proc<"winchip2",        [FeatureX87, FeatureMMX, FeaturePRFCHW],
                              [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
def : Proc<"c3",              [FeatureX87, FeatureMMX, FeaturePRFCHW],
                              [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;
def : Proc<"c3-2",            [FeatureX87, FeatureCX8, FeatureMMX,
                               FeatureSSE1, FeatureFXSR, FeatureCMOV],
                              [TuningSlowUAMem16, TuningInsertVZEROUPPER]>;

// We also provide a generic 64-bit specific x86 processor model which tries to
// be good for modern chips without enabling instruction set encodings past the
// basic SSE2 and 64-bit ones. It disables slow things from any mainstream and
// modern 64-bit x86 chip, and enables features that are generally beneficial.
//
// We currently use the Sandy Bridge model as the default scheduling model as
// we use it across Nehalem, Westmere, Sandy Bridge, and Ivy Bridge which
// covers a huge swath of x86 processors. If there are specific scheduling
// knobs which need to be tuned differently for AMD chips, we might consider
// forming a common base for them.
def : ProcModel<"x86-64", SandyBridgeModel, ProcessorFeatures.X86_64V1Features,
                ProcessorFeatures.X86_64V1Tuning>;
// Close to Sandybridge.
def : ProcModel<"x86-64-v2", SandyBridgeModel, ProcessorFeatures.X86_64V2Features,
                ProcessorFeatures.X86_64V2Tuning>;
// Close to Haswell.
def : ProcModel<"x86-64-v3", HaswellModel, ProcessorFeatures.X86_64V3Features,
                ProcessorFeatures.X86_64V3Tuning>;
// Close to the AVX-512 level implemented by Xeon Scalable Processors.
def : ProcModel<"x86-64-v4", SkylakeServerModel, ProcessorFeatures.X86_64V4Features,
                ProcessorFeatures.X86_64V4Tuning>;

//===----------------------------------------------------------------------===//
// Calling Conventions
//===----------------------------------------------------------------------===//

include "X86CallingConv.td"


//===----------------------------------------------------------------------===//
// Assembly Parser
//===----------------------------------------------------------------------===//

def ATTAsmParserVariant : AsmParserVariant {
  int Variant = 0;

  // Variant name.
  string Name = "att";

  // Discard comments in assembly strings.
  string CommentDelimiter = "#";

  // Recognize hard coded registers.
  string RegisterPrefix = "%";
}

def IntelAsmParserVariant : AsmParserVariant {
  int Variant = 1;

  // Variant name.
  string Name = "intel";

  // Discard comments in assembly strings.
  string CommentDelimiter = ";";

  // Recognize hard coded registers.
  string RegisterPrefix = "";
}

//===----------------------------------------------------------------------===//
// Assembly Printers
//===----------------------------------------------------------------------===//

// The X86 target supports two different syntaxes for emitting machine code.
// This is controlled by the -x86-asm-syntax={att|intel}
def ATTAsmWriter : AsmWriter {
  string AsmWriterClassName  = "ATTInstPrinter";
  int Variant = 0;
}
def IntelAsmWriter : AsmWriter {
  string AsmWriterClassName  = "IntelInstPrinter";
  int Variant = 1;
}

def X86 : Target {
  // Information about the instructions...
  let InstructionSet = X86InstrInfo;
  let AssemblyParserVariants = [ATTAsmParserVariant, IntelAsmParserVariant];
  let AssemblyWriters = [ATTAsmWriter, IntelAsmWriter];
  let AllowRegisterRenaming = 1;
}

//===----------------------------------------------------------------------===//
// Pfm Counters
//===----------------------------------------------------------------------===//

include "X86PfmCounters.td"
