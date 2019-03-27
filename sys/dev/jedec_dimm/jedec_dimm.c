/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Authors: Ravi Pokala (rpokala@freebsd.org), Andriy Gapon (avg@FreeBSD.org)
 *
 * Copyright (c) 2016 Andriy Gapon <avg@FreeBSD.org>
 * Copyright (c) 2018 Panasas
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

/* 
 * This driver is a super-set of the now-deleted jedec_ts(4), and most of the
 * code for reading and reporting the temperature is either based on that driver,
 * or copied from it verbatim.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <dev/jedec_dimm/jedec_dimm.h>
#include <dev/smbus/smbconf.h>
#include <dev/smbus/smbus.h>

#include "smbus_if.h"

struct jedec_dimm_softc {
	device_t dev;
	device_t smbus;
	uint8_t spd_addr;	/* SMBus address of the SPD EEPROM. */
	uint8_t tsod_addr;	/* Address of the Thermal Sensor On DIMM */
	uint32_t capacity_mb;
	char type_str[5];
	char part_str[21]; /* 18 (DDR3) or 20 (DDR4) chars, plus terminator */
	char serial_str[9]; /* 4 bytes = 8 nybble characters, plus terminator */
	char *slotid_str; /* Optional DIMM slot identifier (silkscreen) */
};

/* General Thermal Sensor on DIMM (TSOD) identification notes.
 *
 * The JEDEC TSE2004av specification defines the device ID that all compliant
 * devices should use, but very few do in practice. Maybe that's because the
 * earlier TSE2002av specification was rather vague about that.
 * Rare examples are IDT TSE2004GB2B0 and Atmel AT30TSE004A, not sure if
 * they are TSE2004av compliant by design or by accident.
 * Also, the specification mandates that PCI SIG manufacturer IDs are to be
 * used, but in practice the JEDEC manufacturer IDs are often used.
 */
const struct jedec_dimm_tsod_dev {
	uint16_t	vendor_id;
	uint8_t		device_id;
	const char	*description;
} known_tsod_devices[] = {
	/* Analog Devices ADT7408.
	 * http://www.analog.com/media/en/technical-documentation/data-sheets/ADT7408.pdf
	 */
	{ 0x11d4, 0x08, "Analog Devices TSOD" },

	/* Atmel AT30TSE002B, AT30TSE004A.
	 * http://www.atmel.com/images/doc8711.pdf
	 * http://www.atmel.com/images/atmel-8868-dts-at30tse004a-datasheet.pdf
	 * Note how one chip uses the JEDEC Manufacturer ID while the other
	 * uses the PCI SIG one.
	 */
	{ 0x001f, 0x82, "Atmel TSOD" },
	{ 0x1114, 0x22, "Atmel TSOD" },

	/* Integrated Device Technology (IDT) TS3000B3A, TSE2002B3C,
	 * TSE2004GB2B0 chips and their variants.
	 * http://www.idt.com/sites/default/files/documents/IDT_TSE2002B3C_DST_20100512_120303152056.pdf
	 * http://www.idt.com/sites/default/files/documents/IDT_TS3000B3A_DST_20101129_120303152013.pdf
	 * https://www.idt.com/document/dst/tse2004gb2b0-datasheet
	 */
	{ 0x00b3, 0x29, "IDT TSOD" },
	{ 0x00b3, 0x22, "IDT TSOD" },

	/* Maxim Integrated MAX6604.
	 * Different document revisions specify different Device IDs.
	 * Document 19-3837; Rev 0; 10/05 has 0x3e00 while
	 * 19-3837; Rev 3; 10/11 has 0x5400.
	 * http://datasheets.maximintegrated.com/en/ds/MAX6604.pdf
	 */
	{ 0x004d, 0x3e, "Maxim Integrated TSOD" },
	{ 0x004d, 0x54, "Maxim Integrated TSOD" },

	/* Microchip Technology MCP9805, MCP9843, MCP98242, MCP98243
	 * and their variants.
	 * http://ww1.microchip.com/downloads/en/DeviceDoc/21977b.pdf
	 * Microchip Technology EMC1501.
	 * http://ww1.microchip.com/downloads/en/DeviceDoc/00001605A.pdf
	 */
	{ 0x0054, 0x00, "Microchip TSOD" },
	{ 0x0054, 0x20, "Microchip TSOD" },
	{ 0x0054, 0x21, "Microchip TSOD" },
	{ 0x1055, 0x08, "Microchip TSOD" },

	/* NXP Semiconductors SE97 and SE98.
	 * http://www.nxp.com/docs/en/data-sheet/SE97B.pdf
	 */
	{ 0x1131, 0xa1, "NXP TSOD" },
	{ 0x1131, 0xa2, "NXP TSOD" },

	/* ON Semiconductor CAT34TS02 revisions B and C, CAT6095 and compatible.
	 * https://www.onsemi.com/pub/Collateral/CAT34TS02-D.PDF
	 * http://www.onsemi.com/pub/Collateral/CAT6095-D.PDF
	 */
	{ 0x1b09, 0x08, "ON Semiconductor TSOD" },
	{ 0x1b09, 0x0a, "ON Semiconductor TSOD" },

	/* ST[Microelectronics] STTS424E02, STTS2002 and others.
	 * http://www.st.com/resource/en/datasheet/cd00157558.pdf
	 * http://www.st.com/resource/en/datasheet/stts2002.pdf
	 */
	{ 0x104a, 0x00, "ST Microelectronics TSOD" },
	{ 0x104a, 0x03, "ST Microelectronics TSOD" },
};

