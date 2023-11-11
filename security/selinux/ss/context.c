// SPDX-License-Identifier: GPL-2.0
/*
 * Implementations of the security context functions.
 *
 * Author: Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (C) 2020 Red Hat, Inc.
 */

#include <linux/jhash.h>

#include "context.h"
#include "mls.h"

u32 context_compute_hash(const struct context *c)
{
	u32 hash = 0;

	/*
	 * If a context is invalid, it will always be represented by a
	 * context struct with only the len & str set (and vice versa)
	 * under a given policy. Since context structs from different
	 * policies should never meet, it is safe to hash valid and
	 * invalid contexts differently. The context_cmp() function
	 * already operates under the same assumption.
	 */
	if (c->len)
		return full_name_hash(NULL, c->str, c->len);

	hash = jhash_3words(c->user, c->role, c->type, hash);
	hash = mls_range_hash(&c->range, hash);
	return hash;
}
