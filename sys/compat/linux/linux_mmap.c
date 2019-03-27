/*-
 * Copyright (c) 2004 Tim J. Robbins
 * Copyright (c) 2002 Doug Rabson
 * Copyright (c) 2000 Marcel Moolenaar
 * Copyright (c) 1994-1995 Søren Schmidt
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/capsicum.h>
#include <sys/file.h>
#include <sys/imgact.h>
#include <sys/ktr.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>

#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>

#include <compat/linux/linux_emul.h>
#include <compat/linux/linux_mmap.h>
#include <compat/linux/linux_persona.h>
#include <compat/linux/linux_util.h>


#define STACK_SIZE  (2 * 1024 * 1024)
#define GUARD_SIZE  (4 * PAGE_SIZE)

#if defined(__amd64__)
static void linux_fixup_prot(struct thread *td, int *prot);
#endif


int
linux_mmap_common(struct thread *td, uintptr_t addr, size_t len, int prot,
    int flags, int fd, off_t pos)
{
	struct proc *p = td->td_proc;
	struct vmspace *vms = td->td_proc->p_vmspace;
	int bsd_flags, error;
	struct file *fp;

	LINUX_CTR6(mmap2, "0x%lx, %ld, %ld, 0x%08lx, %ld, 0x%lx",
	    addr, len, prot, flags, fd, pos);

	error = 0;
	bsd_flags = 0;
	fp = NULL;

	/*
	 * Linux mmap(2):
	 * You must specify exactly one of MAP_SHARED and MAP_PRIVATE
	 */
	if (!((flags & LINUX_MAP_SHARED) ^ (flags & LINUX_MAP_PRIVATE)))
		return (EINVAL);

	if (flags & LINUX_MAP_SHARED)
		bsd_flags |= MAP_SHARED;
	if (flags & LINUX_MAP_PRIVATE)
		bsd_flags |= MAP_PRIVATE;
	if (flags & LINUX_MAP_FIXED)
		bsd_flags |= MAP_FIXED;
	if (flags & LINUX_MAP_ANON) {
		/* Enforce pos to be on page boundary, then ignore. */
		if ((pos & PAGE_MASK) != 0)
			return (EINVAL);
		pos = 0;
		bsd_flags |= MAP_ANON;
	} else
		bsd_flags |= MAP_NOSYNC;
	if (flags & LINUX_MAP_GROWSDOWN)
		bsd_flags |= MAP_STACK;

	/*
	 * PROT_READ, PROT_WRITE, or PROT_EXEC implies PROT_READ and PROT_EXEC
	 * on Linux/i386 if the binary requires executable stack.
	 * We do this only for IA32 emulation as on native i386 this is does not
	 * make sense without PAE.
	 *
	 * XXX. Linux checks that the file system is not mounted with noexec.
	 */
#if defined(__amd64__)
	linux_fixup_prot(td, &prot);
