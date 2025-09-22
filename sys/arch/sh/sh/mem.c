/*	$OpenBSD: mem.c,v 1.15 2024/12/30 02:46:00 guenther Exp $	*/
/*	$NetBSD: mem.c,v 1.21 2006/07/23 22:06:07 ad Exp $	*/

/*
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)mem.c	8.3 (Berkeley) 1/12/94
 */

/*
 * Copyright (c) 1988 University of Utah.
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
 *	@(#)mem.c	8.3 (Berkeley) 1/12/94
 */

/*
 * Memory special file
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/filio.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/atomic.h>

#include <uvm/uvm_extern.h>

caddr_t zeropage;
boolean_t __mm_mem_addr(paddr_t);

#define mmread	mmrw
#define mmwrite	mmrw
cdev_decl(mm);


int
mmopen(dev_t dev, int flag, int mode, struct proc *p)
{
	extern int allowkmem;

	switch (minor(dev)) {
	case 0:
	case 1:
		if ((int)atomic_load_int(&securelevel) <= 0 ||
		    atomic_load_int(&allowkmem))
			break;
		return (EPERM);
	case 2:
	case 12:
		break;
	default:
		return (ENXIO);
	}
	return (0);
}

int
mmclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (0);
}

int
mmrw(dev_t dev, struct uio *uio, int flags)
{
	struct iovec *iov;
	vaddr_t v, o;
	size_t c;
	int error = 0;

	while (uio->uio_resid > 0 && !error) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("mmrw");
			continue;
		}

		v = uio->uio_offset;

		switch (minor(dev)) {
		case 0:
			/* Physical address */
			if (__mm_mem_addr(v)) {
				o = v & PGOFSET;
				c = ulmin(uio->uio_resid, PAGE_SIZE - o);
				error = uiomove((caddr_t)SH3_PHYS_TO_P1SEG(v),
				    c, uio);
			} else {
				return (EFAULT);
			}
			break;

		case 1:
			if (v < SH3_P1SEG_BASE)			/* P0 */
				return (EFAULT);
			if (v < SH3_P2SEG_BASE) {		/* P1 */
				/* permitted */
			/*
				if (__mm_mem_addr(SH3_P1SEG_TO_PHYS(v))
				    == FALSE)
					return (EFAULT);
			*/
				c = ulmin(iov->iov_len, MAXPHYS);
				error = uiomove((caddr_t)v, c, uio);
			} else if (v < SH3_P3SEG_BASE)		/* P2 */
				return (EFAULT);
			else {					/* P3 */
				c = ulmin(iov->iov_len, MAXPHYS);
				if (!uvm_kernacc((void *)v, c,
				    uio->uio_rw == UIO_READ ? B_READ : B_WRITE))
					return (EFAULT);
				error = uiomove((caddr_t)v, c, uio);
			}
			break;

		case 2:
			if (uio->uio_rw == UIO_WRITE)
				uio->uio_resid = 0;
			return (0);

		case 12:
			if (uio->uio_rw == UIO_WRITE) {
				uio->uio_resid = 0;
				return (0);
			}
			if (zeropage == NULL)
				zeropage = malloc(PAGE_SIZE, M_TEMP,
				    M_WAITOK | M_ZERO);
			c = ulmin(iov->iov_len, PAGE_SIZE);
			error = uiomove(zeropage, c, uio);
			break;

		default:
			return (ENXIO);
		}
	}

	return (error);
}

paddr_t
mmmmap(dev_t dev, off_t off, int prot)
{
	struct proc *p = curproc;

	if (minor(dev) != 0)
		return (-1);

	if (__mm_mem_addr((paddr_t)off) == FALSE && suser(p) != 0)
		return (-1);
	return ((paddr_t)off);
}

int
mmioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
        switch (cmd) {
        case FIOASYNC:
                /* handled by fd layer */
                return 0;
        }

	return (ENOTTY);
}

/*
 * boolean_t __mm_mem_addr(paddr_t pa):
 *	Check specified physical address is in physical memory, with the
 *	kernel image off-limits.
 */
boolean_t
__mm_mem_addr(paddr_t pa)
{
#if 0
	extern vaddr_t kernend; /* from machdep.c */
#endif
	struct vm_physseg *seg;
	unsigned int segno;

	for (segno = 0, seg = vm_physmem; segno < vm_nphysseg; segno++, seg++) {
		if (pa < seg->start || pa >= seg->end)
			continue;

#if 0
		/*
		 * This assumes the kernel image occupies the beginning of a
		 * memory segment.
		 */
		if (kernend >= seg->start && kernend < seg->end) {
			if (pa < kernend)
				return (FALSE);
		}
#endif

		return (TRUE);
	}

	return (FALSE);
}
