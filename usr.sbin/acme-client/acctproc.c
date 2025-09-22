/*	$Id: acctproc.c,v 1.32 2023/08/29 14:44:53 op Exp $ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/err.h>

#include "extern.h"
#include "key.h"

/*
 * Converts a BIGNUM to the form used in JWK.
 * This is essentially a base64-encoded big-endian binary string
 * representation of the number.
 */
static char *
bn2string(const BIGNUM *bn)
{
	int	 len;
	char	*buf, *bbuf;

	/* Extract big-endian representation of BIGNUM. */

	len = BN_num_bytes(bn);
	if ((buf = malloc(len)) == NULL) {
		warn("malloc");
		return NULL;
	} else if (len != BN_bn2bin(bn, (unsigned char *)buf)) {
		warnx("BN_bn2bin");
		free(buf);
		return NULL;
	}

	/* Convert to base64url. */

	if ((bbuf = base64buf_url(buf, len)) == NULL) {
		warnx("base64buf_url");
		free(buf);
		return NULL;
	}

	free(buf);
	return bbuf;
}

/*
 * Extract the relevant RSA components from the key and create the JSON
 * thumbprint from them.
 */
static char *
op_thumb_rsa(EVP_PKEY *pkey)
{
	char	*exp = NULL, *mod = NULL, *json = NULL;
	RSA	*r;

	if ((r = EVP_PKEY_get0_RSA(pkey)) == NULL)
		warnx("EVP_PKEY_get0_RSA");
	else if ((mod = bn2string(RSA_get0_n(r))) == NULL)
		warnx("bn2string");
	else if ((exp = bn2string(RSA_get0_e(r))) == NULL)
		warnx("bn2string");
	else if ((json = json_fmt_thumb_rsa(exp, mod)) == NULL)
		warnx("json_fmt_thumb_rsa");

	free(exp);
	free(mod);
	return json;
}

/*
 * Extract the relevant EC components from the key and create the JSON
 * thumbprint from them.
 */
static char *
op_thumb_ec(EVP_PKEY *pkey)
{
	BIGNUM	*X = NULL, *Y = NULL;
	EC_KEY	*ec = NULL;
	char	*x = NULL, *y = NULL;
	char	*json = NULL;

	if ((ec = EVP_PKEY_get0_EC_KEY(pkey)) == NULL)
		warnx("EVP_PKEY_get0_EC_KEY");
	else if ((X = BN_new()) == NULL)
		warnx("BN_new");
	else if ((Y = BN_new()) == NULL)
		warnx("BN_new");
	else if (!EC_POINT_get_affine_coordinates(EC_KEY_get0_group(ec),
	    EC_KEY_get0_public_key(ec), X, Y, NULL))
		warnx("EC_POINT_get_affine_coordinates");
	else if ((x = bn2string(X)) == NULL)
		warnx("bn2string");
	else if ((y = bn2string(Y)) == NULL)
		warnx("bn2string");
	else if ((json = json_fmt_thumb_ec(x, y)) == NULL)
		warnx("json_fmt_thumb_ec");

	BN_free(X);
	BN_free(Y);
	free(x);
	free(y);
	return json;
}

/*
 * The thumbprint operation is used for the challenge sequence.
 */
static int
op_thumbprint(int fd, EVP_PKEY *pkey)
{
	char		*thumb = NULL, *dig64 = NULL;
	unsigned char	 dig[EVP_MAX_MD_SIZE];
	unsigned int	 digsz;
	int		 rc = 0;

	/* Construct the thumbprint input itself. */

	switch (EVP_PKEY_base_id(pkey)) {
	case EVP_PKEY_RSA:
		if ((thumb = op_thumb_rsa(pkey)) != NULL)
			break;
		goto out;
	case EVP_PKEY_EC:
		if ((thumb = op_thumb_ec(pkey)) != NULL)
			break;
		goto out;
	default:
		warnx("EVP_PKEY_base_id: unknown key type");
		goto out;
	}

	/*
	 * Compute the SHA256 digest of the thumbprint then
	 * base64-encode the digest itself.
	 * If the reader is closed when we write, ignore it (we'll pick
	 * it up in the read loop).
	 */

	if (!EVP_Digest(thumb, strlen(thumb), dig, &digsz, EVP_sha256(),
	    NULL)) {
		warnx("EVP_Digest");
		goto out;
	}
	if ((dig64 = base64buf_url((char *)dig, digsz)) == NULL) {
		warnx("base64buf_url");
		goto out;
	}
	if (writestr(fd, COMM_THUMB, dig64) < 0)
		goto out;

	rc = 1;
out:
	free(thumb);
	free(dig64);
	return rc;
}

