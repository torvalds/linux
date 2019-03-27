/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1993, David Greenman
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
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/imgact_aout.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/vnode.h>

#include <machine/frame.h>
#include <machine/md_var.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_param.h>

#ifdef __amd64__
#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_util.h>
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32_syscall.h>
#include <compat/ia32/ia32_signal.h>
#endif

static int	exec_aout_imgact(struct image_params *imgp);
static int	aout_fixup(register_t **stack_base, struct image_params *imgp);

#define	AOUT32_USRSTACK		0xbfc00000

#if defined(__i386__)

#define	AOUT32_PS_STRINGS	(AOUT32_USRSTACK - sizeof(struct ps_strings))

struct sysentvec aout_sysvec = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= aout_fixup,
	.sv_sendsig	= sendsig,
	.sv_sigcode	= sigcode,
	.sv_szsigcode	= &szsigcode,
	.sv_name	= "FreeBSD a.out",
	.sv_coredump	= NULL,
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= AOUT32_USRSTACK,
	.sv_usrstack	= AOUT32_USRSTACK,
	.sv_psstrings	= AOUT32_PS_STRINGS,
	.sv_stackprot	= VM_PROT_ALL,
	.sv_copyout_strings	= exec_copyout_strings,
	.sv_setregs	= exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_AOUT | SV_IA32 | SV_ILP32,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_syscallnames = syscallnames,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
};

#elif defined(__amd64__)

#define	AOUT32_PS_STRINGS \
    (AOUT32_USRSTACK - sizeof(struct freebsd32_ps_strings))
#define	AOUT32_MINUSER		FREEBSD32_MINUSER

extern const char *freebsd32_syscallnames[];
extern u_long ia32_maxssiz;

struct sysentvec aout_sysvec = {
	.sv_size	= FREEBSD32_SYS_MAXSYSCALL,
	.sv_table	= freebsd32_sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= aout_fixup,
	.sv_sendsig	= ia32_sendsig,
	.sv_sigcode	= ia32_sigcode,
	.sv_szsigcode	= &sz_ia32_sigcode,
	.sv_name	= "FreeBSD a.out",
	.sv_coredump	= NULL,
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= AOUT32_MINUSER,
	.sv_maxuser	= AOUT32_USRSTACK,
	.sv_usrstack	= AOUT32_USRSTACK,
	.sv_psstrings	= AOUT32_PS_STRINGS,
	.sv_stackprot	= VM_PROT_ALL,
	.sv_copyout_strings	= freebsd32_copyout_strings,
	.sv_setregs	= ia32_setregs,
	.sv_fixlimit	= ia32_fixlimit,
	.sv_maxssiz	= &ia32_maxssiz,
	.sv_flags	= SV_ABI_FREEBSD | SV_AOUT | SV_IA32 | SV_ILP32,
	.sv_set_syscall_retval = ia32_set_syscall_retval,
	.sv_fetch_syscall_args = ia32_fetch_syscall_args,
	.sv_syscallnames = freebsd32_syscallnames,
};
#else
#error "Port me"
#endif

static int
aout_fixup(register_t **stack_base, struct image_params *imgp)
{

	*(char **)stack_base -= sizeof(uint32_t);
	return (suword32(*stack_base, imgp->args->argc));
}

