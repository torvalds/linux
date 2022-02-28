// SPDX-License-Identifier: GPL-2.0-or-later
#include "tests/basic_api.h"
#include "tests/alloc_api.h"
#include "tests/alloc_helpers_api.h"

int main(int argc, char **argv)
{
	memblock_basic_checks();
	memblock_alloc_checks();
	memblock_alloc_helpers_checks();

	return 0;
}
