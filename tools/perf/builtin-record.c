// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-record.c
 *
 * Builtin record command: Record the profile of a workload
 * (or a CPU, or a PID) into the perf.data output file - for
 * later analysis via perf report.
 */
#include "builtin.h"

#include "util/build-id.h"
#include <subcmd/parse-options.h>
#include "util/parse-events.h"
#include "util/config.h"

#include "util/callchain.h"
#include "util/cgroup.h"
#include "util/header.h"
#include "util/event.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/debug.h"
#include "util/mmap.h"
#include "util/target.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/symbol.h"
#include "util/record.h"
#include "util/cpumap.h"
#include "util/thread_map.h"
#include "util/data.h"
#include "util/perf_regs.h"
#include "util/auxtrace.h"
#include "util/tsc.h"
#include "util/parse-branch-options.h"
#include "util/parse-regs-options.h"
#include "util/perf_api_probe.h"
#include "util/llvm-utils.h"
#include "util/bpf-loader.h"
#include "util/trigger.h"
#include "util/perf-hooks.h"
#include "util/cpu-set-sched.h"
#include "util/synthetic-events.h"
#include "util/time-utils.h"
#include "util/units.h"
#include "util/bpf-event.h"
#include "util/util.h"
#include "util/pfm.h"
#include "util/clockid.h"
#include "asm/bug.h"
#include "perf.h"

#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#ifdef HAVE_EVENTFD_SUPPORT
#include <sys/eventfd.h>
#endif
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/zalloc.h>
#include <linux/bitmap.h>
#include <sys/time.h>

struct switch_output {
	bool		 enabled;
	bool		 signal;
	unsigned long	 size;
	unsigned long	 time;
	const char	*str;
	bool		 set;
	char		 **filenames;
	int		 num_files;
	int		 cur_file;
};

struct record {
	struct perf_tool	tool;
	struct record_opts	opts;
	u64			bytes_written;
	struct perf_data	data;
	struct auxtrace_record	*itr;
	struct evlist	*evlist;
	struct perf_session	*session;
	struct evlist		*sb_evlist;
	pthread_t		thread_id;
	int			realtime_prio;
	bool			switch_output_event_set;
	bool			no_buildid;
	bool			no_buildid_set;
	bool			no_buildid_cache;
	bool			no_buildid_cache_set;
	bool			buildid_all;
	bool			timestamp_filename;
	bool			timestamp_boundary;
	struct switch_output	switch_output;
	unsigned long long	samples;
	struct mmap_cpu_mask	affinity_mask;
	unsigned long		output_max_size;	/* = 0: unlimited */
};

static volatile int done;

static volatile int auxtrace_record__snapshot_started;
static DEFINE_TRIGGER(auxtrace_snapshot_trigger);
static DEFINE_TRIGGER(switch_output_trigger);

static const char *affinity_tags[PERF_AFFINITY_MAX] = {
	"SYS", "NODE", "CPU"
};

static bool switch_output_signal(struct record *rec)
{
	return rec->switch_output.signal &&
	       trigger_is_ready(&switch_output_trigger);
}

static bool switch_output_size(struct record *rec)
{
	return rec->switch_output.size &&
	       trigger_is_ready(&switch_output_trigger) &&
	       (rec->bytes_written >= rec->switch_output.size);
}

static bool switch_output_time(struct record *rec)
{
	return rec->switch_output.time &&
	       trigger_is_ready(&switch_output_trigger);
}

static bool record__output_max_size_exceeded(struct record *rec)
{
	return rec->output_max_size &&
	       (rec->bytes_written >= rec->output_max_size);
}

static int record__write(struct record *rec, struct mmap *map __maybe_unused,
			 void *bf, size_t size)
{
	struct perf_data_file *file = &rec->session->data->file;

	if (perf_data_file__write(file, bf, size) < 0) {
		pr_err("failed to write perf data, error: %m\n");
		return -1;
	}

	rec->bytes_written += size;

	if (record__output_max_size_exceeded(rec) && !done) {
		fprintf(stderr, "[ perf record: perf size limit reached (%" PRIu64 " KB),"
				" stopping session ]\n",
				rec->bytes_written >> 10);
		done = 1;
	}

	if (switch_output_size(rec))
		trigger_hit(&switch_output_trigger);

	return 0;
}

static int record__aio_enabled(struct record *rec);
static int record__comp_enabled(struct record *rec);
static size_t zstd_compress(struct perf_session *session, void *dst, size_t dst_size,
			    void *src, size_t src_size);

#ifdef HAVE_AIO_SUPPORT
static int record__aio_write(struct aiocb *cblock, int trace_fd,
		void *buf, size_t size, off_t off)
{
	int rc;

	cblock->aio_fildes = trace_fd;
	cblock->aio_buf    = buf;
	cblock->aio_nbytes = size;
	cblock->aio_offset = off;
	cblock->aio_sigevent.sigev_notify = SIGEV_NONE;

	do {
		rc = aio_write(cblock);
		if (rc == 0) {
			break;
		} else if (errno != EAGAIN) {
			cblock->aio_fildes = -1;
			pr_err("failed to queue perf data, error: %m\n");
			break;
		}
	} while (1);

	return rc;
}

static int record__aio_complete(struct mmap *md, struct aiocb *cblock)
{
	void *rem_buf;
	off_t rem_off;
	size_t rem_size;
	int rc, aio_errno;
	ssize_t aio_ret, written;

	aio_errno = aio_error(cblock);
	if (aio_errno == EINPROGRESS)
		return 0;

	written = aio_ret = aio_return(cblock);
	if (aio_ret < 0) {
		if (aio_errno != EINTR)
			pr_err("failed to write perf data, error: %m\n");
		written = 0;
	}

	rem_size = cblock->aio_nbytes - written;

	if (rem_size == 0) {
		cblock->aio_fildes = -1;
		/*
		 * md->refcount is incremented in record__aio_pushfn() for
		 * every aio write request started in record__aio_push() so
		 * decrement it because the request is now complete.
		 */
		perf_mmap__put(&md->core);
		rc = 1;
	} else {
		/*
		 * aio write request may require restart with the
		 * reminder if the kernel didn't write whole
		 * chunk at once.
		 */
		rem_off = cblock->aio_offset + written;
		rem_buf = (void *)(cblock->aio_buf + written);
		record__aio_write(cblock, cblock->aio_fildes,
				rem_buf, rem_size, rem_off);
		rc = 0;
	}

	return rc;
}

static int record__aio_sync(struct mmap *md, bool sync_all)
{
	struct aiocb **aiocb = md->aio.aiocb;
	struct aiocb *cblocks = md->aio.cblocks;
	struct timespec timeout = { 0, 1000 * 1000  * 1 }; /* 1ms */
	int i, do_suspend;

	do {
		do_suspend = 0;
		for (i = 0; i < md->aio.nr_cblocks; ++i) {
			if (cblocks[i].aio_fildes == -1 || record__aio_complete(md, &cblocks[i])) {
				if (sync_all)
					aiocb[i] = NULL;
				else
					return i;
			} else {
				/*
				 * Started aio write is not complete yet
				 * so it has to be waited before the
				 * next allocation.
				 */
				aiocb[i] = &cblocks[i];
				do_suspend = 1;
			}
		}
		if (!do_suspend)
			return -1;

		while (aio_suspend((const struct aiocb **)aiocb, md->aio.nr_cblocks, &timeout)) {
			if (!(errno == EAGAIN || errno == EINTR))
				pr_err("failed to sync perf data, error: %m\n");
		}
	} while (1);
}

struct record_aio {
	struct record	*rec;
	void		*data;
	size_t		size;
};

static int record__aio_pushfn(struct mmap *map, void *to, void *buf, size_t size)
{
	struct record_aio *aio = to;

	/*
	 * map->core.base data pointed by buf is copied into free map->aio.data[] buffer
	 * to release space in the kernel buffer as fast as possible, calling
	 * perf_mmap__consume() from perf_mmap__push() function.
	 *
	 * That lets the kernel to proceed with storing more profiling data into
	 * the kernel buffer earlier than other per-cpu kernel buffers are handled.
	 *
	 * Coping can be done in two steps in case the chunk of profiling data
	 * crosses the upper bound of the kernel buffer. In this case we first move
	 * part of data from map->start till the upper bound and then the reminder
	 * from the beginning of the kernel buffer till the end of the data chunk.
	 */

	if (record__comp_enabled(aio->rec)) {
		size = zstd_compress(aio->rec->session, aio->data + aio->size,
				     mmap__mmap_len(map) - aio->size,
				     buf, size);
	} else {
		memcpy(aio->data + aio->size, buf, size);
	}

	if (!aio->size) {
		/*
		 * Increment map->refcount to guard map->aio.data[] buffer
		 * from premature deallocation because map object can be
		 * released earlier than aio write request started on
		 * map->aio.data[] buffer is complete.
		 *
		 * perf_mmap__put() is done at record__aio_complete()
		 * after started aio request completion or at record__aio_push()
		 * if the request failed to start.
		 */
		perf_mmap__get(&map->core);
	}

	aio->size += size;

	return size;
}

static int record__aio_push(struct record *rec, struct mmap *map, off_t *off)
{
	int ret, idx;
	int trace_fd = rec->session->data->file.fd;
	struct record_aio aio = { .rec = rec, .size = 0 };

	/*
	 * Call record__aio_sync() to wait till map->aio.data[] buffer
	 * becomes available after previous aio write operation.
	 */

	idx = record__aio_sync(map, false);
	aio.data = map->aio.data[idx];
	ret = perf_mmap__push(map, &aio, record__aio_pushfn);
	if (ret != 0) /* ret > 0 - no data, ret < 0 - error */
		return ret;

	rec->samples++;
	ret = record__aio_write(&(map->aio.cblocks[idx]), trace_fd, aio.data, aio.size, *off);
	if (!ret) {
		*off += aio.size;
		rec->bytes_written += aio.size;
		if (switch_output_size(rec))
			trigger_hit(&switch_output_trigger);
	} else {
		/*
		 * Decrement map->refcount incremented in record__aio_pushfn()
		 * back if record__aio_write() operation failed to start, otherwise
		 * map->refcount is decremented in record__aio_complete() after
		 * aio write operation finishes successfully.
		 */
		perf_mmap__put(&map->core);
	}

	return ret;
}

static off_t record__aio_get_pos(int trace_fd)
{
	return lseek(trace_fd, 0, SEEK_CUR);
}

static void record__aio_set_pos(int trace_fd, off_t pos)
{
	lseek(trace_fd, pos, SEEK_SET);
}

static void record__aio_mmap_read_sync(struct record *rec)
{
	int i;
	struct evlist *evlist = rec->evlist;
	struct mmap *maps = evlist->mmap;

	if (!record__aio_enabled(rec))
		return;

	for (i = 0; i < evlist->core.nr_mmaps; i++) {
		struct mmap *map = &maps[i];

		if (map->core.base)
			record__aio_sync(map, true);
	}
}

static int nr_cblocks_default = 1;
static int nr_cblocks_max = 4;

