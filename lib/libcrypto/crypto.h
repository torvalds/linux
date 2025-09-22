/* $OpenBSD: crypto.h,v 1.79 2025/03/09 15:29:56 tb Exp $ */
/* ====================================================================
 * Copyright (c) 1998-2006 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
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
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * ECDH support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef HEADER_CRYPTO_H
#define HEADER_CRYPTO_H

#include <openssl/opensslconf.h>

#include <openssl/stack.h>
#include <openssl/safestack.h>
#include <openssl/opensslv.h>
#include <openssl/ossl_typ.h>

#ifdef  __cplusplus
extern "C" {
#endif

/* Backward compatibility to SSLeay */
/* This is more to be used to check the correct DLL is being used
 * in the MS world. */
#define SSLEAY_VERSION_NUMBER	OPENSSL_VERSION_NUMBER
#define SSLEAY_VERSION		0
/* #define SSLEAY_OPTIONS	1 no longer supported */
#define SSLEAY_CFLAGS		2
#define SSLEAY_BUILT_ON		3
#define SSLEAY_PLATFORM		4
#define SSLEAY_DIR		5

/* When changing the CRYPTO_LOCK_* list, be sure to maintain the text lock
 * names in cryptlib.c
 */

#define	CRYPTO_LOCK_ERR			1
#define	CRYPTO_LOCK_EX_DATA		2
#define	CRYPTO_LOCK_X509		3
#define	CRYPTO_LOCK_X509_INFO		4
#define	CRYPTO_LOCK_X509_PKEY		5
#define CRYPTO_LOCK_X509_CRL		6
#define CRYPTO_LOCK_X509_REQ		7
#define CRYPTO_LOCK_DSA			8
#define CRYPTO_LOCK_RSA			9
#define CRYPTO_LOCK_EVP_PKEY		10
#define CRYPTO_LOCK_X509_STORE		11
#define CRYPTO_LOCK_SSL_CTX		12
#define CRYPTO_LOCK_SSL_CERT		13
#define CRYPTO_LOCK_SSL_SESSION		14
#define CRYPTO_LOCK_SSL_SESS_CERT	15
#define CRYPTO_LOCK_SSL			16
#define CRYPTO_LOCK_SSL_METHOD		17
#define CRYPTO_LOCK_RAND		18
#define CRYPTO_LOCK_RAND2		19
#define CRYPTO_LOCK_MALLOC		20
#define CRYPTO_LOCK_BIO			21
#define CRYPTO_LOCK_GETHOSTBYNAME	22
#define CRYPTO_LOCK_GETSERVBYNAME	23
#define CRYPTO_LOCK_READDIR		24
#define CRYPTO_LOCK_RSA_BLINDING	25
#define CRYPTO_LOCK_DH			26
#define CRYPTO_LOCK_MALLOC2		27
#define CRYPTO_LOCK_DSO			28
#define CRYPTO_LOCK_DYNLOCK		29
#define CRYPTO_LOCK_ENGINE		30
#define CRYPTO_LOCK_UI			31
#define CRYPTO_LOCK_ECDSA               32
#define CRYPTO_LOCK_EC			33
#define CRYPTO_LOCK_ECDH		34
#define CRYPTO_LOCK_BN			35
#define CRYPTO_LOCK_EC_PRE_COMP		36
#define CRYPTO_LOCK_STORE		37
#define CRYPTO_LOCK_COMP		38
#define CRYPTO_LOCK_FIPS		39
#define CRYPTO_LOCK_FIPS2		40
#define CRYPTO_NUM_LOCKS		41

#define CRYPTO_LOCK		1
#define CRYPTO_UNLOCK		2
#define CRYPTO_READ		4
#define CRYPTO_WRITE		8

#ifndef CRYPTO_w_lock
#define CRYPTO_w_lock(type)	\
	CRYPTO_lock(CRYPTO_LOCK|CRYPTO_WRITE,type,OPENSSL_FILE,OPENSSL_LINE)
#define CRYPTO_w_unlock(type)	\
	CRYPTO_lock(CRYPTO_UNLOCK|CRYPTO_WRITE,type,OPENSSL_FILE,OPENSSL_LINE)
