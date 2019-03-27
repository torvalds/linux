/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2005-2006 Atheros Communications, Inc.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 *
 * $FreeBSD$
 */
#ifndef	__AH_REGDOMAIN_H__
#define	__AH_REGDOMAIN_H__

/* 
 * BMLEN defines the size of the bitmask used to hold frequency
 * band specifications.  Note this must agree with the BM macro
 * definition that's used to setup initializers.  See also further
 * comments below.
 */
#define BMLEN 2		/* 2 x 64 bits in each channel bitmask */
typedef uint64_t chanbmask_t[BMLEN];

/*
 * The following describe the bit masks for different passive scan
 * capability/requirements per regdomain.
 */
#define	NO_PSCAN	0x0ULL			/* NB: must be zero */
#define	PSCAN_FCC	0x0000000000000001ULL
#define	PSCAN_FCC_T	0x0000000000000002ULL
#define	PSCAN_ETSI	0x0000000000000004ULL
#define	PSCAN_MKK1	0x0000000000000008ULL
#define	PSCAN_MKK2	0x0000000000000010ULL
#define	PSCAN_MKKA	0x0000000000000020ULL
#define	PSCAN_MKKA_G	0x0000000000000040ULL
#define	PSCAN_ETSIA	0x0000000000000080ULL
#define	PSCAN_ETSIB	0x0000000000000100ULL
#define	PSCAN_ETSIC	0x0000000000000200ULL
#define	PSCAN_WWR	0x0000000000000400ULL
#define	PSCAN_MKKA1	0x0000000000000800ULL
#define	PSCAN_MKKA1_G	0x0000000000001000ULL
#define	PSCAN_MKKA2	0x0000000000002000ULL
#define	PSCAN_MKKA2_G	0x0000000000004000ULL
#define	PSCAN_MKK3	0x0000000000008000ULL
#define	PSCAN_DEFER	0x7FFFFFFFFFFFFFFFULL
#define	IS_ECM_CHAN	0x8000000000000000ULL

/*
 * The following are flags for different requirements per reg domain.
 * These requirements are either inhereted from the reg domain pair or
 * from the unitary reg domain if the reg domain pair flags value is 0
 */
enum {
	NO_REQ			= 0x00000000,	/* NB: must be zero */
	DISALLOW_ADHOC_11A	= 0x00000001,	/* adhoc not allowed in 5GHz */
	DISALLOW_ADHOC_11A_TURB	= 0x00000002,	/* not allowed w/ 5GHz turbo */
	NEED_NFC		= 0x00000004,	/* need noise floor check */
	ADHOC_PER_11D		= 0x00000008,	/* must receive 11d beacon */
	LIMIT_FRAME_4MS 	= 0x00000020,	/* 4msec tx burst limit */
	NO_HOSTAP		= 0x00000040,	/* No HOSTAP mode opereation */
};

/* Bit masks for DFS per regdomain */
enum {
	NO_DFS   = 0x0000000000000000ULL,	/* NB: must be zero */
	DFS_FCC3 = 0x0000000000000001ULL,
	DFS_ETSI = 0x0000000000000002ULL,
	DFS_MKK4 = 0x0000000000000004ULL,
};

enum {						/* conformance test limits */
	FCC	= 0x10,
	MKK	= 0x40,
	ETSI	= 0x30,
};

/*
 * THE following table is the mapping of regdomain pairs specified by
 * an 8 bit regdomain value to the individual unitary reg domains
 */
typedef struct regDomainPair {
	HAL_REG_DOMAIN regDmnEnum;	/* 16 bit reg domain pair */
	HAL_REG_DOMAIN regDmn5GHz;	/* 5GHz reg domain */
	HAL_REG_DOMAIN regDmn2GHz;	/* 2GHz reg domain */
	uint32_t flags5GHz;		/* Requirements flags (AdHoc
					   disallow, noise floor cal needed,
					   etc) */
	uint32_t flags2GHz;		/* Requirements flags (AdHoc
					   disallow, noise floor cal needed,
					   etc) */
	uint64_t pscanMask;		/* Passive Scan flags which
					   can override unitary domain
					   passive scan flags.  This
					   value is used as a mask on
					   the unitary flags*/
	uint16_t singleCC;		/* Country code of single country if
					   a one-on-one mapping exists */
}  REG_DMN_PAIR_MAPPING;

typedef struct {
	HAL_CTRY_CODE		countryCode;	   
	HAL_REG_DOMAIN		regDmnEnum;
} COUNTRY_CODE_TO_ENUM_RD;

/*
 * Frequency band collections are defined using bitmasks.  Each bit
 * in a mask is the index of an entry in one of the following tables.
 * Bitmasks are BMLEN*64 bits so if a table grows beyond that the bit
 * vectors must be enlarged or the tables split somehow (e.g. split
 * 1/2 and 1/4 rate channels into a separate table).
 *
 * Beware of ordering; the indices are defined relative to the preceding
 * entry so if things get off there will be confusion.  A good way to
 * check the indices is to collect them in a switch statement in a stub
 * function so the compiler checks for duplicates.
 */
typedef struct {
	uint16_t	lowChannel;	/* Low channel center in MHz */
	uint16_t	highChannel;	/* High Channel center in MHz */
	uint8_t		powerDfs;	/* Max power (dBm) for channel
					   range when using DFS */
	uint8_t		antennaMax;	/* Max allowed antenna gain */
	uint8_t		channelBW;	/* Bandwidth of the channel */
	uint8_t		channelSep;	/* Channel separation within
					   the band */
	uint64_t	useDfs;		/* Use DFS in the RegDomain
					   if corresponding bit is set */
	uint64_t	usePassScan;	/* Use Passive Scan in the RegDomain
					   if corresponding bit is set */
} REG_DMN_FREQ_BAND;

typedef struct regDomain {
	uint16_t regDmnEnum;		/* value from EnumRd table */
	uint8_t conformanceTestLimit;
	uint32_t flags;			/* Requirement flags (AdHoc disallow,
					   noise floor cal needed, etc) */
	uint64_t dfsMask;		/* DFS bitmask for 5Ghz tables */
	uint64_t pscan;			/* Bitmask for passive scan */
	chanbmask_t chan11a;		/* 11a channels */
	chanbmask_t chan11a_turbo;	/* 11a static turbo channels */
	chanbmask_t chan11a_dyn_turbo;	/* 11a dynamic turbo channels */
	chanbmask_t chan11a_half;	/* 11a 1/2 width channels */
	chanbmask_t chan11a_quarter;	/* 11a 1/4 width channels */
	chanbmask_t chan11b;		/* 11b channels */
	chanbmask_t chan11g;		/* 11g channels */
	chanbmask_t chan11g_turbo;	/* 11g dynamic turbo channels */
	chanbmask_t chan11g_half;	/* 11g 1/2 width channels */
	chanbmask_t chan11g_quarter;	/* 11g 1/4 width channels */
} REG_DOMAIN;

struct cmode {
	u_int		mode;
	u_int		flags;
	REG_DMN_FREQ_BAND *freqs;
};
#endif
