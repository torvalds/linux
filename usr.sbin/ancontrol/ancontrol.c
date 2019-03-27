/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 1997, 1998, 1999
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#if 0
#ifndef lint
static const char copyright[] = "@(#) Copyright (c) 1997, 1998, 1999\
	Bill Paul. All rights reserved.";
#endif
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <arpa/inet.h>

#include <net/if.h>
#include <net/ethernet.h>

#include <dev/an/if_aironet_ieee.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <md4.h>
#include <ctype.h>

static int an_getval(const char *, struct an_req *);
static void an_setval(const char *, struct an_req *);
static void an_printwords(const u_int16_t *, int);
static void an_printspeeds(const u_int8_t *, int);
static void an_printbool(int);
static void an_printhex(const char *, int);
static void an_printstr(char *, int);
static void an_dumpstatus(const char *);
static void an_dumpstats(const char *);
static void an_dumpconfig(const char *);
static void an_dumpcaps(const char *);
static void an_dumpssid(const char *);
static void an_dumpap(const char *);
static void an_setconfig(const char *, int, void *);
static void an_setssid(const char *, int, void *);
static void an_setap(const char *, int, void *);
static void an_setspeed(const char *, int, void *);
static void an_readkeyinfo(const char *);
#ifdef ANCACHE
static void an_zerocache(const char *);
static void an_readcache(const char *);
#endif
static int an_hex2int(char);
static void an_str2key(const char *, struct an_ltv_key *);
static void an_setkeys(const char *, const char *, int);
static void an_enable_tx_key(const char *, const char *);
static void an_enable_leap_mode(const char *, const char *);
static void an_dumprssimap(const char *);
static void usage(const char *);

#define ACT_DUMPSTATS 1
#define ACT_DUMPCONFIG 2
#define ACT_DUMPSTATUS 3
#define ACT_DUMPCAPS 4
#define ACT_DUMPSSID 5
#define ACT_DUMPAP 6

#define ACT_SET_OPMODE 7
#define ACT_SET_SSID 8
#define ACT_SET_FREQ 11
#define ACT_SET_AP1 12
#define ACT_SET_AP2 13
#define ACT_SET_AP3 14
#define ACT_SET_AP4 15
#define ACT_SET_DRIVERNAME 16
#define ACT_SET_SCANMODE 17
#define ACT_SET_TXRATE 18
#define ACT_SET_RTS_THRESH 19
#define ACT_SET_PWRSAVE 20
#define ACT_SET_DIVERSITY_RX 21
#define ACT_SET_DIVERSITY_TX 22
#define ACT_SET_RTS_RETRYLIM 23
#define ACT_SET_WAKE_DURATION 24
#define ACT_SET_BEACON_PERIOD 25
#define ACT_SET_TXPWR 26
#define ACT_SET_FRAG_THRESH 27
#define ACT_SET_NETJOIN 28
#define ACT_SET_MYNAME 29
#define ACT_SET_MAC 30

#define ACT_DUMPCACHE 31
#define ACT_ZEROCACHE 32

#define ACT_ENABLE_WEP 33
#define ACT_SET_KEY_TYPE 34
#define ACT_SET_KEYS 35
#define ACT_ENABLE_TX_KEY 36
#define ACT_SET_MONITOR_MODE 37
#define ACT_SET_LEAP_MODE 38

#define ACT_DUMPRSSIMAP 39

static int
an_getval(const char *iface, struct an_req *areq)
{
	struct ifreq		ifr;
	int			s, okay = 1;

	bzero(&ifr, sizeof(ifr));

	strlcpy(ifr.ifr_name, iface, sizeof(ifr.ifr_name));
	ifr.ifr_data = (caddr_t)areq;

	s = socket(AF_INET, SOCK_DGRAM, 0);

	if (s == -1)
		err(1, "socket");

	if (ioctl(s, SIOCGAIRONET, &ifr) == -1) {
		okay = 0;
		err(1, "SIOCGAIRONET");
	}

	close(s);

	return (okay);
}

static void
an_setval(const char *iface, struct an_req *areq)
{
	struct ifreq		ifr;
	int			s;

	bzero(&ifr, sizeof(ifr));

	strlcpy(ifr.ifr_name, iface, sizeof(ifr.ifr_name));
	ifr.ifr_data = (caddr_t)areq;

	s = socket(AF_INET, SOCK_DGRAM, 0);

	if (s == -1)
		err(1, "socket");

	if (ioctl(s, SIOCSAIRONET, &ifr) == -1)
		err(1, "SIOCSAIRONET");

	close(s);

	return;
}

static void
an_printstr(char *str, int len)
{
	int			i;

	for (i = 0; i < len - 1; i++) {
		if (str[i] == '\0')
			str[i] = ' ';
	}

	printf("[ %.*s ]", len, str);
}

static void
an_printwords(const u_int16_t *w, int len)
{
	int			i;

	printf("[ ");
	for (i = 0; i < len; i++)
		printf("%u ", w[i]);
	printf("]");
}

static void
an_printspeeds(const u_int8_t *w, int len)
{
	int			i;

	printf("[ ");
	for (i = 0; i < len && w[i]; i++)
		printf("%2.1fMbps ", w[i] * 0.500);
	printf("]");
}

static void
an_printbool(int val)
{
	if (val)
		printf("[ On ]");
	else
		printf("[ Off ]");
}

static void
an_printhex(const char *ptr, int len)
{
	int			i;

	printf("[ ");
	for (i = 0; i < len; i++) {
		printf("%02x", ptr[i] & 0xFF);
		if (i < (len - 1))
			printf(":");
	}

	printf(" ]");
}



static void
an_dumpstatus(const char *iface)
{
	struct an_ltv_status	*sts;
	struct an_req		areq;
	struct an_ltv_rssi_map	an_rssimap;
	int rssimap_valid = 0;

	/*
	 * Try to get RSSI to percent and dBM table
	 */

	an_rssimap.an_len = sizeof(an_rssimap);
	an_rssimap.an_type = AN_RID_RSSI_MAP;
	rssimap_valid = an_getval(iface, (struct an_req*)&an_rssimap);	

	if (rssimap_valid)
		printf("RSSI table:\t\t[ present ]\n");
	else
		printf("RSSI table:\t\t[ not available ]\n");

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_STATUS;

	an_getval(iface, &areq);

	sts = (struct an_ltv_status *)&areq;

	printf("MAC address:\t\t");
	an_printhex((char *)&sts->an_macaddr, ETHER_ADDR_LEN);
	printf("\nOperating mode:\t\t[ ");
	if (sts->an_opmode & AN_STATUS_OPMODE_CONFIGURED)
		printf("configured ");
	if (sts->an_opmode & AN_STATUS_OPMODE_MAC_ENABLED)
		printf("MAC ON ");
	if (sts->an_opmode & AN_STATUS_OPMODE_RX_ENABLED)
		printf("RX ON ");
	if (sts->an_opmode & AN_STATUS_OPMODE_IN_SYNC)
		printf("synced ");
	if (sts->an_opmode & AN_STATUS_OPMODE_ASSOCIATED)
		printf("associated ");
	if (sts->an_opmode & AN_STATUS_OPMODE_LEAP)
		printf("LEAP ");
	if (sts->an_opmode & AN_STATUS_OPMODE_ERROR)
		printf("error ");
	printf("]\n");
	printf("Error code:\t\t");
	an_printhex((char *)&sts->an_errcode, 1);
	if (rssimap_valid)
		printf("\nSignal strength:\t[ %u%% ]",
		    an_rssimap.an_entries[
			sts->an_normalized_strength].an_rss_pct);
	else 
		printf("\nSignal strength:\t[ %u%% ]",
		    sts->an_normalized_strength);
	printf("\nAverage Noise:\t\t[ %u%% ]", sts->an_avg_noise_prev_min_pc);
	if (rssimap_valid)
		printf("\nSignal quality:\t\t[ %u%% ]", 
		    an_rssimap.an_entries[
			sts->an_cur_signal_quality].an_rss_pct);
	else 
		printf("\nSignal quality:\t\t[ %u ]", 
		    sts->an_cur_signal_quality);
	printf("\nMax Noise:\t\t[ %u%% ]", sts->an_max_noise_prev_min_pc);
	/*
	 * XXX: This uses the old definition of the rate field (units of
	 * 500kbps).  Technically the new definition is that this field
	 * contains arbitrary values, but no devices which need this
	 * support exist and the IEEE seems to intend to use the old
	 * definition until they get something big so we'll keep using
	 * it as well because this will work with new cards with
	 * rate <= 63.5Mbps.
	 */
	printf("\nCurrent TX rate:\t[ %u%s ]", sts->an_current_tx_rate / 2,
	    (sts->an_current_tx_rate % 2) ? ".5" : "");
	printf("\nCurrent SSID:\t\t");
	an_printstr((char *)&sts->an_ssid, sts->an_ssidlen);
	printf("\nCurrent AP name:\t");
	an_printstr((char *)&sts->an_ap_name, 16);
	printf("\nCurrent BSSID:\t\t");
	an_printhex((char *)&sts->an_cur_bssid, ETHER_ADDR_LEN);
	printf("\nBeacon period:\t\t");
	an_printwords(&sts->an_beacon_period, 1);
	printf("\nDTIM period:\t\t");
	an_printwords(&sts->an_dtim_period, 1);
	printf("\nATIM duration:\t\t");
	an_printwords(&sts->an_atim_duration, 1);
	printf("\nHOP period:\t\t");
	an_printwords(&sts->an_hop_period, 1);
	printf("\nChannel set:\t\t");
	an_printwords(&sts->an_channel_set, 1);
	printf("\nCurrent channel:\t");
	an_printwords(&sts->an_cur_channel, 1);
	printf("\nHops to backbone:\t");
	an_printwords(&sts->an_hops_to_backbone, 1);
	printf("\nTotal AP load:\t\t");
	an_printwords(&sts->an_ap_total_load, 1);
	printf("\nOur generated load:\t");
	an_printwords(&sts->an_our_generated_load, 1);
	printf("\nAccumulated ARL:\t");
	an_printwords(&sts->an_accumulated_arl, 1);
	printf("\n");
	return;
}

