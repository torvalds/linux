// SPDX-License-Identifier: GPL-2.0
#include <trace/syscall.h>
#include <trace/events/syscalls.h>
#include <linux/kernel_stat.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>	/* for MODULE_NAME_LEN via KSYM_SYMBOL_LEN */
#include <linux/ftrace.h>
#include <linux/perf_event.h>
#include <linux/xarray.h>
#include <asm/syscall.h>

#include "trace_output.h"
#include "trace.h"

static DEFINE_MUTEX(syscall_trace_lock);

static int syscall_enter_register(struct trace_event_call *event,
				 enum trace_reg type, void *data);
static int syscall_exit_register(struct trace_event_call *event,
				 enum trace_reg type, void *data);

static struct list_head *
syscall_get_enter_fields(struct trace_event_call *call)
{
	struct syscall_metadata *entry = call->data;

	return &entry->enter_fields;
}

extern struct syscall_metadata *__start_syscalls_metadata[];
extern struct syscall_metadata *__stop_syscalls_metadata[];

static DEFINE_XARRAY(syscalls_metadata_sparse);
static struct syscall_metadata **syscalls_metadata;

#ifndef ARCH_HAS_SYSCALL_MATCH_SYM_NAME
static inline bool arch_syscall_match_sym_name(const char *sym, const char *name)
{
	/*
	 * Only compare after the "sys" prefix. Archs that use
	 * syscall wrappers may have syscalls symbols aliases prefixed
	 * with ".SyS" or ".sys" instead of "sys", leading to an unwanted
	 * mismatch.
	 */
	return !strcmp(sym + 3, name + 3);
}
#endif

#ifdef ARCH_TRACE_IGNORE_COMPAT_SYSCALLS
/*
 * Some architectures that allow for 32bit applications
 * to run on a 64bit kernel, do not map the syscalls for
 * the 32bit tasks the same as they do for 64bit tasks.
 *
 *     *cough*x86*cough*
 *
 * In such a case, instead of reporting the wrong syscalls,
 * simply ignore them.
 *
 * For an arch to ignore the compat syscalls it needs to
 * define ARCH_TRACE_IGNORE_COMPAT_SYSCALLS as well as
 * define the function arch_trace_is_compat_syscall() to let
 * the tracing system know that it should ignore it.
 */
static int
trace_get_syscall_nr(struct task_struct *task, struct pt_regs *regs)
{
	if (unlikely(arch_trace_is_compat_syscall(regs)))
		return -1;

	return syscall_get_nr(task, regs);
}
#else
static inline int
trace_get_syscall_nr(struct task_struct *task, struct pt_regs *regs)
{
	return syscall_get_nr(task, regs);
}
#endif /* ARCH_TRACE_IGNORE_COMPAT_SYSCALLS */

static __init struct syscall_metadata *
find_syscall_meta(unsigned long syscall)
{
	struct syscall_metadata **start;
	struct syscall_metadata **stop;
	char str[KSYM_SYMBOL_LEN];


	start = __start_syscalls_metadata;
	stop = __stop_syscalls_metadata;
	kallsyms_lookup(syscall, NULL, NULL, NULL, str);

	if (arch_syscall_match_sym_name(str, "sys_ni_syscall"))
		return NULL;

	for ( ; start < stop; start++) {
		if ((*start)->name && arch_syscall_match_sym_name(str, (*start)->name))
			return *start;
	}
	return NULL;
}

static struct syscall_metadata *syscall_nr_to_meta(int nr)
{
	if (IS_ENABLED(CONFIG_HAVE_SPARSE_SYSCALL_NR))
		return xa_load(&syscalls_metadata_sparse, (unsigned long)nr);

	if (!syscalls_metadata || nr >= NR_syscalls || nr < 0)
		return NULL;

	return syscalls_metadata[nr];
}

const char *get_syscall_name(int syscall)
{
	struct syscall_metadata *entry;

	entry = syscall_nr_to_meta(syscall);
	if (!entry)
		return NULL;

	return entry->name;
}

/* Added to user strings or arrays when max limit is reached */
#define EXTRA "..."

static void get_dynamic_len_ptr(struct syscall_trace_enter *trace,
				struct syscall_metadata *entry,
				int *offset_p, int *len_p, unsigned char **ptr_p)
{
	unsigned char *ptr;
	int offset = *offset_p;
	int val;

	/* This arg points to a user space string */
	ptr = (void *)trace->args + sizeof(long) * entry->nb_args + offset;
	val = *(int *)ptr;

	/* The value is a dynamic string (len << 16 | offset) */
	ptr = (void *)trace + (val & 0xffff);
	*len_p = val >> 16;
	offset += 4;

	*ptr_p = ptr;
	*offset_p = offset;
}

static enum print_line_t
sys_enter_openat_print(struct syscall_trace_enter *trace, struct syscall_metadata *entry,
		       struct trace_seq *s, struct trace_event *event)
{
	unsigned char *ptr;
	int offset = 0;
	int bits, len;
	bool done = false;
	static const struct trace_print_flags __flags[] =
		{
			{ O_TMPFILE, "O_TMPFILE" },
			{ O_WRONLY, "O_WRONLY" },
			{ O_RDWR, "O_RDWR" },
			{ O_CREAT, "O_CREAT" },
			{ O_EXCL, "O_EXCL" },
			{ O_NOCTTY, "O_NOCTTY" },
			{ O_TRUNC, "O_TRUNC" },
			{ O_APPEND, "O_APPEND" },
			{ O_NONBLOCK, "O_NONBLOCK" },
			{ O_DSYNC, "O_DSYNC" },
			{ O_DIRECT, "O_DIRECT" },
			{ O_LARGEFILE, "O_LARGEFILE" },
			{ O_DIRECTORY, "O_DIRECTORY" },
			{ O_NOFOLLOW, "O_NOFOLLOW" },
			{ O_NOATIME, "O_NOATIME" },
			{ O_CLOEXEC, "O_CLOEXEC" },
			{ -1, NULL }
		};

	trace_seq_printf(s, "%s(", entry->name);