static int record__aio_parse(const struct option *opt,
			     const char *str,
			     int unset)
{
	struct record_opts *opts = (struct record_opts *)opt->value;

	if (unset) {
		opts->nr_cblocks = 0;
	} else {
		if (str)
			opts->nr_cblocks = strtol(str, NULL, 0);
		if (!opts->nr_cblocks)
			opts->nr_cblocks = nr_cblocks_default;
	}

	return 0;
}
#else /* HAVE_AIO_SUPPORT */
static int nr_cblocks_max = 0;

static int record__aio_push(struct record *rec __maybe_unused, struct mmap *map __maybe_unused,
			    off_t *off __maybe_unused)
{
	return -1;
}

static off_t record__aio_get_pos(int trace_fd __maybe_unused)
{
	return -1;
}

static void record__aio_set_pos(int trace_fd __maybe_unused, off_t pos __maybe_unused)
{
}

static void record__aio_mmap_read_sync(struct record *rec __maybe_unused)
{
}
#endif

static int record__aio_enabled(struct record *rec)
{
	return rec->opts.nr_cblocks > 0;
}

#define MMAP_FLUSH_DEFAULT 1
static int record__mmap_flush_parse(const struct option *opt,
				    const char *str,
				    int unset)
{
	int flush_max;
	struct record_opts *opts = (struct record_opts *)opt->value;
	static struct parse_tag tags[] = {
			{ .tag  = 'B', .mult = 1       },
			{ .tag  = 'K', .mult = 1 << 10 },
			{ .tag  = 'M', .mult = 1 << 20 },
			{ .tag  = 'G', .mult = 1 << 30 },
			{ .tag  = 0 },
	};

	if (unset)
		return 0;

	if (str) {
		opts->mmap_flush = parse_tag_value(str, tags);
		if (opts->mmap_flush == (int)-1)
			opts->mmap_flush = strtol(str, NULL, 0);
	}

	if (!opts->mmap_flush)
		opts->mmap_flush = MMAP_FLUSH_DEFAULT;

	flush_max = evlist__mmap_size(opts->mmap_pages);
	flush_max /= 4;
	if (opts->mmap_flush > flush_max)
		opts->mmap_flush = flush_max;

	return 0;
}

#ifdef HAVE_ZSTD_SUPPORT
static unsigned int comp_level_default = 1;

static int record__parse_comp_level(const struct option *opt, const char *str, int unset)
{
	struct record_opts *opts = opt->value;

	if (unset) {
		opts->comp_level = 0;
	} else {
		if (str)
			opts->comp_level = strtol(str, NULL, 0);
		if (!opts->comp_level)
			opts->comp_level = comp_level_default;
	}

	return 0;
}
#endif
static unsigned int comp_level_max = 22;

static int record__comp_enabled(struct record *rec)
{
	return rec->opts.comp_level > 0;
}

static int process_synthesized_event(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample __maybe_unused,
				     struct machine *machine __maybe_unused)
{
	struct record *rec = container_of(tool, struct record, tool);
	return record__write(rec, NULL, event, event->header.size);
}

static int process_locked_synthesized_event(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample __maybe_unused,
				     struct machine *machine __maybe_unused)
{
	static pthread_mutex_t synth_lock = PTHREAD_MUTEX_INITIALIZER;
	int ret;

	pthread_mutex_lock(&synth_lock);
	ret = process_synthesized_event(tool, event, sample, machine);
	pthread_mutex_unlock(&synth_lock);
	return ret;
}

static int record__pushfn(struct mmap *map, void *to, void *bf, size_t size)
{
	struct record *rec = to;

	if (record__comp_enabled(rec)) {
		size = zstd_compress(rec->session, map->data, mmap__mmap_len(map), bf, size);
		bf   = map->data;
	}

	rec->samples++;
	return record__write(rec, map, bf, size);
}

static volatile int signr = -1;
static volatile int child_finished;
#ifdef HAVE_EVENTFD_SUPPORT
static int done_fd = -1;
#endif

static void sig_handler(int sig)
{
	if (sig == SIGCHLD)
		child_finished = 1;
	else
		signr = sig;

	done = 1;
#ifdef HAVE_EVENTFD_SUPPORT
{
	u64 tmp = 1;
	/*
	 * It is possible for this signal handler to run after done is checked
	 * in the main loop, but before the perf counter fds are polled. If this
	 * happens, the poll() will continue to wait even though done is set,
	 * and will only break out if either another signal is received, or the
	 * counters are ready for read. To ensure the poll() doesn't sleep when
	 * done is set, use an eventfd (done_fd) to wake up the poll().
	 */
	if (write(done_fd, &tmp, sizeof(tmp)) < 0)
		pr_err("failed to signal wakeup fd, error: %m\n");
}
#endif // HAVE_EVENTFD_SUPPORT
}

static void sigsegv_handler(int sig)
{
	perf_hooks__recover();
	sighandler_dump_stack(sig);
}

static void record__sig_exit(void)
{
	if (signr == -1)
		return;

	signal(signr, SIG_DFL);
	raise(signr);
}

#ifdef HAVE_AUXTRACE_SUPPORT

static int record__process_auxtrace(struct perf_tool *tool,
				    struct mmap *map,
				    union perf_event *event, void *data1,
				    size_t len1, void *data2, size_t len2)
{
	struct record *rec = container_of(tool, struct record, tool);
	struct perf_data *data = &rec->data;
	size_t padding;
	u8 pad[8] = {0};

	if (!perf_data__is_pipe(data) && perf_data__is_single_file(data)) {
		off_t file_offset;
		int fd = perf_data__fd(data);
		int err;

		file_offset = lseek(fd, 0, SEEK_CUR);
		if (file_offset == -1)
			return -1;
		err = auxtrace_index__auxtrace_event(&rec->session->auxtrace_index,
						     event, file_offset);
		if (err)
			return err;
	}

	/* event.auxtrace.size includes padding, see __auxtrace_mmap__read() */
	padding = (len1 + len2) & 7;
	if (padding)
		padding = 8 - padding;

	record__write(rec, map, event, event->header.size);
	record__write(rec, map, data1, len1);
	if (len2)
		record__write(rec, map, data2, len2);
	record__write(rec, map, &pad, padding);

	return 0;
}

static int record__auxtrace_mmap_read(struct record *rec,
				      struct mmap *map)
{
	int ret;

	ret = auxtrace_mmap__read(map, rec->itr, &rec->tool,
				  record__process_auxtrace);
	if (ret < 0)
		return ret;

	if (ret)
		rec->samples++;

	return 0;
}

static int record__auxtrace_mmap_read_snapshot(struct record *rec,
					       struct mmap *map)
{
	int ret;

	ret = auxtrace_mmap__read_snapshot(map, rec->itr, &rec->tool,
					   record__process_auxtrace,
					   rec->opts.auxtrace_snapshot_size);
	if (ret < 0)
		return ret;

	if (ret)
		rec->samples++;

	return 0;
}

static int record__auxtrace_read_snapshot_all(struct record *rec)
{
	int i;
	int rc = 0;

	for (i = 0; i < rec->evlist->core.nr_mmaps; i++) {
		struct mmap *map = &rec->evlist->mmap[i];

		if (!map->auxtrace_mmap.base)
			continue;

		if (record__auxtrace_mmap_read_snapshot(rec, map) != 0) {
			rc = -1;
			goto out;
		}
	}
out:
	return rc;
}

static void record__read_auxtrace_snapshot(struct record *rec, bool on_exit)
{
	pr_debug("Recording AUX area tracing snapshot\n");
	if (record__auxtrace_read_snapshot_all(rec) < 0) {
		trigger_error(&auxtrace_snapshot_trigger);
	} else {
		if (auxtrace_record__snapshot_finish(rec->itr, on_exit))
			trigger_error(&auxtrace_snapshot_trigger);
		else
			trigger_ready(&auxtrace_snapshot_trigger);
	}
}

static int record__auxtrace_snapshot_exit(struct record *rec)
{
	if (trigger_is_error(&auxtrace_snapshot_trigger))
		return 0;

	if (!auxtrace_record__snapshot_started &&
	    auxtrace_record__snapshot_start(rec->itr))
		return -1;

	record__read_auxtrace_snapshot(rec, true);
	if (trigger_is_error(&auxtrace_snapshot_trigger))
		return -1;

	return 0;
}

static int record__auxtrace_init(struct record *rec)
{
	int err;

	if (!rec->itr) {
		rec->itr = auxtrace_record__init(rec->evlist, &err);
		if (err)
			return err;
	}

	err = auxtrace_parse_snapshot_options(rec->itr, &rec->opts,
					      rec->opts.auxtrace_snapshot_opts);
	if (err)
		return err;

	err = auxtrace_parse_sample_options(rec->itr, rec->evlist, &rec->opts,
					    rec->opts.auxtrace_sample_opts);
	if (err)
		return err;

	return auxtrace_parse_filters(rec->evlist);
}

#else

static inline
int record__auxtrace_mmap_read(struct record *rec __maybe_unused,
			       struct mmap *map __maybe_unused)
{
	return 0;
}

static inline
void record__read_auxtrace_snapshot(struct record *rec __maybe_unused,
				    bool on_exit __maybe_unused)
{
}

static inline
int auxtrace_record__snapshot_start(struct auxtrace_record *itr __maybe_unused)
{
	return 0;
}

static inline
int record__auxtrace_snapshot_exit(struct record *rec __maybe_unused)
{
	return 0;
}

static int record__auxtrace_init(struct record *rec __maybe_unused)
{
	return 0;
}

#endif

static int record__config_text_poke(struct evlist *evlist)
{
	struct evsel *evsel;
	int err;

	/* Nothing to do if text poke is already configured */
	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.text_poke)
			return 0;
	}

	err = parse_events(evlist, "dummy:u", NULL);
	if (err)
		return err;

	evsel = evlist__last(evlist);

	evsel->core.attr.freq = 0;
	evsel->core.attr.sample_period = 1;
	evsel->core.attr.text_poke = 1;
	evsel->core.attr.ksymbol = 1;

	evsel->core.system_wide = true;
	evsel->no_aux_samples = true;
	evsel->immediate = true;

	/* Text poke must be collected on all CPUs */
	perf_cpu_map__put(evsel->core.own_cpus);
	evsel->core.own_cpus = perf_cpu_map__new(NULL);
	perf_cpu_map__put(evsel->core.cpus);
	evsel->core.cpus = perf_cpu_map__get(evsel->core.own_cpus);

	evsel__set_sample_bit(evsel, TIME);

	return 0;
}

static bool record__kcore_readable(struct machine *machine)
{
	char kcore[PATH_MAX];
	int fd;

	scnprintf(kcore, sizeof(kcore), "%s/proc/kcore", machine->root_dir);

	fd = open(kcore, O_RDONLY);
	if (fd < 0)
		return false;

	close(fd);

	return true;
}

static int record__kcore_copy(struct machine *machine, struct perf_data *data)
{
	char from_dir[PATH_MAX];
	char kcore_dir[PATH_MAX];
	int ret;

	snprintf(from_dir, sizeof(from_dir), "%s/proc", machine->root_dir);

	ret = perf_data__make_kcore_dir(data, kcore_dir, sizeof(kcore_dir));
	if (ret)
		return ret;

	return kcore_copy(from_dir, kcore_dir);
}

static int record__mmap_evlist(struct record *rec,
			       struct evlist *evlist)
{
	struct record_opts *opts = &rec->opts;
	bool auxtrace_overwrite = opts->auxtrace_snapshot_mode ||
				  opts->auxtrace_sample_mode;
	char msg[512];

