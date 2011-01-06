/*
 * builtin-record.c
 *
 * Builtin record command: Record the profile of a workload
 * (or a CPU, or a PID) into the perf.data output file - for
 * later analysis via perf report.
 */
#define _FILE_OFFSET_BITS 64

#include "builtin.h"

#include "perf.h"

#include "util/build-id.h"
#include "util/util.h"
#include "util/parse-options.h"
#include "util/parse-events.h"

#include "util/header.h"
#include "util/event.h"
#include "util/debug.h"
#include "util/session.h"
#include "util/symbol.h"
#include "util/cpumap.h"

#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>

enum write_mode_t {
	WRITE_FORCE,
	WRITE_APPEND
};

static int			*fd[MAX_NR_CPUS][MAX_COUNTERS];

static u64			user_interval			= ULLONG_MAX;
static u64			default_interval		=      0;

static int			nr_cpus				=      0;
static unsigned int		page_size;
static unsigned int		mmap_pages			=    128;
static unsigned int		user_freq 			= UINT_MAX;
static int			freq				=   1000;
static int			output;
static int			pipe_output			=      0;
static const char		*output_name			= "perf.data";
static int			group				=      0;
static int			realtime_prio			=      0;
static bool			raw_samples			=  false;
static bool			system_wide			=  false;
static pid_t			target_pid			=     -1;
static pid_t			target_tid			=     -1;
static pid_t			*all_tids			=      NULL;
static int			thread_num			=      0;
static pid_t			child_pid			=     -1;
static bool			no_inherit			=  false;
static enum write_mode_t	write_mode			= WRITE_FORCE;
static bool			call_graph			=  false;
static bool			inherit_stat			=  false;
static bool			no_samples			=  false;
static bool			sample_address			=  false;
static bool			no_buildid			=  false;

static long			samples				=      0;
static u64			bytes_written			=      0;

static struct pollfd		*event_array;

static int			nr_poll				=      0;
static int			nr_cpu				=      0;

static int			file_new			=      1;
static off_t			post_processing_offset;

static struct perf_session	*session;
static const char		*cpu_list;

struct mmap_data {
	int			counter;
	void			*base;
	unsigned int		mask;
	unsigned int		prev;
};

static struct mmap_data		mmap_array[MAX_NR_CPUS];

static unsigned long mmap_read_head(struct mmap_data *md)
{
	struct perf_event_mmap_page *pc = md->base;
	long head;

	head = pc->data_head;
	rmb();

	return head;
}

static void mmap_write_tail(struct mmap_data *md, unsigned long tail)
{
	struct perf_event_mmap_page *pc = md->base;

	/*
	 * ensure all reads are done before we write the tail out.
	 */
	/* mb(); */
	pc->data_tail = tail;
}

static void advance_output(size_t size)
{
	bytes_written += size;
}

static void write_output(void *buf, size_t size)
{
	while (size) {
		int ret = write(output, buf, size);

		if (ret < 0)
			die("failed to write");

		size -= ret;
		buf += ret;

		bytes_written += ret;
	}
}

static int process_synthesized_event(event_t *event,
				     struct perf_session *self __used)
{
	write_output(event, event->header.size);
	return 0;
}

static void mmap_read(struct mmap_data *md)
{
	unsigned int head = mmap_read_head(md);
	unsigned int old = md->prev;
	unsigned char *data = md->base + page_size;
	unsigned long size;
	void *buf;
	int diff;

	/*
	 * If we're further behind than half the buffer, there's a chance
	 * the writer will bite our tail and mess up the samples under us.
	 *
	 * If we somehow ended up ahead of the head, we got messed up.
	 *
	 * In either case, truncate and restart at head.
	 */
	diff = head - old;
	if (diff < 0) {
		fprintf(stderr, "WARNING: failed to keep up with mmap data\n");
		/*
		 * head points to a known good entry, start there.
		 */
		old = head;
	}

	if (old != head)
		samples++;

	size = head - old;

	if ((old & md->mask) + size != (head & md->mask)) {
		buf = &data[old & md->mask];
		size = md->mask + 1 - (old & md->mask);
		old += size;

		write_output(buf, size);
	}

	buf = &data[old & md->mask];
	size = head - old;
	old += size;

	write_output(buf, size);

	md->prev = old;
	mmap_write_tail(md, old);
}

static volatile int done = 0;
static volatile int signr = -1;

static void sig_handler(int sig)
{
	done = 1;
	signr = sig;
}