	for (int i = 0; !done && i < entry->nb_args; i++) {

		if (trace_seq_has_overflowed(s))
			goto end;

		if (i)
			trace_seq_puts(s, ", ");

		switch (i) {
		case 2:
			bits = trace->args[2];

			trace_seq_puts(s, "flags: ");

			/* No need to show mode when not creating the file */
			if (!(bits & (O_CREAT|O_TMPFILE)))
				done = true;

			if (!(bits & O_ACCMODE)) {
				if (!bits) {
					trace_seq_puts(s, "O_RDONLY");
					continue;
				}
				trace_seq_puts(s, "O_RDONLY|");
			}

			trace_print_flags_seq(s, "|", bits, __flags);
			/*
			 * trace_print_flags_seq() adds a '\0' to the
			 * buffer, but this needs to append more to the seq.
			 */
			if (!trace_seq_has_overflowed(s))
				trace_seq_pop(s);

			continue;
		case 3:
			trace_seq_printf(s, "%s: 0%03o", entry->args[i],
					 (unsigned int)trace->args[i]);
			continue;
		}

		trace_seq_printf(s, "%s: %lu", entry->args[i],
				 trace->args[i]);

		if (!(BIT(i) & entry->user_mask))
			continue;

		get_dynamic_len_ptr(trace, entry, &offset, &len, &ptr);
		trace_seq_printf(s, " \"%.*s\"", len, ptr);
	}

	trace_seq_putc(s, ')');
end:
	trace_seq_putc(s, '\n');

	return trace_handle_return(s);
}

static enum print_line_t
print_syscall_enter(struct trace_iterator *iter, int flags,
		    struct trace_event *event)
{
	struct trace_array *tr = iter->tr;
	struct trace_seq *s = &iter->seq;
	struct trace_entry *ent = iter->ent;
	struct syscall_trace_enter *trace;
	struct syscall_metadata *entry;
	int i, syscall, val, len;
	unsigned char *ptr;
	int offset = 0;

	trace = (typeof(trace))ent;
	syscall = trace->nr;
	entry = syscall_nr_to_meta(syscall);

	if (!entry)
		goto end;

	if (entry->enter_event->event.type != ent->type) {
		WARN_ON_ONCE(1);
		goto end;
	}

	switch (entry->syscall_nr) {
	case __NR_openat:
		if (!tr || !(tr->trace_flags & TRACE_ITER(VERBOSE)))
			return sys_enter_openat_print(trace, entry, s, event);
		break;
	default:
		break;
	}

	trace_seq_printf(s, "%s(", entry->name);

	for (i = 0; i < entry->nb_args; i++) {
		bool printable = false;
		char *str;

		if (trace_seq_has_overflowed(s))
			goto end;

		if (i)
			trace_seq_puts(s, ", ");

		/* parameter types */
		if (tr && tr->trace_flags & TRACE_ITER(VERBOSE))
			trace_seq_printf(s, "%s ", entry->types[i]);

		/* parameter values */
		if (trace->args[i] < 10)
			trace_seq_printf(s, "%s: %lu", entry->args[i],
					 trace->args[i]);
		else
			trace_seq_printf(s, "%s: 0x%lx", entry->args[i],
					 trace->args[i]);

		if (!(BIT(i) & entry->user_mask))
			continue;

		get_dynamic_len_ptr(trace, entry, &offset, &len, &ptr);

		if (entry->user_arg_size < 0 || entry->user_arg_is_str) {
			trace_seq_printf(s, " \"%.*s\"", len, ptr);
			continue;
		}

		val = trace->args[entry->user_arg_size];

		str = ptr;
		trace_seq_puts(s, " (");
		for (int x = 0; x < len; x++, ptr++) {
			if (isascii(*ptr) && isprint(*ptr))
				printable = true;
			if (x)
				trace_seq_putc(s, ':');
			trace_seq_printf(s, "%02x", *ptr);
		}
		if (len < val)
			trace_seq_printf(s, ", %s", EXTRA);

		trace_seq_putc(s, ')');

		/* If nothing is printable, don't bother printing anything */
		if (!printable)
			continue;

		trace_seq_puts(s, " \"");
		for (int x = 0; x < len; x++) {
			if (isascii(str[x]) && isprint(str[x]))
				trace_seq_putc(s, str[x]);
			else
				trace_seq_putc(s, '.');
		}
		if (len < val)
			trace_seq_printf(s, "\"%s", EXTRA);
		else
			trace_seq_putc(s, '"');
	}

	trace_seq_putc(s, ')');
end:
	trace_seq_putc(s, '\n');

	return trace_handle_return(s);
}

static enum print_line_t
print_syscall_exit(struct trace_iterator *iter, int flags,
		   struct trace_event *event)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *ent = iter->ent;
	struct syscall_trace_exit *trace;
	int syscall;
	struct syscall_metadata *entry;

	trace = (typeof(trace))ent;
	syscall = trace->nr;
	entry = syscall_nr_to_meta(syscall);

	if (!entry) {
		trace_seq_putc(s, '\n');
		goto out;
	}

	if (entry->exit_event->event.type != ent->type) {
		WARN_ON_ONCE(1);
		return TRACE_TYPE_UNHANDLED;
	}

	trace_seq_printf(s, "%s -> 0x%lx\n", entry->name,
				trace->ret);

 out:
	return trace_handle_return(s);
}

#define SYSCALL_FIELD(_type, _name) {					\
	.type = #_type, .name = #_name,					\
	.size = sizeof(_type), .align = __alignof__(_type),		\
	.is_signed = is_signed_type(_type), .filter_type = FILTER_OTHER }

/* When len=0, we just calculate the needed length */
#define LEN_OR_ZERO (len ? len - pos : 0)

static int __init
sys_enter_openat_print_fmt(struct syscall_metadata *entry, char *buf, int len)
{
	int pos = 0;

	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"\"dfd: 0x%%08lx, filename: 0x%%08lx \\\"%%s\\\", flags: %%s%%s, mode: 0%%03o\",");
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			" ((unsigned long)(REC->dfd)),");
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			" ((unsigned long)(REC->filename)),");
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			" __get_str(__filename_val),");
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			" (REC->flags & ~3) && !(REC->flags & 3) ? \"O_RDONLY|\" : \"\", ");
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			" REC->flags ? __print_flags(REC->flags, \"|\", ");
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_WRONLY\" }, ", O_WRONLY);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_RDWR\" }, ", O_RDWR);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_CREAT\" }, ", O_CREAT);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_EXCL\" }, ", O_EXCL);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_NOCTTY\" }, ", O_NOCTTY);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_TRUNC\" }, ", O_TRUNC);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_APPEND\" }, ", O_APPEND);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_NONBLOCK\" }, ", O_NONBLOCK);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_DSYNC\" }, ", O_DSYNC);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_DIRECT\" }, ", O_DIRECT);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_LARGEFILE\" }, ", O_LARGEFILE);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_DIRECTORY\" }, ", O_DIRECTORY);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_NOFOLLOW\" }, ", O_NOFOLLOW);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_NOATIME\" }, ", O_NOATIME);
	pos += snprintf(buf + pos, LEN_OR_ZERO,
			"{ 0x%x, \"O_CLOEXEC\" }) : \"O_RDONLY\", ", O_CLOEXEC);

	pos += snprintf(buf + pos, LEN_OR_ZERO,
			" ((unsigned long)(REC->mode))");
	return pos;
}

