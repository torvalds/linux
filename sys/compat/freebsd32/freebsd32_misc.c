/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Doug Rabson
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

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ktrace.h"

#define __ELF_WORD_SIZE 32

#ifdef COMPAT_FREEBSD11
#define	_WANT_FREEBSD11_KEVENT
#endif

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/capsicum.h>
#include <sys/clock.h>
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/filedesc.h>
#include <sys/imgact.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/file.h>		/* Must come after sys/malloc.h */
#include <sys/imgact.h>
#include <sys/mbuf.h>
#include <sys/mman.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/procctl.h>
#include <sys/reboot.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/selinfo.h>
#include <sys/eventvar.h>	/* Must come after sys/selinfo.h */
#include <sys/pipe.h>		/* Must come after sys/selinfo.h */
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/thr.h>
#include <sys/unistd.h>
#include <sys/ucontext.h>
#include <sys/vnode.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#ifdef INET
#include <netinet/in.h>
#endif

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>

#include <machine/cpu.h>
#include <machine/elf.h>
#ifdef __amd64__
#include <machine/md_var.h>
#endif

#include <security/audit/audit.h>

#include <compat/freebsd32/freebsd32_util.h>
#include <compat/freebsd32/freebsd32.h>
#include <compat/freebsd32/freebsd32_ipc.h>
#include <compat/freebsd32/freebsd32_misc.h>
#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_proto.h>

FEATURE(compat_freebsd_32bit, "Compatible with 32-bit FreeBSD");

#ifdef __amd64__
CTASSERT(sizeof(struct timeval32) == 8);
CTASSERT(sizeof(struct timespec32) == 8);
CTASSERT(sizeof(struct itimerval32) == 16);
CTASSERT(sizeof(struct bintime32) == 12);
#endif
CTASSERT(sizeof(struct statfs32) == 256);
#ifdef __amd64__
CTASSERT(sizeof(struct rusage32) == 72);
#endif
CTASSERT(sizeof(struct sigaltstack32) == 12);
#ifdef __amd64__
CTASSERT(sizeof(struct kevent32) == 56);
#else
CTASSERT(sizeof(struct kevent32) == 64);
#endif
CTASSERT(sizeof(struct iovec32) == 8);
CTASSERT(sizeof(struct msghdr32) == 28);
#ifdef __amd64__
CTASSERT(sizeof(struct stat32) == 208);
CTASSERT(sizeof(struct freebsd11_stat32) == 96);
#endif
CTASSERT(sizeof(struct sigaction32) == 24);

static int freebsd32_kevent_copyout(void *arg, struct kevent *kevp, int count);
static int freebsd32_kevent_copyin(void *arg, struct kevent *kevp, int count);
static int freebsd32_user_clock_nanosleep(struct thread *td, clockid_t clock_id,
    int flags, const struct timespec32 *ua_rqtp, struct timespec32 *ua_rmtp);

void
freebsd32_rusage_out(const struct rusage *s, struct rusage32 *s32)
{

	TV_CP(*s, *s32, ru_utime);
	TV_CP(*s, *s32, ru_stime);
	CP(*s, *s32, ru_maxrss);
	CP(*s, *s32, ru_ixrss);
	CP(*s, *s32, ru_idrss);
	CP(*s, *s32, ru_isrss);
	CP(*s, *s32, ru_minflt);
	CP(*s, *s32, ru_majflt);
	CP(*s, *s32, ru_nswap);
	CP(*s, *s32, ru_inblock);
	CP(*s, *s32, ru_oublock);
	CP(*s, *s32, ru_msgsnd);
	CP(*s, *s32, ru_msgrcv);
	CP(*s, *s32, ru_nsignals);
	CP(*s, *s32, ru_nvcsw);
	CP(*s, *s32, ru_nivcsw);
}

int
freebsd32_wait4(struct thread *td, struct freebsd32_wait4_args *uap)
{
	int error, status;
	struct rusage32 ru32;
	struct rusage ru, *rup;

	if (uap->rusage != NULL)
		rup = &ru;
	else
		rup = NULL;
	error = kern_wait(td, uap->pid, &status, uap->options, rup);
	if (error)
		return (error);
	if (uap->status != NULL)
		error = copyout(&status, uap->status, sizeof(status));
	if (uap->rusage != NULL && error == 0) {
		freebsd32_rusage_out(&ru, &ru32);
		error = copyout(&ru32, uap->rusage, sizeof(ru32));
	}
	return (error);
}

int
freebsd32_wait6(struct thread *td, struct freebsd32_wait6_args *uap)
{
	struct wrusage32 wru32;
	struct __wrusage wru, *wrup;
	struct siginfo32 si32;
	struct __siginfo si, *sip;
	int error, status;

	if (uap->wrusage != NULL)
		wrup = &wru;
	else
		wrup = NULL;
	if (uap->info != NULL) {
		sip = &si;
		bzero(sip, sizeof(*sip));
	} else
		sip = NULL;
	error = kern_wait6(td, uap->idtype, PAIR32TO64(id_t, uap->id),
	    &status, uap->options, wrup, sip);
	if (error != 0)
		return (error);
	if (uap->status != NULL)
		error = copyout(&status, uap->status, sizeof(status));
	if (uap->wrusage != NULL && error == 0) {
		freebsd32_rusage_out(&wru.wru_self, &wru32.wru_self);
		freebsd32_rusage_out(&wru.wru_children, &wru32.wru_children);
		error = copyout(&wru32, uap->wrusage, sizeof(wru32));
	}
	if (uap->info != NULL && error == 0) {
		siginfo_to_siginfo32 (&si, &si32);
		error = copyout(&si32, uap->info, sizeof(si32));
	}
	return (error);
}

#ifdef COMPAT_FREEBSD4
static void
copy_statfs(struct statfs *in, struct statfs32 *out)
{

	statfs_scale_blocks(in, INT32_MAX);
	bzero(out, sizeof(*out));
	CP(*in, *out, f_bsize);
	out->f_iosize = MIN(in->f_iosize, INT32_MAX);
	CP(*in, *out, f_blocks);
	CP(*in, *out, f_bfree);
	CP(*in, *out, f_bavail);
	out->f_files = MIN(in->f_files, INT32_MAX);
	out->f_ffree = MIN(in->f_ffree, INT32_MAX);
	CP(*in, *out, f_fsid);
	CP(*in, *out, f_owner);
	CP(*in, *out, f_type);
	CP(*in, *out, f_flags);
	out->f_syncwrites = MIN(in->f_syncwrites, INT32_MAX);
	out->f_asyncwrites = MIN(in->f_asyncwrites, INT32_MAX);
	strlcpy(out->f_fstypename,
	      in->f_fstypename, MFSNAMELEN);
	strlcpy(out->f_mntonname,
	      in->f_mntonname, min(MNAMELEN, FREEBSD4_MNAMELEN));
	out->f_syncreads = MIN(in->f_syncreads, INT32_MAX);
	out->f_asyncreads = MIN(in->f_asyncreads, INT32_MAX);
	strlcpy(out->f_mntfromname,
	      in->f_mntfromname, min(MNAMELEN, FREEBSD4_MNAMELEN));
}
#endif

#ifdef COMPAT_FREEBSD4
int
freebsd4_freebsd32_getfsstat(struct thread *td,
    struct freebsd4_freebsd32_getfsstat_args *uap)
{
	struct statfs *buf, *sp;
	struct statfs32 stat32;
	size_t count, size, copycount;
	int error;

	count = uap->bufsize / sizeof(struct statfs32);
	size = count * sizeof(struct statfs);
	error = kern_getfsstat(td, &buf, size, &count, UIO_SYSSPACE, uap->mode);
	if (size > 0) {
		sp = buf;
		copycount = count;
		while (copycount > 0 && error == 0) {
			copy_statfs(sp, &stat32);
			error = copyout(&stat32, uap->buf, sizeof(stat32));
			sp++;
			uap->buf++;
			copycount--;
		}
		free(buf, M_STATFS);
	}
	if (error == 0)
		td->td_retval[0] = count;
	return (error);
}
#endif

#ifdef COMPAT_FREEBSD10
int
freebsd10_freebsd32_pipe(struct thread *td,
    struct freebsd10_freebsd32_pipe_args *uap) {
	
	return (freebsd10_pipe(td, (struct freebsd10_pipe_args*)uap));
}
#endif

int
freebsd32_sigaltstack(struct thread *td,
		      struct freebsd32_sigaltstack_args *uap)
{
	struct sigaltstack32 s32;
	struct sigaltstack ss, oss, *ssp;
	int error;

	if (uap->ss != NULL) {
		error = copyin(uap->ss, &s32, sizeof(s32));
		if (error)
			return (error);
		PTRIN_CP(s32, ss, ss_sp);
		CP(s32, ss, ss_size);
		CP(s32, ss, ss_flags);
		ssp = &ss;
	} else
		ssp = NULL;
	error = kern_sigaltstack(td, ssp, &oss);
	if (error == 0 && uap->oss != NULL) {
		PTROUT_CP(oss, s32, ss_sp);
		CP(oss, s32, ss_size);
		CP(oss, s32, ss_flags);
		error = copyout(&s32, uap->oss, sizeof(s32));
	}
	return (error);
}

/*
 * Custom version of exec_copyin_args() so that we can translate
 * the pointers.
 */
int
freebsd32_exec_copyin_args(struct image_args *args, const char *fname,
    enum uio_seg segflg, u_int32_t *argv, u_int32_t *envv)
{
	char *argp, *envp;
	u_int32_t *p32, arg;
	int error;

	bzero(args, sizeof(*args));
	if (argv == NULL)
		return (EFAULT);

	/*
	 * Allocate demand-paged memory for the file name, argument, and
	 * environment strings.
	 */
	error = exec_alloc_args(args);
	if (error != 0)
		return (error);

	/*
	 * Copy the file name.
	 */
	error = exec_args_add_fname(args, fname, segflg);
	if (error != 0)
		goto err_exit;

	/*
	 * extract arguments first
	 */
	p32 = argv;
	for (;;) {
		error = copyin(p32++, &arg, sizeof(arg));
		if (error)
			goto err_exit;
		if (arg == 0)
			break;
		argp = PTRIN(arg);
		error = exec_args_add_arg(args, argp, UIO_USERSPACE);
		if (error != 0)
			goto err_exit;
	}
			
	/*
	 * extract environment strings
	 */
	if (envv) {
		p32 = envv;
		for (;;) {
			error = copyin(p32++, &arg, sizeof(arg));
			if (error)
				goto err_exit;
			if (arg == 0)
				break;
			envp = PTRIN(arg);
			error = exec_args_add_env(args, envp, UIO_USERSPACE);
			if (error != 0)
				goto err_exit;
		}
	}

	return (0);

err_exit:
	exec_free_args(args);
	return (error);
}

int
freebsd32_execve(struct thread *td, struct freebsd32_execve_args *uap)
{
	struct image_args eargs;
	struct vmspace *oldvmspace;
	int error;

	error = pre_execve(td, &oldvmspace);
	if (error != 0)
		return (error);
	error = freebsd32_exec_copyin_args(&eargs, uap->fname, UIO_USERSPACE,
	    uap->argv, uap->envv);
	if (error == 0)
		error = kern_execve(td, &eargs, NULL);
	post_execve(td, error, oldvmspace);
	return (error);
}

int
freebsd32_fexecve(struct thread *td, struct freebsd32_fexecve_args *uap)
{
	struct image_args eargs;
	struct vmspace *oldvmspace;
	int error;

	error = pre_execve(td, &oldvmspace);
	if (error != 0)
		return (error);
	error = freebsd32_exec_copyin_args(&eargs, NULL, UIO_SYSSPACE,
	    uap->argv, uap->envv);
	if (error == 0) {
		eargs.fd = uap->fd;
		error = kern_execve(td, &eargs, NULL);
	}
	post_execve(td, error, oldvmspace);
	return (error);
}


int
freebsd32_mknodat(struct thread *td, struct freebsd32_mknodat_args *uap)
{

	return (kern_mknodat(td, uap->fd, uap->path, UIO_USERSPACE,
	    uap->mode, PAIR32TO64(dev_t, uap->dev)));
}

int
freebsd32_mprotect(struct thread *td, struct freebsd32_mprotect_args *uap)
{
	int prot;

	prot = uap->prot;
#if defined(__amd64__)
	if (i386_read_exec && (prot & PROT_READ) != 0)
		prot |= PROT_EXEC;
#endif
	return (kern_mprotect(td, (uintptr_t)PTRIN(uap->addr), uap->len,
	    prot));
}

int
freebsd32_mmap(struct thread *td, struct freebsd32_mmap_args *uap)
{
	int prot;

	prot = uap->prot;
#if defined(__amd64__)
	if (i386_read_exec && (prot & PROT_READ))
		prot |= PROT_EXEC;
#endif

	return (kern_mmap(td, (uintptr_t)uap->addr, uap->len, prot,
	    uap->flags, uap->fd, PAIR32TO64(off_t, uap->pos)));
}

#ifdef COMPAT_FREEBSD6
int
freebsd6_freebsd32_mmap(struct thread *td,
    struct freebsd6_freebsd32_mmap_args *uap)
{
	int prot;

