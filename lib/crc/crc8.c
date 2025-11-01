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

#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

#include <linux/crc8.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/printk.h>

/**
 * crc8_populate_msb - fill crc table for given polynomial in reverse bit order.
 *
 * @table:	table to be filled.
 * @polynomial:	polynomial for which table is to be filled.
 */
void crc8_populate_msb(u8 table[CRC8_TABLE_SIZE], u8 polynomial)
{
	int i, j;
	const u8 msbit = 0x80;
	u8 t = msbit;

	table[0] = 0;

	for (i = 1; i < CRC8_TABLE_SIZE; i *= 2) {
		t = (t << 1) ^ (t & msbit ? polynomial : 0);
		for (j = 0; j < i; j++)
			table[i+j] = table[j] ^ t;
	}
}
EXPORT_SYMBOL(crc8_populate_msb);

/**
 * crc8_populate_lsb - fill crc table for given polynomial in regular bit order.
 *
 * @table:	table to be filled.
 * @polynomial:	polynomial for which table is to be filled.
 */
void crc8_populate_lsb(u8 table[CRC8_TABLE_SIZE], u8 polynomial)
{
	int i, j;
	u8 t = 1;

	table[0] = 0;

	for (i = (CRC8_TABLE_SIZE >> 1); i; i >>= 1) {
		t = (t >> 1) ^ (t & 1 ? polynomial : 0);
		for (j = 0; j < CRC8_TABLE_SIZE; j += 2*i)
			table[i+j] = table[j] ^ t;
	}
}
EXPORT_SYMBOL(crc8_populate_lsb);

/**
 * crc8 - calculate a crc8 over the given input data.
 *
 * @table: crc table used for calculation.
 * @pdata: pointer to data buffer.
 * @nbytes: number of bytes in data buffer.
 * @crc: previous returned crc8 value.
 */
u8 crc8(const u8 table[CRC8_TABLE_SIZE], const u8 *pdata, size_t nbytes, u8 crc)
{
	/* loop over the buffer data */
	while (nbytes-- > 0)
		crc = table[(crc ^ *pdata++) & 0xff];

	return crc;
}
EXPORT_SYMBOL(crc8);

MODULE_DESCRIPTION("CRC8 (by Williams, Ross N.) function");
MODULE_AUTHOR("Broadcom Corporation");
MODULE_LICENSE("Dual BSD/GPL");
