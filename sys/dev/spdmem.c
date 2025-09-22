/*	$OpenBSD: spdmem.c,v 1.7 2019/12/21 12:33:03 kettenis Exp $	*/
/* $NetBSD: spdmem.c,v 1.3 2007/09/20 23:09:59 xtraeme Exp $ */

/*
 * Copyright (c) 2007 Jonathan Gray <jsg@openbsd.org>
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

/*
 * Copyright (c) 2007 Nicolas Joly
 * Copyright (c) 2007 Paul Goyette
 * Copyright (c) 2007 Tobias Nygren
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Serial Presence Detect (SPD) memory identification
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/spdmemvar.h>

/* Encodings of the size used/total byte for certain memory types    */
#define	SPDMEM_SPDSIZE_MASK		0x0F	/* SPD EEPROM Size   */

#define	SPDMEM_SPDLEN_128		0x00	/* SPD EEPROM Sizes  */
#define	SPDMEM_SPDLEN_176		0x10
#define	SPDMEM_SPDLEN_256		0x20
#define	SPDMEM_SPDLEN_MASK		0x70	/* Bits 4 - 6        */

#define	SPDMEM_DDR4_SPDLEN_128		0x01	/* SPD EEPROM Sizes  */
#define	SPDMEM_DDR4_SPDLEN_256		0x02
#define	SPDMEM_DDR4_SPDLEN_384		0x03
#define	SPDMEM_DDR4_SPDLEN_512		0x04
#define	SPDMEM_DDR4_SPDLEN_MASK		0x0f	/* Bits 4 - 6        */

#define	SPDMEM_SPDCRC_116		0x80	/* CRC Bytes covered */
#define	SPDMEM_SPDCRC_125		0x00
#define	SPDMEM_SPDCRC_MASK		0x80	/* Bit 7             */

/* possible values for the memory type */
#define	SPDMEM_MEMTYPE_FPM		0x01
#define	SPDMEM_MEMTYPE_EDO		0x02
#define	SPDMEM_MEMTYPE_PIPE_NIBBLE	0x03
#define	SPDMEM_MEMTYPE_SDRAM		0x04
#define	SPDMEM_MEMTYPE_ROM		0x05
#define	SPDMEM_MEMTYPE_DDRSGRAM		0x06
#define	SPDMEM_MEMTYPE_DDRSDRAM		0x07
#define	SPDMEM_MEMTYPE_DDR2SDRAM	0x08
#define	SPDMEM_MEMTYPE_FBDIMM		0x09
#define	SPDMEM_MEMTYPE_FBDIMM_PROBE	0x0a
#define	SPDMEM_MEMTYPE_DDR3SDRAM	0x0b
#define	SPDMEM_MEMTYPE_DDR4SDRAM	0x0c
					/* 0xd reserved */
#define	SPDMEM_MEMTYPE_DDR4ESDRAM	0x0e
#define	SPDMEM_MEMTYPE_LPDDR3SDRAM	0x0f
#define	SPDMEM_MEMTYPE_LPDDR4SDRAM	0x10
#define	SPDMEM_MEMTYPE_LPDDR4XSDRAM	0x11
#define	SPDMEM_MEMTYPE_DDR5SDRAM	0x12
#define	SPDMEM_MEMTYPE_LPDDR5SDRAM	0x13

#define	SPDMEM_MEMTYPE_NONE		0xff

#define SPDMEM_MEMTYPE_DIRECT_RAMBUS	0x01
#define SPDMEM_MEMTYPE_RAMBUS		0x11

/* possible values for the supply voltage */
#define	SPDMEM_VOLTAGE_TTL_5V		0x00
#define	SPDMEM_VOLTAGE_TTL_LV		0x01
#define	SPDMEM_VOLTAGE_HSTTL_1_5V	0x02
#define	SPDMEM_VOLTAGE_SSTL_3_3V	0x03
#define	SPDMEM_VOLTAGE_SSTL_2_5V	0x04
#define	SPDMEM_VOLTAGE_SSTL_1_8V	0x05

/* possible values for module configuration */
#define	SPDMEM_MODCONFIG_PARITY		0x01
#define	SPDMEM_MODCONFIG_ECC		0x02

/* for DDR2, module configuration is a bit-mask field */
#define	SPDMEM_MODCONFIG_HAS_DATA_PARITY	0x01
#define	SPDMEM_MODCONFIG_HAS_DATA_ECC		0x02
#define	SPDMEM_MODCONFIG_HAS_ADDR_CMD_PARITY	0x04

/* possible values for the refresh field */
#define	SPDMEM_REFRESH_STD		0x00
#define	SPDMEM_REFRESH_QUARTER		0x01
#define	SPDMEM_REFRESH_HALF		0x02
#define	SPDMEM_REFRESH_TWOX		0x03
#define	SPDMEM_REFRESH_FOURX		0x04
#define	SPDMEM_REFRESH_EIGHTX		0x05
#define	SPDMEM_REFRESH_SELFREFRESH	0x80

/* superset types */
#define	SPDMEM_SUPERSET_ESDRAM		0x01
#define	SPDMEM_SUPERSET_DDR_ESDRAM	0x02
#define	SPDMEM_SUPERSET_EDO_PEM		0x03
#define	SPDMEM_SUPERSET_SDR_PEM		0x04

