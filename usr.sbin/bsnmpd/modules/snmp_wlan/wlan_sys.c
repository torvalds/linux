/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.

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

#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_mib.h>
#include <net/if_types.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_regdomain.h>

#include <errno.h>
#include <ifaddrs.h>
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

static int sock = -1;

static int	wlan_ioctl(char *, uint16_t, int *, void *, size_t *, int);
static int	wlan_kmod_load(const char *);
static uint32_t	wlan_drivercaps_to_snmp(uint32_t);
static uint32_t	wlan_cryptocaps_to_snmp(uint32_t);
static uint32_t	wlan_htcaps_to_snmp(uint32_t);
static uint32_t	wlan_peerstate_to_snmp(uint32_t);
static uint32_t	wlan_peercaps_to_snmp(uint32_t );
static uint32_t	wlan_channel_flags_to_snmp_phy(uint32_t);
static uint32_t	wlan_regdomain_to_snmp(int);
static uint32_t	wlan_snmp_to_scan_flags(int);
static int	wlan_config_snmp2ioctl(int);
static int	wlan_snmp_to_regdomain(enum WlanRegDomainCode);
static int	wlan_config_get_country(struct wlan_iface *);
static int	wlan_config_set_country(struct wlan_iface *, char *, int);
static int	wlan_config_get_dchannel(struct wlan_iface *wif);
static int	wlan_config_set_dchannel(struct wlan_iface *wif, uint32_t);
static int	wlan_config_get_bssid(struct wlan_iface *);
static int	wlan_config_set_bssid(struct wlan_iface *, uint8_t *);
static void	wlan_config_set_snmp_intval(struct wlan_iface *, int, int);
static int	wlan_config_snmp2value(int, int, int *);
static int	wlan_config_check(struct wlan_iface *, int);
static int	wlan_config_get_intval(struct wlan_iface *, int);
static int	wlan_config_set_intval(struct wlan_iface *, int, int);
static int	wlan_add_new_scan_result(struct wlan_iface *,
    const struct ieee80211req_scan_result *, uint8_t *);
static int	wlan_add_mac_macinfo(struct wlan_iface *,
    const struct ieee80211req_maclist *);
static struct wlan_peer *wlan_add_peerinfo(const struct ieee80211req_sta_info *);

int
wlan_ioctl_init(void)
{
	if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR, "cannot open socket : %s", strerror(errno));
		return (-1);
	}

	return (0);
}
/*
 * Load the needed modules in kernel if not already there.
 */
enum wlan_kmodules {
	WLAN_KMOD = 0,
	WLAN_KMOD_ACL,
	WLAN_KMOD_WEP,
	WLAN_KMODS_MAX
};

static const char *wmod_names[] = {
	"wlan",
	"wlan_wlan_acl",
	"wlan_wep",
	NULL
};

static int
wlan_kmod_load(const char *modname)
{
	int fileid, modid;
	struct module_stat mstat;

	mstat.version = sizeof(struct module_stat);
	for (fileid = kldnext(0); fileid > 0; fileid = kldnext(fileid)) {
		for (modid = kldfirstmod(fileid); modid > 0;
			modid = modfnext(modid)) {
			if (modstat(modid, &mstat) < 0)
				continue;
			if (strcmp(modname, mstat.name) == 0)
				return (0);
		}
	}

	/* Not present - load it. */
	if (kldload(modname) < 0) {
		syslog(LOG_ERR, "failed to load %s kernel module - %s", modname,
		    strerror(errno));
		return (-1);
	}

	return (1);
}

int
wlan_kmodules_load(void)
{
	if (wlan_kmod_load(wmod_names[WLAN_KMOD]) < 0)
		return (-1);

	if (wlan_kmod_load(wmod_names[WLAN_KMOD_ACL]) > 0)
		syslog(LOG_NOTICE, "SNMP wlan loaded %s module",
		    wmod_names[WLAN_KMOD_ACL]);

	if (wlan_kmod_load(wmod_names[WLAN_KMOD_WEP]) > 0)
		syslog(LOG_NOTICE, "SNMP wlan loaded %s module",
		    wmod_names[WLAN_KMOD_WEP]);

	return (0);
}

/* XXX: FIXME */
static int
wlan_ioctl(char *wif_name, uint16_t req_type, int *val, void *arg,
     size_t *argsize, int set)
{
	struct ieee80211req ireq;

	memset(&ireq, 0, sizeof(struct ieee80211req));
	strlcpy(ireq.i_name, wif_name, IFNAMSIZ);

	ireq.i_type = req_type;
	ireq.i_val = *val;
	ireq.i_len = *argsize;
	ireq.i_data = arg;

	if (ioctl(sock, set ? SIOCS80211 : SIOCG80211, &ireq) < 0) {
		syslog(LOG_ERR, "iface %s - %s param: ioctl(%d) "
		    "failed: %s", wif_name, set ? "set" : "get",
		    req_type, strerror(errno));
		return (-1);
	}

	*argsize = ireq.i_len;
	*val = ireq.i_val;

	return (0);
}

int
wlan_check_media(char *ifname)
{
	struct ifmediareq ifmr;

	memset(&ifmr, 0, sizeof(struct ifmediareq));
	strlcpy(ifmr.ifm_name, ifname, sizeof(ifmr.ifm_name));

	if (ioctl(sock, SIOCGIFMEDIA, &ifmr) < 0 || ifmr.ifm_count == 0)
		return (0);     /* Interface doesn't support SIOCGIFMEDIA. */

	if ((ifmr.ifm_status & IFM_AVALID) == 0)
		return (0);

	return (IFM_TYPE(ifmr.ifm_active));
}

int
wlan_get_opmode(struct wlan_iface *wif)
{
	struct ifmediareq ifmr;

	memset(&ifmr, 0, sizeof(struct ifmediareq));
	strlcpy(ifmr.ifm_name, wif->wname, sizeof(ifmr.ifm_name));

	if (ioctl(sock, SIOCGIFMEDIA, &ifmr) < 0) {
		if (errno == ENXIO)
			return (-1);
		wif->mode = WlanIfaceOperatingModeType_station;
		return (0);
	}

	if (ifmr.ifm_current & IFM_IEEE80211_ADHOC) {
		if (ifmr.ifm_current & IFM_FLAG0)
			wif->mode = WlanIfaceOperatingModeType_adhocDemo;
		else
			wif->mode = WlanIfaceOperatingModeType_ibss;
	} else if (ifmr.ifm_current & IFM_IEEE80211_HOSTAP)
		wif->mode = WlanIfaceOperatingModeType_hostAp;
	else if (ifmr.ifm_current & IFM_IEEE80211_MONITOR)
		wif->mode = WlanIfaceOperatingModeType_monitor;
	else if (ifmr.ifm_current & IFM_IEEE80211_MBSS)
		wif->mode = WlanIfaceOperatingModeType_meshPoint;
	else if (ifmr.ifm_current & IFM_IEEE80211_WDS)
		wif->mode = WlanIfaceOperatingModeType_wds;

	return (0);
}

int
wlan_config_state(struct wlan_iface *wif, uint8_t set)
{
	int	flags;
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, wif->wname);

	if (ioctl(sock, SIOCGIFFLAGS, (caddr_t) &ifr) < 0) {
		syslog(LOG_ERR, "set %s status: ioctl(SIOCGIFFLAGS) "
		    "failed: %s", wif->wname, strerror(errno));
		return (-1);
	}

	if (set == 0) {
		if ((ifr.ifr_flags & IFF_UP) != 0)
			wif->state = wlanIfaceState_up;
		else
			wif->state = wlanIfaceState_down;
		return (0);
	}

	flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);

	if (wif->state == wlanIfaceState_up)
		flags |= IFF_UP;
	else
		flags &= ~IFF_UP;

	ifr.ifr_flags = flags & 0xffff;
	ifr.ifr_flagshigh = flags >> 16;
	if (ioctl(sock, SIOCSIFFLAGS, (caddr_t) &ifr) < 0) {
		syslog(LOG_ERR, "set %s %s: ioctl(SIOCSIFFLAGS) failed: %s",
		    wif->wname, wif->state == wlanIfaceState_up?"up":"down",
		    strerror(errno));
		return (-1);
	}

	return (0);
}

int
wlan_get_local_addr(struct wlan_iface *wif)
{
	int len;
	char ifname[IFNAMSIZ];
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_dl sdl;

	if (getifaddrs(&ifap) != 0) {
		syslog(LOG_ERR, "wlan get mac: getifaddrs() failed - %s",
		    strerror(errno));
		return (-1);
	}

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		memcpy(&sdl, ifa->ifa_addr, sizeof(struct sockaddr_dl));
		if (sdl.sdl_alen > IEEE80211_ADDR_LEN)
			continue;
		if ((len = sdl.sdl_nlen) >= IFNAMSIZ)
			len = IFNAMSIZ - 1;
		memcpy(ifname, sdl.sdl_data, len);
		ifname[len] = '\0';
		if (strcmp(wif->wname, ifname) == 0)
			break;
	}

	freeifaddrs(ifap);
	return (0);
}

int
wlan_get_parent(struct wlan_iface *wif __unused)
{
	/* XXX: There's no way to fetch this from the kernel. */
	return (0);
}

/* XXX */
#define	IEEE80211_C_STA		0x00000001	/* CAPABILITY: STA available */
#define	IEEE80211_C_8023ENCAP	0x00000002	/* CAPABILITY: 802.3 encap */
#define	IEEE80211_C_FF		0x00000040	/* CAPABILITY: ATH FF avail */
#define	IEEE80211_C_TURBOP	0x00000080	/* CAPABILITY: ATH Turbo avail*/
#define	IEEE80211_C_IBSS	0x00000100	/* CAPABILITY: IBSS available */
#define	IEEE80211_C_PMGT	0x00000200	/* CAPABILITY: Power mgmt */
#define	IEEE80211_C_HOSTAP	0x00000400	/* CAPABILITY: HOSTAP avail */
#define	IEEE80211_C_AHDEMO	0x00000800	/* CAPABILITY: Old Adhoc Demo */
#define	IEEE80211_C_SWRETRY	0x00001000	/* CAPABILITY: sw tx retry */
#define	IEEE80211_C_TXPMGT	0x00002000	/* CAPABILITY: tx power mgmt */
#define	IEEE80211_C_SHSLOT	0x00004000	/* CAPABILITY: short slottime */
#define	IEEE80211_C_SHPREAMBLE	0x00008000	/* CAPABILITY: short preamble */
#define	IEEE80211_C_MONITOR	0x00010000	/* CAPABILITY: monitor mode */
#define	IEEE80211_C_DFS		0x00020000	/* CAPABILITY: DFS/radar avail*/
#define	IEEE80211_C_MBSS	0x00040000	/* CAPABILITY: MBSS available */
/* 0x7c0000 available */
#define	IEEE80211_C_WPA1	0x00800000	/* CAPABILITY: WPA1 avail */
#define	IEEE80211_C_WPA2	0x01000000	/* CAPABILITY: WPA2 avail */
#define	IEEE80211_C_WPA		0x01800000	/* CAPABILITY: WPA1+WPA2 avail*/
#define	IEEE80211_C_BURST	0x02000000	/* CAPABILITY: frame bursting */
#define	IEEE80211_C_WME		0x04000000	/* CAPABILITY: WME avail */
#define	IEEE80211_C_WDS		0x08000000	/* CAPABILITY: 4-addr support */
/* 0x10000000 reserved */
#define	IEEE80211_C_BGSCAN	0x20000000	/* CAPABILITY: bg scanning */
#define	IEEE80211_C_TXFRAG	0x40000000	/* CAPABILITY: tx fragments */
#define	IEEE80211_C_TDMA	0x80000000	/* CAPABILITY: TDMA avail */

static uint32_t
wlan_drivercaps_to_snmp(uint32_t dcaps)
{
	uint32_t scaps = 0;

	if ((dcaps & IEEE80211_C_STA) != 0)
		scaps |= (0x1 << WlanDriverCaps_station);
	if ((dcaps & IEEE80211_C_8023ENCAP) != 0)
		scaps |= (0x1 << WlanDriverCaps_ieee8023encap);
	if ((dcaps & IEEE80211_C_FF) != 0)
		scaps |= (0x1 << WlanDriverCaps_athFastFrames);
	if ((dcaps & IEEE80211_C_TURBOP) != 0)
		scaps |= (0x1 << WlanDriverCaps_athTurbo);
	if ((dcaps & IEEE80211_C_IBSS) != 0)
		scaps |= (0x1 << WlanDriverCaps_ibss);
	if ((dcaps & IEEE80211_C_PMGT) != 0)
		scaps |= (0x1 << WlanDriverCaps_pmgt);
	if ((dcaps & IEEE80211_C_HOSTAP) != 0)
		scaps |= (0x1 << WlanDriverCaps_hostAp);
	if ((dcaps & IEEE80211_C_AHDEMO) != 0)
		scaps |= (0x1 << WlanDriverCaps_ahDemo);
	if ((dcaps & IEEE80211_C_SWRETRY) != 0)
		scaps |= (0x1 << WlanDriverCaps_swRetry);
	if ((dcaps & IEEE80211_C_TXPMGT) != 0)
		scaps |= (0x1 << WlanDriverCaps_txPmgt);
	if ((dcaps & IEEE80211_C_SHSLOT) != 0)
		scaps |= (0x1 << WlanDriverCaps_shortSlot);
	if ((dcaps & IEEE80211_C_SHPREAMBLE) != 0)
		scaps |= (0x1 << WlanDriverCaps_shortPreamble);
	if ((dcaps & IEEE80211_C_MONITOR) != 0)
		scaps |= (0x1 << WlanDriverCaps_monitor);
	if ((dcaps & IEEE80211_C_DFS) != 0)
		scaps |= (0x1 << WlanDriverCaps_dfs);
	if ((dcaps & IEEE80211_C_MBSS) != 0)
		scaps |= (0x1 << WlanDriverCaps_mbss);
	if ((dcaps & IEEE80211_C_WPA1) != 0)
		scaps |= (0x1 << WlanDriverCaps_wpa1);
	if ((dcaps & IEEE80211_C_WPA2) != 0)
		scaps |= (0x1 << WlanDriverCaps_wpa2);
	if ((dcaps & IEEE80211_C_BURST) != 0)
		scaps |= (0x1 << WlanDriverCaps_burst);
	if ((dcaps & IEEE80211_C_WME) != 0)
		scaps |= (0x1 << WlanDriverCaps_wme);
	if ((dcaps & IEEE80211_C_WDS) != 0)
		scaps |= (0x1 << WlanDriverCaps_wds);
	if ((dcaps & IEEE80211_C_BGSCAN) != 0)
		scaps |= (0x1 << WlanDriverCaps_bgScan);
	if ((dcaps & IEEE80211_C_TXFRAG) != 0)
		scaps |= (0x1 << WlanDriverCaps_txFrag);
	if ((dcaps & IEEE80211_C_TDMA) != 0)
		scaps |= (0x1 << WlanDriverCaps_tdma);

	return (scaps);
}

