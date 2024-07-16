// SPDX-License-Identifier: GPL-2.0
#include <config.h>

#include "misc.h"
#include "bug_on.h"

struct rcu_head;

void wakeme_after_rcu(struct rcu_head *head)
{
	BUG();
}