	if (opts->affinity != PERF_AFFINITY_SYS)
		cpu__setup_cpunode_map();

	if (evlist__mmap_ex(evlist, opts->mmap_pages,
				 opts->auxtrace_mmap_pages,
				 auxtrace_overwrite,
				 opts->nr_cblocks, opts->affinity,
				 opts->mmap_flush, opts->comp_level) < 0) {
		if (errno == EPERM) {
			pr_err("Permission error mapping pages.\n"
			       "Consider increasing "
			       "/proc/sys/kernel/perf_event_mlock_kb,\n"
			       "or try again with a smaller value of -m/--mmap_pages.\n"
			       "(current value: %u,%u)\n",
			       opts->mmap_pages, opts->auxtrace_mmap_pages);
			return -errno;
		} else {
			pr_err("failed to mmap with %d (%s)\n", errno,
				str_error_r(errno, msg, sizeof(msg)));
			if (errno)
				return -errno;
			else
				return -EINVAL;
		}
	}
	return 0;
}

static int record__mmap(struct record *rec)
{
	return record__mmap_evlist(rec, rec->evlist);
}

static int record__open(struct record *rec)
{
	char msg[BUFSIZ];
	struct evsel *pos;
	struct evlist *evlist = rec->evlist;
	struct perf_session *session = rec->session;
	struct record_opts *opts = &rec->opts;
	int rc = 0;

	/*
	 * For initial_delay or system wide, we need to add a dummy event so
	 * that we can track PERF_RECORD_MMAP to cover the delay of waiting or
	 * event synthesis.
	 */
	if (opts->initial_delay || target__has_cpu(&opts->target)) {
		pos = perf_evlist__get_tracking_event(evlist);
		if (!evsel__is_dummy_event(pos)) {
			/* Set up dummy event. */
			if (evlist__add_dummy(evlist))
				return -ENOMEM;
			pos = evlist__last(evlist);
			perf_evlist__set_tracking_event(evlist, pos);
		}

		/*
		 * Enable the dummy event when the process is forked for
		 * initial_delay, immediately for system wide.
		 */
		if (opts->initial_delay && !pos->immediate)
			pos->core.attr.enable_on_exec = 1;
		else
			pos->immediate = 1;
	}

	perf_evlist__config(evlist, opts, &callchain_param);

	evlist__for_each_entry(evlist, pos) {
try_again:
		if (evsel__open(pos, pos->core.cpus, pos->core.threads) < 0) {
			if (evsel__fallback(pos, errno, msg, sizeof(msg))) {
				if (verbose > 0)
					ui__warning("%s\n", msg);
				goto try_again;
			}
			if ((errno == EINVAL || errno == EBADF) &&
			    pos->leader != pos &&
			    pos->weak_group) {
			        pos = perf_evlist__reset_weak_group(evlist, pos, true);
				goto try_again;
			}
			rc = -errno;
			evsel__open_strerror(pos, &opts->target, errno, msg, sizeof(msg));
			ui__error("%s\n", msg);
			goto out;
		}

		pos->supported = true;
	}

	if (symbol_conf.kptr_restrict && !perf_evlist__exclude_kernel(evlist)) {
		pr_warning(
"WARNING: Kernel address maps (/proc/{kallsyms,modules}) are restricted,\n"
"check /proc/sys/kernel/kptr_restrict and /proc/sys/kernel/perf_event_paranoid.\n\n"
"Samples in kernel functions may not be resolved if a suitable vmlinux\n"
"file is not found in the buildid cache or in the vmlinux path.\n\n"
"Samples in kernel modules won't be resolved at all.\n\n"
"If some relocation was applied (e.g. kexec) symbols may be misresolved\n"
"even with a suitable vmlinux or kallsyms file.\n\n");
	}

	if (perf_evlist__apply_filters(evlist, &pos)) {
		pr_err("failed to set filter \"%s\" on event %s with %d (%s)\n",
			pos->filter, evsel__name(pos), errno,
			str_error_r(errno, msg, sizeof(msg)));
		rc = -1;
		goto out;
	}

	rc = record__mmap(rec);
	if (rc)
		goto out;

	session->evlist = evlist;
	perf_session__set_id_hdr_size(session);
out:
	return rc;
}

static int process_sample_event(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct evsel *evsel,
				struct machine *machine)
{
	struct record *rec = container_of(tool, struct record, tool);

	if (rec->evlist->first_sample_time == 0)
		rec->evlist->first_sample_time = sample->time;

	rec->evlist->last_sample_time = sample->time;

	if (rec->buildid_all)
		return 0;

	rec->samples++;
	return build_id__mark_dso_hit(tool, event, sample, evsel, machine);
}

static int process_buildids(struct record *rec)
{
	struct perf_session *session = rec->session;

	if (perf_data__size(&rec->data) == 0)
		return 0;

	/*
	 * During this process, it'll load kernel map and replace the
	 * dso->long_name to a real pathname it found.  In this case
	 * we prefer the vmlinux path like
	 *   /lib/modules/3.16.4/build/vmlinux
	 *
	 * rather than build-id path (in debug directory).
	 *   $HOME/.debug/.build-id/f0/6e17aa50adf4d00b88925e03775de107611551
	 */
	symbol_conf.ignore_vmlinux_buildid = true;

	/*
	 * If --buildid-all is given, it marks all DSO regardless of hits,
	 * so no need to process samples. But if timestamp_boundary is enabled,
	 * it still needs to walk on all samples to get the timestamps of
	 * first/last samples.
	 */
	if (rec->buildid_all && !rec->timestamp_boundary)
		rec->tool.sample = NULL;

	return perf_session__process_events(session);
}

static void perf_event__synthesize_guest_os(struct machine *machine, void *data)
{
	int err;
	struct perf_tool *tool = data;
	/*
	 *As for guest kernel when processing subcommand record&report,
	 *we arrange module mmap prior to guest kernel mmap and trigger
	 *a preload dso because default guest module symbols are loaded
	 *from guest kallsyms instead of /lib/modules/XXX/XXX. This
	 *method is used to avoid symbol missing when the first addr is
	 *in module instead of in guest kernel.
	 */
	err = perf_event__synthesize_modules(tool, process_synthesized_event,
					     machine);
	if (err < 0)
		pr_err("Couldn't record guest kernel [%d]'s reference"
		       " relocation symbol.\n", machine->pid);

	/*
	 * We use _stext for guest kernel because guest kernel's /proc/kallsyms
	 * have no _text sometimes.
	 */
	err = perf_event__synthesize_kernel_mmap(tool, process_synthesized_event,
						 machine);
	if (err < 0)
		pr_err("Couldn't record guest kernel [%d]'s reference"
		       " relocation symbol.\n", machine->pid);
}

static struct perf_event_header finished_round_event = {
	.size = sizeof(struct perf_event_header),
	.type = PERF_RECORD_FINISHED_ROUND,
};

static void record__adjust_affinity(struct record *rec, struct mmap *map)
{
	if (rec->opts.affinity != PERF_AFFINITY_SYS &&
	    !bitmap_equal(rec->affinity_mask.bits, map->affinity_mask.bits,
			  rec->affinity_mask.nbits)) {
		bitmap_zero(rec->affinity_mask.bits, rec->affinity_mask.nbits);
		bitmap_or(rec->affinity_mask.bits, rec->affinity_mask.bits,
			  map->affinity_mask.bits, rec->affinity_mask.nbits);
		sched_setaffinity(0, MMAP_CPU_MASK_BYTES(&rec->affinity_mask),
				  (cpu_set_t *)rec->affinity_mask.bits);
		if (verbose == 2)
			mmap_cpu_mask__scnprintf(&rec->affinity_mask, "thread");
	}
}

static size_t process_comp_header(void *record, size_t increment)
{
	struct perf_record_compressed *event = record;
	size_t size = sizeof(*event);

	if (increment) {
		event->header.size += increment;
		return increment;
	}

	event->header.type = PERF_RECORD_COMPRESSED;
	event->header.size = size;

	return size;
}

static size_t zstd_compress(struct perf_session *session, void *dst, size_t dst_size,
			    void *src, size_t src_size)
{
	size_t compressed;
	size_t max_record_size = PERF_SAMPLE_MAX_SIZE - sizeof(struct perf_record_compressed) - 1;

	compressed = zstd_compress_stream_to_records(&session->zstd_data, dst, dst_size, src, src_size,
						     max_record_size, process_comp_header);

	session->bytes_transferred += src_size;
	session->bytes_compressed  += compressed;

	return compressed;
}

static int record__mmap_read_evlist(struct record *rec, struct evlist *evlist,
				    bool overwrite, bool synch)
{
	u64 bytes_written = rec->bytes_written;
	int i;
	int rc = 0;
	struct mmap *maps;
	int trace_fd = rec->data.file.fd;
	off_t off = 0;

	if (!evlist)
		return 0;

	maps = overwrite ? evlist->overwrite_mmap : evlist->mmap;
	if (!maps)
		return 0;

	if (overwrite && evlist->bkw_mmap_state != BKW_MMAP_DATA_PENDING)
		return 0;

	if (record__aio_enabled(rec))
		off = record__aio_get_pos(trace_fd);

	for (i = 0; i < evlist->core.nr_mmaps; i++) {
		u64 flush = 0;
		struct mmap *map = &maps[i];

		if (map->core.base) {
			record__adjust_affinity(rec, map);
			if (synch) {
				flush = map->core.flush;
				map->core.flush = 1;
			}
			if (!record__aio_enabled(rec)) {
				if (perf_mmap__push(map, rec, record__pushfn) < 0) {
					if (synch)
						map->core.flush = flush;
					rc = -1;
					goto out;
				}
			} else {
				if (record__aio_push(rec, map, &off) < 0) {
					record__aio_set_pos(trace_fd, off);
					if (synch)
						map->core.flush = flush;
					rc = -1;
					goto out;
				}
			}
			if (synch)
				map->core.flush = flush;
		}

		if (map->auxtrace_mmap.base && !rec->opts.auxtrace_snapshot_mode &&
		    !rec->opts.auxtrace_sample_mode &&
		    record__auxtrace_mmap_read(rec, map) != 0) {
			rc = -1;
			goto out;
		}
	}

	if (record__aio_enabled(rec))
		record__aio_set_pos(trace_fd, off);

	/*
	 * Mark the round finished in case we wrote
	 * at least one event.
	 */
	if (bytes_written != rec->bytes_written)
		rc = record__write(rec, NULL, &finished_round_event, sizeof(finished_round_event));

	if (overwrite)
		perf_evlist__toggle_bkw_mmap(evlist, BKW_MMAP_EMPTY);
out:
	return rc;
}

static int record__mmap_read_all(struct record *rec, bool synch)
{
	int err;

	err = record__mmap_read_evlist(rec, rec->evlist, false, synch);
	if (err)
		return err;

	return record__mmap_read_evlist(rec, rec->evlist, true, synch);
}

