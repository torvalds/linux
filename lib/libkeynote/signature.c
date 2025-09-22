/* $OpenBSD: signature.c,v 1.30 2022/11/30 10:40:23 bluhm Exp $ */
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

/*
 * Support for X509 keys and signing added by Ben Laurie <ben@algroup.co.uk>
 * 3 May 1999
 */

#include <sys/types.h>

#include <limits.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <openssl/dsa.h>
#include <openssl/md5.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include "keynote.h"
#include "assertion.h"
#include "signature.h"

static const char hextab[] = {
     '0', '1', '2', '3', '4', '5', '6', '7',
     '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
}; 

/*
 * Actual conversion to hex.
 */   
static void
bin2hex(unsigned char *data, unsigned char *buffer, int len)
{
    int off = 0;
     
    while(len > 0) 
    {
	buffer[off++] = hextab[*data >> 4];
	buffer[off++] = hextab[*data & 0xF];
	data++;
	len--;
    }
}

/*
 * Encode a binary string with hex encoding. Return 0 on success.
 */
int
kn_encode_hex(unsigned char *buf, char **dest, int len)
{
    keynote_errno = 0;
    if (dest == NULL)
    {
	keynote_errno = ERROR_SYNTAX;
	return -1;
    }

    *dest = calloc(2 * len + 1, sizeof(char));
    if (*dest == NULL)
    {
	keynote_errno = ERROR_MEMORY;
	return -1;
    }

    bin2hex(buf, *dest, len);
    return 0;
}

/*
 * Decode a hex encoding. Return 0 on success. The second argument
 * will be half as large as the first.
 */
int
kn_decode_hex(char *hex, char **dest)
{
    int i, decodedlen;
    char ptr[3];

    keynote_errno = 0;
    if (dest == NULL)
    {
	keynote_errno = ERROR_SYNTAX;
	return -1;
    }

    if (strlen(hex) % 2)			/* Should be even */
    {
	keynote_errno = ERROR_SYNTAX;
	return -1;
    }

    decodedlen = strlen(hex) / 2;
    *dest = calloc(decodedlen, sizeof(char));
    if (*dest == NULL)
    {
	keynote_errno = ERROR_MEMORY;
	return -1;
    }

    ptr[2] = '\0';
    for (i = 0; i < decodedlen; i++)
    {
	ptr[0] = hex[2 * i];
	ptr[1] = hex[(2 * i) + 1];
      	(*dest)[i] = (unsigned char) strtoul(ptr, NULL, 16);
    }

    return 0;
}

void
keynote_free_key(void *key, int type)
{
    if (key == NULL)
      return;

    /* DSA keys */
    if (type == KEYNOTE_ALGORITHM_DSA)
    {
	DSA_free(key);
	return;
    }

    /* RSA keys */
    if (type == KEYNOTE_ALGORITHM_RSA)
    {
	RSA_free(key);
	return;
    }

    /* X509 keys */
    if (type == KEYNOTE_ALGORITHM_X509)
    {
	RSA_free(key); /* RSA-specific */
	return;
    }

    /* BINARY keys */
    if (type == KEYNOTE_ALGORITHM_BINARY)
    {
	free(((struct keynote_binary *) key)->bn_key);
	free(key);
	return;
    }

    /* Catch-all case */
    if (type == KEYNOTE_ALGORITHM_NONE)
      free(key);
}

/*
 * Map a signature to an algorithm. Return algorithm number (defined in
 * keynote.h), or KEYNOTE_ALGORITHM_NONE if unknown.
 * Also return in the second, third and fourth arguments the digest
 * algorithm, ASCII and internal encodings respectively.
 */
