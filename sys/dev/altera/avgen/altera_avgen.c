/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2013, 2016 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <geom/geom_disk.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <vm/vm.h>

#include <dev/altera/avgen/altera_avgen.h>

/*
 * Generic device driver for allowing read(), write(), and mmap() on
 * memory-mapped, Avalon-attached devices.  There is no actual dependence on
 * Avalon, so conceivably this should just be soc_dev or similar, since many
 * system-on-chip bus environments would work fine with the same code.
 */

devclass_t altera_avgen_devclass;

static d_mmap_t altera_avgen_mmap;
static d_read_t altera_avgen_read;
static d_write_t altera_avgen_write;

#define	ALTERA_AVGEN_DEVNAME		"altera_avgen"
#define	ALTERA_AVGEN_DEVNAME_FMT	(ALTERA_AVGEN_DEVNAME "%d")

static struct cdevsw avg_cdevsw = {
	.d_version =	D_VERSION,
	.d_mmap =	altera_avgen_mmap,
	.d_read =	altera_avgen_read,
	.d_write =	altera_avgen_write,
	.d_name =	ALTERA_AVGEN_DEVNAME,
};

#define	ALTERA_AVGEN_SECTORSIZE	512	/* Not configurable at this time. */

static int
altera_avgen_read(struct cdev *dev, struct uio *uio, int flag)
{
	struct altera_avgen_softc *sc;
	u_long offset, size;
#ifdef NOTYET
	uint64_t v8;
#endif
	uint32_t v4;
	uint16_t v2;
	uint8_t v1;
	u_int width;
	int error;

	sc = dev->si_drv1;
	if ((sc->avg_flags & ALTERA_AVALON_FLAG_READ) == 0)
		return (EACCES);
	width = sc->avg_width;
	if (uio->uio_offset < 0 || uio->uio_offset % width != 0 ||
	    uio->uio_resid % width != 0)
		return (ENODEV);
	size = rman_get_size(sc->avg_res);
	if ((uio->uio_offset + uio->uio_resid < 0) ||
	    (uio->uio_offset + uio->uio_resid > size))
		return (ENODEV);
	while (uio->uio_resid > 0) {
		offset = uio->uio_offset;
		if (offset + width > size)
			return (ENODEV);
		switch (width) {
		case 1:
			v1 = bus_read_1(sc->avg_res, offset);
			error = uiomove(&v1, sizeof(v1), uio);
			break;
			
		case 2:
			v2 = bus_read_2(sc->avg_res, offset);
			error = uiomove(&v2, sizeof(v2), uio);
			break;
			
		case 4:
			v4 = bus_read_4(sc->avg_res, offset);
			error = uiomove(&v4, sizeof(v4), uio);
			break;
			
#ifdef NOTYET
		case 8:
			v8 = bus_read_8(sc->avg_res, offset);
			error = uiomove(&v8, sizeof(v8), uio);
			break;
			
#endif

		default:
			panic("%s: unexpected widthment %u", __func__, width);
		}
		if (error)
			return (error);
	}
	return (0);
}

static int
altera_avgen_write(struct cdev *dev, struct uio *uio, int flag)
{
	struct altera_avgen_softc *sc;
	u_long offset, size;
#ifdef NOTYET
	uint64_t v8;
#endif
	uint32_t v4;
	uint16_t v2;
	uint8_t v1;
	u_int width;
	int error;

	sc = dev->si_drv1;
	if ((sc->avg_flags & ALTERA_AVALON_FLAG_WRITE) == 0)
		return (EACCES);
	width = sc->avg_width;
	if (uio->uio_offset < 0 || uio->uio_offset % width != 0 ||
	    uio->uio_resid % width != 0)
		return (ENODEV);
	size = rman_get_size(sc->avg_res);
	while (uio->uio_resid > 0) {
		offset = uio->uio_offset;
		if (offset + width > size)
			return (ENODEV);
		switch (width) {
		case 1:
			error = uiomove(&v1, sizeof(v1), uio);
			if (error)
				return (error);
			bus_write_1(sc->avg_res, offset, v1);
			break;

		case 2:
			error = uiomove(&v2, sizeof(v2), uio);
			if (error)
				return (error);
			bus_write_2(sc->avg_res, offset, v2);
			break;

		case 4:
			error = uiomove(&v4, sizeof(v4), uio);
			if (error)
				return (error);
			bus_write_4(sc->avg_res, offset, v4);
			break;

#ifdef NOTYET
		case 8:
			error = uiomove(&v8, sizeof(v8), uio);
			if (error)
				return (error);
			bus_write_8(sc->avg_res, offset, v8);
			break;
#endif

		default:
			panic("%s: unexpected width %u", __func__, width);
		}
	}
	return (0);
}

