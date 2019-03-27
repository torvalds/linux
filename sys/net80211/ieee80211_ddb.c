/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_wlan.h"

#ifdef DDB
/*
 * IEEE 802.11 DDB support
 */
#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/vnet.h>

#include <net80211/ieee80211_var.h>
#ifdef IEEE80211_SUPPORT_TDMA
#include <net80211/ieee80211_tdma.h>
#endif
#ifdef IEEE80211_SUPPORT_MESH
#include <net80211/ieee80211_mesh.h>
#endif

#include <ddb/ddb.h>
#include <ddb/db_sym.h>

#define DB_PRINTSYM(prefix, name, addr) do { \
	db_printf("%s%-25s : ",  prefix, name); \
	db_printsym((db_addr_t) addr, DB_STGY_ANY); \
	db_printf("\n"); \
} while (0)

static void _db_show_sta(const struct ieee80211_node *);
static void _db_show_vap(const struct ieee80211vap *, int, int);
static void _db_show_com(const struct ieee80211com *,
	int showvaps, int showsta, int showmesh, int showprocs);

static void _db_show_all_vaps(void *, struct ieee80211com *);

static void _db_show_node_table(const char *tag,
	const struct ieee80211_node_table *);
static void _db_show_channel(const char *tag, const struct ieee80211_channel *);
static void _db_show_ssid(const char *tag, int ix, int len, const uint8_t *);
static void _db_show_appie(const char *tag, const struct ieee80211_appie *);
static void _db_show_key(const char *tag, int ix, const struct ieee80211_key *);
static void _db_show_roamparams(const char *tag, const void *arg,
	const struct ieee80211_roamparam *rp);
static void _db_show_txparams(const char *tag, const void *arg,
	const struct ieee80211_txparam *tp);
static void _db_show_ageq(const char *tag, const struct ieee80211_ageq *q);
static void _db_show_stats(const struct ieee80211_stats *);
#ifdef IEEE80211_SUPPORT_MESH
static void _db_show_mesh(const struct ieee80211_mesh_state *);
#endif

DB_SHOW_COMMAND(sta, db_show_sta)
{
	if (!have_addr) {
		db_printf("usage: show sta <addr>\n");
		return;
	}
	_db_show_sta((const struct ieee80211_node *) addr);
}

DB_SHOW_COMMAND(statab, db_show_statab)
{
	if (!have_addr) {
		db_printf("usage: show statab <addr>\n");
		return;
	}
	_db_show_node_table("", (const struct ieee80211_node_table *) addr);
}

DB_SHOW_COMMAND(vap, db_show_vap)
{
	int i, showmesh = 0, showprocs = 0;

	if (!have_addr) {
		db_printf("usage: show vap <addr>\n");
		return;
	}
	for (i = 0; modif[i] != '\0'; i++)
		switch (modif[i]) {
		case 'a':
			showprocs = 1;
			showmesh = 1;
			break;
		case 'm':
			showmesh = 1;
			break;
		case 'p':
			showprocs = 1;
			break;
		}
	_db_show_vap((const struct ieee80211vap *) addr, showmesh, showprocs);
}

DB_SHOW_COMMAND(com, db_show_com)
{
	const struct ieee80211com *ic;
	int i, showprocs = 0, showvaps = 0, showsta = 0, showmesh = 0;

	if (!have_addr) {
		db_printf("usage: show com <addr>\n");
		return;
	}
	for (i = 0; modif[i] != '\0'; i++)
		switch (modif[i]) {
		case 'a':
			showsta = showmesh = showvaps = showprocs = 1;
			break;
		case 's':
			showsta = 1;
			break;
		case 'm':
			showmesh = 1;
			break;
		case 'v':
			showvaps = 1;
			break;
		case 'p':
			showprocs = 1;
			break;
		}

	ic = (const struct ieee80211com *) addr;
	_db_show_com(ic, showvaps, showsta, showmesh, showprocs);
}

DB_SHOW_ALL_COMMAND(vaps, db_show_all_vaps)
{
	int i, showall = 0;

	for (i = 0; modif[i] != '\0'; i++)
		switch (modif[i]) {
		case 'a':
			showall = 1;
			break;
		}

	ieee80211_iterate_coms(_db_show_all_vaps, &showall);
}

#ifdef IEEE80211_SUPPORT_MESH
DB_SHOW_ALL_COMMAND(mesh, db_show_mesh)
{
	const struct ieee80211_mesh_state *ms;

	if (!have_addr) {
		db_printf("usage: show mesh <addr>\n");
		return;
	}
	ms = (const struct ieee80211_mesh_state *) addr;
	_db_show_mesh(ms);
}
#endif /* IEEE80211_SUPPORT_MESH */

static void
_db_show_txampdu(const char *sep, int ix, const struct ieee80211_tx_ampdu *tap)
{
	db_printf("%stxampdu[%d]: %p flags %b %s\n",
		sep, ix, tap, tap->txa_flags, IEEE80211_AGGR_BITS,
		ieee80211_wme_acnames[TID_TO_WME_AC(tap->txa_tid)]);
	db_printf("%s  token %u lastsample %d pkts %d avgpps %d qbytes %d qframes %d\n",
		sep, tap->txa_token, tap->txa_lastsample, tap->txa_pkts,
		tap->txa_avgpps, tap->txa_qbytes, tap->txa_qframes);
	db_printf("%s  start %u seqpending %u wnd %u attempts %d nextrequest %d\n",
		sep, tap->txa_start, tap->txa_seqpending, tap->txa_wnd,
		tap->txa_attempts, tap->txa_nextrequest);
	/* XXX timer */
}

