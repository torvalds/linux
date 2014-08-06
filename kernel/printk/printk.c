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
#include <linux/interrupt.h>			/* For in_interrupt() */
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/security.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/aio.h>
#include <linux/syscalls.h>
#include <linux/kexec.h>
#include <linux/kdb.h>
#include <linux/ratelimit.h>
#include <linux/kmsg_dump.h>
#include <linux/syslog.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/rculist.h>
#include <linux/poll.h>
#include <linux/irq_work.h>
#include <linux/utsname.h>

#include <asm/uaccess.h>

#define CREATE_TRACE_POINTS
#include <trace/events/printk.h>

#include "console_cmdline.h"
#include "braille.h"

int console_printk[4] = {
	CONSOLE_LOGLEVEL_DEFAULT,	/* console_loglevel */
	DEFAULT_MESSAGE_LOGLEVEL,	/* default_message_loglevel */
	CONSOLE_LOGLEVEL_MIN,		/* minimum_console_loglevel */
	CONSOLE_LOGLEVEL_DEFAULT,	/* default_console_loglevel */
};

/* Deferred messaged from sched code are marked by this special level */
#define SCHED_MESSAGE_LOGLEVEL -2

/*
 * Low level drivers may need that to know if they can schedule in
 * their unblank() callback or not. So let's export it.
 */
int oops_in_progress;
EXPORT_SYMBOL(oops_in_progress);

/*
 * console_sem protects the console_drivers list, and also
 * provides serialisation for access to the entire console
 * driver system.
 */
static DEFINE_SEMAPHORE(console_sem);
struct console *console_drivers;
EXPORT_SYMBOL_GPL(console_drivers);

#ifdef CONFIG_LOCKDEP
static struct lockdep_map console_lock_dep_map = {
	.name = "console_lock"
};
#endif

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
	if (down_trylock(&console_sem))
		return 1;
	mutex_acquire(&console_lock_dep_map, 0, 1, ip);
	return 0;
}
#define down_trylock_console_sem() __down_trylock_console_sem(_RET_IP_)

#define up_console_sem() do { \
	mutex_release(&console_lock_dep_map, 1, _RET_IP_);\
	up(&console_sem);\
} while (0)

/*
 * This is used for debugging the mess that is the VT code by
 * keeping track if we have the console semaphore held. It's
 * definitely not the perfect debug tool (we don't know if _WE_
 * hold it are racing, but it helps tracking those weird code
 * path in the console code where we end up in places I want
 * locked without the console sempahore held
 */
static int console_locked, console_suspended;

/*
 * If exclusive_console is non-NULL then only this console is to be printed to.
 */
static struct console *exclusive_console;

/*
 *	Array of consoles built from command line options (console=)
 */

#define MAX_CMDLINECONSOLES 8

static struct console_cmdline console_cmdline[MAX_CMDLINECONSOLES];

static int selected_console = -1;
static int preferred_console = -1;
int console_set_on_cmdline;
EXPORT_SYMBOL(console_set_on_cmdline);

/* Flag: console code may call schedule() */
static int console_may_schedule;

/*
 * The printk log buffer consists of a chain of concatenated variable
 * length records. Every record starts with a record header, containing
 * the overall length of the record.
 *
 * The heads to the first and last entry in the buffer, as well as the
 * sequence numbers of these both entries are maintained when messages
 * are stored..
 *
 * If the heads indicate available messages, the length in the header
 * tells the start next message. A length == 0 for the next message
 * indicates a wrap-around to the beginning of the buffer.
 *
 * Every record carries the monotonic timestamp in microseconds, as well as
 * the standard userspace syslog level and syslog facility. The usual
 * kernel messages use LOG_KERN; userspace-injected messages always carry
 * a matching syslog facility, by default LOG_USER. The origin of every
 * message can be reliably determined that way.
 *
 * The human readable log message directly follows the message header. The
 * length of the message text is stored in the header, the stored message
 * is not terminated.
 *
 * Optionally, a message can carry a dictionary of properties (key/value pairs),
 * to provide userspace with a machine-readable message context.
 *
 * Examples for well-defined, commonly used property names are:
 *   DEVICE=b12:8               device identifier
 *                                b12:8         block dev_t
 *                                c127:3        char dev_t
 *                                n8            netdev ifindex
 *                                +sound:card0  subsystem:devname
 *   SUBSYSTEM=pci              driver-core subsystem name
 *
 * Valid characters in property names are [a-zA-Z0-9.-_]. The plain text value
 * follows directly after a '=' character. Every property is terminated by
 * a '\0' character. The last property is not terminated.
 *
 * Example of a message structure:
 *   0000  ff 8f 00 00 00 00 00 00      monotonic time in nsec
 *   0008  34 00                        record is 52 bytes long
 *   000a        0b 00                  text is 11 bytes long
 *   000c              1f 00            dictionary is 23 bytes long
 *   000e                    03 00      LOG_KERN (facility) LOG_ERR (level)
 *   0010  69 74 27 73 20 61 20 6c      "it's a l"
 *         69 6e 65                     "ine"
 *   001b           44 45 56 49 43      "DEVIC"
 *         45 3d 62 38 3a 32 00 44      "E=b8:2\0D"
 *         52 49 56 45 52 3d 62 75      "RIVER=bu"
 *         67                           "g"
 *   0032     00 00 00                  padding to next message header
 *
 * The 'struct printk_log' buffer header must never be directly exported to
 * userspace, it is a kernel-private implementation detail that might
 * need to be changed in the future, when the requirements change.
 *
 * /dev/kmsg exports the structured data in the following line format:
 *   "level,sequnum,timestamp;<message text>\n"
 *
 * The optional key/value pairs are attached as continuation lines starting
 * with a space character and terminated by a newline. All possible
 * non-prinatable characters are escaped in the "\xff" notation.
 *
 * Users of the export format should ignore possible additional values
 * separated by ',', and find the message after the ';' character.
 */

enum log_flags {
	LOG_NOCONS	= 1,	/* already flushed, do not print to console */
	LOG_NEWLINE	= 2,	/* text ended with a newline */
	LOG_PREFIX	= 4,	/* text started with a prefix */
	LOG_CONT	= 8,	/* text is a fragment of a continuation line */
};

struct printk_log {
	u64 ts_nsec;		/* timestamp in nanoseconds */
	u16 len;		/* length of entire record */
	u16 text_len;		/* length of text buffer */
	u16 dict_len;		/* length of dictionary buffer */
	u8 facility;		/* syslog facility */
	u8 flags:5;		/* internal record flags */
	u8 level:3;		/* syslog level */
};

/*
 * The logbuf_lock protects kmsg buffer, indices, counters.  This can be taken
 * within the scheduler's rq lock. It must be released before calling
 * console_unlock() or anything else that might wake up a process.
 */
static DEFINE_RAW_SPINLOCK(logbuf_lock);

#ifdef CONFIG_PRINTK
DECLARE_WAIT_QUEUE_HEAD(log_wait);
/* the next printk record to read by syslog(READ) or /proc/kmsg */
static u64 syslog_seq;
static u32 syslog_idx;
static enum log_flags syslog_prev;
static size_t syslog_partial;

/* index and sequence number of the first record stored in the buffer */
static u64 log_first_seq;
static u32 log_first_idx;

/* index and sequence number of the next record to store in the buffer */
static u64 log_next_seq;
static u32 log_next_idx;

/* the next printk record to write to the console */
static u64 console_seq;
static u32 console_idx;
static enum log_flags console_prev;

/* the next printk record to read after the last 'clear' command */
static u64 clear_seq;
static u32 clear_idx;

#define PREFIX_MAX		32
#define LOG_LINE_MAX		1024 - PREFIX_MAX

/* record buffer */
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
#define LOG_ALIGN 4
#else
#define LOG_ALIGN __alignof__(struct printk_log)
#endif
#define __LOG_BUF_LEN (1 << CONFIG_LOG_BUF_SHIFT)
static char __log_buf[__LOG_BUF_LEN] __aligned(LOG_ALIGN);
static char *log_buf = __log_buf;
static u32 log_buf_len = __LOG_BUF_LEN;

/* human readable text of the record */
static char *log_text(const struct printk_log *msg)
{
	return (char *)msg + sizeof(struct printk_log);
}

/* optional key/value pair dictionary attached to the record */
static char *log_dict(const struct printk_log *msg)
{
	return (char *)msg + sizeof(struct printk_log) + msg->text_len;
}

/* get record by index; idx must point to valid msg */
static struct printk_log *log_from_idx(u32 idx)
{
	struct printk_log *msg = (struct printk_log *)(log_buf + idx);

	/*
	 * A length == 0 record is the end of buffer marker. Wrap around and
	 * read the message at the start of the buffer.
	 */
	if (!msg->len)
		return (struct printk_log *)log_buf;
	return msg;
}

/* get next record; idx must point to valid msg */
static u32 log_next(u32 idx)
{
	struct printk_log *msg = (struct printk_log *)(log_buf + idx);

	/* length == 0 indicates the end of the buffer; wrap */
	/*
	 * A length == 0 record is the end of buffer marker. Wrap around and
	 * read the message at the start of the buffer as *this* one, and
	 * return the one after that.
	 */
	if (!msg->len) {
		msg = (struct printk_log *)log_buf;
		return msg->len;
	}
	return idx + msg->len;
}

/*
 * Check whether there is enough free space for the given message.
 *
 * The same values of first_idx and next_idx mean that the buffer
 * is either empty or full.
 *
 * If the buffer is empty, we must respect the position of the indexes.
 * They cannot be reset to the beginning of the buffer.
 */
