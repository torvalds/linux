/*	$OpenBSD: pfkey.c,v 1.85 2024/07/13 12:22:46 yasuoka Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2004, 2005 Hans-Joerg Hoexer <hshoexer@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2003, 2004 Markus Friedl <markus@openbsd.org>
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

#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip_ipsp.h>
#include <net/if.h>
#include <net/pfkeyv2.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <event.h>

#include "iked.h"
#include "ikev2.h"

#define ROUNDUP(x) (((x) + (PFKEYV2_CHUNK - 1)) & ~(PFKEYV2_CHUNK - 1))
#define IOV_CNT 28

#define PFKEYV2_CHUNK sizeof(uint64_t)
#define PFKEY_REPLY_TIMEOUT 1000

/* only used internally */
#define IKED_SADB_UPDATE_SA_ADDRESSES 0xff

static uint32_t sadb_msg_seq = 0;
static unsigned int sadb_decoupled = 0;

static int iked_rdomain = 0;

static struct event pfkey_timer_ev;
static struct timeval pfkey_timer_tv;

struct pfkey_message {
	SIMPLEQ_ENTRY(pfkey_message)
			 pm_entry;
	uint8_t		*pm_data;
	ssize_t		 pm_length;
};
SIMPLEQ_HEAD(, pfkey_message) pfkey_retry, pfkey_postponed =
    SIMPLEQ_HEAD_INITIALIZER(pfkey_postponed);

struct pfkey_constmap {
	uint8_t		 pfkey_id;
	unsigned int	 pfkey_ikeid;
	unsigned int	 pfkey_fixedkey;
};

static const struct pfkey_constmap pfkey_encr[] = {
	{ SADB_EALG_3DESCBC,	IKEV2_XFORMENCR_3DES },
	{ SADB_X_EALG_CAST,	IKEV2_XFORMENCR_CAST },
	{ SADB_X_EALG_BLF,	IKEV2_XFORMENCR_BLOWFISH },
	{ SADB_EALG_NULL,	IKEV2_XFORMENCR_NULL },
	{ SADB_X_EALG_AES,	IKEV2_XFORMENCR_AES_CBC },
	{ SADB_X_EALG_AESCTR,	IKEV2_XFORMENCR_AES_CTR },
	{ SADB_X_EALG_AESGCM16,	IKEV2_XFORMENCR_AES_GCM_16 },
	{ SADB_X_EALG_AESGMAC,	IKEV2_XFORMENCR_NULL_AES_GMAC },
	{ SADB_X_EALG_CHACHA20POLY1305, IKEV2_XFORMENCR_CHACHA20_POLY1305 },
	{ 0 }
};

static const struct pfkey_constmap pfkey_integr[] = {
	{ SADB_AALG_MD5HMAC,	IKEV2_XFORMAUTH_HMAC_MD5_96 },
	{ SADB_AALG_SHA1HMAC,	IKEV2_XFORMAUTH_HMAC_SHA1_96 },
	{ SADB_X_AALG_SHA2_256,	IKEV2_XFORMAUTH_HMAC_SHA2_256_128 },
	{ SADB_X_AALG_SHA2_384,	IKEV2_XFORMAUTH_HMAC_SHA2_384_192 },
	{ SADB_X_AALG_SHA2_512,	IKEV2_XFORMAUTH_HMAC_SHA2_512_256 },
	{ 0 }
};

static const struct pfkey_constmap pfkey_satype[] = {
	{ SADB_SATYPE_AH,	IKEV2_SAPROTO_AH },
	{ SADB_SATYPE_ESP,	IKEV2_SAPROTO_ESP },
	{ SADB_X_SATYPE_IPCOMP,	IKEV2_SAPROTO_IPCOMP },
	{ 0 }
};

int	pfkey_map(const struct pfkey_constmap *, uint16_t, uint8_t *);
int	pfkey_flow(struct iked *, uint8_t, uint8_t, struct iked_flow *);
int	pfkey_sa(struct iked *, uint8_t, uint8_t, struct iked_childsa *);
int	pfkey_sa_getspi(struct iked *, uint8_t, struct iked_childsa *, uint32_t *);
int	pfkey_sagroup(struct iked *, uint8_t, uint8_t,
	    struct iked_childsa *, struct iked_childsa *);
int	pfkey_write(struct iked *, struct sadb_msg *, struct iovec *, int,
	    uint8_t **, ssize_t *);
int	pfkey_reply(int, uint8_t **, ssize_t *);
void	pfkey_dispatch(int, short, void *);
int	pfkey_sa_lookup(struct iked *, struct iked_childsa *, uint64_t *,
	    struct iked_sastats *);
int	pfkey_sa_check_exists(struct iked *, struct iked_childsa *);
int	pfkey_sa_sastats(struct iked *, struct iked_childsa *,
	    struct iked_sastats *);

struct sadb_ident *
	pfkey_id2ident(struct iked_id *, unsigned int);
void	*pfkey_find_ext(uint8_t *, ssize_t, int);

void	pfkey_timer_cb(int, short, void *);
int	pfkey_process(struct iked *, struct pfkey_message *);

int
pfkey_couple(struct iked *env, struct iked_sas *sas, int couple)
{
	struct iked_sa		*sa;
	struct iked_flow	*flow;
	struct iked_childsa	*csa, *ipcomp;
	const char		*mode[] = { "coupled", "decoupled" };

	/* Socket is not ready */
	if (env->sc_pfkey == -1)
		return (-1);

	if (sadb_decoupled == !couple)
		return (0);

	log_debug("%s: kernel %s -> %s", __func__,
	    mode[sadb_decoupled], mode[!sadb_decoupled]);

	/* Allow writes to the PF_KEY socket */
	sadb_decoupled = 0;

	RB_FOREACH(sa, iked_sas, sas) {
		TAILQ_FOREACH(csa, &sa->sa_childsas, csa_entry) {
			if (!csa->csa_loaded && couple)
				(void)pfkey_sa_add(env, csa, NULL);
			else if (csa->csa_loaded && !couple)
				(void)pfkey_sa_delete(env, csa);
			if ((ipcomp = csa->csa_bundled) != NULL) {
				if (!ipcomp->csa_loaded && couple)
					(void)pfkey_sa_add(env, ipcomp, csa);
				else if (ipcomp->csa_loaded && !couple)
					(void)pfkey_sa_delete(env, ipcomp);
			}
		}
		TAILQ_FOREACH(flow, &sa->sa_flows, flow_entry) {
			if (!flow->flow_loaded && couple)
				(void)pfkey_flow_add(env, flow);
			else if (flow->flow_loaded && !couple)
				(void)pfkey_flow_delete(env, flow);
		}
	}

	sadb_decoupled = !couple;

	return (0);
}

int
pfkey_map(const struct pfkey_constmap *map, uint16_t alg, uint8_t *pfkalg)
{
	int	 i;

	for (i = 0; map[i].pfkey_id != 0; i++)
		if (map[i].pfkey_ikeid == alg) {
			*pfkalg = map[i].pfkey_id;
			return (0);
		}
	return (-1);
}

