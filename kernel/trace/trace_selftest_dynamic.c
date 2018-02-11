// SPDX-License-Identifier: GPL-2.0
#include "trace.h"

int DYN_FTRACE_TEST_NAME(void)
{
	/* used to call mcount */
	return 0;
}

int DYN_FTRACE_TEST_NAME2(void)
{
	/* used to call mcount */
	return 0;
}
