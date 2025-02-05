// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 ARM Limited
 */

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "testcases.h"

static uint64_t *gcs_page;

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

static int gcs_write_fault_trigger(struct tdescr *td)
{
	/* Verify that the page is readable (ie, not completely unmapped) */
	fprintf(stderr, "Read value 0x%lx\n", gcs_page[0]);

	/* A regular write should trigger a fault */
	gcs_page[0] = EINVAL;

	return 0;
}

static int gcs_write_fault_signal(struct tdescr *td, siginfo_t *si,
				  ucontext_t *uc)
{
	ASSERT_GOOD_CONTEXT(uc);

	return 1;
}


struct tdescr tde = {
	.name = "GCS write fault",
	.descr = "Normal writes to a GCS segfault",
	.feats_required = FEAT_GCS,
	.timeout = 3,
	.sig_ok = SIGSEGV,
	.sanity_disabled = true,
	.init = alloc_gcs,
	.trigger = gcs_write_fault_trigger,
	.run = gcs_write_fault_signal,
};
