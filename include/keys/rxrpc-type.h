/* RxRPC key type
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _KEYS_RXRPC_TYPE_H
#define _KEYS_RXRPC_TYPE_H

#include <linux/key.h>

/*
 * key type for AF_RXRPC keys
 */
extern struct key_type key_type_rxrpc;

extern struct key *rxrpc_get_null_key(const char *);

#endif /* _KEYS_USER_TYPE_H */
