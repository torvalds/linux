/*
 * builtin-diff.c
 *
 * Builtin diff command: Analyze two perf.data input files, look up and read
 * DSOs and symbol information, sort them and produce a diff.
 */
#include "builtin.h"

#include "util/debug.h"
#include "util/event.h"
#include "util/hist.h"
#include "util/session.h"
#include "util/sort.h"
#include "util/symbol.h"
#include "util/util.h"

#include <stdlib.h>

static char	   const *input_old = "perf.data.old",
			 *input_new = "perf.data";
static int	   force;
static bool 	   show_percent;

struct symbol_conf symbol_conf;

static int perf_session__add_hist_entry(struct perf_session *self,
					struct addr_location *al, u64 count)
{
	bool hit;
	struct hist_entry *he = __perf_session__add_hist_entry(self, al, NULL,
							       count, &hit);
	if (he == NULL)
		return -ENOMEM;

	if (hit)
		he->count += count;

	return 0;
}

static int diff__process_sample_event(event_t *event, struct perf_session *session)
{
	struct addr_location al;
	struct sample_data data = { .period = 1, };

	dump_printf("(IP, %d): %d: %p\n", event->header.misc,
		    event->ip.pid, (void *)(long)event->ip.ip);

	if (event__preprocess_sample(event, session, &al, NULL) < 0) {
		pr_warning("problem processing %d event, skipping it.\n",
			   event->header.type);
		return -1;
	}

	event__parse_sample(event, session->sample_type, &data);

	if (al.sym && perf_session__add_hist_entry(session, &al, data.period)) {
		pr_warning("problem incrementing symbol count, skipping event\n");
		return -1;
	}

	session->events_stats.total += data.period;
	return 0;
}

static struct perf_event_ops event_ops = {
	.process_sample_event = diff__process_sample_event,
	.process_mmap_event   = event__process_mmap,
	.process_comm_event   = event__process_comm,
	.process_exit_event   = event__process_task,
	.process_fork_event   = event__process_task,
	.process_lost_event   = event__process_lost,
};

static void perf_session__insert_hist_entry_by_name(struct rb_root *root,
						    struct hist_entry *he)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;

	while (*p != NULL) {
		int cmp;
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node);

		cmp = strcmp(he->map->dso->name, iter->map->dso->name);
		if (cmp > 0)
			p = &(*p)->rb_left;
		else if (cmp < 0)
			p = &(*p)->rb_right;
		else {
			cmp = strcmp(he->sym->name, iter->sym->name);
			if (cmp > 0)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;
		}
	}

	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color(&he->rb_node, root);
}

static void perf_session__resort_by_name(struct perf_session *self)
{
	unsigned long position = 1;
	struct rb_root tmp = RB_ROOT;
	struct rb_node *next = rb_first(&self->hists);

	while (next != NULL) {
		struct hist_entry *n = rb_entry(next, struct hist_entry, rb_node);

		next = rb_next(&n->rb_node);
		rb_erase(&n->rb_node, &self->hists);
		n->position = position++;
		perf_session__insert_hist_entry_by_name(&tmp, n);
	}

	self->hists = tmp;
}

static struct hist_entry *
perf_session__find_hist_entry_by_name(struct perf_session *self,
				      struct hist_entry *he)
{
	struct rb_node *n = self->hists.rb_node;

	while (n) {
		struct hist_entry *iter = rb_entry(n, struct hist_entry, rb_node);
		int cmp = strcmp(he->map->dso->name, iter->map->dso->name);

		if (cmp > 0)
			n = n->rb_left;
		else if (cmp < 0)
			n = n->rb_right;
		else {
			cmp = strcmp(he->sym->name, iter->sym->name);
			if (cmp > 0)
				n = n->rb_left;
			else if (cmp < 0)
				n = n->rb_right;
			else
				return iter;
		}
	}

	return NULL;
}

static void perf_session__match_hists(struct perf_session *old_session,
				      struct perf_session *new_session)
{
	struct rb_node *nd;

	perf_session__resort_by_name(old_session);

