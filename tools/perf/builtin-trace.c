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
#include "util/stat.h"

#include <libaudit.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <linux/futex.h>

/* For older distros: */
#ifndef MAP_STACK
# define MAP_STACK		0x20000
#endif

#ifndef MADV_HWPOISON
# define MADV_HWPOISON		100
#endif

#ifndef MADV_MERGEABLE
# define MADV_MERGEABLE		12
#endif

#ifndef MADV_UNMERGEABLE
# define MADV_UNMERGEABLE	13
#endif

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
	return *(u##bits *)(sample->raw_data + field->offset); \
}

TP_UINT_FIELD(8);
TP_UINT_FIELD(16);
TP_UINT_FIELD(32);
TP_UINT_FIELD(64);

#define TP_UINT_FIELD__SWAPPED(bits) \
static u64 tp_field__swapped_u##bits(struct tp_field *field, struct perf_sample *sample) \
{ \
	u##bits value = *(u##bits *)(sample->raw_data + field->offset); \
	return bswap_##bits(value);\
}

TP_UINT_FIELD__SWAPPED(16);
TP_UINT_FIELD__SWAPPED(32);
TP_UINT_FIELD__SWAPPED(64);

static int tp_field__init_uint(struct tp_field *field,
			       struct format_field *format_field,
			       bool needs_swap)
{
	field->offset = format_field->offset;

	switch (format_field->size) {
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

static void *tp_field__ptr(struct tp_field *field, struct perf_sample *sample)
{
	return sample->raw_data + field->offset;
}

static int tp_field__init_ptr(struct tp_field *field, struct format_field *format_field)
{
	field->offset = format_field->offset;
	field->pointer = tp_field__ptr;
	return 0;
}

struct syscall_tp {
	struct tp_field id;
	union {
		struct tp_field args, ret;
	};
};

static int perf_evsel__init_tp_uint_field(struct perf_evsel *evsel,
					  struct tp_field *field,
					  const char *name)
{
	struct format_field *format_field = perf_evsel__field(evsel, name);

	if (format_field == NULL)
		return -1;

	return tp_field__init_uint(field, format_field, evsel->needs_swap);
}

#define perf_evsel__init_sc_tp_uint_field(evsel, name) \
	({ struct syscall_tp *sc = evsel->priv;\
	   perf_evsel__init_tp_uint_field(evsel, &sc->name, #name); })

static int perf_evsel__init_tp_ptr_field(struct perf_evsel *evsel,
					 struct tp_field *field,
					 const char *name)
{
	struct format_field *format_field = perf_evsel__field(evsel, name);

	if (format_field == NULL)
		return -1;

	return tp_field__init_ptr(field, format_field);
}

#define perf_evsel__init_sc_tp_ptr_field(evsel, name) \
	({ struct syscall_tp *sc = evsel->priv;\
	   perf_evsel__init_tp_ptr_field(evsel, &sc->name, #name); })

static void perf_evsel__delete_priv(struct perf_evsel *evsel)
{
	free(evsel->priv);
	evsel->priv = NULL;
	perf_evsel__delete(evsel);
}

static int perf_evsel__init_syscall_tp(struct perf_evsel *evsel, void *handler)
{
	evsel->priv = malloc(sizeof(struct syscall_tp));
	if (evsel->priv != NULL) {
		if (perf_evsel__init_sc_tp_uint_field(evsel, id))
			goto out_delete;

		evsel->handler = handler;
		return 0;
	}

	return -ENOMEM;

out_delete:
	free(evsel->priv);
	evsel->priv = NULL;
	return -ENOENT;
}

static struct perf_evsel *perf_evsel__syscall_newtp(const char *direction, void *handler)
{
	struct perf_evsel *evsel = perf_evsel__newtp("raw_syscalls", direction);

	if (evsel) {
		if (perf_evsel__init_syscall_tp(evsel, handler))
			goto out_delete;
	}

	return evsel;

out_delete:
	perf_evsel__delete_priv(evsel);
	return NULL;
}

#define perf_evsel__sc_tp_uint(evsel, name, sample) \
	({ struct syscall_tp *fields = evsel->priv; \
	   fields->name.integer(&fields->name, sample); })

#define perf_evsel__sc_tp_ptr(evsel, name, sample) \
	({ struct syscall_tp *fields = evsel->priv; \
	   fields->name.pointer(&fields->name, sample); })

static int perf_evlist__add_syscall_newtp(struct perf_evlist *evlist,
					  void *sys_enter_handler,
					  void *sys_exit_handler)
{
	int ret = -1;
	struct perf_evsel *sys_enter, *sys_exit;

	sys_enter = perf_evsel__syscall_newtp("sys_enter", sys_enter_handler);
	if (sys_enter == NULL)
		goto out;

	if (perf_evsel__init_sc_tp_ptr_field(sys_enter, args))
		goto out_delete_sys_enter;

	sys_exit = perf_evsel__syscall_newtp("sys_exit", sys_exit_handler);
	if (sys_exit == NULL)
		goto out_delete_sys_enter;

	if (perf_evsel__init_sc_tp_uint_field(sys_exit, ret))
		goto out_delete_sys_exit;

	perf_evlist__add(evlist, sys_enter);
	perf_evlist__add(evlist, sys_exit);

	ret = 0;
out:
	return ret;

out_delete_sys_exit:
	perf_evsel__delete_priv(sys_exit);
out_delete_sys_enter:
	perf_evsel__delete_priv(sys_enter);
	goto out;
}


struct syscall_arg {
	unsigned long val;
	struct thread *thread;
	struct trace  *trace;
	void	      *parm;
	u8	      idx;
	u8	      mask;
};

struct strarray {
	int	    offset;
	int	    nr_entries;
	const char **entries;
};

#define DEFINE_STRARRAY(array) struct strarray strarray__##array = { \
	.nr_entries = ARRAY_SIZE(array), \
	.entries = array, \
}

#define DEFINE_STRARRAY_OFFSET(array, off) struct strarray strarray__##array = { \
	.offset	    = off, \
	.nr_entries = ARRAY_SIZE(array), \
	.entries = array, \
}

static size_t __syscall_arg__scnprintf_strarray(char *bf, size_t size,
						const char *intfmt,
					        struct syscall_arg *arg)
{
	struct strarray *sa = arg->parm;
	int idx = arg->val - sa->offset;

	if (idx < 0 || idx >= sa->nr_entries)
		return scnprintf(bf, size, intfmt, arg->val);

	return scnprintf(bf, size, "%s", sa->entries[idx]);
}

static size_t syscall_arg__scnprintf_strarray(char *bf, size_t size,
					      struct syscall_arg *arg)
{
	return __syscall_arg__scnprintf_strarray(bf, size, "%d", arg);
}

#define SCA_STRARRAY syscall_arg__scnprintf_strarray

static size_t syscall_arg__scnprintf_strhexarray(char *bf, size_t size,
						 struct syscall_arg *arg)
{
	return __syscall_arg__scnprintf_strarray(bf, size, "%#x", arg);
}

#define SCA_STRHEXARRAY syscall_arg__scnprintf_strhexarray

static size_t syscall_arg__scnprintf_fd(char *bf, size_t size,
					struct syscall_arg *arg);

#define SCA_FD syscall_arg__scnprintf_fd

static size_t syscall_arg__scnprintf_fd_at(char *bf, size_t size,
					   struct syscall_arg *arg)
{
	int fd = arg->val;

	if (fd == AT_FDCWD)
		return scnprintf(bf, size, "CWD");

	return syscall_arg__scnprintf_fd(bf, size, arg);
}

#define SCA_FDAT syscall_arg__scnprintf_fd_at

static size_t syscall_arg__scnprintf_close_fd(char *bf, size_t size,
					      struct syscall_arg *arg);

#define SCA_CLOSE_FD syscall_arg__scnprintf_close_fd

static size_t syscall_arg__scnprintf_hex(char *bf, size_t size,
					 struct syscall_arg *arg)
{
	return scnprintf(bf, size, "%#lx", arg->val);
}

#define SCA_HEX syscall_arg__scnprintf_hex

static size_t syscall_arg__scnprintf_mmap_prot(char *bf, size_t size,
					       struct syscall_arg *arg)
{
	int printed = 0, prot = arg->val;

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
						struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

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
						      struct syscall_arg *arg)
{
	int behavior = arg->val;

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

static size_t syscall_arg__scnprintf_flock(char *bf, size_t size,
					   struct syscall_arg *arg)
{
	int printed = 0, op = arg->val;

	if (op == 0)
		return scnprintf(bf, size, "NONE");
#define	P_CMD(cmd) \
	if ((op & LOCK_##cmd) == LOCK_##cmd) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #cmd); \
		op &= ~LOCK_##cmd; \
	}

	P_CMD(SH);
	P_CMD(EX);
	P_CMD(NB);
	P_CMD(UN);
	P_CMD(MAND);
	P_CMD(RW);
	P_CMD(READ);
	P_CMD(WRITE);
#undef P_OP

	if (op)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", op);

	return printed;
}

#define SCA_FLOCK syscall_arg__scnprintf_flock

static size_t syscall_arg__scnprintf_futex_op(char *bf, size_t size, struct syscall_arg *arg)
{
	enum syscall_futex_args {
		SCF_UADDR   = (1 << 0),
		SCF_OP	    = (1 << 1),
		SCF_VAL	    = (1 << 2),
		SCF_TIMEOUT = (1 << 3),
		SCF_UADDR2  = (1 << 4),
		SCF_VAL3    = (1 << 5),
	};
	int op = arg->val;
	int cmd = op & FUTEX_CMD_MASK;
	size_t printed = 0;

	switch (cmd) {
#define	P_FUTEX_OP(n) case FUTEX_##n: printed = scnprintf(bf, size, #n);
	P_FUTEX_OP(WAIT);	    arg->mask |= SCF_VAL3|SCF_UADDR2;		  break;
	P_FUTEX_OP(WAKE);	    arg->mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(FD);		    arg->mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(REQUEUE);	    arg->mask |= SCF_VAL3|SCF_TIMEOUT;	          break;
	P_FUTEX_OP(CMP_REQUEUE);    arg->mask |= SCF_TIMEOUT;			  break;
	P_FUTEX_OP(CMP_REQUEUE_PI); arg->mask |= SCF_TIMEOUT;			  break;
	P_FUTEX_OP(WAKE_OP);							  break;
	P_FUTEX_OP(LOCK_PI);	    arg->mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(UNLOCK_PI);	    arg->mask |= SCF_VAL3|SCF_UADDR2|SCF_TIMEOUT; break;
	P_FUTEX_OP(TRYLOCK_PI);	    arg->mask |= SCF_VAL3|SCF_UADDR2;		  break;
	P_FUTEX_OP(WAIT_BITSET);    arg->mask |= SCF_UADDR2;			  break;
	P_FUTEX_OP(WAKE_BITSET);    arg->mask |= SCF_UADDR2;			  break;
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

static const char *epoll_ctl_ops[] = { "ADD", "DEL", "MOD", };
static DEFINE_STRARRAY_OFFSET(epoll_ctl_ops, 1);

static const char *itimers[] = { "REAL", "VIRTUAL", "PROF", };
static DEFINE_STRARRAY(itimers);

static const char *whences[] = { "SET", "CUR", "END",
#ifdef SEEK_DATA
"DATA",
#endif
#ifdef SEEK_HOLE
"HOLE",
#endif
};
static DEFINE_STRARRAY(whences);

static const char *fcntl_cmds[] = {
	"DUPFD", "GETFD", "SETFD", "GETFL", "SETFL", "GETLK", "SETLK",
	"SETLKW", "SETOWN", "GETOWN", "SETSIG", "GETSIG", "F_GETLK64",
	"F_SETLK64", "F_SETLKW64", "F_SETOWN_EX", "F_GETOWN_EX",
	"F_GETOWNER_UIDS",
};
static DEFINE_STRARRAY(fcntl_cmds);

static const char *rlimit_resources[] = {
	"CPU", "FSIZE", "DATA", "STACK", "CORE", "RSS", "NPROC", "NOFILE",
	"MEMLOCK", "AS", "LOCKS", "SIGPENDING", "MSGQUEUE", "NICE", "RTPRIO",
	"RTTIME",
};
static DEFINE_STRARRAY(rlimit_resources);

static const char *sighow[] = { "BLOCK", "UNBLOCK", "SETMASK", };
static DEFINE_STRARRAY(sighow);

static const char *clockid[] = {
	"REALTIME", "MONOTONIC", "PROCESS_CPUTIME_ID", "THREAD_CPUTIME_ID",
	"MONOTONIC_RAW", "REALTIME_COARSE", "MONOTONIC_COARSE",
};
static DEFINE_STRARRAY(clockid);

static const char *socket_families[] = {
	"UNSPEC", "LOCAL", "INET", "AX25", "IPX", "APPLETALK", "NETROM",
	"BRIDGE", "ATMPVC", "X25", "INET6", "ROSE", "DECnet", "NETBEUI",
	"SECURITY", "KEY", "NETLINK", "PACKET", "ASH", "ECONET", "ATMSVC",
	"RDS", "SNA", "IRDA", "PPPOX", "WANPIPE", "LLC", "IB", "CAN", "TIPC",
	"BLUETOOTH", "IUCV", "RXRPC", "ISDN", "PHONET", "IEEE802154", "CAIF",
	"ALG", "NFC", "VSOCK",
};
static DEFINE_STRARRAY(socket_families);

#ifndef SOCK_TYPE_MASK
#define SOCK_TYPE_MASK 0xf
#endif

static size_t syscall_arg__scnprintf_socket_type(char *bf, size_t size,
						      struct syscall_arg *arg)
{
	size_t printed;
	int type = arg->val,
	    flags = type & ~SOCK_TYPE_MASK;

	type &= SOCK_TYPE_MASK;
	/*
 	 * Can't use a strarray, MIPS may override for ABI reasons.
 	 */
	switch (type) {
#define	P_SK_TYPE(n) case SOCK_##n: printed = scnprintf(bf, size, #n); break;
	P_SK_TYPE(STREAM);
	P_SK_TYPE(DGRAM);
	P_SK_TYPE(RAW);
	P_SK_TYPE(RDM);
	P_SK_TYPE(SEQPACKET);
	P_SK_TYPE(DCCP);
	P_SK_TYPE(PACKET);
#undef P_SK_TYPE
	default:
		printed = scnprintf(bf, size, "%#x", type);
	}

#define	P_SK_FLAG(n) \
	if (flags & SOCK_##n) { \
		printed += scnprintf(bf + printed, size - printed, "|%s", #n); \
		flags &= ~SOCK_##n; \
	}

	P_SK_FLAG(CLOEXEC);
	P_SK_FLAG(NONBLOCK);
#undef P_SK_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "|%#x", flags);

	return printed;
}

#define SCA_SK_TYPE syscall_arg__scnprintf_socket_type

#ifndef MSG_PROBE
#define MSG_PROBE	     0x10
#endif
#ifndef MSG_WAITFORONE
#define MSG_WAITFORONE	0x10000
#endif
#ifndef MSG_SENDPAGE_NOTLAST
#define MSG_SENDPAGE_NOTLAST 0x20000
#endif
#ifndef MSG_FASTOPEN
#define MSG_FASTOPEN	     0x20000000
#endif

static size_t syscall_arg__scnprintf_msg_flags(char *bf, size_t size,
					       struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

	if (flags == 0)
		return scnprintf(bf, size, "NONE");
#define	P_MSG_FLAG(n) \
	if (flags & MSG_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~MSG_##n; \
	}

	P_MSG_FLAG(OOB);
	P_MSG_FLAG(PEEK);
	P_MSG_FLAG(DONTROUTE);
	P_MSG_FLAG(TRYHARD);
	P_MSG_FLAG(CTRUNC);
	P_MSG_FLAG(PROBE);
	P_MSG_FLAG(TRUNC);
	P_MSG_FLAG(DONTWAIT);
	P_MSG_FLAG(EOR);
	P_MSG_FLAG(WAITALL);
	P_MSG_FLAG(FIN);
	P_MSG_FLAG(SYN);
	P_MSG_FLAG(CONFIRM);
	P_MSG_FLAG(RST);
	P_MSG_FLAG(ERRQUEUE);
	P_MSG_FLAG(NOSIGNAL);
	P_MSG_FLAG(MORE);
	P_MSG_FLAG(WAITFORONE);
	P_MSG_FLAG(SENDPAGE_NOTLAST);
	P_MSG_FLAG(FASTOPEN);
	P_MSG_FLAG(CMSG_CLOEXEC);
#undef P_MSG_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_MSG_FLAGS syscall_arg__scnprintf_msg_flags

static size_t syscall_arg__scnprintf_access_mode(char *bf, size_t size,
						 struct syscall_arg *arg)
{
	size_t printed = 0;
	int mode = arg->val;

	if (mode == F_OK) /* 0 */
		return scnprintf(bf, size, "F");
#define	P_MODE(n) \
	if (mode & n##_OK) { \
		printed += scnprintf(bf + printed, size - printed, "%s", #n); \
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

static size_t syscall_arg__scnprintf_open_flags(char *bf, size_t size,
					       struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

	if (!(flags & O_CREAT))
		arg->mask |= 1 << (arg->idx + 1); /* Mask the mode parm */

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

static size_t syscall_arg__scnprintf_eventfd_flags(char *bf, size_t size,
						   struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

	if (flags == 0)
		return scnprintf(bf, size, "NONE");
#define	P_FLAG(n) \
	if (flags & EFD_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~EFD_##n; \
	}

	P_FLAG(SEMAPHORE);
	P_FLAG(CLOEXEC);
	P_FLAG(NONBLOCK);
#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_EFD_FLAGS syscall_arg__scnprintf_eventfd_flags

static size_t syscall_arg__scnprintf_pipe_flags(char *bf, size_t size,
						struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

#define	P_FLAG(n) \
	if (flags & O_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
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

static size_t syscall_arg__scnprintf_signum(char *bf, size_t size, struct syscall_arg *arg)
{
	int sig = arg->val;

	switch (sig) {
#define	P_SIGNUM(n) case SIG##n: return scnprintf(bf, size, #n)
	P_SIGNUM(HUP);
	P_SIGNUM(INT);
	P_SIGNUM(QUIT);
	P_SIGNUM(ILL);
	P_SIGNUM(TRAP);
	P_SIGNUM(ABRT);
	P_SIGNUM(BUS);
	P_SIGNUM(FPE);
	P_SIGNUM(KILL);
	P_SIGNUM(USR1);
	P_SIGNUM(SEGV);
	P_SIGNUM(USR2);
	P_SIGNUM(PIPE);
	P_SIGNUM(ALRM);
	P_SIGNUM(TERM);
	P_SIGNUM(STKFLT);
	P_SIGNUM(CHLD);
	P_SIGNUM(CONT);
	P_SIGNUM(STOP);
	P_SIGNUM(TSTP);
	P_SIGNUM(TTIN);
	P_SIGNUM(TTOU);
	P_SIGNUM(URG);
	P_SIGNUM(XCPU);
	P_SIGNUM(XFSZ);
	P_SIGNUM(VTALRM);
	P_SIGNUM(PROF);
	P_SIGNUM(WINCH);
	P_SIGNUM(IO);
	P_SIGNUM(PWR);
	P_SIGNUM(SYS);
	default: break;
	}

	return scnprintf(bf, size, "%#x", sig);
}

#define SCA_SIGNUM syscall_arg__scnprintf_signum

#define TCGETS		0x5401

static const char *tioctls[] = {
	"TCGETS", "TCSETS", "TCSETSW", "TCSETSF", "TCGETA", "TCSETA", "TCSETAW",
	"TCSETAF", "TCSBRK", "TCXONC", "TCFLSH", "TIOCEXCL", "TIOCNXCL",
	"TIOCSCTTY", "TIOCGPGRP", "TIOCSPGRP", "TIOCOUTQ", "TIOCSTI",
	"TIOCGWINSZ", "TIOCSWINSZ", "TIOCMGET", "TIOCMBIS", "TIOCMBIC",
	"TIOCMSET", "TIOCGSOFTCAR", "TIOCSSOFTCAR", "FIONREAD", "TIOCLINUX",
	"TIOCCONS", "TIOCGSERIAL", "TIOCSSERIAL", "TIOCPKT", "FIONBIO",
	"TIOCNOTTY", "TIOCSETD", "TIOCGETD", "TCSBRKP", [0x27] = "TIOCSBRK",
	"TIOCCBRK", "TIOCGSID", "TCGETS2", "TCSETS2", "TCSETSW2", "TCSETSF2",
	"TIOCGRS485", "TIOCSRS485", "TIOCGPTN", "TIOCSPTLCK",
	"TIOCGDEV||TCGETX", "TCSETX", "TCSETXF", "TCSETXW", "TIOCSIG",
	"TIOCVHANGUP", "TIOCGPKT", "TIOCGPTLCK", "TIOCGEXCL",
	[0x50] = "FIONCLEX", "FIOCLEX", "FIOASYNC", "TIOCSERCONFIG",
	"TIOCSERGWILD", "TIOCSERSWILD", "TIOCGLCKTRMIOS", "TIOCSLCKTRMIOS",
	"TIOCSERGSTRUCT", "TIOCSERGETLSR", "TIOCSERGETMULTI", "TIOCSERSETMULTI",
	"TIOCMIWAIT", "TIOCGICOUNT", [0x60] = "FIOQSIZE",
};

static DEFINE_STRARRAY_OFFSET(tioctls, 0x5401);

#define STRARRAY(arg, name, array) \
	  .arg_scnprintf = { [arg] = SCA_STRARRAY, }, \
	  .arg_parm	 = { [arg] = &strarray__##array, }

static struct syscall_fmt {
	const char *name;
	const char *alias;
	size_t	   (*arg_scnprintf[6])(char *bf, size_t size, struct syscall_arg *arg);
	void	   *arg_parm[6];
	bool	   errmsg;
	bool	   timeout;
	bool	   hexret;
} syscall_fmts[] = {
	{ .name	    = "access",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_ACCMODE, /* mode */ }, },
	{ .name	    = "arch_prctl", .errmsg = true, .alias = "prctl", },
	{ .name	    = "brk",	    .hexret = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* brk */ }, },
	{ .name     = "clock_gettime",  .errmsg = true, STRARRAY(0, clk_id, clockid), },
	{ .name	    = "close",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_CLOSE_FD, /* fd */ }, }, 
	{ .name	    = "connect",    .errmsg = true, },
	{ .name	    = "dup",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "dup2",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "dup3",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "epoll_ctl",  .errmsg = true, STRARRAY(1, op, epoll_ctl_ops), },
	{ .name	    = "eventfd2",   .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_EFD_FLAGS, /* flags */ }, },
	{ .name	    = "faccessat",  .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, },
	{ .name	    = "fadvise64",  .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "fallocate",  .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "fchdir",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "fchmod",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "fchmodat",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, }, 
	{ .name	    = "fchown",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "fchownat",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, }, 
	{ .name	    = "fcntl",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */
			     [1] = SCA_STRARRAY, /* cmd */ },
	  .arg_parm	 = { [1] = &strarray__fcntl_cmds, /* cmd */ }, },
	{ .name	    = "fdatasync",  .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "flock",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */
			     [1] = SCA_FLOCK, /* cmd */ }, },
	{ .name	    = "fsetxattr",  .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "fstat",	    .errmsg = true, .alias = "newfstat",
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "fstatat",    .errmsg = true, .alias = "newfstatat",
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, }, 
	{ .name	    = "fstatfs",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "fsync",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "ftruncate", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "futex",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_FUTEX_OP, /* op */ }, },
	{ .name	    = "futimesat", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, }, 
	{ .name	    = "getdents",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "getdents64", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "getitimer",  .errmsg = true, STRARRAY(0, which, itimers), },
	{ .name	    = "getrlimit",  .errmsg = true, STRARRAY(0, resource, rlimit_resources), },
	{ .name	    = "ioctl",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ 
			     [1] = SCA_STRHEXARRAY, /* cmd */
			     [2] = SCA_HEX, /* arg */ },
	  .arg_parm	 = { [1] = &strarray__tioctls, /* cmd */ }, },
	{ .name	    = "kill",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "linkat",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, }, 
	{ .name	    = "lseek",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */
			     [2] = SCA_STRARRAY, /* whence */ },
	  .arg_parm	 = { [2] = &strarray__whences, /* whence */ }, },
	{ .name	    = "lstat",	    .errmsg = true, .alias = "newlstat", },
	{ .name     = "madvise",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX,	 /* start */
			     [2] = SCA_MADV_BHV, /* behavior */ }, },
	{ .name	    = "mkdirat",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, }, 
	{ .name	    = "mknodat",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* fd */ }, }, 
	{ .name	    = "mlock",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */ }, },
	{ .name	    = "mlockall",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */ }, },
	{ .name	    = "mmap",	    .hexret = true,
	  .arg_scnprintf = { [0] = SCA_HEX,	  /* addr */
			     [2] = SCA_MMAP_PROT, /* prot */
			     [3] = SCA_MMAP_FLAGS, /* flags */
			     [4] = SCA_FD, 	  /* fd */ }, },
	{ .name	    = "mprotect",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* start */
			     [2] = SCA_MMAP_PROT, /* prot */ }, },
	{ .name	    = "mremap",	    .hexret = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */
			     [4] = SCA_HEX, /* new_addr */ }, },
	{ .name	    = "munlock",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */ }, },
	{ .name	    = "munmap",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_HEX, /* addr */ }, },
	{ .name	    = "name_to_handle_at", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, }, 
	{ .name	    = "newfstatat", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, }, 
	{ .name	    = "open",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_OPEN_FLAGS, /* flags */ }, },
	{ .name	    = "open_by_handle_at", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */
			     [2] = SCA_OPEN_FLAGS, /* flags */ }, },
	{ .name	    = "openat",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */
			     [2] = SCA_OPEN_FLAGS, /* flags */ }, },
	{ .name	    = "pipe2",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_PIPE_FLAGS, /* flags */ }, },
	{ .name	    = "poll",	    .errmsg = true, .timeout = true, },
	{ .name	    = "ppoll",	    .errmsg = true, .timeout = true, },
	{ .name	    = "pread",	    .errmsg = true, .alias = "pread64",
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "preadv",	    .errmsg = true, .alias = "pread",
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "prlimit64",  .errmsg = true, STRARRAY(1, resource, rlimit_resources), },
	{ .name	    = "pwrite",	    .errmsg = true, .alias = "pwrite64",
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "pwritev",    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "read",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "readlinkat", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, }, 
	{ .name	    = "readv",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "recvfrom",   .errmsg = true,
	  .arg_scnprintf = { [3] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "recvmmsg",   .errmsg = true,
	  .arg_scnprintf = { [3] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "recvmsg",    .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "renameat",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, }, 
	{ .name	    = "rt_sigaction", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "rt_sigprocmask",  .errmsg = true, STRARRAY(0, how, sighow), },
	{ .name	    = "rt_sigqueueinfo", .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "rt_tgsigqueueinfo", .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "select",	    .errmsg = true, .timeout = true, },
	{ .name	    = "sendmmsg",    .errmsg = true,
	  .arg_scnprintf = { [3] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "sendmsg",    .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "sendto",	    .errmsg = true,
	  .arg_scnprintf = { [3] = SCA_MSG_FLAGS, /* flags */ }, },
	{ .name	    = "setitimer",  .errmsg = true, STRARRAY(0, which, itimers), },
	{ .name	    = "setrlimit",  .errmsg = true, STRARRAY(0, resource, rlimit_resources), },
	{ .name	    = "shutdown",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "socket",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_STRARRAY, /* family */
			     [1] = SCA_SK_TYPE, /* type */ },
	  .arg_parm	 = { [0] = &strarray__socket_families, /* family */ }, },
	{ .name	    = "socketpair", .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_STRARRAY, /* family */
			     [1] = SCA_SK_TYPE, /* type */ },
	  .arg_parm	 = { [0] = &strarray__socket_families, /* family */ }, },
	{ .name	    = "stat",	    .errmsg = true, .alias = "newstat", },
	{ .name	    = "symlinkat",  .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, }, 
	{ .name	    = "tgkill",	    .errmsg = true,
	  .arg_scnprintf = { [2] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "tkill",	    .errmsg = true,
	  .arg_scnprintf = { [1] = SCA_SIGNUM, /* sig */ }, },
	{ .name	    = "uname",	    .errmsg = true, .alias = "newuname", },
	{ .name	    = "unlinkat",   .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dfd */ }, },
	{ .name	    = "utimensat",  .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FDAT, /* dirfd */ }, },
	{ .name	    = "write",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
	{ .name	    = "writev",	    .errmsg = true,
	  .arg_scnprintf = { [0] = SCA_FD, /* fd */ }, }, 
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
	size_t		    (**arg_scnprintf)(char *bf, size_t size, struct syscall_arg *arg);
	void		    **arg_parm;
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
	struct {
		int	  max;
		char	  **table;
	} paths;

	struct intlist *syscall_stats;
};