static void sig_atexit(void)
{
	if (child_pid > 0)
		kill(child_pid, SIGTERM);

	if (signr == -1)
		return;

	signal(signr, SIG_DFL);
	kill(getpid(), signr);
}

static int group_fd;

static struct perf_header_attr *get_header_attr(struct perf_event_attr *a, int nr)
{
	struct perf_header_attr *h_attr;

	if (nr < session->header.attrs) {
		h_attr = session->header.attr[nr];
	} else {
		h_attr = perf_header_attr__new(a);
		if (h_attr != NULL)
			if (perf_header__add_attr(&session->header, h_attr) < 0) {
				perf_header_attr__delete(h_attr);
				h_attr = NULL;
			}
	}

	return h_attr;
}

static void create_counter(int counter, int cpu)
{
	char *filter = filters[counter];
	struct perf_event_attr *attr = attrs + counter;
	struct perf_header_attr *h_attr;
	int track = !counter; /* only the first counter needs these */
	int thread_index;
	int ret;
	struct {
		u64 count;
		u64 time_enabled;
		u64 time_running;
		u64 id;
	} read_data;

	attr->read_format	= PERF_FORMAT_TOTAL_TIME_ENABLED |
				  PERF_FORMAT_TOTAL_TIME_RUNNING |
				  PERF_FORMAT_ID;

	attr->sample_type	|= PERF_SAMPLE_IP | PERF_SAMPLE_TID;

	if (nr_counters > 1)
		attr->sample_type |= PERF_SAMPLE_ID;

	/*
	 * We default some events to a 1 default interval. But keep
	 * it a weak assumption overridable by the user.
	 */
	if (!attr->sample_period || (user_freq != UINT_MAX &&
				     user_interval != ULLONG_MAX)) {
		if (freq) {
			attr->sample_type	|= PERF_SAMPLE_PERIOD;
			attr->freq		= 1;
			attr->sample_freq	= freq;
		} else {
			attr->sample_period = default_interval;
		}
	}

	if (no_samples)
		attr->sample_freq = 0;

	if (inherit_stat)
		attr->inherit_stat = 1;

	if (sample_address) {
		attr->sample_type	|= PERF_SAMPLE_ADDR;
		attr->mmap_data = track;
	}

	if (call_graph)
		attr->sample_type	|= PERF_SAMPLE_CALLCHAIN;

	if (system_wide)
		attr->sample_type	|= PERF_SAMPLE_CPU;

	if (raw_samples) {
		attr->sample_type	|= PERF_SAMPLE_TIME;
		attr->sample_type	|= PERF_SAMPLE_RAW;
		attr->sample_type	|= PERF_SAMPLE_CPU;
	}

	attr->mmap		= track;
	attr->comm		= track;
	attr->inherit		= !no_inherit;
	if (target_pid == -1 && target_tid == -1 && !system_wide) {
		attr->disabled = 1;
		attr->enable_on_exec = 1;
	}

	for (thread_index = 0; thread_index < thread_num; thread_index++) {
try_again:
		fd[nr_cpu][counter][thread_index] = sys_perf_event_open(attr,
				all_tids[thread_index], cpu, group_fd, 0);

		if (fd[nr_cpu][counter][thread_index] < 0) {
			int err = errno;

			if (err == EPERM || err == EACCES)
				die("Permission error - are you root?\n"
					"\t Consider tweaking"
					" /proc/sys/kernel/perf_event_paranoid.\n");
			else if (err ==  ENODEV && cpu_list) {
				die("No such device - did you specify"
					" an out-of-range profile CPU?\n");
			}

			/*
			 * If it's cycles then fall back to hrtimer
			 * based cpu-clock-tick sw counter, which
			 * is always available even if no PMU support:
			 */
			if (attr->type == PERF_TYPE_HARDWARE
					&& attr->config == PERF_COUNT_HW_CPU_CYCLES) {

				if (verbose)
					warning(" ... trying to fall back to cpu-clock-ticks\n");
				attr->type = PERF_TYPE_SOFTWARE;
				attr->config = PERF_COUNT_SW_CPU_CLOCK;
				goto try_again;
			}
			printf("\n");
			error("perfcounter syscall returned with %d (%s)\n",
					fd[nr_cpu][counter][thread_index], strerror(err));

#if defined(__i386__) || defined(__x86_64__)
			if (attr->type == PERF_TYPE_HARDWARE && err == EOPNOTSUPP)
				die("No hardware sampling interrupt available."
				    " No APIC? If so then you can boot the kernel"
				    " with the \"lapic\" boot parameter to"
				    " force-enable it.\n");
#endif

			die("No CONFIG_PERF_EVENTS=y kernel support configured?\n");
			exit(-1);
		}

		h_attr = get_header_attr(attr, counter);
		if (h_attr == NULL)
			die("nomem\n");

		if (!file_new) {
			if (memcmp(&h_attr->attr, attr, sizeof(*attr))) {
				fprintf(stderr, "incompatible append\n");
				exit(-1);
			}
		}

		if (read(fd[nr_cpu][counter][thread_index], &read_data, sizeof(read_data)) == -1) {
			perror("Unable to read perf file descriptor");
			exit(-1);
		}

		if (perf_header_attr__add_id(h_attr, read_data.id) < 0) {
			pr_warning("Not enough memory to add id\n");
			exit(-1);
		}

		assert(fd[nr_cpu][counter][thread_index] >= 0);
		fcntl(fd[nr_cpu][counter][thread_index], F_SETFL, O_NONBLOCK);

		/*
		 * First counter acts as the group leader:
		 */
		if (group && group_fd == -1)
			group_fd = fd[nr_cpu][counter][thread_index];

		if (counter || thread_index) {
			ret = ioctl(fd[nr_cpu][counter][thread_index],
					PERF_EVENT_IOC_SET_OUTPUT,
					fd[nr_cpu][0][0]);
			if (ret) {
				error("failed to set output: %d (%s)\n", errno,
						strerror(errno));
				exit(-1);
			}
		} else {
			mmap_array[nr_cpu].counter = counter;
			mmap_array[nr_cpu].prev = 0;
			mmap_array[nr_cpu].mask = mmap_pages*page_size - 1;
			mmap_array[nr_cpu].base = mmap(NULL, (mmap_pages+1)*page_size,
				PROT_READ|PROT_WRITE, MAP_SHARED, fd[nr_cpu][counter][thread_index], 0);
			if (mmap_array[nr_cpu].base == MAP_FAILED) {
				error("failed to mmap with %d (%s)\n", errno, strerror(errno));
				exit(-1);
			}

			event_array[nr_poll].fd = fd[nr_cpu][counter][thread_index];
			event_array[nr_poll].events = POLLIN;
			nr_poll++;
		}

		if (filter != NULL) {
			ret = ioctl(fd[nr_cpu][counter][thread_index],
					PERF_EVENT_IOC_SET_FILTER, filter);
			if (ret) {
				error("failed to set filter with %d (%s)\n", errno,
						strerror(errno));
				exit(-1);
			}
		}
	}
}

