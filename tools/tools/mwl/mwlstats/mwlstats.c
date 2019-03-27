/*-
 * Copyright (c) 2007 Sam Leffler, Errno Consulting
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
 * mwl statistics class.
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../../../sys/net80211/ieee80211_ioctl.h"
#include "../../../../sys/net80211/ieee80211_radiotap.h"

/*
 * Get Hardware Statistics.
 */
struct mwl_hal_hwstats {
	uint32_t	TxRetrySuccesses;
	uint32_t	TxMultipleRetrySuccesses;
	uint32_t	TxFailures;
	uint32_t	RTSSuccesses;
	uint32_t	RTSFailures;
	uint32_t	AckFailures;
	uint32_t	RxDuplicateFrames;
	uint32_t	FCSErrorCount;
	uint32_t	TxWatchDogTimeouts;
	uint32_t	RxOverflows;
	uint32_t	RxFragErrors;
	uint32_t	RxMemErrors;
	uint32_t	PointerErrors;
	uint32_t	TxUnderflows;
	uint32_t	TxDone;
	uint32_t	TxDoneBufTryPut;
	uint32_t	TxDoneBufPut;
	uint32_t	Wait4TxBuf;
	uint32_t	TxAttempts;
	uint32_t	TxSuccesses;
	uint32_t	TxFragments;
	uint32_t	TxMulticasts;
	uint32_t	RxNonCtlPkts;
	uint32_t	RxMulticasts;
	uint32_t	RxUndecryptableFrames;
	uint32_t 	RxICVErrors;
	uint32_t	RxExcludedFrames;
};
#include "../../../../sys/dev/mwl/if_mwlioctl.h"

#include "mwlstats.h"

#define	AFTER(prev)	((prev)+1)

