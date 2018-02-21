/*
 * Copyright (C) ST-Ericsson SA 2010-2011
 * Author: Sebastian Rasmussen <sebastian.rasmussen@stericsson.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials
 *      provided with the distribution.
 *   3. Neither the name of the ST-Ericsson SA nor the names of its
 *      contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mmc.h"

#define MASKTOBIT0(high)	\
	((high >= 0) ? ((1ull << ((high) + 1ull)) - 1ull) : 0ull)
#define MASK(high, low)		(MASKTOBIT0(high) & ~MASKTOBIT0(low - 1))
#define BITS(value, high, low)	(((value) & MASK((high), (low))) >> (low))
#define IDS_MAX			256

struct config {
	char *idsfile;
	char *dir;
	bool verbose;
	int interfaces;
	char **interface;
	char **mmc_ids;
	char **sd_ids;

	char *type;
	char *cid;
	char *csd;
	char *scr;
	char *ext_csd;
};

enum REG_TYPE {
	CID = 0,
	CSD,
	SCR,
	EXT_CSD,
};

struct ids_database {
	char *type;
	int id;
	char *manufacturer;
};

struct ids_database database[] = {
	{
		.type = "sd",
		.id = 0x01,
		.manufacturer = "Panasonic",
	},
	{
		.type = "sd",
		.id = 0x02,
		.manufacturer = "Toshiba/Kingston/Viking",
	},
	{
		.type = "sd",
		.id = 0x03,
		.manufacturer = "SanDisk",
	},
	{
		.type = "sd",
		.id = 0x08,
		.manufacturer = "Silicon Power",
	},
	{
		.type = "sd",
		.id = 0x18,
		.manufacturer = "Infineon",
	},
	{
		.type = "sd",
		.id = 0x1b,
		.manufacturer = "Transcend",
	},
	{
		.type = "sd",
		.id = 0x1c,
		.manufacturer = "Transcend",
	},
	{
		.type = "sd",
		.id = 0x1d,
		.manufacturer = "Corsair",
	},
	{
		.type = "sd",
		.id = 0x1e,
		.manufacturer = "Transcend",
	},
	{
		.type = "sd",
		.id = 0x1f,
		.manufacturer = "Kingston",
	},
	{
		.type = "sd",
		.id = 0x28,
		.manufacturer = "Lexar",
	},
	{
		.type = "sd",
		.id = 0x30,
		.manufacturer = "SanDisk",
	},
	{
		.type = "sd",
		.id = 0x33,
		.manufacturer = "STMicroelectronics",
	},
	{
		.type = "sd",
		.id = 0x41,
		.manufacturer = "Kingston",
	},
	{
		.type = "sd",
		.id = 0x6f,
		.manufacturer = "STMicroelectronics",
	},
	{
		.type = "sd",
		.id = 0x89,
		.manufacturer = "Unknown",
	},
	{
		.type = "mmc",
		.id = 0x00,
		.manufacturer = "SanDisk",
	},
	{
		.type = "mmc",
		.id = 0x02,
		.manufacturer = "Kingston/SanDisk",
	},
	{
		.type = "mmc",
		.id = 0x03,
		.manufacturer = "Toshiba",
	},
	{
		.type = "mmc",
		.id = 0x05,
		.manufacturer = "Unknown",
	},
	{
		.type = "mmc",
		.id = 0x06,
		.manufacturer = "Unknown",
	},
	{
		.type = "mmc",
		.id = 0x11,
		.manufacturer = "Toshiba",
	},
	{
		.type = "mmc",
		.id = 0x15,
		.manufacturer = "Samsung/SanDisk/LG",
	},
	{
		.type = "mmc",
		.id = 0x37,
		.manufacturer = "KingMax",
	},
	{
		.type = "mmc",
		.id = 0x44,
		.manufacturer = "SanDisk",
	},
	{
		.type = "mmc",
		.id = 0x2c,
		.manufacturer = "Kingston",
	},
	{
		.type = "mmc",
		.id = 0x70,
		.manufacturer = "Kingston",
	},
};

/* Command line parsing functions */
void usage(void)
{
	printf("Usage: print mmc [-h] [-v] <device path ...>\n");
	printf("\n");
	printf("Options:\n");
	printf("\t-h\tShow this help.\n");
	printf("\t-v\tEnable verbose mode.\n");
}

