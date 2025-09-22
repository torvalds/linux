/* $OpenBSD: conf.h,v 1.28 2025/03/01 10:11:19 tb Exp $ */
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

#ifndef  HEADER_CONF_H
#define HEADER_CONF_H

#include <openssl/opensslconf.h>

#include <openssl/bio.h>
#include <openssl/lhash.h>
#include <openssl/stack.h>
#include <openssl/safestack.h>

#include <openssl/ossl_typ.h>

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct {
	char *section;
	char *name;
	char *value;
} CONF_VALUE;

DECLARE_STACK_OF(CONF_VALUE)
DECLARE_LHASH_OF(CONF_VALUE);

struct conf_st;
struct conf_method_st;
typedef struct conf_method_st CONF_METHOD;

/* Module definitions */

typedef struct conf_imodule_st CONF_IMODULE;
typedef struct conf_module_st CONF_MODULE;

DECLARE_STACK_OF(CONF_MODULE)
DECLARE_STACK_OF(CONF_IMODULE)

/* DSO module function typedefs */
typedef int conf_init_func(CONF_IMODULE *md, const CONF *cnf);
typedef void conf_finish_func(CONF_IMODULE *md);

#define	CONF_MFLAGS_IGNORE_ERRORS	0x1
#define CONF_MFLAGS_IGNORE_RETURN_CODES	0x2
#define CONF_MFLAGS_SILENT		0x4
#define CONF_MFLAGS_NO_DSO		0x8
#define CONF_MFLAGS_IGNORE_MISSING_FILE	0x10
#define CONF_MFLAGS_DEFAULT_SECTION	0x20

void OPENSSL_config(const char *config_name);
void OPENSSL_no_config(void);

struct conf_st {
	const CONF_METHOD *meth;
	LHASH_OF(CONF_VALUE) *data;
};

CONF *NCONF_new(const CONF_METHOD *meth);
void NCONF_free(CONF *conf);

int NCONF_load(CONF *conf, const char *file, long *eline);
int NCONF_load_bio(CONF *conf, BIO *bp, long *eline);
STACK_OF(CONF_VALUE) *NCONF_get_section(const CONF *conf, const char *section);
char *NCONF_get_string(const CONF *conf, const char *group, const char *name);
int NCONF_get_number_e(const CONF *conf, const char *group, const char *name,
    long *result);

#define NCONF_get_number(c,g,n,r) NCONF_get_number_e(c,g,n,r)

/* Module functions */

int CONF_modules_load(const CONF *cnf, const char *appname,
    unsigned long flags);
int CONF_modules_load_file(const char *filename, const char *appname,
    unsigned long flags);
void CONF_modules_unload(int all);
void CONF_modules_finish(void);
void CONF_modules_free(void);

char *CONF_get1_default_config_file(void);

void ERR_load_CONF_strings(void);

/* Error codes for the CONF functions. */

/* Function codes. */
#define CONF_F_CONF_DUMP_FP				 104
#define CONF_F_CONF_LOAD				 100
#define CONF_F_CONF_LOAD_BIO				 102
#define CONF_F_CONF_LOAD_FP				 103
#define CONF_F_CONF_MODULES_LOAD			 116
#define CONF_F_CONF_PARSE_LIST				 119
#define CONF_F_DEF_LOAD					 120
#define CONF_F_DEF_LOAD_BIO				 121
#define CONF_F_MODULE_INIT				 115
#define CONF_F_MODULE_LOAD_DSO				 117
#define CONF_F_MODULE_RUN				 118
#define CONF_F_NCONF_DUMP_BIO				 105
#define CONF_F_NCONF_DUMP_FP				 106
#define CONF_F_NCONF_GET_NUMBER				 107
#define CONF_F_NCONF_GET_NUMBER_E			 112
#define CONF_F_NCONF_GET_SECTION			 108
#define CONF_F_NCONF_GET_STRING				 109
#define CONF_F_NCONF_LOAD				 113
#define CONF_F_NCONF_LOAD_BIO				 110
#define CONF_F_NCONF_LOAD_FP				 114
#define CONF_F_NCONF_NEW				 111
#define CONF_F_STR_COPY					 101

/* Reason codes. */
#define CONF_R_ERROR_LOADING_DSO			 110
#define CONF_R_LIST_CANNOT_BE_NULL			 115
#define CONF_R_MISSING_CLOSE_SQUARE_BRACKET		 100
#define CONF_R_MISSING_EQUAL_SIGN			 101
#define CONF_R_MISSING_FINISH_FUNCTION			 111
#define CONF_R_MISSING_INIT_FUNCTION			 112
#define CONF_R_MODULE_INITIALIZATION_ERROR		 109
#define CONF_R_NO_CLOSE_BRACE				 102
#define CONF_R_NO_CONF					 105
#define CONF_R_NO_CONF_OR_ENVIRONMENT_VARIABLE		 106
#define CONF_R_NO_SECTION				 107
#define CONF_R_NO_SUCH_FILE				 114
#define CONF_R_NO_VALUE					 108
#define CONF_R_UNABLE_TO_CREATE_NEW_SECTION		 103
#define CONF_R_UNKNOWN_MODULE_NAME			 113
#define CONF_R_VARIABLE_EXPANSION_TOO_LONG		 116
#define CONF_R_VARIABLE_HAS_NO_VALUE			 104

#ifdef  __cplusplus
}
#endif
#endif
