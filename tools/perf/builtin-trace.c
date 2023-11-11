/*
 * builtin-trace.c
 *
 * Builtin 'trace' command:
 *
 * Display a continuously updated trace of any workload, CPU, specific PID,
 * system wide, etc.  Default format is loosely strace like, but any other
 * event may be specified using --event.
 *
 * Copyright (C) 2012, 2013, 2014, 2015 Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Initially based on the 'trace' prototype by Thomas Gleixner:
 *
 * http://lwn.net/Articles/415728/ ("Announcing a new utility: 'trace'")
 */

#include "util/record.h"
#include <traceevent/event-parse.h>
#include <api/fs/tracing_path.h>
#ifdef HAVE_LIBBPF_SUPPORT
#include <bpf/bpf.h>
#endif
#include "util/bpf_map.h"
#include "util/rlimit.h"
#include "builtin.h"
#include "util/cgroup.h"
#include "util/color.h"
#include "util/config.h"
#include "util/debug.h"
#include "util/dso.h"
#include "util/env.h"
#include "util/event.h"
#include "util/evsel.h"
#include "util/evsel_fprintf.h"
#include "util/synthetic-events.h"
#include "util/evlist.h"
#include "util/evswitch.h"
#include "util/mmap.h"
#include <subcmd/pager.h>
#include <subcmd/exec-cmd.h>
#include "util/machine.h"
#include "util/map.h"
#include "util/symbol.h"
#include "util/path.h"
#include "util/session.h"
#include "util/thread.h"
#include <subcmd/parse-options.h>
#include "util/strlist.h"
#include "util/intlist.h"
#include "util/thread_map.h"
#include "util/stat.h"
#include "util/tool.h"
#include "util/util.h"
#include "trace/beauty/beauty.h"
#include "trace-event.h"
#include "util/parse-events.h"
#include "util/bpf-loader.h"
#include "util/tracepoint.h"
#include "callchain.h"
#include "print_binary.h"
#include "string2.h"
#include "syscalltbl.h"
#include "rb_resort.h"
#include "../perf.h"

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <linux/err.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/stringify.h>
#include <linux/time64.h>
#include <linux/zalloc.h>
#include <fcntl.h>
#include <sys/sysmacros.h>

#include <linux/ctype.h>
#include <perf/mmap.h>

#ifndef O_CLOEXEC
# define O_CLOEXEC		02000000
#endif

#ifndef F_LINUX_SPECIFIC_BASE
# define F_LINUX_SPECIFIC_BASE	1024
#endif

#define RAW_SYSCALL_ARGS_NUM	6

/*
 * strtoul: Go from a string to a value, i.e. for msr: MSR_FS_BASE to 0xc0000100
 */
struct syscall_arg_fmt {
	size_t	   (*scnprintf)(char *bf, size_t size, struct syscall_arg *arg);
	bool	   (*strtoul)(char *bf, size_t size, struct syscall_arg *arg, u64 *val);
	unsigned long (*mask_val)(struct syscall_arg *arg, unsigned long val);
	void	   *parm;
	const char *name;
	u16	   nr_entries; // for arrays
	bool	   show_zero;
};

struct syscall_fmt {
	const char *name;
	const char *alias;
	struct {
		const char *sys_enter,
			   *sys_exit;
	}	   bpf_prog_name;
	struct syscall_arg_fmt arg[RAW_SYSCALL_ARGS_NUM];
	u8	   nr_args;
	bool	   errpid;
	bool	   timeout;
	bool	   hexret;
};

struct trace {
	struct perf_tool	tool;
	struct syscalltbl	*sctbl;
	struct {
		struct syscall  *table;
		struct bpf_map  *map;
		struct { // per syscall BPF_MAP_TYPE_PROG_ARRAY
			struct bpf_map  *sys_enter,
					*sys_exit;
		}		prog_array;
		struct {
			struct evsel *sys_enter,
					  *sys_exit,
					  *augmented;
		}		events;
		struct bpf_program *unaugmented_prog;
	} syscalls;
	struct {
		struct bpf_map *map;
	} dump;
	struct record_opts	opts;
	struct evlist	*evlist;
	struct machine		*host;
	struct thread		*current;
	struct bpf_object	*bpf_obj;
	struct cgroup		*cgroup;
	u64			base_time;
	FILE			*output;
	unsigned long		nr_events;
	unsigned long		nr_events_printed;
	unsigned long		max_events;
	struct evswitch		evswitch;
	struct strlist		*ev_qualifier;
	struct {
		size_t		nr;
		int		*entries;
	}			ev_qualifier_ids;
	struct {
		size_t		nr;
		pid_t		*entries;
		struct bpf_map  *map;
	}			filter_pids;
	double			duration_filter;
	double			runtime_ms;
	struct {
		u64		vfs_getname,
				proc_getname;
	} stats;
	unsigned int		max_stack;
	unsigned int		min_stack;
	int			raw_augmented_syscalls_args_size;
	bool			raw_augmented_syscalls;
	bool			fd_path_disabled;
	bool			sort_events;
	bool			not_ev_qualifier;
	bool			live;
	bool			full_time;
	bool			sched;
	bool			multiple_threads;
	bool			summary;
	bool			summary_only;
	bool			errno_summary;
	bool			failure_only;
	bool			show_comm;
	bool			print_sample;
	bool			show_tool_stats;
	bool			trace_syscalls;
	bool			libtraceevent_print;
	bool			kernel_syscallchains;
	s16			args_alignment;
	bool			show_tstamp;
	bool			show_duration;
	bool			show_zeros;
	bool			show_arg_names;
	bool			show_string_prefix;
	bool			force;
	bool			vfs_getname;
	int			trace_pgfaults;
	char			*perfconfig_events;
	struct {
		struct ordered_events	data;
		u64			last;
	} oe;
};

struct tp_field {
	int offset;
	union {
		u64 (*integer)(struct tp_field *field, struct perf_sample *sample);
		void *(*pointer)(struct tp_field *field, struct perf_sample *sample);
	};
};

#define TP_UINT_FIELD(bits) \
static u64 tp_field__u##bits(struct tp_field *field, struct perf_sample *sample) \
{ \
	u##bits value; \
	memcpy(&value, sample->raw_data + field->offset, sizeof(value)); \
	return value;  \
}

TP_UINT_FIELD(8);
TP_UINT_FIELD(16);
TP_UINT_FIELD(32);
TP_UINT_FIELD(64);

#define TP_UINT_FIELD__SWAPPED(bits) \
static u64 tp_field__swapped_u##bits(struct tp_field *field, struct perf_sample *sample) \
{ \
	u##bits value; \
	memcpy(&value, sample->raw_data + field->offset, sizeof(value)); \
	return bswap_##bits(value);\
}

TP_UINT_FIELD__SWAPPED(16);
TP_UINT_FIELD__SWAPPED(32);
TP_UINT_FIELD__SWAPPED(64);

static int __tp_field__init_uint(struct tp_field *field, int size, int offset, bool needs_swap)
{
	field->offset = offset;

	switch (size) {
	case 1:
		field->integer = tp_field__u8;
		break;
	case 2:
		field->integer = needs_swap ? tp_field__swapped_u16 : tp_field__u16;
		break;
	case 4:
		field->integer = needs_swap ? tp_field__swapped_u32 : tp_field__u32;
		break;
	case 8:
		field->integer = needs_swap ? tp_field__swapped_u64 : tp_field__u64;
		break;
	default:
		return -1;
	}

	return 0;
}

static int tp_field__init_uint(struct tp_field *field, struct tep_format_field *format_field, bool needs_swap)
{
	return __tp_field__init_uint(field, format_field->size, format_field->offset, needs_swap);
}

static void *tp_field__ptr(struct tp_field *field, struct perf_sample *sample)
{
	return sample->raw_data + field->offset;
}

static int __tp_field__init_ptr(struct tp_field *field, int offset)
{
	field->offset = offset;
	field->pointer = tp_field__ptr;
	return 0;
}

static int tp_field__init_ptr(struct tp_field *field, struct tep_format_field *format_field)
{
	return __tp_field__init_ptr(field, format_field->offset);
}

struct syscall_tp {
	struct tp_field id;
	union {
		struct tp_field args, ret;
	};
};

/*
 * The evsel->priv as used by 'perf trace'
 * sc:	for raw_syscalls:sys_{enter,exit} and syscalls:sys_{enter,exit}_SYSCALLNAME
 * fmt: for all the other tracepoints
 */
struct evsel_trace {
	struct syscall_tp	sc;
	struct syscall_arg_fmt  *fmt;
};

static struct evsel_trace *evsel_trace__new(void)
{
	return zalloc(sizeof(struct evsel_trace));
}

static void evsel_trace__delete(struct evsel_trace *et)
{
	if (et == NULL)
		return;

	zfree(&et->fmt);
	free(et);
}

/*
 * Used with raw_syscalls:sys_{enter,exit} and with the
 * syscalls:sys_{enter,exit}_SYSCALL tracepoints
 */
static inline struct syscall_tp *__evsel__syscall_tp(struct evsel *evsel)
{
	struct evsel_trace *et = evsel->priv;

	return &et->sc;
}

static struct syscall_tp *evsel__syscall_tp(struct evsel *evsel)
{
	if (evsel->priv == NULL) {
		evsel->priv = evsel_trace__new();
		if (evsel->priv == NULL)
			return NULL;
	}

	return __evsel__syscall_tp(evsel);
}

/*
 * Used with all the other tracepoints.
 */
static inline struct syscall_arg_fmt *__evsel__syscall_arg_fmt(struct evsel *evsel)
{
	struct evsel_trace *et = evsel->priv;

	return et->fmt;
}

static struct syscall_arg_fmt *evsel__syscall_arg_fmt(struct evsel *evsel)
{
	struct evsel_trace *et = evsel->priv;

	if (evsel->priv == NULL) {
		et = evsel->priv = evsel_trace__new();

		if (et == NULL)
			return NULL;
	}

	if (et->fmt == NULL) {
		et->fmt = calloc(evsel->tp_format->format.nr_fields, sizeof(struct syscall_arg_fmt));
		if (et->fmt == NULL)
			goto out_delete;
	}

	return __evsel__syscall_arg_fmt(evsel);

out_delete:
	evsel_trace__delete(evsel->priv);
	evsel->priv = NULL;
	return NULL;
}

static int evsel__init_tp_uint_field(struct evsel *evsel, struct tp_field *field, const char *name)
{
	struct tep_format_field *format_field = evsel__field(evsel, name);

	if (format_field == NULL)
		return -1;

	return tp_field__init_uint(field, format_field, evsel->needs_swap);
}