/* FPM and EDO DIMMS */
#define SPDMEM_FPM_ROWS			0x00
#define SPDMEM_FPM_COLS			0x01
#define SPDMEM_FPM_BANKS		0x02
#define SPDMEM_FPM_CONFIG		0x08
#define SPDMEM_FPM_REFRESH		0x09
#define SPDMEM_FPM_SUPERSET		0x0c

/* PC66/PC100/PC133 SDRAM */
#define SPDMEM_SDR_ROWS			0x00
#define SPDMEM_SDR_COLS			0x01
#define SPDMEM_SDR_BANKS		0x02
#define SPDMEM_SDR_CYCLE		0x06
#define SPDMEM_SDR_BANKS_PER_CHIP	0x0e
#define SPDMEM_SDR_MOD_ATTRIB		0x12
#define SPDMEM_SDR_SUPERSET		0x1d

#define SPDMEM_SDR_FREQUENCY		126
#define SPDMEM_SDR_CAS			127
#define SPDMEM_SDR_FREQ_66		0x66
#define SPDMEM_SDR_FREQ_100		0x64
#define SPDMEM_SDR_FREQ_133		0x85
#define SPDMEM_SDR_CAS2			(1 << 1)
#define SPDMEM_SDR_CAS3			(1 << 2)

/* Rambus Direct DRAM */
#define SPDMEM_RDR_MODULE_TYPE		0x00
#define SPDMEM_RDR_ROWS_COLS		0x01
#define SPDMEM_RDR_BANK			0x02

#define SPDMEM_RDR_TYPE_RIMM		1
#define SPDMEM_RDR_TYPE_SORIMM		2
#define SPDMEM_RDR_TYPE_EMBED		3
#define SPDMEM_RDR_TYPE_RIMM32		4

/* Dual Data Rate SDRAM */
#define SPDMEM_DDR_ROWS			0x00
#define SPDMEM_DDR_COLS			0x01
#define SPDMEM_DDR_RANKS		0x02
#define SPDMEM_DDR_DATAWIDTH		0x03
#define SPDMEM_DDR_VOLTAGE		0x05
#define SPDMEM_DDR_CYCLE		0x06
#define SPDMEM_DDR_REFRESH		0x09
#define SPDMEM_DDR_BANKS_PER_CHIP	0x0e
#define SPDMEM_DDR_CAS			0x0f
#define SPDMEM_DDR_MOD_ATTRIB		0x12
#define SPDMEM_DDR_SUPERSET		0x1d

#define SPDMEM_DDR_ATTRIB_REG		(1 << 1)

/* Dual Data Rate 2 SDRAM */
#define SPDMEM_DDR2_ROWS		0x00
#define SPDMEM_DDR2_COLS		0x01
#define SPDMEM_DDR2_RANKS		0x02
#define SPDMEM_DDR2_DATAWIDTH		0x03
#define SPDMEM_DDR2_VOLTAGE		0x05
#define SPDMEM_DDR2_CYCLE		0x06
#define SPDMEM_DDR2_DIMMTYPE		0x11
#define SPDMEM_DDR2_RANK_DENSITY	0x1c

#define SPDMEM_DDR2_TYPE_REGMASK	((1 << 4) | (1 << 0))
#define SPDMEM_DDR2_SODIMM		(1 << 2)
#define SPDMEM_DDR2_MICRO_DIMM		(1 << 3)
#define SPDMEM_DDR2_MINI_RDIMM		(1 << 4)
#define SPDMEM_DDR2_MINI_UDIMM		(1 << 5)

/* DDR2 FB-DIMM SDRAM */
#define SPDMEM_FBDIMM_ADDR		0x01
#define SPDMEM_FBDIMM_RANKS		0x04
#define SPDMEM_FBDIMM_MTB_DIVIDEND	0x06
#define SPDMEM_FBDIMM_MTB_DIVISOR	0x07
#define SPDMEM_FBDIMM_PROTO		0x4e

#define SPDMEM_FBDIMM_RANKS_WIDTH		0x07
#define SPDMEM_FBDIMM_ADDR_BANKS		0x02
#define SPDMEM_FBDIMM_ADDR_COL			0x0c
#define SPDMEM_FBDIMM_ADDR_COL_SHIFT		2
#define SPDMEM_FBDIMM_ADDR_ROW			0xe0
#define SPDMEM_FBDIMM_ADDR_ROW_SHIFT		5
#define SPDMEM_FBDIMM_PROTO_ECC			(1 << 1)


/* Dual Data Rate 3 SDRAM */
#define SPDMEM_DDR3_MODTYPE		0x00
#define SPDMEM_DDR3_DENSITY		0x01
#define SPDMEM_DDR3_MOD_ORG		0x04
#define SPDMEM_DDR3_DATAWIDTH		0x05
#define SPDMEM_DDR3_MTB_DIVIDEND	0x07
#define SPDMEM_DDR3_MTB_DIVISOR		0x08
#define SPDMEM_DDR3_TCKMIN		0x09
#define SPDMEM_DDR3_THERMAL		0x1d

#define SPDMEM_DDR3_DENSITY_CAPMASK		0x0f
#define SPDMEM_DDR3_MOD_ORG_CHIPWIDTH_MASK	0x07
#define SPDMEM_DDR3_MOD_ORG_BANKS_SHIFT		3
#define SPDMEM_DDR3_MOD_ORG_BANKS_MASK		0x07
#define SPDMEM_DDR3_DATAWIDTH_ECCMASK		(1 << 3)
#define SPDMEM_DDR3_DATAWIDTH_PRIMASK		0x07
#define SPDMEM_DDR3_THERMAL_PRESENT		(1 << 7)

