#include <stdio.h>
#include "api/fs/fs.h"
#include "util/group.h"

/*
 * Check whether we can use a group for top down.
 * Without a group may get bad results due to multiplexing.
 */
bool arch_topdown_check_group(bool *warn)
{
	int n;

	if (sysctl__read_int("kernel/nmi_watchdog", &n) < 0)
		return false;
	if (n > 0) {
		*warn = true;
		return false;
	}
	return true;
}

void arch_topdown_group_warn(void)
{
	fprintf(stderr,
		"nmi_watchdog enabled with topdown. May give wrong results.\n"
		"Disable with echo 0 > /proc/sys/kernel/nmi_watchdog\n");
}
