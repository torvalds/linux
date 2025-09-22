/* $OpenBSD: engine.h,v 1.1 2024/03/27 06:08:45 tb Exp $ */
/*
 * Copyright (c) 2024 Theo Buehler <tb@openbsd.org>
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

#ifndef _LIBCRYPTO_ENGINE_H
#define _LIBCRYPTO_ENGINE_H

#ifndef _MSC_VER
#include_next <openssl/engine.h>
#else
#include "../include/openssl/engine.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(ENGINE_load_builtin_engines);
LCRYPTO_USED(ENGINE_load_dynamic);
LCRYPTO_USED(ENGINE_load_openssl);
LCRYPTO_USED(ENGINE_register_all_complete);
LCRYPTO_USED(ENGINE_cleanup);
LCRYPTO_USED(ENGINE_new);
LCRYPTO_USED(ENGINE_free);
LCRYPTO_USED(ENGINE_init);
LCRYPTO_USED(ENGINE_finish);
LCRYPTO_USED(ENGINE_by_id);
LCRYPTO_USED(ENGINE_get_id);
LCRYPTO_USED(ENGINE_get_name);
LCRYPTO_USED(ENGINE_set_default);
LCRYPTO_USED(ENGINE_get_default_RSA);
LCRYPTO_USED(ENGINE_set_default_RSA);
LCRYPTO_USED(ENGINE_ctrl_cmd);
LCRYPTO_USED(ENGINE_ctrl_cmd_string);
LCRYPTO_USED(ENGINE_load_private_key);
LCRYPTO_USED(ENGINE_load_public_key);

#endif /* _LIBCRYPTO_ENGINE_H */
