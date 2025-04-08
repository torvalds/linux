// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2024, Intel Corporation
#include <stdbool.h>
#include <stdio.h>

#define MAX_PACKED_FIELD_SIZE 50

int main(int argc, char **argv)
{
	/* The first macro doesn't need a 'do {} while(0)' loop */
	printf("#define CHECK_PACKED_FIELDS_1(fields) \\\n");
	printf("\tCHECK_PACKED_FIELD(fields, 0)\n\n");

	/* Remaining macros require a do/while loop, and are implemented
	 * recursively by calling the previous iteration's macro.
	 */
	for (int i = 2; i <= MAX_PACKED_FIELD_SIZE; i++) {
		printf("#define CHECK_PACKED_FIELDS_%d(fields) do { \\\n", i);
		printf("\tCHECK_PACKED_FIELDS_%d(fields); \\\n", i - 1);
		printf("\tCHECK_PACKED_FIELD(fields, %d); \\\n", i - 1);
		printf("} while (0)\n\n");
	}

	printf("#define CHECK_PACKED_FIELDS(fields) \\\n");

	for (int i = 1; i <= MAX_PACKED_FIELD_SIZE; i++)
		printf("\t__builtin_choose_expr(ARRAY_SIZE(fields) == %d, ({ CHECK_PACKED_FIELDS_%d(fields); }), \\\n",
		       i, i);

	printf("\t({ BUILD_BUG_ON_MSG(1, \"CHECK_PACKED_FIELDS() must be regenerated to support array sizes larger than %d.\"); }) \\\n",
	       MAX_PACKED_FIELD_SIZE);

	for (int i = 1; i <= MAX_PACKED_FIELD_SIZE; i++)
		printf(")");

	printf("\n");
}
