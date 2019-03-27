/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994, Sean Eric Fagan
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Sean Eric Fagan.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/pioctl.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/ptrace.h>
#include <sys/rwlock.h>
#include <sys/sx.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>

#include <machine/reg.h>

#include <security/audit/audit.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_param.h>

#ifdef COMPAT_FREEBSD32
#include <sys/procfs.h>
#include <compat/freebsd32/freebsd32_signal.h>

struct ptrace_io_desc32 {
	int		piod_op;
	uint32_t	piod_offs;
	uint32_t	piod_addr;
	uint32_t	piod_len;
};

struct ptrace_vm_entry32 {
	int		pve_entry;
	int		pve_timestamp;
	uint32_t	pve_start;
	uint32_t	pve_end;
	uint32_t	pve_offset;
	u_int		pve_prot;
	u_int		pve_pathlen;
	int32_t		pve_fileid;
	u_int		pve_fsid;
	uint32_t	pve_path;
};
#endif

/*
 * Functions implemented using PROC_ACTION():
 *
 * proc_read_regs(proc, regs)
 *	Get the current user-visible register set from the process
 *	and copy it into the regs structure (<machine/reg.h>).
 *	The process is stopped at the time read_regs is called.
 *
 * proc_write_regs(proc, regs)
 *	Update the current register set from the passed in regs
 *	structure.  Take care to avoid clobbering special CPU
 *	registers or privileged bits in the PSL.
 *	Depending on the architecture this may have fix-up work to do,
 *	especially if the IAR or PCW are modified.
 *	The process is stopped at the time write_regs is called.
 *
 * proc_read_fpregs, proc_write_fpregs
 *	deal with the floating point register set, otherwise as above.
 *
 * proc_read_dbregs, proc_write_dbregs
 *	deal with the processor debug register set, otherwise as above.
 *
 * proc_sstep(proc)
 *	Arrange for the process to trap after executing a single instruction.
 */

#define	PROC_ACTION(action) do {					\
	int error;							\
									\
	PROC_LOCK_ASSERT(td->td_proc, MA_OWNED);			\
	if ((td->td_proc->p_flag & P_INMEM) == 0)			\
		error = EIO;						\
	else								\
		error = (action);					\
	return (error);							\
} while(0)

int
proc_read_regs(struct thread *td, struct reg *regs)
{

	PROC_ACTION(fill_regs(td, regs));
}

int
proc_write_regs(struct thread *td, struct reg *regs)
{

	PROC_ACTION(set_regs(td, regs));
}

int
proc_read_dbregs(struct thread *td, struct dbreg *dbregs)
{

	PROC_ACTION(fill_dbregs(td, dbregs));
}

int
proc_write_dbregs(struct thread *td, struct dbreg *dbregs)
{

	PROC_ACTION(set_dbregs(td, dbregs));
}

/*
 * Ptrace doesn't support fpregs at all, and there are no security holes
 * or translations for fpregs, so we can just copy them.
 */
int
proc_read_fpregs(struct thread *td, struct fpreg *fpregs)
{

	PROC_ACTION(fill_fpregs(td, fpregs));
}

int
proc_write_fpregs(struct thread *td, struct fpreg *fpregs)
{

	PROC_ACTION(set_fpregs(td, fpregs));
}

#ifdef COMPAT_FREEBSD32
/* For 32 bit binaries, we need to expose the 32 bit regs layouts. */
int
proc_read_regs32(struct thread *td, struct reg32 *regs32)
{

	PROC_ACTION(fill_regs32(td, regs32));
}

int
proc_write_regs32(struct thread *td, struct reg32 *regs32)
{

	PROC_ACTION(set_regs32(td, regs32));
}

int
proc_read_dbregs32(struct thread *td, struct dbreg32 *dbregs32)
{

	PROC_ACTION(fill_dbregs32(td, dbregs32));
}

int
proc_write_dbregs32(struct thread *td, struct dbreg32 *dbregs32)
{

	PROC_ACTION(set_dbregs32(td, dbregs32));
}

int
proc_read_fpregs32(struct thread *td, struct fpreg32 *fpregs32)
{

	PROC_ACTION(fill_fpregs32(td, fpregs32));
}

int
proc_write_fpregs32(struct thread *td, struct fpreg32 *fpregs32)
{

	PROC_ACTION(set_fpregs32(td, fpregs32));
}
#endif

int
proc_sstep(struct thread *td)
{

	PROC_ACTION(ptrace_single_step(td));
}

int
proc_rwmem(struct proc *p, struct uio *uio)
{
	vm_map_t map;
	vm_offset_t pageno;		/* page number */
	vm_prot_t reqprot;
	int error, fault_flags, page_offset, writing;

	/*
	 * Assert that someone has locked this vmspace.  (Should be
	 * curthread but we can't assert that.)  This keeps the process
	 * from exiting out from under us until this operation completes.
	 */
	PROC_ASSERT_HELD(p);
	PROC_LOCK_ASSERT(p, MA_NOTOWNED);

	/*
	 * The map we want...
	 */
	map = &p->p_vmspace->vm_map;

	/*
	 * If we are writing, then we request vm_fault() to create a private
	 * copy of each page.  Since these copies will not be writeable by the
	 * process, we must explicity request that they be dirtied.
	 */
	writing = uio->uio_rw == UIO_WRITE;
	reqprot = writing ? VM_PROT_COPY | VM_PROT_READ : VM_PROT_READ;
	fault_flags = writing ? VM_FAULT_DIRTY : VM_FAULT_NORMAL;

	/*
	 * Only map in one page at a time.  We don't have to, but it
	 * makes things easier.  This way is trivial - right?
	 */
	do {
		vm_offset_t uva;
		u_int len;
		vm_page_t m;

		uva = (vm_offset_t)uio->uio_offset;

		/*
		 * Get the page number of this segment.
		 */
		pageno = trunc_page(uva);
		page_offset = uva - pageno;

		/*
		 * How many bytes to copy
		 */
		len = min(PAGE_SIZE - page_offset, uio->uio_resid);

		/*
		 * Fault and hold the page on behalf of the process.
		 */
		error = vm_fault_hold(map, pageno, reqprot, fault_flags, &m);
		if (error != KERN_SUCCESS) {
			if (error == KERN_RESOURCE_SHORTAGE)
				error = ENOMEM;
			else
				error = EFAULT;
			break;
		}

		/*
		 * Now do the i/o move.
		 */
		error = uiomove_fromphys(&m, page_offset, len, uio);

		/* Make the I-cache coherent for breakpoints. */
		if (writing && error == 0) {
			vm_map_lock_read(map);
			if (vm_map_check_protection(map, pageno, pageno +
			    PAGE_SIZE, VM_PROT_EXECUTE))
				vm_sync_icache(map, uva, len);
			vm_map_unlock_read(map);
		}

		/*
		 * Release the page.
		 */
		vm_page_lock(m);
		vm_page_unhold(m);
		vm_page_unlock(m);

	} while (error == 0 && uio->uio_resid > 0);

	return (error);
}

