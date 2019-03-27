/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 David Xu <davidxu@freebsd.org>
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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/linker_set.h>
#include <sys/ptrace.h>
#include <proc_service.h>
#include <thread_db.h>

#include "libpthread_db.h"
#include "kse.h"

#define P2T(c) ps2td(c)

static void pt_unmap_lwp(const td_thragent_t *ta, lwpid_t lwp);
static int pt_validate(const td_thrhandle_t *th);

static int
ps2td(int c)
{
	switch (c) {
	case PS_OK:
		return TD_OK;
	case PS_ERR:
		return TD_ERR;
	case PS_BADPID:
		return TD_BADPH;
	case PS_BADLID:
		return TD_NOLWP;
	case PS_BADADDR:
		return TD_ERR;
	case PS_NOSYM:
		return TD_NOLIBTHREAD;
	case PS_NOFREGS:
		return TD_NOFPREGS;
	default:
		return TD_ERR;
	}
}

static long
pt_map_thread(const td_thragent_t *const_ta, psaddr_t pt, enum pt_type type)
{
	td_thragent_t *ta = __DECONST(td_thragent_t *, const_ta);
	struct pt_map *new;
	int first = -1;
	unsigned int i;

	/* leave zero out */
	for (i = 1; i < ta->map_len; ++i) {
		if (ta->map[i].type == PT_NONE) {
			if (first == -1)
				first = i;
		} else if (ta->map[i].type == type && ta->map[i].thr == pt) {
				return (i);
		}
	}

	if (first == -1) {
		if (ta->map_len == 0) {
			ta->map = calloc(20, sizeof(struct pt_map));
			if (ta->map == NULL)
				return (-1);
			ta->map_len = 20;
			first = 1;
		} else {
			new = reallocarray(ta->map, ta->map_len,
			    2 * sizeof(struct pt_map));
			if (new == NULL)
				return (-1);
			memset(new + ta->map_len, '\0', ta->map_len *
			    sizeof(struct pt_map));
			first = ta->map_len;
			ta->map = new;
			ta->map_len *= 2;
		}
	}

	ta->map[first].type = type;
	ta->map[first].thr = pt;
	return (first);
}

static td_err_e
pt_init(void)
{
	pt_md_init();
	return (0);
}

static td_err_e
pt_ta_new(struct ps_prochandle *ph, td_thragent_t **pta)
{
#define LOOKUP_SYM(proc, sym, addr) 			\
	ret = ps_pglobal_lookup(proc, NULL, sym, addr);	\
	if (ret != 0) {					\
		TDBG("can not find symbol: %s\n", sym);	\
		ret = TD_NOLIBTHREAD;			\
		goto error;				\
	}

#define	LOOKUP_VAL(proc, sym, val)			\
	ret = ps_pglobal_lookup(proc, NULL, sym, &vaddr);\
	if (ret != 0) {					\
		TDBG("can not find symbol: %s\n", sym);	\
		ret = TD_NOLIBTHREAD;			\
		goto error;				\
	}						\
	ret = ps_pread(proc, vaddr, val, sizeof(int));	\
	if (ret != 0) {					\
		TDBG("can not read value of %s\n", sym);\
		ret = TD_NOLIBTHREAD;			\
		goto error;				\
	}

	td_thragent_t *ta;
	psaddr_t vaddr;
	int dbg;
	int ret;

	TDBG_FUNC();

	ta = malloc(sizeof(td_thragent_t));
	if (ta == NULL)
		return (TD_MALLOC);

	ta->ph = ph;
	ta->thread_activated = 0;
	ta->map = NULL;
	ta->map_len = 0;

	LOOKUP_SYM(ph, "_libkse_debug",		&ta->libkse_debug_addr);
	LOOKUP_SYM(ph, "_thread_list",		&ta->thread_list_addr);
	LOOKUP_SYM(ph, "_thread_activated",	&ta->thread_activated_addr);
	LOOKUP_SYM(ph, "_thread_active_threads",&ta->thread_active_threads_addr);
	LOOKUP_SYM(ph, "_thread_keytable",	&ta->thread_keytable_addr);
	LOOKUP_VAL(ph, "_thread_off_dtv",	&ta->thread_off_dtv);
	LOOKUP_VAL(ph, "_thread_off_kse_locklevel", &ta->thread_off_kse_locklevel);
	LOOKUP_VAL(ph, "_thread_off_kse",	&ta->thread_off_kse);
	LOOKUP_VAL(ph, "_thread_off_tlsindex",	&ta->thread_off_tlsindex);
	LOOKUP_VAL(ph, "_thread_off_attr_flags",	&ta->thread_off_attr_flags);
	LOOKUP_VAL(ph, "_thread_size_key",	&ta->thread_size_key);
	LOOKUP_VAL(ph, "_thread_off_tcb",	&ta->thread_off_tcb);
	LOOKUP_VAL(ph, "_thread_off_linkmap",	&ta->thread_off_linkmap);
	LOOKUP_VAL(ph, "_thread_off_tmbx",	&ta->thread_off_tmbx);
	LOOKUP_VAL(ph, "_thread_off_thr_locklevel",	&ta->thread_off_thr_locklevel);
	LOOKUP_VAL(ph, "_thread_off_next",	&ta->thread_off_next);
	LOOKUP_VAL(ph, "_thread_off_state",	&ta->thread_off_state);
	LOOKUP_VAL(ph, "_thread_max_keys",	&ta->thread_max_keys);
	LOOKUP_VAL(ph, "_thread_off_key_allocated", &ta->thread_off_key_allocated);
	LOOKUP_VAL(ph, "_thread_off_key_destructor", &ta->thread_off_key_destructor);
	LOOKUP_VAL(ph, "_thread_state_running", &ta->thread_state_running);
	LOOKUP_VAL(ph, "_thread_state_zoombie", &ta->thread_state_zoombie);
	LOOKUP_VAL(ph, "_thread_off_sigmask",	&ta->thread_off_sigmask);
	LOOKUP_VAL(ph, "_thread_off_sigpend",	&ta->thread_off_sigpend);
	dbg = getpid();
	/*
	 * If this fails it probably means we're debugging a core file and
	 * can't write to it.
	 */
	ps_pwrite(ph, ta->libkse_debug_addr, &dbg, sizeof(int));
	*pta = ta;
	return (0);

error:
	free(ta);
	return (ret);
}

