/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2008, M. Warner Losh
 * Copyright (c) 2000,2001 Jonathan Chen.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * CIS Handling for the Cardbus Bus
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>
#include <sys/endian.h>

#include <sys/pciio.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/pccard/pccardvar.h>
#include <dev/pccard/pccard_cis.h>

#include <dev/cardbus/cardbusreg.h>
#include <dev/cardbus/cardbusvar.h>
#include <dev/cardbus/cardbus_cis.h>

extern int cardbus_cis_debug;

#define	DPRINTF(a) if (cardbus_cis_debug) printf a
#define	DEVPRINTF(x) if (cardbus_cis_debug) device_printf x

#define CIS_CONFIG_SPACE	(struct resource *)~0UL

static int decode_tuple_generic(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *);
static int decode_tuple_linktarget(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *);
static int decode_tuple_vers_1(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *);
static int decode_tuple_funcid(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *);
static int decode_tuple_manfid(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *);
static int decode_tuple_funce(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *);
static int decode_tuple_bar(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *);
static int decode_tuple_unhandled(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *);
static int decode_tuple_end(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *);

static int	cardbus_read_tuple_conf(device_t cbdev, device_t child,
		    uint32_t start, uint32_t *off, int *tupleid, int *len,
		    uint8_t *tupledata);
static int	cardbus_read_tuple_mem(device_t cbdev, struct resource *res,
		    uint32_t start, uint32_t *off, int *tupleid, int *len,
		    uint8_t *tupledata);
static int	cardbus_read_tuple(device_t cbdev, device_t child,
		    struct resource *res, uint32_t start, uint32_t *off,
		    int *tupleid, int *len, uint8_t *tupledata);
static void	cardbus_read_tuple_finish(device_t cbdev, device_t child,
		    int rid, struct resource *res);
static struct resource	*cardbus_read_tuple_init(device_t cbdev, device_t child,
		    uint32_t *start, int *rid);
static int	decode_tuple(device_t cbdev, device_t child, int tupleid,
		    int len, uint8_t *tupledata, uint32_t start,
		    uint32_t *off, struct tuple_callbacks *callbacks,
		    void *);

#define	MAKETUPLE(NAME,FUNC) { CISTPL_ ## NAME, #NAME, decode_tuple_ ## FUNC }

static char *funcnames[] = {
	"Multi-Functioned",
	"Memory",
	"Serial Port",
	"Parallel Port",
	"Fixed Disk",
	"Video Adaptor",
	"Network Adaptor",
	"AIMS",
	"SCSI",
	"Security"
};

/*
 * Handler functions for various CIS tuples
 */

static int
decode_tuple_generic(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *argp)
{
	int i;

	if (cardbus_cis_debug) {
		if (info)
			printf("TUPLE: %s [%d]:", info->name, len);
		else
			printf("TUPLE: Unknown(0x%02x) [%d]:", id, len);

		for (i = 0; i < len; i++) {
			if (i % 0x10 == 0 && len > 0x10)
				printf("\n       0x%02x:", i);
			printf(" %02x", tupledata[i]);
		}
		printf("\n");
	}
	return (0);
}

static int
decode_tuple_linktarget(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *argp)
{
	int i;

	if (cardbus_cis_debug) {
		printf("TUPLE: %s [%d]:", info->name, len);

		for (i = 0; i < len; i++) {
			if (i % 0x10 == 0 && len > 0x10)
				printf("\n       0x%02x:", i);
			printf(" %02x", tupledata[i]);
		}
		printf("\n");
	}
	if (len != 3 || tupledata[0] != 'C' || tupledata[1] != 'I' ||
	    tupledata[2] != 'S') {
		printf("Invalid data for CIS Link Target!\n");
		decode_tuple_generic(cbdev, child, id, len, tupledata,
		    start, off, info, argp);
		return (EINVAL);
	}
	return (0);
}

