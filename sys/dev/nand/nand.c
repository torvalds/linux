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
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/sysctl.h>

#include <dev/nand/nand.h>
#include <dev/nand/nandbus.h>
#include <dev/nand/nand_ecc_pos.h>
#include "nfc_if.h"
#include "nand_if.h"
#include "nandbus_if.h"
#include <machine/stdarg.h>

#define NAND_RESET_DELAY	1000	/* tRST */
#define NAND_ERASE_DELAY	3000	/* tBERS */
#define NAND_PROG_DELAY		700	/* tPROG */
#define NAND_READ_DELAY		50	/* tR */

#define BIT0(x) ((x) & 0x1)
#define BIT1(x) (BIT0(x >> 1))
#define BIT2(x) (BIT0(x >> 2))
#define BIT3(x) (BIT0(x >> 3))
#define BIT4(x) (BIT0(x >> 4))
#define BIT5(x) (BIT0(x >> 5))
#define BIT6(x) (BIT0(x >> 6))
#define BIT7(x) (BIT0(x >> 7))

#define	SOFTECC_SIZE		256
#define	SOFTECC_BYTES		3

int nand_debug_flag = 0;
SYSCTL_INT(_debug, OID_AUTO, nand_debug, CTLFLAG_RWTUN, &nand_debug_flag, 0,
    "NAND subsystem debug flag");

MALLOC_DEFINE(M_NAND, "NAND", "NAND dynamic data");

static void calculate_ecc(const uint8_t *, uint8_t *);
static int correct_ecc(uint8_t *, uint8_t *, uint8_t *);