static void record__init_features(struct record *rec)
{
	struct perf_session *session = rec->session;
	int feat;

	for (feat = HEADER_FIRST_FEATURE; feat < HEADER_LAST_FEATURE; feat++)
		perf_header__set_feat(&session->header, feat);

	if (rec->no_buildid)
		perf_header__clear_feat(&session->header, HEADER_BUILD_ID);

	if (!have_tracepoints(&rec->evlist->core.entries))
		perf_header__clear_feat(&session->header, HEADER_TRACING_DATA);

	if (!rec->opts.branch_stack)
		perf_header__clear_feat(&session->header, HEADER_BRANCH_STACK);

	if (!rec->opts.full_auxtrace)
		perf_header__clear_feat(&session->header, HEADER_AUXTRACE);

	if (!(rec->opts.use_clockid && rec->opts.clockid_res_ns))
		perf_header__clear_feat(&session->header, HEADER_CLOCKID);

	if (!rec->opts.use_clockid)
		perf_header__clear_feat(&session->header, HEADER_CLOCK_DATA);

	perf_header__clear_feat(&session->header, HEADER_DIR_FORMAT);
	if (!record__comp_enabled(rec))
		perf_header__clear_feat(&session->header, HEADER_COMPRESSED);

	perf_header__clear_feat(&session->header, HEADER_STAT);
}

static void
record__finish_output(struct record *rec)
{
	struct perf_data *data = &rec->data;
	int fd = perf_data__fd(data);

	if (data->is_pipe)
		return;

	rec->session->header.data_size += rec->bytes_written;
	data->file.size = lseek(perf_data__fd(data), 0, SEEK_CUR);

	if (!rec->no_buildid) {
		process_buildids(rec);

		if (rec->buildid_all)
			dsos__hit_all(rec->session);
	}
	perf_session__write_header(rec->session, rec->evlist, fd, true);

	return;
}

static int record__synthesize_workload(struct record *rec, bool tail)
{
	int err;
	struct perf_thread_map *thread_map;

	if (rec->opts.tail_synthesize != tail)
		return 0;

	thread_map = thread_map__new_by_tid(rec->evlist->workload.pid);
	if (thread_map == NULL)
		return -1;

	err = perf_event__synthesize_thread_map(&rec->tool, thread_map,
						 process_synthesized_event,
						 &rec->session->machines.host,
						 rec->opts.sample_address);
	perf_thread_map__put(thread_map);
	return err;
}

static int record__synthesize(struct record *rec, bool tail);

static int
record__switch_output(struct record *rec, bool at_exit)
{
	struct perf_data *data = &rec->data;
	int fd, err;
	char *new_filename;

	/* Same Size:      "2015122520103046"*/
	char timestamp[] = "InvalidTimestamp";

	record__aio_mmap_read_sync(rec);

	record__synthesize(rec, true);
	if (target__none(&rec->opts.target))
		record__synthesize_workload(rec, true);

	rec->samples = 0;
	record__finish_output(rec);
	err = fetch_current_timestamp(timestamp, sizeof(timestamp));
	if (err) {
		pr_err("Failed to get current timestamp\n");
		return -EINVAL;
	}

	fd = perf_data__switch(data, timestamp,
				    rec->session->header.data_offset,
				    at_exit, &new_filename);
	if (fd >= 0 && !at_exit) {
		rec->bytes_written = 0;
		rec->session->header.data_size = 0;
	}

	if (!quiet)
		fprintf(stderr, "[ perf record: Dump %s.%s ]\n",
			data->path, timestamp);

	if (rec->switch_output.num_files) {
		int n = rec->switch_output.cur_file + 1;

		if (n >= rec->switch_output.num_files)
			n = 0;
		rec->switch_output.cur_file = n;
		if (rec->switch_output.filenames[n]) {
			remove(rec->switch_output.filenames[n]);
			zfree(&rec->switch_output.filenames[n]);
		}
		rec->switch_output.filenames[n] = new_filename;
	} else {
		free(new_filename);
	}

	/* Output tracking events */
	if (!at_exit) {
		record__synthesize(rec, false);

		/*
		 * In 'perf record --switch-output' without -a,
		 * record__synthesize() in record__switch_output() won't
		 * generate tracking events because there's no thread_map
		 * in evlist. Which causes newly created perf.data doesn't
		 * contain map and comm information.
		 * Create a fake thread_map and directly call
		 * perf_event__synthesize_thread_map() for those events.
		 */
		if (target__none(&rec->opts.target))
			record__synthesize_workload(rec, false);
	}
	return fd;
}

static volatile int workload_exec_errno;

/*
 * perf_evlist__prepare_workload will send a SIGUSR1
 * if the fork fails, since we asked by setting its
 * want_signal to true.
 */
static void workload_exec_failed_signal(int signo __maybe_unused,
					siginfo_t *info,
					void *ucontext __maybe_unused)
{
	workload_exec_errno = info->si_value.sival_int;
	done = 1;
	child_finished = 1;
}

static void snapshot_sig_handler(int sig);
static void alarm_sig_handler(int sig);

static const struct perf_event_mmap_page *
perf_evlist__pick_pc(struct evlist *evlist)
{
	if (evlist) {
		if (evlist->mmap && evlist->mmap[0].core.base)
			return evlist->mmap[0].core.base;
		if (evlist->overwrite_mmap && evlist->overwrite_mmap[0].core.base)
			return evlist->overwrite_mmap[0].core.base;
	}
	return NULL;
}

static const struct perf_event_mmap_page *record__pick_pc(struct record *rec)
{
	const struct perf_event_mmap_page *pc;

	pc = perf_evlist__pick_pc(rec->evlist);
	if (pc)
		return pc;
	return NULL;
}

static int record__synthesize(struct record *rec, bool tail)
{
	struct perf_session *session = rec->session;
	struct machine *machine = &session->machines.host;
	struct perf_data *data = &rec->data;
	struct record_opts *opts = &rec->opts;
	struct perf_tool *tool = &rec->tool;
	int fd = perf_data__fd(data);
	int err = 0;
	event_op f = process_synthesized_event;

	if (rec->opts.tail_synthesize != tail)
		return 0;

	if (data->is_pipe) {
		/*
		 * We need to synthesize events first, because some
		 * features works on top of them (on report side).
		 */
		err = perf_event__synthesize_attrs(tool, rec->evlist,
						   process_synthesized_event);
		if (err < 0) {
			pr_err("Couldn't synthesize attrs.\n");
			goto out;
		}

		err = perf_event__synthesize_features(tool, session, rec->evlist,
						      process_synthesized_event);
		if (err < 0) {
			pr_err("Couldn't synthesize features.\n");
			return err;
		}

		if (have_tracepoints(&rec->evlist->core.entries)) {
			/*
			 * FIXME err <= 0 here actually means that
			 * there were no tracepoints so its not really
			 * an error, just that we don't need to
			 * synthesize anything.  We really have to
			 * return this more properly and also
			 * propagate errors that now are calling die()
			 */
			err = perf_event__synthesize_tracing_data(tool,	fd, rec->evlist,
								  process_synthesized_event);
			if (err <= 0) {
				pr_err("Couldn't record tracing data.\n");
				goto out;
			}
			rec->bytes_written += err;
		}
	}

	err = perf_event__synth_time_conv(record__pick_pc(rec), tool,
					  process_synthesized_event, machine);
	if (err)
		goto out;

	/* Synthesize id_index before auxtrace_info */
	if (rec->opts.auxtrace_sample_mode) {
		err = perf_event__synthesize_id_index(tool,
						      process_synthesized_event,
						      session->evlist, machine);
		if (err)
			goto out;
	}

	if (rec->opts.full_auxtrace) {
		err = perf_event__synthesize_auxtrace_info(rec->itr, tool,
					session, process_synthesized_event);
		if (err)
			goto out;
	}

	if (!perf_evlist__exclude_kernel(rec->evlist)) {
		err = perf_event__synthesize_kernel_mmap(tool, process_synthesized_event,
							 machine);
		WARN_ONCE(err < 0, "Couldn't record kernel reference relocation symbol\n"
				   "Symbol resolution may be skewed if relocation was used (e.g. kexec).\n"
				   "Check /proc/kallsyms permission or run as root.\n");

		err = perf_event__synthesize_modules(tool, process_synthesized_event,
						     machine);
		WARN_ONCE(err < 0, "Couldn't record kernel module information.\n"
				   "Symbol resolution may be skewed if relocation was used (e.g. kexec).\n"
				   "Check /proc/modules permission or run as root.\n");
	}

	if (perf_guest) {
		machines__process_guests(&session->machines,
					 perf_event__synthesize_guest_os, tool);
	}

	err = perf_event__synthesize_extra_attr(&rec->tool,
						rec->evlist,
						process_synthesized_event,
						data->is_pipe);
	if (err)
		goto out;

	err = perf_event__synthesize_thread_map2(&rec->tool, rec->evlist->core.threads,
						 process_synthesized_event,
						NULL);
	if (err < 0) {
		pr_err("Couldn't synthesize thread map.\n");
		return err;
	}

	err = perf_event__synthesize_cpu_map(&rec->tool, rec->evlist->core.cpus,
					     process_synthesized_event, NULL);
	if (err < 0) {
		pr_err("Couldn't synthesize cpu map.\n");
		return err;
	}

	err = perf_event__synthesize_bpf_events(session, process_synthesized_event,
						machine, opts);
	if (err < 0)
		pr_warning("Couldn't synthesize bpf events.\n");

	err = perf_event__synthesize_cgroups(tool, process_synthesized_event,
					     machine);
	if (err < 0)
		pr_warning("Couldn't synthesize cgroup events.\n");

	if (rec->opts.nr_threads_synthesize > 1) {
		perf_set_multithreaded();
		f = process_locked_synthesized_event;
	}

	err = __machine__synthesize_threads(machine, tool, &opts->target, rec->evlist->core.threads,
					    f, opts->sample_address,
					    rec->opts.nr_threads_synthesize);

	if (rec->opts.nr_threads_synthesize > 1)
		perf_set_singlethreaded();

out:
	return err;
}

static int record__process_signal_event(union perf_event *event __maybe_unused, void *data)
{
	struct record *rec = data;
	pthread_kill(rec->thread_id, SIGUSR2);
	return 0;
}

static int record__setup_sb_evlist(struct record *rec)
{
	struct record_opts *opts = &rec->opts;

	if (rec->sb_evlist != NULL) {
		/*
		 * We get here if --switch-output-event populated the
		 * sb_evlist, so associate a callback that will send a SIGUSR2
		 * to the main thread.
		 */
		evlist__set_cb(rec->sb_evlist, record__process_signal_event, rec);
		rec->thread_id = pthread_self();
	}
#ifdef HAVE_LIBBPF_SUPPORT
	if (!opts->no_bpf_event) {
		if (rec->sb_evlist == NULL) {
			rec->sb_evlist = evlist__new();

			if (rec->sb_evlist == NULL) {
				pr_err("Couldn't create side band evlist.\n.");
				return -1;
			}
		}

		if (evlist__add_bpf_sb_event(rec->sb_evlist, &rec->session->header.env)) {
			pr_err("Couldn't ask for PERF_RECORD_BPF_EVENT side band events.\n.");
			return -1;
		}
	}
#endif
	if (perf_evlist__start_sb_thread(rec->sb_evlist, &rec->opts.target)) {
		pr_debug("Couldn't start the BPF side band thread:\nBPF programs starting from now on won't be annotatable\n");
		opts->no_bpf_event = true;
	}

	return 0;
}