#define SPDMEM_DDR3_RDIMM		0x01
#define SPDMEM_DDR3_UDIMM		0x02
#define SPDMEM_DDR3_SODIMM		0x03
#define SPDMEM_DDR3_MICRO_DIMM		0x04
#define SPDMEM_DDR3_MINI_RDIMM		0x05
#define SPDMEM_DDR3_MINI_UDIMM		0x06

/* Dual Data Rate 4 SDRAM */
#define	SPDMEM_DDR4_MODTYPE		0x00
#define	SPDMEM_DDR4_DENSITY		0x01
#define	SPDMEM_DDR4_PACK_TYPE		0x03
#define	SPDMEM_DDR4_MOD_ORG		0x09
#define	SPDMEM_DDR4_DATAWIDTH		0x0a
#define	SPDMEM_DDR4_THERMAL		0x0b
#define	SPDMEM_DDR4_TCKMIN_MTB		0x0f
#define	SPDMEM_DDR4_TCKMIN_FTB		0x7d	/* not offset by 3 */

#define	SPDMEM_DDR4_DENSITY_CAPMASK		0x0f
#define	SPDMEM_DDR4_PACK_TYPE_SIG_LOAD_MASK	0x03
#define	SPDMEM_DDR4_PACK_TYPE_SIG_SINGLE_LOAD	0x02
#define	SPDMEM_DDR4_PACK_TYPE_DIE_COUNT_SHIFT	4
#define	SPDMEM_DDR4_PACK_TYPE_DIE_COUNT_MASK	0x07
#define	SPDMEM_DDR4_MOD_ORG_CHIPWIDTH_MASK	0x07
#define	SPDMEM_DDR4_MOD_ORG_BANKS_SHIFT		3
#define	SPDMEM_DDR4_MOD_ORG_BANKS_MASK		0x07
#define	SPDMEM_DDR4_DATAWIDTH_ECCMASK		(1 << 3)
#define	SPDMEM_DDR4_DATAWIDTH_PRIMASK		0x07
#define	SPDMEM_DDR4_THERMAL_PRESENT		(1 << 7)

#define	SPDMEM_DDR4_RDIMM		0x01
#define	SPDMEM_DDR4_UDIMM		0x02
#define	SPDMEM_DDR4_SODIMM		0x03
#define	SPDMEM_DDR4_LRDIMM		0x04
#define	SPDMEM_DDR4_MINI_RDIMM		0x05
#define	SPDMEM_DDR4_MINI_UDIMM		0x06
#define	SPDMEM_DDR4_LP_DIMM		0x07
#define	SPDMEM_DDR4_72B_SO_RDIMM	0x08
#define	SPDMEM_DDR4_72B_SO_UDIMM	0x09
#define	SPDMEM_DDR4_16B_SO_DIMM		0x0c
#define	SPDMEM_DDR4_32B_SO_DIMM		0x0d
#define	SPDMEM_DDR4_NON_DIMM		0x0e
#define	SPDMEM_DDR4_MODTYPE_MASK	0x0f
#define	SPDMEM_DDR4_MODTYPE_HYBRID	0x80

static const uint8_t ddr2_cycle_tenths[] = {
	0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 25, 33, 66, 75, 0, 0
};

#define SPDMEM_TYPE_MAXLEN 16

uint16_t	spdmem_crc16(struct spdmem_softc *, int);
static inline
uint8_t		spdmem_read(struct spdmem_softc *, uint8_t);
void		spdmem_sdram_decode(struct spdmem_softc *, struct spdmem *);
void		spdmem_rdr_decode(struct spdmem_softc *, struct spdmem *);
void		spdmem_ddr_decode(struct spdmem_softc *, struct spdmem *);
void		spdmem_ddr2_decode(struct spdmem_softc *, struct spdmem *);
void		spdmem_fbdimm_decode(struct spdmem_softc *, struct spdmem *);
void		spdmem_ddr3_decode(struct spdmem_softc *, struct spdmem *);

struct cfdriver spdmem_cd = {
	NULL, "spdmem", DV_DULL
};

#define IS_RAMBUS_TYPE (s->sm_len < 4)

static const char *spdmem_basic_types[] = {
	"unknown",
	"FPM",
	"EDO",
	"Pipelined Nibble",
	"SDRAM",
	"ROM",
	"DDR SGRAM",
	"DDR SDRAM",
	"DDR2 SDRAM",
	"DDR2 SDRAM FB-DIMM",
	"DDR2 SDRAM FB-DIMM Probe",
	"DDR3 SDRAM",
	"DDR4 SDRAM",
	"unknown",
	"DDR4E SDRAM",
	"LPDDR3 SDRAM",
	"LPDDR4 SDRAM",
	"LPDDR4X SDRAM",
	"DDR5 SDRAM",
	"LPDDR5 SDRAM"
};

static const char *spdmem_superset_types[] = {
	"unknown",
	"ESDRAM",
	"DDR ESDRAM",
	"PEM EDO",
	"PEM SDRAM"
};

static const char *spdmem_parity_types[] = {
	"non-parity",
	"data parity",
	"ECC",
	"data parity and ECC",
	"cmd/addr parity",
	"cmd/addr/data parity",
	"cmd/addr parity, data ECC",
	"cmd/addr/data parity, data ECC"
};

