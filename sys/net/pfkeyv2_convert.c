/*	$OpenBSD: pfkeyv2_convert.c,v 1.84 2025/07/07 02:28:50 jsg Exp $	*/
/*
 * The author of this code is Angelos D. Keromytis (angelos@keromytis.org)
 *
 * Part of this code is based on code written by Craig Metz (cmetz@inner.net)
 * for NRL. Those licenses follow this one.
 *
 * Copyright (c) 2001 Angelos D. Keromytis.
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

/*
 *	@(#)COPYRIGHT	1.1 (NRL) 17 January 1995
 *
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed at the Information
 *	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

/*
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 Craig Metz. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <net/route.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip_ipsp.h>
#include <net/pfkeyv2.h>
#include <crypto/cryptodev.h>
#include <crypto/xform.h>

#if NPF > 0
#include <net/pfvar.h>
#endif

/*
 * (Partly) Initialize a TDB based on an SADB_SA payload. Other parts
 * of the TDB will be initialized by other import routines, and tdb_init().
 */
void
import_sa(struct tdb *tdb, struct sadb_sa *sadb_sa, struct ipsecinit *ii)
{
	if (!sadb_sa)
		return;

	mtx_enter(&tdb->tdb_mtx);
	if (ii) {
		ii->ii_encalg = sadb_sa->sadb_sa_encrypt;
		ii->ii_authalg = sadb_sa->sadb_sa_auth;
		ii->ii_compalg = sadb_sa->sadb_sa_encrypt; /* Yeurk! */

		tdb->tdb_spi = sadb_sa->sadb_sa_spi;
		tdb->tdb_wnd = sadb_sa->sadb_sa_replay;

		if (sadb_sa->sadb_sa_flags & SADB_SAFLAGS_PFS)
			tdb->tdb_flags |= TDBF_PFS;

		if (sadb_sa->sadb_sa_flags & SADB_X_SAFLAGS_TUNNEL)
			tdb->tdb_flags |= TDBF_TUNNELING;

		if (sadb_sa->sadb_sa_flags & SADB_X_SAFLAGS_UDPENCAP)
			tdb->tdb_flags |= TDBF_UDPENCAP;

		if (sadb_sa->sadb_sa_flags & SADB_X_SAFLAGS_ESN)
			tdb->tdb_flags |= TDBF_ESN;
	}

	if (sadb_sa->sadb_sa_state != SADB_SASTATE_MATURE)
		tdb->tdb_flags |= TDBF_INVALID;
	mtx_leave(&tdb->tdb_mtx);
}

/*
 * Export some of the information on a TDB.
 */