static int logbuf_has_space(u32 msg_size, bool empty)
{
	u32 free;

	if (log_next_idx > log_first_idx || empty)
		free = max(log_buf_len - log_next_idx, log_first_idx);
	else
		free = log_first_idx - log_next_idx;

	/*
	 * We need space also for an empty header that signalizes wrapping
	 * of the buffer.
	 */
	return free >= msg_size + sizeof(struct printk_log);
}

static int log_make_free_space(u32 msg_size)
{
	while (log_first_seq < log_next_seq) {
		if (logbuf_has_space(msg_size, false))
			return 0;
		/* drop old messages until we have enough continuous space */
		log_first_idx = log_next(log_first_idx);
		log_first_seq++;
	}

	/* sequence numbers are equal, so the log buffer is empty */
	if (logbuf_has_space(msg_size, true))
		return 0;

	return -ENOMEM;
}

/* compute the message size including the padding bytes */
static u32 msg_used_size(u16 text_len, u16 dict_len, u32 *pad_len)
{
	u32 size;

	size = sizeof(struct printk_log) + text_len + dict_len;
	*pad_len = (-size) & (LOG_ALIGN - 1);
	size += *pad_len;

	return size;
}

/*
 * Define how much of the log buffer we could take at maximum. The value
 * must be greater than two. Note that only half of the buffer is available
 * when the index points to the middle.
 */
#define MAX_LOG_TAKE_PART 4
static const char trunc_msg[] = "<truncated>";

static u32 truncate_msg(u16 *text_len, u16 *trunc_msg_len,
			u16 *dict_len, u32 *pad_len)
{
	/*
	 * The message should not take the whole buffer. Otherwise, it might
	 * get removed too soon.
	 */
	u32 max_text_len = log_buf_len / MAX_LOG_TAKE_PART;
	if (*text_len > max_text_len)
		*text_len = max_text_len;
	/* enable the warning message */
	*trunc_msg_len = strlen(trunc_msg);
	/* disable the "dict" completely */
	*dict_len = 0;
	/* compute the size again, count also the warning message */
	return msg_used_size(*text_len + *trunc_msg_len, 0, pad_len);
}

/* insert record into the buffer, discard old ones, update heads */
static int log_store(int facility, int level,
		     enum log_flags flags, u64 ts_nsec,
		     const char *dict, u16 dict_len,
		     const char *text, u16 text_len)
{
	struct printk_log *msg;
	u32 size, pad_len;
	u16 trunc_msg_len = 0;

	/* number of '\0' padding bytes to next message */
	size = msg_used_size(text_len, dict_len, &pad_len);

	if (log_make_free_space(size)) {
		/* truncate the message if it is too long for empty buffer */
		size = truncate_msg(&text_len, &trunc_msg_len,
				    &dict_len, &pad_len);
		/* survive when the log buffer is too small for trunc_msg */
		if (log_make_free_space(size))
			return 0;
	}

	if (log_next_idx + size + sizeof(struct printk_log) > log_buf_len) {
		/*
		 * This message + an additional empty header does not fit
		 * at the end of the buffer. Add an empty header with len == 0
		 * to signify a wrap around.
		 */
		memset(log_buf + log_next_idx, 0, sizeof(struct printk_log));
		log_next_idx = 0;
	}

	/* fill message */
	msg = (struct printk_log *)(log_buf + log_next_idx);
	memcpy(log_text(msg), text, text_len);
	msg->text_len = text_len;
	if (trunc_msg_len) {
		memcpy(log_text(msg) + text_len, trunc_msg, trunc_msg_len);
		msg->text_len += trunc_msg_len;
	}
	memcpy(log_dict(msg), dict, dict_len);
	msg->dict_len = dict_len;
	msg->facility = facility;
	msg->level = level & 7;
	msg->flags = flags & 0x1f;
	if (ts_nsec > 0)
		msg->ts_nsec = ts_nsec;
	else
		msg->ts_nsec = local_clock();
	memset(log_dict(msg) + dict_len, 0, pad_len);
	msg->len = size;

	/* insert message */
	log_next_idx += msg->len;
	log_next_seq++;

	return msg->text_len;
}

#ifdef CONFIG_SECURITY_DMESG_RESTRICT
int dmesg_restrict = 1;
#else
int dmesg_restrict;
#endif

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

static int check_syslog_permissions(int type, bool from_file)
{
	/*
	 * If this is from /proc/kmsg and we've already opened it, then we've
	 * already done the capabilities checks at open time.
	 */
	if (from_file && type != SYSLOG_ACTION_OPEN)
		return 0;

	if (syslog_action_restricted(type)) {
		if (capable(CAP_SYSLOG))
			return 0;
		/*
		 * For historical reasons, accept CAP_SYS_ADMIN too, with
		 * a warning.
		 */
		if (capable(CAP_SYS_ADMIN)) {
			pr_warn_once("%s (%d): Attempt to access syslog with "
				     "CAP_SYS_ADMIN but no CAP_SYSLOG "
				     "(deprecated).\n",
				 current->comm, task_pid_nr(current));
			return 0;
		}
		return -EPERM;
	}
	return security_syslog(type);
}


/* /dev/kmsg - userspace message inject/listen interface */
struct devkmsg_user {
	u64 seq;
	u32 idx;
	enum log_flags prev;
	struct mutex lock;
	char buf[8192];
};

static ssize_t devkmsg_writev(struct kiocb *iocb, const struct iovec *iv,
			      unsigned long count, loff_t pos)
{
	char *buf, *line;
	int i;
	int level = default_message_loglevel;
	int facility = 1;	/* LOG_USER */
	size_t len = iov_length(iv, count);
	ssize_t ret = len;

	if (len > LOG_LINE_MAX)
		return -EINVAL;
	buf = kmalloc(len+1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	line = buf;
	for (i = 0; i < count; i++) {
		if (copy_from_user(line, iv[i].iov_base, iv[i].iov_len)) {
			ret = -EFAULT;
			goto out;
		}
		line += iv[i].iov_len;
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

		i = simple_strtoul(line+1, &endp, 10);
		if (endp && endp[0] == '>') {
			level = i & 7;
			if (i >> 3)
				facility = i >> 3;
			endp++;
			len -= endp - line;
			line = endp;
		}
	}
	line[len] = '\0';

	printk_emit(facility, level, NULL, 0, "%s", line);
out:
	kfree(buf);
	return ret;
}

static ssize_t devkmsg_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct devkmsg_user *user = file->private_data;
	struct printk_log *msg;
	u64 ts_usec;
	size_t i;
	char cont = '-';
	size_t len;
	ssize_t ret;

	if (!user)
		return -EBADF;

	ret = mutex_lock_interruptible(&user->lock);
	if (ret)
		return ret;
	raw_spin_lock_irq(&logbuf_lock);
	while (user->seq == log_next_seq) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			raw_spin_unlock_irq(&logbuf_lock);
			goto out;
		}

		raw_spin_unlock_irq(&logbuf_lock);
		ret = wait_event_interruptible(log_wait,
					       user->seq != log_next_seq);
		if (ret)
			goto out;
		raw_spin_lock_irq(&logbuf_lock);
	}

	if (user->seq < log_first_seq) {
		/* our last seen message is gone, return error and reset */
		user->idx = log_first_idx;
		user->seq = log_first_seq;
		ret = -EPIPE;
		raw_spin_unlock_irq(&logbuf_lock);
		goto out;
	}

	msg = log_from_idx(user->idx);
	ts_usec = msg->ts_nsec;
	do_div(ts_usec, 1000);

	/*
	 * If we couldn't merge continuation line fragments during the print,
	 * export the stored flags to allow an optional external merge of the
	 * records. Merging the records isn't always neccessarily correct, like
	 * when we hit a race during printing. In most cases though, it produces
	 * better readable output. 'c' in the record flags mark the first
	 * fragment of a line, '+' the following.
	 */
	if (msg->flags & LOG_CONT && !(user->prev & LOG_CONT))
		cont = 'c';
	else if ((msg->flags & LOG_CONT) ||
		 ((user->prev & LOG_CONT) && !(msg->flags & LOG_PREFIX)))
		cont = '+';

	len = sprintf(user->buf, "%u,%llu,%llu,%c;",
		      (msg->facility << 3) | msg->level,
		      user->seq, ts_usec, cont);
	user->prev = msg->flags;

	/* escape non-printable characters */
	for (i = 0; i < msg->text_len; i++) {
		unsigned char c = log_text(msg)[i];

		if (c < ' ' || c >= 127 || c == '\\')
			len += sprintf(user->buf + len, "\\x%02x", c);
		else
			user->buf[len++] = c;
	}
	user->buf[len++] = '\n';

	if (msg->dict_len) {
		bool line = true;

		for (i = 0; i < msg->dict_len; i++) {
			unsigned char c = log_dict(msg)[i];

			if (line) {
				user->buf[len++] = ' ';
				line = false;
			}

			if (c == '\0') {
				user->buf[len++] = '\n';
				line = true;
				continue;
			}

			if (c < ' ' || c >= 127 || c == '\\') {
				len += sprintf(user->buf + len, "\\x%02x", c);
				continue;
			}

			user->buf[len++] = c;
		}
		user->buf[len++] = '\n';
	}

	user->idx = log_next(user->idx);
	user->seq++;
	raw_spin_unlock_irq(&logbuf_lock);

	if (len > count) {
		ret = -EINVAL;
		goto out;
	}

	if (copy_to_user(buf, user->buf, len)) {
		ret = -EFAULT;
		goto out;
	}
	ret = len;
