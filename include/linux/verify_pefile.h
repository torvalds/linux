/* Signed PE file verification
 *
 * Copyright (C) 2014 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _LINUX_VERIFY_PEFILE_H
#define _LINUX_VERIFY_PEFILE_H

#include <crypto/public_key.h>

extern int verify_pefile_signature(const void *pebuf, unsigned pelen,
				   struct key *trusted_keyring,
				   enum key_being_used_for usage,
				   bool *_trusted);

#endif /* _LINUX_VERIFY_PEFILE_H */