static int record__init_clock(struct record *rec)
{
	struct perf_session *session = rec->session;
	struct timespec ref_clockid;
	struct timeval ref_tod;
	u64 ref;

	if (!rec->opts.use_clockid)
		return 0;

	if (rec->opts.use_clockid && rec->opts.clockid_res_ns)
		session->header.env.clock.clockid_res_ns = rec->opts.clockid_res_ns;

	session->header.env.clock.clockid = rec->opts.clockid;

	if (gettimeofday(&ref_tod, NULL) != 0) {
		pr_err("gettimeofday failed, cannot set reference time.\n");
		return -1;
	}

	if (clock_gettime(rec->opts.clockid, &ref_clockid)) {
		pr_err("clock_gettime failed, cannot set reference time.\n");
		return -1;
	}

	ref = (u64) ref_tod.tv_sec * NSEC_PER_SEC +
	      (u64) ref_tod.tv_usec * NSEC_PER_USEC;

	session->header.env.clock.tod_ns = ref;

	ref = (u64) ref_clockid.tv_sec * NSEC_PER_SEC +
	      (u64) ref_clockid.tv_nsec;

	session->header.env.clock.clockid_ns = ref;
	return 0;
}

static void hit_auxtrace_snapshot_trigger(struct record *rec)
{
	if (trigger_is_ready(&auxtrace_snapshot_trigger)) {
		trigger_hit(&auxtrace_snapshot_trigger);
		auxtrace_record__snapshot_started = 1;
		if (auxtrace_record__snapshot_start(rec->itr))
			trigger_error(&auxtrace_snapshot_trigger);
	}
}

static int __cmd_record(struct record *rec, int argc, const char **argv)
{
	int err;
	int status = 0;
	unsigned long waking = 0;
	const bool forks = argc > 0;
	struct perf_tool *tool = &rec->tool;
	struct record_opts *opts = &rec->opts;
	struct perf_data *data = &rec->data;
	struct perf_session *session;
	bool disabled = false, draining = false;
	int fd;
	float ratio = 0;
	enum evlist_ctl_cmd cmd = EVLIST_CTL_CMD_UNSUPPORTED;

	atexit(record__sig_exit);
	signal(SIGCHLD, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGSEGV, sigsegv_handler);

	if (rec->opts.record_namespaces)
		tool->namespace_events = true;

	if (rec->opts.record_cgroup) {
#ifdef HAVE_FILE_HANDLE
		tool->cgroup_events = true;
#else
		pr_err("cgroup tracking is not supported\n");
		return -1;
#endif
	}

	if (rec->opts.auxtrace_snapshot_mode || rec->switch_output.enabled) {
		signal(SIGUSR2, snapshot_sig_handler);
		if (rec->opts.auxtrace_snapshot_mode)
			trigger_on(&auxtrace_snapshot_trigger);
		if (rec->switch_output.enabled)
			trigger_on(&switch_output_trigger);
	} else {
		signal(SIGUSR2, SIG_IGN);
	}

	session = perf_session__new(data, false, tool);
	if (IS_ERR(session)) {
		pr_err("Perf session creation failed.\n");
		return PTR_ERR(session);
	}

	fd = perf_data__fd(data);
	rec->session = session;

	if (zstd_init(&session->zstd_data, rec->opts.comp_level) < 0) {
		pr_err("Compression initialization failed.\n");
		return -1;
	}
#ifdef HAVE_EVENTFD_SUPPORT
	done_fd = eventfd(0, EFD_NONBLOCK);
	if (done_fd < 0) {
		pr_err("Failed to create wakeup eventfd, error: %m\n");
		status = -1;
		goto out_delete_session;
	}
	err = evlist__add_pollfd(rec->evlist, done_fd);
	if (err < 0) {
		pr_err("Failed to add wakeup eventfd to poll list\n");
		status = err;
		goto out_delete_session;
	}
#endif // HAVE_EVENTFD_SUPPORT

	session->header.env.comp_type  = PERF_COMP_ZSTD;
	session->header.env.comp_level = rec->opts.comp_level;

	if (rec->opts.kcore &&
	    !record__kcore_readable(&session->machines.host)) {
		pr_err("ERROR: kcore is not readable.\n");
		return -1;
	}

	if (record__init_clock(rec))
		return -1;

	record__init_features(rec);

	if (forks) {
		err = perf_evlist__prepare_workload(rec->evlist, &opts->target,
						    argv, data->is_pipe,
						    workload_exec_failed_signal);
		if (err < 0) {
			pr_err("Couldn't run the workload!\n");
			status = err;
			goto out_delete_session;
		}
	}

	/*
	 * If we have just single event and are sending data
	 * through pipe, we need to force the ids allocation,
	 * because we synthesize event name through the pipe
	 * and need the id for that.
	 */
	if (data->is_pipe && rec->evlist->core.nr_entries == 1)
		rec->opts.sample_id = true;

	if (record__open(rec) != 0) {
		err = -1;
		goto out_child;
	}
	session->header.env.comp_mmap_len = session->evlist->core.mmap_len;

	if (rec->opts.kcore) {
		err = record__kcore_copy(&session->machines.host, data);
		if (err) {
			pr_err("ERROR: Failed to copy kcore\n");
			goto out_child;
		}
	}

	err = bpf__apply_obj_config();
	if (err) {
		char errbuf[BUFSIZ];

		bpf__strerror_apply_obj_config(err, errbuf, sizeof(errbuf));
		pr_err("ERROR: Apply config to BPF failed: %s\n",
			 errbuf);
		goto out_child;
	}

	/*
	 * Normally perf_session__new would do this, but it doesn't have the
	 * evlist.
	 */
	if (rec->tool.ordered_events && !evlist__sample_id_all(rec->evlist)) {
		pr_warning("WARNING: No sample_id_all support, falling back to unordered processing\n");
		rec->tool.ordered_events = false;
	}

	if (!rec->evlist->nr_groups)
		perf_header__clear_feat(&session->header, HEADER_GROUP_DESC);

	if (data->is_pipe) {
		err = perf_header__write_pipe(fd);
		if (err < 0)
			goto out_child;
	} else {
		err = perf_session__write_header(session, rec->evlist, fd, false);
		if (err < 0)
			goto out_child;
	}

	err = -1;
	if (!rec->no_buildid
	    && !perf_header__has_feat(&session->header, HEADER_BUILD_ID)) {
		pr_err("Couldn't generate buildids. "
		       "Use --no-buildid to profile anyway.\n");
		goto out_child;
	}

	err = record__setup_sb_evlist(rec);
	if (err)
		goto out_child;

	err = record__synthesize(rec, false);
	if (err < 0)
		goto out_child;

	if (rec->realtime_prio) {
		struct sched_param param;

		param.sched_priority = rec->realtime_prio;
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			pr_err("Could not set realtime priority.\n");
			err = -1;
			goto out_child;
		}
	}

	/*
	 * When perf is starting the traced process, all the events
	 * (apart from group members) have enable_on_exec=1 set,
	 * so don't spoil it by prematurely enabling them.
	 */
	if (!target__none(&opts->target) && !opts->initial_delay)
		evlist__enable(rec->evlist);

	/*
	 * Let the child rip
	 */
	if (forks) {
		struct machine *machine = &session->machines.host;
		union perf_event *event;
		pid_t tgid;

		event = malloc(sizeof(event->comm) + machine->id_hdr_size);
		if (event == NULL) {
			err = -ENOMEM;
			goto out_child;
		}

		/*
		 * Some H/W events are generated before COMM event
		 * which is emitted during exec(), so perf script
		 * cannot see a correct process name for those events.
		 * Synthesize COMM event to prevent it.
		 */
		tgid = perf_event__synthesize_comm(tool, event,
						   rec->evlist->workload.pid,
						   process_synthesized_event,
						   machine);
		free(event);

		if (tgid == -1)
			goto out_child;

		event = malloc(sizeof(event->namespaces) +
			       (NR_NAMESPACES * sizeof(struct perf_ns_link_info)) +
			       machine->id_hdr_size);
		if (event == NULL) {
			err = -ENOMEM;
			goto out_child;
		}

		/*
		 * Synthesize NAMESPACES event for the command specified.
		 */
		perf_event__synthesize_namespaces(tool, event,
						  rec->evlist->workload.pid,
						  tgid, process_synthesized_event,
						  machine);
		free(event);

		perf_evlist__start_workload(rec->evlist);
	}

	if (evlist__initialize_ctlfd(rec->evlist, opts->ctl_fd, opts->ctl_fd_ack))
		goto out_child;

	if (opts->initial_delay) {
		pr_info(EVLIST_DISABLED_MSG);
		if (opts->initial_delay > 0) {
			usleep(opts->initial_delay * USEC_PER_MSEC);
			evlist__enable(rec->evlist);
			pr_info(EVLIST_ENABLED_MSG);
		}
	}

	trigger_ready(&auxtrace_snapshot_trigger);
	trigger_ready(&switch_output_trigger);
	perf_hooks__invoke_record_start();
	for (;;) {
		unsigned long long hits = rec->samples;

		/*
		 * rec->evlist->bkw_mmap_state is possible to be
		 * BKW_MMAP_EMPTY here: when done == true and
		 * hits != rec->samples in previous round.
		 *
		 * perf_evlist__toggle_bkw_mmap ensure we never
		 * convert BKW_MMAP_EMPTY to BKW_MMAP_DATA_PENDING.
		 */
		if (trigger_is_hit(&switch_output_trigger) || done || draining)
			perf_evlist__toggle_bkw_mmap(rec->evlist, BKW_MMAP_DATA_PENDING);

		if (record__mmap_read_all(rec, false) < 0) {
			trigger_error(&auxtrace_snapshot_trigger);
			trigger_error(&switch_output_trigger);
			err = -1;
			goto out_child;
		}

		if (auxtrace_record__snapshot_started) {
			auxtrace_record__snapshot_started = 0;
			if (!trigger_is_error(&auxtrace_snapshot_trigger))
				record__read_auxtrace_snapshot(rec, false);
			if (trigger_is_error(&auxtrace_snapshot_trigger)) {
				pr_err("AUX area tracing snapshot failed\n");
				err = -1;
				goto out_child;
			}
		}

		if (trigger_is_hit(&switch_output_trigger)) {
			/*
			 * If switch_output_trigger is hit, the data in
			 * overwritable ring buffer should have been collected,
			 * so bkw_mmap_state should be set to BKW_MMAP_EMPTY.
			 *
			 * If SIGUSR2 raise after or during record__mmap_read_all(),
			 * record__mmap_read_all() didn't collect data from
			 * overwritable ring buffer. Read again.
			 */
			if (rec->evlist->bkw_mmap_state == BKW_MMAP_RUNNING)
				continue;
			trigger_ready(&switch_output_trigger);

			/*
			 * Reenable events in overwrite ring buffer after
			 * record__mmap_read_all(): we should have collected
			 * data from it.
			 */
			perf_evlist__toggle_bkw_mmap(rec->evlist, BKW_MMAP_RUNNING);

			if (!quiet)
				fprintf(stderr, "[ perf record: dump data: Woken up %ld times ]\n",
					waking);
			waking = 0;
			fd = record__switch_output(rec, false);
			if (fd < 0) {
				pr_err("Failed to switch to new file\n");
				trigger_error(&switch_output_trigger);
				err = fd;
				goto out_child;
			}

			/* re-arm the alarm */
			if (rec->switch_output.time)
				alarm(rec->switch_output.time);
		}

		if (hits == rec->samples) {
			if (done || draining)
				break;
			err = evlist__poll(rec->evlist, -1);
			/*
			 * Propagate error, only if there's any. Ignore positive
			 * number of returned events and interrupt error.
			 */
			if (err > 0 || (err < 0 && errno == EINTR))
				err = 0;
			waking++;

			if (evlist__filter_pollfd(rec->evlist, POLLERR | POLLHUP) == 0)
				draining = true;
		}

		if (evlist__ctlfd_process(rec->evlist, &cmd) > 0) {
			switch (cmd) {
			case EVLIST_CTL_CMD_ENABLE:
				pr_info(EVLIST_ENABLED_MSG);
				break;
			case EVLIST_CTL_CMD_DISABLE:
				pr_info(EVLIST_DISABLED_MSG);
				break;
			case EVLIST_CTL_CMD_SNAPSHOT:
				hit_auxtrace_snapshot_trigger(rec);
				evlist__ctlfd_ack(rec->evlist);
				break;
			case EVLIST_CTL_CMD_ACK:
			case EVLIST_CTL_CMD_UNSUPPORTED:
			default:
				break;
			}
		}

		/*
		 * When perf is starting the traced process, at the end events
		 * die with the process and we wait for that. Thus no need to
		 * disable events in this case.
		 */
		if (done && !disabled && !target__none(&opts->target)) {
			trigger_off(&auxtrace_snapshot_trigger);
			evlist__disable(rec->evlist);
			disabled = true;
		}
	}

	trigger_off(&auxtrace_snapshot_trigger);
	trigger_off(&switch_output_trigger);

	if (opts->auxtrace_snapshot_on_exit)
		record__auxtrace_snapshot_exit(rec);

	if (forks && workload_exec_errno) {
		char msg[STRERR_BUFSIZE];
		const char *emsg = str_error_r(workload_exec_errno, msg, sizeof(msg));
		pr_err("Workload failed: %s\n", emsg);
		err = -1;
		goto out_child;
	}

	if (!quiet)
		fprintf(stderr, "[ perf record: Woken up %ld times to write data ]\n", waking);

	if (target__none(&rec->opts.target))
		record__synthesize_workload(rec, true);

