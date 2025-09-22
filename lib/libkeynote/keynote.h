/* $OpenBSD: keynote.h,v 1.16 2004/06/24 21:34:33 msf Exp $ */
/*
 * The author of this code is Angelos D. Keromytis (angelos@dsl.cis.upenn.edu)
 *
 * This code was written by Angelos D. Keromytis in Philadelphia, PA, USA,
 * in April-May 1998
 *
 * Copyright (C) 1998, 1999 by Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software. 
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, THE AUTHORS MAKES NO
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#ifndef __KEYNOTE_H__
#define __KEYNOTE_H__

struct environment
{
    char               *env_name;
    char               *env_value;
    int                 env_flags;
    regex_t             env_regex;
    struct environment *env_next;
};

struct keynote_deckey
{
    int   dec_algorithm;
    void *dec_key;
};

struct keynote_binary
{
    int   bn_len;
    char *bn_key;
};

struct keynote_keylist
{
    int                     key_alg;
    void                   *key_key;
    char                   *key_stringkey;
    struct keynote_keylist *key_next;
};

#define SIG_DSA_SHA1_HEX              "sig-dsa-sha1-hex:"
#define SIG_DSA_SHA1_HEX_LEN          strlen(SIG_DSA_SHA1_HEX)
#define SIG_DSA_SHA1_BASE64           "sig-dsa-sha1-base64:"
#define SIG_DSA_SHA1_BASE64_LEN       strlen(SIG_DSA_SHA1_BASE64)
#define SIG_RSA_SHA1_PKCS1_HEX        "sig-rsa-sha1-hex:"
#define SIG_RSA_SHA1_PKCS1_HEX_LEN    strlen(SIG_RSA_SHA1_PKCS1_HEX)
#define SIG_RSA_SHA1_PKCS1_BASE64     "sig-rsa-sha1-base64:"
#define SIG_RSA_SHA1_PKCS1_BASE64_LEN strlen(SIG_RSA_SHA1_PKCS1_BASE64)
#define SIG_RSA_MD5_PKCS1_HEX         "sig-rsa-md5-hex:"
#define SIG_RSA_MD5_PKCS1_HEX_LEN     strlen(SIG_RSA_MD5_PKCS1_HEX)
#define SIG_RSA_MD5_PKCS1_BASE64      "sig-rsa-md5-base64:"
#define SIG_RSA_MD5_PKCS1_BASE64_LEN  strlen(SIG_RSA_MD5_PKCS1_BASE64)
#define SIG_ELGAMAL_SHA1_HEX          "sig-elgamal-sha1-hex:"
#define SIG_ELGAMAL_SHA1_HEX_LEN      strlen(SIG_ELGAMAL_SHA1_HEX)
#define SIG_ELGAMAL_SHA1_BASE64       "sig-elgamal-sha1-base64:"
#define SIG_ELGAMAL_SHA1_BASE64_LEN   strlen(SIG_ELGAMAL_SHA1_BASE64)
#define SIG_PGP_NATIVE                "sig-pgp:"
#define SIG_PGP_NATIVE_LEN            strlen(SIG_PGP_NATIVE)
#define SIG_X509_SHA1_BASE64          "sig-x509-sha1-base64:"
#define SIG_X509_SHA1_BASE64_LEN      strlen(SIG_X509_SHA1_BASE64)
#define SIG_X509_SHA1_HEX             "sig-x509-sha1-hex:"
#define SIG_X509_SHA1_HEX_LEN         strlen(SIG_X509_SHA1_HEX)

#define SIGRESULT_UNTOUCHED     0
#define SIGRESULT_FALSE         1
#define SIGRESULT_TRUE          2

#define ENVIRONMENT_FLAG_FUNC   0x0001 /* This is a callback function */
#define ENVIRONMENT_FLAG_REGEX  0x0002 /* Regular expression for name */

