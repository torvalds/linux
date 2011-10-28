/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

/*
 * This code implements the ECC algorithm used in SmartMedia.
 *
 * The ECC comprises 22 bits of parity information and is stuffed into 3 bytes.
 * The two unused bit are set to 1.
 * The ECC can correct single bit errors in a 256-byte page of data. Thus, two such ECC
 * blocks are used on a 512-byte NAND page.
 *
 */

#ifndef __YAFFS_ECC_H__
#define __YAFFS_ECC_H__

struct yaffs_ecc_other {
	unsigned char col_parity;
	unsigned line_parity;
	unsigned line_parity_prime;
};

void yaffs_ecc_cacl(const unsigned char *data, unsigned char *ecc);
int yaffs_ecc_correct(unsigned char *data, unsigned char *read_ecc,
		      const unsigned char *test_ecc);

void yaffs_ecc_calc_other(const unsigned char *data, unsigned n_bytes,
			  struct yaffs_ecc_other *ecc);
int yaffs_ecc_correct_other(unsigned char *data, unsigned n_bytes,
			    struct yaffs_ecc_other *read_ecc,
			    const struct yaffs_ecc_other *test_ecc);
#endif
