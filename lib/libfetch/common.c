/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998-2016 Dag-Erling Smørgrav
 * Copyright (c) 2013 Michael Gmelin <freebsd@grem.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef WITH_SSL
#include <openssl/x509v3.h>
#endif

#include "fetch.h"
#include "common.h"


/*** Local data **************************************************************/

/*
 * Error messages for resolver errors
 */
static struct fetcherr netdb_errlist[] = {
#ifdef EAI_NODATA
	{ EAI_NODATA,	FETCH_RESOLV,	"Host not found" },
#endif
	{ EAI_AGAIN,	FETCH_TEMP,	"Transient resolver failure" },
	{ EAI_FAIL,	FETCH_RESOLV,	"Non-recoverable resolver failure" },
	{ EAI_NONAME,	FETCH_RESOLV,	"No address record" },
	{ -1,		FETCH_UNKNOWN,	"Unknown resolver error" }
};

/* End-of-Line */
static const char ENDL[2] = "\r\n";


/*** Error-reporting functions ***********************************************/

/*
 * Map error code to string
 */
static struct fetcherr *
fetch_finderr(struct fetcherr *p, int e)
{
	while (p->num != -1 && p->num != e)
		p++;
	return (p);
}

/*
 * Set error code
 */
void
fetch_seterr(struct fetcherr *p, int e)
{
	p = fetch_finderr(p, e);
	fetchLastErrCode = p->cat;
	snprintf(fetchLastErrString, MAXERRSTRING, "%s", p->string);
}

/*
 * Set error code according to errno
 */
void
fetch_syserr(void)
{
	switch (errno) {
	case 0:
		fetchLastErrCode = FETCH_OK;
		break;
	case EPERM:
	case EACCES:
	case EROFS:
	case EAUTH:
	case ENEEDAUTH:
		fetchLastErrCode = FETCH_AUTH;
		break;
	case ENOENT:
	case EISDIR: /* XXX */
		fetchLastErrCode = FETCH_UNAVAIL;
		break;
	case ENOMEM:
		fetchLastErrCode = FETCH_MEMORY;
		break;
	case EBUSY:
	case EAGAIN:
		fetchLastErrCode = FETCH_TEMP;
		break;
	case EEXIST:
		fetchLastErrCode = FETCH_EXISTS;
		break;
	case ENOSPC:
		fetchLastErrCode = FETCH_FULL;
		break;
	case EADDRINUSE:
	case EADDRNOTAVAIL:
	case ENETDOWN:
	case ENETUNREACH:
	case ENETRESET:
	case EHOSTUNREACH:
		fetchLastErrCode = FETCH_NETWORK;
		break;
	case ECONNABORTED:
	case ECONNRESET:
		fetchLastErrCode = FETCH_ABORT;
		break;
	case ETIMEDOUT:
		fetchLastErrCode = FETCH_TIMEOUT;
		break;
	case ECONNREFUSED:
	case EHOSTDOWN:
		fetchLastErrCode = FETCH_DOWN;
		break;
	default:
		fetchLastErrCode = FETCH_UNKNOWN;
	}
	snprintf(fetchLastErrString, MAXERRSTRING, "%s", strerror(errno));
}


/*
 * Emit status message
 */
void
fetch_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}


/*** Network-related utility functions ***************************************/

/*
 * Return the default port for a scheme
 */
int
fetch_default_port(const char *scheme)
{
	struct servent *se;

	if ((se = getservbyname(scheme, "tcp")) != NULL)
		return (ntohs(se->s_port));
	if (strcmp(scheme, SCHEME_FTP) == 0)
		return (FTP_DEFAULT_PORT);
	if (strcmp(scheme, SCHEME_HTTP) == 0)
		return (HTTP_DEFAULT_PORT);
	return (0);
}

/*
 * Return the default proxy port for a scheme
 */
int
fetch_default_proxy_port(const char *scheme)
{
	if (strcmp(scheme, SCHEME_FTP) == 0)
		return (FTP_DEFAULT_PROXY_PORT);
	if (strcmp(scheme, SCHEME_HTTP) == 0)
		return (HTTP_DEFAULT_PROXY_PORT);
	return (0);
}


/*
 * Create a connection for an existing descriptor.
 */
conn_t *
fetch_reopen(int sd)
{
	conn_t *conn;
	int opt = 1;

	/* allocate and fill connection structure */
	if ((conn = calloc(1, sizeof(*conn))) == NULL)
		return (NULL);
	fcntl(sd, F_SETFD, FD_CLOEXEC);
	setsockopt(sd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof opt);
	conn->sd = sd;
	++conn->ref;
	return (conn);
}


/*
 * Bump a connection's reference count.
 */
conn_t *
fetch_ref(conn_t *conn)
{

	++conn->ref;
	return (conn);
}


/*
 * Resolve an address
 */