static int __init
__set_enter_print_fmt(struct syscall_metadata *entry, char *buf, int len)
{
	bool is_string = entry->user_arg_is_str;
	int i;
	int pos = 0;

	switch (entry->syscall_nr) {
	case __NR_openat:
		return sys_enter_openat_print_fmt(entry, buf, len);
	default:
		break;
	}

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"");
	for (i = 0; i < entry->nb_args; i++) {
		if (i)
			pos += snprintf(buf + pos, LEN_OR_ZERO, ", ");
		pos += snprintf(buf + pos, LEN_OR_ZERO, "%s: 0x%%0%zulx",
				entry->args[i], sizeof(unsigned long));

		if (!(BIT(i) & entry->user_mask))
			continue;

		/* Add the format for the user space string or array */
		if (entry->user_arg_size < 0 || is_string)
			pos += snprintf(buf + pos, LEN_OR_ZERO, " \\\"%%s\\\"");
		else
			pos += snprintf(buf + pos, LEN_OR_ZERO, " (%%s)");
	}
	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"");

	for (i = 0; i < entry->nb_args; i++) {
		pos += snprintf(buf + pos, LEN_OR_ZERO,
				", ((unsigned long)(REC->%s))", entry->args[i]);
		if (!(BIT(i) & entry->user_mask))
			continue;
		/* The user space data for arg has name __<arg>_val */
		if (entry->user_arg_size < 0 || is_string) {
			pos += snprintf(buf + pos, LEN_OR_ZERO, ", __get_str(__%s_val)",
					entry->args[i]);
		} else {
			pos += snprintf(buf + pos, LEN_OR_ZERO, ", __print_dynamic_array(__%s_val, 1)",
					entry->args[i]);
		}
	}

#undef LEN_OR_ZERO

	/* return the length of print_fmt */
	return pos;
}

static int __init set_syscall_print_fmt(struct trace_event_call *call)
{
	char *print_fmt;
	int len;
	struct syscall_metadata *entry = call->data;

	if (entry->enter_event != call) {
		call->print_fmt = "\"0x%lx\", REC->ret";
		return 0;
	}

	/* First: called with 0 length to calculate the needed length */
	len = __set_enter_print_fmt(entry, NULL, 0);

	print_fmt = kmalloc(len + 1, GFP_KERNEL);
	if (!print_fmt)
		return -ENOMEM;

	/* Second: actually write the @print_fmt */
	__set_enter_print_fmt(entry, print_fmt, len + 1);
	call->print_fmt = print_fmt;

	return 0;
}

static void __init free_syscall_print_fmt(struct trace_event_call *call)
{
	struct syscall_metadata *entry = call->data;

	if (entry->enter_event == call)
		kfree(call->print_fmt);
}

static int __init syscall_enter_define_fields(struct trace_event_call *call)
{
	struct syscall_trace_enter trace;
	struct syscall_metadata *meta = call->data;
	unsigned long mask;
	char *arg;
	int offset = offsetof(typeof(trace), args);
	int ret = 0;
	int len;
	int i;

	for (i = 0; i < meta->nb_args; i++) {
		ret = trace_define_field(call, meta->types[i],
					 meta->args[i], offset,
					 sizeof(unsigned long), 0,
					 FILTER_OTHER);
		if (ret)
			break;
		offset += sizeof(unsigned long);
	}

	if (ret || !meta->user_mask)
		return ret;

	mask = meta->user_mask;

	while (mask) {
		int idx = ffs(mask) - 1;
		mask &= ~BIT(idx);

		/*
		 * User space data is faulted into a temporary buffer and then
		 * added as a dynamic string or array to the end of the event.
		 * The user space data name for the arg pointer is
		 * "__<arg>_val".
		 */
		len = strlen(meta->args[idx]) + sizeof("___val");
		arg = kmalloc(len, GFP_KERNEL);
		if (WARN_ON_ONCE(!arg)) {
			meta->user_mask = 0;
			return -ENOMEM;
		}

		snprintf(arg, len, "__%s_val", meta->args[idx]);

		ret = trace_define_field(call, "__data_loc char[]",
					 arg, offset, sizeof(int), 0,
					 FILTER_OTHER);
		if (ret) {
			kfree(arg);
			break;
		}
		offset += 4;
	}
	return ret;
}

/*
 * Create a per CPU temporary buffer to copy user space pointers into.
 *
 * SYSCALL_FAULT_USER_MAX is the amount to copy from user space.
 *  (defined in kernel/trace/trace.h)

 * SYSCALL_FAULT_ARG_SZ is the amount to copy from user space plus the
 *   nul terminating byte and possibly appended EXTRA (4 bytes).
 *
 * SYSCALL_FAULT_BUF_SZ holds the size of the per CPU buffer to use
 * to copy memory from user space addresses into that will hold
 * 3 args as only 3 args are allowed to be copied from system calls.
 */
#define SYSCALL_FAULT_ARG_SZ (SYSCALL_FAULT_USER_MAX + 1 + 4)
#define SYSCALL_FAULT_MAX_CNT 3
#define SYSCALL_FAULT_BUF_SZ (SYSCALL_FAULT_ARG_SZ * SYSCALL_FAULT_MAX_CNT)

/* Use the tracing per CPU buffer infrastructure to copy from user space */
struct syscall_user_buffer {
	struct trace_user_buf_info	buf;
	struct rcu_head			rcu;
};

static struct syscall_user_buffer *syscall_buffer;

