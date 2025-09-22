/*	$OpenBSD: pfkey.c,v 1.64 2023/10/09 15:32:14 tobhe Exp $	*/
/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2003, 2004 Markus Friedl <markus@openbsd.org>
 * Copyright (c) 2004, 2005 Hans-Joerg Hoexer <hshoexer@openbsd.org>
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
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_ipsp.h>
#include <net/pfkeyv2.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>

#include "ipsecctl.h"
#include "pfkey.h"

#define ROUNDUP(x) (((x) + (PFKEYV2_CHUNK - 1)) & ~(PFKEYV2_CHUNK - 1))
#define IOV_CNT 20

static int	fd;
static u_int32_t sadb_msg_seq = 1;

static int	pfkey_flow(int, u_int8_t, u_int8_t, u_int8_t, u_int8_t,
		    struct ipsec_addr_wrap *, u_int16_t,
		    struct ipsec_addr_wrap *, u_int16_t,
		    struct ipsec_addr_wrap *, struct ipsec_addr_wrap *,
		    struct ipsec_auth *, u_int8_t);
static int	pfkey_sa(int, u_int8_t, u_int8_t, u_int32_t,
		    struct ipsec_addr_wrap *, struct ipsec_addr_wrap *,
		    u_int8_t, u_int16_t,
		    struct ipsec_transforms *, struct ipsec_key *,
		    struct ipsec_key *, u_int8_t);
static int	pfkey_sabundle(int, u_int8_t, u_int8_t, u_int8_t,
		    struct ipsec_addr_wrap *, u_int32_t,
		    struct ipsec_addr_wrap *, u_int32_t);
static int	pfkey_reply(int, u_int8_t **, ssize_t *);
int		pfkey_parse(struct sadb_msg *, struct ipsec_rule *);
int		pfkey_ipsec_flush(void);
int		pfkey_ipsec_establish(int, struct ipsec_rule *);
int		pfkey_init(void);

static int
pfkey_flow(int sd, u_int8_t satype, u_int8_t action, u_int8_t direction,
    u_int8_t proto, struct ipsec_addr_wrap *src, u_int16_t sport,
    struct ipsec_addr_wrap *dst, u_int16_t dport,
    struct ipsec_addr_wrap *local, struct ipsec_addr_wrap *peer,
    struct ipsec_auth *auth, u_int8_t flowtype)
{
	struct sadb_msg		 smsg;
	struct sadb_address	 sa_src, sa_dst, sa_local, sa_peer, sa_smask,
				 sa_dmask;
	struct sadb_protocol	 sa_flowtype, sa_protocol;
	struct sadb_ident	*sa_srcid, *sa_dstid;
	struct sockaddr_storage	 ssrc, sdst, slocal, speer, smask, dmask;
	struct iovec		 iov[IOV_CNT];
	ssize_t			 n;
	int			 iov_cnt, len, ret = 0;

	sa_srcid = sa_dstid = NULL;

