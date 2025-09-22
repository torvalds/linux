/* $OpenBSD: ike_auth.c,v 1.118 2020/07/07 17:33:40 tobhe Exp $	 */
/* $EOM: ike_auth.c,v 1.59 2000/11/21 00:21:31 angelos Exp $	 */

/*
 * Copyright (c) 1998, 1999, 2000, 2001 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 1999 Niels Provos.  All rights reserved.
 * Copyright (c) 1999 Angelos D. Keromytis.  All rights reserved.
 * Copyright (c) 2000, 2001, 2003 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <keynote.h>
#include <policy.h>

#include "cert.h"
#include "conf.h"
#include "constants.h"
#if defined (USE_DNSSEC)
#include "dnssec.h"
#endif
#include "exchange.h"
#include "hash.h"
#include "ike_auth.h"
#include "ipsec.h"
#include "ipsec_doi.h"
#include "libcrypto.h"
#include "log.h"
#include "message.h"
#include "monitor.h"
#include "prf.h"
#include "transport.h"
#include "util.h"
#include "key.h"
#include "x509.h"

#ifdef notyet
static u_int8_t *enc_gen_skeyid(struct exchange *, size_t *);
#endif
static u_int8_t *pre_shared_gen_skeyid(struct exchange *, size_t *);

static int      pre_shared_decode_hash(struct message *);
static int      pre_shared_encode_hash(struct message *);

static u_int8_t *sig_gen_skeyid(struct exchange *, size_t *);
static int      rsa_sig_decode_hash(struct message *);
static int      rsa_sig_encode_hash(struct message *);

static int      get_raw_key_from_file(int, u_int8_t *, size_t, RSA **);

static int      ike_auth_hash(struct exchange *, u_int8_t *);

static struct ike_auth ike_auth[] = {
	{
		IKE_AUTH_PRE_SHARED, pre_shared_gen_skeyid,
		pre_shared_decode_hash,
		pre_shared_encode_hash
	},
#ifdef notdef
	{
		IKE_AUTH_DSS, sig_gen_skeyid,
		pre_shared_decode_hash,
		pre_shared_encode_hash
	},
#endif
	{
		IKE_AUTH_RSA_SIG, sig_gen_skeyid,
		rsa_sig_decode_hash,
		rsa_sig_encode_hash
	},
#ifdef notdef
	{
		IKE_AUTH_RSA_ENC, enc_gen_skeyid,
		pre_shared_decode_hash,
		pre_shared_encode_hash
	},
	{
		IKE_AUTH_RSA_ENC_REV, enc_gen_skeyid,
		pre_shared_decode_hash,
		pre_shared_encode_hash
	},
#endif
};

struct ike_auth *
ike_auth_get(u_int16_t id)
{
	size_t	i;

	for (i = 0; i < sizeof ike_auth / sizeof ike_auth[0]; i++)
		if (id == ike_auth[i].id)
			return &ike_auth[i];
	return 0;
}

/*
 * Find and decode the configured key (pre-shared or public) for the
 * peer denoted by ID.  Stash the len in KEYLEN.
 */
