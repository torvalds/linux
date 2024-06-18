/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_ADDR_LOCATION
#define __PERF_ADDR_LOCATION 1

#include <linux/types.h>

struct thread;
struct maps;
struct map;
struct symbol;

struct addr_location {
	struct thread *thread;
	struct maps   *maps;
	struct map    *map;
	struct symbol *sym;
	const char    *srcline;
	u64	      addr;
	char	      level;
	u8	      filtered;
	u8	      cpumode;
	s32	      cpu;
	s32	      socket;
};

void addr_location__init(struct addr_location *al);
void addr_location__exit(struct addr_location *al);

void addr_location__copy(struct addr_location *dst, struct addr_location *src);

#endif /* __PERF_ADDR_LOCATION */
