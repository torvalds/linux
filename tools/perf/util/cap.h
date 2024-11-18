/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CAP_H
#define __PERF_CAP_H

#include <stdbool.h>

/* For older systems */
#ifndef CAP_SYSLOG
#define CAP_SYSLOG	34
#endif

#ifndef CAP_PERFMON
#define CAP_PERFMON	38
#endif

/* Query if a capability is supported, used_root is set if the fallback root check was used. */
bool perf_cap__capable(int cap, bool *used_root);

#endif /* __PERF_CAP_H */
