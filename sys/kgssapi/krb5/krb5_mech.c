/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <kgssapi/gssapi.h>
#include <kgssapi/gssapi_impl.h>

#include "kgss_if.h"
#include "kcrypto.h"

#define GSS_TOKEN_SENT_BY_ACCEPTOR	1
#define GSS_TOKEN_SEALED		2
#define GSS_TOKEN_ACCEPTOR_SUBKEY	4

static gss_OID_desc krb5_mech_oid =
{9, (void *) "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02" };

struct krb5_data {
	size_t		kd_length;
	void		*kd_data;
};

struct krb5_keyblock {
	uint16_t	kk_type; /* encryption type */
	struct krb5_data kk_key; /* key data */
};

struct krb5_address {
	uint16_t	ka_type;
	struct krb5_data ka_addr;
};

/*
 * The km_elem array is ordered so that the highest received sequence
 * number is listed first.
 */
struct krb5_msg_order {
	uint32_t		km_flags;
	uint32_t		km_start;
	uint32_t		km_length;
	uint32_t		km_jitter_window;
	uint32_t		km_first_seq;
	uint32_t		*km_elem;
};

struct krb5_context {
	struct _gss_ctx_id_t	kc_common;
	struct mtx		kc_lock;
	uint32_t		kc_ac_flags;
	uint32_t		kc_ctx_flags;
	uint32_t		kc_more_flags;
#define LOCAL			1
#define OPEN			2
#define COMPAT_OLD_DES3		4
#define COMPAT_OLD_DES3_SELECTED 8
#define ACCEPTOR_SUBKEY		16
	struct krb5_address	kc_local_address;
	struct krb5_address	kc_remote_address;
	uint16_t		kc_local_port;
	uint16_t		kc_remote_port;
	struct krb5_keyblock	kc_keyblock;
	struct krb5_keyblock	kc_local_subkey;
	struct krb5_keyblock	kc_remote_subkey;
	volatile uint32_t	kc_local_seqnumber;
	uint32_t		kc_remote_seqnumber;
	uint32_t		kc_keytype;
	uint32_t		kc_cksumtype;
	struct krb5_data	kc_source_name;
	struct krb5_data	kc_target_name;
	uint32_t		kc_lifetime;
	struct krb5_msg_order	kc_msg_order;
	struct krb5_key_state	*kc_tokenkey;
	struct krb5_key_state	*kc_encryptkey;
	struct krb5_key_state	*kc_checksumkey;

	struct krb5_key_state	*kc_send_seal_Ke;
	struct krb5_key_state	*kc_send_seal_Ki;
	struct krb5_key_state	*kc_send_seal_Kc;
	struct krb5_key_state	*kc_send_sign_Kc;

	struct krb5_key_state	*kc_recv_seal_Ke;
	struct krb5_key_state	*kc_recv_seal_Ki;
	struct krb5_key_state	*kc_recv_seal_Kc;
	struct krb5_key_state	*kc_recv_sign_Kc;
};

static uint16_t
get_uint16(const uint8_t **pp, size_t *lenp)
{
	const uint8_t *p = *pp;
	uint16_t v;

	if (*lenp < 2)
		return (0);

	v = (p[0] << 8) | p[1];
	*pp = p + 2;
	*lenp = *lenp - 2;

	return (v);
}

static uint32_t
get_uint32(const uint8_t **pp, size_t *lenp)
{
	const uint8_t *p = *pp;
	uint32_t v;

	if (*lenp < 4)
		return (0);

	v = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	*pp = p + 4;
	*lenp = *lenp - 4;

	return (v);
}

static void
get_data(const uint8_t **pp, size_t *lenp, struct krb5_data *dp)
{
	size_t sz = get_uint32(pp, lenp);

	dp->kd_length = sz;
	dp->kd_data = malloc(sz, M_GSSAPI, M_WAITOK);

	if (*lenp < sz)
		sz = *lenp;
	bcopy(*pp, dp->kd_data, sz);
	(*pp) += sz;
	(*lenp) -= sz;
}

static void
delete_data(struct krb5_data *dp)
{
	if (dp->kd_data) {
		free(dp->kd_data, M_GSSAPI);
		dp->kd_length = 0;
		dp->kd_data = NULL;
	}
}

static void
get_address(const uint8_t **pp, size_t *lenp, struct krb5_address *ka)
{

	ka->ka_type = get_uint16(pp, lenp);
	get_data(pp, lenp, &ka->ka_addr);
}

static void
delete_address(struct krb5_address *ka)
{
	delete_data(&ka->ka_addr);
}

static void
get_keyblock(const uint8_t **pp, size_t *lenp, struct krb5_keyblock *kk)
{

	kk->kk_type = get_uint16(pp, lenp);
	get_data(pp, lenp, &kk->kk_key);
}

static void
delete_keyblock(struct krb5_keyblock *kk)
{
	if (kk->kk_key.kd_data)
		bzero(kk->kk_key.kd_data, kk->kk_key.kd_length);
	delete_data(&kk->kk_key);
}

static void
copy_key(struct krb5_keyblock *from, struct krb5_keyblock **to)
{

	if (from->kk_key.kd_length)
		*to = from;
	else
		*to = NULL;
}

/*
 * Return non-zero if we are initiator.
 */
static __inline int
is_initiator(struct krb5_context *kc)
{
	return (kc->kc_more_flags & LOCAL);
}

/*
 * Return non-zero if we are acceptor.
 */
static __inline int
is_acceptor(struct krb5_context *kc)
{
	return !(kc->kc_more_flags & LOCAL);
}

static void
get_initiator_subkey(struct krb5_context *kc, struct krb5_keyblock **kdp)
{

	if (is_initiator(kc))
		copy_key(&kc->kc_local_subkey, kdp);
	else
		copy_key(&kc->kc_remote_subkey, kdp);
	if (!*kdp)
		copy_key(&kc->kc_keyblock, kdp);
}

static void
get_acceptor_subkey(struct krb5_context *kc, struct krb5_keyblock **kdp)
{

	if (is_initiator(kc))
		copy_key(&kc->kc_remote_subkey, kdp);
	else
		copy_key(&kc->kc_local_subkey, kdp);
}