static uint32_t
wlan_cryptocaps_to_snmp(uint32_t ccaps)
{
	uint32_t scaps = 0;

#if NOT_YET
	if ((ccaps & IEEE80211_CRYPTO_WEP) != 0)
		scaps |= (0x1 << wlanCryptoCaps_wep);
	if ((ccaps & IEEE80211_CRYPTO_TKIP) != 0)
		scaps |= (0x1 << wlanCryptoCaps_tkip);
	if ((ccaps & IEEE80211_CRYPTO_AES_OCB) != 0)
		scaps |= (0x1 << wlanCryptoCaps_aes);
	if ((ccaps & IEEE80211_CRYPTO_AES_CCM) != 0)
		scaps |= (0x1 << wlanCryptoCaps_aesCcm);
	if ((ccaps & IEEE80211_CRYPTO_TKIPMIC) != 0)
		scaps |= (0x1 << wlanCryptoCaps_tkipMic);
	if ((ccaps & IEEE80211_CRYPTO_CKIP) != 0)
		scaps |= (0x1 << wlanCryptoCaps_ckip);
#else /* !NOT_YET */
	scaps = ccaps;
#endif
	return (scaps);
}

#define	IEEE80211_HTC_AMPDU	0x00010000	/* CAPABILITY: A-MPDU tx */
#define	IEEE80211_HTC_AMSDU	0x00020000	/* CAPABILITY: A-MSDU tx */
/* NB: HT40 is implied by IEEE80211_HTCAP_CHWIDTH40 */
#define	IEEE80211_HTC_HT	0x00040000	/* CAPABILITY: HT operation */
#define	IEEE80211_HTC_SMPS	0x00080000	/* CAPABILITY: MIMO power save*/
#define	IEEE80211_HTC_RIFS	0x00100000	/* CAPABILITY: RIFS support */

static uint32_t
wlan_htcaps_to_snmp(uint32_t hcaps)
{
	uint32_t scaps = 0;

	if ((hcaps & IEEE80211_HTCAP_LDPC) != 0)
		scaps |= (0x1 << WlanHTCaps_ldpc);
	if ((hcaps & IEEE80211_HTCAP_CHWIDTH40) != 0)
		scaps |= (0x1 << WlanHTCaps_chwidth40);
	if ((hcaps & IEEE80211_HTCAP_GREENFIELD) != 0)
		scaps |= (0x1 << WlanHTCaps_greenField);
	if ((hcaps & IEEE80211_HTCAP_SHORTGI20) != 0)
		scaps |= (0x1 << WlanHTCaps_shortGi20);
	if ((hcaps & IEEE80211_HTCAP_SHORTGI40) != 0)
		scaps |= (0x1 << WlanHTCaps_shortGi40);
	if ((hcaps & IEEE80211_HTCAP_TXSTBC) != 0)
		scaps |= (0x1 << WlanHTCaps_txStbc);
	if ((hcaps & IEEE80211_HTCAP_DELBA) != 0)
		scaps |= (0x1 << WlanHTCaps_delba);
	if ((hcaps & IEEE80211_HTCAP_MAXAMSDU_7935) != 0)
		scaps |= (0x1 << WlanHTCaps_amsdu7935);
	if ((hcaps & IEEE80211_HTCAP_DSSSCCK40) != 0)
		scaps |= (0x1 << WlanHTCaps_dssscck40);
	if ((hcaps & IEEE80211_HTCAP_PSMP) != 0)
		scaps |= (0x1 << WlanHTCaps_psmp);
	if ((hcaps & IEEE80211_HTCAP_40INTOLERANT) != 0)
		scaps |= (0x1 << WlanHTCaps_fortyMHzIntolerant);
	if ((hcaps & IEEE80211_HTCAP_LSIGTXOPPROT) != 0)
		scaps |= (0x1 << WlanHTCaps_lsigTxOpProt);
	if ((hcaps & IEEE80211_HTC_AMPDU) != 0)
		scaps |= (0x1 << WlanHTCaps_htcAmpdu);
	if ((hcaps & IEEE80211_HTC_AMSDU) != 0)
		scaps |= (0x1 << WlanHTCaps_htcAmsdu);
	if ((hcaps & IEEE80211_HTC_HT) != 0)
		scaps |= (0x1 << WlanHTCaps_htcHt);
	if ((hcaps & IEEE80211_HTC_SMPS) != 0)
		scaps |= (0x1 << WlanHTCaps_htcSmps);
	if ((hcaps & IEEE80211_HTC_RIFS) != 0)
		scaps |= (0x1 << WlanHTCaps_htcRifs);

	return (scaps);
}

/* XXX: Not here? */
#define	WLAN_SET_TDMA_OPMODE(w) do {						\
	if ((w)->mode == WlanIfaceOperatingModeType_adhocDemo &&		\
	    ((w)->drivercaps & WlanDriverCaps_tdma) != 0)			\
		(w)->mode = WlanIfaceOperatingModeType_tdma;			\
} while (0)
int
wlan_get_driver_caps(struct wlan_iface *wif)
{
	int val = 0;
	size_t argsize;
	struct ieee80211_devcaps_req dc;

	memset(&dc, 0, sizeof(struct ieee80211_devcaps_req));
	argsize = sizeof(struct ieee80211_devcaps_req);

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_DEVCAPS, &val, &dc,
	    &argsize, 0) < 0)
		return (-1);

	wif->drivercaps = wlan_drivercaps_to_snmp(dc.dc_drivercaps);
	wif->cryptocaps = wlan_cryptocaps_to_snmp(dc.dc_cryptocaps);
	wif->htcaps = wlan_htcaps_to_snmp(dc.dc_htcaps);

	WLAN_SET_TDMA_OPMODE(wif);

	argsize = dc.dc_chaninfo.ic_nchans * sizeof(struct ieee80211_channel);
	wif->chanlist = (struct ieee80211_channel *)malloc(argsize);
	if (wif->chanlist == NULL)
		return (0);

	memcpy(wif->chanlist, dc.dc_chaninfo.ic_chans, argsize);
	wif->nchannels = dc.dc_chaninfo.ic_nchans;

	return (0);
}

uint8_t
wlan_channel_state_to_snmp(uint8_t cstate)
{
	uint8_t cs = 0;

	if ((cstate & IEEE80211_CHANSTATE_RADAR) != 0)
		cs |= (0x1 << WlanIfaceChannelStateType_radar);
	if ((cstate & IEEE80211_CHANSTATE_CACDONE) != 0)
		cs |= (0x1 << WlanIfaceChannelStateType_cacDone);
	if ((cstate & IEEE80211_CHANSTATE_CWINT) != 0)
		cs |= (0x1 << WlanIfaceChannelStateType_interferenceDetected);
	if ((cstate & IEEE80211_CHANSTATE_NORADAR) != 0)
		cs |= (0x1 << WlanIfaceChannelStateType_radarClear);

	return (cs);
}

uint32_t
wlan_channel_flags_to_snmp(uint32_t cflags)
{
	uint32_t cf = 0;

	if ((cflags & IEEE80211_CHAN_TURBO) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_turbo);
	if ((cflags & IEEE80211_CHAN_CCK) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_cck);
	if ((cflags & IEEE80211_CHAN_OFDM) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_ofdm);
	if ((cflags & IEEE80211_CHAN_2GHZ) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_spectrum2Ghz);
	if ((cflags & IEEE80211_CHAN_5GHZ) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_spectrum5Ghz);
	if ((cflags & IEEE80211_CHAN_PASSIVE) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_passiveScan);
	if ((cflags & IEEE80211_CHAN_DYN) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_dynamicCckOfdm);
	if ((cflags & IEEE80211_CHAN_GFSK) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_gfsk);
	if ((cflags & IEEE80211_CHAN_GSM) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_spectrum900Mhz);
	if ((cflags & IEEE80211_CHAN_STURBO) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_dot11aStaticTurbo);
	if ((cflags & IEEE80211_CHAN_HALF) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_halfRate);
	if ((cflags & IEEE80211_CHAN_QUARTER) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_quarterRate);
	if ((cflags & IEEE80211_CHAN_HT20) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_ht20);
	if ((cflags & IEEE80211_CHAN_HT40U) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_ht40u);
	if ((cflags & IEEE80211_CHAN_HT40D) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_ht40d);
	if ((cflags & IEEE80211_CHAN_DFS) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_dfs);
	if ((cflags & IEEE80211_CHAN_4MSXMIT) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_xmit4ms);
	if ((cflags & IEEE80211_CHAN_NOADHOC) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_noAdhoc);
	if ((cflags & IEEE80211_CHAN_NOHOSTAP) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_noHostAp);
	if ((cflags & IEEE80211_CHAN_11D) != 0)
		cf |= (0x1 << WlanIfaceChannelFlagsType_dot11d);

	return (cf);
}

/* XXX: */
#define WLAN_SNMP_MAX_CHANS	256
int
wlan_get_channel_list(struct wlan_iface *wif)
{
	int val = 0;
	uint32_t i, nchans;
	size_t argsize;
	struct ieee80211req_chaninfo *chaninfo;
	struct ieee80211req_chanlist active;
	const struct ieee80211_channel *c;

	argsize = sizeof(struct ieee80211req_chaninfo) +
	    sizeof(struct ieee80211_channel) * WLAN_SNMP_MAX_CHANS;
	chaninfo = (struct ieee80211req_chaninfo *)malloc(argsize);
	if (chaninfo == NULL)
		return (-1);

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_CHANINFO, &val, chaninfo,
	    &argsize, 0) < 0)
		return (-1);

	argsize = sizeof(active);
	if (wlan_ioctl(wif->wname, IEEE80211_IOC_CHANLIST, &val, &active,
	    &argsize, 0) < 0)
		goto error;

	for (i = 0, nchans = 0; i < chaninfo->ic_nchans; i++) {
		c = &chaninfo->ic_chans[i];
		if (!isset(active.ic_channels, c->ic_ieee))
				continue;
		nchans++;
	}
	wif->chanlist = (struct ieee80211_channel *)reallocf(wif->chanlist,
	    nchans * sizeof(*c));
	if (wif->chanlist == NULL)
		goto error;
	wif->nchannels = nchans;
	for (i = 0, nchans = 0; i < chaninfo->ic_nchans; i++) {
		c = &chaninfo->ic_chans[i];
		if (!isset(active.ic_channels, c->ic_ieee))
				continue;
		memcpy(wif->chanlist + nchans, c, sizeof (*c));
		nchans++;
	}

	free(chaninfo);
	return (0);
error:
	wif->nchannels = 0;
	free(chaninfo);
	return (-1);
}

static enum WlanIfPhyMode
wlan_channel_flags_to_snmp_phy(uint32_t cflags)
{
	/* XXX: recheck */
	if ((cflags & IEEE80211_CHAN_A) != 0)
		return (WlanIfPhyMode_dot11a);
	if ((cflags & IEEE80211_CHAN_B) != 0)
		return (WlanIfPhyMode_dot11b);
	if ((cflags & IEEE80211_CHAN_G) != 0 ||
	    (cflags & IEEE80211_CHAN_PUREG) != 0)
		return (WlanIfPhyMode_dot11g);
	if ((cflags & IEEE80211_CHAN_FHSS) != 0)
		return (WlanIfPhyMode_fh);
	if ((cflags & IEEE80211_CHAN_TURBO) != 0 &&
	    (cflags & IEEE80211_CHAN_A) != 0)
		return (WlanIfPhyMode_turboA);
	if ((cflags & IEEE80211_CHAN_TURBO) != 0 &&
	    (cflags & IEEE80211_CHAN_G) != 0)
		return (WlanIfPhyMode_turboG);
	if ((cflags & IEEE80211_CHAN_STURBO) != 0)
		return (WlanIfPhyMode_sturboA);
	if ((cflags & IEEE80211_CHAN_HALF) != 0)
		return (WlanIfPhyMode_ofdmHalf);
	if ((cflags & IEEE80211_CHAN_QUARTER) != 0)
		return (WlanIfPhyMode_ofdmQuarter);

	return (WlanIfPhyMode_auto);
}

int
wlan_get_roam_params(struct wlan_iface *wif)
{
	int val = 0;
	size_t argsize;

	argsize = sizeof(struct ieee80211_roamparams_req);
	if (wlan_ioctl(wif->wname, IEEE80211_IOC_ROAM, &val,
	    &wif->roamparams, &argsize, 0) < 0)
		return (-1);

	return (0);
}

