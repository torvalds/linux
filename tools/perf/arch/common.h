#ifndef ARCH_PERF_COMMON_H
#define ARCH_PERF_COMMON_H

#include "../util/session.h"

extern const char *objdump_path;

int perf_session_env__lookup_objdump(struct perf_session_env *env);

#endif /* ARCH_PERF_COMMON_H */