static void open_counters(int cpu)
{
	int counter;

	group_fd = -1;
	for (counter = 0; counter < nr_counters; counter++)
		create_counter(counter, cpu);

	nr_cpu++;
}

static int process_buildids(void)
{
	u64 size = lseek(output, 0, SEEK_CUR);

	if (size == 0)
		return 0;

	session->fd = output;
	return __perf_session__process_events(session, post_processing_offset,
					      size - post_processing_offset,
					      size, &build_id__mark_dso_hit_ops);
}

static void atexit_header(void)
{
	if (!pipe_output) {
		session->header.data_size += bytes_written;

		process_buildids();
		perf_header__write(&session->header, output, true);
		perf_session__delete(session);
		symbol__exit();
	}
}

static void event__synthesize_guest_os(struct machine *machine, void *data)
{
	int err;
	struct perf_session *psession = data;

	if (machine__is_host(machine))
		return;

	/*
	 *As for guest kernel when processing subcommand record&report,
	 *we arrange module mmap prior to guest kernel mmap and trigger
	 *a preload dso because default guest module symbols are loaded
	 *from guest kallsyms instead of /lib/modules/XXX/XXX. This
	 *method is used to avoid symbol missing when the first addr is
	 *in module instead of in guest kernel.
	 */
	err = event__synthesize_modules(process_synthesized_event,
					psession, machine);
	if (err < 0)
		pr_err("Couldn't record guest kernel [%d]'s reference"
		       " relocation symbol.\n", machine->pid);

	/*
	 * We use _stext for guest kernel because guest kernel's /proc/kallsyms
	 * have no _text sometimes.
	 */
	err = event__synthesize_kernel_mmap(process_synthesized_event,
					    psession, machine, "_text");
	if (err < 0)
		err = event__synthesize_kernel_mmap(process_synthesized_event,
						    psession, machine, "_stext");
	if (err < 0)
		pr_err("Couldn't record guest kernel [%d]'s reference"
		       " relocation symbol.\n", machine->pid);
}