int
wlan_get_tx_params(struct wlan_iface *wif)
{
	int val = 0;
	size_t argsize;

	/*
	 * XXX: Reset IEEE80211_RATE_MCS bit on IEEE80211_MODE_11NA
	 * and IEEE80211_MODE_11NG modes.
	 */
	argsize = sizeof(struct ieee80211_txparams_req);
	if (wlan_ioctl(wif->wname, IEEE80211_IOC_TXPARAMS, &val,
	    &wif->txparams, &argsize, 0) < 0)
		return (-1);

	return (0);
}

int
wlan_set_tx_params(struct wlan_iface *wif, int32_t pmode __unused)
{
	int val = 0;
	size_t argsize;

	/*
	 * XXX: Set IEEE80211_RATE_MCS bit on IEEE80211_MODE_11NA
	 * and IEEE80211_MODE_11NG modes.
	 */
	argsize = sizeof(struct ieee80211_txparams_req);
	if (wlan_ioctl(wif->wname, IEEE80211_IOC_TXPARAMS, &val,
	    &wif->txparams, &argsize, 1) < 0)
		return (-1);

	return (0);
}

int
wlan_clone_create(struct wlan_iface *wif)
{
	struct ifreq ifr;
	struct ieee80211_clone_params wcp;
	static const uint8_t zerobssid[IEEE80211_ADDR_LEN];

	memset(&wcp, 0, sizeof(wcp));
	memset(&ifr, 0, sizeof(ifr));

	/* Sanity checks. */
	if (wif == NULL || wif->pname[0] == '\0' || wif->mode > WLAN_IFMODE_MAX)
		return (SNMP_ERR_INCONS_VALUE);

	if (wif->mode == WlanIfaceOperatingModeType_wds &&
	    memcmp(wif->dbssid, zerobssid, IEEE80211_ADDR_LEN) == 0)
		return (SNMP_ERR_INCONS_VALUE);

	strlcpy(wcp.icp_parent, wif->pname, IFNAMSIZ);
	if ((wif->flags & WlanIfaceFlagsType_uniqueBssid) != 0)
		wcp.icp_flags |= IEEE80211_CLONE_BSSID;
	if ((wif->flags & WlanIfaceFlagsType_noBeacons) != 0)
		wcp.icp_flags |= IEEE80211_CLONE_NOBEACONS;
	if (wif->mode == WlanIfaceOperatingModeType_wds &&
	    (wif->flags & WlanIfaceFlagsType_wdsLegacy) != 0)
		wcp.icp_flags |= IEEE80211_CLONE_WDSLEGACY;

	switch (wif->mode) {
	case WlanIfaceOperatingModeType_ibss:
		wcp.icp_opmode = IEEE80211_M_IBSS;
		break;
	case WlanIfaceOperatingModeType_station:
		wcp.icp_opmode = IEEE80211_M_STA;
		break;
	case WlanIfaceOperatingModeType_wds:
		wcp.icp_opmode = IEEE80211_M_WDS;
		break;
	case WlanIfaceOperatingModeType_adhocDemo:
		wcp.icp_opmode = IEEE80211_M_AHDEMO;
		break;
	case WlanIfaceOperatingModeType_hostAp:
		wcp.icp_opmode = IEEE80211_M_HOSTAP;
		break;
	case WlanIfaceOperatingModeType_monitor:
		wcp.icp_opmode = IEEE80211_M_MONITOR;
		break;
	case WlanIfaceOperatingModeType_meshPoint:
		wcp.icp_opmode = IEEE80211_M_MBSS;
		break;
	case WlanIfaceOperatingModeType_tdma:
		wcp.icp_opmode = IEEE80211_M_AHDEMO;
		wcp.icp_flags |= IEEE80211_CLONE_TDMA;
		break;
	}

	memcpy(wcp.icp_bssid, wif->dbssid, IEEE80211_ADDR_LEN);
	if (memcmp(wif->dlmac, zerobssid, IEEE80211_ADDR_LEN) != 0) {
		memcpy(wcp.icp_macaddr, wif->dlmac, IEEE80211_ADDR_LEN);
		wcp.icp_flags |= IEEE80211_CLONE_MACADDR;
	}

	strlcpy(ifr.ifr_name, wif->wname, IFNAMSIZ);
	ifr.ifr_data = (caddr_t) &wcp;

	if (ioctl(sock, SIOCIFCREATE2, (caddr_t) &ifr) < 0) {
		syslog(LOG_ERR, "wlan clone create: ioctl(SIOCIFCREATE2) "
		    "failed: %s", strerror(errno));
		return (SNMP_ERR_GENERR);
	}

	return (SNMP_ERR_NOERROR);
}

int
wlan_clone_destroy(struct wlan_iface *wif)
{
	struct ifreq ifr;

	if (wif == NULL)
		return (SNMP_ERR_INCONS_VALUE);

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, wif->wname);

	if (ioctl(sock, SIOCIFDESTROY, &ifr) < 0) {
		syslog(LOG_ERR, "wlan clone destroy: ioctl(SIOCIFDESTROY) "
		    "failed: %s", strerror(errno));
		return (SNMP_ERR_GENERR);
	}

	return (SNMP_ERR_NOERROR);
}

static int
wlan_config_snmp2ioctl(int which)
{
	int op;

	switch (which) {
	case LEAF_wlanIfacePacketBurst:
		op = IEEE80211_IOC_BURST;
		break;
	case LEAF_wlanIfaceCountryCode:
		op = IEEE80211_IOC_REGDOMAIN;
		break;
	case LEAF_wlanIfaceRegDomain:
		op = IEEE80211_IOC_REGDOMAIN;
		break;
	case LEAF_wlanIfaceDesiredSsid:
		op = IEEE80211_IOC_SSID;
		break;
	case LEAF_wlanIfaceDesiredChannel:
		op = IEEE80211_IOC_CURCHAN;
		break;
	case LEAF_wlanIfaceDynamicFreqSelection:
		op = IEEE80211_IOC_DFS;
		break;
	case LEAF_wlanIfaceFastFrames:
		op = IEEE80211_IOC_FF;
		break;
	case LEAF_wlanIfaceDturbo:
		op = IEEE80211_IOC_TURBOP;
		break;
	case LEAF_wlanIfaceTxPower:
		op = IEEE80211_IOC_TXPOWER;
		break;
	case LEAF_wlanIfaceFragmentThreshold:
		op = IEEE80211_IOC_FRAGTHRESHOLD;
		break;
	case LEAF_wlanIfaceRTSThreshold:
		op = IEEE80211_IOC_RTSTHRESHOLD;
		break;
	case LEAF_wlanIfaceWlanPrivacySubscribe:
		op = IEEE80211_IOC_WPS;
		break;
	case LEAF_wlanIfaceBgScan:
		op = IEEE80211_IOC_BGSCAN;
		break;
	case LEAF_wlanIfaceBgScanIdle:
		op = IEEE80211_IOC_BGSCAN_IDLE;
		break;
	case LEAF_wlanIfaceBgScanInterval:
		op = IEEE80211_IOC_BGSCAN_INTERVAL;
		break;
	case LEAF_wlanIfaceBeaconMissedThreshold:
		op = IEEE80211_IOC_BMISSTHRESHOLD;
		break;
	case LEAF_wlanIfaceDesiredBssid:
		op = IEEE80211_IOC_BSSID;
		break;
	case LEAF_wlanIfaceRoamingMode:
		op = IEEE80211_IOC_ROAMING;
		break;
	case LEAF_wlanIfaceDot11d:
		op = IEEE80211_IOC_DOTD;
		break;
	case LEAF_wlanIfaceDot11h:
		op = IEEE80211_IOC_DOTH;
		break;
	case LEAF_wlanIfaceDynamicWds:
		op = IEEE80211_IOC_DWDS;
		break;
	case LEAF_wlanIfacePowerSave:
		op = IEEE80211_IOC_POWERSAVE;
		break;
	case LEAF_wlanIfaceApBridge:
		op = IEEE80211_IOC_APBRIDGE;
		break;
	case LEAF_wlanIfaceBeaconInterval:
		op = IEEE80211_IOC_BEACON_INTERVAL;
		break;
	case LEAF_wlanIfaceDtimPeriod:
		op = IEEE80211_IOC_DTIM_PERIOD;
		break;
	case LEAF_wlanIfaceHideSsid:
		op = IEEE80211_IOC_HIDESSID;
		break;
	case LEAF_wlanIfaceInactivityProccess:
		op = IEEE80211_IOC_INACTIVITY;
		break;
	case LEAF_wlanIfaceDot11gProtMode:
		op = IEEE80211_IOC_PROTMODE;
		break;
	case LEAF_wlanIfaceDot11gPureMode:
		op = IEEE80211_IOC_PUREG;
		break;
	case LEAF_wlanIfaceDot11nPureMode:
		op = IEEE80211_IOC_PUREN;
		break;
	case LEAF_wlanIfaceDot11nAmpdu:
		op = IEEE80211_IOC_AMPDU;
		break;
	case LEAF_wlanIfaceDot11nAmpduDensity:
		op = IEEE80211_IOC_AMPDU_DENSITY;
		break;
	case LEAF_wlanIfaceDot11nAmpduLimit:
		op = IEEE80211_IOC_AMPDU_LIMIT;
		break;
	case LEAF_wlanIfaceDot11nAmsdu:
		op = IEEE80211_IOC_AMSDU;
		break;
	case LEAF_wlanIfaceDot11nAmsduLimit:
		op = IEEE80211_IOC_AMSDU_LIMIT;
		break;
	case LEAF_wlanIfaceDot11nHighThroughput:
		op = IEEE80211_IOC_HTCONF;
		break;
	case LEAF_wlanIfaceDot11nHTCompatible:
		op = IEEE80211_IOC_HTCOMPAT;
		break;
	case LEAF_wlanIfaceDot11nHTProtMode:
		op = IEEE80211_IOC_HTPROTMODE;
		break;
	case LEAF_wlanIfaceDot11nRIFS:
		op = IEEE80211_IOC_RIFS;
		break;
	case LEAF_wlanIfaceDot11nShortGI:
		op = IEEE80211_IOC_SHORTGI;
		break;
	case LEAF_wlanIfaceDot11nSMPSMode:
		op = IEEE80211_IOC_SMPS;
		break;
	case LEAF_wlanIfaceTdmaSlot:
		op = IEEE80211_IOC_TDMA_SLOT;
		break;
	case LEAF_wlanIfaceTdmaSlotCount:
		op = IEEE80211_IOC_TDMA_SLOTCNT;
		break;
	case LEAF_wlanIfaceTdmaSlotLength:
		op = IEEE80211_IOC_TDMA_SLOTLEN;
		break;
	case LEAF_wlanIfaceTdmaBeaconInterval:
		op = IEEE80211_IOC_TDMA_BINTERVAL;
		break;
	default:
		op = -1;
	}

	return (op);
}

static enum WlanRegDomainCode
wlan_regdomain_to_snmp(int which)
{
	enum WlanRegDomainCode reg_domain;

	switch (which) {
	case SKU_FCC:
		reg_domain = WlanRegDomainCode_fcc;
		break;
	case SKU_CA:
		reg_domain = WlanRegDomainCode_ca;
		break;
	case SKU_ETSI:
		reg_domain = WlanRegDomainCode_etsi;
		break;
	case SKU_ETSI2:
		reg_domain = WlanRegDomainCode_etsi2;
		break;
	case SKU_ETSI3:
		reg_domain = WlanRegDomainCode_etsi3;
		break;
	case SKU_FCC3:
		reg_domain = WlanRegDomainCode_fcc3;
		break;
	case SKU_JAPAN:
		reg_domain = WlanRegDomainCode_japan;
		break;
	case SKU_KOREA:
		reg_domain = WlanRegDomainCode_korea;
		break;
	case SKU_APAC:
		reg_domain = WlanRegDomainCode_apac;
		break;
	case SKU_APAC2:
		reg_domain = WlanRegDomainCode_apac2;
		break;
	case SKU_APAC3:
		reg_domain = WlanRegDomainCode_apac3;
		break;
	case SKU_ROW:
		reg_domain = WlanRegDomainCode_row;
		break;
	case SKU_NONE:
		reg_domain = WlanRegDomainCode_none;
		break;
	case SKU_DEBUG:
		reg_domain = WlanRegDomainCode_debug;
		break;
	case SKU_SR9:
		reg_domain = WlanRegDomainCode_sr9;
		break;
	case SKU_XR9:
		reg_domain = WlanRegDomainCode_xr9;
		break;
	case SKU_GZ901:
		reg_domain = WlanRegDomainCode_gz901;
		break;
	case 0:
		reg_domain = WlanRegDomainCode_none;
		break;
	default:
		syslog(LOG_ERR, "unknown regdomain (0x%x) ", which);
		reg_domain = WlanRegDomainCode_none;
		break;
	}

	return (reg_domain);
}

static int
wlan_snmp_to_regdomain(enum WlanRegDomainCode regdomain)
{
	int which;

	switch (regdomain) {
	case WlanRegDomainCode_fcc:
		which = SKU_FCC;
		break;
	case WlanRegDomainCode_ca:
		which = SKU_CA;
		break;
	case WlanRegDomainCode_etsi:
		which = SKU_ETSI;
		break;
	case WlanRegDomainCode_etsi2:
		which = SKU_ETSI2;
		break;
	case WlanRegDomainCode_etsi3:
		which = SKU_ETSI3;
		break;
	case WlanRegDomainCode_fcc3:
		which = SKU_FCC3;
		break;
	case WlanRegDomainCode_japan:
		which = SKU_JAPAN;
		break;
	case WlanRegDomainCode_korea:
		which = SKU_KOREA;
		break;
	case WlanRegDomainCode_apac:
		which = SKU_APAC;
		break;
	case WlanRegDomainCode_apac2:
		which = SKU_APAC2;
		break;
	case WlanRegDomainCode_apac3:
		which = SKU_APAC3;
		break;
	case WlanRegDomainCode_row:
		which = SKU_ROW;
		break;
	case WlanRegDomainCode_none:
		which = SKU_NONE;
		break;
	case WlanRegDomainCode_debug:
		which = SKU_DEBUG;
		break;
	case WlanRegDomainCode_sr9:
		which = SKU_SR9;
		break;
	case WlanRegDomainCode_xr9:
		which = SKU_XR9;
		break;
	case WlanRegDomainCode_gz901:
		which = SKU_GZ901;
		break;
	default:
		syslog(LOG_ERR, "unknown snmp regdomain (0x%x) ", regdomain);
		which = SKU_NONE;
		break;
	}

	return (which);
}

