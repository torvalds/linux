/*-
 * Copyright (c) 2002-2007 Sam Leffler, Errno Consulting
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

/*
 * net80211 statistics class.
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/sockio.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_var.h>
#include <net/ethernet.h>

#include <err.h>
#include <ifaddrs.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../../../sys/net80211/ieee80211_ioctl.h"

#include "wlanstats.h"

#ifndef IEEE80211_ADDR_COPY
#define	IEEE80211_ADDR_COPY(dst, src)	memcpy(dst, src, IEEE80211_ADDR_LEN)
#define	IEEE80211_ADDR_EQ(a1,a2)	(memcmp(a1,a2,IEEE80211_ADDR_LEN) == 0)
#endif

#define	AFTER(prev)	((prev)+1)

static const struct fmt wlanstats[] = {
#define	S_RX_BADVERSION		0
	{ 5,  "rx_badversion",	"bvers",	"rx frame with bad version" },
#define	S_RX_TOOSHORT		AFTER(S_RX_BADVERSION)
	{ 5,  "rx_tooshort",	"2short",	"rx frame too short" },
#define	S_RX_WRONGBSS		AFTER(S_RX_TOOSHORT)
	{ 5,  "rx_wrongbss",	"wrbss",	"rx from wrong bssid" },
#define	S_RX_DUP		AFTER(S_RX_WRONGBSS)
	{ 5,  "rx_dup",		"rxdup",	"rx discard 'cuz dup" },
#define	S_RX_WRONGDIR		AFTER(S_RX_DUP)
	{ 5,  "rx_wrongdir",	"wrdir",	"rx w/ wrong direction" },
#define	S_RX_MCASTECHO		AFTER(S_RX_WRONGDIR)
	{ 5,  "rx_mcastecho",	"mecho",	"rx discard 'cuz mcast echo" },
#define	S_RX_NOTASSOC		AFTER(S_RX_MCASTECHO)
	{ 6,  "rx_notassoc",	"!assoc",	"rx discard 'cuz sta !assoc" },
#define	S_RX_NOPRIVACY		AFTER(S_RX_NOTASSOC)
	{ 6,  "rx_noprivacy",	"nopriv",	"rx w/ wep but privacy off" },
#define	S_RX_UNENCRYPTED	AFTER(S_RX_NOPRIVACY)
	{ 6,  "rx_unencrypted",	"unencr",	"rx w/o wep and privacy on" },
#define	S_RX_WEPFAIL		AFTER(S_RX_UNENCRYPTED)
	{ 7,  "rx_wepfail",	"wepfail",	"rx wep processing failed" },
#define	S_RX_DECAP		AFTER(S_RX_WEPFAIL)
	{ 5,  "rx_decap",	"decap",	"rx decapsulation failed" },
#define	S_RX_MGTDISCARD		AFTER(S_RX_DECAP)
	{ 8,  "rx_mgtdiscard",	"mgtdiscard",	"rx discard mgt frames" },
#define	S_RX_CTL		AFTER(S_RX_MGTDISCARD)
	{ 5,  "rx_ctl",		"ctl",		"rx ctrl frames" },
#define	S_RX_BEACON		AFTER(S_RX_CTL)
	{ 6,  "rx_beacon",	"beacon",	"rx beacon frames" },
#define	S_RX_RSTOOBIG		AFTER(S_RX_BEACON)
	{ 6,  "rx_rstoobig",	"rs2big",	"rx rate set truncated" },
#define	S_RX_ELEM_MISSING	AFTER(S_RX_RSTOOBIG)
	{ 6,  "rx_elem_missing","iemiss",	"rx required element missing" },
#define	S_RX_ELEM_TOOBIG	AFTER(S_RX_ELEM_MISSING)
	{ 6,  "rx_elem_toobig",	"ie2big",	"rx element too big" },
#define	S_RX_ELEM_TOOSMALL	AFTER(S_RX_ELEM_TOOBIG)
	{ 7,  "rx_elem_toosmall","ie2small","rx element too small" },
#define	S_RX_ELEM_UNKNOWN 	AFTER(S_RX_ELEM_TOOSMALL)
	{ 5,  "rx_elem_unknown","ieunk",	"rx element unknown" },
#define	S_RX_BADCHAN		AFTER(S_RX_ELEM_UNKNOWN)
	{ 6,  "rx_badchan",	"badchan",	"rx frame w/ invalid chan" },
#define	S_RX_CHANMISMATCH	AFTER(S_RX_BADCHAN)
	{ 5,  "rx_chanmismatch","chanmismatch",	"rx frame chan mismatch" },
#define	S_RX_NODEALLOC		AFTER(S_RX_CHANMISMATCH)
	{ 5,  "rx_nodealloc",	"nodealloc",	"nodes allocated (rx)" },
#define	S_RX_SSIDMISMATCH	AFTER(S_RX_NODEALLOC)
	{ 5,  "rx_ssidmismatch","ssidmismatch",	"rx frame ssid mismatch" },
#define	S_RX_AUTH_UNSUPPORTED	AFTER(S_RX_SSIDMISMATCH)
	{ 5,  "rx_auth_unsupported","auth_unsupported",
		"rx w/ unsupported auth alg" },
#define	S_RX_AUTH_FAIL		AFTER(S_RX_AUTH_UNSUPPORTED)
	{ 5,  "rx_auth_fail",	"auth_fail",	"rx sta auth failure" },
#define	S_RX_AUTH_FAIL_CODE	AFTER(S_RX_AUTH_FAIL)
	{ 5,  "rx_auth_fail_code","auth_fail_code",
		"last rx auth failure reason" },
#define	S_RX_AUTH_COUNTERMEASURES	AFTER(S_RX_AUTH_FAIL_CODE)
	{ 5,  "rx_auth_countermeasures",	"auth_countermeasures",
		"rx sta auth failure 'cuz of TKIP countermeasures" },
#define	S_RX_ASSOC_BSS		AFTER(S_RX_AUTH_COUNTERMEASURES)
	{ 5,  "rx_assoc_bss",	"assoc_bss",	"rx assoc from wrong bssid" },
#define	S_RX_ASSOC_NOTAUTH	AFTER(S_RX_ASSOC_BSS)
	{ 5,  "rx_assoc_notauth","assoc_notauth",	"rx assoc w/o auth" },
#define	S_RX_ASSOC_CAPMISMATCH	AFTER(S_RX_ASSOC_NOTAUTH)
	{ 5,  "rx_assoc_capmismatch","assoc_capmismatch",
		"rx assoc w/ cap mismatch" },
#define	S_RX_ASSOC_NORATE	AFTER(S_RX_ASSOC_CAPMISMATCH)
	{ 5,  "rx_assoc_norate","assoc_norate",	"rx assoc w/ no rate match" },
#define	S_RX_ASSOC_BADWPAIE	AFTER(S_RX_ASSOC_NORATE)
	{ 5,  "rx_assoc_badwpaie","assoc_badwpaie",
		"rx assoc w/ bad WPA IE" },
#define	S_RX_DEAUTH		AFTER(S_RX_ASSOC_BADWPAIE)
	{ 5,  "rx_deauth",	"deauth",	"rx deauthentication" },
#define	S_RX_DEAUTH_CODE	AFTER(S_RX_DEAUTH)
	{ 5,  "rx_deauth_code","deauth_code",	"last rx deauth reason" },
#define	S_RX_DISASSOC		AFTER(S_RX_DEAUTH_CODE)
	{ 5,  "rx_disassoc",	"disassoc",	"rx disassociation" },
#define	S_RX_DISASSOC_CODE	AFTER(S_RX_DISASSOC)
	{ 5,  "rx_disassoc_code","disassoc_code",
		"last rx disassoc reason" },
#define	S_BMISS			AFTER(S_RX_DISASSOC_CODE)
	{ 5,  "bmiss",		"bmiss",	"beacon miss events handled" },
#define	S_RX_BADSUBTYPE		AFTER(S_BMISS)
	{ 5,  "rx_badsubtype",	"badsubtype",	"rx frame w/ unknown subtype" },
#define	S_RX_NOBUF		AFTER(S_RX_BADSUBTYPE)
	{ 5,  "rx_nobuf",	"nobuf",	"rx failed for lack of mbuf" },
#define	S_RX_DECRYPTCRC		AFTER(S_RX_NOBUF)
	{ 5,  "rx_decryptcrc",	"decryptcrc",	"rx decrypt failed on crc" },
#define	S_RX_AHDEMO_MGT		AFTER(S_RX_DECRYPTCRC)
	{ 5,  "rx_ahdemo_mgt",	"ahdemo_mgt",
		"rx discard mgmt frame received in ahdoc demo mode" },
#define	S_RX_BAD_AUTH		AFTER(S_RX_AHDEMO_MGT)
	{ 5,  "rx_bad_auth",	"bad_auth",	"rx bad authentication request" },
#define	S_RX_UNAUTH		AFTER(S_RX_BAD_AUTH)
	{ 5,  "rx_unauth",	"unauth",
		"rx discard 'cuz port unauthorized" },
#define	S_RX_BADKEYID		AFTER(S_RX_UNAUTH)
	{ 5,  "rx_badkeyid",	"rxkid",	"rx w/ incorrect keyid" },
#define	S_RX_CCMPREPLAY		AFTER(S_RX_BADKEYID)
	{ 5,  "rx_ccmpreplay",	"ccmpreplay",	"rx seq# violation (CCMP)" },
#define	S_RX_CCMPFORMAT		AFTER(S_RX_CCMPREPLAY)
	{ 5,  "rx_ccmpformat",	"ccmpformat",	"rx format bad (CCMP)" },
#define	S_RX_CCMPMIC		AFTER(S_RX_CCMPFORMAT)
	{ 5,  "rx_ccmpmic",	"ccmpmic",	"rx MIC check failed (CCMP)" },
#define	S_RX_TKIPREPLAY		AFTER(S_RX_CCMPMIC)
	{ 5,  "rx_tkipreplay",	"tkipreplay",	"rx seq# violation (TKIP)" },
#define	S_RX_TKIPFORMAT		AFTER(S_RX_TKIPREPLAY)
	{ 5,  "rx_tkipformat",	"tkipformat",	"rx format bad (TKIP)" },
#define	S_RX_TKIPMIC		AFTER(S_RX_TKIPFORMAT)
	{ 5,  "rx_tkipmic",	"tkipmic",	"rx MIC check failed (TKIP)" },
#define	S_RX_TKIPICV		AFTER(S_RX_TKIPMIC)
	{ 5,  "rx_tkipicv",	"tkipicv",	"rx ICV check failed (TKIP)" },
#define	S_RX_BADCIPHER		AFTER(S_RX_TKIPICV)
	{ 5,  "rx_badcipher",	"badcipher",	"rx failed 'cuz bad cipher/key type" },
#define	S_RX_NOCIPHERCTX	AFTER(S_RX_BADCIPHER)
	{ 5,  "rx_nocipherctx",	"nocipherctx",	"rx failed 'cuz key/cipher ctx not setup" },
#define	S_RX_ACL		AFTER(S_RX_NOCIPHERCTX)
	{ 5,  "rx_acl",		"acl",		"rx discard 'cuz acl policy" },
#define	S_TX_NOBUF		AFTER(S_RX_ACL)
	{ 5,  "tx_nobuf",	"nobuf",	"tx failed for lack of mbuf" },
#define	S_TX_NONODE		AFTER(S_TX_NOBUF)
	{ 5,  "tx_nonode",	"nonode",	"tx failed for no node" },
#define	S_TX_UNKNOWNMGT		AFTER(S_TX_NONODE)
	{ 5,  "tx_unknownmgt",	"unknownmgt",	"tx of unknown mgt frame" },
#define	S_TX_BADCIPHER		AFTER(S_TX_UNKNOWNMGT)
	{ 5,  "tx_badcipher",	"badcipher",	"tx failed 'cuz bad ciper/key type" },
#define	S_TX_NODEFKEY		AFTER(S_TX_BADCIPHER)
	{ 5,  "tx_nodefkey",	"nodefkey",	"tx failed 'cuz no defkey" },
#define	S_TX_NOHEADROOM		AFTER(S_TX_NODEFKEY)
	{ 5,  "tx_noheadroom",	"noheadroom",	"tx failed 'cuz no space for crypto hdrs" },
#define	S_TX_FRAGFRAMES		AFTER(S_TX_NOHEADROOM)
	{ 5,  "tx_fragframes",	"fragframes",	"tx frames fragmented" },
#define	S_TX_FRAGS		AFTER(S_TX_FRAGFRAMES)
	{ 5,  "tx_frags",	"frags",		"tx frags generated" },
#define	S_SCAN_ACTIVE		AFTER(S_TX_FRAGS)
	{ 5,  "scan_active",	"ascan",	"active scans started" },
#define	S_SCAN_PASSIVE		AFTER(S_SCAN_ACTIVE)
	{ 5,  "scan_passive",	"pscan",	"passive scans started" },
#define	S_SCAN_BG		AFTER(S_SCAN_PASSIVE)
	{ 5,  "scan_bg",	"bgscn",	"background scans started" },
#define	S_NODE_TIMEOUT		AFTER(S_SCAN_BG)
	{ 5,  "node_timeout",	"node_timeout",	"nodes timed out for inactivity" },
#define	S_CRYPTO_NOMEM		AFTER(S_NODE_TIMEOUT)
	{ 5,  "crypto_nomem",	"crypto_nomem",	"cipher context malloc failed" },
#define	S_CRYPTO_TKIP		AFTER(S_CRYPTO_NOMEM)
	{ 5,  "crypto_tkip",	"crypto_tkip",	"tkip crypto done in s/w" },
#define	S_CRYPTO_TKIPENMIC	AFTER(S_CRYPTO_TKIP)
	{ 5,  "crypto_tkipenmic","crypto_tkipenmic",	"tkip tx MIC done in s/w" },
#define	S_CRYPTO_TKIPDEMIC	AFTER(S_CRYPTO_TKIPENMIC)
	{ 5,  "crypto_tkipdemic","crypto_tkipdemic",	"tkip rx MIC done in s/w" },
#define	S_CRYPTO_TKIPCM		AFTER(S_CRYPTO_TKIPDEMIC)
	{ 5,  "crypto_tkipcm",	"crypto_tkipcm",	"tkip dropped frames 'cuz of countermeasures" },
#define	S_CRYPTO_CCMP		AFTER(S_CRYPTO_TKIPCM)
	{ 5,  "crypto_ccmp",	"crypto_ccmp",	"ccmp crypto done in s/w" },
#define	S_CRYPTO_WEP		AFTER(S_CRYPTO_CCMP)
	{ 5,  "crypto_wep",	"crypto_wep",	"wep crypto done in s/w" },
#define	S_CRYPTO_SETKEY_CIPHER	AFTER(S_CRYPTO_WEP)
	{ 5,  "crypto_setkey_cipher",	"crypto_setkey_cipher","setkey failed 'cuz cipher rejected data" },
#define	S_CRYPTO_SETKEY_NOKEY	AFTER(S_CRYPTO_SETKEY_CIPHER)
	{ 5,  "crypto_setkey_nokey",	"crypto_setkey_nokey","setkey failed 'cuz no key index" },
#define	S_CRYPTO_DELKEY		AFTER(S_CRYPTO_SETKEY_NOKEY)
	{ 5,  "crypto_delkey",	"crypto_delkey",	"driver key delete failed" },
#define	S_CRYPTO_BADCIPHER	AFTER(S_CRYPTO_DELKEY)
	{ 5,  "crypto_badcipher","crypto_badcipher",	"setkey failed 'cuz unknown cipher" },
#define	S_CRYPTO_NOCIPHER	AFTER(S_CRYPTO_BADCIPHER)
	{ 5,  "crypto_nocipher","crypto_nocipher",	"setkey failed 'cuz cipher module unavailable" },
#define	S_CRYPTO_ATTACHFAIL	AFTER(S_CRYPTO_NOCIPHER)
	{ 5,  "crypto_attachfail","crypto_attachfail",	"setkey failed 'cuz cipher attach failed" },
#define	S_CRYPTO_SWFALLBACK	AFTER(S_CRYPTO_ATTACHFAIL)
	{ 5,  "crypto_swfallback","crypto_swfallback",	"crypto fell back to s/w implementation" },
#define	S_CRYPTO_KEYFAIL	AFTER(S_CRYPTO_SWFALLBACK)
	{ 5,  "crypto_keyfail",	"crypto_keyfail",	"setkey failed 'cuz driver key alloc failed" },
#define	S_CRYPTO_ENMICFAIL	AFTER(S_CRYPTO_KEYFAIL)
	{ 5,  "crypto_enmicfail","crypto_enmicfail",	"enmic failed (may be mbuf exhaustion)" },
#define	S_IBSS_CAPMISMATCH	AFTER(S_CRYPTO_ENMICFAIL)
	{ 5,  "ibss_capmismatch","ibss_capmismatch",	"ibss merge faied 'cuz capabilities mismatch" },
#define	S_IBSS_NORATE		AFTER(S_IBSS_CAPMISMATCH)
	{ 5,  "ibss_norate",	"ibss_norate",	"ibss merge faied 'cuz rate set mismatch" },
#define	S_PS_UNASSOC		AFTER(S_IBSS_NORATE)
	{ 5,  "ps_unassoc",	"ps_unassoc",	"ps-poll received for unassociated station" },
#define	S_PS_BADAID		AFTER(S_PS_UNASSOC)
	{ 5,  "ps_badaid",	"ps_badaid",	"ps-poll received with invalid association id" },
#define	S_PS_QEMPTY		AFTER(S_PS_BADAID)
	{ 5,  "ps_qempty",	"ps_qempty",	"ps-poll received with nothing to send" },
#define	S_FF_BADHDR		AFTER(S_PS_QEMPTY)
	{ 5,  "ff_badhdr",	"ff_badhdr",	"fast frame rx'd w/ bad hdr" },
#define	S_FF_TOOSHORT		AFTER(S_FF_BADHDR)
	{ 5,  "ff_tooshort",	"ff_tooshort",	"fast frame rx decap error" },
#define	S_FF_SPLIT		AFTER(S_FF_TOOSHORT)
	{ 5,  "ff_split",	"ff_split",	"fast frame rx split error" },
#define	S_FF_DECAP		AFTER(S_FF_SPLIT)
	{ 5,  "ff_decap",	"ff_decap",	"fast frames decap'd" },
#define	S_FF_ENCAP		AFTER(S_FF_DECAP)
	{ 5,  "ff_encap",	"ff_encap",	"fast frames encap'd for tx" },
#define	S_FF_ENCAPFAIL		AFTER(S_FF_ENCAP)
	{ 5,  "ff_encapfail",	"ff_encapfail",	"fast frames encap failed" },
#define	S_RX_BADBINTVAL		AFTER(S_FF_ENCAPFAIL)
	{ 5,  "rx_badbintval",	"rx_badbintval","rx frame with bogus beacon interval" },
#define	S_RX_MGMT		AFTER(S_RX_BADBINTVAL)
	{ 8,  "rx_mgmt",	"mgmt",		"rx management frames" },
#define	S_RX_DEMICFAIL		AFTER(S_RX_MGMT)
	{ 5,  "rx_demicfail",	"rx_demicfail",	"rx demic failed" },
#define	S_RX_DEFRAG		AFTER(S_RX_DEMICFAIL)
	{ 5,  "rx_defrag",	"rx_defrag",	"rx defragmentation failed" },
#define	S_RX_ACTION		AFTER(S_RX_DEFRAG)
	{ 5,  "rx_action",	"rx_action",	"rx action frames" },
#define	S_AMSDU_TOOSHORT	AFTER(S_RX_ACTION)
	{ 8,  "amsdu_tooshort",	"tooshort","A-MSDU rx decap error" },
#define	S_AMSDU_SPLIT		AFTER(S_AMSDU_TOOSHORT)
	{ 8,  "amsdu_split",	"split",	"A-MSDU rx failed on frame split" },
#define	S_AMSDU_DECAP		AFTER(S_AMSDU_SPLIT)
	{ 8,  "amsdu_decap",	"decap",	"A-MSDU frames received" },
#define	S_AMSDU_ENCAP		AFTER(S_AMSDU_DECAP)
	{ 8,  "amsdu_encap",	"encap",	"A-MSDU frames transmitted" },
#define	S_AMPDU_REORDER		AFTER(S_AMSDU_ENCAP)
	{ 8,  "ampdu_reorder",	"reorder","A-MPDU frames held in reorder q" },
#define	S_AMPDU_FLUSH		AFTER(S_AMPDU_REORDER)
	{ 8,  "ampdu_flush",	"flush",	"A-MPDU frames sent up from reorder q" },
#define	S_AMPDU_BARBAD		AFTER(S_AMPDU_FLUSH)
	{ 6,  "ampdu_barbad",	"barbad",	"A-MPDU BAR rx before ADDBA exchange (or disabled with net.link.ieee80211)" },
#define	S_AMPDU_BAROOW		AFTER(S_AMPDU_BARBAD)
	{ 6,  "ampdu_baroow",	"baroow",	"A-MPDU BAR rx out of BA window" },
#define	S_AMPDU_BARMOVE		AFTER(S_AMPDU_BAROOW)
	{ 8,  "ampdu_barmove",	"barmove","A-MPDU BAR rx moved BA window" },
#define	S_AMPDU_BAR		AFTER(S_AMPDU_BARMOVE)
	{ 8,  "ampdu_bar",	"rxbar",	"A-MPDU BAR rx successful" },
#define	S_AMPDU_MOVE		AFTER(S_AMPDU_BAR)
	{ 5,  "ampdu_move",	"move",	"A-MPDU frame moved BA window" },
#define	S_AMPDU_OOR		AFTER(S_AMPDU_MOVE)
	{ 8,  "ampdu_oor",	"oorx",	"A-MPDU frames rx out-of-order" },
#define	S_AMPDU_COPY		AFTER(S_AMPDU_OOR)
	{ 8,  "ampdu_copy",	"copy",	"A-MPDU rx window slots copied" },
#define	S_AMPDU_DROP		AFTER(S_AMPDU_COPY)
	{ 5,  "ampdu_drop",	"drop",	"A-MPDU frames discarded for out of range seqno" },
#define	S_AMPDU_AGE		AFTER(S_AMPDU_DROP)
	{ 5,  "ampdu_age",	"age",	"A-MPDU frames sent up due to old age" },
#define	S_AMPDU_STOP		AFTER(S_AMPDU_AGE)
	{ 5,  "ampdu_stop",	"stop",	"A-MPDU streams stopped" },
#define	S_AMPDU_STOP_FAILED	AFTER(S_AMPDU_STOP)
	{ 5,  "ampdu_stop_failed","!stop",	"A-MPDU stop requests failed 'cuz stream not running" },
#define	S_ADDBA_REJECT		AFTER(S_AMPDU_STOP_FAILED)
	{ 5,  "addba_reject",	"reject",	"ADDBA requests rejected 'cuz A-MPDU rx is disabled" },
#define	S_ADDBA_NOREQUEST	AFTER(S_ADDBA_REJECT)
	{ 5,  "addba_norequest","norequest","ADDBA response frames discarded because no ADDBA request was pending" },
#define	S_ADDBA_BADTOKEN	AFTER(S_ADDBA_NOREQUEST)
	{ 5,  "addba_badtoken",	"badtoken","ADDBA response frames discarded 'cuz rx'd dialog token is wrong" },
#define	S_TX_BADSTATE		AFTER(S_ADDBA_BADTOKEN)
	{ 4,  "tx_badstate",	"badstate",	"tx failed 'cuz vap not in RUN state" },
#define	S_TX_NOTASSOC		AFTER(S_TX_BADSTATE)
	{ 4,  "tx_notassoc",	"notassoc",	"tx failed 'cuz dest sta not associated" },
#define	S_TX_CLASSIFY		AFTER(S_TX_NOTASSOC)
	{ 4,  "tx_classify",	"classify",	"tx packet classification failed" },
#define	S_DWDS_MCAST		AFTER(S_TX_CLASSIFY)
	{ 8,  "dwds_mcast",	"dwds_mcast",	"mcast frame transmitted on dwds vap discarded" },
#define	S_DWDS_QDROP		AFTER(S_DWDS_MCAST)
	{ 8,  "dwds_qdrop",	"dwds_qdrop",	"4-address frame discarded because dwds pending queue is full" },
#define	S_HT_ASSOC_NOHTCAP	AFTER(S_DWDS_QDROP)
	{ 4,  "ht_nohtcap",	"ht_nohtcap",	"non-HT station rejected in HT-only BSS" },
#define	S_HT_ASSOC_DOWNGRADE	AFTER(S_HT_ASSOC_NOHTCAP)
	{ 4,  "ht_downgrade",	"ht_downgrade",	"HT station downgraded to legacy operation" },
#define	S_HT_ASSOC_NORATE	AFTER(S_HT_ASSOC_DOWNGRADE)
	{ 4,  "ht_norate",	"ht_norate",	"HT station rejected because of HT rate set" },
#define	S_MESH_WRONGMESH	AFTER(S_HT_ASSOC_NORATE)
	{ 4,  "mesh_wrong",	"mesh_wrong",	"frame discarded because sender not a mesh sta" },
#define	S_MESH_NOLINK		AFTER(S_MESH_WRONGMESH)
	{ 4,  "mesh_nolink",	"mesh_nolink",	"frame discarded because link not established" },
#define	S_MESH_FWD_TTL		AFTER(S_MESH_NOLINK)
	{ 4,  "mesh_fwd_ttl",	"mesh_fwd_ttl",	"frame not forwarded because TTL zero" },
#define	S_MESH_FWD_NOBUF	AFTER(S_MESH_FWD_TTL)
	{ 4,  "mesh_fwd_nobuf",	"mesh_fwd_nobuf",	"frame not forwarded because mbuf could not be allocated" },
#define	S_MESH_FWD_TOOSHORT	AFTER(S_MESH_FWD_NOBUF)
	{ 4,  "mesh_fwd_tooshort",	"mesh_fwd_tooshort",	"frame not forwarded because too short to have 802.11 header" },
#define	S_MESH_FWD_DISABLED	AFTER(S_MESH_FWD_TOOSHORT)
	{ 4,  "mesh_fwd_disabled",	"mesh_fwd_disabled",	"frame not forwarded because administratively disabled" },
#define	S_MESH_FWD_NOPATH	AFTER(S_MESH_FWD_DISABLED)
	{ 4,  "mesh_fwd_nopath",	"mesh_fwd_nopath",	"frame not forwarded because no path found to destination" },
#define	S_HWMP_WRONGSEQ		AFTER(S_MESH_FWD_NOPATH)
	{ 4,  "hwmp_wrongseq",	"hwmp_wrongseq",	"frame discarded because mesh sequence number is invalid" },
#define	S_HWMP_ROOTREQS		AFTER(S_HWMP_WRONGSEQ)
	{ 4,  "hwmp_rootreqs",	"hwmp_rootreqs",	"root PREQ frames sent" },
#define	S_HWMP_ROOTANN		AFTER(S_HWMP_ROOTREQS)
	{ 4,  "hwmp_rootann",	"hwmp_rootann",	"root RANN frames received" },
#define	S_MESH_BADAE		AFTER(S_HWMP_ROOTANN)
	{ 4,  "mesh_badae",	"mesh_badae",	"frame discarded for bad AddressExtension (AE)" },
#define	S_MESH_RTADDFAILED	AFTER(S_MESH_BADAE)
	{ 4,  "mesh_rtadd",	"mesh_rtadd",	"mesh route add failed" },
#define	S_MESH_NOTPROXY		AFTER(S_MESH_RTADDFAILED)
	{ 8,  "mesh_notproxy",	"mesh_notproxy","frame discarded because station not acting as a proxy" },
#define	S_RX_BADALIGN		AFTER(S_MESH_NOTPROXY)
	{ 4,  "rx_badalign",	"rx_badalign","frame discarded because payload re-alignment failed" },
#define	S_INPUT			AFTER(S_RX_BADALIGN)
	{ 8,	"input",	"input",	"total data frames received" },
#define	S_RX_UCAST		AFTER(S_INPUT)
	{ 8,	"rx_ucast",	"rx_ucast",	"unicast data frames received" },
#define	S_RX_MCAST		AFTER(S_RX_UCAST)
	{ 8,	"rx_mcast",	"rx_mcast",	"multicast data frames received" },
#define	S_OUTPUT		AFTER(S_RX_MCAST)
	{ 8,	"output",	"output",	"total data frames transmit" },
#define	S_TX_UCAST		AFTER(S_OUTPUT)
	{ 8,	"tx_ucast",	"tx_ucast",	"unicast data frames sent" },
#define	S_TX_MCAST		AFTER(S_TX_UCAST)
	{ 8,	"tx_mcast",	"tx_mcast",	"multicast data frames sent" },
#define	S_RATE			AFTER(S_TX_MCAST)
	{ 7,	"rate",		"rate",		"current transmit rate" },
#define	S_RSSI			AFTER(S_RATE)
	{ 6,	"rssi",		"rssi",		"current rssi" },
#define	S_NOISE			AFTER(S_RSSI)
	{ 5,	"noise",	"noise",	"current noise floor (dBm)" },
#define	S_SIGNAL		AFTER(S_NOISE)
	{ 6,	"signal",	"sig",		"current signal (dBm)" },
#define	S_BEACON_BAD		AFTER(S_SIGNAL)
	{ 9,	"beacon_bad",	"beaconbad",	"bad beacons received" },
#define	S_AMPDU_BARTX		AFTER(S_BEACON_BAD)
	{ 5,	"ampdu_bartx",	"bartx",	"BAR frames sent" },
#define	S_AMPDU_BARTX_FAIL	AFTER(S_AMPDU_BARTX)
	{ 9,	"ampdu_bartxfail",	"bartx_fail",	"BAR frames failed to send" },
#define	S_AMPDU_BARTX_RETRY	AFTER(S_AMPDU_BARTX_FAIL)
	{ 10,	"ampdu_bartxretry",	"bartx_retry",	"BAR frames retried" },
};

struct wlanstatfoo_p {
	struct wlanstatfoo base;
	int s;
	int opmode;
	uint8_t mac[IEEE80211_ADDR_LEN];
	struct ifreq ifr;
	struct ieee80211_stats cur;
	struct ieee80211_stats total;
	struct ieee80211req ireq;
	union {
		struct ieee80211req_sta_req info;
		char buf[1024];
	} u_info;
	struct ieee80211req_sta_stats ncur;
	struct ieee80211req_sta_stats ntotal;
};

static void
wlan_setifname(struct wlanstatfoo *wf0, const char *ifname)
{
	struct wlanstatfoo_p *wf = (struct wlanstatfoo_p *) wf0;

	strncpy(wf->ifr.ifr_name, ifname, sizeof (wf->ifr.ifr_name));
	strncpy(wf->ireq.i_name, ifname, sizeof (wf->ireq.i_name));
}

static const char *
wlan_getifname(struct wlanstatfoo *wf0)
{
	struct wlanstatfoo_p *wf = (struct wlanstatfoo_p *) wf0;

	return wf->ifr.ifr_name;
}

static int
wlan_getopmode(struct wlanstatfoo *wf0)
{
	struct wlanstatfoo_p *wf = (struct wlanstatfoo_p *) wf0;

	if (wf->opmode == -1) {
		struct ifmediareq ifmr;

		memset(&ifmr, 0, sizeof(ifmr));
		strlcpy(ifmr.ifm_name, wf->ifr.ifr_name, sizeof(ifmr.ifm_name));
		if (ioctl(wf->s, SIOCGIFMEDIA, &ifmr) < 0)
			err(1, "%s (SIOCGIFMEDIA)", wf->ifr.ifr_name);
		if (ifmr.ifm_current & IFM_IEEE80211_ADHOC) {
			if (ifmr.ifm_current & IFM_FLAG0)
				wf->opmode = IEEE80211_M_AHDEMO;
			else
				wf->opmode = IEEE80211_M_IBSS;
		} else if (ifmr.ifm_current & IFM_IEEE80211_HOSTAP)
			wf->opmode = IEEE80211_M_HOSTAP;
		else if (ifmr.ifm_current & IFM_IEEE80211_MONITOR)
			wf->opmode = IEEE80211_M_MONITOR;
		else
			wf->opmode = IEEE80211_M_STA;
	}
	return wf->opmode;
}

static void
getlladdr(struct wlanstatfoo_p *wf)
{
	const struct sockaddr_dl *sdl;
	struct ifaddrs *ifp, *p;

	if (getifaddrs(&ifp) != 0)
		err(1, "getifaddrs");
	for (p = ifp; p != NULL; p = p->ifa_next)
		if (strcmp(p->ifa_name, wf->ifr.ifr_name) == 0 &&
		    p->ifa_addr->sa_family == AF_LINK)
			break;
	if (p == NULL)
		errx(1, "did not find link layer address for interface %s",
			wf->ifr.ifr_name);
	sdl = (const struct sockaddr_dl *) p->ifa_addr;
	IEEE80211_ADDR_COPY(wf->mac, LLADDR(sdl));
	freeifaddrs(ifp);
}

static int
getbssid(struct wlanstatfoo_p *wf)
{
	wf->ireq.i_type = IEEE80211_IOC_BSSID;
	wf->ireq.i_data = wf->mac;
	wf->ireq.i_len = IEEE80211_ADDR_LEN;
	return ioctl(wf->s, SIOCG80211, &wf->ireq);
}

static void
wlan_setstamac(struct wlanstatfoo *wf0, const uint8_t *mac)
{
	static const uint8_t zeromac[IEEE80211_ADDR_LEN];
	struct wlanstatfoo_p *wf = (struct wlanstatfoo_p *) wf0;

	if (mac == NULL) {
		switch (wlan_getopmode(wf0)) {
		case IEEE80211_M_HOSTAP:
		case IEEE80211_M_MONITOR:
			getlladdr(wf);
			break;
		case IEEE80211_M_IBSS:
		case IEEE80211_M_AHDEMO:
			/*
			 * NB: this may not work in which case the
			 * mac must be specified on the command line
			 */
			if (getbssid(wf) < 0 ||
			    IEEE80211_ADDR_EQ(wf->mac, zeromac))
				getlladdr(wf);
			break;
		case IEEE80211_M_STA:
			if (getbssid(wf) < 0)
				err(1, "%s (IEEE80211_IOC_BSSID)",
				    wf->ireq.i_name);
			break;
		}
	} else
		IEEE80211_ADDR_COPY(wf->mac, mac);
}

