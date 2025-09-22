/*	$OpenBSD: lsupdate.c,v 1.24 2023/07/03 09:51:38 claudio Exp $ */

/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005, 2007 Esben Norby <norby@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/ip_ah.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <string.h>
#include <siphash.h>

#include "ospf6.h"
#include "ospf6d.h"
#include "log.h"
#include "ospfe.h"
#include "rde.h"

struct ibuf	*prepare_ls_update(struct iface *, int);
int		 add_ls_update(struct ibuf *, struct iface *, void *, u_int16_t,
		    u_int16_t);
int		 send_ls_update(struct ibuf *, struct iface *, struct in6_addr,
		    u_int32_t);

void		 ls_retrans_list_insert(struct nbr *, struct lsa_entry *);
void		 ls_retrans_list_remove(struct nbr *, struct lsa_entry *);

/* link state update packet handling */
int
lsa_flood(struct iface *iface, struct nbr *originator, struct lsa_hdr *lsa_hdr,
    void *data)
{
	struct nbr		*nbr;
	struct lsa_entry	*le = NULL;
	int			 queued = 0, dont_ack = 0;
	int			 r;

	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr == iface->self)
			continue;
		if (!(nbr->state & NBR_STA_FLOOD))
			continue;

		if (iface->state & IF_STA_DROTHER && !queued)
			while ((le = ls_retrans_list_get(iface->self, lsa_hdr)))
			    ls_retrans_list_free(iface->self, le);

		while ((le = ls_retrans_list_get(nbr, lsa_hdr)))
			ls_retrans_list_free(nbr, le);

		if (!(nbr->state & NBR_STA_FULL) &&
		    (le = ls_req_list_get(nbr, lsa_hdr)) != NULL) {
			r = lsa_newer(lsa_hdr, le->le_lsa);
			if (r > 0) {
				/* to flood LSA is newer than requested */
				ls_req_list_free(nbr, le);
				/* new needs to be flooded */
			} else if (r < 0) {
				/* to flood LSA is older than requested */
				continue;
			} else {
				/* LSA are equal */
				ls_req_list_free(nbr, le);
				continue;
			}
		}

		if (nbr == originator) {
			dont_ack++;
			continue;
		}

		/* non DR or BDR router keep all lsa in one retrans list */
		if (iface->state & IF_STA_DROTHER) {
			if (!queued)
				ls_retrans_list_add(iface->self, data,
				    iface->rxmt_interval, 0);
			queued = 1;
		} else {
			ls_retrans_list_add(nbr, data, iface->rxmt_interval, 0);
			queued = 1;
		}
	}

	if (!queued)
		return (0);

	if (iface == originator->iface && iface->self != originator) {
		if (iface->dr == originator || iface->bdr == originator)
			return (0);
		if (iface->state & IF_STA_BACKUP)
			return (0);
		dont_ack++;
	}

	/*
	 * initial flood needs to be queued separately, timeout is zero
	 * and oneshot has to be set because the retransimssion queues
	 * are already loaded.
	 */
	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		ls_retrans_list_add(iface->self, data, 0, 1);
		break;
	case IF_TYPE_NBMA:
	case IF_TYPE_POINTOMULTIPOINT:
	case IF_TYPE_VIRTUALLINK:
		LIST_FOREACH(nbr, &iface->nbr_list, entry) {
			if (nbr == iface->self)
				continue;
			if (!(nbr->state & NBR_STA_FLOOD))
				continue;
			if (!TAILQ_EMPTY(&nbr->ls_retrans_list)) {
				le = TAILQ_LAST(&nbr->ls_retrans_list,
				    lsa_head);
				if (lsa_hdr->type != le->le_lsa->type ||
				    lsa_hdr->ls_id != le->le_lsa->ls_id ||
				    lsa_hdr->adv_rtr != le->le_lsa->adv_rtr)
					continue;
			}
			ls_retrans_list_add(nbr, data, 0, 1);
		}
		break;
	default:
		fatalx("lsa_flood: unknown interface type");
	}

	return (dont_ack == 2);
}

