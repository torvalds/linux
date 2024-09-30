/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#ifndef _IPE_DIGEST_H
#define _IPE_DIGEST_H

#include <linux/types.h>
#include <linux/audit.h>

#include "policy.h"

struct digest_info {
	const char *alg;
	const u8 *digest;
	size_t digest_len;
};

struct digest_info *ipe_digest_parse(const char *valstr);
void ipe_digest_free(struct digest_info *digest_info);
void ipe_digest_audit(struct audit_buffer *ab, const struct digest_info *val);
bool ipe_digest_eval(const struct digest_info *expected,
		     const struct digest_info *digest);

#endif /* _IPE_DIGEST_H */
