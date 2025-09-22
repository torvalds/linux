/*	$OpenBSD: ssl.c,v 1.37 2023/06/25 08:07:39 op Exp $	*/

/*
 * Copyright (c) 2007 - 2014 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006 Pierre-Yves Ritschard <pyr@openbsd.org>
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
#include <sys/queue.h>
#include <sys/uio.h>

#include <unistd.h>
#include <string.h>
#include <imsg.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "relayd.h"

int	ssl_password_cb(char *, int, int, void *);

int
ssl_password_cb(char *buf, int size, int rwflag, void *u)
{
	size_t	len;
	if (u == NULL) {
		bzero(buf, size);
		return (0);
	}
	if ((len = strlcpy(buf, u, size)) >= (size_t)size)
		return (0);
	return (len);
}

char *
ssl_load_key(struct relayd *env, const char *name, off_t *len, char *pass)
{
	FILE		*fp;
	EVP_PKEY	*key = NULL;
	BIO		*bio = NULL;
	long		 size;
	char		*data, *buf = NULL;

	/*
	 * Read (possibly) encrypted key from file
	 */
	if ((fp = fopen(name, "r")) == NULL)
		return (NULL);

	key = PEM_read_PrivateKey(fp, NULL, ssl_password_cb, pass);
	fclose(fp);
	if (key == NULL)
		goto fail;

	/*
	 * Write unencrypted key to memory buffer
	 */
	if ((bio = BIO_new(BIO_s_mem())) == NULL)
		goto fail;
	if (!PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL))
		goto fail;
	if ((size = BIO_get_mem_data(bio, &data)) <= 0)
		goto fail;
	if ((buf = calloc(1, size)) == NULL)
		goto fail;
	memcpy(buf, data, size);

	BIO_free_all(bio);
	EVP_PKEY_free(key);

	*len = (off_t)size;
	return (buf);

 fail:
	free(buf);
	if (bio != NULL)
		BIO_free_all(bio);
	if (key != NULL)
		EVP_PKEY_free(key);
	return (NULL);
}

uint8_t *
ssl_update_certificate(const uint8_t *oldcert, size_t oldlen, EVP_PKEY *pkey,
    EVP_PKEY *capkey, X509 *cacert, size_t *newlen)
{
	char		 name[2][TLS_NAME_SIZE];
	BIO		*in, *out = NULL;
	BUF_MEM		*bptr = NULL;
	X509		*cert = NULL;
	uint8_t		*newcert = NULL;

	if ((in = BIO_new_mem_buf(oldcert, oldlen)) == NULL) {
		log_warnx("%s: BIO_new_mem_buf failed", __func__);
		goto done;
	}

	if ((cert = PEM_read_bio_X509(in, NULL,
	    ssl_password_cb, NULL)) == NULL) {
		log_warnx("%s: PEM_read_bio_X509 failed", __func__);
		goto done;
	}

	BIO_free(in);
	in = NULL;

	name[0][0] = name[1][0] = '\0';
	if (!X509_NAME_oneline(X509_get_subject_name(cert),
	    name[0], sizeof(name[0])) ||
	    !X509_NAME_oneline(X509_get_issuer_name(cert),
	    name[1], sizeof(name[1])))
		goto done;

	if ((cert = X509_dup(cert)) == NULL)
		goto done;

	/* Update certificate key and use our CA as the issuer */
	X509_set_pubkey(cert, pkey);
	X509_set_issuer_name(cert, X509_get_subject_name(cacert));

	/* Sign with our CA */
	if (!X509_sign(cert, capkey, EVP_sha256())) {
		log_warnx("%s: X509_sign failed", __func__);
		goto done;
	}

#if DEBUG_CERT
	log_debug("%s: subject %s", __func__, name[0]);
	log_debug("%s: issuer %s", __func__, name[1]);
#if DEBUG > 2
	X509_print_fp(stdout, cert);
#endif
#endif

	/* write cert as PEM file */
	out = BIO_new(BIO_s_mem());
	if (out == NULL) {
		log_warnx("%s: BIO_new failed", __func__);
		goto done;
	}
	if (!PEM_write_bio_X509(out, cert)) {
		log_warnx("%s: PEM_write_bio_X509 failed", __func__);
		goto done;
	}
	BIO_get_mem_ptr(out, &bptr);
	if ((newcert = malloc(bptr->length)) == NULL) {
		log_warn("%s: malloc", __func__);
		goto done;
	}
	memcpy(newcert, bptr->data, bptr->length);
	*newlen = bptr->length;

done:
	if (in)
		BIO_free(in);
	if (out)
		BIO_free(out);
	if (cert)
		X509_free(cert);
	return (newcert);
}

int
ssl_load_pkey(char *buf, off_t len, X509 **x509ptr, EVP_PKEY **pkeyptr)
{
	BIO		*in;
	X509		*x509 = NULL;
	EVP_PKEY	*pkey = NULL;
	RSA		*rsa = NULL;
	char		*hash = NULL;

	if ((in = BIO_new_mem_buf(buf, len)) == NULL) {
		log_warnx("%s: BIO_new_mem_buf failed", __func__);
		return (0);
	}
	if ((x509 = PEM_read_bio_X509(in, NULL,
	    ssl_password_cb, NULL)) == NULL) {
		log_warnx("%s: PEM_read_bio_X509 failed", __func__);
		goto fail;
	}
	if ((pkey = X509_get_pubkey(x509)) == NULL) {
		log_warnx("%s: X509_get_pubkey failed", __func__);
		goto fail;
	}
	if ((rsa = EVP_PKEY_get1_RSA(pkey)) == NULL) {
		log_warnx("%s: failed to extract RSA", __func__);
		goto fail;
	}
	if ((hash = malloc(TLS_CERT_HASH_SIZE)) == NULL) {
		log_warn("%s: allocate hash failed", __func__);
		goto fail;
	}
	hash_x509(x509, hash, TLS_CERT_HASH_SIZE);
	if (RSA_set_ex_data(rsa, 0, hash) != 1) {
		log_warnx("%s: failed to set hash as exdata", __func__);
		goto fail;
	}

	RSA_free(rsa); /* dereference, will be cleaned up with pkey */
	*pkeyptr = pkey;
	if (x509ptr != NULL)
		*x509ptr = x509;
	else
		X509_free(x509);
	BIO_free(in);

	return (1);

 fail:
	free(hash);
	if (rsa != NULL)
		RSA_free(rsa);
	if (pkey != NULL)
		EVP_PKEY_free(pkey);
	if (x509 != NULL)
		X509_free(x509);
	BIO_free(in);

	return (0);
}