out_child:
	evlist__finalize_ctlfd(rec->evlist);
	record__mmap_read_all(rec, true);
	record__aio_mmap_read_sync(rec);

	if (rec->session->bytes_transferred && rec->session->bytes_compressed) {
		ratio = (float)rec->session->bytes_transferred/(float)rec->session->bytes_compressed;
		session->header.env.comp_ratio = ratio + 0.5;
	}

	if (forks) {
		int exit_status;

		if (!child_finished)
			kill(rec->evlist->workload.pid, SIGTERM);

		wait(&exit_status);

		if (err < 0)
			status = err;
		else if (WIFEXITED(exit_status))
			status = WEXITSTATUS(exit_status);
		else if (WIFSIGNALED(exit_status))
			signr = WTERMSIG(exit_status);
	} else
		status = err;

	record__synthesize(rec, true);
	/* this will be recalculated during process_buildids() */
	rec->samples = 0;

	if (!err) {
		if (!rec->timestamp_filename) {
			record__finish_output(rec);
		} else {
			fd = record__switch_output(rec, true);
			if (fd < 0) {
				status = fd;
				goto out_delete_session;
			}
		}
	}

	perf_hooks__invoke_record_end();

	if (!err && !quiet) {
		char samples[128];
		const char *postfix = rec->timestamp_filename ?
					".<timestamp>" : "";

		if (rec->samples && !rec->opts.full_auxtrace)
			scnprintf(samples, sizeof(samples),
				  " (%" PRIu64 " samples)", rec->samples);
		else
			samples[0] = '\0';

		fprintf(stderr,	"[ perf record: Captured and wrote %.3f MB %s%s%s",
			perf_data__size(data) / 1024.0 / 1024.0,
			data->path, postfix, samples);
		if (ratio) {
			fprintf(stderr,	", compressed (original %.3f MB, ratio is %.3f)",
					rec->session->bytes_transferred / 1024.0 / 1024.0,
					ratio);
		}
		fprintf(stderr, " ]\n");
	}

out_delete_session:
#ifdef HAVE_EVENTFD_SUPPORT
	if (done_fd >= 0)
		close(done_fd);
#endif
	zstd_fini(&session->zstd_data);
	perf_session__delete(session);

	if (!opts->no_bpf_event)
		perf_evlist__stop_sb_thread(rec->sb_evlist);
	return status;
}

static void callchain_debug(struct callchain_param *callchain)
{
	static const char *str[CALLCHAIN_MAX] = { "NONE", "FP", "DWARF", "LBR" };

	pr_debug("callchain: type %s\n", str[callchain->record_mode]);

	if (callchain->record_mode == CALLCHAIN_DWARF)
		pr_debug("callchain: stack dump size %d\n",
			 callchain->dump_size);
}

int record_opts__parse_callchain(struct record_opts *record,
				 struct callchain_param *callchain,
				 const char *arg, bool unset)
{
	int ret;
	callchain->enabled = !unset;

	/* --no-call-graph */
	if (unset) {
		callchain->record_mode = CALLCHAIN_NONE;
		pr_debug("callchain: disabled\n");
		return 0;
	}

	ret = parse_callchain_record_opt(arg, callchain);
	if (!ret) {
		/* Enable data address sampling for DWARF unwind. */
		if (callchain->record_mode == CALLCHAIN_DWARF)
			record->sample_address = true;
		callchain_debug(callchain);
	}

	return ret;
}

int record_parse_callchain_opt(const struct option *opt,
			       const char *arg,
			       int unset)
{
	return record_opts__parse_callchain(opt->value, &callchain_param, arg, unset);
}

int record_callchain_opt(const struct option *opt,
			 const char *arg __maybe_unused,
			 int unset __maybe_unused)
{
	struct callchain_param *callchain = opt->value;

	callchain->enabled = true;

	if (callchain->record_mode == CALLCHAIN_NONE)
		callchain->record_mode = CALLCHAIN_FP;

	callchain_debug(callchain);
	return 0;
}

static int perf_record_config(const char *var, const char *value, void *cb)
{
	struct record *rec = cb;

	if (!strcmp(var, "record.build-id")) {
		if (!strcmp(value, "cache"))
			rec->no_buildid_cache = false;
		else if (!strcmp(value, "no-cache"))
			rec->no_buildid_cache = true;
		else if (!strcmp(value, "skip"))
			rec->no_buildid = true;
		else
			return -1;
		return 0;
	}
	if (!strcmp(var, "record.call-graph")) {
		var = "call-graph.record-mode";
		return perf_default_config(var, value, cb);
	}
#ifdef HAVE_AIO_SUPPORT
	if (!strcmp(var, "record.aio")) {
		rec->opts.nr_cblocks = strtol(value, NULL, 0);
		if (!rec->opts.nr_cblocks)
			rec->opts.nr_cblocks = nr_cblocks_default;
	}
#endif

	return 0;
}


static int record__parse_affinity(const struct option *opt, const char *str, int unset)
{
	struct record_opts *opts = (struct record_opts *)opt->value;

	if (unset || !str)
		return 0;

	if (!strcasecmp(str, "node"))
		opts->affinity = PERF_AFFINITY_NODE;
	else if (!strcasecmp(str, "cpu"))
		opts->affinity = PERF_AFFINITY_CPU;

	return 0;
}

static int parse_output_max_size(const struct option *opt,
				 const char *str, int unset)
{
	unsigned long *s = (unsigned long *)opt->value;
	static struct parse_tag tags_size[] = {
		{ .tag  = 'B', .mult = 1       },
		{ .tag  = 'K', .mult = 1 << 10 },
		{ .tag  = 'M', .mult = 1 << 20 },
		{ .tag  = 'G', .mult = 1 << 30 },
		{ .tag  = 0 },
	};
	unsigned long val;

	if (unset) {
		*s = 0;
		return 0;
	}

	val = parse_tag_value(str, tags_size);
	if (val != (unsigned long) -1) {
		*s = val;
		return 0;
	}

	return -1;
}

static int record__parse_mmap_pages(const struct option *opt,
				    const char *str,
				    int unset __maybe_unused)
{
	struct record_opts *opts = opt->value;
	char *s, *p;
	unsigned int mmap_pages;
	int ret;

	if (!str)
		return -EINVAL;

	s = strdup(str);
	if (!s)
		return -ENOMEM;

	p = strchr(s, ',');
	if (p)
		*p = '\0';

	if (*s) {
		ret = __perf_evlist__parse_mmap_pages(&mmap_pages, s);
		if (ret)
			goto out_free;
		opts->mmap_pages = mmap_pages;
	}

	if (!p) {
		ret = 0;
		goto out_free;
	}

	ret = __perf_evlist__parse_mmap_pages(&mmap_pages, p + 1);
	if (ret)
		goto out_free;

	opts->auxtrace_mmap_pages = mmap_pages;

out_free:
	free(s);
	return ret;
}

static int parse_control_option(const struct option *opt,
				const char *str,
				int unset __maybe_unused)
{
	struct record_opts *opts = opt->value;

	return evlist__parse_control(str, &opts->ctl_fd, &opts->ctl_fd_ack, &opts->ctl_fd_close);
}

static void switch_output_size_warn(struct record *rec)
{
	u64 wakeup_size = evlist__mmap_size(rec->opts.mmap_pages);
	struct switch_output *s = &rec->switch_output;

	wakeup_size /= 2;

	if (s->size < wakeup_size) {
		char buf[100];

		unit_number__scnprintf(buf, sizeof(buf), wakeup_size);
		pr_warning("WARNING: switch-output data size lower than "
			   "wakeup kernel buffer size (%s) "
			   "expect bigger perf.data sizes\n", buf);
	}
}

static int switch_output_setup(struct record *rec)
{
	struct switch_output *s = &rec->switch_output;
	static struct parse_tag tags_size[] = {
		{ .tag  = 'B', .mult = 1       },
		{ .tag  = 'K', .mult = 1 << 10 },
		{ .tag  = 'M', .mult = 1 << 20 },
		{ .tag  = 'G', .mult = 1 << 30 },
		{ .tag  = 0 },
	};
	static struct parse_tag tags_time[] = {
		{ .tag  = 's', .mult = 1        },
		{ .tag  = 'm', .mult = 60       },
		{ .tag  = 'h', .mult = 60*60    },
		{ .tag  = 'd', .mult = 60*60*24 },
		{ .tag  = 0 },
	};
	unsigned long val;

	/*
	 * If we're using --switch-output-events, then we imply its 
	 * --switch-output=signal, as we'll send a SIGUSR2 from the side band
	 *  thread to its parent.
	 */
	if (rec->switch_output_event_set)
		goto do_signal;

	if (!s->set)
		return 0;

	if (!strcmp(s->str, "signal")) {
do_signal:
		s->signal = true;
		pr_debug("switch-output with SIGUSR2 signal\n");
		goto enabled;
	}

	val = parse_tag_value(s->str, tags_size);
	if (val != (unsigned long) -1) {
		s->size = val;
		pr_debug("switch-output with %s size threshold\n", s->str);
		goto enabled;
	}

	val = parse_tag_value(s->str, tags_time);
	if (val != (unsigned long) -1) {
		s->time = val;
		pr_debug("switch-output with %s time threshold (%lu seconds)\n",
			 s->str, s->time);
		goto enabled;
	}

	return -1;

enabled:
	rec->timestamp_filename = true;
	s->enabled              = true;

	if (s->size && !rec->opts.no_buffering)
		switch_output_size_warn(rec);

	return 0;
}