static void *
ike_auth_get_key(int type, char *id, char *local_id, size_t *keylen)
{
	char	*key, *buf;
	char	*keyfile, *privkeyfile;
	FILE	*keyfp;
	RSA	*rsakey;
	size_t	 fsize, pkflen;
	int	 fd;

	switch (type) {
	case IKE_AUTH_PRE_SHARED:
		/* Get the pre-shared key for our peer.  */
		key = conf_get_str(id, "Authentication");
		if (!key && local_id)
			key = conf_get_str(local_id, "Authentication");

		if (!key) {
			log_print("ike_auth_get_key: "
			    "no key found for peer \"%s\" or local ID \"%s\"",
			    id, local_id ? local_id : "<none>");
			return 0;
		}
		/* If the key starts with 0x it is in hex format.  */
		if (strncasecmp(key, "0x", 2) == 0) {
			*keylen = (strlen(key) - 1) / 2;
			buf = malloc(*keylen);
			if (!buf) {
				log_error("ike_auth_get_key: malloc (%lu) "
				    "failed", (unsigned long)*keylen);
				return 0;
			}
			if (hex2raw(key + 2, (unsigned char *)buf, *keylen)) {
				free(buf);
				log_print("ike_auth_get_key: invalid hex key "
				    "%s", key);
				return 0;
			}
			key = buf;
		} else {
			buf = key;
			key = strdup(buf);
			if (!key) {
				log_error("ike_auth_get_key: strdup() failed");
				return 0;
			}
			*keylen = strlen(key);
		}
		break;

	case IKE_AUTH_RSA_SIG:
		if (local_id && (keyfile = conf_get_str("KeyNote",
		    "Credential-directory")) != 0) {
			struct stat     sb;
			struct keynote_deckey dc;
			char           *privkeyfile, *buf2;
			size_t          size;

			if (asprintf(&privkeyfile, "%s/%s/%s", keyfile,
			    local_id, PRIVATE_KEY_FILE) == -1) {
				log_print("ike_auth_get_key: failed to asprintf()");
				return 0;
			}
			keyfile = privkeyfile;

			fd = monitor_open(keyfile, O_RDONLY, 0);
			if (fd < 0) {
				free(keyfile);
				goto ignorekeynote;
			}

			if (fstat(fd, &sb) == -1) {
				log_print("ike_auth_get_key: fstat failed");
				free(keyfile);
				close(fd);
				return 0;
			}
			size = (size_t)sb.st_size;

			buf = calloc(size + 1, sizeof(char));
			if (!buf) {
				log_print("ike_auth_get_key: failed allocating"
				    " %lu bytes", (unsigned long)size + 1);
				free(keyfile);
				close(fd);
				return 0;
			}
			if (read(fd, buf, size) != (ssize_t)size) {
				free(buf);
				log_print("ike_auth_get_key: "
				    "failed reading %lu bytes from \"%s\"",
				    (unsigned long)size, keyfile);
				free(keyfile);
				close(fd);
				return 0;
			}
			close(fd);

			/* Parse private key string */
			buf2 = kn_get_string(buf);
			free(buf);

			if (!buf2 || kn_decode_key(&dc, buf2,
			    KEYNOTE_PRIVATE_KEY) == -1) {
				free(buf2);
				log_print("ike_auth_get_key: failed decoding "
				    "key in \"%s\"", keyfile);
				free(keyfile);
				return 0;
			}
			free(buf2);

			if (dc.dec_algorithm != KEYNOTE_ALGORITHM_RSA) {
				log_print("ike_auth_get_key: wrong algorithm "
				    "type %d in \"%s\"", dc.dec_algorithm,
				    keyfile);
				free(keyfile);
				kn_free_key(&dc);
				return 0;
			}
			free(keyfile);
			return dc.dec_key;
		}
ignorekeynote:
		/* Otherwise, try X.509 */

		privkeyfile = keyfile = NULL;
		fd = -1;

		if (local_id) {
			/* Look in Private-key-directory. */
			keyfile = conf_get_str("X509-certificates",
			    "Private-key-directory");
			pkflen = strlen(keyfile) + strlen(local_id) + sizeof "/";
			privkeyfile = calloc(pkflen, sizeof(char));
			if (!privkeyfile) {
				log_print("ike_auth_get_key: failed to "
				    "allocate %lu bytes", (unsigned long)pkflen);
				return 0;
			}

			snprintf(privkeyfile, pkflen, "%s/%s", keyfile,
			    local_id);
			keyfile = privkeyfile;

			fd = monitor_open(keyfile, O_RDONLY, 0);
			if (fd == -1 && errno != ENOENT) {
				log_print("ike_auth_get_key: failed opening "
				    "\"%s\"", keyfile);
				free(privkeyfile);
				privkeyfile = NULL;
				keyfile = NULL;
			}
		}

		if (fd == -1) {
			/* No key found, try default key. */
			keyfile = conf_get_str("X509-certificates",
			    "Private-key");

			fd = monitor_open(keyfile, O_RDONLY, 0);
			if (fd == -1) {
				log_print("ike_auth_get_key: failed opening "
				    "\"%s\"", keyfile);
				return 0;
			}
		}

		if (check_file_secrecy_fd(fd, keyfile, &fsize)) {
			free(privkeyfile);
			close(fd);
			return 0;
		}

		if ((keyfp = fdopen(fd, "r")) == NULL) {
			log_print("ike_auth_get_key: fdopen failed");
			free(privkeyfile);
			close(fd);
			return 0;
		}
#if SSLEAY_VERSION_NUMBER >= 0x00904100L
		rsakey = PEM_read_RSAPrivateKey(keyfp, NULL, NULL, NULL);
#else
		rsakey = PEM_read_RSAPrivateKey(keyfp, NULL, NULL);
#endif
		fclose(keyfp);

		free(privkeyfile);

		if (!rsakey) {
			log_print("ike_auth_get_key: "
			    "PEM_read_bio_RSAPrivateKey failed");
			return 0;
		}
		return rsakey;

	default:
		log_print("ike_auth_get_key: unknown key type %d", type);
		return 0;
	}

	return key;
}

