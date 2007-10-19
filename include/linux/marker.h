#ifndef _LINUX_MARKER_H
#define _LINUX_MARKER_H

/*
 * Code markup for dynamic and static tracing.
 *
 * See Documentation/marker.txt.
 *
 * (C) Copyright 2006 Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#include <linux/types.h>

struct module;
struct marker;

/**
 * marker_probe_func - Type of a marker probe function
 * @mdata: pointer of type struct marker
 * @private_data: caller site private data
 * @fmt: format string
 * @...: variable argument list
 *
 * Type of marker probe functions. They receive the mdata and need to parse the
 * format string to recover the variable argument list.
 */
typedef void marker_probe_func(const struct marker *mdata,
	void *private_data, const char *fmt, ...);

struct marker {
	const char *name;	/* Marker name */
	const char *format;	/* Marker format string, describing the
				 * variable argument list.
				 */
	char state;		/* Marker state. */
	marker_probe_func *call;/* Probe handler function pointer */
	void *private;		/* Private probe data */
} __attribute__((aligned(8)));

#ifdef CONFIG_MARKERS

/*
 * Note : the empty asm volatile with read constraint is used here instead of a
 * "used" attribute to fix a gcc 4.1.x bug.
 * Make sure the alignment of the structure in the __markers section will
 * not add unwanted padding between the beginning of the section and the
 * structure. Force alignment to the same alignment as the section start.
 */
#define __trace_mark(name, call_data, format, args...)			\
	do {								\
		static const char __mstrtab_name_##name[]		\
		__attribute__((section("__markers_strings")))		\
		= #name;						\
		static const char __mstrtab_format_##name[]		\
		__attribute__((section("__markers_strings")))		\
		= format;						\
		static struct marker __mark_##name			\
		__attribute__((section("__markers"), aligned(8))) =	\
		{ __mstrtab_name_##name, __mstrtab_format_##name,	\
		0, __mark_empty_function, NULL };			\
		__mark_check_format(format, ## args);			\
		if (unlikely(__mark_##name.state)) {			\
			preempt_disable();				\
			(*__mark_##name.call)				\
				(&__mark_##name, call_data,		\
				format, ## args);			\
			preempt_enable();				\
		}							\
	} while (0)

extern void marker_update_probe_range(struct marker *begin,
	struct marker *end, struct module *probe_module, int *refcount);
#else /* !CONFIG_MARKERS */
#define __trace_mark(name, call_data, format, args...) \
		__mark_check_format(format, ## args)
static inline void marker_update_probe_range(struct marker *begin,
	struct marker *end, struct module *probe_module, int *refcount)
{ }
#endif /* CONFIG_MARKERS */

/**
 * trace_mark - Marker
 * @name: marker name, not quoted.
 * @format: format string
 * @args...: variable argument list
 *
 * Places a marker.
 */
#define trace_mark(name, format, args...) \
	__trace_mark(name, NULL, format, ## args)

#define MARK_MAX_FORMAT_LEN	1024

/**
 * MARK_NOARGS - Format string for a marker with no argument.
 */
#define MARK_NOARGS " "

/* To be used for string format validity checking with gcc */
static inline void __printf(1, 2) __mark_check_format(const char *fmt, ...)
{
}

extern marker_probe_func __mark_empty_function;

/*
 * Connect a probe to a marker.
 * private data pointer must be a valid allocated memory address, or NULL.
 */
extern int marker_probe_register(const char *name, const char *format,
				marker_probe_func *probe, void *private);

/*
 * Returns the private data given to marker_probe_register.
 */
extern void *marker_probe_unregister(const char *name);
/*
 * Unregister a marker by providing the registered private data.
 */
extern void *marker_probe_unregister_private_data(void *private);

extern int marker_arm(const char *name);
extern int marker_disarm(const char *name);
extern void *marker_get_private_data(const char *name);

#endif
