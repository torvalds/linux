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

/* Generic NAND driver */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/time.h>
#include <sys/malloc.h>

#include <dev/nand/nand.h>
#include <dev/nand/nandbus.h>
#include "nfc_if.h"
#include "nand_if.h"
#include "nandbus_if.h"


static int onfi_nand_probe(device_t dev);
static int large_nand_probe(device_t dev);
static int small_nand_probe(device_t dev);
static int generic_nand_attach(device_t dev);
static int generic_nand_detach(device_t dev);

static int generic_erase_block(device_t, uint32_t);
static int generic_erase_block_intlv(device_t, uint32_t);
static int generic_read_page (device_t, uint32_t, void *, uint32_t, uint32_t);
static int generic_read_oob(device_t, uint32_t, void *, uint32_t, uint32_t);
static int generic_program_page(device_t, uint32_t, void *, uint32_t, uint32_t);
static int generic_program_page_intlv(device_t, uint32_t, void *, uint32_t,
    uint32_t);
static int generic_program_oob(device_t, uint32_t, void *, uint32_t, uint32_t);
static int generic_is_blk_bad(device_t, uint32_t, uint8_t *);
static int generic_get_ecc(device_t, void *, void *, int *);
static int generic_correct_ecc(device_t, void *, void *, void *);

static int small_read_page(device_t, uint32_t, void *, uint32_t, uint32_t);
static int small_read_oob(device_t, uint32_t, void *, uint32_t, uint32_t);
static int small_program_page(device_t, uint32_t, void *, uint32_t, uint32_t);
static int small_program_oob(device_t, uint32_t, void *, uint32_t, uint32_t);

static int onfi_is_blk_bad(device_t, uint32_t, uint8_t *);
static int onfi_read_parameter(struct nand_chip *, struct onfi_chip_params *);

static int nand_send_address(device_t, int32_t, int32_t, int8_t);

static device_method_t onand_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			onfi_nand_probe),
	DEVMETHOD(device_attach,		generic_nand_attach),
	DEVMETHOD(device_detach,		generic_nand_detach),

	DEVMETHOD(nand_read_page,		generic_read_page),
	DEVMETHOD(nand_program_page,		generic_program_page),
	DEVMETHOD(nand_program_page_intlv,	generic_program_page_intlv),
	DEVMETHOD(nand_read_oob,		generic_read_oob),
	DEVMETHOD(nand_program_oob,		generic_program_oob),
	DEVMETHOD(nand_erase_block,		generic_erase_block),
	DEVMETHOD(nand_erase_block_intlv,	generic_erase_block_intlv),

	DEVMETHOD(nand_is_blk_bad,		onfi_is_blk_bad),
	DEVMETHOD(nand_get_ecc,			generic_get_ecc),
	DEVMETHOD(nand_correct_ecc,		generic_correct_ecc),
	{ 0, 0 }
};

static device_method_t lnand_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		large_nand_probe),
	DEVMETHOD(device_attach,	generic_nand_attach),
	DEVMETHOD(device_detach,	generic_nand_detach),

	DEVMETHOD(nand_read_page,	generic_read_page),
	DEVMETHOD(nand_program_page,	generic_program_page),
	DEVMETHOD(nand_read_oob,	generic_read_oob),
	DEVMETHOD(nand_program_oob,	generic_program_oob),
	DEVMETHOD(nand_erase_block,	generic_erase_block),

	DEVMETHOD(nand_is_blk_bad,	generic_is_blk_bad),
	DEVMETHOD(nand_get_ecc,		generic_get_ecc),
	DEVMETHOD(nand_correct_ecc,	generic_correct_ecc),
	{ 0, 0 }
};

static device_method_t snand_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		small_nand_probe),
	DEVMETHOD(device_attach,	generic_nand_attach),
	DEVMETHOD(device_detach,	generic_nand_detach),

	DEVMETHOD(nand_read_page,	small_read_page),
	DEVMETHOD(nand_program_page,	small_program_page),
	DEVMETHOD(nand_read_oob,	small_read_oob),
	DEVMETHOD(nand_program_oob,	small_program_oob),
	DEVMETHOD(nand_erase_block,	generic_erase_block),

	DEVMETHOD(nand_is_blk_bad,	generic_is_blk_bad),
	DEVMETHOD(nand_get_ecc,		generic_get_ecc),
	DEVMETHOD(nand_correct_ecc,	generic_correct_ecc),
	{ 0, 0 }
};

