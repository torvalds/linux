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

#ifndef NO_DWARF_SUPPORT
/* Find kprobe_trace_events specified by perf_probe_event from debuginfo */
extern int find_kprobe_trace_events(int fd, struct perf_probe_event *pev,
				    struct kprobe_trace_event **tevs);

/* Find a perf_probe_point from debuginfo */
extern int find_perf_probe_point(int fd, unsigned long addr,
				 struct perf_probe_point *ppt);

extern int find_line_range(int fd, struct line_range *lr);

#include <dwarf.h>
#include <libdw.h>

struct probe_finder {
	struct perf_probe_event	*pev;		/* Target probe event */
	int			ntevs;		/* number of trace events */
	struct kprobe_trace_event *tevs;	/* Result trace events */

	/* For function searching */
	Dwarf_Addr		addr;		/* Address */
	const char		*fname;		/* Real file name */
	int			lno;		/* Line number */
	Dwarf_Die		cu_die;		/* Current CU */
	struct list_head	lcache;		/* Line cache for lazy match */

	/* For variable searching */
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

#endif /* NO_DWARF_SUPPORT */

#endif /*_PROBE_FINDER_H */
