/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * internal.h - printk internal definitions
 */
#include <linux/percpu.h>

#if defined(CONFIG_PRINTK) && defined(CONFIG_SYSCTL)
void __init printk_sysctl_init(void);
int devkmsg_sysctl_set_loglvl(struct ctl_table *table, int write,
			      void *buffer, size_t *lenp, loff_t *ppos);
#else
#define printk_sysctl_init() do { } while (0)
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

__printf(4, 0)
int vprintk_store(int facility, int level,
		  const struct dev_printk_info *dev_info,
		  const char *fmt, va_list args);

__printf(1, 0) int vprintk_default(const char *fmt, va_list args);
__printf(1, 0) int vprintk_deferred(const char *fmt, va_list args);

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

u16 printk_parse_prefix(const char *text, int *level,
			enum printk_info_flags *flags);
#else

#define PRINTK_PREFIX_MAX	0
#define PRINTK_MESSAGE_MAX	0
#define PRINTKRB_RECORD_MAX	0

/*
 * In !PRINTK builds we still export console_sem
 * semaphore and some of console functions (console_unlock()/etc.), so
 * printk-safe must preserve the existing local IRQ guarantees.
 */
#define printk_safe_enter_irqsave(flags) local_irq_save(flags)
#define printk_safe_exit_irqrestore(flags) local_irq_restore(flags)

static inline bool printk_percpu_data_ready(void) { return false; }
#endif /* CONFIG_PRINTK */

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