static td_err_e
pt_ta_delete(td_thragent_t *ta)
{
	int dbg;

	TDBG_FUNC();

	dbg = 0;
	/*
	 * Error returns from this write are not really a problem;
	 * the process doesn't exist any more.
	 */
	ps_pwrite(ta->ph, ta->libkse_debug_addr, &dbg, sizeof(int));
	if (ta->map)
		free(ta->map);
	free(ta);
	return (TD_OK);
}

static td_err_e
pt_ta_map_id2thr(const td_thragent_t *ta, thread_t id, td_thrhandle_t *th)
{
	prgregset_t gregs;
	psaddr_t pt, tcb_addr;
	lwpid_t lwp;
	int ret;

	TDBG_FUNC();

	if (id < 0 || id >= (long)ta->map_len || ta->map[id].type == PT_NONE)
		return (TD_NOTHR);

	ret = thr_pread_ptr(ta, ta->thread_list_addr, &pt);
	if (ret != 0)
		return (TD_ERR);
	if (ta->map[id].type == PT_LWP) {
		/*
		 * if we are referencing a lwp, make sure it was not already
		 * mapped to user thread.
		 */
		while (pt != 0) {
			ret = thr_pread_ptr(ta, pt + ta->thread_off_tcb,
			    &tcb_addr);
			if (ret != 0)
				return (TD_ERR);
			ret = thr_pread_int(ta, tcb_addr + ta->thread_off_tmbx +
			    offsetof(struct kse_thr_mailbox, tm_lwp), &lwp);
			if (ret != 0)
				return (TD_ERR);
			/*
			 * If the lwp was already mapped to userland thread,
			 * we shouldn't reference it directly in future.
			 */
			if (lwp == ta->map[id].lwp) {
				ta->map[id].type = PT_NONE;
				return (TD_NOTHR);
			}
			/* get next thread */
			ret = thr_pread_ptr(ta, pt + ta->thread_off_next, &pt);
			if (ret != 0)
				return (TD_ERR);
		}
		/* check lwp */
		ret = ps_lgetregs(ta->ph, ta->map[id].lwp, gregs);
		if (ret != PS_OK) {
			/* no longer exists */
			ta->map[id].type = PT_NONE;
			return (TD_NOTHR);
		}
	} else {
		while (pt != 0 && ta->map[id].thr != pt) {
			ret = thr_pread_ptr(ta, pt + ta->thread_off_tcb,
			    &tcb_addr);
			if (ret != 0)
				return (TD_ERR);
			/* get next thread */
			ret = thr_pread_ptr(ta, pt + ta->thread_off_next, &pt);
			if (ret != 0)
				return (TD_ERR);
		}

		if (pt == 0) {
			/* no longer exists */
			ta->map[id].type = PT_NONE;
			return (TD_NOTHR);
		}
	}
	th->th_ta = ta;
	th->th_tid = id;
	th->th_thread = pt;
	return (TD_OK);
}