struct ibuf *
prepare_ls_update(struct iface *iface, int bigpkt)
{
	struct ibuf		*buf;
	size_t			 size;

	size = bigpkt ? IPV6_MAXPACKET : iface->mtu;
	if (size < IPV6_MMTU)
		size = IPV6_MMTU;
	size -= sizeof(struct ip6_hdr);

	/*
	 * Reserve space for optional ah or esp encryption.  The
	 * algorithm is taken from ah_output and esp_output, the
	 * values are the maxima of crypto/xform.c.
	 */
	size -= max(
	    /* base-ah-header replay authsize */
	    AH_FLENGTH + sizeof(u_int32_t) + 32,
	    /* spi sequence ivlen blocksize pad-length next-header authsize */
	    2 * sizeof(u_int32_t) + 16 + 16 + 2 * sizeof(u_int8_t) + 32);

	if ((buf = ibuf_open(size)) == NULL)
		fatal("prepare_ls_update");

	/* OSPF header */
	if (gen_ospf_hdr(buf, iface, PACKET_TYPE_LS_UPDATE))
		goto fail;

	/* reserve space for number of lsa field */
	if (ibuf_add_zero(buf, sizeof(u_int32_t)) == -1)
		goto fail;

	return (buf);
fail:
	log_warn("prepare_ls_update");
	ibuf_free(buf);
	return (NULL);
}

int
add_ls_update(struct ibuf *buf, struct iface *iface, void *data, u_int16_t len,
    u_int16_t older)
{
	size_t		ageoff;
	u_int16_t	age;

	if (len >= ibuf_left(buf))
		return (0);

	ageoff = ibuf_size(buf);
	if (ibuf_add(buf, data, len)) {
		log_warn("add_ls_update");
		return (0);
	}

	/* age LSA before sending it out */
	memcpy(&age, data, sizeof(age));
	age = ntohs(age);
	if ((age += older + iface->transmit_delay) >= MAX_AGE)
		age = MAX_AGE;
	if (ibuf_set_n16(buf, ageoff, age) == -1) {
		log_warn("add_ls_update");
		return (0);
	}

	return (1);
}

int
send_ls_update(struct ibuf *buf, struct iface *iface, struct in6_addr addr,
    u_int32_t nlsa)
{
	if (ibuf_set_n32(buf, sizeof(struct ospf_hdr), nlsa) == -1)
		goto fail;
	/* calculate checksum */
	if (upd_ospf_hdr(buf, iface))
		goto fail;

	if (send_packet(iface, buf, &addr) == -1)
		goto fail;

	ibuf_free(buf);
	return (0);
fail:
	log_warn("send_ls_update");
	ibuf_free(buf);
	return (-1);
}

void
recv_ls_update(struct nbr *nbr, char *buf, u_int16_t len)
{
	struct lsa_hdr		 lsa;
	u_int32_t		 nlsa;

	if (len < sizeof(nlsa)) {
		log_warnx("recv_ls_update: bad packet size, "
		    "neighbor ID %s (%s)", inet_ntoa(nbr->id),
		    nbr->iface->name);
		return;
	}
	memcpy(&nlsa, buf, sizeof(nlsa));
	nlsa = ntohl(nlsa);
	buf += sizeof(nlsa);
	len -= sizeof(nlsa);

	switch (nbr->state) {
	case NBR_STA_DOWN:
	case NBR_STA_ATTEMPT:
	case NBR_STA_INIT:
	case NBR_STA_2_WAY:
	case NBR_STA_XSTRT:
	case NBR_STA_SNAP:
		log_debug("recv_ls_update: packet ignored in state %s, "
		    "neighbor ID %s (%s)", nbr_state_name(nbr->state),
		    inet_ntoa(nbr->id), nbr->iface->name);
		break;
	case NBR_STA_XCHNG:
	case NBR_STA_LOAD:
	case NBR_STA_FULL:
		for (; nlsa > 0 && len > 0; nlsa--) {
			if (len < sizeof(lsa)) {
				log_warnx("recv_ls_update: bad packet size, "
				    "neighbor ID %s (%s)", inet_ntoa(nbr->id),
				    nbr->iface->name);
				return;
			}
			memcpy(&lsa, buf, sizeof(lsa));
			if (len < ntohs(lsa.len)) {
				log_warnx("recv_ls_update: bad packet size, "
				    "neighbor ID %s (%s)", inet_ntoa(nbr->id),
				    nbr->iface->name);
				return;
			}
			ospfe_imsg_compose_rde(IMSG_LS_UPD, nbr->peerid, 0,
			    buf, ntohs(lsa.len));
			buf += ntohs(lsa.len);
			len -= ntohs(lsa.len);
		}
		if (nlsa > 0 || len > 0) {
			log_warnx("recv_ls_update: bad packet size, "
			    "neighbor ID %s (%s)", inet_ntoa(nbr->id),
			    nbr->iface->name);
			return;
		}
		break;
	default:
		fatalx("recv_ls_update: unknown neighbor state");
	}
}

