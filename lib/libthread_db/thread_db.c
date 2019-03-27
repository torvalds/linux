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

#include <proc_service.h>
#include <stddef.h>
#include <thread_db.h>
#include <unistd.h>
#include <sys/cdefs.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/linker_set.h>

#include "thread_db_int.h"

struct td_thragent 
{
	TD_THRAGENT_FIELDS;
};

static TAILQ_HEAD(, td_thragent) proclist = TAILQ_HEAD_INITIALIZER(proclist);

SET_DECLARE(__ta_ops, struct ta_ops);

td_err_e
td_init(void)
{
	td_err_e ret, tmp;
	struct ta_ops *ops_p, **ops_pp;

	ret = 0;
	SET_FOREACH(ops_pp, __ta_ops) {
		ops_p = *ops_pp;
		if (ops_p->to_init != NULL) {
			tmp = ops_p->to_init();
			if (tmp != TD_OK)
				ret = tmp;
		}
	}
	return (ret);
}

td_err_e
td_ta_clear_event(const td_thragent_t *ta, td_thr_events_t *events)
{
	return (ta->ta_ops->to_ta_clear_event(ta, events));
}

td_err_e
td_ta_delete(td_thragent_t *ta)
{
	TAILQ_REMOVE(&proclist, ta, ta_next);
	return (ta->ta_ops->to_ta_delete(ta));
}

td_err_e
td_ta_event_addr(const td_thragent_t *ta, td_event_e event, td_notify_t *ptr)
{
	return (ta->ta_ops->to_ta_event_addr(ta, event, ptr));
}

td_err_e
td_ta_event_getmsg(const td_thragent_t *ta, td_event_msg_t *msg)
{
	return (ta->ta_ops->to_ta_event_getmsg(ta, msg));
}

td_err_e
td_ta_map_id2thr(const td_thragent_t *ta, thread_t id, td_thrhandle_t *th)
{
	return (ta->ta_ops->to_ta_map_id2thr(ta, id, th));
}

td_err_e
td_ta_map_lwp2thr(const td_thragent_t *ta, lwpid_t lwpid, td_thrhandle_t *th)
{
	return (ta->ta_ops->to_ta_map_lwp2thr(ta, lwpid, th));
}

td_err_e
td_ta_new(struct ps_prochandle *ph, td_thragent_t **pta)
{
	struct ta_ops *ops_p, **ops_pp;

	SET_FOREACH(ops_pp, __ta_ops) {
		ops_p = *ops_pp;
		if (ops_p->to_ta_new(ph, pta) == TD_OK) {
			TAILQ_INSERT_HEAD(&proclist, *pta, ta_next);
			(*pta)->ta_ops = ops_p;
			return (TD_OK);
		}
	}
	return (TD_NOLIBTHREAD);
}

td_err_e
td_ta_set_event(const td_thragent_t *ta, td_thr_events_t *events)
{
	return (ta->ta_ops->to_ta_set_event(ta, events));
}

td_err_e
td_ta_thr_iter(const td_thragent_t *ta, td_thr_iter_f *callback,
    void *cbdata_p, td_thr_state_e state, int ti_pri, sigset_t *ti_sigmask_p,
    unsigned int ti_user_flags)
{
	return (ta->ta_ops->to_ta_thr_iter(ta, callback, cbdata_p, state,
		    ti_pri, ti_sigmask_p, ti_user_flags));
}

td_err_e
td_ta_tsd_iter(const td_thragent_t *ta, td_key_iter_f *callback,
    void *cbdata_p)
{
	return (ta->ta_ops->to_ta_tsd_iter(ta, callback, cbdata_p));
}

td_err_e
td_thr_clear_event(const td_thrhandle_t *th, td_thr_events_t *events)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_clear_event(th, events));
}

td_err_e
td_thr_dbresume(const td_thrhandle_t *th)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_dbresume(th));
}

td_err_e
td_thr_dbsuspend(const td_thrhandle_t *th)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_dbsuspend(th));
}

td_err_e
td_thr_event_enable(const td_thrhandle_t *th, int en)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_event_enable(th, en));
}

td_err_e
td_thr_event_getmsg(const td_thrhandle_t *th, td_event_msg_t *msg)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_event_getmsg(th, msg));
}

td_err_e
td_thr_old_get_info(const td_thrhandle_t *th, td_old_thrinfo_t *info)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_old_get_info(th, info));
}
__sym_compat(td_thr_get_info, td_thr_old_get_info, FBSD_1.0);

td_err_e
td_thr_get_info(const td_thrhandle_t *th, td_thrinfo_t *info)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_get_info(th, info));
}

#ifdef __i386__
td_err_e
td_thr_getxmmregs(const td_thrhandle_t *th, char *fxsave)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_getxmmregs(th, fxsave));
}
#endif


td_err_e
td_thr_getfpregs(const td_thrhandle_t *th, prfpregset_t *fpregset)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_getfpregs(th, fpregset));
}

td_err_e
td_thr_getgregs(const td_thrhandle_t *th, prgregset_t gregs)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_getgregs(th, gregs));
}