struct addrinfo *
fetch_resolve(const char *addr, int port, int af)
{
	char hbuf[256], sbuf[8];
	struct addrinfo hints, *res;
	const char *hb, *he, *sep;
	const char *host, *service;
	int err, len;

	/* first, check for a bracketed IPv6 address */
	if (*addr == '[') {
		hb = addr + 1;
		if ((sep = strchr(hb, ']')) == NULL) {
			errno = EINVAL;
			goto syserr;
		}
		he = sep++;
	} else {
		hb = addr;
		sep = strchrnul(hb, ':');
		he = sep;
	}

	/* see if we need to copy the host name */
	if (*he != '\0') {
		len = snprintf(hbuf, sizeof(hbuf),
		    "%.*s", (int)(he - hb), hb);
		if (len < 0)
			goto syserr;
		if (len >= (int)sizeof(hbuf)) {
			errno = ENAMETOOLONG;
			goto syserr;
		}
		host = hbuf;
	} else {
		host = hb;
	}

	/* was it followed by a service name? */
	if (*sep == '\0' && port != 0) {
		if (port < 1 || port > 65535) {
			errno = EINVAL;
			goto syserr;
		}
		if (snprintf(sbuf, sizeof(sbuf), "%d", port) < 0)
			goto syserr;
		service = sbuf;
	} else if (*sep != '\0') {
		service = sep + 1;
	} else {
		service = NULL;
	}

	/* resolve */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = af;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_ADDRCONFIG;
	if ((err = getaddrinfo(host, service, &hints, &res)) != 0) {
		netdb_seterr(err);
		return (NULL);
	}
	return (res);
syserr:
	fetch_syserr();
	return (NULL);
}



/*
 * Bind a socket to a specific local address
 */
int
fetch_bind(int sd, int af, const char *addr)
{
	struct addrinfo *cliai, *ai;
	int err;

	if ((cliai = fetch_resolve(addr, 0, af)) == NULL)
		return (-1);
	for (ai = cliai; ai != NULL; ai = ai->ai_next)
		if ((err = bind(sd, ai->ai_addr, ai->ai_addrlen)) == 0)
			break;
	if (err != 0)
		fetch_syserr();
	freeaddrinfo(cliai);
	return (err == 0 ? 0 : -1);
}


/*
 * Establish a TCP connection to the specified port on the specified host.
 */
conn_t *
fetch_connect(const char *host, int port, int af, int verbose)
{
	struct addrinfo *cais = NULL, *sais = NULL, *cai, *sai;
	const char *bindaddr;
	conn_t *conn = NULL;
	int err = 0, sd = -1;

	DEBUGF("---> %s:%d\n", host, port);

	/* resolve server address */
	if (verbose)
		fetch_info("resolving server address: %s:%d", host, port);
	if ((sais = fetch_resolve(host, port, af)) == NULL)
		goto fail;

	/* resolve client address */
	bindaddr = getenv("FETCH_BIND_ADDRESS");
	if (bindaddr != NULL && *bindaddr != '\0') {
		if (verbose)
			fetch_info("resolving client address: %s", bindaddr);
		if ((cais = fetch_resolve(bindaddr, 0, af)) == NULL)
			goto fail;
	}

	/* try each server address in turn */
	for (err = 0, sai = sais; sai != NULL; sai = sai->ai_next) {
		/* open socket */
		if ((sd = socket(sai->ai_family, SOCK_STREAM, 0)) < 0)
			goto syserr;
		/* attempt to bind to client address */
		for (err = 0, cai = cais; cai != NULL; cai = cai->ai_next) {
			if (cai->ai_family != sai->ai_family)
				continue;
			if ((err = bind(sd, cai->ai_addr, cai->ai_addrlen)) == 0)
				break;
		}
		if (err != 0) {
			if (verbose)
				fetch_info("failed to bind to %s", bindaddr);
			goto syserr;
		}
		/* attempt to connect to server address */
		if ((err = connect(sd, sai->ai_addr, sai->ai_addrlen)) == 0)
			break;
		/* clean up before next attempt */
		close(sd);
		sd = -1;
	}
	if (err != 0) {
		if (verbose)
			fetch_info("failed to connect to %s:%d", host, port);
		goto syserr;
	}

	if ((conn = fetch_reopen(sd)) == NULL)
		goto syserr;
	if (cais != NULL)
		freeaddrinfo(cais);
	if (sais != NULL)
		freeaddrinfo(sais);
	return (conn);
syserr:
	fetch_syserr();
	goto fail;
fail:
	if (sd >= 0)
		close(sd);
	if (cais != NULL)
		freeaddrinfo(cais);
	if (sais != NULL)
		freeaddrinfo(sais);
	return (NULL);
}

#ifdef WITH_SSL
/*
 * Convert characters A-Z to lowercase (intentionally avoid any locale
 * specific conversions).
 */
static char
fetch_ssl_tolower(char in)
{
	if (in >= 'A' && in <= 'Z')
		return (in + 32);
	else
		return (in);
}

/*
 * isalpha implementation that intentionally avoids any locale specific
 * conversions.
 */
static int
fetch_ssl_isalpha(char in)
{
	return ((in >= 'A' && in <= 'Z') || (in >= 'a' && in <= 'z'));
}

/*
 * Check if passed hostnames a and b are equal.
 */
static int
fetch_ssl_hname_equal(const char *a, size_t alen, const char *b,
    size_t blen)
{
	size_t i;

	if (alen != blen)
		return (0);
	for (i = 0; i < alen; ++i) {
		if (fetch_ssl_tolower(a[i]) != fetch_ssl_tolower(b[i]))
			return (0);
	}
	return (1);
}

/*
 * Check if domain label is traditional, meaning that only A-Z, a-z, 0-9
 * and '-' (hyphen) are allowed. Hyphens have to be surrounded by alpha-
 * numeric characters. Double hyphens (like they're found in IDN a-labels
 * 'xn--') are not allowed. Empty labels are invalid.
 */