static struct perf_event_header finished_round_event = {
	.size = sizeof(struct perf_event_header),
	.type = PERF_RECORD_FINISHED_ROUND,
};

static void mmap_read_all(void)
{
	int i;

	for (i = 0; i < nr_cpu; i++) {
		if (mmap_array[i].base)
			mmap_read(&mmap_array[i]);
	}

	if (perf_header__has_feat(&session->header, HEADER_TRACE_INFO))
		write_output(&finished_round_event, sizeof(finished_round_event));
}

static int __cmd_record(int argc, const char **argv)
{
	int i, counter;
	struct stat st;
	int flags;
	int err;
	unsigned long waking = 0;
	int child_ready_pipe[2], go_pipe[2];
	const bool forks = argc > 0;
	char buf;
	struct machine *machine;

	page_size = sysconf(_SC_PAGE_SIZE);

	atexit(sig_atexit);
	signal(SIGCHLD, sig_handler);
	signal(SIGINT, sig_handler);

	if (forks && (pipe(child_ready_pipe) < 0 || pipe(go_pipe) < 0)) {
		perror("failed to create pipes");
		exit(-1);
	}

	if (!strcmp(output_name, "-"))
		pipe_output = 1;
	else if (!stat(output_name, &st) && st.st_size) {
		if (write_mode == WRITE_FORCE) {
			char oldname[PATH_MAX];
			snprintf(oldname, sizeof(oldname), "%s.old",
				 output_name);
			unlink(oldname);
			rename(output_name, oldname);
		}
	} else if (write_mode == WRITE_APPEND) {
		write_mode = WRITE_FORCE;
	}

	flags = O_CREAT|O_RDWR;
	if (write_mode == WRITE_APPEND)
		file_new = 0;
	else
		flags |= O_TRUNC;

	if (pipe_output)
		output = STDOUT_FILENO;
	else
		output = open(output_name, flags, S_IRUSR | S_IWUSR);
	if (output < 0) {
		perror("failed to create output file");
		exit(-1);
	}

	session = perf_session__new(output_name, O_WRONLY,
				    write_mode == WRITE_FORCE, false);
	if (session == NULL) {
		pr_err("Not enough memory for reading perf file header\n");
		return -1;
	}

	if (!file_new) {
		err = perf_header__read(session, output);
		if (err < 0)
			goto out_delete_session;
	}

	if (have_tracepoints(attrs, nr_counters))
		perf_header__set_feat(&session->header, HEADER_TRACE_INFO);

	/*
 	 * perf_session__delete(session) will be called at atexit_header()
	 */
	atexit(atexit_header);

	if (forks) {
		child_pid = fork();
		if (child_pid < 0) {
			perror("failed to fork");
			exit(-1);
		}

		if (!child_pid) {
			if (pipe_output)
				dup2(2, 1);
			close(child_ready_pipe[0]);
			close(go_pipe[1]);
			fcntl(go_pipe[0], F_SETFD, FD_CLOEXEC);

			/*
			 * Do a dummy execvp to get the PLT entry resolved,
			 * so we avoid the resolver overhead on the real
			 * execvp call.
			 */
			execvp("", (char **)argv);

			/*
			 * Tell the parent we're ready to go
			 */
			close(child_ready_pipe[1]);

			/*
			 * Wait until the parent tells us to go.
			 */
			if (read(go_pipe[0], &buf, 1) == -1)
				perror("unable to read pipe");

			execvp(argv[0], (char **)argv);

			perror(argv[0]);
			exit(-1);
		}

		if (!system_wide && target_tid == -1 && target_pid == -1)
			all_tids[0] = child_pid;

		close(child_ready_pipe[1]);
		close(go_pipe[0]);
		/*
		 * wait for child to settle
		 */
		if (read(child_ready_pipe[0], &buf, 1) == -1) {
			perror("unable to read pipe");
			exit(-1);
		}
		close(child_ready_pipe[0]);
	}

	nr_cpus = read_cpu_map(cpu_list);
	if (nr_cpus < 1) {
		perror("failed to collect number of CPUs");
		return -1;
	}

	if (!system_wide && no_inherit && !cpu_list) {
		open_counters(-1);
	} else {
		for (i = 0; i < nr_cpus; i++)
			open_counters(cpumap[i]);
	}

	if (pipe_output) {
		err = perf_header__write_pipe(output);
		if (err < 0)
			return err;
	} else if (file_new) {
		err = perf_header__write(&session->header, output, false);
		if (err < 0)
			return err;
	}

	post_processing_offset = lseek(output, 0, SEEK_CUR);

	if (pipe_output) {
		err = event__synthesize_attrs(&session->header,
					      process_synthesized_event,
					      session);
		if (err < 0) {
			pr_err("Couldn't synthesize attrs.\n");
			return err;
		}

		err = event__synthesize_event_types(process_synthesized_event,
						    session);
		if (err < 0) {
			pr_err("Couldn't synthesize event_types.\n");
			return err;
		}

		if (have_tracepoints(attrs, nr_counters)) {
			/*
			 * FIXME err <= 0 here actually means that
			 * there were no tracepoints so its not really
			 * an error, just that we don't need to
			 * synthesize anything.  We really have to
			 * return this more properly and also
			 * propagate errors that now are calling die()
			 */
			err = event__synthesize_tracing_data(output, attrs,
							     nr_counters,
							     process_synthesized_event,
							     session);
			if (err <= 0) {
				pr_err("Couldn't record tracing data.\n");
				return err;
			}
			advance_output(err);
		}
	}

	machine = perf_session__find_host_machine(session);
	if (!machine) {
		pr_err("Couldn't find native kernel information.\n");
		return -1;
	}

	err = event__synthesize_kernel_mmap(process_synthesized_event,
					    session, machine, "_text");
	if (err < 0)
		err = event__synthesize_kernel_mmap(process_synthesized_event,
						    session, machine, "_stext");
	if (err < 0)
		pr_err("Couldn't record kernel reference relocation symbol\n"
		       "Symbol resolution may be skewed if relocation was used (e.g. kexec).\n"
		       "Check /proc/kallsyms permission or run as root.\n");

	err = event__synthesize_modules(process_synthesized_event,
					session, machine);
	if (err < 0)
		pr_err("Couldn't record kernel module information.\n"
		       "Symbol resolution may be skewed if relocation was used (e.g. kexec).\n"
		       "Check /proc/modules permission or run as root.\n");

	if (perf_guest)
		perf_session__process_machines(session, event__synthesize_guest_os);

	if (!system_wide)
		event__synthesize_thread(target_tid, process_synthesized_event,
					 session);
	else
		event__synthesize_threads(process_synthesized_event, session);

	if (realtime_prio) {
		struct sched_param param;

		param.sched_priority = realtime_prio;
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			pr_err("Could not set realtime priority.\n");
			exit(-1);
		}
	}

	/*
	 * Let the child rip
	 */
	if (forks)
		close(go_pipe[1]);

	for (;;) {
		int hits = samples;
		int thread;

		mmap_read_all();

		if (hits == samples) {
			if (done)
				break;
			err = poll(event_array, nr_poll, -1);
			waking++;
		}

		if (done) {
			for (i = 0; i < nr_cpu; i++) {
				for (counter = 0;
					counter < nr_counters;
					counter++) {
					for (thread = 0;
						thread < thread_num;
						thread++)
						ioctl(fd[i][counter][thread],
							PERF_EVENT_IOC_DISABLE);
				}
			}
		}
	}

	if (quiet)
		return 0;

	fprintf(stderr, "[ perf record: Woken up %ld times to write data ]\n", waking);

	/*
	 * Approximate RIP event size: 24 bytes.
	 */
	fprintf(stderr,
		"[ perf record: Captured and wrote %.3f MB %s (~%lld samples) ]\n",
		(double)bytes_written / 1024.0 / 1024.0,
		output_name,
		bytes_written / 24);

	return 0;

