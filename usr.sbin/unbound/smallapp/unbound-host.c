/*
 * smallapp/unbound-host.c - replacement for host that supports validation.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file performs functionality like 'host', and also supports validation.
 * It uses the libunbound library.
 */

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
/* remove alloc checks, not in this part of the code */
#ifdef UNBOUND_ALLOC_STATS
#undef malloc
#undef calloc
#undef free
#undef realloc
#undef reallocarray
#undef strdup
#endif
#ifdef UNBOUND_ALLOC_LITE
#undef malloc
#undef calloc
#undef free
#undef realloc
#undef strdup
#define unbound_lite_wrapstr(s) s
#endif
#include "libunbound/unbound.h"
#include "sldns/rrdef.h"
#include "sldns/wire2str.h"
#ifdef HAVE_NSS
/* nss3 */
#include "nss.h"
#endif
#ifdef HAVE_SSL
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#endif /* HAVE_SSL */

/** verbosity for unbound-host app */
static int verb = 0;

/** Give unbound-host usage, and exit (1). */
static void
usage(void)
{
	printf("Usage:	unbound-host [-C configfile] [-vdhr46] [-c class] [-t type]\n");
	printf("                     [-y key] [-f keyfile] [-F namedkeyfile] hostname\n");
	printf("  Queries the DNS for information.\n");
	printf("  The hostname is looked up for IP4, IP6 and mail.\n");
	printf("  If an ip-address is given a reverse lookup is done.\n");
	printf("  Use the -v option to see DNSSEC security information.\n");
	printf("    -t type		what type to look for.\n");
	printf("    -c class		what class to look for, if not class IN.\n");
	printf("    -y 'keystring'	specify trust anchor, DS or DNSKEY, like\n");
	printf("			-y 'example.com DS 31560 5 1 1CFED8478...'\n");
	printf("    -D			DNSSEC enable with default root anchor\n");
	printf("    			from %s\n", ROOT_ANCHOR_FILE);
	printf("    -f keyfile		read trust anchors from file, with lines as -y.\n");
	printf("    -F keyfile		read named.conf-style trust anchors.\n");
	printf("    -C config		use the specified unbound.conf (none read by default)\n");
	printf("			pass as first argument if you want to override some\n");
	printf("			options with further arguments\n");
	printf("    -r			read forwarder information from /etc/resolv.conf\n");
	printf("      			breaks validation if the forwarder does not do DNSSEC.\n");
	printf("    -v			be more verbose, shows nodata and security.\n");
	printf("    -d			debug, traces the action, -d -d shows more.\n");
	printf("    -4			use ipv4 network, avoid ipv6.\n");
	printf("    -6			use ipv6 network, avoid ipv4.\n");
	printf("    -h			show this usage help.\n");
	printf("Version %s\n", PACKAGE_VERSION);
	printf("BSD licensed, see LICENSE in source package for details.\n");
	printf("Report bugs to %s\n", PACKAGE_BUGREPORT);
	exit(1);
}

/** determine if str is ip4 and put into reverse lookup format */
static int
isip4(const char* nm, char** res)
{
	struct in_addr addr;
	/* ddd.ddd.ddd.ddd.in-addr.arpa. is less than 32 */
	char buf[32];
	if(inet_pton(AF_INET, nm, &addr) <= 0) {
		return 0;
	}
	snprintf(buf, sizeof(buf), "%u.%u.%u.%u.in-addr.arpa",
		(unsigned)((uint8_t*)&addr)[3], (unsigned)((uint8_t*)&addr)[2],
		(unsigned)((uint8_t*)&addr)[1], (unsigned)((uint8_t*)&addr)[0]);
	*res = strdup(buf);
	return 1;
}

