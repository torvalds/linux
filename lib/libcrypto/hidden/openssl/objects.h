/* $OpenBSD: objects.h,v 1.5 2024/03/02 09:49:45 tb Exp $ */
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

#ifndef _LIBCRYPTO_OBJECTS_H
#define _LIBCRYPTO_OBJECTS_H

#ifndef _MSC_VER
#include_next <openssl/objects.h>
#else
#include "../include/openssl/objects.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(OBJ_NAME_do_all);
LCRYPTO_USED(OBJ_NAME_do_all_sorted);
LCRYPTO_USED(OBJ_dup);
LCRYPTO_USED(OBJ_nid2obj);
LCRYPTO_USED(OBJ_nid2ln);
LCRYPTO_USED(OBJ_nid2sn);
LCRYPTO_USED(OBJ_obj2nid);
LCRYPTO_USED(OBJ_txt2obj);
LCRYPTO_USED(OBJ_obj2txt);
LCRYPTO_USED(OBJ_txt2nid);
LCRYPTO_USED(OBJ_ln2nid);
LCRYPTO_USED(OBJ_sn2nid);
LCRYPTO_USED(OBJ_cmp);
LCRYPTO_USED(OBJ_new_nid);
LCRYPTO_USED(OBJ_create);
LCRYPTO_USED(OBJ_cleanup);
LCRYPTO_USED(OBJ_create_objects);
LCRYPTO_USED(OBJ_length);
LCRYPTO_USED(OBJ_get0_data);
LCRYPTO_USED(OBJ_find_sigid_algs);
LCRYPTO_USED(OBJ_find_sigid_by_algs);
LCRYPTO_USED(ERR_load_OBJ_strings);

#endif /* _LIBCRYPTO_OBJECTS_H */
