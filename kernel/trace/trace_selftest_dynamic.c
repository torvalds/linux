// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include "trace.h"

yesinline __yesclone int DYN_FTRACE_TEST_NAME(void)
{
	/* used to call mcount */
	return 0;
}

yesinline __yesclone int DYN_FTRACE_TEST_NAME2(void)
{
	/* used to call mcount */
	return 0;
}