static struct thread_trace *thread_trace__new(void)
{
	struct thread_trace *ttrace =  zalloc(sizeof(struct thread_trace));

	if (ttrace)
		ttrace->paths.max = -1;

	ttrace->syscall_stats = intlist__new(NULL);

	return ttrace;
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
	struct {
		int		machine;
		int		open_id;
	}			audit;
	struct {
		int		max;
		struct syscall  *table;
	} syscalls;
	struct perf_record_opts opts;
	struct machine		*host;
	u64			base_time;
	bool			full_time;
	FILE			*output;
	unsigned long		nr_events;
	struct strlist		*ev_qualifier;
	bool			not_ev_qualifier;
	bool			live;
	const char 		*last_vfs_getname;
	struct intlist		*tid_list;
	struct intlist		*pid_list;
	bool			sched;
	bool			multiple_threads;
	bool			summary;
	bool			summary_only;
	bool			show_comm;
	bool			show_tool_stats;
	double			duration_filter;
	double			runtime_ms;
	struct {
		u64		vfs_getname, proc_getname;
	} stats;
};

static int trace__set_fd_pathname(struct thread *thread, int fd, const char *pathname)
{
	struct thread_trace *ttrace = thread->priv;

	if (fd > ttrace->paths.max) {
		char **npath = realloc(ttrace->paths.table, (fd + 1) * sizeof(char *));

		if (npath == NULL)
			return -1;

		if (ttrace->paths.max != -1) {
			memset(npath + ttrace->paths.max + 1, 0,
			       (fd - ttrace->paths.max) * sizeof(char *));
		} else {
			memset(npath, 0, (fd + 1) * sizeof(char *));
		}

		ttrace->paths.table = npath;
		ttrace->paths.max   = fd;
	}

	ttrace->paths.table[fd] = strdup(pathname);

	return ttrace->paths.table[fd] != NULL ? 0 : -1;
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
	struct thread_trace *ttrace = thread->priv;

	if (ttrace == NULL)
		return NULL;

	if (fd < 0)
		return NULL;

	if ((fd > ttrace->paths.max || ttrace->paths.table[fd] == NULL))
		if (!trace->live)
			return NULL;
		++trace->stats.proc_getname;
		if (thread__read_fd_path(thread, fd)) {
			return NULL;
	}

	return ttrace->paths.table[fd];
}