static int
fetch_ssl_is_trad_domain_label(const char *l, size_t len, int wcok)
{
	size_t i;

	if (!len || l[0] == '-' || l[len-1] == '-')
		return (0);
	for (i = 0; i < len; ++i) {
		if (!isdigit(l[i]) &&
		    !fetch_ssl_isalpha(l[i]) &&
		    !(l[i] == '*' && wcok) &&
		    !(l[i] == '-' && l[i - 1] != '-'))
			return (0);
	}
	return (1);
}

/*
 * Check if host name consists only of numbers. This might indicate an IP
 * address, which is not a good idea for CN wildcard comparison.
 */
static int
fetch_ssl_hname_is_only_numbers(const char *hostname, size_t len)
{
	size_t i;

	for (i = 0; i < len; ++i) {
		if (!((hostname[i] >= '0' && hostname[i] <= '9') ||
		    hostname[i] == '.'))
			return (0);
	}
	return (1);
}

/*
 * Check if the host name h passed matches the pattern passed in m which
 * is usually part of subjectAltName or CN of a certificate presented to
 * the client. This includes wildcard matching. The algorithm is based on
 * RFC6125, sections 6.4.3 and 7.2, which clarifies RFC2818 and RFC3280.
 */
static int
fetch_ssl_hname_match(const char *h, size_t hlen, const char *m,
    size_t mlen)
{
	int delta, hdotidx, mdot1idx, wcidx;
	const char *hdot, *mdot1, *mdot2;
	const char *wc; /* wildcard */

	if (!(h && *h && m && *m))
		return (0);
	if ((wc = strnstr(m, "*", mlen)) == NULL)
		return (fetch_ssl_hname_equal(h, hlen, m, mlen));
	wcidx = wc - m;
	/* hostname should not be just dots and numbers */
	if (fetch_ssl_hname_is_only_numbers(h, hlen))
		return (0);
	/* only one wildcard allowed in pattern */
	if (strnstr(wc + 1, "*", mlen - wcidx - 1) != NULL)
		return (0);
	/*
	 * there must be at least two more domain labels and
	 * wildcard has to be in the leftmost label (RFC6125)
	 */
	mdot1 = strnstr(m, ".", mlen);
	if (mdot1 == NULL || mdot1 < wc || (mlen - (mdot1 - m)) < 4)
		return (0);
	mdot1idx = mdot1 - m;
	mdot2 = strnstr(mdot1 + 1, ".", mlen - mdot1idx - 1);
	if (mdot2 == NULL || (mlen - (mdot2 - m)) < 2)
		return (0);
	/* hostname must contain a dot and not be the 1st char */
	hdot = strnstr(h, ".", hlen);
	if (hdot == NULL || hdot == h)
		return (0);
	hdotidx = hdot - h;
	/*
	 * host part of hostname must be at least as long as
	 * pattern it's supposed to match
	 */
	if (hdotidx < mdot1idx)
		return (0);
	/*
	 * don't allow wildcards in non-traditional domain names
	 * (IDN, A-label, U-label...)
	 */
	if (!fetch_ssl_is_trad_domain_label(h, hdotidx, 0) ||
	    !fetch_ssl_is_trad_domain_label(m, mdot1idx, 1))
		return (0);
	/* match domain part (part after first dot) */
	if (!fetch_ssl_hname_equal(hdot, hlen - hdotidx, mdot1,
	    mlen - mdot1idx))
		return (0);
	/* match part left of wildcard */
	if (!fetch_ssl_hname_equal(h, wcidx, m, wcidx))
		return (0);
	/* match part right of wildcard */
	delta = mdot1idx - wcidx - 1;
	if (!fetch_ssl_hname_equal(hdot - delta, delta,
	    mdot1 - delta, delta))
		return (0);
	/* all tests succeeded, it's a match */
	return (1);
}

/*
 * Get numeric host address info - returns NULL if host was not an IP
 * address. The caller is responsible for deallocation using
 * freeaddrinfo(3).
 */
static struct addrinfo *
fetch_ssl_get_numeric_addrinfo(const char *hostname, size_t len)
{
	struct addrinfo hints, *res;
	char *host;

	host = (char *)malloc(len + 1);
	memcpy(host, hostname, len);
	host[len] = '\0';
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_flags = AI_NUMERICHOST;
	/* port is not relevant for this purpose */
	if (getaddrinfo(host, "443", &hints, &res) != 0)
		res = NULL;
	free(host);
	return res;
}

/*
 * Compare ip address in addrinfo with address passes.
 */
static int
fetch_ssl_ipaddr_match_bin(const struct addrinfo *lhost, const char *rhost,
    size_t rhostlen)
{
	const void *left;

	if (lhost->ai_family == AF_INET && rhostlen == 4) {
		left = (void *)&((struct sockaddr_in*)(void *)
		    lhost->ai_addr)->sin_addr.s_addr;
#ifdef INET6
	} else if (lhost->ai_family == AF_INET6 && rhostlen == 16) {
		left = (void *)&((struct sockaddr_in6 *)(void *)
		    lhost->ai_addr)->sin6_addr;
#endif
	} else
		return (0);
	return (!memcmp(left, (const void *)rhost, rhostlen) ? 1 : 0);
}

/*
 * Compare ip address in addrinfo with host passed. If host is not an IP
 * address, comparison will fail.
 */
