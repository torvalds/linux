// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/printk.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * Modified to make sys_syslog() more flexible: added commands to
 * return the last 4k of kernel messages, regardless of whether
 * they've been read or not.  Added option to suppress kernel printk's
 * to the console.  Added hook for sending the console messages
 * elsewhere, in preparation for a serial line console (someday).
 * Ted Ts'o, 2/11/93.
 * Modified for sysctl support, 1/8/97, Chris Horn.
 * Fixed SMP synchronization, 08/08/99, Manfred Spraul
 *     manfred@colorfullife.com
 * Rewrote bits to get rid of console_lock
 *	01Mar01 Andrew Morton
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/nmi.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/security.h>
#include <linux/memblock.h>
#include <linux/syscalls.h>
#include <linux/vmcore_info.h>
#include <linux/ratelimit.h>
#include <linux/kmsg_dump.h>
#include <linux/syslog.h>
#include <linux/cpu.h>
#include <linux/rculist.h>
#include <linux/poll.h>
#include <linux/irq_work.h>
#include <linux/ctype.h>
#include <linux/uio.h>
#include <linux/sched/clock.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>

#include <linux/uaccess.h>
#include <asm/sections.h>

#include <trace/events/initcall.h>
#define CREATE_TRACE_POINTS
#include <trace/events/printk.h>

#include "printk_ringbuffer.h"
#include "console_cmdline.h"
#include "braille.h"
#include "internal.h"

int console_printk[4] = {
	CONSOLE_LOGLEVEL_DEFAULT,	/* console_loglevel */
	MESSAGE_LOGLEVEL_DEFAULT,	/* default_message_loglevel */
	CONSOLE_LOGLEVEL_MIN,		/* minimum_console_loglevel */
	CONSOLE_LOGLEVEL_DEFAULT,	/* default_console_loglevel */
};
EXPORT_SYMBOL_GPL(console_printk);

atomic_t ignore_console_lock_warning __read_mostly = ATOMIC_INIT(0);
EXPORT_SYMBOL(ignore_console_lock_warning);

EXPORT_TRACEPOINT_SYMBOL_GPL(console);

/*
 * Low level drivers may need that to know if they can schedule in
 * their unblank() callback or not. So let's export it.
 */
int oops_in_progress;
EXPORT_SYMBOL(oops_in_progress);

/*
 * console_mutex protects console_list updates and console->flags updates.
 * The flags are synchronized only for consoles that are registered, i.e.
 * accessible via the console list.
 */
static DEFINE_MUTEX(console_mutex);

/*
 * console_sem protects updates to console->seq
 * and also provides serialization for console printing.
 */
static DEFINE_SEMAPHORE(console_sem, 1);
HLIST_HEAD(console_list);
EXPORT_SYMBOL_GPL(console_list);
DEFINE_STATIC_SRCU(console_srcu);

/*
 * System may need to suppress printk message under certain
 * circumstances, like after kernel panic happens.
 */
int __read_mostly suppress_printk;

#ifdef CONFIG_LOCKDEP
static struct lockdep_map console_lock_dep_map = {
	.name = "console_lock"
};

void lockdep_assert_console_list_lock_held(void)
{
	lockdep_assert_held(&console_mutex);
}
EXPORT_SYMBOL(lockdep_assert_console_list_lock_held);
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
bool console_srcu_read_lock_is_held(void)
{
	return srcu_read_lock_held(&console_srcu);
}
EXPORT_SYMBOL(console_srcu_read_lock_is_held);
#endif

enum devkmsg_log_bits {
	__DEVKMSG_LOG_BIT_ON = 0,
	__DEVKMSG_LOG_BIT_OFF,
	__DEVKMSG_LOG_BIT_LOCK,
};

enum devkmsg_log_masks {
	DEVKMSG_LOG_MASK_ON             = BIT(__DEVKMSG_LOG_BIT_ON),
	DEVKMSG_LOG_MASK_OFF            = BIT(__DEVKMSG_LOG_BIT_OFF),
	DEVKMSG_LOG_MASK_LOCK           = BIT(__DEVKMSG_LOG_BIT_LOCK),
};

/* Keep both the 'on' and 'off' bits clear, i.e. ratelimit by default: */
#define DEVKMSG_LOG_MASK_DEFAULT	0

static unsigned int __read_mostly devkmsg_log = DEVKMSG_LOG_MASK_DEFAULT;

static int __control_devkmsg(char *str)
{
	size_t len;

	if (!str)
		return -EINVAL;

	len = str_has_prefix(str, "on");
	if (len) {
		devkmsg_log = DEVKMSG_LOG_MASK_ON;
		return len;
	}

	len = str_has_prefix(str, "off");
	if (len) {
		devkmsg_log = DEVKMSG_LOG_MASK_OFF;
		return len;
	}

	len = str_has_prefix(str, "ratelimit");
	if (len) {
		devkmsg_log = DEVKMSG_LOG_MASK_DEFAULT;
		return len;
	}

	return -EINVAL;
}

static int __init control_devkmsg(char *str)
{
	if (__control_devkmsg(str) < 0) {
		pr_warn("printk.devkmsg: bad option string '%s'\n", str);
		return 1;
	}

	/*
	 * Set sysctl string accordingly:
	 */
	if (devkmsg_log == DEVKMSG_LOG_MASK_ON)
		strcpy(devkmsg_log_str, "on");
	else if (devkmsg_log == DEVKMSG_LOG_MASK_OFF)
		strcpy(devkmsg_log_str, "off");
	/* else "ratelimit" which is set by default. */

	/*
	 * Sysctl cannot change it anymore. The kernel command line setting of
	 * this parameter is to force the setting to be permanent throughout the
	 * runtime of the system. This is a precation measure against userspace
	 * trying to be a smarta** and attempting to change it up on us.
	 */
	devkmsg_log |= DEVKMSG_LOG_MASK_LOCK;

	return 1;
}
__setup("printk.devkmsg=", control_devkmsg);

char devkmsg_log_str[DEVKMSG_STR_MAX_SIZE] = "ratelimit";
#if defined(CONFIG_PRINTK) && defined(CONFIG_SYSCTL)
int devkmsg_sysctl_set_loglvl(struct ctl_table *table, int write,
			      void *buffer, size_t *lenp, loff_t *ppos)
{
	char old_str[DEVKMSG_STR_MAX_SIZE];
	unsigned int old;
	int err;

	if (write) {
		if (devkmsg_log & DEVKMSG_LOG_MASK_LOCK)
			return -EINVAL;

		old = devkmsg_log;
		strncpy(old_str, devkmsg_log_str, DEVKMSG_STR_MAX_SIZE);
	}

	err = proc_dostring(table, write, buffer, lenp, ppos);
	if (err)
		return err;

	if (write) {
		err = __control_devkmsg(devkmsg_log_str);

		/*
		 * Do not accept an unknown string OR a known string with
		 * trailing crap...
		 */
		if (err < 0 || (err + 1 != *lenp)) {

			/* ... and restore old setting. */
			devkmsg_log = old;
			strncpy(devkmsg_log_str, old_str, DEVKMSG_STR_MAX_SIZE);

			return -EINVAL;
		}
	}

	return 0;
}
#endif /* CONFIG_PRINTK && CONFIG_SYSCTL */

/**
 * console_list_lock - Lock the console list
 *
 * For console list or console->flags updates
 */
void console_list_lock(void)
{
	/*
	 * In unregister_console() and console_force_preferred_locked(),
	 * synchronize_srcu() is called with the console_list_lock held.
	 * Therefore it is not allowed that the console_list_lock is taken
	 * with the srcu_lock held.
	 *
	 * Detecting if this context is really in the read-side critical
	 * section is only possible if the appropriate debug options are
	 * enabled.
	 */
	WARN_ON_ONCE(debug_lockdep_rcu_enabled() &&
		     srcu_read_lock_held(&console_srcu));

	mutex_lock(&console_mutex);
}
EXPORT_SYMBOL(console_list_lock);

/**
 * console_list_unlock - Unlock the console list
 *
 * Counterpart to console_list_lock()
 */
void console_list_unlock(void)
{
	mutex_unlock(&console_mutex);
}
EXPORT_SYMBOL(console_list_unlock);

/**
 * console_srcu_read_lock - Register a new reader for the
 *	SRCU-protected console list
 *
 * Use for_each_console_srcu() to iterate the console list
 *
 * Context: Any context.
 * Return: A cookie to pass to console_srcu_read_unlock().
 */
int console_srcu_read_lock(void)
{
	return srcu_read_lock_nmisafe(&console_srcu);
}
EXPORT_SYMBOL(console_srcu_read_lock);

/**
 * console_srcu_read_unlock - Unregister an old reader from
 *	the SRCU-protected console list
 * @cookie: cookie returned from console_srcu_read_lock()
 *
 * Counterpart to console_srcu_read_lock()
 */
void console_srcu_read_unlock(int cookie)
{
	srcu_read_unlock_nmisafe(&console_srcu, cookie);
}
EXPORT_SYMBOL(console_srcu_read_unlock);

/*
 * Helper macros to handle lockdep when locking/unlocking console_sem. We use
 * macros instead of functions so that _RET_IP_ contains useful information.
 */
#define down_console_sem() do { \
	down(&console_sem);\
	mutex_acquire(&console_lock_dep_map, 0, 0, _RET_IP_);\
} while (0)

static int __down_trylock_console_sem(unsigned long ip)
{
	int lock_failed;
	unsigned long flags;

	/*
	 * Here and in __up_console_sem() we need to be in safe mode,
	 * because spindump/WARN/etc from under console ->lock will
	 * deadlock in printk()->down_trylock_console_sem() otherwise.
	 */
	printk_safe_enter_irqsave(flags);
	lock_failed = down_trylock(&console_sem);
	printk_safe_exit_irqrestore(flags);

	if (lock_failed)
		return 1;
	mutex_acquire(&console_lock_dep_map, 0, 1, ip);
	return 0;
}
#define down_trylock_console_sem() __down_trylock_console_sem(_RET_IP_)

static void __up_console_sem(unsigned long ip)
{
	unsigned long flags;

	mutex_release(&console_lock_dep_map, ip);

	printk_safe_enter_irqsave(flags);
	up(&console_sem);
	printk_safe_exit_irqrestore(flags);
}
#define up_console_sem() __up_console_sem(_RET_IP_)

static bool panic_in_progress(void)
{
	return unlikely(atomic_read(&panic_cpu) != PANIC_CPU_INVALID);
}

/* Return true if a panic is in progress on the current CPU. */
bool this_cpu_in_panic(void)
{
	/*
	 * We can use raw_smp_processor_id() here because it is impossible for
	 * the task to be migrated to the panic_cpu, or away from it. If
	 * panic_cpu has already been set, and we're not currently executing on
	 * that CPU, then we never will be.
	 */
	return unlikely(atomic_read(&panic_cpu) == raw_smp_processor_id());
}

/*
 * Return true if a panic is in progress on a remote CPU.
 *
 * On true, the local CPU should immediately release any printing resources
 * that may be needed by the panic CPU.
 */
bool other_cpu_in_panic(void)
{
	return (panic_in_progress() && !this_cpu_in_panic());
}

/*
 * This is used for debugging the mess that is the VT code by
 * keeping track if we have the console semaphore held. It's
 * definitely not the perfect debug tool (we don't know if _WE_
 * hold it and are racing, but it helps tracking those weird code
 * paths in the console code where we end up in places I want
 * locked without the console semaphore held).
 */
static int console_locked;

/*
 *	Array of consoles built from command line options (console=)
 */

#define MAX_CMDLINECONSOLES 8

static struct console_cmdline console_cmdline[MAX_CMDLINECONSOLES];

static int preferred_console = -1;
int console_set_on_cmdline;
EXPORT_SYMBOL(console_set_on_cmdline);

/* Flag: console code may call schedule() */
static int console_may_schedule;

enum con_msg_format_flags {
	MSG_FORMAT_DEFAULT	= 0,
	MSG_FORMAT_SYSLOG	= (1 << 0),
};

static int console_msg_format = MSG_FORMAT_DEFAULT;

/*
 * The printk log buffer consists of a sequenced collection of records, each
 * containing variable length message text. Every record also contains its
 * own meta-data (@info).
 *
 * Every record meta-data carries the timestamp in microseconds, as well as
 * the standard userspace syslog level and syslog facility. The usual kernel
 * messages use LOG_KERN; userspace-injected messages always carry a matching
 * syslog facility, by default LOG_USER. The origin of every message can be
 * reliably determined that way.
 *
 * The human readable log message of a record is available in @text, the
 * length of the message text in @text_len. The stored message is not
 * terminated.
 *
 * Optionally, a record can carry a dictionary of properties (key/value
 * pairs), to provide userspace with a machine-readable message context.
 *
 * Examples for well-defined, commonly used property names are:
 *   DEVICE=b12:8               device identifier
 *                                b12:8         block dev_t
 *                                c127:3        char dev_t
 *                                n8            netdev ifindex
 *                                +sound:card0  subsystem:devname
 *   SUBSYSTEM=pci              driver-core subsystem name
 *
 * Valid characters in property names are [a-zA-Z0-9.-_]. Property names
 * and values are terminated by a '\0' character.
 *
 * Example of record values:
 *   record.text_buf                = "it's a line" (unterminated)
 *   record.info.seq                = 56
 *   record.info.ts_nsec            = 36863
 *   record.info.text_len           = 11
 *   record.info.facility           = 0 (LOG_KERN)
 *   record.info.flags              = 0
 *   record.info.level              = 3 (LOG_ERR)
 *   record.info.caller_id          = 299 (task 299)
 *   record.info.dev_info.subsystem = "pci" (terminated)
 *   record.info.dev_info.device    = "+pci:0000:00:01.0" (terminated)
 *
 * The 'struct printk_info' buffer must never be directly exported to
 * userspace, it is a kernel-private implementation detail that might
 * need to be changed in the future, when the requirements change.
 *
 * /dev/kmsg exports the structured data in the following line format:
 *   "<level>,<sequnum>,<timestamp>,<contflag>[,additional_values, ... ];<message text>\n"
 *
 * Users of the export format should ignore possible additional values
 * separated by ',', and find the message after the ';' character.
 *
 * The optional key/value pairs are attached as continuation lines starting
 * with a space character and terminated by a newline. All possible
 * non-prinatable characters are escaped in the "\xff" notation.
 */

/* syslog_lock protects syslog_* variables and write access to clear_seq. */
static DEFINE_MUTEX(syslog_lock);

#ifdef CONFIG_PRINTK
DECLARE_WAIT_QUEUE_HEAD(log_wait);
/* All 3 protected by @syslog_lock. */
/* the next printk record to read by syslog(READ) or /proc/kmsg */
static u64 syslog_seq;
static size_t syslog_partial;
static bool syslog_time;

struct latched_seq {
	seqcount_latch_t	latch;
	u64			val[2];
};

/*
 * The next printk record to read after the last 'clear' command. There are
 * two copies (updated with seqcount_latch) so that reads can locklessly
 * access a valid value. Writers are synchronized by @syslog_lock.
 */
static struct latched_seq clear_seq = {
	.latch		= SEQCNT_LATCH_ZERO(clear_seq.latch),
	.val[0]		= 0,
	.val[1]		= 0,
};

#define LOG_LEVEL(v)		((v) & 0x07)
#define LOG_FACILITY(v)		((v) >> 3 & 0xff)

/* record buffer */
#define LOG_ALIGN __alignof__(unsigned long)
#define __LOG_BUF_LEN (1 << CONFIG_LOG_BUF_SHIFT)
#define LOG_BUF_LEN_MAX (u32)(1 << 31)
static char __log_buf[__LOG_BUF_LEN] __aligned(LOG_ALIGN);
static char *log_buf = __log_buf;
static u32 log_buf_len = __LOG_BUF_LEN;

/*
 * Define the average message size. This only affects the number of
 * descriptors that will be available. Underestimating is better than
 * overestimating (too many available descriptors is better than not enough).
 */
#define PRB_AVGBITS 5	/* 32 character average length */

#if CONFIG_LOG_BUF_SHIFT <= PRB_AVGBITS
#error CONFIG_LOG_BUF_SHIFT value too small.
#endif
_DEFINE_PRINTKRB(printk_rb_static, CONFIG_LOG_BUF_SHIFT - PRB_AVGBITS,
		 PRB_AVGBITS, &__log_buf[0]);

