/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/







/**
 * @file
 *
 * This is file defines ASM primitives for the executive.

 * <hr>$Revision: 70030 $<hr>
 *
 *
 */
#ifndef __CVMX_ASM_H__
#define __CVMX_ASM_H__

#define CVMX_MAX_CORES  (32)

#define COP0_INDEX	$0,0	/* TLB read/write index */
#define COP0_RANDOM	$1,0	/* TLB random index */
#define COP0_ENTRYLO0	$2,0	/* TLB entryLo0 */
#define COP0_ENTRYLO1	$3,0	/* TLB entryLo1 */
#define COP0_CONTEXT	$4,0	/* Context */
#define COP0_PAGEMASK	$5,0	/* TLB pagemask */
#define COP0_PAGEGRAIN	$5,1	/* TLB config for max page sizes */
#define COP0_WIRED	$6,0	/* TLB number of wired entries */
#define COP0_HWRENA	$7,0	/* rdhw instruction enable per register */
#define COP0_BADVADDR	$8,0	/* Bad virtual address */
#define COP0_COUNT	$9,0	/* Mips count register */
#define COP0_CVMCOUNT	$9,6	/* Cavium count register */
#define COP0_CVMCTL	$9,7	/* Cavium control */
#define COP0_ENTRYHI	$10,0	/* TLB entryHi */
#define COP0_COMPARE	$11,0	/* Mips compare register */
#define COP0_POWTHROTTLE $11,6	/* Power throttle register */
#define COP0_CVMMEMCTL	$11,7	/* Cavium memory control */
#define COP0_STATUS	$12,0	/* Mips status register */
#define COP0_INTCTL	$12,1	/* Useless (Vectored interrupts) */
#define COP0_SRSCTL	$12,2	/* Useless (Shadow registers) */
#define COP0_CAUSE	$13,0	/* Mips cause register */
#define COP0_EPC	$14,0	/* Exception program counter */
#define COP0_PRID	$15,0	/* Processor ID */
#define COP0_EBASE	$15,1	/* Exception base */
#define COP0_CONFIG	$16,0	/* Misc config options */
#define COP0_CONFIG1	$16,1	/* Misc config options */
#define COP0_CONFIG2	$16,2	/* Misc config options */
#define COP0_CONFIG3	$16,3	/* Misc config options */
#define COP0_WATCHLO0	$18,0	/* Address watch registers */
#define COP0_WATCHLO1	$18,1	/* Address watch registers */
#define COP0_WATCHHI0	$19,0	/* Address watch registers */
#define COP0_WATCHHI1	$19,1	/* Address watch registers */
#define COP0_XCONTEXT	$20,0	/* OS context */
#define COP0_MULTICOREDEBUG $22,0 /* Cavium debug */
#define COP0_DEBUG	$23,0	/* Debug status */
#define COP0_DEPC	$24,0	/* Debug PC */
#define COP0_PERFCONTROL0 $25,0	/* Performance counter control */
#define COP0_PERFCONTROL1 $25,2	/* Performance counter control */
#define COP0_PERFVALUE0	$25,1	/* Performance counter */
#define COP0_PERFVALUE1	$25,3	/* Performance counter */
#define COP0_CACHEERRI	$27,0	/* I cache error status */
#define COP0_CACHEERRD	$27,1	/* D cache error status */
#define COP0_TAGLOI	$28,0	/* I cache tagLo */
#define COP0_TAGLOD	$28,2	/* D cache tagLo */
#define COP0_DATALOI	$28,1	/* I cache dataLo */
#define COP0_DATALOD	$28,3	/* D cahce dataLo */
#define COP0_TAGHI	$29,2	/* ? */
#define COP0_DATAHII	$29,1	/* ? */
#define COP0_DATAHID	$29,3	/* ? */
#define COP0_ERROREPC	$30,0	/* Error PC */
#define COP0_DESAVE	$31,0	/* Debug scratch area */

/* This header file can be included from a .S file.  Keep non-preprocessor
   things under !__ASSEMBLER__.  */
#ifndef __ASSEMBLER__