devclass_t onand_devclass;
devclass_t lnand_devclass;
devclass_t snand_devclass;

driver_t onand_driver = {
	"onand",
	onand_methods,
	sizeof(struct nand_chip)
};

driver_t lnand_driver = {
	"lnand",
	lnand_methods,
	sizeof(struct nand_chip)
};

driver_t snand_driver = {
	"snand",
	snand_methods,
	sizeof(struct nand_chip)
};

DRIVER_MODULE(onand, nandbus, onand_driver, onand_devclass, 0, 0);
DRIVER_MODULE(lnand, nandbus, lnand_driver, lnand_devclass, 0, 0);
DRIVER_MODULE(snand, nandbus, snand_driver, snand_devclass, 0, 0);

static int
onfi_nand_probe(device_t dev)
{
	struct nandbus_ivar *ivar;

	ivar = device_get_ivars(dev);
	if (ivar && ivar->is_onfi) {
		device_set_desc(dev, "ONFI compliant NAND");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENODEV);
}

static int
large_nand_probe(device_t dev)
{
	struct nandbus_ivar *ivar;

	ivar = device_get_ivars(dev);
	if (ivar && !ivar->is_onfi && ivar->params->page_size >= 512) {
		device_set_desc(dev, ivar->params->name);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENODEV);
}

static int
small_nand_probe(device_t dev)
{
	struct nandbus_ivar *ivar;

	ivar = device_get_ivars(dev);
	if (ivar && !ivar->is_onfi && ivar->params->page_size == 512) {
		device_set_desc(dev, ivar->params->name);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENODEV);
}

static int
generic_nand_attach(device_t dev)
{
	struct nand_chip *chip;
	struct nandbus_ivar *ivar;
	struct onfi_chip_params *onfi_chip_params;
	device_t nandbus, nfc;
	int err;

	chip = device_get_softc(dev);
	chip->dev = dev;

	ivar = device_get_ivars(dev);
	chip->id.man_id = ivar->man_id;
	chip->id.dev_id = ivar->dev_id;
	chip->num = ivar->cs;

	/* TODO remove when HW ECC supported */
	nandbus = device_get_parent(dev);
	nfc = device_get_parent(nandbus);

	chip->nand = device_get_softc(nfc);

	if (ivar->is_onfi) {
		onfi_chip_params = malloc(sizeof(struct onfi_chip_params),
		    M_NAND, M_WAITOK | M_ZERO);

		if (onfi_read_parameter(chip, onfi_chip_params)) {
			nand_debug(NDBG_GEN,"Could not read parameter page!\n");
			free(onfi_chip_params, M_NAND);
			return (ENXIO);
		}

		nand_onfi_set_params(chip, onfi_chip_params);
		/* Set proper column and row cycles */
		ivar->cols = (onfi_chip_params->address_cycles >> 4) & 0xf;
		ivar->rows = onfi_chip_params->address_cycles & 0xf;
		free(onfi_chip_params, M_NAND);

	} else {
		nand_set_params(chip, ivar->params);
	}

	err = nand_init_stat(chip);
	if (err) {
		generic_nand_detach(dev);
		return (err);
	}

	err = nand_init_bbt(chip);
	if (err) {
		generic_nand_detach(dev);
		return (err);
	}

	err = nand_make_dev(chip);
	if (err) {
		generic_nand_detach(dev);
		return (err);
	}

	err = create_geom_disk(chip);
	if (err) {
		generic_nand_detach(dev);
		return (err);
	}

	return (0);
}

static int
generic_nand_detach(device_t dev)
{
	struct nand_chip *chip;

	chip = device_get_softc(dev);

	nand_destroy_bbt(chip);
	destroy_geom_disk(chip);
	nand_destroy_dev(chip);
	nand_destroy_stat(chip);

	return (0);
}

static int
can_write(device_t nandbus)
{
	uint8_t status;

	if (NANDBUS_WAIT_READY(nandbus, &status))
		return (0);

	if (!(status & NAND_STATUS_WP)) {
		nand_debug(NDBG_GEN,"Chip is write-protected");
		return (0);
	}

	return (1);
}

static int
check_fail(device_t nandbus)
{
	uint8_t status;

	NANDBUS_WAIT_READY(nandbus, &status);
	if (status & NAND_STATUS_FAIL) {
		nand_debug(NDBG_GEN,"Status failed %x", status);
		return (ENXIO);
	}

	return (0);
}