int
pfkey_flow(struct iked *env, uint8_t satype, uint8_t action, struct iked_flow *flow)
{
	struct sadb_msg		 smsg;
	struct iked_addr	*flow_src, *flow_dst;
	struct sadb_address	 sa_src, sa_dst, sa_local, sa_peer, sa_smask,
				 sa_dmask;
	struct sadb_protocol	 sa_flowtype, sa_protocol;
	struct sadb_x_rdomain	 sa_rdomain;
	struct sadb_ident	*sa_srcid, *sa_dstid;
	struct sockaddr_storage	 ssrc, sdst, slocal, speer, smask, dmask;
	struct iovec		 iov[IOV_CNT];
	int			 iov_cnt, ret = -1;
	uint64_t		 pad = 0;
	size_t			 padlen;

	sa_srcid = sa_dstid = NULL;

	flow_src = &flow->flow_src;
	flow_dst = &flow->flow_dst;

	if (flow->flow_prenat.addr_af == flow_src->addr_af) {
		if (flow->flow_dir == IPSP_DIRECTION_IN)
			flow_dst = &flow->flow_prenat;
		else
			flow_src = &flow->flow_prenat;
	}

	bzero(&ssrc, sizeof(ssrc));
	bzero(&smask, sizeof(smask));
	memcpy(&ssrc, &flow_src->addr, sizeof(ssrc));
	memcpy(&smask, &flow_src->addr, sizeof(smask));
	socket_af((struct sockaddr *)&ssrc, flow_src->addr_port);
	socket_af((struct sockaddr *)&smask, flow_src->addr_port ?
	    0xffff : 0);

	switch (flow_src->addr_af) {
	case AF_INET:
		((struct sockaddr_in *)&smask)->sin_addr.s_addr =
		    prefixlen2mask(flow_src->addr_net ?
		    flow_src->addr_mask : 32);
		break;
	case AF_INET6:
		prefixlen2mask6(flow_src->addr_net ?
		    flow_src->addr_mask : 128,
		    (uint32_t *)((struct sockaddr_in6 *)
		    &smask)->sin6_addr.s6_addr);
		break;
	default:
		log_warnx("%s: unsupported address family %d",
		    __func__, flow_src->addr_af);
		return (-1);
	}
	smask.ss_len = ssrc.ss_len;

	bzero(&sdst, sizeof(sdst));
	bzero(&dmask, sizeof(dmask));
	memcpy(&sdst, &flow_dst->addr, sizeof(sdst));
	memcpy(&dmask, &flow_dst->addr, sizeof(dmask));
	socket_af((struct sockaddr *)&sdst, flow_dst->addr_port);
	socket_af((struct sockaddr *)&dmask, flow_dst->addr_port ?
	    0xffff : 0);

	switch (flow_dst->addr_af) {
	case AF_INET:
		((struct sockaddr_in *)&dmask)->sin_addr.s_addr =
		    prefixlen2mask(flow_dst->addr_net ?
		    flow_dst->addr_mask : 32);
		break;
	case AF_INET6:
		prefixlen2mask6(flow_dst->addr_net ?
		    flow_dst->addr_mask : 128,
		    (uint32_t *)((struct sockaddr_in6 *)
		    &dmask)->sin6_addr.s6_addr);
		break;
	default:
		log_warnx("%s: unsupported address family %d",
		    __func__, flow_dst->addr_af);
		return (-1);
	}
	dmask.ss_len = sdst.ss_len;

	bzero(&slocal, sizeof(slocal));
	bzero(&speer, sizeof(speer));
	if (action != SADB_X_DELFLOW && flow->flow_local != NULL) {
		memcpy(&slocal, &flow->flow_local->addr, sizeof(slocal));
		socket_af((struct sockaddr *)&slocal, 0);

		memcpy(&speer, &flow->flow_peer->addr, sizeof(speer));
		socket_af((struct sockaddr *)&speer, 0);
	}

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = ++sadb_msg_seq;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = action;
	smsg.sadb_msg_satype = satype;

	bzero(&sa_flowtype, sizeof(sa_flowtype));
	sa_flowtype.sadb_protocol_exttype = SADB_X_EXT_FLOW_TYPE;
	sa_flowtype.sadb_protocol_len = sizeof(sa_flowtype) / 8;
	sa_flowtype.sadb_protocol_direction = flow->flow_dir;
	sa_flowtype.sadb_protocol_proto = SADB_X_FLOW_TYPE_REQUIRE;

	bzero(&sa_protocol, sizeof(sa_protocol));
	sa_protocol.sadb_protocol_exttype = SADB_X_EXT_PROTOCOL;
	sa_protocol.sadb_protocol_len = sizeof(sa_protocol) / 8;
	sa_protocol.sadb_protocol_direction = 0;
	sa_protocol.sadb_protocol_proto = flow->flow_ipproto;

	bzero(&sa_src, sizeof(sa_src));
	sa_src.sadb_address_exttype = SADB_X_EXT_SRC_FLOW;
	sa_src.sadb_address_len = (sizeof(sa_src) + ROUNDUP(ssrc.ss_len)) / 8;

	bzero(&sa_smask, sizeof(sa_smask));
	sa_smask.sadb_address_exttype = SADB_X_EXT_SRC_MASK;
	sa_smask.sadb_address_len =
	    (sizeof(sa_smask) + ROUNDUP(smask.ss_len)) / 8;

	bzero(&sa_dst, sizeof(sa_dst));
	sa_dst.sadb_address_exttype = SADB_X_EXT_DST_FLOW;
	sa_dst.sadb_address_len = (sizeof(sa_dst) + ROUNDUP(sdst.ss_len)) / 8;

	bzero(&sa_dmask, sizeof(sa_dmask));
	sa_dmask.sadb_address_exttype = SADB_X_EXT_DST_MASK;
	sa_dmask.sadb_address_len =
	    (sizeof(sa_dmask) + ROUNDUP(dmask.ss_len)) / 8;

	if (action != SADB_X_DELFLOW && flow->flow_local != NULL) {
		/* local address */
		bzero(&sa_local, sizeof(sa_local));
		sa_local.sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
		sa_local.sadb_address_len =
		    (sizeof(sa_local) + ROUNDUP(slocal.ss_len)) / 8;

		/* peer address */
		bzero(&sa_peer, sizeof(sa_peer));
		sa_peer.sadb_address_exttype = SADB_EXT_ADDRESS_DST;
		sa_peer.sadb_address_len =
		    (sizeof(sa_peer) + ROUNDUP(speer.ss_len)) / 8;

		/* local id */
		sa_srcid = pfkey_id2ident(IKESA_SRCID(flow->flow_ikesa),
		    SADB_EXT_IDENTITY_SRC);

		/* peer id */
		sa_dstid = pfkey_id2ident(IKESA_DSTID(flow->flow_ikesa),
		    SADB_EXT_IDENTITY_DST);
	}

	if (flow->flow_rdomain >= 0) {
		/* install flow in specific rdomain */
		bzero(&sa_rdomain, sizeof(sa_rdomain));
		sa_rdomain.sadb_x_rdomain_exttype = SADB_X_EXT_RDOMAIN;
		sa_rdomain.sadb_x_rdomain_len = sizeof(sa_rdomain) / 8;
		sa_rdomain.sadb_x_rdomain_dom1 = flow->flow_rdomain;
	}

#define PAD(len)					\
	padlen = ROUNDUP((len)) - (len);		\
	if (padlen) {					\
		iov[iov_cnt].iov_base = &pad;		\
		iov[iov_cnt].iov_len = padlen;		\
		iov_cnt++;				\
	}

	iov_cnt = 0;

	/* header */
	iov[iov_cnt].iov_base = &smsg;
	iov[iov_cnt].iov_len = sizeof(smsg);
	iov_cnt++;

	/* add flow type */
	iov[iov_cnt].iov_base = &sa_flowtype;
	iov[iov_cnt].iov_len = sizeof(sa_flowtype);
	smsg.sadb_msg_len += sa_flowtype.sadb_protocol_len;
	iov_cnt++;

	if (action != SADB_X_DELFLOW && flow->flow_local != NULL) {
		/* remote peer */
		iov[iov_cnt].iov_base = &sa_peer;
		iov[iov_cnt].iov_len = sizeof(sa_peer);
		iov_cnt++;
		iov[iov_cnt].iov_base = &speer;
		iov[iov_cnt].iov_len = speer.ss_len;
		smsg.sadb_msg_len += sa_peer.sadb_address_len;
		iov_cnt++;
		PAD(speer.ss_len);
	}

	/* src addr */
	iov[iov_cnt].iov_base = &sa_src;
	iov[iov_cnt].iov_len = sizeof(sa_src);
	iov_cnt++;
	iov[iov_cnt].iov_base = &ssrc;
	iov[iov_cnt].iov_len = ssrc.ss_len;
	smsg.sadb_msg_len += sa_src.sadb_address_len;
	iov_cnt++;
	PAD(ssrc.ss_len);

	/* src mask */
	iov[iov_cnt].iov_base = &sa_smask;
	iov[iov_cnt].iov_len = sizeof(sa_smask);
	iov_cnt++;
	iov[iov_cnt].iov_base = &smask;
	iov[iov_cnt].iov_len = smask.ss_len;
	smsg.sadb_msg_len += sa_smask.sadb_address_len;
	iov_cnt++;
	PAD(smask.ss_len);

	/* dest addr */
	iov[iov_cnt].iov_base = &sa_dst;
	iov[iov_cnt].iov_len = sizeof(sa_dst);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst;
	iov[iov_cnt].iov_len = sdst.ss_len;
	smsg.sadb_msg_len += sa_dst.sadb_address_len;
	iov_cnt++;
	PAD(sdst.ss_len);

	/* dst mask */
	iov[iov_cnt].iov_base = &sa_dmask;
	iov[iov_cnt].iov_len = sizeof(sa_dmask);
	iov_cnt++;
	iov[iov_cnt].iov_base = &dmask;
	iov[iov_cnt].iov_len = dmask.ss_len;
	smsg.sadb_msg_len += sa_dmask.sadb_address_len;
	iov_cnt++;
	PAD(dmask.ss_len);

	/* add protocol */
	iov[iov_cnt].iov_base = &sa_protocol;
	iov[iov_cnt].iov_len = sizeof(sa_protocol);
	smsg.sadb_msg_len += sa_protocol.sadb_protocol_len;
	iov_cnt++;

	if (sa_srcid) {
		/* src identity */
		iov[iov_cnt].iov_base = sa_srcid;
		iov[iov_cnt].iov_len = sa_srcid->sadb_ident_len * 8;
		smsg.sadb_msg_len += sa_srcid->sadb_ident_len;
		iov_cnt++;
	}
	if (sa_dstid) {
		/* dst identity */
		iov[iov_cnt].iov_base = sa_dstid;
		iov[iov_cnt].iov_len = sa_dstid->sadb_ident_len * 8;
		smsg.sadb_msg_len += sa_dstid->sadb_ident_len;
		iov_cnt++;
	}

	if (flow->flow_rdomain >= 0) {
		iov[iov_cnt].iov_base = &sa_rdomain;
		iov[iov_cnt].iov_len = sizeof(sa_rdomain);
		smsg.sadb_msg_len += sa_rdomain.sadb_x_rdomain_len;
		iov_cnt++;
	}
#undef PAD

	ret = pfkey_write(env, &smsg, iov, iov_cnt, NULL, NULL);

	free(sa_srcid);
	free(sa_dstid);

	return (ret);
}

