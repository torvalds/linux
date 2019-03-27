/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
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

/*
 * Vybrid Family NAND Flash Controller (NFC)
 * Chapter 31, Vybrid Reference Manual, Rev. 5, 07/2013
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/time.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/nand/nand.h>
#include <dev/nand/nandbus.h>

#include <machine/bus.h>

#include "nfc_if.h"

#include <arm/freescale/vybrid/vf_common.h>

enum addr_type {
	ADDR_NONE,
	ADDR_ID,
	ADDR_ROW,
	ADDR_ROWCOL
};

struct fsl_nfc_fcm {
	uint32_t	addr_bits;
	enum addr_type  addr_type;
	uint32_t	col_addr_bits;
	uint32_t	row_addr_bits;
	u_int		read_ptr;
	u_int		addr_ptr;
	u_int		command;
	u_int		code;
};

struct vf_nand_softc {
	struct nand_softc 	nand_dev;
	bus_space_handle_t 	bsh;
	bus_space_tag_t		bst;
	struct resource		*res[2];
	struct fsl_nfc_fcm	fcm;
};

static struct resource_spec nfc_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int	vf_nand_attach(device_t);
static int	vf_nand_probe(device_t);
static int	vf_nand_send_command(device_t, uint8_t);
static int	vf_nand_send_address(device_t, uint8_t);
static int	vf_nand_start_command(device_t);
static uint8_t	vf_nand_read_byte(device_t);
static void	vf_nand_read_buf(device_t, void *, uint32_t);
static void	vf_nand_write_buf(device_t, void *, uint32_t);
static int	vf_nand_select_cs(device_t, uint8_t);
static int	vf_nand_read_rnb(device_t);

#define	CMD_READ_PAGE		0x7EE0
#define	CMD_PROG_PAGE		0x7FC0
#define	CMD_PROG_PAGE_DMA	0xFFC8
#define	CMD_ERASE		0x4EC0
#define	CMD_READ_ID		0x4804
#define	CMD_READ_STATUS		0x4068
#define	CMD_RESET		0x4040
#define	CMD_RANDOM_IN		0x7140
#define	CMD_RANDOM_OUT		0x70E0

#define	CMD_BYTE2_PROG_PAGE	0x10
#define	CMD_BYTE2_PAGE_READ	0x30
#define	CMD_BYTE2_ERASE		0xD0

#define	NFC_CMD1	0x3F00	/* Flash command 1 */
#define	NFC_CMD2	0x3F04	/* Flash command 2 */
#define	NFC_CAR		0x3F08	/* Column address */
#define	NFC_RAR		0x3F0C	/* Row address */
#define	NFC_RPT		0x3F10	/* Flash command repeat */
#define	NFC_RAI		0x3F14	/* Row address increment */
#define	NFC_SR1		0x3F18	/* Flash status 1 */
#define	NFC_SR2		0x3F1C	/* Flash status 2 */
#define	NFC_DMA_CH1	0x3F20	/* DMA channel 1 address */
#define	NFC_DMACFG	0x3F24	/* DMA configuration */
#define	NFC_SWAP	0x3F28	/* Cach swap */
#define	NFC_SECSZ	0x3F2C	/* Sector size */
#define	NFC_CFG		0x3F30	/* Flash configuration */
#define	NFC_DMA_CH2	0x3F34	/* DMA channel 2 address */
#define	NFC_ISR		0x3F38	/* Interrupt status */

#define	ECCMODE_SHIFT		17
#define	AIAD_SHIFT		5
#define	AIBN_SHIFT		4
#define	PAGECOUNT_SHIFT		0
#define	BITWIDTH_SHIFT		7
#define	BITWIDTH8		0
#define	BITWIDTH16		1
#define	PAGECOUNT_MASK		0xf

#define	CMD2_BYTE1_SHIFT	24
#define	CMD2_CODE_SHIFT		8
#define	CMD2_BUFNO_SHIFT	1
#define	CMD2_START_SHIFT	0