static u_int8_t *
pre_shared_gen_skeyid(struct exchange *exchange, size_t *sz)
{
	struct prf     *prf;
	struct ipsec_exch *ie = exchange->data;
	u_int8_t       *skeyid, *buf = 0;
	unsigned char  *key;
	size_t          keylen;

	/*
	 * If we're the responder and have the initiator's ID (which is the
	 * case in Aggressive mode), try to find the preshared key in the
	 * section of the initiator's Phase 1 ID.  This allows us to do
	 * mobile user support with preshared keys.
	 */
	if (!exchange->initiator && exchange->id_i) {
		switch (exchange->id_i[0]) {
		case IPSEC_ID_IPV4_ADDR:
		case IPSEC_ID_IPV6_ADDR:
			util_ntoa((char **) &buf,
			    exchange->id_i[0] == IPSEC_ID_IPV4_ADDR ? AF_INET :
			    AF_INET6, exchange->id_i + ISAKMP_ID_DATA_OFF -
			    ISAKMP_GEN_SZ);
			if (!buf)
				return 0;
			break;

		case IPSEC_ID_FQDN:
		case IPSEC_ID_USER_FQDN:
			buf = calloc(exchange->id_i_len - ISAKMP_ID_DATA_OFF +
			    ISAKMP_GEN_SZ + 1, sizeof(char));
			if (!buf) {
				log_print("pre_shared_gen_skeyid: malloc (%lu"
				    ") failed",
				    (unsigned long)exchange->id_i_len -
				    ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ + 1);
				return 0;
			}
			memcpy(buf,
			    exchange->id_i + ISAKMP_ID_DATA_OFF -
			    ISAKMP_GEN_SZ,
			    exchange->id_i_len - ISAKMP_ID_DATA_OFF +
			    ISAKMP_GEN_SZ);
			break;

			/* XXX Support more ID types ? */
		default:
			break;
		}
	}
	/*
	 * Get the pre-shared key for our peer. This will work even if the key
	 * has been passed to us through a mechanism like PFKEYv2.
	 */
	key = ike_auth_get_key(IKE_AUTH_PRE_SHARED, exchange->name,
	    (char *)buf, &keylen);
	free(buf);

	/* Fail if no key could be found.  */
	if (!key)
		return 0;

	/* Store the secret key for later policy processing.  */
	exchange->recv_key = calloc(keylen + 1, sizeof(char));
	exchange->recv_keytype = ISAKMP_KEY_PASSPHRASE;
	if (!exchange->recv_key) {
		log_error("pre_shared_gen_skeyid: malloc (%lu) failed",
		    (unsigned long)keylen);
		free(key);
		return 0;
	}
	memcpy(exchange->recv_key, key, keylen);
	exchange->recv_certtype = ISAKMP_CERTENC_NONE;
	free(key);

	prf = prf_alloc(ie->prf_type, ie->hash->type, exchange->recv_key,
	    keylen);
	if (!prf)
		return 0;

	*sz = prf->blocksize;
	skeyid = malloc(*sz);
	if (!skeyid) {
		log_error("pre_shared_gen_skeyid: malloc (%lu) failed",
		    (unsigned long)*sz);
		prf_free(prf);
		return 0;
	}
	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, exchange->nonce_i, exchange->nonce_i_len);
	prf->Update(prf->prfctx, exchange->nonce_r, exchange->nonce_r_len);
	prf->Final(skeyid, prf->prfctx);
	prf_free(prf);
	return skeyid;
}

/* Both DSS & RSA signature authentication use this algorithm.  */
static u_int8_t *
sig_gen_skeyid(struct exchange *exchange, size_t *sz)
{
	struct prf     *prf;
	struct ipsec_exch *ie = exchange->data;
	u_int8_t       *skeyid;
	unsigned char  *key;

	key = malloc(exchange->nonce_i_len + exchange->nonce_r_len);
	if (!key)
		return 0;
	memcpy(key, exchange->nonce_i, exchange->nonce_i_len);
	memcpy(key + exchange->nonce_i_len, exchange->nonce_r,
	    exchange->nonce_r_len);

	LOG_DBG((LOG_NEGOTIATION, 80, "sig_gen_skeyid: PRF type %d, hash %d",
	    ie->prf_type, ie->hash->type));
	LOG_DBG_BUF((LOG_NEGOTIATION, 80,
	    "sig_gen_skeyid: SKEYID initialized with",
	    (u_int8_t *)key, exchange->nonce_i_len + exchange->nonce_r_len));

	prf = prf_alloc(ie->prf_type, ie->hash->type, key,
	    exchange->nonce_i_len + exchange->nonce_r_len);
	free(key);
	if (!prf)
		return 0;

	*sz = prf->blocksize;
	skeyid = malloc(*sz);
	if (!skeyid) {
		log_error("sig_gen_skeyid: malloc (%lu) failed",
		    (unsigned long)*sz);
		prf_free(prf);
		return 0;
	}
	LOG_DBG((LOG_NEGOTIATION, 80, "sig_gen_skeyid: g^xy length %lu",
	    (unsigned long)ie->g_xy_len));
	LOG_DBG_BUF((LOG_NEGOTIATION, 80,
	    "sig_gen_skeyid: SKEYID fed with g^xy", ie->g_xy, ie->g_xy_len));

	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, ie->g_xy, ie->g_xy_len);
	prf->Final(skeyid, prf->prfctx);
	prf_free(prf);
	return skeyid;
}

#ifdef notdef
/*
 * Both standard and revised RSA encryption authentication use this SKEYID
 * computation.
 */
