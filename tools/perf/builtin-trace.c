#include <traceevent/event-parse.h>
#include "builtin.h"
#include "util/color.h"
#include "util/debug.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/session.h"
#include "util/thread.h"
#include "util/parse-options.h"
#include "util/strlist.h"
#include "util/intlist.h"
#include "util/thread_map.h"

#include <libaudit.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <linux/futex.h>

static size_t syscall_arg__scnprintf_hex(char *bf, size_t size,
					 unsigned long arg,
					 u8 arg_idx __maybe_unused,
					 u8 *arg_mask __maybe_unused)
{
	return scnprintf(bf, size, "%#lx", arg);
}

#define SCA_HEX syscall_arg__scnprintf_hex

static size_t syscall_arg__scnprintf_whence(char *bf, size_t size,
					    unsigned long arg,
					    u8 arg_idx __maybe_unused,
					    u8 *arg_mask __maybe_unused)
{
	int whence = arg;

	switch (whence) {
#define P_WHENCE(n) case SEEK_##n: return scnprintf(bf, size, #n)
	P_WHENCE(SET);
	P_WHENCE(CUR);
	P_WHENCE(END);
#ifdef SEEK_DATA
	P_WHENCE(DATA);
#endif
#ifdef SEEK_HOLE
	P_WHENCE(HOLE);
#endif
#undef P_WHENCE
	default: break;
	}

	return scnprintf(bf, size, "%#x", whence);
}

#define SCA_WHENCE syscall_arg__scnprintf_whence

static size_t syscall_arg__scnprintf_mmap_prot(char *bf, size_t size,
					       unsigned long arg,
					       u8 arg_idx __maybe_unused,
					       u8 *arg_mask __maybe_unused)
{
	int printed = 0, prot = arg;

	if (prot == PROT_NONE)
		return scnprintf(bf, size, "NONE");
#define	P_MMAP_PROT(n) \
	if (prot & PROT_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		prot &= ~PROT_##n; \
	}

	P_MMAP_PROT(EXEC);
	P_MMAP_PROT(READ);
	P_MMAP_PROT(WRITE);
#ifdef PROT_SEM
	P_MMAP_PROT(SEM);
#endif
	P_MMAP_PROT(GROWSDOWN);
	P_MMAP_PROT(GROWSUP);
#undef P_MMAP_PROT

	if (prot)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", prot);

	return printed;
}

#define SCA_MMAP_PROT syscall_arg__scnprintf_mmap_prot

