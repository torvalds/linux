/*	$OpenBSD: ikev2_msg.c,v 1.104 2025/07/09 06:48:09 yasuoka Exp $	*/

/*
 * Copyright (c) 2019 Tobias Heider <tobias.heider@stusta.de>
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/socket.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <endian.h>
#include <errno.h>
#include <err.h>
#include <event.h>

#include <openssl/sha.h>
#include <openssl/evp.h>

#include "iked.h"
#include "ikev2.h"
#include "eap.h"
#include "dh.h"

void	 ikev1_recv(struct iked *, struct iked_message *);
void	 ikev2_msg_response_timeout(struct iked *, void *);
void	 ikev2_msg_retransmit_timeout(struct iked *, void *);
int	 ikev2_check_frag_oversize(struct iked_sa *, struct ibuf *);
int	 ikev2_send_encrypted_fragments(struct iked *, struct iked_sa *,
	    struct ibuf *, uint8_t, uint8_t, int);
int	 ikev2_msg_encrypt_prepare(struct iked_sa *, struct ikev2_payload *,
	    struct ibuf*, struct ibuf *, struct ike_header *, uint8_t, int);

void
ikev2_msg_cb(int fd, short event, void *arg)
{
	struct iked_socket	*sock = arg;
	struct iked		*env = sock->sock_env;
	struct iked_message	 msg;
	struct ike_header	 hdr;
	uint32_t		 natt = 0x00000000;
	uint8_t			 buf[IKED_MSGBUF_MAX];
	ssize_t			 len;
	off_t			 off;

	bzero(&msg, sizeof(msg));
	bzero(buf, sizeof(buf));

	msg.msg_peerlen = sizeof(msg.msg_peer);
	msg.msg_locallen = sizeof(msg.msg_local);
	msg.msg_parent = &msg;
	memcpy(&msg.msg_local, &sock->sock_addr, sizeof(sock->sock_addr));

	if ((len = recvfromto(fd, buf, sizeof(buf), 0,
	    (struct sockaddr *)&msg.msg_peer, &msg.msg_peerlen,
	    (struct sockaddr *)&msg.msg_local, &msg.msg_locallen)) <
	    (ssize_t)sizeof(natt))
		return;

	if (socket_getport((struct sockaddr *)&msg.msg_local) ==
	    env->sc_nattport) {
		if (memcmp(&natt, buf, sizeof(natt)) != 0)
			return;
		msg.msg_natt = 1;
		off = sizeof(natt);
	} else
		off = 0;

	if ((size_t)(len - off) <= sizeof(hdr))
		return;
	memcpy(&hdr, buf + off, sizeof(hdr));

	if ((msg.msg_data = ibuf_new(buf + off, len - off)) == NULL)
		return;

	TAILQ_INIT(&msg.msg_proposals);
	SIMPLEQ_INIT(&msg.msg_certreqs);
	msg.msg_fd = fd;

	if (hdr.ike_version == IKEV1_VERSION)
		ikev1_recv(env, &msg);
	else
		ikev2_recv(env, &msg);

	ikev2_msg_cleanup(env, &msg);
}

void
ikev1_recv(struct iked *env, struct iked_message *msg)
{
	struct ike_header	*hdr;

	if (ibuf_size(msg->msg_data) <= sizeof(*hdr)) {
		log_debug("%s: short message", __func__);
		return;
	}

	hdr = (struct ike_header *)ibuf_data(msg->msg_data);

	log_debug("%s: header ispi %s rspi %s"
	    " nextpayload %u version 0x%02x exchange %u flags 0x%02x"
	    " msgid %u length %u", __func__,
	    print_spi(betoh64(hdr->ike_ispi), 8),
	    print_spi(betoh64(hdr->ike_rspi), 8),
	    hdr->ike_nextpayload,
	    hdr->ike_version,
	    hdr->ike_exchange,
	    hdr->ike_flags,
	    betoh32(hdr->ike_msgid),
	    betoh32(hdr->ike_length));

	log_debug("%s: IKEv1 not supported", __func__);
}

struct ibuf *
ikev2_msg_init(struct iked *env, struct iked_message *msg,
    struct sockaddr_storage *peer, socklen_t peerlen,
    struct sockaddr_storage *local, socklen_t locallen, int response)
{
	bzero(msg, sizeof(*msg));
	memcpy(&msg->msg_peer, peer, peerlen);
	msg->msg_peerlen = peerlen;
	memcpy(&msg->msg_local, local, locallen);
	msg->msg_locallen = locallen;
	msg->msg_response = response ? 1 : 0;
	msg->msg_fd = -1;
	msg->msg_data = ibuf_static();
	msg->msg_e = 0;
	msg->msg_parent = msg;	/* has to be set */
	TAILQ_INIT(&msg->msg_proposals);

	return (msg->msg_data);
}

struct iked_message *
ikev2_msg_copy(struct iked *env, struct iked_message *msg)
{
	struct iked_message		*m = NULL;
	struct ibuf			*buf;
	size_t				 len;
	void				*ptr;

	if (ibuf_size(msg->msg_data) < msg->msg_offset)
		return (NULL);
	len = ibuf_size(msg->msg_data) - msg->msg_offset;

	if ((m = malloc(sizeof(*m))) == NULL)
		return (NULL);

	if ((ptr = ibuf_seek(msg->msg_data, msg->msg_offset, len)) == NULL ||
	    (buf = ikev2_msg_init(env, m, &msg->msg_peer, msg->msg_peerlen,
	     &msg->msg_local, msg->msg_locallen, msg->msg_response)) == NULL ||
	    ibuf_add(buf, ptr, len)) {
		free(m);
		return (NULL);
	}

	m->msg_fd = msg->msg_fd;
	m->msg_msgid = msg->msg_msgid;
	m->msg_offset = msg->msg_offset;
	m->msg_sa = msg->msg_sa;

	return (m);
}

