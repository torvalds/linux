/* $OpenBSD: err.h,v 1.7 2024/08/31 10:09:15 tb Exp $ */
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

#ifndef _LIBCRYPTO_ERR_H
#define _LIBCRYPTO_ERR_H

#ifndef _MSC_VER
#include_next <openssl/err.h>
#else
#include "../include/openssl/err.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(ERR_put_error);
LCRYPTO_USED(ERR_set_error_data);
LCRYPTO_USED(ERR_get_error);
LCRYPTO_USED(ERR_get_error_line);
LCRYPTO_USED(ERR_get_error_line_data);
LCRYPTO_USED(ERR_peek_error);
LCRYPTO_USED(ERR_peek_error_line);
LCRYPTO_USED(ERR_peek_error_line_data);
LCRYPTO_USED(ERR_peek_last_error);
LCRYPTO_USED(ERR_peek_last_error_line);
LCRYPTO_USED(ERR_peek_last_error_line_data);
LCRYPTO_USED(ERR_clear_error);
LCRYPTO_USED(ERR_error_string);
LCRYPTO_USED(ERR_error_string_n);
LCRYPTO_USED(ERR_lib_error_string);
LCRYPTO_USED(ERR_func_error_string);
LCRYPTO_USED(ERR_reason_error_string);
LCRYPTO_USED(ERR_print_errors_cb);
LCRYPTO_USED(ERR_print_errors_fp);
LCRYPTO_USED(ERR_print_errors);
LCRYPTO_USED(ERR_asprintf_error_data);
LCRYPTO_USED(ERR_load_strings);
LCRYPTO_USED(ERR_unload_strings);
LCRYPTO_USED(ERR_load_ERR_strings);
LCRYPTO_USED(ERR_load_crypto_strings);
LCRYPTO_USED(ERR_free_strings);
LCRYPTO_USED(ERR_remove_thread_state);
LCRYPTO_USED(ERR_remove_state);
LCRYPTO_USED(ERR_get_next_error_library);
LCRYPTO_USED(ERR_set_mark);
LCRYPTO_USED(ERR_pop_to_mark);

#endif /* _LIBCRYPTO_ERR_H */
