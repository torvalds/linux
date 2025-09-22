/*	$OpenBSD: pfkdump.c,v 1.58 2023/10/10 16:16:16 tobhe Exp $	*/

/*
 * Copyright (c) 2003 Markus Friedl.  All rights reserved.
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

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/queue.h>

#include <net/pfkeyv2.h>
#include <netinet/ip_ipsp.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>

#include "ipsecctl.h"
#include "pfkey.h"

static void	print_proto(struct sadb_ext *, struct sadb_msg *, int);
static void	print_flow(struct sadb_ext *, struct sadb_msg *, int);
static void	print_supp(struct sadb_ext *, struct sadb_msg *, int);
static void	print_prop(struct sadb_ext *, struct sadb_msg *, int);
static void	print_sens(struct sadb_ext *, struct sadb_msg *, int);
static void	print_spir(struct sadb_ext *, struct sadb_msg *, int);
static void	print_policy(struct sadb_ext *, struct sadb_msg *, int);
static void	print_sa(struct sadb_ext *, struct sadb_msg *, int);
static void	print_addr(struct sadb_ext *, struct sadb_msg *, int);
static void	print_key(struct sadb_ext *, struct sadb_msg *, int);
static void	print_life(struct sadb_ext *, struct sadb_msg *, int);
static void	print_ident(struct sadb_ext *, struct sadb_msg *, int);
static void	print_udpenc(struct sadb_ext *, struct sadb_msg *, int);
static void	print_tag(struct sadb_ext *, struct sadb_msg *, int);
static void	print_rdomain(struct sadb_ext *, struct sadb_msg *, int);
static void	print_replay(struct sadb_ext *, struct sadb_msg *, int);
static void	print_mtu(struct sadb_ext *, struct sadb_msg *, int);
static void	print_tap(struct sadb_ext *, struct sadb_msg *, int);
static void	print_satype(struct sadb_ext *, struct sadb_msg *, int);
static void	print_counter(struct sadb_ext *, struct sadb_msg *, int);
static void	print_iface(struct sadb_ext *, struct sadb_msg *, int);

static struct idname *lookup(struct idname *, u_int32_t);
static char    *lookup_name(struct idname *, u_int32_t);
static void	print_ext(struct sadb_ext *, struct sadb_msg *, int);

void		pfkey_print_raw(u_int8_t *, ssize_t);
static char	*print_flags(uint32_t);

struct sadb_ext *extensions[SADB_EXT_MAX + 1];

struct idname {
	u_int32_t id;
	char *name;
	void (*func)(struct sadb_ext *, struct sadb_msg *, int);
};

struct idname ext_types[] = {
	{ SADB_EXT_RESERVED,		"reserved",		NULL },
	{ SADB_EXT_SA,			"sa",			print_sa },
	{ SADB_EXT_LIFETIME_CURRENT,	"lifetime_cur",		print_life },
	{ SADB_EXT_LIFETIME_HARD,	"lifetime_hard",	print_life },
	{ SADB_EXT_LIFETIME_SOFT,	"lifetime_soft",	print_life },
	{ SADB_EXT_ADDRESS_SRC,		"address_src",		print_addr },
	{ SADB_EXT_ADDRESS_DST,		"address_dst",		print_addr },
	{ SADB_EXT_ADDRESS_PROXY,	"address_proxy",	print_addr },
	{ SADB_EXT_KEY_AUTH,		"key_auth",		print_key },
	{ SADB_EXT_KEY_ENCRYPT,		"key_encrypt",		print_key },
	{ SADB_EXT_IDENTITY_SRC,	"identity_src",		print_ident },
	{ SADB_EXT_IDENTITY_DST,	"identity_dst",		print_ident },
	{ SADB_EXT_SENSITIVITY,		"sensitivity",		print_sens },
	{ SADB_EXT_PROPOSAL,		"proposal",		print_prop },
	{ SADB_EXT_SUPPORTED_AUTH,	"supported_auth",	print_supp },
	{ SADB_EXT_SUPPORTED_ENCRYPT,	"supported_encrypt",	print_supp },
	{ SADB_EXT_SPIRANGE,		"spirange",		print_spir },
	{ SADB_X_EXT_SRC_MASK,		"src_mask",		print_addr },
	{ SADB_X_EXT_DST_MASK,		"dst_mask",		print_addr },
	{ SADB_X_EXT_PROTOCOL,		"protocol",		print_proto },
	{ SADB_X_EXT_FLOW_TYPE,		"flow_type",		print_flow },
	{ SADB_X_EXT_SRC_FLOW,		"src_flow",		print_addr },
	{ SADB_X_EXT_DST_FLOW,		"dst_flow",		print_addr },
	{ SADB_X_EXT_SA2,		"sa2",			print_sa },
	{ SADB_X_EXT_DST2,		"dst2",			print_addr },
	{ SADB_X_EXT_POLICY,		"policy",		print_policy },
	{ SADB_X_EXT_SUPPORTED_COMP,	"supported_comp",	print_supp },
	{ SADB_X_EXT_UDPENCAP,		"udpencap",		print_udpenc },
	{ SADB_X_EXT_LIFETIME_LASTUSE,	"lifetime_lastuse",	print_life },
	{ SADB_X_EXT_TAG,		"tag",			print_tag },
	{ SADB_X_EXT_RDOMAIN,		"rdomain",		print_rdomain },
	{ SADB_X_EXT_REPLAY,		"replay",		print_replay },
	{ SADB_X_EXT_MTU,		"mtu",			print_mtu },
	{ SADB_X_EXT_TAP,		"tap",			print_tap },
	{ SADB_X_EXT_SATYPE2,		"satype2",		print_satype },
	{ SADB_X_EXT_COUNTER,		"counter",		print_counter },
	{ SADB_X_EXT_IFACE,		"interface",		print_iface },
	{ 0,				NULL,			NULL }
};

struct idname msg_types[] = {
	{ SADB_ACQUIRE,			"sadb_acquire",		NULL },
	{ SADB_ADD,			"sadb_add",		NULL },
	{ SADB_DELETE,			"sadb_delete",		NULL },
	{ SADB_DUMP,			"sadb_dump",		NULL },
	{ SADB_EXPIRE,			"sadb_expire",		NULL },
	{ SADB_FLUSH,			"sadb_flush",		NULL },
	{ SADB_GET,			"sadb_get",		NULL },
	{ SADB_GETSPI,			"sadb_getspi",		NULL },
	{ SADB_REGISTER,		"sadb_register",	NULL },
	{ SADB_UPDATE,			"sadb_update",		NULL },
	{ SADB_X_ADDFLOW,		"sadb_addflow",		NULL },
	{ SADB_X_ASKPOLICY,		"sadb_askpolicy",	NULL },
	{ SADB_X_DELFLOW,		"sadb_delflow",		NULL },
	{ SADB_X_GRPSPIS,		"sadb_grpspis",		NULL },
	{ SADB_X_PROMISC,		"sadb_promisc",		NULL },
	{ 0,				NULL,			NULL },
};

struct idname sa_types[] = {
	{ SADB_SATYPE_UNSPEC,		"unspec",		NULL },
	{ SADB_SATYPE_AH,		"ah",			NULL },
	{ SADB_SATYPE_ESP,		"esp",			NULL },
	{ SADB_SATYPE_RSVP,		"rsvp",			NULL },
	{ SADB_SATYPE_OSPFV2,		"ospfv2",		NULL },
	{ SADB_SATYPE_RIPV2,		"ripv2",		NULL },
	{ SADB_SATYPE_MIP,		"mip",			NULL },
	{ SADB_X_SATYPE_IPIP,		"ipip",			NULL },
	{ SADB_X_SATYPE_TCPSIGNATURE,	"tcpmd5",		NULL },
	{ SADB_X_SATYPE_IPCOMP,		"ipcomp",		NULL },
	{ 0,				NULL,			NULL }
};

struct idname auth_types[] = {
	{ SADB_AALG_NONE,		"none",			NULL },
	{ SADB_AALG_MD5HMAC,		"hmac-md5",		NULL },
	{ SADB_X_AALG_RIPEMD160HMAC,	"hmac-ripemd160",	NULL },
	{ SADB_AALG_SHA1HMAC,		"hmac-sha1",		NULL },
	{ SADB_X_AALG_SHA2_256,		"hmac-sha2-256",	NULL },
	{ SADB_X_AALG_SHA2_384,		"hmac-sha2-384",	NULL },
	{ SADB_X_AALG_SHA2_512,		"hmac-sha2-512",	NULL },
	{ SADB_X_AALG_AES128GMAC,	"gmac-aes-128",		NULL },
	{ SADB_X_AALG_AES192GMAC,	"gmac-aes-192",		NULL },
	{ SADB_X_AALG_AES256GMAC,	"gmac-aes-256",		NULL },
	{ SADB_X_AALG_CHACHA20POLY1305,	"chacha20-poly1305",	NULL },
	{ 0,				NULL,			NULL }
};

struct idname enc_types[] = {
	{ SADB_EALG_NONE,		"none",			NULL },
	{ SADB_EALG_3DESCBC,		"3des-cbc",		NULL },
	{ SADB_X_EALG_AES,		"aes",			NULL },
	{ SADB_X_EALG_AESCTR,		"aesctr",		NULL },
	{ SADB_X_EALG_AESGCM16,		"aes-gcm",		NULL },
	{ SADB_X_EALG_AESGMAC,		"aes-gmac",		NULL },
	{ SADB_X_EALG_BLF,		"blowfish",		NULL },
	{ SADB_X_EALG_CAST,		"cast128",		NULL },
	{ SADB_EALG_NULL,		"null",			NULL },
	{ SADB_X_EALG_CHACHA20POLY1305,	"chacha20-poly1305",	NULL },
	{ 0,				NULL,			NULL }
};

struct idname comp_types[] = {
	{ SADB_X_CALG_NONE,		"none",			NULL },
	{ SADB_X_CALG_OUI,		"oui",			NULL },
	{ SADB_X_CALG_DEFLATE,		"deflate",		NULL },
	{ 0,				NULL,			NULL }
};

struct idname flag_types[] = {
	{ SADB_SAFLAGS_PFS,		"pfs",			NULL },
	{ SADB_X_SAFLAGS_TUNNEL,	"tunnel",		NULL },
	{ SADB_X_SAFLAGS_CHAINDEL,	"chaindel",		NULL },
	{ SADB_X_SAFLAGS_UDPENCAP,	"udpencap",		NULL },
	{ SADB_X_SAFLAGS_ESN,		"esn",			NULL },
	{ 0,				NULL,			NULL }
};

struct idname identity_types[] = {
	{ SADB_IDENTTYPE_RESERVED,	"reserved",		NULL },
	{ SADB_IDENTTYPE_PREFIX,	"prefix",		NULL },
	{ SADB_IDENTTYPE_FQDN,		"fqdn",			NULL },
	{ SADB_IDENTTYPE_USERFQDN,	"ufqdn",		NULL },
	{ SADB_IDENTTYPE_ASN1_DN,	"asn1_dn",		NULL },
	{ 0,				NULL,			NULL }
};

struct idname flow_types[] = {
	{ SADB_X_FLOW_TYPE_USE,		"use",			NULL },
	{ SADB_X_FLOW_TYPE_ACQUIRE,	"acquire",		NULL },
	{ SADB_X_FLOW_TYPE_REQUIRE,	"require",		NULL },
	{ SADB_X_FLOW_TYPE_BYPASS,	"bypass",		NULL },
	{ SADB_X_FLOW_TYPE_DENY,	"deny",			NULL },
	{ SADB_X_FLOW_TYPE_DONTACQ,	"dontacq",		NULL },
	{ 0,				NULL,			NULL }
};

struct idname states[] = {
	{ SADB_SASTATE_LARVAL,		"larval",		NULL },
	{ SADB_SASTATE_MATURE,		"mature",		NULL },
	{ SADB_SASTATE_DYING,		"dying",		NULL },
	{ SADB_SASTATE_DEAD,		"dead",			NULL },
	{ 0,				NULL,			NULL }
};

static struct idname *
lookup(struct idname *tab, u_int32_t id)
{
	struct idname *entry;

	for (entry = tab; entry->name; entry++)
		if (entry->id == id)
			return (entry);
	return (NULL);
}

static char *
lookup_name(struct idname *tab, u_int32_t id)
{
	struct idname *entry;

	entry = lookup(tab, id);
	return (entry ? entry->name : "unknown");
}

static void
print_ext(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct idname *entry;

	if ((entry = lookup(ext_types, ext->sadb_ext_type)) == NULL) {
		printf("unknown ext: type %u len %u\n",
		    ext->sadb_ext_type, ext->sadb_ext_len);
		return;
	}
	printf("\t%s: ", entry->name);
	if (entry->func != NULL)
		(*entry->func)(ext, msg, opts);
	else
		printf("type %u len %u",
		    ext->sadb_ext_type, ext->sadb_ext_len);
	printf("\n");
}

static char *
print_flags(uint32_t flags)
{
	static char fstr[80];
	struct idname *entry;
	int len;
	int i, comma = 0, n;

	len = snprintf(fstr, sizeof(fstr), "%#x<", flags);
	if (len < 0 || (size_t)len >= sizeof(fstr))
		return (NULL);
	for (i = 0; i < 32; i++) {
		if ((flags & (1 << i)) == 0 ||
		    (entry = lookup(flag_types, 1 << i)) == NULL)
			continue;
		n = snprintf(fstr + len, sizeof(fstr) - len - 1,
		    comma ? ",%s" : "%s", entry->name);
		if (n < 0 || (size_t)n >= sizeof(fstr) - len - 1)
			return (NULL);
		len += n;
		comma = 1;
	}
	strlcat(fstr, ">", sizeof(fstr));

	return (fstr);
}

static void
print_sa(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_sa *sa = (struct sadb_sa *)ext;

	if (msg->sadb_msg_satype == SADB_X_SATYPE_IPCOMP)
		printf("cpi 0x%8.8x comp %s\n",
		    ntohl(sa->sadb_sa_spi),
		    lookup_name(comp_types, sa->sadb_sa_encrypt));
	else
		printf("spi 0x%8.8x auth %s enc %s\n",
		    ntohl(sa->sadb_sa_spi),
		    lookup_name(auth_types, sa->sadb_sa_auth),
		    lookup_name(enc_types, sa->sadb_sa_encrypt));
	printf("\t\tstate %s replay %u flags %s",
	    lookup_name(states, sa->sadb_sa_state),
	    sa->sadb_sa_replay, print_flags(sa->sadb_sa_flags));
}

static void
print_addr(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_address *addr = (struct sadb_address *)ext;
	struct sockaddr *sa;
	struct sockaddr_in *sin4;
	struct sockaddr_in6 *sin6;
	char hbuf[NI_MAXHOST];

	sa = (struct sockaddr *)(addr + 1);
	if (sa->sa_family == 0)
		printf("<any>");
	else if (getnameinfo(sa, sa->sa_len, hbuf, sizeof(hbuf), NULL, 0,
	    NI_NUMERICHOST))
		printf("<could not get numeric hostname>");
	else
		printf("%s", hbuf);
	switch (sa->sa_family) {
	case AF_INET:
		sin4 = (struct sockaddr_in *)sa;
		if (sin4->sin_port)
			printf(" port %u", ntohs(sin4->sin_port));
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)sa;
		if (sin6->sin6_port)
			printf(" port %u", ntohs(sin6->sin6_port));
		break;
	}
}

static void
print_key(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_key *key = (struct sadb_key *)ext;
	u_int8_t *data;
	int i;

	printf("bits %u: ", key->sadb_key_bits);
	data = (u_int8_t *)(key + 1);
	for (i = 0; i < key->sadb_key_bits / 8; i++) {
		printf("%2.2x", data[i]);
		data[i] = 0x00;		/* clear sensitive data */
	}
}

