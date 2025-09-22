/*	$OpenBSD: mem.c,v 1.32 2024/12/30 02:46:00 guenther Exp $	*/
/*	$NetBSD: mem.c,v 1.1 1996/09/30 16:34:50 ws Exp $ */

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
#include <sys/ioccom.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/atomic.h>

#include <machine/cpu.h>

#include <uvm/uvm_extern.h>

#include <machine/conf.h>

/* open counter for aperture */
#ifdef APERTURE
static int ap_open_count = 0;
extern int allowaperture;
#endif

#ifndef SMALL_KERNEL

/* 
 * The EEPROMs for Serial Presence Detect don't show up in the
 * OpenFirmware tree, but their contents are available through the
 * "dimm-info" property of the "/memory" node.  To make the
 * information available, we fake up an I2C bus with EEPROMs
 * containing the appropriate slices of the "dimm-info" property.
 */

#include <machine/autoconf.h>

#include <dev/i2c/i2cvar.h>
#include <dev/ofw/openfirm.h>

struct mem_softc {
	struct device sc_dev;

	uint8_t *sc_buf;
	int sc_len;
};

/* Size of a single SPD entry in "dimm-info" property. */
#define SPD_SIZE	128

int	mem_match(struct device *, void *, void *);
void	mem_attach(struct device *, struct device *, void *);

struct cfdriver mem_cd = {
	NULL, "mem", DV_DULL
};

const struct cfattach mem_ca = {
	sizeof(struct mem_softc), mem_match, mem_attach
};

int	mem_i2c_acquire_bus(void *, int);
void	mem_i2c_release_bus(void *, int);
int	mem_i2c_exec(void *, i2c_op_t, i2c_addr_t,
	    const void *, size_t, void *, size_t, int);

int
mem_match(struct device *parent, void *cf, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "mem") == 0)
		return (1);
	return (0);
}

void
mem_attach(struct device *parent, struct device *self, void *aux)
{
	struct mem_softc *sc = (struct mem_softc *)self;
	struct confargs *ca = aux;
	struct i2c_controller ic;
	struct i2c_attach_args ia;

	sc->sc_len = OF_getproplen(ca->ca_node, "dimm-info");
	if (sc->sc_len > 0) {
		sc->sc_buf = malloc(sc->sc_len, M_DEVBUF, M_NOWAIT);
		if (sc->sc_buf == NULL) {
			printf(": can't allocate memory\n");
			return;
		}
	}

	printf("\n");

	if (sc->sc_len > 0) {
		OF_getprop(ca->ca_node, "dimm-info", sc->sc_buf, sc->sc_len);

		memset(&ic, 0, sizeof ic);
		ic.ic_cookie = sc;
		ic.ic_acquire_bus = mem_i2c_acquire_bus;
		ic.ic_release_bus = mem_i2c_release_bus;
		ic.ic_exec = mem_i2c_exec;

		memset(&ia, 0, sizeof ia);
		ia.ia_tag = &ic;
		ia.ia_name = "spd";
		ia.ia_addr = 0;
		while (ia.ia_addr * SPD_SIZE < sc->sc_len) {
			/* Skip entries that have not been filled in. */
			if (sc->sc_buf[ia.ia_addr * SPD_SIZE] != 0)
				config_found(self, &ia, NULL);
			ia.ia_addr++;
		}

		/* No need to keep the "dimm-info" contents around. */
		free(sc->sc_buf, M_DEVBUF, sc->sc_len);
		sc->sc_len = -1;
	}
}

int
mem_i2c_acquire_bus(void *cookie, int flags)
{
	return (0);
}

void
mem_i2c_release_bus(void *cookie, int flags)
{
}

int
mem_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct mem_softc *sc = cookie;
	size_t off;

	if (op != I2C_OP_READ_WITH_STOP || cmdlen != 1)
		return (EINVAL);

	off = addr * SPD_SIZE + *(const uint8_t *)cmdbuf;
	if (off + len > sc->sc_len)
		return (EIO);

	memcpy(buf, &sc->sc_buf[off], len);
	return (0);
}

#endif /* SMALL_KERNEL */

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
	return 0;
}

int
mmrw(dev_t dev, struct uio *uio, int flags)
{
	vaddr_t v;
	vsize_t c;
	struct iovec *iov;
	int error = 0;
	static caddr_t zeropage;

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
			c = uio->uio_resid;
			/* This doesn't allow device mapping!	XXX */
			pmap_real_memory(&v, &c);
			error = uiomove((caddr_t)v, c, uio);
			continue;

		/* minor device 1 is kernel memory */
		case 1:
			v = uio->uio_offset;
			c = ulmin(iov->iov_len, MAXPHYS);
			error = uiomove((caddr_t)v, c, uio);
			continue;

		/* minor device 2 is /dev/null */
		case 2:
			if (uio->uio_rw == UIO_WRITE)
				uio->uio_resid = 0;
			return 0;

		/* minor device 12 is /dev/zero */
		case 12:
			if (uio->uio_rw == UIO_WRITE) {
				c = iov->iov_len;
				break;
			}
			if (zeropage == NULL)
				zeropage = malloc(PAGE_SIZE, M_TEMP,
				    M_WAITOK | M_ZERO);
			c = ulmin(iov->iov_len, PAGE_SIZE);
			error = uiomove(zeropage, c, uio);
			continue;

		default:
			return ENXIO;
		}
		if (error)
			break;
		iov->iov_base += c;
		iov->iov_len -= c;
		uio->uio_offset += c;
		uio->uio_resid -= c;
	}
	return error;
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