static device_method_t vf_nand_methods[] = {
	DEVMETHOD(device_probe,		vf_nand_probe),
	DEVMETHOD(device_attach,	vf_nand_attach),
	DEVMETHOD(nfc_start_command,	vf_nand_start_command),
	DEVMETHOD(nfc_send_command,	vf_nand_send_command),
	DEVMETHOD(nfc_send_address,	vf_nand_send_address),
	DEVMETHOD(nfc_read_byte,	vf_nand_read_byte),
	DEVMETHOD(nfc_read_buf,		vf_nand_read_buf),
	DEVMETHOD(nfc_write_buf,	vf_nand_write_buf),
	DEVMETHOD(nfc_select_cs,	vf_nand_select_cs),
	DEVMETHOD(nfc_read_rnb,		vf_nand_read_rnb),
	{ 0, 0 },
};

static driver_t vf_nand_driver = {
	"nand",
	vf_nand_methods,
	sizeof(struct vf_nand_softc),
};

static devclass_t vf_nand_devclass;
DRIVER_MODULE(vf_nand, simplebus, vf_nand_driver, vf_nand_devclass, 0, 0);

static int
vf_nand_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-nand"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family NAND controller");
	return (BUS_PROBE_DEFAULT);
}

static int
vf_nand_attach(device_t dev)
{
	struct vf_nand_softc *sc;
	int err;
	int reg;

	sc = device_get_softc(dev);
	if (bus_alloc_resources(dev, nfc_spec, sc->res)) {
		device_printf(dev, "could not allocate resources!\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	/* Size in bytes of one elementary transfer unit */
	WRITE4(sc, NFC_SECSZ, 2048);

	/* Flash mode width */
	reg = READ4(sc, NFC_CFG);
	reg |= (BITWIDTH16 << BITWIDTH_SHIFT);

	/* No correction, ECC bypass */
	reg &= ~(0x7 << ECCMODE_SHIFT);

	/* Disable Auto-incrementing of flash row address */
	reg &= ~(0x1 << AIAD_SHIFT);

	/* Disable Auto-incrementing of buffer numbers */
	reg &= ~(0x1 << AIBN_SHIFT);

	/*
	 * Number of virtual pages (in one physical flash page)
	 * to be programmed or read, etc.
	 */
	reg &= ~(PAGECOUNT_MASK);
	reg |= (1 << PAGECOUNT_SHIFT);
	WRITE4(sc, NFC_CFG, reg);

	nand_init(&sc->nand_dev, dev, NAND_ECC_NONE, 0, 0, NULL, NULL);
	err = nandbus_create(dev);
	return (err);
}

static int
vf_nand_start_command(device_t dev)
{
	struct vf_nand_softc *sc;
	struct fsl_nfc_fcm *fcm;
	int reg;

	sc = device_get_softc(dev);
	fcm = &sc->fcm;

	nand_debug(NDBG_DRV,"vf_nand: start command %x", fcm->command);

	/* CMD2 */
	reg = READ4(sc, NFC_CMD2);
	reg &= ~(0xff << CMD2_BYTE1_SHIFT);
	reg |= (fcm->command << CMD2_BYTE1_SHIFT);
	WRITE4(sc, NFC_CMD2, reg);

	/* CMD1 */
	if ((fcm->command == NAND_CMD_READ) ||
	    (fcm->command == NAND_CMD_PROG) ||
	    (fcm->command == NAND_CMD_ERASE)) {
		reg = READ4(sc, NFC_CMD1);
		reg &= ~(0xff << 24);

		if (fcm->command == NAND_CMD_READ)
			reg |= (CMD_BYTE2_PAGE_READ << 24);
		else if (fcm->command == NAND_CMD_PROG)
			reg |= (CMD_BYTE2_PROG_PAGE << 24);
		else if (fcm->command == NAND_CMD_ERASE)
			reg |= (CMD_BYTE2_ERASE << 24);

		WRITE4(sc, NFC_CMD1, reg);
	}

	/* We work with 1st buffer */
	reg = READ4(sc, NFC_CMD2);
	reg &= ~(0xf << CMD2_BUFNO_SHIFT);
	reg |= (0 << CMD2_BUFNO_SHIFT);
	WRITE4(sc, NFC_CMD2, reg);

	/* Cmd CODE */
	reg = READ4(sc, NFC_CMD2);
	reg &= ~(0xffff << CMD2_CODE_SHIFT);
	reg |= (fcm->code << CMD2_CODE_SHIFT);
	WRITE4(sc, NFC_CMD2, reg);

	/* Col */
	if (fcm->addr_type == ADDR_ROWCOL) {
		reg = READ4(sc, NFC_CAR);
		reg &= ~(0xffff);
		reg |= fcm->col_addr_bits;
		nand_debug(NDBG_DRV,"setting CAR to 0x%08x\n", reg);
		WRITE4(sc, NFC_CAR, reg);
	}

	/* Row */
	reg = READ4(sc, NFC_RAR);
	reg &= ~(0xffffff);
	if (fcm->addr_type == ADDR_ID)
		reg |= fcm->addr_bits;
	else
		reg |= fcm->row_addr_bits;
	WRITE4(sc, NFC_RAR, reg);

	/* Start */
	reg = READ4(sc, NFC_CMD2);
	reg |= (1 << CMD2_START_SHIFT);
	WRITE4(sc, NFC_CMD2, reg);

	/* Wait command completion */
	while (READ4(sc, NFC_CMD2) & (1 << CMD2_START_SHIFT))
		;

	return (0);
}

static int
vf_nand_send_command(device_t dev, uint8_t command)
{
	struct vf_nand_softc *sc;
	struct fsl_nfc_fcm *fcm;

	nand_debug(NDBG_DRV,"vf_nand: send command %x", command);

	sc = device_get_softc(dev);
	fcm = &sc->fcm;

	if ((command == NAND_CMD_READ_END) ||
	    (command == NAND_CMD_PROG_END) ||
	    (command == NAND_CMD_ERASE_END)) {
		return (0);
	}

	fcm->command = command;

	fcm->code = 0;
	fcm->read_ptr = 0;
	fcm->addr_type = 0;
	fcm->addr_bits = 0;

	fcm->addr_ptr = 0;
	fcm->col_addr_bits = 0;
	fcm->row_addr_bits = 0;

	switch (command) {
	case NAND_CMD_READ:
		fcm->code = CMD_READ_PAGE;
		fcm->addr_type = ADDR_ROWCOL;
		break;
	case NAND_CMD_PROG:
		fcm->code = CMD_PROG_PAGE;
		fcm->addr_type = ADDR_ROWCOL;
		break;
	case NAND_CMD_PROG_END:
		break;
	case NAND_CMD_ERASE_END:
		break;
	case NAND_CMD_RESET:
		fcm->code = CMD_RESET;
		break;
	case NAND_CMD_READ_ID:
		fcm->code = CMD_READ_ID;
		fcm->addr_type = ADDR_ID;
		break;
	case NAND_CMD_READ_PARAMETER:
		fcm->code = CMD_READ_PAGE;
		fcm->addr_type = ADDR_ID;
		break;
	case NAND_CMD_STATUS:
		fcm->code = CMD_READ_STATUS;
		break;
	case NAND_CMD_ERASE:
		fcm->code = CMD_ERASE;
		fcm->addr_type = ADDR_ROW;
		break;
	default:
		nand_debug(NDBG_DRV, "unknown command %d\n", command);
		return (1);
	}

	return (0);
}

static int
vf_nand_send_address(device_t dev, uint8_t addr)
{
	struct vf_nand_softc *sc;
	struct fsl_nfc_fcm *fcm;

	nand_debug(NDBG_DRV,"vf_nand: send address %x", addr);
	sc = device_get_softc(dev);
	fcm = &sc->fcm;

	nand_debug(NDBG_DRV, "setting addr #%d to 0x%02x\n", fcm->addr_ptr, addr);

	if (fcm->addr_type == ADDR_ID) {
		fcm->addr_bits = addr;
	} else if (fcm->addr_type == ADDR_ROWCOL) {

		if (fcm->addr_ptr < 2)
			fcm->col_addr_bits |= (addr << (fcm->addr_ptr * 8));
		else
			fcm->row_addr_bits |= (addr << ((fcm->addr_ptr - 2) * 8));

	} else if (fcm->addr_type == ADDR_ROW)
		fcm->row_addr_bits |= (addr << (fcm->addr_ptr * 8));

	fcm->addr_ptr += 1;

	return (0);
}

static uint8_t
vf_nand_read_byte(device_t dev)
{
	struct vf_nand_softc *sc;
	struct fsl_nfc_fcm *fcm;
	uint8_t data;
	int sr1, sr2;
	int b;

	sc = device_get_softc(dev);
	fcm = &sc->fcm;

	sr1 = READ4(sc, NFC_SR1);
	sr2 = READ4(sc, NFC_SR2);

	data = 0;
	if (fcm->addr_type == ADDR_ID) {
		b = 32 - ((fcm->read_ptr + 1) * 8);
		data = (sr1 >> b) & 0xff;
		fcm->read_ptr++;
	} else if (fcm->command == NAND_CMD_STATUS) {
		data = sr2 & 0xff;
	}

	nand_debug(NDBG_DRV,"vf_nand: read %x", data);
	return (data);
}

static void
vf_nand_read_buf(device_t dev, void* buf, uint32_t len)
{
	struct vf_nand_softc *sc;
	struct fsl_nfc_fcm *fcm;
	uint16_t *tmp;
	uint8_t *b;
	int i;

	b = (uint8_t*)buf;
	sc = device_get_softc(dev);
	fcm = &sc->fcm;

	nand_debug(NDBG_DRV, "vf_nand: read_buf len %d", len);

	if (fcm->command == NAND_CMD_READ_PARAMETER) {
		tmp = malloc(len, M_DEVBUF, M_NOWAIT);
		bus_read_region_2(sc->res[0], 0x0, tmp, len);

		for (i = 0; i < len; i += 2) {
			b[i] = tmp[i+1];
			b[i+1] = tmp[i];
		}

		free(tmp, M_DEVBUF);

#ifdef NAND_DEBUG
		for (i = 0; i < len; i++) {
			if (!(i % 16))
				printf("%s", i == 0 ? "vf_nand:\n" : "\n");
			printf(" %x", b[i]);
			if (i == len - 1)
				printf("\n");
		}
#endif

	} else {

		for (i = 0; i < len; i++) {
			b[i] = READ1(sc, i);

#ifdef NAND_DEBUG
			if (!(i % 16))
				printf("%s", i == 0 ? "vf_nand:\n" : "\n");
			printf(" %x", b[i]);
			if (i == len - 1)
				printf("\n");
#endif
		}

	}
}

static void
vf_nand_write_buf(device_t dev, void* buf, uint32_t len)
{
	struct vf_nand_softc *sc;
	struct fsl_nfc_fcm *fcm;
	uint8_t *b;
	int i;

	b = (uint8_t*)buf;
	sc = device_get_softc(dev);
	fcm = &sc->fcm;

	nand_debug(NDBG_DRV,"vf_nand: write_buf len %d", len);

	for (i = 0; i < len; i++) {
		WRITE1(sc, i, b[i]);

#ifdef NAND_DEBUG
		if (!(i % 16))
			printf("%s", i == 0 ? "vf_nand:\n" : "\n");
		printf(" %x", b[i]);
		if (i == len - 1)
			printf("\n");
#endif

	}
}

static int
vf_nand_select_cs(device_t dev, uint8_t cs)
{

	if (cs > 0)
		return (ENODEV);

	return (0);
}

static int
vf_nand_read_rnb(device_t dev)
{

	/* no-op */
	return (0); /* ready */
}
