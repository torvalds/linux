/*	$OpenBSD: parse.y,v 1.184 2025/04/30 03:54:09 tb Exp $	*/

/*
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
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
#include <limits.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "ipsecctl.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
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
int		 yywarn(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 lgetc(int);
int		 lungetc(int);
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
int		 cmdline_symset(char *);

#define KEYSIZE_LIMIT	1024

static struct ipsecctl	*ipsec = NULL;
static int		 debug = 0;

const struct ipsec_xf authxfs[] = {
	{ "unknown",		AUTHXF_UNKNOWN,		0,	0 },
	{ "none",		AUTHXF_NONE,		0,	0 },
	{ "hmac-md5",		AUTHXF_HMAC_MD5,	16,	0 },
	{ "hmac-ripemd160",	AUTHXF_HMAC_RIPEMD160,	20,	0 },
	{ "hmac-sha1",		AUTHXF_HMAC_SHA1,	20,	0 },
	{ "hmac-sha2-256",	AUTHXF_HMAC_SHA2_256,	32,	0 },
	{ "hmac-sha2-384",	AUTHXF_HMAC_SHA2_384,	48,	0 },
	{ "hmac-sha2-512",	AUTHXF_HMAC_SHA2_512,	64,	0 },
	{ NULL,			0,			0,	0 },
};

const struct ipsec_xf encxfs[] = {
	{ "unknown",		ENCXF_UNKNOWN,		0,	0,	0, 0 },
	{ "none",		ENCXF_NONE,		0,	0,	0, 0 },
	{ "3des-cbc",		ENCXF_3DES_CBC,		24,	24,	0, 0 },
	{ "aes",		ENCXF_AES,		16,	32,	0, 0 },
	{ "aes-128",		ENCXF_AES_128,		16,	16,	0, 0 },
	{ "aes-192",		ENCXF_AES_192,		24,	24,	0, 0 },
	{ "aes-256",		ENCXF_AES_256,		32,	32,	0, 0 },
	{ "aesctr",		ENCXF_AESCTR,		16+4,	32+4,	0, 1 },
	{ "aes-128-ctr",	ENCXF_AES_128_CTR,	16+4,	16+4,	0, 1 },
	{ "aes-192-ctr",	ENCXF_AES_192_CTR,	24+4,	24+4,	0, 1 },
	{ "aes-256-ctr",	ENCXF_AES_256_CTR,	32+4,	32+4,	0, 1 },
	{ "aes-128-gcm",	ENCXF_AES_128_GCM,	16+4,	16+4,	1, 1 },
	{ "aes-192-gcm",	ENCXF_AES_192_GCM,	24+4,	24+4,	1, 1 },
	{ "aes-256-gcm",	ENCXF_AES_256_GCM,	32+4,	32+4,	1, 1 },
	{ "aes-128-gmac",	ENCXF_AES_128_GMAC,	16+4,	16+4,	1, 1 },
	{ "aes-192-gmac",	ENCXF_AES_192_GMAC,	24+4,	24+4,	1, 1 },
	{ "aes-256-gmac",	ENCXF_AES_256_GMAC,	32+4,	32+4,	1, 1 },
	{ "blowfish",		ENCXF_BLOWFISH,		5,	56,	0, 0 },
	{ "cast128",		ENCXF_CAST128,		5,	16,	0, 0 },
	{ "chacha20-poly1305",	ENCXF_CHACHA20_POLY1305, 32+4,	32+4,	1, 1 },
	{ "null",		ENCXF_NULL,		0,	0,	0, 0 },
	{ NULL,			0,			0,	0,	0, 0 },
};

const struct ipsec_xf compxfs[] = {
	{ "unknown",		COMPXF_UNKNOWN,		0,	0 },
	{ "deflate",		COMPXF_DEFLATE,		0,	0 },
	{ NULL,			0,			0,	0 },
};

const struct ipsec_xf groupxfs[] = {
	{ "unknown",		GROUPXF_UNKNOWN,	0,	0 },
	{ "none",		GROUPXF_NONE,		0,	0 },
	{ "modp768",		GROUPXF_1,		768,	0 },
	{ "grp1",		GROUPXF_1,		768,	0 },
	{ "modp1024",		GROUPXF_2,		1024,	0 },
	{ "grp2",		GROUPXF_2,		1024,	0 },
	{ "modp1536",		GROUPXF_5,		1536,	0 },
	{ "grp5",		GROUPXF_5,		1536,	0 },
	{ "modp2048",		GROUPXF_14,		2048,	0 },
	{ "grp14",		GROUPXF_14,		2048,	0 },
	{ "modp3072",		GROUPXF_15,		3072,	0 },
	{ "grp15",		GROUPXF_15,		3072,	0 },
	{ "modp4096",		GROUPXF_16,		4096,	0 },
	{ "grp16",		GROUPXF_16,		4096,	0 },
	{ "modp6144",		GROUPXF_17,		6144,	0 },
	{ "grp17",		GROUPXF_17,		6144,	0 },
	{ "modp8192",		GROUPXF_18,		8192,	0 },
	{ "grp18",		GROUPXF_18,		8192,	0 },
	{ "ecp256",		GROUPXF_19,		256,	0 },
	{ "grp19",		GROUPXF_19,		256,	0 },
	{ "ecp384",		GROUPXF_20,		384,	0 },
	{ "grp20",		GROUPXF_20,		384,	0 },
	{ "ecp521",		GROUPXF_21,		521,	0 },
	{ "grp21",		GROUPXF_21,		521,	0 },
	{ "ecp224",		GROUPXF_26,		224,	0 },
	{ "grp26",		GROUPXF_26,		224,	0 },
	{ "bp224",		GROUPXF_27,		224,	0 },
	{ "grp27",		GROUPXF_27,		224,	0 },
	{ "bp256",		GROUPXF_28,		256,	0 },
	{ "grp28",		GROUPXF_28,		256,	0 },
	{ "bp384",		GROUPXF_29,		384,	0 },
	{ "grp29",		GROUPXF_29,		384,	0 },
	{ "bp512",		GROUPXF_30,		512,	0 },
	{ "grp30",		GROUPXF_30,		512,	0 },
	{ NULL,			0,			0,	0 },
};

int			 atoul(char *, u_long *);
int			 atospi(char *, u_int32_t *);
u_int8_t		 x2i(unsigned char *);
struct ipsec_key	*parsekey(unsigned char *, size_t);
struct ipsec_key	*parsekeyfile(char *);
struct ipsec_addr_wrap	*host(const char *);
struct ipsec_addr_wrap	*host_v6(const char *, int);
struct ipsec_addr_wrap	*host_v4(const char *, int);
struct ipsec_addr_wrap	*host_dns(const char *, int);
struct ipsec_addr_wrap	*host_if(const char *, int);
struct ipsec_addr_wrap	*host_any(void);
void			 ifa_load(void);
int			 ifa_exists(const char *);
struct ipsec_addr_wrap	*ifa_lookup(const char *ifa_name);
struct ipsec_addr_wrap	*ifa_grouplookup(const char *);
void			 set_ipmask(struct ipsec_addr_wrap *, u_int8_t);
const struct ipsec_xf	*parse_xf(const char *, const struct ipsec_xf *);
struct ipsec_lifetime	*parse_life(const char *);
struct ipsec_transforms *copytransforms(const struct ipsec_transforms *);
struct ipsec_lifetime	*copylife(const struct ipsec_lifetime *);
struct ipsec_auth	*copyipsecauth(const struct ipsec_auth *);
struct ike_auth		*copyikeauth(const struct ike_auth *);
struct ipsec_key	*copykey(struct ipsec_key *);
struct ipsec_addr_wrap	*copyhost(const struct ipsec_addr_wrap *);
char			*copytag(const char *);
struct ipsec_rule	*copyrule(struct ipsec_rule *);
int			 validate_af(struct ipsec_addr_wrap *,
			     struct ipsec_addr_wrap *);
int			 validate_sa(u_int32_t, u_int8_t,
			     struct ipsec_transforms *, struct ipsec_key *,
			     struct ipsec_key *, u_int8_t);
struct ipsec_rule	*create_sa(u_int8_t, u_int8_t, struct ipsec_hosts *,
			     u_int32_t, u_int8_t, u_int16_t,
			     struct ipsec_transforms *,
			     struct ipsec_key *, struct ipsec_key *);
struct ipsec_rule	*reverse_sa(struct ipsec_rule *, u_int32_t,
			     struct ipsec_key *, struct ipsec_key *);
struct ipsec_rule	*create_sabundle(struct ipsec_addr_wrap *, u_int8_t,
			     u_int32_t, struct ipsec_addr_wrap *, u_int8_t,
			     u_int32_t);
struct ipsec_rule	*create_flow(u_int8_t, u_int8_t, struct ipsec_hosts *,
			     u_int8_t, char *, char *, u_int8_t);
int			 set_rule_peers(struct ipsec_rule *r,
			     struct ipsec_hosts *peers);
void			 expand_any(struct ipsec_addr_wrap *);
int			 expand_rule(struct ipsec_rule *, struct ipsec_hosts *,
			     u_int8_t, u_int32_t, struct ipsec_key *,
			     struct ipsec_key *, char *);
struct ipsec_rule	*reverse_rule(struct ipsec_rule *);
struct ipsec_rule	*create_ike(u_int8_t, struct ipsec_hosts *,
			     struct ike_mode *, struct ike_mode *, u_int8_t,
			     u_int8_t, u_int8_t, char *, char *,
			     struct ike_auth *, char *);
int			 add_sabundle(struct ipsec_rule *, char *);
int			 get_id_type(char *);

struct ipsec_transforms *ipsec_transforms;

typedef struct {
	union {
		int64_t	 	 number;
		uint32_t	 unit;
		u_int8_t	 ikemode;
		u_int8_t	 dir;
		u_int8_t	 satype;	/* encapsulating prococol */
		u_int8_t	 proto;		/* encapsulated protocol */
		u_int8_t	 tmode;
		char		*string;
		u_int16_t	 port;
		struct ipsec_hosts hosts;
		struct ipsec_hosts peers;
		struct ipsec_addr_wrap *anyhost;
		struct ipsec_addr_wrap *singlehost;
		struct ipsec_addr_wrap *host;
		struct {
			char *srcid;
			char *dstid;
		} ids;
		char		*id;
		u_int8_t	 type;
		struct ike_auth	 ikeauth;
		struct {
			u_int32_t	spiout;
			u_int32_t	spiin;
		} spis;
		struct {
			u_int8_t	encap;
			u_int16_t	port;
		} udpencap;
		struct {
			struct ipsec_key *keyout;
			struct ipsec_key *keyin;
		} authkeys;
		struct {
			struct ipsec_key *keyout;
			struct ipsec_key *keyin;
		} enckeys;
		struct {
			struct ipsec_key *keyout;
			struct ipsec_key *keyin;
		} keys;
		struct ipsec_transforms *transforms;
		struct ipsec_lifetime	*life;
		struct ike_mode		*mode;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	FLOW FROM ESP AH IN PEER ON OUT TO SRCID DSTID RSA PSK TCPMD5 SPI
