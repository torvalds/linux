/*	$OpenBSD: uvm_mmap.c,v 1.203 2025/08/15 04:21:00 guenther Exp $	*/
/*	$NetBSD: uvm_mmap.c,v 1.49 2001/02/18 21:19:08 chs Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
 * Copyright (c) 1991, 1993 The Regents of the University of California.
 * Copyright (c) 1988 University of Utah.
 *
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 * from: Utah $Hdr: vm_mmap.c 1.6 91/10/21$
 *      @(#)vm_mmap.c   8.5 (Berkeley) 5/19/94
 * from: Id: uvm_mmap.c,v 1.1.2.14 1998/01/05 21:04:26 chuck Exp
 */

/*
 * uvm_mmap.c: system call interface into VM system, plus kernel vm_mmap
 * function.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/resourcevar.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/conf.h>
#include <sys/signalvar.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <sys/specdev.h>
#include <sys/stdint.h>
#include <sys/pledge.h>
#include <sys/unistd.h>		/* for KBIND* */
#include <sys/user.h>

#include <machine/exec.h>	/* for __LDPGSZ */

#include <sys/syscall.h>
#include <sys/syscallargs.h>

#include <uvm/uvm.h>
#include <uvm/uvm_device.h>
#include <uvm/uvm_vnode.h>

/*
 * Locks used to protect data:
 *	a	atomic
 */

int uvm_mmapanon(vm_map_t, vaddr_t *, vsize_t, vm_prot_t, vm_prot_t, int,
    vsize_t, struct proc *);
int uvm_mmapfile(vm_map_t, vaddr_t *, vsize_t, vm_prot_t, vm_prot_t, int,
    struct vnode *, voff_t, vsize_t, struct proc *);


/*
 * Page align addr and size, returning EINVAL on wraparound.
 */
#define ALIGN_ADDR(addr, size, pageoff)	do {				\
	pageoff = (addr & PAGE_MASK);					\
	if (pageoff != 0) {						\
		if (size > SIZE_MAX - pageoff)				\
			return EINVAL;	/* wraparound */	\
		addr -= pageoff;					\
		size += pageoff;					\
	}								\
	if (size != 0) {						\
		size = (vsize_t)round_page(size);			\
		if (size == 0)						\
			return EINVAL;	/* wraparound */	\
	}								\
} while (0)

/*
 * sys_mquery: provide mapping hints to applications that do fixed mappings
 *
 * flags: 0 or MAP_FIXED (MAP_FIXED - means that we insist on this addr and
 *	don't care about PMAP_PREFER or such)
 * addr: hint where we'd like to place the mapping.
 * size: size of the mapping
 * fd: fd of the file we want to map
 * off: offset within the file
 */
int
sys_mquery(struct proc *p, void *v, register_t *retval)
{
	struct sys_mquery_args /* {
		syscallarg(void *) addr;
		syscallarg(size_t) len;
		syscallarg(int) prot;
		syscallarg(int) flags;
		syscallarg(int) fd;
		syscallarg(off_t) pos;
	} */ *uap = v;
	struct file *fp;
	voff_t uoff;
	int error;
	vaddr_t vaddr;
	int flags = 0;
	vsize_t size;
	vm_prot_t prot;
	int fd;

	vaddr = (vaddr_t) SCARG(uap, addr);
	prot = SCARG(uap, prot);
	size = (vsize_t) SCARG(uap, len);
	fd = SCARG(uap, fd);

	if ((prot & PROT_MASK) != prot)
		return EINVAL;

	if (SCARG(uap, flags) & MAP_FIXED)
		flags |= UVM_FLAG_FIXED;

	if (fd >= 0) {
		if ((error = getvnode(p, fd, &fp)) != 0)
			return error;
		uoff = SCARG(uap, pos);
	} else {
		fp = NULL;
		uoff = UVM_UNKNOWN_OFFSET;
	}

	if (vaddr == 0)
		vaddr = uvm_map_hint(p->p_vmspace, prot, VM_MIN_ADDRESS,
		    VM_MAXUSER_ADDRESS);

	error = uvm_map_mquery(&p->p_vmspace->vm_map, &vaddr, size, uoff,
	    flags);
	if (error == 0)
		*retval = (register_t)(vaddr);

	if (fp != NULL)
		FRELE(fp, p);
	return error;
}

int	uvm_wxabort;	/* [a] */

/*
 * W^X violations are only allowed on permitted filesystems.
 */
static inline int
uvm_wxcheck(struct proc *p, char *call)
{
	struct process *pr = p->p_p;
	int wxallowed = (pr->ps_textvp->v_mount &&
	    (pr->ps_textvp->v_mount->mnt_flag & MNT_WXALLOWED));

	if (wxallowed && (pr->ps_iflags & PSI_WXNEEDED))
		return 0;

	if (atomic_load_int(&uvm_wxabort)) {
		KERNEL_LOCK();
		/* Report W^X failures */
		if (pr->ps_wxcounter++ == 0)
			log(LOG_NOTICE, "%s(%d): %s W^X violation\n",
			    pr->ps_comm, pr->ps_pid, call);
		/* Send uncatchable SIGABRT for coredump */
		sigexit(p, SIGABRT);
		KERNEL_UNLOCK();
	}

	return ENOTSUP;
}

