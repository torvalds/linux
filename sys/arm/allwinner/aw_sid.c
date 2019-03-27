/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 *
 * $FreeBSD$
 */

/*
 * Allwinner secure ID controller
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/allwinner/aw_sid.h>

#include "nvmem_if.h"

/* 
 * Starting at least from sun8iw6 (A83T) EFUSE starts at 0x200 
 * There is 3 registers in the low area to read/write protected EFUSE.
 */
#define	SID_PRCTL		0x40
#define	 SID_PRCTL_OFFSET_MASK	0xff
#define	 SID_PRCTL_OFFSET(n)	(((n) & SID_PRCTL_OFFSET_MASK) << 16)
#define	 SID_PRCTL_LOCK		(0xac << 8)
#define	 SID_PRCTL_READ		(0x01 << 1)
#define	 SID_PRCTL_WRITE	(0x01 << 0)
#define	SID_PRKEY		0x50
#define	SID_RDKEY		0x60

#define	EFUSE_OFFSET		0x200
#define	EFUSE_NAME_SIZE		32
#define	EFUSE_DESC_SIZE		64

struct aw_sid_efuse {
	char			name[EFUSE_NAME_SIZE];
	char			desc[EFUSE_DESC_SIZE];
	bus_size_t		base;
	bus_size_t		offset;
	uint32_t		size;
	enum aw_sid_fuse_id	id;
	bool			public;
};

static struct aw_sid_efuse a10_efuses[] = {
	{
		.name = "rootkey",
		.desc = "Root Key or ChipID",
		.offset = 0x0,
		.size = 16,
		.id = AW_SID_FUSE_ROOTKEY,
		.public = true,
	},
};

static struct aw_sid_efuse a64_efuses[] = {
	{
		.name = "rootkey",
		.desc = "Root Key or ChipID",
		.base = EFUSE_OFFSET,
		.offset = 0x00,
		.size = 16,
		.id = AW_SID_FUSE_ROOTKEY,
		.public = true,
	},
	{
		.name = "ths-calib",
		.desc = "Thermal Sensor Calibration Data",
		.base = EFUSE_OFFSET,
		.offset = 0x34,
		.size = 6,
		.id = AW_SID_FUSE_THSSENSOR,
		.public = true,
	},
};

static struct aw_sid_efuse a83t_efuses[] = {
	{
		.name = "rootkey",
		.desc = "Root Key or ChipID",
		.base = EFUSE_OFFSET,
		.offset = 0x00,
		.size = 16,
		.id = AW_SID_FUSE_ROOTKEY,
		.public = true,
	},
	{
		.name = "ths-calib",
		.desc = "Thermal Sensor Calibration Data",
		.base = EFUSE_OFFSET,
		.offset = 0x34,
		.size = 8,
		.id = AW_SID_FUSE_THSSENSOR,
		.public = true,
	},
};

static struct aw_sid_efuse h3_efuses[] = {
	{
		.name = "rootkey",
		.desc = "Root Key or ChipID",
		.base = EFUSE_OFFSET,
		.offset = 0x00,
		.size = 16,
		.id = AW_SID_FUSE_ROOTKEY,
		.public = true,
	},
	{
		.name = "ths-calib",
		.desc = "Thermal Sensor Calibration Data",
		.base = EFUSE_OFFSET,
		.offset = 0x34,
		.size = 2,
		.id = AW_SID_FUSE_THSSENSOR,
		.public = false,
	},
};

static struct aw_sid_efuse h5_efuses[] = {
	{
		.name = "rootkey",
		.desc = "Root Key or ChipID",
		.base = EFUSE_OFFSET,
		.offset = 0x00,
		.size = 16,
		.id = AW_SID_FUSE_ROOTKEY,
		.public = true,
	},
	{
		.name = "ths-calib",
		.desc = "Thermal Sensor Calibration Data",
		.base = EFUSE_OFFSET,
		.offset = 0x34,
		.size = 4,
		.id = AW_SID_FUSE_THSSENSOR,
		.public = true,
	},
};

struct aw_sid_conf {
	struct aw_sid_efuse	*efuses;
	size_t			nfuses;
};

static const struct aw_sid_conf a10_conf = {
	.efuses = a10_efuses,
	.nfuses = nitems(a10_efuses),
};

static const struct aw_sid_conf a20_conf = {
	.efuses = a10_efuses,
	.nfuses = nitems(a10_efuses),
};

static const struct aw_sid_conf a64_conf = {
	.efuses = a64_efuses,
	.nfuses = nitems(a64_efuses),
};

static const struct aw_sid_conf a83t_conf = {
	.efuses = a83t_efuses,
	.nfuses = nitems(a83t_efuses),
};

static const struct aw_sid_conf h3_conf = {
	.efuses = h3_efuses,
	.nfuses = nitems(h3_efuses),
};

static const struct aw_sid_conf h5_conf = {
	.efuses = h5_efuses,
	.nfuses = nitems(h5_efuses),
};

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-sid",		(uintptr_t)&a10_conf},
	{ "allwinner,sun7i-a20-sid",		(uintptr_t)&a20_conf},
	{ "allwinner,sun50i-a64-sid",		(uintptr_t)&a64_conf},
	{ "allwinner,sun8i-a83t-sid",		(uintptr_t)&a83t_conf},
	{ "allwinner,sun8i-h3-sid",		(uintptr_t)&h3_conf},
	{ "allwinner,sun50i-h5-sid",		(uintptr_t)&h5_conf},
	{ NULL,					0 }
};

struct aw_sid_softc {
	device_t		sid_dev;
	struct resource		*res;
	struct aw_sid_conf	*sid_conf;
	struct mtx		prctl_mtx;
};

