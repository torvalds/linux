/*-
 * Copyright (c) 2008 Sam Leffler, Errno Consulting
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
#include "diag.h"

#include "ah.h"
#include "ah_diagcodes.h"

#include <getopt.h>
#include <errno.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct	ath_diag atd;
int	s;
const char *progname;

/* XXX this should likely be defined somewhere in the HAL */
/* XXX This is a lot larger than the v14 ROM */
#define	MAX_EEPROM_SIZE		16384

uint16_t eep[MAX_EEPROM_SIZE];

static void
usage()
{
	fprintf(stderr, "	%s [-i ifname] -d <dumpfile>\n", progname);
	exit(-1);
}

#define	NUM_PER_LINE	8

static void
do_eeprom_dump(const char *dumpfile, uint16_t *eebuf, int eelen)
{
	FILE *fp;
	int i;

	fp = fopen(dumpfile, "w");
	if (!fp) {
		err(1, "fopen");
	}

	/* eelen is in bytes; eebuf is in 2 byte words */
	for (i = 0; i < eelen / 2; i++) {
		if (i % NUM_PER_LINE == 0)
			fprintf(fp, "%.4x: ", i);
		fprintf(fp, "%.4x%s", (int32_t)(eebuf[i]), i % NUM_PER_LINE == (NUM_PER_LINE - 1) ? "\n" : " ");
	}
	fprintf(fp, "\n");
	fclose(fp);
}

int
main(int argc, char *argv[])
{
	FILE *fd = NULL;
	const char *ifname;
	int c;
	const char *dumpname = NULL;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(1, "socket");
	ifname = getenv("ATH");
	if (!ifname)
		ifname = ATH_DEFAULT;

	progname = argv[0];
	while ((c = getopt(argc, argv, "d:i:t:")) != -1)
		switch (c) {
		case 'd':
			dumpname = optarg;
			break;
		case 'i':
			ifname = optarg;
			break;
		case 't':
			fd = fopen(optarg, "r");
			if (fd == NULL)
				err(-1, "Cannot open %s", optarg);
			break;
		default:
			usage();
			/*NOTREACHED*/
		}
	argc -= optind;
	argv += optind;

	strncpy(atd.ad_name, ifname, sizeof (atd.ad_name));

	/* Read in the entire EEPROM */
	atd.ad_id = HAL_DIAG_EEPROM;
	atd.ad_out_data = (caddr_t) eep;
	atd.ad_out_size = sizeof(eep);
	if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
		err(1, "ioctl: %s", atd.ad_name);

	/* Dump file? Then just write to it */
	if (dumpname != NULL) {
		do_eeprom_dump(dumpname, (uint16_t *) &eep, sizeof(eep));
	}
	return 0;
}