static void
print_life(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_lifetime *life = (struct sadb_lifetime *)ext;

	printf("alloc %u bytes %llu add %llu first %llu",
	    life->sadb_lifetime_allocations,
	    life->sadb_lifetime_bytes,
	    life->sadb_lifetime_addtime,
	    life->sadb_lifetime_usetime);
}

static void
print_proto(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_protocol *proto = (struct sadb_protocol *)ext;

	/* overloaded */
	if (msg->sadb_msg_type == SADB_X_GRPSPIS)
		printf("satype %s flags %x",
		    lookup_name(sa_types, proto->sadb_protocol_proto),
		    proto->sadb_protocol_flags);
	else
		printf("proto %u flags %x",
		    proto->sadb_protocol_proto, proto->sadb_protocol_flags);
}

static void
print_flow(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_protocol *proto = (struct sadb_protocol *)ext;
	char *dir = "unknown";

	switch (proto->sadb_protocol_direction) {
	case IPSP_DIRECTION_IN:
		dir = "in";
		break;
	case IPSP_DIRECTION_OUT:
		dir = "out";
		break;
	}
	printf("type %s direction %s",
	    lookup_name(flow_types, proto->sadb_protocol_proto), dir);
}

static void
print_tag(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_x_tag *stag = (struct sadb_x_tag *)ext;
	char *p;
	int plen;

	p = (char *)(stag + 1);
	plen = stag->sadb_x_tag_len * 8 - sizeof(*stag);
	printf("%.*s", plen, p);
}

