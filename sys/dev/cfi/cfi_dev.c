/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007, Juniper Networks, Inc.
 * Copyright (c) 2012-2013, SRI International
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * (FA8750-10-C-0237) ("CTSRD"), as part of the DARPA CRASH research
 * programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_cfi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/malloc.h>   
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <sys/cfictl.h>

#include <machine/atomic.h>
#include <machine/bus.h>

#include <dev/cfi/cfi_var.h>

static d_open_t cfi_devopen;
static d_close_t cfi_devclose;
static d_read_t cfi_devread;
static d_write_t cfi_devwrite;
static d_ioctl_t cfi_devioctl;

struct cdevsw cfi_cdevsw = {
	.d_version	=	D_VERSION,
	.d_flags	=	0,
	.d_name		=	cfi_driver_name,
	.d_open		=	cfi_devopen,
	.d_close	=	cfi_devclose,
	.d_read		=	cfi_devread,
	.d_write	=	cfi_devwrite,
	.d_ioctl	=	cfi_devioctl,
};

/*
 * Begin writing into a new block/sector.  We read the sector into
 * memory and keep updating that, until we move into another sector
 * or the process stops writing. At that time we write the whole
 * sector to flash (see cfi_block_finish).  To avoid unneeded erase
 * cycles, keep a pristine copy of the sector on hand.
 */
int
cfi_block_start(struct cfi_softc *sc, u_int ofs)
{
	union {
		uint8_t		*x8;
		uint16_t	*x16;
		uint32_t	*x32;
	} ptr;
	u_int rofs, rsz;
	uint32_t val;
	int r;

	rofs = 0;
	for (r = 0; r < sc->sc_regions; r++) {
		rsz = sc->sc_region[r].r_blocks * sc->sc_region[r].r_blksz;
		if (ofs < rofs + rsz)
			break;
		rofs += rsz;
	}
	if (r == sc->sc_regions)
		return (EFAULT);

	sc->sc_wrbufsz = sc->sc_region[r].r_blksz;
	sc->sc_wrbuf = malloc(sc->sc_wrbufsz, M_TEMP, M_WAITOK);
	sc->sc_wrofs = ofs - (ofs - rofs) % sc->sc_wrbufsz;

	/* Read the block from flash for byte-serving. */
	ptr.x8 = sc->sc_wrbuf;
	for (r = 0; r < sc->sc_wrbufsz; r += sc->sc_width) {
		val = cfi_read_raw(sc, sc->sc_wrofs + r);
		switch (sc->sc_width) {
		case 1:
			*(ptr.x8)++ = val;
			break;
		case 2:
			*(ptr.x16)++ = val;
			break;
		case 4:
			*(ptr.x32)++ = val;
			break;
		}
	}
	sc->sc_wrbufcpy = malloc(sc->sc_wrbufsz, M_TEMP, M_WAITOK);
	memcpy(sc->sc_wrbufcpy, sc->sc_wrbuf, sc->sc_wrbufsz);
	sc->sc_writing = 1;
	return (0);
}

/*
 * Finish updating the current block/sector by writing the compound
 * set of changes to the flash.
 */
int
cfi_block_finish(struct cfi_softc *sc)
{
	int error;

	error = cfi_write_block(sc);
	free(sc->sc_wrbuf, M_TEMP);
	free(sc->sc_wrbufcpy, M_TEMP);
	sc->sc_wrbuf = NULL;
	sc->sc_wrbufsz = 0;
	sc->sc_wrofs = 0;
	sc->sc_writing = 0;
	return (error);
}

static int
cfi_devopen(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct cfi_softc *sc;

	sc = dev->si_drv1;
	/* We allow only 1 open. */
	if (!atomic_cmpset_acq_ptr((uintptr_t *)&sc->sc_opened,
	    (uintptr_t)NULL, (uintptr_t)td->td_proc))
		return (EBUSY);
	return (0);
}