static void
an_dumpcaps(const char *iface)
{
	struct an_ltv_caps	*caps;
	struct an_req		areq;
	u_int16_t		tmp;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_CAPABILITIES;

	an_getval(iface, &areq);

	caps = (struct an_ltv_caps *)&areq;

	printf("OUI:\t\t\t");
	an_printhex((char *)&caps->an_oui, 3);
	printf("\nProduct number:\t\t");
	an_printwords(&caps->an_prodnum, 1);
	printf("\nManufacturer name:\t");
	an_printstr((char *)&caps->an_manufname, 32);
	printf("\nProduce name:\t\t");
	an_printstr((char *)&caps->an_prodname, 16);
	printf("\nFirmware version:\t");
	an_printstr((char *)&caps->an_prodvers, 1);
	printf("\nOEM MAC address:\t");
	an_printhex((char *)&caps->an_oemaddr, ETHER_ADDR_LEN);
	printf("\nAironet MAC address:\t");
	an_printhex((char *)&caps->an_aironetaddr, ETHER_ADDR_LEN);
	printf("\nRadio type:\t\t[ ");
	if (caps->an_radiotype & AN_RADIOTYPE_80211_FH)
		printf("802.11 FH");
	else if (caps->an_radiotype & AN_RADIOTYPE_80211_DS)
		printf("802.11 DS");
	else if (caps->an_radiotype & AN_RADIOTYPE_LM2000_DS)
		printf("LM2000 DS");
	else
		printf("unknown (%x)", caps->an_radiotype);
	printf(" ]");
	printf("\nRegulatory domain:\t");
	an_printwords(&caps->an_regdomain, 1);
	printf("\nAssigned CallID:\t");
	an_printhex((char *)&caps->an_callid, 6);
	printf("\nSupported speeds:\t");
	an_printspeeds(caps->an_rates, 8);
	printf("\nRX Diversity:\t\t[ ");
	if (caps->an_rx_diversity == AN_DIVERSITY_FACTORY_DEFAULT)
		printf("factory default");
	else if (caps->an_rx_diversity == AN_DIVERSITY_ANTENNA_1_ONLY)
		printf("antenna 1 only");
	else if (caps->an_rx_diversity == AN_DIVERSITY_ANTENNA_2_ONLY)
		printf("antenna 2 only");
	else if (caps->an_rx_diversity == AN_DIVERSITY_ANTENNA_1_AND_2)
		printf("antenna 1 and 2");
	printf(" ]");
	printf("\nTX Diversity:\t\t[ ");
	if (caps->an_tx_diversity == AN_DIVERSITY_FACTORY_DEFAULT)
		printf("factory default");
	else if (caps->an_tx_diversity == AN_DIVERSITY_ANTENNA_1_ONLY)
		printf("antenna 1 only");
	else if (caps->an_tx_diversity == AN_DIVERSITY_ANTENNA_2_ONLY)
		printf("antenna 2 only");
	else if (caps->an_tx_diversity == AN_DIVERSITY_ANTENNA_1_AND_2)
		printf("antenna 1 and 2");
	printf(" ]");
	printf("\nSupported power levels:\t");
	an_printwords(caps->an_tx_powerlevels, 8);
	printf("\nHardware revision:\t");
	tmp = ntohs(caps->an_hwrev);
	an_printhex((char *)&tmp, 2);
	printf("\nSoftware revision:\t");
	tmp = ntohs(caps->an_fwrev);
	an_printhex((char *)&tmp, 2);
	printf("\nSoftware subrevision:\t");
	tmp = ntohs(caps->an_fwsubrev);
	an_printhex((char *)&tmp, 2);
	printf("\nInterface revision:\t");
	tmp = ntohs(caps->an_ifacerev);
	an_printhex((char *)&tmp, 2);
	printf("\nBootblock revision:\t");
	tmp = ntohs(caps->an_bootblockrev);
	an_printhex((char *)&tmp, 2);
	printf("\n");
	return;
}

