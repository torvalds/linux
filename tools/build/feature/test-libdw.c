// SPDX-License-Identifier: GPL-2.0
#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>
#include <elfutils/version.h>

int test_libdw(void)
{
	Dwarf *dbg = dwarf_begin(0, DWARF_C_READ);

	return (long)dbg;
}

int test_libdw_unwind(void)
{
	/*
	 * This function is guarded via: __nonnull_attribute__ (1, 2).
	 * Passing '1' as arguments value. This code is never executed,
	 * only compiled.
	 */
	dwfl_thread_getframes((void *) 1, (void *) 1, NULL);
	return 0;
}

int main(void)
{
	return test_libdw() + test_libdw_unwind();
}
