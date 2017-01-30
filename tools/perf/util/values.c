#include <stdlib.h>

#include "util.h"
#include "values.h"
#include "debug.h"

int perf_read_values_init(struct perf_read_values *values)
{
	values->threads_max = 16;
	values->pid = malloc(values->threads_max * sizeof(*values->pid));
	values->tid = malloc(values->threads_max * sizeof(*values->tid));
	values->value = malloc(values->threads_max * sizeof(*values->value));
	if (!values->pid || !values->tid || !values->value) {
		pr_debug("failed to allocate read_values threads arrays");
		goto out_free_pid;
	}
	values->threads = 0;

	values->counters_max = 16;
	values->counterrawid = malloc(values->counters_max
				      * sizeof(*values->counterrawid));
	values->countername = malloc(values->counters_max
				     * sizeof(*values->countername));
	if (!values->counterrawid || !values->countername) {
		pr_debug("failed to allocate read_values counters arrays");
		goto out_free_counter;
	}
	values->counters = 0;

	return 0;

out_free_counter:
	zfree(&values->counterrawid);
	zfree(&values->countername);
out_free_pid:
	zfree(&values->pid);
	zfree(&values->tid);
	zfree(&values->value);
	return -ENOMEM;
}

void perf_read_values_destroy(struct perf_read_values *values)
{
	int i;

	if (!values->threads_max || !values->counters_max)
		return;

	for (i = 0; i < values->threads; i++)
		zfree(&values->value[i]);
	zfree(&values->value);
	zfree(&values->pid);
	zfree(&values->tid);
	zfree(&values->counterrawid);
	for (i = 0; i < values->counters; i++)
		zfree(&values->countername[i]);
	zfree(&values->countername);
}

static int perf_read_values__enlarge_threads(struct perf_read_values *values)
{
	int nthreads_max = values->threads_max * 2;
	void *npid = realloc(values->pid, nthreads_max * sizeof(*values->pid)),
	     *ntid = realloc(values->tid, nthreads_max * sizeof(*values->tid)),
	     *nvalue = realloc(values->value, nthreads_max * sizeof(*values->value));

	if (!npid || !ntid || !nvalue)
		goto out_err;

	values->threads_max = nthreads_max;
	values->pid = npid;
	values->tid = ntid;
	values->value = nvalue;
	return 0;
out_err:
	free(npid);
	free(ntid);
	free(nvalue);
	pr_debug("failed to enlarge read_values threads arrays");
	return -ENOMEM;
}

static int perf_read_values__findnew_thread(struct perf_read_values *values,
					    u32 pid, u32 tid)
{
	int i;

	for (i = 0; i < values->threads; i++)
		if (values->pid[i] == pid && values->tid[i] == tid)
			return i;

	if (values->threads == values->threads_max) {
		i = perf_read_values__enlarge_threads(values);
		if (i < 0)
			return i;
	}

	i = values->threads + 1;
	values->value[i] = malloc(values->counters_max * sizeof(**values->value));
	if (!values->value[i]) {
		pr_debug("failed to allocate read_values counters array");
		return -ENOMEM;
	}
	values->pid[i] = pid;
	values->tid[i] = tid;
	values->threads = i;

	return i;
}

static void perf_read_values__enlarge_counters(struct perf_read_values *values)
{
	int i;

	values->counters_max *= 2;
	values->counterrawid = realloc(values->counterrawid,
				       values->counters_max * sizeof(*values->counterrawid));
	values->countername = realloc(values->countername,
				      values->counters_max * sizeof(*values->countername));
	if (!values->counterrawid || !values->countername)
		die("failed to enlarge read_values counters arrays");

	for (i = 0; i < values->threads; i++) {
		values->value[i] = realloc(values->value[i],
					   values->counters_max * sizeof(**values->value));
		if (!values->value[i])
			die("failed to enlarge read_values counters arrays");
	}
}

static int perf_read_values__findnew_counter(struct perf_read_values *values,
					     u64 rawid, const char *name)
{
	int i;

	for (i = 0; i < values->counters; i++)
		if (values->counterrawid[i] == rawid)
			return i;

	if (values->counters == values->counters_max)
		perf_read_values__enlarge_counters(values);

	i = values->counters++;
	values->counterrawid[i] = rawid;
	values->countername[i] = strdup(name);

	return i;
}

