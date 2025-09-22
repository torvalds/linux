/*
 * util/net_help.c - implementation of the network helper code
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
 * Implementation of net_help.h.
 */

#include "config.h"
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif
#ifdef HAVE_NETIOAPI_H
#include <netioapi.h>
#endif
#include <ctype.h>
#include "util/net_help.h"
#include "util/log.h"
#include "util/data/dname.h"
#include "util/module.h"
#include "util/regional.h"
#include "util/config_file.h"
#include "sldns/parseutil.h"
#include "sldns/wire2str.h"
#include "sldns/str2wire.h"
#include <fcntl.h>
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#ifdef HAVE_OPENSSL_CORE_NAMES_H
#include <openssl/core_names.h>
#endif
#ifdef USE_WINSOCK
#include <wincrypt.h>
#endif
#ifdef HAVE_NGHTTP2_NGHTTP2_H
#include <nghttp2/nghttp2.h>
#endif

/** max length of an IP address (the address portion) that we allow */
#define MAX_ADDR_STRLEN 128 /* characters */
/** max length of a hostname (with port and tls name) that we allow */
#define MAX_HOST_STRLEN (LDNS_MAX_DOMAINLEN * 3) /* characters */
/** default value for EDNS ADVERTISED size */
uint16_t EDNS_ADVERTISED_SIZE = 4096;

/** minimal responses when positive answer: default is no */
int MINIMAL_RESPONSES = 0;

/** rrset order roundrobin: default is yes */
int RRSET_ROUNDROBIN = 1;

/** log tag queries with name instead of 'info' for filtering */
int LOG_TAG_QUERYREPLY = 0;

#ifdef HAVE_SSL
static struct tls_session_ticket_key {
	unsigned char *key_name;
	unsigned char *aes_key;
	unsigned char *hmac_key;
} *ticket_keys;
#endif /* HAVE_SSL */

#ifdef HAVE_SSL
/**
 * callback TLS session ticket encrypt and decrypt
 * For use with SSL_CTX_set_tlsext_ticket_key_cb or
 * SSL_CTX_set_tlsext_ticket_key_evp_cb
 * @param s: the SSL_CTX to use (from connect_sslctx_create())
 * @param key_name: secret name, 16 bytes
 * @param iv: up to EVP_MAX_IV_LENGTH.
 * @param evp_ctx: the evp cipher context, function sets this.
 * @param hmac_ctx: the hmac context, function sets this.
 * 	with ..key_cb it is of type HMAC_CTX*
 * 	with ..key_evp_cb it is of type EVP_MAC_CTX*
 * @param enc: 1 is encrypt, 0 is decrypt
 * @return 0 on no ticket, 1 for okay, and 2 for okay but renew the ticket
 * 	(the ticket is decrypt only). and <0 for failures.
 */
int tls_session_ticket_key_cb(SSL *s, unsigned char* key_name,
	unsigned char* iv, EVP_CIPHER_CTX *evp_ctx,
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
	EVP_MAC_CTX *hmac_ctx,
#else
	HMAC_CTX* hmac_ctx,
#endif
	int enc);
#endif /* HAVE_SSL */

/* returns true is string addr is an ip6 specced address */
int
str_is_ip6(const char* str)
{
	if(strchr(str, ':'))
		return 1;
	else    return 0;
}

int 
fd_set_nonblock(int s) 
{
#ifdef HAVE_FCNTL
	int flag;
	if((flag = fcntl(s, F_GETFL)) == -1) {
		log_err("can't fcntl F_GETFL: %s", strerror(errno));
		flag = 0;
	}
	flag |= O_NONBLOCK;
	if(fcntl(s, F_SETFL, flag) == -1) {
		log_err("can't fcntl F_SETFL: %s", strerror(errno));
		return 0;
	}
#elif defined(HAVE_IOCTLSOCKET)
	unsigned long on = 1;
	if(ioctlsocket(s, FIONBIO, &on) != 0) {
		log_err("can't ioctlsocket FIONBIO on: %s", 
			wsa_strerror(WSAGetLastError()));
	}
#endif
	return 1;
}

int 
fd_set_block(int s) 
{
#ifdef HAVE_FCNTL
	int flag;
	if((flag = fcntl(s, F_GETFL)) == -1) {
		log_err("cannot fcntl F_GETFL: %s", strerror(errno));
		flag = 0;
	}
	flag &= ~O_NONBLOCK;
	if(fcntl(s, F_SETFL, flag) == -1) {
		log_err("cannot fcntl F_SETFL: %s", strerror(errno));
		return 0;
	}
#elif defined(HAVE_IOCTLSOCKET)
	unsigned long off = 0;
	if(ioctlsocket(s, FIONBIO, &off) != 0) {
		if(WSAGetLastError() != WSAEINVAL || verbosity >= 4)
			log_err("can't ioctlsocket FIONBIO off: %s", 
				wsa_strerror(WSAGetLastError()));
	}
#endif	
	return 1;
}

int 
is_pow2(size_t num)
{
	if(num == 0) return 1;
	return (num & (num-1)) == 0;
}

void* 
memdup(void* data, size_t len)
{
	void* d;
	if(!data) return NULL;
	if(len == 0) return NULL;
	d = malloc(len);
	if(!d) return NULL;
	memcpy(d, data, len);
	return d;
}

void
log_addr(enum verbosity_value v, const char* str, 
	struct sockaddr_storage* addr, socklen_t addrlen)
{
	uint16_t port;
	const char* family = "unknown";
	char dest[100];
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	if(verbosity < v)
		return;
	switch(af) {
		case AF_INET: family="ip4"; break;
		case AF_INET6: family="ip6";
			sinaddr = &((struct sockaddr_in6*)addr)->sin6_addr;
			break;
		case AF_LOCAL:
			dest[0]=0;
			(void)inet_ntop(af, sinaddr, dest,
				(socklen_t)sizeof(dest));
			verbose(v, "%s local %s", str, dest);
			return; /* do not continue and try to get port */
		default: break;
	}
	if(inet_ntop(af, sinaddr, dest, (socklen_t)sizeof(dest)) == 0) {
		(void)strlcpy(dest, "(inet_ntop error)", sizeof(dest));
	}
	dest[sizeof(dest)-1] = 0;
	port = ntohs(((struct sockaddr_in*)addr)->sin_port);
	if(verbosity >= 4)
		verbose(v, "%s %s %s port %d (len %d)", str, family, dest, 
			(int)port, (int)addrlen);
	else	verbose(v, "%s %s port %d", str, dest, (int)port);
}

int
extstrtoaddr(const char* str, struct sockaddr_storage* addr,
	socklen_t* addrlen, int port)
{
	char* s;
	if((s=strchr(str, '@'))) {
		char buf[MAX_ADDR_STRLEN];
		if(s-str >= MAX_ADDR_STRLEN) {
			return 0;
		}
		(void)strlcpy(buf, str, sizeof(buf));
		buf[s-str] = 0;
		port = atoi(s+1);
		if(port == 0 && strcmp(s+1,"0")!=0) {
			return 0;
		}
		return ipstrtoaddr(buf, port, addr, addrlen);
	}
	return ipstrtoaddr(str, port, addr, addrlen);
}

int 
ipstrtoaddr(const char* ip, int port, struct sockaddr_storage* addr,
	socklen_t* addrlen)
{
	uint16_t p;
	if(!ip) return 0;
	p = (uint16_t) port;
	if(str_is_ip6(ip)) {
		char buf[MAX_ADDR_STRLEN];
		char* s;
		struct sockaddr_in6* sa = (struct sockaddr_in6*)addr;
		*addrlen = (socklen_t)sizeof(struct sockaddr_in6);
		memset(sa, 0, *addrlen);
		sa->sin6_family = AF_INET6;
		sa->sin6_port = (in_port_t)htons(p);
		if((s=strchr(ip, '%'))) { /* ip6%interface, rfc 4007 */
			if(s-ip >= MAX_ADDR_STRLEN)
				return 0;
			(void)strlcpy(buf, ip, sizeof(buf));
			buf[s-ip]=0;
#ifdef HAVE_IF_NAMETOINDEX
			if (!(sa->sin6_scope_id = if_nametoindex(s+1)))
#endif /* HAVE_IF_NAMETOINDEX */
				sa->sin6_scope_id = (uint32_t)atoi(s+1);
			ip = buf;
		}
		if(inet_pton((int)sa->sin6_family, ip, &sa->sin6_addr) <= 0) {
			return 0;
		}
	} else { /* ip4 */
		struct sockaddr_in* sa = (struct sockaddr_in*)addr;
		*addrlen = (socklen_t)sizeof(struct sockaddr_in);
		memset(sa, 0, *addrlen);
		sa->sin_family = AF_INET;
		sa->sin_port = (in_port_t)htons(p);
		if(inet_pton((int)sa->sin_family, ip, &sa->sin_addr) <= 0) {
			return 0;
		}
	}
	return 1;
}

