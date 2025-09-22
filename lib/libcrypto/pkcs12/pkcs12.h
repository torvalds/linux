/* $OpenBSD: pkcs12.h,v 1.30 2025/05/10 19:01:16 tb Exp $ */
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

#ifndef HEADER_PKCS12_H
#define HEADER_PKCS12_H

#include <openssl/bio.h>
#include <openssl/x509.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PKCS12_KEY_ID	1
#define PKCS12_IV_ID	2
#define PKCS12_MAC_ID	3

/* Default iteration count */
#ifndef PKCS12_DEFAULT_ITER
#define PKCS12_DEFAULT_ITER	PKCS5_DEFAULT_ITER
#endif

#define PKCS12_MAC_KEY_LENGTH 20

#define PKCS12_SALT_LEN	16

/* Uncomment out next line for unicode password and names, otherwise ASCII */

/*#define PBE_UNICODE*/

#ifdef PBE_UNICODE
#define PKCS12_key_gen PKCS12_key_gen_uni
#define PKCS12_add_friendlyname PKCS12_add_friendlyname_uni
#else
#define PKCS12_key_gen PKCS12_key_gen_asc
#define PKCS12_add_friendlyname PKCS12_add_friendlyname_asc
#endif

/* MS key usage constants */

#define KEY_EX	0x10
#define KEY_SIG 0x80

typedef struct PKCS12_MAC_DATA_st PKCS12_MAC_DATA;

typedef struct PKCS12_st PKCS12;

typedef struct PKCS12_SAFEBAG_st PKCS12_SAFEBAG;

DECLARE_STACK_OF(PKCS12_SAFEBAG)
DECLARE_PKCS12_STACK_OF(PKCS12_SAFEBAG)

typedef struct pkcs12_bag_st PKCS12_BAGS;

#define PKCS12_ERROR	0
#define PKCS12_OK	1

#ifndef LIBRESSL_INTERNAL

/* Compatibility macros */

#define M_PKCS12_x5092certbag PKCS12_x5092certbag
#define M_PKCS12_x509crl2certbag PKCS12_x509crl2certbag

#define M_PKCS12_certbag2x509 PKCS12_certbag2x509
#define M_PKCS12_certbag2x509crl PKCS12_certbag2x509crl

#define M_PKCS12_unpack_p7data PKCS12_unpack_p7data
#define M_PKCS12_pack_authsafes PKCS12_pack_authsafes
#define M_PKCS12_unpack_authsafes PKCS12_unpack_authsafes
#define M_PKCS12_unpack_p7encdata PKCS12_unpack_p7encdata

#define M_PKCS12_decrypt_skey PKCS12_decrypt_skey
#define M_PKCS8_decrypt PKCS8_decrypt

#endif /* !LIBRESSL_INTERNAL */

#define M_PKCS12_bag_type	PKCS12_bag_type
#define M_PKCS12_cert_bag_type	PKCS12_cert_bag_type
#define M_PKCS12_crl_bag_type	PKCS12_cert_bag_type

#define PKCS12_bag_type		PKCS12_SAFEBAG_get_nid
#define PKCS12_cert_bag_type	PKCS12_SAFEBAG_get_bag_nid

#define PKCS12_certbag2x509	PKCS12_SAFEBAG_get1_cert
#define PKCS12_certbag2x509crl	PKCS12_SAFEBAG_get1_crl

#define PKCS12_x5092certbag	PKCS12_SAFEBAG_create_cert
#define PKCS12_x509crl2certbag	PKCS12_SAFEBAG_create_crl
#define PKCS12_MAKE_KEYBAG	PKCS12_SAFEBAG_create0_p8inf
#define PKCS12_MAKE_SHKEYBAG	PKCS12_SAFEBAG_create_pkcs8_encrypt

const ASN1_TYPE *PKCS12_SAFEBAG_get0_attr(const PKCS12_SAFEBAG *bag,
    int attr_nid);
const STACK_OF(X509_ATTRIBUTE) *
    PKCS12_SAFEBAG_get0_attrs(const PKCS12_SAFEBAG *bag);
int PKCS12_SAFEBAG_get_nid(const PKCS12_SAFEBAG *bag);
int PKCS12_SAFEBAG_get_bag_nid(const PKCS12_SAFEBAG *bag);

