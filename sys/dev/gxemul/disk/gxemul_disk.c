/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2012 Juli Mallett <jmallett@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <geom/geom.h>

#include <machine/cpuregs.h>

#include <dev/gxemul/disk/gxemul_diskreg.h>

struct gxemul_disk_softc {
	device_t sc_dev;
	uint64_t sc_size;
	struct g_geom *sc_geom;
	struct g_provider *sc_provider;
};

static struct mtx gxemul_disk_controller_mutex;

static g_start_t	gxemul_disk_start;
static g_access_t	gxemul_disk_access;

struct g_class g_gxemul_disk_class = {
	.name = "GXemul",
	.version = G_VERSION,
	.start = gxemul_disk_start,
	.access = gxemul_disk_access,
};

DECLARE_GEOM_CLASS(g_gxemul_disk_class, g_gxemul_disk);

static void	gxemul_disk_identify(driver_t *, device_t);
static int	gxemul_disk_probe(device_t);
static int	gxemul_disk_attach(device_t);
static void	gxemul_disk_attach_geom(void *, int);

static int	gxemul_disk_read(unsigned, void *, off_t);
static int	gxemul_disk_size(unsigned, uint64_t *);
static int	gxemul_disk_write(unsigned, const void *, off_t);

static void
gxemul_disk_start(struct bio *bp)
{
	struct gxemul_disk_softc *sc;
	unsigned diskid;
	off_t offset;
	uint8_t *buf;
	int error;

	sc = bp->bio_to->geom->softc;
	diskid = device_get_unit(sc->sc_dev);

	if ((bp->bio_length % GXEMUL_DISK_DEV_BLOCKSIZE) != 0) {
		g_io_deliver(bp, EINVAL);
		return;
	}

	buf = bp->bio_data;
	offset = bp->bio_offset;
	bp->bio_resid = bp->bio_length;
	while (bp->bio_resid != 0) {
		switch (bp->bio_cmd) {
		case BIO_READ:
			mtx_lock(&gxemul_disk_controller_mutex);
			error = gxemul_disk_read(diskid, buf, offset);
			mtx_unlock(&gxemul_disk_controller_mutex);
			break;
		case BIO_WRITE:
			mtx_lock(&gxemul_disk_controller_mutex);
			error = gxemul_disk_write(diskid, buf, offset);
			mtx_unlock(&gxemul_disk_controller_mutex);
			break;
		default:
			g_io_deliver(bp, EOPNOTSUPP);
			return;
		}
		if (error != 0) {
			g_io_deliver(bp, error);
			return;
		}

		buf += GXEMUL_DISK_DEV_BLOCKSIZE;
		offset += GXEMUL_DISK_DEV_BLOCKSIZE;
		bp->bio_completed += GXEMUL_DISK_DEV_BLOCKSIZE;
		bp->bio_resid -= GXEMUL_DISK_DEV_BLOCKSIZE;
	}

	g_io_deliver(bp, 0);
}

static int
gxemul_disk_access(struct g_provider *pp, int r, int w, int e)
{
	return (0);
}

static void
gxemul_disk_identify(driver_t *drv, device_t parent)
{
	unsigned diskid;

	mtx_init(&gxemul_disk_controller_mutex, "GXemul disk controller", NULL, MTX_DEF);

	mtx_lock(&gxemul_disk_controller_mutex);
	for (diskid = 0; diskid < 0x100; diskid++) {
		/*
		 * If we can read at offset 0, this disk id must be
		 * present enough.  If we get an error, stop looking.
		 * Disks in GXemul are allocated linearly from 0.
		 */
		if (gxemul_disk_read(diskid, NULL, 0) != 0)
			break;
		BUS_ADD_CHILD(parent, 0, "gxemul_disk", diskid);
	}
	mtx_unlock(&gxemul_disk_controller_mutex);
}

static int
gxemul_disk_probe(device_t dev)
{
	device_set_desc(dev, "GXemul test disk");

	return (BUS_PROBE_NOWILDCARD);
}

static void
gxemul_disk_attach_geom(void *arg, int flag)
{
	struct gxemul_disk_softc *sc;

	sc = arg;

	sc->sc_geom = g_new_geomf(&g_gxemul_disk_class, "%s", device_get_nameunit(sc->sc_dev));
	sc->sc_geom->softc = sc;

	sc->sc_provider = g_new_providerf(sc->sc_geom, "%s", sc->sc_geom->name);
	sc->sc_provider->sectorsize = GXEMUL_DISK_DEV_BLOCKSIZE;
	sc->sc_provider->mediasize = sc->sc_size;
	g_error_provider(sc->sc_provider, 0);
}

static int
gxemul_disk_attach(device_t dev)
{
	struct gxemul_disk_softc *sc;
	unsigned diskid;
	int error;

	diskid = device_get_unit(dev);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_geom = NULL;
	sc->sc_provider = NULL;

	mtx_lock(&gxemul_disk_controller_mutex);
	error = gxemul_disk_size(diskid, &sc->sc_size);
	if (error != 0) {
		mtx_unlock(&gxemul_disk_controller_mutex);
		return (error);
	}
	mtx_unlock(&gxemul_disk_controller_mutex);

	g_post_event(gxemul_disk_attach_geom, sc, M_WAITOK, NULL);

	return (0);
}