int netblockstrtoaddr(const char* str, int port, struct sockaddr_storage* addr,
        socklen_t* addrlen, int* net)
{
	char buf[64];
	char* s;
	*net = (str_is_ip6(str)?128:32);
	if((s=strchr(str, '/'))) {
		if(atoi(s+1) > *net) {
			log_err("netblock too large: %s", str);
			return 0;
		}
		*net = atoi(s+1);
		if(*net == 0 && strcmp(s+1, "0") != 0) {
			log_err("cannot parse netblock: '%s'", str);
			return 0;
		}
		strlcpy(buf, str, sizeof(buf));
		s = strchr(buf, '/');
		if(s) *s = 0;
		s = buf;
	}
	if(!ipstrtoaddr(s?s:str, port, addr, addrlen)) {
		log_err("cannot parse ip address: '%s'", str);
		return 0;
	}
	if(s) {
		addr_mask(addr, *addrlen, *net);
	}
	return 1;
}

/* RPZ format address dname to network byte order address */
static int ipdnametoaddr(uint8_t* dname, size_t dnamelen,
	struct sockaddr_storage* addr, socklen_t* addrlen, int* af)
{
	uint8_t* ia;
	int dnamelabs = dname_count_labels(dname);
	uint8_t lablen;
	char* e = NULL;
	int z = 0;
	size_t len = 0;
	int i;
	*af = AF_INET;

	/* need 1 byte for label length */
	if(dnamelen < 1)
		return 0;

	if(dnamelabs > 6 ||
		dname_has_label(dname, dnamelen, (uint8_t*)"\002zz")) {
		*af = AF_INET6;
	}
	len = *dname;
	lablen = *dname++;
	i = (*af == AF_INET) ? 3 : 15;
	if(*af == AF_INET6) {
		struct sockaddr_in6* sa = (struct sockaddr_in6*)addr;
		*addrlen = (socklen_t)sizeof(struct sockaddr_in6);
		memset(sa, 0, *addrlen);
		sa->sin6_family = AF_INET6;
		ia = (uint8_t*)&sa->sin6_addr;
	} else { /* ip4 */
		struct sockaddr_in* sa = (struct sockaddr_in*)addr;
		*addrlen = (socklen_t)sizeof(struct sockaddr_in);
		memset(sa, 0, *addrlen);
		sa->sin_family = AF_INET;
		ia = (uint8_t*)&sa->sin_addr;
	}
	while(lablen && i >= 0 && len <= dnamelen) {
		char buff[LDNS_MAX_LABELLEN+1];
		uint16_t chunk; /* big enough to not overflow on IPv6 hextet */
		if((*af == AF_INET && (lablen > 3 || dnamelabs > 6)) ||
			(*af == AF_INET6 && (lablen > 4 || dnamelabs > 10))) {
			return 0;
		}
		if(memcmp(dname, "zz", 2) == 0 && *af == AF_INET6) {
			/* Add one or more 0 labels. Address is initialised at
			 * 0, so just skip the zero part. */
			int zl = 11 - dnamelabs;
			if(z || zl < 0)
				return 0;
			z = 1;
			i -= (zl*2);
		} else {
			memcpy(buff, dname, lablen);
			buff[lablen] = '\0';
			chunk = strtol(buff, &e, (*af == AF_INET) ? 10 : 16);
			if(!e || *e != '\0' || (*af == AF_INET && chunk > 255))
				return 0;
			if(*af == AF_INET) {
				log_assert(i < 4 && i >= 0);
				ia[i] = (uint8_t)chunk;
				i--;
			} else {
				log_assert(i < 16 && i >= 1);
				/* ia in network byte order */
				ia[i-1] = (uint8_t)(chunk >> 8);
				ia[i] = (uint8_t)(chunk & 0x00FF);
				i -= 2;
			}
		}
		dname += lablen;
		lablen = *dname++;
		len += lablen;
	}
	if(i != -1)
		/* input too short */
		return 0;
	return 1;
}

int netblockdnametoaddr(uint8_t* dname, size_t dnamelen,
	struct sockaddr_storage* addr, socklen_t* addrlen, int* net, int* af)
{
	char buff[3 /* 3 digit netblock */ + 1];
	size_t nlablen;
	if(dnamelen < 1 || *dname > 3)
		/* netblock invalid */
		return 0;
	nlablen = *dname;

	if(dnamelen < 1 + nlablen)
		return 0;

	memcpy(buff, dname+1, nlablen);
	buff[nlablen] = '\0';
	*net = atoi(buff);
	if(*net == 0 && strcmp(buff, "0") != 0)
		return 0;
	dname += nlablen;
	dname++;
	if(!ipdnametoaddr(dname, dnamelen-1-nlablen, addr, addrlen, af))
		return 0;
	if((*af == AF_INET6 && *net > 128) || (*af == AF_INET && *net > 32))
		return 0;
	return 1;
}

int authextstrtoaddr(char* str, struct sockaddr_storage* addr, 
	socklen_t* addrlen, char** auth_name)
{
	char* s;
	int port = UNBOUND_DNS_PORT;
	if((s=strchr(str, '@'))) {
		char buf[MAX_ADDR_STRLEN];
		size_t len = (size_t)(s-str);
		char* hash = strchr(s+1, '#');
		if(hash) {
			*auth_name = hash+1;
		} else {
			*auth_name = NULL;
		}
		if(len >= MAX_ADDR_STRLEN) {
			return 0;
		}
		(void)strlcpy(buf, str, sizeof(buf));
		buf[len] = 0;
		port = atoi(s+1);
		if(port == 0) {
			if(!hash && strcmp(s+1,"0")!=0)
				return 0;
			if(hash && strncmp(s+1,"0#",2)!=0)
				return 0;
		}
		return ipstrtoaddr(buf, port, addr, addrlen);
	}
	if((s=strchr(str, '#'))) {
		char buf[MAX_ADDR_STRLEN];
		size_t len = (size_t)(s-str);
		if(len >= MAX_ADDR_STRLEN) {
			return 0;
		}
		(void)strlcpy(buf, str, sizeof(buf));
		buf[len] = 0;
		port = UNBOUND_DNS_OVER_TLS_PORT;
		*auth_name = s+1;
		return ipstrtoaddr(buf, port, addr, addrlen);
	}
	*auth_name = NULL;
	return ipstrtoaddr(str, port, addr, addrlen);
}

uint8_t* authextstrtodname(char* str, int* port, char** auth_name)
{
	char* s;
	uint8_t* dname;
	size_t dname_len;
	*port = UNBOUND_DNS_PORT;
	*auth_name = NULL;
	if((s=strchr(str, '@'))) {
		char buf[MAX_HOST_STRLEN];
		size_t len = (size_t)(s-str);
		char* hash = strchr(s+1, '#');
		if(hash) {
			*auth_name = hash+1;
		} else {
			*auth_name = NULL;
		}
		if(len >= MAX_HOST_STRLEN) {
			return NULL;
		}
		(void)strlcpy(buf, str, sizeof(buf));
		buf[len] = 0;
		*port = atoi(s+1);
		if(*port == 0) {
			if(!hash && strcmp(s+1,"0")!=0)
				return NULL;
			if(hash && strncmp(s+1,"0#",2)!=0)
				return NULL;
		}
		dname = sldns_str2wire_dname(buf, &dname_len);
	} else if((s=strchr(str, '#'))) {
		char buf[MAX_HOST_STRLEN];
		size_t len = (size_t)(s-str);
		if(len >= MAX_HOST_STRLEN) {
			return NULL;
		}
		(void)strlcpy(buf, str, sizeof(buf));
		buf[len] = 0;
		*port = UNBOUND_DNS_OVER_TLS_PORT;
		*auth_name = s+1;
		dname = sldns_str2wire_dname(buf, &dname_len);
	} else {
		dname = sldns_str2wire_dname(str, &dname_len);
	}
	return dname;
}

/** store port number into sockaddr structure */
void
sockaddr_store_port(struct sockaddr_storage* addr, socklen_t addrlen, int port)
{
	if(addr_is_ip6(addr, addrlen)) {
		struct sockaddr_in6* sa = (struct sockaddr_in6*)addr;
		sa->sin6_port = (in_port_t)htons((uint16_t)port);
	} else {
		struct sockaddr_in* sa = (struct sockaddr_in*)addr;
		sa->sin_port = (in_port_t)htons((uint16_t)port);
	}
}