	prot = uap->prot;
#if defined(__amd64__)
	if (i386_read_exec && (prot & PROT_READ))
		prot |= PROT_EXEC;
#endif

	return (kern_mmap(td, (uintptr_t)uap->addr, uap->len, prot,
	    uap->flags, uap->fd, PAIR32TO64(off_t, uap->pos)));
}
#endif

int
freebsd32_setitimer(struct thread *td, struct freebsd32_setitimer_args *uap)
{
	struct itimerval itv, oitv, *itvp;	
	struct itimerval32 i32;
	int error;

	if (uap->itv != NULL) {
		error = copyin(uap->itv, &i32, sizeof(i32));
		if (error)
			return (error);
		TV_CP(i32, itv, it_interval);
		TV_CP(i32, itv, it_value);
		itvp = &itv;
	} else
		itvp = NULL;
	error = kern_setitimer(td, uap->which, itvp, &oitv);
	if (error || uap->oitv == NULL)
		return (error);
	TV_CP(oitv, i32, it_interval);
	TV_CP(oitv, i32, it_value);
	return (copyout(&i32, uap->oitv, sizeof(i32)));
}

int
freebsd32_getitimer(struct thread *td, struct freebsd32_getitimer_args *uap)
{
	struct itimerval itv;
	struct itimerval32 i32;
	int error;

	error = kern_getitimer(td, uap->which, &itv);
	if (error || uap->itv == NULL)
		return (error);
	TV_CP(itv, i32, it_interval);
	TV_CP(itv, i32, it_value);
	return (copyout(&i32, uap->itv, sizeof(i32)));
}

int
freebsd32_select(struct thread *td, struct freebsd32_select_args *uap)
{
	struct timeval32 tv32;
	struct timeval tv, *tvp;
	int error;

	if (uap->tv != NULL) {
		error = copyin(uap->tv, &tv32, sizeof(tv32));
		if (error)
			return (error);
		CP(tv32, tv, tv_sec);
		CP(tv32, tv, tv_usec);
		tvp = &tv;
	} else
		tvp = NULL;
	/*
	 * XXX Do pointers need PTRIN()?
	 */
	return (kern_select(td, uap->nd, uap->in, uap->ou, uap->ex, tvp,
	    sizeof(int32_t) * 8));
}

int
freebsd32_pselect(struct thread *td, struct freebsd32_pselect_args *uap)
{
	struct timespec32 ts32;
	struct timespec ts;
	struct timeval tv, *tvp;
	sigset_t set, *uset;
	int error;

	if (uap->ts != NULL) {
		error = copyin(uap->ts, &ts32, sizeof(ts32));
		if (error != 0)
			return (error);
		CP(ts32, ts, tv_sec);
		CP(ts32, ts, tv_nsec);
		TIMESPEC_TO_TIMEVAL(&tv, &ts);
		tvp = &tv;
	} else
		tvp = NULL;
	if (uap->sm != NULL) {
		error = copyin(uap->sm, &set, sizeof(set));
		if (error != 0)
			return (error);
		uset = &set;
	} else
		uset = NULL;
	/*
	 * XXX Do pointers need PTRIN()?
	 */
	error = kern_pselect(td, uap->nd, uap->in, uap->ou, uap->ex, tvp,
	    uset, sizeof(int32_t) * 8);
	return (error);
}

/*
 * Copy 'count' items into the destination list pointed to by uap->eventlist.
 */
static int
freebsd32_kevent_copyout(void *arg, struct kevent *kevp, int count)
{
	struct freebsd32_kevent_args *uap;
	struct kevent32	ks32[KQ_NEVENTS];
	uint64_t e;
	int i, j, error;

	KASSERT(count <= KQ_NEVENTS, ("count (%d) > KQ_NEVENTS", count));
	uap = (struct freebsd32_kevent_args *)arg;

	for (i = 0; i < count; i++) {
		CP(kevp[i], ks32[i], ident);
		CP(kevp[i], ks32[i], filter);
		CP(kevp[i], ks32[i], flags);
		CP(kevp[i], ks32[i], fflags);
#if BYTE_ORDER == LITTLE_ENDIAN
		ks32[i].data1 = kevp[i].data;
		ks32[i].data2 = kevp[i].data >> 32;
#else
		ks32[i].data1 = kevp[i].data >> 32;
		ks32[i].data2 = kevp[i].data;
#endif
		PTROUT_CP(kevp[i], ks32[i], udata);
		for (j = 0; j < nitems(kevp->ext); j++) {
			e = kevp[i].ext[j];
#if BYTE_ORDER == LITTLE_ENDIAN
			ks32[i].ext64[2 * j] = e;
			ks32[i].ext64[2 * j + 1] = e >> 32;
#else
			ks32[i].ext64[2 * j] = e >> 32;
			ks32[i].ext64[2 * j + 1] = e;
#endif
		}
	}
	error = copyout(ks32, uap->eventlist, count * sizeof *ks32);
	if (error == 0)
		uap->eventlist += count;
	return (error);
}

/*
 * Copy 'count' items from the list pointed to by uap->changelist.
 */
static int
freebsd32_kevent_copyin(void *arg, struct kevent *kevp, int count)
{
	struct freebsd32_kevent_args *uap;
	struct kevent32	ks32[KQ_NEVENTS];
	uint64_t e;
	int i, j, error;

	KASSERT(count <= KQ_NEVENTS, ("count (%d) > KQ_NEVENTS", count));
	uap = (struct freebsd32_kevent_args *)arg;

	error = copyin(uap->changelist, ks32, count * sizeof *ks32);
	if (error)
		goto done;
	uap->changelist += count;

	for (i = 0; i < count; i++) {
		CP(ks32[i], kevp[i], ident);
		CP(ks32[i], kevp[i], filter);
		CP(ks32[i], kevp[i], flags);
		CP(ks32[i], kevp[i], fflags);
		kevp[i].data = PAIR32TO64(uint64_t, ks32[i].data);
		PTRIN_CP(ks32[i], kevp[i], udata);
		for (j = 0; j < nitems(kevp->ext); j++) {
#if BYTE_ORDER == LITTLE_ENDIAN
			e = ks32[i].ext64[2 * j + 1];
			e <<= 32;
			e += ks32[i].ext64[2 * j];
#else
			e = ks32[i].ext64[2 * j];
			e <<= 32;
			e += ks32[i].ext64[2 * j + 1];
#endif
			kevp[i].ext[j] = e;
		}
	}
done:
	return (error);
}

int
freebsd32_kevent(struct thread *td, struct freebsd32_kevent_args *uap)
{
	struct timespec32 ts32;
	struct timespec ts, *tsp;
	struct kevent_copyops k_ops = {
		.arg = uap,
		.k_copyout = freebsd32_kevent_copyout,
		.k_copyin = freebsd32_kevent_copyin,
	};
#ifdef KTRACE
	struct kevent32 *eventlist = uap->eventlist;
#endif
	int error;

	if (uap->timeout) {
		error = copyin(uap->timeout, &ts32, sizeof(ts32));
		if (error)
			return (error);
		CP(ts32, ts, tv_sec);
		CP(ts32, ts, tv_nsec);
		tsp = &ts;
	} else
		tsp = NULL;
#ifdef KTRACE
	if (KTRPOINT(td, KTR_STRUCT_ARRAY))
		ktrstructarray("kevent32", UIO_USERSPACE, uap->changelist,
		    uap->nchanges, sizeof(struct kevent32));
#endif
	error = kern_kevent(td, uap->fd, uap->nchanges, uap->nevents,
	    &k_ops, tsp);
#ifdef KTRACE
	if (error == 0 && KTRPOINT(td, KTR_STRUCT_ARRAY))
		ktrstructarray("kevent32", UIO_USERSPACE, eventlist,
		    td->td_retval[0], sizeof(struct kevent32));
#endif
	return (error);
}

#ifdef COMPAT_FREEBSD11
static int
freebsd32_kevent11_copyout(void *arg, struct kevent *kevp, int count)
{
	struct freebsd11_freebsd32_kevent_args *uap;
	struct kevent32_freebsd11 ks32[KQ_NEVENTS];
	int i, error;

	KASSERT(count <= KQ_NEVENTS, ("count (%d) > KQ_NEVENTS", count));
	uap = (struct freebsd11_freebsd32_kevent_args *)arg;

	for (i = 0; i < count; i++) {
		CP(kevp[i], ks32[i], ident);
		CP(kevp[i], ks32[i], filter);
		CP(kevp[i], ks32[i], flags);
		CP(kevp[i], ks32[i], fflags);
		CP(kevp[i], ks32[i], data);
		PTROUT_CP(kevp[i], ks32[i], udata);
	}
	error = copyout(ks32, uap->eventlist, count * sizeof *ks32);
	if (error == 0)
		uap->eventlist += count;
	return (error);
}

/*
 * Copy 'count' items from the list pointed to by uap->changelist.
 */
static int
freebsd32_kevent11_copyin(void *arg, struct kevent *kevp, int count)
{
	struct freebsd11_freebsd32_kevent_args *uap;
	struct kevent32_freebsd11 ks32[KQ_NEVENTS];
	int i, j, error;

	KASSERT(count <= KQ_NEVENTS, ("count (%d) > KQ_NEVENTS", count));
	uap = (struct freebsd11_freebsd32_kevent_args *)arg;

	error = copyin(uap->changelist, ks32, count * sizeof *ks32);
	if (error)
		goto done;
	uap->changelist += count;

	for (i = 0; i < count; i++) {
		CP(ks32[i], kevp[i], ident);
		CP(ks32[i], kevp[i], filter);
		CP(ks32[i], kevp[i], flags);
		CP(ks32[i], kevp[i], fflags);
		CP(ks32[i], kevp[i], data);
		PTRIN_CP(ks32[i], kevp[i], udata);
		for (j = 0; j < nitems(kevp->ext); j++)
			kevp[i].ext[j] = 0;
	}
done:
	return (error);
}

int
freebsd11_freebsd32_kevent(struct thread *td,
    struct freebsd11_freebsd32_kevent_args *uap)
{
	struct timespec32 ts32;
	struct timespec ts, *tsp;
	struct kevent_copyops k_ops = {
		.arg = uap,
		.k_copyout = freebsd32_kevent11_copyout,
		.k_copyin = freebsd32_kevent11_copyin,
	};
#ifdef KTRACE
	struct kevent32_freebsd11 *eventlist = uap->eventlist;
#endif
	int error;

	if (uap->timeout) {
		error = copyin(uap->timeout, &ts32, sizeof(ts32));
		if (error)
			return (error);
		CP(ts32, ts, tv_sec);
		CP(ts32, ts, tv_nsec);
		tsp = &ts;
	} else
		tsp = NULL;
#ifdef KTRACE
	if (KTRPOINT(td, KTR_STRUCT_ARRAY))
		ktrstructarray("kevent32_freebsd11", UIO_USERSPACE,
		    uap->changelist, uap->nchanges,
		    sizeof(struct kevent32_freebsd11));
#endif
	error = kern_kevent(td, uap->fd, uap->nchanges, uap->nevents,
	    &k_ops, tsp);
#ifdef KTRACE
	if (error == 0 && KTRPOINT(td, KTR_STRUCT_ARRAY))
		ktrstructarray("kevent32_freebsd11", UIO_USERSPACE,
		    eventlist, td->td_retval[0],
		    sizeof(struct kevent32_freebsd11));
#endif
	return (error);
}
#endif

int
freebsd32_gettimeofday(struct thread *td,
		       struct freebsd32_gettimeofday_args *uap)
{
	struct timeval atv;
	struct timeval32 atv32;
	struct timezone rtz;
	int error = 0;

	if (uap->tp) {
		microtime(&atv);
		CP(atv, atv32, tv_sec);
		CP(atv, atv32, tv_usec);
		error = copyout(&atv32, uap->tp, sizeof (atv32));
	}
	if (error == 0 && uap->tzp != NULL) {
		rtz.tz_minuteswest = 0;
		rtz.tz_dsttime = 0;
		error = copyout(&rtz, uap->tzp, sizeof (rtz));
	}
	return (error);
}

int
freebsd32_getrusage(struct thread *td, struct freebsd32_getrusage_args *uap)
{
	struct rusage32 s32;
	struct rusage s;
	int error;

	error = kern_getrusage(td, uap->who, &s);
	if (error == 0) {
		freebsd32_rusage_out(&s, &s32);
		error = copyout(&s32, uap->rusage, sizeof(s32));
	}
	return (error);
}

