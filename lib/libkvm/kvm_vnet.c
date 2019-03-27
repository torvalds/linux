/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Robert N. M. Watson
 * Copyright (c) 2009 Bjoern A. Zeeb <bz@FreeBSD.org>
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

#define	_WANT_PRISON
#define	_WANT_UCRED
#define	_WANT_VNET

#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/_task.h>
#include <sys/jail.h>
#include <sys/proc.h>
#include <sys/types.h>

#include <net/vnet.h>

#include <kvm.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include "kvm_private.h"

/*
 * Set up libkvm to handle virtual network stack symbols by selecting a
 * starting pid.
 */
int
_kvm_vnet_selectpid(kvm_t *kd, pid_t pid)
{
	struct proc proc;
	struct ucred cred;
	struct prison prison;
	struct vnet vnet;
	struct kvm_nlist nl[] = {
		/*
		 * Note: kvm_nlist strips the first '_' so add an extra one
		 * here to __{start,stop}_set_vnet.
		 */
#define	NLIST_START_VNET	0
		{ .n_name = "___start_" VNET_SETNAME },
#define	NLIST_STOP_VNET		1
		{ .n_name = "___stop_" VNET_SETNAME },
#define	NLIST_VNET_HEAD		2
		{ .n_name = "vnet_head" },
#define	NLIST_ALLPROC		3
		{ .n_name = "allproc" },
#define	NLIST_DUMPTID		4
		{ .n_name = "dumptid" },
#define	NLIST_PROC0		5
		{ .n_name = "proc0" },
		{ .n_name = NULL },
	};
	uintptr_t procp, credp;
#define	VMCORE_VNET_OF_PROC0
#ifndef VMCORE_VNET_OF_PROC0
	struct thread td;
	uintptr_t tdp;
#endif
	lwpid_t dumptid;

	/*
	 * XXX: This only works for native kernels for now.
	 */
	if (!kvm_native(kd))
		return (-1);

	/*
	 * Locate and cache locations of important symbols
	 * using the internal version of _kvm_nlist, turning
	 * off initialization to avoid recursion in case of
	 * unresolveable symbols.
	 */
	if (_kvm_nlist(kd, nl, 0) != 0) {
		/*
		 * XXX-BZ: ___start_/___stop_VNET_SETNAME may fail.
		 * For now do not report an error here as we are called
		 * internally and in `void context' until we merge the
		 * functionality to optionally activate this into programs.
		 * By that time we can properly fail and let the callers
		 * handle the error.
		 */
		/* _kvm_err(kd, kd->program, "%s: no namelist", __func__); */
		return (-1);
	}

	/*
	 * Auto-detect if this is a crashdump by reading dumptid.
	 */
	dumptid = 0;
	if (nl[NLIST_DUMPTID].n_value) {
		if (kvm_read(kd, nl[NLIST_DUMPTID].n_value, &dumptid,
		    sizeof(dumptid)) != sizeof(dumptid)) {
			_kvm_err(kd, kd->program, "%s: dumptid", __func__);
			return (-1);
		}
	}

	/*
	 * First, find the process for this pid.  If we are working on a
	 * dump, either locate the thread dumptid is referring to or proc0.
	 * Based on either, take the address of the ucred.
	 */
	credp = 0;

	procp = nl[NLIST_ALLPROC].n_value;
#ifdef VMCORE_VNET_OF_PROC0
	if (dumptid > 0) {
		procp = nl[NLIST_PROC0].n_value;
		pid = 0;
	}
#endif
	while (procp != 0) {
		if (kvm_read(kd, procp, &proc, sizeof(proc)) != sizeof(proc)) {
			_kvm_err(kd, kd->program, "%s: proc", __func__);
			return (-1);
		}
#ifndef VMCORE_VNET_OF_PROC0
		if (dumptid > 0) {
			tdp = (uintptr_t)TAILQ_FIRST(&proc.p_threads);
			while (tdp != 0) {
				if (kvm_read(kd, tdp, &td, sizeof(td)) !=
				    sizeof(td)) {
					_kvm_err(kd, kd->program, "%s: thread",
					    __func__);
					return (-1);
				}
				if (td.td_tid == dumptid) {
					credp = (uintptr_t)td.td_ucred;
					break;
				}
				tdp = (uintptr_t)TAILQ_NEXT(&td, td_plist);
			}
		} else
#endif
		if (proc.p_pid == pid)
			credp = (uintptr_t)proc.p_ucred;
		if (credp != 0)
			break;
		procp = (uintptr_t)LIST_NEXT(&proc, p_list);
	}
	if (credp == 0) {
		_kvm_err(kd, kd->program, "%s: pid/tid not found", __func__);
		return (-1);
	}
	if (kvm_read(kd, (uintptr_t)credp, &cred, sizeof(cred)) !=
	    sizeof(cred)) {
		_kvm_err(kd, kd->program, "%s: cred", __func__);
		return (-1);
	}
	if (cred.cr_prison == NULL) {
		_kvm_err(kd, kd->program, "%s: no jail", __func__);
		return (-1);
	}
	if (kvm_read(kd, (uintptr_t)cred.cr_prison, &prison, sizeof(prison)) !=
	    sizeof(prison)) {
		_kvm_err(kd, kd->program, "%s: prison", __func__);
		return (-1);
	}
	if (prison.pr_vnet == NULL) {
		_kvm_err(kd, kd->program, "%s: no vnet", __func__);
		return (-1);
	}
	if (kvm_read(kd, (uintptr_t)prison.pr_vnet, &vnet, sizeof(vnet)) !=
	    sizeof(vnet)) {
		_kvm_err(kd, kd->program, "%s: vnet", __func__);
		return (-1);
	}
	if (vnet.vnet_magic_n != VNET_MAGIC_N) {
		_kvm_err(kd, kd->program, "%s: invalid vnet magic#", __func__);
		return (-1);
	}
	kd->vnet_initialized = 1;
	kd->vnet_start = nl[NLIST_START_VNET].n_value;
	kd->vnet_stop = nl[NLIST_STOP_VNET].n_value;
	kd->vnet_current = (uintptr_t)prison.pr_vnet;
	kd->vnet_base = vnet.vnet_data_base;
	return (0);
}

/*
 * Check whether the vnet module has been initialized successfully
 * or not, initialize it if permitted.
 */
int
_kvm_vnet_initialized(kvm_t *kd, int intialize)
{

	if (kd->vnet_initialized || !intialize)
		return (kd->vnet_initialized);

	(void) _kvm_vnet_selectpid(kd, getpid());

	return (kd->vnet_initialized);
}

/*
 * Check whether the value is within the vnet symbol range and
 * only if so adjust the offset relative to the current base.
 */
kvaddr_t
_kvm_vnet_validaddr(kvm_t *kd, kvaddr_t value)
{

	if (value == 0)
		return (value);

	if (!kd->vnet_initialized)
		return (value);

	if (value < kd->vnet_start || value >= kd->vnet_stop)
		return (value);

	return (kd->vnet_base + value);
}
