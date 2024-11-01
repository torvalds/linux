// SPDX-License-Identifier: GPL-2.0
#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/version.h>

int main(void)
{
	Dwarf *dbg = dwarf_begin(0, DWARF_C_READ);

	return (long)dbg;
}