static u_int8_t *
enc_gen_skeyid(struct exchange *exchange, size_t *sz)
{
	struct prf     *prf;
	struct ipsec_exch *ie = exchange->data;
	struct hash    *hash = ie->hash;
	u_int8_t       *skeyid;

	hash->Init(hash->ctx);
	hash->Update(hash->ctx, exchange->nonce_i, exchange->nonce_i_len);
	hash->Update(hash->ctx, exchange->nonce_r, exchange->nonce_r_len);
	hash->Final(hash->digest, hash->ctx);
	prf = prf_alloc(ie->prf_type, hash->type, hash->digest, *sz);
	if (!prf)
		return 0;

	*sz = prf->blocksize;
	skeyid = malloc(*sz);
	if (!skeyid) {
		log_error("enc_gen_skeyid: malloc (%d) failed", *sz);
		prf_free(prf);
		return 0;
	}
	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, exchange->cookies, ISAKMP_HDR_COOKIES_LEN);
	prf->Final(skeyid, prf->prfctx);
	prf_free(prf);
	return skeyid;
}
#endif				/* notdef */

static int
pre_shared_decode_hash(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct payload *payload;
	size_t          hashsize = ie->hash->hashsize;
	char            header[80];
	int             initiator = exchange->initiator;
	u_int8_t      **hash_p;

	/* Choose the right fields to fill-in.  */
	hash_p = initiator ? &ie->hash_r : &ie->hash_i;

	payload = payload_first(msg, ISAKMP_PAYLOAD_HASH);
	if (!payload) {
		log_print("pre_shared_decode_hash: no HASH payload found");
		return -1;
	}
	/* Check that the hash is of the correct size.  */
	if (GET_ISAKMP_GEN_LENGTH(payload->p) - ISAKMP_GEN_SZ != hashsize)
		return -1;

	/* XXX Need this hash be in the SA?  */
	*hash_p = malloc(hashsize);
	if (!*hash_p) {
		log_error("pre_shared_decode_hash: malloc (%lu) failed",
		    (unsigned long)hashsize);
		return -1;
	}
	memcpy(*hash_p, payload->p + ISAKMP_HASH_DATA_OFF, hashsize);
	snprintf(header, sizeof header, "pre_shared_decode_hash: HASH_%c",
	    initiator ? 'R' : 'I');
	LOG_DBG_BUF((LOG_MISC, 80, header, *hash_p, hashsize));

	payload->flags |= PL_MARK;
	return 0;
}