void
ikev2_msg_cleanup(struct iked *env, struct iked_message *msg)
{
	struct iked_certreq	*cr;
	int			 i;

	if (msg == msg->msg_parent) {
		ibuf_free(msg->msg_nonce);
		ibuf_free(msg->msg_ke);
		ibuf_free(msg->msg_auth.id_buf);
		ibuf_free(msg->msg_peerid.id_buf);
		ibuf_free(msg->msg_localid.id_buf);
		ibuf_free(msg->msg_cert.id_buf);
		for (i = 0; i < IKED_SCERT_MAX; i++)
			ibuf_free(msg->msg_scert[i].id_buf);
		ibuf_free(msg->msg_cookie);
		ibuf_free(msg->msg_cookie2);
		ibuf_free(msg->msg_del_buf);
		ibuf_free(msg->msg_eapmsg);
		free(msg->msg_eap.eam_identity);
		free(msg->msg_eap.eam_user);
		free(msg->msg_cp_addr);
		free(msg->msg_cp_addr6);
		free(msg->msg_cp_dns);

		msg->msg_nonce = NULL;
		msg->msg_ke = NULL;
		msg->msg_auth.id_buf = NULL;
		msg->msg_peerid.id_buf = NULL;
		msg->msg_localid.id_buf = NULL;
		msg->msg_cert.id_buf = NULL;
		for (i = 0; i < IKED_SCERT_MAX; i++)
			msg->msg_scert[i].id_buf = NULL;
		msg->msg_cookie = NULL;
		msg->msg_cookie2 = NULL;
		msg->msg_del_buf = NULL;
		msg->msg_eapmsg = NULL;
		msg->msg_eap.eam_user = NULL;
		msg->msg_cp_addr = NULL;
		msg->msg_cp_addr6 = NULL;
		msg->msg_cp_dns = NULL;

		config_free_proposals(&msg->msg_proposals, 0);
		while ((cr = SIMPLEQ_FIRST(&msg->msg_certreqs))) {
			ibuf_free(cr->cr_data);
			SIMPLEQ_REMOVE_HEAD(&msg->msg_certreqs, cr_entry);
			free(cr);
		}
	}

	if (msg->msg_data != NULL) {
		ibuf_free(msg->msg_data);
		msg->msg_data = NULL;
	}
}

int
ikev2_msg_valid_ike_sa(struct iked *env, struct ike_header *oldhdr,
    struct iked_message *msg)
{
	if (msg->msg_sa != NULL && msg->msg_policy != NULL) {
		if (msg->msg_sa->sa_state == IKEV2_STATE_CLOSED)
			return (-1);
		/*
		 * Only permit informational requests from initiator
		 * on closing SAs (for DELETE).
		 */
		if (msg->msg_sa->sa_state == IKEV2_STATE_CLOSING) {
			if (((oldhdr->ike_flags &
			    (IKEV2_FLAG_INITIATOR|IKEV2_FLAG_RESPONSE)) ==
			    IKEV2_FLAG_INITIATOR) &&
			    (oldhdr->ike_exchange ==
			    IKEV2_EXCHANGE_INFORMATIONAL))
				return (0);
			return (-1);
		}
		return (0);
	}

	/* Always fail */
	return (-1);
}

int
ikev2_msg_send(struct iked *env, struct iked_message *msg)
{
	struct iked_sa		*sa = msg->msg_sa;
	struct ibuf		*buf = msg->msg_data;
	uint32_t		 natt = 0x00000000;
	int			 isnatt = 0;
	uint8_t			 exchange, flags;
	struct ike_header	*hdr;
	struct iked_message	*m;

	if (buf == NULL || (hdr = ibuf_seek(msg->msg_data,
	    msg->msg_offset, sizeof(*hdr))) == NULL)
		return (-1);

	isnatt = (msg->msg_natt || (sa && sa->sa_natt));

	exchange = hdr->ike_exchange;
	flags = hdr->ike_flags;
	logit(exchange == IKEV2_EXCHANGE_INFORMATIONAL ?  LOG_DEBUG : LOG_INFO,
	    "%ssend %s %s %u peer %s local %s, %zu bytes%s",
	    SPI_IH(hdr),
	    print_map(exchange, ikev2_exchange_map),
	    (flags & IKEV2_FLAG_RESPONSE) ? "res" : "req",
	    betoh32(hdr->ike_msgid),
	    print_addr(&msg->msg_peer),
	    print_addr(&msg->msg_local),
	    ibuf_size(buf), isnatt ? ", NAT-T" : "");

	if (isnatt) {
		struct ibuf *new;
		if ((new = ibuf_new(&natt, sizeof(natt))) == NULL) {
			log_debug("%s: failed to set NAT-T", __func__);
			return (-1);
		}
		if (ibuf_add_ibuf(new, buf) == -1) {
			ibuf_free(new);
			log_debug("%s: failed to set NAT-T", __func__);
			return (-1);
		}
		ibuf_free(buf);
		buf = msg->msg_data = new;
	}

	if (sendtofrom(msg->msg_fd, ibuf_data(buf), ibuf_size(buf), 0,
	    (struct sockaddr *)&msg->msg_peer, msg->msg_peerlen,
	    (struct sockaddr *)&msg->msg_local, msg->msg_locallen) == -1) {
		log_warn("%s: sendtofrom", __func__);
		if (sa != NULL && errno == EADDRNOTAVAIL) {
			sa_state(env, sa, IKEV2_STATE_CLOSING);
			timer_del(env, &sa->sa_timer);
			timer_set(env, &sa->sa_timer,
			    ikev2_ike_sa_timeout, sa);
			timer_add(env, &sa->sa_timer,
			    IKED_IKE_SA_DELETE_TIMEOUT);
		}
		ikestat_inc(env, ikes_msg_send_failures);
	} else
		ikestat_inc(env, ikes_msg_sent);

	if (sa == NULL)
		return (0);

	if ((m = ikev2_msg_copy(env, msg)) == NULL) {
		log_debug("%s: failed to copy a message", __func__);
		return (-1);
	}
	m->msg_exchange = exchange;

	if (flags & IKEV2_FLAG_RESPONSE) {
		if (ikev2_msg_enqueue(env, &sa->sa_responses, m,
		    IKED_RESPONSE_TIMEOUT) != 0) {
			ikev2_msg_cleanup(env, m);
			free(m);
			return (-1);
		}
	} else {
		if (ikev2_msg_enqueue(env, &sa->sa_requests, m,
		    IKED_RETRANSMIT_TIMEOUT) != 0) {
			ikev2_msg_cleanup(env, m);
			free(m);
			return (-1);
		}
	}

	return (0);
}

