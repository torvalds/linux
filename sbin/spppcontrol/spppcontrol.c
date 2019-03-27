/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 2001 Joerg Wunsch
 *
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
 * THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE DEVELOPERS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_sppp.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

static void usage(void);
void	print_vals(const char *ifname, struct spppreq *sp);
const char *phase_name(enum ppp_phase phase);
const char *proto_name(u_short proto);
const char *authflags(u_short flags);

#define PPP_PAP		0xc023
#define PPP_CHAP	0xc223

int
main(int argc, char **argv)
{
	int s, c;
	int errs = 0, verbose = 0;
	size_t off;
	long to;
	char *endp;
	const char *ifname, *cp;
	struct ifreq ifr;
	struct spppreq spr;

	while ((c = getopt(argc, argv, "v")) != -1)
		switch (c) {
		case 'v':
			verbose++;
			break;

		default:
			errs++;
			break;
		}
	argv += optind;
	argc -= optind;

	if (errs || argc < 1)
		usage();

	ifname = argv[0];
	strncpy(ifr.ifr_name, ifname, sizeof ifr.ifr_name);

	/* use a random AF to create the socket */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err(EX_UNAVAILABLE, "ifconfig: socket");

	argc--;
	argv++;

	spr.cmd = (uintptr_t) SPPPIOGDEFS;
	ifr.ifr_data = (caddr_t)&spr;

	if (ioctl(s, SIOCGIFGENERIC, &ifr) == -1)
		err(EX_OSERR, "SIOCGIFGENERIC(SPPPIOGDEFS)");

	if (argc == 0) {
		/* list only mode */
		print_vals(ifname, &spr);
		return 0;
	}

#define startswith(s) strncmp(argv[0], s, (off = strlen(s))) == 0

	while (argc > 0) {
		if (startswith("authproto=")) {
			cp = argv[0] + off;
			if (strcmp(cp, "pap") == 0)
				spr.defs.myauth.proto =
					spr.defs.hisauth.proto = PPP_PAP;
			else if (strcmp(cp, "chap") == 0)
				spr.defs.myauth.proto =
					spr.defs.hisauth.proto = PPP_CHAP;
			else if (strcmp(cp, "none") == 0)
				spr.defs.myauth.proto =
					spr.defs.hisauth.proto = 0;
			else
				errx(EX_DATAERR, "bad auth proto: %s", cp);
		} else if (startswith("myauthproto=")) {
			cp = argv[0] + off;
			if (strcmp(cp, "pap") == 0)
				spr.defs.myauth.proto = PPP_PAP;
			else if (strcmp(cp, "chap") == 0)
				spr.defs.myauth.proto = PPP_CHAP;
			else if (strcmp(cp, "none") == 0)
				spr.defs.myauth.proto = 0;
			else
				errx(EX_DATAERR, "bad auth proto: %s", cp);
		} else if (startswith("myauthname="))
			strncpy(spr.defs.myauth.name, argv[0] + off,
				AUTHNAMELEN);
		else if (startswith("myauthsecret=") ||
			 startswith("myauthkey="))
			strncpy(spr.defs.myauth.secret, argv[0] + off,
				AUTHKEYLEN);
		else if (startswith("hisauthproto=")) {
			cp = argv[0] + off;
			if (strcmp(cp, "pap") == 0)
				spr.defs.hisauth.proto = PPP_PAP;
			else if (strcmp(cp, "chap") == 0)
				spr.defs.hisauth.proto = PPP_CHAP;
			else if (strcmp(cp, "none") == 0)
				spr.defs.hisauth.proto = 0;
			else
				errx(EX_DATAERR, "bad auth proto: %s", cp);
		} else if (startswith("hisauthname="))
			strncpy(spr.defs.hisauth.name, argv[0] + off,
				AUTHNAMELEN);
		else if (startswith("hisauthsecret=") ||
			 startswith("hisauthkey="))
			strncpy(spr.defs.hisauth.secret, argv[0] + off,
				AUTHKEYLEN);
		else if (strcmp(argv[0], "callin") == 0)
			spr.defs.hisauth.flags |= AUTHFLAG_NOCALLOUT;
		else if (strcmp(argv[0], "always") == 0)
			spr.defs.hisauth.flags &= ~AUTHFLAG_NOCALLOUT;
		else if (strcmp(argv[0], "norechallenge") == 0)
			spr.defs.hisauth.flags |= AUTHFLAG_NORECHALLENGE;
		else if (strcmp(argv[0], "rechallenge") == 0)
			spr.defs.hisauth.flags &= ~AUTHFLAG_NORECHALLENGE;
		else if (startswith("lcp-timeout=")) {
			cp = argv[0] + off;
			to = strtol(cp, &endp, 10);
			if (*cp == '\0' || *endp != '\0' ||
			    /*
			     * NB: 10 ms is the minimal possible value for
			     * hz=100.  We assume no kernel has less clock
			     * frequency than that...
			     */
			    to < 10 || to > 20000)
				errx(EX_DATAERR, "bad lcp timeout value: %s",
				     cp);
			spr.defs.lcp.timeout = to;
		} else if (strcmp(argv[0], "enable-vj") == 0)
			spr.defs.enable_vj = 1;
		else if (strcmp(argv[0], "disable-vj") == 0)
			spr.defs.enable_vj = 0;
		else if (strcmp(argv[0], "enable-ipv6") == 0)
			spr.defs.enable_ipv6 = 1;
		else if (strcmp(argv[0], "disable-ipv6") == 0)
			spr.defs.enable_ipv6 = 0;
		else
			errx(EX_DATAERR, "bad parameter: \"%s\"", argv[0]);

		argv++;
		argc--;
	}

	spr.cmd = (uintptr_t)SPPPIOSDEFS;

	if (ioctl(s, SIOCSIFGENERIC, &ifr) == -1)
		err(EX_OSERR, "SIOCSIFGENERIC(SPPPIOSDEFS)");

	if (verbose)
		print_vals(ifname, &spr);

	return 0;
}

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n",
	"usage: spppcontrol [-v] ifname [{my|his}auth{proto|name|secret}=...]",
	"       spppcontrol [-v] ifname callin|always");
	exit(EX_USAGE);
}

