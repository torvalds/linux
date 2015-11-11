/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#ifndef __LINUX_IPMI_FRU_H__
#define __LINUX_IPMI_FRU_H__
#ifdef __KERNEL__
#  include <linux/types.h>
#  include <linux/string.h>
#else
#  include <stdint.h>
#  include <string.h>
#endif

/*
 * These structures match the unaligned crap we have in FRU1011.pdf
 * (http://download.intel.com/design/servers/ipmi/FRU1011.pdf)
 */

/* chapter 8, page 5 */
struct fru_common_header {
	uint8_t format;			/* 0x01 */
	uint8_t internal_use_off;	/* multiple of 8 bytes */
	uint8_t chassis_info_off;	/* multiple of 8 bytes */
	uint8_t board_area_off;		/* multiple of 8 bytes */
	uint8_t product_area_off;	/* multiple of 8 bytes */
	uint8_t multirecord_off;	/* multiple of 8 bytes */
	uint8_t pad;			/* must be 0 */
	uint8_t checksum;		/* sum modulo 256 must be 0 */
};

/* chapter 9, page 5 -- internal_use: not used by us */

/* chapter 10, page 6 -- chassis info: not used by us */

/* chapter 13, page 9 -- used by board_info_area below */
struct fru_type_length {
	uint8_t type_length;
	uint8_t data[0];
};

/* chapter 11, page 7 */
struct fru_board_info_area {
	uint8_t format;			/* 0x01 */
	uint8_t area_len;		/* multiple of 8 bytes */
	uint8_t language;		/* I hope it's 0 */
	uint8_t mfg_date[3];		/* LSB, minutes since 1996-01-01 */
	struct fru_type_length tl[0];	/* type-length stuff follows */

	/*
	 * the TL there are in order:
	 * Board Manufacturer
	 * Board Product Name
	 * Board Serial Number
	 * Board Part Number
	 * FRU File ID (may be null)
	 * more manufacturer-specific stuff
	 * 0xc1 as a terminator
	 * 0x00 pad to a multiple of 8 bytes - 1
	 * checksum (sum of all stuff module 256 must be zero)
	 */
};

enum fru_type {
	FRU_TYPE_BINARY		= 0x00,
	FRU_TYPE_BCDPLUS	= 0x40,
	FRU_TYPE_ASCII6		= 0x80,
	FRU_TYPE_ASCII		= 0xc0, /* not ascii: depends on language */
};

/*
 * some helpers
 */
static inline struct fru_board_info_area *fru_get_board_area(
	const struct fru_common_header *header)
{
	/* we know for sure that the header is 8 bytes in size */
	return (struct fru_board_info_area *)(header + header->board_area_off);
}

static inline int fru_type(struct fru_type_length *tl)
{
	return tl->type_length & 0xc0;
}

static inline int fru_length(struct fru_type_length *tl)
{
	return (tl->type_length & 0x3f) + 1; /* len of whole record */
}

/* assume ascii-latin1 encoding */
static inline int fru_strlen(struct fru_type_length *tl)
{
	return fru_length(tl) - 1;
}

static inline char *fru_strcpy(char *dest, struct fru_type_length *tl)
{
	int len = fru_strlen(tl);
	memcpy(dest, tl->data, len);
	dest[len] = '\0';
	return dest;
}

static inline struct fru_type_length *fru_next_tl(struct fru_type_length *tl)
{
	return tl + fru_length(tl);
}

static inline int fru_is_eof(struct fru_type_length *tl)
{
	return tl->type_length == 0xc1;
}

/*
 * External functions defined in fru-parse.c.
 */
extern int fru_header_cksum_ok(struct fru_common_header *header);
extern int fru_bia_cksum_ok(struct fru_board_info_area *bia);

/* All these 4 return allocated strings by calling fru_alloc() */
extern char *fru_get_board_manufacturer(struct fru_common_header *header);
extern char *fru_get_product_name(struct fru_common_header *header);
extern char *fru_get_serial_number(struct fru_common_header *header);
extern char *fru_get_part_number(struct fru_common_header *header);

/* This must be defined by the caller of the above functions */
extern void *fru_alloc(size_t size);

#endif /* __LINUX_IMPI_FRU_H__ */
