/*-
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * IEEE 802.11 PHY-related support.
 */

#include <sys/param.h>

#include <net/if_llc.h>

#include <net80211/_ieee80211.h>
#include <net80211/ieee80211.h>

#define	IEEE80211_F_SHPREAMBLE	0x00040000	/* STATUS: use short preamble */

#include <err.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

struct ieee80211_rate_table {
	int		rateCount;		/* NB: for proper padding */
	uint8_t		rateCodeToIndex[256];	/* back mapping */
	struct {
		uint8_t		phy;		/* CCK/OFDM/TURBO */
		uint32_t	rateKbps;	/* transfer rate in kbs */
		uint8_t		shortPreamble;	/* mask for enabling short
						 * preamble in CCK rate code */
		uint8_t		dot11Rate;	/* value for supported rates
						 * info element of MLME */
		uint8_t		ctlRateIndex;	/* index of next lower basic
						 * rate; used for dur. calcs */
		uint16_t	lpAckDuration;	/* long preamble ACK dur. */
		uint16_t	spAckDuration;	/* short preamble ACK dur. */
	} info[32];
};

uint16_t
ieee80211_compute_duration(const struct ieee80211_rate_table *rt,
	uint32_t frameLen, uint16_t rate, int isShortPreamble);

#define	KASSERT(c, msg) do {			\
	if (!(c)) {				\
		printf msg;			\
		putchar('\n');			\
		exit(-1);			\
	}					\
} while (0)

static void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	exit(-1);
}

/* shorthands to compact tables for readability */
#define	OFDM	IEEE80211_T_OFDM
#define	CCK	IEEE80211_T_CCK
#define	TURBO	IEEE80211_T_TURBO
#define	HALF	IEEE80211_T_OFDM_HALF
#define	QUART	IEEE80211_T_OFDM_QUARTER
#define	PBCC	(IEEE80211_T_OFDM_QUARTER+1)		/* XXX */
#define	B(r)	(0x80 | r)
#define	Mb(x)	(x*1000)

static struct ieee80211_rate_table ieee80211_11b_table = {
    .rateCount = 4,		/* XXX no PBCC */
    .info = {
/*                                   short            ctrl  */
/*                                Preamble  dot11Rate Rate */
     [0] = { .phy = CCK,     1000,    0x00,      B(2),   0 },/*   1 Mb */
     [1] = { .phy = CCK,     2000,    0x04,      B(4),   1 },/*   2 Mb */
     [2] = { .phy = CCK,     5500,    0x04,     B(11),   1 },/* 5.5 Mb */
     [3] = { .phy = CCK,    11000,    0x04,     B(22),   1 },/*  11 Mb */
     [4] = { .phy = PBCC,   22000,    0x04,        44,   3 } /*  22 Mb */
    },
};

static struct ieee80211_rate_table ieee80211_11g_table = {
    .rateCount = 12,
    .info = {
/*                                   short            ctrl  */
/*                                Preamble  dot11Rate Rate */
     [0] = { .phy = CCK,     1000,    0x00,      B(2),   0 },
     [1] = { .phy = CCK,     2000,    0x04,      B(4),   1 },
     [2] = { .phy = CCK,     5500,    0x04,     B(11),   2 },
     [3] = { .phy = CCK,    11000,    0x04,     B(22),   3 },
     [4] = { .phy = OFDM,    6000,    0x00,        12,   4 },
     [5] = { .phy = OFDM,    9000,    0x00,        18,   4 },
     [6] = { .phy = OFDM,   12000,    0x00,        24,   6 },
     [7] = { .phy = OFDM,   18000,    0x00,        36,   6 },
     [8] = { .phy = OFDM,   24000,    0x00,        48,   8 },
     [9] = { .phy = OFDM,   36000,    0x00,        72,   8 },
    [10] = { .phy = OFDM,   48000,    0x00,        96,   8 },
    [11] = { .phy = OFDM,   54000,    0x00,       108,   8 }
    },
};

static struct ieee80211_rate_table ieee80211_11a_table = {
    .rateCount = 8,
    .info = {
/*                                   short            ctrl  */
/*                                Preamble  dot11Rate Rate */
     [0] = { .phy = OFDM,    6000,    0x00,     B(12),   0 },
     [1] = { .phy = OFDM,    9000,    0x00,        18,   0 },
     [2] = { .phy = OFDM,   12000,    0x00,     B(24),   2 },
     [3] = { .phy = OFDM,   18000,    0x00,        36,   2 },
     [4] = { .phy = OFDM,   24000,    0x00,     B(48),   4 },
     [5] = { .phy = OFDM,   36000,    0x00,        72,   4 },
     [6] = { .phy = OFDM,   48000,    0x00,        96,   4 },
     [7] = { .phy = OFDM,   54000,    0x00,       108,   4 }
    },
};

