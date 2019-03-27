/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marcel Moolenaar
 * Copyright (c) 2005 David Xu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <proc_service.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/linker_set.h>
#include <sys/ptrace.h>
#include <thread_db.h>
#include <unistd.h>

#include "thread_db_int.h"

#define	TERMINATED	1

struct td_thragent {
	TD_THRAGENT_FIELDS;
	psaddr_t	libthr_debug_addr;
	psaddr_t	thread_list_addr;
	psaddr_t	thread_active_threads_addr;
	psaddr_t	thread_keytable_addr;
	psaddr_t	thread_last_event_addr;
	psaddr_t	thread_event_mask_addr;
	psaddr_t	thread_bp_create_addr;
	psaddr_t	thread_bp_death_addr;
	int		thread_off_dtv;
	int		thread_off_tlsindex;
	int		thread_off_attr_flags;
	int		thread_size_key;
	int		thread_off_tcb;
	int		thread_off_linkmap;
	int		thread_off_next;
	int		thread_off_state;
	int		thread_off_tid;
	int		thread_max_keys;
	int		thread_off_key_allocated;
	int		thread_off_key_destructor;
	int		thread_off_report_events;
	int		thread_off_event_mask;
	int		thread_off_event_buf;
	int		thread_state_zoombie;
	int		thread_state_running;
};

#define P2T(c) ps2td(c)

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

static td_err_e
pt_init(void)
{
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

	LOOKUP_SYM(ph, "_libthr_debug",		&ta->libthr_debug_addr);
	LOOKUP_SYM(ph, "_thread_list",		&ta->thread_list_addr);
	LOOKUP_SYM(ph, "_thread_active_threads",&ta->thread_active_threads_addr);
	LOOKUP_SYM(ph, "_thread_keytable",	&ta->thread_keytable_addr);
	LOOKUP_SYM(ph, "_thread_last_event",	&ta->thread_last_event_addr);
	LOOKUP_SYM(ph, "_thread_event_mask",	&ta->thread_event_mask_addr);
	LOOKUP_SYM(ph, "_thread_bp_create",	&ta->thread_bp_create_addr);
	LOOKUP_SYM(ph, "_thread_bp_death",	&ta->thread_bp_death_addr);
	LOOKUP_VAL(ph, "_thread_off_dtv",	&ta->thread_off_dtv);
	LOOKUP_VAL(ph, "_thread_off_tlsindex",	&ta->thread_off_tlsindex);
	LOOKUP_VAL(ph, "_thread_off_attr_flags",	&ta->thread_off_attr_flags);
	LOOKUP_VAL(ph, "_thread_size_key",	&ta->thread_size_key);
	LOOKUP_VAL(ph, "_thread_off_tcb",	&ta->thread_off_tcb);
	LOOKUP_VAL(ph, "_thread_off_tid",	&ta->thread_off_tid);
	LOOKUP_VAL(ph, "_thread_off_linkmap",	&ta->thread_off_linkmap);
	LOOKUP_VAL(ph, "_thread_off_next",	&ta->thread_off_next);
	LOOKUP_VAL(ph, "_thread_off_state",	&ta->thread_off_state);
	LOOKUP_VAL(ph, "_thread_max_keys",	&ta->thread_max_keys);
	LOOKUP_VAL(ph, "_thread_off_key_allocated", &ta->thread_off_key_allocated);
	LOOKUP_VAL(ph, "_thread_off_key_destructor", &ta->thread_off_key_destructor);
	LOOKUP_VAL(ph, "_thread_state_running", &ta->thread_state_running);
	LOOKUP_VAL(ph, "_thread_state_zoombie", &ta->thread_state_zoombie);
	LOOKUP_VAL(ph, "_thread_off_report_events", &ta->thread_off_report_events);
	LOOKUP_VAL(ph, "_thread_off_event_mask", &ta->thread_off_event_mask);
	LOOKUP_VAL(ph, "_thread_off_event_buf", &ta->thread_off_event_buf);
	dbg = getpid();
	/*
	 * If this fails it probably means we're debugging a core file and
	 * can't write to it.
	 */
	ps_pwrite(ph, ta->libthr_debug_addr, &dbg, sizeof(int));
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
	ps_pwrite(ta->ph, ta->libthr_debug_addr, &dbg, sizeof(int));
	free(ta);
	return (TD_OK);
}

