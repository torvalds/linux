/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2000-2010 Steven J. Hill <sjhill@realitydiluted.com>
 *			    David Woodhouse <dwmw2@infradead.org>
 *			    Thomas Gleixner <tglx@linutronix.de>
 *
 * This file is the header for the ECC algorithm.
 */

#ifndef __MTD_NAND_ECC_H__
#define __MTD_NAND_ECC_H__

struct nand_chip;

/*
 * Calculate 3 byte ECC code for eccsize byte block
 */
void __nand_calculate_ecc(const u_char *dat, unsigned int eccsize,
			  u_char *ecc_code, bool sm_order);

/*
 * Calculate 3 byte ECC code for 256/512 byte block
 */
int nand_calculate_ecc(struct nand_chip *chip, const u_char *dat,
		       u_char *ecc_code);

/*
 * Detect and correct a 1 bit error for eccsize byte block
 */
int __nand_correct_data(u_char *dat, u_char *read_ecc, u_char *calc_ecc,
			unsigned int eccsize, bool sm_order);

/*
 * Detect and correct a 1 bit error for 256/512 byte block
 */
int nand_correct_data(struct nand_chip *chip, u_char *dat, u_char *read_ecc,
		      u_char *calc_ecc);

#endif /* __MTD_NAND_ECC_H__ */