static void
_db_show_rxampdu(const char *sep, int ix, const struct ieee80211_rx_ampdu *rap)
{
	int i;

	db_printf("%srxampdu[%d]: %p flags 0x%x tid %u\n",
		sep, ix, rap, rap->rxa_flags, ix /*XXX */);
	db_printf("%s  qbytes %d qframes %d seqstart %u start %u wnd %u\n",
		sep, rap->rxa_qbytes, rap->rxa_qframes,
		rap->rxa_seqstart, rap->rxa_start, rap->rxa_wnd);
	db_printf("%s  age %d nframes %d\n", sep,
		rap->rxa_age, rap->rxa_nframes);
	for (i = 0; i < IEEE80211_AGGR_BAWMAX; i++)
		if (rap->rxa_m[i] != NULL)
			db_printf("%s  m[%2u:%4u] %p\n", sep, i,
			    IEEE80211_SEQ_ADD(rap->rxa_start, i),
			    rap->rxa_m[i]);
}

static void
_db_show_sta(const struct ieee80211_node *ni)
{
	int i;

	db_printf("0x%p: mac %s refcnt %d\n", ni,
		ether_sprintf(ni->ni_macaddr), ieee80211_node_refcnt(ni));
	db_printf("\tvap %p wdsvap %p ic %p table %p\n",
		ni->ni_vap, ni->ni_wdsvap, ni->ni_ic, ni->ni_table);
	db_printf("\tflags=%b\n", ni->ni_flags, IEEE80211_NODE_BITS);
	db_printf("\tauthmode %u ath_flags 0x%x ath_defkeyix %u\n",
		ni->ni_authmode, ni->ni_ath_flags, ni->ni_ath_defkeyix);
	db_printf("\tassocid 0x%x txpower %u vlan %u\n",
		ni->ni_associd, ni->ni_txpower, ni->ni_vlan);
	db_printf("\tjointime %d (%lu secs) challenge %p\n",
		ni->ni_jointime, (unsigned long)(time_uptime - ni->ni_jointime),
		ni->ni_challenge);
	db_printf("\ties: data %p len %d\n", ni->ni_ies.data, ni->ni_ies.len);
	db_printf("\t[wpa_ie %p rsn_ie %p wme_ie %p ath_ie %p\n",
		ni->ni_ies.wpa_ie, ni->ni_ies.rsn_ie, ni->ni_ies.wme_ie,
		ni->ni_ies.ath_ie);
	db_printf("\t htcap_ie %p htinfo_ie %p]\n",
		ni->ni_ies.htcap_ie, ni->ni_ies.htinfo_ie);
	if (ni->ni_flags & IEEE80211_NODE_QOS) {
		for (i = 0; i < WME_NUM_TID; i++) {
			if (ni->ni_txseqs[i] || ni->ni_rxseqs[i])
				db_printf("\t[%u] txseq %u rxseq %u fragno %u\n",
				    i, ni->ni_txseqs[i],
				    ni->ni_rxseqs[i] >> IEEE80211_SEQ_SEQ_SHIFT,
				    ni->ni_rxseqs[i] & IEEE80211_SEQ_FRAG_MASK);
		}
	}
	db_printf("\ttxseq %u rxseq %u fragno %u rxfragstamp %u\n",
		ni->ni_txseqs[IEEE80211_NONQOS_TID],
		ni->ni_rxseqs[IEEE80211_NONQOS_TID] >> IEEE80211_SEQ_SEQ_SHIFT,
		ni->ni_rxseqs[IEEE80211_NONQOS_TID] & IEEE80211_SEQ_FRAG_MASK,
		ni->ni_rxfragstamp);
	db_printf("\trxfrag[0] %p rxfrag[1] %p rxfrag[2] %p\n",
		ni->ni_rxfrag[0], ni->ni_rxfrag[1], ni->ni_rxfrag[2]);
	_db_show_key("\tucastkey", 0, &ni->ni_ucastkey);
	db_printf("\tavgrssi 0x%x (rssi %d) noise %d\n",
		ni->ni_avgrssi, IEEE80211_RSSI_GET(ni->ni_avgrssi),
		ni->ni_noise);
	db_printf("\tintval %u capinfo %b\n",
		ni->ni_intval, ni->ni_capinfo, IEEE80211_CAPINFO_BITS);
	db_printf("\tbssid %s", ether_sprintf(ni->ni_bssid));
	_db_show_ssid(" essid ", 0, ni->ni_esslen, ni->ni_essid);
	db_printf("\n");
	_db_show_channel("\tchannel", ni->ni_chan);
	db_printf("\n");
	db_printf("\terp %b dtim_period %u dtim_count %u\n",
		ni->ni_erp, IEEE80211_ERP_BITS,
		ni->ni_dtim_period, ni->ni_dtim_count);

	db_printf("\thtcap %b htparam 0x%x htctlchan %u ht2ndchan %u\n",
		ni->ni_htcap, IEEE80211_HTCAP_BITS,
		ni->ni_htparam, ni->ni_htctlchan, ni->ni_ht2ndchan);
	db_printf("\thtopmode 0x%x htstbc 0x%x chw %u\n",
		ni->ni_htopmode, ni->ni_htstbc, ni->ni_chw);

	/* XXX ampdu state */
	for (i = 0; i < WME_NUM_TID; i++)
		if (ni->ni_tx_ampdu[i].txa_flags & IEEE80211_AGGR_SETUP)
			_db_show_txampdu("\t", i, &ni->ni_tx_ampdu[i]);
	for (i = 0; i < WME_NUM_TID; i++)
		if (ni->ni_rx_ampdu[i].rxa_flags)
			_db_show_rxampdu("\t", i, &ni->ni_rx_ampdu[i]);

	db_printf("\tinact %u inact_reload %u txrate %u\n",
		ni->ni_inact, ni->ni_inact_reload, ni->ni_txrate);
#ifdef IEEE80211_SUPPORT_MESH
	_db_show_ssid("\tmeshid ", 0, ni->ni_meshidlen, ni->ni_meshid);
	db_printf(" mlstate %b mllid 0x%x mlpid 0x%x mlrcnt %u mltval %u\n",
	    ni->ni_mlstate, IEEE80211_MESH_MLSTATE_BITS,
	    ni->ni_mllid, ni->ni_mlpid, ni->ni_mlrcnt, ni->ni_mltval);
#endif
}

