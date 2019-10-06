/* SPDX-License-Identifier: LGPL-2.1 */

#include "util/debug.h"
#include "util/rlimit.h"
#include <sys/time.h>
#include <sys/resource.h>

/*
 * Bump the memlock so that we can get bpf maps of a reasonable size,
 * like the ones used with 'perf trace' and with 'perf test bpf',
 * improve this to some specific request if needed.
 */
void rlimit__bump_memlock(void)
{
	struct rlimit rlim;

	if (getrlimit(RLIMIT_MEMLOCK, &rlim) == 0) {
		rlim.rlim_cur *= 4;
		rlim.rlim_max *= 4;

		if (setrlimit(RLIMIT_MEMLOCK, &rlim) < 0) {
			rlim.rlim_cur /= 2;
			rlim.rlim_max /= 2;

			if (setrlimit(RLIMIT_MEMLOCK, &rlim) < 0)
				pr_debug("Couldn't bump rlimit(MEMLOCK), failures may take place when creating BPF maps, etc\n");
		}
	}
}