static size_t syscall_arg__scnprintf_mmap_flags(char *bf, size_t size,
						unsigned long arg, u8 arg_idx __maybe_unused,
						u8 *arg_mask __maybe_unused)
{
	int printed = 0, flags = arg;

#define	P_MMAP_FLAG(n) \
	if (flags & MAP_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~MAP_##n; \
	}

	P_MMAP_FLAG(SHARED);
	P_MMAP_FLAG(PRIVATE);
#ifdef MAP_32BIT
	P_MMAP_FLAG(32BIT);
#endif
	P_MMAP_FLAG(ANONYMOUS);
	P_MMAP_FLAG(DENYWRITE);
	P_MMAP_FLAG(EXECUTABLE);
	P_MMAP_FLAG(FILE);
	P_MMAP_FLAG(FIXED);
	P_MMAP_FLAG(GROWSDOWN);
#ifdef MAP_HUGETLB
	P_MMAP_FLAG(HUGETLB);
#endif
	P_MMAP_FLAG(LOCKED);
	P_MMAP_FLAG(NONBLOCK);
	P_MMAP_FLAG(NORESERVE);
	P_MMAP_FLAG(POPULATE);
	P_MMAP_FLAG(STACK);
#ifdef MAP_UNINITIALIZED
	P_MMAP_FLAG(UNINITIALIZED);
#endif
#undef P_MMAP_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_MMAP_FLAGS syscall_arg__scnprintf_mmap_flags

static size_t syscall_arg__scnprintf_madvise_behavior(char *bf, size_t size,
						      unsigned long arg, u8 arg_idx __maybe_unused,
						      u8 *arg_mask __maybe_unused)
{
	int behavior = arg;

	switch (behavior) {
#define	P_MADV_BHV(n) case MADV_##n: return scnprintf(bf, size, #n)
	P_MADV_BHV(NORMAL);
	P_MADV_BHV(RANDOM);
	P_MADV_BHV(SEQUENTIAL);
	P_MADV_BHV(WILLNEED);
	P_MADV_BHV(DONTNEED);
	P_MADV_BHV(REMOVE);
	P_MADV_BHV(DONTFORK);
	P_MADV_BHV(DOFORK);
	P_MADV_BHV(HWPOISON);
#ifdef MADV_SOFT_OFFLINE
	P_MADV_BHV(SOFT_OFFLINE);
#endif
	P_MADV_BHV(MERGEABLE);
	P_MADV_BHV(UNMERGEABLE);
#ifdef MADV_HUGEPAGE
	P_MADV_BHV(HUGEPAGE);
#endif
#ifdef MADV_NOHUGEPAGE
	P_MADV_BHV(NOHUGEPAGE);
#endif
#ifdef MADV_DONTDUMP
	P_MADV_BHV(DONTDUMP);
#endif
#ifdef MADV_DODUMP
	P_MADV_BHV(DODUMP);
#endif
#undef P_MADV_PHV
	default: break;
	}

	return scnprintf(bf, size, "%#x", behavior);
}

#define SCA_MADV_BHV syscall_arg__scnprintf_madvise_behavior

static size_t syscall_arg__scnprintf_futex_op(char *bf, size_t size, unsigned long arg,
					      u8 arg_idx __maybe_unused, u8 *arg_mask)
{
	enum syscall_futex_args {
		SCF_UADDR   = (1 << 0),
		SCF_OP	    = (1 << 1),
		SCF_VAL	    = (1 << 2),
		SCF_TIMEOUT = (1 << 3),
		SCF_UADDR2  = (1 << 4),
		SCF_VAL3    = (1 << 5),
	};
	int op = arg;
	int cmd = op & FUTEX_CMD_MASK;
	size_t printed = 0;

	switch (cmd) {
#define	P_FUTEX_OP(n) case FUTEX_##n: printed = scnprintf(bf, size, #n);
	P_FUTEX_OP(WAIT);	    *arg_mask |= SCF_VAL3|SCF_UADDR2;		  break;
	P_FUTEX_OP(WAKE);	    *arg_mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(FD);		    *arg_mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(REQUEUE);	    *arg_mask |= SCF_VAL3|SCF_TIMEOUT;	          break;
	P_FUTEX_OP(CMP_REQUEUE);    *arg_mask |= SCF_TIMEOUT;			  break;
	P_FUTEX_OP(CMP_REQUEUE_PI); *arg_mask |= SCF_TIMEOUT;			  break;
	P_FUTEX_OP(WAKE_OP);							  break;
	P_FUTEX_OP(LOCK_PI);	    *arg_mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(UNLOCK_PI);	    *arg_mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(TRYLOCK_PI);	    *arg_mask |= SCF_VAL3|SCF_UADDR2;		  break;
	P_FUTEX_OP(WAIT_BITSET);    *arg_mask |= SCF_UADDR2;			  break;
	P_FUTEX_OP(WAKE_BITSET);    *arg_mask |= SCF_UADDR2;			  break;
	P_FUTEX_OP(WAIT_REQUEUE_PI);						  break;
	default: printed = scnprintf(bf, size, "%#x", cmd);			  break;
	}

	if (op & FUTEX_PRIVATE_FLAG)
		printed += scnprintf(bf + printed, size - printed, "|PRIV");

	if (op & FUTEX_CLOCK_REALTIME)
		printed += scnprintf(bf + printed, size - printed, "|CLKRT");

	return printed;
}

#define SCA_FUTEX_OP  syscall_arg__scnprintf_futex_op

static size_t syscall_arg__scnprintf_open_flags(char *bf, size_t size,
					       unsigned long arg,
					       u8 arg_idx, u8 *arg_mask)
{
	int printed = 0, flags = arg;

	if (!(flags & O_CREAT))
		*arg_mask |= 1 << (arg_idx + 1); /* Mask the mode parm */

	if (flags == 0)
		return scnprintf(bf, size, "RDONLY");
#define	P_FLAG(n) \
	if (flags & O_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~O_##n; \
	}

	P_FLAG(APPEND);
	P_FLAG(ASYNC);
	P_FLAG(CLOEXEC);
	P_FLAG(CREAT);
	P_FLAG(DIRECT);
	P_FLAG(DIRECTORY);
	P_FLAG(EXCL);
	P_FLAG(LARGEFILE);
	P_FLAG(NOATIME);
	P_FLAG(NOCTTY);
#ifdef O_NONBLOCK
	P_FLAG(NONBLOCK);
#elif O_NDELAY
	P_FLAG(NDELAY);
#endif
#ifdef O_PATH
	P_FLAG(PATH);
#endif
	P_FLAG(RDWR);
#ifdef O_DSYNC
	if ((flags & O_SYNC) == O_SYNC)
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", "SYNC");
	else {
		P_FLAG(DSYNC);
	}
#else
	P_FLAG(SYNC);
#endif
	P_FLAG(TRUNC);
	P_FLAG(WRONLY);
#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_OPEN_FLAGS syscall_arg__scnprintf_open_flags

static struct syscall_fmt {
	const char *name;
	const char *alias;
	size_t	   (*arg_scnprintf[6])(char *bf, size_t size, unsigned long arg, u8 arg_idx, u8 *arg_mask);
	bool	   errmsg;
	bool	   timeout;
	bool	   hexret;
} syscall_fmts[] = {
	{ .name	    = "access",	    .errmsg = true, },
	{ .name	    = "arch_prctl", .errmsg = true, .alias = "prctl", },
	{ .name	    = "brk",	    .hexret = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* brk */ }, },
	{ .name	    = "mmap",	    .hexret = true, },
	{ .name	    = "connect",    .errmsg = true, },
	{ .name	    = "fstat",	    .errmsg = true, .alias = "newfstat", },
	{ .name	    = "fstatat",    .errmsg = true, .alias = "newfstatat", },
	{ .name	    = "futex",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_FUTEX_OP, /* op */ }, },
	{ .name	    = "ioctl",	    .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_HEX, /* arg */ }, },
	{ .name	    = "lseek",	    .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_WHENCE, /* whence */ }, },
	{ .name	    = "lstat",	    .errmsg = true, .alias = "newlstat", },
	{ .name     = "madvise",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX,	 /* start */
			     [2] = SCA_MADV_BHV, /* behavior */ }, },
	{ .name	    = "mmap",	    .hexret = true,
	  .arg_scnprintf = { [0] = SCA_HEX,	  /* addr */
			     [2] = SCA_MMAP_PROT, /* prot */
			     [3] = SCA_MMAP_FLAGS, /* flags */ }, },
	{ .name	    = "mprotect",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* start */
			     [2] = SCA_MMAP_PROT, /* prot */ }, },
	{ .name	    = "mremap",	    .hexret = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */
			     [4] = SCA_HEX, /* new_addr */ }, },
	{ .name	    = "munmap",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */ }, },
	{ .name	    = "open",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_OPEN_FLAGS, /* flags */ }, },
	{ .name	    = "open_by_handle_at", .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_OPEN_FLAGS, /* flags */ }, },
	{ .name	    = "openat",	    .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_OPEN_FLAGS, /* flags */ }, },
	{ .name	    = "poll",	    .errmsg = true, .timeout = true, },
	{ .name	    = "ppoll",	    .errmsg = true, .timeout = true, },
	{ .name	    = "pread",	    .errmsg = true, .alias = "pread64", },
	{ .name	    = "pwrite",	    .errmsg = true, .alias = "pwrite64", },
	{ .name	    = "read",	    .errmsg = true, },
	{ .name	    = "recvfrom",   .errmsg = true, },
	{ .name	    = "select",	    .errmsg = true, .timeout = true, },
	{ .name	    = "socket",	    .errmsg = true, },
	{ .name	    = "stat",	    .errmsg = true, .alias = "newstat", },
	{ .name	    = "uname",	    .errmsg = true, .alias = "newuname", },
};