int
pfkey_sa(struct iked *env, uint8_t satype, uint8_t action, struct iked_childsa *sa)
{
	char			 iface[IF_NAMESIZE];
	struct sadb_msg		 smsg;
	struct sadb_sa		 sadb;
	struct sadb_address	 sa_src, sa_dst, sa_pxy;
	struct sadb_key		 sa_authkey, sa_enckey;
	struct sadb_lifetime	 sa_ltime_hard, sa_ltime_soft;
	struct sadb_x_udpencap	 udpencap;
	struct sadb_x_tag	 sa_tag;
	struct sadb_x_iface	 sa_iface;
	char			*tag = NULL;
	struct sadb_x_tap	 sa_tap;
	struct sadb_x_rdomain	 sa_rdomain;
	struct sockaddr_storage	 ssrc, sdst, spxy;
	struct sadb_ident	*sa_srcid, *sa_dstid;
	struct iked_lifetime	*lt;
	struct iked_policy	*pol;
	struct iked_addr	*dst;
	struct iovec		 iov[IOV_CNT];
	const char		*errstr = NULL;
	uint32_t		 ifminor;
	uint32_t		 jitter;
	int			 iov_cnt;
	int			 ret, dotap = 0;
	uint64_t		 pad = 0;
	size_t			 padlen;

	sa_srcid = sa_dstid = NULL;

	if (sa->csa_ikesa == NULL || sa->csa_ikesa->sa_policy == NULL) {
		log_warn("%s: invalid SA and policy", __func__);
		return (-1);
	}
	pol = sa->csa_ikesa->sa_policy;
	lt = &pol->pol_lifetime;

	bzero(&ssrc, sizeof(ssrc));
	memcpy(&ssrc, &sa->csa_local->addr, sizeof(ssrc));
	if (socket_af((struct sockaddr *)&ssrc, 0) == -1) {
		log_warn("%s: invalid address", __func__);
		return (-1);
	}

	dst = (action == IKED_SADB_UPDATE_SA_ADDRESSES &&
	    sa->csa_dir == IPSP_DIRECTION_OUT) ?
	    &sa->csa_ikesa->sa_peer_loaded :
	    sa->csa_peer;
	bzero(&sdst, sizeof(sdst));
	memcpy(&sdst, &dst->addr, sizeof(sdst));
	if (socket_af((struct sockaddr *)&sdst, 0) == -1) {
		log_warn("%s: invalid address", __func__);
		return (-1);
	}

	bzero(&spxy, sizeof(spxy));
	if (dst != sa->csa_peer) {
		memcpy(&spxy, &sa->csa_peer->addr, sizeof(spxy));
		if (socket_af((struct sockaddr *)&spxy, 0) == -1) {
			log_warn("%s: invalid address", __func__);
			return (-1);
		}
	}

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = ++sadb_msg_seq;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = action;
	smsg.sadb_msg_satype = satype;

	bzero(&sadb, sizeof(sadb));
	sadb.sadb_sa_len = sizeof(sadb) / 8;
	sadb.sadb_sa_exttype = SADB_EXT_SA;
	sadb.sadb_sa_spi = htonl(sa->csa_spi.spi);
	sadb.sadb_sa_state = SADB_SASTATE_MATURE;
	sadb.sadb_sa_replay = 64;

	if (!sa->csa_transport)
		sadb.sadb_sa_flags |= SADB_X_SAFLAGS_TUNNEL;

	if (sa->csa_esn)
		sadb.sadb_sa_flags |= SADB_X_SAFLAGS_ESN;

	bzero(&sa_src, sizeof(sa_src));
	sa_src.sadb_address_len = (sizeof(sa_src) + ROUNDUP(ssrc.ss_len)) / 8;
	sa_src.sadb_address_exttype = SADB_EXT_ADDRESS_SRC;

	bzero(&sa_dst, sizeof(sa_dst));
	sa_dst.sadb_address_len = (sizeof(sa_dst) + ROUNDUP(sdst.ss_len)) / 8;
	sa_dst.sadb_address_exttype = SADB_EXT_ADDRESS_DST;

	bzero(&sa_pxy, sizeof(sa_pxy));
	sa_pxy.sadb_address_len = (sizeof(sa_pxy) + ROUNDUP(spxy.ss_len)) / 8;
	sa_pxy.sadb_address_exttype = SADB_EXT_ADDRESS_PROXY;

	bzero(&sa_authkey, sizeof(sa_authkey));
	bzero(&sa_enckey, sizeof(sa_enckey));
	bzero(&udpencap, sizeof udpencap);
	bzero(&sa_ltime_hard, sizeof(sa_ltime_hard));
	bzero(&sa_ltime_soft, sizeof(sa_ltime_soft));
	bzero(&sa_iface, sizeof(sa_iface));

	if (pol->pol_rdomain >= 0) {
		bzero(&sa_rdomain, sizeof(sa_rdomain));
		sa_rdomain.sadb_x_rdomain_exttype = SADB_X_EXT_RDOMAIN;
		sa_rdomain.sadb_x_rdomain_len = sizeof(sa_rdomain) / 8;
		if (satype == SADB_X_SATYPE_IPCOMP) {
			/* IPCOMP SAs are always in the pol_rdomain */
			sa_rdomain.sadb_x_rdomain_dom1 = pol->pol_rdomain;
			sa_rdomain.sadb_x_rdomain_dom2 = pol->pol_rdomain;
		} else if (sa->csa_dir == IPSP_DIRECTION_OUT) {
			/* switch rdomain on encrypt/decrypt */
			sa_rdomain.sadb_x_rdomain_dom1 = pol->pol_rdomain;
			sa_rdomain.sadb_x_rdomain_dom2 = iked_rdomain;
		} else {
			sa_rdomain.sadb_x_rdomain_dom1 = iked_rdomain;
			sa_rdomain.sadb_x_rdomain_dom2 = pol->pol_rdomain;
		}
	}

	if (action == SADB_DELETE)
		goto send;

	if (satype == SADB_SATYPE_ESP &&
	    sa->csa_ikesa->sa_udpencap && sa->csa_ikesa->sa_natt) {
		sadb.sadb_sa_flags |= SADB_X_SAFLAGS_UDPENCAP;
		udpencap.sadb_x_udpencap_exttype = SADB_X_EXT_UDPENCAP;
		udpencap.sadb_x_udpencap_len = sizeof(udpencap) / 8;
		udpencap.sadb_x_udpencap_port =
		    sa->csa_ikesa->sa_peer.addr_port;

		log_debug("%s: udpencap port %d", __func__,
		    ntohs(udpencap.sadb_x_udpencap_port));
	}

	if (action == IKED_SADB_UPDATE_SA_ADDRESSES) {
		smsg.sadb_msg_type = SADB_UPDATE;
		goto send;
	}

	if ((action == SADB_ADD || action == SADB_UPDATE) &&
	    !sa->csa_persistent && (lt->lt_bytes || lt->lt_seconds)) {
		sa_ltime_hard.sadb_lifetime_exttype = SADB_EXT_LIFETIME_HARD;
		sa_ltime_hard.sadb_lifetime_len = sizeof(sa_ltime_hard) / 8;
		sa_ltime_hard.sadb_lifetime_bytes = lt->lt_bytes;
		sa_ltime_hard.sadb_lifetime_addtime = lt->lt_seconds;

		/* double the lifetime for ipcomp; disable byte lifetime */
		if (satype == SADB_X_SATYPE_IPCOMP) {
			sa_ltime_hard.sadb_lifetime_addtime *= 2;
			sa_ltime_hard.sadb_lifetime_bytes = 0;
		}

		sa_ltime_soft.sadb_lifetime_exttype = SADB_EXT_LIFETIME_SOFT;
		sa_ltime_soft.sadb_lifetime_len = sizeof(sa_ltime_soft) / 8;
		/* set randomly to 85-95% */
		jitter = 850 + arc4random_uniform(100);
		sa_ltime_soft.sadb_lifetime_bytes =
		    (sa_ltime_hard.sadb_lifetime_bytes * jitter) / 1000;
		sa_ltime_soft.sadb_lifetime_addtime =
		    (sa_ltime_hard.sadb_lifetime_addtime * jitter) / 1000;
	}

	/* XXX handle NULL encryption or NULL auth or combined encr/auth */
	if (action == SADB_ADD &&
	    !ibuf_length(sa->csa_integrkey) && !ibuf_length(sa->csa_encrkey) &&
	    satype != SADB_X_SATYPE_IPCOMP && satype != SADB_X_SATYPE_IPIP) {
		log_warnx("%s: no key specified", __func__);
		return (-1);
	}

	if (sa->csa_integrid)
		if (pfkey_map(pfkey_integr,
		    sa->csa_integrid, &sadb.sadb_sa_auth) == -1) {
			log_warnx("%s: unsupported integrity algorithm %s",
			    __func__, print_map(sa->csa_integrid,
			    ikev2_xformauth_map));
			return (-1);
		}

	if (sa->csa_encrid)
		if (pfkey_map(pfkey_encr,
		    sa->csa_encrid, &sadb.sadb_sa_encrypt) == -1) {
			log_warnx("%s: unsupported encryption algorithm %s",
			    __func__, print_map(sa->csa_encrid,
			    ikev2_xformencr_map));
			return (-1);
		}

	if (ibuf_length(sa->csa_integrkey)) {
		sa_authkey.sadb_key_len = (sizeof(sa_authkey) +
		    ROUNDUP(ibuf_size(sa->csa_integrkey))) / 8;
		sa_authkey.sadb_key_exttype = SADB_EXT_KEY_AUTH;
		sa_authkey.sadb_key_bits =
		    8 * ibuf_size(sa->csa_integrkey);
	}

	if (ibuf_length(sa->csa_encrkey)) {
		sa_enckey.sadb_key_len = (sizeof(sa_enckey) +
		    ROUNDUP(ibuf_size(sa->csa_encrkey))) / 8;
		sa_enckey.sadb_key_exttype = SADB_EXT_KEY_ENCRYPT;
		sa_enckey.sadb_key_bits =
		    8 * ibuf_size(sa->csa_encrkey);
	}

	/* we only support deflate */
	if (satype == SADB_X_SATYPE_IPCOMP)
		sadb.sadb_sa_encrypt = SADB_X_CALG_DEFLATE;

	/* Note that we need to swap the IDs for incoming SAs (SADB_UPDATE) */
	if (action != SADB_UPDATE) {
		sa_srcid = pfkey_id2ident(
		    IKESA_SRCID(sa->csa_ikesa), SADB_EXT_IDENTITY_SRC);
		sa_dstid = pfkey_id2ident(
		    IKESA_DSTID(sa->csa_ikesa), SADB_EXT_IDENTITY_DST);
	} else {
		sa_srcid = pfkey_id2ident(
		    IKESA_DSTID(sa->csa_ikesa), SADB_EXT_IDENTITY_SRC);
		sa_dstid = pfkey_id2ident(
		    IKESA_SRCID(sa->csa_ikesa), SADB_EXT_IDENTITY_DST);
	}

	tag = sa->csa_ikesa->sa_tag;
	if (tag != NULL && *tag != '\0') {
		bzero(&sa_tag, sizeof(sa_tag));
		sa_tag.sadb_x_tag_exttype = SADB_X_EXT_TAG;
		sa_tag.sadb_x_tag_len =
		    (ROUNDUP(strlen(tag) + 1) + sizeof(sa_tag)) / 8;
		sa_tag.sadb_x_tag_taglen = strlen(tag) + 1;
	} else
		tag = NULL;

	if (pol->pol_tap != 0) {
		dotap = 1;
		bzero(&sa_tap, sizeof(sa_tap));
		sa_tap.sadb_x_tap_exttype = SADB_X_EXT_TAP;
		sa_tap.sadb_x_tap_len = sizeof(sa_tap) / 8;
		sa_tap.sadb_x_tap_unit = pol->pol_tap;
	}

	if (pol->pol_flags & IKED_POLICY_ROUTING) {
		sa_iface.sadb_x_iface_exttype = SADB_X_EXT_IFACE;
		sa_iface.sadb_x_iface_len = sizeof(sa_iface) / 8;
		if (if_indextoname(pol->pol_iface, iface) == 0) {
			log_warn("%s: unsupported interface %d",
			    __func__, pol->pol_iface);
			return (-1);
		}
		ifminor = strtonum(iface + strlen("sec"), 0, UINT_MAX, &errstr);
		if (errstr != NULL) {
			log_warnx("%s: unsupported interface %s",
			    __func__, iface);
			return (-1);
		}
		sa_iface.sadb_x_iface_unit = ifminor;
		sa_iface.sadb_x_iface_direction = sa->csa_dir;
	}

 send:

#define PAD(len)					\
	padlen = ROUNDUP((len)) - (len);		\
	if (padlen) {					\
		iov[iov_cnt].iov_base = &pad;		\
		iov[iov_cnt].iov_len = padlen;		\
		iov_cnt++;				\
	}

	iov_cnt = 0;

	/* header */
	iov[iov_cnt].iov_base = &smsg;
	iov[iov_cnt].iov_len = sizeof(smsg);
	iov_cnt++;

	/* sa */
	iov[iov_cnt].iov_base = &sadb;
	iov[iov_cnt].iov_len = sizeof(sadb);
	smsg.sadb_msg_len += sadb.sadb_sa_len;
	iov_cnt++;

	/* src addr */
	iov[iov_cnt].iov_base = &sa_src;
	iov[iov_cnt].iov_len = sizeof(sa_src);
	iov_cnt++;
	iov[iov_cnt].iov_base = &ssrc;
	iov[iov_cnt].iov_len = ssrc.ss_len;
	smsg.sadb_msg_len += sa_src.sadb_address_len;
	iov_cnt++;
	PAD(ssrc.ss_len);

	/* dst addr */
	iov[iov_cnt].iov_base = &sa_dst;
	iov[iov_cnt].iov_len = sizeof(sa_dst);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst;
	iov[iov_cnt].iov_len = sdst.ss_len;
	smsg.sadb_msg_len += sa_dst.sadb_address_len;
	iov_cnt++;
	PAD(sdst.ss_len);

	if (spxy.ss_len) {
		/* pxy addr */
		iov[iov_cnt].iov_base = &sa_pxy;
		iov[iov_cnt].iov_len = sizeof(sa_pxy);
		iov_cnt++;
		iov[iov_cnt].iov_base = &spxy;
		iov[iov_cnt].iov_len = spxy.ss_len;
		smsg.sadb_msg_len += sa_pxy.sadb_address_len;
		iov_cnt++;
		PAD(spxy.ss_len);
	}

	if (sa_ltime_soft.sadb_lifetime_len) {
		/* soft lifetime */
		iov[iov_cnt].iov_base = &sa_ltime_soft;
		iov[iov_cnt].iov_len = sizeof(sa_ltime_soft);
		smsg.sadb_msg_len += sa_ltime_soft.sadb_lifetime_len;
		iov_cnt++;
	}

	if (sa_ltime_hard.sadb_lifetime_len) {
		/* hard lifetime */
		iov[iov_cnt].iov_base = &sa_ltime_hard;
		iov[iov_cnt].iov_len = sizeof(sa_ltime_hard);
		smsg.sadb_msg_len += sa_ltime_hard.sadb_lifetime_len;
		iov_cnt++;
	}

	if (udpencap.sadb_x_udpencap_len) {
		iov[iov_cnt].iov_base = &udpencap;
		iov[iov_cnt].iov_len = sizeof(udpencap);
		smsg.sadb_msg_len += udpencap.sadb_x_udpencap_len;
		iov_cnt++;
	}

	if (sa_enckey.sadb_key_len) {
		/* encryption key */
		iov[iov_cnt].iov_base = &sa_enckey;
		iov[iov_cnt].iov_len = sizeof(sa_enckey);
		iov_cnt++;
		iov[iov_cnt].iov_base = ibuf_data(sa->csa_encrkey);
		iov[iov_cnt].iov_len = ibuf_size(sa->csa_encrkey);
		smsg.sadb_msg_len += sa_enckey.sadb_key_len;
		iov_cnt++;
		PAD(ibuf_size(sa->csa_encrkey));
	}
	if (sa_authkey.sadb_key_len) {
		/* authentication key */
		iov[iov_cnt].iov_base = &sa_authkey;
		iov[iov_cnt].iov_len = sizeof(sa_authkey);
		iov_cnt++;
		iov[iov_cnt].iov_base = ibuf_data(sa->csa_integrkey);
		iov[iov_cnt].iov_len = ibuf_size(sa->csa_integrkey);
		smsg.sadb_msg_len += sa_authkey.sadb_key_len;
		iov_cnt++;
		PAD(ibuf_size(sa->csa_integrkey));
	}

	if (sa_srcid) {
		/* src identity */
		iov[iov_cnt].iov_base = sa_srcid;
		iov[iov_cnt].iov_len = sa_srcid->sadb_ident_len * 8;
		smsg.sadb_msg_len += sa_srcid->sadb_ident_len;
		iov_cnt++;
	}
	if (sa_dstid) {
		/* dst identity */
		iov[iov_cnt].iov_base = sa_dstid;
		iov[iov_cnt].iov_len = sa_dstid->sadb_ident_len * 8;
		smsg.sadb_msg_len += sa_dstid->sadb_ident_len;
		iov_cnt++;
	}

	if (tag != NULL) {
		/* tag identity */
		iov[iov_cnt].iov_base = &sa_tag;
		iov[iov_cnt].iov_len = sizeof(sa_tag);
		iov_cnt++;
		iov[iov_cnt].iov_base = tag;
		iov[iov_cnt].iov_len = strlen(tag) + 1;
		smsg.sadb_msg_len += sa_tag.sadb_x_tag_len;
		iov_cnt++;
		PAD(strlen(tag) + 1);
	}

	if (sa_iface.sadb_x_iface_len) {
		iov[iov_cnt].iov_base = &sa_iface;
		iov[iov_cnt].iov_len = sa_iface.sadb_x_iface_len * 8;
		smsg.sadb_msg_len += sa_iface.sadb_x_iface_len;
		iov_cnt++;
	}

	if (dotap != 0) {
		/* enc(4) device tap unit */
		iov[iov_cnt].iov_base = &sa_tap;
		iov[iov_cnt].iov_len = sizeof(sa_tap);
		smsg.sadb_msg_len += sa_tap.sadb_x_tap_len;
		iov_cnt++;
	}

	if (pol->pol_rdomain >= 0) {
		iov[iov_cnt].iov_base = &sa_rdomain;
		iov[iov_cnt].iov_len = sizeof(sa_rdomain);
		smsg.sadb_msg_len += sa_rdomain.sadb_x_rdomain_len;
		iov_cnt++;
	}
#undef PAD

	ret = pfkey_write(env, &smsg, iov, iov_cnt, NULL, NULL);

	free(sa_srcid);
	free(sa_dstid);

	return ret;
}