static OM_uint32
get_keys(struct krb5_context *kc)
{
	struct krb5_keyblock *keydata;
	struct krb5_encryption_class *ec;
	struct krb5_key_state *key;
	int etype;

	keydata = NULL;
	get_acceptor_subkey(kc, &keydata);
	if (!keydata)
		if ((kc->kc_more_flags & ACCEPTOR_SUBKEY) == 0)
			get_initiator_subkey(kc, &keydata);
	if (!keydata)
		return (GSS_S_FAILURE);

	/*
	 * GSS-API treats all DES etypes the same and all DES3 etypes
	 * the same.
	 */
	switch (keydata->kk_type) {
	case ETYPE_DES_CBC_CRC:
	case ETYPE_DES_CBC_MD4:
	case ETYPE_DES_CBC_MD5:
		etype = ETYPE_DES_CBC_CRC;
		break;

	case ETYPE_DES3_CBC_MD5:
	case ETYPE_DES3_CBC_SHA1:
	case ETYPE_OLD_DES3_CBC_SHA1:
		etype = ETYPE_DES3_CBC_SHA1;
		break;

	default:
		etype = keydata->kk_type;
	}

	ec = krb5_find_encryption_class(etype);
	if (!ec)
		return (GSS_S_FAILURE);

	key = krb5_create_key(ec);
	krb5_set_key(key, keydata->kk_key.kd_data);
	kc->kc_tokenkey = key;

	switch (etype) {
	case ETYPE_DES_CBC_CRC:
	case ETYPE_ARCFOUR_HMAC_MD5: 
	case ETYPE_ARCFOUR_HMAC_MD5_56: {
		/*
		 * Single DES and ARCFOUR uses a 'derived' key (XOR
		 * with 0xf0) for encrypting wrap tokens. The original
		 * key is used for checksums and sequence numbers.
		 */
		struct krb5_key_state *ekey;
		uint8_t *ekp, *kp;
		int i;

		ekey = krb5_create_key(ec);
		ekp = ekey->ks_key;
		kp = key->ks_key;
		for (i = 0; i < ec->ec_keylen; i++)
			ekp[i] = kp[i] ^ 0xf0;
		krb5_set_key(ekey, ekp);
		kc->kc_encryptkey = ekey;
		refcount_acquire(&key->ks_refs);
		kc->kc_checksumkey = key;
		break;
	}

	case ETYPE_DES3_CBC_SHA1:
		/*
		 * Triple DES uses a RFC 3961 style derived key with
		 * usage number KG_USAGE_SIGN for checksums. The
		 * original key is used for encryption and sequence
		 * numbers.
		 */
		kc->kc_checksumkey = krb5_get_checksum_key(key, KG_USAGE_SIGN);
		refcount_acquire(&key->ks_refs);
		kc->kc_encryptkey = key;
		break;

	default:
		/*
		 * We need eight derived keys four for sending and
		 * four for receiving.
		 */
		if (is_initiator(kc)) {
			/*
			 * We are initiator.
			 */
			kc->kc_send_seal_Ke = krb5_get_encryption_key(key,
			    KG_USAGE_INITIATOR_SEAL);
			kc->kc_send_seal_Ki = krb5_get_integrity_key(key,
			    KG_USAGE_INITIATOR_SEAL);
			kc->kc_send_seal_Kc = krb5_get_checksum_key(key,
			    KG_USAGE_INITIATOR_SEAL);
			kc->kc_send_sign_Kc = krb5_get_checksum_key(key,
			    KG_USAGE_INITIATOR_SIGN);

			kc->kc_recv_seal_Ke = krb5_get_encryption_key(key,
			    KG_USAGE_ACCEPTOR_SEAL);
			kc->kc_recv_seal_Ki = krb5_get_integrity_key(key,
			    KG_USAGE_ACCEPTOR_SEAL);
			kc->kc_recv_seal_Kc = krb5_get_checksum_key(key,
			    KG_USAGE_ACCEPTOR_SEAL);
			kc->kc_recv_sign_Kc = krb5_get_checksum_key(key,
			    KG_USAGE_ACCEPTOR_SIGN);
		} else {
			/*
			 * We are acceptor.
			 */
			kc->kc_send_seal_Ke = krb5_get_encryption_key(key,
			    KG_USAGE_ACCEPTOR_SEAL);
			kc->kc_send_seal_Ki = krb5_get_integrity_key(key,
			    KG_USAGE_ACCEPTOR_SEAL);
			kc->kc_send_seal_Kc = krb5_get_checksum_key(key,
			    KG_USAGE_ACCEPTOR_SEAL);
			kc->kc_send_sign_Kc = krb5_get_checksum_key(key,
			    KG_USAGE_ACCEPTOR_SIGN);

			kc->kc_recv_seal_Ke = krb5_get_encryption_key(key,
			    KG_USAGE_INITIATOR_SEAL);
			kc->kc_recv_seal_Ki = krb5_get_integrity_key(key,
			    KG_USAGE_INITIATOR_SEAL);
			kc->kc_recv_seal_Kc = krb5_get_checksum_key(key,
			    KG_USAGE_INITIATOR_SEAL);
			kc->kc_recv_sign_Kc = krb5_get_checksum_key(key,
			    KG_USAGE_INITIATOR_SIGN);
		}
		break;
	}

	return (GSS_S_COMPLETE);
}

static void
krb5_init(gss_ctx_id_t ctx)
{
	struct krb5_context *kc = (struct krb5_context *)ctx;

	mtx_init(&kc->kc_lock, "krb5 gss lock", NULL, MTX_DEF);
}

static OM_uint32
krb5_import(gss_ctx_id_t ctx,
    enum sec_context_format format,
    const gss_buffer_t context_token)
{
	struct krb5_context *kc = (struct krb5_context *)ctx;
	OM_uint32 res;
	const uint8_t *p = (const uint8_t *) context_token->value;
	size_t len = context_token->length;
	uint32_t flags;
	int i;
	
	/*
	 * We support heimdal 0.6 and heimdal 1.1
	 */
	if (format != KGSS_HEIMDAL_0_6 && format != KGSS_HEIMDAL_1_1)
		return (GSS_S_DEFECTIVE_TOKEN);

#define SC_LOCAL_ADDRESS	1
#define SC_REMOTE_ADDRESS	2
#define SC_KEYBLOCK		4
#define SC_LOCAL_SUBKEY		8
#define SC_REMOTE_SUBKEY	16

	/*
	 * Ensure that the token starts with krb5 oid.
	 */
	if (p[0] != 0x00 || p[1] != krb5_mech_oid.length
	    || len < krb5_mech_oid.length + 2
	    || bcmp(krb5_mech_oid.elements, p + 2,
		krb5_mech_oid.length))
		return (GSS_S_DEFECTIVE_TOKEN);
	p += krb5_mech_oid.length + 2;
	len -= krb5_mech_oid.length + 2;

	flags = get_uint32(&p, &len);
	kc->kc_ac_flags = get_uint32(&p, &len);
	if (flags & SC_LOCAL_ADDRESS)
		get_address(&p, &len, &kc->kc_local_address);
	if (flags & SC_REMOTE_ADDRESS)
		get_address(&p, &len, &kc->kc_remote_address);
	kc->kc_local_port = get_uint16(&p, &len);
	kc->kc_remote_port = get_uint16(&p, &len);
	if (flags & SC_KEYBLOCK)
		get_keyblock(&p, &len, &kc->kc_keyblock);
	if (flags & SC_LOCAL_SUBKEY)
		get_keyblock(&p, &len, &kc->kc_local_subkey);
	if (flags & SC_REMOTE_SUBKEY)
		get_keyblock(&p, &len, &kc->kc_remote_subkey);
	kc->kc_local_seqnumber = get_uint32(&p, &len);
	kc->kc_remote_seqnumber = get_uint32(&p, &len);
	kc->kc_keytype = get_uint32(&p, &len);
	kc->kc_cksumtype = get_uint32(&p, &len);
	get_data(&p, &len, &kc->kc_source_name);
	get_data(&p, &len, &kc->kc_target_name);
	kc->kc_ctx_flags = get_uint32(&p, &len);
	kc->kc_more_flags = get_uint32(&p, &len);
	kc->kc_lifetime = get_uint32(&p, &len);
	/*
	 * Heimdal 1.1 adds the message order stuff.
	 */
	if (format == KGSS_HEIMDAL_1_1) {
		kc->kc_msg_order.km_flags = get_uint32(&p, &len);
		kc->kc_msg_order.km_start = get_uint32(&p, &len);
		kc->kc_msg_order.km_length = get_uint32(&p, &len);
		kc->kc_msg_order.km_jitter_window = get_uint32(&p, &len);
		kc->kc_msg_order.km_first_seq = get_uint32(&p, &len);
		kc->kc_msg_order.km_elem =
			malloc(kc->kc_msg_order.km_jitter_window * sizeof(uint32_t),
			    M_GSSAPI, M_WAITOK);
		for (i = 0; i < kc->kc_msg_order.km_jitter_window; i++)
			kc->kc_msg_order.km_elem[i] = get_uint32(&p, &len);
	} else {
		kc->kc_msg_order.km_flags = 0;
	}

	res = get_keys(kc);
	if (GSS_ERROR(res))
		return (res);

	/*
	 * We don't need these anymore.
	 */
	delete_keyblock(&kc->kc_keyblock);
	delete_keyblock(&kc->kc_local_subkey);
	delete_keyblock(&kc->kc_remote_subkey);

	return (GSS_S_COMPLETE);
}

static void
krb5_delete(gss_ctx_id_t ctx, gss_buffer_t output_token)
{
	struct krb5_context *kc = (struct krb5_context *)ctx;

	delete_address(&kc->kc_local_address);
	delete_address(&kc->kc_remote_address);
	delete_keyblock(&kc->kc_keyblock);
	delete_keyblock(&kc->kc_local_subkey);
	delete_keyblock(&kc->kc_remote_subkey);
	delete_data(&kc->kc_source_name);
	delete_data(&kc->kc_target_name);
	if (kc->kc_msg_order.km_elem)
		free(kc->kc_msg_order.km_elem, M_GSSAPI);
	if (output_token) {
		output_token->length = 0;
		output_token->value = NULL;
	}
	if (kc->kc_tokenkey) {
		krb5_free_key(kc->kc_tokenkey);
		if (kc->kc_encryptkey) {
			krb5_free_key(kc->kc_encryptkey);
			krb5_free_key(kc->kc_checksumkey);
		} else {
			krb5_free_key(kc->kc_send_seal_Ke);
			krb5_free_key(kc->kc_send_seal_Ki);
			krb5_free_key(kc->kc_send_seal_Kc);
			krb5_free_key(kc->kc_send_sign_Kc);
			krb5_free_key(kc->kc_recv_seal_Ke);
			krb5_free_key(kc->kc_recv_seal_Ki);
			krb5_free_key(kc->kc_recv_seal_Kc);
			krb5_free_key(kc->kc_recv_sign_Kc);
		}
	}
	mtx_destroy(&kc->kc_lock);
}