static int
wlan_config_get_country(struct wlan_iface *wif)
{
	int val = 0;
	size_t argsize;
	struct ieee80211_regdomain regdomain;

	memset(&regdomain, 0, sizeof(regdomain));
	argsize = sizeof(regdomain);

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_REGDOMAIN, &val, &regdomain,
	    &argsize, 0) < 0)
		return (-1);

	wif->reg_domain = wlan_regdomain_to_snmp(regdomain.regdomain);
	wif->country_code[0] = regdomain.isocc[0];
	wif->country_code[1] = regdomain.isocc[1];
	wif->country_code[2] = regdomain.location;

	return (0);
}

static int
wlan_config_set_country(struct wlan_iface *wif, char *ccode, int rdomain)
{
	int val = 0, txpowermax;
	uint32_t i;
	size_t argsize = 0;
	struct ieee80211_regdomain_req *regdomain;

	if (wlan_get_channel_list(wif) < 0)
		return (-1);

	if (wif->nchannels == 0) {
		syslog(LOG_ERR, "iface %s - set regdomain failed", wif->wname);
		return (-1);
	}

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_TXPOWMAX, &txpowermax, 0,
	    &argsize, 0) < 0)
		return (-1);

	regdomain = malloc(IEEE80211_REGDOMAIN_SIZE(wif->nchannels));
	if (regdomain == NULL)
		return (-1);
	memset(regdomain, 0, IEEE80211_REGDOMAIN_SIZE(wif->nchannels));
	argsize = IEEE80211_REGDOMAIN_SIZE(wif->nchannels);

	/* XXX: recheck with how this is done by ifconfig(8) */
	regdomain->rd.regdomain = wlan_snmp_to_regdomain(rdomain);
	regdomain->rd.isocc[0] = ccode[0];
	regdomain->rd.isocc[1] = ccode[1];
	regdomain->rd.location = ccode[2];

	/* XXX: fill the channel list properly */
	regdomain->chaninfo.ic_nchans = wif->nchannels;
	memcpy(regdomain->chaninfo.ic_chans, wif->chanlist,
	    wif->nchannels * sizeof(struct ieee80211_channel));
	for (i = 0; i < wif->nchannels; i++)
		regdomain->chaninfo.ic_chans[i].ic_maxregpower = txpowermax;

	wif->state = wlanIfaceState_down;
	if (wlan_config_state(wif, 1) < 0 ||
	    wlan_ioctl(wif->wname, IEEE80211_IOC_REGDOMAIN, &val, regdomain,
	    &argsize, 1) < 0) {
		free(regdomain);
		return (-1);
	}

	wif->state = wlanIfaceState_up;
	(void)wlan_config_state(wif, 1);
	wif->reg_domain = wlan_regdomain_to_snmp(regdomain->rd.regdomain);
	wif->country_code[0] = regdomain->rd.isocc[0];
	wif->country_code[1] = regdomain->rd.isocc[1];
	wif->country_code[2] = regdomain->rd.location;
	free(regdomain);

	return (0);
}

int
wlan_config_get_dssid(struct wlan_iface *wif)
{
	int val = -1;
	size_t argsize = IEEE80211_NWID_LEN + 1;
	char ssid[IEEE80211_NWID_LEN + 1];

	memset(ssid, 0, IEEE80211_NWID_LEN + 1);

	if (wlan_ioctl(wif->wname,
	    (wif->mode == WlanIfaceOperatingModeType_meshPoint) ?
	    IEEE80211_IOC_MESH_ID : IEEE80211_IOC_SSID, &val, ssid,
	    &argsize, 0) < 0)
		return (-1);

	if (argsize > IEEE80211_NWID_LEN)
		argsize = IEEE80211_NWID_LEN;
	memcpy(wif->desired_ssid, ssid, argsize);
	wif->desired_ssid[argsize] = '\0';

	return (0);
}

int
wlan_config_set_dssid(struct wlan_iface *wif, char *ssid, int slen)
{
	int val = 0;
	size_t argsize = slen;

	if (wlan_ioctl(wif->wname,
	    (wif->mode == WlanIfaceOperatingModeType_meshPoint) ?
	    IEEE80211_IOC_MESH_ID : IEEE80211_IOC_SSID, &val, ssid,
	    &argsize, 1) < 0)
		return (-1);

	if (argsize > IEEE80211_NWID_LEN)
		argsize = IEEE80211_NWID_LEN;
	memcpy(wif->desired_ssid, ssid, argsize);
	wif->desired_ssid[argsize] = '\0';

	return (0);
}

static int
wlan_config_get_dchannel(struct wlan_iface *wif)
{
	uint32_t i = 0;
	int val = 0;
	size_t argsize = sizeof(struct ieee80211_channel);
	struct ieee80211_channel chan;

	if (wlan_get_channel_list(wif) < 0)
		return (-1);

	memset(&chan, 0, sizeof(chan));
	if (wlan_ioctl(wif->wname, IEEE80211_IOC_CURCHAN, &val, &chan,
	    &argsize, 0) < 0)
		return (-1);

	for (i = 0; i < wif->nchannels; i++)
		if (chan.ic_ieee == wif->chanlist[i].ic_ieee &&
		    chan.ic_flags == wif->chanlist[i].ic_flags) {
			wif->desired_channel = i + 1;
			break;
		}

	return (0);
}

static int
wlan_config_set_dchannel(struct wlan_iface *wif, uint32_t dchannel)
{
	int val = 0;
	size_t argsize = sizeof(struct ieee80211_channel);
	struct ieee80211_channel chan;

	if (wlan_get_channel_list(wif) < 0)
		return (-1);

	if (dchannel > wif->nchannels)
		return (-1);

	memcpy(&chan, wif->chanlist + dchannel - 1, sizeof(chan));
	if (wlan_ioctl(wif->wname, IEEE80211_IOC_CURCHAN, &val, &chan,
	    &argsize, 1) < 0)
		return (-1);

	wif->desired_channel = dchannel;

	return (0);
}

static int
wlan_config_get_bssid(struct wlan_iface *wif)
{
	int val = 0;
	size_t argsize = IEEE80211_ADDR_LEN;
	char bssid[IEEE80211_ADDR_LEN];

	memset(bssid, 0, IEEE80211_ADDR_LEN);

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_BSSID, &val, bssid,
	    &argsize, 0) < 0 || argsize != IEEE80211_ADDR_LEN)
		return (-1);

	memcpy(wif->desired_bssid, bssid, IEEE80211_ADDR_LEN);

	return (0);
}

static int
wlan_config_set_bssid(struct wlan_iface *wif, uint8_t *bssid)
{
	int val = 0;
	size_t argsize = IEEE80211_ADDR_LEN;

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_BSSID, &val, bssid,
	    &argsize, 1) < 0 || argsize != IEEE80211_ADDR_LEN)
		return (-1);

	memcpy(wif->desired_bssid, bssid, IEEE80211_ADDR_LEN);

	return (0);
}

/*
 * Convert the value returned by the kernel to the appropriate SNMP
 * representation and set the corresponding interface member accordingly.
 */
static void
wlan_config_set_snmp_intval(struct wlan_iface *wif, int op, int val)
{
	switch (op) {
	case IEEE80211_IOC_BURST:
		if (val == 0)
			wif->packet_burst = TruthValue_false;
		else
			wif->packet_burst = TruthValue_true;
		break;
	case IEEE80211_IOC_DFS:
		if (val == 0)
			wif->dyn_frequency = TruthValue_false;
		else
			wif->dyn_frequency = TruthValue_true;
		break;
	case IEEE80211_IOC_FF:
		if (val == 0)
			wif->fast_frames = TruthValue_false;
		else
			wif->fast_frames = TruthValue_true;
		break;
	case IEEE80211_IOC_TURBOP:
		if (val == 0)
			wif->dturbo = TruthValue_false;
		else
			wif->dturbo = TruthValue_true;
		break;
	case IEEE80211_IOC_TXPOWER:
		wif->tx_power = val / 2;
		break;
	case IEEE80211_IOC_FRAGTHRESHOLD:
		wif->frag_threshold = val;
		break;
	case IEEE80211_IOC_RTSTHRESHOLD:
		wif->rts_threshold = val;
		break;
	case IEEE80211_IOC_WPS:
		if (val == 0)
			wif->priv_subscribe = TruthValue_false;
		else
			wif->priv_subscribe = TruthValue_true;
		break;
	case IEEE80211_IOC_BGSCAN:
		if (val == 0)
			wif->bg_scan = TruthValue_false;
		else
			wif->bg_scan = TruthValue_true;
		break;
	case IEEE80211_IOC_BGSCAN_IDLE:
		wif->bg_scan_idle = val;
		break;
	case IEEE80211_IOC_BGSCAN_INTERVAL:
		wif->bg_scan_interval = val;
		break;
	case IEEE80211_IOC_BMISSTHRESHOLD:
		wif->beacons_missed = val;
		break;
	case IEEE80211_IOC_ROAMING:
		switch (val) {
		case IEEE80211_ROAMING_DEVICE:
			wif->roam_mode = wlanIfaceRoamingMode_device;
			break;
		case IEEE80211_ROAMING_MANUAL:
			wif->roam_mode = wlanIfaceRoamingMode_manual;
			break;
		case IEEE80211_ROAMING_AUTO:
			/* FALTHROUGH */
		default:
			wif->roam_mode = wlanIfaceRoamingMode_auto;
			break;
		}
		break;
	case IEEE80211_IOC_DOTD:
		if (val == 0)
			wif->dot11d = TruthValue_false;
		else
			wif->dot11d = TruthValue_true;
		break;
	case IEEE80211_IOC_DOTH:
		if (val == 0)
			wif->dot11h = TruthValue_false;
		else
			wif->dot11h = TruthValue_true;
		break;
	case IEEE80211_IOC_DWDS:
		if (val == 0)
			wif->dynamic_wds = TruthValue_false;
		else
			wif->dynamic_wds = TruthValue_true;
		break;
	case IEEE80211_IOC_POWERSAVE:
		if (val == 0)
			wif->power_save = TruthValue_false;
		else
			wif->power_save = TruthValue_true;
		break;
	case IEEE80211_IOC_APBRIDGE:
		if (val == 0)
			wif->ap_bridge = TruthValue_false;
		else
			wif->ap_bridge = TruthValue_true;
		break;
	case IEEE80211_IOC_BEACON_INTERVAL:
		wif->beacon_interval = val;
		break;
	case IEEE80211_IOC_DTIM_PERIOD:
		wif->dtim_period = val;
		break;
	case IEEE80211_IOC_HIDESSID:
		if (val == 0)
			wif->hide_ssid = TruthValue_false;
		else
			wif->hide_ssid = TruthValue_true;
		break;
	case IEEE80211_IOC_INACTIVITY:
		if (val == 0)
			wif->inact_process = TruthValue_false;
		else
			wif->inact_process = TruthValue_true;
		break;
	case IEEE80211_IOC_PROTMODE:
		switch (val) {
		case IEEE80211_PROTMODE_CTS:
			wif->do11g_protect = wlanIfaceDot11gProtMode_cts;
			break;
		case IEEE80211_PROTMODE_RTSCTS:
			wif->do11g_protect = wlanIfaceDot11gProtMode_rtscts;
			break;
		case IEEE80211_PROTMODE_OFF:
			/* FALLTHROUGH */
		default:
			wif->do11g_protect = wlanIfaceDot11gProtMode_off;
			break;
		}
		break;
	case IEEE80211_IOC_PUREG:
		if (val == 0)
			wif->dot11g_pure = TruthValue_false;
		else
			wif->dot11g_pure = TruthValue_true;
		break;
	case IEEE80211_IOC_PUREN:
		if (val == 0)
			wif->dot11n_pure = TruthValue_false;
		else
			wif->dot11n_pure = TruthValue_true;
		break;
	case IEEE80211_IOC_AMPDU:
		switch (val) {
		case 0:
			wif->ampdu = WlanIfaceDot11nPduType_disabled;
			break;
		case 1:
			wif->ampdu = WlanIfaceDot11nPduType_txOnly;
			break;
		case 2:
			wif->ampdu = WlanIfaceDot11nPduType_rxOnly;
			break;
		case 3:
			/* FALLTHROUGH */
		default:
			wif->ampdu = WlanIfaceDot11nPduType_txAndRx;
			break;
		}
		break;
	case IEEE80211_IOC_AMPDU_DENSITY:
		switch (val) {
		case IEEE80211_HTCAP_MPDUDENSITY_025:
			wif->ampdu_density = 25;
			break;
		case IEEE80211_HTCAP_MPDUDENSITY_05:
			wif->ampdu_density = 50;
			break;
		case IEEE80211_HTCAP_MPDUDENSITY_1:
			wif->ampdu_density = 100;
			break;
		case IEEE80211_HTCAP_MPDUDENSITY_2:
			wif->ampdu_density = 200;
			break;
		case IEEE80211_HTCAP_MPDUDENSITY_4:
			wif->ampdu_density = 400;
			break;
		case IEEE80211_HTCAP_MPDUDENSITY_8:
			wif->ampdu_density = 800;
			break;
		case IEEE80211_HTCAP_MPDUDENSITY_16:
			wif->ampdu_density = 1600;
			break;
		case IEEE80211_HTCAP_MPDUDENSITY_NA:
		default:
			wif->ampdu_density = 0;
			break;
		}
		break;
	case IEEE80211_IOC_AMPDU_LIMIT:
		switch (val) {
		case IEEE80211_HTCAP_MAXRXAMPDU_8K:
			wif->ampdu_limit = 8192;
			break;
		case IEEE80211_HTCAP_MAXRXAMPDU_16K:
			wif->ampdu_limit = 16384;
			break;
		case IEEE80211_HTCAP_MAXRXAMPDU_32K:
			wif->ampdu_limit = 32768;
			break;
		case IEEE80211_HTCAP_MAXRXAMPDU_64K:
		default:
			wif->ampdu_limit = 65536;
			break;
		}
		break;
	case IEEE80211_IOC_AMSDU:
		switch (val) {
		case 0:
			wif->amsdu = WlanIfaceDot11nPduType_disabled;
			break;
		case 1:
			wif->amsdu = WlanIfaceDot11nPduType_txOnly;
			break;
		case 3:
			wif->amsdu = WlanIfaceDot11nPduType_txAndRx;
			break;
		case 2:
		default:
			/* FALLTHROUGH */
			wif->amsdu = WlanIfaceDot11nPduType_rxOnly;
			break;
		}
		break;
	case IEEE80211_IOC_AMSDU_LIMIT:
		wif->amsdu_limit = val;
		break;
	case IEEE80211_IOC_HTCONF:
		if (val == 0) /* XXX */
			wif->ht_enabled = TruthValue_false;
		else
			wif->ht_enabled = TruthValue_true;
		break;
	case IEEE80211_IOC_HTCOMPAT:
		if (val == 0)
			wif->ht_compatible = TruthValue_false;
		else
			wif->ht_compatible = TruthValue_true;
		break;
	case IEEE80211_IOC_HTPROTMODE:
		if (val == IEEE80211_PROTMODE_RTSCTS)
			wif->ht_prot_mode = wlanIfaceDot11nHTProtMode_rts;
		else
			wif->ht_prot_mode = wlanIfaceDot11nHTProtMode_off;
		break;
	case IEEE80211_IOC_RIFS:
		if (val == 0)
			wif->rifs = TruthValue_false;
		else
			wif->rifs = TruthValue_true;
		break;
	case IEEE80211_IOC_SHORTGI:
		if (val == 0)
			wif->short_gi = TruthValue_false;
		else
			wif->short_gi = TruthValue_true;
		break;
	case IEEE80211_IOC_SMPS:
		switch (val) {
		case IEEE80211_HTCAP_SMPS_DYNAMIC:
			wif->smps_mode = wlanIfaceDot11nSMPSMode_dynamic;
			break;
		case IEEE80211_HTCAP_SMPS_ENA:
			wif->smps_mode = wlanIfaceDot11nSMPSMode_static;
			break;
		case IEEE80211_HTCAP_SMPS_OFF:
			/* FALLTHROUGH */
		default:
			wif->smps_mode = wlanIfaceDot11nSMPSMode_disabled;
			break;
		}
		break;
	case IEEE80211_IOC_TDMA_SLOT:
		wif->tdma_slot = val;
		break;
	case IEEE80211_IOC_TDMA_SLOTCNT:
		wif->tdma_slot_count = val;
		break;
	case IEEE80211_IOC_TDMA_SLOTLEN:
		wif->tdma_slot_length = val;
		break;
	case IEEE80211_IOC_TDMA_BINTERVAL:
		wif->tdma_binterval = val;
		break;
	default:
		break;
	}
}

