/*	$OpenBSD: if_wi_hostap.c,v 1.52 2018/02/19 08:59:52 mpi Exp $	*/

/*
 * Copyright (c) 2002
 *	Thomas Skibo <skibo@pacbell.net>.  All rights reserved.
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
 *	This product includes software developed by Thomas Skibo.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Thomas Skibo AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Thomas Skibo OR HIS DRINKING PALS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* This is experimental Host AP software for Prism 2 802.11b interfaces.
 *
 * Much of this is based upon the "Linux Host AP driver Host AP driver
 * for Intersil Prism2" by Jouni Malinen <jkm@ssh.com> or <jkmaline@cc.hut.fi>.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/timeout.h>
#include <sys/ucred.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>

#include <dev/ic/if_wireg.h>
#include <dev/ic/if_wi_ieee.h>
#include <dev/ic/if_wivar.h>

void wihap_timeout(void *v);
void wihap_sta_timeout(void *v);
struct wihap_sta_info *wihap_sta_alloc(struct wi_softc *sc, u_int8_t *addr);
void wihap_sta_delete(struct wihap_sta_info *sta);
struct wihap_sta_info *wihap_sta_find(struct wihap_info *whi, u_int8_t *addr);
int wihap_sta_is_assoc(struct wihap_info *whi, u_int8_t addr[]);
void wihap_auth_req(struct wi_softc *sc, struct wi_frame *rxfrm,
    caddr_t pkt, int len);
void wihap_sta_deauth(struct wi_softc *sc, u_int8_t sta_addr[],
    u_int16_t reason);
void wihap_deauth_req(struct wi_softc *sc, struct wi_frame *rxfrm,
    caddr_t pkt, int len);
void wihap_assoc_req(struct wi_softc *sc, struct wi_frame *rxfrm,
    caddr_t pkt, int len);
void wihap_sta_disassoc(struct wi_softc *sc, u_int8_t sta_addr[],
    u_int16_t reason);
void wihap_disassoc_req(struct wi_softc *sc, struct wi_frame *rxfrm,
    caddr_t pkt, int len);

#ifndef SMALL_KERNEL
/*
 * take_hword()
 *
 *	Used for parsing management frames.  The pkt pointer and length
 *	variables are updated after the value is removed.
 */
static __inline u_int16_t
take_hword(caddr_t *ppkt, int *plen)
{
	u_int16_t s = letoh16(* (u_int16_t *) *ppkt);
	*ppkt += sizeof(u_int16_t);
	*plen -= sizeof(u_int16_t);
	return s;
}

/* take_tlv()
 *
 *	Parse out TLV element from a packet, check for underflow of packet
 *	or overflow of buffer, update pkt/len.
 */
static int
take_tlv(caddr_t *ppkt, int *plen, int id_expect, void *dst, int maxlen)
{
	u_int8_t id, len;

	if (*plen < 2)
		return -1;

	id = ((u_int8_t *)*ppkt)[0];
	len = ((u_int8_t *)*ppkt)[1];

	if (id != id_expect || *plen < len+2 || maxlen < len)
		return -1;

	bcopy(*ppkt + 2, dst, len);
	*plen -= 2 + len;
	*ppkt += 2 + len;

	return (len);
}

/* put_hword()
 *	Put half-word element into management frames.
 */
static __inline void
put_hword(caddr_t *ppkt, u_int16_t s)
{
	* (u_int16_t *) *ppkt = htole16(s);
	*ppkt += sizeof(u_int16_t);
}

/* put_tlv()
 *	Put TLV elements into management frames.
 */
static void
put_tlv(caddr_t *ppkt, u_int8_t id, void *src, u_int8_t len)
{
	(*ppkt)[0] = id;
	(*ppkt)[1] = len;
	bcopy(src, (*ppkt) + 2, len);
	*ppkt += 2 + len;
}

static int
put_rates(caddr_t *ppkt, u_int16_t rates)
{
	u_int8_t ratebuf[8];
	int len = 0;

	if (rates & WI_SUPPRATES_1M)
		ratebuf[len++] = 0x82;
	if (rates & WI_SUPPRATES_2M)
		ratebuf[len++] = 0x84;
	if (rates & WI_SUPPRATES_5M)
		ratebuf[len++] = 0x8b;
	if (rates & WI_SUPPRATES_11M)
		ratebuf[len++] = 0x96;

	put_tlv(ppkt, IEEE80211_ELEMID_RATES, ratebuf, len);
	return len;
}

/* wihap_init()
 *
 *	Initialize host AP data structures.  Called even if port type is
 *	not AP.  Caller MUST raise to splnet().
 */
void
wihap_init(struct wi_softc *sc)
{
	int i;
	struct wihap_info *whi = &sc->wi_hostap_info;

	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		printf("wihap_init: sc=%p whi=%p\n", sc, whi);

	bzero(whi, sizeof(struct wihap_info));

	if (sc->wi_ptype != WI_PORTTYPE_HOSTAP)
		return;

	whi->apflags = WIHAPFL_ACTIVE;

	TAILQ_INIT(&whi->sta_list);
	for (i = 0; i < WI_STA_HASH_SIZE; i++)
		LIST_INIT(&whi->sta_hash[i]);

	whi->inactivity_time = WIHAP_DFLT_INACTIVITY_TIME;
	timeout_set(&whi->tmo, wihap_timeout, sc);
}

/* wihap_sta_disassoc()
 *
 *	Send a disassociation frame to a specified station.
 */