/** determine if str is ip6 and put into reverse lookup format */
static int
isip6(const char* nm, char** res)
{
	struct in6_addr addr;
	/* [nibble.]{32}.ip6.arpa. is less than 128 */
	const char* hex = "0123456789abcdef";
	char buf[128];
	char *p;
	int i;
	if(inet_pton(AF_INET6, nm, &addr) <= 0) {
		return 0;
	}
	p = buf;
	for(i=15; i>=0; i--) {
		uint8_t b = ((uint8_t*)&addr)[i];
		*p++ = hex[ (b&0x0f) ];
		*p++ = '.';
		*p++ = hex[ (b&0xf0) >> 4 ];
		*p++ = '.';
	}
	snprintf(buf+16*4, sizeof(buf)-16*4, "ip6.arpa");
	*res = strdup(buf);
	if(!*res) {
		fprintf(stderr, "error: out of memory\n");
		exit(1);
	}
	return 1;
}

/** massage input name */
static char*
massage_qname(const char* nm, int* reverse)
{
	/* recognise IP4 and IP6, create reverse addresses if needed */
	char* res;
	if(isip4(nm, &res)) {
		*reverse = 1;
	} else if(isip6(nm, &res)) {
		*reverse = 1;
	} else {
		res = strdup(nm);
	}
	if(!res) {
		fprintf(stderr, "error: out of memory\n");
		exit(1);
	}
	return res;
}

/** massage input type */
static int
massage_type(const char* t, int reverse, int* multi)
{
	if(t) {
		int r = sldns_get_rr_type_by_name(t);
		if(r == 0 && strcasecmp(t, "TYPE0") != 0 && 
			strcmp(t, "") != 0) {
			fprintf(stderr, "error unknown type %s\n", t);
			exit(1);
		}
		return r;
	}
	if(!t && reverse)
		return LDNS_RR_TYPE_PTR;
	*multi = 1;
	return LDNS_RR_TYPE_A;
}

/** massage input class */
static int
massage_class(const char* c)
{
	if(c) {
		int r = sldns_get_rr_class_by_name(c);
		if(r == 0 && strcasecmp(c, "CLASS0") != 0 && 
			strcmp(c, "") != 0) {
			fprintf(stderr, "error unknown class %s\n", c);
			exit(1);
		}
		return r;
	}
	return LDNS_RR_CLASS_IN;
}

/** nice security status string */
static const char* 
secure_str(struct ub_result* result)
{
	if(result->rcode != 0 && result->rcode != 3) return "(error)";
	if(result->secure) return "(secure)";
	if(result->bogus) return "(BOGUS (security failure))";
	return "(insecure)";
}

/** nice string for type */
static void
pretty_type(char* s, size_t len, int t)
{
	char d[16];
	sldns_wire2str_type_buf((uint16_t)t, d, sizeof(d));
	snprintf(s, len, "%s", d);
}

/** nice string for class */
static void
pretty_class(char* s, size_t len, int c)
{
	char d[16];
	sldns_wire2str_class_buf((uint16_t)c, d, sizeof(d));
	snprintf(s, len, "%s", d);
}

/** nice string for rcode */
static void
pretty_rcode(char* s, size_t len, int r)
{
	char d[16];
	sldns_wire2str_rcode_buf(r, d, sizeof(d));
	snprintf(s, len, "%s", d);
}

/** convert and print rdata */
static void
print_rd(int t, char* data, size_t len)
{
	char s[65535];
	sldns_wire2str_rdata_buf((uint8_t*)data, len, s, sizeof(s), (uint16_t)t);
	printf(" %s", s);
}

/** pretty line of RR data for results */
static void
pretty_rdata(char* q, char* cstr, char* tstr, int t, const char* sec, 
	char* data, size_t len)
{
	printf("%s", q);
	if(strcmp(cstr, "IN") != 0)
		printf(" in class %s", cstr);
	if(t == LDNS_RR_TYPE_A)
		printf(" has address");
	else if(t == LDNS_RR_TYPE_AAAA)
		printf(" has IPv6 address");
	else if(t == LDNS_RR_TYPE_MX)
		printf(" mail is handled by");
	else if(t == LDNS_RR_TYPE_PTR)
		printf(" domain name pointer");
	else	printf(" has %s record", tstr);
	print_rd(t, data, len);
	if(verb > 0)
		printf(" %s", sec);
	printf("\n");
}