static void
print_replay(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_x_replay *sreplay = (struct sadb_x_replay *)ext;

	printf("rpl %llu", sreplay->sadb_x_replay_count);
}

static void
print_mtu(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_x_mtu *smtu = (struct sadb_x_mtu *)ext;

	printf("mtu %u", smtu->sadb_x_mtu_mtu);
}

static void
print_tap(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_x_tap *stap = (struct sadb_x_tap *)ext;

	printf("enc%u", stap->sadb_x_tap_unit);
}

static void
print_satype(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_protocol *proto = (struct sadb_protocol *)ext;

	printf("type %s", lookup_name(sa_types, proto->sadb_protocol_proto));
}

static void
print_counter(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_x_counter *scnt = (struct sadb_x_counter *)ext;

	printf("\n");

#define plural(n) ((n) != 1 ? "s" : "")
#define p(f, m) if (scnt->f || opts & IPSECCTL_OPT_VERBOSE2) \
	printf(m, scnt->f, plural(scnt->f))
	p(sadb_x_counter_ipackets, "\t\t%llu input packet%s\n");
	p(sadb_x_counter_opackets, "\t\t%llu output packet%s\n");
	p(sadb_x_counter_ibytes, "\t\t%llu input byte%s\n");
	p(sadb_x_counter_obytes, "\t\t%llu output byte%s\n");
	p(sadb_x_counter_idecompbytes, "\t\t%llu input byte%s, decompressed\n");
	p(sadb_x_counter_ouncompbytes,"\t\t%llu output byte%s, uncompressed\n");
	p(sadb_x_counter_idrops, "\t\t%llu packet%s dropped on input\n");
	p(sadb_x_counter_odrops, "\t\t%llu packet%s dropped on output\n");
#undef p
#undef plural
}