void
wihap_sta_disassoc(struct wi_softc *sc, u_int8_t sta_addr[], u_int16_t reason)
{
	struct wi_80211_hdr	*resp_hdr;
	caddr_t			pkt;

	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		printf("Sending disassoc to sta %s\n", ether_sprintf(sta_addr));

	/* Send disassoc packet. */
	resp_hdr = (struct wi_80211_hdr *)sc->wi_txbuf;
	bzero(resp_hdr, sizeof(struct wi_80211_hdr));
	resp_hdr->frame_ctl = WI_FTYPE_MGMT | WI_STYPE_MGMT_DISAS;
	pkt = (caddr_t)&sc->wi_txbuf + sizeof(struct wi_80211_hdr);

	bcopy(sta_addr, resp_hdr->addr1, ETHER_ADDR_LEN);
	bcopy(sc->sc_ic.ic_myaddr, resp_hdr->addr2, IEEE80211_ADDR_LEN);
	bcopy(sc->sc_ic.ic_myaddr, resp_hdr->addr3, IEEE80211_ADDR_LEN);

	put_hword(&pkt, reason);

	wi_mgmt_xmit(sc, (caddr_t)&sc->wi_txbuf,
	    2 + sizeof(struct wi_80211_hdr));
}

/* wihap_sta_deauth()
 *
 *	Send a deauthentication message to a specified station.
 */
void
wihap_sta_deauth(struct wi_softc *sc, u_int8_t sta_addr[], u_int16_t reason)
{
	struct wi_80211_hdr	*resp_hdr;
	caddr_t			pkt;

	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		printf("Sending deauth to sta %s\n", ether_sprintf(sta_addr));

	/* Send deauth packet. */
	resp_hdr = (struct wi_80211_hdr *)sc->wi_txbuf;
	bzero(resp_hdr, sizeof(struct wi_80211_hdr));
	resp_hdr->frame_ctl = htole16(WI_FTYPE_MGMT | WI_STYPE_MGMT_DEAUTH);
	pkt = (caddr_t)&sc->wi_txbuf + sizeof(struct wi_80211_hdr);

	bcopy(sta_addr, resp_hdr->addr1, ETHER_ADDR_LEN);
	bcopy(sc->sc_ic.ic_myaddr, resp_hdr->addr2, IEEE80211_ADDR_LEN);
	bcopy(sc->sc_ic.ic_myaddr, resp_hdr->addr3, IEEE80211_ADDR_LEN);

	put_hword(&pkt, reason);

	wi_mgmt_xmit(sc, (caddr_t)&sc->wi_txbuf,
	    2 + sizeof(struct wi_80211_hdr));
}

/* wihap_shutdown()
 *
 *	Disassociate all stations and free up data structures.
 */
void
wihap_shutdown(struct wi_softc *sc)
{
	struct wihap_info	*whi = &sc->wi_hostap_info;
	struct wihap_sta_info	*sta, *next;
	int i, s;

	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		printf("wihap_shutdown: sc=%p whi=%p\n", sc, whi);

	if (!(whi->apflags & WIHAPFL_ACTIVE))
		return;
	whi->apflags = 0;

	s = splnet();

	/* Disable wihap inactivity timer. */
	timeout_del(&whi->tmo);

	/* Delete all stations from the list. */
	for (sta = TAILQ_FIRST(&whi->sta_list); sta != NULL; sta = next) {
		timeout_del(&sta->tmo);
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_shutdown: free(sta=%p)\n", sta);
		next = TAILQ_NEXT(sta, list);
		if (sta->challenge)
			free(sta->challenge, M_TEMP, 0);
		free(sta, M_DEVBUF, 0);
	}
	TAILQ_INIT(&whi->sta_list);

	/* Broadcast disassoc and deauth to all the stations. */
	if (sc->wi_flags & WI_FLAGS_ATTACHED) {
		for (i = 0; i < 5; i++) {
			wihap_sta_disassoc(sc, etherbroadcastaddr,
			    IEEE80211_REASON_ASSOC_LEAVE);
			wihap_sta_deauth(sc, etherbroadcastaddr,
			    IEEE80211_REASON_AUTH_LEAVE);
			DELAY(50);
		}
	}

	splx(s);
}

/* sta_hash_func()
 * Hash function for finding stations from ethernet address.
 */
static __inline int
sta_hash_func(u_int8_t addr[])
{
	return ((addr[3] + addr[4] + addr[5]) % WI_STA_HASH_SIZE);
}

/* addr_cmp():  Maybe this is a faster way to compare addresses? */
static __inline int
addr_cmp(u_int8_t a[], u_int8_t b[])
{
	return (*(u_int16_t *)(a + 4) == *(u_int16_t *)(b + 4) &&
		*(u_int16_t *)(a + 2) == *(u_int16_t *)(b + 2) &&
		*(u_int16_t *)(a    ) == *(u_int16_t *)(b));
}

/* wihap_sta_movetail(): move sta to the tail of the station list in whi */
static __inline void
wihap_sta_movetail(struct wihap_info *whi, struct wihap_sta_info *sta)
{
	TAILQ_REMOVE(&whi->sta_list, sta, list);
	sta->flags &= ~WI_SIFLAGS_DEAD;
	TAILQ_INSERT_TAIL(&whi->sta_list, sta, list);
}

