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

#include <asm/uaccess.h>

#define CREATE_TRACE_POINTS
#include <trace/events/printk.h>

/*
 * Architectures can override it:
 */
void asmlinkage __attribute__((weak)) early_printk(const char *fmt, ...)
{
}

/* printk's without a loglevel use this.. */
#define DEFAULT_MESSAGE_LOGLEVEL CONFIG_DEFAULT_MESSAGE_LOGLEVEL

/* We show everything that is MORE important than this.. */
#define MINIMUM_CONSOLE_LOGLEVEL 1 /* Minimum loglevel we let people use */
#define DEFAULT_CONSOLE_LOGLEVEL 7 /* anything MORE serious than KERN_DEBUG */

DECLARE_WAIT_QUEUE_HEAD(log_wait);

int console_printk[4] = {
	DEFAULT_CONSOLE_LOGLEVEL,	/* console_loglevel */
	DEFAULT_MESSAGE_LOGLEVEL,	/* default_message_loglevel */
	MINIMUM_CONSOLE_LOGLEVEL,	/* minimum_console_loglevel */
	DEFAULT_CONSOLE_LOGLEVEL,	/* default_console_loglevel */
};

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
struct console_cmdline
{
	char	name[8];			/* Name of the driver	    */
	int	index;				/* Minor dev. to use	    */
	char	*options;			/* Options for the driver   */
#ifdef CONFIG_A11Y_BRAILLE_CONSOLE
	char	*brl_options;			/* Options for braille driver */
#endif
};

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
 * The 'struct log' buffer header must never be directly exported to
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

struct log {
	u64 ts_nsec;		/* timestamp in nanoseconds */
	u16 len;		/* length of entire record */
	u16 text_len;		/* length of text buffer */
	u16 dict_len;		/* length of dictionary buffer */
	u16 level;		/* syslog level + facility */
};

/*
 * The logbuf_lock protects kmsg buffer, indices, counters. It is also
 * used in interesting ways to provide interlocking in console_unlock();
 */
static DEFINE_RAW_SPINLOCK(logbuf_lock);

/* the next printk record to read by syslog(READ) or /proc/kmsg */
static u64 syslog_seq;
static u32 syslog_idx;

/* index and sequence number of the first record stored in the buffer */
static u64 log_first_seq;
static u32 log_first_idx;

/* index and sequence number of the next record to store in the buffer */
static u64 log_next_seq;
#ifdef CONFIG_PRINTK
static u32 log_next_idx;

/* the next printk record to read after the last 'clear' command */
static u64 clear_seq;
static u32 clear_idx;

#define LOG_LINE_MAX 1024

/* record buffer */
#if !defined(CONFIG_64BIT) || defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
#define LOG_ALIGN 4
#else
#define LOG_ALIGN 8
#endif
#define __LOG_BUF_LEN (1 << CONFIG_LOG_BUF_SHIFT)
static char __log_buf[__LOG_BUF_LEN] __aligned(LOG_ALIGN);
static char *log_buf = __log_buf;
static u32 log_buf_len = __LOG_BUF_LEN;

/* cpu currently holding logbuf_lock */
static volatile unsigned int logbuf_cpu = UINT_MAX;

/* human readable text of the record */
static char *log_text(const struct log *msg)
{
	return (char *)msg + sizeof(struct log);
}

/* optional key/value pair dictionary attached to the record */
static char *log_dict(const struct log *msg)
{
	return (char *)msg + sizeof(struct log) + msg->text_len;
}

/* get record by index; idx must point to valid msg */
static struct log *log_from_idx(u32 idx)
{
	struct log *msg = (struct log *)(log_buf + idx);

	/*
	 * A length == 0 record is the end of buffer marker. Wrap around and
	 * read the message at the start of the buffer.
	 */
	if (!msg->len)
		return (struct log *)log_buf;
	return msg;
}

/* get next record; idx must point to valid msg */
static u32 log_next(u32 idx)
{
	struct log *msg = (struct log *)(log_buf + idx);

	/* length == 0 indicates the end of the buffer; wrap */
	/*
	 * A length == 0 record is the end of buffer marker. Wrap around and
	 * read the message at the start of the buffer as *this* one, and
	 * return the one after that.
	 */
	if (!msg->len) {
		msg = (struct log *)log_buf;
		return msg->len;
	}
	return idx + msg->len;
}