static void
an_dumpstats(const char *iface)
{
	struct an_ltv_stats	*stats;
	struct an_req		areq;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_32BITS_CUM;

	an_getval(iface, &areq);

	stats = (struct an_ltv_stats *)((uint16_t *)&areq - 1);

	printf("RX overruns:\t\t\t\t\t[ %u ]\n", stats->an_rx_overruns);
	printf("RX PLCP CSUM errors:\t\t\t\t[ %u ]\n",
	    stats->an_rx_plcp_csum_errs);
	printf("RX PLCP format errors:\t\t\t\t[ %u ]\n",
	    stats->an_rx_plcp_format_errs);
	printf("RX PLCP length errors:\t\t\t\t[ %u ]\n",
	    stats->an_rx_plcp_len_errs);
	printf("RX MAC CRC errors:\t\t\t\t[ %u ]\n",
	    stats->an_rx_mac_crc_errs);
	printf("RX MAC CRC OK:\t\t\t\t\t[ %u ]\n",
	    stats->an_rx_mac_crc_ok);
	printf("RX WEP errors:\t\t\t\t\t[ %u ]\n",
	    stats->an_rx_wep_errs);
	printf("RX WEP OK:\t\t\t\t\t[ %u ]\n",
	    stats->an_rx_wep_ok);
	printf("Long retries:\t\t\t\t\t[ %u ]\n",
	    stats->an_retry_long);
	printf("Short retries:\t\t\t\t\t[ %u ]\n",
	    stats->an_retry_short);
	printf("Retries exhausted:\t\t\t\t[ %u ]\n",
	    stats->an_retry_max);
	printf("Bad ACK:\t\t\t\t\t[ %u ]\n",
	    stats->an_no_ack);
	printf("Bad CTS:\t\t\t\t\t[ %u ]\n",
	    stats->an_no_cts);
	printf("RX good ACKs:\t\t\t\t\t[ %u ]\n",
	    stats->an_rx_ack_ok);
	printf("RX good CTSs:\t\t\t\t\t[ %u ]\n",
	    stats->an_rx_cts_ok);
	printf("TX good ACKs:\t\t\t\t\t[ %u ]\n",
	    stats->an_tx_ack_ok);
	printf("TX good RTSs:\t\t\t\t\t[ %u ]\n",
	    stats->an_tx_rts_ok);
	printf("TX good CTSs:\t\t\t\t\t[ %u ]\n",
	    stats->an_tx_cts_ok);
	printf("LMAC multicasts transmitted:\t\t\t[ %u ]\n",
	    stats->an_tx_lmac_mcasts);
	printf("LMAC broadcasts transmitted:\t\t\t[ %u ]\n",
	    stats->an_tx_lmac_bcasts);
	printf("LMAC unicast frags transmitted:\t\t\t[ %u ]\n",
	    stats->an_tx_lmac_ucast_frags);
	printf("LMAC unicasts transmitted:\t\t\t[ %u ]\n",
	    stats->an_tx_lmac_ucasts);
	printf("Beacons transmitted:\t\t\t\t[ %u ]\n",
	    stats->an_tx_beacons);
	printf("Beacons received:\t\t\t\t[ %u ]\n",
	    stats->an_rx_beacons);
	printf("Single transmit collisions:\t\t\t[ %u ]\n",
	    stats->an_tx_single_cols);
	printf("Multiple transmit collisions:\t\t\t[ %u ]\n",
	    stats->an_tx_multi_cols);
	printf("Transmits without deferrals:\t\t\t[ %u ]\n",
	    stats->an_tx_defers_no);
	printf("Transmits deferred due to protocol:\t\t[ %u ]\n",
	    stats->an_tx_defers_prot);
	printf("Transmits deferred due to energy detect:\t\t[ %u ]\n",
	    stats->an_tx_defers_energy);
	printf("RX duplicate frames/frags:\t\t\t[ %u ]\n",
	    stats->an_rx_dups);
	printf("RX partial frames:\t\t\t\t[ %u ]\n",
	    stats->an_rx_partial);
	printf("TX max lifetime exceeded:\t\t\t[ %u ]\n",
	    stats->an_tx_too_old);
	printf("RX max lifetime exceeded:\t\t\t[ %u ]\n",
	    stats->an_tx_too_old);
	printf("Sync lost due to too many missed beacons:\t[ %u ]\n",
	    stats->an_lostsync_missed_beacons);
	printf("Sync lost due to ARL exceeded:\t\t\t[ %u ]\n",
	    stats->an_lostsync_arl_exceeded);
	printf("Sync lost due to deauthentication:\t\t[ %u ]\n",
	    stats->an_lostsync_deauthed);
	printf("Sync lost due to disassociation:\t\t[ %u ]\n",
	    stats->an_lostsync_disassociated);
	printf("Sync lost due to excess change in TSF timing:\t[ %u ]\n",
	    stats->an_lostsync_tsf_timing);
	printf("Host transmitted multicasts:\t\t\t[ %u ]\n",
	    stats->an_tx_host_mcasts);
	printf("Host transmitted broadcasts:\t\t\t[ %u ]\n",
	    stats->an_tx_host_bcasts);
	printf("Host transmitted unicasts:\t\t\t[ %u ]\n",
	    stats->an_tx_host_ucasts);
	printf("Host transmission failures:\t\t\t[ %u ]\n",
	    stats->an_tx_host_failed);
	printf("Host received multicasts:\t\t\t[ %u ]\n",
	    stats->an_rx_host_mcasts);
	printf("Host received broadcasts:\t\t\t[ %u ]\n",
	    stats->an_rx_host_bcasts);
	printf("Host received unicasts:\t\t\t\t[ %u ]\n",
	    stats->an_rx_host_ucasts);
	printf("Host receive discards:\t\t\t\t[ %u ]\n",
	    stats->an_rx_host_discarded);
	printf("HMAC transmitted multicasts:\t\t\t[ %u ]\n",
	    stats->an_tx_hmac_mcasts);
	printf("HMAC transmitted broadcasts:\t\t\t[ %u ]\n",
	    stats->an_tx_hmac_bcasts);
	printf("HMAC transmitted unicasts:\t\t\t[ %u ]\n",
	    stats->an_tx_hmac_ucasts);
	printf("HMAC transmissions failed:\t\t\t[ %u ]\n",
	    stats->an_tx_hmac_failed);
	printf("HMAC received multicasts:\t\t\t[ %u ]\n",
	    stats->an_rx_hmac_mcasts);
	printf("HMAC received broadcasts:\t\t\t[ %u ]\n",
	    stats->an_rx_hmac_bcasts);
	printf("HMAC received unicasts:\t\t\t\t[ %u ]\n",
	    stats->an_rx_hmac_ucasts);
	printf("HMAC receive discards:\t\t\t\t[ %u ]\n",
	    stats->an_rx_hmac_discarded);
	printf("HMAC transmits accepted:\t\t\t[ %u ]\n",
	    stats->an_tx_hmac_accepted);
	printf("SSID mismatches:\t\t\t\t[ %u ]\n",
	    stats->an_ssid_mismatches);
	printf("Access point mismatches:\t\t\t[ %u ]\n",
	    stats->an_ap_mismatches);
	printf("Speed mismatches:\t\t\t\t[ %u ]\n",
	    stats->an_rates_mismatches);
	printf("Authentication rejects:\t\t\t\t[ %u ]\n",
	    stats->an_auth_rejects);
	printf("Authentication timeouts:\t\t\t[ %u ]\n",
	    stats->an_auth_timeouts);
	printf("Association rejects:\t\t\t\t[ %u ]\n",
	    stats->an_assoc_rejects);
	printf("Association timeouts:\t\t\t\t[ %u ]\n",
	    stats->an_assoc_timeouts);
	printf("Management frames received:\t\t\t[ %u ]\n",
	    stats->an_rx_mgmt_pkts);
	printf("Management frames transmitted:\t\t\t[ %u ]\n",
	    stats->an_tx_mgmt_pkts);
	printf("Refresh frames received:\t\t\t[ %u ]\n",
	    stats->an_rx_refresh_pkts);
	printf("Refresh frames transmitted:\t\t\t[ %u ]\n",
	    stats->an_tx_refresh_pkts);
	printf("Poll frames received:\t\t\t\t[ %u ]\n",
	    stats->an_rx_poll_pkts);
	printf("Poll frames transmitted:\t\t\t[ %u ]\n",
	    stats->an_tx_poll_pkts);
	printf("Host requested sync losses:\t\t\t[ %u ]\n",
	    stats->an_lostsync_hostreq);
	printf("Host transmitted bytes:\t\t\t\t[ %u ]\n",
	    stats->an_host_tx_bytes);
	printf("Host received bytes:\t\t\t\t[ %u ]\n",
	    stats->an_host_rx_bytes);
	printf("Uptime in microseconds:\t\t\t\t[ %u ]\n",
	    stats->an_uptime_usecs);
	printf("Uptime in seconds:\t\t\t\t[ %u ]\n",
	    stats->an_uptime_secs);
	printf("Sync lost due to better AP:\t\t\t[ %u ]\n",
	    stats->an_lostsync_better_ap);
}

static void
an_dumpap(const char *iface)
{
	struct an_ltv_aplist	*ap;
	struct an_req		areq;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_APLIST;

	an_getval(iface, &areq);

	ap = (struct an_ltv_aplist *)&areq;
	printf("Access point 1:\t\t\t");
	an_printhex((char *)&ap->an_ap1, ETHER_ADDR_LEN);
	printf("\nAccess point 2:\t\t\t");
	an_printhex((char *)&ap->an_ap2, ETHER_ADDR_LEN);
	printf("\nAccess point 3:\t\t\t");
	an_printhex((char *)&ap->an_ap3, ETHER_ADDR_LEN);
	printf("\nAccess point 4:\t\t\t");
	an_printhex((char *)&ap->an_ap4, ETHER_ADDR_LEN);
	printf("\n");

	return;
}