static ssize_t
proc_iop(struct thread *td, struct proc *p, vm_offset_t va, void *buf,
    size_t len, enum uio_rw rw)
{
	struct iovec iov;
	struct uio uio;
	ssize_t slen;

	MPASS(len < SSIZE_MAX);
	slen = (ssize_t)len;

	iov.iov_base = (caddr_t)buf;
	iov.iov_len = len;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = va;
	uio.uio_resid = slen;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = rw;
	uio.uio_td = td;
	proc_rwmem(p, &uio);
	if (uio.uio_resid == slen)
		return (-1);
	return (slen - uio.uio_resid);
}

ssize_t
proc_readmem(struct thread *td, struct proc *p, vm_offset_t va, void *buf,
    size_t len)
{

	return (proc_iop(td, p, va, buf, len, UIO_READ));
}

ssize_t
proc_writemem(struct thread *td, struct proc *p, vm_offset_t va, void *buf,
    size_t len)
{

	return (proc_iop(td, p, va, buf, len, UIO_WRITE));
}

static int
ptrace_vm_entry(struct thread *td, struct proc *p, struct ptrace_vm_entry *pve)
{
	struct vattr vattr;
	vm_map_t map;
	vm_map_entry_t entry;
	vm_object_t obj, tobj, lobj;
	struct vmspace *vm;
	struct vnode *vp;
	char *freepath, *fullpath;
	u_int pathlen;
	int error, index;

	error = 0;
	obj = NULL;

	vm = vmspace_acquire_ref(p);
	map = &vm->vm_map;
	vm_map_lock_read(map);

	do {
		entry = map->header.next;
		index = 0;
		while (index < pve->pve_entry && entry != &map->header) {
			entry = entry->next;
			index++;
		}
		if (index != pve->pve_entry) {
			error = EINVAL;
			break;
		}
		KASSERT((map->header.eflags & MAP_ENTRY_IS_SUB_MAP) == 0,
		    ("Submap in map header"));
		while ((entry->eflags & MAP_ENTRY_IS_SUB_MAP) != 0) {
			entry = entry->next;
			index++;
		}
		if (entry == &map->header) {
			error = ENOENT;
			break;
		}

		/* We got an entry. */
		pve->pve_entry = index + 1;
		pve->pve_timestamp = map->timestamp;
		pve->pve_start = entry->start;
		pve->pve_end = entry->end - 1;
		pve->pve_offset = entry->offset;
		pve->pve_prot = entry->protection;

		/* Backing object's path needed? */
		if (pve->pve_pathlen == 0)
			break;

		pathlen = pve->pve_pathlen;
		pve->pve_pathlen = 0;

		obj = entry->object.vm_object;
		if (obj != NULL)
			VM_OBJECT_RLOCK(obj);
	} while (0);

	vm_map_unlock_read(map);

	pve->pve_fsid = VNOVAL;
	pve->pve_fileid = VNOVAL;

	if (error == 0 && obj != NULL) {
		lobj = obj;
		for (tobj = obj; tobj != NULL; tobj = tobj->backing_object) {
			if (tobj != obj)
				VM_OBJECT_RLOCK(tobj);
			if (lobj != obj)
				VM_OBJECT_RUNLOCK(lobj);
			lobj = tobj;
			pve->pve_offset += tobj->backing_object_offset;
		}
		vp = vm_object_vnode(lobj);
		if (vp != NULL)
			vref(vp);
		if (lobj != obj)
			VM_OBJECT_RUNLOCK(lobj);
		VM_OBJECT_RUNLOCK(obj);

		if (vp != NULL) {
			freepath = NULL;
			fullpath = NULL;
			vn_fullpath(td, vp, &fullpath, &freepath);
			vn_lock(vp, LK_SHARED | LK_RETRY);
			if (VOP_GETATTR(vp, &vattr, td->td_ucred) == 0) {
				pve->pve_fileid = vattr.va_fileid;
				pve->pve_fsid = vattr.va_fsid;
			}
			vput(vp);

			if (fullpath != NULL) {
				pve->pve_pathlen = strlen(fullpath) + 1;
				if (pve->pve_pathlen <= pathlen) {
					error = copyout(fullpath, pve->pve_path,
					    pve->pve_pathlen);
				} else
					error = ENAMETOOLONG;
			}
			if (freepath != NULL)
				free(freepath, M_TEMP);
		}
	}
	vmspace_free(vm);
	if (error == 0)
		CTR3(KTR_PTRACE, "PT_VM_ENTRY: pid %d, entry %d, start %p",
		    p->p_pid, pve->pve_entry, pve->pve_start);

	return (error);
}

#ifdef COMPAT_FREEBSD32
static int
ptrace_vm_entry32(struct thread *td, struct proc *p,
    struct ptrace_vm_entry32 *pve32)
{
	struct ptrace_vm_entry pve;
	int error;

	pve.pve_entry = pve32->pve_entry;
	pve.pve_pathlen = pve32->pve_pathlen;
	pve.pve_path = (void *)(uintptr_t)pve32->pve_path;