/* link state retransmit list */
void
ls_retrans_list_add(struct nbr *nbr, struct lsa_hdr *lsa,
    unsigned short timeout, unsigned short oneshot)
{
	struct timeval		 tv;
	struct lsa_entry	*le;
	struct lsa_ref		*ref;

	if ((ref = lsa_cache_get(lsa)) == NULL)
		fatalx("King Bula sez: somebody forgot to lsa_cache_add");

	if ((le = calloc(1, sizeof(*le))) == NULL)
		fatal("ls_retrans_list_add");

	le->le_ref = ref;
	le->le_when = timeout;
	le->le_oneshot = oneshot;

	ls_retrans_list_insert(nbr, le);

	if (!evtimer_pending(&nbr->ls_retrans_timer, NULL)) {
		timerclear(&tv);
		tv.tv_sec = TAILQ_FIRST(&nbr->ls_retrans_list)->le_when;

		if (evtimer_add(&nbr->ls_retrans_timer, &tv) == -1)
			fatal("ls_retrans_list_add");
	}
}

int
ls_retrans_list_del(struct nbr *nbr, struct lsa_hdr *lsa_hdr)
{
	struct lsa_entry	*le;

	if ((le = ls_retrans_list_get(nbr, lsa_hdr)) == NULL)
		return (-1);
	/*
	 * Compare LSA with the Ack by comparing not only the seq_num and
	 * checksum but also the age field.  Since we only care about MAX_AGE
	 * vs. non-MAX_AGE LSA, a simple >= comparison is good enough.  This
	 * ensures that a LSA withdrawal is not acked by a previous update.
	 */
	if (lsa_hdr->seq_num == le->le_ref->hdr.seq_num &&
	    lsa_hdr->ls_chksum == le->le_ref->hdr.ls_chksum &&
	    ntohs(lsa_hdr->age) >= ntohs(le->le_ref->hdr.age)) {
		ls_retrans_list_free(nbr, le);
		return (0);
	}

	return (-1);
}

struct lsa_entry *
ls_retrans_list_get(struct nbr *nbr, struct lsa_hdr *lsa_hdr)
{
	struct lsa_entry	*le;

	TAILQ_FOREACH(le, &nbr->ls_retrans_list, entry) {
		if ((lsa_hdr->type == le->le_ref->hdr.type) &&
		    (lsa_hdr->ls_id == le->le_ref->hdr.ls_id) &&
		    (lsa_hdr->adv_rtr == le->le_ref->hdr.adv_rtr))
			return (le);
	}
	return (NULL);
}

void
ls_retrans_list_insert(struct nbr *nbr, struct lsa_entry *new)
{
	struct lsa_entry	*le;
	unsigned short		 when = new->le_when;

	TAILQ_FOREACH(le, &nbr->ls_retrans_list, entry) {
		if (when < le->le_when) {
			new->le_when = when;
			TAILQ_INSERT_BEFORE(le, new, entry);
			nbr->ls_ret_cnt++;
			return;
		}
		when -= le->le_when;
	}
	new->le_when = when;
	TAILQ_INSERT_TAIL(&nbr->ls_retrans_list, new, entry);
	nbr->ls_ret_cnt++;
}