static const char * const __record_usage[] = {
	"perf record [<options>] [<command>]",
	"perf record [<options>] -- <command> [<options>]",
	NULL
};
const char * const *record_usage = __record_usage;

static int build_id__process_mmap(struct perf_tool *tool, union perf_event *event,
				  struct perf_sample *sample, struct machine *machine)
{
	/*
	 * We already have the kernel maps, put in place via perf_session__create_kernel_maps()
	 * no need to add them twice.
	 */
	if (!(event->header.misc & PERF_RECORD_MISC_USER))
		return 0;
	return perf_event__process_mmap(tool, event, sample, machine);
}

static int build_id__process_mmap2(struct perf_tool *tool, union perf_event *event,
				   struct perf_sample *sample, struct machine *machine)
{
	/*
	 * We already have the kernel maps, put in place via perf_session__create_kernel_maps()
	 * no need to add them twice.
	 */
	if (!(event->header.misc & PERF_RECORD_MISC_USER))
		return 0;

	return perf_event__process_mmap2(tool, event, sample, machine);
}

/*
 * XXX Ideally would be local to cmd_record() and passed to a record__new
 * because we need to have access to it in record__exit, that is called
 * after cmd_record() exits, but since record_options need to be accessible to
 * builtin-script, leave it here.
 *
 * At least we don't ouch it in all the other functions here directly.
 *
 * Just say no to tons of global variables, sigh.
 */
static struct record record = {
	.opts = {
		.sample_time	     = true,
		.mmap_pages	     = UINT_MAX,
		.user_freq	     = UINT_MAX,
		.user_interval	     = ULLONG_MAX,
		.freq		     = 4000,
		.target		     = {
			.uses_mmap   = true,
			.default_per_cpu = true,
		},
		.mmap_flush          = MMAP_FLUSH_DEFAULT,
		.nr_threads_synthesize = 1,
		.ctl_fd              = -1,
		.ctl_fd_ack          = -1,
	},
	.tool = {
		.sample		= process_sample_event,
		.fork		= perf_event__process_fork,
		.exit		= perf_event__process_exit,
		.comm		= perf_event__process_comm,
		.namespaces	= perf_event__process_namespaces,
		.mmap		= build_id__process_mmap,
		.mmap2		= build_id__process_mmap2,
		.ordered_events	= true,
	},
};

const char record_callchain_help[] = CALLCHAIN_RECORD_HELP
	"\n\t\t\t\tDefault: fp";

static bool dry_run;

/*
 * XXX Will stay a global variable till we fix builtin-script.c to stop messing
 * with it and switch to use the library functions in perf_evlist that came
 * from builtin-record.c, i.e. use record_opts,
 * perf_evlist__prepare_workload, etc instead of fork+exec'in 'perf record',
 * using pipes, etc.
 */
static struct option __record_options[] = {
	OPT_CALLBACK('e', "event", &record.evlist, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events_option),
	OPT_CALLBACK(0, "filter", &record.evlist, "filter",
		     "event filter", parse_filter),
	OPT_CALLBACK_NOOPT(0, "exclude-perf", &record.evlist,
			   NULL, "don't record events from perf itself",
			   exclude_perf),
	OPT_STRING('p', "pid", &record.opts.target.pid, "pid",
		    "record events on existing process id"),
	OPT_STRING('t', "tid", &record.opts.target.tid, "tid",
		    "record events on existing thread id"),
	OPT_INTEGER('r', "realtime", &record.realtime_prio,
		    "collect data with this RT SCHED_FIFO priority"),
	OPT_BOOLEAN(0, "no-buffering", &record.opts.no_buffering,
		    "collect data without buffering"),
	OPT_BOOLEAN('R', "raw-samples", &record.opts.raw_samples,
		    "collect raw sample records from all opened counters"),
	OPT_BOOLEAN('a', "all-cpus", &record.opts.target.system_wide,
			    "system-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &record.opts.target.cpu_list, "cpu",
		    "list of cpus to monitor"),
	OPT_U64('c', "count", &record.opts.user_interval, "event period to sample"),
	OPT_STRING('o', "output", &record.data.path, "file",
		    "output file name"),
	OPT_BOOLEAN_SET('i', "no-inherit", &record.opts.no_inherit,
			&record.opts.no_inherit_set,
			"child tasks do not inherit counters"),
	OPT_BOOLEAN(0, "tail-synthesize", &record.opts.tail_synthesize,
		    "synthesize non-sample events at the end of output"),
	OPT_BOOLEAN(0, "overwrite", &record.opts.overwrite, "use overwrite mode"),
	OPT_BOOLEAN(0, "no-bpf-event", &record.opts.no_bpf_event, "do not record bpf events"),
	OPT_BOOLEAN(0, "strict-freq", &record.opts.strict_freq,
		    "Fail if the specified frequency can't be used"),
	OPT_CALLBACK('F', "freq", &record.opts, "freq or 'max'",
		     "profile at this frequency",
		      record__parse_freq),
	OPT_CALLBACK('m', "mmap-pages", &record.opts, "pages[,pages]",
		     "number of mmap data pages and AUX area tracing mmap pages",
		     record__parse_mmap_pages),
	OPT_CALLBACK(0, "mmap-flush", &record.opts, "number",
		     "Minimal number of bytes that is extracted from mmap data pages (default: 1)",
		     record__mmap_flush_parse),
	OPT_BOOLEAN(0, "group", &record.opts.group,
		    "put the counters into a counter group"),
	OPT_CALLBACK_NOOPT('g', NULL, &callchain_param,
			   NULL, "enables call-graph recording" ,
			   &record_callchain_opt),
	OPT_CALLBACK(0, "call-graph", &record.opts,
		     "record_mode[,record_size]", record_callchain_help,
		     &record_parse_callchain_opt),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_BOOLEAN('q', "quiet", &quiet, "don't print any message"),
	OPT_BOOLEAN('s', "stat", &record.opts.inherit_stat,
		    "per thread counts"),
	OPT_BOOLEAN('d', "data", &record.opts.sample_address, "Record the sample addresses"),
	OPT_BOOLEAN(0, "phys-data", &record.opts.sample_phys_addr,
		    "Record the sample physical addresses"),
	OPT_BOOLEAN(0, "sample-cpu", &record.opts.sample_cpu, "Record the sample cpu"),
	OPT_BOOLEAN_SET('T', "timestamp", &record.opts.sample_time,
			&record.opts.sample_time_set,
			"Record the sample timestamps"),
	OPT_BOOLEAN_SET('P', "period", &record.opts.period, &record.opts.period_set,
			"Record the sample period"),
	OPT_BOOLEAN('n', "no-samples", &record.opts.no_samples,
		    "don't sample"),
	OPT_BOOLEAN_SET('N', "no-buildid-cache", &record.no_buildid_cache,
			&record.no_buildid_cache_set,
			"do not update the buildid cache"),
	OPT_BOOLEAN_SET('B', "no-buildid", &record.no_buildid,
			&record.no_buildid_set,
			"do not collect buildids in perf.data"),
	OPT_CALLBACK('G', "cgroup", &record.evlist, "name",
		     "monitor event in cgroup name only",
		     parse_cgroups),
	OPT_INTEGER('D', "delay", &record.opts.initial_delay,
		  "ms to wait before starting measurement after program start (-1: start with events disabled)"),
	OPT_BOOLEAN(0, "kcore", &record.opts.kcore, "copy /proc/kcore"),
	OPT_STRING('u', "uid", &record.opts.target.uid_str, "user",
		   "user to profile"),

	OPT_CALLBACK_NOOPT('b', "branch-any", &record.opts.branch_stack,
		     "branch any", "sample any taken branches",
		     parse_branch_stack),

	OPT_CALLBACK('j', "branch-filter", &record.opts.branch_stack,
		     "branch filter mask", "branch stack filter modes",
		     parse_branch_stack),
	OPT_BOOLEAN('W', "weight", &record.opts.sample_weight,
		    "sample by weight (on special events only)"),
	OPT_BOOLEAN(0, "transaction", &record.opts.sample_transaction,
		    "sample transaction flags (special events only)"),
	OPT_BOOLEAN(0, "per-thread", &record.opts.target.per_thread,
		    "use per-thread mmaps"),
	OPT_CALLBACK_OPTARG('I', "intr-regs", &record.opts.sample_intr_regs, NULL, "any register",
		    "sample selected machine registers on interrupt,"
		    " use '-I?' to list register names", parse_intr_regs),
	OPT_CALLBACK_OPTARG(0, "user-regs", &record.opts.sample_user_regs, NULL, "any register",
		    "sample selected machine registers on interrupt,"
		    " use '--user-regs=?' to list register names", parse_user_regs),
	OPT_BOOLEAN(0, "running-time", &record.opts.running_time,
		    "Record running/enabled time of read (:S) events"),
	OPT_CALLBACK('k', "clockid", &record.opts,
	"clockid", "clockid to use for events, see clock_gettime()",
	parse_clockid),
	OPT_STRING_OPTARG('S', "snapshot", &record.opts.auxtrace_snapshot_opts,
			  "opts", "AUX area tracing Snapshot Mode", ""),
	OPT_STRING_OPTARG(0, "aux-sample", &record.opts.auxtrace_sample_opts,
			  "opts", "sample AUX area", ""),
	OPT_UINTEGER(0, "proc-map-timeout", &proc_map_timeout,
			"per thread proc mmap processing timeout in ms"),
	OPT_BOOLEAN(0, "namespaces", &record.opts.record_namespaces,
		    "Record namespaces events"),
	OPT_BOOLEAN(0, "all-cgroups", &record.opts.record_cgroup,
		    "Record cgroup events"),
	OPT_BOOLEAN_SET(0, "switch-events", &record.opts.record_switch_events,
			&record.opts.record_switch_events_set,
			"Record context switch events"),
	OPT_BOOLEAN_FLAG(0, "all-kernel", &record.opts.all_kernel,
			 "Configure all used events to run in kernel space.",
			 PARSE_OPT_EXCLUSIVE),
	OPT_BOOLEAN_FLAG(0, "all-user", &record.opts.all_user,
			 "Configure all used events to run in user space.",
			 PARSE_OPT_EXCLUSIVE),
	OPT_BOOLEAN(0, "kernel-callchains", &record.opts.kernel_callchains,
		    "collect kernel callchains"),
	OPT_BOOLEAN(0, "user-callchains", &record.opts.user_callchains,
		    "collect user callchains"),
	OPT_STRING(0, "clang-path", &llvm_param.clang_path, "clang path",
		   "clang binary to use for compiling BPF scriptlets"),
	OPT_STRING(0, "clang-opt", &llvm_param.clang_opt, "clang options",
		   "options passed to clang when compiling BPF scriptlets"),
	OPT_STRING(0, "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_BOOLEAN(0, "buildid-all", &record.buildid_all,
		    "Record build-id of all DSOs regardless of hits"),
	OPT_BOOLEAN(0, "timestamp-filename", &record.timestamp_filename,
		    "append timestamp to output filename"),
	OPT_BOOLEAN(0, "timestamp-boundary", &record.timestamp_boundary,
		    "Record timestamp boundary (time of first/last samples)"),
	OPT_STRING_OPTARG_SET(0, "switch-output", &record.switch_output.str,
			  &record.switch_output.set, "signal or size[BKMG] or time[smhd]",
			  "Switch output when receiving SIGUSR2 (signal) or cross a size or time threshold",
			  "signal"),
	OPT_CALLBACK_SET(0, "switch-output-event", &record.sb_evlist, &record.switch_output_event_set, "switch output event",
			 "switch output event selector. use 'perf list' to list available events",
			 parse_events_option_new_evlist),
	OPT_INTEGER(0, "switch-max-files", &record.switch_output.num_files,
		   "Limit number of switch output generated files"),
	OPT_BOOLEAN(0, "dry-run", &dry_run,
		    "Parse options then exit"),
#ifdef HAVE_AIO_SUPPORT
	OPT_CALLBACK_OPTARG(0, "aio", &record.opts,
		     &nr_cblocks_default, "n", "Use <n> control blocks in asynchronous trace writing mode (default: 1, max: 4)",
		     record__aio_parse),
#endif
	OPT_CALLBACK(0, "affinity", &record.opts, "node|cpu",
		     "Set affinity mask of trace reading thread to NUMA node cpu mask or cpu of processed mmap buffer",
		     record__parse_affinity),
#ifdef HAVE_ZSTD_SUPPORT
	OPT_CALLBACK_OPTARG('z', "compression-level", &record.opts, &comp_level_default,
			    "n", "Compressed records using specified level (default: 1 - fastest compression, 22 - greatest compression)",
			    record__parse_comp_level),
#endif
	OPT_CALLBACK(0, "max-size", &record.output_max_size,
		     "size", "Limit the maximum size of the output file", parse_output_max_size),
	OPT_UINTEGER(0, "num-thread-synthesize",
		     &record.opts.nr_threads_synthesize,
		     "number of threads to run for event synthesis"),
#ifdef HAVE_LIBPFM
	OPT_CALLBACK(0, "pfm-events", &record.evlist, "event",
		"libpfm4 event selector. use 'perf list' to list available events",
		parse_libpfm_events_option),
#endif
	OPT_CALLBACK(0, "control", &record.opts, "fd:ctl-fd[,ack-fd] or fifo:ctl-fifo[,ack-fifo]",
		     "Listen on ctl-fd descriptor for command to control measurement ('enable': enable events, 'disable': disable events,\n"
		     "\t\t\t  'snapshot': AUX area tracing snapshot).\n"
		     "\t\t\t  Optionally send control command completion ('ack\\n') to ack-fd descriptor.\n"
		     "\t\t\t  Alternatively, ctl-fifo / ack-fifo will be opened and used as ctl-fd / ack-fd.",
		      parse_control_option),
	OPT_END()
};