static inline uint8_t
spdmem_read(struct spdmem_softc *sc, uint8_t reg)
{
	return (*sc->sc_read)(sc, reg);
}

/* CRC functions used for certain memory types */
uint16_t
spdmem_crc16(struct spdmem_softc *sc, int count)
{
	uint16_t crc;
	int i, j;
	uint8_t val;
	crc = 0;
	for (j = 0; j <= count; j++) {
		val = spdmem_read(sc, j);
		crc = crc ^ val << 8;
		for (i = 0; i < 8; ++i)
			if (crc & 0x8000)
				crc = crc << 1 ^ 0x1021;
			else
				crc = crc << 1;
	}
	return (crc & 0xFFFF);
}

void
spdmem_sdram_decode(struct spdmem_softc *sc, struct spdmem *s)
{
	const char *type;
	int dimm_size, p_clk;
	int num_banks, per_chip;
	uint8_t rows, cols;

	type = spdmem_basic_types[s->sm_type];

	if (s->sm_data[SPDMEM_SDR_SUPERSET] == SPDMEM_SUPERSET_SDR_PEM)
		type = spdmem_superset_types[SPDMEM_SUPERSET_SDR_PEM];
	if (s->sm_data[SPDMEM_SDR_SUPERSET] == SPDMEM_SUPERSET_ESDRAM)
		type = spdmem_superset_types[SPDMEM_SUPERSET_ESDRAM];

	num_banks = s->sm_data[SPDMEM_SDR_BANKS];
	per_chip = s->sm_data[SPDMEM_SDR_BANKS_PER_CHIP];
	rows = s->sm_data[SPDMEM_SDR_ROWS] & 0x0f;
	cols = s->sm_data[SPDMEM_SDR_COLS] & 0x0f;
	dimm_size = (1 << (rows + cols - 17)) * num_banks * per_chip;

	if (dimm_size > 0) {
		if (dimm_size < 1024)
			printf(" %dMB", dimm_size);
		else
			printf(" %dGB", dimm_size / 1024);
	}

	printf(" %s", type);

	if (s->sm_data[SPDMEM_DDR_MOD_ATTRIB] & SPDMEM_DDR_ATTRIB_REG)
		printf(" registered");

	if (s->sm_data[SPDMEM_FPM_CONFIG] < 8)
		printf(" %s",
		    spdmem_parity_types[s->sm_data[SPDMEM_FPM_CONFIG]]);

	p_clk = 66;
	if (s->sm_len >= 128) {
		switch (spdmem_read(sc, SPDMEM_SDR_FREQUENCY)) {
		case SPDMEM_SDR_FREQ_100:
		case SPDMEM_SDR_FREQ_133:
			/* We need to check ns to decide here */
			if (s->sm_data[SPDMEM_SDR_CYCLE] < 0x80)
				p_clk = 133;
			else
				p_clk = 100;
			break;
		case SPDMEM_SDR_FREQ_66:
		default:
			p_clk = 66;
			break;
		}
	}
	printf(" PC%d", p_clk);

	/* Print CAS latency */
	if (s->sm_len < 128)
		return;
	if (spdmem_read(sc, SPDMEM_SDR_CAS) & SPDMEM_SDR_CAS2)
		printf("CL2");
	else if (spdmem_read(sc, SPDMEM_SDR_CAS) & SPDMEM_SDR_CAS3)
		printf("CL3");
}

void
spdmem_rdr_decode(struct spdmem_softc *sc, struct spdmem *s)
{
	int rimm_size;
	uint8_t row_bits, col_bits, bank_bits;

	row_bits = s->sm_data[SPDMEM_RDR_ROWS_COLS] >> 4;
	col_bits = s->sm_data[SPDMEM_RDR_ROWS_COLS] & 0x0f;
	bank_bits = s->sm_data[SPDMEM_RDR_BANK] & 0x07;

	/* subtracting 13 here is a cheaper way of dividing by 8k later */
	rimm_size = 1 << (row_bits + col_bits + bank_bits - 13);

	if (rimm_size < 1024)
		printf(" %dMB ", rimm_size);
	else
		printf(" %dGB ", rimm_size / 1024);

	switch(s->sm_data[SPDMEM_RDR_MODULE_TYPE]) {
	case SPDMEM_RDR_TYPE_RIMM:
		printf("RIMM");
		break;
	case SPDMEM_RDR_TYPE_SORIMM:
		printf("SO-RIMM");
		break;
	case SPDMEM_RDR_TYPE_EMBED:
		printf("Embedded Rambus");
		break;
	case SPDMEM_RDR_TYPE_RIMM32:
		printf("RIMM32");
		break;
	}
}