static int
op_sign_rsa(char **prot, EVP_PKEY *pkey, const char *nonce, const char *url)
{
	char	*exp = NULL, *mod = NULL;
	int	rc = 0;
	RSA	*r;

	*prot = NULL;

	/*
	 * First, extract relevant portions of our private key.
	 * Finally, format the header combined with the nonce.
	 */

	if ((r = EVP_PKEY_get0_RSA(pkey)) == NULL)
		warnx("EVP_PKEY_get0_RSA");
	else if ((mod = bn2string(RSA_get0_n(r))) == NULL)
		warnx("bn2string");
	else if ((exp = bn2string(RSA_get0_e(r))) == NULL)
		warnx("bn2string");
	else if ((*prot = json_fmt_protected_rsa(exp, mod, nonce, url)) == NULL)
		warnx("json_fmt_protected_rsa");
	else
		rc = 1;

	free(exp);
	free(mod);
	return rc;
}

static int
op_sign_ec(char **prot, EVP_PKEY *pkey, const char *nonce, const char *url)
{
	BIGNUM	*X = NULL, *Y = NULL;
	EC_KEY	*ec = NULL;
	char	*x = NULL, *y = NULL;
	int	rc = 0;

	*prot = NULL;

	if ((ec = EVP_PKEY_get0_EC_KEY(pkey)) == NULL)
		warnx("EVP_PKEY_get0_EC_KEY");
	else if ((X = BN_new()) == NULL)
		warnx("BN_new");
	else if ((Y = BN_new()) == NULL)
		warnx("BN_new");
	else if (!EC_POINT_get_affine_coordinates(EC_KEY_get0_group(ec),
	    EC_KEY_get0_public_key(ec), X, Y, NULL))
		warnx("EC_POINT_get_affine_coordinates");
	else if ((x = bn2string(X)) == NULL)
		warnx("bn2string");
	else if ((y = bn2string(Y)) == NULL)
		warnx("bn2string");
	else if ((*prot = json_fmt_protected_ec(x, y, nonce, url)) == NULL)
		warnx("json_fmt_protected_ec");
	else
		rc = 1;

	BN_free(X);
	BN_free(Y);
	free(x);
	free(y);
	return rc;
}

/*
 * Operation to sign a message with the account key.
 * This requires the sender ("fd") to provide the payload and a nonce.
 */
