// SPDX-License-Identifier: GPL-2.0-only
/*
 * kexec_handover_debug.c - kexec handover optional debug functionality
 * Copyright (C) 2025 Google LLC, Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#define pr_fmt(fmt) "KHO: " fmt

#include "kexec_handover_internal.h"

bool kho_scratch_overlap(phys_addr_t phys, size_t size)
{
	phys_addr_t scratch_start, scratch_end;
	unsigned int i;

	for (i = 0; i < kho_scratch_cnt; i++) {
		scratch_start = kho_scratch[i].addr;
		scratch_end = kho_scratch[i].addr + kho_scratch[i].size;

		if (phys < scratch_end && (phys + size) > scratch_start)
			return true;
	}

	return false;
}
