// SPDX-License-Identifier: GPL-2.0
#include <cpuidle.h>

int main(void)
{
	int rv = cpuidle_state_count(0);
	return rv;
}