struct option *record_options = __record_options;

int cmd_record(int argc, const char **argv)
{
	int err;
	struct record *rec = &record;
	char errbuf[BUFSIZ];

	setlocale(LC_ALL, "");

#ifndef HAVE_LIBBPF_SUPPORT
# define set_nobuild(s, l, c) set_option_nobuild(record_options, s, l, "NO_LIBBPF=1", c)
	set_nobuild('\0', "clang-path", true);
	set_nobuild('\0', "clang-opt", true);
# undef set_nobuild
#endif

#ifndef HAVE_BPF_PROLOGUE
# if !defined (HAVE_DWARF_SUPPORT)
#  define REASON  "NO_DWARF=1"
# elif !defined (HAVE_LIBBPF_SUPPORT)
#  define REASON  "NO_LIBBPF=1"
# else
#  define REASON  "this architecture doesn't support BPF prologue"
# endif
# define set_nobuild(s, l, c) set_option_nobuild(record_options, s, l, REASON, c)
	set_nobuild('\0', "vmlinux", true);
# undef set_nobuild
# undef REASON
#endif

	rec->opts.affinity = PERF_AFFINITY_SYS;

	rec->evlist = evlist__new();
	if (rec->evlist == NULL)
		return -ENOMEM;

	err = perf_config(perf_record_config, rec);
	if (err)
		return err;

	argc = parse_options(argc, argv, record_options, record_usage,
			    PARSE_OPT_STOP_AT_NON_OPTION);
	if (quiet)
		perf_quiet_option();

	/* Make system wide (-a) the default target. */
	if (!argc && target__none(&rec->opts.target))
		rec->opts.target.system_wide = true;

	if (nr_cgroups && !rec->opts.target.system_wide) {
		usage_with_options_msg(record_usage, record_options,
			"cgroup monitoring only available in system-wide mode");

	}

	if (rec->opts.kcore)
		rec->data.is_dir = true;

	if (rec->opts.comp_level != 0) {
		pr_debug("Compression enabled, disabling build id collection at the end of the session.\n");
		rec->no_buildid = true;
	}

	if (rec->opts.record_switch_events &&
	    !perf_can_record_switch_events()) {
		ui__error("kernel does not support recording context switch events\n");
		parse_options_usage(record_usage, record_options, "switch-events", 0);
		err = -EINVAL;
		goto out_opts;
	}

	if (switch_output_setup(rec)) {
		parse_options_usage(record_usage, record_options, "switch-output", 0);
		err = -EINVAL;
		goto out_opts;
	}

	if (rec->switch_output.time) {
		signal(SIGALRM, alarm_sig_handler);
		alarm(rec->switch_output.time);
	}

	if (rec->switch_output.num_files) {
		rec->switch_output.filenames = calloc(sizeof(char *),
						      rec->switch_output.num_files);
		if (!rec->switch_output.filenames) {
			err = -EINVAL;
			goto out_opts;
		}
	}

	/*
	 * Allow aliases to facilitate the lookup of symbols for address
	 * filters. Refer to auxtrace_parse_filters().
	 */
	symbol_conf.allow_aliases = true;

	symbol__init(NULL);

	if (rec->opts.affinity != PERF_AFFINITY_SYS) {
		rec->affinity_mask.nbits = cpu__max_cpu();
		rec->affinity_mask.bits = bitmap_alloc(rec->affinity_mask.nbits);
		if (!rec->affinity_mask.bits) {
			pr_err("Failed to allocate thread mask for %zd cpus\n", rec->affinity_mask.nbits);
			err = -ENOMEM;
			goto out_opts;
		}
		pr_debug2("thread mask[%zd]: empty\n", rec->affinity_mask.nbits);
	}

	err = record__auxtrace_init(rec);
	if (err)
		goto out;

	if (dry_run)
		goto out;

	err = bpf__setup_stdout(rec->evlist);
	if (err) {
		bpf__strerror_setup_stdout(rec->evlist, err, errbuf, sizeof(errbuf));
		pr_err("ERROR: Setup BPF stdout failed: %s\n",
			 errbuf);
		goto out;
	}

	err = -ENOMEM;

	if (rec->no_buildid_cache || rec->no_buildid) {
		disable_buildid_cache();
	} else if (rec->switch_output.enabled) {
		/*
		 * In 'perf record --switch-output', disable buildid
		 * generation by default to reduce data file switching
		 * overhead. Still generate buildid if they are required
		 * explicitly using
		 *
		 *  perf record --switch-output --no-no-buildid \
		 *              --no-no-buildid-cache
		 *
		 * Following code equals to:
		 *
		 * if ((rec->no_buildid || !rec->no_buildid_set) &&
		 *     (rec->no_buildid_cache || !rec->no_buildid_cache_set))
		 *         disable_buildid_cache();
		 */
		bool disable = true;

		if (rec->no_buildid_set && !rec->no_buildid)
			disable = false;
		if (rec->no_buildid_cache_set && !rec->no_buildid_cache)
			disable = false;
		if (disable) {
			rec->no_buildid = true;
			rec->no_buildid_cache = true;
			disable_buildid_cache();
		}
	}

	if (record.opts.overwrite)
		record.opts.tail_synthesize = true;

	if (rec->evlist->core.nr_entries == 0 &&
	    __evlist__add_default(rec->evlist, !record.opts.no_samples) < 0) {
		pr_err("Not enough memory for event selector list\n");
		goto out;
	}

	if (rec->opts.target.tid && !rec->opts.no_inherit_set)
		rec->opts.no_inherit = true;

	err = target__validate(&rec->opts.target);
	if (err) {
		target__strerror(&rec->opts.target, err, errbuf, BUFSIZ);
		ui__warning("%s\n", errbuf);
	}

	err = target__parse_uid(&rec->opts.target);
	if (err) {
		int saved_errno = errno;

		target__strerror(&rec->opts.target, err, errbuf, BUFSIZ);
		ui__error("%s", errbuf);

		err = -saved_errno;
		goto out;
	}

	/* Enable ignoring missing threads when -u/-p option is defined. */
	rec->opts.ignore_missing_thread = rec->opts.target.uid != UINT_MAX || rec->opts.target.pid;

	err = -ENOMEM;
	if (perf_evlist__create_maps(rec->evlist, &rec->opts.target) < 0)
		usage_with_options(record_usage, record_options);

	err = auxtrace_record__options(rec->itr, rec->evlist, &rec->opts);
	if (err)
		goto out;

	/*
	 * We take all buildids when the file contains
	 * AUX area tracing data because we do not decode the
	 * trace because it would take too long.
	 */
	if (rec->opts.full_auxtrace)
		rec->buildid_all = true;

	if (rec->opts.text_poke) {
		err = record__config_text_poke(rec->evlist);
		if (err) {
			pr_err("record__config_text_poke failed, error %d\n", err);
			goto out;
		}
	}

	if (record_opts__config(&rec->opts)) {
		err = -EINVAL;
		goto out;
	}

	if (rec->opts.nr_cblocks > nr_cblocks_max)
		rec->opts.nr_cblocks = nr_cblocks_max;
	pr_debug("nr_cblocks: %d\n", rec->opts.nr_cblocks);

	pr_debug("affinity: %s\n", affinity_tags[rec->opts.affinity]);
	pr_debug("mmap flush: %d\n", rec->opts.mmap_flush);

	if (rec->opts.comp_level > comp_level_max)
		rec->opts.comp_level = comp_level_max;
	pr_debug("comp level: %d\n", rec->opts.comp_level);

	err = __cmd_record(&record, argc, argv);
out:
	bitmap_free(rec->affinity_mask.bits);
	evlist__delete(rec->evlist);
	symbol__exit();
	auxtrace_record__free(rec->itr);
out_opts:
	evlist__close_control(rec->opts.ctl_fd, rec->opts.ctl_fd_ack, &rec->opts.ctl_fd_close);
	return err;
}

static void snapshot_sig_handler(int sig __maybe_unused)
{
	struct record *rec = &record;

	hit_auxtrace_snapshot_trigger(rec);

	if (switch_output_signal(rec))
		trigger_hit(&switch_output_trigger);
}

static void alarm_sig_handler(int sig __maybe_unused)
{
	struct record *rec = &record;

	if (switch_output_time(rec))
		trigger_hit(&switch_output_trigger);
}
