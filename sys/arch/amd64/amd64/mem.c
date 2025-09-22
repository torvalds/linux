/*	$OpenBSD: mem.c,v 1.39 2024/12/30 02:46:00 guenther Exp $ */
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
#include <sys/buf.h>
#include <sys/filio.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/atomic.h>

#include <machine/cpu.h>

#include <uvm/uvm_extern.h>

caddr_t zeropage;
extern int start, end, etext;

/* open counter for aperture */
#ifdef APERTURE
static int ap_open_count = 0;
extern int allowaperture;

#define VGA_START 0xA0000
#define BIOS_END  0xFFFFF
#endif

#ifdef MTRR
struct mem_range_softc mem_range_softc;
int mem_ioctl(dev_t, u_long, caddr_t, int, struct proc *);
int mem_range_attr_get(struct mem_range_desc *, int *);
int mem_range_attr_set(struct mem_range_desc *, int *);
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
	extern vaddr_t kern_end;
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
			v = PMAP_DIRECT_MAP(uio->uio_offset);
			error = uiomove((caddr_t)v, uio->uio_resid, uio);
			continue;

		/* minor device 1 is kernel memory */
		case 1:
			v = uio->uio_offset;
			c = ulmin(iov->iov_len, MAXPHYS);
			if (v >= (vaddr_t)&start && v < kern_end - c) {
                                if (v < (vaddr_t)&etext - c &&
                                    uio->uio_rw == UIO_WRITE)
                                        return EFAULT;
                        } else if ((!uvm_kernacc((caddr_t)v, c,
			    uio->uio_rw == UIO_READ ? B_READ : B_WRITE)) &&
			    (v < PMAP_DIRECT_BASE || v > PMAP_DIRECT_END - c))
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
			if (zeropage == NULL)
				zeropage =
				    malloc(PAGE_SIZE, M_TEMP, M_WAITOK|M_ZERO);
			c = ulmin(iov->iov_len, PAGE_SIZE);
			error = uiomove(zeropage, c, uio);
			continue;

		default:
			return (ENXIO);
		}
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
	struct proc *p = curproc;	/* XXX */

	switch (minor(dev)) {
	/* minor device 0 is physical memory */
	case 0:
		if (suser(p) != 0 && amd64_pa_used(off))
			return -1;
		return off;

#ifdef APERTURE
	/* minor device 4 is aperture driver */
	case 4:
		/* Check if a write combining mapping is requested. */
		if (off >= MEMRANGE_WC_RANGE)
			off = (off - MEMRANGE_WC_RANGE) | PMAP_WC;

		switch (allowaperture) {
		case 1:
			/* Allow mapping of the VGA framebuffer & BIOS only */
			if ((off >= VGA_START && off <= BIOS_END) ||
			    !amd64_pa_used(off))
				return off;
			else
				return -1;
		case 2:
		case 3:
			/* Allow mapping of the whole 1st megabyte 
			   for x86emu */
			if (off <= BIOS_END || !amd64_pa_used(off))
				return off;
			else
				return -1;
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

#ifdef MTRR
	switch (minor(dev)) {
	case 0:
	case 4:
		return mem_ioctl(dev, cmd, data, flags, p);
	}
#endif
	return (ENOTTY);
}

#ifdef MTRR
/*
 * Operations for changing memory attributes.
 *
 * This is basically just an ioctl shim for mem_range_attr_get
 * and mem_range_attr_set.
 */
int
mem_ioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
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
			md = mallocarray(nd, sizeof(struct mem_range_desc),
			    M_MEMDESC, M_WAITOK);
			error = mem_range_attr_get(md, &nd);
			if (!error)
				error = copyout(md, mo->mo_desc,
					nd * sizeof(struct mem_range_desc));
			free(md, M_MEMDESC, nd * sizeof(struct mem_range_desc));
		} else {
			nd = mem_range_softc.mr_ndesc;
		}
		mo->mo_arg[0] = nd;
		break;
		
	case MEMRANGE_SET:
		md = malloc(sizeof(struct mem_range_desc), M_MEMDESC, M_WAITOK);
		error = copyin(mo->mo_desc, md, sizeof(struct mem_range_desc));
		/* clamp description string */
		md->mr_owner[sizeof(md->mr_owner) - 1] = 0;
		if (error == 0)
			error = mem_range_attr_set(md, &mo->mo_arg[0]);
		free(md, M_MEMDESC, sizeof(struct mem_range_desc));
		break;
	}
	return (error);
}

/*
 * Implementation-neutral, kernel-callable functions for manipulating
 * memory range attributes.
 */
int
mem_range_attr_get(struct mem_range_desc *mrd, int *arg)
{
	/* can we handle this? */
	if (mem_range_softc.mr_op == NULL)
		return (EOPNOTSUPP);

	if (*arg == 0) {
		*arg = mem_range_softc.mr_ndesc;
	} else {
		memcpy(mrd, mem_range_softc.mr_desc, (*arg) * sizeof(struct mem_range_desc));
	}
	return (0);
}

int
mem_range_attr_set(struct mem_range_desc *mrd, int *arg)
{
	/* can we handle this? */
	if (mem_range_softc.mr_op == NULL)
		return (EOPNOTSUPP);

	return (mem_range_softc.mr_op->set(&mem_range_softc, mrd, arg));
}
#endif /* MTRR */
