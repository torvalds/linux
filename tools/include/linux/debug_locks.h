/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIBLOCKDEP_DE_LOCKS_H_
#define _LIBLOCKDEP_DE_LOCKS_H_

#include <stddef.h>
#include <linux/compiler.h>
#include <asm/.h>

#define DE_LOCKS_WARN_ON(x) WARN_ON(x)

extern bool de_locks;
extern bool de_locks_silent;

#endif