static int
fetch_ssl_ipaddr_match(const struct addrinfo *laddr, const char *r,
    size_t rlen)
{
	struct addrinfo *raddr;
	int ret;
	char *rip;

	ret = 0;
	if ((raddr = fetch_ssl_get_numeric_addrinfo(r, rlen)) == NULL)
		return 0; /* not a numeric host */

	if (laddr->ai_family == raddr->ai_family) {
		if (laddr->ai_family == AF_INET) {
			rip = (char *)&((struct sockaddr_in *)(void *)
			    raddr->ai_addr)->sin_addr.s_addr;
			ret = fetch_ssl_ipaddr_match_bin(laddr, rip, 4);
#ifdef INET6
		} else if (laddr->ai_family == AF_INET6) {
			rip = (char *)&((struct sockaddr_in6 *)(void *)
			    raddr->ai_addr)->sin6_addr;
			ret = fetch_ssl_ipaddr_match_bin(laddr, rip, 16);
#endif
		}

	}
	freeaddrinfo(raddr);
	return (ret);
}

/*
 * Verify server certificate by subjectAltName.
 */
static int
fetch_ssl_verify_altname(STACK_OF(GENERAL_NAME) *altnames,
    const char *host, struct addrinfo *ip)
{
	const GENERAL_NAME *name;
	size_t nslen;
	int i;
	const char *ns;

	for (i = 0; i < sk_GENERAL_NAME_num(altnames); ++i) {
#if OPENSSL_VERSION_NUMBER < 0x10000000L
		/*
		 * This is a workaround, since the following line causes
		 * alignment issues in clang:
		 * name = sk_GENERAL_NAME_value(altnames, i);
		 * OpenSSL explicitly warns not to use those macros
		 * directly, but there isn't much choice (and there
		 * shouldn't be any ill side effects)
		 */
		name = (GENERAL_NAME *)SKM_sk_value(void, altnames, i);
#else
		name = sk_GENERAL_NAME_value(altnames, i);
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000L
		ns = (const char *)ASN1_STRING_data(name->d.ia5);
#else
		ns = (const char *)ASN1_STRING_get0_data(name->d.ia5);
#endif
		nslen = (size_t)ASN1_STRING_length(name->d.ia5);

		if (name->type == GEN_DNS && ip == NULL &&
		    fetch_ssl_hname_match(host, strlen(host), ns, nslen))
			return (1);
		else if (name->type == GEN_IPADD && ip != NULL &&
		    fetch_ssl_ipaddr_match_bin(ip, ns, nslen))
			return (1);
	}
	return (0);
}

/*
 * Verify server certificate by CN.
 */
static int
fetch_ssl_verify_cn(X509_NAME *subject, const char *host,
    struct addrinfo *ip)
{
	ASN1_STRING *namedata;
	X509_NAME_ENTRY *nameentry;
	int cnlen, lastpos, loc, ret;
	unsigned char *cn;

	ret = 0;
	lastpos = -1;
	loc = -1;
	cn = NULL;
	/* get most specific CN (last entry in list) and compare */
	while ((lastpos = X509_NAME_get_index_by_NID(subject,
	    NID_commonName, lastpos)) != -1)
		loc = lastpos;

	if (loc > -1) {
		nameentry = X509_NAME_get_entry(subject, loc);
		namedata = X509_NAME_ENTRY_get_data(nameentry);
		cnlen = ASN1_STRING_to_UTF8(&cn, namedata);
		if (ip == NULL &&
		    fetch_ssl_hname_match(host, strlen(host), cn, cnlen))
			ret = 1;
		else if (ip != NULL && fetch_ssl_ipaddr_match(ip, cn, cnlen))
			ret = 1;
		OPENSSL_free(cn);
	}
	return (ret);
}

/*
 * Verify that server certificate subjectAltName/CN matches
 * hostname. First check, if there are alternative subject names. If yes,
 * those have to match. Only if those don't exist it falls back to
 * checking the subject's CN.
 */
static int
fetch_ssl_verify_hname(X509 *cert, const char *host)
{
	struct addrinfo *ip;
	STACK_OF(GENERAL_NAME) *altnames;
	X509_NAME *subject;
	int ret;

	ret = 0;
	ip = fetch_ssl_get_numeric_addrinfo(host, strlen(host));
	altnames = X509_get_ext_d2i(cert, NID_subject_alt_name,
	    NULL, NULL);

	if (altnames != NULL) {
		ret = fetch_ssl_verify_altname(altnames, host, ip);
	} else {
		subject = X509_get_subject_name(cert);
		if (subject != NULL)
			ret = fetch_ssl_verify_cn(subject, host, ip);
	}

	if (ip != NULL)
		freeaddrinfo(ip);
	if (altnames != NULL)
		GENERAL_NAMES_free(altnames);
	return (ret);
}

/*
 * Configure transport security layer based on environment.
 */
static void
fetch_ssl_setup_transport_layer(SSL_CTX *ctx, int verbose)
{
	long ssl_ctx_options;

	ssl_ctx_options = SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_TICKET;
	if (getenv("SSL_ALLOW_SSL3") == NULL)
		ssl_ctx_options |= SSL_OP_NO_SSLv3;
	if (getenv("SSL_NO_TLS1") != NULL)
		ssl_ctx_options |= SSL_OP_NO_TLSv1;
	if (getenv("SSL_NO_TLS1_1") != NULL)
		ssl_ctx_options |= SSL_OP_NO_TLSv1_1;
	if (getenv("SSL_NO_TLS1_2") != NULL)
		ssl_ctx_options |= SSL_OP_NO_TLSv1_2;
	if (verbose)
		fetch_info("SSL options: %lx", ssl_ctx_options);
	SSL_CTX_set_options(ctx, ssl_ctx_options);
}


/*
 * Configure peer verification based on environment.
 */