void
ls_retrans_list_remove(struct nbr *nbr, struct lsa_entry *le)
{
	struct timeval		 tv;
	struct lsa_entry	*next = TAILQ_NEXT(le, entry);
	int			 reset = 0;

	/* adjust timeout of next entry */
	if (next)
		next->le_when += le->le_when;

	if (TAILQ_FIRST(&nbr->ls_retrans_list) == le &&
	    evtimer_pending(&nbr->ls_retrans_timer, NULL))
		reset = 1;

	TAILQ_REMOVE(&nbr->ls_retrans_list, le, entry);
	nbr->ls_ret_cnt--;

	if (reset && TAILQ_FIRST(&nbr->ls_retrans_list)) {
		if (evtimer_del(&nbr->ls_retrans_timer) == -1)
			fatal("ls_retrans_list_remove");

		timerclear(&tv);
		tv.tv_sec = TAILQ_FIRST(&nbr->ls_retrans_list)->le_when;

		if (evtimer_add(&nbr->ls_retrans_timer, &tv) == -1)
			fatal("ls_retrans_list_remove");
	}
}

void
ls_retrans_list_free(struct nbr *nbr, struct lsa_entry *le)
{
	ls_retrans_list_remove(nbr, le);

	lsa_cache_put(le->le_ref, nbr);
	free(le);
}

void
ls_retrans_list_clr(struct nbr *nbr)
{
	struct lsa_entry	*le;

	while ((le = TAILQ_FIRST(&nbr->ls_retrans_list)) != NULL)
		ls_retrans_list_free(nbr, le);

	nbr->ls_ret_cnt = 0;
}

void
ls_retrans_timer(int fd, short event, void *bula)
{
	struct timeval		 tv;
	struct timespec		 tp;
	struct in6_addr		 addr;
	struct nbr		*nbr = bula;
	struct lsa_entry	*le;
	struct ibuf		*buf;
	time_t			 now;
	int			 d, bigpkt;
	u_int32_t		 nlsa = 0;

	if ((le = TAILQ_FIRST(&nbr->ls_retrans_list)) != NULL)
		le->le_when = 0;	/* timer fired */
	else
		return;			/* queue empty, nothing to do */

	clock_gettime(CLOCK_MONOTONIC, &tp);
	now = tp.tv_sec;

	if (nbr->iface->self == nbr) {
		/*
		 * oneshot needs to be set for lsa queued for flooding,
		 * if oneshot is not set then the lsa needs to be converted
		 * because the router switched lately to DR or BDR
		 */
		if (le->le_oneshot && nbr->iface->state & IF_STA_DRORBDR)
			inet_pton(AF_INET6, AllSPFRouters, &addr);
		else if (nbr->iface->state & IF_STA_DRORBDR) {
			/*
			 * old retransmission needs to be converted into
			 * flood by rerunning the lsa_flood.
			 */
			lsa_flood(nbr->iface, nbr, &le->le_ref->hdr,
			    le->le_ref->data);
			ls_retrans_list_free(nbr, le);
			/* ls_retrans_list_free retriggers the timer */
			return;
		} else if (nbr->iface->type == IF_TYPE_POINTOPOINT)
			memcpy(&addr, &nbr->addr, sizeof(addr));
		else
			inet_pton(AF_INET6, AllDRouters, &addr);
	} else
		memcpy(&addr, &nbr->addr, sizeof(addr));

	bigpkt = le->le_ref->len > 1024;
	if ((buf = prepare_ls_update(nbr->iface, bigpkt)) == NULL) {
		le->le_when = 1;
		goto done;
	}

	while ((le = TAILQ_FIRST(&nbr->ls_retrans_list)) != NULL &&
	    le->le_when == 0) {
		d = now - le->le_ref->stamp;
		if (d < 0)
			d = 0;
		else if (d > MAX_AGE)
			d = MAX_AGE;

		if (add_ls_update(buf, nbr->iface, le->le_ref->data,
		    le->le_ref->len, d) == 0) {
			if (nlsa == 0) {
				/* something bad happened retry later */
				log_warnx("ls_retrans_timer: sending LS update "
				    "to neighbor ID %s (%s) failed",
				    inet_ntoa(nbr->id), nbr->iface->name);
				log_debug("ls_retrans_timer: type: %04x len: %u",
				    ntohs(le->le_ref->hdr.type),
				    le->le_ref->len);
				TAILQ_REMOVE(&nbr->ls_retrans_list, le, entry);
				nbr->ls_ret_cnt--;
				le->le_when = nbr->iface->rxmt_interval;
				ls_retrans_list_insert(nbr, le);
			}
			break;
		}
		nlsa++;
		if (le->le_oneshot)
			ls_retrans_list_free(nbr, le);
		else {
			TAILQ_REMOVE(&nbr->ls_retrans_list, le, entry);
			nbr->ls_ret_cnt--;
			le->le_when = nbr->iface->rxmt_interval;
			ls_retrans_list_insert(nbr, le);
		}
	}
	if (nlsa)
		send_ls_update(buf, nbr->iface, addr, nlsa);
	else
		ibuf_free(buf);

done:
	if ((le = TAILQ_FIRST(&nbr->ls_retrans_list)) != NULL) {
		timerclear(&tv);
		tv.tv_sec = le->le_when;

		if (evtimer_add(&nbr->ls_retrans_timer, &tv) == -1)
			fatal("ls_retrans_timer");
	}
}