#ifdef IEEE80211_SUPPORT_TDMA
static void
_db_show_tdma(const char *sep, const struct ieee80211_tdma_state *ts, int showprocs)
{
	db_printf("%stdma %p:\n", sep, ts);
	db_printf("%s  version %u slot %u bintval %u peer %p\n", sep,
	    ts->tdma_version, ts->tdma_slot, ts->tdma_bintval, ts->tdma_peer);
	db_printf("%s  slotlen %u slotcnt %u", sep,
	    ts->tdma_slotlen, ts->tdma_slotcnt);
	db_printf(" inuse 0x%x active 0x%x count %d\n",
	    ts->tdma_inuse[0], ts->tdma_active[0], ts->tdma_count);
	if (showprocs) {
		DB_PRINTSYM(sep, "  tdma_newstate", ts->tdma_newstate);
		DB_PRINTSYM(sep, "  tdma_recv_mgmt", ts->tdma_recv_mgmt);
		DB_PRINTSYM(sep, "  tdma_opdetach", ts->tdma_opdetach);
	}
}
#endif /* IEEE80211_SUPPORT_TDMA */

static void
_db_show_vap(const struct ieee80211vap *vap, int showmesh, int showprocs)
{
	const struct ieee80211com *ic = vap->iv_ic;
	int i;

	db_printf("%p:", vap);
	db_printf(" bss %p", vap->iv_bss);
	db_printf(" myaddr %s", ether_sprintf(vap->iv_myaddr));
	db_printf("\n");

	db_printf("\topmode %s", ieee80211_opmode_name[vap->iv_opmode]);
#ifdef IEEE80211_SUPPORT_MESH
	if (vap->iv_opmode == IEEE80211_M_MBSS)
		db_printf("(%p)", vap->iv_mesh);
#endif
	db_printf(" state %s", ieee80211_state_name[vap->iv_state]);
	db_printf(" ifp %p(%s)", vap->iv_ifp, vap->iv_ifp->if_xname);
	db_printf("\n");

	db_printf("\tic %p", vap->iv_ic);
	db_printf(" media %p", &vap->iv_media);
	db_printf(" bpf_if %p", vap->iv_rawbpf);
	db_printf(" mgtsend %p", &vap->iv_mgtsend);
#if 0
	struct sysctllog	*iv_sysctl;	/* dynamic sysctl context */
#endif
	db_printf("\n");
	db_printf("\tdebug=%b\n", vap->iv_debug, IEEE80211_MSG_BITS);

	db_printf("\tflags=%b\n", vap->iv_flags, IEEE80211_F_BITS);
	db_printf("\tflags_ext=%b\n", vap->iv_flags_ext, IEEE80211_FEXT_BITS);
	db_printf("\tflags_ht=%b\n", vap->iv_flags_ht, IEEE80211_FHT_BITS);
	db_printf("\tflags_ven=%b\n", vap->iv_flags_ven, IEEE80211_FVEN_BITS);
	db_printf("\tcaps=%b\n", vap->iv_caps, IEEE80211_C_BITS);
	db_printf("\thtcaps=%b\n", vap->iv_htcaps, IEEE80211_C_HTCAP_BITS);

	_db_show_stats(&vap->iv_stats);

	db_printf("\tinact_init %d", vap->iv_inact_init);
	db_printf(" inact_auth %d", vap->iv_inact_auth);
	db_printf(" inact_run %d", vap->iv_inact_run);
	db_printf(" inact_probe %d", vap->iv_inact_probe);
	db_printf("\n");

	db_printf("\tdes_nssid %d", vap->iv_des_nssid);
	if (vap->iv_des_nssid)
		_db_show_ssid(" des_ssid[%u] ", 0,
		    vap->iv_des_ssid[0].len, vap->iv_des_ssid[0].ssid);
	db_printf(" des_bssid %s", ether_sprintf(vap->iv_des_bssid));
	db_printf("\n");
	db_printf("\tdes_mode %d", vap->iv_des_mode);
	_db_show_channel(" des_chan", vap->iv_des_chan);
	db_printf("\n");
#if 0
	int			iv_nicknamelen;	/* XXX junk */
	uint8_t			iv_nickname[IEEE80211_NWID_LEN];
#endif
	db_printf("\tbgscanidle %u", vap->iv_bgscanidle);
	db_printf(" bgscanintvl %u", vap->iv_bgscanintvl);
	db_printf(" scanvalid %u", vap->iv_scanvalid);
	db_printf("\n");
	db_printf("\tscanreq_duration %u", vap->iv_scanreq_duration);
	db_printf(" scanreq_mindwell %u", vap->iv_scanreq_mindwell);
	db_printf(" scanreq_maxdwell %u", vap->iv_scanreq_maxdwell);
	db_printf("\n");
	db_printf("\tscanreq_flags 0x%x", vap->iv_scanreq_flags);
	db_printf(" scanreq_nssid %d", vap->iv_scanreq_nssid);
	for (i = 0; i < vap->iv_scanreq_nssid; i++)
		_db_show_ssid(" scanreq_ssid[%u]", i,
		    vap->iv_scanreq_ssid[i].len, vap->iv_scanreq_ssid[i].ssid);
	db_printf(" roaming %d", vap->iv_roaming);
	db_printf("\n");
	for (i = IEEE80211_MODE_11A; i < IEEE80211_MODE_MAX; i++)
		if (isset(ic->ic_modecaps, i)) {
			_db_show_roamparams("\troamparms[%s]",
			    ieee80211_phymode_name[i], &vap->iv_roamparms[i]);
			db_printf("\n");
		}

	db_printf("\tbmissthreshold %u", vap->iv_bmissthreshold);
	db_printf(" bmiss_max %u", vap->iv_bmiss_count);
	db_printf(" bmiss_max %d", vap->iv_bmiss_max);
	db_printf("\n");
	db_printf("\tswbmiss_count %u", vap->iv_swbmiss_count);
	db_printf(" swbmiss_period %u", vap->iv_swbmiss_period);
	db_printf(" swbmiss %p", &vap->iv_swbmiss);
	db_printf("\n");

	db_printf("\tampdu_rxmax %d", vap->iv_ampdu_rxmax);
	db_printf(" ampdu_density %d", vap->iv_ampdu_density);
	db_printf(" ampdu_limit %d", vap->iv_ampdu_limit);
	db_printf(" amsdu_limit %d", vap->iv_amsdu_limit);
	db_printf("\n");

	db_printf("\tmax_aid %u", vap->iv_max_aid);
	db_printf(" aid_bitmap %p", vap->iv_aid_bitmap);
	db_printf("\n");
	db_printf("\tsta_assoc %u", vap->iv_sta_assoc);
	db_printf(" ps_sta %u", vap->iv_ps_sta);
	db_printf(" ps_pending %u", vap->iv_ps_pending);
	db_printf(" tim_len %u", vap->iv_tim_len);
	db_printf(" tim_bitmap %p", vap->iv_tim_bitmap);
	db_printf("\n");
	db_printf("\tdtim_period %u", vap->iv_dtim_period);
	db_printf(" dtim_count %u", vap->iv_dtim_count);
	db_printf(" set_tim %p", vap->iv_set_tim);
	db_printf(" csa_count %d", vap->iv_csa_count);
	db_printf("\n");

	db_printf("\trtsthreshold %u", vap->iv_rtsthreshold);
	db_printf(" fragthreshold %u", vap->iv_fragthreshold);
	db_printf(" inact_timer %d", vap->iv_inact_timer);
	db_printf("\n");
	for (i = IEEE80211_MODE_11A; i < IEEE80211_MODE_MAX; i++)
		if (isset(ic->ic_modecaps, i)) {
			_db_show_txparams("\ttxparms[%s]",
			    ieee80211_phymode_name[i], &vap->iv_txparms[i]);
			db_printf("\n");
		}

	/* application-specified IE's to attach to mgt frames */
	_db_show_appie("\tappie_beacon", vap->iv_appie_beacon);
	_db_show_appie("\tappie_probereq", vap->iv_appie_probereq);
	_db_show_appie("\tappie_proberesp", vap->iv_appie_proberesp);
	_db_show_appie("\tappie_assocreq", vap->iv_appie_assocreq);
	_db_show_appie("\tappie_asscoresp", vap->iv_appie_assocresp);
	_db_show_appie("\tappie_wpa", vap->iv_appie_wpa);
	if (vap->iv_wpa_ie != NULL || vap->iv_rsn_ie != NULL) {
		if (vap->iv_wpa_ie != NULL)
			db_printf("\twpa_ie %p", vap->iv_wpa_ie);
		if (vap->iv_rsn_ie != NULL)
			db_printf("\trsn_ie %p", vap->iv_rsn_ie);
		db_printf("\n");
	}
	db_printf("\tmax_keyix %u", vap->iv_max_keyix);
	db_printf(" def_txkey %d", vap->iv_def_txkey);
	db_printf("\n");
	for (i = 0; i < IEEE80211_WEP_NKID; i++)
		_db_show_key("\tnw_keys[%u]", i, &vap->iv_nw_keys[i]);

	db_printf("\tauth %p(%s)", vap->iv_auth, vap->iv_auth->ia_name);
	db_printf(" ec %p", vap->iv_ec);

	db_printf(" acl %p", vap->iv_acl);
	db_printf(" as %p", vap->iv_as);
	db_printf("\n");
#ifdef IEEE80211_SUPPORT_MESH
	if (showmesh && vap->iv_mesh != NULL)
		_db_show_mesh(vap->iv_mesh);
#endif
#ifdef IEEE80211_SUPPORT_TDMA
	if (vap->iv_tdma != NULL)
		_db_show_tdma("\t", vap->iv_tdma, showprocs);
#endif /* IEEE80211_SUPPORT_TDMA */
	if (showprocs) {
		DB_PRINTSYM("\t", "iv_key_alloc", vap->iv_key_alloc);
		DB_PRINTSYM("\t", "iv_key_delete", vap->iv_key_delete);
		DB_PRINTSYM("\t", "iv_key_set", vap->iv_key_set);
		DB_PRINTSYM("\t", "iv_key_update_begin", vap->iv_key_update_begin);
		DB_PRINTSYM("\t", "iv_key_update_end", vap->iv_key_update_end);
		DB_PRINTSYM("\t", "iv_opdetach", vap->iv_opdetach);
		DB_PRINTSYM("\t", "iv_input", vap->iv_input);
		DB_PRINTSYM("\t", "iv_recv_mgmt", vap->iv_recv_mgmt);
		DB_PRINTSYM("\t", "iv_deliver_data", vap->iv_deliver_data);
		DB_PRINTSYM("\t", "iv_bmiss", vap->iv_bmiss);
		DB_PRINTSYM("\t", "iv_reset", vap->iv_reset);
		DB_PRINTSYM("\t", "iv_update_beacon", vap->iv_update_beacon);
		DB_PRINTSYM("\t", "iv_newstate", vap->iv_newstate);
		DB_PRINTSYM("\t", "iv_output", vap->iv_output);
	}
}

