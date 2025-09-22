/*	$OpenBSD: parse.y,v 1.149 2025/04/30 03:51:42 tb Exp $	*/

/*
 * Copyright (c) 2019 Tobias Heider <tobias.heider@stusta.de>
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2004, 2005 Hans-Joerg Hoexer <hshoexer@openbsd.org>
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
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

%{
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip_ipsp.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <radius.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <event.h>

#include "iked.h"
#include "ikev2.h"
#include "eap.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	size_t			 ungetpos;
	size_t			 ungetsize;
	u_char			*ungetbuf;
	int			 eof_reached;
	int			 lineno;
	int			 errors;
} *file, *topfile;
struct file	*pushfile(const char *, int);
int		 popfile(void);
int		 check_file_secrecy(int, const char *);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 igetc(void);
int		 lgetc(int);
void		 lungetc(int);
int		 findeol(void);

TAILQ_HEAD(symhead, sym)	 symhead = TAILQ_HEAD_INITIALIZER(symhead);
struct sym {
	TAILQ_ENTRY(sym)	 entry;
	int			 used;
	int			 persist;
	char			*nam;
	char			*val;
};
int		 symset(const char *, const char *, int);
char		*symget(const char *);

#define KEYSIZE_LIMIT	1024

static struct iked	*env = NULL;
static int		 debug = 0;
static int		 rules = 0;
static int		 passive = 0;
static int		 decouple = 0;
static int		 mobike = 1;
static int		 enforcesingleikesa = 0;
static int		 stickyaddress = 0;
static int		 fragmentation = 0;
static int		 vendorid = 1;
static int		 dpd_interval = IKED_IKE_SA_ALIVE_TIMEOUT;
static char		*ocsp_url = NULL;
static long		 ocsp_tolerate = 0;
static long		 ocsp_maxage = -1;
static int		 cert_partial_chain = 0;
static struct iked_radopts
			 radauth, radacct;

struct iked_transform ikev2_default_ike_transforms[] = {
	{ IKEV2_XFORMTYPE_ENCR, IKEV2_XFORMENCR_AES_CBC, 256 },
	{ IKEV2_XFORMTYPE_ENCR, IKEV2_XFORMENCR_AES_CBC, 192 },
	{ IKEV2_XFORMTYPE_ENCR, IKEV2_XFORMENCR_AES_CBC, 128 },
	{ IKEV2_XFORMTYPE_ENCR, IKEV2_XFORMENCR_3DES },
	{ IKEV2_XFORMTYPE_PRF,	IKEV2_XFORMPRF_HMAC_SHA2_256 },
	{ IKEV2_XFORMTYPE_PRF,	IKEV2_XFORMPRF_HMAC_SHA2_384 },
	{ IKEV2_XFORMTYPE_PRF,	IKEV2_XFORMPRF_HMAC_SHA2_512 },
	{ IKEV2_XFORMTYPE_PRF,	IKEV2_XFORMPRF_HMAC_SHA1 },
	{ IKEV2_XFORMTYPE_INTEGR, IKEV2_XFORMAUTH_HMAC_SHA2_256_128 },
	{ IKEV2_XFORMTYPE_INTEGR, IKEV2_XFORMAUTH_HMAC_SHA2_384_192 },
	{ IKEV2_XFORMTYPE_INTEGR, IKEV2_XFORMAUTH_HMAC_SHA2_512_256 },
	{ IKEV2_XFORMTYPE_INTEGR, IKEV2_XFORMAUTH_HMAC_SHA1_96 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_CURVE25519 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_ECP_521 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_ECP_384 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_ECP_256 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_MODP_4096 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_MODP_3072 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_MODP_2048 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_MODP_1536 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_MODP_1024 },
	{ 0 }
};
size_t ikev2_default_nike_transforms = ((sizeof(ikev2_default_ike_transforms) /
    sizeof(ikev2_default_ike_transforms[0])) - 1);

struct iked_transform ikev2_default_ike_transforms_noauth[] = {
	{ IKEV2_XFORMTYPE_ENCR,	IKEV2_XFORMENCR_AES_GCM_16, 128 },
	{ IKEV2_XFORMTYPE_ENCR,	IKEV2_XFORMENCR_AES_GCM_16, 256 },
	{ IKEV2_XFORMTYPE_PRF,	IKEV2_XFORMPRF_HMAC_SHA2_256 },
	{ IKEV2_XFORMTYPE_PRF,	IKEV2_XFORMPRF_HMAC_SHA2_384 },
	{ IKEV2_XFORMTYPE_PRF,	IKEV2_XFORMPRF_HMAC_SHA2_512 },
	{ IKEV2_XFORMTYPE_PRF,	IKEV2_XFORMPRF_HMAC_SHA1 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_CURVE25519 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_ECP_521 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_ECP_384 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_ECP_256 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_MODP_4096 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_MODP_3072 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_MODP_2048 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_MODP_1536 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_MODP_1024 },
	{ 0 }
};
size_t ikev2_default_nike_transforms_noauth =
    ((sizeof(ikev2_default_ike_transforms_noauth) /
    sizeof(ikev2_default_ike_transforms_noauth[0])) - 1);

struct iked_transform ikev2_default_esp_transforms[] = {
	{ IKEV2_XFORMTYPE_ENCR, IKEV2_XFORMENCR_AES_CBC, 256 },
	{ IKEV2_XFORMTYPE_ENCR, IKEV2_XFORMENCR_AES_CBC, 192 },
	{ IKEV2_XFORMTYPE_ENCR, IKEV2_XFORMENCR_AES_CBC, 128 },
	{ IKEV2_XFORMTYPE_INTEGR, IKEV2_XFORMAUTH_HMAC_SHA2_256_128 },
	{ IKEV2_XFORMTYPE_INTEGR, IKEV2_XFORMAUTH_HMAC_SHA2_384_192 },
	{ IKEV2_XFORMTYPE_INTEGR, IKEV2_XFORMAUTH_HMAC_SHA2_512_256 },
	{ IKEV2_XFORMTYPE_INTEGR, IKEV2_XFORMAUTH_HMAC_SHA1_96 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_NONE },
	{ IKEV2_XFORMTYPE_ESN,	IKEV2_XFORMESN_ESN },
	{ IKEV2_XFORMTYPE_ESN,	IKEV2_XFORMESN_NONE },
	{ 0 }
};
size_t ikev2_default_nesp_transforms = ((sizeof(ikev2_default_esp_transforms) /
    sizeof(ikev2_default_esp_transforms[0])) - 1);

struct iked_transform ikev2_default_esp_transforms_noauth[] = {
	{ IKEV2_XFORMTYPE_ENCR,	IKEV2_XFORMENCR_AES_GCM_16, 128 },
	{ IKEV2_XFORMTYPE_ENCR,	IKEV2_XFORMENCR_AES_GCM_16, 256 },
	{ IKEV2_XFORMTYPE_DH,	IKEV2_XFORMDH_NONE },
	{ IKEV2_XFORMTYPE_ESN,	IKEV2_XFORMESN_ESN },
	{ IKEV2_XFORMTYPE_ESN,	IKEV2_XFORMESN_NONE },
	{ 0 }
};
size_t ikev2_default_nesp_transforms_noauth =
    ((sizeof(ikev2_default_esp_transforms_noauth) /
    sizeof(ikev2_default_esp_transforms_noauth[0])) - 1);

const struct ipsec_xf authxfs[] = {
	{ "hmac-md5",		IKEV2_XFORMAUTH_HMAC_MD5_96,		16 },
	{ "hmac-sha1",		IKEV2_XFORMAUTH_HMAC_SHA1_96,		20 },
	{ "hmac-sha2-256",	IKEV2_XFORMAUTH_HMAC_SHA2_256_128,	32 },
	{ "hmac-sha2-384",	IKEV2_XFORMAUTH_HMAC_SHA2_384_192,	48 },
	{ "hmac-sha2-512",	IKEV2_XFORMAUTH_HMAC_SHA2_512_256,	64 },
	{ NULL }
};

const struct ipsec_xf prfxfs[] = {
	{ "hmac-md5",		IKEV2_XFORMPRF_HMAC_MD5,	16 },
	{ "hmac-sha1",		IKEV2_XFORMPRF_HMAC_SHA1,	20 },
	{ "hmac-sha2-256",	IKEV2_XFORMPRF_HMAC_SHA2_256,	32 },
	{ "hmac-sha2-384",	IKEV2_XFORMPRF_HMAC_SHA2_384,	48 },
	{ "hmac-sha2-512",	IKEV2_XFORMPRF_HMAC_SHA2_512,	64 },
	{ NULL }
};

const struct ipsec_xf *encxfs = NULL;

const struct ipsec_xf ikeencxfs[] = {
	{ "3des",		IKEV2_XFORMENCR_3DES,		24 },
	{ "3des-cbc",		IKEV2_XFORMENCR_3DES,		24 },
	{ "aes-128",		IKEV2_XFORMENCR_AES_CBC,	16, 16 },
	{ "aes-192",		IKEV2_XFORMENCR_AES_CBC,	24, 24 },
	{ "aes-256",		IKEV2_XFORMENCR_AES_CBC,	32, 32 },
	{ "aes-128-gcm",	IKEV2_XFORMENCR_AES_GCM_16,	16, 16, 4, 1 },
	{ "aes-256-gcm",	IKEV2_XFORMENCR_AES_GCM_16,	32, 32, 4, 1 },
	{ "aes-128-gcm-12",	IKEV2_XFORMENCR_AES_GCM_12,	16, 16, 4, 1 },
	{ "aes-256-gcm-12",	IKEV2_XFORMENCR_AES_GCM_12,	32, 32, 4, 1 },
	{ NULL }
};

const struct ipsec_xf ipsecencxfs[] = {
	{ "3des",		IKEV2_XFORMENCR_3DES,		24 },
	{ "3des-cbc",		IKEV2_XFORMENCR_3DES,		24 },
	{ "aes-128",		IKEV2_XFORMENCR_AES_CBC,	16, 16 },
	{ "aes-192",		IKEV2_XFORMENCR_AES_CBC,	24, 24 },
	{ "aes-256",		IKEV2_XFORMENCR_AES_CBC,	32, 32 },
	{ "aes-128-ctr",	IKEV2_XFORMENCR_AES_CTR,	16, 16, 4 },
	{ "aes-192-ctr",	IKEV2_XFORMENCR_AES_CTR,	24, 24, 4 },
	{ "aes-256-ctr",	IKEV2_XFORMENCR_AES_CTR,	32, 32, 4 },
	{ "aes-128-gcm",	IKEV2_XFORMENCR_AES_GCM_16,	16, 16, 4, 1 },
	{ "aes-192-gcm",	IKEV2_XFORMENCR_AES_GCM_16,	24, 24, 4, 1 },
	{ "aes-256-gcm",	IKEV2_XFORMENCR_AES_GCM_16,	32, 32, 4, 1 },
	{ "aes-128-gmac",	IKEV2_XFORMENCR_NULL_AES_GMAC,	16, 16, 4, 1 },
	{ "aes-192-gmac",	IKEV2_XFORMENCR_NULL_AES_GMAC,	24, 24, 4, 1 },
	{ "aes-256-gmac",	IKEV2_XFORMENCR_NULL_AES_GMAC,	32, 32, 4, 1 },
	{ "blowfish",		IKEV2_XFORMENCR_BLOWFISH,	20, 20 },
	{ "cast",		IKEV2_XFORMENCR_CAST,		16, 16 },
	{ "chacha20-poly1305",	IKEV2_XFORMENCR_CHACHA20_POLY1305,
								32, 32, 4, 1 },
	{ "null",		IKEV2_XFORMENCR_NULL,		0, 0 },
	{ NULL }
};

const struct ipsec_xf groupxfs[] = {
	{ "none",		IKEV2_XFORMDH_NONE },
	{ "modp768",		IKEV2_XFORMDH_MODP_768 },
	{ "grp1",		IKEV2_XFORMDH_MODP_768 },
	{ "modp1024",		IKEV2_XFORMDH_MODP_1024 },
	{ "grp2",		IKEV2_XFORMDH_MODP_1024 },
	{ "modp1536",		IKEV2_XFORMDH_MODP_1536 },
	{ "grp5",		IKEV2_XFORMDH_MODP_1536 },
	{ "modp2048",		IKEV2_XFORMDH_MODP_2048 },
	{ "grp14",		IKEV2_XFORMDH_MODP_2048 },
	{ "modp3072",		IKEV2_XFORMDH_MODP_3072 },
	{ "grp15",		IKEV2_XFORMDH_MODP_3072 },
	{ "modp4096",		IKEV2_XFORMDH_MODP_4096 },
	{ "grp16",		IKEV2_XFORMDH_MODP_4096 },
	{ "modp6144",		IKEV2_XFORMDH_MODP_6144 },
	{ "grp17",		IKEV2_XFORMDH_MODP_6144 },
	{ "modp8192",		IKEV2_XFORMDH_MODP_8192 },
	{ "grp18",		IKEV2_XFORMDH_MODP_8192 },
	{ "ecp256",		IKEV2_XFORMDH_ECP_256 },
	{ "grp19",		IKEV2_XFORMDH_ECP_256 },
	{ "ecp384",		IKEV2_XFORMDH_ECP_384 },
	{ "grp20",		IKEV2_XFORMDH_ECP_384 },
	{ "ecp521",		IKEV2_XFORMDH_ECP_521 },
	{ "grp21",		IKEV2_XFORMDH_ECP_521 },
	{ "ecp224",		IKEV2_XFORMDH_ECP_224 },
	{ "grp26",		IKEV2_XFORMDH_ECP_224 },
	{ "brainpool224",	IKEV2_XFORMDH_BRAINPOOL_P224R1 },
	{ "grp27",		IKEV2_XFORMDH_BRAINPOOL_P224R1 },
	{ "brainpool256",	IKEV2_XFORMDH_BRAINPOOL_P256R1 },
	{ "grp28",		IKEV2_XFORMDH_BRAINPOOL_P256R1 },
	{ "brainpool384",	IKEV2_XFORMDH_BRAINPOOL_P384R1 },
	{ "grp29",		IKEV2_XFORMDH_BRAINPOOL_P384R1 },
	{ "brainpool512",	IKEV2_XFORMDH_BRAINPOOL_P512R1 },
	{ "grp30",		IKEV2_XFORMDH_BRAINPOOL_P512R1 },
	{ "curve25519",		IKEV2_XFORMDH_CURVE25519 },
	{ "grp31",		IKEV2_XFORMDH_CURVE25519 },
	{ "sntrup761x25519",	IKEV2_XFORMDH_X_SNTRUP761X25519 },
	{ NULL }
};

const struct ipsec_xf esnxfs[] = {
	{ "esn",		IKEV2_XFORMESN_ESN },
	{ "noesn",		IKEV2_XFORMESN_NONE },
	{ NULL }
};

const struct ipsec_xf methodxfs[] = {
	{ "none",		IKEV2_AUTH_NONE },
	{ "rsa",		IKEV2_AUTH_RSA_SIG },
	{ "ecdsa256",		IKEV2_AUTH_ECDSA_256 },
	{ "ecdsa384",		IKEV2_AUTH_ECDSA_384 },
	{ "ecdsa521",		IKEV2_AUTH_ECDSA_521 },
	{ "rfc7427",		IKEV2_AUTH_SIG },
	{ "signature",		IKEV2_AUTH_SIG_ANY },
	{ NULL }
};

const struct ipsec_xf saxfs[] = {
	{ "esp",		IKEV2_SAPROTO_ESP },
	{ "ah",			IKEV2_SAPROTO_AH },
	{ NULL }
};

const struct ipsec_xf cpxfs[] = {
	{ "address", IKEV2_CFG_INTERNAL_IP4_ADDRESS,		AF_INET },
	{ "netmask", IKEV2_CFG_INTERNAL_IP4_NETMASK,		AF_INET },
	{ "name-server", IKEV2_CFG_INTERNAL_IP4_DNS,		AF_INET },
	{ "netbios-server", IKEV2_CFG_INTERNAL_IP4_NBNS,	AF_INET },
	{ "dhcp-server", IKEV2_CFG_INTERNAL_IP4_DHCP,		AF_INET },
	{ "address", IKEV2_CFG_INTERNAL_IP6_ADDRESS,		AF_INET6 },
	{ "name-server", IKEV2_CFG_INTERNAL_IP6_DNS,		AF_INET6 },
	{ "netbios-server", IKEV2_CFG_INTERNAL_IP6_NBNS,	AF_INET6 },
	{ "dhcp-server", IKEV2_CFG_INTERNAL_IP6_DHCP,		AF_INET6 },
	{ "protected-subnet", IKEV2_CFG_INTERNAL_IP4_SUBNET,	AF_INET },
	{ "protected-subnet", IKEV2_CFG_INTERNAL_IP6_SUBNET,	AF_INET6 },
	{ "access-server", IKEV2_CFG_INTERNAL_IP4_SERVER,	AF_INET },
	{ "access-server", IKEV2_CFG_INTERNAL_IP6_SERVER,	AF_INET6 },
	{ NULL }
};

const struct iked_lifetime deflifetime = {
	IKED_LIFETIME_BYTES,
	IKED_LIFETIME_SECONDS
};

#define IPSEC_ADDR_ANY		(0x1)
#define IPSEC_ADDR_DYNAMIC	(0x2)

struct ipsec_addr_wrap {
	struct sockaddr_storage	 address;
	uint8_t			 mask;
	int			 netaddress;
	sa_family_t		 af;
	unsigned int		 type;
	unsigned int		 action;
	uint16_t		 port;
	char			*name;
	struct ipsec_addr_wrap	*next;
	struct ipsec_addr_wrap	*tail;
	struct ipsec_addr_wrap	*srcnat;
};

struct ipsec_hosts {
	struct ipsec_addr_wrap	*src;
	struct ipsec_addr_wrap	*dst;
};

struct ipsec_filters {
	char			*tag;
	unsigned int		 tap;
};

void			 copy_sockaddrtoipa(struct ipsec_addr_wrap *,
			    struct sockaddr *);
struct ipsec_addr_wrap	*host(const char *);
struct ipsec_addr_wrap	*host_ip(const char *, int);
struct ipsec_addr_wrap	*host_dns(const char *, int);
struct ipsec_addr_wrap	*host_if(const char *, int);
struct ipsec_addr_wrap	*host_any(void);
struct ipsec_addr_wrap	*host_dynamic(void);
void			 ifa_load(void);
int			 ifa_exists(const char *);
struct ipsec_addr_wrap	*ifa_lookup(const char *ifa_name);
struct ipsec_addr_wrap	*ifa_grouplookup(const char *);
void			 set_ipmask(struct ipsec_addr_wrap *, int);
const struct ipsec_xf	*parse_xf(const char *, unsigned int,
			    const struct ipsec_xf *);
void			 copy_transforms(unsigned int,
			    const struct ipsec_xf **, unsigned int,
			    struct iked_transform **, unsigned int *,
			    struct iked_transform *, size_t);
int			 create_ike(char *, int, struct ipsec_addr_wrap *,
			    int, struct ipsec_hosts *,
			    struct ipsec_hosts *, struct ipsec_mode *,
			    struct ipsec_mode *, uint8_t,
			    unsigned int, char *, char *,
			    uint32_t, struct iked_lifetime *,
			    struct iked_auth *, struct ipsec_filters *,
			    struct ipsec_addr_wrap *, char *);
int			 create_user(const char *, const char *);
int			 get_id_type(char *);
uint8_t			 x2i(unsigned char *);
int			 parsekey(unsigned char *, size_t, struct iked_auth *);
int			 parsekeyfile(char *, struct iked_auth *);
void			 iaw_free(struct ipsec_addr_wrap *);
static int		 create_flow(struct iked_policy *pol, int, struct ipsec_addr_wrap *ipa,
			    struct ipsec_addr_wrap *ipb);
static int		 expand_flows(struct iked_policy *, int, struct ipsec_addr_wrap *,
			    struct ipsec_addr_wrap *);
static struct ipsec_addr_wrap *
			 expand_keyword(struct ipsec_addr_wrap *);
struct iked_radserver *
			 create_radserver(const char *, u_short, const char *);

struct ipsec_transforms *ipsec_transforms;
struct ipsec_filters *ipsec_filters;
struct ipsec_mode *ipsec_mode;
/* interface lookup routintes */
struct ipsec_addr_wrap	*iftab;