static int
gxemul_disk_read(unsigned diskid, void *buf, off_t off)
{
	const volatile void *src;

	mtx_assert(&gxemul_disk_controller_mutex, MA_OWNED);

	if (off < 0 || off % GXEMUL_DISK_DEV_BLOCKSIZE != 0)
		return (EINVAL);

#ifdef _LP64
	GXEMUL_DISK_DEV_WRITE(GXEMUL_DISK_DEV_OFFSET, (uint64_t)off);
#else
	GXEMUL_DISK_DEV_WRITE(GXEMUL_DISK_DEV_OFFSET_LO,
	    (uint32_t)(off & 0xffffffff));
	GXEMUL_DISK_DEV_WRITE(GXEMUL_DISK_DEV_OFFSET_HI,
	    (uint32_t)((off >> 32) & 0xffffffff));
#endif
	GXEMUL_DISK_DEV_WRITE(GXEMUL_DISK_DEV_DISKID, diskid);
	GXEMUL_DISK_DEV_WRITE(GXEMUL_DISK_DEV_START, GXEMUL_DISK_DEV_START_READ);
	switch (GXEMUL_DISK_DEV_READ(GXEMUL_DISK_DEV_STATUS)) {
	case GXEMUL_DISK_DEV_STATUS_FAILURE:
		return (EIO);
	default:
		break;
	}

	if (buf != NULL) {
		src = GXEMUL_DISK_DEV_FUNCTION(GXEMUL_DISK_DEV_BLOCK);
		memcpy(buf, (const void *)(uintptr_t)src,
		       GXEMUL_DISK_DEV_BLOCKSIZE);
	}

	return (0);
}

static int
gxemul_disk_size(unsigned diskid, uint64_t *sizep)
{
	uint64_t offset, ogood;
	uint64_t m, s;
	int error;

	m = 1;
	s = 3;
	ogood = 0;

	for (;;) {
		offset = (ogood * s) + (m * GXEMUL_DISK_DEV_BLOCKSIZE);

		error = gxemul_disk_read(diskid, NULL, offset);
		if (error != 0) {
			if (m == 1 && s == 1) {
				*sizep = ogood + GXEMUL_DISK_DEV_BLOCKSIZE;
				return (0);
			}
			if (m > 1)
				m /= 2;
			if (s > 1)
				s--;
			continue;
		}
		if (ogood == offset) {
			m = 1;
			continue;
		}
		ogood = offset;
		m++;
	}

	return (EDOOFUS);
}

static int
gxemul_disk_write(unsigned diskid, const void *buf, off_t off)
{
	volatile void *dst;

	mtx_assert(&gxemul_disk_controller_mutex, MA_OWNED);

	if (off < 0 || off % GXEMUL_DISK_DEV_BLOCKSIZE != 0)
		return (EINVAL);

#ifdef _LP64
	GXEMUL_DISK_DEV_WRITE(GXEMUL_DISK_DEV_OFFSET, (uint64_t)off);
#else
	GXEMUL_DISK_DEV_WRITE(GXEMUL_DISK_DEV_OFFSET_LO,
	    (uint32_t)(off & 0xffffffff));
	GXEMUL_DISK_DEV_WRITE(GXEMUL_DISK_DEV_OFFSET_HI,
	    (uint32_t)((off >> 32) & 0xffffffff));
#endif

	GXEMUL_DISK_DEV_WRITE(GXEMUL_DISK_DEV_DISKID, diskid);

	dst = GXEMUL_DISK_DEV_FUNCTION(GXEMUL_DISK_DEV_BLOCK);
	memcpy((void *)(uintptr_t)dst, buf, GXEMUL_DISK_DEV_BLOCKSIZE);

	GXEMUL_DISK_DEV_WRITE(GXEMUL_DISK_DEV_START, GXEMUL_DISK_DEV_START_WRITE);
	switch (GXEMUL_DISK_DEV_READ(GXEMUL_DISK_DEV_STATUS)) {
	case GXEMUL_DISK_DEV_STATUS_FAILURE:
		return (EIO);
	default:
		break;
	}

	return (0);
}

static device_method_t gxemul_disk_methods[] = {
	DEVMETHOD(device_probe,		gxemul_disk_probe),
	DEVMETHOD(device_identify,      gxemul_disk_identify),
	DEVMETHOD(device_attach,	gxemul_disk_attach),

	{ 0, 0 }
};

static driver_t gxemul_disk_driver = {
	"gxemul_disk", 
	gxemul_disk_methods, 
	sizeof (struct gxemul_disk_softc)
};

static devclass_t gxemul_disk_devclass;
DRIVER_MODULE(gxemul_disk, nexus, gxemul_disk_driver, gxemul_disk_devclass, 0, 0);