static void
_db_show_com(const struct ieee80211com *ic, int showvaps, int showsta,
    int showmesh, int showprocs)
{
	struct ieee80211vap *vap;

	db_printf("%p:", ic);
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
		db_printf(" %s(%p)", vap->iv_ifp->if_xname, vap);
	db_printf("\n");
	db_printf("\tsoftc %p", ic->ic_softc);
	db_printf("\tname %s", ic->ic_name);
	db_printf(" comlock %p", &ic->ic_comlock);
	db_printf(" txlock %p", &ic->ic_txlock);
	db_printf(" fflock %p", &ic->ic_fflock);
	db_printf("\n");
	db_printf("\theadroom %d", ic->ic_headroom);
	db_printf(" phytype %d", ic->ic_phytype);
	db_printf(" opmode %s", ieee80211_opmode_name[ic->ic_opmode]);
	db_printf("\n");
	db_printf(" inact %p", &ic->ic_inact);
	db_printf("\n");

	db_printf("\tflags=%b\n", ic->ic_flags, IEEE80211_F_BITS);
	db_printf("\tflags_ext=%b\n", ic->ic_flags_ext, IEEE80211_FEXT_BITS);
	db_printf("\tflags_ht=%b\n", ic->ic_flags_ht, IEEE80211_FHT_BITS);
	db_printf("\tflags_ven=%b\n", ic->ic_flags_ven, IEEE80211_FVEN_BITS);
	db_printf("\tcaps=%b\n", ic->ic_caps, IEEE80211_C_BITS);
	db_printf("\tcryptocaps=%b\n",
	    ic->ic_cryptocaps, IEEE80211_CRYPTO_BITS);
	db_printf("\thtcaps=%b\n", ic->ic_htcaps, IEEE80211_HTCAP_BITS);

#if 0
	uint8_t			ic_modecaps[2];	/* set of mode capabilities */
#endif
	db_printf("\tcurmode %u", ic->ic_curmode);
	db_printf(" promisc %u", ic->ic_promisc);
	db_printf(" allmulti %u", ic->ic_allmulti);
	db_printf(" nrunning %u", ic->ic_nrunning);
	db_printf("\n");
	db_printf("\tbintval %u", ic->ic_bintval);
	db_printf(" lintval %u", ic->ic_lintval);
	db_printf(" holdover %u", ic->ic_holdover);
	db_printf(" txpowlimit %u", ic->ic_txpowlimit);
	db_printf("\n");
#if 0
	struct ieee80211_rateset ic_sup_rates[IEEE80211_MODE_MAX];
#endif
	/*
	 * Channel state:
	 *
	 * ic_channels is the set of available channels for the device;
	 *    it is setup by the driver
	 * ic_nchans is the number of valid entries in ic_channels
	 * ic_chan_avail is a bit vector of these channels used to check
	 *    whether a channel is available w/o searching the channel table.
	 * ic_chan_active is a (potentially) constrained subset of
	 *    ic_chan_avail that reflects any mode setting or user-specified
	 *    limit on the set of channels to use/scan
	 * ic_curchan is the current channel the device is set to; it may
	 *    be different from ic_bsschan when we are off-channel scanning
	 *    or otherwise doing background work
	 * ic_bsschan is the channel selected for operation; it may
	 *    be undefined (IEEE80211_CHAN_ANYC)
	 * ic_prevchan is a cached ``previous channel'' used to optimize
	 *    lookups when switching back+forth between two channels
	 *    (e.g. for dynamic turbo)
	 */
	db_printf("\tnchans %d", ic->ic_nchans);
#if 0
	struct ieee80211_channel ic_channels[IEEE80211_CHAN_MAX];
	uint8_t			ic_chan_avail[IEEE80211_CHAN_BYTES];
	uint8_t			ic_chan_active[IEEE80211_CHAN_BYTES];
	uint8_t			ic_chan_scan[IEEE80211_CHAN_BYTES];
#endif
	db_printf("\n");
	_db_show_channel("\tcurchan", ic->ic_curchan);
	db_printf("\n");
	_db_show_channel("\tbsschan", ic->ic_bsschan);
	db_printf("\n");
	_db_show_channel("\tprevchan", ic->ic_prevchan);
	db_printf("\n");
	db_printf("\tregdomain %p", &ic->ic_regdomain);
	db_printf("\n");

	_db_show_channel("\tcsa_newchan", ic->ic_csa_newchan);
	db_printf(" csa_count %d", ic->ic_csa_count);
	db_printf( "dfs %p", &ic->ic_dfs);
	db_printf("\n");

	db_printf("\tscan %p", ic->ic_scan);
	db_printf(" lastdata %d", ic->ic_lastdata);
	db_printf(" lastscan %d", ic->ic_lastscan);
	db_printf("\n");

	db_printf("\tmax_keyix %d", ic->ic_max_keyix);
	db_printf(" hash_key 0x%x", ic->ic_hash_key);
	db_printf(" wme %p", &ic->ic_wme);
	if (!showsta)
		db_printf(" sta %p", &ic->ic_sta);
	db_printf("\n");
	db_printf("\tstageq@%p:\n", &ic->ic_stageq);
	_db_show_ageq("\t", &ic->ic_stageq);
	if (showsta)
		_db_show_node_table("\t", &ic->ic_sta);

	db_printf("\tprotmode %d", ic->ic_protmode);
	db_printf(" nonerpsta %u", ic->ic_nonerpsta);
	db_printf(" longslotsta %u", ic->ic_longslotsta);
	db_printf(" lastnonerp %d", ic->ic_lastnonerp);
	db_printf("\n");
	db_printf("\tsta_assoc %u", ic->ic_sta_assoc);
	db_printf(" ht_sta_assoc %u", ic->ic_ht_sta_assoc);
	db_printf(" ht40_sta_assoc %u", ic->ic_ht40_sta_assoc);
	db_printf("\n");
	db_printf("\tcurhtprotmode 0x%x", ic->ic_curhtprotmode);
	db_printf(" htprotmode %d", ic->ic_htprotmode);
	db_printf(" lastnonht %d", ic->ic_lastnonht);
	db_printf("\n");

	db_printf("\tsuperg %p\n", ic->ic_superg);

	db_printf("\tmontaps %d th %p txchan %p rh %p rxchan %p\n",
	    ic->ic_montaps, ic->ic_th, ic->ic_txchan, ic->ic_rh, ic->ic_rxchan);

	if (showprocs) {
		DB_PRINTSYM("\t", "ic_vap_create", ic->ic_vap_create);
		DB_PRINTSYM("\t", "ic_vap_delete", ic->ic_vap_delete);
#if 0
		/* operating mode attachment */
		ieee80211vap_attach	ic_vattach[IEEE80211_OPMODE_MAX];
#endif
		DB_PRINTSYM("\t", "ic_newassoc", ic->ic_newassoc);
		DB_PRINTSYM("\t", "ic_getradiocaps", ic->ic_getradiocaps);
		DB_PRINTSYM("\t", "ic_setregdomain", ic->ic_setregdomain);
		DB_PRINTSYM("\t", "ic_send_mgmt", ic->ic_send_mgmt);
		DB_PRINTSYM("\t", "ic_raw_xmit", ic->ic_raw_xmit);
		DB_PRINTSYM("\t", "ic_updateslot", ic->ic_updateslot);
		DB_PRINTSYM("\t", "ic_update_mcast", ic->ic_update_mcast);
		DB_PRINTSYM("\t", "ic_update_promisc", ic->ic_update_promisc);
		DB_PRINTSYM("\t", "ic_node_alloc", ic->ic_node_alloc);
		DB_PRINTSYM("\t", "ic_node_free", ic->ic_node_free);
		DB_PRINTSYM("\t", "ic_node_cleanup", ic->ic_node_cleanup);
		DB_PRINTSYM("\t", "ic_node_getrssi", ic->ic_node_getrssi);
		DB_PRINTSYM("\t", "ic_node_getsignal", ic->ic_node_getsignal);
		DB_PRINTSYM("\t", "ic_node_getmimoinfo", ic->ic_node_getmimoinfo);
		DB_PRINTSYM("\t", "ic_scan_start", ic->ic_scan_start);
		DB_PRINTSYM("\t", "ic_scan_end", ic->ic_scan_end);
		DB_PRINTSYM("\t", "ic_set_channel", ic->ic_set_channel);
		DB_PRINTSYM("\t", "ic_scan_curchan", ic->ic_scan_curchan);
		DB_PRINTSYM("\t", "ic_scan_mindwell", ic->ic_scan_mindwell);
		DB_PRINTSYM("\t", "ic_recv_action", ic->ic_recv_action);
		DB_PRINTSYM("\t", "ic_send_action", ic->ic_send_action);
		DB_PRINTSYM("\t", "ic_addba_request", ic->ic_addba_request);
		DB_PRINTSYM("\t", "ic_addba_response", ic->ic_addba_response);
		DB_PRINTSYM("\t", "ic_addba_stop", ic->ic_addba_stop);
	}
	if (showvaps && !TAILQ_EMPTY(&ic->ic_vaps)) {
		db_printf("\n");
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
			_db_show_vap(vap, showmesh, showprocs);
	}
	if (showsta && !TAILQ_EMPTY(&ic->ic_sta.nt_node)) {
		const struct ieee80211_node_table *nt = &ic->ic_sta;
		const struct ieee80211_node *ni;

		TAILQ_FOREACH(ni, &nt->nt_node, ni_list) {
			db_printf("\n");
			_db_show_sta(ni);
		}
	}
}