void
wihap_timeout(void *v)
{
	struct wi_softc		*sc = v;
	struct wihap_info	*whi = &sc->wi_hostap_info;
	struct wihap_sta_info	*sta, *next;
	int	i, s;

	s = splnet();

	for (i = 10, sta = TAILQ_FIRST(&whi->sta_list);
	    i != 0 && sta != NULL && (sta->flags & WI_SIFLAGS_DEAD);
	    i--, sta = next) {
		next = TAILQ_NEXT(sta, list);
		if (timeout_pending(&sta->tmo)) {
			/* Became alive again, move to end of list. */
			wihap_sta_movetail(whi, sta);
		} else if (sta->flags & WI_SIFLAGS_ASSOC) {
			if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
				printf("wihap_timeout: disassoc due to inactivity: %s\n",
				    ether_sprintf(sta->addr));

			/* Disassoc station. */
			wihap_sta_disassoc(sc, sta->addr,
			    IEEE80211_REASON_ASSOC_EXPIRE);
			sta->flags &= ~WI_SIFLAGS_ASSOC;

			/*
			 * Move to end of the list and reset station timeout.
			 * We do this to make sure we don't get deauthed
			 * until inactivity_time seconds have passed.
			 */
			wihap_sta_movetail(whi, sta);
			timeout_add_sec(&sta->tmo, whi->inactivity_time);
		} else if (sta->flags & WI_SIFLAGS_AUTHEN) {
			if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
				printf("wihap_timeout: deauth due to inactivity: %s\n",
				    ether_sprintf(sta->addr));

			/* Deauthenticate station. */
			wihap_sta_deauth(sc, sta->addr,
			    IEEE80211_REASON_AUTH_EXPIRE);
			sta->flags &= ~WI_SIFLAGS_AUTHEN;

			/* Delete the station if it's not permanent. */
			if (sta->flags & WI_SIFLAGS_PERM)
				wihap_sta_movetail(whi, sta);
			else
				wihap_sta_delete(sta);
		}
	}

	/* Restart the timeout if there are still dead stations left. */
	sta = TAILQ_FIRST(&whi->sta_list);
	if (sta != NULL && (sta->flags & WI_SIFLAGS_DEAD))
		timeout_add(&whi->tmo, 1);	/* still work left, requeue */

	splx(s);
}

void
wihap_sta_timeout(void *v)
{
	struct wihap_sta_info	*sta = v;
	struct wi_softc		*sc = sta->sc;
	struct wihap_info	*whi = &sc->wi_hostap_info;
	int	s;

	s = splnet();

	/* Mark sta as dead and move it to the head of the list. */
	TAILQ_REMOVE(&whi->sta_list, sta, list);
	sta->flags |= WI_SIFLAGS_DEAD;
	TAILQ_INSERT_HEAD(&whi->sta_list, sta, list);

	/* Add wihap timeout if we have not already done so. */
	if (!timeout_pending(&whi->tmo))
		timeout_add(&whi->tmo, hz / 10);

	splx(s);
}

/* wihap_sta_delete()
 * Delete a single station and free up its data structure.
 * Caller must raise to splnet().
 */
void
wihap_sta_delete(struct wihap_sta_info *sta)
{
	struct wi_softc		*sc = sta->sc;
	struct wihap_info	*whi = &sc->wi_hostap_info;
	int i = sta->asid - 0xc001;

	timeout_del(&sta->tmo);

	whi->asid_inuse_mask[i >> 4] &= ~(1UL << (i & 0xf));

	TAILQ_REMOVE(&whi->sta_list, sta, list);
	LIST_REMOVE(sta, hash);
	if (sta->challenge)
		free(sta->challenge, M_TEMP, 0);
	free(sta, M_DEVBUF, 0);
	whi->n_stations--;
}

/* wihap_sta_alloc()
 *
 *	Create a new station data structure and put it in the list
 *	and hash table.
 */
struct wihap_sta_info *
wihap_sta_alloc(struct wi_softc *sc, u_int8_t *addr)
{
	struct wihap_info	*whi = &sc->wi_hostap_info;
	struct wihap_sta_info	*sta;
	int i, hash = sta_hash_func(addr);

	/* Allocate structure. */
	sta = malloc(sizeof(*sta), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sta == NULL)
		return (NULL);

	/* Allocate an ASID. */
	i=hash<<4;
	while (whi->asid_inuse_mask[i >> 4] & (1UL << (i & 0xf)))
		i = (i == (WI_STA_HASH_SIZE << 4) - 1) ? 0 : (i + 1);
	whi->asid_inuse_mask[i >> 4] |= (1UL << (i & 0xf));
	sta->asid = 0xc001 + i;

	/* Insert in list and hash list. */
	TAILQ_INSERT_TAIL(&whi->sta_list, sta, list);
	LIST_INSERT_HEAD(&whi->sta_hash[hash], sta, hash);

	sta->sc = sc;
	whi->n_stations++;
	bcopy(addr, &sta->addr, ETHER_ADDR_LEN);
	timeout_set(&sta->tmo, wihap_sta_timeout, sta);
	timeout_add_sec(&sta->tmo, whi->inactivity_time);

	return (sta);
}

/* wihap_sta_find()
 *
 *	Find station structure given address.
 */
struct wihap_sta_info *
wihap_sta_find(struct wihap_info *whi, u_int8_t *addr)
{
	int i;
	struct wihap_sta_info *sta;

	i = sta_hash_func(addr);
	LIST_FOREACH(sta, &whi->sta_hash[i], hash)
		if (addr_cmp(addr, sta->addr))
			return sta;

	return (NULL);
}

static __inline int
wihap_check_rates(struct wihap_sta_info *sta, u_int8_t rates[], int rates_len)
{
	struct wi_softc *sc = sta->sc;
	int	i;

	sta->rates = 0;
	sta->tx_max_rate = 0;
	for (i = 0; i < rates_len; i++)
		switch (rates[i] & 0x7f) {
		case 0x02:
			sta->rates |= WI_SUPPRATES_1M;
			break;
		case 0x04:
			sta->rates |= WI_SUPPRATES_2M;
			if (sta->tx_max_rate < 1)
				sta->tx_max_rate = 1;
			break;
		case 0x0b:
			sta->rates |= WI_SUPPRATES_5M;
			if (sta->tx_max_rate < 2)
				sta->tx_max_rate = 2;
			break;
		case 0x16:
			sta->rates |= WI_SUPPRATES_11M;
			sta->tx_max_rate = 3;
			break;
		}

	sta->rates &= sc->wi_supprates;
	sta->tx_curr_rate = sta->tx_max_rate;

	return (sta->rates == 0 ? -1 : 0);
}


/* wihap_auth_req()
 *
 *	Handle incoming authentication request.
 */