static int
decode_tuple_vers_1(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *argp)
{
	int i;

	if (cardbus_cis_debug) {
		printf("Product version: %d.%d\n", tupledata[0], tupledata[1]);
		printf("Product name: ");
		for (i = 2; i < len; i++) {
			if (tupledata[i] == '\0')
				printf(" | ");
			else if (tupledata[i] == 0xff)
				break;
			else
				printf("%c", tupledata[i]);
		}
		printf("\n");
	}
	return (0);
}

static int
decode_tuple_funcid(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *argp)
{
	struct cardbus_devinfo *dinfo = device_get_ivars(child);
	int numnames = nitems(funcnames);
	int i;

	if (cardbus_cis_debug) {
		printf("Functions: ");
		for (i = 0; i < len; i++) {
			if (tupledata[i] < numnames)
				printf("%s", funcnames[tupledata[i]]);
			else
				printf("Unknown(%d)", tupledata[i]);
			if (i < len - 1)
				printf(", ");
		}
		printf("\n");
	}
	if (len > 0)
		dinfo->funcid = tupledata[0];		/* use first in list */
	return (0);
}

static int
decode_tuple_manfid(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *argp)
{
	struct cardbus_devinfo *dinfo = device_get_ivars(child);
	int i;

	if (cardbus_cis_debug) {
		printf("Manufacturer ID: ");
		for (i = 0; i < len; i++)
			printf("%02x", tupledata[i]);
		printf("\n");
	}

	if (len == 5) {
		dinfo->mfrid = tupledata[1] | (tupledata[2] << 8);
		dinfo->prodid = tupledata[3] | (tupledata[4] << 8);
	}
	return (0);
}

static int
decode_tuple_funce(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *argp)
{
	struct cardbus_devinfo *dinfo = device_get_ivars(child);
	int type, i;

	if (cardbus_cis_debug) {
		printf("Function Extension: ");
		for (i = 0; i < len; i++)
			printf("%02x", tupledata[i]);
		printf("\n");
	}
	if (len < 2)			/* too short */
		return (0);
	type = tupledata[0];		/* XXX <32 always? */
	switch (dinfo->funcid) {
	case PCCARD_FUNCTION_NETWORK:
		switch (type) {
		case PCCARD_TPLFE_TYPE_LAN_NID:
			if (tupledata[1] > sizeof(dinfo->funce.lan.nid)) {
				/* ignore, warning? */
				return (0);
			}
			bcopy(tupledata + 2, dinfo->funce.lan.nid,
			    tupledata[1]);
			break;
		}
		dinfo->fepresent |= 1<<type;
		break;
	}
	return (0);
}

static int
decode_tuple_bar(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *argp)
{
	struct cardbus_devinfo *dinfo = device_get_ivars(child);
	int type;
	uint8_t reg;
	uint32_t bar;

	if (len != 6) {
		device_printf(cbdev, "CIS BAR length not 6 (%d)\n", len);
		return (EINVAL);
	}

	reg = *tupledata;
	len = le32toh(*(uint32_t*)(tupledata + 2));
	if (reg & TPL_BAR_REG_AS)
		type = SYS_RES_IOPORT;
	else
		type = SYS_RES_MEMORY;

	bar = reg & TPL_BAR_REG_ASI_MASK;
	if (bar == 0) {
		device_printf(cbdev, "Invalid BAR type 0 in CIS\n");
		return (EINVAL);	/* XXX Return an error? */
	} else if (bar == 7) {
		/* XXX Should we try to map in Option ROMs? */
		return (0);
	}

	/* Convert from BAR type to BAR offset */
	bar = PCIR_BAR(bar - 1);

	if (type == SYS_RES_MEMORY) {
		if (reg & TPL_BAR_REG_PREFETCHABLE)
			dinfo->mprefetchable |= (1 << PCI_RID2BAR(bar));
		/*
		 * The PC Card spec says we're only supposed to honor this
		 * hint when the cardbus bridge is a child of pci0 (the main
		 * bus).  The PC Card spec seems to indicate that this should
		 * only be done on x86 based machines, which suggests that on
		 * non-x86 machines the addresses can be anywhere.  Since the
		 * hardware can do it on non-x86 machines, it should be able
		 * to do it on x86 machines too.  Therefore, we can and should
		 * ignore this hint.  Furthermore, the PC Card spec recommends
		 * always allocating memory above 1MB, contradicting the other
		 * part of the PC Card spec, it seems.  We make note of it,
		 * but otherwise don't use this information.
		 *
		 * Some Realtek cards have this set in their CIS, but fail
		 * to actually work when mapped this way, and experience
		 * has shown ignoring this big to be a wise choice.
		 *
		 * XXX We should cite chapter and verse for standard refs.
		 */
		if (reg & TPL_BAR_REG_BELOW1MB)
			dinfo->mbelow1mb |= (1 << PCI_RID2BAR(bar));
	}

	return (0);
}

