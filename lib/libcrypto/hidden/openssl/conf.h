/* $OpenBSD: conf.h,v 1.9 2024/08/31 09:54:31 tb Exp $ */
/*
 * Copyright (c) 2024 Bob Beck <beck@openbsd.org>
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

#ifndef _LIBCRYPTO_CONF_H
#define _LIBCRYPTO_CONF_H

#ifndef _MSC_VER
#include_next <openssl/conf.h>
#else
#include "../include/openssl/conf.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(OPENSSL_config);
LCRYPTO_USED(OPENSSL_no_config);
LCRYPTO_USED(NCONF_new);
LCRYPTO_USED(NCONF_free);
LCRYPTO_USED(NCONF_load);
LCRYPTO_USED(NCONF_load_bio);
LCRYPTO_USED(NCONF_get_section);
LCRYPTO_USED(NCONF_get_string);
LCRYPTO_USED(NCONF_get_number_e);
LCRYPTO_USED(CONF_modules_load);
LCRYPTO_USED(CONF_modules_load_file);
LCRYPTO_USED(CONF_modules_unload);
LCRYPTO_USED(CONF_modules_finish);
LCRYPTO_USED(CONF_modules_free);
LCRYPTO_USED(CONF_get1_default_config_file);
LCRYPTO_USED(ERR_load_CONF_strings);

#endif /* _LIBCRYPTO_CONF_H */