static int
freebsd32_copyinuio(struct iovec32 *iovp, u_int iovcnt, struct uio **uiop)
{
	struct iovec32 iov32;
	struct iovec *iov;
	struct uio *uio;
	u_int iovlen;
	int error, i;

	*uiop = NULL;
	if (iovcnt > UIO_MAXIOV)
		return (EINVAL);
	iovlen = iovcnt * sizeof(struct iovec);
	uio = malloc(iovlen + sizeof *uio, M_IOV, M_WAITOK);
	iov = (struct iovec *)(uio + 1);
	for (i = 0; i < iovcnt; i++) {
		error = copyin(&iovp[i], &iov32, sizeof(struct iovec32));
		if (error) {
			free(uio, M_IOV);
			return (error);
		}
		iov[i].iov_base = PTRIN(iov32.iov_base);
		iov[i].iov_len = iov32.iov_len;
	}
	uio->uio_iov = iov;
	uio->uio_iovcnt = iovcnt;
	uio->uio_segflg = UIO_USERSPACE;
	uio->uio_offset = -1;
	uio->uio_resid = 0;
	for (i = 0; i < iovcnt; i++) {
		if (iov->iov_len > INT_MAX - uio->uio_resid) {
			free(uio, M_IOV);
			return (EINVAL);
		}
		uio->uio_resid += iov->iov_len;
		iov++;
	}
	*uiop = uio;
	return (0);
}

int
freebsd32_readv(struct thread *td, struct freebsd32_readv_args *uap)
{
	struct uio *auio;
	int error;

	error = freebsd32_copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_readv(td, uap->fd, auio);
	free(auio, M_IOV);
	return (error);
}

int
freebsd32_writev(struct thread *td, struct freebsd32_writev_args *uap)
{
	struct uio *auio;
	int error;

	error = freebsd32_copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_writev(td, uap->fd, auio);
	free(auio, M_IOV);
	return (error);
}

int
freebsd32_preadv(struct thread *td, struct freebsd32_preadv_args *uap)
{
	struct uio *auio;
	int error;

	error = freebsd32_copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_preadv(td, uap->fd, auio, PAIR32TO64(off_t,uap->offset));
	free(auio, M_IOV);
	return (error);
}

int
freebsd32_pwritev(struct thread *td, struct freebsd32_pwritev_args *uap)
{
	struct uio *auio;
	int error;

	error = freebsd32_copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_pwritev(td, uap->fd, auio, PAIR32TO64(off_t,uap->offset));
	free(auio, M_IOV);
	return (error);
}

int
freebsd32_copyiniov(struct iovec32 *iovp32, u_int iovcnt, struct iovec **iovp,
    int error)
{
	struct iovec32 iov32;
	struct iovec *iov;
	u_int iovlen;
	int i;

	*iovp = NULL;
	if (iovcnt > UIO_MAXIOV)
		return (error);
	iovlen = iovcnt * sizeof(struct iovec);
	iov = malloc(iovlen, M_IOV, M_WAITOK);
	for (i = 0; i < iovcnt; i++) {
		error = copyin(&iovp32[i], &iov32, sizeof(struct iovec32));
		if (error) {
			free(iov, M_IOV);
			return (error);
		}
		iov[i].iov_base = PTRIN(iov32.iov_base);
		iov[i].iov_len = iov32.iov_len;
	}
	*iovp = iov;
	return (0);
}

static int
freebsd32_copyinmsghdr(struct msghdr32 *msg32, struct msghdr *msg)
{
	struct msghdr32 m32;
	int error;

	error = copyin(msg32, &m32, sizeof(m32));
	if (error)
		return (error);
	msg->msg_name = PTRIN(m32.msg_name);
	msg->msg_namelen = m32.msg_namelen;
	msg->msg_iov = PTRIN(m32.msg_iov);
	msg->msg_iovlen = m32.msg_iovlen;
	msg->msg_control = PTRIN(m32.msg_control);
	msg->msg_controllen = m32.msg_controllen;
	msg->msg_flags = m32.msg_flags;
	return (0);
}

static int
freebsd32_copyoutmsghdr(struct msghdr *msg, struct msghdr32 *msg32)
{
	struct msghdr32 m32;
	int error;

	m32.msg_name = PTROUT(msg->msg_name);
	m32.msg_namelen = msg->msg_namelen;
	m32.msg_iov = PTROUT(msg->msg_iov);
	m32.msg_iovlen = msg->msg_iovlen;
	m32.msg_control = PTROUT(msg->msg_control);
	m32.msg_controllen = msg->msg_controllen;
	m32.msg_flags = msg->msg_flags;
	error = copyout(&m32, msg32, sizeof(m32));
	return (error);
}

#ifndef __mips__
#define FREEBSD32_ALIGNBYTES	(sizeof(int) - 1)
#else
#define FREEBSD32_ALIGNBYTES	(sizeof(long) - 1)
#endif
#define FREEBSD32_ALIGN(p)	\
	(((u_long)(p) + FREEBSD32_ALIGNBYTES) & ~FREEBSD32_ALIGNBYTES)
#define	FREEBSD32_CMSG_SPACE(l)	\
	(FREEBSD32_ALIGN(sizeof(struct cmsghdr)) + FREEBSD32_ALIGN(l))

#define	FREEBSD32_CMSG_DATA(cmsg)	((unsigned char *)(cmsg) + \
				 FREEBSD32_ALIGN(sizeof(struct cmsghdr)))

static size_t
freebsd32_cmsg_convert(const struct cmsghdr *cm, void *data, socklen_t datalen)
{
	size_t copylen;
	union {
		struct timespec32 ts;
		struct timeval32 tv;
		struct bintime32 bt;
	} tmp32;

	union {
		struct timespec ts;
		struct timeval tv;
		struct bintime bt;
	} *in;

	in = data;
	copylen = 0;
	switch (cm->cmsg_level) {
	case SOL_SOCKET:
		switch (cm->cmsg_type) {
		case SCM_TIMESTAMP:
			TV_CP(*in, tmp32, tv);
			copylen = sizeof(tmp32.tv);
			break;

		case SCM_BINTIME:
			BT_CP(*in, tmp32, bt);
			copylen = sizeof(tmp32.bt);
			break;

		case SCM_REALTIME:
		case SCM_MONOTONIC:
			TS_CP(*in, tmp32, ts);
			copylen = sizeof(tmp32.ts);
			break;

		default:
			break;
		}

	default:
		break;
	}

	if (copylen == 0)
		return (datalen);

	KASSERT((datalen >= copylen), ("corrupted cmsghdr"));

	bcopy(&tmp32, data, copylen);
	return (copylen);
}

static int
freebsd32_copy_msg_out(struct msghdr *msg, struct mbuf *control)
{
	struct cmsghdr *cm;
	void *data;
	socklen_t clen, datalen, datalen_out, oldclen;
	int error;
	caddr_t ctlbuf;
	int len, maxlen, copylen;
	struct mbuf *m;
	error = 0;

	len    = msg->msg_controllen;
	maxlen = msg->msg_controllen;
	msg->msg_controllen = 0;

	ctlbuf = msg->msg_control;
	for (m = control; m != NULL && len > 0; m = m->m_next) {
		cm = mtod(m, struct cmsghdr *);
		clen = m->m_len;
		while (cm != NULL) {
			if (sizeof(struct cmsghdr) > clen ||
			    cm->cmsg_len > clen) {
				error = EINVAL;
				break;
			}

			data   = CMSG_DATA(cm);
			datalen = (caddr_t)cm + cm->cmsg_len - (caddr_t)data;
			datalen_out = freebsd32_cmsg_convert(cm, data, datalen);

			/*
			 * Copy out the message header.  Preserve the native
			 * message size in case we need to inspect the message
			 * contents later.
			 */
			copylen = sizeof(struct cmsghdr);
			if (len < copylen) {
				msg->msg_flags |= MSG_CTRUNC;
				m_dispose_extcontrolm(m);
				goto exit;
			}
			oldclen = cm->cmsg_len;
			cm->cmsg_len = FREEBSD32_ALIGN(sizeof(struct cmsghdr)) +
			    datalen_out;
			error = copyout(cm, ctlbuf, copylen);
			cm->cmsg_len = oldclen;
			if (error != 0)
				goto exit;

			ctlbuf += FREEBSD32_ALIGN(copylen);
			len    -= FREEBSD32_ALIGN(copylen);

			copylen = datalen_out;
			if (len < copylen) {
				msg->msg_flags |= MSG_CTRUNC;
				m_dispose_extcontrolm(m);
				break;
			}

			/* Copy out the message data. */
			error = copyout(data, ctlbuf, copylen);
			if (error)
				goto exit;

			ctlbuf += FREEBSD32_ALIGN(copylen);
			len    -= FREEBSD32_ALIGN(copylen);

			if (CMSG_SPACE(datalen) < clen) {
				clen -= CMSG_SPACE(datalen);
				cm = (struct cmsghdr *)
				    ((caddr_t)cm + CMSG_SPACE(datalen));
			} else {
				clen = 0;
				cm = NULL;
			}

			msg->msg_controllen += FREEBSD32_ALIGN(sizeof(*cm)) +
			    datalen_out;
		}
	}
	if (len == 0 && m != NULL) {
		msg->msg_flags |= MSG_CTRUNC;
		m_dispose_extcontrolm(m);
	}

exit:
	return (error);
}

int
freebsd32_recvmsg(td, uap)
	struct thread *td;
	struct freebsd32_recvmsg_args /* {
		int	s;
		struct	msghdr32 *msg;
		int	flags;
	} */ *uap;
{
	struct msghdr msg;
	struct msghdr32 m32;
	struct iovec *uiov, *iov;
	struct mbuf *control = NULL;
	struct mbuf **controlp;

	int error;
	error = copyin(uap->msg, &m32, sizeof(m32));
	if (error)
		return (error);
	error = freebsd32_copyinmsghdr(uap->msg, &msg);
	if (error)
		return (error);
	error = freebsd32_copyiniov(PTRIN(m32.msg_iov), m32.msg_iovlen, &iov,
	    EMSGSIZE);
	if (error)
		return (error);
	msg.msg_flags = uap->flags;
	uiov = msg.msg_iov;
	msg.msg_iov = iov;

	controlp = (msg.msg_control != NULL) ?  &control : NULL;
	error = kern_recvit(td, uap->s, &msg, UIO_USERSPACE, controlp);
	if (error == 0) {
		msg.msg_iov = uiov;

		if (control != NULL)
			error = freebsd32_copy_msg_out(&msg, control);
		else
			msg.msg_controllen = 0;

		if (error == 0)
			error = freebsd32_copyoutmsghdr(&msg, uap->msg);
	}
	free(iov, M_IOV);

	if (control != NULL) {
		if (error != 0)
			m_dispose_extcontrolm(control);
		m_freem(control);
	}

	return (error);
}

/*
 * Copy-in the array of control messages constructed using alignment
 * and padding suitable for a 32-bit environment and construct an
 * mbuf using alignment and padding suitable for a 64-bit kernel.
 * The alignment and padding are defined indirectly by CMSG_DATA(),
 * CMSG_SPACE() and CMSG_LEN().
 */
static int
freebsd32_copyin_control(struct mbuf **mp, caddr_t buf, u_int buflen)
{
	struct mbuf *m;
	void *md;
	u_int idx, len, msglen;
	int error;

	buflen = FREEBSD32_ALIGN(buflen);

	if (buflen > MCLBYTES)
		return (EINVAL);

	/*
	 * Iterate over the buffer and get the length of each message
	 * in there. This has 32-bit alignment and padding. Use it to
	 * determine the length of these messages when using 64-bit
	 * alignment and padding.
	 */
	idx = 0;
	len = 0;
	while (idx < buflen) {
		error = copyin(buf + idx, &msglen, sizeof(msglen));
		if (error)
			return (error);
		if (msglen < sizeof(struct cmsghdr))
			return (EINVAL);
		msglen = FREEBSD32_ALIGN(msglen);
		if (idx + msglen > buflen)
			return (EINVAL);
		idx += msglen;
		msglen += CMSG_ALIGN(sizeof(struct cmsghdr)) -
		    FREEBSD32_ALIGN(sizeof(struct cmsghdr));
		len += CMSG_ALIGN(msglen);
	}

	if (len > MCLBYTES)
		return (EINVAL);

	m = m_get(M_WAITOK, MT_CONTROL);
	if (len > MLEN)
		MCLGET(m, M_WAITOK);
	m->m_len = len;

	md = mtod(m, void *);
	while (buflen > 0) {
		error = copyin(buf, md, sizeof(struct cmsghdr));
		if (error)
			break;
		msglen = *(u_int *)md;
		msglen = FREEBSD32_ALIGN(msglen);

		/* Modify the message length to account for alignment. */
		*(u_int *)md = msglen + CMSG_ALIGN(sizeof(struct cmsghdr)) -
		    FREEBSD32_ALIGN(sizeof(struct cmsghdr));

		md = (char *)md + CMSG_ALIGN(sizeof(struct cmsghdr));
		buf += FREEBSD32_ALIGN(sizeof(struct cmsghdr));
		buflen -= FREEBSD32_ALIGN(sizeof(struct cmsghdr));

		msglen -= FREEBSD32_ALIGN(sizeof(struct cmsghdr));
		if (msglen > 0) {
			error = copyin(buf, md, msglen);
			if (error)
				break;
			md = (char *)md + CMSG_ALIGN(msglen);
			buf += msglen;
			buflen -= msglen;
		}
	}

	if (error)
		m_free(m);
	else
		*mp = m;
	return (error);
}