static int
decode_tuple_unhandled(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *argp)
{
	/* Make this message suck less XXX */
	printf("TUPLE: %s [%d] is unhandled! Bailing...", info->name, len);
	return (EINVAL);
}

static int
decode_tuple_end(device_t cbdev, device_t child, int id,
    int len, uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *info, void *argp)
{
	if (cardbus_cis_debug)
		printf("CIS reading done\n");
	return (0);
}

/*
 * Functions to read the a tuple from the card
 */

/*
 * Read CIS bytes out of the config space.  We have to read it 4 bytes at a
 * time and do the usual mask and shift to return the bytes.  The standard
 * defines the byte order to be little endian.  pci_read_config converts it to
 * host byte order.  This is why we have no endian conversion functions: the
 * shifts wind up being endian neutral.  This is also why we avoid the obvious
 * memcpy optimization.
 */
static int
cardbus_read_tuple_conf(device_t cbdev, device_t child, uint32_t start,
    uint32_t *off, int *tupleid, int *len, uint8_t *tupledata)
{
	int i, j;
	uint32_t e;
	uint32_t loc;

	loc = start + *off;

	e = pci_read_config(child, loc & ~0x3, 4);
	e >>= 8 * (loc & 0x3);
	*len = 0;
	for (i = loc, j = -2; j < *len; j++, i++) {
		if ((i & 0x3) == 0)
			e = pci_read_config(child, i, 4);
		if (j == -2)
			*tupleid = 0xff & e;
		else if (j == -1)
			*len = 0xff & e;
		else
			tupledata[j] = 0xff & e;
		e >>= 8;
	}
	*off += *len + 2;
	return (0);
}

/*
 * Read the CIS data out of memory.  We indirect through the bus space
 * routines to ensure proper byte ordering conversions when necessary.
 */
static int
cardbus_read_tuple_mem(device_t cbdev, struct resource *res, uint32_t start,
    uint32_t *off, int *tupleid, int *len, uint8_t *tupledata)
{
	int ret;

	*tupleid = bus_read_1(res, start + *off);
	*len = bus_read_1(res, start + *off + 1);
	bus_read_region_1(res, *off + start + 2, tupledata, *len);
	ret = 0;
	*off += *len + 2;
	return (ret);
}

static int
cardbus_read_tuple(device_t cbdev, device_t child, struct resource *res,
    uint32_t start, uint32_t *off, int *tupleid, int *len,
    uint8_t *tupledata)
{
	if (res == CIS_CONFIG_SPACE)
		return (cardbus_read_tuple_conf(cbdev, child, start, off,
		    tupleid, len, tupledata));
	return (cardbus_read_tuple_mem(cbdev, res, start, off, tupleid, len,
	    tupledata));
}

static void
cardbus_read_tuple_finish(device_t cbdev, device_t child, int rid,
    struct resource *res)
{
	if (res != CIS_CONFIG_SPACE) {
		bus_release_resource(child, SYS_RES_MEMORY, rid, res);
		bus_delete_resource(child, SYS_RES_MEMORY, rid);
	}
}