/* XXX only fetch what's needed to do reports */
static void
wlan_collect(struct wlanstatfoo_p *wf,
	struct ieee80211_stats *stats, struct ieee80211req_sta_stats *nstats)
{

	IEEE80211_ADDR_COPY(wf->u_info.info.is_u.macaddr, wf->mac);
	wf->ireq.i_type = IEEE80211_IOC_STA_INFO;
	wf->ireq.i_data = (caddr_t) &wf->u_info;
	wf->ireq.i_len = sizeof(wf->u_info);
	if (ioctl(wf->s, SIOCG80211, &wf->ireq) < 0) {
		warn("%s:%s (IEEE80211_IOC_STA_INFO)", wf->ireq.i_name,
		    ether_ntoa((const struct ether_addr*) wf->mac));
	}

	IEEE80211_ADDR_COPY(nstats->is_u.macaddr, wf->mac);
	wf->ireq.i_type = IEEE80211_IOC_STA_STATS;
	wf->ireq.i_data = (caddr_t) nstats;
	wf->ireq.i_len = sizeof(*nstats);
	if (ioctl(wf->s, SIOCG80211, &wf->ireq) < 0)
		warn("%s:%s (IEEE80211_IOC_STA_STATS)", wf->ireq.i_name,
		    ether_ntoa((const struct ether_addr*) wf->mac));

