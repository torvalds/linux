/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#ifndef _IPE_AUDIT_H
#define _IPE_AUDIT_H

#include "policy.h"

void ipe_audit_match(const struct ipe_eval_ctx *const ctx,
		     enum ipe_match match_type,
		     enum ipe_action_type act, const struct ipe_rule *const r);
void ipe_audit_policy_load(const struct ipe_policy *const p);
void ipe_audit_policy_activation(const struct ipe_policy *const op,
				 const struct ipe_policy *const np);
void ipe_audit_enforce(bool new_enforce, bool old_enforce);

#endif /* _IPE_AUDIT_H */