static int jedec_dimm_attach(device_t dev);

static int jedec_dimm_capacity(struct jedec_dimm_softc *sc, enum dram_type type,
    uint32_t *capacity_mb);

static int jedec_dimm_detach(device_t dev);

static int jedec_dimm_dump(struct jedec_dimm_softc *sc, enum dram_type type);

static int jedec_dimm_field_to_str(struct jedec_dimm_softc *sc, char *dst,
    size_t dstsz, uint16_t offset, uint16_t len, bool ascii);

static int jedec_dimm_probe(device_t dev);

static int jedec_dimm_readw_be(struct jedec_dimm_softc *sc, uint8_t reg,
    uint16_t *val);

static int jedec_dimm_temp_sysctl(SYSCTL_HANDLER_ARGS);

static const char *jedec_dimm_tsod_match(uint16_t vid, uint16_t did);


/**
 * device_attach() method. Read the DRAM type, use that to determine the offsets
 * and lengths of the asset string fields. Calculate the capacity. If a TSOD is
 * present, figure out exactly what it is, and update the device description.
 * If all of that was successful, create the sysctls for the DIMM. If an
 * optional slotid has been hinted, create a sysctl for that too.
 *
 * @author rpokala
 *
 * @param[in,out] dev
 *      Device being attached.
 */