/*
 * sys_mmap: mmap system call.
 *
 * => file offset and address may not be page aligned
 *    - if MAP_FIXED, offset and address must have remainder mod PAGE_SIZE
 *    - if address isn't page aligned the mapping starts at trunc_page(addr)
 *      and the return value is adjusted up by the page offset.
 */
int
sys_mmap(struct proc *p, void *v, register_t *retval)
{
	struct sys_mmap_args /* {
		syscallarg(void *) addr;
		syscallarg(size_t) len;
		syscallarg(int) prot;
		syscallarg(int) flags;
		syscallarg(int) fd;
		syscallarg(off_t) pos;
	} */ *uap = v;
	vaddr_t addr;
	struct vattr va;
	off_t pos;
	vsize_t limit, pageoff, size;
	vm_prot_t prot, maxprot;
	int flags, fd;
	vaddr_t vm_min_address = VM_MIN_ADDRESS;
	struct filedesc *fdp = p->p_fd;
	struct file *fp = NULL;
	struct vnode *vp;
	int error;

	/* first, extract syscall args from the uap. */
	addr = (vaddr_t) SCARG(uap, addr);
	size = (vsize_t) SCARG(uap, len);
	prot = SCARG(uap, prot);
	flags = SCARG(uap, flags);
	fd = SCARG(uap, fd);
	pos = SCARG(uap, pos);

	/*
	 * Validate the flags.
	 */
	if ((prot & PROT_MASK) != prot)
		return EINVAL;
	if ((prot & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC) &&
	    (error = uvm_wxcheck(p, "mmap")))
		return error;

	if ((flags & MAP_FLAGMASK) != flags)
		return EINVAL;
	if ((flags & (MAP_SHARED|MAP_PRIVATE)) == (MAP_SHARED|MAP_PRIVATE))
		return EINVAL;
	if ((flags & (MAP_FIXED|__MAP_NOREPLACE)) == __MAP_NOREPLACE)
		return EINVAL;
	if (flags & MAP_STACK) {
		if ((flags & (MAP_ANON|MAP_PRIVATE)) != (MAP_ANON|MAP_PRIVATE))
			return EINVAL;
		if (flags & ~(MAP_STACK|MAP_FIXED|MAP_ANON|MAP_PRIVATE))
			return EINVAL;
		if (pos != 0)
			return EINVAL;
		if ((prot & (PROT_READ|PROT_WRITE)) != (PROT_READ|PROT_WRITE))
			return EINVAL;
	}
	if (size == 0)
		return EINVAL;

	error = pledge_protexec(p, prot);
	if (error)
		return error;

	/* align file position and save offset.  adjust size. */
	ALIGN_ADDR(pos, size, pageoff);

	/* now check (MAP_FIXED) or get (!MAP_FIXED) the "addr" */
	if (flags & MAP_FIXED) {
		/* adjust address by the same amount as we did the offset */
		addr -= pageoff;
		if (addr & PAGE_MASK)
			return EINVAL;		/* not page aligned */

		if (addr > SIZE_MAX - size)
			return EINVAL;		/* no wrapping! */
		if (VM_MAXUSER_ADDRESS > 0 &&
		    (addr + size) > VM_MAXUSER_ADDRESS)
			return EINVAL;
		if (vm_min_address > 0 && addr < vm_min_address)
			return EINVAL;
	}

	/* check for file mappings (i.e. not anonymous) and verify file. */
	if ((flags & MAP_ANON) == 0) {
		KERNEL_LOCK();
		if ((fp = fd_getfile(fdp, fd)) == NULL) {
			error = EBADF;
			goto out;
		}

		if (fp->f_type != DTYPE_VNODE) {
			error = ENODEV;		/* only mmap vnodes! */
			goto out;
		}
		vp = (struct vnode *)fp->f_data;	/* convert to vnode */

		if (vp->v_type != VREG && vp->v_type != VCHR &&
		    vp->v_type != VBLK) {
			error = ENODEV; /* only REG/CHR/BLK support mmap */
			goto out;
		}

		if (vp->v_type == VREG && (pos + size) < pos) {
			error = EINVAL;		/* no offset wrapping */
			goto out;
		}

		/* special case: catch SunOS style /dev/zero */
		if (vp->v_type == VCHR && iszerodev(vp->v_rdev)) {
			flags |= MAP_ANON;
			FRELE(fp, p);
			fp = NULL;
			KERNEL_UNLOCK();
			goto is_anon;
		}

		/*
		 * Old programs may not select a specific sharing type, so
		 * default to an appropriate one.
		 */
		if ((flags & (MAP_SHARED|MAP_PRIVATE)) == 0) {
#if defined(DEBUG)
			printf("WARNING: defaulted mmap() share type to"
			    " %s (pid %d comm %s)\n",
			    vp->v_type == VCHR ? "MAP_SHARED" : "MAP_PRIVATE",
			    p->p_p->ps_pid, p->p_p->ps_comm);
#endif
			if (vp->v_type == VCHR)
				flags |= MAP_SHARED;	/* for a device */
			else
				flags |= MAP_PRIVATE;	/* for a file */
		}

		/*
		 * MAP_PRIVATE device mappings don't make sense (and aren't
		 * supported anyway).  However, some programs rely on this,
		 * so just change it to MAP_SHARED.
		 */
		if (vp->v_type == VCHR && (flags & MAP_PRIVATE) != 0) {
			flags = (flags & ~MAP_PRIVATE) | MAP_SHARED;
		}

		/* now check protection */
		maxprot = PROT_EXEC;

		/* check read access */
		if (fp->f_flag & FREAD)
			maxprot |= PROT_READ;
		else if (prot & PROT_READ) {
			error = EACCES;
			goto out;
		}

		/* check write access, shared case first */
		if (flags & MAP_SHARED) {
			/*
			 * if the file is writable, only add PROT_WRITE to
			 * maxprot if the file is not immutable, append-only.
			 * otherwise, if we have asked for PROT_WRITE, return
			 * EPERM.
			 */
			if (fp->f_flag & FWRITE) {
				error = VOP_GETATTR(vp, &va, p->p_ucred, p);
				if (error)
					goto out;
				if ((va.va_flags & (IMMUTABLE|APPEND)) == 0)
					maxprot |= PROT_WRITE;
				else if (prot & PROT_WRITE) {
					error = EPERM;
					goto out;
				}
			} else if (prot & PROT_WRITE) {
				error = EACCES;
				goto out;
			}
		} else {
			/* MAP_PRIVATE mappings can always write to */
			maxprot |= PROT_WRITE;
		}
		if ((flags & __MAP_NOFAULT) != 0 ||
		    ((flags & MAP_PRIVATE) != 0 && (prot & PROT_WRITE) != 0)) {
			limit = lim_cur(RLIMIT_DATA);
			if (limit < size ||
			    limit - size < ptoa(p->p_vmspace->vm_dused)) {
				error = ENOMEM;
				goto out;
			}
		}
		error = uvm_mmapfile(&p->p_vmspace->vm_map, &addr, size, prot,
		    maxprot, flags, vp, pos, lim_cur(RLIMIT_MEMLOCK), p);
		FRELE(fp, p);
		KERNEL_UNLOCK();
	} else {		/* MAP_ANON case */
		if (fd != -1)
			return EINVAL;

is_anon:	/* label for SunOS style /dev/zero */

		/* __MAP_NOFAULT only makes sense with a backing object */
		if ((flags & __MAP_NOFAULT) != 0)
			return EINVAL;

		if (prot != PROT_NONE || (flags & MAP_SHARED)) {
			limit = lim_cur(RLIMIT_DATA);
			if (limit < size ||
			    limit - size < ptoa(p->p_vmspace->vm_dused)) {
				return ENOMEM;
			}
		}

		/*
		 * We've been treating (MAP_SHARED|MAP_PRIVATE) == 0 as
		 * MAP_PRIVATE, so make that clear.
		 */
		if ((flags & MAP_SHARED) == 0)
			flags |= MAP_PRIVATE;

		maxprot = PROT_MASK;
		error = uvm_mmapanon(&p->p_vmspace->vm_map, &addr, size, prot,
		    maxprot, flags, lim_cur(RLIMIT_MEMLOCK), p);
	}

	if (error == 0)
		/* remember to add offset */
		*retval = (register_t)(addr + pageoff);

	return error;

out:
	KERNEL_UNLOCK();
	if (fp)
		FRELE(fp, p);
	return error;
}

