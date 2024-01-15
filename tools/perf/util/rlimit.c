/* SPDX-License-Identifier: LGPL-2.1 */

#include <errno.h>
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

bool rlimit__increase_nofile(enum rlimit_action *set_rlimit)
{
	int old_errno;
	struct rlimit l;

	if (*set_rlimit < INCREASED_MAX) {
		old_errno = errno;

		if (getrlimit(RLIMIT_NOFILE, &l) == 0) {
			if (*set_rlimit == NO_CHANGE) {
				l.rlim_cur = l.rlim_max;
			} else {
				l.rlim_cur = l.rlim_max + 1000;
				l.rlim_max = l.rlim_cur;
			}
			if (setrlimit(RLIMIT_NOFILE, &l) == 0) {
				(*set_rlimit) += 1;
				errno = old_errno;
				return true;
			}
		}
		errno = old_errno;
	}

	return false;
}