static int
jedec_dimm_attach(device_t dev)
{
	uint8_t byte;
	uint16_t devid;
	uint16_t partnum_len;
	uint16_t partnum_offset;
	uint16_t serial_len;
	uint16_t serial_offset;
	uint16_t tsod_present_offset;
	uint16_t vendorid;
	bool tsod_present;
	int rc;
	int new_desc_len;
	enum dram_type type;
	struct jedec_dimm_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *oid;
	struct sysctl_oid_list *children;
	const char *tsod_match;
	const char *slotid_str;
	char *new_desc;

	sc = device_get_softc(dev);
	ctx = device_get_sysctl_ctx(dev);
	oid = device_get_sysctl_tree(dev);
	children = SYSCTL_CHILDREN(oid);

	bzero(sc, sizeof(*sc));
	sc->dev = dev;
	sc->smbus = device_get_parent(dev);
	sc->spd_addr = smbus_get_addr(dev);

	/* The TSOD address has a different DTI from the SPD address, but shares
	 * the LSA bits.
	 */
	sc->tsod_addr = JEDEC_DTI_TSOD | (sc->spd_addr & 0x0f);

	/* Read the DRAM type, and set the various offsets and lengths. */
	rc = smbus_readb(sc->smbus, sc->spd_addr, SPD_OFFSET_DRAM_TYPE, &byte);
	if (rc != 0) {
		device_printf(dev, "failed to read dram_type: %d\n", rc);
		goto out;
	}
	type = (enum dram_type) byte;
	switch (type) {
	case DRAM_TYPE_DDR3_SDRAM:
		(void) snprintf(sc->type_str, sizeof(sc->type_str), "DDR3");
		partnum_len = SPD_LEN_DDR3_PARTNUM;
		partnum_offset = SPD_OFFSET_DDR3_PARTNUM;
		serial_len = SPD_LEN_DDR3_SERIAL;
		serial_offset = SPD_OFFSET_DDR3_SERIAL;
		tsod_present_offset = SPD_OFFSET_DDR3_TSOD_PRESENT;
		break;
	case DRAM_TYPE_DDR4_SDRAM:
		(void) snprintf(sc->type_str, sizeof(sc->type_str), "DDR4");
		partnum_len = SPD_LEN_DDR4_PARTNUM;
		partnum_offset = SPD_OFFSET_DDR4_PARTNUM;
		serial_len = SPD_LEN_DDR4_SERIAL;
		serial_offset = SPD_OFFSET_DDR4_SERIAL;
		tsod_present_offset = SPD_OFFSET_DDR4_TSOD_PRESENT;
		break;
	default:
		device_printf(dev, "unsupported dram_type 0x%02x\n", type);
		rc = EINVAL;
		goto out;
	}

	if (bootverbose) {
		/* bootverbose debuggery is best-effort, so ignore the rc. */
		(void) jedec_dimm_dump(sc, type);
	}

	/* Read all the required info from the SPD. If any of it fails, error
	 * out without creating the sysctls.
	 */
	rc = jedec_dimm_capacity(sc, type, &sc->capacity_mb);
	if (rc != 0) {
		goto out;
	}

	rc = jedec_dimm_field_to_str(sc, sc->part_str, sizeof(sc->part_str),
	    partnum_offset, partnum_len, true);
	if (rc != 0) {
		goto out;
	}

	rc = jedec_dimm_field_to_str(sc, sc->serial_str, sizeof(sc->serial_str),
	    serial_offset, serial_len, false);
	if (rc != 0) {
		goto out;
	}

	/* The MSBit of the TSOD-presence byte reports whether or not the TSOD
	 * is in fact present. If it is, read manufacturer and device info from
	 * it to confirm that it's a valid TSOD device. It's an error if any of
	 * those bytes are unreadable; it's not an error if the device is simply
	 * not known to us (tsod_match == NULL).
	 * While DDR3 and DDR4 don't explicitly require a TSOD, essentially all
	 * DDR3 and DDR4 DIMMs include one.
	 */
	rc = smbus_readb(sc->smbus, sc->spd_addr, tsod_present_offset, &byte);
	if (rc != 0) {
		device_printf(dev, "failed to read TSOD-present byte: %d\n",
		    rc);
		goto out;
	}
	if (byte & 0x80) {
		tsod_present = true;
		rc = jedec_dimm_readw_be(sc, TSOD_REG_MANUFACTURER, &vendorid);
		if (rc != 0) {
			device_printf(dev,
			    "failed to read TSOD Manufacturer ID\n");
			goto out;
		}
		rc = jedec_dimm_readw_be(sc, TSOD_REG_DEV_REV, &devid);
		if (rc != 0) {
			device_printf(dev, "failed to read TSOD Device ID\n");
			goto out;
		}

		tsod_match = jedec_dimm_tsod_match(vendorid, devid);
		if (bootverbose) {
			if (tsod_match == NULL) {
				device_printf(dev,
				    "Unknown TSOD Manufacturer and Device IDs,"
				    " 0x%x and 0x%x\n", vendorid, devid);
			} else {
				device_printf(dev,
				    "TSOD: %s\n", tsod_match);
			}
		}
	} else {
		tsod_match = NULL;
		tsod_present = false;
	}

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "type",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, sc->type_str, 0,
	    "DIMM type");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "capacity",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, sc->capacity_mb,
	    "DIMM capacity (MB)");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "part",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, sc->part_str, 0,
	    "DIMM Part Number");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "serial",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, sc->serial_str, 0,
	    "DIMM Serial Number");

	/* Create the temperature sysctl IFF the TSOD is present and valid */
	if (tsod_present && (tsod_match != NULL)) {
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "temp",
		    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, dev, 0,
		    jedec_dimm_temp_sysctl, "IK", "DIMM temperature (deg C)");
	}

	/* If a "slotid" was hinted, add the sysctl for it. */
	if (resource_string_value(device_get_name(dev), device_get_unit(dev),
	    "slotid", &slotid_str) == 0) {
		if (slotid_str != NULL) {
			sc->slotid_str = strdup(slotid_str, M_DEVBUF);
			SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "slotid",
			    CTLFLAG_RD | CTLFLAG_MPSAFE, sc->slotid_str, 0,
			    "DIMM Slot Identifier");
		}
	}

	/* If a TSOD type string or a slotid are present, add them to the
	 * device description.
	 */
	if ((tsod_match != NULL) || (sc->slotid_str != NULL)) {
		new_desc_len = strlen(device_get_desc(dev));
		if (tsod_match != NULL) {
			new_desc_len += strlen(tsod_match);
			new_desc_len += 4; /* " w/ " */
		}
		if (sc->slotid_str != NULL) {
			new_desc_len += strlen(sc->slotid_str);
			new_desc_len += 3; /* space + parens */
		}
		new_desc_len++; /* terminator */
		new_desc = malloc(new_desc_len, M_TEMP, (M_WAITOK | M_ZERO));
		(void) snprintf(new_desc, new_desc_len, "%s%s%s%s%s%s",
		    device_get_desc(dev),
		    (tsod_match ? " w/ " : ""),
		    (tsod_match ? tsod_match : ""),
		    (sc->slotid_str ? " (" : ""),
		    (sc->slotid_str ? sc->slotid_str : ""),
		    (sc->slotid_str ? ")" : ""));
		device_set_desc_copy(dev, new_desc);
		free(new_desc, M_TEMP);
	}

