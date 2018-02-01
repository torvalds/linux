// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-record.c
 *
 * Builtin record command: Record the profile of a workload
 * (or a CPU, or a PID) into the perf.data output file - for
 * later analysis via perf report.
 */
#include "builtin.h"

#include "perf.h"

#include "util/build-id.h"
#include "util/util.h"
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
#include "util/drv_configs.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/symbol.h"
#include "util/cpumap.h"
#include "util/thread_map.h"
#include "util/data.h"
#include "util/perf_regs.h"
#include "util/auxtrace.h"
#include "util/tsc.h"
#include "util/parse-branch-options.h"
#include "util/parse-regs-options.h"
#include "util/llvm-utils.h"
#include "util/bpf-loader.h"
#include "util/trigger.h"
#include "util/perf-hooks.h"
#include "util/time-utils.h"
#include "util/units.h"
#include "asm/bug.h"

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <linux/time64.h>

struct switch_output {
	bool		 enabled;
	bool		 signal;
	unsigned long	 size;
	unsigned long	 time;
	const char	*str;
	bool		 set;
};

struct record {
	struct perf_tool	tool;
	struct record_opts	opts;
	u64			bytes_written;
	struct perf_data	data;
	struct auxtrace_record	*itr;
	struct perf_evlist	*evlist;
	struct perf_session	*session;
	const char		*progname;
	int			realtime_prio;
	bool			no_buildid;
	bool			no_buildid_set;
	bool			no_buildid_cache;
	bool			no_buildid_cache_set;
	bool			buildid_all;
	bool			timestamp_filename;
	bool			timestamp_boundary;
	struct switch_output	switch_output;
	unsigned long long	samples;
};

static volatile int auxtrace_record__snapshot_started;
static DEFINE_TRIGGER(auxtrace_snapshot_trigger);
static DEFINE_TRIGGER(switch_output_trigger);

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

static int record__write(struct record *rec, void *bf, size_t size)
{
	if (perf_data__write(rec->session->data, bf, size) < 0) {
		pr_err("failed to write perf data, error: %m\n");
		return -1;
	}

	rec->bytes_written += size;

	if (switch_output_size(rec))
		trigger_hit(&switch_output_trigger);

	return 0;
}

static int process_synthesized_event(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample __maybe_unused,
				     struct machine *machine __maybe_unused)
{
	struct record *rec = container_of(tool, struct record, tool);
	return record__write(rec, event, event->header.size);
}

static int record__pushfn(void *to, void *bf, size_t size)
{
	struct record *rec = to;

	rec->samples++;
	return record__write(rec, bf, size);
}

static volatile int done;
static volatile int signr = -1;
static volatile int child_finished;