static struct ieee80211_rate_table ieee80211_half_table = {
    .rateCount = 8,
    .info = {
/*                                   short            ctrl  */
/*                                Preamble  dot11Rate Rate */
     [0] = { .phy = HALF,    3000,    0x00,      B(6),   0 },
     [1] = { .phy = HALF,    4500,    0x00,         9,   0 },
     [2] = { .phy = HALF,    6000,    0x00,     B(12),   2 },
     [3] = { .phy = HALF,    9000,    0x00,        18,   2 },
     [4] = { .phy = HALF,   12000,    0x00,     B(24),   4 },
     [5] = { .phy = HALF,   18000,    0x00,        36,   4 },
     [6] = { .phy = HALF,   24000,    0x00,        48,   4 },
     [7] = { .phy = HALF,   27000,    0x00,        54,   4 }
    },
};

static struct ieee80211_rate_table ieee80211_quarter_table = {
    .rateCount = 8,
    .info = {
/*                                   short            ctrl  */
/*                                Preamble  dot11Rate Rate */
     [0] = { .phy = QUART,   1500,    0x00,      B(3),   0 },
     [1] = { .phy = QUART,   2250,    0x00,         4,   0 },
     [2] = { .phy = QUART,   3000,    0x00,      B(9),   2 },
     [3] = { .phy = QUART,   4500,    0x00,         9,   2 },
     [4] = { .phy = QUART,   6000,    0x00,     B(12),   4 },
     [5] = { .phy = QUART,   9000,    0x00,        18,   4 },
     [6] = { .phy = QUART,  12000,    0x00,        24,   4 },
     [7] = { .phy = QUART,  13500,    0x00,        27,   4 }
    },
};

static struct ieee80211_rate_table ieee80211_turbog_table = {
    .rateCount = 7,
    .info = {
/*                                   short            ctrl  */
/*                                Preamble  dot11Rate Rate */
     [0] = { .phy = TURBO,   12000,   0x00,     B(12),   0 },
     [1] = { .phy = TURBO,   24000,   0x00,     B(24),   1 },
     [2] = { .phy = TURBO,   36000,   0x00,        36,   1 },
     [3] = { .phy = TURBO,   48000,   0x00,     B(48),   3 },
     [4] = { .phy = TURBO,   72000,   0x00,        72,   3 },
     [5] = { .phy = TURBO,   96000,   0x00,        96,   3 },
     [6] = { .phy = TURBO,  108000,   0x00,       108,   3 }
    },
};

static struct ieee80211_rate_table ieee80211_turboa_table = {
    .rateCount = 8,
    .info = {
/*                                   short            ctrl  */
/*                                Preamble  dot11Rate Rate */
     [0] = { .phy = TURBO,   12000,   0x00,     B(12),   0 },
     [1] = { .phy = TURBO,   18000,   0x00,        18,   0 },
     [2] = { .phy = TURBO,   24000,   0x00,     B(24),   2 },
     [3] = { .phy = TURBO,   36000,   0x00,        36,   2 },
     [4] = { .phy = TURBO,   48000,   0x00,     B(48),   4 },
     [5] = { .phy = TURBO,   72000,   0x00,        72,   4 },
     [6] = { .phy = TURBO,   96000,   0x00,        96,   4 },
     [7] = { .phy = TURBO,  108000,   0x00,       108,   4 }
    },
};

#undef	Mb
#undef	B
#undef	OFDM
#undef	CCK
#undef	TURBO
#undef	XR

/*
 * Setup a rate table's reverse lookup table and fill in
 * ack durations.  The reverse lookup tables are assumed
 * to be initialized to zero (or at least the first entry).
 * We use this as a key that indicates whether or not
 * we've previously setup the reverse lookup table.
 *
 * XXX not reentrant, but shouldn't matter
 */
