/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
__SCCSID("@(#)kvm_proc.c	8.3 (Berkeley) 9/23/93");

/*
 * Proc traversal interface for kvm.  ps and w are (probably) the exclusive
 * users of this code, so we've factored it out into a separate module.
 * Thus, we keep this grunge out of the other kvm applications (i.e.,
 * most other applications are interested only in open/close/read/nlist).
 */

#include <sys/param.h>
#define	_WANT_UCRED	/* make ucred.h give us 'struct ucred' */
#include <sys/ucred.h>
#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/_task.h>
#include <sys/cpuset.h>
#include <sys/user.h>
#include <sys/proc.h>
#define	_WANT_PRISON	/* make jail.h give us 'struct prison' */
#include <sys/jail.h>
#include <sys/exec.h>
#include <sys/stat.h>
#include <sys/sysent.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <sys/conf.h>
#define	_WANT_KW_EXITCODE
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <nlist.h>
#include <kvm.h>

#include <sys/sysctl.h>

#include <limits.h>
#include <memory.h>
#include <paths.h>

#include "kvm_private.h"

#define KREAD(kd, addr, obj) \
	(kvm_read(kd, addr, (char *)(obj), sizeof(*obj)) != sizeof(*obj))

static int ticks;
static int hz;
static uint64_t cpu_tick_frequency;

/*
 * From sys/kern/kern_tc.c. Depends on cpu_tick_frequency, which is
 * read/initialized before this function is ever called.
 */
static uint64_t
cputick2usec(uint64_t tick)
{

	if (cpu_tick_frequency == 0)
		return (0);
	if (tick > 18446744073709551)		/* floor(2^64 / 1000) */
		return (tick / (cpu_tick_frequency / 1000000));
	else if (tick > 18446744073709)	/* floor(2^64 / 1000000) */
		return ((tick * 1000) / (cpu_tick_frequency / 1000));
	else
		return ((tick * 1000000) / cpu_tick_frequency);
}

/*
 * Read proc's from memory file into buffer bp, which has space to hold
 * at most maxcnt procs.
 */
static int
kvm_proclist(kvm_t *kd, int what, int arg, struct proc *p,
    struct kinfo_proc *bp, int maxcnt)
{
	int cnt = 0;
	struct kinfo_proc kinfo_proc, *kp;
	struct pgrp pgrp;
	struct session sess;
	struct cdev t_cdev;
	struct tty tty;
	struct vmspace vmspace;
	struct sigacts sigacts;
#if 0
	struct pstats pstats;
#endif
	struct ucred ucred;
	struct prison pr;
	struct thread mtd;
	struct proc proc;
	struct proc pproc;
	struct sysentvec sysent;
	char svname[KI_EMULNAMELEN];