static int syscall_fault_buffer_enable(void)
{
	struct syscall_user_buffer *sbuf;
	int ret;

	lockdep_assert_held(&syscall_trace_lock);

	if (syscall_buffer) {
		trace_user_fault_get(&syscall_buffer->buf);
		return 0;
	}

	sbuf = kmalloc(sizeof(*sbuf), GFP_KERNEL);
	if (!sbuf)
		return -ENOMEM;

	ret = trace_user_fault_init(&sbuf->buf, SYSCALL_FAULT_BUF_SZ);
	if (ret < 0) {
		kfree(sbuf);
		return ret;
	}

	WRITE_ONCE(syscall_buffer, sbuf);

	return 0;
}

static void rcu_free_syscall_buffer(struct rcu_head *rcu)
{
	struct syscall_user_buffer *sbuf =
		container_of(rcu, struct syscall_user_buffer, rcu);

	trace_user_fault_destroy(&sbuf->buf);
	kfree(sbuf);
}


static void syscall_fault_buffer_disable(void)
{
	struct syscall_user_buffer *sbuf = syscall_buffer;

	lockdep_assert_held(&syscall_trace_lock);

	if (trace_user_fault_put(&sbuf->buf))
		return;

	WRITE_ONCE(syscall_buffer, NULL);
	call_rcu_tasks_trace(&sbuf->rcu, rcu_free_syscall_buffer);
}

struct syscall_args {
	char		*ptr_array[SYSCALL_FAULT_MAX_CNT];
	int		read[SYSCALL_FAULT_MAX_CNT];
	int		uargs;
};

static int syscall_copy_user(char *buf, const char __user *ptr,
			     size_t size, void *data)
{
	struct syscall_args *args = data;
	int ret;

	for (int i = 0; i < args->uargs; i++, buf += SYSCALL_FAULT_ARG_SZ) {
		ptr = (char __user *)args->ptr_array[i];
		ret = strncpy_from_user(buf, ptr, size);
		args->read[i] = ret;
	}
	return 0;
}

static int syscall_copy_user_array(char *buf, const char __user *ptr,
				   size_t size, void *data)
{
	struct syscall_args *args = data;
	int ret;

	for (int i = 0; i < args->uargs; i++, buf += SYSCALL_FAULT_ARG_SZ) {
		ptr = (char __user *)args->ptr_array[i];
		ret = __copy_from_user(buf, ptr, size);
		args->read[i] = ret ? -1 : size;
	}
	return 0;
}

static char *sys_fault_user(unsigned int buf_size,
			    struct syscall_metadata *sys_data,
			    struct syscall_user_buffer *sbuf,
			    unsigned long *args,
			    unsigned int data_size[SYSCALL_FAULT_MAX_CNT])
{
	trace_user_buf_copy syscall_copy = syscall_copy_user;
	unsigned long mask = sys_data->user_mask;
	unsigned long size = SYSCALL_FAULT_ARG_SZ - 1;
	struct syscall_args sargs;
	bool array = false;
	char *buffer;
	char *buf;
	int ret;
	int i = 0;

	/* The extra is appended to the user data in the buffer */
	BUILD_BUG_ON(SYSCALL_FAULT_USER_MAX + sizeof(EXTRA) >=
		     SYSCALL_FAULT_ARG_SZ);

	/*
	 * If this system call event has a size argument, use
	 * it to define how much of user space memory to read,
	 * and read it as an array and not a string.
	 */
	if (sys_data->user_arg_size >= 0) {
		array = true;
		size = args[sys_data->user_arg_size];
		if (size > SYSCALL_FAULT_ARG_SZ - 1)
			size = SYSCALL_FAULT_ARG_SZ - 1;
		syscall_copy = syscall_copy_user_array;
	}

	while (mask) {
		int idx = ffs(mask) - 1;
		mask &= ~BIT(idx);

		if (WARN_ON_ONCE(i == SYSCALL_FAULT_MAX_CNT))
			break;

		/* Get the pointer to user space memory to read */
		sargs.ptr_array[i++] = (char *)args[idx];
	}

	sargs.uargs = i;

	/* Clear the values that are not used */
	for (; i < SYSCALL_FAULT_MAX_CNT; i++) {
		data_size[i] = -1; /* Denotes no pointer */
	}

	/* A zero size means do not even try */
	if (!buf_size)
		return NULL;

	buffer = trace_user_fault_read(&sbuf->buf, NULL, size,
				       syscall_copy, &sargs);
	if (!buffer)
		return NULL;

	buf = buffer;
	for (i = 0; i < sargs.uargs; i++, buf += SYSCALL_FAULT_ARG_SZ) {

		ret = sargs.read[i];
		if (ret < 0)
			continue;
		buf[ret] = '\0';

		/* For strings, replace any non-printable characters with '.' */
		if (!array) {
			for (int x = 0; x < ret; x++) {
				if (!isprint(buf[x]))
					buf[x] = '.';
			}

			size = min(buf_size, SYSCALL_FAULT_USER_MAX);

			/*
			 * If the text was truncated due to our max limit,
			 * add "..." to the string.
			 */
			if (ret > size) {
				strscpy(buf + size, EXTRA, sizeof(EXTRA));
				ret = size + sizeof(EXTRA);
			} else {
				buf[ret++] = '\0';
			}
		} else {
			ret = min((unsigned int)ret, buf_size);
		}
		data_size[i] = ret;
	}

	return buffer;
}

static int
syscall_get_data(struct syscall_metadata *sys_data, unsigned long *args,
		 char **buffer, int *size, int *user_sizes, int *uargs,
		 int buf_size)
{
	struct syscall_user_buffer *sbuf;
	int i;

	/* If the syscall_buffer is NULL, tracing is being shutdown */
	sbuf = READ_ONCE(syscall_buffer);
	if (!sbuf)
		return -1;

	*buffer = sys_fault_user(buf_size, sys_data, sbuf, args, user_sizes);
	/*
	 * user_size is the amount of data to append.
	 * Need to add 4 for the meta field that points to
	 * the user memory at the end of the event and also
	 * stores its size.
	 */
	for (i = 0; i < SYSCALL_FAULT_MAX_CNT; i++) {
		if (user_sizes[i] < 0)
			break;
		*size += user_sizes[i] + 4;
	}
	/* Save the number of user read arguments of this syscall */
	*uargs = i;
	return 0;
}