void
export_sa(void **p, struct tdb *tdb)
{
	struct sadb_sa *sadb_sa = (struct sadb_sa *) *p;

	sadb_sa->sadb_sa_len = sizeof(struct sadb_sa) / sizeof(uint64_t);

	sadb_sa->sadb_sa_spi = tdb->tdb_spi;
	sadb_sa->sadb_sa_replay = tdb->tdb_wnd;

	if (tdb->tdb_flags & TDBF_INVALID)
		sadb_sa->sadb_sa_state = SADB_SASTATE_LARVAL;
	else
		sadb_sa->sadb_sa_state = SADB_SASTATE_MATURE;

	if (tdb->tdb_sproto == IPPROTO_IPCOMP &&
	    tdb->tdb_compalgxform != NULL) {
		switch (tdb->tdb_compalgxform->type) {
		case CRYPTO_DEFLATE_COMP:
			sadb_sa->sadb_sa_encrypt = SADB_X_CALG_DEFLATE;
			break;
		}
	}

	if (tdb->tdb_authalgxform) {
		switch (tdb->tdb_authalgxform->type) {
		case CRYPTO_MD5_HMAC:
			sadb_sa->sadb_sa_auth = SADB_AALG_MD5HMAC;
			break;

		case CRYPTO_SHA1_HMAC:
			sadb_sa->sadb_sa_auth = SADB_AALG_SHA1HMAC;
			break;

		case CRYPTO_RIPEMD160_HMAC:
			sadb_sa->sadb_sa_auth = SADB_X_AALG_RIPEMD160HMAC;
			break;

		case CRYPTO_SHA2_256_HMAC:
			sadb_sa->sadb_sa_auth = SADB_X_AALG_SHA2_256;
			break;

		case CRYPTO_SHA2_384_HMAC:
			sadb_sa->sadb_sa_auth = SADB_X_AALG_SHA2_384;
			break;

		case CRYPTO_SHA2_512_HMAC:
			sadb_sa->sadb_sa_auth = SADB_X_AALG_SHA2_512;
			break;

		case CRYPTO_AES_128_GMAC:
			sadb_sa->sadb_sa_auth = SADB_X_AALG_AES128GMAC;
			break;

		case CRYPTO_AES_192_GMAC:
			sadb_sa->sadb_sa_auth = SADB_X_AALG_AES192GMAC;
			break;

		case CRYPTO_AES_256_GMAC:
			sadb_sa->sadb_sa_auth = SADB_X_AALG_AES256GMAC;
			break;

		case CRYPTO_CHACHA20_POLY1305_MAC:
			sadb_sa->sadb_sa_auth = SADB_X_AALG_CHACHA20POLY1305;
			break;
		}
	}

	if (tdb->tdb_encalgxform) {
		switch (tdb->tdb_encalgxform->type) {
		case CRYPTO_NULL:
			sadb_sa->sadb_sa_encrypt = SADB_EALG_NULL;
			break;

		case CRYPTO_3DES_CBC:
			sadb_sa->sadb_sa_encrypt = SADB_EALG_3DESCBC;
			break;

		case CRYPTO_AES_CBC:
			sadb_sa->sadb_sa_encrypt = SADB_X_EALG_AES;
			break;

		case CRYPTO_AES_CTR:
			sadb_sa->sadb_sa_encrypt = SADB_X_EALG_AESCTR;
			break;

		case CRYPTO_AES_GCM_16:
			sadb_sa->sadb_sa_encrypt = SADB_X_EALG_AESGCM16;
			break;

		case CRYPTO_AES_GMAC:
			sadb_sa->sadb_sa_encrypt = SADB_X_EALG_AESGMAC;
			break;

		case CRYPTO_CAST_CBC:
			sadb_sa->sadb_sa_encrypt = SADB_X_EALG_CAST;
			break;

		case CRYPTO_BLF_CBC:
			sadb_sa->sadb_sa_encrypt = SADB_X_EALG_BLF;
			break;

		case CRYPTO_CHACHA20_POLY1305:
			sadb_sa->sadb_sa_encrypt = SADB_X_EALG_CHACHA20POLY1305;
			break;
		}
	}

	if (tdb->tdb_flags & TDBF_PFS)
		sadb_sa->sadb_sa_flags |= SADB_SAFLAGS_PFS;

	if (tdb->tdb_flags & TDBF_TUNNELING)
		sadb_sa->sadb_sa_flags |= SADB_X_SAFLAGS_TUNNEL;

	if (tdb->tdb_flags & TDBF_UDPENCAP)
		sadb_sa->sadb_sa_flags |= SADB_X_SAFLAGS_UDPENCAP;

	if (tdb->tdb_flags & TDBF_ESN)
		sadb_sa->sadb_sa_flags |= SADB_X_SAFLAGS_ESN;

	*p += sizeof(struct sadb_sa);
}

/*
 * Initialize expirations and counters based on lifetime payload.
 */
