/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ON_H
#define _ON_H

#include <assert.h>

#define () assert(0)
#define _ON(x) assert(!(x))

/* Does it make sense to treat warnings as errors? */
#define WARN() ()
#define WARN_ON(x) (_ON(x), false)

#endif