static void
_db_show_all_vaps(void *arg, struct ieee80211com *ic)
{
	int showall = *(int *)arg;

	if (!showall) {
		const struct ieee80211vap *vap;
		db_printf("%s: com %p vaps:", ic->ic_name, ic);
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
			db_printf(" %s(%p)", vap->iv_ifp->if_xname, vap);
		db_printf("\n");
	} else
		_db_show_com(ic, 1, 1, 1, 1);
}

static void
_db_show_node_table(const char *tag, const struct ieee80211_node_table *nt)
{
	int i;

	db_printf("%s%s@%p:\n", tag, nt->nt_name, nt);
	db_printf("%s nodelock %p", tag, &nt->nt_nodelock);
	db_printf(" inact_init %d", nt->nt_inact_init);
	db_printf("%s keyixmax %d keyixmap %p\n",
	    tag, nt->nt_keyixmax, nt->nt_keyixmap);
	for (i = 0; i < nt->nt_keyixmax; i++) {
		const struct ieee80211_node *ni = nt->nt_keyixmap[i];
		if (ni != NULL)
			db_printf("%s [%3u] %p %s\n", tag, i, ni,
			    ether_sprintf(ni->ni_macaddr));
	}
}

static void
_db_show_channel(const char *tag, const struct ieee80211_channel *c)
{
	db_printf("%s ", tag);
	if (c == NULL)
		db_printf("<NULL>");
	else if (c == IEEE80211_CHAN_ANYC)
		db_printf("<ANY>");
	else
		db_printf("[%u (%u) flags=%b maxreg %d maxpow %d minpow %d state 0x%x extieee %u]",
		    c->ic_freq, c->ic_ieee,
		    c->ic_flags, IEEE80211_CHAN_BITS,
		    c->ic_maxregpower, c->ic_maxpower, c->ic_minpower,
		    c->ic_state, c->ic_extieee);
}

