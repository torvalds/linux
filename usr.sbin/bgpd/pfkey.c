/*	$OpenBSD: pfkey.c,v 1.73 2025/09/12 11:48:05 claudio Exp $ */

/*
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <net/pfkeyv2.h>
#include <netinet/ip_ipsp.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bgpd.h"
#include "session.h"
#include "log.h"

extern struct bgpd_sysdep sysdep;

#define	PFKEY2_CHUNK sizeof(uint64_t)
#define	ROUNDUP(x) (((x) + (PFKEY2_CHUNK - 1)) & ~(PFKEY2_CHUNK - 1))
#define	IOV_CNT	20

static uint32_t	sadb_msg_seq = 0;
static uint32_t	pid = 0; /* should pid_t but pfkey needs uint32_t */
static int		pfkey_fd;

static int	pfkey_reply(int, uint32_t *);
static int	pfkey_send(int, uint8_t, uint8_t, uint8_t,
		    const struct bgpd_addr *, const struct bgpd_addr *,
		    uint32_t, uint8_t, int, char *, uint8_t, int, char *,
		    uint16_t, uint16_t);

#define pfkey_flow(fd, satype, cmd, dir, from, to, sport, dport) \
	pfkey_send(fd, satype, cmd, dir, from, to, \
	    0, 0, 0, NULL, 0, 0, NULL, sport, dport)

