/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Daniel M. Eischen <deischen@freebsd.org>
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
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/signalvar.h>
#include <sys/ucontext.h>

/*
 * The first two fields of a ucontext_t are the signal mask and the machine
 * context.  The next field is uc_link; we want to avoid destroying the link
 * when copying out contexts.
 */
#define	UC_COPY_SIZE	offsetof(ucontext_t, uc_link)

#ifndef _SYS_SYSPROTO_H_
struct getcontext_args {
	struct __ucontext *ucp;
}
struct setcontext_args {
	const struct __ucontext_t *ucp;
}
struct swapcontext_args {
	struct __ucontext *oucp;
	const struct __ucontext_t *ucp;
}
#endif

int
sys_getcontext(struct thread *td, struct getcontext_args *uap)
{
	ucontext_t uc;
	int ret;

	if (uap->ucp == NULL)
		ret = EINVAL;
	else {
		bzero(&uc, sizeof(ucontext_t));
		get_mcontext(td, &uc.uc_mcontext, GET_MC_CLEAR_RET);
		PROC_LOCK(td->td_proc);
		uc.uc_sigmask = td->td_sigmask;
		PROC_UNLOCK(td->td_proc);
		ret = copyout(&uc, uap->ucp, UC_COPY_SIZE);
	}
	return (ret);
}

int
sys_setcontext(struct thread *td, struct setcontext_args *uap)
{
	ucontext_t uc;
	int ret;

	if (uap->ucp == NULL)
		ret = EINVAL;
	else {
		ret = copyin(uap->ucp, &uc, UC_COPY_SIZE);
		if (ret == 0) {
			ret = set_mcontext(td, &uc.uc_mcontext);
			if (ret == 0) {
				kern_sigprocmask(td, SIG_SETMASK, &uc.uc_sigmask,
				    NULL, 0);
			}
		}
	}
	return (ret == 0 ? EJUSTRETURN : ret);
}

int
sys_swapcontext(struct thread *td, struct swapcontext_args *uap)
{
	ucontext_t uc;
	int ret;

	if (uap->oucp == NULL || uap->ucp == NULL)
		ret = EINVAL;
	else {
		bzero(&uc, sizeof(ucontext_t));
		get_mcontext(td, &uc.uc_mcontext, GET_MC_CLEAR_RET);
		PROC_LOCK(td->td_proc);
		uc.uc_sigmask = td->td_sigmask;
		PROC_UNLOCK(td->td_proc);
		ret = copyout(&uc, uap->oucp, UC_COPY_SIZE);
		if (ret == 0) {
			ret = copyin(uap->ucp, &uc, UC_COPY_SIZE);
			if (ret == 0) {
				ret = set_mcontext(td, &uc.uc_mcontext);
				if (ret == 0) {
					kern_sigprocmask(td, SIG_SETMASK,
					    &uc.uc_sigmask, NULL, 0);
				}
			}
		}
	}
	return (ret == 0 ? EJUSTRETURN : ret);
}