static gss_OID
krb5_mech_type(gss_ctx_id_t ctx)
{

	return (&krb5_mech_oid);
}

/*
 * Make a token with the given type and length (the length includes
 * the TOK_ID), initialising the token header appropriately. Return a
 * pointer to the TOK_ID of the token.  A new mbuf is allocated with
 * the framing header plus hlen bytes of space.
 *
 * Format is as follows:
 *
 *	0x60			[APPLICATION 0] SEQUENCE
 *	DER encoded length	length of oid + type + inner token length
 *	0x06 NN <oid data>	OID of mechanism type
 *	TT TT			TOK_ID
 *	<inner token>		data for inner token
 *	
 * 1:		der encoded length
 */
static void *
krb5_make_token(char tok_id[2], size_t hlen, size_t len, struct mbuf **mp)
{
	size_t inside_len, len_len, tlen;
	gss_OID oid = &krb5_mech_oid;
	struct mbuf *m;
	uint8_t *p;

	inside_len = 2 + oid->length + len;
	if (inside_len < 128)
		len_len = 1;
	else if (inside_len < 0x100)
		len_len = 2;
	else if (inside_len < 0x10000)
		len_len = 3;
	else if (inside_len < 0x1000000)
		len_len = 4;
	else
		len_len = 5;

	tlen = 1 + len_len + 2 + oid->length + hlen;
	KASSERT(tlen <= MLEN, ("token head too large"));
	MGET(m, M_WAITOK, MT_DATA);
	M_ALIGN(m, tlen);
	m->m_len = tlen;

	p = (uint8_t *) m->m_data;
	*p++ = 0x60;
	switch (len_len) {
	case 1:
		*p++ = inside_len;
		break;
	case 2:
		*p++ = 0x81;
		*p++ = inside_len;
		break;
	case 3:
		*p++ = 0x82;
		*p++ = inside_len >> 8;
		*p++ = inside_len;
		break;
	case 4:
		*p++ = 0x83;
		*p++ = inside_len >> 16;
		*p++ = inside_len >> 8;
		*p++ = inside_len;
		break;
	case 5:
		*p++ = 0x84;
		*p++ = inside_len >> 24;
		*p++ = inside_len >> 16;
		*p++ = inside_len >> 8;
		*p++ = inside_len;
		break;
	}

	*p++ = 0x06;
	*p++ = oid->length;
	bcopy(oid->elements, p, oid->length);
	p += oid->length;

	p[0] = tok_id[0];
	p[1] = tok_id[1];

	*mp = m;

	return (p);
}

/*
 * Verify a token, checking the inner token length and mechanism oid.
 * pointer to the first byte of the TOK_ID. The length of the
 * encapsulated data is checked to be at least len bytes; the actual
 * length of the encapsulated data (including TOK_ID) is returned in
 * *encap_len.
 *
 * If can_pullup is TRUE and the token header is fragmented, we will
 * rearrange it.
 *
 * Format is as follows:
 *
 *	0x60			[APPLICATION 0] SEQUENCE
 *	DER encoded length	length of oid + type + inner token length
 *	0x06 NN <oid data>	OID of mechanism type
 *	TT TT			TOK_ID
 *	<inner token>		data for inner token
 *	
 * 1:		der encoded length
 */
static void *
krb5_verify_token(char tok_id[2], size_t len, struct mbuf **mp,
    size_t *encap_len, bool_t can_pullup)
{
	struct mbuf *m;
	size_t tlen, hlen, len_len, inside_len;
	gss_OID oid = &krb5_mech_oid;
	uint8_t *p;

	m = *mp;
	tlen = m_length(m, NULL);
	if (tlen < 2)
		return (NULL);

	/*
	 * Ensure that at least the framing part of the token is
	 * contigous.
	 */
	if (m->m_len < 2) {
		if (can_pullup)
			*mp = m = m_pullup(m, 2);
		else
			return (NULL);
	}

	p = m->m_data;

	if (*p++ != 0x60)
		return (NULL);

	if (*p < 0x80) {
		inside_len = *p++;
		len_len = 1;
	} else {
		/*
		 * Ensure there is enough space for the DER encoded length.
		 */
		len_len = (*p & 0x7f) + 1;
		if (tlen < len_len + 1)
			return (NULL);
		if (m->m_len < len_len + 1) {
			if (can_pullup)
				*mp = m = m_pullup(m, len_len + 1);
			else
				return (NULL);
			p = m->m_data + 1;
		}

		switch (*p++) {
		case 0x81:
			inside_len = *p++;
			break;

		case 0x82:
			inside_len = (p[0] << 8) | p[1];
			p += 2;
			break;

		case 0x83:
			inside_len = (p[0] << 16) | (p[1] << 8) | p[2];
			p += 3;
			break;

		case 0x84:
			inside_len = (p[0] << 24) | (p[1] << 16)
				| (p[2] << 8) | p[3];
			p += 4;
			break;

		default:
			return (NULL);
		}
	}

	if (tlen != inside_len + len_len + 1)
		return (NULL);
	if (inside_len < 2 + oid->length + len)
		return (NULL);

	/*
	 * Now that we know the value of len_len, we can pullup the
	 * whole header. The header is 1 + len_len + 2 + oid->length +
	 * len bytes.
	 */
	hlen = 1 + len_len + 2 + oid->length + len;
	if (m->m_len < hlen) {
		if (can_pullup)
			*mp = m = m_pullup(m, hlen);
		else
			return (NULL);
		p = m->m_data + 1 + len_len;
	}

	if (*p++ != 0x06)
		return (NULL);
	if (*p++ != oid->length)
		return (NULL);
	if (bcmp(oid->elements, p, oid->length))
		return (NULL);
	p += oid->length;

	if (p[0] != tok_id[0])
		return (NULL);

	if (p[1] != tok_id[1])
		return (NULL);

	*encap_len = inside_len - 2 - oid->length;

	return (p);
}

static void
krb5_insert_seq(struct krb5_msg_order *mo, uint32_t seq, int index)
{
	int i;

	if (mo->km_length < mo->km_jitter_window)
		mo->km_length++;

	for (i = mo->km_length - 1; i > index; i--)
		mo->km_elem[i] = mo->km_elem[i - 1];
	mo->km_elem[index] = seq;
}

/*
 * Check sequence numbers according to RFC 2743 section 1.2.3.
 */
static OM_uint32
krb5_sequence_check(struct krb5_context *kc, uint32_t seq)
{
	OM_uint32 res = GSS_S_FAILURE;
	struct krb5_msg_order *mo = &kc->kc_msg_order;
	int check_sequence = mo->km_flags & GSS_C_SEQUENCE_FLAG;
	int check_replay = mo->km_flags & GSS_C_REPLAY_FLAG;
	int i;

	mtx_lock(&kc->kc_lock);

	/*
	 * Message is in-sequence with no gap.
	 */
	if (mo->km_length == 0 || seq == mo->km_elem[0] + 1) {
		/*
		 * This message is received in-sequence with no gaps.
		 */
		krb5_insert_seq(mo, seq, 0);
		res = GSS_S_COMPLETE;
		goto out;
	}

	if (seq > mo->km_elem[0]) {
		/*
		 * This message is received in-sequence with a gap.
		 */
		krb5_insert_seq(mo, seq, 0);
		if (check_sequence)
			res = GSS_S_GAP_TOKEN;
		else
			res = GSS_S_COMPLETE;
		goto out;
	}

	if (seq < mo->km_elem[mo->km_length - 1]) {
		if (check_replay && !check_sequence)
			res = GSS_S_OLD_TOKEN;
		else
			res = GSS_S_UNSEQ_TOKEN;
		goto out;
	}

	for (i = 0; i < mo->km_length; i++) {
		if (mo->km_elem[i] == seq) {
			res = GSS_S_DUPLICATE_TOKEN;
			goto out;
		}
		if (mo->km_elem[i] < seq) {
			/*
			 * We need to insert this seq here,
			 */
			krb5_insert_seq(mo, seq, i);
			if (check_replay && !check_sequence)
				res = GSS_S_COMPLETE;
			else
				res = GSS_S_UNSEQ_TOKEN;
			goto out;
		}
	}

out:
	mtx_unlock(&kc->kc_lock);

	return (res);
}

static uint8_t sgn_alg_des_md5[] = { 0x00, 0x00 };
static uint8_t seal_alg_des[] = { 0x00, 0x00 };
static uint8_t sgn_alg_des3_sha1[] = { 0x04, 0x00 };
static uint8_t seal_alg_des3[] = { 0x02, 0x00 };
static uint8_t seal_alg_rc4[] = { 0x10, 0x00 };
static uint8_t sgn_alg_hmac_md5[] = { 0x11, 0x00 };

