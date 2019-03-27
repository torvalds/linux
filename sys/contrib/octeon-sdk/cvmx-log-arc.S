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







//
// The function defined here is called for every function as it is executed.
// These calls are automatically inserted by GCC when the switch "-pg" is
// used. This allows cvmx-log to add a PC entry as each function is executed.
// This information, along with the timestamps can give the user a good idea
// of the performance characteristics of their program. This function normally
// takes about 22 cycles to execute.
//

#ifdef __linux__
#include <asm/asm.h>
#include <asm/regdef.h>
#define LA dla
#else
#include <machine/asm.h>
#include <machine/regdef.h>
#define LA la
#endif

.set noreorder
.set noat
LEAF(_mcount)
	//
	// All registers we use must be saved since calls are added by gcc
	// after register allocation. The at register ($3) will contain the
	// original ra register before the _mcount call. Also the compiler
	// automatically performs a "dsubu sp, sp, 16" before we're called.
	// At the end of this function all registers must have their original
	// values and the stack pointr must be adjusted by 16. This code is
	// pretty unreadable since it has been arranged to promote dual issue.
	//
#ifdef __linux__
	dsubu	sp, sp, 32
#else
	dsubu	sp, sp, 16
#endif
	sd	s3, 24(sp)				// Save register
	rdhwr	s3, $31					// Read the cycle count
	sd	s0, 0(sp)				// Save register
	LA	s0, cvmx_log_buffer_end_ptr		// Load the address of the end of the log buffer
	sd	s1, 8(sp)				// Save register
	LA	s1, cvmx_log_buffer_write_ptr		// Load the address of the location in the log buffer
	sd	s2, 16(sp)				// Save register
	ld	s0, 0(s0)				// Get value of the current log buffer end location
	ld	s2, 0(s1)				// Get value of the current log buffer location
	dsubu	s0, s0, s2				// Subtract the end pointer and the write pointer
	sltiu	s0, s0, 16				// Check if there are at least 16 bytes
	bne	s0, $0, call_c_pc			// Call the slow C function if we don't have room in the log
	li	s0, 0x001				// 11 bit constant that matches the first 11 bits of a CVMX_LOG_TYPE_PC header
	sd	ra, 8(s2)				// Write the pc to the log
	dins	s3, s0, 53, 11				// Overwrite the upper cycle count bits with the CVMX_LOG_TYPE_PC header
	sd	s3, 0(s2)				// Write the log header
	daddu	s2, s2, 16				// Increment the write location ptr
	sd	s2, 0(s1)				// Store the write location ptr
return_c_pc:
	ld	s0, 0(sp)				// Restore register
	ld	s1, 8(sp)				// Restore register
	ld	s2, 16(sp)				// Restore register
	ld	s3, 24(sp)				// Restore register
	daddu	sp, sp, 32				// Pop everything off the stack, even the 16 bytes done by gcc
	jr	ra					// Return to the caller and
	or	ra, $1, $1				// make sure the ra is back to its original value

call_c_pc:
	// The registers used by the C code may change based on optimizations. To be
	// safe, I'll save all registers. We're in the slow path case anyway.
	dsubu	sp, sp, 216
	sd	$1, 0(sp)
	sd	$2, 8(sp)
	sd	$3, 16(sp)
	sd 	$4, 24(sp)
	sd 	$5, 32(sp)
	sd	$6, 40(sp)
	sd	$7, 48(sp)
	sd	$8, 56(sp)
	sd	$9, 64(sp)
	sd	$10, 72(sp)
	sd	$11, 80(sp)
	sd	$12, 88(sp)
	sd	$13, 96(sp)
	sd	$14, 104(sp)
	sd	$15, 112(sp)
	// s0, s1, s2, s3 are already saved
	sd	$20, 120(sp)
	sd	$21, 128(sp)
	sd	$22, 136(sp)
	sd	$23, 144(sp)
	sd	$24, 152(sp)
	sd	$25, 160(sp)
	sd	$26, 168(sp)
	sd	$27, 176(sp)
	sd	$28, 184(sp)
	sd	$29, 192(sp)
	sd	$30, 200(sp)
	sd	$31, 208(sp)

	or	a0, ra, ra
	jal	cvmx_log_pc
	nop

	ld	$1, 0(sp)
	ld	$2, 8(sp)
	ld	$3, 16(sp)
	ld 	$4, 24(sp)
	ld 	$5, 32(sp)
	ld	$6, 40(sp)
	ld	$7, 48(sp)
	ld	$8, 56(sp)
	ld	$9, 64(sp)
	ld	$10, 72(sp)
	ld	$11, 80(sp)
	ld	$12, 88(sp)
	ld	$13, 96(sp)
	ld	$14, 104(sp)
	ld	$15, 112(sp)
	// s0, s1, s2, s3 will be restored later
	ld	$20, 120(sp)
	ld	$21, 128(sp)
	ld	$22, 136(sp)
	ld	$23, 144(sp)
	ld	$24, 152(sp)
	ld	$25, 160(sp)
	ld	$26, 168(sp)
	ld	$27, 176(sp)
	ld	$28, 184(sp)
	ld	$29, 192(sp)
	ld	$30, 200(sp)
	ld	$31, 208(sp)
	b	return_c_pc
	daddu	sp, sp, 216

END(_mcount)