static const struct fmt mwlstats[] = {
#define	S_INPUT		0
	{ 8,	"input",	"input",	"total frames received" },
#define	S_RX_MCAST	AFTER(S_INPUT)
	{ 7,	"rxmcast",	"rxmcast",	"rx multicast frames" },
#define	S_RX_NONCTL	AFTER(S_RX_MCAST)
	{ 8,	"rxnonctl",	"rxnonctl"	"rx non control frames" },
#define	S_RX_MGT	AFTER(S_RX_NONCTL)
	{ 5,	"rxmgt",	"rxmgt",	"rx management frames" },
#define	S_RX_CTL	AFTER(S_RX_MGT)
	{ 5,	"rxctl",	"rxctl",	"rx control frames" },
#define	S_OUTPUT	AFTER(S_RX_CTL)
	{ 8,	"output",	"output",	"total frames transmit" },
#define	S_TX_MCAST	AFTER(S_OUTPUT)
	{ 7,	"txmcast",	"txmcast",	"tx multicast frames" },
#define	S_TX_MGMT	AFTER(S_TX_MCAST)
	{ 5,	"txmgt",	"txmgt",	"tx management frames" },
#define	S_TX_RETRY	AFTER(S_TX_MGMT)
	{ 7,	"txretry",	"txretry",	"tx success with 1 retry" },
#define	S_TX_MRETRY	AFTER(S_TX_RETRY)
	{ 8,	"txmretry",	"txmretry",	"tx success with >1 retry" },
#define	S_TX_RTSGOOD	AFTER(S_TX_MRETRY)
	{ 7,	"rtsgood",	"rtsgood",	"RTS tx success" },
#define	S_TX_RTSBAD	AFTER(S_TX_RTSGOOD)
	{ 6,	"rtsbad",	"rtsbad",	"RTS tx failed" },
#define	S_TX_NOACK	AFTER(S_TX_RTSBAD)
	{ 5,	"noack",	"noack",	"tx failed because no ACK was received" },
#define	S_RX_DUPLICATE	AFTER(S_TX_NOACK)
	{ 5,	"rxdup",	"rxdup",	"rx discarded by f/w as dup" },
#define	S_RX_FCS	AFTER(S_RX_DUPLICATE)
	{ 5,	"rxfcs",	"rxfcs",	"rx discarded by f/w for bad FCS" },
#define	S_TX_WATCHDOG	AFTER(S_RX_FCS)
	{ 7,	"txwatch",	"txwatch",	"MAC tx hang (f/w recovery)" },
#define	S_RX_OVERFLOW	AFTER(S_TX_WATCHDOG)
	{ 6,	"rxover",	"rxover",	"no f/w buffer for rx" },
#define	S_RX_FRAGERROR	AFTER(S_RX_OVERFLOW)
	{ 6,	"rxfrag",	"rxfrag",	"rx failed in f/w due to defrag" },
#define	S_RX_MEMERROR	AFTER(S_RX_FRAGERROR)
	{ 5,	"rxmem",	"rxmem",	"rx failed in f/w 'cuz out of of memory" },
#define	S_PTRERROR	AFTER(S_RX_MEMERROR)
	{ 6,	"badptr",	"badptr",	"MAC internal pointer problem" },
#define	S_TX_UNDERFLOW	AFTER(S_PTRERROR)
	{ 7,	"txunder",	"txunder",	"tx failed in f/w 'cuz of underflow" },
#define	S_TX_DONE	AFTER(S_TX_UNDERFLOW)
	{ 6,	"txdone",	"txdone",	"MAC tx ops completed" },
#define	S_TX_DONEBUFPUT	AFTER(S_TX_DONE)
	{ 9,	"txdoneput",	"txdoneput",	"tx buffers returned by f/w to host" },
#define	S_TX_WAIT4BUF	AFTER(S_TX_DONEBUFPUT)
	{ 6,	"txwait",	"txwait",	"no f/w buffers available when supplied a tx descriptor" },
#define	S_TX_ATTEMPTS	AFTER(S_TX_WAIT4BUF)
	{ 5,	"txtry",	"txtry",	"tx descriptors processed by f/w" },
#define	S_TX_SUCCESS	AFTER(S_TX_ATTEMPTS)
	{ 4,	"txok",		"txok",		"tx attempts successful" },
#define	S_TX_FRAGS	AFTER(S_TX_SUCCESS)
	{ 6,	"txfrag",	"txfrag",	"tx attempts with fragmentation" },
#define	S_RX_UNDECRYPT	AFTER(S_TX_FRAGS)
	{ 7,	"rxcrypt",	"rxcrypt",	"rx failed in f/w 'cuz decrypt failed" },
#define	S_RX_ICVERROR	AFTER(S_RX_UNDECRYPT)
	{ 5,	"rxicv",	"rxicv",	"rx failed in f/w 'cuz ICV check" },
#define	S_RX_EXCLUDE	AFTER(S_RX_ICVERROR)
	{ 8,	"rxfilter",	"rxfilter",	"rx frames filtered in f/w" },
#define	S_TX_LINEAR	AFTER(S_RX_EXCLUDE)
	{ 5,	"txlinear",	"txlinear",	"tx linearized to cluster" },
#define	S_TX_DISCARD	AFTER(S_TX_LINEAR)
	{ 5,	"txdisc",	"txdisc",	"tx frames discarded prior to association" },
#define	S_TX_QSTOP	AFTER(S_TX_DISCARD)
	{ 5,	"qstop",	"qstop",	"tx stopped 'cuz no xmit buffer" },
#define	S_TX_ENCAP	AFTER(S_TX_QSTOP)
	{ 5,	"txencode",	"txencode",	"tx encapsulation failed" },
#define	S_TX_NOMBUF	AFTER(S_TX_ENCAP)
	{ 5,	"txnombuf",	"txnombuf",	"tx failed 'cuz mbuf allocation failed" },
#define	S_TX_SHORTPRE	AFTER(S_TX_NOMBUF)
	{ 5,	"shpre",	"shpre",	"tx frames with short preamble" },
#define	S_TX_NOHEADROOM	AFTER(S_TX_SHORTPRE)
	{ 5,	"nohead",	"nohead",	"tx frames discarded for lack of headroom" },
#define	S_TX_BADFRAMETYPE	AFTER(S_TX_NOHEADROOM)
	{ 5,	"badtxtype",	"badtxtype",	"tx frames discarded for invalid/unknown 802.11 frame type" },
#define	S_RX_CRYPTO_ERR	AFTER(S_TX_BADFRAMETYPE)
	{ 5,	"crypt",	"crypt",	"rx failed 'cuz decryption" },
#define	S_RX_NOMBUF	AFTER(S_RX_CRYPTO_ERR)
	{ 5,	"rxnombuf",	"rxnombuf",	"rx setup failed 'cuz no mbuf" },
#define	S_RX_TKIPMIC	AFTER(S_RX_NOMBUF)
	{ 5,	"rxtkipmic",	"rxtkipmic",	"rx failed 'cuz TKIP MIC error" },
#define	S_RX_NODMABUF	AFTER(S_RX_TKIPMIC)
	{ 5,	"rxnodmabuf",	"rxnodmabuf",	"rx failed 'cuz no DMA buffer available" },
#define	S_RX_DMABUFMISSING	AFTER(S_RX_NODMABUF)
	{ 5,	"rxdmabufmissing",	"rxdmabufmissing",	"rx descriptor with no DMA buffer attached" },
#define	S_TX_NODATA	AFTER(S_RX_DMABUFMISSING)
	{ 5,	"txnodata",	"txnodata",	"tx discarded empty frame" },
#define	S_TX_BUSDMA	AFTER(S_TX_NODATA)
	{ 5,	"txbusdma",	"txbusdma",	"tx failed for dma resources" },
#define	S_RX_BUSDMA	AFTER(S_TX_BUSDMA)
	{ 5,	"rxbusdma",	"rxbusdma",	"rx setup failed for dma resources" },
#define	S_AMPDU_NOSTREAM	AFTER(S_RX_BUSDMA)
	{ 5,	"ampdu_nostream","ampdu_nostream","ADDBA request failed 'cuz all BA streams in use" },
#define	S_AMPDU_REJECT	AFTER(S_AMPDU_NOSTREAM)
	{ 5,	"ampdu_reject","ampdu_reject","ADDBA request failed 'cuz station already has one BA stream" },
#define	S_ADDBA_NOSTREAM	AFTER(S_AMPDU_REJECT)
	{ 5,	"addba_nostream","addba_nostream","ADDBA response processed but no BA stream present" },
#define	S_TX_TSO	AFTER(S_ADDBA_NOSTREAM)
	{ 8,	"txtso",	"tso",		"tx frames using TSO" },
#define	S_TSO_BADETH	AFTER(S_TX_TSO)
	{ 5,	"tsoeth",	"tsoeth",	"TSO failed 'cuz ether header type not IPv4" },
#define	S_TSO_NOHDR	AFTER(S_TSO_BADETH)
	{ 5,	"tsonohdr",	"tsonohdr",	"TSO failed 'cuz header not in first mbuf" },
#define	S_TSO_BADSPLIT	AFTER(S_TSO_NOHDR)
	{ 5,	"tsobadsplit",	"tsobadsplit",	"TSO failed 'cuz payload split failed" },
#define	S_BAWATCHDOG	AFTER(S_TSO_BADSPLIT)
	{ 5,	"bawatchdog",	"bawatchdog",	"BA watchdog interrupts" },
#define	S_BAWATCHDOG_NOTFOUND	AFTER(S_BAWATCHDOG)
	{ 5,	"bawatchdog_notfound",	"bawatchdog_notfound",
	  "BA watchdog for unknown stream" },
#define	S_BAWATCHDOG_EMPTY	AFTER(S_BAWATCHDOG_NOTFOUND)
	{ 5,	"bawatchdog_empty",	"bawatchdog_empty",
	  "BA watchdog on all streams but none found" },
#define	S_BAWATCHDOG_FAILED	AFTER(S_BAWATCHDOG_EMPTY)
	{ 5,	"bawatchdog_failed",	"bawatchdog_failed",
	  "BA watchdog processing failed to get bitmap from f/w" },
#define	S_RADARDETECT	AFTER(S_BAWATCHDOG_FAILED)
	{ 5,	"radardetect",	"radardetect",	"radar detect interrupts" },
#define	S_RATE		AFTER(S_RADARDETECT)
	{ 4,	"rate",		"rate",		"rate of last transmit" },
#define	S_TX_RSSI	AFTER(S_RATE)
	{ 4,	"arssi",	"arssi",	"rssi of last ack" },
#define	S_RX_RSSI	AFTER(S_TX_RSSI)
	{ 4,	"rssi",		"rssi",		"avg recv rssi" },
#define	S_RX_NOISE	AFTER(S_RX_RSSI)
	{ 5,	"noise",	"noise",	"rx noise floor" },
#define	S_TX_SIGNAL	AFTER(S_RX_NOISE)
	{ 4,	"asignal",	"asig",		"signal of last ack (dBm)" },
#define	S_RX_SIGNAL	AFTER(S_TX_SIGNAL)
	{ 4,	"signal",	"sig",		"avg recv signal (dBm)" },
#define	S_ANT_TX0	AFTER(S_RX_SIGNAL)
	{ 8,	"tx0",		"ant0(tx)",	"frames tx on antenna 0" },
#define	S_ANT_TX1	(S_RX_SIGNAL+2)
	{ 8,	"tx1",		"ant1(tx)",	"frames tx on antenna 1"  },
#define	S_ANT_TX2	(S_RX_SIGNAL+3)
	{ 8,	"tx2",		"ant2(tx)",	"frames tx on antenna 2"  },
#define	S_ANT_TX3	(S_RX_SIGNAL+4)
	{ 8,	"tx3",		"ant3(tx)",	"frames tx on antenna 3"  },
#define	S_ANT_RX0	AFTER(S_ANT_TX3)
	{ 8,	"rx0",		"ant0(rx)",	"frames rx on antenna 0"  },
#define	S_ANT_RX1	(S_ANT_TX3+2)
	{ 8,	"rx1",		"ant1(rx)",	"frames rx on antenna 1"   },
#define	S_ANT_RX2	(S_ANT_TX3+3)
	{ 8,	"rx2",		"ant2(rx)",	"frames rx on antenna 2"   },
#define	S_ANT_RX3	(S_ANT_TX3+4)
	{ 8,	"rx3",		"ant3(rx)",	"frames rx on antenna 3"   },
};
/* NB: this intentionally avoids per-antenna stats */
#define	S_LAST	(S_RX_SIGNAL+1)