#define CRYPTO_r_lock(type)	\
	CRYPTO_lock(CRYPTO_LOCK|CRYPTO_READ,type,OPENSSL_FILE,OPENSSL_LINE)
#define CRYPTO_r_unlock(type)	\
	CRYPTO_lock(CRYPTO_UNLOCK|CRYPTO_READ,type,OPENSSL_FILE,OPENSSL_LINE)
#define CRYPTO_add(addr,amount,type)	\
	CRYPTO_add_lock(addr,amount,type,OPENSSL_FILE,OPENSSL_LINE)
#endif

/* Some applications as well as some parts of OpenSSL need to allocate
   and deallocate locks in a dynamic fashion.  The following typedef
   makes this possible in a type-safe manner.  */
/* struct CRYPTO_dynlock_value has to be defined by the application. */
typedef struct {
	int references;
	struct CRYPTO_dynlock_value *data;
} CRYPTO_dynlock;


/* The following can be used to detect memory leaks in the SSLeay library.
 * It used, it turns on malloc checking */

#define CRYPTO_MEM_CHECK_OFF	0x0	/* an enume */
#define CRYPTO_MEM_CHECK_ON	0x1	/* a bit */
#define CRYPTO_MEM_CHECK_ENABLE	0x2	/* a bit */
#define CRYPTO_MEM_CHECK_DISABLE 0x3	/* an enume */

/* The following are bit values to turn on or off options connected to the
 * malloc checking functionality */

/* Adds time to the memory checking information */
#define V_CRYPTO_MDEBUG_TIME	0x1 /* a bit */
/* Adds thread number to the memory checking information */
#define V_CRYPTO_MDEBUG_THREAD	0x2 /* a bit */

#define V_CRYPTO_MDEBUG_ALL (V_CRYPTO_MDEBUG_TIME | V_CRYPTO_MDEBUG_THREAD)


/* predec of the BIO type */
typedef struct bio_st BIO_dummy;

struct crypto_ex_data_st {
	void *sk;
};
DECLARE_STACK_OF(void)

#define CRYPTO_EX_INDEX_SSL              0
#define CRYPTO_EX_INDEX_SSL_CTX          1
#define CRYPTO_EX_INDEX_SSL_SESSION      2
#define CRYPTO_EX_INDEX_APP              3
#define CRYPTO_EX_INDEX_BIO              4
#define CRYPTO_EX_INDEX_DH               5
#define CRYPTO_EX_INDEX_DSA              6
#define CRYPTO_EX_INDEX_EC_KEY           7
#define CRYPTO_EX_INDEX_ENGINE           8
#define CRYPTO_EX_INDEX_RSA              9
#define CRYPTO_EX_INDEX_UI               10
#define CRYPTO_EX_INDEX_UI_METHOD        11
#define CRYPTO_EX_INDEX_X509             12
#define CRYPTO_EX_INDEX_X509_STORE       13
#define CRYPTO_EX_INDEX_X509_STORE_CTX   14
#define CRYPTO_EX_INDEX__COUNT           15

#ifndef LIBRESSL_INTERNAL
#define CRYPTO_malloc_init()		(0)
#define CRYPTO_malloc_debug_init()	(0)
#endif  /* LIBRESSL_INTERNAL */

#if defined CRYPTO_MDEBUG_ALL || defined CRYPTO_MDEBUG_TIME || defined CRYPTO_MDEBUG_THREAD
# ifndef CRYPTO_MDEBUG /* avoid duplicate #define */
#  define CRYPTO_MDEBUG
# endif
#endif

int CRYPTO_mem_ctrl(int mode);

#define OPENSSL_malloc(num)	CRYPTO_malloc((num),OPENSSL_FILE,OPENSSL_LINE)
#define OPENSSL_strdup(str)	CRYPTO_strdup((str),OPENSSL_FILE,OPENSSL_LINE)
#define OPENSSL_free(addr)	CRYPTO_free((addr),OPENSSL_FILE,OPENSSL_LINE)