static td_err_e
pt_ta_map_lwp2thr(const td_thragent_t *ta, lwpid_t lwp, td_thrhandle_t *th)
{
	psaddr_t pt, tcb_addr;
	lwpid_t lwp1;
	int ret;

	TDBG_FUNC();

	ret = thr_pread_ptr(ta, ta->thread_list_addr, &pt);
	if (ret != 0)
		return (TD_ERR);
	while (pt != 0) {
		ret = thr_pread_ptr(ta, pt + ta->thread_off_tcb, &tcb_addr);
		if (ret != 0)
			return (TD_ERR);
		ret = thr_pread_int(ta, tcb_addr + ta->thread_off_tmbx +
		    offsetof(struct kse_thr_mailbox, tm_lwp), &lwp1);
		if (ret != 0)
			return (TD_ERR);
		if (lwp1 == lwp) {
			th->th_ta = ta;
			th->th_tid = pt_map_thread(ta, pt, PT_USER);
			if (th->th_tid == -1)
				return (TD_MALLOC);
			pt_unmap_lwp(ta, lwp);
			th->th_thread = pt;
			return (TD_OK);
		}

		/* get next thread */
		ret = thr_pread_ptr(ta, pt + ta->thread_off_next, &pt);
		if (ret != 0)
			return (TD_ERR);
	}

	return (TD_NOTHR);
}

static td_err_e
pt_ta_thr_iter(const td_thragent_t *ta, td_thr_iter_f *callback,
    void *cbdata_p, td_thr_state_e state __unused, int ti_pri __unused,
    sigset_t *ti_sigmask_p __unused, unsigned int ti_user_flags __unused)
{
	td_thrhandle_t th;
	psaddr_t pt;
	ps_err_e pserr;
	int activated, ret;

	TDBG_FUNC();

	pserr = ps_pread(ta->ph, ta->thread_activated_addr, &activated,
	    sizeof(int));
	if (pserr != PS_OK)
		return (P2T(pserr));
	if (!activated)
		return (TD_OK);

	ret = thr_pread_ptr(ta, ta->thread_list_addr, &pt);
	if (ret != 0)
		return (TD_ERR);
	while (pt != 0) {
		th.th_ta = ta;
		th.th_tid = pt_map_thread(ta, pt, PT_USER);
		th.th_thread = pt;
		/* should we unmap lwp here ? */
		if (th.th_tid == -1)
			return (TD_MALLOC);
		if ((*callback)(&th, cbdata_p))
			return (TD_DBERR);
		/* get next thread */
		ret = thr_pread_ptr(ta, pt + ta->thread_off_next, &pt);
		if (ret != 0)
			return (TD_ERR);
	}
	return (TD_OK);
}

static td_err_e
pt_ta_tsd_iter(const td_thragent_t *ta, td_key_iter_f *ki, void *arg)
{
	void *keytable;
	void *destructor;
	int i, ret, allocated;

	TDBG_FUNC();

	keytable = malloc(ta->thread_max_keys * ta->thread_size_key);
	if (keytable == NULL)
		return (TD_MALLOC);
	ret = ps_pread(ta->ph, (psaddr_t)ta->thread_keytable_addr, keytable,
	               ta->thread_max_keys * ta->thread_size_key);
	if (ret != 0) {
		free(keytable);
		return (P2T(ret));
	}	
	for (i = 0; i < ta->thread_max_keys; i++) {
		allocated = *(int *)(void *)((uintptr_t)keytable +
		    i * ta->thread_size_key + ta->thread_off_key_allocated);
		destructor = *(void **)(void *)((uintptr_t)keytable +
		    i * ta->thread_size_key + ta->thread_off_key_destructor);
		if (allocated) {
			ret = (ki)(i, destructor, arg);
			if (ret != 0) {
				free(keytable);
				return (TD_DBERR);
			}
		}
	}
	free(keytable);
	return (TD_OK);
}

static td_err_e
pt_ta_event_addr(const td_thragent_t *ta __unused, td_event_e event __unused,
    td_notify_t *ptr __unused)
{
	TDBG_FUNC();
	return (TD_ERR);
}

static td_err_e
pt_ta_set_event(const td_thragent_t *ta __unused,
    td_thr_events_t *events __unused)
{
	TDBG_FUNC();
	return (0);
}

static td_err_e
pt_ta_clear_event(const td_thragent_t *ta __unused,
    td_thr_events_t *events __unused)
{
	TDBG_FUNC();
	return (0);
}

static td_err_e
pt_ta_event_getmsg(const td_thragent_t *ta __unused,
    td_event_msg_t *msg __unused)
{
	TDBG_FUNC();
	return (TD_NOMSG);
}

