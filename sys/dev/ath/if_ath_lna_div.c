/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Adrian Chadd <adrian@FreeBSD.org>
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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This module handles LNA diversity for those chips which implement LNA
 * mixing (AR9285/AR9485.)
 */
#include "opt_ath.h"
#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/errno.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>

#include <sys/socket.h>
 
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/ethernet.h>		/* XXX for ether_sprintf */

#include <net80211/ieee80211_var.h>

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/if_ath_debug.h>
#include <dev/ath/if_ath_lna_div.h>

/* Linux compatibility macros */
/*
 * XXX these don't handle rounding, underflow, overflow, wrapping!
 */
#define	msecs_to_jiffies(a)		( (a) * hz / 1000 )

/*
 * Methods which are required
 */

/*
 * Attach the LNA diversity to the given interface
 */
int
ath_lna_div_attach(struct ath_softc *sc)
{
	struct if_ath_ant_comb_state *ss;
	HAL_ANT_COMB_CONFIG div_ant_conf;

	/* Only do this if diversity is enabled */
	if (! ath_hal_hasdivantcomb(sc->sc_ah))
		return (0);

	ss = malloc(sizeof(struct if_ath_ant_comb_state),
	    M_TEMP, M_WAITOK | M_ZERO);
	if (ss == NULL) {
		device_printf(sc->sc_dev, "%s: failed to allocate\n",
		    __func__);
		/* Don't fail at this point */
		return (0);
	}

	/* Fetch the hardware configuration */
	OS_MEMZERO(&div_ant_conf, sizeof(div_ant_conf));
	ath_hal_div_comb_conf_get(sc->sc_ah, &div_ant_conf);

	/* Figure out what the hardware specific bits should be */
	if ((div_ant_conf.antdiv_configgroup == HAL_ANTDIV_CONFIG_GROUP_1) ||
	    (div_ant_conf.antdiv_configgroup == HAL_ANTDIV_CONFIG_GROUP_2)) {
		ss->lna1_lna2_delta = -9;
	} else {
		ss->lna1_lna2_delta = -3;
	}

	/* Let's flip this on */
	sc->sc_lna_div = ss;
	sc->sc_dolnadiv = 1;

	return (0);
}

/*
 * Detach the LNA diversity state from the given interface
 */
int
ath_lna_div_detach(struct ath_softc *sc)
{
	if (sc->sc_lna_div != NULL) {
		free(sc->sc_lna_div, M_TEMP);
		sc->sc_lna_div = NULL;
	}
	sc->sc_dolnadiv = 0;
	return (0);
}

/*
 * Enable LNA diversity on the current channel if it's required.
 */
int
ath_lna_div_enable(struct ath_softc *sc, const struct ieee80211_channel *chan)
{

	return (0);
}

/*
 * Handle ioctl requests from the diagnostic interface.
 *
 * The initial part of this code resembles ath_ioctl_diag();
 * it's likely a good idea to reduce duplication between
 * these two routines.
 */
int
ath_lna_div_ioctl(struct ath_softc *sc, struct ath_diag *ad)
{
	unsigned int id = ad->ad_id & ATH_DIAG_ID;
	void *indata = NULL;
	void *outdata = NULL;
	u_int32_t insize = ad->ad_in_size;
	u_int32_t outsize = ad->ad_out_size;
	int error = 0;
//	int val;

	if (ad->ad_id & ATH_DIAG_IN) {
		/*
		 * Copy in data.
		 */
		indata = malloc(insize, M_TEMP, M_NOWAIT);
		if (indata == NULL) {
			error = ENOMEM;
			goto bad;
		}
		error = copyin(ad->ad_in_data, indata, insize);
		if (error)
			goto bad;
	}
	if (ad->ad_id & ATH_DIAG_DYN) {
		/*
		 * Allocate a buffer for the results (otherwise the HAL
		 * returns a pointer to a buffer where we can read the
		 * results).  Note that we depend on the HAL leaving this
		 * pointer for us to use below in reclaiming the buffer;
		 * may want to be more defensive.
		 */
		outdata = malloc(outsize, M_TEMP, M_NOWAIT | M_ZERO);
		if (outdata == NULL) {
			error = ENOMEM;
			goto bad;
		}
	}
	switch (id) {
		default:
			error = EINVAL;
			goto bad;
	}
	if (outsize < ad->ad_out_size)
		ad->ad_out_size = outsize;
	if (outdata && copyout(outdata, ad->ad_out_data, ad->ad_out_size))
		error = EFAULT;
bad:
	if ((ad->ad_id & ATH_DIAG_IN) && indata != NULL)
		free(indata, M_TEMP);
	if ((ad->ad_id & ATH_DIAG_DYN) && outdata != NULL)
		free(outdata, M_TEMP);
	return (error);
}