out:
	return (rc);
}

/**
 * Calculate the capacity of a DIMM. Both DDR3 and DDR4 encode "geometry"
 * information in various SPD bytes. The standards documents codify everything
 * in look-up tables, but it's trivial to reverse-engineer the the formulas for
 * most of them. Unless otherwise noted, the same formulas apply for both DDR3
 * and DDR4. The SPD offsets of where the data comes from are different between
 * the two types, because having them be the same would be too easy.
 *
 * @author rpokala
 *
 * @param[in] sc
 *      Instance-specific context data
 *
 * @param[in] dram_type
 *      The locations of the data used to calculate the capacity depends on the
 *      type of the DIMM.
 *
 * @param[out] capacity_mb
 *      The calculated capacity, in MB
 */
static int
jedec_dimm_capacity(struct jedec_dimm_softc *sc, enum dram_type type,
    uint32_t *capacity_mb)
{
	uint8_t bus_width_byte;
	uint8_t bus_width_offset;
	uint8_t dimm_ranks_byte;
	uint8_t dimm_ranks_offset;
	uint8_t sdram_capacity_byte;
	uint8_t sdram_capacity_offset;
	uint8_t sdram_pkg_type_byte;
	uint8_t sdram_pkg_type_offset;
	uint8_t sdram_width_byte;
	uint8_t sdram_width_offset;
	uint32_t bus_width;
	uint32_t dimm_ranks;
	uint32_t sdram_capacity;
	uint32_t sdram_pkg_type;
	uint32_t sdram_width;
	int rc;

	switch (type) {
	case DRAM_TYPE_DDR3_SDRAM:
		bus_width_offset = SPD_OFFSET_DDR3_BUS_WIDTH;
		dimm_ranks_offset = SPD_OFFSET_DDR3_DIMM_RANKS;
		sdram_capacity_offset = SPD_OFFSET_DDR3_SDRAM_CAPACITY;
		sdram_width_offset = SPD_OFFSET_DDR3_SDRAM_WIDTH;
		break;
	case DRAM_TYPE_DDR4_SDRAM:
		bus_width_offset = SPD_OFFSET_DDR4_BUS_WIDTH;
		dimm_ranks_offset = SPD_OFFSET_DDR4_DIMM_RANKS;
		sdram_capacity_offset = SPD_OFFSET_DDR4_SDRAM_CAPACITY;
		sdram_pkg_type_offset = SPD_OFFSET_DDR4_SDRAM_PKG_TYPE;
		sdram_width_offset = SPD_OFFSET_DDR4_SDRAM_WIDTH;
		break;
	default:
		device_printf(sc->dev, "unsupported dram_type 0x%02x\n", type);
		rc = EINVAL;
		goto out;
	}

	rc = smbus_readb(sc->smbus, sc->spd_addr, bus_width_offset,
	    &bus_width_byte);
	if (rc != 0) {
		device_printf(sc->dev, "failed to read bus_width: %d\n", rc);
		goto out;
	}

	rc = smbus_readb(sc->smbus, sc->spd_addr, dimm_ranks_offset,
	    &dimm_ranks_byte);
	if (rc != 0) {
		device_printf(sc->dev, "failed to read dimm_ranks: %d\n", rc);
		goto out;
	}

	rc = smbus_readb(sc->smbus, sc->spd_addr, sdram_capacity_offset,
	    &sdram_capacity_byte);
	if (rc != 0) {
		device_printf(sc->dev, "failed to read sdram_capacity: %d\n",
		    rc);
		goto out;
	}

	rc = smbus_readb(sc->smbus, sc->spd_addr, sdram_width_offset,
	    &sdram_width_byte);
	if (rc != 0) {
		device_printf(sc->dev, "failed to read sdram_width: %d\n", rc);
		goto out;
	}

	/* The "SDRAM Package Type" is only needed for DDR4 DIMMs. */
	if (type == DRAM_TYPE_DDR4_SDRAM) {
		rc = smbus_readb(sc->smbus, sc->spd_addr, sdram_pkg_type_offset,
		    &sdram_pkg_type_byte);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to read sdram_pkg_type: %d\n", rc);
			goto out;
		}
	}

	/* "Primary bus width, in bits" is in bits [2:0]. */
	bus_width_byte &= 0x07;
	if (bus_width_byte <= 3) {
		bus_width = 1 << bus_width_byte;
		bus_width *= 8;
	} else {
		device_printf(sc->dev, "invalid bus width info\n");
		rc = EINVAL;
		goto out;
	}

	/* "Number of ranks per DIMM" is in bits [5:3]. Values 4-7 are only
	 * valid for DDR4.
	 */
	dimm_ranks_byte >>= 3;
	dimm_ranks_byte &= 0x07;
	if (dimm_ranks_byte <= 7) {
		dimm_ranks = dimm_ranks_byte + 1;
	} else {
		device_printf(sc->dev, "invalid DIMM Rank info\n");
		rc = EINVAL;
		goto out;
	}
	if ((dimm_ranks_byte >= 4) && (type != DRAM_TYPE_DDR4_SDRAM)) {
		device_printf(sc->dev, "invalid DIMM Rank info\n");
		rc = EINVAL;
		goto out;
	}

	/* "Total SDRAM capacity per die, in Mb" is in bits [3:0]. There are two
	 * different formulas, for values 0-7 and for values 8-9. Also, values
	 * 7-9 are only valid for DDR4.
	 */
	sdram_capacity_byte &= 0x0f;
	if (sdram_capacity_byte <= 7) {
		sdram_capacity = 1 << sdram_capacity_byte;
		sdram_capacity *= 256;
	} else if (sdram_capacity_byte <= 9) {
		sdram_capacity = 12 << (sdram_capacity_byte - 8);
		sdram_capacity *= 1024;
	} else {
		device_printf(sc->dev, "invalid SDRAM capacity info\n");
		rc = EINVAL;
		goto out;
	}
	if ((sdram_capacity_byte >= 7) && (type != DRAM_TYPE_DDR4_SDRAM)) {
		device_printf(sc->dev, "invalid SDRAM capacity info\n");
		rc = EINVAL;
		goto out;
	}

	/* "SDRAM device width" is in bits [2:0]. */
	sdram_width_byte &= 0x7;
	if (sdram_width_byte <= 3) {
		sdram_width = 1 << sdram_width_byte;
		sdram_width *= 4;
	} else {
		device_printf(sc->dev, "invalid SDRAM width info\n");
		rc = EINVAL;
		goto out;
	}

	/* DDR4 has something called "3DS", which is indicated by [1:0] = 2;
	 * when that is the case, the die count is encoded in [6:4], and
	 * dimm_ranks is multiplied by it.
	 */
	if ((type == DRAM_TYPE_DDR4_SDRAM) &&
	    ((sdram_pkg_type_byte & 0x3) == 2)) {
		sdram_pkg_type_byte >>= 4;
		sdram_pkg_type_byte &= 0x07;
		sdram_pkg_type = sdram_pkg_type_byte + 1;
		dimm_ranks *= sdram_pkg_type;
	}

	/* Finally, assemble the actual capacity. The formula is the same for
	 * both DDR3 and DDR4.
	 */
	*capacity_mb = sdram_capacity / 8 * bus_width / sdram_width *
	    dimm_ranks;