static int
keynote_get_sig_algorithm(char *sig, int *hash, int *enc, int *internal)
{
    if (sig == NULL)
      return KEYNOTE_ALGORITHM_NONE;

    if (!strncasecmp(SIG_DSA_SHA1_HEX, sig, SIG_DSA_SHA1_HEX_LEN))
    {
	*hash = KEYNOTE_HASH_SHA1;
	*enc = ENCODING_HEX;
	*internal = INTERNAL_ENC_ASN1;
	return KEYNOTE_ALGORITHM_DSA;
    }

    if (!strncasecmp(SIG_DSA_SHA1_BASE64, sig, SIG_DSA_SHA1_BASE64_LEN))
    {
	*hash = KEYNOTE_HASH_SHA1;
	*enc = ENCODING_BASE64;
	*internal = INTERNAL_ENC_ASN1;
	return KEYNOTE_ALGORITHM_DSA;
    }

    if (!strncasecmp(SIG_RSA_MD5_PKCS1_HEX, sig, SIG_RSA_MD5_PKCS1_HEX_LEN))
    {
	*hash = KEYNOTE_HASH_MD5;
	*enc = ENCODING_HEX;
	*internal = INTERNAL_ENC_PKCS1;
	return KEYNOTE_ALGORITHM_RSA;
    }

    if (!strncasecmp(SIG_RSA_SHA1_PKCS1_HEX, sig, SIG_RSA_SHA1_PKCS1_HEX_LEN))
    {
	*hash = KEYNOTE_HASH_SHA1;
	*enc = ENCODING_HEX;
	*internal = INTERNAL_ENC_PKCS1;
	return KEYNOTE_ALGORITHM_RSA;
    }

    if (!strncasecmp(SIG_RSA_MD5_PKCS1_BASE64, sig,
                     SIG_RSA_MD5_PKCS1_BASE64_LEN))
    {
	*hash = KEYNOTE_HASH_MD5;
	*enc = ENCODING_BASE64;
	*internal = INTERNAL_ENC_PKCS1;
	return KEYNOTE_ALGORITHM_RSA;
    }

    if (!strncasecmp(SIG_RSA_SHA1_PKCS1_BASE64, sig,
                     SIG_RSA_SHA1_PKCS1_BASE64_LEN))
    {
	*hash = KEYNOTE_HASH_SHA1;
	*enc = ENCODING_BASE64;
	*internal = INTERNAL_ENC_PKCS1;
	return KEYNOTE_ALGORITHM_RSA;
    }

    if (!strncasecmp(SIG_X509_SHA1_BASE64, sig, SIG_X509_SHA1_BASE64_LEN))
    {
	*hash = KEYNOTE_HASH_SHA1;
	*enc = ENCODING_BASE64;
	*internal = INTERNAL_ENC_ASN1;
	return KEYNOTE_ALGORITHM_X509;
    }

    if (!strncasecmp(SIG_X509_SHA1_HEX, sig, SIG_X509_SHA1_HEX_LEN))
    {
	*hash = KEYNOTE_HASH_SHA1;
	*enc = ENCODING_HEX;
	*internal = INTERNAL_ENC_ASN1;
	return KEYNOTE_ALGORITHM_X509;
    }

    *hash = KEYNOTE_HASH_NONE;
    *enc = ENCODING_NONE;
    *internal = INTERNAL_ENC_NONE;
    return KEYNOTE_ALGORITHM_NONE;
}

/*
 * Map a key to an algorithm. Return algorithm number (defined in
 * keynote.h), or KEYNOTE_ALGORITHM_NONE if unknown. 
 * This latter is also a valid algorithm (for logical tags). Also return
 * in the second and third arguments the ASCII and internal encodings.
 */
int
keynote_get_key_algorithm(char *key, int *encoding, int *internalencoding)
{
    if (!strncasecmp(DSA_HEX, key, DSA_HEX_LEN))
    {
	*internalencoding = INTERNAL_ENC_ASN1;
	*encoding = ENCODING_HEX;
	return KEYNOTE_ALGORITHM_DSA;
    }

    if (!strncasecmp(DSA_BASE64, key, DSA_BASE64_LEN))
    {
	*internalencoding = INTERNAL_ENC_ASN1;
	*encoding = ENCODING_BASE64;
	return KEYNOTE_ALGORITHM_DSA;
    }

    if (!strncasecmp(RSA_PKCS1_HEX, key, RSA_PKCS1_HEX_LEN))
    {
	*internalencoding = INTERNAL_ENC_PKCS1;
	*encoding = ENCODING_HEX;
	return KEYNOTE_ALGORITHM_RSA;
    }

    if (!strncasecmp(RSA_PKCS1_BASE64, key, RSA_PKCS1_BASE64_LEN))
    {
	*internalencoding = INTERNAL_ENC_PKCS1;
	*encoding = ENCODING_BASE64;
	return KEYNOTE_ALGORITHM_RSA;
    }

    if (!strncasecmp(X509_BASE64, key, X509_BASE64_LEN))
    {
	*internalencoding = INTERNAL_ENC_ASN1;
	*encoding = ENCODING_BASE64;
	return KEYNOTE_ALGORITHM_X509;
    }

    if (!strncasecmp(X509_HEX, key, X509_HEX_LEN))
    {
	*internalencoding = INTERNAL_ENC_ASN1;
	*encoding = ENCODING_HEX;
	return KEYNOTE_ALGORITHM_X509;
    }

    if (!strncasecmp(BINARY_HEX, key, BINARY_HEX_LEN))
    {
	*internalencoding = INTERNAL_ENC_NONE;
	*encoding = ENCODING_HEX;
	return KEYNOTE_ALGORITHM_BINARY;
    }
    
    if (!strncasecmp(BINARY_BASE64, key, BINARY_BASE64_LEN))
    {
	*internalencoding = INTERNAL_ENC_NONE;
	*encoding = ENCODING_BASE64;
	return KEYNOTE_ALGORITHM_BINARY;
    }
    
    *internalencoding = INTERNAL_ENC_NONE;
    *encoding = ENCODING_NONE;
    return KEYNOTE_ALGORITHM_NONE;
}

/*
 * Same as keynote_get_key_algorithm(), only verify that this is
 * a private key (just look at the prefix).
 */
