/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2011 Ivan Djelic <ivan.djelic@parrot.com>
 *
 * This file is the header for the NAND BCH ECC implementation.
 */

#ifndef __MTD_NAND_ECC_SW_BCH_H__
#define __MTD_NAND_ECC_SW_BCH_H__

#include <linux/mtd/nand.h>
#include <linux/bch.h>

/**
 * struct nand_ecc_sw_bch_conf - private software BCH ECC engine structure
 * @req_ctx: Save request context and tweak the original request to fit the
 *           engine needs
 * @code_size: Number of bytes needed to store a code (one code per step)
 * @nsteps: Number of steps
 * @calc_buf: Buffer to use when calculating ECC bytes
 * @code_buf: Buffer to use when reading (raw) ECC bytes from the chip
 * @bch: BCH control structure
 * @errloc: error location array
 * @eccmask: XOR ecc mask, allows erased pages to be decoded as valid
 */
struct nand_ecc_sw_bch_conf {
	struct nand_ecc_req_tweak_ctx req_ctx;
	unsigned int code_size;
	unsigned int nsteps;
	u8 *calc_buf;
	u8 *code_buf;
	struct bch_control *bch;
	unsigned int *errloc;
	unsigned char *eccmask;
};

#if IS_ENABLED(CONFIG_MTD_NAND_ECC_SW_BCH)

int nand_ecc_sw_bch_calculate(struct nand_device *nand,
			      const unsigned char *buf, unsigned char *code);
int nand_ecc_sw_bch_correct(struct nand_device *nand, unsigned char *buf,
			    unsigned char *read_ecc, unsigned char *calc_ecc);
int nand_ecc_sw_bch_init_ctx(struct nand_device *nand);
void nand_ecc_sw_bch_cleanup_ctx(struct nand_device *nand);
struct nand_ecc_engine *nand_ecc_sw_bch_get_engine(void);

#else /* !CONFIG_MTD_NAND_ECC_SW_BCH */

static inline int nand_ecc_sw_bch_calculate(struct nand_device *nand,
					    const unsigned char *buf,
					    unsigned char *code)
{
	return -ENOTSUPP;
}

static inline int nand_ecc_sw_bch_correct(struct nand_device *nand,
					  unsigned char *buf,
					  unsigned char *read_ecc,
					  unsigned char *calc_ecc)
{
	return -ENOTSUPP;
}

static inline int nand_ecc_sw_bch_init_ctx(struct nand_device *nand)
{
	return -ENOTSUPP;
}

static inline void nand_ecc_sw_bch_cleanup_ctx(struct nand_device *nand) {}

#endif /* CONFIG_MTD_NAND_ECC_SW_BCH */

#endif /* __MTD_NAND_ECC_SW_BCH_H__ */