uint32_t
ikev2_msg_id(struct iked *env, struct iked_sa *sa)
{
	uint32_t		id = sa->sa_reqid;

	if (++sa->sa_reqid == UINT32_MAX) {
		/* XXX we should close and renegotiate the connection now */
		log_debug("%s: IKEv2 message sequence overflow", __func__);
	}
	return (id);
}

/*
 * Calculate the final sizes of the IKEv2 header and the encrypted payload
 * header.  This must be done before encryption to make sure the correct
 * headers are authenticated.
 */
int
ikev2_msg_encrypt_prepare(struct iked_sa *sa, struct ikev2_payload *pld,
    struct ibuf *buf, struct ibuf *e, struct ike_header *hdr,
    uint8_t firstpayload, int fragmentation)
{
	size_t	 len, ivlen, encrlen, integrlen, blocklen, pldlen, outlen;

	if (sa == NULL ||
	    sa->sa_encr == NULL ||
	    sa->sa_integr == NULL) {
		log_debug("%s: invalid SA", __func__);
		return (-1);
	}

	len = ibuf_size(e);
	blocklen = cipher_length(sa->sa_encr);
	integrlen = hash_length(sa->sa_integr);
	ivlen = cipher_ivlength(sa->sa_encr);
	encrlen = roundup(len + 1, blocklen);
	outlen = cipher_outlength(sa->sa_encr, encrlen);
	pldlen = ivlen + outlen + integrlen;

	if (ikev2_next_payload(pld,
	    pldlen + (fragmentation ? sizeof(struct ikev2_frag_payload) : 0),
	    firstpayload) == -1)
		return (-1);
	if (ikev2_set_header(hdr, ibuf_size(buf) + pldlen - sizeof(*hdr)) == -1)
		return (-1);

	return (0);
}

struct ibuf *
ikev2_msg_encrypt(struct iked *env, struct iked_sa *sa, struct ibuf *src,
    struct ibuf *aad)
{
	size_t			 len, encrlen, integrlen, blocklen,
				    outlen;
	uint8_t			*buf, pad = 0, *ptr;
	struct ibuf		*encr, *dst = NULL, *out = NULL;

	buf = ibuf_data(src);
	len = ibuf_size(src);

	log_debug("%s: decrypted length %zu", __func__, len);
	print_hex(buf, 0, len);

	if (sa == NULL ||
	    sa->sa_encr == NULL ||
	    sa->sa_integr == NULL) {
		log_debug("%s: invalid SA", __func__);
		goto done;
	}

	if (sa->sa_hdr.sh_initiator)
		encr = sa->sa_key_iencr;
	else
		encr = sa->sa_key_rencr;

	blocklen = cipher_length(sa->sa_encr);
	integrlen = hash_length(sa->sa_integr);
	encrlen = roundup(len + sizeof(pad), blocklen);
	pad = encrlen - (len + sizeof(pad));

	/*
	 * Pad the payload and encrypt it
	 */
	if (pad) {
		if ((ptr = ibuf_reserve(src, pad)) == NULL)
			goto done;
		arc4random_buf(ptr, pad);
	}
	if (ibuf_add(src, &pad, sizeof(pad)) != 0)
		goto done;

	log_debug("%s: padded length %zu", __func__, ibuf_size(src));
	print_hexbuf(src);

	cipher_setkey(sa->sa_encr, ibuf_data(encr), ibuf_size(encr));
	cipher_setiv(sa->sa_encr, NULL, 0);	/* XXX ivlen */
	if (cipher_init_encrypt(sa->sa_encr) == -1) {
		log_info("%s: error initiating cipher.", __func__);
		goto done;
	}

	if ((dst = ibuf_dup(sa->sa_encr->encr_iv)) == NULL)
		goto done;

	if ((out = ibuf_new(NULL,
	    cipher_outlength(sa->sa_encr, encrlen))) == NULL)
		goto done;

	outlen = ibuf_size(out);

	/* Add AAD for AEAD ciphers */
	if (sa->sa_integr->hash_isaead)
		cipher_aad(sa->sa_encr, ibuf_data(aad), ibuf_size(aad),
		    &outlen);

	if (cipher_update(sa->sa_encr, ibuf_data(src), encrlen,
	    ibuf_data(out), &outlen) == -1) {
		log_info("%s: error updating cipher.", __func__);
		goto done;
	}

	if (cipher_final(sa->sa_encr) == -1) {
		log_info("%s: encryption failed.", __func__);
		goto done;
	}

	if (outlen && ibuf_add(dst, ibuf_data(out), outlen) != 0)
		goto done;

	if ((ptr = ibuf_reserve(dst, integrlen)) == NULL)
		goto done;
	explicit_bzero(ptr, integrlen);

	log_debug("%s: length %zu, padding %d, output length %zu",
	    __func__, len + sizeof(pad), pad, ibuf_size(dst));
	print_hexbuf(dst);

	ibuf_free(src);
	ibuf_free(out);
	return (dst);
 done:
	ibuf_free(src);
	ibuf_free(out);
	ibuf_free(dst);
	return (NULL);
}

