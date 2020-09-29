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
 * @req_ctx: Save request context and tweak the original request to fit the
 *           engine needs
 * @code_size: Number of bytes needed to store a code (one code per step)
 * @nsteps: Number of steps
 * @calc_buf: Buffer to use when calculating ECC bytes
 * @code_buf: Buffer to use when reading (raw) ECC bytes from the chip
 * @sm_order: Smart Media special ordering
 */
struct nand_ecc_sw_hamming_conf {
	struct nand_ecc_req_tweak_ctx req_ctx;
	unsigned int code_size;
	unsigned int nsteps;
	u8 *calc_buf;
	u8 *code_buf;
	unsigned int sm_order;
};

#if IS_ENABLED(CONFIG_MTD_NAND_ECC_SW_HAMMING)

int nand_ecc_sw_hamming_init_ctx(struct nand_device *nand);
void nand_ecc_sw_hamming_cleanup_ctx(struct nand_device *nand);
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

#else /* !CONFIG_MTD_NAND_ECC_SW_HAMMING */

static inline int nand_ecc_sw_hamming_init_ctx(struct nand_device *nand)
{
	return -ENOTSUPP;
}

static inline void nand_ecc_sw_hamming_cleanup_ctx(struct nand_device *nand) {}

static inline int ecc_sw_hamming_calculate(const unsigned char *buf,
					   unsigned int step_size,
					   unsigned char *code, bool sm_order)
{
	return -ENOTSUPP;
}

static inline int nand_ecc_sw_hamming_calculate(struct nand_device *nand,
						const unsigned char *buf,
						unsigned char *code)
{
	return -ENOTSUPP;
}

static inline int ecc_sw_hamming_correct(unsigned char *buf,
					 unsigned char *read_ecc,
					 unsigned char *calc_ecc,
					 unsigned int step_size, bool sm_order)
{
	return -ENOTSUPP;
}

static inline int nand_ecc_sw_hamming_correct(struct nand_device *nand,
					      unsigned char *buf,
					      unsigned char *read_ecc,
					      unsigned char *calc_ecc)
{
	return -ENOTSUPP;
}

#endif /* CONFIG_MTD_NAND_ECC_SW_HAMMING */

#endif /* __MTD_NAND_ECC_SW_HAMMING_H__ */