static void
an_dumpssid(const char *iface)
{
	struct an_ltv_ssidlist_new	*ssid;
	struct an_req		areq;
	int			i, max;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_SSIDLIST;

	an_getval(iface, &areq);

	max = (areq.an_len - 4) / sizeof(struct an_ltv_ssid_entry);
	if ( max > MAX_SSIDS ) {
		printf("Too many SSIDs only printing %d of %d\n",
		    MAX_SSIDS, max);
		max = MAX_SSIDS;
	}
	ssid = (struct an_ltv_ssidlist_new *)&areq;
	for (i = 0; i < max; i++)
		printf("SSID %2d:\t\t\t[ %.*s ]\n", i + 1, 
		    ssid->an_entry[i].an_len, 
		    ssid->an_entry[i].an_ssid);

	return;
}

static void
an_dumpconfig(const char *iface)
{
	struct an_ltv_genconfig	*cfg;
	struct an_req		areq;
	unsigned char		diversity;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_ACTUALCFG;

	an_getval(iface, &areq);

	cfg = (struct an_ltv_genconfig *)&areq;

	printf("Operating mode:\t\t\t\t[ ");
	if ((cfg->an_opmode & 0x7) == AN_OPMODE_IBSS_ADHOC)
		printf("ad-hoc");
	if ((cfg->an_opmode & 0x7) == AN_OPMODE_INFRASTRUCTURE_STATION)
		printf("infrastructure");
	if ((cfg->an_opmode & 0x7) == AN_OPMODE_AP)
		printf("access point");
	if ((cfg->an_opmode & 0x7) == AN_OPMODE_AP_REPEATER)
		printf("access point repeater");
	printf(" ]");
	printf("\nReceive mode:\t\t\t\t[ ");
	if ((cfg->an_rxmode & 0x7) == AN_RXMODE_BC_MC_ADDR)
		printf("broadcast/multicast/unicast");
	if ((cfg->an_rxmode & 0x7) == AN_RXMODE_BC_ADDR)
		printf("broadcast/unicast");
	if ((cfg->an_rxmode & 0x7) == AN_RXMODE_ADDR)
		printf("unicast");
	if ((cfg->an_rxmode & 0x7) == AN_RXMODE_80211_MONITOR_CURBSS)
		printf("802.11 monitor, current BSSID");
	if ((cfg->an_rxmode & 0x7) == AN_RXMODE_80211_MONITOR_ANYBSS)
		printf("802.11 monitor, any BSSID");
	if ((cfg->an_rxmode & 0x7) == AN_RXMODE_LAN_MONITOR_CURBSS)
		printf("LAN monitor, current BSSID");
	printf(" ]");
	printf("\nFragment threshold:\t\t\t");
	an_printwords(&cfg->an_fragthresh, 1);
	printf("\nRTS threshold:\t\t\t\t");
	an_printwords(&cfg->an_rtsthresh, 1);
	printf("\nMAC address:\t\t\t\t");
	an_printhex((char *)&cfg->an_macaddr, ETHER_ADDR_LEN);
	printf("\nSupported rates:\t\t\t");
	an_printspeeds(cfg->an_rates, 8);
	printf("\nShort retry limit:\t\t\t");
	an_printwords(&cfg->an_shortretry_limit, 1);
	printf("\nLong retry limit:\t\t\t");
	an_printwords(&cfg->an_longretry_limit, 1);
	printf("\nTX MSDU lifetime:\t\t\t");
	an_printwords(&cfg->an_tx_msdu_lifetime, 1);
	printf("\nRX MSDU lifetime:\t\t\t");
	an_printwords(&cfg->an_rx_msdu_lifetime, 1);
	printf("\nStationary:\t\t\t\t");
	an_printbool(cfg->an_stationary);
	printf("\nOrdering:\t\t\t\t");
	an_printbool(cfg->an_ordering);
	printf("\nDevice type:\t\t\t\t[ ");
	if (cfg->an_devtype == AN_DEVTYPE_PC4500)
		printf("PC4500");
	else if (cfg->an_devtype == AN_DEVTYPE_PC4800)
		printf("PC4800");
	else
		printf("unknown (%x)", cfg->an_devtype);
	printf(" ]");
	printf("\nScanning mode:\t\t\t\t[ ");
	if (cfg->an_scanmode == AN_SCANMODE_ACTIVE)
		printf("active");
	if (cfg->an_scanmode == AN_SCANMODE_PASSIVE)
		printf("passive");
	if (cfg->an_scanmode == AN_SCANMODE_AIRONET_ACTIVE)
		printf("Aironet active");
	printf(" ]");
	printf("\nProbe delay:\t\t\t\t");
	an_printwords(&cfg->an_probedelay, 1);
	printf("\nProbe energy timeout:\t\t\t");
	an_printwords(&cfg->an_probe_energy_timeout, 1);
	printf("\nProbe response timeout:\t\t\t");
	an_printwords(&cfg->an_probe_response_timeout, 1);
	printf("\nBeacon listen timeout:\t\t\t");
	an_printwords(&cfg->an_beacon_listen_timeout, 1);
	printf("\nIBSS join network timeout:\t\t");
	an_printwords(&cfg->an_ibss_join_net_timeout, 1);
	printf("\nAuthentication timeout:\t\t\t");
	an_printwords(&cfg->an_auth_timeout, 1);
	printf("\nWEP enabled:\t\t\t\t[ ");
	if (cfg->an_authtype & AN_AUTHTYPE_PRIVACY_IN_USE)
	{
		if (cfg->an_authtype & AN_AUTHTYPE_LEAP)
			 printf("LEAP");
		else if (cfg->an_authtype & AN_AUTHTYPE_ALLOW_UNENCRYPTED)
			 printf("mixed cell");
		else
			 printf("full");
	}
	else
		printf("no");
	printf(" ]");
	printf("\nAuthentication type:\t\t\t[ ");
	if ((cfg->an_authtype & AN_AUTHTYPE_MASK) == AN_AUTHTYPE_NONE)
		printf("none");
	if ((cfg->an_authtype & AN_AUTHTYPE_MASK) == AN_AUTHTYPE_OPEN)
		printf("open");
	if ((cfg->an_authtype & AN_AUTHTYPE_MASK) == AN_AUTHTYPE_SHAREDKEY)
		printf("shared key");
	printf(" ]");
	printf("\nAssociation timeout:\t\t\t");
	an_printwords(&cfg->an_assoc_timeout, 1);
	printf("\nSpecified AP association timeout:\t");
	an_printwords(&cfg->an_specified_ap_timeout, 1);
	printf("\nOffline scan interval:\t\t\t");
	an_printwords(&cfg->an_offline_scan_interval, 1);
	printf("\nOffline scan duration:\t\t\t");
	an_printwords(&cfg->an_offline_scan_duration, 1);
	printf("\nLink loss delay:\t\t\t");
	an_printwords(&cfg->an_link_loss_delay, 1);
	printf("\nMax beacon loss time:\t\t\t");
	an_printwords(&cfg->an_max_beacon_lost_time, 1);
	printf("\nRefresh interval:\t\t\t");
	an_printwords(&cfg->an_refresh_interval, 1);
	printf("\nPower save mode:\t\t\t[ ");
	if (cfg->an_psave_mode == AN_PSAVE_NONE)
		printf("none");
	if (cfg->an_psave_mode == AN_PSAVE_CAM)
		printf("constantly awake mode");
	if (cfg->an_psave_mode == AN_PSAVE_PSP)
		printf("PSP");
	if (cfg->an_psave_mode == AN_PSAVE_PSP_CAM)
		printf("PSP-CAM (fast PSP)");
	printf(" ]");
	printf("\nSleep through DTIMs:\t\t\t");
	an_printbool(cfg->an_sleep_for_dtims);
	printf("\nPower save listen interval:\t\t");
	an_printwords(&cfg->an_listen_interval, 1);
	printf("\nPower save fast listen interval:\t");
	an_printwords(&cfg->an_fast_listen_interval, 1);
	printf("\nPower save listen decay:\t\t");
	an_printwords(&cfg->an_listen_decay, 1);
	printf("\nPower save fast listen decay:\t\t");
	an_printwords(&cfg->an_fast_listen_decay, 1);
	printf("\nAP/ad-hoc Beacon period:\t\t");
	an_printwords(&cfg->an_beacon_period, 1);
	printf("\nAP/ad-hoc ATIM duration:\t\t");
	an_printwords(&cfg->an_atim_duration, 1);
	printf("\nAP/ad-hoc current channel:\t\t");
	an_printwords(&cfg->an_ds_channel, 1);
	printf("\nAP/ad-hoc DTIM period:\t\t\t");
	an_printwords(&cfg->an_dtim_period, 1);
	printf("\nRadio type:\t\t\t\t[ ");
	if (cfg->an_radiotype & AN_RADIOTYPE_80211_FH)
		printf("802.11 FH");
	else if (cfg->an_radiotype & AN_RADIOTYPE_80211_DS)
		printf("802.11 DS");
	else if (cfg->an_radiotype & AN_RADIOTYPE_LM2000_DS)
		printf("LM2000 DS");
	else
		printf("unknown (%x)", cfg->an_radiotype);
	printf(" ]");
	printf("\nRX Diversity:\t\t\t\t[ ");
	diversity = cfg->an_diversity & 0xFF;
	if (diversity == AN_DIVERSITY_FACTORY_DEFAULT)
		printf("factory default");
	else if (diversity == AN_DIVERSITY_ANTENNA_1_ONLY)
		printf("antenna 1 only");
	else if (diversity == AN_DIVERSITY_ANTENNA_2_ONLY)
		printf("antenna 2 only");
	else if (diversity == AN_DIVERSITY_ANTENNA_1_AND_2)
		printf("antenna 1 and 2");
	printf(" ]");
	printf("\nTX Diversity:\t\t\t\t[ ");
	diversity = (cfg->an_diversity >> 8) & 0xFF;
	if (diversity == AN_DIVERSITY_FACTORY_DEFAULT)
		printf("factory default");
	else if (diversity == AN_DIVERSITY_ANTENNA_1_ONLY)
		printf("antenna 1 only");
	else if (diversity == AN_DIVERSITY_ANTENNA_2_ONLY)
		printf("antenna 2 only");
	else if (diversity == AN_DIVERSITY_ANTENNA_1_AND_2)
		printf("antenna 1 and 2");
	printf(" ]");
	printf("\nTransmit power level:\t\t\t");
	an_printwords(&cfg->an_tx_power, 1);
	printf("\nRSS threshold:\t\t\t\t");
	an_printwords(&cfg->an_rss_thresh, 1);
	printf("\nNode name:\t\t\t\t");
	an_printstr((char *)&cfg->an_nodename, 16);
	printf("\nARL threshold:\t\t\t\t");
	an_printwords(&cfg->an_arl_thresh, 1);
	printf("\nARL decay:\t\t\t\t");
	an_printwords(&cfg->an_arl_decay, 1);
	printf("\nARL delay:\t\t\t\t");
	an_printwords(&cfg->an_arl_delay, 1);
	printf("\nConfiguration:\t\t\t\t[ ");
	if (cfg->an_home_product & AN_HOME_NETWORK)
		printf("Home Configuration");
	else
		printf("Enterprise Configuration");
	printf(" ]");

	printf("\n");
	printf("\n");
	an_readkeyinfo(iface);
}