static int
altera_avgen_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
	struct altera_avgen_softc *sc;

	sc = dev->si_drv1;
	if (nprot & VM_PROT_READ) {
		if ((sc->avg_flags & ALTERA_AVALON_FLAG_MMAP_READ) == 0)
			return (EACCES);
	}
	if (nprot & VM_PROT_WRITE) {
		if ((sc->avg_flags & ALTERA_AVALON_FLAG_MMAP_WRITE) == 0)
			return (EACCES);
	}
	if (nprot & VM_PROT_EXECUTE) {
		if ((sc->avg_flags & ALTERA_AVALON_FLAG_MMAP_EXEC) == 0)
			return (EACCES);
	}
	if (trunc_page(offset) == offset &&
	    offset + PAGE_SIZE > offset &&
	    rman_get_size(sc->avg_res) >= offset + PAGE_SIZE) {
		*paddr = rman_get_start(sc->avg_res) + offset;
		*memattr = VM_MEMATTR_UNCACHEABLE;
	} else
		return (ENODEV);
	return (0);
}

/*
 * NB: We serialise block reads and writes in case the OS is generating
 * concurrent I/O against the same block, in which case we want one I/O (or
 * another) to win.  This is not sufficient to provide atomicity for the
 * sector in the presence of a fail stop -- however, we're just writing this
 * to non-persistent DRAM .. right?
 */
static void
altera_avgen_disk_strategy(struct bio *bp)
{
	struct altera_avgen_softc *sc;
	void *data;
	long bcount;
	daddr_t pblkno;

	sc = bp->bio_disk->d_drv1;
	data = bp->bio_data;
	bcount = bp->bio_bcount;
	pblkno = bp->bio_pblkno;

	/*
	 * Serialize block reads / writes.
	 */
	mtx_lock(&sc->avg_disk_mtx);
	switch (bp->bio_cmd) {
	case BIO_READ:
		if (!(sc->avg_flags & ALTERA_AVALON_FLAG_GEOM_READ)) {
			biofinish(bp, NULL, EIO);
			break;
		}
		switch (sc->avg_width) {
		case 1:
			bus_read_region_1(sc->avg_res,
			    bp->bio_pblkno * ALTERA_AVGEN_SECTORSIZE,
			    (uint8_t *)data, bcount);
			break;

		case 2:
			bus_read_region_2(sc->avg_res,
			    bp->bio_pblkno * ALTERA_AVGEN_SECTORSIZE,
			    (uint16_t *)data, bcount / 2);
			break;

		case 4:
			bus_read_region_4(sc->avg_res,
			    bp->bio_pblkno * ALTERA_AVGEN_SECTORSIZE,
			    (uint32_t *)data, bcount / 4);
			break;

		default:
			panic("%s: unexpected width %u", __func__,
			    sc->avg_width);
		}
		break;

	case BIO_WRITE:
		if (!(sc->avg_flags & ALTERA_AVALON_FLAG_GEOM_WRITE)) {
			biofinish(bp, NULL, EROFS);
			break;
		}
		switch (sc->avg_width) {
		case 1:
			bus_write_region_1(sc->avg_res,
			    bp->bio_pblkno * ALTERA_AVGEN_SECTORSIZE,
			    (uint8_t *)data, bcount);
			break;

		case 2:
			bus_write_region_2(sc->avg_res,
			    bp->bio_pblkno * ALTERA_AVGEN_SECTORSIZE,
			    (uint16_t *)data, bcount / 2);
			break;

		case 4:
			bus_write_region_4(sc->avg_res,
			    bp->bio_pblkno * ALTERA_AVGEN_SECTORSIZE,
			    (uint32_t *)data, bcount / 4);
			break;

		default:
			panic("%s: unexpected width %u", __func__,
			    sc->avg_width);
		}
		break;

	default:
		panic("%s: unsupported I/O operation %d", __func__,
		    bp->bio_cmd);
	}
	mtx_unlock(&sc->avg_disk_mtx);
	biofinish(bp, NULL, 0);
}

