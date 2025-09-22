//===-- X86ISelLowering.h - X86 DAG Lowering Interface ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that X86 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86ISELLOWERING_H
#define LLVM_LIB_TARGET_X86_X86ISELLOWERING_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
  class X86Subtarget;
  class X86TargetMachine;

  namespace X86ISD {
    // X86 Specific DAG Nodes
  enum NodeType : unsigned {
    // Start the numbering where the builtin ops leave off.
    FIRST_NUMBER = ISD::BUILTIN_OP_END,

    /// Bit scan forward.
    BSF,
    /// Bit scan reverse.
    BSR,

    /// X86 funnel/double shift i16 instructions. These correspond to
    /// X86::SHLDW and X86::SHRDW instructions which have different amt
    /// modulo rules to generic funnel shifts.
    /// NOTE: The operand order matches ISD::FSHL/FSHR not SHLD/SHRD.
    FSHL,
    FSHR,

    /// Bitwise logical AND of floating point values. This corresponds
    /// to X86::ANDPS or X86::ANDPD.
    FAND,

    /// Bitwise logical OR of floating point values. This corresponds
    /// to X86::ORPS or X86::ORPD.
    FOR,

    /// Bitwise logical XOR of floating point values. This corresponds
    /// to X86::XORPS or X86::XORPD.
    FXOR,

    ///  Bitwise logical ANDNOT of floating point values. This
    /// corresponds to X86::ANDNPS or X86::ANDNPD.
    FANDN,

    /// These operations represent an abstract X86 call
    /// instruction, which includes a bunch of information.  In particular the
    /// operands of these node are:
    ///
    ///     #0 - The incoming token chain
    ///     #1 - The callee
    ///     #2 - The number of arg bytes the caller pushes on the stack.
    ///     #3 - The number of arg bytes the callee pops off the stack.
    ///     #4 - The value to pass in AL/AX/EAX (optional)
    ///     #5 - The value to pass in DL/DX/EDX (optional)
    ///
    /// The result values of these nodes are:
    ///
    ///     #0 - The outgoing token chain
    ///     #1 - The first register result value (optional)
    ///     #2 - The second register result value (optional)
    ///
    CALL,

    /// Same as call except it adds the NoTrack prefix.
    NT_CALL,

    // Pseudo for a OBJC call that gets emitted together with a special
    // marker instruction.
    CALL_RVMARKER,

    /// X86 compare and logical compare instructions.
    CMP,
    FCMP,
    COMI,
    UCOMI,

    /// X86 bit-test instructions.
    BT,

    /// X86 SetCC. Operand 0 is condition code, and operand 1 is the EFLAGS
    /// operand, usually produced by a CMP instruction.
    SETCC,

    /// X86 Select
    SELECTS,

    // Same as SETCC except it's materialized with a sbb and the value is all
    // one's or all zero's.
    SETCC_CARRY, // R = carry_bit ? ~0 : 0

    /// X86 FP SETCC, implemented with CMP{cc}SS/CMP{cc}SD.
    /// Operands are two FP values to compare; result is a mask of
    /// 0s or 1s.  Generally DTRT for C/C++ with NaNs.
    FSETCC,

    /// X86 FP SETCC, similar to above, but with output as an i1 mask and
    /// and a version with SAE.
    FSETCCM,
    FSETCCM_SAE,

    /// X86 conditional moves. Operand 0 and operand 1 are the two values
    /// to select from. Operand 2 is the condition code, and operand 3 is the
    /// flag operand produced by a CMP or TEST instruction.
    CMOV,

    /// X86 conditional branches. Operand 0 is the chain operand, operand 1
    /// is the block to branch if condition is true, operand 2 is the
    /// condition code, and operand 3 is the flag operand produced by a CMP
    /// or TEST instruction.
    BRCOND,

    /// BRIND node with NoTrack prefix. Operand 0 is the chain operand and
    /// operand 1 is the target address.
    NT_BRIND,

    /// Return with a glue operand. Operand 0 is the chain operand, operand
    /// 1 is the number of bytes of stack to pop.
    RET_GLUE,

    /// Return from interrupt. Operand 0 is the number of bytes to pop.
    IRET,

    /// Repeat fill, corresponds to X86::REP_STOSx.
    REP_STOS,

    /// Repeat move, corresponds to X86::REP_MOVSx.
    REP_MOVS,

    /// On Darwin, this node represents the result of the popl
    /// at function entry, used for PIC code.
    GlobalBaseReg,

    /// A wrapper node for TargetConstantPool, TargetJumpTable,
    /// TargetExternalSymbol, TargetGlobalAddress, TargetGlobalTLSAddress,
    /// MCSymbol and TargetBlockAddress.
    Wrapper,

    /// Special wrapper used under X86-64 PIC mode for RIP
    /// relative displacements.
    WrapperRIP,

    /// Copies a 64-bit value from an MMX vector to the low word
    /// of an XMM vector, with the high word zero filled.
    MOVQ2DQ,

    /// Copies a 64-bit value from the low word of an XMM vector
    /// to an MMX vector.
    MOVDQ2Q,

    /// Copies a 32-bit value from the low word of a MMX
    /// vector to a GPR.
    MMX_MOVD2W,

    /// Copies a GPR into the low 32-bit word of a MMX vector
    /// and zero out the high word.
    MMX_MOVW2D,

    /// Extract an 8-bit value from a vector and zero extend it to
    /// i32, corresponds to X86::PEXTRB.
    PEXTRB,

    /// Extract a 16-bit value from a vector and zero extend it to
    /// i32, corresponds to X86::PEXTRW.
    PEXTRW,

    /// Insert any element of a 4 x float vector into any element
    /// of a destination 4 x floatvector.
    INSERTPS,

    /// Insert the lower 8-bits of a 32-bit value to a vector,
    /// corresponds to X86::PINSRB.
    PINSRB,

    /// Insert the lower 16-bits of a 32-bit value to a vector,
    /// corresponds to X86::PINSRW.
    PINSRW,

    /// Shuffle 16 8-bit values within a vector.
    PSHUFB,

    /// Compute Sum of Absolute Differences.
    PSADBW,
    /// Compute Double Block Packed Sum-Absolute-Differences
    DBPSADBW,

    /// Bitwise Logical AND NOT of Packed FP values.
    ANDNP,

    /// Blend where the selector is an immediate.
    BLENDI,

    /// Dynamic (non-constant condition) vector blend where only the sign bits
    /// of the condition elements are used. This is used to enforce that the
    /// condition mask is not valid for generic VSELECT optimizations. This
    /// is also used to implement the intrinsics.
    /// Operands are in VSELECT order: MASK, TRUE, FALSE
    BLENDV,

    /// Combined add and sub on an FP vector.
    ADDSUB,

    //  FP vector ops with rounding mode.
    FADD_RND,
    FADDS,
    FADDS_RND,
    FSUB_RND,
    FSUBS,
    FSUBS_RND,
    FMUL_RND,
    FMULS,
    FMULS_RND,
    FDIV_RND,
    FDIVS,
    FDIVS_RND,
    FMAX_SAE,
    FMAXS_SAE,
    FMIN_SAE,
    FMINS_SAE,
    FSQRT_RND,
    FSQRTS,
    FSQRTS_RND,

    // FP vector get exponent.
    FGETEXP,
    FGETEXP_SAE,
    FGETEXPS,
    FGETEXPS_SAE,
    // Extract Normalized Mantissas.
    VGETMANT,
    VGETMANT_SAE,
    VGETMANTS,
    VGETMANTS_SAE,
    // FP Scale.
    SCALEF,
    SCALEF_RND,
    SCALEFS,
    SCALEFS_RND,

    /// Integer horizontal add/sub.
    HADD,
    HSUB,

    /// Floating point horizontal add/sub.
    FHADD,
    FHSUB,

    // Detect Conflicts Within a Vector
    CONFLICT,

    /// Floating point max and min.
    FMAX,
    FMIN,

    /// Commutative FMIN and FMAX.
    FMAXC,
    FMINC,

    /// Scalar intrinsic floating point max and min.
    FMAXS,
    FMINS,

    /// Floating point reciprocal-sqrt and reciprocal approximation.
    /// Note that these typically require refinement
    /// in order to obtain suitable precision.
    FRSQRT,
    FRCP,

    // AVX-512 reciprocal approximations with a little more precision.
    RSQRT14,
    RSQRT14S,
    RCP14,
    RCP14S,

    // Thread Local Storage.
    TLSADDR,

    // Thread Local Storage. A call to get the start address
    // of the TLS block for the current module.
    TLSBASEADDR,

    // Thread Local Storage.  When calling to an OS provided
    // thunk at the address from an earlier relocation.
    TLSCALL,

    // Thread Local Storage. A descriptor containing pointer to
    // code and to argument to get the TLS offset for the symbol.
    TLSDESC,

    // Exception Handling helpers.
    EH_RETURN,

    // SjLj exception handling setjmp.
    EH_SJLJ_SETJMP,

    // SjLj exception handling longjmp.
    EH_SJLJ_LONGJMP,

    // SjLj exception handling dispatch.
    EH_SJLJ_SETUP_DISPATCH,

    /// Tail call return. See X86TargetLowering::LowerCall for
    /// the list of operands.
    TC_RETURN,

    // Vector move to low scalar and zero higher vector elements.
    VZEXT_MOVL,

    // Vector integer truncate.
    VTRUNC,
    // Vector integer truncate with unsigned/signed saturation.
    VTRUNCUS,
    VTRUNCS,

    // Masked version of the above. Used when less than a 128-bit result is
    // produced since the mask only applies to the lower elements and can't
    // be represented by a select.
    // SRC, PASSTHRU, MASK
    VMTRUNC,
    VMTRUNCUS,
    VMTRUNCS,

    // Vector FP extend.
    VFPEXT,
    VFPEXT_SAE,
    VFPEXTS,
    VFPEXTS_SAE,

    // Vector FP round.
    VFPROUND,
    VFPROUND_RND,
    VFPROUNDS,
    VFPROUNDS_RND,

    // Masked version of above. Used for v2f64->v4f32.
    // SRC, PASSTHRU, MASK
    VMFPROUND,

    // 128-bit vector logical left / right shift
    VSHLDQ,
    VSRLDQ,

    // Vector shift elements
    VSHL,
    VSRL,
    VSRA,

    // Vector variable shift
    VSHLV,
    VSRLV,
    VSRAV,

    // Vector shift elements by immediate
    VSHLI,
    VSRLI,
    VSRAI,

    // Shifts of mask registers.
    KSHIFTL,
    KSHIFTR,

    // Bit rotate by immediate
    VROTLI,
    VROTRI,

    // Vector packed double/float comparison.
    CMPP,

    // Vector integer comparisons.
    PCMPEQ,
    PCMPGT,

    // v8i16 Horizontal minimum and position.
    PHMINPOS,

    MULTISHIFT,

    /// Vector comparison generating mask bits for fp and
    /// integer signed and unsigned data types.
    CMPM,
    // Vector mask comparison generating mask bits for FP values.
    CMPMM,
    // Vector mask comparison with SAE for FP values.
    CMPMM_SAE,

    // Arithmetic operations with FLAGS results.
    ADD,
    SUB,
    ADC,
    SBB,
    SMUL,
    UMUL,
    OR,
    XOR,
    AND,

    // Bit field extract.
    BEXTR,
    BEXTRI,

    // Zero High Bits Starting with Specified Bit Position.
    BZHI,

    // Parallel extract and deposit.
    PDEP,
    PEXT,

    // X86-specific multiply by immediate.
    MUL_IMM,

    // Vector sign bit extraction.
    MOVMSK,

    // Vector bitwise comparisons.
    PTEST,

    // Vector packed fp sign bitwise comparisons.
    TESTP,

    // OR/AND test for masks.
    KORTEST,
    KTEST,

    // ADD for masks.
    KADD,

    // Several flavors of instructions with vector shuffle behaviors.
    // Saturated signed/unnsigned packing.
    PACKSS,
    PACKUS,
    // Intra-lane alignr.
    PALIGNR,
    // AVX512 inter-lane alignr.
    VALIGN,
    PSHUFD,
    PSHUFHW,
    PSHUFLW,
    SHUFP,
    // VBMI2 Concat & Shift.
    VSHLD,
    VSHRD,
    VSHLDV,
    VSHRDV,
    // Shuffle Packed Values at 128-bit granularity.
    SHUF128,
    MOVDDUP,
    MOVSHDUP,
    MOVSLDUP,
    MOVLHPS,
    MOVHLPS,
    MOVSD,
    MOVSS,
    MOVSH,
    UNPCKL,
    UNPCKH,
    VPERMILPV,
    VPERMILPI,
    VPERMI,
    VPERM2X128,

    // Variable Permute (VPERM).
    // Res = VPERMV MaskV, V0
    VPERMV,

    // 3-op Variable Permute (VPERMT2).
    // Res = VPERMV3 V0, MaskV, V1
    VPERMV3,

    // Bitwise ternary logic.
    VPTERNLOG,
    // Fix Up Special Packed Float32/64 values.
    VFIXUPIMM,
    VFIXUPIMM_SAE,
    VFIXUPIMMS,
    VFIXUPIMMS_SAE,
    // Range Restriction Calculation For Packed Pairs of Float32/64 values.
    VRANGE,
    VRANGE_SAE,
    VRANGES,
    VRANGES_SAE,
    // Reduce - Perform Reduction Transformation on scalar\packed FP.
    VREDUCE,
    VREDUCE_SAE,
    VREDUCES,
    VREDUCES_SAE,
    // RndScale - Round FP Values To Include A Given Number Of Fraction Bits.
    // Also used by the legacy (V)ROUND intrinsics where we mask out the
    // scaling part of the immediate.
    VRNDSCALE,
    VRNDSCALE_SAE,
    VRNDSCALES,
    VRNDSCALES_SAE,
    // Tests Types Of a FP Values for packed types.
    VFPCLASS,
    // Tests Types Of a FP Values for scalar types.
    VFPCLASSS,

    // Broadcast (splat) scalar or element 0 of a vector. If the operand is
    // a vector, this node may change the vector length as part of the splat.
    VBROADCAST,
    // Broadcast mask to vector.
    VBROADCASTM,

    /// SSE4A Extraction and Insertion.
    EXTRQI,
    INSERTQI,

    // XOP arithmetic/logical shifts.
    VPSHA,
    VPSHL,
    // XOP signed/unsigned integer comparisons.
    VPCOM,
    VPCOMU,
    // XOP packed permute bytes.
    VPPERM,
    // XOP two source permutation.
    VPERMIL2,

    // Vector multiply packed unsigned doubleword integers.
    PMULUDQ,
    // Vector multiply packed signed doubleword integers.
    PMULDQ,
    // Vector Multiply Packed UnsignedIntegers with Round and Scale.
    MULHRS,

    // Multiply and Add Packed Integers.
    VPMADDUBSW,
    VPMADDWD,

    // AVX512IFMA multiply and add.
    // NOTE: These are different than the instruction and perform
    // op0 x op1 + op2.
    VPMADD52L,
    VPMADD52H,

    // VNNI
    VPDPBUSD,
    VPDPBUSDS,
    VPDPWSSD,
    VPDPWSSDS,

    // FMA nodes.
    // We use the target independent ISD::FMA for the non-inverted case.
    FNMADD,
    FMSUB,
    FNMSUB,
    FMADDSUB,
    FMSUBADD,

    // FMA with rounding mode.
    FMADD_RND,
    FNMADD_RND,
    FMSUB_RND,
    FNMSUB_RND,
    FMADDSUB_RND,
    FMSUBADD_RND,

    // AVX512-FP16 complex addition and multiplication.
    VFMADDC,
    VFMADDC_RND,
    VFCMADDC,
    VFCMADDC_RND,

    VFMULC,
    VFMULC_RND,
    VFCMULC,
    VFCMULC_RND,

    VFMADDCSH,
    VFMADDCSH_RND,
    VFCMADDCSH,
    VFCMADDCSH_RND,

    VFMULCSH,
    VFMULCSH_RND,
    VFCMULCSH,
    VFCMULCSH_RND,

    VPDPBSUD,
    VPDPBSUDS,
    VPDPBUUD,
    VPDPBUUDS,
    VPDPBSSD,
    VPDPBSSDS,

    // Compress and expand.
    COMPRESS,
    EXPAND,

    // Bits shuffle
    VPSHUFBITQMB,

    // Convert Unsigned/Integer to Floating-Point Value with rounding mode.
    SINT_TO_FP_RND,
    UINT_TO_FP_RND,
    SCALAR_SINT_TO_FP,
    SCALAR_UINT_TO_FP,
    SCALAR_SINT_TO_FP_RND,
    SCALAR_UINT_TO_FP_RND,

    // Vector float/double to signed/unsigned integer.
    CVTP2SI,
    CVTP2UI,
    CVTP2SI_RND,
    CVTP2UI_RND,
    // Scalar float/double to signed/unsigned integer.
    CVTS2SI,
    CVTS2UI,
    CVTS2SI_RND,
    CVTS2UI_RND,

    // Vector float/double to signed/unsigned integer with truncation.
    CVTTP2SI,
    CVTTP2UI,
    CVTTP2SI_SAE,
    CVTTP2UI_SAE,
    // Scalar float/double to signed/unsigned integer with truncation.
    CVTTS2SI,
    CVTTS2UI,
    CVTTS2SI_SAE,
    CVTTS2UI_SAE,

    // Vector signed/unsigned integer to float/double.
    CVTSI2P,
    CVTUI2P,

    // Masked versions of above. Used for v2f64->v4f32.
    // SRC, PASSTHRU, MASK
    MCVTP2SI,
    MCVTP2UI,
    MCVTTP2SI,
    MCVTTP2UI,
    MCVTSI2P,
    MCVTUI2P,

    // Vector float to bfloat16.
    // Convert TWO packed single data to one packed BF16 data
    CVTNE2PS2BF16,
    // Convert packed single data to packed BF16 data
    CVTNEPS2BF16,
    // Masked version of above.
    // SRC, PASSTHRU, MASK
    MCVTNEPS2BF16,

    // Dot product of BF16 pairs to accumulated into
    // packed single precision.
    DPBF16PS,

    // A stack checking function call. On Windows it's _chkstk call.
    DYN_ALLOCA,

    // For allocating variable amounts of stack space when using
    // segmented stacks. Check if the current stacklet has enough space, and
    // falls back to heap allocation if not.
    SEG_ALLOCA,

    // For allocating stack space when using stack clash protector.
    // Allocation is performed by block, and each block is probed.
    PROBED_ALLOCA,

    // Memory barriers.
    MFENCE,

    // Get a random integer and indicate whether it is valid in CF.
    RDRAND,

    // Get a NIST SP800-90B & C compliant random integer and
    // indicate whether it is valid in CF.
    RDSEED,

    // Protection keys
    // RDPKRU - Operand 0 is chain. Operand 1 is value for ECX.
    // WRPKRU - Operand 0 is chain. Operand 1 is value for EDX. Operand 2 is
    // value for ECX.
    RDPKRU,
    WRPKRU,

    // SSE42 string comparisons.
    // These nodes produce 3 results, index, mask, and flags. X86ISelDAGToDAG
    // will emit one or two instructions based on which results are used. If
    // flags and index/mask this allows us to use a single instruction since
    // we won't have to pick and opcode for flags. Instead we can rely on the
    // DAG to CSE everything and decide at isel.
    PCMPISTR,
    PCMPESTR,

    // Test if in transactional execution.
    XTEST,

    // Conversions between float and half-float.
    CVTPS2PH,
    CVTPS2PH_SAE,
    CVTPH2PS,
    CVTPH2PS_SAE,

    // Masked version of above.
    // SRC, RND, PASSTHRU, MASK
    MCVTPS2PH,
    MCVTPS2PH_SAE,

    // Galois Field Arithmetic Instructions
    GF2P8AFFINEINVQB,
    GF2P8AFFINEQB,
    GF2P8MULB,

    // LWP insert record.
    LWPINS,

    // User level wait
    UMWAIT,
    TPAUSE,

    // Enqueue Stores Instructions
    ENQCMD,
    ENQCMDS,

    // For avx512-vp2intersect
    VP2INTERSECT,

    // User level interrupts - testui
    TESTUI,

    // Perform an FP80 add after changing precision control in FPCW.
    FP80_ADD,

    // Conditional compare instructions
    CCMP,
    CTEST,

    /// X86 strict FP compare instructions.
    STRICT_FCMP = ISD::FIRST_TARGET_STRICTFP_OPCODE,
    STRICT_FCMPS,

    // Vector packed double/float comparison.
    STRICT_CMPP,

    /// Vector comparison generating mask bits for fp and
    /// integer signed and unsigned data types.
    STRICT_CMPM,

    // Vector float/double to signed/unsigned integer with truncation.
    STRICT_CVTTP2SI,
    STRICT_CVTTP2UI,

    // Vector FP extend.
    STRICT_VFPEXT,

    // Vector FP round.
    STRICT_VFPROUND,

    // RndScale - Round FP Values To Include A Given Number Of Fraction Bits.
    // Also used by the legacy (V)ROUND intrinsics where we mask out the
    // scaling part of the immediate.
    STRICT_VRNDSCALE,

    // Vector signed/unsigned integer to float/double.
    STRICT_CVTSI2P,
    STRICT_CVTUI2P,

    // Strict FMA nodes.
    STRICT_FNMADD,
    STRICT_FMSUB,
    STRICT_FNMSUB,

    // Conversions between float and half-float.
    STRICT_CVTPS2PH,
    STRICT_CVTPH2PS,

    // Perform an FP80 add after changing precision control in FPCW.
    STRICT_FP80_ADD,

    // WARNING: Only add nodes here if they are strict FP nodes. Non-memory and
    // non-strict FP nodes should be above FIRST_TARGET_STRICTFP_OPCODE.

    // Compare and swap.
    LCMPXCHG_DAG = ISD::FIRST_TARGET_MEMORY_OPCODE,
    LCMPXCHG8_DAG,
    LCMPXCHG16_DAG,
    LCMPXCHG16_SAVE_RBX_DAG,

    /// LOCK-prefixed arithmetic read-modify-write instructions.
    /// EFLAGS, OUTCHAIN = LADD(INCHAIN, PTR, RHS)
    LADD,
    LSUB,
    LOR,
    LXOR,
    LAND,
    LBTS,
    LBTC,
    LBTR,
    LBTS_RM,
    LBTC_RM,
    LBTR_RM,

    /// RAO arithmetic instructions.
    /// OUTCHAIN = AADD(INCHAIN, PTR, RHS)
    AADD,
    AOR,
    AXOR,
    AAND,

    // Load, scalar_to_vector, and zero extend.
    VZEXT_LOAD,

    // extract_vector_elt, store.
    VEXTRACT_STORE,

    // scalar broadcast from memory.
    VBROADCAST_LOAD,

    // subvector broadcast from memory.
    SUBV_BROADCAST_LOAD,

    // Store FP control word into i16 memory.
    FNSTCW16m,

    // Load FP control word from i16 memory.
    FLDCW16m,

    // Store x87 FPU environment into memory.
    FNSTENVm,

    // Load x87 FPU environment from memory.
    FLDENVm,

    /// This instruction implements FP_TO_SINT with the
    /// integer destination in memory and a FP reg source.  This corresponds
    /// to the X86::FIST*m instructions and the rounding mode change stuff. It
    /// has two inputs (token chain and address) and two outputs (int value
    /// and token chain). Memory VT specifies the type to store to.
    FP_TO_INT_IN_MEM,

    /// This instruction implements SINT_TO_FP with the
    /// integer source in memory and FP reg result.  This corresponds to the
    /// X86::FILD*m instructions. It has two inputs (token chain and address)
    /// and two outputs (FP value and token chain). The integer source type is
    /// specified by the memory VT.
    FILD,

    /// This instruction implements a fp->int store from FP stack
    /// slots. This corresponds to the fist instruction. It takes a
    /// chain operand, value to store, address, and glue. The memory VT
    /// specifies the type to store as.
    FIST,

    /// This instruction implements an extending load to FP stack slots.
    /// This corresponds to the X86::FLD32m / X86::FLD64m. It takes a chain
    /// operand, and ptr to load from. The memory VT specifies the type to
    /// load from.
    FLD,

    /// This instruction implements a truncating store from FP stack
    /// slots. This corresponds to the X86::FST32m / X86::FST64m. It takes a
    /// chain operand, value to store, address, and glue. The memory VT
    /// specifies the type to store as.
    FST,

    /// These instructions grab the address of the next argument
    /// from a va_list. (reads and modifies the va_list in memory)
    VAARG_64,
    VAARG_X32,

    // Vector truncating store with unsigned/signed saturation
    VTRUNCSTOREUS,
    VTRUNCSTORES,
    // Vector truncating masked store with unsigned/signed saturation
    VMTRUNCSTOREUS,
    VMTRUNCSTORES,

    // X86 specific gather and scatter
    MGATHER,
    MSCATTER,

    // Key locker nodes that produce flags.
    AESENC128KL,
    AESDEC128KL,
    AESENC256KL,
    AESDEC256KL,
    AESENCWIDE128KL,
    AESDECWIDE128KL,
    AESENCWIDE256KL,
    AESDECWIDE256KL,

    /// Compare and Add if Condition is Met. Compare value in operand 2 with
    /// value in memory of operand 1. If condition of operand 4 is met, add
    /// value operand 3 to m32 and write new value in operand 1. Operand 2 is
    /// always updated with the original value from operand 1.
    CMPCCXADD,

    // Save xmm argument registers to the stack, according to %al. An operator
    // is needed so that this can be expanded with control flow.
    VASTART_SAVE_XMM_REGS,

    // Conditional load/store instructions
    CLOAD,
    CSTORE,

    // WARNING: Do not add anything in the end unless you want the node to
    // have memop! In fact, starting from FIRST_TARGET_MEMORY_OPCODE all
    // opcodes will be thought as target memory ops!
  };
  } // end namespace X86ISD

  namespace X86 {
    /// Current rounding mode is represented in bits 11:10 of FPSR. These
    /// values are same as corresponding constants for rounding mode used
    /// in glibc.
    enum RoundingMode {
      rmToNearest   = 0,        // FE_TONEAREST
      rmDownward    = 1 << 10,  // FE_DOWNWARD
      rmUpward      = 2 << 10,  // FE_UPWARD
      rmTowardZero  = 3 << 10,  // FE_TOWARDZERO
      rmMask        = 3 << 10   // Bit mask selecting rounding mode
    };
  }

  /// Define some predicates that are used for node matching.
  namespace X86 {
    /// Returns true if Elt is a constant zero or floating point constant +0.0.
    bool isZeroNode(SDValue Elt);

    /// Returns true of the given offset can be
    /// fit into displacement field of the instruction.
    bool isOffsetSuitableForCodeModel(int64_t Offset, CodeModel::Model M,
                                      bool hasSymbolicDisplacement);

    /// Determines whether the callee is required to pop its
    /// own arguments. Callee pop is necessary to support tail calls.
    bool isCalleePop(CallingConv::ID CallingConv,
                     bool is64Bit, bool IsVarArg, bool GuaranteeTCO);

    /// If Op is a constant whose elements are all the same constant or
    /// undefined, return true and return the constant value in \p SplatVal.
    /// If we have undef bits that don't cover an entire element, we treat these
    /// as zero if AllowPartialUndefs is set, else we fail and return false.
    bool isConstantSplat(SDValue Op, APInt &SplatVal,
                         bool AllowPartialUndefs = true);

    /// Check if Op is a load operation that could be folded into some other x86
    /// instruction as a memory operand. Example: vpaddd (%rdi), %xmm0, %xmm0.
    bool mayFoldLoad(SDValue Op, const X86Subtarget &Subtarget,
                     bool AssumeSingleUse = false);

    /// Check if Op is a load operation that could be folded into a vector splat
    /// instruction as a memory operand. Example: vbroadcastss 16(%rdi), %xmm2.
    bool mayFoldLoadIntoBroadcastFromMem(SDValue Op, MVT EltVT,
                                         const X86Subtarget &Subtarget,
                                         bool AssumeSingleUse = false);

    /// Check if Op is a value that could be used to fold a store into some
    /// other x86 instruction as a memory operand. Ex: pextrb $0, %xmm0, (%rdi).
    bool mayFoldIntoStore(SDValue Op);

    /// Check if Op is an operation that could be folded into a zero extend x86
    /// instruction.
    bool mayFoldIntoZeroExtend(SDValue Op);

    /// True if the target supports the extended frame for async Swift
    /// functions.
    bool isExtendedSwiftAsyncFrameSupported(const X86Subtarget &Subtarget,
                                            const MachineFunction &MF);
  } // end namespace X86

  //===--------------------------------------------------------------------===//
  //  X86 Implementation of the TargetLowering interface
  class X86TargetLowering final : public TargetLowering {
  public:
    explicit X86TargetLowering(const X86TargetMachine &TM,
                               const X86Subtarget &STI);

    unsigned getJumpTableEncoding() const override;
    bool useSoftFloat() const override;

    void markLibCallAttributes(MachineFunction *MF, unsigned CC,
                               ArgListTy &Args) const override;

    MVT getScalarShiftAmountTy(const DataLayout &, EVT VT) const override {
      return MVT::i8;
    }

    const MCExpr *
    LowerCustomJumpTableEntry(const MachineJumpTableInfo *MJTI,
                              const MachineBasicBlock *MBB, unsigned uid,
                              MCContext &Ctx) const override;

    /// Returns relocation base for the given PIC jumptable.
    SDValue getPICJumpTableRelocBase(SDValue Table,
                                     SelectionDAG &DAG) const override;
    const MCExpr *
    getPICJumpTableRelocBaseExpr(const MachineFunction *MF,
                                 unsigned JTI, MCContext &Ctx) const override;

    /// Return the desired alignment for ByVal aggregate
    /// function arguments in the caller parameter area. For X86, aggregates
    /// that contains are placed at 16-byte boundaries while the rest are at
    /// 4-byte boundaries.
    uint64_t getByValTypeAlignment(Type *Ty,
                                   const DataLayout &DL) const override;

    EVT getOptimalMemOpType(const MemOp &Op,
                            const AttributeList &FuncAttributes) const override;

    /// Returns true if it's safe to use load / store of the
    /// specified type to expand memcpy / memset inline. This is mostly true
    /// for all types except for some special cases. For example, on X86
    /// targets without SSE2 f64 load / store are done with fldl / fstpl which
    /// also does type conversion. Note the specified type doesn't have to be
    /// legal as the hook is used before type legalization.
    bool isSafeMemOpType(MVT VT) const override;

    bool isMemoryAccessFast(EVT VT, Align Alignment) const;

    /// Returns true if the target allows unaligned memory accesses of the
    /// specified type. Returns whether it is "fast" in the last argument.
    bool allowsMisalignedMemoryAccesses(EVT VT, unsigned AS, Align Alignment,
                                        MachineMemOperand::Flags Flags,
                                        unsigned *Fast) const override;

    /// This function returns true if the memory access is aligned or if the
    /// target allows this specific unaligned memory access. If the access is
    /// allowed, the optional final parameter returns a relative speed of the
    /// access (as defined by the target).
    bool allowsMemoryAccess(
        LLVMContext &Context, const DataLayout &DL, EVT VT, unsigned AddrSpace,
        Align Alignment,
        MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
        unsigned *Fast = nullptr) const override;

    bool allowsMemoryAccess(LLVMContext &Context, const DataLayout &DL, EVT VT,
                            const MachineMemOperand &MMO,
                            unsigned *Fast) const {
      return allowsMemoryAccess(Context, DL, VT, MMO.getAddrSpace(),
                                MMO.getAlign(), MMO.getFlags(), Fast);
    }

    /// Provide custom lowering hooks for some operations.
    ///
    SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

    /// Replace the results of node with an illegal result
    /// type with new values built out of custom code.
    ///
    void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue>&Results,
                            SelectionDAG &DAG) const override;

    SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

    bool preferABDSToABSWithNSW(EVT VT) const override;

    bool preferSextInRegOfTruncate(EVT TruncVT, EVT VT,
                                   EVT ExtVT) const override;

    bool isXAndYEqZeroPreferableToXAndYEqY(ISD::CondCode Cond,
                                           EVT VT) const override;

    /// Return true if the target has native support for
    /// the specified value type and it is 'desirable' to use the type for the
    /// given node type. e.g. On x86 i16 is legal, but undesirable since i16
    /// instruction encodings are longer and some i16 instructions are slow.
    bool isTypeDesirableForOp(unsigned Opc, EVT VT) const override;

    /// Return true if the target has native support for the
    /// specified value type and it is 'desirable' to use the type. e.g. On x86
    /// i16 is legal, but undesirable since i16 instruction encodings are longer
    /// and some i16 instructions are slow.
    bool IsDesirableToPromoteOp(SDValue Op, EVT &PVT) const override;

    /// Return prefered fold type, Abs if this is a vector, AddAnd if its an
    /// integer, None otherwise.
    TargetLowering::AndOrSETCCFoldKind
    isDesirableToCombineLogicOpOfSETCC(const SDNode *LogicOp,
                                       const SDNode *SETCC0,
                                       const SDNode *SETCC1) const override;

    /// Return the newly negated expression if the cost is not expensive and
    /// set the cost in \p Cost to indicate that if it is cheaper or neutral to
    /// do the negation.
    SDValue getNegatedExpression(SDValue Op, SelectionDAG &DAG,
                                 bool LegalOperations, bool ForCodeSize,
                                 NegatibleCost &Cost,
                                 unsigned Depth) const override;

    MachineBasicBlock *
    EmitInstrWithCustomInserter(MachineInstr &MI,
                                MachineBasicBlock *MBB) const override;

    /// This method returns the name of a target specific DAG node.
    const char *getTargetNodeName(unsigned Opcode) const override;

    /// Do not merge vector stores after legalization because that may conflict
    /// with x86-specific store splitting optimizations.
    bool mergeStoresAfterLegalization(EVT MemVT) const override {
      return !MemVT.isVector();
    }

    bool canMergeStoresTo(unsigned AddressSpace, EVT MemVT,
                          const MachineFunction &MF) const override;

    bool isCheapToSpeculateCttz(Type *Ty) const override;

    bool isCheapToSpeculateCtlz(Type *Ty) const override;

    bool isCtlzFast() const override;

    bool isMultiStoresCheaperThanBitsMerge(EVT LTy, EVT HTy) const override {
      // If the pair to store is a mixture of float and int values, we will
      // save two bitwise instructions and one float-to-int instruction and
      // increase one store instruction. There is potentially a more
      // significant benefit because it avoids the float->int domain switch
      // for input value. So It is more likely a win.
      if ((LTy.isFloatingPoint() && HTy.isInteger()) ||
          (LTy.isInteger() && HTy.isFloatingPoint()))
        return true;
      // If the pair only contains int values, we will save two bitwise
      // instructions and increase one store instruction (costing one more
      // store buffer). Since the benefit is more blurred so we leave
      // such pair out until we get testcase to prove it is a win.
      return false;
    }

    bool isMaskAndCmp0FoldingBeneficial(const Instruction &AndI) const override;

    bool hasAndNotCompare(SDValue Y) const override;

    bool hasAndNot(SDValue Y) const override;

    bool hasBitTest(SDValue X, SDValue Y) const override;

    bool shouldProduceAndByConstByHoistingConstFromShiftsLHSOfAnd(
        SDValue X, ConstantSDNode *XC, ConstantSDNode *CC, SDValue Y,
        unsigned OldShiftOpcode, unsigned NewShiftOpcode,
        SelectionDAG &DAG) const override;

    unsigned preferedOpcodeForCmpEqPiecesOfOperand(
        EVT VT, unsigned ShiftOpc, bool MayTransformRotate,
        const APInt &ShiftOrRotateAmt,
        const std::optional<APInt> &AndMask) const override;

    bool preferScalarizeSplat(SDNode *N) const override;

    CondMergingParams
    getJumpConditionMergingParams(Instruction::BinaryOps Opc, const Value *Lhs,
                                  const Value *Rhs) const override;

    bool shouldFoldConstantShiftPairToMask(const SDNode *N,
                                           CombineLevel Level) const override;

    bool shouldFoldMaskToVariableShiftPair(SDValue Y) const override;

    bool
    shouldTransformSignedTruncationCheck(EVT XVT,
                                         unsigned KeptBits) const override {
      // For vectors, we don't have a preference..
      if (XVT.isVector())
        return false;

      auto VTIsOk = [](EVT VT) -> bool {
        return VT == MVT::i8 || VT == MVT::i16 || VT == MVT::i32 ||
               VT == MVT::i64;
      };

      // We are ok with KeptBitsVT being byte/word/dword, what MOVS supports.
      // XVT will be larger than KeptBitsVT.
      MVT KeptBitsVT = MVT::getIntegerVT(KeptBits);
      return VTIsOk(XVT) && VTIsOk(KeptBitsVT);
    }

    ShiftLegalizationStrategy
    preferredShiftLegalizationStrategy(SelectionDAG &DAG, SDNode *N,
                                       unsigned ExpansionFactor) const override;

    bool shouldSplatInsEltVarIndex(EVT VT) const override;

    bool shouldConvertFpToSat(unsigned Op, EVT FPVT, EVT VT) const override {
      // Converting to sat variants holds little benefit on X86 as we will just
      // need to saturate the value back using fp arithmatic.
      return Op != ISD::FP_TO_UINT_SAT && isOperationLegalOrCustom(Op, VT);
    }

    bool convertSetCCLogicToBitwiseLogic(EVT VT) const override {
      return VT.isScalarInteger();
    }

    /// Vector-sized comparisons are fast using PCMPEQ + PMOVMSK or PTEST.
    MVT hasFastEqualityCompare(unsigned NumBits) const override;

    /// Return the value type to use for ISD::SETCC.
    EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                           EVT VT) const override;

    bool targetShrinkDemandedConstant(SDValue Op, const APInt &DemandedBits,
                                      const APInt &DemandedElts,
                                      TargetLoweringOpt &TLO) const override;

    /// Determine which of the bits specified in Mask are known to be either
    /// zero or one and return them in the KnownZero/KnownOne bitsets.
    void computeKnownBitsForTargetNode(const SDValue Op,
                                       KnownBits &Known,
                                       const APInt &DemandedElts,
                                       const SelectionDAG &DAG,
                                       unsigned Depth = 0) const override;

    /// Determine the number of bits in the operation that are sign bits.
    unsigned ComputeNumSignBitsForTargetNode(SDValue Op,
                                             const APInt &DemandedElts,
                                             const SelectionDAG &DAG,
                                             unsigned Depth) const override;

    bool SimplifyDemandedVectorEltsForTargetNode(SDValue Op,
                                                 const APInt &DemandedElts,
                                                 APInt &KnownUndef,
                                                 APInt &KnownZero,
                                                 TargetLoweringOpt &TLO,
                                                 unsigned Depth) const override;

    bool SimplifyDemandedVectorEltsForTargetShuffle(SDValue Op,
                                                    const APInt &DemandedElts,
                                                    unsigned MaskIndex,
                                                    TargetLoweringOpt &TLO,
                                                    unsigned Depth) const;

    bool SimplifyDemandedBitsForTargetNode(SDValue Op,
                                           const APInt &DemandedBits,
                                           const APInt &DemandedElts,
                                           KnownBits &Known,
                                           TargetLoweringOpt &TLO,
                                           unsigned Depth) const override;

    SDValue SimplifyMultipleUseDemandedBitsForTargetNode(
        SDValue Op, const APInt &DemandedBits, const APInt &DemandedElts,
        SelectionDAG &DAG, unsigned Depth) const override;

    bool isGuaranteedNotToBeUndefOrPoisonForTargetNode(
        SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
        bool PoisonOnly, unsigned Depth) const override;

    bool canCreateUndefOrPoisonForTargetNode(
        SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
        bool PoisonOnly, bool ConsiderFlags, unsigned Depth) const override;

    bool isSplatValueForTargetNode(SDValue Op, const APInt &DemandedElts,
                                   APInt &UndefElts, const SelectionDAG &DAG,
                                   unsigned Depth) const override;

    bool isTargetCanonicalConstantNode(SDValue Op) const override {
      // Peek through bitcasts/extracts/inserts to see if we have a broadcast
      // vector from memory.
      while (Op.getOpcode() == ISD::BITCAST ||
             Op.getOpcode() == ISD::EXTRACT_SUBVECTOR ||
             (Op.getOpcode() == ISD::INSERT_SUBVECTOR &&
              Op.getOperand(0).isUndef()))
        Op = Op.getOperand(Op.getOpcode() == ISD::INSERT_SUBVECTOR ? 1 : 0);

      return Op.getOpcode() == X86ISD::VBROADCAST_LOAD ||
             TargetLowering::isTargetCanonicalConstantNode(Op);
    }

    const Constant *getTargetConstantFromLoad(LoadSDNode *LD) const override;

    SDValue unwrapAddress(SDValue N) const override;

    SDValue getReturnAddressFrameIndex(SelectionDAG &DAG) const;

    bool ExpandInlineAsm(CallInst *CI) const override;

    ConstraintType getConstraintType(StringRef Constraint) const override;

    /// Examine constraint string and operand type and determine a weight value.
    /// The operand object must already have been set up with the operand type.
    ConstraintWeight
      getSingleConstraintMatchWeight(AsmOperandInfo &Info,
                                     const char *Constraint) const override;

    const char *LowerXConstraint(EVT ConstraintVT) const override;

    /// Lower the specified operand into the Ops vector. If it is invalid, don't
    /// add anything to Ops. If hasMemory is true it means one of the asm
    /// constraint of the inline asm instruction being processed is 'm'.
    void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                      std::vector<SDValue> &Ops,
                                      SelectionDAG &DAG) const override;

    InlineAsm::ConstraintCode
    getInlineAsmMemConstraint(StringRef ConstraintCode) const override {
      if (ConstraintCode == "v")
        return InlineAsm::ConstraintCode::v;
      return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
    }

    /// Handle Lowering flag assembly outputs.
    SDValue LowerAsmOutputForConstraint(SDValue &Chain, SDValue &Flag,
                                        const SDLoc &DL,
                                        const AsmOperandInfo &Constraint,
                                        SelectionDAG &DAG) const override;

    /// Given a physical register constraint
    /// (e.g. {edx}), return the register number and the register class for the
    /// register.  This should only be used for C_Register constraints.  On
    /// error, this returns a register number of 0.
    std::pair<unsigned, const TargetRegisterClass *>
    getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                 StringRef Constraint, MVT VT) const override;

    /// Return true if the addressing mode represented
    /// by AM is legal for this target, for a load/store of the specified type.
    bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM,
                               Type *Ty, unsigned AS,
                               Instruction *I = nullptr) const override;

    bool addressingModeSupportsTLS(const GlobalValue &GV) const override;

    /// Return true if the specified immediate is legal
    /// icmp immediate, that is the target has icmp instructions which can
    /// compare a register against the immediate without having to materialize
    /// the immediate into a register.
    bool isLegalICmpImmediate(int64_t Imm) const override;

    /// Return true if the specified immediate is legal
    /// add immediate, that is the target has add instructions which can
    /// add a register and the immediate without having to materialize
    /// the immediate into a register.
    bool isLegalAddImmediate(int64_t Imm) const override;

    bool isLegalStoreImmediate(int64_t Imm) const override;

    /// This is used to enable splatted operand transforms for vector shifts
    /// and vector funnel shifts.
    bool isVectorShiftByScalarCheap(Type *Ty) const override;

    /// Add x86-specific opcodes to the default list.
    bool isBinOp(unsigned Opcode) const override;

    /// Returns true if the opcode is a commutative binary operation.
    bool isCommutativeBinOp(unsigned Opcode) const override;

    /// Return true if it's free to truncate a value of
    /// type Ty1 to type Ty2. e.g. On x86 it's free to truncate a i32 value in
    /// register EAX to i16 by referencing its sub-register AX.
    bool isTruncateFree(Type *Ty1, Type *Ty2) const override;
    bool isTruncateFree(EVT VT1, EVT VT2) const override;

    bool allowTruncateForTailCall(Type *Ty1, Type *Ty2) const override;

    /// Return true if any actual instruction that defines a
    /// value of type Ty1 implicit zero-extends the value to Ty2 in the result
    /// register. This does not necessarily include registers defined in
    /// unknown ways, such as incoming arguments, or copies from unknown
    /// virtual registers. Also, if isTruncateFree(Ty2, Ty1) is true, this
    /// does not necessarily apply to truncate instructions. e.g. on x86-64,
    /// all instructions that define 32-bit values implicit zero-extend the
    /// result out to 64 bits.
    bool isZExtFree(Type *Ty1, Type *Ty2) const override;
    bool isZExtFree(EVT VT1, EVT VT2) const override;
    bool isZExtFree(SDValue Val, EVT VT2) const override;

    bool shouldSinkOperands(Instruction *I,
                            SmallVectorImpl<Use *> &Ops) const override;
    bool shouldConvertPhiType(Type *From, Type *To) const override;

    /// Return true if folding a vector load into ExtVal (a sign, zero, or any
    /// extend node) is profitable.
    bool isVectorLoadExtDesirable(SDValue) const override;

    /// Return true if an FMA operation is faster than a pair of fmul and fadd
    /// instructions. fmuladd intrinsics will be expanded to FMAs when this
    /// method returns true, otherwise fmuladd is expanded to fmul + fadd.
    bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                    EVT VT) const override;

    /// Return true if it's profitable to narrow operations of type SrcVT to
    /// DestVT. e.g. on x86, it's profitable to narrow from i32 to i8 but not
    /// from i32 to i16.
    bool isNarrowingProfitable(EVT SrcVT, EVT DestVT) const override;

    bool shouldFoldSelectWithIdentityConstant(unsigned BinOpcode,
                                              EVT VT) const override;

    /// Given an intrinsic, checks if on the target the intrinsic will need to map
    /// to a MemIntrinsicNode (touches memory). If this is the case, it returns
    /// true and stores the intrinsic information into the IntrinsicInfo that was
    /// passed to the function.
    bool getTgtMemIntrinsic(IntrinsicInfo &Info, const CallInst &I,
                            MachineFunction &MF,
                            unsigned Intrinsic) const override;

    /// Returns true if the target can instruction select the
    /// specified FP immediate natively. If false, the legalizer will
    /// materialize the FP immediate as a load from a constant pool.
    bool isFPImmLegal(const APFloat &Imm, EVT VT,
                      bool ForCodeSize) const override;

    /// Targets can use this to indicate that they only support *some*
    /// VECTOR_SHUFFLE operations, those with specific masks. By default, if a
    /// target supports the VECTOR_SHUFFLE node, all mask values are assumed to
    /// be legal.
    bool isShuffleMaskLegal(ArrayRef<int> Mask, EVT VT) const override;

    /// Similar to isShuffleMaskLegal. Targets can use this to indicate if there
    /// is a suitable VECTOR_SHUFFLE that can be used to replace a VAND with a
    /// constant pool entry.
    bool isVectorClearMaskLegal(ArrayRef<int> Mask, EVT VT) const override;

    /// Returns true if lowering to a jump table is allowed.
    bool areJTsAllowed(const Function *Fn) const override;

    MVT getPreferredSwitchConditionType(LLVMContext &Context,
                                        EVT ConditionVT) const override;

    /// If true, then instruction selection should
    /// seek to shrink the FP constant of the specified type to a smaller type
    /// in order to save space and / or reduce runtime.
    bool ShouldShrinkFPConstant(EVT VT) const override;

    /// Return true if we believe it is correct and profitable to reduce the
    /// load node to a smaller type.
    bool shouldReduceLoadWidth(SDNode *Load, ISD::LoadExtType ExtTy,
                               EVT NewVT) const override;

    /// Return true if the specified scalar FP type is computed in an SSE
    /// register, not on the X87 floating point stack.
    bool isScalarFPTypeInSSEReg(EVT VT) const;

    /// Returns true if it is beneficial to convert a load of a constant
    /// to just the constant itself.
    bool shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                           Type *Ty) const override;

    bool reduceSelectOfFPConstantLoads(EVT CmpOpVT) const override;

    bool convertSelectOfConstantsToMath(EVT VT) const override;

    bool decomposeMulByConstant(LLVMContext &Context, EVT VT,
                                SDValue C) const override;

    /// Return true if EXTRACT_SUBVECTOR is cheap for this result type
    /// with this index.
    bool isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                                 unsigned Index) const override;

    /// Scalar ops always have equal or better analysis/performance/power than
    /// the vector equivalent, so this always makes sense if the scalar op is
    /// supported.
    bool shouldScalarizeBinop(SDValue) const override;

    /// Extract of a scalar FP value from index 0 of a vector is free.
    bool isExtractVecEltCheap(EVT VT, unsigned Index) const override {
      EVT EltVT = VT.getScalarType();
      return (EltVT == MVT::f32 || EltVT == MVT::f64) && Index == 0;
    }

    /// Overflow nodes should get combined/lowered to optimal instructions
    /// (they should allow eliminating explicit compares by getting flags from
    /// math ops).
    bool shouldFormOverflowOp(unsigned Opcode, EVT VT,
                              bool MathUsed) const override;

    bool storeOfVectorConstantIsCheap(bool IsZero, EVT MemVT, unsigned NumElem,
                                      unsigned AddrSpace) const override {
      // If we can replace more than 2 scalar stores, there will be a reduction
      // in instructions even after we add a vector constant load.
      return IsZero || NumElem > 2;
    }

    bool isLoadBitCastBeneficial(EVT LoadVT, EVT BitcastVT,
                                 const SelectionDAG &DAG,
                                 const MachineMemOperand &MMO) const override;

    Register getRegisterByName(const char* RegName, LLT VT,
                               const MachineFunction &MF) const override;

    /// If a physical register, this returns the register that receives the
    /// exception address on entry to an EH pad.
    Register
    getExceptionPointerRegister(const Constant *PersonalityFn) const override;

    /// If a physical register, this returns the register that receives the
    /// exception typeid on entry to a landing pad.
    Register
    getExceptionSelectorRegister(const Constant *PersonalityFn) const override;

    bool needsFixedCatchObjects() const override;

    /// This method returns a target specific FastISel object,
    /// or null if the target does not support "fast" ISel.
    FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                             const TargetLibraryInfo *libInfo) const override;

    /// If the target has a standard location for the stack protector cookie,
    /// returns the address of that location. Otherwise, returns nullptr.
    Value *getIRStackGuard(IRBuilderBase &IRB) const override;

    bool useLoadStackGuardNode() const override;
    bool useStackGuardXorFP() const override;
    void insertSSPDeclarations(Module &M) const override;
    Value *getSDagStackGuard(const Module &M) const override;
    Function *getSSPStackGuardCheck(const Module &M) const override;
    SDValue emitStackGuardXorFP(SelectionDAG &DAG, SDValue Val,
                                const SDLoc &DL) const override;


    /// Return true if the target stores SafeStack pointer at a fixed offset in
    /// some non-standard address space, and populates the address space and
    /// offset as appropriate.
    Value *getSafeStackPointerLocation(IRBuilderBase &IRB) const override;

    std::pair<SDValue, SDValue> BuildFILD(EVT DstVT, EVT SrcVT, const SDLoc &DL,
                                          SDValue Chain, SDValue Pointer,
                                          MachinePointerInfo PtrInfo,
                                          Align Alignment,
                                          SelectionDAG &DAG) const;

    /// Customize the preferred legalization strategy for certain types.
    LegalizeTypeAction getPreferredVectorAction(MVT VT) const override;

    bool softPromoteHalfType() const override { return true; }

    MVT getRegisterTypeForCallingConv(LLVMContext &Context, CallingConv::ID CC,
                                      EVT VT) const override;

    unsigned getNumRegistersForCallingConv(LLVMContext &Context,
                                           CallingConv::ID CC,
                                           EVT VT) const override;

    unsigned getVectorTypeBreakdownForCallingConv(
        LLVMContext &Context, CallingConv::ID CC, EVT VT, EVT &IntermediateVT,
        unsigned &NumIntermediates, MVT &RegisterVT) const override;

    bool isIntDivCheap(EVT VT, AttributeList Attr) const override;

    bool supportSwiftError() const override;

    bool supportKCFIBundles() const override { return true; }

    MachineInstr *EmitKCFICheck(MachineBasicBlock &MBB,
                                MachineBasicBlock::instr_iterator &MBBI,
                                const TargetInstrInfo *TII) const override;

    bool hasStackProbeSymbol(const MachineFunction &MF) const override;
    bool hasInlineStackProbe(const MachineFunction &MF) const override;
    StringRef getStackProbeSymbolName(const MachineFunction &MF) const override;

    unsigned getStackProbeSize(const MachineFunction &MF) const;

    bool hasVectorBlend() const override { return true; }

    unsigned getMaxSupportedInterleaveFactor() const override { return 4; }

    bool isInlineAsmTargetBranch(const SmallVectorImpl<StringRef> &AsmStrs,
                                 unsigned OpNo) const override;

    SDValue visitMaskedLoad(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                            MachineMemOperand *MMO, SDValue &NewLoad,
                            SDValue Ptr, SDValue PassThru,
                            SDValue Mask) const override;
    SDValue visitMaskedStore(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                             MachineMemOperand *MMO, SDValue Ptr, SDValue Val,
                             SDValue Mask) const override;

    /// Lower interleaved load(s) into target specific
    /// instructions/intrinsics.
    bool lowerInterleavedLoad(LoadInst *LI,
                              ArrayRef<ShuffleVectorInst *> Shuffles,
                              ArrayRef<unsigned> Indices,
                              unsigned Factor) const override;

    /// Lower interleaved store(s) into target specific
    /// instructions/intrinsics.
    bool lowerInterleavedStore(StoreInst *SI, ShuffleVectorInst *SVI,
                               unsigned Factor) const override;

    SDValue expandIndirectJTBranch(const SDLoc &dl, SDValue Value, SDValue Addr,
                                   int JTI, SelectionDAG &DAG) const override;

    Align getPrefLoopAlignment(MachineLoop *ML) const override;

    EVT getTypeToTransformTo(LLVMContext &Context, EVT VT) const override {
      if (VT == MVT::f80)
        return EVT::getIntegerVT(Context, 96);
      return TargetLoweringBase::getTypeToTransformTo(Context, VT);
    }

  protected:
    std::pair<const TargetRegisterClass *, uint8_t>
    findRepresentativeClass(const TargetRegisterInfo *TRI,
                            MVT VT) const override;

  private:
    /// Keep a reference to the X86Subtarget around so that we can
    /// make the right decision when generating code for different targets.
    const X86Subtarget &Subtarget;

    /// A list of legal FP immediates.
    std::vector<APFloat> LegalFPImmediates;

    /// Indicate that this x86 target can instruction
    /// select the specified FP immediate natively.
    void addLegalFPImmediate(const APFloat& Imm) {
      LegalFPImmediates.push_back(Imm);
    }

    SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                            CallingConv::ID CallConv, bool isVarArg,
                            const SmallVectorImpl<ISD::InputArg> &Ins,
                            const SDLoc &dl, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &InVals,
                            uint32_t *RegMask) const;
    SDValue LowerMemArgument(SDValue Chain, CallingConv::ID CallConv,
                             const SmallVectorImpl<ISD::InputArg> &ArgInfo,
                             const SDLoc &dl, SelectionDAG &DAG,
                             const CCValAssign &VA, MachineFrameInfo &MFI,
                             unsigned i) const;
    SDValue LowerMemOpCallTo(SDValue Chain, SDValue StackPtr, SDValue Arg,
                             const SDLoc &dl, SelectionDAG &DAG,
                             const CCValAssign &VA,
                             ISD::ArgFlagsTy Flags, bool isByval) const;

    // Call lowering helpers.

    /// Check whether the call is eligible for tail call optimization. Targets
    /// that want to do tail call optimization should implement this function.
    bool IsEligibleForTailCallOptimization(
        TargetLowering::CallLoweringInfo &CLI, CCState &CCInfo,
        SmallVectorImpl<CCValAssign> &ArgLocs, bool IsCalleePopSRet) const;
    SDValue EmitTailCallLoadRetAddr(SelectionDAG &DAG, SDValue &OutRetAddr,
                                    SDValue Chain, bool IsTailCall,
                                    bool Is64Bit, int FPDiff,
                                    const SDLoc &dl) const;

    unsigned GetAlignedArgumentStackSize(unsigned StackSize,
                                         SelectionDAG &DAG) const;

    unsigned getAddressSpace() const;

    SDValue FP_TO_INTHelper(SDValue Op, SelectionDAG &DAG, bool IsSigned,
                            SDValue &Chain) const;
    SDValue LRINT_LLRINTHelper(SDNode *N, SelectionDAG &DAG) const;

    SDValue LowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerVSELECT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINSERT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;

    unsigned getGlobalWrapperKind(const GlobalValue *GV,
                                  const unsigned char OpFlags) const;
    SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;

    /// Creates target global address or external symbol nodes for calls or
    /// other uses.
    SDValue LowerGlobalOrExternal(SDValue Op, SelectionDAG &DAG,
                                  bool ForCall) const;

    SDValue LowerSINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerUINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerTRUNCATE(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFP_TO_INT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFP_TO_INT_SAT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerLRINT_LLRINT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSETCCCARRY(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSELECT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerVAARG(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerADDROFRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFRAME_TO_ARGS_OFFSET(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerEH_RETURN(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerEH_SJLJ_SETJMP(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerEH_SJLJ_LONGJMP(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerEH_SJLJ_SETUP_DISPATCH(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINIT_TRAMPOLINE(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGET_FPENV_MEM(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSET_FPENV_MEM(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerRESET_FPENV(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerWin64_i128OP(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerWin64_FP_TO_INT128(SDValue Op, SelectionDAG &DAG,
                                    SDValue &Chain) const;
    SDValue LowerWin64_INT128_TO_FP(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGC_TRANSITION(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerFaddFsub(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFP_EXTEND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFP_ROUND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFP_TO_BF16(SDValue Op, SelectionDAG &DAG) const;

    SDValue
    LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                         const SmallVectorImpl<ISD::InputArg> &Ins,
                         const SDLoc &dl, SelectionDAG &DAG,
                         SmallVectorImpl<SDValue> &InVals) const override;
    SDValue LowerCall(CallLoweringInfo &CLI,
                      SmallVectorImpl<SDValue> &InVals) const override;

    SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                        const SmallVectorImpl<ISD::OutputArg> &Outs,
                        const SmallVectorImpl<SDValue> &OutVals,
                        const SDLoc &dl, SelectionDAG &DAG) const override;

    bool supportSplitCSR(MachineFunction *MF) const override {
      return MF->getFunction().getCallingConv() == CallingConv::CXX_FAST_TLS &&
          MF->getFunction().hasFnAttribute(Attribute::NoUnwind);
    }
    void initializeSplitCSR(MachineBasicBlock *Entry) const override;
    void insertCopiesSplitCSR(
      MachineBasicBlock *Entry,
      const SmallVectorImpl<MachineBasicBlock *> &Exits) const override;

    bool isUsedByReturnOnly(SDNode *N, SDValue &Chain) const override;

    bool mayBeEmittedAsTailCall(const CallInst *CI) const override;

    EVT getTypeForExtReturn(LLVMContext &Context, EVT VT,
                            ISD::NodeType ExtendKind) const override;

    bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                        bool isVarArg,
                        const SmallVectorImpl<ISD::OutputArg> &Outs,
                        LLVMContext &Context) const override;

    const MCPhysReg *getScratchRegisters(CallingConv::ID CC) const override;
    ArrayRef<MCPhysReg> getRoundingControlRegisters() const override;

    TargetLoweringBase::AtomicExpansionKind
    shouldExpandAtomicLoadInIR(LoadInst *LI) const override;
    TargetLoweringBase::AtomicExpansionKind
    shouldExpandAtomicStoreInIR(StoreInst *SI) const override;
    TargetLoweringBase::AtomicExpansionKind
    shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const override;
    TargetLoweringBase::AtomicExpansionKind
    shouldExpandLogicAtomicRMWInIR(AtomicRMWInst *AI) const;
    void emitBitTestAtomicRMWIntrinsic(AtomicRMWInst *AI) const override;
    void emitCmpArithAtomicRMWIntrinsic(AtomicRMWInst *AI) const override;

    LoadInst *
    lowerIdempotentRMWIntoFencedLoad(AtomicRMWInst *AI) const override;

    bool needsCmpXchgNb(Type *MemType) const;

    void SetupEntryBlockForSjLj(MachineInstr &MI, MachineBasicBlock *MBB,
                                MachineBasicBlock *DispatchBB, int FI) const;

    // Utility function to emit the low-level va_arg code for X86-64.
    MachineBasicBlock *
    EmitVAARGWithCustomInserter(MachineInstr &MI, MachineBasicBlock *MBB) const;

    /// Utility function to emit the xmm reg save portion of va_start.
    MachineBasicBlock *EmitLoweredCascadedSelect(MachineInstr &MI1,
                                                 MachineInstr &MI2,
                                                 MachineBasicBlock *BB) const;

    MachineBasicBlock *EmitLoweredSelect(MachineInstr &I,
                                         MachineBasicBlock *BB) const;

    MachineBasicBlock *EmitLoweredCatchRet(MachineInstr &MI,
                                           MachineBasicBlock *BB) const;

    MachineBasicBlock *EmitLoweredSegAlloca(MachineInstr &MI,
                                            MachineBasicBlock *BB) const;

    MachineBasicBlock *EmitLoweredProbedAlloca(MachineInstr &MI,
                                               MachineBasicBlock *BB) const;

    MachineBasicBlock *EmitLoweredTLSAddr(MachineInstr &MI,
                                          MachineBasicBlock *BB) const;

    MachineBasicBlock *EmitLoweredTLSCall(MachineInstr &MI,
                                          MachineBasicBlock *BB) const;

    MachineBasicBlock *EmitLoweredIndirectThunk(MachineInstr &MI,
                                                MachineBasicBlock *BB) const;

    MachineBasicBlock *emitEHSjLjSetJmp(MachineInstr &MI,
                                        MachineBasicBlock *MBB) const;

    void emitSetJmpShadowStackFix(MachineInstr &MI,
                                  MachineBasicBlock *MBB) const;

    MachineBasicBlock *emitEHSjLjLongJmp(MachineInstr &MI,
                                         MachineBasicBlock *MBB) const;

    MachineBasicBlock *emitLongJmpShadowStackFix(MachineInstr &MI,
                                                 MachineBasicBlock *MBB) const;

    MachineBasicBlock *EmitSjLjDispatchBlock(MachineInstr &MI,
                                             MachineBasicBlock *MBB) const;

    MachineBasicBlock *emitPatchableEventCall(MachineInstr &MI,
                                              MachineBasicBlock *MBB) const;

    /// Emit flags for the given setcc condition and operands. Also returns the
    /// corresponding X86 condition code constant in X86CC.
    SDValue emitFlagsForSetcc(SDValue Op0, SDValue Op1, ISD::CondCode CC,
                              const SDLoc &dl, SelectionDAG &DAG,
                              SDValue &X86CC) const;

    bool optimizeFMulOrFDivAsShiftAddBitcast(SDNode *N, SDValue FPConst,
                                             SDValue IntPow2) const override;

    /// Check if replacement of SQRT with RSQRT should be disabled.
    bool isFsqrtCheap(SDValue Op, SelectionDAG &DAG) const override;

    /// Use rsqrt* to speed up sqrt calculations.
    SDValue getSqrtEstimate(SDValue Op, SelectionDAG &DAG, int Enabled,
                            int &RefinementSteps, bool &UseOneConstNR,
                            bool Reciprocal) const override;

    /// Use rcp* to speed up fdiv calculations.
    SDValue getRecipEstimate(SDValue Op, SelectionDAG &DAG, int Enabled,
                             int &RefinementSteps) const override;

    /// Reassociate floating point divisions into multiply by reciprocal.
    unsigned combineRepeatedFPDivisors() const override;

    SDValue BuildSDIVPow2(SDNode *N, const APInt &Divisor, SelectionDAG &DAG,
                          SmallVectorImpl<SDNode *> &Created) const override;

    SDValue getMOVL(SelectionDAG &DAG, const SDLoc &dl, MVT VT, SDValue V1,
                    SDValue V2) const;
  };

  namespace X86 {
    FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                             const TargetLibraryInfo *libInfo);
  } // end namespace X86

  // X86 specific Gather/Scatter nodes.
  // The class has the same order of operands as MaskedGatherScatterSDNode for
  // convenience.
  class X86MaskedGatherScatterSDNode : public MemIntrinsicSDNode {
  public:
    // This is a intended as a utility and should never be directly created.
    X86MaskedGatherScatterSDNode() = delete;
    ~X86MaskedGatherScatterSDNode() = delete;

    const SDValue &getBasePtr() const { return getOperand(3); }
    const SDValue &getIndex()   const { return getOperand(4); }
    const SDValue &getMask()    const { return getOperand(2); }
    const SDValue &getScale()   const { return getOperand(5); }

    static bool classof(const SDNode *N) {
      return N->getOpcode() == X86ISD::MGATHER ||
             N->getOpcode() == X86ISD::MSCATTER;
    }
  };

  class X86MaskedGatherSDNode : public X86MaskedGatherScatterSDNode {
  public:
    const SDValue &getPassThru() const { return getOperand(1); }

    static bool classof(const SDNode *N) {
      return N->getOpcode() == X86ISD::MGATHER;
    }
  };

  class X86MaskedScatterSDNode : public X86MaskedGatherScatterSDNode {
  public:
    const SDValue &getValue() const { return getOperand(1); }

    static bool classof(const SDNode *N) {
      return N->getOpcode() == X86ISD::MSCATTER;
    }
  };

  /// Generate unpacklo/unpackhi shuffle mask.
  void createUnpackShuffleMask(EVT VT, SmallVectorImpl<int> &Mask, bool Lo,
                               bool Unary);

  /// Similar to unpacklo/unpackhi, but without the 128-bit lane limitation
  /// imposed by AVX and specific to the unary pattern. Example:
  /// v8iX Lo --> <0, 0, 1, 1, 2, 2, 3, 3>
  /// v8iX Hi --> <4, 4, 5, 5, 6, 6, 7, 7>
  void createSplat2ShuffleMask(MVT VT, SmallVectorImpl<int> &Mask, bool Lo);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_X86_X86ISELLOWERING_H
