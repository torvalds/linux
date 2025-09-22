/* $OpenBSD: crypto.h,v 1.9 2025/03/09 15:29:56 tb Exp $ */
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

#ifndef _LIBCRYPTO_CRYPTO_H
#define _LIBCRYPTO_CRYPTO_H

#ifndef _MSC_VER
#include_next <openssl/crypto.h>
#else
#include "../include/openssl/crypto.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(OpenSSL_version);
LCRYPTO_USED(OpenSSL_version_num);
LCRYPTO_USED(SSLeay_version);
LCRYPTO_USED(SSLeay);
LCRYPTO_USED(CRYPTO_get_ex_new_index);
LCRYPTO_USED(CRYPTO_new_ex_data);
LCRYPTO_USED(CRYPTO_dup_ex_data);
LCRYPTO_USED(CRYPTO_free_ex_data);
LCRYPTO_USED(CRYPTO_set_ex_data);
LCRYPTO_USED(CRYPTO_get_ex_data);
LCRYPTO_USED(CRYPTO_cleanup_all_ex_data);
LCRYPTO_USED(CRYPTO_lock);
LCRYPTO_USED(CRYPTO_add_lock);
LCRYPTO_USED(CRYPTO_set_mem_functions);
LCRYPTO_USED(OpenSSLDie);
LCRYPTO_USED(OPENSSL_cpu_caps);
LCRYPTO_USED(OPENSSL_init_crypto);
LCRYPTO_USED(OPENSSL_cleanup);
LCRYPTO_USED(OPENSSL_gmtime);
LCRYPTO_USED(ERR_load_CRYPTO_strings);
LCRYPTO_UNUSED(CRYPTO_mem_ctrl);
LCRYPTO_UNUSED(CRYPTO_set_id_callback);
LCRYPTO_UNUSED(CRYPTO_get_id_callback);
LCRYPTO_UNUSED(CRYPTO_thread_id);
LCRYPTO_UNUSED(CRYPTO_get_new_lockid);
LCRYPTO_UNUSED(CRYPTO_get_lock_name);
LCRYPTO_UNUSED(CRYPTO_num_locks);
LCRYPTO_UNUSED(CRYPTO_set_locking_callback);
LCRYPTO_UNUSED(CRYPTO_get_locking_callback);
LCRYPTO_UNUSED(CRYPTO_set_add_lock_callback);
LCRYPTO_UNUSED(CRYPTO_get_add_lock_callback);
LCRYPTO_UNUSED(CRYPTO_THREADID_set_numeric);
LCRYPTO_UNUSED(CRYPTO_THREADID_set_pointer);
LCRYPTO_UNUSED(CRYPTO_THREADID_set_callback);
LCRYPTO_UNUSED(CRYPTO_THREADID_get_callback);
LCRYPTO_UNUSED(CRYPTO_get_new_dynlockid);
LCRYPTO_UNUSED(CRYPTO_destroy_dynlockid);
LCRYPTO_UNUSED(CRYPTO_get_dynlock_value);
LCRYPTO_UNUSED(CRYPTO_set_dynlock_create_callback);
LCRYPTO_UNUSED(CRYPTO_set_dynlock_lock_callback);
LCRYPTO_UNUSED(CRYPTO_set_dynlock_destroy_callback);
LCRYPTO_UNUSED(CRYPTO_get_dynlock_lock_callback);
LCRYPTO_UNUSED(CRYPTO_get_dynlock_destroy_callback);
LCRYPTO_UNUSED(CRYPTO_get_dynlock_create_callback);
LCRYPTO_UNUSED(CRYPTO_malloc);
LCRYPTO_UNUSED(CRYPTO_strdup);
LCRYPTO_UNUSED(CRYPTO_free);
LCRYPTO_UNUSED(OPENSSL_cleanse);
LCRYPTO_UNUSED(FIPS_mode);
LCRYPTO_UNUSED(FIPS_mode_set);
LCRYPTO_UNUSED(OPENSSL_init);
LCRYPTO_UNUSED(CRYPTO_memcmp);

#endif /* _LIBCRYPTO_CRYPTO_H */