out:
	return (rc);
}

/**
 * device_detach() method. If we allocated sc->slotid_str, free it. Even if we
 *      didn't allocate, free it anyway; free(NULL) is safe.
 *
 * @author rpokala
 *
 * @param[in,out] dev
 *      Device being detached.
 */
static int
jedec_dimm_detach(device_t dev)
{
	struct jedec_dimm_softc *sc;

	sc = device_get_softc(dev);
	free(sc->slotid_str, M_DEVBUF);

	return (0);
}

/**
 * Read and dump the entire SPD contents.
 *
 * @author rpokala
 *
 * @param[in] sc
 *      Instance-specific context data
 *
 * @param[in] dram_type
 *      The length of data which needs to be read and dumped differs based on
 *      the type of the DIMM.
 */
static int
jedec_dimm_dump(struct jedec_dimm_softc *sc, enum dram_type type)
{
	int i;
	int rc;
	bool page_changed;
	uint8_t bytes[512];

	page_changed = false;

	for (i = 0; i < 256; i++) {
		rc = smbus_readb(sc->smbus, sc->spd_addr, i, &bytes[i]);
		if (rc != 0) {
			device_printf(sc->dev,
			    "unable to read page0:0x%02x: %d\n", i, rc);
			goto out;
		}
	}

	/* The DDR4 SPD is 512 bytes, but SMBus only allows for 8-bit offsets.
	 * JEDEC gets around this by defining the "PAGE" DTI and LSAs.
	 */
	if (type == DRAM_TYPE_DDR4_SDRAM) {
		page_changed = true;
		rc = smbus_writeb(sc->smbus,
		    (JEDEC_DTI_PAGE | JEDEC_LSA_PAGE_SET1), 0, 0);
		if (rc != 0) {
			device_printf(sc->dev, "unable to change page: %d\n",
			    rc);
			goto out;
		}
		/* Add 256 to the store location, because we're in the second
		 * page.
		 */
		for (i = 0; i < 256; i++) {
			rc = smbus_readb(sc->smbus, sc->spd_addr, i,
			    &bytes[256 + i]);
			if (rc != 0) {
				device_printf(sc->dev,
				    "unable to read page1:0x%02x: %d\n", i, rc);
				goto out;
			}
		}
	}

	/* Display the data in a nice hexdump format, with byte offsets. */
	hexdump(bytes, (page_changed ? 512 : 256), NULL, 0);

out:
	if (page_changed) {
		int rc2;
		/* Switch back to page0 before returning. */
		rc2 = smbus_writeb(sc->smbus,
		    (JEDEC_DTI_PAGE | JEDEC_LSA_PAGE_SET0), 0, 0);
		if (rc2 != 0) {
			device_printf(sc->dev, "unable to restore page: %d\n",
			    rc2);
		}
	}
	return (rc);
}