static struct printk_ringbuffer printk_rb_dynamic;

struct printk_ringbuffer *prb = &printk_rb_static;

/*
 * We cannot access per-CPU data (e.g. per-CPU flush irq_work) before
 * per_cpu_areas are initialised. This variable is set to true when
 * it's safe to access per-CPU data.
 */
static bool __printk_percpu_data_ready __ro_after_init;

bool printk_percpu_data_ready(void)
{
	return __printk_percpu_data_ready;
}

/* Must be called under syslog_lock. */
static void latched_seq_write(struct latched_seq *ls, u64 val)
{
	raw_write_seqcount_latch(&ls->latch);
	ls->val[0] = val;
	raw_write_seqcount_latch(&ls->latch);
	ls->val[1] = val;
}

/* Can be called from any context. */
static u64 latched_seq_read_nolock(struct latched_seq *ls)
{
	unsigned int seq;
	unsigned int idx;
	u64 val;

	do {
		seq = raw_read_seqcount_latch(&ls->latch);
		idx = seq & 0x1;
		val = ls->val[idx];
	} while (raw_read_seqcount_latch_retry(&ls->latch, seq));

	return val;
}

/* Return log buffer address */
char *log_buf_addr_get(void)
{
	return log_buf;
}

/* Return log buffer size */
u32 log_buf_len_get(void)
{
	return log_buf_len;
}

/*
 * Define how much of the log buffer we could take at maximum. The value
 * must be greater than two. Note that only half of the buffer is available
 * when the index points to the middle.
 */
#define MAX_LOG_TAKE_PART 4
static const char trunc_msg[] = "<truncated>";

static void truncate_msg(u16 *text_len, u16 *trunc_msg_len)
{
	/*
	 * The message should not take the whole buffer. Otherwise, it might
	 * get removed too soon.
	 */
	u32 max_text_len = log_buf_len / MAX_LOG_TAKE_PART;

	if (*text_len > max_text_len)
		*text_len = max_text_len;

	/* enable the warning message (if there is room) */
	*trunc_msg_len = strlen(trunc_msg);
	if (*text_len >= *trunc_msg_len)
		*text_len -= *trunc_msg_len;
	else
		*trunc_msg_len = 0;
}

int dmesg_restrict = IS_ENABLED(CONFIG_SECURITY_DMESG_RESTRICT);

static int syslog_action_restricted(int type)
{
	if (dmesg_restrict)
		return 1;
	/*
	 * Unless restricted, we allow "read all" and "get buffer size"
	 * for everybody.
	 */
	return type != SYSLOG_ACTION_READ_ALL &&
	       type != SYSLOG_ACTION_SIZE_BUFFER;
}

static int check_syslog_permissions(int type, int source)
{
	/*
	 * If this is from /proc/kmsg and we've already opened it, then we've
	 * already done the capabilities checks at open time.
	 */
	if (source == SYSLOG_FROM_PROC && type != SYSLOG_ACTION_OPEN)
		goto ok;

	if (syslog_action_restricted(type)) {
		if (capable(CAP_SYSLOG))
			goto ok;
		return -EPERM;
	}
ok:
	return security_syslog(type);
}

static void append_char(char **pp, char *e, char c)
{
	if (*pp < e)
		*(*pp)++ = c;
}

static ssize_t info_print_ext_header(char *buf, size_t size,
				     struct printk_info *info)
{
	u64 ts_usec = info->ts_nsec;
	char caller[20];
#ifdef CONFIG_PRINTK_CALLER
	u32 id = info->caller_id;

	snprintf(caller, sizeof(caller), ",caller=%c%u",
		 id & 0x80000000 ? 'C' : 'T', id & ~0x80000000);
#else
	caller[0] = '\0';
#endif

	do_div(ts_usec, 1000);

	return scnprintf(buf, size, "%u,%llu,%llu,%c%s;",
			 (info->facility << 3) | info->level, info->seq,
			 ts_usec, info->flags & LOG_CONT ? 'c' : '-', caller);
}

static ssize_t msg_add_ext_text(char *buf, size_t size,
				const char *text, size_t text_len,
				unsigned char endc)
{
	char *p = buf, *e = buf + size;
	size_t i;

	/* escape non-printable characters */
	for (i = 0; i < text_len; i++) {
		unsigned char c = text[i];

		if (c < ' ' || c >= 127 || c == '\\')
			p += scnprintf(p, e - p, "\\x%02x", c);
		else
			append_char(&p, e, c);
	}
	append_char(&p, e, endc);

	return p - buf;
}

static ssize_t msg_add_dict_text(char *buf, size_t size,
				 const char *key, const char *val)
{
	size_t val_len = strlen(val);
	ssize_t len;

	if (!val_len)
		return 0;

	len = msg_add_ext_text(buf, size, "", 0, ' ');	/* dict prefix */
	len += msg_add_ext_text(buf + len, size - len, key, strlen(key), '=');
	len += msg_add_ext_text(buf + len, size - len, val, val_len, '\n');

	return len;
}

static ssize_t msg_print_ext_body(char *buf, size_t size,
				  char *text, size_t text_len,
				  struct dev_printk_info *dev_info)
{
	ssize_t len;

	len = msg_add_ext_text(buf, size, text, text_len, '\n');

	if (!dev_info)
		goto out;

	len += msg_add_dict_text(buf + len, size - len, "SUBSYSTEM",
				 dev_info->subsystem);
	len += msg_add_dict_text(buf + len, size - len, "DEVICE",
				 dev_info->device);
out:
	return len;
}

/* /dev/kmsg - userspace message inject/listen interface */
struct devkmsg_user {
	atomic64_t seq;
	struct ratelimit_state rs;
	struct mutex lock;
	struct printk_buffers pbufs;
};

static __printf(3, 4) __cold
int devkmsg_emit(int facility, int level, const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = vprintk_emit(facility, level, NULL, fmt, args);
	va_end(args);

	return r;
}

static ssize_t devkmsg_write(struct kiocb *iocb, struct iov_iter *from)
{
	char *buf, *line;
	int level = default_message_loglevel;
	int facility = 1;	/* LOG_USER */
	struct file *file = iocb->ki_filp;
	struct devkmsg_user *user = file->private_data;
	size_t len = iov_iter_count(from);
	ssize_t ret = len;

	if (len > PRINTKRB_RECORD_MAX)
		return -EINVAL;

	/* Ignore when user logging is disabled. */
	if (devkmsg_log & DEVKMSG_LOG_MASK_OFF)
		return len;

	/* Ratelimit when not explicitly enabled. */
	if (!(devkmsg_log & DEVKMSG_LOG_MASK_ON)) {
		if (!___ratelimit(&user->rs, current->comm))
			return ret;
	}

	buf = kmalloc(len+1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[len] = '\0';
	if (!copy_from_iter_full(buf, len, from)) {
		kfree(buf);
		return -EFAULT;
	}

	/*
	 * Extract and skip the syslog prefix <[0-9]*>. Coming from userspace
	 * the decimal value represents 32bit, the lower 3 bit are the log
	 * level, the rest are the log facility.
	 *
	 * If no prefix or no userspace facility is specified, we
	 * enforce LOG_USER, to be able to reliably distinguish
	 * kernel-generated messages from userspace-injected ones.
	 */
	line = buf;
	if (line[0] == '<') {
		char *endp = NULL;
		unsigned int u;

		u = simple_strtoul(line + 1, &endp, 10);
		if (endp && endp[0] == '>') {
			level = LOG_LEVEL(u);
			if (LOG_FACILITY(u) != 0)
				facility = LOG_FACILITY(u);
			endp++;
			line = endp;
		}
	}

	devkmsg_emit(facility, level, "%s", line);
	kfree(buf);
	return ret;
}

static ssize_t devkmsg_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct devkmsg_user *user = file->private_data;
	char *outbuf = &user->pbufs.outbuf[0];
	struct printk_message pmsg = {
		.pbufs = &user->pbufs,
	};
	ssize_t ret;

	ret = mutex_lock_interruptible(&user->lock);
	if (ret)
		return ret;

	if (!printk_get_next_message(&pmsg, atomic64_read(&user->seq), true, false)) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		}

		/*
		 * Guarantee this task is visible on the waitqueue before
		 * checking the wake condition.
		 *
		 * The full memory barrier within set_current_state() of
		 * prepare_to_wait_event() pairs with the full memory barrier
		 * within wq_has_sleeper().
		 *
		 * This pairs with __wake_up_klogd:A.
		 */
		ret = wait_event_interruptible(log_wait,
				printk_get_next_message(&pmsg, atomic64_read(&user->seq), true,
							false)); /* LMM(devkmsg_read:A) */
		if (ret)
			goto out;
	}

	if (pmsg.dropped) {
		/* our last seen message is gone, return error and reset */
		atomic64_set(&user->seq, pmsg.seq);
		ret = -EPIPE;
		goto out;
	}

	atomic64_set(&user->seq, pmsg.seq + 1);

	if (pmsg.outbuf_len > count) {
		ret = -EINVAL;
		goto out;
	}

	if (copy_to_user(buf, outbuf, pmsg.outbuf_len)) {
		ret = -EFAULT;
		goto out;
	}
	ret = pmsg.outbuf_len;
out:
	mutex_unlock(&user->lock);
	return ret;
}

/*
 * Be careful when modifying this function!!!
 *
 * Only few operations are supported because the device works only with the
 * entire variable length messages (records). Non-standard values are
 * returned in the other cases and has been this way for quite some time.
 * User space applications might depend on this behavior.
 */