#define LOCAL_CERT_FILE	"/usr/local/etc/ssl/cert.pem"
#define BASE_CERT_FILE	"/etc/ssl/cert.pem"
static int
fetch_ssl_setup_peer_verification(SSL_CTX *ctx, int verbose)
{
	X509_LOOKUP *crl_lookup;
	X509_STORE *crl_store;
	const char *ca_cert_file, *ca_cert_path, *crl_file;

	if (getenv("SSL_NO_VERIFY_PEER") == NULL) {
		ca_cert_file = getenv("SSL_CA_CERT_FILE");
		if (ca_cert_file == NULL &&
		    access(LOCAL_CERT_FILE, R_OK) == 0)
			ca_cert_file = LOCAL_CERT_FILE;
		if (ca_cert_file == NULL &&
		    access(BASE_CERT_FILE, R_OK) == 0)
			ca_cert_file = BASE_CERT_FILE;
		ca_cert_path = getenv("SSL_CA_CERT_PATH");
		if (verbose) {
			fetch_info("Peer verification enabled");
			if (ca_cert_file != NULL)
				fetch_info("Using CA cert file: %s",
				    ca_cert_file);
			if (ca_cert_path != NULL)
				fetch_info("Using CA cert path: %s",
				    ca_cert_path);
			if (ca_cert_file == NULL && ca_cert_path == NULL)
				fetch_info("Using OpenSSL default "
				    "CA cert file and path");
		}
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER,
		    fetch_ssl_cb_verify_crt);
		if (ca_cert_file != NULL || ca_cert_path != NULL)
			SSL_CTX_load_verify_locations(ctx, ca_cert_file,
			    ca_cert_path);
		else
			SSL_CTX_set_default_verify_paths(ctx);
		if ((crl_file = getenv("SSL_CRL_FILE")) != NULL) {
			if (verbose)
				fetch_info("Using CRL file: %s", crl_file);
			crl_store = SSL_CTX_get_cert_store(ctx);
			crl_lookup = X509_STORE_add_lookup(crl_store,
			    X509_LOOKUP_file());
			if (crl_lookup == NULL ||
			    !X509_load_crl_file(crl_lookup, crl_file,
				X509_FILETYPE_PEM)) {
				fprintf(stderr,
				    "Could not load CRL file %s\n",
				    crl_file);
				return (0);
			}
			X509_STORE_set_flags(crl_store,
			    X509_V_FLAG_CRL_CHECK |
			    X509_V_FLAG_CRL_CHECK_ALL);
		}
	}
	return (1);
}

/*
 * Configure client certificate based on environment.
 */
static int
fetch_ssl_setup_client_certificate(SSL_CTX *ctx, int verbose)
{
	const char *client_cert_file, *client_key_file;

	if ((client_cert_file = getenv("SSL_CLIENT_CERT_FILE")) != NULL) {
		client_key_file = getenv("SSL_CLIENT_KEY_FILE") != NULL ?
		    getenv("SSL_CLIENT_KEY_FILE") : client_cert_file;
		if (verbose) {
			fetch_info("Using client cert file: %s",
			    client_cert_file);
			fetch_info("Using client key file: %s",
			    client_key_file);
		}
		if (SSL_CTX_use_certificate_chain_file(ctx,
			client_cert_file) != 1) {
			fprintf(stderr,
			    "Could not load client certificate %s\n",
			    client_cert_file);
			return (0);
		}
		if (SSL_CTX_use_PrivateKey_file(ctx, client_key_file,
			SSL_FILETYPE_PEM) != 1) {
			fprintf(stderr,
			    "Could not load client key %s\n",
			    client_key_file);
			return (0);
		}
	}
	return (1);
}

/*
 * Callback for SSL certificate verification, this is called on server
 * cert verification. It takes no decision, but informs the user in case
 * verification failed.
 */
int
fetch_ssl_cb_verify_crt(int verified, X509_STORE_CTX *ctx)
{
	X509 *crt;
	X509_NAME *name;
	char *str;

	str = NULL;
	if (!verified) {
		if ((crt = X509_STORE_CTX_get_current_cert(ctx)) != NULL &&
		    (name = X509_get_subject_name(crt)) != NULL)
			str = X509_NAME_oneline(name, 0, 0);
		fprintf(stderr, "Certificate verification failed for %s\n",
		    str != NULL ? str : "no relevant certificate");
		OPENSSL_free(str);
	}
	return (verified);
}

#endif

/*
 * Enable SSL on a connection.
 */
