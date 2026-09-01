// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <linux/kernel.h>
#include "hist.h"
#include "c2c.h"

void c2c_fmt_free(struct perf_hpp_fmt *fmt)
{
	struct c2c_fmt *c2c_fmt;

	c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	free(c2c_fmt);
}

bool c2c_fmt_equal(struct perf_hpp_fmt *a, struct perf_hpp_fmt *b)
{
	struct c2c_fmt *c2c_a = container_of(a, struct c2c_fmt, fmt);
	struct c2c_fmt *c2c_b = container_of(b, struct c2c_fmt, fmt);

	return c2c_a->dim == c2c_b->dim;
}
