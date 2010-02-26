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
#include <linux/syscalls.h>
#include <linux/kexec.h>
#include <linux/ratelimit.h>
#include <linux/kmsg_dump.h>

#include <asm/uaccess.h>

/*
 * for_each_console() allows you to iterate on each console
 */
#define for_each_console(con) \
	for (con = console_drivers; con != NULL; con = con->next)

/*
 * Architectures can override it:
 */
void asmlinkage __attribute__((weak)) early_printk(const char *fmt, ...)
{
}

#define __LOG_BUF_LEN	(1 << CONFIG_LOG_BUF_SHIFT)

/* printk's without a loglevel use this.. */
#define DEFAULT_MESSAGE_LOGLEVEL 4 /* KERN_WARNING */

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

static int saved_console_loglevel = -1;

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
static DECLARE_MUTEX(console_sem);
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
 * logbuf_lock protects log_buf, log_start, log_end, con_start and logged_chars
 * It is also used in interesting ways to provide interlocking in
 * release_console_sem().
 */
static DEFINE_SPINLOCK(logbuf_lock);

#define LOG_BUF_MASK (log_buf_len-1)
#define LOG_BUF(idx) (log_buf[(idx) & LOG_BUF_MASK])

/*
 * The indices into log_buf are not constrained to log_buf_len - they
 * must be masked before subscripting
 */
static unsigned log_start;	/* Index into log_buf: next char to be read by syslog() */
static unsigned con_start;	/* Index into log_buf: next char to be sent to consoles */
static unsigned log_end;	/* Index into log_buf: most-recently-written-char + 1 */

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

#ifdef CONFIG_PRINTK

static char __log_buf[__LOG_BUF_LEN];
static char *log_buf = __log_buf;
static int log_buf_len = __LOG_BUF_LEN;
static unsigned logged_chars; /* Number of chars produced since last read+clear operation */

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
	VMCOREINFO_SYMBOL(log_end);
	VMCOREINFO_SYMBOL(log_buf_len);
	VMCOREINFO_SYMBOL(logged_chars);
}
#endif

static int __init log_buf_len_setup(char *str)
{
	unsigned size = memparse(str, &str);
	unsigned long flags;

	if (size)
		size = roundup_pow_of_two(size);
	if (size > log_buf_len) {
		unsigned start, dest_idx, offset;
		char *new_log_buf;

		new_log_buf = alloc_bootmem(size);
		if (!new_log_buf) {
			printk(KERN_WARNING "log_buf_len: allocation failed\n");
			goto out;
		}

		spin_lock_irqsave(&logbuf_lock, flags);
		log_buf_len = size;
		log_buf = new_log_buf;

		offset = start = min(con_start, log_start);
		dest_idx = 0;
		while (start != log_end) {
			log_buf[dest_idx] = __log_buf[start & (__LOG_BUF_LEN - 1)];
			start++;
			dest_idx++;
		}
		log_start -= offset;
		con_start -= offset;
		log_end -= offset;
		spin_unlock_irqrestore(&logbuf_lock, flags);

		printk(KERN_NOTICE "log_buf_len: %d\n", log_buf_len);
	}
out:
	return 1;
}

__setup("log_buf_len=", log_buf_len_setup);

#ifdef CONFIG_BOOT_PRINTK_DELAY

static unsigned int boot_delay; /* msecs delay after each printk during bootup */
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

/*
 * Commands to do_syslog:
 *
 * 	0 -- Close the log.  Currently a NOP.
 * 	1 -- Open the log. Currently a NOP.
 * 	2 -- Read from the log.
 * 	3 -- Read all messages remaining in the ring buffer.
 * 	4 -- Read and clear all messages remaining in the ring buffer
 * 	5 -- Clear ring buffer.
 * 	6 -- Disable printk's to console
 * 	7 -- Enable printk's to console
 *	8 -- Set level of messages printed to console
 *	9 -- Return number of unread characters in the log buffer
 *     10 -- Return size of the log buffer
 */
