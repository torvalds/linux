/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Marcel Moolenaar
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
#include <sys/capsicum.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/imgact.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/wait.h>

#include <machine/frame.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/sysarch.h>

#include <vm/pmap.h>
#include <vm/vm.h>
#include <vm/vm_map.h>

#include <i386/linux/linux.h>
#include <i386/linux/linux_proto.h>
#include <compat/linux/linux_emul.h>
#include <compat/linux/linux_ipc.h>
#include <compat/linux/linux_misc.h>
#include <compat/linux/linux_mmap.h>
#include <compat/linux/linux_signal.h>
#include <compat/linux/linux_util.h>

#include <i386/include/pcb.h>			/* needed for pcb definition in linux_set_thread_area */

#include "opt_posix.h"

extern struct sysentvec elf32_freebsd_sysvec;	/* defined in i386/i386/elf_machdep.c */

struct l_descriptor {
	l_uint		entry_number;
	l_ulong		base_addr;
	l_uint		limit;
	l_uint		seg_32bit:1;
	l_uint		contents:2;
	l_uint		read_exec_only:1;
	l_uint		limit_in_pages:1;
	l_uint		seg_not_present:1;
	l_uint		useable:1;
};

struct l_old_select_argv {
	l_int		nfds;
	l_fd_set	*readfds;
	l_fd_set	*writefds;
	l_fd_set	*exceptfds;
	struct l_timeval	*timeout;
};


int
linux_execve(struct thread *td, struct linux_execve_args *args)
{
	struct image_args eargs;
	char *newpath;
	int error;

	LCONVPATHEXIST(td, args->path, &newpath);

#ifdef DEBUG
	if (ldebug(execve))
		printf(ARGS(execve, "%s"), newpath);
#endif

	error = exec_copyin_args(&eargs, newpath, UIO_SYSSPACE,
	    args->argp, args->envp);
	free(newpath, M_TEMP);
	if (error == 0)
		error = linux_common_execve(td, &eargs);
	return (error);
}

struct l_ipc_kludge {
	struct l_msgbuf *msgp;
	l_long msgtyp;
};

int
linux_ipc(struct thread *td, struct linux_ipc_args *args)
{

	switch (args->what & 0xFFFF) {
	case LINUX_SEMOP: {
		struct linux_semop_args a;

		a.semid = args->arg1;
		a.tsops = PTRIN(args->ptr);
		a.nsops = args->arg2;
		return (linux_semop(td, &a));
	}
	case LINUX_SEMGET: {
		struct linux_semget_args a;

		a.key = args->arg1;
		a.nsems = args->arg2;
		a.semflg = args->arg3;
		return (linux_semget(td, &a));
	}
	case LINUX_SEMCTL: {
		struct linux_semctl_args a;
		int error;

		a.semid = args->arg1;
		a.semnum = args->arg2;
		a.cmd = args->arg3;
		error = copyin(PTRIN(args->ptr), &a.arg, sizeof(a.arg));
		if (error)
			return (error);
		return (linux_semctl(td, &a));
	}
	case LINUX_MSGSND: {
		struct linux_msgsnd_args a;

		a.msqid = args->arg1;
		a.msgp = PTRIN(args->ptr);
		a.msgsz = args->arg2;
		a.msgflg = args->arg3;
		return (linux_msgsnd(td, &a));
	}
	case LINUX_MSGRCV: {
		struct linux_msgrcv_args a;

		a.msqid = args->arg1;
		a.msgsz = args->arg2;
		a.msgflg = args->arg3;
		if ((args->what >> 16) == 0) {
			struct l_ipc_kludge tmp;
			int error;

			if (args->ptr == 0)
				return (EINVAL);
			error = copyin(PTRIN(args->ptr), &tmp, sizeof(tmp));
			if (error)
				return (error);
			a.msgp = PTRIN(tmp.msgp);
			a.msgtyp = tmp.msgtyp;
		} else {
			a.msgp = PTRIN(args->ptr);
			a.msgtyp = args->arg5;
		}
		return (linux_msgrcv(td, &a));
	}
	case LINUX_MSGGET: {
		struct linux_msgget_args a;

		a.key = args->arg1;
		a.msgflg = args->arg2;
		return (linux_msgget(td, &a));
	}
	case LINUX_MSGCTL: {
		struct linux_msgctl_args a;

		a.msqid = args->arg1;
		a.cmd = args->arg2;
		a.buf = PTRIN(args->ptr);
		return (linux_msgctl(td, &a));
	}
	case LINUX_SHMAT: {
		struct linux_shmat_args a;
		l_uintptr_t addr;
		int error;

		a.shmid = args->arg1;
		a.shmaddr = PTRIN(args->ptr);
		a.shmflg = args->arg2;
		error = linux_shmat(td, &a);
		if (error != 0)
			return (error);
		addr = td->td_retval[0];
		error = copyout(&addr, PTRIN(args->arg3), sizeof(addr));
		td->td_retval[0] = 0;
		return (error);
	}
	case LINUX_SHMDT: {
		struct linux_shmdt_args a;

		a.shmaddr = PTRIN(args->ptr);
		return (linux_shmdt(td, &a));
	}
	case LINUX_SHMGET: {
		struct linux_shmget_args a;

		a.key = args->arg1;
		a.size = args->arg2;
		a.shmflg = args->arg3;
		return (linux_shmget(td, &a));
	}
	case LINUX_SHMCTL: {
		struct linux_shmctl_args a;

		a.shmid = args->arg1;
		a.cmd = args->arg2;
		a.buf = PTRIN(args->ptr);
		return (linux_shmctl(td, &a));
	}
	default:
		break;
	}

	return (EINVAL);
}