static void
ieee80211_setup_ratetable(struct ieee80211_rate_table *rt)
{
#define	WLAN_CTRL_FRAME_SIZE \
	(sizeof(struct ieee80211_frame_ack) + IEEE80211_CRC_LEN)

	int i;

	for (i = 0; i < nitems(rt->rateCodeToIndex); i++)
		rt->rateCodeToIndex[i] = (uint8_t) -1;
	for (i = 0; i < rt->rateCount; i++) {
		uint8_t code = rt->info[i].dot11Rate;
		uint8_t cix = rt->info[i].ctlRateIndex;
		uint8_t ctl_rate = rt->info[cix].dot11Rate;

		rt->rateCodeToIndex[code] = i;
		if (code & IEEE80211_RATE_BASIC) {
			/*
			 * Map w/o basic rate bit too.
			 */
			code &= IEEE80211_RATE_VAL;
			rt->rateCodeToIndex[code] = i;
		}

		/*
		 * XXX for 11g the control rate to use for 5.5 and 11 Mb/s
		 *     depends on whether they are marked as basic rates;
		 *     the static tables are setup with an 11b-compatible
		 *     2Mb/s rate which will work but is suboptimal
		 *
		 * NB: Control rate is always less than or equal to the
		 *     current rate, so control rate's reverse lookup entry
		 *     has been installed and following call is safe.
		 */
		rt->info[i].lpAckDuration = ieee80211_compute_duration(rt,
			WLAN_CTRL_FRAME_SIZE, ctl_rate, 0);
		rt->info[i].spAckDuration = ieee80211_compute_duration(rt,
			WLAN_CTRL_FRAME_SIZE, ctl_rate, IEEE80211_F_SHPREAMBLE);
	}

#undef WLAN_CTRL_FRAME_SIZE
}

/* Setup all rate tables */
static void
ieee80211_phy_init(void)
{
	static struct ieee80211_rate_table * const ratetables[] = {
		&ieee80211_half_table,
		&ieee80211_quarter_table,
		&ieee80211_11a_table,
		&ieee80211_11g_table,
		&ieee80211_turbog_table,
		&ieee80211_turboa_table,
		&ieee80211_turboa_table,
		&ieee80211_11a_table,
		&ieee80211_11g_table,
		&ieee80211_11b_table
	};
	unsigned int i;

	for (i = 0; i < nitems(ratetables); ++i)
		ieee80211_setup_ratetable(ratetables[i]);

}
#define CCK_SIFS_TIME		10
#define CCK_PREAMBLE_BITS	144
#define CCK_PLCP_BITS		48

#define OFDM_SIFS_TIME		16
#define OFDM_PREAMBLE_TIME	20
#define OFDM_PLCP_BITS		22
#define OFDM_SYMBOL_TIME	4

#define OFDM_HALF_SIFS_TIME	32
#define OFDM_HALF_PREAMBLE_TIME	40
#define OFDM_HALF_PLCP_BITS	22
#define OFDM_HALF_SYMBOL_TIME	8

#define OFDM_QUARTER_SIFS_TIME 		64
#define OFDM_QUARTER_PREAMBLE_TIME	80
#define OFDM_QUARTER_PLCP_BITS		22
#define OFDM_QUARTER_SYMBOL_TIME	16

#define TURBO_SIFS_TIME		8
#define TURBO_PREAMBLE_TIME	14
#define TURBO_PLCP_BITS		22
#define TURBO_SYMBOL_TIME	4

#define	HT_L_STF	8
#define	HT_L_LTF	8
#define	HT_L_SIG	4
#define	HT_SIG		8
#define	HT_STF		4
#define	HT_LTF(n)	((n) * 4)

/*
 * Compute the time to transmit a frame of length frameLen bytes
 * using the specified rate, phy, and short preamble setting.
 * SIFS is included.
 */
