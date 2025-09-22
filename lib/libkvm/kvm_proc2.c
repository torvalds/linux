/*	$OpenBSD: kvm_proc2.c,v 1.39 2024/07/08 13:18:26 claudio Exp $	*/
/*	$NetBSD: kvm_proc.c,v 1.30 1999/03/24 05:50:50 mrg Exp $	*/
/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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
/*-
 * Copyright (c) 1994, 1995 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1989, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software developed by the Computer Systems
 * Engineering group at Lawrence Berkeley Laboratory under DARPA contract
 * BG 91-66 and contributed to Berkeley.
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
 */

/*
 * Proc traversal interface for kvm.  ps and w are (probably) the exclusive
 * users of this code, so we've factored it out into a separate module.
 * Thus, we keep this grunge out of the other kvm applications (i.e.,
 * most other applications are interested only in open/close/read/nlist).
 */

#define __need_process
#include <sys/param.h>	/* NODEV */
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/exec.h>
#include <sys/stat.h>
#include <sys/ucred.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/pledge.h>
#include <sys/wait.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <nlist.h>
#include <kvm.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_amap.h>
#include <machine/vmparam.h>
#include <machine/pmap.h>

#include <sys/sysctl.h>

#include <limits.h>
#include <errno.h>
#include <db.h>
#include <paths.h>

#include "kvm_private.h"

/*
 * Read proc's from memory file into buffer bp, which has space to hold
 * at most maxcnt procs.
 */
