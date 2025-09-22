/*	$OpenBSD: lcore_access.S,v 1.36 2023/12/12 07:37:21 deraadt Exp $ */

/*
 * Copyright (c) 2001-2003 Opsycon AB  (www.opsycon.se / www.opsycon.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/errno.h>
#include <sys/syscall.h>

#include <machine/param.h>
#include <machine/asm.h>
#include <machine/cpu.h>
#include <mips64/mips_cpu.h>
#include <machine/regnum.h>
#include <machine/cpustate.h>
#include <machine/trap.h>

#include "assym.h"

	.set	mips3

	.set	noreorder		# Noreorder is default style!

/*
 * Primitives
 */

/*
 * This table is indexed by u.u_pcb.pcb_onfault in trap().
 * The reason for using this table rather than storing an address in
 * u.u_pcb.pcb_onfault is simply to make the code faster.
 * This table must match with definitions in trap.h.
 */
	.globl	onfault_table
	.data
	.align	3
onfault_table:
	PTR_VAL	0		# invalid index number
	PTR_VAL	_copyerr
	PTR_VAL	_kcopyerr
#if defined(DDB) || defined(DEBUG)
	PTR_VAL	kt_ddberr
#else
	PTR_VAL	0
#endif
	.text

/*
 * This code is copied the user's stack for returning from signal handlers
 * (see sendsig() and sigreturn()). We have to compute the address
 * of the sigcontext struct for the sigreturn call.
 */
	.globl	sigcode
sigcode:
	PTR_ADDU a0, sp, 4*REGSZ		# address of sigcontext
	LI	v0, SYS_sigreturn	# sigreturn(scp)
	.globl	sigcodecall
sigcodecall:
	syscall
	.globl	sigcoderet
sigcoderet:
	.globl	esigcode
esigcode:
	/* FALLTHROUGH */
	.globl	sigfill
sigfill:
	teq	zero, zero, 0x52
esigfill:

	.data
	.globl	sigfillsiz
sigfillsiz:
	.long	esigfill - sigfill

	.text

/*
 * Copy a null terminated string within the kernel address space.
 * Maxlength may be null if count not wanted.
 *	copystr(fromaddr, toaddr, maxlength, &lencopied)
 *		caddr_t fromaddr;
 *		caddr_t toaddr;
 *		u_int maxlength;
 *		u_long *lencopied;
 */
LEAF(copystr, 0)
	beq	a2, zero, 9f
	 move	t2, a2			# Save the number of bytes
	move	v0, zero
1:
	lbu	t0, 0(a0)
	PTR_SUBU a2, a2, 1
	sb	t0, 0(a1)
	PTR_ADDU a0, a0, 1
	beq	t0, zero, 2f
	 PTR_ADDU a1, a1, 1
	bne	a2, zero, 1b
	 NOP
9:
	LI	v0, ENAMETOOLONG	# String is longer than maxlength
2:
	beq	a3, zero, 3f
	 PTR_SUBU a2, t2, a2		# Compute length copied
	REG_S	a2, 0(a3)
3:
	j	ra
	 NOP
END(copystr)

/*
 * Optimized memory zero code.
 * mem_zero_page(addr);
 */
LEAF(mem_zero_page, 0)
	LI	v0, PAGE_SIZE
1:
	PTR_SUBU v0, 8
	sd	zero, 0(a0)
	bne	zero, v0, 1b
	 PTR_ADDU a0, 8
	jr	ra
	 NOP
END(mem_zero_page)

/*
 * Copy a null terminated string from the user address space into
 * the kernel address space.
 *
 *	_copyinstr(fromaddr, toaddr, maxlength, &lencopied)
 *		caddr_t fromaddr;
 *		caddr_t toaddr;
 *		u_int maxlength;
 *		u_int *lencopied;
 */
NON_LEAF(_copyinstr, FRAMESZ(CF_SZ), ra)
	PTR_SUBU sp, sp, FRAMESZ(CF_SZ)
	.mask	0x80000000, (CF_RA_OFFS - FRAMESZ(CF_SZ))
	PTR_S	ra, CF_RA_OFFS(sp)
	blt	a0, zero, _copyerr	# make sure address is in user space
	 LI	v0, KT_COPYERR
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	jal	copystr
	 sw	v0, PCB_ONFAULT(t3)

	PTR_L	ra, CF_RA_OFFS(sp)
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	sw	zero, PCB_ONFAULT(t3)
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ)
	j	ra
	 NOP
END(_copyinstr)

/*
 * Copy a null terminated string from the kernel address space into
 * the user address space.
 *
 *	copyoutstr(fromaddr, toaddr, maxlength, &lencopied)
 *		caddr_t fromaddr;
 *		caddr_t toaddr;
 *		u_int maxlength;
 *		u_int *lencopied;
 */