/*
 * XXX need to low_rssi_thresh config from ath9k, to support CUS198
 * antenna diversity correctly.
 */
static HAL_BOOL
ath_is_alt_ant_ratio_better(int alt_ratio, int maxdelta, int mindelta,
    int main_rssi_avg, int alt_rssi_avg, int pkt_count)
{
	return (((alt_ratio >= ATH_ANT_DIV_COMB_ALT_ANT_RATIO2) &&
		(alt_rssi_avg > main_rssi_avg + maxdelta)) ||
		(alt_rssi_avg > main_rssi_avg + mindelta)) && (pkt_count > 50);
}

static void
ath_lnaconf_alt_good_scan(struct if_ath_ant_comb_state *antcomb,
    HAL_ANT_COMB_CONFIG *ant_conf, int main_rssi_avg)
{
	antcomb->quick_scan_cnt = 0;

	if (ant_conf->main_lna_conf == HAL_ANT_DIV_COMB_LNA2)
		antcomb->rssi_lna2 = main_rssi_avg;
	else if (ant_conf->main_lna_conf == HAL_ANT_DIV_COMB_LNA1)
		antcomb->rssi_lna1 = main_rssi_avg;

	switch ((ant_conf->main_lna_conf << 4) | ant_conf->alt_lna_conf) {
	case (0x10): /* LNA2 A-B */
		antcomb->main_conf = HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->first_quick_scan_conf =
			HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->second_quick_scan_conf = HAL_ANT_DIV_COMB_LNA1;
		break;
	case (0x20): /* LNA1 A-B */
		antcomb->main_conf = HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->first_quick_scan_conf =
			HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->second_quick_scan_conf = HAL_ANT_DIV_COMB_LNA2;
		break;
	case (0x21): /* LNA1 LNA2 */
		antcomb->main_conf = HAL_ANT_DIV_COMB_LNA2;
		antcomb->first_quick_scan_conf =
			HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf =
			HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		break;
	case (0x12): /* LNA2 LNA1 */
		antcomb->main_conf = HAL_ANT_DIV_COMB_LNA1;
		antcomb->first_quick_scan_conf =
			HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf =
			HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		break;
	case (0x13): /* LNA2 A+B */
		antcomb->main_conf = HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->first_quick_scan_conf =
			HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf = HAL_ANT_DIV_COMB_LNA1;
		break;
	case (0x23): /* LNA1 A+B */
		antcomb->main_conf = HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->first_quick_scan_conf =
			HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf = HAL_ANT_DIV_COMB_LNA2;
		break;
	default:
		break;
	}
}