int
ikev2_msg_integr(struct iked *env, struct iked_sa *sa, struct ibuf *src)
{
	int			 ret = -1;
	size_t			 integrlen, tmplen;
	struct ibuf		*integr, *tmp = NULL;
	uint8_t			*ptr;

	log_debug("%s: message length %zu", __func__, ibuf_size(src));
	print_hexbuf(src);

	if (sa == NULL ||
	    sa->sa_encr == NULL ||
	    sa->sa_integr == NULL) {
		log_debug("%s: invalid SA", __func__);
		return (-1);
	}

	integrlen = hash_length(sa->sa_integr);
	log_debug("%s: integrity checksum length %zu", __func__,
	    integrlen);

	/*
	 * Validate packet checksum
	 */
	if ((tmp = ibuf_new(NULL, hash_keylength(sa->sa_integr))) == NULL)
		goto done;

	if (!sa->sa_integr->hash_isaead) {
		if (sa->sa_hdr.sh_initiator)
			integr = sa->sa_key_iauth;
		else
			integr = sa->sa_key_rauth;

		hash_setkey(sa->sa_integr, ibuf_data(integr),
		    ibuf_size(integr));
		hash_init(sa->sa_integr);
		hash_update(sa->sa_integr, ibuf_data(src),
		    ibuf_size(src) - integrlen);
		hash_final(sa->sa_integr, ibuf_data(tmp), &tmplen);

		if (tmplen != integrlen) {
			log_debug("%s: hash failure", __func__);
			goto done;
		}
	} else {
		/* Append AEAD tag */
		if (cipher_gettag(sa->sa_encr, ibuf_data(tmp), ibuf_size(tmp)))
			goto done;
	}

	if ((ptr = ibuf_seek(src,
	    ibuf_size(src) - integrlen, integrlen)) == NULL)
		goto done;
	memcpy(ptr, ibuf_data(tmp), integrlen);

	print_hexbuf(tmp);

	ret = 0;
 done:
	ibuf_free(tmp);

	return (ret);
}

struct ibuf *
ikev2_msg_decrypt(struct iked *env, struct iked_sa *sa,
    struct ibuf *msg, struct ibuf *src)
{
	ssize_t			 ivlen, encrlen, integrlen, blocklen,
				    outlen, tmplen;
	uint8_t			 pad = 0, *ptr, *integrdata;
	struct ibuf		*integr, *encr, *tmp = NULL, *out = NULL;
	off_t			 ivoff, encroff, integroff;

	if (sa == NULL ||
	    sa->sa_encr == NULL ||
	    sa->sa_integr == NULL) {
		log_debug("%s: invalid SA", __func__);
		print_hexbuf(src);
		goto done;
	}

	if (!sa->sa_hdr.sh_initiator) {
		encr = sa->sa_key_iencr;
		integr = sa->sa_key_iauth;
	} else {
		encr = sa->sa_key_rencr;
		integr = sa->sa_key_rauth;
	}

	blocklen = cipher_length(sa->sa_encr);
	ivlen = cipher_ivlength(sa->sa_encr);
	ivoff = 0;
	integrlen = hash_length(sa->sa_integr);
	integroff = ibuf_size(src) - integrlen;
	encroff = ivlen;
	encrlen = ibuf_size(src) - integrlen - ivlen;

	if (encrlen < 0 || integroff < 0) {
		log_debug("%s: invalid integrity value", __func__);
		goto done;
	}

	log_debug("%s: IV length %zd", __func__, ivlen);
	print_hex(ibuf_data(src), 0, ivlen);
	log_debug("%s: encrypted payload length %zd", __func__, encrlen);
	print_hex(ibuf_data(src), encroff, encrlen);
	log_debug("%s: integrity checksum length %zd", __func__, integrlen);
	print_hex(ibuf_data(src), integroff, integrlen);

	/*
	 * Validate packet checksum
	 */
	if (!sa->sa_integr->hash_isaead) {
		if ((tmp = ibuf_new(NULL, hash_keylength(sa->sa_integr))) == NULL)
			goto done;

		hash_setkey(sa->sa_integr, ibuf_data(integr),
		    ibuf_size(integr));
		hash_init(sa->sa_integr);
		hash_update(sa->sa_integr, ibuf_data(msg),
		    ibuf_size(msg) - integrlen);
		hash_final(sa->sa_integr, ibuf_data(tmp), &tmplen);

		integrdata = ibuf_seek(src, integroff, integrlen);
		if (integrdata == NULL)
			goto done;
		if (memcmp(ibuf_data(tmp), integrdata, integrlen) != 0) {
			log_debug("%s: integrity check failed", __func__);
			goto done;
		}

		log_debug("%s: integrity check succeeded", __func__);
		print_hex(ibuf_data(tmp), 0, tmplen);

		ibuf_free(tmp);
		tmp = NULL;
	}

	/*
	 * Decrypt the payload and strip any padding
	 */
	if ((encrlen % blocklen) != 0) {
		log_debug("%s: unaligned encrypted payload", __func__);
		goto done;
	}

	cipher_setkey(sa->sa_encr, ibuf_data(encr), ibuf_size(encr));
	cipher_setiv(sa->sa_encr, ibuf_seek(src, ivoff, ivlen), ivlen);
	if (cipher_init_decrypt(sa->sa_encr) == -1) {
		log_info("%s: error initiating cipher.", __func__);
		goto done;
	}

	/* Set AEAD tag */
	if (sa->sa_integr->hash_isaead) {
		integrdata = ibuf_seek(src, integroff, integrlen);
		if (integrdata == NULL)
			goto done;
		if (cipher_settag(sa->sa_encr, integrdata, integrlen)) {
			log_info("%s: failed to set tag.", __func__);
			goto done;
		}
	}

	if ((out = ibuf_new(NULL, cipher_outlength(sa->sa_encr,
	    encrlen))) == NULL)
		goto done;

	/*
	 * Add additional authenticated data for AEAD ciphers
	 */
	if (sa->sa_integr->hash_isaead) {
		log_debug("%s: AAD length %zu", __func__,
		    ibuf_size(msg) - ibuf_size(src));
		print_hex(ibuf_data(msg), 0, ibuf_size(msg) - ibuf_size(src));
		cipher_aad(sa->sa_encr, ibuf_data(msg),
		    ibuf_size(msg) - ibuf_size(src), &outlen);
	}