static int
op_sign(int fd, EVP_PKEY *pkey, enum acctop op)
{
	EVP_MD_CTX		*ctx = NULL;
	const EVP_MD		*evp_md = NULL;
	ECDSA_SIG		*ec_sig = NULL;
	const BIGNUM		*ec_sig_r = NULL, *ec_sig_s = NULL;
	int			 bn_len, sign_len, rc = 0;
	char			*nonce = NULL, *pay = NULL, *pay64 = NULL;
	char			*prot = NULL, *prot64 = NULL;
	char			*sign = NULL, *dig64 = NULL, *fin = NULL;
	char			*url = NULL, *kid = NULL, *alg = NULL;
	const unsigned char	*digp;
	unsigned char		*dig = NULL, *buf = NULL;
	size_t			 digsz;

	/* Read our payload and nonce from the requestor. */

	if ((pay = readstr(fd, COMM_PAY)) == NULL)
		goto out;
	else if ((nonce = readstr(fd, COMM_NONCE)) == NULL)
		goto out;
	else if ((url = readstr(fd, COMM_URL)) == NULL)
		goto out;

	if (op == ACCT_KID_SIGN)
		if ((kid = readstr(fd, COMM_KID)) == NULL)
			goto out;

	/* Base64-encode the payload. */

	if ((pay64 = base64buf_url(pay, strlen(pay))) == NULL) {
		warnx("base64buf_url");
		goto out;
	}

	switch (EVP_PKEY_base_id(pkey)) {
	case EVP_PKEY_RSA:
		alg = "RS256";
		evp_md = EVP_sha256();
		break;
	case EVP_PKEY_EC:
		alg = "ES384";
		evp_md = EVP_sha384();
		break;
	default:
		warnx("unknown account key type");
		goto out;
	}

	if (op == ACCT_KID_SIGN) {
		if ((prot = json_fmt_protected_kid(alg, kid, nonce, url)) ==
		    NULL) {
			warnx("json_fmt_protected_kid");
			goto out;
		}
	} else {
		switch (EVP_PKEY_base_id(pkey)) {
		case EVP_PKEY_RSA:
			if (!op_sign_rsa(&prot, pkey, nonce, url))
				goto out;
			break;
		case EVP_PKEY_EC:
			if (!op_sign_ec(&prot, pkey, nonce, url))
				goto out;
			break;
		default:
			warnx("EVP_PKEY_base_id");
			goto out;
		}
	}

	/* The header combined with the nonce, base64. */

	if ((prot64 = base64buf_url(prot, strlen(prot))) == NULL) {
		warnx("base64buf_url");
		goto out;
	}

	/* Now the signature material. */

	sign_len = asprintf(&sign, "%s.%s", prot64, pay64);
	if (sign_len == -1) {
		warn("asprintf");
		sign = NULL;
		goto out;
	}

	/* Sign the message. */

	if ((ctx = EVP_MD_CTX_new()) == NULL) {
		warnx("EVP_MD_CTX_new");
		goto out;
	}
	if (!EVP_DigestSignInit(ctx, NULL, evp_md, NULL, pkey)) {
		warnx("EVP_DigestSignInit");
		goto out;
	}
	if (!EVP_DigestSign(ctx, NULL, &digsz, sign, sign_len)) {
		warnx("EVP_DigestSign");
		goto out;
	}
	if ((dig = malloc(digsz)) == NULL) {
		warn("malloc");
		goto out;
	}
	if (!EVP_DigestSign(ctx, dig, &digsz, sign, sign_len)) {
		warnx("EVP_DigestSign");
		goto out;
	}

	switch (EVP_PKEY_base_id(pkey)) {
	case EVP_PKEY_RSA:
		if ((dig64 = base64buf_url((char *)dig, digsz)) == NULL) {
			warnx("base64buf_url");
			goto out;
		}
		break;
	case EVP_PKEY_EC:
		if (digsz > LONG_MAX) {
			warnx("EC signature too long");
			goto out;
		}

		digp = dig;
		if ((ec_sig = d2i_ECDSA_SIG(NULL, &digp, digsz)) == NULL) {
			warnx("d2i_ECDSA_SIG");
			goto out;
		}

		if ((ec_sig_r = ECDSA_SIG_get0_r(ec_sig)) == NULL ||
		    (ec_sig_s = ECDSA_SIG_get0_s(ec_sig)) == NULL) {
			warnx("ECDSA_SIG_get0");
			goto out;
		}

		if ((bn_len = (EVP_PKEY_bits(pkey) + 7) / 8) <= 0) {
			warnx("EVP_PKEY_bits");
			goto out;
		}

		if ((buf = calloc(2, bn_len)) == NULL) {
			warnx("calloc");
			goto out;
		}

		if (BN_bn2binpad(ec_sig_r, buf, bn_len) != bn_len ||
		    BN_bn2binpad(ec_sig_s, buf + bn_len, bn_len) != bn_len) {
			warnx("BN_bn2binpad");
			goto out;
		}

		if ((dig64 = base64buf_url((char *)buf, 2 * bn_len)) == NULL) {
			warnx("base64buf_url");
			goto out;
		}

		break;
	default:
		warnx("EVP_PKEY_base_id");
		goto out;
	}

	/*
	 * Write back in the correct JSON format.
	 * If the reader is closed, just ignore it (we'll pick it up
	 * when we next enter the read loop).
	 */

	if ((fin = json_fmt_signed(prot64, pay64, dig64)) == NULL) {
		warnx("json_fmt_signed");
		goto out;
	} else if (writestr(fd, COMM_REQ, fin) < 0)
		goto out;

	rc = 1;
out:
	ECDSA_SIG_free(ec_sig);
	EVP_MD_CTX_free(ctx);
	free(pay);
	free(sign);
	free(pay64);
	free(url);
	free(nonce);
	free(kid);
	free(prot);
	free(prot64);
	free(dig);
	free(dig64);
	free(fin);
	free(buf);
	return rc;
}