	wf->ifr.ifr_data = (caddr_t) stats;
	if (ioctl(wf->s, SIOCG80211STATS, &wf->ifr) < 0)
		err(1, "%s (SIOCG80211STATS)", wf->ifr.ifr_name);
}

static void
wlan_collect_cur(struct bsdstat *sf)
{
	struct wlanstatfoo_p *wf = (struct wlanstatfoo_p *) sf;

	wlan_collect(wf, &wf->cur, &wf->ncur);
}

static void
wlan_collect_tot(struct bsdstat *sf)
{
	struct wlanstatfoo_p *wf = (struct wlanstatfoo_p *) sf;

	wlan_collect(wf, &wf->total, &wf->ntotal);
}

static void
wlan_update_tot(struct bsdstat *sf)
{
	struct wlanstatfoo_p *wf = (struct wlanstatfoo_p *) sf;

	wf->total = wf->cur;
	wf->ntotal = wf->ncur;
}

void
setreason(char b[], size_t bs, int v)
{
    static const char *reasons[] = {
	[IEEE80211_REASON_UNSPECIFIED]		= "unspecified",
	[IEEE80211_REASON_AUTH_EXPIRE]		= "auth expire",
	[IEEE80211_REASON_AUTH_LEAVE]		= "auth leave",
	[IEEE80211_REASON_ASSOC_EXPIRE]		= "assoc expire",
	[IEEE80211_REASON_ASSOC_TOOMANY]	= "assoc toomany",
	[IEEE80211_REASON_NOT_AUTHED]		= "not authed",
	[IEEE80211_REASON_NOT_ASSOCED]		= "not assoced",
	[IEEE80211_REASON_ASSOC_LEAVE]		= "assoc leave",
	[IEEE80211_REASON_ASSOC_NOT_AUTHED]	= "assoc not authed",
	[IEEE80211_REASON_DISASSOC_PWRCAP_BAD]	= "disassoc pwrcap bad",
	[IEEE80211_REASON_DISASSOC_SUPCHAN_BAD]	= "disassoc supchan bad",
	[IEEE80211_REASON_IE_INVALID]		= "ie invalid",
	[IEEE80211_REASON_MIC_FAILURE]		= "mic failure",
	[IEEE80211_REASON_4WAY_HANDSHAKE_TIMEOUT]= "4-way handshake timeout",
	[IEEE80211_REASON_GROUP_KEY_UPDATE_TIMEOUT] = "group key update timeout",
	[IEEE80211_REASON_IE_IN_4WAY_DIFFERS]	= "ie in 4-way differs",
	[IEEE80211_REASON_GROUP_CIPHER_INVALID]	= "group cipher invalid",
	[IEEE80211_REASON_PAIRWISE_CIPHER_INVALID]= "pairwise cipher invalid",
	[IEEE80211_REASON_AKMP_INVALID]		= "akmp invalid",
	[IEEE80211_REASON_UNSUPP_RSN_IE_VERSION]= "unsupported rsn ie version",
	[IEEE80211_REASON_INVALID_RSN_IE_CAP]	= "invalid rsn ie cap",
	[IEEE80211_REASON_802_1X_AUTH_FAILED]	= "802.1x auth failed",
	[IEEE80211_REASON_CIPHER_SUITE_REJECTED]= "cipher suite rejected",
    };
    if (v < nitems(reasons) && reasons[v] != NULL)
	    snprintf(b, bs, "%s (%u)", reasons[v], v);
    else
	    snprintf(b, bs, "%u", v);
}

