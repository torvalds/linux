/* bug in tracepoint.h, it should include this */
#include <linux/module.h>

/* sparse isn't too happy with all macros... */
#ifndef __CHECKER__
#include "driver-ops.h"
#define CREATE_TRACE_POINTS
#include "driver-trace.h"
#endif