static void
an_dumprssimap(const char *iface)
{
	struct an_ltv_rssi_map	*rssi;
	struct an_req		areq;
	int                     i;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_RSSI_MAP;

	an_getval(iface, &areq);

	rssi = (struct an_ltv_rssi_map *)&areq;

	printf("idx\tpct\t dBm\n");

	for (i = 0; i < 0xFF; i++) {
		/* 
		 * negate the dBm value: it's the only way the power 
		 * level makes sense 
		 */
		printf("%3d\t%3d\t%4d\n", i, 
			rssi->an_entries[i].an_rss_pct,
			- rssi->an_entries[i].an_rss_dbm);
	}
}

static void
usage(const char *p)
{
	fprintf(stderr, "usage:  %s -i iface -A (show specified APs)\n", p);
	fprintf(stderr, "\t%s -i iface -N (show specified SSIDss)\n", p);
	fprintf(stderr, "\t%s -i iface -S (show NIC status)\n", p);
	fprintf(stderr, "\t%s -i iface -I (show NIC capabilities)\n", p);
	fprintf(stderr, "\t%s -i iface -T (show stats counters)\n", p);
	fprintf(stderr, "\t%s -i iface -C (show current config)\n", p);
	fprintf(stderr, "\t%s -i iface -R (show RSSI map)\n", p);
	fprintf(stderr, "\t%s -i iface -t 0-4 (set TX speed)\n", p);
	fprintf(stderr, "\t%s -i iface -s 0-3 (set power save mode)\n", p);
	fprintf(stderr, "\t%s -i iface [-v 1-4] -a AP (specify AP)\n", p);
	fprintf(stderr, "\t%s -i iface -b val (set beacon period)\n", p);
	fprintf(stderr, "\t%s -i iface [-v 0|1] -d val (set diversity)\n", p);
	fprintf(stderr, "\t%s -i iface -j val (set netjoin timeout)\n", p);
	fprintf(stderr, "\t%s -i iface -e 0-4 (enable transmit key)\n", p);
	fprintf(stderr, "\t%s -i iface [-v 0-8] -k key (set key)\n", p);
	fprintf(stderr, "\t%s -i iface -K 0-2 (no auth/open/shared secret)\n", p);
	fprintf(stderr, "\t%s -i iface -W 0-2 (no WEP/full WEP/mixed cell)\n", p);
	fprintf(stderr, "\t%s -i iface -l val (set station name)\n", p);
	fprintf(stderr, "\t%s -i iface -m val (set MAC address)\n", p);
	fprintf(stderr, "\t%s -i iface [-v 1-3] -n SSID "
	    "(specify SSID)\n", p);
	fprintf(stderr, "\t%s -i iface -o 0|1 (set operating mode)\n", p);
	fprintf(stderr, "\t%s -i iface -c val (set ad-hoc channel)\n", p);
	fprintf(stderr, "\t%s -i iface -f val (set frag threshold)\n", p);
	fprintf(stderr, "\t%s -i iface -r val (set RTS threshold)\n", p);
	fprintf(stderr, "\t%s -i iface -M 0-15 (set monitor mode)\n", p);
	fprintf(stderr, "\t%s -i iface -L user (enter LEAP authentication mode)\n", p);
#ifdef ANCACHE
	fprintf(stderr, "\t%s -i iface -Q print signal quality cache\n", p);
	fprintf(stderr, "\t%s -i iface -Z zero out signal cache\n", p);
#endif

	fprintf(stderr, "\t%s -h (display this message)\n", p);

	exit(1);
}