out:
	mutex_unlock(&user->lock);
	return ret;
}

static loff_t devkmsg_llseek(struct file *file, loff_t offset, int whence)
{
	struct devkmsg_user *user = file->private_data;
	loff_t ret = 0;

	if (!user)
		return -EBADF;
	if (offset)
		return -ESPIPE;

	raw_spin_lock_irq(&logbuf_lock);
	switch (whence) {
	case SEEK_SET:
		/* the first record */
		user->idx = log_first_idx;
		user->seq = log_first_seq;
		break;
	case SEEK_DATA:
		/*
		 * The first record after the last SYSLOG_ACTION_CLEAR,
		 * like issued by 'dmesg -c'. Reading /dev/kmsg itself
		 * changes no global state, and does not clear anything.
		 */
		user->idx = clear_idx;
		user->seq = clear_seq;
		break;
	case SEEK_END:
		/* after the last record */
		user->idx = log_next_idx;
		user->seq = log_next_seq;
		break;
	default:
		ret = -EINVAL;
	}
	raw_spin_unlock_irq(&logbuf_lock);
	return ret;
}

static unsigned int devkmsg_poll(struct file *file, poll_table *wait)
{
	struct devkmsg_user *user = file->private_data;
	int ret = 0;

	if (!user)
		return POLLERR|POLLNVAL;

	poll_wait(file, &log_wait, wait);

	raw_spin_lock_irq(&logbuf_lock);
	if (user->seq < log_next_seq) {
		/* return error when data has vanished underneath us */
		if (user->seq < log_first_seq)
			ret = POLLIN|POLLRDNORM|POLLERR|POLLPRI;
		else
			ret = POLLIN|POLLRDNORM;
	}
	raw_spin_unlock_irq(&logbuf_lock);

	return ret;
}

static int devkmsg_open(struct inode *inode, struct file *file)
{
	struct devkmsg_user *user;
	int err;

	/* write-only does not need any file context */
	if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		return 0;

	err = check_syslog_permissions(SYSLOG_ACTION_READ_ALL,
				       SYSLOG_FROM_READER);
	if (err)
		return err;

	user = kmalloc(sizeof(struct devkmsg_user), GFP_KERNEL);
	if (!user)
		return -ENOMEM;

	mutex_init(&user->lock);

	raw_spin_lock_irq(&logbuf_lock);
	user->idx = log_first_idx;
	user->seq = log_first_seq;
	raw_spin_unlock_irq(&logbuf_lock);

	file->private_data = user;
	return 0;
}

static int devkmsg_release(struct inode *inode, struct file *file)
{
	struct devkmsg_user *user = file->private_data;

	if (!user)
		return 0;

	mutex_destroy(&user->lock);
	kfree(user);
	return 0;
}

const struct file_operations kmsg_fops = {
	.open = devkmsg_open,
	.read = devkmsg_read,
	.aio_write = devkmsg_writev,
	.llseek = devkmsg_llseek,
	.poll = devkmsg_poll,
	.release = devkmsg_release,
};

#ifdef CONFIG_KEXEC
/*
 * This appends the listed symbols to /proc/vmcore
 *
 * /proc/vmcore is used by various utilities, like crash and makedumpfile to
 * obtain access to symbols that are otherwise very difficult to locate.  These
 * symbols are specifically used so that utilities can access and extract the
 * dmesg log from a vmcore file after a crash.
 */
void log_buf_kexec_setup(void)
{
	VMCOREINFO_SYMBOL(log_buf);
	VMCOREINFO_SYMBOL(log_buf_len);
	VMCOREINFO_SYMBOL(log_first_idx);
	VMCOREINFO_SYMBOL(log_next_idx);
	/*
	 * Export struct printk_log size and field offsets. User space tools can
	 * parse it and detect any changes to structure down the line.
	 */
	VMCOREINFO_STRUCT_SIZE(printk_log);
	VMCOREINFO_OFFSET(printk_log, ts_nsec);
	VMCOREINFO_OFFSET(printk_log, len);
	VMCOREINFO_OFFSET(printk_log, text_len);
	VMCOREINFO_OFFSET(printk_log, dict_len);
}
#endif

/* requested log_buf_len from kernel cmdline */
static unsigned long __initdata new_log_buf_len;

/* we practice scaling the ring buffer by powers of 2 */
static void __init log_buf_len_update(unsigned size)
{
	if (size)
		size = roundup_pow_of_two(size);
	if (size > log_buf_len)
		new_log_buf_len = size;
}

/* save requested log_buf_len since it's too early to process it */
static int __init log_buf_len_setup(char *str)
{
	unsigned size = memparse(str, &str);

	log_buf_len_update(size);

	return 0;
}
early_param("log_buf_len", log_buf_len_setup);

void __init setup_log_buf(int early)
{
	unsigned long flags;
	char *new_log_buf;
	int free;

	if (!new_log_buf_len)
		return;

	if (early) {
		new_log_buf =
			memblock_virt_alloc(new_log_buf_len, LOG_ALIGN);
	} else {
		new_log_buf = memblock_virt_alloc_nopanic(new_log_buf_len,
							  LOG_ALIGN);
	}

	if (unlikely(!new_log_buf)) {
		pr_err("log_buf_len: %ld bytes not available\n",
			new_log_buf_len);
		return;
	}

	raw_spin_lock_irqsave(&logbuf_lock, flags);
	log_buf_len = new_log_buf_len;
	log_buf = new_log_buf;
	new_log_buf_len = 0;
	free = __LOG_BUF_LEN - log_next_idx;
	memcpy(log_buf, __log_buf, __LOG_BUF_LEN);
	raw_spin_unlock_irqrestore(&logbuf_lock, flags);

	pr_info("log_buf_len: %d bytes\n", log_buf_len);
	pr_info("early log buf free: %d(%d%%)\n",
		free, (free * 100) / __LOG_BUF_LEN);
}

static bool __read_mostly ignore_loglevel;

static int __init ignore_loglevel_setup(char *str)
{
	ignore_loglevel = 1;
	pr_info("debug: ignoring loglevel setting.\n");

	return 0;
}