void
spdmem_ddr_decode(struct spdmem_softc *sc, struct spdmem *s)
{
	const char *type;
	int dimm_size, cycle_time, d_clk, p_clk, bits;
	int i, num_banks, per_chip;
	uint8_t config, rows, cols, cl;

	type = spdmem_basic_types[s->sm_type];

	if (s->sm_data[SPDMEM_DDR_SUPERSET] == SPDMEM_SUPERSET_DDR_ESDRAM)
		type = spdmem_superset_types[SPDMEM_SUPERSET_DDR_ESDRAM];

	num_banks = s->sm_data[SPDMEM_SDR_BANKS];
	per_chip = s->sm_data[SPDMEM_SDR_BANKS_PER_CHIP];
	rows = s->sm_data[SPDMEM_SDR_ROWS] & 0x0f;
	cols = s->sm_data[SPDMEM_SDR_COLS] & 0x0f;
	dimm_size = (1 << (rows + cols - 17)) * num_banks * per_chip;

	if (dimm_size > 0) {
		if (dimm_size < 1024)
			printf(" %dMB", dimm_size);
		else
			printf(" %dGB", dimm_size / 1024);
	}

	printf(" %s", type);

	if (s->sm_data[SPDMEM_DDR_MOD_ATTRIB] & SPDMEM_DDR_ATTRIB_REG)
		printf(" registered");

	if (s->sm_data[SPDMEM_FPM_CONFIG] < 8)
		printf(" %s",
		    spdmem_parity_types[s->sm_data[SPDMEM_FPM_CONFIG]]);

	/* cycle_time is expressed in units of 0.01 ns */
	cycle_time = (s->sm_data[SPDMEM_DDR_CYCLE] >> 4) * 100 +
	    (s->sm_data[SPDMEM_DDR_CYCLE] & 0x0f) * 10;

	if (cycle_time != 0) {
		/*
		 * cycle time is scaled by a factor of 100 to avoid using
		 * floating point.  Calculate memory speed as the number
		 * of cycles per microsecond.
		 * DDR uses dual-pumped clock
		 */
		d_clk = 100 * 1000 * 2;
		config = s->sm_data[SPDMEM_FPM_CONFIG];
		bits = s->sm_data[SPDMEM_DDR_DATAWIDTH] |
		    (s->sm_data[SPDMEM_DDR_DATAWIDTH + 1] << 8);
		if (config == 1 || config == 2)
			bits -= 8;

		d_clk /= cycle_time;
		p_clk = d_clk * bits / 8;
		if ((p_clk % 100) >= 50)
			p_clk += 50;
		p_clk -= p_clk % 100;
		printf(" PC%d", p_clk);
	}

	/* Print CAS latency */
	for (i = 6; i >= 0; i--) {
		if (s->sm_data[SPDMEM_DDR_CAS] & (1 << i)) {
			cl = ((i * 10) / 2) + 10;
			printf("CL%d.%d", cl / 10, cl % 10);
			break;
		}
	}
}

void
spdmem_ddr2_decode(struct spdmem_softc *sc, struct spdmem *s)
{
	const char *type;
	int dimm_size, cycle_time, d_clk, p_clk, bits;
	int i, num_ranks, density;
	uint8_t config;

	type = spdmem_basic_types[s->sm_type];

	num_ranks = (s->sm_data[SPDMEM_DDR2_RANKS] & 0x7) + 1;
	density = (s->sm_data[SPDMEM_DDR2_RANK_DENSITY] & 0xf0) |
	    ((s->sm_data[SPDMEM_DDR2_RANK_DENSITY] & 0x0f) << 8);
	dimm_size = num_ranks * density * 4;

	if (dimm_size > 0) {
		if (dimm_size < 1024)
			printf(" %dMB", dimm_size);
		else
			printf(" %dGB", dimm_size / 1024);
	}

	printf(" %s", type);

	if (s->sm_data[SPDMEM_DDR2_DIMMTYPE] & SPDMEM_DDR2_TYPE_REGMASK)
		printf(" registered");

	if (s->sm_data[SPDMEM_FPM_CONFIG] < 8)
		printf(" %s",
		    spdmem_parity_types[s->sm_data[SPDMEM_FPM_CONFIG]]);

	/* cycle_time is expressed in units of 0.01 ns */
	cycle_time = (s->sm_data[SPDMEM_DDR2_CYCLE] >> 4) * 100 +
	    ddr2_cycle_tenths[(s->sm_data[SPDMEM_DDR2_CYCLE] & 0x0f)];

	if (cycle_time != 0) {
		/*
		 * cycle time is scaled by a factor of 100 to avoid using
		 * floating point.  Calculate memory speed as the number
		 * of cycles per microsecond.
		 * DDR2 uses quad-pumped clock
		 */
		d_clk = 100 * 1000 * 4;
		config = s->sm_data[SPDMEM_FPM_CONFIG];
		bits = s->sm_data[SPDMEM_DDR2_DATAWIDTH];
		if ((config & 0x03) != 0)
			bits -= 8;
		d_clk /= cycle_time;
		d_clk = (d_clk + 1) / 2;
		p_clk = d_clk * bits / 8;
		p_clk -= p_clk % 100;
		printf(" PC2-%d", p_clk);
	}

	/* Print CAS latency */
	for (i = 7; i >= 2; i--) {
		if (s->sm_data[SPDMEM_DDR_CAS] & (1 << i)) {
			printf("CL%d", i);
			break;
		}
	}

	switch (s->sm_data[SPDMEM_DDR2_DIMMTYPE]) {
	case SPDMEM_DDR2_SODIMM:
		printf(" SO-DIMM");
		break;
	case SPDMEM_DDR2_MICRO_DIMM:
		printf(" Micro-DIMM");
		break;
	case SPDMEM_DDR2_MINI_RDIMM:
	case SPDMEM_DDR2_MINI_UDIMM:
		printf(" Mini-DIMM");
		break;
	}
}