	bzero(&ssrc, sizeof(ssrc));
	bzero(&smask, sizeof(smask));
	ssrc.ss_family = smask.ss_family = src->af;
	switch (src->af) {
	case AF_INET:
		((struct sockaddr_in *)&ssrc)->sin_addr = src->address.v4;
		ssrc.ss_len = sizeof(struct sockaddr_in);
		((struct sockaddr_in *)&smask)->sin_addr = src->mask.v4;
		if (sport) {
			((struct sockaddr_in *)&ssrc)->sin_port = sport;
			((struct sockaddr_in *)&smask)->sin_port = 0xffff;
		}
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)&ssrc)->sin6_addr = src->address.v6;
		ssrc.ss_len = sizeof(struct sockaddr_in6);
		((struct sockaddr_in6 *)&smask)->sin6_addr = src->mask.v6;
		if (sport) {
			((struct sockaddr_in6 *)&ssrc)->sin6_port = sport;
			((struct sockaddr_in6 *)&smask)->sin6_port = 0xffff;
		}
		break;
	default:
		warnx("unsupported address family %d", src->af);
		return -1;
	}
	smask.ss_len = ssrc.ss_len;

	bzero(&sdst, sizeof(sdst));
	bzero(&dmask, sizeof(dmask));
	sdst.ss_family = dmask.ss_family = dst->af;
	switch (dst->af) {
	case AF_INET:
		((struct sockaddr_in *)&sdst)->sin_addr = dst->address.v4;
		sdst.ss_len = sizeof(struct sockaddr_in);
		((struct sockaddr_in *)&dmask)->sin_addr = dst->mask.v4;
		if (dport) {
			((struct sockaddr_in *)&sdst)->sin_port = dport;
			((struct sockaddr_in *)&dmask)->sin_port = 0xffff;
		}
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)&sdst)->sin6_addr = dst->address.v6;
		sdst.ss_len = sizeof(struct sockaddr_in6);
		((struct sockaddr_in6 *)&dmask)->sin6_addr = dst->mask.v6;
		if (dport) {
			((struct sockaddr_in6 *)&sdst)->sin6_port = dport;
			((struct sockaddr_in6 *)&dmask)->sin6_port = 0xffff;
		}
		break;
	default:
		warnx("unsupported address family %d", dst->af);
		return -1;
	}
	dmask.ss_len = sdst.ss_len;

	bzero(&slocal, sizeof(slocal));
	if (local) {
		slocal.ss_family = local->af;
		switch (local->af) {
		case AF_INET:
			((struct sockaddr_in *)&slocal)->sin_addr =
			    local->address.v4;
			slocal.ss_len = sizeof(struct sockaddr_in);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)&slocal)->sin6_addr =
			    local->address.v6;
			slocal.ss_len = sizeof(struct sockaddr_in6);
			break;
		default:
			warnx("unsupported address family %d", local->af);
			return -1;
		}
	}

	bzero(&speer, sizeof(speer));
	if (peer) {
		speer.ss_family = peer->af;
		switch (peer->af) {
		case AF_INET:
			((struct sockaddr_in *)&speer)->sin_addr =
			    peer->address.v4;
			speer.ss_len = sizeof(struct sockaddr_in);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)&speer)->sin6_addr =
			    peer->address.v6;
			speer.ss_len = sizeof(struct sockaddr_in6);
			break;
		default:
			warnx("unsupported address family %d", peer->af);
			return -1;
		}
	}

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = sadb_msg_seq++;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = action;
	smsg.sadb_msg_satype = satype;

	bzero(&sa_flowtype, sizeof(sa_flowtype));
	sa_flowtype.sadb_protocol_exttype = SADB_X_EXT_FLOW_TYPE;
	sa_flowtype.sadb_protocol_len = sizeof(sa_flowtype) / 8;
	sa_flowtype.sadb_protocol_direction = direction;

	switch (flowtype) {
	case TYPE_USE:
		sa_flowtype.sadb_protocol_proto = SADB_X_FLOW_TYPE_USE;
		break;
	case TYPE_ACQUIRE:
		sa_flowtype.sadb_protocol_proto = SADB_X_FLOW_TYPE_ACQUIRE;
		break;
	case TYPE_REQUIRE:
		sa_flowtype.sadb_protocol_proto = SADB_X_FLOW_TYPE_REQUIRE;
		break;
	case TYPE_DENY:
		sa_flowtype.sadb_protocol_proto = SADB_X_FLOW_TYPE_DENY;
		break;
	case TYPE_BYPASS:
		sa_flowtype.sadb_protocol_proto = SADB_X_FLOW_TYPE_BYPASS;
		break;
	case TYPE_DONTACQ:
		sa_flowtype.sadb_protocol_proto = SADB_X_FLOW_TYPE_DONTACQ;
		break;
	default:
		warnx("unsupported flowtype %d", flowtype);
		return -1;
	}

	bzero(&sa_protocol, sizeof(sa_protocol));
	sa_protocol.sadb_protocol_exttype = SADB_X_EXT_PROTOCOL;
	sa_protocol.sadb_protocol_len = sizeof(sa_protocol) / 8;
	sa_protocol.sadb_protocol_direction = 0;
	sa_protocol.sadb_protocol_proto = proto;

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

	if (local) {
		bzero(&sa_local, sizeof(sa_local));
		sa_local.sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
		sa_local.sadb_address_len =
		    (sizeof(sa_local) + ROUNDUP(slocal.ss_len)) / 8;
	}
	if (peer) {
		bzero(&sa_peer, sizeof(sa_peer));
		sa_peer.sadb_address_exttype = SADB_EXT_ADDRESS_DST;
		sa_peer.sadb_address_len =
		    (sizeof(sa_peer) + ROUNDUP(speer.ss_len)) / 8;
	}

	if (auth && auth->srcid) {
		len = ROUNDUP(strlen(auth->srcid) + 1) + sizeof(*sa_srcid);

		sa_srcid = calloc(len, sizeof(u_int8_t));
		if (sa_srcid == NULL)
			err(1, "pfkey_flow: calloc");

		sa_srcid->sadb_ident_type = auth->srcid_type;
		sa_srcid->sadb_ident_len = len / 8;
		sa_srcid->sadb_ident_exttype = SADB_EXT_IDENTITY_SRC;

		strlcpy((char *)(sa_srcid + 1), auth->srcid,
		    ROUNDUP(strlen(auth->srcid) + 1));
	}
	if (auth && auth->dstid) {
		len = ROUNDUP(strlen(auth->dstid) + 1) + sizeof(*sa_dstid);

		sa_dstid = calloc(len, sizeof(u_int8_t));
		if (sa_dstid == NULL)
			err(1, "pfkey_flow: calloc");

		sa_dstid->sadb_ident_type = auth->dstid_type;
		sa_dstid->sadb_ident_len = len / 8;
		sa_dstid->sadb_ident_exttype = SADB_EXT_IDENTITY_DST;

		strlcpy((char *)(sa_dstid + 1), auth->dstid,
		    ROUNDUP(strlen(auth->dstid) + 1));
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

	/* local ip */
	if (local) {
		iov[iov_cnt].iov_base = &sa_local;
		iov[iov_cnt].iov_len = sizeof(sa_local);
		iov_cnt++;
		iov[iov_cnt].iov_base = &slocal;
		iov[iov_cnt].iov_len = ROUNDUP(slocal.ss_len);
		smsg.sadb_msg_len += sa_local.sadb_address_len;
		iov_cnt++;
	}

	/* remote peer */
	if (peer) {
		iov[iov_cnt].iov_base = &sa_peer;
		iov[iov_cnt].iov_len = sizeof(sa_peer);
		iov_cnt++;
		iov[iov_cnt].iov_base = &speer;
		iov[iov_cnt].iov_len = ROUNDUP(speer.ss_len);
		smsg.sadb_msg_len += sa_peer.sadb_address_len;
		iov_cnt++;
	}

	/* src addr */
	iov[iov_cnt].iov_base = &sa_src;
	iov[iov_cnt].iov_len = sizeof(sa_src);
	iov_cnt++;
	iov[iov_cnt].iov_base = &ssrc;
	iov[iov_cnt].iov_len = ROUNDUP(ssrc.ss_len);
	smsg.sadb_msg_len += sa_src.sadb_address_len;
	iov_cnt++;

	/* src mask */
	iov[iov_cnt].iov_base = &sa_smask;
	iov[iov_cnt].iov_len = sizeof(sa_smask);
	iov_cnt++;
	iov[iov_cnt].iov_base = &smask;
	iov[iov_cnt].iov_len = ROUNDUP(smask.ss_len);
	smsg.sadb_msg_len += sa_smask.sadb_address_len;
	iov_cnt++;

	/* dest addr */
	iov[iov_cnt].iov_base = &sa_dst;
	iov[iov_cnt].iov_len = sizeof(sa_dst);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst;
	iov[iov_cnt].iov_len = ROUNDUP(sdst.ss_len);
	smsg.sadb_msg_len += sa_dst.sadb_address_len;
	iov_cnt++;

	/* dst mask */
	iov[iov_cnt].iov_base = &sa_dmask;
	iov[iov_cnt].iov_len = sizeof(sa_dmask);
	iov_cnt++;
	iov[iov_cnt].iov_base = &dmask;
	iov[iov_cnt].iov_len = ROUNDUP(dmask.ss_len);
	smsg.sadb_msg_len += sa_dmask.sadb_address_len;
	iov_cnt++;

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
	len = smsg.sadb_msg_len * 8;

	do {
		n = writev(sd, iov, iov_cnt);
	} while (n == -1 && (errno == EAGAIN || errno == EINTR));
	if (n == -1) {
		warn("writev failed");
		ret = -1;
	}

	free(sa_srcid);
	free(sa_dstid);

	return ret;
}

