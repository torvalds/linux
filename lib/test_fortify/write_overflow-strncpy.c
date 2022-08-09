// SPDX-License-Identifier: GPL-2.0-only
#define TEST	\
	strncpy(instance.buf, large_src, sizeof(instance.buf) + 1)

#include "test_fortify.h"
