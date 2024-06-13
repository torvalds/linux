// SPDX-License-Identifier: GPL-2.0
#include <execinfo.h>
#include <stdio.h>

int main(void)
{
	void *backtrace_fns[10];
	size_t entries;

	entries = backtrace(backtrace_fns, 10);
	backtrace_symbols_fd(backtrace_fns, entries, 1);

	return 0;
}