int do_syslog(int type, char __user *buf, int len)
{
	unsigned i, j, limit, count;
	int do_clear = 0;
	char c;
	int error = 0;

	error = security_syslog(type);
	if (error)
		return error;

	switch (type) {
	case 0:		/* Close log */
		break;
	case 1:		/* Open log */
		break;
	case 2:		/* Read from log */
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
							(log_start - log_end));
		if (error)
			goto out;
		i = 0;
		spin_lock_irq(&logbuf_lock);
		while (!error && (log_start != log_end) && i < len) {
			c = LOG_BUF(log_start);
			log_start++;
			spin_unlock_irq(&logbuf_lock);
			error = __put_user(c,buf);
			buf++;
			i++;
			cond_resched();
			spin_lock_irq(&logbuf_lock);
		}
		spin_unlock_irq(&logbuf_lock);
		if (!error)
			error = i;
		break;
	case 4:		/* Read/clear last kernel messages */
		do_clear = 1;
		/* FALL THRU */
	case 3:		/* Read last kernel messages */
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
		count = len;
		if (count > log_buf_len)
			count = log_buf_len;
		spin_lock_irq(&logbuf_lock);
		if (count > logged_chars)
			count = logged_chars;
		if (do_clear)
			logged_chars = 0;
		limit = log_end;
		/*
		 * __put_user() could sleep, and while we sleep
		 * printk() could overwrite the messages
		 * we try to copy to user space. Therefore
		 * the messages are copied in reverse. <manfreds>
		 */
		for (i = 0; i < count && !error; i++) {
			j = limit-1-i;
			if (j + log_buf_len < log_end)
				break;
			c = LOG_BUF(j);
			spin_unlock_irq(&logbuf_lock);
			error = __put_user(c,&buf[count-1-i]);
			cond_resched();
			spin_lock_irq(&logbuf_lock);
		}
		spin_unlock_irq(&logbuf_lock);
		if (error)
			break;
		error = i;
		if (i != count) {
			int offset = count-error;
			/* buffer overflow during copy, correct user buffer. */
			for (i = 0; i < error; i++) {
				if (__get_user(c,&buf[i+offset]) ||
				    __put_user(c,&buf[i])) {
					error = -EFAULT;
					break;
				}
				cond_resched();
			}
		}
		break;
	case 5:		/* Clear ring buffer */
		logged_chars = 0;
		break;
	case 6:		/* Disable logging to console */
		if (saved_console_loglevel == -1)
			saved_console_loglevel = console_loglevel;
		console_loglevel = minimum_console_loglevel;
		break;
	case 7:		/* Enable logging to console */
		if (saved_console_loglevel != -1) {
			console_loglevel = saved_console_loglevel;
			saved_console_loglevel = -1;
		}
		break;
	case 8:		/* Set level of messages printed to console */
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
	case 9:		/* Number of chars in the log buffer */
		error = log_end - log_start;
		break;
	case 10:	/* Size of the log buffer */
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
	return do_syslog(type, buf, len);
}

/*
 * Call the console drivers on a range of log_buf
 */
static void __call_console_drivers(unsigned start, unsigned end)
{
	struct console *con;

	for_each_console(con) {
		if ((con->flags & CON_ENABLED) && con->write &&
				(cpu_online(smp_processor_id()) ||
				(con->flags & CON_ANYTIME)))
			con->write(con, &LOG_BUF(start), end - start);
	}
}

static int __read_mostly ignore_loglevel;

static int __init ignore_loglevel_setup(char *str)
{
	ignore_loglevel = 1;
	printk(KERN_INFO "debug: ignoring loglevel setting.\n");

	return 0;
}

early_param("ignore_loglevel", ignore_loglevel_setup);

/*
 * Write out chars from start to end - 1 inclusive
 */
static void _call_console_drivers(unsigned start,
				unsigned end, int msg_log_level)
{
	if ((msg_log_level < console_loglevel || ignore_loglevel) &&
			console_drivers && start != end) {
		if ((start & LOG_BUF_MASK) > (end & LOG_BUF_MASK)) {
			/* wrapped write */
			__call_console_drivers(start & LOG_BUF_MASK,
						log_buf_len);
			__call_console_drivers(0, end & LOG_BUF_MASK);
		} else {
			__call_console_drivers(start, end);
		}
	}
}