/*
 * Return the size of the inner token given the use of the key's
 * encryption class. For wrap tokens, the length of the padded
 * plaintext will be added to this.
 */
static size_t
token_length(struct krb5_key_state *key)
{

	return (16 + key->ks_class->ec_checksumlen);
}

static OM_uint32
krb5_get_mic_old(struct krb5_context *kc, struct mbuf *m,
    struct mbuf **micp, uint8_t sgn_alg[2])
{
	struct mbuf *mlast, *mic, *tm;
	uint8_t *p, dir;
	size_t tlen, mlen, cklen;
	uint32_t seq;
	char buf[8];

	mlen = m_length(m, &mlast);

	tlen = token_length(kc->kc_tokenkey);
	p = krb5_make_token("\x01\x01", tlen, tlen, &mic);
	p += 2;			/* TOK_ID */
	*p++ = sgn_alg[0];	/* SGN_ALG */
	*p++ = sgn_alg[1];

	*p++ = 0xff;		/* filler */
	*p++ = 0xff;
	*p++ = 0xff;
	*p++ = 0xff;

	/*
	 * SGN_CKSUM:
	 *
	 * Calculate the keyed checksum of the token header plus the
	 * message.
	 */
	cklen = kc->kc_checksumkey->ks_class->ec_checksumlen;

	mic->m_len = p - (uint8_t *) mic->m_data;
	mic->m_next = m;
	MGET(tm, M_WAITOK, MT_DATA);
	tm->m_len = cklen;
	mlast->m_next = tm;

	krb5_checksum(kc->kc_checksumkey, 15, mic, mic->m_len - 8,
	    8 + mlen, cklen);
	bcopy(tm->m_data, p + 8, cklen);
	mic->m_next = NULL;
	mlast->m_next = NULL;
	m_free(tm);
	
	/*
	 * SND_SEQ:
	 *
	 * Take the four bytes of the sequence number least
	 * significant first followed by four bytes of direction
	 * marker (zero for initiator and 0xff for acceptor). Encrypt
	 * that data using the SGN_CKSUM as IV. Note: ARC4 wants the
	 * sequence number big-endian.
	 */
	seq = atomic_fetchadd_32(&kc->kc_local_seqnumber, 1);
	if (sgn_alg[0] == 0x11) {
		p[0] = (seq >> 24);
		p[1] = (seq >> 16);
		p[2] = (seq >> 8);
		p[3] = (seq >> 0);
	} else {
		p[0] = (seq >> 0);
		p[1] = (seq >> 8);
		p[2] = (seq >> 16);
		p[3] = (seq >> 24);
	}
	if (is_initiator(kc)) {
		dir = 0;
	} else {
		dir = 0xff;
	}
	p[4] = dir;
	p[5] = dir;
	p[6] = dir;
	p[7] = dir;
	bcopy(p + 8, buf, 8);

	/*
	 * Set the mic buffer to its final size so that the encrypt
	 * can see the SND_SEQ part.
	 */
	mic->m_len += 8 + cklen;
	krb5_encrypt(kc->kc_tokenkey, mic, mic->m_len - cklen - 8, 8, buf, 8);

	*micp = mic;
	return (GSS_S_COMPLETE);
}

static OM_uint32
krb5_get_mic_new(struct krb5_context *kc,  struct mbuf *m,
    struct mbuf **micp)
{
	struct krb5_key_state *key = kc->kc_send_sign_Kc;
	struct mbuf *mlast, *mic;
	uint8_t *p;
	int flags;
	size_t mlen, cklen;
	uint32_t seq;

	mlen = m_length(m, &mlast);
	cklen = key->ks_class->ec_checksumlen;

	KASSERT(16 + cklen <= MLEN, ("checksum too large for an mbuf"));
	MGET(mic, M_WAITOK, MT_DATA);
	M_ALIGN(mic, 16 + cklen);
	mic->m_len = 16 + cklen;
	p = mic->m_data;

	/* TOK_ID */
	p[0] = 0x04;
	p[1] = 0x04;

	/* Flags */
	flags = 0;
	if (is_acceptor(kc))
		flags |= GSS_TOKEN_SENT_BY_ACCEPTOR;
	if (kc->kc_more_flags & ACCEPTOR_SUBKEY)
		flags |= GSS_TOKEN_ACCEPTOR_SUBKEY;
	p[2] = flags;

	/* Filler */
	p[3] = 0xff;
	p[4] = 0xff;
	p[5] = 0xff;
	p[6] = 0xff;
	p[7] = 0xff;

	/* SND_SEQ */
	p[8] = 0;
	p[9] = 0;
	p[10] = 0;
	p[11] = 0;
	seq = atomic_fetchadd_32(&kc->kc_local_seqnumber, 1);
	p[12] = (seq >> 24);
	p[13] = (seq >> 16);
	p[14] = (seq >> 8);
	p[15] = (seq >> 0);

	/*
	 * SGN_CKSUM:
	 *
	 * Calculate the keyed checksum of the message plus the first
	 * 16 bytes of the token header.
	 */
	mlast->m_next = mic;
	krb5_checksum(key, 0, m, 0, mlen + 16, cklen);
	mlast->m_next = NULL;

	*micp = mic;
	return (GSS_S_COMPLETE);
}

static OM_uint32
krb5_get_mic(gss_ctx_id_t ctx, OM_uint32 *minor_status,
    gss_qop_t qop_req, struct mbuf *m, struct mbuf **micp)
{
	struct krb5_context *kc = (struct krb5_context *)ctx;

	*minor_status = 0;

	if (qop_req != GSS_C_QOP_DEFAULT)
		return (GSS_S_BAD_QOP);

	if (time_uptime > kc->kc_lifetime)
		return (GSS_S_CONTEXT_EXPIRED);

	switch (kc->kc_tokenkey->ks_class->ec_type) {
	case ETYPE_DES_CBC_CRC:
		return (krb5_get_mic_old(kc, m, micp, sgn_alg_des_md5));

	case ETYPE_DES3_CBC_SHA1:
		return (krb5_get_mic_old(kc, m, micp, sgn_alg_des3_sha1));

	case ETYPE_ARCFOUR_HMAC_MD5:
	case ETYPE_ARCFOUR_HMAC_MD5_56:
		return (krb5_get_mic_old(kc, m, micp, sgn_alg_hmac_md5));

	default:
		return (krb5_get_mic_new(kc, m, micp));
	}

	return (GSS_S_FAILURE);
}

static OM_uint32
krb5_verify_mic_old(struct krb5_context *kc, struct mbuf *m, struct mbuf *mic,
    uint8_t sgn_alg[2])
{
	struct mbuf *mlast, *tm;
	uint8_t *p, *tp, dir;
	size_t mlen, tlen, elen, miclen;
	size_t cklen;
	uint32_t seq;

	mlen = m_length(m, &mlast);

	tlen = token_length(kc->kc_tokenkey);
	p = krb5_verify_token("\x01\x01", tlen, &mic, &elen, FALSE);
	if (!p)
		return (GSS_S_DEFECTIVE_TOKEN);
#if 0
	/*
	 * Disable this check - heimdal-1.1 generates DES3 MIC tokens
	 * that are 2 bytes too big.
	 */
	if (elen != tlen)
		return (GSS_S_DEFECTIVE_TOKEN);
#endif
	/* TOK_ID */
	p += 2;

	/* SGN_ALG */
	if (p[0] != sgn_alg[0] || p[1] != sgn_alg[1])
		return (GSS_S_DEFECTIVE_TOKEN);
	p += 2;

	if (p[0] != 0xff || p[1] != 0xff || p[2] != 0xff || p[3] != 0xff)
		return (GSS_S_DEFECTIVE_TOKEN);
	p += 4;

	/*
	 * SGN_CKSUM:
	 *
	 * Calculate the keyed checksum of the token header plus the
	 * message.
	 */
	cklen = kc->kc_checksumkey->ks_class->ec_checksumlen;
	miclen = mic->m_len;
	mic->m_len = p - (uint8_t *) mic->m_data;
	mic->m_next = m;
	MGET(tm, M_WAITOK, MT_DATA);
	tm->m_len = cklen;
	mlast->m_next = tm;

	krb5_checksum(kc->kc_checksumkey, 15, mic, mic->m_len - 8,
	    8 + mlen, cklen);
	mic->m_next = NULL;
	mlast->m_next = NULL;
	if (bcmp(tm->m_data, p + 8, cklen)) {
		m_free(tm);
		return (GSS_S_BAD_SIG);
	}

	/*
	 * SND_SEQ:
	 *
	 * Take the four bytes of the sequence number least
	 * significant first followed by four bytes of direction
	 * marker (zero for initiator and 0xff for acceptor). Encrypt
	 * that data using the SGN_CKSUM as IV.  Note: ARC4 wants the
	 * sequence number big-endian.
	 */
	bcopy(p, tm->m_data, 8);
	tm->m_len = 8;
	krb5_decrypt(kc->kc_tokenkey, tm, 0, 8, p + 8, 8);

	tp = tm->m_data;
	if (sgn_alg[0] == 0x11) {
		seq = tp[3] | (tp[2] << 8) | (tp[1] << 16) | (tp[0] << 24);
	} else {
		seq = tp[0] | (tp[1] << 8) | (tp[2] << 16) | (tp[3] << 24);
	}

	if (is_initiator(kc)) {
		dir = 0xff;
	} else {
		dir = 0;
	}
	if (tp[4] != dir || tp[5] != dir || tp[6] != dir || tp[7] != dir) {
		m_free(tm);
		return (GSS_S_DEFECTIVE_TOKEN);
	}
	m_free(tm);

	if (kc->kc_msg_order.km_flags &
		(GSS_C_REPLAY_FLAG | GSS_C_SEQUENCE_FLAG)) {
		return (krb5_sequence_check(kc, seq));
	}

	return (GSS_S_COMPLETE);
}