static int
pfkey_sa(int sd, u_int8_t satype, u_int8_t action, u_int32_t spi,
    struct ipsec_addr_wrap *src, struct ipsec_addr_wrap *dst,
    u_int8_t encap, u_int16_t dport,
    struct ipsec_transforms *xfs, struct ipsec_key *authkey,
    struct ipsec_key *enckey, u_int8_t tmode)
{
	struct sadb_msg		smsg;
	struct sadb_sa		sa;
	struct sadb_address	sa_src, sa_dst;
	struct sadb_key		sa_authkey, sa_enckey;
	struct sadb_x_udpencap	udpencap;
	struct sockaddr_storage	ssrc, sdst;
	struct iovec		iov[IOV_CNT];
	ssize_t			n;
	int			iov_cnt, len, ret = 0;

	bzero(&ssrc, sizeof(ssrc));
	ssrc.ss_family = src->af;
	switch (src->af) {
	case AF_INET:
		((struct sockaddr_in *)&ssrc)->sin_addr = src->address.v4;
		ssrc.ss_len = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)&ssrc)->sin6_addr = src->address.v6;
		ssrc.ss_len = sizeof(struct sockaddr_in6);
		break;
	default:
		warnx("unsupported address family %d", src->af);
		return -1;
	}

	bzero(&sdst, sizeof(sdst));
	sdst.ss_family = dst->af;
	switch (dst->af) {
	case AF_INET:
		((struct sockaddr_in *)&sdst)->sin_addr = dst->address.v4;
		sdst.ss_len = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)&sdst)->sin6_addr = dst->address.v6;
		sdst.ss_len = sizeof(struct sockaddr_in6);
		break;
	default:
		warnx("unsupported address family %d", dst->af);
		return -1;
	}

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = sadb_msg_seq++;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = action;
	smsg.sadb_msg_satype = satype;

	bzero(&sa, sizeof(sa));
	sa.sadb_sa_len = sizeof(sa) / 8;
	sa.sadb_sa_exttype = SADB_EXT_SA;
	sa.sadb_sa_spi = htonl(spi);
	sa.sadb_sa_state = SADB_SASTATE_MATURE;

	if (satype != SADB_X_SATYPE_IPIP && tmode == IPSEC_TUNNEL)
		sa.sadb_sa_flags |= SADB_X_SAFLAGS_TUNNEL;

	if (xfs && xfs->authxf) {
		switch (xfs->authxf->id) {
		case AUTHXF_NONE:
			break;
		case AUTHXF_HMAC_MD5:
			sa.sadb_sa_auth = SADB_AALG_MD5HMAC;
			break;
		case AUTHXF_HMAC_RIPEMD160:
			sa.sadb_sa_auth = SADB_X_AALG_RIPEMD160HMAC;
			break;
		case AUTHXF_HMAC_SHA1:
			sa.sadb_sa_auth = SADB_AALG_SHA1HMAC;
			break;
		case AUTHXF_HMAC_SHA2_256:
			sa.sadb_sa_auth = SADB_X_AALG_SHA2_256;
			break;
		case AUTHXF_HMAC_SHA2_384:
			sa.sadb_sa_auth = SADB_X_AALG_SHA2_384;
			break;
		case AUTHXF_HMAC_SHA2_512:
			sa.sadb_sa_auth = SADB_X_AALG_SHA2_512;
			break;
		default:
			warnx("unsupported authentication algorithm %d",
			    xfs->authxf->id);
		}
	}
	if (xfs && xfs->encxf) {
		switch (xfs->encxf->id) {
		case ENCXF_NONE:
			break;
		case ENCXF_3DES_CBC:
			sa.sadb_sa_encrypt = SADB_EALG_3DESCBC;
			break;
		case ENCXF_AES:
		case ENCXF_AES_128:
		case ENCXF_AES_192:
		case ENCXF_AES_256:
			sa.sadb_sa_encrypt = SADB_X_EALG_AES;
			break;
		case ENCXF_AESCTR:
		case ENCXF_AES_128_CTR:
		case ENCXF_AES_192_CTR:
		case ENCXF_AES_256_CTR:
			sa.sadb_sa_encrypt = SADB_X_EALG_AESCTR;
			break;
		case ENCXF_AES_128_GCM:
		case ENCXF_AES_192_GCM:
		case ENCXF_AES_256_GCM:
			sa.sadb_sa_encrypt = SADB_X_EALG_AESGCM16;
			break;
		case ENCXF_AES_128_GMAC:
		case ENCXF_AES_192_GMAC:
		case ENCXF_AES_256_GMAC:
			sa.sadb_sa_encrypt = SADB_X_EALG_AESGMAC;
			break;
		case ENCXF_BLOWFISH:
			sa.sadb_sa_encrypt = SADB_X_EALG_BLF;
			break;
		case ENCXF_CAST128:
			sa.sadb_sa_encrypt = SADB_X_EALG_CAST;
			break;
		case ENCXF_NULL:
			sa.sadb_sa_encrypt = SADB_EALG_NULL;
			break;
		default:
			warnx("unsupported encryption algorithm %d",
			    xfs->encxf->id);
		}
	}
	if (xfs && xfs->compxf) {
		switch (xfs->compxf->id) {
		case COMPXF_DEFLATE:
			sa.sadb_sa_encrypt = SADB_X_CALG_DEFLATE;
			break;
		default:
			warnx("unsupported compression algorithm %d",
			    xfs->compxf->id);
		}
	}

	bzero(&sa_src, sizeof(sa_src));
	sa_src.sadb_address_len = (sizeof(sa_src) + ROUNDUP(ssrc.ss_len)) / 8;
	sa_src.sadb_address_exttype = SADB_EXT_ADDRESS_SRC;

	bzero(&sa_dst, sizeof(sa_dst));
	sa_dst.sadb_address_len = (sizeof(sa_dst) + ROUNDUP(sdst.ss_len)) / 8;
	sa_dst.sadb_address_exttype = SADB_EXT_ADDRESS_DST;

	if (encap) {
		sa.sadb_sa_flags |= SADB_X_SAFLAGS_UDPENCAP;
		udpencap.sadb_x_udpencap_exttype = SADB_X_EXT_UDPENCAP;
		udpencap.sadb_x_udpencap_len = sizeof(udpencap) / 8;
		udpencap.sadb_x_udpencap_port = htons(dport);
	}
	if (action == SADB_ADD && !authkey && !enckey && satype !=
	    SADB_X_SATYPE_IPCOMP && satype != SADB_X_SATYPE_IPIP) { /* XXX ENCNULL */
		warnx("no key specified");
		return -1;
	}
	if (authkey) {
		bzero(&sa_authkey, sizeof(sa_authkey));
		sa_authkey.sadb_key_len = (sizeof(sa_authkey) +
		    ((authkey->len + 7) / 8) * 8) / 8;
		sa_authkey.sadb_key_exttype = SADB_EXT_KEY_AUTH;
		sa_authkey.sadb_key_bits = 8 * authkey->len;
	}
	if (enckey) {
		bzero(&sa_enckey, sizeof(sa_enckey));
		sa_enckey.sadb_key_len = (sizeof(sa_enckey) +
		    ((enckey->len + 7) / 8) * 8) / 8;
		sa_enckey.sadb_key_exttype = SADB_EXT_KEY_ENCRYPT;
		sa_enckey.sadb_key_bits = 8 * enckey->len;
	}

	iov_cnt = 0;

	/* header */
	iov[iov_cnt].iov_base = &smsg;
	iov[iov_cnt].iov_len = sizeof(smsg);
	iov_cnt++;

	/* sa */
	iov[iov_cnt].iov_base = &sa;
	iov[iov_cnt].iov_len = sizeof(sa);
	smsg.sadb_msg_len += sa.sadb_sa_len;
	iov_cnt++;

	/* src addr */
	iov[iov_cnt].iov_base = &sa_src;
	iov[iov_cnt].iov_len = sizeof(sa_src);
	iov_cnt++;
	iov[iov_cnt].iov_base = &ssrc;
	iov[iov_cnt].iov_len = ROUNDUP(ssrc.ss_len);
	smsg.sadb_msg_len += sa_src.sadb_address_len;
	iov_cnt++;

	/* dst addr */
	iov[iov_cnt].iov_base = &sa_dst;
	iov[iov_cnt].iov_len = sizeof(sa_dst);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst;
	iov[iov_cnt].iov_len = ROUNDUP(sdst.ss_len);
	smsg.sadb_msg_len += sa_dst.sadb_address_len;
	iov_cnt++;

	if (encap) {
		iov[iov_cnt].iov_base = &udpencap;
		iov[iov_cnt].iov_len = sizeof(udpencap);
		smsg.sadb_msg_len += udpencap.sadb_x_udpencap_len;
		iov_cnt++;
	}
	if (authkey) {
		/* authentication key */
		iov[iov_cnt].iov_base = &sa_authkey;
		iov[iov_cnt].iov_len = sizeof(sa_authkey);
		iov_cnt++;
		iov[iov_cnt].iov_base = authkey->data;
		iov[iov_cnt].iov_len = ((authkey->len + 7) / 8) * 8;
		smsg.sadb_msg_len += sa_authkey.sadb_key_len;
		iov_cnt++;
	}
	if (enckey) {
		/* encryption key */
		iov[iov_cnt].iov_base = &sa_enckey;
		iov[iov_cnt].iov_len = sizeof(sa_enckey);
		iov_cnt++;
		iov[iov_cnt].iov_base = enckey->data;
		iov[iov_cnt].iov_len = ((enckey->len + 7) / 8) * 8;
		smsg.sadb_msg_len += sa_enckey.sadb_key_len;
		iov_cnt++;
	}

	len = smsg.sadb_msg_len * 8;
	if ((n = writev(sd, iov, iov_cnt)) == -1) {
		warn("writev failed");
		ret = -1;
	} else if (n != len) {
		warnx("short write");
		ret = -1;
	}

	return ret;
}

