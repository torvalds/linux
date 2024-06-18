/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#ifndef _NESTED_TRUST_COMMON_H
#define _NESTED_TRUST_COMMON_H

#include <stdbool.h>

bool bpf_cpumask_test_cpu(unsigned int cpu, const struct cpumask *cpumask) __ksym;
bool bpf_cpumask_first_zero(const struct cpumask *cpumask) __ksym;

#endif /* _NESTED_TRUST_COMMON_H */
