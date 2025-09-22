/* $OpenBSD: mem.c,v 1.39 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: mem.c,v 1.26 2000/03/29 03:48:20 simonb Exp $ */

/*
 * Copyright (c) 1988 University of Utah.
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
 * Memory special file
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/filio.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/msgbuf.h>
#include <sys/mman.h>
#include <sys/conf.h>
#include <sys/atomic.h>

#include <machine/cpu.h>

#include <uvm/uvm_extern.h>

#define mmread  mmrw
#define mmwrite mmrw
cdev_decl(mm);

caddr_t zeropage;

/* open counter for aperture */
#ifdef APERTURE
static int ap_open_count = 0;
extern int allowaperture;
#endif

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
#ifdef APERTURE
	case 4:
		if (suser(p) != 0 || !allowaperture)
			return (EPERM);

		/* authorize only one simultaneous open() unless
		 * allowaperture=3 */
		if (ap_open_count > 0 && allowaperture < 3)
			return (EPERM);
		ap_open_count++;
		break;
#endif
	default:
		return (ENXIO);
	}
	return (0);
}

int
mmclose(dev_t dev, int flag, int mode, struct proc *p)
{

#ifdef APERTURE
	if (minor(dev) == 4)
		ap_open_count = 0;
#endif
	return (0);
}

int
mmrw(dev_t dev, struct uio *uio, int flags)
{
	vaddr_t o, v;
	size_t c;
	struct iovec *iov;
	int error = 0, rw;
	extern int msgbufmapped;

	while (uio->uio_resid > 0 && !error) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("mmrw");
			continue;
		}
		switch (minor(dev)) {

		/* minor device 0 is physical memory */
		case 0:
			v = uio->uio_offset;
kmemphys:
			if (v >= ALPHA_K0SEG_TO_PHYS((vaddr_t)msgbufp)) {
				if (msgbufmapped == 0) {
					printf("Message Buf not Mapped\n");
					error = EFAULT;
					break;
				}
			}

			/* Allow reads only in RAM. */
			rw = (uio->uio_rw == UIO_READ) ? PROT_READ : PROT_WRITE;
			if ((alpha_pa_access(v) & rw) != rw) {
				error = EFAULT;
				break;
			}

			o = uio->uio_offset & PGOFSET;
			c = ulmin(uio->uio_resid, PAGE_SIZE - o);
			error =
			    uiomove((caddr_t)ALPHA_PHYS_TO_K0SEG(v), c, uio);
			break;

/* minor device 1 is kernel memory */
		case 1:
			v = uio->uio_offset;

			if (v >= ALPHA_K0SEG_BASE && v <= ALPHA_K0SEG_END) {
				v = ALPHA_K0SEG_TO_PHYS(v);
				goto kmemphys;
			}

			c = ulmin(iov->iov_len, MAXPHYS);
			if (!uvm_kernacc((caddr_t)v, c,
			    uio->uio_rw == UIO_READ ? B_READ : B_WRITE))
				return (EFAULT);
			error = uiomove((caddr_t)v, c, uio);
			break;

		/* minor device 2 is /dev/null */
		case 2:
			if (uio->uio_rw == UIO_WRITE)
				uio->uio_resid = 0;
			return (0);

		/* minor device 12 is /dev/zero */
		case 12:
			if (uio->uio_rw == UIO_WRITE) {
				uio->uio_resid = 0;
				return (0);
			}
			/*
			 * On the first call, allocate and zero a page
			 * of memory for use with /dev/zero.
			 */
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
	switch (minor(dev)) {
	case 0:
		/*
		 * /dev/mem is the only one that makes sense through this
		 * interface.  For /dev/kmem any physaddr we return here
		 * could be transient and hence incorrect or invalid at
		 * a later time.  /dev/null just doesn't make any sense
		 * and /dev/zero is a hack that is handled via the default
		 * pager in mmap().
		 */

		/*
		 * Allow access only in RAM.
		 */
		if ((prot & alpha_pa_access(atop(off))) != prot)
			return (-1);
		return off;
		
#ifdef APERTURE
	case 4:
		/* minor device 4 is aperture driver */
		switch (allowaperture) {
		case 1:
			if ((prot & alpha_pa_access(atop(off))) != prot)
				return (-1);
			return off;
		default:
			return -1;
		}
#endif
	default:
		return -1;
	}
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
