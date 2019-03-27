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
 *
 * $FreeBSD$
 */

#ifndef _DEV_NAND_H_
#define _DEV_NAND_H_

#include <sys/bus.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/taskqueue.h>
#include <sys/queue.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>

#include <dev/nand/nand_dev.h>

MALLOC_DECLARE(M_NAND);

/* Read commands */
#define NAND_CMD_READ		0x00
#define NAND_CMD_CHNG_READ_COL	0x05
#define NAND_CMD_READ_END	0x30
#define NAND_CMD_READ_CACHE	0x31
#define NAND_CMD_READ_CPBK	0x35
#define NAND_CMD_READ_CACHE_END	0x3F
#define	NAND_CMD_CHNG_READ_COL_END	0xE0

/* Erase commands */
#define NAND_CMD_ERASE		0x60
#define NAND_CMD_ERASE_END	0xD0
#define NAND_CMD_ERASE_INTLV	0xD1

/* Program commands */
#define NAND_CMD_PROG		0x80
#define NAND_CMD_CHNG_WRITE_COL	0x85
#define NAND_CMD_PROG_END	0x10
#define NAND_CMD_PROG_INTLV	0x11
#define NAND_CMD_PROG_CACHE	0x15

/* Misc commands */
#define NAND_CMD_STATUS		0x70
#define NAND_CMD_STATUS_ENH	0x78
#define NAND_CMD_READ_ID	0x90
#define NAND_CMD_READ_PARAMETER	0xec
#define NAND_CMD_READ_UNIQUE_ID	0xed
#define NAND_CMD_GET_FEATURE	0xee
#define NAND_CMD_SET_FEATURE	0xef

/* Reset commands */
#define NAND_CMD_SYNCH_RESET	0xfc
#define NAND_CMD_RESET		0xff

/* Small page flash commands */
#define NAND_CMD_SMALLA		0x00
#define NAND_CMD_SMALLB		0x01
#define NAND_CMD_SMALLOOB	0x50

#define NAND_STATUS_FAIL	0x1
#define NAND_STATUS_FAILC	0x2
#define NAND_STATUS_ARDY	0x20
#define NAND_STATUS_RDY		0x40
#define NAND_STATUS_WP		0x80

#define NAND_LP_OOB_COLUMN_START	0x800
#define NAND_LP_OOBSZ			0x40
#define NAND_SP_OOB_COLUMN_START	0x200
#define NAND_SP_OOBSZ			0x10

#define PAGE_PARAM_LENGTH		0x100
#define PAGE_PARAMETER_DEF		0x0
#define PAGE_PARAMETER_RED_1		0x100
#define PAGE_PARAMETER_RED_2		0x200

#define ONFI_SIG_ADDR	0x20

#define NAND_MAX_CHIPS	0x4
#define NAND_MAX_OOBSZ	512
#define NAND_MAX_PAGESZ	16384

#define NAND_SMALL_PAGE_SIZE	0x200

#define NAND_16_BIT		0x00000001

#define NAND_ECC_NONE			0x0
#define NAND_ECC_SOFT			0x1
#define	NAND_ECC_FULLHW			0x2
#define	NAND_ECC_PARTHW			0x4
#define NAND_ECC_MODE_MASK		0x7

#define ECC_OK			0
#define ECC_CORRECTABLE		1
#define ECC_ERROR_ECC		(-1)
#define ECC_UNCORRECTABLE	(-2)

#define NAND_MAN_SAMSUNG		0xec
#define NAND_MAN_HYNIX			0xad
#define NAND_MAN_STMICRO		0x20
#define NAND_MAN_MICRON			0x2c

struct nand_id {
	uint8_t man_id;
	uint8_t dev_id;
};

struct nand_params {
	struct nand_id	id;
	char		*name;
	uint32_t	chip_size;
	uint32_t	page_size;
	uint32_t	oob_size;
	uint32_t	pages_per_block;
	uint32_t	flags;
};

/* nand debug levels */
#define NDBG_NAND	0x01
#define NDBG_CDEV	0x02
#define NDBG_GEN	0x04
#define NDBG_GEOM	0x08
#define NDBG_BUS	0x10
#define NDBG_SIM	0x20
#define NDBG_CTRL	0x40
#define NDBG_DRV	0x80
#define NDBG_ECC	0x100

/* nand_debug_function */
void nand_debug(int level, const char *fmt, ...);
extern int nand_debug_flag;

/* ONFI features bit*/
#define ONFI_FEAT_16BIT		0x01
#define ONFI_FEAT_MULT_LUN	0x02
#define ONFI_FEAT_INTLV_OPS	0x04
#define ONFI_FEAT_CPBK_RESTRICT	0x08
#define ONFI_FEAT_SRC_SYNCH	0x10

/* ONFI optional commands bits */
#define ONFI_OPTCOM_PROG_CACHE	0x01
#define ONFI_OPTCOM_READ_CACHE	0x02
#define ONFI_OPTCOM_GETSET_FEAT	0x04
#define ONFI_OPTCOM_STATUS_ENH	0x08
#define ONFI_OPTCOM_COPYBACK	0x10
#define ONFI_OPTCOM_UNIQUE_ID	0x20