static int
pfkey_send(int sd, uint8_t satype, uint8_t mtype, uint8_t dir,
    const struct bgpd_addr *src, const struct bgpd_addr *dst, uint32_t spi,
    uint8_t aalg, int alen, char *akey, uint8_t ealg, int elen, char *ekey,
    uint16_t sport, uint16_t dport)
{
	struct sadb_msg		smsg;
	struct sadb_sa		sa;
	struct sadb_address	sa_src, sa_dst, sa_peer, sa_smask, sa_dmask;
	struct sadb_key		sa_akey, sa_ekey;
	struct sadb_spirange	sa_spirange;
	struct sadb_protocol	sa_flowtype, sa_protocol;
	struct iovec		iov[IOV_CNT];
	ssize_t			n;
	int			len = 0;
	int			iov_cnt;
	struct sockaddr_storage	ssrc, sdst, speer, smask, dmask;
	struct sockaddr		*saptr;
	socklen_t		 salen;

	if (!pid)
		pid = getpid();

	/* we need clean sockaddr... no ports set */
	memset(&ssrc, 0, sizeof(ssrc));
	memset(&smask, 0, sizeof(smask));
	if ((saptr = addr2sa(src, 0, &salen))) {
		memcpy(&ssrc, saptr, salen);
		ssrc.ss_len = salen;
	}
	switch (src->aid) {
	case AID_INET:
		memset(&((struct sockaddr_in *)&smask)->sin_addr, 0xff, 32/8);
		break;
	case AID_INET6:
		memset(&((struct sockaddr_in6 *)&smask)->sin6_addr, 0xff,
		    128/8);
		break;
	case AID_UNSPEC:
		ssrc.ss_len = sizeof(struct sockaddr);
		break;
	default:
		return (-1);
	}
	smask.ss_family = ssrc.ss_family;
	smask.ss_len = ssrc.ss_len;

	memset(&sdst, 0, sizeof(sdst));
	memset(&dmask, 0, sizeof(dmask));
	if ((saptr = addr2sa(dst, 0, &salen))) {
		memcpy(&sdst, saptr, salen);
		sdst.ss_len = salen;
	}
	switch (dst->aid) {
	case AID_INET:
		memset(&((struct sockaddr_in *)&dmask)->sin_addr, 0xff, 32/8);
		break;
	case AID_INET6:
		memset(&((struct sockaddr_in6 *)&dmask)->sin6_addr, 0xff,
		    128/8);
		break;
	case AID_UNSPEC:
		sdst.ss_len = sizeof(struct sockaddr);
		break;
	default:
		return (-1);
	}
	dmask.ss_family = sdst.ss_family;
	dmask.ss_len = sdst.ss_len;

	memset(&smsg, 0, sizeof(smsg));
	smsg.sadb_msg_version = PF_KEY_V2;
	smsg.sadb_msg_seq = ++sadb_msg_seq;
	smsg.sadb_msg_pid = pid;
	smsg.sadb_msg_len = sizeof(smsg) / 8;
	smsg.sadb_msg_type = mtype;
	smsg.sadb_msg_satype = satype;

	switch (mtype) {
	case SADB_GETSPI:
		memset(&sa_spirange, 0, sizeof(sa_spirange));
		sa_spirange.sadb_spirange_exttype = SADB_EXT_SPIRANGE;
		sa_spirange.sadb_spirange_len = sizeof(sa_spirange) / 8;
		sa_spirange.sadb_spirange_min = 0x100;
		sa_spirange.sadb_spirange_max = 0xffffffff;
		sa_spirange.sadb_spirange_reserved = 0;
		break;
	case SADB_ADD:
	case SADB_UPDATE:
	case SADB_DELETE:
		memset(&sa, 0, sizeof(sa));
		sa.sadb_sa_exttype = SADB_EXT_SA;
		sa.sadb_sa_len = sizeof(sa) / 8;
		sa.sadb_sa_replay = 0;
		sa.sadb_sa_spi = htonl(spi);
		sa.sadb_sa_state = SADB_SASTATE_MATURE;
		break;
	case SADB_X_ADDFLOW:
	case SADB_X_DELFLOW:
		memset(&sa_flowtype, 0, sizeof(sa_flowtype));
		sa_flowtype.sadb_protocol_exttype = SADB_X_EXT_FLOW_TYPE;
		sa_flowtype.sadb_protocol_len = sizeof(sa_flowtype) / 8;
		sa_flowtype.sadb_protocol_direction = dir;
		sa_flowtype.sadb_protocol_proto = SADB_X_FLOW_TYPE_REQUIRE;

		memset(&sa_protocol, 0, sizeof(sa_protocol));
		sa_protocol.sadb_protocol_exttype = SADB_X_EXT_PROTOCOL;
		sa_protocol.sadb_protocol_len = sizeof(sa_protocol) / 8;
		sa_protocol.sadb_protocol_direction = 0;
		sa_protocol.sadb_protocol_proto = 6;
		break;
	}

	memset(&sa_src, 0, sizeof(sa_src));
	sa_src.sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
	sa_src.sadb_address_len = (sizeof(sa_src) + ROUNDUP(ssrc.ss_len)) / 8;

	memset(&sa_dst, 0, sizeof(sa_dst));
	sa_dst.sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	sa_dst.sadb_address_len = (sizeof(sa_dst) + ROUNDUP(sdst.ss_len)) / 8;

	sa.sadb_sa_auth = aalg;
	sa.sadb_sa_encrypt = SADB_X_EALG_AES; /* XXX */

	switch (mtype) {
	case SADB_ADD:
	case SADB_UPDATE:
		memset(&sa_akey, 0, sizeof(sa_akey));
		sa_akey.sadb_key_exttype = SADB_EXT_KEY_AUTH;
		sa_akey.sadb_key_len = (sizeof(sa_akey) +
		    ((alen + 7) / 8) * 8) / 8;
		sa_akey.sadb_key_bits = 8 * alen;

		memset(&sa_ekey, 0, sizeof(sa_ekey));
		sa_ekey.sadb_key_exttype = SADB_EXT_KEY_ENCRYPT;
		sa_ekey.sadb_key_len = (sizeof(sa_ekey) +
		    ((elen + 7) / 8) * 8) / 8;
		sa_ekey.sadb_key_bits = 8 * elen;

		break;
	case SADB_X_ADDFLOW:
	case SADB_X_DELFLOW:
		/* sa_peer always points to the remote machine */
		if (dir == IPSP_DIRECTION_IN) {
			speer = ssrc;
			sa_peer = sa_src;
		} else {
			speer = sdst;
			sa_peer = sa_dst;
		}
		sa_peer.sadb_address_exttype = SADB_EXT_ADDRESS_DST;
		sa_peer.sadb_address_len =
		    (sizeof(sa_peer) + ROUNDUP(speer.ss_len)) / 8;

		/* for addflow we also use src/dst as the flow destination */
		sa_src.sadb_address_exttype = SADB_X_EXT_SRC_FLOW;
		sa_dst.sadb_address_exttype = SADB_X_EXT_DST_FLOW;

		memset(&smask, 0, sizeof(smask));
		switch (src->aid) {
		case AID_INET:
			smask.ss_len = sizeof(struct sockaddr_in);
			smask.ss_family = AF_INET;
			memset(&((struct sockaddr_in *)&smask)->sin_addr,
			    0xff, 32/8);
			if (sport) {
				((struct sockaddr_in *)&ssrc)->sin_port =
				    htons(sport);
				((struct sockaddr_in *)&smask)->sin_port =
				    htons(0xffff);
			}
			break;
		case AID_INET6:
			smask.ss_len = sizeof(struct sockaddr_in6);
			smask.ss_family = AF_INET6;
			memset(&((struct sockaddr_in6 *)&smask)->sin6_addr,
			    0xff, 128/8);
			if (sport) {
				((struct sockaddr_in6 *)&ssrc)->sin6_port =
				    htons(sport);
				((struct sockaddr_in6 *)&smask)->sin6_port =
				    htons(0xffff);
			}
			break;
		}
		memset(&dmask, 0, sizeof(dmask));
		switch (dst->aid) {
		case AID_INET:
			dmask.ss_len = sizeof(struct sockaddr_in);
			dmask.ss_family = AF_INET;
			memset(&((struct sockaddr_in *)&dmask)->sin_addr,
			    0xff, 32/8);
			if (dport) {
				((struct sockaddr_in *)&sdst)->sin_port =
				    htons(dport);
				((struct sockaddr_in *)&dmask)->sin_port =
				    htons(0xffff);
			}
			break;
		case AID_INET6:
			dmask.ss_len = sizeof(struct sockaddr_in6);
			dmask.ss_family = AF_INET6;
			memset(&((struct sockaddr_in6 *)&dmask)->sin6_addr,
			    0xff, 128/8);
			if (dport) {
				((struct sockaddr_in6 *)&sdst)->sin6_port =
				    htons(dport);
				((struct sockaddr_in6 *)&dmask)->sin6_port =
				    htons(0xffff);
			}
			break;
		}

		memset(&sa_smask, 0, sizeof(sa_smask));
		sa_smask.sadb_address_exttype = SADB_X_EXT_SRC_MASK;
		sa_smask.sadb_address_len =
		    (sizeof(sa_smask) + ROUNDUP(smask.ss_len)) / 8;

		memset(&sa_dmask, 0, sizeof(sa_dmask));
		sa_dmask.sadb_address_exttype = SADB_X_EXT_DST_MASK;
		sa_dmask.sadb_address_len =
		    (sizeof(sa_dmask) + ROUNDUP(dmask.ss_len)) / 8;
		break;
	}

	iov_cnt = 0;

	/* msghdr */
	iov[iov_cnt].iov_base = &smsg;
	iov[iov_cnt].iov_len = sizeof(smsg);
	iov_cnt++;

	switch (mtype) {
	case SADB_ADD:
	case SADB_UPDATE:
	case SADB_DELETE:
		/* SA hdr */
		iov[iov_cnt].iov_base = &sa;
		iov[iov_cnt].iov_len = sizeof(sa);
		smsg.sadb_msg_len += sa.sadb_sa_len;
		iov_cnt++;
		break;
	case SADB_GETSPI:
		/* SPI range */
		iov[iov_cnt].iov_base = &sa_spirange;
		iov[iov_cnt].iov_len = sizeof(sa_spirange);
		smsg.sadb_msg_len += sa_spirange.sadb_spirange_len;
		iov_cnt++;
		break;
	case SADB_X_ADDFLOW:
		/* sa_peer always points to the remote machine */
		iov[iov_cnt].iov_base = &sa_peer;
		iov[iov_cnt].iov_len = sizeof(sa_peer);
		iov_cnt++;
		iov[iov_cnt].iov_base = &speer;
		iov[iov_cnt].iov_len = ROUNDUP(speer.ss_len);
		smsg.sadb_msg_len += sa_peer.sadb_address_len;
		iov_cnt++;

		/* FALLTHROUGH */
	case SADB_X_DELFLOW:
		/* add flow type */
		iov[iov_cnt].iov_base = &sa_flowtype;
		iov[iov_cnt].iov_len = sizeof(sa_flowtype);
		smsg.sadb_msg_len += sa_flowtype.sadb_protocol_len;
		iov_cnt++;

		/* add protocol */
		iov[iov_cnt].iov_base = &sa_protocol;
		iov[iov_cnt].iov_len = sizeof(sa_protocol);
		smsg.sadb_msg_len += sa_protocol.sadb_protocol_len;
		iov_cnt++;

		/* add flow masks */
		iov[iov_cnt].iov_base = &sa_smask;
		iov[iov_cnt].iov_len = sizeof(sa_smask);
		iov_cnt++;
		iov[iov_cnt].iov_base = &smask;
		iov[iov_cnt].iov_len = ROUNDUP(smask.ss_len);
		smsg.sadb_msg_len += sa_smask.sadb_address_len;
		iov_cnt++;

		iov[iov_cnt].iov_base = &sa_dmask;
		iov[iov_cnt].iov_len = sizeof(sa_dmask);
		iov_cnt++;
		iov[iov_cnt].iov_base = &dmask;
		iov[iov_cnt].iov_len = ROUNDUP(dmask.ss_len);
		smsg.sadb_msg_len += sa_dmask.sadb_address_len;
		iov_cnt++;
		break;
	}

	/* dest addr */
	iov[iov_cnt].iov_base = &sa_dst;
	iov[iov_cnt].iov_len = sizeof(sa_dst);
	iov_cnt++;
	iov[iov_cnt].iov_base = &sdst;
	iov[iov_cnt].iov_len = ROUNDUP(sdst.ss_len);
	smsg.sadb_msg_len += sa_dst.sadb_address_len;
	iov_cnt++;

	/* src addr */
	iov[iov_cnt].iov_base = &sa_src;
	iov[iov_cnt].iov_len = sizeof(sa_src);
	iov_cnt++;
	iov[iov_cnt].iov_base = &ssrc;
	iov[iov_cnt].iov_len = ROUNDUP(ssrc.ss_len);
	smsg.sadb_msg_len += sa_src.sadb_address_len;
	iov_cnt++;

	switch (mtype) {
	case SADB_ADD:
	case SADB_UPDATE:
		if (alen) {
			/* auth key */
			iov[iov_cnt].iov_base = &sa_akey;
			iov[iov_cnt].iov_len = sizeof(sa_akey);
			iov_cnt++;
			iov[iov_cnt].iov_base = akey;
			iov[iov_cnt].iov_len = ((alen + 7) / 8) * 8;
			smsg.sadb_msg_len += sa_akey.sadb_key_len;
			iov_cnt++;
		}
		if (elen) {
			/* encryption key */
			iov[iov_cnt].iov_base = &sa_ekey;
			iov[iov_cnt].iov_len = sizeof(sa_ekey);
			iov_cnt++;
			iov[iov_cnt].iov_base = ekey;
			iov[iov_cnt].iov_len = ((elen + 7) / 8) * 8;
			smsg.sadb_msg_len += sa_ekey.sadb_key_len;
			iov_cnt++;
		}
		break;
	}

	len = smsg.sadb_msg_len * 8;
	do {
		n = writev(sd, iov, iov_cnt);
	} while (n == -1 && (errno == EAGAIN || errno == EINTR));

	if (n == -1) {
		log_warn("%s: writev (%d/%d)", __func__, iov_cnt, len);
		return (-1);
	}

	return (0);
}

