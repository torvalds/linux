/*	$OpenBSD: ct_err.c,v 1.8 2024/06/24 06:43:22 tb Exp $ */
/* ====================================================================
 * Copyright (c) 1999-2006 The OpenSSL Project.  All rights reserved.
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
 *    openssl-core@OpenSSL.org.
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

#include <openssl/ct.h>
#include <openssl/err.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

static const ERR_STRING_DATA CT_str_functs[] = {
	{ERR_PACK(ERR_LIB_CT, CT_F_CTLOG_NEW, 0), "CTLOG_new"},
	{ERR_PACK(ERR_LIB_CT, CT_F_CTLOG_NEW_FROM_BASE64, 0),
	 "CTLOG_new_from_base64"},
	{ERR_PACK(ERR_LIB_CT, CT_F_CTLOG_NEW_FROM_CONF, 0),
	 "ctlog_new_from_conf"},
	{ERR_PACK(ERR_LIB_CT, CT_F_CTLOG_STORE_LOAD_CTX_NEW, 0),
	 "ctlog_store_load_ctx_new"},
	{ERR_PACK(ERR_LIB_CT, CT_F_CTLOG_STORE_LOAD_FILE, 0),
	 "CTLOG_STORE_load_file"},
	{ERR_PACK(ERR_LIB_CT, CT_F_CTLOG_STORE_LOAD_LOG, 0),
	 "ctlog_store_load_log"},
	{ERR_PACK(ERR_LIB_CT, CT_F_CTLOG_STORE_NEW, 0), "CTLOG_STORE_new"},
	{ERR_PACK(ERR_LIB_CT, CT_F_CT_BASE64_DECODE, 0), "ct_base64_decode"},
	{ERR_PACK(ERR_LIB_CT, CT_F_CT_POLICY_EVAL_CTX_NEW, 0),
	 "CT_POLICY_EVAL_CTX_new"},
	{ERR_PACK(ERR_LIB_CT, CT_F_CT_V1_LOG_ID_FROM_PKEY, 0),
	 "ct_v1_log_id_from_pkey"},
	{ERR_PACK(ERR_LIB_CT, CT_F_I2O_SCT, 0), "i2o_SCT"},
	{ERR_PACK(ERR_LIB_CT, CT_F_I2O_SCT_LIST, 0), "i2o_SCT_LIST"},
	{ERR_PACK(ERR_LIB_CT, CT_F_I2O_SCT_SIGNATURE, 0), "i2o_SCT_signature"},
	{ERR_PACK(ERR_LIB_CT, CT_F_O2I_SCT, 0), "o2i_SCT"},
	{ERR_PACK(ERR_LIB_CT, CT_F_O2I_SCT_LIST, 0), "o2i_SCT_LIST"},
	{ERR_PACK(ERR_LIB_CT, CT_F_O2I_SCT_SIGNATURE, 0), "o2i_SCT_signature"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_CTX_NEW, 0), "SCT_CTX_new"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_CTX_VERIFY, 0), "SCT_CTX_verify"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_NEW, 0), "SCT_new"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_NEW_FROM_BASE64, 0),
	 "SCT_new_from_base64"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_SET0_LOG_ID, 0), "SCT_set0_log_id"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_SET1_EXTENSIONS, 0),
	 "SCT_set1_extensions"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_SET1_LOG_ID, 0), "SCT_set1_log_id"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_SET1_SIGNATURE, 0),
	 "SCT_set1_signature"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_SET_LOG_ENTRY_TYPE, 0),
	 "SCT_set_log_entry_type"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_SET_SIGNATURE_NID, 0),
	 "SCT_set_signature_nid"},
	{ERR_PACK(ERR_LIB_CT, CT_F_SCT_SET_VERSION, 0), "SCT_set_version"},
	{0, NULL}
};

static const ERR_STRING_DATA CT_str_reasons[] = {
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_BASE64_DECODE_ERROR),
	 "base64 decode error"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_INVALID_LOG_ID_LENGTH),
	 "invalid log id length"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_LOG_CONF_INVALID), "log conf invalid"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_LOG_CONF_INVALID_KEY),
	 "log conf invalid key"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_LOG_CONF_MISSING_DESCRIPTION),
	 "log conf missing description"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_LOG_CONF_MISSING_KEY),
	 "log conf missing key"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_LOG_KEY_INVALID), "log key invalid"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_SCT_FUTURE_TIMESTAMP),
	 "sct future timestamp"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_SCT_INVALID), "sct invalid"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_SCT_INVALID_SIGNATURE),
	 "sct invalid signature"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_SCT_LIST_INVALID), "sct list invalid"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_SCT_LOG_ID_MISMATCH),
	 "sct log id mismatch"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_SCT_NOT_SET), "sct not set"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_SCT_UNSUPPORTED_VERSION),
	 "sct unsupported version"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_UNRECOGNIZED_SIGNATURE_NID),
	 "unrecognized signature nid"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_UNSUPPORTED_ENTRY_TYPE),
	 "unsupported entry type"},
	{ERR_PACK(ERR_LIB_CT, 0, CT_R_UNSUPPORTED_VERSION),
	 "unsupported version"},
	{0, NULL}
};

#endif

int
ERR_load_CT_strings(void)
{
	if (ERR_func_error_string(CT_str_functs[0].error) == NULL) {
		ERR_load_const_strings(CT_str_functs);
		ERR_load_const_strings(CT_str_reasons);
	}
	return 1;
}