static uint16_t
onfi_crc(const void *buf, size_t buflen)
{
	int i, j;
	uint16_t crc;
	const uint8_t *bufptr;

	bufptr = buf;
	crc = 0x4f4e;
	for (j = 0; j < buflen; j++) {
		crc ^= *bufptr++ << 8;
		for (i = 0; i < 8; i++)
			if (crc & 0x8000)
				crc = (crc << 1) ^ 0x8005;
			else
				crc <<= 1;
	}
       return crc;
}

static int
onfi_read_parameter(struct nand_chip *chip, struct onfi_chip_params *chip_params)
{
	device_t nandbus;
	struct onfi_params params;
	int found, sigcount, trycopy;

	nand_debug(NDBG_GEN,"read parameter");

	nandbus = device_get_parent(chip->dev);

	NANDBUS_SELECT_CS(nandbus, chip->num);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_READ_PARAMETER))
		return (ENXIO);

	if (nand_send_address(chip->dev, -1, -1, PAGE_PARAMETER_DEF))
		return (ENXIO);

	if (NANDBUS_START_COMMAND(nandbus))
		return (ENXIO);

	/*
	 * XXX Bogus DELAY, we really need a nandbus_wait_ready() here, but it's
	 * not accessible from here (static to nandbus).
	 */
	DELAY(1000);

	/*
	 * The ONFI spec mandates a minimum of three copies of the parameter
	 * data, so loop up to 3 times trying to find good data.  Each copy is
	 * validated by a signature of "ONFI" and a crc. There is a very strange
	 * rule that the signature is valid if any 2 of the 4 bytes are correct.
	 */
	for (found= 0, trycopy = 0; !found && trycopy < 3; trycopy++) {
		NANDBUS_READ_BUFFER(nandbus, &params, sizeof(struct onfi_params));
		sigcount  = params.signature[0] == 'O';
		sigcount += params.signature[1] == 'N';
		sigcount += params.signature[2] == 'F';
		sigcount += params.signature[3] == 'I';
		if (sigcount < 2)
			continue;
		if (onfi_crc(&params, 254) != params.crc)
			continue;
		found = 1;
	}
	if (!found)
		return (ENXIO);

	chip_params->luns = params.luns;
	chip_params->blocks_per_lun = le32dec(&params.blocks_per_lun);
	chip_params->pages_per_block = le32dec(&params.pages_per_block);
	chip_params->bytes_per_page = le32dec(&params.bytes_per_page);
	chip_params->spare_bytes_per_page = le16dec(&params.spare_bytes_per_page);
	chip_params->t_bers = le16dec(&params.t_bers);
	chip_params->t_prog = le16dec(&params.t_prog);
	chip_params->t_r = le16dec(&params.t_r);
	chip_params->t_ccs = le16dec(&params.t_ccs);
	chip_params->features = le16dec(&params.features);
	chip_params->address_cycles = params.address_cycles;

	return (0);
}

static int
send_read_page(device_t nand, uint8_t start_command, uint8_t end_command,
    uint32_t row, uint32_t column)
{
	device_t nandbus = device_get_parent(nand);

	if (NANDBUS_SEND_COMMAND(nandbus, start_command))
		return (ENXIO);

	if (nand_send_address(nand, row, column, -1))
		return (ENXIO);

	if (NANDBUS_SEND_COMMAND(nandbus, end_command))
		return (ENXIO);

	if (NANDBUS_START_COMMAND(nandbus))
		return (ENXIO);

	return (0);
}

static int
generic_read_page(device_t nand, uint32_t page, void *buf, uint32_t len,
    uint32_t offset)
{
	struct nand_chip *chip;
	struct page_stat *pg_stat;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"%p raw read page %x[%x] at %x", nand, page, len, offset);
	chip = device_get_softc(nand);
	nandbus = device_get_parent(nand);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (send_read_page(nand, NAND_CMD_READ, NAND_CMD_READ_END, row,
	    offset))
		return (ENXIO);

	DELAY(chip->t_r);

	NANDBUS_READ_BUFFER(nandbus, buf, len);

	if (check_fail(nandbus))
		return (ENXIO);

	pg_stat = &(chip->pg_stat[page]);
	pg_stat->page_raw_read++;

	return (0);
}

