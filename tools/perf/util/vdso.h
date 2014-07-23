#ifndef __PERF_VDSO__
#define __PERF_VDSO__

#include <linux/types.h>
#include <string.h>
#include <stdbool.h>

#define VDSO__MAP_NAME "[vdso]"

static inline bool is_vdso_map(const char *filename)
{
	return !strcmp(filename, VDSO__MAP_NAME);
}

struct machine;

struct dso *vdso__dso_findnew(struct machine *machine);
void vdso__exit(struct machine *machine);

#endif /* __PERF_VDSO__ */