static OM_uint32
krb5_verify_mic_new(struct krb5_context *kc, struct mbuf *m, struct mbuf *mic)
{
	OM_uint32 res;
	struct krb5_key_state *key = kc->kc_recv_sign_Kc;
	struct mbuf *mlast;
	uint8_t *p;
	int flags;
	size_t mlen, cklen;
	char buf[32];

	mlen = m_length(m, &mlast);
	cklen = key->ks_class->ec_checksumlen;

	KASSERT(mic->m_next == NULL, ("MIC should be contiguous"));
	if (mic->m_len != 16 + cklen)
		return (GSS_S_DEFECTIVE_TOKEN);
	p = mic->m_data;

	/* TOK_ID */
	if (p[0] != 0x04)
		return (GSS_S_DEFECTIVE_TOKEN);
	if (p[1] != 0x04)
		return (GSS_S_DEFECTIVE_TOKEN);

	/* Flags */
	flags = 0;
	if (is_initiator(kc))
		flags |= GSS_TOKEN_SENT_BY_ACCEPTOR;
	if (kc->kc_more_flags & ACCEPTOR_SUBKEY)
		flags |= GSS_TOKEN_ACCEPTOR_SUBKEY;
	if (p[2] != flags)
		return (GSS_S_DEFECTIVE_TOKEN);

	/* Filler */
	if (p[3] != 0xff)
		return (GSS_S_DEFECTIVE_TOKEN);
	if (p[4] != 0xff)
		return (GSS_S_DEFECTIVE_TOKEN);
	if (p[5] != 0xff)
		return (GSS_S_DEFECTIVE_TOKEN);
	if (p[6] != 0xff)
		return (GSS_S_DEFECTIVE_TOKEN);
	if (p[7] != 0xff)
		return (GSS_S_DEFECTIVE_TOKEN);

	/* SND_SEQ */
	if (kc->kc_msg_order.km_flags &
		(GSS_C_REPLAY_FLAG | GSS_C_SEQUENCE_FLAG)) {
		uint32_t seq;
		if (p[8] || p[9] || p[10] || p[11]) {
			res = GSS_S_UNSEQ_TOKEN;
		} else {
			seq = (p[12] << 24) | (p[13] << 16)
				| (p[14] << 8) | p[15];
			res = krb5_sequence_check(kc, seq);
		}
		if (GSS_ERROR(res))
			return (res);
	} else {
		res = GSS_S_COMPLETE;
	}

	/*
	 * SGN_CKSUM:
	 *
	 * Calculate the keyed checksum of the message plus the first
	 * 16 bytes of the token header.
	 */
	m_copydata(mic, 16, cklen, buf);
	mlast->m_next = mic;
	krb5_checksum(key, 0, m, 0, mlen + 16, cklen);
	mlast->m_next = NULL;
	if (bcmp(buf, p + 16, cklen)) {
		return (GSS_S_BAD_SIG);
	}

	return (GSS_S_COMPLETE);
}

static OM_uint32
krb5_verify_mic(gss_ctx_id_t ctx, OM_uint32 *minor_status,
    struct mbuf *m, struct mbuf *mic, gss_qop_t *qop_state)
{
	struct krb5_context *kc = (struct krb5_context *)ctx;

	*minor_status = 0;
	if (qop_state)
		*qop_state = GSS_C_QOP_DEFAULT;

	if (time_uptime > kc->kc_lifetime)
		return (GSS_S_CONTEXT_EXPIRED);

	switch (kc->kc_tokenkey->ks_class->ec_type) {
	case ETYPE_DES_CBC_CRC:
		return (krb5_verify_mic_old(kc, m, mic, sgn_alg_des_md5));

	case ETYPE_ARCFOUR_HMAC_MD5:
	case ETYPE_ARCFOUR_HMAC_MD5_56:
		return (krb5_verify_mic_old(kc, m, mic, sgn_alg_hmac_md5));

	case ETYPE_DES3_CBC_SHA1:
		return (krb5_verify_mic_old(kc, m, mic, sgn_alg_des3_sha1));

	default:
		return (krb5_verify_mic_new(kc, m, mic));
	}

	return (GSS_S_FAILURE);
}

static OM_uint32
krb5_wrap_old(struct krb5_context *kc, int conf_req_flag,
    struct mbuf **mp, int *conf_state,
    uint8_t sgn_alg[2], uint8_t seal_alg[2])
{
	struct mbuf *m, *mlast, *tm, *cm, *pm;
	size_t mlen, tlen, padlen, datalen;
	uint8_t *p, dir;
	size_t cklen;
	uint8_t buf[8];
	uint32_t seq;

	/*
	 * How many trailing pad bytes do we need?
	 */
	m = *mp;
	mlen = m_length(m, &mlast);
	tlen = kc->kc_tokenkey->ks_class->ec_msgblocklen;
	padlen = tlen - (mlen % tlen);

	/*
	 * The data part of the token has eight bytes of random
	 * confounder prepended and followed by up to eight bytes of
	 * padding bytes each of which is set to the number of padding
	 * bytes.
	 */
	datalen = mlen + 8 + padlen;
	tlen = token_length(kc->kc_tokenkey);

	p = krb5_make_token("\x02\x01", tlen, datalen + tlen, &tm);
	p += 2;			/* TOK_ID */
	*p++ = sgn_alg[0];	/* SGN_ALG */
	*p++ = sgn_alg[1];
	if (conf_req_flag) {
		*p++ = seal_alg[0]; /* SEAL_ALG */
		*p++ = seal_alg[1];
	} else {
		*p++ = 0xff;	/* SEAL_ALG = none */
		*p++ = 0xff;
	}

	*p++ = 0xff;		/* filler */
	*p++ = 0xff;

	/*
	 * Copy the padded message data.
	 */
	if (M_LEADINGSPACE(m) >= 8) {
		m->m_data -= 8;
		m->m_len += 8;
	} else {
		MGET(cm, M_WAITOK, MT_DATA);
		cm->m_len = 8;
		cm->m_next = m;
		m = cm;
	}
	arc4rand(m->m_data, 8, 0);
	if (M_TRAILINGSPACE(mlast) >= padlen) {
		memset(mlast->m_data + mlast->m_len, padlen, padlen);
		mlast->m_len += padlen;
	} else {
		MGET(pm, M_WAITOK, MT_DATA);
		memset(pm->m_data, padlen, padlen);
		pm->m_len = padlen;
		mlast->m_next = pm;
		mlast = pm;
	}
	tm->m_next = m;

	/*
	 * SGN_CKSUM:
	 *
	 * Calculate the keyed checksum of the token header plus the
	 * padded message. Fiddle with tm->m_len so that we only
	 * checksum the 8 bytes of head that we care about.
	 */
	cklen = kc->kc_checksumkey->ks_class->ec_checksumlen;
	tlen = tm->m_len;
	tm->m_len = p - (uint8_t *) tm->m_data;
	MGET(cm, M_WAITOK, MT_DATA);
	cm->m_len = cklen;
	mlast->m_next = cm;
	krb5_checksum(kc->kc_checksumkey, 13, tm, tm->m_len - 8,
	    datalen + 8, cklen);
	tm->m_len = tlen;
	mlast->m_next = NULL;
	bcopy(cm->m_data, p + 8, cklen);
	m_free(cm);

	/*
	 * SND_SEQ:
	 *
	 * Take the four bytes of the sequence number least
	 * significant first (most significant first for ARCFOUR)
	 * followed by four bytes of direction marker (zero for
	 * initiator and 0xff for acceptor). Encrypt that data using
	 * the SGN_CKSUM as IV.
	 */
	seq = atomic_fetchadd_32(&kc->kc_local_seqnumber, 1);
	if (sgn_alg[0] == 0x11) {
		p[0] = (seq >> 24);
		p[1] = (seq >> 16);
		p[2] = (seq >> 8);
		p[3] = (seq >> 0);
	} else {
		p[0] = (seq >> 0);
		p[1] = (seq >> 8);
		p[2] = (seq >> 16);
		p[3] = (seq >> 24);
	}
	if (is_initiator(kc)) {
		dir = 0;
	} else {
		dir = 0xff;
	}
	p[4] = dir;
	p[5] = dir;
	p[6] = dir;
	p[7] = dir;
	krb5_encrypt(kc->kc_tokenkey, tm, p - (uint8_t *) tm->m_data,
	    8, p + 8, 8);

	if (conf_req_flag) {
		/*
		 * Encrypt the padded message with an IV of zero for
		 * DES and DES3, or an IV of the sequence number in
		 * big-endian format for ARCFOUR.
		 */
		if (seal_alg[0] == 0x10) {
			buf[0] = (seq >> 24);
			buf[1] = (seq >> 16);
			buf[2] = (seq >> 8);
			buf[3] = (seq >> 0);
			krb5_encrypt(kc->kc_encryptkey, m, 0, datalen,
			    buf, 4);
		} else {
			krb5_encrypt(kc->kc_encryptkey, m, 0, datalen,
			    NULL, 0);
		}
	}

	if (conf_state)
		*conf_state = conf_req_flag;

	*mp = tm;
	return (GSS_S_COMPLETE);
}

