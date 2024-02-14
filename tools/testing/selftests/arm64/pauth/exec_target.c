// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#include <stdio.h>
#include <stdlib.h>
#include <sys/auxv.h>

#include "helper.h"

int main(void)
{
	struct signatures signed_vals;
	unsigned long hwcaps;
	size_t val;

	fread(&val, sizeof(size_t), 1, stdin);

	/* don't try to execute illegal (unimplemented) instructions) caller
	 * should have checked this and keep worker simple
	 */
	hwcaps = getauxval(AT_HWCAP);

	if (hwcaps & HWCAP_PACA) {
		signed_vals.keyia = keyia_sign(val);
		signed_vals.keyib = keyib_sign(val);
		signed_vals.keyda = keyda_sign(val);
		signed_vals.keydb = keydb_sign(val);
	}
	signed_vals.keyg = (hwcaps & HWCAP_PACG) ?  keyg_sign(val) : 0;

	fwrite(&signed_vals, sizeof(struct signatures), 1, stdout);

	return 0;
}