void
log_nametypeclass(enum verbosity_value v, const char* str, uint8_t* name, 
	uint16_t type, uint16_t dclass)
{
	char buf[LDNS_MAX_DOMAINLEN];
	char t[12], c[12];
	const char *ts, *cs; 
	if(verbosity < v)
		return;
	dname_str(name, buf);
	if(type == LDNS_RR_TYPE_TSIG) ts = "TSIG";
	else if(type == LDNS_RR_TYPE_IXFR) ts = "IXFR";
	else if(type == LDNS_RR_TYPE_AXFR) ts = "AXFR";
	else if(type == LDNS_RR_TYPE_MAILB) ts = "MAILB";
	else if(type == LDNS_RR_TYPE_MAILA) ts = "MAILA";
	else if(type == LDNS_RR_TYPE_ANY) ts = "ANY";
	else if(sldns_rr_descript(type) && sldns_rr_descript(type)->_name)
		ts = sldns_rr_descript(type)->_name;
	else {
		snprintf(t, sizeof(t), "TYPE%d", (int)type);
		ts = t;
	}
	if(sldns_lookup_by_id(sldns_rr_classes, (int)dclass) &&
		sldns_lookup_by_id(sldns_rr_classes, (int)dclass)->name)
		cs = sldns_lookup_by_id(sldns_rr_classes, (int)dclass)->name;
	else {
		snprintf(c, sizeof(c), "CLASS%d", (int)dclass);
		cs = c;
	}
	log_info("%s %s %s %s", str, buf, ts, cs);
}

void
log_query_in(const char* str, uint8_t* name, uint16_t type, uint16_t dclass)
{
	char buf[LDNS_MAX_DOMAINLEN];
	char t[12], c[12];
	const char *ts, *cs; 
	dname_str(name, buf);
	if(type == LDNS_RR_TYPE_TSIG) ts = "TSIG";
	else if(type == LDNS_RR_TYPE_IXFR) ts = "IXFR";
	else if(type == LDNS_RR_TYPE_AXFR) ts = "AXFR";
	else if(type == LDNS_RR_TYPE_MAILB) ts = "MAILB";
	else if(type == LDNS_RR_TYPE_MAILA) ts = "MAILA";
	else if(type == LDNS_RR_TYPE_ANY) ts = "ANY";
	else if(sldns_rr_descript(type) && sldns_rr_descript(type)->_name)
		ts = sldns_rr_descript(type)->_name;
	else {
		snprintf(t, sizeof(t), "TYPE%d", (int)type);
		ts = t;
	}
	if(sldns_lookup_by_id(sldns_rr_classes, (int)dclass) &&
		sldns_lookup_by_id(sldns_rr_classes, (int)dclass)->name)
		cs = sldns_lookup_by_id(sldns_rr_classes, (int)dclass)->name;
	else {
		snprintf(c, sizeof(c), "CLASS%d", (int)dclass);
		cs = c;
	}
	if(LOG_TAG_QUERYREPLY)
		log_query("%s %s %s %s", str, buf, ts, cs);
	else	log_info("%s %s %s %s", str, buf, ts, cs);
}

void log_name_addr(enum verbosity_value v, const char* str, uint8_t* zone, 
	struct sockaddr_storage* addr, socklen_t addrlen)
{
	uint16_t port;
	const char* family = "unknown_family ";
	char namebuf[LDNS_MAX_DOMAINLEN];
	char dest[100];
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	if(verbosity < v)
		return;
	switch(af) {
		case AF_INET: family=""; break;
		case AF_INET6: family="";
			sinaddr = &((struct sockaddr_in6*)addr)->sin6_addr;
			break;
		case AF_LOCAL: family="local "; break;
		default: break;
	}
	if(inet_ntop(af, sinaddr, dest, (socklen_t)sizeof(dest)) == 0) {
		(void)strlcpy(dest, "(inet_ntop error)", sizeof(dest));
	}
	dest[sizeof(dest)-1] = 0;
	port = ntohs(((struct sockaddr_in*)addr)->sin_port);
	dname_str(zone, namebuf);
	if(af != AF_INET && af != AF_INET6)
		verbose(v, "%s <%s> %s%s#%d (addrlen %d)",
			str, namebuf, family, dest, (int)port, (int)addrlen);
	else	verbose(v, "%s <%s> %s%s#%d",
			str, namebuf, family, dest, (int)port);
}

void log_err_addr(const char* str, const char* err,
	struct sockaddr_storage* addr, socklen_t addrlen)
{
	uint16_t port;
	char dest[100];
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	if(af == AF_INET6)
		sinaddr = &((struct sockaddr_in6*)addr)->sin6_addr;
	if(inet_ntop(af, sinaddr, dest, (socklen_t)sizeof(dest)) == 0) {
		(void)strlcpy(dest, "(inet_ntop error)", sizeof(dest));
	}
	dest[sizeof(dest)-1] = 0;
	port = ntohs(((struct sockaddr_in*)addr)->sin_port);
	if(verbosity >= 4)
		log_err("%s: %s for %s port %d (len %d)", str, err, dest,
			(int)port, (int)addrlen);
	else	log_err("%s: %s for %s port %d", str, err, dest, (int)port);
}

int
sockaddr_cmp(struct sockaddr_storage* addr1, socklen_t len1, 
	struct sockaddr_storage* addr2, socklen_t len2)
{
	struct sockaddr_in* p1_in = (struct sockaddr_in*)addr1;
	struct sockaddr_in* p2_in = (struct sockaddr_in*)addr2;
	struct sockaddr_in6* p1_in6 = (struct sockaddr_in6*)addr1;
	struct sockaddr_in6* p2_in6 = (struct sockaddr_in6*)addr2;
	if(len1 < len2)
		return -1;
	if(len1 > len2)
		return 1;
	log_assert(len1 == len2);
	if( p1_in->sin_family < p2_in->sin_family)
		return -1;
	if( p1_in->sin_family > p2_in->sin_family)
		return 1;
	log_assert( p1_in->sin_family == p2_in->sin_family );
	/* compare ip4 */
	if( p1_in->sin_family == AF_INET ) {
		/* just order it, ntohs not required */
		if(p1_in->sin_port < p2_in->sin_port)
			return -1;
		if(p1_in->sin_port > p2_in->sin_port)
			return 1;
		log_assert(p1_in->sin_port == p2_in->sin_port);
		return memcmp(&p1_in->sin_addr, &p2_in->sin_addr, INET_SIZE);
	} else if (p1_in6->sin6_family == AF_INET6) {
		/* just order it, ntohs not required */
		if(p1_in6->sin6_port < p2_in6->sin6_port)
			return -1;
		if(p1_in6->sin6_port > p2_in6->sin6_port)
			return 1;
		log_assert(p1_in6->sin6_port == p2_in6->sin6_port);
		return memcmp(&p1_in6->sin6_addr, &p2_in6->sin6_addr, 
			INET6_SIZE);
	} else {
		/* eek unknown type, perform this comparison for sanity. */
		return memcmp(addr1, addr2, len1);
	}
}

int
sockaddr_cmp_addr(struct sockaddr_storage* addr1, socklen_t len1, 
	struct sockaddr_storage* addr2, socklen_t len2)
{
	struct sockaddr_in* p1_in = (struct sockaddr_in*)addr1;
	struct sockaddr_in* p2_in = (struct sockaddr_in*)addr2;
	struct sockaddr_in6* p1_in6 = (struct sockaddr_in6*)addr1;
	struct sockaddr_in6* p2_in6 = (struct sockaddr_in6*)addr2;
	if(len1 < len2)
		return -1;
	if(len1 > len2)
		return 1;
	log_assert(len1 == len2);
	if( p1_in->sin_family < p2_in->sin_family)
		return -1;
	if( p1_in->sin_family > p2_in->sin_family)
		return 1;
	log_assert( p1_in->sin_family == p2_in->sin_family );
	/* compare ip4 */
	if( p1_in->sin_family == AF_INET ) {
		return memcmp(&p1_in->sin_addr, &p2_in->sin_addr, INET_SIZE);
	} else if (p1_in6->sin6_family == AF_INET6) {
		return memcmp(&p1_in6->sin6_addr, &p2_in6->sin6_addr, 
			INET6_SIZE);
	} else {
		/* eek unknown type, perform this comparison for sanity. */
		return memcmp(addr1, addr2, len1);
	}
}

int
sockaddr_cmp_scopeid(struct sockaddr_storage* addr1, socklen_t len1,
	struct sockaddr_storage* addr2, socklen_t len2)
{
	struct sockaddr_in* p1_in = (struct sockaddr_in*)addr1;
	struct sockaddr_in* p2_in = (struct sockaddr_in*)addr2;
	struct sockaddr_in6* p1_in6 = (struct sockaddr_in6*)addr1;
	struct sockaddr_in6* p2_in6 = (struct sockaddr_in6*)addr2;
	if(len1 < len2)
		return -1;
	if(len1 > len2)
		return 1;
	log_assert(len1 == len2);
	if( p1_in->sin_family < p2_in->sin_family)
		return -1;
	if( p1_in->sin_family > p2_in->sin_family)
		return 1;
	log_assert( p1_in->sin_family == p2_in->sin_family );
	/* compare ip4 */
	if( p1_in->sin_family == AF_INET ) {
		/* just order it, ntohs not required */
		if(p1_in->sin_port < p2_in->sin_port)
			return -1;
		if(p1_in->sin_port > p2_in->sin_port)
			return 1;
		log_assert(p1_in->sin_port == p2_in->sin_port);
		return memcmp(&p1_in->sin_addr, &p2_in->sin_addr, INET_SIZE);
	} else if (p1_in6->sin6_family == AF_INET6) {
		/* just order it, ntohs not required */
		if(p1_in6->sin6_port < p2_in6->sin6_port)
			return -1;
		if(p1_in6->sin6_port > p2_in6->sin6_port)
			return 1;
		if(p1_in6->sin6_scope_id < p2_in6->sin6_scope_id)
			return -1;
		if(p1_in6->sin6_scope_id > p2_in6->sin6_scope_id)
			return 1;
		log_assert(p1_in6->sin6_port == p2_in6->sin6_port);
		return memcmp(&p1_in6->sin6_addr, &p2_in6->sin6_addr,
			INET6_SIZE);
	} else {
		/* eek unknown type, perform this comparison for sanity. */
		return memcmp(addr1, addr2, len1);
	}
}