static int
generic_read_oob(device_t nand, uint32_t page, void* buf, uint32_t len,
    uint32_t offset)
{
	struct nand_chip *chip;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"%p raw read oob %x[%x] at %x", nand, page, len, offset);
	chip = device_get_softc(nand);
	nandbus = device_get_parent(nand);

	if (nand_check_page_boundary(chip, page)) {
		nand_debug(NDBG_GEN,"page boundary check failed: %08x\n", page);
		return (ENXIO);
	}

	page_to_row(&chip->chip_geom, page, &row);

	offset += chip->chip_geom.page_size;

	if (send_read_page(nand, NAND_CMD_READ, NAND_CMD_READ_END, row,
	    offset))
		return (ENXIO);

	DELAY(chip->t_r);

	NANDBUS_READ_BUFFER(nandbus, buf, len);

	if (check_fail(nandbus))
		return (ENXIO);

	return (0);
}

static int
send_start_program_page(device_t nand, uint32_t row, uint32_t column)
{
	device_t nandbus = device_get_parent(nand);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_PROG))
		return (ENXIO);

	if (nand_send_address(nand, row, column, -1))
		return (ENXIO);

	return (0);
}

static int
send_end_program_page(device_t nandbus, uint8_t end_command)
{

	if (NANDBUS_SEND_COMMAND(nandbus, end_command))
		return (ENXIO);

	if (NANDBUS_START_COMMAND(nandbus))
		return (ENXIO);

	return (0);
}

static int
generic_program_page(device_t nand, uint32_t page, void *buf, uint32_t len,
    uint32_t offset)
{
	struct nand_chip *chip;
	struct page_stat *pg_stat;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"%p raw prog page %x[%x] at %x", nand, page, len,
	    offset);
	chip = device_get_softc(nand);
	nandbus = device_get_parent(nand);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (!can_write(nandbus))
		return (ENXIO);

	if (send_start_program_page(nand, row, offset))
		return (ENXIO);

	NANDBUS_WRITE_BUFFER(nandbus, buf, len);

	if (send_end_program_page(nandbus, NAND_CMD_PROG_END))
		return (ENXIO);

	DELAY(chip->t_prog);

	if (check_fail(nandbus))
		return (ENXIO);

	pg_stat = &(chip->pg_stat[page]);
	pg_stat->page_raw_written++;

	return (0);
}

static int
generic_program_page_intlv(device_t nand, uint32_t page, void *buf,
    uint32_t len, uint32_t offset)
{
	struct nand_chip *chip;
	struct page_stat *pg_stat;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"%p raw prog page %x[%x] at %x", nand, page, len, offset);
	chip = device_get_softc(nand);
	nandbus = device_get_parent(nand);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (!can_write(nandbus))
		return (ENXIO);

	if (send_start_program_page(nand, row, offset))
		return (ENXIO);

	NANDBUS_WRITE_BUFFER(nandbus, buf, len);

	if (send_end_program_page(nandbus, NAND_CMD_PROG_INTLV))
		return (ENXIO);

	DELAY(chip->t_prog);

	if (check_fail(nandbus))
		return (ENXIO);

	pg_stat = &(chip->pg_stat[page]);
	pg_stat->page_raw_written++;

	return (0);
}

static int
generic_program_oob(device_t nand, uint32_t page, void* buf, uint32_t len,
    uint32_t offset)
{
	struct nand_chip *chip;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"%p raw prog oob %x[%x] at %x", nand, page, len,
	    offset);
	chip = device_get_softc(nand);
	nandbus = device_get_parent(nand);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);
	offset += chip->chip_geom.page_size;

	if (!can_write(nandbus))
		return (ENXIO);

	if (send_start_program_page(nand, row, offset))
		return (ENXIO);

	NANDBUS_WRITE_BUFFER(nandbus, buf, len);

	if (send_end_program_page(nandbus, NAND_CMD_PROG_END))
		return (ENXIO);

	DELAY(chip->t_prog);

	if (check_fail(nandbus))
		return (ENXIO);

	return (0);
}

static int
send_erase_block(device_t nand, uint32_t row, uint8_t second_command)
{
	device_t nandbus = device_get_parent(nand);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_ERASE))
		return (ENXIO);

	if (nand_send_address(nand, row, -1, -1))
		return (ENXIO);

	if (NANDBUS_SEND_COMMAND(nandbus, second_command))
		return (ENXIO);

	if (NANDBUS_START_COMMAND(nandbus))
		return (ENXIO);

	return (0);
}