typedef struct {
	union {
		int64_t			 number;
		unsigned int		 ikemode;
		uint8_t			 dir;
		uint8_t			 satype;
		uint8_t			 accounting;
		char			*string;
		uint16_t		 port;
		struct ipsec_hosts	*hosts;
		struct ipsec_hosts	 peers;
		struct ipsec_addr_wrap	*anyhost;
		struct ipsec_addr_wrap	*host;
		struct ipsec_addr_wrap	*cfg;
		struct ipsec_addr_wrap	*proto;
		struct {
			char		*srcid;
			char		*dstid;
		} ids;
		char			*id;
		uint8_t			 type;
		struct iked_lifetime	 lifetime;
		struct iked_auth	 ikeauth;
		struct iked_auth	 ikekey;
		struct ipsec_transforms	*transforms;
		struct ipsec_filters	*filters;
		struct ipsec_mode	*mode;
		struct {
			uint32_t	 vendorid;
			uint8_t		 attrtype;
		} radattr;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	FROM ESP AH IN PEER ON OUT TO SRCID DSTID PSK PORT
%token	FILENAME AUTHXF PRFXF ENCXF ERROR IKEV2 IKESA CHILDSA ESN NOESN
%token	PASSIVE ACTIVE ANY TAG TAP PROTO LOCAL GROUP NAME CONFIG EAP USER
%token	IKEV1 FLOW SA TCPMD5 TUNNEL TRANSPORT COUPLE DECOUPLE SET
%token	INCLUDE LIFETIME BYTES INET INET6 QUICK SKIP DEFAULT
%token	IPCOMP OCSP IKELIFETIME MOBIKE NOMOBIKE RDOMAIN
%token	FRAGMENTATION NOFRAGMENTATION DPD_CHECK_INTERVAL
%token	ENFORCESINGLEIKESA NOENFORCESINGLEIKESA
%token	STICKYADDRESS NOSTICKYADDRESS
%token	VENDORID NOVENDORID
%token	TOLERATE MAXAGE DYNAMIC
%token	CERTPARTIALCHAIN
%token	REQUEST IFACE
%token	RADIUS ACCOUNTING SERVER SECRET MAX_TRIES MAX_FAILOVERS
%token	CLIENT DAE LISTEN ON NATT
%token	<v.string>		STRING
%token	<v.number>		NUMBER
%type	<v.string>		string
%type	<v.satype>		satype
%type	<v.proto>		proto proto_list protoval
%type	<v.hosts>		hosts hosts_list
%type	<v.port>		port
%type	<v.number>		portval af rdomain hexdecnumber
%type	<v.peers>		peers
%type	<v.anyhost>		anyhost
%type	<v.host>		host host_spec
%type	<v.ids>			ids
%type	<v.id>			id
%type	<v.transforms>		transforms
%type	<v.filters>		filters
%type	<v.ikemode>		ikeflags
%type	<v.ikemode>		ikematch ikemode ipcomp tmode natt_force
%type	<v.ikeauth>		ikeauth
%type	<v.ikekey>		keyspec
%type	<v.mode>		ike_sas child_sas
%type	<v.lifetime>		lifetime
%type	<v.number>		byte_spec time_spec ikelifetime
%type	<v.string>		name iface
%type	<v.cfg>			cfg ikecfg ikecfgvals
%type	<v.string>		transform_esn
%type	<v.accounting>		accounting
%type	<v.radattr>		radattr
%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar set '\n'
		| grammar user '\n'
		| grammar ikev2rule '\n'
		| grammar radius '\n'
		| grammar varset '\n'
		| grammar otherrule skipline '\n'
		| grammar error '\n'		{ file->errors++; }
		;

comma		: ','
		| /* empty */
		;

include		: INCLUDE STRING		{
			struct file	*nfile;

			if ((nfile = pushfile($2, 1)) == NULL) {
				yyerror("failed to include file %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			file = nfile;
			lungetc('\n');
		}
		;

set		: SET ACTIVE	{ passive = 0; }
		| SET PASSIVE	{ passive = 1; }
		| SET COUPLE	{ decouple = 0; }
		| SET DECOUPLE	{ decouple = 1; }
		| SET FRAGMENTATION	{ fragmentation = 1; }
		| SET NOFRAGMENTATION	{ fragmentation = 0; }
		| SET MOBIKE	{ mobike = 1; }
		| SET NOMOBIKE	{ mobike = 0; }
		| SET VENDORID		{ vendorid = 1; }
		| SET NOVENDORID	{ vendorid = 0; }
		| SET ENFORCESINGLEIKESA	{ enforcesingleikesa = 1; }
		| SET NOENFORCESINGLEIKESA	{ enforcesingleikesa = 0; }
		| SET STICKYADDRESS	{ stickyaddress = 1; }
		| SET NOSTICKYADDRESS	{ stickyaddress = 0; }
		| SET OCSP STRING		{
			ocsp_url = $3;
		}
		| SET OCSP STRING TOLERATE time_spec {
			ocsp_url = $3;
			ocsp_tolerate = $5;
		}
		| SET OCSP STRING TOLERATE time_spec MAXAGE time_spec {
			ocsp_url = $3;
			ocsp_tolerate = $5;
			ocsp_maxage = $7;
		}
		| SET CERTPARTIALCHAIN		{
			cert_partial_chain = 1;
		}
		| SET DPD_CHECK_INTERVAL NUMBER {
			if ($3 < 0) {
				yyerror("timeout outside range");
				YYERROR;
			}
			dpd_interval = $3;
		}
		;

user		: USER STRING STRING		{
			if (create_user($2, $3) == -1)
				YYERROR;
			free($2);
			freezero($3, strlen($3));
		}
		;

ikev2rule	: IKEV2 name ikeflags satype af proto rdomain hosts_list peers
		    ike_sas child_sas ids ikelifetime lifetime ikeauth ikecfg
		    iface filters {
			if (create_ike($2, $5, $6, $7, $8, &$9, $10, $11, $4,
			    $3, $12.srcid, $12.dstid, $13, &$14, &$15,
			    $18, $16, $17) == -1) {
				yyerror("create_ike failed");
				YYERROR;
			}
		}
		;

ikecfg		: /* empty */			{ $$ = NULL; }
		| ikecfgvals			{ $$ = $1; }
		;

ikecfgvals	: cfg				{ $$ = $1; }
		| ikecfgvals cfg		{
			if ($2 == NULL)
				$$ = $1;
			else if ($1 == NULL)
				$$ = $2;
			else {
				$1->tail->next = $2;
				$1->tail = $2->tail;
				$$ = $1;
			}
		}
		;

cfg		: CONFIG STRING host_spec	{
			const struct ipsec_xf	*xf;

			if ((xf = parse_xf($2, $3->af, cpxfs)) == NULL) {
				yyerror("not a valid ikecfg option");
				free($2);
				free($3);
				YYERROR;
			}
			free($2);
			$$ = $3;
			$$->type = xf->id;
			$$->action = IKEV2_CP_REPLY;	/* XXX */
		}
		| REQUEST STRING anyhost	{
			const struct ipsec_xf	*xf;

			if ((xf = parse_xf($2, $3->af, cpxfs)) == NULL) {
				yyerror("not a valid ikecfg option");
				free($2);
				free($3);
				YYERROR;
			}
			free($2);
			$$ = $3;
			$$->type = xf->id;
			$$->action = IKEV2_CP_REQUEST;	/* XXX */
		}
		;

name		: /* empty */			{ $$ = NULL; }
		| STRING			{
			$$ = $1;
		}

satype		: /* empty */			{ $$ = IKEV2_SAPROTO_ESP; }
		| ESP				{ $$ = IKEV2_SAPROTO_ESP; }
		| AH				{ $$ = IKEV2_SAPROTO_AH; }
		;

af		: /* empty */			{ $$ = AF_UNSPEC; }
		| INET				{ $$ = AF_INET; }
		| INET6				{ $$ = AF_INET6; }
		;

proto		: /* empty */			{ $$ = NULL; }
		| PROTO protoval		{ $$ = $2; }
		| PROTO '{' proto_list '}'	{ $$ = $3; }
		;

proto_list	: protoval			{ $$ = $1; }
		| proto_list comma protoval	{
			if ($3 == NULL)
				$$ = $1;
			else if ($1 == NULL)
				$$ = $3;
			else {
				$1->tail->next = $3;
				$1->tail = $3->tail;
				$$ = $1;
			}
		}
		;

protoval	: STRING			{
			struct protoent *p;

			p = getprotobyname($1);
			if (p == NULL) {
				yyerror("unknown protocol: %s", $1);
				YYERROR;
			}

			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				err(1, "protoval: calloc");

			$$->type = p->p_proto;
			$$->tail = $$;
			free($1);
		}
		| NUMBER			{
			if ($1 > 255 || $1 < 0) {
				yyerror("protocol outside range");
				YYERROR;
			}
			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				err(1, "protoval: calloc");

			$$->type = $1;
			$$->tail = $$;
		}
		;

rdomain		: /* empty */			{ $$ = -1; }
		| RDOMAIN NUMBER		{
			if ($2 > 255 || $2 < 0) {
				yyerror("rdomain outside range");
				YYERROR;
			}
			$$ = $2;
		}

hosts_list	: hosts				{ $$ = $1; }
		| hosts_list comma hosts	{
			if ($3 == NULL)
				$$ = $1;
			else if ($1 == NULL)
				$$ = $3;
			else {
				$1->src->tail->next = $3->src;
				$1->src->tail = $3->src->tail;
				$1->dst->tail->next = $3->dst;
				$1->dst->tail = $3->dst->tail;
				$$ = $1;
				free($3);
			}
		}
		;

hosts		: FROM host port TO host port		{
			struct ipsec_addr_wrap *ipa;
			for (ipa = $5; ipa; ipa = ipa->next) {
				if (ipa->srcnat) {
					yyerror("no flow NAT support for"
					    " destination network: %s",
					    ipa->name);
					YYERROR;
				}
			}

			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				err(1, "hosts: calloc");

			$$->src = $2;
			$$->src->port = $3;
			$$->dst = $5;
			$$->dst->port = $6;
		}
		| TO host port FROM host port		{
			struct ipsec_addr_wrap *ipa;
			for (ipa = $2; ipa; ipa = ipa->next) {
				if (ipa->srcnat) {
					yyerror("no flow NAT support for"
					    " destination network: %s",
					    ipa->name);
					YYERROR;
				}
			}
			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				err(1, "hosts: calloc");

			$$->src = $5;
			$$->src->port = $6;
			$$->dst = $2;
			$$->dst->port = $3;
		}
		;

port		: /* empty */				{ $$ = 0; }
		| PORT portval				{ $$ = $2; }
		;

portval		: STRING				{
			struct servent *s;

			if ((s = getservbyname($1, "tcp")) != NULL ||
			    (s = getservbyname($1, "udp")) != NULL) {
				$$ = s->s_port;
			} else {
				yyerror("unknown port: %s", $1);
				YYERROR;
			}
			free($1);
		}
		| NUMBER				{
			if ($1 > USHRT_MAX || $1 < 0) {
				yyerror("port outside range");
				YYERROR;
			}
			$$ = htons($1);
		}
		;

peers		: /* empty */				{
			$$.dst = NULL;
			$$.src = NULL;
		}
		| PEER anyhost LOCAL anyhost		{
			$$.dst = $2;
			$$.src = $4;
		}
		| LOCAL anyhost PEER anyhost		{
			$$.dst = $4;
			$$.src = $2;
		}
		| PEER anyhost				{
			$$.dst = $2;
			$$.src = NULL;
		}
		| LOCAL anyhost				{
			$$.dst = NULL;
			$$.src = $2;
		}
		;

anyhost		: host_spec			{ $$ = $1; }
		| ANY				{
			$$ = host_any();
		}

host_spec	: STRING			{
			if (($$ = host($1)) == NULL) {
				free($1);
				yyerror("could not parse host specification");
				YYERROR;
			}
			free($1);
		}
		| STRING '/' NUMBER		{
			char	*buf;

			if (asprintf(&buf, "%s/%lld", $1, $3) == -1)
				err(1, "host: asprintf");
			free($1);
			if (($$ = host(buf)) == NULL)	{
				free(buf);
				yyerror("could not parse host specification");
				YYERROR;
			}
			free(buf);
		}
		;

host		: host_spec			{ $$ = $1; }
		| host_spec '(' host_spec ')'   {
			if (($1->af != AF_UNSPEC) && ($3->af != AF_UNSPEC) &&
			    ($3->af != $1->af)) {
				yyerror("Flow NAT address family mismatch");
				YYERROR;
			}
			$$ = $1;
			$$->srcnat = $3;
		}
		| ANY				{
			$$ = host_any();
		}
		| DYNAMIC			{
			$$ = host_dynamic();
		}
		;

ids		: /* empty */			{
			$$.srcid = NULL;
			$$.dstid = NULL;
		}
		| SRCID id DSTID id		{
			$$.srcid = $2;
			$$.dstid = $4;
		}
		| SRCID id			{
			$$.srcid = $2;
			$$.dstid = NULL;
		}
		| DSTID id			{
			$$.srcid = NULL;
			$$.dstid = $2;
		}
		;

id		: STRING			{ $$ = $1; }
		;

transforms	:					{
			if ((ipsec_transforms = calloc(1,
			    sizeof(struct ipsec_transforms))) == NULL)
				err(1, "transforms: calloc");
		}
		    transforms_l			{
			$$ = ipsec_transforms;
		}
		| /* empty */				{
			$$ = NULL;
		}
		;

transforms_l	: transforms_l transform
		| transform
		;

transform	: AUTHXF STRING			{
			const struct ipsec_xf **xfs = ipsec_transforms->authxf;
			size_t nxfs = ipsec_transforms->nauthxf;
			xfs = recallocarray(xfs, nxfs, nxfs + 1,
			    sizeof(struct ipsec_xf *));
			if (xfs == NULL)
				err(1, "transform: recallocarray");
			if ((xfs[nxfs] = parse_xf($2, 0, authxfs)) == NULL) {
				yyerror("%s not a valid transform", $2);
				YYERROR;
			}
			free($2);
			ipsec_transforms->authxf = xfs;
			ipsec_transforms->nauthxf++;
		}
		| ENCXF STRING			{
			const struct ipsec_xf **xfs = ipsec_transforms->encxf;
			size_t nxfs = ipsec_transforms->nencxf;
			xfs = recallocarray(xfs, nxfs, nxfs + 1,
			    sizeof(struct ipsec_xf *));
			if (xfs == NULL)
				err(1, "transform: recallocarray");
			if ((xfs[nxfs] = parse_xf($2, 0, encxfs)) == NULL) {
				yyerror("%s not a valid transform", $2);
				YYERROR;
			}
			free($2);
			ipsec_transforms->encxf = xfs;
			ipsec_transforms->nencxf++;
		}
		| PRFXF STRING			{
			const struct ipsec_xf **xfs = ipsec_transforms->prfxf;
			size_t nxfs = ipsec_transforms->nprfxf;
			xfs = recallocarray(xfs, nxfs, nxfs + 1,
			    sizeof(struct ipsec_xf *));
			if (xfs == NULL)
				err(1, "transform: recallocarray");
			if ((xfs[nxfs] = parse_xf($2, 0, prfxfs)) == NULL) {
				yyerror("%s not a valid transform", $2);
				YYERROR;
			}
			free($2);
			ipsec_transforms->prfxf = xfs;
			ipsec_transforms->nprfxf++;
		}
		| GROUP STRING			{
			const struct ipsec_xf **xfs = ipsec_transforms->groupxf;
			size_t nxfs = ipsec_transforms->ngroupxf;
			xfs = recallocarray(xfs, nxfs, nxfs + 1,
			    sizeof(struct ipsec_xf *));
			if (xfs == NULL)
				err(1, "transform: recallocarray");
			if ((xfs[nxfs] = parse_xf($2, 0, groupxfs)) == NULL) {
				yyerror("%s not a valid transform", $2);
				YYERROR;
			}
			free($2);
			ipsec_transforms->groupxf = xfs;
			ipsec_transforms->ngroupxf++;
		}
		| transform_esn				{
			const struct ipsec_xf **xfs = ipsec_transforms->esnxf;
			size_t nxfs = ipsec_transforms->nesnxf;
			xfs = recallocarray(xfs, nxfs, nxfs + 1,
			    sizeof(struct ipsec_xf *));
			if (xfs == NULL)
				err(1, "transform: recallocarray");
			if ((xfs[nxfs] = parse_xf($1, 0, esnxfs)) == NULL) {
				yyerror("%s not a valid transform", $1);
				YYERROR;
			}
			ipsec_transforms->esnxf = xfs;
			ipsec_transforms->nesnxf++;
		}
		;

transform_esn	: ESN		{ $$ = "esn"; }
		| NOESN		{ $$ = "noesn"; }
		;

ike_sas		:					{
			if ((ipsec_mode = calloc(1,
			    sizeof(struct ipsec_mode))) == NULL)
				err(1, "ike_sas: calloc");
		}
		    ike_sas_l				{
			$$ = ipsec_mode;
		}
		| /* empty */				{
			$$ = NULL;
		}
		;

ike_sas_l	: ike_sas_l ike_sa
		| ike_sa
		;

ike_sa		: IKESA		{
			if ((ipsec_mode->xfs = recallocarray(ipsec_mode->xfs,
			    ipsec_mode->nxfs, ipsec_mode->nxfs + 1,
			    sizeof(struct ipsec_transforms *))) == NULL)
				err(1, "ike_sa: recallocarray");
			ipsec_mode->nxfs++;
			encxfs = ikeencxfs;
		} transforms	{
			ipsec_mode->xfs[ipsec_mode->nxfs - 1] = $3;
		}
		;

child_sas	:					{
			if ((ipsec_mode = calloc(1,
			    sizeof(struct ipsec_mode))) == NULL)
				err(1, "child_sas: calloc");
		}
		    child_sas_l				{
			$$ = ipsec_mode;
		}
		| /* empty */				{
			$$ = NULL;
		}
		;

child_sas_l	: child_sas_l child_sa
		| child_sa
		;

child_sa	: CHILDSA	{
			if ((ipsec_mode->xfs = recallocarray(ipsec_mode->xfs,
			    ipsec_mode->nxfs, ipsec_mode->nxfs + 1,
			    sizeof(struct ipsec_transforms *))) == NULL)
				err(1, "child_sa: recallocarray");
			ipsec_mode->nxfs++;
			encxfs = ipsecencxfs;
		} transforms	{
			ipsec_mode->xfs[ipsec_mode->nxfs - 1] = $3;
		}
		;

ikeflags	: ikematch ikemode ipcomp tmode natt_force {
			$$ = $1 | $2 | $3 | $4 | $5;
		}
		;

ikematch	: /* empty */			{ $$ = 0; }
		| QUICK				{ $$ = IKED_POLICY_QUICK; }
		| SKIP				{ $$ = IKED_POLICY_SKIP; }
		| DEFAULT			{ $$ = IKED_POLICY_DEFAULT; }
		;

ikemode		: /* empty */			{ $$ = IKED_POLICY_PASSIVE; }
		| PASSIVE			{ $$ = IKED_POLICY_PASSIVE; }
		| ACTIVE			{ $$ = IKED_POLICY_ACTIVE; }
		;

ipcomp		: /* empty */			{ $$ = 0; }
		| IPCOMP			{ $$ = IKED_POLICY_IPCOMP; }
		;

tmode		: /* empty */			{ $$ = 0; }
		| TUNNEL			{ $$ = 0; }
		| TRANSPORT			{ $$ = IKED_POLICY_TRANSPORT; }
		;

natt_force	: /* empty */			{ $$ = 0; }
		| NATT				{ $$ = IKED_POLICY_NATT_FORCE; }
		;

ikeauth		: /* empty */			{
			$$.auth_method = IKEV2_AUTH_SIG_ANY;	/* default */
			$$.auth_eap = 0;
			$$.auth_length = 0;
		}
		| PSK keyspec			{
			memcpy(&$$, &$2, sizeof($$));
			$$.auth_method = IKEV2_AUTH_SHARED_KEY_MIC;
			$$.auth_eap = 0;
			explicit_bzero(&$2, sizeof($2));
		}
		| EAP RADIUS			{
			$$.auth_method = IKEV2_AUTH_SIG_ANY;
			$$.auth_eap = EAP_TYPE_RADIUS;
			$$.auth_length = 0;
		}
		| EAP STRING			{
			unsigned int i;

			for (i = 0; i < strlen($2); i++)
				if ($2[i] == '-')
					$2[i] = '_';

			if (strcasecmp("mschap_v2", $2) == 0)
				$$.auth_eap = EAP_TYPE_MSCHAP_V2;
			else if (strcasecmp("radius", $2) == 0)
				$$.auth_eap = EAP_TYPE_RADIUS;
			else {
				yyerror("unsupported EAP method: %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			$$.auth_method = IKEV2_AUTH_SIG_ANY;
			$$.auth_length = 0;
		}
		| STRING			{
			const struct ipsec_xf *xf;

			if ((xf = parse_xf($1, 0, methodxfs)) == NULL ||
			    xf->id == IKEV2_AUTH_NONE) {
				yyerror("not a valid authentication mode");
				free($1);
				YYERROR;
			}
			free($1);

			$$.auth_method = xf->id;
			$$.auth_eap = 0;
			$$.auth_length = 0;
		}
		;

byte_spec	: NUMBER			{
			$$ = $1;
		}
		| STRING			{
			uint64_t	 bytes = 0;
			char		 unit = 0;

			if (sscanf($1, "%llu%c", &bytes, &unit) != 2) {
				yyerror("invalid byte specification: %s", $1);
				YYERROR;
			}
			free($1);
			switch (toupper((unsigned char)unit)) {
			case 'K':
				bytes *= 1024;
				break;
			case 'M':
				bytes *= 1024 * 1024;
				break;
			case 'G':
				bytes *= 1024 * 1024 * 1024;
				break;
			default:
				yyerror("invalid byte unit");
				YYERROR;
			}
			$$ = bytes;
		}
		;

time_spec	: NUMBER			{
			$$ = $1;
		}
		| STRING			{
			uint64_t	 seconds = 0;
			char		 unit = 0;

			if (sscanf($1, "%llu%c", &seconds, &unit) != 2) {
				yyerror("invalid time specification: %s", $1);
				YYERROR;
			}
			free($1);
			switch (tolower((unsigned char)unit)) {
			case 'm':
				seconds *= 60;
				break;
			case 'h':
				seconds *= 60 * 60;
				break;
			default:
				yyerror("invalid time unit");
				YYERROR;
			}
			$$ = seconds;
		}
		;

lifetime	: /* empty */				{
			$$ = deflifetime;
		}
		| LIFETIME time_spec			{
			$$.lt_seconds = $2;
			$$.lt_bytes = deflifetime.lt_bytes;
		}
		| LIFETIME time_spec BYTES byte_spec	{
			$$.lt_seconds = $2;
			$$.lt_bytes = $4;
		}
		;

ikelifetime	: /* empty */				{
			$$ = 0;
		}
		| IKELIFETIME time_spec			{
			$$ = $2;
		}

keyspec		: STRING			{
			uint8_t		*hex;

			bzero(&$$, sizeof($$));

			hex = $1;
			if (strncmp(hex, "0x", 2) == 0) {
				hex += 2;
				if (parsekey(hex, strlen(hex), &$$) != 0) {
					free($1);
					YYERROR;
				}
			} else {
				if (strlen($1) > sizeof($$.auth_data)) {
					yyerror("psk too long");
					free($1);
					YYERROR;
				}
				strlcpy($$.auth_data, $1,
				    sizeof($$.auth_data));
				$$.auth_length = strlen($1);
			}
			freezero($1, strlen($1));
		}
		| FILENAME STRING		{
			if (parsekeyfile($2, &$$) != 0) {
				free($2);
				YYERROR;
			}
			free($2);
		}
		;

filters		:					{
			if ((ipsec_filters = calloc(1,
			    sizeof(struct ipsec_filters))) == NULL)
				err(1, "filters: calloc");
		}
		    filters_l			{
			$$ = ipsec_filters;
		}
		| /* empty */				{
			$$ = NULL;
		}
		;

filters_l	: filters_l filter
		| filter
		;

filter		: TAG STRING
		{
			ipsec_filters->tag = $2;
		}
		| TAP STRING
		{
			const char	*errstr = NULL;
			size_t		 len;

			len = strcspn($2, "0123456789");
			if (strlen("enc") != len ||
			    strncmp("enc", $2, len) != 0) {
				yyerror("invalid tap interface name: %s", $2);
				free($2);
				YYERROR;
			}
			ipsec_filters->tap =
			    strtonum($2 + len, 0, UINT_MAX, &errstr);
			free($2);
			if (errstr != NULL) {
				yyerror("invalid tap interface unit: %s",
				    errstr);
				YYERROR;
			}
		}
		;

iface		:		{
			$$ = NULL;
		}
		| IFACE STRING	{
			$$ = $2;
		}

string		: string STRING
		{
			if (asprintf(&$$, "%s %s", $1, $2) == -1)
				err(1, "string: asprintf");
			free($1);
			free($2);
		}
		| STRING
		;

radius		: RADIUS accounting SERVER STRING port SECRET STRING
		{
			int		 ret, gai_err;
			struct addrinfo	 hints, *ai;
			u_short		 port;

			memset(&hints, 0, sizeof(hints));
			hints.ai_family = PF_UNSPEC;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
			if ((gai_err = getaddrinfo($4, NULL, &hints, &ai))
			    != 0) {
				yyerror("could not parse the address: %s: %s",
				    $4, gai_strerror(gai_err));
				free($4);
				explicit_bzero($7, strlen($7));
				free($7);
				YYERROR;
			}
			port = $5;
			if (port == 0)
				port = htons((!$2)? RADIUS_DEFAULT_PORT :
				    RADIUS_ACCT_DEFAULT_PORT);
			socket_af(ai->ai_addr, port);
			if ((ret = config_setradserver(env, ai->ai_addr,
			    ai->ai_addrlen, $7, $2)) != 0) {
				yyerror("could not set radius server");
				free($4);
				explicit_bzero($7, strlen($7));
				free($7);
				YYERROR;
			}
			explicit_bzero($7, strlen($7));
			freeaddrinfo(ai);
			free($4);
			free($7);
		}
		| RADIUS accounting MAX_TRIES NUMBER {
			if ($4 <= 0) {
				yyerror("max-tries must a positive value");
				YYERROR;
			}
			if ($2)
				radacct.max_tries = $4;
			else
				radauth.max_tries = $4;
		}
		| RADIUS accounting MAX_FAILOVERS NUMBER {
			if ($4 < 0) {
				yyerror("max-failovers must be 0 or a "
				    "positive value");
				YYERROR;
			}
			if ($2)
				radacct.max_failovers = $4;
			else
				radauth.max_failovers = $4;
		}
		| RADIUS CONFIG af STRING radattr {
			const struct ipsec_xf	*xf;
			int			 af, cfgtype;

			af = $3;
			if (af == AF_UNSPEC)
				af = AF_INET;
			if (strcmp($4, "none") == 0)
				cfgtype = 0;
			else {
				if ((xf = parse_xf($4, af, cpxfs)) == NULL ||
				    xf->id == IKEV2_CFG_INTERNAL_IP4_SUBNET ||
				    xf->id == IKEV2_CFG_INTERNAL_IP6_SUBNET) {
					yyerror("not a valid ikecfg option");
					free($4);
					YYERROR;
				}
				cfgtype = xf->id;
			}
			free($4);
			config_setradcfgmap(env, cfgtype, $5.vendorid,
			    $5.attrtype);
		}
		| RADIUS DAE LISTEN ON STRING port {
			int		 ret, gai_err;
			struct addrinfo	 hints, *ai;
			u_short		 port;

			memset(&hints, 0, sizeof(hints));
			hints.ai_family = PF_UNSPEC;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
			if ((gai_err = getaddrinfo($5, NULL, &hints, &ai))
			    != 0) {
				yyerror("could not parse the address: %s: %s",
				    $5, gai_strerror(gai_err));
				free($5);
				YYERROR;
			}
			port = $6;
			if (port == 0)
				port = htons(RADIUS_DAE_DEFAULT_PORT);
			socket_af(ai->ai_addr, port);
			if ((ret = config_setraddae(env, ai->ai_addr,
			    ai->ai_addrlen)) != 0) {
				yyerror("could not set radius server");
				free($5);
				YYERROR;
			}
			freeaddrinfo(ai);
			free($5);
		}
		| RADIUS DAE CLIENT STRING SECRET STRING {
			int		 gai_err;
			struct addrinfo	 hints, *ai;

			memset(&hints, 0, sizeof(hints));
			hints.ai_family = PF_UNSPEC;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
			if ((gai_err = getaddrinfo($4, NULL, &hints, &ai))
			    != 0) {
				yyerror("could not parse the address: %s: %s",
				    $4, gai_strerror(gai_err));
				free($4);
				explicit_bzero($6, strlen($6));
				free($6);
				YYERROR;
			}
			config_setradclient(env, ai->ai_addr, ai->ai_addrlen,
			    $6);
			free($4);
			explicit_bzero($6, strlen($6));
			free($6);
			freeaddrinfo(ai);
		}
		;

radattr		: hexdecnumber hexdecnumber {
			if ($1 < 0 || 0xffffffL < $1) {
				yyerror("vendor-id must be in 0-0xffffff");
				YYERROR;
			}
			if ($2 < 0 || 256 <= $2) {
				yyerror("attribute type must be in 0-255");
				YYERROR;
			}
			$$.vendorid = $1;
			$$.attrtype = $2;
		}
		| hexdecnumber {
			if ($1 < 0 || 256 <= $1) {
				yyerror("attribute type must be in 0-255");
				YYERROR;
			}
			$$.vendorid = 0;
			$$.attrtype = $1;
		}

hexdecnumber	: STRING {
			const char	*errstr;
			char		*ep;
			uintmax_t	 ul;

			if ($1[0] == '0' && $1[1] == 'x' && isxdigit($1[2])) {
				ul = strtoumax($1 + 2, &ep, 16);
				if (*ep != '\0') {
					yyerror("`%s' is not a number", $1);
					free($1);
					YYERROR;
				}
				if (ul == UINTMAX_MAX || ul > UINT64_MAX) {
					yyerror("`%s' is out-of-range", $1);
					free($1);
					YYERROR;
				}
				$$ = ul;
			} else {
				$$ = strtonum($1, 0, UINT64_MAX, &errstr);
				if (errstr != NULL) {
					yyerror("`%s' is %s", $1, errstr);
					free($1);
					YYERROR;
				}
			}
			free($1);
		}
		| NUMBER
		;

accounting	: {
			$$ = 0;
		}
		| ACCOUNTING {
			$$ = 1;
		}
		;

varset		: STRING '=' string
		{
			char *s = $1;
			log_debug("%s = \"%s\"\n", $1, $3);
			while (*s++) {
				if (isspace((unsigned char)*s)) {
					yyerror("macro name cannot contain "
					    "whitespace");
					free($1);
					free($3);
					YYERROR;
				}
			}
			if (symset($1, $3, 0) == -1)
				err(1, "cannot store variable");
			free($1);
			free($3);
		}
		;

/*
 * ignore IKEv1/manual keying rules in ipsec.conf
 */
otherrule	: IKEV1
		| sarule
		| FLOW
		| TCPMD5
		;

/* manual keying SAs might start with the following keywords */
sarule		: SA
		| FROM
		| TO
		| TUNNEL
		| TRANSPORT
		;

/* ignore everything to the end of the line */
skipline	:
		{
			int	 c;

			while ((c = lgetc(0)) != '\n' && c != EOF)
				; /* nothing */
			if (c == '\n')
				lungetc(c);
		}
		;
%%

struct keywords {
	const char	*k_name;
	int		 k_val;
};

void
copy_sockaddrtoipa(struct ipsec_addr_wrap *ipa, struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET6)
		memcpy(&ipa->address, sa, sizeof(struct sockaddr_in6));
	else if (sa->sa_family == AF_INET)
		memcpy(&ipa->address, sa, sizeof(struct sockaddr_in));
	else
		warnx("unhandled af %d", sa->sa_family);
}

int
yyerror(const char *fmt, ...)
{
	va_list		 ap;

	file->errors++;
	va_start(ap, fmt);
	fprintf(stderr, "%s: %d: ", file->name, yylval.lineno);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	return (0);
}

int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{ "accounting",		ACCOUNTING },
		{ "active",		ACTIVE },
		{ "ah",			AH },
		{ "any",		ANY },
		{ "auth",		AUTHXF },
		{ "bytes",		BYTES },
		{ "cert_partial_chain",	CERTPARTIALCHAIN },
		{ "childsa",		CHILDSA },
		{ "client",		CLIENT },
		{ "config",		CONFIG },
		{ "couple",		COUPLE },
		{ "dae",		DAE },
		{ "decouple",		DECOUPLE },
		{ "default",		DEFAULT },
		{ "dpd_check_interval",	DPD_CHECK_INTERVAL },
		{ "dstid",		DSTID },
		{ "dynamic",		DYNAMIC },
		{ "eap",		EAP },
		{ "enc",		ENCXF },
		{ "enforcesingleikesa",	ENFORCESINGLEIKESA },
		{ "esn",		ESN },
		{ "esp",		ESP },
		{ "file",		FILENAME },
		{ "flow",		FLOW },
		{ "fragmentation",	FRAGMENTATION },
		{ "from",		FROM },
		{ "group",		GROUP },
		{ "iface",		IFACE },
		{ "ike",		IKEV1 },
		{ "ikelifetime",	IKELIFETIME },
		{ "ikesa",		IKESA },
		{ "ikev2",		IKEV2 },
		{ "include",		INCLUDE },
		{ "inet",		INET },
		{ "inet6",		INET6 },
		{ "ipcomp",		IPCOMP },
		{ "lifetime",		LIFETIME },
		{ "listen",		LISTEN },
		{ "local",		LOCAL },
		{ "max-failovers",	MAX_FAILOVERS},
		{ "max-tries",		MAX_TRIES },
		{ "maxage",		MAXAGE },
		{ "mobike",		MOBIKE },
		{ "name",		NAME },
		{ "natt",		NATT },
		{ "noenforcesingleikesa",	NOENFORCESINGLEIKESA },
		{ "noesn",		NOESN },
		{ "nofragmentation",	NOFRAGMENTATION },
		{ "nomobike",		NOMOBIKE },
		{ "nostickyaddress",	NOSTICKYADDRESS },
		{ "novendorid",		NOVENDORID },
		{ "ocsp",		OCSP },
		{ "on",			ON },
		{ "passive",		PASSIVE },
		{ "peer",		PEER },
		{ "port",		PORT },
		{ "prf",		PRFXF },
		{ "proto",		PROTO },
		{ "psk",		PSK },
		{ "quick",		QUICK },
		{ "radius",		RADIUS },
		{ "rdomain",		RDOMAIN },
		{ "request",		REQUEST },
		{ "sa",			SA },
		{ "secret",		SECRET },
		{ "server",		SERVER },
		{ "set",		SET },
		{ "skip",		SKIP },
		{ "srcid",		SRCID },
		{ "stickyaddress",	STICKYADDRESS },
		{ "tag",		TAG },
		{ "tap",		TAP },
		{ "tcpmd5",		TCPMD5 },
		{ "to",			TO },
		{ "tolerate",		TOLERATE },
		{ "transport",		TRANSPORT },
		{ "tunnel",		TUNNEL },
		{ "user",		USER },
		{ "vendorid",		VENDORID }
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p) {
		if (debug > 1)
			fprintf(stderr, "%s: %d\n", s, p->k_val);
		return (p->k_val);
	} else {
		if (debug > 1)
			fprintf(stderr, "string: %s\n", s);
		return (STRING);
	}
}

#define START_EXPAND	1
#define DONE_EXPAND	2

static int	expanding;

int
igetc(void)
{
	int	c;

	while (1) {
		if (file->ungetpos > 0)
			c = file->ungetbuf[--file->ungetpos];
		else
			c = getc(file->stream);

		if (c == START_EXPAND)
			expanding = 1;
		else if (c == DONE_EXPAND)
			expanding = 0;
		else
			break;
	}
	return (c);
}

int
lgetc(int quotec)
{
	int		c, next;

	if (quotec) {
		if ((c = igetc()) == EOF) {
			yyerror("reached end of file while parsing "
			    "quoted string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = igetc()) == '\\') {
		next = igetc();
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}

	while (c == EOF) {
		/*
		 * Fake EOL when hit EOF for the first time. This gets line
		 * count right if last line in included file is syntactically
		 * invalid and has no newline.
		 */
		if (file->eof_reached == 0) {
			file->eof_reached = 1;
			return ('\n');
		}
		while (c == EOF) {
			if (file == topfile || popfile() == EOF)
				return (EOF);
			c = igetc();
		}
	}
	return (c);
}

void
lungetc(int c)
{
	if (c == EOF)
		return;

	if (file->ungetpos >= file->ungetsize) {
		void *p = reallocarray(file->ungetbuf, file->ungetsize, 2);
		if (p == NULL)
			err(1, "lungetc");
		file->ungetbuf = p;
		file->ungetsize *= 2;
	}
	file->ungetbuf[file->ungetpos++] = c;
}

int
findeol(void)
{
	int	c;

	/* skip to either EOF or the first real EOL */
	while (1) {
		c = lgetc(0);
		if (c == '\n') {
			file->lineno++;
			break;
		}
		if (c == EOF)
			break;
	}
	return (ERROR);
}

int
yylex(void)
{
	char	 buf[8096];
	char	*p, *val;
	int	 quotec, next, c;
	int	 token;

top:
	p = buf;
	while ((c = lgetc(0)) == ' ' || c == '\t')
		; /* nothing */

	yylval.lineno = file->lineno;
	if (c == '#')
		while ((c = lgetc(0)) != '\n' && c != EOF)
			; /* nothing */
	if (c == '$' && !expanding) {
		while (1) {
			if ((c = lgetc(0)) == EOF)
				return (0);

			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			if (isalnum(c) || c == '_') {
				*p++ = c;
				continue;
			}
			*p = '\0';
			lungetc(c);
			break;
		}
		val = symget(buf);
		if (val == NULL) {
			yyerror("macro '%s' not defined", buf);
			return (findeol());
		}
		p = val + strlen(val) - 1;
		lungetc(DONE_EXPAND);
		while (p >= val) {
			lungetc((unsigned char)*p);
			p--;
		}
		lungetc(START_EXPAND);
		goto top;
	}

	switch (c) {
	case '\'':
	case '"':
		quotec = c;
		while (1) {
			if ((c = lgetc(quotec)) == EOF)
				return (0);
			if (c == '\n') {
				file->lineno++;
				continue;
			} else if (c == '\\') {
				if ((next = lgetc(quotec)) == EOF)
					return (0);
				if (next == quotec || next == ' ' ||
				    next == '\t')
					c = next;
				else if (next == '\n') {
					file->lineno++;
					continue;
				} else
					lungetc(next);
			} else if (c == quotec) {
				*p = '\0';
				break;
			} else if (c == '\0') {
				yyerror("syntax error");
				return (findeol());
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			*p++ = c;
		}
		yylval.v.string = strdup(buf);
		if (yylval.v.string == NULL)
			err(1, "%s", __func__);
		return (STRING);
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x ==',' || x == '/' || x == '}' || x == '=')

	if (c == '-' || isdigit(c)) {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && isdigit(c));
		lungetc(c);
		if (p == buf + 1 && buf[0] == '-')
			goto nodigits;
		if (c == EOF || allowed_to_end_number(c)) {
			const char *errstr = NULL;

			*p = '\0';
			yylval.v.number = strtonum(buf, LLONG_MIN,
			    LLONG_MAX, &errstr);
			if (errstr) {
				yyerror("\"%s\" invalid number: %s",
				    buf, errstr);
				return (findeol());
			}
			return (NUMBER);
		} else {
nodigits:
			while (p > buf + 1)
				lungetc((unsigned char)*--p);
			c = (unsigned char)*--p;
			if (c == '-')
				return (c);
		}
	}

#define allowed_in_string(x) \
	(isalnum(x) || (ispunct(x) && x != '(' && x != ')' && \
	x != '{' && x != '}' && x != '<' && x != '>' && \
	x != '!' && x != '=' && x != '/' && x != '#' && \
	x != ','))

	if (isalnum(c) || c == ':' || c == '_' || c == '*') {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
		lungetc(c);
		*p = '\0';
		if ((token = lookup(buf)) == STRING)
			if ((yylval.v.string = strdup(buf)) == NULL)
				err(1, "%s", __func__);
		return (token);
	}
	if (c == '\n') {
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == EOF)
		return (0);
	return (c);
}

int
check_file_secrecy(int fd, const char *fname)
{
	struct stat	st;

	if (fstat(fd, &st)) {
		warn("cannot stat %s", fname);
		return (-1);
	}
	if (st.st_uid != 0 && st.st_uid != getuid()) {
		warnx("%s: owner not root or current user", fname);
		return (-1);
	}
	if (st.st_mode & (S_IWGRP | S_IXGRP | S_IRWXO)) {
		warnx("%s: group writable or world read/writable", fname);
		return (-1);
	}
	return (0);
}

struct file *
pushfile(const char *name, int secret)
{
	struct file	*nfile;

	if ((nfile = calloc(1, sizeof(struct file))) == NULL) {
		warn("%s", __func__);
		return (NULL);
	}
	if ((nfile->name = strdup(name)) == NULL) {
		warn("%s", __func__);
		free(nfile);
		return (NULL);
	}
	if (TAILQ_FIRST(&files) == NULL && strcmp(nfile->name, "-") == 0) {
		nfile->stream = stdin;
		free(nfile->name);
		if ((nfile->name = strdup("stdin")) == NULL) {
			warn("%s", __func__);
			free(nfile);
			return (NULL);
		}
	} else if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
		warn("%s: %s", __func__, nfile->name);
		free(nfile->name);
		free(nfile);
		return (NULL);
	} else if (secret &&
	    check_file_secrecy(fileno(nfile->stream), nfile->name)) {
		fclose(nfile->stream);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	nfile->lineno = TAILQ_EMPTY(&files) ? 1 : 0;
	nfile->ungetsize = 16;
	nfile->ungetbuf = malloc(nfile->ungetsize);
	if (nfile->ungetbuf == NULL) {
		warn("%s", __func__);
		fclose(nfile->stream);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return (nfile);
}

int
popfile(void)
{
	struct file	*prev;

	if ((prev = TAILQ_PREV(file, files, entry)) != NULL)
		prev->errors += file->errors;

	TAILQ_REMOVE(&files, file, entry);
	fclose(file->stream);
	free(file->name);
	free(file->ungetbuf);
	free(file);
	file = prev;

	return (file ? 0 : EOF);
}

int
parse_config(const char *filename, struct iked *x_env)
{
	struct sym	*sym;
	int		 errors = 0;

	env = x_env;
	rules = 0;

	if ((file = pushfile(filename, 1)) == NULL)
		return (-1);
	topfile = file;

	free(ocsp_url);

	mobike = 1;
	enforcesingleikesa = stickyaddress = 0;
	cert_partial_chain = decouple = passive = 0;
	ocsp_tolerate = 0;
	ocsp_url = NULL;
	ocsp_maxage = -1;
	fragmentation = 0;
	dpd_interval = IKED_IKE_SA_ALIVE_TIMEOUT;
	decouple = passive = 0;
	ocsp_url = NULL;
	radauth.max_tries = 3;
	radauth.max_failovers = 0;
	radacct.max_tries = 3;
	radacct.max_failovers = 0;

	if (env->sc_opts & IKED_OPT_PASSIVE)
		passive = 1;

	yyparse();
	errors = file->errors;
	popfile();

	env->sc_passive = passive ? 1 : 0;
	env->sc_decoupled = decouple ? 1 : 0;
	env->sc_mobike = mobike;
	env->sc_enforcesingleikesa = enforcesingleikesa;
	env->sc_stickyaddress = stickyaddress;
	env->sc_frag = fragmentation;
	env->sc_alive_timeout = dpd_interval;
	env->sc_ocsp_url = ocsp_url;
	env->sc_ocsp_tolerate = ocsp_tolerate;
	env->sc_ocsp_maxage = ocsp_maxage;
	env->sc_cert_partial_chain = cert_partial_chain;
	env->sc_vendorid = vendorid;
	env->sc_radauth = radauth;
	env->sc_radacct = radacct;

	if (!rules)
		log_warnx("%s: no valid configuration rules found",
		    filename);
	else
		log_debug("%s: loaded %d configuration rules",
		    filename, rules);

	/* Free macros and check which have not been used. */
	while ((sym = TAILQ_FIRST(&symhead))) {
		if (!sym->used)
			log_debug("warning: macro '%s' not "
			    "used\n", sym->nam);
		free(sym->nam);
		free(sym->val);
		TAILQ_REMOVE(&symhead, sym, entry);
		free(sym);
	}

	iaw_free(iftab);
	iftab = NULL;

	return (errors ? -1 : 0);
}

int
symset(const char *nam, const char *val, int persist)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0)
			break;
	}

	if (sym != NULL) {
		if (sym->persist == 1)
			return (0);
		else {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}
	if ((sym = calloc(1, sizeof(*sym))) == NULL)
		return (-1);

	sym->nam = strdup(nam);
	if (sym->nam == NULL) {
		free(sym);
		return (-1);
	}
	sym->val = strdup(val);
	if (sym->val == NULL) {
		free(sym->nam);
		free(sym);
		return (-1);
	}
	sym->used = 0;
	sym->persist = persist;
	TAILQ_INSERT_TAIL(&symhead, sym, entry);
	return (0);
}

int
cmdline_symset(char *s)
{
	char	*sym, *val;
	int	ret;

	if ((val = strrchr(s, '=')) == NULL)
		return (-1);

	sym = strndup(s, val - s);
	if (sym == NULL)
		err(1, "%s", __func__);
	ret = symset(sym, val + 1, 1);
	free(sym);

	return (ret);
}

char *
symget(const char *nam)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0) {
			sym->used = 1;
			return (sym->val);
		}
	}
	return (NULL);
}

uint8_t
x2i(unsigned char *s)
{
	char	ss[3];

	ss[0] = s[0];
	ss[1] = s[1];
	ss[2] = 0;

	if (!isxdigit(s[0]) || !isxdigit(s[1])) {
		yyerror("keys need to be specified in hex digits");
		return (-1);
	}
	return ((uint8_t)strtoul(ss, NULL, 16));
}

int
parsekey(unsigned char *hexkey, size_t len, struct iked_auth *auth)
{
	unsigned int	  i;

	bzero(auth, sizeof(*auth));
	if ((len / 2) > sizeof(auth->auth_data))
		return (-1);
	auth->auth_length = len / 2;

	for (i = 0; i < auth->auth_length; i++)
		auth->auth_data[i] = x2i(hexkey + 2 * i);

	return (0);
}

int
parsekeyfile(char *filename, struct iked_auth *auth)
{
	struct stat	 sb;
	int		 fd, ret;
	unsigned char	*hex;

	if ((fd = open(filename, O_RDONLY)) == -1)
		err(1, "open %s", filename);
	if (check_file_secrecy(fd, filename) == -1)
		exit(1);
	if (fstat(fd, &sb) == -1)
		err(1, "parsekeyfile: stat %s", filename);
	if ((sb.st_size > KEYSIZE_LIMIT) || (sb.st_size == 0))
		errx(1, "%s: key too %s", filename, sb.st_size ? "large" :
		    "small");
	if ((hex = calloc(sb.st_size, sizeof(unsigned char))) == NULL)
		err(1, "parsekeyfile: calloc");
	if (read(fd, hex, sb.st_size) < sb.st_size)
		err(1, "parsekeyfile: read");
	close(fd);
	ret = parsekey(hex, sb.st_size, auth);
	free(hex);
	return (ret);
}

int
get_id_type(char *string)
{
	struct in6_addr ia;

	if (string == NULL)
		return (IKEV2_ID_NONE);

	if (*string == '/')
		return (IKEV2_ID_ASN1_DN);
	else if (inet_pton(AF_INET, string, &ia) == 1)
		return (IKEV2_ID_IPV4);
	else if (inet_pton(AF_INET6, string, &ia) == 1)
		return (IKEV2_ID_IPV6);
	else if (strchr(string, '@'))
		return (IKEV2_ID_UFQDN);
	else
		return (IKEV2_ID_FQDN);
}

struct ipsec_addr_wrap *
host(const char *s)
{
	struct ipsec_addr_wrap	*ipa = NULL;
	int			 mask = -1;
	char			*p, *ps;
	const char		*errstr;

	if ((ps = strdup(s)) == NULL)
		err(1, "%s: strdup", __func__);

	if ((p = strchr(ps, '/')) != NULL) {
		mask = strtonum(p+1, 0, 128, &errstr);
		if (errstr) {
			fprintf(stderr, "netmask is %s: %s\n", errstr, p);
			goto error;
		}
		p[0] = '\0';
	}

	if ((ipa = host_if(ps, mask)) == NULL &&
	    (ipa = host_ip(ps, mask)) == NULL &&
	    (ipa = host_dns(ps, mask)) == NULL)
		fprintf(stderr, "no IP address found for %s\n", s);

error:
	free(ps);
	return (ipa);
}

struct ipsec_addr_wrap *
host_ip(const char *s, int mask)
{
	struct ipsec_addr_wrap	*ipa = NULL;
	struct addrinfo		 hints, *res;
	char			 hbuf[NI_MAXHOST];

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(s, NULL, &hints, &res))
		return (NULL);
	if (res->ai_next)
		err(1, "%s: %s expanded to multiple item", __func__, s);

	ipa = calloc(1, sizeof(struct ipsec_addr_wrap));
	if (ipa == NULL)
		err(1, "%s", __func__);
	ipa->af = res->ai_family;
	copy_sockaddrtoipa(ipa, res->ai_addr);
	ipa->next = NULL;
	ipa->tail = ipa;

	set_ipmask(ipa, mask);
	if (getnameinfo(res->ai_addr, res->ai_addrlen,
	    hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST)) {
		errx(1, "could not get a numeric hostname");
	}

	if (mask > -1) {
		ipa->netaddress = 1;
		if (asprintf(&ipa->name, "%s/%d", hbuf, mask) == -1)
			err(1, "%s", __func__);
	} else {
		if ((ipa->name = strdup(hbuf)) == NULL)
			err(1, "%s", __func__);
	}

	freeaddrinfo(res);

	return (ipa);
}

