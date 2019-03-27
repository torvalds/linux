/*	$FreeBSD$	*/
/*	$KAME: key_debug.c,v 1.26 2001/06/27 10:46:50 sakane Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef _KERNEL
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"
#endif

#include <sys/param.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#endif
#include <sys/socket.h>

#include <net/vnet.h>

#include <netipsec/key_var.h>
#include <netipsec/key_debug.h>

#include <netinet/in.h>
#include <netipsec/ipsec.h>
#ifdef _KERNEL
#include <netipsec/keydb.h>
#include <netipsec/xform.h>
#endif

#ifndef _KERNEL
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#endif /* !_KERNEL */

static void kdebug_sadb_prop(struct sadb_ext *);
static void kdebug_sadb_identity(struct sadb_ext *);
static void kdebug_sadb_supported(struct sadb_ext *);
static void kdebug_sadb_lifetime(struct sadb_ext *);
static void kdebug_sadb_sa(struct sadb_ext *);
static void kdebug_sadb_address(struct sadb_ext *);
static void kdebug_sadb_key(struct sadb_ext *);
static void kdebug_sadb_x_sa2(struct sadb_ext *);
static void kdebug_sadb_x_sa_replay(struct sadb_ext *);
static void kdebug_sadb_x_natt(struct sadb_ext *);

#ifndef _KERNEL
#define panic(fmt, ...)	{ printf(fmt, ## __VA_ARGS__); exit(-1); }
#endif

/* NOTE: host byte order */

static const char*
kdebug_sadb_type(uint8_t type)
{
#define	SADB_NAME(n)	case SADB_ ## n: return (#n)

	switch (type) {
	SADB_NAME(RESERVED);
	SADB_NAME(GETSPI);
	SADB_NAME(UPDATE);
	SADB_NAME(ADD);
	SADB_NAME(DELETE);
	SADB_NAME(GET);
	SADB_NAME(ACQUIRE);
	SADB_NAME(REGISTER);
	SADB_NAME(EXPIRE);
	SADB_NAME(FLUSH);
	SADB_NAME(DUMP);
	SADB_NAME(X_PROMISC);
	SADB_NAME(X_PCHANGE);
	SADB_NAME(X_SPDUPDATE);
	SADB_NAME(X_SPDADD);
	SADB_NAME(X_SPDDELETE);
	SADB_NAME(X_SPDGET);
	SADB_NAME(X_SPDACQUIRE);
	SADB_NAME(X_SPDDUMP);
	SADB_NAME(X_SPDFLUSH);
	SADB_NAME(X_SPDSETIDX);
	SADB_NAME(X_SPDEXPIRE);
	SADB_NAME(X_SPDDELETE2);
	default:
		return ("UNKNOWN");
	}
#undef SADB_NAME
}

static const char*
kdebug_sadb_exttype(uint16_t type)
{
#define	EXT_NAME(n)	case SADB_EXT_ ## n: return (#n)
#define	X_NAME(n)	case SADB_X_EXT_ ## n: return (#n)

	switch (type) {
	EXT_NAME(RESERVED);
	EXT_NAME(SA);
	EXT_NAME(LIFETIME_CURRENT);
	EXT_NAME(LIFETIME_HARD);
	EXT_NAME(LIFETIME_SOFT);
	EXT_NAME(ADDRESS_SRC);
	EXT_NAME(ADDRESS_DST);
	EXT_NAME(ADDRESS_PROXY);
	EXT_NAME(KEY_AUTH);
	EXT_NAME(KEY_ENCRYPT);
	EXT_NAME(IDENTITY_SRC);
	EXT_NAME(IDENTITY_DST);
	EXT_NAME(SENSITIVITY);
	EXT_NAME(PROPOSAL);
	EXT_NAME(SUPPORTED_AUTH);
	EXT_NAME(SUPPORTED_ENCRYPT);
	EXT_NAME(SPIRANGE);
	X_NAME(KMPRIVATE);
	X_NAME(POLICY);
	X_NAME(SA2);
	X_NAME(NAT_T_TYPE);
	X_NAME(NAT_T_SPORT);
	X_NAME(NAT_T_DPORT);
	X_NAME(NAT_T_OAI);
	X_NAME(NAT_T_OAR);
	X_NAME(NAT_T_FRAG);
	X_NAME(SA_REPLAY);
	X_NAME(NEW_ADDRESS_SRC);
	X_NAME(NEW_ADDRESS_DST);
	default:
		return ("UNKNOWN");
	};
#undef EXT_NAME
#undef X_NAME
}