int
addr_is_ip6(struct sockaddr_storage* addr, socklen_t len)
{
	if(len == (socklen_t)sizeof(struct sockaddr_in6) &&
		((struct sockaddr_in6*)addr)->sin6_family == AF_INET6)
		return 1;
	else    return 0;
}

void
addr_mask(struct sockaddr_storage* addr, socklen_t len, int net)
{
	uint8_t mask[8] = {0x0, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe};
	int i, max;
	uint8_t* s;
	if(addr_is_ip6(addr, len)) {
		s = (uint8_t*)&((struct sockaddr_in6*)addr)->sin6_addr;
		max = 128;
	} else {
		s = (uint8_t*)&((struct sockaddr_in*)addr)->sin_addr;
		max = 32;
	}
	if(net >= max)
		return;
	for(i=net/8+1; i<max/8; i++) {
		s[i] = 0;
	}
	s[net/8] &= mask[net&0x7];
}

int
addr_in_common(struct sockaddr_storage* addr1, int net1,
	struct sockaddr_storage* addr2, int net2, socklen_t addrlen)
{
	int min = (net1<net2)?net1:net2;
	int i, to;
	int match = 0;
	uint8_t* s1, *s2;
	if(addr_is_ip6(addr1, addrlen)) {
		s1 = (uint8_t*)&((struct sockaddr_in6*)addr1)->sin6_addr;
		s2 = (uint8_t*)&((struct sockaddr_in6*)addr2)->sin6_addr;
		to = 16;
	} else {
		s1 = (uint8_t*)&((struct sockaddr_in*)addr1)->sin_addr;
		s2 = (uint8_t*)&((struct sockaddr_in*)addr2)->sin_addr;
		to = 4;
	}
	/* match = bits_in_common(s1, s2, to); */
	for(i=0; i<to; i++) {
		if(s1[i] == s2[i]) {
			match += 8;
		} else {
			uint8_t z = s1[i]^s2[i];
			log_assert(z);
			while(!(z&0x80)) {
				match++;
				z<<=1;
			}
			break;
		}
	}
	if(match > min) match = min;
	return match;
}

void
addr_to_str(struct sockaddr_storage* addr, socklen_t addrlen,
	char* buf, size_t len)
{
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	if(addr_is_ip6(addr, addrlen))
		sinaddr = &((struct sockaddr_in6*)addr)->sin6_addr;
	if(inet_ntop(af, sinaddr, buf, (socklen_t)len) == 0) {
		snprintf(buf, len, "(inet_ntop_error)");
	}
}

int
prefixnet_is_nat64(int prefixnet)
{
	return (prefixnet == 32 || prefixnet == 40 ||
		prefixnet == 48 || prefixnet == 56 ||
		prefixnet == 64 || prefixnet == 96);
}

void
addr_to_nat64(const struct sockaddr_storage* addr,
	const struct sockaddr_storage* nat64_prefix,
	socklen_t nat64_prefixlen, int nat64_prefixnet,
	struct sockaddr_storage* nat64_addr, socklen_t* nat64_addrlen)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *sin6;
	uint8_t *v4_byte;
	int i;

	/* This needs to be checked by the caller */
	log_assert(addr->ss_family == AF_INET);
	/* Current usage is only from config values; prefix lengths enforced
	 * during config validation */
	log_assert(prefixnet_is_nat64(nat64_prefixnet));

	*nat64_addr = *nat64_prefix;
	*nat64_addrlen = nat64_prefixlen;

	sin6 = (struct sockaddr_in6 *)nat64_addr;
	sin6->sin6_flowinfo = 0;
	sin6->sin6_port = sin->sin_port;

	nat64_prefixnet = nat64_prefixnet / 8;

	v4_byte = (uint8_t *)&sin->sin_addr.s_addr;
	for(i = 0; i < 4; i++) {
		if(nat64_prefixnet == 8) {
			/* bits 64...71 are MBZ */
			sin6->sin6_addr.s6_addr[nat64_prefixnet++] = 0;
		}
		sin6->sin6_addr.s6_addr[nat64_prefixnet++] = *v4_byte++;
	}
}

int
addr_is_ip4mapped(struct sockaddr_storage* addr, socklen_t addrlen)
{
	/* prefix for ipv4 into ipv6 mapping is ::ffff:x.x.x.x */
	const uint8_t map_prefix[16] = 
		{0,0,0,0,  0,0,0,0, 0,0,0xff,0xff, 0,0,0,0};
	uint8_t* s;
	if(!addr_is_ip6(addr, addrlen))
		return 0;
	/* s is 16 octet ipv6 address string */
	s = (uint8_t*)&((struct sockaddr_in6*)addr)->sin6_addr;
	return (memcmp(s, map_prefix, 12) == 0);
}

int addr_is_ip6linklocal(struct sockaddr_storage* addr, socklen_t addrlen)
{
	const uint8_t prefix[2] = {0xfe, 0x80};
	int af = (int)((struct sockaddr_in6*)addr)->sin6_family;
	void* sin6addr = &((struct sockaddr_in6*)addr)->sin6_addr;
	uint8_t start[2];
	if(af != AF_INET6 || addrlen<(socklen_t)sizeof(struct sockaddr_in6))
		return 0;
	/* Put the first 10 bits of sin6addr in start, match fe80::/10. */
	memmove(start, sin6addr, 2);
	start[1] &= 0xc0;
	return memcmp(start, prefix, 2) == 0;
}

int addr_is_broadcast(struct sockaddr_storage* addr, socklen_t addrlen)
{
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	return af == AF_INET && addrlen>=(socklen_t)sizeof(struct sockaddr_in)
		&& memcmp(sinaddr, "\377\377\377\377", 4) == 0;
}

int addr_is_any(struct sockaddr_storage* addr, socklen_t addrlen)
{
	int af = (int)((struct sockaddr_in*)addr)->sin_family;
	void* sinaddr = &((struct sockaddr_in*)addr)->sin_addr;
	void* sin6addr = &((struct sockaddr_in6*)addr)->sin6_addr;
	if(af == AF_INET && addrlen>=(socklen_t)sizeof(struct sockaddr_in)
		&& memcmp(sinaddr, "\000\000\000\000", 4) == 0)
		return 1;
	else if(af==AF_INET6 && addrlen>=(socklen_t)sizeof(struct sockaddr_in6)
		&& memcmp(sin6addr, "\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000", 16) == 0)
		return 1;
	return 0;
}

void sock_list_insert(struct sock_list** list, struct sockaddr_storage* addr,
	socklen_t len, struct regional* region)
{
	struct sock_list* add = (struct sock_list*)regional_alloc(region,
		sizeof(*add) - sizeof(add->addr) + (size_t)len);
	if(!add) {
		log_err("out of memory in socketlist insert");
		return;
	}
	log_assert(list);
	add->next = *list;
	add->len = len;
	*list = add;
	if(len) memmove(&add->addr, addr, len);
}

void sock_list_prepend(struct sock_list** list, struct sock_list* add)
{
	struct sock_list* last = add;
	if(!last) 
		return;
	while(last->next)
		last = last->next;
	last->next = *list;
	*list = add;
}

int sock_list_find(struct sock_list* list, struct sockaddr_storage* addr,
        socklen_t len)
{
	while(list) {
		if(len == list->len) {
			if(len == 0 || sockaddr_cmp_addr(addr, len, 
				&list->addr, list->len) == 0)
				return 1;
		}
		list = list->next;
	}
	return 0;
}

void sock_list_merge(struct sock_list** list, struct regional* region,
	struct sock_list* add)
{
	struct sock_list* p;
	for(p=add; p; p=p->next) {
		if(!sock_list_find(*list, &p->addr, p->len))
			sock_list_insert(list, &p->addr, p->len, region);
	}
}

void
log_crypto_err(const char* str)
{
#ifdef HAVE_SSL
	log_crypto_err_code(str, ERR_get_error());
#else
	(void)str;
#endif /* HAVE_SSL */
}