void
setstatus(char b[], size_t bs, int v)
{
    static const char *status[] = {
	[IEEE80211_STATUS_SUCCESS]		= "success",
	[IEEE80211_STATUS_UNSPECIFIED]		= "unspecified",
	[IEEE80211_STATUS_CAPINFO]		= "capinfo",
	[IEEE80211_STATUS_NOT_ASSOCED]		= "not assoced",
	[IEEE80211_STATUS_OTHER]		= "other",
	[IEEE80211_STATUS_ALG]			= "algorithm",
	[IEEE80211_STATUS_SEQUENCE]		= "sequence",
	[IEEE80211_STATUS_CHALLENGE]		= "challenge",
	[IEEE80211_STATUS_TIMEOUT]		= "timeout",
	[IEEE80211_STATUS_TOOMANY]		= "toomany",
	[IEEE80211_STATUS_BASIC_RATE]		= "basic rate",
	[IEEE80211_STATUS_SP_REQUIRED]		= "sp required",
	[IEEE80211_STATUS_PBCC_REQUIRED]	= "pbcc required",
	[IEEE80211_STATUS_CA_REQUIRED]		= "ca required",
	[IEEE80211_STATUS_SPECMGMT_REQUIRED]	= "specmgmt required",
	[IEEE80211_STATUS_PWRCAP_REQUIRED]	= "pwrcap required",
	[IEEE80211_STATUS_SUPCHAN_REQUIRED]	= "supchan required",
	[IEEE80211_STATUS_SHORTSLOT_REQUIRED]	= "shortslot required",
	[IEEE80211_STATUS_DSSSOFDM_REQUIRED]	= "dsssofdm required",
	[IEEE80211_STATUS_INVALID_IE]		= "invalid ie",
	[IEEE80211_STATUS_GROUP_CIPHER_INVALID]	= "group cipher invalid",
	[IEEE80211_STATUS_PAIRWISE_CIPHER_INVALID]= "pairwise cipher invalid",
	[IEEE80211_STATUS_AKMP_INVALID]		= "akmp invalid",
	[IEEE80211_STATUS_UNSUPP_RSN_IE_VERSION]= "unsupported rsn ie version",
	[IEEE80211_STATUS_INVALID_RSN_IE_CAP]	= "invalid rsn ie cap",
	[IEEE80211_STATUS_CIPHER_SUITE_REJECTED]= "cipher suite rejected",
    };
    if (v < nitems(status) && status[v] != NULL)
	    snprintf(b, bs, "%s (%u)", status[v], v);
    else
	    snprintf(b, bs, "%u", v);
}

