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
#include "ah_internal.h"
#include "ah_eeprom_v1.h"
#include "ah_eeprom_v3.h"
#include "ah_eeprom_v14.h"

#define	IS_VERS(op, v)		(eeprom.ee_version op (v))

#include <getopt.h>
#include <errno.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef DIR_TEMPLATE
#define	DIR_TEMPLATE	"/usr/local/libdata/athprom"
#endif

struct	ath_diag atd;
int	s;
const char *progname;
union {
	HAL_EEPROM legacy;		/* format v3.x ... v5.x */
	struct ar5416eeprom v14;	/* 11n format v14.x ... */
} eep;
#define	eeprom	eep.legacy
#define	eepromN	eep.v14

static void parseTemplate(FILE *ftemplate, FILE *fd);
static uint16_t eeread(uint16_t);
static void eewrite(uint16_t, uint16_t);

static void
usage()
{
	fprintf(stderr, "usage: %s [-i ifname] [-t pathname] [offset | offset=value]\n", progname);
	exit(-1);
}

static FILE *
opentemplate(const char *dir)
{
	char filename[PATH_MAX];
	FILE *fd;

	/* find the template using the eeprom version */
	snprintf(filename, sizeof(filename), "%s/eeprom-%d.%d",
	    dir, eeprom.ee_version >> 12, eeprom.ee_version & 0xfff);
	fd = fopen(filename, "r");
	if (fd == NULL && errno == ENOENT) {
		/* retry with just the major version */
		snprintf(filename, sizeof(filename), "%s/eeprom-%d",
		    dir, eeprom.ee_version >> 12);
		fd = fopen(filename, "r");
		if (fd != NULL)		/* XXX verbose */
			warnx("Using template file %s", filename);
	}
	return fd;
}