#define ASSERT_FLAG_LOCAL       0x0001 /* 
					* Trusted assertion -- means
					* signature is not verified, and
					* authorizer field can 
					* include symbolic names.
				        */
#define ASSERT_FLAG_SIGGEN      0x0002 /*
					* Be a bit more lax with the
					* contents of the Signature:
					* field; to be used in
					* assertion signing only.
					*/
#define ASSERT_FLAG_SIGVER	0x0004 /*
					* To be used in signature verification
					* only.
					*/
#define RESULT_FALSE            0
#define RESULT_TRUE             1

#define KEYNOTE_CALLBACK_INITIALIZE		"_KEYNOTE_CALLBACK_INITIALIZE"
#define KEYNOTE_CALLBACK_CLEANUP		"_KEYNOTE_CALLBACK_CLEANUP"

#define KEYNOTE_VERSION_STRING			"2"

#define ERROR_MEMORY	       -1
#define ERROR_SYNTAX	       -2
#define ERROR_NOTFOUND         -3
#define ERROR_SIGN_FAILURE     -4

#define KEYNOTE_ALGORITHM_UNSPEC       -1
#define KEYNOTE_ALGORITHM_NONE		0
#define KEYNOTE_ALGORITHM_DSA		1
#define KEYNOTE_ALGORITHM_ELGAMAL	2
#define KEYNOTE_ALGORITHM_PGP		3
#define KEYNOTE_ALGORITHM_BINARY        4
#define KEYNOTE_ALGORITHM_X509          5
#define KEYNOTE_ALGORITHM_RSA		6

#define KEYNOTE_ERROR_ANY        0
#define KEYNOTE_ERROR_SYNTAX     1
#define KEYNOTE_ERROR_MEMORY     2
#define KEYNOTE_ERROR_SIGNATURE  3

#define ENCODING_NONE		   0
#define ENCODING_HEX		   1
#define ENCODING_BASE64		   2
#define ENCODING_NATIVE		   3	/* For things like PGP */

#define INTERNAL_ENC_NONE	   0
#define INTERNAL_ENC_PKCS1	   1
#define INTERNAL_ENC_ASN1          2
#define INTERNAL_ENC_NATIVE	   3	/* For things like PGP */

#define KEYNOTE_PUBLIC_KEY         0
#define KEYNOTE_PRIVATE_KEY        1

extern int keynote_errno;

__BEGIN_DECLS
/* Session API */
int    kn_init(void);
int    kn_add_assertion(int, char *, int, int);
int    kn_remove_assertion(int, int);
int    kn_add_action(int, char *, char *, int);
int    kn_remove_action(int, char *);
int    kn_add_authorizer(int, char *);
int    kn_remove_authorizer(int, char *);
int    kn_do_query(int, char **, int);
int    kn_get_failed(int, int, int);
int    kn_cleanup_action_environment(int);
int    kn_close(int);
void   kn_free_key(struct keynote_deckey *);
char  *kn_get_string(char *);

/* Simple API */
int    kn_query(struct environment *, char **, int, char **, int *, int,
		char **, int *, int, char **, int);

/* Aux. routines */
char **kn_read_asserts(char *, int, int *);
int    kn_keycompare(void *, void *, int);
void  *kn_get_authorizer(int, int, int *);
struct keynote_keylist *kn_get_licensees(int, int);

/* ASCII-encoding API */
int    kn_encode_base64(unsigned char const *, unsigned int, char *,
			unsigned int);
int    kn_decode_base64(char const *, unsigned char *, unsigned int);
int    kn_encode_hex(unsigned char *, char **, int);
int    kn_decode_hex(char *, char **);

/* Key-encoding API */
int    kn_decode_key(struct keynote_deckey *, char *, int);
char  *kn_encode_key(struct keynote_deckey *, int, int, int);

/* Crypto API */
char  *kn_sign_assertion(char *, int, char *, char *, int);
int    kn_verify_assertion(char *, int);
__END_DECLS
#endif /* __KEYNOTE_H__ */