/*
 * sys_msync: the msync system call (a front-end for flush)
 */

int
sys_msync(struct proc *p, void *v, register_t *retval)
{
	struct sys_msync_args /* {
		syscallarg(void *) addr;
		syscallarg(size_t) len;
		syscallarg(int) flags;
	} */ *uap = v;
	vaddr_t addr;
	vsize_t size, pageoff;
	int flags, uvmflags;

	/* extract syscall args from the uap */
	addr = (vaddr_t)SCARG(uap, addr);
	size = (vsize_t)SCARG(uap, len);
	flags = SCARG(uap, flags);

	/* sanity check flags */
	if ((flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) != 0 ||
			(flags & (MS_ASYNC | MS_SYNC | MS_INVALIDATE)) == 0 ||
			(flags & (MS_ASYNC | MS_SYNC)) == (MS_ASYNC | MS_SYNC))
		return EINVAL;
	if ((flags & (MS_ASYNC | MS_SYNC)) == 0)
		flags |= MS_SYNC;

	/* align the address to a page boundary, and adjust the size accordingly */
	ALIGN_ADDR(addr, size, pageoff);
	if (addr > SIZE_MAX - size)
		return EINVAL;		/* disallow wrap-around. */

	/* translate MS_ flags into PGO_ flags */
	uvmflags = PGO_CLEANIT;
	if (flags & MS_INVALIDATE)
		uvmflags |= PGO_FREE;
	if (flags & MS_SYNC)
		uvmflags |= PGO_SYNCIO;
	else
		uvmflags |= PGO_SYNCIO;	 /* XXXCDC: force sync for now! */

	return uvm_map_clean(&p->p_vmspace->vm_map, addr, addr+size, uvmflags);
}

/*
 * sys_munmap: unmap a users memory
 */