/* Layout of parameter page is defined in ONFI */
struct onfi_params {
	char		signature[4];
	uint16_t	rev;
	uint16_t	features;
	uint16_t	optional_commands;
	uint8_t		primary_advanced_command;
	uint8_t		res1;
	uint16_t	extended_parameter_page_length;
	uint8_t		parameter_page_count;
	uint8_t		res2[17];
	char		manufacturer_name[12];
	char		device_model[20];
	uint8_t		manufacturer_id;
	uint8_t		manufacture_date_yy;
	uint8_t		manufacture_date_ww;
	uint8_t		res3[13];
	uint32_t	bytes_per_page;
	uint16_t	spare_bytes_per_page;
	uint32_t	bytes_per_partial_page;
	uint16_t	spare_bytes_per_partial_page;
	uint32_t	pages_per_block;
	uint32_t	blocks_per_lun;
	uint8_t		luns;
	uint8_t		address_cycles;
	uint8_t		bits_per_cell;
	uint16_t	max_bad_block_per_lun;
	uint16_t	block_endurance;
	uint8_t		guaranteed_valid_blocks;
	uint16_t	valid_block_endurance;
	uint8_t		programs_per_page;
	uint8_t		partial_prog_attr;
	uint8_t		bits_of_ecc;
	uint8_t		interleaved_addr_bits;
	uint8_t		interleaved_oper_attr;
	uint8_t		eznand_support;
	uint8_t		res4[12];
	uint8_t		pin_capacitance;
	uint16_t	asynch_timing_mode_support;
	uint16_t	asynch_prog_cache_timing_mode_support;
	uint16_t	t_prog;	/* us, max page program time */
	uint16_t	t_bers;	/* us, max block erase time */
	uint16_t	t_r;	/* us, max page read time */
	uint16_t	t_ccs;	/* ns, min change column setup time */
	uint16_t	source_synch_timing_mode_support;
	uint8_t		source_synch_feat;
	uint16_t	clk_input_capacitance;
	uint16_t	io_capacitance;
	uint16_t	input_capacitance;
	uint8_t		input_capacitance_max;
	uint8_t		driver_strength_support;
	uint16_t	t_r_interleaved;
	uint16_t	t_adl;
	uint16_t	t_r_eznand;
	uint8_t		nv_ddr2_features;
	uint8_t		nv_ddr2_warmup_cycles;
	uint8_t		res5[4];
	uint16_t	vendor_rev;
	uint8_t		vendor_spec[88];
	uint16_t	crc;
}__attribute__((packed));
CTASSERT(sizeof(struct onfi_params) == 256);

struct onfi_chip_params {
	uint32_t blocks_per_lun;
	uint32_t pages_per_block;
	uint32_t bytes_per_page;
	uint32_t spare_bytes_per_page;
	uint16_t t_bers;
	uint16_t t_prog;
	uint16_t t_r;
	uint16_t t_ccs;
	uint16_t features;
	uint8_t address_cycles;
	uint8_t luns;
};

struct nand_ecc_data {
	int	eccsize;		/* Number of data bytes per ECC step */
	int	eccmode;
	int	eccbytes;		/* Number of ECC bytes per step */

	uint16_t	*eccpositions;		/* Positions of ecc bytes */
	uint8_t	ecccalculated[NAND_MAX_OOBSZ];
	uint8_t	eccread[NAND_MAX_OOBSZ];
};

struct ecc_stat {
	uint32_t ecc_succeded;
	uint32_t ecc_corrected;
	uint32_t ecc_failed;
};

struct page_stat {
	struct ecc_stat	ecc_stat;
	uint32_t	page_read;
	uint32_t	page_raw_read;
	uint32_t	page_written;
	uint32_t	page_raw_written;
};

struct block_stat {
	uint32_t block_erased;
};

struct chip_geom {
	uint32_t	chip_size;
	uint32_t	block_size;
	uint32_t	page_size;
	uint32_t	oob_size;

	uint32_t	luns;
	uint32_t	blks_per_lun;
	uint32_t	blks_per_chip;
	uint32_t	pgs_per_blk;

	uint32_t	pg_mask;
	uint32_t	blk_mask;
	uint32_t	lun_mask;
	uint8_t		blk_shift;
	uint8_t		lun_shift;
};

struct nand_chip {
	device_t		dev;
	struct nand_id		id;
	struct chip_geom	chip_geom;

	uint16_t		t_prog;	/* us, max page program time */
	uint16_t		t_bers;	/* us, max block erase time */
	uint16_t		t_r;	/* us, max page read time */
	uint16_t		t_ccs;	/* ns, min change column setup time */
	uint8_t			num;
	uint8_t			flags;

