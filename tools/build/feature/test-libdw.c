// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>
#include <elfutils/version.h>

int test_libdw(void)
{
	Dwarf *dbg = dwarf_begin(0, DWARF_C_READ);

	return dbg == NULL;
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

int test_libdw_getlocations(void)
{
	Dwarf_Addr base, start, end;
	Dwarf_Attribute attr;
	Dwarf_Op *op;
	size_t nops;
	ptrdiff_t offset = 0;

	return (int)dwarf_getlocations(&attr, offset, &base, &start, &end, &op, &nops);
}

int test_libdw_getcfi(void)
{
	Dwarf *dwarf = NULL;

	return dwarf_getcfi(dwarf) == NULL;
}

int test_elfutils(void)
{
	Dwarf_CFI *cfi = NULL;

	dwarf_cfi_end(cfi);
	return 0;
}

int main(void)
{
	return test_libdw() + test_libdw_unwind() + test_libdw_getlocations() +
	       test_libdw_getcfi() + test_elfutils();
}
