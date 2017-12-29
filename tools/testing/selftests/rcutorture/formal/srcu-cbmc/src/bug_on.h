/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BUG_ON_H
#define BUG_ON_H

#include <assert.h>

#define BUG() assert(0)
#define BUG_ON(x) assert(!(x))

/* Does it make sense to treat warnings as errors? */
#define WARN() BUG()
#define WARN_ON(x) (BUG_ON(x), false)

#endif
