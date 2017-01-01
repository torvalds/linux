#include <config.h>

/* Include all source files. */

#include "include_srcu.c"

#include "preempt.c"
#include "misc.c"

/* Used by test.c files */
#include <pthread.h>
#include <stdlib.h>
#include <linux/srcu.h>
