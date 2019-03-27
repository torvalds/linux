/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Authors: Ravi Pokala (rpokala@freebsd.org)
 *
 * Copyright (c) 2018 Panasas
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

#ifndef _DEV__JEDEC_DIMM__JEDEC_DIMM_H_
#define _DEV__JEDEC_DIMM__JEDEC_DIMM_H_

/* JEDEC DIMMs include one or more SMBus devices.
 *
 * At a minimum, they have an EEPROM containing either 256 bytes (DDR3) or 512
 * bytes (DDR4) of "Serial Presence Detect" (SPD) information. The SPD contains
 * data used by the memory controller to configure itself, and it also includes
 * asset information. The layout of SPD data is defined in:
 *
 * JEDEC Standard 21-C, Annex K (DDR3)
 * JEDEC Standard 21-C, Annex L (DDR4)
 *
 * DIMMs may also include a "Thermal Sensor on DIMM" (TSOD), which reports
 * temperature data. While not strictly required, the TSOD is so often included
 * that JEDEC defined standards for single chips which include both SPD and TSOD
 * functions. They respond on multiple SMBus addresses, depending on the
 * function.
 *
 * JEDEC Standard 21-C, TSE2002av (DDR3)
 * JEDEC Standard 21-C, TSE2004av (DDR4)
 */

/* TSE2004av defines several Device Type Identifiers (DTIs), which are the high
 * nybble of the SMBus address. Addresses with DTIs of PROTECT (or PAGE, which
 * has the same value) are essentially "broadcast" addresses; all SPD devices
 * respond to them, changing their mode based on the Logical Serial Address
 * (LSA) encoded in bits [3:1]. For normal SPD access, bits [3:1] encode the
 * DIMM slot number.
 */
#define JEDEC_SPD_PAGE_SIZE	256
#define JEDEC_DTI_SPD		0xa0
#define JEDEC_DTI_TSOD		0x30
#define JEDEC_DTI_PROTECT	0x60
#define JEDEC_LSA_PROTECT_SET0	0x02
#define JEDEC_LSA_PROTECT_SET1	0x08
#define JEDEC_LSA_PROTECT_SET2	0x0a
#define JEDEC_LSA_PROTECT_SET3	0x00
#define JEDEC_LSA_PROTECT_CLR	0x06
#define JEDEC_LSA_PROTECT_GET0	0x03
#define JEDEC_LSA_PROTECT_GET1	0x09
#define JEDEC_LSA_PROTECT_GET2	0x0b
#define JEDEC_LSA_PROTECT_GET3	0x01
#define JEDEC_DTI_PAGE		0x60
#define JEDEC_LSA_PAGE_SET0	0x0c
#define JEDEC_LSA_PAGE_SET1	0x0e
#define JEDEC_LSA_PAGE_GET	0x0d

/* The offsets and lengths of various SPD bytes are defined in Annex K (DDR3)
 * and Annex L (DDR4). Conveniently, the DRAM type is at the same offset for
 * both versions.
 *
 * This list only includes information needed to get the asset information and
 * calculate the DIMM capacity.
 */
#define SPD_OFFSET_DRAM_TYPE 		2
#define SPD_OFFSET_DDR3_SDRAM_CAPACITY	4
#define SPD_OFFSET_DDR3_DIMM_RANKS	7
#define SPD_OFFSET_DDR3_SDRAM_WIDTH	7
#define SPD_OFFSET_DDR3_BUS_WIDTH	8
#define SPD_OFFSET_DDR3_TSOD_PRESENT	32
#define SPD_OFFSET_DDR3_SERIAL		122
#define SPD_LEN_DDR3_SERIAL		4
#define SPD_OFFSET_DDR3_PARTNUM		128
#define SPD_LEN_DDR3_PARTNUM		18
#define SPD_OFFSET_DDR4_SDRAM_CAPACITY	4
#define SPD_OFFSET_DDR4_SDRAM_PKG_TYPE	6
#define SPD_OFFSET_DDR4_DIMM_RANKS	12
#define SPD_OFFSET_DDR4_SDRAM_WIDTH	12
#define SPD_OFFSET_DDR4_BUS_WIDTH	13
#define SPD_OFFSET_DDR4_TSOD_PRESENT	14
#define SPD_OFFSET_DDR4_SERIAL		325
#define SPD_LEN_DDR4_SERIAL		4
#define SPD_OFFSET_DDR4_PARTNUM		329
#define SPD_LEN_DDR4_PARTNUM		20

/* The "DRAM Type" field of the SPD enumerates various memory technologies which
 * have been used over the years. The list is append-only, so we need only refer
 * to the latest SPD specification. In this case, Annex L for DDR4.
 */
enum dram_type {
	DRAM_TYPE_RESERVED = 			0x00,
	DRAM_TYPE_FAST_PAGE_MODE = 		0x01,
	DRAM_TYPE_EDO = 			0x02,
	DRAM_TYPE_PIPLEINED_NYBBLE = 		0x03,
	DRAM_TYPE_SDRAM = 			0x04,
	DRAM_TYPE_ROM = 			0x05,
	DRAM_TYPE_DDR_SGRAM = 			0x06,
	DRAM_TYPE_DDR_SDRAM = 			0x07,
	DRAM_TYPE_DDR2_SDRAM = 			0x08,
	DRAM_TYPE_DDR2_SDRAM_FBDIMM = 		0x09,
	DRAM_TYPE_DDR2_SDRAM_FBDIMM_PROBE =	0x0a,
	DRAM_TYPE_DDR3_SDRAM = 			0x0b,
	DRAM_TYPE_DDR4_SDRAM = 			0x0c,
	DRAM_TYPE_RESERVED_0D = 		0x0d,
	DRAM_TYPE_DDR4E_SDRAM = 		0x0e,
	DRAM_TYPE_LPDDR3_SDRAM = 		0x0f,
	DRAM_TYPE_LPDDR4_SDRAM = 		0x10,
};

/* The TSOD is accessed using a simple word interface, which is identical
 * between TSE2002av (DDR3) and TSE2004av (DDR4).
 */
#define TSOD_REG_CAPABILITES	0
#define TSOD_REG_CONFIG		1
#define TSOD_REG_LIM_HIGH	2
#define TSOD_REG_LIM_LOW	3
#define TSOD_REG_LIM_CRIT	4
#define TSOD_REG_TEMPERATURE	5
#define TSOD_REG_MANUFACTURER	6
#define TSOD_REG_DEV_REV	7

#endif /* _DEV__JEDEC_DIMM__JEDEC_DIMM_H_ */

/* vi: set ts=8 sw=4 sts=8 noet: */
