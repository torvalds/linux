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

#define MAX_PERFORMANCE_COUNTERS 50
#define NR_CPUS 16

#endif // CONFIG_H