int
sys_munmap(struct proc *p, void *v, register_t *retval)
{
	struct sys_munmap_args /* {
		syscallarg(void *) addr;
		syscallarg(size_t) len;
	} */ *uap = v;
	vaddr_t addr;
	vsize_t size, pageoff;
	vm_map_t map;
	vaddr_t vm_min_address = VM_MIN_ADDRESS;
	struct uvm_map_deadq dead_entries;

	/* get syscall args... */
	addr = (vaddr_t) SCARG(uap, addr);
	size = (vsize_t) SCARG(uap, len);

	/* align address to a page boundary, and adjust size accordingly */
	ALIGN_ADDR(addr, size, pageoff);

	/*
	 * Check for illegal addresses.  Watch out for address wrap...
	 * Note that VM_*_ADDRESS are not constants due to casts (argh).
	 */
	if (addr > SIZE_MAX - size)
		return EINVAL;
	if (VM_MAXUSER_ADDRESS > 0 && addr + size > VM_MAXUSER_ADDRESS)
		return EINVAL;
	if (vm_min_address > 0 && addr < vm_min_address)
		return EINVAL;
	map = &p->p_vmspace->vm_map;


	vm_map_lock(map);	/* lock map so we can checkprot */

	/*
	 * interesting system call semantic: make sure entire range is
	 * allocated before allowing an unmap.
	 */
	if (!uvm_map_checkprot(map, addr, addr + size, PROT_NONE)) {
		vm_map_unlock(map);
		return EINVAL;
	}

	TAILQ_INIT(&dead_entries);
	if (uvm_unmap_remove(map, addr, addr + size, &dead_entries,
	    FALSE, TRUE, TRUE) != 0) {
		vm_map_unlock(map);
		return EPERM;	/* immutable entries found */
	}
	vm_map_unlock(map);	/* and unlock */

	uvm_unmap_detach(&dead_entries, 0);

	return 0;
}

/*
 * sys_mprotect: the mprotect system call
 */
int
sys_mprotect(struct proc *p, void *v, register_t *retval)
{
	struct sys_mprotect_args /* {
		syscallarg(void *) addr;
		syscallarg(size_t) len;
		syscallarg(int) prot;
	} */ *uap = v;
	vaddr_t addr;
	vsize_t size, pageoff;
	vm_prot_t prot;
	int error;

	/*
	 * extract syscall args from uap
	 */

	addr = (vaddr_t)SCARG(uap, addr);
	size = (vsize_t)SCARG(uap, len);
	prot = SCARG(uap, prot);

	if ((prot & PROT_MASK) != prot)
		return EINVAL;
	if ((prot & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC) &&
	    (error = uvm_wxcheck(p, "mprotect")))
		return error;

	error = pledge_protexec(p, prot);
	if (error)
		return error;

	/*
	 * align the address to a page boundary, and adjust the size accordingly
	 */
	ALIGN_ADDR(addr, size, pageoff);
	if (addr > SIZE_MAX - size)
		return EINVAL;		/* disallow wrap-around. */

	return (uvm_map_protect(&p->p_vmspace->vm_map, addr, addr+size,
	    prot, 0, FALSE, TRUE));
}

/*
 * sys_pinsyscalls.  The caller is required to normalize base,len
 * to the minimum .text region, and adjust pintable offsets relative
 * to that base.
 */
int
sys_pinsyscalls(struct proc *p, void *v, register_t *retval)
{
	struct sys_pinsyscalls_args /* {
		syscallarg(void *) base;
		syscallarg(size_t) len;
		syscallarg(u_int *) pins;
		syscallarg(int) npins;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct vm_map *map = &p->p_vmspace->vm_map;
	int npins, error = 0, i, map_flags;
	vaddr_t base;
	size_t len;
	u_int *pins;

	/* Must be called before any threads are created */
	if (P_HASSIBLING(p))
		return (EPERM);

	/* Only allow libc syscall pinning once per process */
	mtx_enter(&pr->ps_vmspace->vm_map.flags_lock);
	map_flags = pr->ps_vmspace->vm_map.flags;
	pr->ps_vmspace->vm_map.flags |= VM_MAP_PINSYSCALL_ONCE;
	mtx_leave(&pr->ps_vmspace->vm_map.flags_lock);
	if (map_flags & VM_MAP_PINSYSCALL_ONCE)
		return (EPERM);

	base = (vaddr_t)SCARG(uap, base);
	len = (vsize_t)SCARG(uap, len);
	if (base > SIZE_MAX - len)
		return (EINVAL);	/* disallow wrap-around. */
	if (base < map->min_offset || base+len > map->max_offset)
		return (EINVAL);

	npins = SCARG(uap, npins);
	if (npins < 1 || npins > SYS_MAXSYSCALL)
		return (E2BIG);
	pins = mallocarray(npins, sizeof(u_int), M_PINSYSCALL, M_WAITOK|M_ZERO);
	if (pins == NULL)
		return (ENOMEM);
	error = copyin(SCARG(uap, pins), pins, npins * sizeof(u_int));
	if (error)
		goto err;

	/* Range-check pintable offsets */
	for (i = 0; i < npins; i++) {
		if (pins[i] == (u_int)-1 || pins[i] == 0)
			continue;
		if (pins[i] > SCARG(uap, len)) {
			error = ERANGE;
			break;
		}
	}
	if (error) {
err:
		free(pins, M_PINSYSCALL, npins * sizeof(u_int));
		return (error);
	}
	pr->ps_libcpin.pn_start = base;
	pr->ps_libcpin.pn_end = base + len;
	pr->ps_libcpin.pn_pins = pins;
	pr->ps_libcpin.pn_npins = npins;

#ifdef PMAP_CHECK_COPYIN
	/* Assume (and insist) on libc.so text being execute-only */
	if (PMAP_CHECK_COPYIN)
		uvm_map_check_copyin_add(map, base, base+len);
#endif
	return (0);
}

