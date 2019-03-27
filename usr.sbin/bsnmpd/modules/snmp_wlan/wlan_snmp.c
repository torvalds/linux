/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Shteryana Sotirova Shopova under
 * sponsorship from the FreeBSD Foundation.
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

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_mib.h>
#include <net/if_types.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <bsnmp/snmpmod.h>
#include <bsnmp/snmp_mibII.h>

#define	SNMPTREE_TYPES
#include "wlan_tree.h"
#include "wlan_snmp.h"
#include "wlan_oid.h"

static struct lmodule *wlan_module;

/* For the registration. */
static const struct asn_oid oid_wlan = OIDX_begemotWlan;
/* The registration. */
static uint reg_wlan;

/* Periodic timer for polling the module's data. */
static void *wlan_data_timer;

/*
 * Poll data from kernel every 15 minutes unless explicitly requested by an
 * SNMP client.
 * XXX: make that configurable.
 */
static int wlan_poll_ticks = (15 * 60) * 100;

/* The age of each table. */
#define	WLAN_LIST_MAXAGE	5

static time_t wlan_iflist_age;
static time_t wlan_peerlist_age;
static time_t wlan_chanlist_age;
static time_t wlan_roamlist_age;
static time_t wlan_tx_paramlist_age;
static time_t wlan_scanlist_age;
static time_t wlan_maclist_age;
static time_t wlan_mrlist_age;

/*
 * The list of all virtual wireless interfaces - sorted by name.
 */
SLIST_HEAD(wlan_ifaces, wlan_iface);
static struct wlan_ifaces wlan_ifaces = SLIST_HEAD_INITIALIZER(wlan_ifaces);

static struct wlan_config wlan_config;

/* Forward declarations */
static int	bits_get(struct snmp_value *, const u_char *, ssize_t);

static int	wlan_add_wif(struct wlan_iface *);
static void	wlan_delete_wif(struct wlan_iface *);
static int	wlan_attach_newif(struct mibif *);
static int	wlan_iface_create(struct wlan_iface *);
static int	wlan_iface_destroy(struct wlan_iface *);
static struct wlan_iface *	wlan_new_wif(char *);

static void	wlan_free_interface(struct wlan_iface *);
static void	wlan_free_iflist(void);
static void	wlan_free_peerlist(struct wlan_iface *);
static void	wlan_scan_free_results(struct wlan_iface *);
static void	wlan_mac_free_maclist(struct wlan_iface *);
static void	wlan_mesh_free_routes(struct wlan_iface *);

static int	wlan_update_interface(struct wlan_iface *);
static void	wlan_update_interface_list(void);
static void	wlan_update_peers(void);
static void	wlan_update_channels(void);
static void	wlan_update_roam_params(void);
static void	wlan_update_tx_params(void);
static void	wlan_scan_update_results(void);
static void	wlan_mac_update_aclmacs(void);
static void	wlan_mesh_update_routes(void);

static struct wlan_iface *	wlan_find_interface(const char *);
static struct wlan_peer *	wlan_find_peer(struct wlan_iface *, uint8_t *);
static struct ieee80211_channel*	wlan_find_channel(struct wlan_iface *,
    uint32_t);
static struct wlan_scan_result *	wlan_scan_find_result(struct wlan_iface *,
    uint8_t *, uint8_t *);
static struct wlan_mac_mac *		wlan_mac_find_mac(struct wlan_iface *,
    uint8_t *);
static struct wlan_mesh_route *		wlan_mesh_find_route(struct wlan_iface *,
    uint8_t *);

static struct wlan_iface *	wlan_first_interface(void);
static struct wlan_iface *	wlan_next_interface(struct wlan_iface *);
static struct wlan_iface *	wlan_mesh_first_interface(void);
static struct wlan_iface *	wlan_mesh_next_interface(struct wlan_iface *);

static struct wlan_iface *	wlan_get_interface(const struct asn_oid *, uint);
static struct wlan_iface *	wlan_get_snmp_interface(const struct asn_oid *,
    uint);
static struct wlan_peer *	wlan_get_peer(const struct asn_oid *, uint,
    struct wlan_iface **);
static struct ieee80211_channel *wlan_get_channel(const struct asn_oid *, uint,
    struct wlan_iface **);
static struct ieee80211_roamparam *wlan_get_roam_param(const struct asn_oid *,
    uint, struct wlan_iface **);
static struct ieee80211_txparam *wlan_get_tx_param(const struct asn_oid *,
    uint, struct wlan_iface **, uint32_t *);
static struct wlan_scan_result *wlan_get_scanr(const struct asn_oid *, uint,
    struct wlan_iface **);
static struct wlan_mac_mac *	wlan_get_acl_mac(const struct asn_oid *,
    uint, struct wlan_iface **);
static struct wlan_iface *	wlan_mesh_get_iface(const struct asn_oid *, uint);
static struct wlan_peer *	wlan_mesh_get_peer(const struct asn_oid *, uint,
    struct wlan_iface **);
static struct wlan_mesh_route *	wlan_mesh_get_route(const struct asn_oid *,
    uint, struct wlan_iface **);

static struct wlan_iface *	wlan_get_next_interface(const struct asn_oid *,
    uint);
static struct wlan_iface *	wlan_get_next_snmp_interface(const struct
    asn_oid *, uint);
static struct wlan_peer *	wlan_get_next_peer(const struct asn_oid *, uint,
    struct wlan_iface **);
static struct ieee80211_channel *wlan_get_next_channel(const struct asn_oid *,
    uint, struct wlan_iface **);
static struct ieee80211_roamparam *wlan_get_next_roam_param(const struct
    asn_oid *, uint sub, struct wlan_iface **, uint32_t *);
static struct ieee80211_txparam *wlan_get_next_tx_param(const struct asn_oid *,
    uint, struct wlan_iface **, uint32_t *);
static struct wlan_scan_result *wlan_get_next_scanr(const struct asn_oid *,
    uint , struct wlan_iface **);
static struct wlan_mac_mac *	wlan_get_next_acl_mac(const struct asn_oid *,
    uint, struct wlan_iface **);
static struct wlan_iface *	wlan_mesh_get_next_iface(const struct asn_oid *,
    uint);
static struct wlan_peer *	wlan_mesh_get_next_peer(const struct asn_oid *,
    uint, struct wlan_iface **);
static struct wlan_mesh_route *	wlan_mesh_get_next_route(const struct asn_oid *,
    uint sub, struct wlan_iface **);

static uint8_t *wlan_get_ifname(const struct asn_oid *, uint, uint8_t *);
static int	wlan_mac_index_decode(const struct asn_oid *, uint, char *,
    uint8_t *);
static int	wlan_channel_index_decode(const struct asn_oid *, uint,
    char *, uint32_t *);
static int	wlan_phy_index_decode(const struct asn_oid *, uint, char *,
    uint32_t *);
static int wlan_scanr_index_decode(const struct asn_oid *oid, uint sub,
    char *wname, uint8_t *ssid, uint8_t *bssid);

static void	wlan_append_ifindex(struct asn_oid *, uint,
    const struct wlan_iface *);
static void	wlan_append_mac_index(struct asn_oid *, uint, char *, uint8_t *);
static void	wlan_append_channel_index(struct asn_oid *, uint,
    const struct wlan_iface *, const struct ieee80211_channel *);
static void	wlan_append_phy_index(struct asn_oid *, uint, char *, uint32_t);
static void	wlan_append_scanr_index(struct asn_oid *, uint, char *,
    uint8_t *, uint8_t *);

static int	wlan_acl_mac_set_status(struct snmp_context *,
    struct snmp_value *, uint);
static int	wlan_mesh_route_set_status(struct snmp_context *,
    struct snmp_value *, uint);

static int32_t	wlan_get_channel_type(struct ieee80211_channel *);
static int	wlan_scan_compare_result(struct wlan_scan_result *,
    struct wlan_scan_result *);
static int	wlan_mac_delete_mac(struct wlan_iface *, struct wlan_mac_mac *);
static int	wlan_mesh_delete_route(struct wlan_iface *,
    struct wlan_mesh_route *);

/*
 * The module's GET/SET data hooks per each table or group of objects as
 * required by bsnmpd(1).
 */
int
op_wlan_iface(struct snmp_context *ctx, struct snmp_value *val, uint32_t sub,
    uint32_t iidx __unused, enum snmp_op op)
{
	int rc;
	char wname[IFNAMSIZ];
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_get_snmp_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((wif = wlan_get_next_snmp_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		break;

	case SNMP_OP_SET:
		if ((wif = wlan_get_snmp_interface(&val->var, sub)) == NULL) {
			if (val->var.subs[sub - 1] != LEAF_wlanIfaceName)
				return (SNMP_ERR_NOSUCHNAME);
			if (wlan_get_ifname(&val->var, sub, wname) == NULL)
				return (SNMP_ERR_INCONS_VALUE);
			if ((wif = wlan_new_wif(wname)) == NULL)
				return (SNMP_ERR_GENERR);
			wif->internal = 1;
		}
		if (wif->status == RowStatus_active &&
		    val->var.subs[sub - 1] != LEAF_wlanIfaceStatus &&
		    val->var.subs[sub - 1] != LEAF_wlanIfaceState)
			return (SNMP_ERR_INCONS_VALUE);

		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanIfaceIndex:
			return (SNMP_ERR_NOT_WRITEABLE);

		case LEAF_wlanIfaceName:
			if (val->v.octetstring.len >= IFNAMSIZ)
				return (SNMP_ERR_INCONS_VALUE);
			if ((ctx->scratch->ptr1 = malloc(IFNAMSIZ)) == NULL)
				return (SNMP_ERR_GENERR);
			strlcpy(ctx->scratch->ptr1, wif->wname, IFNAMSIZ);
			memcpy(wif->wname, val->v.octetstring.octets,
			    val->v.octetstring.len);
			wif->wname[val->v.octetstring.len] = '\0';
			return (SNMP_ERR_NOERROR);

		case LEAF_wlanParentIfName:
			if (val->v.octetstring.len >= IFNAMSIZ)
				return (SNMP_ERR_INCONS_VALUE);
			if ((ctx->scratch->ptr1 = malloc(IFNAMSIZ)) == NULL)
				return (SNMP_ERR_GENERR);
			strlcpy(ctx->scratch->ptr1, wif->pname, IFNAMSIZ);
			memcpy(wif->pname, val->v.octetstring.octets,
			    val->v.octetstring.len);
			wif->pname[val->v.octetstring.len] = '\0';
			return (SNMP_ERR_NOERROR);

		case LEAF_wlanIfaceOperatingMode:
			ctx->scratch->int1 = wif->mode;
			wif->mode = val->v.integer;
			return (SNMP_ERR_NOERROR);

		case LEAF_wlanIfaceFlags:
			if (val->v.octetstring.len > sizeof(wif->flags))
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->ptr1 = malloc(sizeof(wif->flags));
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			memcpy(ctx->scratch->ptr1, (uint8_t *)&wif->flags,
			    sizeof(wif->flags));
			memcpy((uint8_t *)&wif->flags, val->v.octetstring.octets,
			    sizeof(wif->flags));
			return (SNMP_ERR_NOERROR);

		case LEAF_wlanIfaceBssid:
			if (val->v.octetstring.len != IEEE80211_ADDR_LEN)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->ptr1 = malloc(IEEE80211_ADDR_LEN);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			memcpy(ctx->scratch->ptr1, wif->dbssid,
			    IEEE80211_ADDR_LEN);
			memcpy(wif->dbssid, val->v.octetstring.octets,
			    IEEE80211_ADDR_LEN);
			return (SNMP_ERR_NOERROR);

		case LEAF_wlanIfaceLocalAddress:
			if (val->v.octetstring.len != IEEE80211_ADDR_LEN)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->ptr1 = malloc(IEEE80211_ADDR_LEN);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			memcpy(ctx->scratch->ptr1, wif->dlmac,
			    IEEE80211_ADDR_LEN);
			memcpy(wif->dlmac, val->v.octetstring.octets,
			    IEEE80211_ADDR_LEN);
			return (SNMP_ERR_NOERROR);

		case LEAF_wlanIfaceStatus:
			ctx->scratch->int1 = wif->status;
			wif->status = val->v.integer;
			if (wif->status == RowStatus_active) {
				rc = wlan_iface_create(wif); /* XXX */
				if (rc != SNMP_ERR_NOERROR) {
					wif->status = ctx->scratch->int1;
					return (rc);
				}
			} else if (wif->status == RowStatus_destroy)
				return (wlan_iface_destroy(wif));
			else
				wif->status = RowStatus_notReady;
			return (SNMP_ERR_NOERROR);

		case LEAF_wlanIfaceState:
			ctx->scratch->int1 = wif->state;
			wif->state = val->v.integer;
			if (wif->status == RowStatus_active)
				if (wlan_config_state(wif, 1) < 0)
					return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);
		}
		abort();

	case SNMP_OP_ROLLBACK:
		if ((wif = wlan_get_snmp_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanIfaceName:
			strlcpy(wif->wname, ctx->scratch->ptr1, IFNAMSIZ);
			free(ctx->scratch->ptr1);
			break;

		case LEAF_wlanParentIfName:
			strlcpy(wif->pname, ctx->scratch->ptr1, IFNAMSIZ);
			free(ctx->scratch->ptr1);
			break;

		case LEAF_wlanIfaceOperatingMode:
			wif->mode = ctx->scratch->int1;
			break;

		case LEAF_wlanIfaceFlags:
			memcpy((uint8_t *)&wif->flags, ctx->scratch->ptr1,
			    sizeof(wif->flags));
			free(ctx->scratch->ptr1);
			break;

		case LEAF_wlanIfaceBssid:
			memcpy(wif->dbssid, ctx->scratch->ptr1,
			    IEEE80211_ADDR_LEN);
			free(ctx->scratch->ptr1);
			break;

		case LEAF_wlanIfaceLocalAddress:
			memcpy(wif->dlmac, ctx->scratch->ptr1,
			    IEEE80211_ADDR_LEN);
			free(ctx->scratch->ptr1);
			break;

		case LEAF_wlanIfaceStatus:
			wif->status = ctx->scratch->int1;
			if (ctx->scratch->int1 == RowStatus_active)
				return (SNMP_ERR_GENERR); /* XXX: FIXME */
			else if (wif->internal != 0)
				return (wlan_iface_destroy(wif));
			break;

		case LEAF_wlanIfaceState:
			wif->state = ctx->scratch->int1;
			if (wif->status == RowStatus_active)
				if (wlan_config_state(wif, 1) < 0)
					return (SNMP_ERR_GENERR);
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanIfaceName:
		case LEAF_wlanParentIfName:
		case LEAF_wlanIfaceFlags:
		case LEAF_wlanIfaceBssid:
		case LEAF_wlanIfaceLocalAddress:
			free(ctx->scratch->ptr1);
			/* FALLTHROUGH */
		default:
			return (SNMP_ERR_NOERROR);
		}
	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanIfaceIndex:
		val->v.integer = wif->index;
		return (SNMP_ERR_NOERROR);
	case LEAF_wlanIfaceName:
		return (string_get(val, wif->wname, -1));
	case LEAF_wlanParentIfName:
		return (string_get(val, wif->pname, -1));
	case LEAF_wlanIfaceOperatingMode:
		val->v.integer = wif->mode;
		return (SNMP_ERR_NOERROR);
	case LEAF_wlanIfaceFlags:
		return (bits_get(val, (uint8_t *)&wif->flags,
		    sizeof(wif->flags)));
	case LEAF_wlanIfaceBssid:
		return (string_get(val, wif->dbssid, IEEE80211_ADDR_LEN));
	case LEAF_wlanIfaceLocalAddress:
		return (string_get(val, wif->dlmac, IEEE80211_ADDR_LEN));
	case LEAF_wlanIfaceStatus:
		val->v.integer = wif->status;
		return (SNMP_ERR_NOERROR);
	case LEAF_wlanIfaceState:
		val->v.integer = wif->state;
		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
op_wlan_if_parent(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;
	case SNMP_OP_GETNEXT:
		if ((wif = wlan_get_next_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		break;
	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);
	case SNMP_OP_COMMIT:
		/* FALLTHROUGH */
	case SNMP_OP_ROLLBACK:
		/* FALLTHROUGH */
	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanIfParentDriverCapabilities:
		return (bits_get(val, (uint8_t *)&wif->drivercaps,
		    sizeof(wif->drivercaps)));
	case LEAF_wlanIfParentCryptoCapabilities:
		return (bits_get(val, (uint8_t *)&wif->cryptocaps,
		    sizeof(wif->cryptocaps)));
	case LEAF_wlanIfParentHTCapabilities:
		return (bits_get(val, (uint8_t *)&wif->htcaps,
		    sizeof(wif->htcaps)));
	}

	abort();
}

int
op_wlan_iface_config(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	int intval, vlen, rc;
	char *strval;
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get_config;

	case SNMP_OP_GETNEXT:
		if ((wif = wlan_get_next_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		goto get_config;

	case SNMP_OP_SET:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);

		intval = val->v.integer;
		strval = NULL;
		vlen = 0;

		/* Simple sanity checks & save old data. */
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanIfaceCountryCode:
			if (val->v.octetstring.len != WLAN_COUNTRY_CODE_SIZE)
				return (SNMP_ERR_INCONS_VALUE);
			break;
		case LEAF_wlanIfaceDesiredSsid:
			if (val->v.octetstring.len > IEEE80211_NWID_LEN)
				return (SNMP_ERR_INCONS_VALUE);
			break;
		case LEAF_wlanIfaceDesiredBssid:
			if (val->v.octetstring.len != IEEE80211_ADDR_LEN)
				return (SNMP_ERR_INCONS_VALUE);
			break;
		case LEAF_wlanIfacePacketBurst:
			ctx->scratch->int1 = wif->packet_burst;
			break;
		case LEAF_wlanIfaceRegDomain:
			ctx->scratch->int1 = wif->reg_domain;
			break;
		case LEAF_wlanIfaceDesiredChannel:
			ctx->scratch->int1 = wif->desired_channel;
			break;
		case LEAF_wlanIfaceDynamicFreqSelection:
			ctx->scratch->int1 = wif->dyn_frequency;
			break;
		case LEAF_wlanIfaceFastFrames:
			ctx->scratch->int1 = wif->fast_frames;
			break;
		case LEAF_wlanIfaceDturbo:
			ctx->scratch->int1 = wif->dturbo;
			break;
		case LEAF_wlanIfaceTxPower:
			ctx->scratch->int1 = wif->tx_power;
			break;
		case LEAF_wlanIfaceFragmentThreshold:
			ctx->scratch->int1 = wif->frag_threshold;
			break;
		case LEAF_wlanIfaceRTSThreshold:
			ctx->scratch->int1 = wif->rts_threshold;
			break;
		case LEAF_wlanIfaceWlanPrivacySubscribe:
			ctx->scratch->int1 = wif->priv_subscribe;
			break;
		case LEAF_wlanIfaceBgScan:
			ctx->scratch->int1 = wif->bg_scan;
			break;
		case LEAF_wlanIfaceBgScanIdle:
			ctx->scratch->int1 = wif->bg_scan_idle;
			break;
		case LEAF_wlanIfaceBgScanInterval:
			ctx->scratch->int1 = wif->bg_scan_interval;
			break;
		case LEAF_wlanIfaceBeaconMissedThreshold:
			ctx->scratch->int1 = wif->beacons_missed;
			break;
		case LEAF_wlanIfaceRoamingMode:
			ctx->scratch->int1 = wif->roam_mode;
			break;
		case LEAF_wlanIfaceDot11d:
			ctx->scratch->int1 = wif->dot11d;
			break;
		case LEAF_wlanIfaceDot11h:
			ctx->scratch->int1 = wif->dot11h;
			break;
		case LEAF_wlanIfaceDynamicWds:
			ctx->scratch->int1 = wif->dynamic_wds;
			break;
		case LEAF_wlanIfacePowerSave:
			ctx->scratch->int1 = wif->power_save;
			break;
		case LEAF_wlanIfaceApBridge:
			ctx->scratch->int1 = wif->ap_bridge;
			break;
		case LEAF_wlanIfaceBeaconInterval:
			ctx->scratch->int1 = wif->beacon_interval;
			break;
		case LEAF_wlanIfaceDtimPeriod:
			ctx->scratch->int1 = wif->dtim_period;
			break;
		case LEAF_wlanIfaceHideSsid:
			ctx->scratch->int1 = wif->hide_ssid;
			break;
		case LEAF_wlanIfaceInactivityProccess:
			ctx->scratch->int1 = wif->inact_process;
			break;
		case LEAF_wlanIfaceDot11gProtMode:
			ctx->scratch->int1 = wif->do11g_protect;
			break;
		case LEAF_wlanIfaceDot11gPureMode:
			ctx->scratch->int1 = wif->dot11g_pure;
			break;
		case LEAF_wlanIfaceDot11nPureMode:
			ctx->scratch->int1 = wif->dot11n_pure;
			break;
		case LEAF_wlanIfaceDot11nAmpdu:
			ctx->scratch->int1 = wif->ampdu;
			break;
		case LEAF_wlanIfaceDot11nAmpduDensity:
			ctx->scratch->int1 = wif->ampdu_density;
			break;
		case LEAF_wlanIfaceDot11nAmpduLimit:
			ctx->scratch->int1 = wif->ampdu_limit;
			break;
		case LEAF_wlanIfaceDot11nAmsdu:
			ctx->scratch->int1 = wif->amsdu;
			break;
		case LEAF_wlanIfaceDot11nAmsduLimit:
			ctx->scratch->int1 = wif->amsdu_limit;
			break;
		case LEAF_wlanIfaceDot11nHighThroughput:
			ctx->scratch->int1 = wif->ht_enabled;
			break;
		case LEAF_wlanIfaceDot11nHTCompatible:
			ctx->scratch->int1 = wif->ht_compatible;
			break;
		case LEAF_wlanIfaceDot11nHTProtMode:
			ctx->scratch->int1 = wif->ht_prot_mode;
			break;
		case LEAF_wlanIfaceDot11nRIFS:
			ctx->scratch->int1 = wif->rifs;
			break;
		case LEAF_wlanIfaceDot11nShortGI:
			ctx->scratch->int1 = wif->short_gi;
			break;
		case LEAF_wlanIfaceDot11nSMPSMode:
			ctx->scratch->int1 = wif->smps_mode;
			break;
		case LEAF_wlanIfaceTdmaSlot:
			ctx->scratch->int1 = wif->tdma_slot;
			break;
		case LEAF_wlanIfaceTdmaSlotCount:
			ctx->scratch->int1 = wif->tdma_slot_count;
			break;
		case LEAF_wlanIfaceTdmaSlotLength:
			ctx->scratch->int1 = wif->tdma_slot_length;
			break;
		case LEAF_wlanIfaceTdmaBeaconInterval:
			ctx->scratch->int1 = wif->tdma_binterval;
			break;
		default:
			abort();
		}

		if (val->syntax != SNMP_SYNTAX_OCTETSTRING)
			goto set_config;

		ctx->scratch->int1 = val->v.octetstring.len;
		ctx->scratch->ptr1 = malloc(val->v.octetstring.len + 1);
		if (ctx->scratch->ptr1 == NULL)
			return (SNMP_ERR_GENERR); /* XXX */
		if (val->var.subs[sub - 1] == LEAF_wlanIfaceDesiredSsid)
			strlcpy(ctx->scratch->ptr1, val->v.octetstring.octets,
			    val->v.octetstring.len + 1);
		else
			memcpy(ctx->scratch->ptr1, val->v.octetstring.octets,
			    val->v.octetstring.len);
		strval = val->v.octetstring.octets;
		vlen = val->v.octetstring.len;
		goto set_config;

	case SNMP_OP_ROLLBACK:
		intval = ctx->scratch->int1;
		strval = NULL;
		vlen = 0;

		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanIfaceCountryCode:
		case LEAF_wlanIfaceDesiredSsid:
		case LEAF_wlanIfaceDesiredBssid:
			strval = ctx->scratch->ptr1;
			vlen = ctx->scratch->int1;
			break;
		default:
			break;
		}
		goto set_config;

	case SNMP_OP_COMMIT:
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanIfaceCountryCode:
		case LEAF_wlanIfaceDesiredSsid:
		case LEAF_wlanIfaceDesiredBssid:
			free(ctx->scratch->ptr1);
			/* FALLTHROUGH */
		default:
			return (SNMP_ERR_NOERROR);
		}
	}
	abort();