static int syscall_fmt__cmp(const void *name, const void *fmtp)
{
	const struct syscall_fmt *fmt = fmtp;
	return strcmp(name, fmt->name);
}

static struct syscall_fmt *syscall_fmt__find(const char *name)
{
	const int nmemb = ARRAY_SIZE(syscall_fmts);
	return bsearch(name, syscall_fmts, nmemb, sizeof(struct syscall_fmt), syscall_fmt__cmp);
}

struct syscall {
	struct event_format *tp_format;
	const char	    *name;
	bool		    filtered;
	struct syscall_fmt  *fmt;
	size_t		    (**arg_scnprintf)(char *bf, size_t size,
					      unsigned long arg, u8 arg_idx, u8 *args_mask);
};

static size_t fprintf_duration(unsigned long t, FILE *fp)
{
	double duration = (double)t / NSEC_PER_MSEC;
	size_t printed = fprintf(fp, "(");

	if (duration >= 1.0)
		printed += color_fprintf(fp, PERF_COLOR_RED, "%6.3f ms", duration);
	else if (duration >= 0.01)
		printed += color_fprintf(fp, PERF_COLOR_YELLOW, "%6.3f ms", duration);
	else
		printed += color_fprintf(fp, PERF_COLOR_NORMAL, "%6.3f ms", duration);
	return printed + fprintf(fp, "): ");
}

struct thread_trace {
	u64		  entry_time;
	u64		  exit_time;
	bool		  entry_pending;
	unsigned long	  nr_events;
	char		  *entry_str;
	double		  runtime_ms;
};

static struct thread_trace *thread_trace__new(void)
{
	return zalloc(sizeof(struct thread_trace));
}

static struct thread_trace *thread__trace(struct thread *thread, FILE *fp)
{
	struct thread_trace *ttrace;

	if (thread == NULL)
		goto fail;

	if (thread->priv == NULL)
		thread->priv = thread_trace__new();
		
	if (thread->priv == NULL)
		goto fail;

	ttrace = thread->priv;
	++ttrace->nr_events;

	return ttrace;
fail:
	color_fprintf(fp, PERF_COLOR_RED,
		      "WARNING: not enough memory, dropping samples!\n");
	return NULL;
}

struct trace {
	struct perf_tool	tool;
	int			audit_machine;
	struct {
		int		max;
		struct syscall  *table;
	} syscalls;
	struct perf_record_opts opts;
	struct machine		host;
	u64			base_time;
	FILE			*output;
	unsigned long		nr_events;
	struct strlist		*ev_qualifier;
	bool			not_ev_qualifier;
	struct intlist		*tid_list;
	struct intlist		*pid_list;
	bool			sched;
	bool			multiple_threads;
	double			duration_filter;
	double			runtime_ms;
};

static bool trace__filter_duration(struct trace *trace, double t)
{
	return t < (trace->duration_filter * NSEC_PER_MSEC);
}