static int
cfi_devclose(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct cfi_softc *sc;
	int error;

	sc = dev->si_drv1;
	/* Sanity. Not really necessary. */
	if (sc->sc_opened != td->td_proc)
		return (ENXIO);

	error = (sc->sc_writing) ? cfi_block_finish(sc) : 0;
	sc->sc_opened = NULL;
	return (error);
}

static int
cfi_devread(struct cdev *dev, struct uio *uio, int ioflag)
{
	union {
		uint8_t		x8[4];
		uint16_t	x16[2];
		uint32_t	x32[1];
	} buf;
	struct cfi_softc *sc;
	u_int ofs;
	uint32_t val;
	int error;

	sc = dev->si_drv1;

	error = (sc->sc_writing) ? cfi_block_finish(sc) : 0;
	if (!error)
		error = (uio->uio_offset > sc->sc_size) ? EIO : 0;

	while (error == 0 && uio->uio_resid > 0 &&
	    uio->uio_offset < sc->sc_size) {
		ofs = uio->uio_offset;
		val = cfi_read_raw(sc, ofs);
		switch (sc->sc_width) {
		case 1:
			buf.x8[0] = val;
			break;
		case 2:
			buf.x16[0] = val;
			break;
		case 4:
			buf.x32[0] = val;
			break;
		}
		ofs &= sc->sc_width - 1;
		error = uiomove(buf.x8 + ofs,
		    MIN(uio->uio_resid, sc->sc_width - ofs), uio);
	}
	return (error);
}

static int
cfi_devwrite(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct cfi_softc *sc;
	u_int ofs, top;
	int error;

	sc = dev->si_drv1;

	error = (uio->uio_offset > sc->sc_size) ? EIO : 0;
	while (error == 0 && uio->uio_resid > 0 &&
	    uio->uio_offset < sc->sc_size) {
		ofs = uio->uio_offset;

		/*
		 * Finish the current block if we're about to write
		 * to a different block.
		 */
		if (sc->sc_writing) {
			top = sc->sc_wrofs + sc->sc_wrbufsz;
			if (ofs < sc->sc_wrofs || ofs >= top)
				cfi_block_finish(sc);
		}

		/* Start writing to a (new) block if applicable. */
		if (!sc->sc_writing) {
			error = cfi_block_start(sc, uio->uio_offset);
			if (error)
				break;
		}

		top = sc->sc_wrofs + sc->sc_wrbufsz;
		error = uiomove(sc->sc_wrbuf + ofs - sc->sc_wrofs,
		    MIN(top - ofs, uio->uio_resid), uio);
	}
	return (error);
}

static int
cfi_devioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct cfi_softc *sc;
	struct cfiocqry *rq;
	int error;
	u_char val;

	sc = dev->si_drv1;
	error = 0;

	switch (cmd) {
	case CFIOCQRY:
		if (sc->sc_writing) {
			error = cfi_block_finish(sc);
			if (error)
				break;
		}
		rq = (struct cfiocqry *)data;
		if (rq->offset >= sc->sc_size / sc->sc_width)
			return (ESPIPE);
		if (rq->offset + rq->count > sc->sc_size / sc->sc_width)
			return (ENOSPC);

		while (!error && rq->count--) {
			val = cfi_read_qry(sc, rq->offset++);
			error = copyout(&val, rq->buffer++, 1);
		}
		break;
#ifdef CFI_SUPPORT_STRATAFLASH
	case CFIOCGFACTORYPR:
		error = cfi_intel_get_factory_pr(sc, (uint64_t *)data);
		break;
	case CFIOCGOEMPR:
		error = cfi_intel_get_oem_pr(sc, (uint64_t *)data);
		break;
	case CFIOCSOEMPR:
		error = cfi_intel_set_oem_pr(sc, *(uint64_t *)data);
		break;
	case CFIOCGPLR:
		error = cfi_intel_get_plr(sc, (uint32_t *)data);
		break;
	case CFIOCSPLR:
		error = cfi_intel_set_plr(sc);
		break;
#endif /* CFI_SUPPORT_STRATAFLASH */
	default:
		error = ENOIOCTL;
		break;
	}
	return (error);
}