	if ((outlen = ibuf_size(out)) != 0) {
		if (cipher_update(sa->sa_encr, ibuf_seek(src, encroff, encrlen),
		    encrlen, ibuf_data(out), &outlen) == -1) {
			log_info("%s: error updating cipher.", __func__);
			goto done;
		}

		ptr = ibuf_seek(out, outlen - 1, 1);
		pad = *ptr;
	}

	if (cipher_final(sa->sa_encr) == -1) {
		log_info("%s: decryption failed.", __func__);
		goto done;
	}

	log_debug("%s: decrypted payload length %zd/%zd padding %d",
	    __func__, outlen, encrlen, pad);
	print_hexbuf(out);

	/* Strip padding and padding length */
	if (ibuf_setsize(out, outlen - pad - 1) != 0)
		goto done;

	ibuf_free(src);
	return (out);
 done:
	ibuf_free(tmp);
	ibuf_free(out);
	ibuf_free(src);
	return (NULL);
}

int
ikev2_check_frag_oversize(struct iked_sa *sa, struct ibuf *buf) {
	size_t		len = ibuf_length(buf);
	sa_family_t	sa_fam;
	size_t		max;
	size_t		ivlen, integrlen, blocklen;

	if (sa == NULL ||
	    sa->sa_encr == NULL ||
	    sa->sa_integr == NULL) {
		log_debug("%s: invalid SA", __func__);
		return (-1);
	}

	sa_fam = ((struct sockaddr *)&sa->sa_local.addr)->sa_family;

	max = sa_fam == AF_INET ? IKEV2_MAXLEN_IPV4_FRAG
	    : IKEV2_MAXLEN_IPV6_FRAG;

	blocklen = cipher_length(sa->sa_encr);
	ivlen = cipher_ivlength(sa->sa_encr);
	integrlen = hash_length(sa->sa_integr);

	/* Estimated maximum packet size (with 0 < padding < blocklen) */
	return ((len + ivlen + blocklen + integrlen) >= max) && sa->sa_frag;
}

int
ikev2_msg_send_encrypt(struct iked *env, struct iked_sa *sa, struct ibuf **ep,
    uint8_t exchange, uint8_t firstpayload, int response)
{
	struct iked_message		 resp;
	struct ike_header		*hdr;
	struct ikev2_payload		*pld;
	struct ibuf			*buf, *e = *ep;
	int				 ret = -1;

	/* Check if msg needs to be fragmented */
	if (ikev2_check_frag_oversize(sa, e)) {
		return ikev2_send_encrypted_fragments(env, sa, e, exchange,
		    firstpayload, response);
	}

	if ((buf = ikev2_msg_init(env, &resp, &sa->sa_peer.addr,
	    sa->sa_peer.addr.ss_len, &sa->sa_local.addr,
	    sa->sa_local.addr.ss_len, response)) == NULL)
		goto done;

	resp.msg_msgid = response ? sa->sa_msgid_current : ikev2_msg_id(env, sa);

	/* IKE header */
	if ((hdr = ikev2_add_header(buf, sa, resp.msg_msgid, IKEV2_PAYLOAD_SK,
	    exchange, response ? IKEV2_FLAG_RESPONSE : 0)) == NULL)
		goto done;

	if ((pld = ikev2_add_payload(buf)) == NULL)
		goto done;

	if (ikev2_msg_encrypt_prepare(sa, pld, buf, e, hdr, firstpayload, 0) == -1)
		goto done;

	/* Encrypt message and add as an E payload */
	if ((e = ikev2_msg_encrypt(env, sa, e, buf)) == NULL) {
		log_debug("%s: encryption failed", __func__);
		goto done;
	}
	if (ibuf_add_ibuf(buf, e) != 0)
		goto done;

	/* Add integrity checksum (HMAC) */
	if (ikev2_msg_integr(env, sa, buf) != 0) {
		log_debug("%s: integrity checksum failed", __func__);
		goto done;
	}

	resp.msg_data = buf;
	resp.msg_sa = sa;
	resp.msg_fd = sa->sa_fd;
	TAILQ_INIT(&resp.msg_proposals);

	(void)ikev2_pld_parse(env, hdr, &resp, 0);

	ret = ikev2_msg_send(env, &resp);

 done:
	/* e is cleaned up by the calling function */
	*ep = e;
	ikev2_msg_cleanup(env, &resp);

	return (ret);
}