static void
print_iface(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_x_iface *siface = (struct sadb_x_iface *)ext;
	const char *dir = "unknown";

	switch (siface->sadb_x_iface_direction) {
	case IPSP_DIRECTION_IN:
		dir = "in";
		break;
	case IPSP_DIRECTION_OUT:
		dir = "out";
		break;
	}

	printf("sec%u direction %s", siface->sadb_x_iface_unit, dir);
}

static char *
alg_by_ext(u_int8_t ext_type, u_int8_t id)
{
	switch (ext_type) {
	case SADB_EXT_SUPPORTED_ENCRYPT:
		return lookup_name(enc_types, id);
	case SADB_EXT_SUPPORTED_AUTH:
		return lookup_name(auth_types, id);
	case SADB_X_EXT_SUPPORTED_COMP:
		return lookup_name(comp_types, id);
	default:
		return "unknown";
	}
}

static void
print_alg(struct sadb_alg *alg, u_int8_t ext_type)
{
	printf("\t\t%s iv %u min %u max %u",
	    alg_by_ext(ext_type, alg->sadb_alg_id), alg->sadb_alg_ivlen,
	    alg->sadb_alg_minbits, alg->sadb_alg_maxbits);
}

static void
print_supp(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_supported *supported = (struct sadb_supported *)ext;
	struct sadb_alg *alg;

	printf("\n");
	for (alg = (struct sadb_alg *)(supported + 1);
	    (size_t)((u_int8_t *)alg - (u_int8_t *)ext) <
	    ext->sadb_ext_len * PFKEYV2_CHUNK;
	    alg++) {
		struct sadb_alg *next = alg + 1;
		print_alg(alg, ext->sadb_ext_type);
		if ((size_t)((u_int8_t *)next - (u_int8_t *)ext) <
		    ext->sadb_ext_len * PFKEYV2_CHUNK)
			printf("\n");
	}
}

