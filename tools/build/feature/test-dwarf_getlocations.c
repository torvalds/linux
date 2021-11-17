// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <elfutils/libdw.h>

int main(void)
{
	Dwarf_Addr base, start, end;
	Dwarf_Attribute attr;
	Dwarf_Op *op;
        size_t nops;
	ptrdiff_t offset = 0;
        return (int)dwarf_getlocations(&attr, offset, &base, &start, &end, &op, &nops);
}
