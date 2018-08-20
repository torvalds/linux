/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_PERF_COMMON_H
#define ARCH_PERF_COMMON_H

#include "../util/env.h"

int perf_env__lookup_objdump(struct perf_env *env, const char **path);

#endif /* ARCH_PERF_COMMON_H */
