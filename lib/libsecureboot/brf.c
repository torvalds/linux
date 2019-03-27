// The functions here are derrived from BearSSL/tools/*.c
// When that is refactored suitably we can use them directly.
/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define NEED_BRSSL_H
#include "libsecureboot-priv.h"
#include <brssl.h>


static int
is_ign(int c)
{
	if (c == 0) {
		return (0);
	}
	if (c <= 32 || c == '-' || c == '_' || c == '.'
		|| c == '/' || c == '+' || c == ':')
	{
		return (1);
	}
	return (0);
}

/*
 * Get next non-ignored character, normalised:
 *    ASCII letters are converted to lowercase
 *    control characters, space, '-', '_', '.', '/', '+' and ':' are ignored
 * A terminating zero is returned as 0.
 */
static int
next_char(const char **ps, const char *limit)
{
	for (;;) {
		int c;

		if (*ps == limit) {
			return (0);
		}
		c = *(*ps) ++;
		if (c == 0) {
			return (0);
		}
		if (c >= 'A' && c <= 'Z') {
			c += 'a' - 'A';
		}
		if (!is_ign(c)) {
			return (c);
		}
	}
}

/*
 * Partial string equality comparison, with normalisation.
 */
static int
eqstr_chunk(const char *s1, size_t s1_len, const char *s2, size_t s2_len)
{
	const char *lim1, *lim2;

	lim1 = s1 + s1_len;
	lim2 = s2 + s2_len;
	for (;;) {
		int c1, c2;

		c1 = next_char(&s1, lim1);
		c2 = next_char(&s2, lim2);
		if (c1 != c2) {
			return (0);
		}
		if (c1 == 0) {
			return (1);
		}
	}
}

/* see brssl.h */
int
eqstr(const char *s1, const char *s2)
{
	return (eqstr_chunk(s1, strlen(s1), s2, strlen(s2)));
}

int
looks_like_DER(const unsigned char *buf, size_t len)
{
	int fb;
	size_t dlen;

	if (len < 2) {
		return (0);
	}
	if (*buf ++ != 0x30) {
		return (0);
	}
	fb = *buf ++;
	len -= 2;
	if (fb < 0x80) {
		return ((size_t)fb == len);
	} else if (fb == 0x80) {
		return (0);
	} else {
		fb -= 0x80;
		if (len < (size_t)fb + 2) {
			return (0);
		}
		len -= (size_t)fb;
		dlen = 0;
		while (fb -- > 0) {
			if (dlen > (len >> 8)) {
				return (0);
			}
			dlen = (dlen << 8) + (size_t)*buf ++;
		}
		return (dlen == len);
	}
}

static void
vblob_append(void *cc, const void *data, size_t len)
{
	bvector *bv;

	bv = cc;
	VEC_ADDMANY(*bv, data, len);
}

void
free_pem_object_contents(pem_object *po)
{
	if (po != NULL) {
		xfree(po->name);
		xfree(po->data);
	}
}

pem_object *
decode_pem(const void *src, size_t len, size_t *num)
{
	VECTOR(pem_object) pem_list = VEC_INIT;
	br_pem_decoder_context pc;
	pem_object po, *pos;
	const unsigned char *buf;
	bvector bv = VEC_INIT;
	int inobj;
	int extra_nl;

	*num = 0;
	br_pem_decoder_init(&pc);
	buf = src;
	inobj = 0;
	po.name = NULL;
	po.data = NULL;
	po.data_len = 0;
	extra_nl = 1;
	while (len > 0) {
		size_t tlen;

		tlen = br_pem_decoder_push(&pc, buf, len);
		buf += tlen;
		len -= tlen;
		switch (br_pem_decoder_event(&pc)) {

		case BR_PEM_BEGIN_OBJ:
			po.name = xstrdup(br_pem_decoder_name(&pc));
			br_pem_decoder_setdest(&pc, vblob_append, &bv);
			inobj = 1;
			break;

		case BR_PEM_END_OBJ:
			if (inobj) {
				po.data = VEC_TOARRAY(bv);
				po.data_len = VEC_LEN(bv);
				VEC_ADD(pem_list, po);
				VEC_CLEAR(bv);
				po.name = NULL;
				po.data = NULL;
				po.data_len = 0;
				inobj = 0;
			}
			break;

		case BR_PEM_ERROR:
			xfree(po.name);
			VEC_CLEAR(bv);
			ve_error_set("ERROR: invalid PEM encoding");
			VEC_CLEAREXT(pem_list, &free_pem_object_contents);
			return (NULL);
		}

		/*
		 * We add an extra newline at the end, in order to
		 * support PEM files that lack the newline on their last
		 * line (this is somwehat invalid, but PEM format is not
		 * standardised and such files do exist in the wild, so
		 * we'd better accept them).
		 */
		if (len == 0 && extra_nl) {
			extra_nl = 0;
			buf = (const unsigned char *)"\n";
			len = 1;
		}
	}
	if (inobj) {
	    ve_error_set("ERROR: unfinished PEM object");
		xfree(po.name);
		VEC_CLEAR(bv);
		VEC_CLEAREXT(pem_list, &free_pem_object_contents);
		return (NULL);
	}

	*num = VEC_LEN(pem_list);
	VEC_ADD(pem_list, po);
	pos = VEC_TOARRAY(pem_list);
	VEC_CLEAR(pem_list);
	return (pos);
}