static void
ath_select_ant_div_from_quick_scan(struct if_ath_ant_comb_state *antcomb,
    HAL_ANT_COMB_CONFIG *div_ant_conf, int main_rssi_avg,
    int alt_rssi_avg, int alt_ratio)
{
	/* alt_good */
	switch (antcomb->quick_scan_cnt) {
	case 0:
		/* set alt to main, and alt to first conf */
		div_ant_conf->main_lna_conf = antcomb->main_conf;
		div_ant_conf->alt_lna_conf = antcomb->first_quick_scan_conf;
		break;
	case 1:
		/* set alt to main, and alt to first conf */
		div_ant_conf->main_lna_conf = antcomb->main_conf;
		div_ant_conf->alt_lna_conf = antcomb->second_quick_scan_conf;
		antcomb->rssi_first = main_rssi_avg;
		antcomb->rssi_second = alt_rssi_avg;

		if (antcomb->main_conf == HAL_ANT_DIV_COMB_LNA1) {
			/* main is LNA1 */
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_HI,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->first_ratio = AH_TRUE;
			else
				antcomb->first_ratio = AH_FALSE;
		} else if (antcomb->main_conf == HAL_ANT_DIV_COMB_LNA2) {
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_MID,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->first_ratio = AH_TRUE;
			else
				antcomb->first_ratio = AH_FALSE;
		} else {
			if ((((alt_ratio >= ATH_ANT_DIV_COMB_ALT_ANT_RATIO2) &&
			    (alt_rssi_avg > main_rssi_avg +
			    ATH_ANT_DIV_COMB_LNA1_DELTA_HI)) ||
			    (alt_rssi_avg > main_rssi_avg)) &&
			    (antcomb->total_pkt_count > 50))
				antcomb->first_ratio = AH_TRUE;
			else
				antcomb->first_ratio = AH_FALSE;
		}
		break;
	case 2:
		antcomb->alt_good = AH_FALSE;
		antcomb->scan_not_start = AH_FALSE;
		antcomb->scan = AH_FALSE;
		antcomb->rssi_first = main_rssi_avg;
		antcomb->rssi_third = alt_rssi_avg;

		if (antcomb->second_quick_scan_conf == HAL_ANT_DIV_COMB_LNA1)
			antcomb->rssi_lna1 = alt_rssi_avg;
		else if (antcomb->second_quick_scan_conf ==
			 HAL_ANT_DIV_COMB_LNA2)
			antcomb->rssi_lna2 = alt_rssi_avg;
		else if (antcomb->second_quick_scan_conf ==
			 HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2) {
			if (antcomb->main_conf == HAL_ANT_DIV_COMB_LNA2)
				antcomb->rssi_lna2 = main_rssi_avg;
			else if (antcomb->main_conf == HAL_ANT_DIV_COMB_LNA1)
				antcomb->rssi_lna1 = main_rssi_avg;
		}

		if (antcomb->rssi_lna2 > antcomb->rssi_lna1 +
		    ATH_ANT_DIV_COMB_LNA1_LNA2_SWITCH_DELTA)
			div_ant_conf->main_lna_conf = HAL_ANT_DIV_COMB_LNA2;
		else
			div_ant_conf->main_lna_conf = HAL_ANT_DIV_COMB_LNA1;

		if (antcomb->main_conf == HAL_ANT_DIV_COMB_LNA1) {
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_HI,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->second_ratio = AH_TRUE;
			else
				antcomb->second_ratio = AH_FALSE;
		} else if (antcomb->main_conf == HAL_ANT_DIV_COMB_LNA2) {
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_MID,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->second_ratio = AH_TRUE;
			else
				antcomb->second_ratio = AH_FALSE;
		} else {
			if ((((alt_ratio >= ATH_ANT_DIV_COMB_ALT_ANT_RATIO2) &&
			    (alt_rssi_avg > main_rssi_avg +
			    ATH_ANT_DIV_COMB_LNA1_DELTA_HI)) ||
			    (alt_rssi_avg > main_rssi_avg)) &&
			    (antcomb->total_pkt_count > 50))
				antcomb->second_ratio = AH_TRUE;
			else
				antcomb->second_ratio = AH_FALSE;
		}

		/* set alt to the conf with maximun ratio */
		if (antcomb->first_ratio && antcomb->second_ratio) {
			if (antcomb->rssi_second > antcomb->rssi_third) {
				/* first alt*/
				if ((antcomb->first_quick_scan_conf ==
				    HAL_ANT_DIV_COMB_LNA1) ||
				    (antcomb->first_quick_scan_conf ==
				    HAL_ANT_DIV_COMB_LNA2))
					/* Set alt LNA1 or LNA2*/
					if (div_ant_conf->main_lna_conf ==
					    HAL_ANT_DIV_COMB_LNA2)
						div_ant_conf->alt_lna_conf =
							HAL_ANT_DIV_COMB_LNA1;
					else
						div_ant_conf->alt_lna_conf =
							HAL_ANT_DIV_COMB_LNA2;
				else
					/* Set alt to A+B or A-B */
					div_ant_conf->alt_lna_conf =
						antcomb->first_quick_scan_conf;
			} else if ((antcomb->second_quick_scan_conf ==
				   HAL_ANT_DIV_COMB_LNA1) ||
				   (antcomb->second_quick_scan_conf ==
				   HAL_ANT_DIV_COMB_LNA2)) {
				/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    HAL_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA2;
			} else {
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf =
					antcomb->second_quick_scan_conf;
			}
		} else if (antcomb->first_ratio) {
			/* first alt */
			if ((antcomb->first_quick_scan_conf ==
			    HAL_ANT_DIV_COMB_LNA1) ||
			    (antcomb->first_quick_scan_conf ==
			    HAL_ANT_DIV_COMB_LNA2))
					/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    HAL_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
							HAL_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
							HAL_ANT_DIV_COMB_LNA2;
			else
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf =
						antcomb->first_quick_scan_conf;
		} else if (antcomb->second_ratio) {
				/* second alt */
			if ((antcomb->second_quick_scan_conf ==
			    HAL_ANT_DIV_COMB_LNA1) ||
			    (antcomb->second_quick_scan_conf ==
			    HAL_ANT_DIV_COMB_LNA2))
				/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    HAL_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA2;
			else
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf =
						antcomb->second_quick_scan_conf;
		} else {
			/* main is largest */
			if ((antcomb->main_conf == HAL_ANT_DIV_COMB_LNA1) ||
			    (antcomb->main_conf == HAL_ANT_DIV_COMB_LNA2))
				/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    HAL_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
							HAL_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
							HAL_ANT_DIV_COMB_LNA2;
			else
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf = antcomb->main_conf;
		}
		break;
	default:
		break;
	}
}