static void
print_comb(struct sadb_comb *comb, struct sadb_msg *msg, int opts)
{
	printf("\t\tauth %s min %u max %u\n"
	    "\t\tenc %s min %u max %u\n"
	    "\t\taddtime hard %llu soft %llu\n"
	    "\t\tusetime hard %llu soft %llu",
	    lookup_name(auth_types, comb->sadb_comb_auth),
	    comb->sadb_comb_auth_minbits,
	    comb->sadb_comb_auth_maxbits,
	    lookup_name(enc_types, comb->sadb_comb_encrypt),
	    comb->sadb_comb_encrypt_minbits,
	    comb->sadb_comb_encrypt_maxbits,
	    comb->sadb_comb_soft_addtime,
	    comb->sadb_comb_hard_addtime,
	    comb->sadb_comb_soft_usetime,
	    comb->sadb_comb_hard_usetime);
#if 0
	    comb->sadb_comb_flags,
	    comb->sadb_comb_reserved,
	    comb->sadb_comb_soft_allocations,
	    comb->sadb_comb_hard_allocations,
	    comb->sadb_comb_soft_bytes,
	    comb->sadb_comb_hard_bytes,
#endif
}

static void
print_prop(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_prop *prop = (struct sadb_prop *)ext;
	struct sadb_comb *comb;

	printf("replay %u\n", prop->sadb_prop_replay);
	for (comb = (struct sadb_comb *)(prop + 1);
	    (size_t)((u_int8_t *)comb - (u_int8_t *)ext) <
	    ext->sadb_ext_len * PFKEYV2_CHUNK;
	    comb++)
		print_comb(comb, msg, opts);
}