#define perf_evsel__init_sc_tp_uint_field(evsel, name) \
	({ struct syscall_tp *sc = __evsel__syscall_tp(evsel);\
	   evsel__init_tp_uint_field(evsel, &sc->name, #name); })

static int evsel__init_tp_ptr_field(struct evsel *evsel, struct tp_field *field, const char *name)
{
	struct tep_format_field *format_field = evsel__field(evsel, name);

	if (format_field == NULL)
		return -1;

	return tp_field__init_ptr(field, format_field);
}

#define perf_evsel__init_sc_tp_ptr_field(evsel, name) \
	({ struct syscall_tp *sc = __evsel__syscall_tp(evsel);\
	   evsel__init_tp_ptr_field(evsel, &sc->name, #name); })

static void evsel__delete_priv(struct evsel *evsel)
{
	zfree(&evsel->priv);
	evsel__delete(evsel);
}

static int evsel__init_syscall_tp(struct evsel *evsel)
{
	struct syscall_tp *sc = evsel__syscall_tp(evsel);

	if (sc != NULL) {
		if (evsel__init_tp_uint_field(evsel, &sc->id, "__syscall_nr") &&
		    evsel__init_tp_uint_field(evsel, &sc->id, "nr"))
			return -ENOENT;
		return 0;
	}

	return -ENOMEM;
}

static int evsel__init_augmented_syscall_tp(struct evsel *evsel, struct evsel *tp)
{
	struct syscall_tp *sc = evsel__syscall_tp(evsel);

	if (sc != NULL) {
		struct tep_format_field *syscall_id = evsel__field(tp, "id");
		if (syscall_id == NULL)
			syscall_id = evsel__field(tp, "__syscall_nr");
		if (syscall_id == NULL ||
		    __tp_field__init_uint(&sc->id, syscall_id->size, syscall_id->offset, evsel->needs_swap))
			return -EINVAL;

		return 0;
	}

	return -ENOMEM;
}

static int evsel__init_augmented_syscall_tp_args(struct evsel *evsel)
{
	struct syscall_tp *sc = __evsel__syscall_tp(evsel);

	return __tp_field__init_ptr(&sc->args, sc->id.offset + sizeof(u64));
}

static int evsel__init_augmented_syscall_tp_ret(struct evsel *evsel)
{
	struct syscall_tp *sc = __evsel__syscall_tp(evsel);

	return __tp_field__init_uint(&sc->ret, sizeof(u64), sc->id.offset + sizeof(u64), evsel->needs_swap);
}

static int evsel__init_raw_syscall_tp(struct evsel *evsel, void *handler)
{
	if (evsel__syscall_tp(evsel) != NULL) {
		if (perf_evsel__init_sc_tp_uint_field(evsel, id))
			return -ENOENT;

		evsel->handler = handler;
		return 0;
	}

	return -ENOMEM;
}

static struct evsel *perf_evsel__raw_syscall_newtp(const char *direction, void *handler)
{
	struct evsel *evsel = evsel__newtp("raw_syscalls", direction);

	/* older kernel (e.g., RHEL6) use syscalls:{enter,exit} */
	if (IS_ERR(evsel))
		evsel = evsel__newtp("syscalls", direction);

	if (IS_ERR(evsel))
		return NULL;

	if (evsel__init_raw_syscall_tp(evsel, handler))
		goto out_delete;

	return evsel;

out_delete:
	evsel__delete_priv(evsel);
	return NULL;
}

#define perf_evsel__sc_tp_uint(evsel, name, sample) \
	({ struct syscall_tp *fields = __evsel__syscall_tp(evsel); \
	   fields->name.integer(&fields->name, sample); })

#define perf_evsel__sc_tp_ptr(evsel, name, sample) \
	({ struct syscall_tp *fields = __evsel__syscall_tp(evsel); \
	   fields->name.pointer(&fields->name, sample); })

size_t strarray__scnprintf_suffix(struct strarray *sa, char *bf, size_t size, const char *intfmt, bool show_suffix, int val)
{
	int idx = val - sa->offset;

	if (idx < 0 || idx >= sa->nr_entries || sa->entries[idx] == NULL) {
		size_t printed = scnprintf(bf, size, intfmt, val);
		if (show_suffix)
			printed += scnprintf(bf + printed, size - printed, " /* %s??? */", sa->prefix);
		return printed;
	}

	return scnprintf(bf, size, "%s%s", sa->entries[idx], show_suffix ? sa->prefix : "");
}

size_t strarray__scnprintf(struct strarray *sa, char *bf, size_t size, const char *intfmt, bool show_prefix, int val)
{
	int idx = val - sa->offset;

	if (idx < 0 || idx >= sa->nr_entries || sa->entries[idx] == NULL) {
		size_t printed = scnprintf(bf, size, intfmt, val);
		if (show_prefix)
			printed += scnprintf(bf + printed, size - printed, " /* %s??? */", sa->prefix);
		return printed;
	}

	return scnprintf(bf, size, "%s%s", show_prefix ? sa->prefix : "", sa->entries[idx]);
}

static size_t __syscall_arg__scnprintf_strarray(char *bf, size_t size,
						const char *intfmt,
					        struct syscall_arg *arg)
{
	return strarray__scnprintf(arg->parm, bf, size, intfmt, arg->show_string_prefix, arg->val);
}

static size_t syscall_arg__scnprintf_strarray(char *bf, size_t size,
					      struct syscall_arg *arg)
{
	return __syscall_arg__scnprintf_strarray(bf, size, "%d", arg);
}

#define SCA_STRARRAY syscall_arg__scnprintf_strarray

bool syscall_arg__strtoul_strarray(char *bf, size_t size, struct syscall_arg *arg, u64 *ret)
{
	return strarray__strtoul(arg->parm, bf, size, ret);
}

bool syscall_arg__strtoul_strarray_flags(char *bf, size_t size, struct syscall_arg *arg, u64 *ret)
{
	return strarray__strtoul_flags(arg->parm, bf, size, ret);
}

bool syscall_arg__strtoul_strarrays(char *bf, size_t size, struct syscall_arg *arg, u64 *ret)
{
	return strarrays__strtoul(arg->parm, bf, size, ret);
}

size_t syscall_arg__scnprintf_strarray_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	return strarray__scnprintf_flags(arg->parm, bf, size, arg->show_string_prefix, arg->val);
}

size_t strarrays__scnprintf(struct strarrays *sas, char *bf, size_t size, const char *intfmt, bool show_prefix, int val)
{
	size_t printed;
	int i;

	for (i = 0; i < sas->nr_entries; ++i) {
		struct strarray *sa = sas->entries[i];
		int idx = val - sa->offset;

		if (idx >= 0 && idx < sa->nr_entries) {
			if (sa->entries[idx] == NULL)
				break;
			return scnprintf(bf, size, "%s%s", show_prefix ? sa->prefix : "", sa->entries[idx]);
		}
	}

	printed = scnprintf(bf, size, intfmt, val);
	if (show_prefix)
		printed += scnprintf(bf + printed, size - printed, " /* %s??? */", sas->entries[0]->prefix);
	return printed;
}

bool strarray__strtoul(struct strarray *sa, char *bf, size_t size, u64 *ret)
{
	int i;

	for (i = 0; i < sa->nr_entries; ++i) {
		if (sa->entries[i] && strncmp(sa->entries[i], bf, size) == 0 && sa->entries[i][size] == '\0') {
			*ret = sa->offset + i;
			return true;
		}
	}

	return false;
}

bool strarray__strtoul_flags(struct strarray *sa, char *bf, size_t size, u64 *ret)
{
	u64 val = 0;
	char *tok = bf, *sep, *end;

	*ret = 0;

	while (size != 0) {
		int toklen = size;

		sep = memchr(tok, '|', size);
		if (sep != NULL) {
			size -= sep - tok + 1;

			end = sep - 1;
			while (end > tok && isspace(*end))
				--end;

			toklen = end - tok + 1;
		}

		while (isspace(*tok))
			++tok;

		if (isalpha(*tok) || *tok == '_') {
			if (!strarray__strtoul(sa, tok, toklen, &val))
				return false;
		} else
			val = strtoul(tok, NULL, 0);

		*ret |= (1 << (val - 1));

		if (sep == NULL)
			break;
		tok = sep + 1;
	}

	return true;
}

bool strarrays__strtoul(struct strarrays *sas, char *bf, size_t size, u64 *ret)
{
	int i;

	for (i = 0; i < sas->nr_entries; ++i) {
		struct strarray *sa = sas->entries[i];

		if (strarray__strtoul(sa, bf, size, ret))
			return true;
	}

	return false;
}

size_t syscall_arg__scnprintf_strarrays(char *bf, size_t size,
					struct syscall_arg *arg)
{
	return strarrays__scnprintf(arg->parm, bf, size, "%d", arg->show_string_prefix, arg->val);
}

#ifndef AT_FDCWD
#define AT_FDCWD	-100
#endif

static size_t syscall_arg__scnprintf_fd_at(char *bf, size_t size,
					   struct syscall_arg *arg)
{
	int fd = arg->val;
	const char *prefix = "AT_FD";

	if (fd == AT_FDCWD)
		return scnprintf(bf, size, "%s%s", arg->show_string_prefix ? prefix : "", "CWD");

	return syscall_arg__scnprintf_fd(bf, size, arg);
}

#define SCA_FDAT syscall_arg__scnprintf_fd_at

static size_t syscall_arg__scnprintf_close_fd(char *bf, size_t size,
					      struct syscall_arg *arg);

#define SCA_CLOSE_FD syscall_arg__scnprintf_close_fd

size_t syscall_arg__scnprintf_hex(char *bf, size_t size, struct syscall_arg *arg)
{
	return scnprintf(bf, size, "%#lx", arg->val);
}

size_t syscall_arg__scnprintf_ptr(char *bf, size_t size, struct syscall_arg *arg)
{
	if (arg->val == 0)
		return scnprintf(bf, size, "NULL");
	return syscall_arg__scnprintf_hex(bf, size, arg);
}

size_t syscall_arg__scnprintf_int(char *bf, size_t size, struct syscall_arg *arg)
{
	return scnprintf(bf, size, "%d", arg->val);
}

size_t syscall_arg__scnprintf_long(char *bf, size_t size, struct syscall_arg *arg)
{
	return scnprintf(bf, size, "%ld", arg->val);
}

static size_t syscall_arg__scnprintf_char_array(char *bf, size_t size, struct syscall_arg *arg)
{
	// XXX Hey, maybe for sched:sched_switch prev/next comm fields we can
	//     fill missing comms using thread__set_comm()...
	//     here or in a special syscall_arg__scnprintf_pid_sched_tp...
	return scnprintf(bf, size, "\"%-.*s\"", arg->fmt->nr_entries ?: arg->len, arg->val);
}

#define SCA_CHAR_ARRAY syscall_arg__scnprintf_char_array

static const char *bpf_cmd[] = {
	"MAP_CREATE", "MAP_LOOKUP_ELEM", "MAP_UPDATE_ELEM", "MAP_DELETE_ELEM",
	"MAP_GET_NEXT_KEY", "PROG_LOAD", "OBJ_PIN", "OBJ_GET", "PROG_ATTACH",
	"PROG_DETACH", "PROG_TEST_RUN", "PROG_GET_NEXT_ID", "MAP_GET_NEXT_ID",
	"PROG_GET_FD_BY_ID", "MAP_GET_FD_BY_ID", "OBJ_GET_INFO_BY_FD",
	"PROG_QUERY", "RAW_TRACEPOINT_OPEN", "BTF_LOAD", "BTF_GET_FD_BY_ID",
	"TASK_FD_QUERY", "MAP_LOOKUP_AND_DELETE_ELEM", "MAP_FREEZE",
	"BTF_GET_NEXT_ID", "MAP_LOOKUP_BATCH", "MAP_LOOKUP_AND_DELETE_BATCH",
	"MAP_UPDATE_BATCH", "MAP_DELETE_BATCH", "LINK_CREATE", "LINK_UPDATE",
	"LINK_GET_FD_BY_ID", "LINK_GET_NEXT_ID", "ENABLE_STATS", "ITER_CREATE",
	"LINK_DETACH", "PROG_BIND_MAP",
};
static DEFINE_STRARRAY(bpf_cmd, "BPF_");

static const char *fsmount_flags[] = {
	[1] = "CLOEXEC",
};
static DEFINE_STRARRAY(fsmount_flags, "FSMOUNT_");

#include "trace/beauty/generated/fsconfig_arrays.c"

static DEFINE_STRARRAY(fsconfig_cmds, "FSCONFIG_");

static const char *epoll_ctl_ops[] = { "ADD", "DEL", "MOD", };
static DEFINE_STRARRAY_OFFSET(epoll_ctl_ops, "EPOLL_CTL_", 1);

static const char *itimers[] = { "REAL", "VIRTUAL", "PROF", };
static DEFINE_STRARRAY(itimers, "ITIMER_");

static const char *keyctl_options[] = {
	"GET_KEYRING_ID", "JOIN_SESSION_KEYRING", "UPDATE", "REVOKE", "CHOWN",
	"SETPERM", "DESCRIBE", "CLEAR", "LINK", "UNLINK", "SEARCH", "READ",
	"INSTANTIATE", "NEGATE", "SET_REQKEY_KEYRING", "SET_TIMEOUT",
	"ASSUME_AUTHORITY", "GET_SECURITY", "SESSION_TO_PARENT", "REJECT",
	"INSTANTIATE_IOV", "INVALIDATE", "GET_PERSISTENT",
};
static DEFINE_STRARRAY(keyctl_options, "KEYCTL_");

static const char *whences[] = { "SET", "CUR", "END",
#ifdef SEEK_DATA
"DATA",
#endif
#ifdef SEEK_HOLE
"HOLE",
#endif
};
static DEFINE_STRARRAY(whences, "SEEK_");

static const char *fcntl_cmds[] = {
	"DUPFD", "GETFD", "SETFD", "GETFL", "SETFL", "GETLK", "SETLK",
	"SETLKW", "SETOWN", "GETOWN", "SETSIG", "GETSIG", "GETLK64",
	"SETLK64", "SETLKW64", "SETOWN_EX", "GETOWN_EX",
	"GETOWNER_UIDS",
};
static DEFINE_STRARRAY(fcntl_cmds, "F_");

static const char *fcntl_linux_specific_cmds[] = {
	"SETLEASE", "GETLEASE", "NOTIFY", [5] =	"CANCELLK", "DUPFD_CLOEXEC",
	"SETPIPE_SZ", "GETPIPE_SZ", "ADD_SEALS", "GET_SEALS",
	"GET_RW_HINT", "SET_RW_HINT", "GET_FILE_RW_HINT", "SET_FILE_RW_HINT",
};

static DEFINE_STRARRAY_OFFSET(fcntl_linux_specific_cmds, "F_", F_LINUX_SPECIFIC_BASE);

static struct strarray *fcntl_cmds_arrays[] = {
	&strarray__fcntl_cmds,
	&strarray__fcntl_linux_specific_cmds,
};

static DEFINE_STRARRAYS(fcntl_cmds_arrays);

static const char *rlimit_resources[] = {
	"CPU", "FSIZE", "DATA", "STACK", "CORE", "RSS", "NPROC", "NOFILE",
	"MEMLOCK", "AS", "LOCKS", "SIGPENDING", "MSGQUEUE", "NICE", "RTPRIO",
	"RTTIME",
};
static DEFINE_STRARRAY(rlimit_resources, "RLIMIT_");

static const char *sighow[] = { "BLOCK", "UNBLOCK", "SETMASK", };
static DEFINE_STRARRAY(sighow, "SIG_");

static const char *clockid[] = {
	"REALTIME", "MONOTONIC", "PROCESS_CPUTIME_ID", "THREAD_CPUTIME_ID",
	"MONOTONIC_RAW", "REALTIME_COARSE", "MONOTONIC_COARSE", "BOOTTIME",
	"REALTIME_ALARM", "BOOTTIME_ALARM", "SGI_CYCLE", "TAI"
};
static DEFINE_STRARRAY(clockid, "CLOCK_");

static size_t syscall_arg__scnprintf_access_mode(char *bf, size_t size,
						 struct syscall_arg *arg)
{
	bool show_prefix = arg->show_string_prefix;
	const char *suffix = "_OK";
	size_t printed = 0;
	int mode = arg->val;

	if (mode == F_OK) /* 0 */
		return scnprintf(bf, size, "F%s", show_prefix ? suffix : "");
#define	P_MODE(n) \
	if (mode & n##_OK) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", #n, show_prefix ? suffix : ""); \
		mode &= ~n##_OK; \
	}

	P_MODE(R);
	P_MODE(W);
	P_MODE(X);
#undef P_MODE

	if (mode)
		printed += scnprintf(bf + printed, size - printed, "|%#x", mode);

	return printed;
}

#define SCA_ACCMODE syscall_arg__scnprintf_access_mode

static size_t syscall_arg__scnprintf_filename(char *bf, size_t size,
					      struct syscall_arg *arg);

#define SCA_FILENAME syscall_arg__scnprintf_filename

static size_t syscall_arg__scnprintf_pipe_flags(char *bf, size_t size,
						struct syscall_arg *arg)
{
	bool show_prefix = arg->show_string_prefix;
	const char *prefix = "O_";
	int printed = 0, flags = arg->val;

#define	P_FLAG(n) \
	if (flags & O_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s%s", printed ? "|" : "", show_prefix ? prefix : "", #n); \
		flags &= ~O_##n; \
	}

	P_FLAG(CLOEXEC);
	P_FLAG(NONBLOCK);
#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_PIPE_FLAGS syscall_arg__scnprintf_pipe_flags

#ifndef GRND_NONBLOCK
#define GRND_NONBLOCK	0x0001
#endif
#ifndef GRND_RANDOM
#define GRND_RANDOM	0x0002
#endif

static size_t syscall_arg__scnprintf_getrandom_flags(char *bf, size_t size,
						   struct syscall_arg *arg)
{
	bool show_prefix = arg->show_string_prefix;
	const char *prefix = "GRND_";
	int printed = 0, flags = arg->val;

#define	P_FLAG(n) \
	if (flags & GRND_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s%s", printed ? "|" : "", show_prefix ? prefix : "", #n); \
		flags &= ~GRND_##n; \
	}

	P_FLAG(RANDOM);
	P_FLAG(NONBLOCK);
#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_GETRANDOM_FLAGS syscall_arg__scnprintf_getrandom_flags

#define STRARRAY(name, array) \
	  { .scnprintf	= SCA_STRARRAY, \
	    .strtoul	= STUL_STRARRAY, \
	    .parm	= &strarray__##array, }

#define STRARRAY_FLAGS(name, array) \
	  { .scnprintf	= SCA_STRARRAY_FLAGS, \
	    .strtoul	= STUL_STRARRAY_FLAGS, \
	    .parm	= &strarray__##array, }

#include "trace/beauty/arch_errno_names.c"
#include "trace/beauty/eventfd.c"
#include "trace/beauty/futex_op.c"
#include "trace/beauty/futex_val3.c"
#include "trace/beauty/mmap.c"
#include "trace/beauty/mode_t.c"
#include "trace/beauty/msg_flags.c"
#include "trace/beauty/open_flags.c"
#include "trace/beauty/perf_event_open.c"
#include "trace/beauty/pid.c"
#include "trace/beauty/sched_policy.c"
#include "trace/beauty/seccomp.c"
#include "trace/beauty/signum.c"
#include "trace/beauty/socket_type.c"
#include "trace/beauty/waitid_options.c"

static struct syscall_fmt syscall_fmts[] = {
	{ .name	    = "access",
	  .arg = { [1] = { .scnprintf = SCA_ACCMODE,  /* mode */ }, }, },
	{ .name	    = "arch_prctl",
	  .arg = { [0] = { .scnprintf = SCA_X86_ARCH_PRCTL_CODE, /* code */ },
		   [1] = { .scnprintf = SCA_PTR, /* arg2 */ }, }, },
	{ .name	    = "bind",
	  .arg = { [0] = { .scnprintf = SCA_INT, /* fd */ },
		   [1] = { .scnprintf = SCA_SOCKADDR, /* umyaddr */ },
		   [2] = { .scnprintf = SCA_INT, /* addrlen */ }, }, },
	{ .name	    = "bpf",
	  .arg = { [0] = STRARRAY(cmd, bpf_cmd), }, },
	{ .name	    = "brk",	    .hexret = true,
	  .arg = { [0] = { .scnprintf = SCA_PTR, /* brk */ }, }, },
	{ .name     = "clock_gettime",
	  .arg = { [0] = STRARRAY(clk_id, clockid), }, },
	{ .name	    = "clone",	    .errpid = true, .nr_args = 5,
	  .arg = { [0] = { .name = "flags",	    .scnprintf = SCA_CLONE_FLAGS, },
		   [1] = { .name = "child_stack",   .scnprintf = SCA_HEX, },
		   [2] = { .name = "parent_tidptr", .scnprintf = SCA_HEX, },
		   [3] = { .name = "child_tidptr",  .scnprintf = SCA_HEX, },
		   [4] = { .name = "tls",	    .scnprintf = SCA_HEX, }, }, },
	{ .name	    = "close",
	  .arg = { [0] = { .scnprintf = SCA_CLOSE_FD, /* fd */ }, }, },
	{ .name	    = "connect",
	  .arg = { [0] = { .scnprintf = SCA_INT, /* fd */ },
		   [1] = { .scnprintf = SCA_SOCKADDR, /* servaddr */ },
		   [2] = { .scnprintf = SCA_INT, /* addrlen */ }, }, },
	{ .name	    = "epoll_ctl",
	  .arg = { [1] = STRARRAY(op, epoll_ctl_ops), }, },
	{ .name	    = "eventfd2",
	  .arg = { [1] = { .scnprintf = SCA_EFD_FLAGS, /* flags */ }, }, },
	{ .name	    = "fchmodat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* fd */ }, }, },
	{ .name	    = "fchownat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* fd */ }, }, },
	{ .name	    = "fcntl",
	  .arg = { [1] = { .scnprintf = SCA_FCNTL_CMD,  /* cmd */
			   .strtoul   = STUL_STRARRAYS,
			   .parm      = &strarrays__fcntl_cmds_arrays,
			   .show_zero = true, },
		   [2] = { .scnprintf =  SCA_FCNTL_ARG, /* arg */ }, }, },
	{ .name	    = "flock",
	  .arg = { [1] = { .scnprintf = SCA_FLOCK, /* cmd */ }, }, },
	{ .name     = "fsconfig",
	  .arg = { [1] = STRARRAY(cmd, fsconfig_cmds), }, },
	{ .name     = "fsmount",
	  .arg = { [1] = STRARRAY_FLAGS(flags, fsmount_flags),
		   [2] = { .scnprintf = SCA_FSMOUNT_ATTR_FLAGS, /* attr_flags */ }, }, },
	{ .name     = "fspick",
	  .arg = { [0] = { .scnprintf = SCA_FDAT,	  /* dfd */ },
		   [1] = { .scnprintf = SCA_FILENAME,	  /* path */ },
		   [2] = { .scnprintf = SCA_FSPICK_FLAGS, /* flags */ }, }, },
	{ .name	    = "fstat", .alias = "newfstat", },
	{ .name	    = "fstatat", .alias = "newfstatat", },
	{ .name	    = "futex",
	  .arg = { [1] = { .scnprintf = SCA_FUTEX_OP, /* op */ },
		   [5] = { .scnprintf = SCA_FUTEX_VAL3, /* val3 */ }, }, },
	{ .name	    = "futimesat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* fd */ }, }, },
	{ .name	    = "getitimer",
	  .arg = { [0] = STRARRAY(which, itimers), }, },
	{ .name	    = "getpid",	    .errpid = true, },
	{ .name	    = "getpgid",    .errpid = true, },
	{ .name	    = "getppid",    .errpid = true, },
	{ .name	    = "getrandom",
	  .arg = { [2] = { .scnprintf = SCA_GETRANDOM_FLAGS, /* flags */ }, }, },
	{ .name	    = "getrlimit",
	  .arg = { [0] = STRARRAY(resource, rlimit_resources), }, },
	{ .name	    = "getsockopt",
	  .arg = { [1] = STRARRAY(level, socket_level), }, },
	{ .name	    = "gettid",	    .errpid = true, },
	{ .name	    = "ioctl",
	  .arg = {
#if defined(__i386__) || defined(__x86_64__)
/*
 * FIXME: Make this available to all arches.
 */
		   [1] = { .scnprintf = SCA_IOCTL_CMD, /* cmd */ },
		   [2] = { .scnprintf = SCA_HEX, /* arg */ }, }, },
#else
		   [2] = { .scnprintf = SCA_HEX, /* arg */ }, }, },
#endif
	{ .name	    = "kcmp",	    .nr_args = 5,
	  .arg = { [0] = { .name = "pid1",	.scnprintf = SCA_PID, },
		   [1] = { .name = "pid2",	.scnprintf = SCA_PID, },
		   [2] = { .name = "type",	.scnprintf = SCA_KCMP_TYPE, },
		   [3] = { .name = "idx1",	.scnprintf = SCA_KCMP_IDX, },
		   [4] = { .name = "idx2",	.scnprintf = SCA_KCMP_IDX, }, }, },
	{ .name	    = "keyctl",
	  .arg = { [0] = STRARRAY(option, keyctl_options), }, },
	{ .name	    = "kill",
	  .arg = { [1] = { .scnprintf = SCA_SIGNUM, /* sig */ }, }, },
	{ .name	    = "linkat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* fd */ }, }, },
	{ .name	    = "lseek",
	  .arg = { [2] = STRARRAY(whence, whences), }, },
	{ .name	    = "lstat", .alias = "newlstat", },
	{ .name     = "madvise",
	  .arg = { [0] = { .scnprintf = SCA_HEX,      /* start */ },
		   [2] = { .scnprintf = SCA_MADV_BHV, /* behavior */ }, }, },
	{ .name	    = "mkdirat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* fd */ }, }, },
	{ .name	    = "mknodat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* fd */ }, }, },
	{ .name	    = "mmap",	    .hexret = true,
/* The standard mmap maps to old_mmap on s390x */
#if defined(__s390x__)
	.alias = "old_mmap",
#endif
	  .arg = { [2] = { .scnprintf = SCA_MMAP_PROT,	/* prot */ },
		   [3] = { .scnprintf = SCA_MMAP_FLAGS,	/* flags */
			   .strtoul   = STUL_STRARRAY_FLAGS,
			   .parm      = &strarray__mmap_flags, },
		   [5] = { .scnprintf = SCA_HEX,	/* offset */ }, }, },
	{ .name	    = "mount",
	  .arg = { [0] = { .scnprintf = SCA_FILENAME, /* dev_name */ },
		   [3] = { .scnprintf = SCA_MOUNT_FLAGS, /* flags */
			   .mask_val  = SCAMV_MOUNT_FLAGS, /* flags */ }, }, },
	{ .name	    = "move_mount",
	  .arg = { [0] = { .scnprintf = SCA_FDAT,	/* from_dfd */ },
		   [1] = { .scnprintf = SCA_FILENAME, /* from_pathname */ },
		   [2] = { .scnprintf = SCA_FDAT,	/* to_dfd */ },
		   [3] = { .scnprintf = SCA_FILENAME, /* to_pathname */ },
		   [4] = { .scnprintf = SCA_MOVE_MOUNT_FLAGS, /* flags */ }, }, },
	{ .name	    = "mprotect",
	  .arg = { [0] = { .scnprintf = SCA_HEX,	/* start */ },
		   [2] = { .scnprintf = SCA_MMAP_PROT,	/* prot */ }, }, },
	{ .name	    = "mq_unlink",
	  .arg = { [0] = { .scnprintf = SCA_FILENAME, /* u_name */ }, }, },
	{ .name	    = "mremap",	    .hexret = true,
	  .arg = { [3] = { .scnprintf = SCA_MREMAP_FLAGS, /* flags */ }, }, },
	{ .name	    = "name_to_handle_at",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* dfd */ }, }, },
	{ .name	    = "newfstatat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* dfd */ }, }, },
	{ .name	    = "open",
	  .arg = { [1] = { .scnprintf = SCA_OPEN_FLAGS, /* flags */ }, }, },
	{ .name	    = "open_by_handle_at",
	  .arg = { [0] = { .scnprintf = SCA_FDAT,	/* dfd */ },
		   [2] = { .scnprintf = SCA_OPEN_FLAGS, /* flags */ }, }, },
	{ .name	    = "openat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT,	/* dfd */ },
		   [2] = { .scnprintf = SCA_OPEN_FLAGS, /* flags */ }, }, },
	{ .name	    = "perf_event_open",
	  .arg = { [2] = { .scnprintf = SCA_INT,	/* cpu */ },
		   [3] = { .scnprintf = SCA_FD,		/* group_fd */ },
		   [4] = { .scnprintf = SCA_PERF_FLAGS, /* flags */ }, }, },
	{ .name	    = "pipe2",
	  .arg = { [1] = { .scnprintf = SCA_PIPE_FLAGS, /* flags */ }, }, },
	{ .name	    = "pkey_alloc",
	  .arg = { [1] = { .scnprintf = SCA_PKEY_ALLOC_ACCESS_RIGHTS,	/* access_rights */ }, }, },
	{ .name	    = "pkey_free",
	  .arg = { [0] = { .scnprintf = SCA_INT,	/* key */ }, }, },
	{ .name	    = "pkey_mprotect",
	  .arg = { [0] = { .scnprintf = SCA_HEX,	/* start */ },
		   [2] = { .scnprintf = SCA_MMAP_PROT,	/* prot */ },
		   [3] = { .scnprintf = SCA_INT,	/* pkey */ }, }, },
	{ .name	    = "poll", .timeout = true, },
	{ .name	    = "ppoll", .timeout = true, },
	{ .name	    = "prctl",
	  .arg = { [0] = { .scnprintf = SCA_PRCTL_OPTION, /* option */
			   .strtoul   = STUL_STRARRAY,
			   .parm      = &strarray__prctl_options, },
		   [1] = { .scnprintf = SCA_PRCTL_ARG2, /* arg2 */ },
		   [2] = { .scnprintf = SCA_PRCTL_ARG3, /* arg3 */ }, }, },
	{ .name	    = "pread", .alias = "pread64", },
	{ .name	    = "preadv", .alias = "pread", },
	{ .name	    = "prlimit64",
	  .arg = { [1] = STRARRAY(resource, rlimit_resources), }, },
	{ .name	    = "pwrite", .alias = "pwrite64", },
	{ .name	    = "readlinkat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* dfd */ }, }, },
	{ .name	    = "recvfrom",
	  .arg = { [3] = { .scnprintf = SCA_MSG_FLAGS, /* flags */ }, }, },
	{ .name	    = "recvmmsg",
	  .arg = { [3] = { .scnprintf = SCA_MSG_FLAGS, /* flags */ }, }, },
	{ .name	    = "recvmsg",
	  .arg = { [2] = { .scnprintf = SCA_MSG_FLAGS, /* flags */ }, }, },
	{ .name	    = "renameat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* olddirfd */ },
		   [2] = { .scnprintf = SCA_FDAT, /* newdirfd */ }, }, },
	{ .name	    = "renameat2",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* olddirfd */ },
		   [2] = { .scnprintf = SCA_FDAT, /* newdirfd */ },
		   [4] = { .scnprintf = SCA_RENAMEAT2_FLAGS, /* flags */ }, }, },
	{ .name	    = "rt_sigaction",
	  .arg = { [0] = { .scnprintf = SCA_SIGNUM, /* sig */ }, }, },
	{ .name	    = "rt_sigprocmask",
	  .arg = { [0] = STRARRAY(how, sighow), }, },
	{ .name	    = "rt_sigqueueinfo",
	  .arg = { [1] = { .scnprintf = SCA_SIGNUM, /* sig */ }, }, },
	{ .name	    = "rt_tgsigqueueinfo",
	  .arg = { [2] = { .scnprintf = SCA_SIGNUM, /* sig */ }, }, },
	{ .name	    = "sched_setscheduler",
	  .arg = { [1] = { .scnprintf = SCA_SCHED_POLICY, /* policy */ }, }, },
	{ .name	    = "seccomp",
	  .arg = { [0] = { .scnprintf = SCA_SECCOMP_OP,	   /* op */ },
		   [1] = { .scnprintf = SCA_SECCOMP_FLAGS, /* flags */ }, }, },
	{ .name	    = "select", .timeout = true, },
	{ .name	    = "sendfile", .alias = "sendfile64", },
	{ .name	    = "sendmmsg",
	  .arg = { [3] = { .scnprintf = SCA_MSG_FLAGS, /* flags */ }, }, },
	{ .name	    = "sendmsg",
	  .arg = { [2] = { .scnprintf = SCA_MSG_FLAGS, /* flags */ }, }, },
	{ .name	    = "sendto",
	  .arg = { [3] = { .scnprintf = SCA_MSG_FLAGS, /* flags */ },
		   [4] = { .scnprintf = SCA_SOCKADDR, /* addr */ }, }, },
	{ .name	    = "set_tid_address", .errpid = true, },
	{ .name	    = "setitimer",
	  .arg = { [0] = STRARRAY(which, itimers), }, },
	{ .name	    = "setrlimit",
	  .arg = { [0] = STRARRAY(resource, rlimit_resources), }, },
	{ .name	    = "setsockopt",
	  .arg = { [1] = STRARRAY(level, socket_level), }, },
	{ .name	    = "socket",
	  .arg = { [0] = STRARRAY(family, socket_families),
		   [1] = { .scnprintf = SCA_SK_TYPE, /* type */ },
		   [2] = { .scnprintf = SCA_SK_PROTO, /* protocol */ }, }, },
	{ .name	    = "socketpair",
	  .arg = { [0] = STRARRAY(family, socket_families),
		   [1] = { .scnprintf = SCA_SK_TYPE, /* type */ },
		   [2] = { .scnprintf = SCA_SK_PROTO, /* protocol */ }, }, },
	{ .name	    = "stat", .alias = "newstat", },
	{ .name	    = "statx",
	  .arg = { [0] = { .scnprintf = SCA_FDAT,	 /* fdat */ },
		   [2] = { .scnprintf = SCA_STATX_FLAGS, /* flags */ } ,
		   [3] = { .scnprintf = SCA_STATX_MASK,	 /* mask */ }, }, },
	{ .name	    = "swapoff",
	  .arg = { [0] = { .scnprintf = SCA_FILENAME, /* specialfile */ }, }, },
	{ .name	    = "swapon",
	  .arg = { [0] = { .scnprintf = SCA_FILENAME, /* specialfile */ }, }, },
	{ .name	    = "symlinkat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* dfd */ }, }, },
	{ .name	    = "sync_file_range",
	  .arg = { [3] = { .scnprintf = SCA_SYNC_FILE_RANGE_FLAGS, /* flags */ }, }, },
	{ .name	    = "tgkill",
	  .arg = { [2] = { .scnprintf = SCA_SIGNUM, /* sig */ }, }, },
	{ .name	    = "tkill",
	  .arg = { [1] = { .scnprintf = SCA_SIGNUM, /* sig */ }, }, },
	{ .name     = "umount2", .alias = "umount",
	  .arg = { [0] = { .scnprintf = SCA_FILENAME, /* name */ }, }, },
	{ .name	    = "uname", .alias = "newuname", },
	{ .name	    = "unlinkat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* dfd */ }, }, },
	{ .name	    = "utimensat",
	  .arg = { [0] = { .scnprintf = SCA_FDAT, /* dirfd */ }, }, },
	{ .name	    = "wait4",	    .errpid = true,
	  .arg = { [2] = { .scnprintf = SCA_WAITID_OPTIONS, /* options */ }, }, },
	{ .name	    = "waitid",	    .errpid = true,
	  .arg = { [3] = { .scnprintf = SCA_WAITID_OPTIONS, /* options */ }, }, },
};

static int syscall_fmt__cmp(const void *name, const void *fmtp)
{
	const struct syscall_fmt *fmt = fmtp;
	return strcmp(name, fmt->name);
}

static struct syscall_fmt *__syscall_fmt__find(struct syscall_fmt *fmts, const int nmemb, const char *name)
{
	return bsearch(name, fmts, nmemb, sizeof(struct syscall_fmt), syscall_fmt__cmp);
}

static struct syscall_fmt *syscall_fmt__find(const char *name)
{
	const int nmemb = ARRAY_SIZE(syscall_fmts);
	return __syscall_fmt__find(syscall_fmts, nmemb, name);
}

static struct syscall_fmt *__syscall_fmt__find_by_alias(struct syscall_fmt *fmts, const int nmemb, const char *alias)
{
	int i;

	for (i = 0; i < nmemb; ++i) {
		if (fmts[i].alias && strcmp(fmts[i].alias, alias) == 0)
			return &fmts[i];
	}

	return NULL;
}

static struct syscall_fmt *syscall_fmt__find_by_alias(const char *alias)
{
	const int nmemb = ARRAY_SIZE(syscall_fmts);
	return __syscall_fmt__find_by_alias(syscall_fmts, nmemb, alias);
}

/*
 * is_exit: is this "exit" or "exit_group"?
 * is_open: is this "open" or "openat"? To associate the fd returned in sys_exit with the pathname in sys_enter.
 * args_size: sum of the sizes of the syscall arguments, anything after that is augmented stuff: pathname for openat, etc.
 * nonexistent: Just a hole in the syscall table, syscall id not allocated
 */
struct syscall {
	struct tep_event    *tp_format;
	int		    nr_args;
	int		    args_size;
	struct {
		struct bpf_program *sys_enter,
				   *sys_exit;
	}		    bpf_prog;
	bool		    is_exit;
	bool		    is_open;
	bool		    nonexistent;
	struct tep_format_field *args;
	const char	    *name;
	struct syscall_fmt  *fmt;
	struct syscall_arg_fmt *arg_fmt;
};

/*
 * Must match what is in the BPF program:
 *
 * tools/perf/examples/bpf/augmented_raw_syscalls.c
 */
struct bpf_map_syscall_entry {
	bool	enabled;
	u16	string_args_len[RAW_SYSCALL_ARGS_NUM];
};

/*
 * We need to have this 'calculated' boolean because in some cases we really
 * don't know what is the duration of a syscall, for instance, when we start
 * a session and some threads are waiting for a syscall to finish, say 'poll',
 * in which case all we can do is to print "( ? ) for duration and for the
 * start timestamp.
 */
static size_t fprintf_duration(unsigned long t, bool calculated, FILE *fp)
{
	double duration = (double)t / NSEC_PER_MSEC;
	size_t printed = fprintf(fp, "(");

	if (!calculated)
		printed += fprintf(fp, "         ");
	else if (duration >= 1.0)
		printed += color_fprintf(fp, PERF_COLOR_RED, "%6.3f ms", duration);
	else if (duration >= 0.01)
		printed += color_fprintf(fp, PERF_COLOR_YELLOW, "%6.3f ms", duration);
	else
		printed += color_fprintf(fp, PERF_COLOR_NORMAL, "%6.3f ms", duration);
	return printed + fprintf(fp, "): ");
}

/**
 * filename.ptr: The filename char pointer that will be vfs_getname'd
 * filename.entry_str_pos: Where to insert the string translated from
 *                         filename.ptr by the vfs_getname tracepoint/kprobe.
 * ret_scnprintf: syscall args may set this to a different syscall return
 *                formatter, for instance, fcntl may return fds, file flags, etc.
 */
struct thread_trace {
	u64		  entry_time;
	bool		  entry_pending;
	unsigned long	  nr_events;
	unsigned long	  pfmaj, pfmin;
	char		  *entry_str;
	double		  runtime_ms;
	size_t		  (*ret_scnprintf)(char *bf, size_t size, struct syscall_arg *arg);
        struct {
		unsigned long ptr;
		short int     entry_str_pos;
		bool	      pending_open;
		unsigned int  namelen;
		char	      *name;
	} filename;
	struct {
		int	      max;
		struct file   *table;
	} files;

	struct intlist *syscall_stats;
};

static struct thread_trace *thread_trace__new(void)
{
	struct thread_trace *ttrace =  zalloc(sizeof(struct thread_trace));

	if (ttrace) {
		ttrace->files.max = -1;
		ttrace->syscall_stats = intlist__new(NULL);
	}

	return ttrace;
}

static struct thread_trace *thread__trace(struct thread *thread, FILE *fp)
{
	struct thread_trace *ttrace;

	if (thread == NULL)
		goto fail;

	if (thread__priv(thread) == NULL)
		thread__set_priv(thread, thread_trace__new());

	if (thread__priv(thread) == NULL)
		goto fail;

	ttrace = thread__priv(thread);
	++ttrace->nr_events;

	return ttrace;
fail:
	color_fprintf(fp, PERF_COLOR_RED,
		      "WARNING: not enough memory, dropping samples!\n");
	return NULL;
}


void syscall_arg__set_ret_scnprintf(struct syscall_arg *arg,
				    size_t (*ret_scnprintf)(char *bf, size_t size, struct syscall_arg *arg))
{
	struct thread_trace *ttrace = thread__priv(arg->thread);

	ttrace->ret_scnprintf = ret_scnprintf;
}

#define TRACE_PFMAJ		(1 << 0)
#define TRACE_PFMIN		(1 << 1)

static const size_t trace__entry_str_size = 2048;

static struct file *thread_trace__files_entry(struct thread_trace *ttrace, int fd)
{
	if (fd < 0)
		return NULL;

	if (fd > ttrace->files.max) {
		struct file *nfiles = realloc(ttrace->files.table, (fd + 1) * sizeof(struct file));

		if (nfiles == NULL)
			return NULL;

		if (ttrace->files.max != -1) {
			memset(nfiles + ttrace->files.max + 1, 0,
			       (fd - ttrace->files.max) * sizeof(struct file));
		} else {
			memset(nfiles, 0, (fd + 1) * sizeof(struct file));
		}

		ttrace->files.table = nfiles;
		ttrace->files.max   = fd;
	}

	return ttrace->files.table + fd;
}

struct file *thread__files_entry(struct thread *thread, int fd)
{
	return thread_trace__files_entry(thread__priv(thread), fd);
}

static int trace__set_fd_pathname(struct thread *thread, int fd, const char *pathname)
{
	struct thread_trace *ttrace = thread__priv(thread);
	struct file *file = thread_trace__files_entry(ttrace, fd);

	if (file != NULL) {
		struct stat st;
		if (stat(pathname, &st) == 0)
			file->dev_maj = major(st.st_rdev);
		file->pathname = strdup(pathname);
		if (file->pathname)
			return 0;
	}

	return -1;
}

static int thread__read_fd_path(struct thread *thread, int fd)
{
	char linkname[PATH_MAX], pathname[PATH_MAX];
	struct stat st;
	int ret;

	if (thread->pid_ == thread->tid) {
		scnprintf(linkname, sizeof(linkname),
			  "/proc/%d/fd/%d", thread->pid_, fd);
	} else {
		scnprintf(linkname, sizeof(linkname),
			  "/proc/%d/task/%d/fd/%d", thread->pid_, thread->tid, fd);
	}

	if (lstat(linkname, &st) < 0 || st.st_size + 1 > (off_t)sizeof(pathname))
		return -1;

	ret = readlink(linkname, pathname, sizeof(pathname));

	if (ret < 0 || ret > st.st_size)
		return -1;

	pathname[ret] = '\0';
	return trace__set_fd_pathname(thread, fd, pathname);
}

static const char *thread__fd_path(struct thread *thread, int fd,
				   struct trace *trace)
{
	struct thread_trace *ttrace = thread__priv(thread);

	if (ttrace == NULL || trace->fd_path_disabled)
		return NULL;

	if (fd < 0)
		return NULL;

	if ((fd > ttrace->files.max || ttrace->files.table[fd].pathname == NULL)) {
		if (!trace->live)
			return NULL;
		++trace->stats.proc_getname;
		if (thread__read_fd_path(thread, fd))
			return NULL;
	}

	return ttrace->files.table[fd].pathname;
}

size_t syscall_arg__scnprintf_fd(char *bf, size_t size, struct syscall_arg *arg)
{
	int fd = arg->val;
	size_t printed = scnprintf(bf, size, "%d", fd);
	const char *path = thread__fd_path(arg->thread, fd, arg->trace);

	if (path)
		printed += scnprintf(bf + printed, size - printed, "<%s>", path);

	return printed;
}

size_t pid__scnprintf_fd(struct trace *trace, pid_t pid, int fd, char *bf, size_t size)
{
        size_t printed = scnprintf(bf, size, "%d", fd);
	struct thread *thread = machine__find_thread(trace->host, pid, pid);

	if (thread) {
		const char *path = thread__fd_path(thread, fd, trace);

		if (path)
			printed += scnprintf(bf + printed, size - printed, "<%s>", path);

		thread__put(thread);
	}

        return printed;
}

static size_t syscall_arg__scnprintf_close_fd(char *bf, size_t size,
					      struct syscall_arg *arg)
{
	int fd = arg->val;
	size_t printed = syscall_arg__scnprintf_fd(bf, size, arg);
	struct thread_trace *ttrace = thread__priv(arg->thread);

	if (ttrace && fd >= 0 && fd <= ttrace->files.max)
		zfree(&ttrace->files.table[fd].pathname);

	return printed;
}

static void thread__set_filename_pos(struct thread *thread, const char *bf,
				     unsigned long ptr)
{
	struct thread_trace *ttrace = thread__priv(thread);

	ttrace->filename.ptr = ptr;
	ttrace->filename.entry_str_pos = bf - ttrace->entry_str;
}

static size_t syscall_arg__scnprintf_augmented_string(struct syscall_arg *arg, char *bf, size_t size)
{
	struct augmented_arg *augmented_arg = arg->augmented.args;
	size_t printed = scnprintf(bf, size, "\"%.*s\"", augmented_arg->size, augmented_arg->value);
	/*
	 * So that the next arg with a payload can consume its augmented arg, i.e. for rename* syscalls
	 * we would have two strings, each prefixed by its size.
	 */
	int consumed = sizeof(*augmented_arg) + augmented_arg->size;

	arg->augmented.args = ((void *)arg->augmented.args) + consumed;
	arg->augmented.size -= consumed;

	return printed;
}

static size_t syscall_arg__scnprintf_filename(char *bf, size_t size,
					      struct syscall_arg *arg)
{
	unsigned long ptr = arg->val;

	if (arg->augmented.args)
		return syscall_arg__scnprintf_augmented_string(arg, bf, size);

	if (!arg->trace->vfs_getname)
		return scnprintf(bf, size, "%#x", ptr);

	thread__set_filename_pos(arg->thread, bf, ptr);
	return 0;
}

static bool trace__filter_duration(struct trace *trace, double t)
{
	return t < (trace->duration_filter * NSEC_PER_MSEC);
}

static size_t __trace__fprintf_tstamp(struct trace *trace, u64 tstamp, FILE *fp)
{
	double ts = (double)(tstamp - trace->base_time) / NSEC_PER_MSEC;

	return fprintf(fp, "%10.3f ", ts);
}

/*
 * We're handling tstamp=0 as an undefined tstamp, i.e. like when we are
 * using ttrace->entry_time for a thread that receives a sys_exit without
 * first having received a sys_enter ("poll" issued before tracing session
 * starts, lost sys_enter exit due to ring buffer overflow).
 */
static size_t trace__fprintf_tstamp(struct trace *trace, u64 tstamp, FILE *fp)
{
	if (tstamp > 0)
		return __trace__fprintf_tstamp(trace, tstamp, fp);

	return fprintf(fp, "         ? ");
}

static pid_t workload_pid = -1;
static bool done = false;
static bool interrupted = false;

static void sighandler_interrupt(int sig __maybe_unused)
{
	done = interrupted = true;
}

static void sighandler_chld(int sig __maybe_unused, siginfo_t *info,
			    void *context __maybe_unused)
{
	if (info->si_pid == workload_pid)
		done = true;
}

static size_t trace__fprintf_comm_tid(struct trace *trace, struct thread *thread, FILE *fp)
{
	size_t printed = 0;

	if (trace->multiple_threads) {
		if (trace->show_comm)
			printed += fprintf(fp, "%.14s/", thread__comm_str(thread));
		printed += fprintf(fp, "%d ", thread->tid);
	}

	return printed;
}

static size_t trace__fprintf_entry_head(struct trace *trace, struct thread *thread,
					u64 duration, bool duration_calculated, u64 tstamp, FILE *fp)
{
	size_t printed = 0;

	if (trace->show_tstamp)
		printed = trace__fprintf_tstamp(trace, tstamp, fp);
	if (trace->show_duration)
		printed += fprintf_duration(duration, duration_calculated, fp);
	return printed + trace__fprintf_comm_tid(trace, thread, fp);
}

static int trace__process_event(struct trace *trace, struct machine *machine,
				union perf_event *event, struct perf_sample *sample)
{
	int ret = 0;

	switch (event->header.type) {
	case PERF_RECORD_LOST:
		color_fprintf(trace->output, PERF_COLOR_RED,
			      "LOST %" PRIu64 " events!\n", event->lost.lost);
		ret = machine__process_lost_event(machine, event, sample);
		break;
	default:
		ret = machine__process_event(machine, event, sample);
		break;
	}

	return ret;
}

static int trace__tool_process(struct perf_tool *tool,
			       union perf_event *event,
			       struct perf_sample *sample,
			       struct machine *machine)
{
	struct trace *trace = container_of(tool, struct trace, tool);
	return trace__process_event(trace, machine, event, sample);
}

static char *trace__machine__resolve_kernel_addr(void *vmachine, unsigned long long *addrp, char **modp)
{
	struct machine *machine = vmachine;

	if (machine->kptr_restrict_warned)
		return NULL;

	if (symbol_conf.kptr_restrict) {
		pr_warning("Kernel address maps (/proc/{kallsyms,modules}) are restricted.\n\n"
			   "Check /proc/sys/kernel/kptr_restrict and /proc/sys/kernel/perf_event_paranoid.\n\n"
			   "Kernel samples will not be resolved.\n");
		machine->kptr_restrict_warned = true;
		return NULL;
	}

	return machine__resolve_kernel_addr(vmachine, addrp, modp);
}

static int trace__symbols_init(struct trace *trace, struct evlist *evlist)
{
	int err = symbol__init(NULL);

	if (err)
		return err;

	trace->host = machine__new_host();
	if (trace->host == NULL)
		return -ENOMEM;

	err = trace_event__register_resolver(trace->host, trace__machine__resolve_kernel_addr);
	if (err < 0)
		goto out;

	err = __machine__synthesize_threads(trace->host, &trace->tool, &trace->opts.target,
					    evlist->core.threads, trace__tool_process,
					    true, false, 1);
out:
	if (err)
		symbol__exit();

	return err;
}

static void trace__symbols__exit(struct trace *trace)
{
	machine__exit(trace->host);
	trace->host = NULL;

	symbol__exit();
}

static int syscall__alloc_arg_fmts(struct syscall *sc, int nr_args)
{
	int idx;

	if (nr_args == RAW_SYSCALL_ARGS_NUM && sc->fmt && sc->fmt->nr_args != 0)
		nr_args = sc->fmt->nr_args;

	sc->arg_fmt = calloc(nr_args, sizeof(*sc->arg_fmt));
	if (sc->arg_fmt == NULL)
		return -1;

	for (idx = 0; idx < nr_args; ++idx) {
		if (sc->fmt)
			sc->arg_fmt[idx] = sc->fmt->arg[idx];
	}

	sc->nr_args = nr_args;
	return 0;
}

static struct syscall_arg_fmt syscall_arg_fmts__by_name[] = {
	{ .name = "msr",	.scnprintf = SCA_X86_MSR,	  .strtoul = STUL_X86_MSR,	   },
	{ .name = "vector",	.scnprintf = SCA_X86_IRQ_VECTORS, .strtoul = STUL_X86_IRQ_VECTORS, },
};

static int syscall_arg_fmt__cmp(const void *name, const void *fmtp)
{
       const struct syscall_arg_fmt *fmt = fmtp;
       return strcmp(name, fmt->name);
}

static struct syscall_arg_fmt *
__syscall_arg_fmt__find_by_name(struct syscall_arg_fmt *fmts, const int nmemb, const char *name)
{
       return bsearch(name, fmts, nmemb, sizeof(struct syscall_arg_fmt), syscall_arg_fmt__cmp);
}

static struct syscall_arg_fmt *syscall_arg_fmt__find_by_name(const char *name)
{
       const int nmemb = ARRAY_SIZE(syscall_arg_fmts__by_name);
       return __syscall_arg_fmt__find_by_name(syscall_arg_fmts__by_name, nmemb, name);
}

static struct tep_format_field *
syscall_arg_fmt__init_array(struct syscall_arg_fmt *arg, struct tep_format_field *field)
{
	struct tep_format_field *last_field = NULL;
	int len;

	for (; field; field = field->next, ++arg) {
		last_field = field;

		if (arg->scnprintf)
			continue;

		len = strlen(field->name);

		if (strcmp(field->type, "const char *") == 0 &&
		    ((len >= 4 && strcmp(field->name + len - 4, "name") == 0) ||
		     strstr(field->name, "path") != NULL))
			arg->scnprintf = SCA_FILENAME;
		else if ((field->flags & TEP_FIELD_IS_POINTER) || strstr(field->name, "addr"))
			arg->scnprintf = SCA_PTR;
		else if (strcmp(field->type, "pid_t") == 0)
			arg->scnprintf = SCA_PID;
		else if (strcmp(field->type, "umode_t") == 0)
			arg->scnprintf = SCA_MODE_T;
		else if ((field->flags & TEP_FIELD_IS_ARRAY) && strstr(field->type, "char")) {
			arg->scnprintf = SCA_CHAR_ARRAY;
			arg->nr_entries = field->arraylen;
		} else if ((strcmp(field->type, "int") == 0 ||
			  strcmp(field->type, "unsigned int") == 0 ||
			  strcmp(field->type, "long") == 0) &&
			 len >= 2 && strcmp(field->name + len - 2, "fd") == 0) {
			/*
			 * /sys/kernel/tracing/events/syscalls/sys_enter*
			 * egrep 'field:.*fd;' .../format|sed -r 's/.*field:([a-z ]+) [a-z_]*fd.+/\1/g'|sort|uniq -c
			 * 65 int
			 * 23 unsigned int
			 * 7 unsigned long
			 */
			arg->scnprintf = SCA_FD;
               } else {
			struct syscall_arg_fmt *fmt = syscall_arg_fmt__find_by_name(field->name);

			if (fmt) {
				arg->scnprintf = fmt->scnprintf;
				arg->strtoul   = fmt->strtoul;
			}
		}
	}

	return last_field;
}

static int syscall__set_arg_fmts(struct syscall *sc)
{
	struct tep_format_field *last_field = syscall_arg_fmt__init_array(sc->arg_fmt, sc->args);

	if (last_field)
		sc->args_size = last_field->offset + last_field->size;

	return 0;
}

static int trace__read_syscall_info(struct trace *trace, int id)
{
	char tp_name[128];
	struct syscall *sc;
	const char *name = syscalltbl__name(trace->sctbl, id);

#ifdef HAVE_SYSCALL_TABLE_SUPPORT
	if (trace->syscalls.table == NULL) {
		trace->syscalls.table = calloc(trace->sctbl->syscalls.max_id + 1, sizeof(*sc));
		if (trace->syscalls.table == NULL)
			return -ENOMEM;
	}
#else
	if (id > trace->sctbl->syscalls.max_id || (id == 0 && trace->syscalls.table == NULL)) {
		// When using libaudit we don't know beforehand what is the max syscall id
		struct syscall *table = realloc(trace->syscalls.table, (id + 1) * sizeof(*sc));

		if (table == NULL)
			return -ENOMEM;

		// Need to memset from offset 0 and +1 members if brand new
		if (trace->syscalls.table == NULL)
			memset(table, 0, (id + 1) * sizeof(*sc));
		else
			memset(table + trace->sctbl->syscalls.max_id + 1, 0, (id - trace->sctbl->syscalls.max_id) * sizeof(*sc));

		trace->syscalls.table	      = table;
		trace->sctbl->syscalls.max_id = id;
	}
#endif
	sc = trace->syscalls.table + id;
	if (sc->nonexistent)
		return -EEXIST;

	if (name == NULL) {
		sc->nonexistent = true;
		return -EEXIST;
	}

	sc->name = name;
	sc->fmt  = syscall_fmt__find(sc->name);

	snprintf(tp_name, sizeof(tp_name), "sys_enter_%s", sc->name);
	sc->tp_format = trace_event__tp_format("syscalls", tp_name);

	if (IS_ERR(sc->tp_format) && sc->fmt && sc->fmt->alias) {
		snprintf(tp_name, sizeof(tp_name), "sys_enter_%s", sc->fmt->alias);
		sc->tp_format = trace_event__tp_format("syscalls", tp_name);
	}

	/*
	 * Fails to read trace point format via sysfs node, so the trace point
	 * doesn't exist.  Set the 'nonexistent' flag as true.
	 */
	if (IS_ERR(sc->tp_format)) {
		sc->nonexistent = true;
		return PTR_ERR(sc->tp_format);
	}

	if (syscall__alloc_arg_fmts(sc, IS_ERR(sc->tp_format) ?
					RAW_SYSCALL_ARGS_NUM : sc->tp_format->format.nr_fields))
		return -ENOMEM;

	sc->args = sc->tp_format->format.fields;
	/*
	 * We need to check and discard the first variable '__syscall_nr'
	 * or 'nr' that mean the syscall number. It is needless here.
	 * So drop '__syscall_nr' or 'nr' field but does not exist on older kernels.
	 */
	if (sc->args && (!strcmp(sc->args->name, "__syscall_nr") || !strcmp(sc->args->name, "nr"))) {
		sc->args = sc->args->next;
		--sc->nr_args;
	}

	sc->is_exit = !strcmp(name, "exit_group") || !strcmp(name, "exit");
	sc->is_open = !strcmp(name, "open") || !strcmp(name, "openat");

	return syscall__set_arg_fmts(sc);
}

static int evsel__init_tp_arg_scnprintf(struct evsel *evsel)
{
	struct syscall_arg_fmt *fmt = evsel__syscall_arg_fmt(evsel);

	if (fmt != NULL) {
		syscall_arg_fmt__init_array(fmt, evsel->tp_format->format.fields);
		return 0;
	}

	return -ENOMEM;
}

static int intcmp(const void *a, const void *b)
{
	const int *one = a, *another = b;

	return *one - *another;
}

static int trace__validate_ev_qualifier(struct trace *trace)
{
	int err = 0;
	bool printed_invalid_prefix = false;
	struct str_node *pos;
	size_t nr_used = 0, nr_allocated = strlist__nr_entries(trace->ev_qualifier);

	trace->ev_qualifier_ids.entries = malloc(nr_allocated *
						 sizeof(trace->ev_qualifier_ids.entries[0]));

	if (trace->ev_qualifier_ids.entries == NULL) {
		fputs("Error:\tNot enough memory for allocating events qualifier ids\n",
		       trace->output);
		err = -EINVAL;
		goto out;
	}

	strlist__for_each_entry(pos, trace->ev_qualifier) {
		const char *sc = pos->s;
		int id = syscalltbl__id(trace->sctbl, sc), match_next = -1;

		if (id < 0) {
			id = syscalltbl__strglobmatch_first(trace->sctbl, sc, &match_next);
			if (id >= 0)
				goto matches;

			if (!printed_invalid_prefix) {
				pr_debug("Skipping unknown syscalls: ");
				printed_invalid_prefix = true;
			} else {
				pr_debug(", ");
			}

			pr_debug("%s", sc);
			continue;
		}
matches:
		trace->ev_qualifier_ids.entries[nr_used++] = id;
		if (match_next == -1)
			continue;

		while (1) {
			id = syscalltbl__strglobmatch_next(trace->sctbl, sc, &match_next);
			if (id < 0)
				break;
			if (nr_allocated == nr_used) {
				void *entries;

				nr_allocated += 8;
				entries = realloc(trace->ev_qualifier_ids.entries,
						  nr_allocated * sizeof(trace->ev_qualifier_ids.entries[0]));
				if (entries == NULL) {
					err = -ENOMEM;
					fputs("\nError:\t Not enough memory for parsing\n", trace->output);
					goto out_free;
				}
				trace->ev_qualifier_ids.entries = entries;
			}
			trace->ev_qualifier_ids.entries[nr_used++] = id;
		}
	}

	trace->ev_qualifier_ids.nr = nr_used;
	qsort(trace->ev_qualifier_ids.entries, nr_used, sizeof(int), intcmp);
out:
	if (printed_invalid_prefix)
		pr_debug("\n");
	return err;
out_free:
	zfree(&trace->ev_qualifier_ids.entries);
	trace->ev_qualifier_ids.nr = 0;
	goto out;
}

static __maybe_unused bool trace__syscall_enabled(struct trace *trace, int id)
{
	bool in_ev_qualifier;

	if (trace->ev_qualifier_ids.nr == 0)
		return true;

	in_ev_qualifier = bsearch(&id, trace->ev_qualifier_ids.entries,
				  trace->ev_qualifier_ids.nr, sizeof(int), intcmp) != NULL;

	if (in_ev_qualifier)
	       return !trace->not_ev_qualifier;

	return trace->not_ev_qualifier;
}

/*
 * args is to be interpreted as a series of longs but we need to handle
 * 8-byte unaligned accesses. args points to raw_data within the event
 * and raw_data is guaranteed to be 8-byte unaligned because it is
 * preceded by raw_size which is a u32. So we need to copy args to a temp
 * variable to read it. Most notably this avoids extended load instructions
 * on unaligned addresses
 */
unsigned long syscall_arg__val(struct syscall_arg *arg, u8 idx)
{
	unsigned long val;
	unsigned char *p = arg->args + sizeof(unsigned long) * idx;

	memcpy(&val, p, sizeof(val));
	return val;
}

static size_t syscall__scnprintf_name(struct syscall *sc, char *bf, size_t size,
				      struct syscall_arg *arg)
{
	if (sc->arg_fmt && sc->arg_fmt[arg->idx].name)
		return scnprintf(bf, size, "%s: ", sc->arg_fmt[arg->idx].name);

	return scnprintf(bf, size, "arg%d: ", arg->idx);
}

/*
 * Check if the value is in fact zero, i.e. mask whatever needs masking, such
 * as mount 'flags' argument that needs ignoring some magic flag, see comment
 * in tools/perf/trace/beauty/mount_flags.c
 */
static unsigned long syscall_arg_fmt__mask_val(struct syscall_arg_fmt *fmt, struct syscall_arg *arg, unsigned long val)
{
	if (fmt && fmt->mask_val)
		return fmt->mask_val(arg, val);

	return val;
}

static size_t syscall_arg_fmt__scnprintf_val(struct syscall_arg_fmt *fmt, char *bf, size_t size,
					     struct syscall_arg *arg, unsigned long val)
{
	if (fmt && fmt->scnprintf) {
		arg->val = val;
		if (fmt->parm)
			arg->parm = fmt->parm;
		return fmt->scnprintf(bf, size, arg);
	}
	return scnprintf(bf, size, "%ld", val);
}

static size_t syscall__scnprintf_args(struct syscall *sc, char *bf, size_t size,
				      unsigned char *args, void *augmented_args, int augmented_args_size,
				      struct trace *trace, struct thread *thread)
{
	size_t printed = 0;
	unsigned long val;
	u8 bit = 1;
	struct syscall_arg arg = {
		.args	= args,
		.augmented = {
			.size = augmented_args_size,
			.args = augmented_args,
		},
		.idx	= 0,
		.mask	= 0,
		.trace  = trace,
		.thread = thread,
		.show_string_prefix = trace->show_string_prefix,
	};
	struct thread_trace *ttrace = thread__priv(thread);

	/*
	 * Things like fcntl will set this in its 'cmd' formatter to pick the
	 * right formatter for the return value (an fd? file flags?), which is
	 * not needed for syscalls that always return a given type, say an fd.
	 */
	ttrace->ret_scnprintf = NULL;

	if (sc->args != NULL) {
		struct tep_format_field *field;

		for (field = sc->args; field;
		     field = field->next, ++arg.idx, bit <<= 1) {
			if (arg.mask & bit)
				continue;

			arg.fmt = &sc->arg_fmt[arg.idx];
			val = syscall_arg__val(&arg, arg.idx);
			/*
			 * Some syscall args need some mask, most don't and
			 * return val untouched.
			 */
			val = syscall_arg_fmt__mask_val(&sc->arg_fmt[arg.idx], &arg, val);

			/*
 			 * Suppress this argument if its value is zero and
 			 * and we don't have a string associated in an
 			 * strarray for it.
 			 */
			if (val == 0 &&
			    !trace->show_zeros &&
			    !(sc->arg_fmt &&
			      (sc->arg_fmt[arg.idx].show_zero ||
			       sc->arg_fmt[arg.idx].scnprintf == SCA_STRARRAY ||
			       sc->arg_fmt[arg.idx].scnprintf == SCA_STRARRAYS) &&
			      sc->arg_fmt[arg.idx].parm))
				continue;

			printed += scnprintf(bf + printed, size - printed, "%s", printed ? ", " : "");

			if (trace->show_arg_names)
				printed += scnprintf(bf + printed, size - printed, "%s: ", field->name);

			printed += syscall_arg_fmt__scnprintf_val(&sc->arg_fmt[arg.idx],
								  bf + printed, size - printed, &arg, val);
		}
	} else if (IS_ERR(sc->tp_format)) {
		/*
		 * If we managed to read the tracepoint /format file, then we
		 * may end up not having any args, like with gettid(), so only
		 * print the raw args when we didn't manage to read it.
		 */
		while (arg.idx < sc->nr_args) {
			if (arg.mask & bit)
				goto next_arg;
			val = syscall_arg__val(&arg, arg.idx);
			if (printed)
				printed += scnprintf(bf + printed, size - printed, ", ");
			printed += syscall__scnprintf_name(sc, bf + printed, size - printed, &arg);
			printed += syscall_arg_fmt__scnprintf_val(&sc->arg_fmt[arg.idx], bf + printed, size - printed, &arg, val);
next_arg:
			++arg.idx;
			bit <<= 1;
		}
	}

	return printed;
}

typedef int (*tracepoint_handler)(struct trace *trace, struct evsel *evsel,
				  union perf_event *event,
				  struct perf_sample *sample);

static struct syscall *trace__syscall_info(struct trace *trace,
					   struct evsel *evsel, int id)
{
	int err = 0;

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
				id, evsel__name(evsel), ++n);
		}
		return NULL;
	}

	err = -EINVAL;

#ifdef HAVE_SYSCALL_TABLE_SUPPORT
	if (id > trace->sctbl->syscalls.max_id) {
#else
	if (id >= trace->sctbl->syscalls.max_id) {
		/*
		 * With libaudit we don't know beforehand what is the max_id,
		 * so we let trace__read_syscall_info() figure that out as we
		 * go on reading syscalls.
		 */
		err = trace__read_syscall_info(trace, id);
		if (err)
#endif
		goto out_cant_read;
	}

	if ((trace->syscalls.table == NULL || trace->syscalls.table[id].name == NULL) &&
	    (err = trace__read_syscall_info(trace, id)) != 0)
		goto out_cant_read;

	if (trace->syscalls.table && trace->syscalls.table[id].nonexistent)
		goto out_cant_read;

	return &trace->syscalls.table[id];

out_cant_read:
	if (verbose > 0) {
		char sbuf[STRERR_BUFSIZE];
		fprintf(trace->output, "Problems reading syscall %d: %d (%s)", id, -err, str_error_r(-err, sbuf, sizeof(sbuf)));
		if (id <= trace->sctbl->syscalls.max_id && trace->syscalls.table[id].name != NULL)
			fprintf(trace->output, "(%s)", trace->syscalls.table[id].name);
		fputs(" information\n", trace->output);
	}
	return NULL;
}

struct syscall_stats {
	struct stats stats;
	u64	     nr_failures;
	int	     max_errno;
	u32	     *errnos;
};

static void thread__update_stats(struct thread *thread, struct thread_trace *ttrace,
				 int id, struct perf_sample *sample, long err, bool errno_summary)
{
	struct int_node *inode;
	struct syscall_stats *stats;
	u64 duration = 0;

	inode = intlist__findnew(ttrace->syscall_stats, id);
	if (inode == NULL)
		return;

	stats = inode->priv;
	if (stats == NULL) {
		stats = zalloc(sizeof(*stats));
		if (stats == NULL)
			return;

		init_stats(&stats->stats);
		inode->priv = stats;
	}

	if (ttrace->entry_time && sample->time > ttrace->entry_time)
		duration = sample->time - ttrace->entry_time;

	update_stats(&stats->stats, duration);

	if (err < 0) {
		++stats->nr_failures;

		if (!errno_summary)
			return;

		err = -err;
		if (err > stats->max_errno) {
			u32 *new_errnos = realloc(stats->errnos, err * sizeof(u32));

			if (new_errnos) {
				memset(new_errnos + stats->max_errno, 0, (err - stats->max_errno) * sizeof(u32));
			} else {
				pr_debug("Not enough memory for errno stats for thread \"%s\"(%d/%d), results will be incomplete\n",
					 thread__comm_str(thread), thread->pid_, thread->tid);
				return;
			}

			stats->errnos = new_errnos;
			stats->max_errno = err;
		}

		++stats->errnos[err - 1];
	}
}

static int trace__printf_interrupted_entry(struct trace *trace)
{
	struct thread_trace *ttrace;
	size_t printed;
	int len;

	if (trace->failure_only || trace->current == NULL)
		return 0;

	ttrace = thread__priv(trace->current);

	if (!ttrace->entry_pending)
		return 0;

	printed  = trace__fprintf_entry_head(trace, trace->current, 0, false, ttrace->entry_time, trace->output);
	printed += len = fprintf(trace->output, "%s)", ttrace->entry_str);

	if (len < trace->args_alignment - 4)
		printed += fprintf(trace->output, "%-*s", trace->args_alignment - 4 - len, " ");

	printed += fprintf(trace->output, " ...\n");

	ttrace->entry_pending = false;
	++trace->nr_events_printed;

	return printed;
}

static int trace__fprintf_sample(struct trace *trace, struct evsel *evsel,
				 struct perf_sample *sample, struct thread *thread)
{
	int printed = 0;

	if (trace->print_sample) {
		double ts = (double)sample->time / NSEC_PER_MSEC;

		printed += fprintf(trace->output, "%22s %10.3f %s %d/%d [%d]\n",
				   evsel__name(evsel), ts,
				   thread__comm_str(thread),
				   sample->pid, sample->tid, sample->cpu);
	}

	return printed;
}

static void *syscall__augmented_args(struct syscall *sc, struct perf_sample *sample, int *augmented_args_size, int raw_augmented_args_size)
{
	void *augmented_args = NULL;
	/*
	 * For now with BPF raw_augmented we hook into raw_syscalls:sys_enter
	 * and there we get all 6 syscall args plus the tracepoint common fields
	 * that gets calculated at the start and the syscall_nr (another long).
	 * So we check if that is the case and if so don't look after the
	 * sc->args_size but always after the full raw_syscalls:sys_enter payload,
	 * which is fixed.
	 *
	 * We'll revisit this later to pass s->args_size to the BPF augmenter
	 * (now tools/perf/examples/bpf/augmented_raw_syscalls.c, so that it
	 * copies only what we need for each syscall, like what happens when we
	 * use syscalls:sys_enter_NAME, so that we reduce the kernel/userspace
	 * traffic to just what is needed for each syscall.
	 */
	int args_size = raw_augmented_args_size ?: sc->args_size;

	*augmented_args_size = sample->raw_size - args_size;
	if (*augmented_args_size > 0)
		augmented_args = sample->raw_data + args_size;

	return augmented_args;
}

static void syscall__exit(struct syscall *sc)
{
	if (!sc)
		return;

	free(sc->arg_fmt);
}

static int trace__sys_enter(struct trace *trace, struct evsel *evsel,
			    union perf_event *event __maybe_unused,
			    struct perf_sample *sample)
{
	char *msg;
	void *args;
	int printed = 0;
	struct thread *thread;
	int id = perf_evsel__sc_tp_uint(evsel, id, sample), err = -1;
	int augmented_args_size = 0;
	void *augmented_args = NULL;
	struct syscall *sc = trace__syscall_info(trace, evsel, id);
	struct thread_trace *ttrace;

	if (sc == NULL)
		return -1;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);
	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		goto out_put;

	trace__fprintf_sample(trace, evsel, sample, thread);

	args = perf_evsel__sc_tp_ptr(evsel, args, sample);

	if (ttrace->entry_str == NULL) {
		ttrace->entry_str = malloc(trace__entry_str_size);
		if (!ttrace->entry_str)
			goto out_put;
	}

	if (!(trace->duration_filter || trace->summary_only || trace->min_stack))
		trace__printf_interrupted_entry(trace);
	/*
	 * If this is raw_syscalls.sys_enter, then it always comes with the 6 possible
	 * arguments, even if the syscall being handled, say "openat", uses only 4 arguments
	 * this breaks syscall__augmented_args() check for augmented args, as we calculate
	 * syscall->args_size using each syscalls:sys_enter_NAME tracefs format file,
	 * so when handling, say the openat syscall, we end up getting 6 args for the
	 * raw_syscalls:sys_enter event, when we expected just 4, we end up mistakenly
	 * thinking that the extra 2 u64 args are the augmented filename, so just check
	 * here and avoid using augmented syscalls when the evsel is the raw_syscalls one.
	 */
	if (evsel != trace->syscalls.events.sys_enter)
		augmented_args = syscall__augmented_args(sc, sample, &augmented_args_size, trace->raw_augmented_syscalls_args_size);
	ttrace->entry_time = sample->time;
	msg = ttrace->entry_str;
	printed += scnprintf(msg + printed, trace__entry_str_size - printed, "%s(", sc->name);

	printed += syscall__scnprintf_args(sc, msg + printed, trace__entry_str_size - printed,
					   args, augmented_args, augmented_args_size, trace, thread);

	if (sc->is_exit) {
		if (!(trace->duration_filter || trace->summary_only || trace->failure_only || trace->min_stack)) {
			int alignment = 0;

			trace__fprintf_entry_head(trace, thread, 0, false, ttrace->entry_time, trace->output);
			printed = fprintf(trace->output, "%s)", ttrace->entry_str);
			if (trace->args_alignment > printed)
				alignment = trace->args_alignment - printed;
			fprintf(trace->output, "%*s= ?\n", alignment, " ");
		}
	} else {
		ttrace->entry_pending = true;
		/* See trace__vfs_getname & trace__sys_exit */
		ttrace->filename.pending_open = false;
	}

	if (trace->current != thread) {
		thread__put(trace->current);
		trace->current = thread__get(thread);
	}
	err = 0;
out_put:
	thread__put(thread);
	return err;
}

static int trace__fprintf_sys_enter(struct trace *trace, struct evsel *evsel,
				    struct perf_sample *sample)
{
	struct thread_trace *ttrace;
	struct thread *thread;
	int id = perf_evsel__sc_tp_uint(evsel, id, sample), err = -1;
	struct syscall *sc = trace__syscall_info(trace, evsel, id);
	char msg[1024];
	void *args, *augmented_args = NULL;
	int augmented_args_size;

	if (sc == NULL)
		return -1;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);
	ttrace = thread__trace(thread, trace->output);
	/*
	 * We need to get ttrace just to make sure it is there when syscall__scnprintf_args()
	 * and the rest of the beautifiers accessing it via struct syscall_arg touches it.
	 */
	if (ttrace == NULL)
		goto out_put;

	args = perf_evsel__sc_tp_ptr(evsel, args, sample);
	augmented_args = syscall__augmented_args(sc, sample, &augmented_args_size, trace->raw_augmented_syscalls_args_size);
	syscall__scnprintf_args(sc, msg, sizeof(msg), args, augmented_args, augmented_args_size, trace, thread);
	fprintf(trace->output, "%s", msg);
	err = 0;
out_put:
	thread__put(thread);
	return err;
}

static int trace__resolve_callchain(struct trace *trace, struct evsel *evsel,
				    struct perf_sample *sample,
				    struct callchain_cursor *cursor)
{
	struct addr_location al;
	int max_stack = evsel->core.attr.sample_max_stack ?
			evsel->core.attr.sample_max_stack :
			trace->max_stack;
	int err;

	if (machine__resolve(trace->host, &al, sample) < 0)
		return -1;

	err = thread__resolve_callchain(al.thread, cursor, evsel, sample, NULL, NULL, max_stack);
	addr_location__put(&al);
	return err;
}

static int trace__fprintf_callchain(struct trace *trace, struct perf_sample *sample)
{
	/* TODO: user-configurable print_opts */
	const unsigned int print_opts = EVSEL__PRINT_SYM |
				        EVSEL__PRINT_DSO |
				        EVSEL__PRINT_UNKNOWN_AS_ADDR;

	return sample__fprintf_callchain(sample, 38, print_opts, &callchain_cursor, symbol_conf.bt_stop_list, trace->output);
}

static const char *errno_to_name(struct evsel *evsel, int err)
{
	struct perf_env *env = evsel__env(evsel);
	const char *arch_name = perf_env__arch(env);

	return arch_syscalls__strerrno(arch_name, err);
}

static int trace__sys_exit(struct trace *trace, struct evsel *evsel,
			   union perf_event *event __maybe_unused,
			   struct perf_sample *sample)
{
	long ret;
	u64 duration = 0;
	bool duration_calculated = false;
	struct thread *thread;
	int id = perf_evsel__sc_tp_uint(evsel, id, sample), err = -1, callchain_ret = 0, printed = 0;
	int alignment = trace->args_alignment;
	struct syscall *sc = trace__syscall_info(trace, evsel, id);
	struct thread_trace *ttrace;

	if (sc == NULL)
		return -1;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);
	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		goto out_put;

	trace__fprintf_sample(trace, evsel, sample, thread);

	ret = perf_evsel__sc_tp_uint(evsel, ret, sample);

	if (trace->summary)
		thread__update_stats(thread, ttrace, id, sample, ret, trace->errno_summary);

	if (!trace->fd_path_disabled && sc->is_open && ret >= 0 && ttrace->filename.pending_open) {
		trace__set_fd_pathname(thread, ret, ttrace->filename.name);
		ttrace->filename.pending_open = false;
		++trace->stats.vfs_getname;
	}

	if (ttrace->entry_time) {
		duration = sample->time - ttrace->entry_time;
		if (trace__filter_duration(trace, duration))
			goto out;
		duration_calculated = true;
	} else if (trace->duration_filter)
		goto out;

	if (sample->callchain) {
		callchain_ret = trace__resolve_callchain(trace, evsel, sample, &callchain_cursor);
		if (callchain_ret == 0) {
			if (callchain_cursor.nr < trace->min_stack)
				goto out;
			callchain_ret = 1;
		}
	}

	if (trace->summary_only || (ret >= 0 && trace->failure_only))
		goto out;

	trace__fprintf_entry_head(trace, thread, duration, duration_calculated, ttrace->entry_time, trace->output);

	if (ttrace->entry_pending) {
		printed = fprintf(trace->output, "%s", ttrace->entry_str);
	} else {
		printed += fprintf(trace->output, " ... [");
		color_fprintf(trace->output, PERF_COLOR_YELLOW, "continued");
		printed += 9;
		printed += fprintf(trace->output, "]: %s()", sc->name);
	}

	printed++; /* the closing ')' */

	if (alignment > printed)
		alignment -= printed;
	else
		alignment = 0;

	fprintf(trace->output, ")%*s= ", alignment, " ");

	if (sc->fmt == NULL) {
		if (ret < 0)
			goto errno_print;
signed_print:
		fprintf(trace->output, "%ld", ret);
	} else if (ret < 0) {
errno_print: {
		char bf[STRERR_BUFSIZE];
		const char *emsg = str_error_r(-ret, bf, sizeof(bf)),
			   *e = errno_to_name(evsel, -ret);

		fprintf(trace->output, "-1 %s (%s)", e, emsg);
	}
	} else if (ret == 0 && sc->fmt->timeout)
		fprintf(trace->output, "0 (Timeout)");
	else if (ttrace->ret_scnprintf) {
		char bf[1024];
		struct syscall_arg arg = {
			.val	= ret,
			.thread	= thread,
			.trace	= trace,
		};
		ttrace->ret_scnprintf(bf, sizeof(bf), &arg);
		ttrace->ret_scnprintf = NULL;
		fprintf(trace->output, "%s", bf);
	} else if (sc->fmt->hexret)
		fprintf(trace->output, "%#lx", ret);
	else if (sc->fmt->errpid) {
		struct thread *child = machine__find_thread(trace->host, ret, ret);

		if (child != NULL) {
			fprintf(trace->output, "%ld", ret);
			if (child->comm_set)
				fprintf(trace->output, " (%s)", thread__comm_str(child));
			thread__put(child);
		}
	} else
		goto signed_print;

	fputc('\n', trace->output);

	/*
	 * We only consider an 'event' for the sake of --max-events a non-filtered
	 * sys_enter + sys_exit and other tracepoint events.
	 */
	if (++trace->nr_events_printed == trace->max_events && trace->max_events != ULONG_MAX)
		interrupted = true;

	if (callchain_ret > 0)
		trace__fprintf_callchain(trace, sample);
	else if (callchain_ret < 0)
		pr_err("Problem processing %s callchain, skipping...\n", evsel__name(evsel));
out:
	ttrace->entry_pending = false;
	err = 0;
out_put:
	thread__put(thread);
	return err;
}

static int trace__vfs_getname(struct trace *trace, struct evsel *evsel,
			      union perf_event *event __maybe_unused,
			      struct perf_sample *sample)
{
	struct thread *thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);
	struct thread_trace *ttrace;
	size_t filename_len, entry_str_len, to_move;
	ssize_t remaining_space;
	char *pos;
	const char *filename = evsel__rawptr(evsel, sample, "pathname");

	if (!thread)
		goto out;

	ttrace = thread__priv(thread);
	if (!ttrace)
		goto out_put;

	filename_len = strlen(filename);
	if (filename_len == 0)
		goto out_put;

	if (ttrace->filename.namelen < filename_len) {
		char *f = realloc(ttrace->filename.name, filename_len + 1);

		if (f == NULL)
			goto out_put;

		ttrace->filename.namelen = filename_len;
		ttrace->filename.name = f;
	}

	strcpy(ttrace->filename.name, filename);
	ttrace->filename.pending_open = true;

	if (!ttrace->filename.ptr)
		goto out_put;

	entry_str_len = strlen(ttrace->entry_str);
	remaining_space = trace__entry_str_size - entry_str_len - 1; /* \0 */
	if (remaining_space <= 0)
		goto out_put;

	if (filename_len > (size_t)remaining_space) {
		filename += filename_len - remaining_space;
		filename_len = remaining_space;
	}

	to_move = entry_str_len - ttrace->filename.entry_str_pos + 1; /* \0 */
	pos = ttrace->entry_str + ttrace->filename.entry_str_pos;
	memmove(pos + filename_len, pos, to_move);
	memcpy(pos, filename, filename_len);

	ttrace->filename.ptr = 0;
	ttrace->filename.entry_str_pos = 0;
out_put:
	thread__put(thread);
out:
	return 0;
}

static int trace__sched_stat_runtime(struct trace *trace, struct evsel *evsel,
				     union perf_event *event __maybe_unused,
				     struct perf_sample *sample)
{
        u64 runtime = evsel__intval(evsel, sample, "runtime");
	double runtime_ms = (double)runtime / NSEC_PER_MSEC;
	struct thread *thread = machine__findnew_thread(trace->host,
							sample->pid,
							sample->tid);
	struct thread_trace *ttrace = thread__trace(thread, trace->output);

	if (ttrace == NULL)
		goto out_dump;

	ttrace->runtime_ms += runtime_ms;
	trace->runtime_ms += runtime_ms;
out_put:
	thread__put(thread);
	return 0;

out_dump:
	fprintf(trace->output, "%s: comm=%s,pid=%u,runtime=%" PRIu64 ",vruntime=%" PRIu64 ")\n",
	       evsel->name,
	       evsel__strval(evsel, sample, "comm"),
	       (pid_t)evsel__intval(evsel, sample, "pid"),
	       runtime,
	       evsel__intval(evsel, sample, "vruntime"));
	goto out_put;
}

static int bpf_output__printer(enum binary_printer_ops op,
			       unsigned int val, void *extra __maybe_unused, FILE *fp)
{
	unsigned char ch = (unsigned char)val;

	switch (op) {
	case BINARY_PRINT_CHAR_DATA:
		return fprintf(fp, "%c", isprint(ch) ? ch : '.');
	case BINARY_PRINT_DATA_BEGIN:
	case BINARY_PRINT_LINE_BEGIN:
	case BINARY_PRINT_ADDR:
	case BINARY_PRINT_NUM_DATA:
	case BINARY_PRINT_NUM_PAD:
	case BINARY_PRINT_SEP:
	case BINARY_PRINT_CHAR_PAD:
	case BINARY_PRINT_LINE_END:
	case BINARY_PRINT_DATA_END:
	default:
		break;
	}

	return 0;
}

static void bpf_output__fprintf(struct trace *trace,
				struct perf_sample *sample)
{
	binary__fprintf(sample->raw_data, sample->raw_size, 8,
			bpf_output__printer, NULL, trace->output);
	++trace->nr_events_printed;
}

static size_t trace__fprintf_tp_fields(struct trace *trace, struct evsel *evsel, struct perf_sample *sample,
				       struct thread *thread, void *augmented_args, int augmented_args_size)
{
	char bf[2048];
	size_t size = sizeof(bf);
	struct tep_format_field *field = evsel->tp_format->format.fields;
	struct syscall_arg_fmt *arg = __evsel__syscall_arg_fmt(evsel);
	size_t printed = 0;
	unsigned long val;
	u8 bit = 1;
	struct syscall_arg syscall_arg = {
		.augmented = {
			.size = augmented_args_size,
			.args = augmented_args,
		},
		.idx	= 0,
		.mask	= 0,
		.trace  = trace,
		.thread = thread,
		.show_string_prefix = trace->show_string_prefix,
	};

	for (; field && arg; field = field->next, ++syscall_arg.idx, bit <<= 1, ++arg) {
		if (syscall_arg.mask & bit)
			continue;

		syscall_arg.len = 0;
		syscall_arg.fmt = arg;
		if (field->flags & TEP_FIELD_IS_ARRAY) {
			int offset = field->offset;

			if (field->flags & TEP_FIELD_IS_DYNAMIC) {
				offset = format_field__intval(field, sample, evsel->needs_swap);
				syscall_arg.len = offset >> 16;
				offset &= 0xffff;
				if (field->flags & TEP_FIELD_IS_RELATIVE)
					offset += field->offset + field->size;
			}

			val = (uintptr_t)(sample->raw_data + offset);
		} else
			val = format_field__intval(field, sample, evsel->needs_swap);
		/*
		 * Some syscall args need some mask, most don't and
		 * return val untouched.
		 */
		val = syscall_arg_fmt__mask_val(arg, &syscall_arg, val);

		/*
		 * Suppress this argument if its value is zero and
		 * we don't have a string associated in an
		 * strarray for it.
		 */
		if (val == 0 &&
		    !trace->show_zeros &&
		    !((arg->show_zero ||
		       arg->scnprintf == SCA_STRARRAY ||
		       arg->scnprintf == SCA_STRARRAYS) &&
		      arg->parm))
			continue;

		printed += scnprintf(bf + printed, size - printed, "%s", printed ? ", " : "");

		if (trace->show_arg_names)
			printed += scnprintf(bf + printed, size - printed, "%s: ", field->name);

		printed += syscall_arg_fmt__scnprintf_val(arg, bf + printed, size - printed, &syscall_arg, val);
	}

	return printed + fprintf(trace->output, "%s", bf);
}

static int trace__event_handler(struct trace *trace, struct evsel *evsel,
				union perf_event *event __maybe_unused,
				struct perf_sample *sample)
{
	struct thread *thread;
	int callchain_ret = 0;
	/*
	 * Check if we called perf_evsel__disable(evsel) due to, for instance,
	 * this event's max_events having been hit and this is an entry coming
	 * from the ring buffer that we should discard, since the max events
	 * have already been considered/printed.
	 */
	if (evsel->disabled)
		return 0;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);

	if (sample->callchain) {
		callchain_ret = trace__resolve_callchain(trace, evsel, sample, &callchain_cursor);
		if (callchain_ret == 0) {
			if (callchain_cursor.nr < trace->min_stack)
				goto out;
			callchain_ret = 1;
		}
	}

	trace__printf_interrupted_entry(trace);
	trace__fprintf_tstamp(trace, sample->time, trace->output);

	if (trace->trace_syscalls && trace->show_duration)
		fprintf(trace->output, "(         ): ");

	if (thread)
		trace__fprintf_comm_tid(trace, thread, trace->output);

	if (evsel == trace->syscalls.events.augmented) {
		int id = perf_evsel__sc_tp_uint(evsel, id, sample);
		struct syscall *sc = trace__syscall_info(trace, evsel, id);

		if (sc) {
			fprintf(trace->output, "%s(", sc->name);
			trace__fprintf_sys_enter(trace, evsel, sample);
			fputc(')', trace->output);
			goto newline;
		}

		/*
		 * XXX: Not having the associated syscall info or not finding/adding
		 * 	the thread should never happen, but if it does...
		 * 	fall thru and print it as a bpf_output event.
		 */
	}

	fprintf(trace->output, "%s(", evsel->name);

	if (evsel__is_bpf_output(evsel)) {
		bpf_output__fprintf(trace, sample);
	} else if (evsel->tp_format) {
		if (strncmp(evsel->tp_format->name, "sys_enter_", 10) ||
		    trace__fprintf_sys_enter(trace, evsel, sample)) {
			if (trace->libtraceevent_print) {
				event_format__fprintf(evsel->tp_format, sample->cpu,
						      sample->raw_data, sample->raw_size,
						      trace->output);
			} else {
				trace__fprintf_tp_fields(trace, evsel, sample, thread, NULL, 0);
			}
		}
	}

newline:
	fprintf(trace->output, ")\n");

	if (callchain_ret > 0)
		trace__fprintf_callchain(trace, sample);
	else if (callchain_ret < 0)
		pr_err("Problem processing %s callchain, skipping...\n", evsel__name(evsel));

	++trace->nr_events_printed;

	if (evsel->max_events != ULONG_MAX && ++evsel->nr_events_printed == evsel->max_events) {
		evsel__disable(evsel);
		evsel__close(evsel);
	}
out:
	thread__put(thread);
	return 0;
}

static void print_location(FILE *f, struct perf_sample *sample,
			   struct addr_location *al,
			   bool print_dso, bool print_sym)
{

	if ((verbose > 0 || print_dso) && al->map)
		fprintf(f, "%s@", al->map->dso->long_name);

	if ((verbose > 0 || print_sym) && al->sym)
		fprintf(f, "%s+0x%" PRIx64, al->sym->name,
			al->addr - al->sym->start);
	else if (al->map)
		fprintf(f, "0x%" PRIx64, al->addr);
	else
		fprintf(f, "0x%" PRIx64, sample->addr);
}

static int trace__pgfault(struct trace *trace,
			  struct evsel *evsel,
			  union perf_event *event __maybe_unused,
			  struct perf_sample *sample)
{
	struct thread *thread;
	struct addr_location al;
	char map_type = 'd';
	struct thread_trace *ttrace;
	int err = -1;
	int callchain_ret = 0;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);

	if (sample->callchain) {
		callchain_ret = trace__resolve_callchain(trace, evsel, sample, &callchain_cursor);
		if (callchain_ret == 0) {
			if (callchain_cursor.nr < trace->min_stack)
				goto out_put;
			callchain_ret = 1;
		}
	}

	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		goto out_put;

	if (evsel->core.attr.config == PERF_COUNT_SW_PAGE_FAULTS_MAJ)
		ttrace->pfmaj++;
	else
		ttrace->pfmin++;

	if (trace->summary_only)
		goto out;

	thread__find_symbol(thread, sample->cpumode, sample->ip, &al);

	trace__fprintf_entry_head(trace, thread, 0, true, sample->time, trace->output);

	fprintf(trace->output, "%sfault [",
		evsel->core.attr.config == PERF_COUNT_SW_PAGE_FAULTS_MAJ ?
		"maj" : "min");

	print_location(trace->output, sample, &al, false, true);

	fprintf(trace->output, "] => ");

	thread__find_symbol(thread, sample->cpumode, sample->addr, &al);

	if (!al.map) {
		thread__find_symbol(thread, sample->cpumode, sample->addr, &al);

		if (al.map)
			map_type = 'x';
		else
			map_type = '?';
	}

	print_location(trace->output, sample, &al, true, false);

	fprintf(trace->output, " (%c%c)\n", map_type, al.level);

	if (callchain_ret > 0)
		trace__fprintf_callchain(trace, sample);
	else if (callchain_ret < 0)
		pr_err("Problem processing %s callchain, skipping...\n", evsel__name(evsel));

	++trace->nr_events_printed;
out:
	err = 0;
out_put:
	thread__put(thread);
	return err;
}

static void trace__set_base_time(struct trace *trace,
				 struct evsel *evsel,
				 struct perf_sample *sample)
{
	/*
	 * BPF events were not setting PERF_SAMPLE_TIME, so be more robust
	 * and don't use sample->time unconditionally, we may end up having
	 * some other event in the future without PERF_SAMPLE_TIME for good
	 * reason, i.e. we may not be interested in its timestamps, just in
	 * it taking place, picking some piece of information when it
	 * appears in our event stream (vfs_getname comes to mind).
	 */
	if (trace->base_time == 0 && !trace->full_time &&
	    (evsel->core.attr.sample_type & PERF_SAMPLE_TIME))
		trace->base_time = sample->time;
}

static int trace__process_sample(struct perf_tool *tool,
				 union perf_event *event,
				 struct perf_sample *sample,
				 struct evsel *evsel,
				 struct machine *machine __maybe_unused)
{
	struct trace *trace = container_of(tool, struct trace, tool);
	struct thread *thread;
	int err = 0;

	tracepoint_handler handler = evsel->handler;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);
	if (thread && thread__is_filtered(thread))
		goto out;

	trace__set_base_time(trace, evsel, sample);

	if (handler) {
		++trace->nr_events;
		handler(trace, evsel, event, sample);
	}
out:
	thread__put(thread);
	return err;
}

static int trace__record(struct trace *trace, int argc, const char **argv)
{
	unsigned int rec_argc, i, j;
	const char **rec_argv;
	const char * const record_args[] = {
		"record",
		"-R",
		"-m", "1024",
		"-c", "1",
	};
	pid_t pid = getpid();
	char *filter = asprintf__tp_filter_pids(1, &pid);
	const char * const sc_args[] = { "-e", };
	unsigned int sc_args_nr = ARRAY_SIZE(sc_args);
	const char * const majpf_args[] = { "-e", "major-faults" };
	unsigned int majpf_args_nr = ARRAY_SIZE(majpf_args);
	const char * const minpf_args[] = { "-e", "minor-faults" };
	unsigned int minpf_args_nr = ARRAY_SIZE(minpf_args);
	int err = -1;

	/* +3 is for the event string below and the pid filter */
	rec_argc = ARRAY_SIZE(record_args) + sc_args_nr + 3 +
		majpf_args_nr + minpf_args_nr + argc;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL || filter == NULL)
		goto out_free;

	j = 0;
	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[j++] = record_args[i];

	if (trace->trace_syscalls) {
		for (i = 0; i < sc_args_nr; i++)
			rec_argv[j++] = sc_args[i];

		/* event string may be different for older kernels - e.g., RHEL6 */
		if (is_valid_tracepoint("raw_syscalls:sys_enter"))
			rec_argv[j++] = "raw_syscalls:sys_enter,raw_syscalls:sys_exit";
		else if (is_valid_tracepoint("syscalls:sys_enter"))
			rec_argv[j++] = "syscalls:sys_enter,syscalls:sys_exit";
		else {
			pr_err("Neither raw_syscalls nor syscalls events exist.\n");
			goto out_free;
		}
	}

	rec_argv[j++] = "--filter";
	rec_argv[j++] = filter;

	if (trace->trace_pgfaults & TRACE_PFMAJ)
		for (i = 0; i < majpf_args_nr; i++)
			rec_argv[j++] = majpf_args[i];

	if (trace->trace_pgfaults & TRACE_PFMIN)
		for (i = 0; i < minpf_args_nr; i++)
			rec_argv[j++] = minpf_args[i];

	for (i = 0; i < (unsigned int)argc; i++)
		rec_argv[j++] = argv[i];

	err = cmd_record(j, rec_argv);
out_free:
	free(filter);
	free(rec_argv);
	return err;
}

static size_t trace__fprintf_thread_summary(struct trace *trace, FILE *fp);

static bool evlist__add_vfs_getname(struct evlist *evlist)
{
	bool found = false;
	struct evsel *evsel, *tmp;
	struct parse_events_error err;
	int ret;

	parse_events_error__init(&err);
	ret = parse_events(evlist, "probe:vfs_getname*", &err);
	parse_events_error__exit(&err);
	if (ret)
		return false;

	evlist__for_each_entry_safe(evlist, evsel, tmp) {
		if (!strstarts(evsel__name(evsel), "probe:vfs_getname"))
			continue;

		if (evsel__field(evsel, "pathname")) {
			evsel->handler = trace__vfs_getname;
			found = true;
			continue;
		}

		list_del_init(&evsel->core.node);
		evsel->evlist = NULL;
		evsel__delete(evsel);
	}

	return found;
}

static struct evsel *evsel__new_pgfault(u64 config)
{
	struct evsel *evsel;
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.mmap_data = 1,
	};

	attr.config = config;
	attr.sample_period = 1;

	event_attr_init(&attr);

	evsel = evsel__new(&attr);
	if (evsel)
		evsel->handler = trace__pgfault;

	return evsel;
}

static void evlist__free_syscall_tp_fields(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		struct evsel_trace *et = evsel->priv;

		if (!et || !evsel->tp_format || strcmp(evsel->tp_format->system, "syscalls"))
			continue;

		free(et->fmt);
		free(et);
	}
}

static void trace__handle_event(struct trace *trace, union perf_event *event, struct perf_sample *sample)
{
	const u32 type = event->header.type;
	struct evsel *evsel;

	if (type != PERF_RECORD_SAMPLE) {
		trace__process_event(trace, trace->host, event, sample);
		return;
	}

	evsel = evlist__id2evsel(trace->evlist, sample->id);
	if (evsel == NULL) {
		fprintf(trace->output, "Unknown tp ID %" PRIu64 ", skipping...\n", sample->id);
		return;
	}

	if (evswitch__discard(&trace->evswitch, evsel))
		return;

	trace__set_base_time(trace, evsel, sample);

	if (evsel->core.attr.type == PERF_TYPE_TRACEPOINT &&
	    sample->raw_data == NULL) {
		fprintf(trace->output, "%s sample with no payload for tid: %d, cpu %d, raw_size=%d, skipping...\n",
		       evsel__name(evsel), sample->tid,
		       sample->cpu, sample->raw_size);
	} else {
		tracepoint_handler handler = evsel->handler;
		handler(trace, evsel, event, sample);
	}

	if (trace->nr_events_printed >= trace->max_events && trace->max_events != ULONG_MAX)
		interrupted = true;
}

static int trace__add_syscall_newtp(struct trace *trace)
{
	int ret = -1;
	struct evlist *evlist = trace->evlist;
	struct evsel *sys_enter, *sys_exit;

	sys_enter = perf_evsel__raw_syscall_newtp("sys_enter", trace__sys_enter);
	if (sys_enter == NULL)
		goto out;

	if (perf_evsel__init_sc_tp_ptr_field(sys_enter, args))
		goto out_delete_sys_enter;

	sys_exit = perf_evsel__raw_syscall_newtp("sys_exit", trace__sys_exit);
	if (sys_exit == NULL)
		goto out_delete_sys_enter;

	if (perf_evsel__init_sc_tp_uint_field(sys_exit, ret))
		goto out_delete_sys_exit;

	evsel__config_callchain(sys_enter, &trace->opts, &callchain_param);
	evsel__config_callchain(sys_exit, &trace->opts, &callchain_param);

	evlist__add(evlist, sys_enter);
	evlist__add(evlist, sys_exit);

	if (callchain_param.enabled && !trace->kernel_syscallchains) {
		/*
		 * We're interested only in the user space callchain
		 * leading to the syscall, allow overriding that for
		 * debugging reasons using --kernel_syscall_callchains
		 */
		sys_exit->core.attr.exclude_callchain_kernel = 1;
	}

	trace->syscalls.events.sys_enter = sys_enter;
	trace->syscalls.events.sys_exit  = sys_exit;

	ret = 0;
out:
	return ret;

out_delete_sys_exit:
	evsel__delete_priv(sys_exit);
out_delete_sys_enter:
	evsel__delete_priv(sys_enter);
	goto out;
}

static int trace__set_ev_qualifier_tp_filter(struct trace *trace)
{
	int err = -1;
	struct evsel *sys_exit;
	char *filter = asprintf_expr_inout_ints("id", !trace->not_ev_qualifier,
						trace->ev_qualifier_ids.nr,
						trace->ev_qualifier_ids.entries);

	if (filter == NULL)
		goto out_enomem;

	if (!evsel__append_tp_filter(trace->syscalls.events.sys_enter, filter)) {
		sys_exit = trace->syscalls.events.sys_exit;
		err = evsel__append_tp_filter(sys_exit, filter);
	}

	free(filter);
out:
	return err;
out_enomem:
	errno = ENOMEM;
	goto out;
}

#ifdef HAVE_LIBBPF_SUPPORT
static struct bpf_map *trace__find_bpf_map_by_name(struct trace *trace, const char *name)
{
	if (trace->bpf_obj == NULL)
		return NULL;

	return bpf_object__find_map_by_name(trace->bpf_obj, name);
}

static void trace__set_bpf_map_filtered_pids(struct trace *trace)
{
	trace->filter_pids.map = trace__find_bpf_map_by_name(trace, "pids_filtered");
}

static void trace__set_bpf_map_syscalls(struct trace *trace)
{
	trace->syscalls.map = trace__find_bpf_map_by_name(trace, "syscalls");
	trace->syscalls.prog_array.sys_enter = trace__find_bpf_map_by_name(trace, "syscalls_sys_enter");
	trace->syscalls.prog_array.sys_exit  = trace__find_bpf_map_by_name(trace, "syscalls_sys_exit");
}

static struct bpf_program *trace__find_bpf_program_by_title(struct trace *trace, const char *name)
{
	struct bpf_program *pos, *prog = NULL;
	const char *sec_name;

	if (trace->bpf_obj == NULL)
		return NULL;

	bpf_object__for_each_program(pos, trace->bpf_obj) {
		sec_name = bpf_program__section_name(pos);
		if (sec_name && !strcmp(sec_name, name)) {
			prog = pos;
			break;
		}
	}

	return prog;
}

static struct bpf_program *trace__find_syscall_bpf_prog(struct trace *trace, struct syscall *sc,
							const char *prog_name, const char *type)
{
	struct bpf_program *prog;

	if (prog_name == NULL) {
		char default_prog_name[256];
		scnprintf(default_prog_name, sizeof(default_prog_name), "!syscalls:sys_%s_%s", type, sc->name);
		prog = trace__find_bpf_program_by_title(trace, default_prog_name);
		if (prog != NULL)
			goto out_found;
		if (sc->fmt && sc->fmt->alias) {
			scnprintf(default_prog_name, sizeof(default_prog_name), "!syscalls:sys_%s_%s", type, sc->fmt->alias);
			prog = trace__find_bpf_program_by_title(trace, default_prog_name);
			if (prog != NULL)
				goto out_found;
		}
		goto out_unaugmented;
	}

	prog = trace__find_bpf_program_by_title(trace, prog_name);

	if (prog != NULL) {
out_found:
		return prog;
	}

	pr_debug("Couldn't find BPF prog \"%s\" to associate with syscalls:sys_%s_%s, not augmenting it\n",
		 prog_name, type, sc->name);
out_unaugmented:
	return trace->syscalls.unaugmented_prog;
}

static void trace__init_syscall_bpf_progs(struct trace *trace, int id)
{
	struct syscall *sc = trace__syscall_info(trace, NULL, id);

	if (sc == NULL)
		return;

	sc->bpf_prog.sys_enter = trace__find_syscall_bpf_prog(trace, sc, sc->fmt ? sc->fmt->bpf_prog_name.sys_enter : NULL, "enter");
	sc->bpf_prog.sys_exit  = trace__find_syscall_bpf_prog(trace, sc, sc->fmt ? sc->fmt->bpf_prog_name.sys_exit  : NULL,  "exit");
}

static int trace__bpf_prog_sys_enter_fd(struct trace *trace, int id)
{
	struct syscall *sc = trace__syscall_info(trace, NULL, id);
	return sc ? bpf_program__fd(sc->bpf_prog.sys_enter) : bpf_program__fd(trace->syscalls.unaugmented_prog);
}

static int trace__bpf_prog_sys_exit_fd(struct trace *trace, int id)
{
	struct syscall *sc = trace__syscall_info(trace, NULL, id);
	return sc ? bpf_program__fd(sc->bpf_prog.sys_exit) : bpf_program__fd(trace->syscalls.unaugmented_prog);
}

static void trace__init_bpf_map_syscall_args(struct trace *trace, int id, struct bpf_map_syscall_entry *entry)
{
	struct syscall *sc = trace__syscall_info(trace, NULL, id);
	int arg = 0;

	if (sc == NULL)
		goto out;

	for (; arg < sc->nr_args; ++arg) {
		entry->string_args_len[arg] = 0;
		if (sc->arg_fmt[arg].scnprintf == SCA_FILENAME) {
			/* Should be set like strace -s strsize */
			entry->string_args_len[arg] = PATH_MAX;
		}
	}
out:
	for (; arg < 6; ++arg)
		entry->string_args_len[arg] = 0;
}
static int trace__set_ev_qualifier_bpf_filter(struct trace *trace)
{
	int fd = bpf_map__fd(trace->syscalls.map);
	struct bpf_map_syscall_entry value = {
		.enabled = !trace->not_ev_qualifier,
	};
	int err = 0;
	size_t i;

	for (i = 0; i < trace->ev_qualifier_ids.nr; ++i) {
		int key = trace->ev_qualifier_ids.entries[i];

		if (value.enabled) {
			trace__init_bpf_map_syscall_args(trace, key, &value);
			trace__init_syscall_bpf_progs(trace, key);
		}

		err = bpf_map_update_elem(fd, &key, &value, BPF_EXIST);
		if (err)
			break;
	}

	return err;
}

static int __trace__init_syscalls_bpf_map(struct trace *trace, bool enabled)
{
	int fd = bpf_map__fd(trace->syscalls.map);
	struct bpf_map_syscall_entry value = {
		.enabled = enabled,
	};
	int err = 0, key;

	for (key = 0; key < trace->sctbl->syscalls.nr_entries; ++key) {
		if (enabled)
			trace__init_bpf_map_syscall_args(trace, key, &value);

		err = bpf_map_update_elem(fd, &key, &value, BPF_ANY);
		if (err)
			break;
	}

	return err;
}

static int trace__init_syscalls_bpf_map(struct trace *trace)
{
	bool enabled = true;

	if (trace->ev_qualifier_ids.nr)
		enabled = trace->not_ev_qualifier;

	return __trace__init_syscalls_bpf_map(trace, enabled);
}

static struct bpf_program *trace__find_usable_bpf_prog_entry(struct trace *trace, struct syscall *sc)
{
	struct tep_format_field *field, *candidate_field;
	int id;

	/*
	 * We're only interested in syscalls that have a pointer:
	 */
	for (field = sc->args; field; field = field->next) {
		if (field->flags & TEP_FIELD_IS_POINTER)
			goto try_to_find_pair;
	}

	return NULL;

try_to_find_pair:
	for (id = 0; id < trace->sctbl->syscalls.nr_entries; ++id) {
		struct syscall *pair = trace__syscall_info(trace, NULL, id);
		struct bpf_program *pair_prog;
		bool is_candidate = false;

		if (pair == NULL || pair == sc ||
		    pair->bpf_prog.sys_enter == trace->syscalls.unaugmented_prog)
			continue;

		for (field = sc->args, candidate_field = pair->args;
		     field && candidate_field; field = field->next, candidate_field = candidate_field->next) {
			bool is_pointer = field->flags & TEP_FIELD_IS_POINTER,
			     candidate_is_pointer = candidate_field->flags & TEP_FIELD_IS_POINTER;

			if (is_pointer) {
			       if (!candidate_is_pointer) {
					// The candidate just doesn't copies our pointer arg, might copy other pointers we want.
					continue;
			       }
			} else {
				if (candidate_is_pointer) {
					// The candidate might copy a pointer we don't have, skip it.
					goto next_candidate;
				}
				continue;
			}

			if (strcmp(field->type, candidate_field->type))
				goto next_candidate;

			is_candidate = true;
		}

		if (!is_candidate)
			goto next_candidate;

		/*
		 * Check if the tentative pair syscall augmenter has more pointers, if it has,
		 * then it may be collecting that and we then can't use it, as it would collect
		 * more than what is common to the two syscalls.
		 */
		if (candidate_field) {
			for (candidate_field = candidate_field->next; candidate_field; candidate_field = candidate_field->next)
				if (candidate_field->flags & TEP_FIELD_IS_POINTER)
					goto next_candidate;
		}

		pair_prog = pair->bpf_prog.sys_enter;
		/*
		 * If the pair isn't enabled, then its bpf_prog.sys_enter will not
		 * have been searched for, so search it here and if it returns the
		 * unaugmented one, then ignore it, otherwise we'll reuse that BPF
		 * program for a filtered syscall on a non-filtered one.
		 *
		 * For instance, we have "!syscalls:sys_enter_renameat" and that is
		 * useful for "renameat2".
		 */
		if (pair_prog == NULL) {
			pair_prog = trace__find_syscall_bpf_prog(trace, pair, pair->fmt ? pair->fmt->bpf_prog_name.sys_enter : NULL, "enter");
			if (pair_prog == trace->syscalls.unaugmented_prog)
				goto next_candidate;
		}

		pr_debug("Reusing \"%s\" BPF sys_enter augmenter for \"%s\"\n", pair->name, sc->name);
		return pair_prog;
	next_candidate:
		continue;
	}

	return NULL;
}

static int trace__init_syscalls_bpf_prog_array_maps(struct trace *trace)
{
	int map_enter_fd = bpf_map__fd(trace->syscalls.prog_array.sys_enter),
	    map_exit_fd  = bpf_map__fd(trace->syscalls.prog_array.sys_exit);
	int err = 0, key;

	for (key = 0; key < trace->sctbl->syscalls.nr_entries; ++key) {
		int prog_fd;

		if (!trace__syscall_enabled(trace, key))
			continue;

		trace__init_syscall_bpf_progs(trace, key);

		// It'll get at least the "!raw_syscalls:unaugmented"
		prog_fd = trace__bpf_prog_sys_enter_fd(trace, key);
		err = bpf_map_update_elem(map_enter_fd, &key, &prog_fd, BPF_ANY);
		if (err)
			break;
		prog_fd = trace__bpf_prog_sys_exit_fd(trace, key);
		err = bpf_map_update_elem(map_exit_fd, &key, &prog_fd, BPF_ANY);
		if (err)
			break;
	}

	/*
	 * Now lets do a second pass looking for enabled syscalls without
	 * an augmenter that have a signature that is a superset of another
	 * syscall with an augmenter so that we can auto-reuse it.
	 *
	 * I.e. if we have an augmenter for the "open" syscall that has
	 * this signature:
	 *
	 *   int open(const char *pathname, int flags, mode_t mode);
	 *
	 * I.e. that will collect just the first string argument, then we
	 * can reuse it for the 'creat' syscall, that has this signature:
	 *
	 *   int creat(const char *pathname, mode_t mode);
	 *
	 * and for:
	 *
	 *   int stat(const char *pathname, struct stat *statbuf);
	 *   int lstat(const char *pathname, struct stat *statbuf);
	 *
	 * Because the 'open' augmenter will collect the first arg as a string,
	 * and leave alone all the other args, which already helps with
	 * beautifying 'stat' and 'lstat''s pathname arg.
	 *
	 * Then, in time, when 'stat' gets an augmenter that collects both
	 * first and second arg (this one on the raw_syscalls:sys_exit prog
	 * array tail call, then that one will be used.
	 */
	for (key = 0; key < trace->sctbl->syscalls.nr_entries; ++key) {
		struct syscall *sc = trace__syscall_info(trace, NULL, key);
		struct bpf_program *pair_prog;
		int prog_fd;

		if (sc == NULL || sc->bpf_prog.sys_enter == NULL)
			continue;

		/*
		 * For now we're just reusing the sys_enter prog, and if it
		 * already has an augmenter, we don't need to find one.
		 */
		if (sc->bpf_prog.sys_enter != trace->syscalls.unaugmented_prog)
			continue;

		/*
		 * Look at all the other syscalls for one that has a signature
		 * that is close enough that we can share:
		 */
		pair_prog = trace__find_usable_bpf_prog_entry(trace, sc);
		if (pair_prog == NULL)
			continue;

		sc->bpf_prog.sys_enter = pair_prog;

		/*
		 * Update the BPF_MAP_TYPE_PROG_SHARED for raw_syscalls:sys_enter
		 * with the fd for the program we're reusing:
		 */
		prog_fd = bpf_program__fd(sc->bpf_prog.sys_enter);
		err = bpf_map_update_elem(map_enter_fd, &key, &prog_fd, BPF_ANY);
		if (err)
			break;
	}


	return err;
}

static void trace__delete_augmented_syscalls(struct trace *trace)
{
	struct evsel *evsel, *tmp;

	evlist__remove(trace->evlist, trace->syscalls.events.augmented);
	evsel__delete(trace->syscalls.events.augmented);
	trace->syscalls.events.augmented = NULL;

	evlist__for_each_entry_safe(trace->evlist, tmp, evsel) {
		if (evsel->bpf_obj == trace->bpf_obj) {
			evlist__remove(trace->evlist, evsel);
			evsel__delete(evsel);
		}

	}

	bpf_object__close(trace->bpf_obj);
	trace->bpf_obj = NULL;
}
#else // HAVE_LIBBPF_SUPPORT
static struct bpf_map *trace__find_bpf_map_by_name(struct trace *trace __maybe_unused,
						   const char *name __maybe_unused)
{
	return NULL;
}

static void trace__set_bpf_map_filtered_pids(struct trace *trace __maybe_unused)
{
}

static void trace__set_bpf_map_syscalls(struct trace *trace __maybe_unused)
{
}

static int trace__set_ev_qualifier_bpf_filter(struct trace *trace __maybe_unused)
{
	return 0;
}

static int trace__init_syscalls_bpf_map(struct trace *trace __maybe_unused)
{
	return 0;
}

static struct bpf_program *trace__find_bpf_program_by_title(struct trace *trace __maybe_unused,
							    const char *name __maybe_unused)
{
	return NULL;
}

static int trace__init_syscalls_bpf_prog_array_maps(struct trace *trace __maybe_unused)
{
	return 0;
}

static void trace__delete_augmented_syscalls(struct trace *trace __maybe_unused)
{
}
#endif // HAVE_LIBBPF_SUPPORT

static bool trace__only_augmented_syscalls_evsels(struct trace *trace)
{
	struct evsel *evsel;

	evlist__for_each_entry(trace->evlist, evsel) {
		if (evsel == trace->syscalls.events.augmented ||
		    evsel->bpf_obj == trace->bpf_obj)
			continue;

		return false;
	}

	return true;
}

static int trace__set_ev_qualifier_filter(struct trace *trace)
{
	if (trace->syscalls.map)
		return trace__set_ev_qualifier_bpf_filter(trace);
	if (trace->syscalls.events.sys_enter)
		return trace__set_ev_qualifier_tp_filter(trace);
	return 0;
}

static int bpf_map__set_filter_pids(struct bpf_map *map __maybe_unused,
				    size_t npids __maybe_unused, pid_t *pids __maybe_unused)
{
	int err = 0;
#ifdef HAVE_LIBBPF_SUPPORT
	bool value = true;
	int map_fd = bpf_map__fd(map);
	size_t i;

	for (i = 0; i < npids; ++i) {
		err = bpf_map_update_elem(map_fd, &pids[i], &value, BPF_ANY);
		if (err)
			break;
	}
#endif
	return err;
}

static int trace__set_filter_loop_pids(struct trace *trace)
{
	unsigned int nr = 1, err;
	pid_t pids[32] = {
		getpid(),
	};
	struct thread *thread = machine__find_thread(trace->host, pids[0], pids[0]);

	while (thread && nr < ARRAY_SIZE(pids)) {
		struct thread *parent = machine__find_thread(trace->host, thread->ppid, thread->ppid);

		if (parent == NULL)
			break;

		if (!strcmp(thread__comm_str(parent), "sshd") ||
		    strstarts(thread__comm_str(parent), "gnome-terminal")) {
			pids[nr++] = parent->tid;
			break;
		}
		thread = parent;
	}

	err = evlist__append_tp_filter_pids(trace->evlist, nr, pids);
	if (!err && trace->filter_pids.map)
		err = bpf_map__set_filter_pids(trace->filter_pids.map, nr, pids);

	return err;
}

static int trace__set_filter_pids(struct trace *trace)
{
	int err = 0;
	/*
	 * Better not use !target__has_task() here because we need to cover the
	 * case where no threads were specified in the command line, but a
	 * workload was, and in that case we will fill in the thread_map when
	 * we fork the workload in evlist__prepare_workload.
	 */
	if (trace->filter_pids.nr > 0) {
		err = evlist__append_tp_filter_pids(trace->evlist, trace->filter_pids.nr,
						    trace->filter_pids.entries);
		if (!err && trace->filter_pids.map) {
			err = bpf_map__set_filter_pids(trace->filter_pids.map, trace->filter_pids.nr,
						       trace->filter_pids.entries);
		}
	} else if (perf_thread_map__pid(trace->evlist->core.threads, 0) == -1) {
		err = trace__set_filter_loop_pids(trace);
	}

	return err;
}

static int __trace__deliver_event(struct trace *trace, union perf_event *event)
{
	struct evlist *evlist = trace->evlist;
	struct perf_sample sample;
	int err = evlist__parse_sample(evlist, event, &sample);

	if (err)
		fprintf(trace->output, "Can't parse sample, err = %d, skipping...\n", err);
	else
		trace__handle_event(trace, event, &sample);

	return 0;
}

static int __trace__flush_events(struct trace *trace)
{
	u64 first = ordered_events__first_time(&trace->oe.data);
	u64 flush = trace->oe.last - NSEC_PER_SEC;

	/* Is there some thing to flush.. */
	if (first && first < flush)
		return ordered_events__flush_time(&trace->oe.data, flush);

	return 0;
}

static int trace__flush_events(struct trace *trace)
{
	return !trace->sort_events ? 0 : __trace__flush_events(trace);
}

static int trace__deliver_event(struct trace *trace, union perf_event *event)
{
	int err;

	if (!trace->sort_events)
		return __trace__deliver_event(trace, event);

	err = evlist__parse_sample_timestamp(trace->evlist, event, &trace->oe.last);
	if (err && err != -1)
		return err;

	err = ordered_events__queue(&trace->oe.data, event, trace->oe.last, 0, NULL);
	if (err)
		return err;

	return trace__flush_events(trace);
}

static int ordered_events__deliver_event(struct ordered_events *oe,
					 struct ordered_event *event)
{
	struct trace *trace = container_of(oe, struct trace, oe.data);

	return __trace__deliver_event(trace, event->event);
}

static struct syscall_arg_fmt *evsel__find_syscall_arg_fmt_by_name(struct evsel *evsel, char *arg)
{
	struct tep_format_field *field;
	struct syscall_arg_fmt *fmt = __evsel__syscall_arg_fmt(evsel);

	if (evsel->tp_format == NULL || fmt == NULL)
		return NULL;

	for (field = evsel->tp_format->format.fields; field; field = field->next, ++fmt)
		if (strcmp(field->name, arg) == 0)
			return fmt;

	return NULL;
}

static int trace__expand_filter(struct trace *trace __maybe_unused, struct evsel *evsel)
{
	char *tok, *left = evsel->filter, *new_filter = evsel->filter;

	while ((tok = strpbrk(left, "=<>!")) != NULL) {
		char *right = tok + 1, *right_end;

		if (*right == '=')
			++right;

		while (isspace(*right))
			++right;

		if (*right == '\0')
			break;

		while (!isalpha(*left))
			if (++left == tok) {
				/*
				 * Bail out, can't find the name of the argument that is being
				 * used in the filter, let it try to set this filter, will fail later.
				 */
				return 0;
			}

		right_end = right + 1;
		while (isalnum(*right_end) || *right_end == '_' || *right_end == '|')
			++right_end;

		if (isalpha(*right)) {
			struct syscall_arg_fmt *fmt;
			int left_size = tok - left,
			    right_size = right_end - right;
			char arg[128];

			while (isspace(left[left_size - 1]))
				--left_size;

			scnprintf(arg, sizeof(arg), "%.*s", left_size, left);

			fmt = evsel__find_syscall_arg_fmt_by_name(evsel, arg);
			if (fmt == NULL) {
				pr_err("\"%s\" not found in \"%s\", can't set filter \"%s\"\n",
				       arg, evsel->name, evsel->filter);
				return -1;
			}

			pr_debug2("trying to expand \"%s\" \"%.*s\" \"%.*s\" -> ",
				 arg, (int)(right - tok), tok, right_size, right);

			if (fmt->strtoul) {
				u64 val;
				struct syscall_arg syscall_arg = {
					.parm = fmt->parm,
				};

				if (fmt->strtoul(right, right_size, &syscall_arg, &val)) {
					char *n, expansion[19];
					int expansion_lenght = scnprintf(expansion, sizeof(expansion), "%#" PRIx64, val);
					int expansion_offset = right - new_filter;

					pr_debug("%s", expansion);

					if (asprintf(&n, "%.*s%s%s", expansion_offset, new_filter, expansion, right_end) < 0) {
						pr_debug(" out of memory!\n");
						free(new_filter);
						return -1;
					}
					if (new_filter != evsel->filter)
						free(new_filter);
					left = n + expansion_offset + expansion_lenght;
					new_filter = n;
				} else {
					pr_err("\"%.*s\" not found for \"%s\" in \"%s\", can't set filter \"%s\"\n",
					       right_size, right, arg, evsel->name, evsel->filter);
					return -1;
				}
			} else {
				pr_err("No resolver (strtoul) for \"%s\" in \"%s\", can't set filter \"%s\"\n",
				       arg, evsel->name, evsel->filter);
				return -1;
			}

			pr_debug("\n");
		} else {
			left = right_end;
		}
	}

	if (new_filter != evsel->filter) {
		pr_debug("New filter for %s: %s\n", evsel->name, new_filter);
		evsel__set_filter(evsel, new_filter);
		free(new_filter);
	}

	return 0;
}

static int trace__expand_filters(struct trace *trace, struct evsel **err_evsel)
{
	struct evlist *evlist = trace->evlist;
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->filter == NULL)
			continue;

		if (trace__expand_filter(trace, evsel)) {
			*err_evsel = evsel;
			return -1;
		}
	}

	return 0;
}