static int
keynote_get_private_key_algorithm(char *key, int *encoding,
				  int *internalencoding)
{
    if (strncasecmp(KEYNOTE_PRIVATE_KEY_PREFIX, key, 
		    KEYNOTE_PRIVATE_KEY_PREFIX_LEN))
    {
	*internalencoding = INTERNAL_ENC_NONE;
	*encoding = ENCODING_NONE;
	return KEYNOTE_ALGORITHM_NONE;
    }

    return keynote_get_key_algorithm(key + KEYNOTE_PRIVATE_KEY_PREFIX_LEN,
				     encoding, internalencoding);
}

/*
 * Decode a string to a key. Return 0 on success.
 */
int
kn_decode_key(struct keynote_deckey *dc, char *key, int keytype)
{
    X509 *px509Cert;
    EVP_PKEY *pPublicKey;
    unsigned char *ptr = NULL, *decoded = NULL;
    int encoding, internalencoding;
    long len = 0;

    keynote_errno = 0;
    if (keytype == KEYNOTE_PRIVATE_KEY)
      dc->dec_algorithm = keynote_get_private_key_algorithm(key, &encoding,
							    &internalencoding);
    else
      dc->dec_algorithm = keynote_get_key_algorithm(key, &encoding,
						    &internalencoding);
    if (dc->dec_algorithm == KEYNOTE_ALGORITHM_NONE)
    {
	if ((dc->dec_key = strdup(key)) == NULL) {
	    keynote_errno = ERROR_MEMORY;
	    return -1;
	}

	return 0;
    }

    key = strchr(key, ':'); /* Move forward, to the Encoding. We're guaranteed
			    * to have a ':' character, since this is a key */
    key++;

    /* Remove ASCII encoding */
    switch (encoding)
    {
	case ENCODING_NONE:
	    break;

	case ENCODING_HEX:
            len = strlen(key) / 2;
	    if (kn_decode_hex(key, (char **) &decoded) != 0)
	      return -1;
	    ptr = decoded;
	    break;

	case ENCODING_BASE64:
	    len = strlen(key);
	    if (len % 4)  /* Base64 encoding must be a multiple of 4 */
	    {
		keynote_errno = ERROR_SYNTAX;
		return -1;
	    }

	    len = 3 * (len / 4);
	    decoded = calloc(len, sizeof(unsigned char));
	    ptr = decoded;
	    if (decoded == NULL) {
		keynote_errno = ERROR_MEMORY;
		return -1;
	    }

	    if ((len = kn_decode_base64(key, decoded, len)) == -1)
	      return -1;
	    break;

	case ENCODING_NATIVE:
	    decoded = strdup(key);
	    if (decoded == NULL) {
		keynote_errno = ERROR_MEMORY;
		return -1;
	    }
	    len = strlen(key);
	    ptr = decoded;
	    break;

	default:
	    keynote_errno = ERROR_SYNTAX;
	    return -1;
    }

    /* DSA-HEX */
    if ((dc->dec_algorithm == KEYNOTE_ALGORITHM_DSA) &&
	(internalencoding == INTERNAL_ENC_ASN1))
    {
	if (keytype == KEYNOTE_PRIVATE_KEY)
	{
	    if ((dc->dec_key =
		d2i_DSAPrivateKey(NULL, (const unsigned char **) &decoded, len))
		== NULL)
	    {
		free(ptr);
		keynote_errno = ERROR_SYNTAX; /* Could be a memory error */
		return -1;
	    }
	}
	else
	{
	    if ((dc->dec_key =
		d2i_DSAPublicKey(NULL, (const unsigned char **) &decoded, len))
		== NULL)
	    {
		free(ptr);
		keynote_errno = ERROR_SYNTAX; /* Could be a memory error */
		return -1;
	    }
	}

	free(ptr);

	return 0;
    }

    /* RSA-PKCS1-HEX */
    if ((dc->dec_algorithm == KEYNOTE_ALGORITHM_RSA) &&
        (internalencoding == INTERNAL_ENC_PKCS1))
    {
        if (keytype == KEYNOTE_PRIVATE_KEY)
        {
            if ((dc->dec_key =
		d2i_RSAPrivateKey(NULL, (const unsigned char **) &decoded, len))
		== NULL)
	    {
                free(ptr);
                keynote_errno = ERROR_SYNTAX; /* Could be a memory error */
                return -1;
            }
	    if (RSA_blinding_on(dc->dec_key, NULL) != 1) {
                free(ptr);
                RSA_free(dc->dec_key);
                keynote_errno = ERROR_MEMORY;
                return -1;
	    }
        }
        else
        {
            if ((dc->dec_key =
		d2i_RSAPublicKey(NULL, (const unsigned char **) &decoded, len))
		== NULL)
	    {
                free(ptr);
                keynote_errno = ERROR_SYNTAX; /* Could be a memory error */
                return -1;
            }
        }

        free(ptr);

        return 0;
    }

    /* X509 Cert */
    if ((dc->dec_algorithm == KEYNOTE_ALGORITHM_X509) &&
	(internalencoding == INTERNAL_ENC_ASN1) &&
	(keytype == KEYNOTE_PUBLIC_KEY))
    {
	if((px509Cert =
	    d2i_X509(NULL, (const unsigned char **)&decoded, len)) == NULL)
	{
	    free(ptr);
	    keynote_errno = ERROR_SYNTAX;
	    return -1;
	}

	if ((pPublicKey = X509_get0_pubkey(px509Cert)) == NULL) {
	    free(ptr);
	    X509_free(px509Cert);
	    keynote_errno = ERROR_SYNTAX;
	    return -1;
	}

	/* RSA-specific */
	dc->dec_key = EVP_PKEY_get0_RSA(pPublicKey);
	RSA_up_ref(dc->dec_key);

	free(ptr);
	X509_free(px509Cert);
	return 0;
    }

    /* BINARY keys */
    if ((dc->dec_algorithm == KEYNOTE_ALGORITHM_BINARY) &&
	(internalencoding == INTERNAL_ENC_NONE))
    {
	dc->dec_key = calloc(1, sizeof(struct keynote_binary));
	if (dc->dec_key == NULL)
	{
	    keynote_errno = ERROR_MEMORY;
	    return -1;
	}

	((struct keynote_binary *) dc->dec_key)->bn_key = decoded;
	((struct keynote_binary *) dc->dec_key)->bn_len = len;
	return RESULT_TRUE;
    }

    /* Add support for more algorithms here */

    free(ptr);

    /* This shouldn't ever be reached really */
    keynote_errno = ERROR_SYNTAX;
    return -1;
}