static td_err_e
pt_dbsuspend(const td_thrhandle_t *th, int suspend)
{
	const td_thragent_t *ta = th->th_ta;
	psaddr_t tcb_addr, tmbx_addr, ptr;
	lwpid_t lwp;
	uint32_t dflags;
	int attrflags, locklevel, ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (ta->map[th->th_tid].type == PT_LWP) {
		if (suspend)
			ret = ps_lstop(ta->ph, ta->map[th->th_tid].lwp);
		else
			ret = ps_lcontinue(ta->ph, ta->map[th->th_tid].lwp);
		return (P2T(ret));
	}

	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr +
		ta->thread_off_attr_flags,
		&attrflags, sizeof(attrflags));
	if (ret != 0)
		return (P2T(ret));
	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr +
	               ta->thread_off_tcb,
	               &tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));
	tmbx_addr = tcb_addr + ta->thread_off_tmbx;
	ptr = tmbx_addr + offsetof(struct kse_thr_mailbox, tm_lwp);
	ret = ps_pread(ta->ph, ptr, &lwp, sizeof(lwpid_t));
	if (ret != 0)
		return (P2T(ret));

	if (lwp != 0) {
		/* don't suspend signal thread */
		if (attrflags & 0x200)
			return (0);
		if (attrflags & PTHREAD_SCOPE_SYSTEM) {
			/*
			 * don't suspend system scope thread if it is holding
			 * some low level locks
			 */
			ptr = ta->map[th->th_tid].thr + ta->thread_off_kse;
			ret = ps_pread(ta->ph, ptr, &ptr, sizeof(ptr));
			if (ret != 0)
				return (P2T(ret));
			ret = ps_pread(ta->ph, ptr + ta->thread_off_kse_locklevel,
				&locklevel, sizeof(int));
			if (ret != 0)
				return (P2T(ret));
			if (locklevel <= 0) {
				ptr = ta->map[th->th_tid].thr +
					ta->thread_off_thr_locklevel;
				ret = ps_pread(ta->ph, ptr, &locklevel,
					sizeof(int));
				if (ret != 0)
					return (P2T(ret));
			}
			if (suspend) {
				if (locklevel <= 0)
					ret = ps_lstop(ta->ph, lwp);
			} else {
				ret = ps_lcontinue(ta->ph, lwp);
			}
			if (ret != 0)
				return (P2T(ret));
			/* FALLTHROUGH */
		} else {
			struct ptrace_lwpinfo pl;

			if (ps_linfo(ta->ph, lwp, (caddr_t)&pl))
				return (TD_ERR);
			if (suspend) {
				if (!(pl.pl_flags & PL_FLAG_BOUND))
					ret = ps_lstop(ta->ph, lwp);
			} else {
				ret = ps_lcontinue(ta->ph, lwp);
			}
			if (ret != 0)
				return (P2T(ret));
			/* FALLTHROUGH */
		}
	}
	/* read tm_dflags */
	ret = ps_pread(ta->ph,
		tmbx_addr + offsetof(struct kse_thr_mailbox, tm_dflags),
		&dflags, sizeof(dflags));
	if (ret != 0)
		return (P2T(ret));
	if (suspend)
		dflags |= TMDF_SUSPEND;
	else
		dflags &= ~TMDF_SUSPEND;
	ret = ps_pwrite(ta->ph,
	       tmbx_addr + offsetof(struct kse_thr_mailbox, tm_dflags),
	       &dflags, sizeof(dflags));
	return (P2T(ret));
}

static td_err_e
pt_thr_dbresume(const td_thrhandle_t *th)
{
	TDBG_FUNC();

	return pt_dbsuspend(th, 0);
}

static td_err_e
pt_thr_dbsuspend(const td_thrhandle_t *th)
{
	TDBG_FUNC();

	return pt_dbsuspend(th, 1);
}

static td_err_e
pt_thr_validate(const td_thrhandle_t *th)
{
	td_thrhandle_t temp;
	int ret;

	TDBG_FUNC();

	ret = pt_ta_map_id2thr(th->th_ta, th->th_tid,
	                       &temp);
	return (ret);
}