/*
 * Call the console drivers, asking them to write out
 * log_buf[start] to log_buf[end - 1].
 * The console_sem must be held.
 */
static void call_console_drivers(unsigned start, unsigned end)
{
	unsigned cur_index, start_print;
	static int msg_level = -1;

	BUG_ON(((int)(start - end)) > 0);

	cur_index = start;
	start_print = start;
	while (cur_index != end) {
		if (msg_level < 0 && ((end - cur_index) > 2) &&
				LOG_BUF(cur_index + 0) == '<' &&
				LOG_BUF(cur_index + 1) >= '0' &&
				LOG_BUF(cur_index + 1) <= '7' &&
				LOG_BUF(cur_index + 2) == '>') {
			msg_level = LOG_BUF(cur_index + 1) - '0';
			cur_index += 3;
			start_print = cur_index;
		}
		while (cur_index != end) {
			char c = LOG_BUF(cur_index);

			cur_index++;
			if (c == '\n') {
				if (msg_level < 0) {
					/*
					 * printk() has already given us loglevel tags in
					 * the buffer.  This code is here in case the
					 * log buffer has wrapped right round and scribbled
					 * on those tags
					 */
					msg_level = default_message_loglevel;
				}
				_call_console_drivers(start_print, cur_index, msg_level);
				msg_level = -1;
				start_print = cur_index;
				break;
			}
		}
	}
	_call_console_drivers(start_print, end, msg_level);
}

static void emit_log_char(char c)
{
	LOG_BUF(log_end) = c;
	log_end++;
	if (log_end - log_start > log_buf_len)
		log_start = log_end - log_buf_len;
	if (log_end - con_start > log_buf_len)
		con_start = log_end - log_buf_len;
	if (logged_chars < log_buf_len)
		logged_chars++;
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

	/* If a crash is occurring, make sure we can't deadlock */
	spin_lock_init(&logbuf_lock);
	/* And make sure that we print immediately */
	init_MUTEX(&console_sem);
}

#if defined(CONFIG_PRINTK_TIME)
static int printk_time = 1;
#else
static int printk_time = 0;
#endif
module_param_named(time, printk_time, bool, S_IRUGO | S_IWUSR);

/* Check if we have any console registered that can be called early in boot. */
static int have_callable_console(void)
{
	struct console *con;

	for_each_console(con)
		if (con->flags & CON_ANYTIME)
			return 1;

	return 0;
}

/**
 * printk - print a kernel message
 * @fmt: format string
 *
 * This is printk().  It can be called from any context.  We want it to work.
 *
 * We try to grab the console_sem.  If we succeed, it's easy - we log the output and
 * call the console drivers.  If we fail to get the semaphore we place the output
 * into the log buffer and return.  The current holder of the console_sem will
 * notice the new output in release_console_sem() and will send it to the
 * consoles before releasing the semaphore.
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

	va_start(args, fmt);
	r = vprintk(fmt, args);
	va_end(args);

	return r;
}

/* cpu currently holding logbuf_lock */
static volatile unsigned int printk_cpu = UINT_MAX;

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
 * console_semaphore held, and 'console_locked' set) if it
 * is successful, false otherwise.
 *
 * This gets called with the 'logbuf_lock' spinlock held and
 * interrupts disabled. It should return with 'lockbuf_lock'
 * released but interrupts still disabled.
 */
static int acquire_console_semaphore_for_printk(unsigned int cpu)
{
	int retval = 0;

	if (!try_acquire_console_sem()) {
		retval = 1;

		/*
		 * If we can't use the console, we need to release
		 * the console semaphore by hand to avoid flushing
		 * the buffer. We need to hold the console semaphore
		 * in order to do this test safely.
		 */
		if (!can_use_console(cpu)) {
			console_locked = 0;
			up(&console_sem);
			retval = 0;
		}
	}
	printk_cpu = UINT_MAX;
	spin_unlock(&logbuf_lock);
	return retval;
}
static const char recursion_bug_msg [] =
		KERN_CRIT "BUG: recent printk recursion!\n";
