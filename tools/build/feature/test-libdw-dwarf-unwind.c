// SPDX-License-Identifier: GPL-2.0

#include <elfutils/libdwfl.h>

int main(void)
{
	/*
	 * This function is guarded via: __nonnull_attribute__ (1, 2).
	 * Passing '1' as arguments value. This code is never executed,
	 * only compiled.
	 */
	dwfl_thread_getframes((void *) 1, (void *) 1, NULL);
	return 0;
}