static size_t trace__fprintf_tstamp(struct trace *trace, u64 tstamp, FILE *fp)
{
	double ts = (double)(tstamp - trace->base_time) / NSEC_PER_MSEC;

	return fprintf(fp, "%10.3f ", ts);
}

static bool done = false;

static void sig_handler(int sig __maybe_unused)
{
	done = true;
}

static size_t trace__fprintf_entry_head(struct trace *trace, struct thread *thread,
					u64 duration, u64 tstamp, FILE *fp)
{
	size_t printed = trace__fprintf_tstamp(trace, tstamp, fp);
	printed += fprintf_duration(duration, fp);

	if (trace->multiple_threads)
		printed += fprintf(fp, "%d ", thread->tid);

	return printed;
}

static int trace__process_event(struct trace *trace, struct machine *machine,
				union perf_event *event)
{
	int ret = 0;

	switch (event->header.type) {
	case PERF_RECORD_LOST:
		color_fprintf(trace->output, PERF_COLOR_RED,
			      "LOST %" PRIu64 " events!\n", event->lost.lost);
		ret = machine__process_lost_event(machine, event);
	default:
		ret = machine__process_event(machine, event);
		break;
	}

	return ret;
}

static int trace__tool_process(struct perf_tool *tool,
			       union perf_event *event,
			       struct perf_sample *sample __maybe_unused,
			       struct machine *machine)
{
	struct trace *trace = container_of(tool, struct trace, tool);
	return trace__process_event(trace, machine, event);
}

static int trace__symbols_init(struct trace *trace, struct perf_evlist *evlist)
{
	int err = symbol__init();

	if (err)
		return err;

	machine__init(&trace->host, "", HOST_KERNEL_ID);
	machine__create_kernel_maps(&trace->host);

	if (perf_target__has_task(&trace->opts.target)) {
		err = perf_event__synthesize_thread_map(&trace->tool, evlist->threads,
							trace__tool_process,
							&trace->host);
	} else {
		err = perf_event__synthesize_threads(&trace->tool, trace__tool_process,
						     &trace->host);
	}

	if (err)
		symbol__exit();

	return err;
}

static int syscall__set_arg_fmts(struct syscall *sc)
{
	struct format_field *field;
	int idx = 0;

	sc->arg_scnprintf = calloc(sc->tp_format->format.nr_fields - 1, sizeof(void *));
	if (sc->arg_scnprintf == NULL)
		return -1;

	for (field = sc->tp_format->format.fields->next; field; field = field->next) {
		if (sc->fmt && sc->fmt->arg_scnprintf[idx])
			sc->arg_scnprintf[idx] = sc->fmt->arg_scnprintf[idx];
		else if (field->flags & FIELD_IS_POINTER)
			sc->arg_scnprintf[idx] = syscall_arg__scnprintf_hex;
		++idx;
	}

	return 0;
}

static int trace__read_syscall_info(struct trace *trace, int id)
{
	char tp_name[128];
	struct syscall *sc;
	const char *name = audit_syscall_to_name(id, trace->audit_machine);

	if (name == NULL)
		return -1;

	if (id > trace->syscalls.max) {
		struct syscall *nsyscalls = realloc(trace->syscalls.table, (id + 1) * sizeof(*sc));

		if (nsyscalls == NULL)
			return -1;

		if (trace->syscalls.max != -1) {
			memset(nsyscalls + trace->syscalls.max + 1, 0,
			       (id - trace->syscalls.max) * sizeof(*sc));
		} else {
			memset(nsyscalls, 0, (id + 1) * sizeof(*sc));
		}

		trace->syscalls.table = nsyscalls;
		trace->syscalls.max   = id;
	}

	sc = trace->syscalls.table + id;
	sc->name = name;

	if (trace->ev_qualifier) {
		bool in = strlist__find(trace->ev_qualifier, name) != NULL;

		if (!(in ^ trace->not_ev_qualifier)) {
			sc->filtered = true;
			/*
			 * No need to do read tracepoint information since this will be
			 * filtered out.
			 */
			return 0;
		}
	}

	sc->fmt  = syscall_fmt__find(sc->name);

	snprintf(tp_name, sizeof(tp_name), "sys_enter_%s", sc->name);
	sc->tp_format = event_format__new("syscalls", tp_name);

	if (sc->tp_format == NULL && sc->fmt && sc->fmt->alias) {
		snprintf(tp_name, sizeof(tp_name), "sys_enter_%s", sc->fmt->alias);
		sc->tp_format = event_format__new("syscalls", tp_name);
	}

	if (sc->tp_format == NULL)
		return -1;

	return syscall__set_arg_fmts(sc);
}

static size_t syscall__scnprintf_args(struct syscall *sc, char *bf, size_t size,
				      unsigned long *args)
{
	int i = 0;
	size_t printed = 0;

	if (sc->tp_format != NULL) {
		struct format_field *field;
		u8 mask = 0, bit = 1;

		for (field = sc->tp_format->format.fields->next; field;
		     field = field->next, ++i, bit <<= 1) {
			if (mask & bit)
				continue;

			printed += scnprintf(bf + printed, size - printed,
					     "%s%s: ", printed ? ", " : "", field->name);

			if (sc->arg_scnprintf && sc->arg_scnprintf[i]) {
				printed += sc->arg_scnprintf[i](bf + printed, size - printed,
								args[i], i, &mask);
			} else {
				printed += scnprintf(bf + printed, size - printed,
						     "%ld", args[i]);
			}
		}
	} else {
		while (i < 6) {
			printed += scnprintf(bf + printed, size - printed,
					     "%sarg%d: %ld",
					     printed ? ", " : "", i, args[i]);
			++i;
		}
	}

	return printed;
}