struct mwlstatfoo_p {
	struct mwlstatfoo base;
	int s;
	struct ifreq ifr;
	struct mwl_stats cur;
	struct mwl_stats total;
};

static void
mwl_setifname(struct mwlstatfoo *wf0, const char *ifname)
{
	struct mwlstatfoo_p *wf = (struct mwlstatfoo_p *) wf0;

	strncpy(wf->ifr.ifr_name, ifname, sizeof (wf->ifr.ifr_name));
}

static void
mwl_collect(struct mwlstatfoo_p *wf, struct mwl_stats *stats)
{
	wf->ifr.ifr_data = (caddr_t) stats;
	if (ioctl(wf->s, SIOCGMVSTATS, &wf->ifr) < 0)
		err(1, "%s: ioctl: %s", __func__, wf->ifr.ifr_name);
}

static void
mwl_collect_cur(struct bsdstat *sf)
{
	struct mwlstatfoo_p *wf = (struct mwlstatfoo_p *) sf;

	mwl_collect(wf, &wf->cur);
}

static void
mwl_collect_tot(struct bsdstat *sf)
{
	struct mwlstatfoo_p *wf = (struct mwlstatfoo_p *) sf;

	mwl_collect(wf, &wf->total);
}

static void
mwl_update_tot(struct bsdstat *sf)
{
	struct mwlstatfoo_p *wf = (struct mwlstatfoo_p *) sf;

	wf->total = wf->cur;
}