	error = ptrace_vm_entry(td, p, &pve);
	if (error == 0) {
		pve32->pve_entry = pve.pve_entry;
		pve32->pve_timestamp = pve.pve_timestamp;
		pve32->pve_start = pve.pve_start;
		pve32->pve_end = pve.pve_end;
		pve32->pve_offset = pve.pve_offset;
		pve32->pve_prot = pve.pve_prot;
		pve32->pve_fileid = pve.pve_fileid;
		pve32->pve_fsid = pve.pve_fsid;
	}

	pve32->pve_pathlen = pve.pve_pathlen;
	return (error);
}

static void
ptrace_lwpinfo_to32(const struct ptrace_lwpinfo *pl,
    struct ptrace_lwpinfo32 *pl32)
{

	bzero(pl32, sizeof(*pl32));
	pl32->pl_lwpid = pl->pl_lwpid;
	pl32->pl_event = pl->pl_event;
	pl32->pl_flags = pl->pl_flags;
	pl32->pl_sigmask = pl->pl_sigmask;
	pl32->pl_siglist = pl->pl_siglist;
	siginfo_to_siginfo32(&pl->pl_siginfo, &pl32->pl_siginfo);
	strcpy(pl32->pl_tdname, pl->pl_tdname);
	pl32->pl_child_pid = pl->pl_child_pid;
	pl32->pl_syscall_code = pl->pl_syscall_code;
	pl32->pl_syscall_narg = pl->pl_syscall_narg;
}
#endif /* COMPAT_FREEBSD32 */

/*
 * Process debugging system call.
 */
#ifndef _SYS_SYSPROTO_H_
struct ptrace_args {
	int	req;
	pid_t	pid;
	caddr_t	addr;
	int	data;
};
#endif

#ifdef COMPAT_FREEBSD32
/*
 * This CPP subterfuge is to try and reduce the number of ifdefs in
 * the body of the code.
 *   COPYIN(uap->addr, &r.reg, sizeof r.reg);
 * becomes either:
 *   copyin(uap->addr, &r.reg, sizeof r.reg);
 * or
 *   copyin(uap->addr, &r.reg32, sizeof r.reg32);
 * .. except this is done at runtime.
 */
