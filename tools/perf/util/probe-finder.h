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
/* Find probe_trace_events specified by perf_probe_event from debuginfo */
extern int find_probe_trace_events(int fd, struct perf_probe_event *pev,
				    struct probe_trace_event **tevs,
				    int max_tevs);

/* Find a perf_probe_point from debuginfo */
extern int find_perf_probe_point(unsigned long addr,
				 struct perf_probe_point *ppt);

/* Find a line range */
extern int find_line_range(int fd, struct line_range *lr);

/* Find available variables */
extern int find_available_vars_at(int fd, struct perf_probe_event *pev,
				  struct variable_list **vls, int max_points,
				  bool externs);

#include <dwarf.h>
#include <libdw.h>
#include <libdwfl.h>
#include <version.h>

struct probe_finder {
	struct perf_probe_event	*pev;		/* Target probe event */

	/* Callback when a probe point is found */
	int (*callback)(Dwarf_Die *sp_die, struct probe_finder *pf);

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
	struct probe_trace_arg	*tvar;		/* Current result variable */
};

struct trace_event_finder {
	struct probe_finder	pf;
	struct probe_trace_event *tevs;		/* Found trace events */
	int			ntevs;		/* Number of trace events */
	int			max_tevs;	/* Max number of trace events */
};

struct available_var_finder {
	struct probe_finder	pf;
	struct variable_list	*vls;		/* Found variable lists */
	int			nvls;		/* Number of variable lists */
	int			max_vls;	/* Max no. of variable lists */
	bool			externs;	/* Find external vars too */
	bool			child;		/* Search child scopes */
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
