// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_tpebs.c: Intel TPEBS support
 */


#include <sys/param.h>
#include <subcmd/run-command.h>
#include <thread.h>
#include "intel-tpebs.h"
#include <linux/list.h>
#include <linux/zalloc.h>
#include <linux/err.h>
#include "sample.h"
#include "counts.h"
#include "debug.h"
#include "evlist.h"
#include "evsel.h"
#include "session.h"
#include "tool.h"
#include "cpumap.h"
#include "metricgroup.h"
#include "stat.h"
#include <sys/stat.h>
#include <sys/file.h>
#include <poll.h>
#include <math.h>

#define PERF_DATA		"-"

bool tpebs_recording;
static LIST_HEAD(tpebs_results);
static pthread_t tpebs_reader_thread;
static struct child_process tpebs_cmd;

struct tpebs_retire_lat {
	struct list_head nd;
	/** @evsel: The evsel that opened the retire_lat event. */
	struct evsel *evsel;
	/** @event: Event passed to perf record. */
	char *event;
	/* Count of retire_latency values found in sample data */
	size_t count;
	/* Sum of all the retire_latency values in sample data */
	int sum;
	/* Average of retire_latency, val = sum / count */
	double val;
	/* Has the event been sent to perf record? */
	bool started;
};

static struct tpebs_retire_lat *tpebs_retire_lat__find(struct evsel *evsel);

static int evsel__tpebs_start_perf_record(struct evsel *evsel, int control_fd[], int ack_fd[])
{
	const char **record_argv;
	int tpebs_event_size = 0, i = 0, ret;
	char control_fd_buf[32];
	char cpumap_buf[50];
	struct tpebs_retire_lat *t;

	list_for_each_entry(t, &tpebs_results, nd)
		tpebs_event_size++;

	record_argv = malloc((10 + 2 * tpebs_event_size) * sizeof(*record_argv));
	if (!record_argv)
		return -ENOMEM;

	record_argv[i++] = "perf";
	record_argv[i++] = "record";
	record_argv[i++] = "-W";
	record_argv[i++] = "--synth=no";

	scnprintf(control_fd_buf, sizeof(control_fd_buf), "--control=fd:%d,%d",
		  control_fd[0], ack_fd[1]);
	record_argv[i++] = control_fd_buf;

	record_argv[i++] = "-o";
	record_argv[i++] = PERF_DATA;

	if (!perf_cpu_map__is_any_cpu_or_is_empty(evsel->evlist->core.user_requested_cpus)) {
		cpu_map__snprint(evsel->evlist->core.user_requested_cpus, cpumap_buf,
				 sizeof(cpumap_buf));
		record_argv[i++] = "-C";
		record_argv[i++] = cpumap_buf;
	}

	list_for_each_entry(t, &tpebs_results, nd) {
		record_argv[i++] = "-e";
		record_argv[i++] = t->event;
	}
	record_argv[i++] = NULL;
	assert(i == 10 + 2 * tpebs_event_size || i == 8 + 2 * tpebs_event_size);
	/* Note, no workload given so system wide is implied. */

	assert(tpebs_cmd.pid == 0);
	tpebs_cmd.argv = record_argv;
	tpebs_cmd.out = -1;
	ret = start_command(&tpebs_cmd);
	zfree(&tpebs_cmd.argv);
	list_for_each_entry(t, &tpebs_results, nd)
		t->started = true;

	return ret;
}

static int process_sample_event(const struct perf_tool *tool __maybe_unused,
				union perf_event *event __maybe_unused,
				struct perf_sample *sample,
				struct evsel *evsel,
				struct machine *machine __maybe_unused)
{
	struct tpebs_retire_lat *t;

	t = tpebs_retire_lat__find(evsel);
	if (!t)
		return -EINVAL;
	/*
	 * Need to handle per core results? We are assuming average retire
	 * latency value will be used. Save the number of samples and the sum of
	 * retire latency value for each event.
	 */
	t->count += 1;
	t->sum += sample->retire_lat;
	t->val = (double) t->sum / t->count;
	return 0;
}

static int process_feature_event(struct perf_session *session,
				 union perf_event *event)
{
	if (event->feat.feat_id < HEADER_LAST_FEATURE)
		return perf_event__process_feature(session, event);
	return 0;
}

static void *__sample_reader(void *arg __maybe_unused)
{
	struct perf_session *session;
	struct perf_data data = {
		.mode = PERF_DATA_MODE_READ,
		.path = PERF_DATA,
		.file.fd = tpebs_cmd.out,
	};
	struct perf_tool tool;

	perf_tool__init(&tool, /*ordered_events=*/false);
	tool.sample = process_sample_event;
	tool.feature = process_feature_event;
	tool.attr = perf_event__process_attr;

	session = perf_session__new(&data, &tool);
	if (IS_ERR(session))
		return NULL;
	perf_session__process_events(session);
	perf_session__delete(session);

	return NULL;
}

