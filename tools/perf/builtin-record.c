/*
 * builtin-record.c
 *
 * Builtin record command: Record the profile of a workload
 * (or a CPU, or a PID) into the perf.data output file - for
 * later analysis via perf report.
 */
#include "builtin.h"

#include "perf.h"

#include "util/util.h"
#include "util/parse-options.h"
#include "util/parse-events.h"
#include "util/string.h"

#include <unistd.h>
#include <sched.h>

#define ALIGN(x, a)		__ALIGN_MASK(x, (typeof(x))(a)-1)
#define __ALIGN_MASK(x, mask)	(((x)+(mask))&~(mask))

static int			fd[MAX_NR_CPUS][MAX_COUNTERS];

static long			default_interval		= 100000;

static int			nr_cpus				= 0;
static unsigned int		page_size;
static unsigned int		mmap_pages			= 128;
static int			freq				= 0;
static int			output;
static const char		*output_name			= "perf.data";
static int			group				= 0;
static unsigned int		realtime_prio			= 0;
static int			system_wide			= 0;
static pid_t			target_pid			= -1;
static int			inherit				= 1;
static int			force				= 0;
static int			append_file			= 0;
static int			verbose				= 0;

static long			samples;
static struct timeval		last_read;
static struct timeval		this_read;

static __u64			bytes_written;

static struct pollfd		event_array[MAX_NR_CPUS * MAX_COUNTERS];

static int			nr_poll;
static int			nr_cpu;

struct mmap_event {
	struct perf_event_header	header;
	__u32				pid;
	__u32				tid;
	__u64				start;
	__u64				len;
	__u64				pgoff;
	char				filename[PATH_MAX];
};

struct comm_event {
	struct perf_event_header	header;
	__u32				pid;
	__u32				tid;
	char				comm[16];
};


struct mmap_data {
	int			counter;
	void			*base;
	unsigned int		mask;
	unsigned int		prev;
};

static struct mmap_data		mmap_array[MAX_NR_CPUS][MAX_COUNTERS];

static unsigned int mmap_read_head(struct mmap_data *md)
{
	struct perf_counter_mmap_page *pc = md->base;
	int head;

	head = pc->data_head;
	rmb();

	return head;
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
	if (diff > md->mask / 2 || diff < 0) {
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

		while (size) {
			int ret = write(output, buf, size);

			if (ret < 0)
				die("failed to write");

			size -= ret;
			buf += ret;

			bytes_written += ret;
		}
	}

	buf = &data[old & md->mask];
	size = head - old;
	old += size;

	while (size) {
		int ret = write(output, buf, size);

		if (ret < 0)
			die("failed to write");

		size -= ret;
		buf += ret;

		bytes_written += ret;
	}

	md->prev = old;
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
	if (signr == -1)
		return;

	signal(signr, SIG_DFL);
	kill(getpid(), signr);
}

static void pid_synthesize_comm_event(pid_t pid, int full)
{
	struct comm_event comm_ev;
	char filename[PATH_MAX];
	char bf[BUFSIZ];
	int fd, ret;
	size_t size;
	char *field, *sep;
	DIR *tasks;
	struct dirent dirent, *next;

	snprintf(filename, sizeof(filename), "/proc/%d/stat", pid);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "couldn't open %s\n", filename);
		exit(EXIT_FAILURE);
	}
	if (read(fd, bf, sizeof(bf)) < 0) {
		fprintf(stderr, "couldn't read %s\n", filename);
		exit(EXIT_FAILURE);
	}
	close(fd);

	/* 9027 (cat) R 6747 9027 6747 34816 9027 ... */
	memset(&comm_ev, 0, sizeof(comm_ev));
	field = strchr(bf, '(');
	if (field == NULL)
		goto out_failure;
	sep = strchr(++field, ')');
	if (sep == NULL)
		goto out_failure;
	size = sep - field;
	memcpy(comm_ev.comm, field, size++);

	comm_ev.pid = pid;
	comm_ev.header.type = PERF_EVENT_COMM;
	size = ALIGN(size, sizeof(__u64));
	comm_ev.header.size = sizeof(comm_ev) - (sizeof(comm_ev.comm) - size);

	if (!full) {
		comm_ev.tid = pid;

		ret = write(output, &comm_ev, comm_ev.header.size);
		if (ret < 0) {
			perror("failed to write");
			exit(-1);
		}
		return;
	}

	snprintf(filename, sizeof(filename), "/proc/%d/task", pid);

	tasks = opendir(filename);
	while (!readdir_r(tasks, &dirent, &next) && next) {
		char *end;
		pid = strtol(dirent.d_name, &end, 10);
		if (*end)
			continue;

		comm_ev.tid = pid;

		ret = write(output, &comm_ev, comm_ev.header.size);
		if (ret < 0) {
			perror("failed to write");
			exit(-1);
		}
	}
	closedir(tasks);
	return;

