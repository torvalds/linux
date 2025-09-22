//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _HEXAGON_CIRC_BREV_INTRINSICS_H_
#define _HEXAGON_CIRC_BREV_INTRINSICS_H_ 1

#include <hexagon_protos.h>
#include <stdint.h>

/* Circular Load */
/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_circ_load_update_D(Word64 dst, Word64 *ptr, UWord32 incr, UWord32 bufsize, UWord32 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_load_update_D(dest,ptr,incr,bufsize,K)  \
    { ptr = (int64_t *) HEXAGON_circ_ldd (ptr, &(dest), ((((K)+1)<<24)|((bufsize)<<3)), ((incr)*8)); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_circ_load_update_W(Word32 dst, Word32 *ptr, UWord32 incr, UWord32 bufsize, UWord32 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_load_update_W(dest,ptr,incr,bufsize,K)  \
    { ptr = (int *) HEXAGON_circ_ldw (ptr, &(dest), (((K)<<24)|((bufsize)<<2)), ((incr)*4)); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_circ_load_update_H(Word16 dst, Word16 *ptr, UWord32 incr, UWord32 bufsize, UWord32 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_load_update_H(dest,ptr,incr,bufsize,K)  \
    { ptr = (int16_t *) HEXAGON_circ_ldh (ptr, &(dest), ((((K)-1)<<24)|((bufsize)<<1)), ((incr)*2)); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_circ_load_update_UH( UWord16 dst,  UWord16 *ptr, UWord32 incr, UWord32 bufsize, UWord32 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_load_update_UH(dest,ptr,incr,bufsize,K) \
    { ptr = (uint16_t *) HEXAGON_circ_lduh (ptr, &(dest), ((((K)-1)<<24)|((bufsize)<<1)), ((incr)*2)); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_circ_load_update_B(Word8 dst, Word8 *ptr, UWord32 incr, UWord32 bufsize, UWord32 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_load_update_B(dest,ptr,incr,bufsize,K)  \
    { ptr = (int8_t *) HEXAGON_circ_ldb (ptr, &(dest), ((((K)-2)<<24)|(bufsize)), incr); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void  Q6_circ_load_update_UB(UWord8 dst, UWord8 *ptr, UWord32 incr, UWord32 bufsize, UWord32 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_load_update_UB(dest,ptr,incr,bufsize,K) \
    { ptr = (uint8_t *) HEXAGON_circ_ldub (ptr, &(dest), ((((K)-2)<<24)|(bufsize)), incr); }

/* Circular Store */
/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_circ_store_update_D(Word64 *src, Word64 *ptr, UWord32 incr, UWord32 bufsize, UWord32 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_store_update_D(src,ptr,incr,bufsize,K)  \
    { ptr = (int64_t *) HEXAGON_circ_std (ptr, src, ((((K)+1)<<24)|((bufsize)<<3)), ((incr)*8)); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_circ_store_update_W(Word32 *src, Word32 *ptr, UWord32 incr, UWord32 bufsize, UWord32 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_store_update_W(src,ptr,incr,bufsize,K)  \
    { ptr = (int *) HEXAGON_circ_stw (ptr, src, (((K)<<24)|((bufsize)<<2)), ((incr)*4)); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_circ_store_update_HL(Word16 *src, Word16 *ptr, UWord32 incr, UWord32 bufsize, UWord32 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_store_update_HL(src,ptr,incr,bufsize,K) \
    { ptr = (int16_t *) HEXAGON_circ_sth (ptr, src, ((((K)-1)<<24)|((bufsize)<<1)), ((incr)*2)); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_circ_store_update_HH(Word16 *src, Word16 *ptr, UWord32 incr, UWord32 bufsize, UWord32 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_store_update_HH(src,ptr,incr,bufsize,K) \
    { ptr = (int16_t *) HEXAGON_circ_sthhi (ptr, src, ((((K)-1)<<24)|((bufsize)<<1)), ((incr)*2)); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_circ_store_update_B(Word8 *src, Word8 *ptr, UWord32 I4, UWord32 bufsize,  UWord64 K)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_circ_store_update_B(src,ptr,incr,bufsize,K)  \
    { ptr = (int8_t *) HEXAGON_circ_stb (ptr, src, ((((K)-2)<<24)|(bufsize)), incr); }


/* Bit Reverse Load */
/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_load_update_D(Word64 dst, Word64 *ptr, UWord32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_load_update_D(dest,ptr,log2bufsize) \
    { ptr = (int64_t *) HEXAGON_brev_ldd (ptr, &(dest), (1<<(16-((log2bufsize) + 3)))); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_load_update_W(Word32 dst, Word32 *ptr, UWord32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_load_update_W(dest,ptr,log2bufsize) \
    { ptr = (int *) HEXAGON_brev_ldw (ptr, &(dest), (1<<(16-((log2bufsize) + 2)))); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_load_update_H(Word16 dst, Word16 *ptr, UWord32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_load_update_H(dest,ptr,log2bufsize) \
    { ptr = (int16_t *) HEXAGON_brev_ldh (ptr, &(dest), (1<<(16-((log2bufsize) + 1)))); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_load_update_UH(UWord16 dst,  UWord16 *ptr, UWord32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_load_update_UH(dest,ptr,log2bufsize) \
    { ptr = (uint16_t *) HEXAGON_brev_lduh (ptr, &(dest), (1<<(16-((log2bufsize) + 1)))); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_load_update_B(Word8 dst, Word8 *ptr, UWord32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_load_update_B(dest,ptr,log2bufsize) \
    { ptr = (int8_t *) HEXAGON_brev_ldb (ptr, &(dest), (1<<(16-((log2bufsize))))); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_load_update_UB(UWord8 dst, UWord8 *ptr, UWord32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_load_update_UB(dest,ptr,log2bufsize) \
    { ptr = (uint8_t *) HEXAGON_brev_ldub (ptr, &(dest), (1<<(16-((log2bufsize))))); }

/* Bit Reverse Store */

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_store_update_D(Word64 *src, Word64 *ptr, UWord32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_store_update_D(src,ptr,log2bufsize)   \
    { ptr = (int64_t *) HEXAGON_brev_std (ptr, src, (1<<(16-((log2bufsize) + 3)))); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_store_update_W(Word32 *src, Word32 *ptr, UWord32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_store_update_W(src,ptr,log2bufsize)   \
    { ptr = (int *) HEXAGON_brev_stw (ptr, src, (1<<(16-((log2bufsize) + 2)))); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_store_update_HL(Word16 *src, Word16 *ptr, Word32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_store_update_HL(src,ptr,log2bufsize)   \
    { ptr = (int16_t *) HEXAGON_brev_sth (ptr, src, (1<<(16-((log2bufsize) + 1)))); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_store_update_HH(Word16 *src, Word16 *ptr, UWord32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_store_update_HH(src,ptr,log2bufsize)   \
    { ptr = (int16_t *) HEXAGON_brev_sthhi (ptr, src, (1<<(16-((log2bufsize) + 1)))); }

/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: void Q6_bitrev_store_update_B(Word8 *src, Word8 *ptr, UWord32 Iu4)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#define Q6_bitrev_store_update_B(src,ptr,log2bufsize)   \
    { ptr = (int8_t *) HEXAGON_brev_stb (ptr, src, (1<<(16-((log2bufsize))))); }


#define HEXAGON_circ_ldd  __builtin_circ_ldd
#define HEXAGON_circ_ldw  __builtin_circ_ldw
#define HEXAGON_circ_ldh  __builtin_circ_ldh
#define HEXAGON_circ_lduh __builtin_circ_lduh
#define HEXAGON_circ_ldb  __builtin_circ_ldb
#define HEXAGON_circ_ldub __builtin_circ_ldub


#define HEXAGON_circ_std  __builtin_circ_std
#define HEXAGON_circ_stw  __builtin_circ_stw
#define HEXAGON_circ_sth  __builtin_circ_sth
#define HEXAGON_circ_sthhi __builtin_circ_sthhi
#define HEXAGON_circ_stb  __builtin_circ_stb


#define HEXAGON_brev_ldd  __builtin_brev_ldd
#define HEXAGON_brev_ldw  __builtin_brev_ldw
#define HEXAGON_brev_ldh  __builtin_brev_ldh
#define HEXAGON_brev_lduh __builtin_brev_lduh
#define HEXAGON_brev_ldb  __builtin_brev_ldb
#define HEXAGON_brev_ldub __builtin_brev_ldub

#define HEXAGON_brev_std  __builtin_brev_std
#define HEXAGON_brev_stw  __builtin_brev_stw
#define HEXAGON_brev_sth  __builtin_brev_sth
#define HEXAGON_brev_sthhi __builtin_brev_sthhi
#define HEXAGON_brev_stb  __builtin_brev_stb

#ifdef __HVX__
/* ==========================================================================
   Assembly Syntax:       if (Qt) vmem(Rt+#0) = Vs
   C Intrinsic Prototype: void Q6_vmaskedstoreq_QAV(HVX_VectorPred Qt, HVX_VectorAddress A, HVX_Vector Vs)
   Instruction Type:      COPROC_VMEM
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_vmaskedstoreq_QAV __BUILTIN_VECTOR_WRAP(__builtin_HEXAGON_V6_vmaskedstoreq)

/* ==========================================================================
   Assembly Syntax:       if (!Qt) vmem(Rt+#0) = Vs
   C Intrinsic Prototype: void Q6_vmaskedstorenq_QAV(HVX_VectorPred Qt, HVX_VectorAddress A, HVX_Vector Vs)
   Instruction Type:      COPROC_VMEM
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_vmaskedstorenq_QAV __BUILTIN_VECTOR_WRAP(__builtin_HEXAGON_V6_vmaskedstorenq)

/* ==========================================================================
   Assembly Syntax:       if (Qt) vmem(Rt+#0):nt = Vs
   C Intrinsic Prototype: void Q6_vmaskedstorentq_QAV(HVX_VectorPred Qt, HVX_VectorAddress A, HVX_Vector Vs)
   Instruction Type:      COPROC_VMEM
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_vmaskedstorentq_QAV __BUILTIN_VECTOR_WRAP(__builtin_HEXAGON_V6_vmaskedstorentq)

/* ==========================================================================
   Assembly Syntax:       if (!Qt) vmem(Rt+#0):nt = Vs
   C Intrinsic Prototype: void Q6_vmaskedstorentnq_QAV(HVX_VectorPred Qt, HVX_VectorAddress A, HVX_Vector Vs)
   Instruction Type:      COPROC_VMEM
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_vmaskedstorentnq_QAV __BUILTIN_VECTOR_WRAP(__builtin_HEXAGON_V6_vmaskedstorentnq)

#endif


#endif  /* #ifndef _HEXAGON_CIRC_BREV_INTRINSICS_H_ */

#ifdef __NOT_DEFINED__
/*** comment block template  ***/
/* ==========================================================================
   Assembly Syntax:       Return=instruction()
   C Intrinsic Prototype: ReturnType Intrinsic(ParamType Rs, ParamType Rt)
   Instruction Type:      InstructionType
   Execution Slots:       SLOT0123
   ========================================================================== */
#endif /***  __NOT_DEFINED__  ***/
