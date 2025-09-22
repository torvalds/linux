/*	$OpenBSD: kern_sysctl.c,v 1.482 2025/08/06 14:00:33 mvs Exp $	*/
/*	$NetBSD: kern_sysctl.c,v 1.17 1996/05/20 17:49:05 mrg Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Karels at Berkeley Software Design, Inc.
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
 *
 *	@(#)kern_sysctl.c	8.4 (Berkeley) 4/14/94
 */

/*
 * sysctl system call.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/vnode.h>
#include <sys/unistd.h>
#include <sys/buf.h>
#include <sys/clockintr.h>
#include <sys/tty.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <sys/sysctl.h>
#include <sys/msgbuf.h>
#include <sys/vmmeter.h>
#include <sys/namei.h>
#include <sys/exec.h>
#include <sys/mbuf.h>
#include <sys/percpu.h>
#include <sys/sensors.h>
#include <sys/pipe.h>
#include <sys/eventvar.h>
#include <sys/socketvar.h>
#include <sys/socket.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/pledge.h>
#include <sys/timetc.h>
#include <sys/evcount.h>
#include <sys/un.h>
#include <sys/unpcb.h>
#include <sys/sched.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/wait.h>
#include <sys/witness.h>

#include <uvm/uvm_extern.h>

#include <dev/cons.h>

#include <dev/usb/ucomvar.h>

#include <net/route.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet6/ip6_var.h>

#ifdef DDB
#include <ddb/db_var.h>
#endif

#ifdef SYSVMSG
#include <sys/msg.h>
#endif
#ifdef SYSVSEM
#include <sys/sem.h>
#endif
#ifdef SYSVSHM
#include <sys/shm.h>
#endif

#include "audio.h"
#include "dt.h"
#include "pf.h"
#include "ucom.h"
#include "video.h"
#include "wskbd.h"

/*
 * Locks used to protect data:
 *	a	atomic
 */

extern struct forkstat forkstat;
extern struct nchstats nchstats;
extern int fscale;
extern fixpt_t ccpu;
extern long numvnodes;
extern int allowdt;
extern int audio_record_enable;
extern int audio_kbdcontrol_enable;
extern int video_record_enable;
extern int autoconf_serial;

int allowkmem;		/* [a] */

int sysctl_securelevel(void *, size_t *, void *, size_t, struct proc *);
int sysctl_diskinit(int, struct proc *);
int sysctl_proc_args(int *, u_int, void *, size_t *, struct proc *);
int sysctl_proc_cwd(int *, u_int, void *, size_t *, struct proc *);
int sysctl_proc_nobroadcastkill(int *, u_int, void *, size_t, void *, size_t *,
	struct proc *);
int sysctl_proc_vmmap(int *, u_int, void *, size_t *, struct proc *);
int sysctl_intrcnt(int *, u_int, void *, size_t *);
int sysctl_sensors(int *, u_int, void *, size_t *, void *, size_t);
int sysctl_cptime2(int *, u_int, void *, size_t *, void *, size_t);
int sysctl_audio(int *, u_int, void *, size_t *, void *, size_t);
int sysctl_video(int *, u_int, void *, size_t *, void *, size_t);
int sysctl_cpustats(int *, u_int, void *, size_t *, void *, size_t);
int sysctl_utc_offset(void *, size_t *, void *, size_t);
int sysctl_hwbattery(int *, u_int, void *, size_t *, void *, size_t);

void fill_file(struct kinfo_file *, struct file *, struct filedesc *, int,
    struct vnode *, struct process *, struct proc *, struct socket *, int);
void fill_kproc(struct process *, struct kinfo_proc *, struct proc *, int);

int kern_sysctl_locked(int *, u_int, void *, size_t *, void *, size_t,
	struct proc *);
int kern_sysctl_dirs(int, int *, u_int, void *, size_t *, void *,
	size_t, struct proc *);
int kern_sysctl_dirs_locked(int, int *, u_int, void *, size_t *, void *,
	size_t, struct proc *);
int hw_sysctl_locked(int *, u_int, void *, size_t *,void *, size_t,
	struct proc *);

int (*cpu_cpuspeed)(int *);

#ifndef SMALL_KERNEL
static void sysctl_ci_cp_time(struct cpu_info *, uint64_t *);
#endif

/*
 * Lock to avoid too many processes vslocking a large amount of memory
 * at the same time.
 */
struct rwlock sysctl_lock = RWLOCK_INITIALIZER("sysctllk");
struct rwlock sysctl_disklock = RWLOCK_INITIALIZER("sysctldlk");

int
sysctl_vslock(void *addr, size_t len)
{
	int error;

	error = rw_enter(&sysctl_lock, RW_WRITE|RW_INTR);
	if (error)
		return (error);
	KERNEL_LOCK();

	if (addr) {
		if (atop(len) > uvmexp.wiredmax - uvmexp.wired) {
			error = ENOMEM;
			goto out;
		}
		error = uvm_vslock(curproc, addr, len, PROT_READ | PROT_WRITE);
		if (error)
			goto out;
	}

	return (0);
out:
	KERNEL_UNLOCK();
	rw_exit_write(&sysctl_lock);
	return (error);
}

void
sysctl_vsunlock(void *addr, size_t len)
{
	KERNEL_ASSERT_LOCKED();

	if (addr)
		uvm_vsunlock(curproc, addr, len);
	KERNEL_UNLOCK();
	rw_exit_write(&sysctl_lock);
}

int
sys_sysctl(struct proc *p, void *v, register_t *retval)
{
	struct sys_sysctl_args /* {
		syscallarg(const int *) name;
		syscallarg(u_int) namelen;
		syscallarg(void *) old;
		syscallarg(size_t *) oldlenp;
		syscallarg(void *) new;
		syscallarg(size_t) newlen;
	} */ *uap = v;
	int error, dolock = 1;
	size_t savelen = 0, oldlen = 0;
	sysctlfn *fn;
	int name[CTL_MAXNAME];

	if (SCARG(uap, new) != NULL &&
	    (error = suser(p)))
		return (error);
	/*
	 * all top-level sysctl names are non-terminal
	 */
	if (SCARG(uap, namelen) > CTL_MAXNAME || SCARG(uap, namelen) < 2)
		return (EINVAL);
	error = copyin(SCARG(uap, name), name,
		       SCARG(uap, namelen) * sizeof(int));
	if (error)
		return (error);

	error = pledge_sysctl(p, SCARG(uap, namelen),
	    name, SCARG(uap, new));
	if (error)
		return (error);

	switch (name[0]) {
	case CTL_KERN:
		dolock = 0;
		fn = kern_sysctl;
		break;
	case CTL_HW:
		dolock = 0;
		fn = hw_sysctl;
		break;
	case CTL_NET:
		dolock = 0;
		fn = net_sysctl;
		break;
#ifndef SMALL_KERNEL
	case CTL_VM:
		fn = uvm_sysctl;
		break;
	case CTL_VFS:
		fn = vfs_sysctl;
		break;
#endif /* SMALL_KERNEL */
	case CTL_MACHDEP:
		fn = cpu_sysctl;
		break;
#ifdef DEBUG_SYSCTL
	case CTL_DEBUG:
		fn = debug_sysctl;
		break;
#endif
#ifdef DDB
	case CTL_DDB:
		fn = ddb_sysctl;
		break;
#endif
	default:
		return (EOPNOTSUPP);
	}

	if (SCARG(uap, oldlenp) &&
	    (error = copyin(SCARG(uap, oldlenp), &oldlen, sizeof(oldlen))))
		return (error);

	if (dolock) {
		error = sysctl_vslock(SCARG(uap, old), oldlen);
		if (error)
			return (error);
		savelen = oldlen;
	}
	error = (*fn)(&name[1], SCARG(uap, namelen) - 1, SCARG(uap, old),
	    &oldlen, SCARG(uap, new), SCARG(uap, newlen), p);
	if (dolock)
		sysctl_vsunlock(SCARG(uap, old), savelen);

	if (error)
		return (error);
	if (SCARG(uap, oldlenp))
		error = copyout(&oldlen, SCARG(uap, oldlenp), sizeof(oldlen));
	return (error);
}

/*
 * Attributes stored in the kernel.
 */
char hostname[MAXHOSTNAMELEN];
int hostnamelen;
char domainname[MAXHOSTNAMELEN];
int domainnamelen;
int hostid;
char *disknames = NULL;
size_t disknameslen;
struct diskstats *diskstats = NULL;
size_t diskstatslen;
int securelevel;

/* morally const values reported by sysctl_bounded_arr */
static int arg_max = ARG_MAX;
static int openbsd = OpenBSD;
static int posix_version = _POSIX_VERSION;
static int ngroups_max = NGROUPS_MAX;
static int int_zero = 0;
static int int_one = 1;
static int maxpartitions = MAXPARTITIONS;
static int raw_part = RAW_PART;

extern int somaxconn, sominconn;
extern int nosuidcoredump;
extern int maxlocksperuid;
extern int uvm_wxabort;
extern int global_ptrace;

const struct sysctl_bounded_args kern_vars[] = {
	{KERN_OSREV, &openbsd, SYSCTL_INT_READONLY},
	{KERN_MAXVNODES, &maxvnodes, 0, INT_MAX},
	{KERN_MAXPROC, &maxprocess, 0, INT_MAX},
	{KERN_MAXFILES, &maxfiles, 0, INT_MAX},
	{KERN_NFILES, &numfiles, SYSCTL_INT_READONLY},
	{KERN_TTYCOUNT, &tty_count, SYSCTL_INT_READONLY},
	{KERN_ARGMAX, &arg_max, SYSCTL_INT_READONLY},
	{KERN_POSIX1, &posix_version, SYSCTL_INT_READONLY},
	{KERN_NGROUPS, &ngroups_max, SYSCTL_INT_READONLY},
	{KERN_JOB_CONTROL, &int_one, SYSCTL_INT_READONLY},
	{KERN_SAVED_IDS, &int_one, SYSCTL_INT_READONLY},
	{KERN_MAXPARTITIONS, &maxpartitions, SYSCTL_INT_READONLY},
	{KERN_RAWPARTITION, &raw_part, SYSCTL_INT_READONLY},
	{KERN_MAXTHREAD, &maxthread, 0, INT_MAX},
	{KERN_NTHREADS, &nthreads, SYSCTL_INT_READONLY},
	{KERN_SOMAXCONN, &somaxconn, 0, SHRT_MAX},
	{KERN_SOMINCONN, &sominconn, 0, SHRT_MAX},
	{KERN_NOSUIDCOREDUMP, &nosuidcoredump, 0, 3},
	{KERN_FSYNC, &int_one, SYSCTL_INT_READONLY},
	{KERN_SYSVMSG,
#ifdef SYSVMSG
	 &int_one,
#else
	 &int_zero,
#endif
	 SYSCTL_INT_READONLY},
	{KERN_SYSVSEM,
#ifdef SYSVSEM
	 &int_one,
#else
	 &int_zero,
#endif
	 SYSCTL_INT_READONLY},
	{KERN_SYSVSHM,
#ifdef SYSVSHM
	 &int_one,
#else
	 &int_zero,
#endif
	 SYSCTL_INT_READONLY},
	{KERN_FSCALE, &fscale, SYSCTL_INT_READONLY},
	{KERN_CCPU, &ccpu, SYSCTL_INT_READONLY},
	{KERN_NPROCS, &nprocesses, SYSCTL_INT_READONLY},
	{KERN_SPLASSERT, &splassert_ctl, 0, 3},
	{KERN_MAXLOCKSPERUID, &maxlocksperuid, 0, INT_MAX},
	{KERN_WXABORT, &uvm_wxabort, 0, 1},
	{KERN_NETLIVELOCKS, &int_zero, SYSCTL_INT_READONLY},
#ifdef PTRACE
	{KERN_GLOBAL_PTRACE, &global_ptrace, 0, 1},
#endif
	{KERN_AUTOCONF_SERIAL, &autoconf_serial, SYSCTL_INT_READONLY},
};