static int
generic_erase_block(device_t nand, uint32_t block)
{
	struct block_stat *blk_stat;
	struct nand_chip *chip;
	device_t nandbus;
	int row;

	nand_debug(NDBG_GEN,"%p erase block  %x", nand, block);
	nandbus = device_get_parent(nand);
	chip = device_get_softc(nand);

	if (block >= (chip->chip_geom.blks_per_lun * chip->chip_geom.luns))
		return (ENXIO);

	row = (block << chip->chip_geom.blk_shift) &
	    chip->chip_geom.blk_mask;

	nand_debug(NDBG_GEN,"%p erase block  row %x", nand, row);

	if (!can_write(nandbus))
		return (ENXIO);

	send_erase_block(nand, row, NAND_CMD_ERASE_END);

	DELAY(chip->t_bers);

	if (check_fail(nandbus))
		return (ENXIO);

	blk_stat = &(chip->blk_stat[block]);
	blk_stat->block_erased++;

	return (0);
}

static int
generic_erase_block_intlv(device_t nand, uint32_t block)
{
	struct block_stat *blk_stat;
	struct nand_chip *chip;
	device_t nandbus;
	int row;

	nand_debug(NDBG_GEN,"%p erase block  %x", nand, block);
	nandbus = device_get_parent(nand);
	chip = device_get_softc(nand);

	if (block >= (chip->chip_geom.blks_per_lun * chip->chip_geom.luns))
		return (ENXIO);

	row = (block << chip->chip_geom.blk_shift) &
	    chip->chip_geom.blk_mask;

	if (!can_write(nandbus))
		return (ENXIO);

	send_erase_block(nand, row, NAND_CMD_ERASE_INTLV);

	DELAY(chip->t_bers);

	if (check_fail(nandbus))
		return (ENXIO);

	blk_stat = &(chip->blk_stat[block]);
	blk_stat->block_erased++;

	return (0);

}

static int
onfi_is_blk_bad(device_t device, uint32_t block_number, uint8_t *bad)
{
	struct nand_chip *chip;
	int page_number, i, j, err;
	uint8_t *oob;

	chip = device_get_softc(device);

	oob = malloc(chip->chip_geom.oob_size, M_NAND, M_WAITOK);

	page_number = block_number * chip->chip_geom.pgs_per_blk;
	*bad = 0;
	/* Check OOB of first and last page */
	for (i = 0; i < 2; i++, page_number+= chip->chip_geom.pgs_per_blk - 1) {
		err = generic_read_oob(device, page_number, oob,
		    chip->chip_geom.oob_size, 0);
		if (err) {
			device_printf(device, "%s: cannot allocate oob\n",
			    __func__);
			free(oob, M_NAND);
			return (ENOMEM);
		}

		for (j = 0; j < chip->chip_geom.oob_size; j++) {
			if (!oob[j]) {
				*bad = 1;
				free(oob, M_NAND);
				return (0);
			}
		}
	}

	free(oob, M_NAND);

	return (0);
}

static int
send_small_read_page(device_t nand, uint8_t start_command,
    uint32_t row, uint32_t column)
{
	device_t nandbus = device_get_parent(nand);

	if (NANDBUS_SEND_COMMAND(nandbus, start_command))
		return (ENXIO);

	if (nand_send_address(nand, row, column, -1))
		return (ENXIO);

	if (NANDBUS_START_COMMAND(nandbus))
		return (ENXIO);

	return (0);
}


static int
small_read_page(device_t nand, uint32_t page, void *buf, uint32_t len,
    uint32_t offset)
{
	struct nand_chip *chip;
	struct page_stat *pg_stat;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"%p small read page %x[%x] at %x", nand, page, len, offset);
	chip = device_get_softc(nand);
	nandbus = device_get_parent(nand);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (offset < 256) {
		if (send_small_read_page(nand, NAND_CMD_SMALLA, row, offset))
			return (ENXIO);
	} else {
		offset -= 256;
		if (send_small_read_page(nandbus, NAND_CMD_SMALLB, row, offset))
			return (ENXIO);
	}

	DELAY(chip->t_r);

	NANDBUS_READ_BUFFER(nandbus, buf, len);

	if (check_fail(nandbus))
		return (ENXIO);

	pg_stat = &(chip->pg_stat[page]);
	pg_stat->page_raw_read++;

	return (0);
}

