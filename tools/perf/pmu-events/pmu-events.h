#ifndef PMU_EVENTS_H
#define PMU_EVENTS_H

/*
 * Describe each PMU event. Each CPU has a table of PMU events.
 */
struct pmu_event {
	const char *name;
	const char *event;
	const char *desc;
	const char *topic;
};

/*
 *
 * Map a CPU to its table of PMU events. The CPU is identified by the
 * cpuid field, which is an arch-specific identifier for the CPU.
 * The identifier specified in tools/perf/pmu-events/arch/xxx/mapfile
 * must match the get_cpustr() in tools/perf/arch/xxx/util/header.c)
 *
 * The  cpuid can contain any character other than the comma.
 */
struct pmu_events_map {
	const char *cpuid;
	const char *version;
	const char *type;		/* core, uncore etc */
	struct pmu_event *table;
};

/*
 * Global table mapping each known CPU for the architecture to its
 * table of PMU events.
 */
extern struct pmu_events_map pmu_events_map[];

#endif