static void syscall_put_data(struct syscall_metadata *sys_data,
			     struct syscall_trace_enter *entry,
			     char *buffer, int size, int *user_sizes, int uargs)
{
	char *buf = buffer;
	void *ptr;
	int val;

	/*
	 * Set the pointer to point to the meta data of the event
	 * that has information about the stored user space memory.
	 */
	ptr = (void *)entry->args + sizeof(unsigned long) * sys_data->nb_args;

	/*
	 * The meta data will store the offset of the user data from
	 * the beginning of the event. That is after the static arguments
	 * and the meta data fields.
	 */
	val = (ptr - (void *)entry) + 4 * uargs;

	for (int i = 0; i < uargs; i++) {

		if (i)
			val += user_sizes[i - 1];

		/* Store the offset and the size into the meta data */
		*(int *)ptr = val | (user_sizes[i] << 16);

		/* Skip the meta data */
		ptr += 4;
	}

	for (int i = 0; i < uargs; i++, buf += SYSCALL_FAULT_ARG_SZ) {
		/* Nothing to do if the user space was empty or faulted */
		if (!user_sizes[i])
			continue;

		memcpy(ptr, buf, user_sizes[i]);
		ptr += user_sizes[i];
	}
}

static void ftrace_syscall_enter(void *data, struct pt_regs *regs, long id)
{
	struct trace_array *tr = data;
	struct trace_event_file *trace_file;
	struct syscall_trace_enter *entry;
	struct syscall_metadata *sys_data;
	struct trace_event_buffer fbuffer;
	unsigned long args[6];
	char *user_ptr;
	int user_sizes[SYSCALL_FAULT_MAX_CNT] = {};
	int syscall_nr;
	int size = 0;
	int uargs = 0;
	bool mayfault;

	/*
	 * Syscall probe called with preemption enabled, but the ring
	 * buffer and per-cpu data require preemption to be disabled.
	 */
	might_fault();

	syscall_nr = trace_get_syscall_nr(current, regs);
	if (syscall_nr < 0 || syscall_nr >= NR_syscalls)
		return;

	trace_file = READ_ONCE(tr->enter_syscall_files[syscall_nr]);
	if (!trace_file)
		return;

	if (trace_trigger_soft_disabled(trace_file))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	/* Check if this syscall event faults in user space memory */
	mayfault = sys_data->user_mask != 0;

	guard(preempt_notrace)();

	syscall_get_arguments(current, regs, args);

	if (mayfault) {
		if (syscall_get_data(sys_data, args, &user_ptr,
				     &size, user_sizes, &uargs, tr->syscall_buf_sz) < 0)
			return;
	}

	size += sizeof(*entry) + sizeof(unsigned long) * sys_data->nb_args;

	entry = trace_event_buffer_reserve(&fbuffer, trace_file, size);
	if (!entry)
		return;

	entry = ring_buffer_event_data(fbuffer.event);
	entry->nr = syscall_nr;

	memcpy(entry->args, args, sizeof(unsigned long) * sys_data->nb_args);

	if (mayfault)
		syscall_put_data(sys_data, entry, user_ptr, size, user_sizes, uargs);

	trace_event_buffer_commit(&fbuffer);
}

static void ftrace_syscall_exit(void *data, struct pt_regs *regs, long ret)
{
	struct trace_array *tr = data;
	struct trace_event_file *trace_file;
	struct syscall_trace_exit *entry;
	struct syscall_metadata *sys_data;
	struct trace_event_buffer fbuffer;
	int syscall_nr;

	/*
	 * Syscall probe called with preemption enabled, but the ring
	 * buffer and per-cpu data require preemption to be disabled.
	 */
	might_fault();
	guard(preempt_notrace)();

	syscall_nr = trace_get_syscall_nr(current, regs);
	if (syscall_nr < 0 || syscall_nr >= NR_syscalls)
		return;

	trace_file = READ_ONCE(tr->exit_syscall_files[syscall_nr]);
	if (!trace_file)
		return;

	if (trace_trigger_soft_disabled(trace_file))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	entry = trace_event_buffer_reserve(&fbuffer, trace_file, sizeof(*entry));
	if (!entry)
		return;

	entry = ring_buffer_event_data(fbuffer.event);
	entry->nr = syscall_nr;
	entry->ret = syscall_get_return_value(current, regs);

	trace_event_buffer_commit(&fbuffer);
}

static int reg_event_syscall_enter(struct trace_event_file *file,
				   struct trace_event_call *call)
{
	struct syscall_metadata *sys_data = call->data;
	struct trace_array *tr = file->tr;
	int ret = 0;
	int num;

	num = sys_data->syscall_nr;
	if (WARN_ON_ONCE(num < 0 || num >= NR_syscalls))
		return -ENOSYS;
	guard(mutex)(&syscall_trace_lock);
	if (sys_data->user_mask) {
		ret = syscall_fault_buffer_enable();
		if (ret < 0)
			return ret;
	}
	if (!tr->sys_refcount_enter) {
		ret = register_trace_sys_enter(ftrace_syscall_enter, tr);
		if (ret < 0) {
			if (sys_data->user_mask)
				syscall_fault_buffer_disable();
			return ret;
		}
	}
	WRITE_ONCE(tr->enter_syscall_files[num], file);
	tr->sys_refcount_enter++;
	return 0;
}

static void unreg_event_syscall_enter(struct trace_event_file *file,
				      struct trace_event_call *call)
{
	struct syscall_metadata *sys_data = call->data;
	struct trace_array *tr = file->tr;
	int num;

	num = sys_data->syscall_nr;
	if (WARN_ON_ONCE(num < 0 || num >= NR_syscalls))
		return;
	guard(mutex)(&syscall_trace_lock);
	tr->sys_refcount_enter--;
	WRITE_ONCE(tr->enter_syscall_files[num], NULL);
	if (!tr->sys_refcount_enter)
		unregister_trace_sys_enter(ftrace_syscall_enter, tr);
	if (sys_data->user_mask)
		syscall_fault_buffer_disable();
}

static int reg_event_syscall_exit(struct trace_event_file *file,
				  struct trace_event_call *call)
{
	struct trace_array *tr = file->tr;
	int ret = 0;
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;
	if (WARN_ON_ONCE(num < 0 || num >= NR_syscalls))
		return -ENOSYS;
	mutex_lock(&syscall_trace_lock);
	if (!tr->sys_refcount_exit)
		ret = register_trace_sys_exit(ftrace_syscall_exit, tr);
	if (!ret) {
		WRITE_ONCE(tr->exit_syscall_files[num], file);
		tr->sys_refcount_exit++;
	}
	mutex_unlock(&syscall_trace_lock);
	return ret;
}