int
freebsd32_sendmsg(struct thread *td,
		  struct freebsd32_sendmsg_args *uap)
{
	struct msghdr msg;
	struct msghdr32 m32;
	struct iovec *iov;
	struct mbuf *control = NULL;
	struct sockaddr *to = NULL;
	int error;

	error = copyin(uap->msg, &m32, sizeof(m32));
	if (error)
		return (error);
	error = freebsd32_copyinmsghdr(uap->msg, &msg);
	if (error)
		return (error);
	error = freebsd32_copyiniov(PTRIN(m32.msg_iov), m32.msg_iovlen, &iov,
	    EMSGSIZE);
	if (error)
		return (error);
	msg.msg_iov = iov;
	if (msg.msg_name != NULL) {
		error = getsockaddr(&to, msg.msg_name, msg.msg_namelen);
		if (error) {
			to = NULL;
			goto out;
		}
		msg.msg_name = to;
	}

	if (msg.msg_control) {
		if (msg.msg_controllen < sizeof(struct cmsghdr)) {
			error = EINVAL;
			goto out;
		}

		error = freebsd32_copyin_control(&control, msg.msg_control,
		    msg.msg_controllen);
		if (error)
			goto out;

		msg.msg_control = NULL;
		msg.msg_controllen = 0;
	}

	error = kern_sendit(td, uap->s, &msg, uap->flags, control,
	    UIO_USERSPACE);

out:
	free(iov, M_IOV);
	if (to)
		free(to, M_SONAME);
	return (error);
}

int
freebsd32_recvfrom(struct thread *td,
		   struct freebsd32_recvfrom_args *uap)
{
	struct msghdr msg;
	struct iovec aiov;
	int error;

	if (uap->fromlenaddr) {
		error = copyin(PTRIN(uap->fromlenaddr), &msg.msg_namelen,
		    sizeof(msg.msg_namelen));
		if (error)
			return (error);
	} else {
		msg.msg_namelen = 0;
	}

	msg.msg_name = PTRIN(uap->from);
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	aiov.iov_base = PTRIN(uap->buf);
	aiov.iov_len = uap->len;
	msg.msg_control = NULL;
	msg.msg_flags = uap->flags;
	error = kern_recvit(td, uap->s, &msg, UIO_USERSPACE, NULL);
	if (error == 0 && uap->fromlenaddr)
		error = copyout(&msg.msg_namelen, PTRIN(uap->fromlenaddr),
		    sizeof (msg.msg_namelen));
	return (error);
}

int
freebsd32_settimeofday(struct thread *td,
		       struct freebsd32_settimeofday_args *uap)
{
	struct timeval32 tv32;
	struct timeval tv, *tvp;
	struct timezone tz, *tzp;
	int error;

	if (uap->tv) {
		error = copyin(uap->tv, &tv32, sizeof(tv32));
		if (error)
			return (error);
		CP(tv32, tv, tv_sec);
		CP(tv32, tv, tv_usec);
		tvp = &tv;
	} else
		tvp = NULL;
	if (uap->tzp) {
		error = copyin(uap->tzp, &tz, sizeof(tz));
		if (error)
			return (error);
		tzp = &tz;
	} else
		tzp = NULL;
	return (kern_settimeofday(td, tvp, tzp));
}

int
freebsd32_utimes(struct thread *td, struct freebsd32_utimes_args *uap)
{
	struct timeval32 s32[2];
	struct timeval s[2], *sp;
	int error;

	if (uap->tptr != NULL) {
		error = copyin(uap->tptr, s32, sizeof(s32));
		if (error)
			return (error);
		CP(s32[0], s[0], tv_sec);
		CP(s32[0], s[0], tv_usec);
		CP(s32[1], s[1], tv_sec);
		CP(s32[1], s[1], tv_usec);
		sp = s;
	} else
		sp = NULL;
	return (kern_utimesat(td, AT_FDCWD, uap->path, UIO_USERSPACE,
	    sp, UIO_SYSSPACE));
}

int
freebsd32_lutimes(struct thread *td, struct freebsd32_lutimes_args *uap)
{
	struct timeval32 s32[2];
	struct timeval s[2], *sp;
	int error;

	if (uap->tptr != NULL) {
		error = copyin(uap->tptr, s32, sizeof(s32));
		if (error)
			return (error);
		CP(s32[0], s[0], tv_sec);
		CP(s32[0], s[0], tv_usec);
		CP(s32[1], s[1], tv_sec);
		CP(s32[1], s[1], tv_usec);
		sp = s;
	} else
		sp = NULL;
	return (kern_lutimes(td, uap->path, UIO_USERSPACE, sp, UIO_SYSSPACE));
}

int
freebsd32_futimes(struct thread *td, struct freebsd32_futimes_args *uap)
{
	struct timeval32 s32[2];
	struct timeval s[2], *sp;
	int error;

	if (uap->tptr != NULL) {
		error = copyin(uap->tptr, s32, sizeof(s32));
		if (error)
			return (error);
		CP(s32[0], s[0], tv_sec);
		CP(s32[0], s[0], tv_usec);
		CP(s32[1], s[1], tv_sec);
		CP(s32[1], s[1], tv_usec);
		sp = s;
	} else
		sp = NULL;
	return (kern_futimes(td, uap->fd, sp, UIO_SYSSPACE));
}

int
freebsd32_futimesat(struct thread *td, struct freebsd32_futimesat_args *uap)
{
	struct timeval32 s32[2];
	struct timeval s[2], *sp;
	int error;

	if (uap->times != NULL) {
		error = copyin(uap->times, s32, sizeof(s32));
		if (error)
			return (error);
		CP(s32[0], s[0], tv_sec);
		CP(s32[0], s[0], tv_usec);
		CP(s32[1], s[1], tv_sec);
		CP(s32[1], s[1], tv_usec);
		sp = s;
	} else
		sp = NULL;
	return (kern_utimesat(td, uap->fd, uap->path, UIO_USERSPACE,
		sp, UIO_SYSSPACE));
}

int
freebsd32_futimens(struct thread *td, struct freebsd32_futimens_args *uap)
{
	struct timespec32 ts32[2];
	struct timespec ts[2], *tsp;
	int error;

	if (uap->times != NULL) {
		error = copyin(uap->times, ts32, sizeof(ts32));
		if (error)
			return (error);
		CP(ts32[0], ts[0], tv_sec);
		CP(ts32[0], ts[0], tv_nsec);
		CP(ts32[1], ts[1], tv_sec);
		CP(ts32[1], ts[1], tv_nsec);
		tsp = ts;
	} else
		tsp = NULL;
	return (kern_futimens(td, uap->fd, tsp, UIO_SYSSPACE));
}

int
freebsd32_utimensat(struct thread *td, struct freebsd32_utimensat_args *uap)
{
	struct timespec32 ts32[2];
	struct timespec ts[2], *tsp;
	int error;

	if (uap->times != NULL) {
		error = copyin(uap->times, ts32, sizeof(ts32));
		if (error)
			return (error);
		CP(ts32[0], ts[0], tv_sec);
		CP(ts32[0], ts[0], tv_nsec);
		CP(ts32[1], ts[1], tv_sec);
		CP(ts32[1], ts[1], tv_nsec);
		tsp = ts;
	} else
		tsp = NULL;
	return (kern_utimensat(td, uap->fd, uap->path, UIO_USERSPACE,
	    tsp, UIO_SYSSPACE, uap->flag));
}

int
freebsd32_adjtime(struct thread *td, struct freebsd32_adjtime_args *uap)
{
	struct timeval32 tv32;
	struct timeval delta, olddelta, *deltap;
	int error;

	if (uap->delta) {
		error = copyin(uap->delta, &tv32, sizeof(tv32));
		if (error)
			return (error);
		CP(tv32, delta, tv_sec);
		CP(tv32, delta, tv_usec);
		deltap = &delta;
	} else
		deltap = NULL;
	error = kern_adjtime(td, deltap, &olddelta);
	if (uap->olddelta && error == 0) {
		CP(olddelta, tv32, tv_sec);
		CP(olddelta, tv32, tv_usec);
		error = copyout(&tv32, uap->olddelta, sizeof(tv32));
	}
	return (error);
}

#ifdef COMPAT_FREEBSD4
int
freebsd4_freebsd32_statfs(struct thread *td, struct freebsd4_freebsd32_statfs_args *uap)
{
	struct statfs32 s32;
	struct statfs *sp;
	int error;

	sp = malloc(sizeof(struct statfs), M_STATFS, M_WAITOK);
	error = kern_statfs(td, uap->path, UIO_USERSPACE, sp);
	if (error == 0) {
		copy_statfs(sp, &s32);
		error = copyout(&s32, uap->buf, sizeof(s32));
	}
	free(sp, M_STATFS);
	return (error);
}
#endif

#ifdef COMPAT_FREEBSD4
int
freebsd4_freebsd32_fstatfs(struct thread *td, struct freebsd4_freebsd32_fstatfs_args *uap)
{
	struct statfs32 s32;
	struct statfs *sp;
	int error;

	sp = malloc(sizeof(struct statfs), M_STATFS, M_WAITOK);
	error = kern_fstatfs(td, uap->fd, sp);
	if (error == 0) {
		copy_statfs(sp, &s32);
		error = copyout(&s32, uap->buf, sizeof(s32));
	}
	free(sp, M_STATFS);
	return (error);
}
#endif

#ifdef COMPAT_FREEBSD4
int
freebsd4_freebsd32_fhstatfs(struct thread *td, struct freebsd4_freebsd32_fhstatfs_args *uap)
{
	struct statfs32 s32;
	struct statfs *sp;
	fhandle_t fh;
	int error;

	if ((error = copyin(uap->u_fhp, &fh, sizeof(fhandle_t))) != 0)
		return (error);
	sp = malloc(sizeof(struct statfs), M_STATFS, M_WAITOK);
	error = kern_fhstatfs(td, fh, sp);
	if (error == 0) {
		copy_statfs(sp, &s32);
		error = copyout(&s32, uap->buf, sizeof(s32));
	}
	free(sp, M_STATFS);
	return (error);
}
#endif

int
freebsd32_pread(struct thread *td, struct freebsd32_pread_args *uap)
{

	return (kern_pread(td, uap->fd, uap->buf, uap->nbyte,
	    PAIR32TO64(off_t, uap->offset)));
}

int
freebsd32_pwrite(struct thread *td, struct freebsd32_pwrite_args *uap)
{

	return (kern_pwrite(td, uap->fd, uap->buf, uap->nbyte,
	    PAIR32TO64(off_t, uap->offset)));
}

#ifdef COMPAT_43
int
ofreebsd32_lseek(struct thread *td, struct ofreebsd32_lseek_args *uap)
{

	return (kern_lseek(td, uap->fd, uap->offset, uap->whence));
}
#endif

int
freebsd32_lseek(struct thread *td, struct freebsd32_lseek_args *uap)
{
	int error;
	off_t pos;

	error = kern_lseek(td, uap->fd, PAIR32TO64(off_t, uap->offset),
	    uap->whence);
	/* Expand the quad return into two parts for eax and edx */
	pos = td->td_uretoff.tdu_off;
	td->td_retval[RETVAL_LO] = pos & 0xffffffff;	/* %eax */
	td->td_retval[RETVAL_HI] = pos >> 32;		/* %edx */
	return error;
}

int
freebsd32_truncate(struct thread *td, struct freebsd32_truncate_args *uap)
{

	return (kern_truncate(td, uap->path, UIO_USERSPACE,
	    PAIR32TO64(off_t, uap->length)));
}

int
freebsd32_ftruncate(struct thread *td, struct freebsd32_ftruncate_args *uap)
{

	return (kern_ftruncate(td, uap->fd, PAIR32TO64(off_t, uap->length)));
}

#ifdef COMPAT_43
int
ofreebsd32_getdirentries(struct thread *td,
    struct ofreebsd32_getdirentries_args *uap)
{
	struct ogetdirentries_args ap;
	int error;
	long loff;
	int32_t loff_cut;

	ap.fd = uap->fd;
	ap.buf = uap->buf;
	ap.count = uap->count;
	ap.basep = NULL;
	error = kern_ogetdirentries(td, &ap, &loff);
	if (error == 0) {
		loff_cut = loff;
		error = copyout(&loff_cut, uap->basep, sizeof(int32_t));
	}
	return (error);
}
#endif

#if defined(COMPAT_FREEBSD11)
int
freebsd11_freebsd32_getdirentries(struct thread *td,
    struct freebsd11_freebsd32_getdirentries_args *uap)
{
	long base;
	int32_t base32;
	int error;

	error = freebsd11_kern_getdirentries(td, uap->fd, uap->buf, uap->count,
	    &base, NULL);
	if (error)
		return (error);
	if (uap->basep != NULL) {
		base32 = base;
		error = copyout(&base32, uap->basep, sizeof(int32_t));
	}
	return (error);
}

int
freebsd11_freebsd32_getdents(struct thread *td,
    struct freebsd11_freebsd32_getdents_args *uap)
{
	struct freebsd11_freebsd32_getdirentries_args ap;

	ap.fd = uap->fd;
	ap.buf = uap->buf;
	ap.count = uap->count;
	ap.basep = NULL;
	return (freebsd11_freebsd32_getdirentries(td, &ap));
}
#endif /* COMPAT_FREEBSD11 */