/**
 * Read a specified range of bytes from the SPD, convert them to a string, and
 * store them in the provided buffer. Some SPD fields are space-padded ASCII,
 * and some are just a string of bits that we want to convert to a hex string.
 *
 * @author rpokala
 *
 * @param[in] sc
 *      Instance-specific context data
 *
 * @param[out] dst
 *      The output buffer to populate
 *
 * @param[in] dstsz
 *      The size of the output buffer
 *
 * @param[in] offset
 *      The starting offset of the field within the SPD
 *
 * @param[in] len
 *      The length in bytes of the field within the SPD
 *
 * @param[in] ascii
 *      Is the field a sequence of ASCII characters? If not, it is binary data
 *      which should be converted to characters.
 */
static int
jedec_dimm_field_to_str(struct jedec_dimm_softc *sc, char *dst, size_t dstsz,
    uint16_t offset, uint16_t len, bool ascii)
{
	uint8_t byte;
	int i;
	int rc;
	bool page_changed;

	/* Change to the proper page. Offsets [0, 255] are in page0; offsets
	 * [256, 512] are in page1.
	 *
	 * *The page must be reset to page0 before returning.*
	 *
	 * For the page-change operation, only the DTI and LSA matter; the
	 * offset and write-value are ignored, so use just 0.
	 *
	 * Mercifully, JEDEC defined the fields such that none of them cross
	 * pages, so we don't need to worry about that complication.
	 */
	if (offset < JEDEC_SPD_PAGE_SIZE) {
		page_changed = false;
	} else if (offset < (2 * JEDEC_SPD_PAGE_SIZE)) {
		page_changed = true;
		rc = smbus_writeb(sc->smbus,
		    (JEDEC_DTI_PAGE | JEDEC_LSA_PAGE_SET1), 0, 0);
		if (rc != 0) {
			device_printf(sc->dev,
			    "unable to change page for offset 0x%04x: %d\n",
			    offset, rc);
		}
		/* Adjust the offset to account for the page change. */
		offset -= JEDEC_SPD_PAGE_SIZE;
	} else {
		page_changed = false;
		rc = EINVAL;
		device_printf(sc->dev, "invalid offset 0x%04x\n", offset);
		goto out;
	}

	/* Sanity-check (adjusted) offset and length; everything must be within
	 * the same page.
	 */
	if (offset >= JEDEC_SPD_PAGE_SIZE) {
		rc = EINVAL;
		device_printf(sc->dev, "invalid offset 0x%04x\n", offset);
		goto out;
	}
	if ((offset + len) >= JEDEC_SPD_PAGE_SIZE) {
		rc = EINVAL;
		device_printf(sc->dev,
		    "(offset + len) would cross page (0x%04x + 0x%04x)\n",
		    offset, len);
		goto out;
	}

	/* Sanity-check the destination string length. If we're dealing with
	 * ASCII chars, then the destination must be at least the same length;
	 * otherwise, it must be *twice* the length, because each byte must
	 * be converted into two nybble characters.
	 *
	 * And, of course, there needs to be an extra byte for the terminator.
	 */
	if (ascii) {
		if (dstsz < (len + 1)) {
			rc = EINVAL;
			device_printf(sc->dev,
			    "destination too short (%u < %u)\n",
			    (uint16_t) dstsz, (len + 1));
			goto out;
		}
	} else {
		if (dstsz < ((2 * len) + 1)) {
			rc = EINVAL;
			device_printf(sc->dev,
			    "destination too short (%u < %u)\n",
			    (uint16_t) dstsz, ((2 * len) + 1));
			goto out;
		}
	}

	/* Read a byte at a time. */
	for (i = 0; i < len; i++) {
		rc = smbus_readb(sc->smbus, sc->spd_addr, (offset + i), &byte);
		if (rc != 0) {
			device_printf(sc->dev,
			    "failed to read byte at 0x%02x: %d\n",
			    (offset + i), rc);
			goto out;
		}
		if (ascii) {
			/* chars can be copied directly. */
			dst[i] = byte;
		} else {
			/* Raw bytes need to be converted to a two-byte hex
			 * string, plus the terminator.
			 */
			(void) snprintf(&dst[(2 * i)], 3, "%02x", byte);
		}
	}

	/* If we're dealing with ASCII, convert trailing spaces to NULs. */
	if (ascii) {
		for (i = dstsz; i > 0; i--) {
			if (dst[i] == ' ') {
				dst[i] = 0;
			} else if (dst[i] == 0) {
				continue;
			} else {
				break;
			}
		}
	}

out:
	if (page_changed) {
		int rc2;
		/* Switch back to page0 before returning. */
		rc2 = smbus_writeb(sc->smbus,
		    (JEDEC_DTI_PAGE | JEDEC_LSA_PAGE_SET0), 0, 0);
		if (rc2 != 0) {
			device_printf(sc->dev,
			    "unable to restore page for offset 0x%04x: %d\n",
			    offset, rc2);
		}
	}

	return (rc);
}