/*
 * Compare two keys for equality. Return RESULT_TRUE if equal,
 * RESULT_FALSE otherwise.
 */
int
kn_keycompare(void *key1, void *key2, int algorithm)
{
    DSA *p1, *p2;
    RSA *p3, *p4;
    struct keynote_binary *bn1, *bn2;

    if (key1 == NULL || key2 == NULL)
      return RESULT_FALSE;

    switch (algorithm)
    {
	case KEYNOTE_ALGORITHM_NONE:
	    if (!strcmp(key1, key2))
	      return RESULT_TRUE;
	    else
	      return RESULT_FALSE;
	    
	case KEYNOTE_ALGORITHM_DSA:
	    p1 = (DSA *) key1;
	    p2 = (DSA *) key2;
	    if (!BN_cmp(DSA_get0_p(p1), DSA_get0_p(p2)) &&
		!BN_cmp(DSA_get0_q(p1), DSA_get0_q(p2)) &&
		!BN_cmp(DSA_get0_g(p1), DSA_get0_g(p2)) &&
		!BN_cmp(DSA_get0_pub_key(p1), DSA_get0_pub_key(p2)))
	      return RESULT_TRUE;
	    else
	      return RESULT_FALSE;

	case KEYNOTE_ALGORITHM_X509:
            p3 = (RSA *) key1;
            p4 = (RSA *) key2;
            if (!BN_cmp(RSA_get0_n(p3), RSA_get0_n(p4)) &&
                !BN_cmp(RSA_get0_e(p3), RSA_get0_e(p4)))
              return RESULT_TRUE;
            else
	      return RESULT_FALSE;

	case KEYNOTE_ALGORITHM_RSA:
            p3 = (RSA *) key1;
            p4 = (RSA *) key2;
            if (!BN_cmp(RSA_get0_n(p3), RSA_get0_n(p4)) &&
                !BN_cmp(RSA_get0_e(p3), RSA_get0_e(p4)))
              return RESULT_TRUE;
            else
	      return RESULT_FALSE;

	case KEYNOTE_ALGORITHM_ELGAMAL:
	    /* Not supported yet */
	    return RESULT_FALSE;

	case KEYNOTE_ALGORITHM_PGP:
	    /* Not supported yet */
	    return RESULT_FALSE;

	case KEYNOTE_ALGORITHM_BINARY:
	    bn1 = (struct keynote_binary *) key1;
	    bn2 = (struct keynote_binary *) key2;
	    if ((bn1->bn_len == bn2->bn_len) &&
		!memcmp(bn1->bn_key, bn2->bn_key, bn1->bn_len))
	      return RESULT_TRUE;
	    else
	      return RESULT_FALSE;

	default:
	    return RESULT_FALSE;
    }
}

/*
 * Verify the signature on an assertion; return SIGRESULT_TRUE is
 * success, SIGRESULT_FALSE otherwise.
 */
