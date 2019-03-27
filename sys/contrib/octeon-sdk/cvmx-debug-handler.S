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

#undef __ASSEMBLY__
#define __ASSEMBLY__

#ifdef __linux__
#include <asm/asm.h>
#include <asm/regdef.h>
#else
#include <machine/asm.h>
#include <machine/regdef.h>
#endif

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx-asm.h>
#include <asm/octeon/octeon-boot-info.h>
#else

#include "cvmx-asm.h"

#ifndef _OCTEON_TOOLCHAIN_RUNTIME
#include <octeon_mem_map.h>
#else
#include "cvmx-platform.h"
#include "octeon-boot-info.h"
#endif

#endif

/* The registers saving/restoring is split into two because k0 is stored in the COP0_DESAVE register.  */
#define REGS0 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25
#define REGS1 27,28,29,30,31

#define SAVE_REGISTER(reg)	\
	sd reg, 0(k0);		\
	addi k0, 8

#define RESTORE_REGISTER(reg)	\
	ld reg, -8(k0);		\
	addi k0, -8

#define SAVE_COP0(reg)		\
	dmfc0 k1,reg;		\
	sd k1, 0(k0);		\
	addi k0, 8

#define RESTORE_COP0(reg)	\
	ld k1, -8(k0);		\
	addi k0, -8;		\
	dmtc0 k1,reg	

#define SAVE_ADDRESS(addr)	\
	dli k1, addr;		\
	ld k1, 0(k1);		\
	sd k1, 0(k0);		\
	addi k0, 8

#define RESTORE_ADDRESS(addr)	\
	dli t0, addr;		\
	ld k1, -8(k0);		\
	sd k1, 0(t0);		\
	addi k0, -8

#define REG_SAVE_BASE_DIV_8  (BOOTLOADER_DEBUG_REG_SAVE_BASE >> 3)


#define HW_INSTRUCTION_BREAKPOINT_STATUS            (0xFFFFFFFFFF301000)
#define HW_INSTRUCTION_BREAKPOINT_ADDRESS(num)      (0xFFFFFFFFFF301100 + 0x100 * (num))
#define HW_INSTRUCTION_BREAKPOINT_ADDRESS_MASK(num) (0xFFFFFFFFFF301108 + 0x100 * (num))
#define HW_INSTRUCTION_BREAKPOINT_ASID(num)         (0xFFFFFFFFFF301110 + 0x100 * (num))
#define HW_INSTRUCTION_BREAKPOINT_CONTROL(num)      (0xFFFFFFFFFF301118 + 0x100 * (num))

#define HW_DATA_BREAKPOINT_STATUS                   (0xFFFFFFFFFF302000)
#define HW_DATA_BREAKPOINT_ADDRESS(num)             (0xFFFFFFFFFF302100 + 0x100 * (num))
#define HW_DATA_BREAKPOINT_ADDRESS_MASK(num)        (0xFFFFFFFFFF302108 + 0x100 * (num))
#define HW_DATA_BREAKPOINT_ASID(num)                (0xFFFFFFFFFF302110 + 0x100 * (num))
#define HW_DATA_BREAKPOINT_CONTROL(num)             (0xFFFFFFFFFF302118 + 0x100 * (num))


#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#define loadaddr(reg, addr, shift) \
	dla	reg, addr##_all; \
        mfc0    $1, $15, 1; \
        andi    $1, 0xff; \
        sll     $1, shift; \
	add	reg, reg, $1
#else
#define loadaddr(reg, addr, shift) \
	dla	reg, addr
#endif


	.set	noreorder
	.set	noat

	.text

// Detect debug-mode exception, save all registers, create a stack and then
// call the stage3 C function.

	.ent	__cvmx_debug_handler_stage2
	.globl	__cvmx_debug_handler_stage2