static td_err_e
pt_thr_old_get_info(const td_thrhandle_t *th, td_old_thrinfo_t *info)
{
	const td_thragent_t *ta = th->th_ta;
	struct ptrace_lwpinfo linfo;
	psaddr_t tcb_addr;
	uint32_t dflags;
	lwpid_t lwp;
	int state;
	int ret;
	int attrflags;

	TDBG_FUNC();

	bzero(info, sizeof(*info));
	ret = pt_validate(th);
	if (ret)
		return (ret);

	memset(info, 0, sizeof(*info));
	if (ta->map[th->th_tid].type == PT_LWP) {
		info->ti_type = TD_THR_SYSTEM;
		info->ti_lid = ta->map[th->th_tid].lwp;
		info->ti_tid = th->th_tid;
		info->ti_state = TD_THR_RUN;
		info->ti_type = TD_THR_SYSTEM;
		return (TD_OK);
	}

	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr +
		ta->thread_off_attr_flags,
		&attrflags, sizeof(attrflags));
	if (ret != 0)
		return (P2T(ret));
	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr + ta->thread_off_tcb,
	               &tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));
	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr + ta->thread_off_state,
	               &state, sizeof(state));
	ret = ps_pread(ta->ph,
	        tcb_addr + ta->thread_off_tmbx +
		 offsetof(struct kse_thr_mailbox, tm_lwp),
	        &info->ti_lid, sizeof(lwpid_t));
	if (ret != 0)
		return (P2T(ret));
	ret = ps_pread(ta->ph,
		tcb_addr + ta->thread_off_tmbx +
		 offsetof(struct kse_thr_mailbox, tm_dflags),
		&dflags, sizeof(dflags));
	if (ret != 0)
		return (P2T(ret));
	ret = ps_pread(ta->ph, tcb_addr + ta->thread_off_tmbx +
		offsetof(struct kse_thr_mailbox, tm_lwp), &lwp, sizeof(lwpid_t));
	if (ret != 0)
		return (P2T(ret));
	info->ti_ta_p = th->th_ta;
	info->ti_tid = th->th_tid;

	if (attrflags & PTHREAD_SCOPE_SYSTEM) {
		ret = ps_linfo(ta->ph, lwp, &linfo);
		if (ret == PS_OK) {
			info->ti_sigmask = linfo.pl_sigmask;
			info->ti_pending = linfo.pl_siglist;
		} else
			return (ret);
	} else {
		ret = ps_pread(ta->ph,
			ta->map[th->th_tid].thr + ta->thread_off_sigmask,
			&info->ti_sigmask, sizeof(sigset_t));
		if (ret)
			return (ret);
		ret = ps_pread(ta->ph,
			ta->map[th->th_tid].thr + ta->thread_off_sigpend,
			&info->ti_pending, sizeof(sigset_t));
		if (ret)
			return (ret);
	}

	if (state == ta->thread_state_running)
		info->ti_state = TD_THR_RUN;
	else if (state == ta->thread_state_zoombie)
		info->ti_state = TD_THR_ZOMBIE;
	else
		info->ti_state = TD_THR_SLEEP;
	info->ti_db_suspended = ((dflags & TMDF_SUSPEND) != 0);
	info->ti_type = TD_THR_USER;
	return (0);
}

static td_err_e
pt_thr_get_info(const td_thrhandle_t *th, td_thrinfo_t *info)
{
	td_err_e e;

	e = pt_thr_old_get_info(th, (td_old_thrinfo_t *)info);
	bzero(&info->ti_siginfo, sizeof(info->ti_siginfo));
	return (e);
}

#ifdef __i386__
static td_err_e
pt_thr_getxmmregs(const td_thrhandle_t *th, char *fxsave)
{
	const td_thragent_t *ta = th->th_ta;
	struct kse_thr_mailbox tmbx;
	psaddr_t tcb_addr, tmbx_addr, ptr;
	lwpid_t lwp;
	int ret;

	return TD_ERR;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (ta->map[th->th_tid].type == PT_LWP) {
		ret = ps_lgetxmmregs(ta->ph, ta->map[th->th_tid].lwp, fxsave);
		return (P2T(ret));
	}

	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr + ta->thread_off_tcb,
	               &tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));
	tmbx_addr = tcb_addr + ta->thread_off_tmbx;
	ptr = tmbx_addr + offsetof(struct kse_thr_mailbox, tm_lwp);
	ret = ps_pread(ta->ph, ptr, &lwp, sizeof(lwpid_t));
	if (ret != 0)
		return (P2T(ret));
	if (lwp != 0) {
		ret = ps_lgetxmmregs(ta->ph, lwp, fxsave);
		return (P2T(ret));
	}

	ret = ps_pread(ta->ph, tmbx_addr, &tmbx, sizeof(tmbx));
	if (ret != 0)
		return (P2T(ret));
	pt_ucontext_to_fxsave(&tmbx.tm_context, fxsave);
	return (0);
}
#endif