static loff_t devkmsg_llseek(struct file *file, loff_t offset, int whence)
{
	struct devkmsg_user *user = file->private_data;
	loff_t ret = 0;

	if (offset)
		return -ESPIPE;

	switch (whence) {
	case SEEK_SET:
		/* the first record */
		atomic64_set(&user->seq, prb_first_valid_seq(prb));
		break;
	case SEEK_DATA:
		/*
		 * The first record after the last SYSLOG_ACTION_CLEAR,
		 * like issued by 'dmesg -c'. Reading /dev/kmsg itself
		 * changes no global state, and does not clear anything.
		 */
		atomic64_set(&user->seq, latched_seq_read_nolock(&clear_seq));
		break;
	case SEEK_END:
		/* after the last record */
		atomic64_set(&user->seq, prb_next_seq(prb));
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static __poll_t devkmsg_poll(struct file *file, poll_table *wait)
{
	struct devkmsg_user *user = file->private_data;
	struct printk_info info;
	__poll_t ret = 0;

	poll_wait(file, &log_wait, wait);

	if (prb_read_valid_info(prb, atomic64_read(&user->seq), &info, NULL)) {
		/* return error when data has vanished underneath us */
		if (info.seq != atomic64_read(&user->seq))
			ret = EPOLLIN|EPOLLRDNORM|EPOLLERR|EPOLLPRI;
		else
			ret = EPOLLIN|EPOLLRDNORM;
	}

	return ret;
}

static int devkmsg_open(struct inode *inode, struct file *file)
{
	struct devkmsg_user *user;
	int err;

	if (devkmsg_log & DEVKMSG_LOG_MASK_OFF)
		return -EPERM;

	/* write-only does not need any file context */
	if ((file->f_flags & O_ACCMODE) != O_WRONLY) {
		err = check_syslog_permissions(SYSLOG_ACTION_READ_ALL,
					       SYSLOG_FROM_READER);
		if (err)
			return err;
	}

	user = kvmalloc(sizeof(struct devkmsg_user), GFP_KERNEL);
	if (!user)
		return -ENOMEM;

	ratelimit_default_init(&user->rs);
	ratelimit_set_flags(&user->rs, RATELIMIT_MSG_ON_RELEASE);

	mutex_init(&user->lock);

	atomic64_set(&user->seq, prb_first_valid_seq(prb));

	file->private_data = user;
	return 0;
}

static int devkmsg_release(struct inode *inode, struct file *file)
{
	struct devkmsg_user *user = file->private_data;

	ratelimit_state_exit(&user->rs);

	mutex_destroy(&user->lock);
	kvfree(user);
	return 0;
}

const struct file_operations kmsg_fops = {
	.open = devkmsg_open,
	.read = devkmsg_read,
	.write_iter = devkmsg_write,
	.llseek = devkmsg_llseek,
	.poll = devkmsg_poll,
	.release = devkmsg_release,
};

#ifdef CONFIG_VMCORE_INFO
/*
 * This appends the listed symbols to /proc/vmcore
 *
 * /proc/vmcore is used by various utilities, like crash and makedumpfile to
 * obtain access to symbols that are otherwise very difficult to locate.  These
 * symbols are specifically used so that utilities can access and extract the
 * dmesg log from a vmcore file after a crash.
 */
void log_buf_vmcoreinfo_setup(void)
{
	struct dev_printk_info *dev_info = NULL;

	VMCOREINFO_SYMBOL(prb);
	VMCOREINFO_SYMBOL(printk_rb_static);
	VMCOREINFO_SYMBOL(clear_seq);

	/*
	 * Export struct size and field offsets. User space tools can
	 * parse it and detect any changes to structure down the line.
	 */

	VMCOREINFO_STRUCT_SIZE(printk_ringbuffer);
	VMCOREINFO_OFFSET(printk_ringbuffer, desc_ring);
	VMCOREINFO_OFFSET(printk_ringbuffer, text_data_ring);
	VMCOREINFO_OFFSET(printk_ringbuffer, fail);

	VMCOREINFO_STRUCT_SIZE(prb_desc_ring);
	VMCOREINFO_OFFSET(prb_desc_ring, count_bits);
	VMCOREINFO_OFFSET(prb_desc_ring, descs);
	VMCOREINFO_OFFSET(prb_desc_ring, infos);
	VMCOREINFO_OFFSET(prb_desc_ring, head_id);
	VMCOREINFO_OFFSET(prb_desc_ring, tail_id);

	VMCOREINFO_STRUCT_SIZE(prb_desc);
	VMCOREINFO_OFFSET(prb_desc, state_var);
	VMCOREINFO_OFFSET(prb_desc, text_blk_lpos);

	VMCOREINFO_STRUCT_SIZE(prb_data_blk_lpos);
	VMCOREINFO_OFFSET(prb_data_blk_lpos, begin);
	VMCOREINFO_OFFSET(prb_data_blk_lpos, next);

	VMCOREINFO_STRUCT_SIZE(printk_info);
	VMCOREINFO_OFFSET(printk_info, seq);
	VMCOREINFO_OFFSET(printk_info, ts_nsec);
	VMCOREINFO_OFFSET(printk_info, text_len);
	VMCOREINFO_OFFSET(printk_info, caller_id);
	VMCOREINFO_OFFSET(printk_info, dev_info);

	VMCOREINFO_STRUCT_SIZE(dev_printk_info);
	VMCOREINFO_OFFSET(dev_printk_info, subsystem);
	VMCOREINFO_LENGTH(printk_info_subsystem, sizeof(dev_info->subsystem));
	VMCOREINFO_OFFSET(dev_printk_info, device);
	VMCOREINFO_LENGTH(printk_info_device, sizeof(dev_info->device));

	VMCOREINFO_STRUCT_SIZE(prb_data_ring);
	VMCOREINFO_OFFSET(prb_data_ring, size_bits);
	VMCOREINFO_OFFSET(prb_data_ring, data);
	VMCOREINFO_OFFSET(prb_data_ring, head_lpos);
	VMCOREINFO_OFFSET(prb_data_ring, tail_lpos);

	VMCOREINFO_SIZE(atomic_long_t);
	VMCOREINFO_TYPE_OFFSET(atomic_long_t, counter);

	VMCOREINFO_STRUCT_SIZE(latched_seq);
	VMCOREINFO_OFFSET(latched_seq, val);
}
#endif

/* requested log_buf_len from kernel cmdline */
static unsigned long __initdata new_log_buf_len;

/* we practice scaling the ring buffer by powers of 2 */
static void __init log_buf_len_update(u64 size)
{
	if (size > (u64)LOG_BUF_LEN_MAX) {
		size = (u64)LOG_BUF_LEN_MAX;
		pr_err("log_buf over 2G is not supported.\n");
	}

	if (size)
		size = roundup_pow_of_two(size);
	if (size > log_buf_len)
		new_log_buf_len = (unsigned long)size;
}

/* save requested log_buf_len since it's too early to process it */
static int __init log_buf_len_setup(char *str)
{
	u64 size;

	if (!str)
		return -EINVAL;

	size = memparse(str, &str);

	log_buf_len_update(size);

	return 0;
}
early_param("log_buf_len", log_buf_len_setup);

#ifdef CONFIG_SMP
#define __LOG_CPU_MAX_BUF_LEN (1 << CONFIG_LOG_CPU_MAX_BUF_SHIFT)

static void __init log_buf_add_cpu(void)
{
	unsigned int cpu_extra;

	/*
	 * archs should set up cpu_possible_bits properly with
	 * set_cpu_possible() after setup_arch() but just in
	 * case lets ensure this is valid.
	 */
	if (num_possible_cpus() == 1)
		return;

	cpu_extra = (num_possible_cpus() - 1) * __LOG_CPU_MAX_BUF_LEN;

	/* by default this will only continue through for large > 64 CPUs */
	if (cpu_extra <= __LOG_BUF_LEN / 2)
		return;

	pr_info("log_buf_len individual max cpu contribution: %d bytes\n",
		__LOG_CPU_MAX_BUF_LEN);
	pr_info("log_buf_len total cpu_extra contributions: %d bytes\n",
		cpu_extra);
	pr_info("log_buf_len min size: %d bytes\n", __LOG_BUF_LEN);

	log_buf_len_update(cpu_extra + __LOG_BUF_LEN);
}
#else /* !CONFIG_SMP */
static inline void log_buf_add_cpu(void) {}
#endif /* CONFIG_SMP */

static void __init set_percpu_data_ready(void)
{
	__printk_percpu_data_ready = true;
}

static unsigned int __init add_to_rb(struct printk_ringbuffer *rb,
				     struct printk_record *r)
{
	struct prb_reserved_entry e;
	struct printk_record dest_r;

	prb_rec_init_wr(&dest_r, r->info->text_len);

	if (!prb_reserve(&e, rb, &dest_r))
		return 0;

	memcpy(&dest_r.text_buf[0], &r->text_buf[0], r->info->text_len);
	dest_r.info->text_len = r->info->text_len;
	dest_r.info->facility = r->info->facility;
	dest_r.info->level = r->info->level;
	dest_r.info->flags = r->info->flags;
	dest_r.info->ts_nsec = r->info->ts_nsec;
	dest_r.info->caller_id = r->info->caller_id;
	memcpy(&dest_r.info->dev_info, &r->info->dev_info, sizeof(dest_r.info->dev_info));

	prb_final_commit(&e);

	return prb_record_text_space(&e);
}

static char setup_text_buf[PRINTKRB_RECORD_MAX] __initdata;

void __init setup_log_buf(int early)
{
	struct printk_info *new_infos;
	unsigned int new_descs_count;
	struct prb_desc *new_descs;
	struct printk_info info;
	struct printk_record r;
	unsigned int text_size;
	size_t new_descs_size;
	size_t new_infos_size;
	unsigned long flags;
	char *new_log_buf;
	unsigned int free;
	u64 seq;

	/*
	 * Some archs call setup_log_buf() multiple times - first is very
	 * early, e.g. from setup_arch(), and second - when percpu_areas
	 * are initialised.
	 */
	if (!early)
		set_percpu_data_ready();

	if (log_buf != __log_buf)
		return;

	if (!early && !new_log_buf_len)
		log_buf_add_cpu();

	if (!new_log_buf_len)
		return;

	new_descs_count = new_log_buf_len >> PRB_AVGBITS;
	if (new_descs_count == 0) {
		pr_err("new_log_buf_len: %lu too small\n", new_log_buf_len);
		return;
	}

	new_log_buf = memblock_alloc(new_log_buf_len, LOG_ALIGN);
	if (unlikely(!new_log_buf)) {
		pr_err("log_buf_len: %lu text bytes not available\n",
		       new_log_buf_len);
		return;
	}

	new_descs_size = new_descs_count * sizeof(struct prb_desc);
	new_descs = memblock_alloc(new_descs_size, LOG_ALIGN);
	if (unlikely(!new_descs)) {
		pr_err("log_buf_len: %zu desc bytes not available\n",
		       new_descs_size);
		goto err_free_log_buf;
	}

	new_infos_size = new_descs_count * sizeof(struct printk_info);
	new_infos = memblock_alloc(new_infos_size, LOG_ALIGN);
	if (unlikely(!new_infos)) {
		pr_err("log_buf_len: %zu info bytes not available\n",
		       new_infos_size);
		goto err_free_descs;
	}

	prb_rec_init_rd(&r, &info, &setup_text_buf[0], sizeof(setup_text_buf));

	prb_init(&printk_rb_dynamic,
		 new_log_buf, ilog2(new_log_buf_len),
		 new_descs, ilog2(new_descs_count),
		 new_infos);

	local_irq_save(flags);

	log_buf_len = new_log_buf_len;
	log_buf = new_log_buf;
	new_log_buf_len = 0;

	free = __LOG_BUF_LEN;
	prb_for_each_record(0, &printk_rb_static, seq, &r) {
		text_size = add_to_rb(&printk_rb_dynamic, &r);
		if (text_size > free)
			free = 0;
		else
			free -= text_size;
	}

	prb = &printk_rb_dynamic;

	local_irq_restore(flags);

	/*
	 * Copy any remaining messages that might have appeared from
	 * NMI context after copying but before switching to the
	 * dynamic buffer.
	 */
	prb_for_each_record(seq, &printk_rb_static, seq, &r) {
		text_size = add_to_rb(&printk_rb_dynamic, &r);
		if (text_size > free)
			free = 0;
		else
			free -= text_size;
	}

	if (seq != prb_next_seq(&printk_rb_static)) {
		pr_err("dropped %llu messages\n",
		       prb_next_seq(&printk_rb_static) - seq);
	}

	pr_info("log_buf_len: %u bytes\n", log_buf_len);
	pr_info("early log buf free: %u(%u%%)\n",
		free, (free * 100) / __LOG_BUF_LEN);
	return;

err_free_descs:
	memblock_free(new_descs, new_descs_size);
err_free_log_buf:
	memblock_free(new_log_buf, new_log_buf_len);
}

static bool __read_mostly ignore_loglevel;

static int __init ignore_loglevel_setup(char *str)
{
	ignore_loglevel = true;
	pr_info("debug: ignoring loglevel setting.\n");

	return 0;
}

early_param("ignore_loglevel", ignore_loglevel_setup);
module_param(ignore_loglevel, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ignore_loglevel,
		 "ignore loglevel setting (prints all kernel messages to the console)");

static bool suppress_message_printing(int level)
{
	return (level >= console_loglevel && !ignore_loglevel);
}

#ifdef CONFIG_BOOT_PRINTK_DELAY

static int boot_delay; /* msecs delay after each printk during bootup */
static unsigned long long loops_per_msec;	/* based on boot_delay */

static int __init boot_delay_setup(char *str)
{
	unsigned long lpj;

	lpj = preset_lpj ? preset_lpj : 1000000;	/* some guess */
	loops_per_msec = (unsigned long long)lpj / 1000 * HZ;

	get_option(&str, &boot_delay);
	if (boot_delay > 10 * 1000)
		boot_delay = 0;

	pr_debug("boot_delay: %u, preset_lpj: %ld, lpj: %lu, "
		"HZ: %d, loops_per_msec: %llu\n",
		boot_delay, preset_lpj, lpj, HZ, loops_per_msec);
	return 0;
}
early_param("boot_delay", boot_delay_setup);

static void boot_delay_msec(int level)
{
	unsigned long long k;
	unsigned long timeout;

	if ((boot_delay == 0 || system_state >= SYSTEM_RUNNING)
		|| suppress_message_printing(level)) {
		return;
	}

	k = (unsigned long long)loops_per_msec * boot_delay;

	timeout = jiffies + msecs_to_jiffies(boot_delay);
	while (k) {
		k--;
		cpu_relax();
		/*
		 * use (volatile) jiffies to prevent
		 * compiler reduction; loop termination via jiffies
		 * is secondary and may or may not happen.
		 */
		if (time_after(jiffies, timeout))
			break;
		touch_nmi_watchdog();
	}
}
#else
static inline void boot_delay_msec(int level)
{
}
#endif

static bool printk_time = IS_ENABLED(CONFIG_PRINTK_TIME);
module_param_named(time, printk_time, bool, S_IRUGO | S_IWUSR);

static size_t print_syslog(unsigned int level, char *buf)
{
	return sprintf(buf, "<%u>", level);
}

static size_t print_time(u64 ts, char *buf)
{
	unsigned long rem_nsec = do_div(ts, 1000000000);

	return sprintf(buf, "[%5lu.%06lu]",
		       (unsigned long)ts, rem_nsec / 1000);
}

#ifdef CONFIG_PRINTK_CALLER
static size_t print_caller(u32 id, char *buf)
{
	char caller[12];

	snprintf(caller, sizeof(caller), "%c%u",
		 id & 0x80000000 ? 'C' : 'T', id & ~0x80000000);
	return sprintf(buf, "[%6s]", caller);
}
#else
#define print_caller(id, buf) 0
#endif

static size_t info_print_prefix(const struct printk_info  *info, bool syslog,
				bool time, char *buf)
{
	size_t len = 0;

	if (syslog)
		len = print_syslog((info->facility << 3) | info->level, buf);

	if (time)
		len += print_time(info->ts_nsec, buf + len);

	len += print_caller(info->caller_id, buf + len);

	if (IS_ENABLED(CONFIG_PRINTK_CALLER) || time) {
		buf[len++] = ' ';
		buf[len] = '\0';
	}

	return len;
}

/*
 * Prepare the record for printing. The text is shifted within the given
 * buffer to avoid a need for another one. The following operations are
 * done:
 *
 *   - Add prefix for each line.
 *   - Drop truncated lines that no longer fit into the buffer.
 *   - Add the trailing newline that has been removed in vprintk_store().
 *   - Add a string terminator.
 *
 * Since the produced string is always terminated, the maximum possible
 * return value is @r->text_buf_size - 1;
 *
 * Return: The length of the updated/prepared text, including the added
 * prefixes and the newline. The terminator is not counted. The dropped
 * line(s) are not counted.
 */
static size_t record_print_text(struct printk_record *r, bool syslog,
				bool time)
{
	size_t text_len = r->info->text_len;
	size_t buf_size = r->text_buf_size;
	char *text = r->text_buf;
	char prefix[PRINTK_PREFIX_MAX];
	bool truncated = false;
	size_t prefix_len;
	size_t line_len;
	size_t len = 0;
	char *next;

	/*
	 * If the message was truncated because the buffer was not large
	 * enough, treat the available text as if it were the full text.
	 */
	if (text_len > buf_size)
		text_len = buf_size;

	prefix_len = info_print_prefix(r->info, syslog, time, prefix);

	/*
	 * @text_len: bytes of unprocessed text
	 * @line_len: bytes of current line _without_ newline
	 * @text:     pointer to beginning of current line
	 * @len:      number of bytes prepared in r->text_buf
	 */
	for (;;) {
		next = memchr(text, '\n', text_len);
		if (next) {
			line_len = next - text;
		} else {
			/* Drop truncated line(s). */
			if (truncated)
				break;
			line_len = text_len;
		}

		/*
		 * Truncate the text if there is not enough space to add the
		 * prefix and a trailing newline and a terminator.
		 */
		if (len + prefix_len + text_len + 1 + 1 > buf_size) {
			/* Drop even the current line if no space. */
			if (len + prefix_len + line_len + 1 + 1 > buf_size)
				break;

			text_len = buf_size - len - prefix_len - 1 - 1;
			truncated = true;
		}

		memmove(text + prefix_len, text, text_len);
		memcpy(text, prefix, prefix_len);

		/*
		 * Increment the prepared length to include the text and
		 * prefix that were just moved+copied. Also increment for the
		 * newline at the end of this line. If this is the last line,
		 * there is no newline, but it will be added immediately below.
		 */
		len += prefix_len + line_len + 1;
		if (text_len == line_len) {
			/*
			 * This is the last line. Add the trailing newline
			 * removed in vprintk_store().
			 */
			text[prefix_len + line_len] = '\n';
			break;
		}

		/*
		 * Advance beyond the added prefix and the related line with
		 * its newline.
		 */
		text += prefix_len + line_len + 1;

		/*
		 * The remaining text has only decreased by the line with its
		 * newline.
		 *
		 * Note that @text_len can become zero. It happens when @text
		 * ended with a newline (either due to truncation or the
		 * original string ending with "\n\n"). The loop is correctly
		 * repeated and (if not truncated) an empty line with a prefix
		 * will be prepared.
		 */
		text_len -= line_len + 1;
	}

	/*
	 * If a buffer was provided, it will be terminated. Space for the
	 * string terminator is guaranteed to be available. The terminator is
	 * not counted in the return value.
	 */
	if (buf_size > 0)
		r->text_buf[len] = 0;

	return len;
}

static size_t get_record_print_text_size(struct printk_info *info,
					 unsigned int line_count,
					 bool syslog, bool time)
{
	char prefix[PRINTK_PREFIX_MAX];
	size_t prefix_len;

	prefix_len = info_print_prefix(info, syslog, time, prefix);

	/*
	 * Each line will be preceded with a prefix. The intermediate
	 * newlines are already within the text, but a final trailing
	 * newline will be added.
	 */
	return ((prefix_len * line_count) + info->text_len + 1);
}

/*
 * Beginning with @start_seq, find the first record where it and all following
 * records up to (but not including) @max_seq fit into @size.
 *
 * @max_seq is simply an upper bound and does not need to exist. If the caller
 * does not require an upper bound, -1 can be used for @max_seq.
 */
static u64 find_first_fitting_seq(u64 start_seq, u64 max_seq, size_t size,
				  bool syslog, bool time)
{
	struct printk_info info;
	unsigned int line_count;
	size_t len = 0;
	u64 seq;

	/* Determine the size of the records up to @max_seq. */
	prb_for_each_info(start_seq, prb, seq, &info, &line_count) {
		if (info.seq >= max_seq)
			break;
		len += get_record_print_text_size(&info, line_count, syslog, time);
	}

	/*
	 * Adjust the upper bound for the next loop to avoid subtracting
	 * lengths that were never added.
	 */
	if (seq < max_seq)
		max_seq = seq;

	/*
	 * Move first record forward until length fits into the buffer. Ignore
	 * newest messages that were not counted in the above cycle. Messages
	 * might appear and get lost in the meantime. This is a best effort
	 * that prevents an infinite loop that could occur with a retry.
	 */
	prb_for_each_info(start_seq, prb, seq, &info, &line_count) {
		if (len <= size || info.seq >= max_seq)
			break;
		len -= get_record_print_text_size(&info, line_count, syslog, time);
	}

	return seq;
}

/* The caller is responsible for making sure @size is greater than 0. */
static int syslog_print(char __user *buf, int size)
{
	struct printk_info info;
	struct printk_record r;
	char *text;
	int len = 0;
	u64 seq;

	text = kmalloc(PRINTK_MESSAGE_MAX, GFP_KERNEL);
	if (!text)
		return -ENOMEM;

	prb_rec_init_rd(&r, &info, text, PRINTK_MESSAGE_MAX);

	mutex_lock(&syslog_lock);

	/*
	 * Wait for the @syslog_seq record to be available. @syslog_seq may
	 * change while waiting.
	 */
	do {
		seq = syslog_seq;

		mutex_unlock(&syslog_lock);
		/*
		 * Guarantee this task is visible on the waitqueue before
		 * checking the wake condition.
		 *
		 * The full memory barrier within set_current_state() of
		 * prepare_to_wait_event() pairs with the full memory barrier
		 * within wq_has_sleeper().
		 *
		 * This pairs with __wake_up_klogd:A.
		 */
		len = wait_event_interruptible(log_wait,
				prb_read_valid(prb, seq, NULL)); /* LMM(syslog_print:A) */
		mutex_lock(&syslog_lock);

		if (len)
			goto out;
	} while (syslog_seq != seq);

	/*
	 * Copy records that fit into the buffer. The above cycle makes sure
	 * that the first record is always available.
	 */
	do {
		size_t n;
		size_t skip;
		int err;

		if (!prb_read_valid(prb, syslog_seq, &r))
			break;

		if (r.info->seq != syslog_seq) {
			/* message is gone, move to next valid one */
			syslog_seq = r.info->seq;
			syslog_partial = 0;
		}

		/*
		 * To keep reading/counting partial line consistent,
		 * use printk_time value as of the beginning of a line.
		 */
		if (!syslog_partial)
			syslog_time = printk_time;

		skip = syslog_partial;
		n = record_print_text(&r, true, syslog_time);
		if (n - syslog_partial <= size) {
			/* message fits into buffer, move forward */
			syslog_seq = r.info->seq + 1;
			n -= syslog_partial;
			syslog_partial = 0;
		} else if (!len){
			/* partial read(), remember position */
			n = size;
			syslog_partial += n;
		} else
			n = 0;

		if (!n)
			break;

		mutex_unlock(&syslog_lock);
		err = copy_to_user(buf, text + skip, n);
		mutex_lock(&syslog_lock);

		if (err) {
			if (!len)
				len = -EFAULT;
			break;
		}

		len += n;
		size -= n;
		buf += n;
	} while (size);
out:
	mutex_unlock(&syslog_lock);
	kfree(text);
	return len;
}

static int syslog_print_all(char __user *buf, int size, bool clear)
{
	struct printk_info info;
	struct printk_record r;
	char *text;
	int len = 0;
	u64 seq;
	bool time;

	text = kmalloc(PRINTK_MESSAGE_MAX, GFP_KERNEL);
	if (!text)
		return -ENOMEM;

	time = printk_time;
	/*
	 * Find first record that fits, including all following records,
	 * into the user-provided buffer for this dump.
	 */
	seq = find_first_fitting_seq(latched_seq_read_nolock(&clear_seq), -1,
				     size, true, time);

	prb_rec_init_rd(&r, &info, text, PRINTK_MESSAGE_MAX);

	prb_for_each_record(seq, prb, seq, &r) {
		int textlen;

		textlen = record_print_text(&r, true, time);

		if (len + textlen > size) {
			seq--;
			break;
		}

		if (copy_to_user(buf + len, text, textlen))
			len = -EFAULT;
		else
			len += textlen;

		if (len < 0)
			break;
	}

	if (clear) {
		mutex_lock(&syslog_lock);
		latched_seq_write(&clear_seq, seq);
		mutex_unlock(&syslog_lock);
	}

	kfree(text);
	return len;
}

static void syslog_clear(void)
{
	mutex_lock(&syslog_lock);
	latched_seq_write(&clear_seq, prb_next_seq(prb));
	mutex_unlock(&syslog_lock);
}

int do_syslog(int type, char __user *buf, int len, int source)
{
	struct printk_info info;
	bool clear = false;
	static int saved_console_loglevel = LOGLEVEL_DEFAULT;
	int error;

	error = check_syslog_permissions(type, source);
	if (error)
		return error;

	switch (type) {
	case SYSLOG_ACTION_CLOSE:	/* Close log */
		break;
	case SYSLOG_ACTION_OPEN:	/* Open log */
		break;
	case SYSLOG_ACTION_READ:	/* Read from log */
		if (!buf || len < 0)
			return -EINVAL;
		if (!len)
			return 0;
		if (!access_ok(buf, len))
			return -EFAULT;
		error = syslog_print(buf, len);
		break;
	/* Read/clear last kernel messages */
	case SYSLOG_ACTION_READ_CLEAR:
		clear = true;
		fallthrough;
	/* Read last kernel messages */
	case SYSLOG_ACTION_READ_ALL:
		if (!buf || len < 0)
			return -EINVAL;
		if (!len)
			return 0;
		if (!access_ok(buf, len))
			return -EFAULT;
		error = syslog_print_all(buf, len, clear);
		break;
	/* Clear ring buffer */
	case SYSLOG_ACTION_CLEAR:
		syslog_clear();
		break;
	/* Disable logging to console */
	case SYSLOG_ACTION_CONSOLE_OFF:
		if (saved_console_loglevel == LOGLEVEL_DEFAULT)
			saved_console_loglevel = console_loglevel;
		console_loglevel = minimum_console_loglevel;
		break;
	/* Enable logging to console */
	case SYSLOG_ACTION_CONSOLE_ON:
		if (saved_console_loglevel != LOGLEVEL_DEFAULT) {
			console_loglevel = saved_console_loglevel;
			saved_console_loglevel = LOGLEVEL_DEFAULT;
		}
		break;
	/* Set level of messages printed to console */
	case SYSLOG_ACTION_CONSOLE_LEVEL:
		if (len < 1 || len > 8)
			return -EINVAL;
		if (len < minimum_console_loglevel)
			len = minimum_console_loglevel;
		console_loglevel = len;
		/* Implicitly re-enable logging to console */
		saved_console_loglevel = LOGLEVEL_DEFAULT;
		break;
	/* Number of chars in the log buffer */
	case SYSLOG_ACTION_SIZE_UNREAD:
		mutex_lock(&syslog_lock);
		if (!prb_read_valid_info(prb, syslog_seq, &info, NULL)) {
			/* No unread messages. */
			mutex_unlock(&syslog_lock);
			return 0;
		}
		if (info.seq != syslog_seq) {
			/* messages are gone, move to first one */
			syslog_seq = info.seq;
			syslog_partial = 0;
		}
		if (source == SYSLOG_FROM_PROC) {
			/*
			 * Short-cut for poll(/"proc/kmsg") which simply checks
			 * for pending data, not the size; return the count of
			 * records, not the length.
			 */
			error = prb_next_seq(prb) - syslog_seq;
		} else {
			bool time = syslog_partial ? syslog_time : printk_time;
			unsigned int line_count;
			u64 seq;

			prb_for_each_info(syslog_seq, prb, seq, &info,
					  &line_count) {
				error += get_record_print_text_size(&info, line_count,
								    true, time);
				time = printk_time;
			}
			error -= syslog_partial;
		}
		mutex_unlock(&syslog_lock);
		break;
	/* Size of the log buffer */
	case SYSLOG_ACTION_SIZE_BUFFER:
		error = log_buf_len;
		break;
	default:
		error = -EINVAL;
		break;
	}

	return error;
}

SYSCALL_DEFINE3(syslog, int, type, char __user *, buf, int, len)
{
	return do_syslog(type, buf, len, SYSLOG_FROM_READER);
}

/*
 * Special console_lock variants that help to reduce the risk of soft-lockups.
 * They allow to pass console_lock to another printk() call using a busy wait.
 */

#ifdef CONFIG_LOCKDEP
static struct lockdep_map console_owner_dep_map = {
	.name = "console_owner"
};
#endif

static DEFINE_RAW_SPINLOCK(console_owner_lock);
static struct task_struct *console_owner;
static bool console_waiter;

/**
 * console_lock_spinning_enable - mark beginning of code where another
 *	thread might safely busy wait
 *
 * This basically converts console_lock into a spinlock. This marks
 * the section where the console_lock owner can not sleep, because
 * there may be a waiter spinning (like a spinlock). Also it must be
 * ready to hand over the lock at the end of the section.
 */
static void console_lock_spinning_enable(void)
{
	/*
	 * Do not use spinning in panic(). The panic CPU wants to keep the lock.
	 * Non-panic CPUs abandon the flush anyway.
	 *
	 * Just keep the lockdep annotation. The panic-CPU should avoid
	 * taking console_owner_lock because it might cause a deadlock.
	 * This looks like the easiest way how to prevent false lockdep
	 * reports without handling races a lockless way.
	 */
	if (panic_in_progress())
		goto lockdep;

	raw_spin_lock(&console_owner_lock);
	console_owner = current;
	raw_spin_unlock(&console_owner_lock);

lockdep:
	/* The waiter may spin on us after setting console_owner */
	spin_acquire(&console_owner_dep_map, 0, 0, _THIS_IP_);
}

/**
 * console_lock_spinning_disable_and_check - mark end of code where another
 *	thread was able to busy wait and check if there is a waiter
 * @cookie: cookie returned from console_srcu_read_lock()
 *
 * This is called at the end of the section where spinning is allowed.
 * It has two functions. First, it is a signal that it is no longer
 * safe to start busy waiting for the lock. Second, it checks if
 * there is a busy waiter and passes the lock rights to her.
 *
 * Important: Callers lose both the console_lock and the SRCU read lock if
 *	there was a busy waiter. They must not touch items synchronized by
 *	console_lock or SRCU read lock in this case.
 *
 * Return: 1 if the lock rights were passed, 0 otherwise.
 */
static int console_lock_spinning_disable_and_check(int cookie)
{
	int waiter;

	/*
	 * Ignore spinning waiters during panic() because they might get stopped
	 * or blocked at any time,
	 *
	 * It is safe because nobody is allowed to start spinning during panic
	 * in the first place. If there has been a waiter then non panic CPUs
	 * might stay spinning. They would get stopped anyway. The panic context
	 * will never start spinning and an interrupted spin on panic CPU will
	 * never continue.
	 */
	if (panic_in_progress()) {
		/* Keep lockdep happy. */
		spin_release(&console_owner_dep_map, _THIS_IP_);
		return 0;
	}

	raw_spin_lock(&console_owner_lock);
	waiter = READ_ONCE(console_waiter);
	console_owner = NULL;
	raw_spin_unlock(&console_owner_lock);

	if (!waiter) {
		spin_release(&console_owner_dep_map, _THIS_IP_);
		return 0;
	}

	/* The waiter is now free to continue */
	WRITE_ONCE(console_waiter, false);

	spin_release(&console_owner_dep_map, _THIS_IP_);

	/*
	 * Preserve lockdep lock ordering. Release the SRCU read lock before
	 * releasing the console_lock.
	 */
	console_srcu_read_unlock(cookie);

	/*
	 * Hand off console_lock to waiter. The waiter will perform
	 * the up(). After this, the waiter is the console_lock owner.
	 */
	mutex_release(&console_lock_dep_map, _THIS_IP_);
	return 1;
}

/**
 * console_trylock_spinning - try to get console_lock by busy waiting
 *
 * This allows to busy wait for the console_lock when the current
 * owner is running in specially marked sections. It means that
 * the current owner is running and cannot reschedule until it
 * is ready to lose the lock.
 *
 * Return: 1 if we got the lock, 0 othrewise
 */
static int console_trylock_spinning(void)
{
	struct task_struct *owner = NULL;
	bool waiter;
	bool spin = false;
	unsigned long flags;

	if (console_trylock())
		return 1;

	/*
	 * It's unsafe to spin once a panic has begun. If we are the
	 * panic CPU, we may have already halted the owner of the
	 * console_sem. If we are not the panic CPU, then we should
	 * avoid taking console_sem, so the panic CPU has a better
	 * chance of cleanly acquiring it later.
	 */
	if (panic_in_progress())
		return 0;

	printk_safe_enter_irqsave(flags);

	raw_spin_lock(&console_owner_lock);
	owner = READ_ONCE(console_owner);
	waiter = READ_ONCE(console_waiter);
	if (!waiter && owner && owner != current) {
		WRITE_ONCE(console_waiter, true);
		spin = true;
	}
	raw_spin_unlock(&console_owner_lock);

	/*
	 * If there is an active printk() writing to the
	 * consoles, instead of having it write our data too,
	 * see if we can offload that load from the active
	 * printer, and do some printing ourselves.
	 * Go into a spin only if there isn't already a waiter
	 * spinning, and there is an active printer, and
	 * that active printer isn't us (recursive printk?).
	 */
	if (!spin) {
		printk_safe_exit_irqrestore(flags);
		return 0;
	}

	/* We spin waiting for the owner to release us */
	spin_acquire(&console_owner_dep_map, 0, 0, _THIS_IP_);
	/* Owner will clear console_waiter on hand off */
	while (READ_ONCE(console_waiter))
		cpu_relax();
	spin_release(&console_owner_dep_map, _THIS_IP_);

	printk_safe_exit_irqrestore(flags);
	/*
	 * The owner passed the console lock to us.
	 * Since we did not spin on console lock, annotate
	 * this as a trylock. Otherwise lockdep will
	 * complain.
	 */
	mutex_acquire(&console_lock_dep_map, 0, 1, _THIS_IP_);

	/*
	 * Update @console_may_schedule for trylock because the previous
	 * owner may have been schedulable.
	 */
	console_may_schedule = 0;

	return 1;
}

/*
 * Recursion is tracked separately on each CPU. If NMIs are supported, an
 * additional NMI context per CPU is also separately tracked. Until per-CPU
 * is available, a separate "early tracking" is performed.
 */
static DEFINE_PER_CPU(u8, printk_count);
static u8 printk_count_early;
#ifdef CONFIG_HAVE_NMI
static DEFINE_PER_CPU(u8, printk_count_nmi);
static u8 printk_count_nmi_early;
#endif

/*
 * Recursion is limited to keep the output sane. printk() should not require
 * more than 1 level of recursion (allowing, for example, printk() to trigger
 * a WARN), but a higher value is used in case some printk-internal errors
 * exist, such as the ringbuffer validation checks failing.
 */
#define PRINTK_MAX_RECURSION 3

/*
 * Return a pointer to the dedicated counter for the CPU+context of the
 * caller.
 */
static u8 *__printk_recursion_counter(void)
{
#ifdef CONFIG_HAVE_NMI
	if (in_nmi()) {
		if (printk_percpu_data_ready())
			return this_cpu_ptr(&printk_count_nmi);
		return &printk_count_nmi_early;
	}
#endif
	if (printk_percpu_data_ready())
		return this_cpu_ptr(&printk_count);
	return &printk_count_early;
}

/*
 * Enter recursion tracking. Interrupts are disabled to simplify tracking.
 * The caller must check the boolean return value to see if the recursion is
 * allowed. On failure, interrupts are not disabled.
 *
 * @recursion_ptr must be a variable of type (u8 *) and is the same variable
 * that is passed to printk_exit_irqrestore().
 */
#define printk_enter_irqsave(recursion_ptr, flags)	\
({							\
	bool success = true;				\
							\
	typecheck(u8 *, recursion_ptr);			\
	local_irq_save(flags);				\
	(recursion_ptr) = __printk_recursion_counter();	\
	if (*(recursion_ptr) > PRINTK_MAX_RECURSION) {	\
		local_irq_restore(flags);		\
		success = false;			\
	} else {					\
		(*(recursion_ptr))++;			\
	}						\
	success;					\
})

/* Exit recursion tracking, restoring interrupts. */
#define printk_exit_irqrestore(recursion_ptr, flags)	\
	do {						\
		typecheck(u8 *, recursion_ptr);		\
		(*(recursion_ptr))--;			\
		local_irq_restore(flags);		\
	} while (0)

int printk_delay_msec __read_mostly;

static inline void printk_delay(int level)
{
	boot_delay_msec(level);

	if (unlikely(printk_delay_msec)) {
		int m = printk_delay_msec;

		while (m--) {
			mdelay(1);
			touch_nmi_watchdog();
		}
	}
}

static inline u32 printk_caller_id(void)
{
	return in_task() ? task_pid_nr(current) :
		0x80000000 + smp_processor_id();
}

/**
 * printk_parse_prefix - Parse level and control flags.
 *
 * @text:     The terminated text message.
 * @level:    A pointer to the current level value, will be updated.
 * @flags:    A pointer to the current printk_info flags, will be updated.
 *
 * @level may be NULL if the caller is not interested in the parsed value.
 * Otherwise the variable pointed to by @level must be set to
 * LOGLEVEL_DEFAULT in order to be updated with the parsed value.
 *
 * @flags may be NULL if the caller is not interested in the parsed value.
 * Otherwise the variable pointed to by @flags will be OR'd with the parsed
 * value.
 *
 * Return: The length of the parsed level and control flags.
 */
u16 printk_parse_prefix(const char *text, int *level,
			enum printk_info_flags *flags)
{
	u16 prefix_len = 0;
	int kern_level;

	while (*text) {
		kern_level = printk_get_level(text);
		if (!kern_level)
			break;

		switch (kern_level) {
		case '0' ... '7':
			if (level && *level == LOGLEVEL_DEFAULT)
				*level = kern_level - '0';
			break;
		case 'c':	/* KERN_CONT */
			if (flags)
				*flags |= LOG_CONT;
		}

		prefix_len += 2;
		text += 2;
	}

	return prefix_len;
}

__printf(5, 0)
static u16 printk_sprint(char *text, u16 size, int facility,
			 enum printk_info_flags *flags, const char *fmt,
			 va_list args)
{
	u16 text_len;

	text_len = vscnprintf(text, size, fmt, args);

	/* Mark and strip a trailing newline. */
	if (text_len && text[text_len - 1] == '\n') {
		text_len--;
		*flags |= LOG_NEWLINE;
	}

	/* Strip log level and control flags. */
	if (facility == 0) {
		u16 prefix_len;

		prefix_len = printk_parse_prefix(text, NULL, NULL);
		if (prefix_len) {
			text_len -= prefix_len;
			memmove(text, text + prefix_len, text_len);
		}
	}

	trace_console(text, text_len);

	return text_len;
}

__printf(4, 0)
int vprintk_store(int facility, int level,
		  const struct dev_printk_info *dev_info,
		  const char *fmt, va_list args)
{
	struct prb_reserved_entry e;
	enum printk_info_flags flags = 0;
	struct printk_record r;
	unsigned long irqflags;
	u16 trunc_msg_len = 0;
	char prefix_buf[8];
	u8 *recursion_ptr;
	u16 reserve_size;
	va_list args2;
	u32 caller_id;
	u16 text_len;
	int ret = 0;
	u64 ts_nsec;

	if (!printk_enter_irqsave(recursion_ptr, irqflags))
		return 0;

	/*
	 * Since the duration of printk() can vary depending on the message
	 * and state of the ringbuffer, grab the timestamp now so that it is
	 * close to the call of printk(). This provides a more deterministic
	 * timestamp with respect to the caller.
	 */
	ts_nsec = local_clock();

	caller_id = printk_caller_id();

	/*
	 * The sprintf needs to come first since the syslog prefix might be
	 * passed in as a parameter. An extra byte must be reserved so that
	 * later the vscnprintf() into the reserved buffer has room for the
	 * terminating '\0', which is not counted by vsnprintf().
	 */
	va_copy(args2, args);
	reserve_size = vsnprintf(&prefix_buf[0], sizeof(prefix_buf), fmt, args2) + 1;
	va_end(args2);

	if (reserve_size > PRINTKRB_RECORD_MAX)
		reserve_size = PRINTKRB_RECORD_MAX;

	/* Extract log level or control flags. */
	if (facility == 0)
		printk_parse_prefix(&prefix_buf[0], &level, &flags);

	if (level == LOGLEVEL_DEFAULT)
		level = default_message_loglevel;

	if (dev_info)
		flags |= LOG_NEWLINE;

	if (flags & LOG_CONT) {
		prb_rec_init_wr(&r, reserve_size);
		if (prb_reserve_in_last(&e, prb, &r, caller_id, PRINTKRB_RECORD_MAX)) {
			text_len = printk_sprint(&r.text_buf[r.info->text_len], reserve_size,
						 facility, &flags, fmt, args);
			r.info->text_len += text_len;

			if (flags & LOG_NEWLINE) {
				r.info->flags |= LOG_NEWLINE;
				prb_final_commit(&e);
			} else {
				prb_commit(&e);
			}

			ret = text_len;
			goto out;
		}
	}

	/*
	 * Explicitly initialize the record before every prb_reserve() call.
	 * prb_reserve_in_last() and prb_reserve() purposely invalidate the
	 * structure when they fail.
	 */
	prb_rec_init_wr(&r, reserve_size);
	if (!prb_reserve(&e, prb, &r)) {
		/* truncate the message if it is too long for empty buffer */
		truncate_msg(&reserve_size, &trunc_msg_len);

		prb_rec_init_wr(&r, reserve_size + trunc_msg_len);
		if (!prb_reserve(&e, prb, &r))
			goto out;
	}

	/* fill message */
	text_len = printk_sprint(&r.text_buf[0], reserve_size, facility, &flags, fmt, args);
	if (trunc_msg_len)
		memcpy(&r.text_buf[text_len], trunc_msg, trunc_msg_len);
	r.info->text_len = text_len + trunc_msg_len;
	r.info->facility = facility;
	r.info->level = level & 7;
	r.info->flags = flags & 0x1f;
	r.info->ts_nsec = ts_nsec;
	r.info->caller_id = caller_id;
	if (dev_info)
		memcpy(&r.info->dev_info, dev_info, sizeof(r.info->dev_info));

	/* A message without a trailing newline can be continued. */
	if (!(flags & LOG_NEWLINE))
		prb_commit(&e);
	else
		prb_final_commit(&e);

	ret = text_len + trunc_msg_len;
out:
	printk_exit_irqrestore(recursion_ptr, irqflags);
	return ret;
}

asmlinkage int vprintk_emit(int facility, int level,
			    const struct dev_printk_info *dev_info,
			    const char *fmt, va_list args)
{
	int printed_len;
	bool in_sched = false;

	/* Suppress unimportant messages after panic happens */
	if (unlikely(suppress_printk))
		return 0;

	/*
	 * The messages on the panic CPU are the most important. If
	 * non-panic CPUs are generating any messages, they will be
	 * silently dropped.
	 */
	if (other_cpu_in_panic())
		return 0;

	if (level == LOGLEVEL_SCHED) {
		level = LOGLEVEL_DEFAULT;
		in_sched = true;
	}

	printk_delay(level);

	printed_len = vprintk_store(facility, level, dev_info, fmt, args);

	/* If called from the scheduler, we can not call up(). */
	if (!in_sched) {
		/*
		 * The caller may be holding system-critical or
		 * timing-sensitive locks. Disable preemption during
		 * printing of all remaining records to all consoles so that
		 * this context can return as soon as possible. Hopefully
		 * another printk() caller will take over the printing.
		 */
		preempt_disable();
		/*
		 * Try to acquire and then immediately release the console
		 * semaphore. The release will print out buffers. With the
		 * spinning variant, this context tries to take over the
		 * printing from another printing context.
		 */
		if (console_trylock_spinning())
			console_unlock();
		preempt_enable();
	}

	if (in_sched)
		defer_console_output();
	else
		wake_up_klogd();

	return printed_len;
}
EXPORT_SYMBOL(vprintk_emit);

int vprintk_default(const char *fmt, va_list args)
{
	return vprintk_emit(0, LOGLEVEL_DEFAULT, NULL, fmt, args);
}
EXPORT_SYMBOL_GPL(vprintk_default);

asmlinkage __visible int _printk(const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = vprintk(fmt, args);
	va_end(args);

	return r;
}
EXPORT_SYMBOL(_printk);

static bool pr_flush(int timeout_ms, bool reset_on_progress);
static bool __pr_flush(struct console *con, int timeout_ms, bool reset_on_progress);

#else /* CONFIG_PRINTK */

#define printk_time		false

#define prb_read_valid(rb, seq, r)	false
#define prb_first_valid_seq(rb)		0
#define prb_next_seq(rb)		0

static u64 syslog_seq;

static bool pr_flush(int timeout_ms, bool reset_on_progress) { return true; }
static bool __pr_flush(struct console *con, int timeout_ms, bool reset_on_progress) { return true; }

#endif /* CONFIG_PRINTK */

#ifdef CONFIG_EARLY_PRINTK
struct console *early_console;

asmlinkage __visible void early_printk(const char *fmt, ...)
{
	va_list ap;
	char buf[512];
	int n;

	if (!early_console)
		return;

	va_start(ap, fmt);
	n = vscnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	early_console->write(early_console, buf, n);
}
#endif

static void set_user_specified(struct console_cmdline *c, bool user_specified)
{
	if (!user_specified)
		return;

	/*
	 * @c console was defined by the user on the command line.
	 * Do not clear when added twice also by SPCR or the device tree.
	 */
	c->user_specified = true;
	/* At least one console defined by the user on the command line. */
	console_set_on_cmdline = 1;
}

static int __add_preferred_console(const char *name, const short idx, char *options,
				   char *brl_options, bool user_specified)
{
	struct console_cmdline *c;
	int i;

	/*
	 * We use a signed short index for struct console for device drivers to
	 * indicate a not yet assigned index or port. However, a negative index
	 * value is not valid for preferred console.
	 */
	if (idx < 0)
		return -EINVAL;

	/*
	 *	See if this tty is not yet registered, and
	 *	if we have a slot free.
	 */
	for (i = 0, c = console_cmdline;
	     i < MAX_CMDLINECONSOLES && c->name[0];
	     i++, c++) {
		if (strcmp(c->name, name) == 0 && c->index == idx) {
			if (!brl_options)
				preferred_console = i;
			set_user_specified(c, user_specified);
			return 0;
		}
	}
	if (i == MAX_CMDLINECONSOLES)
		return -E2BIG;
	if (!brl_options)
		preferred_console = i;
	strscpy(c->name, name, sizeof(c->name));
	c->options = options;
	set_user_specified(c, user_specified);
	braille_set_options(c, brl_options);

	c->index = idx;
	return 0;
}

static int __init console_msg_format_setup(char *str)
{
	if (!strcmp(str, "syslog"))
		console_msg_format = MSG_FORMAT_SYSLOG;
	if (!strcmp(str, "default"))
		console_msg_format = MSG_FORMAT_DEFAULT;
	return 1;
}
__setup("console_msg_format=", console_msg_format_setup);

/*
 * Set up a console.  Called via do_early_param() in init/main.c
 * for each "console=" parameter in the boot command line.
 */
static int __init console_setup(char *str)
{
	char buf[sizeof(console_cmdline[0].name) + 4]; /* 4 for "ttyS" */
	char *s, *options, *brl_options = NULL;
	int idx;

	/*
	 * console="" or console=null have been suggested as a way to
	 * disable console output. Use ttynull that has been created
	 * for exactly this purpose.
	 */
	if (str[0] == 0 || strcmp(str, "null") == 0) {
		__add_preferred_console("ttynull", 0, NULL, NULL, true);
		return 1;
	}

	if (_braille_console_setup(&str, &brl_options))
		return 1;

	/*
	 * Decode str into name, index, options.
	 */
	if (str[0] >= '0' && str[0] <= '9') {
		strcpy(buf, "ttyS");
		strncpy(buf + 4, str, sizeof(buf) - 5);
	} else {
		strncpy(buf, str, sizeof(buf) - 1);
	}
	buf[sizeof(buf) - 1] = 0;
	options = strchr(str, ',');
	if (options)
		*(options++) = 0;
#ifdef __sparc__
	if (!strcmp(str, "ttya"))
		strcpy(buf, "ttyS0");
	if (!strcmp(str, "ttyb"))
		strcpy(buf, "ttyS1");
#endif
	for (s = buf; *s; s++)
		if (isdigit(*s) || *s == ',')
			break;
	idx = simple_strtoul(s, NULL, 10);
	*s = 0;

	__add_preferred_console(buf, idx, options, brl_options, true);
	return 1;
}
__setup("console=", console_setup);

/**
 * add_preferred_console - add a device to the list of preferred consoles.
 * @name: device name
 * @idx: device index
 * @options: options for this console
 *
 * The last preferred console added will be used for kernel messages
 * and stdin/out/err for init.  Normally this is used by console_setup
 * above to handle user-supplied console arguments; however it can also
 * be used by arch-specific code either to override the user or more
 * commonly to provide a default console (ie from PROM variables) when
 * the user has not supplied one.
 */
int add_preferred_console(const char *name, const short idx, char *options)
{
	return __add_preferred_console(name, idx, options, NULL, false);
}

bool console_suspend_enabled = true;
EXPORT_SYMBOL(console_suspend_enabled);

static int __init console_suspend_disable(char *str)
{
	console_suspend_enabled = false;
	return 1;
}
__setup("no_console_suspend", console_suspend_disable);
module_param_named(console_suspend, console_suspend_enabled,
		bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(console_suspend, "suspend console during suspend"
	" and hibernate operations");

static bool printk_console_no_auto_verbose;

void console_verbose(void)
{
	if (console_loglevel && !printk_console_no_auto_verbose)
		console_loglevel = CONSOLE_LOGLEVEL_MOTORMOUTH;
}
EXPORT_SYMBOL_GPL(console_verbose);

module_param_named(console_no_auto_verbose, printk_console_no_auto_verbose, bool, 0644);
MODULE_PARM_DESC(console_no_auto_verbose, "Disable console loglevel raise to highest on oops/panic/etc");

/**
 * suspend_console - suspend the console subsystem
 *
 * This disables printk() while we go into suspend states
 */
void suspend_console(void)
{
	struct console *con;

	if (!console_suspend_enabled)
		return;
	pr_info("Suspending console(s) (use no_console_suspend to debug)\n");
	pr_flush(1000, true);

	console_list_lock();
	for_each_console(con)
		console_srcu_write_flags(con, con->flags | CON_SUSPENDED);
	console_list_unlock();

	/*
	 * Ensure that all SRCU list walks have completed. All printing
	 * contexts must be able to see that they are suspended so that it
	 * is guaranteed that all printing has stopped when this function
	 * completes.
	 */
	synchronize_srcu(&console_srcu);
}

void resume_console(void)
{
	struct console *con;

	if (!console_suspend_enabled)
		return;

	console_list_lock();
	for_each_console(con)
		console_srcu_write_flags(con, con->flags & ~CON_SUSPENDED);
	console_list_unlock();

	/*
	 * Ensure that all SRCU list walks have completed. All printing
	 * contexts must be able to see they are no longer suspended so
	 * that they are guaranteed to wake up and resume printing.
	 */
	synchronize_srcu(&console_srcu);

	pr_flush(1000, true);
}

/**
 * console_cpu_notify - print deferred console messages after CPU hotplug
 * @cpu: unused
 *
 * If printk() is called from a CPU that is not online yet, the messages
 * will be printed on the console only if there are CON_ANYTIME consoles.
 * This function is called when a new CPU comes online (or fails to come
 * up) or goes offline.
 */
static int console_cpu_notify(unsigned int cpu)
{
	if (!cpuhp_tasks_frozen) {
		/* If trylock fails, someone else is doing the printing */
		if (console_trylock())
			console_unlock();
	}
	return 0;
}

/**
 * console_lock - block the console subsystem from printing
 *
 * Acquires a lock which guarantees that no consoles will
 * be in or enter their write() callback.
 *
 * Can sleep, returns nothing.
 */
void console_lock(void)
{
	might_sleep();

	/* On panic, the console_lock must be left to the panic cpu. */
	while (other_cpu_in_panic())
		msleep(1000);

	down_console_sem();
	console_locked = 1;
	console_may_schedule = 1;
}
EXPORT_SYMBOL(console_lock);

/**
 * console_trylock - try to block the console subsystem from printing
 *
 * Try to acquire a lock which guarantees that no consoles will
 * be in or enter their write() callback.
 *
 * returns 1 on success, and 0 on failure to acquire the lock.
 */
int console_trylock(void)
{
	/* On panic, the console_lock must be left to the panic cpu. */
	if (other_cpu_in_panic())
		return 0;
	if (down_trylock_console_sem())
		return 0;
	console_locked = 1;
	console_may_schedule = 0;
	return 1;
}
EXPORT_SYMBOL(console_trylock);

int is_console_locked(void)
{
	return console_locked;
}
EXPORT_SYMBOL(is_console_locked);

/*
 * Check if the given console is currently capable and allowed to print
 * records.
 *
 * Requires the console_srcu_read_lock.
 */
static inline bool console_is_usable(struct console *con)
{
	short flags = console_srcu_read_flags(con);

	if (!(flags & CON_ENABLED))
		return false;

	if ((flags & CON_SUSPENDED))
		return false;

	if (!con->write)
		return false;

	/*
	 * Console drivers may assume that per-cpu resources have been
	 * allocated. So unless they're explicitly marked as being able to
	 * cope (CON_ANYTIME) don't call them until this CPU is officially up.
	 */
	if (!cpu_online(raw_smp_processor_id()) && !(flags & CON_ANYTIME))
		return false;

	return true;
}

static void __console_unlock(void)
{
	console_locked = 0;
	up_console_sem();
}

#ifdef CONFIG_PRINTK

/*
 * Prepend the message in @pmsg->pbufs->outbuf with a "dropped message". This
 * is achieved by shifting the existing message over and inserting the dropped
 * message.
 *
 * @pmsg is the printk message to prepend.
 *
 * @dropped is the dropped count to report in the dropped message.
 *
 * If the message text in @pmsg->pbufs->outbuf does not have enough space for
 * the dropped message, the message text will be sufficiently truncated.
 *
 * If @pmsg->pbufs->outbuf is modified, @pmsg->outbuf_len is updated.
 */
void console_prepend_dropped(struct printk_message *pmsg, unsigned long dropped)
{
	struct printk_buffers *pbufs = pmsg->pbufs;
	const size_t scratchbuf_sz = sizeof(pbufs->scratchbuf);
	const size_t outbuf_sz = sizeof(pbufs->outbuf);
	char *scratchbuf = &pbufs->scratchbuf[0];
	char *outbuf = &pbufs->outbuf[0];
	size_t len;

	len = scnprintf(scratchbuf, scratchbuf_sz,
		       "** %lu printk messages dropped **\n", dropped);

	/*
	 * Make sure outbuf is sufficiently large before prepending.
	 * Keep at least the prefix when the message must be truncated.
	 * It is a rather theoretical problem when someone tries to
	 * use a minimalist buffer.
	 */
	if (WARN_ON_ONCE(len + PRINTK_PREFIX_MAX >= outbuf_sz))
		return;

	if (pmsg->outbuf_len + len >= outbuf_sz) {
		/* Truncate the message, but keep it terminated. */
		pmsg->outbuf_len = outbuf_sz - (len + 1);
		outbuf[pmsg->outbuf_len] = 0;
	}

	memmove(outbuf + len, outbuf, pmsg->outbuf_len + 1);
	memcpy(outbuf, scratchbuf, len);
	pmsg->outbuf_len += len;
}

/*
 * Read and format the specified record (or a later record if the specified
 * record is not available).
 *
 * @pmsg will contain the formatted result. @pmsg->pbufs must point to a
 * struct printk_buffers.
 *
 * @seq is the record to read and format. If it is not available, the next
 * valid record is read.
 *
 * @is_extended specifies if the message should be formatted for extended
 * console output.
 *
 * @may_supress specifies if records may be skipped based on loglevel.
 *
 * Returns false if no record is available. Otherwise true and all fields
 * of @pmsg are valid. (See the documentation of struct printk_message
 * for information about the @pmsg fields.)
 */
bool printk_get_next_message(struct printk_message *pmsg, u64 seq,
			     bool is_extended, bool may_suppress)
{
	struct printk_buffers *pbufs = pmsg->pbufs;
	const size_t scratchbuf_sz = sizeof(pbufs->scratchbuf);
	const size_t outbuf_sz = sizeof(pbufs->outbuf);
	char *scratchbuf = &pbufs->scratchbuf[0];
	char *outbuf = &pbufs->outbuf[0];
	struct printk_info info;
	struct printk_record r;
	size_t len = 0;

	/*
	 * Formatting extended messages requires a separate buffer, so use the
	 * scratch buffer to read in the ringbuffer text.
	 *
	 * Formatting normal messages is done in-place, so read the ringbuffer
	 * text directly into the output buffer.
	 */
	if (is_extended)
		prb_rec_init_rd(&r, &info, scratchbuf, scratchbuf_sz);
	else
		prb_rec_init_rd(&r, &info, outbuf, outbuf_sz);

	if (!prb_read_valid(prb, seq, &r))
		return false;

	pmsg->seq = r.info->seq;
	pmsg->dropped = r.info->seq - seq;

	/* Skip record that has level above the console loglevel. */
	if (may_suppress && suppress_message_printing(r.info->level))
		goto out;

	if (is_extended) {
		len = info_print_ext_header(outbuf, outbuf_sz, r.info);
		len += msg_print_ext_body(outbuf + len, outbuf_sz - len,
					  &r.text_buf[0], r.info->text_len, &r.info->dev_info);
	} else {
		len = record_print_text(&r, console_msg_format & MSG_FORMAT_SYSLOG, printk_time);
	}
out:
	pmsg->outbuf_len = len;
	return true;
}

/*
 * Used as the printk buffers for non-panic, serialized console printing.
 * This is for legacy (!CON_NBCON) as well as all boot (CON_BOOT) consoles.
 * Its usage requires the console_lock held.
 */
struct printk_buffers printk_shared_pbufs;

/*
 * Print one record for the given console. The record printed is whatever
 * record is the next available record for the given console.
 *
 * @handover will be set to true if a printk waiter has taken over the
 * console_lock, in which case the caller is no longer holding both the
 * console_lock and the SRCU read lock. Otherwise it is set to false.
 *
 * @cookie is the cookie from the SRCU read lock.
 *
 * Returns false if the given console has no next record to print, otherwise
 * true.
 *
 * Requires the console_lock and the SRCU read lock.
 */
static bool console_emit_next_record(struct console *con, bool *handover, int cookie)
{
	bool is_extended = console_srcu_read_flags(con) & CON_EXTENDED;
	char *outbuf = &printk_shared_pbufs.outbuf[0];
	struct printk_message pmsg = {
		.pbufs = &printk_shared_pbufs,
	};
	unsigned long flags;

	*handover = false;

	if (!printk_get_next_message(&pmsg, con->seq, is_extended, true))
		return false;

	con->dropped += pmsg.dropped;

	/* Skip messages of formatted length 0. */
	if (pmsg.outbuf_len == 0) {
		con->seq = pmsg.seq + 1;
		goto skip;
	}

	if (con->dropped && !is_extended) {
		console_prepend_dropped(&pmsg, con->dropped);
		con->dropped = 0;
	}

	/*
	 * While actively printing out messages, if another printk()
	 * were to occur on another CPU, it may wait for this one to
	 * finish. This task can not be preempted if there is a
	 * waiter waiting to take over.
	 *
	 * Interrupts are disabled because the hand over to a waiter
	 * must not be interrupted until the hand over is completed
	 * (@console_waiter is cleared).
	 */
	printk_safe_enter_irqsave(flags);
	console_lock_spinning_enable();

	/* Do not trace print latency. */
	stop_critical_timings();

	/* Write everything out to the hardware. */
	con->write(con, outbuf, pmsg.outbuf_len);

	start_critical_timings();

	con->seq = pmsg.seq + 1;

	*handover = console_lock_spinning_disable_and_check(cookie);
	printk_safe_exit_irqrestore(flags);
skip:
	return true;
}

#else

static bool console_emit_next_record(struct console *con, bool *handover, int cookie)
{
	*handover = false;
	return false;
}

#endif /* CONFIG_PRINTK */

/*
 * Print out all remaining records to all consoles.
 *
 * @do_cond_resched is set by the caller. It can be true only in schedulable
 * context.
 *
 * @next_seq is set to the sequence number after the last available record.
 * The value is valid only when this function returns true. It means that all
 * usable consoles are completely flushed.
 *
 * @handover will be set to true if a printk waiter has taken over the
 * console_lock, in which case the caller is no longer holding the
 * console_lock. Otherwise it is set to false.
 *
 * Returns true when there was at least one usable console and all messages
 * were flushed to all usable consoles. A returned false informs the caller
 * that everything was not flushed (either there were no usable consoles or
 * another context has taken over printing or it is a panic situation and this
 * is not the panic CPU). Regardless the reason, the caller should assume it
 * is not useful to immediately try again.
 *
 * Requires the console_lock.
 */
static bool console_flush_all(bool do_cond_resched, u64 *next_seq, bool *handover)
{
	bool any_usable = false;
	struct console *con;
	bool any_progress;
	int cookie;

	*next_seq = 0;
	*handover = false;

	do {
		any_progress = false;

		cookie = console_srcu_read_lock();
		for_each_console_srcu(con) {
			bool progress;

			if (!console_is_usable(con))
				continue;
			any_usable = true;

			progress = console_emit_next_record(con, handover, cookie);

			/*
			 * If a handover has occurred, the SRCU read lock
			 * is already released.
			 */
			if (*handover)
				return false;

			/* Track the next of the highest seq flushed. */
			if (con->seq > *next_seq)
				*next_seq = con->seq;

			if (!progress)
				continue;
			any_progress = true;

			/* Allow panic_cpu to take over the consoles safely. */
			if (other_cpu_in_panic())
				goto abandon;

			if (do_cond_resched)
				cond_resched();
		}
		console_srcu_read_unlock(cookie);
	} while (any_progress);

	return any_usable;

abandon:
	console_srcu_read_unlock(cookie);
	return false;
}

/**
 * console_unlock - unblock the console subsystem from printing
 *
 * Releases the console_lock which the caller holds to block printing of
 * the console subsystem.
 *
 * While the console_lock was held, console output may have been buffered
 * by printk().  If this is the case, console_unlock(); emits
 * the output prior to releasing the lock.
 *
 * console_unlock(); may be called from any context.
 */
void console_unlock(void)
{
	bool do_cond_resched;
	bool handover;
	bool flushed;
	u64 next_seq;

	/*
	 * Console drivers are called with interrupts disabled, so
	 * @console_may_schedule should be cleared before; however, we may
	 * end up dumping a lot of lines, for example, if called from
	 * console registration path, and should invoke cond_resched()
	 * between lines if allowable.  Not doing so can cause a very long
	 * scheduling stall on a slow console leading to RCU stall and
	 * softlockup warnings which exacerbate the issue with more
	 * messages practically incapacitating the system. Therefore, create
	 * a local to use for the printing loop.
	 */
	do_cond_resched = console_may_schedule;

	do {
		console_may_schedule = 0;

		flushed = console_flush_all(do_cond_resched, &next_seq, &handover);
		if (!handover)
			__console_unlock();

		/*
		 * Abort if there was a failure to flush all messages to all
		 * usable consoles. Either it is not possible to flush (in
		 * which case it would be an infinite loop of retrying) or
		 * another context has taken over printing.
		 */
		if (!flushed)
			break;

		/*
		 * Some context may have added new records after
		 * console_flush_all() but before unlocking the console.
		 * Re-check if there is a new record to flush. If the trylock
		 * fails, another context is already handling the printing.
		 */
	} while (prb_read_valid(prb, next_seq, NULL) && console_trylock());
}
EXPORT_SYMBOL(console_unlock);

/**
 * console_conditional_schedule - yield the CPU if required
 *
 * If the console code is currently allowed to sleep, and
 * if this CPU should yield the CPU to another task, do
 * so here.
 *
 * Must be called within console_lock();.
 */
void __sched console_conditional_schedule(void)
{
	if (console_may_schedule)
		cond_resched();
}
EXPORT_SYMBOL(console_conditional_schedule);

void console_unblank(void)
{
	bool found_unblank = false;
	struct console *c;
	int cookie;

	/*
	 * First check if there are any consoles implementing the unblank()
	 * callback. If not, there is no reason to continue and take the
	 * console lock, which in particular can be dangerous if
	 * @oops_in_progress is set.
	 */
	cookie = console_srcu_read_lock();
	for_each_console_srcu(c) {
		if ((console_srcu_read_flags(c) & CON_ENABLED) && c->unblank) {
			found_unblank = true;
			break;
		}
	}
	console_srcu_read_unlock(cookie);
	if (!found_unblank)
		return;

	/*
	 * Stop console printing because the unblank() callback may
	 * assume the console is not within its write() callback.
	 *
	 * If @oops_in_progress is set, this may be an atomic context.
	 * In that case, attempt a trylock as best-effort.
	 */
	if (oops_in_progress) {
		/* Semaphores are not NMI-safe. */
		if (in_nmi())
			return;

		/*
		 * Attempting to trylock the console lock can deadlock
		 * if another CPU was stopped while modifying the
		 * semaphore. "Hope and pray" that this is not the
		 * current situation.
		 */
		if (down_trylock_console_sem() != 0)
			return;
	} else
		console_lock();

	console_locked = 1;
	console_may_schedule = 0;

	cookie = console_srcu_read_lock();
	for_each_console_srcu(c) {
		if ((console_srcu_read_flags(c) & CON_ENABLED) && c->unblank)
			c->unblank();
	}
	console_srcu_read_unlock(cookie);

	console_unlock();

	if (!oops_in_progress)
		pr_flush(1000, true);
}

/**
 * console_flush_on_panic - flush console content on panic
 * @mode: flush all messages in buffer or just the pending ones
 *
 * Immediately output all pending messages no matter what.
 */
void console_flush_on_panic(enum con_flush_mode mode)
{
	bool handover;
	u64 next_seq;

	/*
	 * Ignore the console lock and flush out the messages. Attempting a
	 * trylock would not be useful because:
	 *
	 *   - if it is contended, it must be ignored anyway
	 *   - console_lock() and console_trylock() block and fail
	 *     respectively in panic for non-panic CPUs
	 *   - semaphores are not NMI-safe
	 */

	/*
	 * If another context is holding the console lock,
	 * @console_may_schedule might be set. Clear it so that
	 * this context does not call cond_resched() while flushing.
	 */
	console_may_schedule = 0;

	if (mode == CONSOLE_REPLAY_ALL) {
		struct console *c;
		short flags;
		int cookie;
		u64 seq;

		seq = prb_first_valid_seq(prb);

		cookie = console_srcu_read_lock();
		for_each_console_srcu(c) {
			flags = console_srcu_read_flags(c);

			if (flags & CON_NBCON) {
				nbcon_seq_force(c, seq);
			} else {
				/*
				 * This is an unsynchronized assignment. On
				 * panic legacy consoles are only best effort.
				 */
				c->seq = seq;
			}
		}
		console_srcu_read_unlock(cookie);
	}

	console_flush_all(false, &next_seq, &handover);
}

/*
 * Return the console tty driver structure and its associated index
 */
struct tty_driver *console_device(int *index)
{
	struct console *c;
	struct tty_driver *driver = NULL;
	int cookie;

	/*
	 * Take console_lock to serialize device() callback with
	 * other console operations. For example, fg_console is
	 * modified under console_lock when switching vt.
	 */
	console_lock();

	cookie = console_srcu_read_lock();
	for_each_console_srcu(c) {
		if (!c->device)
			continue;
		driver = c->device(c, index);
		if (driver)
			break;
	}
	console_srcu_read_unlock(cookie);

	console_unlock();
	return driver;
}

/*
 * Prevent further output on the passed console device so that (for example)
 * serial drivers can disable console output before suspending a port, and can
 * re-enable output afterwards.
 */
void console_stop(struct console *console)
{
	__pr_flush(console, 1000, true);
	console_list_lock();
	console_srcu_write_flags(console, console->flags & ~CON_ENABLED);
	console_list_unlock();

	/*
	 * Ensure that all SRCU list walks have completed. All contexts must
	 * be able to see that this console is disabled so that (for example)
	 * the caller can suspend the port without risk of another context
	 * using the port.
	 */
	synchronize_srcu(&console_srcu);
}
EXPORT_SYMBOL(console_stop);

void console_start(struct console *console)
{
	console_list_lock();
	console_srcu_write_flags(console, console->flags | CON_ENABLED);
	console_list_unlock();
	__pr_flush(console, 1000, true);
}
EXPORT_SYMBOL(console_start);

static int __read_mostly keep_bootcon;

static int __init keep_bootcon_setup(char *str)
{
	keep_bootcon = 1;
	pr_info("debug: skip boot console de-registration.\n");

	return 0;
}

early_param("keep_bootcon", keep_bootcon_setup);

static int console_call_setup(struct console *newcon, char *options)
{
	int err;

	if (!newcon->setup)
		return 0;

	/* Synchronize with possible boot console. */
	console_lock();
	err = newcon->setup(newcon, options);
	console_unlock();

	return err;
}

/*
 * This is called by register_console() to try to match
 * the newly registered console with any of the ones selected
 * by either the command line or add_preferred_console() and
 * setup/enable it.
 *
 * Care need to be taken with consoles that are statically
 * enabled such as netconsole
 */
static int try_enable_preferred_console(struct console *newcon,
					bool user_specified)
{
	struct console_cmdline *c;
	int i, err;

	for (i = 0, c = console_cmdline;
	     i < MAX_CMDLINECONSOLES && c->name[0];
	     i++, c++) {
		if (c->user_specified != user_specified)
			continue;
		if (!newcon->match ||
		    newcon->match(newcon, c->name, c->index, c->options) != 0) {
			/* default matching */
			BUILD_BUG_ON(sizeof(c->name) != sizeof(newcon->name));
			if (strcmp(c->name, newcon->name) != 0)
				continue;
			if (newcon->index >= 0 &&
			    newcon->index != c->index)
				continue;
			if (newcon->index < 0)
				newcon->index = c->index;

			if (_braille_register_console(newcon, c))
				return 0;

			err = console_call_setup(newcon, c->options);
			if (err)
				return err;
		}
		newcon->flags |= CON_ENABLED;
		if (i == preferred_console)
			newcon->flags |= CON_CONSDEV;
		return 0;
	}

	/*
	 * Some consoles, such as pstore and netconsole, can be enabled even
	 * without matching. Accept the pre-enabled consoles only when match()
	 * and setup() had a chance to be called.
	 */
	if (newcon->flags & CON_ENABLED && c->user_specified ==	user_specified)
		return 0;

	return -ENOENT;
}

/* Try to enable the console unconditionally */
static void try_enable_default_console(struct console *newcon)
{
	if (newcon->index < 0)
		newcon->index = 0;

	if (console_call_setup(newcon, NULL) != 0)
		return;

	newcon->flags |= CON_ENABLED;

	if (newcon->device)
		newcon->flags |= CON_CONSDEV;
}

static void console_init_seq(struct console *newcon, bool bootcon_registered)
{
	struct console *con;
	bool handover;

	if (newcon->flags & (CON_PRINTBUFFER | CON_BOOT)) {
		/* Get a consistent copy of @syslog_seq. */
		mutex_lock(&syslog_lock);
		newcon->seq = syslog_seq;
		mutex_unlock(&syslog_lock);
	} else {
		/* Begin with next message added to ringbuffer. */
		newcon->seq = prb_next_seq(prb);

		/*
		 * If any enabled boot consoles are due to be unregistered
		 * shortly, some may not be caught up and may be the same
		 * device as @newcon. Since it is not known which boot console
		 * is the same device, flush all consoles and, if necessary,
		 * start with the message of the enabled boot console that is
		 * the furthest behind.
		 */
		if (bootcon_registered && !keep_bootcon) {
			/*
			 * Hold the console_lock to stop console printing and
			 * guarantee safe access to console->seq.
			 */
			console_lock();

			/*
			 * Flush all consoles and set the console to start at
			 * the next unprinted sequence number.
			 */
			if (!console_flush_all(true, &newcon->seq, &handover)) {
				/*
				 * Flushing failed. Just choose the lowest
				 * sequence of the enabled boot consoles.
				 */

				/*
				 * If there was a handover, this context no
				 * longer holds the console_lock.
				 */
				if (handover)
					console_lock();

				newcon->seq = prb_next_seq(prb);
				for_each_console(con) {
					if ((con->flags & CON_BOOT) &&
					    (con->flags & CON_ENABLED) &&
					    con->seq < newcon->seq) {
						newcon->seq = con->seq;
					}
				}
			}

			console_unlock();
		}
	}
}

#define console_first()				\
	hlist_entry(console_list.first, struct console, node)

static int unregister_console_locked(struct console *console);

/*
 * The console driver calls this routine during kernel initialization
 * to register the console printing procedure with printk() and to
 * print any messages that were printed by the kernel before the
 * console driver was initialized.
 *
 * This can happen pretty early during the boot process (because of
 * early_printk) - sometimes before setup_arch() completes - be careful
 * of what kernel features are used - they may not be initialised yet.
 *
 * There are two types of consoles - bootconsoles (early_printk) and
 * "real" consoles (everything which is not a bootconsole) which are
 * handled differently.
 *  - Any number of bootconsoles can be registered at any time.
 *  - As soon as a "real" console is registered, all bootconsoles
 *    will be unregistered automatically.
 *  - Once a "real" console is registered, any attempt to register a
 *    bootconsoles will be rejected
 */
void register_console(struct console *newcon)
{
	struct console *con;
	bool bootcon_registered = false;
	bool realcon_registered = false;
	int err;

	console_list_lock();

	for_each_console(con) {
		if (WARN(con == newcon, "console '%s%d' already registered\n",
					 con->name, con->index)) {
			goto unlock;
		}

		if (con->flags & CON_BOOT)
			bootcon_registered = true;
		else
			realcon_registered = true;
	}

	/* Do not register boot consoles when there already is a real one. */
	if ((newcon->flags & CON_BOOT) && realcon_registered) {
		pr_info("Too late to register bootconsole %s%d\n",
			newcon->name, newcon->index);
		goto unlock;
	}

	if (newcon->flags & CON_NBCON) {
		/*
		 * Ensure the nbcon console buffers can be allocated
		 * before modifying any global data.
		 */
		if (!nbcon_alloc(newcon))
			goto unlock;
	}

	/*
	 * See if we want to enable this console driver by default.
	 *
	 * Nope when a console is preferred by the command line, device
	 * tree, or SPCR.
	 *
	 * The first real console with tty binding (driver) wins. More
	 * consoles might get enabled before the right one is found.
	 *
	 * Note that a console with tty binding will have CON_CONSDEV
	 * flag set and will be first in the list.
	 */
	if (preferred_console < 0) {
		if (hlist_empty(&console_list) || !console_first()->device ||
		    console_first()->flags & CON_BOOT) {
			try_enable_default_console(newcon);
		}
	}

	/* See if this console matches one we selected on the command line */
	err = try_enable_preferred_console(newcon, true);

	/* If not, try to match against the platform default(s) */
	if (err == -ENOENT)
		err = try_enable_preferred_console(newcon, false);

	/* printk() messages are not printed to the Braille console. */
	if (err || newcon->flags & CON_BRL) {
		if (newcon->flags & CON_NBCON)
			nbcon_free(newcon);
		goto unlock;
	}

	/*
	 * If we have a bootconsole, and are switching to a real console,
	 * don't print everything out again, since when the boot console, and
	 * the real console are the same physical device, it's annoying to
	 * see the beginning boot messages twice
	 */
	if (bootcon_registered &&
	    ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV)) {
		newcon->flags &= ~CON_PRINTBUFFER;
	}

	newcon->dropped = 0;
	console_init_seq(newcon, bootcon_registered);

	if (newcon->flags & CON_NBCON)
		nbcon_init(newcon);

	/*
	 * Put this console in the list - keep the
	 * preferred driver at the head of the list.
	 */
	if (hlist_empty(&console_list)) {
		/* Ensure CON_CONSDEV is always set for the head. */
		newcon->flags |= CON_CONSDEV;
		hlist_add_head_rcu(&newcon->node, &console_list);

	} else if (newcon->flags & CON_CONSDEV) {
		/* Only the new head can have CON_CONSDEV set. */
		console_srcu_write_flags(console_first(), console_first()->flags & ~CON_CONSDEV);
		hlist_add_head_rcu(&newcon->node, &console_list);

	} else {
		hlist_add_behind_rcu(&newcon->node, console_list.first);
	}

	/*
	 * No need to synchronize SRCU here! The caller does not rely
	 * on all contexts being able to see the new console before
	 * register_console() completes.
	 */

	console_sysfs_notify();

	/*
	 * By unregistering the bootconsoles after we enable the real console
	 * we get the "console xxx enabled" message on all the consoles -
	 * boot consoles, real consoles, etc - this is to ensure that end
	 * users know there might be something in the kernel's log buffer that
	 * went to the bootconsole (that they do not see on the real console)
	 */
	con_printk(KERN_INFO, newcon, "enabled\n");
	if (bootcon_registered &&
	    ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV) &&
	    !keep_bootcon) {
		struct hlist_node *tmp;

		hlist_for_each_entry_safe(con, tmp, &console_list, node) {
			if (con->flags & CON_BOOT)
				unregister_console_locked(con);
		}
	}
unlock:
	console_list_unlock();
}
EXPORT_SYMBOL(register_console);

/* Must be called under console_list_lock(). */
static int unregister_console_locked(struct console *console)
{
	int res;

	lockdep_assert_console_list_lock_held();

	con_printk(KERN_INFO, console, "disabled\n");

	res = _braille_unregister_console(console);
	if (res < 0)
		return res;
	if (res > 0)
		return 0;

	/* Disable it unconditionally */
	console_srcu_write_flags(console, console->flags & ~CON_ENABLED);

	if (!console_is_registered_locked(console))
		return -ENODEV;

	hlist_del_init_rcu(&console->node);

	/*
	 * <HISTORICAL>
	 * If this isn't the last console and it has CON_CONSDEV set, we
	 * need to set it on the next preferred console.
	 * </HISTORICAL>
	 *
	 * The above makes no sense as there is no guarantee that the next
	 * console has any device attached. Oh well....
	 */
	if (!hlist_empty(&console_list) && console->flags & CON_CONSDEV)
		console_srcu_write_flags(console_first(), console_first()->flags | CON_CONSDEV);

	/*
	 * Ensure that all SRCU list walks have completed. All contexts
	 * must not be able to see this console in the list so that any
	 * exit/cleanup routines can be performed safely.
	 */
	synchronize_srcu(&console_srcu);

	if (console->flags & CON_NBCON)
		nbcon_free(console);

	console_sysfs_notify();

	if (console->exit)
		res = console->exit(console);

	return res;
}

int unregister_console(struct console *console)
{
	int res;

	console_list_lock();
	res = unregister_console_locked(console);
	console_list_unlock();
	return res;
}
EXPORT_SYMBOL(unregister_console);

/**
 * console_force_preferred_locked - force a registered console preferred
 * @con: The registered console to force preferred.
 *
 * Must be called under console_list_lock().
 */
void console_force_preferred_locked(struct console *con)
{
	struct console *cur_pref_con;

	if (!console_is_registered_locked(con))
		return;

	cur_pref_con = console_first();

	/* Already preferred? */
	if (cur_pref_con == con)
		return;

	/*
	 * Delete, but do not re-initialize the entry. This allows the console
	 * to continue to appear registered (via any hlist_unhashed_lockless()
	 * checks), even though it was briefly removed from the console list.
	 */
	hlist_del_rcu(&con->node);

	/*
	 * Ensure that all SRCU list walks have completed so that the console
	 * can be added to the beginning of the console list and its forward
	 * list pointer can be re-initialized.
	 */
	synchronize_srcu(&console_srcu);

	con->flags |= CON_CONSDEV;
	WARN_ON(!con->device);

	/* Only the new head can have CON_CONSDEV set. */
	console_srcu_write_flags(cur_pref_con, cur_pref_con->flags & ~CON_CONSDEV);
	hlist_add_head_rcu(&con->node, &console_list);
}
EXPORT_SYMBOL(console_force_preferred_locked);

/*
 * Initialize the console device. This is called *early*, so
 * we can't necessarily depend on lots of kernel help here.
 * Just do some early initializations, and do the complex setup
 * later.
 */
void __init console_init(void)
{
	int ret;
	initcall_t call;
	initcall_entry_t *ce;

	/* Setup the default TTY line discipline. */
	n_tty_init();

	/*
	 * set up the console device so that later boot sequences can
	 * inform about problems etc..
	 */
	ce = __con_initcall_start;
	trace_initcall_level("console");
	while (ce < __con_initcall_end) {
		call = initcall_from_entry(ce);
		trace_initcall_start(call);
		ret = call();
		trace_initcall_finish(call, ret);
		ce++;
	}
}

/*
 * Some boot consoles access data that is in the init section and which will
 * be discarded after the initcalls have been run. To make sure that no code
 * will access this data, unregister the boot consoles in a late initcall.
 *
 * If for some reason, such as deferred probe or the driver being a loadable
 * module, the real console hasn't registered yet at this point, there will
 * be a brief interval in which no messages are logged to the console, which
 * makes it difficult to diagnose problems that occur during this time.
 *
 * To mitigate this problem somewhat, only unregister consoles whose memory
 * intersects with the init section. Note that all other boot consoles will
 * get unregistered when the real preferred console is registered.
 */
static int __init printk_late_init(void)
{
	struct hlist_node *tmp;
	struct console *con;
	int ret;

	console_list_lock();
	hlist_for_each_entry_safe(con, tmp, &console_list, node) {
		if (!(con->flags & CON_BOOT))
			continue;

		/* Check addresses that might be used for enabled consoles. */
		if (init_section_intersects(con, sizeof(*con)) ||
		    init_section_contains(con->write, 0) ||
		    init_section_contains(con->read, 0) ||
		    init_section_contains(con->device, 0) ||
		    init_section_contains(con->unblank, 0) ||
		    init_section_contains(con->data, 0)) {
			/*
			 * Please, consider moving the reported consoles out
			 * of the init section.
			 */
			pr_warn("bootconsole [%s%d] uses init memory and must be disabled even before the real one is ready\n",
				con->name, con->index);
			unregister_console_locked(con);
		}
	}
	console_list_unlock();

	ret = cpuhp_setup_state_nocalls(CPUHP_PRINTK_DEAD, "printk:dead", NULL,
					console_cpu_notify);
	WARN_ON(ret < 0);
	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "printk:online",
					console_cpu_notify, NULL);
	WARN_ON(ret < 0);
	printk_sysctl_init();
	return 0;
}
late_initcall(printk_late_init);