out_delete_session:
	perf_session__delete(session);
	return err;
}

static const char * const record_usage[] = {
	"perf record [<options>] [<command>]",
	"perf record [<options>] -- <command> [<options>]",
	NULL
};

static bool force, append_file;

const struct option record_options[] = {
	OPT_CALLBACK('e', "event", NULL, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events),
	OPT_CALLBACK(0, "filter", NULL, "filter",
		     "event filter", parse_filter),
	OPT_INTEGER('p', "pid", &target_pid,
		    "record events on existing process id"),
	OPT_INTEGER('t', "tid", &target_tid,
		    "record events on existing thread id"),
	OPT_INTEGER('r', "realtime", &realtime_prio,
		    "collect data with this RT SCHED_FIFO priority"),
	OPT_BOOLEAN('R', "raw-samples", &raw_samples,
		    "collect raw sample records from all opened counters"),
	OPT_BOOLEAN('a', "all-cpus", &system_wide,
			    "system-wide collection from all CPUs"),
	OPT_BOOLEAN('A', "append", &append_file,
			    "append to the output file to do incremental profiling"),
	OPT_STRING('C', "cpu", &cpu_list, "cpu",
		    "list of cpus to monitor"),
	OPT_BOOLEAN('f', "force", &force,
			"overwrite existing data file (deprecated)"),
	OPT_U64('c', "count", &user_interval, "event period to sample"),
	OPT_STRING('o', "output", &output_name, "file",
		    "output file name"),
	OPT_BOOLEAN('i', "no-inherit", &no_inherit,
		    "child tasks do not inherit counters"),
	OPT_UINTEGER('F', "freq", &user_freq, "profile at this frequency"),
	OPT_UINTEGER('m', "mmap-pages", &mmap_pages, "number of mmap data pages"),
	OPT_BOOLEAN('g', "call-graph", &call_graph,
		    "do call-graph (stack chain/backtrace) recording"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_BOOLEAN('q', "quiet", &quiet, "don't print any message"),
	OPT_BOOLEAN('s', "stat", &inherit_stat,
		    "per thread counts"),
	OPT_BOOLEAN('d', "data", &sample_address,
		    "Sample addresses"),
	OPT_BOOLEAN('n', "no-samples", &no_samples,
		    "don't sample"),
	OPT_BOOLEAN('N', "no-buildid-cache", &no_buildid,
		    "do not update the buildid cache"),
	OPT_END()
};

int cmd_record(int argc, const char **argv, const char *prefix __used)
{
	int i, j, err = -ENOMEM;

	argc = parse_options(argc, argv, record_options, record_usage,
			    PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc && target_pid == -1 && target_tid == -1 &&
		!system_wide && !cpu_list)
		usage_with_options(record_usage, record_options);

	if (force && append_file) {
		fprintf(stderr, "Can't overwrite and append at the same time."
				" You need to choose between -f and -A");
		usage_with_options(record_usage, record_options);
	} else if (append_file) {
		write_mode = WRITE_APPEND;
	} else {
		write_mode = WRITE_FORCE;
	}

	symbol__init();
	if (no_buildid)
		disable_buildid_cache();

	if (!nr_counters) {
		nr_counters	= 1;
		attrs[0].type	= PERF_TYPE_HARDWARE;
		attrs[0].config = PERF_COUNT_HW_CPU_CYCLES;
	}

	if (target_pid != -1) {
		target_tid = target_pid;
		thread_num = find_all_tid(target_pid, &all_tids);
		if (thread_num <= 0) {
			fprintf(stderr, "Can't find all threads of pid %d\n",
					target_pid);
			usage_with_options(record_usage, record_options);
		}
	} else {
		all_tids=malloc(sizeof(pid_t));
		if (!all_tids)
			goto out_symbol_exit;

		all_tids[0] = target_tid;
		thread_num = 1;
	}

	for (i = 0; i < MAX_NR_CPUS; i++) {
		for (j = 0; j < MAX_COUNTERS; j++) {
			fd[i][j] = malloc(sizeof(int)*thread_num);
			if (!fd[i][j])
				goto out_free_fd;
		}
	}
	event_array = malloc(
		sizeof(struct pollfd)*MAX_NR_CPUS*MAX_COUNTERS*thread_num);
	if (!event_array)
		goto out_free_fd;

	if (user_interval != ULLONG_MAX)
		default_interval = user_interval;
	if (user_freq != UINT_MAX)
		freq = user_freq;

	/*
	 * User specified count overrides default frequency.
	 */
	if (default_interval)
		freq = 0;
	else if (freq) {
		default_interval = freq;
	} else {
		fprintf(stderr, "frequency and count are zero, aborting\n");
		err = -EINVAL;
		goto out_free_event_array;
	}

	err = __cmd_record(argc, argv);

out_free_event_array:
	free(event_array);
out_free_fd:
	for (i = 0; i < MAX_NR_CPUS; i++) {
		for (j = 0; j < MAX_COUNTERS; j++)
			free(fd[i][j]);
	}
	free(all_tids);
	all_tids = NULL;
out_symbol_exit:
	symbol__exit();
	return err;
}