/*
 * sys_mimmutable: the mimmutable system call
 */
int
sys_mimmutable(struct proc *p, void *v, register_t *retval)
{
	struct sys_mimmutable_args /* {
		syscallarg(void *) addr;
		syscallarg(size_t) len;
	} */ *uap = v;
	vaddr_t addr;
	vsize_t size, pageoff;

	addr = (vaddr_t)SCARG(uap, addr);
	size = (vsize_t)SCARG(uap, len);

	/*
	 * align the address to a page boundary, and adjust the size accordingly
	 */
	ALIGN_ADDR(addr, size, pageoff);
	if (addr > SIZE_MAX - size)
		return EINVAL;		/* disallow wrap-around. */

	return uvm_map_immutable(&p->p_vmspace->vm_map, addr, addr+size, 1);
}

/*
 * sys_minherit: the minherit system call
 */
int
sys_minherit(struct proc *p, void *v, register_t *retval)
{
	struct sys_minherit_args /* {
		syscallarg(void *) addr;
		syscallarg(size_t) len;
		syscallarg(int) inherit;
	} */ *uap = v;
	vaddr_t addr;
	vsize_t size, pageoff;
	vm_inherit_t inherit;

	addr = (vaddr_t)SCARG(uap, addr);
	size = (vsize_t)SCARG(uap, len);
	inherit = SCARG(uap, inherit);

	/*
	 * align the address to a page boundary, and adjust the size accordingly
	 */
	ALIGN_ADDR(addr, size, pageoff);
	if (addr > SIZE_MAX - size)
		return EINVAL;		/* disallow wrap-around. */

	return (uvm_map_inherit(&p->p_vmspace->vm_map, addr, addr+size,
	    inherit));
}

/*
 * sys_madvise: give advice about memory usage.
 */
int
sys_madvise(struct proc *p, void *v, register_t *retval)
{
	struct sys_madvise_args /* {
		syscallarg(void *) addr;
		syscallarg(size_t) len;
		syscallarg(int) behav;
	} */ *uap = v;
	vaddr_t addr;
	vsize_t size, pageoff;
	int advice, error;

	addr = (vaddr_t)SCARG(uap, addr);
	size = (vsize_t)SCARG(uap, len);
	advice = SCARG(uap, behav);

	/*
	 * align the address to a page boundary, and adjust the size accordingly
	 */
	ALIGN_ADDR(addr, size, pageoff);
	if (addr > SIZE_MAX - size)
		return EINVAL;		/* disallow wrap-around. */

	switch (advice) {
	case MADV_NORMAL:
	case MADV_RANDOM:
	case MADV_SEQUENTIAL:
		error = uvm_map_advice(&p->p_vmspace->vm_map, addr,
		    addr + size, advice);
		break;

	case MADV_WILLNEED:
		/*
		 * Activate all these pages, pre-faulting them in if
		 * necessary.
		 */
		/*
		 * XXX IMPLEMENT ME.
		 * Should invent a "weak" mode for uvm_fault()
		 * which would only do the PGO_LOCKED pgo_get().
		 */
		return 0;

	case MADV_DONTNEED:
		/*
		 * Deactivate all these pages.  We don't need them
		 * any more.  We don't, however, toss the data in
		 * the pages.
		 */
		error = uvm_map_clean(&p->p_vmspace->vm_map, addr, addr + size,
		    PGO_DEACTIVATE);
		break;

	case MADV_FREE:
		/*
		 * These pages contain no valid data, and may be
		 * garbage-collected.  Toss all resources, including
		 * any swap space in use.
		 */
		error = uvm_map_clean(&p->p_vmspace->vm_map, addr, addr + size,
		    PGO_FREE);
		break;

	case MADV_SPACEAVAIL:
		/*
		 * XXXMRG What is this?  I think it's:
		 *
		 *	Ensure that we have allocated backing-store
		 *	for these pages.
		 *
		 * This is going to require changes to the page daemon,
		 * as it will free swap space allocated to pages in core.
		 * There's also what to do for device/file/anonymous memory.
		 */
		return EINVAL;

	default:
		return EINVAL;
	}

	return error;
}

/*
 * sys_mlock: memory lock
 */

int
sys_mlock(struct proc *p, void *v, register_t *retval)
{
	struct sys_mlock_args /* {
		syscallarg(const void *) addr;
		syscallarg(size_t) len;
	} */ *uap = v;
	vaddr_t addr;
	vsize_t size, pageoff;
	int error;

	/* extract syscall args from uap */
	addr = (vaddr_t)SCARG(uap, addr);
	size = (vsize_t)SCARG(uap, len);

	/* align address to a page boundary and adjust size accordingly */
	ALIGN_ADDR(addr, size, pageoff);
	if (addr > SIZE_MAX - size)
		return EINVAL;		/* disallow wrap-around. */

	if (atop(size) + uvmexp.wired > uvmexp.wiredmax)
		return EAGAIN;

#ifdef pmap_wired_count
	if (size + ptoa(pmap_wired_count(vm_map_pmap(&p->p_vmspace->vm_map))) >
			lim_cur(RLIMIT_MEMLOCK))
		return EAGAIN;
#else
	if ((error = suser(p)) != 0)
		return error;
#endif

	error = uvm_map_pageable(&p->p_vmspace->vm_map, addr, addr+size, FALSE,
	    0);
	return error == 0 ? 0 : ENOMEM;
}

