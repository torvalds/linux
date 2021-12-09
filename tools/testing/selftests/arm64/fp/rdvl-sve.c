// SPDX-License-Identifier: GPL-2.0-only

#include <stdio.h>

#include "rdvl.h"

int main(void)
{
	int vl = rdvl_sve();

	printf("%d\n", vl);

	return 0;
}
