/* $OpenBSD: engine.h,v 1.44 2024/03/02 10:22:07 tb Exp $ */
/* Written by Geoff Thorpe (geoff@geoffthorpe.net) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 1999-2004 The OpenSSL Project.  All rights reserved.
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
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * ECDH support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project.
 */

#ifndef HEADER_ENGINE_H
#define HEADER_ENGINE_H

#include <openssl/opensslconf.h>

#include <openssl/err.h>
#include <openssl/ui.h>

#include <openssl/ossl_typ.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define ENGINE_METHOD_RSA		(unsigned int)0x0001
#define ENGINE_METHOD_DSA		(unsigned int)0x0002
#define ENGINE_METHOD_DH		(unsigned int)0x0004
#define ENGINE_METHOD_RAND		(unsigned int)0x0008
#define ENGINE_METHOD_CIPHERS		(unsigned int)0x0040
#define ENGINE_METHOD_DIGESTS		(unsigned int)0x0080
#define ENGINE_METHOD_STORE		(unsigned int)0x0100
#define ENGINE_METHOD_PKEY_METHS	(unsigned int)0x0200
#define ENGINE_METHOD_PKEY_ASN1_METHS	(unsigned int)0x0400
#define ENGINE_METHOD_EC		(unsigned int)0x0800
#define ENGINE_METHOD_ALL		(unsigned int)0xFFFF
#define ENGINE_METHOD_NONE		(unsigned int)0x0000

/*
 * Prototypes for the stub functions in engine_stubs.c. They are provided to
 * build M2Crypto, Dovecot, apr-utils without patching.
 */
void ENGINE_load_builtin_engines(void);
void ENGINE_load_dynamic(void);
void ENGINE_load_openssl(void);
int ENGINE_register_all_complete(void);

void ENGINE_cleanup(void);

ENGINE *ENGINE_new(void);
int ENGINE_free(ENGINE *engine);
int ENGINE_init(ENGINE *engine);
int ENGINE_finish(ENGINE *engine);

ENGINE *ENGINE_by_id(const char *id);
const char *ENGINE_get_id(const ENGINE *engine);
const char *ENGINE_get_name(const ENGINE *engine);

int ENGINE_set_default(ENGINE *engine, unsigned int flags);

ENGINE *ENGINE_get_default_RSA(void);
int ENGINE_set_default_RSA(ENGINE *engine);

int ENGINE_ctrl_cmd(ENGINE *engine, const char *cmd_name, long i, void *p,
    void (*f)(void), int cmd_optional);
int ENGINE_ctrl_cmd_string(ENGINE *engine, const char *cmd, const char *arg,
    int cmd_optional);

EVP_PKEY *ENGINE_load_private_key(ENGINE *engine, const char *key_id,
    UI_METHOD *ui_method, void *callback_data);
EVP_PKEY *ENGINE_load_public_key(ENGINE *engine, const char *key_id,
    UI_METHOD *ui_method, void *callback_data);

/* Error codes for the ENGINE functions. */

/* Function codes. */
#define ENGINE_F_DYNAMIC_CTRL				 180
#define ENGINE_F_DYNAMIC_GET_DATA_CTX			 181
#define ENGINE_F_DYNAMIC_LOAD				 182
#define ENGINE_F_DYNAMIC_SET_DATA_CTX			 183
#define ENGINE_F_ENGINE_ADD				 105
#define ENGINE_F_ENGINE_BY_ID				 106
#define ENGINE_F_ENGINE_CMD_IS_EXECUTABLE		 170
#define ENGINE_F_ENGINE_CTRL				 142
#define ENGINE_F_ENGINE_CTRL_CMD			 178
#define ENGINE_F_ENGINE_CTRL_CMD_STRING			 171
#define ENGINE_F_ENGINE_FINISH				 107
#define ENGINE_F_ENGINE_FREE_UTIL			 108
#define ENGINE_F_ENGINE_GET_CIPHER			 185
#define ENGINE_F_ENGINE_GET_DEFAULT_TYPE		 177
#define ENGINE_F_ENGINE_GET_DIGEST			 186
#define ENGINE_F_ENGINE_GET_NEXT			 115
#define ENGINE_F_ENGINE_GET_PKEY_ASN1_METH		 193
#define ENGINE_F_ENGINE_GET_PKEY_METH			 192
#define ENGINE_F_ENGINE_GET_PREV			 116
#define ENGINE_F_ENGINE_INIT				 119
#define ENGINE_F_ENGINE_LIST_ADD			 120
#define ENGINE_F_ENGINE_LIST_REMOVE			 121
#define ENGINE_F_ENGINE_LOAD_PRIVATE_KEY		 150
#define ENGINE_F_ENGINE_LOAD_PUBLIC_KEY			 151
#define ENGINE_F_ENGINE_LOAD_SSL_CLIENT_CERT		 194
#define ENGINE_F_ENGINE_NEW				 122
#define ENGINE_F_ENGINE_REMOVE				 123
#define ENGINE_F_ENGINE_SET_DEFAULT_STRING		 189
#define ENGINE_F_ENGINE_SET_DEFAULT_TYPE		 126
#define ENGINE_F_ENGINE_SET_ID				 129
#define ENGINE_F_ENGINE_SET_NAME			 130
#define ENGINE_F_ENGINE_TABLE_REGISTER			 184
#define ENGINE_F_ENGINE_UNLOAD_KEY			 152
#define ENGINE_F_ENGINE_UNLOCKED_FINISH			 191
#define ENGINE_F_ENGINE_UP_REF				 190
#define ENGINE_F_INT_CTRL_HELPER			 172
#define ENGINE_F_INT_ENGINE_CONFIGURE			 188
#define ENGINE_F_INT_ENGINE_MODULE_INIT			 187
#define ENGINE_F_LOG_MESSAGE				 141