static int
altera_avgen_process_options(struct altera_avgen_softc *sc,
    const char *str_fileio, const char *str_geomio, const char *str_mmapio,
    const char *str_devname, int devunit)
{
	const char *cp;
	device_t dev = sc->avg_dev;

	/*
	 * Check for valid combinations of options.
	 */
	if (str_fileio == NULL && str_geomio == NULL && str_mmapio == NULL) {
		device_printf(dev,
		    "at least one of %s, %s, or %s must be specified\n",
		    ALTERA_AVALON_STR_FILEIO, ALTERA_AVALON_STR_GEOMIO,
		    ALTERA_AVALON_STR_MMAPIO);
		return (ENXIO);
	}

	/*
	 * Validity check: a device can either be a GEOM device (in which case
	 * we use GEOM to register the device node), or a special device --
	 * but not both as that causes a collision in /dev.
	 */
	if (str_geomio != NULL && (str_fileio != NULL || str_mmapio != NULL)) {
		device_printf(dev,
		    "at most one of %s and (%s or %s) may be specified\n",
		    ALTERA_AVALON_STR_GEOMIO, ALTERA_AVALON_STR_FILEIO,
		    ALTERA_AVALON_STR_MMAPIO);
		return (ENXIO);
	}

	/*
	 * Ensure that a unit is specified if a name is also specified.
	 */
	if (str_devname == NULL && devunit != -1) {
		device_printf(dev, "%s requires %s be specified\n",
		    ALTERA_AVALON_STR_DEVUNIT, ALTERA_AVALON_STR_DEVNAME);
		return (ENXIO);
	}

	/*
	 * Extract, digest, and save values.
	 */
	switch (sc->avg_width) {
	case 1:
	case 2:
	case 4:
#ifdef NOTYET
	case 8:
#endif
		break;

	default:
		device_printf(dev, "%s unsupported value %u\n",
		    ALTERA_AVALON_STR_WIDTH, sc->avg_width);
		return (ENXIO);
	}
	sc->avg_flags = 0;
	if (str_fileio != NULL) {
		for (cp = str_fileio; *cp != '\0'; cp++) {
			switch (*cp) {
			case ALTERA_AVALON_CHAR_READ:
				sc->avg_flags |= ALTERA_AVALON_FLAG_READ;
				break;

			case ALTERA_AVALON_CHAR_WRITE:
				sc->avg_flags |= ALTERA_AVALON_FLAG_WRITE;
				break;

			default:
				device_printf(dev,
				    "invalid %s character %c\n", 
				    ALTERA_AVALON_STR_FILEIO, *cp);
				return (ENXIO);
			}
		}
	}
	if (str_geomio != NULL) {
		for (cp = str_geomio; *cp != '\0'; cp++){
			switch (*cp) {
			case ALTERA_AVALON_CHAR_READ:
				sc->avg_flags |= ALTERA_AVALON_FLAG_GEOM_READ;
				break;

			case ALTERA_AVALON_CHAR_WRITE:
				sc->avg_flags |= ALTERA_AVALON_FLAG_GEOM_WRITE;
				break;

			default:
				device_printf(dev,
				    "invalid %s character %c\n",
				    ALTERA_AVALON_STR_GEOMIO, *cp);
				return (ENXIO);
			}
		}
	}
	if (str_mmapio != NULL) {
		for (cp = str_mmapio; *cp != '\0'; cp++) {
			switch (*cp) {
			case ALTERA_AVALON_CHAR_READ:
				sc->avg_flags |= ALTERA_AVALON_FLAG_MMAP_READ;
				break;

			case ALTERA_AVALON_CHAR_WRITE:
				sc->avg_flags |=
				    ALTERA_AVALON_FLAG_MMAP_WRITE;
				break;

			case ALTERA_AVALON_CHAR_EXEC:
				sc->avg_flags |= ALTERA_AVALON_FLAG_MMAP_EXEC;
				break;

			default:
				device_printf(dev,
				    "invalid %s character %c\n",
				    ALTERA_AVALON_STR_MMAPIO, *cp);
				return (ENXIO);
			}
		}
	}
	return (0);
}

