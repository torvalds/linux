/*
 * tsig-openssl.h -- Interface to OpenSSL for TSIG support.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef TSIG_OPENSSL_H
#define TSIG_OPENSSL_H

#if defined(HAVE_SSL)

#include "region-allocator.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>

/*
 * Initialize OpenSSL support for TSIG.
 */
int tsig_openssl_init(region_type *region);

void tsig_openssl_finalize(void);

#endif /* defined(HAVE_SSL) */

#endif /* TSIG_OPENSSL_H */
