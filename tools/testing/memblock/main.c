// SPDX-License-Identifier: GPL-2.0-or-later
#include "tests/basic_api.h"
#include "tests/alloc_api.h"

int main(int argc, char **argv)
{
	memblock_basic_checks();
	memblock_alloc_checks();

	return 0;
}