/* Reason codes. */
#define ENGINE_R_ALREADY_LOADED				 100
#define ENGINE_R_ARGUMENT_IS_NOT_A_NUMBER		 133
#define ENGINE_R_CMD_NOT_EXECUTABLE			 134
#define ENGINE_R_COMMAND_TAKES_INPUT			 135
#define ENGINE_R_COMMAND_TAKES_NO_INPUT			 136
#define ENGINE_R_CONFLICTING_ENGINE_ID			 103
#define ENGINE_R_CTRL_COMMAND_NOT_IMPLEMENTED		 119
#define ENGINE_R_DH_NOT_IMPLEMENTED			 139
#define ENGINE_R_DSA_NOT_IMPLEMENTED			 140
#define ENGINE_R_DSO_FAILURE				 104
#define ENGINE_R_DSO_NOT_FOUND				 132
#define ENGINE_R_ENGINES_SECTION_ERROR			 148
#define ENGINE_R_ENGINE_CONFIGURATION_ERROR		 102
#define ENGINE_R_ENGINE_IS_NOT_IN_LIST			 105
#define ENGINE_R_ENGINE_SECTION_ERROR			 149
#define ENGINE_R_FAILED_LOADING_PRIVATE_KEY		 128
#define ENGINE_R_FAILED_LOADING_PUBLIC_KEY		 129
#define ENGINE_R_FINISH_FAILED				 106
#define ENGINE_R_GET_HANDLE_FAILED			 107
#define ENGINE_R_ID_OR_NAME_MISSING			 108
#define ENGINE_R_INIT_FAILED				 109
#define ENGINE_R_INTERNAL_LIST_ERROR			 110
#define ENGINE_R_INVALID_ARGUMENT			 143
#define ENGINE_R_INVALID_CMD_NAME			 137
#define ENGINE_R_INVALID_CMD_NUMBER			 138
#define ENGINE_R_INVALID_INIT_VALUE			 151
#define ENGINE_R_INVALID_STRING				 150
#define ENGINE_R_NOT_INITIALISED			 117
#define ENGINE_R_NOT_LOADED				 112
#define ENGINE_R_NO_CONTROL_FUNCTION			 120
#define ENGINE_R_NO_INDEX				 144
#define ENGINE_R_NO_LOAD_FUNCTION			 125
#define ENGINE_R_NO_REFERENCE				 130
#define ENGINE_R_NO_SUCH_ENGINE				 116
#define ENGINE_R_NO_UNLOAD_FUNCTION			 126
#define ENGINE_R_PROVIDE_PARAMETERS			 113
#define ENGINE_R_RSA_NOT_IMPLEMENTED			 141
#define ENGINE_R_UNIMPLEMENTED_CIPHER			 146
#define ENGINE_R_UNIMPLEMENTED_DIGEST			 147
#define ENGINE_R_UNIMPLEMENTED_PUBLIC_KEY_METHOD	 101
#define ENGINE_R_VERSION_INCOMPATIBILITY		 145

#ifdef  __cplusplus
}
#endif
#endif