static int
wlan_getinfo(struct wlanstatfoo_p *wf, int s, char b[], size_t bs)
{
	const struct ieee80211req_sta_info *si = &wf->u_info.info.info[0];

	switch (s) {
	case S_RATE:
		snprintf(b, bs, "%.1fM", (float) si->isi_txmbps/2.0);
		return 1;
	case S_RSSI:
		snprintf(b, bs, "%.1f", (float) si->isi_rssi/2.0);
		return 1;
	case S_NOISE:
		snprintf(b, bs, "%d", si->isi_noise);
		return 1;
	case S_SIGNAL:
		snprintf(b, bs, "%.1f", (float) si->isi_rssi/2.0
		    + (float) si->isi_noise);
		return 1;
	case S_RX_AUTH_FAIL_CODE:
		if (wf->cur.is_rx_authfail_code == 0)
			break;
		setstatus(b, bs, wf->cur.is_rx_authfail_code);
		return 1;
	case S_RX_DEAUTH_CODE:
		if (wf->cur.is_rx_deauth_code == 0)
			break;
		setreason(b, bs, wf->cur.is_rx_deauth_code);
		return 1;
	case S_RX_DISASSOC_CODE:
		if (wf->cur.is_rx_disassoc_code == 0)
			break;
		setreason(b, bs, wf->cur.is_rx_disassoc_code);
		return 1;
	}
	b[0] = '\0';
	return 0;
}