#ifdef	__cplusplus
extern "C" {
#endif

/* turn the variable name into a string */
#define CVMX_TMP_STR(x) CVMX_TMP_STR2(x)
#define CVMX_TMP_STR2(x) #x

/* Since sync is required for Octeon2. */
#ifdef _MIPS_ARCH_OCTEON2
#define CVMX_CAVIUM_OCTEON2   1
#endif

/* other useful stuff */
#define CVMX_BREAK asm volatile ("break")
#define CVMX_SYNC asm volatile ("sync" : : :"memory")
/* String version of SYNCW macro for using in inline asm constructs */
#define CVMX_SYNCW_STR_OCTEON2 "syncw\n"
#ifdef CVMX_CAVIUM_OCTEON2
 #define CVMX_SYNCW_STR CVMX_SYNCW_STR_OCTEON2
#else
 #define CVMX_SYNCW_STR "syncw\nsyncw\n"
#endif /* CVMX_CAVIUM_OCTEON2 */

#ifdef __OCTEON__
    #define CVMX_SYNCIOBDMA asm volatile ("synciobdma" : : :"memory")
    /* We actually use two syncw instructions in a row when we need a write
        memory barrier. This is because the CN3XXX series of Octeons have
        errata Core-401. This can cause a single syncw to not enforce
        ordering under very rare conditions. Even if it is rare, better safe
        than sorry */
    #define CVMX_SYNCW_OCTEON2 asm volatile ("syncw\n" : : :"memory")
    #ifdef CVMX_CAVIUM_OCTEON2
     #define CVMX_SYNCW CVMX_SYNCW_OCTEON2
    #else
     #define CVMX_SYNCW asm volatile ("syncw\nsyncw\n" : : :"memory")
    #endif /* CVMX_CAVIUM_OCTEON2 */
#if defined(VXWORKS) || defined(__linux__)
        /* Define new sync instructions to be normal SYNC instructions for
           operating systems that use threads */
        #define CVMX_SYNCWS CVMX_SYNCW
        #define CVMX_SYNCS  CVMX_SYNC
        #define CVMX_SYNCWS_STR CVMX_SYNCW_STR
        #define CVMX_SYNCWS_OCTEON2 CVMX_SYNCW_OCTEON2
        #define CVMX_SYNCWS_STR_OCTEON2 CVMX_SYNCW_STR_OCTEON2
#else
    #if defined(CVMX_BUILD_FOR_TOOLCHAIN)
        /* While building simple exec toolchain, always use syncw to
           support all Octeon models. */
        #define CVMX_SYNCWS CVMX_SYNCW
        #define CVMX_SYNCS  CVMX_SYNC
        #define CVMX_SYNCWS_STR CVMX_SYNCW_STR
        #define CVMX_SYNCWS_OCTEON2 CVMX_SYNCW_OCTEON2
        #define CVMX_SYNCWS_STR_OCTEON2 CVMX_SYNCW_STR_OCTEON2
    #else
        /* Again, just like syncw, we may need two syncws instructions in a row due
           errata Core-401. Only one syncws is required for Octeon2 models */
        #define CVMX_SYNCS asm volatile ("syncs" : : :"memory")
        #define CVMX_SYNCWS_OCTEON2 asm volatile ("syncws\n" : : :"memory")
        #define CVMX_SYNCWS_STR_OCTEON2 "syncws\n"
        #ifdef CVMX_CAVIUM_OCTEON2
          #define CVMX_SYNCWS CVMX_SYNCWS_OCTEON2
          #define CVMX_SYNCWS_STR CVMX_SYNCWS_STR_OCTEON2
        #else
          #define CVMX_SYNCWS asm volatile ("syncws\nsyncws\n" : : :"memory")
          #define CVMX_SYNCWS_STR "syncws\nsyncws\n"
        #endif /* CVMX_CAVIUM_OCTEON2 */
    #endif
#endif
#else /* !__OCTEON__ */
    /* Not using a Cavium compiler, always use the slower sync so the assembler stays happy */
    #define CVMX_SYNCIOBDMA asm volatile ("sync" : : :"memory")
    #define CVMX_SYNCW asm volatile ("sync" : : :"memory")
    #define CVMX_SYNCWS CVMX_SYNCW
    #define CVMX_SYNCS  CVMX_SYNC
    #define CVMX_SYNCWS_STR CVMX_SYNCW_STR
    #define CVMX_SYNCWS_OCTEON2 CVMX_SYNCW
    #define CVMX_SYNCWS_STR_OCTEON2 CVMX_SYNCW_STR
#endif
#define CVMX_SYNCI(address, offset) asm volatile ("synci " CVMX_TMP_STR(offset) "(%[rbase])" : : [rbase] "d" (address) )
#define CVMX_PREFETCH0(address) CVMX_PREFETCH(address, 0)
#define CVMX_PREFETCH128(address) CVMX_PREFETCH(address, 128)
// a normal prefetch
#define CVMX_PREFETCH(address, offset) CVMX_PREFETCH_PREF0(address, offset)
// normal prefetches that use the pref instruction
#define CVMX_PREFETCH_PREFX(X, address, offset) asm volatile ("pref %[type], %[off](%[rbase])" : : [rbase] "d" (address), [off] "I" (offset), [type] "n" (X))
#define CVMX_PREFETCH_PREF0(address, offset) CVMX_PREFETCH_PREFX(0, address, offset)
#define CVMX_PREFETCH_PREF1(address, offset) CVMX_PREFETCH_PREFX(1, address, offset)
#define CVMX_PREFETCH_PREF6(address, offset) CVMX_PREFETCH_PREFX(6, address, offset)
#define CVMX_PREFETCH_PREF7(address, offset) CVMX_PREFETCH_PREFX(7, address, offset)
// prefetch into L1, do not put the block in the L2
#define CVMX_PREFETCH_NOTL2(address, offset) CVMX_PREFETCH_PREFX(4, address, offset)
#define CVMX_PREFETCH_NOTL22(address, offset) CVMX_PREFETCH_PREFX(5, address, offset)
// prefetch into L2, do not put the block in the L1
#define CVMX_PREFETCH_L2(address, offset) CVMX_PREFETCH_PREFX(28, address, offset)
// CVMX_PREPARE_FOR_STORE makes each byte of the block unpredictable (actually old value or zero) until
// that byte is stored to (by this or another processor. Note that the value of each byte is not only
// unpredictable, but may also change again - up until the point when one of the cores stores to the
// byte.
#define CVMX_PREPARE_FOR_STORE(address, offset) CVMX_PREFETCH_PREFX(30, address, offset)
// This is a command headed to the L2 controller to tell it to clear its dirty bit for a
// block. Basically, SW is telling HW that the current version of the block will not be
// used.
#define CVMX_DONT_WRITE_BACK(address, offset) CVMX_PREFETCH_PREFX(29, address, offset)

#define CVMX_ICACHE_INVALIDATE  { CVMX_SYNC; asm volatile ("synci 0($0)" : : ); }    // flush stores, invalidate entire icache
#define CVMX_ICACHE_INVALIDATE2 { CVMX_SYNC; asm volatile ("cache 0, 0($0)" : : ); } // flush stores, invalidate entire icache
#define CVMX_DCACHE_INVALIDATE  { CVMX_SYNC; asm volatile ("cache 9, 0($0)" : : ); } // complete prefetches, invalidate entire dcache

#define CVMX_CACHE(op, address, offset) asm volatile ("cache " CVMX_TMP_STR(op) ", " CVMX_TMP_STR(offset) "(%[rbase])" : : [rbase] "d" (address) )
#define CVMX_CACHE_LCKL2(address, offset) CVMX_CACHE(31, address, offset) // fetch and lock the state.
#define CVMX_CACHE_WBIL2(address, offset) CVMX_CACHE(23, address, offset) // unlock the state.
#define CVMX_CACHE_WBIL2I(address, offset) CVMX_CACHE(3, address, offset) // invalidate the cache block and clear the USED bits for the block
#define CVMX_CACHE_LTGL2I(address, offset) CVMX_CACHE(7, address, offset) // load virtual tag and data for the L2 cache block into L2C_TAD0_TAG register

/* new instruction to make RC4 run faster */
#define CVMX_BADDU(result, input1, input2) asm ("baddu %[rd],%[rs],%[rt]" : [rd] "=d" (result) : [rs] "d" (input1) , [rt] "d" (input2))

// misc v2 stuff
#define CVMX_ROTR(result, input1, shiftconst) asm ("rotr %[rd],%[rs]," CVMX_TMP_STR(shiftconst) : [rd] "=d" (result) : [rs] "d" (input1))
#define CVMX_ROTRV(result, input1, input2) asm ("rotrv %[rd],%[rt],%[rs]" : [rd] "=d" (result) : [rt] "d" (input1) , [rs] "d" (input2))
#define CVMX_DROTR(result, input1, shiftconst) asm ("drotr %[rd],%[rs]," CVMX_TMP_STR(shiftconst) : [rd] "=d" (result) : [rs] "d" (input1))
#define CVMX_DROTRV(result, input1, input2) asm ("drotrv %[rd],%[rt],%[rs]" : [rd] "=d" (result) : [rt] "d" (input1) , [rs] "d" (input2))
#define CVMX_SEB(result, input1) asm ("seb %[rd],%[rt]" : [rd] "=d" (result) : [rt] "d" (input1))
#define CVMX_SEH(result, input1) asm ("seh %[rd],%[rt]" : [rd] "=d" (result) : [rt] "d" (input1))
#define CVMX_DSBH(result, input1) asm ("dsbh %[rd],%[rt]" : [rd] "=d" (result) : [rt] "d" (input1))
#define CVMX_DSHD(result, input1) asm ("dshd %[rd],%[rt]" : [rd] "=d" (result) : [rt] "d" (input1))
#define CVMX_WSBH(result, input1) asm ("wsbh %[rd],%[rt]" : [rd] "=d" (result) : [rt] "d" (input1))

// Endian swap
#define CVMX_ES64(result, input) \
        do {\
        CVMX_DSBH(result, input); \
        CVMX_DSHD(result, result); \
        } while (0)
#define CVMX_ES32(result, input) \
        do {\
        CVMX_WSBH(result, input); \
        CVMX_ROTR(result, result, 16); \
        } while (0)


/* extract and insert - NOTE that pos and len variables must be constants! */
/* the P variants take len rather than lenm1 */
/* the M1 variants take lenm1 rather than len */
#define CVMX_EXTS(result,input,pos,lenm1) asm ("exts %[rt],%[rs]," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(lenm1) : [rt] "=d" (result) : [rs] "d" (input))
#define CVMX_EXTSP(result,input,pos,len) CVMX_EXTS(result,input,pos,(len)-1)

#define CVMX_DEXT(result,input,pos,len) asm ("dext %[rt],%[rs]," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(len) : [rt] "=d" (result) : [rs] "d" (input))
#define CVMX_DEXTM1(result,input,pos,lenm1) CVMX_DEXT(result,input,pos,(lenm1)+1)

#define CVMX_EXT(result,input,pos,len) asm ("ext %[rt],%[rs]," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(len) : [rt] "=d" (result) : [rs] "d" (input))
#define CVMX_EXTM1(result,input,pos,lenm1) CVMX_EXT(result,input,pos,(lenm1)+1)

// removed
// #define CVMX_EXTU(result,input,pos,lenm1) asm ("extu %[rt],%[rs]," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(lenm1) : [rt] "=d" (result) : [rs] "d" (input))
// #define CVMX_EXTUP(result,input,pos,len) CVMX_EXTU(result,input,pos,(len)-1)

#define CVMX_CINS(result,input,pos,lenm1) asm ("cins %[rt],%[rs]," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(lenm1) : [rt] "=d" (result) : [rs] "d" (input))
#define CVMX_CINSP(result,input,pos,len) CVMX_CINS(result,input,pos,(len)-1)

#define CVMX_DINS(result,input,pos,len) asm ("dins %[rt],%[rs]," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(len): [rt] "=d" (result): [rs] "d" (input), "[rt]" (result))
#define CVMX_DINSM1(result,input,pos,lenm1) CVMX_DINS(result,input,pos,(lenm1)+1)
#define CVMX_DINSC(result,pos,len) asm ("dins %[rt],$0," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(len): [rt] "=d" (result): "[rt]" (result))
#define CVMX_DINSCM1(result,pos,lenm1) CVMX_DINSC(result,pos,(lenm1)+1)

#define CVMX_INS(result,input,pos,len) asm ("ins %[rt],%[rs]," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(len): [rt] "=d" (result): [rs] "d" (input), "[rt]" (result))
#define CVMX_INSM1(result,input,pos,lenm1) CVMX_INS(result,input,pos,(lenm1)+1)
#define CVMX_INSC(result,pos,len) asm ("ins %[rt],$0," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(len): [rt] "=d" (result): "[rt]" (result))
#define CVMX_INSCM1(result,pos,lenm1) CVMX_INSC(result,pos,(lenm1)+1)

// removed
// #define CVMX_INS0(result,input,pos,lenm1) asm("ins0 %[rt],%[rs]," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(lenm1): [rt] "=d" (result): [rs] "d" (input), "[rt]" (result))
// #define CVMX_INS0P(result,input,pos,len) CVMX_INS0(result,input,pos,(len)-1)
// #define CVMX_INS0C(result,pos,lenm1) asm ("ins0 %[rt],$0," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(lenm1) : [rt] "=d" (result) : "[rt]" (result))
// #define CVMX_INS0CP(result,pos,len) CVMX_INS0C(result,pos,(len)-1)

#define CVMX_CLZ(result, input) asm ("clz %[rd],%[rs]" : [rd] "=d" (result) : [rs] "d" (input))
#define CVMX_DCLZ(result, input) asm ("dclz %[rd],%[rs]" : [rd] "=d" (result) : [rs] "d" (input))
#define CVMX_CLO(result, input) asm ("clo %[rd],%[rs]" : [rd] "=d" (result) : [rs] "d" (input))
#define CVMX_DCLO(result, input) asm ("dclo %[rd],%[rs]" : [rd] "=d" (result) : [rs] "d" (input))
#define CVMX_POP(result, input) asm ("pop %[rd],%[rs]" : [rd] "=d" (result) : [rs] "d" (input))
#define CVMX_DPOP(result, input) asm ("dpop %[rd],%[rs]" : [rd] "=d" (result) : [rs] "d" (input))

#ifdef CVMX_ABI_O32

  /* rdhwr $31 is the 64 bit cmvcount register, it needs to be split
     into one or two (depending on the width of the result) properly
     sign extended registers.  All other registers are 32 bits wide
     and already properly sign extended. */
#  define CVMX_RDHWRX(result, regstr, ASM_STMT) ({			\
  if (regstr == 31) {							\
    if (sizeof(result) == 8) {						\
      ASM_STMT (".set\tpush\n"						\
		"\t.set\tmips64r2\n"					\
		"\trdhwr\t%L0,$31\n"					\
		"\tdsra\t%M0,%L0,32\n"					\
		"\tsll\t%L0,%L0,0\n"					\
		"\t.set\tpop": "=d"(result));				\
    } else {								\
      unsigned long _v;							\
      ASM_STMT ("rdhwr\t%0,$31\n"					\
		"\tsll\t%0,%0,0" : "=d"(_v));				\
      result = (__typeof(result))_v;					\
    }									\
  } else {								\
    unsigned long _v;							\
    ASM_STMT ("rdhwr\t%0,$" CVMX_TMP_STR(regstr) : "=d"(_v));		\
    result = (__typeof(result))_v;					\
  }})