/*
 * sys_munlock: unlock wired pages
 */

int
sys_munlock(struct proc *p, void *v, register_t *retval)
{
	struct sys_munlock_args /* {
		syscallarg(const void *) addr;
		syscallarg(size_t) len;
	} */ *uap = v;
	vaddr_t addr;
	vsize_t size, pageoff;
	int error;

	/* extract syscall args from uap */
	addr = (vaddr_t)SCARG(uap, addr);
	size = (vsize_t)SCARG(uap, len);

	/* align address to a page boundary, and adjust size accordingly */
	ALIGN_ADDR(addr, size, pageoff);
	if (addr > SIZE_MAX - size)
		return EINVAL;		/* disallow wrap-around. */

#ifndef pmap_wired_count
	if ((error = suser(p)) != 0)
		return error;
#endif

	error = uvm_map_pageable(&p->p_vmspace->vm_map, addr, addr+size, TRUE,
	    0);
	return error == 0 ? 0 : ENOMEM;
}

/*
 * sys_mlockall: lock all pages mapped into an address space.
 */
int
sys_mlockall(struct proc *p, void *v, register_t *retval)
{
	struct sys_mlockall_args /* {
		syscallarg(int) flags;
	} */ *uap = v;
	int error, flags;

	flags = SCARG(uap, flags);

	if (flags == 0 ||
	    (flags & ~(MCL_CURRENT|MCL_FUTURE)) != 0)
		return EINVAL;

#ifndef pmap_wired_count
	if ((error = suser(p)) != 0)
		return error;
#endif

	error = uvm_map_pageable_all(&p->p_vmspace->vm_map, flags,
	    lim_cur(RLIMIT_MEMLOCK));
	if (error != 0 && error != ENOMEM)
		return EAGAIN;
	return error;
}

/*
 * sys_munlockall: unlock all pages mapped into an address space.
 */
int
sys_munlockall(struct proc *p, void *v, register_t *retval)
{

	(void) uvm_map_pageable_all(&p->p_vmspace->vm_map, 0, 0);
	return 0;
}

/*
 * common code for mmapanon and mmapfile to lock a mmaping
 */
int
uvm_mmaplock(vm_map_t map, vaddr_t *addr, vsize_t size, vm_prot_t prot,
    vsize_t locklimit)
{
	int error;

	/*
	 * POSIX 1003.1b -- if our address space was configured
	 * to lock all future mappings, wire the one we just made.
	 */
	if (prot == PROT_NONE) {
		/*
		 * No more work to do in this case.
		 */
		return 0;
	}

	vm_map_lock(map);
	if (map->flags & VM_MAP_WIREFUTURE) {
		KERNEL_LOCK();
		if ((atop(size) + uvmexp.wired) > uvmexp.wiredmax
#ifdef pmap_wired_count
		    || (locklimit != 0 && (size +
			 ptoa(pmap_wired_count(vm_map_pmap(map)))) >
			locklimit)
#endif
		) {
			error = ENOMEM;
			vm_map_unlock(map);
			/* unmap the region! */
			uvm_unmap(map, *addr, *addr + size);
			KERNEL_UNLOCK();
			return error;
		}
		/*
		 * uvm_map_pageable() always returns the map
		 * unlocked.
		 */
		error = uvm_map_pageable(map, *addr, *addr + size,
		    FALSE, UVM_LK_ENTER);
		if (error != 0) {
			/* unmap the region! */
			uvm_unmap(map, *addr, *addr + size);
			KERNEL_UNLOCK();
			return error;
		}
		KERNEL_UNLOCK();
		return 0;
	}
	vm_map_unlock(map);
	return 0;
}

/*
 * uvm_mmapanon: internal version of mmap for anons
 *
 * - used by sys_mmap
 */
int
uvm_mmapanon(vm_map_t map, vaddr_t *addr, vsize_t size, vm_prot_t prot,
    vm_prot_t maxprot, int flags, vsize_t locklimit, struct proc *p)
{
	int error;
	int advice = MADV_NORMAL;
	unsigned int uvmflag = 0;
	vsize_t align = 0;	/* userland page size */

	/*
	 * for non-fixed mappings, round off the suggested address.
	 * for fixed mappings, check alignment and zap old mappings.
	 */
	if ((flags & MAP_FIXED) == 0) {
		*addr = round_page(*addr);	/* round */
	} else {
		if (*addr & PAGE_MASK)
			return EINVAL;

		uvmflag |= UVM_FLAG_FIXED;
		if ((flags & __MAP_NOREPLACE) == 0)
			uvmflag |= UVM_FLAG_UNMAP;
	}

	if ((flags & MAP_FIXED) == 0 && size >= __LDPGSZ)
		align = __LDPGSZ;
	if ((flags & MAP_SHARED) == 0)
		uvmflag |= UVM_FLAG_COPYONW;
	else
		uvmflag |= UVM_FLAG_OVERLAY;
	if (flags & MAP_STACK)
		uvmflag |= UVM_FLAG_STACK;
	if (flags & MAP_CONCEAL)
		uvmflag |= UVM_FLAG_CONCEAL;

	/* set up mapping flags */
	uvmflag = UVM_MAPFLAG(prot, maxprot,
	    (flags & MAP_SHARED) ? MAP_INHERIT_SHARE : MAP_INHERIT_COPY,
	    advice, uvmflag);

	error = uvm_mapanon(map, addr, size, align, uvmflag);

	if (error == 0)
		error = uvm_mmaplock(map, addr, size, prot, locklimit);
	return error;
}