/* insert record into the buffer, discard old ones, update heads */
static void log_store(int facility, int level,
		      const char *dict, u16 dict_len,
		      const char *text, u16 text_len)
{
	struct log *msg;
	u32 size, pad_len;

	/* number of '\0' padding bytes to next message */
	size = sizeof(struct log) + text_len + dict_len;
	pad_len = (-size) & (LOG_ALIGN - 1);
	size += pad_len;

	while (log_first_seq < log_next_seq) {
		u32 free;

		if (log_next_idx > log_first_idx)
			free = max(log_buf_len - log_next_idx, log_first_idx);
		else
			free = log_first_idx - log_next_idx;

		if (free > size + sizeof(struct log))
			break;

		/* drop old messages until we have enough contiuous space */
		log_first_idx = log_next(log_first_idx);
		log_first_seq++;
	}

	if (log_next_idx + size + sizeof(struct log) >= log_buf_len) {
		/*
		 * This message + an additional empty header does not fit
		 * at the end of the buffer. Add an empty header with len == 0
		 * to signify a wrap around.
		 */
		memset(log_buf + log_next_idx, 0, sizeof(struct log));
		log_next_idx = 0;
	}

	/* fill message */
	msg = (struct log *)(log_buf + log_next_idx);
	memcpy(log_text(msg), text, text_len);
	msg->text_len = text_len;
	memcpy(log_dict(msg), dict, dict_len);
	msg->dict_len = dict_len;
	msg->level = (facility << 3) | (level & 7);
	msg->ts_nsec = local_clock();
	memset(log_dict(msg) + dict_len, 0, pad_len);
	msg->len = sizeof(struct log) + text_len + dict_len + pad_len;

	/* insert message */
	log_next_idx += msg->len;
	log_next_seq++;
}

/* /dev/kmsg - userspace message inject/listen interface */
struct devkmsg_user {
	u64 seq;
	u32 idx;
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
		if (copy_from_user(line, iv[i].iov_base, iv[i].iov_len))
			goto out;
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
	struct log *msg;
	u64 ts_usec;
	size_t i;
	size_t len;
	ssize_t ret;

	if (!user)
		return -EBADF;

	mutex_lock(&user->lock);
	raw_spin_lock(&logbuf_lock);
	while (user->seq == log_next_seq) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			raw_spin_unlock(&logbuf_lock);
			goto out;
		}

		raw_spin_unlock(&logbuf_lock);
		ret = wait_event_interruptible(log_wait,
					       user->seq != log_next_seq);
		if (ret)
			goto out;
		raw_spin_lock(&logbuf_lock);
	}

	if (user->seq < log_first_seq) {
		/* our last seen message is gone, return error and reset */
		user->idx = log_first_idx;
		user->seq = log_first_seq;
		ret = -EPIPE;
		raw_spin_unlock(&logbuf_lock);
		goto out;
	}

	msg = log_from_idx(user->idx);
	ts_usec = msg->ts_nsec;
	do_div(ts_usec, 1000);
	len = sprintf(user->buf, "%u,%llu,%llu;",
		      msg->level, user->seq, ts_usec);

	/* escape non-printable characters */
	for (i = 0; i < msg->text_len; i++) {
		char c = log_text(msg)[i];

		if (c < ' ' || c >= 128)
			len += sprintf(user->buf + len, "\\x%02x", c);
		else
			user->buf[len++] = c;
	}
	user->buf[len++] = '\n';

	if (msg->dict_len) {
		bool line = true;

		for (i = 0; i < msg->dict_len; i++) {
			char c = log_dict(msg)[i];

			if (line) {
				user->buf[len++] = ' ';
				line = false;
			}

			if (c == '\0') {
				user->buf[len++] = '\n';
				line = true;
				continue;
			}

			if (c < ' ' || c >= 128) {
				len += sprintf(user->buf + len, "\\x%02x", c);
				continue;
			}

			user->buf[len++] = c;
		}
		user->buf[len++] = '\n';
	}

	user->idx = log_next(user->idx);
	user->seq++;
	raw_spin_unlock(&logbuf_lock);

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

	raw_spin_lock(&logbuf_lock);
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
	raw_spin_unlock(&logbuf_lock);
	return ret;
}

