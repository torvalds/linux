/*-
 * Copyright (c) 2013 Dmitry Chagin
 * Copyright (c) 2004 Tim J. Robbins
 * Copyright (c) 2002 Doug Rabson
 * Copyright (c) 2000 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/clock.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/wait.h>

#include <security/mac/mac_framework.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>

#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/specialreg.h>

#include <vm/pmap.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>

#include <x86/ifunc.h>
#include <x86/sysarch.h>

#include <amd64/linux/linux.h>
#include <amd64/linux/linux_proto.h>
#include <compat/linux/linux_emul.h>
#include <compat/linux/linux_file.h>
#include <compat/linux/linux_ipc.h>
#include <compat/linux/linux_misc.h>
#include <compat/linux/linux_mmap.h>
#include <compat/linux/linux_signal.h>
#include <compat/linux/linux_util.h>

int
linux_execve(struct thread *td, struct linux_execve_args *args)
{
	struct image_args eargs;
	char *path;
	int error;

	LCONVPATHEXIST(td, args->path, &path);

	LINUX_CTR(execve);

	error = exec_copyin_args(&eargs, path, UIO_SYSSPACE, args->argp,
	    args->envp);
	free(path, M_TEMP);
	if (error == 0)
		error = linux_common_execve(td, &eargs);
	return (error);
}

int
linux_set_upcall_kse(struct thread *td, register_t stack)
{

	if (stack)
		td->td_frame->tf_rsp = stack;

	/*
	 * The newly created Linux thread returns
	 * to the user space by the same path that a parent do.
	 */
	td->td_frame->tf_rax = 0;
	return (0);
}

int
linux_mmap2(struct thread *td, struct linux_mmap2_args *args)
{

	return (linux_mmap_common(td, PTROUT(args->addr), args->len, args->prot,
		args->flags, args->fd, args->pgoff));
}

int
linux_mprotect(struct thread *td, struct linux_mprotect_args *uap)
{

	return (linux_mprotect_common(td, PTROUT(uap->addr), uap->len, uap->prot));
}

int
linux_iopl(struct thread *td, struct linux_iopl_args *args)
{
	int error;

	LINUX_CTR(iopl);

	if (args->level > 3)
		return (EINVAL);
	if ((error = priv_check(td, PRIV_IO)) != 0)
		return (error);
	if ((error = securelevel_gt(td->td_ucred, 0)) != 0)
		return (error);
	td->td_frame->tf_rflags = (td->td_frame->tf_rflags & ~PSL_IOPL) |
	    (args->level * (PSL_IOPL / 3));

	return (0);
}

int
linux_rt_sigsuspend(struct thread *td, struct linux_rt_sigsuspend_args *uap)
{
	l_sigset_t lmask;
	sigset_t sigmask;
	int error;

	LINUX_CTR2(rt_sigsuspend, "%p, %ld",
	    uap->newset, uap->sigsetsize);

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

	LINUX_CTR(pause);

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

	memset(&lss, 0, sizeof(lss));
	LINUX_CTR2(sigaltstack, "%p, %p", uap->uss, uap->uoss);

	if (uap->uss != NULL) {
		error = copyin(uap->uss, &lss, sizeof(l_stack_t));
		if (error)
			return (error);

		ss.ss_sp = PTRIN(lss.ss_sp);
		ss.ss_size = lss.ss_size;
		ss.ss_flags = linux_to_bsd_sigaltstack(lss.ss_flags);
	}
	error = kern_sigaltstack(td, (uap->uss != NULL) ? &ss : NULL,
	    (uap->uoss != NULL) ? &oss : NULL);
	if (!error && uap->uoss != NULL) {
		lss.ss_sp = PTROUT(oss.ss_sp);
		lss.ss_size = oss.ss_size;
		lss.ss_flags = bsd_to_linux_sigaltstack(oss.ss_flags);
		error = copyout(&lss, uap->uoss, sizeof(l_stack_t));
	}

	return (error);
}

int
linux_arch_prctl(struct thread *td, struct linux_arch_prctl_args *args)
{
	struct pcb *pcb;
	int error;

	pcb = td->td_pcb;
	LINUX_CTR2(arch_prctl, "0x%x, %p", args->code, args->addr);

	switch (args->code) {
	case LINUX_ARCH_SET_GS:
		if (args->addr < VM_MAXUSER_ADDRESS) {
			set_pcb_flags(pcb, PCB_FULL_IRET);
			pcb->pcb_gsbase = args->addr;
			td->td_frame->tf_gs = _ugssel;
			error = 0;
		} else
			error = EPERM;
		break;
	case LINUX_ARCH_SET_FS:
		if (args->addr < VM_MAXUSER_ADDRESS) {
			set_pcb_flags(pcb, PCB_FULL_IRET);
			pcb->pcb_fsbase = args->addr;
			td->td_frame->tf_fs = _ufssel;
			error = 0;
		} else
			error = EPERM;
		break;
	case LINUX_ARCH_GET_FS:
		error = copyout(&pcb->pcb_fsbase, PTRIN(args->addr),
		    sizeof(args->addr));
		break;
	case LINUX_ARCH_GET_GS:
		error = copyout(&pcb->pcb_gsbase, PTRIN(args->addr),
		    sizeof(args->addr));
		break;
	default:
		error = EINVAL;
	}
	return (error);
}

int
linux_set_cloned_tls(struct thread *td, void *desc)
{
	struct pcb *pcb;

	if ((uint64_t)desc >= VM_MAXUSER_ADDRESS)
		return (EPERM);

	pcb = td->td_pcb;
	pcb->pcb_fsbase = (register_t)desc;
	td->td_frame->tf_fs = _ufssel;

	return (0);
}

int futex_xchgl_nosmap(int oparg, uint32_t *uaddr, int *oldval);
int futex_xchgl_smap(int oparg, uint32_t *uaddr, int *oldval);
DEFINE_IFUNC(, int, futex_xchgl, (int, uint32_t *, int *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    futex_xchgl_smap : futex_xchgl_nosmap);
}

int futex_addl_nosmap(int oparg, uint32_t *uaddr, int *oldval);
int futex_addl_smap(int oparg, uint32_t *uaddr, int *oldval);
DEFINE_IFUNC(, int, futex_addl, (int, uint32_t *, int *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    futex_addl_smap : futex_addl_nosmap);
}

int futex_orl_nosmap(int oparg, uint32_t *uaddr, int *oldval);
int futex_orl_smap(int oparg, uint32_t *uaddr, int *oldval);
DEFINE_IFUNC(, int, futex_orl, (int, uint32_t *, int *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    futex_orl_smap : futex_orl_nosmap);
}

int futex_andl_nosmap(int oparg, uint32_t *uaddr, int *oldval);
int futex_andl_smap(int oparg, uint32_t *uaddr, int *oldval);
DEFINE_IFUNC(, int, futex_andl, (int, uint32_t *, int *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    futex_andl_smap : futex_andl_nosmap);
}

int futex_xorl_nosmap(int oparg, uint32_t *uaddr, int *oldval);
int futex_xorl_smap(int oparg, uint32_t *uaddr, int *oldval);
DEFINE_IFUNC(, int, futex_xorl, (int, uint32_t *, int *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 ?
	    futex_xorl_smap : futex_xorl_nosmap);
}