static int recursion_bug;
static int new_text_line = 1;
static char printk_buf[1024];

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

asmlinkage int vprintk(const char *fmt, va_list args)
{
	int printed_len = 0;
	int current_log_level = default_message_loglevel;
	unsigned long flags;
	int this_cpu;
	char *p;

	boot_delay_msec();
	printk_delay();

	preempt_disable();
	/* This stops the holder of console_sem just where we want him */
	raw_local_irq_save(flags);
	this_cpu = smp_processor_id();

	/*
	 * Ouch, printk recursed into itself!
	 */
	if (unlikely(printk_cpu == this_cpu)) {
		/*
		 * If a crash is occurring during printk() on this CPU,
		 * then try to get the crash message out but make sure
		 * we can't deadlock. Otherwise just return to avoid the
		 * recursion and return - but flag the recursion so that
		 * it can be printed at the next appropriate moment:
		 */
		if (!oops_in_progress) {
			recursion_bug = 1;
			goto out_restore_irqs;
		}
		zap_locks();
	}

	lockdep_off();
	spin_lock(&logbuf_lock);
	printk_cpu = this_cpu;

	if (recursion_bug) {
		recursion_bug = 0;
		strcpy(printk_buf, recursion_bug_msg);
		printed_len = strlen(recursion_bug_msg);
	}
	/* Emit the output into the temporary buffer */
	printed_len += vscnprintf(printk_buf + printed_len,
				  sizeof(printk_buf) - printed_len, fmt, args);


	p = printk_buf;

	/* Do we have a loglevel in the string? */
	if (p[0] == '<') {
		unsigned char c = p[1];
		if (c && p[2] == '>') {
			switch (c) {
			case '0' ... '7': /* loglevel */
				current_log_level = c - '0';
			/* Fallthrough - make sure we're on a new line */
			case 'd': /* KERN_DEFAULT */
				if (!new_text_line) {
					emit_log_char('\n');
					new_text_line = 1;
				}
			/* Fallthrough - skip the loglevel */
			case 'c': /* KERN_CONT */
				p += 3;
				break;
			}
		}
	}

	/*
	 * Copy the output into log_buf.  If the caller didn't provide
	 * appropriate log level tags, we insert them here
	 */
	for ( ; *p; p++) {
		if (new_text_line) {
			/* Always output the token */
			emit_log_char('<');
			emit_log_char(current_log_level + '0');
			emit_log_char('>');
			printed_len += 3;
			new_text_line = 0;

			if (printk_time) {
				/* Follow the token with the time */
				char tbuf[50], *tp;
				unsigned tlen;
				unsigned long long t;
				unsigned long nanosec_rem;

				t = cpu_clock(printk_cpu);
				nanosec_rem = do_div(t, 1000000000);
				tlen = sprintf(tbuf, "[%5lu.%06lu] ",
						(unsigned long) t,
						nanosec_rem / 1000);

				for (tp = tbuf; tp < tbuf + tlen; tp++)
					emit_log_char(*tp);
				printed_len += tlen;
			}

			if (!*p)
				break;
		}

		emit_log_char(*p);
		if (*p == '\n')
			new_text_line = 1;
	}

	/*
	 * Try to acquire and then immediately release the
	 * console semaphore. The release will do all the
	 * actual magic (print out buffers, wake up klogd,
	 * etc). 
	 *
	 * The acquire_console_semaphore_for_printk() function
	 * will release 'logbuf_lock' regardless of whether it
	 * actually gets the semaphore or not.
	 */
	if (acquire_console_semaphore_for_printk(this_cpu))
		release_console_sem();

	lockdep_on();
out_restore_irqs:
	raw_local_irq_restore(flags);

	preempt_enable();
	return printed_len;
}
EXPORT_SYMBOL(printk);
EXPORT_SYMBOL(vprintk);

#else