/* Decrypt the HASH in SIG, we already need a parsed ID payload.  */
static int
rsa_sig_decode_hash(struct message *msg)
{
	struct cert_handler *handler;
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct payload *p;
	void           *cert = 0;
	u_int8_t       *rawcert = 0, **hash_p, **id_cert, *id;
	u_int32_t       rawcertlen, *id_cert_len;
	RSA            *key = 0;
	size_t          hashsize = ie->hash->hashsize, id_len;
	char            header[80];
	int             len, initiator = exchange->initiator;
	int             found = 0, n, i, id_found;
#if defined (USE_DNSSEC)
	u_int8_t       *rawkey = 0;
	u_int32_t       rawkeylen;
#endif

	/* Choose the right fields to fill-in.  */
	hash_p = initiator ? &ie->hash_r : &ie->hash_i;
	id = initiator ? exchange->id_r : exchange->id_i;
	id_len = initiator ? exchange->id_r_len : exchange->id_i_len;

	if (!id || id_len == 0) {
		log_print("rsa_sig_decode_hash: ID is missing");
		return -1;
	}
	/*
	 * XXX Assume we should use the same kind of certification as the
	 * remote...  moreover, just use the first CERT payload to decide what
	 * to use.
	 */
	p = payload_first(msg, ISAKMP_PAYLOAD_CERT);
	if (!p)
		handler = cert_get(ISAKMP_CERTENC_KEYNOTE);
	else
		handler = cert_get(GET_ISAKMP_CERT_ENCODING(p->p));
	if (!handler) {
		log_print("rsa_sig_decode_hash: cert_get (%d) failed",
		    p ? GET_ISAKMP_CERT_ENCODING(p->p) : -1);
		return -1;
	}
	/*
	 * We need the policy session initialized now, so we can add
	 * credentials etc.
	 */
	exchange->policy_id = kn_init();
	if (exchange->policy_id == -1) {
		log_print("rsa_sig_decode_hash: failed to initialize policy "
		    "session");
		return -1;
	}

	/* Obtain a certificate from our certificate storage.  */
	if (handler->cert_obtain(id, id_len, 0, &rawcert, &rawcertlen)) {
		if (handler->id == ISAKMP_CERTENC_X509_SIG) {
			cert = handler->cert_get(rawcert, rawcertlen);
			if (!cert)
				LOG_DBG((LOG_CRYPTO, 50, "rsa_sig_decode_hash:"
				    " certificate malformed"));
			else {
				if (!handler->cert_get_key(cert, &key)) {
					log_print("rsa_sig_decode_hash: "
					    "decoding certificate failed");
					handler->cert_free(cert);
				} else {
					found++;
					LOG_DBG((LOG_CRYPTO, 40,
					    "rsa_sig_decode_hash: using cert "
					    "of type %d", handler->id));
					exchange->recv_cert = cert;
					exchange->recv_certtype = handler->id;
					x509_generate_kn(exchange->policy_id,
					    cert);
				}
			}
		} else if (handler->id == ISAKMP_CERTENC_KEYNOTE)
			handler->cert_insert(exchange->policy_id, rawcert);
		free(rawcert);
	}
	/*
	 * Walk over potential CERT payloads in this message.
	 * XXX I believe this is the wrong spot for this.  CERTs can appear
	 * anytime.
	 */
	TAILQ_FOREACH(p, &msg->payload[ISAKMP_PAYLOAD_CERT], link) {
		p->flags |= PL_MARK;

		/*
		 * When we have found a key, just walk over the rest, marking
		 * them.
		 */
		if (found)
			continue;

		handler = cert_get(GET_ISAKMP_CERT_ENCODING(p->p));
		if (!handler) {
			LOG_DBG((LOG_MISC, 30, "rsa_sig_decode_hash: "
			    "no handler for %s CERT encoding",
			    constant_name(isakmp_certenc_cst,
			    GET_ISAKMP_CERT_ENCODING(p->p))));
			continue;
		}
		cert = handler->cert_get(p->p + ISAKMP_CERT_DATA_OFF,
		    GET_ISAKMP_GEN_LENGTH(p->p) - ISAKMP_CERT_DATA_OFF);
		if (!cert) {
			log_print("rsa_sig_decode_hash: "
			    "can not get data from CERT");
			continue;
		}
		if (!handler->cert_validate(cert)) {
			handler->cert_free(cert);
			log_print("rsa_sig_decode_hash: received CERT can't "
			    "be validated");
			continue;
		}
		if (GET_ISAKMP_CERT_ENCODING(p->p) ==
		    ISAKMP_CERTENC_X509_SIG) {
			if (!handler->cert_get_subjects(cert, &n, &id_cert,
			    &id_cert_len)) {
				handler->cert_free(cert);
				log_print("rsa_sig_decode_hash: can not get "
				    "subject from CERT");
				continue;
			}
			id_found = 0;
			for (i = 0; i < n; i++)
				if (id_cert_len[i] == id_len &&
				    id[0] == id_cert[i][0] &&
				    memcmp(id + 4, id_cert[i] + 4, id_len - 4)
				    == 0) {
					id_found++;
					break;
				}
			if (!id_found) {
				handler->cert_free(cert);
				log_print("rsa_sig_decode_hash: no CERT "
				    "subject match the ID");
				free(id_cert);
				continue;
			}
			cert_free_subjects(n, id_cert, id_cert_len);
		}
		if (!handler->cert_get_key(cert, &key)) {
			handler->cert_free(cert);
			log_print("rsa_sig_decode_hash: decoding payload CERT "
			    "failed");
			continue;
		}
		/* We validated the cert, cache it for later use.  */
		handler->cert_insert(exchange->policy_id, cert);

		exchange->recv_cert = cert;
		exchange->recv_certtype = GET_ISAKMP_CERT_ENCODING(p->p);

		if (exchange->recv_certtype == ISAKMP_CERTENC_KEYNOTE) {
			struct keynote_deckey dc;
			char           *pp;

			dc.dec_algorithm = KEYNOTE_ALGORITHM_RSA;
			dc.dec_key = key;

			pp = kn_encode_key(&dc, INTERNAL_ENC_PKCS1,
			    ENCODING_HEX, KEYNOTE_PUBLIC_KEY);
			if (pp == NULL) {
				kn_free_key(&dc);
				log_print("rsa_sig_decode_hash: failed to "
				    "ASCII-encode key");
				return -1;
			}
			if (asprintf(&exchange->keynote_key, "rsa-hex:%s",
			    pp) == -1) {
				free(pp);
				kn_free_key(&dc);
				log_print("rsa_sig_decode_hash: failed to asprintf()");
				return -1;
			}
			free(pp);
		}
		found++;
	}

#if defined (USE_DNSSEC)
	/*
	 * If no certificate provided a key, try to find a validated DNSSEC
	 * KEY.
	 */
	if (!found) {
		rawkey = dns_get_key(IKE_AUTH_RSA_SIG, msg, &rawkeylen);

		/* We need to convert 'void *rawkey' into 'RSA *key'.  */
		if (dns_RSA_dns_to_x509(rawkey, rawkeylen, &key) == 0)
			found++;
		else
			log_print("rsa_sig_decode_hash: KEY to RSA key "
			    "conversion failed");

		free(rawkey);
	}
#endif				/* USE_DNSSEC */

	/* If we still have not found a key, try to read it from a file. */
	if (!found)
		if (get_raw_key_from_file(IKE_AUTH_RSA_SIG, id, id_len, &key)
		    != -1)
			found++;

	if (!found) {
		log_print("rsa_sig_decode_hash: no public key found");
		return -1;
	}
	p = payload_first(msg, ISAKMP_PAYLOAD_SIG);
	if (!p) {
		log_print("rsa_sig_decode_hash: missing signature payload");
		RSA_free(key);
		return -1;
	}
	/* Check that the sig is of the correct size.  */
	len = GET_ISAKMP_GEN_LENGTH(p->p) - ISAKMP_SIG_SZ;
	if (len != RSA_size(key)) {
		RSA_free(key);
		log_print("rsa_sig_decode_hash: "
		    "SIG payload length does not match public key");
		return -1;
	}
	*hash_p = malloc(len);
	if (!*hash_p) {
		RSA_free(key);
		log_error("rsa_sig_decode_hash: malloc (%d) failed", len);
		return -1;
	}
	len = RSA_public_decrypt(len, p->p + ISAKMP_SIG_DATA_OFF, *hash_p, key,
	    RSA_PKCS1_PADDING);
	if (len == -1) {
		RSA_free(key);
		log_print("rsa_sig_decode_hash: RSA_public_decrypt () failed");
		return -1;
	}
	/* Store key for later use */
	exchange->recv_key = key;
	exchange->recv_keytype = ISAKMP_KEY_RSA;

	if (len != (int)hashsize) {
		free(*hash_p);
		*hash_p = 0;
		log_print("rsa_sig_decode_hash: len %lu != hashsize %lu",
		    (unsigned long)len, (unsigned long)hashsize);
		return -1;
	}
	snprintf(header, sizeof header, "rsa_sig_decode_hash: HASH_%c",
	    initiator ? 'R' : 'I');
	LOG_DBG_BUF((LOG_MISC, 80, header, *hash_p, hashsize));

	p->flags |= PL_MARK;
	return 0;
}