static void sig_handler(int sig)
{
	if (sig == SIGCHLD)
		child_finished = 1;
	else
		signr = sig;

	done = 1;
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
				    union perf_event *event, void *data1,
				    size_t len1, void *data2, size_t len2)
{
	struct record *rec = container_of(tool, struct record, tool);
	struct perf_data *data = &rec->data;
	size_t padding;
	u8 pad[8] = {0};

	if (!perf_data__is_pipe(data)) {
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

	record__write(rec, event, event->header.size);
	record__write(rec, data1, len1);
	if (len2)
		record__write(rec, data2, len2);
	record__write(rec, &pad, padding);

	return 0;
}

static int record__auxtrace_mmap_read(struct record *rec,
				      struct auxtrace_mmap *mm)
{
	int ret;

	ret = auxtrace_mmap__read(mm, rec->itr, &rec->tool,
				  record__process_auxtrace);
	if (ret < 0)
		return ret;

	if (ret)
		rec->samples++;

	return 0;
}

static int record__auxtrace_mmap_read_snapshot(struct record *rec,
					       struct auxtrace_mmap *mm)
{
	int ret;

	ret = auxtrace_mmap__read_snapshot(mm, rec->itr, &rec->tool,
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

	for (i = 0; i < rec->evlist->nr_mmaps; i++) {
		struct auxtrace_mmap *mm =
				&rec->evlist->mmap[i].auxtrace_mmap;

		if (!mm->base)
			continue;

		if (record__auxtrace_mmap_read_snapshot(rec, mm) != 0) {
			rc = -1;
			goto out;
		}
	}
out:
	return rc;
}

static void record__read_auxtrace_snapshot(struct record *rec)
{
	pr_debug("Recording AUX area tracing snapshot\n");
	if (record__auxtrace_read_snapshot_all(rec) < 0) {
		trigger_error(&auxtrace_snapshot_trigger);
	} else {
		if (auxtrace_record__snapshot_finish(rec->itr))
			trigger_error(&auxtrace_snapshot_trigger);
		else
			trigger_ready(&auxtrace_snapshot_trigger);
	}
}

#else

static inline
int record__auxtrace_mmap_read(struct record *rec __maybe_unused,
			       struct auxtrace_mmap *mm __maybe_unused)
{
	return 0;
}

static inline
void record__read_auxtrace_snapshot(struct record *rec __maybe_unused)
{
}

static inline
int auxtrace_record__snapshot_start(struct auxtrace_record *itr __maybe_unused)
{
	return 0;
}

#endif

static int record__mmap_evlist(struct record *rec,
			       struct perf_evlist *evlist)
{
	struct record_opts *opts = &rec->opts;
	char msg[512];

	if (perf_evlist__mmap_ex(evlist, opts->mmap_pages,
				 opts->auxtrace_mmap_pages,
				 opts->auxtrace_snapshot_mode) < 0) {
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
	struct perf_evsel *pos;
	struct perf_evlist *evlist = rec->evlist;
	struct perf_session *session = rec->session;
	struct record_opts *opts = &rec->opts;
	struct perf_evsel_config_term *err_term;
	int rc = 0;

	/*
	 * For initial_delay we need to add a dummy event so that we can track
	 * PERF_RECORD_MMAP while we wait for the initial delay to enable the
	 * real events, the ones asked by the user.
	 */
	if (opts->initial_delay) {
		if (perf_evlist__add_dummy(evlist))
			return -ENOMEM;

		pos = perf_evlist__first(evlist);
		pos->tracking = 0;
		pos = perf_evlist__last(evlist);
		pos->tracking = 1;
		pos->attr.enable_on_exec = 1;
	}

	perf_evlist__config(evlist, opts, &callchain_param);

	evlist__for_each_entry(evlist, pos) {
try_again:
		if (perf_evsel__open(pos, pos->cpus, pos->threads) < 0) {
			if (perf_evsel__fallback(pos, errno, msg, sizeof(msg))) {
				if (verbose > 0)
					ui__warning("%s\n", msg);
				goto try_again;
			}

			rc = -errno;
			perf_evsel__open_strerror(pos, &opts->target,
						  errno, msg, sizeof(msg));
			ui__error("%s\n", msg);
			goto out;
		}

		pos->supported = true;
	}

	if (perf_evlist__apply_filters(evlist, &pos)) {
		pr_err("failed to set filter \"%s\" on event %s with %d (%s)\n",
			pos->filter, perf_evsel__name(pos), errno,
			str_error_r(errno, msg, sizeof(msg)));
		rc = -1;
		goto out;
	}

	if (perf_evlist__apply_drv_configs(evlist, &pos, &err_term)) {
		pr_err("failed to set config \"%s\" on event %s with %d (%s)\n",
		      err_term->val.drv_cfg, perf_evsel__name(pos), errno,
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
				struct perf_evsel *evsel,
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
	struct perf_data *data = &rec->data;
	struct perf_session *session = rec->session;

	if (data->size == 0)
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

static int record__mmap_read_evlist(struct record *rec, struct perf_evlist *evlist,
				    bool overwrite)
{
	u64 bytes_written = rec->bytes_written;
	int i;
	int rc = 0;
	struct perf_mmap *maps;

	if (!evlist)
		return 0;

	maps = overwrite ? evlist->overwrite_mmap : evlist->mmap;
	if (!maps)
		return 0;

	if (overwrite && evlist->bkw_mmap_state != BKW_MMAP_DATA_PENDING)
		return 0;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		struct auxtrace_mmap *mm = &maps[i].auxtrace_mmap;

		if (maps[i].base) {
			if (perf_mmap__push(&maps[i], overwrite, rec, record__pushfn) != 0) {
				rc = -1;
				goto out;
			}
		}

		if (mm->base && !rec->opts.auxtrace_snapshot_mode &&
		    record__auxtrace_mmap_read(rec, mm) != 0) {
			rc = -1;
			goto out;
		}
	}

	/*
	 * Mark the round finished in case we wrote
	 * at least one event.
	 */
	if (bytes_written != rec->bytes_written)
		rc = record__write(rec, &finished_round_event, sizeof(finished_round_event));

	if (overwrite)
		perf_evlist__toggle_bkw_mmap(evlist, BKW_MMAP_EMPTY);
out:
	return rc;
}

static int record__mmap_read_all(struct record *rec)
{
	int err;

	err = record__mmap_read_evlist(rec, rec->evlist, false);
	if (err)
		return err;

	return record__mmap_read_evlist(rec, rec->evlist, true);
}

static void record__init_features(struct record *rec)
{
	struct perf_session *session = rec->session;
	int feat;

	for (feat = HEADER_FIRST_FEATURE; feat < HEADER_LAST_FEATURE; feat++)
		perf_header__set_feat(&session->header, feat);

	if (rec->no_buildid)
		perf_header__clear_feat(&session->header, HEADER_BUILD_ID);

	if (!have_tracepoints(&rec->evlist->entries))
		perf_header__clear_feat(&session->header, HEADER_TRACING_DATA);

	if (!rec->opts.branch_stack)
		perf_header__clear_feat(&session->header, HEADER_BRANCH_STACK);

	if (!rec->opts.full_auxtrace)
		perf_header__clear_feat(&session->header, HEADER_AUXTRACE);

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
	data->size = lseek(perf_data__fd(data), 0, SEEK_CUR);

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
	struct thread_map *thread_map;

	if (rec->opts.tail_synthesize != tail)
		return 0;

	thread_map = thread_map__new_by_tid(rec->evlist->workload.pid);
	if (thread_map == NULL)
		return -1;

	err = perf_event__synthesize_thread_map(&rec->tool, thread_map,
						 process_synthesized_event,
						 &rec->session->machines.host,
						 rec->opts.sample_address,
						 rec->opts.proc_map_timeout);
	thread_map__put(thread_map);
	return err;
}

static int record__synthesize(struct record *rec, bool tail);

static int
record__switch_output(struct record *rec, bool at_exit)
{
	struct perf_data *data = &rec->data;
	int fd, err;

	/* Same Size:      "2015122520103046"*/
	char timestamp[] = "InvalidTimestamp";

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
				    at_exit);
	if (fd >= 0 && !at_exit) {
		rec->bytes_written = 0;
		rec->session->header.data_size = 0;
	}

	if (!quiet)
		fprintf(stderr, "[ perf record: Dump %s.%s ]\n",
			data->file.path, timestamp);

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

int __weak
perf_event__synth_time_conv(const struct perf_event_mmap_page *pc __maybe_unused,
			    struct perf_tool *tool __maybe_unused,
			    perf_event__handler_t process __maybe_unused,
			    struct machine *machine __maybe_unused)
{
	return 0;
}

static const struct perf_event_mmap_page *
perf_evlist__pick_pc(struct perf_evlist *evlist)
{
	if (evlist) {
		if (evlist->mmap && evlist->mmap[0].base)
			return evlist->mmap[0].base;
		if (evlist->overwrite_mmap && evlist->overwrite_mmap[0].base)
			return evlist->overwrite_mmap[0].base;
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

	if (rec->opts.tail_synthesize != tail)
		return 0;

	if (data->is_pipe) {
		err = perf_event__synthesize_features(
			tool, session, rec->evlist, process_synthesized_event);
		if (err < 0) {
			pr_err("Couldn't synthesize features.\n");
			return err;
		}

		err = perf_event__synthesize_attrs(tool, session,
						   process_synthesized_event);
		if (err < 0) {
			pr_err("Couldn't synthesize attrs.\n");
			goto out;
		}

		if (have_tracepoints(&rec->evlist->entries)) {
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

	err = perf_event__synthesize_thread_map2(&rec->tool, rec->evlist->threads,
						 process_synthesized_event,
						NULL);
	if (err < 0) {
		pr_err("Couldn't synthesize thread map.\n");
		return err;
	}

	err = perf_event__synthesize_cpu_map(&rec->tool, rec->evlist->cpus,
					     process_synthesized_event, NULL);
	if (err < 0) {
		pr_err("Couldn't synthesize cpu map.\n");
		return err;
	}

	err = __machine__synthesize_threads(machine, tool, &opts->target, rec->evlist->threads,
					    process_synthesized_event, opts->sample_address,
					    opts->proc_map_timeout, 1);
out:
	return err;
}

static int __cmd_record(struct record *rec, int argc, const char **argv)
{
	int err;
	int status = 0;
	unsigned long waking = 0;
	const bool forks = argc > 0;
	struct machine *machine;
	struct perf_tool *tool = &rec->tool;
	struct record_opts *opts = &rec->opts;
	struct perf_data *data = &rec->data;
	struct perf_session *session;
	bool disabled = false, draining = false;
	int fd;

	rec->progname = argv[0];

	atexit(record__sig_exit);
	signal(SIGCHLD, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGSEGV, sigsegv_handler);

	if (rec->opts.record_namespaces)
		tool->namespace_events = true;

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
	if (session == NULL) {
		pr_err("Perf session creation failed.\n");
		return -1;
	}

	fd = perf_data__fd(data);
	rec->session = session;

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

	if (record__open(rec) != 0) {
		err = -1;
		goto out_child;
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
	if (rec->tool.ordered_events && !perf_evlist__sample_id_all(rec->evlist)) {
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

	if (!rec->no_buildid
	    && !perf_header__has_feat(&session->header, HEADER_BUILD_ID)) {
		pr_err("Couldn't generate buildids. "
		       "Use --no-buildid to profile anyway.\n");
		err = -1;
		goto out_child;
	}

	machine = &session->machines.host;

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
		perf_evlist__enable(rec->evlist);

	/*
	 * Let the child rip
	 */
	if (forks) {
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

	if (opts->initial_delay) {
		usleep(opts->initial_delay * USEC_PER_MSEC);
		perf_evlist__enable(rec->evlist);
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

		if (record__mmap_read_all(rec) < 0) {
			trigger_error(&auxtrace_snapshot_trigger);
			trigger_error(&switch_output_trigger);
			err = -1;
			goto out_child;
		}

		if (auxtrace_record__snapshot_started) {
			auxtrace_record__snapshot_started = 0;
			if (!trigger_is_error(&auxtrace_snapshot_trigger))
				record__read_auxtrace_snapshot(rec);
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
			err = perf_evlist__poll(rec->evlist, -1);
			/*
			 * Propagate error, only if there's any. Ignore positive
			 * number of returned events and interrupt error.
			 */
			if (err > 0 || (err < 0 && errno == EINTR))
				err = 0;
			waking++;

			if (perf_evlist__filter_pollfd(rec->evlist, POLLERR | POLLHUP) == 0)
				draining = true;
		}

		/*
		 * When perf is starting the traced process, at the end events
		 * die with the process and we wait for that. Thus no need to
		 * disable events in this case.
		 */
		if (done && !disabled && !target__none(&opts->target)) {
			trigger_off(&auxtrace_snapshot_trigger);
			perf_evlist__disable(rec->evlist);
			disabled = true;
		}
	}
	trigger_off(&auxtrace_snapshot_trigger);
	trigger_off(&switch_output_trigger);

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

		fprintf(stderr,	"[ perf record: Captured and wrote %.3f MB %s%s%s ]\n",
			perf_data__size(data) / 1024.0 / 1024.0,
			data->file.path, postfix, samples);
	}

out_delete_session:
	perf_session__delete(session);
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
	if (!strcmp(var, "record.call-graph"))
		var = "call-graph.record-mode"; /* fall-through */

	return perf_default_config(var, value, cb);
}

struct clockid_map {
	const char *name;
	int clockid;
};

#define CLOCKID_MAP(n, c)	\
	{ .name = n, .clockid = (c), }

#define CLOCKID_END	{ .name = NULL, }


/*
 * Add the missing ones, we need to build on many distros...
 */
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW 4
#endif
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME 7
#endif
#ifndef CLOCK_TAI
#define CLOCK_TAI 11
#endif

static const struct clockid_map clockids[] = {
	/* available for all events, NMI safe */
	CLOCKID_MAP("monotonic", CLOCK_MONOTONIC),
	CLOCKID_MAP("monotonic_raw", CLOCK_MONOTONIC_RAW),

	/* available for some events */
	CLOCKID_MAP("realtime", CLOCK_REALTIME),
	CLOCKID_MAP("boottime", CLOCK_BOOTTIME),
	CLOCKID_MAP("tai", CLOCK_TAI),

	/* available for the lazy */
	CLOCKID_MAP("mono", CLOCK_MONOTONIC),
	CLOCKID_MAP("raw", CLOCK_MONOTONIC_RAW),
	CLOCKID_MAP("real", CLOCK_REALTIME),
	CLOCKID_MAP("boot", CLOCK_BOOTTIME),

	CLOCKID_END,
};

static int parse_clockid(const struct option *opt, const char *str, int unset)
{
	struct record_opts *opts = (struct record_opts *)opt->value;
	const struct clockid_map *cm;
	const char *ostr = str;

	if (unset) {
		opts->use_clockid = 0;
		return 0;
	}

	/* no arg passed */
	if (!str)
		return 0;

	/* no setting it twice */
	if (opts->use_clockid)
		return -1;

	opts->use_clockid = true;

	/* if its a number, we're done */
	if (sscanf(str, "%d", &opts->clockid) == 1)
		return 0;

	/* allow a "CLOCK_" prefix to the name */
	if (!strncasecmp(str, "CLOCK_", 6))
		str += 6;

	for (cm = clockids; cm->name; cm++) {
		if (!strcasecmp(str, cm->name)) {
			opts->clockid = cm->clockid;
			return 0;
		}
	}

	opts->use_clockid = false;
	ui__warning("unknown clockid %s, check man page\n", ostr);
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

static void switch_output_size_warn(struct record *rec)
{
	u64 wakeup_size = perf_evlist__mmap_size(rec->opts.mmap_pages);
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

	if (!s->set)
		return 0;

	if (!strcmp(s->str, "signal")) {
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
		.proc_map_timeout     = 500,
	},
	.tool = {
		.sample		= process_sample_event,
		.fork		= perf_event__process_fork,
		.exit		= perf_event__process_exit,
		.comm		= perf_event__process_comm,
		.namespaces	= perf_event__process_namespaces,
		.mmap		= perf_event__process_mmap,
		.mmap2		= perf_event__process_mmap2,
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
	OPT_STRING('o', "output", &record.data.file.path, "file",
		    "output file name"),
	OPT_BOOLEAN_SET('i', "no-inherit", &record.opts.no_inherit,
			&record.opts.no_inherit_set,
			"child tasks do not inherit counters"),
	OPT_BOOLEAN(0, "tail-synthesize", &record.opts.tail_synthesize,
		    "synthesize non-sample events at the end of output"),
	OPT_BOOLEAN(0, "overwrite", &record.opts.overwrite, "use overwrite mode"),
	OPT_UINTEGER('F', "freq", &record.opts.user_freq, "profile at this frequency"),
	OPT_CALLBACK('m', "mmap-pages", &record.opts, "pages[,pages]",
		     "number of mmap data pages and AUX area tracing mmap pages",
		     record__parse_mmap_pages),
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
	OPT_UINTEGER('D', "delay", &record.opts.initial_delay,
		  "ms to wait before starting measurement after program start"),
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
		    " use -I ? to list register names", parse_regs),
	OPT_CALLBACK_OPTARG(0, "user-regs", &record.opts.sample_user_regs, NULL, "any register",
		    "sample selected machine registers on interrupt,"
		    " use -I ? to list register names", parse_regs),
	OPT_BOOLEAN(0, "running-time", &record.opts.running_time,
		    "Record running/enabled time of read (:S) events"),
	OPT_CALLBACK('k', "clockid", &record.opts,
	"clockid", "clockid to use for events, see clock_gettime()",
	parse_clockid),
	OPT_STRING_OPTARG('S', "snapshot", &record.opts.auxtrace_snapshot_opts,
			  "opts", "AUX area tracing Snapshot Mode", ""),
	OPT_UINTEGER(0, "proc-map-timeout", &record.opts.proc_map_timeout,
			"per thread proc mmap processing timeout in ms"),
	OPT_BOOLEAN(0, "namespaces", &record.opts.record_namespaces,
		    "Record namespaces events"),
	OPT_BOOLEAN(0, "switch-events", &record.opts.record_switch_events,
		    "Record context switch events"),
	OPT_BOOLEAN_FLAG(0, "all-kernel", &record.opts.all_kernel,
			 "Configure all used events to run in kernel space.",
			 PARSE_OPT_EXCLUSIVE),
	OPT_BOOLEAN_FLAG(0, "all-user", &record.opts.all_user,
			 "Configure all used events to run in user space.",
			 PARSE_OPT_EXCLUSIVE),
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
			  &record.switch_output.set, "signal,size,time",
			  "Switch output when receive SIGUSR2 or cross size,time threshold",
			  "signal"),
	OPT_BOOLEAN(0, "dry-run", &dry_run,
		    "Parse options then exit"),
	OPT_END()
};

struct option *record_options = __record_options;

int cmd_record(int argc, const char **argv)
{
	int err;
	struct record *rec = &record;
	char errbuf[BUFSIZ];

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

	rec->evlist = perf_evlist__new();
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
	if (rec->opts.record_switch_events &&
	    !perf_can_record_switch_events()) {
		ui__error("kernel does not support recording context switch events\n");
		parse_options_usage(record_usage, record_options, "switch-events", 0);
		return -EINVAL;
	}

	if (switch_output_setup(rec)) {
		parse_options_usage(record_usage, record_options, "switch-output", 0);
		return -EINVAL;
	}

	if (rec->switch_output.time) {
		signal(SIGALRM, alarm_sig_handler);
		alarm(rec->switch_output.time);
	}

	if (!rec->itr) {
		rec->itr = auxtrace_record__init(rec->evlist, &err);
		if (err)
			goto out;
	}

	err = auxtrace_parse_snapshot_options(rec->itr, &rec->opts,
					      rec->opts.auxtrace_snapshot_opts);
	if (err)
		goto out;

	/*
	 * Allow aliases to facilitate the lookup of symbols for address
	 * filters. Refer to auxtrace_parse_filters().
	 */
	symbol_conf.allow_aliases = true;

	symbol__init(NULL);

	err = auxtrace_parse_filters(rec->evlist);
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

	if (symbol_conf.kptr_restrict && !perf_evlist__exclude_kernel(rec->evlist))
		pr_warning(
"WARNING: Kernel address maps (/proc/{kallsyms,modules}) are restricted,\n"
"check /proc/sys/kernel/kptr_restrict.\n\n"
"Samples in kernel functions may not be resolved if a suitable vmlinux\n"
"file is not found in the buildid cache or in the vmlinux path.\n\n"
"Samples in kernel modules won't be resolved at all.\n\n"
"If some relocation was applied (e.g. kexec) symbols may be misresolved\n"
"even with a suitable vmlinux or kallsyms file.\n\n");

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

	if (rec->evlist->nr_entries == 0 &&
	    __perf_evlist__add_default(rec->evlist, !record.opts.no_samples) < 0) {
		pr_err("Not enough memory for event selector list\n");
		goto out;
	}

	if (rec->opts.target.tid && !rec->opts.no_inherit_set)
		rec->opts.no_inherit = true;

	err = target__validate(&rec->opts.target);
	if (err) {
		target__strerror(&rec->opts.target, err, errbuf, BUFSIZ);
		ui__warning("%s", errbuf);
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

	if (record_opts__config(&rec->opts)) {
		err = -EINVAL;
		goto out;
	}

	err = __cmd_record(&record, argc, argv);
out:
	perf_evlist__delete(rec->evlist);
	symbol__exit();
	auxtrace_record__free(rec->itr);
	return err;
}

static void snapshot_sig_handler(int sig __maybe_unused)
{
	struct record *rec = &record;

	if (trigger_is_ready(&auxtrace_snapshot_trigger)) {
		trigger_hit(&auxtrace_snapshot_trigger);
		auxtrace_record__snapshot_started = 1;
		if (auxtrace_record__snapshot_start(record.itr))
			trigger_error(&auxtrace_snapshot_trigger);
	}

	if (switch_output_signal(rec))
		trigger_hit(&switch_output_trigger);
}

static void alarm_sig_handler(int sig __maybe_unused)
{
	struct record *rec = &record;

	if (switch_output_time(rec))
		trigger_hit(&switch_output_trigger);
}