br_x509_certificate *
parse_certificates(unsigned char *buf, size_t len, size_t *num)
{
	VECTOR(br_x509_certificate) cert_list = VEC_INIT;
	pem_object *pos;
	size_t u, num_pos;
	br_x509_certificate *xcs;
	br_x509_certificate dummy;

	*num = 0;

	/*
	 * Check for a DER-encoded certificate.
	 */
	if (looks_like_DER(buf, len)) {
		xcs = xmalloc(2 * sizeof *xcs);
		xcs[0].data = buf;
		xcs[0].data_len = len;
		xcs[1].data = NULL;
		xcs[1].data_len = 0;
		*num = 1;
		return (xcs);
	}

	pos = decode_pem(buf, len, &num_pos);
	if (pos == NULL) {
		return (NULL);
	}
	for (u = 0; u < num_pos; u ++) {
		if (eqstr(pos[u].name, "CERTIFICATE")
			|| eqstr(pos[u].name, "X509 CERTIFICATE"))
		{
			br_x509_certificate xc;

			xc.data = pos[u].data;
			xc.data_len = pos[u].data_len;
			pos[u].data = NULL;
			VEC_ADD(cert_list, xc);
		}
	}
	for (u = 0; u < num_pos; u ++) {
		free_pem_object_contents(&pos[u]);
	}
	xfree(pos);

	if (VEC_LEN(cert_list) == 0) {
		return (NULL);
	}
	*num = VEC_LEN(cert_list);
	dummy.data = NULL;
	dummy.data_len = 0;
	VEC_ADD(cert_list, dummy);
	xcs = VEC_TOARRAY(cert_list);
	VEC_CLEAR(cert_list);
	return (xcs);
}

br_x509_certificate *
read_certificates(const char *fname, size_t *num)
{
	br_x509_certificate *xcs;
	unsigned char *buf;
	size_t len;

	*num = 0;

	/*
	 * TODO: reading the whole file is crude; we could parse them
	 * in a streamed fashion. But it does not matter much in practice.
	 */
	buf = read_file(fname, &len);
	if (buf == NULL) {
		return (NULL);
	}
	xcs = parse_certificates(buf, len, num);
	if (xcs == NULL) {
	    ve_error_set("ERROR: no certificate in file '%s'\n", fname);
	}
	xfree(buf);
	return (xcs);
}

/* see brssl.h */
void
free_certificates(br_x509_certificate *certs, size_t num)
{
	size_t u;

	for (u = 0; u < num; u ++) {
		xfree(certs[u].data);
	}
	xfree(certs);
}


static void
dn_append(void *ctx, const void *buf, size_t len)
{
	VEC_ADDMANY(*(bvector *)ctx, buf, len);
}

int
certificate_to_trust_anchor_inner(br_x509_trust_anchor *ta,
	br_x509_certificate *xc)
{
	br_x509_decoder_context dc;
	bvector vdn = VEC_INIT;
	br_x509_pkey *pk;

	br_x509_decoder_init(&dc, dn_append, &vdn);
	br_x509_decoder_push(&dc, xc->data, xc->data_len);
	pk = br_x509_decoder_get_pkey(&dc);
	if (pk == NULL) {
	    ve_error_set("ERROR: CA decoding failed with error %d\n",
		      br_x509_decoder_last_error(&dc));
	    VEC_CLEAR(vdn);
	    return (-1);
	}
	ta->dn.data = VEC_TOARRAY(vdn);
	ta->dn.len = VEC_LEN(vdn);
	VEC_CLEAR(vdn);
	ta->flags = 0;
	if (br_x509_decoder_isCA(&dc)) {
		ta->flags |= BR_X509_TA_CA;
	}
	switch (pk->key_type) {
	case BR_KEYTYPE_RSA:
		ta->pkey.key_type = BR_KEYTYPE_RSA;
		ta->pkey.key.rsa.n = xblobdup(pk->key.rsa.n, pk->key.rsa.nlen);
		ta->pkey.key.rsa.nlen = pk->key.rsa.nlen;
		ta->pkey.key.rsa.e = xblobdup(pk->key.rsa.e, pk->key.rsa.elen);
		ta->pkey.key.rsa.elen = pk->key.rsa.elen;
		break;
	case BR_KEYTYPE_EC:
		ta->pkey.key_type = BR_KEYTYPE_EC;
		ta->pkey.key.ec.curve = pk->key.ec.curve;
		ta->pkey.key.ec.q = xblobdup(pk->key.ec.q, pk->key.ec.qlen);
		ta->pkey.key.ec.qlen = pk->key.ec.qlen;
		break;
	default:
	    ve_error_set("ERROR: unsupported public key type in CA\n");
		xfree(ta->dn.data);
		return (-1);
	}
	return (0);
}

/* see brssl.h */
void
free_ta_contents(br_x509_trust_anchor *ta)
{
	xfree(ta->dn.data);
	switch (ta->pkey.key_type) {
	case BR_KEYTYPE_RSA:
		xfree(ta->pkey.key.rsa.n);
		xfree(ta->pkey.key.rsa.e);
		break;
	case BR_KEYTYPE_EC:
		xfree(ta->pkey.key.ec.q);
		break;
	}
}
