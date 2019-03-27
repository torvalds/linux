/*-
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
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

#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "ah.h"
#include "ah_internal.h"
#include "ah_eeprom.h"
#include "ah_eeprom_v1.h"
#include "ah_eeprom_v3.h"
#include "ah_eeprom_v14.h"
#include "ar5212/ar5212reg.h"
#define	IS_5112(ah) \
	(((ah)->ah_analog5GhzRev&0xf0) >= AR_RAD5112_SREV_MAJOR \
	 && ((ah)->ah_analog5GhzRev&0xf0) < AR_RAD2316_SREV_MAJOR )
#define	IS_2316(ah) \
	((ah)->ah_macVersion == AR_SREV_2415)
#define	IS_2413(ah) \
	((ah)->ah_macVersion == AR_SREV_2413 || IS_2316(ah))
#define IS_5424(ah) \
	((ah)->ah_macVersion == AR_SREV_5424 || \
	((ah)->ah_macVersion == AR_SREV_5413 && \
	  (ah)->ah_macRev <= AR_SREV_D2PLUS_MS))
#define IS_5413(ah) \
	((ah)->ah_macVersion == AR_SREV_5413 || IS_5424(ah))

#ifndef MAX
#define	MAX(a,b)	((a) > (b) ? (a) : (b))
#endif

static void printPcdacTable(FILE *fd, u_int16_t pcdac[], u_int n);
static void printPowerPerRate(FILE *fd, u_int16_t ratesArray[], u_int n);
static void printRevs(FILE *fd, const HAL_REVS *revs);

static void
usage(const char *progname)
{
	fprintf(stderr, "usage: %s [-v] [-i dev]\n", progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int s, i, verbose = 0, c;
	struct ath_diag atd;
	const char *ifname;
	HAL_REVS revs;
	u_int16_t pcdacTable[MAX(PWR_TABLE_SIZE,PWR_TABLE_SIZE_2413)];
	u_int16_t ratesArray[37];
	u_int nrates, npcdac;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(1, "socket");
	ifname = getenv("ATH");
	if (!ifname)
		ifname = ATH_DEFAULT;
	while ((c = getopt(argc, argv, "i:v")) != -1)
		switch (c) {
		case 'i':
			ifname = optarg;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage(argv[0]);
		}
	strncpy(atd.ad_name, ifname, sizeof (atd.ad_name));

	atd.ad_id = HAL_DIAG_REVS;
	atd.ad_out_data = (caddr_t) &revs;
	atd.ad_out_size = sizeof(revs);
	if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
		err(1, atd.ad_name);

	if (verbose)
		printRevs(stdout, &revs);

	atd.ad_id = HAL_DIAG_TXRATES;
	atd.ad_out_data = (caddr_t) ratesArray;
	atd.ad_out_size = sizeof(ratesArray);
	if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
		err(1, atd.ad_name);
	nrates = sizeof(ratesArray) / sizeof(u_int16_t);

	atd.ad_id = HAL_DIAG_PCDAC;
	atd.ad_out_data = (caddr_t) pcdacTable;
	atd.ad_out_size = sizeof(pcdacTable);
	if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
		err(1, atd.ad_name);
	if (IS_2413(&revs) || IS_5413(&revs))
		npcdac = PWR_TABLE_SIZE_2413;
	else
		npcdac = PWR_TABLE_SIZE;

	printf("PCDAC table:\n");
	printPcdacTable(stdout, pcdacTable, npcdac);

	printf("Power per rate table:\n");
	printPowerPerRate(stdout, ratesArray, nrates);

	return 0;
}

static void
printPcdacTable(FILE *fd, u_int16_t pcdac[], u_int n)
{
	int i, halfRates = n/2;

	for (i = 0; i < halfRates; i += 2)
		fprintf(fd, "[%2u] %04x %04x [%2u] %04x %04x\n",
			i, pcdac[2*i + 1], pcdac[2*i],
			i+1, pcdac[2*(i+1) + 1], pcdac[2*(i+1)]);
}

static void
printPowerPerRate(FILE *fd, u_int16_t ratesArray[], u_int n)
{
	const char *rateString[] = {
		" 6mb OFDM", " 9mb OFDM", "12mb OFDM", "18mb OFDM",
		"24mb OFDM", "36mb OFDM", "48mb OFDM", "54mb OFDM",
		"1L   CCK ", "2L   CCK ", "2S   CCK ", "5.5L CCK ",
		"5.5S CCK ", "11L  CCK ", "11S  CCK ", "XR       "
	};
	int i, halfRates = n/2;

	for (i = 0; i < halfRates; i++)
		fprintf(fd, " %s %3d.%1d dBm | %s %3d.%1d dBm\n", 
			 rateString[i], ratesArray[i]/2,
			 (ratesArray[i] %2) * 5, 
			 rateString[i + halfRates],
			 ratesArray[i + halfRates]/2,
			 (ratesArray[i + halfRates] %2) *5);
}

static void
printRevs(FILE *fd, const HAL_REVS *revs)
{
	const char *rfbackend;

	fprintf(fd, "PCI device id 0x%x subvendor id 0x%x\n",
		revs->ah_devid, revs->ah_subvendorid);
	fprintf(fd, "mac %d.%d phy %d.%d"
		, revs->ah_macVersion, revs->ah_macRev
		, revs->ah_phyRev >> 4, revs->ah_phyRev & 0xf
	);
	rfbackend = IS_5413(revs) ? "5413" :
		    IS_2413(revs) ? "2413" :
		    IS_5112(revs) ? "5112" :
				    "5111";
	if (revs->ah_analog5GhzRev && revs->ah_analog2GhzRev)
		fprintf(fd, " 5ghz radio %d.%d 2ghz radio %d.%d (%s)\n"
			, revs->ah_analog5GhzRev >> 4
			, revs->ah_analog5GhzRev & 0xf
			, revs->ah_analog2GhzRev >> 4
			, revs->ah_analog2GhzRev & 0xf
			, rfbackend
		);
	else
		fprintf(fd, " radio %d.%d (%s)\n"
			, revs->ah_analog5GhzRev >> 4
			, revs->ah_analog5GhzRev & 0xf
			, rfbackend
		);
}