typedef int (*tracepoint_handler)(struct trace *trace, struct perf_evsel *evsel,
				  struct perf_sample *sample);

static struct syscall *trace__syscall_info(struct trace *trace,
					   struct perf_evsel *evsel,
					   struct perf_sample *sample)
{
	int id = perf_evsel__intval(evsel, sample, "id");

	if (id < 0) {

		/*
		 * XXX: Noticed on x86_64, reproduced as far back as 3.0.36, haven't tried
		 * before that, leaving at a higher verbosity level till that is
		 * explained. Reproduced with plain ftrace with:
		 *
		 * echo 1 > /t/events/raw_syscalls/sys_exit/enable
		 * grep "NR -1 " /t/trace_pipe
		 *
		 * After generating some load on the machine.
 		 */
		if (verbose > 1) {
			static u64 n;
			fprintf(trace->output, "Invalid syscall %d id, skipping (%s, %" PRIu64 ") ...\n",
				id, perf_evsel__name(evsel), ++n);
		}
		return NULL;
	}

	if ((id > trace->syscalls.max || trace->syscalls.table[id].name == NULL) &&
	    trace__read_syscall_info(trace, id))
		goto out_cant_read;

	if ((id > trace->syscalls.max || trace->syscalls.table[id].name == NULL))
		goto out_cant_read;

	return &trace->syscalls.table[id];

out_cant_read:
	if (verbose) {
		fprintf(trace->output, "Problems reading syscall %d", id);
		if (id <= trace->syscalls.max && trace->syscalls.table[id].name != NULL)
			fprintf(trace->output, "(%s)", trace->syscalls.table[id].name);
		fputs(" information\n", trace->output);
	}
	return NULL;
}

static int trace__sys_enter(struct trace *trace, struct perf_evsel *evsel,
			    struct perf_sample *sample)
{
	char *msg;
	void *args;
	size_t printed = 0;
	struct thread *thread;
	struct syscall *sc = trace__syscall_info(trace, evsel, sample);
	struct thread_trace *ttrace;

	if (sc == NULL)
		return -1;

	if (sc->filtered)
		return 0;

	thread = machine__findnew_thread(&trace->host, sample->pid,
					 sample->tid);
	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		return -1;

	args = perf_evsel__rawptr(evsel, sample, "args");
	if (args == NULL) {
		fprintf(trace->output, "Problems reading syscall arguments\n");
		return -1;
	}

	ttrace = thread->priv;

	if (ttrace->entry_str == NULL) {
		ttrace->entry_str = malloc(1024);
		if (!ttrace->entry_str)
			return -1;
	}

	ttrace->entry_time = sample->time;
	msg = ttrace->entry_str;
	printed += scnprintf(msg + printed, 1024 - printed, "%s(", sc->name);

	printed += syscall__scnprintf_args(sc, msg + printed, 1024 - printed,  args);

	if (!strcmp(sc->name, "exit_group") || !strcmp(sc->name, "exit")) {
		if (!trace->duration_filter) {
			trace__fprintf_entry_head(trace, thread, 1, sample->time, trace->output);
			fprintf(trace->output, "%-70s\n", ttrace->entry_str);
		}
	} else
		ttrace->entry_pending = true;

	return 0;
}

static int trace__sys_exit(struct trace *trace, struct perf_evsel *evsel,
			   struct perf_sample *sample)
{
	int ret;
	u64 duration = 0;
	struct thread *thread;
	struct syscall *sc = trace__syscall_info(trace, evsel, sample);
	struct thread_trace *ttrace;

	if (sc == NULL)
		return -1;

	if (sc->filtered)
		return 0;

	thread = machine__findnew_thread(&trace->host, sample->pid,
					 sample->tid);
	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		return -1;

	ret = perf_evsel__intval(evsel, sample, "ret");

	ttrace = thread->priv;

	ttrace->exit_time = sample->time;

	if (ttrace->entry_time) {
		duration = sample->time - ttrace->entry_time;
		if (trace__filter_duration(trace, duration))
			goto out;
	} else if (trace->duration_filter)
		goto out;

	trace__fprintf_entry_head(trace, thread, duration, sample->time, trace->output);

	if (ttrace->entry_pending) {
		fprintf(trace->output, "%-70s", ttrace->entry_str);
	} else {
		fprintf(trace->output, " ... [");
		color_fprintf(trace->output, PERF_COLOR_YELLOW, "continued");
		fprintf(trace->output, "]: %s()", sc->name);
	}

