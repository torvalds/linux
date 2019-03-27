/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_ath.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/callout.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/priv.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_llc.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#ifdef IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif
#ifdef IEEE80211_SUPPORT_TDMA
#include <net80211/ieee80211_tdma.h>
#endif

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/ath_hal/ah_devid.h>		/* XXX for softled */
#include <dev/ath/ath_hal/ah_diagcodes.h>

#ifdef ATH_TX99_DIAG
#include <dev/ath/ath_tx99/ath_tx99.h>
#endif

#ifdef ATH_DEBUG
#include <dev/ath/if_ath_debug.h>

uint64_t ath_debug = 0;

SYSCTL_DECL(_hw_ath);
SYSCTL_QUAD(_hw_ath, OID_AUTO, debug, CTLFLAG_RWTUN, &ath_debug,
	    0, "control debugging printfs");

void
ath_printrxbuf(struct ath_softc *sc, const struct ath_buf *bf,
	u_int ix, int done)
{
	const struct ath_rx_status *rs = &bf->bf_status.ds_rxstat;
	struct ath_hal *ah = sc->sc_ah;
	const struct ath_desc *ds;
	int i;

	for (i = 0, ds = bf->bf_desc; i < bf->bf_nseg; i++, ds++) {
		printf("R[%2u] (DS.V:%p DS.P:%p) L:%08x D:%08x%s\n"
		       "      %08x %08x %08x %08x\n",
		    ix, ds, (const struct ath_desc *)bf->bf_daddr + i,
		    ds->ds_link, ds->ds_data,
		    !done ? "" : (rs->rs_status == 0) ? " *" : " !",
		    ds->ds_ctl0, ds->ds_ctl1,
		    ds->ds_hw[0], ds->ds_hw[1]);
		if (ah->ah_magic == 0x20065416) {
			printf("        %08x %08x %08x %08x %08x %08x %08x\n",
			    ds->ds_hw[2], ds->ds_hw[3], ds->ds_hw[4],
			    ds->ds_hw[5], ds->ds_hw[6], ds->ds_hw[7],
			    ds->ds_hw[8]);
		} else if (ah->ah_magic == 0x19741014) {
			printf("        %08x %08x %08x %08x %08x %08x %08x\n",
			    ds->ds_hw[2], ds->ds_hw[3], ds->ds_hw[4],
			    ds->ds_hw[5], ds->ds_hw[6], ds->ds_hw[7],
			    ds->ds_hw[8]);

			printf("        %08x %08x %08x %08x %08x %08x %08x\n",
			    ds->ds_hw[9], ds->ds_hw[10], ds->ds_hw[11],
			    ds->ds_hw[12], ds->ds_hw[13], ds->ds_hw[14],
			    ds->ds_hw[15]);
		}
	}
}

static void
ath_printtxbuf_edma(struct ath_softc *sc, const struct ath_buf *first_bf,
	u_int qnum, u_int ix, int done)
{
	const struct ath_tx_status *ts =
	    &first_bf->bf_last->bf_status.ds_txstat;
	const struct ath_buf *bf = first_bf;
	const char *ds;
	const struct ath_desc_txedma *eds;
	int i, n;

	printf("Q%u[%3u] (nseg=%d)", qnum, ix, bf->bf_nseg);
	while (bf != NULL) {
		/*
		 * XXX For now, assume the txmap size is 4.
		 */

		/*
		 * Assume the TX map size is 4 for now and only walk
		 * the appropriate number of segments.
		 */
		n = ((bf->bf_nseg - 1) / 4) + 1;

		for (i = 0, ds = (const char *) bf->bf_desc;
		    i < n;
		    i ++, ds += sc->sc_tx_desclen) {
			eds = (const struct ath_desc_txedma *) ds;
			printf(" (DS.V:%p DS.P:%p) I: %08x L:%08x F:%04x%s\n",
			    eds, (const struct ath_desc *)bf->bf_daddr + i,
			    eds->ds_info, eds->ds_link,
			    bf->bf_state.bfs_txflags,
			    !done ? "" : (ts->ts_status == 0) ? " *" : " !");
			printf(" (D[0] = %08x(%08x), D[1] = %08x(%08x)\n",
			    eds->ds_hw[0], eds->ds_hw[1],
			    eds->ds_hw[2], eds->ds_hw[3]);
			printf(" (D[2] = %08x(%08x), D[3] = %08x(%08x)\n",
			    eds->ds_hw[4], eds->ds_hw[5],
			    eds->ds_hw[6], eds->ds_hw[7]);
			printf("        Seq: %d swtry: %d ADDBAW?: %d DOBAW?: %d\n",
			    bf->bf_state.bfs_seqno,
			    bf->bf_state.bfs_retries,
			    bf->bf_state.bfs_addedbaw,
			    bf->bf_state.bfs_dobaw);
			printf("        %08x %08x %08x %08x %08x %08x\n",
			    eds->ds_hw[8], eds->ds_hw[9],
			    eds->ds_hw[10], eds->ds_hw[11],
			    eds->ds_hw[12], eds->ds_hw[13]);
			printf("        %08x %08x %08x %08x %08x %08x %08x\n",
			    eds->ds_hw[14], eds->ds_hw[15], eds->ds_hw[16],
			    eds->ds_hw[17], eds->ds_hw[18], eds->ds_hw[19],
			    eds->ds_hw[20]);
#if 0
			printf("        %08x %08x %08x %08x %08x %08x %08x %08x\n",
			    ds->ds_hw[22],ds->ds_hw[23],ds->ds_hw[24],
			    ds->ds_hw[25],ds->ds_hw[26],ds->ds_hw[27],
			    ds->ds_hw[28], ds->ds_hw[29]);
#endif
		}
		printf("  [end]\n");
		bf = bf->bf_next;
	}
}