int
pfkey_sa_lookup(struct iked *env, struct iked_childsa *sa, uint64_t *last_used,
    struct iked_sastats *stats)
{
	struct iked_policy	*pol = sa->csa_ikesa->sa_policy;
	struct sadb_msg		*msg, smsg;
	struct sadb_address	 sa_src, sa_dst;
	struct sadb_sa		 sadb;
	struct sadb_x_rdomain	 sa_rdomain;
	struct sadb_lifetime	*sa_life;
	struct sadb_x_counter	*sa_counter;
	struct sockaddr_storage	 ssrc, sdst;
	struct iovec		 iov[IOV_CNT];
	uint64_t		 pad = 0;
	size_t			 padlen;
	uint8_t			*data;
	ssize_t			 n;
	int			 iov_cnt, ret = -1, rdomain;
	uint8_t			 satype;

	if (last_used)
		*last_used = 0;

	if (pfkey_map(pfkey_satype, sa->csa_saproto, &satype) == -1)
		return (-1);

	bzero(&ssrc, sizeof(ssrc));
	memcpy(&ssrc, &sa->csa_local->addr, sizeof(ssrc));
	if (socket_af((struct sockaddr *)&ssrc, 0) == -1) {
		log_warn("%s: invalid address", __func__);
		return (-1);
	}

	bzero(&sdst, sizeof(sdst));
	memcpy(&sdst, &sa->csa_peer->addr, sizeof(sdst));
	if (socket_af((struct sockaddr *)&sdst, 0) == -1) {
		log_warn("%s: invalid address", __func__);
		return (-1);
	}

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = ++sadb_msg_seq;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = SADB_GET;
	smsg.sadb_msg_satype = satype;

	bzero(&sadb, sizeof(sadb));
	sadb.sadb_sa_len = sizeof(sadb) / 8;
	sadb.sadb_sa_exttype = SADB_EXT_SA;
	sadb.sadb_sa_spi = htonl(sa->csa_spi.spi);
	sadb.sadb_sa_state = SADB_SASTATE_MATURE;
	sadb.sadb_sa_replay = 64;

	if (pol->pol_rdomain >= 0) {
		rdomain = (sa->csa_dir == IPSP_DIRECTION_IN) ?
		    iked_rdomain : pol->pol_rdomain;
		bzero(&sa_rdomain, sizeof(sa_rdomain));
		sa_rdomain.sadb_x_rdomain_exttype = SADB_X_EXT_RDOMAIN;
		sa_rdomain.sadb_x_rdomain_len = sizeof(sa_rdomain) / 8;
		sa_rdomain.sadb_x_rdomain_dom1 = rdomain;
	}

	bzero(&sa_src, sizeof(sa_src));
	sa_src.sadb_address_len = (sizeof(sa_src) + ROUNDUP(ssrc.ss_len)) / 8;
	sa_src.sadb_address_exttype = SADB_EXT_ADDRESS_SRC;

	bzero(&sa_dst, sizeof(sa_dst));
	sa_dst.sadb_address_len = (sizeof(sa_dst) + ROUNDUP(sdst.ss_len)) / 8;
	sa_dst.sadb_address_exttype = SADB_EXT_ADDRESS_DST;

	iov_cnt = 0;

#define PAD(len)					\
	padlen = ROUNDUP((len)) - (len);		\
	if (padlen) {					\
		iov[iov_cnt].iov_base = &pad;		\
		iov[iov_cnt].iov_len = padlen;		\
		iov_cnt++;				\
	}

	/* header */
	iov[iov_cnt].iov_base = &smsg;
	iov[iov_cnt].iov_len = sizeof(smsg);
	iov_cnt++;