void
import_lifetime(struct tdb *tdb, struct sadb_lifetime *sadb_lifetime, int type)
{
	if (!sadb_lifetime)
		return;

	mtx_enter(&tdb->tdb_mtx);
	switch (type) {
	case PFKEYV2_LIFETIME_HARD:
		if ((tdb->tdb_exp_allocations =
		    sadb_lifetime->sadb_lifetime_allocations) != 0)
			tdb->tdb_flags |= TDBF_ALLOCATIONS;
		else
			tdb->tdb_flags &= ~TDBF_ALLOCATIONS;

		if ((tdb->tdb_exp_bytes =
		    sadb_lifetime->sadb_lifetime_bytes) != 0)
			tdb->tdb_flags |= TDBF_BYTES;
		else
			tdb->tdb_flags &= ~TDBF_BYTES;

		if ((tdb->tdb_exp_timeout =
		    sadb_lifetime->sadb_lifetime_addtime) != 0) {
			tdb->tdb_flags |= TDBF_TIMER;
		} else
			tdb->tdb_flags &= ~TDBF_TIMER;

		if ((tdb->tdb_exp_first_use =
		    sadb_lifetime->sadb_lifetime_usetime) != 0)
			tdb->tdb_flags |= TDBF_FIRSTUSE;
		else
			tdb->tdb_flags &= ~TDBF_FIRSTUSE;
		break;

	case PFKEYV2_LIFETIME_SOFT:
		if ((tdb->tdb_soft_allocations =
		    sadb_lifetime->sadb_lifetime_allocations) != 0)
			tdb->tdb_flags |= TDBF_SOFT_ALLOCATIONS;
		else
			tdb->tdb_flags &= ~TDBF_SOFT_ALLOCATIONS;

		if ((tdb->tdb_soft_bytes =
		    sadb_lifetime->sadb_lifetime_bytes) != 0)
			tdb->tdb_flags |= TDBF_SOFT_BYTES;
		else
			tdb->tdb_flags &= ~TDBF_SOFT_BYTES;

		if ((tdb->tdb_soft_timeout =
		    sadb_lifetime->sadb_lifetime_addtime) != 0) {
			tdb->tdb_flags |= TDBF_SOFT_TIMER;
		} else
			tdb->tdb_flags &= ~TDBF_SOFT_TIMER;

		if ((tdb->tdb_soft_first_use =
		    sadb_lifetime->sadb_lifetime_usetime) != 0)
			tdb->tdb_flags |= TDBF_SOFT_FIRSTUSE;
		else
			tdb->tdb_flags &= ~TDBF_SOFT_FIRSTUSE;
		break;

	case PFKEYV2_LIFETIME_CURRENT:  /* Nothing fancy here. */
		tdb->tdb_cur_allocations =
		    sadb_lifetime->sadb_lifetime_allocations;
		tdb->tdb_cur_bytes = sadb_lifetime->sadb_lifetime_bytes;
		tdb->tdb_established = sadb_lifetime->sadb_lifetime_addtime;
		tdb->tdb_first_use = sadb_lifetime->sadb_lifetime_usetime;
	}
	mtx_leave(&tdb->tdb_mtx);
}

/*
 * Export TDB expiration information.
 */
void
export_lifetime(void **p, struct tdb *tdb, int type)
{
	struct sadb_lifetime *sadb_lifetime = (struct sadb_lifetime *) *p;

	sadb_lifetime->sadb_lifetime_len = sizeof(struct sadb_lifetime) /
	    sizeof(uint64_t);

	switch (type) {
	case PFKEYV2_LIFETIME_HARD:
		if (tdb->tdb_flags & TDBF_ALLOCATIONS)
			sadb_lifetime->sadb_lifetime_allocations =
			    tdb->tdb_exp_allocations;

		if (tdb->tdb_flags & TDBF_BYTES)
			sadb_lifetime->sadb_lifetime_bytes =
			    tdb->tdb_exp_bytes;

		if (tdb->tdb_flags & TDBF_TIMER)
			sadb_lifetime->sadb_lifetime_addtime =
			    tdb->tdb_exp_timeout;

		if (tdb->tdb_flags & TDBF_FIRSTUSE)
			sadb_lifetime->sadb_lifetime_usetime =
			    tdb->tdb_exp_first_use;
		break;

	case PFKEYV2_LIFETIME_SOFT:
		if (tdb->tdb_flags & TDBF_SOFT_ALLOCATIONS)
			sadb_lifetime->sadb_lifetime_allocations =
			    tdb->tdb_soft_allocations;

		if (tdb->tdb_flags & TDBF_SOFT_BYTES)
			sadb_lifetime->sadb_lifetime_bytes =
			    tdb->tdb_soft_bytes;

		if (tdb->tdb_flags & TDBF_SOFT_TIMER)
			sadb_lifetime->sadb_lifetime_addtime =
			    tdb->tdb_soft_timeout;

		if (tdb->tdb_flags & TDBF_SOFT_FIRSTUSE)
			sadb_lifetime->sadb_lifetime_usetime =
			    tdb->tdb_soft_first_use;
		break;

	case PFKEYV2_LIFETIME_CURRENT:
		sadb_lifetime->sadb_lifetime_allocations =
		    tdb->tdb_cur_allocations;
		sadb_lifetime->sadb_lifetime_bytes = tdb->tdb_cur_bytes;
		sadb_lifetime->sadb_lifetime_addtime = tdb->tdb_established;
		sadb_lifetime->sadb_lifetime_usetime = tdb->tdb_first_use;
		break;

	case PFKEYV2_LIFETIME_LASTUSE:
		sadb_lifetime->sadb_lifetime_allocations = 0;
		sadb_lifetime->sadb_lifetime_bytes = 0;
		sadb_lifetime->sadb_lifetime_addtime = 0;
		sadb_lifetime->sadb_lifetime_usetime = tdb->tdb_last_used;
		break;
	}

	*p += sizeof(struct sadb_lifetime);
}

/*
 * Import flow information to two struct sockaddr_encap's. Either
 * all or none of the address arguments are NULL.
 */