static int trace__run(struct trace *trace, int argc, const char **argv)
{
	struct evlist *evlist = trace->evlist;
	struct evsel *evsel, *pgfault_maj = NULL, *pgfault_min = NULL;
	int err = -1, i;
	unsigned long before;
	const bool forks = argc > 0;
	bool draining = false;

	trace->live = true;

	if (!trace->raw_augmented_syscalls) {
		if (trace->trace_syscalls && trace__add_syscall_newtp(trace))
			goto out_error_raw_syscalls;

		if (trace->trace_syscalls)
			trace->vfs_getname = evlist__add_vfs_getname(evlist);
	}

	if ((trace->trace_pgfaults & TRACE_PFMAJ)) {
		pgfault_maj = evsel__new_pgfault(PERF_COUNT_SW_PAGE_FAULTS_MAJ);
		if (pgfault_maj == NULL)
			goto out_error_mem;
		evsel__config_callchain(pgfault_maj, &trace->opts, &callchain_param);
		evlist__add(evlist, pgfault_maj);
	}

	if ((trace->trace_pgfaults & TRACE_PFMIN)) {
		pgfault_min = evsel__new_pgfault(PERF_COUNT_SW_PAGE_FAULTS_MIN);
		if (pgfault_min == NULL)
			goto out_error_mem;
		evsel__config_callchain(pgfault_min, &trace->opts, &callchain_param);
		evlist__add(evlist, pgfault_min);
	}

	/* Enable ignoring missing threads when -u/-p option is defined. */
	trace->opts.ignore_missing_thread = trace->opts.target.uid != UINT_MAX || trace->opts.target.pid;

	if (trace->sched &&
	    evlist__add_newtp(evlist, "sched", "sched_stat_runtime", trace__sched_stat_runtime))
		goto out_error_sched_stat_runtime;
	/*
	 * If a global cgroup was set, apply it to all the events without an
	 * explicit cgroup. I.e.:
	 *
	 * 	trace -G A -e sched:*switch
	 *
	 * Will set all raw_syscalls:sys_{enter,exit}, pgfault, vfs_getname, etc
	 * _and_ sched:sched_switch to the 'A' cgroup, while:
	 *
	 * trace -e sched:*switch -G A
	 *
	 * will only set the sched:sched_switch event to the 'A' cgroup, all the
	 * other events (raw_syscalls:sys_{enter,exit}, etc are left "without"
	 * a cgroup (on the root cgroup, sys wide, etc).
	 *
	 * Multiple cgroups:
	 *
	 * trace -G A -e sched:*switch -G B
	 *
	 * the syscall ones go to the 'A' cgroup, the sched:sched_switch goes
	 * to the 'B' cgroup.
	 *
	 * evlist__set_default_cgroup() grabs a reference of the passed cgroup
	 * only for the evsels still without a cgroup, i.e. evsel->cgroup == NULL.
	 */
	if (trace->cgroup)
		evlist__set_default_cgroup(trace->evlist, trace->cgroup);

	err = evlist__create_maps(evlist, &trace->opts.target);
	if (err < 0) {
		fprintf(trace->output, "Problems parsing the target to trace, check your options!\n");
		goto out_delete_evlist;
	}

	err = trace__symbols_init(trace, evlist);
	if (err < 0) {
		fprintf(trace->output, "Problems initializing symbol libraries!\n");
		goto out_delete_evlist;
	}

	evlist__config(evlist, &trace->opts, &callchain_param);

	if (forks) {
		err = evlist__prepare_workload(evlist, &trace->opts.target, argv, false, NULL);
		if (err < 0) {
			fprintf(trace->output, "Couldn't run the workload!\n");
			goto out_delete_evlist;
		}
		workload_pid = evlist->workload.pid;
	}

	err = evlist__open(evlist);
	if (err < 0)
		goto out_error_open;

	err = bpf__apply_obj_config();
	if (err) {
		char errbuf[BUFSIZ];

		bpf__strerror_apply_obj_config(err, errbuf, sizeof(errbuf));
		pr_err("ERROR: Apply config to BPF failed: %s\n",
			 errbuf);
		goto out_error_open;
	}

	err = trace__set_filter_pids(trace);
	if (err < 0)
		goto out_error_mem;

	if (trace->syscalls.map)
		trace__init_syscalls_bpf_map(trace);

	if (trace->syscalls.prog_array.sys_enter)
		trace__init_syscalls_bpf_prog_array_maps(trace);

	if (trace->ev_qualifier_ids.nr > 0) {
		err = trace__set_ev_qualifier_filter(trace);
		if (err < 0)
			goto out_errno;

		if (trace->syscalls.events.sys_exit) {
			pr_debug("event qualifier tracepoint filter: %s\n",
				 trace->syscalls.events.sys_exit->filter);
		}
	}

	/*
	 * If the "close" syscall is not traced, then we will not have the
	 * opportunity to, in syscall_arg__scnprintf_close_fd() invalidate the
	 * fd->pathname table and were ending up showing the last value set by
	 * syscalls opening a pathname and associating it with a descriptor or
	 * reading it from /proc/pid/fd/ in cases where that doesn't make
	 * sense.
	 *
	 *  So just disable this beautifier (SCA_FD, SCA_FDAT) when 'close' is
	 *  not in use.
	 */
	trace->fd_path_disabled = !trace__syscall_enabled(trace, syscalltbl__id(trace->sctbl, "close"));

	err = trace__expand_filters(trace, &evsel);
	if (err)
		goto out_delete_evlist;
	err = evlist__apply_filters(evlist, &evsel);
	if (err < 0)
		goto out_error_apply_filters;

	if (trace->dump.map)
		bpf_map__fprintf(trace->dump.map, trace->output);

	err = evlist__mmap(evlist, trace->opts.mmap_pages);
	if (err < 0)
		goto out_error_mmap;

	if (!target__none(&trace->opts.target) && !trace->opts.initial_delay)
		evlist__enable(evlist);

	if (forks)
		evlist__start_workload(evlist);

	if (trace->opts.initial_delay) {
		usleep(trace->opts.initial_delay * 1000);
		evlist__enable(evlist);
	}

	trace->multiple_threads = perf_thread_map__pid(evlist->core.threads, 0) == -1 ||
				  evlist->core.threads->nr > 1 ||
				  evlist__first(evlist)->core.attr.inherit;

	/*
	 * Now that we already used evsel->core.attr to ask the kernel to setup the
	 * events, lets reuse evsel->core.attr.sample_max_stack as the limit in
	 * trace__resolve_callchain(), allowing per-event max-stack settings
	 * to override an explicitly set --max-stack global setting.
	 */
	evlist__for_each_entry(evlist, evsel) {
		if (evsel__has_callchain(evsel) &&
		    evsel->core.attr.sample_max_stack == 0)
			evsel->core.attr.sample_max_stack = trace->max_stack;
	}
again:
	before = trace->nr_events;

	for (i = 0; i < evlist->core.nr_mmaps; i++) {
		union perf_event *event;
		struct mmap *md;

		md = &evlist->mmap[i];
		if (perf_mmap__read_init(&md->core) < 0)
			continue;

		while ((event = perf_mmap__read_event(&md->core)) != NULL) {
			++trace->nr_events;

			err = trace__deliver_event(trace, event);
			if (err)
				goto out_disable;

			perf_mmap__consume(&md->core);

			if (interrupted)
				goto out_disable;

			if (done && !draining) {
				evlist__disable(evlist);
				draining = true;
			}
		}
		perf_mmap__read_done(&md->core);
	}

	if (trace->nr_events == before) {
		int timeout = done ? 100 : -1;

		if (!draining && evlist__poll(evlist, timeout) > 0) {
			if (evlist__filter_pollfd(evlist, POLLERR | POLLHUP | POLLNVAL) == 0)
				draining = true;

			goto again;
		} else {
			if (trace__flush_events(trace))
				goto out_disable;
		}
	} else {
		goto again;
	}

out_disable:
	thread__zput(trace->current);

	evlist__disable(evlist);

	if (trace->sort_events)
		ordered_events__flush(&trace->oe.data, OE_FLUSH__FINAL);

	if (!err) {
		if (trace->summary)
			trace__fprintf_thread_summary(trace, trace->output);

		if (trace->show_tool_stats) {
			fprintf(trace->output, "Stats:\n "
					       " vfs_getname : %" PRIu64 "\n"
					       " proc_getname: %" PRIu64 "\n",
				trace->stats.vfs_getname,
				trace->stats.proc_getname);
		}
	}

out_delete_evlist:
	trace__symbols__exit(trace);
	evlist__free_syscall_tp_fields(evlist);
	evlist__delete(evlist);
	cgroup__put(trace->cgroup);
	trace->evlist = NULL;
	trace->live = false;
	return err;
{
	char errbuf[BUFSIZ];

out_error_sched_stat_runtime:
	tracing_path__strerror_open_tp(errno, errbuf, sizeof(errbuf), "sched", "sched_stat_runtime");
	goto out_error;

out_error_raw_syscalls:
	tracing_path__strerror_open_tp(errno, errbuf, sizeof(errbuf), "raw_syscalls", "sys_(enter|exit)");
	goto out_error;

out_error_mmap:
	evlist__strerror_mmap(evlist, errno, errbuf, sizeof(errbuf));
	goto out_error;

out_error_open:
	evlist__strerror_open(evlist, errno, errbuf, sizeof(errbuf));

out_error:
	fprintf(trace->output, "%s\n", errbuf);
	goto out_delete_evlist;

out_error_apply_filters:
	fprintf(trace->output,
		"Failed to set filter \"%s\" on event %s with %d (%s)\n",
		evsel->filter, evsel__name(evsel), errno,
		str_error_r(errno, errbuf, sizeof(errbuf)));
	goto out_delete_evlist;
}
out_error_mem:
	fprintf(trace->output, "Not enough memory to run!\n");
	goto out_delete_evlist;

out_errno:
	fprintf(trace->output, "errno=%d,%s\n", errno, strerror(errno));
	goto out_delete_evlist;
}