struct ipsec_addr_wrap *
host_dns(const char *s, int mask)
{
	struct ipsec_addr_wrap	*ipa = NULL, *head = NULL;
	struct addrinfo		 hints, *res0, *res;
	int			 error;
	char			 hbuf[NI_MAXHOST];

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_ADDRCONFIG;
	error = getaddrinfo(s, NULL, &hints, &res0);
	if (error)
		return (NULL);

	for (res = res0; res; res = res->ai_next) {
		if (res->ai_family != AF_INET && res->ai_family != AF_INET6)
			continue;

		ipa = calloc(1, sizeof(struct ipsec_addr_wrap));
		if (ipa == NULL)
			err(1, "%s", __func__);
		copy_sockaddrtoipa(ipa, res->ai_addr);
		error = getnameinfo(res->ai_addr, res->ai_addrlen, hbuf,
		    sizeof(hbuf), NULL, 0, NI_NUMERICHOST);
		if (error)
			err(1, "host_dns: getnameinfo");
		ipa->name = strdup(hbuf);
		if (ipa->name == NULL)
			err(1, "%s", __func__);
		ipa->af = res->ai_family;
		ipa->next = NULL;
		ipa->tail = ipa;
		if (head == NULL)
			head = ipa;
		else {
			head->tail->next = ipa;
			head->tail = ipa;
		}

		/*
		 * XXX for now, no netmask support for IPv6.
		 * but since there's no way to specify address family, once you
		 * have IPv6 address on a host, you cannot use dns/netmask
		 * syntax.
		 */
		if (ipa->af == AF_INET)
			set_ipmask(ipa, mask == -1 ? 32 : mask);
		else
			if (mask != -1)
				err(1, "host_dns: cannot apply netmask "
				    "on non-IPv4 address");
	}
	freeaddrinfo(res0);

	return (head);
}

