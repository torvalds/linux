/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 * wlandebug [-i interface] flags
 * (default interface is wlan.0).
 */
#include <sys/types.h>
#include <sys/sysctl.h>

#include <net/if.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <string.h>
#include <err.h>

#include <libifconfig.h>

#define	N(a)	(sizeof(a)/sizeof(a[0]))

const char *progname;

#define	IEEE80211_MSG_11N	0x80000000	/* 11n mode debug */
#define	IEEE80211_MSG_DEBUG	0x40000000	/* IFF_DEBUG equivalent */
#define	IEEE80211_MSG_DUMPPKTS	0x20000000	/* IFF_LINK2 equivalant */
#define	IEEE80211_MSG_CRYPTO	0x10000000	/* crypto work */
#define	IEEE80211_MSG_INPUT	0x08000000	/* input handling */
#define	IEEE80211_MSG_XRATE	0x04000000	/* rate set handling */
#define	IEEE80211_MSG_ELEMID	0x02000000	/* element id parsing */
#define	IEEE80211_MSG_NODE	0x01000000	/* node handling */
#define	IEEE80211_MSG_ASSOC	0x00800000	/* association handling */
#define	IEEE80211_MSG_AUTH	0x00400000	/* authentication handling */
#define	IEEE80211_MSG_SCAN	0x00200000	/* scanning */
#define	IEEE80211_MSG_OUTPUT	0x00100000	/* output handling */
#define	IEEE80211_MSG_STATE	0x00080000	/* state machine */
#define	IEEE80211_MSG_POWER	0x00040000	/* power save handling */
#define	IEEE80211_MSG_HWMP	0x00020000	/* hybrid mesh protocol */
#define	IEEE80211_MSG_DOT1XSM	0x00010000	/* 802.1x state machine */
#define	IEEE80211_MSG_RADIUS	0x00008000	/* 802.1x radius client */
#define	IEEE80211_MSG_RADDUMP	0x00004000	/* dump 802.1x radius packets */
#define	IEEE80211_MSG_MESH	0x00002000	/* mesh networking */
#define	IEEE80211_MSG_WPA	0x00001000	/* WPA/RSN protocol */
#define	IEEE80211_MSG_ACL	0x00000800	/* ACL handling */
#define	IEEE80211_MSG_WME	0x00000400	/* WME protocol */
#define	IEEE80211_MSG_SUPERG	0x00000200	/* Atheros SuperG protocol */
#define	IEEE80211_MSG_DOTH	0x00000100	/* 802.11h support */
#define	IEEE80211_MSG_INACT	0x00000080	/* inactivity handling */
#define	IEEE80211_MSG_ROAM	0x00000040	/* sta-mode roaming */
#define	IEEE80211_MSG_RATECTL	0x00000020	/* tx rate control */
#define	IEEE80211_MSG_ACTION	0x00000010	/* action frame handling */
#define	IEEE80211_MSG_WDS	0x00000008	/* WDS handling */
#define	IEEE80211_MSG_IOCTL	0x00000004	/* ioctl handling */
#define	IEEE80211_MSG_TDMA	0x00000002	/* TDMA handling */