static int trace__replay(struct trace *trace)
{
	const struct evsel_str_handler handlers[] = {
		{ "probe:vfs_getname",	     trace__vfs_getname, },
	};
	struct perf_data data = {
		.path  = input_name,
		.mode  = PERF_DATA_MODE_READ,
		.force = trace->force,
	};
	struct perf_session *session;
	struct evsel *evsel;
	int err = -1;

	trace->tool.sample	  = trace__process_sample;
	trace->tool.mmap	  = perf_event__process_mmap;
	trace->tool.mmap2	  = perf_event__process_mmap2;
	trace->tool.comm	  = perf_event__process_comm;
	trace->tool.exit	  = perf_event__process_exit;
	trace->tool.fork	  = perf_event__process_fork;
	trace->tool.attr	  = perf_event__process_attr;
	trace->tool.tracing_data  = perf_event__process_tracing_data;
	trace->tool.build_id	  = perf_event__process_build_id;
	trace->tool.namespaces	  = perf_event__process_namespaces;

	trace->tool.ordered_events = true;
	trace->tool.ordering_requires_timestamps = true;

	/* add tid to output */
	trace->multiple_threads = true;

	session = perf_session__new(&data, &trace->tool);
	if (IS_ERR(session))
		return PTR_ERR(session);

	if (trace->opts.target.pid)
		symbol_conf.pid_list_str = strdup(trace->opts.target.pid);

	if (trace->opts.target.tid)
		symbol_conf.tid_list_str = strdup(trace->opts.target.tid);

	if (symbol__init(&session->header.env) < 0)
		goto out;

	trace->host = &session->machines.host;

	err = perf_session__set_tracepoints_handlers(session, handlers);
	if (err)
		goto out;

	evsel = evlist__find_tracepoint_by_name(session->evlist, "raw_syscalls:sys_enter");
	trace->syscalls.events.sys_enter = evsel;
	/* older kernels have syscalls tp versus raw_syscalls */
	if (evsel == NULL)
		evsel = evlist__find_tracepoint_by_name(session->evlist, "syscalls:sys_enter");

	if (evsel &&
	    (evsel__init_raw_syscall_tp(evsel, trace__sys_enter) < 0 ||
	    perf_evsel__init_sc_tp_ptr_field(evsel, args))) {
		pr_err("Error during initialize raw_syscalls:sys_enter event\n");
		goto out;
	}

	evsel = evlist__find_tracepoint_by_name(session->evlist, "raw_syscalls:sys_exit");
	trace->syscalls.events.sys_exit = evsel;
	if (evsel == NULL)
		evsel = evlist__find_tracepoint_by_name(session->evlist, "syscalls:sys_exit");
	if (evsel &&
	    (evsel__init_raw_syscall_tp(evsel, trace__sys_exit) < 0 ||
	    perf_evsel__init_sc_tp_uint_field(evsel, ret))) {
		pr_err("Error during initialize raw_syscalls:sys_exit event\n");
		goto out;
	}

	evlist__for_each_entry(session->evlist, evsel) {
		if (evsel->core.attr.type == PERF_TYPE_SOFTWARE &&
		    (evsel->core.attr.config == PERF_COUNT_SW_PAGE_FAULTS_MAJ ||
		     evsel->core.attr.config == PERF_COUNT_SW_PAGE_FAULTS_MIN ||
		     evsel->core.attr.config == PERF_COUNT_SW_PAGE_FAULTS))
			evsel->handler = trace__pgfault;
	}

	setup_pager();

	err = perf_session__process_events(session);
	if (err)
		pr_err("Failed to process events, error %d", err);

	else if (trace->summary)
		trace__fprintf_thread_summary(trace, trace->output);

out:
	perf_session__delete(session);

	return err;
}

