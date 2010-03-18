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
#include "util/string.h"

#include "util/header.h"
#include "util/event.h"
#include "util/debug.h"
#include "util/session.h"
#include "util/symbol.h"
#include "util/cpumap.h"

#include <unistd.h>
#include <sched.h>

static int			*fd[MAX_NR_CPUS][MAX_COUNTERS];

static long			default_interval		=      0;

static int			nr_cpus				=      0;
static unsigned int		page_size;
static unsigned int		mmap_pages			=    128;
static int			freq				=   1000;
static int			output;
static const char		*output_name			= "perf.data";
static int			group				=      0;
static unsigned int		realtime_prio			=      0;
static int			raw_samples			=      0;
static int			system_wide			=      0;
static int			profile_cpu			=     -1;
static pid_t			target_pid			=     -1;
static pid_t			target_tid			=     -1;
static pid_t			*all_tids			=      NULL;
static int			thread_num			=      0;
static pid_t			child_pid			=     -1;
static int			inherit				=      1;
static int			force				=      0;
static int			append_file			=      0;
static int			call_graph			=      0;
static int			inherit_stat			=      0;
static int			no_samples			=      0;
static int			sample_address			=      0;
static int			multiplex			=      0;
static int			multiplex_fd			=     -1;

static long			samples				=      0;
static struct timeval		last_read;
static struct timeval		this_read;

static u64			bytes_written			=      0;

static struct pollfd		*event_array;

static int			nr_poll				=      0;
static int			nr_cpu				=      0;

static int			file_new			=      1;
static off_t			post_processing_offset;

static struct perf_session	*session;

struct mmap_data {
	int			counter;
	void			*base;
	unsigned int		mask;
	unsigned int		prev;
};

