/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_PERF_COMMON_H
#define ARCH_PERF_COMMON_H

#include "../util/env.h"

extern const char *objdump_path;

int perf_env__lookup_objdump(struct perf_env *env);
const char *normalize_arch(char *arch);

#endif /* ARCH_PERF_COMMON_H */