struct ipsec_addr_wrap *
host_if(const char *s, int mask)
{
	struct ipsec_addr_wrap *ipa = NULL;

	if (ifa_exists(s))
		ipa = ifa_lookup(s);

	return (ipa);
}

struct ipsec_addr_wrap *
host_any(void)
{
	struct ipsec_addr_wrap	*ipa;

	ipa = calloc(1, sizeof(struct ipsec_addr_wrap));
	if (ipa == NULL)
		err(1, "%s", __func__);
	ipa->af = AF_UNSPEC;
	ipa->netaddress = 1;
	ipa->tail = ipa;
	ipa->type = IPSEC_ADDR_ANY;
	return (ipa);
}

struct ipsec_addr_wrap *
host_dynamic(void)
{
	struct ipsec_addr_wrap	*ipa;

	ipa = calloc(1, sizeof(struct ipsec_addr_wrap));
	if (ipa == NULL)
		err(1, "%s", __func__);
	ipa->af = AF_UNSPEC;
	ipa->tail = ipa;
	ipa->type = IPSEC_ADDR_DYNAMIC;
	return (ipa);
}

void
ifa_load(void)
{
	struct ifaddrs		*ifap, *ifa;
	struct ipsec_addr_wrap	*n = NULL, *h = NULL;
	struct sockaddr_in	*sa_in;
	struct sockaddr_in6	*sa_in6;

	if (getifaddrs(&ifap) == -1)
		err(1, "ifa_load: getifaddrs");

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL ||
		    !(ifa->ifa_addr->sa_family == AF_INET ||
		    ifa->ifa_addr->sa_family == AF_INET6 ||
		    ifa->ifa_addr->sa_family == AF_LINK))
			continue;
		n = calloc(1, sizeof(struct ipsec_addr_wrap));
		if (n == NULL)
			err(1, "%s", __func__);
		n->af = ifa->ifa_addr->sa_family;
		if ((n->name = strdup(ifa->ifa_name)) == NULL)
			err(1, "%s", __func__);
		if (n->af == AF_INET) {
			sa_in = (struct sockaddr_in *)ifa->ifa_addr;
			memcpy(&n->address, sa_in, sizeof(*sa_in));
			sa_in = (struct sockaddr_in *)ifa->ifa_netmask;
			n->mask = mask2prefixlen((struct sockaddr *)sa_in);
		} else if (n->af == AF_INET6) {
			sa_in6 = (struct sockaddr_in6 *)ifa->ifa_addr;
			memcpy(&n->address, sa_in6, sizeof(*sa_in6));
			sa_in6 = (struct sockaddr_in6 *)ifa->ifa_netmask;
			n->mask = mask2prefixlen6((struct sockaddr *)sa_in6);
		}
		n->next = NULL;
		n->tail = n;
		if (h == NULL)
			h = n;
		else {
			h->tail->next = n;
			h->tail = n;
		}
	}

	iftab = h;
	freeifaddrs(ifap);
}