	if (sc->fmt == NULL) {
signed_print:
		fprintf(trace->output, ") = %d", ret);
	} else if (ret < 0 && sc->fmt->errmsg) {
		char bf[256];
		const char *emsg = strerror_r(-ret, bf, sizeof(bf)),
			   *e = audit_errno_to_name(-ret);

		fprintf(trace->output, ") = -1 %s %s", e, emsg);
	} else if (ret == 0 && sc->fmt->timeout)
		fprintf(trace->output, ") = 0 Timeout");
	else if (sc->fmt->hexret)
		fprintf(trace->output, ") = %#x", ret);
	else
		goto signed_print;

	fputc('\n', trace->output);
out:
	ttrace->entry_pending = false;

	return 0;
}

static int trace__sched_stat_runtime(struct trace *trace, struct perf_evsel *evsel,
				     struct perf_sample *sample)
{
        u64 runtime = perf_evsel__intval(evsel, sample, "runtime");
	double runtime_ms = (double)runtime / NSEC_PER_MSEC;
	struct thread *thread = machine__findnew_thread(&trace->host,
							sample->pid,
							sample->tid);
	struct thread_trace *ttrace = thread__trace(thread, trace->output);

	if (ttrace == NULL)
		goto out_dump;

	ttrace->runtime_ms += runtime_ms;
	trace->runtime_ms += runtime_ms;
	return 0;

out_dump:
	fprintf(trace->output, "%s: comm=%s,pid=%u,runtime=%" PRIu64 ",vruntime=%" PRIu64 ")\n",
	       evsel->name,
	       perf_evsel__strval(evsel, sample, "comm"),
	       (pid_t)perf_evsel__intval(evsel, sample, "pid"),
	       runtime,
	       perf_evsel__intval(evsel, sample, "vruntime"));
	return 0;
}

static bool skip_sample(struct trace *trace, struct perf_sample *sample)
{
	if ((trace->pid_list && intlist__find(trace->pid_list, sample->pid)) ||
	    (trace->tid_list && intlist__find(trace->tid_list, sample->tid)))
		return false;

	if (trace->pid_list || trace->tid_list)
		return true;

	return false;
}

static int trace__process_sample(struct perf_tool *tool,
				 union perf_event *event __maybe_unused,
				 struct perf_sample *sample,
				 struct perf_evsel *evsel,
				 struct machine *machine __maybe_unused)
{
	struct trace *trace = container_of(tool, struct trace, tool);
	int err = 0;

	tracepoint_handler handler = evsel->handler.func;

	if (skip_sample(trace, sample))
		return 0;

	if (trace->base_time == 0)
		trace->base_time = sample->time;

	if (handler)
		handler(trace, evsel, sample);

	return err;
}

static bool
perf_session__has_tp(struct perf_session *session, const char *name)
{
	struct perf_evsel *evsel;

	evsel = perf_evlist__find_tracepoint_by_name(session->evlist, name);

	return evsel != NULL;
}