int
pfkey_read(int sd, struct sadb_msg *h)
{
	struct sadb_msg hdr;

	if (recv(sd, &hdr, sizeof(hdr), MSG_PEEK) != sizeof(hdr)) {
		if (errno == EAGAIN || errno == EINTR)
			return (1);
		log_warn("pfkey peek");
		return (-1);
	}

	/* XXX: Only one message can be outstanding. */
	if (hdr.sadb_msg_seq == sadb_msg_seq &&
	    hdr.sadb_msg_pid == pid) {
		if (h)
			memcpy(h, &hdr, sizeof(hdr));
		return (0);
	}

	/* not ours, discard */
	if (read(sd, &hdr, sizeof(hdr)) == -1) {
		if (errno == EAGAIN || errno == EINTR)
			return (1);
		log_warn("pfkey read");
		return (-1);
	}

	return (1);
}

static int
pfkey_reply(int sd, uint32_t *spi)
{
	struct sadb_msg hdr, *msg;
	struct sadb_ext *ext;
	struct sadb_sa *sa;
	uint8_t *data;
	ssize_t len;
	int rv;

	do {
		rv = pfkey_read(sd, &hdr);
		if (rv == -1)
			return (-1);
	} while (rv);

	if (hdr.sadb_msg_errno != 0) {
		errno = hdr.sadb_msg_errno;

		/* discard error message */
		if (read(sd, &hdr, sizeof(hdr)) == -1)
			log_warn("pfkey read");

		if (errno == ESRCH)
			return (0);
		else {
			log_warn("pfkey");
			return (-1);
		}
	}
	if ((data = reallocarray(NULL, hdr.sadb_msg_len, PFKEY2_CHUNK))
	    == NULL) {
		log_warn("pfkey malloc");
		return (-1);
	}
	len = hdr.sadb_msg_len * PFKEY2_CHUNK;
	if (read(sd, data, len) != len) {
		log_warn("pfkey read");
		freezero(data, len);
		return (-1);
	}

	if (hdr.sadb_msg_type == SADB_GETSPI) {
		if (spi == NULL) {
			freezero(data, len);
			return (0);
		}

		msg = (struct sadb_msg *)data;
		for (ext = (struct sadb_ext *)(msg + 1);
		    (size_t)((uint8_t *)ext - (uint8_t *)msg) <
		    msg->sadb_msg_len * PFKEY2_CHUNK;
		    ext = (struct sadb_ext *)((uint8_t *)ext +
		    ext->sadb_ext_len * PFKEY2_CHUNK)) {
			if (ext->sadb_ext_type == SADB_EXT_SA) {
				sa = (struct sadb_sa *) ext;
				*spi = ntohl(sa->sadb_sa_spi);
				break;
			}
		}
	}
	freezero(data, len);
	return (0);
}

