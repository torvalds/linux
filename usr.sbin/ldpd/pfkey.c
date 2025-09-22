/*	$OpenBSD: pfkey.c,v 1.13 2022/11/07 22:39:13 mbuhl Exp $ */

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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ldpd.h"
#include "ldpe.h"
#include "log.h"

static int	 pfkey_send(int, uint8_t, uint8_t, uint8_t,
		    int, union ldpd_addr *, union ldpd_addr *,
		    uint32_t, uint8_t, int, char *, uint8_t, int, char *,
		    uint16_t, uint16_t);
static int	 pfkey_reply(int, uint32_t *);
static int	 pfkey_sa_add(int, union ldpd_addr *, union ldpd_addr *,
		    uint8_t, char *, uint32_t *);
static int	 pfkey_sa_remove(int, union ldpd_addr *, union ldpd_addr *,
		    uint32_t *);
static int	 pfkey_md5sig_establish(struct nbr *, struct ldp_auth *);
static int	 pfkey_md5sig_remove(struct nbr *);

#define	PFKEY2_CHUNK sizeof(uint64_t)
#define	ROUNDUP(x) (((x) + (PFKEY2_CHUNK - 1)) & ~(PFKEY2_CHUNK - 1))
#define	IOV_CNT	20

static uint32_t	 sadb_msg_seq;
static uint32_t	 pid; /* should pid_t but pfkey needs uint32_t */
static int	 fd;

static int
pfkey_send(int sd, uint8_t satype, uint8_t mtype, uint8_t dir,
    int af, union ldpd_addr *src, union ldpd_addr *dst, uint32_t spi,
    uint8_t aalg, int alen, char *akey, uint8_t ealg, int elen, char *ekey,
    uint16_t sport, uint16_t dport)
{
	struct sadb_msg		smsg;
	struct sadb_sa		sa;
	struct sadb_address	sa_src, sa_dst;
	struct sadb_key		sa_akey, sa_ekey;
	struct sadb_spirange	sa_spirange;
	struct iovec		iov[IOV_CNT];
	ssize_t			n;
	int			len = 0;
	int			iov_cnt;
	struct sockaddr_storage	ssrc, sdst, smask, dmask;
	struct sockaddr		*saptr;

	if (!pid)
		pid = getpid();

	/* we need clean sockaddr... no ports set */
	memset(&ssrc, 0, sizeof(ssrc));
	memset(&smask, 0, sizeof(smask));
	if ((saptr = addr2sa(af, src, 0)))
		memcpy(&ssrc, saptr, sizeof(ssrc));
	switch (af) {
	case AF_INET:
		memset(&((struct sockaddr_in *)&smask)->sin_addr, 0xff, 32/8);
		break;
	case AF_INET6:
		memset(&((struct sockaddr_in6 *)&smask)->sin6_addr, 0xff,
		    128/8);
		break;
	default:
		return (-1);
	}
	smask.ss_family = ssrc.ss_family;
	smask.ss_len = ssrc.ss_len;

	memset(&sdst, 0, sizeof(sdst));
	memset(&dmask, 0, sizeof(dmask));
	if ((saptr = addr2sa(af, dst, 0)))
		memcpy(&sdst, saptr, sizeof(sdst));
	switch (af) {
	case AF_INET:
		memset(&((struct sockaddr_in *)&dmask)->sin_addr, 0xff, 32/8);
		break;
	case AF_INET6:
		memset(&((struct sockaddr_in6 *)&dmask)->sin6_addr, 0xff,
		    128/8);
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
		sa.sadb_sa_spi = spi;
		sa.sadb_sa_state = SADB_SASTATE_MATURE;
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
		log_warn("writev (%d/%d)", iov_cnt, len);
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
			*h = hdr;
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
pfkey_reply(int sd, uint32_t *spip)
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
		if (errno == ESRCH)
			return (0);
		else {
			log_warn("pfkey");
			return (-1);
		}
	}
	if ((data = reallocarray(NULL, hdr.sadb_msg_len, PFKEY2_CHUNK)) == NULL) {
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
		if (spip == NULL) {
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
				*spip = sa->sadb_sa_spi;
				break;
			}
		}
	}
	freezero(data, len);
	return (0);
}