static void
ath_printtxbuf_legacy(struct ath_softc *sc, const struct ath_buf *first_bf,
	u_int qnum, u_int ix, int done)
{
	const struct ath_tx_status *ts = &first_bf->bf_status.ds_txstat;
	const struct ath_buf *bf = first_bf;
	struct ath_hal *ah = sc->sc_ah;
	const struct ath_desc *ds;
	int i;

	printf("Q%u[%3u]", qnum, ix);
	while (bf != NULL) {
		printf("    (bf=%p, lastds=%p)\n", bf, first_bf->bf_lastds);
		printf("        Seq: %d swtry: %d ADDBAW?: %d DOBAW?: %d\n",
		    bf->bf_state.bfs_seqno,
		    bf->bf_state.bfs_retries,
		    bf->bf_state.bfs_addedbaw,
		    bf->bf_state.bfs_dobaw);
		for (i = 0, ds = bf->bf_desc; i < bf->bf_nseg; i++, ds++) {
			printf(" (DS.V:%p DS.P:%p) L:%08x D:%08x F:%04x%s\n",
			    ds, (const struct ath_desc *)bf->bf_daddr + i,
			    ds->ds_link, ds->ds_data, bf->bf_state.bfs_txflags,
			    !done ? "" : (ts->ts_status == 0) ? " *" : " !");
			printf("        %08x %08x %08x %08x %08x %08x\n",
			    ds->ds_ctl0, ds->ds_ctl1,
			    ds->ds_hw[0], ds->ds_hw[1],
			    ds->ds_hw[2], ds->ds_hw[3]);
			if (ah->ah_magic == 0x20065416) {
				printf("        %08x %08x %08x %08x %08x %08x %08x %08x\n",
				    ds->ds_hw[4], ds->ds_hw[5], ds->ds_hw[6],
				    ds->ds_hw[7], ds->ds_hw[8], ds->ds_hw[9],
				    ds->ds_hw[10],ds->ds_hw[11]);
				printf("        %08x %08x %08x %08x %08x %08x %08x %08x\n",
				    ds->ds_hw[12],ds->ds_hw[13],ds->ds_hw[14],
				    ds->ds_hw[15],ds->ds_hw[16],ds->ds_hw[17],
				    ds->ds_hw[18], ds->ds_hw[19]);
			}
		}
		printf("  [end]\n");
		bf = bf->bf_next;
	}
}

void
ath_printtxbuf(struct ath_softc *sc, const struct ath_buf *first_bf,
	u_int qnum, u_int ix, int done)
{
	if (sc->sc_ah->ah_magic == 0x19741014)
		ath_printtxbuf_edma(sc, first_bf, qnum, ix, done);
	else
		ath_printtxbuf_legacy(sc, first_bf, qnum, ix, done);
}

void
ath_printtxstatbuf(struct ath_softc *sc, const struct ath_buf *first_bf,
	const uint32_t *ds, u_int qnum, u_int ix, int done)
{

	printf("Q%u[%3u] ", qnum, ix);
	printf("        %08x %08x %08x %08x %08x %08x\n",
	    ds[0], ds[1], ds[2], ds[3], ds[4], ds[5]);
	printf("        %08x %08x %08x %08x %08x\n",
	    ds[6], ds[7], ds[8], ds[9], ds[10]);
}

#endif	/* ATH_DEBUG */