static int
wlan_get_curstat(struct bsdstat *sf, int s, char b[], size_t bs)
{
	struct wlanstatfoo_p *wf = (struct wlanstatfoo_p *) sf;
#define	STAT(x) \
	snprintf(b, bs, "%u", wf->cur.is_##x - wf->total.is_##x); return 1
#define	NSTAT(x) \
	snprintf(b, bs, "%u", \
	    wf->ncur.is_stats.ns_##x - wf->ntotal.is_stats.ns_##x); \
	    return 1

	switch (s) {
	case S_RX_BADVERSION:	STAT(rx_badversion);
	case S_RX_TOOSHORT:	STAT(rx_tooshort);
	case S_RX_WRONGBSS:	STAT(rx_wrongbss);
	case S_RX_DUP:		STAT(rx_dup);
	case S_RX_WRONGDIR:	STAT(rx_wrongdir);
	case S_RX_MCASTECHO:	STAT(rx_mcastecho);
	case S_RX_NOTASSOC:	STAT(rx_notassoc);
	case S_RX_NOPRIVACY:	STAT(rx_noprivacy);
	case S_RX_UNENCRYPTED:	STAT(rx_unencrypted);
	case S_RX_WEPFAIL:	STAT(rx_wepfail);
	case S_RX_DECAP:	STAT(rx_decap);
	case S_RX_MGTDISCARD:	STAT(rx_mgtdiscard);
	case S_RX_CTL:		STAT(rx_ctl);
	case S_RX_BEACON:	STAT(rx_beacon);
	case S_RX_RSTOOBIG:	STAT(rx_rstoobig);
	case S_RX_ELEM_MISSING:	STAT(rx_elem_missing);
	case S_RX_ELEM_TOOBIG:	STAT(rx_elem_toobig);
	case S_RX_ELEM_TOOSMALL:	STAT(rx_elem_toosmall);
	case S_RX_ELEM_UNKNOWN:	STAT(rx_elem_unknown);
	case S_RX_BADCHAN:	STAT(rx_badchan);
	case S_RX_CHANMISMATCH:	STAT(rx_chanmismatch);
	case S_RX_NODEALLOC:	STAT(rx_nodealloc);
	case S_RX_SSIDMISMATCH:	STAT(rx_ssidmismatch);
	case S_RX_AUTH_UNSUPPORTED:	STAT(rx_auth_unsupported);
	case S_RX_AUTH_FAIL:	STAT(rx_auth_fail);
	case S_RX_AUTH_COUNTERMEASURES:	STAT(rx_auth_countermeasures);
	case S_RX_ASSOC_BSS:	STAT(rx_assoc_bss);
	case S_RX_ASSOC_NOTAUTH:	STAT(rx_assoc_notauth);
	case S_RX_ASSOC_CAPMISMATCH:	STAT(rx_assoc_capmismatch);
	case S_RX_ASSOC_NORATE:	STAT(rx_assoc_norate);
	case S_RX_ASSOC_BADWPAIE:	STAT(rx_assoc_badwpaie);
	case S_RX_DEAUTH:	STAT(rx_deauth);
	case S_RX_DISASSOC:	STAT(rx_disassoc);
	case S_BMISS:		STAT(beacon_miss);
	case S_RX_BADSUBTYPE:	STAT(rx_badsubtype);
	case S_RX_NOBUF:	STAT(rx_nobuf);
	case S_RX_DECRYPTCRC:	STAT(rx_decryptcrc);
	case S_RX_AHDEMO_MGT:	STAT(rx_ahdemo_mgt);
	case S_RX_BAD_AUTH:	STAT(rx_bad_auth);
	case S_RX_UNAUTH:	STAT(rx_unauth);
	case S_RX_BADKEYID:	STAT(rx_badkeyid);
	case S_RX_CCMPREPLAY:	STAT(rx_ccmpreplay);
	case S_RX_CCMPFORMAT:	STAT(rx_ccmpformat);
	case S_RX_CCMPMIC:	STAT(rx_ccmpmic);
	case S_RX_TKIPREPLAY:	STAT(rx_tkipreplay);
	case S_RX_TKIPFORMAT:	STAT(rx_tkipformat);
	case S_RX_TKIPMIC:	STAT(rx_tkipmic);
	case S_RX_TKIPICV:	STAT(rx_tkipicv);
	case S_RX_BADCIPHER:	STAT(rx_badcipher);
	case S_RX_NOCIPHERCTX:	STAT(rx_nocipherctx);
	case S_RX_ACL:		STAT(rx_acl);
	case S_TX_NOBUF:	STAT(tx_nobuf);
	case S_TX_NONODE:	STAT(tx_nonode);
	case S_TX_UNKNOWNMGT:	STAT(tx_unknownmgt);
	case S_TX_BADCIPHER:	STAT(tx_badcipher);
	case S_TX_NODEFKEY:	STAT(tx_nodefkey);
	case S_TX_NOHEADROOM:	STAT(tx_noheadroom);
	case S_TX_FRAGFRAMES:	STAT(tx_fragframes);
	case S_TX_FRAGS:	STAT(tx_frags);
	case S_SCAN_ACTIVE:	STAT(scan_active);
	case S_SCAN_PASSIVE:	STAT(scan_passive);
	case S_SCAN_BG:		STAT(scan_bg);
	case S_NODE_TIMEOUT:	STAT(node_timeout);
	case S_CRYPTO_NOMEM:	STAT(crypto_nomem);
	case S_CRYPTO_TKIP:	STAT(crypto_tkip);
	case S_CRYPTO_TKIPENMIC:	STAT(crypto_tkipenmic);
	case S_CRYPTO_TKIPDEMIC:	STAT(crypto_tkipdemic);
	case S_CRYPTO_TKIPCM:	STAT(crypto_tkipcm);
	case S_CRYPTO_CCMP:	STAT(crypto_ccmp);
	case S_CRYPTO_WEP:	STAT(crypto_wep);
	case S_CRYPTO_SETKEY_CIPHER:	STAT(crypto_setkey_cipher);
	case S_CRYPTO_SETKEY_NOKEY:	STAT(crypto_setkey_nokey);
	case S_CRYPTO_DELKEY:	STAT(crypto_delkey);
	case S_CRYPTO_BADCIPHER:	STAT(crypto_badcipher);
	case S_CRYPTO_NOCIPHER:	STAT(crypto_nocipher);
	case S_CRYPTO_ATTACHFAIL:	STAT(crypto_attachfail);
	case S_CRYPTO_SWFALLBACK:	STAT(crypto_swfallback);
	case S_CRYPTO_KEYFAIL:	STAT(crypto_keyfail);
	case S_CRYPTO_ENMICFAIL:	STAT(crypto_enmicfail);
	case S_IBSS_CAPMISMATCH:	STAT(ibss_capmismatch);
	case S_IBSS_NORATE:	STAT(ibss_norate);
	case S_PS_UNASSOC:	STAT(ps_unassoc);
	case S_PS_BADAID:	STAT(ps_badaid);
	case S_PS_QEMPTY:	STAT(ps_qempty);
	case S_FF_BADHDR:	STAT(ff_badhdr);
	case S_FF_TOOSHORT:	STAT(ff_tooshort);
	case S_FF_SPLIT:	STAT(ff_split);
	case S_FF_DECAP:	STAT(ff_decap);
	case S_FF_ENCAP:	STAT(ff_encap);
	case S_FF_ENCAPFAIL:	STAT(ff_encapfail);
	case S_RX_BADBINTVAL:	STAT(rx_badbintval);
	case S_RX_MGMT:		STAT(rx_mgmt);
	case S_RX_DEMICFAIL:	STAT(rx_demicfail);
	case S_RX_DEFRAG:	STAT(rx_defrag);
	case S_RX_ACTION:	STAT(rx_action);
	case S_AMSDU_TOOSHORT:	STAT(amsdu_tooshort);
	case S_AMSDU_SPLIT:	STAT(amsdu_split);
	case S_AMSDU_DECAP:	STAT(amsdu_decap);
	case S_AMSDU_ENCAP:	STAT(amsdu_encap);
	case S_AMPDU_REORDER:	STAT(ampdu_rx_reorder);
	case S_AMPDU_FLUSH:	STAT(ampdu_rx_flush);
	case S_AMPDU_BARBAD:	STAT(ampdu_bar_bad);
	case S_AMPDU_BAROOW:	STAT(ampdu_bar_oow);
	case S_AMPDU_BARMOVE:	STAT(ampdu_bar_move);
	case S_AMPDU_BAR:	STAT(ampdu_bar_rx);
	case S_AMPDU_MOVE:	STAT(ampdu_rx_move);
	case S_AMPDU_OOR:	STAT(ampdu_rx_oor);
	case S_AMPDU_COPY:	STAT(ampdu_rx_copy);
	case S_AMPDU_DROP:	STAT(ampdu_rx_drop);
	case S_AMPDU_AGE:	STAT(ampdu_rx_age);
	case S_AMPDU_STOP:	STAT(ampdu_stop);
	case S_AMPDU_STOP_FAILED:STAT(ampdu_stop_failed);
	case S_ADDBA_REJECT:	STAT(addba_reject);
	case S_ADDBA_NOREQUEST:	STAT(addba_norequest);
	case S_ADDBA_BADTOKEN:	STAT(addba_badtoken);
	case S_TX_BADSTATE:	STAT(tx_badstate);
	case S_TX_NOTASSOC:	STAT(tx_notassoc);
	case S_TX_CLASSIFY:	STAT(tx_classify);
	case S_DWDS_MCAST:	STAT(dwds_mcast);
	case S_DWDS_QDROP:	STAT(dwds_qdrop);
	case S_HT_ASSOC_NOHTCAP:STAT(ht_assoc_nohtcap);
	case S_HT_ASSOC_DOWNGRADE:STAT(ht_assoc_downgrade);
	case S_HT_ASSOC_NORATE:	STAT(ht_assoc_norate);
	case S_MESH_WRONGMESH:	STAT(mesh_wrongmesh);
	case S_MESH_NOLINK:	STAT(mesh_nolink);
	case S_MESH_FWD_TTL:	STAT(mesh_fwd_ttl);
	case S_MESH_FWD_NOBUF:	STAT(mesh_fwd_nobuf);
	case S_MESH_FWD_TOOSHORT: STAT(mesh_fwd_tooshort);
	case S_MESH_FWD_DISABLED: STAT(mesh_fwd_disabled);
	case S_MESH_FWD_NOPATH:	STAT(mesh_fwd_nopath);
	case S_HWMP_WRONGSEQ:	STAT(hwmp_wrongseq);
	case S_HWMP_ROOTREQS:	STAT(hwmp_rootreqs);
	case S_HWMP_ROOTANN:	STAT(hwmp_rootrann);
	case S_MESH_BADAE:	STAT(mesh_badae);
	case S_MESH_RTADDFAILED:STAT(mesh_rtaddfailed);
	case S_MESH_NOTPROXY:	STAT(mesh_notproxy);
	case S_RX_BADALIGN:	STAT(rx_badalign);
	case S_INPUT:		NSTAT(rx_data);
	case S_OUTPUT:		NSTAT(tx_data);
	case S_RX_UCAST:	NSTAT(rx_ucast);
	case S_RX_MCAST:	NSTAT(rx_mcast);
	case S_TX_UCAST:	NSTAT(tx_ucast);
	case S_TX_MCAST:	NSTAT(tx_mcast);
	case S_BEACON_BAD:	STAT(beacon_bad);
	case S_AMPDU_BARTX:	STAT(ampdu_bar_tx);
	case S_AMPDU_BARTX_RETRY:	STAT(ampdu_bar_tx_retry);
	case S_AMPDU_BARTX_FAIL:	STAT(ampdu_bar_tx_fail);
	}
	return wlan_getinfo(wf, s, b, bs);
#undef NSTAT
#undef STAT
}

static int
wlan_get_totstat(struct bsdstat *sf, int s, char b[], size_t bs)
{
	struct wlanstatfoo_p *wf = (struct wlanstatfoo_p *) sf;
#define	STAT(x) \
	snprintf(b, bs, "%u", wf->total.is_##x); return 1
#define	NSTAT(x) \
	snprintf(b, bs, "%u", wf->ntotal.is_stats.ns_##x); return 1

	switch (s) {
	case S_RX_BADVERSION:	STAT(rx_badversion);
	case S_RX_TOOSHORT:	STAT(rx_tooshort);
	case S_RX_WRONGBSS:	STAT(rx_wrongbss);
	case S_RX_DUP:	STAT(rx_dup);
	case S_RX_WRONGDIR:	STAT(rx_wrongdir);
	case S_RX_MCASTECHO:	STAT(rx_mcastecho);
	case S_RX_NOTASSOC:	STAT(rx_notassoc);
	case S_RX_NOPRIVACY:	STAT(rx_noprivacy);
	case S_RX_UNENCRYPTED:	STAT(rx_unencrypted);
	case S_RX_WEPFAIL:	STAT(rx_wepfail);
	case S_RX_DECAP:	STAT(rx_decap);
	case S_RX_MGTDISCARD:	STAT(rx_mgtdiscard);
	case S_RX_CTL:		STAT(rx_ctl);
	case S_RX_BEACON:	STAT(rx_beacon);
	case S_RX_RSTOOBIG:	STAT(rx_rstoobig);
	case S_RX_ELEM_MISSING:	STAT(rx_elem_missing);
	case S_RX_ELEM_TOOBIG:	STAT(rx_elem_toobig);
	case S_RX_ELEM_TOOSMALL:	STAT(rx_elem_toosmall);
	case S_RX_ELEM_UNKNOWN:	STAT(rx_elem_unknown);
	case S_RX_BADCHAN:	STAT(rx_badchan);
	case S_RX_CHANMISMATCH:	STAT(rx_chanmismatch);
	case S_RX_NODEALLOC:	STAT(rx_nodealloc);
	case S_RX_SSIDMISMATCH:	STAT(rx_ssidmismatch);
	case S_RX_AUTH_UNSUPPORTED:	STAT(rx_auth_unsupported);
	case S_RX_AUTH_FAIL:	STAT(rx_auth_fail);
	case S_RX_AUTH_COUNTERMEASURES:	STAT(rx_auth_countermeasures);
	case S_RX_ASSOC_BSS:	STAT(rx_assoc_bss);
	case S_RX_ASSOC_NOTAUTH:	STAT(rx_assoc_notauth);
	case S_RX_ASSOC_CAPMISMATCH:	STAT(rx_assoc_capmismatch);
	case S_RX_ASSOC_NORATE:	STAT(rx_assoc_norate);
	case S_RX_ASSOC_BADWPAIE:	STAT(rx_assoc_badwpaie);
	case S_RX_DEAUTH:	STAT(rx_deauth);
	case S_RX_DISASSOC:	STAT(rx_disassoc);
	case S_BMISS:		STAT(beacon_miss);
	case S_RX_BADSUBTYPE:	STAT(rx_badsubtype);
	case S_RX_NOBUF:	STAT(rx_nobuf);
	case S_RX_DECRYPTCRC:	STAT(rx_decryptcrc);
	case S_RX_AHDEMO_MGT:	STAT(rx_ahdemo_mgt);
	case S_RX_BAD_AUTH:	STAT(rx_bad_auth);
	case S_RX_UNAUTH:	STAT(rx_unauth);
	case S_RX_BADKEYID:	STAT(rx_badkeyid);
	case S_RX_CCMPREPLAY:	STAT(rx_ccmpreplay);
	case S_RX_CCMPFORMAT:	STAT(rx_ccmpformat);
	case S_RX_CCMPMIC:	STAT(rx_ccmpmic);
	case S_RX_TKIPREPLAY:	STAT(rx_tkipreplay);
	case S_RX_TKIPFORMAT:	STAT(rx_tkipformat);
	case S_RX_TKIPMIC:	STAT(rx_tkipmic);
	case S_RX_TKIPICV:	STAT(rx_tkipicv);
	case S_RX_BADCIPHER:	STAT(rx_badcipher);
	case S_RX_NOCIPHERCTX:	STAT(rx_nocipherctx);
	case S_RX_ACL:		STAT(rx_acl);
	case S_TX_NOBUF:	STAT(tx_nobuf);
	case S_TX_NONODE:	STAT(tx_nonode);
	case S_TX_UNKNOWNMGT:	STAT(tx_unknownmgt);
	case S_TX_BADCIPHER:	STAT(tx_badcipher);
	case S_TX_NODEFKEY:	STAT(tx_nodefkey);
	case S_TX_NOHEADROOM:	STAT(tx_noheadroom);
	case S_TX_FRAGFRAMES:	STAT(tx_fragframes);
	case S_TX_FRAGS:	STAT(tx_frags);
	case S_SCAN_ACTIVE:	STAT(scan_active);
	case S_SCAN_PASSIVE:	STAT(scan_passive);
	case S_SCAN_BG:		STAT(scan_bg);
	case S_NODE_TIMEOUT:	STAT(node_timeout);
	case S_CRYPTO_NOMEM:	STAT(crypto_nomem);
	case S_CRYPTO_TKIP:	STAT(crypto_tkip);
	case S_CRYPTO_TKIPENMIC:	STAT(crypto_tkipenmic);
	case S_CRYPTO_TKIPDEMIC:	STAT(crypto_tkipdemic);
	case S_CRYPTO_TKIPCM:	STAT(crypto_tkipcm);
	case S_CRYPTO_CCMP:	STAT(crypto_ccmp);
	case S_CRYPTO_WEP:	STAT(crypto_wep);
	case S_CRYPTO_SETKEY_CIPHER:	STAT(crypto_setkey_cipher);
	case S_CRYPTO_SETKEY_NOKEY:	STAT(crypto_setkey_nokey);
	case S_CRYPTO_DELKEY:	STAT(crypto_delkey);
	case S_CRYPTO_BADCIPHER:	STAT(crypto_badcipher);
	case S_CRYPTO_NOCIPHER:	STAT(crypto_nocipher);
	case S_CRYPTO_ATTACHFAIL:	STAT(crypto_attachfail);
	case S_CRYPTO_SWFALLBACK:	STAT(crypto_swfallback);
	case S_CRYPTO_KEYFAIL:	STAT(crypto_keyfail);
	case S_CRYPTO_ENMICFAIL:	STAT(crypto_enmicfail);
	case S_IBSS_CAPMISMATCH:	STAT(ibss_capmismatch);
	case S_IBSS_NORATE:	STAT(ibss_norate);
	case S_PS_UNASSOC:	STAT(ps_unassoc);
	case S_PS_BADAID:	STAT(ps_badaid);
	case S_PS_QEMPTY:	STAT(ps_qempty);
	case S_FF_BADHDR:	STAT(ff_badhdr);
	case S_FF_TOOSHORT:	STAT(ff_tooshort);
	case S_FF_SPLIT:	STAT(ff_split);
	case S_FF_DECAP:	STAT(ff_decap);
	case S_FF_ENCAP:	STAT(ff_encap);
	case S_FF_ENCAPFAIL:	STAT(ff_encapfail);
	case S_RX_BADBINTVAL:	STAT(rx_badbintval);
	case S_RX_MGMT:		STAT(rx_mgmt);
	case S_RX_DEMICFAIL:	STAT(rx_demicfail);
	case S_RX_DEFRAG:	STAT(rx_defrag);
	case S_RX_ACTION:	STAT(rx_action);
	case S_AMSDU_TOOSHORT:	STAT(amsdu_tooshort);
	case S_AMSDU_SPLIT:	STAT(amsdu_split);
	case S_AMSDU_DECAP:	STAT(amsdu_decap);
	case S_AMSDU_ENCAP:	STAT(amsdu_encap);
	case S_AMPDU_REORDER:	STAT(ampdu_rx_reorder);
	case S_AMPDU_FLUSH:	STAT(ampdu_rx_flush);
	case S_AMPDU_BARBAD:	STAT(ampdu_bar_bad);
	case S_AMPDU_BAROOW:	STAT(ampdu_bar_oow);
	case S_AMPDU_BARMOVE:	STAT(ampdu_bar_move);
	case S_AMPDU_BAR:	STAT(ampdu_bar_rx);
	case S_AMPDU_MOVE:	STAT(ampdu_rx_move);
	case S_AMPDU_OOR:	STAT(ampdu_rx_oor);
	case S_AMPDU_COPY:	STAT(ampdu_rx_copy);
	case S_AMPDU_DROP:	STAT(ampdu_rx_drop);
	case S_AMPDU_AGE:	STAT(ampdu_rx_age);
	case S_AMPDU_STOP:	STAT(ampdu_stop);
	case S_AMPDU_STOP_FAILED:STAT(ampdu_stop_failed);
	case S_ADDBA_REJECT:	STAT(addba_reject);
	case S_ADDBA_NOREQUEST:	STAT(addba_norequest);
	case S_ADDBA_BADTOKEN:	STAT(addba_badtoken);
	case S_TX_BADSTATE:	STAT(tx_badstate);
	case S_TX_NOTASSOC:	STAT(tx_notassoc);
	case S_TX_CLASSIFY:	STAT(tx_classify);
	case S_DWDS_MCAST:	STAT(dwds_mcast);
	case S_DWDS_QDROP:	STAT(dwds_qdrop);
	case S_HT_ASSOC_NOHTCAP:STAT(ht_assoc_nohtcap);
	case S_HT_ASSOC_DOWNGRADE:STAT(ht_assoc_downgrade);
	case S_HT_ASSOC_NORATE:	STAT(ht_assoc_norate);
	case S_MESH_WRONGMESH:	STAT(mesh_wrongmesh);
	case S_MESH_NOLINK:	STAT(mesh_nolink);
	case S_MESH_FWD_TTL:	STAT(mesh_fwd_ttl);
	case S_MESH_FWD_NOBUF:	STAT(mesh_fwd_nobuf);
	case S_MESH_FWD_TOOSHORT: STAT(mesh_fwd_tooshort);
	case S_MESH_FWD_DISABLED: STAT(mesh_fwd_disabled);
	case S_MESH_FWD_NOPATH:	STAT(mesh_fwd_nopath);
	case S_HWMP_WRONGSEQ:	STAT(hwmp_wrongseq);
	case S_HWMP_ROOTREQS:	STAT(hwmp_rootreqs);
	case S_HWMP_ROOTANN:	STAT(hwmp_rootrann);
	case S_MESH_BADAE:	STAT(mesh_badae);
	case S_MESH_RTADDFAILED:STAT(mesh_rtaddfailed);
	case S_MESH_NOTPROXY:	STAT(mesh_notproxy);
	case S_RX_BADALIGN:	STAT(rx_badalign);
	case S_INPUT:		NSTAT(rx_data);
	case S_OUTPUT:		NSTAT(tx_data);
	case S_RX_UCAST:	NSTAT(rx_ucast);
	case S_RX_MCAST:	NSTAT(rx_mcast);
	case S_TX_UCAST:	NSTAT(tx_ucast);
	case S_TX_MCAST:	NSTAT(tx_mcast);
	case S_BEACON_BAD:	STAT(beacon_bad);
	case S_AMPDU_BARTX:	STAT(ampdu_bar_tx);
	case S_AMPDU_BARTX_RETRY:	STAT(ampdu_bar_tx_retry);
	case S_AMPDU_BARTX_FAIL:	STAT(ampdu_bar_tx_fail);
	}
	return wlan_getinfo(wf, s, b, bs);
#undef NSTAT
#undef STAT
}

BSDSTAT_DEFINE_BOUNCE(wlanstatfoo)

struct wlanstatfoo *
wlanstats_new(const char *ifname, const char *fmtstring)
{
	struct wlanstatfoo_p *wf;

	wf = calloc(1, sizeof(struct wlanstatfoo_p));
	if (wf != NULL) {
		bsdstat_init(&wf->base.base, "wlanstats", wlanstats,
		    nitems(wlanstats));
		/* override base methods */
		wf->base.base.collect_cur = wlan_collect_cur;
		wf->base.base.collect_tot = wlan_collect_tot;
		wf->base.base.get_curstat = wlan_get_curstat;
		wf->base.base.get_totstat = wlan_get_totstat;
		wf->base.base.update_tot = wlan_update_tot;

		/* setup bounce functions for public methods */
		BSDSTAT_BOUNCE(wf, wlanstatfoo);

		/* setup our public methods */
		wf->base.setifname = wlan_setifname;
		wf->base.getifname = wlan_getifname;
		wf->base.getopmode = wlan_getopmode;
		wf->base.setstamac = wlan_setstamac;
		wf->opmode = -1;

		wf->s = socket(AF_INET, SOCK_DGRAM, 0);
		if (wf->s < 0)
			err(1, "socket");

		wlan_setifname(&wf->base, ifname);
		wf->base.setfmt(&wf->base, fmtstring);
	}
	return &wf->base;
}
