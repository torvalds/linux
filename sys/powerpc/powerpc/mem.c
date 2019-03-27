/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1986, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department, and code derived from software contributed to
 * Berkeley by William Jolitz.
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
 *	from: Utah $Hdr: mem.c 1.13 89/10/08$
 *	from: @(#)mem.c	7.2 (Berkeley) 5/9/91
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Memory special file
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/msgbuf.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/uio.h>

#include <machine/md_var.h>
#include <machine/vmparam.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_page.h>

#include <machine/memdev.h>

static void ppc_mrinit(struct mem_range_softc *);
static int ppc_mrset(struct mem_range_softc *, struct mem_range_desc *, int *);

MALLOC_DEFINE(M_MEMDESC, "memdesc", "memory range descriptors");

static struct mem_range_ops ppc_mem_range_ops = {
	ppc_mrinit,
	ppc_mrset,
	NULL,
	NULL
};
struct mem_range_softc mem_range_softc = {
	&ppc_mem_range_ops,
	0, 0, NULL
}; 

/* ARGSUSED */
int
memrw(struct cdev *dev, struct uio *uio, int flags)
{
	struct iovec *iov;
	int error = 0;
	vm_offset_t va, eva, off, v;
	vm_prot_t prot;
	struct vm_page m;
	vm_page_t marr;
	vm_size_t cnt;

	cnt = 0;
	error = 0;

	while (uio->uio_resid > 0 && !error) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("memrw");
			continue;
		}
		if (dev2unit(dev) == CDEV_MINOR_MEM) {
			v = uio->uio_offset;

kmem_direct_mapped:	off = v & PAGE_MASK;
			cnt = PAGE_SIZE - ((vm_offset_t)iov->iov_base &
			    PAGE_MASK);
			cnt = min(cnt, PAGE_SIZE - off);
			cnt = min(cnt, iov->iov_len);

			if (mem_valid(v, cnt)) {
				error = EFAULT;
				break;
			}
	
			if (hw_direct_map && !pmap_dev_direct_mapped(v, cnt)) {
				error = uiomove((void *)PHYS_TO_DMAP(v), cnt,
				    uio);
			} else {
				m.phys_addr = trunc_page(v);
				marr = &m;
				error = uiomove_fromphys(&marr, off, cnt, uio);
			}
		}
		else if (dev2unit(dev) == CDEV_MINOR_KMEM) {
			va = uio->uio_offset;

			if ((va < VM_MIN_KERNEL_ADDRESS) || (va > virtual_end)) {
				v = DMAP_TO_PHYS(va);
				goto kmem_direct_mapped;
			}

			va = trunc_page(uio->uio_offset);
			eva = round_page(uio->uio_offset
			    + iov->iov_len);

			/* 
			 * Make sure that all the pages are currently resident
			 * so that we don't create any zero-fill pages.
			 */

			for (; va < eva; va += PAGE_SIZE)
				if (pmap_extract(kernel_pmap, va) == 0)
					return (EFAULT);

			prot = (uio->uio_rw == UIO_READ)
			    ? VM_PROT_READ : VM_PROT_WRITE;

			va = uio->uio_offset;
			if (kernacc((void *) va, iov->iov_len, prot)
			    == FALSE)
				return (EFAULT);

			error = uiomove((void *)va, iov->iov_len, uio);

			continue;
		}
	}

	return (error);
}

/*
 * allow user processes to MMAP some memory sections
 * instead of going through read/write
 */
int
memmmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)
{
	int i;

	if (dev2unit(dev) == CDEV_MINOR_MEM)
		*paddr = offset;
	else
		return (EFAULT);

	for (i = 0; i < mem_range_softc.mr_ndesc; i++) {
		if (!(mem_range_softc.mr_desc[i].mr_flags & MDF_ACTIVE))
			continue;

		if (offset >= mem_range_softc.mr_desc[i].mr_base &&
		    offset < mem_range_softc.mr_desc[i].mr_base +
		    mem_range_softc.mr_desc[i].mr_len) {
			switch (mem_range_softc.mr_desc[i].mr_flags &
			    MDF_ATTRMASK) {
			case MDF_WRITEBACK:
				*memattr = VM_MEMATTR_WRITE_BACK;
				break;
			case MDF_WRITECOMBINE:
				*memattr = VM_MEMATTR_WRITE_COMBINING;
				break;
			case MDF_UNCACHEABLE:
				*memattr = VM_MEMATTR_UNCACHEABLE;
				break;
			case MDF_WRITETHROUGH:
				*memattr = VM_MEMATTR_WRITE_THROUGH;
				break;
			}

			break;
		}
	}

	return (0);
}