	/* sa */
	iov[iov_cnt].iov_base = &sadb;
	iov[iov_cnt].iov_len = sizeof(sadb);
	smsg.sadb_msg_len += sadb.sadb_sa_len;
	iov_cnt++;

	/* src addr */
	iov[iov_cnt].iov_base = &sa_src;
	iov[iov_cnt].iov_len = sizeof(sa_src);
	iov_cnt++;
	iov[iov_cnt].iov_base = &ssrc;
	iov[iov_cnt].iov_len = ssrc.ss_len;
	smsg.sadb_msg_len += sa_src.sadb_address_len;
	iov_cnt++;
	PAD(ssrc.ss_len);

	/* dst addr */
	iov[iov_cnt].iov_base = &sa_dst;
	iov[iov_cnt].iov_len = sizeof(sa_dst);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst;
	iov[iov_cnt].iov_len = sdst.ss_len;
	smsg.sadb_msg_len += sa_dst.sadb_address_len;
	iov_cnt++;
	PAD(sdst.ss_len);

	if (pol->pol_rdomain >= 0) {
		iov[iov_cnt].iov_base = &sa_rdomain;
		iov[iov_cnt].iov_len = sizeof(sa_rdomain);
		smsg.sadb_msg_len += sa_rdomain.sadb_x_rdomain_len;
		iov_cnt++;
	}

	if ((ret = pfkey_write(env, &smsg, iov, iov_cnt, &data, &n)) != 0)
		return (-1);

	msg = (struct sadb_msg *)data;
	if (msg->sadb_msg_errno != 0) {
		errno = msg->sadb_msg_errno;
		ret = -1;
		if (errno == ESRCH)
			log_debug("%s: not found", __func__);
		else
			log_warn("%s: message", __func__);
		goto done;
	}
	if (last_used) {
		if ((sa_life = pfkey_find_ext(data, n,
		    SADB_X_EXT_LIFETIME_LASTUSE)) == NULL) {
			/* has never been used */
			ret = -1;
			goto done;
		}
		*last_used = sa_life->sadb_lifetime_usetime;
		log_debug("%s: last_used %llu", __func__, *last_used);
	}
	if (stats) {
		if ((sa_counter = pfkey_find_ext(data, n,
		    SADB_X_EXT_COUNTER)) == NULL) {
			/* has never been used */
			ret = -1;
			goto done;
		}
		stats->sas_ibytes = sa_counter->sadb_x_counter_ibytes;
		stats->sas_obytes = sa_counter->sadb_x_counter_obytes;
		stats->sas_ipackets = sa_counter->sadb_x_counter_ipackets;
		stats->sas_opackets = sa_counter->sadb_x_counter_opackets;
		stats->sas_idrops = sa_counter->sadb_x_counter_idrops;
		stats->sas_odrops = sa_counter->sadb_x_counter_odrops;
	}

#undef PAD
done:
	freezero(data, n);
	return (ret);
}

int
pfkey_sa_last_used(struct iked *env, struct iked_childsa *sa, uint64_t *last_used)
{
	return pfkey_sa_lookup(env, sa, last_used, NULL);
}

int
pfkey_sa_check_exists(struct iked *env, struct iked_childsa *sa)
{
	return pfkey_sa_lookup(env, sa, NULL, NULL);
}

int
pfkey_sa_sastats(struct iked *env, struct iked_childsa *sa,
    struct iked_sastats *stats)
{
	return pfkey_sa_lookup(env, sa, NULL, stats);
}

int
pfkey_sa_getspi(struct iked *env, uint8_t satype, struct iked_childsa *sa,
    uint32_t *spip)
{
	struct sadb_msg		*msg, smsg;
	struct sadb_address	 sa_src, sa_dst;
	struct sadb_sa		*sa_ext;
	struct sadb_spirange	 sa_spirange;
	struct sockaddr_storage	 ssrc, sdst;
	struct iovec		 iov[IOV_CNT];
	uint64_t		 pad = 0;
	size_t			 padlen;
	uint8_t			*data;
	ssize_t			 n;
	int			 iov_cnt, ret = -1;

	bzero(&ssrc, sizeof(ssrc));
	memcpy(&ssrc, &sa->csa_local->addr, sizeof(ssrc));
	if (socket_af((struct sockaddr *)&ssrc, 0) == -1) {
		log_warn("%s: invalid address", __func__);
		return (-1);
	}

	bzero(&sdst, sizeof(sdst));
	memcpy(&sdst, &sa->csa_peer->addr, sizeof(sdst));
	if (socket_af((struct sockaddr *)&sdst, 0) == -1) {
		log_warn("%s: invalid address", __func__);
		return (-1);
	}

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = ++sadb_msg_seq;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = SADB_GETSPI;
	smsg.sadb_msg_satype = satype;

	bzero(&sa_spirange, sizeof(sa_spirange));
	sa_spirange.sadb_spirange_exttype = SADB_EXT_SPIRANGE;
	sa_spirange.sadb_spirange_len = sizeof(sa_spirange) / 8;
	sa_spirange.sadb_spirange_min = 0x100;
	sa_spirange.sadb_spirange_max = (satype == SADB_X_SATYPE_IPCOMP) ?
	    (CPI_PRIVATE_MIN - 1) : 0xffffffff;
	sa_spirange.sadb_spirange_reserved = 0;

	bzero(&sa_src, sizeof(sa_src));
	sa_src.sadb_address_len = (sizeof(sa_src) + ROUNDUP(ssrc.ss_len)) / 8;
	sa_src.sadb_address_exttype = SADB_EXT_ADDRESS_SRC;

	bzero(&sa_dst, sizeof(sa_dst));
	sa_dst.sadb_address_len = (sizeof(sa_dst) + ROUNDUP(sdst.ss_len)) / 8;
	sa_dst.sadb_address_exttype = SADB_EXT_ADDRESS_DST;

#define PAD(len)					\
	padlen = ROUNDUP((len)) - (len);		\
	if (padlen) {					\
		iov[iov_cnt].iov_base = &pad;		\
		iov[iov_cnt].iov_len = padlen;		\
		iov_cnt++;				\
	}

	iov_cnt = 0;

	/* header */
	iov[iov_cnt].iov_base = &smsg;
	iov[iov_cnt].iov_len = sizeof(smsg);
	iov_cnt++;

	/* SPI range */
	iov[iov_cnt].iov_base = &sa_spirange;
	iov[iov_cnt].iov_len = sizeof(sa_spirange);
	smsg.sadb_msg_len += sa_spirange.sadb_spirange_len;
	iov_cnt++;

	/* src addr */
	iov[iov_cnt].iov_base = &sa_src;
	iov[iov_cnt].iov_len = sizeof(sa_src);
	iov_cnt++;
	iov[iov_cnt].iov_base = &ssrc;
	iov[iov_cnt].iov_len = ssrc.ss_len;
	smsg.sadb_msg_len += sa_src.sadb_address_len;
	iov_cnt++;
	PAD(ssrc.ss_len);

	/* dst addr */
	iov[iov_cnt].iov_base = &sa_dst;
	iov[iov_cnt].iov_len = sizeof(sa_dst);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst;
	iov[iov_cnt].iov_len = sdst.ss_len;
	smsg.sadb_msg_len += sa_dst.sadb_address_len;
	iov_cnt++;
	PAD(sdst.ss_len);

	*spip = 0;

	if ((ret = pfkey_write(env, &smsg, iov, iov_cnt, &data, &n)) != 0)
		return (-1);

	msg = (struct sadb_msg *)data;
	if (msg->sadb_msg_errno != 0) {
		errno = msg->sadb_msg_errno;
		log_warn("%s: message", __func__);
		goto done;
	}
	if ((sa_ext = pfkey_find_ext(data, n, SADB_EXT_SA)) == NULL) {
		log_debug("%s: erroneous reply", __func__);
		goto done;
	}

	*spip = ntohl(sa_ext->sadb_sa_spi);
	log_debug("%s: spi 0x%08x", __func__, *spip);

#undef PAD

done:
	freezero(data, n);
	return (ret);
}

int
pfkey_sagroup(struct iked *env, uint8_t satype1, uint8_t action,
    struct iked_childsa *sa1, struct iked_childsa *sa2)
{
	struct sadb_msg		smsg;
	struct sadb_sa		sadb1, sadb2;
	struct sadb_address	sa_dst1, sa_dst2;
	struct sockaddr_storage	sdst1, sdst2;
	struct sadb_protocol	sa_proto;
	struct sadb_x_rdomain	sa_rdomain;
	struct iked_policy	*pol;
	struct iovec		iov[IOV_CNT];
	uint64_t		pad = 0;
	size_t			padlen;
	int			iov_cnt;
	int			group_rdomain;
	uint8_t			satype2;

	if (pfkey_map(pfkey_satype, sa2->csa_saproto, &satype2) == -1)
		return (-1);

	bzero(&sdst1, sizeof(sdst1));
	memcpy(&sdst1, &sa1->csa_peer->addr, sizeof(sdst1));
	if (socket_af((struct sockaddr *)&sdst1, 0) == -1) {
		log_warnx("%s: unsupported address family %d",
		    __func__, sdst1.ss_family);
		return (-1);
	}

	bzero(&sdst2, sizeof(sdst2));
	memcpy(&sdst2, &sa2->csa_peer->addr, sizeof(sdst2));
	if (socket_af((struct sockaddr *)&sdst2, 0) == -1) {
		log_warnx("%s: unsupported address family %d",
		    __func__, sdst2.ss_family);
		return (-1);
	}

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = ++sadb_msg_seq;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = action;
	smsg.sadb_msg_satype = satype1;

	bzero(&sadb1, sizeof(sadb1));
	sadb1.sadb_sa_len = sizeof(sadb1) / 8;
	sadb1.sadb_sa_exttype = SADB_EXT_SA;
	sadb1.sadb_sa_spi = htonl(sa1->csa_spi.spi);
	sadb1.sadb_sa_state = SADB_SASTATE_MATURE;

	bzero(&sadb2, sizeof(sadb2));
	sadb2.sadb_sa_len = sizeof(sadb2) / 8;
	sadb2.sadb_sa_exttype = SADB_X_EXT_SA2;
	sadb2.sadb_sa_spi = htonl(sa2->csa_spi.spi);
	sadb2.sadb_sa_state = SADB_SASTATE_MATURE;