/**
 * device_probe() method. Validate the address that was given as a hint, and
 * display an error if it's bogus. Make sure that we're dealing with one of the
 * SPD versions that we can handle.
 *
 * @author rpokala
 *
 * @param[in] dev
 *      Device being probed.
 */
static int
jedec_dimm_probe(device_t dev)
{
	uint8_t addr;
	uint8_t byte;
	int rc;
	enum dram_type type;
	device_t smbus;

	smbus = device_get_parent(dev);
	addr = smbus_get_addr(dev);

	/* Don't bother if this isn't an SPD address, or if the LSBit is set. */
	if (((addr & 0xf0) != JEDEC_DTI_SPD) ||
	    ((addr & 0x01) != 0)) {
		device_printf(dev,
		    "invalid \"addr\" hint; address must start with \"0x%x\","
		    " and the least-significant bit must be 0\n",
		    JEDEC_DTI_SPD);
		rc = ENXIO;
		goto out;
	}

	/* Try to read the DRAM_TYPE from the SPD. */
	rc = smbus_readb(smbus, addr, SPD_OFFSET_DRAM_TYPE, &byte);
	if (rc != 0) {
		device_printf(dev, "failed to read dram_type\n");
		goto out;
	}

	/* This driver currently only supports DDR3 and DDR4 SPDs. */
	type = (enum dram_type) byte;
	switch (type) {
	case DRAM_TYPE_DDR3_SDRAM:
		rc = BUS_PROBE_DEFAULT;
		device_set_desc(dev, "DDR3 DIMM");
		break;
	case DRAM_TYPE_DDR4_SDRAM:
		rc = BUS_PROBE_DEFAULT;
		device_set_desc(dev, "DDR4 DIMM");
		break;
	default:
		rc = ENXIO;
		break;
	}

out:
	return (rc);
}