	for (nd = rb_first(&new_session->hists); nd; nd = rb_next(nd)) {
		struct hist_entry *pos = rb_entry(nd, struct hist_entry, rb_node);
		pos->pair = perf_session__find_hist_entry_by_name(old_session, pos);
	}
}

static size_t hist_entry__fprintf_matched(struct hist_entry *self,
					  unsigned long pos,
					  struct perf_session *session,
					  struct perf_session *pair_session,
					  FILE *fp)
{
	u64 old_count = 0;
	char displacement[16];
	size_t printed;

	if (self->pair != NULL) {
		long pdiff = (long)self->pair->position - (long)pos;
		old_count = self->pair->count;
		if (pdiff == 0)
			goto blank;
		snprintf(displacement, sizeof(displacement), "%+4ld", pdiff);
	} else {
blank:		memset(displacement, ' ', sizeof(displacement));
	}

	printed = fprintf(fp, "%4lu %5.5s ", pos, displacement);

	if (show_percent) {
		double old_percent = (old_count * 100) / pair_session->events_stats.total,
		       new_percent = (self->count * 100) / session->events_stats.total;
		double diff = old_percent - new_percent;

		if (verbose)
			printed += fprintf(fp, " %3.2f%% %3.2f%%", old_percent, new_percent);

		if ((u64)diff != 0)
			printed += fprintf(fp, " %+4.2F%%", diff);
		else
			printed += fprintf(fp, "       ");
	} else {
		if (verbose)
			printed += fprintf(fp, " %9Lu %9Lu", old_count, self->count);
		printed += fprintf(fp, " %+9Ld", (s64)self->count - (s64)old_count);
	}

	return printed + fprintf(fp, " %25.25s   %s\n",
				 self->map->dso->name, self->sym->name);
}

static size_t perf_session__fprintf_matched_hists(struct perf_session *self,
						  struct perf_session *pair,
						  FILE *fp)
{
	struct rb_node *nd;
	size_t printed = 0;
	unsigned long pos = 1;

	for (nd = rb_first(&self->hists); nd; nd = rb_next(nd)) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		printed += hist_entry__fprintf_matched(he, pos++, self, pair, fp);
	}

	return printed;
}

static int __cmd_diff(void)
{
	int ret, i;
	struct perf_session *session[2];

	session[0] = perf_session__new(input_old, O_RDONLY, force, &symbol_conf);
	session[1] = perf_session__new(input_new, O_RDONLY, force, &symbol_conf);
	if (session[0] == NULL || session[1] == NULL)
		return -ENOMEM;

	for (i = 0; i < 2; ++i) {
		ret = perf_session__process_events(session[i], &event_ops);
		if (ret)
			goto out_delete;
		perf_session__output_resort(session[i], session[i]->events_stats.total);
	}

	perf_session__match_hists(session[0], session[1]);
	perf_session__fprintf_matched_hists(session[1], session[0], stdout);
out_delete:
	for (i = 0; i < 2; ++i)
		perf_session__delete(session[i]);
	return ret;
}

static const char *const diff_usage[] = {
	"perf diff [<options>] [old_file] [new_file]",
};

static const struct option options[] = {
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_BOOLEAN('m', "modules", &symbol_conf.use_modules,
		    "load module symbols - WARNING: use only with -k and LIVE kernel"),
	OPT_BOOLEAN('p', "percentages", &show_percent,
		    "Don't shorten the pathnames taking into account the cwd"),
	OPT_BOOLEAN('P', "full-paths", &event_ops.full_paths,
		    "Don't shorten the pathnames taking into account the cwd"),
	OPT_END()
};

int cmd_diff(int argc, const char **argv, const char *prefix __used)
{
	if (symbol__init(&symbol_conf) < 0)
		return -1;

	setup_sorting(diff_usage, options);

	argc = parse_options(argc, argv, options, diff_usage, 0);
	if (argc) {
		if (argc > 2)
			usage_with_options(diff_usage, options);
		if (argc == 2) {
			input_old = argv[0];
			input_new = argv[1];
		} else
			input_new = argv[0];
	}

	setup_pager();
	return __cmd_diff();
}