#  define CVMX_RDHWR(result, regstr) CVMX_RDHWRX(result, regstr, asm volatile)
#  define CVMX_RDHWRNV(result, regstr) CVMX_RDHWRX(result, regstr, asm)
#else
#  define CVMX_RDHWR(result, regstr) asm volatile ("rdhwr %[rt],$" CVMX_TMP_STR(regstr) : [rt] "=d" (result))
#  define CVMX_RDHWRNV(result, regstr) asm ("rdhwr %[rt],$" CVMX_TMP_STR(regstr) : [rt] "=d" (result))
#endif

// some new cop0-like stuff
#define CVMX_DI(result) asm volatile ("di %[rt]" : [rt] "=d" (result))
#define CVMX_DI_NULL asm volatile ("di")
#define CVMX_EI(result) asm volatile ("ei %[rt]" : [rt] "=d" (result))
#define CVMX_EI_NULL asm volatile ("ei")
#define CVMX_EHB asm volatile ("ehb")

/* mul stuff */
#define CVMX_MTM0(m) asm volatile ("mtm0 %[rs]" : : [rs] "d" (m))
#define CVMX_MTM1(m) asm volatile ("mtm1 %[rs]" : : [rs] "d" (m))
#define CVMX_MTM2(m) asm volatile ("mtm2 %[rs]" : : [rs] "d" (m))
#define CVMX_MTP0(p) asm volatile ("mtp0 %[rs]" : : [rs] "d" (p))
#define CVMX_MTP1(p) asm volatile ("mtp1 %[rs]" : : [rs] "d" (p))
#define CVMX_MTP2(p) asm volatile ("mtp2 %[rs]" : : [rs] "d" (p))
#define CVMX_VMULU(dest,mpcand,accum) asm volatile ("vmulu %[rd],%[rs],%[rt]" : [rd] "=d" (dest) : [rs] "d" (mpcand), [rt] "d" (accum))
#define CVMX_VMM0(dest,mpcand,accum) asm volatile ("vmm0 %[rd],%[rs],%[rt]" : [rd] "=d" (dest) : [rs] "d" (mpcand), [rt] "d" (accum))
#define CVMX_V3MULU(dest,mpcand,accum) asm volatile ("v3mulu %[rd],%[rs],%[rt]" : [rd] "=d" (dest) : [rs] "d" (mpcand), [rt] "d" (accum))