static OM_uint32
krb5_wrap_new(struct krb5_context *kc, int conf_req_flag,
    struct mbuf **mp, int *conf_state)
{
	struct krb5_key_state *Ke = kc->kc_send_seal_Ke;
	struct krb5_key_state *Ki = kc->kc_send_seal_Ki;
	struct krb5_key_state *Kc = kc->kc_send_seal_Kc;
	const struct krb5_encryption_class *ec = Ke->ks_class;
	struct mbuf *m, *mlast, *tm;
	uint8_t *p;
	int flags, EC;
	size_t mlen, blen, mblen, cklen, ctlen;
	uint32_t seq;
	static char zpad[32];

	m = *mp;
	mlen = m_length(m, &mlast);

	blen = ec->ec_blocklen;
	mblen = ec->ec_msgblocklen;
	cklen = ec->ec_checksumlen;

	if (conf_req_flag) {
		/*
		 * For sealed messages, we need space for 16 bytes of
		 * header, blen confounder, plaintext, padding, copy
		 * of header and checksum.
		 *
		 * We pad to mblen (which may be different from
		 * blen). If the encryption class is using CTS, mblen
		 * will be one (i.e. no padding required).
		 */
		if (mblen > 1)
			EC = mlen % mblen;
		else
			EC = 0;
		ctlen = blen + mlen + EC + 16;

		/*
		 * Put initial header and confounder before the
		 * message.
		 */
		M_PREPEND(m, 16 + blen, M_WAITOK);

		/*
		 * Append padding + copy of header and checksum. Try
		 * to fit this into the end of the original message,
		 * otherwise allocate a trailer.
		 */
		if (M_TRAILINGSPACE(mlast) >= EC + 16 + cklen) {
			tm = NULL;
			mlast->m_len += EC + 16 + cklen;
		} else {
			MGET(tm, M_WAITOK, MT_DATA);
			tm->m_len = EC + 16 + cklen;
			mlast->m_next = tm;
		}
	} else {
		/*
		 * For unsealed messages, we need 16 bytes of header
		 * plus space for the plaintext and a checksum. EC is
		 * set to the checksum size. We leave space in tm for
		 * a copy of the header - this will be trimmed later.
		 */
		M_PREPEND(m, 16, M_WAITOK);

		MGET(tm, M_WAITOK, MT_DATA);
		tm->m_len = cklen + 16;
		mlast->m_next = tm;
		ctlen = 0;
		EC = cklen;
	}

	p = m->m_data;

	/* TOK_ID */
	p[0] = 0x05;
	p[1] = 0x04;

	/* Flags */
	flags = 0;
	if (conf_req_flag)
		flags = GSS_TOKEN_SEALED;
	if (is_acceptor(kc))
		flags |= GSS_TOKEN_SENT_BY_ACCEPTOR;
	if (kc->kc_more_flags & ACCEPTOR_SUBKEY)
		flags |= GSS_TOKEN_ACCEPTOR_SUBKEY;
	p[2] = flags;

	/* Filler */
	p[3] = 0xff;

	/* EC + RRC - set to zero initially */
	p[4] = 0;
	p[5] = 0;
	p[6] = 0;
	p[7] = 0;

	/* SND_SEQ */
	p[8] = 0;
	p[9] = 0;
	p[10] = 0;
	p[11] = 0;
	seq = atomic_fetchadd_32(&kc->kc_local_seqnumber, 1);
	p[12] = (seq >> 24);
	p[13] = (seq >> 16);
	p[14] = (seq >> 8);
	p[15] = (seq >> 0);

	if (conf_req_flag) {
		/*
		 * Encrypt according to RFC 4121 section 4.2 and RFC
		 * 3961 section 5.3. Note: we don't generate tokens
		 * with RRC values other than zero. If we did, we
		 * should zero RRC in the copied header.
		 */
		arc4rand(p + 16, blen, 0);
		if (EC) {
			m_copyback(m, 16 + blen + mlen, EC, zpad);
		}
		m_copyback(m, 16 + blen + mlen + EC, 16, p);

		krb5_checksum(Ki, 0, m, 16, ctlen, cklen);
		krb5_encrypt(Ke, m, 16, ctlen, NULL, 0);
	} else {
		/*
		 * The plaintext message is followed by a checksum of
		 * the plaintext plus a version of the header where EC
		 * and RRC are set to zero. Also, the original EC must
		 * be our checksum size.
		 */
		bcopy(p, tm->m_data, 16);
		krb5_checksum(Kc, 0, m, 16, mlen + 16, cklen);
		tm->m_data += 16;
		tm->m_len -= 16;
	}

	/*
	 * Finally set EC to its actual value
	 */
	p[4] = EC >> 8;
	p[5] = EC;

	*mp = m;
	return (GSS_S_COMPLETE);
}

static OM_uint32
krb5_wrap(gss_ctx_id_t ctx, OM_uint32 *minor_status,
    int conf_req_flag, gss_qop_t qop_req,
    struct mbuf **mp, int *conf_state)
{
	struct krb5_context *kc = (struct krb5_context *)ctx;

	*minor_status = 0;
	if (conf_state)
		*conf_state = 0;

	if (qop_req != GSS_C_QOP_DEFAULT)
		return (GSS_S_BAD_QOP);

	if (time_uptime > kc->kc_lifetime)
		return (GSS_S_CONTEXT_EXPIRED);

	switch (kc->kc_tokenkey->ks_class->ec_type) {
	case ETYPE_DES_CBC_CRC:
		return (krb5_wrap_old(kc, conf_req_flag,
			mp, conf_state, sgn_alg_des_md5, seal_alg_des));

	case ETYPE_ARCFOUR_HMAC_MD5:
	case ETYPE_ARCFOUR_HMAC_MD5_56:
		return (krb5_wrap_old(kc, conf_req_flag,
			mp, conf_state, sgn_alg_hmac_md5, seal_alg_rc4));

	case ETYPE_DES3_CBC_SHA1:
		return (krb5_wrap_old(kc, conf_req_flag,
			mp, conf_state, sgn_alg_des3_sha1, seal_alg_des3));

	default:
		return (krb5_wrap_new(kc, conf_req_flag, mp, conf_state));
	}

	return (GSS_S_FAILURE);
}

static void
m_trim(struct mbuf *m, int len)
{
	struct mbuf *n;
	int off;

	if (m == NULL)
		return;
	n = m_getptr(m, len, &off);
	if (n) {
		n->m_len = off;
		if (n->m_next) {
			m_freem(n->m_next);
			n->m_next = NULL;
		}
	}
}