static void unreg_event_syscall_exit(struct trace_event_file *file,
				     struct trace_event_call *call)
{
	struct trace_array *tr = file->tr;
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;
	if (WARN_ON_ONCE(num < 0 || num >= NR_syscalls))
		return;
	mutex_lock(&syscall_trace_lock);
	tr->sys_refcount_exit--;
	WRITE_ONCE(tr->exit_syscall_files[num], NULL);
	if (!tr->sys_refcount_exit)
		unregister_trace_sys_exit(ftrace_syscall_exit, tr);
	mutex_unlock(&syscall_trace_lock);
}

/*
 * For system calls that reference user space memory that can
 * be recorded into the event, set the system call meta data's user_mask
 * to the "args" index that points to the user space memory to retrieve.
 */
static void check_faultable_syscall(struct trace_event_call *call, int nr)
{
	struct syscall_metadata *sys_data = call->data;
	unsigned long mask;

	/* Only work on entry */
	if (sys_data->enter_event != call)
		return;

	sys_data->user_arg_size = -1;

	switch (nr) {
	/* user arg 1 with size arg at 2 */
	case __NR_write:
#ifdef __NR_mq_timedsend
	case __NR_mq_timedsend:
#endif
	case __NR_pwrite64:
		sys_data->user_mask = BIT(1);
		sys_data->user_arg_size = 2;
		break;
	/* user arg 0 with size arg at 1 as string */
	case __NR_setdomainname:
	case __NR_sethostname:
		sys_data->user_mask = BIT(0);
		sys_data->user_arg_size = 1;
		sys_data->user_arg_is_str = 1;
		break;
#ifdef __NR_kexec_file_load
	/* user arg 4 with size arg at 3 as string */
	case __NR_kexec_file_load:
		sys_data->user_mask = BIT(4);
		sys_data->user_arg_size = 3;
		sys_data->user_arg_is_str = 1;
		break;
#endif
	/* user arg at position 0 */
#ifdef __NR_access
	case __NR_access:
#endif
	case __NR_acct:
	case __NR_chdir:
#ifdef  __NR_chown
	case __NR_chown:
#endif
#ifdef  __NR_chmod
	case __NR_chmod:
#endif
	case __NR_chroot:
#ifdef __NR_creat
	case __NR_creat:
#endif
	case __NR_delete_module:
	case __NR_execve:
	case __NR_fsopen:
#ifdef __NR_lchown
	case __NR_lchown:
#endif
#ifdef __NR_open
	case __NR_open:
#endif
	case __NR_memfd_create:
#ifdef __NR_mkdir
	case __NR_mkdir:
#endif
#ifdef __NR_mknod
	case __NR_mknod:
#endif
	case __NR_mq_open:
	case __NR_mq_unlink:
#ifdef __NR_readlink
	case __NR_readlink:
#endif
#ifdef  __NR_rmdir
	case __NR_rmdir:
#endif
	case __NR_shmdt:
#ifdef __NR_statfs
	case __NR_statfs:
#endif
	case __NR_swapon:
	case __NR_swapoff:
#ifdef __NR_truncate
	case __NR_truncate:
#endif
#ifdef __NR_unlink
	case __NR_unlink:
#endif
	case __NR_umount2:
#ifdef __NR_utime
	case __NR_utime:
#endif
#ifdef __NR_utimes
	case __NR_utimes:
#endif
		sys_data->user_mask = BIT(0);
		break;
	/* user arg at position 1 */
	case __NR_execveat:
	case __NR_faccessat:
	case __NR_faccessat2:
	case __NR_finit_module:
	case __NR_fchmodat:
	case __NR_fchmodat2:
	case __NR_fchownat:
	case __NR_fgetxattr:
	case __NR_flistxattr:
	case __NR_fsetxattr:
	case __NR_fspick:
	case __NR_fremovexattr:
#ifdef __NR_futimesat
	case __NR_futimesat:
#endif
	case __NR_inotify_add_watch:
	case __NR_mkdirat:
	case __NR_mknodat:
	case __NR_mount_setattr:
	case __NR_name_to_handle_at:
#ifdef __NR_newfstatat
	case __NR_newfstatat:
#endif
	case __NR_openat:
	case __NR_openat2:
	case __NR_open_tree:
	case __NR_open_tree_attr:
	case __NR_readlinkat:
	case __NR_quotactl:
	case __NR_syslog:
	case __NR_statx:
	case __NR_unlinkat:
#ifdef __NR_utimensat
	case __NR_utimensat:
#endif
		sys_data->user_mask = BIT(1);
		break;
	/* user arg at position 2 */
	case __NR_init_module:
	case __NR_fsconfig:
		sys_data->user_mask = BIT(2);
		break;
	/* user arg at position 4 */
	case __NR_fanotify_mark:
		sys_data->user_mask = BIT(4);
		break;
	/* 2 user args, 0 and 1 */
	case __NR_add_key:
	case __NR_getxattr:
	case __NR_lgetxattr:
	case __NR_lremovexattr:
#ifdef __NR_link
	case __NR_link:
#endif
	case __NR_listxattr:
	case __NR_llistxattr:
	case __NR_lsetxattr:
	case __NR_pivot_root:
	case __NR_removexattr:
#ifdef __NR_rename
	case __NR_rename:
#endif
	case __NR_request_key:
	case __NR_setxattr:
#ifdef __NR_symlink
	case __NR_symlink:
#endif
		sys_data->user_mask = BIT(0) | BIT(1);
		break;
	/* 2 user args, 0 and 2 */
	case __NR_symlinkat:
		sys_data->user_mask = BIT(0) | BIT(2);
		break;
	/* 2 user args, 1 and 3 */
	case __NR_getxattrat:
	case __NR_linkat:
	case __NR_listxattrat:
	case __NR_move_mount:
#ifdef __NR_renameat
	case __NR_renameat:
#endif
	case __NR_renameat2:
	case __NR_removexattrat:
	case __NR_setxattrat:
		sys_data->user_mask = BIT(1) | BIT(3);
		break;
	case __NR_mount: /* Just dev_name and dir_name, TODO add type */
		sys_data->user_mask = BIT(0) | BIT(1) | BIT(2);
		break;
	default:
		sys_data->user_mask = 0;
		return;
	}

	if (sys_data->user_arg_size < 0)
		return;

	/*
	 * The user_arg_size can only be used when the system call
	 * is reading only a single address from user space.
	 */
	mask = sys_data->user_mask;
	if (WARN_ON(mask & (mask - 1)))
		sys_data->user_arg_size = -1;
}

