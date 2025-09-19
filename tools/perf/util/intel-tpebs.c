// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_tpebs.c: Intel TPEBS support
 */

#include <api/fs/fs.h>
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
#include "mutex.h"
#include "session.h"
#include "stat.h"
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
enum tpebs_mode tpebs_mode;
static LIST_HEAD(tpebs_results);
static pthread_t tpebs_reader_thread;
static struct child_process tpebs_cmd;
static int control_fd[2], ack_fd[2];
static struct mutex tpebs_mtx;

struct tpebs_retire_lat {
	struct list_head nd;
	/** @evsel: The evsel that opened the retire_lat event. */
	struct evsel *evsel;
	/** @event: Event passed to perf record. */
	char *event;
	/** @stats: Recorded retirement latency stats. */
	struct stats stats;
	/** @last: Last retirement latency read. */
	uint64_t last;
	/* Has the event been sent to perf record? */
	bool started;
};

static void tpebs_mtx_init(void)
{
	mutex_init(&tpebs_mtx);
}

static struct mutex *tpebs_mtx_get(void)
{
	static pthread_once_t tpebs_mtx_once = PTHREAD_ONCE_INIT;

	pthread_once(&tpebs_mtx_once, tpebs_mtx_init);
	return &tpebs_mtx;
}

static struct tpebs_retire_lat *tpebs_retire_lat__find(struct evsel *evsel)
	EXCLUSIVE_LOCKS_REQUIRED(tpebs_mtx_get());

static int evsel__tpebs_start_perf_record(struct evsel *evsel)
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

static bool is_child_pid(pid_t parent, pid_t child)
{
	if (parent < 0 || child < 0)
		return false;

	while (true) {
		char path[PATH_MAX];
		char line[256];
		FILE *fp;

new_child:
		if (parent == child)
			return true;

		if (child <= 0)
			return false;

		scnprintf(path, sizeof(path), "%s/%d/status", procfs__mountpoint(), child);
		fp = fopen(path, "r");
		if (!fp) {
			/* Presumably the process went away. Assume not a child. */
			return false;
		}
		while (fgets(line, sizeof(line), fp) != NULL) {
			if (strncmp(line, "PPid:", 5) == 0) {
				fclose(fp);
				if (sscanf(line + 5, "%d", &child) != 1) {
					/* Unexpected error parsing. */
					return false;
				}
				goto new_child;
			}
		}
		/* Unexpected EOF. */
		fclose(fp);
		return false;
	}
}

static bool should_ignore_sample(const struct perf_sample *sample, const struct tpebs_retire_lat *t)
{
	pid_t workload_pid, sample_pid = sample->pid;

	/*
	 * During evlist__purge the evlist will be removed prior to the
	 * evsel__exit calling evsel__tpebs_close and taking the
	 * tpebs_mtx. Avoid a segfault by ignoring samples in this case.
	 */
	if (t->evsel->evlist == NULL)
		return true;

	workload_pid = t->evsel->evlist->workload.pid;
	if (workload_pid < 0 || workload_pid == sample_pid)
		return false;

	if (!t->evsel->core.attr.inherit)
		return true;

	return !is_child_pid(workload_pid, sample_pid);
}

static int process_sample_event(const struct perf_tool *tool __maybe_unused,
				union perf_event *event __maybe_unused,
				struct perf_sample *sample,
				struct evsel *evsel,
				struct machine *machine __maybe_unused)
{
	struct tpebs_retire_lat *t;