void
wihap_auth_req(struct wi_softc *sc, struct wi_frame *rxfrm,
    caddr_t pkt, int len)
{
	struct wihap_info	*whi = &sc->wi_hostap_info;
	struct wihap_sta_info	*sta;
	int			i, s;

	u_int16_t		algo;
	u_int16_t		seq;
	u_int16_t		status;
	int			challenge_len;
	u_int32_t		challenge[32];

	struct wi_80211_hdr	*resp_hdr;

	if (len < 6) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_auth_req: station %s short request\n",
			    ether_sprintf(rxfrm->wi_addr2));
		return;
	}

	/* Break open packet. */
	algo = take_hword(&pkt, &len);
	seq = take_hword(&pkt, &len);
	status = take_hword(&pkt, &len);
	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		printf("wihap_auth_req: station %s algo=0x%x seq=0x%x\n",
		    ether_sprintf(rxfrm->wi_addr2), algo, seq);

	/* Ignore vendor private tlv (if any). */
	(void)take_tlv(&pkt, &len, IEEE80211_ELEMID_VENDOR, challenge,
	    sizeof(challenge));

	challenge_len = 0;
	if (len > 0 && (challenge_len = take_tlv(&pkt, &len,
	    IEEE80211_ELEMID_CHALLENGE, challenge, sizeof(challenge))) < 0) {
		status = IEEE80211_STATUS_CHALLENGE;
		goto fail;
	}

	/* Find or create station info. */
	sta = wihap_sta_find(whi, rxfrm->wi_addr2);
	if (sta == NULL) {

		/* Are we allowing new stations?
		 */
		if (whi->apflags & WIHAPFL_MAC_FILT) {
			status = IEEE80211_STATUS_OTHER; /* XXX */
			goto fail;
		}

		/* Check for too many stations.
		 */
		if (whi->n_stations >= WIHAP_MAX_STATIONS) {
			status = IEEE80211_STATUS_TOOMANY;
			goto fail;
		}

		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_auth_req: new station\n");

		/* Create new station. */
		s = splnet();
		sta = wihap_sta_alloc(sc, rxfrm->wi_addr2);
		splx(s);
		if (sta == NULL) {
			/* Out of memory! */
			status = IEEE80211_STATUS_TOOMANY;
			goto fail;
		}
	}
	timeout_add_sec(&sta->tmo, whi->inactivity_time);

	/* Note: it's okay to leave the station info structure around
	 * if the authen fails.  It'll be timed out eventually.
	 */
	switch (algo) {
	case IEEE80211_AUTH_ALG_OPEN:
		if (sc->wi_authtype != IEEE80211_AUTH_OPEN) {
			status = IEEE80211_STATUS_ALG;
			goto fail;
		}
		if (seq != 1) {
			status = IEEE80211_STATUS_SEQUENCE;
			goto fail;
		}
		challenge_len = 0;
		sta->flags |= WI_SIFLAGS_AUTHEN;
		break;
	case IEEE80211_AUTH_ALG_SHARED:
		if (sc->wi_authtype != IEEE80211_AUTH_SHARED) {
			status = IEEE80211_STATUS_ALG;
			goto fail;
		}
		switch (seq) {
		case 1:
			/* Create a challenge frame. */
			if (!sta->challenge) {
				sta->challenge = malloc(128, M_TEMP, M_NOWAIT);
				if (!sta->challenge)
					return;
			}
			for (i = 0; i < 32; i++)
				challenge[i] = sta->challenge[i] =
					arc4random();
			
			if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
				printf("\tchallenge: 0x%x 0x%x ...\n",
				   challenge[0], challenge[1]);
			challenge_len = 128;
			break;
		case 3:
			if (challenge_len != 128 || !sta->challenge ||
			    !(letoh16(rxfrm->wi_frame_ctl) & WI_FCTL_WEP)) {
				status = IEEE80211_STATUS_CHALLENGE;
				goto fail;
			}

			for (i=0; i<32; i++)
				if (sta->challenge[i] != challenge[i]) {
					status = IEEE80211_STATUS_CHALLENGE;
					goto fail;
				}

			sta->flags |= WI_SIFLAGS_AUTHEN;
			free(sta->challenge, M_TEMP, 0);
			sta->challenge = NULL;
			challenge_len = 0;
			break;
		default:
			status = IEEE80211_STATUS_SEQUENCE;
			goto fail;
		} /* switch (seq) */
		break;
	default:
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_auth_req: algorithm unsupported: 0x%x\n",
			   algo);
		status = IEEE80211_STATUS_ALG;
		goto fail;
	} /* switch (algo) */

	status = IEEE80211_STATUS_SUCCESS;

fail:
	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		printf("wihap_auth_req: returns status=0x%x\n", status);

	/* Send response. */
	resp_hdr = (struct wi_80211_hdr *)&sc->wi_txbuf;
	bzero(resp_hdr, sizeof(struct wi_80211_hdr));
	resp_hdr->frame_ctl = htole16(WI_FTYPE_MGMT | WI_STYPE_MGMT_AUTH);
	bcopy(rxfrm->wi_addr2, resp_hdr->addr1, ETHER_ADDR_LEN);
	bcopy(sc->sc_ic.ic_myaddr, resp_hdr->addr2, IEEE80211_ADDR_LEN);
	bcopy(sc->sc_ic.ic_myaddr, resp_hdr->addr3, IEEE80211_ADDR_LEN);

	pkt = (caddr_t)&sc->wi_txbuf + sizeof(struct wi_80211_hdr);
	put_hword(&pkt, algo);
	put_hword(&pkt, seq + 1);
	put_hword(&pkt, status);
	if (challenge_len > 0)
		put_tlv(&pkt, IEEE80211_ELEMID_CHALLENGE,
			challenge, challenge_len);

	wi_mgmt_xmit(sc, (caddr_t)&sc->wi_txbuf,
	    6 + sizeof(struct wi_80211_hdr) +
	    (challenge_len > 0 ? challenge_len + 2 : 0));
}


