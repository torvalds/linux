#ifndef _LIBLOCKDEP_DEBUG_LOCKS_H_
#define _LIBLOCKDEP_DEBUG_LOCKS_H_

#include <stddef.h>
#include <linux/compiler.h>

#define DEBUG_LOCKS_WARN_ON(x) (x)

extern bool debug_locks;
extern bool debug_locks_silent;

#endif