%token	AUTHKEY ENCKEY FILENAME AUTHXF ENCXF ERROR IKE MAIN QUICK AGGRESSIVE
%token	PASSIVE ACTIVE ANY IPIP IPCOMP COMPXF TUNNEL TRANSPORT DYNAMIC LIFETIME
%token	TYPE DENY BYPASS LOCAL PROTO USE ACQUIRE REQUIRE DONTACQ GROUP PORT TAG
%token	INCLUDE BUNDLE UDPENCAP INTERFACE
%token	<v.string>		STRING
%token	<v.number>		NUMBER
%type	<v.unit>		iface
%type	<v.string>		string
%type	<v.dir>			dir
%type	<v.satype>		satype
%type	<v.proto>		proto
%type	<v.number>		protoval
%type	<v.tmode>		tmode
%type	<v.hosts>		hosts
%type	<v.port>		port
%type	<v.number>		portval
%type	<v.peers>		peers
%type	<v.anyhost>		anyhost
%type	<v.singlehost>		singlehost
%type	<v.host>		host host_list host_spec
%type	<v.ids>			ids
%type	<v.id>			id
%type	<v.spis>		spispec
%type	<v.udpencap>		udpencap
%type	<v.authkeys>		authkeyspec
%type	<v.enckeys>		enckeyspec
%type	<v.string>		bundlestring
%type	<v.keys>		keyspec
%type	<v.transforms>		transforms
%type	<v.ikemode>		ikemode
%type	<v.ikeauth>		ikeauth
%type	<v.type>		type
%type	<v.life>		lifetime
%type	<v.mode>		phase1mode phase2mode
%type	<v.string>		tag
%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar ikerule '\n'
		| grammar flowrule '\n'
		| grammar sarule '\n'
		| grammar tcpmd5rule '\n'
		| grammar varset '\n'
		| grammar error '\n'		{ file->errors++; }
		;

comma		: ','
		| /* empty */
		;