	kp = &kinfo_proc;
	kp->ki_structsize = sizeof(kinfo_proc);
	/*
	 * Loop on the processes. this is completely broken because we need to be
	 * able to loop on the threads and merge the ones that are the same process some how.
	 */
	for (; cnt < maxcnt && p != NULL; p = LIST_NEXT(&proc, p_list)) {
		memset(kp, 0, sizeof *kp);
		if (KREAD(kd, (u_long)p, &proc)) {
			_kvm_err(kd, kd->program, "can't read proc at %p", p);
			return (-1);
		}
		if (proc.p_state == PRS_NEW)
			continue;
		if (proc.p_state != PRS_ZOMBIE) {
			if (KREAD(kd, (u_long)TAILQ_FIRST(&proc.p_threads),
			    &mtd)) {
				_kvm_err(kd, kd->program,
				    "can't read thread at %p",
				    TAILQ_FIRST(&proc.p_threads));
				return (-1);
			}
		}
		if (KREAD(kd, (u_long)proc.p_ucred, &ucred) == 0) {
			kp->ki_ruid = ucred.cr_ruid;
			kp->ki_svuid = ucred.cr_svuid;
			kp->ki_rgid = ucred.cr_rgid;
			kp->ki_svgid = ucred.cr_svgid;
			kp->ki_cr_flags = ucred.cr_flags;
			if (ucred.cr_ngroups > KI_NGROUPS) {
				kp->ki_ngroups = KI_NGROUPS;
				kp->ki_cr_flags |= KI_CRF_GRP_OVERFLOW;
			} else
				kp->ki_ngroups = ucred.cr_ngroups;
			kvm_read(kd, (u_long)ucred.cr_groups, kp->ki_groups,
			    kp->ki_ngroups * sizeof(gid_t));
			kp->ki_uid = ucred.cr_uid;
			if (ucred.cr_prison != NULL) {
				if (KREAD(kd, (u_long)ucred.cr_prison, &pr)) {
					_kvm_err(kd, kd->program,
					    "can't read prison at %p",
					    ucred.cr_prison);
					return (-1);
				}
				kp->ki_jid = pr.pr_id;
			}
		}

		switch(what & ~KERN_PROC_INC_THREAD) {

		case KERN_PROC_GID:
			if (kp->ki_groups[0] != (gid_t)arg)
				continue;
			break;

		case KERN_PROC_PID:
			if (proc.p_pid != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_RGID:
			if (kp->ki_rgid != (gid_t)arg)
				continue;
			break;

		case KERN_PROC_UID:
			if (kp->ki_uid != (uid_t)arg)
				continue;
			break;

		case KERN_PROC_RUID:
			if (kp->ki_ruid != (uid_t)arg)
				continue;
			break;
		}
		/*
		 * We're going to add another proc to the set.  If this
		 * will overflow the buffer, assume the reason is because
		 * nprocs (or the proc list) is corrupt and declare an error.
		 */
		if (cnt >= maxcnt) {
			_kvm_err(kd, kd->program, "nprocs corrupt");
			return (-1);
		}
		/*
		 * gather kinfo_proc
		 */
		kp->ki_paddr = p;
		kp->ki_addr = 0;	/* XXX uarea */
		/* kp->ki_kstack = proc.p_thread.td_kstack; XXXKSE */
		kp->ki_args = proc.p_args;
		kp->ki_tracep = proc.p_tracevp;
		kp->ki_textvp = proc.p_textvp;
		kp->ki_fd = proc.p_fd;
		kp->ki_vmspace = proc.p_vmspace;
		if (proc.p_sigacts != NULL) {
			if (KREAD(kd, (u_long)proc.p_sigacts, &sigacts)) {
				_kvm_err(kd, kd->program,
				    "can't read sigacts at %p", proc.p_sigacts);
				return (-1);
			}
			kp->ki_sigignore = sigacts.ps_sigignore;
			kp->ki_sigcatch = sigacts.ps_sigcatch;
		}
#if 0
		if ((proc.p_flag & P_INMEM) && proc.p_stats != NULL) {
			if (KREAD(kd, (u_long)proc.p_stats, &pstats)) {
				_kvm_err(kd, kd->program,
				    "can't read stats at %x", proc.p_stats);
				return (-1);
			}
			kp->ki_start = pstats.p_start;

			/*
			 * XXX: The times here are probably zero and need
			 * to be calculated from the raw data in p_rux and
			 * p_crux.
			 */
			kp->ki_rusage = pstats.p_ru;
			kp->ki_childstime = pstats.p_cru.ru_stime;
			kp->ki_childutime = pstats.p_cru.ru_utime;
			/* Some callers want child-times in a single value */
			timeradd(&kp->ki_childstime, &kp->ki_childutime,
			    &kp->ki_childtime);
		}
#endif
		if (proc.p_oppid)
			kp->ki_ppid = proc.p_oppid;
		else if (proc.p_pptr) {
			if (KREAD(kd, (u_long)proc.p_pptr, &pproc)) {
				_kvm_err(kd, kd->program,
				    "can't read pproc at %p", proc.p_pptr);
				return (-1);
			}
			kp->ki_ppid = pproc.p_pid;
		} else
			kp->ki_ppid = 0;
		if (proc.p_pgrp == NULL)
			goto nopgrp;
		if (KREAD(kd, (u_long)proc.p_pgrp, &pgrp)) {
			_kvm_err(kd, kd->program, "can't read pgrp at %p",
				 proc.p_pgrp);
			return (-1);
		}
		kp->ki_pgid = pgrp.pg_id;
		kp->ki_jobc = pgrp.pg_jobc;
		if (KREAD(kd, (u_long)pgrp.pg_session, &sess)) {
			_kvm_err(kd, kd->program, "can't read session at %p",
				pgrp.pg_session);
			return (-1);
		}
		kp->ki_sid = sess.s_sid;
		(void)memcpy(kp->ki_login, sess.s_login,
						sizeof(kp->ki_login));
		kp->ki_kiflag = sess.s_ttyvp ? KI_CTTY : 0;
		if (sess.s_leader == p)
			kp->ki_kiflag |= KI_SLEADER;
		if ((proc.p_flag & P_CONTROLT) && sess.s_ttyp != NULL) {
			if (KREAD(kd, (u_long)sess.s_ttyp, &tty)) {
				_kvm_err(kd, kd->program,
					 "can't read tty at %p", sess.s_ttyp);
				return (-1);
			}
			if (tty.t_dev != NULL) {
				if (KREAD(kd, (u_long)tty.t_dev, &t_cdev)) {
					_kvm_err(kd, kd->program,
						 "can't read cdev at %p",
						tty.t_dev);
					return (-1);
				}
#if 0
				kp->ki_tdev = t_cdev.si_udev;
#else
				kp->ki_tdev = NODEV;
#endif
			}
			if (tty.t_pgrp != NULL) {
				if (KREAD(kd, (u_long)tty.t_pgrp, &pgrp)) {
					_kvm_err(kd, kd->program,
						 "can't read tpgrp at %p",
						tty.t_pgrp);
					return (-1);
				}
				kp->ki_tpgid = pgrp.pg_id;
			} else
				kp->ki_tpgid = -1;
			if (tty.t_session != NULL) {
				if (KREAD(kd, (u_long)tty.t_session, &sess)) {
					_kvm_err(kd, kd->program,
					    "can't read session at %p",
					    tty.t_session);
					return (-1);
				}
				kp->ki_tsid = sess.s_sid;
			}
		} else {
nopgrp:
			kp->ki_tdev = NODEV;
		}
		if ((proc.p_state != PRS_ZOMBIE) && mtd.td_wmesg)
			(void)kvm_read(kd, (u_long)mtd.td_wmesg,
			    kp->ki_wmesg, WMESGLEN);

		(void)kvm_read(kd, (u_long)proc.p_vmspace,
		    (char *)&vmspace, sizeof(vmspace));
		kp->ki_size = vmspace.vm_map.size;
		/*
		 * Approximate the kernel's method of calculating
		 * this field.
		 */
#define		pmap_resident_count(pm) ((pm)->pm_stats.resident_count)
		kp->ki_rssize = pmap_resident_count(&vmspace.vm_pmap);
		kp->ki_swrss = vmspace.vm_swrss;
		kp->ki_tsize = vmspace.vm_tsize;
		kp->ki_dsize = vmspace.vm_dsize;
		kp->ki_ssize = vmspace.vm_ssize;

		switch (what & ~KERN_PROC_INC_THREAD) {

		case KERN_PROC_PGRP:
			if (kp->ki_pgid != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_SESSION:
			if (kp->ki_sid != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_TTY:
			if ((proc.p_flag & P_CONTROLT) == 0 ||
			     kp->ki_tdev != (dev_t)arg)
				continue;
			break;
		}
		if (proc.p_comm[0] != 0)
			strlcpy(kp->ki_comm, proc.p_comm, MAXCOMLEN);
		(void)kvm_read(kd, (u_long)proc.p_sysent, (char *)&sysent,
		    sizeof(sysent));
		(void)kvm_read(kd, (u_long)sysent.sv_name, (char *)&svname,
		    sizeof(svname));
		if (svname[0] != 0)
			strlcpy(kp->ki_emul, svname, KI_EMULNAMELEN);
		if ((proc.p_state != PRS_ZOMBIE) &&
		    (mtd.td_blocked != 0)) {
			kp->ki_kiflag |= KI_LOCKBLOCK;
			if (mtd.td_lockname)
				(void)kvm_read(kd,
				    (u_long)mtd.td_lockname,
				    kp->ki_lockname, LOCKNAMELEN);
			kp->ki_lockname[LOCKNAMELEN] = 0;
		}
		kp->ki_runtime = cputick2usec(proc.p_rux.rux_runtime);
		kp->ki_pid = proc.p_pid;
		kp->ki_siglist = proc.p_siglist;
		SIGSETOR(kp->ki_siglist, mtd.td_siglist);
		kp->ki_sigmask = mtd.td_sigmask;
		kp->ki_xstat = KW_EXITCODE(proc.p_xexit, proc.p_xsig);
		kp->ki_acflag = proc.p_acflag;
		kp->ki_lock = proc.p_lock;
		if (proc.p_state != PRS_ZOMBIE) {
			kp->ki_swtime = (ticks - proc.p_swtick) / hz;
			kp->ki_flag = proc.p_flag;
			kp->ki_sflag = 0;
			kp->ki_nice = proc.p_nice;
			kp->ki_traceflag = proc.p_traceflag;
			if (proc.p_state == PRS_NORMAL) {
				if (TD_ON_RUNQ(&mtd) ||
				    TD_CAN_RUN(&mtd) ||
				    TD_IS_RUNNING(&mtd)) {
					kp->ki_stat = SRUN;
				} else if (mtd.td_state ==
				    TDS_INHIBITED) {
					if (P_SHOULDSTOP(&proc)) {
						kp->ki_stat = SSTOP;
					} else if (
					    TD_IS_SLEEPING(&mtd)) {
						kp->ki_stat = SSLEEP;
					} else if (TD_ON_LOCK(&mtd)) {
						kp->ki_stat = SLOCK;
					} else {
						kp->ki_stat = SWAIT;
					}
				}
			} else {
				kp->ki_stat = SIDL;
			}
			/* Stuff from the thread */
			kp->ki_pri.pri_level = mtd.td_priority;
			kp->ki_pri.pri_native = mtd.td_base_pri;
			kp->ki_lastcpu = mtd.td_lastcpu;
			kp->ki_wchan = mtd.td_wchan;
			kp->ki_oncpu = mtd.td_oncpu;
			if (mtd.td_name[0] != '\0')
				strlcpy(kp->ki_tdname, mtd.td_name, sizeof(kp->ki_tdname));
			kp->ki_pctcpu = 0;
			kp->ki_rqindex = 0;

			/*
			 * Note: legacy fields; wraps at NO_CPU_OLD or the
			 * old max CPU value as appropriate
			 */
			if (mtd.td_lastcpu == NOCPU)
				kp->ki_lastcpu_old = NOCPU_OLD;
			else if (mtd.td_lastcpu > MAXCPU_OLD)
				kp->ki_lastcpu_old = MAXCPU_OLD;
			else
				kp->ki_lastcpu_old = mtd.td_lastcpu;

			if (mtd.td_oncpu == NOCPU)
				kp->ki_oncpu_old = NOCPU_OLD;
			else if (mtd.td_oncpu > MAXCPU_OLD)
				kp->ki_oncpu_old = MAXCPU_OLD;
			else
				kp->ki_oncpu_old = mtd.td_oncpu;
		} else {
			kp->ki_stat = SZOMB;
		}
		kp->ki_tdev_freebsd11 = kp->ki_tdev; /* truncate */
		bcopy(&kinfo_proc, bp, sizeof(kinfo_proc));
		++bp;
		++cnt;
	}
	return (cnt);
}

/*
 * Build proc info array by reading in proc list from a crash dump.
 * Return number of procs read.  maxcnt is the max we will read.
 */
static int
kvm_deadprocs(kvm_t *kd, int what, int arg, u_long a_allproc,
    u_long a_zombproc, int maxcnt)
{
	struct kinfo_proc *bp = kd->procbase;
	int acnt, zcnt;
	struct proc *p;

	if (KREAD(kd, a_allproc, &p)) {
		_kvm_err(kd, kd->program, "cannot read allproc");
		return (-1);
	}
	acnt = kvm_proclist(kd, what, arg, p, bp, maxcnt);
	if (acnt < 0)
		return (acnt);

	if (KREAD(kd, a_zombproc, &p)) {
		_kvm_err(kd, kd->program, "cannot read zombproc");
		return (-1);
	}
	zcnt = kvm_proclist(kd, what, arg, p, bp + acnt, maxcnt - acnt);
	if (zcnt < 0)
		zcnt = 0;

	return (acnt + zcnt);
}

struct kinfo_proc *
kvm_getprocs(kvm_t *kd, int op, int arg, int *cnt)
{
	int mib[4], st, nprocs;
	size_t size, osize;
	int temp_op;

	if (kd->procbase != 0) {
		free((void *)kd->procbase);
		/*
		 * Clear this pointer in case this call fails.  Otherwise,
		 * kvm_close() will free it again.
		 */
		kd->procbase = 0;
	}
	if (ISALIVE(kd)) {
		size = 0;
		mib[0] = CTL_KERN;
		mib[1] = KERN_PROC;
		mib[2] = op;
		mib[3] = arg;
		temp_op = op & ~KERN_PROC_INC_THREAD;
		st = sysctl(mib,
		    temp_op == KERN_PROC_ALL || temp_op == KERN_PROC_PROC ?
		    3 : 4, NULL, &size, NULL, 0);
		if (st == -1) {
			_kvm_syserr(kd, kd->program, "kvm_getprocs");
			return (0);
		}
		/*
		 * We can't continue with a size of 0 because we pass
		 * it to realloc() (via _kvm_realloc()), and passing 0
		 * to realloc() results in undefined behavior.
		 */
		if (size == 0) {
			/*
			 * XXX: We should probably return an invalid,
			 * but non-NULL, pointer here so any client
			 * program trying to dereference it will
			 * crash.  However, _kvm_freeprocs() calls
			 * free() on kd->procbase if it isn't NULL,
			 * and free()'ing a junk pointer isn't good.
			 * Then again, _kvm_freeprocs() isn't used
			 * anywhere . . .
			 */
			kd->procbase = _kvm_malloc(kd, 1);
			goto liveout;
		}
		do {
			size += size / 10;
			kd->procbase = (struct kinfo_proc *)
			    _kvm_realloc(kd, kd->procbase, size);
			if (kd->procbase == NULL)
				return (0);
			osize = size;
			st = sysctl(mib, temp_op == KERN_PROC_ALL ||
			    temp_op == KERN_PROC_PROC ? 3 : 4,
			    kd->procbase, &size, NULL, 0);
		} while (st == -1 && errno == ENOMEM && size == osize);
		if (st == -1) {
			_kvm_syserr(kd, kd->program, "kvm_getprocs");
			return (0);
		}
		/*
		 * We have to check the size again because sysctl()
		 * may "round up" oldlenp if oldp is NULL; hence it
		 * might've told us that there was data to get when
		 * there really isn't any.
		 */
		if (size > 0 &&
		    kd->procbase->ki_structsize != sizeof(struct kinfo_proc)) {
			_kvm_err(kd, kd->program,
			    "kinfo_proc size mismatch (expected %zu, got %d)",
			    sizeof(struct kinfo_proc),
			    kd->procbase->ki_structsize);
			return (0);
		}
liveout:
		nprocs = size == 0 ? 0 : size / kd->procbase->ki_structsize;
	} else {
		struct nlist nl[7], *p;

		nl[0].n_name = "_nprocs";
		nl[1].n_name = "_allproc";
		nl[2].n_name = "_zombproc";
		nl[3].n_name = "_ticks";
		nl[4].n_name = "_hz";
		nl[5].n_name = "_cpu_tick_frequency";
		nl[6].n_name = 0;

		if (!kd->arch->ka_native(kd)) {
			_kvm_err(kd, kd->program,
			    "cannot read procs from non-native core");
			return (0);
		}

		if (kvm_nlist(kd, nl) != 0) {
			for (p = nl; p->n_type != 0; ++p)
				;
			_kvm_err(kd, kd->program,
				 "%s: no such symbol", p->n_name);
			return (0);
		}
		if (KREAD(kd, nl[0].n_value, &nprocs)) {
			_kvm_err(kd, kd->program, "can't read nprocs");
			return (0);
		}
		if (KREAD(kd, nl[3].n_value, &ticks)) {
			_kvm_err(kd, kd->program, "can't read ticks");
			return (0);
		}
		if (KREAD(kd, nl[4].n_value, &hz)) {
			_kvm_err(kd, kd->program, "can't read hz");
			return (0);
		}
		if (KREAD(kd, nl[5].n_value, &cpu_tick_frequency)) {
			_kvm_err(kd, kd->program,
			    "can't read cpu_tick_frequency");
			return (0);
		}
		size = nprocs * sizeof(struct kinfo_proc);
		kd->procbase = (struct kinfo_proc *)_kvm_malloc(kd, size);
		if (kd->procbase == NULL)
			return (0);

		nprocs = kvm_deadprocs(kd, op, arg, nl[1].n_value,
				      nl[2].n_value, nprocs);
		if (nprocs <= 0) {
			_kvm_freeprocs(kd);
			nprocs = 0;
		}
#ifdef notdef
		else {
			size = nprocs * sizeof(struct kinfo_proc);
			kd->procbase = realloc(kd->procbase, size);
		}
#endif
	}
	*cnt = nprocs;
	return (kd->procbase);
}

void
_kvm_freeprocs(kvm_t *kd)
{

	free(kd->procbase);
	kd->procbase = NULL;
}

void *
_kvm_realloc(kvm_t *kd, void *p, size_t n)
{
	void *np;

	np = reallocf(p, n);
	if (np == NULL)
		_kvm_err(kd, kd->program, "out of memory");
	return (np);
}

/*
 * Get the command args or environment.
 */
static char **
kvm_argv(kvm_t *kd, const struct kinfo_proc *kp, int env, int nchr)
{
	int oid[4];
	int i;
	size_t bufsz;
	static int buflen;
	static char *buf, *p;
	static char **bufp;
	static int argc;
	char **nbufp;

	if (!ISALIVE(kd)) {
		_kvm_err(kd, kd->program,
		    "cannot read user space from dead kernel");
		return (NULL);
	}

	if (nchr == 0 || nchr > ARG_MAX)
		nchr = ARG_MAX;
	if (buflen == 0) {
		buf = malloc(nchr);
		if (buf == NULL) {
			_kvm_err(kd, kd->program, "cannot allocate memory");
			return (NULL);
		}
		argc = 32;
		bufp = malloc(sizeof(char *) * argc);
		if (bufp == NULL) {
			free(buf);
			buf = NULL;
			_kvm_err(kd, kd->program, "cannot allocate memory");
			return (NULL);
		}
		buflen = nchr;
	} else if (nchr > buflen) {
		p = realloc(buf, nchr);
		if (p != NULL) {
			buf = p;
			buflen = nchr;
		}
	}
	oid[0] = CTL_KERN;
	oid[1] = KERN_PROC;
	oid[2] = env ? KERN_PROC_ENV : KERN_PROC_ARGS;
	oid[3] = kp->ki_pid;
	bufsz = buflen;
	if (sysctl(oid, 4, buf, &bufsz, 0, 0) == -1) {
		/*
		 * If the supplied buf is too short to hold the requested
		 * value the sysctl returns with ENOMEM. The buf is filled
		 * with the truncated value and the returned bufsz is equal
		 * to the requested len.
		 */
		if (errno != ENOMEM || bufsz != (size_t)buflen)
			return (NULL);
		buf[bufsz - 1] = '\0';
		errno = 0;
	} else if (bufsz == 0)
		return (NULL);
	i = 0;
	p = buf;
	do {
		bufp[i++] = p;
		p += strlen(p) + 1;
		if (i >= argc) {
			argc += argc;
			nbufp = realloc(bufp, sizeof(char *) * argc);
			if (nbufp == NULL)
				return (NULL);
			bufp = nbufp;
		}
	} while (p < buf + bufsz);
	bufp[i++] = 0;
	return (bufp);
}

char **
kvm_getargv(kvm_t *kd, const struct kinfo_proc *kp, int nchr)
{
	return (kvm_argv(kd, kp, 0, nchr));
}

char **
kvm_getenvv(kvm_t *kd, const struct kinfo_proc *kp, int nchr)
{
	return (kvm_argv(kd, kp, 1, nchr));
}