	/* Incoming SA1 (IPCOMP) and SA2 (ESP) are in different/other rdomain */
	group_rdomain =
	    (pol = sa1->csa_ikesa->sa_policy) != NULL &&
	    pol->pol_rdomain >= 0 &&
	    satype1 == SADB_X_SATYPE_IPCOMP &&
	    satype2 == SADB_SATYPE_ESP;
	if (group_rdomain) {
		bzero(&sa_rdomain, sizeof(sa_rdomain));
		sa_rdomain.sadb_x_rdomain_exttype = SADB_X_EXT_RDOMAIN;
		sa_rdomain.sadb_x_rdomain_len = sizeof(sa_rdomain) / 8;
		if (sa1->csa_dir == IPSP_DIRECTION_IN) {
			/* only ESP SA is iked's rdomain */
			sa_rdomain.sadb_x_rdomain_dom1 = pol->pol_rdomain;
			sa_rdomain.sadb_x_rdomain_dom2 = iked_rdomain;
		} else {
			/* both SAs are in pol_rdomain */
			sa_rdomain.sadb_x_rdomain_dom1 = pol->pol_rdomain;
			sa_rdomain.sadb_x_rdomain_dom2 = pol->pol_rdomain;
		}
	}

	iov_cnt = 0;

	bzero(&sa_dst1, sizeof(sa_dst1));
	sa_dst1.sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	sa_dst1.sadb_address_len =
	    (sizeof(sa_dst1) + ROUNDUP(sdst1.ss_len)) / 8;

	bzero(&sa_dst2, sizeof(sa_dst2));
	sa_dst2.sadb_address_exttype = SADB_X_EXT_DST2;
	sa_dst2.sadb_address_len =
	    (sizeof(sa_dst2) + ROUNDUP(sdst2.ss_len)) / 8;

	bzero(&sa_proto, sizeof(sa_proto));
	sa_proto.sadb_protocol_exttype = SADB_X_EXT_SATYPE2;
	sa_proto.sadb_protocol_len = sizeof(sa_proto) / 8;
	sa_proto.sadb_protocol_direction = 0;
	sa_proto.sadb_protocol_proto = satype2;

#define PAD(len)					\
	padlen = ROUNDUP((len)) - (len);		\
	if (padlen) {					\
		iov[iov_cnt].iov_base = &pad;		\
		iov[iov_cnt].iov_len = padlen;		\
		iov_cnt++;				\
	}

	/* header */
	iov[iov_cnt].iov_base = &smsg;
	iov[iov_cnt].iov_len = sizeof(smsg);
	iov_cnt++;

	/* sa */
	iov[iov_cnt].iov_base = &sadb1;
	iov[iov_cnt].iov_len = sizeof(sadb1);
	smsg.sadb_msg_len += sadb1.sadb_sa_len;
	iov_cnt++;

	/* dst addr */
	iov[iov_cnt].iov_base = &sa_dst1;
	iov[iov_cnt].iov_len = sizeof(sa_dst1);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst1;
	iov[iov_cnt].iov_len = sdst1.ss_len;
	smsg.sadb_msg_len += sa_dst1.sadb_address_len;
	iov_cnt++;
	PAD(sdst1.ss_len);

	/* second sa */
	iov[iov_cnt].iov_base = &sadb2;
	iov[iov_cnt].iov_len = sizeof(sadb2);
	smsg.sadb_msg_len += sadb2.sadb_sa_len;
	iov_cnt++;

	/* second dst addr */
	iov[iov_cnt].iov_base = &sa_dst2;
	iov[iov_cnt].iov_len = sizeof(sa_dst2);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst2;
	iov[iov_cnt].iov_len = sdst2.ss_len;
	smsg.sadb_msg_len += sa_dst2.sadb_address_len;
	iov_cnt++;
	PAD(sdst2.ss_len);

	/* SA type */
	iov[iov_cnt].iov_base = &sa_proto;
	iov[iov_cnt].iov_len = sizeof(sa_proto);
	smsg.sadb_msg_len += sa_proto.sadb_protocol_len;
	iov_cnt++;

	/* SA1 and SA2 are from different rdomains */
	if (group_rdomain) {
		iov[iov_cnt].iov_base = &sa_rdomain;
		iov[iov_cnt].iov_len = sizeof(sa_rdomain);
		smsg.sadb_msg_len += sa_rdomain.sadb_x_rdomain_len;
		iov_cnt++;
	}

#undef PAD

	return (pfkey_write(env, &smsg, iov, iov_cnt, NULL, NULL));
}

int
pfkey_write(struct iked *env, struct sadb_msg *smsg, struct iovec *iov, int iov_cnt,
    uint8_t **datap, ssize_t *lenp)
{
	ssize_t n, len = smsg->sadb_msg_len * 8;
	int ret = -1;

	if (sadb_decoupled) {
		switch (smsg->sadb_msg_type) {
		case SADB_GETSPI:
			/* we need to get a new SPI from the kernel */
			break;
		default:
			if (datap || lenp) {
				log_warnx("%s: pfkey not coupled", __func__);
				return (-1);
			}
			/* ignore request */
			return (0);
		}
	}

	/* Delete event to poll() in pfkey_reply() */
	event_del(&env->sc_pfkeyev);

	if ((n = writev(env->sc_pfkey, iov, iov_cnt)) == -1) {
		log_warn("%s: writev failed: type %u len %zd",
		    __func__, smsg->sadb_msg_type, len);
		goto done;
	} else if (n != len) {
		log_warn("%s: short write", __func__);
		goto done;
	}

	ret = pfkey_reply(env->sc_pfkey, datap, lenp);
 done:
	event_add(&env->sc_pfkeyev, NULL);
	return (ret);
}

/* wait for pfkey response and returns 0 for ok, -1 for error, -2 for timeout */
int
pfkey_reply(int fd, uint8_t **datap, ssize_t *lenp)
{
	struct pfkey_message	*pm;
	struct sadb_msg		 hdr;
	ssize_t			 len;
	uint8_t			*data;
	struct pollfd		pfd[1];
	int			 n;

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;

	for (;;) {
		/*
		 * We should actually expect the reply to get lost
		 * as PF_KEY is an unreliable service per the specs.
		 * Currently we do this by setting a short timeout,
		 * and if it is not readable in that time, we fail
		 * the read.
		 */
		n = poll(pfd, 1, PFKEY_REPLY_TIMEOUT / 1000);
		if (n == -1) {
			log_warn("%s: poll() failed", __func__);
			return (-1);
		}
		if (n == 0) {
			log_warnx("%s: no reply from PF_KEY", __func__);
			return (-2);	/* retry */
		}

		if (recv(fd, &hdr, sizeof(hdr), MSG_PEEK) != sizeof(hdr)) {
			log_warn("%s: short recv", __func__);
			return (-1);
		}

		if (hdr.sadb_msg_version != PF_KEY_V2) {
			log_warnx("%s: wrong pfkey version", __func__);
			return (-1);
		}

		if ((data = reallocarray(NULL, hdr.sadb_msg_len,
		    PFKEYV2_CHUNK)) == NULL) {
			log_warn("%s: malloc", __func__);
			return (-1);
		}
		len = hdr.sadb_msg_len * PFKEYV2_CHUNK;

		if (read(fd, data, len) != len) {
			log_warnx("%s: short read", __func__);
			free(data);
			return (-1);
		}

		/* XXX: Only one message can be outstanding. */
		if (hdr.sadb_msg_seq == sadb_msg_seq &&
		    hdr.sadb_msg_pid == (uint32_t)getpid())
			break;

		/* ignore messages for other processes */
		if (hdr.sadb_msg_pid != 0 &&
		    hdr.sadb_msg_pid != (uint32_t)getpid()) {
			free(data);
			continue;
		}

		/* not the reply, enqueue */
		if ((pm = malloc(sizeof(*pm))) == NULL) {
			log_warn("%s: malloc", __func__);
			free(data);
			return (-1);
		}
		pm->pm_data = data;
		pm->pm_length = len;
		SIMPLEQ_INSERT_TAIL(&pfkey_postponed, pm, pm_entry);
		evtimer_add(&pfkey_timer_ev, &pfkey_timer_tv);
	}

	if (datap) {
		*datap = data;
		if (lenp)
			*lenp = len;
	} else
		free(data);

	if (datap == NULL && hdr.sadb_msg_errno != 0) {
		errno = hdr.sadb_msg_errno;
		if (errno != EEXIST) {
			if (errno == ESRCH)
				log_debug("%s: not found", __func__);
			else
				log_warn("%s: message", __func__);
			return (-1);
		}
	}
	return (0);
}

int
pfkey_flow_add(struct iked *env, struct iked_flow *flow)
{
	uint8_t		 satype;

	if (flow->flow_loaded)
		return (0);

	if (pfkey_map(pfkey_satype, flow->flow_saproto, &satype) == -1)
		return (-1);

	if (pfkey_flow(env, satype, SADB_X_ADDFLOW, flow) == -1)
		return (-1);

	flow->flow_loaded = 1;

	return (0);
}

int
pfkey_flow_delete(struct iked *env, struct iked_flow *flow)
{
	uint8_t		satype;

	if (!flow->flow_loaded)
		return (0);

	if (pfkey_map(pfkey_satype, flow->flow_saproto, &satype) == -1)
		return (-1);

	if (pfkey_flow(env, satype, SADB_X_DELFLOW, flow) == -1)
		return (-1);

	flow->flow_loaded = 0;

	return (0);
}

int
pfkey_sa_init(struct iked *env, struct iked_childsa *sa, uint32_t *spi)
{
	uint8_t		 satype;

	if (pfkey_map(pfkey_satype, sa->csa_saproto, &satype) == -1)
		return (-1);

	if (pfkey_sa_getspi(env, satype, sa, spi) == -1)
		return (-1);

	log_debug("%s: new spi 0x%08x", __func__, *spi);

	return (0);
}

