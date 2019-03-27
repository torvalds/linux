/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2012 Semihalf
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/bus.h>

#include <dev/nand/nand.h>

#include "nand_if.h"

#define BBT_PRIMARY_PATTERN	0x01020304
#define BBT_SECONDARY_PATTERN	0x05060708

enum bbt_place {
	BBT_NONE,
	BBT_PRIMARY,
	BBT_SECONDARY
};

struct nand_bbt {
	struct nand_chip	*chip;
	uint32_t		primary_map;
	uint32_t		secondary_map;
	enum bbt_place		active;
	struct bbt_header	*hdr;
	uint32_t		tab_len;
	uint32_t		*table;
};

struct bbt_header {
	uint32_t pattern;
	int32_t seq_nr;
};

static int nand_bbt_save(struct nand_bbt *);
static int nand_bbt_load_hdr(struct nand_bbt *, struct bbt_header *, int8_t);
static int nand_bbt_load_table(struct nand_bbt *);
static int nand_bbt_prescan(struct nand_bbt *);

int
nand_init_bbt(struct nand_chip *chip)
{
	struct chip_geom *cg;
	struct nand_bbt *bbt;
	int err;

	cg = &chip->chip_geom;

	bbt = malloc(sizeof(struct nand_bbt), M_NAND, M_ZERO | M_WAITOK);
	if (!bbt) {
		device_printf(chip->dev,
		    "Cannot allocate memory for bad block struct");
		return (ENOMEM);
	}

	bbt->chip = chip;
	bbt->active = BBT_NONE;
	bbt->primary_map = cg->chip_size - cg->block_size;
	bbt->secondary_map = cg->chip_size - 2 * cg->block_size;
	bbt->tab_len = cg->blks_per_chip * sizeof(uint32_t);
	bbt->hdr = malloc(sizeof(struct bbt_header) + bbt->tab_len, M_NAND,
	    M_WAITOK);
	if (!bbt->hdr) {
		device_printf(chip->dev, "Cannot allocate %d bytes for BB "
		    "Table", bbt->tab_len);
		free(bbt, M_NAND);
		return (ENOMEM);
	}
	bbt->hdr->seq_nr = 0;
	bbt->table = (uint32_t *)((uint8_t *)bbt->hdr +
	    sizeof(struct bbt_header));

	err = nand_bbt_load_table(bbt);
	if (err) {
		free(bbt->table, M_NAND);
		free(bbt, M_NAND);
		return (err);
	}

	chip->bbt = bbt;
	if (bbt->active == BBT_NONE) {
		bbt->active = BBT_PRIMARY;
		memset(bbt->table, 0xff, bbt->tab_len);
		nand_bbt_prescan(bbt);
		nand_bbt_save(bbt);
	} else
		device_printf(chip->dev, "Found BBT table for chip\n");

	return (0);
}

void
nand_destroy_bbt(struct nand_chip *chip)
{

	if (chip->bbt) {
		nand_bbt_save(chip->bbt);

		free(chip->bbt->hdr, M_NAND);
		free(chip->bbt, M_NAND);
		chip->bbt = NULL;
	}
}

int
nand_update_bbt(struct nand_chip *chip)
{

	nand_bbt_save(chip->bbt);

	return (0);
}

static int
nand_bbt_save(struct nand_bbt *bbt)
{
	enum bbt_place next;
	uint32_t addr;
	int32_t err;

	if (bbt->active == BBT_PRIMARY) {
		addr = bbt->secondary_map;
		bbt->hdr->pattern = BBT_SECONDARY_PATTERN;
		next = BBT_SECONDARY;
	} else {
		addr = bbt->primary_map;
		bbt->hdr->pattern = BBT_PRIMARY_PATTERN;
		next = BBT_PRIMARY;
	}

	err = nand_erase_blocks(bbt->chip, addr,
	    bbt->chip->chip_geom.block_size);
	if (err)
		return (err);

	bbt->hdr->seq_nr++;

	err = nand_prog_pages_raw(bbt->chip, addr, bbt->hdr,
	    bbt->tab_len + sizeof(struct bbt_header));
	if (err)
		return (err);

	bbt->active = next;
	return (0);
}

static int
nand_bbt_load_hdr(struct nand_bbt *bbt, struct bbt_header *hdr, int8_t primary)
{
	uint32_t addr;

	if (primary)
		addr = bbt->primary_map;
	else
		addr = bbt->secondary_map;

	return (nand_read_pages_raw(bbt->chip, addr, hdr,
	    sizeof(struct bbt_header)));
}

static int
nand_bbt_load_table(struct nand_bbt *bbt)
{
	struct bbt_header hdr1, hdr2;
	uint32_t address = 0;
	int err = 0;

	bzero(&hdr1, sizeof(hdr1));
	bzero(&hdr2, sizeof(hdr2));

	nand_bbt_load_hdr(bbt, &hdr1, 1);
	if (hdr1.pattern == BBT_PRIMARY_PATTERN) {
		bbt->active = BBT_PRIMARY;
		address = bbt->primary_map;
	} else
		bzero(&hdr1, sizeof(hdr1));


	nand_bbt_load_hdr(bbt, &hdr2, 0);
	if ((hdr2.pattern == BBT_SECONDARY_PATTERN) &&
	    (hdr2.seq_nr > hdr1.seq_nr)) {
		bbt->active = BBT_SECONDARY;
		address = bbt->secondary_map;
	} else
		bzero(&hdr2, sizeof(hdr2));

	if (bbt->active != BBT_NONE)
		err = nand_read_pages_raw(bbt->chip, address, bbt->hdr,
		    bbt->tab_len + sizeof(struct bbt_header));

	return (err);
}

static int
nand_bbt_prescan(struct nand_bbt *bbt)
{
	int32_t i;
	uint8_t bad;
	bool printed_hash = 0;

	device_printf(bbt->chip->dev, "No BBT found. Prescan chip...\n");
	for (i = 0; i < bbt->chip->chip_geom.blks_per_chip; i++) {
		if (NAND_IS_BLK_BAD(bbt->chip->dev, i, &bad))
			return (ENXIO);

		if (bad) {
			device_printf(bbt->chip->dev, "Bad block(%d)\n", i);
			bbt->table[i] = 0x0FFFFFFF;
		}
		if (!(i % 100)) {
			printf("#");
			printed_hash = 1;
		}
	}

	if (printed_hash)
		printf("\n");

	return (0);
}

int
nand_check_bad_block(struct nand_chip *chip, uint32_t block_number)
{

	if (!chip || !chip->bbt)
		return (0);

	if ((chip->bbt->table[block_number] & 0xF0000000) == 0)
		return (1);

	return (0);
}

int
nand_mark_bad_block(struct nand_chip *chip, uint32_t block_number)
{

	chip->bbt->table[block_number] = 0x0FFFFFFF;

	return (0);
}
