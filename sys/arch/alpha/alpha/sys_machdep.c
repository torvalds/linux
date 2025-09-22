/*	$OpenBSD: sys_machdep.c,v 1.8 2022/10/31 03:20:41 guenther Exp $	*/
/*	$NetBSD: sys_machdep.c,v 1.14 2002/01/14 00:53:16 thorpej Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#ifndef NO_IEEE
#include <sys/device.h>
#include <sys/proc.h>
#endif

#include <sys/mount.h>
#include <sys/syscallargs.h>

#ifndef NO_IEEE
#include <machine/fpu.h>
#include <machine/sysarch.h>

#include <dev/pci/pcivar.h>

int
sys_sysarch(struct proc *p, void *v, register_t *retval)
{
	struct sys_sysarch_args /* {
		syscallarg(int) op;
		syscallarg(void *) parms;
	} */ *uap = v;
	int error = 0;

	switch(SCARG(uap, op)) {
	case ALPHA_FPGETMASK:
		*retval = FP_C_TO_OPENBSD_MASK(p->p_md.md_flags);
		break;
	case ALPHA_FPGETSTICKY:
		*retval = FP_C_TO_OPENBSD_FLAG(p->p_md.md_flags);
		break;
	case ALPHA_FPSETMASK:
	case ALPHA_FPSETSTICKY:
	    {
		fp_except m;
		u_int64_t md_flags;
		struct alpha_fp_except_args args;

		error = copyin(SCARG(uap, parms), &args, sizeof args);
		if (error)
			return error;
		m = args.mask;
		md_flags = p->p_md.md_flags;
		switch (SCARG(uap, op)) {
		case ALPHA_FPSETMASK:
			*retval = FP_C_TO_OPENBSD_MASK(md_flags);
			md_flags = SET_FP_C_MASK(md_flags, m);
			break;
		case ALPHA_FPSETSTICKY:
			*retval = FP_C_TO_OPENBSD_FLAG(md_flags);
			md_flags = SET_FP_C_FLAG(md_flags, m);
			break;
		}
		alpha_write_fp_c(p, md_flags);
		break;
	    }
	case ALPHA_GET_FP_C:
	    {
		struct alpha_fp_c_args args;

		args.fp_c = alpha_read_fp_c(p);
		error = copyout(&args, SCARG(uap, parms), sizeof args);
		break;
	    }
	case ALPHA_SET_FP_C:
	    {
		struct alpha_fp_c_args args;

		error = copyin(SCARG(uap, parms), &args, sizeof args);
		if (error)
			return (error);
		if ((args.fp_c >> 63) != 0)
			args.fp_c |= IEEE_INHERIT;
		alpha_write_fp_c(p, args.fp_c);
		break;
	    }

	default:
		error = EINVAL;
		break;
	}

	return (error);
}
#else
int
sys_sysarch(struct proc *p, void *v, register_t *retval)
{
	return (ENOSYS);
}
#endif
