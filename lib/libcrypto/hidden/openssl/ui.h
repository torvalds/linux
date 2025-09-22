/* $OpenBSD: ui.h,v 1.5 2024/08/31 10:28:03 tb Exp $ */
/*
 * Copyright (c) 2022 Bob Beck <beck@openbsd.org>
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

#ifndef _LIBCRYPTO_UI_H
#define _LIBCRYPTO_UI_H

#ifndef _MSC_VER
#include_next <openssl/ui.h>
#else
#include "../include/openssl/ui.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(UI_new);
LCRYPTO_USED(UI_new_method);
LCRYPTO_USED(UI_free);
LCRYPTO_USED(UI_add_input_string);
LCRYPTO_USED(UI_dup_input_string);
LCRYPTO_USED(UI_add_verify_string);
LCRYPTO_USED(UI_dup_verify_string);
LCRYPTO_USED(UI_add_input_boolean);
LCRYPTO_USED(UI_dup_input_boolean);
LCRYPTO_USED(UI_add_info_string);
LCRYPTO_USED(UI_dup_info_string);
LCRYPTO_USED(UI_add_error_string);
LCRYPTO_USED(UI_dup_error_string);
LCRYPTO_USED(UI_construct_prompt);
LCRYPTO_USED(UI_add_user_data);
LCRYPTO_USED(UI_get0_user_data);
LCRYPTO_USED(UI_get0_result);
LCRYPTO_USED(UI_process);
LCRYPTO_USED(UI_ctrl);
LCRYPTO_USED(UI_get_ex_new_index);
LCRYPTO_USED(UI_set_ex_data);
LCRYPTO_USED(UI_get_ex_data);
LCRYPTO_USED(UI_set_default_method);
LCRYPTO_USED(UI_get_default_method);
LCRYPTO_USED(UI_get_method);
LCRYPTO_USED(UI_set_method);
LCRYPTO_USED(UI_OpenSSL);
LCRYPTO_USED(UI_null);
LCRYPTO_USED(UI_create_method);
LCRYPTO_USED(UI_destroy_method);
LCRYPTO_USED(UI_method_set_opener);
LCRYPTO_USED(UI_method_set_writer);
LCRYPTO_USED(UI_method_set_flusher);
LCRYPTO_USED(UI_method_set_reader);
LCRYPTO_USED(UI_method_set_closer);
LCRYPTO_USED(UI_method_set_prompt_constructor);
LCRYPTO_USED(UI_method_get_opener);
LCRYPTO_USED(UI_method_get_writer);
LCRYPTO_USED(UI_method_get_flusher);
LCRYPTO_USED(UI_method_get_reader);
LCRYPTO_USED(UI_method_get_closer);
LCRYPTO_USED(UI_get_string_type);
LCRYPTO_USED(UI_get_input_flags);
LCRYPTO_USED(UI_get0_output_string);
LCRYPTO_USED(UI_get0_action_string);
LCRYPTO_USED(UI_get0_result_string);
LCRYPTO_USED(UI_get0_test_string);
LCRYPTO_USED(UI_get_result_minsize);
LCRYPTO_USED(UI_get_result_maxsize);
LCRYPTO_USED(UI_set_result);
LCRYPTO_USED(ERR_load_UI_strings);
LCRYPTO_USED(UI_method_get_prompt_constructor);

#endif /* _LIBCRYPTO_UI_H */
