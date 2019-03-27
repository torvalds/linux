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

#ifndef _DEV_NAND_ECC_POS_H_
#define _DEV_NAND_ECC_POS_H_

static uint16_t default_software_ecc_positions_16[] = {2, 0, 1, 7, 4, 6};

static uint16_t default_software_ecc_positions_64[] = {

	42, 40, 41, 45, 43, 44, 48, 46,
	47, 51, 49, 50, 54, 52, 53, 57,
	55, 56, 60, 58, 59, 63, 61, 62
};

static uint16_t default_software_ecc_positions_128[] = {
	8, 9, 10, 11, 12, 13,
	18, 19, 20, 21, 22, 23,
	28, 29, 30, 31, 32, 33,
	38, 39, 40, 41, 42, 43,
	48, 49, 50, 51, 52, 53,
	58, 59, 60, 61, 62, 63,
	68, 69, 70, 71, 72, 73,
	78, 79, 80, 81, 82, 83,
	88, 89, 90, 91, 92, 93,
	98, 99, 100, 101, 102, 103,
	108, 109, 110, 111, 112, 113,
	118, 119, 120, 121, 122, 123,
};
#endif /* _DEV_NAND_ECC_POS_H_ */