static OM_uint32
krb5_unwrap_old(struct krb5_context *kc, struct mbuf **mp, int *conf_state,
    uint8_t sgn_alg[2], uint8_t seal_alg[2])
{
	OM_uint32 res;
	struct mbuf *m, *mlast, *hm, *cm, *n;
	uint8_t *p, dir;
	size_t mlen, tlen, elen, datalen, padlen;
	size_t cklen;
	uint8_t buf[32];
	uint32_t seq;
	int i, conf;

	m = *mp;
	mlen = m_length(m, &mlast);

	tlen = token_length(kc->kc_tokenkey);
	cklen = kc->kc_tokenkey->ks_class->ec_checksumlen;

	p = krb5_verify_token("\x02\x01", tlen, &m, &elen, TRUE);
	*mp = m;
	if (!p)
		return (GSS_S_DEFECTIVE_TOKEN);
	datalen = elen - tlen;

	/*
	 * Trim the framing header first to make life a little easier
	 * later.
	 */
	m_adj(m, p - (uint8_t *) m->m_data);

	/* TOK_ID */
	p += 2;

	/* SGN_ALG */
	if (p[0] != sgn_alg[0] || p[1] != sgn_alg[1])
		return (GSS_S_DEFECTIVE_TOKEN);
	p += 2;

	/* SEAL_ALG */
	if (p[0] == seal_alg[0] && p[1] == seal_alg[1])
		conf = 1;
	else if (p[0] == 0xff && p[1] == 0xff)
		conf = 0;
	else
		return (GSS_S_DEFECTIVE_TOKEN);
	p += 2;

	if (p[0] != 0xff || p[1] != 0xff)
		return (GSS_S_DEFECTIVE_TOKEN);
	p += 2;

	/*
	 * SND_SEQ:
	 *
	 * Take the four bytes of the sequence number least
	 * significant first (most significant for ARCFOUR) followed
	 * by four bytes of direction marker (zero for initiator and
	 * 0xff for acceptor). Encrypt that data using the SGN_CKSUM
	 * as IV.
	 */
	krb5_decrypt(kc->kc_tokenkey, m, 8, 8, p + 8, 8);
	if (sgn_alg[0] == 0x11) {
		seq = p[3] | (p[2] << 8) | (p[1] << 16) | (p[0] << 24);
	} else {
		seq = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
	}

	if (is_initiator(kc)) {
		dir = 0xff;
	} else {
		dir = 0;
	}
	if (p[4] != dir || p[5] != dir || p[6] != dir || p[7] != dir)
		return (GSS_S_DEFECTIVE_TOKEN);

	if (kc->kc_msg_order.km_flags &
	    (GSS_C_REPLAY_FLAG | GSS_C_SEQUENCE_FLAG)) {
		res = krb5_sequence_check(kc, seq);
		if (GSS_ERROR(res))
			return (res);
	} else {
		res = GSS_S_COMPLETE;
	}

	/*
	 * If the token was encrypted, decode it in-place.
	 */
	if (conf) {
		/*
		 * Decrypt the padded message with an IV of zero for
		 * DES and DES3 or an IV of the big-endian encoded
		 * sequence number for ARCFOUR.
		 */
		if (seal_alg[0] == 0x10) {
			krb5_decrypt(kc->kc_encryptkey, m, 16 + cklen,
			    datalen, p, 4);
		} else {
			krb5_decrypt(kc->kc_encryptkey, m, 16 + cklen,
			    datalen, NULL, 0);
		}
	}
	if (conf_state)
		*conf_state = conf;

	/*
	 * Check the trailing pad bytes.
	 * RFC1964 specifies between 1<->8 bytes, each with a binary value
	 * equal to the number of bytes.
	 */
	if (mlast->m_len > 0)
		padlen = mlast->m_data[mlast->m_len - 1];
	else {
		n = m_getptr(m, tlen + datalen - 1, &i);
		/*
		 * When the position is exactly equal to the # of data bytes
		 * in the mbuf list, m_getptr() will return the last mbuf in
		 * the list and an off == m_len for that mbuf, so that case
		 * needs to be checked as well as a NULL return.
		 */
		if (n == NULL || n->m_len == i)
			return (GSS_S_DEFECTIVE_TOKEN);
		padlen = n->m_data[i];
	}
	if (padlen < 1 || padlen > 8 || padlen > tlen + datalen)
		return (GSS_S_DEFECTIVE_TOKEN);
	m_copydata(m, tlen + datalen - padlen, padlen, buf);
	for (i = 0; i < padlen; i++) {
		if (buf[i] != padlen) {
			return (GSS_S_DEFECTIVE_TOKEN);
		}
	}

	/*
	 * SGN_CKSUM:
	 *
	 * Calculate the keyed checksum of the token header plus the
	 * padded message. We do a little mbuf surgery to trim out the
	 * parts we don't want to checksum.
	 */
	hm = m;
	*mp = m = m_split(m, 16 + cklen, M_WAITOK);
	mlast = m_last(m);
	hm->m_len = 8;
	hm->m_next = m;
	MGET(cm, M_WAITOK, MT_DATA);
	cm->m_len = cklen;
	mlast->m_next = cm;

	krb5_checksum(kc->kc_checksumkey, 13, hm, 0, datalen + 8, cklen);
	hm->m_next = NULL;
	mlast->m_next = NULL;

	if (bcmp(cm->m_data, hm->m_data + 16, cklen)) {
		m_freem(hm);
		m_free(cm);
		return (GSS_S_BAD_SIG);
	}
	m_freem(hm);
	m_free(cm);

	/*
	 * Trim off the confounder and padding.
	 */
	m_adj(m, 8);
	if (mlast->m_len >= padlen) {
		mlast->m_len -= padlen;
	} else {
		m_trim(m, datalen - 8 - padlen);
	}

	*mp = m;
	return (res);
}