uint16_t
ieee80211_compute_duration(const struct ieee80211_rate_table *rt,
	uint32_t frameLen, uint16_t rate, int isShortPreamble)
{
	uint8_t rix = rt->rateCodeToIndex[rate];
	uint32_t bitsPerSymbol, numBits, numSymbols, phyTime, txTime;
	uint32_t kbps;

	KASSERT(rix != (uint8_t)-1, ("rate %d has no info", rate));
	kbps = rt->info[rix].rateKbps;
	if (kbps == 0)			/* XXX bandaid for channel changes */
		return 0;

	switch (rt->info[rix].phy) {
	case IEEE80211_T_CCK:
		phyTime		= CCK_PREAMBLE_BITS + CCK_PLCP_BITS;
		if (isShortPreamble && rt->info[rix].shortPreamble)
			phyTime >>= 1;
		numBits		= frameLen << 3;
		txTime		= CCK_SIFS_TIME + phyTime
				+ ((numBits * 1000)/kbps);
		break;
	case IEEE80211_T_OFDM:
		bitsPerSymbol	= (kbps * OFDM_SYMBOL_TIME) / 1000;
		KASSERT(bitsPerSymbol != 0, ("full rate bps"));

		numBits		= OFDM_PLCP_BITS + (frameLen << 3);
		numSymbols	= howmany(numBits, bitsPerSymbol);
		txTime		= OFDM_SIFS_TIME
				+ OFDM_PREAMBLE_TIME
				+ (numSymbols * OFDM_SYMBOL_TIME);
		break;
	case IEEE80211_T_OFDM_HALF:
		bitsPerSymbol	= (kbps * OFDM_HALF_SYMBOL_TIME) / 1000;
		KASSERT(bitsPerSymbol != 0, ("1/4 rate bps"));

		numBits		= OFDM_PLCP_BITS + (frameLen << 3);
		numSymbols	= howmany(numBits, bitsPerSymbol);
		txTime		= OFDM_HALF_SIFS_TIME
				+ OFDM_HALF_PREAMBLE_TIME
				+ (numSymbols * OFDM_HALF_SYMBOL_TIME);
		break;
	case IEEE80211_T_OFDM_QUARTER:
		bitsPerSymbol	= (kbps * OFDM_QUARTER_SYMBOL_TIME) / 1000;
		KASSERT(bitsPerSymbol != 0, ("1/2 rate bps"));

		numBits		= OFDM_PLCP_BITS + (frameLen << 3);
		numSymbols	= howmany(numBits, bitsPerSymbol);
		txTime		= OFDM_QUARTER_SIFS_TIME
				+ OFDM_QUARTER_PREAMBLE_TIME
				+ (numSymbols * OFDM_QUARTER_SYMBOL_TIME);
		break;
	case IEEE80211_T_TURBO:
		/* we still save OFDM rates in kbps - so double them */
		bitsPerSymbol = ((kbps << 1) * TURBO_SYMBOL_TIME) / 1000;
		KASSERT(bitsPerSymbol != 0, ("turbo bps"));

		numBits       = TURBO_PLCP_BITS + (frameLen << 3);
		numSymbols    = howmany(numBits, bitsPerSymbol);
		txTime        = TURBO_SIFS_TIME + TURBO_PREAMBLE_TIME
			      + (numSymbols * TURBO_SYMBOL_TIME);
		break;
	default:
		panic("%s: unknown phy %u (rate %u)\n", __func__,
		      rt->info[rix].phy, rate);
		break;
	}
	return txTime;
}

uint32_t
ieee80211_compute_duration_ht(const struct ieee80211_rate_table *rt,
	uint32_t frameLen, uint16_t rate,
	int streams, int isht40, int isShortGI)
{
	static const uint16_t ht20_bps[16] = {
	    26, 52, 78, 104, 156, 208, 234, 260,
	    52, 104, 156, 208, 312, 416, 468, 520
	};
	static const uint16_t ht40_bps[16] = {
	    54, 108, 162, 216, 324, 432, 486, 540,
	    108, 216, 324, 432, 648, 864, 972, 1080,
	};
	uint32_t bitsPerSymbol, numBits, numSymbols, txTime;

	KASSERT(rate & IEEE80211_RATE_MCS, ("not mcs %d", rate));
	KASSERT((rate &~ IEEE80211_RATE_MCS) < 16, ("bad mcs 0x%x", rate));

	if (isht40)
		bitsPerSymbol = ht40_bps[rate & 0xf];
	else
		bitsPerSymbol = ht20_bps[rate & 0xf];
	numBits = OFDM_PLCP_BITS + (frameLen << 3);
	numSymbols = howmany(numBits, bitsPerSymbol);
	if (isShortGI)
		txTime = ((numSymbols * 18) + 4) / 5;	/* 3.6us */
	else
		txTime = numSymbols * 4;		/* 4us */
	return txTime + HT_L_STF + HT_L_LTF +
	    HT_L_SIG + HT_SIG + HT_STF + HT_LTF(streams);
}

static const struct ieee80211_rate_table *
mode2table(const char *mode)
{
	if (strcasecmp(mode, "half") == 0)
		return &ieee80211_half_table;
	else if (strcasecmp(mode, "quarter") == 0)
		return &ieee80211_quarter_table;
	else if (strcasecmp(mode, "hta") == 0)
		return &ieee80211_11a_table;	/* XXX */
	else if (strcasecmp(mode, "htg") == 0)
		return &ieee80211_11g_table;	/* XXX */
	else if (strcasecmp(mode, "108g") == 0)
		return &ieee80211_turbog_table;
	else if (strcasecmp(mode, "sturbo") == 0)
		return &ieee80211_turboa_table;
	else if (strcasecmp(mode, "turbo") == 0)
		return &ieee80211_turboa_table;
	else if (strcasecmp(mode, "11a") == 0)
		return &ieee80211_11a_table;
	else if (strcasecmp(mode, "11g") == 0)
		return &ieee80211_11g_table;
	else if (strcasecmp(mode, "11b") == 0)
		return &ieee80211_11b_table;
	else
		return NULL;
}

