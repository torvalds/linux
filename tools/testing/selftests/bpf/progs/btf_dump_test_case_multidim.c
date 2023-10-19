// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * BTF-to-C dumper test for multi-dimensional array output.
 *
 * Copyright (c) 2019 Facebook
 */
/* ----- START-EXPECTED-OUTPUT ----- */
typedef int arr_t[2];

typedef int multiarr_t[3][4][5];

typedef int *ptr_arr_t[6];

typedef int *ptr_multiarr_t[7][8][9][10];

typedef int * (*fn_ptr_arr_t[11])();

typedef int * (*fn_ptr_multiarr_t[12][13])();

struct root_struct {
	arr_t _1;
	multiarr_t _2;
	ptr_arr_t _3;
	ptr_multiarr_t _4;
	fn_ptr_arr_t _5;
	fn_ptr_multiarr_t _6;
};

/* ------ END-EXPECTED-OUTPUT ------ */

int f(struct root_struct *s)
{
	return 0;
}
