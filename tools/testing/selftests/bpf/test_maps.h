/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TEST_MAPS_H
#define _TEST_MAPS_H

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition, tag, format...) ({				\
	int __ret = !!(condition);					\
	if (__ret) {							\
		printf("%s(%d):FAIL:%s ", __func__, __LINE__, tag);	\
		printf(format);						\
		exit(-1);						\
	}								\
})

#endif