int
keynote_sigverify_assertion(struct assertion *as)
{
    int hashtype, enc, intenc, alg = KEYNOTE_ALGORITHM_NONE, hashlen = 0;
    unsigned char *sig, *decoded = NULL, *ptr;
    unsigned char res2[20];
    SHA_CTX shscontext;
    MD5_CTX md5context;
    int len = 0;
    DSA *dsa;
    RSA *rsa;
    if (as->as_signature == NULL ||
	as->as_startofsignature == NULL ||
	as->as_allbutsignature == NULL ||
	as->as_allbutsignature - as->as_startofsignature <= 0)
      return SIGRESULT_FALSE;

    alg = keynote_get_sig_algorithm(as->as_signature, &hashtype, &enc,
				    &intenc);
    if (alg == KEYNOTE_ALGORITHM_NONE)
      return SIGRESULT_FALSE;

    /* Check for matching algorithms */
    if ((alg != as->as_signeralgorithm) &&
	!((alg == KEYNOTE_ALGORITHM_RSA) &&
	  (as->as_signeralgorithm == KEYNOTE_ALGORITHM_X509)) &&
	!((alg == KEYNOTE_ALGORITHM_X509) &&
	  (as->as_signeralgorithm == KEYNOTE_ALGORITHM_RSA)))
      return SIGRESULT_FALSE;

    sig = strchr(as->as_signature, ':');   /* Move forward to the Encoding. We
					   * are guaranteed to have a ':'
					   * character, since this is a valid
					   * signature */
    sig++;

    switch (hashtype)
    {
	case KEYNOTE_HASH_SHA1:
	    hashlen = 20;
	    memset(res2, 0, hashlen);
	    SHA1_Init(&shscontext);
	    SHA1_Update(&shscontext, as->as_startofsignature,
			as->as_allbutsignature - as->as_startofsignature);
	    SHA1_Update(&shscontext, as->as_signature, 
			(char *) sig - as->as_signature);
	    SHA1_Final(res2, &shscontext);
	    break;
	    
	case KEYNOTE_HASH_MD5:
	    hashlen = 16;
	    memset(res2, 0, hashlen);
	    MD5_Init(&md5context);
	    MD5_Update(&md5context, as->as_startofsignature,
		       as->as_allbutsignature - as->as_startofsignature);
	    MD5_Update(&md5context, as->as_signature,
		       (char *) sig - as->as_signature);
	    MD5_Final(res2, &md5context);
	    break;

	case KEYNOTE_HASH_NONE:
	    break;
    }

    /* Remove ASCII encoding */
    switch (enc)
    {
	case ENCODING_NONE:
	    ptr = NULL;
	    break;

	case ENCODING_HEX:
	    len = strlen(sig) / 2;
	    if (kn_decode_hex(sig, (char **) &decoded) != 0)
	      return -1;
	    ptr = decoded;
	    break;

	case ENCODING_BASE64:
	    len = strlen(sig);
	    if (len % 4)  /* Base64 encoding must be a multiple of 4 */
	    {
		keynote_errno = ERROR_SYNTAX;
		return -1;
	    }

	    len = 3 * (len / 4);
	    decoded = calloc(len, sizeof(unsigned char));
	    ptr = decoded;
	    if (decoded == NULL) {
		keynote_errno = ERROR_MEMORY;
		return -1;
	    }

	    len = kn_decode_base64(sig, decoded, len);
	    if ((len == -1) || (len == 0) || (len == 1))
	      return -1;
	    break;

	case ENCODING_NATIVE:
	    
	    if ((decoded = strdup(sig)) == NULL) {
		keynote_errno = ERROR_MEMORY;
		return -1;
	    }
	    len = strlen(sig);
	    ptr = decoded;
	    break;

	default:
	    keynote_errno = ERROR_SYNTAX;
	    return -1;
    }

    /* DSA */
    if ((alg == KEYNOTE_ALGORITHM_DSA) && (intenc == INTERNAL_ENC_ASN1))
    {
	dsa = (DSA *) as->as_authorizer;
	if (DSA_verify(0, res2, hashlen, decoded, len, dsa) == 1) {
	    free(ptr);
	    return SIGRESULT_TRUE;
	}
    }
    else /* RSA */
      if ((alg == KEYNOTE_ALGORITHM_RSA) && (intenc == INTERNAL_ENC_PKCS1))
      {
          rsa = (RSA *) as->as_authorizer;
          if (RSA_verify_ASN1_OCTET_STRING(RSA_PKCS1_PADDING, res2, hashlen,
					   decoded, len, rsa) == 1) {
              free(ptr);
              return SIGRESULT_TRUE;
          }
      }
      else
	if ((alg == KEYNOTE_ALGORITHM_X509) && (intenc == INTERNAL_ENC_ASN1))
	{
	    /* RSA-specific */
	    rsa = (RSA *) as->as_authorizer;
	    if (RSA_verify(NID_shaWithRSAEncryption, res2, hashlen, decoded,
			   len, rsa) == 1) {
		free(ptr);
		return SIGRESULT_TRUE;
	    }
	}
    
    /* Handle more algorithms here */
    
    free(ptr);

    return SIGRESULT_FALSE;
}

/*
 * Sign an assertion.
 */
