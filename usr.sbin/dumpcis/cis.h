/*
 *	PCMCIA card structures and defines.
 *	These defines relate to the user level
 *	structures and card information, not
 *	driver/process communication.
 *-------------------------------------------------------------------------
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995 Andrew McRae.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

/*
 *	Card Information Structure tuples definitions
 *	The structure of a tuple is basically:
 *
 *		Tuple_code
 *		Tuple_data_length
 *		Tuple_data ...
 *
 *	Tuples are contiguous in attribute memory, and
 *	are terminated with a 0xFF for the tuple code or
 *	the tuple length.
 */
#ifndef	_PCCARD_CIS_H
#define	_PCCARD_CIS_H

#define	CIS_NULL	0	/* Empty tuple */
#define	CIS_MEM_COMMON	0x01	/* Device descriptor, common memory */
#define	CIS_LONGLINK_CB	0x02	/* Long link to next chain for CardBus */
#define	CIS_INDIRECT	0x03	/* Indirect access */
#define	CIS_CONF_MAP_CB	0x04	/* Card Configuration map for CardBus */
#define	CIS_CONFIG_CB	0x05	/* Card Configuration entry for CardBus */
#define	CIS_LONGLINK_MFC	0x06	/* Long link to next chain for Multi function card */
#define	CIS_BAR		0x07	/* Base address register for CardBus */
#define	CIS_CHECKSUM	0x10	/* Checksum */
#define	CIS_LONGLINK_A	0x11	/* Link to Attribute memory */
#define	CIS_LONGLINK_C	0x12	/* Link to Common memory */
#define	CIS_LINKTARGET	0x13	/* Linked tuple must start with this. */
#define	CIS_NOLINK	0x14	/* Assume no common memory link tuple. */
#define	CIS_INFO_V1	0x15	/* Card info data, version 1 */
#define	CIS_ALTSTR	0x16	/* Alternate language string tuple. */
#define	CIS_MEM_ATTR	0x17	/* Device descriptor, Attribute memory */
#define	CIS_JEDEC_C	0x18	/* JEDEC descr for common memory */
#define	CIS_JEDEC_A	0x19	/* JEDEC descr for Attribute memory */
#define	CIS_CONF_MAP	0x1A	/* Card Configuration map */
#define	CIS_CONFIG	0x1B	/* Card Configuration entry */
#define	CIS_DEVICE_OC	0x1C	/* Other conditions info - common memory */
#define	CIS_DEVICE_OA	0x1D	/* Other conditions info - attribute memory */
#define	CIS_DEVICEGEO	0x1E	/* Geometry info for common memory */
#define	CIS_DEVICEGEO_A	0x1F	/* Geometry info for attribute memory */
#define	CIS_MANUF_ID	0x20	/* Card manufacturer's ID */
#define	CIS_FUNC_ID	0x21	/* Function of card */
#define	CIS_FUNC_EXT	0x22	/* Functional extension */
/*
 *	Data recording format tuples.
 */
#define	CIS_SW_INTERLV	0x23	/* Software interleave */
#define	CIS_VERS_2	0x40	/* Card info data, version 2 */
#define	CIS_FORMAT	0x41	/* Memory card format */
#define	CIS_GEOMETRY	0x42	/* Disk sector layout */
#define	CIS_BYTEORDER	0x43	/* Byte order of memory data */
#define	CIS_DATE	0x44	/* Format data/time */
#define	CIS_BATTERY	0x45	/* Battery replacement date */
#define	CIS_ORG		0x46	/* Organization of data on card */
#define	CIS_END		0xFF	/* Termination code */

/*
 *	Internal tuple definitions.
 *
 *	Device descriptor for memory (CIS_MEM_ATTR, CIS_MEM_COMMON)
 *
 *	Byte 1:
 *		0xF0 - Device type
 *		0x08 - Write protect switch
 *		0x07 - Speed index (7 = extended speed)
 *	Byte 2: Extended speed (bit 7 = another follows)
 *	Byte 3: (ignored if 0xFF)
 *		0xF8 - Addressable units (0's numbered)
 *		0x07 - Unit size
 *	The three byte sequence is repeated until byte 1 == 0xFF
 */