int
import_flow(struct sockaddr_encap *flow, struct sockaddr_encap *flowmask,
    struct sadb_address *ssrc, struct sadb_address *ssrcmask,
    struct sadb_address *ddst, struct sadb_address *ddstmask,
    struct sadb_protocol *sab, struct sadb_protocol *ftype)
{
	u_int8_t transproto = 0;
	union sockaddr_union *src, *dst, *srcmask, *dstmask;

	if (ssrc == NULL)
		return 0; /* There wasn't any information to begin with. */

	src = (union sockaddr_union *)(ssrc + 1);
	dst = (union sockaddr_union *)(ddst + 1);
	srcmask = (union sockaddr_union *)(ssrcmask + 1);
	dstmask = (union sockaddr_union *)(ddstmask + 1);

	bzero(flow, sizeof(*flow));
	bzero(flowmask, sizeof(*flowmask));

	if (sab != NULL)
		transproto = sab->sadb_protocol_proto;

	/*
	 * Check that all the address families match. We know they are
	 * valid and supported because pfkeyv2_parsemessage() checked that.
	 */
	if ((src->sa.sa_family != dst->sa.sa_family) ||
	    (src->sa.sa_family != srcmask->sa.sa_family) ||
	    (src->sa.sa_family != dstmask->sa.sa_family))
		return EINVAL;

	/*
	 * We set these as an indication that tdb_filter/tdb_filtermask are
	 * in fact initialized.
	 */
	flow->sen_family = flowmask->sen_family = PF_KEY;
	flow->sen_len = flowmask->sen_len = SENT_LEN;

	switch (src->sa.sa_family) {
	case AF_INET:
		/* netmask handling */
		rt_maskedcopy(&src->sa, &src->sa, &srcmask->sa);
		rt_maskedcopy(&dst->sa, &dst->sa, &dstmask->sa);

		flow->sen_type = SENT_IP4;
		flow->sen_direction = ftype->sadb_protocol_direction;
		flow->sen_ip_src = src->sin.sin_addr;
		flow->sen_ip_dst = dst->sin.sin_addr;
		flow->sen_proto = transproto;
		flow->sen_sport = src->sin.sin_port;
		flow->sen_dport = dst->sin.sin_port;

		flowmask->sen_type = SENT_IP4;
		flowmask->sen_direction = 0xff;
		flowmask->sen_ip_src = srcmask->sin.sin_addr;
		flowmask->sen_ip_dst = dstmask->sin.sin_addr;
		flowmask->sen_sport = srcmask->sin.sin_port;
		flowmask->sen_dport = dstmask->sin.sin_port;
		if (transproto)
			flowmask->sen_proto = 0xff;
		break;

#ifdef INET6
	case AF_INET6:
		in6_embedscope(&src->sin6.sin6_addr, &src->sin6, NULL, NULL);
		in6_embedscope(&dst->sin6.sin6_addr, &dst->sin6, NULL, NULL);

		/* netmask handling */
		rt_maskedcopy(&src->sa, &src->sa, &srcmask->sa);
		rt_maskedcopy(&dst->sa, &dst->sa, &dstmask->sa);

		flow->sen_type = SENT_IP6;
		flow->sen_ip6_direction = ftype->sadb_protocol_direction;
		flow->sen_ip6_src = src->sin6.sin6_addr;
		flow->sen_ip6_dst = dst->sin6.sin6_addr;
		flow->sen_ip6_proto = transproto;
		flow->sen_ip6_sport = src->sin6.sin6_port;
		flow->sen_ip6_dport = dst->sin6.sin6_port;

		flowmask->sen_type = SENT_IP6;
		flowmask->sen_ip6_direction = 0xff;
		flowmask->sen_ip6_src = srcmask->sin6.sin6_addr;
		flowmask->sen_ip6_dst = dstmask->sin6.sin6_addr;
		flowmask->sen_ip6_sport = srcmask->sin6.sin6_port;
		flowmask->sen_ip6_dport = dstmask->sin6.sin6_port;
		if (transproto)
			flowmask->sen_ip6_proto = 0xff;
		break;
#endif /* INET6 */
	}

	return 0;
}

/*
 * Helper to export addresses from an struct sockaddr_encap.
 */