/* branch stuff */
// these are hard to make work because the compiler does not realize that the
// instruction is a branch so may optimize away the label
// the labels to these next two macros must not include a ":" at the end
#define CVMX_BBIT1(var, pos, label) asm volatile ("bbit1 %[rs]," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(label) : : [rs] "d" (var))
#define CVMX_BBIT0(var, pos, label) asm volatile ("bbit0 %[rs]," CVMX_TMP_STR(pos) "," CVMX_TMP_STR(label) : : [rs] "d" (var))
// the label to this macro must include a ":" at the end
#define CVMX_ASM_LABEL(label) label \
                             asm volatile (CVMX_TMP_STR(label) : : )

//
// Low-latency memory stuff
//
// set can be 0-1
#define CVMX_MT_LLM_READ_ADDR(set,val)    asm volatile ("dmtc2 %[rt],0x0400+(8*(" CVMX_TMP_STR(set) "))" : : [rt] "d" (val))
#define CVMX_MT_LLM_WRITE_ADDR_INTERNAL(set,val)   asm volatile ("dmtc2 %[rt],0x0401+(8*(" CVMX_TMP_STR(set) "))" : : [rt] "d" (val))
#define CVMX_MT_LLM_READ64_ADDR(set,val)  asm volatile ("dmtc2 %[rt],0x0404+(8*(" CVMX_TMP_STR(set) "))" : : [rt] "d" (val))
#define CVMX_MT_LLM_WRITE64_ADDR_INTERNAL(set,val) asm volatile ("dmtc2 %[rt],0x0405+(8*(" CVMX_TMP_STR(set) "))" : : [rt] "d" (val))
#define CVMX_MT_LLM_DATA(set,val)         asm volatile ("dmtc2 %[rt],0x0402+(8*(" CVMX_TMP_STR(set) "))" : : [rt] "d" (val))
#define CVMX_MF_LLM_DATA(set,val)         asm volatile ("dmfc2 %[rt],0x0402+(8*(" CVMX_TMP_STR(set) "))" : [rt] "=d" (val) : )


// load linked, store conditional
#define CVMX_LL(dest, address, offset) asm volatile ("ll %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : [rt] "=d" (dest) : [rbase] "d" (address) )
#define CVMX_LLD(dest, address, offset) asm volatile ("lld %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : [rt] "=d" (dest) : [rbase] "d" (address) )
#define CVMX_SC(srcdest, address, offset) asm volatile ("sc %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : [rt] "=d" (srcdest) : [rbase] "d" (address), "[rt]" (srcdest) )
#define CVMX_SCD(srcdest, address, offset) asm volatile ("scd %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : [rt] "=d" (srcdest) : [rbase] "d" (address), "[rt]" (srcdest) )

// load/store word left/right
#define CVMX_LWR(srcdest, address, offset) asm volatile ("lwr %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : [rt] "=d" (srcdest) : [rbase] "d" (address), "[rt]" (srcdest) )
#define CVMX_LWL(srcdest, address, offset) asm volatile ("lwl %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : [rt] "=d" (srcdest) : [rbase] "d" (address), "[rt]" (srcdest) )
#define CVMX_LDR(srcdest, address, offset) asm volatile ("ldr %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : [rt] "=d" (srcdest) : [rbase] "d" (address), "[rt]" (srcdest) )
#define CVMX_LDL(srcdest, address, offset) asm volatile ("ldl %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : [rt] "=d" (srcdest) : [rbase] "d" (address), "[rt]" (srcdest) )