static int
pre_shared_encode_hash(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	size_t          hashsize = ie->hash->hashsize;
	char            header[80];
	int             initiator = exchange->initiator;
	u_int8_t       *buf;

	buf = ipsec_add_hash_payload(msg, hashsize);
	if (!buf)
		return -1;

	if (ike_auth_hash(exchange, buf + ISAKMP_HASH_DATA_OFF) == -1)
		return -1;

	snprintf(header, sizeof header, "pre_shared_encode_hash: HASH_%c",
	    initiator ? 'I' : 'R');
	LOG_DBG_BUF((LOG_MISC, 80, header, buf + ISAKMP_HASH_DATA_OFF,
	    hashsize));
	return 0;
}

/* Encrypt the HASH into a SIG type.  */
static int
rsa_sig_encode_hash(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	size_t          hashsize = ie->hash->hashsize, id_len;
	struct cert_handler *handler;
	char            header[80];
	int             initiator = exchange->initiator, idtype;
	u_int8_t       *buf, *data, *buf2, *id;
	u_int32_t       datalen;
	int32_t         sigsize;
	void           *sent_key;

	id = initiator ? exchange->id_i : exchange->id_r;
	id_len = initiator ? exchange->id_i_len : exchange->id_r_len;

	/* We may have been provided these by the kernel */
	buf = (u_int8_t *)conf_get_str(exchange->name, "Credentials");
	if (buf && (idtype = conf_get_num(exchange->name, "Credential_Type",
	    -1)) != -1) {
		exchange->sent_certtype = idtype;
		handler = cert_get(idtype);
		if (!handler) {
			log_print("rsa_sig_encode_hash: cert_get (%d) failed",
			    idtype);
			return -1;
		}
		exchange->sent_cert =
		    handler->cert_from_printable((char *)buf);
		if (!exchange->sent_cert) {
			log_print("rsa_sig_encode_hash: failed to retrieve "
			    "certificate");
			return -1;
		}
		handler->cert_serialize(exchange->sent_cert, &data, &datalen);
		if (!data) {
			log_print("rsa_sig_encode_hash: cert serialization "
			    "failed");
			return -1;
		}
		goto aftercert;	/* Skip all the certificate discovery */
	}
	/* XXX This needs to be configurable.  */
	idtype = ISAKMP_CERTENC_KEYNOTE;

	/* Find a certificate with subjectAltName = id.  */
	handler = cert_get(idtype);
	if (!handler) {
		idtype = ISAKMP_CERTENC_X509_SIG;
		handler = cert_get(idtype);
		if (!handler) {
			log_print("rsa_sig_encode_hash: cert_get(%d) failed",
			    idtype);
			return -1;
		}
	}
	if (handler->cert_obtain(id, id_len, 0, &data, &datalen) == 0) {
		if (idtype == ISAKMP_CERTENC_KEYNOTE) {
			idtype = ISAKMP_CERTENC_X509_SIG;
			handler = cert_get(idtype);
			if (!handler) {
				log_print("rsa_sig_encode_hash: cert_get(%d) "
				    "failed", idtype);
				return -1;
			}
			if (handler->cert_obtain(id, id_len, 0, &data,
			    &datalen) == 0) {
				LOG_DBG((LOG_MISC, 10, "rsa_sig_encode_hash: "
				    "no certificate to send for id %s",
				    ipsec_id_string(id, id_len)));
				goto skipcert;
			}
		} else {
			LOG_DBG((LOG_MISC, 10,
			    "rsa_sig_encode_hash: no certificate to send"
			    " for id %s", ipsec_id_string(id, id_len)));
			goto skipcert;
		}
	}
	/* Let's store the certificate we are going to use */
	exchange->sent_certtype = idtype;
	exchange->sent_cert = handler->cert_get(data, datalen);
	if (!exchange->sent_cert) {
		free(data);
		log_print("rsa_sig_encode_hash: failed to get certificate "
		    "from wire encoding");
		return -1;
	}
aftercert:

	buf = realloc(data, ISAKMP_CERT_SZ + datalen);
	if (!buf) {
		log_error("rsa_sig_encode_hash: realloc (%p, %d) failed", data,
		    ISAKMP_CERT_SZ + datalen);
		free(data);
		return -1;
	}
	memmove(buf + ISAKMP_CERT_SZ, buf, datalen);
	SET_ISAKMP_CERT_ENCODING(buf, idtype);
	if (message_add_payload(msg, ISAKMP_PAYLOAD_CERT, buf,
	    ISAKMP_CERT_SZ + datalen, 1)) {
		free(buf);
		return -1;
	}
skipcert:

	/* Again, we may have these from the kernel */
	buf = (u_int8_t *)conf_get_str(exchange->name, "PKAuthentication");
	if (buf) {
		key_from_printable(ISAKMP_KEY_RSA, ISAKMP_KEYTYPE_PRIVATE,
		    (char *)buf, &data, &datalen);
		if (!data) {
			log_print("rsa_sig_encode_hash: badly formatted RSA "
			    "private key");
			return 0;
		}
		sent_key = key_internalize(ISAKMP_KEY_RSA,
		    ISAKMP_KEYTYPE_PRIVATE, data, datalen);
		if (!sent_key) {
			log_print("rsa_sig_encode_hash: bad RSA private key "
			    "from dynamic SA acquisition subsystem");
			return 0;
		}
	} else {
		/* Try through the regular means.  */
		switch (id[ISAKMP_ID_TYPE_OFF - ISAKMP_GEN_SZ]) {
		case IPSEC_ID_IPV4_ADDR:
		case IPSEC_ID_IPV6_ADDR:
			util_ntoa((char **)&buf2,
			    id[ISAKMP_ID_TYPE_OFF - ISAKMP_GEN_SZ] ==
			    IPSEC_ID_IPV4_ADDR ? AF_INET : AF_INET6,
			    id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ);
			if (!buf2)
				return 0;
			break;

		case IPSEC_ID_FQDN:
		case IPSEC_ID_USER_FQDN:
			buf2 = calloc(id_len - ISAKMP_ID_DATA_OFF +
			    ISAKMP_GEN_SZ + 1, sizeof(char));
			if (!buf2) {
				log_print("rsa_sig_encode_hash: malloc (%lu) "
				    "failed", (unsigned long)id_len -
				    ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ + 1);
				return 0;
			}
			memcpy(buf2, id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ,
			    id_len - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ);
			break;

			/* XXX Support more ID types?  */
		default:
			buf2 = 0;
			return 0;
		}

		sent_key = ike_auth_get_key(IKE_AUTH_RSA_SIG, exchange->name,
		    (char *)buf2, 0);
		free(buf2);

		/* Did we find a key?  */
		if (!sent_key) {
			log_print("rsa_sig_encode_hash: "
			    "could not get private key");
			return -1;
		}
	}

	/* Enable RSA blinding.  */
	if (RSA_blinding_on(sent_key, NULL) != 1) {
		log_error("rsa_sig_encode_hash: RSA_blinding_on () failed.");
		RSA_free(sent_key);
		return -1;
	}
	/* XXX hashsize is not necessarily prf->blocksize.  */
	buf = malloc(hashsize);
	if (!buf) {
		log_error("rsa_sig_encode_hash: malloc (%lu) failed",
		    (unsigned long)hashsize);
		RSA_free(sent_key);
		return -1;
	}
	if (ike_auth_hash(exchange, buf) == -1) {
		free(buf);
		RSA_free(sent_key);
		return -1;
	}
	snprintf(header, sizeof header, "rsa_sig_encode_hash: HASH_%c",
	    initiator ? 'I' : 'R');
	LOG_DBG_BUF((LOG_MISC, 80, header, buf, hashsize));

	data = malloc(RSA_size(sent_key));
	if (!data) {
		log_error("rsa_sig_encode_hash: malloc (%d) failed",
		    RSA_size(sent_key));
		free(buf);
		RSA_free(sent_key);
		return -1;
	}
	sigsize = RSA_private_encrypt(hashsize, buf, data, sent_key,
	    RSA_PKCS1_PADDING);
	if (sigsize == -1) {
		log_print("rsa_sig_encode_hash: "
		    "RSA_private_encrypt () failed");
		free(data);
		free(buf);
		RSA_free(sent_key);
		return -1;
	}
	datalen = (u_int32_t) sigsize;

	free(buf);
	RSA_free(sent_key);

	buf = realloc(data, ISAKMP_SIG_SZ + datalen);
	if (!buf) {
		log_error("rsa_sig_encode_hash: realloc (%p, %d) failed", data,
		    ISAKMP_SIG_SZ + datalen);
		free(data);
		return -1;
	}
	memmove(buf + ISAKMP_SIG_SZ, buf, datalen);

	snprintf(header, sizeof header, "rsa_sig_encode_hash: SIG_%c",
	    initiator ? 'I' : 'R');
	LOG_DBG_BUF((LOG_MISC, 80, header, buf + ISAKMP_SIG_DATA_OFF,
	    datalen));
	if (message_add_payload(msg, ISAKMP_PAYLOAD_SIG, buf,
	    ISAKMP_SIG_SZ + datalen, 1)) {
		free(buf);
		return -1;
	}
	return 0;
}

