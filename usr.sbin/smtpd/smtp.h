/*	$OpenBSD: smtp.h,v 1.5 2024/06/02 23:26:39 jsg Exp $	*/

/*
 * Copyright (c) 2018 Eric Faurot <eric@openbsd.org>
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

#define TLS_NO		0
#define TLS_YES		1
#define TLS_FORCE	2
#define TLS_SMTPS	3

#define CERT_OK		0
#define CERT_UNKNOWN	1
#define CERT_INVALID	2

#define FAIL_INTERNAL	1	/* malloc, etc. (check errno)  */
#define FAIL_CONN	2	/* disconnect, timeout... (check errno) */
#define FAIL_PROTO	3	/* protocol violation */
#define FAIL_IMPL	4	/* server lacks a required feature */
#define FAIL_RESP	5	/* rejected command */

struct smtp_params {

	/* Client options */
	size_t			 linemax;	/* max input line size */
	size_t			 ibufmax;	/* max input buffer size */
	size_t			 obufmax;	/* max output buffer size */

	/* Connection settings */
	const struct sockaddr	*dst;		/* address to connect to */
	const struct sockaddr	*src;		/* address to bind to */
	int			 timeout;	/* timeout in seconds */

	/* TLS options */
	int			 tls_req;	/* requested TLS mode */
	int			 tls_verify;	/* need valid server certificate */
	const char 		*tls_servname;	/* SNI */

	/* SMTP options */
	int			 lmtp;		/* use LMTP protocol */
	const char		*helo;		/* string to use with HELO */
	const char		*auth_user;	/* for AUTH */
	const char		*auth_pass;	/* for AUTH */
};

struct smtp_rcpt {
	const char	*to;
	const char	*dsn_notify;
	const char	*dsn_orcpt;
	int		 done;
};

struct smtp_mail {
	const char		*from;
	const char		*dsn_ret;
	const char		*dsn_envid;
	struct smtp_rcpt	*rcpt;
	int			 rcptcount;
	FILE			*fp;
};

struct smtp_status {
	struct smtp_rcpt	*rcpt;
	const char		*cmd;
	const char		*status;
};

struct smtp_client;

/* smtp_client.c */
struct smtp_client *smtp_connect(const struct smtp_params *, void *);
void smtp_cert_verified(struct smtp_client *, int);
void smtp_set_tls(struct smtp_client *, void *);
void smtp_quit(struct smtp_client *);
void smtp_sendmail(struct smtp_client *, struct smtp_mail *);

/* callbacks */
void smtp_require_tls(void *, struct smtp_client *);
void smtp_ready(void *, struct smtp_client *);
void smtp_failed(void *, struct smtp_client *, int, const char *);
void smtp_closed(void *, struct smtp_client *);
void smtp_status(void *, struct smtp_client *, struct smtp_status *);
void smtp_done(void *, struct smtp_client *, struct smtp_mail *);