static td_err_e
pt_ta_map_id2thr(const td_thragent_t *ta, thread_t id, td_thrhandle_t *th)
{
	psaddr_t pt;
	int64_t lwp;
	int ret;

	TDBG_FUNC();

	if (id == 0)
		return (TD_NOTHR);
	ret = thr_pread_ptr(ta, ta->thread_list_addr, &pt);
	if (ret != 0)
		return (TD_ERR);
	/* Iterate through thread list to find pthread */
	while (pt != 0) {
		ret = thr_pread_long(ta, pt + ta->thread_off_tid, &lwp);
		if (ret != 0)
			return (TD_ERR);
		if (lwp == id)
			break;
		/* get next thread */
		ret = thr_pread_ptr(ta, pt + ta->thread_off_next, &pt);
		if (ret != 0)
			return (TD_ERR);
	}
	if (pt == 0)
		return (TD_NOTHR);
	th->th_ta = ta;
	th->th_tid = id;
	th->th_thread = pt;
	return (TD_OK);
}

static td_err_e
pt_ta_map_lwp2thr(const td_thragent_t *ta, lwpid_t lwp, td_thrhandle_t *th)
{
	return (pt_ta_map_id2thr(ta, lwp, th));
}

static td_err_e
pt_ta_thr_iter(const td_thragent_t *ta, td_thr_iter_f *callback,
    void *cbdata_p, td_thr_state_e state __unused, int ti_pri __unused,
    sigset_t *ti_sigmask_p __unused, unsigned int ti_user_flags __unused)
{
	td_thrhandle_t th;
	psaddr_t pt;
	int64_t lwp;
	int ret;

	TDBG_FUNC();

	ret = thr_pread_ptr(ta, ta->thread_list_addr, &pt);
	if (ret != 0)
		return (TD_ERR);
	while (pt != 0) {
		ret = thr_pread_long(ta, pt + ta->thread_off_tid, &lwp);
		if (ret != 0)
			return (TD_ERR);
		if (lwp != 0 && lwp != TERMINATED) {
			th.th_ta = ta;
			th.th_tid = (thread_t)lwp;
			th.th_thread = pt;
			if ((*callback)(&th, cbdata_p))
				return (TD_DBERR);
		}
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
pt_ta_event_addr(const td_thragent_t *ta, td_event_e event, td_notify_t *ptr)
{

	TDBG_FUNC();

	switch (event) {
	case TD_CREATE:
		ptr->type = NOTIFY_BPT;
		ptr->u.bptaddr = ta->thread_bp_create_addr;
		return (0);
	case TD_DEATH:
		ptr->type = NOTIFY_BPT;
		ptr->u.bptaddr = ta->thread_bp_death_addr;
		return (0);
	default:
		return (TD_ERR);
	}
}

static td_err_e
pt_ta_set_event(const td_thragent_t *ta, td_thr_events_t *events)
{
	td_thr_events_t mask;
	int ret;

	TDBG_FUNC();
	ret = ps_pread(ta->ph, ta->thread_event_mask_addr, &mask,
		sizeof(mask));
	if (ret != 0)
		return (P2T(ret));
	mask |= *events;
	ret = ps_pwrite(ta->ph, ta->thread_event_mask_addr, &mask,
		sizeof(mask));
	return (P2T(ret));
}

static td_err_e
pt_ta_clear_event(const td_thragent_t *ta, td_thr_events_t *events)
{
	td_thr_events_t mask;
	int ret;

	TDBG_FUNC();
	ret = ps_pread(ta->ph, ta->thread_event_mask_addr, &mask,
		sizeof(mask));
	if (ret != 0)
		return (P2T(ret));
	mask &= ~*events;
	ret = ps_pwrite(ta->ph, ta->thread_event_mask_addr, &mask,
		sizeof(mask));
	return (P2T(ret));
}

static td_err_e
pt_ta_event_getmsg(const td_thragent_t *ta, td_event_msg_t *msg)
{
	static td_thrhandle_t handle;

	psaddr_t pt;
	td_thr_events_e	tmp;
	int64_t lwp;
	int ret;

	TDBG_FUNC();

	ret = thr_pread_ptr(ta, ta->thread_last_event_addr, &pt);
	if (ret != 0)
		return (TD_ERR);
	if (pt == 0)
		return (TD_NOMSG);
	/*
	 * Take the event pointer, at the time, libthr only reports event
	 * once a time, so it is not a link list.
	 */
	thr_pwrite_ptr(ta, ta->thread_last_event_addr, 0);

	/* Read event info */
	ret = ps_pread(ta->ph, pt + ta->thread_off_event_buf, msg, sizeof(*msg));
	if (ret != 0)
		return (P2T(ret));
	if (msg->event == 0)
		return (TD_NOMSG);
	/* Clear event */
	tmp = 0;
	ps_pwrite(ta->ph, pt + ta->thread_off_event_buf, &tmp, sizeof(tmp));
	/* Convert event */
	pt = msg->th_p;
	ret = thr_pread_long(ta, pt + ta->thread_off_tid, &lwp);
	if (ret != 0)
		return (TD_ERR);
	handle.th_ta = ta;
	handle.th_tid = lwp;
	handle.th_thread = pt;
	msg->th_p = (uintptr_t)&handle;
	return (0);
}

static td_err_e
pt_dbsuspend(const td_thrhandle_t *th, int suspend)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (suspend)
		ret = ps_lstop(ta->ph, th->th_tid);
	else
		ret = ps_lcontinue(ta->ph, th->th_tid);
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

	ret = pt_ta_map_id2thr(th->th_ta, th->th_tid, &temp);
	return (ret);
}

static td_err_e
pt_thr_get_info_common(const td_thrhandle_t *th, td_thrinfo_t *info, int old)
{
	const td_thragent_t *ta = th->th_ta;
	struct ptrace_lwpinfo linfo;
	int traceme;
	int state;
	int ret;

	TDBG_FUNC();

	bzero(info, sizeof(*info));
	ret = pt_validate(th);
	if (ret)
		return (ret);
	ret = thr_pread_int(ta, th->th_thread + ta->thread_off_state, &state);
	if (ret != 0)
		return (TD_ERR);
	ret = thr_pread_int(ta, th->th_thread + ta->thread_off_report_events,
	    &traceme);
	info->ti_traceme = traceme;
	if (ret != 0)
		return (TD_ERR);
	ret = ps_pread(ta->ph, th->th_thread + ta->thread_off_event_mask,
		&info->ti_events, sizeof(td_thr_events_t));
	if (ret != 0)
		return (P2T(ret));
	ret = ps_pread(ta->ph, th->th_thread + ta->thread_off_tcb,
		&info->ti_tls, sizeof(void *));
	info->ti_lid = th->th_tid;
	info->ti_tid = th->th_tid;
	info->ti_thread = th->th_thread;
	info->ti_ta_p = th->th_ta;
	ret = ps_linfo(ta->ph, th->th_tid, &linfo);
	if (ret == PS_OK) {
		info->ti_sigmask = linfo.pl_sigmask;
		info->ti_pending = linfo.pl_siglist;
		if (!old) {
			if ((linfo.pl_flags & PL_FLAG_SI) != 0)
				info->ti_siginfo = linfo.pl_siginfo;
			else
				bzero(&info->ti_siginfo,
				    sizeof(info->ti_siginfo));
		}
	} else
		return (ret);
	if (state == ta->thread_state_running)
		info->ti_state = TD_THR_RUN;
	else if (state == ta->thread_state_zoombie)
		info->ti_state = TD_THR_ZOMBIE;
	else
		info->ti_state = TD_THR_SLEEP;
	info->ti_type = TD_THR_USER;
	return (0);
}

static td_err_e
pt_thr_old_get_info(const td_thrhandle_t *th, td_old_thrinfo_t *info)
{

	return (pt_thr_get_info_common(th, (td_thrinfo_t *)info, 1));
}

static td_err_e
pt_thr_get_info(const td_thrhandle_t *th, td_thrinfo_t *info)
{

	return (pt_thr_get_info_common(th, info, 0));
}

#ifdef __i386__
static td_err_e
pt_thr_getxmmregs(const td_thrhandle_t *th, char *fxsave)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	ret = ps_lgetxmmregs(ta->ph, th->th_tid, fxsave);
	return (P2T(ret));
}
#endif