static void
ath_ant_adjust_fast_divbias(struct if_ath_ant_comb_state *antcomb,
    int alt_ratio, int alt_ant_ratio_th, u_int config_group,
    HAL_ANT_COMB_CONFIG *pdiv_ant_conf)
{

	if (config_group == HAL_ANTDIV_CONFIG_GROUP_1) {
		switch ((pdiv_ant_conf->main_lna_conf << 4)
		    | pdiv_ant_conf->alt_lna_conf) {
		case (0x01): //A-B LNA2
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x02): //A-B LNA1
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x03): //A-B A+B
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x10): //LNA2 A-B
			if ((antcomb->scan == 0)
			    && (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO)) {
				pdiv_ant_conf->fast_div_bias = 0x3f;
			} else {
				pdiv_ant_conf->fast_div_bias = 0x1;
			}
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x12): //LNA2 LNA1
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
			case (0x13): //LNA2 A+B
			if ((antcomb->scan == 0)
			    && (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO)) {
				pdiv_ant_conf->fast_div_bias = 0x3f;
			} else {
				pdiv_ant_conf->fast_div_bias = 0x1;
			}
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x20): //LNA1 A-B
			if ((antcomb->scan == 0)
			    && (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO)) {
				pdiv_ant_conf->fast_div_bias = 0x3f;
			} else {
				pdiv_ant_conf->fast_div_bias = 0x1;
			}
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x21): //LNA1 LNA2
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x23): //LNA1 A+B
			if ((antcomb->scan == 0)
			    && (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO)) {
				pdiv_ant_conf->fast_div_bias = 0x3f;
			} else {
				pdiv_ant_conf->fast_div_bias = 0x1;
			}
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x30): //A+B A-B
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x31): //A+B LNA2
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x32): //A+B LNA1
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		default:
			break;
		}
	} else if (config_group == HAL_ANTDIV_CONFIG_GROUP_2) {
		switch ((pdiv_ant_conf->main_lna_conf << 4)
		    | pdiv_ant_conf->alt_lna_conf) {
		case (0x01): //A-B LNA2
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x02): //A-B LNA1
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x03): //A-B A+B
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x10): //LNA2 A-B
			if ((antcomb->scan == 0)
			    && (alt_ratio > alt_ant_ratio_th)) {
				pdiv_ant_conf->fast_div_bias = 0x1;
			} else {
				pdiv_ant_conf->fast_div_bias = 0x2;
			}
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x12): //LNA2 LNA1
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x13): //LNA2 A+B
			if ((antcomb->scan == 0)
			    && (alt_ratio > alt_ant_ratio_th)) {
				pdiv_ant_conf->fast_div_bias = 0x1;
			} else {
				pdiv_ant_conf->fast_div_bias = 0x2;
			}
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x20): //LNA1 A-B
			if ((antcomb->scan == 0)
			    && (alt_ratio > alt_ant_ratio_th)) {
				pdiv_ant_conf->fast_div_bias = 0x1;
			} else {
				pdiv_ant_conf->fast_div_bias = 0x2;
			}
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x21): //LNA1 LNA2
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x23): //LNA1 A+B
			if ((antcomb->scan == 0)
			    && (alt_ratio > alt_ant_ratio_th)) {
				pdiv_ant_conf->fast_div_bias = 0x1;
			} else {
				pdiv_ant_conf->fast_div_bias = 0x2;
			}
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x30): //A+B A-B
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x31): //A+B LNA2
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		case (0x32): //A+B LNA1
			pdiv_ant_conf->fast_div_bias = 0x1;
			pdiv_ant_conf->main_gaintb   = 0;
			pdiv_ant_conf->alt_gaintb    = 0;
			break;
		default:
			break;
		}
	} else { /* DEFAULT_ANTDIV_CONFIG_GROUP */
		switch ((pdiv_ant_conf->main_lna_conf << 4) | pdiv_ant_conf->alt_lna_conf) {
		case (0x01): //A-B LNA2
			pdiv_ant_conf->fast_div_bias = 0x3b;
			break;
		case (0x02): //A-B LNA1
			pdiv_ant_conf->fast_div_bias = 0x3d;
			break;
		case (0x03): //A-B A+B
			pdiv_ant_conf->fast_div_bias = 0x1;
			break;
		case (0x10): //LNA2 A-B
			pdiv_ant_conf->fast_div_bias = 0x7;
			break;
		case (0x12): //LNA2 LNA1
			pdiv_ant_conf->fast_div_bias = 0x2;
			break;
		case (0x13): //LNA2 A+B
			pdiv_ant_conf->fast_div_bias = 0x7;
			break;
		case (0x20): //LNA1 A-B
			pdiv_ant_conf->fast_div_bias = 0x6;
			break;
		case (0x21): //LNA1 LNA2
			pdiv_ant_conf->fast_div_bias = 0x0;
			break;
		case (0x23): //LNA1 A+B
			pdiv_ant_conf->fast_div_bias = 0x6;
			break;
		case (0x30): //A+B A-B
			pdiv_ant_conf->fast_div_bias = 0x1;
			break;
		case (0x31): //A+B LNA2
			pdiv_ant_conf->fast_div_bias = 0x3b;
			break;
		case (0x32): //A+B LNA1
			pdiv_ant_conf->fast_div_bias = 0x3d;
			break;
		default:
			break;
		}
	}
}

