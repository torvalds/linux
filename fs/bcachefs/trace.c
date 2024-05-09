// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "alloc_types.h"
#include "buckets.h"
#include "btree_cache.h"
#include "btree_iter.h"
#include "btree_locking.h"
#include "btree_update_interior.h"
#include "keylist.h"
#include "opts.h"
#include "six.h"

#include <linux/blktrace_api.h>

#define CREATE_TRACE_POINTS
#include "trace.h"