static td_err_e
pt_thr_getfpregs(const td_thrhandle_t *th, prfpregset_t *fpregs)
{
	const td_thragent_t *ta = th->th_ta;
	struct kse_thr_mailbox tmbx;
	psaddr_t tcb_addr, tmbx_addr, ptr;
	lwpid_t lwp;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (ta->map[th->th_tid].type == PT_LWP) {
		ret = ps_lgetfpregs(ta->ph, ta->map[th->th_tid].lwp, fpregs);
		return (P2T(ret));
	}

	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr + ta->thread_off_tcb,
	               &tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));
	tmbx_addr = tcb_addr + ta->thread_off_tmbx;
	ptr = tmbx_addr + offsetof(struct kse_thr_mailbox, tm_lwp);
	ret = ps_pread(ta->ph, ptr, &lwp, sizeof(lwpid_t));
	if (ret != 0)
		return (P2T(ret));
	if (lwp != 0) {
		ret = ps_lgetfpregs(ta->ph, lwp, fpregs);
		return (P2T(ret));
	}

	ret = ps_pread(ta->ph, tmbx_addr, &tmbx, sizeof(tmbx));
	if (ret != 0)
		return (P2T(ret));
	pt_ucontext_to_fpreg(&tmbx.tm_context, fpregs);
	return (0);
}

static td_err_e
pt_thr_getgregs(const td_thrhandle_t *th, prgregset_t gregs)
{
	const td_thragent_t *ta = th->th_ta;
	struct kse_thr_mailbox tmbx;
	psaddr_t tcb_addr, tmbx_addr, ptr;
	lwpid_t lwp;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (ta->map[th->th_tid].type == PT_LWP) {
		ret = ps_lgetregs(ta->ph,
		                  ta->map[th->th_tid].lwp, gregs);
		return (P2T(ret));
	}

	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr + ta->thread_off_tcb,
			&tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));
	tmbx_addr = tcb_addr + ta->thread_off_tmbx;
	ptr = tmbx_addr + offsetof(struct kse_thr_mailbox, tm_lwp);
	ret = ps_pread(ta->ph, ptr, &lwp, sizeof(lwpid_t));
	if (ret != 0)
		return (P2T(ret));
	if (lwp != 0) {
		ret = ps_lgetregs(ta->ph, lwp, gregs);
		return (P2T(ret));
	}
	ret = ps_pread(ta->ph, tmbx_addr, &tmbx, sizeof(tmbx));
	if (ret != 0)
		return (P2T(ret));
	pt_ucontext_to_reg(&tmbx.tm_context, gregs);
	return (0);
}

#ifdef __i386__
static td_err_e
pt_thr_setxmmregs(const td_thrhandle_t *th, const char *fxsave)
{
	const td_thragent_t *ta = th->th_ta;
	struct kse_thr_mailbox tmbx;
	psaddr_t tcb_addr, tmbx_addr, ptr;
	lwpid_t lwp;
	int ret;

	return TD_ERR;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (ta->map[th->th_tid].type == PT_LWP) {
		ret = ps_lsetxmmregs(ta->ph, ta->map[th->th_tid].lwp, fxsave);
		return (P2T(ret));
	}

	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr +
	                ta->thread_off_tcb,
                        &tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));
	tmbx_addr = tcb_addr + ta->thread_off_tmbx;
	ptr = tmbx_addr + offsetof(struct kse_thr_mailbox, tm_lwp);
	ret = ps_pread(ta->ph, ptr, &lwp, sizeof(lwpid_t));
	if (ret != 0)
		return (P2T(ret));
	if (lwp != 0) {
		ret = ps_lsetxmmregs(ta->ph, lwp, fxsave);
		return (P2T(ret));
	}
	/*
	 * Read a copy of context, this makes sure that registers
	 * not covered by structure reg won't be clobbered
	 */
	ret = ps_pread(ta->ph, tmbx_addr, &tmbx, sizeof(tmbx));
	if (ret != 0)
		return (P2T(ret));

	pt_fxsave_to_ucontext(fxsave, &tmbx.tm_context);
	ret = ps_pwrite(ta->ph, tmbx_addr, &tmbx, sizeof(tmbx));
	return (P2T(ret));
}
#endif

