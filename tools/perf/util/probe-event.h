#ifndef _PROBE_EVENT_H
#define _PROBE_EVENT_H

#include <linux/compiler.h>
#include <stdbool.h>
#include "intlist.h"
#include "namespaces.h"

/* Probe related configurations */
struct probe_conf {
	bool	show_ext_vars;
	bool	show_location_range;
	bool	force_add;
	bool	no_inlines;
	bool	cache;
	int	max_probes;
};
extern struct probe_conf probe_conf;
extern bool probe_event_dry_run;

struct symbol;

/* kprobe-tracer and uprobe-tracer tracing point */
struct probe_trace_point {
	char		*realname;	/* function real name (if needed) */
	char		*symbol;	/* Base symbol */
	char		*module;	/* Module name */
	unsigned long	offset;		/* Offset from symbol */
	unsigned long	address;	/* Actual address of the trace point */
	bool		retprobe;	/* Return probe flag */
};

/* probe-tracer tracing argument referencing offset */
struct probe_trace_arg_ref {
	struct probe_trace_arg_ref	*next;	/* Next reference */
	long				offset;	/* Offset value */
};

/* kprobe-tracer and uprobe-tracer tracing argument */
struct probe_trace_arg {
	char				*name;	/* Argument name */
	char				*value;	/* Base value */
	char				*type;	/* Type name */
	struct probe_trace_arg_ref	*ref;	/* Referencing offset */
};

/* kprobe-tracer and uprobe-tracer tracing event (point + arg) */
struct probe_trace_event {
	char				*event;	/* Event name */
	char				*group;	/* Group name */
	struct probe_trace_point	point;	/* Trace point */
	int				nargs;	/* Number of args */
	bool				uprobes;	/* uprobes only */
	struct probe_trace_arg		*args;	/* Arguments */
};

/* Perf probe probing point */
struct perf_probe_point {
	char		*file;		/* File path */
	char		*function;	/* Function name */
	int		line;		/* Line number */
	bool		retprobe;	/* Return probe flag */
	char		*lazy_line;	/* Lazy matching pattern */
	unsigned long	offset;		/* Offset from function entry */
	unsigned long	abs_address;	/* Absolute address of the point */
};

/* Perf probe probing argument field chain */
struct perf_probe_arg_field {
	struct perf_probe_arg_field	*next;	/* Next field */
	char				*name;	/* Name of the field */
	long				index;	/* Array index number */
	bool				ref;	/* Referencing flag */
};

/* Perf probe probing argument */
struct perf_probe_arg {
	char				*name;	/* Argument name */
	char				*var;	/* Variable name */
	char				*type;	/* Type name */
	struct perf_probe_arg_field	*field;	/* Structure fields */
};

/* Perf probe probing event (point + arg) */
struct perf_probe_event {
	char			*event;	/* Event name */
	char			*group;	/* Group name */
	struct perf_probe_point	point;	/* Probe point */
	int			nargs;	/* Number of arguments */
	bool			sdt;	/* SDT/cached event flag */
	bool			uprobes;	/* Uprobe event flag */
	char			*target;	/* Target binary */
	struct perf_probe_arg	*args;	/* Arguments */
	struct probe_trace_event *tevs;
	int			ntevs;
	struct nsinfo		*nsi;	/* Target namespace */
};

/* Line range */
struct line_range {
	char			*file;		/* File name */
	char			*function;	/* Function name */
	int			start;		/* Start line number */
	int			end;		/* End line number */
	int			offset;		/* Start line offset */
	char			*path;		/* Real path name */
	char			*comp_dir;	/* Compile directory */
	struct intlist		*line_list;	/* Visible lines */
};

struct strlist;

/* List of variables */
struct variable_list {
	struct probe_trace_point	point;	/* Actual probepoint */
	struct strlist			*vars;	/* Available variables */
};

struct map;
int init_probe_symbol_maps(bool user_only);
void exit_probe_symbol_maps(void);

/* Command string to events */
int parse_perf_probe_command(const char *cmd, struct perf_probe_event *pev);
int parse_probe_trace_command(const char *cmd, struct probe_trace_event *tev);

/* Events to command string */
char *synthesize_perf_probe_command(struct perf_probe_event *pev);
char *synthesize_probe_trace_command(struct probe_trace_event *tev);
char *synthesize_perf_probe_arg(struct perf_probe_arg *pa);
char *synthesize_perf_probe_point(struct perf_probe_point *pp);

int perf_probe_event__copy(struct perf_probe_event *dst,
			   struct perf_probe_event *src);

bool perf_probe_with_var(struct perf_probe_event *pev);

/* Check the perf_probe_event needs debuginfo */
bool perf_probe_event_need_dwarf(struct perf_probe_event *pev);

/* Release event contents */
void clear_perf_probe_event(struct perf_probe_event *pev);
void clear_probe_trace_event(struct probe_trace_event *tev);

/* Command string to line-range */
int parse_line_range_desc(const char *cmd, struct line_range *lr);

/* Release line range members */
void line_range__clear(struct line_range *lr);

/* Initialize line range */
int line_range__init(struct line_range *lr);

int add_perf_probe_events(struct perf_probe_event *pevs, int npevs);
int convert_perf_probe_events(struct perf_probe_event *pevs, int npevs);
int apply_perf_probe_events(struct perf_probe_event *pevs, int npevs);
int show_probe_trace_events(struct perf_probe_event *pevs, int npevs);
void cleanup_perf_probe_events(struct perf_probe_event *pevs, int npevs);

struct strfilter;

int del_perf_probe_events(struct strfilter *filter);

int show_perf_probe_event(const char *group, const char *event,
			  struct perf_probe_event *pev,
			  const char *module, bool use_stdout);
int show_perf_probe_events(struct strfilter *filter);
int show_line_range(struct line_range *lr, const char *module,
		    struct nsinfo *nsi, bool user);
int show_available_vars(struct perf_probe_event *pevs, int npevs,
			struct strfilter *filter);
int show_available_funcs(const char *module, struct nsinfo *nsi,
			 struct strfilter *filter, bool user);
void arch__fix_tev_from_maps(struct perf_probe_event *pev,
			     struct probe_trace_event *tev, struct map *map,
			     struct symbol *sym);

/* If there is no space to write, returns -E2BIG. */
int e_snprintf(char *str, size_t size, const char *format, ...) __printf(3, 4);

/* Maximum index number of event-name postfix */
#define MAX_EVENT_INDEX	1024

int copy_to_probe_trace_arg(struct probe_trace_arg *tvar,
			    struct perf_probe_arg *pvar);

struct map *get_target_map(const char *target, struct nsinfo *nsi, bool user);

void arch__post_process_probe_trace_events(struct perf_probe_event *pev,
					   int ntevs);

#endif /*_PROBE_EVENT_H */