int
ifa_exists(const char *ifa_name)
{
	struct ipsec_addr_wrap	*n;
	struct ifgroupreq	 ifgr;
	int			 s;

	if (iftab == NULL)
		ifa_load();

	/* check wether this is a group */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		err(1, "ifa_exists: socket");
	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, ifa_name, sizeof(ifgr.ifgr_name));
	if (ioctl(s, SIOCGIFGMEMB, (caddr_t)&ifgr) == 0) {
		close(s);
		return (1);
	}
	close(s);

	for (n = iftab; n; n = n->next) {
		if (n->af == AF_LINK && !strncmp(n->name, ifa_name,
		    IFNAMSIZ))
			return (1);
	}

	return (0);
}

struct ipsec_addr_wrap *
ifa_grouplookup(const char *ifa_name)
{
	struct ifg_req		*ifg;
	struct ifgroupreq	 ifgr;
	int			 s;
	size_t			 len;
	struct ipsec_addr_wrap	*n, *h = NULL, *hn;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		err(1, "socket");
	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, ifa_name, sizeof(ifgr.ifgr_name));
	if (ioctl(s, SIOCGIFGMEMB, (caddr_t)&ifgr) == -1) {
		close(s);
		return (NULL);
	}

	len = ifgr.ifgr_len;
	if ((ifgr.ifgr_groups = calloc(1, len)) == NULL)
		err(1, "%s", __func__);
	if (ioctl(s, SIOCGIFGMEMB, (caddr_t)&ifgr) == -1)
		err(1, "ioctl");

	for (ifg = ifgr.ifgr_groups; ifg && len >= sizeof(struct ifg_req);
	    ifg++) {
		len -= sizeof(struct ifg_req);
		if ((n = ifa_lookup(ifg->ifgrq_member)) == NULL)
			continue;
		if (h == NULL)
			h = n;
		else {
			for (hn = h; hn->next != NULL; hn = hn->next)
				;	/* nothing */
			hn->next = n;
			n->tail = hn;
		}
	}
	free(ifgr.ifgr_groups);
	close(s);

	return (h);
}

