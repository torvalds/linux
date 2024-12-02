/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CAP_H
#define __PERF_CAP_H

#include <stdbool.h>
#include <linux/capability.h>
#include <linux/compiler.h>

#ifdef HAVE_LIBCAP_SUPPORT

#include <sys/capability.h>

bool perf_cap__capable(cap_value_t cap);

#else

#include <unistd.h>
#include <sys/types.h>

static inline bool perf_cap__capable(int cap __maybe_unused)
{
	return geteuid() == 0;
}

#endif /* HAVE_LIBCAP_SUPPORT */

/* For older systems */
#ifndef CAP_SYSLOG
#define CAP_SYSLOG	34
#endif

#ifndef CAP_PERFMON
#define CAP_PERFMON	38
#endif

#endif /* __PERF_CAP_H */