int perf_read_values_add_value(struct perf_read_values *values,
				u32 pid, u32 tid,
				u64 rawid, const char *name, u64 value)
{
	int tindex, cindex;

	tindex = perf_read_values__findnew_thread(values, pid, tid);
	if (tindex < 0)
		return tindex;
	cindex = perf_read_values__findnew_counter(values, rawid, name);
	if (cindex < 0)
		return cindex;

	values->value[tindex][cindex] = value;
	return 0;
}

static void perf_read_values__display_pretty(FILE *fp,
					     struct perf_read_values *values)
{
	int i, j;
	int pidwidth, tidwidth;
	int *counterwidth;

	counterwidth = malloc(values->counters * sizeof(*counterwidth));
	if (!counterwidth)
		die("failed to allocate counterwidth array");
	tidwidth = 3;
	pidwidth = 3;
	for (j = 0; j < values->counters; j++)
		counterwidth[j] = strlen(values->countername[j]);
	for (i = 0; i < values->threads; i++) {
		int width;

		width = snprintf(NULL, 0, "%d", values->pid[i]);
		if (width > pidwidth)
			pidwidth = width;
		width = snprintf(NULL, 0, "%d", values->tid[i]);
		if (width > tidwidth)
			tidwidth = width;
		for (j = 0; j < values->counters; j++) {
			width = snprintf(NULL, 0, "%" PRIu64, values->value[i][j]);
			if (width > counterwidth[j])
				counterwidth[j] = width;
		}
	}

	fprintf(fp, "# %*s  %*s", pidwidth, "PID", tidwidth, "TID");
	for (j = 0; j < values->counters; j++)
		fprintf(fp, "  %*s", counterwidth[j], values->countername[j]);
	fprintf(fp, "\n");

	for (i = 0; i < values->threads; i++) {
		fprintf(fp, "  %*d  %*d", pidwidth, values->pid[i],
			tidwidth, values->tid[i]);
		for (j = 0; j < values->counters; j++)
			fprintf(fp, "  %*" PRIu64,
				counterwidth[j], values->value[i][j]);
		fprintf(fp, "\n");
	}
	free(counterwidth);
}

static void perf_read_values__display_raw(FILE *fp,
					  struct perf_read_values *values)
{
	int width, pidwidth, tidwidth, namewidth, rawwidth, countwidth;
	int i, j;

	tidwidth = 3; /* TID */
	pidwidth = 3; /* PID */
	namewidth = 4; /* "Name" */
	rawwidth = 3; /* "Raw" */
	countwidth = 5; /* "Count" */

	for (i = 0; i < values->threads; i++) {
		width = snprintf(NULL, 0, "%d", values->pid[i]);
		if (width > pidwidth)
			pidwidth = width;
		width = snprintf(NULL, 0, "%d", values->tid[i]);
		if (width > tidwidth)
			tidwidth = width;
	}
	for (j = 0; j < values->counters; j++) {
		width = strlen(values->countername[j]);
		if (width > namewidth)
			namewidth = width;
		width = snprintf(NULL, 0, "%" PRIx64, values->counterrawid[j]);
		if (width > rawwidth)
			rawwidth = width;
	}
	for (i = 0; i < values->threads; i++) {
		for (j = 0; j < values->counters; j++) {
			width = snprintf(NULL, 0, "%" PRIu64, values->value[i][j]);
			if (width > countwidth)
				countwidth = width;
		}
	}

	fprintf(fp, "# %*s  %*s  %*s  %*s  %*s\n",
		pidwidth, "PID", tidwidth, "TID",
		namewidth, "Name", rawwidth, "Raw",
		countwidth, "Count");
	for (i = 0; i < values->threads; i++)
		for (j = 0; j < values->counters; j++)
			fprintf(fp, "  %*d  %*d  %*s  %*" PRIx64 "  %*" PRIu64,
				pidwidth, values->pid[i],
				tidwidth, values->tid[i],
				namewidth, values->countername[j],
				rawwidth, values->counterrawid[j],
				countwidth, values->value[i][j]);
}

void perf_read_values_display(FILE *fp, struct perf_read_values *values, int raw)
{
	if (raw)
		perf_read_values__display_raw(fp, values);
	else
		perf_read_values__display_pretty(fp, values);
}
