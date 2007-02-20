#ifndef __ALPHA_PERCPU_H
#define __ALPHA_PERCPU_H

/*
 * Increase the per cpu area for Alpha so that
 * modules using percpu area can load.
 */
#ifdef CONFIG_MODULES
# define PERCPU_MODULE_RESERVE 8192
#else
# define PERCPU_MODULE_RESERVE 0
#endif

#define PERCPU_ENOUGH_ROOM \
	(ALIGN(__per_cpu_end - __per_cpu_start, SMP_CACHE_BYTES) + \
	 PERCPU_MODULE_RESERVE)

#include <asm-generic/percpu.h>

#endif /* __ALPHA_PERCPU_H */