static size_t trace__fprintf_threads_header(FILE *fp)
{
	size_t printed;

	printed  = fprintf(fp, "\n Summary of events:\n\n");

	return printed;
}

DEFINE_RESORT_RB(syscall_stats, a->msecs > b->msecs,
	struct syscall_stats *stats;
	double		     msecs;
	int		     syscall;
)
{
	struct int_node *source = rb_entry(nd, struct int_node, rb_node);
	struct syscall_stats *stats = source->priv;

	entry->syscall = source->i;
	entry->stats   = stats;
	entry->msecs   = stats ? (u64)stats->stats.n * (avg_stats(&stats->stats) / NSEC_PER_MSEC) : 0;
}

static size_t thread__dump_stats(struct thread_trace *ttrace,
				 struct trace *trace, FILE *fp)
{
	size_t printed = 0;
	struct syscall *sc;
	struct rb_node *nd;
	DECLARE_RESORT_RB_INTLIST(syscall_stats, ttrace->syscall_stats);

	if (syscall_stats == NULL)
		return 0;

	printed += fprintf(fp, "\n");

	printed += fprintf(fp, "   syscall            calls  errors  total       min       avg       max       stddev\n");
	printed += fprintf(fp, "                                     (msec)    (msec)    (msec)    (msec)        (%%)\n");
	printed += fprintf(fp, "   --------------- --------  ------ -------- --------- --------- ---------     ------\n");