int
main(int argc, char *argv[])
{
	FILE *fd = NULL;
	const char *ifname;
	int c;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(1, "socket");
	ifname = getenv("ATH");
	if (!ifname)
		ifname = ATH_DEFAULT;

	progname = argv[0];
	while ((c = getopt(argc, argv, "i:t:")) != -1)
		switch (c) {
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

	if (argc != 0) {
		for (; argc > 0; argc--, argv++) {
			uint16_t off, val, oval;
			char line[256];
			char *cp;

			cp = strchr(argv[0], '=');
			if (cp != NULL)
				*cp = '\0';
			off = (uint16_t) strtoul(argv[0], NULL, 0);
			if (off == 0 && errno == EINVAL)
				errx(1, "%s: invalid eeprom offset %s",
					progname, argv[0]);
			if (cp == NULL) {
				printf("%04x: %04x\n", off, eeread(off));
			} else {
				val = (uint16_t) strtoul(cp+1, NULL, 0);
				if (val == 0 && errno == EINVAL)
				errx(1, "%s: invalid eeprom value %s",
					progname, cp+1);
				oval = eeread(off);
				printf("Write %04x: %04x = %04x? ",
					off, oval, val);
				fflush(stdout);
				if (fgets(line, sizeof(line), stdin) != NULL &&
				    line[0] == 'y')
					eewrite(off, val);
			}
		}
	} else {
		atd.ad_id = HAL_DIAG_EEPROM;
		atd.ad_out_data = (caddr_t) &eep;
		atd.ad_out_size = sizeof(eep);
		if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
			err(1, "ioctl: %s", atd.ad_name);
		if (fd == NULL) {
			fd = opentemplate(DIR_TEMPLATE);
			if (fd == NULL)
				fd = opentemplate(".");
			if (fd == NULL)
				errx(-1, "Cannot locate template file for "
				    "v%d.%d EEPROM", eeprom.ee_version >> 12,
				    eeprom.ee_version & 0xfff);
		}
		parseTemplate(fd, stdout);
		fclose(fd);
	}
	return 0;
}

static u_int16_t
eeread(u_int16_t off)
{
	u_int16_t eedata;

	atd.ad_id = HAL_DIAG_EEREAD | ATH_DIAG_IN | ATH_DIAG_DYN;
	atd.ad_in_size = sizeof(off);
	atd.ad_in_data = (caddr_t) &off;
	atd.ad_out_size = sizeof(eedata);
	atd.ad_out_data = (caddr_t) &eedata;
	if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
		err(1, "ioctl: %s", atd.ad_name);
	return eedata;
}

static void
eewrite(uint16_t off, uint16_t value)
{
	HAL_DIAG_EEVAL eeval;

	eeval.ee_off = off;
	eeval.ee_data = value;

	atd.ad_id = HAL_DIAG_EEWRITE | ATH_DIAG_IN;
	atd.ad_in_size = sizeof(eeval);
	atd.ad_in_data = (caddr_t) &eeval;
	atd.ad_out_size = 0;
	atd.ad_out_data = NULL;
	if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
		err(1, "ioctl: %s", atd.ad_name);
}

#define	MAXID	128
int	lineno;
int	bol;
int	curmode = -1;
int	curchan;
int	curpdgain;	/* raw pdgain index */
int	curlpdgain;	/* logical pdgain index */
int	curpcdac;
int	curctl;
int	numChannels;
const RAW_DATA_STRUCT_2413 *pRaw;
const TRGT_POWER_INFO *pPowerInfo;
const DATA_PER_CHANNEL *pDataPerChannel;
const EEPROM_POWER_EXPN_5112 *pExpnPower;
int	singleXpd;

static int
token(FILE *fd, char id[], int maxid, const char *what)
{
	int c, i;

	i = 0;
	for (;;) {
		c = getc(fd);
		if (c == EOF)
			return EOF;
		if (!isalnum(c) && c != '_') {
			ungetc(c, fd);
			break;
		}
		if (i == maxid-1) {
			warnx("line %d, %s too long", lineno, what);
			break;
		}
		id[i++] = c;
	}
	id[i] = '\0';
	if (i != 0)
		bol = 0;
	return i;
}

static int
skipto(FILE *fd, const char *what)
{
	char id[MAXID];
	int c;

	for (;;) {
		c = getc(fd);
		if (c == EOF)
			goto bad;
		if (c == '.' && bol) {		/* .directive */
			if (token(fd, id, MAXID, ".directive") == EOF)
				goto bad;
			if (strcasecmp(id, what) == 0)
				break;
			continue;
		}
		if (c == '\\') {		/* escape next character */
			c = getc(fd);
			if (c == EOF)
				goto bad;
		}
		bol = (c == '\n');
		if (bol)
			lineno++;
	}
	return 0;
bad:
	warnx("EOF with no matching .%s", what);
	return EOF;
}

static int
skipws(FILE *fd)
{
	int c, i;

	i = 0;
	while ((c = getc(fd)) != EOF && isblank(c))
		i++;
	if (c != EOF)
		ungetc(c, fd);
	if (i != 0)
		bol = 0;
	return 0;
}

static void
setmode(int mode)
{
	EEPROM_POWER_EXPN_5112 *exp;

	curmode = mode;
	curchan = -1;
	curctl = -1;
	curpdgain = -1;
	curlpdgain = -1;
	curpcdac = -1;
	switch (curmode) {
	case headerInfo11A:
		pPowerInfo = eeprom.ee_trgtPwr_11a;
		pDataPerChannel = eeprom.ee_dataPerChannel11a;
		break;
	case headerInfo11B:
		pPowerInfo = eeprom.ee_trgtPwr_11b;
		pDataPerChannel = eeprom.ee_dataPerChannel11b;
		break;
	case headerInfo11G:
		pPowerInfo = eeprom.ee_trgtPwr_11g;
		pDataPerChannel = eeprom.ee_dataPerChannel11g;
		break;
	}
	if (IS_VERS(<, AR_EEPROM_VER4_0))		/* nothing to do */
		return;
	if (IS_VERS(<, AR_EEPROM_VER5_0)) {
		exp = &eeprom.ee_modePowerArray5112[curmode];
		/* fetch indirect data*/
		atd.ad_id = HAL_DIAG_EEPROM_EXP_11A+curmode;
		atd.ad_out_size = roundup(
			sizeof(u_int16_t) * exp->numChannels, sizeof(u_int32_t))
		    + sizeof(EXPN_DATA_PER_CHANNEL_5112) * exp->numChannels;
		atd.ad_out_data = (caddr_t) malloc(atd.ad_out_size);
		if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
			err(1, "ioctl: %s", atd.ad_name);
		exp->pChannels = (void *) atd.ad_out_data;
		exp->pDataPerChannel = (void *)((char *)atd.ad_out_data +
		   roundup(sizeof(u_int16_t) * exp->numChannels, sizeof(u_int32_t)));
		pExpnPower = exp;
		numChannels = pExpnPower->numChannels;
		if (exp->xpdMask != 0x9) {
			for (singleXpd = 0; singleXpd < NUM_XPD_PER_CHANNEL; singleXpd++)
				if (exp->xpdMask == (1<<singleXpd))
					break;
		} else
			singleXpd = 0;
	} else if (IS_VERS(<, AR_EEPROM_VER14_2)) {
		pRaw = &eeprom.ee_rawDataset2413[curmode];
		numChannels = pRaw->numChannels;
	}
}

int
nextctl(int start)
{
	int i;

	for (i = start; i < eeprom.ee_numCtls && eeprom.ee_ctl[i]; i++) {
		switch (eeprom.ee_ctl[i] & 3) {
		case 0: case 3:
			if (curmode != headerInfo11A)
				continue;
			break;
		case 1:
			if (curmode != headerInfo11B)
				continue;
			break;
		case 2:
			if (curmode != headerInfo11G)
				continue;
			break;
		}
		return i;
	}
	return -1;
}

static void
printAntennaControl(FILE *fd, int ant)
{
	fprintf(fd, "0x%02X", eeprom.ee_antennaControl[ant][curmode]);
}

static void
printEdge(FILE *fd, int edge)
{
	const RD_EDGES_POWER *pRdEdgePwrInfo =
	    &eeprom.ee_rdEdgesPower[curctl*NUM_EDGES];

	if (pRdEdgePwrInfo[edge].rdEdge == 0)
		fprintf(fd, " -- ");
	else
		fprintf(fd, "%04d", pRdEdgePwrInfo[edge].rdEdge);
}

static void
printEdgePower(FILE *fd, int edge)
{
	const RD_EDGES_POWER *pRdEdgePwrInfo =
	    &eeprom.ee_rdEdgesPower[curctl*NUM_EDGES];

	if (pRdEdgePwrInfo[edge].rdEdge == 0)
		fprintf(fd, " -- ");
	else
                fprintf(fd, "%2d.%d",
		    pRdEdgePwrInfo[edge].twice_rdEdgePower / 2,
                    (pRdEdgePwrInfo[edge].twice_rdEdgePower % 2) * 5);
}

static void
printEdgeFlag(FILE *fd, int edge)
{
	const RD_EDGES_POWER *pRdEdgePwrInfo =
	    &eeprom.ee_rdEdgesPower[curctl*NUM_EDGES];

	if (pRdEdgePwrInfo[edge].rdEdge == 0)
		fprintf(fd, "--");
	else
                fprintf(fd, " %1d", pRdEdgePwrInfo[edge].flag);
}

static int16_t
getMaxPowerV5(const RAW_DATA_PER_CHANNEL_2413 *data)
{
	uint32_t i;
	uint16_t numVpd;

	for (i = 0; i < MAX_NUM_PDGAINS_PER_CHANNEL; i++) {
		numVpd = data->pDataPerPDGain[i].numVpd;
		if (numVpd > 0)
			return data->pDataPerPDGain[i].pwr_t4[numVpd-1];
	}
	return 0;
}

static void
printQuarterDbmPower(FILE *fd, int16_t power25dBm)
{
	fprintf(fd, "%2d.%02d", power25dBm / 4, (power25dBm % 4) * 25);
}

static void
printHalfDbmPower(FILE *fd, int16_t power5dBm)
{
	fprintf(fd, "%2d.%d", power5dBm / 2, (power5dBm % 2) * 5);
}

static void
printVpd(FILE *fd, int vpd)
{
	fprintf(fd, "[%3d]", vpd);
}

static void
printPcdacValue(FILE *fd, int v)
{
	fprintf(fd, "%2d.%02d", v / EEP_SCALE, v % EEP_SCALE);
}

static void
undef(const char *what)
{
	warnx("%s undefined for version %d.%d format EEPROM", what,
	    eeprom.ee_version >> 12, eeprom.ee_version & 0xfff);
}

static int
pdgain(int lpdgain)
{
	uint32_t mask;
	int i, l = lpdgain;

	if (IS_VERS(<, AR_EEPROM_VER5_0))
		mask = pExpnPower->xpdMask;
	else
		mask = pRaw->xpd_mask;
	for (i = 0; mask != 0; mask >>= 1, i++)
		if ((mask & 1) && l-- == 0)
			return i;
	warnx("can't find logical pdgain %d", lpdgain);
	return -1;
}

#define	COUNTRY_ERD_FLAG        0x8000
#define WORLDWIDE_ROAMING_FLAG  0x4000

void
eevar(FILE *fd, const char *var)
{
#define	streq(a,b)	(strcasecmp(a,b) == 0)
#define	strneq(a,b,n)	(strncasecmp(a,b,n) == 0)
	if (streq(var, "mode")) {
		fprintf(fd, "%s",
		    curmode == headerInfo11A ? "11a" :
		    curmode == headerInfo11B ? "11b" :
		    curmode == headerInfo11G ? "11g" : "???");
	} else if (streq(var, "version")) {
		fprintf(fd, "%04x", eeprom.ee_version);
	} else if (streq(var, "V_major")) {
		fprintf(fd, "%2d", eeprom.ee_version >> 12);
	} else if (streq(var, "V_minor")) {
		fprintf(fd, "%2d", eeprom.ee_version & 0xfff);
	} else if (streq(var, "earStart")) {
		fprintf(fd, "%03x", eeprom.ee_earStart);
	} else if (streq(var, "tpStart")) {
		fprintf(fd, "%03x", eeprom.ee_targetPowersStart);
	} else if (streq(var, "eepMap")) {
		fprintf(fd, "%3d", eeprom.ee_eepMap);
	} else if (streq(var, "exist32KHzCrystal")) {
		fprintf(fd, "%3d", eeprom.ee_exist32kHzCrystal);
	} else if (streq(var, "eepMap2PowerCalStart")) {
		fprintf(fd , "%3d", eeprom.ee_eepMap2PowerCalStart);
	} else if (streq(var, "Amode")) {
		fprintf(fd , "%1d", eeprom.ee_Amode);
	} else if (streq(var, "Bmode")) {
		fprintf(fd , "%1d", eeprom.ee_Bmode);
	} else if (streq(var, "Gmode")) {
		fprintf(fd , "%1d", eeprom.ee_Gmode);
	} else if (streq(var, "regdomain")) {
		if ((eeprom.ee_regdomain & COUNTRY_ERD_FLAG) == 0)
			fprintf(fd, "%03X ", eeprom.ee_regdomain >> 15);
		else
			fprintf(fd, "%-3dC", eeprom.ee_regdomain & 0xfff);
	} else if (streq(var, "turbo2Disable")) {
		fprintf(fd, "%1d", eeprom.ee_turbo2Disable);
	} else if (streq(var, "turbo5Disable")) {
		fprintf(fd, "%1d", eeprom.ee_turbo5Disable);
	} else if (streq(var, "rfKill")) {
		fprintf(fd, "%1d", eeprom.ee_rfKill);
	} else if (streq(var, "disableXr5")) {
		fprintf(fd, "%1d", eeprom.ee_disableXr5);
	} else if (streq(var, "disableXr2")) {
		fprintf(fd, "%1d", eeprom.ee_disableXr2);
	} else if (streq(var, "turbo2WMaxPower5")) {
		fprintf(fd, "%2d", eeprom.ee_turbo2WMaxPower5);
	} else if (streq(var, "cckOfdmDelta")) {
		fprintf(fd, "%2d", eeprom.ee_cckOfdmPwrDelta);
	} else if (streq(var, "gainI")) {
		fprintf(fd, "%2d", eeprom.ee_gainI[curmode]);
	} else if (streq(var, "WWR")) {
		fprintf(fd, "%1x",
		    (eeprom.ee_regdomain & WORLDWIDE_ROAMING_FLAG) != 0);
	} else if (streq(var, "falseDetectBackoff")) {
		fprintf(fd, "0x%02x", eeprom.ee_falseDetectBackoff[curmode]);
	} else if (streq(var, "deviceType")) {
		fprintf(fd, "%1x", eeprom.ee_deviceType);
	} else if (streq(var, "switchSettling")) {
		if (IS_VERS(<, AR_EEPROM_VER14_2))
			fprintf(fd, "0x%02x", eeprom.ee_switchSettling[curmode]);
		else
			fprintf(fd, "%3d", eepromN.modalHeader[curmode].switchSettling);
	} else if (streq(var, "adcDesiredSize")) {
		if (IS_VERS(<, AR_EEPROM_VER14_2))
			fprintf(fd, "%2d", eeprom.ee_adcDesiredSize[curmode]);
		else
			fprintf(fd, "%3d", eepromN.modalHeader[curmode].adcDesiredSize);
	} else if (streq(var, "xlnaGain")) {
		fprintf(fd, "0x%02x", eeprom.ee_xlnaGain[curmode]);
	} else if (streq(var, "txEndToXLNAOn")) {
		fprintf(fd, "0x%02x", eeprom.ee_txEndToXLNAOn[curmode]);
	} else if (streq(var, "thresh62")) {
		if (IS_VERS(<, AR_EEPROM_VER14_2))
			fprintf(fd, "0x%02x", eeprom.ee_thresh62[curmode]);
		else
			fprintf(fd, "%3d", eepromN.modalHeader[curmode].thresh62);
	} else if (streq(var, "txEndToRxOn")) {
		fprintf(fd, "%3d", eepromN.modalHeader[curmode].txEndToRxOn);
	} else if (streq(var, "txEndToXPAOff")) {
		if (IS_VERS(<, AR_EEPROM_VER14_2))
			fprintf(fd, "0x%02x", eeprom.ee_txEndToXPAOff[curmode]);
		else
			fprintf(fd, "%3d", eepromN.modalHeader[curmode].txEndToXpaOff);
	} else if (streq(var, "txFrameToXPAOn")) {
		if (IS_VERS(<, AR_EEPROM_VER14_2))
			fprintf(fd, "0x%02x", eeprom.ee_txFrameToXPAOn[curmode]);
		else
			fprintf(fd, "%3d", eepromN.modalHeader[curmode].txEndToRxOn);
	} else if (streq(var, "pgaDesiredSize")) {
		if (IS_VERS(<, AR_EEPROM_VER14_2))
			fprintf(fd, "%2d", eeprom.ee_pgaDesiredSize[curmode]);
		else
			fprintf(fd, "%3d", eepromN.modalHeader[curmode].pgaDesiredSize);
	} else if (streq(var, "noiseFloorThresh")) {
		fprintf(fd, "%3d", eeprom.ee_noiseFloorThresh[curmode]);
	} else if (strneq(var, "noiseFloorThreshCh", 18)) {
		fprintf(fd, "%3d", eepromN.modalHeader[curmode].noiseFloorThreshCh[atoi(var+18)]);
	} else if (strneq(var, "xlnaGainCh", 10)) {
		fprintf(fd, "%3d", eepromN.modalHeader[curmode].xlnaGainCh[atoi(var+10)]);
	} else if (streq(var, "xgain")) {
		fprintf(fd, "0x%02x", eeprom.ee_xgain[curmode]);
	} else if (streq(var, "xpd")) {
		if (IS_VERS(<, AR_EEPROM_VER14_2))
			fprintf(fd, "%1d", eeprom.ee_xpd[curmode]);
		else
			fprintf(fd, "%3d", eepromN.modalHeader[curmode].xpd);
	} else if (streq(var, "txrxAtten")) {
		fprintf(fd, "0x%02x", eeprom.ee_txrxAtten[curmode]);
	} else if (streq(var, "capField")) {
		fprintf(fd, "0x%04X", eeprom.ee_capField);
	} else if (streq(var, "txrxAttenTurbo")) {
		fprintf(fd, "0x%02x",
		    eeprom.ee_txrxAtten[curmode != headerInfo11A]);
	} else if (streq(var, "switchSettlingTurbo")) {
		fprintf(fd, "0x%02X",
		    eeprom.ee_switchSettlingTurbo[curmode != headerInfo11A]);
	} else if (streq(var, "adcDesiredSizeTurbo")) {
		fprintf(fd, "%2d",
		    eeprom.ee_adcDesiredSizeTurbo[curmode != headerInfo11A]);
	} else if (streq(var, "pgaDesiredSizeTurbo")) {
		fprintf(fd, "%2d",
		    eeprom.ee_pgaDesiredSizeTurbo[curmode != headerInfo11A]);
	} else if (streq(var, "rxtxMarginTurbo")) {
		fprintf(fd, "0x%02x",
		    eeprom.ee_rxtxMarginTurbo[curmode != headerInfo11A]);
	} else if (strneq(var, "antennaControl", 14)) {
		printAntennaControl(fd, atoi(var+14));
	} else if (strneq(var, "antCtrlChain", 12)) {
		fprintf(fd, "0x%08X",
		    eepromN.modalHeader[curmode].antCtrlChain[atoi(var+12)]);
	} else if (strneq(var, "antGainCh", 9)) {
		fprintf(fd, "%3d",
		    eepromN.modalHeader[curmode].antennaGainCh[atoi(var+9)]);
	} else if (strneq(var, "txRxAttenCh", 11)) {
		fprintf(fd, "%3d",
		    eepromN.modalHeader[curmode].txRxAttenCh[atoi(var+11)]);
	} else if (strneq(var, "rxTxMarginCh", 12)) {
		fprintf(fd, "%3d",
		    eepromN.modalHeader[curmode].rxTxMarginCh[atoi(var+12)]);
	} else if (streq(var, "xpdGain")) {
		fprintf(fd, "%3d", eepromN.modalHeader[curmode].xpdGain);
	} else if (strneq(var, "iqCalICh", 8)) {
		fprintf(fd, "%3d",
		    eepromN.modalHeader[curmode].iqCalICh[atoi(var+8)]);
	} else if (strneq(var, "iqCalQCh", 8)) {
		fprintf(fd, "%3d",
		    eepromN.modalHeader[curmode].iqCalQCh[atoi(var+8)]);
	} else if (streq(var, "pdGainOverlap")) {
		printHalfDbmPower(fd, eepromN.modalHeader[curmode].pdGainOverlap);
	} else if (streq(var, "ob1")) {
		fprintf(fd, "%1d", eeprom.ee_ob1);
	} else if (streq(var, "ob2")) {
		fprintf(fd, "%1d", eeprom.ee_ob2);
	} else if (streq(var, "ob3")) {
		fprintf(fd, "%1d", eeprom.ee_ob3);
	} else if (streq(var, "ob4")) {
		fprintf(fd, "%1d", eeprom.ee_ob4);
	} else if (streq(var, "db1")) {
		fprintf(fd, "%1d", eeprom.ee_db1);
	} else if (streq(var, "db2")) {
		fprintf(fd, "%1d", eeprom.ee_db2);
	} else if (streq(var, "db3")) {
		fprintf(fd, "%1d", eeprom.ee_db3);
	} else if (streq(var, "db4")) {
		fprintf(fd, "%1d", eeprom.ee_db4);
	} else if (streq(var, "obFor24")) {
                fprintf(fd, "%1d", eeprom.ee_obFor24);
	} else if (streq(var, "ob2GHz0")) {
                fprintf(fd, "%1d", eeprom.ee_ob2GHz[0]);
	} else if (streq(var, "dbFor24")) {
                fprintf(fd, "%1d", eeprom.ee_dbFor24);
	} else if (streq(var, "db2GHz0")) {
                fprintf(fd, "%1d", eeprom.ee_db2GHz[0]);
	} else if (streq(var, "obFor24g")) {
                fprintf(fd, "%1d", eeprom.ee_obFor24g);
	} else if (streq(var, "ob2GHz1")) {
                fprintf(fd, "%1d", eeprom.ee_ob2GHz[1]);
	} else if (streq(var, "dbFor24g")) {
                fprintf(fd, "%1d", eeprom.ee_dbFor24g);
	} else if (streq(var, "db2GHz1")) {
                fprintf(fd, "%1d", eeprom.ee_db2GHz[1]);
	} else if (streq(var, "ob")) {
		fprintf(fd, "%3d", eepromN.modalHeader[curmode].ob);
	} else if (streq(var, "db")) {
		fprintf(fd, "%3d", eepromN.modalHeader[curmode].db);
	} else if (streq(var, "xpaBiasLvl")) {
		fprintf(fd, "%3d", eepromN.modalHeader[curmode].xpaBiasLvl);
	} else if (streq(var, "pwrDecreaseFor2Chain")) {
		printHalfDbmPower(fd, eepromN.modalHeader[curmode].pwrDecreaseFor2Chain);
	} else if (streq(var, "pwrDecreaseFor3Chain")) {
		printHalfDbmPower(fd, eepromN.modalHeader[curmode].pwrDecreaseFor3Chain);
	} else if (streq(var, "txFrameToDataStart")) {
		fprintf(fd, "%3d", eepromN.modalHeader[curmode].txFrameToDataStart);
	} else if (streq(var, "txFrameToPaOn")) {
		fprintf(fd, "%3d", eepromN.modalHeader[curmode].txFrameToPaOn);
	} else if (streq(var, "ht40PowerIncForPdadc")) {
		fprintf(fd, "%3d", eepromN.modalHeader[curmode].ht40PowerIncForPdadc);
	} else if (streq(var, "checksum")) {
                fprintf(fd, "0x%04X", eepromN.baseEepHeader.checksum);
	} else if (streq(var, "length")) {
                fprintf(fd, "0x%04X", eepromN.baseEepHeader.length);
	} else if (streq(var, "regDmn0")) {
                fprintf(fd, "0x%04X", eepromN.baseEepHeader.regDmn[0]);
	} else if (streq(var, "regDmn1")) {
                fprintf(fd, "0x%04X", eepromN.baseEepHeader.regDmn[1]);
	} else if (streq(var, "txMask")) {
                fprintf(fd, "0x%04X", eepromN.baseEepHeader.txMask);
	} else if (streq(var, "rxMask")) {
                fprintf(fd, "0x%04X", eepromN.baseEepHeader.rxMask);
	} else if (streq(var, "rfSilent")) {
                fprintf(fd, "0x%04X", eepromN.baseEepHeader.rfSilent);
	} else if (streq(var, "btOptions")) {
                fprintf(fd, "0x%04X", eepromN.baseEepHeader.blueToothOptions);
	} else if (streq(var, "deviceCap")) {
                fprintf(fd, "0x%04X", eepromN.baseEepHeader.deviceCap);
	} else if (strneq(var, "macaddr", 7)) {
                fprintf(fd, "%02X",
		    eepromN.baseEepHeader.macAddr[atoi(var+7)]);
	} else if (streq(var, "opCapFlags")) {
                fprintf(fd, "0x%02X", eepromN.baseEepHeader.opCapFlags);
	} else if (streq(var, "eepMisc")) {
                fprintf(fd, "0x%02X", eepromN.baseEepHeader.eepMisc);
	} else if (strneq(var, "binBuildNumber", 14)) {
                fprintf(fd, "%3d",
		    (eepromN.baseEepHeader.binBuildNumber >> (8*atoi(var+14)))
		    & 0xff);
	} else if (strneq(var, "custData", 8)) {
		fprintf(fd, "%2.2X", eepromN.custData[atoi(var+8)]);
	} else if (streq(var, "xpd_mask")) {
		if (IS_VERS(<, AR_EEPROM_VER5_0))
			fprintf(fd, "0x%02x", pExpnPower->xpdMask);
		else
			fprintf(fd, "0x%02x", pRaw->xpd_mask);
	} else if (streq(var, "numChannels")) {
		if (IS_VERS(<, AR_EEPROM_VER5_0))
			fprintf(fd, "%2d", pExpnPower->numChannels);
		else
			fprintf(fd, "%2d", pRaw->numChannels);
	} else if (streq(var, "freq")) {
		if (IS_VERS(<, AR_EEPROM_VER5_0))
			fprintf(fd, "%4d", pExpnPower->pChannels[curchan]);
		else
			fprintf(fd, "%4d", pRaw->pChannels[curchan]);
	} else if (streq(var, "maxpow")) {
		int16_t maxPower_t4;
		if (IS_VERS(<, AR_EEPROM_VER5_0)) {
			maxPower_t4 = pExpnPower->pDataPerChannel[curchan].maxPower_t4;
		} else {
			maxPower_t4 = pRaw->pDataPerChannel[curchan].maxPower_t4;
			if (maxPower_t4 == 0)
				maxPower_t4 = getMaxPowerV5(&pRaw->pDataPerChannel[curchan]);
		}
		printQuarterDbmPower(fd, maxPower_t4);
	} else if (streq(var, "pd_gain")) {
		fprintf(fd, "%4d", pRaw->pDataPerChannel[curchan].
		    pDataPerPDGain[curpdgain].pd_gain);
	} else if (strneq(var, "maxpwr", 6)) {
		int vpd = atoi(var+6);
		if (vpd < pRaw->pDataPerChannel[curchan].pDataPerPDGain[curpdgain].numVpd)
			printQuarterDbmPower(fd, pRaw->pDataPerChannel[curchan].
			    pDataPerPDGain[curpdgain].pwr_t4[vpd]);
		else
			fprintf(fd, "     ");
	} else if (strneq(var, "pwr_t4_", 7)) {
		printQuarterDbmPower(fd, pExpnPower->pDataPerChannel[curchan].
		    pDataPerXPD[singleXpd].pwr_t4[atoi(var+7)]);
	} else if (strneq(var, "Vpd", 3)) {
		int vpd = atoi(var+3);
		if (vpd < pRaw->pDataPerChannel[curchan].pDataPerPDGain[curpdgain].numVpd)
			printVpd(fd, pRaw->pDataPerChannel[curchan].
			    pDataPerPDGain[curpdgain].Vpd[vpd]);
		else
			fprintf(fd, "     ");
	} else if (streq(var, "CTL")) {
		fprintf(fd, "0x%2x", eeprom.ee_ctl[curctl] & 0xff);
	} else if (streq(var, "ctlType")) {
		static const char *ctlType[16] = {
		    "11a base", "11b", "11g", "11a TURBO", "108g",
		    "2GHT20", "5GHT20", "2GHT40", "5GHT40",
		    "0x9", "0xa", "0xb", "0xc", "0xd", "0xe", "0xf",
		};
		fprintf(fd, "%8s", ctlType[eeprom.ee_ctl[curctl] & CTL_MODE_M]);
	} else if (streq(var, "ctlRD")) {
		static const char *ctlRD[8] = {
		    "0x00", " FCC", "0x20", "ETSI",
		    " MKK", "0x50", "0x60", "0x70"
		};
		fprintf(fd, "%s", ctlRD[(eeprom.ee_ctl[curctl] >> 4) & 7]);
	} else if (strneq(var, "rdEdgePower", 11)) {
		printEdgePower(fd, atoi(var+11));
	} else if (strneq(var, "rdEdgeFlag", 10)) {
		printEdgeFlag(fd, atoi(var+10));
	} else if (strneq(var, "rdEdge", 6)) {
		printEdge(fd, atoi(var+6));
	} else if (strneq(var, "testChannel", 11)) {
		fprintf(fd, "%4d", pPowerInfo[atoi(var+11)].testChannel);
	} else if (strneq(var, "pwr6_24_", 8)) {
		printHalfDbmPower(fd, pPowerInfo[atoi(var+8)].twicePwr6_24);
	} else if (strneq(var, "pwr36_", 6)) {
		printHalfDbmPower(fd, pPowerInfo[atoi(var+6)].twicePwr36);
	} else if (strneq(var, "pwr48_", 6)) {
		printHalfDbmPower(fd, pPowerInfo[atoi(var+6)].twicePwr48);
	} else if (strneq(var, "pwr54_", 6)) {
		printHalfDbmPower(fd, pPowerInfo[atoi(var+6)].twicePwr54);
	} else if (strneq(var, "channelValue", 12)) {
		fprintf(fd, "%4d", pDataPerChannel[atoi(var+12)].channelValue);
	} else if (strneq(var, "pcdacMin", 8)) {
		fprintf(fd, "%02d", pDataPerChannel[atoi(var+8)].pcdacMin);
	} else if (strneq(var, "pcdacMax", 8)) {
		fprintf(fd, "%02d", pDataPerChannel[atoi(var+8)].pcdacMax);
	} else if (strneq(var, "pcdac", 5)) {
		if (IS_VERS(<, AR_EEPROM_VER4_0)) {
			fprintf(fd, "%02d", pDataPerChannel[atoi(var+5)].
			    PcdacValues[curpcdac]);
		} else if (IS_VERS(<, AR_EEPROM_VER5_0)) {
			fprintf(fd, "%02d",
			    pExpnPower->pDataPerChannel[curchan].
				pDataPerXPD[singleXpd].pcdac[atoi(var+5)]);
		} else
			undef("pcdac");
	} else if (strneq(var, "pwrValue", 8)) {
		printPcdacValue(fd,
		    pDataPerChannel[atoi(var+8)].PwrValues[curpcdac]);
	} else if (streq(var, "singleXpd")) {
		fprintf(fd, "%2d", singleXpd);
	} else
		warnx("line %u, unknown EEPROM variable \"%s\"", lineno, var);
#undef strneq
#undef streq
}

static void
ifmode(FILE *ftemplate, const char *mode)
{
	if (strcasecmp(mode, "11a") == 0) {
		if (IS_VERS(<, AR_EEPROM_VER14_2)) {
			if (eeprom.ee_Amode)
				setmode(headerInfo11A);
			else
				skipto(ftemplate, "endmode");
			return;
		}
		if (IS_VERS(>=, AR_EEPROM_VER14_2)) {
			if (eepromN.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11A)
				setmode(headerInfo11A);
			else
				skipto(ftemplate, "endmode");
			return;
		}
	} else if (strcasecmp(mode, "11g") == 0) {
		if (IS_VERS(<, AR_EEPROM_VER14_2)) {
			if (eeprom.ee_Gmode)
				setmode(headerInfo11G);
			else
				skipto(ftemplate, "endmode");
			return;
		}
		if (IS_VERS(>=, AR_EEPROM_VER14_2)) {
			if (eepromN.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11G)
				setmode(headerInfo11B);		/* NB: 2.4GHz */
			else
				skipto(ftemplate, "endmode");
			return;
		}
	} else if (strcasecmp(mode, "11b") == 0) {
		if (IS_VERS(<, AR_EEPROM_VER14_2)) {
			if (eeprom.ee_Bmode)
				setmode(headerInfo11B);
			else
				skipto(ftemplate, "endmode");
			return;
		}
	}
	warnx("line %d, unknown/unexpected mode \"%s\" ignored",
	    lineno, mode);
	skipto(ftemplate, "endmode");
}

static void
parseTemplate(FILE *ftemplate, FILE *fd)
{
	int c, i;
	char id[MAXID];
	long forchan, forpdgain, forctl, forpcdac;

	lineno = 1;
	bol = 1;
	while ((c = getc(ftemplate)) != EOF) {
		if (c == '#') {			/* comment */
	skiptoeol:
			while ((c = getc(ftemplate)) != EOF && c != '\n')
				;
			if (c == EOF)
				return;
			lineno++;
			bol = 1;
			continue;
		}
		if (c == '.' && bol) {		/* .directive */
			if (token(ftemplate, id, MAXID, ".directive") == EOF)
				return;
			/* process directive */
			if (strcasecmp(id, "ifmode") == 0) {
				skipws(ftemplate);
				if (token(ftemplate, id, MAXID, "id") == EOF)
					return;
				ifmode(ftemplate, id);
			} else if (strcasecmp(id, "endmode") == 0) {
				/* XXX free malloc'd indirect data */
				curmode = -1;	/* NB: undefined */
			} else if (strcasecmp(id, "forchan") == 0) {
				forchan = ftell(ftemplate) - sizeof("forchan");
				if (curchan == -1)
					curchan = 0;
			} else if (strcasecmp(id, "endforchan") == 0) {
				if (++curchan < numChannels)
					fseek(ftemplate, forchan, SEEK_SET);
				else
					curchan = -1;
			} else if (strcasecmp(id, "ifpdgain") == 0) {
				skipws(ftemplate);
				if (token(ftemplate, id, MAXID, "pdgain") == EOF)
					return;
				curlpdgain = strtoul(id, NULL, 0);
				if (curlpdgain >= pRaw->pDataPerChannel[curchan].numPdGains) {
					skipto(ftemplate, "endpdgain");
					curlpdgain = -1;
				} else
					curpdgain = pdgain(curlpdgain);
			} else if (strcasecmp(id, "endpdgain") == 0) {
				curlpdgain = curpdgain = -1;
			} else if (strcasecmp(id, "forpdgain") == 0) {
				forpdgain = ftell(ftemplate) - sizeof("forpdgain");
				if (curlpdgain == -1) {
					skipws(ftemplate);
					if (token(ftemplate, id, MAXID, "pdgain") == EOF)
						return;
					curlpdgain = strtoul(id, NULL, 0);
					if (curlpdgain >= pRaw->pDataPerChannel[curchan].numPdGains) {
						skipto(ftemplate, "endforpdgain");
						curlpdgain = -1;
					} else
						curpdgain = pdgain(curlpdgain);
				}
			} else if (strcasecmp(id, "endforpdgain") == 0) {
				if (++curpdgain < pRaw->pDataPerChannel[curchan].numPdGains)
					fseek(ftemplate, forpdgain, SEEK_SET);
				else
					curpdgain = -1;
			} else if (strcasecmp(id, "forpcdac") == 0) {
				forpcdac = ftell(ftemplate) - sizeof("forpcdac");
				if (curpcdac == -1)
					curpcdac = 0;
			} else if (strcasecmp(id, "endforpcdac") == 0) {
				if (++curpcdac < pDataPerChannel[0].numPcdacValues)
					fseek(ftemplate, forpcdac, SEEK_SET);
				else
					curpcdac = -1;
			} else if (strcasecmp(id, "forctl") == 0) {
				forctl = ftell(ftemplate) - sizeof("forchan");
				if (curctl == -1)
					curctl = nextctl(0);
			} else if (strcasecmp(id, "endforctl") == 0) {
				curctl = nextctl(curctl+1);
				if (curctl != -1)
					fseek(ftemplate, forctl, SEEK_SET);
			} else {
				warnx("line %d, unknown directive %s ignored",
				    lineno, id);
			}
			goto skiptoeol;
		}
		if (c == '$') {			/* $variable reference */
			if (token(ftemplate, id, MAXID, "$var") == EOF)
				return;
			/* XXX not valid if variable depends on curmode */
			eevar(fd, id);
			continue;
		}
		if (c == '\\') {		/* escape next character */
			c = getc(ftemplate);
			if (c == EOF)
				return;
		}
		fputc(c, fd);
		bol = (c == '\n');
		if (bol)
			lineno++;
	}
}
