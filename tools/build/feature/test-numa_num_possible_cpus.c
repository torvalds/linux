// SPDX-License-Identifier: GPL-2.0-only
#include <numa.h>

int main(void)
{
	return numa_num_possible_cpus();
}
