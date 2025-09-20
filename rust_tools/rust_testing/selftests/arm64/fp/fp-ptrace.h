// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2021-3 ARM Limited.

#ifndef FP_PTRACE_H
#define FP_PTRACE_H

#define SVCR_SM_SHIFT 0
#define SVCR_ZA_SHIFT 1

#define SVCR_SM (1 << SVCR_SM_SHIFT)
#define SVCR_ZA (1 << SVCR_ZA_SHIFT)

#define HAVE_SVE_SHIFT		0
#define HAVE_SME_SHIFT		1
#define HAVE_SME2_SHIFT		2
#define HAVE_FA64_SHIFT		3
#define HAVE_FPMR_SHIFT		4

#define HAVE_SVE	(1 << HAVE_SVE_SHIFT)
#define HAVE_SME	(1 << HAVE_SME_SHIFT)
#define HAVE_SME2	(1 << HAVE_SME2_SHIFT)
#define HAVE_FA64	(1 << HAVE_FA64_SHIFT)
#define HAVE_FPMR	(1 << HAVE_FPMR_SHIFT)

#endif