#if defined CONFIG_PRINTK
/* If @con is specified, only wait for that console. Otherwise wait for all. */
static bool __pr_flush(struct console *con, int timeout_ms, bool reset_on_progress)
{
	unsigned long timeout_jiffies = msecs_to_jiffies(timeout_ms);
	unsigned long remaining_jiffies = timeout_jiffies;
	struct console *c;
	u64 last_diff = 0;
	u64 printk_seq;
	short flags;
	int cookie;
	u64 diff;
	u64 seq;

	might_sleep();

	seq = prb_next_reserve_seq(prb);

	/* Flush the consoles so that records up to @seq are printed. */
	console_lock();
	console_unlock();

	for (;;) {
		unsigned long begin_jiffies;
		unsigned long slept_jiffies;

		diff = 0;

		/*
		 * Hold the console_lock to guarantee safe access to
		 * console->seq. Releasing console_lock flushes more
		 * records in case @seq is still not printed on all
		 * usable consoles.
		 */
		console_lock();

		cookie = console_srcu_read_lock();
		for_each_console_srcu(c) {
			if (con && con != c)
				continue;

			flags = console_srcu_read_flags(c);

			/*
			 * If consoles are not usable, it cannot be expected
			 * that they make forward progress, so only increment
			 * @diff for usable consoles.
			 */
			if (!console_is_usable(c))
				continue;

			if (flags & CON_NBCON) {
				printk_seq = nbcon_seq_read(c);
			} else {
				printk_seq = c->seq;
			}

			if (printk_seq < seq)
				diff += seq - printk_seq;
		}
		console_srcu_read_unlock(cookie);

		if (diff != last_diff && reset_on_progress)
			remaining_jiffies = timeout_jiffies;

		console_unlock();

		/* Note: @diff is 0 if there are no usable consoles. */
		if (diff == 0 || remaining_jiffies == 0)
			break;

		/* msleep(1) might sleep much longer. Check time by jiffies. */
		begin_jiffies = jiffies;
		msleep(1);
		slept_jiffies = jiffies - begin_jiffies;

		remaining_jiffies -= min(slept_jiffies, remaining_jiffies);

		last_diff = diff;
	}

	return (diff == 0);
}