static void
export_encap(void **p, struct sockaddr_encap *encap, int type)
{
	struct sadb_address *saddr = (struct sadb_address *)*p;
	union sockaddr_union *sunion;

	*p += sizeof(struct sadb_address);
	sunion = (union sockaddr_union *)*p;

	switch (encap->sen_type) {
	case SENT_IP4:
		saddr->sadb_address_len = (sizeof(struct sadb_address) +
		    PADUP(sizeof(struct sockaddr_in))) / sizeof(uint64_t);
		sunion->sa.sa_len = sizeof(struct sockaddr_in);
		sunion->sa.sa_family = AF_INET;
		if (type == SADB_X_EXT_SRC_FLOW ||
		    type == SADB_X_EXT_SRC_MASK) {
			sunion->sin.sin_addr = encap->sen_ip_src;
			sunion->sin.sin_port = encap->sen_sport;
		} else {
			sunion->sin.sin_addr = encap->sen_ip_dst;
			sunion->sin.sin_port = encap->sen_dport;
		}
		*p += PADUP(sizeof(struct sockaddr_in));
		break;
	case SENT_IP6:
		saddr->sadb_address_len = (sizeof(struct sadb_address)
		    + PADUP(sizeof(struct sockaddr_in6))) / sizeof(uint64_t);
		sunion->sa.sa_len = sizeof(struct sockaddr_in6);
		sunion->sa.sa_family = AF_INET6;
		if (type == SADB_X_EXT_SRC_FLOW ||
		    type == SADB_X_EXT_SRC_MASK) {
			sunion->sin6.sin6_addr = encap->sen_ip6_src;
			sunion->sin6.sin6_port = encap->sen_ip6_sport;
		} else {
			sunion->sin6.sin6_addr = encap->sen_ip6_dst;
			sunion->sin6.sin6_port = encap->sen_ip6_dport;
		}
		*p += PADUP(sizeof(struct sockaddr_in6));
		break;
	}
}

/*
 * Export flow information from two struct sockaddr_encap's.
 */
void
export_flow(void **p, u_int8_t ftype, struct sockaddr_encap *flow,
    struct sockaddr_encap *flowmask, void **headers)
{
	struct sadb_protocol *sab;

	headers[SADB_X_EXT_FLOW_TYPE] = *p;
	sab = (struct sadb_protocol *)*p;
	sab->sadb_protocol_len = sizeof(struct sadb_protocol) /
	    sizeof(uint64_t);

	switch (ftype) {
	case IPSP_IPSEC_USE:
		sab->sadb_protocol_proto = SADB_X_FLOW_TYPE_USE;
		break;
	case IPSP_IPSEC_ACQUIRE:
		sab->sadb_protocol_proto = SADB_X_FLOW_TYPE_ACQUIRE;
		break;
	case IPSP_IPSEC_REQUIRE:
		sab->sadb_protocol_proto = SADB_X_FLOW_TYPE_REQUIRE;
		break;
	case IPSP_DENY:
		sab->sadb_protocol_proto = SADB_X_FLOW_TYPE_DENY;
		break;
	case IPSP_PERMIT:
		sab->sadb_protocol_proto = SADB_X_FLOW_TYPE_BYPASS;
		break;
	case IPSP_IPSEC_DONTACQ:
		sab->sadb_protocol_proto = SADB_X_FLOW_TYPE_DONTACQ;
		break;
	default:
		sab->sadb_protocol_proto = 0;
		break;
	}

	switch (flow->sen_type) {
	case SENT_IP4:
		sab->sadb_protocol_direction = flow->sen_direction;
		break;
#ifdef INET6
	case SENT_IP6:
		sab->sadb_protocol_direction = flow->sen_ip6_direction;
		break;
#endif /* INET6 */
	}
	*p += sizeof(struct sadb_protocol);

	headers[SADB_X_EXT_PROTOCOL] = *p;
	sab = (struct sadb_protocol *)*p;
	sab->sadb_protocol_len = sizeof(struct sadb_protocol) /
	    sizeof(uint64_t);
	switch (flow->sen_type) {
	case SENT_IP4:
		sab->sadb_protocol_proto = flow->sen_proto;
		break;
#ifdef INET6
	case SENT_IP6:
		sab->sadb_protocol_proto = flow->sen_ip6_proto;
		break;
#endif /* INET6 */
	}
	*p += sizeof(struct sadb_protocol);

	headers[SADB_X_EXT_SRC_FLOW] = *p;
	export_encap(p, flow, SADB_X_EXT_SRC_FLOW);

	headers[SADB_X_EXT_SRC_MASK] = *p;
	export_encap(p, flowmask, SADB_X_EXT_SRC_MASK);

	headers[SADB_X_EXT_DST_FLOW] = *p;
	export_encap(p, flow, SADB_X_EXT_DST_FLOW);