/*
 * Convert an SNMP value to the kernel equivalent and also do sanity check
 * for each specific type.
 */
static int
wlan_config_snmp2value(int which, int sval, int *value)
{
	*value = 0;

	switch (which) {
	case IEEE80211_IOC_BURST:
	case IEEE80211_IOC_DFS:
	case IEEE80211_IOC_FF:
	case IEEE80211_IOC_TURBOP:
	case IEEE80211_IOC_WPS:
	case IEEE80211_IOC_BGSCAN:
	case IEEE80211_IOC_DOTD:
	case IEEE80211_IOC_DOTH:
	case IEEE80211_IOC_DWDS:
	case IEEE80211_IOC_POWERSAVE:
	case IEEE80211_IOC_APBRIDGE:
	case IEEE80211_IOC_HIDESSID:
	case IEEE80211_IOC_INACTIVITY:
	case IEEE80211_IOC_PUREG:
	case IEEE80211_IOC_PUREN:
	case IEEE80211_IOC_HTCONF:
	case IEEE80211_IOC_HTCOMPAT:
	case IEEE80211_IOC_RIFS:
		if (sval == TruthValue_true)
			*value = 1;
		else if (sval != TruthValue_false)
			return (SNMP_ERR_INCONS_VALUE);
		break;
	case IEEE80211_IOC_REGDOMAIN:
		break;
	case IEEE80211_IOC_SSID:
		break;
	case IEEE80211_IOC_CURCHAN:
		break;
	case IEEE80211_IOC_TXPOWER:
		*value = sval * 2;
		break;
	case IEEE80211_IOC_FRAGTHRESHOLD:
		if (sval < IEEE80211_FRAG_MIN || sval > IEEE80211_FRAG_MAX)
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	case IEEE80211_IOC_RTSTHRESHOLD:
		if (sval < IEEE80211_RTS_MIN || sval > IEEE80211_RTS_MAX)
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	case IEEE80211_IOC_BGSCAN_IDLE:
		if (sval < WLAN_BGSCAN_IDLE_MIN)
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	case IEEE80211_IOC_BGSCAN_INTERVAL:
		if (sval < WLAN_SCAN_VALID_MIN)
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	case IEEE80211_IOC_BMISSTHRESHOLD:
		if (sval < IEEE80211_HWBMISS_MIN || sval > IEEE80211_HWBMISS_MAX)
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	case IEEE80211_IOC_BSSID:
		break;
	case IEEE80211_IOC_ROAMING:
		switch (sval) {
		case wlanIfaceRoamingMode_device:
			*value = IEEE80211_ROAMING_DEVICE;
			break;
		case wlanIfaceRoamingMode_manual:
			*value = IEEE80211_ROAMING_MANUAL;
			break;
		case wlanIfaceRoamingMode_auto:
			*value = IEEE80211_ROAMING_AUTO;
			break;
		default:
			return (SNMP_ERR_INCONS_VALUE);
		}
		break;
	case IEEE80211_IOC_BEACON_INTERVAL:
		if (sval < IEEE80211_BINTVAL_MIN || sval > IEEE80211_BINTVAL_MAX)
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	case IEEE80211_IOC_DTIM_PERIOD:
		if (sval < IEEE80211_DTIM_MIN || sval > IEEE80211_DTIM_MAX)
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	case IEEE80211_IOC_PROTMODE:
		switch (sval) {
		case wlanIfaceDot11gProtMode_cts:
			*value = IEEE80211_PROTMODE_CTS;
			break;
		case wlanIfaceDot11gProtMode_rtscts:
			*value = IEEE80211_PROTMODE_RTSCTS;
			break;
		case wlanIfaceDot11gProtMode_off:
			*value = IEEE80211_PROTMODE_OFF;
			break;
		default:
			return (SNMP_ERR_INCONS_VALUE);
		}
		break;
	case IEEE80211_IOC_AMPDU:
		switch (sval) {
		case WlanIfaceDot11nPduType_disabled:
			break;
		case WlanIfaceDot11nPduType_txOnly:
			*value = 1;
			break;
		case WlanIfaceDot11nPduType_rxOnly:
			*value = 2;
			break;
		case WlanIfaceDot11nPduType_txAndRx:
			*value = 3;
			break;
		default:
			return (SNMP_ERR_INCONS_VALUE);
		}
		break;
	case IEEE80211_IOC_AMPDU_DENSITY:
		switch (sval) {
		case 0:
			*value = IEEE80211_HTCAP_MPDUDENSITY_NA;
			break;
		case 25:
			*value = IEEE80211_HTCAP_MPDUDENSITY_025;
			break;
		case 50:
			*value = IEEE80211_HTCAP_MPDUDENSITY_05;
			break;
		case 100:
			*value = IEEE80211_HTCAP_MPDUDENSITY_1;
			break;
		case 200:
			*value = IEEE80211_HTCAP_MPDUDENSITY_2;
			break;
		case 400:
			*value = IEEE80211_HTCAP_MPDUDENSITY_4;
			break;
		case 800:
			*value = IEEE80211_HTCAP_MPDUDENSITY_8;
			break;
		case 1600:
			*value = IEEE80211_HTCAP_MPDUDENSITY_16;
			break;
		default:
			return (SNMP_ERR_INCONS_VALUE);
		}
		break;
	case IEEE80211_IOC_AMPDU_LIMIT:
		switch (sval) {
		case 8192:
			*value = IEEE80211_HTCAP_MAXRXAMPDU_8K;
			break;
		case 16384:
			*value = IEEE80211_HTCAP_MAXRXAMPDU_16K;
			break;
		case 32768:
			*value = IEEE80211_HTCAP_MAXRXAMPDU_32K;
			break;
		case 65536:
			*value = IEEE80211_HTCAP_MAXRXAMPDU_64K;
			break;
		default:
			return (SNMP_ERR_INCONS_VALUE);
		}
		break;
	case IEEE80211_IOC_AMSDU:
		switch (sval) {
		case WlanIfaceDot11nPduType_disabled:
			break;
		case WlanIfaceDot11nPduType_txOnly:
			*value = 1;
			break;
		case WlanIfaceDot11nPduType_rxOnly:
			*value = 2;
			break;
		case WlanIfaceDot11nPduType_txAndRx:
			*value = 3;
			break;
		default:
			return (SNMP_ERR_INCONS_VALUE);
		}
		break;
	case IEEE80211_IOC_AMSDU_LIMIT:
		if (sval == 3839 || sval == 0)
			*value = IEEE80211_HTCAP_MAXAMSDU_3839;
		else if (sval == 7935)
			*value = IEEE80211_HTCAP_MAXAMSDU_7935;
		else
			return (SNMP_ERR_INCONS_VALUE);
		break;
	case IEEE80211_IOC_HTPROTMODE:
		switch (sval) {
		case wlanIfaceDot11nHTProtMode_rts:
			*value = IEEE80211_PROTMODE_RTSCTS;
			break;
		case wlanIfaceDot11nHTProtMode_off:
			break;
		default:
			return (SNMP_ERR_INCONS_VALUE);
		}
		break;
	case IEEE80211_IOC_SHORTGI:
		if (sval == TruthValue_true)
			*value = IEEE80211_HTCAP_SHORTGI20 |
			    IEEE80211_HTCAP_SHORTGI40;
		else if (sval != TruthValue_false)
			return (SNMP_ERR_INCONS_VALUE);
		break;
	case IEEE80211_IOC_SMPS:
		switch (sval) {
		case wlanIfaceDot11nSMPSMode_disabled:
			*value = IEEE80211_HTCAP_SMPS_OFF;
			break;
		case wlanIfaceDot11nSMPSMode_static:
			*value = IEEE80211_HTCAP_SMPS_ENA;
			break;
		case wlanIfaceDot11nSMPSMode_dynamic:
			*value = IEEE80211_HTCAP_SMPS_DYNAMIC;
			break;
		default:
			return (SNMP_ERR_INCONS_VALUE);
		}
		break;
	case IEEE80211_IOC_TDMA_SLOT:
		if (sval < 0 || sval > WLAN_TDMA_MAXSLOTS) /* XXX */
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	case IEEE80211_IOC_TDMA_SLOTCNT:
		if (sval < 0 || sval > WLAN_TDMA_MAXSLOTS) /* XXX */
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	case IEEE80211_IOC_TDMA_SLOTLEN:
		if (sval < 2*100 || sval > 0xfffff) /* XXX */
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	case IEEE80211_IOC_TDMA_BINTERVAL:
		if (sval < 1) /* XXX */
			return (SNMP_ERR_INCONS_VALUE);
		*value = sval;
		break;
	default:
		return (SNMP_ERR_INCONS_VALUE);
	}

	return (SNMP_ERR_NOERROR);
}

/*
 * Sanity checks for the wlanIfaceConfigTable.
 */