include		: INCLUDE STRING		{
			struct file	*nfile;

			if ((nfile = pushfile($2, 0)) == NULL) {
				yyerror("failed to include file %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			file = nfile;
			lungetc('\n');
		}
		;

tcpmd5rule	: TCPMD5 hosts spispec authkeyspec	{
			struct ipsec_rule	*r;

			r = create_sa(IPSEC_TCPMD5, IPSEC_TRANSPORT, &$2,
			    $3.spiout, 0, 0, NULL, $4.keyout, NULL);
			if (r == NULL)
				YYERROR;

			if (expand_rule(r, NULL, 0, $3.spiin, $4.keyin, NULL,
			    NULL))
				errx(1, "tcpmd5rule: expand_rule");
		}
		;

sarule		: satype tmode hosts spispec udpencap transforms authkeyspec
		    enckeyspec bundlestring {
			struct ipsec_rule	*r;

			r = create_sa($1, $2, &$3, $4.spiout, $5.encap, $5.port,
			    $6, $7.keyout, $8.keyout);
			if (r == NULL)
				YYERROR;

			if (expand_rule(r, NULL, 0, $4.spiin, $7.keyin,
			    $8.keyin, $9))
				errx(1, "sarule: expand_rule");
		}
		;

flowrule	: FLOW satype dir proto hosts peers ids type {
			struct ipsec_rule	*r;

			r = create_flow($3, $4, &$5, $2, $7.srcid,
			    $7.dstid, $8);
			if (r == NULL)
				YYERROR;

			if (expand_rule(r, &$6, $3, 0, NULL, NULL, NULL))
				errx(1, "flowrule: expand_rule");
		}
		;

ikerule		: IKE ikemode satype tmode proto hosts peers
		    phase1mode phase2mode ids ikeauth tag {
			struct ipsec_rule	*r;

			r = create_ike($5, &$6, $8, $9, $3, $4, $2,
			    $10.srcid, $10.dstid, &$11, $12);
			if (r == NULL)
				YYERROR;

			if (expand_rule(r, &$7, 0, 0, NULL, NULL, NULL))
				errx(1, "ikerule: expand_rule");
		}

		/* ike interface sec0 local $h_self peer $h_s2s1 ... */
		| IKE ikemode iface peers
		    phase1mode phase2mode ids ikeauth {
			uint8_t			 proto = 0; // IPPROTO_IPIP;
			struct ipsec_hosts	 hosts;
			struct ike_mode		*phase1mode = $5;
			struct ike_mode		*phase2mode = $6;
			uint8_t			 satype = IPSEC_ESP;
			uint8_t			 tmode = IPSEC_TUNNEL;
			uint8_t			 mode = $2;
			struct ike_auth		*authtype = &$8;
			char			*tag = NULL;

			struct ipsec_rule	*r;

			hosts.src = host_v4("0.0.0.0/0", 1);
			hosts.sport = htons(0);
			hosts.dst = host_v4("0.0.0.0/0", 1);
			hosts.dport = htons(0);

			r = create_ike(proto, &hosts, phase1mode, phase2mode,
			    satype, tmode, mode, $7.srcid, $7.dstid,
			    authtype, tag);
			if (r == NULL) {
				YYERROR;
			}

			r->flags |= IPSEC_RULE_F_IFACE;
			r->iface = $3;

			if (expand_rule(r, &$4, 0, 0, NULL, NULL, NULL))
				errx(1, "ikerule: expand interface rule");

		}
		;

satype		: /* empty */			{ $$ = IPSEC_ESP; }
		| ESP				{ $$ = IPSEC_ESP; }
		| AH				{ $$ = IPSEC_AH; }
		| IPCOMP			{ $$ = IPSEC_IPCOMP; }
		| IPIP				{ $$ = IPSEC_IPIP; }
		;

proto		: /* empty */			{ $$ = 0; }
		| PROTO protoval		{ $$ = $2; }
		| PROTO ESP 			{ $$ = IPPROTO_ESP; }
		| PROTO AH			{ $$ = IPPROTO_AH; }
		;

protoval	: STRING			{
			struct protoent *p;

			p = getprotobyname($1);
			if (p == NULL) {
				yyerror("unknown protocol: %s", $1);
				YYERROR;
			}
			$$ = p->p_proto;
			free($1);
		}
		| NUMBER			{
			if ($1 > 255 || $1 < 0) {
				yyerror("protocol outside range");
				YYERROR;
			}
		}
		;

tmode		: /* empty */			{ $$ = IPSEC_TUNNEL; }
		| TUNNEL			{ $$ = IPSEC_TUNNEL; }
		| TRANSPORT			{ $$ = IPSEC_TRANSPORT; }
		;

dir		: /* empty */			{ $$ = IPSEC_INOUT; }
		| IN				{ $$ = IPSEC_IN; }
		| OUT				{ $$ = IPSEC_OUT; }
		;

hosts		: FROM host port TO host port		{
			struct ipsec_addr_wrap *ipa;
			for (ipa = $5; ipa; ipa = ipa->next) {
				if (ipa->srcnat) {
					yyerror("no flow NAT support for"
					    " destination network: %s", ipa->name);
					YYERROR;
				}
			}
			$$.src = $2;
			$$.sport = $3;
			$$.dst = $5;
			$$.dport = $6;
		}
		| TO host port FROM host port		{
			struct ipsec_addr_wrap *ipa;
			for (ipa = $2; ipa; ipa = ipa->next) {
				if (ipa->srcnat) {
					yyerror("no flow NAT support for"
					    " destination network: %s", ipa->name);
					YYERROR;
				}
			}
			$$.src = $5;
			$$.sport = $6;
			$$.dst = $2;
			$$.dport = $3;
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
		| PEER anyhost LOCAL singlehost		{
			$$.dst = $2;
			$$.src = $4;
		}
		| LOCAL singlehost PEER anyhost		{
			$$.dst = $4;
			$$.src = $2;
		}
		| PEER anyhost				{
			$$.dst = $2;
			$$.src = NULL;
		}
		| LOCAL singlehost			{
			$$.dst = NULL;
			$$.src = $2;
		}
		;

anyhost		: singlehost			{ $$ = $1; }
		| ANY				{
			$$ = host_any();
		}

singlehost	: /* empty */			{ $$ = NULL; }
		| STRING			{
			if (($$ = host($1)) == NULL) {
				free($1);
				yyerror("could not parse host specification");
				YYERROR;
			}
			free($1);
		}
		;

host_list	: host				{ $$ = $1; }
		| host_list comma host		{
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
			if ($3->af != $1->af) {
				yyerror("Flow NAT address family mismatch");
				YYERROR;
			}
			$$ = $1;
			$$->srcnat = $3;
		}
		| ANY				{
			$$ = host_any();
		}
		| '{' host_list '}'		{ $$ = $2; }
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

type		: /* empty */			{
			$$ = TYPE_UNKNOWN;
		}
		| TYPE USE			{
			$$ = TYPE_USE;
		}
		| TYPE ACQUIRE			{
			$$ = TYPE_ACQUIRE;
		}
		| TYPE REQUIRE			{
			$$ = TYPE_REQUIRE;
		}
		| TYPE DENY			{
			$$ = TYPE_DENY;
		}
		| TYPE BYPASS			{
			$$ = TYPE_BYPASS;
		}
		| TYPE DONTACQ			{
			$$ = TYPE_DONTACQ;
		}
		;

id		: STRING			{ $$ = $1; }
		;

spispec		: SPI STRING			{
			u_int32_t	 spi;
			char		*p = strchr($2, ':');

			if (p != NULL) {
				*p++ = 0;

				if (atospi(p, &spi) == -1) {
					free($2);
					YYERROR;
				}
				$$.spiin = spi;
			} else
				$$.spiin = 0;

			if (atospi($2, &spi) == -1) {
				free($2);
				YYERROR;
			}
			$$.spiout = spi;


			free($2);
		}
		| SPI NUMBER			{
			if ($2 > UINT_MAX || $2 < 0) {
				yyerror("%lld not a valid spi", $2);
				YYERROR;
			}
			if ($2 >= SPI_RESERVED_MIN && $2 <= SPI_RESERVED_MAX) {
				yyerror("%lld within reserved spi range", $2);
				YYERROR;
			}

			$$.spiin = 0;
			$$.spiout = $2;
		}
		;

udpencap	: /* empty */				{
			$$.encap = 0;
		}
		| UDPENCAP				{
			$$.encap = 1;
			$$.port = 0;
		}
		| UDPENCAP PORT NUMBER			{
			$$.encap = 1;
			$$.port = $3;
		}
		;

transforms	:					{
			if ((ipsec_transforms = calloc(1,
			    sizeof(struct ipsec_transforms))) == NULL)
				err(1, "transforms: calloc");
		}
		    transforms_l
			{ $$ = ipsec_transforms; }
		| /* empty */				{
			if (($$ = calloc(1,
			    sizeof(struct ipsec_transforms))) == NULL)
				err(1, "transforms: calloc");
		}
		;

transforms_l	: transforms_l transform
		| transform
		;

transform	: AUTHXF STRING			{
			if (ipsec_transforms->authxf)
				yyerror("auth already set");
			else {
				ipsec_transforms->authxf = parse_xf($2,
				    authxfs);
				if (!ipsec_transforms->authxf)
					yyerror("%s not a valid transform", $2);
			}
		}
		| ENCXF STRING			{
			if (ipsec_transforms->encxf)
				yyerror("enc already set");
			else {
				ipsec_transforms->encxf = parse_xf($2, encxfs);
				if (!ipsec_transforms->encxf)
					yyerror("%s not a valid transform", $2);
			}
		}
		| COMPXF STRING			{
			if (ipsec_transforms->compxf)
				yyerror("comp already set");
			else {
				ipsec_transforms->compxf = parse_xf($2,
				    compxfs);
				if (!ipsec_transforms->compxf)
					yyerror("%s not a valid transform", $2);
			}
		}
		| GROUP STRING			{
			if (ipsec_transforms->groupxf)
				yyerror("group already set");
			else {
				ipsec_transforms->groupxf = parse_xf($2,
				    groupxfs);
				if (!ipsec_transforms->groupxf)
					yyerror("%s not a valid transform", $2);
			}
		}
		;

phase1mode	: /* empty */	{
			struct ike_mode		*p1;

			/* We create just an empty main mode */
			if ((p1 = calloc(1, sizeof(struct ike_mode))) == NULL)
				err(1, "phase1mode: calloc");
			p1->ike_exch = IKE_MM;
			$$ = p1;
		}
		| MAIN transforms lifetime		{
			struct ike_mode *p1;

			if ((p1 = calloc(1, sizeof(struct ike_mode))) == NULL)
				err(1, "phase1mode: calloc");
			p1->xfs = $2;
			p1->life = $3;
			p1->ike_exch = IKE_MM;
			$$ = p1;
		}
		| AGGRESSIVE transforms lifetime	{
			struct ike_mode	*p1;

			if ((p1 = calloc(1, sizeof(struct ike_mode))) == NULL)
				err(1, "phase1mode: calloc");
			p1->xfs = $2;
			p1->life = $3;
			p1->ike_exch = IKE_AM;
			$$ = p1;
		}
		;

phase2mode	: /* empty */	{
			struct ike_mode		*p2;

			/* We create just an empty quick mode */
			if ((p2 = calloc(1, sizeof(struct ike_mode))) == NULL)
				err(1, "phase2mode: calloc");
			p2->ike_exch = IKE_QM;
			$$ = p2;
		}
		| QUICK transforms lifetime	{
			struct ike_mode	*p2;

			if ((p2 = calloc(1, sizeof(struct ike_mode))) == NULL)
				err(1, "phase2mode: calloc");
			p2->xfs = $2;
			p2->life = $3;
			p2->ike_exch = IKE_QM;
			$$ = p2;
		}
		;

lifetime	: /* empty */			{
			struct ipsec_lifetime *life;

			/* We create just an empty transform */
			if ((life = calloc(1, sizeof(struct ipsec_lifetime)))
			    == NULL)
				err(1, "life: calloc");
			life->lt_seconds = -1;
			life->lt_bytes = -1;
			$$ = life;
		}
		| LIFETIME NUMBER		{
			struct ipsec_lifetime *life;

			if ((life = calloc(1, sizeof(struct ipsec_lifetime)))
			    == NULL)
				err(1, "life: calloc");
			life->lt_seconds = $2;
			life->lt_bytes = -1;
			$$ = life;
		}
		| LIFETIME STRING		{
			$$ = parse_life($2);
		}
		;

authkeyspec	: /* empty */			{
			$$.keyout = NULL;
			$$.keyin = NULL;
		}
		| AUTHKEY keyspec		{
			$$.keyout = $2.keyout;
			$$.keyin = $2.keyin;
		}
		;

enckeyspec	: /* empty */			{
			$$.keyout = NULL;
			$$.keyin = NULL;
		}
		| ENCKEY keyspec		{
			$$.keyout = $2.keyout;
			$$.keyin = $2.keyin;
		}
		;

bundlestring	: /* empty */			{ $$ = NULL; }
		| BUNDLE STRING			{ $$ = $2; }
		;

keyspec		: STRING			{
			unsigned char	*hex;
			unsigned char	*p = strchr($1, ':');

			if (p != NULL ) {
				*p++ = 0;

				if (!strncmp(p, "0x", 2))
					p += 2;
				$$.keyin = parsekey(p, strlen(p));
			} else
				$$.keyin = NULL;

			hex = $1;
			if (!strncmp(hex, "0x", 2))
				hex += 2;
			$$.keyout = parsekey(hex, strlen(hex));

			free($1);
		}
		| FILENAME STRING		{
			unsigned char	*p = strchr($2, ':');

			if (p != NULL) {
				*p++ = 0;
				$$.keyin = parsekeyfile(p);
			}
			$$.keyout = parsekeyfile($2);
			free($2);
		}
		;

ikemode		: /* empty */			{ $$ = IKE_ACTIVE; }
		| PASSIVE			{ $$ = IKE_PASSIVE; }
		| DYNAMIC			{ $$ = IKE_DYNAMIC; }
		| ACTIVE			{ $$ = IKE_ACTIVE; }
		;

ikeauth		: /* empty */			{
			$$.type = IKE_AUTH_RSA;
			$$.string = NULL;
		}
		| RSA				{
			$$.type = IKE_AUTH_RSA;
			$$.string = NULL;
		}
		| PSK STRING			{
			$$.type = IKE_AUTH_PSK;
			if (($$.string = strdup($2)) == NULL)
				err(1, "ikeauth: strdup");
		}
		;

tag		: /* empty */
		{
			$$ = NULL;
		}
		| TAG STRING
		{
			$$ = $2;
		}
		;

iface		: INTERFACE STRING		{
			static const char prefix[] = "sec";
			const char *errstr = NULL;
			size_t len, plen;

			plen = strlen(prefix);
			len = strlen($2);

			if (len <= plen || memcmp($2, prefix, plen) != 0) {
				yyerror("invalid %s interface name", prefix);
				free($2);
				YYERROR;
			}

			$$ = strtonum($2 + plen, 0, UINT_MAX, &errstr);
			free($2);
			if (errstr != NULL) {
				yyerror("invalid %s interface unit: %s",
				    prefix, errstr);
				YYERROR;
			}
		}
		;

string		: string STRING
		{
			if (asprintf(&$$, "%s %s", $1, $2) == -1)
				err(1, "string: asprintf");
			free($1);
			free($2);
		}
		| STRING
		;

varset		: STRING '=' string
		{
			char *s = $1;
			if (ipsec->opts & IPSECCTL_OPT_VERBOSE)
				printf("%s = \"%s\"\n", $1, $3);
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

%%

struct keywords {
	const char	*k_name;
	int		 k_val;
};

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
yywarn(const char *fmt, ...)
{
	va_list		 ap;

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
		{ "acquire",		ACQUIRE },
		{ "active",		ACTIVE },
		{ "aggressive",		AGGRESSIVE },
		{ "ah",			AH },
		{ "any",		ANY },
		{ "auth",		AUTHXF },
		{ "authkey",		AUTHKEY },
		{ "bundle",		BUNDLE },
		{ "bypass",		BYPASS },
		{ "comp",		COMPXF },
		{ "deny",		DENY },
		{ "dontacq",		DONTACQ },
		{ "dstid",		DSTID },
		{ "dynamic",		DYNAMIC },
		{ "enc",		ENCXF },
		{ "enckey",		ENCKEY },
		{ "esp",		ESP },
		{ "file",		FILENAME },
		{ "flow",		FLOW },
		{ "from",		FROM },
		{ "group",		GROUP },
		{ "ike",		IKE },
		{ "in",			IN },
		{ "include",		INCLUDE },
		{ "interface",		INTERFACE },
		{ "ipcomp",		IPCOMP },
		{ "ipip",		IPIP },
		{ "lifetime",		LIFETIME },
		{ "local",		LOCAL },
		{ "main",		MAIN },
		{ "out",		OUT },
		{ "passive",		PASSIVE },
		{ "peer",		PEER },
		{ "port",		PORT },
		{ "proto",		PROTO },
		{ "psk",		PSK },
		{ "quick",		QUICK },
		{ "require",		REQUIRE },
		{ "rsa",		RSA },
		{ "spi",		SPI },
		{ "srcid",		SRCID },
		{ "tag",		TAG },
		{ "tcpmd5",		TCPMD5 },
		{ "to",			TO },
		{ "transport",		TRANSPORT },
		{ "tunnel",		TUNNEL },
		{ "type",		TYPE },
		{ "udpencap",		UDPENCAP },
		{ "use",		USE }
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

#define MAXPUSHBACK	128

char	*parsebuf;
int	 parseindex;
char	 pushback_buffer[MAXPUSHBACK];
int	 pushback_index = 0;

int
lgetc(int quotec)
{
	int		c, next;

	if (parsebuf) {
		/* Read character from the parsebuffer instead of input. */
		if (parseindex >= 0) {
			c = (unsigned char)parsebuf[parseindex++];
			if (c != '\0')
				return (c);
			parsebuf = NULL;
		} else
			parseindex++;
	}

	if (pushback_index)
		return ((unsigned char)pushback_buffer[--pushback_index]);

	if (quotec) {
		if ((c = getc(file->stream)) == EOF) {
			yyerror("reached end of file while parsing quoted string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = getc(file->stream)) == '\\') {
		next = getc(file->stream);
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}

	while (c == EOF) {
		if (file == topfile || popfile() == EOF)
			return (EOF);
		c = getc(file->stream);
	}
	return (c);
}

int
lungetc(int c)
{
	if (c == EOF)
		return (EOF);
	if (parsebuf) {
		parseindex--;
		if (parseindex >= 0)
			return (c);
	}
	if (pushback_index + 1 >= MAXPUSHBACK)
		return (EOF);
	pushback_buffer[pushback_index++] = c;
	return (c);
}

int
findeol(void)
{
	int	c;

	parsebuf = NULL;

	/* skip to either EOF or the first real EOL */
	while (1) {
		if (pushback_index)
			c = (unsigned char)pushback_buffer[--pushback_index];
		else
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
	if (c == '$' && parsebuf == NULL) {
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
		parsebuf = val;
		parseindex = 0;
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
	nfile->lineno = 1;
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
	free(file);
	file = prev;

	return (file ? 0 : EOF);
}

int
parse_rules(const char *filename, struct ipsecctl *ipsecx)
{
	struct sym	*sym;
	int		 errors = 0;

	ipsec = ipsecx;

	if ((file = pushfile(filename, 1)) == NULL) {
		return (-1);
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	while ((sym = TAILQ_FIRST(&symhead))) {
		if ((ipsec->opts & IPSECCTL_OPT_VERBOSE2) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		free(sym->nam);
		free(sym->val);
		TAILQ_REMOVE(&symhead, sym, entry);
		free(sym);
	}

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

int
atoul(char *s, u_long *ulvalp)
{
	u_long	 ulval;
	char	*ep;

	errno = 0;
	ulval = strtoul(s, &ep, 0);
	if (s[0] == '\0' || *ep != '\0')
		return (-1);
	if (errno == ERANGE && ulval == ULONG_MAX)
		return (-1);
	*ulvalp = ulval;
	return (0);
}

int
atospi(char *s, u_int32_t *spivalp)
{
	unsigned long	ulval;

	if (atoul(s, &ulval) == -1)
		return (-1);
	if (ulval > UINT_MAX) {
		yyerror("%lu not a valid spi", ulval);
		return (-1);
	}
	if (ulval >= SPI_RESERVED_MIN && ulval <= SPI_RESERVED_MAX) {
		yyerror("%lu within reserved spi range", ulval);
		return (-1);
	}
	*spivalp = ulval;
	return (0);
}

u_int8_t
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
	return ((u_int8_t)strtoul(ss, NULL, 16));
}

struct ipsec_key *
parsekey(unsigned char *hexkey, size_t len)
{
	struct ipsec_key *key;
	int		  i;

	key = calloc(1, sizeof(struct ipsec_key));
	if (key == NULL)
		err(1, "%s", __func__);

	key->len = len / 2;
	key->data = calloc(key->len, sizeof(u_int8_t));
	if (key->data == NULL)
		err(1, "%s", __func__);

	for (i = 0; i < (int)key->len; i++)
		key->data[i] = x2i(hexkey + 2 * i);

	return (key);
}

struct ipsec_key *
parsekeyfile(char *filename)
{
	struct stat	 sb;
	int		 fd;
	unsigned char	*hex;

	if ((fd = open(filename, O_RDONLY)) < 0)
		err(1, "open %s", filename);
	if (fstat(fd, &sb) < 0)
		err(1, "parsekeyfile: stat %s", filename);
	if ((sb.st_size > KEYSIZE_LIMIT) || (sb.st_size == 0))
		errx(1, "%s: key too %s", filename, sb.st_size ? "large" :
		    "small");
	if ((hex = calloc(sb.st_size, sizeof(unsigned char))) == NULL)
		err(1, "%s", __func__);
	if (read(fd, hex, sb.st_size) < sb.st_size)
		err(1, "parsekeyfile: read");
	close(fd);
	return (parsekey(hex, sb.st_size));
}

int
get_id_type(char *string)
{
	struct in6_addr ia;

	if (string == NULL)
		return (ID_UNKNOWN);

	if (inet_pton(AF_INET, string, &ia) == 1)
		return (ID_IPV4);
	else if (inet_pton(AF_INET6, string, &ia) == 1)
		return (ID_IPV6);
	else if (strchr(string, '@'))
		return (ID_UFQDN);
	else
		return (ID_FQDN);
}

struct ipsec_addr_wrap *
host(const char *s)
{
	struct ipsec_addr_wrap	*ipa = NULL;
	int			 mask, cont = 1;
	char			*p, *q, *ps;

	if ((p = strrchr(s, '/')) != NULL) {
		errno = 0;
		mask = strtol(p + 1, &q, 0);
		if (errno == ERANGE || !q || *q || mask > 128 || q == (p + 1))
			errx(1, "host: invalid netmask '%s'", p);
		if ((ps = malloc(strlen(s) - strlen(p) + 1)) == NULL)
			err(1, "%s", __func__);
		strlcpy(ps, s, strlen(s) - strlen(p) + 1);
	} else {
		if ((ps = strdup(s)) == NULL)
			err(1, "%s", __func__);
		mask = -1;
	}

	/* Does interface with this name exist? */
	if (cont && (ipa = host_if(ps, mask)) != NULL)
		cont = 0;

	/* IPv4 address? */
	if (cont && (ipa = host_v4(s, mask == -1 ? 32 : mask)) != NULL)
		cont = 0;

	/* IPv6 address? */
	if (cont && (ipa = host_v6(ps, mask == -1 ? 128 : mask)) != NULL)
		cont = 0;

	/* dns lookup */
	if (cont && mask == -1 && (ipa = host_dns(s, mask)) != NULL)
		cont = 0;
	free(ps);

	if (ipa == NULL || cont == 1) {
		fprintf(stderr, "no IP address found for %s\n", s);
		return (NULL);
	}
	return (ipa);
}

struct ipsec_addr_wrap *
host_v6(const char *s, int prefixlen)
{
	struct ipsec_addr_wrap	*ipa = NULL;
	struct addrinfo		 hints, *res;
	char			 hbuf[NI_MAXHOST];

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(s, NULL, &hints, &res))
		return (NULL);
	if (res->ai_next)
		err(1, "host_v6: numeric hostname expanded to multiple item");

	ipa = calloc(1, sizeof(struct ipsec_addr_wrap));
	if (ipa == NULL)
		err(1, "%s", __func__);
	ipa->af = res->ai_family;
	memcpy(&ipa->address.v6,
	    &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr,
	    sizeof(struct in6_addr));
	if (prefixlen > 128)
		prefixlen = 128;
	ipa->next = NULL;
	ipa->tail = ipa;

	set_ipmask(ipa, prefixlen);
	if (getnameinfo(res->ai_addr, res->ai_addrlen,
	    hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST)) {
		errx(1, "could not get a numeric hostname");
	}

	if (prefixlen != 128) {
		ipa->netaddress = 1;
		if (asprintf(&ipa->name, "%s/%d", hbuf, prefixlen) == -1)
			err(1, "%s", __func__);
	} else {
		if ((ipa->name = strdup(hbuf)) == NULL)
			err(1, "%s", __func__);
	}

	freeaddrinfo(res);

	return (ipa);
}

struct ipsec_addr_wrap *
host_v4(const char *s, int mask)
{
	struct ipsec_addr_wrap	*ipa = NULL;
	struct in_addr		 ina;
	int			 bits = 32;

	bzero(&ina, sizeof(struct in_addr));
	if (strrchr(s, '/') != NULL) {
		if ((bits = inet_net_pton(AF_INET, s, &ina, sizeof(ina))) == -1)
			return (NULL);
	} else {
		if (inet_pton(AF_INET, s, &ina) != 1)
			return (NULL);
	}

	ipa = calloc(1, sizeof(struct ipsec_addr_wrap));
	if (ipa == NULL)
		err(1, "%s", __func__);

	ipa->address.v4 = ina;
	ipa->name = strdup(s);
	if (ipa->name == NULL)
		err(1, "%s", __func__);
	ipa->af = AF_INET;
	ipa->next = NULL;
	ipa->tail = ipa;

	set_ipmask(ipa, bits);
	if (strrchr(s, '/') != NULL)
		ipa->netaddress = 1;

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
	error = getaddrinfo(s, NULL, &hints, &res0);
	if (error)
		return (NULL);

	for (res = res0; res; res = res->ai_next) {
		if (res->ai_family != AF_INET && res->ai_family != AF_INET6)
			continue;

		ipa = calloc(1, sizeof(struct ipsec_addr_wrap));
		if (ipa == NULL)
			err(1, "%s", __func__);
		switch (res->ai_family) {
		case AF_INET:
			memcpy(&ipa->address.v4,
			    &((struct sockaddr_in *)res->ai_addr)->sin_addr,
			    sizeof(struct in_addr));
			break;
		case AF_INET6:
			/* XXX we do not support scoped IPv6 address yet */
			if (((struct sockaddr_in6 *)res->ai_addr)->sin6_scope_id) {
				free(ipa);
				continue;
			}
			memcpy(&ipa->address.v6,
			    &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr,
			    sizeof(struct in6_addr));
			break;
		}
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
	return (ipa);
}

/* interface lookup routintes */

struct ipsec_addr_wrap	*iftab;

void
ifa_load(void)
{
	struct ifaddrs		*ifap, *ifa;
	struct ipsec_addr_wrap	*n = NULL, *h = NULL;

	if (getifaddrs(&ifap) < 0)
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
			n->af = AF_INET;
			memcpy(&n->address.v4, &((struct sockaddr_in *)
			    ifa->ifa_addr)->sin_addr,
			    sizeof(struct in_addr));
			memcpy(&n->mask.v4, &((struct sockaddr_in *)
			    ifa->ifa_netmask)->sin_addr,
			    sizeof(struct in_addr));
		} else if (n->af == AF_INET6) {
			n->af = AF_INET6;
			memcpy(&n->address.v6, &((struct sockaddr_in6 *)
			    ifa->ifa_addr)->sin6_addr,
			    sizeof(struct in6_addr));
			memcpy(&n->mask.v6, &((struct sockaddr_in6 *)
			    ifa->ifa_netmask)->sin6_addr,
			    sizeof(struct in6_addr));
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

	/* check whether this is a group */
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
			/* route/show.c and bgpd/util.c give KAME credit */
			if (IN6_IS_ADDR_LINKLOCAL(&n->address.v6)) {
				u_int16_t tmp16;
				/* for now we can not handle link local,
				 * therefore bail for now
				 */
				free(n);
				continue;

				memcpy(&tmp16, &n->address.v6.s6_addr[2],
				    sizeof(tmp16));
				/* use this when we support link-local
				 * n->??.scopeid = ntohs(tmp16);
				 */
				n->address.v6.s6_addr[2] = 0;
				n->address.v6.s6_addr[3] = 0;
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
set_ipmask(struct ipsec_addr_wrap *address, u_int8_t b)
{
	struct ipsec_addr	*ipa;
	int			 i, j = 0;

	ipa = &address->mask;
	bzero(ipa, sizeof(struct ipsec_addr));

	while (b >= 32) {
		ipa->addr32[j++] = 0xffffffff;
		b -= 32;
	}
	for (i = 31; i > 31 - b; --i)
		ipa->addr32[j] |= (1 << i);
	if (b)
		ipa->addr32[j] = htonl(ipa->addr32[j]);
}

const struct ipsec_xf *
parse_xf(const char *name, const struct ipsec_xf xfs[])
{
	int		i;

	for (i = 0; xfs[i].name != NULL; i++) {
		if (strncmp(name, xfs[i].name, strlen(name)))
			continue;
		return &xfs[i];
	}
	return (NULL);
}

struct ipsec_lifetime *
parse_life(const char *value)
{
	struct ipsec_lifetime	*life;
	int			ret;
	int			seconds = 0;
	char			unit = 0;

	ret = sscanf(value, "%d%c", &seconds, &unit);
	if (ret == 2) {
		switch (tolower((unsigned char)unit)) {
		case 'm':
			seconds *= 60;
			break;
		case 'h':
			seconds *= 60 * 60;
			break;
		default:
			err(1, "invalid time unit");
		}
	} else if (ret != 1)
		err(1, "invalid time specification: %s", value);

	life = calloc(1, sizeof(struct ipsec_lifetime));
	if (life == NULL)
		err(1, "%s", __func__);

	life->lt_seconds = seconds;
	life->lt_bytes = -1;

	return (life);
}

struct ipsec_transforms *
copytransforms(const struct ipsec_transforms *xfs)
{
	struct ipsec_transforms *newxfs;

	if (xfs == NULL)
		return (NULL);

	newxfs = calloc(1, sizeof(struct ipsec_transforms));
	if (newxfs == NULL)
		err(1, "%s", __func__);

	memcpy(newxfs, xfs, sizeof(struct ipsec_transforms));
	return (newxfs);
}

struct ipsec_lifetime *
copylife(const struct ipsec_lifetime *life)
{
	struct ipsec_lifetime *newlife;

	if (life == NULL)
		return (NULL);

	newlife = calloc(1, sizeof(struct ipsec_lifetime));
	if (newlife == NULL)
		err(1, "%s", __func__);

	memcpy(newlife, life, sizeof(struct ipsec_lifetime));
	return (newlife);
}

struct ipsec_auth *
copyipsecauth(const struct ipsec_auth *auth)
{
	struct ipsec_auth	*newauth;

	if (auth == NULL)
		return (NULL);

	if ((newauth = calloc(1, sizeof(struct ipsec_auth))) == NULL)
		err(1, "%s", __func__);
	if (auth->srcid &&
	    asprintf(&newauth->srcid, "%s", auth->srcid) == -1)
		err(1, "%s", __func__);
	if (auth->dstid &&
	    asprintf(&newauth->dstid, "%s", auth->dstid) == -1)
		err(1, "%s", __func__);

	newauth->srcid_type = auth->srcid_type;
	newauth->dstid_type = auth->dstid_type;
	newauth->type = auth->type;

	return (newauth);
}

struct ike_auth *
copyikeauth(const struct ike_auth *auth)
{
	struct ike_auth	*newauth;

	if (auth == NULL)
		return (NULL);

	if ((newauth = calloc(1, sizeof(struct ike_auth))) == NULL)
		err(1, "%s", __func__);
	if (auth->string &&
	    asprintf(&newauth->string, "%s", auth->string) == -1)
		err(1, "%s", __func__);

	newauth->type = auth->type;

	return (newauth);
}

struct ipsec_key *
copykey(struct ipsec_key *key)
{
	struct ipsec_key	*newkey;

	if (key == NULL)
		return (NULL);

	if ((newkey = calloc(1, sizeof(struct ipsec_key))) == NULL)
		err(1, "%s", __func__);
	if ((newkey->data = calloc(key->len, sizeof(u_int8_t))) == NULL)
		err(1, "%s", __func__);
	memcpy(newkey->data, key->data, key->len);
	newkey->len = key->len;

	return (newkey);
}

struct ipsec_addr_wrap *
copyhost(const struct ipsec_addr_wrap *src)
{
	struct ipsec_addr_wrap *dst;

	if (src == NULL)
		return (NULL);

	dst = calloc(1, sizeof(struct ipsec_addr_wrap));
	if (dst == NULL)
		err(1, "%s", __func__);

	memcpy(dst, src, sizeof(struct ipsec_addr_wrap));

	if (src->name != NULL && (dst->name = strdup(src->name)) == NULL)
		err(1, "%s", __func__);

	return dst;
}

char *
copytag(const char *src)
{
	char *tag;

	if (src == NULL)
		return (NULL);
	if ((tag = strdup(src)) == NULL)
		err(1, "%s", __func__);

	return (tag);
}

struct ipsec_rule *
copyrule(struct ipsec_rule *rule)
{
	struct ipsec_rule	*r;

	if ((r = calloc(1, sizeof(struct ipsec_rule))) == NULL)
		err(1, "%s", __func__);

	r->src = copyhost(rule->src);
	r->dst = copyhost(rule->dst);
	r->local = copyhost(rule->local);
	r->peer = copyhost(rule->peer);
	r->auth = copyipsecauth(rule->auth);
	r->ikeauth = copyikeauth(rule->ikeauth);
	r->xfs = copytransforms(rule->xfs);
	r->p1xfs = copytransforms(rule->p1xfs);
	r->p2xfs = copytransforms(rule->p2xfs);
	r->p1life = copylife(rule->p1life);
	r->p2life = copylife(rule->p2life);
	r->authkey = copykey(rule->authkey);
	r->enckey = copykey(rule->enckey);
	r->tag = copytag(rule->tag);

	r->flags = rule->flags;
	r->p1ie = rule->p1ie;
	r->p2ie = rule->p2ie;
	r->type = rule->type;
	r->satype = rule->satype;
	r->proto = rule->proto;
	r->tmode = rule->tmode;
	r->direction = rule->direction;
	r->flowtype = rule->flowtype;
	r->sport = rule->sport;
	r->dport = rule->dport;
	r->ikemode = rule->ikemode;
	r->spi = rule->spi;
	r->udpencap = rule->udpencap;
	r->udpdport = rule->udpdport;
	r->nr = rule->nr;
	r->iface = rule->iface;

	return (r);
}

int
validate_af(struct ipsec_addr_wrap *src, struct ipsec_addr_wrap *dst)
{
	struct ipsec_addr_wrap *ta;
	u_int8_t src_v4 = 0;
	u_int8_t dst_v4 = 0;
	u_int8_t src_v6 = 0;
	u_int8_t dst_v6 = 0;

	for (ta = src; ta; ta = ta->next) {
		if (ta->af == AF_INET)
			src_v4 = 1;
		if (ta->af == AF_INET6)
			src_v6 = 1;
		if (ta->af == AF_UNSPEC)
			return 0;
		if (src_v4 && src_v6)
			break;
	}
	for (ta = dst; ta; ta = ta->next) {
		if (ta->af == AF_INET)
			dst_v4 = 1;
		if (ta->af == AF_INET6)
			dst_v6 = 1;
		if (ta->af == AF_UNSPEC)
			return 0;
		if (dst_v4 && dst_v6)
			break;
	}
	if (src_v4 != dst_v4 && src_v6 != dst_v6)
		return (1);

	return (0);
}


int
validate_sa(u_int32_t spi, u_int8_t satype, struct ipsec_transforms *xfs,
    struct ipsec_key *authkey, struct ipsec_key *enckey, u_int8_t tmode)
{
	/* Sanity checks */
	if (spi == 0) {
		yyerror("no SPI specified");
		return (0);
	}
	if (satype == IPSEC_AH) {
		if (!xfs) {
			yyerror("no transforms specified");
			return (0);
		}
		if (!xfs->authxf)
			xfs->authxf = &authxfs[AUTHXF_HMAC_SHA2_256];
		if (xfs->encxf) {
			yyerror("ah does not provide encryption");
			return (0);
		}
		if (xfs->compxf) {
			yyerror("ah does not provide compression");
			return (0);
		}
	}
	if (satype == IPSEC_ESP) {
		if (!xfs) {
			yyerror("no transforms specified");
			return (0);
		}
		if (xfs->compxf) {
			yyerror("esp does not provide compression");
			return (0);
		}
		if (!xfs->encxf)
			xfs->encxf = &encxfs[ENCXF_AES];
		if (xfs->encxf->nostatic) {
			yyerror("%s is disallowed with static keys",
			    xfs->encxf->name);
			return 0;
		}
		if (xfs->encxf->noauth && xfs->authxf) {
			yyerror("authentication is implicit for %s",
			    xfs->encxf->name);
			return (0);
		} else if (!xfs->encxf->noauth && !xfs->authxf)
			xfs->authxf = &authxfs[AUTHXF_HMAC_SHA2_256];
	}
	if (satype == IPSEC_IPCOMP) {
		if (!xfs) {
			yyerror("no transform specified");
			return (0);
		}
		if (xfs->authxf || xfs->encxf) {
			yyerror("no encryption or authentication with ipcomp");
			return (0);
		}
		if (!xfs->compxf)
			xfs->compxf = &compxfs[COMPXF_DEFLATE];
	}
	if (satype == IPSEC_IPIP) {
		if (!xfs) {
			yyerror("no transform specified");
			return (0);
		}
		if (xfs->authxf || xfs->encxf || xfs->compxf) {
			yyerror("no encryption, authentication or compression"
			    " with ipip");
			return (0);
		}
	}
	if (satype == IPSEC_TCPMD5 && authkey == NULL && tmode !=
	    IPSEC_TRANSPORT) {
		yyerror("authentication key needed for tcpmd5");
		return (0);
	}
	if (xfs && xfs->authxf) {
		if (!authkey && xfs->authxf != &authxfs[AUTHXF_NONE]) {
			yyerror("no authentication key specified");
			return (0);
		}
		if (authkey && authkey->len != xfs->authxf->keymin) {
			yyerror("wrong authentication key length, needs to be "
			    "%zu bits", xfs->authxf->keymin * 8);
			return (0);
		}
	}
	if (xfs && xfs->encxf) {
		if (!enckey && xfs->encxf != &encxfs[ENCXF_NULL]) {
			yyerror("no encryption key specified");
			return (0);
		}
		if (enckey) {
			if (enckey->len < xfs->encxf->keymin) {
				yyerror("encryption key too short (%zu bits), "
				    "minimum %zu bits", enckey->len * 8,
				    xfs->encxf->keymin * 8);
				return (0);
			}
			if (xfs->encxf->keymax < enckey->len) {
				yyerror("encryption key too long (%zu bits), "
				    "maximum %zu bits", enckey->len * 8,
				    xfs->encxf->keymax * 8);
				return (0);
			}
		}
	}

	return 1;
}

int
add_sabundle(struct ipsec_rule *r, char *bundle)
{
	struct ipsec_rule	*rp, *last, *sabundle;
	int			 found = 0;

	TAILQ_FOREACH(rp, &ipsec->bundle_queue, bundle_entry) {
		if ((strcmp(rp->src->name, r->src->name) == 0) &&
		    (strcmp(rp->dst->name, r->dst->name) == 0) &&
		    (strcmp(rp->bundle, bundle) == 0)) {
			found = 1;
			break;
		}
	}
	if (found) {
		last = TAILQ_LAST(&rp->dst_bundle_queue, dst_bundle_queue);
		TAILQ_INSERT_TAIL(&rp->dst_bundle_queue, r, dst_bundle_entry);

		sabundle = create_sabundle(last->dst, last->satype, last->spi,
		    r->dst, r->satype, r->spi);
		if (sabundle == NULL)
			return (1);
		sabundle->nr = ipsec->rule_nr++;
		if (ipsecctl_add_rule(ipsec, sabundle))
			return (1);
	} else {
		TAILQ_INSERT_TAIL(&ipsec->bundle_queue, r, bundle_entry);
		TAILQ_INIT(&r->dst_bundle_queue);
		TAILQ_INSERT_TAIL(&r->dst_bundle_queue, r, dst_bundle_entry);
		r->bundle = bundle;
	}

	return (0);
}

struct ipsec_rule *
create_sa(u_int8_t satype, u_int8_t tmode, struct ipsec_hosts *hosts,
    u_int32_t spi, u_int8_t udpencap, u_int16_t udpdport,
    struct ipsec_transforms *xfs, struct ipsec_key *authkey, struct ipsec_key *enckey)
{
	struct ipsec_rule *r;

	if (validate_sa(spi, satype, xfs, authkey, enckey, tmode) == 0)
		return (NULL);

	r = calloc(1, sizeof(struct ipsec_rule));
	if (r == NULL)
		err(1, "%s", __func__);

	r->type |= RULE_SA;
	r->satype = satype;
	r->tmode = tmode;
	r->src = hosts->src;
	r->dst = hosts->dst;
	r->spi = spi;
	r->udpencap = udpencap;
	r->udpdport = udpdport;
	r->xfs = xfs;
	r->authkey = authkey;
	r->enckey = enckey;

	return r;
}

struct ipsec_rule *
reverse_sa(struct ipsec_rule *rule, u_int32_t spi, struct ipsec_key *authkey,
    struct ipsec_key *enckey)
{
	struct ipsec_rule *reverse;

	if (validate_sa(spi, rule->satype, rule->xfs, authkey, enckey,
	    rule->tmode) == 0)
		return (NULL);

	reverse = calloc(1, sizeof(struct ipsec_rule));
	if (reverse == NULL)
		err(1, "%s", __func__);

	reverse->type |= RULE_SA;
	reverse->satype = rule->satype;
	reverse->tmode = rule->tmode;
	reverse->src = copyhost(rule->dst);
	reverse->dst = copyhost(rule->src);
	reverse->spi = spi;
	reverse->udpencap = rule->udpencap;
	reverse->udpdport = rule->udpdport;
	reverse->xfs = copytransforms(rule->xfs);
	reverse->authkey = authkey;
	reverse->enckey = enckey;

	return (reverse);
}

struct ipsec_rule *
create_sabundle(struct ipsec_addr_wrap *dst, u_int8_t proto, u_int32_t spi,
    struct ipsec_addr_wrap *dst2, u_int8_t proto2, u_int32_t spi2)
{
	struct ipsec_rule *r;

	r = calloc(1, sizeof(struct ipsec_rule));
	if (r == NULL)
		err(1, "%s", __func__);

	r->type |= RULE_BUNDLE;

	r->dst = copyhost(dst);
	r->dst2 = copyhost(dst2);
	r->proto = proto;
	r->proto2 = proto2;
	r->spi = spi;
	r->spi2 = spi2;
	r->satype = proto;

	return (r);
}

struct ipsec_rule *
create_flow(u_int8_t dir, u_int8_t proto, struct ipsec_hosts *hosts,
    u_int8_t satype, char *srcid, char *dstid, u_int8_t type)
{
	struct ipsec_rule *r;

	r = calloc(1, sizeof(struct ipsec_rule));
	if (r == NULL)
		err(1, "%s", __func__);

	r->type |= RULE_FLOW;

	if (dir == IPSEC_INOUT)
		r->direction = IPSEC_OUT;
	else
		r->direction = dir;

	r->satype = satype;
	r->proto = proto;
	r->src = hosts->src;
	r->sport = hosts->sport;
	r->dst = hosts->dst;
	r->dport = hosts->dport;
	if ((hosts->sport != 0 || hosts->dport != 0) &&
	    (proto != IPPROTO_TCP && proto != IPPROTO_UDP)) {
		yyerror("no protocol supplied with source/destination ports");
		goto errout;
	}

	switch (satype) {
	case IPSEC_IPCOMP:
	case IPSEC_IPIP:
		if (type == TYPE_UNKNOWN)
			type = TYPE_USE;
		break;
	default:
		if (type == TYPE_UNKNOWN)
			type = TYPE_REQUIRE;
		break;
	}		

	r->flowtype = type;
	if (type == TYPE_DENY || type == TYPE_BYPASS)
		return (r);

	r->auth = calloc(1, sizeof(struct ipsec_auth));
	if (r->auth == NULL)
		err(1, "%s", __func__);
	r->auth->srcid = srcid;
	r->auth->dstid = dstid;
	r->auth->srcid_type = get_id_type(srcid);
	r->auth->dstid_type = get_id_type(dstid);
	return r;

errout:
	free(r);
	if (srcid)
		free(srcid);
	if (dstid)
		free(dstid);
	free(hosts->src);
	hosts->src = NULL;
	free(hosts->dst);
	hosts->dst = NULL;

	return NULL;
}

void
expand_any(struct ipsec_addr_wrap *ipa_in)
{
	struct ipsec_addr_wrap *oldnext, *ipa;

	for (ipa = ipa_in; ipa; ipa = ipa->next) {
		if (ipa->af != AF_UNSPEC)
			continue;
		oldnext = ipa->next;

		ipa->af = AF_INET;
		ipa->netaddress = 1;
		if ((ipa->name = strdup("0.0.0.0/0")) == NULL)
			err(1, "%s", __func__);

		ipa->next = calloc(1, sizeof(struct ipsec_addr_wrap));
		if (ipa->next == NULL)
			err(1, "%s", __func__);
		ipa->next->af = AF_INET6;
		ipa->next->netaddress = 1;
		if ((ipa->next->name = strdup("::/0")) == NULL)
			err(1, "%s", __func__);

		ipa->next->next = oldnext;
	}
}
	 
int
set_rule_peers(struct ipsec_rule *r, struct ipsec_hosts *peers)
{
	if (r->type == RULE_FLOW &&
	    (r->flowtype == TYPE_DENY || r->flowtype == TYPE_BYPASS))
		return (0);

	r->local = copyhost(peers->src);
	r->peer = copyhost(peers->dst);
	if (r->peer == NULL) {
		/* Set peer to remote host.  Must be a host address. */
		if (r->direction == IPSEC_IN) {
			if (!r->src->netaddress)
				r->peer = copyhost(r->src);
		} else {
			if (!r->dst->netaddress)
				r->peer = copyhost(r->dst);
		}
	}
	if (r->type == RULE_FLOW && r->peer == NULL) {
		yyerror("no peer specified for destination %s",
		    r->dst->name);
		return (1);
	}
	if (r->peer != NULL && r->peer->af == AF_UNSPEC) {
		/* If peer has been specified as any, use the default peer. */
		free(r->peer);
		r->peer = NULL;
	}
	if (r->type == RULE_IKE && r->peer == NULL) {
		/*
                 * Check if the default peer is consistent for all
                 * rules.  Only warn to avoid breaking existing configs.
		 */
		static struct ipsec_rule *pdr = NULL;

		if (pdr == NULL) {
			/* Remember first default peer rule for comparison. */
			pdr = r;
		} else {
			/* The new default peer must create the same config. */
			if ((pdr->local == NULL && r->local != NULL) ||
			    (pdr->local != NULL && r->local == NULL) ||
			    (pdr->local != NULL && r->local != NULL &&
			    strcmp(pdr->local->name, r->local->name)))
				yywarn("default peer local mismatch");
			if (pdr->ikeauth->type != r->ikeauth->type)
				yywarn("default peer phase 1 auth mismatch");
			if (pdr->ikeauth->type == IKE_AUTH_PSK &&
			    r->ikeauth->type == IKE_AUTH_PSK &&
			    strcmp(pdr->ikeauth->string, r->ikeauth->string))
				yywarn("default peer psk mismatch");
			if (pdr->p1ie != r->p1ie)
				yywarn("default peer phase 1 mode mismatch");
			/*
			 * Transforms have ADD insted of SET so they may be
			 * different and are not checked here.
			 */
			if ((pdr->auth->srcid == NULL &&
			    r->auth->srcid != NULL) ||
			    (pdr->auth->srcid != NULL &&
			    r->auth->srcid == NULL) ||
			    (pdr->auth->srcid != NULL &&
			    r->auth->srcid != NULL &&
			    strcmp(pdr->auth->srcid, r->auth->srcid)))
				yywarn("default peer srcid mismatch");
			if ((pdr->auth->dstid == NULL &&
			    r->auth->dstid != NULL) ||
			    (pdr->auth->dstid != NULL &&
			    r->auth->dstid == NULL) ||
			    (pdr->auth->dstid != NULL &&
			    r->auth->dstid != NULL &&
			    strcmp(pdr->auth->dstid, r->auth->dstid)))
				yywarn("default peer dstid mismatch");
		}
	}
	return (0);
}

int
expand_rule(struct ipsec_rule *rule, struct ipsec_hosts *peers,
    u_int8_t direction, u_int32_t spi, struct ipsec_key *authkey,
    struct ipsec_key *enckey, char *bundle)
{
	struct ipsec_rule	*r, *revr;
	struct ipsec_addr_wrap	*src, *dst;
	int added = 0, ret = 1;

	if (validate_af(rule->src, rule->dst)) {
		yyerror("source/destination address families do not match");
		goto errout;
	}
	expand_any(rule->src);
	expand_any(rule->dst);
	for (src = rule->src; src; src = src->next) {
		for (dst = rule->dst; dst; dst = dst->next) {
			if (src->af != dst->af)
				continue;
			r = copyrule(rule);

			r->src = copyhost(src);
			r->dst = copyhost(dst);

			if (peers && set_rule_peers(r, peers)) {
				ipsecctl_free_rule(r);
				goto errout;
			}

			r->nr = ipsec->rule_nr++;
			if (ipsecctl_add_rule(ipsec, r))
				goto out;
			if (bundle && add_sabundle(r, bundle))
				goto out;

			if (direction == IPSEC_INOUT) {
				/* Create and add reverse flow rule. */
				revr = reverse_rule(r);
				if (revr == NULL)
					goto out;

				revr->nr = ipsec->rule_nr++;
				if (ipsecctl_add_rule(ipsec, revr))
					goto out;
				if (bundle && add_sabundle(revr, bundle))
					goto out;
			} else if (spi != 0 || authkey || enckey) {
				/* Create and add reverse sa rule. */
				revr = reverse_sa(r, spi, authkey, enckey);
				if (revr == NULL)
					goto out;

				revr->nr = ipsec->rule_nr++;
				if (ipsecctl_add_rule(ipsec, revr))
					goto out;
				if (bundle && add_sabundle(revr, bundle))
					goto out;
			}
			added++;
		}
	}
	if (!added)
		yyerror("rule expands to no valid combination");
 errout:
	ret = 0;
	ipsecctl_free_rule(rule);
 out:
	if (peers) {
		if (peers->src)
			free(peers->src);
		if (peers->dst)
			free(peers->dst);
	}
	return (ret);
}

struct ipsec_rule *
reverse_rule(struct ipsec_rule *rule)
{
	struct ipsec_rule *reverse;

	reverse = calloc(1, sizeof(struct ipsec_rule));
	if (reverse == NULL)
		err(1, "%s", __func__);

	reverse->type |= RULE_FLOW;

	/* Reverse direction */
	if (rule->direction == (u_int8_t)IPSEC_OUT)
		reverse->direction = (u_int8_t)IPSEC_IN;
	else
		reverse->direction = (u_int8_t)IPSEC_OUT;

	reverse->flowtype = rule->flowtype;
	reverse->src = copyhost(rule->dst);
	reverse->dst = copyhost(rule->src);
	reverse->sport = rule->dport;
	reverse->dport = rule->sport;
	if (rule->local)
		reverse->local = copyhost(rule->local);
	if (rule->peer)
		reverse->peer = copyhost(rule->peer);
	reverse->satype = rule->satype;
	reverse->proto = rule->proto;

	if (rule->auth) {
		reverse->auth = calloc(1, sizeof(struct ipsec_auth));
		if (reverse->auth == NULL)
			err(1, "%s", __func__);
		if (rule->auth->dstid && (reverse->auth->dstid =
		    strdup(rule->auth->dstid)) == NULL)
			err(1, "%s", __func__);
		if (rule->auth->srcid && (reverse->auth->srcid =
		    strdup(rule->auth->srcid)) == NULL)
			err(1, "%s", __func__);
		reverse->auth->srcid_type = rule->auth->srcid_type;
		reverse->auth->dstid_type = rule->auth->dstid_type;
		reverse->auth->type = rule->auth->type;
	}

	return reverse;
}

struct ipsec_rule *
create_ike(u_int8_t proto, struct ipsec_hosts *hosts,
    struct ike_mode *phase1mode, struct ike_mode *phase2mode, u_int8_t satype,
    u_int8_t tmode, u_int8_t mode, char *srcid, char *dstid,
    struct ike_auth *authtype, char *tag)
{
	struct ipsec_rule *r;

	r = calloc(1, sizeof(struct ipsec_rule));
	if (r == NULL)
		err(1, "%s", __func__);

	r->type = RULE_IKE;

	r->proto = proto;
	r->src = hosts->src;
	r->sport = hosts->sport;
	r->dst = hosts->dst;
	r->dport = hosts->dport;
	if ((hosts->sport != 0 || hosts->dport != 0) &&
	    (proto != IPPROTO_TCP && proto != IPPROTO_UDP)) {
		yyerror("no protocol supplied with source/destination ports");
		goto errout;
	}

	r->satype = satype;
	r->tmode = tmode;
	r->ikemode = mode;
	if (phase1mode) {
		r->p1xfs = phase1mode->xfs;
		r->p1life = phase1mode->life;
		r->p1ie = phase1mode->ike_exch;
	} else {
		r->p1ie = IKE_MM;
	}
	if (phase2mode) {
		if (phase2mode->xfs && phase2mode->xfs->encxf &&
		    phase2mode->xfs->encxf->noauth &&
		    phase2mode->xfs->authxf) {
			yyerror("authentication is implicit for %s",
			    phase2mode->xfs->encxf->name);
			goto errout;
		}
		r->p2xfs = phase2mode->xfs;
		r->p2life = phase2mode->life;
		r->p2ie = phase2mode->ike_exch;
	} else {
		r->p2ie = IKE_QM;
	}

	r->auth = calloc(1, sizeof(struct ipsec_auth));
	if (r->auth == NULL)
		err(1, "%s", __func__);
	r->auth->srcid = srcid;
	r->auth->dstid = dstid;
	r->auth->srcid_type = get_id_type(srcid);
	r->auth->dstid_type = get_id_type(dstid);
	r->ikeauth = calloc(1, sizeof(struct ike_auth));
	if (r->ikeauth == NULL)
		err(1, "%s", __func__);
	r->ikeauth->type = authtype->type;
	r->ikeauth->string = authtype->string;
	r->tag = tag;

	return (r);

errout:
	free(r);
	free(hosts->src);
	hosts->src = NULL;
	free(hosts->dst);
	hosts->dst = NULL;
	if (phase1mode) {
		free(phase1mode->xfs);
		phase1mode->xfs = NULL;
		free(phase1mode->life);
		phase1mode->life = NULL;
	}
	if (phase2mode) {
		free(phase2mode->xfs);
		phase2mode->xfs = NULL;
		free(phase2mode->life);
		phase2mode->life = NULL;
	}
	if (srcid)
		free(srcid);
	if (dstid)
		free(dstid);
	return NULL;
}