int
pfkey_sa_add(struct iked *env, struct iked_childsa *sa, struct iked_childsa *last)
{
	uint8_t		 satype;
	unsigned int	 cmd;
	int		 rval;

	if (pfkey_map(pfkey_satype, sa->csa_saproto, &satype) == -1)
		return (-1);

	if (sa->csa_allocated || sa->csa_loaded)
		cmd = SADB_UPDATE;
	else
		cmd = SADB_ADD;

	log_debug("%s: %s spi %s", __func__, cmd == SADB_ADD ? "add": "update",
	    print_spi(sa->csa_spi.spi, 4));

	rval = pfkey_sa(env, satype, cmd, sa);
	if (rval != 0) {
		if (cmd == SADB_ADD) {
			if (rval == -2) {
				/* timeout: check for existence */
				if (pfkey_sa_check_exists(env, sa) == 0) {
					log_debug("%s: SA exists after timeout",
					    __func__);
					goto loaded;
				}
			}
			(void)pfkey_sa_delete(env, sa);
			return (-1);
		}
		if (sa->csa_allocated && !sa->csa_loaded && errno == ESRCH) {
			/* Needed for recoupling local SAs */
			log_debug("%s: SADB_UPDATE on local SA returned ESRCH,"
			    " trying SADB_ADD", __func__);
			if (pfkey_sa(env, satype, SADB_ADD, sa) == -1)
				return (-1);
		} else {
			return (-1);
		}
	}

 loaded:
	if (last != NULL) {
		if (pfkey_sagroup(env, satype,
		    SADB_X_GRPSPIS, sa, last) == -1) {
			(void)pfkey_sa_delete(env, sa);
			return (-1);
		}
	}

	sa->csa_loaded = 1;
	return (0);
}

int
pfkey_sa_update_addresses(struct iked *env, struct iked_childsa *sa)
{
	uint8_t		 satype;

	if (!sa->csa_ikesa)
		return (-1);
	/* check if peer has changed */
	if (sa->csa_ikesa->sa_peer_loaded.addr.ss_family == AF_UNSPEC ||
	    memcmp(&sa->csa_ikesa->sa_peer_loaded, &sa->csa_ikesa->sa_peer,
	    sizeof(sa->csa_ikesa->sa_peer_loaded)) == 0)
		return (0);
	if (pfkey_map(pfkey_satype, sa->csa_saproto, &satype) == -1)
		return (-1);
	log_debug("%s: spi %s", __func__, print_spi(sa->csa_spi.spi, 4));
	return pfkey_sa(env, satype, IKED_SADB_UPDATE_SA_ADDRESSES, sa);
}

int
pfkey_sa_delete(struct iked *env, struct iked_childsa *sa)
{
	uint8_t			satype;
	struct iked_sastats	sas;

	if (!sa->csa_loaded || sa->csa_spi.spi == 0)
		return (0);

	if (pfkey_map(pfkey_satype, sa->csa_saproto, &satype) == -1)
		return (-1);

	/* preserve the statistics */
	memset(&sas, 0, sizeof(sas));
	pfkey_sa_sastats(env, sa, &sas);

	if (pfkey_sa(env, satype, SADB_DELETE, sa) == -1 &&
	    pfkey_sa_check_exists(env, sa) == 0)
		return (-1);

	sa->csa_loaded = 0;

	sa->csa_ikesa->sa_stats.sas_ipackets += sas.sas_ipackets;
	sa->csa_ikesa->sa_stats.sas_opackets += sas.sas_opackets;
	sa->csa_ikesa->sa_stats.sas_ibytes += sas.sas_ibytes;
	sa->csa_ikesa->sa_stats.sas_obytes += sas.sas_obytes;
	sa->csa_ikesa->sa_stats.sas_idrops += sas.sas_idrops;
	sa->csa_ikesa->sa_stats.sas_odrops += sas.sas_odrops;

	return (0);
}

int
pfkey_flush(struct iked *env)
{
	struct sadb_msg smsg;
	struct iovec	iov[IOV_CNT];
	int		iov_cnt;

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = ++sadb_msg_seq;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = SADB_FLUSH;
	smsg.sadb_msg_satype = SADB_SATYPE_UNSPEC;

	iov_cnt = 0;

	iov[iov_cnt].iov_base = &smsg;
	iov[iov_cnt].iov_len = sizeof(smsg);
	iov_cnt++;

	return (pfkey_write(env, &smsg, iov, iov_cnt, NULL, NULL));
}

struct sadb_ident *
pfkey_id2ident(struct iked_id *id, unsigned int exttype)
{
	char			 idstr[IKED_ID_SIZE];
	unsigned int		 type;
	size_t			 len;
	struct sadb_ident	*sa_id;

	switch (id->id_type) {
	case IKEV2_ID_FQDN:
		type = SADB_IDENTTYPE_FQDN;
		break;
	case IKEV2_ID_UFQDN:
		type = SADB_IDENTTYPE_USERFQDN;
		break;
	case IKEV2_ID_IPV4:
	case IKEV2_ID_IPV6:
		type = SADB_IDENTTYPE_PREFIX;
		break;
	case IKEV2_ID_ASN1_DN:
		type = SADB_IDENTTYPE_ASN1_DN;
		break;
	case IKEV2_ID_ASN1_GN:
	case IKEV2_ID_KEY_ID:
	case IKEV2_ID_NONE:
	default:
		/* XXX not implemented/supported by PFKEY */
		return (NULL);
	}

	bzero(&idstr, sizeof(idstr));

	if (ikev2_print_id(id, idstr, sizeof(idstr)) == -1)
		return (NULL);

	len = ROUNDUP(strlen(idstr) + 1) + sizeof(*sa_id);
	if ((sa_id = calloc(1, len)) == NULL)
		return (NULL);

	strlcpy((char *)(sa_id + 1), idstr, ROUNDUP(strlen(idstr) + 1));
	sa_id->sadb_ident_type = type;
	sa_id->sadb_ident_len = len / 8;
	sa_id->sadb_ident_exttype = exttype;

	return (sa_id);
}

int
pfkey_socket(struct iked *env)
{
	int	 fd;

	if (privsep_process != PROC_PARENT)
		fatal("pfkey_socket: called from unprivileged process");

	if ((fd = socket(PF_KEY, SOCK_RAW, PF_KEY_V2)) == -1)
		fatal("pfkey_socket: failed to open PF_KEY socket");

	return (fd);
}

void
pfkey_init(struct iked *env, int fd)
{
	struct sadb_msg		smsg;
	struct iovec		iov;

	iked_rdomain = getrtable();

	/* Set up a timer to process messages deferred by the pfkey_reply */
	pfkey_timer_tv.tv_sec = 1;
	pfkey_timer_tv.tv_usec = 0;
	evtimer_set(&pfkey_timer_ev, pfkey_timer_cb, env);

	/* Register the pfkey socket event handler */
	env->sc_pfkey = fd;
	event_set(&env->sc_pfkeyev, env->sc_pfkey,
	    EV_READ|EV_PERSIST, pfkey_dispatch, env);
	event_add(&env->sc_pfkeyev, NULL);

	pfkey_flush(env);

	/* Register it to get ESP and AH acquires from the kernel */
	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = ++sadb_msg_seq;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = SADB_REGISTER;
	smsg.sadb_msg_satype = SADB_SATYPE_ESP;

	iov.iov_base = &smsg;
	iov.iov_len = sizeof(smsg);

	if (pfkey_write(env, &smsg, &iov, 1, NULL, NULL))
		fatal("pfkey_init: failed to set up ESP acquires");

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = ++sadb_msg_seq;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = SADB_REGISTER;
	smsg.sadb_msg_satype = SADB_SATYPE_AH;

	iov.iov_base = &smsg;
	iov.iov_len = sizeof(smsg);

	if (pfkey_write(env, &smsg, &iov, 1, NULL, NULL))
		fatal("pfkey_init: failed to set up AH acquires");
}

void *
pfkey_find_ext(uint8_t *data, ssize_t len, int type)
{
	struct sadb_ext	*ext = (struct sadb_ext *)(data +
	    sizeof(struct sadb_msg));

	while (ext && ((uint8_t *)ext - data < len)) {
		if (ext->sadb_ext_type == type)
			return (ext);
		ext = (struct sadb_ext *)((uint8_t *)ext +
		    ext->sadb_ext_len * PFKEYV2_CHUNK);
	}

	return (NULL);
}

void
pfkey_dispatch(int fd, short event, void *arg)
{
	struct iked		*env = (struct iked *)arg;
	struct pfkey_message	 pm, *pmp;
	struct sadb_msg		 hdr;
	ssize_t			 len;
	uint8_t			*data;

	if (recv(fd, &hdr, sizeof(hdr), MSG_PEEK) != sizeof(hdr)) {
		log_warn("%s: short recv", __func__);
		return;
	}

	if (hdr.sadb_msg_version != PF_KEY_V2) {
		log_warnx("%s: wrong pfkey version", __func__);
		return;
	}

	if ((data = reallocarray(NULL, hdr.sadb_msg_len, PFKEYV2_CHUNK))
	    == NULL) {
		log_warn("%s: malloc", __func__);
		return;
	}
	len = hdr.sadb_msg_len * PFKEYV2_CHUNK;

	if (read(fd, data, len) != len) {
		log_warn("%s: short read", __func__);
		free(data);
		return;
	}

	/* Try postponed requests first, so we do in-order processing */
	if (!SIMPLEQ_EMPTY(&pfkey_postponed))
		pfkey_timer_cb(0, 0, env);

	pm.pm_data = data;
	pm.pm_length = len;

	if (pfkey_process(env, &pm) == -1 &&
	    (pmp = calloc(1, sizeof(*pmp))) != NULL) {
		pmp->pm_data = data;
		pmp->pm_length = len;
		log_debug("%s: pfkey_process is busy, retry later", __func__);
		SIMPLEQ_INSERT_TAIL(&pfkey_postponed, pmp, pm_entry);
		evtimer_add(&pfkey_timer_ev, &pfkey_timer_tv);
	} else {
		free(data);
	}
}

void
pfkey_timer_cb(int unused, short event, void *arg)
{
	struct iked		*env = arg;
	struct pfkey_message	*pm;

	SIMPLEQ_INIT(&pfkey_retry);
	while (!SIMPLEQ_EMPTY(&pfkey_postponed)) {
		pm = SIMPLEQ_FIRST(&pfkey_postponed);
		SIMPLEQ_REMOVE_HEAD(&pfkey_postponed, pm_entry);
		if (pfkey_process(env, pm) == -1) {
			log_debug("%s: pfkey_process is busy, retry later",
			    __func__);
			SIMPLEQ_INSERT_TAIL(&pfkey_retry, pm, pm_entry);
		} else {
			free(pm->pm_data);
			free(pm);
		}
	}
	/* move from retry to postponed */
	SIMPLEQ_CONCAT(&pfkey_postponed, &pfkey_retry);
	if (!SIMPLEQ_EMPTY(&pfkey_postponed))
		evtimer_add(&pfkey_timer_ev, &pfkey_timer_tv);
}