static struct {
	const char	*name;
	u_int		bit;
} flags[] = {
	{ "11n",	IEEE80211_MSG_11N },
	{ "debug",	IEEE80211_MSG_DEBUG },
	{ "dumppkts",	IEEE80211_MSG_DUMPPKTS },
	{ "crypto",	IEEE80211_MSG_CRYPTO },
	{ "input",	IEEE80211_MSG_INPUT },
	{ "xrate",	IEEE80211_MSG_XRATE },
	{ "elemid",	IEEE80211_MSG_ELEMID },
	{ "node",	IEEE80211_MSG_NODE },
	{ "assoc",	IEEE80211_MSG_ASSOC },
	{ "auth",	IEEE80211_MSG_AUTH },
	{ "scan",	IEEE80211_MSG_SCAN },
	{ "output",	IEEE80211_MSG_OUTPUT },
	{ "state",	IEEE80211_MSG_STATE },
	{ "power",	IEEE80211_MSG_POWER },
	{ "hwmp",	IEEE80211_MSG_HWMP },
	{ "dot1xsm",	IEEE80211_MSG_DOT1XSM },
	{ "radius",	IEEE80211_MSG_RADIUS },
	{ "raddump",	IEEE80211_MSG_RADDUMP },
	{ "mesh",	IEEE80211_MSG_MESH },
	{ "wpa",	IEEE80211_MSG_WPA },
	{ "acl",	IEEE80211_MSG_ACL },
	{ "wme",	IEEE80211_MSG_WME },
	{ "superg",	IEEE80211_MSG_SUPERG },
	{ "doth",	IEEE80211_MSG_DOTH },
	{ "inact",	IEEE80211_MSG_INACT },
	{ "roam",	IEEE80211_MSG_ROAM },
	{ "rate",	IEEE80211_MSG_RATECTL },
	{ "action",	IEEE80211_MSG_ACTION },
	{ "wds",	IEEE80211_MSG_WDS },
	{ "ioctl",	IEEE80211_MSG_IOCTL },
	{ "tdma",	IEEE80211_MSG_TDMA },
};

static u_int
getflag(const char *name, int len)
{
	int i;

	for (i = 0; i < N(flags); i++)
		if (strncasecmp(flags[i].name, name, len) == 0)
			return flags[i].bit;
	return 0;
}

static void
usage(void)
{
	int i;

	fprintf(stderr, "usage: %s [-d | -i device] [flags]\n", progname);
	fprintf(stderr, "where flags are:\n");
	for (i = 0; i < N(flags); i++)
		printf("%s\n", flags[i].name);
	exit(-1);
}

static void
setoid(char oid[], size_t oidlen, const char *wlan)
{
#ifdef __linux__
	if (wlan)
		snprintf(oid, oidlen, "net.%s.debug", wlan);
#elif __FreeBSD__
	if (wlan)
		snprintf(oid, oidlen, "net.wlan.%s.debug", wlan+4);
	else
		snprintf(oid, oidlen, "net.wlan.debug");
#elif __NetBSD__
	if (wlan)
		snprintf(oid, oidlen, "net.link.ieee80211.%s.debug", wlan);
	else
		snprintf(oid, oidlen, "net.link.ieee80211.debug");
#else
#error "No support for this system"
#endif
}

static void
get_orig_iface_name(char *oid, size_t oid_size, char *name)
{
	struct ifconfig_handle *h;
	char *orig_name;

	h = ifconfig_open();
	if (ifconfig_get_orig_name(h, name, &orig_name) < 0) {
		/* check for original interface name. */
		orig_name = name;
	}

	if (strlen(orig_name) < strlen("wlan") + 1 ||
	    strncmp(orig_name, "wlan", 4) != 0)
		errx(1, "expecting a wlan interface name");

	ifconfig_close(h);
	setoid(oid, oid_size, orig_name);
	if (orig_name != name)
		free(orig_name);
}

int
main(int argc, char *argv[])
{
	const char *cp, *tp;
	const char *sep;
	int op, i;
	u_int32_t debug, ndebug;
	size_t debuglen;
	char oid[256];

	progname = argv[0];
	setoid(oid, sizeof(oid), "wlan0");
	if (argc > 1) {
		if (strcmp(argv[1], "-d") == 0) {
			setoid(oid, sizeof(oid), NULL);
			argc -= 1, argv += 1;
		} else if (strcmp(argv[1], "-i") == 0) {
			if (argc <= 2)
				errx(1, "missing interface name for -i option");
			get_orig_iface_name(oid, sizeof(oid), argv[2]);
			argc -= 2, argv += 2;
		} else if (strcmp(argv[1], "-?") == 0)
			usage();
	}

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
					int c = *cp;
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
	for (i = 0; i < N(flags); i++)
		if (debug & flags[i].bit) {
			printf("%s%s", sep, flags[i].name);
			sep = ",";
		}
	printf("%s\n", *sep != '<' ? ">" : "");
	return 0;
}