static int
small_read_oob(device_t nand, uint32_t page, void *buf, uint32_t len,
    uint32_t offset)
{
	struct nand_chip *chip;
	struct page_stat *pg_stat;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"%p small read oob %x[%x] at %x", nand, page, len, offset);
	chip = device_get_softc(nand);
	nandbus = device_get_parent(nand);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (send_small_read_page(nand, NAND_CMD_SMALLOOB, row, 0))
		return (ENXIO);

	DELAY(chip->t_r);

	NANDBUS_READ_BUFFER(nandbus, buf, len);

	if (check_fail(nandbus))
		return (ENXIO);

	pg_stat = &(chip->pg_stat[page]);
	pg_stat->page_raw_read++;

	return (0);
}

static int
small_program_page(device_t nand, uint32_t page, void* buf, uint32_t len,
    uint32_t offset)
{
	struct nand_chip *chip;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"%p small prog page %x[%x] at %x", nand, page, len, offset);
	chip = device_get_softc(nand);
	nandbus = device_get_parent(nand);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (!can_write(nandbus))
		return (ENXIO);

	if (offset < 256) {
		if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_SMALLA))
			return (ENXIO);
	} else {
		if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_SMALLB))
			return (ENXIO);
	}

	if (send_start_program_page(nand, row, offset))
		return (ENXIO);

	NANDBUS_WRITE_BUFFER(nandbus, buf, len);

	if (send_end_program_page(nandbus, NAND_CMD_PROG_END))
		return (ENXIO);

	DELAY(chip->t_prog);

	if (check_fail(nandbus))
		return (ENXIO);

	return (0);
}

static int
small_program_oob(device_t nand, uint32_t page, void* buf, uint32_t len,
    uint32_t offset)
{
	struct nand_chip *chip;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"%p small prog oob %x[%x] at %x", nand, page, len, offset);
	chip = device_get_softc(nand);
	nandbus = device_get_parent(nand);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (!can_write(nandbus))
		return (ENXIO);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_SMALLOOB))
		return (ENXIO);

	if (send_start_program_page(nand, row, offset))
		return (ENXIO);

	NANDBUS_WRITE_BUFFER(nandbus, buf, len);

	if (send_end_program_page(nandbus, NAND_CMD_PROG_END))
		return (ENXIO);

	DELAY(chip->t_prog);

	if (check_fail(nandbus))
		return (ENXIO);

	return (0);
}

int
nand_send_address(device_t nand, int32_t row, int32_t col, int8_t id)
{
	struct nandbus_ivar *ivar;
	device_t nandbus;
	uint8_t addr;
	int err = 0;
	int i;

	nandbus = device_get_parent(nand);
	ivar = device_get_ivars(nand);

	if (id != -1) {
		nand_debug(NDBG_GEN,"send_address: send id %02x", id);
		err = NANDBUS_SEND_ADDRESS(nandbus, id);
	}

	if (!err && col != -1) {
		for (i = 0; i < ivar->cols; i++, col >>= 8) {
			addr = (uint8_t)(col & 0xff);
			nand_debug(NDBG_GEN,"send_address: send address column "
			    "%02x", addr);
			err = NANDBUS_SEND_ADDRESS(nandbus, addr);
			if (err)
				break;
		}
	}

	if (!err && row != -1) {
		for (i = 0; i < ivar->rows; i++, row >>= 8) {
			addr = (uint8_t)(row & 0xff);
			nand_debug(NDBG_GEN,"send_address: send address row "
			    "%02x", addr);
			err = NANDBUS_SEND_ADDRESS(nandbus, addr);
			if (err)
				break;
		}
	}

	return (err);
}

static int
generic_is_blk_bad(device_t dev, uint32_t block, uint8_t *bad)
{
	struct nand_chip *chip;
	int page_number, err, i;
	uint8_t *oob;

	chip = device_get_softc(dev);

	oob = malloc(chip->chip_geom.oob_size, M_NAND, M_WAITOK);

	page_number = block * chip->chip_geom.pgs_per_blk;
	*bad = 0;

	/* Check OOB of first and second page */
	for (i = 0; i < 2; i++) {
		err = NAND_READ_OOB(dev, page_number + i, oob,
		    chip->chip_geom.oob_size, 0);
		if (err) {
			device_printf(dev, "%s: cannot allocate OOB\n",
			    __func__);
			free(oob, M_NAND);
			return (ENOMEM);
		}

		if (!oob[0]) {
			*bad = 1;
			free(oob, M_NAND);
			return (0);
		}
	}

	free(oob, M_NAND);

	return (0);
}