get_config:

	if (wlan_config_get_ioctl(wif, val->var.subs[sub - 1]) < 0)
		return (SNMP_ERR_GENERR);

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanIfacePacketBurst:
		val->v.integer = wif->packet_burst;
		break;
	case LEAF_wlanIfaceCountryCode:
		return (string_get(val, wif->country_code,
		    WLAN_COUNTRY_CODE_SIZE));
	case LEAF_wlanIfaceRegDomain:
		val->v.integer = wif->reg_domain;
		break;
	case LEAF_wlanIfaceDesiredSsid:
		return (string_get(val, wif->desired_ssid, -1));
	case LEAF_wlanIfaceDesiredChannel:
		val->v.integer = wif->desired_channel;
		break;
	case LEAF_wlanIfaceDynamicFreqSelection:
		val->v.integer = wif->dyn_frequency;
		break;
	case LEAF_wlanIfaceFastFrames:
		val->v.integer = wif->fast_frames;
		break;
	case LEAF_wlanIfaceDturbo:
		val->v.integer = wif->dturbo;
		break;
	case LEAF_wlanIfaceTxPower:
		val->v.integer = wif->tx_power;
		break;
	case LEAF_wlanIfaceFragmentThreshold:
		val->v.integer = wif->frag_threshold;
		break;
	case LEAF_wlanIfaceRTSThreshold:
		val->v.integer = wif->rts_threshold;
		break;
	case LEAF_wlanIfaceWlanPrivacySubscribe:
		val->v.integer = wif->priv_subscribe;
		break;
	case LEAF_wlanIfaceBgScan:
		val->v.integer = wif->bg_scan;
		break;
	case LEAF_wlanIfaceBgScanIdle:
		val->v.integer = wif->bg_scan_idle;
		break;
	case LEAF_wlanIfaceBgScanInterval:
		val->v.integer = wif->bg_scan_interval;
		break;
	case LEAF_wlanIfaceBeaconMissedThreshold:
		val->v.integer = wif->beacons_missed;
		break;
	case LEAF_wlanIfaceDesiredBssid:
		return (string_get(val, wif->desired_bssid,
		    IEEE80211_ADDR_LEN));
	case LEAF_wlanIfaceRoamingMode:
		val->v.integer = wif->roam_mode;
		break;
	case LEAF_wlanIfaceDot11d:
		val->v.integer = wif->dot11d;
		break;
	case LEAF_wlanIfaceDot11h:
		val->v.integer = wif->dot11h;
		break;
	case LEAF_wlanIfaceDynamicWds:
		val->v.integer = wif->dynamic_wds;
		break;
	case LEAF_wlanIfacePowerSave:
		val->v.integer = wif->power_save;
		break;
	case LEAF_wlanIfaceApBridge:
		val->v.integer = wif->ap_bridge;
		break;
	case LEAF_wlanIfaceBeaconInterval:
		val->v.integer = wif->beacon_interval;
		break;
	case LEAF_wlanIfaceDtimPeriod:
		val->v.integer = wif->dtim_period;
		break;
	case LEAF_wlanIfaceHideSsid:
		val->v.integer = wif->hide_ssid;
		break;
	case LEAF_wlanIfaceInactivityProccess:
		val->v.integer = wif->inact_process;
		break;
	case LEAF_wlanIfaceDot11gProtMode:
		val->v.integer = wif->do11g_protect;
		break;
	case LEAF_wlanIfaceDot11gPureMode:
		val->v.integer = wif->dot11g_pure;
		break;
	case LEAF_wlanIfaceDot11nPureMode:
		val->v.integer = wif->dot11n_pure;
		break;
	case LEAF_wlanIfaceDot11nAmpdu:
		val->v.integer = wif->ampdu;
		break;
	case LEAF_wlanIfaceDot11nAmpduDensity:
		val->v.integer = wif->ampdu_density;
		break;
	case LEAF_wlanIfaceDot11nAmpduLimit:
		val->v.integer = wif->ampdu_limit;
		break;
	case LEAF_wlanIfaceDot11nAmsdu:
		val->v.integer = wif->amsdu;
		break;
	case LEAF_wlanIfaceDot11nAmsduLimit:
		val->v.integer = wif->amsdu_limit;
		break;
	case LEAF_wlanIfaceDot11nHighThroughput:
		val->v.integer = wif->ht_enabled;
		break;
	case LEAF_wlanIfaceDot11nHTCompatible:
		val->v.integer = wif->ht_compatible;
		break;
	case LEAF_wlanIfaceDot11nHTProtMode:
		val->v.integer = wif->ht_prot_mode;
		break;
	case LEAF_wlanIfaceDot11nRIFS:
		val->v.integer = wif->rifs;
		break;
	case LEAF_wlanIfaceDot11nShortGI:
		val->v.integer = wif->short_gi;
		break;
	case LEAF_wlanIfaceDot11nSMPSMode:
		val->v.integer = wif->smps_mode;
		break;
	case LEAF_wlanIfaceTdmaSlot:
		val->v.integer = wif->tdma_slot;
		break;
	case LEAF_wlanIfaceTdmaSlotCount:
		val->v.integer = wif->tdma_slot_count;
		break;
	case LEAF_wlanIfaceTdmaSlotLength:
		val->v.integer = wif->tdma_slot_length;
		break;
	case LEAF_wlanIfaceTdmaBeaconInterval:
		val->v.integer = wif->tdma_binterval;
		break;
	}

	return (SNMP_ERR_NOERROR);