/**
 * pr_flush() - Wait for printing threads to catch up.
 *
 * @timeout_ms:        The maximum time (in ms) to wait.
 * @reset_on_progress: Reset the timeout if forward progress is seen.
 *
 * A value of 0 for @timeout_ms means no waiting will occur. A value of -1
 * represents infinite waiting.
 *
 * If @reset_on_progress is true, the timeout will be reset whenever any
 * printer has been seen to make some forward progress.
 *
 * Context: Process context. May sleep while acquiring console lock.
 * Return: true if all usable printers are caught up.
 */
static bool pr_flush(int timeout_ms, bool reset_on_progress)
{
	return __pr_flush(NULL, timeout_ms, reset_on_progress);
}

/*
 * Delayed printk version, for scheduler-internal messages:
 */
#define PRINTK_PENDING_WAKEUP	0x01
#define PRINTK_PENDING_OUTPUT	0x02

static DEFINE_PER_CPU(int, printk_pending);

static void wake_up_klogd_work_func(struct irq_work *irq_work)
{
	int pending = this_cpu_xchg(printk_pending, 0);

	if (pending & PRINTK_PENDING_OUTPUT) {
		/* If trylock fails, someone else is doing the printing */
		if (console_trylock())
			console_unlock();
	}

	if (pending & PRINTK_PENDING_WAKEUP)
		wake_up_interruptible(&log_wait);
}