/* wihap_assoc_req()
 *
 *	Handle incoming association and reassociation requests.
 */
void
wihap_assoc_req(struct wi_softc *sc, struct wi_frame *rxfrm,
		caddr_t pkt, int len)
{
	struct wihap_info	*whi = &sc->wi_hostap_info;
	struct wihap_sta_info	*sta;
	struct wi_80211_hdr	*resp_hdr;
	u_int16_t		capinfo;
	u_int16_t		lstintvl;
	u_int8_t		rates[12];
	int			ssid_len, rates_len;
	struct ieee80211_nwid	ssid;
	u_int16_t		status;
	u_int16_t		asid = 0;

	if (len < 8)
		return;

	/* Pull out request parameters. */
	capinfo = take_hword(&pkt, &len);
	lstintvl = take_hword(&pkt, &len);

	if ((rxfrm->wi_frame_ctl & htole16(WI_FCTL_STYPE)) ==
	    htole16(WI_STYPE_MGMT_REASREQ)) {
		if (len < 6)
			return;
		/* Eat the MAC address of the current AP */
		take_hword(&pkt, &len);
		take_hword(&pkt, &len);
		take_hword(&pkt, &len);
	}

	if ((ssid_len = take_tlv(&pkt, &len, IEEE80211_ELEMID_SSID,
	    ssid.i_nwid, sizeof(ssid))) < 0)
		return;
	ssid.i_len = ssid_len;
	if ((rates_len = take_tlv(&pkt, &len, IEEE80211_ELEMID_RATES,
	    rates, sizeof(rates))) < 0)
		return;

	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		printf("wihap_assoc_req: from station %s\n",
		    ether_sprintf(rxfrm->wi_addr2));

	/* If SSID doesn't match, simply drop. */
	if (sc->wi_net_name.i_len != ssid.i_len ||
	    memcmp(sc->wi_net_name.i_nwid, ssid.i_nwid, ssid.i_len)) {

		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_assoc_req: bad ssid: '%.*s' != '%.*s'\n",
			    ssid.i_len, ssid.i_nwid, sc->wi_net_name.i_len,
			    sc->wi_net_name.i_nwid);
		return;
	}

	/* Is this station authenticated yet? */
	sta = wihap_sta_find(whi, rxfrm->wi_addr2);
	if (sta == NULL || !(sta->flags & WI_SIFLAGS_AUTHEN)) {
		wihap_sta_deauth(sc, rxfrm->wi_addr2,
		    IEEE80211_REASON_NOT_AUTHED);
		return;
	}

	/* Check supported rates against ours. */
	if (wihap_check_rates(sta, rates, rates_len) < 0) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_assoc_req: rates mismatch.\n");
		status = IEEE80211_STATUS_BASIC_RATE;
		goto fail;
	}

	/* Check capinfo.
	 * Check for ESS, not IBSS.
	 * Check WEP/PRIVACY flags match.
	 * Refuse stations requesting to be put on CF-polling list.
	 */
	sta->capinfo = capinfo;
	status = IEEE80211_STATUS_CAPINFO;
	if ((capinfo & (IEEE80211_CAPINFO_ESS | IEEE80211_CAPINFO_IBSS)) !=
	    IEEE80211_CAPINFO_ESS) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_assoc_req: capinfo: not ESS: "
			    "capinfo=0x%x\n", capinfo);
		goto fail;

	}
	if ((sc->wi_use_wep && !(capinfo & IEEE80211_CAPINFO_PRIVACY)) ||
	    (!sc->wi_use_wep && (capinfo & IEEE80211_CAPINFO_PRIVACY))) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_assoc_req: WEP flag mismatch: "
			    "capinfo=0x%x\n", capinfo);
		goto fail;
	}
	if ((capinfo & (IEEE80211_CAPINFO_CF_POLLABLE |
	    IEEE80211_CAPINFO_CF_POLLREQ)) == IEEE80211_CAPINFO_CF_POLLABLE) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_assoc_req: polling not supported: "
			    "capinfo=0x%x\n", capinfo);
		goto fail;
	}

	/* Use ASID is allocated by whi_sta_alloc(). */
	asid = sta->asid;

	if (sta->flags & WI_SIFLAGS_ASSOC) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_assoc_req: already assoc'ed?\n");
	}

	sta->flags |= WI_SIFLAGS_ASSOC;
	timeout_add_sec(&sta->tmo, whi->inactivity_time);
	status = IEEE80211_STATUS_SUCCESS;

fail:
	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		printf("wihap_assoc_req: returns status=0x%x\n", status);

	/* Send response. */
	resp_hdr = (struct wi_80211_hdr *)&sc->wi_txbuf;
	bzero(resp_hdr, sizeof(struct wi_80211_hdr));
	resp_hdr->frame_ctl = htole16(WI_FTYPE_MGMT | WI_STYPE_MGMT_ASRESP);
	pkt = (caddr_t)&sc->wi_txbuf + sizeof(struct wi_80211_hdr);

	bcopy(rxfrm->wi_addr2, resp_hdr->addr1, ETHER_ADDR_LEN);
	bcopy(sc->sc_ic.ic_myaddr, resp_hdr->addr2, IEEE80211_ADDR_LEN);
	bcopy(sc->sc_ic.ic_myaddr, resp_hdr->addr3, IEEE80211_ADDR_LEN);

	put_hword(&pkt, capinfo);
	put_hword(&pkt, status);
	put_hword(&pkt, asid);
	rates_len = put_rates(&pkt, sc->wi_supprates);

	wi_mgmt_xmit(sc, (caddr_t)&sc->wi_txbuf,
	    8 + rates_len + sizeof(struct wi_80211_hdr));
}

/* wihap_deauth_req()
 *
 *	Handle deauthentication requests.  Delete the station.
 */