/** pretty line of output for results */
static void
pretty_output(char* q, int t, int c, struct ub_result* result, int docname)
{
	int i;
	const char *secstatus = secure_str(result);
	char tstr[16];
	char cstr[16];
	char rcodestr[16];
	pretty_type(tstr, 16, t);
	pretty_class(cstr, 16, c);
	pretty_rcode(rcodestr, 16, result->rcode);

	if(!result->havedata && result->rcode) {
		printf("Host %s not found: %d(%s).",
			q, result->rcode, rcodestr);
		if(verb > 0)
			printf(" %s", secstatus);
		printf("\n");
		if(result->bogus && result->why_bogus)
			printf("%s\n", result->why_bogus);
		return;
	}
	if(docname && result->canonname &&
		result->canonname != result->qname) {
		printf("%s is an alias for %s", result->qname, 
			result->canonname);
		if(verb > 0)
			printf(" %s", secstatus);
		printf("\n");
	}
	/* remove trailing . from long canonnames for nicer output */
	if(result->canonname && strlen(result->canonname) > 1 &&
		result->canonname[strlen(result->canonname)-1] == '.')
		result->canonname[strlen(result->canonname)-1] = 0;
	if(!result->havedata) {
		if(verb > 0) {
			printf("%s", result->canonname?result->canonname:q);
			if(strcmp(cstr, "IN") != 0)
				printf(" in class %s", cstr);
			if(t == LDNS_RR_TYPE_A)
				printf(" has no address");
			else if(t == LDNS_RR_TYPE_AAAA)
				printf(" has no IPv6 address");
			else if(t == LDNS_RR_TYPE_PTR)
				printf(" has no domain name ptr");
			else if(t == LDNS_RR_TYPE_MX)
				printf(" has no mail handler record");
			else if(t == LDNS_RR_TYPE_ANY) {
				char* s = sldns_wire2str_pkt(
					result->answer_packet,
					(size_t)result->answer_len);
				if(!s) {
					fprintf(stderr, "alloc failure\n");
					exit(1);
				}
				printf("%s\n", s);
				free(s);
			} else	printf(" has no %s record", tstr);
			printf(" %s\n", secstatus);
		}
		/* else: emptiness to indicate no data */
		if(result->bogus && result->why_bogus)
			printf("%s\n", result->why_bogus);
		return;
	}
	i=0;
	while(result->data[i])
	{
		pretty_rdata(
			result->canonname?result->canonname:q,
			cstr, tstr, t, secstatus, result->data[i],
			(size_t)result->len[i]);
		i++;
	}
	if(result->bogus && result->why_bogus)
		printf("%s\n", result->why_bogus);
}

/** perform a lookup and printout return if domain existed */
static int
dnslook(struct ub_ctx* ctx, char* q, int t, int c, int docname)
{
	int ret;
	struct ub_result* result;

	ret = ub_resolve(ctx, q, t, c, &result);
	if(ret != 0) {
		fprintf(stderr, "resolve error: %s\n", ub_strerror(ret));
		exit(1);
	}
	pretty_output(q, t, c, result, docname);
	ret = result->nxdomain;
	ub_resolve_free(result);
	return ret;
}

/** perform host lookup */
static void
lookup(struct ub_ctx* ctx, const char* nm, const char* qt, const char* qc)
{
	/* massage input into a query name, type and class */
	int multi = 0;	 /* no type, so do A, AAAA, MX */
	int reverse = 0; /* we are doing a reverse lookup */
	char* realq = massage_qname(nm, &reverse);
	int t = massage_type(qt, reverse, &multi);
	int c = massage_class(qc);

	/* perform the query */
	if(multi) {
		if(!dnslook(ctx, realq, LDNS_RR_TYPE_A, c, 1)) {
			/* domain exists, lookup more */
			(void)dnslook(ctx, realq, LDNS_RR_TYPE_AAAA, c, 0);
			(void)dnslook(ctx, realq, LDNS_RR_TYPE_MX, c, 0);
		}
	} else {
		(void)dnslook(ctx, realq, t, c, 1);
	}
	ub_ctx_delete(ctx);
	free(realq);
}

