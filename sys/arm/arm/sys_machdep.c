/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)sys_machdep.c	5.5 (Berkeley) 1/19/91
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_capsicum.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/capsicum.h>
#include <sys/proc.h>
#include <sys/sysproto.h>
#include <sys/syscall.h>
#include <sys/sysent.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <machine/cpu.h>
#include <machine/sysarch.h>
#include <machine/machdep.h>
#include <machine/vmparam.h>

#ifndef _SYS_SYSPROTO_H_
struct sysarch_args {
	int op;
	char *parms;
};
#endif

/* Prototypes */
static int arm32_sync_icache (struct thread *, void *);
static int arm32_drain_writebuf(struct thread *, void *);

#if __ARM_ARCH >= 6
static int
sync_icache(uintptr_t addr, size_t len)
{
	size_t size;
	vm_offset_t rv;

	/*
	 * Align starting address to even number because value of "1"
	 * is used as return value for success.
	 */
	len += addr & 1;
	addr &= ~1;

	/* Break whole range to pages. */
	do {
		size = PAGE_SIZE - (addr & PAGE_MASK);
		size = min(size, len);
		rv = dcache_wb_pou_checked(addr, size);
		if (rv == 1) /* see dcache_wb_pou_checked() */
			rv = icache_inv_pou_checked(addr, size);
		if (rv != 1) {
			if (!useracc((void *)addr, size, VM_PROT_READ)) {
				/* Invalid access */
				return (rv);
			}
			/* Valid but unmapped page - skip it. */
		}
		len -= size;
		addr += size;
	} while (len > 0);

	/* Invalidate branch predictor buffer. */
	bpb_inv_all();
	return (1);
}
#endif

static int
arm32_sync_icache(struct thread *td, void *args)
{
	struct arm_sync_icache_args ua;
	int error;
	ksiginfo_t ksi;
#if __ARM_ARCH >= 6
	vm_offset_t rv;
#endif

	if ((error = copyin(args, &ua, sizeof(ua))) != 0)
		return (error);

	if  (ua.len == 0) {
		td->td_retval[0] = 0;
		return (0);
	}

	/*
	 * Validate arguments. Address and length are unsigned,
	 * so we can use wrapped overflow check.
	 */
	if (((ua.addr + ua.len) < ua.addr) ||
	    ((ua.addr + ua.len) > VM_MAXUSER_ADDRESS)) {
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGSEGV;
		ksi.ksi_code = SEGV_ACCERR;
		ksi.ksi_addr = (void *)max(ua.addr, VM_MAXUSER_ADDRESS);
		trapsignal(td, &ksi);
		return (EINVAL);
	}

#if __ARM_ARCH >= 6
	rv = sync_icache(ua.addr, ua.len);
	if (rv != 1) {
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGSEGV;
		ksi.ksi_code = SEGV_MAPERR;
		ksi.ksi_addr = (void *)rv;
		trapsignal(td, &ksi);
		return (EINVAL);
	}
#else
	cpu_icache_sync_range(ua.addr, ua.len);
#endif

	td->td_retval[0] = 0;
	return (0);
}

static int
arm32_drain_writebuf(struct thread *td, void *args)
{
	/* No args. */

#if __ARM_ARCH < 6
	cpu_drain_writebuf();
#else
	dsb();
	cpu_l2cache_drain_writebuf();
#endif
	td->td_retval[0] = 0;
	return (0);
}

static int
arm32_set_tp(struct thread *td, void *args)
{

#if __ARM_ARCH >= 6
	set_tls(args);
#else
	td->td_md.md_tp = (register_t)args;
	*(register_t *)ARM_TP_ADDRESS = (register_t)args;
#endif
	return (0);
}

static int
arm32_get_tp(struct thread *td, void *args)
{

#if __ARM_ARCH >= 6
	td->td_retval[0] = (register_t)get_tls();
#else
	td->td_retval[0] = *(register_t *)ARM_TP_ADDRESS;
#endif
	return (0);
}

int
sysarch(struct thread *td, struct sysarch_args *uap)
{
	int error;

#ifdef CAPABILITY_MODE
	/*
	 * When adding new operations, add a new case statement here to
	 * explicitly indicate whether or not the operation is safe to
	 * perform in capability mode.
	 */
	if (IN_CAPABILITY_MODE(td)) {
		switch (uap->op) {
		case ARM_SYNC_ICACHE:
		case ARM_DRAIN_WRITEBUF:
		case ARM_SET_TP:
		case ARM_GET_TP:
		case ARM_GET_VFPSTATE:
			break;

		default:
#ifdef KTRACE
			if (KTRPOINT(td, KTR_CAPFAIL))
				ktrcapfail(CAPFAIL_SYSCALL, NULL, NULL);
#endif
			return (ECAPMODE);
		}
	}
#endif

	switch (uap->op) {
	case ARM_SYNC_ICACHE:
		error = arm32_sync_icache(td, uap->parms);
		break;
	case ARM_DRAIN_WRITEBUF:
		error = arm32_drain_writebuf(td, uap->parms);
		break;
	case ARM_SET_TP:
		error = arm32_set_tp(td, uap->parms);
		break;
	case ARM_GET_TP:
		error = arm32_get_tp(td, uap->parms);
		break;
	case ARM_GET_VFPSTATE:
		error = arm_get_vfpstate(td, uap->parms);
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}
