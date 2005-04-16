/*
 *  drivers/mtd/nand_ecc.h
 *
 *  Copyright (C) 2000 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * $Id: nand_ecc.h,v 1.4 2004/06/17 02:35:02 dbrown Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file is the header for the ECC algorithm.
 */

#ifndef __MTD_NAND_ECC_H__
#define __MTD_NAND_ECC_H__

struct mtd_info;

/*
 * Calculate 3 byte ECC code for 256 byte block
 */
int nand_calculate_ecc(struct mtd_info *mtd, const u_char *dat, u_char *ecc_code);

/*
 * Detect and correct a 1 bit error for 256 byte block
 */
int nand_correct_data(struct mtd_info *mtd, u_char *dat, u_char *read_ecc, u_char *calc_ecc);

#endif /* __MTD_NAND_ECC_H__ */