void log_crypto_err_code(const char* str, unsigned long err)
{
#ifdef HAVE_SSL
	/* error:[error code]:[library name]:[function name]:[reason string] */
	char buf[128];
	unsigned long e;
	ERR_error_string_n(err, buf, sizeof(buf));
	log_err("%s crypto %s", str, buf);
	while( (e=ERR_get_error()) ) {
		ERR_error_string_n(e, buf, sizeof(buf));
		log_err("and additionally crypto %s", buf);
	}
#else
	(void)str;
	(void)err;
#endif /* HAVE_SSL */
}

#ifdef HAVE_SSL
/** Print crypt erro with SSL_get_error want code and err_get_error code */
static void log_crypto_err_io_code_arg(const char* str, int r,
	unsigned long err, int err_present)
{
	int print_errno = 0, print_crypto_err = 0;
	const char* inf = NULL;

	switch(r) {
	case SSL_ERROR_NONE:
		inf = "no error";
		break;
	case SSL_ERROR_ZERO_RETURN:
		inf = "channel closed";
		break;
	case SSL_ERROR_WANT_READ:
		inf = "want read";
		break;
	case SSL_ERROR_WANT_WRITE:
		inf = "want write";
		break;
	case SSL_ERROR_WANT_CONNECT:
		inf = "want connect";
		break;
	case SSL_ERROR_WANT_ACCEPT:
		inf = "want accept";
		break;
	case SSL_ERROR_WANT_X509_LOOKUP:
		inf = "want X509 lookup";
		break;
#ifdef SSL_ERROR_WANT_ASYNC
	case SSL_ERROR_WANT_ASYNC:
		inf = "want async";
		break;
#endif
#ifdef SSL_ERROR_WANT_ASYNC_JOB
	case SSL_ERROR_WANT_ASYNC_JOB:
		inf = "want async job";
		break;
#endif
#ifdef SSL_ERROR_WANT_CLIENT_HELLO_CB
	case SSL_ERROR_WANT_CLIENT_HELLO_CB:
		inf = "want client hello cb";
		break;
#endif
	case SSL_ERROR_SYSCALL:
		print_errno = 1;
		inf = "syscall";
		break;
	case SSL_ERROR_SSL:
		print_crypto_err = 1;
		inf = "SSL, usually protocol, error";
		break;
	default:
		inf = "unknown SSL_get_error result code";
		print_errno = 1;
		print_crypto_err = 1;
	}
	if(print_crypto_err) {
		if(print_errno) {
			char buf[1024];
			snprintf(buf, sizeof(buf), "%s with errno %s",
				str, strerror(errno));
			if(err_present)
				log_crypto_err_code(buf, err);
			else	log_crypto_err(buf);
		} else {
			if(err_present)
				log_crypto_err_code(str, err);
			else	log_crypto_err(str);
		}
	} else {
		if(print_errno) {
			if(errno == 0)
				log_err("%s: syscall error with errno %s",
					str, strerror(errno));
			else log_err("%s: %s", str, strerror(errno));
		} else {
			log_err("%s: %s", str, inf);
		}
	}
}
#endif /* HAVE_SSL */

void log_crypto_err_io(const char* str, int r)
{
#ifdef HAVE_SSL
	log_crypto_err_io_code_arg(str, r, 0, 0);
#else
	(void)str;
	(void)r;
#endif /* HAVE_SSL */
}

void log_crypto_err_io_code(const char* str, int r, unsigned long err)
{
#ifdef HAVE_SSL
	log_crypto_err_io_code_arg(str, r, err, 1);
#else
	(void)str;
	(void)r;
	(void)err;
#endif /* HAVE_SSL */
}

#ifdef HAVE_SSL
/** log certificate details */
void
log_cert(unsigned level, const char* str, void* cert)
{
	BIO* bio;
	char nul = 0;
	char* pp = NULL;
	long len;
	if(verbosity < level) return;
	bio = BIO_new(BIO_s_mem());
	if(!bio) return;
	X509_print_ex(bio, (X509*)cert, 0, (unsigned long)-1
		^(X509_FLAG_NO_SUBJECT
                        |X509_FLAG_NO_ISSUER|X509_FLAG_NO_VALIDITY
			|X509_FLAG_NO_EXTENSIONS|X509_FLAG_NO_AUX
			|X509_FLAG_NO_ATTRIBUTES));
	BIO_write(bio, &nul, (int)sizeof(nul));
	len = BIO_get_mem_data(bio, &pp);
	if(len != 0 && pp) {
		/* reduce size of cert printout */
		char* s;
		while((s=strstr(pp, "  "))!=NULL)
			memmove(s, s+1, strlen(s+1)+1);
		while((s=strstr(pp, "\t\t"))!=NULL)
			memmove(s, s+1, strlen(s+1)+1);
		verbose(level, "%s: \n%s", str, pp);
	}
	BIO_free(bio);
}
#endif /* HAVE_SSL */

#if defined(HAVE_SSL) && defined(HAVE_SSL_CTX_SET_ALPN_SELECT_CB)
static int
dot_alpn_select_cb(SSL* ATTR_UNUSED(ssl), const unsigned char** out,
	unsigned char* outlen, const unsigned char* in, unsigned int inlen,
	void* ATTR_UNUSED(arg))
{
	static const unsigned char alpns[] = { 3, 'd', 'o', 't' };
	unsigned char* tmp_out;
	int ret;
	ret = SSL_select_next_proto(&tmp_out, outlen, alpns, sizeof(alpns), in, inlen);
	if(ret == OPENSSL_NPN_NO_OVERLAP) {
		/* Client sent ALPN but no overlap. Should have been error,
		 * but for privacy we continue without ALPN (e.g., if certain
		 * ALPNs are blocked) */
		return SSL_TLSEXT_ERR_NOACK;
	}
	*out = tmp_out;
	return SSL_TLSEXT_ERR_OK;
}
#endif

#if defined(HAVE_SSL) && defined(HAVE_NGHTTP2) && defined(HAVE_SSL_CTX_SET_ALPN_SELECT_CB)
static int doh_alpn_select_cb(SSL* ATTR_UNUSED(ssl), const unsigned char** out,
	unsigned char* outlen, const unsigned char* in, unsigned int inlen,
	void* ATTR_UNUSED(arg))
{
	int rv = nghttp2_select_next_protocol((unsigned char **)out, outlen, in,
		inlen);
	if(rv == -1) {
		return SSL_TLSEXT_ERR_NOACK;
	}
	/* either http/1.1 or h2 selected */
	return SSL_TLSEXT_ERR_OK;
}
#endif

#ifdef HAVE_SSL
/* setup the callback for ticket keys */
static int
setup_ticket_keys_cb(void* sslctx)
{
#  ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
	if(SSL_CTX_set_tlsext_ticket_key_evp_cb(sslctx, tls_session_ticket_key_cb) == 0) {
		return 0;
	}
#  else
	if(SSL_CTX_set_tlsext_ticket_key_cb(sslctx, tls_session_ticket_key_cb) == 0) {
		return 0;
	}
#  endif
	return 1;
}
#endif /* HAVE_SSL */

int
listen_sslctx_setup(void* ctxt)
{
#ifdef HAVE_SSL
	SSL_CTX* ctx = (SSL_CTX*)ctxt;
	/* no SSLv2, SSLv3 because has defects */
#if SSL_OP_NO_SSLv2 != 0
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2) & SSL_OP_NO_SSLv2)
		!= SSL_OP_NO_SSLv2){
		log_crypto_err("could not set SSL_OP_NO_SSLv2");
		return 0;
	}
#endif
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3) & SSL_OP_NO_SSLv3)
		!= SSL_OP_NO_SSLv3){
		log_crypto_err("could not set SSL_OP_NO_SSLv3");
		return 0;
	}
#if defined(SSL_OP_NO_TLSv1) && defined(SSL_OP_NO_TLSv1_1)
	/* if we have tls 1.1 disable 1.0 */
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1) & SSL_OP_NO_TLSv1)
		!= SSL_OP_NO_TLSv1){
		log_crypto_err("could not set SSL_OP_NO_TLSv1");
		return 0;
	}
#endif
#if defined(SSL_OP_NO_TLSv1_1) && defined(SSL_OP_NO_TLSv1_2)
	/* if we have tls 1.2 disable 1.1 */
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1_1) & SSL_OP_NO_TLSv1_1)
		!= SSL_OP_NO_TLSv1_1){
		log_crypto_err("could not set SSL_OP_NO_TLSv1_1");
		return 0;
	}
#endif
#if defined(SSL_OP_NO_RENEGOTIATION)
	/* disable client renegotiation */
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_RENEGOTIATION) &
		SSL_OP_NO_RENEGOTIATION) != SSL_OP_NO_RENEGOTIATION) {
		log_crypto_err("could not set SSL_OP_NO_RENEGOTIATION");
		return 0;
	}