#define CVMX_SWR(src, address, offset) asm volatile ("swr %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : : [rbase] "d" (address), [rt] "d" (src) )
#define CVMX_SWL(src, address, offset) asm volatile ("swl %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : : [rbase] "d" (address), [rt] "d" (src) )
#define CVMX_SDR(src, address, offset) asm volatile ("sdr %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : : [rbase] "d" (address), [rt] "d" (src) )
#define CVMX_SDL(src, address, offset) asm volatile ("sdl %[rt], " CVMX_TMP_STR(offset) "(%[rbase])" : : [rbase] "d" (address), [rt] "d" (src) )



//
// Useful crypto ASM's
//

// CRC

#define CVMX_MT_CRC_POLYNOMIAL(val)         asm volatile ("dmtc2 %[rt],0x4200" : : [rt] "d" (val))
#define CVMX_MT_CRC_IV(val)                 asm volatile ("dmtc2 %[rt],0x0201" : : [rt] "d" (val))
#define CVMX_MT_CRC_LEN(val)                asm volatile ("dmtc2 %[rt],0x1202" : : [rt] "d" (val))
#define CVMX_MT_CRC_BYTE(val)               asm volatile ("dmtc2 %[rt],0x0204" : : [rt] "d" (val))
#define CVMX_MT_CRC_HALF(val)               asm volatile ("dmtc2 %[rt],0x0205" : : [rt] "d" (val))
#define CVMX_MT_CRC_WORD(val)               asm volatile ("dmtc2 %[rt],0x0206" : : [rt] "d" (val))
#define CVMX_MT_CRC_DWORD(val)              asm volatile ("dmtc2 %[rt],0x1207" : : [rt] "d" (val))
#define CVMX_MT_CRC_VAR(val)                asm volatile ("dmtc2 %[rt],0x1208" : : [rt] "d" (val))
#define CVMX_MT_CRC_POLYNOMIAL_REFLECT(val) asm volatile ("dmtc2 %[rt],0x4210" : : [rt] "d" (val))
#define CVMX_MT_CRC_IV_REFLECT(val)         asm volatile ("dmtc2 %[rt],0x0211" : : [rt] "d" (val))
#define CVMX_MT_CRC_BYTE_REFLECT(val)       asm volatile ("dmtc2 %[rt],0x0214" : : [rt] "d" (val))
#define CVMX_MT_CRC_HALF_REFLECT(val)       asm volatile ("dmtc2 %[rt],0x0215" : : [rt] "d" (val))
#define CVMX_MT_CRC_WORD_REFLECT(val)       asm volatile ("dmtc2 %[rt],0x0216" : : [rt] "d" (val))
#define CVMX_MT_CRC_DWORD_REFLECT(val)      asm volatile ("dmtc2 %[rt],0x1217" : : [rt] "d" (val))
#define CVMX_MT_CRC_VAR_REFLECT(val)        asm volatile ("dmtc2 %[rt],0x1218" : : [rt] "d" (val))

#define CVMX_MF_CRC_POLYNOMIAL(val)         asm volatile ("dmfc2 %[rt],0x0200" : [rt] "=d" (val) : )
#define CVMX_MF_CRC_IV(val)                 asm volatile ("dmfc2 %[rt],0x0201" : [rt] "=d" (val) : )
#define CVMX_MF_CRC_IV_REFLECT(val)         asm volatile ("dmfc2 %[rt],0x0203" : [rt] "=d" (val) : )
#define CVMX_MF_CRC_LEN(val)                asm volatile ("dmfc2 %[rt],0x0202" : [rt] "=d" (val) : )

// MD5 and SHA-1

// pos can be 0-6
#define CVMX_MT_HSH_DAT(val,pos)    asm volatile ("dmtc2 %[rt],0x0040+" CVMX_TMP_STR(pos) :                 : [rt] "d" (val))
#define CVMX_MT_HSH_DATZ(pos)       asm volatile ("dmtc2    $0,0x0040+" CVMX_TMP_STR(pos) :                 :               )
// pos can be 0-14
#define CVMX_MT_HSH_DATW(val,pos)   asm volatile ("dmtc2 %[rt],0x0240+" CVMX_TMP_STR(pos) :                 : [rt] "d" (val))
#define CVMX_MT_HSH_DATWZ(pos)      asm volatile ("dmtc2    $0,0x0240+" CVMX_TMP_STR(pos) :                 :               )
#define CVMX_MT_HSH_STARTMD5(val)   asm volatile ("dmtc2 %[rt],0x4047"                   :                 : [rt] "d" (val))
#define CVMX_MT_HSH_STARTSHA(val)   asm volatile ("dmtc2 %[rt],0x4057"                   :                 : [rt] "d" (val))
#define CVMX_MT_HSH_STARTSHA256(val)   asm volatile ("dmtc2 %[rt],0x404f"                   :                 : [rt] "d" (val))
#define CVMX_MT_HSH_STARTSHA512(val)   asm volatile ("dmtc2 %[rt],0x424f"                   :                 : [rt] "d" (val))
// pos can be 0-3
#define CVMX_MT_HSH_IV(val,pos)     asm volatile ("dmtc2 %[rt],0x0048+" CVMX_TMP_STR(pos) :                 : [rt] "d" (val))
// pos can be 0-7
#define CVMX_MT_HSH_IVW(val,pos)     asm volatile ("dmtc2 %[rt],0x0250+" CVMX_TMP_STR(pos) :                 : [rt] "d" (val))

// pos can be 0-6
#define CVMX_MF_HSH_DAT(val,pos)    asm volatile ("dmfc2 %[rt],0x0040+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
// pos can be 0-14
#define CVMX_MF_HSH_DATW(val,pos)   asm volatile ("dmfc2 %[rt],0x0240+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
// pos can be 0-3
#define CVMX_MF_HSH_IV(val,pos)     asm volatile ("dmfc2 %[rt],0x0048+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
// pos can be 0-7
#define CVMX_MF_HSH_IVW(val,pos)     asm volatile ("dmfc2 %[rt],0x0250+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )

// 3DES

// pos can be 0-2
#define CVMX_MT_3DES_KEY(val,pos)   asm volatile ("dmtc2 %[rt],0x0080+" CVMX_TMP_STR(pos) :                 : [rt] "d" (val))
#define CVMX_MT_3DES_IV(val)        asm volatile ("dmtc2 %[rt],0x0084"                   :                 : [rt] "d" (val))
#define CVMX_MT_3DES_ENC_CBC(val)   asm volatile ("dmtc2 %[rt],0x4088"                   :                 : [rt] "d" (val))
#define CVMX_MT_3DES_ENC(val)       asm volatile ("dmtc2 %[rt],0x408a"                   :                 : [rt] "d" (val))
#define CVMX_MT_3DES_DEC_CBC(val)   asm volatile ("dmtc2 %[rt],0x408c"                   :                 : [rt] "d" (val))
#define CVMX_MT_3DES_DEC(val)       asm volatile ("dmtc2 %[rt],0x408e"                   :                 : [rt] "d" (val))
#define CVMX_MT_3DES_RESULT(val)    asm volatile ("dmtc2 %[rt],0x0098"                   :                 : [rt] "d" (val))