static void call_console_drivers(unsigned start, unsigned end)
{
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

int console_suspend_enabled = 1;
EXPORT_SYMBOL(console_suspend_enabled);

static int __init console_suspend_disable(char *str)
{
	console_suspend_enabled = 0;
	return 1;
}
__setup("no_console_suspend", console_suspend_disable);

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
	acquire_console_sem();
	console_suspended = 1;
	up(&console_sem);
}

void resume_console(void)
{
	if (!console_suspend_enabled)
		return;
	down(&console_sem);
	console_suspended = 0;
	release_console_sem();
}

/**
 * acquire_console_sem - lock the console system for exclusive use.
 *
 * Acquires a semaphore which guarantees that the caller has
 * exclusive access to the console system and the console_drivers list.
 *
 * Can sleep, returns nothing.
 */
void acquire_console_sem(void)
{
	BUG_ON(in_interrupt());
	down(&console_sem);
	if (console_suspended)
		return;
	console_locked = 1;
	console_may_schedule = 1;
}
EXPORT_SYMBOL(acquire_console_sem);

int try_acquire_console_sem(void)
{
	if (down_trylock(&console_sem))
		return -1;
	if (console_suspended) {
		up(&console_sem);
		return -1;
	}
	console_locked = 1;
	console_may_schedule = 0;
	return 0;
}
EXPORT_SYMBOL(try_acquire_console_sem);

int is_console_locked(void)
{
	return console_locked;
}

static DEFINE_PER_CPU(int, printk_pending);

void printk_tick(void)
{
	if (__get_cpu_var(printk_pending)) {
		__get_cpu_var(printk_pending) = 0;
		wake_up_interruptible(&log_wait);
	}
}

int printk_needs_cpu(int cpu)
{
	return per_cpu(printk_pending, cpu);
}

void wake_up_klogd(void)
{
	if (waitqueue_active(&log_wait))
		__raw_get_cpu_var(printk_pending) = 1;
}

/**
 * release_console_sem - unlock the console system
 *
 * Releases the semaphore which the caller holds on the console system
 * and the console driver list.
 *
 * While the semaphore was held, console output may have been buffered
 * by printk().  If this is the case, release_console_sem() emits
 * the output prior to releasing the semaphore.
 *
 * If there is output waiting for klogd, we wake it up.
 *
 * release_console_sem() may be called from any context.
 */
void release_console_sem(void)
{
	unsigned long flags;
	unsigned _con_start, _log_end;
	unsigned wake_klogd = 0;

	if (console_suspended) {
		up(&console_sem);
		return;
	}

	console_may_schedule = 0;

	for ( ; ; ) {
		spin_lock_irqsave(&logbuf_lock, flags);
		wake_klogd |= log_start - log_end;
		if (con_start == log_end)
			break;			/* Nothing to print */
		_con_start = con_start;
		_log_end = log_end;
		con_start = log_end;		/* Flush */
		spin_unlock(&logbuf_lock);
		stop_critical_timings();	/* don't trace print latency */
		call_console_drivers(_con_start, _log_end);
		start_critical_timings();
		local_irq_restore(flags);
	}
	console_locked = 0;
	up(&console_sem);
	spin_unlock_irqrestore(&logbuf_lock, flags);
	if (wake_klogd)
		wake_up_klogd();
}
EXPORT_SYMBOL(release_console_sem);

/**
 * console_conditional_schedule - yield the CPU if required
 *
 * If the console code is currently allowed to sleep, and
 * if this CPU should yield the CPU to another task, do
 * so here.
 *
 * Must be called within acquire_console_sem().
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
		acquire_console_sem();

	console_locked = 1;
	console_may_schedule = 0;
	for_each_console(c)
		if ((c->flags & CON_ENABLED) && c->unblank)
			c->unblank();
	release_console_sem();
}

/*
 * Return the console tty driver structure and its associated index
 */
struct tty_driver *console_device(int *index)
{
	struct console *c;
	struct tty_driver *driver = NULL;

	acquire_console_sem();
	for_each_console(c) {
		if (!c->device)
			continue;
		driver = c->device(c, index);
		if (driver)
			break;
	}
	release_console_sem();
	return driver;
}