int
kern_sysctl_dirs(int top_name, int *name, u_int namelen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen, struct proc *p)
{
	size_t savelen;
	int error;

	switch (top_name) {
#ifndef SMALL_KERNEL
	case KERN_FILE:
		return (sysctl_file(name, namelen, oldp, oldlenp, p));
	case KERN_MALLOCSTATS:
		return (sysctl_malloc(name, namelen, oldp, oldlenp,
		    newp, newlen, p));
	case KERN_CPTIME2:
		return (sysctl_cptime2(name, namelen, oldp, oldlenp,
		    newp, newlen));
	case KERN_POOL:
		return (sysctl_dopool(name, namelen, oldp, oldlenp));
	case KERN_CPUSTATS:
		return (sysctl_cpustats(name, namelen, oldp, oldlenp,
		    newp, newlen));
#endif /* SMALL_KERNEL */
#if NAUDIO > 0
	case KERN_AUDIO:
		return (sysctl_audio(name, namelen, oldp, oldlenp,
		    newp, newlen));
#endif
#if NVIDEO > 0
	case KERN_VIDEO:
		return (sysctl_video(name, namelen, oldp, oldlenp,
		    newp, newlen));
#endif
	default:
		break;
	}

	savelen = *oldlenp;
	if ((error = sysctl_vslock(oldp, savelen)))
		return (error);
	error = kern_sysctl_dirs_locked(top_name, name, namelen,
	    oldp, oldlenp, newp, newlen, p);
	sysctl_vsunlock(oldp, savelen);

	return (error);
}

int
kern_sysctl_dirs_locked(int top_name, int *name, u_int namelen,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen, struct proc *p)
{
	switch (top_name) {
#ifndef SMALL_KERNEL
	case KERN_PROC:
		return (sysctl_doproc(name, namelen, oldp, oldlenp));
	case KERN_PROC_ARGS:
		return (sysctl_proc_args(name, namelen, oldp, oldlenp, p));
	case KERN_PROC_CWD:
		return (sysctl_proc_cwd(name, namelen, oldp, oldlenp, p));
	case KERN_PROC_NOBROADCASTKILL:
		return (sysctl_proc_nobroadcastkill(name, namelen,
		     newp, newlen, oldp, oldlenp, p));
	case KERN_PROC_VMMAP:
		return (sysctl_proc_vmmap(name, namelen, oldp, oldlenp, p));
	case KERN_INTRCNT:
		return (sysctl_intrcnt(name, namelen, oldp, oldlenp));
	case KERN_WATCHDOG:
		return (sysctl_wdog(name, namelen, oldp, oldlenp,
		    newp, newlen));
	case KERN_EVCOUNT:
		return (evcount_sysctl(name, namelen, oldp, oldlenp,
		    newp, newlen));
	case KERN_CLOCKINTR:
		return sysctl_clockintr(name, namelen, oldp, oldlenp, newp,
		    newlen);
	case KERN_TTY:
		return (sysctl_tty(name, namelen, oldp, oldlenp,
		    newp, newlen));
#if defined(GPROF) || defined(DDBPROF)
	case KERN_PROF:
		return (sysctl_doprof(name, namelen, oldp, oldlenp,
		    newp, newlen));
#endif
#if defined(SYSVMSG) || defined(SYSVSEM) || defined(SYSVSHM)
	case KERN_SYSVIPC_INFO:
		return (sysctl_sysvipc(name, namelen, oldp, oldlenp));
#endif
#ifdef SYSVSEM
	case KERN_SEMINFO:
		return (sysctl_sysvsem(name, namelen, oldp, oldlenp,
		    newp, newlen));
#endif
#ifdef SYSVSHM
	case KERN_SHMINFO:
		return (sysctl_sysvshm(name, namelen, oldp, oldlenp,
		    newp, newlen));
#endif
	case KERN_TIMECOUNTER:
		return (sysctl_tc(name, namelen, oldp, oldlenp, newp, newlen));
#ifdef WITNESS
	case KERN_WITNESSWATCH:
		return witness_sysctl_watch(oldp, oldlenp, newp, newlen);
	case KERN_WITNESS:
		return witness_sysctl(name, namelen, oldp, oldlenp,
		    newp, newlen);
#endif /* WITNESS */
#endif /* SMALL_KERNEL */
	default:
		return (ENOTDIR);	/* overloaded */
	}
}

/*
 * kernel related system variables.
 */
int
kern_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	int error;
	size_t savelen;

	/* dispatch the non-terminal nodes first */
	if (namelen != 1)
		return (kern_sysctl_dirs(name[0], name + 1, namelen - 1,
		    oldp, oldlenp, newp, newlen, p));

	switch (name[0]) {
	case KERN_OSTYPE:
		return (sysctl_rdstring(oldp, oldlenp, newp, ostype));
	case KERN_OSRELEASE:
		return (sysctl_rdstring(oldp, oldlenp, newp, osrelease));
	case KERN_OSVERSION:
		return (sysctl_rdstring(oldp, oldlenp, newp, osversion));
	case KERN_VERSION:
		return (sysctl_rdstring(oldp, oldlenp, newp, version));
	case KERN_CONSBUF:
		if ((error = suser(p)))
			return (error);
		/* FALLTHROUGH */
	case KERN_MSGBUF: {
		extern struct mutex log_mtx;
		const size_t hlen = offsetof(struct msgbuf, msg_bufc);
		struct msgbuf ump, *mp = (name[0] == KERN_MSGBUF) ?
		    msgbufp : consbufp;

		/*
		 * deal with cases where the message buffer has
		 * become corrupted.
		 */
		if (!mp || mp->msg_magic != MSG_MAGIC)
			return (ENXIO);
		if (newp)
			return (EPERM);
		if (oldp) {
			if ((hlen + mp->msg_bufs) > *oldlenp)
				return (ENOMEM);
		} else
			return (0);

		mtx_enter(&log_mtx);
		memset(&ump, 0, sizeof(ump));
		ump.msg_magic = mp->msg_magic;
		ump.msg_bufs = mp->msg_bufs;
		ump.msg_bufx = mp->msg_bufx;
		ump.msg_bufr = mp->msg_bufr;
		ump.msg_bufd = mp->msg_bufd;
		mtx_leave(&log_mtx);

		/* copy header... */
		if ((error = copyout(&ump, oldp, hlen)))
			return (error);
		/* ...and the data. */
		error = copyout(mp->msg_bufc, oldp + hlen, mp->msg_bufs);

		return (error);
	}
	case KERN_CONSBUFSIZE:
	case KERN_MSGBUFSIZE: {
		struct msgbuf *mp = (name[0] == KERN_MSGBUFSIZE) ?
		    msgbufp : consbufp;

		/*
		 * deal with cases where the message buffer has
		 * become corrupted.
		 */
		if (!mp || mp->msg_magic != MSG_MAGIC)
			return (ENXIO);
		return (sysctl_rdint(oldp, oldlenp, newp, mp->msg_bufs));
	}
#if NDT > 0
	case KERN_ALLOWDT:
		return (sysctl_securelevel_int(oldp, oldlenp, newp, newlen,
		    &allowdt));
#endif
	case KERN_HOSTID:
		return (sysctl_int(oldp, oldlenp, newp, newlen, &hostid));
	case KERN_CLOCKRATE:
		return (sysctl_clockrate(oldp, oldlenp, newp));
#ifndef SMALL_KERNEL
	case KERN_ALLOWKMEM:
		return (sysctl_securelevel_int(oldp, oldlenp, newp, newlen,
		    &allowkmem));
	case KERN_NUMVNODES:  /* XXX numvnodes is a long */
		return (sysctl_rdint(oldp, oldlenp, newp, numvnodes));
	case KERN_BOOTTIME: {
		struct timeval bt;
		memset(&bt, 0, sizeof bt);
		microboottime(&bt);
		return (sysctl_rdstruct(oldp, oldlenp, newp, &bt, sizeof bt));
	}
	case KERN_MAXCLUSTERS: {
		int oldval, newval;

		oldval = newval = atomic_load_long(&nmbclust);
		error = sysctl_int(oldp, oldlenp, newp, newlen, &newval);

		if (error == 0 && oldval != newval) {
			rw_enter_write(&sysctl_lock);
			error = nmbclust_update(newval);
			rw_exit_write(&sysctl_lock);
		}

		return (error);
	}
	case KERN_MBSTAT: {
		uint64_t counters[mbs_ncounters];
		struct mbstat mbs;
		unsigned int i;

		memset(&mbs, 0, sizeof(mbs));
		counters_read(mbstat, counters, mbs_ncounters, NULL);
		for (i = 0; i < MT_NTYPES; i++)
			mbs.m_mtypes[i] = counters[i];
#define ASSIGN(name) do { mbs.m_##name = counters[mbs_##name]; } while (0)
		ASSIGN(drops);
		ASSIGN(wait);
		ASSIGN(drain);
		ASSIGN(defrag_alloc);
		ASSIGN(prepend_alloc);
		ASSIGN(pullup_alloc);
		ASSIGN(pullup_copy);
		ASSIGN(pulldown_alloc);
		ASSIGN(pulldown_copy);
#undef ASSIGN
		return (sysctl_rdstruct(oldp, oldlenp, newp,
		    &mbs, sizeof(mbs)));
	}
	case KERN_CPTIME:
	{
		CPU_INFO_ITERATOR cii;
		struct cpu_info *ci;
		long cp_time[CPUSTATES];
		int i, n = 0;

		memset(cp_time, 0, sizeof(cp_time));

		CPU_INFO_FOREACH(cii, ci) {
			uint64_t ci_cp_time[CPUSTATES];

			if (!cpu_is_online(ci))
				continue;

			n++;
			sysctl_ci_cp_time(ci, ci_cp_time);
			for (i = 0; i < CPUSTATES; i++)
				cp_time[i] += ci_cp_time[i];
		}

		for (i = 0; i < CPUSTATES; i++)
			cp_time[i] /= n;

		return (sysctl_rdstruct(oldp, oldlenp, newp, &cp_time,
		    sizeof(cp_time)));
	}
	case KERN_POOL_DEBUG: {
		extern int pool_debug;
		int oldval, newval;

		oldval = newval = atomic_load_int(&pool_debug);

		error = sysctl_int(oldp, oldlenp, newp, newlen, &newval);
		if (error == 0 && oldval != newval &&
		    oldval == atomic_cas_uint(&pool_debug, oldval, newval))
			pool_reclaim_all();

		return (error);
	}
	case KERN_TIMEOUT_STATS:
		return (timeout_sysctl(oldp, oldlenp, newp, newlen));
	case KERN_MAXPROC:
	case KERN_MAXFILES:
	case KERN_NFILES:
	case KERN_TTYCOUNT:
	case KERN_ARGMAX:
	case KERN_POSIX1:
	case KERN_NGROUPS:
	case KERN_JOB_CONTROL:
	case KERN_SAVED_IDS:
	case KERN_FSYNC:
	case KERN_SYSVMSG:
	case KERN_SYSVSEM:
	case KERN_SYSVSHM:
	case KERN_SOMAXCONN:
	case KERN_SOMINCONN:
	case KERN_NOSUIDCOREDUMP:
	case KERN_WXABORT:
	case KERN_NETLIVELOCKS:
	case KERN_GLOBAL_PTRACE:
	case KERN_AUTOCONF_SERIAL:
#endif /* SMALL_KERNEL */
	case KERN_OSREV:
	case KERN_MAXPARTITIONS:
	case KERN_RAWPARTITION:
	case KERN_MAXTHREAD:
	case KERN_NTHREADS:
	case KERN_FSCALE:
	case KERN_CCPU:
	case KERN_NPROCS:
		return (sysctl_bounded_arr(kern_vars, nitems(kern_vars), name,
		    namelen, oldp, oldlenp, newp, newlen));
	}

	savelen = *oldlenp;
	if ((error = sysctl_vslock(oldp, savelen)))
		return (error);
	error = kern_sysctl_locked(name, namelen, oldp, oldlenp,
	    newp, newlen, p);
	sysctl_vsunlock(oldp, savelen);

	return (error);
}