	headers[SADB_X_EXT_DST_MASK] = *p;
	export_encap(p, flowmask, SADB_X_EXT_DST_MASK);
}

/*
 * Copy an SADB_ADDRESS payload to a struct sockaddr.
 */
void
import_address(struct sockaddr *sa, struct sadb_address *sadb_address)
{
	int salen;
	struct sockaddr *ssa = (struct sockaddr *)((void *) sadb_address +
	    sizeof(struct sadb_address));

	if (!sadb_address)
		return;

	if (ssa->sa_len)
		salen = ssa->sa_len;
	else
		switch (ssa->sa_family) {
		case AF_INET:
			salen = sizeof(struct sockaddr_in);
			break;

#ifdef INET6
		case AF_INET6:
			salen = sizeof(struct sockaddr_in6);
			break;
#endif /* INET6 */

		default:
			return;
		}

	bcopy(ssa, sa, salen);
	sa->sa_len = salen;
}

/*
 * Export a struct sockaddr as an SADB_ADDRESS payload.
 */
void
export_address(void **p, struct sockaddr *sa)
{
	struct sadb_address *sadb_address = (struct sadb_address *) *p;

	sadb_address->sadb_address_len = (sizeof(struct sadb_address) +
	    PADUP(sa->sa_len)) / sizeof(uint64_t);

	*p += sizeof(struct sadb_address);
	bcopy(sa, *p, sa->sa_len);
	((struct sockaddr *) *p)->sa_family = sa->sa_family;
	*p += PADUP(sa->sa_len);
}

/*
 * Import an identity payload into the TDB.
 */
static void
import_identity(struct ipsec_id **id, struct sadb_ident *sadb_ident,
    size_t *id_sz)
{
	size_t id_len;

	if (!sadb_ident) {
		*id = NULL;
		return;
	}

	id_len = EXTLEN(sadb_ident) - sizeof(struct sadb_ident);
	*id_sz = sizeof(struct ipsec_id) + id_len;
	*id = malloc(*id_sz, M_CREDENTIALS, M_WAITOK);
	(*id)->len = id_len;

	switch (sadb_ident->sadb_ident_type) {
	case SADB_IDENTTYPE_PREFIX:
		(*id)->type = IPSP_IDENTITY_PREFIX;
		break;
	case SADB_IDENTTYPE_FQDN:
		(*id)->type = IPSP_IDENTITY_FQDN;
		break;
	case SADB_IDENTTYPE_USERFQDN:
		(*id)->type = IPSP_IDENTITY_USERFQDN;
		break;
	case SADB_IDENTTYPE_ASN1_DN:
		(*id)->type = IPSP_IDENTITY_ASN1_DN;
		break;
	default:
		free(*id, M_CREDENTIALS, *id_sz);
		*id = NULL;
		return;
	}
	bcopy((void *) sadb_ident + sizeof(struct sadb_ident), (*id) + 1,
	    (*id)->len);
}

void
import_identities(struct ipsec_ids **ids, int swapped,
    struct sadb_ident *srcid, struct sadb_ident *dstid)
{
	struct ipsec_ids *tmp;
	size_t id_local_sz, id_remote_sz;

	*ids = NULL;
	tmp = malloc(sizeof(struct ipsec_ids), M_CREDENTIALS, M_WAITOK);
	import_identity(&tmp->id_local, swapped ? dstid: srcid, &id_local_sz);
	import_identity(&tmp->id_remote, swapped ? srcid: dstid, &id_remote_sz);
	if (tmp->id_local != NULL && tmp->id_remote != NULL) {
		*ids = ipsp_ids_insert(tmp);
		if (*ids == tmp)
			return;
	}
	free(tmp->id_local, M_CREDENTIALS, id_local_sz);
	free(tmp->id_remote, M_CREDENTIALS, id_remote_sz);
	free(tmp, M_CREDENTIALS, sizeof(*tmp));
}

static void
export_identity(void **p, struct ipsec_id *id)
{
	struct sadb_ident *sadb_ident = (struct sadb_ident *) *p;

	sadb_ident->sadb_ident_len = (sizeof(struct sadb_ident) +
	    PADUP(id->len)) / sizeof(uint64_t);

	switch (id->type) {
	case IPSP_IDENTITY_PREFIX:
		sadb_ident->sadb_ident_type = SADB_IDENTTYPE_PREFIX;
		break;
	case IPSP_IDENTITY_FQDN:
		sadb_ident->sadb_ident_type = SADB_IDENTTYPE_FQDN;
		break;
	case IPSP_IDENTITY_USERFQDN:
		sadb_ident->sadb_ident_type = SADB_IDENTTYPE_USERFQDN;
		break;
	case IPSP_IDENTITY_ASN1_DN:
		sadb_ident->sadb_ident_type = SADB_IDENTTYPE_ASN1_DN;
		break;
	}
	*p += sizeof(struct sadb_ident);
	bcopy(id + 1, *p, id->len);
	*p += PADUP(id->len);
}