/*
 * Prevent further output on the passed console device so that (for example)
 * serial drivers can disable console output before suspending a port, and can
 * re-enable output afterwards.
 */
void console_stop(struct console *console)
{
	acquire_console_sem();
	console->flags &= ~CON_ENABLED;
	release_console_sem();
}
EXPORT_SYMBOL(console_stop);

void console_start(struct console *console)
{
	acquire_console_sem();
	console->flags |= CON_ENABLED;
	release_console_sem();
}
EXPORT_SYMBOL(console_start);

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
	acquire_console_sem();
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
		 * release_console_sem() will print out the buffered messages
		 * for us.
		 */
		spin_lock_irqsave(&logbuf_lock, flags);
		con_start = log_start;
		spin_unlock_irqrestore(&logbuf_lock, flags);
	}
	release_console_sem();

	/*
	 * By unregistering the bootconsoles after we enable the real console
	 * we get the "console xxx enabled" message on all the consoles -
	 * boot consoles, real consoles, etc - this is to ensure that end
	 * users know there might be something in the kernel's log buffer that
	 * went to the bootconsole (that they do not see on the real console)
	 */
	if (bcon && ((newcon->flags & (CON_CONSDEV | CON_BOOT)) == CON_CONSDEV)) {
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

	acquire_console_sem();
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

	release_console_sem();
	return res;
}
EXPORT_SYMBOL(unregister_console);

static int __init disable_boot_consoles(void)
{
	struct console *con;

	for_each_console(con) {
		if (con->flags & CON_BOOT) {
			printk(KERN_INFO "turn off boot console %s%d\n",
				con->name, con->index);
			unregister_console(con);
		}
	}
	return 0;
}
late_initcall(disable_boot_consoles);

#if defined CONFIG_PRINTK

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
		list_add_tail(&dumper->list, &dump_list);
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
		list_del(&dumper->list);
		err = 0;
	}
	spin_unlock_irqrestore(&dump_list_lock, flags);

	return err;
}
EXPORT_SYMBOL_GPL(kmsg_dump_unregister);

static const char const *kmsg_reasons[] = {
	[KMSG_DUMP_OOPS]	= "oops",
	[KMSG_DUMP_PANIC]	= "panic",
	[KMSG_DUMP_KEXEC]	= "kexec",
};

static const char *kmsg_to_str(enum kmsg_dump_reason reason)
{
	if (reason >= ARRAY_SIZE(kmsg_reasons) || reason < 0)
		return "unknown";

	return kmsg_reasons[reason];
}

/**
 * kmsg_dump - dump kernel log to kernel message dumpers.
 * @reason: the reason (oops, panic etc) for dumping
 *
 * Iterate through each of the dump devices and call the oops/panic
 * callbacks with the log buffer.
 */
void kmsg_dump(enum kmsg_dump_reason reason)
{
	unsigned long end;
	unsigned chars;
	struct kmsg_dumper *dumper;
	const char *s1, *s2;
	unsigned long l1, l2;
	unsigned long flags;

	/* Theoretically, the log could move on after we do this, but
	   there's not a lot we can do about that. The new messages
	   will overwrite the start of what we dump. */
	spin_lock_irqsave(&logbuf_lock, flags);
	end = log_end & LOG_BUF_MASK;
	chars = logged_chars;
	spin_unlock_irqrestore(&logbuf_lock, flags);

	if (logged_chars > end) {
		s1 = log_buf + log_buf_len - logged_chars + end;
		l1 = logged_chars - end;

		s2 = log_buf;
		l2 = end;
	} else {
		s1 = "";
		l1 = 0;

		s2 = log_buf + end - logged_chars;
		l2 = logged_chars;
	}

	if (!spin_trylock_irqsave(&dump_list_lock, flags)) {
		printk(KERN_ERR "dump_kmsg: dump list lock is held during %s, skipping dump\n",
				kmsg_to_str(reason));
		return;
	}
	list_for_each_entry(dumper, &dump_list, list)
		dumper->dump(dumper, reason, s1, l1, s2, l2);
	spin_unlock_irqrestore(&dump_list_lock, flags);
}
#endif