#ifdef COMPAT_FREEBSD6
/* versions with the 'int pad' argument */
int
freebsd6_freebsd32_pread(struct thread *td, struct freebsd6_freebsd32_pread_args *uap)
{

	return (kern_pread(td, uap->fd, uap->buf, uap->nbyte,
	    PAIR32TO64(off_t, uap->offset)));
}

int
freebsd6_freebsd32_pwrite(struct thread *td, struct freebsd6_freebsd32_pwrite_args *uap)
{

	return (kern_pwrite(td, uap->fd, uap->buf, uap->nbyte,
	    PAIR32TO64(off_t, uap->offset)));
}

int
freebsd6_freebsd32_lseek(struct thread *td, struct freebsd6_freebsd32_lseek_args *uap)
{
	int error;
	off_t pos;

	error = kern_lseek(td, uap->fd, PAIR32TO64(off_t, uap->offset),
	    uap->whence);
	/* Expand the quad return into two parts for eax and edx */
	pos = *(off_t *)(td->td_retval);
	td->td_retval[RETVAL_LO] = pos & 0xffffffff;	/* %eax */
	td->td_retval[RETVAL_HI] = pos >> 32;		/* %edx */
	return error;
}

int
freebsd6_freebsd32_truncate(struct thread *td, struct freebsd6_freebsd32_truncate_args *uap)
{

	return (kern_truncate(td, uap->path, UIO_USERSPACE,
	    PAIR32TO64(off_t, uap->length)));
}

int
freebsd6_freebsd32_ftruncate(struct thread *td, struct freebsd6_freebsd32_ftruncate_args *uap)
{

	return (kern_ftruncate(td, uap->fd, PAIR32TO64(off_t, uap->length)));
}
#endif /* COMPAT_FREEBSD6 */

struct sf_hdtr32 {
	uint32_t headers;
	int hdr_cnt;
	uint32_t trailers;
	int trl_cnt;
};

static int
freebsd32_do_sendfile(struct thread *td,
    struct freebsd32_sendfile_args *uap, int compat)
{
	struct sf_hdtr32 hdtr32;
	struct sf_hdtr hdtr;
	struct uio *hdr_uio, *trl_uio;
	struct file *fp;
	cap_rights_t rights;
	struct iovec32 *iov32;
	off_t offset, sbytes;
	int error;

	offset = PAIR32TO64(off_t, uap->offset);
	if (offset < 0)
		return (EINVAL);

	hdr_uio = trl_uio = NULL;

	if (uap->hdtr != NULL) {
		error = copyin(uap->hdtr, &hdtr32, sizeof(hdtr32));
		if (error)
			goto out;
		PTRIN_CP(hdtr32, hdtr, headers);
		CP(hdtr32, hdtr, hdr_cnt);
		PTRIN_CP(hdtr32, hdtr, trailers);
		CP(hdtr32, hdtr, trl_cnt);

		if (hdtr.headers != NULL) {
			iov32 = PTRIN(hdtr32.headers);
			error = freebsd32_copyinuio(iov32,
			    hdtr32.hdr_cnt, &hdr_uio);
			if (error)
				goto out;
#ifdef COMPAT_FREEBSD4
			/*
			 * In FreeBSD < 5.0 the nbytes to send also included
			 * the header.  If compat is specified subtract the
			 * header size from nbytes.
			 */
			if (compat) {
				if (uap->nbytes > hdr_uio->uio_resid)
					uap->nbytes -= hdr_uio->uio_resid;
				else
					uap->nbytes = 0;
			}
#endif
		}
		if (hdtr.trailers != NULL) {
			iov32 = PTRIN(hdtr32.trailers);
			error = freebsd32_copyinuio(iov32,
			    hdtr32.trl_cnt, &trl_uio);
			if (error)
				goto out;
		}
	}

	AUDIT_ARG_FD(uap->fd);

	if ((error = fget_read(td, uap->fd,
	    cap_rights_init(&rights, CAP_PREAD), &fp)) != 0)
		goto out;

	error = fo_sendfile(fp, uap->s, hdr_uio, trl_uio, offset,
	    uap->nbytes, &sbytes, uap->flags, td);
	fdrop(fp, td);

	if (uap->sbytes != NULL)
		copyout(&sbytes, uap->sbytes, sizeof(off_t));

out:
	if (hdr_uio)
		free(hdr_uio, M_IOV);
	if (trl_uio)
		free(trl_uio, M_IOV);
	return (error);
}

#ifdef COMPAT_FREEBSD4
int
freebsd4_freebsd32_sendfile(struct thread *td,
    struct freebsd4_freebsd32_sendfile_args *uap)
{
	return (freebsd32_do_sendfile(td,
	    (struct freebsd32_sendfile_args *)uap, 1));
}
#endif

int
freebsd32_sendfile(struct thread *td, struct freebsd32_sendfile_args *uap)
{

	return (freebsd32_do_sendfile(td, uap, 0));
}

static void
copy_stat(struct stat *in, struct stat32 *out)
{

	CP(*in, *out, st_dev);
	CP(*in, *out, st_ino);
	CP(*in, *out, st_mode);
	CP(*in, *out, st_nlink);
	CP(*in, *out, st_uid);
	CP(*in, *out, st_gid);
	CP(*in, *out, st_rdev);
	TS_CP(*in, *out, st_atim);
	TS_CP(*in, *out, st_mtim);
	TS_CP(*in, *out, st_ctim);
	CP(*in, *out, st_size);
	CP(*in, *out, st_blocks);
	CP(*in, *out, st_blksize);
	CP(*in, *out, st_flags);
	CP(*in, *out, st_gen);
	TS_CP(*in, *out, st_birthtim);
	out->st_padding0 = 0;
	out->st_padding1 = 0;
#ifdef __STAT32_TIME_T_EXT
	out->st_atim_ext = 0;
	out->st_mtim_ext = 0;
	out->st_ctim_ext = 0;
	out->st_btim_ext = 0;
#endif
	bzero(out->st_spare, sizeof(out->st_spare));
}

#ifdef COMPAT_43
static void
copy_ostat(struct stat *in, struct ostat32 *out)
{

	bzero(out, sizeof(*out));
	CP(*in, *out, st_dev);
	CP(*in, *out, st_ino);
	CP(*in, *out, st_mode);
	CP(*in, *out, st_nlink);
	CP(*in, *out, st_uid);
	CP(*in, *out, st_gid);
	CP(*in, *out, st_rdev);
	out->st_size = MIN(in->st_size, INT32_MAX);
	TS_CP(*in, *out, st_atim);
	TS_CP(*in, *out, st_mtim);
	TS_CP(*in, *out, st_ctim);
	CP(*in, *out, st_blksize);
	CP(*in, *out, st_blocks);
	CP(*in, *out, st_flags);
	CP(*in, *out, st_gen);
}
#endif

#ifdef COMPAT_43
int
ofreebsd32_stat(struct thread *td, struct ofreebsd32_stat_args *uap)
{
	struct stat sb;
	struct ostat32 sb32;
	int error;

	error = kern_statat(td, 0, AT_FDCWD, uap->path, UIO_USERSPACE,
	    &sb, NULL);
	if (error)
		return (error);
	copy_ostat(&sb, &sb32);
	error = copyout(&sb32, uap->ub, sizeof (sb32));
	return (error);
}
#endif

int
freebsd32_fstat(struct thread *td, struct freebsd32_fstat_args *uap)
{
	struct stat ub;
	struct stat32 ub32;
	int error;

	error = kern_fstat(td, uap->fd, &ub);
	if (error)
		return (error);
	copy_stat(&ub, &ub32);
	error = copyout(&ub32, uap->ub, sizeof(ub32));
	return (error);
}

#ifdef COMPAT_43
int
ofreebsd32_fstat(struct thread *td, struct ofreebsd32_fstat_args *uap)
{
	struct stat ub;
	struct ostat32 ub32;
	int error;

	error = kern_fstat(td, uap->fd, &ub);
	if (error)
		return (error);
	copy_ostat(&ub, &ub32);
	error = copyout(&ub32, uap->ub, sizeof(ub32));
	return (error);
}
#endif

int
freebsd32_fstatat(struct thread *td, struct freebsd32_fstatat_args *uap)
{
	struct stat ub;
	struct stat32 ub32;
	int error;

	error = kern_statat(td, uap->flag, uap->fd, uap->path, UIO_USERSPACE,
	    &ub, NULL);
	if (error)
		return (error);
	copy_stat(&ub, &ub32);
	error = copyout(&ub32, uap->buf, sizeof(ub32));
	return (error);
}

#ifdef COMPAT_43
int
ofreebsd32_lstat(struct thread *td, struct ofreebsd32_lstat_args *uap)
{
	struct stat sb;
	struct ostat32 sb32;
	int error;

	error = kern_statat(td, AT_SYMLINK_NOFOLLOW, AT_FDCWD, uap->path,
	    UIO_USERSPACE, &sb, NULL);
	if (error)
		return (error);
	copy_ostat(&sb, &sb32);
	error = copyout(&sb32, uap->ub, sizeof (sb32));
	return (error);
}
#endif

int
freebsd32_fhstat(struct thread *td, struct freebsd32_fhstat_args *uap)
{
	struct stat sb;
	struct stat32 sb32;
	struct fhandle fh;
	int error;

	error = copyin(uap->u_fhp, &fh, sizeof(fhandle_t));
        if (error != 0)
                return (error);
	error = kern_fhstat(td, fh, &sb);
	if (error != 0)
		return (error);
	copy_stat(&sb, &sb32);
	error = copyout(&sb32, uap->sb, sizeof (sb32));
	return (error);
}

#if defined(COMPAT_FREEBSD11)
extern int ino64_trunc_error;

static int
freebsd11_cvtstat32(struct stat *in, struct freebsd11_stat32 *out)
{

	CP(*in, *out, st_ino);
	if (in->st_ino != out->st_ino) {
		switch (ino64_trunc_error) {
		default:
		case 0:
			break;
		case 1:
			return (EOVERFLOW);
		case 2:
			out->st_ino = UINT32_MAX;
			break;
		}
	}
	CP(*in, *out, st_nlink);
	if (in->st_nlink != out->st_nlink) {
		switch (ino64_trunc_error) {
		default:
		case 0:
			break;
		case 1:
			return (EOVERFLOW);
		case 2:
			out->st_nlink = UINT16_MAX;
			break;
		}
	}
	out->st_dev = in->st_dev;
	if (out->st_dev != in->st_dev) {
		switch (ino64_trunc_error) {
		default:
			break;
		case 1:
			return (EOVERFLOW);
		}
	}
	CP(*in, *out, st_mode);
	CP(*in, *out, st_uid);
	CP(*in, *out, st_gid);
	out->st_rdev = in->st_rdev;
	if (out->st_rdev != in->st_rdev) {
		switch (ino64_trunc_error) {
		default:
			break;
		case 1:
			return (EOVERFLOW);
		}
	}
	TS_CP(*in, *out, st_atim);
	TS_CP(*in, *out, st_mtim);
	TS_CP(*in, *out, st_ctim);
	CP(*in, *out, st_size);
	CP(*in, *out, st_blocks);
	CP(*in, *out, st_blksize);
	CP(*in, *out, st_flags);
	CP(*in, *out, st_gen);
	TS_CP(*in, *out, st_birthtim);
	out->st_lspare = 0;
	bzero((char *)&out->st_birthtim + sizeof(out->st_birthtim),
	    sizeof(*out) - offsetof(struct freebsd11_stat32,
	    st_birthtim) - sizeof(out->st_birthtim));
	return (0);
}

int
freebsd11_freebsd32_stat(struct thread *td,
    struct freebsd11_freebsd32_stat_args *uap)
{
	struct stat sb;
	struct freebsd11_stat32 sb32;
	int error;

	error = kern_statat(td, 0, AT_FDCWD, uap->path, UIO_USERSPACE,
	    &sb, NULL);
	if (error != 0)
		return (error);
	error = freebsd11_cvtstat32(&sb, &sb32);
	if (error == 0)
		error = copyout(&sb32, uap->ub, sizeof (sb32));
	return (error);
}

int
freebsd11_freebsd32_fstat(struct thread *td,
    struct freebsd11_freebsd32_fstat_args *uap)
{
	struct stat sb;
	struct freebsd11_stat32 sb32;
	int error;

	error = kern_fstat(td, uap->fd, &sb);
	if (error != 0)
		return (error);
	error = freebsd11_cvtstat32(&sb, &sb32);
	if (error == 0)
		error = copyout(&sb32, uap->ub, sizeof (sb32));
	return (error);
}

int
freebsd11_freebsd32_fstatat(struct thread *td,
    struct freebsd11_freebsd32_fstatat_args *uap)
{
	struct stat sb;
	struct freebsd11_stat32 sb32;
	int error;

	error = kern_statat(td, uap->flag, uap->fd, uap->path, UIO_USERSPACE,
	    &sb, NULL);
	if (error != 0)
		return (error);
	error = freebsd11_cvtstat32(&sb, &sb32);
	if (error == 0)
		error = copyout(&sb32, uap->buf, sizeof (sb32));
	return (error);
}

