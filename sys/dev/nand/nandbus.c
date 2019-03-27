/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>

#include <dev/nand/nand.h>
#include <dev/nand/nandbus.h>
#include "nand_if.h"
#include "nandbus_if.h"
#include "nfc_if.h"

#define NAND_NCS 4

static int nandbus_probe(device_t dev);
static int nandbus_attach(device_t dev);
static int nandbus_detach(device_t dev);

static int nandbus_child_location_str(device_t, device_t, char *, size_t);
static int nandbus_child_pnpinfo_str(device_t, device_t, char *, size_t);

static int nandbus_get_status(device_t, uint8_t *);
static void nandbus_read_buffer(device_t, void *, uint32_t);
static int nandbus_select_cs(device_t, uint8_t);
static int nandbus_send_command(device_t, uint8_t);
static int nandbus_send_address(device_t, uint8_t);
static int nandbus_start_command(device_t);
static int nandbus_wait_ready(device_t, uint8_t *);
static void nandbus_write_buffer(device_t, void *, uint32_t);
static int nandbus_get_ecc(device_t, void *, uint32_t, void *, int *);
static int nandbus_correct_ecc(device_t, void *, int, void *, void *);
static void nandbus_lock(device_t);
static void nandbus_unlock(device_t);

static int nand_readid(device_t, uint8_t *, uint8_t *);
static int nand_probe_onfi(device_t, uint8_t *);
static int nand_reset(device_t);

struct nandbus_softc {
	device_t dev;
	struct cv nandbus_cv;
	struct mtx nandbus_mtx;
	uint8_t busy;
};

static device_method_t nandbus_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		nandbus_probe),
	DEVMETHOD(device_attach,	nandbus_attach),
	DEVMETHOD(device_detach,	nandbus_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),
	DEVMETHOD(bus_child_pnpinfo_str, nandbus_child_pnpinfo_str),
	DEVMETHOD(bus_child_location_str, nandbus_child_location_str),

	/* nandbus interface */
	DEVMETHOD(nandbus_get_status,	nandbus_get_status),
	DEVMETHOD(nandbus_read_buffer,	nandbus_read_buffer),
	DEVMETHOD(nandbus_select_cs,	nandbus_select_cs),
	DEVMETHOD(nandbus_send_command,	nandbus_send_command),
	DEVMETHOD(nandbus_send_address,	nandbus_send_address),
	DEVMETHOD(nandbus_start_command,nandbus_start_command),
	DEVMETHOD(nandbus_wait_ready,	nandbus_wait_ready),
	DEVMETHOD(nandbus_write_buffer,	nandbus_write_buffer),
	DEVMETHOD(nandbus_get_ecc,	nandbus_get_ecc),
	DEVMETHOD(nandbus_correct_ecc,	nandbus_correct_ecc),
	DEVMETHOD(nandbus_lock,		nandbus_lock),
	DEVMETHOD(nandbus_unlock,	nandbus_unlock),
	{ 0, 0 }
};

devclass_t nandbus_devclass;

driver_t nandbus_driver = {
	"nandbus",
	nandbus_methods,
	sizeof(struct nandbus_softc)
};

DRIVER_MODULE(nandbus, nand, nandbus_driver, nandbus_devclass, 0, 0);

int
nandbus_create(device_t nfc)
{
	device_t child;

	child = device_add_child(nfc, "nandbus", -1);
	if (!child)
		return (ENODEV);

	bus_generic_attach(nfc);

	return(0);
}

void
nandbus_destroy(device_t nfc)
{
	device_t *children;
	int nchildren, i;

	mtx_lock(&Giant);
	/* Detach & delete all children */
	if (!device_get_children(nfc, &children, &nchildren)) {
		for (i = 0; i < nchildren; i++)
			device_delete_child(nfc, children[i]);

		free(children, M_TEMP);
	}
	mtx_unlock(&Giant);
}

static int
nandbus_probe(device_t dev)
{

	device_set_desc(dev, "NAND bus");

	return (0);
}