int
fetch_ssl(conn_t *conn, const struct url *URL, int verbose)
{
#ifdef WITH_SSL
	int ret, ssl_err;
	X509_NAME *name;
	char *str;

	/* Init the SSL library and context */
	if (!SSL_library_init()){
		fprintf(stderr, "SSL library init failed\n");
		return (-1);
	}

	SSL_load_error_strings();

	conn->ssl_meth = SSLv23_client_method();
	conn->ssl_ctx = SSL_CTX_new(conn->ssl_meth);
	SSL_CTX_set_mode(conn->ssl_ctx, SSL_MODE_AUTO_RETRY);

	fetch_ssl_setup_transport_layer(conn->ssl_ctx, verbose);
	if (!fetch_ssl_setup_peer_verification(conn->ssl_ctx, verbose))
		return (-1);
	if (!fetch_ssl_setup_client_certificate(conn->ssl_ctx, verbose))
		return (-1);

	conn->ssl = SSL_new(conn->ssl_ctx);
	if (conn->ssl == NULL) {
		fprintf(stderr, "SSL context creation failed\n");
		return (-1);
	}
	SSL_set_fd(conn->ssl, conn->sd);

#if OPENSSL_VERSION_NUMBER >= 0x0090806fL && !defined(OPENSSL_NO_TLSEXT)
	if (!SSL_set_tlsext_host_name(conn->ssl,
	    __DECONST(struct url *, URL)->host)) {
		fprintf(stderr,
		    "TLS server name indication extension failed for host %s\n",
		    URL->host);
		return (-1);
	}
#endif
	while ((ret = SSL_connect(conn->ssl)) == -1) {
		ssl_err = SSL_get_error(conn->ssl, ret);
		if (ssl_err != SSL_ERROR_WANT_READ &&
		    ssl_err != SSL_ERROR_WANT_WRITE) {
			ERR_print_errors_fp(stderr);
			return (-1);
		}
	}
	conn->ssl_cert = SSL_get_peer_certificate(conn->ssl);

	if (conn->ssl_cert == NULL) {
		fprintf(stderr, "No server SSL certificate\n");
		return (-1);
	}

	if (getenv("SSL_NO_VERIFY_HOSTNAME") == NULL) {
		if (verbose)
			fetch_info("Verify hostname");
		if (!fetch_ssl_verify_hname(conn->ssl_cert, URL->host)) {
			fprintf(stderr,
			    "SSL certificate subject doesn't match host %s\n",
			    URL->host);
			return (-1);
		}
	}

	if (verbose) {
		fetch_info("%s connection established using %s",
		    SSL_get_version(conn->ssl), SSL_get_cipher(conn->ssl));
		name = X509_get_subject_name(conn->ssl_cert);
		str = X509_NAME_oneline(name, 0, 0);
		fetch_info("Certificate subject: %s", str);
		OPENSSL_free(str);
		name = X509_get_issuer_name(conn->ssl_cert);
		str = X509_NAME_oneline(name, 0, 0);
		fetch_info("Certificate issuer: %s", str);
		OPENSSL_free(str);
	}

	return (0);
#else
	(void)conn;
	(void)verbose;
	fprintf(stderr, "SSL support disabled\n");
	return (-1);
#endif
}

#define FETCH_READ_WAIT		-2
#define FETCH_READ_ERROR	-1
#define FETCH_READ_DONE		 0

#ifdef WITH_SSL
static ssize_t
fetch_ssl_read(SSL *ssl, char *buf, size_t len)
{
	ssize_t rlen;
	int ssl_err;

	rlen = SSL_read(ssl, buf, len);
	if (rlen < 0) {
		ssl_err = SSL_get_error(ssl, rlen);
		if (ssl_err == SSL_ERROR_WANT_READ ||
		    ssl_err == SSL_ERROR_WANT_WRITE) {
			return (FETCH_READ_WAIT);
		} else {
			ERR_print_errors_fp(stderr);
			return (FETCH_READ_ERROR);
		}
	}
	return (rlen);
}
#endif

static ssize_t
fetch_socket_read(int sd, char *buf, size_t len)
{
	ssize_t rlen;

	rlen = read(sd, buf, len);
	if (rlen < 0) {
		if (errno == EAGAIN || (errno == EINTR && fetchRestartCalls))
			return (FETCH_READ_WAIT);
		else
			return (FETCH_READ_ERROR);
	}
	return (rlen);
}

/*
 * Read a character from a connection w/ timeout
 */
ssize_t
fetch_read(conn_t *conn, char *buf, size_t len)
{
	struct timeval now, timeout, delta;
	struct pollfd pfd;
	ssize_t rlen;
	int deltams;

	if (fetchTimeout > 0) {
		gettimeofday(&timeout, NULL);
		timeout.tv_sec += fetchTimeout;
	}

	deltams = INFTIM;
	memset(&pfd, 0, sizeof pfd);
	pfd.fd = conn->sd;
	pfd.events = POLLIN | POLLERR;

	for (;;) {
		/*
		 * The socket is non-blocking.  Instead of the canonical
		 * poll() -> read(), we do the following:
		 *
		 * 1) call read() or SSL_read().
		 * 2) if we received some data, return it.
		 * 3) if an error occurred, return -1.
		 * 4) if read() or SSL_read() signaled EOF, return.
		 * 5) if we did not receive any data but we're not at EOF,
		 *    call poll().
		 *
		 * In the SSL case, this is necessary because if we
		 * receive a close notification, we have to call
		 * SSL_read() one additional time after we've read
		 * everything we received.
		 *
		 * In the non-SSL case, it may improve performance (very
		 * slightly) when reading small amounts of data.
		 */
#ifdef WITH_SSL
		if (conn->ssl != NULL)
			rlen = fetch_ssl_read(conn->ssl, buf, len);
		else
#endif
			rlen = fetch_socket_read(conn->sd, buf, len);
		if (rlen >= 0) {
			break;
		} else if (rlen == FETCH_READ_ERROR) {
			fetch_syserr();
			return (-1);
		}
		// assert(rlen == FETCH_READ_WAIT);
		if (fetchTimeout > 0) {
			gettimeofday(&now, NULL);
			if (!timercmp(&timeout, &now, >)) {
				errno = ETIMEDOUT;
				fetch_syserr();
				return (-1);
			}
			timersub(&timeout, &now, &delta);
			deltams = delta.tv_sec * 1000 +
			    delta.tv_usec / 1000;;
		}
		errno = 0;
		pfd.revents = 0;
		if (poll(&pfd, 1, deltams) < 0) {
			if (errno == EINTR && fetchRestartCalls)
				continue;
			fetch_syserr();
			return (-1);
		}
	}
	return (rlen);
}