static struct resource *
cardbus_read_tuple_init(device_t cbdev, device_t child, uint32_t *start,
    int *rid)
{
	struct resource *res;
	uint32_t space;

	space = *start & PCIM_CIS_ASI_MASK;
	switch (space) {
	case PCIM_CIS_ASI_CONFIG:
		DEVPRINTF((cbdev, "CIS in PCI config space\n"));
		/* CIS in PCI config space need no initialization */
		return (CIS_CONFIG_SPACE);
	case PCIM_CIS_ASI_BAR0:
	case PCIM_CIS_ASI_BAR1:
	case PCIM_CIS_ASI_BAR2:
	case PCIM_CIS_ASI_BAR3:
	case PCIM_CIS_ASI_BAR4:
	case PCIM_CIS_ASI_BAR5:
		*rid = PCIR_BAR(space - PCIM_CIS_ASI_BAR0);
		DEVPRINTF((cbdev, "CIS in BAR %#x\n", *rid));
		break;
	case PCIM_CIS_ASI_ROM:
		*rid = PCIR_BIOS;
		DEVPRINTF((cbdev, "CIS in option rom\n"));
		break;
	default:
		device_printf(cbdev, "Unable to read CIS: Unknown space: %d\n",
		    space);
		return (NULL);
	}

	/* allocate the memory space to read CIS */
	res = bus_alloc_resource_any(child, SYS_RES_MEMORY, rid,
	    rman_make_alignment_flags(4096) | RF_ACTIVE);
	if (res == NULL) {
		device_printf(cbdev, "Unable to allocate resource "
		    "to read CIS.\n");
		return (NULL);
	}
	DEVPRINTF((cbdev, "CIS Mapped to %#jx\n",
	    rman_get_start(res)));

	/* Flip to the right ROM image if CIS is in ROM */
	if (space == PCIM_CIS_ASI_ROM) {
		uint32_t imagesize;
		uint32_t imagebase = 0;
		uint32_t pcidata;
		uint16_t romsig;
		int romnum = 0;
		int imagenum;

		imagenum = (*start & PCIM_CIS_ROM_MASK) >> 28;
		for (romnum = 0;; romnum++) {
			romsig = bus_read_2(res,
			    imagebase + CARDBUS_EXROM_SIGNATURE);
			if (romsig != 0xaa55) {
				device_printf(cbdev, "Bad header in rom %d: "
				    "[%x] %04x\n", romnum, imagebase +
				    CARDBUS_EXROM_SIGNATURE, romsig);
				cardbus_read_tuple_finish(cbdev, child, *rid,
				    res);
				*rid = 0;
				return (NULL);
			}

			/*
			 * If this was the Option ROM image that we were
			 * looking for, then we are done.
			 */
			if (romnum == imagenum)
				break;

			/* Find out where the next Option ROM image is */
			pcidata = imagebase + bus_read_2(res,
			    imagebase + CARDBUS_EXROM_DATA_PTR);
			imagesize = bus_read_2(res,
			    pcidata + CARDBUS_EXROM_DATA_IMAGE_LENGTH);

			if (imagesize == 0) {
				/*
				 * XXX some ROMs seem to have this as zero,
				 * can we assume this means 1 block?
				 */
				device_printf(cbdev, "Warning, size of Option "
				    "ROM image %d is 0 bytes, assuming 512 "
				    "bytes.\n", romnum);
				imagesize = 1;
			}

			/* Image size is in 512 byte units */
			imagesize <<= 9;

			if ((bus_read_1(res, pcidata +
			    CARDBUS_EXROM_DATA_INDICATOR) & 0x80) != 0) {
				device_printf(cbdev, "Cannot find CIS in "
				    "Option ROM\n");
				cardbus_read_tuple_finish(cbdev, child, *rid,
				    res);
				*rid = 0;
				return (NULL);
			}
			imagebase += imagesize;
		}
		*start = imagebase + (*start & PCIM_CIS_ADDR_MASK);
	} else {
		*start = *start & PCIM_CIS_ADDR_MASK;
	}
	DEVPRINTF((cbdev, "CIS offset is %#x\n", *start));

	return (res);
}