static void
_db_show_ssid(const char *tag, int ix, int len, const uint8_t *ssid)
{
	const uint8_t *p;
	int i;

	db_printf(tag, ix);

	if (len > IEEE80211_NWID_LEN)
		len = IEEE80211_NWID_LEN;
	/* determine printable or not */
	for (i = 0, p = ssid; i < len; i++, p++) {
		if (*p < ' ' || *p > 0x7e)
			break;
	}
	if (i == len) {
		db_printf("\"");
		for (i = 0, p = ssid; i < len; i++, p++)
			db_printf("%c", *p);
		db_printf("\"");
	} else {
		db_printf("0x");
		for (i = 0, p = ssid; i < len; i++, p++)
			db_printf("%02x", *p);
	}
}

static void
_db_show_appie(const char *tag, const struct ieee80211_appie *ie)
{
	const uint8_t *p;
	int i;

	if (ie == NULL)
		return;
	db_printf("%s [0x", tag);
	for (i = 0, p = ie->ie_data; i < ie->ie_len; i++, p++)
		db_printf("%02x", *p);
	db_printf("]\n");
}

static void
_db_show_key(const char *tag, int ix, const struct ieee80211_key *wk)
{
	static const uint8_t zerodata[IEEE80211_KEYBUF_SIZE];
	const struct ieee80211_cipher *cip = wk->wk_cipher;
	int keylen = wk->wk_keylen;

	db_printf(tag, ix);
	switch (cip->ic_cipher) {
	case IEEE80211_CIPHER_WEP:
		/* compatibility */
		db_printf(" wepkey %u:%s", wk->wk_keyix,
		    keylen <= 5 ? "40-bit" :
		    keylen <= 13 ? "104-bit" : "128-bit");
		break;
	case IEEE80211_CIPHER_TKIP:
		if (keylen > 128/8)
			keylen -= 128/8;	/* ignore MIC for now */
		db_printf(" TKIP %u:%u-bit", wk->wk_keyix, 8*keylen);
		break;
	case IEEE80211_CIPHER_AES_OCB:
		db_printf(" AES-OCB %u:%u-bit", wk->wk_keyix, 8*keylen);
		break;
	case IEEE80211_CIPHER_AES_CCM:
		db_printf(" AES-CCM %u:%u-bit", wk->wk_keyix, 8*keylen);
		break;
	case IEEE80211_CIPHER_CKIP:
		db_printf(" CKIP %u:%u-bit", wk->wk_keyix, 8*keylen);
		break;
	case IEEE80211_CIPHER_NONE:
		db_printf(" NULL %u:%u-bit", wk->wk_keyix, 8*keylen);
		break;
	default:
		db_printf(" UNKNOWN (0x%x) %u:%u-bit",
			cip->ic_cipher, wk->wk_keyix, 8*keylen);
		break;
	}
	if (wk->wk_rxkeyix != wk->wk_keyix)
		db_printf(" rxkeyix %u", wk->wk_rxkeyix);
	if (memcmp(wk->wk_key, zerodata, keylen) != 0) {
		int i;

		db_printf(" <");
		for (i = 0; i < keylen; i++)
			db_printf("%02x", wk->wk_key[i]);
		db_printf(">");
		if (cip->ic_cipher != IEEE80211_CIPHER_WEP &&
		    wk->wk_keyrsc[IEEE80211_NONQOS_TID] != 0)
			db_printf(" rsc %ju", (uintmax_t)wk->wk_keyrsc[IEEE80211_NONQOS_TID]);
		if (cip->ic_cipher != IEEE80211_CIPHER_WEP &&
		    wk->wk_keytsc != 0)
			db_printf(" tsc %ju", (uintmax_t)wk->wk_keytsc);
		db_printf(" flags=%b", wk->wk_flags, IEEE80211_KEY_BITS);
	}
	db_printf("\n");
}