int
linux_old_select(struct thread *td, struct linux_old_select_args *args)
{
	struct l_old_select_argv linux_args;
	struct linux_select_args newsel;
	int error;

#ifdef DEBUG
	if (ldebug(old_select))
		printf(ARGS(old_select, "%p"), args->ptr);
#endif

	error = copyin(args->ptr, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	newsel.nfds = linux_args.nfds;
	newsel.readfds = linux_args.readfds;
	newsel.writefds = linux_args.writefds;
	newsel.exceptfds = linux_args.exceptfds;
	newsel.timeout = linux_args.timeout;
	return (linux_select(td, &newsel));
}

int
linux_set_cloned_tls(struct thread *td, void *desc)
{
	struct segment_descriptor sd;
	struct l_user_desc info;
	int idx, error;
	int a[2];

	error = copyin(desc, &info, sizeof(struct l_user_desc));
	if (error) {
		printf(LMSG("copyin failed!"));
	} else {
		idx = info.entry_number;

		/*
		 * looks like we're getting the idx we returned
		 * in the set_thread_area() syscall
		 */
		if (idx != 6 && idx != 3) {
			printf(LMSG("resetting idx!"));
			idx = 3;
		}

		/* this doesnt happen in practice */
		if (idx == 6) {
			/* we might copy out the entry_number as 3 */
			info.entry_number = 3;
			error = copyout(&info, desc, sizeof(struct l_user_desc));
			if (error)
				printf(LMSG("copyout failed!"));
		}

		a[0] = LINUX_LDT_entry_a(&info);
		a[1] = LINUX_LDT_entry_b(&info);

		memcpy(&sd, &a, sizeof(a));
#ifdef DEBUG
		if (ldebug(clone))
			printf("Segment created in clone with "
			"CLONE_SETTLS: lobase: %x, hibase: %x, "
			"lolimit: %x, hilimit: %x, type: %i, "
			"dpl: %i, p: %i, xx: %i, def32: %i, "
			"gran: %i\n", sd.sd_lobase, sd.sd_hibase,
			sd.sd_lolimit, sd.sd_hilimit, sd.sd_type,
			sd.sd_dpl, sd.sd_p, sd.sd_xx,
			sd.sd_def32, sd.sd_gran);
#endif

		/* set %gs */
		td->td_pcb->pcb_gsd = sd;
		td->td_pcb->pcb_gs = GSEL(GUGS_SEL, SEL_UPL);
	}

	return (error);
}

int
linux_set_upcall_kse(struct thread *td, register_t stack)
{

	if (stack)
		td->td_frame->tf_esp = stack;

	/*
	 * The newly created Linux thread returns
	 * to the user space by the same path that a parent do.
	 */
	td->td_frame->tf_eax = 0;
	return (0);
}

int
linux_mmap2(struct thread *td, struct linux_mmap2_args *args)
{

#ifdef DEBUG
	if (ldebug(mmap2))
		printf(ARGS(mmap2, "%p, %d, %d, 0x%08x, %d, %d"),
		    (void *)args->addr, args->len, args->prot,
		    args->flags, args->fd, args->pgoff);
#endif

	return (linux_mmap_common(td, args->addr, args->len, args->prot,
		args->flags, args->fd, (uint64_t)(uint32_t)args->pgoff *
		PAGE_SIZE));
}

int
linux_mmap(struct thread *td, struct linux_mmap_args *args)
{
	int error;
	struct l_mmap_argv linux_args;

	error = copyin(args->ptr, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

#ifdef DEBUG
	if (ldebug(mmap))
		printf(ARGS(mmap, "%p, %d, %d, 0x%08x, %d, %d"),
		    (void *)linux_args.addr, linux_args.len, linux_args.prot,
		    linux_args.flags, linux_args.fd, linux_args.pgoff);
#endif

	return (linux_mmap_common(td, linux_args.addr, linux_args.len,
	    linux_args.prot, linux_args.flags, linux_args.fd,
	    (uint32_t)linux_args.pgoff));
}

int
linux_mprotect(struct thread *td, struct linux_mprotect_args *uap)
{

	return (linux_mprotect_common(td, PTROUT(uap->addr), uap->len, uap->prot));
}

int
linux_ioperm(struct thread *td, struct linux_ioperm_args *args)
{
	int error;
	struct i386_ioperm_args iia;

	iia.start = args->start;
	iia.length = args->length;
	iia.enable = args->enable;
	error = i386_set_ioperm(td, &iia);
	return (error);
}

int
linux_iopl(struct thread *td, struct linux_iopl_args *args)
{
	int error;

	if (args->level < 0 || args->level > 3)
		return (EINVAL);
	if ((error = priv_check(td, PRIV_IO)) != 0)
		return (error);
	if ((error = securelevel_gt(td->td_ucred, 0)) != 0)
		return (error);
	td->td_frame->tf_eflags = (td->td_frame->tf_eflags & ~PSL_IOPL) |
	    (args->level * (PSL_IOPL / 3));
	return (0);
}

int
linux_modify_ldt(struct thread *td, struct linux_modify_ldt_args *uap)
{
	int error;
	struct i386_ldt_args ldt;
	struct l_descriptor ld;
	union descriptor desc;
	int size, written;

	switch (uap->func) {
	case 0x00: /* read_ldt */
		ldt.start = 0;
		ldt.descs = uap->ptr;
		ldt.num = uap->bytecount / sizeof(union descriptor);
		error = i386_get_ldt(td, &ldt);
		td->td_retval[0] *= sizeof(union descriptor);
		break;
	case 0x02: /* read_default_ldt = 0 */
		size = 5*sizeof(struct l_desc_struct);
		if (size > uap->bytecount)
			size = uap->bytecount;
		for (written = error = 0; written < size && error == 0; written++)
			error = subyte((char *)uap->ptr + written, 0);
		td->td_retval[0] = written;
		break;
	case 0x01: /* write_ldt */
	case 0x11: /* write_ldt */
		if (uap->bytecount != sizeof(ld))
			return (EINVAL);

		error = copyin(uap->ptr, &ld, sizeof(ld));
		if (error)
			return (error);

		ldt.start = ld.entry_number;
		ldt.descs = &desc;
		ldt.num = 1;
		desc.sd.sd_lolimit = (ld.limit & 0x0000ffff);
		desc.sd.sd_hilimit = (ld.limit & 0x000f0000) >> 16;
		desc.sd.sd_lobase = (ld.base_addr & 0x00ffffff);
		desc.sd.sd_hibase = (ld.base_addr & 0xff000000) >> 24;
		desc.sd.sd_type = SDT_MEMRO | ((ld.read_exec_only ^ 1) << 1) |
			(ld.contents << 2);
		desc.sd.sd_dpl = 3;
		desc.sd.sd_p = (ld.seg_not_present ^ 1);
		desc.sd.sd_xx = 0;
		desc.sd.sd_def32 = ld.seg_32bit;
		desc.sd.sd_gran = ld.limit_in_pages;
		error = i386_set_ldt(td, &ldt, &desc);
		break;
	default:
		error = ENOSYS;
		break;
	}

	if (error == EOPNOTSUPP) {
		printf("linux: modify_ldt needs kernel option USER_LDT\n");
		error = ENOSYS;
	}

	return (error);
}

int
linux_sigaction(struct thread *td, struct linux_sigaction_args *args)
{
	l_osigaction_t osa;
	l_sigaction_t act, oact;
	int error;

#ifdef DEBUG
	if (ldebug(sigaction))
		printf(ARGS(sigaction, "%d, %p, %p"),
		    args->sig, (void *)args->nsa, (void *)args->osa);
#endif

	if (args->nsa != NULL) {
		error = copyin(args->nsa, &osa, sizeof(l_osigaction_t));
		if (error)
			return (error);
		act.lsa_handler = osa.lsa_handler;
		act.lsa_flags = osa.lsa_flags;
		act.lsa_restorer = osa.lsa_restorer;
		LINUX_SIGEMPTYSET(act.lsa_mask);
		act.lsa_mask.__mask = osa.lsa_mask;
	}

	error = linux_do_sigaction(td, args->sig, args->nsa ? &act : NULL,
	    args->osa ? &oact : NULL);

	if (args->osa != NULL && !error) {
		osa.lsa_handler = oact.lsa_handler;
		osa.lsa_flags = oact.lsa_flags;
		osa.lsa_restorer = oact.lsa_restorer;
		osa.lsa_mask = oact.lsa_mask.__mask;
		error = copyout(&osa, args->osa, sizeof(l_osigaction_t));
	}

	return (error);
}

/*
 * Linux has two extra args, restart and oldmask.  We dont use these,
 * but it seems that "restart" is actually a context pointer that
 * enables the signal to happen with a different register set.
 */
int
linux_sigsuspend(struct thread *td, struct linux_sigsuspend_args *args)
{
	sigset_t sigmask;
	l_sigset_t mask;

#ifdef DEBUG
	if (ldebug(sigsuspend))
		printf(ARGS(sigsuspend, "%08lx"), (unsigned long)args->mask);
#endif

	LINUX_SIGEMPTYSET(mask);
	mask.__mask = args->mask;
	linux_to_bsd_sigset(&mask, &sigmask);
	return (kern_sigsuspend(td, sigmask));
}

int
linux_rt_sigsuspend(struct thread *td, struct linux_rt_sigsuspend_args *uap)
{
	l_sigset_t lmask;
	sigset_t sigmask;
	int error;

#ifdef DEBUG
	if (ldebug(rt_sigsuspend))
		printf(ARGS(rt_sigsuspend, "%p, %d"),
		    (void *)uap->newset, uap->sigsetsize);
#endif

	if (uap->sigsetsize != sizeof(l_sigset_t))
		return (EINVAL);

	error = copyin(uap->newset, &lmask, sizeof(l_sigset_t));
	if (error)
		return (error);

	linux_to_bsd_sigset(&lmask, &sigmask);
	return (kern_sigsuspend(td, sigmask));
}

int
linux_pause(struct thread *td, struct linux_pause_args *args)
{
	struct proc *p = td->td_proc;
	sigset_t sigmask;

#ifdef DEBUG
	if (ldebug(pause))
		printf(ARGS(pause, ""));
#endif

	PROC_LOCK(p);
	sigmask = td->td_sigmask;
	PROC_UNLOCK(p);
	return (kern_sigsuspend(td, sigmask));
}

int
linux_sigaltstack(struct thread *td, struct linux_sigaltstack_args *uap)
{
	stack_t ss, oss;
	l_stack_t lss;
	int error;

#ifdef DEBUG
	if (ldebug(sigaltstack))
		printf(ARGS(sigaltstack, "%p, %p"), uap->uss, uap->uoss);
#endif

	if (uap->uss != NULL) {
		error = copyin(uap->uss, &lss, sizeof(l_stack_t));
		if (error)
			return (error);

		ss.ss_sp = lss.ss_sp;
		ss.ss_size = lss.ss_size;
		ss.ss_flags = linux_to_bsd_sigaltstack(lss.ss_flags);
	}
	error = kern_sigaltstack(td, (uap->uss != NULL) ? &ss : NULL,
	    (uap->uoss != NULL) ? &oss : NULL);
	if (!error && uap->uoss != NULL) {
		lss.ss_sp = oss.ss_sp;
		lss.ss_size = oss.ss_size;
		lss.ss_flags = bsd_to_linux_sigaltstack(oss.ss_flags);
		error = copyout(&lss, uap->uoss, sizeof(l_stack_t));
	}

	return (error);
}

int
linux_ftruncate64(struct thread *td, struct linux_ftruncate64_args *args)
{

#ifdef DEBUG
	if (ldebug(ftruncate64))
		printf(ARGS(ftruncate64, "%u, %jd"), args->fd,
		    (intmax_t)args->length);
#endif

	return (kern_ftruncate(td, args->fd, args->length));
}

int
linux_set_thread_area(struct thread *td, struct linux_set_thread_area_args *args)
{
	struct l_user_desc info;
	int error;
	int idx;
	int a[2];
	struct segment_descriptor sd;

	error = copyin(args->desc, &info, sizeof(struct l_user_desc));
	if (error)
		return (error);

#ifdef DEBUG
	if (ldebug(set_thread_area))
		printf(ARGS(set_thread_area, "%i, %x, %x, %i, %i, %i, %i, %i, %i\n"),
		      info.entry_number,
		      info.base_addr,
		      info.limit,
		      info.seg_32bit,
		      info.contents,
		      info.read_exec_only,
		      info.limit_in_pages,
		      info.seg_not_present,
		      info.useable);
#endif

	idx = info.entry_number;
	/*
	 * Semantics of Linux version: every thread in the system has array of
	 * 3 tls descriptors. 1st is GLIBC TLS, 2nd is WINE, 3rd unknown. This
	 * syscall loads one of the selected tls decriptors with a value and
	 * also loads GDT descriptors 6, 7 and 8 with the content of the
	 * per-thread descriptors.
	 *
	 * Semantics of FreeBSD version: I think we can ignore that Linux has 3
	 * per-thread descriptors and use just the 1st one. The tls_array[]
	 * is used only in set/get-thread_area() syscalls and for loading the
	 * GDT descriptors. In FreeBSD we use just one GDT descriptor for TLS
	 * so we will load just one.
	 *
	 * XXX: this doesn't work when a user space process tries to use more
	 * than 1 TLS segment. Comment in the Linux sources says wine might do
	 * this.
	 */

	/*
	 * we support just GLIBC TLS now
	 * we should let 3 proceed as well because we use this segment so
	 * if code does two subsequent calls it should succeed
	 */
	if (idx != 6 && idx != -1 && idx != 3)
		return (EINVAL);

	/*
	 * we have to copy out the GDT entry we use
	 * FreeBSD uses GDT entry #3 for storing %gs so load that
	 *
	 * XXX: what if a user space program doesn't check this value and tries
	 * to use 6, 7 or 8?
	 */
	idx = info.entry_number = 3;
	error = copyout(&info, args->desc, sizeof(struct l_user_desc));
	if (error)
		return (error);

	if (LINUX_LDT_empty(&info)) {
		a[0] = 0;
		a[1] = 0;
	} else {
		a[0] = LINUX_LDT_entry_a(&info);
		a[1] = LINUX_LDT_entry_b(&info);
	}

	memcpy(&sd, &a, sizeof(a));
#ifdef DEBUG
	if (ldebug(set_thread_area))
		printf("Segment created in set_thread_area: lobase: %x, hibase: %x, lolimit: %x, hilimit: %x, type: %i, dpl: %i, p: %i, xx: %i, def32: %i, gran: %i\n", sd.sd_lobase,
			sd.sd_hibase,
			sd.sd_lolimit,
			sd.sd_hilimit,
			sd.sd_type,
			sd.sd_dpl,
			sd.sd_p,
			sd.sd_xx,
			sd.sd_def32,
			sd.sd_gran);
#endif

	/* this is taken from i386 version of cpu_set_user_tls() */
	critical_enter();
	/* set %gs */
	td->td_pcb->pcb_gsd = sd;
	PCPU_GET(fsgs_gdt)[1] = sd;
	load_gs(GSEL(GUGS_SEL, SEL_UPL));
	critical_exit();

	return (0);
}

int
linux_get_thread_area(struct thread *td, struct linux_get_thread_area_args *args)
{

	struct l_user_desc info;
	int error;
	int idx;
	struct l_desc_struct desc;
	struct segment_descriptor sd;

#ifdef DEBUG
	if (ldebug(get_thread_area))
		printf(ARGS(get_thread_area, "%p"), args->desc);
#endif

	error = copyin(args->desc, &info, sizeof(struct l_user_desc));
	if (error)
		return (error);

	idx = info.entry_number;
	/* XXX: I am not sure if we want 3 to be allowed too. */
	if (idx != 6 && idx != 3)
		return (EINVAL);

	idx = 3;

	memset(&info, 0, sizeof(info));

	sd = PCPU_GET(fsgs_gdt)[1];

	memcpy(&desc, &sd, sizeof(desc));

	info.entry_number = idx;
	info.base_addr = LINUX_GET_BASE(&desc);
	info.limit = LINUX_GET_LIMIT(&desc);
	info.seg_32bit = LINUX_GET_32BIT(&desc);
	info.contents = LINUX_GET_CONTENTS(&desc);
	info.read_exec_only = !LINUX_GET_WRITABLE(&desc);
	info.limit_in_pages = LINUX_GET_LIMIT_PAGES(&desc);
	info.seg_not_present = !LINUX_GET_PRESENT(&desc);
	info.useable = LINUX_GET_USEABLE(&desc);

	error = copyout(&info, args->desc, sizeof(struct l_user_desc));
	if (error)
		return (EFAULT);

	return (0);
}

/* XXX: this wont work with module - convert it */
int
linux_mq_open(struct thread *td, struct linux_mq_open_args *args)
{
#ifdef P1003_1B_MQUEUE
	return (sys_kmq_open(td, (struct kmq_open_args *)args));
#else
	return (ENOSYS);
#endif
}

int
linux_mq_unlink(struct thread *td, struct linux_mq_unlink_args *args)
{
#ifdef P1003_1B_MQUEUE
	return (sys_kmq_unlink(td, (struct kmq_unlink_args *)args));
#else
	return (ENOSYS);
#endif
}

int
linux_mq_timedsend(struct thread *td, struct linux_mq_timedsend_args *args)
{
#ifdef P1003_1B_MQUEUE
	return (sys_kmq_timedsend(td, (struct kmq_timedsend_args *)args));
#else
	return (ENOSYS);
#endif
}

int
linux_mq_timedreceive(struct thread *td, struct linux_mq_timedreceive_args *args)
{
#ifdef P1003_1B_MQUEUE
	return (sys_kmq_timedreceive(td, (struct kmq_timedreceive_args *)args));
#else
	return (ENOSYS);
#endif
}

int
linux_mq_notify(struct thread *td, struct linux_mq_notify_args *args)
{
#ifdef P1003_1B_MQUEUE
	return (sys_kmq_notify(td, (struct kmq_notify_args *)args));
#else
	return (ENOSYS);
#endif
}

int
linux_mq_getsetattr(struct thread *td, struct linux_mq_getsetattr_args *args)
{
#ifdef P1003_1B_MQUEUE
	return (sys_kmq_setattr(td, (struct kmq_setattr_args *)args));
#else
	return (ENOSYS);
#endif
}