static char *
keynote_sign_assertion(struct assertion *as, char *sigalg, void *key,
		       int keyalg, int verifyflag)
{
    int slen, i, hashlen = 0, hashtype, alg, encoding, internalenc;
    unsigned char *sig = NULL, *finalbuf = NULL;
    unsigned char res2[LARGEST_HASH_SIZE], *sbuf = NULL;
    BIO *biokey = NULL;
    DSA *dsa = NULL;
    RSA *rsa = NULL;
    SHA_CTX shscontext;
    MD5_CTX md5context;
    int len;

    if (as->as_signature_string_s == NULL ||
	as->as_startofsignature == NULL ||
	as->as_allbutsignature == NULL ||
	as->as_allbutsignature - as->as_startofsignature <= 0 ||
	as->as_authorizer == NULL ||
	key == NULL ||
	as->as_signeralgorithm == KEYNOTE_ALGORITHM_NONE)
    {
	keynote_errno = ERROR_SYNTAX;
	return NULL;
    }

    alg = keynote_get_sig_algorithm(sigalg, &hashtype, &encoding,
				    &internalenc);
    if (((alg != as->as_signeralgorithm) &&
	 !((alg == KEYNOTE_ALGORITHM_RSA) &&
	   (as->as_signeralgorithm == KEYNOTE_ALGORITHM_X509)) &&
	 !((alg == KEYNOTE_ALGORITHM_X509) &&
	   (as->as_signeralgorithm == KEYNOTE_ALGORITHM_RSA))) ||
        ((alg != keyalg) &&
	 !((alg == KEYNOTE_ALGORITHM_RSA) &&
	   (keyalg == KEYNOTE_ALGORITHM_X509)) &&
	 !((alg == KEYNOTE_ALGORITHM_X509) &&
	   (keyalg == KEYNOTE_ALGORITHM_RSA))))
    {
	keynote_errno = ERROR_SYNTAX;
	return NULL;
    }

    sig = strchr(sigalg, ':');
    if (sig == NULL)
    {
	keynote_errno = ERROR_SYNTAX;
	return NULL;
    }

    sig++;

    switch (hashtype)
    {
	case KEYNOTE_HASH_SHA1:
    	    hashlen = 20;
	    memset(res2, 0, hashlen);
	    SHA1_Init(&shscontext);
	    SHA1_Update(&shscontext, as->as_startofsignature,
			as->as_allbutsignature - as->as_startofsignature);
	    SHA1_Update(&shscontext, sigalg, (char *) sig - sigalg);
	    SHA1_Final(res2, &shscontext);
	    break;
   
	case KEYNOTE_HASH_MD5:
	    hashlen = 16;
	    memset(res2, 0, hashlen);
	    MD5_Init(&md5context);
	    MD5_Update(&md5context, as->as_startofsignature,
		       as->as_allbutsignature - as->as_startofsignature);
	    MD5_Update(&md5context, sigalg, (char *) sig - sigalg);
	    MD5_Final(res2, &md5context);
	    break;

	case KEYNOTE_HASH_NONE:
	    break;
    }

    if ((alg == KEYNOTE_ALGORITHM_DSA) &&
	(hashtype == KEYNOTE_HASH_SHA1) &&
	(internalenc == INTERNAL_ENC_ASN1) &&
	((encoding == ENCODING_HEX) || (encoding == ENCODING_BASE64)))
    {
	dsa = (DSA *) key;
	sbuf = calloc(DSA_size(dsa), sizeof(unsigned char));
	if (sbuf == NULL)
	{
	    keynote_errno = ERROR_MEMORY;
	    return NULL;
	}

	if (DSA_sign(0, res2, hashlen, sbuf, &slen, dsa) <= 0)
	{
	    free(sbuf);
	    keynote_errno = ERROR_SYNTAX;
	    return NULL;
	}
    }
    else
      if ((alg == KEYNOTE_ALGORITHM_RSA) &&
          ((hashtype == KEYNOTE_HASH_SHA1) ||
           (hashtype == KEYNOTE_HASH_MD5)) &&
          (internalenc == INTERNAL_ENC_PKCS1) &&
          ((encoding == ENCODING_HEX) || (encoding == ENCODING_BASE64)))
      {
          rsa = (RSA *) key;
          sbuf = calloc(RSA_size(rsa), sizeof(unsigned char));
          if (sbuf == NULL)
          {
              keynote_errno = ERROR_MEMORY;
              return NULL;
          }

          if (RSA_sign_ASN1_OCTET_STRING(RSA_PKCS1_PADDING, res2, hashlen,
					 sbuf, &slen, rsa) <= 0)
          {
              free(sbuf);
              keynote_errno = ERROR_SYNTAX;
              return NULL;
          }
      }
    else
      if ((alg == KEYNOTE_ALGORITHM_X509) &&
	  (hashtype == KEYNOTE_HASH_SHA1) &&
	  (internalenc == INTERNAL_ENC_ASN1))
      {
	  if ((biokey = BIO_new(BIO_s_mem())) == NULL)
	  {
	      keynote_errno = ERROR_SYNTAX;
	      return NULL;
	  }
	  
	  if (BIO_write(biokey, key, strlen(key) + 1) <= 0)
	  {
	      BIO_free(biokey);
	      keynote_errno = ERROR_SYNTAX;
	      return NULL;
	  }

	  /* RSA-specific */
	  rsa = (RSA *) PEM_read_bio_RSAPrivateKey(biokey, NULL, NULL, NULL);
	  if (rsa == NULL)
	  {
	      BIO_free(biokey);
	      keynote_errno = ERROR_SYNTAX;
	      return NULL;
	  }

	  sbuf = calloc(RSA_size(rsa), sizeof(char));
	  if (sbuf == NULL)
	  {
	      BIO_free(biokey);
	      RSA_free(rsa);
	      keynote_errno = ERROR_MEMORY;
	      return NULL;
	  }

	  if (RSA_sign(NID_shaWithRSAEncryption, res2, hashlen, sbuf, &slen,
		       rsa) <= 0)
          {
	      BIO_free(biokey);
	      RSA_free(rsa);
	      free(sbuf);
	      keynote_errno = ERROR_SIGN_FAILURE;
	      return NULL;
	  }

	  BIO_free(biokey);
	  RSA_free(rsa);
      }
      else /* Other algorithms here */
      {
	  keynote_errno = ERROR_SYNTAX;
	  return NULL;
      }

    /* ASCII encoding */
    switch (encoding)
    {
	case ENCODING_HEX:
	    i = kn_encode_hex(sbuf, (char **) &finalbuf, slen);
	    free(sbuf);
	    if (i != 0)
	      return NULL;
	    break;

	case ENCODING_BASE64:
	    finalbuf = calloc(2 * slen, sizeof(unsigned char));
	    if (finalbuf == NULL)
	    {
		keynote_errno = ERROR_MEMORY;
		free(sbuf);
		return NULL;
	    }

	    slen = kn_encode_base64(sbuf, slen, finalbuf, 2 * slen);
	    free(sbuf);
	    if (slen == -1) {
	      free(finalbuf);
	      return NULL;
	    }
	    break;

	default:
	    free(sbuf);
	    keynote_errno = ERROR_SYNTAX;
	    return NULL;
    }

    /* Replace as->as_signature */
    len = strlen(sigalg) + strlen(finalbuf) + 1;
    as->as_signature = calloc(len, sizeof(char));
    if (as->as_signature == NULL)
    {
	free(finalbuf);
	keynote_errno = ERROR_MEMORY;
	return NULL;
    }

    /* Concatenate algorithm name and signature value */
    snprintf(as->as_signature, len, "%s%s", sigalg, finalbuf);
    free(finalbuf);
    finalbuf = as->as_signature;

    /* Verify the newly-created signature if requested */
    if (verifyflag)
    {
	/* Do the signature verification */
	if (keynote_sigverify_assertion(as) != SIGRESULT_TRUE)
	{
	    as->as_signature = NULL;
	    free(finalbuf);
	    if (keynote_errno == 0)
	      keynote_errno = ERROR_SYNTAX;
	    return NULL;
	}

	as->as_signature = NULL;
    }
    else
      as->as_signature = NULL;

    /* Everything ok */
    return (char *) finalbuf;
}

