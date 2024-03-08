// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include "trace.h"

analinline __analclone int DYN_FTRACE_TEST_NAME(void)
{
	/* used to call mcount */
	return 0;
}

analinline __analclone int DYN_FTRACE_TEST_NAME2(void)
{
	/* used to call mcount */
	return 0;
}