static size_t syscall_arg__scnprintf_fd(char *bf, size_t size,
					struct syscall_arg *arg)
{
	int fd = arg->val;
	size_t printed = scnprintf(bf, size, "%d", fd);
	const char *path = thread__fd_path(arg->thread, fd, arg->trace);

	if (path)
		printed += scnprintf(bf + printed, size - printed, "<%s>", path);

	return printed;
}

static size_t syscall_arg__scnprintf_close_fd(char *bf, size_t size,
					      struct syscall_arg *arg)
{
	int fd = arg->val;
	size_t printed = syscall_arg__scnprintf_fd(bf, size, arg);
	struct thread_trace *ttrace = arg->thread->priv;

	if (ttrace && fd >= 0 && fd <= ttrace->paths.max) {
		free(ttrace->paths.table[fd]);
		ttrace->paths.table[fd] = NULL;
	}

	return printed;
}

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
static bool interrupted = false;

static void sig_handler(int sig)
{
	done = true;
	interrupted = sig == SIGINT;
}

static size_t trace__fprintf_entry_head(struct trace *trace, struct thread *thread,
					u64 duration, u64 tstamp, FILE *fp)
{
	size_t printed = trace__fprintf_tstamp(trace, tstamp, fp);
	printed += fprintf_duration(duration, fp);

	if (trace->multiple_threads) {
		if (trace->show_comm)
			printed += fprintf(fp, "%.14s/", thread__comm_str(thread));
		printed += fprintf(fp, "%d ", thread->tid);
	}

	return printed;
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

static int trace__symbols_init(struct trace *trace, struct perf_evlist *evlist)
{
	int err = symbol__init();

	if (err)
		return err;

	trace->host = machine__new_host();
	if (trace->host == NULL)
		return -ENOMEM;

	err = __machine__synthesize_threads(trace->host, &trace->tool, &trace->opts.target,
					    evlist->threads, trace__tool_process, false);
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

	if (sc->fmt)
		sc->arg_parm = sc->fmt->arg_parm;

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
	const char *name = audit_syscall_to_name(id, trace->audit.machine);

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
				      unsigned long *args, struct trace *trace,
				      struct thread *thread)
{
	size_t printed = 0;

	if (sc->tp_format != NULL) {
		struct format_field *field;
		u8 bit = 1;
		struct syscall_arg arg = {
			.idx	= 0,
			.mask	= 0,
			.trace  = trace,
			.thread = thread,
		};

		for (field = sc->tp_format->format.fields->next; field;
		     field = field->next, ++arg.idx, bit <<= 1) {
			if (arg.mask & bit)
				continue;
			/*
 			 * Suppress this argument if its value is zero and
 			 * and we don't have a string associated in an
 			 * strarray for it.
 			 */
			if (args[arg.idx] == 0 &&
			    !(sc->arg_scnprintf &&
			      sc->arg_scnprintf[arg.idx] == SCA_STRARRAY &&
			      sc->arg_parm[arg.idx]))
				continue;

			printed += scnprintf(bf + printed, size - printed,
					     "%s%s: ", printed ? ", " : "", field->name);
			if (sc->arg_scnprintf && sc->arg_scnprintf[arg.idx]) {
				arg.val = args[arg.idx];
				if (sc->arg_parm)
					arg.parm = sc->arg_parm[arg.idx];
				printed += sc->arg_scnprintf[arg.idx](bf + printed,
								      size - printed, &arg);
			} else {
				printed += scnprintf(bf + printed, size - printed,
						     "%ld", args[arg.idx]);
			}
		}
	} else {
		int i = 0;

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
					   struct perf_evsel *evsel, int id)
{

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

static void thread__update_stats(struct thread_trace *ttrace,
				 int id, struct perf_sample *sample)
{
	struct int_node *inode;
	struct stats *stats;
	u64 duration = 0;

	inode = intlist__findnew(ttrace->syscall_stats, id);
	if (inode == NULL)
		return;

	stats = inode->priv;
	if (stats == NULL) {
		stats = malloc(sizeof(struct stats));
		if (stats == NULL)
			return;
		init_stats(stats);
		inode->priv = stats;
	}

	if (ttrace->entry_time && sample->time > ttrace->entry_time)
		duration = sample->time - ttrace->entry_time;

	update_stats(stats, duration);
}

static int trace__sys_enter(struct trace *trace, struct perf_evsel *evsel,
			    struct perf_sample *sample)
{
	char *msg;
	void *args;
	size_t printed = 0;
	struct thread *thread;
	int id = perf_evsel__sc_tp_uint(evsel, id, sample);
	struct syscall *sc = trace__syscall_info(trace, evsel, id);
	struct thread_trace *ttrace;

	if (sc == NULL)
		return -1;

	if (sc->filtered)
		return 0;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);
	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		return -1;

	args = perf_evsel__sc_tp_ptr(evsel, args, sample);
	ttrace = thread->priv;

	if (ttrace->entry_str == NULL) {
		ttrace->entry_str = malloc(1024);
		if (!ttrace->entry_str)
			return -1;
	}

	ttrace->entry_time = sample->time;
	msg = ttrace->entry_str;
	printed += scnprintf(msg + printed, 1024 - printed, "%s(", sc->name);

	printed += syscall__scnprintf_args(sc, msg + printed, 1024 - printed,
					   args, trace, thread);

	if (!strcmp(sc->name, "exit_group") || !strcmp(sc->name, "exit")) {
		if (!trace->duration_filter && !trace->summary_only) {
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
	int id = perf_evsel__sc_tp_uint(evsel, id, sample);
	struct syscall *sc = trace__syscall_info(trace, evsel, id);
	struct thread_trace *ttrace;

	if (sc == NULL)
		return -1;

	if (sc->filtered)
		return 0;

	thread = machine__findnew_thread(trace->host, sample->pid, sample->tid);
	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		return -1;

	if (trace->summary)
		thread__update_stats(ttrace, id, sample);

	ret = perf_evsel__sc_tp_uint(evsel, ret, sample);

	if (id == trace->audit.open_id && ret >= 0 && trace->last_vfs_getname) {
		trace__set_fd_pathname(thread, ret, trace->last_vfs_getname);
		trace->last_vfs_getname = NULL;
		++trace->stats.vfs_getname;
	}

	ttrace = thread->priv;

	ttrace->exit_time = sample->time;

	if (ttrace->entry_time) {
		duration = sample->time - ttrace->entry_time;
		if (trace__filter_duration(trace, duration))
			goto out;
	} else if (trace->duration_filter)
		goto out;

	if (trace->summary_only)
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

static int trace__vfs_getname(struct trace *trace, struct perf_evsel *evsel,
			      struct perf_sample *sample)
{
	trace->last_vfs_getname = perf_evsel__rawptr(evsel, sample, "pathname");
	return 0;
}

static int trace__sched_stat_runtime(struct trace *trace, struct perf_evsel *evsel,
				     struct perf_sample *sample)
{
        u64 runtime = perf_evsel__intval(evsel, sample, "runtime");
	double runtime_ms = (double)runtime / NSEC_PER_MSEC;
	struct thread *thread = machine__findnew_thread(trace->host,
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

	tracepoint_handler handler = evsel->handler;

	if (skip_sample(trace, sample))
		return 0;

	if (!trace->full_time && trace->base_time == 0)
		trace->base_time = sample->time;

	if (handler)
		handler(trace, evsel, sample);

	return err;
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

static int trace__record(int argc, const char **argv)
{
	unsigned int rec_argc, i, j;
	const char **rec_argv;
	const char * const record_args[] = {
		"record",
		"-R",
		"-m", "1024",
		"-c", "1",
		"-e", "raw_syscalls:sys_enter,raw_syscalls:sys_exit",
	};

	rec_argc = ARRAY_SIZE(record_args) + argc;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = record_args[i];

	for (j = 0; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	return cmd_record(i, rec_argv, NULL);
}

static size_t trace__fprintf_thread_summary(struct trace *trace, FILE *fp);

static void perf_evlist__add_vfs_getname(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evsel__newtp("probe", "vfs_getname");
	if (evsel == NULL)
		return;

	if (perf_evsel__field(evsel, "pathname") == NULL) {
		perf_evsel__delete(evsel);
		return;
	}

	evsel->handler = trace__vfs_getname;
	perf_evlist__add(evlist, evsel);
}

static int trace__run(struct trace *trace, int argc, const char **argv)
{
	struct perf_evlist *evlist = perf_evlist__new();
	struct perf_evsel *evsel;
	int err = -1, i;
	unsigned long before;
	const bool forks = argc > 0;

	trace->live = true;

	if (evlist == NULL) {
		fprintf(trace->output, "Not enough memory to run!\n");
		goto out;
	}

	if (perf_evlist__add_syscall_newtp(evlist, trace__sys_enter, trace__sys_exit))
		goto out_error_tp;

	perf_evlist__add_vfs_getname(evlist);

	if (trace->sched &&
		perf_evlist__add_newtp(evlist, "sched", "sched_stat_runtime",
				trace__sched_stat_runtime))
		goto out_error_tp;

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
	if (err < 0)
		goto out_error_open;

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
				goto next_event;
			}

			if (!trace->full_time && trace->base_time == 0)
				trace->base_time = sample.time;

			if (type != PERF_RECORD_SAMPLE) {
				trace__process_event(trace, trace->host, event, &sample);
				continue;
			}

			evsel = perf_evlist__id2evsel(evlist, sample.id);
			if (evsel == NULL) {
				fprintf(trace->output, "Unknown tp ID %" PRIu64 ", skipping...\n", sample.id);
				goto next_event;
			}

			if (sample.raw_data == NULL) {
				fprintf(trace->output, "%s sample with no payload for tid: %d, cpu %d, raw_size=%d, skipping...\n",
				       perf_evsel__name(evsel), sample.tid,
				       sample.cpu, sample.raw_size);
				goto next_event;
			}

			handler = evsel->handler;
			handler(trace, evsel, &sample);
next_event:
			perf_evlist__mmap_consume(evlist, i);

			if (interrupted)
				goto out_disable;
		}
	}

	if (trace->nr_events == before) {
		int timeout = done ? 100 : -1;

		if (poll(evlist->pollfd, evlist->nr_fds, timeout) > 0)
			goto again;
	} else {
		goto again;
	}

out_disable:
	perf_evlist__disable(evlist);

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

	perf_evlist__munmap(evlist);
out_close_evlist:
	perf_evlist__close(evlist);
out_delete_maps:
	perf_evlist__delete_maps(evlist);
out_delete_evlist:
	perf_evlist__delete(evlist);
out:
	trace->live = false;
	return err;
{
	char errbuf[BUFSIZ];

out_error_tp:
	perf_evlist__strerror_tp(evlist, errno, errbuf, sizeof(errbuf));
	goto out_error;

out_error_open:
	perf_evlist__strerror_open(evlist, errno, errbuf, sizeof(errbuf));

out_error:
	fprintf(trace->output, "%s\n", errbuf);
	goto out_delete_evlist;
}
}

static int trace__replay(struct trace *trace)
{
	const struct perf_evsel_str_handler handlers[] = {
		{ "probe:vfs_getname",	     trace__vfs_getname, },
	};
	struct perf_data_file file = {
		.path  = input_name,
		.mode  = PERF_DATA_MODE_READ,
	};
	struct perf_session *session;
	struct perf_evsel *evsel;
	int err = -1;

	trace->tool.sample	  = trace__process_sample;
	trace->tool.mmap	  = perf_event__process_mmap;
	trace->tool.mmap2	  = perf_event__process_mmap2;
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

	session = perf_session__new(&file, false, &trace->tool);
	if (session == NULL)
		return -ENOMEM;

	trace->host = &session->machines.host;

	err = perf_session__set_tracepoints_handlers(session, handlers);
	if (err)
		goto out;

	evsel = perf_evlist__find_tracepoint_by_name(session->evlist,
						     "raw_syscalls:sys_enter");
	if (evsel == NULL) {
		pr_err("Data file does not have raw_syscalls:sys_enter event\n");
		goto out;
	}

	if (perf_evsel__init_syscall_tp(evsel, trace__sys_enter) < 0 ||
	    perf_evsel__init_sc_tp_ptr_field(evsel, args)) {
		pr_err("Error during initialize raw_syscalls:sys_enter event\n");
		goto out;
	}

	evsel = perf_evlist__find_tracepoint_by_name(session->evlist,
						     "raw_syscalls:sys_exit");
	if (evsel == NULL) {
		pr_err("Data file does not have raw_syscalls:sys_exit event\n");
		goto out;
	}

	if (perf_evsel__init_syscall_tp(evsel, trace__sys_exit) < 0 ||
	    perf_evsel__init_sc_tp_uint_field(evsel, ret)) {
		pr_err("Error during initialize raw_syscalls:sys_exit event\n");
		goto out;
	}

	err = parse_target_str(trace);
	if (err != 0)
		goto out;

	setup_pager();

	err = perf_session__process_events(session, &trace->tool);
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

static size_t thread__dump_stats(struct thread_trace *ttrace,
				 struct trace *trace, FILE *fp)
{
	struct stats *stats;
	size_t printed = 0;
	struct syscall *sc;
	struct int_node *inode = intlist__first(ttrace->syscall_stats);

	if (inode == NULL)
		return 0;

	printed += fprintf(fp, "\n");

	printed += fprintf(fp, "   syscall            calls      min       avg       max      stddev\n");
	printed += fprintf(fp, "                               (msec)    (msec)    (msec)        (%%)\n");
	printed += fprintf(fp, "   --------------- -------- --------- --------- ---------     ------\n");

	/* each int_node is a syscall */
	while (inode) {
		stats = inode->priv;
		if (stats) {
			double min = (double)(stats->min) / NSEC_PER_MSEC;
			double max = (double)(stats->max) / NSEC_PER_MSEC;
			double avg = avg_stats(stats);
			double pct;
			u64 n = (u64) stats->n;

			pct = avg ? 100.0 * stddev_stats(stats)/avg : 0.0;
			avg /= NSEC_PER_MSEC;

			sc = &trace->syscalls.table[inode->i];
			printed += fprintf(fp, "   %-15s", sc->name);
			printed += fprintf(fp, " %8" PRIu64 " %9.3f %9.3f",
					   n, min, avg);
			printed += fprintf(fp, " %9.3f %9.2f%%\n", max, pct);
		}

		inode = intlist__next(inode);
	}

	printed += fprintf(fp, "\n\n");

	return printed;
}

/* struct used to pass data to per-thread function */
struct summary_data {
	FILE *fp;
	struct trace *trace;
	size_t printed;
};

static int trace__fprintf_one_thread(struct thread *thread, void *priv)
{
	struct summary_data *data = priv;
	FILE *fp = data->fp;
	size_t printed = data->printed;
	struct trace *trace = data->trace;
	struct thread_trace *ttrace = thread->priv;
	const char *color;
	double ratio;

	if (ttrace == NULL)
		return 0;

	ratio = (double)ttrace->nr_events / trace->nr_events * 100.0;

	color = PERF_COLOR_NORMAL;
	if (ratio > 50.0)
		color = PERF_COLOR_RED;
	else if (ratio > 25.0)
		color = PERF_COLOR_GREEN;
	else if (ratio > 5.0)
		color = PERF_COLOR_YELLOW;

	printed += color_fprintf(fp, color, " %s (%d), ", thread__comm_str(thread), thread->tid);
	printed += fprintf(fp, "%lu events, ", ttrace->nr_events);
	printed += color_fprintf(fp, color, "%.1f%%", ratio);
	printed += fprintf(fp, ", %.3f msec\n", ttrace->runtime_ms);
	printed += thread__dump_stats(ttrace, trace, fp);

	data->printed += printed;

	return 0;
}

static size_t trace__fprintf_thread_summary(struct trace *trace, FILE *fp)
{
	struct summary_data data = {
		.fp = fp,
		.trace = trace
	};
	data.printed = trace__fprintf_threads_header(fp);

	machine__for_each_thread(trace->host, trace__fprintf_one_thread, &data);

	return data.printed;
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
		"perf trace record [<options>] [<command>]",
		"perf trace record [<options>] -- <command> [<options>]",
		NULL
	};
	struct trace trace = {
		.audit = {
			.machine = audit_detect_machine(),
			.open_id = audit_name_to_syscall("open", trace.audit.machine),
		},
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
		.show_comm = true,
	};
	const char *output_name = NULL;
	const char *ev_qualifier_str = NULL;
	const struct option trace_options[] = {
	OPT_BOOLEAN(0, "comm", &trace.show_comm,
		    "show the thread COMM next to its id"),
	OPT_BOOLEAN(0, "tool_stats", &trace.show_tool_stats, "show tool stats"),
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
	OPT_CALLBACK('m', "mmap-pages", &trace.opts.mmap_pages, "pages",
		     "number of mmap data pages",
		     perf_evlist__parse_mmap_pages),
	OPT_STRING('u', "uid", &trace.opts.target.uid_str, "user",
		   "user to profile"),
	OPT_CALLBACK(0, "duration", &trace, "float",
		     "show only events with duration > N.M ms",
		     trace__set_duration),
	OPT_BOOLEAN(0, "sched", &trace.sched, "show blocking scheduler events"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_BOOLEAN('T', "time", &trace.full_time,
		    "Show full timestamp, not time relative to first start"),
	OPT_BOOLEAN('s', "summary", &trace.summary_only,
		    "Show only syscall summary with statistics"),
	OPT_BOOLEAN('S', "with-summary", &trace.summary,
		    "Show all syscalls and summary with statistics"),
	OPT_END()
	};
	int err;
	char bf[BUFSIZ];

	if ((argc > 1) && (strcmp(argv[1], "record") == 0))
		return trace__record(argc-2, &argv[2]);

	argc = parse_options(argc, argv, trace_options, trace_usage, 0);

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
	return err;
}