static int
pfkey_sa_add(const struct bgpd_addr *src, const struct bgpd_addr *dst,
    uint8_t keylen, char *key, uint32_t *spi)
{
	if (pfkey_send(pfkey_fd, SADB_X_SATYPE_TCPSIGNATURE, SADB_GETSPI, 0,
	    src, dst, 0, 0, 0, NULL, 0, 0, NULL, 0, 0) == -1)
		return (-1);
	if (pfkey_reply(pfkey_fd, spi) == -1)
		return (-1);
	if (pfkey_send(pfkey_fd, SADB_X_SATYPE_TCPSIGNATURE, SADB_UPDATE, 0,
		src, dst, *spi, 0, keylen, key, 0, 0, NULL, 0, 0) == -1)
		return (-1);
	if (pfkey_reply(pfkey_fd, NULL) == -1)
		return (-1);
	return (0);
}

static int
pfkey_sa_remove(const struct bgpd_addr *src, const struct bgpd_addr *dst,
    uint32_t *spi)
{
	if (pfkey_send(pfkey_fd, SADB_X_SATYPE_TCPSIGNATURE, SADB_DELETE, 0,
	    src, dst, *spi, 0, 0, NULL, 0, 0, NULL, 0, 0) == -1)
		return (-1);
	if (pfkey_reply(pfkey_fd, NULL) == -1)
		return (-1);
	*spi = 0;
	return (0);
}