set_config:
	rc = wlan_config_set_ioctl(wif, val->var.subs[sub - 1], intval,
	    strval, vlen);

	if (op == SNMP_OP_ROLLBACK) {
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanIfaceCountryCode:
		case LEAF_wlanIfaceDesiredSsid:
		case LEAF_wlanIfaceDesiredBssid:
			free(ctx->scratch->ptr1);
			/* FALLTHROUGH */
		default:
			break;
		}
	}

	if (rc < 0)
		return (SNMP_ERR_GENERR);

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_if_peer(struct snmp_context *ctx, struct snmp_value *val, uint32_t sub,
    uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_peer *wip;
	struct wlan_iface *wif;

	wlan_update_interface_list();
	wlan_update_peers();

	switch (op) {
	case SNMP_OP_GET:
		if ((wip = wlan_get_peer(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;
	case SNMP_OP_GETNEXT:
		if ((wip = wlan_get_next_peer(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_mac_index(&val->var, sub, wif->wname, wip->pmac);
		break;
	case SNMP_OP_SET:
		if ((wip = wlan_get_peer(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if (val->var.subs[sub - 1] != LEAF_wlanIfacePeerVlanTag)
			return (SNMP_ERR_GENERR);
		ctx->scratch->int1 = wip->vlan;
		if (wlan_peer_set_vlan(wif, wip, val->v.integer) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);
	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	case SNMP_OP_ROLLBACK:
		if ((wip = wlan_get_peer(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if (val->var.subs[sub - 1] != LEAF_wlanIfacePeerVlanTag)
			return (SNMP_ERR_GENERR);
		if (wlan_peer_set_vlan(wif, wip, ctx->scratch->int1) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);
	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanIfacePeerAddress:
		return (string_get(val, wip->pmac, IEEE80211_ADDR_LEN));
	case LEAF_wlanIfacePeerAssociationId:
		val->v.integer = wip->associd;
		break;
	case LEAF_wlanIfacePeerVlanTag:
		val->v.integer = wip->vlan;
		break;
	case LEAF_wlanIfacePeerFrequency:
		val->v.integer = wip->frequency;
		break;
	case LEAF_wlanIfacePeerCurrentTXRate:
		val->v.integer = wip->txrate;
		break;
	case LEAF_wlanIfacePeerRxSignalStrength:
		val->v.integer = wip->rssi;
		break;
	case LEAF_wlanIfacePeerIdleTimer:
		val->v.integer = wip->idle;
		break;
	case LEAF_wlanIfacePeerTxSequenceNo:
		val->v.integer = wip->txseqs;
		break;
	case LEAF_wlanIfacePeerRxSequenceNo:
		val->v.integer = wip->rxseqs;
		break;
	case LEAF_wlanIfacePeerTxPower:
		val->v.integer = wip->txpower;
		break;
	case LEAF_wlanIfacePeerCapabilities:
		return (bits_get(val, (uint8_t *)&wip->capinfo,
		    sizeof(wip->capinfo)));
	case LEAF_wlanIfacePeerFlags:
		return (bits_get(val, (uint8_t *)&wip->state,
		    sizeof(wip->state)));
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_channels(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	int32_t bits;
	struct ieee80211_channel *channel;
	struct wlan_iface *wif;

	wlan_update_interface_list();
	wlan_update_channels();

	switch (op) {
	case SNMP_OP_GET:
		if ((channel = wlan_get_channel(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;
	case SNMP_OP_GETNEXT:
		channel = wlan_get_next_channel(&val->var, sub, &wif);
		if (channel == NULL || wif == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_channel_index(&val->var, sub, wif, channel);
		break;
	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);
	case SNMP_OP_COMMIT:
		/* FALLTHROUGH */
	case SNMP_OP_ROLLBACK:
		/* FALLTHROUGH */
	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanIfaceChannelIeeeId:
		val->v.integer = channel->ic_ieee;
		break;
	case LEAF_wlanIfaceChannelType:
		val->v.integer = wlan_get_channel_type(channel);
		break;
	case LEAF_wlanIfaceChannelFlags:
		bits = wlan_channel_flags_to_snmp(channel->ic_flags);
		return (bits_get(val, (uint8_t *)&bits, sizeof(bits)));
	case LEAF_wlanIfaceChannelFrequency:
		val->v.integer = channel->ic_freq;
		break;
	case LEAF_wlanIfaceChannelMaxRegPower:
		val->v.integer = channel->ic_maxregpower;
		break;
	case LEAF_wlanIfaceChannelMaxTxPower:
		val->v.integer = channel->ic_maxpower;
		break;
	case LEAF_wlanIfaceChannelMinTxPower:
		val->v.integer = channel->ic_minpower;
		break;
	case LEAF_wlanIfaceChannelState:
		bits = wlan_channel_state_to_snmp(channel->ic_state);
		return (bits_get(val, (uint8_t *)&bits, sizeof(bits)));
	case LEAF_wlanIfaceChannelHTExtension:
		val->v.integer = channel->ic_extieee;
		break;
	case LEAF_wlanIfaceChannelMaxAntennaGain:
		val->v.integer = channel->ic_maxantgain;
		break;
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_roam_params(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	uint32_t phy;
	struct ieee80211_roamparam *rparam;
	struct wlan_iface *wif;

	wlan_update_interface_list();
	wlan_update_roam_params();

	switch (op) {
	case SNMP_OP_GET:
		rparam = wlan_get_roam_param(&val->var, sub, &wif);
		if (rparam == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;
	case SNMP_OP_GETNEXT:
		rparam = wlan_get_next_roam_param(&val->var, sub, &wif, &phy);
		if (rparam == NULL || wif == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_phy_index(&val->var, sub, wif->wname, phy);
		break;
	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);
	case SNMP_OP_COMMIT:
		/* FALLTHROUGH */
	case SNMP_OP_ROLLBACK:
		/* FALLTHROUGH */
	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanIfRoamRxSignalStrength:
		val->v.integer = rparam->rssi/2;
		break;
	case LEAF_wlanIfRoamTxRateThreshold:
		val->v.integer = rparam->rate/2;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_tx_params(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	uint32_t phy;
	struct ieee80211_txparam *txparam;
	struct wlan_iface *wif;

	wlan_update_interface_list();
	wlan_update_tx_params();

	switch (op) {
	case SNMP_OP_GET:
		txparam = wlan_get_tx_param(&val->var, sub, &wif, &phy);
		if (txparam == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get_txparams;

	case SNMP_OP_GETNEXT:
		txparam = wlan_get_next_tx_param(&val->var, sub, &wif, &phy);
		if (txparam == NULL || wif == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_phy_index(&val->var, sub, wif->wname, phy);
		goto get_txparams;

	case SNMP_OP_SET:
		txparam = wlan_get_tx_param(&val->var, sub, &wif, &phy);
		if (txparam == NULL || wif == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanIfTxUnicastRate:
			ctx->scratch->int1 = txparam->ucastrate;
			txparam->ucastrate = val->v.integer * 2;
			break;
		case LEAF_wlanIfTxMcastRate:
			ctx->scratch->int1 = txparam->mcastrate;
			txparam->mcastrate = val->v.integer * 2;
			break;
		case LEAF_wlanIfTxMgmtRate:
			ctx->scratch->int1 = txparam->mgmtrate;
			txparam->mgmtrate = val->v.integer * 2;
			break;
		case LEAF_wlanIfTxMaxRetryCount:
			ctx->scratch->int1 = txparam->maxretry;
			txparam->maxretry = val->v.integer;
			break;
		default:
			abort();
		}
		if (wlan_set_tx_params(wif, phy) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		txparam = wlan_get_tx_param(&val->var, sub, &wif, &phy);
		if (txparam == NULL || wif == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanIfTxUnicastRate:
			txparam->ucastrate = ctx->scratch->int1;
			break;
		case LEAF_wlanIfTxMcastRate:
			txparam->mcastrate = ctx->scratch->int1;
			break;
		case LEAF_wlanIfTxMgmtRate:
			txparam->mgmtrate = ctx->scratch->int1;
			break;
		case LEAF_wlanIfTxMaxRetryCount:
			txparam->maxretry = ctx->scratch->int1;
			break;
		default:
			abort();
		}
		if (wlan_set_tx_params(wif, phy) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);
	default:
		abort();
	}

get_txparams:
	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanIfTxUnicastRate:
		val->v.integer = txparam->ucastrate / 2;
		break;
	case LEAF_wlanIfTxMcastRate:
		val->v.integer = txparam->mcastrate / 2;
		break;
	case LEAF_wlanIfTxMgmtRate:
		val->v.integer = txparam->mgmtrate / 2;
		break;
	case LEAF_wlanIfTxMaxRetryCount:
		val->v.integer = txparam->maxretry;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_scan_config(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((wif = wlan_get_next_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		break;

	case SNMP_OP_SET:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if (wif->scan_status ==  wlanScanConfigStatus_running
		    && val->var.subs[sub - 1] != LEAF_wlanScanConfigStatus)
			return (SNMP_ERR_INCONS_VALUE);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanScanFlags:
			ctx->scratch->int1 = wif->scan_flags;
			wif->scan_flags = val->v.integer;
			break;
		case LEAF_wlanScanDuration:
			ctx->scratch->int1 = wif->scan_duration;
			wif->scan_duration = val->v.integer;
			break;
		case LEAF_wlanScanMinChannelDwellTime:
			ctx->scratch->int1 = wif->scan_mindwell;
			wif->scan_mindwell = val->v.integer;
			break;
		case LEAF_wlanScanMaxChannelDwellTime:
			ctx->scratch->int1 = wif->scan_maxdwell;
			wif->scan_maxdwell = val->v.integer;
			break;
		case LEAF_wlanScanConfigStatus:
			if (val->v.integer == wlanScanConfigStatus_running ||
			    val->v.integer == wlanScanConfigStatus_cancel) {
				ctx->scratch->int1 = wif->scan_status;
				wif->scan_status = val->v.integer;
				break;
			}
			return (SNMP_ERR_INCONS_VALUE);
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if (val->var.subs[sub - 1] == LEAF_wlanScanConfigStatus)
			if (wif->scan_status == wlanScanConfigStatus_running)
				(void)wlan_set_scan_config(wif); /* XXX */
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanScanFlags:
			wif->scan_flags = ctx->scratch->int1;
			break;
		case LEAF_wlanScanDuration:
			wif->scan_duration = ctx->scratch->int1;
			break;
		case LEAF_wlanScanMinChannelDwellTime:
			wif->scan_mindwell = ctx->scratch->int1;
			break;
		case LEAF_wlanScanMaxChannelDwellTime:
			wif->scan_maxdwell = ctx->scratch->int1;
			break;
		case LEAF_wlanScanConfigStatus:
			wif->scan_status = ctx->scratch->int1;
			break;
		}
		return (SNMP_ERR_NOERROR);
	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanScanFlags:
		val->v.integer = wif->scan_flags;
		break;
	case LEAF_wlanScanDuration:
		val->v.integer = wif->scan_duration;
		break;
	case LEAF_wlanScanMinChannelDwellTime:
		val->v.integer = wif->scan_mindwell;
		break;
	case LEAF_wlanScanMaxChannelDwellTime:
		val->v.integer = wif->scan_maxdwell;
		break;
	case LEAF_wlanScanConfigStatus:
		val->v.integer = wif->scan_status;
		break;
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_scan_results(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_scan_result *sr;
	struct wlan_iface *wif;

	wlan_update_interface_list();
	wlan_scan_update_results();

	switch (op) {
	case SNMP_OP_GET:
		if ((sr = wlan_get_scanr(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((sr = wlan_get_next_scanr(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_scanr_index(&val->var, sub, wif->wname, sr->ssid,
		    sr->bssid);
		break;

	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);
	case SNMP_OP_COMMIT:
		/* FALLTHROUGH */
	case SNMP_OP_ROLLBACK:
		/* FALLTHROUGH */
	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanScanResultID:
		return (string_get(val, sr->ssid, -1));
	case LEAF_wlanScanResultBssid:
		return (string_get(val, sr->bssid, IEEE80211_ADDR_LEN));
	case LEAF_wlanScanResultChannel:
		val->v.integer = sr->opchannel; /* XXX */
		break;
	case LEAF_wlanScanResultRate:
		val->v.integer = sr->rssi;
		break;
	case LEAF_wlanScanResultNoise:
		val->v.integer = sr->noise;
		break;
	case LEAF_wlanScanResultBeaconInterval:
		val->v.integer = sr->bintval;
		break;
	case LEAF_wlanScanResultCapabilities:
		return (bits_get(val, &sr->capinfo, sizeof(sr->capinfo)));
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_iface_stats(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;
	case SNMP_OP_GETNEXT:
		if ((wif = wlan_get_next_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		break;
	case SNMP_OP_SET:
		/* XXX: LEAF_wlanStatsReset */
		return (SNMP_ERR_NOT_WRITEABLE);
	case SNMP_OP_COMMIT:
		/* FALLTHROUGH */
	case SNMP_OP_ROLLBACK:
		/* FALLTHROUGH */
	default:
		abort();
	}

	if (wlan_get_stats(wif) < 0)
		return (SNMP_ERR_GENERR);

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanStatsRxBadVersion:
		val->v.uint32 = wif->stats.is_rx_badversion;
		break;
	case LEAF_wlanStatsRxTooShort:
		val->v.uint32 = wif->stats.is_rx_tooshort;
		break;
	case LEAF_wlanStatsRxWrongBssid:
		val->v.uint32 = wif->stats.is_rx_wrongbss;
		break;
	case LEAF_wlanStatsRxDiscardedDups:
		val->v.uint32 = wif->stats.is_rx_dup;
		break;
	case LEAF_wlanStatsRxWrongDir:
		val->v.uint32 = wif->stats.is_rx_wrongdir;
		break;
	case LEAF_wlanStatsRxDiscardMcastEcho:
		val->v.uint32 = wif->stats.is_rx_mcastecho;
		break;
	case LEAF_wlanStatsRxDiscardNoAssoc:
		val->v.uint32 = wif->stats.is_rx_notassoc;
		break;
	case LEAF_wlanStatsRxWepNoPrivacy:
		val->v.uint32 = wif->stats.is_rx_noprivacy;
		break;
	case LEAF_wlanStatsRxWepUnencrypted:
		val->v.uint32 = wif->stats.is_rx_unencrypted;
		break;
	case LEAF_wlanStatsRxWepFailed:
		val->v.uint32 = wif->stats.is_rx_wepfail;
		break;
	case LEAF_wlanStatsRxDecapsulationFailed:
		val->v.uint32 = wif->stats.is_rx_decap;
		break;
	case LEAF_wlanStatsRxDiscardMgmt:
		val->v.uint32 = wif->stats.is_rx_mgtdiscard;
		break;
	case LEAF_wlanStatsRxControl:
		val->v.uint32 = wif->stats.is_rx_ctl;
		break;
	case LEAF_wlanStatsRxBeacon:
		val->v.uint32 = wif->stats.is_rx_beacon;
		break;
	case LEAF_wlanStatsRxRateSetTooBig:
		val->v.uint32 = wif->stats.is_rx_rstoobig;
		break;
	case LEAF_wlanStatsRxElemMissing:
		val->v.uint32 = wif->stats.is_rx_elem_missing;
		break;
	case LEAF_wlanStatsRxElemTooBig:
		val->v.uint32 = wif->stats.is_rx_elem_toobig;
		break;
	case LEAF_wlanStatsRxElemTooSmall:
		val->v.uint32 = wif->stats.is_rx_elem_toosmall;
		break;
	case LEAF_wlanStatsRxElemUnknown:
		val->v.uint32 = wif->stats.is_rx_elem_unknown;
		break;
	case LEAF_wlanStatsRxChannelMismatch:
		val->v.uint32 = wif->stats.is_rx_chanmismatch;
		break;
	case LEAF_wlanStatsRxDropped:
		val->v.uint32 = wif->stats.is_rx_nodealloc;
		break;
	case LEAF_wlanStatsRxSsidMismatch:
		val->v.uint32 = wif->stats.is_rx_ssidmismatch;
		break;
	case LEAF_wlanStatsRxAuthNotSupported:
		val->v.uint32 = wif->stats.is_rx_auth_unsupported;
		break;
	case LEAF_wlanStatsRxAuthFailed:
		val->v.uint32 = wif->stats.is_rx_auth_fail;
		break;
	case LEAF_wlanStatsRxAuthCM:
		val->v.uint32 = wif->stats.is_rx_auth_countermeasures;
		break;
	case LEAF_wlanStatsRxAssocWrongBssid:
		val->v.uint32 = wif->stats.is_rx_assoc_bss;
		break;
	case LEAF_wlanStatsRxAssocNoAuth:
		val->v.uint32 = wif->stats.is_rx_assoc_notauth;
		break;
	case LEAF_wlanStatsRxAssocCapMismatch:
		val->v.uint32 = wif->stats.is_rx_assoc_capmismatch;
		break;
	case LEAF_wlanStatsRxAssocNoRateMatch:
		val->v.uint32 = wif->stats.is_rx_assoc_norate;
		break;
	case LEAF_wlanStatsRxBadWpaIE:
		val->v.uint32 = wif->stats.is_rx_assoc_badwpaie;
		break;
	case LEAF_wlanStatsRxDeauthenticate:
		val->v.uint32 = wif->stats.is_rx_deauth;
		break;
	case LEAF_wlanStatsRxDisassociate:
		val->v.uint32 = wif->stats.is_rx_disassoc;
		break;
	case LEAF_wlanStatsRxUnknownSubtype:
		val->v.uint32 = wif->stats.is_rx_badsubtype;
		break;
	case LEAF_wlanStatsRxFailedNoBuf:
		val->v.uint32 = wif->stats.is_rx_nobuf;
		break;
	case LEAF_wlanStatsRxBadAuthRequest:
		val->v.uint32 = wif->stats.is_rx_bad_auth;
		break;
	case LEAF_wlanStatsRxUnAuthorized:
		val->v.uint32 = wif->stats.is_rx_unauth;
		break;
	case LEAF_wlanStatsRxBadKeyId:
		val->v.uint32 = wif->stats.is_rx_badkeyid;
		break;
	case LEAF_wlanStatsRxCCMPSeqViolation:
		val->v.uint32 = wif->stats.is_rx_ccmpreplay;
		break;
	case LEAF_wlanStatsRxCCMPBadFormat:
		val->v.uint32 = wif->stats.is_rx_ccmpformat;
		break;
	case LEAF_wlanStatsRxCCMPFailedMIC:
		val->v.uint32 = wif->stats.is_rx_ccmpmic;
		break;
	case LEAF_wlanStatsRxTKIPSeqViolation:
		val->v.uint32 = wif->stats.is_rx_tkipreplay;
		break;
	case LEAF_wlanStatsRxTKIPBadFormat:
		val->v.uint32 = wif->stats.is_rx_tkipformat;
		break;
	case LEAF_wlanStatsRxTKIPFailedMIC:
		val->v.uint32 = wif->stats.is_rx_tkipmic;
		break;
	case LEAF_wlanStatsRxTKIPFailedICV:
		val->v.uint32 = wif->stats.is_rx_tkipicv;
		break;
	case LEAF_wlanStatsRxDiscardACL:
		val->v.uint32 = wif->stats.is_rx_acl;
		break;
	case LEAF_wlanStatsTxFailedNoBuf:
		val->v.uint32 = wif->stats.is_tx_nobuf;
		break;
	case LEAF_wlanStatsTxFailedNoNode:
		val->v.uint32 = wif->stats.is_tx_nonode;
		break;
	case LEAF_wlanStatsTxUnknownMgmt:
		val->v.uint32 = wif->stats.is_tx_unknownmgt;
		break;
	case LEAF_wlanStatsTxBadCipher:
		val->v.uint32 = wif->stats.is_tx_badcipher;
		break;
	case LEAF_wlanStatsTxNoDefKey:
		val->v.uint32 = wif->stats.is_tx_nodefkey;
		break;
	case LEAF_wlanStatsTxFragmented:
		val->v.uint32 = wif->stats.is_tx_fragframes;
		break;
	case LEAF_wlanStatsTxFragmentsCreated:
		val->v.uint32 = wif->stats.is_tx_frags;
		break;
	case LEAF_wlanStatsActiveScans:
		val->v.uint32 = wif->stats.is_scan_active;
		break;
	case LEAF_wlanStatsPassiveScans:
		val->v.uint32 = wif->stats.is_scan_passive;
		break;
	case LEAF_wlanStatsTimeoutInactivity:
		val->v.uint32 = wif->stats.is_node_timeout;
		break;
	case LEAF_wlanStatsCryptoNoMem:
		val->v.uint32 = wif->stats.is_crypto_nomem;
		break;
	case LEAF_wlanStatsSwCryptoTKIP:
		val->v.uint32 = wif->stats.is_crypto_tkip;
		break;
	case LEAF_wlanStatsSwCryptoTKIPEnMIC:
		val->v.uint32 = wif->stats.is_crypto_tkipenmic;
		break;
	case LEAF_wlanStatsSwCryptoTKIPDeMIC:
		val->v.uint32 = wif->stats.is_crypto_tkipdemic;
		break;
	case LEAF_wlanStatsCryptoTKIPCM:
		val->v.uint32 = wif->stats.is_crypto_tkipcm;
		break;
	case LEAF_wlanStatsSwCryptoCCMP:
		val->v.uint32 = wif->stats.is_crypto_ccmp;
		break;
	case LEAF_wlanStatsSwCryptoWEP:
		val->v.uint32 = wif->stats.is_crypto_wep;
		break;
	case LEAF_wlanStatsCryptoCipherKeyRejected:
		val->v.uint32 = wif->stats.is_crypto_setkey_cipher;
		break;
	case LEAF_wlanStatsCryptoNoKey:
		val->v.uint32 = wif->stats.is_crypto_setkey_nokey;
		break;
	case LEAF_wlanStatsCryptoDeleteKeyFailed:
		val->v.uint32 = wif->stats.is_crypto_delkey;
		break;
	case LEAF_wlanStatsCryptoUnknownCipher:
		val->v.uint32 = wif->stats.is_crypto_badcipher;
		break;
	case LEAF_wlanStatsCryptoAttachFailed:
		val->v.uint32 = wif->stats.is_crypto_attachfail;
		break;
	case LEAF_wlanStatsCryptoKeyFailed:
		val->v.uint32 = wif->stats.is_crypto_keyfail;
		break;
	case LEAF_wlanStatsCryptoEnMICFailed:
		val->v.uint32 = wif->stats.is_crypto_enmicfail;
		break;
	case LEAF_wlanStatsIBSSCapMismatch:
		val->v.uint32 = wif->stats.is_ibss_capmismatch;
		break;
	case LEAF_wlanStatsUnassocStaPSPoll:
		val->v.uint32 = wif->stats.is_ps_unassoc;
		break;
	case LEAF_wlanStatsBadAidPSPoll:
		val->v.uint32 = wif->stats.is_ps_badaid;
		break;
	case LEAF_wlanStatsEmptyPSPoll:
		val->v.uint32 = wif->stats.is_ps_qempty;
		break;
	case LEAF_wlanStatsRxFFBadHdr:
		val->v.uint32 = wif->stats.is_ff_badhdr;
		break;
	case LEAF_wlanStatsRxFFTooShort:
		val->v.uint32 = wif->stats.is_ff_tooshort;
		break;
	case LEAF_wlanStatsRxFFSplitError:
		val->v.uint32 = wif->stats.is_ff_split;
		break;
	case LEAF_wlanStatsRxFFDecap:
		val->v.uint32 = wif->stats.is_ff_decap;
		break;
	case LEAF_wlanStatsTxFFEncap:
		val->v.uint32 = wif->stats.is_ff_encap;
		break;
	case LEAF_wlanStatsRxBadBintval:
		val->v.uint32 = wif->stats.is_rx_badbintval;
		break;
	case LEAF_wlanStatsRxDemicFailed:
		val->v.uint32 = wif->stats.is_rx_demicfail;
		break;
	case LEAF_wlanStatsRxDefragFailed:
		val->v.uint32 = wif->stats.is_rx_defrag;
		break;
	case LEAF_wlanStatsRxMgmt:
		val->v.uint32 = wif->stats.is_rx_mgmt;
		break;
	case LEAF_wlanStatsRxActionMgmt:
		val->v.uint32 = wif->stats.is_rx_action;
		break;
	case LEAF_wlanStatsRxAMSDUTooShort:
		val->v.uint32 = wif->stats.is_amsdu_tooshort;
		break;
	case LEAF_wlanStatsRxAMSDUSplitError:
		val->v.uint32 = wif->stats.is_amsdu_split;
		break;
	case LEAF_wlanStatsRxAMSDUDecap:
		val->v.uint32 = wif->stats.is_amsdu_decap;
		break;
	case LEAF_wlanStatsTxAMSDUEncap:
		val->v.uint32 = wif->stats.is_amsdu_encap;
		break;
	case LEAF_wlanStatsAMPDUBadBAR:
		val->v.uint32 = wif->stats.is_ampdu_bar_bad;
		break;
	case LEAF_wlanStatsAMPDUOowBar:
		val->v.uint32 = wif->stats.is_ampdu_bar_oow;
		break;
	case LEAF_wlanStatsAMPDUMovedBAR:
		val->v.uint32 = wif->stats.is_ampdu_bar_move;
		break;
	case LEAF_wlanStatsAMPDURxBAR:
		val->v.uint32 = wif->stats.is_ampdu_bar_rx;
		break;
	case LEAF_wlanStatsAMPDURxOor:
		val->v.uint32 = wif->stats.is_ampdu_rx_oor;
		break;
	case LEAF_wlanStatsAMPDURxCopied:
		val->v.uint32 = wif->stats.is_ampdu_rx_copy;
		break;
	case LEAF_wlanStatsAMPDURxDropped:
		val->v.uint32 = wif->stats.is_ampdu_rx_drop;
		break;
	case LEAF_wlanStatsTxDiscardBadState:
		val->v.uint32 = wif->stats.is_tx_badstate;
		break;
	case LEAF_wlanStatsTxFailedNoAssoc:
		val->v.uint32 = wif->stats.is_tx_notassoc;
		break;
	case LEAF_wlanStatsTxClassifyFailed:
		val->v.uint32 = wif->stats.is_tx_classify;
		break;
	case LEAF_wlanStatsDwdsMcastDiscard:
		val->v.uint32 = wif->stats.is_dwds_mcast;
		break;
	case LEAF_wlanStatsHTAssocRejectNoHT:
		val->v.uint32 = wif->stats.is_ht_assoc_nohtcap;
		break;
	case LEAF_wlanStatsHTAssocDowngrade:
		val->v.uint32 = wif->stats.is_ht_assoc_downgrade;
		break;
	case LEAF_wlanStatsHTAssocRateMismatch:
		val->v.uint32 = wif->stats.is_ht_assoc_norate;
		break;
	case LEAF_wlanStatsAMPDURxAge:
		val->v.uint32 = wif->stats.is_ampdu_rx_age;
		break;
	case LEAF_wlanStatsAMPDUMoved:
		val->v.uint32 = wif->stats.is_ampdu_rx_move;
		break;
	case LEAF_wlanStatsADDBADisabledReject:
		val->v.uint32 = wif->stats.is_addba_reject;
		break;
	case LEAF_wlanStatsADDBANoRequest:
		val->v.uint32 = wif->stats.is_addba_norequest;
		break;
	case LEAF_wlanStatsADDBABadToken:
		val->v.uint32 = wif->stats.is_addba_badtoken;
		break;
	case LEAF_wlanStatsADDBABadPolicy:
		val->v.uint32 = wif->stats.is_addba_badpolicy;
		break;
	case LEAF_wlanStatsAMPDUStopped:
		val->v.uint32 = wif->stats.is_ampdu_stop;
		break;
	case LEAF_wlanStatsAMPDUStopFailed:
		val->v.uint32 = wif->stats.is_ampdu_stop_failed;
		break;
	case LEAF_wlanStatsAMPDURxReorder:
		val->v.uint32 = wif->stats.is_ampdu_rx_reorder;
		break;
	case LEAF_wlanStatsScansBackground:
		val->v.uint32 = wif->stats.is_scan_bg;
		break;
	case LEAF_wlanLastDeauthReason:
		val->v.uint32 = wif->stats.is_rx_deauth_code;
		break;
	case LEAF_wlanLastDissasocReason:
		val->v.uint32 = wif->stats.is_rx_disassoc_code;
		break;
	case LEAF_wlanLastAuthFailReason:
		val->v.uint32 = wif->stats.is_rx_authfail_code;
		break;
	case LEAF_wlanStatsBeaconMissedEvents:
		val->v.uint32 = wif->stats.is_beacon_miss;
		break;
	case LEAF_wlanStatsRxDiscardBadStates:
		val->v.uint32 = wif->stats.is_rx_badstate;
		break;
	case LEAF_wlanStatsFFFlushed:
		val->v.uint32 = wif->stats.is_ff_flush;
		break;
	case LEAF_wlanStatsTxControlFrames:
		val->v.uint32 = wif->stats.is_tx_ctl;
		break;
	case LEAF_wlanStatsAMPDURexmt:
		val->v.uint32 = wif->stats.is_ampdu_rexmt;
		break;
	case LEAF_wlanStatsAMPDURexmtFailed:
		val->v.uint32 = wif->stats.is_ampdu_rexmt_fail;
		break;
	case LEAF_wlanStatsReset:
		val->v.uint32 = wlanStatsReset_no_op;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_wep_iface(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL ||
		    !wif->wepsupported)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		/* XXX: filter wif->wepsupported */
		if ((wif = wlan_get_next_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		break;

	case SNMP_OP_SET:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL ||
		    !wif->wepsupported)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanWepMode:
			if (val->v.integer < wlanWepMode_off ||
			    val->v.integer > wlanWepMode_mixed)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = wif->wepmode;
			wif->wepmode = val->v.integer;
			if (wlan_set_wepmode(wif) < 0) {
				wif->wepmode = ctx->scratch->int1;
				return (SNMP_ERR_GENERR);
			}
			break;
		case LEAF_wlanWepDefTxKey:
			if (val->v.integer < 0 ||
			    val->v.integer > IEEE80211_WEP_NKID)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = wif->weptxkey;
			wif->weptxkey = val->v.integer;
			if (wlan_set_weptxkey(wif) < 0) {
				wif->weptxkey = ctx->scratch->int1;
				return (SNMP_ERR_GENERR);
			}
			break;
		default:
			abort();
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanWepMode:
			wif->wepmode = ctx->scratch->int1;
			if (wlan_set_wepmode(wif) < 0)
				return (SNMP_ERR_GENERR);
			break;
		case LEAF_wlanWepDefTxKey:
			wif->weptxkey = ctx->scratch->int1;
			if (wlan_set_weptxkey(wif) < 0)
				return (SNMP_ERR_GENERR);
			break;
		default:
			abort();
		}
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanWepMode:
		if (wlan_get_wepmode(wif) < 0)
			return (SNMP_ERR_GENERR);
		val->v.integer = wif->wepmode;
		break;
	case LEAF_wlanWepDefTxKey:
		if (wlan_get_weptxkey(wif) < 0)
			return (SNMP_ERR_GENERR);
		val->v.integer = wif->weptxkey;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_wep_key(struct snmp_context *ctx __unused,
    struct snmp_value *val __unused, uint32_t sub __unused,
    uint32_t iidx __unused, enum snmp_op op __unused)
{
	return (SNMP_ERR_NOSUCHNAME);
}

int
op_wlan_mac_access_control(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL ||
		    !wif->macsupported)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		/* XXX: filter wif->macsupported */
		if ((wif = wlan_get_next_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		break;

	case SNMP_OP_SET:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL ||
		    !wif->macsupported)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanMACAccessControlPolicy:
			ctx->scratch->int1 = wif->mac_policy;
			wif->mac_policy = val->v.integer;
			break;
		case LEAF_wlanMACAccessControlNacl:
			return (SNMP_ERR_NOT_WRITEABLE);
		case LEAF_wlanMACAccessControlFlush:
			break;
		default:
			abort();
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanMACAccessControlPolicy:
			if (wlan_set_mac_policy(wif) < 0) {
				wif->mac_policy = ctx->scratch->int1;
				return (SNMP_ERR_GENERR);
			}
			break;
		case LEAF_wlanMACAccessControlFlush:
			if (wlan_flush_mac_mac(wif) < 0)
				return (SNMP_ERR_GENERR);
			break;
		default:
			abort();
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((wif = wlan_get_interface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if (val->var.subs[sub - 1] == LEAF_wlanMACAccessControlPolicy)
			wif->mac_policy = ctx->scratch->int1;
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	if (wlan_get_mac_policy(wif) < 0)
		return (SNMP_ERR_GENERR);

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanMACAccessControlPolicy:
		val->v.integer = wif->mac_policy;
		break;
	case LEAF_wlanMACAccessControlNacl:
		val->v.integer = wif->mac_nacls;
		break;
	case LEAF_wlanMACAccessControlFlush:
		val->v.integer = wlanMACAccessControlFlush_no_op;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_mac_acl_mac(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_iface *wif;
	struct wlan_mac_mac *macl;

	wlan_update_interface_list();
	wlan_mac_update_aclmacs();

	switch (op) {
	case SNMP_OP_GET:
		if ((macl = wlan_get_acl_mac(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((macl = wlan_get_next_acl_mac(&val->var, sub, &wif))
		    == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_mac_index(&val->var, sub, wif->wname, macl->mac);
		break;

	case SNMP_OP_SET:
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanMACAccessControlMAC:
			return (SNMP_ERR_INCONS_NAME);
		case LEAF_wlanMACAccessControlMACStatus:
			return(wlan_acl_mac_set_status(ctx, val, sub));
		default:
			abort();
		}

	case SNMP_OP_COMMIT:
		if ((macl = wlan_get_acl_mac(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if (val->v.integer == RowStatus_destroy &&
		    wlan_mac_delete_mac(wif, macl) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((macl = wlan_get_acl_mac(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if (ctx->scratch->int1 == RowStatus_destroy &&
		    wlan_mac_delete_mac(wif, macl) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanMACAccessControlMAC:
		return (string_get(val, macl->mac, IEEE80211_ADDR_LEN));
	case LEAF_wlanMACAccessControlMACStatus:
		val->v.integer = macl->mac_status;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_mesh_config(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	int which;

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanMeshMaxRetries:
		which = WLAN_MESH_MAX_RETRIES;
		break;
	case LEAF_wlanMeshHoldingTimeout:
		which = WLAN_MESH_HOLDING_TO;
		break;
	case LEAF_wlanMeshConfirmTimeout:
		which = WLAN_MESH_CONFIRM_TO;
		break;
	case LEAF_wlanMeshRetryTimeout:
		which = WLAN_MESH_RETRY_TO;
		break;
	default:
		abort();
	}

	switch (op) {
	case SNMP_OP_GET:
		if (wlan_do_sysctl(&wlan_config, which, 0) < 0)
			return (SNMP_ERR_GENERR);
		break;

	case SNMP_OP_GETNEXT:
		abort();

	case SNMP_OP_SET:
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanMeshRetryTimeout :
			ctx->scratch->int1 = wlan_config.mesh_retryto;
			wlan_config.mesh_retryto = val->v.integer;
			break;
		case LEAF_wlanMeshHoldingTimeout:
			ctx->scratch->int1 = wlan_config.mesh_holdingto;
			wlan_config.mesh_holdingto = val->v.integer;
			break;
		case LEAF_wlanMeshConfirmTimeout:
			ctx->scratch->int1 = wlan_config.mesh_confirmto;
			wlan_config.mesh_confirmto = val->v.integer;
			break;
		case LEAF_wlanMeshMaxRetries:
			ctx->scratch->int1 = wlan_config.mesh_maxretries;
			wlan_config.mesh_maxretries = val->v.integer;
			break;
		}
		if (wlan_do_sysctl(&wlan_config, which, 1) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanMeshRetryTimeout:
			wlan_config.mesh_retryto = ctx->scratch->int1;
			break;
		case LEAF_wlanMeshConfirmTimeout:
			wlan_config.mesh_confirmto = ctx->scratch->int1;
			break;
		case LEAF_wlanMeshHoldingTimeout:
			wlan_config.mesh_holdingto= ctx->scratch->int1;
			break;
		case LEAF_wlanMeshMaxRetries:
			wlan_config.mesh_maxretries = ctx->scratch->int1;
			break;
		}
		if (wlan_do_sysctl(&wlan_config, which, 1) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanMeshRetryTimeout:
		val->v.integer = wlan_config.mesh_retryto;
		break;
	case LEAF_wlanMeshHoldingTimeout:
		val->v.integer = wlan_config.mesh_holdingto;
		break;
	case LEAF_wlanMeshConfirmTimeout:
		val->v.integer = wlan_config.mesh_confirmto;
		break;
	case LEAF_wlanMeshMaxRetries:
		val->v.integer = wlan_config.mesh_maxretries;
		break;
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_mesh_iface(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	int rc;
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_mesh_get_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((wif = wlan_mesh_get_next_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		break;

	case SNMP_OP_SET:
		if ((wif = wlan_mesh_get_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanMeshId:
			if (val->v.octetstring.len > IEEE80211_NWID_LEN)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->ptr1 = malloc(val->v.octetstring.len + 1);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			strlcpy(ctx->scratch->ptr1, wif->desired_ssid,
			    val->v.octetstring.len + 1);
			ctx->scratch->int1 = strlen(wif->desired_ssid);
			memcpy(wif->desired_ssid, val->v.octetstring.octets,
			    val->v.octetstring.len);
			wif->desired_ssid[val->v.octetstring.len] = '\0';
			break;
		case LEAF_wlanMeshTTL:
			ctx->scratch->int1 = wif->mesh_ttl;
			wif->mesh_ttl = val->v.integer;
			break;
		case LEAF_wlanMeshPeeringEnabled:
			ctx->scratch->int1 = wif->mesh_peering;
			wif->mesh_peering = val->v.integer;
			break;
		case LEAF_wlanMeshForwardingEnabled:
			ctx->scratch->int1 = wif->mesh_forwarding;
			wif->mesh_forwarding = val->v.integer;
			break;
		case LEAF_wlanMeshMetric:
			ctx->scratch->int1 = wif->mesh_metric;
			wif->mesh_metric = val->v.integer;
			break;
		case LEAF_wlanMeshPath:
			ctx->scratch->int1 = wif->mesh_path;
			wif->mesh_path = val->v.integer;
			break;
		case LEAF_wlanMeshRoutesFlush:
			if (val->v.integer != wlanMeshRoutesFlush_flush)
				return (SNMP_ERR_INCONS_VALUE);
			return (SNMP_ERR_NOERROR);
		default:
			abort();
		}
		if (val->var.subs[sub - 1] == LEAF_wlanMeshId)
			rc = wlan_config_set_dssid(wif,
			    val->v.octetstring.octets, val->v.octetstring.len);
		else
			rc = wlan_mesh_config_set(wif, val->var.subs[sub - 1]);
		if (rc < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		if ((wif = wlan_mesh_get_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if (val->var.subs[sub - 1] == LEAF_wlanMeshRoutesFlush &&
		    wlan_mesh_flush_routes(wif) < 0)
			return (SNMP_ERR_GENERR);
		if (val->var.subs[sub - 1] == LEAF_wlanMeshId)
			free(ctx->scratch->ptr1);
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((wif = wlan_mesh_get_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanMeshId:
			strlcpy(wif->desired_ssid, ctx->scratch->ptr1,
			    IEEE80211_NWID_LEN);
			free(ctx->scratch->ptr1);
			break;
		case LEAF_wlanMeshTTL:
			wif->mesh_ttl = ctx->scratch->int1;
			break;
		case LEAF_wlanMeshPeeringEnabled:
			wif->mesh_peering = ctx->scratch->int1;
			break;
		case LEAF_wlanMeshForwardingEnabled:
			wif->mesh_forwarding = ctx->scratch->int1;
			break;
		case LEAF_wlanMeshMetric:
			wif->mesh_metric = ctx->scratch->int1;
			break;
		case LEAF_wlanMeshPath:
			wif->mesh_path = ctx->scratch->int1;
			break;
		case LEAF_wlanMeshRoutesFlush:
			return (SNMP_ERR_NOERROR);
		default:
			abort();
		}
		if (val->var.subs[sub - 1] == LEAF_wlanMeshId)
			rc = wlan_config_set_dssid(wif, wif->desired_ssid,
			    strlen(wif->desired_ssid));
		else
			rc = wlan_mesh_config_set(wif, val->var.subs[sub - 1]);
		if (rc < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	if (val->var.subs[sub - 1] == LEAF_wlanMeshId)
		rc = wlan_config_get_dssid(wif);
	else
		rc = wlan_mesh_config_get(wif, val->var.subs[sub - 1]);
	if (rc < 0)
		return (SNMP_ERR_GENERR);

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanMeshId:
		return (string_get(val, wif->desired_ssid, -1));
	case LEAF_wlanMeshTTL:
		val->v.integer = wif->mesh_ttl;
		break;
	case LEAF_wlanMeshPeeringEnabled:
		val->v.integer = wif->mesh_peering;
		break;
	case LEAF_wlanMeshForwardingEnabled:
		val->v.integer = wif->mesh_forwarding;
		break;
	case LEAF_wlanMeshMetric:
		val->v.integer = wif->mesh_metric;
		break;
	case LEAF_wlanMeshPath:
		val->v.integer = wif->mesh_path;
		break;
	case LEAF_wlanMeshRoutesFlush:
		val->v.integer = wlanMeshRoutesFlush_no_op;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_mesh_neighbor(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_peer *wip;
	struct wlan_iface *wif;

	wlan_update_interface_list();
	wlan_update_peers();

	switch (op) {
	case SNMP_OP_GET:
		if ((wip = wlan_mesh_get_peer(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;
	case SNMP_OP_GETNEXT:
		wip = wlan_mesh_get_next_peer(&val->var, sub, &wif);
		if (wip == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_mac_index(&val->var, sub, wif->wname,
		    wip->pmac);
		break;
	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);
	case SNMP_OP_COMMIT:
		/* FALLTHROUGH */
	case SNMP_OP_ROLLBACK:
		/* FALLTHROUGH */
	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanMeshNeighborAddress:
		return (string_get(val, wip->pmac, IEEE80211_ADDR_LEN));
	case LEAF_wlanMeshNeighborFrequency:
		val->v.integer = wip->frequency;
		break;
	case LEAF_wlanMeshNeighborLocalId:
		val->v.integer = wip->local_id;
		break;
	case LEAF_wlanMeshNeighborPeerId:
		val->v.integer = wip->peer_id;
		break;
	case LEAF_wlanMeshNeighborPeerState:
		return (bits_get(val, (uint8_t *)&wip->state,
		    sizeof(wip->state)));
	case LEAF_wlanMeshNeighborCurrentTXRate:
		val->v.integer = wip->txrate;
		break;
	case LEAF_wlanMeshNeighborRxSignalStrength:
		val->v.integer = wip->rssi;
		break;
	case LEAF_wlanMeshNeighborIdleTimer:
		val->v.integer = wip->idle;
		break;
	case LEAF_wlanMeshNeighborTxSequenceNo:
		val->v.integer = wip->txseqs;
		break;
	case LEAF_wlanMeshNeighborRxSequenceNo:
		val->v.integer = wip->rxseqs;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_mesh_route(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_mesh_route *wmr;
	struct wlan_iface *wif;

	wlan_update_interface_list();
	wlan_mesh_update_routes();

	switch (op) {
	case SNMP_OP_GET:
		if ((wmr = wlan_mesh_get_route(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		wmr = wlan_mesh_get_next_route(&val->var, sub, &wif);
		if (wmr == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_mac_index(&val->var, sub, wif->wname,
		    wmr->imroute.imr_dest);
		break;

	case SNMP_OP_SET:
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanMeshRouteDestination:
			return (SNMP_ERR_INCONS_NAME);
		case LEAF_wlanMeshRouteStatus:
			return(wlan_mesh_route_set_status(ctx, val, sub));
		default:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		abort();

	case SNMP_OP_COMMIT:
		if ((wmr = wlan_mesh_get_route(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if (val->v.integer == RowStatus_destroy &&
		    wlan_mesh_delete_route(wif, wmr) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((wmr = wlan_mesh_get_route(&val->var, sub, &wif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if (ctx->scratch->int1 == RowStatus_destroy &&
		    wlan_mesh_delete_route(wif, wmr) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanMeshRouteDestination:
		return (string_get(val, wmr->imroute.imr_dest,
		    IEEE80211_ADDR_LEN));
	case LEAF_wlanMeshRouteNextHop:
		return (string_get(val, wmr->imroute.imr_nexthop,
		    IEEE80211_ADDR_LEN));
	case LEAF_wlanMeshRouteHops:
		val->v.integer = wmr->imroute.imr_nhops;
		break;
	case LEAF_wlanMeshRouteMetric:
		val->v.integer = wmr->imroute.imr_metric;
		break;
	case LEAF_wlanMeshRouteLifeTime:
		val->v.integer = wmr->imroute.imr_lifetime;
		break;
	case LEAF_wlanMeshRouteLastMseq:
		val->v.integer = wmr->imroute.imr_lastmseq;
		break;
	case LEAF_wlanMeshRouteFlags:
		val->v.integer = 0;
		if ((wmr->imroute.imr_flags &
		    IEEE80211_MESHRT_FLAGS_VALID) != 0)
			val->v.integer |= (0x1 << wlanMeshRouteFlags_valid);
		if ((wmr->imroute.imr_flags &
		    IEEE80211_MESHRT_FLAGS_PROXY) != 0)
			val->v.integer |= (0x1 << wlanMeshRouteFlags_proxy);
		return (bits_get(val, (uint8_t *)&val->v.integer,
		    sizeof(val->v.integer)));
	case LEAF_wlanMeshRouteStatus:
		val->v.integer = wmr->mroute_status;
		break;
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_mesh_stats(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_mesh_get_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;
	case SNMP_OP_GETNEXT:
		if ((wif = wlan_mesh_get_next_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		break;
	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);
	case SNMP_OP_COMMIT:
		/* FALLTHROUGH */
	case SNMP_OP_ROLLBACK:
		/* FALLTHROUGH */
	default:
		abort();
	}

	if (wlan_get_stats(wif) < 0)
		return (SNMP_ERR_GENERR);

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanMeshDroppedBadSta:
		val->v.uint32 = wif->stats.is_mesh_wrongmesh;
		break;
	case LEAF_wlanMeshDroppedNoLink:
		val->v.uint32 = wif->stats.is_mesh_nolink;
		break;
	case LEAF_wlanMeshNoFwdTtl:
		val->v.uint32 = wif->stats.is_mesh_fwd_ttl;
		break;
	case LEAF_wlanMeshNoFwdBuf:
		val->v.uint32 = wif->stats.is_mesh_fwd_nobuf;
		break;
	case LEAF_wlanMeshNoFwdTooShort:
		val->v.uint32 = wif->stats.is_mesh_fwd_tooshort;
		break;
	case LEAF_wlanMeshNoFwdDisabled:
		val->v.uint32 = wif->stats.is_mesh_fwd_disabled;
		break;
	case LEAF_wlanMeshNoFwdPathUnknown:
		val->v.uint32 = wif->stats.is_mesh_fwd_nopath;
		break;
	case LEAF_wlanMeshDroppedBadAE:
		val->v.uint32 = wif->stats.is_mesh_badae;
		break;
	case LEAF_wlanMeshRouteAddFailed:
		val->v.uint32 = wif->stats.is_mesh_rtaddfailed;
		break;
	case LEAF_wlanMeshDroppedNoProxy:
		val->v.uint32 = wif->stats.is_mesh_notproxy;
		break;
	case LEAF_wlanMeshDroppedMisaligned:
		val->v.uint32 = wif->stats.is_rx_badalign;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_hwmp_config(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	int which;

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanHWMPRouteInactiveTimeout:
		which = WLAN_HWMP_INACTIVITY_TO;
		break;
	case LEAF_wlanHWMPRootAnnounceInterval:
		which = WLAN_HWMP_RANN_INT;
		break;
	case LEAF_wlanHWMPRootInterval:
		which = WLAN_HWMP_ROOT_INT;
		break;
	case LEAF_wlanHWMPRootTimeout:
		which = WLAN_HWMP_ROOT_TO;
		break;
	case LEAF_wlanHWMPPathLifetime:
		which = WLAN_HWMP_PATH_LIFETIME;
		break;
	case LEAF_wlanHWMPReplyForwardBit:
		which = WLAN_HWMP_REPLY_FORWARD;
		break;
	case LEAF_wlanHWMPTargetOnlyBit:
		which = WLAN_HWMP_TARGET_ONLY;
		break;
	default:
		abort();
	}

	switch (op) {
	case SNMP_OP_GET:
		if (wlan_do_sysctl(&wlan_config, which, 0) < 0)
			return (SNMP_ERR_GENERR);
		break;

	case SNMP_OP_GETNEXT:
		abort();

	case SNMP_OP_SET:
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanHWMPRouteInactiveTimeout:
			ctx->scratch->int1 = wlan_config.hwmp_inact;
			wlan_config.hwmp_inact = val->v.integer;
			break;
		case LEAF_wlanHWMPRootAnnounceInterval:
			ctx->scratch->int1 = wlan_config.hwmp_rannint;
			wlan_config.hwmp_rannint = val->v.integer;
			break;
		case LEAF_wlanHWMPRootInterval:
			ctx->scratch->int1 = wlan_config.hwmp_rootint;
			wlan_config.hwmp_rootint = val->v.integer;
			break;
		case LEAF_wlanHWMPRootTimeout:
			ctx->scratch->int1 = wlan_config.hwmp_roottimeout;
			wlan_config.hwmp_roottimeout = val->v.integer;
			break;
		case LEAF_wlanHWMPPathLifetime:
			ctx->scratch->int1 = wlan_config.hwmp_pathlifetime;
			wlan_config.hwmp_pathlifetime = val->v.integer;
			break;
		case LEAF_wlanHWMPReplyForwardBit:
			ctx->scratch->int1 = wlan_config.hwmp_replyforward;
			wlan_config.hwmp_replyforward = val->v.integer;
			break;
		case LEAF_wlanHWMPTargetOnlyBit:
			ctx->scratch->int1 = wlan_config.hwmp_targetonly;
			wlan_config.hwmp_targetonly = val->v.integer;
			break;
		}
		if (wlan_do_sysctl(&wlan_config, which, 1) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanHWMPRouteInactiveTimeout:
			wlan_config.hwmp_inact = ctx->scratch->int1;
			break;
		case LEAF_wlanHWMPRootAnnounceInterval:
			wlan_config.hwmp_rannint = ctx->scratch->int1;
			break;
		case LEAF_wlanHWMPRootInterval:
			wlan_config.hwmp_rootint = ctx->scratch->int1;
			break;
		case LEAF_wlanHWMPRootTimeout:
			wlan_config.hwmp_roottimeout = ctx->scratch->int1;
			break;
		case LEAF_wlanHWMPPathLifetime:
			wlan_config.hwmp_pathlifetime = ctx->scratch->int1;
			break;
		case LEAF_wlanHWMPReplyForwardBit:
			wlan_config.hwmp_replyforward = ctx->scratch->int1;
			break;
		case LEAF_wlanHWMPTargetOnlyBit:
			wlan_config.hwmp_targetonly = ctx->scratch->int1;
			break;
		}
		if (wlan_do_sysctl(&wlan_config, which, 1) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanHWMPRouteInactiveTimeout:
		val->v.integer = wlan_config.hwmp_inact;
		break;
	case LEAF_wlanHWMPRootAnnounceInterval:
		val->v.integer = wlan_config.hwmp_rannint;
		break;
	case LEAF_wlanHWMPRootInterval:
		val->v.integer = wlan_config.hwmp_rootint;
		break;
	case LEAF_wlanHWMPRootTimeout:
		val->v.integer = wlan_config.hwmp_roottimeout;
		break;
	case LEAF_wlanHWMPPathLifetime:
		val->v.integer = wlan_config.hwmp_pathlifetime;
		break;
	case LEAF_wlanHWMPReplyForwardBit:
		val->v.integer = wlan_config.hwmp_replyforward;
		break;
	case LEAF_wlanHWMPTargetOnlyBit:
		val->v.integer = wlan_config.hwmp_targetonly;
		break;
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_hwmp_iface(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_mesh_get_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((wif = wlan_mesh_get_next_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		break;

	case SNMP_OP_SET:
		if ((wif = wlan_mesh_get_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanHWMPRootMode:
			ctx->scratch->int1 = wif->hwmp_root_mode;
			wif->hwmp_root_mode = val->v.integer;
			break;
		case LEAF_wlanHWMPMaxHops:
			ctx->scratch->int1 = wif->hwmp_max_hops;
			wif->hwmp_max_hops = val->v.integer;
			break;
		default:
			abort();
		}
		if (wlan_hwmp_config_set(wif, val->var.subs[sub - 1]) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((wif = wlan_mesh_get_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (val->var.subs[sub - 1]) {
		case LEAF_wlanHWMPRootMode:
			wif->hwmp_root_mode = ctx->scratch->int1;
			break;
		case LEAF_wlanHWMPMaxHops:
			wif->hwmp_max_hops = ctx->scratch->int1;
			break;
		default:
			abort();
		}
		if (wlan_hwmp_config_set(wif, val->var.subs[sub - 1]) < 0)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	if (wlan_hwmp_config_get(wif, val->var.subs[sub - 1]) < 0)
		return (SNMP_ERR_GENERR);

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanHWMPRootMode:
		val->v.integer = wif->hwmp_root_mode;
		break;
	case LEAF_wlanHWMPMaxHops:
		val->v.integer = wif->hwmp_max_hops;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_wlan_hwmp_stats(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct wlan_iface *wif;

	wlan_update_interface_list();

	switch (op) {
	case SNMP_OP_GET:
		if ((wif = wlan_mesh_get_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;
	case SNMP_OP_GETNEXT:
		if ((wif = wlan_mesh_get_next_iface(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		wlan_append_ifindex(&val->var, sub, wif);
		break;
	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);
	case SNMP_OP_COMMIT:
		/* FALLTHROUGH */
	case SNMP_OP_ROLLBACK:
		/* FALLTHROUGH */
	default:
		abort();
	}

	if (wlan_get_stats(wif) < 0)
		return (SNMP_ERR_GENERR);

	switch (val->var.subs[sub - 1]) {
	case LEAF_wlanMeshHWMPWrongSeqNo:
		val->v.uint32 = wif->stats.is_hwmp_wrongseq;
		break;
	case LEAF_wlanMeshHWMPTxRootPREQ:
		val->v.uint32 = wif->stats.is_hwmp_rootreqs;
		break;
	case LEAF_wlanMeshHWMPTxRootRANN:
		val->v.uint32 = wif->stats.is_hwmp_rootrann;
		break;
	case LEAF_wlanMeshHWMPProxy:
		val->v.uint32 = wif->stats.is_hwmp_proxy;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

/*
 * Encode BITS type for a response packet - XXX: this belongs to the snmp lib.
 */
static int
bits_get(struct snmp_value *value, const u_char *ptr, ssize_t len)
{
	int size;

	if (ptr == NULL) {
		value->v.octetstring.len = 0;
		value->v.octetstring.octets = NULL;
		return (SNMP_ERR_NOERROR);
	}

	/* Determine length - up to 8 octets supported so far. */
	for (size = len; size > 0; size--)
		if (ptr[size - 1] != 0)
			break;
	if (size == 0)
		size = 1;

	value->v.octetstring.len = (u_long)size;
	if ((value->v.octetstring.octets = malloc((size_t)size)) == NULL)
		return (SNMP_ERR_RES_UNAVAIL);
	memcpy(value->v.octetstring.octets, ptr, (size_t)size);
	return (SNMP_ERR_NOERROR);
}

/*
 * Calls for adding/updating/freeing/etc of wireless interfaces.
 */
static void
wlan_free_interface(struct wlan_iface *wif)
{
	wlan_free_peerlist(wif);
	free(wif->chanlist);
	wlan_scan_free_results(wif);
	wlan_mac_free_maclist(wif);
	wlan_mesh_free_routes(wif);
	free(wif);
}

static void
wlan_free_iflist(void)
{
	struct wlan_iface *w;

	while ((w = SLIST_FIRST(&wlan_ifaces)) != NULL) {
		SLIST_REMOVE_HEAD(&wlan_ifaces, w_if);
		wlan_free_interface(w);
	}
}

static struct wlan_iface *
wlan_find_interface(const char *wname)
{
	struct wlan_iface *wif;

	SLIST_FOREACH(wif, &wlan_ifaces, w_if)
		if (strcmp(wif->wname, wname) == 0) {
			if (wif->status != RowStatus_active)
				return (NULL);
			break;
		}

	return (wif);
}

static struct wlan_iface *
wlan_first_interface(void)
{
	return (SLIST_FIRST(&wlan_ifaces));
}

static struct wlan_iface *
wlan_next_interface(struct wlan_iface *wif)
{
	if (wif == NULL)
		return (NULL);

	return (SLIST_NEXT(wif, w_if));
}

/*
 * Add a new interface to the list - sorted by name.
 */
static int
wlan_add_wif(struct wlan_iface *wif)
{
	int cmp;
	struct wlan_iface *temp, *prev;

	if ((prev = SLIST_FIRST(&wlan_ifaces)) == NULL ||
	    strcmp(wif->wname, prev->wname) < 0) {
		SLIST_INSERT_HEAD(&wlan_ifaces, wif, w_if);
		return (0);
	}

	SLIST_FOREACH(temp, &wlan_ifaces, w_if) {
		if ((cmp = strcmp(wif->wname, temp->wname)) <= 0)
			break;
		prev = temp;
	}

	if (temp == NULL)
		SLIST_INSERT_AFTER(prev, wif, w_if);
	else if (cmp > 0)
		SLIST_INSERT_AFTER(temp, wif, w_if);
	else {
		syslog(LOG_ERR, "Wlan iface %s already in list", wif->wname);
		return (-1);
	}

	return (0);
}

static struct wlan_iface *
wlan_new_wif(char *wname)
{
	struct wlan_iface *wif;

	/* Make sure it's not in the list. */
	for (wif = wlan_first_interface(); wif != NULL;
	    wif = wlan_next_interface(wif))
		if (strcmp(wname, wif->wname) == 0) {
			wif->internal = 0;
			return (wif);
		}

	if ((wif = (struct wlan_iface *)malloc(sizeof(*wif))) == NULL)
		return (NULL);

	memset(wif, 0, sizeof(struct wlan_iface));
	strlcpy(wif->wname, wname, IFNAMSIZ);
	wif->status = RowStatus_notReady;
	wif->state = wlanIfaceState_down;
	wif->mode = WlanIfaceOperatingModeType_station;

	if (wlan_add_wif(wif) < 0) {
		free(wif);
		return (NULL);
	}

	return (wif);
}

static void
wlan_delete_wif(struct wlan_iface *wif)
{
	SLIST_REMOVE(&wlan_ifaces, wif, wlan_iface, w_if);
	wlan_free_interface(wif);
}

static int
wlan_attach_newif(struct mibif *mif)
{
	struct wlan_iface *wif;

	if (mif->mib.ifmd_data.ifi_type != IFT_ETHER ||
	    wlan_check_media(mif->name) != IFM_IEEE80211)
		return (0);

	if ((wif = wlan_new_wif(mif->name)) == NULL)
		return (-1);

	(void)wlan_get_opmode(wif);
	wif->index = mif->index;
	wif->status = RowStatus_active;
	(void)wlan_update_interface(wif);

	return (0);
}

static int
wlan_iface_create(struct wlan_iface *wif)
{
	int rc;

	if ((rc = wlan_clone_create(wif)) == SNMP_ERR_NOERROR) {
		/*
		 * The rest of the info will be updated once the
		 * snmp_mibII module notifies us of the interface.
		 */
		wif->status = RowStatus_active;
		if (wif->state == wlanIfaceState_up)
			(void)wlan_config_state(wif, 1);
	}

	return (rc);
}

static int
wlan_iface_destroy(struct wlan_iface *wif)
{
	int rc = SNMP_ERR_NOERROR;

	if (wif->internal == 0)
		rc = wlan_clone_destroy(wif);

	if (rc == SNMP_ERR_NOERROR)
		wlan_delete_wif(wif);

	return (rc);
}

static int
wlan_update_interface(struct wlan_iface *wif)
{
	int i;

	(void)wlan_config_state(wif, 0);
	(void)wlan_get_driver_caps(wif);
	for (i = LEAF_wlanIfacePacketBurst;
	    i <= LEAF_wlanIfaceTdmaBeaconInterval; i++)
		(void)wlan_config_get_ioctl(wif, i);
	(void)wlan_get_stats(wif);
	/*
	 * XXX: wlan_get_channel_list() not needed -
	 * fetched with wlan_get_driver_caps()
	 */
	(void)wlan_get_channel_list(wif);
	(void)wlan_get_roam_params(wif);
	(void)wlan_get_tx_params(wif);
	(void)wlan_get_scan_results(wif);
	(void)wlan_get_wepmode(wif);
	(void)wlan_get_weptxkey(wif);
	(void)wlan_get_mac_policy(wif);
	(void)wlan_get_mac_acl_macs(wif);
	(void)wlan_get_peerinfo(wif);

	if (wif->mode == WlanIfaceOperatingModeType_meshPoint) {
		for (i = LEAF_wlanMeshTTL; i <= LEAF_wlanMeshPath; i++)
			(void)wlan_mesh_config_get(wif, i);
		(void)wlan_mesh_get_routelist(wif);
		for (i = LEAF_wlanHWMPRootMode; i <= LEAF_wlanHWMPMaxHops; i++)
			(void)wlan_hwmp_config_get(wif, i);
	}

	return (0);
}

static void
wlan_update_interface_list(void)
{
	struct wlan_iface *wif, *twif;

	if ((time(NULL) - wlan_iflist_age) <= WLAN_LIST_MAXAGE)
		return;

	/*
	 * The snmp_mibII module would have notified us for new interfaces,
	 * so only check if any have been deleted.
	 */
	SLIST_FOREACH_SAFE(wif, &wlan_ifaces, w_if, twif)
		if (wif->status == RowStatus_active && wlan_get_opmode(wif) < 0)
			wlan_delete_wif(wif);

	wlan_iflist_age = time(NULL);
}

static void
wlan_append_ifindex(struct asn_oid *oid, uint sub, const struct wlan_iface *w)
{
	uint32_t i;

	oid->len = sub + strlen(w->wname) + 1;
	oid->subs[sub] = strlen(w->wname);
	for (i = 1; i <= strlen(w->wname); i++)
		oid->subs[sub + i] = w->wname[i - 1];
}

static uint8_t *
wlan_get_ifname(const struct asn_oid *oid, uint sub, uint8_t *wname)
{
	uint32_t i;

	memset(wname, 0, IFNAMSIZ);

	if (oid->len - sub != oid->subs[sub] + 1 || oid->subs[sub] >= IFNAMSIZ)
		return (NULL);

	for (i = 0; i < oid->subs[sub]; i++)
		wname[i] = oid->subs[sub + i + 1];
	wname[i] = '\0';

	return (wname);
}

static struct wlan_iface *
wlan_get_interface(const struct asn_oid *oid, uint sub)
{
	uint8_t wname[IFNAMSIZ];

	if (wlan_get_ifname(oid, sub, wname) == NULL)
		return (NULL);

	return (wlan_find_interface(wname));
}

static struct wlan_iface *
wlan_get_next_interface(const struct asn_oid *oid, uint sub)
{
	uint32_t i;
	uint8_t wname[IFNAMSIZ];
	struct wlan_iface *wif;

	if (oid->len - sub == 0) {
		for (wif = wlan_first_interface(); wif != NULL;
		    wif = wlan_next_interface(wif))
			if (wif->status == RowStatus_active)
				break;
		return (wif);
	}

	if (oid->len - sub != oid->subs[sub] + 1 || oid->subs[sub] >= IFNAMSIZ)
		return (NULL);

	memset(wname, 0, IFNAMSIZ);
	for (i = 0; i < oid->subs[sub]; i++)
		wname[i] = oid->subs[sub + i + 1];
	wname[i] = '\0';
	if ((wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	while ((wif = wlan_next_interface(wif)) != NULL)
		if (wif->status == RowStatus_active)
			break;

	return (wif);
}

static struct wlan_iface *
wlan_get_snmp_interface(const struct asn_oid *oid, uint sub)
{
	uint8_t wname[IFNAMSIZ];
	struct wlan_iface *wif;

	if (wlan_get_ifname(oid, sub, wname) == NULL)
		return (NULL);

	for (wif = wlan_first_interface(); wif != NULL;
	    wif = wlan_next_interface(wif))
		if (strcmp(wif->wname, wname) == 0)
			break;

	return (wif);
}

static struct wlan_iface *
wlan_get_next_snmp_interface(const struct asn_oid *oid, uint sub)
{
	uint32_t i;
	uint8_t wname[IFNAMSIZ];
	struct wlan_iface *wif;

	if (oid->len - sub == 0)
		return (wlan_first_interface());

	if (oid->len - sub != oid->subs[sub] + 1 || oid->subs[sub] >= IFNAMSIZ)
		return (NULL);

	memset(wname, 0, IFNAMSIZ);
	for (i = 0; i < oid->subs[sub]; i++)
		wname[i] = oid->subs[sub + i + 1];
	wname[i] = '\0';

	for (wif = wlan_first_interface(); wif != NULL;
	    wif = wlan_next_interface(wif))
		if (strcmp(wif->wname, wname) == 0)
			break;

	return (wlan_next_interface(wif));
}

/*
 * Decode/Append an index for tables indexed by the wireless interface
 * name and a MAC address - ACL MACs and Mesh Routes.
 */
static int
wlan_mac_index_decode(const struct asn_oid *oid, uint sub,
    char *wname, uint8_t *mac)
{
	uint32_t i;
	int mac_off;

	if (oid->len - sub != oid->subs[sub] + 2 + IEEE80211_ADDR_LEN
	    || oid->subs[sub] >= IFNAMSIZ)
		return (-1);

	for (i = 0; i < oid->subs[sub]; i++)
		wname[i] = oid->subs[sub + i + 1];
	wname[i] = '\0';

	mac_off = sub + oid->subs[sub] + 1;
	if (oid->subs[mac_off] != IEEE80211_ADDR_LEN)
		return (-1);
	for (i = 0; i < IEEE80211_ADDR_LEN; i++)
		mac[i] = oid->subs[mac_off + i + 1];

	return (0);
}

static void
wlan_append_mac_index(struct asn_oid *oid, uint sub, char *wname, uint8_t *mac)
{
	uint32_t i;

	oid->len = sub + strlen(wname) + IEEE80211_ADDR_LEN + 2;
	oid->subs[sub] = strlen(wname);
	for (i = 1; i <= strlen(wname); i++)
		oid->subs[sub + i] = wname[i - 1];

	sub += strlen(wname) + 1;
	oid->subs[sub] = IEEE80211_ADDR_LEN;
	for (i = 1; i <= IEEE80211_ADDR_LEN; i++)
		oid->subs[sub + i] = mac[i - 1];
}

/*
 * Decode/Append an index for tables indexed by the wireless interface
 * name and the PHY mode - Roam and TX params.
 */
static int
wlan_phy_index_decode(const struct asn_oid *oid, uint sub, char *wname,
    uint32_t *phy)
{
	uint32_t i;

	if (oid->len - sub != oid->subs[sub] + 2 || oid->subs[sub] >= IFNAMSIZ)
		return (-1);

	for (i = 0; i < oid->subs[sub]; i++)
		wname[i] = oid->subs[sub + i + 1];
	wname[i] = '\0';

	*phy = oid->subs[sub + oid->subs[sub] + 1];
	return (0);
}

static void
wlan_append_phy_index(struct asn_oid *oid, uint sub, char *wname, uint32_t phy)
{
	uint32_t i;

	oid->len = sub + strlen(wname) + 2;
	oid->subs[sub] = strlen(wname);
	for (i = 1; i <= strlen(wname); i++)
		oid->subs[sub + i] = wname[i - 1];
	oid->subs[sub + strlen(wname) + 1] = phy;
}

/*
 * Calls for manipulating the peerlist of a wireless interface.
 */
static void
wlan_free_peerlist(struct wlan_iface *wif)
{
	struct wlan_peer *wip;

	while ((wip = SLIST_FIRST(&wif->peerlist)) != NULL) {
		SLIST_REMOVE_HEAD(&wif->peerlist, wp);
		free(wip);
	}

	SLIST_INIT(&wif->peerlist);
}

static struct wlan_peer *
wlan_find_peer(struct wlan_iface *wif, uint8_t *peermac)
{
	struct wlan_peer *wip;

	SLIST_FOREACH(wip, &wif->peerlist, wp)
		if (memcmp(wip->pmac, peermac, IEEE80211_ADDR_LEN) == 0)
			break;

	return (wip);
}

struct wlan_peer *
wlan_new_peer(const uint8_t *pmac)
{
	struct wlan_peer *wip;

	if ((wip = (struct wlan_peer *)malloc(sizeof(*wip))) == NULL)
		return (NULL);

	memset(wip, 0, sizeof(struct wlan_peer));
	memcpy(wip->pmac, pmac, IEEE80211_ADDR_LEN);

	return (wip);
}

void
wlan_free_peer(struct wlan_peer *wip)
{
	free(wip);
}

int
wlan_add_peer(struct wlan_iface *wif, struct wlan_peer *wip)
{
	struct wlan_peer *temp, *prev;

	SLIST_FOREACH(temp, &wif->peerlist, wp)
		if (memcmp(temp->pmac, wip->pmac, IEEE80211_ADDR_LEN) == 0)
			return (-1);

	if ((prev = SLIST_FIRST(&wif->peerlist)) == NULL ||
	    memcmp(wip->pmac, prev->pmac, IEEE80211_ADDR_LEN) < 0) {
	    	SLIST_INSERT_HEAD(&wif->peerlist, wip, wp);
	    	return (0);
	}

	SLIST_FOREACH(temp, &wif->peerlist, wp) {
		if (memcmp(wip->pmac, temp->pmac, IEEE80211_ADDR_LEN) < 0)
			break;
		prev = temp;
	}

	SLIST_INSERT_AFTER(prev, wip, wp);
	return (0);
}

static void
wlan_update_peers(void)
{
	struct wlan_iface *wif;

	if ((time(NULL) - wlan_peerlist_age) <= WLAN_LIST_MAXAGE)
		return;

	for (wif = wlan_first_interface(); wif != NULL;
	    wif = wlan_next_interface(wif)) {
		if (wif->status != RowStatus_active)
			continue;
		wlan_free_peerlist(wif);
		(void)wlan_get_peerinfo(wif);
	}
	wlan_peerlist_age = time(NULL);
}

static struct wlan_peer *
wlan_get_peer(const struct asn_oid *oid, uint sub, struct wlan_iface **wif)
{
	char wname[IFNAMSIZ];
	uint8_t pmac[IEEE80211_ADDR_LEN];

	if (wlan_mac_index_decode(oid, sub, wname, pmac) < 0)
		return (NULL);

	if ((*wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	return (wlan_find_peer(*wif, pmac));
}

static struct wlan_peer *
wlan_get_next_peer(const struct asn_oid *oid, uint sub, struct wlan_iface **wif)
{
	char wname[IFNAMSIZ];
	char pmac[IEEE80211_ADDR_LEN];
	struct wlan_peer *wip;

	if (oid->len - sub == 0) {
		for (*wif = wlan_first_interface(); *wif != NULL;
		    *wif = wlan_next_interface(*wif)) {
			if ((*wif)->mode ==
			    WlanIfaceOperatingModeType_meshPoint)
				continue;
			wip = SLIST_FIRST(&(*wif)->peerlist);
			if (wip != NULL)
				return (wip);
		}
		return (NULL);
	}

	if (wlan_mac_index_decode(oid, sub, wname, pmac) < 0 ||
	    (*wif = wlan_find_interface(wname)) == NULL ||
	    (wip = wlan_find_peer(*wif, pmac)) == NULL)
		return (NULL);

	if ((wip = SLIST_NEXT(wip, wp)) != NULL)
		return (wip);

	while ((*wif = wlan_next_interface(*wif)) != NULL) {
		if ((*wif)->mode == WlanIfaceOperatingModeType_meshPoint)
			continue;
		if ((wip = SLIST_FIRST(&(*wif)->peerlist)) != NULL)
			break;
	}

	return (wip);
}

/*
 * Calls for manipulating the active channel list of a wireless interface.
 */
static void
wlan_update_channels(void)
{
	struct wlan_iface *wif;

	if ((time(NULL) - wlan_chanlist_age) <= WLAN_LIST_MAXAGE)
		return;

	for (wif = wlan_first_interface(); wif != NULL;
	    wif = wlan_next_interface(wif)) {
		if (wif->status != RowStatus_active)
			continue;
		(void)wlan_get_channel_list(wif);
	}
	wlan_chanlist_age = time(NULL);
}

static int
wlan_channel_index_decode(const struct asn_oid *oid, uint sub, char *wname,
    uint32_t *cindex)
{
	uint32_t i;
	if (oid->len - sub != oid->subs[sub] + 2 || oid->subs[sub] >= IFNAMSIZ)
		return (-1);

	for (i = 0; i < oid->subs[sub]; i++)
		wname[i] = oid->subs[sub + i + 1];
	wname[i] = '\0';

	*cindex = oid->subs[sub + oid->subs[sub] + 1];

	return (0);
}

static void
wlan_append_channel_index(struct asn_oid *oid, uint sub,
    const struct wlan_iface *wif, const struct ieee80211_channel *channel)
{
	uint32_t i;

	oid->len = sub + strlen(wif->wname) + 2;
	oid->subs[sub] = strlen(wif->wname);
	for (i = 1; i <= strlen(wif->wname); i++)
		oid->subs[sub + i] = wif->wname[i - 1];
	oid->subs[sub + strlen(wif->wname) + 1] = (channel - wif->chanlist) + 1;
}

static int32_t
wlan_get_channel_type(struct ieee80211_channel *c)
{
	if (IEEE80211_IS_CHAN_FHSS(c))
		return (WlanChannelType_fhss);
	if (IEEE80211_IS_CHAN_A(c))
		return (WlanChannelType_dot11a);
	if (IEEE80211_IS_CHAN_B(c))
		return (WlanChannelType_dot11b);
	if (IEEE80211_IS_CHAN_ANYG(c))
		return (WlanChannelType_dot11g);
	if (IEEE80211_IS_CHAN_HALF(c))
		return (WlanChannelType_tenMHz);
	if (IEEE80211_IS_CHAN_QUARTER(c))
		return (WlanChannelType_fiveMHz);
	if (IEEE80211_IS_CHAN_TURBO(c))
		return (WlanChannelType_turbo);
	if (IEEE80211_IS_CHAN_HT(c))
		return (WlanChannelType_ht);

	return (-1);
}

static struct ieee80211_channel *
wlan_find_channel(struct wlan_iface *wif, uint32_t cindex)
{
	if (wif->chanlist == NULL || cindex > wif->nchannels)
		return (NULL);

	return (wif->chanlist + cindex - 1);
}

static struct ieee80211_channel *
wlan_get_channel(const struct asn_oid *oid, uint sub, struct wlan_iface **wif)
{
	uint32_t cindex;
	char wname[IFNAMSIZ];

	if (wlan_channel_index_decode(oid, sub, wname, &cindex) < 0)
		return (NULL);

	if ((*wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	return (wlan_find_channel(*wif, cindex));
}

static struct ieee80211_channel *
wlan_get_next_channel(const struct asn_oid *oid, uint sub,
    struct wlan_iface **wif)
{
	uint32_t cindex;
	char wname[IFNAMSIZ];

	if (oid->len - sub == 0) {
		for (*wif = wlan_first_interface(); *wif != NULL;
		    *wif = wlan_next_interface(*wif)) {
			if ((*wif)->status != RowStatus_active)
				continue;
			if ((*wif)->nchannels != 0 && (*wif)->chanlist != NULL)
				return ((*wif)->chanlist);
		}
		return (NULL);
	}

	if (wlan_channel_index_decode(oid, sub, wname, &cindex) < 0)
		return (NULL);

	if ((*wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	if (cindex < (*wif)->nchannels)
		return ((*wif)->chanlist + cindex);

	while ((*wif = wlan_next_interface(*wif)) != NULL)
		if ((*wif)->status == RowStatus_active)
			if ((*wif)->nchannels != 0 && (*wif)->chanlist != NULL)
				return ((*wif)->chanlist);

	return (NULL);
}

/*
 * Calls for manipulating the roam params of a wireless interface.
 */
static void
wlan_update_roam_params(void)
{
	struct wlan_iface *wif;

	if ((time(NULL) - wlan_roamlist_age) <= WLAN_LIST_MAXAGE)
		return;

	for (wif = wlan_first_interface(); wif != NULL;
	    wif = wlan_next_interface(wif)) {
		if (wif->status != RowStatus_active)
			continue;
		(void)wlan_get_roam_params(wif);
	}
	wlan_roamlist_age = time(NULL);
}

static struct ieee80211_roamparam *
wlan_get_roam_param(const struct asn_oid *oid, uint sub, struct wlan_iface **wif)
{
	uint32_t phy;
	char wname[IFNAMSIZ];

	if (wlan_phy_index_decode(oid, sub, wname, &phy) < 0)
		return (NULL);

	if ((*wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	if (phy == 0 || phy > IEEE80211_MODE_MAX)
		return (NULL);

	return ((*wif)->roamparams.params + phy - 1);
}

static struct ieee80211_roamparam *
wlan_get_next_roam_param(const struct asn_oid *oid, uint sub,
    struct wlan_iface **wif, uint32_t *phy)
{
	char wname[IFNAMSIZ];

	if (oid->len - sub == 0) {
		for (*wif = wlan_first_interface(); *wif != NULL;
		    *wif = wlan_next_interface(*wif)) {
			if ((*wif)->status != RowStatus_active)
				continue;
			*phy = 1;
			return ((*wif)->roamparams.params);
		}
		return (NULL);
	}

	if (wlan_phy_index_decode(oid, sub, wname, phy) < 0)
		return (NULL);

	if (*phy == 0  || (*wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	if (++(*phy) <= IEEE80211_MODE_MAX)
		return ((*wif)->roamparams.params + *phy - 1);

	*phy = 1;
	while ((*wif = wlan_next_interface(*wif)) != NULL)
		if ((*wif)->status == RowStatus_active)
			return ((*wif)->roamparams.params);

	return (NULL);
}

/*
 * Calls for manipulating the tx params of a wireless interface.
 */
static void
wlan_update_tx_params(void)
{
	struct wlan_iface *wif;

	if ((time(NULL) - wlan_tx_paramlist_age) <= WLAN_LIST_MAXAGE)
		return;

	for (wif = wlan_first_interface(); wif != NULL;
	    wif = wlan_next_interface(wif)) {
		if (wif->status != RowStatus_active)
			continue;
		(void)wlan_get_tx_params(wif);
	}

	wlan_tx_paramlist_age = time(NULL);
}

static struct ieee80211_txparam *
wlan_get_tx_param(const struct asn_oid *oid, uint sub, struct wlan_iface **wif,
    uint32_t *phy)
{
	char wname[IFNAMSIZ];

	if (wlan_phy_index_decode(oid, sub, wname, phy) < 0)
		return (NULL);

	if ((*wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	if (*phy == 0 || *phy > IEEE80211_MODE_MAX)
		return (NULL);

	return ((*wif)->txparams.params + *phy - 1);
}

static struct ieee80211_txparam *
wlan_get_next_tx_param(const struct asn_oid *oid, uint sub,
    struct wlan_iface **wif, uint32_t *phy)
{
	char wname[IFNAMSIZ];

	if (oid->len - sub == 0) {
		for (*wif = wlan_first_interface(); *wif != NULL;
		    *wif = wlan_next_interface(*wif)) {
			if ((*wif)->status != RowStatus_active)
				continue;
			*phy = 1;
			return ((*wif)->txparams.params);
		}
		return (NULL);
	}

	if (wlan_phy_index_decode(oid, sub, wname, phy) < 0)
		return (NULL);

	if (*phy == 0 || (*wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	if (++(*phy) <= IEEE80211_MODE_MAX)
		return ((*wif)->txparams.params + *phy - 1);

	*phy = 1;
	while ((*wif = wlan_next_interface(*wif)) != NULL)
		if ((*wif)->status == RowStatus_active)
			return ((*wif)->txparams.params);

	return (NULL);
}

/*
 * Calls for manipulating the scan results for a wireless interface.
 */
static void
wlan_scan_free_results(struct wlan_iface *wif)
{
	struct wlan_scan_result *sr;

	while ((sr = SLIST_FIRST(&wif->scanlist)) != NULL) {
		SLIST_REMOVE_HEAD(&wif->scanlist, wsr);
		free(sr);
	}

	SLIST_INIT(&wif->scanlist);
}

static struct wlan_scan_result *
wlan_scan_find_result(struct wlan_iface *wif, uint8_t *ssid, uint8_t *bssid)
{
	struct wlan_scan_result *sr;

	SLIST_FOREACH(sr, &wif->scanlist, wsr)
		if (strlen(ssid) == strlen(sr->ssid) &&
		    strcmp(sr->ssid, ssid) == 0 &&
		    memcmp(sr->bssid, bssid, IEEE80211_ADDR_LEN) == 0)
			break;

	return (sr);
}

struct wlan_scan_result *
wlan_scan_new_result(const uint8_t *ssid, const uint8_t *bssid)
{
	struct wlan_scan_result *sr;

	sr = (struct wlan_scan_result *)malloc(sizeof(*sr));
	if (sr == NULL)
		return (NULL);

	memset(sr, 0, sizeof(*sr));
	if (ssid[0] != '\0')
		strlcpy(sr->ssid, ssid, IEEE80211_NWID_LEN + 1);
	memcpy(sr->bssid, bssid, IEEE80211_ADDR_LEN);

	return (sr);
}

void
wlan_scan_free_result(struct wlan_scan_result *sr)
{
	free(sr);
}

static int
wlan_scan_compare_result(struct wlan_scan_result *sr1,
    struct wlan_scan_result *sr2)
{
	uint32_t i;

	if (strlen(sr1->ssid) < strlen(sr2->ssid))
		return (-1);
	if (strlen(sr1->ssid) > strlen(sr2->ssid))
		return (1);

	for (i = 0; i < strlen(sr1->ssid) && i < strlen(sr2->ssid); i++) {
		if (sr1->ssid[i] < sr2->ssid[i])
			return (-1);
		if (sr1->ssid[i] > sr2->ssid[i])
			return (1);
	}

	for (i = 0; i < IEEE80211_ADDR_LEN; i++) {
		if (sr1->bssid[i] < sr2->bssid[i])
			return (-1);
		if (sr1->bssid[i] > sr2->bssid[i])
			return (1);
	}

	return (0);
}

int
wlan_scan_add_result(struct wlan_iface *wif, struct wlan_scan_result *sr)
{
	struct wlan_scan_result *prev, *temp;

	SLIST_FOREACH(temp, &wif->scanlist, wsr)
		if (strlen(temp->ssid) == strlen(sr->ssid) &&
		    strcmp(sr->ssid, temp->ssid) == 0 &&
		    memcmp(sr->bssid, temp->bssid, IEEE80211_ADDR_LEN) == 0)
			return (-1);

	if ((prev = SLIST_FIRST(&wif->scanlist)) == NULL ||
	    wlan_scan_compare_result(sr, prev) < 0) {
	    	SLIST_INSERT_HEAD(&wif->scanlist, sr, wsr);
	    	return (0);
	}

	SLIST_FOREACH(temp, &wif->scanlist, wsr) {
		if (wlan_scan_compare_result(sr, temp) < 0)
			break;
		prev = temp;
	}

	SLIST_INSERT_AFTER(prev, sr, wsr);
	return (0);
}

static void
wlan_scan_update_results(void)
{
	struct wlan_iface *wif;

	if ((time(NULL) - wlan_scanlist_age) <= WLAN_LIST_MAXAGE)
		return;

	for (wif = wlan_first_interface(); wif != NULL;
	    wif = wlan_next_interface(wif)) {
		if (wif->status != RowStatus_active)
			continue;
		wlan_scan_free_results(wif);
		(void)wlan_get_scan_results(wif);
	}
	wlan_scanlist_age = time(NULL);
}

static int
wlan_scanr_index_decode(const struct asn_oid *oid, uint sub,
    char *wname, uint8_t *ssid, uint8_t *bssid)
{
	uint32_t i;
	int offset;

	if (oid->subs[sub] >= IFNAMSIZ)
		return (-1);
	for (i = 0; i < oid->subs[sub]; i++)
		wname[i] = oid->subs[sub + i + 1];
	wname[oid->subs[sub]] = '\0';

	offset = sub + oid->subs[sub] + 1;
	if (oid->subs[offset] > IEEE80211_NWID_LEN)
		return (-1);
	for (i = 0; i < oid->subs[offset]; i++)
		ssid[i] = oid->subs[offset + i + 1];
	ssid[i] = '\0';

	offset = sub + oid->subs[sub] + oid->subs[offset] + 2;
	if (oid->subs[offset] != IEEE80211_ADDR_LEN)
		return (-1);
	for (i = 0; i < IEEE80211_ADDR_LEN; i++)
		bssid[i] = oid->subs[offset + i + 1];

	return (0);
}

static void
wlan_append_scanr_index(struct asn_oid *oid, uint sub, char *wname,
    uint8_t *ssid, uint8_t *bssid)
{
	uint32_t i;

	oid->len = sub + strlen(wname) + strlen(ssid) + IEEE80211_ADDR_LEN + 3;
	oid->subs[sub] = strlen(wname);
	for (i = 1; i <= strlen(wname); i++)
		oid->subs[sub + i] = wname[i - 1];

	sub += strlen(wname) + 1;
	oid->subs[sub] = strlen(ssid);
	for (i = 1; i <= strlen(ssid); i++)
		oid->subs[sub + i] = ssid[i - 1];

	sub += strlen(ssid) + 1;
	oid->subs[sub] = IEEE80211_ADDR_LEN;
	for (i = 1; i <= IEEE80211_ADDR_LEN; i++)
		oid->subs[sub + i] = bssid[i - 1];
}

static struct wlan_scan_result *
wlan_get_scanr(const struct asn_oid *oid, uint sub, struct wlan_iface **wif)
{
	char wname[IFNAMSIZ];
	uint8_t ssid[IEEE80211_NWID_LEN + 1];
	uint8_t bssid[IEEE80211_ADDR_LEN];

	if (wlan_scanr_index_decode(oid, sub, wname, ssid, bssid) < 0)
		return (NULL);

	if ((*wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	return (wlan_scan_find_result(*wif, ssid, bssid));
}

static struct wlan_scan_result *
wlan_get_next_scanr(const struct asn_oid *oid, uint sub,
    struct wlan_iface **wif)
{
	char wname[IFNAMSIZ];
	uint8_t ssid[IEEE80211_NWID_LEN + 1];
	uint8_t bssid[IEEE80211_ADDR_LEN];
	struct wlan_scan_result *sr;

	if (oid->len - sub == 0) {
		for (*wif = wlan_first_interface(); *wif != NULL;
		    *wif = wlan_next_interface(*wif)) {
			sr = SLIST_FIRST(&(*wif)->scanlist);
			if (sr != NULL)
				return (sr);
		}
		return (NULL);
	}

	if (wlan_scanr_index_decode(oid, sub, wname, ssid, bssid) < 0 ||
	    (*wif = wlan_find_interface(wname)) == NULL ||
	    (sr = wlan_scan_find_result(*wif, ssid, bssid)) == NULL)
		return (NULL);

	if ((sr = SLIST_NEXT(sr, wsr)) != NULL)
		return (sr);

	while ((*wif = wlan_next_interface(*wif)) != NULL)
		if ((sr = SLIST_FIRST(&(*wif)->scanlist)) != NULL)
			break;

	return (sr);
}

/*
 * MAC Access Control.
 */
static void
wlan_mac_free_maclist(struct wlan_iface *wif)
{
	struct wlan_mac_mac *wmm;

	while ((wmm = SLIST_FIRST(&wif->mac_maclist)) != NULL) {
		SLIST_REMOVE_HEAD(&wif->mac_maclist, wm);
		free(wmm);
	}

	SLIST_INIT(&wif->mac_maclist);
}

static struct wlan_mac_mac *
wlan_mac_find_mac(struct wlan_iface *wif, uint8_t *mac)
{
	struct wlan_mac_mac *wmm;

	SLIST_FOREACH(wmm, &wif->mac_maclist, wm)
		if (memcmp(wmm->mac, mac, IEEE80211_ADDR_LEN) == 0)
			break;

	return (wmm);
}

struct wlan_mac_mac *
wlan_mac_new_mac(const uint8_t *mac)
{
	struct wlan_mac_mac *wmm;

	if ((wmm = (struct wlan_mac_mac *)malloc(sizeof(*wmm))) == NULL)
		return (NULL);

	memset(wmm, 0, sizeof(*wmm));
	memcpy(wmm->mac, mac, IEEE80211_ADDR_LEN);
	wmm->mac_status = RowStatus_notReady;

	return (wmm);
}

void
wlan_mac_free_mac(struct wlan_mac_mac *wmm)
{
	free(wmm);
}

int
wlan_mac_add_mac(struct wlan_iface *wif, struct wlan_mac_mac *wmm)
{
	struct wlan_mac_mac *temp, *prev;

	SLIST_FOREACH(temp, &wif->mac_maclist, wm)
		if (memcmp(temp->mac, wmm->mac, IEEE80211_ADDR_LEN) == 0)
			return (-1);

	if ((prev = SLIST_FIRST(&wif->mac_maclist)) == NULL ||
	    memcmp(wmm->mac, prev->mac,IEEE80211_ADDR_LEN) < 0) {
	    	SLIST_INSERT_HEAD(&wif->mac_maclist, wmm, wm);
	    	return (0);
	}

	SLIST_FOREACH(temp, &wif->mac_maclist, wm) {
		if (memcmp(wmm->mac, temp->mac, IEEE80211_ADDR_LEN) < 0)
			break;
		prev = temp;
	}

	SLIST_INSERT_AFTER(prev, wmm, wm);
	return (0);
}

static int
wlan_mac_delete_mac(struct wlan_iface *wif, struct wlan_mac_mac *wmm)
{
	if (wmm->mac_status == RowStatus_active &&
	    wlan_del_mac_acl_mac(wif, wmm) < 0)
		return (-1);

	SLIST_REMOVE(&wif->mac_maclist, wmm, wlan_mac_mac, wm);
	free(wmm);

	return (0);
}

static void
wlan_mac_update_aclmacs(void)
{
	struct wlan_iface *wif;
	struct wlan_mac_mac *wmm, *twmm;

	if ((time(NULL) - wlan_maclist_age) <= WLAN_LIST_MAXAGE)
		return;

	for (wif = wlan_first_interface(); wif != NULL;
	    wif = wlan_next_interface(wif)) {
		if (wif->status != RowStatus_active)
			continue;
		/*
		 * Nuke old entries - XXX - they are likely not to
		 * change often - reconsider.
		 */
		SLIST_FOREACH_SAFE(wmm, &wif->mac_maclist, wm, twmm)
			if (wmm->mac_status == RowStatus_active) {
				SLIST_REMOVE(&wif->mac_maclist, wmm,
				    wlan_mac_mac, wm);
				wlan_mac_free_mac(wmm);
			}
		(void)wlan_get_mac_acl_macs(wif);
	}
	wlan_maclist_age = time(NULL);
}

static struct wlan_mac_mac *
wlan_get_acl_mac(const struct asn_oid *oid, uint sub, struct wlan_iface **wif)
{
	char wname[IFNAMSIZ];
	char mac[IEEE80211_ADDR_LEN];

	if (wlan_mac_index_decode(oid, sub, wname, mac) < 0)
		return (NULL);

	if ((*wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	return (wlan_mac_find_mac(*wif, mac));
}

static struct wlan_mac_mac *
wlan_get_next_acl_mac(const struct asn_oid *oid, uint sub,
    struct wlan_iface **wif)
{
	char wname[IFNAMSIZ];
	char mac[IEEE80211_ADDR_LEN];
	struct wlan_mac_mac *wmm;

	if (oid->len - sub == 0) {
		for (*wif = wlan_first_interface(); *wif != NULL;
		    *wif = wlan_next_interface(*wif)) {
			wmm = SLIST_FIRST(&(*wif)->mac_maclist);
			if (wmm != NULL)
				return (wmm);
		}
		return (NULL);
	}

	if (wlan_mac_index_decode(oid, sub, wname, mac) < 0 ||
	    (*wif = wlan_find_interface(wname)) == NULL ||
	    (wmm = wlan_mac_find_mac(*wif, mac)) == NULL)
		return (NULL);

	if ((wmm = SLIST_NEXT(wmm, wm)) != NULL)
		return (wmm);

	while ((*wif = wlan_next_interface(*wif)) != NULL)
		if ((wmm = SLIST_FIRST(&(*wif)->mac_maclist)) != NULL)
			break;

	return (wmm);
}

static int
wlan_acl_mac_set_status(struct snmp_context *ctx, struct snmp_value *val,
    uint sub)
{
	char wname[IFNAMSIZ];
	uint8_t mac[IEEE80211_ADDR_LEN];
	struct wlan_iface *wif;
	struct wlan_mac_mac *macl;

	if (wlan_mac_index_decode(&val->var, sub, wname, mac) < 0)
		return (SNMP_ERR_GENERR);
	macl = wlan_get_acl_mac(&val->var, sub, &wif);

	switch (val->v.integer) {
	case RowStatus_createAndGo:
		if (macl != NULL)
			return (SNMP_ERR_INCONS_NAME);
		break;
	case RowStatus_destroy:
		if (macl == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		ctx->scratch->int1 = RowStatus_active;
		return (SNMP_ERR_NOERROR);
	default:
		return (SNMP_ERR_INCONS_VALUE);
	}


	if (wif == NULL || !wif->macsupported)
		return (SNMP_ERR_INCONS_VALUE);

	if ((macl = wlan_mac_new_mac((const uint8_t *)mac)) == NULL)
		return (SNMP_ERR_GENERR);

	ctx->scratch->int1 = RowStatus_destroy;

	if (wlan_mac_add_mac(wif, macl) < 0) {
		wlan_mac_free_mac(macl);
		return (SNMP_ERR_GENERR);
	}

	ctx->scratch->int1 = RowStatus_destroy;
	if (wlan_add_mac_acl_mac(wif, macl) < 0) {
		(void)wlan_mac_delete_mac(wif, macl);
		return (SNMP_ERR_GENERR);
	}

	return (SNMP_ERR_NOERROR);
}

/*
 * Wireless interfaces operating as mesh points.
 */
static struct wlan_iface *
wlan_mesh_first_interface(void)
{
	struct wlan_iface *wif;

	SLIST_FOREACH(wif, &wlan_ifaces, w_if)
		if (wif->mode == WlanIfaceOperatingModeType_meshPoint &&
		    wif->status == RowStatus_active)
			break;

	return (wif);
}

static struct wlan_iface *
wlan_mesh_next_interface(struct wlan_iface *wif)
{
	struct wlan_iface *nwif;

	while ((nwif = wlan_next_interface(wif)) != NULL) {
		if (nwif->mode == WlanIfaceOperatingModeType_meshPoint &&
		    nwif->status == RowStatus_active)
			break;
		wif = nwif;
	}

	return (nwif);
}

static struct wlan_iface *
wlan_mesh_get_iface(const struct asn_oid *oid, uint sub)
{
	struct wlan_iface *wif;

	if ((wif = wlan_get_interface(oid, sub)) == NULL)
		return (NULL);

	if (wif->mode != WlanIfaceOperatingModeType_meshPoint)
		return (NULL);

	return (wif);
}

static struct wlan_iface *
wlan_mesh_get_next_iface(const struct asn_oid *oid, uint sub)
{
	uint32_t i;
	uint8_t wname[IFNAMSIZ];
	struct wlan_iface *wif;

	if (oid->len - sub == 0)
		return (wlan_mesh_first_interface());

	if (oid->len - sub != oid->subs[sub] + 1 || oid->subs[sub] >= IFNAMSIZ)
		return (NULL);

	memset(wname, 0, IFNAMSIZ);
	for (i = 0; i < oid->subs[sub]; i++)
		wname[i] = oid->subs[sub + i + 1];
	wname[i] = '\0';

	if ((wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	return (wlan_mesh_next_interface(wif));
}

/*
 * The neighbors of wireless interfaces operating as mesh points.
 */
static struct wlan_peer *
wlan_mesh_get_peer(const struct asn_oid *oid, uint sub, struct wlan_iface **wif)
{
	char wname[IFNAMSIZ];
	uint8_t pmac[IEEE80211_ADDR_LEN];

	if (wlan_mac_index_decode(oid, sub, wname, pmac) < 0)
		return (NULL);

	if ((*wif = wlan_find_interface(wname)) == NULL ||
	    (*wif)->mode != WlanIfaceOperatingModeType_meshPoint)
		return (NULL);

	return (wlan_find_peer(*wif, pmac));
}

static struct wlan_peer *
wlan_mesh_get_next_peer(const struct asn_oid *oid, uint sub, struct wlan_iface **wif)
{
	char wname[IFNAMSIZ];
	char pmac[IEEE80211_ADDR_LEN];
	struct wlan_peer *wip;

	if (oid->len - sub == 0) {
		for (*wif = wlan_mesh_first_interface(); *wif != NULL;
		    *wif = wlan_mesh_next_interface(*wif)) {
			wip = SLIST_FIRST(&(*wif)->peerlist);
			if (wip != NULL)
				return (wip);
		}
		return (NULL);
	}

	if (wlan_mac_index_decode(oid, sub, wname, pmac) < 0 ||
	    (*wif = wlan_find_interface(wname)) == NULL ||
	    (*wif)->mode != WlanIfaceOperatingModeType_meshPoint ||
	    (wip = wlan_find_peer(*wif, pmac)) == NULL)
		return (NULL);

	if ((wip = SLIST_NEXT(wip, wp)) != NULL)
		return (wip);

	while ((*wif = wlan_mesh_next_interface(*wif)) != NULL)
		if ((wip = SLIST_FIRST(&(*wif)->peerlist)) != NULL)
			break;

	return (wip);
}

/*
 * Mesh routing table.
 */
static void
wlan_mesh_free_routes(struct wlan_iface *wif)
{
	struct wlan_mesh_route *wmr;

	while ((wmr = SLIST_FIRST(&wif->mesh_routelist)) != NULL) {
		SLIST_REMOVE_HEAD(&wif->mesh_routelist, wr);
		free(wmr);
	}

	SLIST_INIT(&wif->mesh_routelist);
}

static struct wlan_mesh_route *
wlan_mesh_find_route(struct wlan_iface *wif, uint8_t *dstmac)
{
	struct wlan_mesh_route *wmr;

	if (wif->mode != WlanIfaceOperatingModeType_meshPoint)
		return (NULL);

	SLIST_FOREACH(wmr, &wif->mesh_routelist, wr)
		if (memcmp(wmr->imroute.imr_dest, dstmac,
		    IEEE80211_ADDR_LEN) == 0)
			break;

	return (wmr);
}

struct wlan_mesh_route *
wlan_mesh_new_route(const uint8_t *dstmac)
{
	struct wlan_mesh_route *wmr;

	if ((wmr = (struct wlan_mesh_route *)malloc(sizeof(*wmr))) == NULL)
		return (NULL);

	memset(wmr, 0, sizeof(*wmr));
	memcpy(wmr->imroute.imr_dest, dstmac, IEEE80211_ADDR_LEN);
	wmr->mroute_status = RowStatus_notReady;

	return (wmr);
}

void
wlan_mesh_free_route(struct wlan_mesh_route *wmr)
{
	free(wmr);
}

int
wlan_mesh_add_rtentry(struct wlan_iface *wif, struct wlan_mesh_route *wmr)
{
	struct wlan_mesh_route *temp, *prev;

	SLIST_FOREACH(temp, &wif->mesh_routelist, wr)
		if (memcmp(temp->imroute.imr_dest, wmr->imroute.imr_dest,
		    IEEE80211_ADDR_LEN) == 0)
			return (-1);

	if ((prev = SLIST_FIRST(&wif->mesh_routelist)) == NULL ||
	    memcmp(wmr->imroute.imr_dest, prev->imroute.imr_dest,
	    IEEE80211_ADDR_LEN) < 0) {
	    	SLIST_INSERT_HEAD(&wif->mesh_routelist, wmr, wr);
	    	return (0);
	}

	SLIST_FOREACH(temp, &wif->mesh_routelist, wr) {
		if (memcmp(wmr->imroute.imr_dest, temp->imroute.imr_dest,
		    IEEE80211_ADDR_LEN) < 0)
			break;
		prev = temp;
	}

	SLIST_INSERT_AFTER(prev, wmr, wr);
	return (0);
}

static int
wlan_mesh_delete_route(struct wlan_iface *wif, struct wlan_mesh_route *wmr)
{
	if (wmr->mroute_status == RowStatus_active &&
	    wlan_mesh_del_route(wif, wmr) < 0)
		return (-1);

	SLIST_REMOVE(&wif->mesh_routelist, wmr, wlan_mesh_route, wr);
	free(wmr);

	return (0);
}

static void
wlan_mesh_update_routes(void)
{
	struct wlan_iface *wif;
	struct wlan_mesh_route *wmr, *twmr;

	if ((time(NULL) - wlan_mrlist_age) <= WLAN_LIST_MAXAGE)
		return;

	for (wif = wlan_mesh_first_interface(); wif != NULL;
	    wif = wlan_mesh_next_interface(wif)) {
		/*
		 * Nuke old entries - XXX - they are likely not to
		 * change often - reconsider.
		 */
		SLIST_FOREACH_SAFE(wmr, &wif->mesh_routelist, wr, twmr)
			if (wmr->mroute_status == RowStatus_active) {
				SLIST_REMOVE(&wif->mesh_routelist, wmr,
				    wlan_mesh_route, wr);
				wlan_mesh_free_route(wmr);
			}
		(void)wlan_mesh_get_routelist(wif);
	}
	wlan_mrlist_age = time(NULL);
}

static struct wlan_mesh_route *
wlan_mesh_get_route(const struct asn_oid *oid, uint sub, struct wlan_iface **wif)
{
	char wname[IFNAMSIZ];
	char dstmac[IEEE80211_ADDR_LEN];

	if (wlan_mac_index_decode(oid, sub, wname, dstmac) < 0)
		return (NULL);

	if ((*wif = wlan_find_interface(wname)) == NULL)
		return (NULL);

	return (wlan_mesh_find_route(*wif, dstmac));
}

static struct wlan_mesh_route *
wlan_mesh_get_next_route(const struct asn_oid *oid, uint sub,
    struct wlan_iface **wif)
{
	char wname[IFNAMSIZ];
	char dstmac[IEEE80211_ADDR_LEN];
	struct wlan_mesh_route *wmr;

	if (oid->len - sub == 0) {
		for (*wif = wlan_mesh_first_interface(); *wif != NULL;
		    *wif = wlan_mesh_next_interface(*wif)) {
			wmr = SLIST_FIRST(&(*wif)->mesh_routelist);
			if (wmr != NULL)
				return (wmr);
		}
		return (NULL);
	}

	if (wlan_mac_index_decode(oid, sub, wname, dstmac) < 0 ||
	    (*wif = wlan_find_interface(wname)) == NULL ||
	    (wmr = wlan_mesh_find_route(*wif, dstmac)) == NULL)
		return (NULL);

	if ((wmr = SLIST_NEXT(wmr, wr)) != NULL)
		return (wmr);

	while ((*wif = wlan_mesh_next_interface(*wif)) != NULL)
		if ((wmr = SLIST_FIRST(&(*wif)->mesh_routelist)) != NULL)
			break;

	return (wmr);
}

static int
wlan_mesh_route_set_status(struct snmp_context *ctx, struct snmp_value *val,
    uint sub)
{
	char wname[IFNAMSIZ];
	char mac[IEEE80211_ADDR_LEN];
	struct wlan_mesh_route *wmr;
	struct wlan_iface *wif;

	if (wlan_mac_index_decode(&val->var, sub, wname, mac) < 0)
		return (SNMP_ERR_GENERR);
	wmr = wlan_mesh_get_route(&val->var, sub, &wif);

	switch (val->v.integer) {
	case RowStatus_createAndGo:
		if (wmr != NULL)
			return (SNMP_ERR_INCONS_NAME);
		break;
	case RowStatus_destroy:
		if (wmr == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		ctx->scratch->int1 = RowStatus_active;
		return (SNMP_ERR_NOERROR);
	default:
		return (SNMP_ERR_INCONS_VALUE);
	}

	if ((wif = wlan_find_interface(wname)) == NULL)
		return (SNMP_ERR_INCONS_NAME);

	if ((wmr = wlan_mesh_new_route(mac)) == NULL)
		return (SNMP_ERR_GENERR);

	if (wlan_mesh_add_rtentry(wif, wmr) < 0) {
		wlan_mesh_free_route(wmr);
		return (SNMP_ERR_GENERR);
	}

	ctx->scratch->int1 = RowStatus_destroy;
	if (wlan_mesh_add_route(wif, wmr) < 0) {
		(void)wlan_mesh_delete_route(wif, wmr);
		return (SNMP_ERR_GENERR);
	}

	return (SNMP_ERR_NOERROR);
}

/*
 * Wlan snmp module initialization hook.
 * Returns 0 on success, < 0 on error.
 */
static int
wlan_init(struct lmodule * mod __unused, int argc __unused,
     char *argv[] __unused)
{
	if (wlan_kmodules_load() < 0)
		return (-1);

	if (wlan_ioctl_init() < 0)
		return (-1);

	/* Register for new interface creation notifications. */
	if (mib_register_newif(wlan_attach_newif, wlan_module)) {
		syslog(LOG_ERR, "Cannot register newif function: %s",
		    strerror(errno));
		return (-1);
	}

	return (0);
}

/*
 * Wlan snmp module finalization hook.
 */
static int
wlan_fini(void)
{
	mib_unregister_newif(wlan_module);
	or_unregister(reg_wlan);

	/* XXX: Cleanup! */
	wlan_free_iflist();

	return (0);
}

/*
 * Refetch all available data from the kernel.
 */
static void
wlan_update_data(void *arg __unused)
{
}

/*
 * Wlan snmp module start operation.
 */
static void
wlan_start(void)
{
	struct mibif *ifp;

	reg_wlan = or_register(&oid_wlan,
	    "The MIB module for managing wireless networking.", wlan_module);

	 /* Add the existing wlan interfaces. */
	 for (ifp = mib_first_if(); ifp != NULL; ifp = mib_next_if(ifp))
		wlan_attach_newif(ifp);

	wlan_data_timer = timer_start_repeat(wlan_poll_ticks,
	    wlan_poll_ticks, wlan_update_data, NULL, wlan_module);
}

/*
 * Dump the Wlan snmp module data on SIGUSR1.
 */
static void
wlan_dump(void)
{
	/* XXX: Print some debug info to syslog. */
	struct wlan_iface *wif;

	for (wif = wlan_first_interface(); wif != NULL;
	    wif = wlan_next_interface(wif))
		syslog(LOG_ERR, "wlan iface %s", wif->wname);
}

const char wlan_comment[] = \
"This module implements the BEGEMOT MIB for wireless networking.";

const struct snmp_module config = {
	.comment =	wlan_comment,
	.init =		wlan_init,
	.fini =		wlan_fini,
	.start =	wlan_start,
	.tree =		wlan_ctree,
	.dump =		wlan_dump,
	.tree_size =	wlan_CTREE_SIZE,
};