int
altera_avgen_attach(struct altera_avgen_softc *sc, const char *str_fileio,
    const char *str_geomio, const char *str_mmapio, const char *str_devname,
    int devunit)
{
	device_t dev = sc->avg_dev;
	int error;

	error = altera_avgen_process_options(sc, str_fileio, str_geomio,
	    str_mmapio, str_devname, devunit);
	if (error)
		return (error);

	if (rman_get_size(sc->avg_res) >= PAGE_SIZE || str_mmapio != NULL) {
		if (rman_get_size(sc->avg_res) % PAGE_SIZE != 0) {
			device_printf(dev,
			    "memory region not even multiple of page size\n");
			return (ENXIO);
		}
		if (rman_get_start(sc->avg_res) % PAGE_SIZE != 0) {
			device_printf(dev, "memory region not page-aligned\n");
			return (ENXIO);
		}
	}

	/*
	 * If a GEOM permission is requested, then create the device via GEOM.
	 * Otherwise, create a special device.  We checked during options
	 * processing that both weren't requested a once.
	 */
	if (str_devname != NULL) {
		sc->avg_name = strdup(str_devname, M_TEMP);
		devunit = sc->avg_unit;
	} else
		sc->avg_name = strdup(ALTERA_AVGEN_DEVNAME, M_TEMP);
	if (sc->avg_flags & (ALTERA_AVALON_FLAG_GEOM_READ |
	    ALTERA_AVALON_FLAG_GEOM_WRITE)) {
		mtx_init(&sc->avg_disk_mtx, "altera_avgen_disk", NULL,
		    MTX_DEF);
		sc->avg_disk = disk_alloc();
		sc->avg_disk->d_drv1 = sc;
		sc->avg_disk->d_strategy = altera_avgen_disk_strategy;
		if (devunit == -1)
			devunit = 0;
		sc->avg_disk->d_name = sc->avg_name;
		sc->avg_disk->d_unit = devunit;

		/*
		 * NB: As avg_res is a multiple of PAGE_SIZE, it is also a
		 * multiple of ALTERA_AVGEN_SECTORSIZE.
		 */
		sc->avg_disk->d_sectorsize = ALTERA_AVGEN_SECTORSIZE;
		sc->avg_disk->d_mediasize = rman_get_size(sc->avg_res);
		sc->avg_disk->d_maxsize = ALTERA_AVGEN_SECTORSIZE;
		disk_create(sc->avg_disk, DISK_VERSION);
	} else {
		/* Device node allocation. */
		if (str_devname == NULL) {
			str_devname = ALTERA_AVGEN_DEVNAME_FMT;
			devunit = sc->avg_unit;
		}
		if (devunit != -1)
			sc->avg_cdev = make_dev(&avg_cdevsw, sc->avg_unit,
			    UID_ROOT, GID_WHEEL, S_IRUSR | S_IWUSR, "%s%d",
			    str_devname, devunit);
		else
			sc->avg_cdev = make_dev(&avg_cdevsw, sc->avg_unit,
			    UID_ROOT, GID_WHEEL, S_IRUSR | S_IWUSR,
			    "%s", str_devname);
		if (sc->avg_cdev == NULL) {
			device_printf(sc->avg_dev, "%s: make_dev failed\n",
			    __func__);
			return (ENXIO);
		}

		/* XXXRW: Slight race between make_dev(9) and here. */
		sc->avg_cdev->si_drv1 = sc;
	}
	return (0);
}

void
altera_avgen_detach(struct altera_avgen_softc *sc)
{

	KASSERT((sc->avg_disk != NULL) || (sc->avg_cdev != NULL),
	    ("%s: neither GEOM nor special device", __func__));

	if (sc->avg_disk != NULL) {
		disk_gone(sc->avg_disk);
		disk_destroy(sc->avg_disk);
		free(sc->avg_name, M_TEMP);
		mtx_destroy(&sc->avg_disk_mtx);
	} else {
		destroy_dev(sc->avg_cdev);
	}
}
