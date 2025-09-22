/*	$OpenBSD: mem.c,v 1.24 2024/12/30 02:46:00 guenther Exp $	*/
/*	$NetBSD: mem.c,v 1.18 2001/04/24 04:31:12 thorpej Exp $ */

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
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/conf.h>
#include <sys/atomic.h>

#include <machine/conf.h>
#include <machine/ctlreg.h>

#include <uvm/uvm_extern.h>

vaddr_t prom_vstart = 0xf000000;
vaddr_t prom_vend = 0xf0100000;
caddr_t zeropage;

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

static struct rwlock physlock = RWLOCK_INITIALIZER("mmrw");

int
mmrw(dev_t dev, struct uio *uio, int flags)
{
	vaddr_t o, v;
	size_t c;
	struct iovec *iov;
	int error = 0;
	vm_prot_t prot;
	extern caddr_t vmmap;

	if (minor(dev) == 0) {
		/* lock against other uses of shared vmmap */
		error = rw_enter(&physlock, RW_WRITE | RW_INTR);
		if (error)
			return (error);
	}
	while (uio->uio_resid > 0 && error == 0) {
		int n;
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("mmrw");
			continue;
		}

		/* Note how much is still to go */
		n = uio->uio_resid;

		switch (minor(dev)) {

		/* minor device 0 is physical memory */
		case 0:
			v = uio->uio_offset;
			if (!pmap_pa_exists(v)) {
				error = EFAULT;
				goto unlock;
			}
			prot = uio->uio_rw == UIO_READ ? PROT_READ :
			    PROT_WRITE;
			pmap_enter(pmap_kernel(), (vaddr_t)vmmap,
			    trunc_page(v), prot, prot|PMAP_WIRED);
			pmap_update(pmap_kernel());
			o = uio->uio_offset & PGOFSET;
			c = ulmin(uio->uio_resid, NBPG - o);
			error = uiomove((caddr_t)vmmap + o, c, uio);
			pmap_remove(pmap_kernel(), (vaddr_t)vmmap,
			    (vaddr_t)vmmap + NBPG);
			pmap_update(pmap_kernel());
			break;

		/* minor device 1 is kernel memory */
		case 1:
			v = uio->uio_offset;
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
				return(0);
			}
			if (zeropage == NULL)
				zeropage = malloc(NBPG, M_TEMP,
				    M_WAITOK | M_ZERO);
			c = ulmin(iov->iov_len, NBPG);
			error = uiomove(zeropage, c, uio);
			break;

		default:
			return (ENXIO);
		}

		/* If we didn't make any progress (i.e. EOF), we're done here */
		if (n == uio->uio_resid)
			break;
	}
	if (minor(dev) == 0) {
unlock:
		rw_exit(&physlock);
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