static int
pfkey_md5sig_establish(struct auth_state *as, struct auth_config *auth,
    const struct bgpd_addr *local_addr, const struct bgpd_addr *remote_addr)
{
	uint32_t spi_out = 0;
	uint32_t spi_in = 0;

	if (pfkey_sa_add(local_addr, remote_addr,
	    auth->md5key_len, auth->md5key, &spi_out) == -1)
		goto fail;

	if (pfkey_sa_add(remote_addr, local_addr,
	    auth->md5key_len, auth->md5key, &spi_in) == -1)
		goto fail;

	/* cleanup old flow if one was present */
	if (pfkey_remove(as) == -1)
		return (-1);

	as->established = 1;
	as->method = auth->method;
	as->local_addr = *local_addr;
	as->remote_addr = *remote_addr;
	as->spi_out = spi_out;
	as->spi_in = spi_in;
	return (0);

fail:
	return (-1);
}

static int
pfkey_md5sig_remove(struct auth_state *as)
{
	if (as->spi_out)
		if (pfkey_sa_remove(&as->local_addr, &as->remote_addr,
		    &as->spi_out) == -1)
			goto fail;
	if (as->spi_in)
		if (pfkey_sa_remove(&as->remote_addr, &as->local_addr,
		    &as->spi_in) == -1)
			goto fail;

	explicit_bzero(as, sizeof(*as));
	return (0);

fail:
	return (-1);
}