LIST_HEAD(lsa_cache_head, lsa_ref);

struct lsa_cache {
	struct lsa_cache_head	*hashtbl;
	u_int32_t		 hashmask;
} lsacache;

SIPHASH_KEY lsacachekey;

struct lsa_ref		*lsa_cache_look(struct lsa_hdr *);

void
lsa_cache_init(u_int32_t hashsize)
{
	u_int32_t        hs, i;

	for (hs = 1; hs < hashsize; hs <<= 1)
		;
	lsacache.hashtbl = calloc(hs, sizeof(struct lsa_cache_head));
	if (lsacache.hashtbl == NULL)
		fatal("lsa_cache_init");

	for (i = 0; i < hs; i++)
		LIST_INIT(&lsacache.hashtbl[i]);
	arc4random_buf(&lsacachekey, sizeof(lsacachekey));

	lsacache.hashmask = hs - 1;
}

static uint32_t
lsa_hash_hdr(const struct lsa_hdr *hdr)
{
	return SipHash24(&lsacachekey, hdr, sizeof(*hdr));
}

struct lsa_ref *
lsa_cache_add(void *data, u_int16_t len)
{
	struct lsa_cache_head	*head;
	struct lsa_ref		*ref, *old;
	struct timespec		 tp;

	if ((ref = calloc(1, sizeof(*ref))) == NULL)
		fatal("lsa_cache_add");
	memcpy(&ref->hdr, data, sizeof(ref->hdr));

	if ((old = lsa_cache_look(&ref->hdr))) {
		free(ref);
		old->refcnt++;
		return (old);
	}

	if ((ref->data = malloc(len)) == NULL)
		fatal("lsa_cache_add");
	memcpy(ref->data, data, len);

	clock_gettime(CLOCK_MONOTONIC, &tp);
	ref->stamp = tp.tv_sec;
	ref->len = len;
	ref->refcnt = 1;

	head = &lsacache.hashtbl[lsa_hash_hdr(&ref->hdr) & lsacache.hashmask];
	LIST_INSERT_HEAD(head, ref, entry);
	return (ref);
}

struct lsa_ref *
lsa_cache_get(struct lsa_hdr *lsa_hdr)
{
	struct lsa_ref		*ref;

	ref = lsa_cache_look(lsa_hdr);
	if (ref)
		ref->refcnt++;

	return (ref);
}

void
lsa_cache_put(struct lsa_ref *ref, struct nbr *nbr)
{
	if (--ref->refcnt > 0)
		return;

	if (ntohs(ref->hdr.age) >= MAX_AGE)
		ospfe_imsg_compose_rde(IMSG_LS_MAXAGE, nbr->peerid, 0,
		    ref->data, sizeof(struct lsa_hdr));

	free(ref->data);
	LIST_REMOVE(ref, entry);
	free(ref);
}

struct lsa_ref *
lsa_cache_look(struct lsa_hdr *lsa_hdr)
{
	struct lsa_cache_head	*head;
	struct lsa_ref		*ref;

	head = &lsacache.hashtbl[lsa_hash_hdr(lsa_hdr) & lsacache.hashmask];

	LIST_FOREACH(ref, head, entry) {
		if (memcmp(&ref->hdr, lsa_hdr, sizeof(*lsa_hdr)) == 0)
			/* found match */
			return (ref);
	}

	return (NULL);
}