static void
print_sens(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_sens *sens = (struct sadb_sens *)ext;

	printf("dpd %u sens_level %u integ_level %u",
	    sens->sadb_sens_dpd,
	    sens->sadb_sens_sens_level,
	    sens->sadb_sens_integ_level);
}

static void
print_spir(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_spirange *spirange = (struct sadb_spirange *)ext;

	printf("min 0x%8.8x max 0x%8.8x",
	    spirange->sadb_spirange_min, spirange->sadb_spirange_max);
}

static void
print_ident(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_ident *ident = (struct sadb_ident *)ext;
	int ilen;

	ilen = ident->sadb_ident_len * 8 - sizeof(*ident);
	printf("type %s id %llu: %.*s",
	    lookup_name(identity_types, ident->sadb_ident_type),
	    ident->sadb_ident_id, ilen, (char *)(ident + 1));
}

static void
print_policy(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_x_policy *x_policy = (struct sadb_x_policy *)ext;

	printf("seq %u", x_policy->sadb_x_policy_seq);
}

static void
print_udpenc(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_x_udpencap *x_udpencap = (struct sadb_x_udpencap *)ext;

	printf("udpencap port %u", ntohs(x_udpencap->sadb_x_udpencap_port));
}

static void
print_rdomain(struct sadb_ext *ext, struct sadb_msg *msg, int opts)
{
	struct sadb_x_rdomain *srdomain = (struct sadb_x_rdomain *)ext;

	printf("%d/%d", srdomain->sadb_x_rdomain_dom1,
	    srdomain->sadb_x_rdomain_dom2);
}

static void
setup_extensions(struct sadb_msg *msg)
{
	struct sadb_ext *ext;

	bzero(extensions, sizeof(extensions));
	if (msg->sadb_msg_len == 0)
		return;
	for (ext = (struct sadb_ext *)(msg + 1);
	    (size_t)((u_int8_t *)ext - (u_int8_t *)msg) <
	    msg->sadb_msg_len * PFKEYV2_CHUNK && ext->sadb_ext_len > 0;
	    ext = (struct sadb_ext *)((u_int8_t *)ext +
	    ext->sadb_ext_len * PFKEYV2_CHUNK))
		extensions[ext->sadb_ext_type] = ext;
}

static void
parse_addr(struct sadb_ext *ext, struct ipsec_addr_wrap *ipa)
{
	struct sadb_address *addr = (struct sadb_address *)ext;
	struct sockaddr *sa;

	if (addr == NULL)
		return;
	sa = (struct sockaddr *)(addr + 1);
	switch (sa->sa_family) {
	case AF_INET:
		ipa->address.v4 = ((struct sockaddr_in *)sa)->sin_addr;
		set_ipmask(ipa, 32);
		break;
	case AF_INET6:
		ipa->address.v6 = ((struct sockaddr_in6 *)sa)->sin6_addr;
		set_ipmask(ipa, 128);
		break;
	}
	ipa->af = sa->sa_family;
	ipa->next = NULL;
	ipa->tail = ipa;
}

static void
parse_key(struct sadb_ext *ext, struct ipsec_key *ikey)
{
	struct sadb_key *key = (struct sadb_key *)ext;
	u_int8_t *data;

	if (key == NULL)
		return;
	data = (u_int8_t *)(key + 1);
	ikey->data = data;
	ikey->len = key->sadb_key_bits / 8;
}

static void
parse_satype(struct sadb_ext *ext, u_int8_t *satype)
{
	struct sadb_protocol *proto = (struct sadb_protocol *)ext;

	if (proto == NULL)
		return;
	switch (proto->sadb_protocol_proto) {
	case SADB_SATYPE_ESP:
		*satype = IPSEC_ESP;
		break;
	case SADB_SATYPE_AH:
		*satype = IPSEC_AH;
		break;
	case SADB_X_SATYPE_IPCOMP:
		*satype = IPSEC_IPCOMP;
		break;
	case SADB_X_SATYPE_IPIP:
		*satype = IPSEC_IPIP;
		break;
	default:
		return;
	}
}

