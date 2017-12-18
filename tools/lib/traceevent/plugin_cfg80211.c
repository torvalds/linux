// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <endian.h>
#include "event-parse.h"

/*
 * From glibc endian.h, for older systems where it is not present, e.g.: RHEL5,
 * Fedora6.
 */
#ifndef le16toh
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define le16toh(x) (x)
# else
#  define le16toh(x) __bswap_16 (x)
# endif
#endif


static unsigned long long
process___le16_to_cpup(struct trace_seq *s, unsigned long long *args)
{
	uint16_t *val = (uint16_t *) (unsigned long) args[0];
	return val ? (long long) le16toh(*val) : 0;
}

int PEVENT_PLUGIN_LOADER(struct pevent *pevent)
{
	pevent_register_print_function(pevent,
				       process___le16_to_cpup,
				       PEVENT_FUNC_ARG_INT,
				       "__le16_to_cpup",
				       PEVENT_FUNC_ARG_PTR,
				       PEVENT_FUNC_ARG_VOID);
	return 0;
}

void PEVENT_PLUGIN_UNLOADER(struct pevent *pevent)
{
	pevent_unregister_print_function(pevent, process___le16_to_cpup,
					 "__le16_to_cpup");
}
