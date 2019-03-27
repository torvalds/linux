/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Monthadar Al Jaberi, TerraNet AB
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

#ifndef _DEV_WTAP_WTAPVAR_H
#define _DEV_WTAP_WTAPVAR_H

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/errno.h>
#include <sys/callout.h>
#include <sys/endian.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/priv.h>
#include <sys/sysctl.h>

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

#include <net/bpf.h>

#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if 0
#define DWTAP_PRINTF(...) printf(__VA_ARGS__)
#else
#define DWTAP_PRINTF(...)
#endif

#include "if_wtapioctl.h"

#define MAX_NBR_WTAP (64)
#define BEACON_INTRERVAL (1000)

MALLOC_DECLARE(M_WTAP);
MALLOC_DECLARE(M_WTAP_PACKET);
MALLOC_DECLARE(M_WTAP_BEACON);
MALLOC_DECLARE(M_WTAP_RXBUF);
MALLOC_DECLARE(M_WTAP_PLUGIN);

/* driver-specific node state */
struct wtap_node {
	struct ieee80211_node an_node;	/* base class */
	/* future addons */
};
#define	WTAP_NODE(ni)	((struct ath_node *)(ni))
#define	WTAP_NODE_CONST(ni)	((const struct ath_node *)(ni))

struct wtap_buf {
	STAILQ_ENTRY(wtap_buf)	bf_list;
	struct mbuf		*m;	/* mbuf for buf */
};
typedef STAILQ_HEAD(, wtap_buf) wtap_bufhead;

#define	WTAP_BUF_BUSY 0x00000002	/* (tx) desc owned by h/w */

struct wtap_vap {
	struct ieee80211vap av_vap;		/* base class */
	int32_t			id;		/* wtap id */
	struct cdev 		*av_dev;	/* userspace injecting frames */
	struct wtap_medium	*av_md;		/* back pointer */
	struct mbuf *beacon;			/* beacon */
	struct ieee80211_node	*bf_node;	/* pointer to the node */
	struct callout		av_swba;	/* software beacon alert */
	uint32_t		av_bcinterval;	/* beacon interval */
	void (*av_recv_mgmt)(struct ieee80211_node *,
	    struct mbuf *, int, const struct ieee80211_rx_stats *, int, int);
	int (*av_newstate)(struct ieee80211vap *,
	    enum ieee80211_state, int);
	void (*av_bmiss)(struct ieee80211vap *);
};
#define	WTAP_VAP(vap)	((struct wtap_vap *)(vap))

struct taskqueue;

struct wtap_softc {
	struct ieee80211com	sc_ic;
	char 			name[7];	/* wtapXX\0 */
	int32_t			id;
	int32_t			up;
	struct wtap_medium	*sc_md;		/* interface medium */
	struct ieee80211_node*	(* sc_node_alloc)
	    (struct ieee80211vap *, const uint8_t [IEEE80211_ADDR_LEN]);
	void (*sc_node_free)(struct ieee80211_node *);
	struct mtx		sc_mtx;		/* master lock (recursive) */
	struct taskqueue	*sc_tq;		/* private task queue */
	wtap_bufhead		sc_rxbuf;	/* receive buffer */
	struct task		sc_rxtask;	/* rx int processing */
	struct wtap_tx_radiotap_header sc_tx_th;
	int			sc_tx_th_len;
	struct wtap_rx_radiotap_header sc_rx_th;
	int			sc_rx_th_len;
};

int32_t	wtap_attach(struct wtap_softc *, const uint8_t *macaddr);
int32_t	wtap_detach(struct wtap_softc *);
void	wtap_resume(struct wtap_softc *);
void	wtap_suspend(struct wtap_softc *);
void	wtap_shutdown(struct wtap_softc *);
void	wtap_intr(struct wtap_softc *);
void	wtap_inject(struct wtap_softc *, struct mbuf *);
void	wtap_rx_deliver(struct wtap_softc *, struct mbuf *);

#endif