const char *OpenSSL_version(int type);
#define OPENSSL_VERSION		0
#define OPENSSL_CFLAGS		1
#define OPENSSL_BUILT_ON	2
#define OPENSSL_PLATFORM	3
#define OPENSSL_DIR		4
#define OPENSSL_ENGINES_DIR	5
unsigned long OpenSSL_version_num(void);

const char *SSLeay_version(int type);
unsigned long SSLeay(void);

/* Within a given class, get/register a new index */
int CRYPTO_get_ex_new_index(int class_index, long argl, void *argp,
    CRYPTO_EX_new *new_func, CRYPTO_EX_dup *dup_func,
    CRYPTO_EX_free *free_func);
/* Initialise/duplicate/free CRYPTO_EX_DATA variables corresponding to a given
 * class (invokes whatever per-class callbacks are applicable) */
int CRYPTO_new_ex_data(int class_index, void *obj, CRYPTO_EX_DATA *ad);
int CRYPTO_dup_ex_data(int class_index, CRYPTO_EX_DATA *to,
    CRYPTO_EX_DATA *from);
void CRYPTO_free_ex_data(int class_index, void *obj, CRYPTO_EX_DATA *ad);
/* Get/set data in a CRYPTO_EX_DATA variable corresponding to a particular index
 * (relative to the class type involved) */
int CRYPTO_set_ex_data(CRYPTO_EX_DATA *ad, int idx, void *val);
void *CRYPTO_get_ex_data(const CRYPTO_EX_DATA *ad, int idx);
/* This function cleans up all "ex_data" state. It mustn't be called under
 * potential race-conditions. */
void CRYPTO_cleanup_all_ex_data(void);

void CRYPTO_lock(int mode, int type, const char *file, int line);
int CRYPTO_add_lock(int *pointer, int amount, int type, const char *file,
    int line);

/* Don't use this structure directly. */
typedef struct crypto_threadid_st CRYPTO_THREADID;

/* These functions are deprecated no-op stubs */
void CRYPTO_set_id_callback(unsigned long (*func)(void));
unsigned long (*CRYPTO_get_id_callback(void))(void);
unsigned long CRYPTO_thread_id(void);

int CRYPTO_get_new_lockid(char *name);
const char *CRYPTO_get_lock_name(int type);

int CRYPTO_num_locks(void);
void CRYPTO_set_locking_callback(void (*func)(int mode, int type,
    const char *file, int line));
void (*CRYPTO_get_locking_callback(void))(int mode, int type,
    const char *file, int line);
void CRYPTO_set_add_lock_callback(int (*func)(int *num, int mount, int type,
    const char *file, int line));
int (*CRYPTO_get_add_lock_callback(void))(int *num, int mount, int type,
    const char *file, int line);

void CRYPTO_THREADID_set_numeric(CRYPTO_THREADID *id, unsigned long val);
void CRYPTO_THREADID_set_pointer(CRYPTO_THREADID *id, void *ptr);
int CRYPTO_THREADID_set_callback(void (*threadid_func)(CRYPTO_THREADID *));
void (*CRYPTO_THREADID_get_callback(void))(CRYPTO_THREADID *);

int CRYPTO_get_new_dynlockid(void);
void CRYPTO_destroy_dynlockid(int i);
struct CRYPTO_dynlock_value *CRYPTO_get_dynlock_value(int i);
void CRYPTO_set_dynlock_create_callback(struct CRYPTO_dynlock_value *(*dyn_create_function)(const char *file, int line));
void CRYPTO_set_dynlock_lock_callback(void (*dyn_lock_function)(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line));
void CRYPTO_set_dynlock_destroy_callback(void (*dyn_destroy_function)(struct CRYPTO_dynlock_value *l, const char *file, int line));
struct CRYPTO_dynlock_value *(*CRYPTO_get_dynlock_create_callback(void))(const char *file, int line);
void (*CRYPTO_get_dynlock_lock_callback(void))(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line);
void (*CRYPTO_get_dynlock_destroy_callback(void))(struct CRYPTO_dynlock_value *l, const char *file, int line);

int CRYPTO_set_mem_functions(void *(*m)(size_t, const char *, int),
    void *(*r)(void *, size_t, const char *, int),
    void (*f)(void *, const char *, int));