static int
pfkey_sabundle(int sd, u_int8_t satype, u_int8_t satype2, u_int8_t action,
    struct ipsec_addr_wrap *dst, u_int32_t spi, struct ipsec_addr_wrap *dst2,
    u_int32_t spi2)
{
	struct sadb_msg		smsg;
	struct sadb_sa		sa1, sa2;
	struct sadb_address	sa_dst, sa_dst2;
	struct sockaddr_storage	sdst, sdst2;
	struct sadb_protocol	sa_proto;
	struct iovec		iov[IOV_CNT];
	ssize_t			n;
	int			iov_cnt, len, ret = 0;

	bzero(&sdst, sizeof(sdst));
	sdst.ss_family = dst->af;
	switch (dst->af) {
	case AF_INET:
		((struct sockaddr_in *)&sdst)->sin_addr = dst->address.v4;
		sdst.ss_len = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)&sdst)->sin6_addr = dst->address.v6;
		sdst.ss_len = sizeof(struct sockaddr_in6);
		break;
	default:
		warnx("unsupported address family %d", dst->af);
		return -1;
	}

	bzero(&sdst2, sizeof(sdst2));
	sdst2.ss_family = dst2->af;
	switch (dst2->af) {
	case AF_INET:
		((struct sockaddr_in *)&sdst2)->sin_addr = dst2->address.v4;
		sdst2.ss_len = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)&sdst2)->sin6_addr = dst2->address.v6;
		sdst2.ss_len = sizeof(struct sockaddr_in6);
		break;
	default:
		warnx("unsupported address family %d", dst2->af);
		return -1;
	}

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = sadb_msg_seq++;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = action;
	smsg.sadb_msg_satype = satype;

	bzero(&sa1, sizeof(sa1));
	sa1.sadb_sa_len = sizeof(sa1) / 8;
	sa1.sadb_sa_exttype = SADB_EXT_SA;
	sa1.sadb_sa_spi = htonl(spi);
	sa1.sadb_sa_state = SADB_SASTATE_MATURE;

	bzero(&sa2, sizeof(sa2));
	sa2.sadb_sa_len = sizeof(sa2) / 8;
	sa2.sadb_sa_exttype = SADB_X_EXT_SA2;
	sa2.sadb_sa_spi = htonl(spi2);
	sa2.sadb_sa_state = SADB_SASTATE_MATURE;
	iov_cnt = 0;

	bzero(&sa_dst, sizeof(sa_dst));
	sa_dst.sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	sa_dst.sadb_address_len = (sizeof(sa_dst) + ROUNDUP(sdst.ss_len)) / 8;

	bzero(&sa_dst2, sizeof(sa_dst2));
	sa_dst2.sadb_address_exttype = SADB_X_EXT_DST2;
	sa_dst2.sadb_address_len = (sizeof(sa_dst2) + ROUNDUP(sdst2.ss_len)) / 8;

	bzero(&sa_proto, sizeof(sa_proto));
	sa_proto.sadb_protocol_exttype = SADB_X_EXT_SATYPE2;
	sa_proto.sadb_protocol_len = sizeof(sa_proto) / 8;
	sa_proto.sadb_protocol_direction = 0;
	sa_proto.sadb_protocol_proto = satype2;

	/* header */
	iov[iov_cnt].iov_base = &smsg;
	iov[iov_cnt].iov_len = sizeof(smsg);
	iov_cnt++;

	/* sa */
	iov[iov_cnt].iov_base = &sa1;
	iov[iov_cnt].iov_len = sizeof(sa1);
	smsg.sadb_msg_len += sa1.sadb_sa_len;
	iov_cnt++;

	/* dst addr */
	iov[iov_cnt].iov_base = &sa_dst;
	iov[iov_cnt].iov_len = sizeof(sa_dst);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst;
	iov[iov_cnt].iov_len = ROUNDUP(sdst.ss_len);
	smsg.sadb_msg_len += sa_dst.sadb_address_len;
	iov_cnt++;

	/* second sa */
	iov[iov_cnt].iov_base = &sa2;
	iov[iov_cnt].iov_len = sizeof(sa2);
	smsg.sadb_msg_len += sa2.sadb_sa_len;
	iov_cnt++;

	/* second dst addr */
	iov[iov_cnt].iov_base = &sa_dst2;
	iov[iov_cnt].iov_len = sizeof(sa_dst2);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst2;
	iov[iov_cnt].iov_len = ROUNDUP(sdst2.ss_len);
	smsg.sadb_msg_len += sa_dst2.sadb_address_len;
	iov_cnt++;

	/* SA type */
	iov[iov_cnt].iov_base = &sa_proto;
	iov[iov_cnt].iov_len = sizeof(sa_proto);
	smsg.sadb_msg_len += sa_proto.sadb_protocol_len;
	iov_cnt++;

	len = smsg.sadb_msg_len * 8;
	if ((n = writev(sd, iov, iov_cnt)) == -1) {
		warn("writev failed");
		ret = -1;
	} else if (n != len) {
		warnx("short write");
		ret = -1;
	}

	return (ret);
}