static td_err_e
pt_thr_getfpregs(const td_thrhandle_t *th, prfpregset_t *fpregs)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	ret = ps_lgetfpregs(ta->ph, th->th_tid, fpregs);
	return (P2T(ret));
}

static td_err_e
pt_thr_getgregs(const td_thrhandle_t *th, prgregset_t gregs)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	ret = ps_lgetregs(ta->ph, th->th_tid, gregs);
	return (P2T(ret));
}

#ifdef __i386__
static td_err_e
pt_thr_setxmmregs(const td_thrhandle_t *th, const char *fxsave)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	ret = ps_lsetxmmregs(ta->ph, th->th_tid, fxsave);
	return (P2T(ret));
}
#endif

static td_err_e
pt_thr_setfpregs(const td_thrhandle_t *th, const prfpregset_t *fpregs)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	ret = ps_lsetfpregs(ta->ph, th->th_tid, fpregs);
	return (P2T(ret));
}

static td_err_e
pt_thr_setgregs(const td_thrhandle_t *th, const prgregset_t gregs)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	ret = ps_lsetregs(ta->ph, th->th_tid, gregs);
	return (P2T(ret));
}

static td_err_e
pt_thr_event_enable(const td_thrhandle_t *th, int en)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();
	ret = ps_pwrite(ta->ph, th->th_thread + ta->thread_off_report_events,
		&en, sizeof(int));
	return (P2T(ret));
}

