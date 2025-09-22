/* $OpenBSD: asn1_local.h,v 1.10 2024/03/02 09:10:42 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2006.
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include "bytestring.h"

__BEGIN_HIDDEN_DECLS

/* Internal ASN1 structures and functions: not for application use */

ASN1_TYPE *ASN1_TYPE_pack_sequence(const ASN1_ITEM *it, void *s, ASN1_TYPE **t);
void *ASN1_TYPE_unpack_sequence(const ASN1_ITEM *it, const ASN1_TYPE *t);

/* These are used internally in the ASN1_OBJECT to keep track of
 * whether the names and data need to be free()ed */
#define ASN1_OBJECT_FLAG_DYNAMIC	 0x01	/* internal use */
#define ASN1_OBJECT_FLAG_CRITICAL	 0x02	/* critical x509v3 object id */
#define ASN1_OBJECT_FLAG_DYNAMIC_STRINGS 0x04	/* internal use */
#define ASN1_OBJECT_FLAG_DYNAMIC_DATA	 0x08	/* internal use */
struct asn1_object_st {
	const char *sn, *ln;
	int nid;
	int length;
	const unsigned char *data;	/* data remains const after init */
	int flags;	/* Should we free this one */
} /* ASN1_OBJECT */;

/* ASN1 print context structure */

struct asn1_pctx_st {
	unsigned long flags;
	unsigned long nm_flags;
	unsigned long cert_flags;
	unsigned long oid_flags;
	unsigned long str_flags;
} /* ASN1_PCTX */;

int asn1_get_choice_selector(ASN1_VALUE **pval, const ASN1_ITEM *it);
int asn1_set_choice_selector(ASN1_VALUE **pval, int value, const ASN1_ITEM *it);

ASN1_VALUE ** asn1_get_field_ptr(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt);

const ASN1_TEMPLATE *asn1_do_adb(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt, int nullerr);

int asn1_do_lock(ASN1_VALUE **pval, int op, const ASN1_ITEM *it);

void asn1_enc_init(ASN1_VALUE **pval, const ASN1_ITEM *it);
void asn1_enc_cleanup(ASN1_VALUE **pval, const ASN1_ITEM *it);
int asn1_enc_save(ASN1_VALUE **pval, CBS *cbs, const ASN1_ITEM *it);
int asn1_enc_restore(int *len, unsigned char **out, ASN1_VALUE **pval, const ASN1_ITEM *it);

int i2d_ASN1_BOOLEAN(int a, unsigned char **pp);
int d2i_ASN1_BOOLEAN(int *a, const unsigned char **pp, long length);

/*
 * Unicode codepoint constants
 */
#define	UNICODE_MAX		0x10FFFF
#define	UNICODE_SURROGATE_MIN	0x00D800
#define	UNICODE_SURROGATE_MAX	0x00DFFF

#define	UNICODE_IS_SURROGATE(x) \
	((x) >= UNICODE_SURROGATE_MIN && (x) <= UNICODE_SURROGATE_MAX)

int UTF8_getc(const unsigned char *str, int len, unsigned long *val);
int UTF8_putc(unsigned char *str, int len, unsigned long value);

int asn1_d2i_read_bio(BIO *in, BUF_MEM **pb);

int asn1_get_identifier_cbs(CBS *cbs, int der_mode, uint8_t *out_class,
    int *out_constructed, uint32_t *out_tag_number);
int asn1_get_length_cbs(CBS *cbs, int der_mode, int *out_indefinite,
    size_t *out_length);
int asn1_get_object_cbs(CBS *cbs, int der_mode, uint8_t *out_class,
    int *out_constructed, uint32_t *out_tag_number, int *out_indefinite,
    size_t *out_length);
int asn1_get_primitive(CBS *cbs, int der_mode, uint32_t *out_tag_number,
    CBS *out_content);

int asn1_must_be_constructed(int tag);
int asn1_must_be_primitive(int tag);
int asn1_tag2charwidth(int tag);

int asn1_abs_set_unused_bits(ASN1_BIT_STRING *abs, uint8_t unused_bits);
int c2i_ASN1_BIT_STRING_cbs(ASN1_BIT_STRING **out_abs, CBS *cbs);

int c2i_ASN1_ENUMERATED_cbs(ASN1_ENUMERATED **out_aenum, CBS *cbs);

int asn1_aint_get_uint64(CBS *cbs, uint64_t *out_val);
int asn1_aint_set_uint64(uint64_t val, uint8_t **out_data, int *out_len);
int asn1_aint_get_int64(CBS *cbs, int negative, int64_t *out_val);
int c2i_ASN1_INTEGER_cbs(ASN1_INTEGER **out_aint, CBS *cbs);

int c2i_ASN1_OBJECT_cbs(ASN1_OBJECT **out_aobj, CBS *content);
int i2t_ASN1_OBJECT_internal(const ASN1_OBJECT *aobj, char *buf, int buf_len,
    int no_name);
ASN1_OBJECT *t2i_ASN1_OBJECT_internal(const char *oid);

int asn1_time_parse_cbs(const CBS *cbs, int is_gentime, struct tm *out_tm);

ASN1_OBJECT *c2i_ASN1_OBJECT(ASN1_OBJECT **a, const unsigned char **pp,
    long length);
int i2c_ASN1_BIT_STRING(ASN1_BIT_STRING *a, unsigned char **pp);
ASN1_BIT_STRING *c2i_ASN1_BIT_STRING(ASN1_BIT_STRING **a,
    const unsigned char **pp, long length);
int i2c_ASN1_INTEGER(ASN1_INTEGER *a, unsigned char **pp);
ASN1_INTEGER *c2i_ASN1_INTEGER(ASN1_INTEGER **a, const unsigned char **pp,
    long length);
int OPENSSL_gmtime_adj(struct tm *tm, int offset_day, int64_t offset_sec);
int OPENSSL_gmtime_diff(int *pday, int *psec, const struct tm *from,
    const struct tm *to);
int asn1_time_time_t_to_tm(const time_t *time, struct tm *out_tm);
int asn1_time_tm_to_time_t(const struct tm *tm, time_t *out);

int ASN1_item_ndef_i2d(ASN1_VALUE *val, unsigned char **out, const ASN1_ITEM *it);

const BIO_METHOD *BIO_f_asn1(void);

BIO *BIO_new_NDEF(BIO *out, ASN1_VALUE *val, const ASN1_ITEM *it);

int i2d_ASN1_bio_stream(BIO *out, ASN1_VALUE *val, BIO *in, int flags,
    const ASN1_ITEM *it);
int PEM_write_bio_ASN1_stream(BIO *out, ASN1_VALUE *val, BIO *in, int flags,
    const char *hdr, const ASN1_ITEM *it);
int SMIME_write_ASN1(BIO *bio, ASN1_VALUE *val, BIO *data, int flags,
    int ctype_nid, int econt_nid, STACK_OF(X509_ALGOR) *mdalgs,
    const ASN1_ITEM *it);
ASN1_VALUE *SMIME_read_ASN1(BIO *bio, BIO **bcont, const ASN1_ITEM *it);

int ASN1_primitive_new(ASN1_VALUE **pval, const ASN1_ITEM *it);
void ASN1_primitive_free(ASN1_VALUE **pval, const ASN1_ITEM *it);

int ASN1_template_new(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt);
void ASN1_template_free(ASN1_VALUE **pval, const ASN1_TEMPLATE *tt);

int ASN1_time_parse(const char *_bytes, size_t _len, struct tm *_tm, int _mode);
int ASN1_time_tm_cmp(struct tm *_tm1, struct tm *_tm2);

__END_HIDDEN_DECLS
