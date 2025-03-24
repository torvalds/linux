// SPDX-License-Identifier: GPL-2.0+
/*
 * xarray.c: Userspace shim for XArray test-suite
 * Copyright (c) 2018 Matthew Wilcox <willy@infradead.org>
 */

#include "xarray-shared.h"
#include "test.h"

#undef XA_DEBUG
#include "../../../lib/test_xarray.c"

void xarray_tests(void)
{
	xarray_checks();
	xarray_exit();
}

int __weak main(void)
{
	rcu_register_thread();
	radix_tree_init();
	xarray_tests();
	radix_tree_cpu_dead(1);
	rcu_barrier();
	if (nr_allocated)
		printf("nr_allocated = %d\n", nr_allocated);
	rcu_unregister_thread();
	return 0;
}
