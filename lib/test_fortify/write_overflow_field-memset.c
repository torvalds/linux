// SPDX-License-Identifier: GPL-2.0-only
#define TEST	\
	memset(instance.buf, 0x42, sizeof(instance.buf) + 1)

#include "test_fortify.h"
