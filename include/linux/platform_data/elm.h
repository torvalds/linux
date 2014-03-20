/*
 * BCH Error Location Module
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ELM_H
#define __ELM_H

enum bch_ecc {
	BCH4_ECC = 0,
	BCH8_ECC,
};

/* ELM support 8 error syndrome process */
#define ERROR_VECTOR_MAX		8

/**
 * struct elm_errorvec - error vector for elm
 * @error_reported:		set true for vectors error is reported
 * @error_uncorrectable:	number of uncorrectable errors
 * @error_count:		number of correctable errors in the sector
 * @error_loc:			buffer for error location
 *
 */
struct elm_errorvec {
	bool error_reported;
	bool error_uncorrectable;
	int error_count;
	int error_loc[ERROR_VECTOR_MAX];
};

void elm_decode_bch_error_page(struct device *dev, u8 *ecc_calc,
		struct elm_errorvec *err_vec);
int elm_config(struct device *dev, enum bch_ecc bch_type,
	int ecc_steps, int ecc_step_size, int ecc_syndrome_size);
#endif /* __ELM_H */
