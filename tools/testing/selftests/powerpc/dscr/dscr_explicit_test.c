// SPDX-License-Identifier: GPL-2.0-only
/*
 * POWER Data Stream Control Register (DSCR) explicit test
 *
 * This test modifies the DSCR value using mtspr instruction and
 * verifies the change with mfspr instruction. It uses both the
 * privilege state SPR and the problem state SPR for this purpose.
 *
 * When using the privilege state SPR, the instructions such as
 * mfspr or mtspr are priviledged and the kernel emulates them
 * for us. Instructions using problem state SPR can be exuecuted
 * directly without any emulation if the HW supports them. Else
 * they also get emulated by the kernel.
 *
 * Copyright 2012, Anton Blanchard, IBM Corporation.
 * Copyright 2015, Anshuman Khandual, IBM Corporation.
 */
#include "dscr.h"

int dscr_explicit(void)
{
	unsigned long i, dscr = 0;

	srand(getpid());
	set_dscr(dscr);

	for (i = 0; i < COUNT; i++) {
		unsigned long cur_dscr, cur_dscr_usr;
		double ret = uniform_deviate(rand());

		if (ret < 0.001) {
			dscr++;
			if (dscr > DSCR_MAX)
				dscr = 0;

			set_dscr(dscr);
		}

		cur_dscr = get_dscr();
		if (cur_dscr != dscr) {
			fprintf(stderr, "Kernel DSCR should be %ld but "
					"is %ld\n", dscr, cur_dscr);
			return 1;
		}

		ret = uniform_deviate(rand());
		if (ret < 0.001) {
			dscr++;
			if (dscr > DSCR_MAX)
				dscr = 0;

			set_dscr_usr(dscr);
		}

		cur_dscr_usr = get_dscr_usr();
		if (cur_dscr_usr != dscr) {
			fprintf(stderr, "User DSCR should be %ld but "
					"is %ld\n", dscr, cur_dscr_usr);
			return 1;
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	return test_harness(dscr_explicit, "dscr_explicit_test");
}