early_param("ignore_loglevel", ignore_loglevel_setup);
module_param(ignore_loglevel, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ignore_loglevel, "ignore loglevel setting, to"
	"print all kernel messages to the console.");

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

	if ((boot_delay == 0 || system_state != SYSTEM_BOOTING)
		|| (level >= console_loglevel && !ignore_loglevel)) {
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

#if defined(CONFIG_PRINTK_TIME)
static bool printk_time = 1;
#else
static bool printk_time;
#endif
module_param_named(time, printk_time, bool, S_IRUGO | S_IWUSR);

static size_t print_time(u64 ts, char *buf)
{
	unsigned long rem_nsec;

	if (!printk_time)
		return 0;

	rem_nsec = do_div(ts, 1000000000);

	if (!buf)
		return snprintf(NULL, 0, "[%5lu.000000] ", (unsigned long)ts);

	return sprintf(buf, "[%5lu.%06lu] ",
		       (unsigned long)ts, rem_nsec / 1000);
}

static size_t print_prefix(const struct printk_log *msg, bool syslog, char *buf)
{
	size_t len = 0;
	unsigned int prefix = (msg->facility << 3) | msg->level;

	if (syslog) {
		if (buf) {
			len += sprintf(buf, "<%u>", prefix);
		} else {
			len += 3;
			if (prefix > 999)
				len += 3;
			else if (prefix > 99)
				len += 2;
			else if (prefix > 9)
				len++;
		}
	}

	len += print_time(msg->ts_nsec, buf ? buf + len : NULL);
	return len;
}

static size_t msg_print_text(const struct printk_log *msg, enum log_flags prev,
			     bool syslog, char *buf, size_t size)
{
	const char *text = log_text(msg);
	size_t text_size = msg->text_len;
	bool prefix = true;
	bool newline = true;
	size_t len = 0;

	if ((prev & LOG_CONT) && !(msg->flags & LOG_PREFIX))
		prefix = false;

	if (msg->flags & LOG_CONT) {
		if ((prev & LOG_CONT) && !(prev & LOG_NEWLINE))
			prefix = false;

		if (!(msg->flags & LOG_NEWLINE))
			newline = false;
	}

	do {
		const char *next = memchr(text, '\n', text_size);
		size_t text_len;

		if (next) {
			text_len = next - text;
			next++;
			text_size -= next - text;
		} else {
			text_len = text_size;
		}

		if (buf) {
			if (print_prefix(msg, syslog, NULL) +
			    text_len + 1 >= size - len)
				break;

			if (prefix)
				len += print_prefix(msg, syslog, buf + len);
			memcpy(buf + len, text, text_len);
			len += text_len;
			if (next || newline)
				buf[len++] = '\n';
		} else {
			/* SYSLOG_ACTION_* buffer size only calculation */
			if (prefix)
				len += print_prefix(msg, syslog, NULL);
			len += text_len;
			if (next || newline)
				len++;
		}

		prefix = true;
		text = next;
	} while (text);

	return len;
}

static int syslog_print(char __user *buf, int size)
{
	char *text;
	struct printk_log *msg;
	int len = 0;

	text = kmalloc(LOG_LINE_MAX + PREFIX_MAX, GFP_KERNEL);
	if (!text)
		return -ENOMEM;

	while (size > 0) {
		size_t n;
		size_t skip;

		raw_spin_lock_irq(&logbuf_lock);
		if (syslog_seq < log_first_seq) {
			/* messages are gone, move to first one */
			syslog_seq = log_first_seq;
			syslog_idx = log_first_idx;
			syslog_prev = 0;
			syslog_partial = 0;
		}
		if (syslog_seq == log_next_seq) {
			raw_spin_unlock_irq(&logbuf_lock);
			break;
		}

		skip = syslog_partial;
		msg = log_from_idx(syslog_idx);
		n = msg_print_text(msg, syslog_prev, true, text,
				   LOG_LINE_MAX + PREFIX_MAX);
		if (n - syslog_partial <= size) {
			/* message fits into buffer, move forward */
			syslog_idx = log_next(syslog_idx);
			syslog_seq++;
			syslog_prev = msg->flags;
			n -= syslog_partial;
			syslog_partial = 0;
		} else if (!len){
			/* partial read(), remember position */
			n = size;
			syslog_partial += n;
		} else
			n = 0;
		raw_spin_unlock_irq(&logbuf_lock);

		if (!n)
			break;

		if (copy_to_user(buf, text + skip, n)) {
			if (!len)
				len = -EFAULT;
			break;
		}

		len += n;
		size -= n;
		buf += n;
	}

	kfree(text);
	return len;
}

static int syslog_print_all(char __user *buf, int size, bool clear)
{
	char *text;
	int len = 0;

	text = kmalloc(LOG_LINE_MAX + PREFIX_MAX, GFP_KERNEL);
	if (!text)
		return -ENOMEM;

	raw_spin_lock_irq(&logbuf_lock);
	if (buf) {
		u64 next_seq;
		u64 seq;
		u32 idx;
		enum log_flags prev;

		if (clear_seq < log_first_seq) {
			/* messages are gone, move to first available one */
			clear_seq = log_first_seq;
			clear_idx = log_first_idx;
		}

		/*
		 * Find first record that fits, including all following records,
		 * into the user-provided buffer for this dump.
		 */
		seq = clear_seq;
		idx = clear_idx;
		prev = 0;
		while (seq < log_next_seq) {
			struct printk_log *msg = log_from_idx(idx);

			len += msg_print_text(msg, prev, true, NULL, 0);
			prev = msg->flags;
			idx = log_next(idx);
			seq++;
		}

		/* move first record forward until length fits into the buffer */
		seq = clear_seq;
		idx = clear_idx;
		prev = 0;
		while (len > size && seq < log_next_seq) {
			struct printk_log *msg = log_from_idx(idx);

			len -= msg_print_text(msg, prev, true, NULL, 0);
			prev = msg->flags;
			idx = log_next(idx);
			seq++;
		}

		/* last message fitting into this dump */
		next_seq = log_next_seq;

		len = 0;
		while (len >= 0 && seq < next_seq) {
			struct printk_log *msg = log_from_idx(idx);
			int textlen;

			textlen = msg_print_text(msg, prev, true, text,
						 LOG_LINE_MAX + PREFIX_MAX);
			if (textlen < 0) {
				len = textlen;
				break;
			}
			idx = log_next(idx);
			seq++;
			prev = msg->flags;

			raw_spin_unlock_irq(&logbuf_lock);
			if (copy_to_user(buf + len, text, textlen))
				len = -EFAULT;
			else
				len += textlen;
			raw_spin_lock_irq(&logbuf_lock);

			if (seq < log_first_seq) {
				/* messages are gone, move to next one */
				seq = log_first_seq;
				idx = log_first_idx;
				prev = 0;
			}
		}
	}

	if (clear) {
		clear_seq = log_next_seq;
		clear_idx = log_next_idx;
	}
	raw_spin_unlock_irq(&logbuf_lock);

	kfree(text);
	return len;
}

int do_syslog(int type, char __user *buf, int len, bool from_file)
{
	bool clear = false;
	static int saved_console_loglevel = -1;
	int error;

	error = check_syslog_permissions(type, from_file);
	if (error)
		goto out;

	error = security_syslog(type);
	if (error)
		return error;

	switch (type) {
	case SYSLOG_ACTION_CLOSE:	/* Close log */
		break;
	case SYSLOG_ACTION_OPEN:	/* Open log */
		break;
	case SYSLOG_ACTION_READ:	/* Read from log */
		error = -EINVAL;
		if (!buf || len < 0)
			goto out;
		error = 0;
		if (!len)
			goto out;
		if (!access_ok(VERIFY_WRITE, buf, len)) {
			error = -EFAULT;
			goto out;
		}
		error = wait_event_interruptible(log_wait,
						 syslog_seq != log_next_seq);
		if (error)
			goto out;
		error = syslog_print(buf, len);
		break;
	/* Read/clear last kernel messages */
	case SYSLOG_ACTION_READ_CLEAR:
		clear = true;
		/* FALL THRU */
	/* Read last kernel messages */
	case SYSLOG_ACTION_READ_ALL:
		error = -EINVAL;
		if (!buf || len < 0)
			goto out;
		error = 0;
		if (!len)
			goto out;
		if (!access_ok(VERIFY_WRITE, buf, len)) {
			error = -EFAULT;
			goto out;
		}
		error = syslog_print_all(buf, len, clear);
		break;
	/* Clear ring buffer */
	case SYSLOG_ACTION_CLEAR:
		syslog_print_all(NULL, 0, true);
		break;
	/* Disable logging to console */
	case SYSLOG_ACTION_CONSOLE_OFF:
		if (saved_console_loglevel == -1)
			saved_console_loglevel = console_loglevel;
		console_loglevel = minimum_console_loglevel;
		break;
	/* Enable logging to console */
	case SYSLOG_ACTION_CONSOLE_ON:
		if (saved_console_loglevel != -1) {
			console_loglevel = saved_console_loglevel;
			saved_console_loglevel = -1;
		}
		break;
	/* Set level of messages printed to console */
	case SYSLOG_ACTION_CONSOLE_LEVEL:
		error = -EINVAL;
		if (len < 1 || len > 8)
			goto out;
		if (len < minimum_console_loglevel)
			len = minimum_console_loglevel;
		console_loglevel = len;
		/* Implicitly re-enable logging to console */
		saved_console_loglevel = -1;
		error = 0;
		break;
	/* Number of chars in the log buffer */
	case SYSLOG_ACTION_SIZE_UNREAD:
		raw_spin_lock_irq(&logbuf_lock);
		if (syslog_seq < log_first_seq) {
			/* messages are gone, move to first one */
			syslog_seq = log_first_seq;
			syslog_idx = log_first_idx;
			syslog_prev = 0;
			syslog_partial = 0;
		}
		if (from_file) {
			/*
			 * Short-cut for poll(/"proc/kmsg") which simply checks
			 * for pending data, not the size; return the count of
			 * records, not the length.
			 */
			error = log_next_idx - syslog_idx;
		} else {
			u64 seq = syslog_seq;
			u32 idx = syslog_idx;
			enum log_flags prev = syslog_prev;

			error = 0;
			while (seq < log_next_seq) {
				struct printk_log *msg = log_from_idx(idx);

				error += msg_print_text(msg, prev, true, NULL, 0);
				idx = log_next(idx);
				seq++;
				prev = msg->flags;
			}
			error -= syslog_partial;
		}
		raw_spin_unlock_irq(&logbuf_lock);
		break;
	/* Size of the log buffer */
	case SYSLOG_ACTION_SIZE_BUFFER:
		error = log_buf_len;
		break;
	default:
		error = -EINVAL;
		break;
	}
out:
	return error;
}

SYSCALL_DEFINE3(syslog, int, type, char __user *, buf, int, len)
{
	return do_syslog(type, buf, len, SYSLOG_FROM_READER);
}

/*
 * Call the console drivers, asking them to write out
 * log_buf[start] to log_buf[end - 1].
 * The console_lock must be held.
 */
static void call_console_drivers(int level, const char *text, size_t len)
{
	struct console *con;

	trace_console(text, len);

	if (level >= console_loglevel && !ignore_loglevel)
		return;
	if (!console_drivers)
		return;

	for_each_console(con) {
		if (exclusive_console && con != exclusive_console)
			continue;
		if (!(con->flags & CON_ENABLED))
			continue;
		if (!con->write)
			continue;
		if (!cpu_online(smp_processor_id()) &&
		    !(con->flags & CON_ANYTIME))
			continue;
		con->write(con, text, len);
	}
}

/*
 * Zap console related locks when oopsing. Only zap at most once
 * every 10 seconds, to leave time for slow consoles to print a
 * full oops.
 */
static void zap_locks(void)
{
	static unsigned long oops_timestamp;

	if (time_after_eq(jiffies, oops_timestamp) &&
			!time_after(jiffies, oops_timestamp + 30 * HZ))
		return;

	oops_timestamp = jiffies;

	debug_locks_off();
	/* If a crash is occurring, make sure we can't deadlock */
	raw_spin_lock_init(&logbuf_lock);
	/* And make sure that we print immediately */
	sema_init(&console_sem, 1);
}

/*
 * Check if we have any console that is capable of printing while cpu is
 * booting or shutting down. Requires console_sem.
 */
static int have_callable_console(void)
{
	struct console *con;

	for_each_console(con)
		if (con->flags & CON_ANYTIME)
			return 1;

	return 0;
}

/*
 * Can we actually use the console at this time on this cpu?
 *
 * Console drivers may assume that per-cpu resources have
 * been allocated. So unless they're explicitly marked as
 * being able to cope (CON_ANYTIME) don't call them until
 * this CPU is officially up.
 */
static inline int can_use_console(unsigned int cpu)
{
	return cpu_online(cpu) || have_callable_console();
}

/*
 * Try to get console ownership to actually show the kernel
 * messages from a 'printk'. Return true (and with the
 * console_lock held, and 'console_locked' set) if it
 * is successful, false otherwise.
 */
static int console_trylock_for_printk(unsigned int cpu)
{
	if (!console_trylock())
		return 0;
	/*
	 * If we can't use the console, we need to release the console
	 * semaphore by hand to avoid flushing the buffer. We need to hold the
	 * console semaphore in order to do this test safely.
	 */
	if (!can_use_console(cpu)) {
		console_locked = 0;
		up_console_sem();
		return 0;
	}
	return 1;
}

int printk_delay_msec __read_mostly;

static inline void printk_delay(void)
{
	if (unlikely(printk_delay_msec)) {
		int m = printk_delay_msec;

		while (m--) {
			mdelay(1);
			touch_nmi_watchdog();
		}
	}
}

/*
 * Continuation lines are buffered, and not committed to the record buffer
 * until the line is complete, or a race forces it. The line fragments
 * though, are printed immediately to the consoles to ensure everything has
 * reached the console in case of a kernel crash.
 */
static struct cont {
	char buf[LOG_LINE_MAX];
	size_t len;			/* length == 0 means unused buffer */
	size_t cons;			/* bytes written to console */
	struct task_struct *owner;	/* task of first print*/
	u64 ts_nsec;			/* time of first print */
	u8 level;			/* log level of first message */
	u8 facility;			/* log level of first message */
	enum log_flags flags;		/* prefix, newline flags */
	bool flushed:1;			/* buffer sealed and committed */
} cont;

static void cont_flush(enum log_flags flags)
{
	if (cont.flushed)
		return;
	if (cont.len == 0)
		return;

	if (cont.cons) {
		/*
		 * If a fragment of this line was directly flushed to the
		 * console; wait for the console to pick up the rest of the
		 * line. LOG_NOCONS suppresses a duplicated output.
		 */
		log_store(cont.facility, cont.level, flags | LOG_NOCONS,
			  cont.ts_nsec, NULL, 0, cont.buf, cont.len);
		cont.flags = flags;
		cont.flushed = true;
	} else {
		/*
		 * If no fragment of this line ever reached the console,
		 * just submit it to the store and free the buffer.
		 */
		log_store(cont.facility, cont.level, flags, 0,
			  NULL, 0, cont.buf, cont.len);
		cont.len = 0;
	}
}

static bool cont_add(int facility, int level, const char *text, size_t len)
{
	if (cont.len && cont.flushed)
		return false;

	if (cont.len + len > sizeof(cont.buf)) {
		/* the line gets too long, split it up in separate records */
		cont_flush(LOG_CONT);
		return false;
	}

	if (!cont.len) {
		cont.facility = facility;
		cont.level = level;
		cont.owner = current;
		cont.ts_nsec = local_clock();
		cont.flags = 0;
		cont.cons = 0;
		cont.flushed = false;
	}

	memcpy(cont.buf + cont.len, text, len);
	cont.len += len;

	if (cont.len > (sizeof(cont.buf) * 80) / 100)
		cont_flush(LOG_CONT);

	return true;
}

static size_t cont_print_text(char *text, size_t size)
{
	size_t textlen = 0;
	size_t len;

	if (cont.cons == 0 && (console_prev & LOG_NEWLINE)) {
		textlen += print_time(cont.ts_nsec, text);
		size -= textlen;
	}

	len = cont.len - cont.cons;
	if (len > 0) {
		if (len+1 > size)
			len = size-1;
		memcpy(text + textlen, cont.buf + cont.cons, len);
		textlen += len;
		cont.cons = cont.len;
	}

	if (cont.flushed) {
		if (cont.flags & LOG_NEWLINE)
			text[textlen++] = '\n';
		/* got everything, release buffer */
		cont.len = 0;
	}
	return textlen;
}

asmlinkage int vprintk_emit(int facility, int level,
			    const char *dict, size_t dictlen,
			    const char *fmt, va_list args)
{
	static int recursion_bug;
	static char textbuf[LOG_LINE_MAX];
	char *text = textbuf;
	size_t text_len = 0;
	enum log_flags lflags = 0;
	unsigned long flags;
	int this_cpu;
	int printed_len = 0;
	bool in_sched = false;
	/* cpu currently holding logbuf_lock in this function */
	static volatile unsigned int logbuf_cpu = UINT_MAX;

	if (level == SCHED_MESSAGE_LOGLEVEL) {
		level = -1;
		in_sched = true;
	}

	boot_delay_msec(level);
	printk_delay();

	/* This stops the holder of console_sem just where we want him */
	local_irq_save(flags);
	this_cpu = smp_processor_id();

	/*
	 * Ouch, printk recursed into itself!
	 */
	if (unlikely(logbuf_cpu == this_cpu)) {
		/*
		 * If a crash is occurring during printk() on this CPU,
		 * then try to get the crash message out but make sure
		 * we can't deadlock. Otherwise just return to avoid the
		 * recursion and return - but flag the recursion so that
		 * it can be printed at the next appropriate moment:
		 */
		if (!oops_in_progress && !lockdep_recursing(current)) {
			recursion_bug = 1;
			goto out_restore_irqs;
		}
		zap_locks();
	}

	lockdep_off();
	raw_spin_lock(&logbuf_lock);
	logbuf_cpu = this_cpu;

	if (recursion_bug) {
		static const char recursion_msg[] =
			"BUG: recent printk recursion!";

		recursion_bug = 0;
		text_len = strlen(recursion_msg);
		/* emit KERN_CRIT message */
		printed_len += log_store(0, 2, LOG_PREFIX|LOG_NEWLINE, 0,
					 NULL, 0, recursion_msg, text_len);
	}

	/*
	 * The printf needs to come first; we need the syslog
	 * prefix which might be passed-in as a parameter.
	 */
	if (in_sched)
		text_len = scnprintf(text, sizeof(textbuf),
				     KERN_WARNING "[sched_delayed] ");

	text_len += vscnprintf(text + text_len,
			       sizeof(textbuf) - text_len, fmt, args);

	/* mark and strip a trailing newline */
	if (text_len && text[text_len-1] == '\n') {
		text_len--;
		lflags |= LOG_NEWLINE;
	}

	/* strip kernel syslog prefix and extract log level or control flags */
	if (facility == 0) {
		int kern_level = printk_get_level(text);

		if (kern_level) {
			const char *end_of_header = printk_skip_level(text);
			switch (kern_level) {
			case '0' ... '7':
				if (level == -1)
					level = kern_level - '0';
			case 'd':	/* KERN_DEFAULT */
				lflags |= LOG_PREFIX;
			}
			/*
			 * No need to check length here because vscnprintf
			 * put '\0' at the end of the string. Only valid and
			 * newly printed level is detected.
			 */
			text_len -= end_of_header - text;
			text = (char *)end_of_header;
		}
	}

	if (level == -1)
		level = default_message_loglevel;

	if (dict)
		lflags |= LOG_PREFIX|LOG_NEWLINE;

	if (!(lflags & LOG_NEWLINE)) {
		/*
		 * Flush the conflicting buffer. An earlier newline was missing,
		 * or another task also prints continuation lines.
		 */
		if (cont.len && (lflags & LOG_PREFIX || cont.owner != current))
			cont_flush(LOG_NEWLINE);

		/* buffer line if possible, otherwise store it right away */
		if (cont_add(facility, level, text, text_len))
			printed_len += text_len;
		else
			printed_len += log_store(facility, level,
						 lflags | LOG_CONT, 0,
						 dict, dictlen, text, text_len);
	} else {
		bool stored = false;

		/*
		 * If an earlier newline was missing and it was the same task,
		 * either merge it with the current buffer and flush, or if
		 * there was a race with interrupts (prefix == true) then just
		 * flush it out and store this line separately.
		 * If the preceding printk was from a different task and missed
		 * a newline, flush and append the newline.
		 */
		if (cont.len) {
			if (cont.owner == current && !(lflags & LOG_PREFIX))
				stored = cont_add(facility, level, text,
						  text_len);
			cont_flush(LOG_NEWLINE);
		}

		if (stored)
			printed_len += text_len;
		else
			printed_len += log_store(facility, level, lflags, 0,
						 dict, dictlen, text, text_len);
	}

	logbuf_cpu = UINT_MAX;
	raw_spin_unlock(&logbuf_lock);

	/* If called from the scheduler, we can not call up(). */
	if (!in_sched) {
		/*
		 * Try to acquire and then immediately release the console
		 * semaphore.  The release will print out buffers and wake up
		 * /dev/kmsg and syslog() users.
		 */
		if (console_trylock_for_printk(this_cpu))
			console_unlock();
	}

	lockdep_on();
out_restore_irqs:
	local_irq_restore(flags);
	return printed_len;
}
EXPORT_SYMBOL(vprintk_emit);

asmlinkage int vprintk(const char *fmt, va_list args)
{
	return vprintk_emit(0, -1, NULL, 0, fmt, args);
}
EXPORT_SYMBOL(vprintk);

asmlinkage int printk_emit(int facility, int level,
			   const char *dict, size_t dictlen,
			   const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = vprintk_emit(facility, level, dict, dictlen, fmt, args);
	va_end(args);

	return r;
}
EXPORT_SYMBOL(printk_emit);

/**
 * printk - print a kernel message
 * @fmt: format string
 *
 * This is printk(). It can be called from any context. We want it to work.
 *
 * We try to grab the console_lock. If we succeed, it's easy - we log the
 * output and call the console drivers.  If we fail to get the semaphore, we
 * place the output into the log buffer and return. The current holder of
 * the console_sem will notice the new output in console_unlock(); and will
 * send it to the consoles before releasing the lock.
 *
 * One effect of this deferred printing is that code which calls printk() and
 * then changes console_loglevel may break. This is because console_loglevel
 * is inspected when the actual printing occurs.
 *
 * See also:
 * printf(3)
 *
 * See the vsnprintf() documentation for format string extensions over C99.
 */
asmlinkage __visible int printk(const char *fmt, ...)
{
	va_list args;
	int r;

#ifdef CONFIG_KGDB_KDB
	if (unlikely(kdb_trap_printk)) {
		va_start(args, fmt);
		r = vkdb_printf(fmt, args);
		va_end(args);
		return r;
	}
#endif
	va_start(args, fmt);
	r = vprintk_emit(0, -1, NULL, 0, fmt, args);
	va_end(args);

	return r;
}
EXPORT_SYMBOL(printk);

#else /* CONFIG_PRINTK */

#define LOG_LINE_MAX		0
#define PREFIX_MAX		0
#define LOG_LINE_MAX 0
static u64 syslog_seq;
static u32 syslog_idx;
static u64 console_seq;
static u32 console_idx;
static enum log_flags syslog_prev;
static u64 log_first_seq;
static u32 log_first_idx;
static u64 log_next_seq;
static enum log_flags console_prev;
static struct cont {
	size_t len;
	size_t cons;
	u8 level;
	bool flushed:1;
} cont;
static struct printk_log *log_from_idx(u32 idx) { return NULL; }
static u32 log_next(u32 idx) { return 0; }
static void call_console_drivers(int level, const char *text, size_t len) {}
static size_t msg_print_text(const struct printk_log *msg, enum log_flags prev,
			     bool syslog, char *buf, size_t size) { return 0; }
static size_t cont_print_text(char *text, size_t size) { return 0; }

#endif /* CONFIG_PRINTK */

#ifdef CONFIG_EARLY_PRINTK
struct console *early_console;

void early_vprintk(const char *fmt, va_list ap)
{
	if (early_console) {
		char buf[512];
		int n = vscnprintf(buf, sizeof(buf), fmt, ap);

		early_console->write(early_console, buf, n);
	}
}

asmlinkage __visible void early_printk(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	early_vprintk(fmt, ap);
	va_end(ap);
}
#endif

static int __add_preferred_console(char *name, int idx, char *options,
				   char *brl_options)
{
	struct console_cmdline *c;
	int i;

	/*
	 *	See if this tty is not yet registered, and
	 *	if we have a slot free.
	 */
	for (i = 0, c = console_cmdline;
	     i < MAX_CMDLINECONSOLES && c->name[0];
	     i++, c++) {
		if (strcmp(c->name, name) == 0 && c->index == idx) {
			if (!brl_options)
				selected_console = i;
			return 0;
		}
	}
	if (i == MAX_CMDLINECONSOLES)
		return -E2BIG;
	if (!brl_options)
		selected_console = i;
	strlcpy(c->name, name, sizeof(c->name));
	c->options = options;
	braille_set_options(c, brl_options);

	c->index = idx;
	return 0;
}
/*
 * Set up a list of consoles.  Called from init/main.c
 */
static int __init console_setup(char *str)
{
	char buf[sizeof(console_cmdline[0].name) + 4]; /* 4 for index */
	char *s, *options, *brl_options = NULL;
	int idx;

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
	if ((options = strchr(str, ',')) != NULL)
		*(options++) = 0;
#ifdef __sparc__
	if (!strcmp(str, "ttya"))
		strcpy(buf, "ttyS0");
	if (!strcmp(str, "ttyb"))
		strcpy(buf, "ttyS1");
#endif
	for (s = buf; *s; s++)
		if ((*s >= '0' && *s <= '9') || *s == ',')
			break;
	idx = simple_strtoul(s, NULL, 10);
	*s = 0;

	__add_preferred_console(buf, idx, options, brl_options);
	console_set_on_cmdline = 1;
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
int add_preferred_console(char *name, int idx, char *options)
{
	return __add_preferred_console(name, idx, options, NULL);
}

int update_console_cmdline(char *name, int idx, char *name_new, int idx_new, char *options)
{
	struct console_cmdline *c;
	int i;

	for (i = 0, c = console_cmdline;
	     i < MAX_CMDLINECONSOLES && c->name[0];
	     i++, c++)
		if (strcmp(c->name, name) == 0 && c->index == idx) {
			strlcpy(c->name, name_new, sizeof(c->name));
			c->name[sizeof(c->name) - 1] = 0;
			c->options = options;
			c->index = idx_new;
			return i;
		}
	/* not found */
	return -1;
}

bool console_suspend_enabled = 1;
EXPORT_SYMBOL(console_suspend_enabled);

static int __init console_suspend_disable(char *str)
{
	console_suspend_enabled = 0;
	return 1;
}
__setup("no_console_suspend", console_suspend_disable);
module_param_named(console_suspend, console_suspend_enabled,
		bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(console_suspend, "suspend console during suspend"
	" and hibernate operations");

/**
 * suspend_console - suspend the console subsystem
 *
 * This disables printk() while we go into suspend states
 */
void suspend_console(void)
{
	if (!console_suspend_enabled)
		return;
	printk("Suspending console(s) (use no_console_suspend to debug)\n");
	console_lock();
	console_suspended = 1;
	up_console_sem();
}

void resume_console(void)
{
	if (!console_suspend_enabled)
		return;
	down_console_sem();
	console_suspended = 0;
	console_unlock();
}

/**
 * console_cpu_notify - print deferred console messages after CPU hotplug
 * @self: notifier struct
 * @action: CPU hotplug event
 * @hcpu: unused
 *
 * If printk() is called from a CPU that is not online yet, the messages
 * will be spooled but will not show up on the console.  This function is
 * called when a new CPU comes online (or fails to come up), and ensures
 * that any such output gets printed.
 */
static int console_cpu_notify(struct notifier_block *self,
	unsigned long action, void *hcpu)
{
	switch (action) {
	case CPU_ONLINE:
	case CPU_DEAD:
	case CPU_DOWN_FAILED:
	case CPU_UP_CANCELED:
		console_lock();
		console_unlock();
	}
	return NOTIFY_OK;
}

/**
 * console_lock - lock the console system for exclusive use.
 *
 * Acquires a lock which guarantees that the caller has
 * exclusive access to the console system and the console_drivers list.
 *
 * Can sleep, returns nothing.
 */
void console_lock(void)
{
	might_sleep();

	down_console_sem();
	if (console_suspended)
		return;
	console_locked = 1;
	console_may_schedule = 1;
}
EXPORT_SYMBOL(console_lock);

/**
 * console_trylock - try to lock the console system for exclusive use.
 *
 * Tried to acquire a lock which guarantees that the caller has
 * exclusive access to the console system and the console_drivers list.
 *
 * returns 1 on success, and 0 on failure to acquire the lock.
 */
int console_trylock(void)
{
	if (down_trylock_console_sem())
		return 0;
	if (console_suspended) {
		up_console_sem();
		return 0;
	}
	console_locked = 1;
	console_may_schedule = 0;
	return 1;
}
EXPORT_SYMBOL(console_trylock);

int is_console_locked(void)
{
	return console_locked;
}

static void console_cont_flush(char *text, size_t size)
{
	unsigned long flags;
	size_t len;

	raw_spin_lock_irqsave(&logbuf_lock, flags);

	if (!cont.len)
		goto out;

	/*
	 * We still queue earlier records, likely because the console was
	 * busy. The earlier ones need to be printed before this one, we
	 * did not flush any fragment so far, so just let it queue up.
	 */
	if (console_seq < log_next_seq && !cont.cons)
		goto out;

	len = cont_print_text(text, size);
	raw_spin_unlock(&logbuf_lock);
	stop_critical_timings();
	call_console_drivers(cont.level, text, len);
	start_critical_timings();
	local_irq_restore(flags);
	return;
out:
	raw_spin_unlock_irqrestore(&logbuf_lock, flags);
}

/**
 * console_unlock - unlock the console system
 *
 * Releases the console_lock which the caller holds on the console system
 * and the console driver list.
 *
 * While the console_lock was held, console output may have been buffered
 * by printk().  If this is the case, console_unlock(); emits
 * the output prior to releasing the lock.
 *
 * If there is output waiting, we wake /dev/kmsg and syslog() users.
 *
 * console_unlock(); may be called from any context.
 */
void console_unlock(void)
{
	static char text[LOG_LINE_MAX + PREFIX_MAX];
	static u64 seen_seq;
	unsigned long flags;
	bool wake_klogd = false;
	bool retry;

	if (console_suspended) {
		up_console_sem();
		return;
	}

	console_may_schedule = 0;

	/* flush buffered message fragment immediately to console */
	console_cont_flush(text, sizeof(text));
again:
	for (;;) {
		struct printk_log *msg;
		size_t len;
		int level;

		raw_spin_lock_irqsave(&logbuf_lock, flags);
		if (seen_seq != log_next_seq) {
			wake_klogd = true;
			seen_seq = log_next_seq;
		}

		if (console_seq < log_first_seq) {
			len = sprintf(text, "** %u printk messages dropped ** ",
				      (unsigned)(log_first_seq - console_seq));

			/* messages are gone, move to first one */
			console_seq = log_first_seq;
			console_idx = log_first_idx;
			console_prev = 0;
		} else {
			len = 0;
		}
skip:
		if (console_seq == log_next_seq)
			break;

		msg = log_from_idx(console_idx);
		if (msg->flags & LOG_NOCONS) {
			/*
			 * Skip record we have buffered and already printed
			 * directly to the console when we received it.
			 */
			console_idx = log_next(console_idx);
			console_seq++;
			/*
			 * We will get here again when we register a new
			 * CON_PRINTBUFFER console. Clear the flag so we
			 * will properly dump everything later.
			 */
			msg->flags &= ~LOG_NOCONS;
			console_prev = msg->flags;
			goto skip;
		}

		level = msg->level;
		len += msg_print_text(msg, console_prev, false,
				      text + len, sizeof(text) - len);
		console_idx = log_next(console_idx);
		console_seq++;
		console_prev = msg->flags;
		raw_spin_unlock(&logbuf_lock);

		stop_critical_timings();	/* don't trace print latency */
		call_console_drivers(level, text, len);
		start_critical_timings();
		local_irq_restore(flags);
	}
	console_locked = 0;

	/* Release the exclusive_console once it is used */
	if (unlikely(exclusive_console))
		exclusive_console = NULL;

	raw_spin_unlock(&logbuf_lock);

	up_console_sem();

	/*
	 * Someone could have filled up the buffer again, so re-check if there's
	 * something to flush. In case we cannot trylock the console_sem again,
	 * there's a new owner and the console_unlock() from them will do the
	 * flush, no worries.
	 */
	raw_spin_lock(&logbuf_lock);
	retry = console_seq != log_next_seq;
	raw_spin_unlock_irqrestore(&logbuf_lock, flags);

	if (retry && console_trylock())
		goto again;

	if (wake_klogd)
		wake_up_klogd();
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
	struct console *c;

	/*
	 * console_unblank can no longer be called in interrupt context unless
	 * oops_in_progress is set to 1..
	 */
	if (oops_in_progress) {
		if (down_trylock_console_sem() != 0)
			return;
	} else
		console_lock();

	console_locked = 1;
	console_may_schedule = 0;
	for_each_console(c)
		if ((c->flags & CON_ENABLED) && c->unblank)
			c->unblank();
	console_unlock();
}

/*
 * Return the console tty driver structure and its associated index
 */
struct tty_driver *console_device(int *index)
{
	struct console *c;
	struct tty_driver *driver = NULL;

	console_lock();
	for_each_console(c) {
		if (!c->device)
			continue;
		driver = c->device(c, index);
		if (driver)
			break;
	}
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
	console_lock();
	console->flags &= ~CON_ENABLED;
	console_unlock();
}
EXPORT_SYMBOL(console_stop);

void console_start(struct console *console)
{
	console_lock();
	console->flags |= CON_ENABLED;
	console_unlock();
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
	int i;
	unsigned long flags;
	struct console *bcon = NULL;
	struct console_cmdline *c;

	if (console_drivers)
		for_each_console(bcon)
			if (WARN(bcon == newcon,
					"console '%s%d' already registered\n",
					bcon->name, bcon->index))
				return;

	/*
	 * before we register a new CON_BOOT console, make sure we don't
	 * already have a valid console
	 */
	if (console_drivers && newcon->flags & CON_BOOT) {
		/* find the last or real console */
		for_each_console(bcon) {
			if (!(bcon->flags & CON_BOOT)) {
				pr_info("Too late to register bootconsole %s%d\n",
					newcon->name, newcon->index);
				return;
			}
		}
	}

	if (console_drivers && console_drivers->flags & CON_BOOT)
		bcon = console_drivers;

	if (preferred_console < 0 || bcon || !console_drivers)
		preferred_console = selected_console;

	if (newcon->early_setup)
		newcon->early_setup();

	/*
	 *	See if we want to use this console driver. If we
	 *	didn't select a console we take the first one
	 *	that registers here.
	 */
	if (preferred_console < 0) {
		if (newcon->index < 0)
			newcon->index = 0;
		if (newcon->setup == NULL ||
		    newcon->setup(newcon, NULL) == 0) {
			newcon->flags |= CON_ENABLED;
			if (newcon->device) {
				newcon->flags |= CON_CONSDEV;
				preferred_console = 0;
			}
		}
	}

	/*
	 *	See if this console matches one we selected on
	 *	the command line.
	 */
	for (i = 0, c = console_cmdline;
	     i < MAX_CMDLINECONSOLES && c->name[0];
	     i++, c++) {
		if (strcmp(c->name, newcon->name) != 0)
			continue;
		if (newcon->index >= 0 &&
		    newcon->index != c->index)
			continue;
		if (newcon->index < 0)
			newcon->index = c->index;

		if (_braille_register_console(newcon, c))
			return;

		if (newcon->setup &&
		    newcon->setup(newcon, console_cmdline[i].options) != 0)
			break;
		newcon->flags |= CON_ENABLED;
		newcon->index = c->index;
		if (i == selected_console) {
			newcon->flags |= CON_CONSDEV;
			preferred_console = selected_console;
		}
		break;
	}

	if (!(newcon->flags & CON_ENABLED))
		return;

	/*
	 * If we have a bootconsole, and are switching to a real console,
	 * don't print everything out again, since when the boot console, and
	 * the real console are the same physical device, it's annoying to
	 * see the beginning boot messages twice
	 */
	if (bcon && ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV))
		newcon->flags &= ~CON_PRINTBUFFER;

	/*
	 *	Put this console in the list - keep the
	 *	preferred driver at the head of the list.
	 */
	console_lock();
	if ((newcon->flags & CON_CONSDEV) || console_drivers == NULL) {
		newcon->next = console_drivers;
		console_drivers = newcon;
		if (newcon->next)
			newcon->next->flags &= ~CON_CONSDEV;
	} else {
		newcon->next = console_drivers->next;
		console_drivers->next = newcon;
	}
	if (newcon->flags & CON_PRINTBUFFER) {
		/*
		 * console_unlock(); will print out the buffered messages
		 * for us.
		 */
		raw_spin_lock_irqsave(&logbuf_lock, flags);
		console_seq = syslog_seq;
		console_idx = syslog_idx;
		console_prev = syslog_prev;
		raw_spin_unlock_irqrestore(&logbuf_lock, flags);
		/*
		 * We're about to replay the log buffer.  Only do this to the
		 * just-registered console to avoid excessive message spam to
		 * the already-registered consoles.
		 */
		exclusive_console = newcon;
	}
	console_unlock();
	console_sysfs_notify();

	/*
	 * By unregistering the bootconsoles after we enable the real console
	 * we get the "console xxx enabled" message on all the consoles -
	 * boot consoles, real consoles, etc - this is to ensure that end
	 * users know there might be something in the kernel's log buffer that
	 * went to the bootconsole (that they do not see on the real console)
	 */
	pr_info("%sconsole [%s%d] enabled\n",
		(newcon->flags & CON_BOOT) ? "boot" : "" ,
		newcon->name, newcon->index);
	if (bcon &&
	    ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV) &&
	    !keep_bootcon) {
		/* We need to iterate through all boot consoles, to make
		 * sure we print everything out, before we unregister them.
		 */
		for_each_console(bcon)
			if (bcon->flags & CON_BOOT)
				unregister_console(bcon);
	}
}
EXPORT_SYMBOL(register_console);

int unregister_console(struct console *console)
{
        struct console *a, *b;
	int res;

	pr_info("%sconsole [%s%d] disabled\n",
		(console->flags & CON_BOOT) ? "boot" : "" ,
		console->name, console->index);

	res = _braille_unregister_console(console);
	if (res)
		return res;

	res = 1;
	console_lock();
	if (console_drivers == console) {
		console_drivers=console->next;
		res = 0;
	} else if (console_drivers) {
		for (a=console_drivers->next, b=console_drivers ;
		     a; b=a, a=b->next) {
			if (a == console) {
				b->next = a->next;
				res = 0;
				break;
			}
		}
	}

	/*
	 * If this isn't the last console and it has CON_CONSDEV set, we
	 * need to set it on the next preferred console.
	 */
	if (console_drivers != NULL && console->flags & CON_CONSDEV)
		console_drivers->flags |= CON_CONSDEV;

	console->flags &= ~CON_ENABLED;
	console_unlock();
	console_sysfs_notify();
	return res;
}
EXPORT_SYMBOL(unregister_console);

static int __init printk_late_init(void)
{
	struct console *con;

	for_each_console(con) {
		if (!keep_bootcon && con->flags & CON_BOOT) {
			unregister_console(con);
		}
	}
	hotcpu_notifier(console_cpu_notify, 0);
	return 0;
}
late_initcall(printk_late_init);

#if defined CONFIG_PRINTK
/*
 * Delayed printk version, for scheduler-internal messages:
 */
#define PRINTK_PENDING_WAKEUP	0x01
#define PRINTK_PENDING_OUTPUT	0x02

static DEFINE_PER_CPU(int, printk_pending);

static void wake_up_klogd_work_func(struct irq_work *irq_work)
{
	int pending = __this_cpu_xchg(printk_pending, 0);

	if (pending & PRINTK_PENDING_OUTPUT) {
		/* If trylock fails, someone else is doing the printing */
		if (console_trylock())
			console_unlock();
	}

	if (pending & PRINTK_PENDING_WAKEUP)
		wake_up_interruptible(&log_wait);
}

static DEFINE_PER_CPU(struct irq_work, wake_up_klogd_work) = {
	.func = wake_up_klogd_work_func,
	.flags = IRQ_WORK_LAZY,
};

void wake_up_klogd(void)
{
	preempt_disable();
	if (waitqueue_active(&log_wait)) {
		this_cpu_or(printk_pending, PRINTK_PENDING_WAKEUP);
		irq_work_queue(&__get_cpu_var(wake_up_klogd_work));
	}
	preempt_enable();
}

int printk_deferred(const char *fmt, ...)
{
	va_list args;
	int r;

	preempt_disable();
	va_start(args, fmt);
	r = vprintk_emit(0, SCHED_MESSAGE_LOGLEVEL, NULL, 0, fmt, args);
	va_end(args);

	__this_cpu_or(printk_pending, PRINTK_PENDING_OUTPUT);
	irq_work_queue(&__get_cpu_var(wake_up_klogd_work));
	preempt_enable();

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
	if (*caller_jiffies == 0
			|| !time_in_range(jiffies, *caller_jiffies,
					*caller_jiffies
					+ msecs_to_jiffies(interval_msecs))) {
		*caller_jiffies = jiffies;
		return true;
	}
	return false;
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
	unsigned long flags;

	if ((reason > KMSG_DUMP_OOPS) && !always_kmsg_dump)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(dumper, &dump_list, list) {
		if (dumper->max_reason && reason > dumper->max_reason)
			continue;

		/* initialize iterator with data about the stored records */
		dumper->active = true;

		raw_spin_lock_irqsave(&logbuf_lock, flags);
		dumper->cur_seq = clear_seq;
		dumper->cur_idx = clear_idx;
		dumper->next_seq = log_next_seq;
		dumper->next_idx = log_next_idx;
		raw_spin_unlock_irqrestore(&logbuf_lock, flags);

		/* invoke dumper which will iterate over records */
		dumper->dump(dumper, reason);

		/* reset iterator */
		dumper->active = false;
	}
	rcu_read_unlock();
}

/**
 * kmsg_dump_get_line_nolock - retrieve one kmsg log line (unlocked version)
 * @dumper: registered kmsg dumper
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
 *
 * The function is similar to kmsg_dump_get_line(), but grabs no locks.
 */
bool kmsg_dump_get_line_nolock(struct kmsg_dumper *dumper, bool syslog,
			       char *line, size_t size, size_t *len)
{
	struct printk_log *msg;
	size_t l = 0;
	bool ret = false;

	if (!dumper->active)
		goto out;

	if (dumper->cur_seq < log_first_seq) {
		/* messages are gone, move to first available one */
		dumper->cur_seq = log_first_seq;
		dumper->cur_idx = log_first_idx;
	}

	/* last entry */
	if (dumper->cur_seq >= log_next_seq)
		goto out;

	msg = log_from_idx(dumper->cur_idx);
	l = msg_print_text(msg, 0, syslog, line, size);

	dumper->cur_idx = log_next(dumper->cur_idx);
	dumper->cur_seq++;
	ret = true;
out:
	if (len)
		*len = l;
	return ret;
}

/**
 * kmsg_dump_get_line - retrieve one kmsg log line
 * @dumper: registered kmsg dumper
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
bool kmsg_dump_get_line(struct kmsg_dumper *dumper, bool syslog,
			char *line, size_t size, size_t *len)
{
	unsigned long flags;
	bool ret;

	raw_spin_lock_irqsave(&logbuf_lock, flags);
	ret = kmsg_dump_get_line_nolock(dumper, syslog, line, size, len);
	raw_spin_unlock_irqrestore(&logbuf_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(kmsg_dump_get_line);

/**
 * kmsg_dump_get_buffer - copy kmsg log lines
 * @dumper: registered kmsg dumper
 * @syslog: include the "<4>" prefixes
 * @buf: buffer to copy the line to
 * @size: maximum size of the buffer
 * @len: length of line placed into buffer
 *
 * Start at the end of the kmsg buffer and fill the provided buffer
 * with as many of the the *youngest* kmsg records that fit into it.
 * If the buffer is large enough, all available kmsg records will be
 * copied with a single call.
 *
 * Consecutive calls will fill the buffer with the next block of
 * available older records, not including the earlier retrieved ones.
 *
 * A return value of FALSE indicates that there are no more records to
 * read.
 */
bool kmsg_dump_get_buffer(struct kmsg_dumper *dumper, bool syslog,
			  char *buf, size_t size, size_t *len)
{
	unsigned long flags;
	u64 seq;
	u32 idx;
	u64 next_seq;
	u32 next_idx;
	enum log_flags prev;
	size_t l = 0;
	bool ret = false;

	if (!dumper->active)
		goto out;

	raw_spin_lock_irqsave(&logbuf_lock, flags);
	if (dumper->cur_seq < log_first_seq) {
		/* messages are gone, move to first available one */
		dumper->cur_seq = log_first_seq;
		dumper->cur_idx = log_first_idx;
	}

	/* last entry */
	if (dumper->cur_seq >= dumper->next_seq) {
		raw_spin_unlock_irqrestore(&logbuf_lock, flags);
		goto out;
	}

	/* calculate length of entire buffer */
	seq = dumper->cur_seq;
	idx = dumper->cur_idx;
	prev = 0;
	while (seq < dumper->next_seq) {
		struct printk_log *msg = log_from_idx(idx);

		l += msg_print_text(msg, prev, true, NULL, 0);
		idx = log_next(idx);
		seq++;
		prev = msg->flags;
	}

	/* move first record forward until length fits into the buffer */
	seq = dumper->cur_seq;
	idx = dumper->cur_idx;
	prev = 0;
	while (l > size && seq < dumper->next_seq) {
		struct printk_log *msg = log_from_idx(idx);

		l -= msg_print_text(msg, prev, true, NULL, 0);
		idx = log_next(idx);
		seq++;
		prev = msg->flags;
	}

	/* last message in next interation */
	next_seq = seq;
	next_idx = idx;

	l = 0;
	while (seq < dumper->next_seq) {
		struct printk_log *msg = log_from_idx(idx);

		l += msg_print_text(msg, prev, syslog, buf + l, size - l);
		idx = log_next(idx);
		seq++;
		prev = msg->flags;
	}

	dumper->next_seq = next_seq;
	dumper->next_idx = next_idx;
	ret = true;
	raw_spin_unlock_irqrestore(&logbuf_lock, flags);
out:
	if (len)
		*len = l;
	return ret;
}
EXPORT_SYMBOL_GPL(kmsg_dump_get_buffer);

/**
 * kmsg_dump_rewind_nolock - reset the interator (unlocked version)
 * @dumper: registered kmsg dumper
 *
 * Reset the dumper's iterator so that kmsg_dump_get_line() and
 * kmsg_dump_get_buffer() can be called again and used multiple
 * times within the same dumper.dump() callback.
 *
 * The function is similar to kmsg_dump_rewind(), but grabs no locks.
 */
void kmsg_dump_rewind_nolock(struct kmsg_dumper *dumper)
{
	dumper->cur_seq = clear_seq;
	dumper->cur_idx = clear_idx;
	dumper->next_seq = log_next_seq;
	dumper->next_idx = log_next_idx;
}

/**
 * kmsg_dump_rewind - reset the interator
 * @dumper: registered kmsg dumper
 *
 * Reset the dumper's iterator so that kmsg_dump_get_line() and
 * kmsg_dump_get_buffer() can be called again and used multiple
 * times within the same dumper.dump() callback.
 */
void kmsg_dump_rewind(struct kmsg_dumper *dumper)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&logbuf_lock, flags);
	kmsg_dump_rewind_nolock(dumper);
	raw_spin_unlock_irqrestore(&logbuf_lock, flags);
}
EXPORT_SYMBOL_GPL(kmsg_dump_rewind);