// pos can be 0-2
#define CVMX_MF_3DES_KEY(val,pos)   asm volatile ("dmfc2 %[rt],0x0080+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
#define CVMX_MF_3DES_IV(val)        asm volatile ("dmfc2 %[rt],0x0084"                   : [rt] "=d" (val) :               )
#define CVMX_MF_3DES_RESULT(val)    asm volatile ("dmfc2 %[rt],0x0088"                   : [rt] "=d" (val) :               )

// KASUMI

// pos can be 0-1
#define CVMX_MT_KAS_KEY(val,pos)    CVMX_MT_3DES_KEY(val,pos)
#define CVMX_MT_KAS_ENC_CBC(val)    asm volatile ("dmtc2 %[rt],0x4089"                   :                 : [rt] "d" (val))
#define CVMX_MT_KAS_ENC(val)        asm volatile ("dmtc2 %[rt],0x408b"                   :                 : [rt] "d" (val))
#define CVMX_MT_KAS_RESULT(val)     CVMX_MT_3DES_RESULT(val)

// pos can be 0-1
#define CVMX_MF_KAS_KEY(val,pos)    CVMX_MF_3DES_KEY(val,pos)
#define CVMX_MF_KAS_RESULT(val)     CVMX_MF_3DES_RESULT(val)

// AES

#define CVMX_MT_AES_ENC_CBC0(val)   asm volatile ("dmtc2 %[rt],0x0108"                   :                 : [rt] "d" (val))
#define CVMX_MT_AES_ENC_CBC1(val)   asm volatile ("dmtc2 %[rt],0x3109"                   :                 : [rt] "d" (val))
#define CVMX_MT_AES_ENC0(val)       asm volatile ("dmtc2 %[rt],0x010a"                   :                 : [rt] "d" (val))
#define CVMX_MT_AES_ENC1(val)       asm volatile ("dmtc2 %[rt],0x310b"                   :                 : [rt] "d" (val))
#define CVMX_MT_AES_DEC_CBC0(val)   asm volatile ("dmtc2 %[rt],0x010c"                   :                 : [rt] "d" (val))
#define CVMX_MT_AES_DEC_CBC1(val)   asm volatile ("dmtc2 %[rt],0x310d"                   :                 : [rt] "d" (val))
#define CVMX_MT_AES_DEC0(val)       asm volatile ("dmtc2 %[rt],0x010e"                   :                 : [rt] "d" (val))
#define CVMX_MT_AES_DEC1(val)       asm volatile ("dmtc2 %[rt],0x310f"                   :                 : [rt] "d" (val))
// pos can be 0-3
#define CVMX_MT_AES_KEY(val,pos)    asm volatile ("dmtc2 %[rt],0x0104+" CVMX_TMP_STR(pos) :                 : [rt] "d" (val))
// pos can be 0-1
#define CVMX_MT_AES_IV(val,pos)     asm volatile ("dmtc2 %[rt],0x0102+" CVMX_TMP_STR(pos) :                 : [rt] "d" (val))
#define CVMX_MT_AES_KEYLENGTH(val)  asm volatile ("dmtc2 %[rt],0x0110"                   :                 : [rt] "d" (val)) // write the keylen
// pos can be 0-1
#define CVMX_MT_AES_RESULT(val,pos) asm volatile ("dmtc2 %[rt],0x0100+" CVMX_TMP_STR(pos) :                 : [rt] "d" (val))

// pos can be 0-1
#define CVMX_MF_AES_RESULT(val,pos) asm volatile ("dmfc2 %[rt],0x0100+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
// pos can be 0-1
#define CVMX_MF_AES_IV(val,pos)     asm volatile ("dmfc2 %[rt],0x0102+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
// pos can be 0-3
#define CVMX_MF_AES_KEY(val,pos)    asm volatile ("dmfc2 %[rt],0x0104+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
#define CVMX_MF_AES_KEYLENGTH(val)  asm volatile ("dmfc2 %[rt],0x0110"                   : [rt] "=d" (val) :               ) // read the keylen
#define CVMX_MF_AES_DAT0(val)       asm volatile ("dmfc2 %[rt],0x0111"                   : [rt] "=d" (val) :               ) // first piece of input data

// GFM

// pos can be 0-1
#define CVMX_MF_GFM_MUL(val,pos)             asm volatile ("dmfc2 %[rt],0x0258+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
#define CVMX_MF_GFM_POLY(val)                asm volatile ("dmfc2 %[rt],0x025e"                    : [rt] "=d" (val) :               )
// pos can be 0-1
#define CVMX_MF_GFM_RESINP(val,pos)          asm volatile ("dmfc2 %[rt],0x025a+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
// pos can be 0-1
#define CVMX_MF_GFM_RESINP_REFLECT(val,pos)  asm volatile ("dmfc2 %[rt],0x005a+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )

// pos can be 0-1
#define CVMX_MT_GFM_MUL(val,pos)             asm volatile ("dmtc2 %[rt],0x0258+" CVMX_TMP_STR(pos) :                 : [rt] "d" (val))
#define CVMX_MT_GFM_POLY(val)                asm volatile ("dmtc2 %[rt],0x025e"                    :                 : [rt] "d" (val))
// pos can be 0-1
#define CVMX_MT_GFM_RESINP(val,pos)          asm volatile ("dmtc2 %[rt],0x025a+" CVMX_TMP_STR(pos) :                 : [rt] "d" (val))
#define CVMX_MT_GFM_XOR0(val)                asm volatile ("dmtc2 %[rt],0x025c"                    :                 : [rt] "d" (val))
#define CVMX_MT_GFM_XORMUL1(val)             asm volatile ("dmtc2 %[rt],0x425d"                    :                 : [rt] "d" (val))
// pos can be 0-1
#define CVMX_MT_GFM_MUL_REFLECT(val,pos)     asm volatile ("dmtc2 %[rt],0x0058+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
#define CVMX_MT_GFM_XOR0_REFLECT(val)        asm volatile ("dmtc2 %[rt],0x005c"                    :                 : [rt] "d" (val))
#define CVMX_MT_GFM_XORMUL1_REFLECT(val)     asm volatile ("dmtc2 %[rt],0x405d"                    :                 : [rt] "d" (val))