out_failure:
	fprintf(stderr, "couldn't get COMM and pgid, malformed %s\n",
		filename);
	exit(EXIT_FAILURE);
}

static void pid_synthesize_mmap_samples(pid_t pid)
{
	char filename[PATH_MAX];
	FILE *fp;

	snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);

	fp = fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "couldn't open %s\n", filename);
		exit(EXIT_FAILURE);
	}
	while (1) {
		char bf[BUFSIZ], *pbf = bf;
		struct mmap_event mmap_ev = {
			.header.type = PERF_EVENT_MMAP,
		};
		int n;
		size_t size;
		if (fgets(bf, sizeof(bf), fp) == NULL)
			break;

		/* 00400000-0040c000 r-xp 00000000 fd:01 41038  /bin/cat */
		n = hex2u64(pbf, &mmap_ev.start);
		if (n < 0)
			continue;
		pbf += n + 1;
		n = hex2u64(pbf, &mmap_ev.len);
		if (n < 0)
			continue;
		pbf += n + 3;
		if (*pbf == 'x') { /* vm_exec */
			char *execname = strrchr(bf, ' ');

			if (execname == NULL || execname[1] != '/')
				continue;

			execname += 1;
			size = strlen(execname);
			execname[size - 1] = '\0'; /* Remove \n */
			memcpy(mmap_ev.filename, execname, size);
			size = ALIGN(size, sizeof(__u64));
			mmap_ev.len -= mmap_ev.start;
			mmap_ev.header.size = (sizeof(mmap_ev) -
					       (sizeof(mmap_ev.filename) - size));
			mmap_ev.pid = pid;
			mmap_ev.tid = pid;

			if (write(output, &mmap_ev, mmap_ev.header.size) < 0) {
				perror("failed to write");
				exit(-1);
			}
		}
	}

	fclose(fp);
}

static void synthesize_samples(void)
{
	DIR *proc;
	struct dirent dirent, *next;

	proc = opendir("/proc");

	while (!readdir_r(proc, &dirent, &next) && next) {
		char *end;
		pid_t pid;

		pid = strtol(dirent.d_name, &end, 10);
		if (*end) /* only interested in proper numerical dirents */
			continue;

		pid_synthesize_comm_event(pid, 1);
		pid_synthesize_mmap_samples(pid);
	}

	closedir(proc);
}

static int group_fd;

static void create_counter(int counter, int cpu, pid_t pid)
{
	struct perf_counter_attr *attr = attrs + counter;
	int track = 1;

	attr->sample_type	= PERF_SAMPLE_IP | PERF_SAMPLE_TID;
	if (freq) {
		attr->sample_type	|= PERF_SAMPLE_PERIOD;
		attr->freq		= 1;
		attr->sample_freq	= freq;
	}
	attr->mmap		= track;
	attr->comm		= track;
	attr->inherit		= (cpu < 0) && inherit;
	attr->disabled		= 1;

	track = 0; /* only the first counter needs these */

try_again:
	fd[nr_cpu][counter] = sys_perf_counter_open(attr, pid, cpu, group_fd, 0);

	if (fd[nr_cpu][counter] < 0) {
		int err = errno;

		if (err == EPERM)
			die("Permission error - are you root?\n");

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
			fd[nr_cpu][counter], strerror(err));
		die("No CONFIG_PERF_COUNTERS=y kernel support configured?\n");
		exit(-1);
	}

	assert(fd[nr_cpu][counter] >= 0);
	fcntl(fd[nr_cpu][counter], F_SETFL, O_NONBLOCK);

	/*
	 * First counter acts as the group leader:
	 */
	if (group && group_fd == -1)
		group_fd = fd[nr_cpu][counter];

	event_array[nr_poll].fd = fd[nr_cpu][counter];
	event_array[nr_poll].events = POLLIN;
	nr_poll++;

	mmap_array[nr_cpu][counter].counter = counter;
	mmap_array[nr_cpu][counter].prev = 0;
	mmap_array[nr_cpu][counter].mask = mmap_pages*page_size - 1;
	mmap_array[nr_cpu][counter].base = mmap(NULL, (mmap_pages+1)*page_size,
			PROT_READ, MAP_SHARED, fd[nr_cpu][counter], 0);
	if (mmap_array[nr_cpu][counter].base == MAP_FAILED) {
		error("failed to mmap with %d (%s)\n", errno, strerror(errno));
		exit(-1);
	}

	ioctl(fd[nr_cpu][counter], PERF_COUNTER_IOC_ENABLE);
}