/*
 * tpebs_stop - stop the sample data read thread and the perf record process.
 */
static int tpebs_stop(void)
{
	int ret = 0;

	/* Like tpebs_start, we should only run tpebs_end once. */
	if (tpebs_cmd.pid != 0) {
		kill(tpebs_cmd.pid, SIGTERM);
		pthread_join(tpebs_reader_thread, NULL);
		close(tpebs_cmd.out);
		ret = finish_command(&tpebs_cmd);
		tpebs_cmd.pid = 0;
		if (ret == -ERR_RUN_COMMAND_WAITPID_SIGNAL)
			ret = 0;
	}
	return ret;
}

/**
 * evsel__tpebs_event() - Create string event encoding to pass to `perf record`.
 */
static int evsel__tpebs_event(struct evsel *evsel, char **event)
{
	char *name, *modifier;
	int ret;

	name = strdup(evsel->name);
	if (!name)
		return -ENOMEM;

	modifier = strrchr(name, 'R');
	if (!modifier) {
		ret = -EINVAL;
		goto out;
	}
	*modifier = 'p';
	modifier = strchr(name, ':');
	if (!modifier)
		modifier = strrchr(name, '/');
	if (!modifier) {
		ret = -EINVAL;
		goto out;
	}
	*modifier = '\0';
	if (asprintf(event, "%s/name=tpebs_event_%p/%s", name, evsel, modifier + 1) > 0)
		ret = 0;
	else
		ret = -ENOMEM;
out:
	if (ret)
		pr_err("Tpebs event modifier broken '%s'\n", evsel->name);
	free(name);
	return ret;
}

static struct tpebs_retire_lat *tpebs_retire_lat__new(struct evsel *evsel)
{
	struct tpebs_retire_lat *result = zalloc(sizeof(*result));
	int ret;

	if (!result)
		return NULL;

	ret = evsel__tpebs_event(evsel, &result->event);
	if (ret) {
		free(result);
		return NULL;
	}
	result->evsel = evsel;
	list_add_tail(&result->nd, &tpebs_results);
	return result;
}

static void tpebs_retire_lat__delete(struct tpebs_retire_lat *r)
{
	zfree(&r->event);
	free(r);
}

static struct tpebs_retire_lat *tpebs_retire_lat__find(struct evsel *evsel)
{
	struct tpebs_retire_lat *t;
	unsigned long num;
	const char *evsel_name;

	/*
	 * Evsels will match for evlist with the retirement latency event. The
	 * name with "tpebs_event_" prefix will be present on events being read
	 * from `perf record`.
	 */
	if (evsel__is_retire_lat(evsel)) {
		list_for_each_entry(t, &tpebs_results, nd) {
			if (t->evsel == evsel)
				return t;
		}
		return NULL;
	}
	evsel_name = strstr(evsel->name, "tpebs_event_");
	if (!evsel_name) {
		/* Unexpected that the perf record should have other events. */
		return NULL;
	}
	errno = 0;
	num = strtoull(evsel_name + 12, NULL, 16);
	if (errno) {
		pr_err("Bad evsel for tpebs find '%s'\n", evsel->name);
		return NULL;
	}
	list_for_each_entry(t, &tpebs_results, nd) {
		if ((unsigned long)t->evsel == num)
			return t;
	}
	return NULL;
}

/**
 * evsel__tpebs_prepare - create tpebs data structures ready for opening.
 * @evsel: retire_latency evsel, all evsels on its list will be prepared.
 */
static int evsel__tpebs_prepare(struct evsel *evsel)
{
	struct evsel *pos;
	struct tpebs_retire_lat *tpebs_event = tpebs_retire_lat__find(evsel);

	if (tpebs_event) {
		/* evsel, or an identically named one, was already prepared. */
		return 0;
	}
	tpebs_event = tpebs_retire_lat__new(evsel);
	if (!tpebs_event)
		return -ENOMEM;

	/*
	 * Eagerly prepare all other evsels on the list to try to ensure that by
	 * open they are all known.
	 */
	evlist__for_each_entry(evsel->evlist, pos) {
		int ret;

		if (pos == evsel || !pos->retire_lat)
			continue;

		ret = evsel__tpebs_prepare(pos);
		if (ret)
			return ret;
	}
	return 0;
}

/**
 * evsel__tpebs_open - starts tpebs execution.
 * @evsel: retire_latency evsel, all evsels on its list will be selected. Each
 *         evsel is sampled to get the average retire_latency value.
 */