__cvmx_debug_handler_stage2:
	// Save off k0 in COP0_DESAVE
	dmtc0	k0, COP0_DESAVE

	// Use reserved space in kseg0 to save off some temp regs
        mfc0    k0, $15, 1  // read exception base reg.
        andi    k0, 0xff    // mask off core ID
        sll     k0, 12      // multiply by 4096 (512 dwords) DEBUG_NUMREGS

        addiu   k0,  REG_SAVE_BASE_DIV_8
        addiu   k0,  REG_SAVE_BASE_DIV_8
        addiu   k0,  REG_SAVE_BASE_DIV_8
        addiu   k0,  REG_SAVE_BASE_DIV_8
        addiu   k0,  REG_SAVE_BASE_DIV_8
        addiu   k0,  REG_SAVE_BASE_DIV_8
        addiu   k0,  REG_SAVE_BASE_DIV_8
        addiu   k0,  REG_SAVE_BASE_DIV_8
		// add base offset - after exeption vectors for all cores

        rotr    k0, k0, 31   // set bit 31 for kseg0 access
        addi    k0, 1
        rotr    k0, k0, 1

	// save off k1 and at ($1) off to the bootloader reg save area
	// at is used by dla
	sd      $1, 8(k0)	// save at for temp usage
	sd      k1, 216(k0)	// save k1 for temp usage


	// Detect debug-mode exception.
	// If COP0_MULTICOREDEBUG[DExecC] is set,
	dmfc0	k1, COP0_MULTICOREDEBUG
	bbit0	k1, 16, noexc
	nop

	// COP0_DEBUG[DINT,DIB,DDBS,DBp,DSS] are not set and
	dmfc0	k1, COP0_DEBUG
	andi	k1, 0x3f
	bnez	k1, noexc
	nop

	// COP0_DEBUG[DExecC] is set.
	dmfc0	k1, COP0_DEBUG
	dext	k1,k1,10,5
	beqz	k1,noexc
	nop
	
	// We don't handle debug-mode exceptions in delay-slots so DEBUG[DBD]
	// should not be set.  If yes spin forever.
	dmfc0	k1, COP0_DEBUG
1:
	bbit1	k1, 31, 1b
	nop

	// It's a debug-mode exception.  Flag the occurence.  Also if it's
	// expected just ignore it but returning the subsequent instruction
	// after the fault.
	
	loadaddr (k1, __cvmx_debug_mode_exception_occured, 3)
	sd	k1, 0(k1)

	loadaddr (k1, __cvmx_debug_mode_exception_ignore, 3)
	ld	k1, 0(k1)
	beqz	k1, noexc
	nop

	// Restore k1 and at from the bootloader reg save area
	ld      $1, 8(k0)	// save at for temp usage
	ld      k1, 216(k0)	// save k1 for temp usage

	dmfc0	k0, COP0_DEPC
	// Skip the faulting instruction.
	daddiu	k0, 4
	jr	k0
	dmfc0	k0, COP0_DESAVE

noexc:	

	loadaddr (k1, __cvmx_debug_save_regs_area, 8)

	// Restore at
	ld      $1, 8(k0)	// restore at for temp usage

	.irp	n, REGS0
	sd	$\n, 0(k1)
	addiu	k1, 8
	.endr

	move	$25, k1
	ld      k1, 216(k0)	// restore k1 for temp usage
	move	k0, $25

	// Store out k0, we can use $25 here because we just saved it
	dmfc0	$25, COP0_DESAVE
	sd	$25, 0(k0)
	addiu	k0, 8

	.irp	n, REGS1
	sd	$\n, 0(k0)
	addiu	k0, 8
	.endr

	loadaddr(sp, __cvmx_debug_stack_top, 3)
	// Load the stack pointer as a pointer size.
#ifdef _ABIN32
	lw	sp,0(sp)
#else
	ld	sp,0(sp)
#endif
	mflo	$4
	mfhi	$5
	jal	__cvmx_debug_handler_stage3
	nop

	loadaddr(k0, __cvmx_debug_save_regs_area, 8)

	.irp	n, REGS0
	ld	$\n, 0(k0)
	addiu	k0, 8
	.endr

	// Restore k0 to COP0_DESAVE via k1
	ld	k1, 0(k0)
	addiu	k0, 8
	dmtc0	k1, COP0_DESAVE

	.irp	n, REGS1
	ld	$\n, 0(k0)
	addiu	k0, 8
	.endr
	
	dmfc0	k0, COP0_DESAVE
	// Flush the icache; by adding and removing SW breakpoints we change
	// the instruction stream.
	synci 	0($0)
	deret
	nop

	.end	__cvmx_debug_handler_stage2