void
wihap_deauth_req(struct wi_softc *sc, struct wi_frame *rxfrm,
		 caddr_t pkt, int len)
{
	struct wihap_info	*whi = &sc->wi_hostap_info;
	struct wihap_sta_info	*sta;
	u_int16_t		reason;

	if (len<2)
		return;

	reason = take_hword(&pkt, &len);

	sta = wihap_sta_find(whi, rxfrm->wi_addr2);
	if (sta == NULL) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_deauth_req: unknown station: %s\n",
			    ether_sprintf(rxfrm->wi_addr2));
	}
	else
		wihap_sta_delete(sta);
}

/* wihap_disassoc_req()
 *
 *	Handle disassociation requests.  Just reset the assoc flag.
 *	We'll free up the station resources when we get a deauth
 *	request or when it times out.
 */
void
wihap_disassoc_req(struct wi_softc *sc, struct wi_frame *rxfrm,
    caddr_t pkt, int len)
{
	struct wihap_info	*whi = &sc->wi_hostap_info;
	struct wihap_sta_info	*sta;
	u_int16_t		reason;

	if (len < 2)
		return;

	reason = take_hword(&pkt, &len);

	sta = wihap_sta_find(whi, rxfrm->wi_addr2);
	if (sta == NULL) {
		if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
			printf("wihap_disassoc_req: unknown station: %s\n",
			    ether_sprintf(rxfrm->wi_addr2));
	}
	else if (!(sta->flags & WI_SIFLAGS_AUTHEN)) {
		/*
		 * If station is not authenticated, send deauthentication
		 * frame.
		 */
		wihap_sta_deauth(sc, rxfrm->wi_addr2,
		    IEEE80211_REASON_NOT_AUTHED);
		return;
	}
	else
		sta->flags &= ~WI_SIFLAGS_ASSOC;
}

/* wihap_debug_frame_type()
 *
 * Print out frame type.  Used in early debugging.
 */
static __inline void
wihap_debug_frame_type(struct wi_frame *rxfrm)
{
	printf("wihap_mgmt_input: len=%d ", letoh16(rxfrm->wi_dat_len));

	if ((rxfrm->wi_frame_ctl & htole16(WI_FCTL_FTYPE)) ==
	    htole16(WI_FTYPE_MGMT)) {

		printf("MGMT: ");

		switch (letoh16(rxfrm->wi_frame_ctl) & WI_FCTL_STYPE) {
		case WI_STYPE_MGMT_ASREQ:
			printf("assoc req: \n");
			break;
		case WI_STYPE_MGMT_ASRESP:
			printf("assoc resp: \n");
			break;
		case WI_STYPE_MGMT_REASREQ:
			printf("reassoc req: \n");
			break;
		case WI_STYPE_MGMT_REASRESP:
			printf("reassoc resp: \n");
			break;
		case WI_STYPE_MGMT_PROBEREQ:
			printf("probe req: \n");
			break;
		case WI_STYPE_MGMT_PROBERESP:
			printf("probe resp: \n");
			break;
		case WI_STYPE_MGMT_BEACON:
			printf("beacon: \n");
			break;
		case WI_STYPE_MGMT_ATIM:
			printf("ann traf ind \n");
			break;
		case WI_STYPE_MGMT_DISAS:
			printf("disassociation: \n");
			break;
		case WI_STYPE_MGMT_AUTH:
			printf("auth: \n");
			break;
		case WI_STYPE_MGMT_DEAUTH:
			printf("deauth: \n");
			break;
		default:
			printf("unknown (stype=0x%x)\n",
			    letoh16(rxfrm->wi_frame_ctl) & WI_FCTL_STYPE);
		}

	}
	else {
		printf("ftype=0x%x (ctl=0x%x)\n",
		    letoh16(rxfrm->wi_frame_ctl) & WI_FCTL_FTYPE,
		    letoh16(rxfrm->wi_frame_ctl));
	}
}

/*
 * wihap_mgmt_input:
 *
 *	Called for each management frame received in host ap mode.
 *	wihap_mgmt_input() is expected to free the mbuf.
 */
void
wihap_mgmt_input(struct wi_softc *sc, struct wi_frame *rxfrm, struct mbuf *m)
{
	caddr_t	pkt;
	int	s, len;

	if (sc->sc_ic.ic_if.if_flags & IFF_DEBUG)
		wihap_debug_frame_type(rxfrm);

	pkt = mtod(m, caddr_t) + WI_802_11_OFFSET_RAW;
	len = m->m_len - WI_802_11_OFFSET_RAW;

	if ((rxfrm->wi_frame_ctl & htole16(WI_FCTL_FTYPE)) ==
	    htole16(WI_FTYPE_MGMT)) {

		/* any of the following will mess w/ the station list */
		s = splsoftclock();
		switch (letoh16(rxfrm->wi_frame_ctl) & WI_FCTL_STYPE) {
		case WI_STYPE_MGMT_ASREQ:
			wihap_assoc_req(sc, rxfrm, pkt, len);
			break;
		case WI_STYPE_MGMT_ASRESP:
			break;
		case WI_STYPE_MGMT_REASREQ:
			wihap_assoc_req(sc, rxfrm, pkt, len);
			break;
		case WI_STYPE_MGMT_REASRESP:
			break;
		case WI_STYPE_MGMT_PROBEREQ:
			break;
		case WI_STYPE_MGMT_PROBERESP:
			break;
		case WI_STYPE_MGMT_BEACON:
			break;
		case WI_STYPE_MGMT_ATIM:
			break;
		case WI_STYPE_MGMT_DISAS:
			wihap_disassoc_req(sc, rxfrm, pkt, len);
			break;
		case WI_STYPE_MGMT_AUTH:
			wihap_auth_req(sc, rxfrm, pkt, len);
			break;
		case WI_STYPE_MGMT_DEAUTH:
			wihap_deauth_req(sc, rxfrm, pkt, len);
			break;
		}
		splx(s);
	}

	m_freem(m);
}