static int
nandbus_attach(device_t dev)
{
	device_t child, nfc;
	struct nand_id chip_id;
	struct nandbus_softc *sc;
	struct nandbus_ivar *ivar;
	struct nand_softc *nfc_sc;
	struct nand_params *chip_params;
	uint8_t cs, onfi;

	sc = device_get_softc(dev);
	sc->dev = dev;

	nfc = device_get_parent(dev);
	nfc_sc = device_get_softc(nfc);

	mtx_init(&sc->nandbus_mtx, "nandbus lock", NULL, MTX_DEF);
	cv_init(&sc->nandbus_cv, "nandbus cv");

	/* Check each possible CS for existing nand devices */
	for (cs = 0; cs < NAND_NCS; cs++) {
		nand_debug(NDBG_BUS,"probe chip select %x", cs);

		/* Select & reset chip */
		if (nandbus_select_cs(dev, cs))
			break;

		if (nand_reset(dev))
			continue;

		/* Read manufacturer and device id */
		if (nand_readid(dev, &chip_id.man_id, &chip_id.dev_id))
			continue;

		if (chip_id.man_id == 0xff)
			continue;

		/*
		 * First try to get info from the table.  If that fails, see if
		 * the chip can provide ONFI info.  We check the table first to
		 * allow table entries to override info from chips that are
		 * known to provide bad ONFI data.
		 */
		onfi = 0;
		chip_params = nand_get_params(&chip_id);
		if (chip_params == NULL) {
			nand_probe_onfi(dev, &onfi);
		}

		/*
		 * At this point it appears there is a chip at this chipselect,
		 * so if we can't work with it, whine about it.
		 */
		if (chip_params == NULL && onfi == 0) {
			if (bootverbose || (nand_debug_flag & NDBG_BUS))
				printf("Chip params not found, chipsel: %d "
				    "(manuf: 0x%0x, chipid: 0x%0x, onfi: %d)\n",
				    cs, chip_id.man_id, chip_id.dev_id, onfi);
			continue;
		}

		ivar = malloc(sizeof(struct nandbus_ivar),
		    M_NAND, M_WAITOK);

		if (onfi == 1) {
			ivar->cs = cs;
			ivar->cols = 0;
			ivar->rows = 0;
			ivar->params = NULL;
			ivar->man_id = chip_id.man_id;
			ivar->dev_id = chip_id.dev_id;
			ivar->is_onfi = onfi;
			ivar->chip_cdev_name = nfc_sc->chip_cdev_name;

			child = device_add_child(dev, NULL, -1);
			device_set_ivars(child, ivar);
			continue;
		}

		ivar->cs = cs;
		ivar->cols = 1;
		ivar->rows = 2;
		ivar->params = chip_params;
		ivar->man_id = chip_id.man_id;
		ivar->dev_id = chip_id.dev_id;
		ivar->is_onfi = onfi;
		ivar->chip_cdev_name = nfc_sc->chip_cdev_name;

		/*
		 * Check what type of device we have.
		 * devices bigger than 32MiB have on more row (3)
		 */
		if (chip_params->chip_size > 32)
			ivar->rows++;
		/* Large page devices have one more col (2) */
		if (chip_params->chip_size >= 128 &&
		    chip_params->page_size > 512)
			ivar->cols++;

		child = device_add_child(dev, NULL, -1);
		device_set_ivars(child, ivar);
	}

	bus_generic_attach(dev);
	return (0);
}

static int
nandbus_detach(device_t dev)
{
	struct nandbus_softc *sc;

	sc = device_get_softc(dev);

	bus_generic_detach(dev);

	mtx_destroy(&sc->nandbus_mtx);
	cv_destroy(&sc->nandbus_cv);

	return (0);
}

static int
nandbus_child_location_str(device_t bus, device_t child, char *buf,
    size_t buflen)
{
	struct nandbus_ivar *ivar = device_get_ivars(child);

	snprintf(buf, buflen, "at cs#%d", ivar->cs);
	return (0);
}

static int
nandbus_child_pnpinfo_str(device_t bus, device_t child, char *buf,
    size_t buflen)
{
	// XXX man id, model id ????
	*buf = '\0';
	return (0);
}

static int
nand_readid(device_t bus, uint8_t *man_id, uint8_t *dev_id)
{
	device_t nfc;

	if (!bus || !man_id || !dev_id)
		return (EINVAL);

	nand_debug(NDBG_BUS,"read id");

	nfc = device_get_parent(bus);

	if (NFC_SEND_COMMAND(nfc, NAND_CMD_READ_ID)) {
		nand_debug(NDBG_BUS,"Error : could not send READ ID command");
		return (ENXIO);
	}

	if (NFC_SEND_ADDRESS(nfc, 0)) {
		nand_debug(NDBG_BUS,"Error : could not sent address to chip");
		return (ENXIO);
	}

	if (NFC_START_COMMAND(nfc) != 0) {
		nand_debug(NDBG_BUS,"Error : could not start command");
		return (ENXIO);
	}

	DELAY(25);

	*man_id = NFC_READ_BYTE(nfc);
	*dev_id = NFC_READ_BYTE(nfc);

	nand_debug(NDBG_BUS,"manufacturer id: %x chip id: %x", *man_id,
	    *dev_id);

	return (0);
}