	mutex_lock(tpebs_mtx_get());
	if (tpebs_cmd.pid == 0) {
		/* Record has terminated. */
		mutex_unlock(tpebs_mtx_get());
		return 0;
	}
	t = tpebs_retire_lat__find(evsel);
	if (!t) {
		mutex_unlock(tpebs_mtx_get());
		return -EINVAL;
	}
	if (should_ignore_sample(sample, t)) {
		mutex_unlock(tpebs_mtx_get());
		return 0;
	}
	/*
	 * Need to handle per core results? We are assuming average retire
	 * latency value will be used. Save the number of samples and the sum of
	 * retire latency value for each event.
	 */
	t->last = sample->weight3;
	update_stats(&t->stats, sample->weight3);
	mutex_unlock(tpebs_mtx_get());
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

static int tpebs_send_record_cmd(const char *msg) EXCLUSIVE_LOCKS_REQUIRED(tpebs_mtx_get())
{
	struct pollfd pollfd = { .events = POLLIN, };
	int ret, len, retries = 0;
	char ack_buf[8];

	/* Check if the command exited before the send, done with the lock held. */
	if (tpebs_cmd.pid == 0)
		return 0;

	/*
	 * Let go of the lock while sending/receiving as blocking can starve the
	 * sample reading thread.
	 */
	mutex_unlock(tpebs_mtx_get());

	/* Send perf record command.*/
	len = strlen(msg);
	ret = write(control_fd[1], msg, len);
	if (ret != len) {
		pr_err("perf record control write control message '%s' failed\n", msg);
		ret = -EPIPE;
		goto out;
	}

	if (!strcmp(msg, EVLIST_CTL_CMD_STOP_TAG)) {
		ret = 0;
		goto out;
	}

	/* Wait for an ack. */
	pollfd.fd = ack_fd[0];

	/*
	 * We need this poll to ensure the ack_fd PIPE will not hang
	 * when perf record failed for any reason. The timeout value
	 * 3000ms is an empirical selection.
	 */
again:
	if (!poll(&pollfd, 1, 500)) {
		if (check_if_command_finished(&tpebs_cmd)) {
			ret = 0;
			goto out;
		}

		if (retries++ < 6)
			goto again;
		pr_err("tpebs failed: perf record ack timeout for '%s'\n", msg);
		ret = -ETIMEDOUT;
		goto out;
	}

	if (!(pollfd.revents & POLLIN)) {
		if (check_if_command_finished(&tpebs_cmd)) {
			ret = 0;
			goto out;
		}

		pr_err("tpebs failed: did not received an ack for '%s'\n", msg);
		ret = -EPIPE;
		goto out;
	}

	ret = read(ack_fd[0], ack_buf, sizeof(ack_buf));
	if (ret > 0)
		ret = strcmp(ack_buf, EVLIST_CTL_CMD_ACK_TAG);
	else
		pr_err("tpebs: perf record control ack failed\n");
out:
	/* Re-take lock as expected by caller. */
	mutex_lock(tpebs_mtx_get());
	return ret;
}

/*
 * tpebs_stop - stop the sample data read thread and the perf record process.
 */
static int tpebs_stop(void) EXCLUSIVE_LOCKS_REQUIRED(tpebs_mtx_get())
{
	int ret = 0;

	/* Like tpebs_start, we should only run tpebs_end once. */
	if (tpebs_cmd.pid != 0) {
		tpebs_send_record_cmd(EVLIST_CTL_CMD_STOP_TAG);
		tpebs_cmd.pid = 0;
		mutex_unlock(tpebs_mtx_get());
		pthread_join(tpebs_reader_thread, NULL);
		mutex_lock(tpebs_mtx_get());
		close(control_fd[0]);
		close(control_fd[1]);
		close(ack_fd[0]);
		close(ack_fd[1]);
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
	struct tpebs_retire_lat *tpebs_event;

	mutex_lock(tpebs_mtx_get());
	tpebs_event = tpebs_retire_lat__find(evsel);
	if (tpebs_event) {
		/* evsel, or an identically named one, was already prepared. */
		mutex_unlock(tpebs_mtx_get());
		return 0;
	}
	tpebs_event = tpebs_retire_lat__new(evsel);
	if (!tpebs_event) {
		mutex_unlock(tpebs_mtx_get());
		return -ENOMEM;
	}
	list_add_tail(&tpebs_event->nd, &tpebs_results);
	mutex_unlock(tpebs_mtx_get());

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
	bool tpebs_empty;

	/* We should only run tpebs_start when tpebs_recording is enabled. */
	if (!tpebs_recording)
		return 0;
	/* Only start the events once. */
	if (tpebs_cmd.pid != 0) {
		struct tpebs_retire_lat *t;
		bool valid;

		mutex_lock(tpebs_mtx_get());
		t = tpebs_retire_lat__find(evsel);
		valid = t && t->started;
		mutex_unlock(tpebs_mtx_get());
		/* May fail as the event wasn't started. */
		return valid ? 0 : -EBUSY;
	}

	ret = evsel__tpebs_prepare(evsel);
	if (ret)
		return ret;

	mutex_lock(tpebs_mtx_get());
	tpebs_empty = list_empty(&tpebs_results);
	if (!tpebs_empty) {
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

		ret = evsel__tpebs_start_perf_record(evsel);
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
		ret = tpebs_send_record_cmd(EVLIST_CTL_CMD_ENABLE_TAG);
	}
out:
	if (ret) {
		struct tpebs_retire_lat *t = tpebs_retire_lat__find(evsel);

		list_del_init(&t->nd);
		tpebs_retire_lat__delete(t);
	}
	mutex_unlock(tpebs_mtx_get());
	return ret;
}

int evsel__tpebs_read(struct evsel *evsel, int cpu_map_idx, int thread)
{
	struct perf_counts_values *count, *old_count = NULL;
	struct tpebs_retire_lat *t;
	uint64_t val;
	int ret;

	/* Only set retire_latency value to the first CPU and thread. */
	if (cpu_map_idx != 0 || thread != 0)
		return 0;

	if (evsel->prev_raw_counts)
		old_count = perf_counts(evsel->prev_raw_counts, cpu_map_idx, thread);

	count = perf_counts(evsel->counts, cpu_map_idx, thread);

	mutex_lock(tpebs_mtx_get());
	t = tpebs_retire_lat__find(evsel);
	/*
	 * If reading the first tpebs result, send a ping to the record
	 * process. Allow the sample reader a chance to read by releasing and
	 * reacquiring the lock.
	 */
	if (t && &t->nd == tpebs_results.next) {
		ret = tpebs_send_record_cmd(EVLIST_CTL_CMD_PING_TAG);
		mutex_unlock(tpebs_mtx_get());
		if (ret)
			return ret;
		mutex_lock(tpebs_mtx_get());
	}
	if (t == NULL || t->stats.n == 0) {
		/* No sample data, use default. */
		if (tpebs_recording) {
			pr_warning_once(
				"Using precomputed retirement latency data as no samples\n");
		}
		val = 0;
		switch (tpebs_mode) {
		case TPEBS_MODE__MIN:
			val = rint(evsel->retirement_latency.min);
			break;
		case TPEBS_MODE__MAX:
			val = rint(evsel->retirement_latency.max);
			break;
		default:
		case TPEBS_MODE__LAST:
		case TPEBS_MODE__MEAN:
			val = rint(evsel->retirement_latency.mean);
			break;
		}
	} else {
		switch (tpebs_mode) {
		case TPEBS_MODE__MIN:
			val = t->stats.min;
			break;
		case TPEBS_MODE__MAX:
			val = t->stats.max;
			break;
		case TPEBS_MODE__LAST:
			val = t->last;
			break;
		default:
		case TPEBS_MODE__MEAN:
			val = rint(t->stats.mean);
			break;
		}
	}
	mutex_unlock(tpebs_mtx_get());

	if (old_count) {
		count->val = old_count->val + val;
		count->run = old_count->run + 1;
		count->ena = old_count->ena + 1;
	} else {
		count->val = val;
		count->run++;
		count->ena++;
	}
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
	struct tpebs_retire_lat *t;

	mutex_lock(tpebs_mtx_get());
	t = tpebs_retire_lat__find(evsel);
	if (t) {
		list_del_init(&t->nd);
		tpebs_retire_lat__delete(t);

		if (list_empty(&tpebs_results))
			tpebs_stop();
	}
	mutex_unlock(tpebs_mtx_get());
}
