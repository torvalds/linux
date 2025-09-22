/* $OpenBSD: pkcs12_local.h,v 1.6 2025/03/09 15:45:52 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999 The OpenSSL Project.  All rights reserved.
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

#ifndef HEADER_PKCS12_LOCAL_H
#define HEADER_PKCS12_LOCAL_H

__BEGIN_HIDDEN_DECLS

struct PKCS12_MAC_DATA_st {
	X509_SIG *dinfo;
	ASN1_OCTET_STRING *salt;
	ASN1_INTEGER *iter;	/* defaults to 1 */
};

struct PKCS12_st {
	ASN1_INTEGER *version;
	PKCS12_MAC_DATA *mac;
	PKCS7 *authsafes;
};

struct PKCS12_SAFEBAG_st {
	ASN1_OBJECT *type;
	union {
	struct pkcs12_bag_st *bag; /* secret, crl and certbag */
	struct pkcs8_priv_key_info_st	*keybag; /* keybag */
	X509_SIG *shkeybag; /* shrouded key bag */
		STACK_OF(PKCS12_SAFEBAG) *safes;
		ASN1_TYPE *other;
	} value;
	STACK_OF(X509_ATTRIBUTE) *attrib;
};

struct pkcs12_bag_st {
	ASN1_OBJECT *type;
	union {
		ASN1_OCTET_STRING *x509cert;
		ASN1_OCTET_STRING *x509crl;
		ASN1_OCTET_STRING *octet;
		ASN1_IA5STRING *sdsicert;
		ASN1_TYPE *other; /* Secret or other bag */
	} value;
};

extern const ASN1_ITEM PKCS12_SAFEBAGS_it;
extern const ASN1_ITEM PKCS12_AUTHSAFES_it;

PKCS12_BAGS *PKCS12_BAGS_new(void);
void PKCS12_BAGS_free(PKCS12_BAGS *a);
PKCS12_BAGS *d2i_PKCS12_BAGS(PKCS12_BAGS **a, const unsigned char **in, long len);
int i2d_PKCS12_BAGS(PKCS12_BAGS *a, unsigned char **out);
extern const ASN1_ITEM PKCS12_BAGS_it;

PKCS12_MAC_DATA *PKCS12_MAC_DATA_new(void);
void PKCS12_MAC_DATA_free(PKCS12_MAC_DATA *a);
PKCS12_MAC_DATA *d2i_PKCS12_MAC_DATA(PKCS12_MAC_DATA **a, const unsigned char **in, long len);
int i2d_PKCS12_MAC_DATA(PKCS12_MAC_DATA *a, unsigned char **out);
extern const ASN1_ITEM PKCS12_MAC_DATA_it;

PKCS12_SAFEBAG *PKCS12_SAFEBAG_create0_p8inf(PKCS8_PRIV_KEY_INFO *p8);
PKCS12_SAFEBAG *PKCS12_SAFEBAG_create0_pkcs8(X509_SIG *p8);
PKCS12_SAFEBAG *PKCS12_SAFEBAG_create_cert(X509 *x509);
PKCS12_SAFEBAG *PKCS12_SAFEBAG_create_crl(X509_CRL *crl);
PKCS12_SAFEBAG *PKCS12_SAFEBAG_create_pkcs8_encrypt(int pbe_nid,
    const char *pass, int passlen, unsigned char *salt, int saltlen, int iter,
    PKCS8_PRIV_KEY_INFO *p8);

PKCS12_SAFEBAG *PKCS12_add_cert(STACK_OF(PKCS12_SAFEBAG) **pbags, X509 *cert);
PKCS12_SAFEBAG *PKCS12_add_key(STACK_OF(PKCS12_SAFEBAG) **pbags, EVP_PKEY *key,
    int key_usage, int iter, int key_nid, const char *pass);
int PKCS12_add_safe(STACK_OF(PKCS7) **psafes, STACK_OF(PKCS12_SAFEBAG) *bags,
    int safe_nid, int iter, const char *pass);
PKCS12 *PKCS12_add_safes(STACK_OF(PKCS7) *safes, int p7_nid);

int PKCS12_add_CSPName_asc(PKCS12_SAFEBAG *bag, const char *name,
    int namelen);
int PKCS12_add_friendlyname_asc(PKCS12_SAFEBAG *bag, const char *name,
    int namelen);
int PKCS12_add_friendlyname_uni(PKCS12_SAFEBAG *bag, const unsigned char *name,
    int namelen);
int PKCS12_add_localkeyid(PKCS12_SAFEBAG *bag, unsigned char *name,
    int namelen);

int PKCS12_gen_mac(PKCS12 *p12, const char *pass, int passlen,
    unsigned char *mac, unsigned int *maclen);

ASN1_TYPE *PKCS12_get_attr_gen(const STACK_OF(X509_ATTRIBUTE) *attrs,
    int attr_nid);

PKCS12 *PKCS12_init(int mode);

void *PKCS12_item_decrypt_d2i(const X509_ALGOR *algor, const ASN1_ITEM *it,
    const char *pass, int passlen, const ASN1_OCTET_STRING *oct, int zbuf);
ASN1_OCTET_STRING *PKCS12_item_i2d_encrypt(X509_ALGOR *algor,
    const ASN1_ITEM *it, const char *pass, int passlen, void *obj, int zbuf);
PKCS12_SAFEBAG *PKCS12_item_pack_safebag(void *obj, const ASN1_ITEM *it,
    int nid1, int nid2);

int PKCS12_key_gen_asc(const char *pass, int passlen, unsigned char *salt,
    int saltlen, int id, int iter, int n, unsigned char *out,
    const EVP_MD *md_type);

int PKCS12_pack_authsafes(PKCS12 *p12, STACK_OF(PKCS7) *safes);
PKCS7 *PKCS12_pack_p7data(STACK_OF(PKCS12_SAFEBAG) *sk);
PKCS7 *PKCS12_pack_p7encdata(int pbe_nid, const char *pass, int passlen,
    unsigned char *salt, int saltlen, int iter, STACK_OF(PKCS12_SAFEBAG) *bags);

unsigned char *PKCS12_pbe_crypt(const X509_ALGOR *algor, const char *pass,
    int passlen, const unsigned char *in, int inlen, unsigned char **data,
    int *datalen, int en_de);

int PKCS12_setup_mac(PKCS12 *p12, int iter, unsigned char *salt,
    int saltlen, const EVP_MD *md_type);

/* XXX - should go into pkcs7_local.h. */
ASN1_OCTET_STRING *PKCS7_get_octet_string(PKCS7 *p7);

__END_HIDDEN_DECLS

#endif /* !HEADER_PKCS12_LOCAL_H */
