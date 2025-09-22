/* $OpenBSD: asn1_par.c,v 1.35 2023/07/05 21:23:36 beck Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>

#include <openssl/asn1.h>
#include <openssl/buffer.h>
#include <openssl/objects.h>

static int asn1_print_info(BIO *bp, int tag, int xclass, int constructed,
    int indent);
static int asn1_parse2(BIO *bp, const unsigned char **pp, long length,
    int offset, int depth, int indent, int dump);

static int
asn1_print_info(BIO *bp, int tag, int xclass, int constructed,
    int indent)
{
	char str[128];
	const char *p;

	if (constructed & V_ASN1_CONSTRUCTED)
		p="cons: ";
	else
		p="prim: ";
	if (BIO_write(bp, p, 6) < 6)
		goto err;
	if (!BIO_indent(bp, indent, 128))
		goto err;

	p = str;
	if ((xclass & V_ASN1_PRIVATE) == V_ASN1_PRIVATE)
		snprintf(str, sizeof str, "priv [ %d ] ", tag);
	else if ((xclass & V_ASN1_CONTEXT_SPECIFIC) == V_ASN1_CONTEXT_SPECIFIC)
		snprintf(str, sizeof str, "cont [ %d ]", tag);
	else if ((xclass & V_ASN1_APPLICATION) == V_ASN1_APPLICATION)
		snprintf(str, sizeof str, "appl [ %d ]", tag);
	else if (tag > 30)
		snprintf(str, sizeof str, "<ASN1 %d>", tag);
	else
		p = ASN1_tag2str(tag);

	if (BIO_printf(bp, "%-18s", p) <= 0)
		goto err;
	return (1);
 err:
	return (0);
}

int
ASN1_parse(BIO *bp, const unsigned char *pp, long len, int indent)
{
	return (asn1_parse2(bp, &pp, len, 0, 0, indent, 0));
}
LCRYPTO_ALIAS(ASN1_parse);

int
ASN1_parse_dump(BIO *bp, const unsigned char *pp, long len, int indent, int dump)
{
	return (asn1_parse2(bp, &pp, len, 0, 0, indent, dump));
}
LCRYPTO_ALIAS(ASN1_parse_dump);

static int
asn1_parse2(BIO *bp, const unsigned char **pp, long length, int offset,
    int depth, int indent, int dump)
{
	const unsigned char *p, *ep, *tot, *op, *opp;
	long len;
	int tag, xclass, ret = 0;
	int nl, hl, j, r;
	ASN1_OBJECT *o = NULL;
	ASN1_OCTET_STRING *os = NULL;
	ASN1_INTEGER *ai = NULL;
	ASN1_ENUMERATED *ae = NULL;
	/* ASN1_BMPSTRING *bmp=NULL;*/
	int dump_indent;

	dump_indent = 6;	/* Because we know BIO_dump_indent() */
	p = *pp;
	tot = p + length;
	op = p - 1;
	if (depth > 128) {
		BIO_printf(bp, "Max depth exceeded\n");
		goto end;
	}
	while ((p < tot) && (op < p)) {
		op = p;
		j = ASN1_get_object(&p, &len, &tag, &xclass, length);

		if (j & 0x80) {
			if (BIO_write(bp, "Error in encoding\n", 18) <= 0)
				goto end;
			ret = 0;
			goto end;
		}
		hl = (p - op);
		length -= hl;
		/* if j == 0x21 it is a constructed indefinite length object */
		if (BIO_printf(bp, "%5ld:", (long)offset +
		    (long)(op - *pp)) <= 0)
		    goto end;

		if (j != (V_ASN1_CONSTRUCTED | 1)) {
			if (BIO_printf(bp, "d=%-2d hl=%ld l=%4ld ",
			    depth, (long)hl, len) <= 0)
				goto end;
		} else {
			if (BIO_printf(bp, "d=%-2d hl=%ld l=inf  ",
			    depth, (long)hl) <= 0)
				goto end;
		}
		if (!asn1_print_info(bp, tag, xclass, j, (indent) ? depth : 0))
			goto end;
		if (j & V_ASN1_CONSTRUCTED) {
			ep = p + len;
			if (BIO_write(bp, "\n", 1) <= 0)
				goto end;
			if (len > length) {
				BIO_printf(bp, "length is greater than %ld\n",
				    length);
				ret = 0;
				goto end;
			}
			if ((j == 0x21) && (len == 0)) {
				for (;;) {
					r = asn1_parse2(bp, &p, (long)(tot - p),
					    offset + (p - *pp), depth + 1,
					    indent, dump);
					if (r == 0) {
						ret = 0;
						goto end;
					}
					if ((r == 2) || (p >= tot)) {
						len = (long)(p - ep);
						break;
					}
				}
			} else {
				while (p < ep) {
					r = asn1_parse2(bp, &p, (long)(ep - p),
					    offset + (p - *pp), depth + 1,
					    indent, dump);
					if (r == 0) {
						ret = 0;
						goto end;
					}
				}
			}
		} else if (xclass != 0) {
			p += len;
			if (BIO_write(bp, "\n", 1) <= 0)
				goto end;
		} else {
			nl = 0;
			if ((tag == V_ASN1_PRINTABLESTRING) ||
			    (tag == V_ASN1_T61STRING) ||
			    (tag == V_ASN1_IA5STRING) ||
			    (tag == V_ASN1_VISIBLESTRING) ||
			    (tag == V_ASN1_NUMERICSTRING) ||
			    (tag == V_ASN1_UTF8STRING) ||
			    (tag == V_ASN1_UTCTIME) ||
			    (tag == V_ASN1_GENERALIZEDTIME)) {
				if (BIO_write(bp, ":", 1) <= 0)
					goto end;
				if ((len > 0) &&
				    BIO_write(bp, (const char *)p, (int)len) !=
				    (int)len)
					goto end;
			} else if (tag == V_ASN1_OBJECT) {
				opp = op;
				if (d2i_ASN1_OBJECT(&o, &opp, len + hl) !=
				    NULL) {
					if (BIO_write(bp, ":", 1) <= 0)
						goto end;
					i2a_ASN1_OBJECT(bp, o);
				} else {
					if (BIO_write(bp, ":BAD OBJECT",
					    11) <= 0)
						goto end;
				}
			} else if (tag == V_ASN1_BOOLEAN) {
				if (len == 1 && p < tot) {
					BIO_printf(bp, ":%u", p[0]);
				} else {
					if (BIO_write(bp, "Bad boolean\n",
					    12) <= 0)
						goto end;
				}
			} else if (tag == V_ASN1_BMPSTRING) {
				/* do the BMP thang */
			} else if (tag == V_ASN1_OCTET_STRING) {
				int i, printable = 1;

				opp = op;
				os = d2i_ASN1_OCTET_STRING(NULL, &opp, len + hl);
				if (os != NULL && os->length > 0) {
					opp = os->data;
					/* testing whether the octet string is
					 * printable */
					for (i = 0; i < os->length; i++) {
						if (((opp[i] < ' ') &&
						    (opp[i] != '\n') &&
						    (opp[i] != '\r') &&
						    (opp[i] != '\t')) ||
						    (opp[i] > '~')) {
							printable = 0;
							break;
						}
					}
					if (printable) {
						/* printable string */
						if (BIO_write(bp, ":", 1) <= 0)
							goto end;
						if (BIO_write(bp, (const char *)opp,
							    os->length) <= 0)
							goto end;
					} else if (!dump) {
						/* not printable => print octet string
						 * as hex dump */
						if (BIO_write(bp, "[HEX DUMP]:", 11) <= 0)
							goto end;
						for (i = 0; i < os->length; i++) {
							if (BIO_printf(bp,
							    "%02X", opp[i]) <= 0)
								goto end;
						}
					} else {
						/* print the normal dump */
						if (!nl) {
							if (BIO_write(bp, "\n", 1) <= 0)
								goto end;
						}
						if (BIO_dump_indent(bp,
						    (const char *)opp,
						    ((dump == -1 || dump >
						    os->length) ? os->length : dump),
						    dump_indent) <= 0)
							goto end;
						nl = 1;
					}
				}
				ASN1_OCTET_STRING_free(os);
				os = NULL;
			} else if (tag == V_ASN1_INTEGER) {
				int i;

				opp = op;
				ai = d2i_ASN1_INTEGER(NULL, &opp, len + hl);
				if (ai != NULL) {
					if (BIO_write(bp, ":", 1) <= 0)
						goto end;
					if (ai->type == V_ASN1_NEG_INTEGER)
						if (BIO_write(bp, "-", 1) <= 0)
							goto end;
					for (i = 0; i < ai->length; i++) {
						if (BIO_printf(bp, "%02X",
						    ai->data[i]) <= 0)
							goto end;
					}
					if (ai->length == 0) {
						if (BIO_write(bp, "00", 2) <= 0)
							goto end;
					}
				} else {
					if (BIO_write(bp, "BAD INTEGER", 11) <= 0)
						goto end;
				}
				ASN1_INTEGER_free(ai);
				ai = NULL;
			} else if (tag == V_ASN1_ENUMERATED) {
				int i;

				opp = op;
				ae = d2i_ASN1_ENUMERATED(NULL, &opp, len + hl);
				if (ae != NULL) {
					if (BIO_write(bp, ":", 1) <= 0)
						goto end;
					if (ae->type == V_ASN1_NEG_ENUMERATED)
						if (BIO_write(bp, "-", 1) <= 0)
							goto end;
					for (i = 0; i < ae->length; i++) {
						if (BIO_printf(bp, "%02X",
						    ae->data[i]) <= 0)
							goto end;
					}
					if (ae->length == 0) {
						if (BIO_write(bp, "00", 2) <= 0)
							goto end;
					}
				} else {
					if (BIO_write(bp, "BAD ENUMERATED", 14) <= 0)
						goto end;
				}
				ASN1_ENUMERATED_free(ae);
				ae = NULL;
			} else if (len > 0 && dump) {
				if (!nl) {
					if (BIO_write(bp, "\n", 1) <= 0)
						goto end;
				}
				if (BIO_dump_indent(bp, (const char *)p,
				    ((dump == -1 || dump > len) ? len : dump),
				    dump_indent) <= 0)
					goto end;
				nl = 1;
			}

			if (!nl) {
				if (BIO_write(bp, "\n", 1) <= 0)
					goto end;
			}
			p += len;
			if ((tag == V_ASN1_EOC) && (xclass == 0)) {
				ret = 2; /* End of sequence */
				goto end;
			}
		}
		length -= len;
	}
	ret = 1;

 end:
	if (o != NULL)
		ASN1_OBJECT_free(o);
	ASN1_OCTET_STRING_free(os);
	ASN1_INTEGER_free(ai);
	ASN1_ENUMERATED_free(ae);
	*pp = p;
	return (ret);
}