void
spdmem_fbdimm_decode(struct spdmem_softc *sc, struct spdmem *s)
{
	int dimm_size, cycle_time, d_clk, p_clk, bits;
	uint8_t rows, cols, dividend, divisor;
	/*
	 * FB-DIMM is very much like DDR3
	 */

	cols = (s->sm_data[SPDMEM_FBDIMM_ADDR] & SPDMEM_FBDIMM_ADDR_COL) >>
	    SPDMEM_FBDIMM_ADDR_COL_SHIFT;
	rows = (s->sm_data[SPDMEM_FBDIMM_ADDR] & SPDMEM_FBDIMM_ADDR_ROW) >>
	    SPDMEM_FBDIMM_ADDR_ROW_SHIFT;
	dimm_size = rows + 12 + cols +  9 - 20 - 3;

	if (dimm_size < 1024)
		printf(" %dMB", dimm_size);
	else
		printf(" %dGB", dimm_size / 1024);

	dividend = s->sm_data[SPDMEM_FBDIMM_MTB_DIVIDEND];
	divisor = s->sm_data[SPDMEM_FBDIMM_MTB_DIVISOR];

	cycle_time = (1000 * dividend + (divisor / 2)) / divisor;

	if (cycle_time != 0) {
		/*
		 * cycle time is scaled by a factor of 1000 to avoid using
		 * floating point.  Calculate memory speed as the number
		 * of cycles per microsecond.
		 */
		d_clk = 1000 * 1000;

		/* DDR2 FB-DIMM uses a dual-pumped clock */
		d_clk *= 2;
		bits = 1 << ((s->sm_data[SPDMEM_FBDIMM_RANKS] &
		    SPDMEM_FBDIMM_RANKS_WIDTH) + 2);

		p_clk = (d_clk * bits) / 8 / cycle_time;
		p_clk -= p_clk % 100;
		printf(" PC2-%d", p_clk);
	}
}

void
spdmem_ddr3_decode(struct spdmem_softc *sc, struct spdmem *s)
{
	const char *type;
	int dimm_size, cycle_time, d_clk, p_clk, bits;
	uint8_t mtype, chipsize, dividend, divisor;
	uint8_t datawidth, chipwidth, physbanks;

	type = spdmem_basic_types[s->sm_type];

	chipsize = s->sm_data[SPDMEM_DDR3_DENSITY] &
	    SPDMEM_DDR3_DENSITY_CAPMASK;
	datawidth = s->sm_data[SPDMEM_DDR3_DATAWIDTH] &
	    SPDMEM_DDR3_DATAWIDTH_PRIMASK;
	chipwidth = s->sm_data[SPDMEM_DDR3_MOD_ORG] &
	    SPDMEM_DDR3_MOD_ORG_CHIPWIDTH_MASK;
	physbanks = (s->sm_data[SPDMEM_DDR3_MOD_ORG] >> 
	    SPDMEM_DDR3_MOD_ORG_BANKS_SHIFT) & SPDMEM_DDR3_MOD_ORG_BANKS_MASK;

	dimm_size = (chipsize + 28 - 20) - 3 + (datawidth + 3) -
	    (chipwidth + 2);
	dimm_size = (1 << dimm_size) * (physbanks + 1);

	if (dimm_size < 1024)
		printf(" %dMB", dimm_size);
	else
		printf(" %dGB", dimm_size / 1024);

	printf(" %s", type);

	mtype = s->sm_data[SPDMEM_DDR3_MODTYPE];
	if (mtype == SPDMEM_DDR3_RDIMM || mtype == SPDMEM_DDR3_MINI_RDIMM)
		printf(" registered");

	if (s->sm_data[SPDMEM_DDR3_DATAWIDTH] & SPDMEM_DDR3_DATAWIDTH_ECCMASK) 
		printf(" ECC");

	dividend = s->sm_data[SPDMEM_DDR3_MTB_DIVIDEND];
	divisor = s->sm_data[SPDMEM_DDR3_MTB_DIVISOR];
	cycle_time = (1000 * dividend +  (divisor / 2)) / divisor;
	cycle_time *= s->sm_data[SPDMEM_DDR3_TCKMIN];

	if (cycle_time != 0) {
		/*
		 * cycle time is scaled by a factor of 1000 to avoid using
		 * floating point.  Calculate memory speed as the number
		 * of cycles per microsecond.
		 * DDR3 uses a dual-pumped clock
		 */
		d_clk = 1000 * 1000;
		d_clk *= 2;
		bits = 1 << ((s->sm_data[SPDMEM_DDR3_DATAWIDTH] &
		    SPDMEM_DDR3_DATAWIDTH_PRIMASK) + 3);
		/*
		 * Calculate p_clk first, since for DDR3 we need maximum
		 * significance.  DDR3 rating is not rounded to a multiple
		 * of 100.  This results in cycle_time of 1.5ns displayed
		 * as p_clk PC3-10666 (d_clk DDR3-1333)
		 */
		p_clk = (d_clk * bits) / 8 / cycle_time;
		p_clk -= (p_clk % 100);
		d_clk = ((d_clk + cycle_time / 2) ) / cycle_time;
		printf(" PC3-%d", p_clk);
	}

	switch (s->sm_data[SPDMEM_DDR3_MODTYPE]) {
	case SPDMEM_DDR3_SODIMM:
		printf(" SO-DIMM");
		break;
	case SPDMEM_DDR3_MICRO_DIMM:
		printf(" Micro-DIMM");
		break;
	case SPDMEM_DDR3_MINI_RDIMM:
	case SPDMEM_DDR3_MINI_UDIMM:
		printf(" Mini-DIMM");
		break;
	}

	if (s->sm_data[SPDMEM_DDR3_THERMAL] & SPDMEM_DDR3_THERMAL_PRESENT)
		printf(" with thermal sensor");
}

