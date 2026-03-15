// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 ARM Limited
 */

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "testcases.h"

static uint64_t *gcs_page;
static bool post_mprotect;

#ifndef __NR_map_shadow_stack
#define __NR_map_shadow_stack 453
#endif

static bool alloc_gcs(struct tdescr *td)
{
	long page_size = sysconf(_SC_PAGE_SIZE);

	gcs_page = (void *)syscall(__NR_map_shadow_stack, 0,
				   page_size, 0);
	if (gcs_page == MAP_FAILED) {
		fprintf(stderr, "Failed to map %ld byte GCS: %d\n",
			page_size, errno);
		return false;
	}

	return true;
}

static int gcs_prot_none_fault_trigger(struct tdescr *td)
{
	/* Verify that the page is readable (ie, not completely unmapped) */
	fprintf(stderr, "Read value 0x%lx\n", gcs_page[0]);

	if (mprotect(gcs_page, sysconf(_SC_PAGE_SIZE), PROT_NONE) != 0) {
		fprintf(stderr, "mprotect(PROT_NONE) failed: %d\n", errno);
		return 0;
	}
	post_mprotect = true;

	/* This should trigger a fault if PROT_NONE is honoured for the GCS page */
	fprintf(stderr, "Read value after mprotect(PROT_NONE) 0x%lx\n", gcs_page[0]);
	return 0;
}

static int gcs_prot_none_fault_signal(struct tdescr *td, siginfo_t *si,
				      ucontext_t *uc)
{
	ASSERT_GOOD_CONTEXT(uc);

	/* A fault before mprotect(PROT_NONE) is unexpected. */
	if (!post_mprotect)
		return 0;

	return 1;
}

struct tdescr tde = {
	.name = "GCS PROT_NONE fault",
	.descr = "Read from GCS after mprotect(PROT_NONE) segfaults",
	.feats_required = FEAT_GCS,
	.timeout = 3,
	.sig_ok = SIGSEGV,
	.sanity_disabled = true,
	.init = alloc_gcs,
	.trigger = gcs_prot_none_fault_trigger,
	.run = gcs_prot_none_fault_signal,
};