static void open_counters(int cpu, pid_t pid)
{
	int counter;

	if (pid > 0) {
		pid_synthesize_comm_event(pid, 0);
		pid_synthesize_mmap_samples(pid);
	}

	group_fd = -1;
	for (counter = 0; counter < nr_counters; counter++)
		create_counter(counter, cpu, pid);

	nr_cpu++;
}

static int __cmd_record(int argc, const char **argv)
{
	int i, counter;
	struct stat st;
	pid_t pid;
	int flags;
	int ret;

	page_size = sysconf(_SC_PAGE_SIZE);
	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	assert(nr_cpus <= MAX_NR_CPUS);
	assert(nr_cpus >= 0);

	if (!stat(output_name, &st) && !force && !append_file) {
		fprintf(stderr, "Error, output file %s exists, use -A to append or -f to overwrite.\n",
				output_name);
		exit(-1);
	}

	flags = O_CREAT|O_RDWR;
	if (append_file)
		flags |= O_APPEND;
	else
		flags |= O_TRUNC;

	output = open(output_name, flags, S_IRUSR|S_IWUSR);
	if (output < 0) {
		perror("failed to create output file");
		exit(-1);
	}

	if (!system_wide) {
		open_counters(-1, target_pid != -1 ? target_pid : getpid());
	} else for (i = 0; i < nr_cpus; i++)
		open_counters(i, target_pid);

	atexit(sig_atexit);
	signal(SIGCHLD, sig_handler);
	signal(SIGINT, sig_handler);

	if (target_pid == -1 && argc) {
		pid = fork();
		if (pid < 0)
			perror("failed to fork");

		if (!pid) {
			if (execvp(argv[0], (char **)argv)) {
				perror(argv[0]);
				exit(-1);
			}
		}
	}

	if (realtime_prio) {
		struct sched_param param;

		param.sched_priority = realtime_prio;
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			printf("Could not set realtime priority.\n");
			exit(-1);
		}
	}

	if (system_wide)
		synthesize_samples();

	while (!done) {
		int hits = samples;

		for (i = 0; i < nr_cpu; i++) {
			for (counter = 0; counter < nr_counters; counter++)
				mmap_read(&mmap_array[i][counter]);
		}

		if (hits == samples)
			ret = poll(event_array, nr_poll, 100);
	}

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
	OPT_INTEGER('p', "pid", &target_pid,
		    "record events on existing pid"),
	OPT_INTEGER('r', "realtime", &realtime_prio,
		    "collect data with this RT SCHED_FIFO priority"),
	OPT_BOOLEAN('a', "all-cpus", &system_wide,
			    "system-wide collection from all CPUs"),
	OPT_BOOLEAN('A', "append", &append_file,
			    "append to the output file to do incremental profiling"),
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
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_END()
};

int cmd_record(int argc, const char **argv, const char *prefix)
{
	int counter;

	argc = parse_options(argc, argv, options, record_usage, 0);
	if (!argc && target_pid == -1 && !system_wide)
		usage_with_options(record_usage, options);

	if (!nr_counters) {
		nr_counters	= 1;
		attrs[0].type	= PERF_TYPE_HARDWARE;
		attrs[0].config = PERF_COUNT_HW_CPU_CYCLES;
	}

	for (counter = 0; counter < nr_counters; counter++) {
		if (attrs[counter].sample_period)
			continue;

		attrs[counter].sample_period = default_interval;
	}

	return __cmd_record(argc, argv);
}