static int
exec_aout_imgact(struct image_params *imgp)
{
	const struct exec *a_out = (const struct exec *) imgp->image_header;
	struct vmspace *vmspace;
	vm_map_t map;
	vm_object_t object;
	vm_offset_t text_end, data_end;
	unsigned long virtual_offset;
	unsigned long file_offset;
	unsigned long bss_size;
	int error;

	/*
	 * Linux and *BSD binaries look very much alike,
	 * only the machine id is different:
	 * 0x64 for Linux, 0x86 for *BSD, 0x00 for BSDI.
	 * NetBSD is in network byte order.. ugh.
	 */
	if (((a_out->a_midmag >> 16) & 0xff) != 0x86 &&
	    ((a_out->a_midmag >> 16) & 0xff) != 0 &&
	    ((((int)ntohl(a_out->a_midmag)) >> 16) & 0xff) != 0x86)
                return -1;

	/*
	 * Set file/virtual offset based on a.out variant.
	 *	We do two cases: host byte order and network byte order
	 *	(for NetBSD compatibility)
	 */
	switch ((int)(a_out->a_midmag & 0xffff)) {
	case ZMAGIC:
		virtual_offset = 0;
		if (a_out->a_text) {
			file_offset = PAGE_SIZE;
		} else {
			/* Bill's "screwball mode" */
			file_offset = 0;
		}
		break;
	case QMAGIC:
		virtual_offset = PAGE_SIZE;
		file_offset = 0;
		/* Pass PS_STRINGS for BSD/OS binaries only. */
		if (N_GETMID(*a_out) == MID_ZERO)
			imgp->ps_strings = aout_sysvec.sv_psstrings;
		break;
	default:
		/* NetBSD compatibility */
		switch ((int)(ntohl(a_out->a_midmag) & 0xffff)) {
		case ZMAGIC:
		case QMAGIC:
			virtual_offset = PAGE_SIZE;
			file_offset = 0;
			break;
		default:
			return (-1);
		}
	}

	bss_size = roundup(a_out->a_bss, PAGE_SIZE);

	/*
	 * Check various fields in header for validity/bounds.
	 */
	if (/* entry point must lay with text region */
	    a_out->a_entry < virtual_offset ||
	    a_out->a_entry >= virtual_offset + a_out->a_text ||

	    /* text and data size must each be page rounded */
	    a_out->a_text & PAGE_MASK || a_out->a_data & PAGE_MASK

#ifdef __amd64__
	    ||
	    /* overflows */
	    virtual_offset + a_out->a_text + a_out->a_data + bss_size > UINT_MAX
#endif
	    )
		return (-1);

	/* text + data can't exceed file size */
	if (a_out->a_data + a_out->a_text > imgp->attr->va_size)
		return (EFAULT);

	/*
	 * text/data/bss must not exceed limits
	 */
	PROC_LOCK(imgp->proc);
	if (/* text can't exceed maximum text size */
	    a_out->a_text > maxtsiz ||

	    /* data + bss can't exceed rlimit */
	    a_out->a_data + bss_size > lim_cur_proc(imgp->proc, RLIMIT_DATA) ||
	    racct_set(imgp->proc, RACCT_DATA, a_out->a_data + bss_size) != 0) {
			PROC_UNLOCK(imgp->proc);
			return (ENOMEM);
	}
	PROC_UNLOCK(imgp->proc);

	/*
	 * Avoid a possible deadlock if the current address space is destroyed
	 * and that address space maps the locked vnode.  In the common case,
	 * the locked vnode's v_usecount is decremented but remains greater
	 * than zero.  Consequently, the vnode lock is not needed by vrele().
	 * However, in cases where the vnode lock is external, such as nullfs,
	 * v_usecount may become zero.
	 */
	VOP_UNLOCK(imgp->vp, 0);

	/*
	 * Destroy old process VM and create a new one (with a new stack)
	 */
	error = exec_new_vmspace(imgp, &aout_sysvec);

	vn_lock(imgp->vp, LK_EXCLUSIVE | LK_RETRY);
	if (error)
		return (error);

	/*
	 * The vm space can be changed by exec_new_vmspace
	 */
	vmspace = imgp->proc->p_vmspace;

	object = imgp->object;
	map = &vmspace->vm_map;
	vm_map_lock(map);
	vm_object_reference(object);

	text_end = virtual_offset + a_out->a_text;
	error = vm_map_insert(map, object,
		file_offset,
		virtual_offset, text_end,
		VM_PROT_READ | VM_PROT_EXECUTE, VM_PROT_ALL,
		MAP_COPY_ON_WRITE | MAP_PREFAULT);
	if (error) {
		vm_map_unlock(map);
		vm_object_deallocate(object);
		return (error);
	}
	data_end = text_end + a_out->a_data;
	if (a_out->a_data) {
		vm_object_reference(object);
		error = vm_map_insert(map, object,
			file_offset + a_out->a_text,
			text_end, data_end,
			VM_PROT_ALL, VM_PROT_ALL,
			MAP_COPY_ON_WRITE | MAP_PREFAULT);
		if (error) {
			vm_map_unlock(map);
			vm_object_deallocate(object);
			return (error);
		}
	}

	if (bss_size) {
		error = vm_map_insert(map, NULL, 0,
			data_end, data_end + bss_size,
			VM_PROT_ALL, VM_PROT_ALL, 0);
		if (error) {
			vm_map_unlock(map);
			return (error);
		}
	}
	vm_map_unlock(map);

	/* Fill in process VM information */
	vmspace->vm_tsize = a_out->a_text >> PAGE_SHIFT;
	vmspace->vm_dsize = (a_out->a_data + bss_size) >> PAGE_SHIFT;
	vmspace->vm_taddr = (caddr_t) (uintptr_t) virtual_offset;
	vmspace->vm_daddr = (caddr_t) (uintptr_t)
			    (virtual_offset + a_out->a_text);

	/* Fill in image_params */
	imgp->interpreted = 0;
	imgp->entry_addr = a_out->a_entry;

	imgp->proc->p_sysent = &aout_sysvec;

	return (0);
}

/*
 * Tell kern_execve.c about it, with a little help from the linker.
 */
static struct execsw aout_execsw = {
	.ex_imgact = exec_aout_imgact,
	.ex_name = "a.out"
};
EXEC_SET(aout, aout_execsw);
