/*	$OpenBSD: sys_machdep.c,v 1.11 2019/12/20 13:34:41 visa Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)sys_machdep.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/buf.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>

#include <uvm/uvm_extern.h>

#include <mips64/cache.h>
#include <mips64/sysarch.h>

#include <machine/autoconf.h>

int	mips64_cacheflush(struct proc *, struct mips64_cacheflush_args *);

int
sys_sysarch(struct proc *p, void *v, register_t *retval)
{
	struct sys_sysarch_args /* {
		syscallarg(int) op;
		syscallarg(char *) parms;
	} */ *uap = v;
	int error = 0;

	switch(SCARG(uap, op)) {
	case MIPS64_CACHEFLUSH:
	    {
		struct mips64_cacheflush_args cfa;

		if ((error = copyin(SCARG(uap, parms), &cfa, sizeof cfa)) != 0)
			return error;
		error = mips64_cacheflush(p, &cfa);
	    }
		break;
	default:
		error = EINVAL;
		break;
	}

	return error;
}

int
mips64_cacheflush(struct proc *p, struct mips64_cacheflush_args *cfa)
{
	vaddr_t va;
	paddr_t pa;
	size_t sz, chunk;
	struct vm_map *map = &p->p_vmspace->vm_map;
	struct pmap *pm = map->pmap;
	struct vm_map_entry *entry;
	int rc = 0;

	/*
	 * Sanity checks.
	 */
	if ((cfa->which & BCACHE) != cfa->which)
		return EINVAL;

	if (cfa->which == 0)
		return 0;

	va = cfa->va;
	sz = cfa->sz;
	chunk = PAGE_SIZE - (va & PAGE_MASK);
	vm_map_lock_read(map);
	if (va < vm_map_min(map) || va + sz > vm_map_max(map) || va + sz < va)
		rc = EFAULT;
	else while (sz != 0) {
		if (chunk > sz)
			chunk = sz;

		/*
		 * Check for a resident mapping first, this is faster than
		 * uvm_map_lookup_entry().
		 */
		if (pmap_extract(pm, va, &pa) != 0) {
			if (cfa->which & ICACHE)
				Mips_InvalidateICache(p->p_cpu, va, chunk);
			if (cfa->which & DCACHE)
				Mips_HitSyncDCache(p->p_cpu, va, chunk);
		} else {
			if (uvm_map_lookup_entry(map, va, &entry) == FALSE) {
				rc = EFAULT;
				break;
			}
			/* else simply not resident at the moment */
		}

		va += chunk;
		sz -= chunk;
		chunk = PAGE_SIZE;
	}
	vm_map_unlock_read(map);

	return rc;
}