static int
nand_probe_onfi(device_t bus, uint8_t *onfi_compliant)
{
	device_t nfc;
	char onfi_id[] = {'O', 'N', 'F', 'I', '\0'};
	int i;

	nand_debug(NDBG_BUS,"probing ONFI");

	nfc = device_get_parent(bus);

	if (NFC_SEND_COMMAND(nfc, NAND_CMD_READ_ID)) {
		nand_debug(NDBG_BUS,"Error : could not sent READ ID command");
		return (ENXIO);
	}

	if (NFC_SEND_ADDRESS(nfc, ONFI_SIG_ADDR)) {
		nand_debug(NDBG_BUS,"Error : could not sent address to chip");
		return (ENXIO);
	}

	if (NFC_START_COMMAND(nfc) != 0) {
		nand_debug(NDBG_BUS,"Error : could not start command");
		return (ENXIO);
	}
	for (i = 0; onfi_id[i] != '\0'; i++)
		if (NFC_READ_BYTE(nfc) != onfi_id[i]) {
			nand_debug(NDBG_BUS,"ONFI non-compliant");
			*onfi_compliant = 0;
			return (0);
		}

	nand_debug(NDBG_BUS,"ONFI compliant");
	*onfi_compliant = 1;

	return (0);
}

static int
nand_reset(device_t bus)
{
	device_t nfc;
	nand_debug(NDBG_BUS,"resetting...");

	nfc = device_get_parent(bus);

	if (NFC_SEND_COMMAND(nfc, NAND_CMD_RESET) != 0) {
		nand_debug(NDBG_BUS,"Error : could not sent RESET command");
		return (ENXIO);
	}

	if (NFC_START_COMMAND(nfc) != 0) {
		nand_debug(NDBG_BUS,"Error : could not start RESET command");
		return (ENXIO);
	}

	DELAY(1000);

	return (0);
}

void
nandbus_lock(device_t dev)
{
	struct nandbus_softc *sc;

	sc = device_get_softc(dev);

	mtx_lock(&sc->nandbus_mtx);
	if (sc->busy)
		cv_wait(&sc->nandbus_cv, &sc->nandbus_mtx);
	sc->busy = 1;
	mtx_unlock(&sc->nandbus_mtx);
}

void
nandbus_unlock(device_t dev)
{
	struct nandbus_softc *sc;

	sc = device_get_softc(dev);

	mtx_lock(&sc->nandbus_mtx);
	sc->busy = 0;
	cv_signal(&sc->nandbus_cv);
	mtx_unlock(&sc->nandbus_mtx);
}

int
nandbus_select_cs(device_t dev, uint8_t cs)
{

	return (NFC_SELECT_CS(device_get_parent(dev), cs));
}

int
nandbus_send_command(device_t dev, uint8_t command)
{
	int err;

	if ((err = NFC_SEND_COMMAND(device_get_parent(dev), command)))
		nand_debug(NDBG_BUS,"Err: Could not send command %x, err %x",
		    command, err);

	return (err);
}

int
nandbus_send_address(device_t dev, uint8_t address)
{
	int err;

	if ((err = NFC_SEND_ADDRESS(device_get_parent(dev), address)))
		nand_debug(NDBG_BUS,"Err: Could not send address %x, err %x",
		    address, err);

	return (err);
}

int
nandbus_start_command(device_t dev)
{
	int err;

	if ((err = NFC_START_COMMAND(device_get_parent(dev))))
		nand_debug(NDBG_BUS,"Err: Could not start command, err %x",
		    err);

	return (err);
}

void
nandbus_read_buffer(device_t dev, void *buf, uint32_t len)
{

	NFC_READ_BUF(device_get_parent(dev), buf, len);
}

void
nandbus_write_buffer(device_t dev, void *buf, uint32_t len)
{

	NFC_WRITE_BUF(device_get_parent(dev), buf, len);
}

int
nandbus_get_status(device_t dev, uint8_t *status)
{
	int err;

	if ((err = NANDBUS_SEND_COMMAND(dev, NAND_CMD_STATUS)))
		return (err);
	if ((err = NANDBUS_START_COMMAND(dev)))
		return (err);

	*status = NFC_READ_BYTE(device_get_parent(dev));

	return (0);
}

int
nandbus_wait_ready(device_t dev, uint8_t *status)
{
	struct timeval tv, tv2;

	tv2.tv_sec = 0;
	tv2.tv_usec = 50 * 5000; /* 250ms */

	getmicrotime(&tv);
	timevaladd(&tv, &tv2);

	do {
		if (NANDBUS_GET_STATUS(dev, status))
			return (ENXIO);

		if (*status & NAND_STATUS_RDY)
			return (0);

		getmicrotime(&tv2);
	} while (timevalcmp(&tv2, &tv, <=));

	return (EBUSY);
}

int
nandbus_get_ecc(device_t dev, void *buf, uint32_t pagesize, void *ecc,
    int *needwrite)
{

	return (NFC_GET_ECC(device_get_parent(dev), buf, pagesize, ecc, needwrite));
}

int
nandbus_correct_ecc(device_t dev, void *buf, int pagesize, void *readecc,
    void *calcecc)
{

	return (NFC_CORRECT_ECC(device_get_parent(dev), buf, pagesize,
	    readecc, calcecc));
}