static OM_uint32
krb5_unwrap_new(struct krb5_context *kc, struct mbuf **mp, int *conf_state)
{
	OM_uint32 res;
	struct krb5_key_state *Ke = kc->kc_recv_seal_Ke;
	struct krb5_key_state *Ki = kc->kc_recv_seal_Ki;
	struct krb5_key_state *Kc = kc->kc_recv_seal_Kc;
	const struct krb5_encryption_class *ec = Ke->ks_class;
	struct mbuf *m, *mlast, *hm, *cm;
	uint8_t *p, *pp;
	int sealed, flags, EC, RRC;
	size_t blen, cklen, ctlen, mlen, plen, tlen;
	char buf[32], buf2[32];

	m = *mp;
	mlen = m_length(m, &mlast);

	if (mlen <= 16)
		return (GSS_S_DEFECTIVE_TOKEN);
	if (m->m_len < 16) {
		m = m_pullup(m, 16);
		*mp = m;
	}
	p = m->m_data;

	/* TOK_ID */
	if (p[0] != 0x05)
		return (GSS_S_DEFECTIVE_TOKEN);
	if (p[1] != 0x04)
		return (GSS_S_DEFECTIVE_TOKEN);

	/* Flags */
	sealed = p[2] & GSS_TOKEN_SEALED;
	flags = sealed;
	if (is_initiator(kc))
		flags |= GSS_TOKEN_SENT_BY_ACCEPTOR;
	if (kc->kc_more_flags & ACCEPTOR_SUBKEY)
		flags |= GSS_TOKEN_ACCEPTOR_SUBKEY;
	if (p[2] != flags)
		return (GSS_S_DEFECTIVE_TOKEN);

	/* Filler */
	if (p[3] != 0xff)
		return (GSS_S_DEFECTIVE_TOKEN);

	/* EC + RRC */
	EC = (p[4] << 8) + p[5];
	RRC = (p[6] << 8) + p[7];

	/* SND_SEQ */
	if (kc->kc_msg_order.km_flags &
		(GSS_C_REPLAY_FLAG | GSS_C_SEQUENCE_FLAG)) {
		uint32_t seq;
		if (p[8] || p[9] || p[10] || p[11]) {
			res = GSS_S_UNSEQ_TOKEN;
		} else {
			seq = (p[12] << 24) | (p[13] << 16)
				| (p[14] << 8) | p[15];
			res = krb5_sequence_check(kc, seq);
		}
		if (GSS_ERROR(res))
			return (res);
	} else {
		res = GSS_S_COMPLETE;
	}

	/*
	 * Separate the header before dealing with RRC. We only need
	 * to keep the header if the message isn't encrypted.
	 */
	if (sealed) {
		hm = NULL;
		m_adj(m, 16);
	} else {
		hm = m;
		*mp = m = m_split(m, 16, M_WAITOK);
		mlast = m_last(m);
	}

	/*
	 * Undo the effects of RRC by rotating left.
	 */
	if (RRC > 0) {
		struct mbuf *rm;
		size_t rlen;

		rlen = mlen - 16;
		if (RRC <= sizeof(buf) && m->m_len >= rlen) {
			/*
			 * Simple case, just rearrange the bytes in m.
			 */
			bcopy(m->m_data, buf, RRC);
			bcopy(m->m_data + RRC, m->m_data, rlen - RRC);
			bcopy(buf, m->m_data + rlen - RRC, RRC);
		} else {
			/*
			 * More complicated - rearrange the mbuf
			 * chain.
			 */
			rm = m;
			*mp = m = m_split(m, RRC, M_WAITOK);
			m_cat(m, rm);
			mlast = rm;
		}
	}

	blen = ec->ec_blocklen;
	cklen = ec->ec_checksumlen;
	if (sealed) {
		/*
		 * Decrypt according to RFC 4121 section 4.2 and RFC
		 * 3961 section 5.3. The message must be large enough
		 * for a blocksize confounder, at least one block of
		 * cyphertext and a checksum.
		 */
		if (mlen < 16 + 2*blen + cklen)
			return (GSS_S_DEFECTIVE_TOKEN);

		ctlen = mlen - 16 - cklen;
		krb5_decrypt(Ke, m, 0, ctlen, NULL, 0);

		/*
		 * The size of the plaintext is ctlen minus blocklen
		 * (for the confounder), 16 (for the copy of the token
		 * header) and EC (for the filler). The actual
		 * plaintext starts after the confounder.
		 */
		plen = ctlen - blen - 16 - EC;
		pp = p + 16 + blen;

		/*
		 * Checksum the padded plaintext.
		 */
		m_copydata(m, ctlen, cklen, buf);
		krb5_checksum(Ki, 0, m, 0, ctlen, cklen);
		m_copydata(m, ctlen, cklen, buf2);

		if (bcmp(buf, buf2, cklen))
			return (GSS_S_BAD_SIG);

		/*
		 * Trim the message back to just plaintext.
		 */
		m_adj(m, blen);
		tlen = 16 + EC + cklen;
		if (mlast->m_len >= tlen) {
			mlast->m_len -= tlen;
		} else {
			m_trim(m, plen);
		}
	} else {
		/*
		 * The plaintext message is followed by a checksum of
		 * the plaintext plus a version of the header where EC
		 * and RRC are set to zero. Also, the original EC must
		 * be our checksum size.
		 */
		if (mlen < 16 + cklen || EC != cklen)
			return (GSS_S_DEFECTIVE_TOKEN);

		/*
		 * The size of the plaintext is simply the message
		 * size less header and checksum. The plaintext starts
		 * right after the header (which we have saved in hm).
		 */
		plen = mlen - 16 - cklen;

		/*
		 * Insert a copy of the header (with EC and RRC set to
		 * zero) between the plaintext message and the
		 * checksum.
		 */
		p = hm->m_data;
		p[4] = p[5] = p[6] = p[7] = 0;

		cm = m_split(m, plen, M_WAITOK);
		mlast = m_last(m);
		m->m_next = hm;
		hm->m_next = cm;

		bcopy(cm->m_data, buf, cklen);
		krb5_checksum(Kc, 0, m, 0, plen + 16, cklen);
		if (bcmp(cm->m_data, buf, cklen))
			return (GSS_S_BAD_SIG);

		/*
		 * The checksum matches, discard all buf the plaintext.
		 */
		mlast->m_next = NULL;
		m_freem(hm);
	}

	if (conf_state)
		*conf_state = (sealed != 0);

	return (res);
}

static OM_uint32
krb5_unwrap(gss_ctx_id_t ctx, OM_uint32 *minor_status,
    struct mbuf **mp, int *conf_state, gss_qop_t *qop_state)
{
	struct krb5_context *kc = (struct krb5_context *)ctx;
	OM_uint32 maj_stat;

	*minor_status = 0;
	if (qop_state)
		*qop_state = GSS_C_QOP_DEFAULT;
	if (conf_state)
		*conf_state = 0;

	if (time_uptime > kc->kc_lifetime)
		return (GSS_S_CONTEXT_EXPIRED);

	switch (kc->kc_tokenkey->ks_class->ec_type) {
	case ETYPE_DES_CBC_CRC:
		maj_stat = krb5_unwrap_old(kc, mp, conf_state,
			sgn_alg_des_md5, seal_alg_des);
		break;

	case ETYPE_ARCFOUR_HMAC_MD5:
	case ETYPE_ARCFOUR_HMAC_MD5_56:
		maj_stat = krb5_unwrap_old(kc, mp, conf_state,
			sgn_alg_hmac_md5, seal_alg_rc4);
		break;

	case ETYPE_DES3_CBC_SHA1:
		maj_stat = krb5_unwrap_old(kc, mp, conf_state,
			sgn_alg_des3_sha1, seal_alg_des3);
		break;

	default:
		maj_stat = krb5_unwrap_new(kc, mp, conf_state);
		break;
	}

	if (GSS_ERROR(maj_stat)) {
		m_freem(*mp);
		*mp = NULL;
	}

	return (maj_stat);
}

static OM_uint32
krb5_wrap_size_limit(gss_ctx_id_t ctx, OM_uint32 *minor_status,
    int conf_req_flag, gss_qop_t qop_req, OM_uint32 req_output_size,
    OM_uint32 *max_input_size)
{
	struct krb5_context *kc = (struct krb5_context *)ctx;
	const struct krb5_encryption_class *ec;
	OM_uint32 overhead;

	*minor_status = 0;
	*max_input_size = 0;

	if (qop_req != GSS_C_QOP_DEFAULT)
		return (GSS_S_BAD_QOP);

	ec = kc->kc_tokenkey->ks_class;
	switch (ec->ec_type) {
	case ETYPE_DES_CBC_CRC:
	case ETYPE_DES3_CBC_SHA1:
	case ETYPE_ARCFOUR_HMAC_MD5: 
	case ETYPE_ARCFOUR_HMAC_MD5_56:
		/*
		 * up to 5 bytes for [APPLICATION 0] SEQUENCE
		 * 2 + krb5 oid length
		 * 8 bytes of header
		 * 8 bytes of confounder
		 * maximum of 8 bytes of padding
		 * checksum
		 */
		overhead = 5 + 2 + krb5_mech_oid.length;
		overhead += 8 + 8 + ec->ec_msgblocklen;
		overhead += ec->ec_checksumlen;
		break;

	default:
		if (conf_req_flag) {
			/*
			 * 16 byts of header
			 * blocklen bytes of confounder
			 * up to msgblocklen - 1 bytes of padding
			 * 16 bytes for copy of header
			 * checksum
			 */
			overhead = 16 + ec->ec_blocklen;
			overhead += ec->ec_msgblocklen - 1;
			overhead += 16;
			overhead += ec->ec_checksumlen;
		} else {
			/*
			 * 16 bytes of header plus checksum.
			 */
			overhead = 16 + ec->ec_checksumlen;
		}
	}

	*max_input_size = req_output_size - overhead;

	return (GSS_S_COMPLETE);
}

static kobj_method_t krb5_methods[] = {
	KOBJMETHOD(kgss_init,		krb5_init),
	KOBJMETHOD(kgss_import,		krb5_import),
	KOBJMETHOD(kgss_delete,		krb5_delete),
	KOBJMETHOD(kgss_mech_type,	krb5_mech_type),
	KOBJMETHOD(kgss_get_mic,	krb5_get_mic),
	KOBJMETHOD(kgss_verify_mic,	krb5_verify_mic),
	KOBJMETHOD(kgss_wrap,		krb5_wrap),
	KOBJMETHOD(kgss_unwrap,		krb5_unwrap),
	KOBJMETHOD(kgss_wrap_size_limit, krb5_wrap_size_limit),
	{ 0, 0 }
};

static struct kobj_class krb5_class = {
	"kerberosv5",
	krb5_methods,
	sizeof(struct krb5_context)
};

/*
 * Kernel module glue
 */
static int
kgssapi_krb5_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		kgss_install_mech(&krb5_mech_oid, "kerberosv5", &krb5_class);
		break;

	case MOD_UNLOAD:
		kgss_uninstall_mech(&krb5_mech_oid);
		break;
	}


	return (0);
}
static moduledata_t kgssapi_krb5_mod = {
	"kgssapi_krb5",
	kgssapi_krb5_modevent,
	NULL,
};
DECLARE_MODULE(kgssapi_krb5, kgssapi_krb5_mod, SI_SUB_VFS, SI_ORDER_ANY);
MODULE_DEPEND(kgssapi_krb5, kgssapi, 1, 1, 1);
MODULE_DEPEND(kgssapi_krb5, crypto, 1, 1, 1);
MODULE_DEPEND(kgssapi_krb5, rc4, 1, 1, 1);
MODULE_VERSION(kgssapi_krb5, 1);