	resort_rb__for_each_entry(nd, syscall_stats) {
		struct syscall_stats *stats = syscall_stats_entry->stats;
		if (stats) {
			double min = (double)(stats->stats.min) / NSEC_PER_MSEC;
			double max = (double)(stats->stats.max) / NSEC_PER_MSEC;
			double avg = avg_stats(&stats->stats);
			double pct;
			u64 n = (u64)stats->stats.n;

			pct = avg ? 100.0 * stddev_stats(&stats->stats) / avg : 0.0;
			avg /= NSEC_PER_MSEC;

			sc = &trace->syscalls.table[syscall_stats_entry->syscall];
			printed += fprintf(fp, "   %-15s", sc->name);
			printed += fprintf(fp, " %8" PRIu64 " %6" PRIu64 " %9.3f %9.3f %9.3f",
					   n, stats->nr_failures, syscall_stats_entry->msecs, min, avg);
			printed += fprintf(fp, " %9.3f %9.2f%%\n", max, pct);

			if (trace->errno_summary && stats->nr_failures) {
				const char *arch_name = perf_env__arch(trace->host->env);
				int e;

				for (e = 0; e < stats->max_errno; ++e) {
					if (stats->errnos[e] != 0)
						fprintf(fp, "\t\t\t\t%s: %d\n", arch_syscalls__strerrno(arch_name, e + 1), stats->errnos[e]);
				}
			}
		}
	}