static void
an_setconfig(const char *iface, int act, void *arg)
{
	struct an_ltv_genconfig	*cfg;
	struct an_ltv_caps	*caps;
	struct an_req		areq;
	struct an_req		areq_caps;
	u_int16_t		diversity = 0;
	struct ether_addr	*addr;
	int			i;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_GENCONFIG;
	an_getval(iface, &areq);
	cfg = (struct an_ltv_genconfig *)&areq;

	areq_caps.an_len = sizeof(areq);
	areq_caps.an_type = AN_RID_CAPABILITIES;
	an_getval(iface, &areq_caps);
	caps = (struct an_ltv_caps *)&areq_caps;

	switch(act) {
	case ACT_SET_OPMODE:
		cfg->an_opmode = atoi(arg);
		break;
	case ACT_SET_FREQ:
		cfg->an_ds_channel = atoi(arg);
		break;
	case ACT_SET_PWRSAVE:
		cfg->an_psave_mode = atoi(arg);
		break;
	case ACT_SET_SCANMODE:
		cfg->an_scanmode = atoi(arg);
		break;
	case ACT_SET_DIVERSITY_RX:
	case ACT_SET_DIVERSITY_TX:
		switch(atoi(arg)) {
		case 0:
			diversity = AN_DIVERSITY_FACTORY_DEFAULT;
			break;
		case 1:
			diversity = AN_DIVERSITY_ANTENNA_1_ONLY;
			break;
		case 2:
			diversity = AN_DIVERSITY_ANTENNA_2_ONLY;
			break;
		case 3:
			diversity = AN_DIVERSITY_ANTENNA_1_AND_2;
			break;
		default:
			errx(1, "bad diversity setting: %u", diversity);
			break;
		}
		if (act == ACT_SET_DIVERSITY_RX) {
			cfg->an_diversity &= 0xFF00;
			cfg->an_diversity |= diversity;
		} else {
			cfg->an_diversity &= 0x00FF;
			cfg->an_diversity |= (diversity << 8);
		}
		break;
	case ACT_SET_TXPWR:
		for (i = 0; i < 8; i++) {
			if (caps->an_tx_powerlevels[i] == atoi(arg))
				break;
		}
		if (i == 8)
			errx(1, "unsupported power level: %dmW", atoi(arg));

		cfg->an_tx_power = atoi(arg);
		break;
	case ACT_SET_RTS_THRESH:
		cfg->an_rtsthresh = atoi(arg);
		break;
	case ACT_SET_RTS_RETRYLIM:
		cfg->an_shortretry_limit =
		   cfg->an_longretry_limit = atoi(arg);
		break;
	case ACT_SET_BEACON_PERIOD:
		cfg->an_beacon_period = atoi(arg);
		break;
	case ACT_SET_WAKE_DURATION:
		cfg->an_atim_duration = atoi(arg);
		break;
	case ACT_SET_FRAG_THRESH:
		cfg->an_fragthresh = atoi(arg);
		break;
	case ACT_SET_NETJOIN:
		cfg->an_ibss_join_net_timeout = atoi(arg);
		break;
	case ACT_SET_MYNAME:
		bzero(cfg->an_nodename, 16);
		strncpy((char *)&cfg->an_nodename, optarg, 16);
		break;
	case ACT_SET_MAC:
		addr = ether_aton((char *)arg);

		if (addr == NULL)
			errx(1, "badly formatted address");
		bzero(cfg->an_macaddr, ETHER_ADDR_LEN);
		bcopy(addr, &cfg->an_macaddr, ETHER_ADDR_LEN);
		break;
	case ACT_ENABLE_WEP:
		switch (atoi (arg)) {
		case 0:
			/* no WEP */
			cfg->an_authtype &= ~(AN_AUTHTYPE_PRIVACY_IN_USE 
					| AN_AUTHTYPE_ALLOW_UNENCRYPTED
					| AN_AUTHTYPE_LEAP);
			break;
		case 1:
			/* full WEP */
			cfg->an_authtype |= AN_AUTHTYPE_PRIVACY_IN_USE;
			cfg->an_authtype &= ~AN_AUTHTYPE_ALLOW_UNENCRYPTED;
			cfg->an_authtype &= ~AN_AUTHTYPE_LEAP;
			break;
		case 2:
			/* mixed cell */
			cfg->an_authtype = AN_AUTHTYPE_PRIVACY_IN_USE 
					| AN_AUTHTYPE_ALLOW_UNENCRYPTED;
			break;
		}
		break;
	case ACT_SET_KEY_TYPE:
		cfg->an_authtype = (cfg->an_authtype & ~AN_AUTHTYPE_MASK) 
		        | atoi(arg);
		break;
	case ACT_SET_MONITOR_MODE:
		areq.an_type = AN_RID_MONITOR_MODE;
		cfg->an_len = atoi(arg);      /* mode is put in length */
		break;
	default:
		errx(1, "unknown action");
		break;
	}

	an_setval(iface, &areq);
	exit(0);
}

static void
an_setspeed(const char *iface, int act __unused, void *arg)
{
	struct an_req		areq;
	struct an_ltv_caps	*caps;
	u_int16_t		speed;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_CAPABILITIES;

	an_getval(iface, &areq);
	caps = (struct an_ltv_caps *)&areq;

	switch(atoi(arg)) {
	case 0:
		speed = 0;
		break;
	case 1:
		speed = AN_RATE_1MBPS;
		break;
	case 2:
		speed = AN_RATE_2MBPS;
		break;
	case 3:
		if (caps->an_rates[2] != AN_RATE_5_5MBPS)
			errx(1, "5.5Mbps not supported on this card");
		speed = AN_RATE_5_5MBPS;
		break;
	case 4:
		if (caps->an_rates[3] != AN_RATE_11MBPS)
			errx(1, "11Mbps not supported on this card");
		speed = AN_RATE_11MBPS;
		break;
	default:
		errx(1, "unsupported speed");
		break;
	}

	areq.an_len = 6;
	areq.an_type = AN_RID_TX_SPEED;
	areq.an_val[0] = speed;

	an_setval(iface, &areq);
	exit(0);
}

static void
an_setap(const char *iface, int act, void *arg)
{
	struct an_ltv_aplist	*ap;
	struct an_req		areq;
	struct ether_addr	*addr;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_APLIST;

	an_getval(iface, &areq);
	ap = (struct an_ltv_aplist *)&areq;

	addr = ether_aton((char *)arg);

	if (addr == NULL)
		errx(1, "badly formatted address");

	switch(act) {
	case ACT_SET_AP1:
		bzero(ap->an_ap1, ETHER_ADDR_LEN);
		bcopy(addr, &ap->an_ap1, ETHER_ADDR_LEN);
		break;
	case ACT_SET_AP2:
		bzero(ap->an_ap2, ETHER_ADDR_LEN);
		bcopy(addr, &ap->an_ap2, ETHER_ADDR_LEN);
		break;
	case ACT_SET_AP3:
		bzero(ap->an_ap3, ETHER_ADDR_LEN);
		bcopy(addr, &ap->an_ap3, ETHER_ADDR_LEN);
		break;
	case ACT_SET_AP4:
		bzero(ap->an_ap4, ETHER_ADDR_LEN);
		bcopy(addr, &ap->an_ap4, ETHER_ADDR_LEN);
		break;
	default:
		errx(1, "unknown action");
		break;
	}

	an_setval(iface, &areq);
	exit(0);
}

static void
an_setssid(const char *iface, int act, void *arg)
{
	struct an_ltv_ssidlist_new	*ssid;
	struct an_req		areq;
	int			max;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_SSIDLIST;

	an_getval(iface, &areq);
	ssid = (struct an_ltv_ssidlist_new *)&areq;

	max = (areq.an_len - 4) / sizeof(struct an_ltv_ssid_entry);
	if ( max > MAX_SSIDS ) {
		printf("Too many SSIDs only printing %d of %d\n",
		    MAX_SSIDS, max);
		max = MAX_SSIDS;
	}

	if ( act > max ) {
		errx(1, "bad modifier %d: there "
		    "are only %d SSID settings", act, max);
		exit(1);
	}

	bzero(ssid->an_entry[act-1].an_ssid, 
	    sizeof(ssid->an_entry[act-1].an_ssid));
	strlcpy(ssid->an_entry[act-1].an_ssid, (char *)arg, 
	    sizeof(ssid->an_entry[act-1].an_ssid));
	ssid->an_entry[act-1].an_len 
	    = strlen(ssid->an_entry[act-1].an_ssid);

	an_setval(iface, &areq);

	exit(0);
}