/*
 * uvm_mmapfile: internal version of mmap for non-anons
 *
 * - used by sys_mmap
 * - caller must page-align the file offset
 */
int
uvm_mmapfile(vm_map_t map, vaddr_t *addr, vsize_t size, vm_prot_t prot,
    vm_prot_t maxprot, int flags, struct vnode *vp, voff_t foff,
    vsize_t locklimit, struct proc *p)
{
	struct uvm_object *uobj;
	int error;
	int advice = MADV_NORMAL;
	unsigned int uvmflag = 0;
	vsize_t align = 0;	/* userland page size */

	/*
	 * for non-fixed mappings, round off the suggested address.
	 * for fixed mappings, check alignment and zap old mappings.
	 */
	if ((flags & MAP_FIXED) == 0) {
		*addr = round_page(*addr);	/* round */
	} else {
		if (*addr & PAGE_MASK)
			return EINVAL;

		uvmflag |= UVM_FLAG_FIXED;
		if ((flags & __MAP_NOREPLACE) == 0)
			uvmflag |= UVM_FLAG_UNMAP;
	}

	/*
	 * attach to underlying vm object.
	 */
	if (vp->v_type != VCHR) {
		uobj = uvn_attach(vp, (flags & MAP_SHARED) ?
		   maxprot : (maxprot & ~PROT_WRITE));

		/*
		 * XXXCDC: hack from old code
		 * don't allow vnodes which have been mapped
		 * shared-writeable to persist [forces them to be
		 * flushed out when last reference goes].
		 * XXXCDC: interesting side effect: avoids a bug.
		 * note that in WRITE [ufs_readwrite.c] that we
		 * allocate buffer, uncache, and then do the write.
		 * the problem with this is that if the uncache causes
		 * VM data to be flushed to the same area of the file
		 * we are writing to... in that case we've got the
		 * buffer locked and our process goes to sleep forever.
		 *
		 * XXXCDC: checking maxprot protects us from the
		 * "persistbug" program but this is not a long term
		 * solution.
		 *
		 * XXXCDC: we don't bother calling uncache with the vp
		 * VOP_LOCKed since we know that we are already
		 * holding a valid reference to the uvn (from the
		 * uvn_attach above), and thus it is impossible for
		 * the uncache to kill the uvn and trigger I/O.
		 */
		if (flags & MAP_SHARED) {
			if ((prot & PROT_WRITE) ||
			    (maxprot & PROT_WRITE)) {
				uvm_vnp_uncache(vp);
			}
		}
	} else {
		uobj = udv_attach(vp->v_rdev,
		    (flags & MAP_SHARED) ? maxprot :
		    (maxprot & ~PROT_WRITE), foff, size);
		/*
		 * XXX Some devices don't like to be mapped with
		 * XXX PROT_EXEC, but we don't really have a
		 * XXX better way of handling this, right now
		 */
		if (uobj == NULL && (prot & PROT_EXEC) == 0) {
			maxprot &= ~PROT_EXEC;
			uobj = udv_attach(vp->v_rdev,
			    (flags & MAP_SHARED) ? maxprot :
			    (maxprot & ~PROT_WRITE), foff, size);
		}
		advice = MADV_RANDOM;
	}

	if (uobj == NULL)
		return vp->v_type == VREG ? ENOMEM : EINVAL;

	if ((flags & MAP_SHARED) == 0)
		uvmflag |= UVM_FLAG_COPYONW;
	if (flags & __MAP_NOFAULT)
		uvmflag |= (UVM_FLAG_NOFAULT | UVM_FLAG_OVERLAY);
	if (flags & MAP_STACK)
		uvmflag |= UVM_FLAG_STACK;
	if (flags & MAP_CONCEAL)
		uvmflag |= UVM_FLAG_CONCEAL;

	/* set up mapping flags */
	uvmflag = UVM_MAPFLAG(prot, maxprot,
	    (flags & MAP_SHARED) ? MAP_INHERIT_SHARE : MAP_INHERIT_COPY,
	    advice, uvmflag);

	error = uvm_map(map, addr, size, uobj, foff, align, uvmflag);

	if (error == 0)
		return uvm_mmaplock(map, addr, size, prot, locklimit);

	/* errors: first detach from the uobj, if any.  */
	if (uobj)
		uobj->pgops->pgo_detach(uobj);

	return error;
}