static int
pfkey_reply(int sd, u_int8_t **datap, ssize_t *lenp)
{
	struct sadb_msg	 hdr;
	ssize_t		 len;
	u_int8_t	*data;

	if (recv(sd, &hdr, sizeof(hdr), MSG_PEEK) != sizeof(hdr)) {
		warnx("short read");
		return -1;
	}
	len = hdr.sadb_msg_len * PFKEYV2_CHUNK;
	if ((data = malloc(len)) == NULL)
		err(1, "pfkey_reply: malloc");
	if (read(sd, data, len) != len) {
		warn("PF_KEY short read");
		freezero(data, len);
		return -1;
	}
	if (datap) {
		*datap = data;
		if (lenp)
			*lenp = len;
	} else {
		freezero(data, len);
	}
	if (datap == NULL && hdr.sadb_msg_errno != 0) {
		errno = hdr.sadb_msg_errno;
		if (errno != EEXIST) {
			warn("PF_KEY failed");
			return -1;
		}
	}
	return 0;
}

int
pfkey_parse(struct sadb_msg *msg, struct ipsec_rule *rule)
{
	struct sadb_ext		*ext;
	struct sadb_address	*saddr;
	struct sadb_protocol	*sproto;
	struct sadb_ident	*sident;
	struct sockaddr		*sa;
	struct sockaddr_in	*sa_in;
	struct sockaddr_in6	*sa_in6;
	int			 len;

	switch (msg->sadb_msg_satype) {
	case SADB_SATYPE_ESP:
		rule->satype = IPSEC_ESP;
		break;
	case SADB_SATYPE_AH:
		rule->satype = IPSEC_AH;
		break;
	case SADB_X_SATYPE_IPCOMP:
		rule->satype = IPSEC_IPCOMP;
		break;
	case SADB_X_SATYPE_IPIP:
		rule->satype = IPSEC_IPIP;
		break;
	default:
		return (1);
	}

	for (ext = (struct sadb_ext *)(msg + 1);
	    (size_t)((u_int8_t *)ext - (u_int8_t *)msg) <
	    msg->sadb_msg_len * PFKEYV2_CHUNK && ext->sadb_ext_len > 0;
	    ext = (struct sadb_ext *)((u_int8_t *)ext +
	    ext->sadb_ext_len * PFKEYV2_CHUNK)) {
		switch (ext->sadb_ext_type) {
		case SADB_EXT_ADDRESS_SRC:
			saddr = (struct sadb_address *)ext;
			sa = (struct sockaddr *)(saddr + 1);

			rule->local = calloc(1, sizeof(struct ipsec_addr_wrap));
			if (rule->local == NULL)
				err(1, "pfkey_parse: calloc");

			rule->local->af = sa->sa_family;
			switch (sa->sa_family) {
			case AF_INET:
				bcopy(&((struct sockaddr_in *)sa)->sin_addr,
				    &rule->local->address.v4,
				    sizeof(struct in_addr));
				set_ipmask(rule->local, 32);
				break;
			case AF_INET6:
				bcopy(&((struct sockaddr_in6 *)sa)->sin6_addr,
				    &rule->local->address.v6,
				    sizeof(struct in6_addr));
				set_ipmask(rule->local, 128);
				break;
			default:
				return (1);
			}
			break;


		case SADB_EXT_ADDRESS_DST:
			saddr = (struct sadb_address *)ext;
			sa = (struct sockaddr *)(saddr + 1);

			rule->peer = calloc(1, sizeof(struct ipsec_addr_wrap));
			if (rule->peer == NULL)
				err(1, "pfkey_parse: calloc");

			rule->peer->af = sa->sa_family;
			switch (sa->sa_family) {
			case AF_INET:
				bcopy(&((struct sockaddr_in *)sa)->sin_addr,
				    &rule->peer->address.v4,
				    sizeof(struct in_addr));
				set_ipmask(rule->peer, 32);
				break;
			case AF_INET6:
				bcopy(&((struct sockaddr_in6 *)sa)->sin6_addr,
				    &rule->peer->address.v6,
				    sizeof(struct in6_addr));
				set_ipmask(rule->peer, 128);
				break;
			default:
				return (1);
			}
			break;

		case SADB_EXT_IDENTITY_SRC:
			sident = (struct sadb_ident *)ext;
			len = (sident->sadb_ident_len * sizeof(uint64_t)) -
			    sizeof(struct sadb_ident);

			if (rule->auth == NULL) {
				rule->auth = calloc(1, sizeof(struct
				    ipsec_auth));
				if (rule->auth == NULL)
					err(1, "pfkey_parse: calloc");
			}

			rule->auth->srcid = calloc(1, len);
			if (rule->auth->srcid == NULL)
				err(1, "pfkey_parse: calloc");

			strlcpy(rule->auth->srcid, (char *)(sident + 1), len);
			break;

		case SADB_EXT_IDENTITY_DST:
			sident = (struct sadb_ident *)ext;
			len = (sident->sadb_ident_len * sizeof(uint64_t)) -
			    sizeof(struct sadb_ident);

			if (rule->auth == NULL) {
				rule->auth = calloc(1, sizeof(struct
				    ipsec_auth));
				if (rule->auth == NULL)
					err(1, "pfkey_parse: calloc");
			}

			rule->auth->dstid = calloc(1, len);
			if (rule->auth->dstid == NULL)
				err(1, "pfkey_parse: calloc");

			strlcpy(rule->auth->dstid, (char *)(sident + 1), len);
			break;

		case SADB_X_EXT_PROTOCOL:
			sproto = (struct sadb_protocol *)ext;
			if (sproto->sadb_protocol_direction == 0)
				rule->proto = sproto->sadb_protocol_proto;
			break;

		case SADB_X_EXT_FLOW_TYPE:
			sproto = (struct sadb_protocol *)ext;

			switch (sproto->sadb_protocol_direction) {
			case IPSP_DIRECTION_IN:
				rule->direction = IPSEC_IN;
				break;
			case IPSP_DIRECTION_OUT:
				rule->direction = IPSEC_OUT;
				break;
			default:
				return (1);
			}
			switch (sproto->sadb_protocol_proto) {
			case SADB_X_FLOW_TYPE_USE:
				rule->flowtype = TYPE_USE;
				break;
			case SADB_X_FLOW_TYPE_ACQUIRE:
				rule->flowtype = TYPE_ACQUIRE;
				break;
			case SADB_X_FLOW_TYPE_REQUIRE:
				rule->flowtype = TYPE_REQUIRE;
				break;
			case SADB_X_FLOW_TYPE_DENY:
				rule->flowtype = TYPE_DENY;
				break;
			case SADB_X_FLOW_TYPE_BYPASS:
				rule->flowtype = TYPE_BYPASS;
				break;
			case SADB_X_FLOW_TYPE_DONTACQ:
				rule->flowtype = TYPE_DONTACQ;
				break;
			default:
				rule->flowtype = TYPE_UNKNOWN;
				break;
			}
			break;

		case SADB_X_EXT_SRC_FLOW:
			saddr = (struct sadb_address *)ext;
			sa = (struct sockaddr *)(saddr + 1);

			if (rule->src == NULL) {
				rule->src = calloc(1,
				    sizeof(struct ipsec_addr_wrap));
				if (rule->src == NULL)
					err(1, "pfkey_parse: calloc");
			}

			rule->src->af = sa->sa_family;
			switch (sa->sa_family) {
			case AF_INET:
				bcopy(&((struct sockaddr_in *)sa)->sin_addr,
				    &rule->src->address.v4,
				    sizeof(struct in_addr));
				rule->sport =
				    ((struct sockaddr_in *)sa)->sin_port;
				break;
			case AF_INET6:
				bcopy(&((struct sockaddr_in6 *)sa)->sin6_addr,
				    &rule->src->address.v6,
				    sizeof(struct in6_addr));
				rule->sport =
				    ((struct sockaddr_in6 *)sa)->sin6_port;
				break;
			default:
				return (1);
			}
			break;

		case SADB_X_EXT_DST_FLOW:
			saddr = (struct sadb_address *)ext;
			sa = (struct sockaddr *)(saddr + 1);

			if (rule->dst == NULL) {
				rule->dst = calloc(1,
				    sizeof(struct ipsec_addr_wrap));
				if (rule->dst == NULL)
					err(1, "pfkey_parse: calloc");
			}

			rule->dst->af = sa->sa_family;
			switch (sa->sa_family) {
			case AF_INET:
				bcopy(&((struct sockaddr_in *)sa)->sin_addr,
				    &rule->dst->address.v4,
				    sizeof(struct in_addr));
				rule->dport =
				    ((struct sockaddr_in *)sa)->sin_port;
				break;
			case AF_INET6:
				bcopy(&((struct sockaddr_in6 *)sa)->sin6_addr,
				    &rule->dst->address.v6,
				    sizeof(struct in6_addr));
				rule->dport =
				    ((struct sockaddr_in6 *)sa)->sin6_port;
				break;
			default:
				return (1);
			}
			break;


		case SADB_X_EXT_SRC_MASK:
			saddr = (struct sadb_address *)ext;
			sa = (struct sockaddr *)(saddr + 1);

			if (rule->src == NULL) {
				rule->src = calloc(1,
				    sizeof(struct ipsec_addr_wrap));
				if (rule->src == NULL)
					err(1, "pfkey_parse: calloc");
			}

			rule->src->af = sa->sa_family;
			switch (sa->sa_family) {
			case AF_INET:
				sa_in = (struct sockaddr_in *)sa;
				bcopy(&sa_in->sin_addr, &rule->src->mask.v4,
				    sizeof(struct in_addr));
				break;
			case AF_INET6:
				sa_in6 = (struct sockaddr_in6 *)sa;
				bcopy(&sa_in6->sin6_addr, &rule->src->mask.v6,
				    sizeof(struct in6_addr));
				break;

			default:
				return (1);
			}
			break;

		case SADB_X_EXT_DST_MASK:
			saddr = (struct sadb_address *)ext;
			sa = (struct sockaddr *)(saddr + 1);

			if (rule->dst == NULL) {
				rule->dst = calloc(1,
				    sizeof(struct ipsec_addr_wrap));
				if (rule->dst == NULL)
					err(1, "pfkey_parse: calloc");
			}

			rule->dst->af = sa->sa_family;
			switch (sa->sa_family) {
			case AF_INET:
				sa_in = (struct sockaddr_in *)sa;
				bcopy(&sa_in->sin_addr, &rule->dst->mask.v4,
				    sizeof(struct in_addr));
				break;
			case AF_INET6:
				sa_in6 = (struct sockaddr_in6 *)sa;
				bcopy(&sa_in6->sin6_addr, &rule->dst->mask.v6,
				    sizeof(struct in6_addr));
				break;
			default:
				return (1);
			}
			break;

		default:
			return (1);
		}
	}

	return (0);
}