int
freebsd11_freebsd32_lstat(struct thread *td,
    struct freebsd11_freebsd32_lstat_args *uap)
{
	struct stat sb;
	struct freebsd11_stat32 sb32;
	int error;

	error = kern_statat(td, AT_SYMLINK_NOFOLLOW, AT_FDCWD, uap->path,
	    UIO_USERSPACE, &sb, NULL);
	if (error != 0)
		return (error);
	error = freebsd11_cvtstat32(&sb, &sb32);
	if (error == 0)
		error = copyout(&sb32, uap->ub, sizeof (sb32));
	return (error);
}

int
freebsd11_freebsd32_fhstat(struct thread *td,
    struct freebsd11_freebsd32_fhstat_args *uap)
{
	struct stat sb;
	struct freebsd11_stat32 sb32;
	struct fhandle fh;
	int error;

	error = copyin(uap->u_fhp, &fh, sizeof(fhandle_t));
        if (error != 0)
                return (error);
	error = kern_fhstat(td, fh, &sb);
	if (error != 0)
		return (error);
	error = freebsd11_cvtstat32(&sb, &sb32);
	if (error == 0)
		error = copyout(&sb32, uap->sb, sizeof (sb32));
	return (error);
}
#endif

int
freebsd32___sysctl(struct thread *td, struct freebsd32___sysctl_args *uap)
{
	int error, name[CTL_MAXNAME];
	size_t j, oldlen;
	uint32_t tmp;

	if (uap->namelen > CTL_MAXNAME || uap->namelen < 2)
		return (EINVAL);
 	error = copyin(uap->name, name, uap->namelen * sizeof(int));
 	if (error)
		return (error);
	if (uap->oldlenp) {
		error = fueword32(uap->oldlenp, &tmp);
		oldlen = tmp;
	} else {
		oldlen = 0;
	}
	if (error != 0)
		return (EFAULT);
	error = userland_sysctl(td, name, uap->namelen,
		uap->old, &oldlen, 1,
		uap->new, uap->newlen, &j, SCTL_MASK32);
	if (error)
		return (error);
	if (uap->oldlenp)
		suword32(uap->oldlenp, j);
	return (0);
}

int
freebsd32_jail(struct thread *td, struct freebsd32_jail_args *uap)
{
	uint32_t version;
	int error;
	struct jail j;

	error = copyin(uap->jail, &version, sizeof(uint32_t));
	if (error)
		return (error);

	switch (version) {
	case 0:
	{
		/* FreeBSD single IPv4 jails. */
		struct jail32_v0 j32_v0;

		bzero(&j, sizeof(struct jail));
		error = copyin(uap->jail, &j32_v0, sizeof(struct jail32_v0));
		if (error)
			return (error);
		CP(j32_v0, j, version);
		PTRIN_CP(j32_v0, j, path);
		PTRIN_CP(j32_v0, j, hostname);
		j.ip4s = htonl(j32_v0.ip_number);	/* jail_v0 is host order */
		break;
	}

	case 1:
		/*
		 * Version 1 was used by multi-IPv4 jail implementations
		 * that never made it into the official kernel.
		 */
		return (EINVAL);

	case 2:	/* JAIL_API_VERSION */
	{
		/* FreeBSD multi-IPv4/IPv6,noIP jails. */
		struct jail32 j32;

		error = copyin(uap->jail, &j32, sizeof(struct jail32));
		if (error)
			return (error);
		CP(j32, j, version);
		PTRIN_CP(j32, j, path);
		PTRIN_CP(j32, j, hostname);
		PTRIN_CP(j32, j, jailname);
		CP(j32, j, ip4s);
		CP(j32, j, ip6s);
		PTRIN_CP(j32, j, ip4);
		PTRIN_CP(j32, j, ip6);
		break;
	}

	default:
		/* Sci-Fi jails are not supported, sorry. */
		return (EINVAL);
	}
	return (kern_jail(td, &j));
}

int
freebsd32_jail_set(struct thread *td, struct freebsd32_jail_set_args *uap)
{
	struct uio *auio;
	int error;

	/* Check that we have an even number of iovecs. */
	if (uap->iovcnt & 1)
		return (EINVAL);

	error = freebsd32_copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_jail_set(td, auio, uap->flags);
	free(auio, M_IOV);
	return (error);
}

int
freebsd32_jail_get(struct thread *td, struct freebsd32_jail_get_args *uap)
{
	struct iovec32 iov32;
	struct uio *auio;
	int error, i;

	/* Check that we have an even number of iovecs. */
	if (uap->iovcnt & 1)
		return (EINVAL);

	error = freebsd32_copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_jail_get(td, auio, uap->flags);
	if (error == 0)
		for (i = 0; i < uap->iovcnt; i++) {
			PTROUT_CP(auio->uio_iov[i], iov32, iov_base);
			CP(auio->uio_iov[i], iov32, iov_len);
			error = copyout(&iov32, uap->iovp + i, sizeof(iov32));
			if (error != 0)
				break;
		}
	free(auio, M_IOV);
	return (error);
}

int
freebsd32_sigaction(struct thread *td, struct freebsd32_sigaction_args *uap)
{
	struct sigaction32 s32;
	struct sigaction sa, osa, *sap;
	int error;

	if (uap->act) {
		error = copyin(uap->act, &s32, sizeof(s32));
		if (error)
			return (error);
		sa.sa_handler = PTRIN(s32.sa_u);
		CP(s32, sa, sa_flags);
		CP(s32, sa, sa_mask);
		sap = &sa;
	} else
		sap = NULL;
	error = kern_sigaction(td, uap->sig, sap, &osa, 0);
	if (error == 0 && uap->oact != NULL) {
		s32.sa_u = PTROUT(osa.sa_handler);
		CP(osa, s32, sa_flags);
		CP(osa, s32, sa_mask);
		error = copyout(&s32, uap->oact, sizeof(s32));
	}
	return (error);
}

#ifdef COMPAT_FREEBSD4
int
freebsd4_freebsd32_sigaction(struct thread *td,
			     struct freebsd4_freebsd32_sigaction_args *uap)
{
	struct sigaction32 s32;
	struct sigaction sa, osa, *sap;
	int error;

	if (uap->act) {
		error = copyin(uap->act, &s32, sizeof(s32));
		if (error)
			return (error);
		sa.sa_handler = PTRIN(s32.sa_u);
		CP(s32, sa, sa_flags);
		CP(s32, sa, sa_mask);
		sap = &sa;
	} else
		sap = NULL;
	error = kern_sigaction(td, uap->sig, sap, &osa, KSA_FREEBSD4);
	if (error == 0 && uap->oact != NULL) {
		s32.sa_u = PTROUT(osa.sa_handler);
		CP(osa, s32, sa_flags);
		CP(osa, s32, sa_mask);
		error = copyout(&s32, uap->oact, sizeof(s32));
	}
	return (error);
}
#endif

#ifdef COMPAT_43
struct osigaction32 {
	u_int32_t	sa_u;
	osigset_t	sa_mask;
	int		sa_flags;
};

#define	ONSIG	32

int
ofreebsd32_sigaction(struct thread *td,
			     struct ofreebsd32_sigaction_args *uap)
{
	struct osigaction32 s32;
	struct sigaction sa, osa, *sap;
	int error;

	if (uap->signum <= 0 || uap->signum >= ONSIG)
		return (EINVAL);

	if (uap->nsa) {
		error = copyin(uap->nsa, &s32, sizeof(s32));
		if (error)
			return (error);
		sa.sa_handler = PTRIN(s32.sa_u);
		CP(s32, sa, sa_flags);
		OSIG2SIG(s32.sa_mask, sa.sa_mask);
		sap = &sa;
	} else
		sap = NULL;
	error = kern_sigaction(td, uap->signum, sap, &osa, KSA_OSIGSET);
	if (error == 0 && uap->osa != NULL) {
		s32.sa_u = PTROUT(osa.sa_handler);
		CP(osa, s32, sa_flags);
		SIG2OSIG(osa.sa_mask, s32.sa_mask);
		error = copyout(&s32, uap->osa, sizeof(s32));
	}
	return (error);
}

int
ofreebsd32_sigprocmask(struct thread *td,
			       struct ofreebsd32_sigprocmask_args *uap)
{
	sigset_t set, oset;
	int error;

	OSIG2SIG(uap->mask, set);
	error = kern_sigprocmask(td, uap->how, &set, &oset, SIGPROCMASK_OLD);
	SIG2OSIG(oset, td->td_retval[0]);
	return (error);
}

int
ofreebsd32_sigpending(struct thread *td,
			      struct ofreebsd32_sigpending_args *uap)
{
	struct proc *p = td->td_proc;
	sigset_t siglist;

	PROC_LOCK(p);
	siglist = p->p_siglist;
	SIGSETOR(siglist, td->td_siglist);
	PROC_UNLOCK(p);
	SIG2OSIG(siglist, td->td_retval[0]);
	return (0);
}

struct sigvec32 {
	u_int32_t	sv_handler;
	int		sv_mask;
	int		sv_flags;
};

int
ofreebsd32_sigvec(struct thread *td,
			  struct ofreebsd32_sigvec_args *uap)
{
	struct sigvec32 vec;
	struct sigaction sa, osa, *sap;
	int error;

	if (uap->signum <= 0 || uap->signum >= ONSIG)
		return (EINVAL);

	if (uap->nsv) {
		error = copyin(uap->nsv, &vec, sizeof(vec));
		if (error)
			return (error);
		sa.sa_handler = PTRIN(vec.sv_handler);
		OSIG2SIG(vec.sv_mask, sa.sa_mask);
		sa.sa_flags = vec.sv_flags;
		sa.sa_flags ^= SA_RESTART;
		sap = &sa;
	} else
		sap = NULL;
	error = kern_sigaction(td, uap->signum, sap, &osa, KSA_OSIGSET);
	if (error == 0 && uap->osv != NULL) {
		vec.sv_handler = PTROUT(osa.sa_handler);
		SIG2OSIG(osa.sa_mask, vec.sv_mask);
		vec.sv_flags = osa.sa_flags;
		vec.sv_flags &= ~SA_NOCLDWAIT;
		vec.sv_flags ^= SA_RESTART;
		error = copyout(&vec, uap->osv, sizeof(vec));
	}
	return (error);
}

int
ofreebsd32_sigblock(struct thread *td,
			    struct ofreebsd32_sigblock_args *uap)
{
	sigset_t set, oset;

	OSIG2SIG(uap->mask, set);
	kern_sigprocmask(td, SIG_BLOCK, &set, &oset, 0);
	SIG2OSIG(oset, td->td_retval[0]);
	return (0);
}

int
ofreebsd32_sigsetmask(struct thread *td,
			      struct ofreebsd32_sigsetmask_args *uap)
{
	sigset_t set, oset;

	OSIG2SIG(uap->mask, set);
	kern_sigprocmask(td, SIG_SETMASK, &set, &oset, 0);
	SIG2OSIG(oset, td->td_retval[0]);
	return (0);
}

int
ofreebsd32_sigsuspend(struct thread *td,
			      struct ofreebsd32_sigsuspend_args *uap)
{
	sigset_t mask;

	OSIG2SIG(uap->mask, mask);
	return (kern_sigsuspend(td, mask));
}

struct sigstack32 {
	u_int32_t	ss_sp;
	int		ss_onstack;
};

int
ofreebsd32_sigstack(struct thread *td,
			    struct ofreebsd32_sigstack_args *uap)
{
	struct sigstack32 s32;
	struct sigstack nss, oss;
	int error = 0, unss;

	if (uap->nss != NULL) {
		error = copyin(uap->nss, &s32, sizeof(s32));
		if (error)
			return (error);
		nss.ss_sp = PTRIN(s32.ss_sp);
		CP(s32, nss, ss_onstack);
		unss = 1;
	} else {
		unss = 0;
	}
	oss.ss_sp = td->td_sigstk.ss_sp;
	oss.ss_onstack = sigonstack(cpu_getstack(td));
	if (unss) {
		td->td_sigstk.ss_sp = nss.ss_sp;
		td->td_sigstk.ss_size = 0;
		td->td_sigstk.ss_flags |= (nss.ss_onstack & SS_ONSTACK);
		td->td_pflags |= TDP_ALTSTACK;
	}
	if (uap->oss != NULL) {
		s32.ss_sp = PTROUT(oss.ss_sp);
		CP(oss, s32, ss_onstack);
		error = copyout(&s32, uap->oss, sizeof(s32));
	}
	return (error);
}
#endif

int
freebsd32_nanosleep(struct thread *td, struct freebsd32_nanosleep_args *uap)
{

	return (freebsd32_user_clock_nanosleep(td, CLOCK_REALTIME,
	    TIMER_RELTIME, uap->rqtp, uap->rmtp));
}

int
freebsd32_clock_nanosleep(struct thread *td,
    struct freebsd32_clock_nanosleep_args *uap)
{
	int error;

