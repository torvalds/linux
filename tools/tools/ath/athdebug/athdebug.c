/*-
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

/*
 * athdebug [-i interface] flags
 * (default interface is wlan0).
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <ctype.h>
#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *progname;

/* XXX TODO: include if_ath_debug.h */
enum {
	ATH_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	ATH_DEBUG_XMIT_DESC	= 0x00000002,	/* xmit descriptors */
	ATH_DEBUG_RECV		= 0x00000004,	/* basic recv operation */
	ATH_DEBUG_RECV_DESC	= 0x00000008,	/* recv descriptors */
	ATH_DEBUG_RATE		= 0x00000010,	/* rate control */
	ATH_DEBUG_RESET		= 0x00000020,	/* reset processing */
	ATH_DEBUG_MODE		= 0x00000040,	/* mode init/setup */
	ATH_DEBUG_BEACON 	= 0x00000080,	/* beacon handling */
	ATH_DEBUG_WATCHDOG 	= 0x00000100,	/* watchdog timeout */
	ATH_DEBUG_INTR		= 0x00001000,	/* ISR */
	ATH_DEBUG_TX_PROC	= 0x00002000,	/* tx ISR proc */
	ATH_DEBUG_RX_PROC	= 0x00004000,	/* rx ISR proc */
	ATH_DEBUG_BEACON_PROC	= 0x00008000,	/* beacon ISR proc */
	ATH_DEBUG_CALIBRATE	= 0x00010000,	/* periodic calibration */
	ATH_DEBUG_KEYCACHE	= 0x00020000,	/* key cache management */
	ATH_DEBUG_STATE		= 0x00040000,	/* 802.11 state transitions */
	ATH_DEBUG_NODE		= 0x00080000,	/* node management */
	ATH_DEBUG_LED		= 0x00100000,	/* led management */
	ATH_DEBUG_FF		= 0x00200000,	/* fast frames */
	ATH_DEBUG_DFS		= 0x00400000,	/* DFS processing */
	ATH_DEBUG_TDMA		= 0x00800000,	/* TDMA processing */
	ATH_DEBUG_TDMA_TIMER	= 0x01000000,	/* TDMA timer processing */
	ATH_DEBUG_REGDOMAIN	= 0x02000000,	/* regulatory processing */
	ATH_DEBUG_FATAL		= 0x80000000,	/* fatal errors */
	ATH_DEBUG_ANY		= 0xffffffff
};

static struct {
	const char	*name;
	uint64_t	bit;
} flags[] = {
	{ "xmit",	ATH_DEBUG_XMIT },
	{ "xmit_desc",	ATH_DEBUG_XMIT_DESC },
	{ "recv",	ATH_DEBUG_RECV },
	{ "recv_desc",	ATH_DEBUG_RECV_DESC },
	{ "rate",	ATH_DEBUG_RATE },
	{ "reset",	ATH_DEBUG_RESET },
	{ "mode",	ATH_DEBUG_MODE },
	{ "beacon",	ATH_DEBUG_BEACON },
	{ "watchdog",	ATH_DEBUG_WATCHDOG },
	{ "intr",	ATH_DEBUG_INTR },
	{ "xmit_proc",	ATH_DEBUG_TX_PROC },
	{ "recv_proc",	ATH_DEBUG_RX_PROC },
	{ "beacon_proc",ATH_DEBUG_BEACON_PROC },
	{ "calibrate",	ATH_DEBUG_CALIBRATE },
	{ "keycache",	ATH_DEBUG_KEYCACHE },
	{ "state",	ATH_DEBUG_STATE },
	{ "node",	ATH_DEBUG_NODE },
	{ "led",	ATH_DEBUG_LED },
	{ "ff",		ATH_DEBUG_FF },
	{ "dfs",	ATH_DEBUG_DFS },
	{ "tdma",	ATH_DEBUG_TDMA },
	{ "tdma_timer",	ATH_DEBUG_TDMA_TIMER },
	{ "regdomain",	ATH_DEBUG_REGDOMAIN },
	{ "fatal",	ATH_DEBUG_FATAL },
};

static uint64_t
getflag(const char *name, int len)
{
	int i;

	for (i = 0; i < nitems(flags); i++)
		if (strncasecmp(flags[i].name, name, len) == 0)
			return flags[i].bit;
	return 0;
}

static const char *
getflagname(u_int flag)
{
	int i;

	for (i = 0; i < nitems(flags); i++)
		if (flags[i].bit == flag)
			return flags[i].name;
	return "???";
}

static void
usage(void)
{
	int i;

	fprintf(stderr, "usage: %s [-i device] [flags]\n", progname);
	fprintf(stderr, "where flags are:\n");
	for (i = 0; i < nitems(flags); i++)
		printf("%s\n", flags[i].name);
	exit(-1);
}

int
main(int argc, char *argv[])
{
	const char *ifname;
	const char *cp, *tp;
	const char *sep;
	int c, op, i;
	uint64_t debug, ndebug;
	size_t debuglen;
	char oid[256];

	ifname = getenv("ATH");
	if (ifname == NULL)
		ifname = ATH_DEFAULT;
	progname = argv[0];
	if (argc > 1) {
		if (strcmp(argv[1], "-i") == 0) {
			if (argc < 2)
				errx(1, "missing interface name for -i option");
			ifname = argv[2];
			if (strncmp(ifname, "ath", 3) != 0)
				errx(2, "huh, this is for ath devices?");
			argc -= 2, argv += 2;
		} else if (strcmp(argv[1], "-?") == 0)
			usage();
	}

#ifdef __linux__
	snprintf(oid, sizeof(oid), "dev.%s.debug", ifname);
#else
	snprintf(oid, sizeof(oid), "dev.ath.%s.debug", ifname+3);
#endif
	debuglen = sizeof(debug);
	if (sysctlbyname(oid, &debug, &debuglen, NULL, 0) < 0)
		err(1, "sysctl-get(%s)", oid);
	ndebug = debug;
	for (; argc > 1; argc--, argv++) {
		cp = argv[1];
		do {
			u_int bit;

			if (*cp == '-') {
				cp++;
				op = -1;
			} else if (*cp == '+') {
				cp++;
				op = 1;
			} else
				op = 0;
			for (tp = cp; *tp != '\0' && *tp != '+' && *tp != '-';)
				tp++;
			bit = getflag(cp, tp-cp);
			if (op < 0)
				ndebug &= ~bit;
			else if (op > 0)
				ndebug |= bit;
			else {
				if (bit == 0) {
					if (isdigit(*cp))
						bit = strtoul(cp, NULL, 0);
					else
						errx(1, "unknown flag %.*s",
							(int) (tp-cp), cp);
				}
				ndebug = bit;
			}
		} while (*(cp = tp) != '\0');
	}
	if (debug != ndebug) {
		printf("%s: 0x%llx => ", oid, (long long) debug);
		if (sysctlbyname(oid, NULL, NULL, &ndebug, sizeof(ndebug)) < 0)
			err(1, "sysctl-set(%s)", oid);
		printf("0x%llx", (long long) ndebug);
		debug = ndebug;
	} else
		printf("%s: 0x%llx", oid, (long long) debug);
	sep = "<";
	for (i = 0; i < nitems(flags); i++)
		if (debug & flags[i].bit) {
			printf("%s%s", sep, flags[i].name);
			sep = ",";
		}
	printf("%s\n", *sep != '<' ? ">" : "");
	return 0;
}