static int
wlan_config_check(struct wlan_iface *wif, int op)
{
	switch (op) {
	case IEEE80211_IOC_BURST:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_burst)) == 0) {
			wif->packet_burst = TruthValue_false;
			return (-1);
		}
		break;
	case IEEE80211_IOC_DFS:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_dfs)) == 0) {
			wif->dyn_frequency = TruthValue_false;
			return (-1);
		}
		break;
	case IEEE80211_IOC_FF:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_athFastFrames))
		    == 0) {
			wif->fast_frames = TruthValue_false;
			return (-1);
		}
		break;
	case IEEE80211_IOC_TURBOP:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_athTurbo)) == 0) {
			wif->dturbo = TruthValue_false;
			return (-1);
		}
		break;
	case IEEE80211_IOC_TXPOWER:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_txPmgt)) == 0) {
			wif->tx_power = 0;
			return (-1);
		}
		break;
	case IEEE80211_IOC_FRAGTHRESHOLD:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_txFrag)) == 0) {
			wif->frag_threshold = IEEE80211_FRAG_MAX;
			return (-1);
		}
		break;
	case IEEE80211_IOC_DWDS:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_wds)) == 0) {
			wif->dynamic_wds = TruthValue_false;
			return (-1);
		}
		break;
	case IEEE80211_IOC_POWERSAVE:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_pmgt)) == 0) {
			wif->power_save = TruthValue_false;
			return (-1);
		}
		break;
	case IEEE80211_IOC_BEACON_INTERVAL:
		if (wif->mode != WlanIfaceOperatingModeType_hostAp &&
		    wif->mode != WlanIfaceOperatingModeType_meshPoint &&
		    wif->mode != WlanIfaceOperatingModeType_ibss) {
			wif->beacon_interval = 100; /* XXX */
			return (-1);
		}
		break;
	case IEEE80211_IOC_DTIM_PERIOD:
		if (wif->mode != WlanIfaceOperatingModeType_hostAp &&
		    wif->mode != WlanIfaceOperatingModeType_meshPoint &&
		    wif->mode != WlanIfaceOperatingModeType_ibss) {
			wif->dtim_period = 1; /* XXX */
			return (-1);
		}
		break;
	case IEEE80211_IOC_PUREN:
		if ((wif->htcaps & (0x1 << WlanHTCaps_htcHt)) == 0) {
			wif->dot11n_pure = TruthValue_false;
			return (-1);
		}
		break;
	case IEEE80211_IOC_AMPDU:
		if ((wif->htcaps & (0x1 << WlanHTCaps_htcAmpdu)) == 0) {
			wif->ampdu = WlanIfaceDot11nPduType_disabled;
			return (-1);
		}
		break;
	case IEEE80211_IOC_AMSDU:
		if ((wif->htcaps & (0x1 << WlanHTCaps_htcAmsdu)) == 0) {
			wif->amsdu = WlanIfaceDot11nPduType_disabled;
			return (-1);
		}
		break;
	case IEEE80211_IOC_RIFS:
		if ((wif->htcaps & (0x1 << WlanHTCaps_htcRifs)) == 0) {
			wif->rifs = TruthValue_false;
			return (-1);
		}
		break;
	case IEEE80211_IOC_SHORTGI:
		if ((wif->htcaps & (0x1 << WlanHTCaps_shortGi20 |
		    0x1 << WlanHTCaps_shortGi40)) == 0) {
			wif->short_gi = TruthValue_false;
			return (-1);
		}
		break;
	case IEEE80211_IOC_SMPS:
		if ((wif->htcaps & (0x1 << WlanHTCaps_htcSmps)) == 0) {
			wif->smps_mode = wlanIfaceDot11nSMPSMode_disabled;
			return (-1);
		}
		break;
	case IEEE80211_IOC_TDMA_SLOT:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_tdma)) == 0) {
			wif->tdma_slot = 0;
			return (-1);
		}
		break;
	case IEEE80211_IOC_TDMA_SLOTCNT:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_tdma)) == 0) {
			wif->tdma_slot_count = 0;
			return (-1);
		}
		break;
	case IEEE80211_IOC_TDMA_SLOTLEN:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_tdma)) == 0) {
			wif->tdma_slot_length = 0;
			return (-1);
		}
		break;
	case IEEE80211_IOC_TDMA_BINTERVAL:
		if ((wif->drivercaps & (0x1 << WlanDriverCaps_tdma)) == 0) {
			wif->tdma_binterval = 0;
			return (-1);
		}
		break;
	default:
		break;
	}

	return (0);
}

static int
wlan_config_get_intval(struct wlan_iface *wif, int op)
{
	int val = 0;
	size_t argsize = 0;

	if (wlan_config_check(wif, op) < 0)
		return (0);
	if (wlan_ioctl(wif->wname, op, &val, NULL, &argsize, 0) < 0)
		return (-1);
	wlan_config_set_snmp_intval(wif, op, val);

	return (0);
}

static int
wlan_config_set_intval(struct wlan_iface *wif, int op, int sval)
{
	size_t argsize = 0;
	int val;

	if (wlan_config_check(wif, op) < 0)
		return (-1);
	if (wlan_config_snmp2value(op, sval, &val) != SNMP_ERR_NOERROR)
		return (-1);
	if (wlan_ioctl(wif->wname, op, &val, NULL, &argsize, 1) < 0)
		return (-1);
	wlan_config_set_snmp_intval(wif, op, val);

	return (0);
}

int
wlan_config_get_ioctl(struct wlan_iface *wif, int which)
{
	int op;

	switch (which) {
		case LEAF_wlanIfaceCountryCode:
			/* FALLTHROUGH */
		case LEAF_wlanIfaceRegDomain:
			return (wlan_config_get_country(wif));
		case LEAF_wlanIfaceDesiredSsid:
			return (wlan_config_get_dssid(wif));
		case LEAF_wlanIfaceDesiredChannel:
			return (wlan_config_get_dchannel(wif));
		case LEAF_wlanIfaceDesiredBssid:
			return (wlan_config_get_bssid(wif));
		default:
			op = wlan_config_snmp2ioctl(which);
			return (wlan_config_get_intval(wif, op));
	}

	return (-1);
}

int
wlan_config_set_ioctl(struct wlan_iface *wif, int which, int val,
    char *strval, int len)
{
	int op;

	switch (which) {
		case LEAF_wlanIfaceCountryCode:
			return (wlan_config_set_country(wif, strval,
			    wif->reg_domain));
		case LEAF_wlanIfaceRegDomain:
			return (wlan_config_set_country(wif, wif->country_code,
			    val));
		case LEAF_wlanIfaceDesiredSsid:
			return (wlan_config_set_dssid(wif, strval, len));
		case LEAF_wlanIfaceDesiredChannel:
			return (wlan_config_set_dchannel(wif, val));
		case LEAF_wlanIfaceDesiredBssid:
			return (wlan_config_set_bssid(wif, strval));
		default:
			op = wlan_config_snmp2ioctl(which);
			return (wlan_config_set_intval(wif, op, val));
	}

	return (-1);
}

static uint32_t
wlan_snmp_to_scan_flags(int flags)
{
	int sr_flags = 0;

	if ((flags & (0x1 << WlanScanFlagsType_noSelection)) != 0)
		sr_flags |= IEEE80211_IOC_SCAN_NOPICK;
	if ((flags & (0x1 << WlanScanFlagsType_activeScan)) != 0)
		sr_flags |= IEEE80211_IOC_SCAN_ACTIVE;
	if ((flags & (0x1 << WlanScanFlagsType_pickFirst)) != 0)
		sr_flags |= IEEE80211_IOC_SCAN_PICK1ST;
	if ((flags & (0x1 << WlanScanFlagsType_backgroundScan)) != 0)
		sr_flags |= IEEE80211_IOC_SCAN_BGSCAN;
	if ((flags & (0x1 << WlanScanFlagsType_once)) != 0)
		sr_flags |= IEEE80211_IOC_SCAN_ONCE;
	if ((flags & (0x1 << WlanScanFlagsType_noBroadcast)) != 0)
		sr_flags |= IEEE80211_IOC_SCAN_NOBCAST;
	if ((flags & (0x1 << WlanScanFlagsType_noAutoSequencing)) != 0)
		sr_flags |= IEEE80211_IOC_SCAN_NOJOIN;
	if ((flags & (0x1 << WlanScanFlagsType_flushCashe)) != 0)
		sr_flags |= IEEE80211_IOC_SCAN_FLUSH;
	if ((flags & (0x1 << WlanScanFlagsType_chechCashe)) != 0)
		sr_flags |= IEEE80211_IOC_SCAN_CHECK;

	return (sr_flags);
}

int
wlan_set_scan_config(struct wlan_iface *wif)
{
	int val = 0;
	size_t argsize;
	struct ieee80211_scan_req sr;


	memset(&sr, 0, sizeof(sr));
	argsize = sizeof(struct ieee80211_scan_req);
	sr.sr_flags = wlan_snmp_to_scan_flags(wif->scan_flags);
	sr.sr_flags |= IEEE80211_IOC_SCAN_BGSCAN;
	sr.sr_duration = wif->scan_duration;
	sr.sr_mindwell = wif->scan_mindwell;
	sr.sr_maxdwell = wif->scan_maxdwell;
	sr.sr_nssid = 0;

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_SCAN_REQ,
	    &val, &sr, &argsize, 1) < 0)
		return (-1);

	wif->scan_status = wlanScanConfigStatus_running;
	return (0);
}

static uint32_t
wlan_peercaps_to_snmp(uint32_t pcaps)
{
	uint32_t scaps = 0;

	if ((pcaps & IEEE80211_CAPINFO_ESS) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_ess);
	if ((pcaps & IEEE80211_CAPINFO_IBSS) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_ibss);
	if ((pcaps & IEEE80211_CAPINFO_CF_POLLABLE) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_cfPollable);
	if ((pcaps & IEEE80211_CAPINFO_CF_POLLREQ) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_cfPollRequest);
	if ((pcaps & IEEE80211_CAPINFO_PRIVACY) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_privacy);
	if ((pcaps & IEEE80211_CAPINFO_SHORT_PREAMBLE) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_shortPreamble);
	if ((pcaps & IEEE80211_CAPINFO_PBCC) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_pbcc);
	if ((pcaps & IEEE80211_CAPINFO_CHNL_AGILITY) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_channelAgility);
	if ((pcaps & IEEE80211_CAPINFO_SHORT_SLOTTIME) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_shortSlotTime);
	if ((pcaps & IEEE80211_CAPINFO_RSN) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_rsn);
	if ((pcaps & IEEE80211_CAPINFO_DSSSOFDM) != 0)
		scaps |= (0x1 << WlanPeerCapabilityFlags_dsssofdm);

	return (scaps);
}

static int
wlan_add_new_scan_result(struct wlan_iface *wif,
    const struct ieee80211req_scan_result *isr, uint8_t *ssid)
{
	struct wlan_scan_result *sr;

	if ((sr = wlan_scan_new_result(ssid, isr->isr_bssid)) == NULL)
		return (-1);

	sr->opchannel = wlan_channel_flags_to_snmp_phy(isr->isr_flags);
	sr->rssi = isr->isr_rssi;
	sr->frequency = isr->isr_freq;
	sr->noise = isr->isr_noise;
	sr->bintval = isr->isr_intval;
	sr->capinfo = wlan_peercaps_to_snmp(isr->isr_capinfo);

	if (wlan_scan_add_result(wif, sr) < 0) {
		wlan_scan_free_result(sr);
		return (-1);
	}

	return (0);
}

int
wlan_get_scan_results(struct wlan_iface *wif)
{
	int ssidlen, val = 0;
	uint8_t buf[24 * 1024];
	size_t argsize;
	const uint8_t *cp, *idp;
	uint8_t ssid[IEEE80211_NWID_LEN + 1];
	struct ieee80211req_scan_result isr;

	argsize = sizeof(buf);
	if (wlan_ioctl(wif->wname, IEEE80211_IOC_SCAN_RESULTS, &val, &buf,
	    &argsize, 0) < 0)
		return (-1);

	if (argsize < sizeof(struct ieee80211req_scan_result))
		return (0);

	cp = buf;
	do {
		memcpy(&isr, cp, sizeof(struct ieee80211req_scan_result));
		memset(ssid, 0, IEEE80211_NWID_LEN + 1);

		if (isr.isr_meshid_len) {
			idp = cp + isr.isr_ie_off + isr.isr_ssid_len;
			ssidlen = isr.isr_meshid_len;
		} else {
			idp = cp + isr.isr_ie_off;
			ssidlen = isr.isr_ssid_len;
		}
		if (ssidlen > IEEE80211_NWID_LEN)
			ssidlen = IEEE80211_NWID_LEN;
		memcpy(ssid, idp, ssidlen);
		ssid[IEEE80211_NWID_LEN] = '\0';
		(void)wlan_add_new_scan_result(wif, &isr, ssid);
		cp += isr.isr_len;
		argsize -= isr.isr_len;
	} while (argsize >= sizeof(struct ieee80211req_scan_result));

	return (0);
}

int
wlan_get_stats(struct wlan_iface *wif)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(struct ifreq));
	strlcpy(ifr.ifr_name, wif->wname, IFNAMSIZ);

	ifr.ifr_data = (caddr_t) &wif->stats;

	if (ioctl(sock, SIOCG80211STATS, &ifr) < 0) {
		syslog(LOG_ERR, "iface %s - ioctl(SIOCG80211STATS) failed: %s",
		    wif->wname, strerror(errno));
		return (-1);
	}

	return (0);
}

int
wlan_get_wepmode(struct wlan_iface *wif)
{
	int val = 0;
	size_t argsize = 0;

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_WEP, &val, NULL,
	    &argsize, 0) < 0 || val == IEEE80211_WEP_NOSUP) {
		wif->wepsupported = 0; /* XXX */
		wif->wepmode = wlanWepMode_off;
		wif->weptxkey = 0;
		return (-1);
	}

	wif->wepsupported = 1;

	switch (val) {
	case IEEE80211_WEP_ON:
		wif->wepmode = wlanWepMode_on;
		break;
	case IEEE80211_WEP_MIXED:
		wif->wepmode = wlanWepMode_mixed;
		break;
	case IEEE80211_WEP_OFF:
		/* FALLTHROUGH */
	default:
		wif->wepmode = wlanWepMode_off;
		break;
	}

	return (0);
}

int
wlan_set_wepmode(struct wlan_iface *wif)
{
	int val;
	size_t argsize = 0;

	if (!wif->wepsupported)
		return (-1);

	switch (wif->wepmode) {
	case wlanWepMode_off:
		val = IEEE80211_WEP_OFF;
		break;
	case wlanWepMode_on:
		val = IEEE80211_WEP_ON;
		break;
	case wlanWepMode_mixed:
		val = IEEE80211_WEP_MIXED;
		break;
	default:
		return (-1);
	}

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_WEP, &val, NULL,
	    &argsize, 1) < 0)
		return (-1);

	return (0);
}