/**
 * SMBus specifies little-endian byte order, but it looks like the TSODs use
 * big-endian. Read and convert.
 *
 * @author avg
 *
 * @param[in] sc
 *      Instance-specific context data
 *
 * @param[in] reg
 *      The register number to read.
 *
 * @param[out] val
 *      Pointer to populate with the value read.
 */
static int
jedec_dimm_readw_be(struct jedec_dimm_softc *sc, uint8_t reg, uint16_t *val)
{
	int rc;

	rc = smbus_readw(sc->smbus, sc->tsod_addr, reg, val);
	if (rc != 0) {
		goto out;
	}
	*val = be16toh(*val);

out:
	return (rc);
}

/**
 * Read the temperature data from the TSOD and convert it to the deciKelvin
 * value that the sysctl expects.
 *
 * @author avg
 */
static int
jedec_dimm_temp_sysctl(SYSCTL_HANDLER_ARGS)
{
	uint16_t val;
	int rc;
	int temp;
	device_t dev = arg1;
	struct jedec_dimm_softc *sc;

	sc = device_get_softc(dev);

	rc = jedec_dimm_readw_be(sc, TSOD_REG_TEMPERATURE, &val);
	if (rc != 0) {
		goto out;
	}

	/* The three MSBits are flags, and the next bit is a sign bit. */
	temp = val & 0xfff;
	if ((val & 0x1000) != 0)
		temp = -temp;
	/* Each step is 0.0625 degrees, so convert to 1000ths of a degree C. */
	temp *= 625;
	/* ... and then convert to 1000ths of a Kelvin */
	temp += 2731500;
	/* As a practical matter, few (if any) TSODs are more accurate than
	 * about a tenth of a degree, so round accordingly. This correlates with
	 * the "IK" formatting used for this sysctl.
	 */
	temp = (temp + 500) / 1000;

	rc = sysctl_handle_int(oidp, &temp, 0, req);

out:
	return (rc);
}

/**
 * Check the TSOD's Vendor ID and Device ID against the list of known TSOD
 * devices. Return the description, or NULL if this doesn't look like a valid
 * TSOD.
 *
 * @author avg
 *
 * @param[in] vid
 *      The Vendor ID of the TSOD device
 *
 * @param[in] did
 *      The Device ID of the TSOD device
 *
 * @return
 *      The description string, or NULL for a failure to match.
 */
static const char *
jedec_dimm_tsod_match(uint16_t vid, uint16_t did)
{
	const struct jedec_dimm_tsod_dev *d;
	int i;

	for (i = 0; i < nitems(known_tsod_devices); i++) {
		d = &known_tsod_devices[i];
		if ((vid == d->vendor_id) && ((did >> 8) == d->device_id)) {
			return (d->description);
		}
	}

	/* If no matches for a specific device, then check for a generic
	 * TSE2004av-compliant device.
	 */
	if ((did >> 8) == 0x22) {
		return ("TSE2004av compliant TSOD");
	}

	return (NULL);
}

static device_method_t jedec_dimm_methods[] = {
	/* Methods from the device interface */
	DEVMETHOD(device_probe,		jedec_dimm_probe),
	DEVMETHOD(device_attach,	jedec_dimm_attach),
	DEVMETHOD(device_detach,	jedec_dimm_detach),
	DEVMETHOD_END
};

static driver_t jedec_dimm_driver = {
	.name = "jedec_dimm",
	.methods = jedec_dimm_methods,
	.size = sizeof(struct jedec_dimm_softc),
};

static devclass_t jedec_dimm_devclass;

DRIVER_MODULE(jedec_dimm, smbus, jedec_dimm_driver, jedec_dimm_devclass, 0, 0);
MODULE_DEPEND(jedec_dimm, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(jedec_dimm, 1);

/* vi: set ts=8 sw=4 sts=8 noet: */
