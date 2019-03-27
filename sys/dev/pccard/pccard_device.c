/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005, M. Warner Losh
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pccard/pccardreg.h>
#include <dev/pccard/pccardvar.h>
#include <dev/pccard/pccardvarp.h>
#include <dev/pccard/pccard_cis.h>

static	d_open_t	pccard_open;
static	d_close_t	pccard_close;
static	d_read_t	pccard_read;
static	d_ioctl_t	pccard_ioctl;

static struct cdevsw pccard_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	pccard_open,
	.d_close =	pccard_close,
	.d_read =	pccard_read,
	.d_ioctl =	pccard_ioctl,
	.d_name =	"pccard"
};

int
pccard_device_create(struct pccard_softc *sc)
{
	uint32_t minor;

	minor = device_get_unit(sc->dev) << 16;
	sc->cisdev = make_dev(&pccard_cdevsw, minor, 0, 0, 0666,
	    "pccard%u.cis", device_get_unit(sc->dev));
	sc->cisdev->si_drv1 = sc;
	return (0);
}

int
pccard_device_destroy(struct pccard_softc *sc)
{
	if (sc->cisdev)
		destroy_dev(sc->cisdev);
	return (0);
}

static int
pccard_build_cis(const struct pccard_tuple *tuple, void *argp)
{
	struct cis_buffer *cis;
	int i;
	uint8_t ch;

	cis = (struct cis_buffer *)argp;
	/*
	 * CISTPL_END is a special case, it has no length field.
	 */
	if (tuple->code == CISTPL_END) {
		if (cis->len + 1 > sizeof(cis->buffer))
			return (ENOSPC);
		cis->buffer[cis->len++] = tuple->code;
		return (0);
	}
	if (cis->len + 2 + tuple->length > sizeof(cis->buffer))
		return (ENOSPC);
	cis->buffer[cis->len++] = tuple->code;
	cis->buffer[cis->len++] = tuple->length;
	for (i = 0; i < tuple->length; i++) {
		ch = pccard_tuple_read_1(tuple, i);
		cis->buffer[cis->len++] = ch;
	}
	return (0);
}

static	int
pccard_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	device_t parent, child;
	device_t *kids;
	int cnt, err;
	struct pccard_softc *sc;

	sc = dev->si_drv1;
	if (sc->cis_open)
		return (EBUSY);
	parent = sc->dev;
	err = device_get_children(parent, &kids, &cnt);
	if (err)
		return err;
	if (cnt == 0) {
		free(kids, M_TEMP);
		sc->cis_open++;
		sc->cis = NULL;
		return (0);
	}
	child = kids[0];
	free(kids, M_TEMP);
	sc->cis = malloc(sizeof(*sc->cis), M_TEMP, M_ZERO | M_WAITOK);
	err = pccard_scan_cis(parent, child, pccard_build_cis, sc->cis);
	if (err) {
		free(sc->cis, M_TEMP);
		sc->cis = NULL;
		return (err);
	}
	sc->cis_open++;
	return (0);
}

static	int
pccard_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
	struct pccard_softc *sc;

	sc = dev->si_drv1;
	free(sc->cis, M_TEMP);
	sc->cis = NULL;
	sc->cis_open = 0;
	return (0);
}

static	int
pccard_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	return (ENOTTY);
}

static	int
pccard_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct pccard_softc *sc;

	sc = dev->si_drv1;
	/* EOF */
	if (sc->cis == NULL || uio->uio_offset > sc->cis->len)
		return (0);
	return (uiomove(sc->cis->buffer + uio->uio_offset,
	  MIN(uio->uio_resid, sc->cis->len - uio->uio_offset), uio));
}