int
wlan_get_weptxkey(struct wlan_iface *wif)
{
	int val;
	size_t argsize = 0;

	if (!wif->wepsupported)
		return (0);

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_WEPTXKEY, &val, NULL,
	    &argsize, 0) < 0)
		return (-1);

	if (val == IEEE80211_KEYIX_NONE)
		wif->weptxkey = 0;
	else
		wif->weptxkey = val + 1;

	return (0);
}

int
wlan_set_weptxkey(struct wlan_iface *wif)
{
	int val;
	size_t argsize = 0;

	if (!wif->wepsupported)
		return (0);

	if (wif->weptxkey >= IEEE80211_WEP_NKID)
		return (-1);

	if (wif->weptxkey == 0)
		val = IEEE80211_KEYIX_NONE;
	else
		val = wif->weptxkey - 1;
	if (wlan_ioctl(wif->wname, IEEE80211_IOC_WEPTXKEY, &val, NULL,
	    &argsize, 1) < 0)
		return (-1);

	return (0);
}

int
wlan_get_wepkeys(struct wlan_iface *wif __unused)
{
	/* XXX: should they be visible via SNMP */
	return (0);
}

int
wlan_set_wepkeys(struct wlan_iface *wif __unused)
{
	/* XXX: should they be configurable via SNMP */
	return (0);
}

int
wlan_get_mac_policy(struct wlan_iface *wif)
{
	int val = IEEE80211_MACCMD_POLICY;
	size_t argsize = 0;
	struct ieee80211req ireq;

	memset(&ireq, 0, sizeof(struct ieee80211req));
	strlcpy(ireq.i_name, wif->wname, IFNAMSIZ);
	ireq.i_type = IEEE80211_IOC_MACCMD;
	ireq.i_val = IEEE80211_MACCMD_POLICY;

	if (ioctl(sock, SIOCG80211, &ireq) < 0) {
		if (errno != EINVAL) {
			syslog(LOG_ERR, "iface %s - get param: ioctl(%d) "
			    "failed: %s", wif->wname, ireq.i_type,
			    strerror(errno));
			wif->macsupported = 0;
			return (-1);
		} else {
			wif->macsupported = 1;
			wif->mac_policy = wlanMACAccessControlPolicy_open;
			return (0);
		}

	}

	wif->macsupported = 1;

	switch (val) {
	case IEEE80211_MACCMD_POLICY_ALLOW:
		wif->mac_policy = wlanMACAccessControlPolicy_allow;
		break;
	case IEEE80211_MACCMD_POLICY_DENY:
		wif->mac_policy = wlanMACAccessControlPolicy_deny;
		break;
	case IEEE80211_MACCMD_POLICY_RADIUS:
		wif->mac_policy = wlanMACAccessControlPolicy_radius;
		break;
	case IEEE80211_MACCMD_POLICY_OPEN:
		/* FALLTHROUGH */
	default:
		wif->mac_policy = wlanMACAccessControlPolicy_open;
		break;
	}

	argsize = 0;
	val = IEEE80211_MACCMD_LIST;
	if (wlan_ioctl(wif->wname, IEEE80211_IOC_MACCMD, &val, NULL,
	    &argsize, 0) < 0)
		return (-1);

	wif->mac_nacls = argsize / sizeof(struct ieee80211req_maclist *);
	return (0);
}

int
wlan_set_mac_policy(struct wlan_iface *wif)
{
	int val;
	size_t argsize = 0;

	if (!wif->macsupported)
		return (-1);

	switch (wif->mac_policy) {
	case wlanMACAccessControlPolicy_allow:
		val = IEEE80211_MACCMD_POLICY_ALLOW;
		break;
	case wlanMACAccessControlPolicy_deny:
		val = IEEE80211_MACCMD_POLICY_DENY;
		break;
	case wlanMACAccessControlPolicy_radius:
		val = IEEE80211_MACCMD_POLICY_RADIUS;
		break;
	case wlanMACAccessControlPolicy_open:
		val = IEEE80211_MACCMD_POLICY_OPEN;
		break;
	default:
		return (-1);
	}

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_MACCMD, &val, NULL,
	    &argsize, 1) < 0)
		return (-1);

	return (0);
}

int
wlan_flush_mac_mac(struct wlan_iface *wif)
{
	int val = IEEE80211_MACCMD_FLUSH;
	size_t argsize = 0;

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_MACCMD, &val, NULL,
	    &argsize, 1) < 0)
		return (-1);

	return (0);
}

static int
wlan_add_mac_macinfo(struct wlan_iface *wif,
    const struct ieee80211req_maclist *ml)
{
	struct wlan_mac_mac *mmac;

	if ((mmac = wlan_mac_new_mac(ml->ml_macaddr)) == NULL)
		return (-1);

	mmac->mac_status = RowStatus_active;
	if (wlan_mac_add_mac(wif, mmac) < 0) {
		wlan_mac_free_mac(mmac);
		return (-1);
	}

	return (0);
}

int
wlan_get_mac_acl_macs(struct wlan_iface *wif)
{
	int i, nacls, val = IEEE80211_MACCMD_LIST;
	size_t argsize = 0;
	uint8_t *data;
	struct ieee80211req ireq;
	const struct ieee80211req_maclist *acllist;

	if (wif->mac_policy == wlanMACAccessControlPolicy_radius) {
		wif->mac_nacls = 0;
		return (0);
	}

	memset(&ireq, 0, sizeof(struct ieee80211req));
	strlcpy(ireq.i_name, wif->wname, IFNAMSIZ);
	ireq.i_type = IEEE80211_IOC_MACCMD;
	ireq.i_val = IEEE80211_MACCMD_LIST;


	if (ioctl(sock, SIOCG80211, &ireq) < 0) {
		if (errno != EINVAL) {
			syslog(LOG_ERR, "iface %s - get param: ioctl(%d) "
			    "failed: %s", wif->wname, ireq.i_type,
			    strerror(errno));
			wif->macsupported = 0;
			return (-1);
		}
	}

	if (argsize == 0) {
		wif->mac_nacls = 0;
		return (0);
	}

	if ((data = (uint8_t *)malloc(argsize)) == NULL)
		return (-1);

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_MACCMD, &val, data,
	    &argsize, 0) < 0)
		return (-1);

	nacls = argsize / sizeof(*acllist);
	acllist = (struct ieee80211req_maclist *) data;
	for (i = 0; i < nacls; i++)
		(void)wlan_add_mac_macinfo(wif, acllist + i);

	wif->mac_nacls = nacls;
	return (0);
}

int
wlan_add_mac_acl_mac(struct wlan_iface *wif, struct wlan_mac_mac *mmac)
{
	int val = 0;
	size_t argsize = IEEE80211_ADDR_LEN;
	struct ieee80211req_mlme mlme;

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_ADDMAC, &val,
	    mmac->mac, &argsize, 1) < 0)
		return (-1);

	mmac->mac_status = RowStatus_active;

	/* If policy is deny, try to kick the station just in case. */
	if (wif->mac_policy != wlanMACAccessControlPolicy_deny)
		return (0);

	memset(&mlme, 0, sizeof(mlme));
	mlme.im_op = IEEE80211_MLME_DEAUTH;
	mlme.im_reason = IEEE80211_REASON_AUTH_EXPIRE;
	memcpy(mlme.im_macaddr, mmac->mac, IEEE80211_ADDR_LEN);
	argsize = sizeof(struct ieee80211req_mlme);

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_MLME, &val, &mlme,
	    &argsize, 1) < 0 && errno != ENOENT)
		return (-1);

	return (0);
}

int
wlan_del_mac_acl_mac(struct wlan_iface *wif, struct wlan_mac_mac *mmac)
{
	int val = 0;
	size_t argsize = IEEE80211_ADDR_LEN;
	struct ieee80211req_mlme mlme;

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_DELMAC, &val,
	    mmac->mac, &argsize, 1) < 0)
		return (-1);

	mmac->mac_status = RowStatus_active;

	/* If policy is allow, try to kick the station just in case. */
	if (wif->mac_policy != wlanMACAccessControlPolicy_allow)
		return (0);

	memset(&mlme, 0, sizeof(mlme));
	mlme.im_op = IEEE80211_MLME_DEAUTH;
	mlme.im_reason = IEEE80211_REASON_AUTH_EXPIRE;
	memcpy(mlme.im_macaddr, mmac->mac, IEEE80211_ADDR_LEN);
	argsize = sizeof(struct ieee80211req_mlme);

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_MLME, &val, &mlme,
	    &argsize, 1) < 0 && errno != ENOENT)
		return (-1);

	return (0);
}

int
wlan_peer_set_vlan(struct wlan_iface *wif, struct wlan_peer *wip, int vlan)
{
	int val = 0;
	size_t argsize;
	struct ieee80211req_sta_vlan vreq;

	memcpy(vreq.sv_macaddr, wip->pmac, IEEE80211_ADDR_LEN);
	vreq.sv_vlan = vlan;
	argsize = sizeof(struct ieee80211req_sta_vlan);

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_STA_VLAN,
	    &val, &vreq, &argsize, 1) < 0)
		return (-1);

	wip->vlan = vlan;

	return (0);
}

/* XXX */
#ifndef IEEE80211_NODE_AUTH
#define	IEEE80211_NODE_AUTH	0x000001	/* authorized for data */
#define	IEEE80211_NODE_QOS	0x000002	/* QoS enabled */
#define	IEEE80211_NODE_ERP	0x000004	/* ERP enabled */
#define	IEEE80211_NODE_PWR_MGT	0x000010	/* power save mode enabled */
#define	IEEE80211_NODE_AREF	0x000020	/* authentication ref held */
#define	IEEE80211_NODE_HT	0x000040	/* HT enabled */
#define	IEEE80211_NODE_HTCOMPAT	0x000080	/* HT setup w/ vendor OUI's */
#define	IEEE80211_NODE_WPS	0x000100	/* WPS association */
#define	IEEE80211_NODE_TSN	0x000200	/* TSN association */
#define	IEEE80211_NODE_AMPDU_RX	0x000400	/* AMPDU rx enabled */
#define	IEEE80211_NODE_AMPDU_TX	0x000800	/* AMPDU tx enabled */
#define	IEEE80211_NODE_MIMO_PS	0x001000	/* MIMO power save enabled */
#define	IEEE80211_NODE_MIMO_RTS	0x002000	/* send RTS in MIMO PS */
#define	IEEE80211_NODE_RIFS	0x004000	/* RIFS enabled */
#define	IEEE80211_NODE_SGI20	0x008000	/* Short GI in HT20 enabled */
#define	IEEE80211_NODE_SGI40	0x010000	/* Short GI in HT40 enabled */
#define	IEEE80211_NODE_ASSOCID	0x020000	/* xmit requires associd */
#define	IEEE80211_NODE_AMSDU_RX	0x040000	/* AMSDU rx enabled */
#define	IEEE80211_NODE_AMSDU_TX	0x080000	/* AMSDU tx enabled */
#endif

static uint32_t
wlan_peerstate_to_snmp(uint32_t pstate)
{
	uint32_t sstate = 0;

	if ((pstate & IEEE80211_NODE_AUTH) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_authorizedForData);
	if ((pstate & IEEE80211_NODE_QOS) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_qosEnabled);
	if ((pstate & IEEE80211_NODE_ERP) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_erpEnabled);
	if ((pstate & IEEE80211_NODE_PWR_MGT) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_powerSaveMode);
	if ((pstate & IEEE80211_NODE_AREF) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_authRefHeld);
	if ((pstate & IEEE80211_NODE_HT) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_htEnabled);
	if ((pstate & IEEE80211_NODE_HTCOMPAT) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_htCompat);
	if ((pstate & IEEE80211_NODE_WPS) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_wpsAssoc);
	if ((pstate & IEEE80211_NODE_TSN) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_tsnAssoc);
	if ((pstate & IEEE80211_NODE_AMPDU_RX) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_ampduRx);
	if ((pstate & IEEE80211_NODE_AMPDU_TX) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_ampduTx);
	if ((pstate & IEEE80211_NODE_MIMO_PS) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_mimoPowerSave);
	if ((pstate & IEEE80211_NODE_MIMO_RTS) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_sendRts);
	if ((pstate & IEEE80211_NODE_RIFS) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_rifs);
	if ((pstate & IEEE80211_NODE_SGI20) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_shortGiHT20);
	if ((pstate & IEEE80211_NODE_SGI40) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_shortGiHT40);
	if ((pstate & IEEE80211_NODE_AMSDU_RX) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_amsduRx);
	if ((pstate & IEEE80211_NODE_AMSDU_TX) != 0)
		sstate |= (0x1 << WlanIfacePeerFlagsType_amsduTx);

	return (sstate);
}

static struct wlan_peer *
wlan_add_peerinfo(const struct ieee80211req_sta_info *si)
{
	struct wlan_peer *wip;

	if ((wip = wlan_new_peer(si->isi_macaddr))== NULL)
		return (NULL);

	wip->associd = IEEE80211_AID(si->isi_associd);
	wip->vlan = si->isi_vlan;
	wip->frequency =  si->isi_freq;
	wip->fflags = si->isi_flags;
	wip->txrate = si->isi_txrate;
	wip->rssi = si->isi_rssi;
	wip->idle = si->isi_inact;
	wip->txseqs = si->isi_txseqs[0]; /* XXX */
	wip->rxseqs = si->isi_rxseqs[0]; /* XXX */
	wip->txpower = si->isi_txpower;
	wip->capinfo = wlan_peercaps_to_snmp(si->isi_capinfo);
	wip->state = wlan_peerstate_to_snmp(si->isi_state);
	wip->local_id = si->isi_localid;
	wip->peer_id = si->isi_peerid;

	return (wip);
}

