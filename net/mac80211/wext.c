/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <asm/uaccess.h>

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "led.h"
#include "rate.h"
#include "wpa.h"
#include "aes_ccm.h"



/* Structures to export the Wireless Handlers */

static const iw_handler ieee80211_handler[] =
{
	(iw_handler) NULL,				/* SIOCSIWCOMMIT */
	(iw_handler) cfg80211_wext_giwname,		/* SIOCGIWNAME */
	(iw_handler) NULL,				/* SIOCSIWNWID */
	(iw_handler) NULL,				/* SIOCGIWNWID */
	(iw_handler) cfg80211_wext_siwfreq,		/* SIOCSIWFREQ */
	(iw_handler) cfg80211_wext_giwfreq,		/* SIOCGIWFREQ */
	(iw_handler) cfg80211_wext_siwmode,		/* SIOCSIWMODE */
	(iw_handler) cfg80211_wext_giwmode,		/* SIOCGIWMODE */
	(iw_handler) NULL,				/* SIOCSIWSENS */
	(iw_handler) NULL,				/* SIOCGIWSENS */
	(iw_handler) NULL /* not used */,		/* SIOCSIWRANGE */
	(iw_handler) cfg80211_wext_giwrange,		/* SIOCGIWRANGE */
	(iw_handler) NULL /* not used */,		/* SIOCSIWPRIV */
	(iw_handler) NULL /* kernel code */,		/* SIOCGIWPRIV */
	(iw_handler) NULL /* not used */,		/* SIOCSIWSTATS */
	(iw_handler) NULL /* kernel code */,		/* SIOCGIWSTATS */
	(iw_handler) NULL,				/* SIOCSIWSPY */
	(iw_handler) NULL,				/* SIOCGIWSPY */
	(iw_handler) NULL,				/* SIOCSIWTHRSPY */
	(iw_handler) NULL,				/* SIOCGIWTHRSPY */
	(iw_handler) cfg80211_wext_siwap,		/* SIOCSIWAP */
	(iw_handler) cfg80211_wext_giwap,		/* SIOCGIWAP */
	(iw_handler) cfg80211_wext_siwmlme,		/* SIOCSIWMLME */
	(iw_handler) NULL,				/* SIOCGIWAPLIST */
	(iw_handler) cfg80211_wext_siwscan,		/* SIOCSIWSCAN */
	(iw_handler) cfg80211_wext_giwscan,		/* SIOCGIWSCAN */
	(iw_handler) cfg80211_wext_siwessid,		/* SIOCSIWESSID */
	(iw_handler) cfg80211_wext_giwessid,		/* SIOCGIWESSID */
	(iw_handler) NULL,				/* SIOCSIWNICKN */
	(iw_handler) NULL,				/* SIOCGIWNICKN */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) cfg80211_wext_siwrate,		/* SIOCSIWRATE */
	(iw_handler) cfg80211_wext_giwrate,		/* SIOCGIWRATE */
	(iw_handler) cfg80211_wext_siwrts,		/* SIOCSIWRTS */
	(iw_handler) cfg80211_wext_giwrts,		/* SIOCGIWRTS */
	(iw_handler) cfg80211_wext_siwfrag,		/* SIOCSIWFRAG */
	(iw_handler) cfg80211_wext_giwfrag,		/* SIOCGIWFRAG */
	(iw_handler) cfg80211_wext_siwtxpower,		/* SIOCSIWTXPOW */
	(iw_handler) cfg80211_wext_giwtxpower,		/* SIOCGIWTXPOW */
	(iw_handler) cfg80211_wext_siwretry,		/* SIOCSIWRETRY */
	(iw_handler) cfg80211_wext_giwretry,		/* SIOCGIWRETRY */
	(iw_handler) cfg80211_wext_siwencode,		/* SIOCSIWENCODE */
	(iw_handler) cfg80211_wext_giwencode,		/* SIOCGIWENCODE */
	(iw_handler) cfg80211_wext_siwpower,		/* SIOCSIWPOWER */
	(iw_handler) cfg80211_wext_giwpower,		/* SIOCGIWPOWER */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) cfg80211_wext_siwgenie,		/* SIOCSIWGENIE */
	(iw_handler) NULL,				/* SIOCGIWGENIE */
	(iw_handler) cfg80211_wext_siwauth,		/* SIOCSIWAUTH */
	(iw_handler) cfg80211_wext_giwauth,		/* SIOCGIWAUTH */
	(iw_handler) cfg80211_wext_siwencodeext,	/* SIOCSIWENCODEEXT */
	(iw_handler) NULL,				/* SIOCGIWENCODEEXT */
	(iw_handler) NULL,				/* SIOCSIWPMKSA */
	(iw_handler) NULL,				/* -- hole -- */
};

const struct iw_handler_def ieee80211_iw_handler_def =
{
	.num_standard	= ARRAY_SIZE(ieee80211_handler),
	.standard	= (iw_handler *) ieee80211_handler,
	.get_wireless_stats = cfg80211_wireless_stats,
};
