/*
 * Copyright (c) 2013 Adrian Chadd <adrian@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include "diag.h"

#include "ah.h"
#include "ah_internal.h"

#include <getopt.h>
#include <errno.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

struct spectralhandler {
	struct		ath_diag atd;
	int		s;
	struct ifreq	ifr;
	int		ah_devid;
};

int
spectral_opendev(struct spectralhandler *spectral, const char *devid)
{
	HAL_REVS revs;

	spectral->s = socket(AF_INET, SOCK_DGRAM, 0);
	if (spectral->s < 0) {
		warn("socket");
		return 0;
	}

	strncpy(spectral->atd.ad_name, devid, sizeof (spectral->atd.ad_name));

	/* Get the hardware revision, just to verify things are working */
	spectral->atd.ad_id = HAL_DIAG_REVS;
	spectral->atd.ad_out_data = (caddr_t) &revs;
	spectral->atd.ad_out_size = sizeof(revs);
	if (ioctl(spectral->s, SIOCGATHDIAG, &spectral->atd) < 0) {
		warn("%s", spectral->atd.ad_name);
		return 0;
	}
	spectral->ah_devid = revs.ah_devid;
	return 1;
}

void
spectral_closedev(struct spectralhandler *spectral)
{
	close(spectral->s);
	spectral->s = -1;
}

void
spectralset(struct spectralhandler *spectral, int op, u_int32_t param)
{
	HAL_SPECTRAL_PARAM pe;

	pe.ss_fft_period = HAL_SPECTRAL_PARAM_NOVAL;
	pe.ss_period = HAL_SPECTRAL_PARAM_NOVAL;
	pe.ss_count = HAL_SPECTRAL_PARAM_NOVAL;
	pe.ss_short_report = HAL_SPECTRAL_PARAM_NOVAL;
	pe.ss_spectral_pri = HAL_SPECTRAL_PARAM_NOVAL;
	pe.ss_fft_period = HAL_SPECTRAL_PARAM_NOVAL;
	pe.ss_enabled = HAL_SPECTRAL_PARAM_NOVAL;
	pe.ss_active = HAL_SPECTRAL_PARAM_NOVAL;

	switch (op) {
	case SPECTRAL_PARAM_FFT_PERIOD:
		pe.ss_fft_period = param;
		break;
	case SPECTRAL_PARAM_SS_PERIOD:
		pe.ss_period = param;
		break;
	case SPECTRAL_PARAM_SS_COUNT:
		pe.ss_count = param;
		break;
	case SPECTRAL_PARAM_SS_SHORT_RPT:
		pe.ss_short_report = param;
		break;
	case SPECTRAL_PARAM_SS_SPECTRAL_PRI:
		pe.ss_spectral_pri = param;
		break;
	}

	spectral->atd.ad_id = SPECTRAL_CONTROL_SET_PARAMS | ATH_DIAG_IN;
	spectral->atd.ad_out_data = NULL;
	spectral->atd.ad_out_size = 0;
	spectral->atd.ad_in_data = (caddr_t) &pe;
	spectral->atd.ad_in_size = sizeof(HAL_SPECTRAL_PARAM);
	if (ioctl(spectral->s, SIOCGATHSPECTRAL, &spectral->atd) < 0)
		err(1, "%s", spectral->atd.ad_name);
}

static void
spectral_get(struct spectralhandler *spectral)
{
	HAL_SPECTRAL_PARAM pe;

	spectral->atd.ad_id = SPECTRAL_CONTROL_GET_PARAMS | ATH_DIAG_DYN;
	memset(&pe, 0, sizeof(pe));

	spectral->atd.ad_in_data = NULL;
	spectral->atd.ad_in_size = 0;
	spectral->atd.ad_out_data = (caddr_t) &pe;
	spectral->atd.ad_out_size = sizeof(pe);

	if (ioctl(spectral->s, SIOCGATHSPECTRAL, &spectral->atd) < 0)
		err(1, "%s", spectral->atd.ad_name);

	printf("Spectral parameters (raw):\n");
	printf("   ss_enabled: %d\n", pe.ss_enabled);
	printf("   ss_active: %d\n", pe.ss_active);
	printf("   ss_count: %d\n", pe.ss_count);
	printf("   ss_fft_period: %d\n", pe.ss_fft_period);
	printf("   ss_period: %d\n", pe.ss_period);
	printf("   ss_short_report: %d\n", pe.ss_short_report);
	printf("   ss_spectral_pri: %d\n", pe.ss_spectral_pri);
	printf("   radar_bin_thresh_sel: %d\n", pe.radar_bin_thresh_sel);
}

