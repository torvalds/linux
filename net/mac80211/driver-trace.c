/* bug in tracepoint.h, it should include this */
#include <linux/module.h>

#include "driver-ops.h"
#define CREATE_TRACE_POINTS
#include "driver-trace.h"