void
spdmem_ddr4_decode(struct spdmem_softc *sc, struct spdmem *s)
{
	static const int ddr4_chipsize[16] = { 256, 512, 1024, 2048, 4096,
	    8 * 1024, 16 * 1024, 32 * 1024, 12 * 1024, 24 * 1024,
	    3 * 1024, 6 * 1024, 18 * 1024 };
	const char *type;
	int dimm_size, cycle_time, d_clk, p_clk, bits;
	uint8_t mtype, chipsize, mtb;
	int8_t ftb;
	uint8_t datawidth, chipwidth, physbanks, diecount = 0;

	type = spdmem_basic_types[s->sm_type];

	chipsize = s->sm_data[SPDMEM_DDR4_DENSITY] &
	    SPDMEM_DDR4_DENSITY_CAPMASK;
	datawidth = s->sm_data[SPDMEM_DDR4_DATAWIDTH] &
	    SPDMEM_DDR4_DATAWIDTH_PRIMASK;
	chipwidth = s->sm_data[SPDMEM_DDR4_MOD_ORG] &
	    SPDMEM_DDR4_MOD_ORG_CHIPWIDTH_MASK;
	physbanks = (s->sm_data[SPDMEM_DDR4_MOD_ORG] >> 
	    SPDMEM_DDR4_MOD_ORG_BANKS_SHIFT) & SPDMEM_DDR4_MOD_ORG_BANKS_MASK;

	if ((s->sm_data[SPDMEM_DDR4_PACK_TYPE] &
	    SPDMEM_DDR4_PACK_TYPE_SIG_LOAD_MASK) ==
	    SPDMEM_DDR4_PACK_TYPE_SIG_SINGLE_LOAD) {
		diecount = (s->sm_data[SPDMEM_DDR4_PACK_TYPE] >>
		    SPDMEM_DDR4_PACK_TYPE_DIE_COUNT_SHIFT) &
		    SPDMEM_DDR4_PACK_TYPE_DIE_COUNT_MASK;
	}

	dimm_size = (datawidth + 3) - (chipwidth + 2);
	dimm_size = (ddr4_chipsize[chipsize] / 8) * (1 << dimm_size) *
	    (physbanks + 1) * (diecount + 1);

	if (dimm_size < 1024)
		printf(" %dMB", dimm_size);
	else
		printf(" %dGB", dimm_size / 1024);

	printf(" %s", type);

	mtype = s->sm_data[SPDMEM_DDR4_MODTYPE];
	if (mtype & SPDMEM_DDR4_MODTYPE_HYBRID)
		printf(" hybrid");
	mtype &= SPDMEM_DDR4_MODTYPE_MASK;
	if (mtype == SPDMEM_DDR4_RDIMM || mtype == SPDMEM_DDR4_MINI_RDIMM ||
	    mtype == SPDMEM_DDR4_72B_SO_RDIMM)
		printf(" registered");
	if (mtype == SPDMEM_DDR4_72B_SO_UDIMM ||
	    mtype == SPDMEM_DDR4_72B_SO_RDIMM)
		printf(" 72-bit");
	if (mtype == SPDMEM_DDR4_32B_SO_DIMM)
		printf(" 32-bit");
	if (mtype == SPDMEM_DDR4_16B_SO_DIMM)
		printf(" 16-bit");

	if (s->sm_data[SPDMEM_DDR4_DATAWIDTH] & SPDMEM_DDR4_DATAWIDTH_ECCMASK) 
		printf(" ECC");

	mtb = s->sm_data[SPDMEM_DDR4_TCKMIN_MTB];
	/* SPDMEM_DDR4_TCKMIN_FTB (addr 125) is outside of s->sm_data */
	ftb = spdmem_read(sc, SPDMEM_DDR4_TCKMIN_FTB);
	cycle_time = mtb * 125 + ftb; /* in ps */

	if (cycle_time != 0) {
		/*
		 * cycle time is scaled by a factor of 1000 to avoid using
		 * floating point.  Calculate memory speed as the number
		 * of cycles per microsecond.
		 * DDR4 uses a dual-pumped clock
		 */
		d_clk = 1000 * 1000;
		d_clk *= 2;
		bits = 1 << ((s->sm_data[SPDMEM_DDR4_DATAWIDTH] &
		    SPDMEM_DDR4_DATAWIDTH_PRIMASK) + 3);

		p_clk = (d_clk * bits) / 8 / cycle_time;
		p_clk -= (p_clk % 100);
		printf(" PC4-%d", p_clk);
	}

	switch (s->sm_data[SPDMEM_DDR4_MODTYPE] & SPDMEM_DDR4_MODTYPE_MASK) {
	case SPDMEM_DDR4_SODIMM:
	case SPDMEM_DDR4_72B_SO_RDIMM:
	case SPDMEM_DDR4_72B_SO_UDIMM:
	case SPDMEM_DDR4_16B_SO_DIMM:
	case SPDMEM_DDR4_32B_SO_DIMM:
		printf(" SO-DIMM");
		break;
	case SPDMEM_DDR4_LRDIMM:
		printf(" LR-DIMM");
		break;
	case SPDMEM_DDR4_MINI_RDIMM:
	case SPDMEM_DDR4_MINI_UDIMM:
		printf(" Mini-DIMM");
		break;
	case SPDMEM_DDR4_LP_DIMM:
		printf(" LP-DIMM");
		break;
	case SPDMEM_DDR4_NON_DIMM:
		printf(" non-DIMM solution");
		break;
	}

	if (s->sm_data[SPDMEM_DDR4_THERMAL] & SPDMEM_DDR4_THERMAL_PRESENT)
		printf(" with thermal sensor");
}

