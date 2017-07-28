#include <config.h>

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

#include "int_typedefs.h"

#include "barriers.h"
#include "bug_on.h"
#include "locks.h"
#include "misc.h"
#include "preempt.h"
#include "percpu.h"
#include "workqueues.h"

#include <linux/srcu.h>

/* Functions needed from modify_srcu.c */
bool try_check_zero(struct srcu_struct *sp, int idx, int trycount);
void srcu_flip(struct srcu_struct *sp);

/* Simpler implementation of synchronize_srcu that ignores batching. */
void synchronize_srcu(struct srcu_struct *sp)
{
	int idx;
	/*
	 * This code assumes that try_check_zero will succeed anyway,
	 * so there is no point in multiple tries.
	 */
	const int trycount = 1;

	might_sleep();

	/* Ignore the lock, as multiple writers aren't working yet anyway. */

	idx = 1 ^ (sp->completed & 1);

	/* For comments see srcu_advance_batches. */

	assume(try_check_zero(sp, idx, trycount));

	srcu_flip(sp);

	assume(try_check_zero(sp, idx^1, trycount));
}