void
nand_debug(int level, const char *fmt, ...)
{
	va_list ap;

	if (!(nand_debug_flag & level))
		return;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

void
nand_init(struct nand_softc *nand, device_t dev, int ecc_mode,
    int ecc_bytes, int ecc_size, uint16_t *eccposition, char *cdev_name)
{

	nand->ecc.eccmode = ecc_mode;
	nand->chip_cdev_name = cdev_name;

	if (ecc_mode == NAND_ECC_SOFT) {
		nand->ecc.eccbytes = SOFTECC_BYTES;
		nand->ecc.eccsize = SOFTECC_SIZE;
	} else if (ecc_mode != NAND_ECC_NONE) {
		nand->ecc.eccbytes = ecc_bytes;
		nand->ecc.eccsize = ecc_size;
		if (eccposition)
			nand->ecc.eccpositions = eccposition;
	}
}

void
nand_onfi_set_params(struct nand_chip *chip, struct onfi_chip_params *params)
{
	struct chip_geom *cg;

	cg = &chip->chip_geom;

	init_chip_geom(cg, params->luns, params->blocks_per_lun,
	    params->pages_per_block, params->bytes_per_page,
	    params->spare_bytes_per_page);
	chip->t_bers = params->t_bers;
	chip->t_prog = params->t_prog;
	chip->t_r = params->t_r;
	chip->t_ccs = params->t_ccs;

	if (params->features & ONFI_FEAT_16BIT)
		chip->flags |= NAND_16_BIT;
}

void
nand_set_params(struct nand_chip *chip, struct nand_params *params)
{
	struct chip_geom *cg;
	uint32_t blocks_per_chip;

	cg = &chip->chip_geom;
	blocks_per_chip = (params->chip_size << 20) /
	    (params->page_size * params->pages_per_block);

	init_chip_geom(cg, 1, blocks_per_chip,
	    params->pages_per_block, params->page_size,
	    params->oob_size);

	chip->t_bers = NAND_ERASE_DELAY;
	chip->t_prog = NAND_PROG_DELAY;
	chip->t_r = NAND_READ_DELAY;
	chip->t_ccs = 0;

	if (params->flags & NAND_16_BIT)
		chip->flags |= NAND_16_BIT;
}

int
nand_init_stat(struct nand_chip *chip)
{
	struct block_stat *blk_stat;
	struct page_stat *pg_stat;
	struct chip_geom *cg;
	uint32_t blks, pgs;

	cg = &chip->chip_geom;
	blks = cg->blks_per_lun * cg->luns;
	blk_stat = malloc(sizeof(struct block_stat) * blks, M_NAND,
	    M_WAITOK | M_ZERO);
	if (!blk_stat)
		return (ENOMEM);

	pgs = blks * cg->pgs_per_blk;
	pg_stat = malloc(sizeof(struct page_stat) * pgs, M_NAND,
	    M_WAITOK | M_ZERO);
	if (!pg_stat) {
		free(blk_stat, M_NAND);
		return (ENOMEM);
	}

	chip->blk_stat = blk_stat;
	chip->pg_stat = pg_stat;

	return (0);
}

void
nand_destroy_stat(struct nand_chip *chip)
{

	free(chip->pg_stat, M_NAND);
	free(chip->blk_stat, M_NAND);
}

int
init_chip_geom(struct chip_geom *cg, uint32_t luns, uint32_t blks_per_lun,
    uint32_t pgs_per_blk, uint32_t pg_size, uint32_t oob_size)
{
	int shift;

	if (!cg)
		return (-1);

	cg->luns = luns;
	cg->blks_per_lun = blks_per_lun;
	cg->blks_per_chip = blks_per_lun * luns;
	cg->pgs_per_blk = pgs_per_blk;

	cg->page_size = pg_size;
	cg->oob_size = oob_size;
	cg->block_size = cg->page_size * cg->pgs_per_blk;
	cg->chip_size = cg->block_size * cg->blks_per_chip;

	shift = fls(cg->pgs_per_blk - 1);
	cg->pg_mask = (1 << shift) - 1;
	cg->blk_shift = shift;

	if (cg->blks_per_lun > 0) {
		shift = fls(cg->blks_per_lun - 1);
		cg->blk_mask = ((1 << shift) - 1) << cg->blk_shift;
	} else {
		shift = 0;
		cg->blk_mask = 0;
	}

	cg->lun_shift = shift + cg->blk_shift;
	shift = fls(cg->luns - 1);
	cg->lun_mask = ((1 << shift) - 1) << cg->lun_shift;

	nand_debug(NDBG_NAND, "Masks: lun 0x%x blk 0x%x page 0x%x\n"
	    "Shifts: lun %d blk %d",
	    cg->lun_mask, cg->blk_mask, cg->pg_mask,
	    cg->lun_shift, cg->blk_shift);

	return (0);
}

int
nand_row_to_blkpg(struct chip_geom *cg, uint32_t row, uint32_t *lun,
    uint32_t *blk, uint32_t *pg)
{

	if (!cg || !lun || !blk || !pg)
		return (-1);

	if (row & ~(cg->lun_mask | cg->blk_mask | cg->pg_mask)) {
		nand_debug(NDBG_NAND,"Address out of bounds\n");
		return (-1);
	}

	*lun = (row & cg->lun_mask) >> cg->lun_shift;
	*blk = (row & cg->blk_mask) >> cg->blk_shift;
	*pg = (row & cg->pg_mask);

	nand_debug(NDBG_NAND,"address %x-%x-%x\n", *lun, *blk, *pg);

	return (0);
}

int page_to_row(struct chip_geom *cg, uint32_t page, uint32_t *row)
{
	uint32_t lun, block, pg_in_blk;

	if (!cg || !row)
		return (-1);

	block = page / cg->pgs_per_blk;
	pg_in_blk = page % cg->pgs_per_blk;

	lun = block / cg->blks_per_lun;
	block = block % cg->blks_per_lun;

	*row = (lun << cg->lun_shift) & cg->lun_mask;
	*row |= ((block << cg->blk_shift) & cg->blk_mask);
	*row |= (pg_in_blk & cg->pg_mask);

	return (0);
}

int
nand_check_page_boundary(struct nand_chip *chip, uint32_t page)
{
	struct chip_geom* cg;

	cg = &chip->chip_geom;
	if (page >= (cg->pgs_per_blk * cg->blks_per_lun * cg->luns)) {
		nand_debug(NDBG_GEN,"%s: page number too big %#x\n",
		    __func__, page);
		return (1);
	}

	return (0);
}

void
nand_get_chip_param(struct nand_chip *chip, struct chip_param_io *param)
{
	struct chip_geom *cg;

	cg = &chip->chip_geom;
	param->page_size = cg->page_size;
	param->oob_size = cg->oob_size;

	param->blocks = cg->blks_per_lun * cg->luns;
	param->pages_per_block = cg->pgs_per_blk;
}

static uint16_t *
default_software_ecc_positions(struct nand_chip *chip)
{
	/* If positions have been set already, use them. */
	if (chip->nand->ecc.eccpositions)
		return (chip->nand->ecc.eccpositions);

	/*
	 * XXX Note that the following logic isn't really sufficient, especially
	 * in the ONFI case where the number of ECC bytes can be dictated by
	 * values in the parameters page, and that could lead to needing more
	 * byte positions than exist within the tables of software-ecc defaults.
	 */
	if (chip->chip_geom.oob_size >= 128)
		return (default_software_ecc_positions_128);
	if (chip->chip_geom.oob_size >= 64)
		return (default_software_ecc_positions_64);
	else if (chip->chip_geom.oob_size >= 16)
		return (default_software_ecc_positions_16);

	return (NULL);
}

static void
calculate_ecc(const uint8_t *buf, uint8_t *ecc)
{
	uint8_t p8, byte;
	int i;

	memset(ecc, 0, 3);

	for (i = 0; i < 256; i++) {
		byte = buf[i];
		ecc[0] ^= (BIT0(byte) ^ BIT2(byte) ^ BIT4(byte) ^
		    BIT6(byte)) << 2;
		ecc[0] ^= (BIT1(byte) ^ BIT3(byte) ^ BIT5(byte) ^
		    BIT7(byte)) << 3;
		ecc[0] ^= (BIT0(byte) ^ BIT1(byte) ^ BIT4(byte) ^
		    BIT5(byte)) << 4;
		ecc[0] ^= (BIT2(byte) ^ BIT3(byte) ^ BIT6(byte) ^
		    BIT7(byte)) << 5;
		ecc[0] ^= (BIT0(byte) ^ BIT1(byte) ^ BIT2(byte) ^
		    BIT3(byte)) << 6;
		ecc[0] ^= (BIT4(byte) ^ BIT5(byte) ^ BIT6(byte) ^
		    BIT7(byte)) << 7;

		p8 = BIT0(byte) ^ BIT1(byte) ^ BIT2(byte) ^
		    BIT3(byte) ^ BIT4(byte) ^ BIT5(byte) ^ BIT6(byte) ^
		    BIT7(byte);

		if (p8) {
			ecc[2] ^= (0x1 << BIT0(i));
			ecc[2] ^= (0x4 << BIT1(i));
			ecc[2] ^= (0x10 << BIT2(i));
			ecc[2] ^= (0x40 << BIT3(i));

			ecc[1] ^= (0x1 << BIT4(i));
			ecc[1] ^= (0x4 << BIT5(i));
			ecc[1] ^= (0x10 << BIT6(i));
			ecc[1] ^= (0x40 << BIT7(i));
		}
	}
	ecc[0] = ~ecc[0];
	ecc[1] = ~ecc[1];
	ecc[2] = ~ecc[2];
	ecc[0] |= 3;
}

static int
correct_ecc(uint8_t *buf, uint8_t *calc_ecc, uint8_t *read_ecc)
{
	uint8_t ecc0, ecc1, ecc2, onesnum, bit, byte;
	uint16_t addr = 0;

	ecc0 = calc_ecc[0] ^ read_ecc[0];
	ecc1 = calc_ecc[1] ^ read_ecc[1];
	ecc2 = calc_ecc[2] ^ read_ecc[2];

	if (!ecc0 && !ecc1 && !ecc2)
		return (ECC_OK);

	addr = BIT3(ecc0) | (BIT5(ecc0) << 1) | (BIT7(ecc0) << 2);
	addr |= (BIT1(ecc2) << 3) | (BIT3(ecc2) << 4) |
	    (BIT5(ecc2) << 5) |  (BIT7(ecc2) << 6);
	addr |= (BIT1(ecc1) << 7) | (BIT3(ecc1) << 8) |
	    (BIT5(ecc1) << 9) |  (BIT7(ecc1) << 10);

	onesnum = 0;
	while (ecc0 || ecc1 || ecc2) {
		if (ecc0 & 1)
			onesnum++;
		if (ecc1 & 1)
			onesnum++;
		if (ecc2 & 1)
			onesnum++;

		ecc0 >>= 1;
		ecc1 >>= 1;
		ecc2 >>= 1;
	}

	if (onesnum == 11) {
		/* Correctable error */
		bit = addr & 7;
		byte = addr >> 3;
		buf[byte] ^= (1 << bit);
		return (ECC_CORRECTABLE);
	} else if (onesnum == 1) {
		/* ECC error */
		return (ECC_ERROR_ECC);
	} else {
		/* Uncorrectable error */
		return (ECC_UNCORRECTABLE);
	}

	return (0);
}

int
nand_softecc_get(device_t dev, uint8_t *buf, int pagesize, uint8_t *ecc)
{
	int steps = pagesize / SOFTECC_SIZE;
	int i = 0, j = 0;

	for (; i < (steps * SOFTECC_BYTES);
	    i += SOFTECC_BYTES, j += SOFTECC_SIZE) {
		calculate_ecc(&buf[j], &ecc[i]);
	}

	return (0);
}

int
nand_softecc_correct(device_t dev, uint8_t *buf, int pagesize,
    uint8_t *readecc, uint8_t *calcecc)
{
	int steps = pagesize / SOFTECC_SIZE;
	int i = 0, j = 0, ret = 0;

	for (i = 0; i < (steps * SOFTECC_BYTES);
	    i += SOFTECC_BYTES, j += SOFTECC_SIZE) {
		ret += correct_ecc(&buf[j], &calcecc[i], &readecc[i]);
		if (ret < 0)
			return (ret);
	}

	return (ret);
}

static int
offset_to_page(struct chip_geom *cg, uint32_t offset)
{

	return (offset / cg->page_size);
}

int
nand_read_pages(struct nand_chip *chip, uint32_t offset, void *buf,
    uint32_t len)
{
	struct chip_geom *cg;
	struct nand_ecc_data *eccd;
	struct page_stat *pg_stat;
	device_t nandbus;
	void *oob = NULL;
	uint8_t *ptr;
	uint16_t *eccpos = NULL;
	uint32_t page, num, steps = 0;
	int i, retval = 0, needwrite;

	nand_debug(NDBG_NAND,"%p read page %x[%x]", chip, offset, len);
	cg = &chip->chip_geom;
	eccd = &chip->nand->ecc;
	page = offset_to_page(cg, offset);
	num = len / cg->page_size;

	if (eccd->eccmode != NAND_ECC_NONE) {
		steps = cg->page_size / eccd->eccsize;
		eccpos = default_software_ecc_positions(chip);
		oob = malloc(cg->oob_size, M_NAND, M_WAITOK);
	}

	nandbus = device_get_parent(chip->dev);
	NANDBUS_LOCK(nandbus);
	NANDBUS_SELECT_CS(device_get_parent(chip->dev), chip->num);

	ptr = (uint8_t *)buf;
	while (num--) {
		pg_stat = &(chip->pg_stat[page]);

		if (NAND_READ_PAGE(chip->dev, page, ptr, cg->page_size, 0)) {
			retval = ENXIO;
			break;
		}

		if (eccd->eccmode != NAND_ECC_NONE) {
			if (NAND_GET_ECC(chip->dev, ptr, eccd->ecccalculated,
			    &needwrite)) {
				retval = ENXIO;
				break;
			}
			nand_debug(NDBG_ECC,"%s: ECC calculated:",
			    __func__);
			if (nand_debug_flag & NDBG_ECC)
				for (i = 0; i < (eccd->eccbytes * steps); i++)
					printf("%x ", eccd->ecccalculated[i]);

			nand_debug(NDBG_ECC,"\n");

			if (NAND_READ_OOB(chip->dev, page, oob, cg->oob_size,
			    0)) {
				retval = ENXIO;
				break;
			}
			for (i = 0; i < (eccd->eccbytes * steps); i++)
				eccd->eccread[i] = ((uint8_t *)oob)[eccpos[i]];

			nand_debug(NDBG_ECC,"%s: ECC read:", __func__);
			if (nand_debug_flag & NDBG_ECC)
				for (i = 0; i < (eccd->eccbytes * steps); i++)
					printf("%x ", eccd->eccread[i]);
			nand_debug(NDBG_ECC,"\n");

			retval = NAND_CORRECT_ECC(chip->dev, ptr, eccd->eccread,
			    eccd->ecccalculated);

			nand_debug(NDBG_ECC, "NAND_CORRECT_ECC() returned %d",
			    retval);

			if (retval == 0)
				pg_stat->ecc_stat.ecc_succeded++;
			else if (retval > 0) {
				pg_stat->ecc_stat.ecc_corrected += retval;
				retval = ECC_CORRECTABLE;
			} else {
				pg_stat->ecc_stat.ecc_failed++;
				break;
			}
		}

		pg_stat->page_read++;
		page++;
		ptr += cg->page_size;
	}

	NANDBUS_UNLOCK(nandbus);

	if (oob)
		free(oob, M_NAND);

	return (retval);
}

int
nand_read_pages_raw(struct nand_chip *chip, uint32_t offset, void *buf,
    uint32_t len)
{
	struct chip_geom *cg;
	device_t nandbus;
	uint8_t *ptr;
	uint32_t page, num, end, begin = 0, begin_off;
	int retval = 0;

	cg = &chip->chip_geom;
	page = offset_to_page(cg, offset);
	begin_off = offset - page * cg->page_size;
	if (begin_off) {
		begin = cg->page_size - begin_off;
		len -= begin;
	}
	num = len / cg->page_size;
	end = len % cg->page_size;

	nandbus = device_get_parent(chip->dev);
	NANDBUS_LOCK(nandbus);
	NANDBUS_SELECT_CS(device_get_parent(chip->dev), chip->num);

	ptr = (uint8_t *)buf;
	if (begin_off) {
		if (NAND_READ_PAGE(chip->dev, page, ptr, begin, begin_off)) {
			NANDBUS_UNLOCK(nandbus);
			return (ENXIO);
		}

		page++;
		ptr += begin;
	}

	while (num--) {
		if (NAND_READ_PAGE(chip->dev, page, ptr, cg->page_size, 0)) {
			NANDBUS_UNLOCK(nandbus);
			return (ENXIO);
		}

		page++;
		ptr += cg->page_size;
	}

	if (end)
		if (NAND_READ_PAGE(chip->dev, page, ptr, end, 0)) {
			NANDBUS_UNLOCK(nandbus);
			return (ENXIO);
		}

	NANDBUS_UNLOCK(nandbus);

	return (retval);
}


int
nand_prog_pages(struct nand_chip *chip, uint32_t offset, uint8_t *buf,
    uint32_t len)
{
	struct chip_geom *cg;
	struct page_stat *pg_stat;
	struct nand_ecc_data *eccd;
	device_t nandbus;
	uint32_t page, num;
	uint8_t *oob = NULL;
	uint16_t *eccpos = NULL;
	int steps = 0, i, needwrite, err = 0;

	nand_debug(NDBG_NAND,"%p prog page %x[%x]", chip, offset, len);

	eccd = &chip->nand->ecc;
	cg = &chip->chip_geom;
	page = offset_to_page(cg, offset);
	num = len / cg->page_size;

	if (eccd->eccmode != NAND_ECC_NONE) {
		steps = cg->page_size / eccd->eccsize;
		oob = malloc(cg->oob_size, M_NAND, M_WAITOK);
		eccpos = default_software_ecc_positions(chip);
	}

	nandbus = device_get_parent(chip->dev);
	NANDBUS_LOCK(nandbus);
	NANDBUS_SELECT_CS(device_get_parent(chip->dev), chip->num);

	while (num--) {
		if (NAND_PROGRAM_PAGE(chip->dev, page, buf, cg->page_size, 0)) {
			err = ENXIO;
			break;
		}

		if (eccd->eccmode != NAND_ECC_NONE) {
			if (NAND_GET_ECC(chip->dev, buf, &eccd->ecccalculated,
			    &needwrite)) {
				err = ENXIO;
				break;
			}
			nand_debug(NDBG_ECC,"ECC calculated:");
			if (nand_debug_flag & NDBG_ECC)
				for (i = 0; i < (eccd->eccbytes * steps); i++)
					printf("%x ", eccd->ecccalculated[i]);

			nand_debug(NDBG_ECC,"\n");

			if (needwrite) {
				if (NAND_READ_OOB(chip->dev, page, oob, cg->oob_size,
				    0)) {
					err = ENXIO;
					break;
				}

				for (i = 0; i < (eccd->eccbytes * steps); i++)
					oob[eccpos[i]] = eccd->ecccalculated[i];

				if (NAND_PROGRAM_OOB(chip->dev, page, oob,
				    cg->oob_size, 0)) {
					err = ENXIO;
					break;
				}
			}
		}

		pg_stat = &(chip->pg_stat[page]);
		pg_stat->page_written++;

		page++;
		buf += cg->page_size;
	}

	NANDBUS_UNLOCK(nandbus);

	if (oob)
		free(oob, M_NAND);

	return (err);
}

int
nand_prog_pages_raw(struct nand_chip *chip, uint32_t offset, void *buf,
    uint32_t len)
{
	struct chip_geom *cg;
	device_t nandbus;
	uint8_t *ptr;
	uint32_t page, num, end, begin = 0, begin_off;
	int retval = 0;

	cg = &chip->chip_geom;
	page = offset_to_page(cg, offset);
	begin_off = offset - page * cg->page_size;
	if (begin_off) {
		begin = cg->page_size - begin_off;
		len -= begin;
	}
	num = len / cg->page_size;
	end = len % cg->page_size;

	nandbus = device_get_parent(chip->dev);
	NANDBUS_LOCK(nandbus);
	NANDBUS_SELECT_CS(device_get_parent(chip->dev), chip->num);

	ptr = (uint8_t *)buf;
	if (begin_off) {
		if (NAND_PROGRAM_PAGE(chip->dev, page, ptr, begin, begin_off)) {
			NANDBUS_UNLOCK(nandbus);
			return (ENXIO);
		}

		page++;
		ptr += begin;
	}

	while (num--) {
		if (NAND_PROGRAM_PAGE(chip->dev, page, ptr, cg->page_size, 0)) {
			NANDBUS_UNLOCK(nandbus);
			return (ENXIO);
		}

		page++;
		ptr += cg->page_size;
	}

	if (end)
		retval = NAND_PROGRAM_PAGE(chip->dev, page, ptr, end, 0);

	NANDBUS_UNLOCK(nandbus);

	return (retval);
}

int
nand_read_oob(struct nand_chip *chip, uint32_t page, void *buf,
    uint32_t len)
{
	device_t nandbus;
	int retval = 0;

	nandbus = device_get_parent(chip->dev);
	NANDBUS_LOCK(nandbus);
	NANDBUS_SELECT_CS(device_get_parent(chip->dev), chip->num);

	retval = NAND_READ_OOB(chip->dev, page, buf, len, 0);

	NANDBUS_UNLOCK(nandbus);

	return (retval);
}


int
nand_prog_oob(struct nand_chip *chip, uint32_t page, void *buf,
    uint32_t len)
{
	device_t nandbus;
	int retval = 0;

	nandbus = device_get_parent(chip->dev);
	NANDBUS_LOCK(nandbus);
	NANDBUS_SELECT_CS(device_get_parent(chip->dev), chip->num);

	retval = NAND_PROGRAM_OOB(chip->dev, page, buf, len, 0);

	NANDBUS_UNLOCK(nandbus);

	return (retval);
}

int
nand_erase_blocks(struct nand_chip *chip, off_t offset, size_t len)
{
	device_t nandbus;
	struct chip_geom *cg;
	uint32_t block, num_blocks;
	int err = 0;

	cg = &chip->chip_geom;
	if ((offset % cg->block_size) || (len % cg->block_size))
		return (EINVAL);

	block = offset / cg->block_size;
	num_blocks = len / cg->block_size;
	nand_debug(NDBG_NAND,"%p erase blocks %d[%d]", chip, block, num_blocks);

	nandbus = device_get_parent(chip->dev);
	NANDBUS_LOCK(nandbus);
	NANDBUS_SELECT_CS(device_get_parent(chip->dev), chip->num);

	while (num_blocks--) {
		if (!nand_check_bad_block(chip, block)) {
			if (NAND_ERASE_BLOCK(chip->dev, block)) {
				nand_debug(NDBG_NAND,"%p erase blocks %d error",
				    chip, block);
				nand_mark_bad_block(chip, block);
				err = ENXIO;
			}
		} else
			err = ENXIO;

		block++;
	}

	NANDBUS_UNLOCK(nandbus);

	if (err)
		nand_update_bbt(chip);

	return (err);
}

MODULE_VERSION(nand, 1);