int
ikev2_send_encrypted_fragments(struct iked *env, struct iked_sa *sa,
    struct ibuf *in, uint8_t exchange, uint8_t firstpayload, int response) {
	struct iked_message		 resp;
	struct ibuf			*buf, *e = NULL;
	struct ike_header		*hdr;
	struct ikev2_payload		*pld;
	struct ikev2_frag_payload	*frag;
	sa_family_t			 sa_fam;
	size_t				 ivlen, integrlen, blocklen;
	size_t				 max_len, left,  offset=0;
	size_t				 frag_num = 1, frag_total;
	uint8_t				*data;
	uint32_t			 msgid;
	int				 ret = -1;

	if (sa == NULL ||
	    sa->sa_encr == NULL ||
	    sa->sa_integr == NULL) {
		log_debug("%s: invalid SA", __func__);
		ikestat_inc(env, ikes_frag_send_failures);
		return ret;
	}

	sa_fam = ((struct sockaddr *)&sa->sa_local.addr)->sa_family;

	left = ibuf_length(in);

	/* Calculate max allowed size of a fragments payload */
	blocklen = cipher_length(sa->sa_encr);
	ivlen = cipher_ivlength(sa->sa_encr);
	integrlen = hash_length(sa->sa_integr);
	max_len = (sa_fam == AF_INET ? IKEV2_MAXLEN_IPV4_FRAG
	    : IKEV2_MAXLEN_IPV6_FRAG)
	    - ivlen - blocklen - integrlen;

	/* Total number of fragments to send */
	frag_total = (left / max_len) + 1;

	msgid = response ? sa->sa_msgid_current : ikev2_msg_id(env, sa);

	while (frag_num <= frag_total) {
		if ((buf = ikev2_msg_init(env, &resp, &sa->sa_peer.addr,
		    sa->sa_peer.addr.ss_len, &sa->sa_local.addr,
		    sa->sa_local.addr.ss_len, response)) == NULL)
			goto done;

		resp.msg_msgid = msgid;

		/* IKE header */
		if ((hdr = ikev2_add_header(buf, sa, resp.msg_msgid,
		    IKEV2_PAYLOAD_SKF, exchange, response ? IKEV2_FLAG_RESPONSE
		    : 0)) == NULL)
			goto done;

		/* Payload header */
		if ((pld = ikev2_add_payload(buf)) == NULL)
			goto done;

		/* Fragment header */
		if ((frag = ibuf_reserve(buf, sizeof(*frag))) == NULL) {
			log_debug("%s: failed to add SKF fragment header",
			    __func__);
			goto done;
		}
		frag->frag_num = htobe16(frag_num);
		frag->frag_total = htobe16(frag_total);

		/* Encrypt message and add as an E payload */
		data = ibuf_seek(in, offset, 0);
		if ((e = ibuf_new(data, MINIMUM(left, max_len))) == NULL) {
			goto done;
		}

		if (ikev2_msg_encrypt_prepare(sa, pld, buf, e, hdr,
		    firstpayload, 1) == -1)
			goto done;

		if ((e = ikev2_msg_encrypt(env, sa, e, buf)) == NULL) {
			log_debug("%s: encryption failed", __func__);
			goto done;
		}
		if (ibuf_add_ibuf(buf, e) != 0)
			goto done;

		/* Add integrity checksum (HMAC) */
		if (ikev2_msg_integr(env, sa, buf) != 0) {
			log_debug("%s: integrity checksum failed", __func__);
			goto done;
		}

		log_debug("%s: Fragment %zu of %zu has size of %zu bytes.",
		    __func__, frag_num, frag_total,
		    ibuf_size(buf) - sizeof(*hdr));
		print_hexbuf(buf);

		resp.msg_data = buf;
		resp.msg_sa = sa;
		resp.msg_fd = sa->sa_fd;
		TAILQ_INIT(&resp.msg_proposals);

		if (ikev2_msg_send(env, &resp) == -1)
			goto done;

		ikestat_inc(env, ikes_frag_sent);

		offset += MINIMUM(left, max_len);
		left -= MINIMUM(left, max_len);
		frag_num++;

		/* MUST be zero after first fragment */
		firstpayload = 0;

		ikev2_msg_cleanup(env, &resp);
		ibuf_free(e);
		e = NULL;
	}

	return 0;
done:
	ikev2_msg_cleanup(env, &resp);
	ibuf_free(e);
	ikestat_inc(env, ikes_frag_send_failures);
	return ret;
}

struct ibuf *
ikev2_msg_auth(struct iked *env, struct iked_sa *sa, int response)
{
	struct ibuf		*authmsg = NULL, *nonce, *prfkey, *buf;
	uint8_t			*ptr;
	struct iked_id		*id;
	size_t			 tmplen;

	/*
	 * Create the payload to be signed/MAC'ed for AUTH
	 */

	if (!response) {
		if ((nonce = sa->sa_rnonce) == NULL ||
		    (sa->sa_iid.id_type == 0) ||
		    (prfkey = sa->sa_key_iprf) == NULL ||
		    (buf = sa->sa_1stmsg) == NULL)
			return (NULL);
		id = &sa->sa_iid;
	} else {
		if ((nonce = sa->sa_inonce) == NULL ||
		    (sa->sa_rid.id_type == 0) ||
		    (prfkey = sa->sa_key_rprf) == NULL ||
		    (buf = sa->sa_2ndmsg) == NULL)
			return (NULL);
		id = &sa->sa_rid;
	}

	if ((authmsg = ibuf_dup(buf)) == NULL)
		return (NULL);
	if (ibuf_add_ibuf(authmsg, nonce) != 0)
		goto fail;

	if ((hash_setkey(sa->sa_prf, ibuf_data(prfkey),
	    ibuf_size(prfkey))) == NULL)
		goto fail;

	/* require non-truncating hash */
	if (hash_keylength(sa->sa_prf) != hash_length(sa->sa_prf))
		goto fail;

	if ((ptr = ibuf_reserve(authmsg, hash_keylength(sa->sa_prf))) == NULL)
		goto fail;

	hash_init(sa->sa_prf);
	hash_update(sa->sa_prf, ibuf_data(id->id_buf), ibuf_size(id->id_buf));
	hash_final(sa->sa_prf, ptr, &tmplen);

	if (tmplen != hash_length(sa->sa_prf))
		goto fail;

	log_debug("%s: %s auth data length %zu",
	    __func__, response ? "responder" : "initiator",
	    ibuf_size(authmsg));
	print_hexbuf(authmsg);

	return (authmsg);

 fail:
	ibuf_free(authmsg);
	return (NULL);
}

int
ikev2_msg_authverify(struct iked *env, struct iked_sa *sa,
    struct iked_auth *auth, uint8_t *buf, size_t len, struct ibuf *authmsg)
{
	uint8_t				*key, *psk = NULL;
	ssize_t				 keylen;
	struct iked_id			*id;
	struct iked_dsa			*dsa = NULL;
	int				 ret = -1;
	uint8_t				 keytype;

	if (sa->sa_hdr.sh_initiator)
		id = &sa->sa_rcert;
	else
		id = &sa->sa_icert;

	if ((dsa = dsa_verify_new(auth->auth_method, sa->sa_prf)) == NULL) {
		log_debug("%s: invalid auth method", __func__);
		return (-1);
	}

	switch (auth->auth_method) {
	case IKEV2_AUTH_SHARED_KEY_MIC:
		if (!auth->auth_length) {
			log_debug("%s: no pre-shared key found", __func__);
			goto done;
		}
		if ((keylen = ikev2_psk(sa, auth->auth_data,
		    auth->auth_length, &psk)) == -1) {
			log_debug("%s: failed to get PSK", __func__);
			goto done;
		}
		key = psk;
		keytype = 0;
		break;
	default:
		if (!id->id_type || !ibuf_length(id->id_buf)) {
			log_debug("%s: no cert found", __func__);
			goto done;
		}
		key = ibuf_data(id->id_buf);
		keylen = ibuf_size(id->id_buf);
		keytype = id->id_type;
		break;
	}