static DEFINE_PER_CPU(struct irq_work, wake_up_klogd_work) =
	IRQ_WORK_INIT_LAZY(wake_up_klogd_work_func);

static void __wake_up_klogd(int val)
{
	if (!printk_percpu_data_ready())
		return;

	preempt_disable();
	/*
	 * Guarantee any new records can be seen by tasks preparing to wait
	 * before this context checks if the wait queue is empty.
	 *
	 * The full memory barrier within wq_has_sleeper() pairs with the full
	 * memory barrier within set_current_state() of
	 * prepare_to_wait_event(), which is called after ___wait_event() adds
	 * the waiter but before it has checked the wait condition.
	 *
	 * This pairs with devkmsg_read:A and syslog_print:A.
	 */
	if (wq_has_sleeper(&log_wait) || /* LMM(__wake_up_klogd:A) */
	    (val & PRINTK_PENDING_OUTPUT)) {
		this_cpu_or(printk_pending, val);
		irq_work_queue(this_cpu_ptr(&wake_up_klogd_work));
	}
	preempt_enable();
}

/**
 * wake_up_klogd - Wake kernel logging daemon
 *
 * Use this function when new records have been added to the ringbuffer
 * and the console printing of those records has already occurred or is
 * known to be handled by some other context. This function will only
 * wake the logging daemon.
 *
 * Context: Any context.
 */