static void
ppc_mrinit(struct mem_range_softc *sc)
{
	sc->mr_cap = 0;
	sc->mr_ndesc = 8; /* XXX: Should be dynamically expandable */
	sc->mr_desc = malloc(sc->mr_ndesc * sizeof(struct mem_range_desc),
	    M_MEMDESC, M_WAITOK | M_ZERO);
}

static int
ppc_mrset(struct mem_range_softc *sc, struct mem_range_desc *desc, int *arg)
{
	int i;

	switch(*arg) {
	case MEMRANGE_SET_UPDATE:
		for (i = 0; i < sc->mr_ndesc; i++) {
			if (!sc->mr_desc[i].mr_len) {
				sc->mr_desc[i] = *desc;
				sc->mr_desc[i].mr_flags |= MDF_ACTIVE;
				return (0);
			}
			if (sc->mr_desc[i].mr_base == desc->mr_base &&
			    sc->mr_desc[i].mr_len == desc->mr_len)
				return (EEXIST);
		}
		return (ENOSPC);
	case MEMRANGE_SET_REMOVE:
		for (i = 0; i < sc->mr_ndesc; i++)
			if (sc->mr_desc[i].mr_base == desc->mr_base &&
			    sc->mr_desc[i].mr_len == desc->mr_len) {
				bzero(&sc->mr_desc[i], sizeof(sc->mr_desc[i]));
				return (0);
			}
		return (ENOENT);
	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

/*
 * Operations for changing memory attributes.
 *
 * This is basically just an ioctl shim for mem_range_attr_get
 * and mem_range_attr_set.
 */
/* ARGSUSED */
int 
memioctl(struct cdev *dev __unused, u_long cmd, caddr_t data, int flags,
    struct thread *td)
{
	int nd, error = 0;
	struct mem_range_op *mo = (struct mem_range_op *)data;
	struct mem_range_desc *md;
	
	/* is this for us? */
	if ((cmd != MEMRANGE_GET) &&
	    (cmd != MEMRANGE_SET))
		return (ENOTTY);

	/* any chance we can handle this? */
	if (mem_range_softc.mr_op == NULL)
		return (EOPNOTSUPP);

	/* do we have any descriptors? */
	if (mem_range_softc.mr_ndesc == 0)
		return (ENXIO);

	switch (cmd) {
	case MEMRANGE_GET:
		nd = imin(mo->mo_arg[0], mem_range_softc.mr_ndesc);
		if (nd > 0) {
			md = (struct mem_range_desc *)
				malloc(nd * sizeof(struct mem_range_desc),
				       M_MEMDESC, M_WAITOK);
			error = mem_range_attr_get(md, &nd);
			if (!error)
				error = copyout(md, mo->mo_desc, 
					nd * sizeof(struct mem_range_desc));
			free(md, M_MEMDESC);
		}
		else
			nd = mem_range_softc.mr_ndesc;
		mo->mo_arg[0] = nd;
		break;
		
	case MEMRANGE_SET:
		md = (struct mem_range_desc *)malloc(sizeof(struct mem_range_desc),
						    M_MEMDESC, M_WAITOK);
		error = copyin(mo->mo_desc, md, sizeof(struct mem_range_desc));
		/* clamp description string */
		md->mr_owner[sizeof(md->mr_owner) - 1] = 0;
		if (error == 0)
			error = mem_range_attr_set(md, &mo->mo_arg[0]);
		free(md, M_MEMDESC);
		break;
	}
	return (error);
}