void
print_vals(const char *ifname, struct spppreq *sp)
{
	printf("%s:\tphase=%s\n", ifname, phase_name(sp->defs.pp_phase));
	if (sp->defs.myauth.proto) {
		printf("\tmyauthproto=%s myauthname=\"%.*s\"\n",
		       proto_name(sp->defs.myauth.proto),
		       AUTHNAMELEN, sp->defs.myauth.name);
	}
	if (sp->defs.hisauth.proto) {
		printf("\thisauthproto=%s hisauthname=\"%.*s\"%s\n",
		       proto_name(sp->defs.hisauth.proto),
		       AUTHNAMELEN, sp->defs.hisauth.name,
		       authflags(sp->defs.hisauth.flags));
	}
	printf("\tlcp-timeout=%d ms\n", sp->defs.lcp.timeout);
	printf("\t%sable-vj\n", sp->defs.enable_vj? "en": "dis");
	printf("\t%sable-ipv6\n", sp->defs.enable_ipv6? "en": "dis");
}

const char *
phase_name(enum ppp_phase phase)
{
	switch (phase) {
	case PHASE_DEAD:	return "dead";
	case PHASE_ESTABLISH:	return "establish";
	case PHASE_TERMINATE:	return "terminate";
	case PHASE_AUTHENTICATE: return "authenticate";
	case PHASE_NETWORK:	return "network";
	}
	return "illegal";
}

const char *
proto_name(u_short proto)
{
	static char buf[12];
	switch (proto) {
	case PPP_PAP:	return "pap";
	case PPP_CHAP:	return "chap";
	}
	sprintf(buf, "0x%x", (unsigned)proto);
	return buf;
}

const char *
authflags(u_short flags)
{
	static char buf[30];
	buf[0] = '\0';
	if (flags & AUTHFLAG_NOCALLOUT)
		strcat(buf, " callin");
	if (flags & AUTHFLAG_NORECHALLENGE)
		strcat(buf, " norechallenge");
	return buf;
}
