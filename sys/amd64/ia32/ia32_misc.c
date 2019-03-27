/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Konstantin Belousov
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <machine/cpu.h>
#include <machine/sysarch.h>

#include <compat/freebsd32/freebsd32_util.h>
#include <compat/freebsd32/freebsd32.h>
#include <compat/freebsd32/freebsd32_proto.h>

int
freebsd32_sysarch(struct thread *td, struct freebsd32_sysarch_args *uap)
{
	struct sysarch_args uap1;
	struct i386_ldt_args uapl;
	struct i386_ldt_args32 uapl32;
	int error;

	if (uap->op == I386_SET_LDT || uap->op == I386_GET_LDT) {
		if ((error = copyin(uap->parms, &uapl32, sizeof(uapl32))) != 0)
			return (error);
		uap1.op = uap->op;
		uap1.parms = (char *)&uapl;
		uapl.start = uapl32.start;
		uapl.descs = (struct user_segment_descriptor *)(uintptr_t)
		    uapl32.descs;
		uapl.num = uapl32.num;
		return (sysarch_ldt(td, &uap1, UIO_SYSSPACE));
	} else {
		uap1.op = uap->op;
		uap1.parms = uap->parms;
		return (sysarch(td, &uap1));
	}
}

#ifdef COMPAT_43
int
ofreebsd32_getpagesize(struct thread *td,
    struct ofreebsd32_getpagesize_args *uap)
{

	td->td_retval[0] = IA32_PAGE_SIZE;
	return (0);
}
#endif
