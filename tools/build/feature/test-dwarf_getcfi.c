// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <elfutils/libdw.h>

int main(void)
{
	Dwarf *dwarf = NULL;
	return dwarf_getcfi(dwarf) == NULL;
}