	error = freebsd32_user_clock_nanosleep(td, uap->clock_id, uap->flags,
	    uap->rqtp, uap->rmtp);
	return (kern_posix_error(td, error));
}

static int
freebsd32_user_clock_nanosleep(struct thread *td, clockid_t clock_id,
    int flags, const struct timespec32 *ua_rqtp, struct timespec32 *ua_rmtp)
{
	struct timespec32 rmt32, rqt32;
	struct timespec rmt, rqt;
	int error;

	error = copyin(ua_rqtp, &rqt32, sizeof(rqt32));
	if (error)
		return (error);

	CP(rqt32, rqt, tv_sec);
	CP(rqt32, rqt, tv_nsec);

	if (ua_rmtp != NULL && (flags & TIMER_ABSTIME) == 0 &&
	    !useracc(ua_rmtp, sizeof(rmt32), VM_PROT_WRITE))
		return (EFAULT);
	error = kern_clock_nanosleep(td, clock_id, flags, &rqt, &rmt);
	if (error == EINTR && ua_rmtp != NULL && (flags & TIMER_ABSTIME) == 0) {
		int error2;

		CP(rmt, rmt32, tv_sec);
		CP(rmt, rmt32, tv_nsec);

		error2 = copyout(&rmt32, ua_rmtp, sizeof(rmt32));
		if (error2)
			error = error2;
	}
	return (error);
}

int
freebsd32_clock_gettime(struct thread *td,
			struct freebsd32_clock_gettime_args *uap)
{
	struct timespec	ats;
	struct timespec32 ats32;
	int error;

	error = kern_clock_gettime(td, uap->clock_id, &ats);
	if (error == 0) {
		CP(ats, ats32, tv_sec);
		CP(ats, ats32, tv_nsec);
		error = copyout(&ats32, uap->tp, sizeof(ats32));
	}
	return (error);
}

int
freebsd32_clock_settime(struct thread *td,
			struct freebsd32_clock_settime_args *uap)
{
	struct timespec	ats;
	struct timespec32 ats32;
	int error;

	error = copyin(uap->tp, &ats32, sizeof(ats32));
	if (error)
		return (error);
	CP(ats32, ats, tv_sec);
	CP(ats32, ats, tv_nsec);

	return (kern_clock_settime(td, uap->clock_id, &ats));
}

int
freebsd32_clock_getres(struct thread *td,
		       struct freebsd32_clock_getres_args *uap)
{
	struct timespec	ts;
	struct timespec32 ts32;
	int error;

	if (uap->tp == NULL)
		return (0);
	error = kern_clock_getres(td, uap->clock_id, &ts);
	if (error == 0) {
		CP(ts, ts32, tv_sec);
		CP(ts, ts32, tv_nsec);
		error = copyout(&ts32, uap->tp, sizeof(ts32));
	}
	return (error);
}

int freebsd32_ktimer_create(struct thread *td,
    struct freebsd32_ktimer_create_args *uap)
{
	struct sigevent32 ev32;
	struct sigevent ev, *evp;
	int error, id;

	if (uap->evp == NULL) {
		evp = NULL;
	} else {
		evp = &ev;
		error = copyin(uap->evp, &ev32, sizeof(ev32));
		if (error != 0)
			return (error);
		error = convert_sigevent32(&ev32, &ev);
		if (error != 0)
			return (error);
	}
	error = kern_ktimer_create(td, uap->clock_id, evp, &id, -1);
	if (error == 0) {
		error = copyout(&id, uap->timerid, sizeof(int));
		if (error != 0)
			kern_ktimer_delete(td, id);
	}
	return (error);
}

int
freebsd32_ktimer_settime(struct thread *td,
    struct freebsd32_ktimer_settime_args *uap)
{
	struct itimerspec32 val32, oval32;
	struct itimerspec val, oval, *ovalp;
	int error;

	error = copyin(uap->value, &val32, sizeof(val32));
	if (error != 0)
		return (error);
	ITS_CP(val32, val);
	ovalp = uap->ovalue != NULL ? &oval : NULL;
	error = kern_ktimer_settime(td, uap->timerid, uap->flags, &val, ovalp);
	if (error == 0 && uap->ovalue != NULL) {
		ITS_CP(oval, oval32);
		error = copyout(&oval32, uap->ovalue, sizeof(oval32));
	}
	return (error);
}

int
freebsd32_ktimer_gettime(struct thread *td,
    struct freebsd32_ktimer_gettime_args *uap)
{
	struct itimerspec32 val32;
	struct itimerspec val;
	int error;

	error = kern_ktimer_gettime(td, uap->timerid, &val);
	if (error == 0) {
		ITS_CP(val, val32);
		error = copyout(&val32, uap->value, sizeof(val32));
	}
	return (error);
}

int
freebsd32_clock_getcpuclockid2(struct thread *td,
    struct freebsd32_clock_getcpuclockid2_args *uap)
{
	clockid_t clk_id;
	int error;

	error = kern_clock_getcpuclockid2(td, PAIR32TO64(id_t, uap->id),
	    uap->which, &clk_id);
	if (error == 0)
		error = copyout(&clk_id, uap->clock_id, sizeof(clockid_t));
	return (error);
}

int
freebsd32_thr_new(struct thread *td,
		  struct freebsd32_thr_new_args *uap)
{
	struct thr_param32 param32;
	struct thr_param param;
	int error;

	if (uap->param_size < 0 ||
	    uap->param_size > sizeof(struct thr_param32))
		return (EINVAL);
	bzero(&param, sizeof(struct thr_param));
	bzero(&param32, sizeof(struct thr_param32));
	error = copyin(uap->param, &param32, uap->param_size);
	if (error != 0)
		return (error);
	param.start_func = PTRIN(param32.start_func);
	param.arg = PTRIN(param32.arg);
	param.stack_base = PTRIN(param32.stack_base);
	param.stack_size = param32.stack_size;
	param.tls_base = PTRIN(param32.tls_base);
	param.tls_size = param32.tls_size;
	param.child_tid = PTRIN(param32.child_tid);
	param.parent_tid = PTRIN(param32.parent_tid);
	param.flags = param32.flags;
	param.rtp = PTRIN(param32.rtp);
	param.spare[0] = PTRIN(param32.spare[0]);
	param.spare[1] = PTRIN(param32.spare[1]);
	param.spare[2] = PTRIN(param32.spare[2]);

	return (kern_thr_new(td, &param));
}

int
freebsd32_thr_suspend(struct thread *td, struct freebsd32_thr_suspend_args *uap)
{
	struct timespec32 ts32;
	struct timespec ts, *tsp;
	int error;

	error = 0;
	tsp = NULL;
	if (uap->timeout != NULL) {
		error = copyin((const void *)uap->timeout, (void *)&ts32,
		    sizeof(struct timespec32));
		if (error != 0)
			return (error);
		ts.tv_sec = ts32.tv_sec;
		ts.tv_nsec = ts32.tv_nsec;
		tsp = &ts;
	}
	return (kern_thr_suspend(td, tsp));
}

void
siginfo_to_siginfo32(const siginfo_t *src, struct siginfo32 *dst)
{
	bzero(dst, sizeof(*dst));
	dst->si_signo = src->si_signo;
	dst->si_errno = src->si_errno;
	dst->si_code = src->si_code;
	dst->si_pid = src->si_pid;
	dst->si_uid = src->si_uid;
	dst->si_status = src->si_status;
	dst->si_addr = (uintptr_t)src->si_addr;
	dst->si_value.sival_int = src->si_value.sival_int;
	dst->si_timerid = src->si_timerid;
	dst->si_overrun = src->si_overrun;
}

#ifndef _FREEBSD32_SYSPROTO_H_
struct freebsd32_sigqueue_args {
        pid_t pid;
        int signum;
        /* union sigval32 */ int value;
};
#endif
int
freebsd32_sigqueue(struct thread *td, struct freebsd32_sigqueue_args *uap)
{
	union sigval sv;

	/*
	 * On 32-bit ABIs, sival_int and sival_ptr are the same.
	 * On 64-bit little-endian ABIs, the low bits are the same.
	 * In 64-bit big-endian ABIs, sival_int overlaps with
	 * sival_ptr's HIGH bits.  We choose to support sival_int
	 * rather than sival_ptr in this case as it seems to be
	 * more common.
	 */
	bzero(&sv, sizeof(sv));
	sv.sival_int = uap->value;

	return (kern_sigqueue(td, uap->pid, uap->signum, &sv));
}

int
freebsd32_sigtimedwait(struct thread *td, struct freebsd32_sigtimedwait_args *uap)
{
	struct timespec32 ts32;
	struct timespec ts;
	struct timespec *timeout;
	sigset_t set;
	ksiginfo_t ksi;
	struct siginfo32 si32;
	int error;

	if (uap->timeout) {
		error = copyin(uap->timeout, &ts32, sizeof(ts32));
		if (error)
			return (error);
		ts.tv_sec = ts32.tv_sec;
		ts.tv_nsec = ts32.tv_nsec;
		timeout = &ts;
	} else
		timeout = NULL;

	error = copyin(uap->set, &set, sizeof(set));
	if (error)
		return (error);

	error = kern_sigtimedwait(td, set, &ksi, timeout);
	if (error)
		return (error);

	if (uap->info) {
		siginfo_to_siginfo32(&ksi.ksi_info, &si32);
		error = copyout(&si32, uap->info, sizeof(struct siginfo32));
	}

	if (error == 0)
		td->td_retval[0] = ksi.ksi_signo;
	return (error);
}

/*
 * MPSAFE
 */
int
freebsd32_sigwaitinfo(struct thread *td, struct freebsd32_sigwaitinfo_args *uap)
{
	ksiginfo_t ksi;
	struct siginfo32 si32;
	sigset_t set;
	int error;

	error = copyin(uap->set, &set, sizeof(set));
	if (error)
		return (error);

	error = kern_sigtimedwait(td, set, &ksi, NULL);
	if (error)
		return (error);

	if (uap->info) {
		siginfo_to_siginfo32(&ksi.ksi_info, &si32);
		error = copyout(&si32, uap->info, sizeof(struct siginfo32));
	}	
	if (error == 0)
		td->td_retval[0] = ksi.ksi_signo;
	return (error);
}

int
freebsd32_cpuset_setid(struct thread *td,
    struct freebsd32_cpuset_setid_args *uap)
{

	return (kern_cpuset_setid(td, uap->which,
	    PAIR32TO64(id_t, uap->id), uap->setid));
}

int
freebsd32_cpuset_getid(struct thread *td,
    struct freebsd32_cpuset_getid_args *uap)
{

	return (kern_cpuset_getid(td, uap->level, uap->which,
	    PAIR32TO64(id_t, uap->id), uap->setid));
}

int
freebsd32_cpuset_getaffinity(struct thread *td,
    struct freebsd32_cpuset_getaffinity_args *uap)
{

	return (kern_cpuset_getaffinity(td, uap->level, uap->which,
	    PAIR32TO64(id_t,uap->id), uap->cpusetsize, uap->mask));
}

int
freebsd32_cpuset_setaffinity(struct thread *td,
    struct freebsd32_cpuset_setaffinity_args *uap)
{

	return (kern_cpuset_setaffinity(td, uap->level, uap->which,
	    PAIR32TO64(id_t,uap->id), uap->cpusetsize, uap->mask));
}

int
freebsd32_cpuset_getdomain(struct thread *td,
    struct freebsd32_cpuset_getdomain_args *uap)
{

	return (kern_cpuset_getdomain(td, uap->level, uap->which,
	    PAIR32TO64(id_t,uap->id), uap->domainsetsize, uap->mask, uap->policy));
}

int
freebsd32_cpuset_setdomain(struct thread *td,
    struct freebsd32_cpuset_setdomain_args *uap)
{

	return (kern_cpuset_setdomain(td, uap->level, uap->which,
	    PAIR32TO64(id_t,uap->id), uap->domainsetsize, uap->mask, uap->policy));
}

int
freebsd32_nmount(struct thread *td,
    struct freebsd32_nmount_args /* {
    	struct iovec *iovp;
    	unsigned int iovcnt;
    	int flags;
    } */ *uap)
{
	struct uio *auio;
	uint64_t flags;
	int error;

	/*
	 * Mount flags are now 64-bits. On 32-bit archtectures only
	 * 32-bits are passed in, but from here on everything handles
	 * 64-bit flags correctly.
	 */
	flags = uap->flags;

	AUDIT_ARG_FFLAGS(flags);

	/*
	 * Filter out MNT_ROOTFS.  We do not want clients of nmount() in
	 * userspace to set this flag, but we must filter it out if we want
	 * MNT_UPDATE on the root file system to work.
	 * MNT_ROOTFS should only be set by the kernel when mounting its
	 * root file system.
	 */
	flags &= ~MNT_ROOTFS;

	/*
	 * check that we have an even number of iovec's
	 * and that we have at least two options.
	 */
	if ((uap->iovcnt & 1) || (uap->iovcnt < 4))
		return (EINVAL);

	error = freebsd32_copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = vfs_donmount(td, flags, auio);

	free(auio, M_IOV);
	return error;
}

