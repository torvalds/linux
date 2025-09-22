/* $OpenBSD: tls_client.c,v 1.51 2024/03/26 08:54:48 joshua Exp $ */
/*
 * Copyright (c) 2014 Joel Sing <jsing@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <limits.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/x509.h>

#include <tls.h>
#include "tls_internal.h"

struct tls *
tls_client(void)
{
	struct tls *ctx;

	if (tls_init() == -1)
		return (NULL);

	if ((ctx = tls_new()) == NULL)
		return (NULL);

	ctx->flags |= TLS_CLIENT;

	return (ctx);
}

int
tls_connect(struct tls *ctx, const char *host, const char *port)
{
	return tls_connect_servername(ctx, host, port, NULL);
}

int
tls_connect_servername(struct tls *ctx, const char *host, const char *port,
    const char *servername)
{
	struct addrinfo hints, *res, *res0;
	const char *h = NULL, *p = NULL;
	char *hs = NULL, *ps = NULL;
	int rv = -1, s = -1, ret;

	if ((ctx->flags & TLS_CLIENT) == 0) {
		tls_set_errorx(ctx, TLS_ERROR_INVALID_CONTEXT,
		    "not a client context");
		goto err;
	}

	if (host == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "host not specified");
		goto err;
	}

	/* If port is NULL, try to extract a port from the specified host. */
	if (port == NULL) {
		ret = tls_host_port(host, &hs, &ps);
		if (ret == -1) {
			tls_set_errorx(ctx, TLS_ERROR_OUT_OF_MEMORY, "out of memory");
			goto err;
		}
		if (ret != 0) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "no port provided");
			goto err;
		}
	}

	h = (hs != NULL) ? hs : host;
	p = (ps != NULL) ? ps : port;

	/*
	 * First check if the host is specified as a numeric IP address,
	 * either IPv4 or IPv6, before trying to resolve the host.
	 * The AI_ADDRCONFIG resolver option will not return IPv4 or IPv6
	 * records if it is not configured on an interface;  not considering
	 * loopback addresses.  Checking the numeric addresses first makes
	 * sure that connection attempts to numeric addresses and especially
	 * 127.0.0.1 or ::1 loopback addresses are always possible.
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;

	/* try as an IPv4 literal */
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(h, p, &hints, &res0) != 0) {
		/* try again as an IPv6 literal */
		hints.ai_family = AF_INET6;
		if (getaddrinfo(h, p, &hints, &res0) != 0) {
			/* last try, with name resolution and save the error */
			hints.ai_family = AF_UNSPEC;
			hints.ai_flags = AI_ADDRCONFIG;
			if ((s = getaddrinfo(h, p, &hints, &res0)) != 0) {
				tls_set_error(ctx, TLS_ERROR_UNKNOWN,
				    "%s", gai_strerror(s));
				goto err;
			}
		}
	}

	/* It was resolved somehow; now try connecting to what we got */
	s = -1;
	for (res = res0; res; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s == -1) {
			tls_set_error(ctx, TLS_ERROR_UNKNOWN,
			    "socket");
			continue;
		}
		if (connect(s, res->ai_addr, res->ai_addrlen) == -1) {
			tls_set_error(ctx, TLS_ERROR_UNKNOWN,
			    "connect");
			close(s);
			s = -1;
			continue;
		}

		break;  /* Connected. */
	}
	freeaddrinfo(res0);

	if (s == -1)
		goto err;

	if (servername == NULL)
		servername = h;

	if (tls_connect_socket(ctx, s, servername) != 0) {
		close(s);
		goto err;
	}

	ctx->socket = s;

	rv = 0;

 err:
	free(hs);
	free(ps);

	return (rv);
}