int
kern_sysctl_locked(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen, struct proc *p)
{
	int error;
	dev_t dev;

	switch (name[0]) {
	case KERN_SECURELVL:
		return (sysctl_securelevel(oldp, oldlenp, newp, newlen, p));
	case KERN_HOSTNAME:
		error = sysctl_tstring(oldp, oldlenp, newp, newlen,
		    hostname, sizeof(hostname));
		if (newp && !error)
			hostnamelen = newlen;
		return (error);
	case KERN_DOMAINNAME:
		if (securelevel >= 1 && domainnamelen && newp)
			error = EPERM;
		else
			error = sysctl_tstring(oldp, oldlenp, newp, newlen,
			    domainname, sizeof(domainname));
		if (newp && !error)
			domainnamelen = newlen;
		return (error);
#ifndef SMALL_KERNEL
	case KERN_NCHSTATS:
		return (sysctl_rdstruct(oldp, oldlenp, newp, &nchstats,
		    sizeof(struct nchstats)));
	case KERN_FORKSTAT:
		return (sysctl_rdstruct(oldp, oldlenp, newp, &forkstat,
		    sizeof(struct forkstat)));
	case KERN_STACKGAPRANDOM: {
		int stackgap = stackgap_random;

		error = sysctl_int(oldp, oldlenp, newp, newlen, &stackgap);
		if (error)
			return (error);
		/*
		 * Safety harness.
		 */
		if ((stackgap < ALIGNBYTES && stackgap != 0) ||
		    !powerof2(stackgap) || stackgap >= MAXSSIZ)
			return (EINVAL);
		stackgap_random = stackgap;
		return (0);
	    }
	case KERN_CACHEPCT: {
		u_int64_t dmapages;
		int opct, pgs;
		opct = bufcachepercent;
		error = sysctl_int(oldp, oldlenp, newp, newlen,
		    &bufcachepercent);
		if (error)
			return(error);
		if (bufcachepercent > 90 || bufcachepercent < 5) {
			bufcachepercent = opct;
			return (EINVAL);
		}
		dmapages = uvm_pagecount(&dma_constraint);
		if (bufcachepercent != opct) {
			pgs = bufcachepercent * dmapages / 100;
			bufadjust(pgs); /* adjust bufpages */
			bufhighpages = bufpages; /* set high water mark */
		}
		return(0);
	}
#endif /* SMALL_KERNEL */
#if NPF > 0
	case KERN_PFSTATUS:
		return (pf_sysctl(oldp, oldlenp, newp, newlen));
#endif
	case KERN_CONSDEV:
		if (cn_tab != NULL)
			dev = cn_tab->cn_dev;
		else
			dev = NODEV;
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
	case KERN_UTC_OFFSET:
		return (sysctl_utc_offset(oldp, oldlenp, newp, newlen));
	default:
		return (sysctl_bounded_arr(kern_vars, nitems(kern_vars), name,
		    namelen, oldp, oldlenp, newp, newlen));
	}
	/* NOTREACHED */
}

/*
 * hardware related system variables.
 */
char *hw_vendor, *hw_prod, *hw_uuid, *hw_serial, *hw_ver;
int allowpowerdown = 1;
int hw_power = 1;

/* morally const values reported by sysctl_bounded_arr */
static int byte_order = BYTE_ORDER;

const struct sysctl_bounded_args hw_vars[] = {
	{HW_NCPU, &ncpus, SYSCTL_INT_READONLY},
	{HW_NCPUFOUND, &ncpusfound, SYSCTL_INT_READONLY},
	{HW_BYTEORDER, &byte_order, SYSCTL_INT_READONLY},
	{HW_PAGESIZE, &uvmexp.pagesize, SYSCTL_INT_READONLY},
	{HW_DISKCOUNT, &disk_count, SYSCTL_INT_READONLY},
	{HW_POWER, &hw_power, SYSCTL_INT_READONLY},
};

int
hw_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	extern char machine[], cpu_model[];
	int err;

	/*
	 * all sysctl names at this level except sensors and battery
	 * are terminal
	 */
	if (name[0] != HW_SENSORS && name[0] != HW_BATTERY && namelen != 1)
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
	case HW_MACHINE:
		return (sysctl_rdstring(oldp, oldlenp, newp, machine));
	case HW_MODEL:
		return (sysctl_rdstring(oldp, oldlenp, newp, cpu_model));
	case HW_NCPUONLINE:
		return (sysctl_rdint(oldp, oldlenp, newp,
		    sysctl_hwncpuonline()));
	case HW_PHYSMEM:
		return (sysctl_rdint(oldp, oldlenp, newp, ptoa(physmem)));
	case HW_USERMEM:
		return (sysctl_rdint(oldp, oldlenp, newp,
		    ptoa(physmem - uvmexp.wired)));
#ifndef SMALL_KERNEL
	case HW_SENSORS:
		return (sysctl_sensors(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif
	case HW_DISKNAMES:
#ifndef	SMALL_KERNEL
	case HW_DISKSTATS:
	case HW_CPUSPEED:
	case HW_SETPERF:
	case HW_PERFPOLICY:
	case HW_BATTERY:
	case HW_ALLOWPOWERDOWN:
	case HW_UCOMNAMES:
#ifdef __HAVE_CPU_TOPOLOGY
	case HW_SMT:
#endif
#endif /* !SMALL_KERNEL */
	{
		size_t savelen = *oldlenp;
		if ((err = sysctl_vslock(oldp, savelen)))
			return (err);
		err = hw_sysctl_locked(name, namelen, oldp, oldlenp,
		    newp, newlen, p);
		sysctl_vsunlock(oldp, savelen);
		return (err);
	}
	case HW_VENDOR:
		if (hw_vendor)
			return (sysctl_rdstring(oldp, oldlenp, newp,
			    hw_vendor));
		else
			return (EOPNOTSUPP);
	case HW_PRODUCT:
		if (hw_prod)
			return (sysctl_rdstring(oldp, oldlenp, newp, hw_prod));
		else
			return (EOPNOTSUPP);
	case HW_VERSION:
		if (hw_ver)
			return (sysctl_rdstring(oldp, oldlenp, newp, hw_ver));
		else
			return (EOPNOTSUPP);
	case HW_SERIALNO:
		if (hw_serial)
			return (sysctl_rdstring(oldp, oldlenp, newp,
			    hw_serial));
		else
			return (EOPNOTSUPP);
	case HW_UUID:
		if (hw_uuid)
			return (sysctl_rdstring(oldp, oldlenp, newp, hw_uuid));
		else
			return (EOPNOTSUPP);
	case HW_PHYSMEM64:
		return (sysctl_rdquad(oldp, oldlenp, newp,
		    ptoa((psize_t)physmem)));
	case HW_USERMEM64:
		return (sysctl_rdquad(oldp, oldlenp, newp,
		    ptoa((psize_t)physmem - uvmexp.wired)));
	default:
		return sysctl_bounded_arr(hw_vars, nitems(hw_vars), name,
		    namelen, oldp, oldlenp, newp, newlen);
	}
	/* NOTREACHED */
}

int
hw_sysctl_locked(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen, struct proc *p)
{
	int err;

	switch (name[0]) {
	case HW_DISKNAMES:
		err = sysctl_diskinit(0, p);
		if (err)
			return err;
		if (disknames)
			return (sysctl_rdstring(oldp, oldlenp, newp,
			    disknames));
		else
			return (sysctl_rdstring(oldp, oldlenp, newp, ""));
#ifndef SMALL_KERNEL
	case HW_DISKSTATS:
		err = sysctl_diskinit(1, p);
		if (err)
			return err;
		return (sysctl_rdstruct(oldp, oldlenp, newp, diskstats,
		    disk_count * sizeof(struct diskstats)));
	case HW_CPUSPEED: {
		int cpuspeed;
		if (!cpu_cpuspeed)
			return (EOPNOTSUPP);
		err = cpu_cpuspeed(&cpuspeed);
		if (err)
			return err;
		return (sysctl_rdint(oldp, oldlenp, newp, cpuspeed));
	    }
	case HW_SETPERF:
		return (sysctl_hwsetperf(oldp, oldlenp, newp, newlen));
	case HW_PERFPOLICY:
		return (sysctl_hwperfpolicy(oldp, oldlenp, newp, newlen));
	case HW_ALLOWPOWERDOWN:
		return (sysctl_securelevel_int(oldp, oldlenp, newp, newlen,
		    &allowpowerdown));
	case HW_UCOMNAMES: {
		const char *str = "";
#if NUCOM > 0
		str = sysctl_ucominit();
#endif	/* NUCOM > 0 */
		return (sysctl_rdstring(oldp, oldlenp, newp, str));
	    }
#ifdef __HAVE_CPU_TOPOLOGY
	case HW_SMT:
		return (sysctl_hwsmt(oldp, oldlenp, newp, newlen));
#endif
	case HW_BATTERY:
		return (sysctl_hwbattery(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen));
#endif /* SMALL_KERNEL */
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

#ifndef SMALL_KERNEL
int hw_battery_chargemode;
int hw_battery_chargestart;
int hw_battery_chargestop;
int (*hw_battery_setchargemode)(int);
int (*hw_battery_setchargestart)(int);
int (*hw_battery_setchargestop)(int);

int
sysctl_hwchargemode(void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
	int mode = hw_battery_chargemode;
	int error;

	if (!hw_battery_setchargemode)
		return EOPNOTSUPP;

	error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
	    &mode, -1, 1);
	if (error)
		return error;

	if (newp != NULL)
		error = hw_battery_setchargemode(mode);

	return error;
}

int
sysctl_hwchargestart(void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
	int start = hw_battery_chargestart;
	int error;

	if (!hw_battery_setchargestart)
		return EOPNOTSUPP;

	error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
	    &start, 0, 100);
	if (error)
		return error;

	if (newp != NULL)
		error = hw_battery_setchargestart(start);

	return error;
}

int
sysctl_hwchargestop(void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
	int stop = hw_battery_chargestop;
	int error;

	if (!hw_battery_setchargestop)
		return EOPNOTSUPP;

	error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
	    &stop, 0, 100);
	if (error)
		return error;

	if (newp != NULL)
		error = hw_battery_setchargestop(stop);

	return error;
}

