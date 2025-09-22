/*	$OpenBSD: mem.c,v 1.10 2024/12/30 02:46:00 guenther Exp $ */

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
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/atomic.h>

#include <machine/conf.h>

#include <uvm/uvm_extern.h>

caddr_t zpage;
extern vaddr_t first_addr, last_addr;
extern caddr_t kernelstart;
extern void *etext;

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
	vaddr_t v;
	size_t c;
	struct iovec *iov;
	int error = 0;

	while (uio->uio_resid > 0 && error == 0) {
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
			error = uiomove((caddr_t)v, uio->uio_resid, uio);
			continue;

		/* minor device 1 is kernel memory */
		case 1:
			v = uio->uio_offset;
			c = ulmin(iov->iov_len, MAXPHYS);
			if (v >= (vaddr_t)&kernelstart &&
			    v < (vaddr_t)first_addr) {
				if (v < (vaddr_t)etext &&
				    uio->uio_rw == UIO_WRITE)
					return (EFAULT);
			} else if (!uvm_kernacc((caddr_t)v, c,
			    uio->uio_rw == UIO_READ ? B_READ : B_WRITE))
				return (EFAULT);
			error = uiomove((caddr_t)v, c, uio);
			continue;

		/* minor device 2 is /dev/null */
		case 2:
			if (uio->uio_rw == UIO_WRITE)
				uio->uio_resid = 0;
			return (0);

		/* minor device 12 is /dev/zero */
		case 12:
			if (uio->uio_rw == UIO_WRITE) {
				c = iov->iov_len;
				break;
			}
			if (zpage == NULL)
				zpage = malloc(PAGE_SIZE, M_TEMP,
				    M_WAITOK | M_ZERO);
			c = ulmin(iov->iov_len, PAGE_SIZE);
			error = uiomove(zpage, c, uio);
			continue;

		default:
			return (ENXIO);
		}
		if (error)
			break;
		iov->iov_base += c;
		iov->iov_len -= c;
		uio->uio_offset += c;
		uio->uio_resid -= c;
	}

	return (error);
}

paddr_t
mmmmap(dev_t dev, off_t off, int prot)
{
	return (-1);
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