static struct aw_sid_softc *aw_sid_sc;

static struct resource_spec aw_sid_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	RD1(sc, reg)		bus_read_1((sc)->res, (reg))
#define	RD4(sc, reg)		bus_read_4((sc)->res, (reg))
#define	WR4(sc, reg, val)	bus_write_4((sc)->res, (reg), (val))

static int aw_sid_sysctl(SYSCTL_HANDLER_ARGS);

static int
aw_sid_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner Secure ID Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_sid_attach(device_t dev)
{
	struct aw_sid_softc *sc;
	phandle_t node;
	int i;

	node = ofw_bus_get_node(dev);
	sc = device_get_softc(dev);
	sc->sid_dev = dev;

	if (bus_alloc_resources(dev, aw_sid_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	mtx_init(&sc->prctl_mtx, device_get_nameunit(dev), NULL, MTX_DEF);
	sc->sid_conf = (struct aw_sid_conf *)ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	aw_sid_sc = sc;

	/* Register ourself so device can resolve who we are */
	OF_device_register_xref(OF_xref_from_node(node), dev);

	for (i = 0; i < sc->sid_conf->nfuses ;i++) {\
		SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		    OID_AUTO, sc->sid_conf->efuses[i].name,
		    CTLTYPE_STRING | CTLFLAG_RD,
		    dev, sc->sid_conf->efuses[i].id, aw_sid_sysctl,
		    "A", sc->sid_conf->efuses[i].desc);
	}
	return (0);
}

int
aw_sid_get_fuse(enum aw_sid_fuse_id id, uint8_t *out, uint32_t *size)
{
	struct aw_sid_softc *sc;
	uint32_t val;
	int i, j;

	sc = aw_sid_sc;
	if (sc == NULL)
		return (ENXIO);

	for (i = 0; i < sc->sid_conf->nfuses; i++)
		if (id == sc->sid_conf->efuses[i].id)
			break;

	if (i == sc->sid_conf->nfuses)
		return (ENOENT);

	if (*size != sc->sid_conf->efuses[i].size) {
		*size = sc->sid_conf->efuses[i].size;
		return (ENOMEM);
	}

	if (out == NULL)
		return (ENOMEM);

	if (sc->sid_conf->efuses[i].public == false)
		mtx_lock(&sc->prctl_mtx);
	for (j = 0; j < sc->sid_conf->efuses[i].size; j += 4) {
		if (sc->sid_conf->efuses[i].public == false) {
			val = SID_PRCTL_OFFSET(sc->sid_conf->efuses[i].offset + j) |
				SID_PRCTL_LOCK |
				SID_PRCTL_READ;
			WR4(sc, SID_PRCTL, val);
			/* Read bit will be cleared once read has concluded */
			while (RD4(sc, SID_PRCTL) & SID_PRCTL_READ)
				continue;
			val = RD4(sc, SID_RDKEY);
		} else
			val = RD4(sc, sc->sid_conf->efuses[i].base +
			    sc->sid_conf->efuses[i].offset + j);
		out[j] = val & 0xFF;
		if (j + 1 < *size)
			out[j + 1] = (val & 0xFF00) >> 8;
		if (j + 2 < *size)
			out[j + 2] = (val & 0xFF0000) >> 16;
		if (j + 3 < *size)
			out[j + 3] = (val & 0xFF000000) >> 24;
	}
	if (sc->sid_conf->efuses[i].public == false)
		mtx_unlock(&sc->prctl_mtx);

	return (0);
}

static int
aw_sid_read(device_t dev, uint32_t offset, uint32_t size, uint8_t *buffer)
{
	struct aw_sid_softc *sc;
	enum aw_sid_fuse_id fuse_id = 0;
	int i;

	sc = device_get_softc(dev);

	for (i = 0; i < sc->sid_conf->nfuses; i++)
		if (offset == (sc->sid_conf->efuses[i].base +
		    sc->sid_conf->efuses[i].offset)) {
			fuse_id = sc->sid_conf->efuses[i].id;
			break;
		}

	if (fuse_id == 0)
		return (ENOENT);

	return (aw_sid_get_fuse(fuse_id, buffer, &size));
}

static int
aw_sid_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct aw_sid_softc *sc;
	device_t dev = arg1;
	enum aw_sid_fuse_id fuse = arg2;
	uint8_t data[32];
	char out[128];
	uint32_t size;
	int ret, i;

	sc = device_get_softc(dev);

	/* Get the size of the efuse data */
	size = 0;
	aw_sid_get_fuse(fuse, NULL, &size);
	/* We now have the real size */
	ret = aw_sid_get_fuse(fuse, data, &size);
	if (ret != 0) {
		device_printf(dev, "Cannot get fuse id %d: %d\n", fuse, ret);
		return (ENOENT);
	}

	for (i = 0; i < size; i++)
		snprintf(out + (i * 2), sizeof(out) - (i * 2),
		  "%.2x", data[i]);

	return sysctl_handle_string(oidp, out, sizeof(out), req);
}

static device_method_t aw_sid_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_sid_probe),
	DEVMETHOD(device_attach,	aw_sid_attach),

	/* NVMEM interface */
	DEVMETHOD(nvmem_read,		aw_sid_read),
	DEVMETHOD_END
};

static driver_t aw_sid_driver = {
	"aw_sid",
	aw_sid_methods,
	sizeof(struct aw_sid_softc),
};

static devclass_t aw_sid_devclass;

EARLY_DRIVER_MODULE(aw_sid, simplebus, aw_sid_driver, aw_sid_devclass, 0, 0,
    BUS_PASS_RESOURCE + BUS_PASS_ORDER_FIRST);
MODULE_VERSION(aw_sid, 1);