int
pfkey_ipsec_establish(int action, struct ipsec_rule *r)
{
	int		ret;
	u_int8_t	satype, satype2, direction;

	if (r->type == RULE_FLOW) {
		switch (r->satype) {
		case IPSEC_ESP:
			satype = SADB_SATYPE_ESP;
			break;
		case IPSEC_AH:
			satype = SADB_SATYPE_AH;
			break;
		case IPSEC_IPCOMP:
			satype = SADB_X_SATYPE_IPCOMP;
			break;
		case IPSEC_IPIP:
			satype = SADB_X_SATYPE_IPIP;
			break;
		default:
			return -1;
		}

		switch (r->direction) {
		case IPSEC_IN:
			direction = IPSP_DIRECTION_IN;
			break;
		case IPSEC_OUT:
			direction = IPSP_DIRECTION_OUT;
			break;
		default:
			return -1;
		}

		switch (action) {
		case ACTION_ADD:
			ret = pfkey_flow(fd, satype, SADB_X_ADDFLOW, direction,
			    r->proto, r->src, r->sport, r->dst, r->dport,
			    r->local, r->peer, r->auth, r->flowtype);
			break;
		case ACTION_DELETE:
			/* No peer for flow deletion. */
			ret = pfkey_flow(fd, satype, SADB_X_DELFLOW, direction,
			    r->proto, r->src, r->sport, r->dst, r->dport,
			    NULL, NULL, NULL, r->flowtype);
			break;
		default:
			return -1;
		}
	} else if (r->type == RULE_SA) {
		switch (r->satype) {
		case IPSEC_AH:
			satype = SADB_SATYPE_AH;
			break;
		case IPSEC_ESP:
			satype = SADB_SATYPE_ESP;
			break;
		case IPSEC_IPCOMP:
			satype = SADB_X_SATYPE_IPCOMP;
			break;
		case IPSEC_TCPMD5:
			satype = SADB_X_SATYPE_TCPSIGNATURE;
			break;
		case IPSEC_IPIP:
			satype = SADB_X_SATYPE_IPIP;
			break;
		default:
			return -1;
		}
		switch (action) {
		case ACTION_ADD:
			ret = pfkey_sa(fd, satype, SADB_ADD, r->spi,
			    r->src, r->dst, r->udpencap, r->udpdport,
			    r->xfs, r->authkey, r->enckey, r->tmode);
			break;
		case ACTION_DELETE:
			ret = pfkey_sa(fd, satype, SADB_DELETE, r->spi,
			    r->src, r->dst, 0, 0, r->xfs, NULL, NULL, r->tmode);
			break;
		default:
			return -1;
		}
	} else if (r->type == RULE_BUNDLE) {
		switch (r->satype) {
		case IPSEC_AH:
			satype = SADB_SATYPE_AH;
			break;
		case IPSEC_ESP:
			satype = SADB_SATYPE_ESP;
			break;
		case IPSEC_IPCOMP:
			satype = SADB_X_SATYPE_IPCOMP;
			break;
		case IPSEC_TCPMD5:
			satype = SADB_X_SATYPE_TCPSIGNATURE;
			break;
		case IPSEC_IPIP:
			satype = SADB_X_SATYPE_IPIP;
			break;
		default:
			return -1;
		}
		switch (r->proto2) {
		case IPSEC_AH:
			satype2 = SADB_SATYPE_AH;
			break;
		case IPSEC_ESP:
			satype2 = SADB_SATYPE_ESP;
			break;
		case IPSEC_IPCOMP:
			satype2 = SADB_X_SATYPE_IPCOMP;
			break;
		case IPSEC_TCPMD5:
			satype2 = SADB_X_SATYPE_TCPSIGNATURE;
			break;
		case IPSEC_IPIP:
			satype2 = SADB_X_SATYPE_IPIP;
			break;
		default:
			return -1;
		}
		switch (action) {
		case ACTION_ADD:
			ret = pfkey_sabundle(fd, satype, satype2,
			    SADB_X_GRPSPIS, r->dst, r->spi, r->dst2, r->spi2);
			break;
		case ACTION_DELETE:
			return 0;
		default:
			return -1;
		}
	} else
		return -1;

	if (ret < 0)
		return -1;
	if (pfkey_reply(fd, NULL, NULL) < 0)
		return -1;

	return 0;
}