/** print error if any */
static void
check_ub_res(int r)
{
	if(r != 0) {
		fprintf(stderr, "error: %s\n", ub_strerror(r));
		exit(1);
	}
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** Main routine for unbound-host */
int main(int argc, char* argv[])
{
	int c;
	char* qclass = NULL;
	char* qtype = NULL;
	char* use_syslog = NULL;
	struct ub_ctx* ctx = NULL;
	int debuglevel = 0;
	
	ctx = ub_ctx_create();
	if(!ctx) {
		fprintf(stderr, "error: out of memory\n");
		exit(1);
	}
	/* no need to fetch additional targets, we only do few lookups */
	check_ub_res(ub_ctx_set_option(ctx, "target-fetch-policy:", "0 0 0 0 0"));

	/* parse the options */
	while( (c=getopt(argc, argv, "46DF:c:df:hrt:vy:C:")) != -1) {
		switch(c) {
		case '4':
			check_ub_res(ub_ctx_set_option(ctx, "do-ip6:", "no"));
			break;
		case '6':
			check_ub_res(ub_ctx_set_option(ctx, "do-ip4:", "no"));
			break;
		case 'c':
			qclass = optarg;
			break;
		case 'C':
			check_ub_res(ub_ctx_config(ctx, optarg));
			break;
		case 'D':
			check_ub_res(ub_ctx_add_ta_file(ctx, ROOT_ANCHOR_FILE));
			break;
		case 'd':
			debuglevel++;
			if(debuglevel < 2) 
				debuglevel = 2; /* at least VERB_DETAIL */
			break;
		case 'r':
			check_ub_res(ub_ctx_resolvconf(ctx, "/etc/resolv.conf"));
			break;
		case 't':
			qtype = optarg;
			break;
		case 'v':
			verb++;
			break;
		case 'y':
			check_ub_res(ub_ctx_add_ta(ctx, optarg));
			break;
		case 'f':
			check_ub_res(ub_ctx_add_ta_file(ctx, optarg));
			break;
		case 'F':
			check_ub_res(ub_ctx_trustedkeys(ctx, optarg));
			break;
		case '?':
		case 'h':
		default:
			ub_ctx_delete(ctx);
			usage();
		}
	}
	if(debuglevel != 0) /* set after possible -C options */
		check_ub_res(ub_ctx_debuglevel(ctx, debuglevel));
	if(ub_ctx_get_option(ctx, "use-syslog", &use_syslog) == 0) {
		if(strcmp(use_syslog, "yes") == 0) /* disable use-syslog */
			check_ub_res(ub_ctx_set_option(ctx, 
				"use-syslog:", "no"));
#ifdef UNBOUND_ALLOC_STATS
		unbound_stat_free_log(use_syslog, __FILE__, __LINE__, __func__);
#else
		free(use_syslog);
#endif
	}
	argc -= optind;
	argv += optind;
	if(argc != 1) {
		ub_ctx_delete(ctx);
		usage();
	}

#ifdef HAVE_SSL
#ifdef HAVE_ERR_LOAD_CRYPTO_STRINGS
	ERR_load_crypto_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	ERR_load_SSL_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_CRYPTO)
#  ifndef S_SPLINT_S
	OpenSSL_add_all_algorithms();
#  endif
#else
	OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS
		| OPENSSL_INIT_ADD_ALL_DIGESTS
		| OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	(void)SSL_library_init();
#else
	(void)OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
#endif
#endif /* HAVE_SSL */
#ifdef HAVE_NSS
        if(NSS_NoDB_Init(".") != SECSuccess) {
		fprintf(stderr, "could not init NSS\n");
		return 1;
	}
#endif
	lookup(ctx, argv[0], qtype, qclass);
	return 0;
}