#ifdef ANCACHE
static void
an_zerocache(const char *iface)
{
	struct an_req		areq;

	bzero(&areq, sizeof(areq));
	areq.an_len = 0;
	areq.an_type = AN_RID_ZERO_CACHE;

	an_getval(iface, &areq);
}

static void
an_readcache(const char *iface)
{
	struct an_req		areq;
	uint16_t 		*an_sigitems;
	struct an_sigcache 	*sc;
	int 			i;

	if (iface == NULL)
		errx(1, "must specify interface name");

	bzero(&areq, sizeof(areq));
	areq.an_len = AN_MAX_DATALEN;
	areq.an_type = AN_RID_READ_CACHE;

	an_getval(iface, &areq);

	an_sigitems = areq.an_val; 
	sc = (struct an_sigcache *)((int32_t *)areq.an_val + 1);

	for (i = 0; i < *an_sigitems; i++) {
		printf("[%d/%d]:", i+1, *an_sigitems);
		printf(" %02x:%02x:%02x:%02x:%02x:%02x,",
		  		    	sc->macsrc[0]&0xff,
		  		    	sc->macsrc[1]&0xff,
		   		    	sc->macsrc[2]&0xff,
		   			sc->macsrc[3]&0xff,
		   			sc->macsrc[4]&0xff,
		   			sc->macsrc[5]&0xff);
        	printf(" %d.%d.%d.%d,",((sc->ipsrc >> 0) & 0xff),
				        ((sc->ipsrc >> 8) & 0xff),
				        ((sc->ipsrc >> 16) & 0xff),
				        ((sc->ipsrc >> 24) & 0xff));
		printf(" sig: %d, noise: %d, qual: %d\n",
		   			sc->signal,
		   			sc->noise,
		   			sc->quality);
		sc++;
	}
}
#endif

static int
an_hex2int(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);

	return (0); 
}

static void
an_str2key(const char *s, struct an_ltv_key *k)
{
	int			n, i;
	char			*p;

	/* Is this a hex string? */
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		/* Yes, convert to int. */
		n = 0;
		p = (char *)&k->key[0];
		for (i = 2; s[i] != '\0' && s[i + 1] != '\0'; i+= 2) {
			*p++ = (an_hex2int(s[i]) << 4) + an_hex2int(s[i + 1]);
			n++;
		}
		if (s[i] != '\0')
			errx(1, "hex strings must be of even length");
		k->klen = n;
	} else {
		/* No, just copy it in. */
		bcopy(s, k->key, strlen(s));
		k->klen = strlen(s);
	}

	return;
}

static void
an_setkeys(const char *iface, const char *key, int keytype)
{
	struct an_req		areq;
	struct an_ltv_key	*k;

	bzero(&areq, sizeof(areq));
	k = (struct an_ltv_key *)&areq;

	if (strlen(key) > 28) {
		err(1, "encryption key must be no "
		    "more than 18 characters long");
	}

	an_str2key(key, k);
	
	k->kindex=keytype/2;

	if (!(k->klen==0 || k->klen==5 || k->klen==13)) {
		err(1, "encryption key must be 0, 5 or 13 bytes long");
	}

	/* default mac and only valid one (from manual) 1.0.0.0.0.0 */
	k->mac[0]=1;
	k->mac[1]=0;
	k->mac[2]=0;
	k->mac[3]=0;
	k->mac[4]=0;
	k->mac[5]=0;

	switch(keytype & 1) {
	case 0:
	  areq.an_len = sizeof(struct an_ltv_key);
	  areq.an_type = AN_RID_WEP_PERM;
	  an_setval(iface, &areq);
	  break;
	case 1:
	  areq.an_len = sizeof(struct an_ltv_key);
	  areq.an_type = AN_RID_WEP_TEMP;
	  an_setval(iface, &areq);
	  break;
	}
}

static void
an_readkeyinfo(const char *iface)
{
	struct an_req		areq;
	struct an_ltv_genconfig	*cfg;
	struct an_ltv_key	*k;
	int i;
	int home;

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_ACTUALCFG;
	an_getval(iface, &areq);
	cfg = (struct an_ltv_genconfig *)&areq;
	if (cfg->an_home_product & AN_HOME_NETWORK)
		home = 1;
	else
		home = 0;

	bzero(&areq, sizeof(areq));
	k = (struct an_ltv_key *)&areq;

	printf("WEP Key status:\n");
	areq.an_type = AN_RID_WEP_TEMP;  	/* read first key */
	for(i=0; i<5; i++) {
		areq.an_len = sizeof(struct an_ltv_key);
		an_getval(iface, &areq);
       		if (k->kindex == 0xffff)
			break;
		switch (k->klen) {
		case 0:
			printf("\tKey %u is unset\n", k->kindex);
			break;
		case 5:
			printf("\tKey %u is set  40 bits\n", k->kindex);
			break;
		case 13:
			printf("\tKey %u is set 128 bits\n", k->kindex);
			break;
		default:
			printf("\tWEP Key %d has an unknown size %u\n",
			    i, k->klen);
		}

		areq.an_type = AN_RID_WEP_PERM;	/* read next key */
	}
	k->kindex = 0xffff;
	areq.an_len = sizeof(struct an_ltv_key);
      	an_getval(iface, &areq);
	printf("\tThe active transmit key is %d\n", 4 * home + k->mac[0]);

	return;
}

static void
an_enable_tx_key(const char *iface, const char *arg)
{
	struct an_req		areq;
	struct an_ltv_key	*k;
	struct an_ltv_genconfig *config;

	bzero(&areq, sizeof(areq));

	/* set home or not home mode */
	areq.an_len  = sizeof(struct an_ltv_genconfig);
	areq.an_type = AN_RID_GENCONFIG;
	an_getval(iface, &areq);
	config = (struct an_ltv_genconfig *)&areq;
	if (atoi(arg) == 4) {
		config->an_home_product |= AN_HOME_NETWORK;
	}else{
		config->an_home_product &= ~AN_HOME_NETWORK;
	}
	an_setval(iface, &areq);

	bzero(&areq, sizeof(areq));

	k = (struct an_ltv_key *)&areq;

	/* From a Cisco engineer write the transmit key to use in the
	   first MAC, index is FFFF*/
	k->kindex=0xffff;
	k->klen=0;

	k->mac[0]=atoi(arg);
	k->mac[1]=0;
	k->mac[2]=0;
	k->mac[3]=0;
	k->mac[4]=0;
	k->mac[5]=0;

	areq.an_len = sizeof(struct an_ltv_key);
	areq.an_type = AN_RID_WEP_PERM;
	an_setval(iface, &areq);
}