int
wlan_get_peerinfo(struct wlan_iface *wif)
{
	union {
		struct ieee80211req_sta_req req;
		uint8_t buf[24 * 1024];
	} u;
	const uint8_t *cp;
	int val = 0;
	size_t len;
	struct ieee80211req_sta_info si;
	struct wlan_peer *wip;

	/* Get all stations - broadcast address */
	(void) memset(u.req.is_u.macaddr, 0xff, IEEE80211_ADDR_LEN);
	len =  sizeof(u);

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_STA_INFO,
	    & val, &u, &len, 0) < 0)
		return (-1);

	if (len < sizeof(struct ieee80211req_sta_info))
		return (-1);

	cp = (const uint8_t *) u.req.info;
	do {
		memcpy(&si, cp, sizeof(struct ieee80211req_sta_info));
		if ((wip = wlan_add_peerinfo(&si)) != NULL &&
		    wlan_add_peer(wif, wip) < 0)
			wlan_free_peer(wip);
		cp += si.isi_len, len -= si.isi_len;
	} while (len >= sizeof(struct ieee80211req_sta_info));

	return (0);
}

/************************************************************************
 * Wireless MESH & HWMP sysctl config.
 */
const char wlan_sysctl_name[] = "net.wlan.";

static const char *wlan_sysctl[] = {
	"mesh.retrytimeout",
	"mesh.holdingtimeout",
	"mesh.confirmtimeout",
	"mesh.maxretries",
	"hwmp.targetonly",
	"hwmp.replyforward",
	"hwmp.pathlifetime",
	"hwmp.roottimeout",
	"hwmp.rootint",
	"hwmp.rannint",
	"hwmp.inact",
};

int32_t
wlan_do_sysctl(struct wlan_config *cfg, enum wlan_syscl which, int set)
{
	char mib_name[100];
	int val, sval;
	size_t len, vlen;

	if (set) {
		vlen = sizeof(sval);
		switch (which) {
		case WLAN_MESH_RETRY_TO:
			sval = cfg->mesh_retryto;
			break;
		case WLAN_MESH_HOLDING_TO:
			sval = cfg->mesh_holdingto;
			break;
		case WLAN_MESH_CONFIRM_TO:
			sval = cfg->mesh_confirmto;
			break;
		case WLAN_MESH_MAX_RETRIES:
			sval = cfg->mesh_maxretries;
			break;
		case WLAN_HWMP_TARGET_ONLY:
			sval = cfg->hwmp_targetonly;
			break;
		case WLAN_HWMP_REPLY_FORWARD:
			sval = cfg->hwmp_replyforward;
			break;
		case WLAN_HWMP_PATH_LIFETIME:
			sval = cfg->hwmp_pathlifetime;
			break;
		case WLAN_HWMP_ROOT_TO:
			sval = cfg->hwmp_roottimeout;
			break;
		case WLAN_HWMP_ROOT_INT:
			sval = cfg->hwmp_rootint;
			break;
		case WLAN_HWMP_RANN_INT:
			sval = cfg->hwmp_rannint;
			break;
		case WLAN_HWMP_INACTIVITY_TO:
			sval = cfg->hwmp_inact;
			break;
		default:
			return (-1);
		}
	} else {
		if (which >= WLAN_SYSCTL_MAX)
			return (-1);
		vlen = 0;
	}

	strlcpy(mib_name, wlan_sysctl_name, sizeof(mib_name));
	strlcat(mib_name, wlan_sysctl[which], sizeof(mib_name));
	len = sizeof (val);

	if (sysctlbyname(mib_name, &val, &len, (set? &sval : NULL), vlen) < 0) {
		syslog(LOG_ERR, "sysctl(%s) failed - %s", mib_name,
		    strerror(errno));
		return (-1);
	}

	switch (which) {
	case WLAN_MESH_RETRY_TO:
		cfg->mesh_retryto = val;
		break;
	case WLAN_MESH_HOLDING_TO:
		cfg->mesh_holdingto = val;
		break;
	case WLAN_MESH_CONFIRM_TO:
		cfg->mesh_confirmto = val;
		break;
	case WLAN_MESH_MAX_RETRIES:
		cfg->mesh_maxretries = val;
		break;
	case WLAN_HWMP_TARGET_ONLY:
		cfg->hwmp_targetonly = val;
		break;
	case WLAN_HWMP_REPLY_FORWARD:
		cfg->hwmp_replyforward = val;
		break;
	case WLAN_HWMP_PATH_LIFETIME:
		cfg->hwmp_pathlifetime = val;
		break;
	case WLAN_HWMP_ROOT_TO:
		cfg->hwmp_roottimeout = val;
		break;
	case WLAN_HWMP_ROOT_INT:
		cfg->hwmp_rootint = val;
		break;
	case WLAN_HWMP_RANN_INT:
		cfg->hwmp_rannint = val;
		break;
	case WLAN_HWMP_INACTIVITY_TO:
		cfg->hwmp_inact = val;
		break;
	default:
		/* NOTREACHED */
		abort();
	}

	return (0);
}

int
wlan_mesh_config_get(struct wlan_iface *wif, int which)
{
	int op, val = 0;
	size_t argsize = 0;
	uint8_t data[32], *pd = NULL;

	switch (which) {
	case LEAF_wlanMeshTTL:
		op = IEEE80211_IOC_MESH_TTL;
		break;
	case LEAF_wlanMeshPeeringEnabled:
		op = IEEE80211_IOC_MESH_AP;
		break;
	case LEAF_wlanMeshForwardingEnabled:
		op = IEEE80211_IOC_MESH_FWRD;
		break;
	case LEAF_wlanMeshMetric:
		op = IEEE80211_IOC_MESH_PR_METRIC;
		pd = data;
		argsize = sizeof(data);
		break;
	case LEAF_wlanMeshPath:
		op = IEEE80211_IOC_MESH_PR_PATH;
		pd = data;
		argsize = sizeof(data);
		break;
	case LEAF_wlanMeshRoutesFlush:
		return (0);
	default:
		return (-1);
	}

	if (wlan_ioctl(wif->wname, op, &val, pd, &argsize, 0) < 0)
		return (-1);

	switch (which) {
	case LEAF_wlanMeshTTL:
		wif->mesh_ttl = val;
		break;
	case LEAF_wlanMeshPeeringEnabled:
		if (val)
			wif->mesh_peering = wlanMeshPeeringEnabled_true;
		else
			wif->mesh_peering = wlanMeshPeeringEnabled_false;
		break;
	case LEAF_wlanMeshForwardingEnabled:
		if (val)
			wif->mesh_forwarding = wlanMeshForwardingEnabled_true;
		else
			wif->mesh_forwarding = wlanMeshForwardingEnabled_false;
		break;
	case LEAF_wlanMeshMetric:
		data[argsize] = '\0';
		if (strcmp(data, "AIRTIME") == 0)
			wif->mesh_metric = wlanMeshMetric_airtime;
		else
			wif->mesh_metric = wlanMeshMetric_unknown;
		break;
	case LEAF_wlanMeshPath:
		data[argsize] = '\0';
		if (strcmp(data, "HWMP") == 0)
			wif->mesh_path = wlanMeshPath_hwmp;
		else
			wif->mesh_path = wlanMeshPath_unknown;
	}

	return (0);
}

int
wlan_mesh_config_set(struct wlan_iface *wif, int which)
{
	int op, val = 0;
	size_t argsize = 0;
	uint8_t data[32], *pd = NULL;

	switch (which) {
	case LEAF_wlanMeshTTL:
		op = IEEE80211_IOC_MESH_TTL;
		val = wif->mesh_ttl;
		break;
	case LEAF_wlanMeshPeeringEnabled:
		op = IEEE80211_IOC_MESH_AP;
		if (wif->mesh_peering == wlanMeshPeeringEnabled_true)
			val = 1;
		break;
	case LEAF_wlanMeshForwardingEnabled:
		if (wif->mesh_forwarding == wlanMeshForwardingEnabled_true)
			val = 1;
		op = IEEE80211_IOC_MESH_FWRD;
		break;
	case LEAF_wlanMeshMetric:
		op = IEEE80211_IOC_MESH_PR_METRIC;
		if (wif->mesh_metric == wlanMeshMetric_airtime)
			strcpy(data, "AIRTIME");
		else
			return (-1);
		pd = data;
		argsize = sizeof(data);
		break;
	case LEAF_wlanMeshPath:
		op = IEEE80211_IOC_MESH_PR_PATH;
		if (wif->mesh_path == wlanMeshPath_hwmp)
			strcpy(data, "HWMP");
		else
			return (-1);
		pd = data;
		argsize = sizeof(data);
		break;
	default:
		return (-1);
	}

	if (wlan_ioctl(wif->wname, op, &val, pd, &argsize, 1) < 0)
		return (-1);

	return(0);
}

int
wlan_mesh_flush_routes(struct wlan_iface *wif)
{
	int val = IEEE80211_MESH_RTCMD_FLUSH;
	size_t argsize = 0;

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_MESH_RTCMD, &val, NULL,
	    &argsize, 1) < 0)
		return (-1);

	return (0);
}

int
wlan_mesh_add_route(struct wlan_iface *wif, struct wlan_mesh_route *wmr)
{
	int val = IEEE80211_MESH_RTCMD_ADD;
	size_t argsize = IEEE80211_ADDR_LEN;

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_MESH_RTCMD, &val,
	    wmr->imroute.imr_dest, &argsize, 1) < 0)
		return (-1);

	wmr->mroute_status = RowStatus_active;

	return (0);
}

int
wlan_mesh_del_route(struct wlan_iface *wif, struct wlan_mesh_route *wmr)
{
	int val = IEEE80211_MESH_RTCMD_DELETE;
	size_t argsize = IEEE80211_ADDR_LEN;

	if (wlan_ioctl(wif->wname, IEEE80211_IOC_MESH_RTCMD, &val,
	    wmr->imroute.imr_dest, &argsize, 1) < 0)
		return (-1);

	wmr->mroute_status = RowStatus_destroy;

	return (0);
}

int
wlan_mesh_get_routelist(struct wlan_iface *wif)
{
	int i, nroutes, val = IEEE80211_MESH_RTCMD_LIST;
	size_t argsize;
	struct ieee80211req_mesh_route routes[128];
	struct ieee80211req_mesh_route *rt;
	struct wlan_mesh_route *wmr;

	argsize = sizeof(routes);
	if (wlan_ioctl(wif->wname, IEEE80211_IOC_MESH_RTCMD, &val, routes,
	    &argsize, 0) < 0) /* XXX: ENOMEM? */
		return (-1);

	nroutes = argsize / sizeof(*rt);
	for (i = 0; i < nroutes; i++) {
		rt = routes + i;
		if ((wmr = wlan_mesh_new_route(rt->imr_dest)) == NULL)
			return (-1);
		memcpy(&wmr->imroute, rt, sizeof(*rt));
		wmr->mroute_status = RowStatus_active;
		if (wlan_mesh_add_rtentry(wif, wmr) < 0)
			wlan_mesh_free_route(wmr);
	}

	return (0);
}

int
wlan_hwmp_config_get(struct wlan_iface *wif, int which)
{
	int op, val = 0;
	size_t argsize = 0;

	switch (which) {
	case LEAF_wlanHWMPRootMode:
		op = IEEE80211_IOC_HWMP_ROOTMODE;
		break;
	case LEAF_wlanHWMPMaxHops:
		op = IEEE80211_IOC_HWMP_MAXHOPS;
		break;
	default:
		return (-1);
	}

	if (wlan_ioctl(wif->wname, op, &val, NULL, &argsize, 0) < 0)
		return (-1);

	switch (which) {
	case LEAF_wlanHWMPRootMode:
		switch (val) {
		case IEEE80211_HWMP_ROOTMODE_NORMAL:
			wif->hwmp_root_mode = wlanHWMPRootMode_normal;
			break;
		case IEEE80211_HWMP_ROOTMODE_PROACTIVE:
			wif->hwmp_root_mode = wlanHWMPRootMode_proactive;
			break;
		case IEEE80211_HWMP_ROOTMODE_RANN:
			wif->hwmp_root_mode = wlanHWMPRootMode_rann;
			break;
		case IEEE80211_HWMP_ROOTMODE_DISABLED:
		default:
			wif->hwmp_root_mode = wlanHWMPRootMode_disabled;
			break;
		}
		break;
	case LEAF_wlanHWMPMaxHops:
		wif->hwmp_max_hops = val;
		break;
	}

	return (0);
}

int
wlan_hwmp_config_set(struct wlan_iface *wif, int which)
{
	int op, val = 0;
	size_t argsize = 0;

	switch (which) {
	case LEAF_wlanHWMPRootMode:
		op = IEEE80211_IOC_HWMP_ROOTMODE;
		switch (wif->hwmp_root_mode) {
		case wlanHWMPRootMode_disabled:
			val = IEEE80211_HWMP_ROOTMODE_DISABLED;
			break;
		case wlanHWMPRootMode_normal:
			val = IEEE80211_HWMP_ROOTMODE_NORMAL;
			break;
		case wlanHWMPRootMode_proactive:
			val = IEEE80211_HWMP_ROOTMODE_PROACTIVE;
			break;
		case wlanHWMPRootMode_rann:
			val = IEEE80211_HWMP_ROOTMODE_RANN;
			break;
		default:
			return (-1);
		}
		break;
	case LEAF_wlanHWMPMaxHops:
		op = IEEE80211_IOC_HWMP_MAXHOPS;
		val = wif->hwmp_max_hops;
		break;
	default:
		return (-1);
	}

	if (wlan_ioctl(wif->wname, op, &val, NULL, &argsize, 1) < 0)
		return (-1);

	return (0);
}