static td_err_e
pt_thr_setfpregs(const td_thrhandle_t *th, const prfpregset_t *fpregs)
{
	const td_thragent_t *ta = th->th_ta;
	struct kse_thr_mailbox tmbx;
	psaddr_t tcb_addr, tmbx_addr, ptr;
	lwpid_t lwp;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (ta->map[th->th_tid].type == PT_LWP) {
		ret = ps_lsetfpregs(ta->ph, ta->map[th->th_tid].lwp, fpregs);
		return (P2T(ret));
	}

	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr +
	                ta->thread_off_tcb,
                        &tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));
	tmbx_addr = tcb_addr + ta->thread_off_tmbx;
	ptr = tmbx_addr + offsetof(struct kse_thr_mailbox, tm_lwp);
	ret = ps_pread(ta->ph, ptr, &lwp, sizeof(lwpid_t));
	if (ret != 0)
		return (P2T(ret));
	if (lwp != 0) {
		ret = ps_lsetfpregs(ta->ph, lwp, fpregs);
		return (P2T(ret));
	}
	/*
	 * Read a copy of context, this makes sure that registers
	 * not covered by structure reg won't be clobbered
	 */
	ret = ps_pread(ta->ph, tmbx_addr, &tmbx, sizeof(tmbx));
	if (ret != 0)
		return (P2T(ret));

	pt_fpreg_to_ucontext(fpregs, &tmbx.tm_context);
	ret = ps_pwrite(ta->ph, tmbx_addr, &tmbx, sizeof(tmbx));
	return (P2T(ret));
}

static td_err_e
pt_thr_setgregs(const td_thrhandle_t *th, const prgregset_t gregs)
{
	const td_thragent_t *ta = th->th_ta;
	struct kse_thr_mailbox tmbx;
	psaddr_t tcb_addr, tmbx_addr, ptr;
	lwpid_t lwp;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (ta->map[th->th_tid].type == PT_LWP) {
		ret = ps_lsetregs(ta->ph, ta->map[th->th_tid].lwp, gregs);
		return (P2T(ret));
	}

	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr +
	                ta->thread_off_tcb,
	                &tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));
	tmbx_addr = tcb_addr + ta->thread_off_tmbx;
	ptr = tmbx_addr + offsetof(struct kse_thr_mailbox, tm_lwp);
	ret = ps_pread(ta->ph, ptr, &lwp, sizeof(lwpid_t));
	if (ret != 0)
		return (P2T(ret));
	if (lwp != 0) {
		ret = ps_lsetregs(ta->ph, lwp, gregs);
		return (P2T(ret));
	}

	/*
	 * Read a copy of context, make sure that registers
	 * not covered by structure reg won't be clobbered
	 */
	ret = ps_pread(ta->ph, tmbx_addr, &tmbx, sizeof(tmbx));
	if (ret != 0)
		return (P2T(ret));
	pt_reg_to_ucontext(gregs, &tmbx.tm_context);
	ret = ps_pwrite(ta->ph, tmbx_addr, &tmbx, sizeof(tmbx));
	return (P2T(ret));
}

static td_err_e
pt_thr_event_enable(const td_thrhandle_t *th __unused, int en __unused)
{
	TDBG_FUNC();
	return (0);
}

static td_err_e
pt_thr_set_event(const td_thrhandle_t *th __unused,
    td_thr_events_t *setp __unused)
{
	TDBG_FUNC();
	return (0);
}

static td_err_e
pt_thr_clear_event(const td_thrhandle_t *th __unused,
    td_thr_events_t *setp __unused)
{
	TDBG_FUNC();
	return (0);
}

static td_err_e
pt_thr_event_getmsg(const td_thrhandle_t *th __unused,
    td_event_msg_t *msg __unused)
{
	TDBG_FUNC();
	return (TD_NOMSG);
}

static td_err_e
pt_thr_sstep(const td_thrhandle_t *th, int step)
{
	const td_thragent_t *ta = th->th_ta;
	struct kse_thr_mailbox tmbx;
	struct reg regs;
	psaddr_t tcb_addr, tmbx_addr;
	uint32_t dflags;
	lwpid_t lwp;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (ta->map[th->th_tid].type == PT_LWP)
		return (TD_BADTH);

	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr + 
	                ta->thread_off_tcb,
	                &tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));

	/* Clear or set single step flag in thread mailbox */
	ret = ps_pread(ta->ph,
		tcb_addr + ta->thread_off_tmbx +
		 offsetof(struct kse_thr_mailbox, tm_dflags),
		&dflags, sizeof(uint32_t));
	if (ret != 0)
		return (P2T(ret));
	if (step != 0)
		dflags |= TMDF_SSTEP;
	else
		dflags &= ~TMDF_SSTEP;
	ret = ps_pwrite(ta->ph,
		tcb_addr + ta->thread_off_tmbx +
		 offsetof(struct kse_thr_mailbox, tm_dflags),
	        &dflags, sizeof(uint32_t));
	if (ret != 0)
		return (P2T(ret));
	/* Get lwp */
	ret = ps_pread(ta->ph,
		tcb_addr + ta->thread_off_tmbx +
		 offsetof(struct kse_thr_mailbox, tm_lwp),
		&lwp, sizeof(lwpid_t));
	if (ret != 0)
		return (P2T(ret));
	if (lwp != 0)
		return (0);

	tmbx_addr = tcb_addr + ta->thread_off_tmbx;
	/*
	 * context is in userland, some architectures store
	 * single step status in registers, we should change
	 * these registers.
	 */
	ret = ps_pread(ta->ph, tmbx_addr, &tmbx, sizeof(tmbx));
	if (ret == 0) {
		pt_ucontext_to_reg(&tmbx.tm_context, &regs);
		/* only write out if it is really changed. */
		if (pt_reg_sstep(&regs, step) != 0) {
			pt_reg_to_ucontext(&regs, &tmbx.tm_context);
			ret = ps_pwrite(ta->ph, tmbx_addr, &tmbx,
			                 sizeof(tmbx));
		}
	}
	return (P2T(ret));
}

