/*	$OpenBSD: htbreg.h,v 1.1 2016/11/17 14:41:21 visa Exp $	*/

/*
 * Copyright (c) 2016 Visa Hankala
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LOONGSON_DEV_HTBREG_H_
#define _LOONGSON_DEV_HTBREG_H_

/*
 * These addresses are translated by the CPU nodes' crossbar to the address
 * space of the HT interface that connects to the northbridge. The translation
 * is set up by the firmware.
 */

#define HTB_IO_BASE			0x18000000u
#define HTB_CFG_TYPE0_BASE		0x1a000000u
#define HTB_CFG_TYPE1_BASE		0x1b000000u
#define HTB_MEM_BASE			0x40000000u

#define HTB_IO_SIZE			0x01000000u
#define HTB_MEM_SIZE			0x40000000u

#define HTB_IO_LEGACY			0x4000u

#endif /* _LOONGSON_DEV_HTBREG_H_ */