void
export_identities(void **p, struct ipsec_ids *ids, int swapped,
    void **headers)
{
	headers[SADB_EXT_IDENTITY_SRC] = *p;
	export_identity(p, swapped ? ids->id_remote : ids->id_local);
	headers[SADB_EXT_IDENTITY_DST] = *p;
	export_identity(p, swapped ? ids->id_local : ids->id_remote);
}

/* ... */
void
import_key(struct ipsecinit *ii, struct sadb_key *sadb_key, int type)
{
	if (!sadb_key)
		return;

	if (type == PFKEYV2_ENCRYPTION_KEY) { /* Encryption key */
		ii->ii_enckeylen = sadb_key->sadb_key_bits / 8;
		ii->ii_enckey = (void *)sadb_key + sizeof(struct sadb_key);
	} else {
		ii->ii_authkeylen = sadb_key->sadb_key_bits / 8;
		ii->ii_authkey = (void *)sadb_key + sizeof(struct sadb_key);
	}
}

void
export_key(void **p, struct tdb *tdb, int type)
{
	struct sadb_key *sadb_key = (struct sadb_key *) *p;

	if (type == PFKEYV2_ENCRYPTION_KEY) {
		sadb_key->sadb_key_len = (sizeof(struct sadb_key) +
		    PADUP(tdb->tdb_emxkeylen)) /
		    sizeof(uint64_t);
		sadb_key->sadb_key_bits = tdb->tdb_emxkeylen * 8;
		*p += sizeof(struct sadb_key);
		bcopy(tdb->tdb_emxkey, *p, tdb->tdb_emxkeylen);
		*p += PADUP(tdb->tdb_emxkeylen);
	} else {
		sadb_key->sadb_key_len = (sizeof(struct sadb_key) +
		    PADUP(tdb->tdb_amxkeylen)) /
		    sizeof(uint64_t);
		sadb_key->sadb_key_bits = tdb->tdb_amxkeylen * 8;
		*p += sizeof(struct sadb_key);
		bcopy(tdb->tdb_amxkey, *p, tdb->tdb_amxkeylen);
		*p += PADUP(tdb->tdb_amxkeylen);
	}
}

/* Import/Export remote port for UDP Encapsulation */
void
import_udpencap(struct tdb *tdb, struct sadb_x_udpencap *sadb_udpencap)
{
	if (sadb_udpencap)
		tdb->tdb_udpencap_port = sadb_udpencap->sadb_x_udpencap_port;
}

void
export_udpencap(void **p, struct tdb *tdb)
{
	struct sadb_x_udpencap *sadb_udpencap = (struct sadb_x_udpencap *) *p;

	sadb_udpencap->sadb_x_udpencap_port = tdb->tdb_udpencap_port;
	sadb_udpencap->sadb_x_udpencap_reserved = 0;
	sadb_udpencap->sadb_x_udpencap_len =
	    sizeof(struct sadb_x_udpencap) / sizeof(uint64_t);
	*p += sizeof(struct sadb_x_udpencap);
}

/* Export PF replay for SA */
void
export_replay(void **p, struct tdb *tdb)
{
	struct sadb_x_replay *sreplay = (struct sadb_x_replay *)*p;

	sreplay->sadb_x_replay_count = tdb->tdb_rpl;
	sreplay->sadb_x_replay_len =
	    sizeof(struct sadb_x_replay) / sizeof(uint64_t);
	*p += sizeof(struct sadb_x_replay);
}

/* Export mtu for SA */
void
export_mtu(void **p, struct tdb *tdb)
{
	struct sadb_x_mtu *smtu = (struct sadb_x_mtu *)*p;

	smtu->sadb_x_mtu_mtu = tdb->tdb_mtu;
	smtu->sadb_x_mtu_len =
	    sizeof(struct sadb_x_mtu) / sizeof(uint64_t);
	*p += sizeof(struct sadb_x_mtu);
}

/* Import rdomain switch for SA */
void
import_rdomain(struct tdb *tdb, struct sadb_x_rdomain *srdomain)
{
	if (srdomain)
		tdb->tdb_rdomain_post = srdomain->sadb_x_rdomain_dom2;
}