/* wihap_sta_is_assoc()
 *
 *	Determine if a station is assoc'ed.  Update its activity
 *	counter as a side-effect.
 */
int
wihap_sta_is_assoc(struct wihap_info *whi, u_int8_t addr[])
{
	struct wihap_sta_info *sta;

	sta = wihap_sta_find(whi, addr);
	if (sta != NULL && (sta->flags & WI_SIFLAGS_ASSOC)) {
		/* Keep it active. */
		timeout_add_sec(&sta->tmo, whi->inactivity_time);
		return (1);
	}

	return (0);
}

/* wihap_check_tx()
 *
 *	Determine if a station is assoc'ed, get its tx rate, and update
 *	its activity.
 */
int
wihap_check_tx(struct wihap_info *whi, u_int8_t addr[], u_int8_t *txrate)
{
	struct wihap_sta_info *sta;
	static u_int8_t txratetable[] = { 10, 20, 55, 110 };
	int s;

	if (addr[0] & 0x01) {
		*txrate = 0; /* XXX: multicast rate? */
		return (1);
	}

	s = splsoftclock();
	sta = wihap_sta_find(whi, addr);
	if (sta != NULL && (sta->flags & WI_SIFLAGS_ASSOC)) {
		/* Keep it active. */
		timeout_add_sec(&sta->tmo, whi->inactivity_time);
		*txrate = txratetable[sta->tx_curr_rate];
		splx(s);
		return (1);
	}
	splx(s);

	return (0);
}

/*
 * wihap_data_input()
 *
 *	Handle all data input on interface when in Host AP mode.
 *	Some packets are destined for this machine, others are
 *	repeated to other stations.
 *
 *	If wihap_data_input() returns a non-zero, it has processed
 *	the packet and will free the mbuf.
 */
int
wihap_data_input(struct wi_softc *sc, struct wi_frame *rxfrm, struct mbuf *m)
{
	struct ifnet		*ifp = &sc->sc_ic.ic_if;
	struct wihap_info	*whi = &sc->wi_hostap_info;
	struct wihap_sta_info	*sta;
	int			mcast, s;
	u_int16_t		fctl;

	/*
	 * TODS flag must be set.  However, Lucent cards set NULLFUNC but
	 * not TODS when probing an AP to see if it is alive after it has
	 * been down for a while.  We accept these probe packets and send a
	 * disassoc packet later on if the station is not already associated.
	 */
	fctl = letoh16(rxfrm->wi_frame_ctl);
	if (!(fctl & WI_FCTL_TODS) && !(fctl & WI_STYPE_NULLFUNC)) {
		if (ifp->if_flags & IFF_DEBUG)
			printf("wihap_data_input: no TODS src=%s, fctl=0x%x\n",
			    ether_sprintf(rxfrm->wi_addr2), fctl);
		m_freem(m);
		return (1);
	}

	/* Check BSSID. (Is this necessary?) */
	if (!addr_cmp(rxfrm->wi_addr1, sc->sc_ic.ic_myaddr)) {
		if (ifp->if_flags & IFF_DEBUG)
			printf("wihap_data_input: incorrect bss: %s\n",
			    ether_sprintf(rxfrm->wi_addr1));
		m_freem(m);
		return (1);
	}

	s = splsoftclock();

	/* Find source station. */
	sta = wihap_sta_find(whi, rxfrm->wi_addr2);

	/* Source station must be associated. */
	if (sta == NULL || !(sta->flags & WI_SIFLAGS_ASSOC)) {
		if (ifp->if_flags & IFF_DEBUG)
			printf("wihap_data_input: dropping unassoc src %s\n",
			    ether_sprintf(rxfrm->wi_addr2));
		wihap_sta_disassoc(sc, rxfrm->wi_addr2,
		    IEEE80211_REASON_ASSOC_LEAVE);
		splx(s);
		m_freem(m);
		return (1);
	}

	timeout_add_sec(&sta->tmo, whi->inactivity_time);
	sta->sig_info = letoh16(rxfrm->wi_q_info);

	splx(s);

	/* Repeat this packet to BSS? */
	mcast = (rxfrm->wi_addr3[0] & 0x01) != 0;
	if (mcast || wihap_sta_is_assoc(whi, rxfrm->wi_addr3)) {

		/* If it's multicast, make a copy.
		 */
		if (mcast) {
			m = m_copym(m, 0, M_COPYALL, M_DONTWAIT);
			if (m == NULL)
				return (0);
			m->m_flags |= M_MCAST; /* XXX */
		}

		/* Queue up for repeating.
		 */
		if_enqueue(ifp, m);
		return (!mcast);
	}

	return (0);
}

/* wihap_ioctl()
 *
 *	Handle Host AP specific ioctls.  Called from wi_ioctl().
 */