/* %%%: about struct sadb_msg */
void
kdebug_sadb(struct sadb_msg *base)
{
	struct sadb_ext *ext;
	int tlen, extlen;

	/* sanity check */
	if (base == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	printf("sadb_msg{ version=%u type=%u(%s) errno=%u satype=%u\n",
	    base->sadb_msg_version, base->sadb_msg_type,
	    kdebug_sadb_type(base->sadb_msg_type),
	    base->sadb_msg_errno, base->sadb_msg_satype);
	printf("  len=%u reserved=%u seq=%u pid=%u\n",
	    base->sadb_msg_len, base->sadb_msg_reserved,
	    base->sadb_msg_seq, base->sadb_msg_pid);

	tlen = PFKEY_UNUNIT64(base->sadb_msg_len) - sizeof(struct sadb_msg);
	ext = (struct sadb_ext *)((caddr_t)base + sizeof(struct sadb_msg));

	while (tlen > 0) {
		printf("sadb_ext{ len=%u type=%u(%s) }\n",
		    ext->sadb_ext_len, ext->sadb_ext_type,
		    kdebug_sadb_exttype(ext->sadb_ext_type));

		if (ext->sadb_ext_len == 0) {
			printf("%s: invalid ext_len=0 was passed.\n", __func__);
			return;
		}
		if (ext->sadb_ext_len > tlen) {
			printf("%s: ext_len too big (%u > %u).\n",
				__func__, ext->sadb_ext_len, tlen);
			return;
		}

		switch (ext->sadb_ext_type) {
		case SADB_EXT_SA:
			kdebug_sadb_sa(ext);
			break;
		case SADB_EXT_LIFETIME_CURRENT:
		case SADB_EXT_LIFETIME_HARD:
		case SADB_EXT_LIFETIME_SOFT:
			kdebug_sadb_lifetime(ext);
			break;
		case SADB_EXT_ADDRESS_SRC:
		case SADB_EXT_ADDRESS_DST:
		case SADB_EXT_ADDRESS_PROXY:
		case SADB_X_EXT_NAT_T_OAI:
		case SADB_X_EXT_NAT_T_OAR:
		case SADB_X_EXT_NEW_ADDRESS_SRC:
		case SADB_X_EXT_NEW_ADDRESS_DST:
			kdebug_sadb_address(ext);
			break;
		case SADB_EXT_KEY_AUTH:
		case SADB_EXT_KEY_ENCRYPT:
			kdebug_sadb_key(ext);
			break;
		case SADB_EXT_IDENTITY_SRC:
		case SADB_EXT_IDENTITY_DST:
			kdebug_sadb_identity(ext);
			break;
		case SADB_EXT_SENSITIVITY:
			break;
		case SADB_EXT_PROPOSAL:
			kdebug_sadb_prop(ext);
			break;
		case SADB_EXT_SUPPORTED_AUTH:
		case SADB_EXT_SUPPORTED_ENCRYPT:
			kdebug_sadb_supported(ext);
			break;
		case SADB_EXT_SPIRANGE:
		case SADB_X_EXT_KMPRIVATE:
			break;
		case SADB_X_EXT_POLICY:
			kdebug_sadb_x_policy(ext);
			break;
		case SADB_X_EXT_SA2:
			kdebug_sadb_x_sa2(ext);
			break;
		case SADB_X_EXT_SA_REPLAY:
			kdebug_sadb_x_sa_replay(ext);
			break;
		case SADB_X_EXT_NAT_T_TYPE:
		case SADB_X_EXT_NAT_T_SPORT:
		case SADB_X_EXT_NAT_T_DPORT:
			kdebug_sadb_x_natt(ext);
			break;
		default:
			printf("%s: invalid ext_type %u\n", __func__,
			    ext->sadb_ext_type);
			return;
		}

		extlen = PFKEY_UNUNIT64(ext->sadb_ext_len);
		tlen -= extlen;
		ext = (struct sadb_ext *)((caddr_t)ext + extlen);
	}

	return;
}

static void
kdebug_sadb_prop(struct sadb_ext *ext)
{
	struct sadb_prop *prop = (struct sadb_prop *)ext;
	struct sadb_comb *comb;
	int len;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	len = (PFKEY_UNUNIT64(prop->sadb_prop_len) - sizeof(*prop))
		/ sizeof(*comb);
	comb = (struct sadb_comb *)(prop + 1);
	printf("sadb_prop{ replay=%u\n", prop->sadb_prop_replay);

	while (len--) {
		printf("sadb_comb{ auth=%u encrypt=%u "
			"flags=0x%04x reserved=0x%08x\n",
			comb->sadb_comb_auth, comb->sadb_comb_encrypt,
			comb->sadb_comb_flags, comb->sadb_comb_reserved);

		printf("  auth_minbits=%u auth_maxbits=%u "
			"encrypt_minbits=%u encrypt_maxbits=%u\n",
			comb->sadb_comb_auth_minbits,
			comb->sadb_comb_auth_maxbits,
			comb->sadb_comb_encrypt_minbits,
			comb->sadb_comb_encrypt_maxbits);

		printf("  soft_alloc=%u hard_alloc=%u "
			"soft_bytes=%lu hard_bytes=%lu\n",
			comb->sadb_comb_soft_allocations,
			comb->sadb_comb_hard_allocations,
			(unsigned long)comb->sadb_comb_soft_bytes,
			(unsigned long)comb->sadb_comb_hard_bytes);

		printf("  soft_alloc=%lu hard_alloc=%lu "
			"soft_bytes=%lu hard_bytes=%lu }\n",
			(unsigned long)comb->sadb_comb_soft_addtime,
			(unsigned long)comb->sadb_comb_hard_addtime,
			(unsigned long)comb->sadb_comb_soft_usetime,
			(unsigned long)comb->sadb_comb_hard_usetime);
		comb++;
	}
	printf("}\n");

	return;
}

static void
kdebug_sadb_identity(struct sadb_ext *ext)
{
	struct sadb_ident *id = (struct sadb_ident *)ext;
	int len;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	len = PFKEY_UNUNIT64(id->sadb_ident_len) - sizeof(*id);
	printf("sadb_ident_%s{",
	    id->sadb_ident_exttype == SADB_EXT_IDENTITY_SRC ? "src" : "dst");
	switch (id->sadb_ident_type) {
	default:
		printf(" type=%d id=%lu",
			id->sadb_ident_type, (u_long)id->sadb_ident_id);
		if (len) {
#ifdef _KERNEL
			ipsec_hexdump((caddr_t)(id + 1), len); /*XXX cast ?*/
#else
			char *p, *ep;
			printf("\n  str=\"");
			p = (char *)(id + 1);
			ep = p + len;
			for (/*nothing*/; *p && p < ep; p++) {
				if (isprint(*p))
					printf("%c", *p & 0xff);
				else
					printf("\\%03o", *p & 0xff);
			}
#endif
			printf("\"");
		}
		break;
	}

	printf(" }\n");

	return;
}

static void
kdebug_sadb_supported(struct sadb_ext *ext)
{
	struct sadb_supported *sup = (struct sadb_supported *)ext;
	struct sadb_alg *alg;
	int len;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	len = (PFKEY_UNUNIT64(sup->sadb_supported_len) - sizeof(*sup))
		/ sizeof(*alg);
	alg = (struct sadb_alg *)(sup + 1);
	printf("sadb_sup{\n");
	while (len--) {
		printf("  { id=%d ivlen=%d min=%d max=%d }\n",
			alg->sadb_alg_id, alg->sadb_alg_ivlen,
			alg->sadb_alg_minbits, alg->sadb_alg_maxbits);
		alg++;
	}
	printf("}\n");

	return;
}

static void
kdebug_sadb_lifetime(struct sadb_ext *ext)
{
	struct sadb_lifetime *lft = (struct sadb_lifetime *)ext;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	printf("sadb_lifetime{ alloc=%u, bytes=%u\n",
		lft->sadb_lifetime_allocations,
		(u_int32_t)lft->sadb_lifetime_bytes);
	printf("  addtime=%u, usetime=%u }\n",
		(u_int32_t)lft->sadb_lifetime_addtime,
		(u_int32_t)lft->sadb_lifetime_usetime);

	return;
}

static void
kdebug_sadb_sa(struct sadb_ext *ext)
{
	struct sadb_sa *sa = (struct sadb_sa *)ext;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	printf("sadb_sa{ spi=%u replay=%u state=%u\n",
	    (u_int32_t)ntohl(sa->sadb_sa_spi), sa->sadb_sa_replay,
	    sa->sadb_sa_state);
	printf("  auth=%u encrypt=%u flags=0x%08x }\n",
	    sa->sadb_sa_auth, sa->sadb_sa_encrypt, sa->sadb_sa_flags);

	return;
}

static void
kdebug_sadb_address(struct sadb_ext *ext)
{
	struct sadb_address *addr = (struct sadb_address *)ext;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	printf("sadb_address{ proto=%u prefixlen=%u reserved=0x%02x%02x }\n",
	    addr->sadb_address_proto, addr->sadb_address_prefixlen,
	    ((u_char *)&addr->sadb_address_reserved)[0],
	    ((u_char *)&addr->sadb_address_reserved)[1]);

	kdebug_sockaddr((struct sockaddr *)((caddr_t)ext + sizeof(*addr)));
}

static void
kdebug_sadb_key(struct sadb_ext *ext)
{
	struct sadb_key *key = (struct sadb_key *)ext;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	printf("sadb_key{ bits=%u reserved=%u\n",
	    key->sadb_key_bits, key->sadb_key_reserved);
	printf("  key=");

	/* sanity check 2 */
	if ((key->sadb_key_bits >> 3) >
		(PFKEY_UNUNIT64(key->sadb_key_len) - sizeof(struct sadb_key))) {
		printf("%s: key length mismatch, bit:%d len:%ld.\n",
			__func__,
			key->sadb_key_bits >> 3,
			(long)PFKEY_UNUNIT64(key->sadb_key_len) - sizeof(struct sadb_key));
	}

	ipsec_hexdump((caddr_t)key + sizeof(struct sadb_key),
	              key->sadb_key_bits >> 3);
	printf(" }\n");
	return;
}

static void
kdebug_sadb_x_sa2(struct sadb_ext *ext)
{
	struct sadb_x_sa2 *sa2 = (struct sadb_x_sa2 *)ext;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	printf("sadb_x_sa2{ mode=%u reqid=%u\n",
	    sa2->sadb_x_sa2_mode, sa2->sadb_x_sa2_reqid);
	printf("  reserved1=%u reserved2=%u sequence=%u }\n",
	    sa2->sadb_x_sa2_reserved1, sa2->sadb_x_sa2_reserved2,
	    sa2->sadb_x_sa2_sequence);

	return;
}

static void
kdebug_sadb_x_sa_replay(struct sadb_ext *ext)
{
	struct sadb_x_sa_replay *replay;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	replay = (struct sadb_x_sa_replay *)ext;
	printf("sadb_x_sa_replay{ replay=%u }\n",
	    replay->sadb_x_sa_replay_replay);
}

static void
kdebug_sadb_x_natt(struct sadb_ext *ext)
{
	struct sadb_x_nat_t_type *type;
	struct sadb_x_nat_t_port *port;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	if (ext->sadb_ext_type == SADB_X_EXT_NAT_T_TYPE) {
		type = (struct sadb_x_nat_t_type *)ext;
		printf("sadb_x_nat_t_type{ type=%u }\n",
		    type->sadb_x_nat_t_type_type);
	} else {
		port = (struct sadb_x_nat_t_port *)ext;
		printf("sadb_x_nat_t_port{ port=%u }\n",
		    ntohs(port->sadb_x_nat_t_port_port));
	}
}

void
kdebug_sadb_x_policy(struct sadb_ext *ext)
{
	struct sadb_x_policy *xpl = (struct sadb_x_policy *)ext;
	struct sockaddr *addr;

	/* sanity check */
	if (ext == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	printf("sadb_x_policy{ type=%u dir=%u id=%x scope=%u %s=%u }\n",
		xpl->sadb_x_policy_type, xpl->sadb_x_policy_dir,
		xpl->sadb_x_policy_id, xpl->sadb_x_policy_scope,
		xpl->sadb_x_policy_scope == IPSEC_POLICYSCOPE_IFNET ?
		"ifindex": "priority", xpl->sadb_x_policy_priority);

	if (xpl->sadb_x_policy_type == IPSEC_POLICY_IPSEC) {
		int tlen;
		struct sadb_x_ipsecrequest *xisr;

		tlen = PFKEY_UNUNIT64(xpl->sadb_x_policy_len) - sizeof(*xpl);
		xisr = (struct sadb_x_ipsecrequest *)(xpl + 1);

		while (tlen > 0) {
			printf(" { len=%u proto=%u mode=%u level=%u reqid=%u\n",
				xisr->sadb_x_ipsecrequest_len,
				xisr->sadb_x_ipsecrequest_proto,
				xisr->sadb_x_ipsecrequest_mode,
				xisr->sadb_x_ipsecrequest_level,
				xisr->sadb_x_ipsecrequest_reqid);

			if (xisr->sadb_x_ipsecrequest_len > sizeof(*xisr)) {
				addr = (struct sockaddr *)(xisr + 1);
				kdebug_sockaddr(addr);
				addr = (struct sockaddr *)((caddr_t)addr
							+ addr->sa_len);
				kdebug_sockaddr(addr);
			}

			printf(" }\n");

			/* prevent infinite loop */
			if (xisr->sadb_x_ipsecrequest_len <= 0) {
				printf("%s: wrong policy struct.\n", __func__);
				return;
			}
			/* prevent overflow */
			if (xisr->sadb_x_ipsecrequest_len > tlen) {
				printf("%s: invalid ipsec policy length "
					"(%u > %u)\n", __func__,
					xisr->sadb_x_ipsecrequest_len, tlen);
				return;
			}

			tlen -= xisr->sadb_x_ipsecrequest_len;

			xisr = (struct sadb_x_ipsecrequest *)((caddr_t)xisr
			                + xisr->sadb_x_ipsecrequest_len);
		}

		if (tlen != 0)
			panic("%s: wrong policy struct.\n", __func__);
	}

	return;
}

#ifdef _KERNEL
/* %%%: about SPD and SAD */
const char*
kdebug_secpolicy_state(u_int state)
{

	switch (state) {
	case IPSEC_SPSTATE_DEAD:
		return ("dead");
	case IPSEC_SPSTATE_LARVAL:
		return ("larval");
	case IPSEC_SPSTATE_ALIVE:
		return ("alive");
	case IPSEC_SPSTATE_PCB:
		return ("pcb");
	case IPSEC_SPSTATE_IFNET:
		return ("ifnet");
	}
	return ("unknown");
}

const char*
kdebug_secpolicy_policy(u_int policy)
{

	switch (policy) {
	case IPSEC_POLICY_DISCARD:
		return ("discard");
	case IPSEC_POLICY_NONE:
		return ("none");
	case IPSEC_POLICY_IPSEC:
		return ("ipsec");
	case IPSEC_POLICY_ENTRUST:
		return ("entrust");
	case IPSEC_POLICY_BYPASS:
		return ("bypass");
	}
	return ("unknown");
}

const char*
kdebug_secpolicyindex_dir(u_int dir)
{

	switch (dir) {
	case IPSEC_DIR_ANY:
		return ("any");
	case IPSEC_DIR_INBOUND:
		return ("in");
	case IPSEC_DIR_OUTBOUND:
		return ("out");
	}
	return ("unknown");
}

const char*
kdebug_ipsecrequest_level(u_int level)
{

	switch (level) {
	case IPSEC_LEVEL_DEFAULT:
		return ("default");
	case IPSEC_LEVEL_USE:
		return ("use");
	case IPSEC_LEVEL_REQUIRE:
		return ("require");
	case IPSEC_LEVEL_UNIQUE:
		return ("unique");
	}
	return ("unknown");
}

const char*
kdebug_secasindex_mode(u_int mode)
{

	switch (mode) {
	case IPSEC_MODE_ANY:
		return ("any");
	case IPSEC_MODE_TRANSPORT:
		return ("transport");
	case IPSEC_MODE_TUNNEL:
		return ("tunnel");
	case IPSEC_MODE_TCPMD5:
		return ("tcp-md5");
	}
	return ("unknown");
}

const char*
kdebug_secasv_state(u_int state)
{

	switch (state) {
	case SADB_SASTATE_LARVAL:
		return ("larval");
	case SADB_SASTATE_MATURE:
		return ("mature");
	case SADB_SASTATE_DYING:
		return ("dying");
	case SADB_SASTATE_DEAD:
		return ("dead");
	}
	return ("unknown");
}

static char*
kdebug_port2str(const struct sockaddr *sa, char *buf, size_t len)
{
	uint16_t port;

	IPSEC_ASSERT(sa != NULL, ("null sa"));
	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		port = ntohs(((const struct sockaddr_in *)sa)->sin_port);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		port = ntohs(((const struct sockaddr_in6 *)sa)->sin6_port);
		break;
#endif
	default:
		port = 0;
	}
	if (port == 0)
		return ("*");
	snprintf(buf, len, "%u", port);
	return (buf);
}

void
kdebug_secpolicy(struct secpolicy *sp)
{
	u_int idx;

	IPSEC_ASSERT(sp != NULL, ("null sp"));
	printf("SP { refcnt=%u id=%u priority=%u state=%s policy=%s\n",
	    sp->refcnt, sp->id, sp->priority,
	    kdebug_secpolicy_state(sp->state),
	    kdebug_secpolicy_policy(sp->policy));
	kdebug_secpolicyindex(&sp->spidx, "  ");
	for (idx = 0; idx < sp->tcount; idx++) {
		printf("  req[%u]{ level=%s ", idx,
		    kdebug_ipsecrequest_level(sp->req[idx]->level));
		kdebug_secasindex(&sp->req[idx]->saidx, NULL);
		printf("  }\n");
	}
	printf("}\n");
}

void
kdebug_secpolicyindex(struct secpolicyindex *spidx, const char *indent)
{
	char buf[IPSEC_ADDRSTRLEN];

	IPSEC_ASSERT(spidx != NULL, ("null spidx"));
	if (indent != NULL)
		printf("%s", indent);
	printf("spidx { dir=%s ul_proto=",
	    kdebug_secpolicyindex_dir(spidx->dir));
	if (spidx->ul_proto == IPSEC_ULPROTO_ANY)
		printf("* ");
	else
		printf("%u ", spidx->ul_proto);
	printf("%s/%u -> ", ipsec_address(&spidx->src, buf, sizeof(buf)),
	    spidx->prefs);
	printf("%s/%u }\n", ipsec_address(&spidx->dst, buf, sizeof(buf)),
	    spidx->prefd);
}

void
kdebug_secasindex(const struct secasindex *saidx, const char *indent)
{
	char buf[IPSEC_ADDRSTRLEN], port[6];

	IPSEC_ASSERT(saidx != NULL, ("null saidx"));
	if (indent != NULL)
		printf("%s", indent);
	printf("saidx { mode=%s proto=%u reqid=%u ",
	    kdebug_secasindex_mode(saidx->mode), saidx->proto, saidx->reqid);
	printf("%s:%s -> ", ipsec_address(&saidx->src, buf, sizeof(buf)),
	    kdebug_port2str(&saidx->src.sa, port, sizeof(port)));
	printf("%s:%s }\n", ipsec_address(&saidx->dst, buf, sizeof(buf)),
	    kdebug_port2str(&saidx->dst.sa, port, sizeof(port)));
}

static void
kdebug_sec_lifetime(struct seclifetime *lft, const char *indent)
{

	IPSEC_ASSERT(lft != NULL, ("null lft"));
	if (indent != NULL)
		printf("%s", indent);
	printf("lifetime { alloc=%u, bytes=%ju addtime=%ju usetime=%ju }\n",
	    lft->allocations, (uintmax_t)lft->bytes, (uintmax_t)lft->addtime,
	    (uintmax_t)lft->usetime);
}

void
kdebug_secash(struct secashead *sah, const char *indent)
{

	IPSEC_ASSERT(sah != NULL, ("null sah"));
	if (indent != NULL)
		printf("%s", indent);
	printf("SAH { refcnt=%u state=%s\n", sah->refcnt,
	    kdebug_secasv_state(sah->state));
	if (indent != NULL)
		printf("%s", indent);
	kdebug_secasindex(&sah->saidx, indent);
	if (indent != NULL)
		printf("%s", indent);
	printf("}\n");
}

#ifdef IPSEC_DEBUG
static void
kdebug_secreplay(struct secreplay *rpl)
{
	int len, l;

	IPSEC_ASSERT(rpl != NULL, ("null rpl"));
	printf(" secreplay{ count=%u bitmap_size=%u wsize=%u seq=%u lastseq=%u",
	    rpl->count, rpl->bitmap_size, rpl->wsize, rpl->seq, rpl->lastseq);

	if (rpl->bitmap == NULL) {
		printf("  }\n");
		return;
	}

	printf("\n    bitmap { ");
	for (len = 0; len < rpl->bitmap_size*4; len++) {
		for (l = 7; l >= 0; l--)
			printf("%u", (((rpl->bitmap)[len] >> l) & 1) ? 1 : 0);
	}
	printf("    }\n");
}
#endif /* IPSEC_DEBUG */

static void
kdebug_secnatt(struct secnatt *natt)
{
	char buf[IPSEC_ADDRSTRLEN];

	IPSEC_ASSERT(natt != NULL, ("null natt"));
	printf("  natt{ sport=%u dport=%u ", ntohs(natt->sport),
	    ntohs(natt->dport));
	if (natt->flags & IPSEC_NATT_F_OAI)
		printf("oai=%s ", ipsec_address(&natt->oai, buf, sizeof(buf)));
	if (natt->flags & IPSEC_NATT_F_OAR)
		printf("oar=%s ", ipsec_address(&natt->oar, buf, sizeof(buf)));
	printf("}\n");
}

void
kdebug_secasv(struct secasvar *sav)
{
	struct seclifetime lft_c;

	IPSEC_ASSERT(sav != NULL, ("null sav"));

	printf("SA { refcnt=%u spi=%u seq=%u pid=%u flags=0x%x state=%s\n",
	    sav->refcnt, ntohl(sav->spi), sav->seq, (uint32_t)sav->pid,
	    sav->flags, kdebug_secasv_state(sav->state));
	kdebug_secash(sav->sah, "  ");

	lft_c.addtime = sav->created;
	lft_c.allocations = (uint32_t)counter_u64_fetch(
	    sav->lft_c_allocations);
	lft_c.bytes = counter_u64_fetch(sav->lft_c_bytes);
	lft_c.usetime = sav->firstused;
	kdebug_sec_lifetime(&lft_c, "  c_");
	if (sav->lft_h != NULL)
		kdebug_sec_lifetime(sav->lft_h, "  h_");
	if (sav->lft_s != NULL)
		kdebug_sec_lifetime(sav->lft_s, "  s_");

	if (sav->tdb_authalgxform != NULL)
		printf("  alg_auth=%s\n", sav->tdb_authalgxform->name);
	if (sav->key_auth != NULL)
		KEYDBG(DUMP,
		    kdebug_sadb_key((struct sadb_ext *)sav->key_auth));
	if (sav->tdb_encalgxform != NULL)
		printf("  alg_enc=%s\n", sav->tdb_encalgxform->name);
	if (sav->key_enc != NULL)
		KEYDBG(DUMP,
		    kdebug_sadb_key((struct sadb_ext *)sav->key_enc));
	if (sav->natt != NULL)
		kdebug_secnatt(sav->natt);
	if (sav->replay != NULL) {
		KEYDBG(DUMP,
		    SECASVAR_LOCK(sav);
		    kdebug_secreplay(sav->replay);
		    SECASVAR_UNLOCK(sav));
	}
	printf("}\n");
}

void
kdebug_mbufhdr(const struct mbuf *m)
{
	/* sanity check */
	if (m == NULL)
		return;

	printf("mbuf(%p){ m_next:%p m_nextpkt:%p m_data:%p "
	       "m_len:%d m_type:0x%02x m_flags:0x%02x }\n",
		m, m->m_next, m->m_nextpkt, m->m_data,
		m->m_len, m->m_type, m->m_flags);

	if (m->m_flags & M_PKTHDR) {
		printf("  m_pkthdr{ len:%d rcvif:%p }\n",
		    m->m_pkthdr.len, m->m_pkthdr.rcvif);
	}

	if (m->m_flags & M_EXT) {
		printf("  m_ext{ ext_buf:%p ext_free:%p "
		       "ext_size:%u ext_cnt:%p }\n",
			m->m_ext.ext_buf, m->m_ext.ext_free,
			m->m_ext.ext_size, m->m_ext.ext_cnt);
	}

	return;
}

void
kdebug_mbuf(const struct mbuf *m0)
{
	const struct mbuf *m = m0;
	int i, j;

	for (j = 0; m; m = m->m_next) {
		kdebug_mbufhdr(m);
		printf("  m_data:\n");
		for (i = 0; i < m->m_len; i++) {
			if (i && i % 32 == 0)
				printf("\n");
			if (i % 4 == 0)
				printf(" ");
			printf("%02x", mtod(m, const u_char *)[i]);
			j++;
		}
		printf("\n");
	}

	return;
}

/* Return a printable string for the address. */
char *
ipsec_address(const union sockaddr_union* sa, char *buf, socklen_t size)
{

	switch (sa->sa.sa_family) {
#ifdef INET
	case AF_INET:
		return (inet_ntop(AF_INET, &sa->sin.sin_addr, buf, size));
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		if (IN6_IS_SCOPE_LINKLOCAL(&sa->sin6.sin6_addr)) {
			snprintf(buf, size, "%s%%%u", inet_ntop(AF_INET6,
			    &sa->sin6.sin6_addr, buf, size),
			    sa->sin6.sin6_scope_id);
			return (buf);
		} else
			return (inet_ntop(AF_INET6, &sa->sin6.sin6_addr,
			    buf, size));
#endif /* INET6 */
	case 0:
		return ("*");
	default:
		return ("(unknown address family)");
	}
}

char *
ipsec_sa2str(struct secasvar *sav, char *buf, size_t size)
{
	char sbuf[IPSEC_ADDRSTRLEN], dbuf[IPSEC_ADDRSTRLEN];

	snprintf(buf, size, "SA(SPI=%08lx src=%s dst=%s)",
	    (u_long)ntohl(sav->spi),
	    ipsec_address(&sav->sah->saidx.src, sbuf, sizeof(sbuf)),
	    ipsec_address(&sav->sah->saidx.dst, dbuf, sizeof(dbuf)));
	return (buf);
}

#endif /* _KERNEL */

void
kdebug_sockaddr(struct sockaddr *addr)
{
	char buf[IPSEC_ADDRSTRLEN];

	/* sanity check */
	if (addr == NULL)
		panic("%s: NULL pointer was passed.\n", __func__);

	switch (addr->sa_family) {
#ifdef INET
	case AF_INET: {
		struct sockaddr_in *sin;

		sin = (struct sockaddr_in *)addr;
		inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
		break;
	}
#endif
#ifdef INET6
	case AF_INET6: {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)addr;
		if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
			snprintf(buf, sizeof(buf), "%s%%%u",
			    inet_ntop(AF_INET6, &sin6->sin6_addr, buf,
			    sizeof(buf)), sin6->sin6_scope_id);
		} else
			inet_ntop(AF_INET6, &sin6->sin6_addr, buf,
			    sizeof(buf));
		break;
	}
#endif
	default:
		sprintf(buf, "unknown");
	}
	printf("sockaddr{ len=%u family=%u addr=%s }\n", addr->sa_len,
	    addr->sa_family, buf);
}

void
ipsec_bindump(caddr_t buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		printf("%c", (unsigned char)buf[i]);

	return;
}


void
ipsec_hexdump(caddr_t buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (i != 0 && i % 32 == 0) printf("\n");
		if (i % 4 == 0) printf(" ");
		printf("%02x", (unsigned char)buf[i]);
	}
#if 0
	if (i % 32 != 0) printf("\n");
#endif

	return;
}
