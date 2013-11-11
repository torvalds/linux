#ifndef _PROBE_FINDER_H
#define _PROBE_FINDER_H

#include <stdbool.h>
#include "util.h"
#include "probe-event.h"

#define MAX_PROBE_BUFFER	1024
#define MAX_PROBES		 128

static inline int is_c_varname(const char *name)
{
	/* TODO */
	return isalpha(name[0]) || name[0] == '_';
}

#ifdef DWARF_SUPPORT

#include "dwarf-aux.h"

/* TODO: export debuginfo data structure even if no dwarf support */

/* debug information structure */
struct debuginfo {
	Dwarf		*dbg;
	Dwfl		*dwfl;
	Dwarf_Addr	bias;
};

extern struct debuginfo *debuginfo__new(const char *path);
extern struct debuginfo *debuginfo__new_online_kernel(unsigned long addr);
extern void debuginfo__delete(struct debuginfo *self);

/* Find probe_trace_events specified by perf_probe_event from debuginfo */
extern int debuginfo__find_trace_events(struct debuginfo *self,
					struct perf_probe_event *pev,
					struct probe_trace_event **tevs,
					int max_tevs);

/* Find a perf_probe_point from debuginfo */
extern int debuginfo__find_probe_point(struct debuginfo *self,
				       unsigned long addr,
				       struct perf_probe_point *ppt);

/* Find a line range */
extern int debuginfo__find_line_range(struct debuginfo *self,
				      struct line_range *lr);

/* Find available variables */
extern int debuginfo__find_available_vars_at(struct debuginfo *self,
					     struct perf_probe_event *pev,
					     struct variable_list **vls,
					     int max_points, bool externs);

struct probe_finder {
	struct perf_probe_event	*pev;		/* Target probe event */

	/* Callback when a probe point is found */
	int (*callback)(Dwarf_Die *sc_die, struct probe_finder *pf);

	/* For function searching */
	int			lno;		/* Line number */
	Dwarf_Addr		addr;		/* Address */
	const char		*fname;		/* Real file name */
	Dwarf_Die		cu_die;		/* Current CU */
	Dwarf_Die		sp_die;
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
	Dwarf_Die		sp_die;
	int			found;
};

#endif /* DWARF_SUPPORT */

#endif /*_PROBE_FINDER_H */
