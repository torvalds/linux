#ifndef __TRACE_STAT_H
#define __TRACE_STAT_H

#include <linux/seq_file.h>

/*
 * If you want to provide a stat file (one-shot statistics), fill
 * an iterator with stat_start/stat_next and a stat_show callbacks.
 * The others callbacks are optional.
 */
struct tracer_stat {
	/* The name of your stat file */
	const char		*name;
	/* Iteration over statistic entries */
	void			*(*stat_start)(struct tracer_stat *trace);
	void			*(*stat_next)(void *prev, int idx);
	/* Compare two entries for stats sorting */
	int			(*stat_cmp)(void *p1, void *p2);
	/* Print a stat entry */
	int			(*stat_show)(struct seq_file *s, void *p);
	/* Release an entry */
	void			(*stat_release)(void *stat);
	/* Print the headers of your stat entries */
	int			(*stat_headers)(struct seq_file *s);
};

/*
 * Destroy or create a stat file
 */
extern int register_stat_tracer(struct tracer_stat *trace);
extern void unregister_stat_tracer(struct tracer_stat *trace);

#endif /* __TRACE_STAT_H */
