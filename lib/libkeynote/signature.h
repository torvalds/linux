/* $OpenBSD: signature.h,v 1.5 2001/09/03 20:14:51 deraadt Exp $ */
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

#ifndef __SIGNATURE_H__
#define __SIGNATURE_H__

#define KEYNOTE_HASH_NONE            0
#define KEYNOTE_HASH_SHA1            1
#define KEYNOTE_HASH_MD5             2

#define DSA_HEX                    "dsa-hex:"
#define DSA_HEX_LEN 	           strlen(DSA_HEX)
#define DSA_BASE64           	   "dsa-base64:"
#define DSA_BASE64_LEN             strlen(DSA_BASE64)
#define RSA_PKCS1_HEX              "rsa-hex:"
#define RSA_PKCS1_HEX_LEN          strlen(RSA_PKCS1_HEX)
#define RSA_PKCS1_BASE64       	   "rsa-base64:"
#define RSA_PKCS1_BASE64_LEN       strlen(RSA_PKCS1_BASE64)
#define ELGAMAL_HEX                "elgamal-hex:"
#define ELGAMAL_HEX_LEN            strlen(ELGAMAL_HEX)
#define ELGAMAL_BASE64             "elgamal-base64:"
#define ELGAMAL_BASE64_LEN         strlen(ELGAMAL_BASE64)
#define PGP_NATIVE                 "pgp:"
#define PGP_NATIVE_LEN             strlen(PGP_NATIVE)
#define BINARY_BASE64              "binary-base64:"
#define BINARY_BASE64_LEN          strlen(BINARY_BASE64)
#define BINARY_HEX                 "binary-hex:"
#define BINARY_HEX_LEN             strlen(BINARY_HEX)
#define X509_BASE64		   "x509-base64:"
#define X509_BASE64_LEN		   strlen(X509_BASE64)
#define X509_HEX		   "x509-hex:"
#define X509_HEX_LEN		   strlen(X509_HEX)

#define KEYNOTE_PRIVATE_KEY_PREFIX     "private-"
#define KEYNOTE_PRIVATE_KEY_PREFIX_LEN strlen(KEYNOTE_PRIVATE_KEY_PREFIX)

#define LARGEST_HASH_SIZE          20 /* In bytes, length of SHA1 hash */
#endif /* __SIGNATURE_H__ */