int evsel__tpebs_open(struct evsel *evsel)
{
	int ret;

	/* We should only run tpebs_start when tpebs_recording is enabled. */
	if (!tpebs_recording)
		return 0;
	/* Only start the events once. */
	if (tpebs_cmd.pid != 0) {
		struct tpebs_retire_lat *t = tpebs_retire_lat__find(evsel);

		if (!t || !t->started) {
			/* Fail, as the event wasn't started. */
			return -EBUSY;
		}
		return 0;
	}

	ret = evsel__tpebs_prepare(evsel);
	if (ret)
		return ret;

	if (!list_empty(&tpebs_results)) {
		struct pollfd pollfd = { .events = POLLIN, };
		int control_fd[2], ack_fd[2], len;
		char ack_buf[8];

		/*Create control and ack fd for --control*/
		if (pipe(control_fd) < 0) {
			pr_err("tpebs: Failed to create control fifo");
			ret = -1;
			goto out;
		}
		if (pipe(ack_fd) < 0) {
			pr_err("tpebs: Failed to create control fifo");
			ret = -1;
			goto out;
		}

		ret = evsel__tpebs_start_perf_record(evsel, control_fd, ack_fd);
		if (ret)
			goto out;

		if (pthread_create(&tpebs_reader_thread, /*attr=*/NULL, __sample_reader,
				   /*arg=*/NULL)) {
			kill(tpebs_cmd.pid, SIGTERM);
			close(tpebs_cmd.out);
			pr_err("Could not create thread to process sample data.\n");
			ret = -1;
			goto out;
		}
		/* Wait for perf record initialization.*/
		len = strlen(EVLIST_CTL_CMD_ENABLE_TAG);
		ret = write(control_fd[1], EVLIST_CTL_CMD_ENABLE_TAG, len);
		if (ret != len) {
			pr_err("perf record control write control message failed\n");
			goto out;
		}

		/* wait for an ack */
		pollfd.fd = ack_fd[0];

		/*
		 * We need this poll to ensure the ack_fd PIPE will not hang
		 * when perf record failed for any reason. The timeout value
		 * 3000ms is an empirical selection.
		 */
		if (!poll(&pollfd, 1, 3000)) {
			pr_err("tpebs failed: perf record ack timeout\n");
			ret = -1;
			goto out;
		}

		if (!(pollfd.revents & POLLIN)) {
			pr_err("tpebs failed: did not received an ack\n");
			ret = -1;
			goto out;
		}

		ret = read(ack_fd[0], ack_buf, sizeof(ack_buf));
		if (ret > 0)
			ret = strcmp(ack_buf, EVLIST_CTL_CMD_ACK_TAG);
		else {
			pr_err("tpebs: perf record control ack failed\n");
			goto out;
		}
out:
		close(control_fd[0]);
		close(control_fd[1]);
		close(ack_fd[0]);
		close(ack_fd[1]);
	}
	if (ret) {
		struct tpebs_retire_lat *t = tpebs_retire_lat__find(evsel);

		list_del_init(&t->nd);
		tpebs_retire_lat__delete(t);
	}
	return ret;
}


int tpebs_set_evsel(struct evsel *evsel, int cpu_map_idx, int thread)
{
	__u64 val;
	struct tpebs_retire_lat *t;
	struct perf_counts_values *count;

	/* Non reitre_latency evsel should never enter this function. */
	if (!evsel__is_retire_lat(evsel))
		return -1;

	/*
	 * Need to stop the forked record to ensure get sampled data from the
	 * PIPE to process and get non-zero retire_lat value for hybrid.
	 */
	tpebs_stop();
	count = perf_counts(evsel->counts, cpu_map_idx, thread);

	t = tpebs_retire_lat__find(evsel);

	/* Set ena and run to non-zero */
	count->ena = count->run = 1;
	count->lost = 0;

	if (!t) {
		/*
		 * Set default value or 0 when retire_latency for this event is
		 * not found from sampling data (record_tpebs not set or 0
		 * sample recorded).
		 */
		count->val = 0;
		return 0;
	}

	/*
	 * Only set retire_latency value to the first CPU and thread.
	 */
	if (cpu_map_idx == 0 && thread == 0)
		val = rint(t->val);
	else
		val = 0;

	count->val = val;
	return 0;
}

/**
 * evsel__tpebs_close() - delete tpebs related data. If the last event, stop the
 * created thread and process by calling tpebs_stop().
 *
 * This function is called in evsel__close() to be symmetric with
 * evsel__tpebs_open() being called in evsel__open().
 */
void evsel__tpebs_close(struct evsel *evsel)
{
	struct tpebs_retire_lat *t = tpebs_retire_lat__find(evsel);

	tpebs_retire_lat__delete(t);

	if (list_empty(&tpebs_results))
		tpebs_stop();
}