static int
kvm_proclist(kvm_t *kd, int op, int arg, struct process *pr,
    char *bp, int maxcnt, size_t esize)
{
	struct kinfo_proc kp;
	struct session sess;
	struct ucred ucred;
	struct proc proc, *p;
	struct process process, process2;
	struct pgrp pgrp;
	struct tty tty;
	struct timeval elapsed, monostart, monostop, realstart, realstop;
	struct nlist nl[3];
	struct sigacts sa, *sap;
	struct vmspace vm, *vmp;
	struct plimit limits, *limp;
	pid_t parent_pid, leader_pid;
	int cnt = 0;
	int dothreads = 0;
	int i;

	dothreads = op & KERN_PROC_SHOW_THREADS;
	op &= ~KERN_PROC_SHOW_THREADS;

	/* Anchor a time to compare process starting times from. */
	nl[0].n_name = "_time_second";
	nl[1].n_name = "_time_uptime";
	nl[2].n_name = NULL;
	if (kvm_nlist(kd, nl) != 0) {
		for (i = 0; nl[i].n_type != 0; ++i)
			continue;
		_kvm_err(kd, kd->program, "%s: no such symbol", nl[i].n_name);
		return (-1);
	}
	timerclear(&realstop);
	timerclear(&monostop);
	if (KREAD(kd, nl[0].n_value, &realstop.tv_sec)) {
		_kvm_err(kd, kd->program, "cannot read time_second");
		return (-1);
	}
	if (KREAD(kd, nl[1].n_value, &monostop.tv_sec)) {
		_kvm_err(kd, kd->program, "cannot read time_uptime");
		return (-1);
	}

	/*
	 * Modelled on sysctl_doproc() in sys/kern/kern_sysctl.c
	 */
	for (; cnt < maxcnt && pr != NULL; pr = LIST_NEXT(&process, ps_list)) {
		if (KREAD(kd, (u_long)pr, &process)) {
			_kvm_err(kd, kd->program, "can't read process at %lx",
			    (u_long)pr);
			return (-1);
		}
		if (process.ps_pgrp == NULL)
			continue;
		if (process.ps_flags & PS_EMBRYO)
			continue;
		if (KREAD(kd, (u_long)process.ps_ucred, &ucred)) {
			_kvm_err(kd, kd->program, "can't read ucred at %lx",
			    (u_long)process.ps_ucred);
			return (-1);
		}
		if (KREAD(kd, (u_long)process.ps_pgrp, &pgrp)) {
			_kvm_err(kd, kd->program, "can't read pgrp at %lx",
			    (u_long)process.ps_pgrp);
			return (-1);
		}
		if (KREAD(kd, (u_long)pgrp.pg_session, &sess)) {
			_kvm_err(kd, kd->program, "can't read session at %lx",
			    (u_long)pgrp.pg_session);
			return (-1);
		}
		if ((process.ps_flags & PS_CONTROLT) && sess.s_ttyp != NULL &&
		    KREAD(kd, (u_long)sess.s_ttyp, &tty)) {
			_kvm_err(kd, kd->program, "can't read tty at %lx",
			    (u_long)sess.s_ttyp);
			return (-1);
		}
		if (process.ps_pptr) {
			if (KREAD(kd, (u_long)process.ps_pptr, &process2)) {
				_kvm_err(kd, kd->program,
				    "can't read process at %lx",
				    (u_long)process.ps_pptr);
				return (-1);
			}
			parent_pid = process2.ps_pid;
		}
		else
			parent_pid = 0;
		if (sess.s_leader) {
			if (KREAD(kd, (u_long)sess.s_leader, &process2)) {
				_kvm_err(kd, kd->program,
				    "can't read proc at %lx",
				    (u_long)sess.s_leader);
				return (-1);
			}
			leader_pid = process2.ps_pid;
		}
		else
			leader_pid = 0;
		if (process.ps_sigacts) {
			if (KREAD(kd, (u_long)process.ps_sigacts, &sa)) {
				_kvm_err(kd, kd->program,
				    "can't read sigacts at %lx",
				    (u_long)process.ps_sigacts);
				return (-1);
			}
			sap = &sa;
		}
		else
			sap = NULL;

		switch (op) {
		case KERN_PROC_PID:
			if (process.ps_pid != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_PGRP:
			if (pgrp.pg_id != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_SESSION:
			if (sess.s_leader == NULL ||
			    leader_pid != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_TTY:
			if ((process.ps_flags & PS_CONTROLT) == 0 ||
			    sess.s_ttyp == NULL ||
			    tty.t_dev != (dev_t)arg)
				continue;
			break;

		case KERN_PROC_UID:
			if (ucred.cr_uid != (uid_t)arg)
				continue;
			break;

		case KERN_PROC_RUID:
			if (ucred.cr_ruid != (uid_t)arg)
				continue;
			break;

		case KERN_PROC_ALL:
			if (process.ps_flags & PS_SYSTEM)
				continue;
			break;

		case KERN_PROC_KTHREAD:
			/* no filtering */
			break;

		default:
			_kvm_err(kd, kd->program, "invalid filter");
			return (-1);
		}

		/*
		 * We're going to add another proc to the set.  If this
		 * will overflow the buffer, assume the reason is because
		 * nthreads (or the proc list) is corrupt and declare an error.
		 */
		if (cnt >= maxcnt) {
			_kvm_err(kd, kd->program, "nthreads corrupt");
			return (-1);
		}

		/* set up stuff that might not always be there */
		limp = &limits;
		if (!process.ps_limit ||
		    KREAD(kd, (u_long)process.ps_limit, &limits))
			limp = NULL;

		vmp = NULL;

		if ((process.ps_flags & PS_ZOMBIE) == 0 &&
		    !KREAD(kd, (u_long)process.ps_vmspace, &vm))
			vmp = &vm;

#define do_copy_str(_d, _s, _l)	kvm_read(kd, (u_long)(_s), (_d), (_l)-1)
		FILL_KPROC(&kp, do_copy_str, &proc, &process,
		    &ucred, &pgrp, process.ps_mainproc, pr, &sess,
		    vmp, limp, sap, &process.ps_tu, 0, 1);

		/* stuff that's too painful to generalize */
		kp.p_ppid = parent_pid;
		kp.p_sid = leader_pid;
		if ((process.ps_flags & PS_CONTROLT) && sess.s_ttyp != NULL) {
			kp.p_tdev = tty.t_dev;
			if (tty.t_pgrp != NULL &&
			    tty.t_pgrp != process.ps_pgrp &&
			    KREAD(kd, (u_long)tty.t_pgrp, &pgrp)) {
				_kvm_err(kd, kd->program,
				    "can't read tpgrp at %lx",
				    (u_long)tty.t_pgrp);
				return (-1);
			}
			kp.p_tpgid = tty.t_pgrp ? pgrp.pg_id : -1;
			kp.p_tsess = PTRTOINT64(tty.t_session);
		} else {
			kp.p_tpgid = -1;
			kp.p_tdev = NODEV;
		}

		/* Convert the starting uptime to a starting UTC time. */
		if ((process.ps_flags & PS_ZOMBIE) == 0) {
			monostart.tv_sec = kp.p_ustart_sec;
			monostart.tv_usec = kp.p_ustart_usec;
			timersub(&monostop, &monostart, &elapsed);
			if (elapsed.tv_sec < 0)
				timerclear(&elapsed);
			timersub(&realstop, &elapsed, &realstart);
			kp.p_ustart_sec = realstart.tv_sec;
			kp.p_ustart_usec = realstart.tv_usec;
		}

		/* update %cpu for all threads */
		if (dothreads) {
			if (KREAD(kd, (u_long)process.ps_mainproc, &proc)) {
				_kvm_err(kd, kd->program,
				    "can't read proc at %lx",
				    (u_long)process.ps_mainproc);
				return (-1);
			}
			kp.p_pctcpu = proc.p_pctcpu;
			kp.p_stat   = proc.p_stat;
		} else {
			kp.p_pctcpu = 0;
			kp.p_stat = (process.ps_flags & PS_ZOMBIE) ? SDEAD :
			    SIDL;
			for (p = TAILQ_FIRST(&process.ps_threads); p != NULL; 
			    p = TAILQ_NEXT(&proc, p_thr_link)) {
				if (KREAD(kd, (u_long)p, &proc)) {
					_kvm_err(kd, kd->program,
					    "can't read proc at %lx",
					    (u_long)p);
					return (-1);
				}
				kp.p_pctcpu += proc.p_pctcpu;
				/*
				 * find best state:
				 * ONPROC > RUN > STOP > SLEEP > ...
				 */
				if (proc.p_stat == SONPROC ||
				    kp.p_stat == SONPROC)
					kp.p_stat = SONPROC;
				else if (proc.p_stat == SRUN ||
				    kp.p_stat == SRUN)
					kp.p_stat = SRUN;
				else if (proc.p_stat == SSTOP ||
				    kp.p_stat == SSTOP)
					kp.p_stat = SSTOP;
				else if (proc.p_stat == SSLEEP)
					kp.p_stat = SSLEEP;
			}
                }

		memcpy(bp, &kp, esize);
		bp += esize;
		++cnt;

		/* Skip per-thread entries if not required by op */
		if (!dothreads)
			continue;

		for (p = TAILQ_FIRST(&process.ps_threads); p != NULL; 
		    p = TAILQ_NEXT(&proc, p_thr_link)) {
			if (KREAD(kd, (u_long)p, &proc)) {
				_kvm_err(kd, kd->program,
				    "can't read proc at %lx",
				    (u_long)p);
				return (-1);
			}
			FILL_KPROC(&kp, do_copy_str, &proc, &process,
			    &ucred, &pgrp, p, pr, &sess, vmp, limp, sap,
			    &proc.p_tu, 1, 1);

			/* see above */
			kp.p_ppid = parent_pid;
			kp.p_sid = leader_pid;
			if ((process.ps_flags & PS_CONTROLT) &&
			    sess.s_ttyp != NULL) {
				kp.p_tdev = tty.t_dev;
				if (tty.t_pgrp != NULL &&
				    tty.t_pgrp != process.ps_pgrp &&
				    KREAD(kd, (u_long)tty.t_pgrp, &pgrp)) {
					_kvm_err(kd, kd->program,
					    "can't read tpgrp at %lx",
					    (u_long)tty.t_pgrp);
					return (-1);
				}
				kp.p_tpgid = tty.t_pgrp ? pgrp.pg_id : -1;
				kp.p_tsess = PTRTOINT64(tty.t_session);
			} else {
				kp.p_tpgid = -1;
				kp.p_tdev = NODEV;
			}
		}

		memcpy(bp, &kp, esize);
		bp += esize;
		++cnt;
#undef do_copy_str
	}
	return (cnt);
}

struct kinfo_proc *
kvm_getprocs(kvm_t *kd, int op, int arg, size_t esize, int *cnt)
{
	int mib[6], st, nthreads;
	void *procbase;
	size_t size;

	if ((ssize_t)esize < 0)
		return (NULL);

	if (ISALIVE(kd)) {
		size = 0;
		mib[0] = CTL_KERN;
		mib[1] = KERN_PROC;
		mib[2] = op;
		mib[3] = arg;
		mib[4] = esize;

		do {
			mib[5] = 0;
			st = sysctl(mib, 6, NULL, &size, NULL, 0);
			if (st == -1) {
				_kvm_syserr(kd, kd->program, "kvm_getprocs");
				return (NULL);
			}

			size += size / 8; /* add ~10% */

			procbase = _kvm_realloc(kd, kd->procbase, size);
			if (procbase == NULL)
				return (NULL);

			kd->procbase = procbase;

			mib[5] = size / esize;
			st = sysctl(mib, 6, kd->procbase, &size, NULL, 0);
			if (st == -1 && errno != ENOMEM) {
				_kvm_syserr(kd, kd->program, "kvm_getprocs");
				return (NULL);
			}
		} while (st == -1);

		nthreads = size / esize;
	} else {
		struct nlist nl[5];
		int i, maxthread, maxprocess;
		struct process *pr;
		char *bp;

		if (esize > sizeof(struct kinfo_proc)) {
			_kvm_syserr(kd, kd->program,
			    "kvm_getprocs: unknown fields requested: libkvm out of date?");
			return (NULL);
		}

		memset(nl, 0, sizeof(nl));
		nl[0].n_name = "_nthreads";
		nl[1].n_name = "_nprocesses";
		nl[2].n_name = "_allprocess";
		nl[3].n_name = "_zombprocess";
		nl[4].n_name = NULL;

		if (kvm_nlist(kd, nl) != 0) {
			for (i = 0; nl[i].n_type != 0; ++i)
				;
			_kvm_err(kd, kd->program,
			    "%s: no such symbol", nl[i].n_name);
			return (NULL);
		}
		if (KREAD(kd, nl[0].n_value, &maxthread)) {
			_kvm_err(kd, kd->program, "can't read nthreads");
			return (NULL);
		}
		if (KREAD(kd, nl[1].n_value, &maxprocess)) {
			_kvm_err(kd, kd->program, "can't read nprocesses");
			return (NULL);
		}
		maxthread += maxprocess;

		kd->procbase = _kvm_reallocarray(kd, NULL, maxthread, esize);
		if (kd->procbase == 0)
			return (NULL);
		bp = (char *)kd->procbase;

		/* allprocess */
		if (KREAD(kd, nl[2].n_value, &pr)) {
			_kvm_err(kd, kd->program, "cannot read allprocess");
			return (NULL);
		}
		nthreads = kvm_proclist(kd, op, arg, pr, bp, maxthread, esize);
		if (nthreads < 0)
			return (NULL);

		/* zombprocess */
		if (KREAD(kd, nl[3].n_value, &pr)) {
			_kvm_err(kd, kd->program, "cannot read zombprocess");
			return (NULL);
		}
		i = kvm_proclist(kd, op, arg, pr, bp + (esize * nthreads),
		    maxthread - nthreads, esize);
		if (i > 0)
			nthreads += i;
	}
	if (kd->procbase != NULL)
		*cnt = nthreads;
	return (kd->procbase);
}
