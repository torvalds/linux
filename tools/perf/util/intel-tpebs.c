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
	/* Event name */
	char *name;
	/* Event name with the TPEBS modifier R */
	const char *tpebs_name;
	/* Count of retire_latency values found in sample data */
	size_t count;
	/* Sum of all the retire_latency values in sample data */
	int sum;
	/* Average of retire_latency, val = sum / count */
	double val;
};

static int get_perf_record_args(const char **record_argv, char buf[],
				const char *cpumap_buf)
{
	struct tpebs_retire_lat *e;
	int i = 0;

	pr_debug("tpebs: Prepare perf record for retire_latency\n");

	record_argv[i++] = "perf";
	record_argv[i++] = "record";
	record_argv[i++] = "-W";
	record_argv[i++] = "--synth=no";
	record_argv[i++] = buf;

	if (!cpumap_buf) {
		pr_err("tpebs: Require cpumap list to run sampling\n");
		return -ECANCELED;
	}
	/* Use -C when cpumap_buf is not "-1" */
	if (strcmp(cpumap_buf, "-1")) {
		record_argv[i++] = "-C";
		record_argv[i++] = cpumap_buf;
	}

	list_for_each_entry(e, &tpebs_results, nd) {
		record_argv[i++] = "-e";
		record_argv[i++] = e->name;
	}

	record_argv[i++] = "-o";
	record_argv[i++] = PERF_DATA;

	return 0;
}

static int evsel__tpebs_start_perf_record(struct evsel *evsel, int control_fd[], int ack_fd[])
{
	const char **record_argv;
	size_t tpebs_event_size = 0;
	int ret;
	char buf[32];
	char cpumap_buf[50];
	struct tpebs_retire_lat *t;

	cpu_map__snprint(evsel->evlist->core.user_requested_cpus, cpumap_buf,
			 sizeof(cpumap_buf));

	scnprintf(buf, sizeof(buf), "--control=fd:%d,%d", control_fd[0], ack_fd[1]);

	list_for_each_entry(t, &tpebs_results, nd)
		tpebs_event_size++;

	record_argv = calloc(12 + 2 * tpebs_event_size, sizeof(char *));
	if (!record_argv)
		return -ENOMEM;

	ret = get_perf_record_args(record_argv, buf, cpumap_buf);
	if (ret)
		goto out;

	assert(tpebs_cmd.pid == 0);
	tpebs_cmd.argv = record_argv;
	tpebs_cmd.out = -1;
	ret = start_command(&tpebs_cmd);
out:
	free(record_argv);
	return ret;
}

static int process_sample_event(const struct perf_tool *tool __maybe_unused,
				union perf_event *event __maybe_unused,
				struct perf_sample *sample,
				struct evsel *evsel,
				struct machine *machine __maybe_unused)
{
	int ret = 0;
	const char *evname;
	struct tpebs_retire_lat *t;

	evname = evsel__name(evsel);

	/*
	 * Need to handle per core results? We are assuming average retire
	 * latency value will be used. Save the number of samples and the sum of
	 * retire latency value for each event.
	 */
	list_for_each_entry(t, &tpebs_results, nd) {
		if (!strcmp(evname, t->name)) {
			t->count += 1;
			t->sum += sample->retire_lat;
			t->val = (double) t->sum / t->count;
			break;
		}
	}

	return ret;
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

static char *evsel__tpebs_name(struct evsel *evsel)
{
	char *name, *modifier;

	name = strdup(evsel->name);
	if (!name)
		return NULL;

	modifier = strrchr(name, 'R');
	if (!modifier) {
		pr_err("Tpebs event missing modifier '%s'\n", name);
		free(name);
		return NULL;
	}

	*modifier = 'p';
	return name;
}

static struct tpebs_retire_lat *tpebs_retire_lat__new(struct evsel *evsel)
{
	struct tpebs_retire_lat *result = zalloc(sizeof(*result));

	if (!result)
		return NULL;

	result->tpebs_name = evsel->name;
	result->name = evsel__tpebs_name(evsel);
	if (!result->name) {
		free(result);
		return NULL;
	}
	list_add_tail(&result->nd, &tpebs_results);
	return result;
}

/**
 * evsel__tpebs_prepare - create tpebs data structures ready for opening.
 * @evsel: retire_latency evsel, all evsels on its list will be prepared.
 */
static int evsel__tpebs_prepare(struct evsel *evsel)
{
	struct evsel *pos;
	struct tpebs_retire_lat *tpebs_event;

	list_for_each_entry(tpebs_event, &tpebs_results, nd) {
		if (!strcmp(tpebs_event->tpebs_name, evsel->name)) {
			/*
			 * evsel, or an identically named one, was already
			 * prepared.
			 */
			return 0;
		}
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

	/*
	 * We should only run tpebs_start when tpebs_recording is enabled.
	 * And we should only run it once with all the required events.
	 */
	if (tpebs_cmd.pid != 0 || !tpebs_recording)
		return 0;

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
	if (ret)
		tpebs_delete();
	return ret;
}


int tpebs_set_evsel(struct evsel *evsel, int cpu_map_idx, int thread)
{
	__u64 val;
	bool found = false;
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

	list_for_each_entry(t, &tpebs_results, nd) {
		if (t->tpebs_name == evsel->name ||
		    (evsel->metric_id && !strcmp(t->tpebs_name, evsel->metric_id))) {
			found = true;
			break;
		}
	}

	/* Set ena and run to non-zero */
	count->ena = count->run = 1;
	count->lost = 0;

	if (!found) {
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

static void tpebs_retire_lat__delete(struct tpebs_retire_lat *r)
{
	zfree(&r->name);
	free(r);
}


/*
 * tpebs_delete - delete tpebs related data and stop the created thread and
 * process by calling tpebs_stop().
 *
 * This function is called from evlist_delete() and also from builtin-stat
 * stat_handle_error(). If tpebs_start() is called from places other then perf
 * stat, need to ensure tpebs_delete() is also called to safely free mem and
 * close the data read thread and the forked perf record process.
 *
 * This function is also called in evsel__close() to be symmetric with
 * tpebs_start() being called in evsel__open(). We will update this call site
 * when move tpebs_start() to evlist level.
 */
void tpebs_delete(void)
{
	struct tpebs_retire_lat *r, *rtmp;

	tpebs_stop();

	list_for_each_entry_safe(r, rtmp, &tpebs_results, nd) {
		list_del_init(&r->nd);
		tpebs_retire_lat__delete(r);
	}
}
