/*-
 * Copyright (c) 2019 Adrian Chadd <adrian@FreeBSD.org>.
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

#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <err.h>

#include "../common/ctrl.h"

/*
 * This is a simple wrapper program around the ANI diagnostic interface.
 * It is for fetching and setting the live ANI configuration when trying
 * to diagnose a noisy environment.
 */

/*
 * HAL_DIAG_ANI_CMD is used to set the ANI configuration.
 * HAL_DIAG_ANI_CURRENT is used to fetch the current ANI configuration.
 */

struct ani_var {
  const char *name;
  int id;
};

static struct ani_var ani_labels[] = {
  { "ofdm_noise_immunity_level", 1, },
  { "noise_immunity_level", 1, },
  { "ofdm_weak_signal_detect", 2, },
  { "cck_weak_signal_threshold", 3, },
  { "firstep_level", 4, },
  { "spur_immunity_level", 5, },
  { "mrc_cck", 8, },
  { "cck_noise_immunity_level", 9, },
  { NULL, -1, },
};

static void
usage(void)
{
	fprintf(stderr, "usage: athani [-i interface] [-l]\n");
	fprintf(stderr, "    -i: interface\n");
	fprintf(stderr, "    -l: list ANI labels\n");
	fprintf(stderr, "  If no args are given after flags, the ANI state will be listed.\n");
	fprintf(stderr, "  To set, use '<label> <value>' to set the state\n");
	exit(-1);
}

static void
list_labels(void)
{
	int i;

	for (i = 0; ani_labels[i].name != NULL; i++) {
		printf("%s (%d)\n", ani_labels[i].name, ani_labels[i].id);
	}
}

static int
ani_write_state(struct ath_driver_req *req, const char *ifname,
  const char *label, const char *value)
{
	struct ath_diag atd;
	uint32_t args[2];
	uint32_t cmd, val;
	size_t sl;
	int i;

	/* Find the label */
	sl = strlen(label);
	for (i = 0; ani_labels[i].name != NULL; i++) {
		if ((strlen(ani_labels[i].name) == sl) &&
		    (strcmp(label, ani_labels[i].name) == 0)) {
			cmd = ani_labels[i].id;
			break;
		}
	}
	if (ani_labels[i].name == NULL) {
		fprintf(stderr, "%s: couldn't find ANI label (%s)\n",
		    __func__, label);
		return (-1);
	}

	val = strtoul(value, NULL, 0);

	/*
	 * Whilst we're doing the ath_diag pieces, we have to set this
	 * ourselves.
	 */
	strncpy(atd.ad_name, ifname, sizeof (atd.ad_name));

	/*
	 * Populate HAL_DIAG_ANI_CMD fields.
	 */
	args[0] = cmd;
	args[1] = val;

	atd.ad_id = HAL_DIAG_ANI_CMD | ATH_DIAG_IN;
	atd.ad_out_data = NULL;
	atd.ad_out_size = 0;
	atd.ad_in_data = (void *) &args;
	atd.ad_in_size = sizeof(args);

	if (ath_driver_req_fetch_diag(req, SIOCGATHDIAG, &atd) < 0) {
		warn("SIOCGATHDIAG HAL_DIAG_ANI_CMD (%s)", atd.ad_name);
		return (-1);
	}

	return (0);
}

static void
ani_read_state(struct ath_driver_req *req, const char *ifname)
{
	struct ath_diag atd;
	HAL_ANI_STATE state;

	/*
	 * Whilst we're doing the ath_diag pieces, we have to set this
	 * ourselves.
	 */
	strncpy(atd.ad_name, ifname, sizeof (atd.ad_name));

	atd.ad_id = HAL_DIAG_ANI_CURRENT; /* XXX | DIAG_DYN? */
	atd.ad_out_data = (caddr_t) &state;
	atd.ad_out_size = sizeof(state);

	if (ath_driver_req_fetch_diag(req, SIOCGATHDIAG, &atd) < 0)
		err(1, "%s", atd.ad_name);


	printf("  ofdm_noise_immunity_level=%d\n", state.noiseImmunityLevel);
	printf("  cck_noise_immunity_level=%d\n", state.cckNoiseImmunityLevel);
	printf("  spur_immunity_level=%d\n", state.spurImmunityLevel);
	printf("  firstep_level=%d\n", state.firstepLevel);
	printf("  ofdm_weak_signal_detect=%d\n", state.ofdmWeakSigDetectOff);
	printf("  cck_weak_signal_threshold=%d\n", state.cckWeakSigThreshold);
	printf("  mrc_cck=%d\n", state.mrcCckOff);
	/* XXX TODO: cycle counts? */
}

int
main(int argc, char *argv[])
{
	struct ath_diag atd;
	const char *ifname;
	struct ath_driver_req req;
	int what, c;

	ath_driver_req_init(&req);

	ifname = getenv("ATH");
	if (!ifname)
		ifname = ATH_DEFAULT;

	what = 0;
	while ((c = getopt(argc, argv, "i:l")) != -1)
		switch (c) {
		case 'i':
			ifname = optarg;
			break;
		case 'l':
			list_labels();
			exit(0);
		default:
			usage();
			/*NOTREACHED*/
		}

	/* Initialise the driver interface */
	if (ath_driver_req_open(&req, ifname) < 0) {
		exit(127);
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		ani_read_state(&req, ifname);
		exit(0);
	}

	if (argc < 2) {
		usage();
		/*NOTREACHED*/
	}

	if (ani_write_state(&req, ifname, argv[0], argv[1]) != 0)
		exit(1);

	exit(0);
}
