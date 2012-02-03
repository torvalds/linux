/*
 * Misc utility routines used by kernel or app-level.
 * Contents are wifi-specific, used by any kernel or app-level
 * software that might want wifi things as it grows.
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 * $Id: bcm_app_utils.c 275693 2011-08-04 19:59:34Z $
 */

#include <typedefs.h>

#ifdef BCMDRIVER
#include <osl.h>
#include <bcmutils.h>
#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
#define tolower(c) (bcm_isupper((c)) ? ((c) + 'a' - 'A') : (c))
#else /* BCMDRIVER */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#ifndef ASSERT
#define ASSERT(exp)
#endif
#endif /* BCMDRIVER */
#include <bcmwifi.h>

#if defined(WIN32) && (defined(BCMDLL) || defined(WLMDLL))
#include <bcmstdlib.h> 	/* For wl/exe/GNUmakefile.brcm_wlu and GNUmakefile.wlm_dll */
#endif

#include <bcmutils.h>
#include <wlioctl.h>

cca_congest_channel_req_t *
cca_per_chan_summary(cca_congest_channel_req_t *input, cca_congest_channel_req_t *avg,
	bool percent);

int
cca_analyze(cca_congest_channel_req_t *input[], int num_chans, uint flags, chanspec_t *answer);

/* 	Take an array of measurments representing a single channel over time and return
	a summary. Currently implemented as a simple average but could easily evolve
	into more cpomplex alogrithms.
*/
cca_congest_channel_req_t *
cca_per_chan_summary(cca_congest_channel_req_t *input, cca_congest_channel_req_t *avg, bool percent)
{
	int sec;
	cca_congest_t totals;

	totals.duration  = 0;
	totals.congest_ibss  = 0;
	totals.congest_obss  = 0;
	totals.interference  = 0;
	avg->num_secs = 0;

	for (sec = 0; sec < input->num_secs; sec++) {
		if (input->secs[sec].duration) {
			totals.duration += input->secs[sec].duration;
			totals.congest_ibss += input->secs[sec].congest_ibss;
			totals.congest_obss += input->secs[sec].congest_obss;
			totals.interference += input->secs[sec].interference;
			avg->num_secs++;
		}
	}
	avg->chanspec = input->chanspec;

	if (!avg->num_secs || !totals.duration)
		return (avg);

	if (percent) {
		avg->secs[0].duration = totals.duration / avg->num_secs;
		avg->secs[0].congest_ibss = totals.congest_ibss * 100/totals.duration;
		avg->secs[0].congest_obss = totals.congest_obss * 100/totals.duration;
		avg->secs[0].interference = totals.interference * 100/totals.duration;
	} else {
		avg->secs[0].duration = totals.duration / avg->num_secs;
		avg->secs[0].congest_ibss = totals.congest_ibss / avg->num_secs;
		avg->secs[0].congest_obss = totals.congest_obss / avg->num_secs;
		avg->secs[0].interference = totals.interference / avg->num_secs;
	}

	return (avg);
}

static void
cca_info(uint8 *bitmap, int num_bits, int *left, int *bit_pos)
{
	int i;
	for (*left = 0, i = 0; i < num_bits; i++) {
		if (isset(bitmap, i)) {
			(*left)++;
			*bit_pos = i;
		}
	}
}

static uint8
spec_to_chan(chanspec_t chspec)
{
	switch (CHSPEC_CTL_SB(chspec)) {
		case WL_CHANSPEC_CTL_SB_NONE:
			return CHSPEC_CHANNEL(chspec);
		case WL_CHANSPEC_CTL_SB_UPPER:
			return UPPER_20_SB(CHSPEC_CHANNEL(chspec));
		case WL_CHANSPEC_CTL_SB_LOWER:
			return LOWER_20_SB(CHSPEC_CHANNEL(chspec));
		default:
			return 0;
	}
}

#define CCA_THRESH_MILLI	14
#define CCA_THRESH_INTERFERE	6

/*
	Take an array of measumrements representing summaries of different channels.
	Return a recomended channel.
	Interference is evil, get rid of that first.
	Then hunt for lowest Other bss traffic.
	Don't forget that channels with low duration times may not have accurate readings.
	For the moment, do not overwrite input array.
*/
int
cca_analyze(cca_congest_channel_req_t *input[], int num_chans, uint flags, chanspec_t *answer)
{
	uint8 bitmap[CEIL(MAX_CCA_CHANNELS, NBBY)];	/* 38 Max channels needs 5 bytes  = 40 */
	int i, left, winner;
	uint32 min_obss = 1 << 30;

	ASSERT(num_chans < MAX_CCA_CHANNELS);
	for (i = 0; i < (int)sizeof(bitmap); i++)
		bitmap[i] = 0;

	/* Initially, all channels are up for consideration */
	for (i = 0; i < num_chans; i++) {
		if (input[i]->chanspec)
			setbit(bitmap, i);
	}
	cca_info(bitmap, num_chans, &left, &i);
	if (!left)
		return CCA_ERRNO_TOO_FEW;

	/* Filter for 2.4 GHz Band */
	if (flags & CCA_FLAG_2G_ONLY) {
		for (i = 0; i < num_chans; i++) {
			if (!CHSPEC_IS2G(input[i]->chanspec))
				clrbit(bitmap, i);
		}
	}
	cca_info(bitmap, num_chans, &left, &i);
	if (!left)
		return CCA_ERRNO_BAND;

	/* Filter for 5 GHz Band */
	if (flags & CCA_FLAG_5G_ONLY) {
		for (i = 0; i < num_chans; i++) {
			if (!CHSPEC_IS5G(input[i]->chanspec))
				clrbit(bitmap, i);
		}
	}
	cca_info(bitmap, num_chans, &left, &i);
	if (!left)
		return CCA_ERRNO_BAND;

	/* Filter for Duration */
	if (!(flags & CCA_FLAG_IGNORE_DURATION)) {
		for (i = 0; i < num_chans; i++) {
			if (input[i]->secs[0].duration < CCA_THRESH_MILLI)
				clrbit(bitmap, i);
		}
	}
	cca_info(bitmap, num_chans, &left, &i);
	if (!left)
		return CCA_ERRNO_DURATION;

	/* Filter for 1 6 11 on 2.4 Band */
	if (flags &  CCA_FLAGS_PREFER_1_6_11) {
		int tmp_channel = spec_to_chan(input[i]->chanspec);
		int is2g = CHSPEC_IS2G(input[i]->chanspec);
		for (i = 0; i < num_chans; i++) {
			if (is2g && tmp_channel != 1 && tmp_channel != 6 && tmp_channel != 11)
				clrbit(bitmap, i);
		}
	}
	cca_info(bitmap, num_chans, &left, &i);
	if (!left)
		return CCA_ERRNO_PREF_CHAN;

	/* Toss high interference interference */
	if (!(flags & CCA_FLAG_IGNORE_INTERFER)) {
		for (i = 0; i < num_chans; i++) {
			if (input[i]->secs[0].interference > CCA_THRESH_INTERFERE)
				clrbit(bitmap, i);
		}
		cca_info(bitmap, num_chans, &left, &i);
		if (!left)
			return CCA_ERRNO_INTERFER;
	}

	/* Now find lowest obss */
	winner = 0;
	for (i = 0; i < num_chans; i++) {
		if (isset(bitmap, i) && input[i]->secs[0].congest_obss < min_obss) {
			winner = i;
			min_obss = input[i]->secs[0].congest_obss;
		}
	}
	*answer = input[winner]->chanspec;

	return 0;
}