#endif
#if defined(SHA256_DIGEST_LENGTH) && defined(USE_ECDSA)
	/* if we detect system-wide crypto policies, use those */
	if (access( "/etc/crypto-policies/config", F_OK ) != 0 ) {
	/* if we have sha256, set the cipher list to have no known vulns */
		if(!SSL_CTX_set_cipher_list(ctx, "TLS13-CHACHA20-POLY1305-SHA256:TLS13-AES-256-GCM-SHA384:TLS13-AES-128-GCM-SHA256:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256"))
			log_crypto_err("could not set cipher list with SSL_CTX_set_cipher_list");
	}
#endif
#if defined(SSL_OP_IGNORE_UNEXPECTED_EOF)
	/* ignore errors when peers do not send the mandatory close_notify
	 * alert on shutdown.
	 * Relevant for openssl >= 3 */
	if((SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF) &
		SSL_OP_IGNORE_UNEXPECTED_EOF) != SSL_OP_IGNORE_UNEXPECTED_EOF) {
		log_crypto_err("could not set SSL_OP_IGNORE_UNEXPECTED_EOF");
		return 0;
	}
#endif

	if((SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE) &
		SSL_OP_CIPHER_SERVER_PREFERENCE) !=
		SSL_OP_CIPHER_SERVER_PREFERENCE) {
		log_crypto_err("could not set SSL_OP_CIPHER_SERVER_PREFERENCE");
		return 0;
	}

#ifdef HAVE_SSL_CTX_SET_SECURITY_LEVEL
	SSL_CTX_set_security_level(ctx, 0);
#endif
#else
	(void)ctxt;
#endif /* HAVE_SSL */
	return 1;
}

void
listen_sslctx_setup_2(void* ctxt)
{
#ifdef HAVE_SSL
	SSL_CTX* ctx = (SSL_CTX*)ctxt;
	(void)ctx;
#if HAVE_DECL_SSL_CTX_SET_ECDH_AUTO
	if(!SSL_CTX_set_ecdh_auto(ctx,1)) {
		log_crypto_err("Error in SSL_CTX_ecdh_auto, not enabling ECDHE");
	}
#elif defined(USE_ECDSA) && defined(HAVE_SSL_CTX_SET_TMP_ECDH)
	if(1) {
		EC_KEY *ecdh = EC_KEY_new_by_curve_name (NID_X9_62_prime256v1);
		if (!ecdh) {
			log_crypto_err("could not find p256, not enabling ECDHE");
		} else {
			if (1 != SSL_CTX_set_tmp_ecdh (ctx, ecdh)) {
				log_crypto_err("Error in SSL_CTX_set_tmp_ecdh, not enabling ECDHE");
			}
			EC_KEY_free (ecdh);
		}
	}
#endif
#else
	(void)ctxt;
#endif /* HAVE_SSL */
}

void* listen_sslctx_create(const char* key, const char* pem,
	const char* verifypem, const char* tls_ciphers,
	const char* tls_ciphersuites, int set_ticket_keys_cb,
	int is_dot, int is_doh)
{
#ifdef HAVE_SSL
	SSL_CTX* ctx = SSL_CTX_new(SSLv23_server_method());
	if(!ctx) {
		log_crypto_err("could not SSL_CTX_new");
		return NULL;
	}
	if(!key || key[0] == 0) {
		log_err("error: no tls-service-key file specified");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!pem || pem[0] == 0) {
		log_err("error: no tls-service-pem file specified");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!listen_sslctx_setup(ctx)) {
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!SSL_CTX_use_certificate_chain_file(ctx, pem)) {
		log_err("error for cert file: %s", pem);
		log_crypto_err("error in SSL_CTX use_certificate_chain_file");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM)) {
		log_err("error for private key file: %s", key);
		log_crypto_err("Error in SSL_CTX use_PrivateKey_file");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!SSL_CTX_check_private_key(ctx)) {
		log_err("error for key file: %s", key);
		log_crypto_err("Error in SSL_CTX check_private_key");
		SSL_CTX_free(ctx);
		return NULL;
	}
	listen_sslctx_setup_2(ctx);
	if(verifypem && verifypem[0]) {
		if(!SSL_CTX_load_verify_locations(ctx, verifypem, NULL)) {
			log_crypto_err("Error in SSL_CTX verify locations");
			SSL_CTX_free(ctx);
			return NULL;
		}
		SSL_CTX_set_client_CA_list(ctx, SSL_load_client_CA_file(
			verifypem));
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
	}
	if(tls_ciphers && tls_ciphers[0]) {
		if (!SSL_CTX_set_cipher_list(ctx, tls_ciphers)) {
			log_err("failed to set tls-cipher %s",
				tls_ciphers);
			log_crypto_err("Error in SSL_CTX_set_cipher_list");
			SSL_CTX_free(ctx);
			return NULL;
		}
	}
#ifdef HAVE_SSL_CTX_SET_CIPHERSUITES
	if(tls_ciphersuites && tls_ciphersuites[0]) {
		if (!SSL_CTX_set_ciphersuites(ctx, tls_ciphersuites)) {
			log_err("failed to set tls-ciphersuites %s",
				tls_ciphersuites);
			log_crypto_err("Error in SSL_CTX_set_ciphersuites");
			SSL_CTX_free(ctx);
			return NULL;
		}
	}
#else
	(void)tls_ciphersuites; /* variable unused. */
#endif /* HAVE_SSL_CTX_SET_CIPHERSUITES */
	if(set_ticket_keys_cb) {
		if(!setup_ticket_keys_cb(ctx)) {
			log_crypto_err("no support for TLS session ticket");
			SSL_CTX_free(ctx);
			return NULL;
		}
	}
	/* setup ALPN */
#if defined(HAVE_SSL_CTX_SET_ALPN_SELECT_CB)
	if(is_dot) {
		SSL_CTX_set_alpn_select_cb(ctx, dot_alpn_select_cb, NULL);
	} else if(is_doh) {
#if defined(HAVE_NGHTTP2)
		SSL_CTX_set_alpn_select_cb(ctx, doh_alpn_select_cb, NULL);
#endif
	}
#endif /* HAVE_SSL_CTX_SET_ALPN_SELECT_CB */
	return ctx;
#else
	(void)key; (void)pem; (void)verifypem;
	(void)tls_ciphers; (void)tls_ciphersuites;
	(void)set_ticket_keys_cb; (void)is_dot; (void)is_doh;
	return NULL;
#endif /* HAVE_SSL */
}

#ifdef USE_WINSOCK
/* For windows, the CA trust store is not read by openssl.
   Add code to open the trust store using wincrypt API and add
   the root certs into openssl trust store */
static int
add_WIN_cacerts_to_openssl_store(SSL_CTX* tls_ctx)
{
	HCERTSTORE      hSystemStore;
	PCCERT_CONTEXT  pTargetCert = NULL;
	X509_STORE*	store;

	verbose(VERB_ALGO, "Adding Windows certificates from system root store to CA store");

	/* load just once per context lifetime for this version
	   TODO: dynamically update CA trust changes as they are available */
	if (!tls_ctx)
		return 0;

	/* Call wincrypt's CertOpenStore to open the CA root store. */

	if ((hSystemStore = CertOpenStore(
		CERT_STORE_PROV_SYSTEM,
		0,
		0,
		/* NOTE: mingw does not have this const: replace with 1 << 16 from code 
		   CERT_SYSTEM_STORE_CURRENT_USER, */
		1 << 16,
		L"root")) == 0)
	{
		return 0;
	}

	store = SSL_CTX_get_cert_store(tls_ctx);
	if (!store)
		return 0;

	/* failure if the CA store is empty or the call fails */
	if ((pTargetCert = CertEnumCertificatesInStore(
		hSystemStore, pTargetCert)) == 0) {
		verbose(VERB_ALGO, "CA certificate store for Windows is empty.");
		return 0;
	}
	/* iterate over the windows cert store and add to openssl store */
	do
	{
		X509 *cert1 = d2i_X509(NULL,
			(const unsigned char **)&pTargetCert->pbCertEncoded,
			pTargetCert->cbCertEncoded);
		if (!cert1) {
			unsigned long error = ERR_get_error();
			/* return error if a cert fails */
			verbose(VERB_ALGO, "%s %d:%s",
				"Unable to parse certificate in memory",
				(int)error, ERR_error_string(error, NULL));
			return 0;
		}
		else {
			/* return error if a cert add to store fails */
			if (X509_STORE_add_cert(store, cert1) == 0) {
				unsigned long error = ERR_peek_last_error();

				/* Ignore error X509_R_CERT_ALREADY_IN_HASH_TABLE which means the
				* certificate is already in the store.  */
				if(ERR_GET_LIB(error) != ERR_LIB_X509 ||
					ERR_GET_REASON(error) != X509_R_CERT_ALREADY_IN_HASH_TABLE) {
					error = ERR_get_error();
					verbose(VERB_ALGO, "%s %d:%s\n",
					    "Error adding certificate", (int)error,
					     ERR_error_string(error, NULL));
					X509_free(cert1);
					return 0;
				}
			}
			X509_free(cert1);
		}
	} while ((pTargetCert = CertEnumCertificatesInStore(
		hSystemStore, pTargetCert)) != 0);

	/* Clean up memory and quit. */
	if (pTargetCert)
		CertFreeCertificateContext(pTargetCert);
	if (hSystemStore)
	{
		if (!CertCloseStore(
			hSystemStore, 0))
			return 0;
	}
	verbose(VERB_ALGO, "Completed adding Windows certificates to CA store successfully");
	return 1;
}
#endif /* USE_WINSOCK */