	log_debug("%s: method %s keylen %zd type %s", __func__,
	    print_map(auth->auth_method, ikev2_auth_map), keylen,
	    print_map(id->id_type, ikev2_cert_map));

	if (dsa_setkey(dsa, key, keylen, keytype) == NULL ||
	    dsa_init(dsa, buf, len) != 0 ||
	    dsa_update(dsa, ibuf_data(authmsg), ibuf_size(authmsg))) {
		log_debug("%s: failed to compute digital signature", __func__);
		goto done;
	}

	if ((ret = dsa_verify_final(dsa, buf, len)) == 0) {
		log_debug("%s: authentication successful", __func__);
		sa_state(env, sa, IKEV2_STATE_AUTH_SUCCESS);
		sa_stateflags(sa, IKED_REQ_AUTHVALID);
	} else {
		log_debug("%s: authentication failed", __func__);
		sa_state(env, sa, IKEV2_STATE_AUTH_REQUEST);
	}

 done:
	free(psk);
	dsa_free(dsa);

	return (ret);
}

int
ikev2_msg_authsign(struct iked *env, struct iked_sa *sa,
    struct iked_auth *auth, struct ibuf *authmsg)
{
	uint8_t				*key, *psk = NULL;
	ssize_t				 keylen, siglen;
	struct iked_hash		*prf = sa->sa_prf;
	struct iked_id			*id;
	struct iked_dsa			*dsa = NULL;
	struct ibuf			*buf;
	int				 ret = -1;
	uint8_t			 keytype;

	if (sa->sa_hdr.sh_initiator)
		id = &sa->sa_icert;
	else
		id = &sa->sa_rcert;

	if ((dsa = dsa_sign_new(auth->auth_method, prf)) == NULL) {
		log_debug("%s: invalid auth method", __func__);
		return (-1);
	}

	switch (auth->auth_method) {
	case IKEV2_AUTH_SHARED_KEY_MIC:
		if (!auth->auth_length) {
			log_debug("%s: no pre-shared key found", __func__);
			goto done;
		}
		if ((keylen = ikev2_psk(sa, auth->auth_data,
		    auth->auth_length, &psk)) == -1) {
			log_debug("%s: failed to get PSK", __func__);
			goto done;
		}
		key = psk;
		keytype = 0;
		break;
	default:
		if (id == NULL) {
			log_debug("%s: no cert found", __func__);
			goto done;
		}
		key = ibuf_data(id->id_buf);
		keylen = ibuf_size(id->id_buf);
		keytype = id->id_type;
		break;
	}

	if (dsa_setkey(dsa, key, keylen, keytype) == NULL ||
	    dsa_init(dsa, NULL, 0) != 0 ||
	    dsa_update(dsa, ibuf_data(authmsg), ibuf_size(authmsg))) {
		log_debug("%s: failed to compute digital signature", __func__);
		goto done;
	}

	ibuf_free(sa->sa_localauth.id_buf);
	sa->sa_localauth.id_buf = NULL;

	if ((buf = ibuf_new(NULL, dsa_length(dsa))) == NULL) {
		log_debug("%s: failed to get auth buffer", __func__);
		goto done;
	}

	if ((siglen = dsa_sign_final(dsa,
	    ibuf_data(buf), ibuf_size(buf))) < 0) {
		log_debug("%s: failed to create auth signature", __func__);
		ibuf_free(buf);
		goto done;
	}

	if (ibuf_setsize(buf, siglen) < 0) {
		log_debug("%s: failed to set auth signature size to %zd",
		    __func__, siglen);
		ibuf_free(buf);
		goto done;
	}

	sa->sa_localauth.id_type = auth->auth_method;
	sa->sa_localauth.id_buf = buf;

	ret = 0;
 done:
	free(psk);
	dsa_free(dsa);

	return (ret);
}

int
ikev2_msg_frompeer(struct iked_message *msg)
{
	struct iked_sa		*sa = msg->msg_sa;
	struct ike_header	*hdr;

	msg = msg->msg_parent;

	if (sa == NULL ||
	    (hdr = ibuf_seek(msg->msg_data, 0, sizeof(*hdr))) == NULL)
		return (0);

	if (!sa->sa_hdr.sh_initiator &&
	    (hdr->ike_flags & IKEV2_FLAG_INITIATOR))
		return (1);
	else if (sa->sa_hdr.sh_initiator &&
	    (hdr->ike_flags & IKEV2_FLAG_INITIATOR) == 0)
		return (1);

	return (0);
}

struct iked_socket *
ikev2_msg_getsocket(struct iked *env, int af, int natt)
{
	switch (af) {
	case AF_INET:
		return (env->sc_sock4[natt ? 1 : 0]);
	case AF_INET6:
		return (env->sc_sock6[natt ? 1 : 0]);
	}

	log_debug("%s: af socket %d not available", __func__, af);
	return (NULL);
}

int
ikev2_msg_enqueue(struct iked *env, struct iked_msgqueue *queue,
    struct iked_message *msg, int timeout)
{
	struct iked_msg_retransmit *mr;

	if ((mr = ikev2_msg_lookup(env, queue, msg, msg->msg_exchange)) ==
	    NULL) {
		if ((mr = calloc(1, sizeof(*mr))) == NULL)
			return (-1);
		TAILQ_INIT(&mr->mrt_frags);
		mr->mrt_tries = 0;

		timer_set(env, &mr->mrt_timer, msg->msg_response ?
		    ikev2_msg_response_timeout : ikev2_msg_retransmit_timeout,
		    mr);
		timer_add(env, &mr->mrt_timer, timeout);

		TAILQ_INSERT_TAIL(queue, mr, mrt_entry);
	}

	TAILQ_INSERT_TAIL(&mr->mrt_frags, msg, msg_entry);

