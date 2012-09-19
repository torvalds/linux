#ifndef __PERF_CPUMAP_H
#define __PERF_CPUMAP_H

#include <stdio.h>

struct cpu_map {
	int nr;
	int map[];
};

struct cpu_map *cpu_map__new(const char *cpu_list);
struct cpu_map *cpu_map__dummy_new(void);
void cpu_map__delete(struct cpu_map *map);
struct cpu_map *cpu_map__read(FILE *file);
size_t cpu_map__fprintf(struct cpu_map *map, FILE *fp);

#endif /* __PERF_CPUMAP_H */