#define	BZERO(a, s)		wrap32 ? \
	bzero(a ## 32, s ## 32) : \
	bzero(a, s)
#define	COPYIN(u, k, s)		wrap32 ? \
	copyin(u, k ## 32, s ## 32) : \
	copyin(u, k, s)
#define	COPYOUT(k, u, s)	wrap32 ? \
	copyout(k ## 32, u, s ## 32) : \
	copyout(k, u, s)
#else
#define	BZERO(a, s)		bzero(a, s)
#define	COPYIN(u, k, s)		copyin(u, k, s)
#define	COPYOUT(k, u, s)	copyout(k, u, s)
#endif
int
sys_ptrace(struct thread *td, struct ptrace_args *uap)
{
	/*
	 * XXX this obfuscation is to reduce stack usage, but the register
	 * structs may be too large to put on the stack anyway.
	 */
	union {
		struct ptrace_io_desc piod;
		struct ptrace_lwpinfo pl;
		struct ptrace_vm_entry pve;
		struct dbreg dbreg;
		struct fpreg fpreg;
		struct reg reg;
#ifdef COMPAT_FREEBSD32
		struct dbreg32 dbreg32;
		struct fpreg32 fpreg32;
		struct reg32 reg32;
		struct ptrace_io_desc32 piod32;
		struct ptrace_lwpinfo32 pl32;
		struct ptrace_vm_entry32 pve32;
#endif
		char args[sizeof(td->td_sa.args)];
		int ptevents;
	} r;
	void *addr;
	int error = 0;
#ifdef COMPAT_FREEBSD32
	int wrap32 = 0;

	if (SV_CURPROC_FLAG(SV_ILP32))
		wrap32 = 1;
#endif
	AUDIT_ARG_PID(uap->pid);
	AUDIT_ARG_CMD(uap->req);
	AUDIT_ARG_VALUE(uap->data);
	addr = &r;
	switch (uap->req) {
	case PT_GET_EVENT_MASK:
	case PT_LWPINFO:
	case PT_GET_SC_ARGS:
		break;
	case PT_GETREGS:
		BZERO(&r.reg, sizeof r.reg);
		break;
	case PT_GETFPREGS:
		BZERO(&r.fpreg, sizeof r.fpreg);
		break;
	case PT_GETDBREGS:
		BZERO(&r.dbreg, sizeof r.dbreg);
		break;
	case PT_SETREGS:
		error = COPYIN(uap->addr, &r.reg, sizeof r.reg);
		break;
	case PT_SETFPREGS:
		error = COPYIN(uap->addr, &r.fpreg, sizeof r.fpreg);
		break;
	case PT_SETDBREGS:
		error = COPYIN(uap->addr, &r.dbreg, sizeof r.dbreg);
		break;
	case PT_SET_EVENT_MASK:
		if (uap->data != sizeof(r.ptevents))
			error = EINVAL;
		else
			error = copyin(uap->addr, &r.ptevents, uap->data);
		break;
	case PT_IO:
		error = COPYIN(uap->addr, &r.piod, sizeof r.piod);
		break;
	case PT_VM_ENTRY:
		error = COPYIN(uap->addr, &r.pve, sizeof r.pve);
		break;
	default:
		addr = uap->addr;
		break;
	}
	if (error)
		return (error);

	error = kern_ptrace(td, uap->req, uap->pid, addr, uap->data);
	if (error)
		return (error);

	switch (uap->req) {
	case PT_VM_ENTRY:
		error = COPYOUT(&r.pve, uap->addr, sizeof r.pve);
		break;
	case PT_IO:
		error = COPYOUT(&r.piod, uap->addr, sizeof r.piod);
		break;
	case PT_GETREGS:
		error = COPYOUT(&r.reg, uap->addr, sizeof r.reg);
		break;
	case PT_GETFPREGS:
		error = COPYOUT(&r.fpreg, uap->addr, sizeof r.fpreg);
		break;
	case PT_GETDBREGS:
		error = COPYOUT(&r.dbreg, uap->addr, sizeof r.dbreg);
		break;
	case PT_GET_EVENT_MASK:
		/* NB: The size in uap->data is validated in kern_ptrace(). */
		error = copyout(&r.ptevents, uap->addr, uap->data);
		break;
	case PT_LWPINFO:
		/* NB: The size in uap->data is validated in kern_ptrace(). */
		error = copyout(&r.pl, uap->addr, uap->data);
		break;
	case PT_GET_SC_ARGS:
		error = copyout(r.args, uap->addr, MIN(uap->data,
		    sizeof(r.args)));
		break;
	}

	return (error);
}
#undef COPYIN
#undef COPYOUT
#undef BZERO

#ifdef COMPAT_FREEBSD32
/*
 *   PROC_READ(regs, td2, addr);
 * becomes either:
 *   proc_read_regs(td2, addr);
 * or
 *   proc_read_regs32(td2, addr);
 * .. except this is done at runtime.  There is an additional
 * complication in that PROC_WRITE disallows 32 bit consumers
 * from writing to 64 bit address space targets.
 */
#define	PROC_READ(w, t, a)	wrap32 ? \
	proc_read_ ## w ## 32(t, a) : \
	proc_read_ ## w (t, a)
#define	PROC_WRITE(w, t, a)	wrap32 ? \
	(safe ? proc_write_ ## w ## 32(t, a) : EINVAL ) : \
	proc_write_ ## w (t, a)
#else
#define	PROC_READ(w, t, a)	proc_read_ ## w (t, a)
#define	PROC_WRITE(w, t, a)	proc_write_ ## w (t, a)
#endif

void
proc_set_traced(struct proc *p, bool stop)
{

	sx_assert(&proctree_lock, SX_XLOCKED);
	PROC_LOCK_ASSERT(p, MA_OWNED);
	p->p_flag |= P_TRACED;
	if (stop)
		p->p_flag2 |= P2_PTRACE_FSTP;
	p->p_ptevents = PTRACE_DEFAULT;
}

int
kern_ptrace(struct thread *td, int req, pid_t pid, void *addr, int data)
{
	struct iovec iov;
	struct uio uio;
	struct proc *curp, *p, *pp;
	struct thread *td2 = NULL, *td3;
	struct ptrace_io_desc *piod = NULL;
	struct ptrace_lwpinfo *pl;
	int error, num, tmp;
	int proctree_locked = 0;
	lwpid_t tid = 0, *buf;
#ifdef COMPAT_FREEBSD32
	int wrap32 = 0, safe = 0;
	struct ptrace_io_desc32 *piod32 = NULL;
	struct ptrace_lwpinfo32 *pl32 = NULL;
	struct ptrace_lwpinfo plr;
#endif

	curp = td->td_proc;

	/* Lock proctree before locking the process. */
	switch (req) {
	case PT_TRACE_ME:
	case PT_ATTACH:
	case PT_STEP:
	case PT_CONTINUE:
	case PT_TO_SCE:
	case PT_TO_SCX:
	case PT_SYSCALL:
	case PT_FOLLOW_FORK:
	case PT_LWP_EVENTS:
	case PT_GET_EVENT_MASK:
	case PT_SET_EVENT_MASK:
	case PT_DETACH:
	case PT_GET_SC_ARGS:
		sx_xlock(&proctree_lock);
		proctree_locked = 1;
		break;
	default:
		break;
	}

	if (req == PT_TRACE_ME) {
		p = td->td_proc;
		PROC_LOCK(p);
	} else {
		if (pid <= PID_MAX) {
			if ((p = pfind(pid)) == NULL) {
				if (proctree_locked)
					sx_xunlock(&proctree_lock);
				return (ESRCH);
			}
		} else {
			td2 = tdfind(pid, -1);
			if (td2 == NULL) {
				if (proctree_locked)
					sx_xunlock(&proctree_lock);
				return (ESRCH);
			}
			p = td2->td_proc;
			tid = pid;
			pid = p->p_pid;
		}
	}
	AUDIT_ARG_PROCESS(p);

	if ((p->p_flag & P_WEXIT) != 0) {
		error = ESRCH;
		goto fail;
	}
	if ((error = p_cansee(td, p)) != 0)
		goto fail;

	if ((error = p_candebug(td, p)) != 0)
		goto fail;

	/*
	 * System processes can't be debugged.
	 */
	if ((p->p_flag & P_SYSTEM) != 0) {
		error = EINVAL;
		goto fail;
	}

	if (tid == 0) {
		if ((p->p_flag & P_STOPPED_TRACE) != 0) {
			KASSERT(p->p_xthread != NULL, ("NULL p_xthread"));
			td2 = p->p_xthread;
		} else {
			td2 = FIRST_THREAD_IN_PROC(p);
		}
		tid = td2->td_tid;
	}

#ifdef COMPAT_FREEBSD32
	/*
	 * Test if we're a 32 bit client and what the target is.
	 * Set the wrap controls accordingly.
	 */
	if (SV_CURPROC_FLAG(SV_ILP32)) {
		if (SV_PROC_FLAG(td2->td_proc, SV_ILP32))
			safe = 1;
		wrap32 = 1;
	}
#endif
	/*
	 * Permissions check
	 */
	switch (req) {
	case PT_TRACE_ME:
		/*
		 * Always legal, when there is a parent process which
		 * could trace us.  Otherwise, reject.
		 */
		if ((p->p_flag & P_TRACED) != 0) {
			error = EBUSY;
			goto fail;
		}
		if (p->p_pptr == initproc) {
			error = EPERM;
			goto fail;
		}
		break;

	case PT_ATTACH:
		/* Self */
		if (p == td->td_proc) {
			error = EINVAL;
			goto fail;
		}

		/* Already traced */
		if (p->p_flag & P_TRACED) {
			error = EBUSY;
			goto fail;
		}

		/* Can't trace an ancestor if you're being traced. */
		if (curp->p_flag & P_TRACED) {
			for (pp = curp->p_pptr; pp != NULL; pp = pp->p_pptr) {
				if (pp == p) {
					error = EINVAL;
					goto fail;
				}
			}
		}


		/* OK */
		break;

	case PT_CLEARSTEP:
		/* Allow thread to clear single step for itself */
		if (td->td_tid == tid)
			break;

		/* FALLTHROUGH */
	default:
		/* not being traced... */
		if ((p->p_flag & P_TRACED) == 0) {
			error = EPERM;
			goto fail;
		}

		/* not being traced by YOU */
		if (p->p_pptr != td->td_proc) {
			error = EBUSY;
			goto fail;
		}

		/* not currently stopped */
		if ((p->p_flag & P_STOPPED_TRACE) == 0 ||
		    p->p_suspcount != p->p_numthreads  ||
		    (p->p_flag & P_WAITED) == 0) {
			error = EBUSY;
			goto fail;
		}

		/* OK */
		break;
	}

	/* Keep this process around until we finish this request. */
	_PHOLD(p);

#ifdef FIX_SSTEP
	/*
	 * Single step fixup ala procfs
	 */
	FIX_SSTEP(td2);
#endif

	/*
	 * Actually do the requests
	 */

	td->td_retval[0] = 0;

	switch (req) {
	case PT_TRACE_ME:
		/* set my trace flag and "owner" so it can read/write me */
		proc_set_traced(p, false);
		if (p->p_flag & P_PPWAIT)
			p->p_flag |= P_PPTRACE;
		CTR1(KTR_PTRACE, "PT_TRACE_ME: pid %d", p->p_pid);
		break;

	case PT_ATTACH:
		/* security check done above */
		/*
		 * It would be nice if the tracing relationship was separate
		 * from the parent relationship but that would require
		 * another set of links in the proc struct or for "wait"
		 * to scan the entire proc table.  To make life easier,
		 * we just re-parent the process we're trying to trace.
		 * The old parent is remembered so we can put things back
		 * on a "detach".
		 */
		proc_set_traced(p, true);
		if (p->p_pptr != td->td_proc) {
			proc_reparent(p, td->td_proc, false);
		}
		CTR2(KTR_PTRACE, "PT_ATTACH: pid %d, oppid %d", p->p_pid,
		    p->p_oppid);

		sx_xunlock(&proctree_lock);
		proctree_locked = 0;
		MPASS(p->p_xthread == NULL);
		MPASS((p->p_flag & P_STOPPED_TRACE) == 0);

		/*
		 * If already stopped due to a stop signal, clear the
		 * existing stop before triggering a traced SIGSTOP.
		 */
		if ((p->p_flag & P_STOPPED_SIG) != 0) {
			PROC_SLOCK(p);
			p->p_flag &= ~(P_STOPPED_SIG | P_WAITED);
			thread_unsuspend(p);
			PROC_SUNLOCK(p);
		}

		kern_psignal(p, SIGSTOP);
		break;

	case PT_CLEARSTEP:
		CTR2(KTR_PTRACE, "PT_CLEARSTEP: tid %d (pid %d)", td2->td_tid,
		    p->p_pid);
		error = ptrace_clear_single_step(td2);
		break;

	case PT_SETSTEP:
		CTR2(KTR_PTRACE, "PT_SETSTEP: tid %d (pid %d)", td2->td_tid,
		    p->p_pid);
		error = ptrace_single_step(td2);
		break;

	case PT_SUSPEND:
		CTR2(KTR_PTRACE, "PT_SUSPEND: tid %d (pid %d)", td2->td_tid,
		    p->p_pid);
		td2->td_dbgflags |= TDB_SUSPEND;
		thread_lock(td2);
		td2->td_flags |= TDF_NEEDSUSPCHK;
		thread_unlock(td2);
		break;

	case PT_RESUME:
		CTR2(KTR_PTRACE, "PT_RESUME: tid %d (pid %d)", td2->td_tid,
		    p->p_pid);
		td2->td_dbgflags &= ~TDB_SUSPEND;
		break;

	case PT_FOLLOW_FORK:
		CTR3(KTR_PTRACE, "PT_FOLLOW_FORK: pid %d %s -> %s", p->p_pid,
		    p->p_ptevents & PTRACE_FORK ? "enabled" : "disabled",
		    data ? "enabled" : "disabled");
		if (data)
			p->p_ptevents |= PTRACE_FORK;
		else
			p->p_ptevents &= ~PTRACE_FORK;
		break;

	case PT_LWP_EVENTS:
		CTR3(KTR_PTRACE, "PT_LWP_EVENTS: pid %d %s -> %s", p->p_pid,
		    p->p_ptevents & PTRACE_LWP ? "enabled" : "disabled",
		    data ? "enabled" : "disabled");
		if (data)
			p->p_ptevents |= PTRACE_LWP;
		else
			p->p_ptevents &= ~PTRACE_LWP;
		break;

	case PT_GET_EVENT_MASK:
		if (data != sizeof(p->p_ptevents)) {
			error = EINVAL;
			break;
		}
		CTR2(KTR_PTRACE, "PT_GET_EVENT_MASK: pid %d mask %#x", p->p_pid,
		    p->p_ptevents);
		*(int *)addr = p->p_ptevents;
		break;

	case PT_SET_EVENT_MASK:
		if (data != sizeof(p->p_ptevents)) {
			error = EINVAL;
			break;
		}
		tmp = *(int *)addr;
		if ((tmp & ~(PTRACE_EXEC | PTRACE_SCE | PTRACE_SCX |
		    PTRACE_FORK | PTRACE_LWP | PTRACE_VFORK)) != 0) {
			error = EINVAL;
			break;
		}
		CTR3(KTR_PTRACE, "PT_SET_EVENT_MASK: pid %d mask %#x -> %#x",
		    p->p_pid, p->p_ptevents, tmp);
		p->p_ptevents = tmp;
		break;

	case PT_GET_SC_ARGS:
		CTR1(KTR_PTRACE, "PT_GET_SC_ARGS: pid %d", p->p_pid);
		if ((td2->td_dbgflags & (TDB_SCE | TDB_SCX)) == 0
#ifdef COMPAT_FREEBSD32
		    || (wrap32 && !safe)
#endif
		    ) {
			error = EINVAL;
			break;
		}
		bzero(addr, sizeof(td2->td_sa.args));
#ifdef COMPAT_FREEBSD32
		if (wrap32)
			for (num = 0; num < nitems(td2->td_sa.args); num++)
				((uint32_t *)addr)[num] = (uint32_t)
				    td2->td_sa.args[num];
		else
#endif
			bcopy(td2->td_sa.args, addr, td2->td_sa.narg *
			    sizeof(register_t));
		break;
		
	case PT_STEP:
	case PT_CONTINUE:
	case PT_TO_SCE:
	case PT_TO_SCX:
	case PT_SYSCALL:
	case PT_DETACH:
		/* Zero means do not send any signal */
		if (data < 0 || data > _SIG_MAXSIG) {
			error = EINVAL;
			break;
		}

		switch (req) {
		case PT_STEP:
			CTR3(KTR_PTRACE, "PT_STEP: tid %d (pid %d), sig = %d",
			    td2->td_tid, p->p_pid, data);
			error = ptrace_single_step(td2);
			if (error)
				goto out;
			break;
		case PT_CONTINUE:
		case PT_TO_SCE:
		case PT_TO_SCX:
		case PT_SYSCALL:
			if (addr != (void *)1) {
				error = ptrace_set_pc(td2,
				    (u_long)(uintfptr_t)addr);
				if (error)
					goto out;
			}
			switch (req) {
			case PT_TO_SCE:
				p->p_ptevents |= PTRACE_SCE;
				CTR4(KTR_PTRACE,
		    "PT_TO_SCE: pid %d, events = %#x, PC = %#lx, sig = %d",
				    p->p_pid, p->p_ptevents,
				    (u_long)(uintfptr_t)addr, data);
				break;
			case PT_TO_SCX:
				p->p_ptevents |= PTRACE_SCX;
				CTR4(KTR_PTRACE,
		    "PT_TO_SCX: pid %d, events = %#x, PC = %#lx, sig = %d",
				    p->p_pid, p->p_ptevents,
				    (u_long)(uintfptr_t)addr, data);
				break;
			case PT_SYSCALL:
				p->p_ptevents |= PTRACE_SYSCALL;
				CTR4(KTR_PTRACE,
		    "PT_SYSCALL: pid %d, events = %#x, PC = %#lx, sig = %d",
				    p->p_pid, p->p_ptevents,
				    (u_long)(uintfptr_t)addr, data);
				break;
			case PT_CONTINUE:
				CTR3(KTR_PTRACE,
				    "PT_CONTINUE: pid %d, PC = %#lx, sig = %d",
				    p->p_pid, (u_long)(uintfptr_t)addr, data);
				break;
			}
			break;
		case PT_DETACH:
			/*
			 * Reset the process parent.
			 *
			 * NB: This clears P_TRACED before reparenting
			 * a detached process back to its original
			 * parent.  Otherwise the debugee will be set
			 * as an orphan of the debugger.
			 */
			p->p_flag &= ~(P_TRACED | P_WAITED);
			if (p->p_oppid != p->p_pptr->p_pid) {
				PROC_LOCK(p->p_pptr);
				sigqueue_take(p->p_ksi);
				PROC_UNLOCK(p->p_pptr);

				pp = proc_realparent(p);
				proc_reparent(p, pp, false);
				if (pp == initproc)
					p->p_sigparent = SIGCHLD;
				CTR3(KTR_PTRACE,
			    "PT_DETACH: pid %d reparented to pid %d, sig %d",
				    p->p_pid, pp->p_pid, data);
			} else
				CTR2(KTR_PTRACE, "PT_DETACH: pid %d, sig %d",
				    p->p_pid, data);
			p->p_ptevents = 0;
			FOREACH_THREAD_IN_PROC(p, td3) {
				if ((td3->td_dbgflags & TDB_FSTP) != 0) {
					sigqueue_delete(&td3->td_sigqueue,
					    SIGSTOP);
				}
				td3->td_dbgflags &= ~(TDB_XSIG | TDB_FSTP |
				    TDB_SUSPEND);
			}

			if ((p->p_flag2 & P2_PTRACE_FSTP) != 0) {
				sigqueue_delete(&p->p_sigqueue, SIGSTOP);
				p->p_flag2 &= ~P2_PTRACE_FSTP;
			}

			/* should we send SIGCHLD? */
			/* childproc_continued(p); */
			break;
		}

		sx_xunlock(&proctree_lock);
		proctree_locked = 0;

	sendsig:
		MPASS(proctree_locked == 0);
		
		/* 
		 * Clear the pending event for the thread that just
		 * reported its event (p_xthread).  This may not be
		 * the thread passed to PT_CONTINUE, PT_STEP, etc. if
		 * the debugger is resuming a different thread.
		 *
		 * Deliver any pending signal via the reporting thread.
		 */
		MPASS(p->p_xthread != NULL);
		p->p_xthread->td_dbgflags &= ~TDB_XSIG;
		p->p_xthread->td_xsig = data;
		p->p_xthread = NULL;
		p->p_xsig = data;

		/*
		 * P_WKILLED is insurance that a PT_KILL/SIGKILL
		 * always works immediately, even if another thread is
		 * unsuspended first and attempts to handle a
		 * different signal or if the POSIX.1b style signal
		 * queue cannot accommodate any new signals.
		 */
		if (data == SIGKILL)
			proc_wkilled(p);

		/*
		 * Unsuspend all threads.  To leave a thread
		 * suspended, use PT_SUSPEND to suspend it before
		 * continuing the process.
		 */
		PROC_SLOCK(p);
		p->p_flag &= ~(P_STOPPED_TRACE | P_STOPPED_SIG | P_WAITED);
		thread_unsuspend(p);
		PROC_SUNLOCK(p);
		break;

	case PT_WRITE_I:
	case PT_WRITE_D:
		td2->td_dbgflags |= TDB_USERWR;
		PROC_UNLOCK(p);
		error = 0;
		if (proc_writemem(td, p, (off_t)(uintptr_t)addr, &data,
		    sizeof(int)) != sizeof(int))
			error = ENOMEM;
		else
			CTR3(KTR_PTRACE, "PT_WRITE: pid %d: %p <= %#x",
			    p->p_pid, addr, data);
		PROC_LOCK(p);
		break;

	case PT_READ_I:
	case PT_READ_D:
		PROC_UNLOCK(p);
		error = tmp = 0;
		if (proc_readmem(td, p, (off_t)(uintptr_t)addr, &tmp,
		    sizeof(int)) != sizeof(int))
			error = ENOMEM;
		else
			CTR3(KTR_PTRACE, "PT_READ: pid %d: %p >= %#x",
			    p->p_pid, addr, tmp);
		td->td_retval[0] = tmp;
		PROC_LOCK(p);
		break;

	case PT_IO:
#ifdef COMPAT_FREEBSD32
		if (wrap32) {
			piod32 = addr;
			iov.iov_base = (void *)(uintptr_t)piod32->piod_addr;
			iov.iov_len = piod32->piod_len;
			uio.uio_offset = (off_t)(uintptr_t)piod32->piod_offs;
			uio.uio_resid = piod32->piod_len;
		} else
#endif
		{
			piod = addr;
			iov.iov_base = piod->piod_addr;
			iov.iov_len = piod->piod_len;
			uio.uio_offset = (off_t)(uintptr_t)piod->piod_offs;
			uio.uio_resid = piod->piod_len;
		}
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_segflg = UIO_USERSPACE;
		uio.uio_td = td;
#ifdef COMPAT_FREEBSD32
		tmp = wrap32 ? piod32->piod_op : piod->piod_op;
#else
		tmp = piod->piod_op;
#endif
		switch (tmp) {
		case PIOD_READ_D:
		case PIOD_READ_I:
			CTR3(KTR_PTRACE, "PT_IO: pid %d: READ (%p, %#x)",
			    p->p_pid, (uintptr_t)uio.uio_offset, uio.uio_resid);
			uio.uio_rw = UIO_READ;
			break;
		case PIOD_WRITE_D:
		case PIOD_WRITE_I:
			CTR3(KTR_PTRACE, "PT_IO: pid %d: WRITE (%p, %#x)",
			    p->p_pid, (uintptr_t)uio.uio_offset, uio.uio_resid);
			td2->td_dbgflags |= TDB_USERWR;
			uio.uio_rw = UIO_WRITE;
			break;
		default:
			error = EINVAL;
			goto out;
		}
		PROC_UNLOCK(p);
		error = proc_rwmem(p, &uio);
#ifdef COMPAT_FREEBSD32
		if (wrap32)
			piod32->piod_len -= uio.uio_resid;
		else
#endif
			piod->piod_len -= uio.uio_resid;
		PROC_LOCK(p);
		break;

	case PT_KILL:
		CTR1(KTR_PTRACE, "PT_KILL: pid %d", p->p_pid);
		data = SIGKILL;
		goto sendsig;	/* in PT_CONTINUE above */

	case PT_SETREGS:
		CTR2(KTR_PTRACE, "PT_SETREGS: tid %d (pid %d)", td2->td_tid,
		    p->p_pid);
		td2->td_dbgflags |= TDB_USERWR;
		error = PROC_WRITE(regs, td2, addr);
		break;

	case PT_GETREGS:
		CTR2(KTR_PTRACE, "PT_GETREGS: tid %d (pid %d)", td2->td_tid,
		    p->p_pid);
		error = PROC_READ(regs, td2, addr);
		break;

	case PT_SETFPREGS:
		CTR2(KTR_PTRACE, "PT_SETFPREGS: tid %d (pid %d)", td2->td_tid,
		    p->p_pid);
		td2->td_dbgflags |= TDB_USERWR;
		error = PROC_WRITE(fpregs, td2, addr);
		break;

	case PT_GETFPREGS:
		CTR2(KTR_PTRACE, "PT_GETFPREGS: tid %d (pid %d)", td2->td_tid,
		    p->p_pid);
		error = PROC_READ(fpregs, td2, addr);
		break;

	case PT_SETDBREGS:
		CTR2(KTR_PTRACE, "PT_SETDBREGS: tid %d (pid %d)", td2->td_tid,
		    p->p_pid);
		td2->td_dbgflags |= TDB_USERWR;
		error = PROC_WRITE(dbregs, td2, addr);
		break;

	case PT_GETDBREGS:
		CTR2(KTR_PTRACE, "PT_GETDBREGS: tid %d (pid %d)", td2->td_tid,
		    p->p_pid);
		error = PROC_READ(dbregs, td2, addr);
		break;

	case PT_LWPINFO:
		if (data <= 0 ||
#ifdef COMPAT_FREEBSD32
		    (!wrap32 && data > sizeof(*pl)) ||
		    (wrap32 && data > sizeof(*pl32))) {
#else
		    data > sizeof(*pl)) {
#endif
			error = EINVAL;
			break;
		}
#ifdef COMPAT_FREEBSD32
		if (wrap32) {
			pl = &plr;
			pl32 = addr;
		} else
#endif
		pl = addr;
		bzero(pl, sizeof(*pl));
		pl->pl_lwpid = td2->td_tid;
		pl->pl_event = PL_EVENT_NONE;
		pl->pl_flags = 0;
		if (td2->td_dbgflags & TDB_XSIG) {
			pl->pl_event = PL_EVENT_SIGNAL;
			if (td2->td_si.si_signo != 0 &&
#ifdef COMPAT_FREEBSD32
			    ((!wrap32 && data >= offsetof(struct ptrace_lwpinfo,
			    pl_siginfo) + sizeof(pl->pl_siginfo)) ||
			    (wrap32 && data >= offsetof(struct ptrace_lwpinfo32,
			    pl_siginfo) + sizeof(struct siginfo32)))
#else
			    data >= offsetof(struct ptrace_lwpinfo, pl_siginfo)
			    + sizeof(pl->pl_siginfo)
#endif
			){
				pl->pl_flags |= PL_FLAG_SI;
				pl->pl_siginfo = td2->td_si;
			}
		}
		if (td2->td_dbgflags & TDB_SCE)
			pl->pl_flags |= PL_FLAG_SCE;
		else if (td2->td_dbgflags & TDB_SCX)
			pl->pl_flags |= PL_FLAG_SCX;
		if (td2->td_dbgflags & TDB_EXEC)
			pl->pl_flags |= PL_FLAG_EXEC;
		if (td2->td_dbgflags & TDB_FORK) {
			pl->pl_flags |= PL_FLAG_FORKED;
			pl->pl_child_pid = td2->td_dbg_forked;
			if (td2->td_dbgflags & TDB_VFORK)
				pl->pl_flags |= PL_FLAG_VFORKED;
		} else if ((td2->td_dbgflags & (TDB_SCX | TDB_VFORK)) ==
		    TDB_VFORK)
			pl->pl_flags |= PL_FLAG_VFORK_DONE;
		if (td2->td_dbgflags & TDB_CHILD)
			pl->pl_flags |= PL_FLAG_CHILD;
		if (td2->td_dbgflags & TDB_BORN)
			pl->pl_flags |= PL_FLAG_BORN;
		if (td2->td_dbgflags & TDB_EXIT)
			pl->pl_flags |= PL_FLAG_EXITED;
		pl->pl_sigmask = td2->td_sigmask;
		pl->pl_siglist = td2->td_siglist;
		strcpy(pl->pl_tdname, td2->td_name);
		if ((td2->td_dbgflags & (TDB_SCE | TDB_SCX)) != 0) {
			pl->pl_syscall_code = td2->td_sa.code;
			pl->pl_syscall_narg = td2->td_sa.narg;
		} else {
			pl->pl_syscall_code = 0;
			pl->pl_syscall_narg = 0;
		}
#ifdef COMPAT_FREEBSD32
		if (wrap32)
			ptrace_lwpinfo_to32(pl, pl32);
#endif
		CTR6(KTR_PTRACE,
    "PT_LWPINFO: tid %d (pid %d) event %d flags %#x child pid %d syscall %d",
		    td2->td_tid, p->p_pid, pl->pl_event, pl->pl_flags,
		    pl->pl_child_pid, pl->pl_syscall_code);
		break;

	case PT_GETNUMLWPS:
		CTR2(KTR_PTRACE, "PT_GETNUMLWPS: pid %d: %d threads", p->p_pid,
		    p->p_numthreads);
		td->td_retval[0] = p->p_numthreads;
		break;

	case PT_GETLWPLIST:
		CTR3(KTR_PTRACE, "PT_GETLWPLIST: pid %d: data %d, actual %d",
		    p->p_pid, data, p->p_numthreads);
		if (data <= 0) {
			error = EINVAL;
			break;
		}
		num = imin(p->p_numthreads, data);
		PROC_UNLOCK(p);
		buf = malloc(num * sizeof(lwpid_t), M_TEMP, M_WAITOK);
		tmp = 0;
		PROC_LOCK(p);
		FOREACH_THREAD_IN_PROC(p, td2) {
			if (tmp >= num)
				break;
			buf[tmp++] = td2->td_tid;
		}
		PROC_UNLOCK(p);
		error = copyout(buf, addr, tmp * sizeof(lwpid_t));
		free(buf, M_TEMP);
		if (!error)
			td->td_retval[0] = tmp;
		PROC_LOCK(p);
		break;

	case PT_VM_TIMESTAMP:
		CTR2(KTR_PTRACE, "PT_VM_TIMESTAMP: pid %d: timestamp %d",
		    p->p_pid, p->p_vmspace->vm_map.timestamp);
		td->td_retval[0] = p->p_vmspace->vm_map.timestamp;
		break;

	case PT_VM_ENTRY:
		PROC_UNLOCK(p);
#ifdef COMPAT_FREEBSD32
		if (wrap32)
			error = ptrace_vm_entry32(td, p, addr);
		else
#endif
		error = ptrace_vm_entry(td, p, addr);
		PROC_LOCK(p);
		break;

	default:
#ifdef __HAVE_PTRACE_MACHDEP
		if (req >= PT_FIRSTMACH) {
			PROC_UNLOCK(p);
			error = cpu_ptrace(td2, req, addr, data);
			PROC_LOCK(p);
		} else
#endif
			/* Unknown request. */
			error = EINVAL;
		break;
	}

out:
	/* Drop our hold on this process now that the request has completed. */
	_PRELE(p);
fail:
	PROC_UNLOCK(p);
	if (proctree_locked)
		sx_xunlock(&proctree_lock);
	return (error);
}
#undef PROC_READ
#undef PROC_WRITE

/*
 * Stop a process because of a debugging event;
 * stay stopped until p->p_step is cleared
 * (cleared by PIOCCONT in procfs).
 */
void
stopevent(struct proc *p, unsigned int event, unsigned int val)
{

	PROC_LOCK_ASSERT(p, MA_OWNED);
	p->p_step = 1;
	CTR3(KTR_PTRACE, "stopevent: pid %d event %u val %u", p->p_pid, event,
	    val);
	do {
		if (event != S_EXIT)
			p->p_xsig = val;
		p->p_xthread = NULL;
		p->p_stype = event;	/* Which event caused the stop? */
		wakeup(&p->p_stype);	/* Wake up any PIOCWAIT'ing procs */
		msleep(&p->p_step, &p->p_mtx, PWAIT, "stopevent", 0);
	} while (p->p_step);
}
