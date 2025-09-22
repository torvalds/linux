/* $OpenBSD: ct.h,v 1.1 2023/07/08 07:22:58 beck Exp $ */
/*
 * Copyright (c) 2023 Bob Beck <beck@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LIBCRYPTO_CT_H
#define _LIBCRYPTO_CT_H

#ifndef _MSC_VER
#include_next <openssl/ct.h>
#else
#include "../include/openssl/ct.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(CT_POLICY_EVAL_CTX_new);
LCRYPTO_USED(CT_POLICY_EVAL_CTX_free);
LCRYPTO_USED(CT_POLICY_EVAL_CTX_get0_cert);
LCRYPTO_USED(CT_POLICY_EVAL_CTX_set1_cert);
LCRYPTO_USED(CT_POLICY_EVAL_CTX_get0_issuer);
LCRYPTO_USED(CT_POLICY_EVAL_CTX_set1_issuer);
LCRYPTO_USED(CT_POLICY_EVAL_CTX_get0_log_store);
LCRYPTO_USED(CT_POLICY_EVAL_CTX_set_shared_CTLOG_STORE);
LCRYPTO_USED(CT_POLICY_EVAL_CTX_get_time);
LCRYPTO_USED(CT_POLICY_EVAL_CTX_set_time);
LCRYPTO_USED(SCT_new);
LCRYPTO_USED(SCT_new_from_base64);
LCRYPTO_USED(SCT_free);
LCRYPTO_USED(SCT_LIST_free);
LCRYPTO_USED(SCT_get_version);
LCRYPTO_USED(SCT_set_version);
LCRYPTO_USED(SCT_get_log_entry_type);
LCRYPTO_USED(SCT_set_log_entry_type);
LCRYPTO_USED(SCT_get0_log_id);
LCRYPTO_USED(SCT_set0_log_id);
LCRYPTO_USED(SCT_set1_log_id);
LCRYPTO_USED(SCT_get_timestamp);
LCRYPTO_USED(SCT_set_timestamp);
LCRYPTO_USED(SCT_get_signature_nid);
LCRYPTO_USED(SCT_set_signature_nid);
LCRYPTO_USED(SCT_get0_extensions);
LCRYPTO_USED(SCT_set0_extensions);
LCRYPTO_USED(SCT_set1_extensions);
LCRYPTO_USED(SCT_get0_signature);
LCRYPTO_USED(SCT_set0_signature);
LCRYPTO_USED(SCT_set1_signature);
LCRYPTO_USED(SCT_get_source);
LCRYPTO_USED(SCT_set_source);
LCRYPTO_USED(SCT_validation_status_string);
LCRYPTO_USED(SCT_print);
LCRYPTO_USED(SCT_LIST_print);
LCRYPTO_USED(SCT_get_validation_status);
LCRYPTO_USED(SCT_validate);
LCRYPTO_USED(SCT_LIST_validate);
LCRYPTO_USED(i2o_SCT_LIST);
LCRYPTO_USED(o2i_SCT_LIST);
LCRYPTO_USED(i2d_SCT_LIST);
LCRYPTO_USED(d2i_SCT_LIST);
LCRYPTO_USED(i2o_SCT);
LCRYPTO_USED(o2i_SCT);
LCRYPTO_USED(CTLOG_new);
LCRYPTO_USED(CTLOG_new_from_base64);
LCRYPTO_USED(CTLOG_free);
LCRYPTO_USED(CTLOG_get0_name);
LCRYPTO_USED(CTLOG_get0_log_id);
LCRYPTO_USED(CTLOG_get0_public_key);
LCRYPTO_USED(CTLOG_STORE_new);
LCRYPTO_USED(CTLOG_STORE_free);
LCRYPTO_USED(CTLOG_STORE_get0_log_by_id);
LCRYPTO_USED(CTLOG_STORE_load_file);
LCRYPTO_USED(CTLOG_STORE_load_default_file);

#endif /* _LIBCRYPTO_CT_H */
