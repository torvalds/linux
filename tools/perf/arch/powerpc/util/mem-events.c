// SPDX-License-Identifier: GPL-2.0
#include "mem-events.h"

/* PowerPC does not support 'ldlat' parameter. */
char *perf_mem_events__name(int i)
{
	if (i == PERF_MEM_EVENTS__LOAD)
		return (char *) "cpu/mem-loads/";

	return (char *) "cpu/mem-stores/";
}
