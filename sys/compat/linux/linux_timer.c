/*-
 * Copyright (c) 2014 Bjoern A. Zeeb
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-11-C-0249
 * ("MRC2"), as part of the DARPA MRC research programme.
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

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/syscallsubr.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef COMPAT_LINUX32
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif
#include <compat/linux/linux_timer.h>


static int
linux_convert_l_sigevent(struct l_sigevent *l_sig, struct sigevent *sig)
{

	CP(*l_sig, *sig, sigev_notify);
	switch (l_sig->sigev_notify) {
	case L_SIGEV_SIGNAL:
		if (!LINUX_SIG_VALID(l_sig->sigev_signo))
			return (EINVAL);
		sig->sigev_notify = SIGEV_SIGNAL;
		sig->sigev_signo = linux_to_bsd_signal(l_sig->sigev_signo);
		PTRIN_CP(*l_sig, *sig, sigev_value.sival_ptr);
		break;
	case L_SIGEV_NONE:
		sig->sigev_notify = SIGEV_NONE;
		break;
	case L_SIGEV_THREAD:
#if 0
		/* Seems to not be used anywhere (anymore)? */
		sig->sigev_notify = SIGEV_THREAD;
		return (ENOSYS);
#else
		return (EINVAL);
#endif
	case L_SIGEV_THREAD_ID:
		if (!LINUX_SIG_VALID(l_sig->sigev_signo))
			return (EINVAL);
		sig->sigev_notify = SIGEV_THREAD_ID;
		CP2(*l_sig, *sig, _l_sigev_un._tid, sigev_notify_thread_id);
		sig->sigev_signo = linux_to_bsd_signal(l_sig->sigev_signo);
		PTRIN_CP(*l_sig, *sig, sigev_value.sival_ptr);
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

int
linux_timer_create(struct thread *td, struct linux_timer_create_args *uap)
{
	struct l_sigevent l_ev;
	struct sigevent ev, *evp;
	clockid_t nwhich;
	int error, id;

	if (uap->evp == NULL) {
		evp = NULL;
	} else {
		error = copyin(uap->evp, &l_ev, sizeof(l_ev));
		if (error != 0)
			return (error);
		error = linux_convert_l_sigevent(&l_ev, &ev);
		if (error != 0)
			return (error);
		evp = &ev;
	}
	error = linux_to_native_clockid(&nwhich, uap->clock_id);
	if (error != 0)
		return (error);
	error = kern_ktimer_create(td, nwhich, evp, &id, -1);
	if (error == 0) {
		error = copyout(&id, uap->timerid, sizeof(int));
		if (error != 0)
			kern_ktimer_delete(td, id);
	}
	return (error);
}

int
linux_timer_settime(struct thread *td, struct linux_timer_settime_args *uap)
{
	struct l_itimerspec l_val, l_oval;
	struct itimerspec val, oval, *ovalp;
	int error;

	error = copyin(uap->new, &l_val, sizeof(l_val));
	if (error != 0)
		return (error);
	ITS_CP(l_val, val);
	ovalp = uap->old != NULL ? &oval : NULL;
	error = kern_ktimer_settime(td, uap->timerid, uap->flags, &val, ovalp);
	if (error == 0 && uap->old != NULL) {
		ITS_CP(oval, l_oval);
		error = copyout(&l_oval, uap->old, sizeof(l_oval));
	}
	return (error);
}

int
linux_timer_gettime(struct thread *td, struct linux_timer_gettime_args *uap)
{
	struct l_itimerspec l_val;
	struct itimerspec val;
	int error;

	error = kern_ktimer_gettime(td, uap->timerid, &val);
	if (error == 0) {
		ITS_CP(val, l_val);
		error = copyout(&l_val, uap->setting, sizeof(l_val));
	}
	return (error);
}

int
linux_timer_getoverrun(struct thread *td, struct linux_timer_getoverrun_args *uap)
{

	return (kern_ktimer_getoverrun(td, uap->timerid));
}

int
linux_timer_delete(struct thread *td, struct linux_timer_delete_args *uap)
{

	return (kern_ktimer_delete(td, uap->timerid));
}