u_int32_t
pfkey_get_spi(struct sadb_msg *msg)
{
	struct sadb_sa *sa;

	setup_extensions(msg);
	sa = (struct sadb_sa *)extensions[SADB_EXT_SA];
	return (ntohl(sa->sadb_sa_spi));
}

/* opposite of pfkey_sa() */
void
pfkey_print_sa(struct sadb_msg *msg, int opts)
{
	int i;
	struct ipsec_rule r;
	struct ipsec_key enckey, authkey;
	struct ipsec_transforms xfs;
	struct ipsec_addr_wrap src, dst, dst2;
	struct sadb_sa *sa, *sa2;

	setup_extensions(msg);
	sa = (struct sadb_sa *)extensions[SADB_EXT_SA];
	bzero(&r, sizeof r);
	r.type |= RULE_SA;
	r.tmode = (msg->sadb_msg_satype != SADB_X_SATYPE_TCPSIGNATURE) &&
	    (sa->sadb_sa_flags & SADB_X_SAFLAGS_TUNNEL) ?
	    IPSEC_TUNNEL : IPSEC_TRANSPORT;
	r.spi = ntohl(sa->sadb_sa_spi);

	switch (msg->sadb_msg_satype) {
	case SADB_SATYPE_AH:
		r.satype = IPSEC_AH;
		break;
	case SADB_SATYPE_ESP:
		r.satype = IPSEC_ESP;
		break;
	case SADB_X_SATYPE_IPCOMP:
		r.satype = IPSEC_IPCOMP;
		break;
	case SADB_X_SATYPE_TCPSIGNATURE:
		r.satype = IPSEC_TCPMD5;
		break;
	case SADB_X_SATYPE_IPIP:
		r.satype = IPSEC_IPIP;
		break;
	default:
		return;
	}
	bzero(&dst, sizeof dst);
	bzero(&src, sizeof src);
	parse_addr(extensions[SADB_EXT_ADDRESS_SRC], &src);
	parse_addr(extensions[SADB_EXT_ADDRESS_DST], &dst);
	r.src = &src;
	r.dst = &dst;
	if (r.satype == IPSEC_IPCOMP) {
		if (sa->sadb_sa_encrypt) {
			bzero(&xfs, sizeof xfs);
			r.xfs = &xfs;
			switch (sa->sadb_sa_encrypt) {
			case SADB_X_CALG_DEFLATE:
				xfs.compxf = &compxfs[COMPXF_DEFLATE];
				break;
			}
		}
	} else if (r.satype == IPSEC_TCPMD5) {
		bzero(&authkey, sizeof authkey);
		parse_key(extensions[SADB_EXT_KEY_AUTH], &authkey);
		r.authkey = &authkey;
	} else if (sa->sadb_sa_encrypt || sa->sadb_sa_auth) {
		bzero(&xfs, sizeof xfs);
		r.xfs = &xfs;
		if (sa->sadb_sa_encrypt) {
			bzero(&enckey, sizeof enckey);
			parse_key(extensions[SADB_EXT_KEY_ENCRYPT], &enckey);
			r.enckey = &enckey;

			switch (sa->sadb_sa_encrypt) {
			case SADB_EALG_3DESCBC:
				xfs.encxf = &encxfs[ENCXF_3DES_CBC];
				break;
			case SADB_X_EALG_AES:
				switch (r.enckey->len) {
				case 192/8:
					xfs.encxf = &encxfs[ENCXF_AES_192];
					break;
				case 256/8:
					xfs.encxf = &encxfs[ENCXF_AES_256];
					break;
				default:
					xfs.encxf = &encxfs[ENCXF_AES];
					break;
				}
				break;
			case SADB_X_EALG_AESCTR:
				switch (r.enckey->len) {
				case 28:
					xfs.encxf = &encxfs[ENCXF_AES_192_CTR];
					break;
				case 36:
					xfs.encxf = &encxfs[ENCXF_AES_256_CTR];
					break;
				default:
					xfs.encxf = &encxfs[ENCXF_AESCTR];
					break;
				}
				break;
			case SADB_X_EALG_AESGCM16:
				switch (r.enckey->len) {
				case 28:
					xfs.encxf = &encxfs[ENCXF_AES_192_GCM];
					break;
				case 36:
					xfs.encxf = &encxfs[ENCXF_AES_256_GCM];
					break;
				default:
					xfs.encxf = &encxfs[ENCXF_AES_128_GCM];
					break;
				}
				break;
			case SADB_X_EALG_AESGMAC:
				switch (r.enckey->len) {
				case 28:
					xfs.encxf = &encxfs[ENCXF_AES_192_GMAC];
					break;
				case 36:
					xfs.encxf = &encxfs[ENCXF_AES_256_GMAC];
					break;
				default:
					xfs.encxf = &encxfs[ENCXF_AES_128_GMAC];
					break;
				}
				break;
			case SADB_X_EALG_BLF:
				xfs.encxf = &encxfs[ENCXF_BLOWFISH];
				break;
			case SADB_X_EALG_CAST:
				xfs.encxf = &encxfs[ENCXF_CAST128];
				break;
			case SADB_X_EALG_CHACHA20POLY1305:
				xfs.encxf = &encxfs[ENCXF_CHACHA20_POLY1305];
				break;
			case SADB_EALG_NULL:
				xfs.encxf = &encxfs[ENCXF_NULL];
				break;
			}
		}
		if (sa->sadb_sa_auth) {
			bzero(&authkey, sizeof authkey);
			parse_key(extensions[SADB_EXT_KEY_AUTH], &authkey);
			r.authkey = &authkey;

			switch (sa->sadb_sa_auth) {
			case SADB_AALG_MD5HMAC:
				xfs.authxf = &authxfs[AUTHXF_HMAC_MD5];
				break;
			case SADB_X_AALG_RIPEMD160HMAC:
				xfs.authxf = &authxfs[AUTHXF_HMAC_RIPEMD160];
				break;
			case SADB_AALG_SHA1HMAC:
				xfs.authxf = &authxfs[AUTHXF_HMAC_SHA1];
				break;
			case SADB_X_AALG_SHA2_256:
				xfs.authxf = &authxfs[AUTHXF_HMAC_SHA2_256];
				break;
			case SADB_X_AALG_SHA2_384:
				xfs.authxf = &authxfs[AUTHXF_HMAC_SHA2_384];
				break;
			case SADB_X_AALG_SHA2_512:
				xfs.authxf = &authxfs[AUTHXF_HMAC_SHA2_512];
				break;
			}
		}
	}
	if (!(opts & IPSECCTL_OPT_SHOWKEY)) {
		bzero(&enckey, sizeof enckey);
		bzero(&authkey, sizeof authkey);
		extensions[SADB_EXT_KEY_AUTH] = NULL;
		extensions[SADB_EXT_KEY_ENCRYPT] = NULL;
	}
	if (extensions[SADB_X_EXT_SA2]) {
		r.type |= RULE_BUNDLE;
		sa2 = (struct sadb_sa *)extensions[SADB_X_EXT_SA2];
		r.spi2 = ntohl(sa2->sadb_sa_spi);
		parse_addr(extensions[SADB_X_EXT_DST2], &dst2);
		r.dst2 = &dst2;
		parse_satype(extensions[SADB_X_EXT_SATYPE2], &r.proto2);
		r.proto = r.satype;
	}
	ipsecctl_print_rule(&r, opts);

	if (opts & IPSECCTL_OPT_VERBOSE) {
		for (i = 0; i <= SADB_EXT_MAX; i++)
			if (extensions[i])
				print_ext(extensions[i], msg, opts);
	}
	fflush(stdout);
}

void
pfkey_monitor_sa(struct sadb_msg *msg, int opts)
{
	int		 i;

	setup_extensions(msg);

	printf("%s: satype %s vers %u len %u seq %u pid %u\n",
	    lookup_name(msg_types, msg->sadb_msg_type),
	    lookup_name(sa_types, msg->sadb_msg_satype),
	    msg->sadb_msg_version, msg->sadb_msg_len,
	    msg->sadb_msg_seq,
	    msg->sadb_msg_pid);
	if (msg->sadb_msg_errno)
		printf("\terrno %u: %s\n", msg->sadb_msg_errno,
		    strerror(msg->sadb_msg_errno));
	for (i = 0; i <= SADB_EXT_MAX; i++)
		if (extensions[i])
			print_ext(extensions[i], msg, opts);
	fflush(stdout);
}

void
pfkey_print_raw(u_int8_t *data, ssize_t len)
{
	int i;
	const u_int8_t *sp = (const u_int8_t *)data;

	printf("RAW PFKEYV2 MESSAGE:\n");
	for (i = 0; i < len; i++) {
		if ((i % 8 == 0) && (i != 0))
			printf("\n");
		printf("%02x ", *sp);
		sp++;
	}
	printf("\n");
}
