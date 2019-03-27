/*-
 * Copyright (c) 2007 Sam Leffler, Errno Consulting
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
 * mwldebug [-i interface] flags
 * (default interface is mwl0).
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

const char *progname;

enum {
	MWL_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	MWL_DEBUG_XMIT_DESC	= 0x00000002,	/* xmit descriptors */
	MWL_DEBUG_RECV		= 0x00000004,	/* basic recv operation */
	MWL_DEBUG_RECV_DESC	= 0x00000008,	/* recv descriptors */
	MWL_DEBUG_RESET		= 0x00000010,	/* reset processing */
	MWL_DEBUG_BEACON 	= 0x00000020,	/* beacon handling */
	MWL_DEBUG_INTR		= 0x00000040,	/* ISR */
	MWL_DEBUG_TX_PROC	= 0x00000080,	/* tx ISR proc */
	MWL_DEBUG_RX_PROC	= 0x00000100,	/* rx ISR proc */
	MWL_DEBUG_KEYCACHE	= 0x00000200,	/* key cache management */
	MWL_DEBUG_STATE		= 0x00000400,	/* 802.11 state transitions */
	MWL_DEBUG_NODE		= 0x00000800,	/* node management */
	MWL_DEBUG_RECV_ALL	= 0x00001000,	/* trace all frames (beacons) */
	MWL_DEBUG_TSO		= 0x00002000,	/* TSO processing */
	MWL_DEBUG_AMPDU		= 0x00004000,	/* BA stream handling */
	MWL_DEBUG_ANY		= 0xffffffff
};

static struct {
	const char	*name;
	u_int		bit;
} flags[] = {
	{ "xmit",	MWL_DEBUG_XMIT },
	{ "xmit_desc",	MWL_DEBUG_XMIT_DESC },
	{ "recv",	MWL_DEBUG_RECV },
	{ "recv_desc",	MWL_DEBUG_RECV_DESC },
	{ "reset",	MWL_DEBUG_RESET },
	{ "beacon",	MWL_DEBUG_BEACON },
	{ "intr",	MWL_DEBUG_INTR },
	{ "xmit_proc",	MWL_DEBUG_TX_PROC },
	{ "recv_proc",	MWL_DEBUG_RX_PROC },
	{ "keycache",	MWL_DEBUG_KEYCACHE },
	{ "state",	MWL_DEBUG_STATE },
	{ "node",	MWL_DEBUG_NODE },
	{ "recv_all",	MWL_DEBUG_RECV_ALL },
	{ "tso",	MWL_DEBUG_TSO },
	{ "ampdu",	MWL_DEBUG_AMPDU },
	/* XXX these are a hack; there should be a separate sysctl knob */
	{ "hal",	0x02000000 },		/* cmd-completion processing */
	{ "hal2",	0x01000000 },		/* cmd submission processing */
	{ "halhang",	0x04000000 },		/* disable fw hang stuff */
};

static u_int
getflag(const char *name, int len)
{
	int i;

	for (i = 0; i < nitems(flags); i++)
		if (strncasecmp(flags[i].name, name, len) == 0)
			return flags[i].bit;
	return 0;
}

#if 0
static const char *
getflagname(u_int flag)
{
	int i;

	for (i = 0; i < nitems(flags); i++)
		if (flags[i].bit == flag)
			return flags[i].name;
	return "???";
}
#endif

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
	const char *ifname = "mwl0";
	const char *cp, *tp;
	const char *sep;
	int c, op, i;
	u_int32_t debug, ndebug;
	size_t debuglen;
	char oid[256];

	progname = argv[0];
	if (argc > 1) {
		if (strcmp(argv[1], "-i") == 0) {
			if (argc < 2)
				errx(1, "missing interface name for -i option");
			ifname = argv[2];
			if (strncmp(ifname, "mv", 2) != 0)
				errx(2, "huh, this is for mv devices?");
			argc -= 2, argv += 2;
		} else if (strcmp(argv[1], "-?") == 0)
			usage();
	}

	snprintf(oid, sizeof(oid), "dev.mwl.%s.debug", ifname+3);
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
					c = *cp;
					if (isdigit(c))
						bit = strtoul(cp, NULL, 0);
					else
						errx(1, "unknown flag %.*s",
							(int)(tp-cp), cp);
				}
				ndebug = bit;
			}
		} while (*(cp = tp) != '\0');
	}
	if (debug != ndebug) {
		printf("%s: 0x%x => ", oid, debug);
		if (sysctlbyname(oid, NULL, NULL, &ndebug, sizeof(ndebug)) < 0)
			err(1, "sysctl-set(%s)", oid);
		printf("0x%x", ndebug);
		debug = ndebug;
	} else
		printf("%s: 0x%x", oid, debug);
	sep = "<";
	for (i = 0; i < nitems(flags); i++)
		if (debug & flags[i].bit) {
			printf("%s%s", sep, flags[i].name);
			sep = ",";
		}
	printf("%s\n", *sep != '<' ? ">" : "");
	return 0;
}