NON_LEAF(copyoutstr, FRAMESZ(CF_SZ), ra)
	PTR_SUBU sp, sp, FRAMESZ(CF_SZ)
	.mask	0x80000000, (CF_RA_OFFS - FRAMESZ(CF_SZ))
	PTR_S	ra, CF_RA_OFFS(sp)
	blt	a1, zero, _copyerr	# make sure address is in user space
	 LI	v0, KT_COPYERR
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	jal	copystr
	 sw	v0, PCB_ONFAULT(t3)

	PTR_L	ra, CF_RA_OFFS(sp)
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	sw	zero, PCB_ONFAULT(t3)
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ)
	j	ra
	 NOP
END(copyoutstr)

/*
 * Copy specified amount of data from user space into the kernel
 *	_copyin(from, to, len)
 *		caddr_t *from;	(user source address)
 *		caddr_t *to;	(kernel destination address)
 *		unsigned len;
 */
NON_LEAF(_copyin, FRAMESZ(CF_SZ), ra)
	PTR_SUBU sp, sp, FRAMESZ(CF_SZ)
	.mask	0x80000000, (CF_RA_OFFS - FRAMESZ(CF_SZ))
	PTR_S	ra, CF_RA_OFFS(sp)
	blt	a0, zero, _copyerr	# make sure address is in user space
	 li	v0, KT_COPYERR
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	jal	bcopy
	 sw	v0, PCB_ONFAULT(t3)

	PTR_L	ra, CF_RA_OFFS(sp)
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	sw	zero, PCB_ONFAULT(t3)
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ)
	j	ra
	 move	v0, zero
END(_copyin)

/*
 * Copy specified amount of data from kernel to the user space
 *	copyout(from, to, len)
 *		caddr_t *from;	(kernel source address)
 *		caddr_t *to;	(user destination address)
 *		unsigned len;
 */
NON_LEAF(copyout, FRAMESZ(CF_SZ), ra)
	PTR_SUBU sp, sp, FRAMESZ(CF_SZ)
	.mask	0x80000000, (CF_RA_OFFS - FRAMESZ(CF_SZ))
	PTR_S	ra, CF_RA_OFFS(sp)
	blt	a1, zero, _copyerr	# make sure address is in user space
	 li	v0, KT_COPYERR
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	jal	bcopy
	 sw	v0, PCB_ONFAULT(t3)

	PTR_L	ra, CF_RA_OFFS(sp)
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	sw	zero, PCB_ONFAULT(t3)
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ)
	j	ra
	 move	v0, zero
END(copyout)

/*
 * Copy an aligned 32-bit word atomically from user space into the kernel
 *	copyin32(from, to)
 *		uint32_t *from;	(user source address)
 *		uint32_t *to;	(kernel destination address)
 */
NON_LEAF(copyin32, FRAMESZ(CF_SZ), ra)
	PTR_SUBU sp, sp, FRAMESZ(CF_SZ)
	.mask	0x80000000, (CF_RA_OFFS - FRAMESZ(CF_SZ))
	PTR_S	ra, CF_RA_OFFS(sp)
	blt	a0, zero, _copyerr	# make sure address is in user space
	 li	v0, KT_COPYERR
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	sw	v0, PCB_ONFAULT(t3)

	lw	v1, 0(a0)
	sw	v1, 0(a1)

	sw	zero, PCB_ONFAULT(t3)
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ)
	j	ra
	 move	v0, zero
END(copyin32)

_copyerr:
	PTR_L	ra, CF_RA_OFFS(sp)
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	sw	zero, PCB_ONFAULT(t3)
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ)
	j	ra
	 li	v0, EFAULT		# return error

/*
 *  kcopy is a wrapper around bcopy that catches bad memory references.
 */
NON_LEAF(kcopy, FRAMESZ(CF_SZ + REGSZ), ra)
	PTR_SUBU sp, sp, FRAMESZ(CF_SZ + REGSZ)
	.mask	0x80000000, (CF_RA_OFFS - FRAMESZ(CF_SZ + REGSZ))
	PTR_S	ra, CF_RA_OFFS + REGSZ(sp)
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	lw	v1, PCB_ONFAULT(t3)
	li	v0, KT_KCOPYERR
	PTR_S	v1, CF_ARGSZ(sp)	# save old pcb_onfault
	jal	bcopy
	 sw	v0, PCB_ONFAULT(t3)

	PTR_L	v0, CF_ARGSZ(sp)
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	PTR_L	ra, CF_RA_OFFS + REGSZ(sp)
	sw	v0, PCB_ONFAULT(t3)
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ + REGSZ)
	j	ra
	 move	v0, zero
END(kcopy)

_kcopyerr:
	PTR_L	v0, CF_ARGSZ(sp)
	GET_CPU_INFO(t1, t0)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	PTR_L	ra, CF_RA_OFFS + REGSZ(sp)
	sw	v0, PCB_ONFAULT(t3)
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ + REGSZ)
	j	ra
	 li	v0, EFAULT		# return error