/*
 *	CIS_INFO_V1 - Version one card information.
 *
 *	Byte 1:   Major version number (should be 4)
 *	Byte 2:   Minor version number (should be 1)
 *	Byte 3-x: Null terminated Manufacturer name
 *	Byte x-x: Null terminated product name
 *	Byte x-x: Null terminated additional info 1
 *	Byte x-x: Null terminated additional info 2
 *	Byte x:   final byte must be 0xFF
 */
#define	CIS_MAJOR_VERSION	4
#define	CIS_MINOR_VERSION	1

/*
 *	CIS_CONF_MAP - Provides an address map for the card
 *			configuration register(s), and a max value
 *			identifying the last configuration tuple.
 *
 *	Byte 1:
 *		0x3C - Register mask size (0's numbered)
 *		0x03 - Register address size (0's numbered)
 *	Byte 2:
 *		0x3F - ID of last configuration.
 *	Byte 3-n: Card register address (size is determined by
 *			the value in byte 1).
 *	Byte x-x: Card register masks (size determined by the
 *			value in byte 1)
 */

/*
 *	CIS_CONFIG - Card configuration entry. Multiple tuples may
 *		exist of this type, each one describing a different
 *		memory/I-O map that can be used to address this card.
 *		The first one usually has extra config data about the
 *		card features. The final configuration tuple number
 *		is stored in the CIS_CONF_MAP tuple so that the complete
 *		list can be scanned.
 *
 *	Byte 1:
 *		0x3F - Configuration ID number.
 *		0x40 - Indicates this is the default configuration
 *		0x80 - Interface byte exists
 *	Byte 2: (exists only if bit 0x80 set in byte 1)
 *		0x0F - Interface type value
 *		0x10 - Battery voltage detect
 *		0x20 - Write protect active
 *		0x40 - RdyBsy active bit
 *		0x80 - Wait signal required
 *	Byte 3: (features byte)
 *		0x03 - Power sub-tuple(s) exists
 *		0x04 - Timing sub-tuple exists
 *		0x08 - I/O space sub-tuple exists
 *		0x10 - IRQ sub-tuple exists
 *		0x60 - Memory space sub-tuple(s) exists
 *		0x80 - Miscellaneous sub-tuple exists
 */
#define	CIS_FEAT_POWER(x)	((x) & 0x3)
#define	CIS_FEAT_TIMING		0x4
#define	CIS_FEAT_I_O		0x8
#define	CIS_FEAT_IRQ		0x10
#define	CIS_FEAT_MEMORY(x)	(((x) >> 5) & 0x3)
#define	CIS_FEAT_MISC		0x80
/*
 *	Depending on whether the "features" byte has the corresponding
 *	bit set, a number of sub-tuples follow. Some features have
 *	more than one sub-tuple, depending on the count within the
 *	features byte (e.g power feature bits allows up to 3 sub-tuples).
 *
 *	Power structure sub-tuple:
 *	Byte 1: parameter exists - Each bit (starting from 0x01) indicates
 *		that a parameter block exists - up to 8 parameter blocks
 *		are therefore allowed).
 *	Byte 2:
 *		0x7F - Parameter data
 *		0x80 - More bytes follow (0 = last byte)
 *
 *	Timing sub-tuple
 *	Byte 1:
 *		0x03 - Wait scale
 *		0x1C - Ready scale
 *		0xE0 - Reserved scale
 *	Byte 2: extended wait scale if wait scale != 3
 *	Byte 3: extended ready scale if ready scale != 7
 *	Byte 4: extended reserved scale if reserved scale != 7
 */
