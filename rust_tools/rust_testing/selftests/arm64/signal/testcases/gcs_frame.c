// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 ARM Limited
 */

#include <signal.h>
#include <ucontext.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "testcases.h"

static union {
	ucontext_t uc;
	char buf[1024 * 64];
} context;

static int gcs_regs(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	size_t offset;
	struct _aarch64_ctx *head = GET_BUF_RESV_HEAD(context);
	struct gcs_context *gcs;
	unsigned long expected, gcspr;
	uint64_t *u64_val;
	int ret;

	ret = prctl(PR_GET_SHADOW_STACK_STATUS, &expected, 0, 0, 0);
	if (ret != 0) {
		fprintf(stderr, "Unable to query GCS status\n");
		return 1;
	}

	/* We expect a cap to be added to the GCS in the signal frame */
	gcspr = get_gcspr_el0();
	gcspr -= 8;
	fprintf(stderr, "Expecting GCSPR_EL0 %lx\n", gcspr);

	if (!get_current_context(td, &context.uc, sizeof(context))) {
		fprintf(stderr, "Failed getting context\n");
		return 1;
	}

	/* Ensure that the signal restore token was consumed */
	u64_val = (uint64_t *)get_gcspr_el0() + 1;
	if (*u64_val) {
		fprintf(stderr, "GCS value at %p is %lx not 0\n",
			u64_val, *u64_val);
		return 1;
	}

	fprintf(stderr, "Got context\n");

	head = get_header(head, GCS_MAGIC, GET_BUF_RESV_SIZE(context),
			  &offset);
	if (!head) {
		fprintf(stderr, "No GCS context\n");
		return 1;
	}

	gcs = (struct gcs_context *)head;

	/* Basic size validation is done in get_current_context() */

	if (gcs->features_enabled != expected) {
		fprintf(stderr, "Features enabled %llx but expected %lx\n",
			gcs->features_enabled, expected);
		return 1;
	}

	if (gcs->gcspr != gcspr) {
		fprintf(stderr, "Got GCSPR %llx but expected %lx\n",
			gcs->gcspr, gcspr);
		return 1;
	}

	fprintf(stderr, "GCS context validated\n");
	td->pass = 1;

	return 0;
}

struct tdescr tde = {
	.name = "GCS basics",
	.descr = "Validate a GCS signal context",
	.feats_required = FEAT_GCS,
	.timeout = 3,
	.run = gcs_regs,
};