const char *
srate(int rate)
{
	static char buf[32];
	if (rate & 1)
		snprintf(buf, sizeof(buf), "%u.5", rate/2);
	else
		snprintf(buf, sizeof(buf), "%u", rate/2);
	return buf;
}

static int
checkpreamble(const struct ieee80211_rate_table *rt, uint8_t rix,
	int isShortPreamble, int verbose)
{
	if (isShortPreamble) {
		if (rt->info[rix].phy != IEEE80211_T_CCK) {
			if (verbose)
				warnx("short preamble not meaningful, ignored");
			isShortPreamble = 0;
		} else if (!rt->info[rix].shortPreamble) {
			if (verbose)
				warnx("short preamble not meaningful with "
				    "rate %s, ignored",
				    srate(rt->info[rix].dot11Rate &~ IEEE80211_RATE_BASIC));
			isShortPreamble = 0;
		}
	}
	return isShortPreamble;
}

static void
usage(const char *progname)
{
	fprintf(stderr, "usage: %s [-a] [-l framelen] [-m mode] [-r rate] [-s]\n",
	    progname);
	fprintf(stderr, "-a             display calculations for all possible rates\n");
	fprintf(stderr, "-l framelen    length in bytes of 802.11 payload (default 1536)\n");
	fprintf(stderr, "-m 11a         calculate for 11a channel\n");
	fprintf(stderr, "-m 11b         calculate for 11b channel\n");
	fprintf(stderr, "-m 11g         calculate for 11g channel (default)\n");
	fprintf(stderr, "-m half        calculate for 1/2 width channel\n");
	fprintf(stderr, "-m quarter     calculate for 1/4 width channel\n");
	fprintf(stderr, "-m 108g        calculate for dynamic turbo 11g channel\n");
	fprintf(stderr, "-m sturbo      calculate for static turbo channel\n");
	fprintf(stderr, "-m turbo       calculate for dynamic turbo 11a channel\n");
	fprintf(stderr, "-r rate        IEEE rate code (default 54)\n");
	fprintf(stderr, "-s             short preamble (default long)\n");
	exit(0);
}

int
main(int argc, char *argv[])
{
	const struct ieee80211_rate_table *rt;
	const char *mode;
	uint32_t frameLen;
	uint16_t rate;
	uint16_t time;
	uint8_t rix;
	int ch, allrates, isShortPreamble, isShort;
	float frate;

	ieee80211_phy_init();

	mode = "11g";
	isShortPreamble = 0;
	frameLen = 1500
		 + sizeof(struct ieee80211_frame)
		 + LLC_SNAPFRAMELEN
		 + IEEE80211_CRC_LEN
		 ;
	rate = 2*54;
	allrates = 0;
	while ((ch = getopt(argc, argv, "al:m:r:s")) != -1) {
		switch (ch) {
		case 'a':
			allrates = 1;
			break;
		case 'l':
			frameLen = strtoul(optarg, NULL, 0);
			break;
		case 'm':
			mode = optarg;
			break;
		case 'r':
			frate = atof(optarg);
			rate = (int) 2*frate;
			break;
		case 's':
			isShortPreamble = 1;
			break;
		default:
			usage(argv[0]);
			break;
		}
	}
	rt = mode2table(mode);
	if (rt == NULL)
		errx(-1, "unknown mode %s", mode);
	if (!allrates) {
		rix = rt->rateCodeToIndex[rate];
		if (rix == (uint8_t) -1)
			errx(-1, "rate %s not valid for mode %s", srate(rate), mode);
		isShort = checkpreamble(rt, rix, isShortPreamble, 1);

		time = ieee80211_compute_duration(rt, frameLen, rate, isShort);
		printf("%u usec to send %u bytes @ %s Mb/s, %s preamble\n",
		    time, frameLen, srate(rate),
		    isShort ? "short" : "long");
	} else {
		for (rix = 0; rix < rt->rateCount; rix++) {
			rate = rt->info[rix].dot11Rate &~ IEEE80211_RATE_BASIC;
			isShort = checkpreamble(rt, rix, isShortPreamble, 0);
			time = ieee80211_compute_duration(rt, frameLen, rate,
			    isShort);
			printf("%u usec to send %u bytes @ %s Mb/s, %s preamble\n",
			    time, frameLen, srate(rate),
			    isShort ? "short" : "long");
		}
	}
	return 0;
}