static void
an_enable_leap_mode(const char *iface, const char *username)
{
	struct an_req		areq;
	struct an_ltv_status	*sts;
	struct an_ltv_genconfig	*cfg;
	struct an_ltv_caps	*caps;
	struct an_ltv_leap_username an_username;
	struct an_ltv_leap_password an_password;
	char *password;
	MD4_CTX context;
	int len;
	int i;
	char unicode_password[LEAP_PASSWORD_MAX * 2];

	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_CAPABILITIES;

	an_getval(iface, &areq);

	caps = (struct an_ltv_caps *)&areq;

	if (!(caps->an_softcaps & AN_AUTHTYPE_LEAP)) {
		fprintf(stderr, "Firmware does not support LEAP\n");
		exit(1);
	}

	bzero(&an_username, sizeof(an_username));
	bzero(&an_password, sizeof(an_password));

	len = strlen(username);
	if (len > LEAP_USERNAME_MAX) {
		printf("Username too long (max %d)\n", LEAP_USERNAME_MAX);
		exit(1);
	}
	strncpy(an_username.an_username, username, len);
	an_username.an_username_len = len;
	an_username.an_len  = sizeof(an_username);	
	an_username.an_type = AN_RID_LEAPUSERNAME;

	password = getpass("Enter LEAP password:");

	len = strlen(password);
	if (len > LEAP_PASSWORD_MAX) {
		printf("Password too long (max %d)\n", LEAP_PASSWORD_MAX);
		exit(1);
	}
	
	bzero(&unicode_password, sizeof(unicode_password));
	for(i = 0; i < len; i++) {
		unicode_password[i * 2] = *password++;
	}
	
	/* First half */
	MD4Init(&context);
	MD4Update(&context, unicode_password, len * 2);
	MD4Final(&an_password.an_password[0], &context);
	
	/* Second half */
	MD4Init (&context);
	MD4Update (&context, &an_password.an_password[0], 16);
	MD4Final (&an_password.an_password[16], &context);

	an_password.an_password_len = 32;
	an_password.an_len  = sizeof(an_password);	
	an_password.an_type = AN_RID_LEAPPASSWORD;	

	an_setval(iface, (struct an_req *)&an_username);
	an_setval(iface, (struct an_req *)&an_password);
	
	areq.an_len = sizeof(areq);
	areq.an_type = AN_RID_GENCONFIG;
	an_getval(iface, &areq);
	cfg = (struct an_ltv_genconfig *)&areq;
	cfg->an_authtype = (AN_AUTHTYPE_PRIVACY_IN_USE | AN_AUTHTYPE_LEAP);
	an_setval(iface, &areq);

	sts = (struct an_ltv_status *)&areq;
	areq.an_type = AN_RID_STATUS;
	
	for (i = 60; i > 0; i--) {
		an_getval(iface, &areq);
		if (sts->an_opmode & AN_STATUS_OPMODE_LEAP) {
			printf("Authenticated\n");
			break;
		}
		sleep(1);
	}

	if (i == 0) {
		fprintf(stderr, "Failed LEAP authentication\n");
		exit(1);
	}
}

int
main(int argc, char *argv[])
{
	int			ch;
	int			act = 0;
	const char		*iface = NULL;
	int			modifier = 0;
	char			*key = NULL;
	void			*arg = NULL;
	char			*p = argv[0];

	/* Get the interface name */
	opterr = 0;
	ch = getopt(argc, argv, "i:");
	if (ch == 'i') {
		iface = optarg;
	} else {
		if (argc > 1 && *argv[1] != '-') {
			iface = argv[1];
			optind = 2; 
		} else {
			iface = "an0";
			optind = 1;
		}
		optreset = 1;
	}
	opterr = 1;

	while ((ch = getopt(argc, argv,
	    "ANISCTRht:a:e:o:s:n:v:d:j:b:c:f:r:p:w:m:l:k:K:W:QZM:L:")) != -1) {
		switch(ch) {
		case 'Z':
#ifdef ANCACHE
			act = ACT_ZEROCACHE;
#else
			errx(1, "ANCACHE not available");
#endif
			break;
		case 'Q':
#ifdef ANCACHE
			act = ACT_DUMPCACHE;
#else
			errx(1, "ANCACHE not available");
#endif
			break;
		case 'A':
			act = ACT_DUMPAP;
			break;
		case 'N':
			act = ACT_DUMPSSID;
			break;
		case 'S':
			act = ACT_DUMPSTATUS;
			break;
		case 'I':
			act = ACT_DUMPCAPS;
			break;
		case 'T':
			act = ACT_DUMPSTATS;
			break;
		case 'C':
			act = ACT_DUMPCONFIG;
			break;
		case 'R':
			act = ACT_DUMPRSSIMAP;
			break;
		case 't':
			act = ACT_SET_TXRATE;
			arg = optarg;
			break;
		case 's':
			act = ACT_SET_PWRSAVE;
			arg = optarg;
			break;
		case 'p':
			act = ACT_SET_TXPWR;
			arg = optarg;
			break;
		case 'v':
			modifier = atoi(optarg);
			break;
		case 'a':
			switch(modifier) {
			case 0:
			case 1:
				act = ACT_SET_AP1;
				break;
			case 2:
				act = ACT_SET_AP2;
				break;
			case 3:
				act = ACT_SET_AP3;
				break;
			case 4:
				act = ACT_SET_AP4;
				break;
			default:
				errx(1, "bad modifier %d: there "
				    "are only 4 access point settings",
				    modifier);
				usage(p);
				break;
			}
			arg = optarg;
			break;
		case 'b':
			act = ACT_SET_BEACON_PERIOD;
			arg = optarg;
			break;
		case 'd':
			switch(modifier) {
			case 0:
				act = ACT_SET_DIVERSITY_RX;
				break;
			case 1:
				act = ACT_SET_DIVERSITY_TX;
				break;
			default:
				errx(1, "must specify RX or TX diversity");
				break;
			}
			if (!isdigit(*optarg)) {
				errx(1, "%s is not numeric", optarg);
				exit(1);
			}
			arg = optarg;
			break;
		case 'j':
			act = ACT_SET_NETJOIN;
			arg = optarg;
			break;
		case 'l':
			act = ACT_SET_MYNAME;
			arg = optarg;
			break;
		case 'm':
			act = ACT_SET_MAC;
			arg = optarg;
			break;
		case 'n':
			if (modifier == 0)
				modifier = 1;
			act = ACT_SET_SSID;
			arg = optarg;
			break;
		case 'o':
			act = ACT_SET_OPMODE;
			arg = optarg;
			break;
		case 'c':
			act = ACT_SET_FREQ;
			arg = optarg;
			break;
		case 'f':
			act = ACT_SET_FRAG_THRESH;
			arg = optarg;
			break;
		case 'W':
			act = ACT_ENABLE_WEP;
			arg = optarg;
			break;
		case 'K':
			act = ACT_SET_KEY_TYPE;
			arg = optarg;
			break;
		case 'k':
			act = ACT_SET_KEYS;
			key = optarg;
			break;
		case 'e':
			act = ACT_ENABLE_TX_KEY;
			arg = optarg;
			break;
		case 'q':
			act = ACT_SET_RTS_RETRYLIM;
			arg = optarg;
			break;
		case 'r':
			act = ACT_SET_RTS_THRESH;
			arg = optarg;
			break;
		case 'w':
			act = ACT_SET_WAKE_DURATION;
			arg = optarg;
			break;
		case 'M':
			act = ACT_SET_MONITOR_MODE;
			arg = optarg;
			break;
		case 'L':
			act = ACT_SET_LEAP_MODE;
			arg = optarg;
			break;
		case 'h':
		default:
			usage(p);
		}
	}

	if (iface == NULL || (!act && !key))
		usage(p);

	switch(act) {
	case ACT_DUMPSTATUS:
		an_dumpstatus(iface);
		break;
	case ACT_DUMPCAPS:
		an_dumpcaps(iface);
		break;
	case ACT_DUMPSTATS:
		an_dumpstats(iface);
		break;
	case ACT_DUMPCONFIG:
		an_dumpconfig(iface);
		break;
	case ACT_DUMPSSID:
		an_dumpssid(iface);
		break;
	case ACT_DUMPAP:
		an_dumpap(iface);
		break;
	case ACT_DUMPRSSIMAP:
		an_dumprssimap(iface);
		break;
	case ACT_SET_SSID:
		an_setssid(iface, modifier, arg);
		break;
	case ACT_SET_AP1:
	case ACT_SET_AP2:
	case ACT_SET_AP3:
	case ACT_SET_AP4:
		an_setap(iface, act, arg);
		break;
	case ACT_SET_TXRATE:
		an_setspeed(iface, act, arg);
		break;
#ifdef ANCACHE
	case ACT_ZEROCACHE:
		an_zerocache(iface);
		break;
	case ACT_DUMPCACHE:
		an_readcache(iface);
		break;

#endif
	case ACT_SET_KEYS:
		an_setkeys(iface, key, modifier);
		break;
	case ACT_ENABLE_TX_KEY:
		an_enable_tx_key(iface, arg);
		break;
	case ACT_SET_LEAP_MODE:
		an_enable_leap_mode(iface, arg);
		break;
	default:
		an_setconfig(iface, act, arg);
		break;
	}

	exit(0);
}