int
sys_kbind(struct proc *p, void *v, register_t *retval)
{
	struct sys_kbind_args /* {
		syscallarg(const struct __kbind *) param;
		syscallarg(size_t) psize;
		syscallarg(uint64_t) proc_cookie;
	} */ *uap = v;
	const struct __kbind *paramp;
	union {
		struct __kbind uk[KBIND_BLOCK_MAX];
		char upad[KBIND_BLOCK_MAX * sizeof(*paramp) + KBIND_DATA_MAX];
	} param;
	struct uvm_map_deadq dead_entries;
	struct process *pr = p->p_p;
	const char *data;
	vaddr_t baseva, last_baseva, endva, pageoffset, kva;
	size_t psize, s;
	u_long pc;
	int count, i, extra;
	int error, sigill = 0;

	/*
	 * extract syscall args from uap
	 */
	paramp = SCARG(uap, param);
	psize = SCARG(uap, psize);

	/*
	 * If paramp is NULL and we're uninitialized, disable the syscall
	 * for the process.  Raise SIGILL if paramp is NULL and we're
	 * already initialized.
	 *
	 * If paramp is non-NULL and we're uninitialized, do initialization.
	 * Otherwise, do security checks and raise SIGILL on failure.
	 */
	pc = PROC_PC(p);
	mtx_enter(&pr->ps_mtx);
	if (paramp == NULL) {
		/* ld.so disables kbind() when lazy binding is disabled */
		if (pr->ps_kbind_addr == 0)
			pr->ps_kbind_addr = BOGO_PC;
		/* pre-7.3 static binaries disable kbind */
		/* XXX delete check in 2026 */
		else if (pr->ps_kbind_addr != BOGO_PC)
			sigill = 1;
	} else if (pr->ps_kbind_addr == 0) {
		pr->ps_kbind_addr = pc;
		pr->ps_kbind_cookie = SCARG(uap, proc_cookie);
	} else if (pc != pr->ps_kbind_addr || pc == BOGO_PC ||
	    pr->ps_kbind_cookie != SCARG(uap, proc_cookie)) {
		sigill = 1;
	}
	mtx_leave(&pr->ps_mtx);

	/* Raise SIGILL if something is off. */
	if (sigill) {
		KERNEL_LOCK();
		sigexit(p, SIGILL);
		/* NOTREACHED */
		KERNEL_UNLOCK();
	}

	/* We're done if we were disabling the syscall. */
	if (paramp == NULL)
		return 0;

	if (psize < sizeof(struct __kbind) || psize > sizeof(param))
		return EINVAL;
	if ((error = copyin(paramp, &param, psize)))
		return error;

	/*
	 * The param argument points to an array of __kbind structures
	 * followed by the corresponding new data areas for them.  Verify
	 * that the sizes in the __kbind structures add up to the total
	 * size and find the start of the new area.
	 */
	paramp = &param.uk[0];
	s = psize;
	for (count = 0; s > 0 && count < KBIND_BLOCK_MAX; count++) {
		if (s < sizeof(*paramp))
			return EINVAL;
		s -= sizeof(*paramp);

		baseva = (vaddr_t)paramp[count].kb_addr;
		endva = baseva + paramp[count].kb_size - 1;
		if (paramp[count].kb_addr == NULL ||
		    paramp[count].kb_size == 0 ||
		    paramp[count].kb_size > KBIND_DATA_MAX ||
		    baseva >= VM_MAXUSER_ADDRESS ||
		    endva >= VM_MAXUSER_ADDRESS ||
		    s < paramp[count].kb_size)
			return EINVAL;

		s -= paramp[count].kb_size;
	}
	if (s > 0)
		return EINVAL;
	data = (const char *)&paramp[count];

	/* all looks good, so do the bindings */
	last_baseva = VM_MAXUSER_ADDRESS;
	kva = 0;
	TAILQ_INIT(&dead_entries);
	for (i = 0; i < count; i++) {
		baseva = (vaddr_t)paramp[i].kb_addr;
		s = paramp[i].kb_size;
		pageoffset = baseva & PAGE_MASK;
		baseva = trunc_page(baseva);

		/* hppa at least runs PLT entries over page edge */
		extra = (pageoffset + s) & PAGE_MASK;
		if (extra > pageoffset)
			extra = 0;
		else
			s -= extra;
redo:
		/* make sure the desired page is mapped into kernel_map */
		if (baseva != last_baseva) {
			if (kva != 0) {
				vm_map_lock(kernel_map);
				uvm_unmap_remove(kernel_map, kva,
				    kva+PAGE_SIZE, &dead_entries,
				    FALSE, TRUE, FALSE);	/* XXX */
				vm_map_unlock(kernel_map);
				kva = 0;
			}
			if ((error = uvm_map_extract(&p->p_vmspace->vm_map,
			    baseva, PAGE_SIZE, &kva, UVM_EXTRACT_FIXPROT)))
				break;
			last_baseva = baseva;
		}

		/* do the update */
		if ((error = kcopy(data, (char *)kva + pageoffset, s)))
			break;
		data += s;

		if (extra > 0) {
			baseva += PAGE_SIZE;
			s = extra;
			pageoffset = 0;
			extra = 0;
			goto redo;
		}
	}

	if (kva != 0) {
		vm_map_lock(kernel_map);
		uvm_unmap_remove(kernel_map, kva, kva+PAGE_SIZE,
		    &dead_entries, FALSE, TRUE, FALSE);		/* XXX */
		vm_map_unlock(kernel_map);
	}
	uvm_unmap_detach(&dead_entries, AMAP_REFALL);

	return error;
}
