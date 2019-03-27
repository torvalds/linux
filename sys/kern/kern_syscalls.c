/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Assar Westerlund
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
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sx.h>
#include <sys/syscall.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <machine/atomic.h>

/*
 * Acts like "nosys" but can be identified in sysent for dynamic call
 * number assignment for a limited number of calls.
 *
 * Place holder for system call slots reserved for loadable modules.
 */
int
lkmnosys(struct thread *td, struct nosys_args *args)
{

	return (nosys(td, args));
}

int
lkmressys(struct thread *td, struct nosys_args *args)
{

	return (nosys(td, args));
}

static void
syscall_thread_drain(struct sysent *se)
{
	u_int32_t cnt, oldcnt;

	do {
		oldcnt = se->sy_thrcnt;
		KASSERT((oldcnt & SY_THR_STATIC) == 0,
		    ("drain on static syscall"));
		cnt = oldcnt | SY_THR_DRAINING;
	} while (atomic_cmpset_acq_32(&se->sy_thrcnt, oldcnt, cnt) == 0);
	while (atomic_cmpset_32(&se->sy_thrcnt, SY_THR_DRAINING,
	    SY_THR_ABSENT) == 0)
		pause("scdrn", hz/2);
}

int
_syscall_thread_enter(struct thread *td, struct sysent *se)
{
	u_int32_t cnt, oldcnt;

	do {
		oldcnt = se->sy_thrcnt;
		if ((oldcnt & (SY_THR_DRAINING | SY_THR_ABSENT)) != 0)
			return (ENOSYS);
		cnt = oldcnt + SY_THR_INCR;
	} while (atomic_cmpset_acq_32(&se->sy_thrcnt, oldcnt, cnt) == 0);
	return (0);
}

void
_syscall_thread_exit(struct thread *td, struct sysent *se)
{
	u_int32_t cnt, oldcnt;

	do {
		oldcnt = se->sy_thrcnt;
		cnt = oldcnt - SY_THR_INCR;
	} while (atomic_cmpset_rel_32(&se->sy_thrcnt, oldcnt, cnt) == 0);
}

int
kern_syscall_register(struct sysent *sysents, int *offset,
    struct sysent *new_sysent, struct sysent *old_sysent, int flags)
{
	int i;

	if ((flags & ~SY_THR_STATIC) != 0)
		return (EINVAL);

	if (*offset == NO_SYSCALL) {
		for (i = 1; i < SYS_MAXSYSCALL; ++i)
			if (sysents[i].sy_call == (sy_call_t *)lkmnosys)
				break;
		if (i == SYS_MAXSYSCALL)
			return (ENFILE);
		*offset = i;
	} else if (*offset < 0 || *offset >= SYS_MAXSYSCALL)
		return (EINVAL);
	else if (sysents[*offset].sy_call != (sy_call_t *)lkmnosys &&
	    sysents[*offset].sy_call != (sy_call_t *)lkmressys)
		return (EEXIST);

	KASSERT(sysents[*offset].sy_thrcnt == SY_THR_ABSENT,
	    ("dynamic syscall is not protected"));
	*old_sysent = sysents[*offset];
	new_sysent->sy_thrcnt = SY_THR_ABSENT;
	sysents[*offset] = *new_sysent;
	atomic_store_rel_32(&sysents[*offset].sy_thrcnt, flags);
	return (0);
}

int
kern_syscall_deregister(struct sysent *sysents, int offset,
    const struct sysent *old_sysent)
{
	struct sysent *se;

	if (offset == 0)
		return (0); /* XXX? */

	se = &sysents[offset];
	if ((se->sy_thrcnt & SY_THR_STATIC) != 0)
		return (EINVAL);
	syscall_thread_drain(se);
	sysents[offset] = *old_sysent;
	return (0);
}

int
syscall_module_handler(struct module *mod, int what, void *arg)
{

	return (kern_syscall_module_handler(sysent, mod, what, arg));
}

int
kern_syscall_module_handler(struct sysent *sysents, struct module *mod,
    int what, void *arg)
{
	struct syscall_module_data *data = arg;
	modspecific_t ms;
	int error;

	switch (what) {
	case MOD_LOAD:
		error = kern_syscall_register(sysents, data->offset,
		    data->new_sysent, &data->old_sysent, data->flags);
		if (error) {
			/* Leave a mark so we know to safely unload below. */
			data->offset = NULL;
			return (error);
		}
		ms.intval = *data->offset;
		MOD_XLOCK;
		module_setspecific(mod, &ms);
		MOD_XUNLOCK;
		if (data->chainevh)
			error = data->chainevh(mod, what, data->chainarg);
		return (error);
	case MOD_UNLOAD:
		/*
		 * MOD_LOAD failed, so just return without calling the
		 * chained handler since we didn't pass along the MOD_LOAD
		 * event.
		 */
		if (data->offset == NULL)
			return (0);
		if (data->chainevh) {
			error = data->chainevh(mod, what, data->chainarg);
			if (error)
				return error;
		}
		error = kern_syscall_deregister(sysents, *data->offset,
		    &data->old_sysent);
		return (error);
	default:
		if (data->chainevh)
			return (data->chainevh(mod, what, data->chainarg));
		return (EOPNOTSUPP);
	}

	/* NOTREACHED */
}

int
syscall_helper_register(struct syscall_helper_data *sd, int flags)
{

	return (kern_syscall_helper_register(sysent, sd, flags));
}

int
kern_syscall_helper_register(struct sysent *sysents,
    struct syscall_helper_data *sd, int flags)
{
	struct syscall_helper_data *sd1;
	int error;

	for (sd1 = sd; sd1->syscall_no != NO_SYSCALL; sd1++) {
		error = kern_syscall_register(sysents, &sd1->syscall_no,
		    &sd1->new_sysent, &sd1->old_sysent, flags);
		if (error != 0) {
			kern_syscall_helper_unregister(sysents, sd);
			return (error);
		}
		sd1->registered = 1;
	}
	return (0);
}

int
syscall_helper_unregister(struct syscall_helper_data *sd)
{

	return (kern_syscall_helper_unregister(sysent, sd));
}

int
kern_syscall_helper_unregister(struct sysent *sysents,
    struct syscall_helper_data *sd)
{
	struct syscall_helper_data *sd1;

	for (sd1 = sd; sd1->registered != 0; sd1++) {
		kern_syscall_deregister(sysents, sd1->syscall_no,
		    &sd1->old_sysent);
		sd1->registered = 0;
	}
	return (0);
}