static struct mmap_data		*mmap_array[MAX_NR_CPUS][MAX_COUNTERS];

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

	gettimeofday(&this_read, NULL);

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
		struct timeval iv;
		unsigned long msecs;

		timersub(&this_read, &last_read, &iv);
		msecs = iv.tv_sec*1000 + iv.tv_usec/1000;

		fprintf(stderr, "WARNING: failed to keep up with mmap data."
				"  Last read %lu msecs ago.\n", msecs);

		/*
		 * head points to a known good entry, start there.
		 */
		old = head;
	}

	last_read = this_read;

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
	if (child_pid != -1)
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

	if (freq) {
		attr->sample_type	|= PERF_SAMPLE_PERIOD;
		attr->freq		= 1;
		attr->sample_freq	= freq;
	}

	if (no_samples)
		attr->sample_freq = 0;

	if (inherit_stat)
		attr->inherit_stat = 1;

	if (sample_address)
		attr->sample_type	|= PERF_SAMPLE_ADDR;

	if (call_graph)
		attr->sample_type	|= PERF_SAMPLE_CALLCHAIN;

	if (raw_samples) {
		attr->sample_type	|= PERF_SAMPLE_TIME;
		attr->sample_type	|= PERF_SAMPLE_RAW;
		attr->sample_type	|= PERF_SAMPLE_CPU;
	}

	attr->mmap		= track;
	attr->comm		= track;
	attr->inherit		= inherit;
	if (target_pid == -1 && !system_wide) {
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
			else if (err ==  ENODEV && profile_cpu != -1) {
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
			perror("Unable to read perf file descriptor\n");
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
		if (multiplex && multiplex_fd == -1)
			multiplex_fd = fd[nr_cpu][counter][thread_index];

		if (multiplex && fd[nr_cpu][counter][thread_index] != multiplex_fd) {

			ret = ioctl(fd[nr_cpu][counter][thread_index], PERF_EVENT_IOC_SET_OUTPUT, multiplex_fd);
			assert(ret != -1);
		} else {
			event_array[nr_poll].fd = fd[nr_cpu][counter][thread_index];
			event_array[nr_poll].events = POLLIN;
			nr_poll++;

			mmap_array[nr_cpu][counter][thread_index].counter = counter;
			mmap_array[nr_cpu][counter][thread_index].prev = 0;
			mmap_array[nr_cpu][counter][thread_index].mask = mmap_pages*page_size - 1;
			mmap_array[nr_cpu][counter][thread_index].base = mmap(NULL, (mmap_pages+1)*page_size,
				PROT_READ|PROT_WRITE, MAP_SHARED, fd[nr_cpu][counter][thread_index], 0);
			if (mmap_array[nr_cpu][counter][thread_index].base == MAP_FAILED) {
				error("failed to mmap with %d (%s)\n", errno, strerror(errno));
				exit(-1);
			}
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
	session->header.data_size += bytes_written;

	process_buildids();
	perf_header__write(&session->header, output, true);
}

static int __cmd_record(int argc, const char **argv)
{
	int i, counter;
	struct stat st;
	pid_t pid = 0;
	int flags;
	int err;
	unsigned long waking = 0;
	int child_ready_pipe[2], go_pipe[2];
	const bool forks = argc > 0;
	char buf;

	page_size = sysconf(_SC_PAGE_SIZE);

	atexit(sig_atexit);
	signal(SIGCHLD, sig_handler);
	signal(SIGINT, sig_handler);

	if (forks && (pipe(child_ready_pipe) < 0 || pipe(go_pipe) < 0)) {
		perror("failed to create pipes");
		exit(-1);
	}

	if (!stat(output_name, &st) && st.st_size) {
		if (!force) {
			if (!append_file) {
				pr_err("Error, output file %s exists, use -A "
				       "to append or -f to overwrite.\n",
				       output_name);
				exit(-1);
			}
		} else {
			char oldname[PATH_MAX];
			snprintf(oldname, sizeof(oldname), "%s.old",
				 output_name);
			unlink(oldname);
			rename(output_name, oldname);
		}
	} else {
		append_file = 0;
	}

	flags = O_CREAT|O_RDWR;
	if (append_file)
		file_new = 0;
	else
		flags |= O_TRUNC;

	output = open(output_name, flags, S_IRUSR|S_IWUSR);
	if (output < 0) {
		perror("failed to create output file");
		exit(-1);
	}

	session = perf_session__new(output_name, O_WRONLY, force);
	if (session == NULL) {
		pr_err("Not enough memory for reading perf file header\n");
		return -1;
	}

	if (!file_new) {
		err = perf_header__read(&session->header, output);
		if (err < 0)
			return err;
	}

	if (raw_samples) {
		perf_header__set_feat(&session->header, HEADER_TRACE_INFO);
	} else {
		for (i = 0; i < nr_counters; i++) {
			if (attrs[i].sample_type & PERF_SAMPLE_RAW) {
				perf_header__set_feat(&session->header, HEADER_TRACE_INFO);
				break;
			}
		}
	}

	atexit(atexit_header);

	if (forks) {
		child_pid = fork();
		if (pid < 0) {
			perror("failed to fork");
			exit(-1);
		}

		if (!child_pid) {
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

	if ((!system_wide && !inherit) || profile_cpu != -1) {
		open_counters(profile_cpu);
	} else {
		nr_cpus = read_cpu_map();
		for (i = 0; i < nr_cpus; i++)
			open_counters(cpumap[i]);
	}

	if (file_new) {
		err = perf_header__write(&session->header, output, false);
		if (err < 0)
			return err;
	}

	post_processing_offset = lseek(output, 0, SEEK_CUR);

	err = event__synthesize_kernel_mmap(process_synthesized_event,
					    session, "_text");
	if (err < 0) {
		pr_err("Couldn't record kernel reference relocation symbol.\n");
		return err;
	}

	err = event__synthesize_modules(process_synthesized_event, session);
	if (err < 0) {
		pr_err("Couldn't record kernel reference relocation symbol.\n");
		return err;
	}

	if (!system_wide && profile_cpu == -1)
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

		for (i = 0; i < nr_cpu; i++) {
			for (counter = 0; counter < nr_counters; counter++) {
				for (thread = 0;
					thread < thread_num; thread++) {
					if (mmap_array[i][counter][thread].base)
						mmap_read(&mmap_array[i][counter][thread]);
				}

			}
		}

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
}

static const char * const record_usage[] = {
	"perf record [<options>] [<command>]",
	"perf record [<options>] -- <command> [<options>]",
	NULL
};

static const struct option options[] = {
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
	OPT_INTEGER('C', "profile_cpu", &profile_cpu,
			    "CPU to profile on"),
	OPT_BOOLEAN('f', "force", &force,
			"overwrite existing data file"),
	OPT_LONG('c', "count", &default_interval,
		    "event period to sample"),
	OPT_STRING('o', "output", &output_name, "file",
		    "output file name"),
	OPT_BOOLEAN('i', "inherit", &inherit,
		    "child tasks inherit counters"),
	OPT_INTEGER('F', "freq", &freq,
		    "profile at this frequency"),
	OPT_INTEGER('m', "mmap-pages", &mmap_pages,
		    "number of mmap data pages"),
	OPT_BOOLEAN('g', "call-graph", &call_graph,
		    "do call-graph (stack chain/backtrace) recording"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_BOOLEAN('s', "stat", &inherit_stat,
		    "per thread counts"),
	OPT_BOOLEAN('d', "data", &sample_address,
		    "Sample addresses"),
	OPT_BOOLEAN('n', "no-samples", &no_samples,
		    "don't sample"),
	OPT_BOOLEAN('M', "multiplex", &multiplex,
		    "multiplex counter output in a single channel"),
	OPT_END()
};

int cmd_record(int argc, const char **argv, const char *prefix __used)
{
	int counter;
	int i,j;

	argc = parse_options(argc, argv, options, record_usage,
			    PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc && target_pid == -1 && target_tid == -1 &&
		!system_wide && profile_cpu == -1)
		usage_with_options(record_usage, options);

	symbol__init();

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
			usage_with_options(record_usage, options);
		}
	} else {
		all_tids=malloc(sizeof(pid_t));
		if (!all_tids)
			return -ENOMEM;

		all_tids[0] = target_tid;
		thread_num = 1;
	}

	for (i = 0; i < MAX_NR_CPUS; i++) {
		for (j = 0; j < MAX_COUNTERS; j++) {
			fd[i][j] = malloc(sizeof(int)*thread_num);
			mmap_array[i][j] = malloc(
				sizeof(struct mmap_data)*thread_num);
			if (!fd[i][j] || !mmap_array[i][j])
				return -ENOMEM;
		}
	}
	event_array = malloc(
		sizeof(struct pollfd)*MAX_NR_CPUS*MAX_COUNTERS*thread_num);
	if (!event_array)
		return -ENOMEM;

	/*
	 * User specified count overrides default frequency.
	 */
	if (default_interval)
		freq = 0;
	else if (freq) {
		default_interval = freq;
	} else {
		fprintf(stderr, "frequency and count are zero, aborting\n");
		exit(EXIT_FAILURE);
	}

	for (counter = 0; counter < nr_counters; counter++) {
		if (attrs[counter].sample_period)
			continue;

		attrs[counter].sample_period = default_interval;
	}

	return __cmd_record(argc, argv);
}