int
ike_auth_hash(struct exchange *exchange, u_int8_t *buf)
{
	struct ipsec_exch *ie = exchange->data;
	struct prf     *prf;
	struct hash    *hash = ie->hash;
	int             initiator = exchange->initiator;
	u_int8_t       *id;
	size_t          id_len;

	/* Choose the right fields to fill-in.  */
	id = initiator ? exchange->id_i : exchange->id_r;
	id_len = initiator ? exchange->id_i_len : exchange->id_r_len;

	/* Allocate the prf and start calculating our HASH.  */
	prf = prf_alloc(ie->prf_type, hash->type, ie->skeyid, ie->skeyid_len);
	if (!prf)
		return -1;

	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, initiator ? ie->g_xi : ie->g_xr, ie->g_x_len);
	prf->Update(prf->prfctx, initiator ? ie->g_xr : ie->g_xi, ie->g_x_len);
	prf->Update(prf->prfctx, exchange->cookies +
	    (initiator ? ISAKMP_HDR_ICOOKIE_OFF : ISAKMP_HDR_RCOOKIE_OFF),
	    ISAKMP_HDR_ICOOKIE_LEN);
	prf->Update(prf->prfctx, exchange->cookies +
	    (initiator ? ISAKMP_HDR_RCOOKIE_OFF : ISAKMP_HDR_ICOOKIE_OFF),
	    ISAKMP_HDR_ICOOKIE_LEN);
	prf->Update(prf->prfctx, ie->sa_i_b, ie->sa_i_b_len);
	prf->Update(prf->prfctx, id, id_len);
	prf->Final(buf, prf->prfctx);
	prf_free(prf);
	return 0;
}