static int parse_target_str(struct trace *trace)
{
	if (trace->opts.target.pid) {
		trace->pid_list = intlist__new(trace->opts.target.pid);
		if (trace->pid_list == NULL) {
			pr_err("Error parsing process id string\n");
			return -EINVAL;
		}
	}

	if (trace->opts.target.tid) {
		trace->tid_list = intlist__new(trace->opts.target.tid);
		if (trace->tid_list == NULL) {
			pr_err("Error parsing thread id string\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int trace__run(struct trace *trace, int argc, const char **argv)
{
	struct perf_evlist *evlist = perf_evlist__new();
	struct perf_evsel *evsel;
	int err = -1, i;
	unsigned long before;
	const bool forks = argc > 0;

	if (evlist == NULL) {
		fprintf(trace->output, "Not enough memory to run!\n");
		goto out;
	}

	if (perf_evlist__add_newtp(evlist, "raw_syscalls", "sys_enter", trace__sys_enter) ||
	    perf_evlist__add_newtp(evlist, "raw_syscalls", "sys_exit", trace__sys_exit)) {
		fprintf(trace->output, "Couldn't read the raw_syscalls tracepoints information!\n");
		goto out_delete_evlist;
	}

	if (trace->sched &&
	    perf_evlist__add_newtp(evlist, "sched", "sched_stat_runtime",
				   trace__sched_stat_runtime)) {
		fprintf(trace->output, "Couldn't read the sched_stat_runtime tracepoint information!\n");
		goto out_delete_evlist;
	}

	err = perf_evlist__create_maps(evlist, &trace->opts.target);
	if (err < 0) {
		fprintf(trace->output, "Problems parsing the target to trace, check your options!\n");
		goto out_delete_evlist;
	}

	err = trace__symbols_init(trace, evlist);
	if (err < 0) {
		fprintf(trace->output, "Problems initializing symbol libraries!\n");
		goto out_delete_maps;
	}

	perf_evlist__config(evlist, &trace->opts);

	signal(SIGCHLD, sig_handler);
	signal(SIGINT, sig_handler);

	if (forks) {
		err = perf_evlist__prepare_workload(evlist, &trace->opts.target,
						    argv, false, false);
		if (err < 0) {
			fprintf(trace->output, "Couldn't run the workload!\n");
			goto out_delete_maps;
		}
	}

	err = perf_evlist__open(evlist);
	if (err < 0) {
		fprintf(trace->output, "Couldn't create the events: %s\n", strerror(errno));
		goto out_delete_maps;
	}

	err = perf_evlist__mmap(evlist, UINT_MAX, false);
	if (err < 0) {
		fprintf(trace->output, "Couldn't mmap the events: %s\n", strerror(errno));
		goto out_close_evlist;
	}

	perf_evlist__enable(evlist);

	if (forks)
		perf_evlist__start_workload(evlist);

	trace->multiple_threads = evlist->threads->map[0] == -1 || evlist->threads->nr > 1;
again:
	before = trace->nr_events;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		union perf_event *event;

		while ((event = perf_evlist__mmap_read(evlist, i)) != NULL) {
			const u32 type = event->header.type;
			tracepoint_handler handler;
			struct perf_sample sample;

			++trace->nr_events;

			err = perf_evlist__parse_sample(evlist, event, &sample);
			if (err) {
				fprintf(trace->output, "Can't parse sample, err = %d, skipping...\n", err);
				continue;
			}

			if (trace->base_time == 0)
				trace->base_time = sample.time;

			if (type != PERF_RECORD_SAMPLE) {
				trace__process_event(trace, &trace->host, event);
				continue;
			}

			evsel = perf_evlist__id2evsel(evlist, sample.id);
			if (evsel == NULL) {
				fprintf(trace->output, "Unknown tp ID %" PRIu64 ", skipping...\n", sample.id);
				continue;
			}

			if (sample.raw_data == NULL) {
				fprintf(trace->output, "%s sample with no payload for tid: %d, cpu %d, raw_size=%d, skipping...\n",
				       perf_evsel__name(evsel), sample.tid,
				       sample.cpu, sample.raw_size);
				continue;
			}

			handler = evsel->handler.func;
			handler(trace, evsel, &sample);

			if (done)
				goto out_unmap_evlist;
		}
	}

	if (trace->nr_events == before) {
		if (done)
			goto out_unmap_evlist;

		poll(evlist->pollfd, evlist->nr_fds, -1);
	}

	if (done)
		perf_evlist__disable(evlist);

	goto again;

out_unmap_evlist:
	perf_evlist__munmap(evlist);
out_close_evlist:
	perf_evlist__close(evlist);
out_delete_maps:
	perf_evlist__delete_maps(evlist);
out_delete_evlist:
	perf_evlist__delete(evlist);
out:
	return err;
}

static int trace__replay(struct trace *trace)
{
	const struct perf_evsel_str_handler handlers[] = {
		{ "raw_syscalls:sys_enter",  trace__sys_enter, },
		{ "raw_syscalls:sys_exit",   trace__sys_exit, },
	};

	struct perf_session *session;
	int err = -1;

	trace->tool.sample	  = trace__process_sample;
	trace->tool.mmap	  = perf_event__process_mmap;
	trace->tool.comm	  = perf_event__process_comm;
	trace->tool.exit	  = perf_event__process_exit;
	trace->tool.fork	  = perf_event__process_fork;
	trace->tool.attr	  = perf_event__process_attr;
	trace->tool.tracing_data = perf_event__process_tracing_data;
	trace->tool.build_id	  = perf_event__process_build_id;

	trace->tool.ordered_samples = true;
	trace->tool.ordering_requires_timestamps = true;

	/* add tid to output */
	trace->multiple_threads = true;

	if (symbol__init() < 0)
		return -1;

	session = perf_session__new(input_name, O_RDONLY, 0, false,
				    &trace->tool);
	if (session == NULL)
		return -ENOMEM;

	err = perf_session__set_tracepoints_handlers(session, handlers);
	if (err)
		goto out;

	if (!perf_session__has_tp(session, "raw_syscalls:sys_enter")) {
		pr_err("Data file does not have raw_syscalls:sys_enter events\n");
		goto out;
	}

	if (!perf_session__has_tp(session, "raw_syscalls:sys_exit")) {
		pr_err("Data file does not have raw_syscalls:sys_exit events\n");
		goto out;
	}

	err = parse_target_str(trace);
	if (err != 0)
		goto out;

	setup_pager();

	err = perf_session__process_events(session, &trace->tool);
	if (err)
		pr_err("Failed to process events, error %d", err);

out:
	perf_session__delete(session);

	return err;
}

static size_t trace__fprintf_threads_header(FILE *fp)
{
	size_t printed;

	printed  = fprintf(fp, "\n _____________________________________________________________________\n");
	printed += fprintf(fp," __)    Summary of events    (__\n\n");
	printed += fprintf(fp,"              [ task - pid ]     [ events ] [ ratio ]  [ runtime ]\n");
	printed += fprintf(fp," _____________________________________________________________________\n\n");

	return printed;
}

static size_t trace__fprintf_thread_summary(struct trace *trace, FILE *fp)
{
	size_t printed = trace__fprintf_threads_header(fp);
	struct rb_node *nd;

	for (nd = rb_first(&trace->host.threads); nd; nd = rb_next(nd)) {
		struct thread *thread = rb_entry(nd, struct thread, rb_node);
		struct thread_trace *ttrace = thread->priv;
		const char *color;
		double ratio;

		if (ttrace == NULL)
			continue;

		ratio = (double)ttrace->nr_events / trace->nr_events * 100.0;

		color = PERF_COLOR_NORMAL;
		if (ratio > 50.0)
			color = PERF_COLOR_RED;
		else if (ratio > 25.0)
			color = PERF_COLOR_GREEN;
		else if (ratio > 5.0)
			color = PERF_COLOR_YELLOW;

		printed += color_fprintf(fp, color, "%20s", thread->comm);
		printed += fprintf(fp, " - %-5d :%11lu   [", thread->tid, ttrace->nr_events);
		printed += color_fprintf(fp, color, "%5.1f%%", ratio);
		printed += fprintf(fp, " ] %10.3f ms\n", ttrace->runtime_ms);
	}

	return printed;
}

static int trace__set_duration(const struct option *opt, const char *str,
			       int unset __maybe_unused)
{
	struct trace *trace = opt->value;

	trace->duration_filter = atof(str);
	return 0;
}

static int trace__open_output(struct trace *trace, const char *filename)
{
	struct stat st;

	if (!stat(filename, &st) && st.st_size) {
		char oldname[PATH_MAX];

		scnprintf(oldname, sizeof(oldname), "%s.old", filename);
		unlink(oldname);
		rename(filename, oldname);
	}

	trace->output = fopen(filename, "w");

	return trace->output == NULL ? -errno : 0;
}

int cmd_trace(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const trace_usage[] = {
		"perf trace [<options>] [<command>]",
		"perf trace [<options>] -- <command> [<options>]",
		NULL
	};
	struct trace trace = {
		.audit_machine = audit_detect_machine(),
		.syscalls = {
			. max = -1,
		},
		.opts = {
			.target = {
				.uid	   = UINT_MAX,
				.uses_mmap = true,
			},
			.user_freq     = UINT_MAX,
			.user_interval = ULLONG_MAX,
			.no_delay      = true,
			.mmap_pages    = 1024,
		},
		.output = stdout,
	};
	const char *output_name = NULL;
	const char *ev_qualifier_str = NULL;
	const struct option trace_options[] = {
	OPT_STRING('e', "expr", &ev_qualifier_str, "expr",
		    "list of events to trace"),
	OPT_STRING('o', "output", &output_name, "file", "output file name"),
	OPT_STRING('i', "input", &input_name, "file", "Analyze events in file"),
	OPT_STRING('p', "pid", &trace.opts.target.pid, "pid",
		    "trace events on existing process id"),
	OPT_STRING('t', "tid", &trace.opts.target.tid, "tid",
		    "trace events on existing thread id"),
	OPT_BOOLEAN('a', "all-cpus", &trace.opts.target.system_wide,
		    "system-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &trace.opts.target.cpu_list, "cpu",
		    "list of cpus to monitor"),
	OPT_BOOLEAN(0, "no-inherit", &trace.opts.no_inherit,
		    "child tasks do not inherit counters"),
	OPT_UINTEGER('m', "mmap-pages", &trace.opts.mmap_pages,
		     "number of mmap data pages"),
	OPT_STRING('u', "uid", &trace.opts.target.uid_str, "user",
		   "user to profile"),
	OPT_CALLBACK(0, "duration", &trace, "float",
		     "show only events with duration > N.M ms",
		     trace__set_duration),
	OPT_BOOLEAN(0, "sched", &trace.sched, "show blocking scheduler events"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};
	int err;
	char bf[BUFSIZ];

	argc = parse_options(argc, argv, trace_options, trace_usage, 0);

	if (output_name != NULL) {
		err = trace__open_output(&trace, output_name);
		if (err < 0) {
			perror("failed to create output file");
			goto out;
		}
	}

	if (ev_qualifier_str != NULL) {
		const char *s = ev_qualifier_str;

		trace.not_ev_qualifier = *s == '!';
		if (trace.not_ev_qualifier)
			++s;
		trace.ev_qualifier = strlist__new(true, s);
		if (trace.ev_qualifier == NULL) {
			fputs("Not enough memory to parse event qualifier",
			      trace.output);
			err = -ENOMEM;
			goto out_close;
		}
	}

	err = perf_target__validate(&trace.opts.target);
	if (err) {
		perf_target__strerror(&trace.opts.target, err, bf, sizeof(bf));
		fprintf(trace.output, "%s", bf);
		goto out_close;
	}

	err = perf_target__parse_uid(&trace.opts.target);
	if (err) {
		perf_target__strerror(&trace.opts.target, err, bf, sizeof(bf));
		fprintf(trace.output, "%s", bf);
		goto out_close;
	}

	if (!argc && perf_target__none(&trace.opts.target))
		trace.opts.target.system_wide = true;

	if (input_name)
		err = trace__replay(&trace);
	else
		err = trace__run(&trace, argc, argv);

	if (trace.sched && !err)
		trace__fprintf_thread_summary(&trace, trace.output);

out_close:
	if (output_name != NULL)
		fclose(trace.output);
out:
	return err;
}