/*
 * AR9485/AR933x TODO:
 * + Select a ratio based on whether RSSI is low or not; but I need
 *   to figure out what "low_rssi_th" is sourced from.
 * + What's ath_ant_div_comb_alt_check() in the reference driver do?
 * + .. and there's likely a bunch of other things to include in this.
 */

/* Antenna diversity and combining */
void
ath_lna_rx_comb_scan(struct ath_softc *sc, struct ath_rx_status *rs,
    unsigned long ticks, int hz)
{
	HAL_ANT_COMB_CONFIG div_ant_conf;
	struct if_ath_ant_comb_state *antcomb = sc->sc_lna_div;
	int alt_ratio = 0, alt_rssi_avg = 0, main_rssi_avg = 0, curr_alt_set;
	int curr_main_set, curr_bias;
	int main_rssi = rs->rs_rssi_ctl[0];
	int alt_rssi = rs->rs_rssi_ctl[1];
	int rx_ant_conf, main_ant_conf, alt_ant_conf;
	HAL_BOOL short_scan = AH_FALSE;

	rx_ant_conf = (rs->rs_rssi_ctl[2] >> 4) & ATH_ANT_RX_MASK;
	main_ant_conf = (rs->rs_rssi_ctl[2] >> 2) & ATH_ANT_RX_MASK;
	alt_ant_conf = (rs->rs_rssi_ctl[2] >> 0) & ATH_ANT_RX_MASK;

#if 0
	DPRINTF(sc, ATH_DEBUG_DIVERSITY,
	    "%s: RSSI %d/%d, conf %x/%x, rxconf %x, LNA: %d; ANT: %d; "
	    "FastDiv: %d\n",
	    __func__,
	    main_rssi,
	    alt_rssi,
	    main_ant_conf,
	    alt_ant_conf,
	    rx_ant_conf,
	    !!(rs->rs_rssi_ctl[2] & 0x80),
	    !!(rs->rs_rssi_ctl[2] & 0x40),
	    !!(rs->rs_rssi_ext[2] & 0x40));
#endif

	/*
	 * If LNA diversity combining isn't enabled, don't run this.
	 */
	if (! sc->sc_dolnadiv)
		return;

	/*
	 * XXX this is ugly, but the HAL code attaches the
	 * LNA diversity to the TX antenna settings.
	 * I don't know why.
	 */
	if (sc->sc_txantenna != HAL_ANT_VARIABLE)
		return;

	/* Record packet only when alt_rssi is positive */
	if (main_rssi > 0 && alt_rssi > 0) {
		antcomb->total_pkt_count++;
		antcomb->main_total_rssi += main_rssi;
		antcomb->alt_total_rssi  += alt_rssi;
		if (main_ant_conf == rx_ant_conf)
			antcomb->main_recv_cnt++;
		else
			antcomb->alt_recv_cnt++;
	}

	/* Short scan check */
	if (antcomb->scan && antcomb->alt_good) {
		if (ieee80211_time_after(ticks, antcomb->scan_start_time +
		    msecs_to_jiffies(ATH_ANT_DIV_COMB_SHORT_SCAN_INTR)))
			short_scan = AH_TRUE;
		else
			if (antcomb->total_pkt_count ==
			    ATH_ANT_DIV_COMB_SHORT_SCAN_PKTCOUNT) {
				alt_ratio = ((antcomb->alt_recv_cnt * 100) /
					    antcomb->total_pkt_count);
				if (alt_ratio < ATH_ANT_DIV_COMB_ALT_ANT_RATIO)
					short_scan = AH_TRUE;
			}
	}

#if 0
	DPRINTF(sc, ATH_DEBUG_DIVERSITY,
	    "%s: total pkt=%d, aggr=%d, short_scan=%d\n",
	    __func__,
	    antcomb->total_pkt_count,
	    !! (rs->rs_moreaggr),
	    !! (short_scan));
#endif

	if (((antcomb->total_pkt_count < ATH_ANT_DIV_COMB_MAX_PKTCOUNT) ||
	    rs->rs_moreaggr) && !short_scan)
		return;

	if (antcomb->total_pkt_count) {
		alt_ratio = ((antcomb->alt_recv_cnt * 100) /
			     antcomb->total_pkt_count);
		main_rssi_avg = (antcomb->main_total_rssi /
				 antcomb->total_pkt_count);
		alt_rssi_avg = (antcomb->alt_total_rssi /
				 antcomb->total_pkt_count);
	}

	OS_MEMZERO(&div_ant_conf, sizeof(div_ant_conf));

	ath_hal_div_comb_conf_get(sc->sc_ah, &div_ant_conf);
	curr_alt_set = div_ant_conf.alt_lna_conf;
	curr_main_set = div_ant_conf.main_lna_conf;
	curr_bias = div_ant_conf.fast_div_bias;

	antcomb->count++;

	if (antcomb->count == ATH_ANT_DIV_COMB_MAX_COUNT) {
		if (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO) {
			ath_lnaconf_alt_good_scan(antcomb, &div_ant_conf,
						  main_rssi_avg);
			antcomb->alt_good = AH_TRUE;
		} else {
			antcomb->alt_good = AH_FALSE;
		}

		antcomb->count = 0;
		antcomb->scan = AH_TRUE;
		antcomb->scan_not_start = AH_TRUE;
	}

	if (!antcomb->scan) {
		if (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO) {
			if (curr_alt_set == HAL_ANT_DIV_COMB_LNA2) {
				/* Switch main and alt LNA */
				div_ant_conf.main_lna_conf =
						HAL_ANT_DIV_COMB_LNA2;
				div_ant_conf.alt_lna_conf  =
						HAL_ANT_DIV_COMB_LNA1;
			} else if (curr_alt_set == HAL_ANT_DIV_COMB_LNA1) {
				div_ant_conf.main_lna_conf =
						HAL_ANT_DIV_COMB_LNA1;
				div_ant_conf.alt_lna_conf  =
						HAL_ANT_DIV_COMB_LNA2;
			}

			goto div_comb_done;
		} else if ((curr_alt_set != HAL_ANT_DIV_COMB_LNA1) &&
			   (curr_alt_set != HAL_ANT_DIV_COMB_LNA2)) {
			/* Set alt to another LNA */
			if (curr_main_set == HAL_ANT_DIV_COMB_LNA2)
				div_ant_conf.alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA1;
			else if (curr_main_set == HAL_ANT_DIV_COMB_LNA1)
				div_ant_conf.alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA2;

			goto div_comb_done;
		}

		if ((alt_rssi_avg < (main_rssi_avg +
		    antcomb->lna1_lna2_delta)))
			goto div_comb_done;
	}

	if (!antcomb->scan_not_start) {
		switch (curr_alt_set) {
		case HAL_ANT_DIV_COMB_LNA2:
			antcomb->rssi_lna2 = alt_rssi_avg;
			antcomb->rssi_lna1 = main_rssi_avg;
			antcomb->scan = AH_TRUE;
			/* set to A+B */
			div_ant_conf.main_lna_conf =
				HAL_ANT_DIV_COMB_LNA1;
			div_ant_conf.alt_lna_conf  =
				HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2;
			break;
		case HAL_ANT_DIV_COMB_LNA1:
			antcomb->rssi_lna1 = alt_rssi_avg;
			antcomb->rssi_lna2 = main_rssi_avg;
			antcomb->scan = AH_TRUE;
			/* set to A+B */
			div_ant_conf.main_lna_conf = HAL_ANT_DIV_COMB_LNA2;
			div_ant_conf.alt_lna_conf  =
				HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2;
			break;
		case HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2:
			antcomb->rssi_add = alt_rssi_avg;
			antcomb->scan = AH_TRUE;
			/* set to A-B */
			div_ant_conf.alt_lna_conf =
				HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2;
			break;
		case HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2:
			antcomb->rssi_sub = alt_rssi_avg;
			antcomb->scan = AH_FALSE;
			if (antcomb->rssi_lna2 >
			    (antcomb->rssi_lna1 +
			    ATH_ANT_DIV_COMB_LNA1_LNA2_SWITCH_DELTA)) {
				/* use LNA2 as main LNA */
				if ((antcomb->rssi_add > antcomb->rssi_lna1) &&
				    (antcomb->rssi_add > antcomb->rssi_sub)) {
					/* set to A+B */
					div_ant_conf.main_lna_conf =
						HAL_ANT_DIV_COMB_LNA2;
					div_ant_conf.alt_lna_conf  =
						HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2;
				} else if (antcomb->rssi_sub >
					   antcomb->rssi_lna1) {
					/* set to A-B */
					div_ant_conf.main_lna_conf =
						HAL_ANT_DIV_COMB_LNA2;
					div_ant_conf.alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2;
				} else {
					/* set to LNA1 */
					div_ant_conf.main_lna_conf =
						HAL_ANT_DIV_COMB_LNA2;
					div_ant_conf.alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA1;
				}
			} else {
				/* use LNA1 as main LNA */
				if ((antcomb->rssi_add > antcomb->rssi_lna2) &&
				    (antcomb->rssi_add > antcomb->rssi_sub)) {
					/* set to A+B */
					div_ant_conf.main_lna_conf =
						HAL_ANT_DIV_COMB_LNA1;
					div_ant_conf.alt_lna_conf  =
						HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2;
				} else if (antcomb->rssi_sub >
					   antcomb->rssi_lna1) {
					/* set to A-B */
					div_ant_conf.main_lna_conf =
						HAL_ANT_DIV_COMB_LNA1;
					div_ant_conf.alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2;
				} else {
					/* set to LNA2 */
					div_ant_conf.main_lna_conf =
						HAL_ANT_DIV_COMB_LNA1;
					div_ant_conf.alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA2;
				}
			}
			break;
		default:
			break;
		}
	} else {
		if (!antcomb->alt_good) {
			antcomb->scan_not_start = AH_FALSE;
			/* Set alt to another LNA */
			if (curr_main_set == HAL_ANT_DIV_COMB_LNA2) {
				div_ant_conf.main_lna_conf =
						HAL_ANT_DIV_COMB_LNA2;
				div_ant_conf.alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA1;
			} else if (curr_main_set == HAL_ANT_DIV_COMB_LNA1) {
				div_ant_conf.main_lna_conf =
						HAL_ANT_DIV_COMB_LNA1;
				div_ant_conf.alt_lna_conf =
						HAL_ANT_DIV_COMB_LNA2;
			}
			goto div_comb_done;
		}
	}

	ath_select_ant_div_from_quick_scan(antcomb, &div_ant_conf,
					   main_rssi_avg, alt_rssi_avg,
					   alt_ratio);

	antcomb->quick_scan_cnt++;

div_comb_done:
#if 0
	ath_ant_div_conf_fast_divbias(&div_ant_conf);
#endif

	ath_ant_adjust_fast_divbias(antcomb,
	    alt_ratio,
	    ATH_ANT_DIV_COMB_ALT_ANT_RATIO,
	    div_ant_conf.antdiv_configgroup,
	    &div_ant_conf);

	ath_hal_div_comb_conf_set(sc->sc_ah, &div_ant_conf);

	DPRINTF(sc, ATH_DEBUG_DIVERSITY, "%s: total_pkt_count=%d\n",
	   __func__, antcomb->total_pkt_count);

	DPRINTF(sc, ATH_DEBUG_DIVERSITY, "%s: main_total_rssi=%d\n",
	   __func__, antcomb->main_total_rssi);
	DPRINTF(sc, ATH_DEBUG_DIVERSITY, "%s: alt_total_rssi=%d\n",
	   __func__, antcomb->alt_total_rssi);

	DPRINTF(sc, ATH_DEBUG_DIVERSITY, "%s: main_rssi_avg=%d\n",
	   __func__, main_rssi_avg);
	DPRINTF(sc, ATH_DEBUG_DIVERSITY, "%s: alt_alt_rssi_avg=%d\n",
	   __func__, alt_rssi_avg);

	DPRINTF(sc, ATH_DEBUG_DIVERSITY, "%s: main_recv_cnt=%d\n",
	   __func__, antcomb->main_recv_cnt);
	DPRINTF(sc, ATH_DEBUG_DIVERSITY, "%s: alt_recv_cnt=%d\n",
	   __func__, antcomb->alt_recv_cnt);

//	if (curr_alt_set != div_ant_conf.alt_lna_conf)
		DPRINTF(sc, ATH_DEBUG_DIVERSITY, "%s: lna_conf: %x -> %x\n",
		    __func__, curr_alt_set, div_ant_conf.alt_lna_conf);
//	if (curr_main_set != div_ant_conf.main_lna_conf)
		DPRINTF(sc, ATH_DEBUG_DIVERSITY, "%s: main_lna_conf: %x -> %x\n",
		    __func__, curr_main_set, div_ant_conf.main_lna_conf);
//	if (curr_bias != div_ant_conf.fast_div_bias)
		DPRINTF(sc, ATH_DEBUG_DIVERSITY, "%s: fast_div_bias: %x -> %x\n",
		    __func__, curr_bias, div_ant_conf.fast_div_bias);

	antcomb->scan_start_time = ticks;
	antcomb->total_pkt_count = 0;
	antcomb->main_total_rssi = 0;
	antcomb->alt_total_rssi = 0;
	antcomb->main_recv_cnt = 0;
	antcomb->alt_recv_cnt = 0;
}