static char dump_stack_arch_desc_str[128];

/**
 * dump_stack_set_arch_desc - set arch-specific str to show with task dumps
 * @fmt: printf-style format string
 * @...: arguments for the format string
 *
 * The configured string will be printed right after utsname during task
 * dumps.  Usually used to add arch-specific system identifiers.  If an
 * arch wants to make use of such an ID string, it should initialize this
 * as soon as possible during boot.
 */
void __init dump_stack_set_arch_desc(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(dump_stack_arch_desc_str, sizeof(dump_stack_arch_desc_str),
		  fmt, args);
	va_end(args);
}

/**
 * dump_stack_print_info - print generic debug info for dump_stack()
 * @log_lvl: log level
 *
 * Arch-specific dump_stack() implementations can use this function to
 * print out the same debug information as the generic dump_stack().
 */
void dump_stack_print_info(const char *log_lvl)
{
	printk("%sCPU: %d PID: %d Comm: %.20s %s %s %.*s\n",
	       log_lvl, raw_smp_processor_id(), current->pid, current->comm,
	       print_tainted(), init_utsname()->release,
	       (int)strcspn(init_utsname()->version, " "),
	       init_utsname()->version);

	if (dump_stack_arch_desc_str[0] != '\0')
		printk("%sHardware name: %s\n",
		       log_lvl, dump_stack_arch_desc_str);

	print_worker_info(log_lvl, current);
}

/**
 * show_regs_print_info - print generic debug info for show_regs()
 * @log_lvl: log level
 *
 * show_regs() implementations can use this function to print out generic
 * debug information.
 */
void show_regs_print_info(const char *log_lvl)
{
	dump_stack_print_info(log_lvl);

	printk("%stask: %p ti: %p task.ti: %p\n",
	       log_lvl, current, current_thread_info(),
	       task_thread_info(current));
}

#endif