static int __init init_syscall_trace(struct trace_event_call *call)
{
	int id;
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;
	if (num < 0 || num >= NR_syscalls) {
		pr_debug("syscall %s metadata not mapped, disabling ftrace event\n",
				((struct syscall_metadata *)call->data)->name);
		return -ENOSYS;
	}

	check_faultable_syscall(call, num);

	if (set_syscall_print_fmt(call) < 0)
		return -ENOMEM;

	id = trace_event_raw_init(call);

	if (id < 0) {
		free_syscall_print_fmt(call);
		return id;
	}

	return id;
}

static struct trace_event_fields __refdata syscall_enter_fields_array[] = {
	SYSCALL_FIELD(int, __syscall_nr),
	{ .type = TRACE_FUNCTION_TYPE,
	  .define_fields = syscall_enter_define_fields },
	{}
};

struct trace_event_functions enter_syscall_print_funcs = {
	.trace		= print_syscall_enter,
};

struct trace_event_functions exit_syscall_print_funcs = {
	.trace		= print_syscall_exit,
};

struct trace_event_class __refdata event_class_syscall_enter = {
	.system		= "syscalls",
	.reg		= syscall_enter_register,
	.fields_array	= syscall_enter_fields_array,
	.get_fields	= syscall_get_enter_fields,
	.raw_init	= init_syscall_trace,
};

struct trace_event_class __refdata event_class_syscall_exit = {
	.system		= "syscalls",
	.reg		= syscall_exit_register,
	.fields_array	= (struct trace_event_fields[]){
		SYSCALL_FIELD(int, __syscall_nr),
		SYSCALL_FIELD(long, ret),
		{}
	},
	.fields		= LIST_HEAD_INIT(event_class_syscall_exit.fields),
	.raw_init	= init_syscall_trace,
};

unsigned long __init __weak arch_syscall_addr(int nr)
{
	return (unsigned long)sys_call_table[nr];
}

void __init init_ftrace_syscalls(void)
{
	struct syscall_metadata *meta;
	unsigned long addr;
	int i;
	void *ret;

	if (!IS_ENABLED(CONFIG_HAVE_SPARSE_SYSCALL_NR)) {
		syscalls_metadata = kcalloc(NR_syscalls,
					sizeof(*syscalls_metadata),
					GFP_KERNEL);
		if (!syscalls_metadata) {
			WARN_ON(1);
			return;
		}
	}

	for (i = 0; i < NR_syscalls; i++) {
		addr = arch_syscall_addr(i);
		meta = find_syscall_meta(addr);
		if (!meta)
			continue;

		meta->syscall_nr = i;

		if (!IS_ENABLED(CONFIG_HAVE_SPARSE_SYSCALL_NR)) {
			syscalls_metadata[i] = meta;
		} else {
			ret = xa_store(&syscalls_metadata_sparse, i, meta,
					GFP_KERNEL);
			WARN(xa_is_err(ret),
				"Syscall memory allocation failed\n");
		}

	}
}

#ifdef CONFIG_PERF_EVENTS

static DECLARE_BITMAP(enabled_perf_enter_syscalls, NR_syscalls);
static DECLARE_BITMAP(enabled_perf_exit_syscalls, NR_syscalls);
static int sys_perf_refcount_enter;
static int sys_perf_refcount_exit;

static int perf_call_bpf_enter(struct trace_event_call *call, struct pt_regs *regs,
			       struct syscall_metadata *sys_data,
			       struct syscall_trace_enter *rec)
{
	struct syscall_tp_t {
		struct trace_entry ent;
		int syscall_nr;
		unsigned long args[SYSCALL_DEFINE_MAXARGS];
	} __aligned(8) param;
	int i;

	BUILD_BUG_ON(sizeof(param.ent) < sizeof(void *));

	/* bpf prog requires 'regs' to be the first member in the ctx (a.k.a. &param) */
	perf_fetch_caller_regs(regs);
	*(struct pt_regs **)&param = regs;
	param.syscall_nr = rec->nr;
	for (i = 0; i < sys_data->nb_args; i++)
		param.args[i] = rec->args[i];
	return trace_call_bpf(call, &param);
}