static int
generic_get_ecc(device_t dev, void *buf, void *ecc, int *needwrite)
{
	struct nand_chip *chip = device_get_softc(dev);
	struct chip_geom *cg = &chip->chip_geom;

	return (NANDBUS_GET_ECC(device_get_parent(dev), buf, cg->page_size,
	    ecc, needwrite));
}

static int
generic_correct_ecc(device_t dev, void *buf, void *readecc, void *calcecc)
{
	struct nand_chip *chip = device_get_softc(dev);
	struct chip_geom *cg = &chip->chip_geom;

	return (NANDBUS_CORRECT_ECC(device_get_parent(dev), buf,
	    cg->page_size, readecc, calcecc));
}


#if 0
int
nand_chng_read_col(device_t nand, uint32_t col, void *buf, size_t len)
{
	struct nand_chip *chip;
	device_t nandbus;

	chip = device_get_softc(nand);
	nandbus = device_get_parent(nand);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_CHNG_READ_COL))
		return (ENXIO);

	if (NANDBUS_SEND_ADDRESS(nandbus, -1, col, -1))
		return (ENXIO);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_CHNG_READ_COL_END))
		return (ENXIO);

	if (NANDBUS_START_COMMAND(nandbus))
		return (ENXIO);

	if (buf != NULL && len > 0)
		NANDBUS_READ_BUFFER(nandbus, buf, len);

	return (0);
}

int
nand_chng_write_col(device_t dev, uint32_t col, void *buf,
    size_t len)
{
	struct nand_chip *chip;
	device_t nandbus;

	chip = device_get_softc(dev);
	nandbus = device_get_parent(dev);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_CHNG_WRITE_COL))
		return (ENXIO);

	if (NANDBUS_SEND_ADDRESS(nandbus, -1, col, -1))
		return (ENXIO);

	if (buf != NULL && len > 0)
		NANDBUS_WRITE_BUFFER(nandbus, buf, len);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_CHNG_READ_COL_END))
		return (ENXIO);

	if (NANDBUS_START_COMMAND(nandbus))
		return (ENXIO);

	return (0);
}

int
nand_copyback_read(device_t dev, uint32_t page, uint32_t col,
    void *buf, size_t len)
{
	struct nand_chip *chip;
	struct page_stat *pg_stat;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN," raw read page %x[%x] at %x", page, col, len);
	chip = device_get_softc(dev);
	nandbus = device_get_parent(dev);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (send_read_page(nand, NAND_CMD_READ, NAND_CMD_READ_CPBK, row, 0))
		return (ENXIO);

	DELAY(chip->t_r);
	if (check_fail(nandbus))
		return (ENXIO);

	if (buf != NULL && len > 0)
		NANDBUS_READ_BUFFER(nandbus, buf, len);

	pg_stat = &(chip->pg_stat[page]);
	pg_stat->page_raw_read++;

	return (0);
}

int
nand_copyback_prog(device_t dev, uint32_t page, uint32_t col,
    void *buf, size_t len)
{
	struct nand_chip *chip;
	struct page_stat *pg_stat;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"copyback prog page %x[%x]",  page, len);
	chip = device_get_softc(dev);
	nandbus = device_get_parent(dev);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (!can_write(nandbus))
		return (ENXIO);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_CHNG_WRITE_COL))
		return (ENXIO);

	if (NANDBUS_SEND_ADDRESS(nandbus, row, col, -1))
		return (ENXIO);

	if (buf != NULL && len > 0)
		NANDBUS_WRITE_BUFFER(nandbus, buf, len);

	if (send_end_program_page(nandbus, NAND_CMD_PROG_END))
		return (ENXIO);

	DELAY(chip->t_prog);

	if (check_fail(nandbus))
		return (ENXIO);

	pg_stat = &(chip->pg_stat[page]);
	pg_stat->page_raw_written++;

	return (0);
}