#define	CIS_WAIT_SCALE(x)	((x) & 0x3)
#define	CIS_READY_SCALE(x)	(((x)>>2) & 0x7)
#define	CIS_RESERVED_SCALE(x)	(((x)>>5) & 0x7)
/*
 *	I/O mapping sub-tuple:
 *	Byte 1:
 *		0x1F - I/O address lines
 *		0x20 - 8 bit I/O
 *		0x40 - 16 bit I/O
 *		0x80 - I/O range??
 *	Byte 2:
 *		0x0F - 0's numbered count of I/O block subtuples following.
 *		0x30 - Size of I/O address value within subtuple. Values
 *			can be 1 (8 bits), 2 (16 bits) or 3 (32 bits).
 *		0xC0 - Size of I/O port block size value within subtuple.
 *	I/O block sub-tuples, count from previous block:
 *		Byte 1-n: I/O start address
 *		Byte x-x: Size of I/O port block.
 */
#define	CIS_IO_ADDR(x)	((x) & 0x1F)
#define	CIS_IO_8BIT	0x20
#define	CIS_IO_16BIT	0x40
#define	CIS_IO_RANGE	0x80
#define	CIS_IO_BLKS(x)	((x) & 0xF)
#define	CIS_IO_ADSZ(x)	(((x)>>4) & 3)
#define	CIS_IO_BLKSZ(x)	(((x)>>6) & 3)
/*
 *	IRQ sub-tuple.
 *	Byte 1:
 *		0x0F - Irq number or mask bits
 *		0x10 - IRQ mask values exist
 *		0x20 - Level triggered interrupts
 *		0x40 - Pulse triggered requests
 *		0x80 - Interrupt sharing.
 *	Byte 2-3: Interrupt req mask (if 0x10 of byte 1 set).
 */
#define	CIS_IRQ_IRQN(x)		((x) & 0xF)
#define	CIS_IRQ_MASK		0x10
#define	CIS_IRQ_LEVEL		0x20
#define	CIS_IRQ_PULSE		0x40
#define	CIS_IRQ_SHARING		0x80
/*
 *	Memory block subtuple. Depending on the features bits, the
 *	following subtuples are used:
 *	mem features == 1
 *		Byte 1-2: upper 16 bits of 24 bit memory length.
 *	mem features == 2
 *		Byte 1-2: upper 16 bits of 24 bit memory length.
 *		Byte 3-4: upper 16 bits of 24 bit memory address.
 *	mem_features == 3
 *		Byte 1:
 *			0x07 - 0's numbered count of memory sub-tuples
 *			0x18 - Memory length size (1's numbered)
 *			0x60 - Memory address size (1's numbered)
 *			0x80 - Host address value exists
 *		Memory sub-tuples follow:
 *			Byte 1-n: Memory length value (<< 8)
 *			Byte n-n: Memory card address value (<< 8)
 *			Byte n-n: Memory host address value (<< 8)
 */
#define	CIS_FEAT_MEM_NONE	0	/* No memory config */
#define	CIS_FEAT_MEM_LEN	1	/* Just length */
#define	CIS_FEAT_MEM_ADDR	2	/* Card address & length */
#define	CIS_FEAT_MEM_WIN	3	/* Multiple windows */

#define	CIS_MEM_WINS(x)		(((x) & 0x7)+1)
#define	CIS_MEM_LENSZ(x)	(((x) >> 3) & 0x3)
#define	CIS_MEM_ADDRSZ(x)	(((x) >> 5) & 0x3)
#define	CIS_MEM_HOST		0x80
/*
 *	Misc sub-tuple.
 *	Byte 1:
 *	Byte 2:
 *		0x0c - DMA Request Signal
 *                      00 - not support DMA
 *                      01 - use SPKR# line
 *                      10 - use IOIS16# line
 *                      11 - use INPACK# line
 *		0x10 - DMA Width
 *                      0 - 8 bit DMA
 *                      1 - 16 bit DMA
 */
#define	CIS_MISC_DMA_WIDTH(x)	(((x) & 0x10) >> 4)
#define	CIS_MISC_DMA_REQ(x)	(((x) >> 2) & 0x3)

#endif	/* _PCCARD_CIS_H */