void* connect_sslctx_create(char* key, char* pem, char* verifypem, int wincert)
{
#ifdef HAVE_SSL
	SSL_CTX* ctx = SSL_CTX_new(SSLv23_client_method());
	if(!ctx) {
		log_crypto_err("could not allocate SSL_CTX pointer");
		return NULL;
	}
#if SSL_OP_NO_SSLv2 != 0
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2) & SSL_OP_NO_SSLv2)
		!= SSL_OP_NO_SSLv2) {
		log_crypto_err("could not set SSL_OP_NO_SSLv2");
		SSL_CTX_free(ctx);
		return NULL;
	}
#endif
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3) & SSL_OP_NO_SSLv3)
		!= SSL_OP_NO_SSLv3) {
		log_crypto_err("could not set SSL_OP_NO_SSLv3");
		SSL_CTX_free(ctx);
		return NULL;
	}
#if defined(SSL_OP_NO_RENEGOTIATION)
	/* disable client renegotiation */
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_RENEGOTIATION) &
		SSL_OP_NO_RENEGOTIATION) != SSL_OP_NO_RENEGOTIATION) {
		log_crypto_err("could not set SSL_OP_NO_RENEGOTIATION");
		SSL_CTX_free(ctx);
		return 0;
	}
#endif
#if defined(SSL_OP_IGNORE_UNEXPECTED_EOF)
	/* ignore errors when peers do not send the mandatory close_notify
	 * alert on shutdown.
	 * Relevant for openssl >= 3 */
	if((SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF) &
		SSL_OP_IGNORE_UNEXPECTED_EOF) != SSL_OP_IGNORE_UNEXPECTED_EOF) {
		log_crypto_err("could not set SSL_OP_IGNORE_UNEXPECTED_EOF");
		SSL_CTX_free(ctx);
		return 0;
	}
#endif
	if(key && key[0]) {
		if(!SSL_CTX_use_certificate_chain_file(ctx, pem)) {
			log_err("error in client certificate %s", pem);
			log_crypto_err("error in certificate file");
			SSL_CTX_free(ctx);
			return NULL;
		}
		if(!SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM)) {
			log_err("error in client private key %s", key);
			log_crypto_err("error in key file");
			SSL_CTX_free(ctx);
			return NULL;
		}
		if(!SSL_CTX_check_private_key(ctx)) {
			log_err("error in client key %s", key);
			log_crypto_err("error in SSL_CTX_check_private_key");
			SSL_CTX_free(ctx);
			return NULL;
		}
	}
	if((verifypem && verifypem[0]) || wincert) {
		if(verifypem && verifypem[0]) {
			if(!SSL_CTX_load_verify_locations(ctx, verifypem, NULL)) {
				log_crypto_err("error in SSL_CTX verify");
				SSL_CTX_free(ctx);
				return NULL;
			}
		}
#ifdef USE_WINSOCK
		if(wincert) {
			if(!add_WIN_cacerts_to_openssl_store(ctx)) {
				log_crypto_err("error in add_WIN_cacerts_to_openssl_store");
				SSL_CTX_free(ctx);
				return NULL;
			}
		}
#else
		if(wincert) {
			if(!SSL_CTX_set_default_verify_paths(ctx)) {
				log_crypto_err("error in default_verify_paths");
				SSL_CTX_free(ctx);
				return NULL;
			}
		}
#endif
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
	}
	return ctx;
#else
	(void)key; (void)pem; (void)verifypem; (void)wincert;
	return NULL;
#endif
}

void* incoming_ssl_fd(void* sslctx, int fd)
{
#ifdef HAVE_SSL
	SSL* ssl = SSL_new((SSL_CTX*)sslctx);
	if(!ssl) {
		log_crypto_err("could not SSL_new");
		return NULL;
	}
	SSL_set_accept_state(ssl);
	(void)SSL_set_mode(ssl, (long)SSL_MODE_AUTO_RETRY);
	if(!SSL_set_fd(ssl, fd)) {
		log_crypto_err("could not SSL_set_fd");
		SSL_free(ssl);
		return NULL;
	}
	return ssl;
#else
	(void)sslctx; (void)fd;
	return NULL;
#endif
}

void* outgoing_ssl_fd(void* sslctx, int fd)
{
#ifdef HAVE_SSL
	SSL* ssl = SSL_new((SSL_CTX*)sslctx);
	if(!ssl) {
		log_crypto_err("could not SSL_new");
		return NULL;
	}
	SSL_set_connect_state(ssl);
	(void)SSL_set_mode(ssl, (long)SSL_MODE_AUTO_RETRY);
	if(!SSL_set_fd(ssl, fd)) {
		log_crypto_err("could not SSL_set_fd");
		SSL_free(ssl);
		return NULL;
	}
	return ssl;
#else
	(void)sslctx; (void)fd;
	return NULL;
#endif
}

int check_auth_name_for_ssl(char* auth_name)
{
	if(!auth_name) return 1;
#if defined(HAVE_SSL) && !defined(HAVE_SSL_SET1_HOST) && !defined(HAVE_X509_VERIFY_PARAM_SET1_HOST)
	log_err("the query has an auth_name %s, but libssl has no call to "
		"perform TLS authentication.  Remove that name from config "
		"or upgrade the ssl crypto library.", auth_name);
	return 0;
#else
	return 1;
#endif
}

/** set the authname on an SSL structure, SSL* ssl */
int set_auth_name_on_ssl(void* ssl, char* auth_name, int use_sni)
{
	if(!auth_name) return 1;
#ifdef HAVE_SSL
	if(use_sni) {
		(void)SSL_set_tlsext_host_name(ssl, auth_name);
	}
#else
	(void)ssl;
	(void)use_sni;
#endif
#ifdef HAVE_SSL_SET1_HOST
	SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
	/* setting the hostname makes openssl verify the
	 * host name in the x509 certificate in the
	 * SSL connection*/
	if(!SSL_set1_host(ssl, auth_name)) {
		log_err("SSL_set1_host failed");
		return 0;
	}
#elif defined(HAVE_X509_VERIFY_PARAM_SET1_HOST)
	/* openssl 1.0.2 has this function that can be used for
	 * set1_host like verification */
	if(auth_name) {
		X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
#  ifdef X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS
		X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
#  endif
		if(!X509_VERIFY_PARAM_set1_host(param, auth_name, strlen(auth_name))) {
			log_err("X509_VERIFY_PARAM_set1_host failed");
			return 0;
		}
		SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
	}
#else
	verbose(VERB_ALGO, "the query has an auth_name, but libssl has no call to perform TLS authentication");
#endif /* HAVE_SSL_SET1_HOST */
	return 1;
}

#if defined(HAVE_SSL) && defined(OPENSSL_THREADS) && !defined(THREADS_DISABLED) && defined(CRYPTO_LOCK) && OPENSSL_VERSION_NUMBER < 0x10100000L
/** global lock list for openssl locks */
static lock_basic_type *ub_openssl_locks = NULL;

/** callback that gets thread id for openssl */
#ifdef HAVE_CRYPTO_THREADID_SET_CALLBACK
static void
ub_crypto_id_cb(CRYPTO_THREADID *id)
{
	CRYPTO_THREADID_set_numeric(id, (unsigned long)log_thread_get());
}
#else
static unsigned long
ub_crypto_id_cb(void)
{
	return (unsigned long)log_thread_get();
}
#endif

static void
ub_crypto_lock_cb(int mode, int type, const char *ATTR_UNUSED(file),
	int ATTR_UNUSED(line))
{
	if((mode&CRYPTO_LOCK)) {
		lock_basic_lock(&ub_openssl_locks[type]);
	} else {
		lock_basic_unlock(&ub_openssl_locks[type]);
	}
}
#endif /* OPENSSL_THREADS */

