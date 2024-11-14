/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdbool.h>

bool is_xtheadvector_supported(void);

bool is_vector_supported(void);

int launch_test(char *next_program, int test_inherit, int xtheadvector);