static void
spectral_start(struct spectralhandler *spectral)
{
	HAL_SPECTRAL_PARAM pe;

	spectral->atd.ad_id = SPECTRAL_CONTROL_START | ATH_DIAG_DYN;
	memset(&pe, 0, sizeof(pe));

	/*
	 * XXX don't need these, but need to eliminate the ATH_DIAG_DYN flag
	 * and debug
	 */
	spectral->atd.ad_in_data = NULL;
	spectral->atd.ad_in_size = 0;
	spectral->atd.ad_out_data = (caddr_t) &pe;
	spectral->atd.ad_out_size = sizeof(pe);

	if (ioctl(spectral->s, SIOCGATHSPECTRAL, &spectral->atd) < 0)
		err(1, "%s", spectral->atd.ad_name);
}

static void
spectral_stop(struct spectralhandler *spectral)
{
	HAL_SPECTRAL_PARAM pe;

	spectral->atd.ad_id = SPECTRAL_CONTROL_STOP | ATH_DIAG_DYN;
	memset(&pe, 0, sizeof(pe));

	/*
	 * XXX don't need these, but need to eliminate the ATH_DIAG_DYN flag
	 * and debug
	 */
	spectral->atd.ad_in_data = NULL;
	spectral->atd.ad_in_size = 0;
	spectral->atd.ad_out_data = (caddr_t) &pe;
	spectral->atd.ad_out_size = sizeof(pe);

	if (ioctl(spectral->s, SIOCGATHSPECTRAL, &spectral->atd) < 0)
		err(1, "%s", spectral->atd.ad_name);
}

static void
spectral_enable_at_reset(struct spectralhandler *spectral, int val)
{
	int v = val;

	spectral->atd.ad_id = SPECTRAL_CONTROL_ENABLE_AT_RESET
	    | ATH_DIAG_IN;

	/*
	 * XXX don't need these, but need to eliminate the ATH_DIAG_DYN flag
	 * and debug
	 */
	spectral->atd.ad_out_data = NULL;
	spectral->atd.ad_out_size = 0;
	spectral->atd.ad_in_data = (caddr_t) &v;
	spectral->atd.ad_in_size = sizeof(v);

	printf("%s: val=%d\n", __func__, v);

	if (ioctl(spectral->s, SIOCGATHSPECTRAL, &spectral->atd) < 0)
		err(1, "%s", spectral->atd.ad_name);
}