struct ipsec_addr_wrap *
ifa_lookup(const char *ifa_name)
{
	struct ipsec_addr_wrap	*p = NULL, *h = NULL, *n = NULL;
	struct sockaddr_in6	*in6;
	uint8_t			*s6;

	if (iftab == NULL)
		ifa_load();

	if ((n = ifa_grouplookup(ifa_name)) != NULL)
		return (n);

	for (p = iftab; p; p = p->next) {
		if (p->af != AF_INET && p->af != AF_INET6)
			continue;
		if (strncmp(p->name, ifa_name, IFNAMSIZ))
			continue;
		n = calloc(1, sizeof(struct ipsec_addr_wrap));
		if (n == NULL)
			err(1, "%s", __func__);
		memcpy(n, p, sizeof(struct ipsec_addr_wrap));
		if ((n->name = strdup(p->name)) == NULL)
			err(1, "%s", __func__);
		switch (n->af) {
		case AF_INET:
			set_ipmask(n, 32);
			break;
		case AF_INET6:
			in6 = (struct sockaddr_in6 *)&n->address;
			s6 = (uint8_t *)&in6->sin6_addr.s6_addr;

			/* route/show.c and bgpd/util.c give KAME credit */
			if (IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr)) {
				uint16_t	 tmp16;

				/* for now we can not handle link local,
				 * therefore bail for now
				 */
				free(n->name);
				free(n);
				continue;

				memcpy(&tmp16, &s6[2], sizeof(tmp16));
				/* use this when we support link-local
				 * n->??.scopeid = ntohs(tmp16);
				 */
				s6[2] = 0;
				s6[3] = 0;
			}
			set_ipmask(n, 128);
			break;
		}

		n->next = NULL;
		n->tail = n;
		if (h == NULL)
			h = n;
		else {
			h->tail->next = n;
			h->tail = n;
		}
	}

	return (h);
}

void
set_ipmask(struct ipsec_addr_wrap *address, int b)
{
	if (b == -1)
		address->mask = address->af == AF_INET ? 32 : 128;
	else
		address->mask = b;
}

const struct ipsec_xf *
parse_xf(const char *name, unsigned int length, const struct ipsec_xf xfs[])
{
	int		i;

	for (i = 0; xfs[i].name != NULL; i++) {
		if (strncmp(name, xfs[i].name, strlen(name)))
			continue;
		if (length == 0 || length == xfs[i].length)
			return &xfs[i];
	}
	return (NULL);
}

int
encxf_noauth(unsigned int id)
{
	int i;

	for (i = 0; ikeencxfs[i].name != NULL; i++)
		if (ikeencxfs[i].id == id)
			return ikeencxfs[i].noauth;
	return (0);
}

size_t
keylength_xf(unsigned int saproto, unsigned int type, unsigned int id)
{
	int			 i;
	const struct ipsec_xf	*xfs;

	switch (type) {
	case IKEV2_XFORMTYPE_ENCR:
		if (saproto == IKEV2_SAPROTO_IKE)
			xfs = ikeencxfs;
		else
			xfs = ipsecencxfs;
		break;
	case IKEV2_XFORMTYPE_INTEGR:
		xfs = authxfs;
		break;
	default:
		return (0);
	}

	for (i = 0; xfs[i].name != NULL; i++) {
		if (xfs[i].id == id)
			return (xfs[i].length * 8);
	}
	return (0);
}

size_t
noncelength_xf(unsigned int type, unsigned int id)
{
	const struct ipsec_xf	*xfs = ipsecencxfs;
	int			 i;

	if (type != IKEV2_XFORMTYPE_ENCR)
		return (0);

	for (i = 0; xfs[i].name != NULL; i++)
		if (xfs[i].id == id)
			return (xfs[i].nonce * 8);
	return (0);
}