int
spdmem_probe(struct spdmem_softc *sc)
{
	uint8_t i, val, type;
	int cksum = 0;
	int spd_len, spd_crc_cover;
	uint16_t crc_calc, crc_spd;

	type = spdmem_read(sc, 2);
	/* For older memory types, validate the checksum over 1st 63 bytes */
	if (type <= SPDMEM_MEMTYPE_DDR2SDRAM) {
		for (i = 0; i < 63; i++)
			cksum += spdmem_read(sc, i);

		val = spdmem_read(sc, 63);

		if (cksum == 0 || (cksum & 0xff) != val) {
			return 0;
		} else
			return 1;
	}

	/* For DDR3 and FBDIMM, verify the CRC */
	else if (type <= SPDMEM_MEMTYPE_DDR3SDRAM) {
		spd_len = spdmem_read(sc, 0);
		if (spd_len & SPDMEM_SPDCRC_116)
			spd_crc_cover = 116;
		else
			spd_crc_cover = 125;
		switch (spd_len & SPDMEM_SPDLEN_MASK) {
		case SPDMEM_SPDLEN_128:
			spd_len = 128;
			break;
		case SPDMEM_SPDLEN_176:
			spd_len = 176;
			break;
		case SPDMEM_SPDLEN_256:
			spd_len = 256;
			break;
		default:
			return 0;
		}
calc_crc:
		if (spd_crc_cover > spd_len)
			return 0;
		crc_calc = spdmem_crc16(sc, spd_crc_cover);
		crc_spd = spdmem_read(sc, 127) << 8;
		crc_spd |= spdmem_read(sc, 126);
		if (crc_calc != crc_spd) {
			return 0;
		}
		return 1;
	} else if (type <= SPDMEM_MEMTYPE_LPDDR4SDRAM) {
		spd_len = spdmem_read(sc, 0);
		spd_crc_cover = 125;
		switch (spd_len & SPDMEM_DDR4_SPDLEN_MASK) {
		case SPDMEM_DDR4_SPDLEN_128:
			spd_len = 128;
			break;
		case SPDMEM_DDR4_SPDLEN_256:
			spd_len = 256;
			break;
		case SPDMEM_DDR4_SPDLEN_384:
			spd_len = 384;
			break;
		case SPDMEM_DDR4_SPDLEN_512:
			spd_len = 512;
			break;
		default:
			return 0;
		}
		goto calc_crc;
	}

	return 0;
}

void
spdmem_attach_common(struct spdmem_softc *sc)
{
	struct spdmem *s = &(sc->sc_spd_data);
	int i;

	/* All SPD have at least 64 bytes of data including checksum */
	for (i = 0; i < 64; i++) {
		((uint8_t *)s)[i] = spdmem_read(sc, i);
	}

	/*
	 * Decode and print SPD contents
	 */
	if (s->sm_len < 4) {
		if (s->sm_type == SPDMEM_MEMTYPE_DIRECT_RAMBUS)
			spdmem_rdr_decode(sc, s);
		else
			printf(" no decode method for Rambus memory");
	} else {
		switch(s->sm_type) {
		case SPDMEM_MEMTYPE_EDO:
		case SPDMEM_MEMTYPE_SDRAM:
			spdmem_sdram_decode(sc, s);
			break;
		case SPDMEM_MEMTYPE_DDRSDRAM:
			spdmem_ddr_decode(sc, s);
			break;
		case SPDMEM_MEMTYPE_DDR2SDRAM:
			spdmem_ddr2_decode(sc, s);
			break;
		case SPDMEM_MEMTYPE_FBDIMM:
		case SPDMEM_MEMTYPE_FBDIMM_PROBE:
			spdmem_fbdimm_decode(sc, s);
			break;
		case SPDMEM_MEMTYPE_DDR3SDRAM:
			spdmem_ddr3_decode(sc, s);
			break;
		case SPDMEM_MEMTYPE_DDR4SDRAM:
		case SPDMEM_MEMTYPE_DDR4ESDRAM:
		case SPDMEM_MEMTYPE_LPDDR3SDRAM:
		case SPDMEM_MEMTYPE_LPDDR4SDRAM:
			spdmem_ddr4_decode(sc, s);
			break;
		case SPDMEM_MEMTYPE_NONE:
			printf(" no EEPROM found");
			break;
		default:
			if (s->sm_type <= SPDMEM_MEMTYPE_LPDDR5SDRAM)
				printf(" no decode method for %s memory",
				    spdmem_basic_types[s->sm_type]);
			else
				printf(" unknown memory type %d", s->sm_type);
			break;
		}
	}

	printf("\n");
}