static int
spectral_set_param(struct spectralhandler *spectral, const char *param,
    const char *val)
{
	int v;

	v = atoi(val);

	if (strcmp(param, "ss_short_report") == 0) {
		spectralset(spectral, SPECTRAL_PARAM_SS_SHORT_RPT, v);
	} else if (strcmp(param, "ss_fft_period") == 0) {
		spectralset(spectral, SPECTRAL_PARAM_FFT_PERIOD, v);
	} else if (strcmp(param, "ss_period") == 0) {
		spectralset(spectral, SPECTRAL_PARAM_SS_PERIOD, v);
	} else if (strcmp(param, "ss_count") == 0) {
		spectralset(spectral, SPECTRAL_PARAM_SS_COUNT, v);
	} else if (strcmp(param, "ss_spectral_pri") == 0) {
		spectralset(spectral, SPECTRAL_PARAM_SS_SPECTRAL_PRI, v);
	} else {
		return (0);
	}

#if 0
	if (strcmp(param, "enabled") == 0) {
		spectralset(spectral, DFS_PARAM_ENABLE, v);
	} else if (strcmp(param, "firpwr") == 0) {
		spectralset(spectral, DFS_PARAM_FIRPWR, v);
	} else if (strcmp(param, "rrssi") == 0) {
		spectralset(spectral, DFS_PARAM_RRSSI, v);
	} else if (strcmp(param, "height") == 0) {
		spectralset(spectral, DFS_PARAM_HEIGHT, v);
	} else if (strcmp(param, "prssi") == 0) {
		spectralset(spectral, DFS_PARAM_PRSSI, v);
	} else if (strcmp(param, "inband") == 0) {
		spectralset(spectral, DFS_PARAM_INBAND, v);
	} else if (strcmp(param, "relpwr") == 0) {
		spectralset(spectral, DFS_PARAM_RELPWR, v);
	} else if (strcmp(param, "relstep") == 0) {
		spectralset(spectral, DFS_PARAM_RELSTEP, v);
	} else if (strcmp(param, "maxlen") == 0) {
		spectralset(spectral, DFS_PARAM_MAXLEN, v);
	} else if (strcmp(param, "usefir128") == 0) {
		spectralset(spectral, DFS_PARAM_USEFIR128, v);
	} else if (strcmp(param, "blockspectral") == 0) {
		spectralset(spectral, DFS_PARAM_BLOCKRADAR, v);
	} else if (strcmp(param, "enmaxrssi") == 0) {
		spectralset(spectral, DFS_PARAM_MAXRSSI_EN, v);
	} else if (strcmp(param, "extchannel") == 0) {
		spectralset(spectral, DFS_PARAM_EN_EXTCH, v);
	} else if (strcmp(param, "enrelpwr") == 0) {
		spectralset(spectral, DFS_PARAM_RELPWR_EN, v);
	} else if (strcmp(param, "en_relstep_check") == 0) {
		spectralset(spectral, DFS_PARAM_RELSTEP_EN, v);
	} else {
		return 0;
	}
#endif

	return 1;
}

void
usage(const char *progname)
{
	printf("Usage:\n");
	printf("\t%s: [-i <interface>] <cmd> (<arg>)\n", progname);
	printf("\t%s: [-h]\n", progname);
	printf("\n");
	printf("Valid commands:\n");
	printf("\tget:\t\tGet current spectral parameters\n");
	printf("\tset <param> <value>:\t\tSet spectral parameter\n");
	printf("\tstart: Start spectral scan\n");
	printf("\tstop: Stop spectral scan\n");
	printf("\tenable_at_reset <0|1>: enable reporting upon channel reset\n");
}

int
main(int argc, char *argv[])
{
	struct spectralhandler spectral;
	const char *devname = ATH_DEFAULT;
	const char *progname = argv[0];

	memset(&spectral, 0, sizeof(spectral));

	/* Parse command line options */
	if (argc >= 2 && strcmp(argv[1], "-h") == 0) {
		usage(progname);
		exit(0);
	}
	if (argc >= 2 && strcmp(argv[1], "-?") == 0) {
		usage(progname);
		exit(0);
	}

	if (argc >= 2 && strcmp(argv[1], "-i") == 0) {
		if (argc == 2) {
			usage(progname);
			exit(127);
		}
		devname = argv[2];
		argc -= 2; argv += 2;
	}

	/* At this point we require at least one command */
	if (argc == 1) {
		usage(progname);
		exit(127);
	}

	if (spectral_opendev(&spectral, devname) == 0)
		exit(127);

	if (strcasecmp(argv[1], "get") == 0) {
		spectral_get(&spectral);
	} else if (strcasecmp(argv[1], "set") == 0) {
		if (argc < 4) {
			usage(progname);
			exit(127);
		}
		if (spectral_set_param(&spectral, argv[2], argv[3]) == 0) {
			usage(progname);
			exit(127);
		}
	} else if (strcasecmp(argv[1], "start") == 0) {
		spectral_start(&spectral);
	} else if (strcasecmp(argv[1], "stop") == 0) {
		spectral_stop(&spectral);
	} else if (strcasecmp(argv[1], "enable_at_reset") == 0) {
		if (argc < 3) {
			usage(progname);
			exit(127);
		}
		spectral_enable_at_reset(&spectral, atoi(argv[2]));
	} else {
		usage(progname);
		exit(127);
	}

	/* wrap up */
	spectral_closedev(&spectral);
	exit(0);
}