static uint8_t
pfkey_auth_alg(enum auth_alg alg)
{
	switch (alg) {
	case AUTH_AALG_SHA1HMAC:
		return SADB_AALG_SHA1HMAC;
	case AUTH_AALG_MD5HMAC:
		return SADB_AALG_MD5HMAC;
	default:
		return SADB_AALG_NONE;
	}
}

static uint8_t
pfkey_enc_alg(enum auth_enc_alg alg)
{
	switch (alg) {
	case AUTH_EALG_3DESCBC:
		return SADB_EALG_3DESCBC;
	case AUTH_EALG_AES:
		return SADB_X_EALG_AES;
	default:
		return SADB_AALG_NONE;
	}
}

static int
pfkey_ipsec_establish(struct auth_state *as, struct auth_config *auth,
    const struct bgpd_addr *local_addr, const struct bgpd_addr *remote_addr)
{
	uint8_t satype = SADB_SATYPE_ESP;

	/* cleanup first, unlike in the TCP MD5 case */
	if (pfkey_remove(as) == -1)
		return (-1);

	switch (auth->method) {
	case AUTH_IPSEC_IKE_ESP:
		satype = SADB_SATYPE_ESP;
		break;
	case AUTH_IPSEC_IKE_AH:
		satype = SADB_SATYPE_AH;
		break;
	case AUTH_IPSEC_MANUAL_ESP:
	case AUTH_IPSEC_MANUAL_AH:
		satype = auth->method == AUTH_IPSEC_MANUAL_ESP ?
		    SADB_SATYPE_ESP : SADB_SATYPE_AH;
		if (pfkey_send(pfkey_fd, satype, SADB_ADD, 0,
		    local_addr, remote_addr,
		    auth->spi_out,
		    pfkey_auth_alg(auth->auth_alg_out),
		    auth->auth_keylen_out,
		    auth->auth_key_out,
		    pfkey_enc_alg(auth->enc_alg_out),
		    auth->enc_keylen_out,
		    auth->enc_key_out,
		    0, 0) == -1)
			goto fail_key;
		if (pfkey_reply(pfkey_fd, NULL) == -1)
			goto fail_key;
		if (pfkey_send(pfkey_fd, satype, SADB_ADD, 0,
		    remote_addr, local_addr,
		    auth->spi_in,
		    pfkey_auth_alg(auth->auth_alg_in),
		    auth->auth_keylen_in,
		    auth->auth_key_in,
		    pfkey_enc_alg(auth->enc_alg_in),
		    auth->enc_keylen_in,
		    auth->enc_key_in,
		    0, 0) == -1)
			goto fail_key;
		if (pfkey_reply(pfkey_fd, NULL) == -1)
			goto fail_key;
		break;
	default:
		return (-1);
	}

	if (pfkey_flow(pfkey_fd, satype, SADB_X_ADDFLOW, IPSP_DIRECTION_OUT,
	    local_addr, remote_addr, 0, BGP_PORT) == -1)
		goto fail_flow;
	if (pfkey_reply(pfkey_fd, NULL) == -1)
		goto fail_flow;

	if (pfkey_flow(pfkey_fd, satype, SADB_X_ADDFLOW, IPSP_DIRECTION_OUT,
	    local_addr, remote_addr, BGP_PORT, 0) == -1)
		goto fail_flow;
	if (pfkey_reply(pfkey_fd, NULL) == -1)
		goto fail_flow;

	if (pfkey_flow(pfkey_fd, satype, SADB_X_ADDFLOW, IPSP_DIRECTION_IN,
	    remote_addr, local_addr, 0, BGP_PORT) == -1)
		goto fail_flow;
	if (pfkey_reply(pfkey_fd, NULL) == -1)
		goto fail_flow;

	if (pfkey_flow(pfkey_fd, satype, SADB_X_ADDFLOW, IPSP_DIRECTION_IN,
	    remote_addr, local_addr, BGP_PORT, 0) == -1)
		goto fail_flow;
	if (pfkey_reply(pfkey_fd, NULL) == -1)
		goto fail_flow;

	/* save SPI so that they can be removed later on */
	as->established = 1;
	as->method = auth->method;
	as->local_addr = *local_addr;
	as->remote_addr = *remote_addr;
	as->spi_in = auth->spi_in;
	as->spi_out = auth->spi_out;
	return (0);

fail_key:
	log_warn("failed to insert ipsec key");
	return (-1);
fail_flow:
	log_warn("failed to insert ipsec flow");
	return (-1);
}