static void
setrate(char b[], size_t bs, uint8_t rate)
{
	if (rate & IEEE80211_RATE_MCS)
		snprintf(b, bs, "MCS%u", rate & IEEE80211_RATE_VAL);
	else if (rate & 1)
		snprintf(b, bs, "%u.5M", rate / 2);
	else
		snprintf(b, bs, "%uM", rate / 2);
}

static int
mwl_get_curstat(struct bsdstat *sf, int s, char b[], size_t bs)
{
	struct mwlstatfoo_p *wf = (struct mwlstatfoo_p *) sf;
#define	STAT(x) \
	snprintf(b, bs, "%u", wf->cur.mst_##x - wf->total.mst_##x); return 1
#define	HWSTAT(x) \
	snprintf(b, bs, "%u", wf->cur.hw_stats.x - wf->total.hw_stats.x); return 1
#define	RXANT(x) \
	snprintf(b, bs, "%u", wf->cur.mst_ant_rx[x] - wf->total.mst_ant_rx[x]); return 1
#define	TXANT(x) \
	snprintf(b, bs, "%u", wf->cur.mst_ant_tx[x] - wf->total.mst_ant_tx[x]); return 1

	switch (s) {
	case S_INPUT:
		snprintf(b, bs, "%lu", (u_long)(
		    (wf->cur.mst_rx_packets - wf->total.mst_rx_packets)));
		return 1;
	case S_OUTPUT:
		snprintf(b, bs, "%lu", (u_long)(
		    wf->cur.mst_tx_packets - wf->total.mst_tx_packets));
		return 1;
	case S_RATE:
		setrate(b, bs, wf->cur.mst_tx_rate);
		return 1;
	case S_TX_RETRY:	HWSTAT(TxRetrySuccesses);
	case S_TX_MRETRY:	HWSTAT(TxMultipleRetrySuccesses);
	case S_TX_RTSGOOD:	HWSTAT(RTSSuccesses);
	case S_TX_RTSBAD:	HWSTAT(RTSFailures);
	case S_TX_NOACK:	HWSTAT(AckFailures);
	case S_RX_DUPLICATE:	HWSTAT(RxDuplicateFrames);
	case S_RX_FCS:		HWSTAT(FCSErrorCount);
	case S_TX_WATCHDOG:	HWSTAT(TxWatchDogTimeouts);
	case S_RX_OVERFLOW:	HWSTAT(RxOverflows);
	case S_RX_FRAGERROR:	HWSTAT(RxFragErrors);
	case S_RX_MEMERROR:	HWSTAT(RxMemErrors);
	case S_PTRERROR:	HWSTAT(PointerErrors);
	case S_TX_UNDERFLOW:	HWSTAT(TxUnderflows);
	case S_TX_DONE:		HWSTAT(TxDone);
	case S_TX_DONEBUFPUT:	HWSTAT(TxDoneBufPut);
	case S_TX_WAIT4BUF:	HWSTAT(Wait4TxBuf);
	case S_TX_ATTEMPTS:	HWSTAT(TxAttempts);
	case S_TX_SUCCESS:	HWSTAT(TxSuccesses);
	case S_TX_FRAGS:	HWSTAT(TxFragments);
	case S_TX_MCAST:	HWSTAT(TxMulticasts);
	case S_RX_NONCTL:	HWSTAT(RxNonCtlPkts);
	case S_RX_MCAST:	HWSTAT(RxMulticasts);
	case S_RX_UNDECRYPT:	HWSTAT(RxUndecryptableFrames);
	case S_RX_ICVERROR:	HWSTAT(RxICVErrors);
	case S_RX_EXCLUDE:	HWSTAT(RxExcludedFrames);
	case S_TX_MGMT:		STAT(tx_mgmt);
	case S_TX_DISCARD:	STAT(tx_discard);
	case S_TX_QSTOP:	STAT(tx_qstop);
	case S_TX_ENCAP:	STAT(tx_encap);
	case S_TX_NOMBUF:	STAT(tx_nombuf);
	case S_TX_LINEAR:	STAT(tx_linear);
	case S_TX_NODATA:	STAT(tx_nodata);
	case S_TX_BUSDMA:	STAT(tx_busdma);
	case S_TX_SHORTPRE:	STAT(tx_shortpre);
	case S_TX_NOHEADROOM:	STAT(tx_noheadroom);
	case S_TX_BADFRAMETYPE:	STAT(tx_badframetype);
	case S_RX_CRYPTO_ERR:	STAT(rx_crypto);
	case S_RX_TKIPMIC:	STAT(rx_tkipmic);
	case S_RX_NODMABUF:	STAT(rx_nodmabuf);
	case S_RX_DMABUFMISSING:STAT(rx_dmabufmissing);
	case S_RX_NOMBUF:	STAT(rx_nombuf);
	case S_RX_BUSDMA:	STAT(rx_busdma);
	case S_AMPDU_NOSTREAM:	STAT(ampdu_nostream);
	case S_AMPDU_REJECT:	STAT(ampdu_reject);
	case S_ADDBA_NOSTREAM:	STAT(addba_nostream);
	case S_TX_TSO:		STAT(tx_tso);
	case S_TSO_BADETH:	STAT(tso_badeth);
	case S_TSO_NOHDR:	STAT(tso_nohdr);
	case S_TSO_BADSPLIT:	STAT(tso_badsplit);
	case S_BAWATCHDOG:	STAT(bawatchdog);
	case S_BAWATCHDOG_NOTFOUND:STAT(bawatchdog_notfound);
	case S_BAWATCHDOG_EMPTY: STAT(bawatchdog_empty);
	case S_BAWATCHDOG_FAILED:STAT(bawatchdog_failed);
	case S_RADARDETECT:	STAT(radardetect);
	case S_RX_RSSI:
		snprintf(b, bs, "%d", wf->cur.mst_rx_rssi);
		return 1;
	case S_ANT_TX0:		TXANT(0);
	case S_ANT_TX1:		TXANT(1);
	case S_ANT_TX2:		TXANT(2);
	case S_ANT_TX3:		TXANT(3);
	case S_ANT_RX0:		RXANT(0);
	case S_ANT_RX1:		RXANT(1);
	case S_ANT_RX2:		RXANT(2);
	case S_ANT_RX3:		RXANT(3);
	case S_RX_NOISE:
		snprintf(b, bs, "%d", wf->cur.mst_rx_noise);
		return 1;
	case S_RX_SIGNAL:
		snprintf(b, bs, "%d",
			wf->cur.mst_rx_rssi + wf->cur.mst_rx_noise);
		return 1;
	}
	b[0] = '\0';
	return 0;
#undef RXANT
#undef TXANT
#undef HWSTAT
#undef STAT
}

static int
mwl_get_totstat(struct bsdstat *sf, int s, char b[], size_t bs)
{
	struct mwlstatfoo_p *wf = (struct mwlstatfoo_p *) sf;
#define	STAT(x) \
	snprintf(b, bs, "%u", wf->total.mst_##x); return 1
#define	HWSTAT(x) \
	snprintf(b, bs, "%u", wf->total.hw_stats.x); return 1
#define	TXANT(x) \
	snprintf(b, bs, "%u", wf->total.mst_ant_tx[x]); return 1
#define	RXANT(x) \
	snprintf(b, bs, "%u", wf->total.mst_ant_rx[x]); return 1

	switch (s) {
	case S_INPUT:
		snprintf(b, bs, "%lu", (u_long)wf->total.mst_rx_packets);
		return 1;
	case S_OUTPUT:
		snprintf(b, bs, "%lu", (u_long) wf->total.mst_tx_packets);
		return 1;
	case S_RATE:
		setrate(b, bs, wf->total.mst_tx_rate);
		return 1;
	case S_TX_RETRY:	HWSTAT(TxRetrySuccesses);
	case S_TX_MRETRY:	HWSTAT(TxMultipleRetrySuccesses);
	case S_TX_RTSGOOD:	HWSTAT(RTSSuccesses);
	case S_TX_RTSBAD:	HWSTAT(RTSFailures);
	case S_TX_NOACK:	HWSTAT(AckFailures);
	case S_RX_DUPLICATE:	HWSTAT(RxDuplicateFrames);
	case S_RX_FCS:		HWSTAT(FCSErrorCount);
	case S_TX_WATCHDOG:	HWSTAT(TxWatchDogTimeouts);
	case S_RX_OVERFLOW:	HWSTAT(RxOverflows);
	case S_RX_FRAGERROR:	HWSTAT(RxFragErrors);
	case S_RX_MEMERROR:	HWSTAT(RxMemErrors);
	case S_PTRERROR:	HWSTAT(PointerErrors);
	case S_TX_UNDERFLOW:	HWSTAT(TxUnderflows);
	case S_TX_DONE:		HWSTAT(TxDone);
	case S_TX_DONEBUFPUT:	HWSTAT(TxDoneBufPut);
	case S_TX_WAIT4BUF:	HWSTAT(Wait4TxBuf);
	case S_TX_ATTEMPTS:	HWSTAT(TxAttempts);
	case S_TX_SUCCESS:	HWSTAT(TxSuccesses);
	case S_TX_FRAGS:	HWSTAT(TxFragments);
	case S_TX_MCAST:	HWSTAT(TxMulticasts);
	case S_RX_NONCTL:	HWSTAT(RxNonCtlPkts);
	case S_RX_MCAST:	HWSTAT(RxMulticasts);
	case S_RX_UNDECRYPT:	HWSTAT(RxUndecryptableFrames);
	case S_RX_ICVERROR:	HWSTAT(RxICVErrors);
	case S_RX_EXCLUDE:	HWSTAT(RxExcludedFrames);
	case S_TX_MGMT:		STAT(tx_mgmt);
	case S_TX_DISCARD:	STAT(tx_discard);
	case S_TX_QSTOP:	STAT(tx_qstop);
	case S_TX_ENCAP:	STAT(tx_encap);
	case S_TX_NOMBUF:	STAT(tx_nombuf);
	case S_TX_LINEAR:	STAT(tx_linear);
	case S_TX_NODATA:	STAT(tx_nodata);
	case S_TX_BUSDMA:	STAT(tx_busdma);
	case S_TX_SHORTPRE:	STAT(tx_shortpre);
	case S_TX_NOHEADROOM:	STAT(tx_noheadroom);
	case S_TX_BADFRAMETYPE:	STAT(tx_badframetype);
	case S_RX_CRYPTO_ERR:	STAT(rx_crypto);
	case S_RX_TKIPMIC:	STAT(rx_tkipmic);
	case S_RX_NODMABUF:	STAT(rx_nodmabuf);
	case S_RX_DMABUFMISSING:STAT(rx_dmabufmissing);
	case S_RX_NOMBUF:	STAT(rx_nombuf);
	case S_RX_BUSDMA:	STAT(rx_busdma);
	case S_AMPDU_NOSTREAM:	STAT(ampdu_nostream);
	case S_AMPDU_REJECT:	STAT(ampdu_reject);
	case S_ADDBA_NOSTREAM:	STAT(addba_nostream);
	case S_TX_TSO:		STAT(tx_tso);
	case S_TSO_BADETH:	STAT(tso_badeth);
	case S_TSO_NOHDR:	STAT(tso_nohdr);
	case S_TSO_BADSPLIT:	STAT(tso_badsplit);
	case S_BAWATCHDOG:	STAT(bawatchdog);
	case S_BAWATCHDOG_NOTFOUND:STAT(bawatchdog_notfound);
	case S_BAWATCHDOG_EMPTY: STAT(bawatchdog_empty);
	case S_BAWATCHDOG_FAILED:STAT(bawatchdog_failed);
	case S_RADARDETECT:	STAT(radardetect);
	case S_RX_RSSI:
		snprintf(b, bs, "%d", wf->total.mst_rx_rssi);
		return 1;
	case S_ANT_TX0:		TXANT(0);
	case S_ANT_TX1:		TXANT(1);
	case S_ANT_TX2:		TXANT(2);
	case S_ANT_TX3:		TXANT(3);
	case S_ANT_RX0:		RXANT(0);
	case S_ANT_RX1:		RXANT(1);
	case S_ANT_RX2:		RXANT(2);
	case S_ANT_RX3:		RXANT(3);
	case S_RX_NOISE:
		snprintf(b, bs, "%d", wf->total.mst_rx_noise);
		return 1;
	case S_RX_SIGNAL:
		snprintf(b, bs, "%d",
			wf->total.mst_rx_rssi + wf->total.mst_rx_noise);
		return 1;
	}
	b[0] = '\0';
	return 0;
#undef RXANT
#undef TXANT
#undef HWSTAT
#undef STAT
}

static void
mwl_print_verbose(struct bsdstat *sf, FILE *fd)
{
	struct mwlstatfoo_p *wf = (struct mwlstatfoo_p *) sf;
	const struct fmt *f;
	char s[32];
	const char *indent;
	int i, width;

	width = 0;
	for (i = 0; i < S_LAST; i++) {
		f = &sf->stats[i];
		if (f->width > width)
			width = f->width;
	}
	for (i = 0; i < S_LAST; i++) {
		f = &sf->stats[i];
		if (mwl_get_totstat(sf, i, s, sizeof(s)) && strcmp(s, "0")) {
			indent = "";
			fprintf(fd, "%s%-*s %s\n", indent, width, s, f->desc);
		}
	}
	fprintf(fd, "Antenna profile:\n");
	for (i = 0; i < 4; i++)
		if (wf->total.mst_ant_rx[i] || wf->total.mst_ant_tx[i])
			fprintf(fd, "[%u] tx %8u rx %8u\n", i,
				wf->total.mst_ant_tx[i],
				wf->total.mst_ant_rx[i]);
}

BSDSTAT_DEFINE_BOUNCE(mwlstatfoo)

struct mwlstatfoo *
mwlstats_new(const char *ifname, const char *fmtstring)
{
	struct mwlstatfoo_p *wf;

	wf = calloc(1, sizeof(struct mwlstatfoo_p));
	if (wf != NULL) {
		bsdstat_init(&wf->base.base, "mwlstats", mwlstats,
		    nitems(mwlstats));
		/* override base methods */
		wf->base.base.collect_cur = mwl_collect_cur;
		wf->base.base.collect_tot = mwl_collect_tot;
		wf->base.base.get_curstat = mwl_get_curstat;
		wf->base.base.get_totstat = mwl_get_totstat;
		wf->base.base.update_tot = mwl_update_tot;
		wf->base.base.print_verbose = mwl_print_verbose;

		/* setup bounce functions for public methods */
		BSDSTAT_BOUNCE(wf, mwlstatfoo);

		/* setup our public methods */
		wf->base.setifname = mwl_setifname;
#if 0
		wf->base.setstamac = wlan_setstamac;
#endif
		wf->s = socket(AF_INET, SOCK_DGRAM, 0);
		if (wf->s < 0)
			err(1, "socket");

		mwl_setifname(&wf->base, ifname);
		wf->base.setfmt(&wf->base, fmtstring);
	}
	return &wf->base;
}