	return 0;
}

void
ikev2_msg_prevail(struct iked *env, struct iked_msgqueue *queue,
    struct iked_message *msg)
{
	struct iked_msg_retransmit	*mr, *mrtmp;

	TAILQ_FOREACH_SAFE(mr, queue, mrt_entry, mrtmp) {
		if (TAILQ_FIRST(&mr->mrt_frags)->msg_msgid < msg->msg_msgid)
			ikev2_msg_dispose(env, queue, mr);
	}
}

void
ikev2_msg_dispose(struct iked *env, struct iked_msgqueue *queue,
    struct iked_msg_retransmit *mr)
{
	struct iked_message	*m;

	while ((m = TAILQ_FIRST(&mr->mrt_frags)) != NULL) {
		TAILQ_REMOVE(&mr->mrt_frags, m, msg_entry);
		ikev2_msg_cleanup(env, m);
		free(m);
	}

	timer_del(env, &mr->mrt_timer);
	TAILQ_REMOVE(queue, mr, mrt_entry);
	free(mr);
}

void
ikev2_msg_flushqueue(struct iked *env, struct iked_msgqueue *queue)
{
	struct iked_msg_retransmit	*mr = NULL;

	while ((mr = TAILQ_FIRST(queue)) != NULL)
		ikev2_msg_dispose(env, queue, mr);
}

struct iked_msg_retransmit *
ikev2_msg_lookup(struct iked *env, struct iked_msgqueue *queue,
    struct iked_message *msg, uint8_t exchange)
{
	struct iked_msg_retransmit	*mr = NULL;

	TAILQ_FOREACH(mr, queue, mrt_entry) {
		if (TAILQ_FIRST(&mr->mrt_frags)->msg_msgid ==
		    msg->msg_msgid &&
		    TAILQ_FIRST(&mr->mrt_frags)->msg_exchange == exchange)
			break;
	}

	return (mr);
}

int
ikev2_msg_retransmit_response(struct iked *env, struct iked_sa *sa,
    struct iked_message *msg, struct ike_header *hdr)
{
	struct iked_msg_retransmit	*mr = NULL;
	struct iked_message		*m = NULL;

	if ((mr = ikev2_msg_lookup(env, &sa->sa_responses, msg,
	    hdr->ike_exchange)) == NULL)
		return (-2);	/* not found */

	if (hdr->ike_nextpayload == IKEV2_PAYLOAD_SKF) {
		/* only retransmit for fragment number one */
		if (ikev2_pld_parse_quick(env, hdr, msg,
		    msg->msg_offset) != 0 || msg->msg_frag_num != 1) {
			log_debug("%s: ignoring fragment", SPI_SA(sa, __func__));
			return (0);
		}
		log_debug("%s: first fragment", SPI_SA(sa, __func__));
	}

	TAILQ_FOREACH(m, &mr->mrt_frags, msg_entry) {
		if (sendtofrom(m->msg_fd, ibuf_data(m->msg_data),
		    ibuf_size(m->msg_data), 0,
		    (struct sockaddr *)&m->msg_peer, m->msg_peerlen,
		    (struct sockaddr *)&m->msg_local, m->msg_locallen) == -1) {
			log_warn("%s: sendtofrom", __func__);
			ikestat_inc(env, ikes_msg_send_failures);
			return (-1);
		}
		log_info("%sretransmit %s res %u local %s peer %s",
		    SPI_SA(sa, NULL),
		    print_map(hdr->ike_exchange, ikev2_exchange_map),
		    m->msg_msgid,
		    print_addr(&m->msg_local),
		    print_addr(&m->msg_peer));
	}

	timer_add(env, &mr->mrt_timer, IKED_RESPONSE_TIMEOUT);
	ikestat_inc(env, ikes_retransmit_response);
	return (0);
}

void
ikev2_msg_response_timeout(struct iked *env, void *arg)
{
	struct iked_msg_retransmit	*mr = arg;
	struct iked_sa		*sa;

	sa = TAILQ_FIRST(&mr->mrt_frags)->msg_sa;
	ikev2_msg_dispose(env, &sa->sa_responses, mr);
}

void
ikev2_msg_retransmit_timeout(struct iked *env, void *arg)
{
	struct iked_msg_retransmit *mr = arg;
	struct iked_message	*msg = TAILQ_FIRST(&mr->mrt_frags);
	struct iked_sa		*sa = msg->msg_sa;

	if (mr->mrt_tries < IKED_RETRANSMIT_TRIES) {
		TAILQ_FOREACH(msg, &mr->mrt_frags, msg_entry) {
			if (sendtofrom(msg->msg_fd, ibuf_data(msg->msg_data),
			    ibuf_size(msg->msg_data), 0,
			    (struct sockaddr *)&msg->msg_peer, msg->msg_peerlen,
			    (struct sockaddr *)&msg->msg_local,
			    msg->msg_locallen) == -1) {
				log_warn("%s: sendtofrom", __func__);
				ikev2_ike_sa_setreason(sa, "retransmit failed");
				sa_free(env, sa);
				ikestat_inc(env, ikes_msg_send_failures);
				return;
			}
			log_info("%sretransmit %d %s req %u peer %s "
			    "local %s", SPI_SA(sa, NULL), mr->mrt_tries + 1,
			    print_map(msg->msg_exchange, ikev2_exchange_map),
			    msg->msg_msgid,
			    print_addr(&msg->msg_peer),
			    print_addr(&msg->msg_local));
		}
		/* Exponential timeout */
		timer_add(env, &mr->mrt_timer,
		    IKED_RETRANSMIT_TIMEOUT * (2 << (mr->mrt_tries++)));
		ikestat_inc(env, ikes_retransmit_request);
	} else {
		log_debug("%s: retransmit limit reached for req %u",
		    __func__, msg->msg_msgid);
		ikev2_ike_sa_setreason(sa, "retransmit limit reached");
		ikestat_inc(env, ikes_retransmit_limit);
		sa_free(env, sa);
	}
}