void *CRYPTO_malloc(size_t num, const char *file, int line);
char *CRYPTO_strdup(const char *str, const char *file, int line);
void CRYPTO_free(void *ptr, const char *file, int line);

void OPENSSL_cleanse(void *ptr, size_t len);

/*
 * Because this is a public header, use a portable method of indicating the
 * function does not return, rather than __dead.
 */
#ifdef _MSC_VER
__declspec(noreturn)
#else
__attribute__((__noreturn__))
#endif
void OpenSSLDie(const char *file, int line, const char *assertion);
#define OPENSSL_assert(e)       (void)((e) ? 0 : (OpenSSLDie(OPENSSL_FILE, OPENSSL_LINE, #e),1))

int FIPS_mode(void);
int FIPS_mode_set(int r);

void OPENSSL_init(void);

/* CRYPTO_memcmp returns zero iff the |len| bytes at |a| and |b| are equal. It
 * takes an amount of time dependent on |len|, but independent of the contents
 * of |a| and |b|. Unlike memcmp, it cannot be used to put elements into a
 * defined order as the return value when a != b is undefined, other than to be
 * non-zero. */
int CRYPTO_memcmp(const void *a, const void *b, size_t len);

/*
 * OpenSSL compatible OPENSSL_INIT options.
 */

#define OPENSSL_INIT_NO_LOAD_CONFIG		0x00000001L
#define OPENSSL_INIT_LOAD_CONFIG		0x00000002L

/* LibreSSL specific */
#define _OPENSSL_INIT_FLAG_NOOP			0x80000000L

/*
 * These are provided for compatibility, but have no effect
 * on how LibreSSL is initialized.
 */
#define OPENSSL_INIT_NO_LOAD_CRYPTO_STRINGS	_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_LOAD_CRYPTO_STRINGS	_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ADD_ALL_CIPHERS		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ADD_ALL_DIGESTS		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_NO_ADD_ALL_CIPHERS		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_NO_ADD_ALL_DIGESTS		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ASYNC			_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ENGINE_RDRAND		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ENGINE_DYNAMIC		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ENGINE_OPENSSL		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ENGINE_CRYPTODEV		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ENGINE_CAPI		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ENGINE_PADLOCK		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ENGINE_AFALG		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_reserved_internal		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ATFORK			_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_ENGINE_ALL_BUILTIN		_OPENSSL_INIT_FLAG_NOOP
#define OPENSSL_INIT_NO_ATEXIT			_OPENSSL_INIT_FLAG_NOOP

int OPENSSL_init_crypto(uint64_t opts, const void *settings);
void OPENSSL_cleanup(void);

/*
 * CPU capabilities.
 */
#define	CRYPTO_CPU_CAPS_ACCELERATED_AES		0x00000001ULL

uint64_t OPENSSL_cpu_caps(void);

/*
 * OpenSSL helpfully put OPENSSL_gmtime() here because all other time related
 * functions are in asn1.h.
 */
struct tm *OPENSSL_gmtime(const time_t *time, struct tm *out_tm);

void ERR_load_CRYPTO_strings(void);

/* Error codes for the CRYPTO functions. */

/* Function codes. */
#define CRYPTO_F_CRYPTO_GET_EX_NEW_INDEX		 100
#define CRYPTO_F_CRYPTO_GET_NEW_DYNLOCKID		 103
#define CRYPTO_F_CRYPTO_GET_NEW_LOCKID			 101
#define CRYPTO_F_CRYPTO_SET_EX_DATA			 102
#define CRYPTO_F_DEF_ADD_INDEX				 104
#define CRYPTO_F_DEF_GET_CLASS				 105
#define CRYPTO_F_FIPS_MODE_SET				 109
#define CRYPTO_F_INT_DUP_EX_DATA			 106
#define CRYPTO_F_INT_FREE_EX_DATA			 107
#define CRYPTO_F_INT_NEW_EX_DATA			 108

/* Reason codes. */
#define CRYPTO_R_FIPS_MODE_NOT_SUPPORTED		 101
#define CRYPTO_R_NO_DYNLOCK_CREATE_CALLBACK		 100

#ifdef  __cplusplus
}
#endif
#endif