int
nand_copyback_prog_intlv(device_t dev, uint32_t page)
{
	struct nand_chip *chip;
	struct page_stat *pg_stat;
	device_t nandbus;
	uint32_t row;

	nand_debug(NDBG_GEN,"cache prog page %x", page);
	chip = device_get_softc(dev);
	nandbus = device_get_parent(dev);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (!can_write(nandbus))
		return (ENXIO);

	if (send_start_program_page(nand, row, 0))
		return (ENXIO);

	if (send_end_program_page(nandbus, NAND_CMD_PROG_INTLV))
		return (ENXIO);

	DELAY(chip->t_prog);

	if (check_fail(nandbus))
		return (ENXIO);

	pg_stat = &(chip->pg_stat[page]);
	pg_stat->page_raw_written++;

	return (0);
}

int
nand_prog_cache(device_t dev, uint32_t page, uint32_t col,
    void *buf, size_t len, uint8_t end)
{
	struct nand_chip *chip;
	struct page_stat *pg_stat;
	device_t nandbus;
	uint32_t row;
	uint8_t command;

	nand_debug(NDBG_GEN,"cache prog page %x[%x]",  page, len);
	chip = device_get_softc(dev);
	nandbus = device_get_parent(dev);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (!can_write(nandbus))
		return (ENXIO);

	if (send_start_program_page(dev, row, 0))
		return (ENXIO);

	NANDBUS_WRITE_BUFFER(nandbus, buf, len);

	if (end)
		command = NAND_CMD_PROG_END;
	else
		command = NAND_CMD_PROG_CACHE;

	if (send_end_program_page(nandbus, command))
		return (ENXIO);

	DELAY(chip->t_prog);

	if (check_fail(nandbus))
		return (ENXIO);

	pg_stat = &(chip->pg_stat[page]);
	pg_stat->page_raw_written++;

	return (0);
}

int
nand_read_cache(device_t dev, uint32_t page, uint32_t col,
    void *buf, size_t len, uint8_t end)
{
	struct nand_chip *chip;
	struct page_stat *pg_stat;
	device_t nandbus;
	uint32_t row;
	uint8_t command;

	nand_debug(NDBG_GEN,"cache read page %x[%x] ", page, len);
	chip = device_get_softc(dev);
	nandbus = device_get_parent(dev);

	if (nand_check_page_boundary(chip, page))
		return (ENXIO);

	page_to_row(&chip->chip_geom, page, &row);

	if (page != -1) {
		if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_READ))
			return (ENXIO);

		if (NANDBUS_SEND_ADDRESS(nandbus, row, col, -1))
			return (ENXIO);
	}

	if (end)
		command = NAND_CMD_READ_CACHE_END;
	else
		command = NAND_CMD_READ_CACHE;

	if (NANDBUS_SEND_COMMAND(nandbus, command))
		return (ENXIO);

	if (NANDBUS_START_COMMAND(nandbus))
		return (ENXIO);

	DELAY(chip->t_r);
	if (check_fail(nandbus))
		return (ENXIO);

	if (buf != NULL && len > 0)
		NANDBUS_READ_BUFFER(nandbus, buf, len);

	pg_stat = &(chip->pg_stat[page]);
	pg_stat->page_raw_read++;

	return (0);
}

int
nand_get_feature(device_t dev, uint8_t feat, void *buf)
{
	struct nand_chip *chip;
	device_t nandbus;

	nand_debug(NDBG_GEN,"nand get feature");

	chip = device_get_softc(dev);
	nandbus = device_get_parent(dev);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_GET_FEATURE))
		return (ENXIO);

	if (NANDBUS_SEND_ADDRESS(nandbus, -1, -1, feat))
		return (ENXIO);

	if (NANDBUS_START_COMMAND(nandbus))
		return (ENXIO);

	DELAY(chip->t_r);
	NANDBUS_READ_BUFFER(nandbus, buf, 4);

	return (0);
}

int
nand_set_feature(device_t dev, uint8_t feat, void *buf)
{
	struct nand_chip *chip;
	device_t nandbus;

	nand_debug(NDBG_GEN,"nand set feature");

	chip = device_get_softc(dev);
	nandbus = device_get_parent(dev);

	if (NANDBUS_SEND_COMMAND(nandbus, NAND_CMD_SET_FEATURE))
		return (ENXIO);

	if (NANDBUS_SEND_ADDRESS(nandbus, -1, -1, feat))
		return (ENXIO);

	NANDBUS_WRITE_BUFFER(nandbus, buf, 4);

	if (NANDBUS_START_COMMAND(nandbus))
		return (ENXIO);

	return (0);
}
#endif
