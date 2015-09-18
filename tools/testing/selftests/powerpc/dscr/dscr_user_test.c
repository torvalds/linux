/*
 * POWER Data Stream Control Register (DSCR) SPR test
 *
 * This test modifies the DSCR value through both the SPR number
 * based mtspr instruction and then makes sure that the same is
 * reflected through mfspr instruction using either of the SPR
 * numbers.
 *
 * When using the privilege state SPR, the instructions such as
 * mfspr or mtspr are priviledged and the kernel emulates them
 * for us. Instructions using problem state SPR can be exuecuted
 * directly without any emulation if the HW supports them. Else
 * they also get emulated by the kernel.
 *
 * Copyright 2013, Anton Blanchard, IBM Corporation.
 * Copyright 2015, Anshuman Khandual, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */
#include "dscr.h"

static int check_dscr(char *str)
{
	unsigned long cur_dscr, cur_dscr_usr;

	cur_dscr = get_dscr();
	cur_dscr_usr = get_dscr_usr();
	if (cur_dscr != cur_dscr_usr) {
		printf("%s set, kernel get %lx != user get %lx\n",
					str, cur_dscr, cur_dscr_usr);
		return 1;
	}
	return 0;
}

int dscr_user(void)
{
	int i;

	check_dscr("");

	for (i = 0; i < COUNT; i++) {
		set_dscr(i);
		if (check_dscr("kernel"))
			return 1;
	}

	for (i = 0; i < COUNT; i++) {
		set_dscr_usr(i);
		if (check_dscr("user"))
			return 1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	return test_harness(dscr_user, "dscr_user_test");
}