static int
pfkey_ipsec_remove(struct auth_state *as)
{
	uint8_t satype;

	switch (as->method) {
	case AUTH_IPSEC_IKE_ESP:
		satype = SADB_SATYPE_ESP;
		break;
	case AUTH_IPSEC_IKE_AH:
		satype = SADB_SATYPE_AH;
		break;
	case AUTH_IPSEC_MANUAL_ESP:
	case AUTH_IPSEC_MANUAL_AH:
		satype = as->method == AUTH_IPSEC_MANUAL_ESP ?
		    SADB_SATYPE_ESP : SADB_SATYPE_AH;
		if (pfkey_send(pfkey_fd, satype, SADB_DELETE, 0,
		    &as->local_addr, &as->remote_addr,
		    as->spi_out, 0, 0, NULL, 0, 0, NULL, 0, 0) == -1)
			goto fail_key;
		if (pfkey_reply(pfkey_fd, NULL) == -1)
			goto fail_key;

		if (pfkey_send(pfkey_fd, satype, SADB_DELETE, 0,
		    &as->remote_addr, &as->local_addr,
		    as->spi_in, 0, 0, NULL, 0, 0, NULL, 0, 0) == -1)
			goto fail_key;
		if (pfkey_reply(pfkey_fd, NULL) == -1)
			goto fail_key;
		break;
	default:
		return (-1);
	}

	if (pfkey_flow(pfkey_fd, satype, SADB_X_DELFLOW, IPSP_DIRECTION_OUT,
	    &as->local_addr, &as->remote_addr, 0, BGP_PORT) == -1)
		goto fail_flow;
	if (pfkey_reply(pfkey_fd, NULL) == -1)
		goto fail_flow;

	if (pfkey_flow(pfkey_fd, satype, SADB_X_DELFLOW, IPSP_DIRECTION_OUT,
	    &as->local_addr, &as->remote_addr, BGP_PORT, 0) == -1)
		goto fail_flow;
	if (pfkey_reply(pfkey_fd, NULL) == -1)
		goto fail_flow;

	if (pfkey_flow(pfkey_fd, satype, SADB_X_DELFLOW, IPSP_DIRECTION_IN,
	    &as->remote_addr, &as->local_addr, 0, BGP_PORT) == -1)
		goto fail_flow;
	if (pfkey_reply(pfkey_fd, NULL) == -1)
		goto fail_flow;

	if (pfkey_flow(pfkey_fd, satype, SADB_X_DELFLOW, IPSP_DIRECTION_IN,
	    &as->remote_addr, &as->local_addr, BGP_PORT, 0) == -1)
		goto fail_flow;
	if (pfkey_reply(pfkey_fd, NULL) == -1)
		goto fail_flow;

	explicit_bzero(as, sizeof(*as));
	return (0);

fail_key:
	log_warn("failed to remove ipsec key");
	return (-1);
fail_flow:
	log_warn("failed to remove ipsec flow");
	return (-1);
}