	struct page_stat	*pg_stat;
	struct block_stat	*blk_stat;
	struct nand_softc	*nand;
	struct nand_bbt		*bbt;
	struct nand_ops		*ops;
	struct cdev		*cdev;

	struct disk		*ndisk;
	struct disk		*rdisk;
	struct bio_queue_head	bioq;	/* bio queue */
	struct mtx		qlock;	/* bioq lock */
	struct taskqueue	*tq;	/* private task queue for i/o request */
	struct task		iotask;	/* i/o processing */

};

struct nand_softc {
	uint8_t			flags;

	char			*chip_cdev_name;
	struct nand_ecc_data	ecc;
};

/* NAND ops */
int nand_erase_blocks(struct nand_chip *chip, off_t offset, size_t len);
int nand_prog_pages(struct nand_chip *chip, uint32_t offset, uint8_t *buf,
    uint32_t len);
int nand_read_pages(struct nand_chip *chip, uint32_t offset, void *buf,
    uint32_t len);
int nand_read_pages_raw(struct nand_chip *chip, uint32_t offset, void *buf,
    uint32_t len);
int nand_prog_pages_raw(struct nand_chip *chip, uint32_t offset, void *buf,
    uint32_t len);
int nand_read_oob(struct nand_chip *chip, uint32_t page, void *buf,
    uint32_t len);
int nand_prog_oob(struct nand_chip *chip, uint32_t page, void *buf,
    uint32_t len);

int nand_select_cs(device_t dev, uint8_t cs);

int nand_read_parameter(struct nand_softc *nand, struct onfi_params *param);
int nand_synch_reset(struct nand_softc *nand);
int nand_chng_read_col(device_t dev, uint32_t col, void *buf, size_t len);
int nand_chng_write_col(device_t dev, uint32_t col, void *buf, size_t len);
int nand_get_feature(device_t dev, uint8_t feat, void* buf);
int nand_set_feature(device_t dev, uint8_t feat, void* buf);


int nand_erase_block_intlv(device_t dev, uint32_t block);
int nand_copyback_read(device_t dev, uint32_t page, uint32_t col,
    void *buf, size_t len);
int nand_copyback_prog(device_t dev, uint32_t page, uint32_t col,
    void *buf, size_t len);
int nand_copyback_prog_intlv(device_t dev, uint32_t page);
int nand_prog_cache(device_t dev, uint32_t page, uint32_t col,
    void *buf, size_t len, uint8_t end);
int nand_prog_intlv(device_t dev, uint32_t page, uint32_t col,
    void *buf, size_t len);
int nand_read_cache(device_t dev, uint32_t page, uint32_t col,
    void *buf, size_t len, uint8_t end);

int nand_write_ecc(struct nand_softc *nand, uint32_t page, uint8_t *data);
int nand_read_ecc(struct nand_softc *nand, uint32_t page, uint8_t *data);

int nand_softecc_get(device_t dev, uint8_t *buf, int pagesize, uint8_t *ecc);
int nand_softecc_correct(device_t dev, uint8_t *buf, int pagesize,
    uint8_t *readecc, uint8_t *calcecc);

/* Chip initialization */
void nand_init(struct nand_softc *nand, device_t dev, int ecc_mode,
    int ecc_bytes, int ecc_size, uint16_t* eccposition, char* cdev_name);
void nand_detach(struct nand_softc *nand);
struct nand_params *nand_get_params(struct nand_id *id);

void nand_onfi_set_params(struct nand_chip *chip, struct onfi_chip_params *params);
void nand_set_params(struct nand_chip *chip, struct nand_params *params);
int  nand_init_stat(struct nand_chip *chip);
void nand_destroy_stat(struct nand_chip *chip);

/* BBT */
int nand_init_bbt(struct nand_chip *chip);
void nand_destroy_bbt(struct nand_chip *chip);
int nand_update_bbt(struct nand_chip *chip);
int nand_mark_bad_block(struct nand_chip* chip, uint32_t block_num);
int nand_check_bad_block(struct nand_chip* chip, uint32_t block_num);

/* cdev creation/removal */
int  nand_make_dev(struct nand_chip* chip);
void nand_destroy_dev(struct nand_chip *chip);

int  create_geom_disk(struct nand_chip* chip);
int  create_geom_raw_disk(struct nand_chip *chip);
void destroy_geom_disk(struct nand_chip *chip);
void destroy_geom_raw_disk(struct nand_chip *chip);

int init_chip_geom(struct chip_geom* cg, uint32_t luns, uint32_t blks_per_lun,
    uint32_t pgs_per_blk, uint32_t pg_size, uint32_t oob_size);
int nand_row_to_blkpg(struct chip_geom *cg, uint32_t row, uint32_t *lun,
    uint32_t *blk, uint32_t *pg);
int page_to_row(struct chip_geom *cg, uint32_t page, uint32_t *row);
int nand_check_page_boundary(struct nand_chip *chip, uint32_t page);
void nand_get_chip_param(struct nand_chip *chip, struct chip_param_io *param);

#endif /* _DEV_NAND_H_ */