/*
 * Verify the signature on an assertion.
 */
int
kn_verify_assertion(char *buf, int len)
{
    struct assertion *as;
    int res;

    keynote_errno = 0;
    as = keynote_parse_assertion(buf, len, ASSERT_FLAG_SIGVER);
    if (as == NULL)
      return -1;

    res = keynote_sigverify_assertion(as);
    keynote_free_assertion(as);
    return res;
}

/*
 * Produce the signature for an assertion.
 */
char *
kn_sign_assertion(char *buf, int buflen, char *key, char *sigalg, int vflag)
{
    int i, alg, hashtype, encoding, internalenc;
    struct keynote_deckey dc;
    struct assertion *as;
    char *s, *sig;

    keynote_errno = 0;
    s = NULL;

    if (sigalg == NULL || buf == NULL || key == NULL)
    {
	keynote_errno = ERROR_NOTFOUND;
	return NULL;
    }

    if (sigalg[0] == '\0' || sigalg[strlen(sigalg) - 1] != ':')
    {
	keynote_errno = ERROR_SYNTAX;
	return NULL;
    }

    /* We're using a different format for X509 private keys, so... */
    alg = keynote_get_sig_algorithm(sigalg, &hashtype, &encoding,
				    &internalenc);
    if (alg != KEYNOTE_ALGORITHM_X509)
    {
	/* Parse the private key */
	s = keynote_get_private_key(key);
	if (s == NULL)
	  return NULL;

	/* Decode private key */
	i = kn_decode_key(&dc, s, KEYNOTE_PRIVATE_KEY);
	if (i == -1)
	{
	    free(s);
	    return NULL;
	}
    }
    else /* X509 private key */
    {
	dc.dec_key = key;
	dc.dec_algorithm = alg;
    }

    as = keynote_parse_assertion(buf, buflen, ASSERT_FLAG_SIGGEN);
    if (as == NULL)
    {
	if (alg != KEYNOTE_ALGORITHM_X509)
	{
	    keynote_free_key(dc.dec_key, dc.dec_algorithm);
	    free(s);
	}
	return NULL;
    }

    sig = keynote_sign_assertion(as, sigalg, dc.dec_key, dc.dec_algorithm,
				 vflag);
    if (alg != KEYNOTE_ALGORITHM_X509)
      keynote_free_key(dc.dec_key, dc.dec_algorithm);
    keynote_free_assertion(as);
    if (s != NULL)
      free(s);
    return sig;
}

/*
 * ASCII-encode a key.
 */