#endif

	/* Linux does not check file descriptor when MAP_ANONYMOUS is set. */
	fd = (bsd_flags & MAP_ANON) ? -1 : fd;
	if (fd != -1) {
		/*
		 * Linux follows Solaris mmap(2) description:
		 * The file descriptor fildes is opened with
		 * read permission, regardless of the
		 * protection options specified.
		 */

		error = fget(td, fd, &cap_mmap_rights, &fp);
		if (error != 0)
			return (error);
		if (fp->f_type != DTYPE_VNODE && fp->f_type != DTYPE_DEV) {
			fdrop(fp, td);
			return (EINVAL);
		}

		/* Linux mmap() just fails for O_WRONLY files */
		if (!(fp->f_flag & FREAD)) {
			fdrop(fp, td);
			return (EACCES);
		}

		fdrop(fp, td);
	}

	if (flags & LINUX_MAP_GROWSDOWN) {
		/*
		 * The Linux MAP_GROWSDOWN option does not limit auto
		 * growth of the region.  Linux mmap with this option
		 * takes as addr the initial BOS, and as len, the initial
		 * region size.  It can then grow down from addr without
		 * limit.  However, Linux threads has an implicit internal
		 * limit to stack size of STACK_SIZE.  Its just not
		 * enforced explicitly in Linux.  But, here we impose
		 * a limit of (STACK_SIZE - GUARD_SIZE) on the stack
		 * region, since we can do this with our mmap.
		 *
		 * Our mmap with MAP_STACK takes addr as the maximum
		 * downsize limit on BOS, and as len the max size of
		 * the region.  It then maps the top SGROWSIZ bytes,
		 * and auto grows the region down, up to the limit
		 * in addr.
		 *
		 * If we don't use the MAP_STACK option, the effect
		 * of this code is to allocate a stack region of a
		 * fixed size of (STACK_SIZE - GUARD_SIZE).
		 */

		if ((caddr_t)addr + len > vms->vm_maxsaddr) {
			/*
			 * Some Linux apps will attempt to mmap
			 * thread stacks near the top of their
			 * address space.  If their TOS is greater
			 * than vm_maxsaddr, vm_map_growstack()
			 * will confuse the thread stack with the
			 * process stack and deliver a SEGV if they
			 * attempt to grow the thread stack past their
			 * current stacksize rlimit.  To avoid this,
			 * adjust vm_maxsaddr upwards to reflect
			 * the current stacksize rlimit rather
			 * than the maximum possible stacksize.
			 * It would be better to adjust the
			 * mmap'ed region, but some apps do not check
			 * mmap's return value.
			 */
			PROC_LOCK(p);
			vms->vm_maxsaddr = (char *)p->p_sysent->sv_usrstack -
			    lim_cur_proc(p, RLIMIT_STACK);
			PROC_UNLOCK(p);
		}

		/*
		 * This gives us our maximum stack size and a new BOS.
		 * If we're using VM_STACK, then mmap will just map
		 * the top SGROWSIZ bytes, and let the stack grow down
		 * to the limit at BOS.  If we're not using VM_STACK
		 * we map the full stack, since we don't have a way
		 * to autogrow it.
		 */
		if (len <= STACK_SIZE - GUARD_SIZE) {
			addr = addr - (STACK_SIZE - GUARD_SIZE - len);
			len = STACK_SIZE - GUARD_SIZE;
		}
	}

	/*
	 * FreeBSD is free to ignore the address hint if MAP_FIXED wasn't
	 * passed.  However, some Linux applications, like the ART runtime,
	 * depend on the hint.  If the MAP_FIXED wasn't passed, but the
	 * address is not zero, try with MAP_FIXED and MAP_EXCL first,
	 * and fall back to the normal behaviour if that fails.
	 */
	if (addr != 0 && (bsd_flags & MAP_FIXED) == 0 &&
	    (bsd_flags & MAP_EXCL) == 0) {
		error = kern_mmap(td, addr, len, prot,
		    bsd_flags | MAP_FIXED | MAP_EXCL, fd, pos);
		if (error == 0)
			goto out;
	}

	error = kern_mmap(td, addr, len, prot, bsd_flags, fd, pos);
out:
	LINUX_CTR2(mmap2, "return: %d (%p)", error, td->td_retval[0]);

	return (error);
}

int
linux_mprotect_common(struct thread *td, uintptr_t addr, size_t len, int prot)
{

#if defined(__amd64__)
	linux_fixup_prot(td, &prot);
#endif
	return (kern_mprotect(td, addr, len, prot));
}

#if defined(__amd64__)
static void
linux_fixup_prot(struct thread *td, int *prot)
{
	struct linux_pemuldata *pem;

	if (SV_PROC_FLAG(td->td_proc, SV_ILP32) && *prot & PROT_READ) {
		pem = pem_find(td->td_proc);
		if (pem->persona & LINUX_READ_IMPLIES_EXEC)
			*prot |= PROT_EXEC;
	}

}
#endif