void wake_up_klogd(void)
{
	__wake_up_klogd(PRINTK_PENDING_WAKEUP);
}

/**
 * defer_console_output - Wake kernel logging daemon and trigger
 *	console printing in a deferred context
 *
 * Use this function when new records have been added to the ringbuffer,
 * this context is responsible for console printing those records, but
 * the current context is not allowed to perform the console printing.
 * Trigger an irq_work context to perform the console printing. This
 * function also wakes the logging daemon.
 *
 * Context: Any context.
 */
void defer_console_output(void)
{
	/*
	 * New messages may have been added directly to the ringbuffer
	 * using vprintk_store(), so wake any waiters as well.
	 */
	__wake_up_klogd(PRINTK_PENDING_WAKEUP | PRINTK_PENDING_OUTPUT);
}

void printk_trigger_flush(void)
{
	defer_console_output();
}

int vprintk_deferred(const char *fmt, va_list args)
{
	return vprintk_emit(0, LOGLEVEL_SCHED, NULL, fmt, args);
}

int _printk_deferred(const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = vprintk_deferred(fmt, args);
	va_end(args);

	return r;
}

/*
 * printk rate limiting, lifted from the networking subsystem.
 *
 * This enforces a rate limit: not more than 10 kernel messages
 * every 5s to make a denial-of-service attack impossible.
 */