int parse_opts(int argc, char **argv, struct config *config)
{
	int c;

	while ((c = getopt(argc, argv, "hv")) != -1) {
		switch (c) {
		case 'h':
			usage();
			return -1;
		case 'v':
			config->verbose = true;
			break;
		case '?':
			fprintf(stderr,
				"Unknown option '%c' encountered.\n\n", c);
			usage();
			return -1;
		case ':':
			fprintf(stderr,
				"Argument for option '%c' missing.\n\n", c);
			usage();
			return -1;
		default:
			fprintf(stderr,
				"Unimplemented option '%c' encountered.\n", c);
			break;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Expected mmc directory arguments.\n\n");
		usage();
		return -1;
	}

	config->dir = strdup(argv[optind]);
	return 0;
}

int parse_ids(struct config *config)
{
	unsigned int ids_cnt = sizeof(database) / sizeof(struct ids_database);
	unsigned int value;
	char **ids;
	char *type;
	int i;

	for (i = 0; i < ids_cnt; i++) {
		type = database[i].type;

		if (!strcmp(type, "mmc")) {
			ids = config->mmc_ids;
		} else if (!strcmp(type, "sd")) {
			ids = config->sd_ids;
		} else {
			fprintf(stderr,
				"MMC/SD id parse error, unknown type: '%s'.\n",
				type);
			return -1;
		}

		value = database[i].id;

		if (value >= IDS_MAX) {
			fprintf(stderr,
				"MMC/SD id parse error, id out of range.\n");
			return -1;
		}

		if (ids[value]) {
			fprintf(stderr,
				"Duplicate entries: type='%s', id='0x%1x'.\n",
				type, value);
			return -1;
		}

		ids[value] = database[i].manufacturer;
	}

	return 0;
}

/* MMC/SD file parsing functions */
char *read_file(char *name)
{
	char *preparsed;
	char line[4096];
	FILE *f;

	f = fopen(name, "r");
	if (!f) {
		fprintf(stderr, "Could not open MMC/SD file '%s'.\n", name);
		return NULL;
	}

	preparsed = fgets(line, sizeof(line), f);
	if (!preparsed) {
		if (ferror(f))
			fprintf(stderr, "Could not read MMC/SD file '%s'.\n",
				name);
		else
			fprintf(stderr,
				"Could not read data from MMC/SD file '%s'.\n",
				name);

		if (fclose(f))
			fprintf(stderr, "Could not close MMC/SD file '%s'.\n",
				name);
		return NULL;
	}

	if (fclose(f)) {
		fprintf(stderr, "Could not close MMC/SD file '%s'.\n", name);
		return NULL;
	}

	line[sizeof(line) - 1] = '\0';

	while (isspace(line[strlen(line) - 1]))
		line[strlen(line) - 1] = '\0';

	while (isspace(line[0]))
		strncpy(&line[0], &line[1], sizeof(line));

	return strdup(line);
}

/* Hexadecimal string parsing functions */
char *to_binstr(char *hexstr)
{
	char *bindigits[] = {
		"0000", "0001", "0010", "0011", "0100", "0101", "0110", "0111",
		"1000", "1001", "1010", "1011", "1100", "1101", "1110", "1111",
	};
	char *binstr;

	binstr = calloc(strlen(hexstr) * 4 + 1, sizeof(char));

	while (hexstr && *hexstr != '\0') {
		if (!isxdigit(*hexstr))
			return NULL;

		if (isdigit(*hexstr))
			strcat(binstr, bindigits[*hexstr - '0']);
		else if (islower(*hexstr))
			strcat(binstr, bindigits[*hexstr - 'a']);
		else
			strcat(binstr, bindigits[*hexstr - 'A']);

		hexstr++;
	}

	return binstr;
}

void bin_to_unsigned(unsigned int *u, char *binstr, int width)
{
	*u = 0;
	assert(width <= 32);

	while (binstr && *binstr != '\0' && width > 0) {
		*u <<= 1;
		*u |= *binstr == '0' ? 0 : 1;

		binstr++;
		width--;
	}
}

void bin_to_ascii(char *a, char *binstr, int width)
{
	assert(width % 8 == 0);
	*a = '\0';

	while (binstr && *binstr != '\0' && width > 0) {
		unsigned int u;
		char c[2] = { '\0', '\0' };
		char *s = &c[0];

		bin_to_unsigned(&u, binstr, 8);
		c[0] = u;

		strcat(a, s);
		binstr += 8;
		width -= 8;
	}
}

void parse_bin(char *hexstr, char *fmt, ...)
{
	va_list args;
	char *origstr;
	char *binstr;
	unsigned long width = 0;

	binstr = to_binstr(hexstr);
	origstr = binstr;

	va_start(args, fmt);

	while (binstr && fmt && *fmt != '\0') {
		if (isdigit(*fmt)) {
			char *rest;

			errno = 0;
			width = strtoul(fmt, &rest, 10);
			if (width == ULONG_MAX && errno != 0)
				fprintf(stderr, "strtoul()");
			fmt = rest;
		} else if (*fmt == 'u') {
			unsigned int *u = va_arg(args, unsigned int *);

			if (u)
				bin_to_unsigned(u, binstr, width);
			binstr += width;
			width = 0;
			fmt++;
		} else if (*fmt == 'r') {
			binstr += width;
			width = 0;
			fmt++;
		} else if (*fmt == 'a') {
			char *c = va_arg(args, char *);

			if (c)
				bin_to_ascii(c, binstr, width);
			binstr += width;
			width = 0;
			fmt++;
		} else {
			fmt++;
		}
	}

	va_end(args);
	free(origstr);
}

/* MMC/SD information parsing functions */
void print_sd_cid(struct config *config, char *cid)
{
	static const char *months[] = {
		"jan", "feb", "mar", "apr", "may", "jun",
		"jul", "aug", "sep", "oct", "nov", "dec",
		"invalid0", "invalid1", "invalid2", "invalid3",
	};
	unsigned int mid;
	char oid[3];
	char pnm[6];
	unsigned int prv_major;
	unsigned int prv_minor;
	unsigned int psn;
	unsigned int mdt_month;
	unsigned int mdt_year;
	unsigned int crc;

	parse_bin(cid, "8u16a40a4u4u32u4r8u4u7u1r",
		&mid, &oid[0], &pnm[0], &prv_major, &prv_minor, &psn,
		&mdt_year, &mdt_month, &crc);

	oid[2] = '\0';
	pnm[5] = '\0';

	if (config->verbose) {
		printf("======SD/CID======\n");

		printf("\tMID: 0x%02x (", mid);
		if (config->sd_ids[mid])
			printf("%s)\n", config->sd_ids[mid]);
		else
			printf("Unlisted)\n");

		printf("\tOID: %s\n", oid);
		printf("\tPNM: %s\n", pnm);
		printf("\tPRV: 0x%01x%01x ", prv_major, prv_minor);
		printf("(%d.%d)\n", prv_major, prv_minor);
		printf("\tPSN: 0x%08x\n", psn);
		printf("\tMDT: 0x%02x%01x %d %s\n", mdt_year, mdt_month,
		       2000 + mdt_year, months[mdt_month]);
		printf("\tCRC: 0x%02x\n", crc);
	} else {
		if (config->sd_ids[mid])
			printf("manufacturer: '%s' '%s'\n",
			       config->sd_ids[mid], oid);
		else
			printf("manufacturer: 'Unlisted' '%s'\n", oid);

		printf("product: '%s' %d.%d\n", pnm, prv_major, prv_minor);
		printf("serial: 0x%08x\n", psn);
		printf("manfacturing date: %d %s\n", 2000 + mdt_year,
		       months[mdt_month]);
	}
}

void print_mmc_cid(struct config *config, char *cid)
{
	static const char *months[] = {
		"jan", "feb", "mar", "apr", "may", "jun",
		"jul", "aug", "sep", "oct", "nov", "dec",
		"invalid0", "invalid1", "invalid2", "invalid3",
	};
	unsigned int mid;
	unsigned int cbx;
	unsigned int oid;
	char pnm[7];
	unsigned int prv_major;
	unsigned int prv_minor;
	unsigned int psn;
	unsigned int mdt_month;
	unsigned int mdt_year;
	unsigned int crc;

	parse_bin(cid, "8u6r2u8u48a4u4u32u4u4u7u1r",
		&mid, &cbx, &oid, &pnm[0], &psn, &prv_major, &prv_minor,
		&mdt_year, &mdt_month, &crc);

	pnm[6] = '\0';

	if (config->verbose) {
		printf("======MMC/CID======\n");

		printf("\tMID: 0x%02x (", mid);
		if (config->mmc_ids[mid])
			printf("%s)\n", config->mmc_ids[mid]);
		else
			printf("Unlisted)\n");

		printf("\tCBX: 0x%01x (", cbx);
		switch (cbx) {
		case 0:
			printf("card)\n");
			break;
		case 1:
			printf("BGA)\n");
			break;
		case 2:
			printf("PoP)\n");
			break;
		case 3:
			printf("reserved)\n");
			break;
		}

		printf("\tOID: 0x%01x\n", oid);
		printf("\tPNM: %s\n", pnm);
		printf("\tPRV: 0x%01x%01x ", prv_major, prv_minor);
		printf("(%d.%d)\n", prv_major, prv_minor);
		printf("\tPSN: 0x%08x\n", psn);
		printf("\tMDT: 0x%01x%01x %d %s\n", mdt_month, mdt_year,
		       1997 + mdt_year, months[mdt_month]);
		printf("\tCRC: 0x%02x\n", crc);
	} else {
		if (config->mmc_ids[mid])
			printf("manufacturer: '%s' '%c'\n",
			       config->mmc_ids[mid], oid);
		else
			printf("manufacturer: 'Unlisted' '%c'\n", oid);

		printf("product: '%s' %d.%d\n", pnm, prv_major, prv_minor);
		printf("serial: 0x%08x\n", psn);
		printf("manfacturing date: %d %s\n", 1997 + mdt_year,
		       months[mdt_month]);
	}
}

void print_sd_csd(struct config *config, char *csd)
{
	unsigned int csd_structure;
	unsigned int taac_timevalue;
	unsigned int taac_timeunit;
	unsigned int nsac;
	unsigned int tran_speed_timevalue;
	unsigned int tran_speed_transferrateunit;
	unsigned int ccc;
	unsigned int read_bl_len;
	unsigned int read_bl_partial;
	unsigned int write_blk_misalign;
	unsigned int read_blk_misalign;
	unsigned int dsr_imp;
	unsigned int c_size;
	unsigned int vdd_r_curr_min;
	unsigned int vdd_r_curr_max;
	unsigned int vdd_w_curr_min;
	unsigned int vdd_w_curr_max;
	unsigned int c_size_mult;
	unsigned int erase_blk_en;
	unsigned int sector_size;
	unsigned int wp_grp_size;
	unsigned int wp_grp_enable;
	unsigned int r2w_factor;
	unsigned int write_bl_len;
	unsigned int write_bl_partial;
	unsigned int file_format_grp;
	unsigned int copy;
	unsigned int perm_write_protect;
	unsigned int tmp_write_protect;
	unsigned int file_format;
	unsigned int crc;
	unsigned int taac;
	unsigned int tran_speed;

	parse_bin(csd, "2u", &csd_structure);

	if (csd_structure == 0) {
		parse_bin(csd, "2u6r1r4u3u8u1r4u3u12u4u1u1u1u1u2r12u3u3u3u3u3u"
			  "1u7u7u1u2r3u4u1u5r1u1u1u1u2u2r7u1r",
			  NULL, &taac_timevalue, &taac_timeunit, &nsac,
			  &tran_speed_timevalue,
			  &tran_speed_transferrateunit, &ccc,
			  &read_bl_len, &read_bl_partial,
			  &write_blk_misalign, &read_blk_misalign,
			  &dsr_imp, &c_size, &vdd_r_curr_min,
			  &vdd_r_curr_max, &vdd_w_curr_min,
			  &vdd_w_curr_max, &c_size_mult, &erase_blk_en,
			  &sector_size, &wp_grp_size, &wp_grp_enable,
			  &r2w_factor, &write_bl_len, &write_bl_partial,
			  &file_format_grp, &copy, &perm_write_protect,
			  &tmp_write_protect, &file_format, &crc);
	} else if (csd_structure == 1) {
		parse_bin(csd, "2u6r1r4u3u8u1r4u3u12u4u1u1u1u1u6r22u1r1u7u7u1u"
			  "2r3u4u1u5r1u1u1u1u2u2r7u1r",
			  NULL, &taac_timevalue, &taac_timeunit, &nsac,
			  &tran_speed_timevalue,
			  &tran_speed_transferrateunit, &ccc,
			  &read_bl_len, &read_bl_partial,
			  &write_blk_misalign, &read_blk_misalign,
			  &dsr_imp, &c_size, &erase_blk_en, &sector_size,
			  &wp_grp_size, &wp_grp_enable, &r2w_factor,
			  &write_bl_len, &write_bl_partial,
			  &file_format_grp, &copy, &perm_write_protect,
			  &tmp_write_protect, &file_format, &crc);

		vdd_r_curr_min = 0;
		c_size_mult = 0;
	} else {
		printf("Unknown CSD structure: 0x%1x\n", csd_structure);
		return;
	}

	taac = taac_timevalue << 3 | taac_timeunit;
	tran_speed = tran_speed_timevalue << 3 | tran_speed_transferrateunit;

	if (config->verbose) {
		float value;
		unsigned long long blocks = 0;
		int block_size = 0;
		unsigned long long memory_capacity;

		printf("======SD/CSD======\n");

		printf("\tCSD_STRUCTURE: %d\n", csd_structure);
		printf("\tTAAC: 0x%02x (", taac);

		switch (taac_timevalue) {
		case 0x0:
			value = 0.0f;
			break;
		case 0x1:
			value = 1.0f;
			break;
		case 0x2:
			value = 1.2f;
			break;
		case 0x3:
			value = 1.3f;
			break;
		case 0x4:
			value = 1.5f;
			break;
		case 0x5:
			value = 2.0f;
			break;
		case 0x6:
			value = 2.5f;
			break;
		case 0x7:
			value = 3.0f;
			break;
		case 0x8:
			value = 3.5f;
			break;
		case 0x9:
			value = 4.0f;
			break;
		case 0xa:
			value = 4.5f;
			break;
		case 0xb:
			value = 5.0f;
			break;
		case 0xc:
			value = 5.5f;
			break;
		case 0xd:
			value = 6.0f;
			break;
		case 0xe:
			value = 7.0f;
			break;
		case 0xf:
			value = 8.0f;
			break;
		default:
			value = 0.0f;
			break;
		}

		switch (taac_timeunit) {
		case 0x0:
			printf("%.2fns)\n", value * 1.0f);
			break;
		case 0x1:
			printf("%.2fns)\n", value * 10.0f);
			break;
		case 0x2:
			printf("%.2fns)\n", value * 100.0f);
			break;
		case 0x3:
			printf("%.2fus)\n", value * 1.0f);
			break;
		case 0x4:
			printf("%.2fus)\n", value * 10.0f);
			break;
		case 0x5:
			printf("%.2fus)\n", value * 100.0f);
			break;
		case 0x6:
			printf("%.2fms)\n", value * 1.0f);
			break;
		case 0x7:
			printf("%.2fms)\n", value * 10.0f);
			break;
		}

		if (csd_structure == 1 && taac != 0x0e)
			printf("Warn: Invalid TAAC (should be 0x0e)\n");

		printf("\tNSAC: %d clocks\n", nsac);
		if (csd_structure == 1 && nsac != 0x00)
			printf("Warn: Invalid NSAC (should be 0x00)\n");

		printf("\tTRAN_SPEED: 0x%02x (", tran_speed);
		switch (tran_speed_timevalue) {
		case 0x0:
			value = 0.0f;
			break;
		case 0x1:
			value = 1.0f;
			break;
		case 0x2:
			value = 1.2f;
			break;
		case 0x3:
			value = 1.3f;
			break;
		case 0x4:
			value = 1.5f;
			break;
		case 0x5:
			value = 2.0f;
			break;
		case 0x6:
			value = 2.5f;
			break;
		case 0x7:
			value = 3.0f;
			break;
		case 0x8:
			value = 3.5f;
			break;
		case 0x9:
			value = 4.0f;
			break;
		case 0xa:
			value = 4.5f;
			break;
		case 0xb:
			value = 5.0f;
			break;
		case 0xc:
			value = 5.5f;
			break;
		case 0xd:
			value = 6.0f;
			break;
		case 0xe:
			value = 7.0f;
			break;
		case 0xf:
			value = 8.0f;
			break;
		default:
			value = 0.0f;
			break;
		}

		switch (tran_speed_transferrateunit) {
		case 0x0:
			printf("%.2fkbit/s)\n", value * 100.0f);
			break;
		case 0x1:
			printf("%.2fMbit/s)\n", value * 1.0f);
			break;
		case 0x2:
			printf("%.2fMbit/s)\n", value * 10.0f);
			break;
		case 0x3:
			printf("%.2fMbit/s)\n", value * 100.0f);
			break;
		default:
			printf("reserved)\n");
			break;
		}
		if (csd_structure == 0 &&
		    (tran_speed != 0x32 && tran_speed != 0x5a))
			printf("Warn: Invalid TRAN_SPEED "
			       "(should be 0x32 or 0x5a)\n");
		if (csd_structure == 1 && tran_speed != 0x32 &&
		    tran_speed != 0x5a && tran_speed != 0x0b &&
		    tran_speed != 0x2b)
			printf("Warn: Invalid TRAN_SPEED "
			       "(should be 0x32, 0x5a, 0x0b or 0x2b\n");

		printf("\tCCC: 0x%03x (class: ", ccc);
		if (ccc & 0x800)
			printf("11, ");
		if (ccc & 0x400)
			printf("10, ");
		if (ccc & 0x200)
			printf("9, ");
		if (ccc & 0x100)
			printf("8, ");
		if (ccc & 0x080)
			printf("7, ");
		if (ccc & 0x040)
			printf("6, ");
		if (ccc & 0x020)
			printf("5, ");
		if (ccc & 0x010)
			printf("4, ");
		if (ccc & 0x008)
			printf("3, ");
		if (ccc & 0x004)
			printf("2, ");
		if (ccc & 0x002)
			printf("1, ");
		if (ccc & 0x001)
			printf("0, ");
		printf("  )\n");

		if (csd_structure == 0 &&
		    (ccc != 0x5b5 && ccc != 0x7b5 && ccc != 0x5f5))
			printf("Warn: Invalid CCC (should be 0x5b5, "
			       "0x7b5 or 0x5f5)\n");
		else if (csd_structure == 1 && ccc != 0x5b5 && ccc != 0x7b5)
			printf("Warn: Invalid CCC (should be 0x5b5 or 0x7b5)\n");

		printf("\tREAD_BL_LEN: 0x%01x (", read_bl_len);
		switch (read_bl_len) {
		case 0x9:
			printf("512 bytes)\n");
			break;
		case 0xa:
			printf("1024 bytes)\n");
			break;
		case 0xb:
			printf("2048 bytes)\n");
			break;
		default:
			printf("reserved bytes)\n");
			break;
		}

		if (csd_structure == 1 && read_bl_len != 0x9)
			printf("Warn: Invalid READ_BL_LEN (should be 0x9)\n");

		printf("\tREAD_BL_PARTIAL: 0x%01x\n", read_bl_partial);
		if (csd_structure == 0 && read_bl_partial != 0x01)
			printf("Warn: Invalid READ_BL_PARTIAL (should be 0x01)\n");
		else if (csd_structure == 1 && read_bl_partial != 0x00)
			printf("Warn: Invalid READ_BL_PARTIAL (should be 0x00)\n");

		printf("\tWRITE_BLK_MISALIGN: 0x%01x\n", write_blk_misalign);
		if (csd_structure == 1 && write_blk_misalign != 0x00)
			printf("Warn: Invalid WRITE_BLK_MISALIGN (should be 0x00)\n");

		printf("\tREAD_BLK_MISALIGN: 0x%01x\n", read_blk_misalign);
		if (csd_structure == 1 && read_blk_misalign != 0x00)
			printf("Warn: Invalid READ_BLK_MISALIGN (should be 0x00)\n");

		printf("\tDSR_IMP: 0x%01x\n", dsr_imp);

		if (csd_structure == 0) {
			int mult;
			int blocknr;
			int block_len;

			printf("\tC_SIZE: 0x%03x\n", c_size);
			printf("\tVDD_R_CURR_MIN: 0x%01x (", vdd_r_curr_min);
			switch (vdd_r_curr_min) {
			case 0x0:
				printf("0.5mA)\n");
				break;
			case 0x1:
				printf("1mA)\n");
				break;
			case 0x2:
				printf("5mA)\n");
				break;
			case 0x3:
				printf("10mA)\n");
				break;
			case 0x4:
				printf("25mA)\n");
				break;
			case 0x5:
				printf("35mA)\n");
				break;
			case 0x6:
				printf("60mA)\n");
				break;
			case 0x7:
				printf("100mA)\n");
				break;
			}

			printf("\tVDD_R_CURR_MAX: 0x%01x (", vdd_r_curr_max);
			switch (vdd_r_curr_max) {
			case 0x0:
				printf("1mA)\n");
				break;
			case 0x1:
				printf("5mA)\n");
				break;
			case 0x2:
				printf("10mA)\n");
				break;
			case 0x3:
				printf("25mA)\n");
				break;
			case 0x4:
				printf("35mA)\n");
				break;
			case 0x5:
				printf("45mA)\n");
				break;
			case 0x6:
				printf("80mA)\n");
				break;
			case 0x7:
				printf("200mA)\n");
				break;
			}

			printf("\tVDD_W_CURR_MIN: 0x%01x (", vdd_w_curr_min);
			switch (vdd_w_curr_min) {
			case 0x0:
				printf("0.5mA)\n");
				break;
			case 0x1:
				printf("1mA)\n");
				break;
			case 0x2:
				printf("5mA)\n");
				break;
			case 0x3:
				printf("10mA)\n");
				break;
			case 0x4:
				printf("25mA)\n");
				break;
			case 0x5:
				printf("35mA)\n");
				break;
			case 0x6:
				printf("60mA)\n");
				break;
			case 0x7:
				printf("100mA)\n");
				break;
			}

			printf("\tVDD_W_CURR_MAX: 0x%01x (", vdd_w_curr_max);
			switch (vdd_w_curr_max) {
			case 0x0:
				printf("1mA)\n");
				break;
			case 0x1:
				printf("5mA)\n");
				break;
			case 0x2:
				printf("10mA)\n");
				break;
			case 0x3:
				printf("25mA)\n");
				break;
			case 0x4:
				printf("35mA)\n");
				break;
			case 0x5:
				printf("45mA)\n");
				break;
			case 0x6:
				printf("80mA)\n");
				break;
			case 0x7:
				printf("200mA)\n");
				break;
			}

			printf("\tC_SIZE_MULT: 0x%01x\n", c_size_mult);

			mult = 1 << (c_size_mult + 2);
			blocknr = (c_size + 1) * mult;
			block_len = 1 << read_bl_len;
			blocks = blocknr;
			block_size = block_len;
		} else if (csd_structure == 1) {
			printf("\tC_SIZE: 0x%06x\n", c_size);

			printf("\tERASE_BLK_EN: 0x%01x\n", erase_blk_en);
			if (erase_blk_en != 0x01)
				printf("Warn: Invalid ERASE_BLK_EN (should be 0x01)\n");

			printf("\tSECTOR_SIZE: 0x%02x (Erasable sector: %d blocks)\n",
			       sector_size, sector_size + 1);
			if (sector_size != 0x7f)
				printf("Warn: Invalid SECTOR_SIZE (should be 0x7f)\n");

			printf("\tWP_GRP_SIZE: 0x%02x (Write protect group: %d blocks)\n",
			       wp_grp_size, wp_grp_size + 1);
			if (wp_grp_size != 0x00)
				printf("Warn: Invalid WP_GRP_SIZE (should be 0x00)\n");

			printf("\tWP_GRP_ENABLE: 0x%01x\n", wp_grp_enable);
			if (wp_grp_enable != 0x00)
				printf("Warn: Invalid WP_GRP_ENABLE (should be 0x00)\n");

			printf("\tR2W_FACTOR: 0x%01x (Write %d times read)\n",
			       r2w_factor, r2w_factor);
			if (r2w_factor != 0x02)
				printf("Warn: Invalid R2W_FACTOR (should be 0x02)\n");

			printf("\tWRITE_BL_LEN: 0x%01x (", write_bl_len);
			switch (write_bl_len) {
			case 9:
				printf("512 bytes)\n");
				break;
			case 10:
				printf("1024 bytes)\n");
				break;
			case 11:
				printf("2048 bytes)\n");
				break;
			default:
				printf("reserved)\n");
				break;
			}

			if (write_bl_len != 0x09)
				printf("Warn: Invalid WRITE_BL_LEN (should be 0x09)\n");

			printf("\tWRITE_BL_PARTIAL: 0x%01x\n", write_bl_partial);
			if (write_bl_partial != 0x00)
				printf("Warn: Invalid WRITE_BL_PARTIAL (should be 0x00)\n");

			printf("\tFILE_FORMAT_GRP: 0x%01x\n", file_format_grp);
			if (file_format_grp != 0x00)
				printf("Warn: Invalid FILE_FORMAT_GRP (should be 0x00)\n");

			printf("\tCOPY: 0x%01x\n", copy);
			printf("\tPERM_WRITE_PROTECT: 0x%01x\n",
			       perm_write_protect);
			printf("\tTMP_WRITE_PROTECT: 0x%01x\n",
			       tmp_write_protect);
			printf("\tFILE_FORMAT: 0x%01x (",
			       file_format);

			if (file_format_grp == 1) {
				printf("reserved)\n");
			} else {
				switch (file_format) {
				case 0:
					printf("partition table)\n");
					break;
				case 1:
					printf("no partition table)\n");
					break;
				case 2:
					printf("Universal File Format)\n");
					break;
				case 3:
					printf("Others/unknown)\n");
					break;
				}
			}

			if (file_format != 0x00)
				printf("Warn: Invalid FILE_FORMAT (should be 0x00)\n");

			printf("\tCRC: 0x%01x\n", crc);

			memory_capacity = (c_size + 1) * 512ull * 1024ull;
			block_size = 512;
			blocks = memory_capacity / block_size;
		}

		memory_capacity = blocks * block_size;

		printf("\tCAPACITY: ");
		if (memory_capacity / (1024ull * 1024ull * 1024ull) > 0)
			printf("%.2fGbyte",
			       memory_capacity / (1024.0 * 1024.0 * 1024.0));
		else if (memory_capacity / (1024ull * 1024ull) > 0)
			printf("%.2fMbyte", memory_capacity / (1024.0 * 1024.0));
		else if (memory_capacity / (1024ull) > 0)
			printf("%.2fKbyte", memory_capacity / (1024.0));
		else
			printf("%.2fbyte", memory_capacity * 1.0);

		printf(" (%lld bytes, %lld sectors, %d bytes each)\n",
		       memory_capacity, blocks, block_size);
	} else {
		unsigned long long blocks = 0;
		int block_size = 0;
		unsigned long long memory_capacity;

		printf("card classes: ");
		if (ccc & 0x800)
			printf("11 extension, ");
		if (ccc & 0x400)
			printf("10 switch, ");
		if (ccc & 0x200)
			printf("9 I/O mode, ");
		if (ccc & 0x100)
			printf("8 application specific, ");
		if (ccc & 0x080)
			printf("7 lock card, ");
		if (ccc & 0x040)
			printf("6 write protection, ");
		if (ccc & 0x020)
			printf("5 erase, ");
		if (ccc & 0x010)
			printf("4 block write, ");
		if (ccc & 0x008)
			printf("3 reserved, ");
		if (ccc & 0x004)
			printf("2 block read, ");
		if (ccc & 0x002)
			printf("1 reserved, ");
		if (ccc & 0x001)
			printf("0 basic, ");
		printf("\b\b\n");

		if (csd_structure == 0) {
			int mult;
			int blocknr;
			int block_len;

			mult = 1 << (c_size_mult + 2);
			blocknr = (c_size + 1) * mult;
			block_len = 1 << read_bl_len;
			blocks = blocknr;
			block_size = block_len;
		} else if (csd_structure == 1) {
			memory_capacity = (c_size + 1) * 512ull * 1024ull;
			block_size = 512;
			blocks = memory_capacity / block_size;
		}

		memory_capacity = blocks * block_size;

		printf("capacity: ");
		if (memory_capacity / (1024ull * 1024ull * 1024ull) > 0)
			printf("%.2fGbyte",
			       memory_capacity / (1024.0 * 1024.0 * 1024.0));
		else if (memory_capacity / (1024ull * 1024ull) > 0)
			printf("%.2fMbyte", memory_capacity / (1024.0 * 1024.0));
		else if (memory_capacity / (1024ull) > 0)
			printf("%.2fKbyte", memory_capacity / (1024.0));
		else
			printf("%.2fbyte", memory_capacity * 1.0);

		printf(" (%lld bytes, %lld sectors, %d bytes each)\n",
		       memory_capacity, blocks, block_size);
	}
}

void print_mmc_csd(struct config *config, char *csd)
{
	unsigned int csd_structure;
	unsigned int spec_vers;
	unsigned int taac_timevalue;
	unsigned int taac_timeunit;
	unsigned int nsac;
	unsigned int tran_speed_timevalue;
	unsigned int tran_speed_transferrateunit;
	unsigned int ccc;
	unsigned int read_bl_len;
	unsigned int read_bl_partial;
	unsigned int write_blk_misalign;
	unsigned int read_blk_misalign;
	unsigned int dsr_imp;
	unsigned int c_size;
	unsigned int vdd_r_curr_min;
	unsigned int vdd_r_curr_max;
	unsigned int vdd_w_curr_min;
	unsigned int vdd_w_curr_max;
	unsigned int c_size_mult;
	unsigned int erase_grp_size;
	unsigned int erase_grp_mult;
	unsigned int wp_grp_size;
	unsigned int wp_grp_enable;
	unsigned int default_ecc;
	unsigned int r2w_factor;
	unsigned int write_bl_len;
	unsigned int write_bl_partial;
	unsigned int content_prot_app;
	unsigned int file_format_grp;
	unsigned int copy;
	unsigned int perm_write_protect;
	unsigned int tmp_write_protect;
	unsigned int file_format;
	unsigned int ecc;
	unsigned int crc;
	unsigned int taac;
	unsigned int tran_speed;

	parse_bin(csd, "2u4u2r1r4u3u8u1r4u3u12u4u1u1u1u1u2r12u3u3u3u3u3u"
		  "5u5u5u1u2u3u4u1u4r1u1u1u1u1u2u2u7u1r",
		  &csd_structure, &spec_vers, &taac_timevalue,
		  &taac_timeunit, &nsac, &tran_speed_timevalue,
		  &tran_speed_transferrateunit, &ccc, &read_bl_len,
		  &read_bl_partial, &write_blk_misalign,
		  &read_blk_misalign, &dsr_imp, &c_size,
		  &vdd_r_curr_min, &vdd_r_curr_max,
		  &vdd_w_curr_min, &vdd_w_curr_max, &c_size_mult,
		  &erase_grp_size, &erase_grp_mult, &wp_grp_size,
		  &wp_grp_enable, &default_ecc, &r2w_factor,
		  &write_bl_len, &write_bl_partial, &content_prot_app,
		  &file_format_grp, &copy, &perm_write_protect,
		  &tmp_write_protect, &file_format, &ecc, &crc);

	taac = taac_timevalue << 3 | taac_timeunit;
	tran_speed = tran_speed_timevalue << 3 | tran_speed_transferrateunit;

	if (config->verbose) {
		float value;
		int mult;
		int blocknr;
		int block_len;
		unsigned long long blocks = 0;
		int block_size = 0;
		unsigned long long memory_capacity;

		printf("======MMC/CSD======\n");

		printf("\tCSD_STRUCTURE: 0x%01x (", csd_structure);
		switch (csd_structure) {
		case 0x0:
			printf("v1.0)\n");
			break;
		case 0x1:
			printf("v1.1)\n");
			break;
		case 0x2:
			printf("v1.2)\n");
			break;
		case 0x3:
			printf("version in ext_csd)\n");
			break;
		}

		printf("\tSPEC_VERS: 0x%01x (", spec_vers);
		switch (spec_vers) {
		case 0x0:
			printf("v1.0-v1.2)\n");
			break;
		case 0x1:
			printf("v1.4)\n");
			break;
		case 0x2:
			printf("v2.0-v2.2)\n");
			break;
		case 0x3:
			printf("v3.1-v3.31)\n");
			break;
		case 0x4:
			printf("v4.0-v4.3)\n");
			break;
		default:
			printf("reserved)\n");
			break;
		}

		printf("\tTAAC: 0x%02x (", taac);
		switch (taac_timevalue) {
		case 0x0:
			value = 0.0f;
			break;
		case 0x1:
			value = 1.0f;
			break;
		case 0x2:
			value = 1.2f;
			break;
		case 0x3:
			value = 1.3f;
			break;
		case 0x4:
			value = 1.5f;
			break;
		case 0x5:
			value = 2.0f;
			break;
		case 0x6:
			value = 2.5f;
			break;
		case 0x7:
			value = 3.0f;
			break;
		case 0x8:
			value = 3.5f;
			break;
		case 0x9:
			value = 4.0f;
			break;
		case 0xa:
			value = 4.5f;
			break;
		case 0xb:
			value = 5.0f;
			break;
		case 0xc:
			value = 5.5f;
			break;
		case 0xd:
			value = 6.0f;
			break;
		case 0xe:
			value = 7.0f;
			break;
		case 0xf:
			value = 8.0f;
			break;
		default:
			value = 0.0f;
			break;
		}

		switch (taac_timeunit) {
		case 0x0:
			printf("%.2fns)\n", value * 1.0f);
			break;
		case 0x1:
			printf("%.2fns)\n", value * 10.0f);
			break;
		case 0x2:
			printf("%.2fns)\n", value * 100.0f);
			break;
		case 0x3:
			printf("%.2fus)\n", value * 1.0f);
			break;
		case 0x4:
			printf("%.2fus)\n", value * 10.0f);
			break;
		case 0x5:
			printf("%.2fus)\n", value * 100.0f);
			break;
		case 0x6:
			printf("%.2fms)\n", value * 1.0f);
			break;
		case 0x7:
			printf("%.2fms)\n", value * 10.0f);
			break;
		}

		printf("\tNSAC: %d clocks\n", nsac);
		printf("\tTRAN_SPEED: 0x%02x (", tran_speed);
		switch (tran_speed_timevalue) {
		case 0x0:
			value = 0.0f;
			break;
		case 0x1:
			value = 1.0f;
			break;
		case 0x2:
			value = 1.2f;
			break;
		case 0x3:
			value = 1.3f;
			break;
		case 0x4:
			value = 1.5f;
			break;
		case 0x5:
			value = 2.0f;
			break;
		case 0x6:
			value = 2.6f;
			break;
		case 0x7:
			value = 3.0f;
			break;
		case 0x8:
			value = 3.5f;
			break;
		case 0x9:
			value = 4.0f;
			break;
		case 0xa:
			value = 4.5f;
			break;
		case 0xb:
			value = 5.2f;
			break;
		case 0xc:
			value = 5.5f;
			break;
		case 0xd:
			value = 6.0f;
			break;
		case 0xe:
			value = 7.0f;
			break;
		case 0xf:
			value = 8.0f;
			break;
		default:
			value = 0.0f;
			break;
		}

		switch (tran_speed_transferrateunit) {
		case 0x0:
			printf("%.2fKHz/s)\n", value * 100.0f);
			break;
		case 0x1:
			printf("%.2fMHz/s)\n", value * 1.0f);
			break;
		case 0x2:
			printf("%.2fMHz/s)\n", value * 10.0f);
			break;
		case 0x3:
			printf("%.2fMHz/s)\n", value * 100.0f);
			break;
		default:
			printf("reserved)\n");
			break;
		}

		printf("\tCCC: 0x%03x (class: ", ccc);
		if (ccc & 0x800)
			printf("11, ");
		if (ccc & 0x400)
			printf("10, ");
		if (ccc & 0x200)
			printf("9, ");
		if (ccc & 0x100)
			printf("8, ");
		if (ccc & 0x080)
			printf("7, ");
		if (ccc & 0x040)
			printf("6, ");
		if (ccc & 0x020)
			printf("5, ");
		if (ccc & 0x010)
			printf("4, ");
		if (ccc & 0x008)
			printf("3, ");
		if (ccc & 0x004)
			printf("2, ");
		if (ccc & 0x002)
			printf("1, ");
		if (ccc & 0x001)
			printf("0, ");
		printf("  )\n");

		printf("\tREAD_BL_LEN: 0x%01x (", read_bl_len);
		switch (read_bl_len) {
		case 0x0:
			printf("1 byte)\n");
			break;
		case 0x1:
			printf("2 byte)\n");
			break;
		case 0x2:
			printf("4 byte)\n");
			break;
		case 0x3:
			printf("8 byte)\n");
			break;
		case 0x4:
			printf("16 byte)\n");
			break;
		case 0x5:
			printf("32 byte)\n");
			break;
		case 0x6:
			printf("64 byte)\n");
			break;
		case 0x7:
			printf("128 byte)\n");
			break;
		case 0x8:
			printf("256 byte)\n");
			break;
		case 0x9:
			printf("512 bytes)\n");
			break;
		case 0xa:
			printf("1024 bytes)\n");
			break;
		case 0xb:
			printf("2048 bytes)\n");
			break;
		case 0xc:
			printf("4096 bytes)\n");
			break;
		case 0xd:
			printf("8192 bytes)\n");
			break;
		case 0xe:
			printf("16K bytes)\n");
			break;
		default:
			printf("reserved bytes)\n");
			break;
		}

		printf("\tREAD_BL_PARTIAL: 0x%01x (", read_bl_partial);
		switch (read_bl_partial) {
		case 0x0:
			printf("only 512 byte and READ_BL_LEN block size)\n");
			break;
		case 0x1:
			printf("less than READ_BL_LEN block size can be used)\n");
			break;
		}

		printf("\tWRITE_BLK_MISALIGN: 0x%01x (", write_blk_misalign);
		switch (write_blk_misalign) {
		case 0x0:
			printf("writes across block boundaries are invalid)\n");
			break;
		case 0x1:
			printf("writes across block boundaries are allowed)\n");
			break;
		}

		printf("\tREAD_BLK_MISALIGN: 0x%01x (", read_blk_misalign);
		switch (read_blk_misalign) {
		case 0x0:
			printf("reads across block boundaries are invalid)\n");
			break;
		case 0x1:
			printf("reads across block boundaries are allowed)\n");
			break;
		}

		printf("\tDSR_IMP: 0x%01x (", dsr_imp);
		switch (dsr_imp) {
		case 0x0:
			printf("configurable driver stage not available)\n");
			break;
		case 0x1:
			printf("configurable driver state available)\n");
			break;
		}

		printf("\tC_SIZE: 0x%03x\n", c_size);
		printf("\tVDD_R_CURR_MIN: 0x%01x (", vdd_r_curr_min);
		switch (vdd_r_curr_min) {
		case 0x0:
			printf("0.5mA)\n");
			break;
		case 0x1:
			printf("1mA)\n");
			break;
		case 0x2:
			printf("5mA)\n");
			break;
		case 0x3:
			printf("10mA)\n");
			break;
		case 0x4:
			printf("25mA)\n");
			break;
		case 0x5:
			printf("35mA)\n");
			break;
		case 0x6:
			printf("60mA)\n");
			break;
		case 0x7:
			printf("100mA)\n");
			break;
		}

		printf("\tVDD_R_CURR_MAX: 0x%01x (", vdd_r_curr_max);
		switch (vdd_r_curr_max) {
		case 0x0:
			printf("1mA)\n");
			break;
		case 0x1:
			printf("5mA)\n");
			break;
		case 0x2:
			printf("10mA)\n");
			break;
		case 0x3:
			printf("25mA)\n");
			break;
		case 0x4:
			printf("35mA)\n");
			break;
		case 0x5:
			printf("45mA)\n");
			break;
		case 0x6:
			printf("80mA)\n");
			break;
		case 0x7:
			printf("200mA)\n");
			break;
		}

		printf("\tVDD_W_CURR_MIN: 0x%01x (", vdd_w_curr_min);
		switch (vdd_w_curr_min) {
		case 0x0:
			printf("0.5mA)\n");
			break;
		case 0x1:
			printf("1mA)\n");
			break;
		case 0x2:
			printf("5mA)\n");
			break;
		case 0x3:
			printf("10mA)\n");
			break;
		case 0x4:
			printf("25mA)\n");
			break;
		case 0x5:
			printf("35mA)\n");
			break;
		case 0x6:
			printf("60mA)\n");
			break;
		case 0x7:
			printf("100mA)\n");
			break;
		}

		printf("\tVDD_W_CURR_MAX: 0x%01x (", vdd_w_curr_max);
		switch (vdd_w_curr_max) {
		case 0x0:
			printf("1mA)\n");
			break;
		case 0x1:
			printf("5mA)\n");
			break;
		case 0x2:
			printf("10mA)\n");
			break;
		case 0x3:
			printf("25mA)\n");
			break;
		case 0x4:
			printf("35mA)\n");
			break;
		case 0x5:
			printf("45mA)\n");
			break;
		case 0x6:
			printf("80mA)\n");
			break;
		case 0x7:
			printf("200mA)\n");
			break;
		}

		printf("\tC_SIZE_MULT: 0x%01x\n", c_size_mult);
		printf("\tERASE_GRP_SIZE: 0x%02x\n", erase_grp_size);
		printf("\tERASE_GRP_MULT: 0x%02x (%d write blocks/erase group)\n",
		       erase_grp_mult, (erase_grp_size + 1) *
		       (erase_grp_mult + 1));
		printf("\tWP_GRP_SIZE: 0x%02x (%d blocks/write protect group)\n",
		       wp_grp_size, wp_grp_size + 1);
		printf("\tWP_GRP_ENABLE: 0x%01x\n", wp_grp_enable);

		printf("\tDEFAULT_ECC: 0x%01x (", default_ecc);
		switch (default_ecc) {
		case 0:
			printf("none)\n");
			break;
		case 1:
			printf("BCH)\n");
			break;
		default:
			printf("reserved)\n");
			break;
		}

		printf("\tR2W_FACTOR: 0x%01x (Write %d times read)\n",
		       r2w_factor, r2w_factor);

		printf("\tWRITE_BL_LEN: 0x%01x (", write_bl_len);
		switch (write_bl_len) {
		case 0x0:
			printf("1 byte)\n");
			break;
		case 0x1:
			printf("2 byte)\n");
			break;
		case 0x2:
			printf("4 byte)\n");
			break;
		case 0x3:
			printf("8 byte)\n");
			break;
		case 0x4:
			printf("16 byte)\n");
			break;
		case 0x5:
			printf("32 byte)\n");
			break;
		case 0x6:
			printf("64 byte)\n");
			break;
		case 0x7:
			printf("128 byte)\n");
			break;
		case 0x8:
			printf("256 byte)\n");
			break;
		case 0x9:
			printf("512 bytes)\n");
			break;
		case 0xa:
			printf("1024 bytes)\n");
			break;
		case 0xb:
			printf("2048 bytes)\n");
			break;
		case 0xc:
			printf("4096 bytes)\n");
			break;
		case 0xd:
			printf("8192 bytes)\n");
			break;
		case 0xe:
			printf("16K bytes)\n");
			break;
		default:
			printf("reserved bytes)\n");
			break;
		}

		printf("\tWRITE_BL_PARTIAL: 0x%01x (", write_bl_partial);
		switch (write_bl_partial) {
		case 0x0:
			printf("only 512 byte and WRITE_BL_LEN block size)\n");
			break;
		case 0x1:
			printf("less than WRITE_BL_LEN block size can be used)\n");
			break;
		}

		printf("\tCONTENT_PROT_APP: 0x%01x\n", content_prot_app);
		printf("\tFILE_FORMAT_GRP: 0x%01x\n", file_format_grp);
		if (file_format_grp != 0)
			printf("Warn: Invalid FILE_FORMAT_GRP\n");

		printf("\tCOPY: 0x%01x\n", copy);
		printf("\tPERM_WRITE_PROTECT: 0x%01x\n", perm_write_protect);
		printf("\tTMP_WRITE_PROTECT: 0x%01x\n", tmp_write_protect);
		printf("\tFILE_FORMAT: 0x%01x (", file_format);
		if (file_format != 0)
			printf("Warn: Invalid FILE_FORMAT\n");

		if (file_format_grp == 1) {
			printf("reserved)\n");
		} else {
			switch (file_format) {
			case 0:
				printf("partition table)\n");
				break;
			case 1:
				printf("no partition table)\n");
				break;
			case 2:
				printf("Universal File Format)\n");
				break;
			case 3:
				printf("Others/unknown)\n");
				break;
			}
		}

		printf("\tECC: 0x%01x (", ecc);
		switch (ecc) {
		case 0:
			printf("none)\n");
			break;
		case 1:
			printf("BCH(542,512))\n");
			break;
		default:
			printf("reserved)\n");
			break;
		}

		printf("\tCRC: 0x%01x\n", crc);

		mult = 1 << (c_size_mult + 2);
		blocknr = (c_size + 1) * mult;
		block_len = 1 << read_bl_len;
		blocks = blocknr;
		block_size = block_len;

		memory_capacity = blocks * block_size;

		printf("\tCAPACITY: ");
		if (memory_capacity / (1024ull * 1024ull * 1024ull) > 0)
			printf("%.2fGbyte",
			       memory_capacity / (1024.0 * 1024.0 * 1024.0));
		else if (memory_capacity / (1024ull * 1024ull) > 0)
			printf("%.2fMbyte", memory_capacity / (1024.0 * 1024.0));
		else if (memory_capacity / (1024ull) > 0)
			printf("%.2fKbyte", memory_capacity / (1024.0));
		else
			printf("%.2fbyte", memory_capacity * 1.0);

		printf(" (%lld bytes, %lld sectors, %d bytes each)\n",
		       memory_capacity, blocks, block_size);
	} else {
		int mult;
		int blocknr;
		int block_len;
		unsigned long long blocks = 0;
		int block_size = 0;
		unsigned long long memory_capacity;

		printf("version: ");
		switch (spec_vers) {
		case 0x0:
			printf("MMC v1.0-v1.2\n");
			break;
		case 0x1:
			printf("MMC v1.4\n");
			break;
		case 0x2:
			printf("MMC v2.0-v2.2\n");
			break;
		case 0x3:
			printf("MMC v3.1-v3.31\n");
			break;
		case 0x4:
			printf("MMC v4.0-v4.3\n");
			break;
		default:
			printf("reserved\n");
			break;
		}

		printf("card classes: ");
		if (ccc & 0x800)
			printf("11, ");
		if (ccc & 0x400)
			printf("10, ");
		if (ccc & 0x200)
			printf("9, ");
		if (ccc & 0x100)
			printf("8, ");
		if (ccc & 0x080)
			printf("7, ");
		if (ccc & 0x040)
			printf("6, ");
		if (ccc & 0x020)
			printf("5, ");
		if (ccc & 0x010)
			printf("4, ");
		if (ccc & 0x008)
			printf("3, ");
		if (ccc & 0x004)
			printf("2, ");
		if (ccc & 0x002)
			printf("1, ");
		if (ccc & 0x001)
			printf("0, ");
		printf("\b\b\n");

		mult = 1 << (c_size_mult + 2);
		blocknr = (c_size + 1) * mult;
		block_len = 1 << read_bl_len;
		blocks = blocknr;
		block_size = block_len;

		memory_capacity = blocks * block_size;

		printf("capacity: ");
		if (memory_capacity / (1024ull * 1024ull * 1024ull) > 0)
			printf("%.2fGbyte",
			       memory_capacity / (1024.0 * 1024.0 * 1024.0));
		else if (memory_capacity / (1024ull * 1024ull) > 0)
			printf("%.2fMbyte", memory_capacity / (1024.0 * 1024.0));
		else if (memory_capacity / (1024ull) > 0)
			printf("%.2fKbyte", memory_capacity / (1024.0));
		else
			printf("%.2fbyte", memory_capacity * 1.0);
		printf(" (%lld bytes, %lld sectors, %d bytes each)\n",
		       memory_capacity, blocks, block_size);
	}
}

char *speed_class_speed(unsigned char id, bool ddr)
{
	if (ddr) {
		switch (id) {
		case 0x00: return "<4.8MB/s";
		case 0x08: return " 4.8MB/s";
		case 0x0a: return " 6.0MB/s";
		case 0x0f: return " 9.0MB/s";
		case 0x14: return "12.0MB/s";
		case 0x1e: return "18.0MB/s";
		case 0x28: return "24.0MB/s";
		case 0x32: return "30.0MB/s";
		case 0x3c: return "36.0MB/s";
		case 0x46: return "42.0MB/s";
		case 0x50: return "48.0MB/s";
		case 0x64: return "60.0MB/s";
		case 0x78: return "72.0MB/s";
		case 0x8c: return "84.0MB/s";
		case 0xa0: return "96.0MB/s";
		default: return "??.?MB/s";
		}
	} else {
		switch (id) {
		case 0x00: return "<2.4MB/s";
		case 0x08: return " 2.4MB/s";
		case 0x0a: return " 3.0MB/s";
		case 0x0f: return " 4.5MB/s";
		case 0x14: return " 6.0MB/s";
		case 0x1e: return " 9.0MB/s";
		case 0x28: return "12.0MB/s";
		case 0x32: return "15.0MB/s";
		case 0x3c: return "18.0MB/s";
		case 0x46: return "21.0MB/s";
		case 0x50: return "24.0MB/s";
		case 0x64: return "30.0MB/s";
		case 0x78: return "36.0MB/s";
		case 0x8c: return "42.0MB/s";
		case 0xa0: return "48.0MB/s";
		default: return "??.?MB/s";
		}
	}
}

char speed_class_name(unsigned char id)
{
	switch (id) {
	case 0x00: return '?';
	case 0x08: return 'A';
	case 0x0a: return 'B';
	case 0x0f: return 'C';
	case 0x14: return 'D';
	case 0x1e: return 'E';
	case 0x28: return 'F';
	case 0x32: return 'G';
	case 0x3c: return 'H';
	case 0x46: return 'J';
	case 0x50: return 'K';
	case 0x64: return 'M';
	case 0x78: return 'O';
	case 0x8c: return 'R';
	case 0xa0: return 'T';
	default: return '?';
	}
}

char *power_class_consumption(unsigned int id, bool volt360)
{
	if (volt360) {
		switch (id) {
		case 0x0: return "100-200mA";
		case 0x1: return "120-220mA";
		case 0x2: return "150-250mA";
		case 0x3: return "180-280mA";
		case 0x4: return "200-300mA";
		case 0x5: return "220-320mA";
		case 0x6: return "250-350mA";
		case 0x7: return "300-400mA";
		case 0x8: return "350-450mA";
		case 0x9: return "400-500mA";
		case 0xa: return "450-550mA";
		default: return "reserved";
		}
	} else {
		switch (id) {
		case 0x0: return "65-130mA";
		case 0x1: return "70-140mA";
		case 0x2: return "80-160mA";
		case 0x3: return "90-180mA";
		case 0x4: return "100-200mA";
		case 0x5: return "120-220mA";
		case 0x6: return "140-240mA";
		case 0x7: return "160-260mA";
		case 0x8: return "180-280mA";
		case 0x9: return "200-300mA";
		case 0xa: return "250-350mA";
		default: return "reserved";
		}
	}
}

char *sleep_consumption(unsigned int id)
{
	switch (id) {
	case 0x00: return "not defined";
	case 0x01: return "2uA";
	case 0x02: return "4uA";
	case 0x03: return "8uA";
	case 0x04: return "16uA";
	case 0x05: return "32uA";
	case 0x06: return "64uA";
	case 0x07: return "128uA";
	case 0x08: return "0.256mA";
	case 0x09: return "0.512mA";
	case 0x0a: return "1.024mA";
	case 0x0b: return "2.048mA";
	case 0x0c: return "4.096mA";
	case 0x0d: return "8.192mA";
	default: return "reserved";
	}
}

void print_sd_scr(struct config *config, char *scr)
{
	unsigned int scr_structure;
	unsigned int sd_spec;
	unsigned int data_stat_after_erase;
	unsigned int sd_security;
	unsigned int sd_bus_widths;
	unsigned int sd_spec3;
	unsigned int ex_security;
	unsigned int cmd_support;

	parse_bin(scr, "4u4u1u3u4u1u4u9r2u32r",
		&scr_structure, &sd_spec, &data_stat_after_erase,
		&sd_security, &sd_bus_widths, &sd_spec3,
		&ex_security, &cmd_support);

	if (config->verbose) {
		printf("======SD/SCR======\n");

		printf("\tSCR_STRUCTURE: 0x%01x (", scr_structure);
		switch (scr_structure) {
		case 0:
			printf("SCR v1.0)\n");
			break;
		default:
			printf("reserved)\n");
			break;
		}

		printf("\tSD_SPEC: 0x%01x (", sd_spec);
		switch (sd_spec) {
		case 0:
			printf("SD v1.0/1.01)\n");
			break;
		case 1:
			printf("SD v1.10)\n");
			break;
		case 2:
			printf("SD v2.00/v3.0x)\n");
			break;
		case 3:
			printf("SD v4.00)\n");
			break;
		default:
			printf("reserved)\n");
			break;
		}

		printf("\tDATA_STAT_AFTER_ERASE: 0x%01x\n",
		       data_stat_after_erase);

		printf("\tSD_SECURITY: 0x%01x (", sd_security);
		switch (sd_security) {
		case 0:
			printf("no security)\n");
			break;
		case 1:
			printf("not used)\n");
			break;
		case 2:
			printf("SDSC card/security v1.01)\n");
			break;
		case 3:
			printf("SDHC card/security v2.00)\n");
			break;
		case 4:
			printf("SDXC card/security v3.xx)\n");
			break;
		default:
			printf("reserved)\n");
			break;
		}

		printf("\tSD_BUS_WIDTHS: 0x%01x (", sd_bus_widths);
		if (BITS(sd_bus_widths, 2, 2))
			printf("4bit, ");
		if (BITS(sd_bus_widths, 0, 0))
			printf("1bit, ");
		printf(" bus)\n");

		printf("\tSD_SPEC3: 0x%01x (", sd_spec3);
		if (sd_spec >= 2) {
			switch (sd_spec3) {
			case 0:
				printf("SD v2.00)\n");
				break;
			case 1:
				printf("SD v3.0x)\n");
				break;
			}
		} else {
			printf("SD 1.xx)\n");
		}

		printf("\tEX_SECURITY: 0x%01x\n", ex_security);

		printf("\tCMD_SUPPORT: 0x%01x (", cmd_support);
		if (BITS(cmd_support, 1, 1))
			printf("CMD23 ");
		if (BITS(cmd_support, 0, 0))
			printf("CMD20 ");
		printf(" )\n");
	} else {
		printf("version: ");
		switch (sd_spec) {
		case 0:
			printf("SD 1.0/1.01\n");
			break;
		case 1:
			printf("SD 1.10\n");
			break;
		case 2:
			switch (sd_spec3) {
			case 0:
				printf("SD 2.00\n");
				break;
			case 1:
				printf("SD 3.0x\n");
				break;
			default:
				printf("unknown\n");
				break;
			}
			break;
		case 3:
			printf("SD 4.00\n");
			break;
		default:
			printf("unknown\n");
			break;
		}

		printf("bus widths: ");
		if (BITS(sd_bus_widths, 2, 2))
			printf("4bit, ");
		if (BITS(sd_bus_widths, 0, 0))
			printf("1bit, ");
		printf("\b\b\n");
	}
}

/* MMC/SD interface processing functions */
void print_info(struct config *config, char *type,
	char *cid, char *csd, char *scr, char *ext_csd)
{
	printf("type: '%s'\n", type);

	if (!strcmp(type, "SD") && cid)
		print_sd_cid(config, cid);
	else if (!strcmp(type, "MMC") && cid)
		print_mmc_cid(config, cid);

	if (!strcmp(type, "SD") && scr)
		print_sd_scr(config, scr);

	if (!strcmp(type, "MMC") && csd)
		print_mmc_csd(config, csd);
	else if (!strcmp(type, "SD") && csd)
		print_sd_csd(config, csd);
}

int process_dir(struct config *config, enum REG_TYPE reg)
{
	char *type = NULL, *cid = NULL, *csd = NULL, *scr = NULL, *ext_csd = NULL;
	int ret = 0;

	if (chdir(config->dir) < 0) {
		fprintf(stderr,
			"MMC/SD information directory '%s' does not exist.\n",
			config->dir);
		return -1;
	}

	type = read_file("type");
	if (!type) {
		fprintf(stderr,
			"Could not read card interface type in directory '%s'.\n",
			config->dir);
		return -1;
	}

	if (strcmp(type, "MMC") && strcmp(type, "SD")) {
		fprintf(stderr, "Unknown type: '%s'\n", type);
		ret = -1;
		goto err;
	}

	switch (reg) {
	case CID:
		cid = read_file("cid");
		if (!cid) {
			fprintf(stderr,
				"Could not read card identity in directory '%s'.\n",
				config->dir);
			ret = -1;
			goto err;
		}
		break;
	case CSD:
		csd = read_file("csd");
		if (!csd) {
			fprintf(stderr,
				"Could not read card specific data in "
				"directory '%s'.\n", config->dir);
			ret = -1;
			goto err;
		}
		break;
	case SCR:
		if (!strcmp(type, "SD")) {
			scr = read_file("scr");
			if (!scr) {
				fprintf(stderr, "Could not read SD card "
					"configuration in directory '%s'.\n",
					config->dir);
				ret = -1;
				goto err;
			}
		}
		break;
	case EXT_CSD:
		if (!strcmp(type, "MMC")) {
			ext_csd = read_file("ext_csd");
			if (!ext_csd) {
				fprintf(stderr, "Could not read extra specific "
					"data in directory '%s'.\n",
					config->dir);
				ret = -1;
				goto err;
			}
		}
		break;
	default:
		goto err;
	}

	print_info(config, type, cid, csd, scr, ext_csd);

err:
	free(ext_csd);
	free(scr);
	free(csd);
	free(cid);
	free(type);

	return ret;
}

int lsmmc_main(struct config *config, int argc, char **argv)
{
	int ret;

	memset(config, 0, sizeof(*config));
	config->mmc_ids = calloc(IDS_MAX, sizeof(char *));
	config->sd_ids = calloc(IDS_MAX, sizeof(char *));
	if (!config->mmc_ids || !config->sd_ids) {
		fprintf(stderr, "Could not allocate memory for lsmmc.\n");
		return -1;
	}

	ret = parse_opts(argc, argv, config);
	if (ret)
		return ret;

	return parse_ids(config);
}

void lsmmc_free(struct config *config)
{
	free(config->mmc_ids);
	free(config->sd_ids);
	free(config->dir);
}

int do_read_csd(int argc, char **argv)
{
	struct config config;
	int ret;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "Usage: Print CSD data from <device path>.\n");
		exit(1);
	}

	ret = lsmmc_main(&config, argc, argv);
	if (ret)
		goto out;

	if (config.dir)
		ret = process_dir(&config, CSD);

out:
	lsmmc_free(&config);

	return ret;
}

int do_read_cid(int argc, char **argv)
{
	struct config config;
	int ret;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "Usage: Print CID data from <device path>.\n");
		exit(1);
	}

	ret = lsmmc_main(&config, argc, argv);
	if (ret)
		goto out;

	if (config.dir)
		ret = process_dir(&config, CID);

out:
	lsmmc_free(&config);

	return ret;
}

int do_read_scr(int argc, char **argv)
{
	struct config config;
	int ret;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "Usage: Print SCR data from <device path>.\n");
		exit(1);
	}

	ret = lsmmc_main(&config, argc, argv);
	if (ret)
		goto out;

	if (config.dir)
		ret = process_dir(&config, SCR);

out:
	lsmmc_free(&config);

	return ret;
}
