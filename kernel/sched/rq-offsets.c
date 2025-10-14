// SPDX-License-Identifier: GPL-2.0
#define COMPILE_OFFSETS
#include <linux/kbuild.h>
#include <linux/types.h>
#include "sched.h"

int main(void)
{
	DEFINE(RQ_nr_pinned, offsetof(struct rq, nr_pinned));

	return 0;
}
