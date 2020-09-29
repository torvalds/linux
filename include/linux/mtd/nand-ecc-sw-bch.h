/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2011 Ivan Djelic <ivan.djelic@parrot.com>
 *
 * This file is the header for the NAND BCH ECC implementation.
 */

#ifndef __MTD_NAND_ECC_SW_BCH_H__
#define __MTD_NAND_ECC_SW_BCH_H__

#include <linux/mtd/nand.h>

#if IS_ENABLED(CONFIG_MTD_NAND_ECC_SW_BCH)

int nand_ecc_sw_bch_calculate(struct nand_device *nand,
			      const unsigned char *buf, unsigned char *code);
int nand_ecc_sw_bch_correct(struct nand_device *nand, unsigned char *buf,
			    unsigned char *read_ecc, unsigned char *calc_ecc);
int nand_ecc_sw_bch_init(struct nand_device *nand);
void nand_ecc_sw_bch_cleanup(struct nand_device *nand);

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

static inline int nand_ecc_sw_bch_init(struct nand_device *nand)
{
	return -ENOTSUPP;
}

static inline void nand_ecc_sw_bch_cleanup(struct nand_device *nand) {}

#endif /* CONFIG_MTD_NAND_ECC_SW_BCH */

#endif /* __MTD_NAND_ECC_SW_BCH_H__ */