int
sysctl_hwbattery(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case HW_BATTERY_CHARGEMODE:
		return (sysctl_hwchargemode(oldp, oldlenp, newp, newlen));
	case HW_BATTERY_CHARGESTART:
		return (sysctl_hwchargestart(oldp, oldlenp, newp, newlen));
	case HW_BATTERY_CHARGESTOP:
		return (sysctl_hwchargestop(oldp, oldlenp, newp, newlen));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

#endif	/* SMALL_KERNEL */

#ifdef DEBUG_SYSCTL
/*
 * Debugging related system variables.
 */
extern struct ctldebug debug_vfs_busyprt;
struct ctldebug debug1, debug2, debug3, debug4;
struct ctldebug debug5, debug6, debug7, debug8, debug9;
struct ctldebug debug10, debug11, debug12, debug13, debug14;
struct ctldebug debug15, debug16, debug17, debug18, debug19;
static struct ctldebug *debugvars[CTL_DEBUG_MAXID] = {
	&debug_vfs_busyprt,
	&debug1, &debug2, &debug3, &debug4,
	&debug5, &debug6, &debug7, &debug8, &debug9,
	&debug10, &debug11, &debug12, &debug13, &debug14,
	&debug15, &debug16, &debug17, &debug18, &debug19,
};
int
debug_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	struct ctldebug *cdp;

	/* all sysctl names at this level are name and field */
	if (namelen != 2)
		return (ENOTDIR);		/* overloaded */
	if (name[0] < 0 || name[0] >= nitems(debugvars))
		return (EOPNOTSUPP);
	cdp = debugvars[name[0]];
	if (cdp->debugname == 0)
		return (EOPNOTSUPP);
	switch (name[1]) {
	case CTL_DEBUG_NAME:
		return (sysctl_rdstring(oldp, oldlenp, newp, cdp->debugname));
	case CTL_DEBUG_VALUE:
		return (sysctl_int(oldp, oldlenp, newp, newlen, cdp->debugvar));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}
#endif /* DEBUG_SYSCTL */

/*
 * Reads, or writes that lower the value
 */
int
sysctl_int_lower(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    int *valp)
{
	unsigned int oldval, newval;
	int error;

	if (oldp && *oldlenp < sizeof(int))
		return (ENOMEM);
	if (newp && newlen != sizeof(int))
		return (EINVAL);
	*oldlenp = sizeof(int);

	if (newp) {
		if ((error = copyin(newp, &newval, sizeof(int))))
			return (error);
		do {
			oldval = atomic_load_int(valp);
			if (oldval < (unsigned int)newval)
				return (EPERM);	/* do not allow raising */
		} while (atomic_cas_uint(valp, oldval, newval) != oldval);

		if (oldp) {
			/* new value has been set although user gets error */
			if ((error = copyout(&oldval, oldp, sizeof(int))))
				return (error);
		}
	} else if (oldp) {
		oldval = atomic_load_int(valp);

		if ((error = copyout(&oldval, oldp, sizeof(int))))
			return (error);
	}

	return (0);
}

/*
 * Validate parameters and get old / set new parameters
 * for an integer-valued sysctl function.
 */
int
sysctl_int(void *oldp, size_t *oldlenp, void *newp, size_t newlen, int *valp)
{
	return (sysctl_int_bounded(oldp, oldlenp, newp, newlen, valp,
	    INT_MIN, INT_MAX));
}

/*
 * As above, but read-only.
 */
int
sysctl_rdint(void *oldp, size_t *oldlenp, void *newp, int val)
{
	int error = 0;

	if (oldp && *oldlenp < sizeof(int))
		return (ENOMEM);
	if (newp)
		return (EPERM);
	*oldlenp = sizeof(int);
	if (oldp)
		error = copyout((caddr_t)&val, oldp, sizeof(int));
	return (error);
}

int
sysctl_securelevel(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    struct proc *p)
{
	int oldval, newval;
	int error;

	if (oldp && *oldlenp < sizeof(int))
		return (ENOMEM);
	if (newp && newlen != sizeof(int))
		return (EINVAL);
	*oldlenp = sizeof(int);

	if (newp) {
		if ((error = copyin(newp, &newval, sizeof(int))))
			return (error);
		do {
			oldval = atomic_load_int(&securelevel);
			if ((oldval > 0 || newval < -1) && newval < oldval &&
			    p->p_p->ps_pid != 1)
				return (EPERM);
		} while (atomic_cas_uint(&securelevel, oldval, newval) !=
		    oldval);

		if (oldp) {
			/* new value has been set although user gets error */
			if ((error = copyout(&oldval, oldp, sizeof(int))))
				return (error);
		}
	} else if (oldp) {
		oldval = atomic_load_int(&securelevel);

		if ((error = copyout(&oldval, oldp, sizeof(int))))
			return (error);
	}

	return (0);
}

/*
 * Selects between sysctl_rdint and sysctl_int according to securelevel.
 */
int
sysctl_securelevel_int(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    int *valp)
{
	if ((int)atomic_load_int(&securelevel) > 0)
		return (sysctl_rdint(oldp, oldlenp, newp, *valp));
	return (sysctl_int(oldp, oldlenp, newp, newlen, valp));
}

/*
 * Read-only or bounded integer values.
 */
int
sysctl_int_bounded(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    int *valp, int minimum, int maximum)
{
	int oldval, newval;
	int error;

	/* read only */
	if (newp != NULL && minimum > maximum)
		return (EPERM);

	if (oldp != NULL && *oldlenp < sizeof(int))
		return (ENOMEM);
	if (newp != NULL && newlen != sizeof(int))
		return (EINVAL);
	*oldlenp = sizeof(int);

	/* copyin() may sleep, call it first */
	if (newp != NULL) {
		if ((error = copyin(newp, &newval, sizeof(int))))
			return (error);
		/* outside limits */
		if (newval < minimum || maximum < newval)
			return (EINVAL);
	}
	if (oldp != NULL) {
		if (newp != NULL)
			oldval = atomic_swap_uint(valp, newval);
		else
			oldval = atomic_load_int(valp);
		if ((error = copyout(&oldval, oldp, sizeof(int)))) {
			/* new value has been set although user gets error */
			return (error);
		}
	} else if (newp != NULL)
		atomic_store_int(valp, newval);

	return (0);
}

/*
 * Array of read-only or bounded integer values.
 */
int
sysctl_bounded_arr(const struct sysctl_bounded_args *valpp, u_int valplen,
    int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	u_int i;
	if (namelen != 1)
		return (ENOTDIR);
	for (i = 0; i < valplen; ++i) {
		if (valpp[i].mib == name[0]) {
			return (sysctl_int_bounded(oldp, oldlenp, newp, newlen,
			    valpp[i].var, valpp[i].minimum, valpp[i].maximum));
		}
	}
	return (EOPNOTSUPP);
}

/*
 * Validate parameters and get old parameters
 * for an integer-valued sysctl function.
 */
int
sysctl_rdquad(void *oldp, size_t *oldlenp, void *newp, int64_t val)
{
	int error = 0;

	if (oldp && *oldlenp < sizeof(int64_t))
		return (ENOMEM);
	if (newp)
		return (EPERM);
	*oldlenp = sizeof(int64_t);
	if (oldp)
		error = copyout((caddr_t)&val, oldp, sizeof(int64_t));
	return (error);
}

/*
 * Validate parameters and get old / set new parameters
 * for a string-valued sysctl function.
 */
int
sysctl_string(void *oldp, size_t *oldlenp, void *newp, size_t newlen, char *str,
    size_t maxlen)
{
	return sysctl__string(oldp, oldlenp, newp, newlen, str, maxlen, 0);
}

int
sysctl_tstring(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    char *str, size_t maxlen)
{
	return sysctl__string(oldp, oldlenp, newp, newlen, str, maxlen, 1);
}

int
sysctl__string(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    char *str, size_t maxlen, int trunc)
{
	size_t len;
	int error = 0;

	len = strlen(str) + 1;
	if (oldp && *oldlenp < len) {
		if (trunc == 0 || *oldlenp == 0)
			return (ENOMEM);
	}
	if (newp && newlen >= maxlen)
		return (EINVAL);
	if (oldp) {
		if (trunc && *oldlenp < len) {
			len = *oldlenp;
			error = copyout(str, oldp, len - 1);
			if (error == 0)
				error = copyout("", (char *)oldp + len - 1, 1);
		} else {
			error = copyout(str, oldp, len);
		}
	}
	*oldlenp = len;
	if (error == 0 && newp) {
		error = copyin(newp, str, newlen);
		str[newlen] = 0;
	}
	return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdstring(void *oldp, size_t *oldlenp, void *newp, const char *str)
{
	size_t len;
	int error = 0;

	len = strlen(str) + 1;
	if (oldp && *oldlenp < len)
		return (ENOMEM);
	if (newp)
		return (EPERM);
	*oldlenp = len;
	if (oldp)
		error = copyout(str, oldp, len);
	return (error);
}

/*
 * Validate parameters and get old / set new parameters
 * for a structure oriented sysctl function.
 */
int
sysctl_struct(void *oldp, size_t *oldlenp, void *newp, size_t newlen, void *sp,
    size_t len)
{
	int error = 0;

	if (oldp && *oldlenp < len)
		return (ENOMEM);
	if (newp && newlen > len)
		return (EINVAL);
	if (oldp) {
		*oldlenp = len;
		error = copyout(sp, oldp, len);
	}
	if (error == 0 && newp)
		error = copyin(newp, sp, len);
	return (error);
}

/*
 * Validate parameters and get old parameters
 * for a structure oriented sysctl function.
 */
int
sysctl_rdstruct(void *oldp, size_t *oldlenp, void *newp, const void *sp,
    size_t len)
{
	int error = 0;

	if (oldp && *oldlenp < len)
		return (ENOMEM);
	if (newp)
		return (EPERM);
	*oldlenp = len;
	if (oldp)
		error = copyout(sp, oldp, len);
	return (error);
}

#ifndef SMALL_KERNEL
void
fill_file(struct kinfo_file *kf, struct file *fp, struct filedesc *fdp,
	  int fd, struct vnode *vp, struct process *pr, struct proc *p,
	  struct socket *so, int show_pointers)
{
	struct vattr va;

	memset(kf, 0, sizeof(*kf));

	kf->fd_fd = fd;		/* might not really be an fd */

	if (fp != NULL) {
		if (show_pointers)
			kf->f_fileaddr = PTRTOINT64(fp);
		kf->f_flag = fp->f_flag;
		kf->f_iflags = fp->f_iflags;
		kf->f_type = fp->f_type;
		kf->f_count = fp->f_count;
		if (show_pointers)
			kf->f_ucred = PTRTOINT64(fp->f_cred);
		kf->f_uid = fp->f_cred->cr_uid;
		kf->f_gid = fp->f_cred->cr_gid;
		if (show_pointers)
			kf->f_ops = PTRTOINT64(fp->f_ops);
		if (show_pointers)
			kf->f_data = PTRTOINT64(fp->f_data);
		kf->f_usecount = 0;

		if (suser(p) == 0 || p->p_ucred->cr_uid == fp->f_cred->cr_uid) {
			mtx_enter(&fp->f_mtx);
			kf->f_offset = fp->f_offset;
			kf->f_rxfer = fp->f_rxfer;
			kf->f_rwfer = fp->f_wxfer;
			kf->f_seek = fp->f_seek;
			kf->f_rbytes = fp->f_rbytes;
			kf->f_wbytes = fp->f_wbytes;
			mtx_leave(&fp->f_mtx);
		} else
			kf->f_offset = -1;
	} else if (vp != NULL) {
		/* fake it */
		kf->f_type = DTYPE_VNODE;
		kf->f_flag = FREAD;
		if (fd == KERN_FILE_TRACE)
			kf->f_flag |= FWRITE;
	} else if (so != NULL) {
		/* fake it */
		kf->f_type = DTYPE_SOCKET;
	}

	/* information about the object associated with this file */
	switch (kf->f_type) {
	case DTYPE_VNODE:
		if (fp != NULL)
			vp = (struct vnode *)fp->f_data;

		if (show_pointers)
			kf->v_un = PTRTOINT64(vp->v_un.vu_socket);
		kf->v_type = vp->v_type;
		kf->v_tag = vp->v_tag;
		kf->v_flag = vp->v_flag;
		if (show_pointers)
			kf->v_data = PTRTOINT64(vp->v_data);
		if (show_pointers)
			kf->v_mount = PTRTOINT64(vp->v_mount);
		if (vp->v_mount)
			strlcpy(kf->f_mntonname,
			    vp->v_mount->mnt_stat.f_mntonname,
			    sizeof(kf->f_mntonname));

		if (VOP_GETATTR(vp, &va, p->p_ucred, p) == 0) {
			kf->va_fileid = va.va_fileid;
			kf->va_mode = MAKEIMODE(va.va_type, va.va_mode);
			kf->va_size = va.va_size;
			kf->va_rdev = va.va_rdev;
			kf->va_fsid = va.va_fsid & 0xffffffff;
			kf->va_nlink = va.va_nlink;
		}
		break;

	case DTYPE_SOCKET: {
		int locked = 0;

		if (so == NULL) {
			so = (struct socket *)fp->f_data;
			/* if so is passed as parameter it is already locked */
			solock_shared(so);
			locked = 1;
		}

		kf->so_type = so->so_type;
		kf->so_state = so->so_state | so->so_snd.sb_state |
		    so->so_rcv.sb_state;
		if (show_pointers)
			kf->so_pcb = PTRTOINT64(so->so_pcb);
		else
			kf->so_pcb = -1;
		kf->so_protocol = so->so_proto->pr_protocol;
		kf->so_family = so->so_proto->pr_domain->dom_family;
		kf->so_rcv_cc = so->so_rcv.sb_cc;
		kf->so_snd_cc = so->so_snd.sb_cc;
		if (isspliced(so)) {
			if (show_pointers)
				kf->so_splice =
				    PTRTOINT64(so->so_sp->ssp_socket);
			kf->so_splicelen = so->so_sp->ssp_len;
		} else if (issplicedback(so))
			kf->so_splicelen = -1;
		if (so->so_pcb == NULL) {
			if (locked)
				sounlock_shared(so);
			break;
		}
		switch (kf->so_family) {
		case AF_INET: {
			struct inpcb *inpcb = so->so_pcb;

			soassertlocked(so);
			if (show_pointers)
				kf->inp_ppcb = PTRTOINT64(inpcb->inp_ppcb);
			kf->inp_lport = inpcb->inp_lport;
			kf->inp_laddru[0] = inpcb->inp_laddr.s_addr;
			kf->inp_fport = inpcb->inp_fport;
			kf->inp_faddru[0] = inpcb->inp_faddr.s_addr;
			kf->inp_rtableid = inpcb->inp_rtableid;
			if (so->so_type == SOCK_RAW)
				kf->inp_proto = inpcb->inp_ip.ip_p;
			if (so->so_proto->pr_protocol == IPPROTO_TCP) {
				struct tcpcb *tcpcb = intotcpcb(inpcb);

				kf->t_rcv_wnd = tcpcb->rcv_wnd;
				kf->t_snd_wnd = tcpcb->snd_wnd;
				kf->t_snd_cwnd = tcpcb->snd_cwnd;
				kf->t_state = tcpcb->t_state;
			}
			break;
		    }
		case AF_INET6: {
			struct inpcb *inpcb = so->so_pcb;

			soassertlocked(so);
			if (show_pointers)
				kf->inp_ppcb = PTRTOINT64(inpcb->inp_ppcb);
			kf->inp_lport = inpcb->inp_lport;
			kf->inp_laddru[0] = inpcb->inp_laddr6.s6_addr32[0];
			kf->inp_laddru[1] = inpcb->inp_laddr6.s6_addr32[1];
			kf->inp_laddru[2] = inpcb->inp_laddr6.s6_addr32[2];
			kf->inp_laddru[3] = inpcb->inp_laddr6.s6_addr32[3];
			kf->inp_fport = inpcb->inp_fport;
			kf->inp_faddru[0] = inpcb->inp_faddr6.s6_addr32[0];
			kf->inp_faddru[1] = inpcb->inp_faddr6.s6_addr32[1];
			kf->inp_faddru[2] = inpcb->inp_faddr6.s6_addr32[2];
			kf->inp_faddru[3] = inpcb->inp_faddr6.s6_addr32[3];
			kf->inp_rtableid = inpcb->inp_rtableid;
			if (so->so_type == SOCK_RAW)
				kf->inp_proto = inpcb->inp_ipv6.ip6_nxt;
			if (so->so_proto->pr_protocol == IPPROTO_TCP) {
				struct tcpcb *tcpcb = intotcpcb(inpcb);

				kf->t_rcv_wnd = tcpcb->rcv_wnd;
				kf->t_snd_wnd = tcpcb->snd_wnd;
				kf->t_snd_cwnd = tcpcb->snd_cwnd;
				kf->t_state = tcpcb->t_state;
			}
			break;
		    }
		case AF_UNIX: {
			struct unpcb *unpcb = so->so_pcb;

			kf->f_msgcount = unpcb->unp_msgcount;
			if (show_pointers) {
				kf->unp_conn	= PTRTOINT64(unpcb->unp_conn);
				kf->unp_refs	= PTRTOINT64(
				    SLIST_FIRST(&unpcb->unp_refs));
				kf->unp_nextref	= PTRTOINT64(
				    SLIST_NEXT(unpcb, unp_nextref));
				kf->v_un	= PTRTOINT64(unpcb->unp_vnode);
				kf->unp_addr	= PTRTOINT64(unpcb->unp_addr);
			}
			if (unpcb->unp_addr != NULL) {
				struct sockaddr_un *un = mtod(unpcb->unp_addr,
				    struct sockaddr_un *);
				memcpy(kf->unp_path, un->sun_path, un->sun_len
				    - offsetof(struct sockaddr_un,sun_path));
			}
			break;
		    }
		}
		if (locked)
			sounlock_shared(so);
		break;
	    }

	case DTYPE_PIPE: {
		struct pipe *pipe = (struct pipe *)fp->f_data;

		if (show_pointers)
			kf->pipe_peer = PTRTOINT64(pipe->pipe_peer);
		kf->pipe_state = pipe->pipe_state;
		break;
	    }

	case DTYPE_KQUEUE: {
		struct kqueue *kqi = (struct kqueue *)fp->f_data;

		kf->kq_count = kqi->kq_count;
		kf->kq_state = kqi->kq_state;
		break;
	    }
	}

	/* per-process information for KERN_FILE_BY[PU]ID */
	if (pr != NULL) {
		kf->p_pid = pr->ps_pid;
		kf->p_uid = pr->ps_ucred->cr_uid;
		kf->p_gid = pr->ps_ucred->cr_gid;
		kf->p_tid = -1;
		strlcpy(kf->p_comm, pr->ps_comm, sizeof(kf->p_comm));
	}
	if (fdp != NULL) {
		fdplock(fdp);
		kf->fd_ofileflags = fdp->fd_ofileflags[fd];
		fdpunlock(fdp);
	}
}

/*
 * Get file structures.
 */
int
sysctl_file(int *name, u_int namelen, char *where, size_t *sizep,
    struct proc *p)
{
	struct kinfo_file *kf;
	struct filedesc *fdp;
	struct file *fp;
	struct process *pr;
	size_t buflen, elem_size, elem_count, outsize;
	char *dp = where;
	int arg, i, error = 0, needed = 0, matched;
	u_int op;
	int show_pointers;

	if (namelen > 4)
		return (ENOTDIR);
	if (namelen < 4 || name[2] > sizeof(*kf))
		return (EINVAL);

	buflen = where != NULL ? *sizep : 0;
	op = name[0];
	arg = name[1];
	elem_size = name[2];
	elem_count = name[3];
	outsize = MIN(sizeof(*kf), elem_size);

	if (elem_size < 1)
		return (EINVAL);

	show_pointers = suser(curproc) == 0;

	kf = malloc(sizeof(*kf), M_TEMP, M_WAITOK);

#define FILLIT(fp, fdp, i, vp, pr) do {					\
	if (buflen >= elem_size && elem_count > 0) {			\
		fill_file(kf, fp, fdp, i, vp, pr, p, NULL,		\
		    show_pointers);					\
		error = copyout(kf, dp, outsize);			\
		if (error)						\
			break;						\
		dp += elem_size;					\
		buflen -= elem_size;					\
		elem_count--;						\
	}								\
	needed += elem_size;						\
} while (0)

#define FILLINPTABLE(table)						\
do {									\
	struct inpcb_iterator iter = { .inp_table = NULL };		\
	struct inpcb *inp = NULL;					\
	struct socket *so;						\
									\
	mtx_enter(&(table)->inpt_mtx);					\
	while ((inp = in_pcb_iterator(table, inp, &iter)) != NULL) {	\
		if (buflen >= elem_size && elem_count > 0) {		\
			mtx_leave(&(table)->inpt_mtx);			\
			NET_LOCK_SHARED();				\
			so = in_pcbsolock(inp);				\
			if (so == NULL)	{				\
				NET_UNLOCK_SHARED();			\
				mtx_enter(&(table)->inpt_mtx);		\
				continue;				\
			}						\
			fill_file(kf, NULL, NULL, 0, NULL, NULL, p,	\
			    so, show_pointers);				\
			in_pcbsounlock(inp, so);			\
			NET_UNLOCK_SHARED();				\
			error = copyout(kf, dp, outsize);		\
			mtx_enter(&(table)->inpt_mtx);			\
			if (error) {					\
				in_pcb_iterator_abort((table), inp,	\
				    &iter);				\
				break;					\
			}						\
			dp += elem_size;				\
			buflen -= elem_size;				\
			elem_count--;					\
		}							\
		needed += elem_size;					\
	}								\
	mtx_leave(&(table)->inpt_mtx);					\
} while (0)

	switch (op) {
	case KERN_FILE_BYFILE:
		/* use the inp-tables to pick up closed connections, too */
		if (arg == DTYPE_SOCKET) {
			FILLINPTABLE(&tcbtable);
#ifdef INET6
			FILLINPTABLE(&tcb6table);
#endif
			FILLINPTABLE(&udbtable);
#ifdef INET6
			FILLINPTABLE(&udb6table);
#endif
			FILLINPTABLE(&rawcbtable);
#ifdef INET6
			FILLINPTABLE(&rawin6pcbtable);
#endif
		}
		fp = NULL;
		while ((fp = fd_iterfile(fp, p)) != NULL) {
			if ((arg == 0 || fp->f_type == arg)) {
				int af, skip = 0;
				if (arg == DTYPE_SOCKET && fp->f_type == arg) {
					af = ((struct socket *)fp->f_data)->
					    so_proto->pr_domain->dom_family;
					if (af == AF_INET || af == AF_INET6)
						skip = 1;
				}
				if (!skip) {
					KERNEL_LOCK();
					FILLIT(fp, NULL, 0, NULL, NULL);
					KERNEL_UNLOCK();
				}
			}
		}
		break;
	case KERN_FILE_BYPID:
		/* A arg of -1 indicates all processes */
		if (arg < -1) {
			error = EINVAL;
			break;
		}
		matched = 0;
		KERNEL_LOCK();
		LIST_FOREACH(pr, &allprocess, ps_list) {
			/*
			 * skip system, exiting, embryonic and undead
			 * processes
			 */
			if (pr->ps_flags & (PS_SYSTEM | PS_EMBRYO | PS_EXITING))
				continue;
			if (arg >= 0 && pr->ps_pid != (pid_t)arg) {
				/* not the pid we are looking for */
				continue;
			}

			refcnt_take(&pr->ps_refcnt);

			matched = 1;
			fdp = pr->ps_fd;
			if (pr->ps_textvp)
				FILLIT(NULL, NULL, KERN_FILE_TEXT, pr->ps_textvp, pr);
			if (fdp->fd_cdir)
				FILLIT(NULL, NULL, KERN_FILE_CDIR, fdp->fd_cdir, pr);
			if (fdp->fd_rdir)
				FILLIT(NULL, NULL, KERN_FILE_RDIR, fdp->fd_rdir, pr);
			if (pr->ps_tracevp)
				FILLIT(NULL, NULL, KERN_FILE_TRACE, pr->ps_tracevp, pr);
			for (i = 0; i < fdp->fd_nfiles; i++) {
				if ((fp = fd_getfile(fdp, i)) == NULL)
					continue;
				FILLIT(fp, fdp, i, NULL, pr);
				FRELE(fp, p);
			}

			refcnt_rele_wake(&pr->ps_refcnt);

			/* pid is unique, stop searching */
			if (arg >= 0)
				break;
		}
		KERNEL_UNLOCK();
		if (!matched)
			error = ESRCH;
		break;
	case KERN_FILE_BYUID:
		KERNEL_LOCK();
		LIST_FOREACH(pr, &allprocess, ps_list) {
			/*
			 * skip system, exiting, embryonic and undead
			 * processes
			 */
			if (pr->ps_flags & (PS_SYSTEM | PS_EMBRYO | PS_EXITING))
				continue;
			if (arg >= 0 && pr->ps_ucred->cr_uid != (uid_t)arg) {
				/* not the uid we are looking for */
				continue;
			}

			refcnt_take(&pr->ps_refcnt);

			fdp = pr->ps_fd;
			if (fdp->fd_cdir)
				FILLIT(NULL, NULL, KERN_FILE_CDIR, fdp->fd_cdir, pr);
			if (fdp->fd_rdir)
				FILLIT(NULL, NULL, KERN_FILE_RDIR, fdp->fd_rdir, pr);
			if (pr->ps_tracevp)
				FILLIT(NULL, NULL, KERN_FILE_TRACE, pr->ps_tracevp, pr);
			for (i = 0; i < fdp->fd_nfiles; i++) {
				if ((fp = fd_getfile(fdp, i)) == NULL)
					continue;
				FILLIT(fp, fdp, i, NULL, pr);
				FRELE(fp, p);
			}

			refcnt_rele_wake(&pr->ps_refcnt);
		}
		KERNEL_UNLOCK();
		break;
	default:
		error = EINVAL;
		break;
	}
	free(kf, M_TEMP, sizeof(*kf));

	if (!error) {
		if (where == NULL)
			needed += KERN_FILESLOP * elem_size;
		else if (*sizep < needed)
			error = ENOMEM;
		*sizep = needed;
	}

	return (error);
}

/*
 * try over estimating by 5 procs
 */
#define KERN_PROCSLOP	5

int
sysctl_doproc(int *name, u_int namelen, char *where, size_t *sizep)
{
	struct kinfo_proc *kproc = NULL;
	struct proc *p;
	struct process *pr;
	char *dp;
	int arg, buflen, doingzomb, elem_size, elem_count;
	int error, needed, op;
	int dothreads = 0;
	int show_pointers;

	dp = where;
	buflen = where != NULL ? *sizep : 0;
	needed = error = 0;

	if (namelen != 4 || name[2] <= 0 || name[3] < 0 ||
	    name[2] > sizeof(*kproc))
		return (EINVAL);
	op = name[0];
	arg = name[1];
	elem_size = name[2];
	elem_count = name[3];

	dothreads = op & KERN_PROC_SHOW_THREADS;
	op &= ~KERN_PROC_SHOW_THREADS;

	show_pointers = suser(curproc) == 0;

	if (where != NULL)
		kproc = malloc(sizeof(*kproc), M_TEMP, M_WAITOK);

	pr = LIST_FIRST(&allprocess);
	doingzomb = 0;
again:
	for (; pr != NULL; pr = LIST_NEXT(pr, ps_list)) {
		/* XXX skip processes in the middle of being zapped */
		if (pr->ps_pgrp == NULL)
			continue;

		/*
		 * Skip embryonic processes.
		 */
		if (pr->ps_flags & PS_EMBRYO)
			continue;

		/*
		 * TODO - make more efficient (see notes below).
		 */
		switch (op) {

		case KERN_PROC_PID:
			/* could do this with just a lookup */
			if (pr->ps_pid != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_PGRP:
			/* could do this by traversing pgrp */
			if (pr->ps_pgrp->pg_id != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_SESSION:
			if (pr->ps_session->s_leader == NULL ||
			    pr->ps_session->s_leader->ps_pid != (pid_t)arg)
				continue;
			break;

		case KERN_PROC_TTY:
			if ((pr->ps_flags & PS_CONTROLT) == 0 ||
			    pr->ps_session->s_ttyp == NULL ||
			    pr->ps_session->s_ttyp->t_dev != (dev_t)arg)
				continue;
			break;

		case KERN_PROC_UID:
			if (pr->ps_ucred->cr_uid != (uid_t)arg)
				continue;
			break;

		case KERN_PROC_RUID:
			if (pr->ps_ucred->cr_ruid != (uid_t)arg)
				continue;
			break;

		case KERN_PROC_ALL:
			if (pr->ps_flags & PS_SYSTEM)
				continue;
			break;

		case KERN_PROC_KTHREAD:
			/* no filtering */
			break;

		default:
			error = EINVAL;
			goto err;
		}

		if (buflen >= elem_size && elem_count > 0) {
			fill_kproc(pr, kproc, NULL, show_pointers);
			error = copyout(kproc, dp, elem_size);
			if (error)
				goto err;
			dp += elem_size;
			buflen -= elem_size;
			elem_count--;
		}
		needed += elem_size;

		/* Skip per-thread entries if not required by op */
		if (!dothreads)
			continue;

		TAILQ_FOREACH(p, &pr->ps_threads, p_thr_link) {
			if (buflen >= elem_size && elem_count > 0) {
				fill_kproc(pr, kproc, p, show_pointers);
				error = copyout(kproc, dp, elem_size);
				if (error)
					goto err;
				dp += elem_size;
				buflen -= elem_size;
				elem_count--;
			}
			needed += elem_size;
		}
	}
	if (doingzomb == 0) {
		pr = LIST_FIRST(&zombprocess);
		doingzomb++;
		goto again;
	}
	if (where != NULL) {
		*sizep = dp - where;
		if (needed > *sizep) {
			error = ENOMEM;
			goto err;
		}
	} else {
		needed += KERN_PROCSLOP * elem_size;
		*sizep = needed;
	}
err:
	if (kproc)
		free(kproc, M_TEMP, sizeof(*kproc));
	return (error);
}

/*
 * Fill in a kproc structure for the specified process.
 */
void
fill_kproc(struct process *pr, struct kinfo_proc *ki, struct proc *p,
    int show_pointers)
{
	struct session *s = pr->ps_session;
	struct tty *tp;
	struct vmspace *vm = pr->ps_vmspace;
	struct timespec booted, st, ut, utc;
	struct tusage tu;
	int isthread;

	isthread = p != NULL;
	if (!isthread) {
		p = pr->ps_mainproc;		/* XXX */
		tuagg_get_process(&tu, pr);
	} else
		tuagg_get_proc(&tu, p);

	FILL_KPROC(ki, strlcpy, p, pr, pr->ps_ucred, pr->ps_pgrp,
	    p, pr, s, vm, pr->ps_limit, pr->ps_sigacts, &tu, isthread,
	    show_pointers);

	/* stuff that's too painful to generalize into the macros */
	if (s->s_leader)
		ki->p_sid = s->s_leader->ps_pid;

	if ((pr->ps_flags & PS_CONTROLT) && (tp = s->s_ttyp)) {
		ki->p_tdev = tp->t_dev;
		ki->p_tpgid = tp->t_pgrp ? tp->t_pgrp->pg_id : -1;
		if (show_pointers)
			ki->p_tsess = PTRTOINT64(tp->t_session);
	} else {
		ki->p_tdev = NODEV;
		ki->p_tpgid = -1;
	}

	/* fixups that can only be done in the kernel */
	if ((pr->ps_flags & PS_ZOMBIE) == 0) {
		if ((pr->ps_flags & PS_EMBRYO) == 0 && vm != NULL)
			ki->p_vm_rssize = vm_resident_count(vm);
		calctsru(&tu, &ut, &st, NULL);
		ki->p_uutime_sec = ut.tv_sec;
		ki->p_uutime_usec = ut.tv_nsec/1000;
		ki->p_ustime_sec = st.tv_sec;
		ki->p_ustime_usec = st.tv_nsec/1000;

		/* Convert starting uptime to a starting UTC time. */
		nanoboottime(&booted);
		timespecadd(&booted, &pr->ps_start, &utc);
		ki->p_ustart_sec = utc.tv_sec;
		ki->p_ustart_usec = utc.tv_nsec / 1000;

#ifdef MULTIPROCESSOR
		if (p->p_cpu != NULL)
			ki->p_cpuid = CPU_INFO_UNIT(p->p_cpu);
#endif
	}

	/* get %cpu and schedule state: just one thread or sum of all? */
	if (isthread) {
		ki->p_pctcpu = p->p_pctcpu;
		ki->p_stat   = p->p_stat;
	} else {
		ki->p_pctcpu = 0;
		ki->p_stat = (pr->ps_flags & PS_ZOMBIE) ? SDEAD : SIDL;
		TAILQ_FOREACH(p, &pr->ps_threads, p_thr_link) {
			ki->p_pctcpu += p->p_pctcpu;
			/* find best state: ONPROC > RUN > STOP > SLEEP > .. */
			if (p->p_stat == SONPROC || ki->p_stat == SONPROC)
				ki->p_stat = SONPROC;
			else if (p->p_stat == SRUN || ki->p_stat == SRUN)
				ki->p_stat = SRUN;
			else if (p->p_stat == SSTOP || ki->p_stat == SSTOP)
				ki->p_stat = SSTOP;
			else if (p->p_stat == SSLEEP)
				ki->p_stat = SSLEEP;
		}
	}
}

int
sysctl_proc_args(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    struct proc *cp)
{
	struct process *vpr;
	pid_t pid;
	struct ps_strings pss;
	struct iovec iov;
	struct uio uio;
	int error, cnt, op;
	size_t limit;
	char **rargv, **vargv;		/* reader vs. victim */
	char *rarg, *varg, *buf;
	struct vmspace *vm;
	vaddr_t ps_strings;

	if (namelen > 2)
		return (ENOTDIR);
	if (namelen < 2)
		return (EINVAL);

	pid = name[0];
	op = name[1];

	switch (op) {
	case KERN_PROC_ARGV:
	case KERN_PROC_NARGV:
	case KERN_PROC_ENV:
	case KERN_PROC_NENV:
		break;
	default:
		return (EOPNOTSUPP);
	}

	if ((vpr = prfind(pid)) == NULL)
		return (ESRCH);

	if (oldp == NULL) {
		if (op == KERN_PROC_NARGV || op == KERN_PROC_NENV)
			*oldlenp = sizeof(int);
		else
			*oldlenp = ARG_MAX;	/* XXX XXX XXX */
		return (0);
	}

	/* Either system process or exiting/zombie */
	if (vpr->ps_flags & (PS_SYSTEM | PS_EXITING))
		return (EINVAL);

	/* Execing - danger. */
	if ((vpr->ps_flags & PS_INEXEC))
		return (EBUSY);

	/* Only owner or root can get env */
	if ((op == KERN_PROC_NENV || op == KERN_PROC_ENV) &&
	    (vpr->ps_ucred->cr_uid != cp->p_ucred->cr_uid &&
	    (error = suser(cp)) != 0))
		return (error);

	ps_strings = vpr->ps_strings;
	vm = vpr->ps_vmspace;
	uvmspace_addref(vm);
	vpr = NULL;

	buf = malloc(PAGE_SIZE, M_TEMP, M_WAITOK);

	iov.iov_base = &pss;
	iov.iov_len = sizeof(pss);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = (off_t)ps_strings;
	uio.uio_resid = sizeof(pss);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_procp = cp;

	if ((error = uvm_io(&vm->vm_map, &uio, 0)) != 0)
		goto out;

	if (op == KERN_PROC_NARGV) {
		error = sysctl_rdint(oldp, oldlenp, NULL, pss.ps_nargvstr);
		goto out;
	}
	if (op == KERN_PROC_NENV) {
		error = sysctl_rdint(oldp, oldlenp, NULL, pss.ps_nenvstr);
		goto out;
	}

	if (op == KERN_PROC_ARGV) {
		cnt = pss.ps_nargvstr;
		vargv = pss.ps_argvstr;
	} else {
		cnt = pss.ps_nenvstr;
		vargv = pss.ps_envstr;
	}

	/* -1 to have space for a terminating NUL */
	limit = *oldlenp - 1;
	*oldlenp = 0;

	rargv = oldp;

	/*
	 * *oldlenp - number of bytes copied out into readers buffer.
	 * limit - maximal number of bytes allowed into readers buffer.
	 * rarg - pointer into readers buffer where next arg will be stored.
	 * rargv - pointer into readers buffer where the next rarg pointer
	 *  will be stored.
	 * vargv - pointer into victim address space where the next argument
	 *  will be read.
	 */

	/* space for cnt pointers and a NULL */
	rarg = (char *)(rargv + cnt + 1);
	*oldlenp += (cnt + 1) * sizeof(char **);

	while (cnt > 0 && *oldlenp < limit) {
		size_t len, vstrlen;

		/* Write to readers argv */
		if ((error = copyout(&rarg, rargv, sizeof(rarg))) != 0)
			goto out;

		/* read the victim argv */
		iov.iov_base = &varg;
		iov.iov_len = sizeof(varg);
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_offset = (off_t)(vaddr_t)vargv;
		uio.uio_resid = sizeof(varg);
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_rw = UIO_READ;
		uio.uio_procp = cp;
		if ((error = uvm_io(&vm->vm_map, &uio, 0)) != 0)
			goto out;

		if (varg == NULL)
			break;

		/*
		 * read the victim arg. We must jump through hoops to avoid
		 * crossing a page boundary too much and returning an error.
		 */
more:
		len = PAGE_SIZE - (((vaddr_t)varg) & PAGE_MASK);
		/* leave space for the terminating NUL */
		iov.iov_base = buf;
		iov.iov_len = len;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_offset = (off_t)(vaddr_t)varg;
		uio.uio_resid = len;
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_rw = UIO_READ;
		uio.uio_procp = cp;
		if ((error = uvm_io(&vm->vm_map, &uio, 0)) != 0)
			goto out;

		for (vstrlen = 0; vstrlen < len; vstrlen++) {
			if (buf[vstrlen] == '\0')
				break;
		}

		/* Don't overflow readers buffer. */
		if (*oldlenp + vstrlen + 1 >= limit) {
			error = ENOMEM;
			goto out;
		}

		if ((error = copyout(buf, rarg, vstrlen)) != 0)
			goto out;

		*oldlenp += vstrlen;
		rarg += vstrlen;

		/* The string didn't end in this page? */
		if (vstrlen == len) {
			varg += vstrlen;
			goto more;
		}

		/* End of string. Terminate it with a NUL */
		buf[0] = '\0';
		if ((error = copyout(buf, rarg, 1)) != 0)
			goto out;
		*oldlenp += 1;
		rarg += 1;

		vargv++;
		rargv++;
		cnt--;
	}

	if (*oldlenp >= limit) {
		error = ENOMEM;
		goto out;
	}

	/* Write the terminating null */
	rarg = NULL;
	error = copyout(&rarg, rargv, sizeof(rarg));

out:
	uvmspace_free(vm);
	free(buf, M_TEMP, PAGE_SIZE);
	return (error);
}

int
sysctl_proc_cwd(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    struct proc *cp)
{
	struct process *findpr;
	struct vnode *vp;
	pid_t pid;
	int error;
	size_t lenused, len;
	char *path, *bp, *bend;

	if (namelen > 1)
		return (ENOTDIR);
	if (namelen < 1)
		return (EINVAL);

	pid = name[0];
	if ((findpr = prfind(pid)) == NULL)
		return (ESRCH);

	if (oldp == NULL) {
		*oldlenp = MAXPATHLEN * 4;
		return (0);
	}

	/* Either system process or exiting/zombie */
	if (findpr->ps_flags & (PS_SYSTEM | PS_EXITING))
		return (EINVAL);

	/* Only owner or root can get cwd */
	if (findpr->ps_ucred->cr_uid != cp->p_ucred->cr_uid &&
	    (error = suser(cp)) != 0)
		return (error);

	len = *oldlenp;
	if (len > MAXPATHLEN * 4)
		len = MAXPATHLEN * 4;
	else if (len < 2)
		return (ERANGE);
	*oldlenp = 0;

	/* snag a reference to the vnode before we can sleep */
	vp = findpr->ps_fd->fd_cdir;
	vref(vp);

	path = malloc(len, M_TEMP, M_WAITOK);

	bp = &path[len];
	bend = bp;
	*(--bp) = '\0';

	/* Same as sys__getcwd */
	error = vfs_getcwd_common(vp, NULL,
	    &bp, path, len / 2, GETCWD_CHECK_ACCESS, cp);
	if (error == 0) {
		*oldlenp = lenused = bend - bp;
		error = copyout(bp, oldp, lenused);
	}

	vrele(vp);
	free(path, M_TEMP, len);

	return (error);
}

int
sysctl_proc_nobroadcastkill(int *name, u_int namelen, void *newp, size_t newlen,
    void *oldp, size_t *oldlenp, struct proc *cp)
{
	struct process *findpr;
	pid_t pid;
	int error, flag;

	if (namelen > 1)
		return (ENOTDIR);
	if (namelen < 1)
		return (EINVAL);

	pid = name[0];
	if ((findpr = prfind(pid)) == NULL)
		return (ESRCH);

	/* Either system process or exiting/zombie */
	if (findpr->ps_flags & (PS_SYSTEM | PS_EXITING))
		return (EINVAL);

	/* Only root can change PS_NOBROADCASTKILL */
	if (newp != NULL && (error = suser(cp)) != 0)
		return (error);

	/* get the PS_NOBROADCASTKILL flag */
	flag = findpr->ps_flags & PS_NOBROADCASTKILL ? 1 : 0;

	error = sysctl_int(oldp, oldlenp, newp, newlen, &flag);
	if (error == 0 && newp) {
		if (flag)
			atomic_setbits_int(&findpr->ps_flags,
			    PS_NOBROADCASTKILL);
		else
			atomic_clearbits_int(&findpr->ps_flags,
			    PS_NOBROADCASTKILL);
	}

	return (error);
}

/* Arbitrary but reasonable limit for one iteration. */
#define	VMMAP_MAXLEN	MAXPHYS

int
sysctl_proc_vmmap(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    struct proc *cp)
{
	struct process *findpr;
	pid_t pid;
	int error;
	size_t oldlen, len;
	struct kinfo_vmentry *kve, *ukve;
	u_long *ustart, start;

	if (namelen > 1)
		return (ENOTDIR);
	if (namelen < 1)
		return (EINVAL);

	/* Provide max buffer length as hint. */
	if (oldp == NULL) {
		if (oldlenp == NULL)
			return (EINVAL);
		else {
			*oldlenp = VMMAP_MAXLEN;
			return (0);
		}
	}

	pid = name[0];
	if (pid == cp->p_p->ps_pid) {
		/* Self process mapping. */
		findpr = cp->p_p;
	} else if (pid > 0) {
		if ((findpr = prfind(pid)) == NULL)
			return (ESRCH);

		/* Either system process or exiting/zombie */
		if (findpr->ps_flags & (PS_SYSTEM | PS_EXITING))
			return (EINVAL);

#if 1
		/* XXX Allow only root for now */
		if ((error = suser(cp)) != 0)
			return (error);
#else
		/* Only owner or root can get vmmap */
		if (findpr->ps_ucred->cr_uid != cp->p_ucred->cr_uid &&
		    (error = suser(cp)) != 0)
			return (error);
#endif
	} else {
		/* Only root can get kernel_map */
		if ((error = suser(cp)) != 0)
			return (error);
		findpr = NULL;
	}

	/* Check the given size. */
	oldlen = *oldlenp;
	if (oldlen == 0 || oldlen % sizeof(*kve) != 0)
		return (EINVAL);

	/* Deny huge allocation. */
	if (oldlen > VMMAP_MAXLEN)
		return (EINVAL);

	/*
	 * Iterate from the given address passed as the first element's
	 * kve_start via oldp.
	 */
	ukve = (struct kinfo_vmentry *)oldp;
	ustart = &ukve->kve_start;
	error = copyin(ustart, &start, sizeof(start));
	if (error != 0)
		return (error);

	/* Allocate wired memory to not block. */
	kve = malloc(oldlen, M_TEMP, M_WAITOK);

	/* Set the base address and read entries. */
	kve[0].kve_start = start;
	len = oldlen;
	error = fill_vmmap(findpr, kve, &len);
	if (error != 0 && error != ENOMEM)
		goto done;
	if (len == 0)
		goto done;

	KASSERT(len <= oldlen);
	KASSERT((len % sizeof(struct kinfo_vmentry)) == 0);

	error = copyout(kve, oldp, len);

done:
	*oldlenp = len;

	free(kve, M_TEMP, oldlen);

	return (error);
}
#endif /* SMALL_KERNEL */

/*
 * Initialize disknames/diskstats for export by sysctl. If update is set,
 * then we simply update the disk statistics information.
 */
int
sysctl_diskinit(int update, struct proc *p)
{
	struct diskstats *sdk;
	struct disk *dk;
	const char *duid;
	int error, changed = 0;

	KERNEL_ASSERT_LOCKED();

	if ((error = rw_enter(&sysctl_disklock, RW_WRITE|RW_INTR)) != 0)
		return error;

	/* Run in a loop, disks may change while malloc sleeps. */
	while (disk_change) {
		int tlen, count;

		disk_change = 0;

		tlen = 0;
		TAILQ_FOREACH(dk, &disklist, dk_link) {
			if (dk->dk_name)
				tlen += strlen(dk->dk_name);
			tlen += 18;	/* label uid + separators */
		}
		tlen++;
		/* disk_count may change when malloc sleeps */
		count = disk_count;

		/*
		 * The sysctl_disklock ensures that no other process can
		 * allocate disknames and diskstats while our malloc sleeps.
		 */
		free(disknames, M_SYSCTL, disknameslen);
		free(diskstats, M_SYSCTL, diskstatslen);
		diskstats = NULL;
		disknames = NULL;
		diskstats = mallocarray(count, sizeof(struct diskstats),
		    M_SYSCTL, M_WAITOK|M_ZERO);
		diskstatslen = count * sizeof(struct diskstats);
		disknames = malloc(tlen, M_SYSCTL, M_WAITOK|M_ZERO);
		disknameslen = tlen;
		disknames[0] = '\0';
		changed = 1;
	}

	if (changed) {
		int l;

		l = 0;
		sdk = diskstats;
		TAILQ_FOREACH(dk, &disklist, dk_link) {
			duid = NULL;
			if (dk->dk_label && !duid_iszero(dk->dk_label->d_uid))
				duid = duid_format(dk->dk_label->d_uid);
			snprintf(disknames + l, disknameslen - l, "%s:%s,",
			    dk->dk_name ? dk->dk_name : "",
			    duid ? duid : "");
			l += strlen(disknames + l);
			strlcpy(sdk->ds_name, dk->dk_name,
			    sizeof(sdk->ds_name));
			mtx_enter(&dk->dk_mtx);
			sdk->ds_busy = dk->dk_busy;
			sdk->ds_rxfer = dk->dk_rxfer;
			sdk->ds_wxfer = dk->dk_wxfer;
			sdk->ds_seek = dk->dk_seek;
			sdk->ds_rbytes = dk->dk_rbytes;
			sdk->ds_wbytes = dk->dk_wbytes;
			sdk->ds_attachtime = dk->dk_attachtime;
			sdk->ds_timestamp = dk->dk_timestamp;
			sdk->ds_time = dk->dk_time;
			mtx_leave(&dk->dk_mtx);
			sdk++;
		}

		/* Eliminate trailing comma */
		if (l != 0)
			disknames[l - 1] = '\0';
	} else if (update) {
		/* Just update, number of drives hasn't changed */
		sdk = diskstats;
		TAILQ_FOREACH(dk, &disklist, dk_link) {
			strlcpy(sdk->ds_name, dk->dk_name,
			    sizeof(sdk->ds_name));
			mtx_enter(&dk->dk_mtx);
			sdk->ds_busy = dk->dk_busy;
			sdk->ds_rxfer = dk->dk_rxfer;
			sdk->ds_wxfer = dk->dk_wxfer;
			sdk->ds_seek = dk->dk_seek;
			sdk->ds_rbytes = dk->dk_rbytes;
			sdk->ds_wbytes = dk->dk_wbytes;
			sdk->ds_attachtime = dk->dk_attachtime;
			sdk->ds_timestamp = dk->dk_timestamp;
			sdk->ds_time = dk->dk_time;
			mtx_leave(&dk->dk_mtx);
			sdk++;
		}
	}
	rw_exit_write(&sysctl_disklock);
	return 0;
}

#if defined(SYSVMSG) || defined(SYSVSEM) || defined(SYSVSHM)
int
sysctl_sysvipc(int *name, u_int namelen, void *where, size_t *sizep)
{
#ifdef SYSVSEM
	struct sem_sysctl_info *semsi;
#endif
#ifdef SYSVSHM
	struct shm_sysctl_info *shmsi;
#endif
	size_t infosize, dssize, tsize, buflen, bufsiz;
	int i, nds, error, ret;
	void *buf;

	if (namelen != 1)
		return (EINVAL);

	buflen = *sizep;

	switch (*name) {
	case KERN_SYSVIPC_MSG_INFO:
#ifdef SYSVMSG
		return (sysctl_sysvmsg(name, namelen, where, sizep));
#else
		return (EOPNOTSUPP);
#endif
	case KERN_SYSVIPC_SEM_INFO:
#ifdef SYSVSEM
		infosize = sizeof(semsi->seminfo);
		nds = seminfo.semmni;
		dssize = sizeof(semsi->semids[0]);
		break;
#else
		return (EOPNOTSUPP);
#endif
	case KERN_SYSVIPC_SHM_INFO:
#ifdef SYSVSHM
		infosize = sizeof(shmsi->shminfo);
		nds = shminfo.shmmni;
		dssize = sizeof(shmsi->shmids[0]);
		break;
#else
		return (EOPNOTSUPP);
#endif
	default:
		return (EINVAL);
	}
	tsize = infosize + (nds * dssize);

	/* Return just the total size required. */
	if (where == NULL) {
		*sizep = tsize;
		return (0);
	}

	/* Not enough room for even the info struct. */
	if (buflen < infosize) {
		*sizep = 0;
		return (ENOMEM);
	}
	bufsiz = min(tsize, buflen);
	buf = malloc(bufsiz, M_TEMP, M_WAITOK|M_ZERO);

	switch (*name) {
#ifdef SYSVSEM
	case KERN_SYSVIPC_SEM_INFO:
		semsi = (struct sem_sysctl_info *)buf;
		semsi->seminfo = seminfo;
		break;
#endif
#ifdef SYSVSHM
	case KERN_SYSVIPC_SHM_INFO:
		shmsi = (struct shm_sysctl_info *)buf;
		shmsi->shminfo = shminfo;
		break;
#endif
	}
	buflen -= infosize;

	ret = 0;
	if (buflen > 0) {
		/* Fill in the IPC data structures.  */
		for (i = 0; i < nds; i++) {
			if (buflen < dssize) {
				ret = ENOMEM;
				break;
			}
			switch (*name) {
#ifdef SYSVSEM
			case KERN_SYSVIPC_SEM_INFO:
				if (sema[i] != NULL)
					memcpy(&semsi->semids[i], sema[i],
					    dssize);
				else
					memset(&semsi->semids[i], 0, dssize);
				break;
#endif
#ifdef SYSVSHM
			case KERN_SYSVIPC_SHM_INFO:
				if (shmsegs[i] != NULL)
					memcpy(&shmsi->shmids[i], shmsegs[i],
					    dssize);
				else
					memset(&shmsi->shmids[i], 0, dssize);
				break;
#endif
			}
			buflen -= dssize;
		}
	}
	*sizep -= buflen;
	error = copyout(buf, where, *sizep);
	free(buf, M_TEMP, bufsiz);
	/* If copyout succeeded, use return code set earlier. */
	return (error ? error : ret);
}
#endif /* SYSVMSG || SYSVSEM || SYSVSHM */

#ifndef	SMALL_KERNEL
int
sysctl_intrcnt(int *name, u_int namelen, void *oldp, size_t *oldlenp)
{
	return (evcount_sysctl(name, namelen, oldp, oldlenp, NULL, 0));
}

int
sysctl_sensors(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	union {
		struct sensor us;
		struct sensordev usd;
	} buf;

	struct sensor *us = (struct sensor *)&buf;
	struct sensordev *usd = (struct sensordev *)&buf;
	struct ksensor *ks ;
	struct ksensordev *ksd;
	int dev, numt, ret;
	enum sensor_type type;

	if (namelen != 1 && namelen != 3)
		return (ENOTDIR);

	dev = name[0];
	if (namelen == 1) {
		KERNEL_LOCK();
		ret = sensordev_get(dev, &ksd);
		if (ret) {
			KERNEL_UNLOCK();
			return (ret);
		}

		/* Grab a copy, to clear the kernel pointers */
		memset(usd, 0, sizeof(*usd));
		usd->num = ksd->num;
		strlcpy(usd->xname, ksd->xname, sizeof(usd->xname));
		memcpy(usd->maxnumt, ksd->maxnumt, sizeof(usd->maxnumt));
		usd->sensors_count = ksd->sensors_count;
		KERNEL_UNLOCK();

		ret = sysctl_rdstruct(oldp, oldlenp, newp, usd, sizeof(*usd));

		return (ret);
	}

	type = name[1];
	numt = name[2];

	KERNEL_LOCK();
	ret = sensor_find(dev, type, numt, &ks);
	if (ret) {
		KERNEL_UNLOCK();
		return (ret);
	}

	/* Grab a copy, to clear the kernel pointers */
	memset(us, 0, sizeof(*us));
	memcpy(us->desc, ks->desc, sizeof(us->desc));
	us->tv = ks->tv;
	us->value = ks->value;
	us->type = ks->type;
	us->status = ks->status;
	us->numt = ks->numt;
	us->flags = ks->flags;
	KERNEL_UNLOCK();

	ret = sysctl_rdstruct(oldp, oldlenp, newp, us, sizeof(*us));

	return (ret);
}

int
sysctl_cpustats(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	CPU_INFO_ITERATOR cii;
	struct cpustats cs;
	struct cpu_info *ci;
	int found = 0;

	if (namelen != 1)
		return (ENOTDIR);

	CPU_INFO_FOREACH(cii, ci) {
		if (name[0] == CPU_INFO_UNIT(ci)) {
			found = 1;
			break;
		}
	}
	if (!found)
		return (ENOENT);

	memset(&cs, 0, sizeof cs);
	sysctl_ci_cp_time(ci, cs.cs_time);
	cs.cs_flags = 0;
	if (cpu_is_online(ci))
		cs.cs_flags |= CPUSTATS_ONLINE;

	return (sysctl_rdstruct(oldp, oldlenp, newp, &cs, sizeof(cs)));
}

static void
sysctl_ci_cp_time(struct cpu_info *ci, uint64_t *cp_time)
{
	struct schedstate_percpu *spc = &ci->ci_schedstate;
	unsigned int gen;

	pc_cons_enter(&spc->spc_cp_time_lock, &gen);
	do {
		int i;
		for (i = 0; i < CPUSTATES; i++)
			cp_time[i] = spc->spc_cp_time[i];
	} while (pc_cons_leave(&spc->spc_cp_time_lock, &gen) != 0);
}

int
sysctl_cptime2(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	uint64_t cp_time[CPUSTATES];
	int found = 0;

	if (namelen != 1)
		return (ENOTDIR);

	CPU_INFO_FOREACH(cii, ci) {
		if (name[0] == CPU_INFO_UNIT(ci)) {
			found = 1;
			break;
		}
	}
	if (!found)
		return (ENOENT);

	sysctl_ci_cp_time(ci, cp_time);

	return (sysctl_rdstruct(oldp, oldlenp, newp,
	    cp_time, sizeof(cp_time)));
}
#endif	/* SMALL_KERNEL */

#if NAUDIO > 0
int
sysctl_audio(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	int *intptr;

	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case KERN_AUDIO_RECORD:
		intptr = &audio_record_enable;
		break;
#if NWSKBD > 0
	case KERN_AUDIO_KBDCONTROL:
		intptr = &audio_kbdcontrol_enable;
		break;
#endif
	default:
		return (ENOENT);
	}
	return (sysctl_int(oldp, oldlenp, newp, newlen, intptr));
}
#endif

#if NVIDEO > 0
int
sysctl_video(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	if (namelen != 1)
		return (ENOTDIR);

	if (name[0] != KERN_VIDEO_RECORD)
		return (ENOENT);

	return (sysctl_int(oldp, oldlenp, newp, newlen, &video_record_enable));
}
#endif

int
sysctl_utc_offset(void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
	struct timespec adjusted, now;
	int adjustment_seconds, error, new_offset_minutes, old_offset_minutes;

	old_offset_minutes = utc_offset / 60;	/* seconds -> minutes */
	new_offset_minutes = old_offset_minutes;
	error = sysctl_securelevel_int(oldp, oldlenp, newp, newlen,
	     &new_offset_minutes);
	if (error)
		return error;
	if (new_offset_minutes < -24 * 60 || new_offset_minutes > 24 * 60)
		return EINVAL;
	if (new_offset_minutes == old_offset_minutes)
		return 0;

	utc_offset = new_offset_minutes * 60;	/* minutes -> seconds */
	adjustment_seconds = (new_offset_minutes - old_offset_minutes) * 60;

	nanotime(&now);
	adjusted = now;
	adjusted.tv_sec -= adjustment_seconds;
	tc_setrealtimeclock(&adjusted);
	resettodr();

	return 0;
}