static int
pfkey_sa_add(int af, union ldpd_addr *src, union ldpd_addr *dst, uint8_t keylen,
    char *key, uint32_t *spi)
{
	if (pfkey_send(fd, SADB_X_SATYPE_TCPSIGNATURE, SADB_GETSPI, 0,
	    af, src, dst, 0, 0, 0, NULL, 0, 0, NULL, 0, 0) < 0)
		return (-1);
	if (pfkey_reply(fd, spi) < 0)
		return (-1);
	if (pfkey_send(fd, SADB_X_SATYPE_TCPSIGNATURE, SADB_UPDATE, 0,
	    af, src, dst, *spi, 0, keylen, key, 0, 0, NULL, 0, 0) < 0)
		return (-1);
	if (pfkey_reply(fd, NULL) < 0)
		return (-1);
	return (0);
}

static int
pfkey_sa_remove(int af, union ldpd_addr *src, union ldpd_addr *dst,
    uint32_t *spi)
{
	if (pfkey_send(fd, SADB_X_SATYPE_TCPSIGNATURE, SADB_DELETE, 0,
	    af, src, dst, *spi, 0, 0, NULL, 0, 0, NULL, 0, 0) < 0)
		return (-1);
	if (pfkey_reply(fd, NULL) < 0)
		return (-1);
	*spi = 0;
	return (0);
}

static int
pfkey_md5sig_establish(struct nbr *nbr, struct ldp_auth *auth)
{
	sleep(1);

	pfkey_md5sig_remove(nbr);

	if (pfkey_sa_add(nbr->af, &nbr->laddr, &nbr->raddr,
	    auth->md5key_len, auth->md5key, &nbr->auth_spi_out) == -1)
		return (-1);

	if (pfkey_sa_add(nbr->af, &nbr->raddr, &nbr->laddr,
	    auth->md5key_len, auth->md5key, &nbr->auth_spi_in) == -1)
		return (-1);

	nbr->auth_established = 1;

	return (0);
}

static int
pfkey_md5sig_remove(struct nbr *nbr)
{
	if (nbr->auth_spi_out) {
		if (pfkey_sa_remove(nbr->af, &nbr->laddr, &nbr->raddr,
		    &nbr->auth_spi_out) == -1)
			return (-1);
	}
	if (nbr->auth_spi_in) {
		if (pfkey_sa_remove(nbr->af, &nbr->raddr, &nbr->laddr,
		    &nbr->auth_spi_in) == -1)
			return (-1);
	}

	nbr->auth_established = 0;
	nbr->auth_spi_in = 0;
	nbr->auth_spi_out = 0;

	return (0);
}

static struct ldp_auth *
pfkey_find_auth(struct ldpd_conf *conf, struct nbr *nbr)
{
	struct ldp_auth *auth, *match = NULL;

	LIST_FOREACH(auth, &conf->auth_list, entry) {
		in_addr_t mask = prefixlen2mask(auth->idlen);
		if ((auth->id.s_addr & mask) != (nbr->id.s_addr & mask))
			continue;

		if (match == NULL ||
		    match->idlen < auth->idlen)
			match = auth;
	}

	return (match);
}

int
pfkey_establish(struct ldpd_conf *conf, struct nbr *nbr)
{
	struct ldp_auth *auth = NULL;

	auth = pfkey_find_auth(conf, nbr);
	if (auth == NULL || /* no policy */
	    auth->md5key_len == 0) /* "no tcpmd5 sig" */
		return (0);
	
	return (pfkey_md5sig_establish(nbr, auth));
}

int
pfkey_remove(struct nbr *nbr)
{
	return (pfkey_md5sig_remove(nbr));
}

int
pfkey_init(void)
{
	if ((fd = socket(PF_KEY, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_KEY_V2)) == -1) {
		if (errno == EPROTONOSUPPORT) {
			log_warnx("PF_KEY not available");
			sysdep.no_pfkey = 1;
			return (-1);
		} else
			fatal("pfkey setup failed");
	}
	return (fd);
}