/*
 * Read a line of text from a connection w/ timeout
 */
#define MIN_BUF_SIZE 1024

int
fetch_getln(conn_t *conn)
{
	char *tmp;
	size_t tmpsize;
	ssize_t len;
	char c;

	if (conn->buf == NULL) {
		if ((conn->buf = malloc(MIN_BUF_SIZE)) == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		conn->bufsize = MIN_BUF_SIZE;
	}

	conn->buf[0] = '\0';
	conn->buflen = 0;

	do {
		len = fetch_read(conn, &c, 1);
		if (len == -1)
			return (-1);
		if (len == 0)
			break;
		conn->buf[conn->buflen++] = c;
		if (conn->buflen == conn->bufsize) {
			tmp = conn->buf;
			tmpsize = conn->bufsize * 2 + 1;
			if ((tmp = realloc(tmp, tmpsize)) == NULL) {
				errno = ENOMEM;
				return (-1);
			}
			conn->buf = tmp;
			conn->bufsize = tmpsize;
		}
	} while (c != '\n');

	conn->buf[conn->buflen] = '\0';
	DEBUGF("<<< %s", conn->buf);
	return (0);
}


/*
 * Write to a connection w/ timeout
 */
ssize_t
fetch_write(conn_t *conn, const char *buf, size_t len)
{
	struct iovec iov;

	iov.iov_base = __DECONST(char *, buf);
	iov.iov_len = len;
	return fetch_writev(conn, &iov, 1);
}

/*
 * Write a vector to a connection w/ timeout
 * Note: can modify the iovec.
 */
ssize_t
fetch_writev(conn_t *conn, struct iovec *iov, int iovcnt)
{
	struct timeval now, timeout, delta;
	struct pollfd pfd;
	ssize_t wlen, total;
	int deltams;

	memset(&pfd, 0, sizeof pfd);
	if (fetchTimeout) {
		pfd.fd = conn->sd;
		pfd.events = POLLOUT | POLLERR;
		gettimeofday(&timeout, NULL);
		timeout.tv_sec += fetchTimeout;
	}

	total = 0;
	while (iovcnt > 0) {
		while (fetchTimeout && pfd.revents == 0) {
			gettimeofday(&now, NULL);
			if (!timercmp(&timeout, &now, >)) {
				errno = ETIMEDOUT;
				fetch_syserr();
				return (-1);
			}
			timersub(&timeout, &now, &delta);
			deltams = delta.tv_sec * 1000 +
			    delta.tv_usec / 1000;
			errno = 0;
			pfd.revents = 0;
			if (poll(&pfd, 1, deltams) < 0) {
				/* POSIX compliance */
				if (errno == EAGAIN)
					continue;
				if (errno == EINTR && fetchRestartCalls)
					continue;
				return (-1);
			}
		}
		errno = 0;
#ifdef WITH_SSL
		if (conn->ssl != NULL)
			wlen = SSL_write(conn->ssl,
			    iov->iov_base, iov->iov_len);
		else
#endif
			wlen = writev(conn->sd, iov, iovcnt);
		if (wlen == 0) {
			/* we consider a short write a failure */
			/* XXX perhaps we shouldn't in the SSL case */
			errno = EPIPE;
			fetch_syserr();
			return (-1);
		}
		if (wlen < 0) {
			if (errno == EINTR && fetchRestartCalls)
				continue;
			return (-1);
		}
		total += wlen;
		while (iovcnt > 0 && wlen >= (ssize_t)iov->iov_len) {
			wlen -= iov->iov_len;
			iov++;
			iovcnt--;
		}
		if (iovcnt > 0) {
			iov->iov_len -= wlen;
			iov->iov_base = __DECONST(char *, iov->iov_base) + wlen;
		}
	}
	return (total);
}


/*
 * Write a line of text to a connection w/ timeout
 */
int
fetch_putln(conn_t *conn, const char *str, size_t len)
{
	struct iovec iov[2];
	int ret;

	DEBUGF(">>> %s\n", str);
	iov[0].iov_base = __DECONST(char *, str);
	iov[0].iov_len = len;
	iov[1].iov_base = __DECONST(char *, ENDL);
	iov[1].iov_len = sizeof(ENDL);
	if (len == 0)
		ret = fetch_writev(conn, &iov[1], 1);
	else
		ret = fetch_writev(conn, iov, 2);
	if (ret == -1)
		return (-1);
	return (0);
}


/*
 * Close connection
 */
int
fetch_close(conn_t *conn)
{
	int ret;

	if (--conn->ref > 0)
		return (0);
#ifdef WITH_SSL
	if (conn->ssl) {
		SSL_shutdown(conn->ssl);
		SSL_set_connect_state(conn->ssl);
		SSL_free(conn->ssl);
		conn->ssl = NULL;
	}
	if (conn->ssl_ctx) {
		SSL_CTX_free(conn->ssl_ctx);
		conn->ssl_ctx = NULL;
	}
	if (conn->ssl_cert) {
		X509_free(conn->ssl_cert);
		conn->ssl_cert = NULL;
	}
#endif
	ret = close(conn->sd);
	free(conn->buf);
	free(conn);
	return (ret);
}


/*** Directory-related utility functions *************************************/

int
fetch_add_entry(struct url_ent **p, int *size, int *len,
    const char *name, struct url_stat *us)
{
	struct url_ent *tmp;

	if (*p == NULL) {
		*size = 0;
		*len = 0;
	}

	if (*len >= *size - 1) {
		tmp = reallocarray(*p, *size * 2 + 1, sizeof(**p));
		if (tmp == NULL) {
			errno = ENOMEM;
			fetch_syserr();
			return (-1);
		}
		*size = (*size * 2 + 1);
		*p = tmp;
	}

	tmp = *p + *len;
	snprintf(tmp->name, PATH_MAX, "%s", name);
	memcpy(&tmp->stat, us, sizeof(*us));

	(*len)++;
	(++tmp)->name[0] = 0;

	return (0);
}


/*** Authentication-related utility functions ********************************/

static const char *
fetch_read_word(FILE *f)
{
	static char word[1024];

	if (fscanf(f, " %1023s ", word) != 1)
		return (NULL);
	return (word);
}

static int
fetch_netrc_open(void)
{
	struct passwd *pwd;
	char fn[PATH_MAX];
	const char *p;
	int fd, serrno;

	if ((p = getenv("NETRC")) != NULL) {
		DEBUGF("NETRC=%s\n", p);
		if (snprintf(fn, sizeof(fn), "%s", p) >= (int)sizeof(fn)) {
			fetch_info("$NETRC specifies a file name "
			    "longer than PATH_MAX");
			return (-1);
		}
	} else {
		if ((p = getenv("HOME")) == NULL) {
			if ((pwd = getpwuid(getuid())) == NULL ||
			    (p = pwd->pw_dir) == NULL)
				return (-1);
		}
		if (snprintf(fn, sizeof(fn), "%s/.netrc", p) >= (int)sizeof(fn))
			return (-1);
	}

	if ((fd = open(fn, O_RDONLY)) < 0) {
		serrno = errno;
		DEBUGF("%s: %s\n", fn, strerror(serrno));
		errno = serrno;
	}
	return (fd);
}

/*
 * Get authentication data for a URL from .netrc
 */
int
fetch_netrc_auth(struct url *url)
{
	const char *word;
	int serrno;
	FILE *f;

	if (url->netrcfd < 0)
		url->netrcfd = fetch_netrc_open();
	if (url->netrcfd < 0)
		return (-1);
	if ((f = fdopen(url->netrcfd, "r")) == NULL) {
		serrno = errno;
		DEBUGF("fdopen(netrcfd): %s", strerror(errno));
		close(url->netrcfd);
		url->netrcfd = -1;
		errno = serrno;
		return (-1);
	}
	rewind(f);
	DEBUGF("searching netrc for %s\n", url->host);
	while ((word = fetch_read_word(f)) != NULL) {
		if (strcmp(word, "default") == 0) {
			DEBUGF("using default netrc settings\n");
			break;
		}
		if (strcmp(word, "machine") == 0 &&
		    (word = fetch_read_word(f)) != NULL &&
		    strcasecmp(word, url->host) == 0) {
			DEBUGF("using netrc settings for %s\n", word);
			break;
		}
	}
	if (word == NULL)
		goto ferr;
	while ((word = fetch_read_word(f)) != NULL) {
		if (strcmp(word, "login") == 0) {
			if ((word = fetch_read_word(f)) == NULL)
				goto ferr;
			if (snprintf(url->user, sizeof(url->user),
				"%s", word) > (int)sizeof(url->user)) {
				fetch_info("login name in .netrc is too long");
				url->user[0] = '\0';
			}
		} else if (strcmp(word, "password") == 0) {
			if ((word = fetch_read_word(f)) == NULL)
				goto ferr;
			if (snprintf(url->pwd, sizeof(url->pwd),
				"%s", word) > (int)sizeof(url->pwd)) {
				fetch_info("password in .netrc is too long");
				url->pwd[0] = '\0';
			}
		} else if (strcmp(word, "account") == 0) {
			if ((word = fetch_read_word(f)) == NULL)
				goto ferr;
			/* XXX not supported! */
		} else {
			break;
		}
	}
	fclose(f);
	url->netrcfd = -1;
	return (0);
ferr:
	serrno = errno;
	fclose(f);
	url->netrcfd = -1;
	errno = serrno;
	return (-1);
}

/*
 * The no_proxy environment variable specifies a set of domains for
 * which the proxy should not be consulted; the contents is a comma-,
 * or space-separated list of domain names.  A single asterisk will
 * override all proxy variables and no transactions will be proxied
 * (for compatibility with lynx and curl, see the discussion at
 * <http://curl.haxx.se/mail/archive_pre_oct_99/0009.html>).
 */
int
fetch_no_proxy_match(const char *host)
{
	const char *no_proxy, *p, *q;
	size_t h_len, d_len;

	if ((no_proxy = getenv("NO_PROXY")) == NULL &&
	    (no_proxy = getenv("no_proxy")) == NULL)
		return (0);

	/* asterisk matches any hostname */
	if (strcmp(no_proxy, "*") == 0)
		return (1);

	h_len = strlen(host);
	p = no_proxy;
	do {
		/* position p at the beginning of a domain suffix */
		while (*p == ',' || isspace((unsigned char)*p))
			p++;

		/* position q at the first separator character */
		for (q = p; *q; ++q)
			if (*q == ',' || isspace((unsigned char)*q))
				break;

		d_len = q - p;
		if (d_len > 0 && h_len >= d_len &&
		    strncasecmp(host + h_len - d_len,
			p, d_len) == 0) {
			/* domain name matches */
			return (1);
		}

		p = q + 1;
	} while (*q);

	return (0);
}