/*
 * Dispatch the right handler function per tuple
 */

static int
decode_tuple(device_t cbdev, device_t child, int tupleid, int len,
    uint8_t *tupledata, uint32_t start, uint32_t *off,
    struct tuple_callbacks *callbacks, void *argp)
{
	int i;
	for (i = 0; callbacks[i].id != CISTPL_GENERIC; i++) {
		if (tupleid == callbacks[i].id)
			return (callbacks[i].func(cbdev, child, tupleid, len,
			    tupledata, start, off, &callbacks[i], argp));
	}
	return (callbacks[i].func(cbdev, child, tupleid, len,
	    tupledata, start, off, NULL, argp));
}

int
cardbus_parse_cis(device_t cbdev, device_t child,
    struct tuple_callbacks *callbacks, void *argp)
{
	uint8_t *tupledata;
	int tupleid = CISTPL_NULL;
	int len;
	int expect_linktarget;
	uint32_t start, off;
	struct resource *res;
	int rid;

	tupledata = malloc(MAXTUPLESIZE, M_DEVBUF, M_WAITOK | M_ZERO);
	expect_linktarget = TRUE;
	if ((start = pci_read_config(child, PCIR_CIS, 4)) == 0) {
		DEVPRINTF((cbdev, "Warning: CIS pointer is 0: (no CIS)\n"));
		free(tupledata, M_DEVBUF);
		return (0);
	}
	DEVPRINTF((cbdev, "CIS pointer is %#x\n", start));
	off = 0;
	res = cardbus_read_tuple_init(cbdev, child, &start, &rid);
	if (res == NULL) {
		device_printf(cbdev, "Unable to allocate resources for CIS\n");
		free(tupledata, M_DEVBUF);
		return (ENXIO);
	}

	do {
		if (cardbus_read_tuple(cbdev, child, res, start, &off,
		    &tupleid, &len, tupledata) != 0) {
			device_printf(cbdev, "Failed to read CIS.\n");
			cardbus_read_tuple_finish(cbdev, child, rid, res);
			free(tupledata, M_DEVBUF);
			return (ENXIO);
		}

		if (expect_linktarget && tupleid != CISTPL_LINKTARGET) {
			device_printf(cbdev, "Expecting link target, got 0x%x\n",
			    tupleid);
			cardbus_read_tuple_finish(cbdev, child, rid, res);
			free(tupledata, M_DEVBUF);
			return (EINVAL);
		}
		expect_linktarget = decode_tuple(cbdev, child, tupleid, len,
		    tupledata, start, &off, callbacks, argp);
		if (expect_linktarget != 0) {
			device_printf(cbdev, "Parsing failed with %d\n",
			    expect_linktarget);
			cardbus_read_tuple_finish(cbdev, child, rid, res);
			free(tupledata, M_DEVBUF);
			return (expect_linktarget);
		}
	} while (tupleid != CISTPL_END);
	cardbus_read_tuple_finish(cbdev, child, rid, res);
	free(tupledata, M_DEVBUF);
	return (0);
}

int
cardbus_do_cis(device_t cbdev, device_t child)
{
	struct tuple_callbacks init_callbacks[] = {
		MAKETUPLE(LONGLINK_CB,		unhandled),
		MAKETUPLE(INDIRECT,		unhandled),
		MAKETUPLE(LONGLINK_MFC,		unhandled),
		MAKETUPLE(BAR,			bar),
		MAKETUPLE(LONGLINK_A,		unhandled),
		MAKETUPLE(LONGLINK_C,		unhandled),
		MAKETUPLE(LINKTARGET,		linktarget),
		MAKETUPLE(VERS_1,		vers_1),
		MAKETUPLE(MANFID,		manfid),
		MAKETUPLE(FUNCID,		funcid),
		MAKETUPLE(FUNCE,		funce),
		MAKETUPLE(END,			end),
		MAKETUPLE(GENERIC,		generic),
	};

	return (cardbus_parse_cis(cbdev, child, init_callbacks, NULL));
}