#if 0
int
freebsd32_xxx(struct thread *td, struct freebsd32_xxx_args *uap)
{
	struct yyy32 *p32, s32;
	struct yyy *p = NULL, s;
	struct xxx_arg ap;
	int error;

	if (uap->zzz) {
		error = copyin(uap->zzz, &s32, sizeof(s32));
		if (error)
			return (error);
		/* translate in */
		p = &s;
	}
	error = kern_xxx(td, p);
	if (error)
		return (error);
	if (uap->zzz) {
		/* translate out */
		error = copyout(&s32, p32, sizeof(s32));
	}
	return (error);
}
#endif

int
syscall32_module_handler(struct module *mod, int what, void *arg)
{

	return (kern_syscall_module_handler(freebsd32_sysent, mod, what, arg));
}

int
syscall32_helper_register(struct syscall_helper_data *sd, int flags)
{

	return (kern_syscall_helper_register(freebsd32_sysent, sd, flags));
}

int
syscall32_helper_unregister(struct syscall_helper_data *sd)
{

	return (kern_syscall_helper_unregister(freebsd32_sysent, sd));
}

register_t *
freebsd32_copyout_strings(struct image_params *imgp)
{
	int argc, envc, i;
	u_int32_t *vectp;
	char *stringp;
	uintptr_t destp;
	u_int32_t *stack_base;
	struct freebsd32_ps_strings *arginfo;
	char canary[sizeof(long) * 8];
	int32_t pagesizes32[MAXPAGESIZES];
	size_t execpath_len;
	int szsigcode;

	/*
	 * Calculate string base and vector table pointers.
	 * Also deal with signal trampoline code for this exec type.
	 */
	if (imgp->execpath != NULL && imgp->auxargs != NULL)
		execpath_len = strlen(imgp->execpath) + 1;
	else
		execpath_len = 0;
	arginfo = (struct freebsd32_ps_strings *)curproc->p_sysent->
	    sv_psstrings;
	if (imgp->proc->p_sysent->sv_sigcode_base == 0)
		szsigcode = *(imgp->proc->p_sysent->sv_szsigcode);
	else
		szsigcode = 0;
	destp =	(uintptr_t)arginfo;

	/*
	 * install sigcode
	 */
	if (szsigcode != 0) {
		destp -= szsigcode;
		destp = rounddown2(destp, sizeof(uint32_t));
		copyout(imgp->proc->p_sysent->sv_sigcode, (void *)destp,
		    szsigcode);
	}

	/*
	 * Copy the image path for the rtld.
	 */
	if (execpath_len != 0) {
		destp -= execpath_len;
		imgp->execpathp = destp;
		copyout(imgp->execpath, (void *)destp, execpath_len);
	}

	/*
	 * Prepare the canary for SSP.
	 */
	arc4rand(canary, sizeof(canary), 0);
	destp -= sizeof(canary);
	imgp->canary = destp;
	copyout(canary, (void *)destp, sizeof(canary));
	imgp->canarylen = sizeof(canary);

	/*
	 * Prepare the pagesizes array.
	 */
	for (i = 0; i < MAXPAGESIZES; i++)
		pagesizes32[i] = (uint32_t)pagesizes[i];
	destp -= sizeof(pagesizes32);
	destp = rounddown2(destp, sizeof(uint32_t));
	imgp->pagesizes = destp;
	copyout(pagesizes32, (void *)destp, sizeof(pagesizes32));
	imgp->pagesizeslen = sizeof(pagesizes32);

	destp -= ARG_MAX - imgp->args->stringspace;
	destp = rounddown2(destp, sizeof(uint32_t));

	vectp = (uint32_t *)destp;
	if (imgp->auxargs) {
		/*
		 * Allocate room on the stack for the ELF auxargs
		 * array.  It has up to AT_COUNT entries.
		 */
		vectp -= howmany(AT_COUNT * sizeof(Elf32_Auxinfo),
		    sizeof(*vectp));
	}

	/*
	 * Allocate room for the argv[] and env vectors including the
	 * terminating NULL pointers.
	 */
	vectp -= imgp->args->argc + 1 + imgp->args->envc + 1;

	/*
	 * vectp also becomes our initial stack base
	 */
	stack_base = vectp;

	stringp = imgp->args->begin_argv;
	argc = imgp->args->argc;
	envc = imgp->args->envc;
	/*
	 * Copy out strings - arguments and environment.
	 */
	copyout(stringp, (void *)destp, ARG_MAX - imgp->args->stringspace);

	/*
	 * Fill in "ps_strings" struct for ps, w, etc.
	 */
	suword32(&arginfo->ps_argvstr, (u_int32_t)(intptr_t)vectp);
	suword32(&arginfo->ps_nargvstr, argc);

	/*
	 * Fill in argument portion of vector table.
	 */
	for (; argc > 0; --argc) {
		suword32(vectp++, (u_int32_t)(intptr_t)destp);
		while (*stringp++ != 0)
			destp++;
		destp++;
	}

	/* a null vector table pointer separates the argp's from the envp's */
	suword32(vectp++, 0);

	suword32(&arginfo->ps_envstr, (u_int32_t)(intptr_t)vectp);
	suword32(&arginfo->ps_nenvstr, envc);

	/*
	 * Fill in environment portion of vector table.
	 */
	for (; envc > 0; --envc) {
		suword32(vectp++, (u_int32_t)(intptr_t)destp);
		while (*stringp++ != 0)
			destp++;
		destp++;
	}

	/* end of vector table is a null pointer */
	suword32(vectp, 0);

	return ((register_t *)stack_base);
}

int
freebsd32_kldstat(struct thread *td, struct freebsd32_kldstat_args *uap)
{
	struct kld_file_stat *stat;
	struct kld32_file_stat *stat32;
	int error, version;

	if ((error = copyin(&uap->stat->version, &version, sizeof(version)))
	    != 0)
		return (error);
	if (version != sizeof(struct kld32_file_stat_1) &&
	    version != sizeof(struct kld32_file_stat))
		return (EINVAL);

	stat = malloc(sizeof(*stat), M_TEMP, M_WAITOK | M_ZERO);
	stat32 = malloc(sizeof(*stat32), M_TEMP, M_WAITOK | M_ZERO);
	error = kern_kldstat(td, uap->fileid, stat);
	if (error == 0) {
		bcopy(&stat->name[0], &stat32->name[0], sizeof(stat->name));
		CP(*stat, *stat32, refs);
		CP(*stat, *stat32, id);
		PTROUT_CP(*stat, *stat32, address);
		CP(*stat, *stat32, size);
		bcopy(&stat->pathname[0], &stat32->pathname[0],
		    sizeof(stat->pathname));
		stat32->version  = version;
		error = copyout(stat32, uap->stat, version);
	}
	free(stat, M_TEMP);
	free(stat32, M_TEMP);
	return (error);
}

int
freebsd32_posix_fallocate(struct thread *td,
    struct freebsd32_posix_fallocate_args *uap)
{
	int error;

	error = kern_posix_fallocate(td, uap->fd,
	    PAIR32TO64(off_t, uap->offset), PAIR32TO64(off_t, uap->len));
	return (kern_posix_error(td, error));
}

int
freebsd32_posix_fadvise(struct thread *td,
    struct freebsd32_posix_fadvise_args *uap)
{
	int error;

	error = kern_posix_fadvise(td, uap->fd, PAIR32TO64(off_t, uap->offset),
	    PAIR32TO64(off_t, uap->len), uap->advice);
	return (kern_posix_error(td, error));
}

int
convert_sigevent32(struct sigevent32 *sig32, struct sigevent *sig)
{

	CP(*sig32, *sig, sigev_notify);
	switch (sig->sigev_notify) {
	case SIGEV_NONE:
		break;
	case SIGEV_THREAD_ID:
		CP(*sig32, *sig, sigev_notify_thread_id);
		/* FALLTHROUGH */
	case SIGEV_SIGNAL:
		CP(*sig32, *sig, sigev_signo);
		PTRIN_CP(*sig32, *sig, sigev_value.sival_ptr);
		break;
	case SIGEV_KEVENT:
		CP(*sig32, *sig, sigev_notify_kqueue);
		CP(*sig32, *sig, sigev_notify_kevent_flags);
		PTRIN_CP(*sig32, *sig, sigev_value.sival_ptr);
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

int
freebsd32_procctl(struct thread *td, struct freebsd32_procctl_args *uap)
{
	void *data;
	union {
		struct procctl_reaper_status rs;
		struct procctl_reaper_pids rp;
		struct procctl_reaper_kill rk;
	} x;
	union {
		struct procctl_reaper_pids32 rp;
	} x32;
	int error, error1, flags, signum;

	if (uap->com >= PROC_PROCCTL_MD_MIN)
		return (cpu_procctl(td, uap->idtype, PAIR32TO64(id_t, uap->id),
		    uap->com, PTRIN(uap->data)));

	switch (uap->com) {
	case PROC_ASLR_CTL:
	case PROC_SPROTECT:
	case PROC_TRACE_CTL:
	case PROC_TRAPCAP_CTL:
		error = copyin(PTRIN(uap->data), &flags, sizeof(flags));
		if (error != 0)
			return (error);
		data = &flags;
		break;
	case PROC_REAP_ACQUIRE:
	case PROC_REAP_RELEASE:
		if (uap->data != NULL)
			return (EINVAL);
		data = NULL;
		break;
	case PROC_REAP_STATUS:
		data = &x.rs;
		break;
	case PROC_REAP_GETPIDS:
		error = copyin(uap->data, &x32.rp, sizeof(x32.rp));
		if (error != 0)
			return (error);
		CP(x32.rp, x.rp, rp_count);
		PTRIN_CP(x32.rp, x.rp, rp_pids);
		data = &x.rp;
		break;
	case PROC_REAP_KILL:
		error = copyin(uap->data, &x.rk, sizeof(x.rk));
		if (error != 0)
			return (error);
		data = &x.rk;
		break;
	case PROC_ASLR_STATUS:
	case PROC_TRACE_STATUS:
	case PROC_TRAPCAP_STATUS:
		data = &flags;
		break;
	case PROC_PDEATHSIG_CTL:
		error = copyin(uap->data, &signum, sizeof(signum));
		if (error != 0)
			return (error);
		data = &signum;
		break;
	case PROC_PDEATHSIG_STATUS:
		data = &signum;
		break;
	default:
		return (EINVAL);
	}
	error = kern_procctl(td, uap->idtype, PAIR32TO64(id_t, uap->id),
	    uap->com, data);
	switch (uap->com) {
	case PROC_REAP_STATUS:
		if (error == 0)
			error = copyout(&x.rs, uap->data, sizeof(x.rs));
		break;
	case PROC_REAP_KILL:
		error1 = copyout(&x.rk, uap->data, sizeof(x.rk));
		if (error == 0)
			error = error1;
		break;
	case PROC_ASLR_STATUS:
	case PROC_TRACE_STATUS:
	case PROC_TRAPCAP_STATUS:
		if (error == 0)
			error = copyout(&flags, uap->data, sizeof(flags));
		break;
	case PROC_PDEATHSIG_STATUS:
		if (error == 0)
			error = copyout(&signum, uap->data, sizeof(signum));
		break;
	}
	return (error);
}

int
freebsd32_fcntl(struct thread *td, struct freebsd32_fcntl_args *uap)
{
	long tmp;

	switch (uap->cmd) {
	/*
	 * Do unsigned conversion for arg when operation
	 * interprets it as flags or pointer.
	 */
	case F_SETLK_REMOTE:
	case F_SETLKW:
	case F_SETLK:
	case F_GETLK:
	case F_SETFD:
	case F_SETFL:
	case F_OGETLK:
	case F_OSETLK:
	case F_OSETLKW:
		tmp = (unsigned int)(uap->arg);
		break;
	default:
		tmp = uap->arg;
		break;
	}
	return (kern_fcntl_freebsd(td, uap->fd, uap->cmd, tmp));
}

int
freebsd32_ppoll(struct thread *td, struct freebsd32_ppoll_args *uap)
{
	struct timespec32 ts32;
	struct timespec ts, *tsp;
	sigset_t set, *ssp;
	int error;

	if (uap->ts != NULL) {
		error = copyin(uap->ts, &ts32, sizeof(ts32));
		if (error != 0)
			return (error);
		CP(ts32, ts, tv_sec);
		CP(ts32, ts, tv_nsec);
		tsp = &ts;
	} else
		tsp = NULL;
	if (uap->set != NULL) {
		error = copyin(uap->set, &set, sizeof(set));
		if (error != 0)
			return (error);
		ssp = &set;
	} else
		ssp = NULL;

	return (kern_poll(td, uap->fds, uap->nfds, tsp, ssp));
}

int
freebsd32_sched_rr_get_interval(struct thread *td,
    struct freebsd32_sched_rr_get_interval_args *uap)
{
	struct timespec ts;
	struct timespec32 ts32;
	int error;

	error = kern_sched_rr_get_interval(td, uap->pid, &ts);
	if (error == 0) {
		CP(ts, ts32, tv_sec);
		CP(ts, ts32, tv_nsec);
		error = copyout(&ts32, uap->interval, sizeof(ts32));
	}
	return (error);
}