// SNOW 3G

// pos can be 0-7
#define CVMX_MF_SNOW3G_LFSR(val,pos)    asm volatile ("dmfc2 %[rt],0x0240+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
// pos can be 0-2
#define CVMX_MF_SNOW3G_FSM(val,pos)     asm volatile ("dmfc2 %[rt],0x0251+" CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
#define CVMX_MF_SNOW3G_RESULT(val)      asm volatile ("dmfc2 %[rt],0x0250"                    : [rt] "=d" (val) :               )

// pos can be 0-7
#define CVMX_MT_SNOW3G_LFSR(val,pos)    asm volatile ("dmtc2 %[rt],0x0240+" CVMX_TMP_STR(pos) : : [rt] "d" (val))
// pos can be 0-2
#define CVMX_MT_SNOW3G_FSM(val,pos)     asm volatile ("dmtc2 %[rt],0x0251+" CVMX_TMP_STR(pos) : : [rt] "d" (val))
#define CVMX_MT_SNOW3G_RESULT(val)      asm volatile ("dmtc2 %[rt],0x0250"                    : : [rt] "d" (val))
#define CVMX_MT_SNOW3G_START(val)       asm volatile ("dmtc2 %[rt],0x404d"                    : : [rt] "d" (val))
#define CVMX_MT_SNOW3G_MORE(val)        asm volatile ("dmtc2 %[rt],0x404e"                    : : [rt] "d" (val))

// SMS4

// pos can be 0-1
#define CVMX_MF_SMS4_IV(val,pos)	asm volatile ("dmfc2 %[rt],0x0102+"CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
// pos can be 0-1
#define CVMX_MF_SMS4_KEY(val,pos)	asm volatile ("dmfc2 %[rt],0x0104+"CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
// pos can be 0-1
#define CVMX_MF_SMS4_RESINP(val,pos)	asm volatile ("dmfc2 %[rt],0x0100+"CVMX_TMP_STR(pos) : [rt] "=d" (val) :               )
#define CVMX_MT_SMS4_DEC_CBC0(val)	asm volatile ("dmtc2 %[rt],0x010c"                   : : [rt] "d" (val))
#define CVMX_MT_SMS4_DEC_CBC1(val)	asm volatile ("dmtc2 %[rt],0x311d"      : : [rt] "d" (val))
#define CVMX_MT_SMS4_DEC0(val)		asm volatile ("dmtc2 %[rt],0x010e"      : : [rt] "d" (val))
#define CVMX_MT_SMS4_DEC1(val)		asm volatile ("dmtc2 %[rt],0x311f"      : : [rt] "d" (val))
#define CVMX_MT_SMS4_ENC_CBC0(val)	asm volatile ("dmtc2 %[rt],0x0108"      : : [rt] "d" (val))
#define CVMX_MT_SMS4_ENC_CBC1(val)	asm volatile ("dmtc2 %[rt],0x3119"      : : [rt] "d" (val))
#define CVMX_MT_SMS4_ENC0(val)		asm volatile ("dmtc2 %[rt],0x010a"      : : [rt] "d" (val))
#define CVMX_MT_SMS4_ENC1(val)		asm volatile ("dmtc2 %[rt],0x311b"      : : [rt] "d" (val))
// pos can be 0-1
#define CVMX_MT_SMS4_IV(val,pos)	asm volatile ("dmtc2 %[rt],0x0102+"CVMX_TMP_STR(pos) : : [rt] "d" (val))
// pos can be 0-1
#define CVMX_MT_SMS4_KEY(val,pos)	asm volatile ("dmtc2 %[rt],0x0104+"CVMX_TMP_STR(pos) : : [rt] "d" (val))
// pos can be 0-1
#define CVMX_MT_SMS4_RESINP(val,pos)	asm volatile ("dmtc2 %[rt],0x0100+"CVMX_TMP_STR(pos) : : [rt] "d" (val))

/* check_ordering stuff */
#if 0
#define CVMX_MF_CHORD(dest)         asm volatile ("dmfc2 %[rt],0x400" : [rt] "=d" (dest) : )
#else
#define CVMX_MF_CHORD(dest)         CVMX_RDHWR(dest, 30)
#endif

#if 0
#define CVMX_MF_CYCLE(dest)         asm volatile ("dmfc0 %[rt],$9,6" : [rt] "=d" (dest) : ) // Use (64-bit) CvmCount register rather than Count
#else
#define CVMX_MF_CYCLE(dest)         CVMX_RDHWR(dest, 31) /* reads the current (64-bit) CvmCount value */
#endif

#define CVMX_MT_CYCLE(src)         asm volatile ("dmtc0 %[rt],$9,6" :: [rt] "d" (src))

#define VASTR(...) #__VA_ARGS__

#define CVMX_MF_COP0(val, cop0)           asm volatile ("dmfc0 %[rt]," VASTR(cop0) : [rt] "=d" (val));
#define CVMX_MT_COP0(val, cop0)           asm volatile ("dmtc0 %[rt]," VASTR(cop0) : : [rt] "d" (val));

#define CVMX_MF_CACHE_ERR(val)            CVMX_MF_COP0(val, COP0_CACHEERRI)
#define CVMX_MF_DCACHE_ERR(val)           CVMX_MF_COP0(val, COP0_CACHEERRD)
#define CVMX_MF_CVM_MEM_CTL(val)          CVMX_MF_COP0(val, COP0_CVMMEMCTL)
#define CVMX_MF_CVM_CTL(val)              CVMX_MF_COP0(val, COP0_CVMCTL)
#define CVMX_MT_CACHE_ERR(val)            CVMX_MT_COP0(val, COP0_CACHEERRI)
#define CVMX_MT_DCACHE_ERR(val)           CVMX_MT_COP0(val, COP0_CACHEERRD)
#define CVMX_MT_CVM_MEM_CTL(val)          CVMX_MT_COP0(val, COP0_CVMMEMCTL)
#define CVMX_MT_CVM_CTL(val)              CVMX_MT_COP0(val, COP0_CVMCTL)