int ub_openssl_lock_init(void)
{
#if defined(HAVE_SSL) && defined(OPENSSL_THREADS) && !defined(THREADS_DISABLED) && defined(CRYPTO_LOCK) && OPENSSL_VERSION_NUMBER < 0x10100000L
	int i;
	ub_openssl_locks = (lock_basic_type*)reallocarray(
		NULL, (size_t)CRYPTO_num_locks(), sizeof(lock_basic_type));
	if(!ub_openssl_locks)
		return 0;
	for(i=0; i<CRYPTO_num_locks(); i++) {
		lock_basic_init(&ub_openssl_locks[i]);
	}
#  ifdef HAVE_CRYPTO_THREADID_SET_CALLBACK
	CRYPTO_THREADID_set_callback(&ub_crypto_id_cb);
#  else
	CRYPTO_set_id_callback(&ub_crypto_id_cb);
#  endif
	CRYPTO_set_locking_callback(&ub_crypto_lock_cb);
#endif /* OPENSSL_THREADS */
	return 1;
}

void ub_openssl_lock_delete(void)
{
#if defined(HAVE_SSL) && defined(OPENSSL_THREADS) && !defined(THREADS_DISABLED) && defined(CRYPTO_LOCK) && OPENSSL_VERSION_NUMBER < 0x10100000L
	int i;
	if(!ub_openssl_locks)
		return;
#  ifdef HAVE_CRYPTO_THREADID_SET_CALLBACK
	CRYPTO_THREADID_set_callback(NULL);
#  else
	CRYPTO_set_id_callback(NULL);
#  endif
	CRYPTO_set_locking_callback(NULL);
	for(i=0; i<CRYPTO_num_locks(); i++) {
		lock_basic_destroy(&ub_openssl_locks[i]);
	}
	free(ub_openssl_locks);
#endif /* OPENSSL_THREADS */
}

int listen_sslctx_setup_ticket_keys(struct config_strlist* tls_session_ticket_keys) {
#ifdef HAVE_SSL
	size_t s = 1;
	struct config_strlist* p;
	struct tls_session_ticket_key *keys;
	for(p = tls_session_ticket_keys; p; p = p->next) {
		s++;
	}
	keys = calloc(s, sizeof(struct tls_session_ticket_key));
	if(!keys)
		return 0;
	memset(keys, 0, s*sizeof(*keys));
	ticket_keys = keys;

	for(p = tls_session_ticket_keys; p; p = p->next) {
		size_t n;
		unsigned char *data;
		FILE *f;

		data = (unsigned char *)malloc(80);
		if(!data)
			return 0;

		f = fopen(p->str, "rb");
		if(!f) {
			log_err("could not read tls-session-ticket-key %s: %s", p->str, strerror(errno));
			free(data);
			return 0;
		}
		n = fread(data, 1, 80, f);
		fclose(f);

		if(n != 80) {
			log_err("tls-session-ticket-key %s is %d bytes, must be 80 bytes", p->str, (int)n);
			free(data);
			return 0;
		}
		verbose(VERB_OPS, "read tls-session-ticket-key: %s", p->str);

		keys->key_name = data;
		keys->aes_key = data + 16;
		keys->hmac_key = data + 48;
		keys++;
	}
	/* terminate array with NULL key name entry */
	keys->key_name = NULL;
	return 1;
#else
	(void)tls_session_ticket_keys;
	return 0;
#endif
}

#ifdef HAVE_SSL
int tls_session_ticket_key_cb(SSL *ATTR_UNUSED(sslctx), unsigned char* key_name,
	unsigned char* iv, EVP_CIPHER_CTX *evp_sctx,
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
	EVP_MAC_CTX *hmac_ctx,
#else
	HMAC_CTX* hmac_ctx,
#endif
	int enc)
{
#ifdef HAVE_SSL
#  ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
	OSSL_PARAM params[3];
#  else
	const EVP_MD *digest;
#  endif
	const EVP_CIPHER *cipher;
	int evp_cipher_length;
#  ifndef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
	digest = EVP_sha256();
#  endif
	cipher = EVP_aes_256_cbc();
	evp_cipher_length = EVP_CIPHER_iv_length(cipher);
	if( enc == 1 ) {
		/* encrypt */
		verbose(VERB_CLIENT, "start session encrypt");
		memcpy(key_name, ticket_keys->key_name, 16);
		if (RAND_bytes(iv, evp_cipher_length) != 1) {
			verbose(VERB_CLIENT, "RAND_bytes failed");
			return -1;
		}
		if (EVP_EncryptInit_ex(evp_sctx, cipher, NULL, ticket_keys->aes_key, iv) != 1) {
			verbose(VERB_CLIENT, "EVP_EncryptInit_ex failed");
			return -1;
		}
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
		params[0] = OSSL_PARAM_construct_octet_string(OSSL_MAC_PARAM_KEY,
			ticket_keys->hmac_key, 32);
		params[1] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
			"sha256", 0);
		params[2] = OSSL_PARAM_construct_end();
#ifdef HAVE_EVP_MAC_CTX_SET_PARAMS
		EVP_MAC_CTX_set_params(hmac_ctx, params);
#else
		EVP_MAC_set_ctx_params(hmac_ctx, params);
#endif
#elif !defined(HMAC_INIT_EX_RETURNS_VOID)
		if (HMAC_Init_ex(hmac_ctx, ticket_keys->hmac_key, 32, digest, NULL) != 1) {
			verbose(VERB_CLIENT, "HMAC_Init_ex failed");
			return -1;
		}
#else
		HMAC_Init_ex(hmac_ctx, ticket_keys->hmac_key, 32, digest, NULL);
#endif
		return 1;
	} else if (enc == 0) {
		/* decrypt */
		struct tls_session_ticket_key *key;
		verbose(VERB_CLIENT, "start session decrypt");
		for(key = ticket_keys; key->key_name != NULL; key++) {
			if (!memcmp(key_name, key->key_name, 16)) {
				verbose(VERB_CLIENT, "Found session_key");
				break;
			}
		}
		if(key->key_name == NULL) {
			verbose(VERB_CLIENT, "Not found session_key");
			return 0;
		}

#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
		params[0] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
			key->hmac_key, 32);
		params[1] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
			"sha256", 0);
		params[2] = OSSL_PARAM_construct_end();
#ifdef HAVE_EVP_MAC_CTX_SET_PARAMS
		EVP_MAC_CTX_set_params(hmac_ctx, params);
#else
		EVP_MAC_set_ctx_params(hmac_ctx, params);
#endif
#elif !defined(HMAC_INIT_EX_RETURNS_VOID)
		if (HMAC_Init_ex(hmac_ctx, key->hmac_key, 32, digest, NULL) != 1) {
			verbose(VERB_CLIENT, "HMAC_Init_ex failed");
			return -1;
		}
#else
		HMAC_Init_ex(hmac_ctx, key->hmac_key, 32, digest, NULL);
#endif
		if (EVP_DecryptInit_ex(evp_sctx, cipher, NULL, key->aes_key, iv) != 1) {
			log_err("EVP_DecryptInit_ex failed");
			return -1;
		}

		return (key == ticket_keys) ? 1 : 2;
	}
	return -1;
#else
	(void)key_name;
	(void)iv;
	(void)evp_sctx;
	(void)hmac_ctx;
	(void)enc;
	return 0;
#endif
}
#endif /* HAVE_SSL */

#ifdef HAVE_SSL
void
listen_sslctx_delete_ticket_keys(void)
{
	struct tls_session_ticket_key *key;
	if(!ticket_keys) return;
	for(key = ticket_keys; key->key_name != NULL; key++) {
		/* wipe key data from memory*/
#ifdef HAVE_EXPLICIT_BZERO
		explicit_bzero(key->key_name, 80);
#else
		memset(key->key_name, 0xdd, 80);
#endif
		free(key->key_name);
	}
	free(ticket_keys);
	ticket_keys = NULL;
}
#endif /* HAVE_SSL */

#  ifndef USE_WINSOCK
char*
sock_strerror(int errn)
{
	return strerror(errn);
}

void
sock_close(int socket)
{
	close(socket);
}

#  else
char*
sock_strerror(int ATTR_UNUSED(errn))
{
	return wsa_strerror(WSAGetLastError());
}

void
sock_close(int socket)
{
	closesocket(socket);
}
#  endif /* USE_WINSOCK */

ssize_t
hex_ntop(uint8_t const *src, size_t srclength, char *target, size_t targsize)
{
	static char hexdigits[] = {
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	};
	size_t i;

	if (targsize < srclength * 2 + 1) {
		return -1;
	}

	for (i = 0; i < srclength; ++i) {
		*target++ = hexdigits[src[i] >> 4U];
		*target++ = hexdigits[src[i] & 0xfU];
	}
	*target = '\0';
	return 2 * srclength;
}

ssize_t
hex_pton(const char* src, uint8_t* target, size_t targsize)
{
	uint8_t *t = target;
	if(strlen(src) % 2 != 0 || strlen(src)/2 > targsize) {
		return -1;
	}
	while(*src) {
		if(!isxdigit((unsigned char)src[0]) ||
			!isxdigit((unsigned char)src[1]))
			return -1;
		*t++ = sldns_hexdigit_to_int(src[0]) * 16 +
			sldns_hexdigit_to_int(src[1]) ;
		src += 2;
	}
	return t-target;
}