	resort_rb__delete(syscall_stats);
	printed += fprintf(fp, "\n\n");

	return printed;
}

static size_t trace__fprintf_thread(FILE *fp, struct thread *thread, struct trace *trace)
{
	size_t printed = 0;
	struct thread_trace *ttrace = thread__priv(thread);
	double ratio;

	if (ttrace == NULL)
		return 0;

	ratio = (double)ttrace->nr_events / trace->nr_events * 100.0;

	printed += fprintf(fp, " %s (%d), ", thread__comm_str(thread), thread->tid);
	printed += fprintf(fp, "%lu events, ", ttrace->nr_events);
	printed += fprintf(fp, "%.1f%%", ratio);
	if (ttrace->pfmaj)
		printed += fprintf(fp, ", %lu majfaults", ttrace->pfmaj);
	if (ttrace->pfmin)
		printed += fprintf(fp, ", %lu minfaults", ttrace->pfmin);
	if (trace->sched)
		printed += fprintf(fp, ", %.3f msec\n", ttrace->runtime_ms);
	else if (fputc('\n', fp) != EOF)
		++printed;

	printed += thread__dump_stats(ttrace, trace, fp);

	return printed;
}

static unsigned long thread__nr_events(struct thread_trace *ttrace)
{
	return ttrace ? ttrace->nr_events : 0;
}

DEFINE_RESORT_RB(threads, (thread__nr_events(a->thread->priv) < thread__nr_events(b->thread->priv)),
	struct thread *thread;
)
{
	entry->thread = rb_entry(nd, struct thread, rb_node);
}

static size_t trace__fprintf_thread_summary(struct trace *trace, FILE *fp)
{
	size_t printed = trace__fprintf_threads_header(fp);
	struct rb_node *nd;
	int i;

	for (i = 0; i < THREADS__TABLE_SIZE; i++) {
		DECLARE_RESORT_RB_MACHINE_THREADS(threads, trace->host, i);

		if (threads == NULL) {
			fprintf(fp, "%s", "Error sorting output by nr_events!\n");
			return 0;
		}

		resort_rb__for_each_entry(nd, threads)
			printed += trace__fprintf_thread(fp, threads_entry->thread, trace);

		resort_rb__delete(threads);
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

static int trace__set_filter_pids_from_option(const struct option *opt, const char *str,
					      int unset __maybe_unused)
{
	int ret = -1;
	size_t i;
	struct trace *trace = opt->value;
	/*
	 * FIXME: introduce a intarray class, plain parse csv and create a
	 * { int nr, int entries[] } struct...
	 */
	struct intlist *list = intlist__new(str);

	if (list == NULL)
		return -1;

	i = trace->filter_pids.nr = intlist__nr_entries(list) + 1;
	trace->filter_pids.entries = calloc(i, sizeof(pid_t));

	if (trace->filter_pids.entries == NULL)
		goto out;

	trace->filter_pids.entries[0] = getpid();

	for (i = 1; i < trace->filter_pids.nr; ++i)
		trace->filter_pids.entries[i] = intlist__entry(list, i - 1)->i;

	intlist__delete(list);
	ret = 0;
out:
	return ret;
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

static int parse_pagefaults(const struct option *opt, const char *str,
			    int unset __maybe_unused)
{
	int *trace_pgfaults = opt->value;

	if (strcmp(str, "all") == 0)
		*trace_pgfaults |= TRACE_PFMAJ | TRACE_PFMIN;
	else if (strcmp(str, "maj") == 0)
		*trace_pgfaults |= TRACE_PFMAJ;
	else if (strcmp(str, "min") == 0)
		*trace_pgfaults |= TRACE_PFMIN;
	else
		return -1;

	return 0;
}

static void evlist__set_default_evsel_handler(struct evlist *evlist, void *handler)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->handler == NULL)
			evsel->handler = handler;
	}
}

static void evsel__set_syscall_arg_fmt(struct evsel *evsel, const char *name)
{
	struct syscall_arg_fmt *fmt = evsel__syscall_arg_fmt(evsel);

	if (fmt) {
		struct syscall_fmt *scfmt = syscall_fmt__find(name);

		if (scfmt) {
			int skip = 0;

			if (strcmp(evsel->tp_format->format.fields->name, "__syscall_nr") == 0 ||
			    strcmp(evsel->tp_format->format.fields->name, "nr") == 0)
				++skip;

			memcpy(fmt + skip, scfmt->arg, (evsel->tp_format->format.nr_fields - skip) * sizeof(*fmt));
		}
	}
}

static int evlist__set_syscall_tp_fields(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->priv || !evsel->tp_format)
			continue;

		if (strcmp(evsel->tp_format->system, "syscalls")) {
			evsel__init_tp_arg_scnprintf(evsel);
			continue;
		}

		if (evsel__init_syscall_tp(evsel))
			return -1;

		if (!strncmp(evsel->tp_format->name, "sys_enter_", 10)) {
			struct syscall_tp *sc = __evsel__syscall_tp(evsel);

			if (__tp_field__init_ptr(&sc->args, sc->id.offset + sizeof(u64)))
				return -1;

			evsel__set_syscall_arg_fmt(evsel, evsel->tp_format->name + sizeof("sys_enter_") - 1);
		} else if (!strncmp(evsel->tp_format->name, "sys_exit_", 9)) {
			struct syscall_tp *sc = __evsel__syscall_tp(evsel);

			if (__tp_field__init_uint(&sc->ret, sizeof(u64), sc->id.offset + sizeof(u64), evsel->needs_swap))
				return -1;

			evsel__set_syscall_arg_fmt(evsel, evsel->tp_format->name + sizeof("sys_exit_") - 1);
		}
	}

	return 0;
}

/*
 * XXX: Hackish, just splitting the combined -e+--event (syscalls
 * (raw_syscalls:{sys_{enter,exit}} + events (tracepoints, HW, SW, etc) to use
 * existing facilities unchanged (trace->ev_qualifier + parse_options()).
 *
 * It'd be better to introduce a parse_options() variant that would return a
 * list with the terms it didn't match to an event...
 */