td_err_e
td_thr_set_event(const td_thrhandle_t *th, td_thr_events_t *events)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_set_event(th, events));
}

#ifdef __i386__
td_err_e
td_thr_setxmmregs(const td_thrhandle_t *th, const char *fxsave)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_setxmmregs(th, fxsave));
}
#endif

td_err_e
td_thr_setfpregs(const td_thrhandle_t *th, const prfpregset_t *fpregs)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_setfpregs(th, fpregs));
}

td_err_e
td_thr_setgregs(const td_thrhandle_t *th, const prgregset_t gregs)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_setgregs(th, gregs));
}

td_err_e
td_thr_validate(const td_thrhandle_t *th)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_validate(th));
}

td_err_e
td_thr_tls_get_addr(const td_thrhandle_t *th, psaddr_t linkmap, size_t offset,
    psaddr_t *address)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_tls_get_addr(th, linkmap, offset, address));
}

/* FreeBSD specific extensions. */

td_err_e
td_thr_sstep(const td_thrhandle_t *th, int step)
{
	const td_thragent_t *ta = th->th_ta;
	return (ta->ta_ops->to_thr_sstep(th, step));
}

/*
 * Support functions for reading from and writing to the target
 * address space.
 */

static int
thr_pread(struct ps_prochandle *ph, psaddr_t addr, uint64_t *val,
    u_int size, u_int byteorder)
{
	uint8_t buf[sizeof(*val)];
	ps_err_e err;

	if (size > sizeof(buf))
		return (EOVERFLOW);

	err = ps_pread(ph, addr, buf, size);
	if (err != PS_OK)
		return (EFAULT);

	switch (byteorder) {
	case BIG_ENDIAN:
		switch (size) {
		case 1:
			*val = buf[0];
			break;
		case 2:
			*val = be16dec(buf);
			break;
		case 4:
			*val = be32dec(buf);
			break;
		case 8:
			*val = be64dec(buf);
			break;
		default:
			return (EINVAL);
		}
		break;
	case LITTLE_ENDIAN:
		switch (size) {
		case 1:
			*val = buf[0];
			break;
		case 2:
			*val = le16dec(buf);
			break;
		case 4:
			*val = le32dec(buf);
			break;
		case 8:
			*val = le64dec(buf);
			break;
		default:
			return (EINVAL);
		}
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

int
thr_pread_int(const struct td_thragent *ta, psaddr_t addr, uint32_t *val)
{
	uint64_t tmp;
	int error;

	error = thr_pread(ta->ph, addr, &tmp, sizeof(int), BYTE_ORDER);
	if (!error)
		*val = tmp;

	return (error);
}

int
thr_pread_long(const struct td_thragent *ta, psaddr_t addr, uint64_t *val)
{

	return (thr_pread(ta->ph, addr, val, sizeof(long), BYTE_ORDER));
}

int
thr_pread_ptr(const struct td_thragent *ta, psaddr_t addr, psaddr_t *val)
{
	uint64_t tmp;
	int error;

	error = thr_pread(ta->ph, addr, &tmp, sizeof(void *), BYTE_ORDER);
	if (!error)
		*val = tmp;

	return (error);
}

static int
thr_pwrite(struct ps_prochandle *ph, psaddr_t addr, uint64_t val,
    u_int size, u_int byteorder)
{
	uint8_t buf[sizeof(val)];
	ps_err_e err;

	if (size > sizeof(buf))
		return (EOVERFLOW);

	switch (byteorder) {
	case BIG_ENDIAN:
		switch (size) {
		case 1:
			buf[0] = (uint8_t)val;
			break;
		case 2:
			be16enc(buf, (uint16_t)val);
			break;
		case 4:
			be32enc(buf, (uint32_t)val);
			break;
		case 8:
			be64enc(buf, (uint64_t)val);
			break;
		default:
			return (EINVAL);
		}
		break;
	case LITTLE_ENDIAN:
		switch (size) {
		case 1:
			buf[0] = (uint8_t)val;
			break;
		case 2:
			le16enc(buf, (uint16_t)val);
			break;
		case 4:
			le32enc(buf, (uint32_t)val);
			break;
		case 8:
			le64enc(buf, (uint64_t)val);
			break;
		default:
			return (EINVAL);
		}
		break;
	default:
		return (EINVAL);
	}

	err = ps_pwrite(ph, addr, buf, size);
	return ((err != PS_OK) ? EFAULT : 0);
}

int
thr_pwrite_int(const struct td_thragent *ta, psaddr_t addr, uint32_t val)
{

	return (thr_pwrite(ta->ph, addr, val, sizeof(int), BYTE_ORDER));
}

int
thr_pwrite_long(const struct td_thragent *ta, psaddr_t addr, uint64_t val)
{

	return (thr_pwrite(ta->ph, addr, val, sizeof(long), BYTE_ORDER));
}

int
thr_pwrite_ptr(const struct td_thragent *ta, psaddr_t addr, psaddr_t val)
{

	return (thr_pwrite(ta->ph, addr, val, sizeof(void *), BYTE_ORDER));
}