static unsigned int devkmsg_poll(struct file *file, poll_table *wait)
{
	struct devkmsg_user *user = file->private_data;
	int ret = 0;

	if (!user)
		return POLLERR|POLLNVAL;

	poll_wait(file, &log_wait, wait);

	raw_spin_lock(&logbuf_lock);
	if (user->seq < log_next_seq) {
		/* return error when data has vanished underneath us */
		if (user->seq < log_first_seq)
			ret = POLLIN|POLLRDNORM|POLLERR|POLLPRI;
		ret = POLLIN|POLLRDNORM;
	}
	raw_spin_unlock(&logbuf_lock);

	return ret;
}

static int devkmsg_open(struct inode *inode, struct file *file)
{
	struct devkmsg_user *user;
	int err;

	/* write-only does not need any file context */
	if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		return 0;

	err = security_syslog(SYSLOG_ACTION_READ_ALL);
	if (err)
		return err;

	user = kmalloc(sizeof(struct devkmsg_user), GFP_KERNEL);
	if (!user)
		return -ENOMEM;

	mutex_init(&user->lock);

	raw_spin_lock(&logbuf_lock);
	user->idx = log_first_idx;
	user->seq = log_first_seq;
	raw_spin_unlock(&logbuf_lock);

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
 * This appends the listed symbols to /proc/vmcoreinfo
 *
 * /proc/vmcoreinfo is used by various utiilties, like crash and makedumpfile to
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
}
#endif

/* requested log_buf_len from kernel cmdline */
static unsigned long __initdata new_log_buf_len;

/* save requested log_buf_len since it's too early to process it */
static int __init log_buf_len_setup(char *str)
{
	unsigned size = memparse(str, &str);

	if (size)
		size = roundup_pow_of_two(size);
	if (size > log_buf_len)
		new_log_buf_len = size;

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
		unsigned long mem;

		mem = memblock_alloc(new_log_buf_len, PAGE_SIZE);
		if (!mem)
			return;
		new_log_buf = __va(mem);
	} else {
		new_log_buf = alloc_bootmem_nopanic(new_log_buf_len);
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

	pr_info("log_buf_len: %d\n", log_buf_len);
	pr_info("early log buf free: %d(%d%%)\n",
		free, (free * 100) / __LOG_BUF_LEN);
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
	return 1;
}
__setup("boot_delay=", boot_delay_setup);

static void boot_delay_msec(void)
{
	unsigned long long k;
	unsigned long timeout;

	if (boot_delay == 0 || system_state != SYSTEM_BOOTING)
		return;

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
static inline void boot_delay_msec(void)
{
}
#endif

#ifdef CONFIG_SECURITY_DMESG_RESTRICT
int dmesg_restrict = 1;
#else
int dmesg_restrict;
#endif

static int syslog_action_restricted(int type)
{
	if (dmesg_restrict)
		return 1;
	/* Unless restricted, we allow "read all" and "get buffer size" for everybody */
	return type != SYSLOG_ACTION_READ_ALL && type != SYSLOG_ACTION_SIZE_BUFFER;
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
		/* For historical reasons, accept CAP_SYS_ADMIN too, with a warning */
		if (capable(CAP_SYS_ADMIN)) {
			printk_once(KERN_WARNING "%s (%d): "
				 "Attempt to access syslog with CAP_SYS_ADMIN "
				 "but no CAP_SYSLOG (deprecated).\n",
				 current->comm, task_pid_nr(current));
			return 0;
		}
		return -EPERM;
	}
	return 0;
}

#if defined(CONFIG_PRINTK_TIME)
static bool printk_time = 1;
#else
static bool printk_time;
#endif
module_param_named(time, printk_time, bool, S_IRUGO | S_IWUSR);

static size_t prepend_timestamp(unsigned long long t, char *buf)
{
	unsigned long rem_ns;

	if (!printk_time)
		return 0;

	if (!buf)
		return 15;

	rem_ns = do_div(t, 1000000000);

	return sprintf(buf, "[%5lu.%06lu] ",
		       (unsigned long) t, rem_ns / 1000);
}

static int syslog_print_line(u32 idx, char *text, size_t size)
{
	struct log *msg;
	size_t len;

	msg = log_from_idx(idx);
	if (!text) {
		/* calculate length only */
		len = 3;

		if (msg->level > 9)
			len++;
		if (msg->level > 99)
			len++;
		len += prepend_timestamp(0, NULL);

		len += msg->text_len;
		len++;
		return len;
	}

	len = sprintf(text, "<%u>", msg->level);
	len += prepend_timestamp(msg->ts_nsec, text + len);
	if (len + msg->text_len > size)
		return -EINVAL;
	memcpy(text + len, log_text(msg), msg->text_len);
	len += msg->text_len;
	text[len++] = '\n';
	return len;
}

