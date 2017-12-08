// SPDX-License-Identifier: GPL-2.0
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

#ifdef USE_SIMPLE_SYNC_SRCU
#define synchronize_srcu(sp) synchronize_srcu_original(sp)
#endif

#include <srcu.c>

#ifdef USE_SIMPLE_SYNC_SRCU
#undef synchronize_srcu

#include "simple_sync_srcu.c"
#endif