static int trace__parse_events_option(const struct option *opt, const char *str,
				      int unset __maybe_unused)
{
	struct trace *trace = (struct trace *)opt->value;
	const char *s = str;
	char *sep = NULL, *lists[2] = { NULL, NULL, };
	int len = strlen(str) + 1, err = -1, list, idx;
	char *strace_groups_dir = system_path(STRACE_GROUPS_DIR);
	char group_name[PATH_MAX];
	struct syscall_fmt *fmt;

	if (strace_groups_dir == NULL)
		return -1;

	if (*s == '!') {
		++s;
		trace->not_ev_qualifier = true;
	}

	while (1) {
		if ((sep = strchr(s, ',')) != NULL)
			*sep = '\0';

		list = 0;
		if (syscalltbl__id(trace->sctbl, s) >= 0 ||
		    syscalltbl__strglobmatch_first(trace->sctbl, s, &idx) >= 0) {
			list = 1;
			goto do_concat;
		}

		fmt = syscall_fmt__find_by_alias(s);
		if (fmt != NULL) {
			list = 1;
			s = fmt->name;
		} else {
			path__join(group_name, sizeof(group_name), strace_groups_dir, s);
			if (access(group_name, R_OK) == 0)
				list = 1;
		}
do_concat:
		if (lists[list]) {
			sprintf(lists[list] + strlen(lists[list]), ",%s", s);
		} else {
			lists[list] = malloc(len);
			if (lists[list] == NULL)
				goto out;
			strcpy(lists[list], s);
		}

		if (!sep)
			break;

		*sep = ',';
		s = sep + 1;
	}

	if (lists[1] != NULL) {
		struct strlist_config slist_config = {
			.dirname = strace_groups_dir,
		};

		trace->ev_qualifier = strlist__new(lists[1], &slist_config);
		if (trace->ev_qualifier == NULL) {
			fputs("Not enough memory to parse event qualifier", trace->output);
			goto out;
		}

		if (trace__validate_ev_qualifier(trace))
			goto out;
		trace->trace_syscalls = true;
	}

	err = 0;

	if (lists[0]) {
		struct option o = {
			.value = &trace->evlist,
		};
		err = parse_events_option(&o, lists[0], 0);
	}
out:
	free(strace_groups_dir);
	free(lists[0]);
	free(lists[1]);
	if (sep)
		*sep = ',';

	return err;
}

static int trace__parse_cgroups(const struct option *opt, const char *str, int unset)
{
	struct trace *trace = opt->value;

	if (!list_empty(&trace->evlist->core.entries)) {
		struct option o = {
			.value = &trace->evlist,
		};
		return parse_cgroups(&o, str, unset);
	}
	trace->cgroup = evlist__findnew_cgroup(trace->evlist, str);

	return 0;
}

static int trace__config(const char *var, const char *value, void *arg)
{
	struct trace *trace = arg;
	int err = 0;

	if (!strcmp(var, "trace.add_events")) {
		trace->perfconfig_events = strdup(value);
		if (trace->perfconfig_events == NULL) {
			pr_err("Not enough memory for %s\n", "trace.add_events");
			return -1;
		}
	} else if (!strcmp(var, "trace.show_timestamp")) {
		trace->show_tstamp = perf_config_bool(var, value);
	} else if (!strcmp(var, "trace.show_duration")) {
		trace->show_duration = perf_config_bool(var, value);
	} else if (!strcmp(var, "trace.show_arg_names")) {
		trace->show_arg_names = perf_config_bool(var, value);
		if (!trace->show_arg_names)
			trace->show_zeros = true;
	} else if (!strcmp(var, "trace.show_zeros")) {
		bool new_show_zeros = perf_config_bool(var, value);
		if (!trace->show_arg_names && !new_show_zeros) {
			pr_warning("trace.show_zeros has to be set when trace.show_arg_names=no\n");
			goto out;
		}
		trace->show_zeros = new_show_zeros;
	} else if (!strcmp(var, "trace.show_prefix")) {
		trace->show_string_prefix = perf_config_bool(var, value);
	} else if (!strcmp(var, "trace.no_inherit")) {
		trace->opts.no_inherit = perf_config_bool(var, value);
	} else if (!strcmp(var, "trace.args_alignment")) {
		int args_alignment = 0;
		if (perf_config_int(&args_alignment, var, value) == 0)
			trace->args_alignment = args_alignment;
	} else if (!strcmp(var, "trace.tracepoint_beautifiers")) {
		if (strcasecmp(value, "libtraceevent") == 0)
			trace->libtraceevent_print = true;
		else if (strcasecmp(value, "libbeauty") == 0)
			trace->libtraceevent_print = false;
	}
out:
	return err;
}

static void trace__exit(struct trace *trace)
{
	int i;

	strlist__delete(trace->ev_qualifier);
	free(trace->ev_qualifier_ids.entries);
	if (trace->syscalls.table) {
		for (i = 0; i <= trace->sctbl->syscalls.max_id; i++)
			syscall__exit(&trace->syscalls.table[i]);
		free(trace->syscalls.table);
	}
	syscalltbl__delete(trace->sctbl);
	zfree(&trace->perfconfig_events);
}

int cmd_trace(int argc, const char **argv)
{
	const char *trace_usage[] = {
		"perf trace [<options>] [<command>]",
		"perf trace [<options>] -- <command> [<options>]",
		"perf trace record [<options>] [<command>]",
		"perf trace record [<options>] -- <command> [<options>]",
		NULL
	};
	struct trace trace = {
		.opts = {
			.target = {
				.uid	   = UINT_MAX,
				.uses_mmap = true,
			},
			.user_freq     = UINT_MAX,
			.user_interval = ULLONG_MAX,
			.no_buffering  = true,
			.mmap_pages    = UINT_MAX,
		},
		.output = stderr,
		.show_comm = true,
		.show_tstamp = true,
		.show_duration = true,
		.show_arg_names = true,
		.args_alignment = 70,
		.trace_syscalls = false,
		.kernel_syscallchains = false,
		.max_stack = UINT_MAX,
		.max_events = ULONG_MAX,
	};
	const char *map_dump_str = NULL;
	const char *output_name = NULL;
	const struct option trace_options[] = {
	OPT_CALLBACK('e', "event", &trace, "event",
		     "event/syscall selector. use 'perf list' to list available events",
		     trace__parse_events_option),
	OPT_CALLBACK(0, "filter", &trace.evlist, "filter",
		     "event filter", parse_filter),
	OPT_BOOLEAN(0, "comm", &trace.show_comm,
		    "show the thread COMM next to its id"),
	OPT_BOOLEAN(0, "tool_stats", &trace.show_tool_stats, "show tool stats"),
	OPT_CALLBACK(0, "expr", &trace, "expr", "list of syscalls/events to trace",
		     trace__parse_events_option),
	OPT_STRING('o', "output", &output_name, "file", "output file name"),
	OPT_STRING('i', "input", &input_name, "file", "Analyze events in file"),
	OPT_STRING('p', "pid", &trace.opts.target.pid, "pid",
		    "trace events on existing process id"),
	OPT_STRING('t', "tid", &trace.opts.target.tid, "tid",
		    "trace events on existing thread id"),
	OPT_CALLBACK(0, "filter-pids", &trace, "CSV list of pids",
		     "pids to filter (by the kernel)", trace__set_filter_pids_from_option),
	OPT_BOOLEAN('a', "all-cpus", &trace.opts.target.system_wide,
		    "system-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &trace.opts.target.cpu_list, "cpu",
		    "list of cpus to monitor"),
	OPT_BOOLEAN(0, "no-inherit", &trace.opts.no_inherit,
		    "child tasks do not inherit counters"),
	OPT_CALLBACK('m', "mmap-pages", &trace.opts.mmap_pages, "pages",
		     "number of mmap data pages", evlist__parse_mmap_pages),
	OPT_STRING('u', "uid", &trace.opts.target.uid_str, "user",
		   "user to profile"),
	OPT_CALLBACK(0, "duration", &trace, "float",
		     "show only events with duration > N.M ms",
		     trace__set_duration),
#ifdef HAVE_LIBBPF_SUPPORT
	OPT_STRING(0, "map-dump", &map_dump_str, "BPF map", "BPF map to periodically dump"),
#endif
	OPT_BOOLEAN(0, "sched", &trace.sched, "show blocking scheduler events"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_BOOLEAN('T', "time", &trace.full_time,
		    "Show full timestamp, not time relative to first start"),
	OPT_BOOLEAN(0, "failure", &trace.failure_only,
		    "Show only syscalls that failed"),
	OPT_BOOLEAN('s', "summary", &trace.summary_only,
		    "Show only syscall summary with statistics"),
	OPT_BOOLEAN('S', "with-summary", &trace.summary,
		    "Show all syscalls and summary with statistics"),
	OPT_BOOLEAN(0, "errno-summary", &trace.errno_summary,
		    "Show errno stats per syscall, use with -s or -S"),
	OPT_CALLBACK_DEFAULT('F', "pf", &trace.trace_pgfaults, "all|maj|min",
		     "Trace pagefaults", parse_pagefaults, "maj"),
	OPT_BOOLEAN(0, "syscalls", &trace.trace_syscalls, "Trace syscalls"),
	OPT_BOOLEAN('f', "force", &trace.force, "don't complain, do it"),
	OPT_CALLBACK(0, "call-graph", &trace.opts,
		     "record_mode[,record_size]", record_callchain_help,
		     &record_parse_callchain_opt),
	OPT_BOOLEAN(0, "libtraceevent_print", &trace.libtraceevent_print,
		    "Use libtraceevent to print the tracepoint arguments."),
	OPT_BOOLEAN(0, "kernel-syscall-graph", &trace.kernel_syscallchains,
		    "Show the kernel callchains on the syscall exit path"),
	OPT_ULONG(0, "max-events", &trace.max_events,
		"Set the maximum number of events to print, exit after that is reached. "),
	OPT_UINTEGER(0, "min-stack", &trace.min_stack,
		     "Set the minimum stack depth when parsing the callchain, "
		     "anything below the specified depth will be ignored."),
	OPT_UINTEGER(0, "max-stack", &trace.max_stack,
		     "Set the maximum stack depth when parsing the callchain, "
		     "anything beyond the specified depth will be ignored. "
		     "Default: kernel.perf_event_max_stack or " __stringify(PERF_MAX_STACK_DEPTH)),
	OPT_BOOLEAN(0, "sort-events", &trace.sort_events,
			"Sort batch of events before processing, use if getting out of order events"),
	OPT_BOOLEAN(0, "print-sample", &trace.print_sample,
			"print the PERF_RECORD_SAMPLE PERF_SAMPLE_ info, for debugging"),
	OPT_UINTEGER(0, "proc-map-timeout", &proc_map_timeout,
			"per thread proc mmap processing timeout in ms"),
	OPT_CALLBACK('G', "cgroup", &trace, "name", "monitor event in cgroup name only",
		     trace__parse_cgroups),
	OPT_INTEGER('D', "delay", &trace.opts.initial_delay,
		     "ms to wait before starting measurement after program "
		     "start"),
	OPTS_EVSWITCH(&trace.evswitch),
	OPT_END()
	};
	bool __maybe_unused max_stack_user_set = true;
	bool mmap_pages_user_set = true;
	struct evsel *evsel;
	const char * const trace_subcommands[] = { "record", NULL };
	int err = -1;
	char bf[BUFSIZ];
	struct sigaction sigchld_act;

	signal(SIGSEGV, sighandler_dump_stack);
	signal(SIGFPE, sighandler_dump_stack);
	signal(SIGINT, sighandler_interrupt);

	memset(&sigchld_act, 0, sizeof(sigchld_act));
	sigchld_act.sa_flags = SA_SIGINFO;
	sigchld_act.sa_sigaction = sighandler_chld;
	sigaction(SIGCHLD, &sigchld_act, NULL);

	trace.evlist = evlist__new();
	trace.sctbl = syscalltbl__new();

	if (trace.evlist == NULL || trace.sctbl == NULL) {
		pr_err("Not enough memory to run!\n");
		err = -ENOMEM;
		goto out;
	}

	/*
	 * Parsing .perfconfig may entail creating a BPF event, that may need
	 * to create BPF maps, so bump RLIM_MEMLOCK as the default 64K setting
	 * is too small. This affects just this process, not touching the
	 * global setting. If it fails we'll get something in 'perf trace -v'
	 * to help diagnose the problem.
	 */
	rlimit__bump_memlock();

	err = perf_config(trace__config, &trace);
	if (err)
		goto out;

	argc = parse_options_subcommand(argc, argv, trace_options, trace_subcommands,
				 trace_usage, PARSE_OPT_STOP_AT_NON_OPTION);

	/*
	 * Here we already passed thru trace__parse_events_option() and it has
	 * already figured out if -e syscall_name, if not but if --event
	 * foo:bar was used, the user is interested _just_ in those, say,
	 * tracepoint events, not in the strace-like syscall-name-based mode.
	 *
	 * This is important because we need to check if strace-like mode is
	 * needed to decided if we should filter out the eBPF
	 * __augmented_syscalls__ code, if it is in the mix, say, via
	 * .perfconfig trace.add_events, and filter those out.
	 */
	if (!trace.trace_syscalls && !trace.trace_pgfaults &&
	    trace.evlist->core.nr_entries == 0 /* Was --events used? */) {
		trace.trace_syscalls = true;
	}
	/*
	 * Now that we have --verbose figured out, lets see if we need to parse
	 * events from .perfconfig, so that if those events fail parsing, say some
	 * BPF program fails, then we'll be able to use --verbose to see what went
	 * wrong in more detail.
	 */
	if (trace.perfconfig_events != NULL) {
		struct parse_events_error parse_err;

		parse_events_error__init(&parse_err);
		err = parse_events(trace.evlist, trace.perfconfig_events, &parse_err);
		if (err)
			parse_events_error__print(&parse_err, trace.perfconfig_events);
		parse_events_error__exit(&parse_err);
		if (err)
			goto out;
	}

	if ((nr_cgroups || trace.cgroup) && !trace.opts.target.system_wide) {
		usage_with_options_msg(trace_usage, trace_options,
				       "cgroup monitoring only available in system-wide mode");
	}

	evsel = bpf__setup_output_event(trace.evlist, "__augmented_syscalls__");
	if (IS_ERR(evsel)) {
		bpf__strerror_setup_output_event(trace.evlist, PTR_ERR(evsel), bf, sizeof(bf));
		pr_err("ERROR: Setup trace syscalls enter failed: %s\n", bf);
		goto out;
	}

	if (evsel) {
		trace.syscalls.events.augmented = evsel;

		evsel = evlist__find_tracepoint_by_name(trace.evlist, "raw_syscalls:sys_enter");
		if (evsel == NULL) {
			pr_err("ERROR: raw_syscalls:sys_enter not found in the augmented BPF object\n");
			goto out;
		}

		if (evsel->bpf_obj == NULL) {
			pr_err("ERROR: raw_syscalls:sys_enter not associated to a BPF object\n");
			goto out;
		}

		trace.bpf_obj = evsel->bpf_obj;

		/*
		 * If we have _just_ the augmenter event but don't have a
		 * explicit --syscalls, then assume we want all strace-like
		 * syscalls:
		 */
		if (!trace.trace_syscalls && trace__only_augmented_syscalls_evsels(&trace))
			trace.trace_syscalls = true;
		/*
		 * So, if we have a syscall augmenter, but trace_syscalls, aka
		 * strace-like syscall tracing is not set, then we need to trow
		 * away the augmenter, i.e. all the events that were created
		 * from that BPF object file.
		 *
		 * This is more to fix the current .perfconfig trace.add_events
		 * style of setting up the strace-like eBPF based syscall point
		 * payload augmenter.
		 *
		 * All this complexity will be avoided by adding an alternative
		 * to trace.add_events in the form of
		 * trace.bpf_augmented_syscalls, that will be only parsed if we
		 * need it.
		 *
		 * .perfconfig trace.add_events is still useful if we want, for
		 * instance, have msr_write.msr in some .perfconfig profile based
		 * 'perf trace --config determinism.profile' mode, where for some
		 * particular goal/workload type we want a set of events and
		 * output mode (with timings, etc) instead of having to add
		 * all via the command line.
		 *
		 * Also --config to specify an alternate .perfconfig file needs
		 * to be implemented.
		 */
		if (!trace.trace_syscalls) {
			trace__delete_augmented_syscalls(&trace);
		} else {
			trace__set_bpf_map_filtered_pids(&trace);
			trace__set_bpf_map_syscalls(&trace);
			trace.syscalls.unaugmented_prog = trace__find_bpf_program_by_title(&trace, "!raw_syscalls:unaugmented");
		}
	}

	err = bpf__setup_stdout(trace.evlist);
	if (err) {
		bpf__strerror_setup_stdout(trace.evlist, err, bf, sizeof(bf));
		pr_err("ERROR: Setup BPF stdout failed: %s\n", bf);
		goto out;
	}

	err = -1;

	if (map_dump_str) {
		trace.dump.map = trace__find_bpf_map_by_name(&trace, map_dump_str);
		if (trace.dump.map == NULL) {
			pr_err("ERROR: BPF map \"%s\" not found\n", map_dump_str);
			goto out;
		}
	}

	if (trace.trace_pgfaults) {
		trace.opts.sample_address = true;
		trace.opts.sample_time = true;
	}

	if (trace.opts.mmap_pages == UINT_MAX)
		mmap_pages_user_set = false;

	if (trace.max_stack == UINT_MAX) {
		trace.max_stack = input_name ? PERF_MAX_STACK_DEPTH : sysctl__max_stack();
		max_stack_user_set = false;
	}

#ifdef HAVE_DWARF_UNWIND_SUPPORT
	if ((trace.min_stack || max_stack_user_set) && !callchain_param.enabled) {
		record_opts__parse_callchain(&trace.opts, &callchain_param, "dwarf", false);
	}
#endif

	if (callchain_param.enabled) {
		if (!mmap_pages_user_set && geteuid() == 0)
			trace.opts.mmap_pages = perf_event_mlock_kb_in_pages() * 4;

		symbol_conf.use_callchain = true;
	}

	if (trace.evlist->core.nr_entries > 0) {
		evlist__set_default_evsel_handler(trace.evlist, trace__event_handler);
		if (evlist__set_syscall_tp_fields(trace.evlist)) {
			perror("failed to set syscalls:* tracepoint fields");
			goto out;
		}
	}

	if (trace.sort_events) {
		ordered_events__init(&trace.oe.data, ordered_events__deliver_event, &trace);
		ordered_events__set_copy_on_queue(&trace.oe.data, true);
	}

	/*
	 * If we are augmenting syscalls, then combine what we put in the
	 * __augmented_syscalls__ BPF map with what is in the
	 * syscalls:sys_exit_FOO tracepoints, i.e. just like we do without BPF,
	 * combining raw_syscalls:sys_enter with raw_syscalls:sys_exit.
	 *
	 * We'll switch to look at two BPF maps, one for sys_enter and the
	 * other for sys_exit when we start augmenting the sys_exit paths with
	 * buffers that are being copied from kernel to userspace, think 'read'
	 * syscall.
	 */
	if (trace.syscalls.events.augmented) {
		evlist__for_each_entry(trace.evlist, evsel) {
			bool raw_syscalls_sys_exit = strcmp(evsel__name(evsel), "raw_syscalls:sys_exit") == 0;

			if (raw_syscalls_sys_exit) {
				trace.raw_augmented_syscalls = true;
				goto init_augmented_syscall_tp;
			}

			if (trace.syscalls.events.augmented->priv == NULL &&
			    strstr(evsel__name(evsel), "syscalls:sys_enter")) {
				struct evsel *augmented = trace.syscalls.events.augmented;
				if (evsel__init_augmented_syscall_tp(augmented, evsel) ||
				    evsel__init_augmented_syscall_tp_args(augmented))
					goto out;
				/*
				 * Augmented is __augmented_syscalls__ BPF_OUTPUT event
				 * Above we made sure we can get from the payload the tp fields
				 * that we get from syscalls:sys_enter tracefs format file.
				 */
				augmented->handler = trace__sys_enter;
				/*
				 * Now we do the same for the *syscalls:sys_enter event so that
				 * if we handle it directly, i.e. if the BPF prog returns 0 so
				 * as not to filter it, then we'll handle it just like we would
				 * for the BPF_OUTPUT one:
				 */
				if (evsel__init_augmented_syscall_tp(evsel, evsel) ||
				    evsel__init_augmented_syscall_tp_args(evsel))
					goto out;
				evsel->handler = trace__sys_enter;
			}

			if (strstarts(evsel__name(evsel), "syscalls:sys_exit_")) {
				struct syscall_tp *sc;
init_augmented_syscall_tp:
				if (evsel__init_augmented_syscall_tp(evsel, evsel))
					goto out;
				sc = __evsel__syscall_tp(evsel);
				/*
				 * For now with BPF raw_augmented we hook into
				 * raw_syscalls:sys_enter and there we get all
				 * 6 syscall args plus the tracepoint common
				 * fields and the syscall_nr (another long).
				 * So we check if that is the case and if so
				 * don't look after the sc->args_size but
				 * always after the full raw_syscalls:sys_enter
				 * payload, which is fixed.
				 *
				 * We'll revisit this later to pass
				 * s->args_size to the BPF augmenter (now
				 * tools/perf/examples/bpf/augmented_raw_syscalls.c,
				 * so that it copies only what we need for each
				 * syscall, like what happens when we use
				 * syscalls:sys_enter_NAME, so that we reduce
				 * the kernel/userspace traffic to just what is
				 * needed for each syscall.
				 */
				if (trace.raw_augmented_syscalls)
					trace.raw_augmented_syscalls_args_size = (6 + 1) * sizeof(long) + sc->id.offset;
				evsel__init_augmented_syscall_tp_ret(evsel);
				evsel->handler = trace__sys_exit;
			}
		}
	}

	if ((argc >= 1) && (strcmp(argv[0], "record") == 0))
		return trace__record(&trace, argc-1, &argv[1]);

	/* Using just --errno-summary will trigger --summary */
	if (trace.errno_summary && !trace.summary && !trace.summary_only)
		trace.summary_only = true;

	/* summary_only implies summary option, but don't overwrite summary if set */
	if (trace.summary_only)
		trace.summary = trace.summary_only;

	if (output_name != NULL) {
		err = trace__open_output(&trace, output_name);
		if (err < 0) {
			perror("failed to create output file");
			goto out;
		}
	}

	err = evswitch__init(&trace.evswitch, trace.evlist, stderr);
	if (err)
		goto out_close;

	err = target__validate(&trace.opts.target);
	if (err) {
		target__strerror(&trace.opts.target, err, bf, sizeof(bf));
		fprintf(trace.output, "%s", bf);
		goto out_close;
	}

	err = target__parse_uid(&trace.opts.target);
	if (err) {
		target__strerror(&trace.opts.target, err, bf, sizeof(bf));
		fprintf(trace.output, "%s", bf);
		goto out_close;
	}

	if (!argc && target__none(&trace.opts.target))
		trace.opts.target.system_wide = true;

	if (input_name)
		err = trace__replay(&trace);
	else
		err = trace__run(&trace, argc, argv);

out_close:
	if (output_name != NULL)
		fclose(trace.output);
out:
	trace__exit(&trace);
	return err;
}