static int syslog_print(char __user *buf, int size)
{
	char *text;
	int len;

	text = kmalloc(LOG_LINE_MAX, GFP_KERNEL);
	if (!text)
		return -ENOMEM;

	raw_spin_lock_irq(&logbuf_lock);
	if (syslog_seq < log_first_seq) {
		/* messages are gone, move to first one */
		syslog_seq = log_first_seq;
		syslog_idx = log_first_idx;
	}
	len = syslog_print_line(syslog_idx, text, LOG_LINE_MAX);
	syslog_idx = log_next(syslog_idx);
	syslog_seq++;
	raw_spin_unlock_irq(&logbuf_lock);

	if (len > 0 && copy_to_user(buf, text, len))
		len = -EFAULT;

	kfree(text);
	return len;
}

static int syslog_print_all(char __user *buf, int size, bool clear)
{
	char *text;
	int len = 0;

	text = kmalloc(LOG_LINE_MAX, GFP_KERNEL);
	if (!text)
		return -ENOMEM;

	raw_spin_lock_irq(&logbuf_lock);
	if (buf) {
		u64 next_seq;
		u64 seq;
		u32 idx;

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
		while (seq < log_next_seq) {
			len += syslog_print_line(idx, NULL, 0);
			idx = log_next(idx);
			seq++;
		}
		seq = clear_seq;
		idx = clear_idx;
		while (len > size && seq < log_next_seq) {
			len -= syslog_print_line(idx, NULL, 0);
			idx = log_next(idx);
			seq++;
		}

		/* last message in this dump */
		next_seq = log_next_seq;

		len = 0;
		while (len >= 0 && seq < next_seq) {
			int textlen;

			textlen = syslog_print_line(idx, text, LOG_LINE_MAX);
			if (textlen < 0) {
				len = textlen;
				break;
			}
			idx = log_next(idx);
			seq++;

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
		}
		if (from_file) {
			/*
			 * Short-cut for poll(/"proc/kmsg") which simply checks
			 * for pending data, not the size; return the count of
			 * records, not the length.
			 */
			error = log_next_idx - syslog_idx;
		} else {
			u64 seq;
			u32 idx;

			error = 0;
			seq = syslog_seq;
			idx = syslog_idx;
			while (seq < log_next_seq) {
				error += syslog_print_line(idx, NULL, 0);
				idx = log_next(idx);
				seq++;
			}
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
	return do_syslog(type, buf, len, SYSLOG_FROM_CALL);
}

#ifdef	CONFIG_KGDB_KDB
/* kdb dmesg command needs access to the syslog buffer.  do_syslog()
 * uses locks so it cannot be used during debugging.  Just tell kdb
 * where the start and end of the physical and logical logs are.  This
 * is equivalent to do_syslog(3).
 */
void kdb_syslog_data(char *syslog_data[4])
{
	syslog_data[0] = log_buf;
	syslog_data[1] = log_buf + log_buf_len;
	syslog_data[2] = log_buf + log_first_idx;
	syslog_data[3] = log_buf + log_next_idx;
}
#endif	/* CONFIG_KGDB_KDB */

static bool __read_mostly ignore_loglevel;

static int __init ignore_loglevel_setup(char *str)
{
	ignore_loglevel = 1;
	printk(KERN_INFO "debug: ignoring loglevel setting.\n");

	return 0;
}

early_param("ignore_loglevel", ignore_loglevel_setup);
module_param(ignore_loglevel, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ignore_loglevel, "ignore loglevel setting, to"
	"print all kernel messages to the console.");

/*
 * Call the console drivers, asking them to write out
 * log_buf[start] to log_buf[end - 1].
 * The console_lock must be held.
 */
static void call_console_drivers(int level, const char *text, size_t len)
{
	struct console *con;

	trace_console(text, 0, len, len);

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

/* Check if we have any console registered that can be called early in boot. */
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
 *
 * This gets called with the 'logbuf_lock' spinlock held and
 * interrupts disabled. It should return with 'lockbuf_lock'
 * released but interrupts still disabled.
 */
static int console_trylock_for_printk(unsigned int cpu)
	__releases(&logbuf_lock)
{
	int retval = 0, wake = 0;

	if (console_trylock()) {
		retval = 1;

		/*
		 * If we can't use the console, we need to release
		 * the console semaphore by hand to avoid flushing
		 * the buffer. We need to hold the console semaphore
		 * in order to do this test safely.
		 */
		if (!can_use_console(cpu)) {
			console_locked = 0;
			wake = 1;
			retval = 0;
		}
	}
	logbuf_cpu = UINT_MAX;
	if (wake)
		up(&console_sem);
	raw_spin_unlock(&logbuf_lock);
	return retval;
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

asmlinkage int vprintk_emit(int facility, int level,
			    const char *dict, size_t dictlen,
			    const char *fmt, va_list args)
{
	static int recursion_bug;
	static char buf[LOG_LINE_MAX];
	static size_t buflen;
	static int buflevel;
	static char textbuf[LOG_LINE_MAX];
	static struct task_struct *cont;
	char *text = textbuf;
	size_t textlen;
	unsigned long flags;
	int this_cpu;
	bool newline = false;
	bool prefix = false;
	int printed_len = 0;

	boot_delay_msec();
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
		printed_len += strlen(recursion_msg);
		/* emit KERN_CRIT message */
		log_store(0, 2, NULL, 0, recursion_msg, printed_len);
	}

	/*
	 * The printf needs to come first; we need the syslog
	 * prefix which might be passed-in as a parameter.
	 */
	textlen = vscnprintf(text, sizeof(textbuf), fmt, args);

	/* mark and strip a trailing newline */
	if (textlen && text[textlen-1] == '\n') {
		textlen--;
		newline = true;
	}

	/* strip syslog prefix and extract log level or flags */
	if (text[0] == '<' && text[1] && text[2] == '>') {
		switch (text[1]) {
		case '0' ... '7':
			if (level == -1)
				level = text[1] - '0';
		case 'd':	/* KERN_DEFAULT */
			prefix = true;
		case 'c':	/* KERN_CONT */
			text += 3;
			textlen -= 3;
		}
	}

	if (buflen && (prefix || dict || cont != current)) {
		/* flush existing buffer */
		log_store(facility, buflevel, NULL, 0, buf, buflen);
		printed_len += buflen;
		buflen = 0;
	}

	if (buflen == 0) {
		/* remember level for first message in the buffer */
		if (level == -1)
			buflevel = default_message_loglevel;
		else
			buflevel = level;
	}

	if (buflen || !newline) {
		/* append to existing buffer, or buffer until next message */
		if (buflen + textlen > sizeof(buf))
			textlen = sizeof(buf) - buflen;
		memcpy(buf + buflen, text, textlen);
		buflen += textlen;
	}

	if (newline) {
		/* end of line; flush buffer */
		if (buflen) {
			log_store(facility, buflevel,
				  dict, dictlen, buf, buflen);
			printed_len += buflen;
			buflen = 0;
		} else {
			log_store(facility, buflevel,
				  dict, dictlen, text, textlen);
			printed_len += textlen;
		}
		cont = NULL;
	} else {
		/* remember thread which filled the buffer */
		cont = current;
	}

	/*
	 * Try to acquire and then immediately release the console semaphore.
	 * The release will print out buffers and wake up /dev/kmsg and syslog()
	 * users.
	 *
	 * The console_trylock_for_printk() function will release 'logbuf_lock'
	 * regardless of whether it actually gets the console semaphore or not.
	 */
	if (console_trylock_for_printk(this_cpu))
		console_unlock();

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
asmlinkage int printk(const char *fmt, ...)
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

#else

#define LOG_LINE_MAX 0
static struct log *log_from_idx(u32 idx) { return NULL; }
static u32 log_next(u32 idx) { return 0; }
static char *log_text(const struct log *msg) { return NULL; }
static void call_console_drivers(int level, const char *text, size_t len) {}

#endif /* CONFIG_PRINTK */

static int __add_preferred_console(char *name, int idx, char *options,
				   char *brl_options)
{
	struct console_cmdline *c;
	int i;

	/*
	 *	See if this tty is not yet registered, and
	 *	if we have a slot free.
	 */
	for (i = 0; i < MAX_CMDLINECONSOLES && console_cmdline[i].name[0]; i++)
		if (strcmp(console_cmdline[i].name, name) == 0 &&
			  console_cmdline[i].index == idx) {
				if (!brl_options)
					selected_console = i;
				return 0;
		}
	if (i == MAX_CMDLINECONSOLES)
		return -E2BIG;
	if (!brl_options)
		selected_console = i;
	c = &console_cmdline[i];
	strlcpy(c->name, name, sizeof(c->name));
	c->options = options;
#ifdef CONFIG_A11Y_BRAILLE_CONSOLE
	c->brl_options = brl_options;
#endif
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

#ifdef CONFIG_A11Y_BRAILLE_CONSOLE
	if (!memcmp(str, "brl,", 4)) {
		brl_options = "";
		str += 4;
	} else if (!memcmp(str, "brl=", 4)) {
		brl_options = str + 4;
		str = strchr(brl_options, ',');
		if (!str) {
			printk(KERN_ERR "need port name after brl=\n");
			return 1;
		}
		*(str++) = 0;
	}
#endif

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

	for (i = 0; i < MAX_CMDLINECONSOLES && console_cmdline[i].name[0]; i++)
		if (strcmp(console_cmdline[i].name, name) == 0 &&
			  console_cmdline[i].index == idx) {
				c = &console_cmdline[i];
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
	up(&console_sem);
}

void resume_console(void)
{
	if (!console_suspend_enabled)
		return;
	down(&console_sem);
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
static int __cpuinit console_cpu_notify(struct notifier_block *self,
	unsigned long action, void *hcpu)
{
	switch (action) {
	case CPU_ONLINE:
	case CPU_DEAD:
	case CPU_DYING:
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
	BUG_ON(in_interrupt());
	down(&console_sem);
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
	if (down_trylock(&console_sem))
		return 0;
	if (console_suspended) {
		up(&console_sem);
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

/*
 * Delayed printk version, for scheduler-internal messages:
 */
#define PRINTK_BUF_SIZE		512

#define PRINTK_PENDING_WAKEUP	0x01
#define PRINTK_PENDING_SCHED	0x02

static DEFINE_PER_CPU(int, printk_pending);
static DEFINE_PER_CPU(char [PRINTK_BUF_SIZE], printk_sched_buf);

void printk_tick(void)
{
	if (__this_cpu_read(printk_pending)) {
		int pending = __this_cpu_xchg(printk_pending, 0);
		if (pending & PRINTK_PENDING_SCHED) {
			char *buf = __get_cpu_var(printk_sched_buf);
			printk(KERN_WARNING "[sched_delayed] %s", buf);
		}
		if (pending & PRINTK_PENDING_WAKEUP)
			wake_up_interruptible(&log_wait);
	}
}

int printk_needs_cpu(int cpu)
{
	if (cpu_is_offline(cpu))
		printk_tick();
	return __this_cpu_read(printk_pending);
}

void wake_up_klogd(void)
{
	if (waitqueue_active(&log_wait))
		this_cpu_or(printk_pending, PRINTK_PENDING_WAKEUP);
}

/* the next printk record to write to the console */
static u64 console_seq;
static u32 console_idx;

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
	static u64 seen_seq;
	unsigned long flags;
	bool wake_klogd = false;
	bool retry;

	if (console_suspended) {
		up(&console_sem);
		return;
	}

	console_may_schedule = 0;

again:
	for (;;) {
		struct log *msg;
		static char text[LOG_LINE_MAX];
		size_t len, l;
		int level;

		raw_spin_lock_irqsave(&logbuf_lock, flags);
		if (seen_seq != log_next_seq) {
			wake_klogd = true;
			seen_seq = log_next_seq;
		}

		if (console_seq < log_first_seq) {
			/* messages are gone, move to first one */
			console_seq = log_first_seq;
			console_idx = log_first_idx;
		}

		if (console_seq == log_next_seq)
			break;

		msg = log_from_idx(console_idx);
		level = msg->level & 7;

		len = prepend_timestamp(msg->ts_nsec, text);
		l = msg->text_len;
		if (len + l + 1 >= sizeof(text))
			l = sizeof(text) - len - 1;
		memcpy(text + len, log_text(msg), l);
		len += l;
		text[len++] = '\n';

		console_idx = log_next(console_idx);
		console_seq++;
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

	up(&console_sem);

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
		if (down_trylock(&console_sem) != 0)
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
	printk(KERN_INFO "debug: skip boot console de-registration.\n");

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

	/*
	 * before we register a new CON_BOOT console, make sure we don't
	 * already have a valid console
	 */
	if (console_drivers && newcon->flags & CON_BOOT) {
		/* find the last or real console */
		for_each_console(bcon) {
			if (!(bcon->flags & CON_BOOT)) {
				printk(KERN_INFO "Too late to register bootconsole %s%d\n",
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
	for (i = 0; i < MAX_CMDLINECONSOLES && console_cmdline[i].name[0];
			i++) {
		if (strcmp(console_cmdline[i].name, newcon->name) != 0)
			continue;
		if (newcon->index >= 0 &&
		    newcon->index != console_cmdline[i].index)
			continue;
		if (newcon->index < 0)
			newcon->index = console_cmdline[i].index;
#ifdef CONFIG_A11Y_BRAILLE_CONSOLE
		if (console_cmdline[i].brl_options) {
			newcon->flags |= CON_BRL;
			braille_register_console(newcon,
					console_cmdline[i].index,
					console_cmdline[i].options,
					console_cmdline[i].brl_options);
			return;
		}
#endif
		if (newcon->setup &&
		    newcon->setup(newcon, console_cmdline[i].options) != 0)
			break;
		newcon->flags |= CON_ENABLED;
		newcon->index = console_cmdline[i].index;
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
	if (bcon &&
	    ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV) &&
	    !keep_bootcon) {
		/* we need to iterate through twice, to make sure we print
		 * everything out, before we unregister the console(s)
		 */
		printk(KERN_INFO "console [%s%d] enabled, bootconsole disabled\n",
			newcon->name, newcon->index);
		for_each_console(bcon)
			if (bcon->flags & CON_BOOT)
				unregister_console(bcon);
	} else {
		printk(KERN_INFO "%sconsole [%s%d] enabled\n",
			(newcon->flags & CON_BOOT) ? "boot" : "" ,
			newcon->name, newcon->index);
	}
}
EXPORT_SYMBOL(register_console);

int unregister_console(struct console *console)
{
        struct console *a, *b;
	int res = 1;

#ifdef CONFIG_A11Y_BRAILLE_CONSOLE
	if (console->flags & CON_BRL)
		return braille_unregister_console(console);
#endif

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
			printk(KERN_INFO "turn off boot console %s%d\n",
				con->name, con->index);
			unregister_console(con);
		}
	}
	hotcpu_notifier(console_cpu_notify, 0);
	return 0;
}
late_initcall(printk_late_init);

#if defined CONFIG_PRINTK

int printk_sched(const char *fmt, ...)
{
	unsigned long flags;
	va_list args;
	char *buf;
	int r;

	local_irq_save(flags);
	buf = __get_cpu_var(printk_sched_buf);

	va_start(args, fmt);
	r = vsnprintf(buf, PRINTK_BUF_SIZE, fmt, args);
	va_end(args);

	__this_cpu_or(printk_pending, PRINTK_PENDING_SCHED);
	local_irq_restore(flags);

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
 * Iterate through each of the dump devices and call the oops/panic
 * callbacks with the log buffer.
 */
void kmsg_dump(enum kmsg_dump_reason reason)
{
	u64 idx;
	struct kmsg_dumper *dumper;
	const char *s1, *s2;
	unsigned long l1, l2;
	unsigned long flags;

	if ((reason > KMSG_DUMP_OOPS) && !always_kmsg_dump)
		return;

	/* Theoretically, the log could move on after we do this, but
	   there's not a lot we can do about that. The new messages
	   will overwrite the start of what we dump. */

	raw_spin_lock_irqsave(&logbuf_lock, flags);
	if (syslog_seq < log_first_seq)
		idx = syslog_idx;
	else
		idx = log_first_idx;

	if (idx > log_next_idx) {
		s1 = log_buf;
		l1 = log_next_idx;

		s2 = log_buf + idx;
		l2 = log_buf_len - idx;
	} else {
		s1 = "";
		l1 = 0;

		s2 = log_buf + idx;
		l2 = log_next_idx - idx;
	}
	raw_spin_unlock_irqrestore(&logbuf_lock, flags);

	rcu_read_lock();
	list_for_each_entry_rcu(dumper, &dump_list, list)
		dumper->dump(dumper, reason, s1, l1, s2, l2);
	rcu_read_unlock();
}
#endif