static void
pt_unmap_lwp(const td_thragent_t *ta, lwpid_t lwp)
{
	unsigned int i;

	for (i = 0; i < ta->map_len; ++i) {
		if (ta->map[i].type == PT_LWP && ta->map[i].lwp == lwp) {
			ta->map[i].type = PT_NONE;
			return;
		}
	}
}

static int
pt_validate(const td_thrhandle_t *th)
{

	if (th->th_tid < 0 || th->th_tid >= (long)th->th_ta->map_len ||
	    th->th_ta->map[th->th_tid].type == PT_NONE)
		return (TD_NOTHR);
	return (TD_OK);
}

static td_err_e
pt_thr_tls_get_addr(const td_thrhandle_t *th, psaddr_t _linkmap, size_t offset,
    psaddr_t *address)
{
	const td_thragent_t *ta = th->th_ta;
	psaddr_t dtv_addr, obj_entry, tcb_addr;
	int tls_index, ret;

	/* linkmap is a member of Obj_Entry */
	obj_entry = _linkmap - ta->thread_off_linkmap;

	/* get tlsindex of the object file */
	ret = ps_pread(ta->ph,
		obj_entry + ta->thread_off_tlsindex,
		&tls_index, sizeof(tls_index));
	if (ret != 0)
		return (P2T(ret));

	/* get thread tcb */
	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr +
		ta->thread_off_tcb,
		&tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));

	/* get dtv array address */
	ret = ps_pread(ta->ph, tcb_addr + ta->thread_off_dtv,
		&dtv_addr, sizeof(dtv_addr));
	if (ret != 0)
		return (P2T(ret));
	/* now get the object's tls block base address */
	ret = ps_pread(ta->ph, dtv_addr + sizeof(void *) * (tls_index + 1),
	    address, sizeof(*address));
	if (ret != 0)
		return (P2T(ret));

	*address += offset;
	return (TD_OK);
}

static struct ta_ops libpthread_db_ops = {
	.to_init		= pt_init,
	.to_ta_clear_event	= pt_ta_clear_event,
	.to_ta_delete		= pt_ta_delete,
	.to_ta_event_addr	= pt_ta_event_addr,
	.to_ta_event_getmsg	= pt_ta_event_getmsg,
	.to_ta_map_id2thr	= pt_ta_map_id2thr,
	.to_ta_map_lwp2thr	= pt_ta_map_lwp2thr,
	.to_ta_new		= pt_ta_new,
	.to_ta_set_event	= pt_ta_set_event,
	.to_ta_thr_iter		= pt_ta_thr_iter,
	.to_ta_tsd_iter		= pt_ta_tsd_iter,
	.to_thr_clear_event	= pt_thr_clear_event,
	.to_thr_dbresume	= pt_thr_dbresume,
	.to_thr_dbsuspend	= pt_thr_dbsuspend,
	.to_thr_event_enable	= pt_thr_event_enable,
	.to_thr_event_getmsg	= pt_thr_event_getmsg,
	.to_thr_old_get_info	= pt_thr_old_get_info,
	.to_thr_get_info	= pt_thr_get_info,
	.to_thr_getfpregs	= pt_thr_getfpregs,
	.to_thr_getgregs	= pt_thr_getgregs,
	.to_thr_set_event	= pt_thr_set_event,
	.to_thr_setfpregs	= pt_thr_setfpregs,
	.to_thr_setgregs	= pt_thr_setgregs,
	.to_thr_validate	= pt_thr_validate,
	.to_thr_tls_get_addr	= pt_thr_tls_get_addr,

	/* FreeBSD specific extensions. */
	.to_thr_sstep		= pt_thr_sstep,
#ifdef __i386__
	.to_thr_getxmmregs	= pt_thr_getxmmregs,
	.to_thr_setxmmregs	= pt_thr_setxmmregs,
#endif
};

DATA_SET(__ta_ops, libpthread_db_ops);