int
pfkey_ipsec_flush(void)
{
	struct sadb_msg smsg;
	struct iovec	iov[IOV_CNT];
	ssize_t		n;
	int		iov_cnt, len;

	bzero(&smsg, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = sadb_msg_seq++;
	smsg.sadb_msg_pid = getpid();
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = SADB_FLUSH;
	smsg.sadb_msg_satype = SADB_SATYPE_UNSPEC;

	iov_cnt = 0;

	iov[iov_cnt].iov_base = &smsg;
	iov[iov_cnt].iov_len = sizeof(smsg);
	iov_cnt++;

	len = smsg.sadb_msg_len * 8;
	if ((n = writev(fd, iov, iov_cnt)) == -1) {
		warn("writev failed");
		return -1;
	}
	if (n != len) {
		warnx("short write");
		return -1;
	}
	if (pfkey_reply(fd, NULL, NULL) < 0)
		return -1;

	return 0;
}

static int
pfkey_promisc(void)
{
	struct sadb_msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.sadb_msg_version = PF_KEY_V2;
	msg.sadb_msg_seq = sadb_msg_seq++;
	msg.sadb_msg_pid = getpid();
	msg.sadb_msg_len = sizeof(msg) / PFKEYV2_CHUNK;
	msg.sadb_msg_type = SADB_X_PROMISC;
	msg.sadb_msg_satype = 1;	/* enable */
	if (write(fd, &msg, sizeof(msg)) != sizeof(msg)) {
		warn("pfkey_promisc: write failed");
		return -1;
	}
	if (pfkey_reply(fd, NULL, NULL) < 0)
		return -1;
	return 0;
}

int
pfkey_monitor(int opts)
{
	struct pollfd pfd[1];
	struct sadb_msg *msg;
	u_int8_t *data;
	ssize_t len;
	int n;

	if (pfkey_init() < 0)
		return -1;
	if (pfkey_promisc() < 0)
		return -1;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	for (;;) {
		if ((n = poll(pfd, 1, -1)) == -1)
			err(2, "poll");
		if (n == 0)
			break;
		if ((pfd[0].revents & POLLIN) == 0)
			continue;
		if (pfkey_reply(fd, &data, &len) < 0)
			continue;
		msg = (struct sadb_msg *)data;
		if (msg->sadb_msg_type == SADB_X_PROMISC) {
			/* remove extra header from promisc messages */
			if ((msg->sadb_msg_len * PFKEYV2_CHUNK) >=
			    2 * sizeof(struct sadb_msg)) {
				msg++;
			}
		}
		pfkey_monitor_sa(msg, opts);
		if (opts & IPSECCTL_OPT_VERBOSE)
			pfkey_print_raw(data, len);
		freezero(data, len);
	}
	close(fd);
	return 0;
}

int
pfkey_init(void)
{
	if ((fd = socket(PF_KEY, SOCK_RAW, PF_KEY_V2)) == -1)
		err(1, "pfkey_init: failed to open PF_KEY socket");

	return 0;
}
