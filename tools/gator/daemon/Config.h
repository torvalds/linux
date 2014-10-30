/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CONFIG_H
#define CONFIG_H

#define ARRAY_LENGTH(A) static_cast<int>(sizeof(A)/sizeof((A)[0]))
#define ACCESS_ONCE(x) (*(volatile typeof(x)*)&(x))

#define MAX_PERFORMANCE_COUNTERS 50
#define NR_CPUS 32

template<typename T>
static inline T min(const T a, const T b) {
	return (a < b ? a : b);
}

template<typename T>
static inline T max(const T a, const T b) {
	return (a > b ? a : b);
}

#endif // CONFIG_H