int
wihap_ioctl(struct wi_softc *sc, u_long command, caddr_t data)
{
	struct proc		*p = curproc;
	struct ifreq		*ifr = (struct ifreq *) data;
	struct wihap_info	*whi = &sc->wi_hostap_info;
	struct wihap_sta_info	*sta;
	struct hostap_getall	reqall;
	struct hostap_sta	reqsta;
	struct hostap_sta	stabuf;
	int			s, error = 0, n, flag;

	struct ieee80211_nodereq nr;
	struct ieee80211_nodereq_all *na;

	if (!(sc->sc_ic.ic_if.if_flags & IFF_RUNNING))
		return ENODEV;

	switch (command) {
	case SIOCHOSTAP_DEL:
		if ((error = suser(p)))
			break;
		if ((error = copyin(ifr->ifr_data, &reqsta, sizeof(reqsta))))
			break;
		s = splnet();
		sta = wihap_sta_find(whi, reqsta.addr);
		if (sta == NULL)
			error = ENOENT;
		else {
			/* Disassociate station. */
			if (sta->flags & WI_SIFLAGS_ASSOC)
				wihap_sta_disassoc(sc, sta->addr,
				    IEEE80211_REASON_ASSOC_LEAVE);
			/* Deauth station. */
			if (sta->flags & WI_SIFLAGS_AUTHEN)
				wihap_sta_deauth(sc, sta->addr,
				    IEEE80211_REASON_AUTH_LEAVE);

			wihap_sta_delete(sta);
		}
		splx(s);
		break;

	case SIOCHOSTAP_GET:
		if ((error = copyin(ifr->ifr_data, &reqsta, sizeof(reqsta))))
			break;
		s = splnet();
		sta = wihap_sta_find(whi, reqsta.addr);
		if (sta == NULL)
			error = ENOENT;
		else {
			reqsta.flags = sta->flags;
			reqsta.asid = sta->asid;
			reqsta.capinfo = sta->capinfo;
			reqsta.sig_info = sta->sig_info;
			reqsta.rates = sta->rates;

			error = copyout(&reqsta, ifr->ifr_data,
			    sizeof(reqsta));
		}
		splx(s);
		break;

	case SIOCHOSTAP_ADD:
		if ((error = suser(p)))
			break;
		if ((error = copyin(ifr->ifr_data, &reqsta, sizeof(reqsta))))
			break;
		s = splnet();
		sta = wihap_sta_find(whi, reqsta.addr);
		if (sta != NULL) {
			error = EEXIST;
			splx(s);
			break;
		}
		if (whi->n_stations >= WIHAP_MAX_STATIONS) {
			error = ENOSPC;
			splx(s);
			break;
		}
		sta = wihap_sta_alloc(sc, reqsta.addr);
		sta->flags = reqsta.flags;
		timeout_add_sec(&sta->tmo, whi->inactivity_time);
		splx(s);
		break;

	case SIOCHOSTAP_SFLAGS:
		if ((error = suser(p)))
			break;
		if ((error = copyin(ifr->ifr_data, &flag, sizeof(int))))
			break;

		whi->apflags = (whi->apflags & WIHAPFL_CANTCHANGE) |
		    (flag & ~WIHAPFL_CANTCHANGE);
		break;

	case SIOCHOSTAP_GFLAGS:
		flag = (int) whi->apflags;
		error = copyout(&flag, ifr->ifr_data, sizeof(int));
		break;

	case SIOCHOSTAP_GETALL:
		if ((error = copyin(ifr->ifr_data, &reqall, sizeof(reqall))))
			break;

		reqall.nstations = whi->n_stations;
		n = 0;
		s = splnet();
		sta = TAILQ_FIRST(&whi->sta_list);
		while (sta && reqall.size >= n+sizeof(struct hostap_sta)) {

			bcopy(sta->addr, stabuf.addr, ETHER_ADDR_LEN);
			stabuf.asid = sta->asid;
			stabuf.flags = sta->flags;
			stabuf.capinfo = sta->capinfo;
			stabuf.sig_info = sta->sig_info;
			stabuf.rates = sta->rates;

			error = copyout(&stabuf, (caddr_t) reqall.addr + n,
			    sizeof(struct hostap_sta));
			if (error)
				break;

			sta = TAILQ_NEXT(sta, list);
			n += sizeof(struct hostap_sta);
		}
		splx(s);

		if (!error)
			error = copyout(&reqall, ifr->ifr_data,
			    sizeof(reqall));
		break;

	case SIOCG80211ALLNODES:
		na = (struct ieee80211_nodereq_all *)data;
		na->na_nodes = n = 0;
		s = splnet();
		sta = TAILQ_FIRST(&whi->sta_list);
		while (sta && na->na_size >=
		    n + sizeof(struct ieee80211_nodereq)) {
			bzero(&nr, sizeof(nr));
			IEEE80211_ADDR_COPY(nr.nr_macaddr, sta->addr);
			IEEE80211_ADDR_COPY(nr.nr_bssid,
			    &sc->sc_ic.ic_myaddr);
			nr.nr_channel = sc->wi_channel;
			nr.nr_chan_flags = IEEE80211_CHAN_B;
			nr.nr_associd = sta->asid;
			nr.nr_rssi = sta->sig_info >> 8;
			nr.nr_max_rssi = 0;
			nr.nr_capinfo = sta->capinfo;
			nr.nr_nrates = 0;
			if (sta->rates & WI_SUPPRATES_1M)
				nr.nr_rates[nr.nr_nrates++] = 2;
			if (sta->rates & WI_SUPPRATES_2M)
				nr.nr_rates[nr.nr_nrates++] = 4;
			if (sta->rates & WI_SUPPRATES_5M)
				nr.nr_rates[nr.nr_nrates++] = 11;
			if (sta->rates & WI_SUPPRATES_11M)
				nr.nr_rates[nr.nr_nrates++] = 22;

			error = copyout(&nr, (caddr_t)na->na_node + n,
			    sizeof(struct ieee80211_nodereq));
			if (error)
				break;
			n += sizeof(struct ieee80211_nodereq);
			na->na_nodes++;
			sta = TAILQ_NEXT(sta, list);
		}
		splx(s);
		break;

	default:
		printf("wihap_ioctl: i shouldn't get other ioctls!\n");
		error = EINVAL;
	}

	return (error);
}

#else
void
wihap_init(struct wi_softc *sc)
{
	return;
}

void
wihap_shutdown(struct wi_softc *sc)
{
	return;
}

void
wihap_mgmt_input(struct wi_softc *sc, struct wi_frame *rxfrm, struct mbuf *m)
{
	return;
}

int
wihap_data_input(struct wi_softc *sc, struct wi_frame *rxfrm, struct mbuf *m)
{
	return (0);
}

int
wihap_ioctl(struct wi_softc *sc, u_long command, caddr_t data)
{
	return (EINVAL);
}

int
wihap_check_tx(struct wihap_info *whi, u_int8_t addr[], u_int8_t *txrate)
{
	return (0);
}
#endif
