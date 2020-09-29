/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2000-2010 Steven J. Hill <sjhill@realitydiluted.com>
 *			    David Woodhouse <dwmw2@infradead.org>
 *			    Thomas Gleixner <tglx@linutronix.de>
 *
 * This file is the header for the NAND Hamming ECC implementation.
 */

#ifndef __MTD_NAND_ECC_SW_HAMMING_H__
#define __MTD_NAND_ECC_SW_HAMMING_H__

#include <linux/mtd/nand.h>

/**
 * struct nand_ecc_sw_hamming_conf - private software Hamming ECC engine structure
 * @reqooblen: Save the actual user OOB length requested before overwriting it
 * @spare_oobbuf: Spare OOB buffer if none is provided
 * @code_size: Number of bytes needed to store a code (one code per step)
 * @nsteps: Number of steps
 * @calc_buf: Buffer to use when calculating ECC bytes
 * @code_buf: Buffer to use when reading (raw) ECC bytes from the chip
 * @sm_order: Smart Media special ordering
 */
struct nand_ecc_sw_hamming_conf {
	unsigned int reqooblen;
	void *spare_oobbuf;
	unsigned int code_size;
	unsigned int nsteps;
	u8 *calc_buf;
	u8 *code_buf;
	unsigned int sm_order;
};

int ecc_sw_hamming_calculate(const unsigned char *buf, unsigned int step_size,
			     unsigned char *code, bool sm_order);
int nand_ecc_sw_hamming_calculate(struct nand_device *nand,
				  const unsigned char *buf,
				  unsigned char *code);
int ecc_sw_hamming_correct(unsigned char *buf, unsigned char *read_ecc,
			   unsigned char *calc_ecc, unsigned int step_size,
			   bool sm_order);
int nand_ecc_sw_hamming_correct(struct nand_device *nand, unsigned char *buf,
				unsigned char *read_ecc,
				unsigned char *calc_ecc);

#endif /* __MTD_NAND_ECC_SW_HAMMING_H__ */