static int
tls_client_read_session(struct tls *ctx)
{
	int sfd = ctx->config->session_fd;
	uint8_t *session = NULL;
	size_t session_len = 0;
	SSL_SESSION *ss = NULL;
	BIO *bio = NULL;
	struct stat sb;
	ssize_t n;
	int rv = -1;

	if (fstat(sfd, &sb) == -1) {
		tls_set_error(ctx, TLS_ERROR_UNKNOWN,
		    "failed to stat session file");
		goto err;
	}
	if (sb.st_size < 0 || sb.st_size > INT_MAX) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "invalid session file size");
		goto err;
	}
	session_len = (size_t)sb.st_size;

	/* A zero size file means that we do not yet have a valid session. */
	if (session_len == 0)
		goto done;

	if ((session = malloc(session_len)) == NULL)
		goto err;

	n = pread(sfd, session, session_len, 0);
	if (n < 0 || (size_t)n != session_len) {
		tls_set_error(ctx, TLS_ERROR_UNKNOWN,
		    "failed to read session file");
		goto err;
	}
	if ((bio = BIO_new_mem_buf(session, session_len)) == NULL)
		goto err;
	if ((ss = PEM_read_bio_SSL_SESSION(bio, NULL, tls_password_cb,
	    NULL)) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "failed to parse session");
		goto err;
	}

	if (SSL_set_session(ctx->ssl_conn, ss) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "failed to set session");
		goto err;
	}

 done:
	rv = 0;

 err:
	freezero(session, session_len);
	SSL_SESSION_free(ss);
	BIO_free(bio);

	return rv;
}

static int
tls_client_write_session(struct tls *ctx)
{
	int sfd = ctx->config->session_fd;
	SSL_SESSION *ss = NULL;
	BIO *bio = NULL;
	long data_len;
	char *data;
	off_t offset;
	size_t len;
	ssize_t n;
	int rv = -1;

	if ((ss = SSL_get1_session(ctx->ssl_conn)) == NULL) {
		if (ftruncate(sfd, 0) == -1) {
			tls_set_error(ctx, TLS_ERROR_UNKNOWN,
			    "failed to truncate session file");
			goto err;
		}
		goto done;
	}

	if ((bio = BIO_new(BIO_s_mem())) == NULL)
		goto err;
	if (PEM_write_bio_SSL_SESSION(bio, ss) == 0)
		goto err;
	if ((data_len = BIO_get_mem_data(bio, &data)) <= 0)
		goto err;

	len = (size_t)data_len;
	offset = 0;

	if (ftruncate(sfd, len) == -1) {
		tls_set_error(ctx, TLS_ERROR_UNKNOWN,
		    "failed to truncate session file");
		goto err;
	}
	while (len > 0) {
		if ((n = pwrite(sfd, data + offset, len, offset)) == -1) {
			tls_set_error(ctx, TLS_ERROR_UNKNOWN,
			    "failed to write session file");
			goto err;
		}
		offset += n;
		len -= n;
	}

 done:
	rv = 0;

 err:
	SSL_SESSION_free(ss);
	BIO_free_all(bio);

	return (rv);
}

static int
tls_connect_common(struct tls *ctx, const char *servername)
{
	union tls_addr addrbuf;
	size_t servername_len;
	int rv = -1;

	if ((ctx->flags & TLS_CLIENT) == 0) {
		tls_set_errorx(ctx, TLS_ERROR_INVALID_CONTEXT,
		    "not a client context");
		goto err;
	}

	if (servername != NULL) {
		if ((ctx->servername = strdup(servername)) == NULL) {
			tls_set_errorx(ctx, TLS_ERROR_OUT_OF_MEMORY,
			    "out of memory");
			goto err;
		}

		/*
		 * If there's a trailing dot, remove it. While an FQDN includes
		 * the terminating dot representing the zero-length label of
		 * the root (RFC 8499, section 2), the SNI explicitly does not
		 * include it (RFC 6066, section 3).
		 */
		servername_len = strlen(ctx->servername);
		if (servername_len > 0 &&
		    ctx->servername[servername_len - 1] == '.')
			ctx->servername[servername_len - 1] = '\0';
	}

	if ((ctx->ssl_ctx = SSL_CTX_new(SSLv23_client_method())) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "ssl context failure");
		goto err;
	}

	if (tls_configure_ssl(ctx, ctx->ssl_ctx) != 0)
		goto err;

	if (tls_configure_ssl_keypair(ctx, ctx->ssl_ctx,
	    ctx->config->keypair, 0) != 0)
		goto err;

	if (ctx->config->verify_name) {
		if (ctx->servername == NULL) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "server name not specified");
			goto err;
		}
	}

	if (tls_configure_ssl_verify(ctx, ctx->ssl_ctx, SSL_VERIFY_PEER) == -1)
		goto err;

	if (ctx->config->ecdhecurves != NULL) {
		if (SSL_CTX_set1_groups(ctx->ssl_ctx, ctx->config->ecdhecurves,
		    ctx->config->ecdhecurves_len) != 1) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "failed to set ecdhe curves");
			goto err;
		}
	}

	if (SSL_CTX_set_tlsext_status_cb(ctx->ssl_ctx, tls_ocsp_verify_cb) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ssl OCSP verification setup failure");
		goto err;
	}

	if ((ctx->ssl_conn = SSL_new(ctx->ssl_ctx)) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "ssl connection failure");
		goto err;
	}

	if (SSL_set_app_data(ctx->ssl_conn, ctx) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ssl application data failure");
		goto err;
	}

	if (ctx->config->session_fd != -1) {
		SSL_clear_options(ctx->ssl_conn, SSL_OP_NO_TICKET);
		if (tls_client_read_session(ctx) == -1)
			goto err;
	}

	if (SSL_set_tlsext_status_type(ctx->ssl_conn, TLSEXT_STATUSTYPE_ocsp) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ssl OCSP extension setup failure");
		goto err;
	}

	/*
	 * RFC 6066 (SNI): Literal IPv4 and IPv6 addresses are not
	 * permitted in "HostName".
	 */
	if (ctx->servername != NULL &&
	    inet_pton(AF_INET, ctx->servername, &addrbuf) != 1 &&
	    inet_pton(AF_INET6, ctx->servername, &addrbuf) != 1) {
		if (SSL_set_tlsext_host_name(ctx->ssl_conn,
		    ctx->servername) == 0) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "server name indication failure");
			goto err;
		}
	}

	ctx->state |= TLS_CONNECTED;
	rv = 0;

 err:
	return (rv);
}