static int
get_raw_key_from_file(int type, u_int8_t *id, size_t id_len, RSA **rsa)
{
	char            filename[FILENAME_MAX];
	char           *fstr;
	FILE           *keyfp;

	if (type != IKE_AUTH_RSA_SIG) {	/* XXX More types? */
		LOG_DBG((LOG_NEGOTIATION, 20, "get_raw_key_from_file: "
		    "invalid auth type %d\n", type));
		return -1;
	}
	*rsa = 0;

	fstr = conf_get_str("General", "Pubkey-directory");
	if (!fstr)
		fstr = CONF_DFLT_PUBKEY_DIR;

	if (snprintf(filename, sizeof filename, "%s/", fstr) >
	    (int)sizeof filename - 1)
		return -1;

	fstr = ipsec_id_string(id, id_len);
	if (!fstr) {
		LOG_DBG((LOG_NEGOTIATION, 50, "get_raw_key_from_file: "
		    "ipsec_id_string failed"));
		return -1;
	}
	strlcat(filename, fstr, sizeof filename - strlen(filename));
	free(fstr);

	/* If the file does not exist, fail silently.  */
	keyfp = monitor_fopen(filename, "r");
	if (keyfp) {
		*rsa = PEM_read_RSA_PUBKEY(keyfp, NULL, NULL, NULL);
		if (!*rsa) {
			rewind(keyfp);
			*rsa = PEM_read_RSAPublicKey(keyfp, NULL, NULL, NULL);
		}
		if (!*rsa)
			log_print("get_raw_key_from_file: failed to get "
			    "public key %s", filename);
		fclose(keyfp);
	} else if (errno != ENOENT) {
		log_error("get_raw_key_from_file: monitor_fopen "
		    "(\"%s\", \"r\") failed", filename);
		return -1;
	} else
		LOG_DBG((LOG_NEGOTIATION, 50,
		    "get_raw_key_from_file: file %s not found", filename));

	return (*rsa ? 0 : -1);
}
