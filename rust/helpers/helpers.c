// SPDX-License-Identifier: GPL-2.0
/*
 * Non-trivial C macros cannot be used in Rust. Similarly, inlined C functions
 * cannot be called either. This file explicitly creates functions ("helpers")
 * that wrap those so that they can be called from Rust.
 *
 * Sorted alphabetically.
 */

#include "blk.c"
#include "bug.c"
#include "build_assert.c"
#include "build_bug.c"
#include "cred.c"
#include "err.c"
#include "fs.c"
#include "jump_label.c"
#include "kunit.c"
#include "mutex.c"
#include "page.c"
#include "pid_namespace.c"
#include "rbtree.c"
#include "refcount.c"
#include "security.c"
#include "signal.c"
#include "slab.c"
#include "spinlock.c"
#include "task.c"
#include "uaccess.c"
#include "wait.c"
#include "workqueue.c"
