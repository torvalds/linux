#ifndef _PROBE_FINDER_H
#define _PROBE_FINDER_H

#include <stdbool.h>
#include "util.h"
#include "probe-event.h"

#define MAX_PATH_LEN		 256
#define MAX_PROBE_BUFFER	1024
#define MAX_PROBES		 128

static inline int is_c_varname(const char *name)
{
	/* TODO */
	return isalpha(name[0]) || name[0] == '_';
}

#ifdef DWARF_SUPPORT
/* Find kprobe_trace_events specified by perf_probe_event from debuginfo */
extern int find_kprobe_trace_events(int fd, struct perf_probe_event *pev,
				    struct kprobe_trace_event **tevs,
				    int max_tevs);

/* Find a perf_probe_point from debuginfo */
extern int find_perf_probe_point(int fd, unsigned long addr,
				 struct perf_probe_point *ppt);

extern int find_line_range(int fd, struct line_range *lr);

#include <dwarf.h>
#include <libdw.h>
#include <version.h>

struct probe_finder {
	struct perf_probe_event	*pev;		/* Target probe event */
	struct kprobe_trace_event *tevs;	/* Result trace events */
	int			ntevs;		/* Number of trace events */
	int			max_tevs;	/* Max number of trace events */

	/* For function searching */
	int			lno;		/* Line number */
	Dwarf_Addr		addr;		/* Address */
	const char		*fname;		/* Real file name */
	Dwarf_Die		cu_die;		/* Current CU */
	struct list_head	lcache;		/* Line cache for lazy match */

	/* For variable searching */
#if _ELFUTILS_PREREQ(0, 142)
	Dwarf_CFI		*cfi;		/* Call Frame Information */
#endif
	Dwarf_Op		*fb_ops;	/* Frame base attribute */
	struct perf_probe_arg	*pvar;		/* Current target variable */
	struct kprobe_trace_arg	*tvar;		/* Current result variable */
};

struct line_finder {
	struct line_range	*lr;		/* Target line range */

	const char		*fname;		/* File name */
	int			lno_s;		/* Start line number */
	int			lno_e;		/* End line number */
	Dwarf_Die		cu_die;		/* Current CU */
	int			found;
};

#endif /* DWARF_SUPPORT */

#endif /*_PROBE_FINDER_H */
