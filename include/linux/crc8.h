/*
 * Copyright (c) 2011 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __CRC8_H_
#define __CRC8_H_

#include <linux/types.h>

/* see usage of this value in crc8() description */
#define CRC8_INIT_VALUE		0xFF

/*
 * Return value of crc8() indicating valid message+crc. This is true
 * if a CRC is inverted before transmission. The CRC computed over the
 * whole received bitstream is _table[x], where x is the bit pattern
 * of the modification (almost always 0xff).
 */
#define CRC8_GOOD_VALUE(_table)	(_table[0xFF])

/* required table size for crc8 algorithm */
#define CRC8_TABLE_SIZE			256

/* helper macro assuring right table size is used */
#define DECLARE_CRC8_TABLE(_table) \
	static u8 _table[CRC8_TABLE_SIZE]

/**
 * crc8_populate_lsb - fill crc table for given polynomial in regular bit order.
 *
 * @table:	table to be filled.
 * @polynomial:	polynomial for which table is to be filled.
 *
 * This function fills the provided table according the polynomial provided for
 * regular bit order (lsb first). Polynomials in CRC algorithms are typically
 * represented as shown below.
 *
 *	poly = x^8 + x^7 + x^6 + x^4 + x^2 + 1
 *
 * For lsb first direction x^7 maps to the lsb. So the polynomial is as below.
 *
 * - lsb first: poly = 10101011(1) = 0xAB
 */
void crc8_populate_lsb(u8 table[CRC8_TABLE_SIZE], u8 polynomial);

/**
 * crc8_populate_msb - fill crc table for given polynomial in reverse bit order.
 *
 * @table:	table to be filled.
 * @polynomial:	polynomial for which table is to be filled.
 *
 * This function fills the provided table according the polynomial provided for
 * reverse bit order (msb first). Polynomials in CRC algorithms are typically
 * represented as shown below.
 *
 *	poly = x^8 + x^7 + x^6 + x^4 + x^2 + 1
 *
 * For msb first direction x^7 maps to the msb. So the polynomial is as below.
 *
 * - msb first: poly = (1)11010101 = 0xD5
 */
void crc8_populate_msb(u8 table[CRC8_TABLE_SIZE], u8 polynomial);

/**
 * crc8() - calculate a crc8 over the given input data.
 *
 * @table:	crc table used for calculation.
 * @pdata:	pointer to data buffer.
 * @nbytes:	number of bytes in data buffer.
 * @crc:	previous returned crc8 value.
 *
 * The CRC8 is calculated using the polynomial given in crc8_populate_msb()
 * or crc8_populate_lsb().
 *
 * The caller provides the initial value (either %CRC8_INIT_VALUE
 * or the previous returned value) to allow for processing of
 * discontiguous blocks of data.  When generating the CRC the
 * caller is responsible for complementing the final return value
 * and inserting it into the byte stream.  When validating a byte
 * stream (including CRC8), a final return value of %CRC8_GOOD_VALUE
 * indicates the byte stream data can be considered valid.
 *
 * Reference:
 * "A Painless Guide to CRC Error Detection Algorithms", ver 3, Aug 1993
 * Williams, Ross N., ross<at>ross.net
 * (see URL http://www.ross.net/crc/download/crc_v3.txt).
 */
u8 crc8(const u8 table[CRC8_TABLE_SIZE], u8 *pdata, size_t nbytes, u8 crc);

#endif /* __CRC8_H_ */
