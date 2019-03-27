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










#include <machine/asm.h>
#include <machine/regdef.h>

.set noreorder
.set noat

LEAF(cvmx_interrupt_stage1)
	dla     k0, cvmx_interrupt_stage2
	jalr 	k1, k0   // Save our address in k1, so we can tell which
                         // vector we are coming from.
	nop
END(cvmx_interrupt_stage1)

#define STACK_SIZE  (36*8)
LEAF(cvmx_interrupt_stage2)
	dsubu	sp, sp, STACK_SIZE
	sd	zero, 0(sp)	// Just a place holder
	sd	$1, 8(sp)	// start saving registers
	sd	$2, 16(sp)
	sd 	$3, 24(sp)
	sd 	$4, 32(sp)
	sd	$5, 40(sp)
	sd	$6, 48(sp)
	sd	$7, 56(sp)
	sd	$8, 64(sp)
	sd	$9, 72(sp)
	sd	$10, 80(sp)
	sd	$11, 88(sp)
	sd	$12, 96(sp)
	sd	$13, 104(sp)
	sd	$14, 112(sp)
	sd	$15, 120(sp)
	sd	$16, 128(sp)
	sd	$17, 136(sp)
	sd	$18, 144(sp)
	sd	$19, 152(sp)
	sd	$20, 160(sp)
	sd	$21, 168(sp)
	sd	$22, 176(sp)
	sd	$23, 184(sp)
	sd	$24, 192(sp)
	sd	$25, 200(sp)
	sd	$26, 208(sp)
	sd	$27, 216(sp)
	mfhi	k0		// Reading lo and high takes multiple cycles
	mflo	k1		// Do it here so it completes by the time we need it
	sd	$28, 224(sp)
	daddu	$1, sp, STACK_SIZE // Correct the SP for the space we used
	sd	$1, 232(sp)
	sd	$30, 240(sp)
	sd	$31, 248(sp)	// saved all general purpose registers
	sd	k0, 256(sp)	// save hi
	sd	k1, 264(sp)	// save lo
        /* Save DCACHE error register early, since any non-errored DCACHE accesses will clear
        ** error bit */
        dmfc0   k0, $27, 1
        sd      k0, 272(sp)
        /* Store EPC for GCC's frame unwinder. */
        dmfc0   k0, $14
        sd      k0, 280(sp)

	dla	k0, cvmx_interrupt_in_isr
	li	k1, 1
	sw	k1, 0(k0)

	dla     k0, cvmx_interrupt_do_irq
	jal 	k0
	dadd	a0, sp, 0	// First argument is array of registers

	dla	k0, cvmx_interrupt_in_isr
	sw	$0, 0(k0)

	ld	k0, 256(sp)	// read hi
	ld	k1, 264(sp)	// read lo
	mthi	k0		// restore hi
	mtlo	k1		// restore lo

	ld	$1, 8(sp)	// start restoring registers
	ld	$2, 16(sp)
	ld 	$3, 24(sp)
	ld 	$4, 32(sp)
	ld	$5, 40(sp)
	ld	$6, 48(sp)
	ld	$7, 56(sp)
	ld	$8, 64(sp)
	ld	$9, 72(sp)
	ld	$10, 80(sp)
	ld	$11, 88(sp)
	ld	$12, 96(sp)
	ld	$13, 104(sp)
	ld	$14, 112(sp)
	ld	$15, 120(sp)
	ld	$16, 128(sp)
	ld	$17, 136(sp)
	ld	$18, 144(sp)
	ld	$19, 152(sp)
	ld	$20, 160(sp)
	ld	$21, 168(sp)
	ld	$22, 176(sp)
	ld	$23, 184(sp)
	ld	$24, 192(sp)
	ld	$25, 200(sp)
	ld	$26, 208(sp)
	ld	$28, 224(sp)
	ld	$30, 240(sp)
	ld	$31, 248(sp)	// restored all general purpose registers
	ld	$29, 232(sp)	// No need to correct for STACK_SIZE
	eret
	nop
END(cvmx_interrupt_stage2)

// Icache and Dcache exception handler. This code is executed
// with ERL set so we can't us virtual addresses. We save and restore
// K0 to a global memory location so we can handle cache errors from exception
// context. This means that if two cores get a cache exception at the same time
// the K0 might be corrupted. This entire handler MUST fit in 128 bytes.
#define K0_STORE_LOCATION	8
#define DCACHE_ERROR_COUNT	16
#define ICACHE_ERROR_COUNT	24
LEAF(cvmx_interrupt_cache_error)
	.set push
	.set noreorder
	sd	k0, K0_STORE_LOCATION($0)	// Store K0 into global loc in case we're in an exception
	dmfc0	k0, $27, 1			// Get Dcache error status before any loads
	bbit0	k0, 0, not_dcache_error		// Skip dcache count if no error
	 dmtc0	k0, $27, 1			// Clear any Dcache errors
	ld	k0, DCACHE_ERROR_COUNT($0)	// Load the dcache error count
	daddu	k0, 1				// Increment the dcache error count
	sd	k0, DCACHE_ERROR_COUNT($0)	// Store the dcache error count
not_dcache_error:
	dmfc0	k0, $27, 0			// Get the Icache error status
	bbit0	k0, 0, not_icache_error		// Skip Icache count if no error
	 dmtc0	k0, $27, 0			// Clear any Icache errors
	ld	k0, ICACHE_ERROR_COUNT($0)	// Load the icache error count
	daddu	k0, 1				// Increment the icache error count
	sd	k0, ICACHE_ERROR_COUNT($0)	// Store the icache error count
not_icache_error:
	ld	k0, K0_STORE_LOCATION($0)	// Restore K0 since we might have been in an exception
	nop
	nop
	nop
	nop
	nop
	nop					// Keep the ERET 8 instructions away
	nop					// from a branch target.
	eret					// Return from the Icache exception
	.set pop
END(cvmx_interrupt_cache_error)

