#include "../perf.h"
#include "util.h"
#include "trace-event.h"

int common_pc(struct scripting_context *context)
{
	return parse_common_pc(context->event_data);
}

int common_flags(struct scripting_context *context)
{
	return parse_common_flags(context->event_data);
}

int common_lock_depth(struct scripting_context *context)
{
	return parse_common_lock_depth(context->event_data);
}
