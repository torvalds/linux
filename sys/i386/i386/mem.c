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
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <machine/specialreg.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/memdev.h>

/*
 * Used in /dev/mem drivers and elsewhere
 */
MALLOC_DEFINE(M_MEMDESC, "memdesc", "memory range descriptors");

static struct sx memsxlock;
SX_SYSINIT(memsxlockinit, &memsxlock, "/dev/mem lock");

/* ARGSUSED */
int
memrw(struct cdev *dev, struct uio *uio, int flags)
{
	int o;
	u_int c = 0;
	vm_paddr_t pa;
	struct iovec *iov;
	int error = 0;
	vm_offset_t addr;

	if (dev2unit(dev) != CDEV_MINOR_MEM && dev2unit(dev) != CDEV_MINOR_KMEM)
		return EIO;

	if (dev2unit(dev) == CDEV_MINOR_KMEM && uio->uio_resid > 0) {
		if (!kernacc((caddr_t)(int)uio->uio_offset, uio->uio_resid,
		    uio->uio_rw == UIO_READ ?  VM_PROT_READ : VM_PROT_WRITE))
			return (EFAULT);
	}

	while (uio->uio_resid > 0 && error == 0) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("memrw");
			continue;
		}
		if (dev2unit(dev) == CDEV_MINOR_MEM) {
			if (uio->uio_offset > cpu_getmaxphyaddr()) {
				error = EFAULT;
				break;
			}
			pa = trunc_page(uio->uio_offset);
		} else {
			/*
			 * Extract the physical page since the mapping may
			 * change at any time. This avoids panics on page 
			 * fault in this case but will cause reading/writing
			 * to the wrong page.
			 * Hopefully an application will notice the wrong
			 * data on read access and refrain from writing.
			 * This should be replaced by a special uiomove
			 * type function that just returns an error if there
			 * is a page fault on a kernel page. 
			 */
			addr = trunc_page(uio->uio_offset);
			pa = pmap_extract(kernel_pmap, addr);
			if (pa == 0) 
				return EFAULT;

		}
		
		/* 
		 * XXX UPS This should just use sf_buf_alloc.
		 * Unfortunately sf_buf_alloc needs a vm_page
		 * and we may want to look at memory not covered
		 * by the page array.
		 */

		sx_xlock(&memsxlock);
		pmap_kenter((vm_offset_t)ptvmmap, pa);
		pmap_invalidate_page(kernel_pmap,(vm_offset_t)ptvmmap);

		o = (int)uio->uio_offset & PAGE_MASK;
		c = PAGE_SIZE - o;
		c = min(c, (u_int)iov->iov_len);
		error = uiomove((caddr_t)&ptvmmap[o], (int)c, uio);
		pmap_qremove((vm_offset_t)ptvmmap, 1);
		sx_xunlock(&memsxlock);
	}

	return (error);
}

/*
 * allow user processes to MMAP some memory sections
 * instead of going through read/write
 */
/* ARGSUSED */
int
memmmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot __unused, vm_memattr_t *memattr __unused)
{
	if (dev2unit(dev) == CDEV_MINOR_MEM) {
		if (offset > cpu_getmaxphyaddr())
			return (-1);
		*paddr = offset;
		return (0);
	}
	return (-1);
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
