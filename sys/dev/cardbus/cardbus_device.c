/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2008, M. Warner Losh
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

#include <sys/pciio.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pci_private.h>

#include <dev/cardbus/cardbusreg.h>
#include <dev/cardbus/cardbusvar.h>
#include <dev/cardbus/cardbus_cis.h>
#include <dev/pccard/pccard_cis.h>

static	d_open_t	cardbus_open;
static	d_close_t	cardbus_close;
static	d_read_t	cardbus_read;
static	d_ioctl_t	cardbus_ioctl;

static struct cdevsw cardbus_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	cardbus_open,
	.d_close =	cardbus_close,
	.d_read =	cardbus_read,
	.d_ioctl =	cardbus_ioctl,
	.d_name =	"cardbus"
};

static int
cardbus_build_cis(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *argp)
{
	struct cis_buffer *cis;
	int i;

	cis = (struct cis_buffer *)argp;
	/*
	 * CISTPL_END is a special case, it has no length field.
	 */
	if (id == CISTPL_END) {
		if (cis->len + 1 > sizeof(cis->buffer)) {
			cis->len = 0;
			return (ENOSPC);
		}
		cis->buffer[cis->len++] = id;
		return (0);
	}
	if (cis->len + 2 + len > sizeof(cis->buffer)) {
		cis->len = 0;
		return (ENOSPC);
	}
	cis->buffer[cis->len++] = id;
	cis->buffer[cis->len++] = len;
	for (i = 0; i < len; i++)
		cis->buffer[cis->len++] = tupledata[i];
	return (0);
}

static int
cardbus_device_buffer_cis(device_t parent, device_t child,
    struct cis_buffer *cbp)
{
	struct tuple_callbacks cb[] = {
		{CISTPL_GENERIC, "GENERIC", cardbus_build_cis}
	};

	return (cardbus_parse_cis(parent, child, cb, cbp));
}

int
cardbus_device_create(struct cardbus_softc *sc, struct cardbus_devinfo *devi,
    device_t parent, device_t child)
{
	uint32_t minor;
	int unit;

	cardbus_device_buffer_cis(parent, child, &devi->sc_cis);
	minor = (device_get_unit(sc->sc_dev) << 8) + devi->pci.cfg.func;
	unit = device_get_unit(sc->sc_dev);
	devi->sc_cisdev = make_dev(&cardbus_cdevsw, minor, 0, 0, 0666,
	    "cardbus%d.%d.cis", unit, devi->pci.cfg.func);
	if (devi->pci.cfg.func == 0)
		make_dev_alias(devi->sc_cisdev, "cardbus%d.cis", unit);
	devi->sc_cisdev->si_drv1 = devi;
	return (0);
}

int
cardbus_device_destroy(struct cardbus_devinfo *devi)
{
	if (devi->sc_cisdev)
		destroy_dev(devi->sc_cisdev);
	return (0);
}

static	int
cardbus_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{

	return (0);
}

static	int
cardbus_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{

	return (0);
}

static	int
cardbus_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	return (ENOTTY);
}

static	int
cardbus_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct cardbus_devinfo *devi;

	devi = dev->si_drv1;
	/* EOF */
	if (uio->uio_offset >= devi->sc_cis.len)
		return (0);
	return (uiomove(devi->sc_cis.buffer + uio->uio_offset,
	  MIN(uio->uio_resid, devi->sc_cis.len - uio->uio_offset), uio));
}