static td_err_e
pt_thr_set_event(const td_thrhandle_t *th, td_thr_events_t *setp)
{
	const td_thragent_t *ta = th->th_ta;
	td_thr_events_t mask;
	int ret;

	TDBG_FUNC();
	ret = ps_pread(ta->ph, th->th_thread + ta->thread_off_event_mask,
			&mask, sizeof(mask));
	mask |= *setp;
	ret = ps_pwrite(ta->ph, th->th_thread + ta->thread_off_event_mask,
			&mask, sizeof(mask));
	return (P2T(ret));
}

static td_err_e
pt_thr_clear_event(const td_thrhandle_t *th, td_thr_events_t *setp)
{
	const td_thragent_t *ta = th->th_ta;
	td_thr_events_t mask;
	int ret;

	TDBG_FUNC();
	ret = ps_pread(ta->ph, th->th_thread + ta->thread_off_event_mask,
			&mask, sizeof(mask));
	mask &= ~*setp;
	ret = ps_pwrite(ta->ph, th->th_thread + ta->thread_off_event_mask,
			&mask, sizeof(mask));
	return (P2T(ret));
}

static td_err_e
pt_thr_event_getmsg(const td_thrhandle_t *th, td_event_msg_t *msg)
{
	static td_thrhandle_t handle;
	const td_thragent_t *ta = th->th_ta;
	psaddr_t pt, pt_temp;
	int64_t lwp;
	int ret;
	td_thr_events_e	tmp;

	TDBG_FUNC();
	pt = th->th_thread;
	ret = thr_pread_ptr(ta, ta->thread_last_event_addr, &pt_temp);
	if (ret != 0)
		return (TD_ERR);
	/* Get event */
	ret = ps_pread(ta->ph, pt + ta->thread_off_event_buf, msg, sizeof(*msg));
	if (ret != 0)
		return (P2T(ret));
	if (msg->event == 0)
		return (TD_NOMSG);
	/*
	 * Take the event pointer, at the time, libthr only reports event
	 * once a time, so it is not a link list.
	 */
	if (pt == pt_temp)
		thr_pwrite_ptr(ta, ta->thread_last_event_addr, 0);

	/* Clear event */
	tmp = 0;
	ps_pwrite(ta->ph, pt + ta->thread_off_event_buf, &tmp, sizeof(tmp));
	/* Convert event */
	pt = msg->th_p;
	ret = thr_pread_long(ta, pt + ta->thread_off_tid, &lwp);
	if (ret != 0)
		return (TD_ERR);
	handle.th_ta = ta;
	handle.th_tid = lwp;
	handle.th_thread = pt;
	msg->th_p = (uintptr_t)&handle;
	return (0);
}

static td_err_e
pt_thr_sstep(const td_thrhandle_t *th, int step __unused)
{
	TDBG_FUNC();

	return pt_validate(th);
}

static int
pt_validate(const td_thrhandle_t *th)
{

	if (th->th_tid == 0 || th->th_thread == 0)
		return (TD_ERR);
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
	ret = ps_pread(ta->ph, th->th_thread + ta->thread_off_tcb,
		&tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));

	/* get dtv array address */
	ret = ps_pread(ta->ph, tcb_addr + ta->thread_off_dtv,
		&dtv_addr, sizeof(dtv_addr));
	if (ret != 0)
		return (P2T(ret));
	/* now get the object's tls block base address */
	ret = ps_pread(ta->ph, dtv_addr + sizeof(void *) * (tls_index+1),
	    address, sizeof(*address));
	if (ret != 0)
		return (P2T(ret));

	*address += offset;
	return (TD_OK);
}

static struct ta_ops libthr_db_ops = {
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

DATA_SET(__ta_ops, libthr_db_ops);