/*
 * pfkey_process returns 0 if the message has been processed and -1 if
 * the system is busy and the message should be passed again, later.
 */
int
pfkey_process(struct iked *env, struct pfkey_message *pm)
{
	struct iked_spi		 spi;
	struct sadb_sa		*sa;
	struct sadb_lifetime	*sa_ltime;
	struct sadb_msg		*hdr;
	struct sadb_msg		 smsg;
	struct iked_addr	 peer;
	struct iked_flow	 flow;
	struct sadb_address	*sa_addr;
	struct sadb_protocol	*sa_proto;
	struct sadb_x_policy	 sa_pol;
	struct sockaddr		*ssrc, *sdst, *smask, *dmask, *speer;
	struct iovec		 iov[IOV_CNT];
	int			 ret = 0, iov_cnt;
	uint8_t			*reply;
	ssize_t			 rlen;
	const char		*errmsg = NULL;
	uint8_t			*data = pm->pm_data;
	ssize_t			 len = pm->pm_length;
	size_t			 slen;

	if (!env || !data || !len)
		return (0);

	hdr = (struct sadb_msg *)data;

	switch (hdr->sadb_msg_type) {
	case SADB_ACQUIRE:
		bzero(&flow, sizeof(flow));
		bzero(&peer, sizeof(peer));

		if ((sa_addr = pfkey_find_ext(data, len,
		    SADB_EXT_ADDRESS_DST)) == NULL) {
			log_debug("%s: no peer address", __func__);
			return (0);
		}
		speer = (struct sockaddr *)(sa_addr + 1);
		peer.addr_af = speer->sa_family;
		peer.addr_port = htons(socket_getport(speer));
		if ((slen = speer->sa_len) > sizeof(peer.addr)) {
			log_debug("%s: invalid peer address len", __func__);
			return (0);
		}
		memcpy(&peer.addr, speer, slen);
		if (socket_af((struct sockaddr *)&peer.addr,
		    peer.addr_port) == -1) {
			log_debug("%s: invalid address", __func__);
			return (0);
		}
		flow.flow_peer = &peer;

		log_debug("%s: acquire request (peer %s)", __func__,
		    print_addr(speer));

		/* get the matching flow */
		bzero(&smsg, sizeof(smsg));
		smsg.sadb_msg_version = PF_KEY_V2;
		smsg.sadb_msg_seq = ++sadb_msg_seq;
		smsg.sadb_msg_pid = getpid();
		smsg.sadb_msg_len = sizeof(smsg) / 8;
		smsg.sadb_msg_type = SADB_X_ASKPOLICY;

		iov_cnt = 0;

		iov[iov_cnt].iov_base = &smsg;
		iov[iov_cnt].iov_len = sizeof(smsg);
		iov_cnt++;

		bzero(&sa_pol, sizeof(sa_pol));
		sa_pol.sadb_x_policy_exttype = SADB_X_EXT_POLICY;
		sa_pol.sadb_x_policy_len = sizeof(sa_pol) / 8;
		sa_pol.sadb_x_policy_seq = hdr->sadb_msg_seq;

		iov[iov_cnt].iov_base = &sa_pol;
		iov[iov_cnt].iov_len = sizeof(sa_pol);
		smsg.sadb_msg_len += sizeof(sa_pol) / 8;
		iov_cnt++;

		if (pfkey_write(env, &smsg, iov, iov_cnt, &reply, &rlen)) {
			log_warnx("%s: failed to get a policy", __func__);
			return (0);
		}

		if ((sa_addr = pfkey_find_ext(reply, rlen,
		    SADB_X_EXT_SRC_FLOW)) == NULL) {
			errmsg = "flow source address";
			goto out;
		}
		ssrc = (struct sockaddr *)(sa_addr + 1);
		flow.flow_src.addr_af = ssrc->sa_family;
		flow.flow_src.addr_port = htons(socket_getport(ssrc));
		if ((slen = ssrc->sa_len) > sizeof(flow.flow_src.addr)) {
			log_debug("%s: invalid src address len", __func__);
			goto out;
		}
		memcpy(&flow.flow_src.addr, ssrc, slen);
		if (socket_af((struct sockaddr *)&flow.flow_src.addr,
		    flow.flow_src.addr_port) == -1) {
			log_debug("%s: invalid address", __func__);
			goto out;
		}

		if ((sa_addr = pfkey_find_ext(reply, rlen,
		    SADB_X_EXT_DST_FLOW)) == NULL) {
			errmsg = "flow destination address";
			goto out;
		}
		sdst = (struct sockaddr *)(sa_addr + 1);
		flow.flow_dst.addr_af = sdst->sa_family;
		flow.flow_dst.addr_port = htons(socket_getport(sdst));
		if ((slen = sdst->sa_len) > sizeof(flow.flow_dst.addr)) {
			log_debug("%s: invalid dst address len", __func__);
			goto out;
		}
		memcpy(&flow.flow_dst.addr, sdst, slen);
		if (socket_af((struct sockaddr *)&flow.flow_dst.addr,
		    flow.flow_dst.addr_port) == -1) {
			log_debug("%s: invalid address", __func__);
			goto out;
		}

		if ((sa_addr = pfkey_find_ext(reply, rlen,
		    SADB_X_EXT_SRC_MASK)) == NULL) {
			errmsg = "flow source mask";
			goto out;
		}
		smask = (struct sockaddr *)(sa_addr + 1);
		switch (smask->sa_family) {
		case AF_INET:
			flow.flow_src.addr_mask =
			    mask2prefixlen((struct sockaddr *)smask);
			if (flow.flow_src.addr_mask != 32)
				flow.flow_src.addr_net = 1;
			break;
		case AF_INET6:
			flow.flow_src.addr_mask =
			    mask2prefixlen6((struct sockaddr *)smask);
			if (flow.flow_src.addr_mask != 128)
				flow.flow_src.addr_net = 1;
			break;
		default:
			log_debug("%s: bad address family", __func__);
			goto out;
		}

		if ((sa_addr = pfkey_find_ext(reply, rlen,
		    SADB_X_EXT_DST_MASK)) == NULL) {
			errmsg = "flow destination mask";
			goto out;
		}
		dmask = (struct sockaddr *)(sa_addr + 1);
		switch (dmask->sa_family) {
		case AF_INET:
			flow.flow_dst.addr_mask =
			    mask2prefixlen((struct sockaddr *)dmask);
			if (flow.flow_src.addr_mask != 32)
				flow.flow_src.addr_net = 1;
			break;
		case AF_INET6:
			flow.flow_dst.addr_mask =
			    mask2prefixlen6((struct sockaddr *)dmask);
			if (flow.flow_src.addr_mask != 128)
				flow.flow_src.addr_net = 1;
			break;
		default:
			log_debug("%s: bad address family", __func__);
			goto out;
		}

		switch (hdr->sadb_msg_satype) {
		case SADB_SATYPE_AH:
			flow.flow_saproto = IKEV2_SAPROTO_AH;
			break;
		case SADB_SATYPE_ESP:
			flow.flow_saproto = IKEV2_SAPROTO_ESP;
			break;
		case SADB_X_SATYPE_IPCOMP:
			flow.flow_saproto = IKEV2_SAPROTO_IPCOMP;
			break;
		}

		if ((sa_proto = pfkey_find_ext(reply, rlen,
		    SADB_X_EXT_FLOW_TYPE)) == NULL) {
			errmsg = "flow protocol";
			goto out;
		}
		flow.flow_dir = sa_proto->sadb_protocol_direction;
		flow.flow_rdomain = -1;	/* XXX get from kernel */

		log_debug("%s: flow %s from %s/%s to %s/%s via %s", __func__,
		    flow.flow_dir == IPSP_DIRECTION_IN ? "in" : "out",
		    print_addr(ssrc), print_addr(smask),
		    print_addr(sdst), print_addr(dmask),
		    print_addr(speer));

		ret = ikev2_child_sa_acquire(env, &flow);

out:
		if (errmsg)
			log_warnx("%s: %s wasn't found", __func__, errmsg);
		free(reply);
		break;

	case SADB_EXPIRE:
		if ((sa = pfkey_find_ext(data, len, SADB_EXT_SA)) == NULL) {
			log_warnx("%s: SA extension wasn't found", __func__);
			return (0);
		}
		if ((sa_ltime = pfkey_find_ext(data, len,
			SADB_EXT_LIFETIME_SOFT)) == NULL &&
		    (sa_ltime = pfkey_find_ext(data, len,
			SADB_EXT_LIFETIME_HARD)) == NULL) {
			log_warnx("%s: lifetime extension wasn't found",
			    __func__);
			return (0);
		}
		spi.spi = ntohl(sa->sadb_sa_spi);
		spi.spi_size = 4;
		switch (hdr->sadb_msg_satype) {
		case SADB_SATYPE_AH:
			spi.spi_protoid = IKEV2_SAPROTO_AH;
			break;
		case SADB_SATYPE_ESP:
			spi.spi_protoid = IKEV2_SAPROTO_ESP;
			break;
		case SADB_X_SATYPE_IPCOMP:
			spi.spi_size = 2;
			spi.spi_protoid = IKEV2_SAPROTO_IPCOMP;
			break;
		default:
			log_warnx("%s: unsupported SA type %d spi %s",
			    __func__, hdr->sadb_msg_satype,
			    print_spi(spi.spi, spi.spi_size));
			return (0);
		}

		log_debug("%s: SA %s is expired, pending %s", __func__,
		    print_spi(spi.spi, spi.spi_size),
		    sa_ltime->sadb_lifetime_exttype == SADB_EXT_LIFETIME_SOFT ?
		    "rekeying" : "deletion");

		if (sa_ltime->sadb_lifetime_exttype == SADB_EXT_LIFETIME_SOFT)
			ret = ikev2_child_sa_rekey(env, &spi);
		else
			ret = ikev2_child_sa_drop(env, &spi);
		break;
	}
	return (ret);
}