DEFINE_RATELIMIT_STATE(printk_ratelimit_state, 5 * HZ, 10);

int __printk_ratelimit(const char *func)
{
	return ___ratelimit(&printk_ratelimit_state, func);
}
EXPORT_SYMBOL(__printk_ratelimit);

/**
 * printk_timed_ratelimit - caller-controlled printk ratelimiting
 * @caller_jiffies: pointer to caller's state
 * @interval_msecs: minimum interval between prints
 *
 * printk_timed_ratelimit() returns true if more than @interval_msecs
 * milliseconds have elapsed since the last time printk_timed_ratelimit()
 * returned true.
 */
bool printk_timed_ratelimit(unsigned long *caller_jiffies,
			unsigned int interval_msecs)
{
	unsigned long elapsed = jiffies - *caller_jiffies;

	if (*caller_jiffies && elapsed <= msecs_to_jiffies(interval_msecs))
		return false;

	*caller_jiffies = jiffies;
	return true;
}
EXPORT_SYMBOL(printk_timed_ratelimit);

static DEFINE_SPINLOCK(dump_list_lock);
static LIST_HEAD(dump_list);

/**
 * kmsg_dump_register - register a kernel log dumper.
 * @dumper: pointer to the kmsg_dumper structure
 *
 * Adds a kernel log dumper to the system. The dump callback in the
 * structure will be called when the kernel oopses or panics and must be
 * set. Returns zero on success and %-EINVAL or %-EBUSY otherwise.
 */
int kmsg_dump_register(struct kmsg_dumper *dumper)
{
	unsigned long flags;
	int err = -EBUSY;

	/* The dump callback needs to be set */
	if (!dumper->dump)
		return -EINVAL;

	spin_lock_irqsave(&dump_list_lock, flags);
	/* Don't allow registering multiple times */
	if (!dumper->registered) {
		dumper->registered = 1;
		list_add_tail_rcu(&dumper->list, &dump_list);
		err = 0;
	}
	spin_unlock_irqrestore(&dump_list_lock, flags);

	return err;
}
EXPORT_SYMBOL_GPL(kmsg_dump_register);

/**
 * kmsg_dump_unregister - unregister a kmsg dumper.
 * @dumper: pointer to the kmsg_dumper structure
 *
 * Removes a dump device from the system. Returns zero on success and
 * %-EINVAL otherwise.
 */
int kmsg_dump_unregister(struct kmsg_dumper *dumper)
{
	unsigned long flags;
	int err = -EINVAL;

	spin_lock_irqsave(&dump_list_lock, flags);
	if (dumper->registered) {
		dumper->registered = 0;
		list_del_rcu(&dumper->list);
		err = 0;
	}
	spin_unlock_irqrestore(&dump_list_lock, flags);
	synchronize_rcu();

	return err;
}
EXPORT_SYMBOL_GPL(kmsg_dump_unregister);

static bool always_kmsg_dump;
module_param_named(always_kmsg_dump, always_kmsg_dump, bool, S_IRUGO | S_IWUSR);

const char *kmsg_dump_reason_str(enum kmsg_dump_reason reason)
{
	switch (reason) {
	case KMSG_DUMP_PANIC:
		return "Panic";
	case KMSG_DUMP_OOPS:
		return "Oops";
	case KMSG_DUMP_EMERG:
		return "Emergency";
	case KMSG_DUMP_SHUTDOWN:
		return "Shutdown";
	default:
		return "Unknown";
	}
}
EXPORT_SYMBOL_GPL(kmsg_dump_reason_str);

/**
 * kmsg_dump - dump kernel log to kernel message dumpers.
 * @reason: the reason (oops, panic etc) for dumping
 *
 * Call each of the registered dumper's dump() callback, which can
 * retrieve the kmsg records with kmsg_dump_get_line() or
 * kmsg_dump_get_buffer().
 */
void kmsg_dump(enum kmsg_dump_reason reason)
{
	struct kmsg_dumper *dumper;

	rcu_read_lock();
	list_for_each_entry_rcu(dumper, &dump_list, list) {
		enum kmsg_dump_reason max_reason = dumper->max_reason;

		/*
		 * If client has not provided a specific max_reason, default
		 * to KMSG_DUMP_OOPS, unless always_kmsg_dump was set.
		 */
		if (max_reason == KMSG_DUMP_UNDEF) {
			max_reason = always_kmsg_dump ? KMSG_DUMP_MAX :
							KMSG_DUMP_OOPS;
		}
		if (reason > max_reason)
			continue;

		/* invoke dumper which will iterate over records */
		dumper->dump(dumper, reason);
	}
	rcu_read_unlock();
}

/**
 * kmsg_dump_get_line - retrieve one kmsg log line
 * @iter: kmsg dump iterator
 * @syslog: include the "<4>" prefixes
 * @line: buffer to copy the line to
 * @size: maximum size of the buffer
 * @len: length of line placed into buffer
 *
 * Start at the beginning of the kmsg buffer, with the oldest kmsg
 * record, and copy one record into the provided buffer.
 *
 * Consecutive calls will return the next available record moving
 * towards the end of the buffer with the youngest messages.
 *
 * A return value of FALSE indicates that there are no more records to
 * read.
 */
bool kmsg_dump_get_line(struct kmsg_dump_iter *iter, bool syslog,
			char *line, size_t size, size_t *len)
{
	u64 min_seq = latched_seq_read_nolock(&clear_seq);
	struct printk_info info;
	unsigned int line_count;
	struct printk_record r;
	size_t l = 0;
	bool ret = false;

	if (iter->cur_seq < min_seq)
		iter->cur_seq = min_seq;

	prb_rec_init_rd(&r, &info, line, size);

	/* Read text or count text lines? */
	if (line) {
		if (!prb_read_valid(prb, iter->cur_seq, &r))
			goto out;
		l = record_print_text(&r, syslog, printk_time);
	} else {
		if (!prb_read_valid_info(prb, iter->cur_seq,
					 &info, &line_count)) {
			goto out;
		}
		l = get_record_print_text_size(&info, line_count, syslog,
					       printk_time);

	}

	iter->cur_seq = r.info->seq + 1;
	ret = true;
out:
	if (len)
		*len = l;
	return ret;
}
EXPORT_SYMBOL_GPL(kmsg_dump_get_line);

/**
 * kmsg_dump_get_buffer - copy kmsg log lines
 * @iter: kmsg dump iterator
 * @syslog: include the "<4>" prefixes
 * @buf: buffer to copy the line to
 * @size: maximum size of the buffer
 * @len_out: length of line placed into buffer
 *
 * Start at the end of the kmsg buffer and fill the provided buffer
 * with as many of the *youngest* kmsg records that fit into it.
 * If the buffer is large enough, all available kmsg records will be
 * copied with a single call.
 *
 * Consecutive calls will fill the buffer with the next block of
 * available older records, not including the earlier retrieved ones.
 *
 * A return value of FALSE indicates that there are no more records to
 * read.
 */
bool kmsg_dump_get_buffer(struct kmsg_dump_iter *iter, bool syslog,
			  char *buf, size_t size, size_t *len_out)
{
	u64 min_seq = latched_seq_read_nolock(&clear_seq);
	struct printk_info info;
	struct printk_record r;
	u64 seq;
	u64 next_seq;
	size_t len = 0;
	bool ret = false;
	bool time = printk_time;

	if (!buf || !size)
		goto out;

	if (iter->cur_seq < min_seq)
		iter->cur_seq = min_seq;

	if (prb_read_valid_info(prb, iter->cur_seq, &info, NULL)) {
		if (info.seq != iter->cur_seq) {
			/* messages are gone, move to first available one */
			iter->cur_seq = info.seq;
		}
	}

	/* last entry */
	if (iter->cur_seq >= iter->next_seq)
		goto out;

	/*
	 * Find first record that fits, including all following records,
	 * into the user-provided buffer for this dump. Pass in size-1
	 * because this function (by way of record_print_text()) will
	 * not write more than size-1 bytes of text into @buf.
	 */
	seq = find_first_fitting_seq(iter->cur_seq, iter->next_seq,
				     size - 1, syslog, time);

	/*
	 * Next kmsg_dump_get_buffer() invocation will dump block of
	 * older records stored right before this one.
	 */
	next_seq = seq;

	prb_rec_init_rd(&r, &info, buf, size);

	prb_for_each_record(seq, prb, seq, &r) {
		if (r.info->seq >= iter->next_seq)
			break;

		len += record_print_text(&r, syslog, time);

		/* Adjust record to store to remaining buffer space. */
		prb_rec_init_rd(&r, &info, buf + len, size - len);
	}

	iter->next_seq = next_seq;
	ret = true;
out:
	if (len_out)
		*len_out = len;
	return ret;
}
EXPORT_SYMBOL_GPL(kmsg_dump_get_buffer);

/**
 * kmsg_dump_rewind - reset the iterator
 * @iter: kmsg dump iterator
 *
 * Reset the dumper's iterator so that kmsg_dump_get_line() and
 * kmsg_dump_get_buffer() can be called again and used multiple
 * times within the same dumper.dump() callback.
 */
void kmsg_dump_rewind(struct kmsg_dump_iter *iter)
{
	iter->cur_seq = latched_seq_read_nolock(&clear_seq);
	iter->next_seq = prb_next_seq(prb);
}
EXPORT_SYMBOL_GPL(kmsg_dump_rewind);

#endif

#ifdef CONFIG_SMP
static atomic_t printk_cpu_sync_owner = ATOMIC_INIT(-1);
static atomic_t printk_cpu_sync_nested = ATOMIC_INIT(0);

/**
 * __printk_cpu_sync_wait() - Busy wait until the printk cpu-reentrant
 *                            spinning lock is not owned by any CPU.
 *
 * Context: Any context.
 */
void __printk_cpu_sync_wait(void)
{
	do {
		cpu_relax();
	} while (atomic_read(&printk_cpu_sync_owner) != -1);
}
EXPORT_SYMBOL(__printk_cpu_sync_wait);

/**
 * __printk_cpu_sync_try_get() - Try to acquire the printk cpu-reentrant
 *                               spinning lock.
 *
 * If no processor has the lock, the calling processor takes the lock and
 * becomes the owner. If the calling processor is already the owner of the
 * lock, this function succeeds immediately.
 *
 * Context: Any context. Expects interrupts to be disabled.
 * Return: 1 on success, otherwise 0.
 */
int __printk_cpu_sync_try_get(void)
{
	int cpu;
	int old;

	cpu = smp_processor_id();

	/*
	 * Guarantee loads and stores from this CPU when it is the lock owner
	 * are _not_ visible to the previous lock owner. This pairs with
	 * __printk_cpu_sync_put:B.
	 *
	 * Memory barrier involvement:
	 *
	 * If __printk_cpu_sync_try_get:A reads from __printk_cpu_sync_put:B,
	 * then __printk_cpu_sync_put:A can never read from
	 * __printk_cpu_sync_try_get:B.
	 *
	 * Relies on:
	 *
	 * RELEASE from __printk_cpu_sync_put:A to __printk_cpu_sync_put:B
	 * of the previous CPU
	 *    matching
	 * ACQUIRE from __printk_cpu_sync_try_get:A to
	 * __printk_cpu_sync_try_get:B of this CPU
	 */
	old = atomic_cmpxchg_acquire(&printk_cpu_sync_owner, -1,
				     cpu); /* LMM(__printk_cpu_sync_try_get:A) */
	if (old == -1) {
		/*
		 * This CPU is now the owner and begins loading/storing
		 * data: LMM(__printk_cpu_sync_try_get:B)
		 */
		return 1;

	} else if (old == cpu) {
		/* This CPU is already the owner. */
		atomic_inc(&printk_cpu_sync_nested);
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(__printk_cpu_sync_try_get);

/**
 * __printk_cpu_sync_put() - Release the printk cpu-reentrant spinning lock.
 *
 * The calling processor must be the owner of the lock.
 *
 * Context: Any context. Expects interrupts to be disabled.
 */
void __printk_cpu_sync_put(void)
{
	if (atomic_read(&printk_cpu_sync_nested)) {
		atomic_dec(&printk_cpu_sync_nested);
		return;
	}

	/*
	 * This CPU is finished loading/storing data:
	 * LMM(__printk_cpu_sync_put:A)
	 */

	/*
	 * Guarantee loads and stores from this CPU when it was the
	 * lock owner are visible to the next lock owner. This pairs
	 * with __printk_cpu_sync_try_get:A.
	 *
	 * Memory barrier involvement:
	 *
	 * If __printk_cpu_sync_try_get:A reads from __printk_cpu_sync_put:B,
	 * then __printk_cpu_sync_try_get:B reads from __printk_cpu_sync_put:A.
	 *
	 * Relies on:
	 *
	 * RELEASE from __printk_cpu_sync_put:A to __printk_cpu_sync_put:B
	 * of this CPU
	 *    matching
	 * ACQUIRE from __printk_cpu_sync_try_get:A to
	 * __printk_cpu_sync_try_get:B of the next CPU
	 */
	atomic_set_release(&printk_cpu_sync_owner,
			   -1); /* LMM(__printk_cpu_sync_put:B) */
}
EXPORT_SYMBOL(__printk_cpu_sync_put);
#endif /* CONFIG_SMP */
