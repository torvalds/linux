/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * internal.h - printk internal definitions
 */
#include <linux/console.h>
#include <linux/percpu.h>
#include <linux/types.h>

#if defined(CONFIG_PRINTK) && defined(CONFIG_SYSCTL)
struct ctl_table;
void __init printk_sysctl_init(void);
int devkmsg_sysctl_set_loglvl(const struct ctl_table *table, int write,
			      void *buffer, size_t *lenp, loff_t *ppos);
#else
#define printk_sysctl_init() do { } while (0)
#endif

#define con_printk(lvl, con, fmt, ...)				\
	printk(lvl pr_fmt("%s%sconsole [%s%d] " fmt),		\
		(con->flags & CON_NBCON) ? "" : "legacy ",	\
		(con->flags & CON_BOOT) ? "boot" : "",		\
		con->name, con->index, ##__VA_ARGS__)

/*
 * Identify if legacy printing is forced in a dedicated kthread. If
 * true, all printing via console lock occurs within a dedicated
 * legacy printer thread. The only exception is on panic, after the
 * nbcon consoles have had their chance to print the panic messages
 * first.
 */
#ifdef CONFIG_PREEMPT_RT
# define force_legacy_kthread()	(true)
#else
# define force_legacy_kthread()	(false)
#endif

#ifdef CONFIG_PRINTK

#ifdef CONFIG_PRINTK_CALLER
#define PRINTK_PREFIX_MAX	48
#else
#define PRINTK_PREFIX_MAX	32
#endif

/*
 * the maximum size of a formatted record (i.e. with prefix added
 * per line and dropped messages or in extended message format)
 */
#define PRINTK_MESSAGE_MAX	2048

/* the maximum size allowed to be reserved for a record */
#define PRINTKRB_RECORD_MAX	1024

/* Flags for a single printk record. */
enum printk_info_flags {
	LOG_NEWLINE	= 2,	/* text ended with a newline */
	LOG_CONT	= 8,	/* text is a fragment of a continuation line */
};

struct printk_ringbuffer;
struct dev_printk_info;

extern struct printk_ringbuffer *prb;
extern bool printk_kthreads_running;

__printf(4, 0)
int vprintk_store(int facility, int level,
		  const struct dev_printk_info *dev_info,
		  const char *fmt, va_list args);

__printf(1, 0) int vprintk_default(const char *fmt, va_list args);
__printf(1, 0) int vprintk_deferred(const char *fmt, va_list args);

void __printk_safe_enter(void);
void __printk_safe_exit(void);

bool printk_percpu_data_ready(void);

#define printk_safe_enter_irqsave(flags)	\
	do {					\
		local_irq_save(flags);		\
		__printk_safe_enter();		\
	} while (0)

#define printk_safe_exit_irqrestore(flags)	\
	do {					\
		__printk_safe_exit();		\
		local_irq_restore(flags);	\
	} while (0)

void defer_console_output(void);
bool is_printk_legacy_deferred(void);

u16 printk_parse_prefix(const char *text, int *level,
			enum printk_info_flags *flags);
void console_lock_spinning_enable(void);
int console_lock_spinning_disable_and_check(int cookie);

u64 nbcon_seq_read(struct console *con);
void nbcon_seq_force(struct console *con, u64 seq);
bool nbcon_alloc(struct console *con);
void nbcon_free(struct console *con);
enum nbcon_prio nbcon_get_default_prio(void);
void nbcon_atomic_flush_pending(void);
bool nbcon_legacy_emit_next_record(struct console *con, bool *handover,
				   int cookie, bool use_atomic);
bool nbcon_kthread_create(struct console *con);
void nbcon_kthread_stop(struct console *con);
void nbcon_kthreads_wake(void);

/*
 * Check if the given console is currently capable and allowed to print
 * records. Note that this function does not consider the current context,
 * which can also play a role in deciding if @con can be used to print
 * records.
 */
static inline bool console_is_usable(struct console *con, short flags, bool use_atomic)
{
	if (!(flags & CON_ENABLED))
		return false;

	if ((flags & CON_SUSPENDED))
		return false;

	if (flags & CON_NBCON) {
		/* The write_atomic() callback is optional. */
		if (use_atomic && !con->write_atomic)
			return false;

		/*
		 * For the !use_atomic case, @printk_kthreads_running is not
		 * checked because the write_thread() callback is also used
		 * via the legacy loop when the printer threads are not
		 * available.
		 */
	} else {
		if (!con->write)
			return false;
	}

	/*
	 * Console drivers may assume that per-cpu resources have been
	 * allocated. So unless they're explicitly marked as being able to
	 * cope (CON_ANYTIME) don't call them until this CPU is officially up.
	 */
	if (!cpu_online(raw_smp_processor_id()) && !(flags & CON_ANYTIME))
		return false;

	return true;
}

/**
 * nbcon_kthread_wake - Wake up a console printing thread
 * @con:	Console to operate on
 */
static inline void nbcon_kthread_wake(struct console *con)
{
	/*
	 * Guarantee any new records can be seen by tasks preparing to wait
	 * before this context checks if the rcuwait is empty.
	 *
	 * The full memory barrier in rcuwait_wake_up() pairs with the full
	 * memory barrier within set_current_state() of
	 * ___rcuwait_wait_event(), which is called after prepare_to_rcuwait()
	 * adds the waiter but before it has checked the wait condition.
	 *
	 * This pairs with nbcon_kthread_func:A.
	 */
	rcuwait_wake_up(&con->rcuwait); /* LMM(nbcon_kthread_wake:A) */
}

#else

#define PRINTK_PREFIX_MAX	0
#define PRINTK_MESSAGE_MAX	0
#define PRINTKRB_RECORD_MAX	0

#define printk_kthreads_running (false)

/*
 * In !PRINTK builds we still export console_sem
 * semaphore and some of console functions (console_unlock()/etc.), so
 * printk-safe must preserve the existing local IRQ guarantees.
 */
#define printk_safe_enter_irqsave(flags) local_irq_save(flags)
#define printk_safe_exit_irqrestore(flags) local_irq_restore(flags)

static inline bool printk_percpu_data_ready(void) { return false; }
static inline void defer_console_output(void) { }
static inline bool is_printk_legacy_deferred(void) { return false; }
static inline u64 nbcon_seq_read(struct console *con) { return 0; }
static inline void nbcon_seq_force(struct console *con, u64 seq) { }
static inline bool nbcon_alloc(struct console *con) { return false; }
static inline void nbcon_free(struct console *con) { }
static inline enum nbcon_prio nbcon_get_default_prio(void) { return NBCON_PRIO_NONE; }
static inline void nbcon_atomic_flush_pending(void) { }
static inline bool nbcon_legacy_emit_next_record(struct console *con, bool *handover,
						 int cookie, bool use_atomic) { return false; }
static inline void nbcon_kthread_wake(struct console *con) { }
static inline void nbcon_kthreads_wake(void) { }

static inline bool console_is_usable(struct console *con, short flags,
				     bool use_atomic) { return false; }

#endif /* CONFIG_PRINTK */

extern bool have_boot_console;
extern bool have_nbcon_console;
extern bool have_legacy_console;
extern bool legacy_allow_panic_sync;

/**
 * struct console_flush_type - Define available console flush methods
 * @nbcon_atomic:	Flush directly using nbcon_atomic() callback
 * @nbcon_offload:	Offload flush to printer thread
 * @legacy_direct:	Call the legacy loop in this context
 * @legacy_offload:	Offload the legacy loop into IRQ or legacy thread
 *
 * Note that the legacy loop also flushes the nbcon consoles.
 */
struct console_flush_type {
	bool	nbcon_atomic;
	bool	nbcon_offload;
	bool	legacy_direct;
	bool	legacy_offload;
};

/*
 * Identify which console flushing methods should be used in the context of
 * the caller.
 */
static inline void printk_get_console_flush_type(struct console_flush_type *ft)
{
	memset(ft, 0, sizeof(*ft));

	switch (nbcon_get_default_prio()) {
	case NBCON_PRIO_NORMAL:
		if (have_nbcon_console && !have_boot_console) {
			if (printk_kthreads_running)
				ft->nbcon_offload = true;
			else
				ft->nbcon_atomic = true;
		}

		/* Legacy consoles are flushed directly when possible. */
		if (have_legacy_console || have_boot_console) {
			if (!is_printk_legacy_deferred())
				ft->legacy_direct = true;
			else
				ft->legacy_offload = true;
		}
		break;

	case NBCON_PRIO_EMERGENCY:
		if (have_nbcon_console && !have_boot_console)
			ft->nbcon_atomic = true;

		/* Legacy consoles are flushed directly when possible. */
		if (have_legacy_console || have_boot_console) {
			if (!is_printk_legacy_deferred())
				ft->legacy_direct = true;
			else
				ft->legacy_offload = true;
		}
		break;

	case NBCON_PRIO_PANIC:
		/*
		 * In panic, the nbcon consoles will directly print. But
		 * only allowed if there are no boot consoles.
		 */
		if (have_nbcon_console && !have_boot_console)
			ft->nbcon_atomic = true;

		if (have_legacy_console || have_boot_console) {
			/*
			 * This is the same decision as NBCON_PRIO_NORMAL
			 * except that offloading never occurs in panic.
			 *
			 * Note that console_flush_on_panic() will flush
			 * legacy consoles anyway, even if unsafe.
			 */
			if (!is_printk_legacy_deferred())
				ft->legacy_direct = true;

			/*
			 * In panic, if nbcon atomic printing occurs,
			 * the legacy consoles must remain silent until
			 * explicitly allowed.
			 */
			if (ft->nbcon_atomic && !legacy_allow_panic_sync)
				ft->legacy_direct = false;
		}
		break;

	default:
		WARN_ON_ONCE(1);
		break;
	}
}

extern struct printk_buffers printk_shared_pbufs;

/**
 * struct printk_buffers - Buffers to read/format/output printk messages.
 * @outbuf:	After formatting, contains text to output.
 * @scratchbuf:	Used as temporary ringbuffer reading and string-print space.
 */
struct printk_buffers {
	char	outbuf[PRINTK_MESSAGE_MAX];
	char	scratchbuf[PRINTKRB_RECORD_MAX];
};

/**
 * struct printk_message - Container for a prepared printk message.
 * @pbufs:	printk buffers used to prepare the message.
 * @outbuf_len:	The length of prepared text in @pbufs->outbuf to output. This
 *		does not count the terminator. A value of 0 means there is
 *		nothing to output and this record should be skipped.
 * @seq:	The sequence number of the record used for @pbufs->outbuf.
 * @dropped:	The number of dropped records from reading @seq.
 */
struct printk_message {
	struct printk_buffers	*pbufs;
	unsigned int		outbuf_len;
	u64			seq;
	unsigned long		dropped;
};

bool other_cpu_in_panic(void);
bool printk_get_next_message(struct printk_message *pmsg, u64 seq,
			     bool is_extended, bool may_supress);

#ifdef CONFIG_PRINTK
void console_prepend_dropped(struct printk_message *pmsg, unsigned long dropped);
void console_prepend_replay(struct printk_message *pmsg);
#endif