/* Export rdomain switch for SA */
void
export_rdomain(void **p, struct tdb *tdb)
{
	struct sadb_x_rdomain *srdomain = (struct sadb_x_rdomain *)*p;

	srdomain->sadb_x_rdomain_dom1 = tdb->tdb_rdomain;
	srdomain->sadb_x_rdomain_dom2 = tdb->tdb_rdomain_post;
	srdomain->sadb_x_rdomain_len =
	    sizeof(struct sadb_x_rdomain) / sizeof(uint64_t);
	*p += sizeof(struct sadb_x_rdomain);
}

#if NPF > 0
/* Import PF tag information for SA */
void
import_tag(struct tdb *tdb, struct sadb_x_tag *stag)
{
	char *s;

	if (stag) {
		s = (char *)(stag + 1);
		tdb->tdb_tag = pf_tagname2tag(s, 1);
	}
}

/* Export PF tag information for SA */
void
export_tag(void **p, struct tdb *tdb)
{
	struct sadb_x_tag *stag = (struct sadb_x_tag *)*p;
	char *s = (char *)(stag + 1);

	pf_tag2tagname(tdb->tdb_tag, s);

	stag->sadb_x_tag_taglen = strlen(s) + 1;
	stag->sadb_x_tag_len = (sizeof(struct sadb_x_tag) +
	    PADUP(stag->sadb_x_tag_taglen)) / sizeof(uint64_t);
	*p += sizeof(struct sadb_x_tag) + PADUP(stag->sadb_x_tag_taglen);
}

/* Import enc(4) tap device information for SA */
void
import_tap(struct tdb *tdb, struct sadb_x_tap *stap)
{
	if (stap)
		tdb->tdb_tap = stap->sadb_x_tap_unit;
}

/* Export enc(4) tap device information for SA */
void
export_tap(void **p, struct tdb *tdb)
{
	struct sadb_x_tap *stag = (struct sadb_x_tap *)*p;

	stag->sadb_x_tap_unit = tdb->tdb_tap;
	stag->sadb_x_tap_len = sizeof(struct sadb_x_tap) / sizeof(uint64_t);
	*p += sizeof(struct sadb_x_tap);
}
#endif

/* Import interface information for SA */
void
import_iface(struct tdb *tdb, struct sadb_x_iface *siface)
{
	if (siface != NULL) {
		SET(tdb->tdb_flags, TDBF_IFACE);
		tdb->tdb_iface = siface->sadb_x_iface_unit;
		tdb->tdb_iface_dir = siface->sadb_x_iface_direction;
	}
}

/* Export interface information for SA */
void
export_iface(void **p, struct tdb *tdb)
{
	struct sadb_x_iface *siface = (struct sadb_x_iface *)*p;

	siface->sadb_x_iface_len = sizeof(*siface) / sizeof(uint64_t);
	siface->sadb_x_iface_unit = tdb->tdb_iface;
	siface->sadb_x_iface_direction = tdb->tdb_iface_dir;

	*p += sizeof(*siface);
}

void
export_satype(void **p, struct tdb *tdb)
{
	struct sadb_protocol *sab = *p;

	sab->sadb_protocol_len = sizeof(struct sadb_protocol) /
	    sizeof(uint64_t);
	sab->sadb_protocol_proto = tdb->tdb_satype;
	*p += sizeof(struct sadb_protocol);
}

void
export_counter(void **p, struct tdb *tdb)
{
	uint64_t counters[tdb_ncounters];
	struct sadb_x_counter *scnt = (struct sadb_x_counter *)*p;

	counters_read(tdb->tdb_counters, counters, tdb_ncounters, NULL);

	scnt->sadb_x_counter_len = sizeof(struct sadb_x_counter) /
	    sizeof(uint64_t);
	scnt->sadb_x_counter_pad = 0;
	scnt->sadb_x_counter_ipackets = counters[tdb_ipackets];
	scnt->sadb_x_counter_opackets = counters[tdb_opackets];
	scnt->sadb_x_counter_ibytes = counters[tdb_ibytes];
	scnt->sadb_x_counter_obytes = counters[tdb_obytes];
	scnt->sadb_x_counter_idrops = counters[tdb_idrops];
	scnt->sadb_x_counter_odrops = counters[tdb_odrops];
	scnt->sadb_x_counter_idecompbytes = counters[tdb_idecompbytes];
	scnt->sadb_x_counter_ouncompbytes = counters[tdb_ouncompbytes];
	*p += sizeof(struct sadb_x_counter);
}