void
copy_transforms(unsigned int type,
    const struct ipsec_xf **xfs, unsigned int nxfs,
    struct iked_transform **dst, unsigned int *ndst,
    struct iked_transform *src, size_t nsrc)
{
	unsigned int		 i;
	struct iked_transform	*a, *b;
	const struct ipsec_xf	*xf;

	if (nxfs) {
		for (i = 0; i < nxfs; i++) {
			xf = xfs[i];
			*dst = recallocarray(*dst, *ndst,
			    *ndst + 1, sizeof(struct iked_transform));
			if (*dst == NULL)
				err(1, "%s", __func__);
			b = *dst + (*ndst)++;

			b->xform_type = type;
			b->xform_id = xf->id;
			b->xform_keylength = xf->length * 8;
			b->xform_length = xf->keylength * 8;
		}
		return;
	}

	for (i = 0; i < nsrc; i++) {
		a = src + i;
		if (a->xform_type != type)
			continue;
		*dst = recallocarray(*dst, *ndst,
		    *ndst + 1, sizeof(struct iked_transform));
		if (*dst == NULL)
			err(1, "%s", __func__);
		b = *dst + (*ndst)++;
		memcpy(b, a, sizeof(*b));
	}
}

int
create_ike(char *name, int af, struct ipsec_addr_wrap *ipproto,
    int rdomain, struct ipsec_hosts *hosts,
    struct ipsec_hosts *peers, struct ipsec_mode *ike_sa,
    struct ipsec_mode *ipsec_sa, uint8_t saproto,
    unsigned int flags, char *srcid, char *dstid,
    uint32_t ikelifetime, struct iked_lifetime *lt,
    struct iked_auth *authtype, struct ipsec_filters *filter,
    struct ipsec_addr_wrap *ikecfg, char *iface)
{
	char			 idstr[IKED_ID_SIZE];
	struct ipsec_addr_wrap	*ipa, *ipb, *ipp;
	struct iked_auth	*ikeauth;
	struct iked_policy	 pol;
	struct iked_proposal	*p, *ptmp;
	struct iked_transform	*xf;
	unsigned int		 i, j, xfi, noauth, auth;
	unsigned int		 ikepropid = 1, ipsecpropid = 1;
	struct iked_flow	*flow, *ftmp;
	static unsigned int	 policy_id = 0;
	struct iked_cfg		*cfg;
	int			 ret = -1;

	bzero(&pol, sizeof(pol));
	bzero(idstr, sizeof(idstr));

	pol.pol_id = ++policy_id;
	pol.pol_certreqtype = env->sc_certreqtype;
	pol.pol_af = af;
	pol.pol_saproto = saproto;
	for (i = 0, ipp = ipproto; ipp; ipp = ipp->next, i++) {
		if (i >= IKED_IPPROTO_MAX) {
			yyerror("too many protocols");
			return (-1);
		}
		pol.pol_ipproto[i] = ipp->type;
		pol.pol_nipproto++;
	}

	pol.pol_flags = flags;
	pol.pol_rdomain = rdomain;
	memcpy(&pol.pol_auth, authtype, sizeof(struct iked_auth));
	explicit_bzero(authtype, sizeof(*authtype));

	if (name != NULL) {
		if (strlcpy(pol.pol_name, name,
		    sizeof(pol.pol_name)) >= sizeof(pol.pol_name)) {
			yyerror("name too long");
			return (-1);
		}
	} else {
		snprintf(pol.pol_name, sizeof(pol.pol_name),
		    "policy%d", policy_id);
	}

	if (iface != NULL) {
		/* sec(4) */
		if (strncmp("sec", iface, strlen("sec")) == 0)
			pol.pol_flags |= IKED_POLICY_ROUTING;

		pol.pol_iface = if_nametoindex(iface);
		if (pol.pol_iface == 0) {
			yyerror("invalid iface");
			return (-1);
		}
	}

	if (srcid) {
		pol.pol_localid.id_type = get_id_type(srcid);
		pol.pol_localid.id_length = strlen(srcid);
		if (strlcpy((char *)pol.pol_localid.id_data,
		    srcid, IKED_ID_SIZE) >= IKED_ID_SIZE) {
			yyerror("srcid too long");
			return (-1);
		}
	}
	if (dstid) {
		pol.pol_peerid.id_type = get_id_type(dstid);
		pol.pol_peerid.id_length = strlen(dstid);
		if (strlcpy((char *)pol.pol_peerid.id_data,
		    dstid, IKED_ID_SIZE) >= IKED_ID_SIZE) {
			yyerror("dstid too long");
			return (-1);
		}
	}

	if (filter != NULL) {
		if (filter->tag)
			strlcpy(pol.pol_tag, filter->tag, sizeof(pol.pol_tag));
		pol.pol_tap = filter->tap;
	}

	if (peers == NULL) {
		if (pol.pol_flags & IKED_POLICY_ACTIVE) {
			yyerror("active mode requires peer specification");
			return (-1);
		}
		pol.pol_flags |= IKED_POLICY_DEFAULT|IKED_POLICY_SKIP;
	}

	if (peers && peers->src && peers->dst &&
	    (peers->src->af != AF_UNSPEC) && (peers->dst->af != AF_UNSPEC) &&
	    (peers->src->af != peers->dst->af))
		fatalx("create_ike: peer address family mismatch");

	if (peers && (pol.pol_af != AF_UNSPEC) &&
	    ((peers->src && (peers->src->af != AF_UNSPEC) &&
	    (peers->src->af != pol.pol_af)) ||
	    (peers->dst && (peers->dst->af != AF_UNSPEC) &&
	    (peers->dst->af != pol.pol_af))))
		fatalx("create_ike: policy address family mismatch");

	ipa = ipb = NULL;
	if (peers) {
		if (peers->src)
			ipa = peers->src;
		if (peers->dst)
			ipb = peers->dst;
		if (ipa == NULL && ipb == NULL) {
			if (hosts->src && hosts->src->next == NULL)
				ipa = hosts->src;
			if (hosts->dst && hosts->dst->next == NULL)
				ipb = hosts->dst;
		}
	}
	if (ipa == NULL && ipb == NULL) {
		yyerror("could not get local/peer specification");
		return (-1);
	}
	if (pol.pol_flags & IKED_POLICY_ACTIVE) {
		if (ipb == NULL || ipb->netaddress ||
		    (ipa != NULL && ipa->netaddress)) {
			yyerror("active mode requires local/peer address");
			return (-1);
		}
	}
	if (ipa) {
		memcpy(&pol.pol_local.addr, &ipa->address,
		    sizeof(ipa->address));
		pol.pol_local.addr_af = ipa->af;
		pol.pol_local.addr_mask = ipa->mask;
		pol.pol_local.addr_net = ipa->netaddress;
		if (pol.pol_af == AF_UNSPEC)
			pol.pol_af = ipa->af;
	}
	if (ipb) {
		memcpy(&pol.pol_peer.addr, &ipb->address,
		    sizeof(ipb->address));
		pol.pol_peer.addr_af = ipb->af;
		pol.pol_peer.addr_mask = ipb->mask;
		pol.pol_peer.addr_net = ipb->netaddress;
		if (pol.pol_af == AF_UNSPEC)
			pol.pol_af = ipb->af;
	}

	if (ikelifetime)
		pol.pol_rekey = ikelifetime;

	if (lt)
		pol.pol_lifetime = *lt;
	else
		pol.pol_lifetime = deflifetime;

	TAILQ_INIT(&pol.pol_proposals);
	RB_INIT(&pol.pol_flows);

	if (ike_sa == NULL || ike_sa->nxfs == 0) {
		/* AES-GCM proposal */
		if ((p = calloc(1, sizeof(*p))) == NULL)
			err(1, "%s", __func__);
		p->prop_id = ikepropid++;
		p->prop_protoid = IKEV2_SAPROTO_IKE;
		p->prop_nxforms = ikev2_default_nike_transforms_noauth;
		p->prop_xforms = ikev2_default_ike_transforms_noauth;
		TAILQ_INSERT_TAIL(&pol.pol_proposals, p, prop_entry);
		pol.pol_nproposals++;

		/* Non GCM proposal */
		if ((p = calloc(1, sizeof(*p))) == NULL)
			err(1, "%s", __func__);
		p->prop_id = ikepropid++;
		p->prop_protoid = IKEV2_SAPROTO_IKE;
		p->prop_nxforms = ikev2_default_nike_transforms;
		p->prop_xforms = ikev2_default_ike_transforms;
		TAILQ_INSERT_TAIL(&pol.pol_proposals, p, prop_entry);
		pol.pol_nproposals++;
	} else {
		for (i = 0; i < ike_sa->nxfs; i++) {
			noauth = auth = 0;
			for (j = 0; j < ike_sa->xfs[i]->nencxf; j++) {
				if (ike_sa->xfs[i]->encxf[j]->noauth)
					noauth++;
				else
					auth++;
			}
			for (j = 0; j < ike_sa->xfs[i]->ngroupxf; j++) {
				if (ike_sa->xfs[i]->groupxf[j]->id
				    == IKEV2_XFORMDH_NONE) {
					yyerror("IKE group can not be \"none\".");
					goto done;
				}
			}
			if (ike_sa->xfs[i]->nauthxf)
				auth++;

			if (ike_sa->xfs[i]->nesnxf) {
				yyerror("cannot use ESN with ikesa.");
				goto done;
			}
			if (noauth && noauth != ike_sa->xfs[i]->nencxf) {
				yyerror("cannot mix encryption transforms with "
				    "implicit and non-implicit authentication");
				goto done;
			}
			if (noauth && ike_sa->xfs[i]->nauthxf) {
				yyerror("authentication is implicit for given "
				    "encryption transforms");
				goto done;
			}

			if (!auth) {
				if ((p = calloc(1, sizeof(*p))) == NULL)
					err(1, "%s", __func__);

				xf = NULL;
				xfi = 0;
				copy_transforms(IKEV2_XFORMTYPE_ENCR,
				    ike_sa->xfs[i]->encxf,
				    ike_sa->xfs[i]->nencxf, &xf, &xfi,
				    ikev2_default_ike_transforms_noauth,
				    ikev2_default_nike_transforms_noauth);
				copy_transforms(IKEV2_XFORMTYPE_DH,
				    ike_sa->xfs[i]->groupxf,
				    ike_sa->xfs[i]->ngroupxf, &xf, &xfi,
				    ikev2_default_ike_transforms_noauth,
				    ikev2_default_nike_transforms_noauth);
				copy_transforms(IKEV2_XFORMTYPE_PRF,
				    ike_sa->xfs[i]->prfxf,
				    ike_sa->xfs[i]->nprfxf, &xf, &xfi,
				    ikev2_default_ike_transforms_noauth,
				    ikev2_default_nike_transforms_noauth);

				p->prop_id = ikepropid++;
				p->prop_protoid = IKEV2_SAPROTO_IKE;
				p->prop_xforms = xf;
				p->prop_nxforms = xfi;
				TAILQ_INSERT_TAIL(&pol.pol_proposals, p, prop_entry);
				pol.pol_nproposals++;
			}
			if (!noauth) {
				if ((p = calloc(1, sizeof(*p))) == NULL)
					err(1, "%s", __func__);

				xf = NULL;
				xfi = 0;
				copy_transforms(IKEV2_XFORMTYPE_INTEGR,
				    ike_sa->xfs[i]->authxf,
				    ike_sa->xfs[i]->nauthxf, &xf, &xfi,
				    ikev2_default_ike_transforms,
				    ikev2_default_nike_transforms);
				copy_transforms(IKEV2_XFORMTYPE_ENCR,
				    ike_sa->xfs[i]->encxf,
				    ike_sa->xfs[i]->nencxf, &xf, &xfi,
				    ikev2_default_ike_transforms,
				    ikev2_default_nike_transforms);
				copy_transforms(IKEV2_XFORMTYPE_DH,
				    ike_sa->xfs[i]->groupxf,
				    ike_sa->xfs[i]->ngroupxf, &xf, &xfi,
				    ikev2_default_ike_transforms,
				    ikev2_default_nike_transforms);
				copy_transforms(IKEV2_XFORMTYPE_PRF,
				    ike_sa->xfs[i]->prfxf,
				    ike_sa->xfs[i]->nprfxf, &xf, &xfi,
				    ikev2_default_ike_transforms,
				    ikev2_default_nike_transforms);

				p->prop_id = ikepropid++;
				p->prop_protoid = IKEV2_SAPROTO_IKE;
				p->prop_xforms = xf;
				p->prop_nxforms = xfi;
				TAILQ_INSERT_TAIL(&pol.pol_proposals, p, prop_entry);
				pol.pol_nproposals++;
			}
		}
	}

