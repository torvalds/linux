/*
 * This is the place to register all trace points as events.
 * Include the trace/<type>.h at the top.
 * Include the trace/<type>_event_types.h at the bottom.
 */

/* trace/<type>.h here */
#include <trace/sched.h>
#include <trace/irq.h>

#include "trace_events.h"

/* trace/<type>_event_types.h here */
#include <trace/sched_event_types.h>
#include <trace/irq_event_types.h>