int
acctproc(int netsock, const char *acctkey, enum keytype keytype)
{
	FILE		*f = NULL;
	EVP_PKEY	*pkey = NULL;
	long		 lval;
	enum acctop	 op;
	int		 rc = 0, cc, newacct = 0;
	mode_t		 prev;

	/*
	 * First, open our private key file read-only or write-only if
	 * we're creating from scratch.
	 * Set our umask to be maximally restrictive.
	 */

	prev = umask((S_IWUSR | S_IXUSR) | S_IRWXG | S_IRWXO);
	if ((f = fopen(acctkey, "r")) == NULL && errno == ENOENT) {
		f = fopen(acctkey, "wx");
		newacct = 1;
	}
	umask(prev);

	if (f == NULL) {
		warn("%s", acctkey);
		goto out;
	}

	/* File-system, user, and sandbox jailing. */

	ERR_load_crypto_strings();

	if (pledge("stdio", NULL) == -1) {
		warn("pledge");
		goto out;
	}

	if (newacct) {
		switch (keytype) {
		case KT_ECDSA:
			if ((pkey = ec_key_create(f, acctkey)) == NULL)
				goto out;
			dodbg("%s: generated ECDSA account key", acctkey);
			break;
		case KT_RSA:
			if ((pkey = rsa_key_create(f, acctkey)) == NULL)
				goto out;
			dodbg("%s: generated RSA account key", acctkey);
			break;
		}
	} else {
		if ((pkey = key_load(f, acctkey)) == NULL)
			goto out;
		/* XXX check if account key type equals configured key type */
		doddbg("%s: loaded account key", acctkey);
	}

	fclose(f);
	f = NULL;

	/* Notify the netproc that we've started up. */

	if ((cc = writeop(netsock, COMM_ACCT_STAT, ACCT_READY)) == 0)
		rc = 1;
	if (cc <= 0)
		goto out;

	/*
	 * Now we wait for requests from the network-facing process.
	 * It might ask us for our thumbprint, for example, or for us to
	 * sign a message.
	 */

	for (;;) {
		op = ACCT__MAX;
		if ((lval = readop(netsock, COMM_ACCT)) == 0)
			op = ACCT_STOP;
		else if (lval == ACCT_SIGN || lval == ACCT_KID_SIGN ||
		    lval == ACCT_THUMBPRINT)
			op = lval;

		if (ACCT__MAX == op) {
			warnx("unknown operation from netproc");
			goto out;
		} else if (ACCT_STOP == op)
			break;

		switch (op) {
		case ACCT_SIGN:
		case ACCT_KID_SIGN:
			if (op_sign(netsock, pkey, op))
				break;
			warnx("op_sign");
			goto out;
		case ACCT_THUMBPRINT:
			if (op_thumbprint(netsock, pkey))
				break;
			warnx("op_thumbprint");
			goto out;
		default:
			abort();
		}
	}

	rc = 1;
out:
	close(netsock);
	if (f != NULL)
		fclose(f);
	EVP_PKEY_free(pkey);
	ERR_print_errors_fp(stderr);
	ERR_free_strings();
	return rc;
}