X509 *PKCS12_SAFEBAG_get1_cert(const PKCS12_SAFEBAG *bag);
X509_CRL *PKCS12_SAFEBAG_get1_crl(const PKCS12_SAFEBAG *bag);

ASN1_TYPE *PKCS8_get_attr(PKCS8_PRIV_KEY_INFO *p8, int attr_nid);
int PKCS12_mac_present(const PKCS12 *p12);
void PKCS12_get0_mac(const ASN1_OCTET_STRING **pmac, const X509_ALGOR **pmacalg,
    const ASN1_OCTET_STRING **psalt, const ASN1_INTEGER **piter,
    const PKCS12 *p12);

const PKCS8_PRIV_KEY_INFO *PKCS12_SAFEBAG_get0_p8inf(const PKCS12_SAFEBAG *bag);
const X509_SIG *PKCS12_SAFEBAG_get0_pkcs8(const PKCS12_SAFEBAG *bag);
const STACK_OF(PKCS12_SAFEBAG) *
    PKCS12_SAFEBAG_get0_safes(const PKCS12_SAFEBAG *bag);
const ASN1_OBJECT *PKCS12_SAFEBAG_get0_type(const PKCS12_SAFEBAG *bag);

PKCS8_PRIV_KEY_INFO *PKCS8_decrypt(const X509_SIG *p8, const char *pass,
    int passlen);
PKCS8_PRIV_KEY_INFO *PKCS12_decrypt_skey(const PKCS12_SAFEBAG *bag,
    const char *pass, int passlen);
X509_SIG *PKCS8_encrypt(int pbe_nid, const EVP_CIPHER *cipher,
    const char *pass, int passlen, unsigned char *salt, int saltlen, int iter,
    PKCS8_PRIV_KEY_INFO *p8);

STACK_OF(PKCS12_SAFEBAG) *PKCS12_unpack_p7data(PKCS7 *p7);
STACK_OF(PKCS12_SAFEBAG) *PKCS12_unpack_p7encdata(PKCS7 *p7, const char *pass,
    int passlen);
STACK_OF(PKCS7) *PKCS12_unpack_authsafes(const PKCS12 *p12);

int PKCS8_add_keyusage(PKCS8_PRIV_KEY_INFO *p8, int usage);
char *PKCS12_get_friendlyname(PKCS12_SAFEBAG *bag);
int PKCS12_key_gen_uni(unsigned char *pass, int passlen, unsigned char *salt,
    int saltlen, int id, int iter, int n, unsigned char *out,
    const EVP_MD *md_type);
int PKCS12_verify_mac(PKCS12 *p12, const char *pass, int passlen);
int PKCS12_set_mac(PKCS12 *p12, const char *pass, int passlen,
    unsigned char *salt, int saltlen, int iter,
    const EVP_MD *md_type);

unsigned char *OPENSSL_asc2uni(const char *asc, int asclen,
    unsigned char **uni, int *unilen);
char *OPENSSL_uni2asc(const unsigned char *uni, int unilen);

PKCS12 *PKCS12_new(void);
void PKCS12_free(PKCS12 *a);
PKCS12 *d2i_PKCS12(PKCS12 **a, const unsigned char **in, long len);
int i2d_PKCS12(PKCS12 *a, unsigned char **out);
extern const ASN1_ITEM PKCS12_it;

PKCS12_SAFEBAG *PKCS12_SAFEBAG_new(void);
void PKCS12_SAFEBAG_free(PKCS12_SAFEBAG *a);
PKCS12_SAFEBAG *d2i_PKCS12_SAFEBAG(PKCS12_SAFEBAG **a, const unsigned char **in, long len);
int i2d_PKCS12_SAFEBAG(PKCS12_SAFEBAG *a, unsigned char **out);
extern const ASN1_ITEM PKCS12_SAFEBAG_it;

void PKCS12_PBE_add(void);
int PKCS12_parse(PKCS12 *p12, const char *pass, EVP_PKEY **pkey, X509 **cert,
    STACK_OF(X509) **ca);
PKCS12 *PKCS12_create(const char *pass, const char *name, EVP_PKEY *pkey,
    X509 *cert, STACK_OF(X509) *ca, int nid_key, int nid_cert, int iter,
    int mac_iter, int keytype);

