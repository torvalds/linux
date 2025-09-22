/*	$OpenBSD: ssl.h,v 1.27 2023/06/25 08:08:03 op Exp $	*/
/*
 * Copyright (c) 2013 Gilles Chehade <gilles@poolp.org>
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

struct pki {
	char			 pki_name[HOST_NAME_MAX+1];

	char			*pki_cert_file;
	char			*pki_cert;
	off_t			 pki_cert_len;

	char			*pki_key_file;
	char			*pki_key;
	off_t			 pki_key_len;

	int			 pki_dhe;
};

struct ca {
	char			 ca_name[HOST_NAME_MAX+1];

	char			*ca_cert_file;
	char			*ca_cert;
	off_t			 ca_cert_len;
};


/* ssl.c */
void ssl_error(const char *);
int ssl_load_certificate(struct pki *, const char *);
int ssl_load_keyfile(struct pki *, const char *, const char *);
int ssl_load_cafile(struct ca *, const char *);
char *ssl_pubkey_hash(const char *, off_t);