char *
kn_encode_key(struct keynote_deckey *dc, int iencoding,
	      int encoding, int keytype)
{
    char *foo, *ptr;
    DSA *dsa;
    RSA *rsa;
    int i;
    struct keynote_binary *bn;
    char *s;

    keynote_errno = 0;
    if (dc == NULL || dc->dec_key == NULL)
    {
	keynote_errno = ERROR_NOTFOUND;
	return NULL;
    }

    /* DSA keys */
    if ((dc->dec_algorithm == KEYNOTE_ALGORITHM_DSA) &&
	(iencoding == INTERNAL_ENC_ASN1) &&
	((encoding == ENCODING_HEX) || (encoding == ENCODING_BASE64)))
    {
	dsa = (DSA *) dc->dec_key;
	if (keytype == KEYNOTE_PUBLIC_KEY)
	  i = i2d_DSAPublicKey(dsa, NULL);
	else
	  i = i2d_DSAPrivateKey(dsa, NULL);

	if (i <= 0)
	{
	    keynote_errno = ERROR_SYNTAX;
	    return NULL;
	}

 	ptr = foo = calloc(i, sizeof(char));
	if (foo == NULL)
	{
	    keynote_errno = ERROR_MEMORY;
	    return NULL;
	}

	if (keytype == KEYNOTE_PUBLIC_KEY)
	  i2d_DSAPublicKey(dsa, (unsigned char **) &foo);
	else
	  i2d_DSAPrivateKey(dsa, (unsigned char **) &foo);

	if (encoding == ENCODING_HEX)
	{
	    if (kn_encode_hex(ptr, &s, i) != 0)
	    {
		free(ptr);
		return NULL;
	    }

	    free(ptr);
	    return s;
	}
	else
	  if (encoding == ENCODING_BASE64)
	  {
	      s = calloc(2 * i, sizeof(char));
	      if (s == NULL)
	      {
		  free(ptr);
		  keynote_errno = ERROR_MEMORY;
		  return NULL;
	      }

	      if (kn_encode_base64(ptr, i, s, 2 * i) == -1)
	      {
		  free(s);
		  free(ptr);
		  return NULL;
	      }

	      free(ptr);
	      return s;
	  }
    }

    /* RSA keys */
    if ((dc->dec_algorithm == KEYNOTE_ALGORITHM_RSA) &&
	(iencoding == INTERNAL_ENC_PKCS1) &&
	((encoding == ENCODING_HEX) || (encoding == ENCODING_BASE64)))
    {
	rsa = (RSA *) dc->dec_key;
	if (keytype == KEYNOTE_PUBLIC_KEY)
	  i = i2d_RSAPublicKey(rsa, NULL);
	else
	  i = i2d_RSAPrivateKey(rsa, NULL);

	if (i <= 0)
	{
	    keynote_errno = ERROR_SYNTAX;
	    return NULL;
	}

	ptr = foo = calloc(i, sizeof(char));
	if (foo == NULL)
	{
	    keynote_errno = ERROR_MEMORY;
	    return NULL;
	}

	if (keytype == KEYNOTE_PUBLIC_KEY)
	  i2d_RSAPublicKey(rsa, (unsigned char **) &foo);
	else
	  i2d_RSAPrivateKey(rsa, (unsigned char **) &foo);

	if (encoding == ENCODING_HEX)
	{
	    if (kn_encode_hex(ptr, &s, i) != 0)
	    {
		free(ptr);
		return NULL;
	    }

	    free(ptr);
	    return s;
	}
	else
	  if (encoding == ENCODING_BASE64)
	  {
	      s = calloc(2 * i, sizeof(char));
	      if (s == NULL)
	      {
		  free(ptr);
		  keynote_errno = ERROR_MEMORY;
		  return NULL;
	      }

	      if (kn_encode_base64(ptr, i, s, 2 * i) == -1)
	      {
		  free(s);
		  free(ptr);
		  return NULL;
	      }

	      free(ptr);
	      return s;
	  }
    }

    /* BINARY keys */
    if ((dc->dec_algorithm == KEYNOTE_ALGORITHM_BINARY) &&
	(iencoding == INTERNAL_ENC_NONE) &&
	((encoding == ENCODING_HEX) || (encoding == ENCODING_BASE64)))
    {
	bn = (struct keynote_binary *) dc->dec_key;

	if (encoding == ENCODING_HEX)
	{
	    if (kn_encode_hex(bn->bn_key, &s, bn->bn_len) != 0)
	      return NULL;

	    return s;
	}
	else
	  if (encoding == ENCODING_BASE64)
	  {
	      s = calloc(2 * bn->bn_len, sizeof(char));
	      if (s == NULL)
	      {
		  keynote_errno = ERROR_MEMORY;
		  return NULL;
	      }

	      if (kn_encode_base64(bn->bn_key, bn->bn_len, s,
				   2 * bn->bn_len) == -1)
	      {
		  free(s);
		  return NULL;
	      }

	      return s;
	  }
    }

    keynote_errno = ERROR_NOTFOUND;
    return NULL;
}