static void
printrate(const char *tag, int v)
{
	if (v == IEEE80211_FIXED_RATE_NONE)
		db_printf(" %s <none>", tag);
	else if (v == 11)
		db_printf(" %s 5.5", tag);
	else if (v & IEEE80211_RATE_MCS)
		db_printf(" %s MCS%d", tag, v &~ IEEE80211_RATE_MCS);
	else
		db_printf(" %s %d", tag, v/2);
}

static void
_db_show_roamparams(const char *tag, const void *arg,
    const struct ieee80211_roamparam *rp)
{

	db_printf(tag, arg);
	if (rp->rssi & 1)
		db_printf(" rssi %u.5", rp->rssi/2);
	else
		db_printf(" rssi %u", rp->rssi/2);
	printrate("rate", rp->rate);
}

static void
_db_show_txparams(const char *tag, const void *arg,
    const struct ieee80211_txparam *tp)
{

	db_printf(tag, arg);
	printrate("ucastrate", tp->ucastrate);
	printrate("mcastrate", tp->mcastrate);
	printrate("mgmtrate", tp->mgmtrate);
	db_printf(" maxretry %d", tp->maxretry);
}

static void
_db_show_ageq(const char *tag, const struct ieee80211_ageq *q)
{
	const struct mbuf *m;

	db_printf("%s lock %p len %d maxlen %d drops %d head %p tail %p\n",
	    tag, &q->aq_lock, q->aq_len, q->aq_maxlen, q->aq_drops,
	    q->aq_head, q->aq_tail);
	for (m = q->aq_head; m != NULL; m = m->m_nextpkt)
		db_printf("%s %p (len %d, %b)\n", tag, m, m->m_len,
		    /* XXX could be either TX or RX but is mostly TX */
		    m->m_flags, IEEE80211_MBUF_TX_FLAG_BITS);
}

static void
_db_show_stats(const struct ieee80211_stats *is)
{
}

#ifdef IEEE80211_SUPPORT_MESH
static void
_db_show_mesh(const struct ieee80211_mesh_state *ms)
{
	struct ieee80211_mesh_route *rt;
	int i;

	_db_show_ssid(" meshid ", 0, ms->ms_idlen, ms->ms_id);
	db_printf("nextseq %u ttl %u flags 0x%x\n", ms->ms_seq,
	    ms->ms_ttl, ms->ms_flags);
	db_printf("routing table:\n");
	i = 0;
	TAILQ_FOREACH(rt, &ms->ms_routes, rt_next) {
		db_printf("entry %d:\tdest: %6D nexthop: %6D metric: %u", i,
		    rt->rt_dest, ":", rt->rt_nexthop, ":", rt->rt_metric);

		db_printf("\tlifetime: %u lastseq: %u priv: %p\n",
		    ieee80211_mesh_rt_update(rt, 0),
		    rt->rt_lastmseq, rt->rt_priv);
		i++;
	}
}
#endif /* IEEE80211_SUPPORT_MESH */
#endif /* DDB */