int
pfkey_establish(struct auth_state *as, struct auth_config *auth,
    const struct bgpd_addr *local_addr, const struct bgpd_addr *remote_addr)
{
	switch (auth->method) {
	case AUTH_NONE:
		return pfkey_remove(as);
	case AUTH_MD5SIG:
		return pfkey_md5sig_establish(as, auth, local_addr,
		    remote_addr);
	default:
		return pfkey_ipsec_establish(as, auth, local_addr, remote_addr);
	}
}

int
pfkey_remove(struct auth_state *as)
{
	if (as->established == 0)
		return (0);

	switch (as->method) {
	case AUTH_NONE:
		return (0);
	case AUTH_MD5SIG:
		return (pfkey_md5sig_remove(as));
	default:
		return (pfkey_ipsec_remove(as));
	}
}

int
pfkey_init(void)
{
	if ((pfkey_fd = socket(PF_KEY, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_KEY_V2)) == -1) {
		if (errno == EPROTONOSUPPORT) {
			log_warnx("PF_KEY not available, disabling ipsec");
			return (-1);
		} else
			fatal("pfkey setup failed");
	}
	return (pfkey_fd);
}

int
pfkey_send_conf(struct imsgbuf *imsgbuf, uint32_t id, struct auth_config *auth)
{
	/* SE only needs the auth method */
	return imsg_compose(imsgbuf, IMSG_RECONF_PEER_AUTH, id, 0, -1,
	    &auth->method, sizeof(auth->method));
}

int
pfkey_recv_conf(struct peer *p, struct imsg *imsg)
{
	struct auth_config *auth = &p->auth_conf;

	return imsg_get_data(imsg, &auth->method, sizeof(auth->method));
}

/* verify that connection is using TCP MD5SIG if required by config */
int
tcp_md5_check(int fd, struct auth_config *auth)
{
	socklen_t len;
	int opt;

	if (auth->method == AUTH_MD5SIG) {
		if (sysdep.no_md5sig) {
			errno = ENOPROTOOPT;
			return -1;
		}
		len = sizeof(opt);
		if (getsockopt(fd, IPPROTO_TCP, TCP_MD5SIG,
		    &opt, &len) == -1)
			return -1;
		if (!opt) {	/* non-md5'd connection! */
			errno = ECONNREFUSED;
			return -1;
		}
	}
	return 0;
}

/* enable or set TCP MD5SIG on a new client connection */
int
tcp_md5_set(int fd, struct auth_config *auth, struct bgpd_addr *remote_addr)
{
	int opt = 1;

	if (auth->method == AUTH_MD5SIG) {
		if (sysdep.no_md5sig) {
			errno = ENOPROTOOPT;
			return -1;
		}
		if (setsockopt(fd, IPPROTO_TCP, TCP_MD5SIG,
		    &opt, sizeof(opt)) == -1)
			return -1;
	}
	return 0;
}

/* enable or prepare a new listening socket for TCP MD5SIG usage */
int
tcp_md5_prep_listener(struct listen_addr *la, struct peer_head *p)
{
	int opt = 1;

	if (setsockopt(la->fd, IPPROTO_TCP, TCP_MD5SIG,
	    &opt, sizeof(opt)) == -1) {
		if (errno == ENOPROTOOPT) {	/* system w/o md5sig */
			log_warnx("md5sig not available, disabling");
			sysdep.no_md5sig = 1;
			return 0;
		}
		return -1;
	}
	return 0;
}

/* add md5 key to all listening sockets, dummy function for portable */
void
tcp_md5_add_listener(struct bgpd_config *conf, struct peer *p)
{
}

/* delete md5 key form all listening sockets, dummy function for portable */
void
tcp_md5_del_listener(struct bgpd_config *conf, struct peer *p)
{
}
