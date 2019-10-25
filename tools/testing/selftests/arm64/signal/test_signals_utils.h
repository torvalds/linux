/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 ARM Limited */

#ifndef __TEST_SIGNALS_UTILS_H__
#define __TEST_SIGNALS_UTILS_H__

#include "test_signals.h"

int test_init(struct tdescr *td);
int test_setup(struct tdescr *td);
void test_cleanup(struct tdescr *td);
int test_run(struct tdescr *td);
void test_result(struct tdescr *td);

static inline bool feats_ok(struct tdescr *td)
{
	return (td->feats_required & td->feats_supported) == td->feats_required;
}

#endif
