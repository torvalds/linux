// SPDX-License-Identifier: GPL-2.0
#include <elfutils/debuginfod.h>

int main(void)
{
	debuginfod_client* c = debuginfod_begin();
	return (long)c;
}