/* Macros for TLB */
#define CVMX_TLBWI                       asm volatile ("tlbwi" : : )
#define CVMX_TLBWR                       asm volatile ("tlbwr" : : )
#define CVMX_TLBR                        asm volatile ("tlbr" : : )
#define CVMX_TLBP                        asm volatile ("tlbp" : : )
#define CVMX_MT_ENTRY_HIGH(val)          asm volatile ("dmtc0 %[rt],$10,0" : : [rt] "d" (val))
#define CVMX_MT_ENTRY_LO_0(val)          asm volatile ("dmtc0 %[rt],$2,0" : : [rt] "d" (val))
#define CVMX_MT_ENTRY_LO_1(val)          asm volatile ("dmtc0 %[rt],$3,0" : : [rt] "d" (val))
#define CVMX_MT_PAGEMASK(val)            asm volatile ("mtc0 %[rt],$5,0" : : [rt] "d" (val))
#define CVMX_MT_PAGEGRAIN(val)           asm volatile ("mtc0 %[rt],$5,1" : : [rt] "d" (val))
#define CVMX_MT_TLB_INDEX(val)           asm volatile ("mtc0 %[rt],$0,0" : : [rt] "d" (val))
#define CVMX_MT_TLB_CONTEXT(val)         asm volatile ("dmtc0 %[rt],$4,0" : : [rt] "d" (val))
#define CVMX_MT_TLB_WIRED(val)           asm volatile ("mtc0 %[rt],$6,0" : : [rt] "d" (val))
#define CVMX_MT_TLB_RANDOM(val)          asm volatile ("mtc0 %[rt],$1,0" : : [rt] "d" (val))
#define CVMX_MF_ENTRY_LO_0(val)          asm volatile ("dmfc0 %[rt],$2,0" :  [rt] "=d" (val):)
#define CVMX_MF_ENTRY_LO_1(val)          asm volatile ("dmfc0 %[rt],$3,0" :  [rt] "=d" (val):)
#define CVMX_MF_ENTRY_HIGH(val)          asm volatile ("dmfc0 %[rt],$10,0" :  [rt] "=d" (val):)
#define CVMX_MF_PAGEMASK(val)            asm volatile ("mfc0 %[rt],$5,0" :  [rt] "=d" (val):)
#define CVMX_MF_PAGEGRAIN(val)           asm volatile ("mfc0 %[rt],$5,1" :  [rt] "=d" (val):)
#define CVMX_MF_TLB_WIRED(val)           asm volatile ("mfc0 %[rt],$6,0" :  [rt] "=d" (val):)
#define CVMX_MF_TLB_INDEX(val)           asm volatile ("mfc0 %[rt],$0,0" :  [rt] "=d" (val):)
#define CVMX_MF_TLB_RANDOM(val)          asm volatile ("mfc0 %[rt],$1,0" :  [rt] "=d" (val):)
#define TLB_DIRTY   (0x1ULL<<2)
#define TLB_VALID   (0x1ULL<<1)
#define TLB_GLOBAL  (0x1ULL<<0)


#if !defined(__FreeBSD__) || !defined(_KERNEL)
/* Macros to PUSH and POP Octeon2 ISA. */
#define CVMX_PUSH_OCTEON2    asm volatile (".set push\n.set arch=octeon2")
#define CVMX_POP_OCTEON2     asm volatile (".set pop")
#endif

/* assembler macros to guarantee byte loads/stores are used */
/* for an unaligned 16-bit access (these use AT register) */
/* we need the hidden argument (__a) so that GCC gets the dependencies right */
#define CVMX_LOADUNA_INT16(result, address, offset) \
	{ char *__a = (char *)(address); \
	  asm ("ulh %[rdest], " CVMX_TMP_STR(offset) "(%[rbase])" : [rdest] "=d" (result) : [rbase] "d" (__a), "m"(__a[offset]), "m"(__a[offset + 1])); }
#define CVMX_LOADUNA_UINT16(result, address, offset) \
	{ char *__a = (char *)(address); \
	  asm ("ulhu %[rdest], " CVMX_TMP_STR(offset) "(%[rbase])" : [rdest] "=d" (result) : [rbase] "d" (__a), "m"(__a[offset + 0]), "m"(__a[offset + 1])); }
#define CVMX_STOREUNA_INT16(data, address, offset) \
	{ char *__a = (char *)(address); \
	  asm ("ush %[rsrc], " CVMX_TMP_STR(offset) "(%[rbase])" : "=m"(__a[offset + 0]), "=m"(__a[offset + 1]): [rsrc] "d" (data), [rbase] "d" (__a)); }

#define CVMX_LOADUNA_INT32(result, address, offset) \
	{ char *__a = (char *)(address); \
	  asm ("ulw %[rdest], " CVMX_TMP_STR(offset) "(%[rbase])" : [rdest] "=d" (result) : \
	       [rbase] "d" (__a), "m"(__a[offset + 0]), "m"(__a[offset + 1]), "m"(__a[offset + 2]), "m"(__a[offset + 3])); }
#define CVMX_STOREUNA_INT32(data, address, offset) \
	{ char *__a = (char *)(address); \
	  asm ("usw %[rsrc], " CVMX_TMP_STR(offset) "(%[rbase])" : \
	       "=m"(__a[offset + 0]), "=m"(__a[offset + 1]), "=m"(__a[offset + 2]), "=m"(__a[offset + 3]) : \
	       [rsrc] "d" (data), [rbase] "d" (__a)); }

#define CVMX_LOADUNA_INT64(result, address, offset) \
	{ char *__a = (char *)(address); \
	  asm ("uld %[rdest], " CVMX_TMP_STR(offset) "(%[rbase])" : [rdest] "=d" (result) : \
	       [rbase] "d" (__a), "m"(__a[offset + 0]), "m"(__a[offset + 1]), "m"(__a[offset + 2]), "m"(__a[offset + 3]), \
	       "m"(__a[offset + 4]), "m"(__a[offset + 5]), "m"(__a[offset + 6]), "m"(__a[offset + 7])); }
#define CVMX_STOREUNA_INT64(data, address, offset) \
	{ char *__a = (char *)(address); \
	  asm ("usd %[rsrc], " CVMX_TMP_STR(offset) "(%[rbase])" : \
	       "=m"(__a[offset + 0]), "=m"(__a[offset + 1]), "=m"(__a[offset + 2]), "=m"(__a[offset + 3]), \
	       "=m"(__a[offset + 4]), "=m"(__a[offset + 5]), "=m"(__a[offset + 6]), "=m"(__a[offset + 7]) : \
	       [rsrc] "d" (data), [rbase] "d" (__a)); }

#ifdef	__cplusplus
}
#endif

#endif	/* __ASSEMBLER__ */

#endif /* __CVMX_ASM_H__ */
