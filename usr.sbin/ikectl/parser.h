/*	$OpenBSD: parser.h,v 1.18 2022/09/19 20:54:02 tobhe Exp $	*/

/*
 * Copyright (c) 2007-2013 Reyk Floeter <reyk@openbsd.org>
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

#ifndef IKECTL_PARSER_H
#define IKECTL_PARSER_H

enum actions {
	NONE,
	LOAD,
	RELOAD,
	MONITOR,
	LOG_VERBOSE,
	LOG_BRIEF,
	COUPLE,
	DECOUPLE,
	ACTIVE,
	PASSIVE,
	RESETALL,
	RESETCA,
	RESETPOLICY,
	RESETSA,
	RESETUSER,
	CA,
	CA_CREATE,
	CA_DELETE,
	CA_INSTALL,
	CA_EXPORT,
	CA_CERTIFICATE,
	CA_CERT_CREATE,
	CA_SERVER,
	CA_CLIENT,
	CA_OCSP,
	CA_CERT_DELETE,
	CA_CERT_INSTALL,
	CA_CERT_EXPORT,
	CA_CERT_REVOKE,
	CA_KEY_CREATE,
	CA_KEY_DELETE,
	CA_KEY_INSTALL,
	CA_KEY_IMPORT,
	SHOW_CA,
	SHOW_CA_CERTIFICATES,
	SHOW_SA,
	RESET_ID,
	SHOW_CERTSTORE,
	SHOW_STATS
};

struct parse_result {
	enum actions	 action;
	struct imsgbuf	*ibuf;
	char		*path;
	char		*caname;
	char		*pass;
	char		*host;
	char		*peer;
	char		*id;
	int		 htype;
	int		 quiet;
};

#define HOST_IPADDR	1
#define HOST_FQDN	2

struct parse_result	*parse(int, char *[]);

struct ca	*ca_setup(char *, int, int, char *);
int		 ca_create(struct ca *);
int		 ca_certificate(struct ca *, char *, int, int);
int		 ca_export(struct ca *, char *, char *, char *);
int		 ca_revoke(struct ca *, char *);
int		 ca_delete(struct ca *);
int		 ca_delkey(struct ca *, char *);
int		 ca_install(struct ca *, char *);
int		 ca_cert_install(struct ca *, char *, char *);
int		 ca_show_certs(struct ca *, char *);
int		 ca_key_create(struct ca *, char *);
int		 ca_key_delete(struct ca *, char *);
int		 ca_key_install(struct ca *, char *, char *);
int		 ca_key_import(struct ca *, char *, char *);

#endif /* IKECTL_PARSER_H */