	if (ipsec_sa == NULL || ipsec_sa->nxfs == 0) {
		if ((p = calloc(1, sizeof(*p))) == NULL)
			err(1, "%s", __func__);
		p->prop_id = ipsecpropid++;
		p->prop_protoid = saproto;
		p->prop_nxforms = ikev2_default_nesp_transforms_noauth;
		p->prop_xforms = ikev2_default_esp_transforms_noauth;
		TAILQ_INSERT_TAIL(&pol.pol_proposals, p, prop_entry);
		pol.pol_nproposals++;

		if ((p = calloc(1, sizeof(*p))) == NULL)
			err(1, "%s", __func__);
		p->prop_id = ipsecpropid++;
		p->prop_protoid = saproto;
		p->prop_nxforms = ikev2_default_nesp_transforms;
		p->prop_xforms = ikev2_default_esp_transforms;
		TAILQ_INSERT_TAIL(&pol.pol_proposals, p, prop_entry);
		pol.pol_nproposals++;
	} else {
		for (i = 0; i < ipsec_sa->nxfs; i++) {
			noauth = auth = 0;
			for (j = 0; j < ipsec_sa->xfs[i]->nencxf; j++) {
				if (ipsec_sa->xfs[i]->encxf[j]->noauth)
					noauth++;
				else
					auth++;
			}
			if (ipsec_sa->xfs[i]->nauthxf)
				auth++;

			if (noauth && noauth != ipsec_sa->xfs[i]->nencxf) {
				yyerror("cannot mix encryption transforms with "
				    "implicit and non-implicit authentication");
				goto done;
			}
			if (noauth && ipsec_sa->xfs[i]->nauthxf) {
				yyerror("authentication is implicit for given "
				    "encryption transforms");
				goto done;
			}

			if (!auth) {
				if ((p = calloc(1, sizeof(*p))) == NULL)
					err(1, "%s", __func__);

				xf = NULL;
				xfi = 0;
				copy_transforms(IKEV2_XFORMTYPE_ENCR,
				    ipsec_sa->xfs[i]->encxf,
				    ipsec_sa->xfs[i]->nencxf, &xf, &xfi,
				    ikev2_default_esp_transforms_noauth,
				    ikev2_default_nesp_transforms_noauth);
				copy_transforms(IKEV2_XFORMTYPE_DH,
				    ipsec_sa->xfs[i]->groupxf,
				    ipsec_sa->xfs[i]->ngroupxf, &xf, &xfi,
				    ikev2_default_esp_transforms_noauth,
				    ikev2_default_nesp_transforms_noauth);
				copy_transforms(IKEV2_XFORMTYPE_ESN,
				    ipsec_sa->xfs[i]->esnxf,
				    ipsec_sa->xfs[i]->nesnxf, &xf, &xfi,
				    ikev2_default_esp_transforms_noauth,
				    ikev2_default_nesp_transforms_noauth);

				p->prop_id = ipsecpropid++;
				p->prop_protoid = saproto;
				p->prop_xforms = xf;
				p->prop_nxforms = xfi;
				TAILQ_INSERT_TAIL(&pol.pol_proposals, p, prop_entry);
				pol.pol_nproposals++;
			}
			if (!noauth) {
				if ((p = calloc(1, sizeof(*p))) == NULL)
					err(1, "%s", __func__);

				xf = NULL;
				xfi = 0;
				copy_transforms(IKEV2_XFORMTYPE_INTEGR,
				    ipsec_sa->xfs[i]->authxf,
				    ipsec_sa->xfs[i]->nauthxf, &xf, &xfi,
				    ikev2_default_esp_transforms,
				    ikev2_default_nesp_transforms);
				copy_transforms(IKEV2_XFORMTYPE_ENCR,
				    ipsec_sa->xfs[i]->encxf,
				    ipsec_sa->xfs[i]->nencxf, &xf, &xfi,
				    ikev2_default_esp_transforms,
				    ikev2_default_nesp_transforms);
				copy_transforms(IKEV2_XFORMTYPE_DH,
				    ipsec_sa->xfs[i]->groupxf,
				    ipsec_sa->xfs[i]->ngroupxf, &xf, &xfi,
				    ikev2_default_esp_transforms,
				    ikev2_default_nesp_transforms);
				copy_transforms(IKEV2_XFORMTYPE_ESN,
				    ipsec_sa->xfs[i]->esnxf,
				    ipsec_sa->xfs[i]->nesnxf, &xf, &xfi,
				    ikev2_default_esp_transforms,
				    ikev2_default_nesp_transforms);

				p->prop_id = ipsecpropid++;
				p->prop_protoid = saproto;
				p->prop_xforms = xf;
				p->prop_nxforms = xfi;
				TAILQ_INSERT_TAIL(&pol.pol_proposals, p, prop_entry);
				pol.pol_nproposals++;
			}
		}
	}

	for (ipa = hosts->src, ipb = hosts->dst; ipa && ipb;
	    ipa = ipa->next, ipb = ipb->next) {
		for (j = 0; j < pol.pol_nipproto; j++)
			if (expand_flows(&pol, pol.pol_ipproto[j], ipa, ipb))
				fatalx("create_ike: invalid flow");
		if (pol.pol_nipproto == 0)
			if (expand_flows(&pol, 0, ipa, ipb))
				fatalx("create_ike: invalid flow");
	}

	for (j = 0, ipa = ikecfg; ipa; ipa = ipa->next, j++) {
		if (j >= IKED_CFG_MAX)
			break;
		cfg = &pol.pol_cfg[j];
		pol.pol_ncfg++;

		cfg->cfg_action = ipa->action;
		cfg->cfg_type = ipa->type;
		memcpy(&cfg->cfg.address.addr, &ipa->address,
		    sizeof(ipa->address));
		cfg->cfg.address.addr_mask = ipa->mask;
		cfg->cfg.address.addr_net = ipa->netaddress;
		cfg->cfg.address.addr_af = ipa->af;
	}

	if (dstid)
		strlcpy(idstr, dstid, sizeof(idstr));
	else if (!pol.pol_peer.addr_net)
		strlcpy(idstr, print_addr(&pol.pol_peer.addr), sizeof(idstr));

	ikeauth = &pol.pol_auth;
	switch (ikeauth->auth_method) {
	case IKEV2_AUTH_RSA_SIG:
		pol.pol_certreqtype = IKEV2_CERT_RSA_KEY;
		break;
	case IKEV2_AUTH_ECDSA_256:
	case IKEV2_AUTH_ECDSA_384:
	case IKEV2_AUTH_ECDSA_521:
		pol.pol_certreqtype = IKEV2_CERT_ECDSA;
		break;
	default:
		pol.pol_certreqtype = IKEV2_CERT_NONE;
		break;
	}

	log_debug("%s: using %s for peer %s", __func__,
	    print_xf(ikeauth->auth_method, 0, methodxfs), idstr);

	config_setpolicy(env, &pol, PROC_IKEV2);
	config_setflow(env, &pol, PROC_IKEV2);

	rules++;
	ret = 0;

done:
	if (ike_sa) {
		for (i = 0; i < ike_sa->nxfs; i++) {
			free(ike_sa->xfs[i]->authxf);
			free(ike_sa->xfs[i]->encxf);
			free(ike_sa->xfs[i]->groupxf);
			free(ike_sa->xfs[i]->prfxf);
			free(ike_sa->xfs[i]);
		}
		free(ike_sa->xfs);
		free(ike_sa);
	}
	if (ipsec_sa) {
		for (i = 0; i < ipsec_sa->nxfs; i++) {
			free(ipsec_sa->xfs[i]->authxf);
			free(ipsec_sa->xfs[i]->encxf);
			free(ipsec_sa->xfs[i]->groupxf);
			free(ipsec_sa->xfs[i]->prfxf);
			free(ipsec_sa->xfs[i]->esnxf);
			free(ipsec_sa->xfs[i]);
		}
		free(ipsec_sa->xfs);
		free(ipsec_sa);
	}
	TAILQ_FOREACH_SAFE(p, &pol.pol_proposals, prop_entry, ptmp) {
		if (p->prop_xforms != ikev2_default_ike_transforms &&
		    p->prop_xforms != ikev2_default_ike_transforms_noauth &&
		    p->prop_xforms != ikev2_default_esp_transforms &&
		    p->prop_xforms != ikev2_default_esp_transforms_noauth)
			free(p->prop_xforms);
		free(p);
	}
	if (peers != NULL) {
		iaw_free(peers->src);
		iaw_free(peers->dst);
		/* peers is static, cannot be freed */
	}
	if (hosts != NULL) {
		iaw_free(hosts->src);
		iaw_free(hosts->dst);
		free(hosts);
	}
	iaw_free(ikecfg);
	iaw_free(ipproto);
	RB_FOREACH_SAFE(flow, iked_flows, &pol.pol_flows, ftmp) {
		RB_REMOVE(iked_flows, &pol.pol_flows, flow);
		free(flow);
	}
	free(name);
	free(srcid);
	free(dstid);
	return (ret);
}

static int
create_flow(struct iked_policy *pol, int proto, struct ipsec_addr_wrap *ipa,
    struct ipsec_addr_wrap *ipb)
{
	struct iked_flow	*flow;
	struct ipsec_addr_wrap	*ippn;

	if (ipa->af != ipb->af) {
		yyerror("cannot mix different address families.");
		return (-1);
	}

	if ((flow = calloc(1, sizeof(struct iked_flow))) == NULL)
		fatalx("%s: failed to alloc flow.", __func__);

	memcpy(&flow->flow_src.addr, &ipa->address,
	    sizeof(ipa->address));
	flow->flow_src.addr_af = ipa->af;
	flow->flow_src.addr_mask = ipa->mask;
	flow->flow_src.addr_net = ipa->netaddress;
	flow->flow_src.addr_port = ipa->port;

	memcpy(&flow->flow_dst.addr, &ipb->address,
	    sizeof(ipb->address));
	flow->flow_dst.addr_af = ipb->af;
	flow->flow_dst.addr_mask = ipb->mask;
	flow->flow_dst.addr_net = ipb->netaddress;
	flow->flow_dst.addr_port = ipb->port;

	ippn = ipa->srcnat;
	if (ippn) {
		memcpy(&flow->flow_prenat.addr, &ippn->address,
		    sizeof(ippn->address));
		flow->flow_prenat.addr_af = ippn->af;
		flow->flow_prenat.addr_mask = ippn->mask;
		flow->flow_prenat.addr_net = ippn->netaddress;
	} else {
		flow->flow_prenat.addr_af = 0;
	}

	flow->flow_dir = IPSP_DIRECTION_OUT;
	flow->flow_ipproto = proto;
	flow->flow_saproto = pol->pol_saproto;
	flow->flow_rdomain = pol->pol_rdomain;

	if (RB_INSERT(iked_flows, &pol->pol_flows, flow) == NULL)
		pol->pol_nflows++;
	else {
		warnx("create_ike: duplicate flow");
		free(flow);
	}

	return (0);
}

static int
expand_flows(struct iked_policy *pol, int proto, struct ipsec_addr_wrap *src,
    struct ipsec_addr_wrap *dst)
{
	struct ipsec_addr_wrap	*ipa = NULL, *ipb = NULL;
	int			 ret = -1;
	int			 srcaf, dstaf;

	srcaf = src->af;
	dstaf = dst->af;

	if (src->af == AF_UNSPEC &&
	    dst->af == AF_UNSPEC) {
		/* Need both IPv4 and IPv6 flows */
		src->af = dst->af = AF_INET;
		ipa = expand_keyword(src);
		ipb = expand_keyword(dst);
		if (!ipa || !ipb)
			goto done;
		if (create_flow(pol, proto, ipa, ipb))
			goto done;

		iaw_free(ipa);
		iaw_free(ipb);
		src->af = dst->af = AF_INET6;
		ipa = expand_keyword(src);
		ipb = expand_keyword(dst);
		if (!ipa || !ipb)
			goto done;
		if (create_flow(pol, proto, ipa, ipb))
			goto done;
	} else if (src->af == AF_UNSPEC) {
		src->af = dst->af;
		ipa = expand_keyword(src);
		if (!ipa)
			goto done;
		if (create_flow(pol, proto, ipa, dst))
			goto done;
	} else if (dst->af == AF_UNSPEC) {
		dst->af = src->af;
		ipa = expand_keyword(dst);
		if (!ipa)
			goto done;
		if (create_flow(pol, proto, src, ipa))
			goto done;
	} else if (create_flow(pol, proto, src, dst))
		goto done;
	ret = 0;
 done:
	src->af = srcaf;
	dst->af = dstaf;
	iaw_free(ipa);
	iaw_free(ipb);
	return (ret);
}

static struct ipsec_addr_wrap *
expand_keyword(struct ipsec_addr_wrap *ip)
{
	switch(ip->af) {
	case AF_INET:
		switch(ip->type) {
		case IPSEC_ADDR_ANY:
			return (host("0.0.0.0/0"));
		case IPSEC_ADDR_DYNAMIC:
			return (host("0.0.0.0"));
		}
		break;
	case AF_INET6:
		switch(ip->type) {
		case IPSEC_ADDR_ANY:
			return (host("::/0"));
		case IPSEC_ADDR_DYNAMIC:
			return (host("::"));
		}
	}
	return (NULL);
}

int
create_user(const char *user, const char *pass)
{
	struct iked_user	 usr;

	bzero(&usr, sizeof(usr));

	if (*user == '\0' || (strlcpy(usr.usr_name, user,
	    sizeof(usr.usr_name)) >= sizeof(usr.usr_name))) {
		yyerror("invalid user name");
		return (-1);
	}
	if (*pass == '\0' || (strlcpy(usr.usr_pass, pass,
	    sizeof(usr.usr_pass)) >= sizeof(usr.usr_pass))) {
		yyerror("invalid password");
		explicit_bzero(&usr, sizeof usr);	/* zap partial password */
		return (-1);
	}

	config_setuser(env, &usr, PROC_IKEV2);

	rules++;

	explicit_bzero(&usr, sizeof usr);
	return (0);
}

void
iaw_free(struct ipsec_addr_wrap *head)
{
	struct ipsec_addr_wrap *n, *cur;

	if (head == NULL)
		return;

	for (n = head; n != NULL; ) {
		cur = n;
		n = n->next;
		if (cur->srcnat != NULL) {
			free(cur->srcnat->name);
			free(cur->srcnat);
		}
		free(cur->name);
		free(cur);
	}
}