static void perf_syscall_enter(void *ignore, struct pt_regs *regs, long id)
{
	struct syscall_metadata *sys_data;
	struct syscall_trace_enter *rec;
	struct pt_regs *fake_regs;
	struct hlist_head *head;
	unsigned long args[6];
	bool valid_prog_array;
	bool mayfault;
	char *user_ptr;
	int user_sizes[SYSCALL_FAULT_MAX_CNT] = {};
	int buf_size = CONFIG_TRACE_SYSCALL_BUF_SIZE_DEFAULT;
	int syscall_nr;
	int rctx;
	int size = 0;
	int uargs = 0;

	/*
	 * Syscall probe called with preemption enabled, but the ring
	 * buffer and per-cpu data require preemption to be disabled.
	 */
	might_fault();
	guard(preempt_notrace)();

	syscall_nr = trace_get_syscall_nr(current, regs);
	if (syscall_nr < 0 || syscall_nr >= NR_syscalls)
		return;
	if (!test_bit(syscall_nr, enabled_perf_enter_syscalls))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	syscall_get_arguments(current, regs, args);

	/* Check if this syscall event faults in user space memory */
	mayfault = sys_data->user_mask != 0;

	if (mayfault) {
		if (syscall_get_data(sys_data, args, &user_ptr,
				     &size, user_sizes, &uargs, buf_size) < 0)
			return;
	}

	head = this_cpu_ptr(sys_data->enter_event->perf_events);
	valid_prog_array = bpf_prog_array_valid(sys_data->enter_event);
	if (!valid_prog_array && hlist_empty(head))
		return;

	/* get the size after alignment with the u32 buffer size field */
	size += sizeof(unsigned long) * sys_data->nb_args + sizeof(*rec);
	size = ALIGN(size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	rec = perf_trace_buf_alloc(size, &fake_regs, &rctx);
	if (!rec)
		return;

	rec->nr = syscall_nr;
	memcpy(&rec->args, args, sizeof(unsigned long) * sys_data->nb_args);

	if (mayfault)
		syscall_put_data(sys_data, rec, user_ptr, size, user_sizes, uargs);

	if ((valid_prog_array &&
	     !perf_call_bpf_enter(sys_data->enter_event, fake_regs, sys_data, rec)) ||
	    hlist_empty(head)) {
		perf_swevent_put_recursion_context(rctx);
		return;
	}

	perf_trace_buf_submit(rec, size, rctx,
			      sys_data->enter_event->event.type, 1, regs,
			      head, NULL);
}

static int perf_sysenter_enable(struct trace_event_call *call)
{
	struct syscall_metadata *sys_data = call->data;
	int num;
	int ret;

	num = sys_data->syscall_nr;

	guard(mutex)(&syscall_trace_lock);
	if (sys_data->user_mask) {
		ret = syscall_fault_buffer_enable();
		if (ret < 0)
			return ret;
	}
	if (!sys_perf_refcount_enter) {
		ret = register_trace_sys_enter(perf_syscall_enter, NULL);
		if (ret) {
			pr_info("event trace: Could not activate syscall entry trace point");
			if (sys_data->user_mask)
				syscall_fault_buffer_disable();
			return ret;
		}
	}
	set_bit(num, enabled_perf_enter_syscalls);
	sys_perf_refcount_enter++;
	return 0;
}

static void perf_sysenter_disable(struct trace_event_call *call)
{
	struct syscall_metadata *sys_data = call->data;
	int num;

	num = sys_data->syscall_nr;

	guard(mutex)(&syscall_trace_lock);
	sys_perf_refcount_enter--;
	clear_bit(num, enabled_perf_enter_syscalls);
	if (!sys_perf_refcount_enter)
		unregister_trace_sys_enter(perf_syscall_enter, NULL);
	if (sys_data->user_mask)
		syscall_fault_buffer_disable();
}

static int perf_call_bpf_exit(struct trace_event_call *call, struct pt_regs *regs,
			      struct syscall_trace_exit *rec)
{
	struct syscall_tp_t {
		struct trace_entry ent;
		int syscall_nr;
		unsigned long ret;
	} __aligned(8) param;

	/* bpf prog requires 'regs' to be the first member in the ctx (a.k.a. &param) */
	perf_fetch_caller_regs(regs);
	*(struct pt_regs **)&param = regs;
	param.syscall_nr = rec->nr;
	param.ret = rec->ret;
	return trace_call_bpf(call, &param);
}

static void perf_syscall_exit(void *ignore, struct pt_regs *regs, long ret)
{
	struct syscall_metadata *sys_data;
	struct syscall_trace_exit *rec;
	struct pt_regs *fake_regs;
	struct hlist_head *head;
	bool valid_prog_array;
	int syscall_nr;
	int rctx;
	int size;

	/*
	 * Syscall probe called with preemption enabled, but the ring
	 * buffer and per-cpu data require preemption to be disabled.
	 */
	might_fault();
	guard(preempt_notrace)();

	syscall_nr = trace_get_syscall_nr(current, regs);
	if (syscall_nr < 0 || syscall_nr >= NR_syscalls)
		return;
	if (!test_bit(syscall_nr, enabled_perf_exit_syscalls))
		return;

	sys_data = syscall_nr_to_meta(syscall_nr);
	if (!sys_data)
		return;

	head = this_cpu_ptr(sys_data->exit_event->perf_events);
	valid_prog_array = bpf_prog_array_valid(sys_data->exit_event);
	if (!valid_prog_array && hlist_empty(head))
		return;

	/* We can probably do that at build time */
	size = ALIGN(sizeof(*rec) + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	rec = perf_trace_buf_alloc(size, &fake_regs, &rctx);
	if (!rec)
		return;

	rec->nr = syscall_nr;
	rec->ret = syscall_get_return_value(current, regs);

	if ((valid_prog_array &&
	     !perf_call_bpf_exit(sys_data->exit_event, fake_regs, rec)) ||
	    hlist_empty(head)) {
		perf_swevent_put_recursion_context(rctx);
		return;
	}

	perf_trace_buf_submit(rec, size, rctx, sys_data->exit_event->event.type,
			      1, regs, head, NULL);
}

static int perf_sysexit_enable(struct trace_event_call *call)
{
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;

	guard(mutex)(&syscall_trace_lock);
	if (!sys_perf_refcount_exit) {
		int ret = register_trace_sys_exit(perf_syscall_exit, NULL);
		if (ret) {
			pr_info("event trace: Could not activate syscall exit trace point");
			return ret;
		}
	}
	set_bit(num, enabled_perf_exit_syscalls);
	sys_perf_refcount_exit++;
	return 0;
}

static void perf_sysexit_disable(struct trace_event_call *call)
{
	int num;

	num = ((struct syscall_metadata *)call->data)->syscall_nr;

	guard(mutex)(&syscall_trace_lock);
	sys_perf_refcount_exit--;
	clear_bit(num, enabled_perf_exit_syscalls);
	if (!sys_perf_refcount_exit)
		unregister_trace_sys_exit(perf_syscall_exit, NULL);
}

#endif /* CONFIG_PERF_EVENTS */

static int syscall_enter_register(struct trace_event_call *event,
				 enum trace_reg type, void *data)
{
	struct trace_event_file *file = data;

	switch (type) {
	case TRACE_REG_REGISTER:
		return reg_event_syscall_enter(file, event);
	case TRACE_REG_UNREGISTER:
		unreg_event_syscall_enter(file, event);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return perf_sysenter_enable(event);
	case TRACE_REG_PERF_UNREGISTER:
		perf_sysenter_disable(event);
		return 0;
	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		return 0;
#endif
	}
	return 0;
}

static int syscall_exit_register(struct trace_event_call *event,
				 enum trace_reg type, void *data)
{
	struct trace_event_file *file = data;

	switch (type) {
	case TRACE_REG_REGISTER:
		return reg_event_syscall_exit(file, event);
	case TRACE_REG_UNREGISTER:
		unreg_event_syscall_exit(file, event);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return perf_sysexit_enable(event);
	case TRACE_REG_PERF_UNREGISTER:
		perf_sysexit_disable(event);
		return 0;
	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		return 0;
#endif
	}
	return 0;
}