int i2d_PKCS12_bio(BIO *bp, PKCS12 *p12);
int i2d_PKCS12_fp(FILE *fp, PKCS12 *p12);
PKCS12 *d2i_PKCS12_bio(BIO *bp, PKCS12 **p12);
PKCS12 *d2i_PKCS12_fp(FILE *fp, PKCS12 **p12);
int PKCS12_newpass(PKCS12 *p12, const char *oldpass, const char *newpass);

void ERR_load_PKCS12_strings(void);

/* Error codes for the PKCS12 functions. */

/* Function codes. */
#define PKCS12_F_PARSE_BAG				 129
#define PKCS12_F_PARSE_BAGS				 103
#define PKCS12_F_PKCS12_ADD_FRIENDLYNAME		 100
#define PKCS12_F_PKCS12_ADD_FRIENDLYNAME_ASC		 127
#define PKCS12_F_PKCS12_ADD_FRIENDLYNAME_UNI		 102
#define PKCS12_F_PKCS12_ADD_LOCALKEYID			 104
#define PKCS12_F_PKCS12_CREATE				 105
#define PKCS12_F_PKCS12_GEN_MAC				 107
#define PKCS12_F_PKCS12_INIT				 109
#define PKCS12_F_PKCS12_ITEM_DECRYPT_D2I		 106
#define PKCS12_F_PKCS12_ITEM_I2D_ENCRYPT		 108
#define PKCS12_F_PKCS12_ITEM_PACK_SAFEBAG		 117
#define PKCS12_F_PKCS12_KEY_GEN_ASC			 110
#define PKCS12_F_PKCS12_KEY_GEN_UNI			 111
#define PKCS12_F_PKCS12_MAKE_KEYBAG			 112
#define PKCS12_F_PKCS12_MAKE_SHKEYBAG			 113
#define PKCS12_F_PKCS12_NEWPASS				 128
#define PKCS12_F_PKCS12_PACK_P7DATA			 114
#define PKCS12_F_PKCS12_PACK_P7ENCDATA			 115
#define PKCS12_F_PKCS12_PARSE				 118
#define PKCS12_F_PKCS12_PBE_CRYPT			 119
#define PKCS12_F_PKCS12_PBE_KEYIVGEN			 120
#define PKCS12_F_PKCS12_SETUP_MAC			 122
#define PKCS12_F_PKCS12_SET_MAC				 123
#define PKCS12_F_PKCS12_UNPACK_AUTHSAFES		 130
#define PKCS12_F_PKCS12_UNPACK_P7DATA			 131
#define PKCS12_F_PKCS12_VERIFY_MAC			 126
#define PKCS12_F_PKCS8_ADD_KEYUSAGE			 124
#define PKCS12_F_PKCS8_ENCRYPT				 125

/* Reason codes. */
#define PKCS12_R_CANT_PACK_STRUCTURE			 100
#define PKCS12_R_CONTENT_TYPE_NOT_DATA			 121
#define PKCS12_R_DECODE_ERROR				 101
#define PKCS12_R_ENCODE_ERROR				 102
#define PKCS12_R_ENCRYPT_ERROR				 103
#define PKCS12_R_ERROR_SETTING_ENCRYPTED_DATA_TYPE	 120
#define PKCS12_R_INVALID_NULL_ARGUMENT			 104
#define PKCS12_R_INVALID_NULL_PKCS12_POINTER		 105
#define PKCS12_R_IV_GEN_ERROR				 106
#define PKCS12_R_KEY_GEN_ERROR				 107
#define PKCS12_R_MAC_ABSENT				 108
#define PKCS12_R_MAC_GENERATION_ERROR			 109
#define PKCS12_R_MAC_SETUP_ERROR			 110
#define PKCS12_R_MAC_STRING_SET_ERROR			 111
#define PKCS12_R_MAC_VERIFY_ERROR			 112
#define PKCS12_R_MAC_VERIFY_FAILURE			 113
#define PKCS12_R_PARSE_ERROR				 114
#define PKCS12_R_PKCS12_ALGOR_CIPHERINIT_ERROR		 115
#define PKCS12_R_PKCS12_CIPHERFINAL_ERROR		 116
#define PKCS12_R_PKCS12_PBE_CRYPT_ERROR			 117
#define PKCS12_R_UNKNOWN_DIGEST_ALGORITHM		 118
#define PKCS12_R_UNSUPPORTED_PKCS12_MODE		 119

#ifdef  __cplusplus
}
#endif
#endif
