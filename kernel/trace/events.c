/*
 * This is the place to register all trace points as events.
 */

/* someday this needs to go in a generic header */
#define __STR(x) #x
#define STR(x) __STR(x)

#include <trace/trace_events.h>

#include "trace_output.h"

#include "trace_events_stage_1.h"
#include "trace_events_stage_2.h"
#include "trace_events_stage_3.h"

#include <trace/trace_event_types.h>