int
tls_connect_socket(struct tls *ctx, int s, const char *servername)
{
	return tls_connect_fds(ctx, s, s, servername);
}

int
tls_connect_fds(struct tls *ctx, int fd_read, int fd_write,
    const char *servername)
{
	int rv = -1;

	if (fd_read < 0 || fd_write < 0) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "invalid file descriptors");
		goto err;
	}

	if (tls_connect_common(ctx, servername) != 0)
		goto err;

	if (SSL_set_rfd(ctx->ssl_conn, fd_read) != 1 ||
	    SSL_set_wfd(ctx->ssl_conn, fd_write) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ssl file descriptor failure");
		goto err;
	}

	rv = 0;
 err:
	return (rv);
}

int
tls_connect_cbs(struct tls *ctx, tls_read_cb read_cb,
    tls_write_cb write_cb, void *cb_arg, const char *servername)
{
	int rv = -1;

	if (tls_connect_common(ctx, servername) != 0)
		goto err;

	if (tls_set_cbs(ctx, read_cb, write_cb, cb_arg) != 0)
		goto err;

	rv = 0;

 err:
	return (rv);
}

int
tls_handshake_client(struct tls *ctx)
{
	X509 *cert = NULL;
	int match, ssl_ret;
	int rv = -1;

	if ((ctx->flags & TLS_CLIENT) == 0) {
		tls_set_errorx(ctx, TLS_ERROR_INVALID_CONTEXT,
		    "not a client context");
		goto err;
	}

	if ((ctx->state & TLS_CONNECTED) == 0) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "context not connected");
		goto err;
	}

	ctx->state |= TLS_SSL_NEEDS_SHUTDOWN;

	ERR_clear_error();
	if ((ssl_ret = SSL_connect(ctx->ssl_conn)) != 1) {
		rv = tls_ssl_error(ctx, ctx->ssl_conn, ssl_ret, "handshake");
		goto err;
	}

	if (ctx->config->verify_name) {
		cert = SSL_get_peer_certificate(ctx->ssl_conn);
		if (cert == NULL) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "no server certificate");
			goto err;
		}
		if (tls_check_name(ctx, cert, ctx->servername, &match) == -1)
			goto err;
		if (!match) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "name `%s' not present in server certificate",
			    ctx->servername);
			goto err;
		}
	}

	ctx->state |= TLS_HANDSHAKE_COMPLETE;

	if (ctx->config->session_fd != -1) {
		if (tls_client_write_session(ctx) == -1)
			goto err;
	}

	rv = 0;

 err:
	X509_free(cert);

	return (rv);
}
