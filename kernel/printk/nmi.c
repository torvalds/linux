/*
 * nmi.c - Safe printk in NMI context
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/debug_locks.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/irq_work.h>
#include <linux/printk.h>

#include "internal.h"

/*
 * printk() could not take logbuf_lock in NMI context. Instead,
 * it uses an alternative implementation that temporary stores
 * the strings into a per-CPU buffer. The content of the buffer
 * is later flushed into the main ring buffer via IRQ work.
 *
 * The alternative implementation is chosen transparently
 * via @printk_func per-CPU variable.
 *
 * The implementation allows to flush the strings also from another CPU.
 * There are situations when we want to make sure that all buffers
 * were handled or when IRQs are blocked.
 */
DEFINE_PER_CPU(printk_func_t, printk_func) = vprintk_default;
static int printk_nmi_irq_ready;
atomic_t nmi_message_lost;

#define NMI_LOG_BUF_LEN ((1 << CONFIG_NMI_LOG_BUF_SHIFT) -		\
			 sizeof(atomic_t) - sizeof(struct irq_work))

struct nmi_seq_buf {
	atomic_t		len;	/* length of written data */
	struct irq_work		work;	/* IRQ work that flushes the buffer */
	unsigned char		buffer[NMI_LOG_BUF_LEN];
};
static DEFINE_PER_CPU(struct nmi_seq_buf, nmi_print_seq);

/*
 * Safe printk() for NMI context. It uses a per-CPU buffer to
 * store the message. NMIs are not nested, so there is always only
 * one writer running. But the buffer might get flushed from another
 * CPU, so we need to be careful.
 */
static int vprintk_nmi(const char *fmt, va_list args)
{
	struct nmi_seq_buf *s = this_cpu_ptr(&nmi_print_seq);
	int add = 0;
	size_t len;

again:
	len = atomic_read(&s->len);

	if (len >= sizeof(s->buffer)) {
		atomic_inc(&nmi_message_lost);
		return 0;
	}

	/*
	 * Make sure that all old data have been read before the buffer was
	 * reseted. This is not needed when we just append data.
	 */
	if (!len)
		smp_rmb();

	add = vsnprintf(s->buffer + len, sizeof(s->buffer) - len, fmt, args);

	/*
	 * Do it once again if the buffer has been flushed in the meantime.
	 * Note that atomic_cmpxchg() is an implicit memory barrier that
	 * makes sure that the data were written before updating s->len.
	 */
	if (atomic_cmpxchg(&s->len, len, len + add) != len)
		goto again;

	/* Get flushed in a more safe context. */
	if (add && printk_nmi_irq_ready) {
		/* Make sure that IRQ work is really initialized. */
		smp_rmb();
		irq_work_queue(&s->work);
	}

	return add;
}

/*
 * printk one line from the temporary buffer from @start index until
 * and including the @end index.
 */
static void print_nmi_seq_line(struct nmi_seq_buf *s, int start, int end)
{
	const char *buf = s->buffer + start;

	/*
	 * The buffers are flushed in NMI only on panic.  The messages must
	 * go only into the ring buffer at this stage.  Consoles will get
	 * explicitly called later when a crashdump is not generated.
	 */
	if (in_nmi())
		printk_deferred("%.*s", (end - start) + 1, buf);
	else
		printk("%.*s", (end - start) + 1, buf);

}

/*
 * Flush data from the associated per_CPU buffer. The function
 * can be called either via IRQ work or independently.
 */
static void __printk_nmi_flush(struct irq_work *work)
{
	static raw_spinlock_t read_lock =
		__RAW_SPIN_LOCK_INITIALIZER(read_lock);
	struct nmi_seq_buf *s = container_of(work, struct nmi_seq_buf, work);
	unsigned long flags;
	size_t len, size;
	int i, last_i;

	/*
	 * The lock has two functions. First, one reader has to flush all
	 * available message to make the lockless synchronization with
	 * writers easier. Second, we do not want to mix messages from
	 * different CPUs. This is especially important when printing
	 * a backtrace.
	 */
	raw_spin_lock_irqsave(&read_lock, flags);

	i = 0;
more:
	len = atomic_read(&s->len);

	/*
	 * This is just a paranoid check that nobody has manipulated
	 * the buffer an unexpected way. If we printed something then
	 * @len must only increase.
	 */
	if (i && i >= len)
		pr_err("printk_nmi_flush: internal error: i=%d >= len=%zu\n",
		       i, len);

	if (!len)
		goto out; /* Someone else has already flushed the buffer. */

	/* Make sure that data has been written up to the @len */
	smp_rmb();

	size = min(len, sizeof(s->buffer));
	last_i = i;

	/* Print line by line. */
	for (; i < size; i++) {
		if (s->buffer[i] == '\n') {
			print_nmi_seq_line(s, last_i, i);
			last_i = i + 1;
		}
	}
	/* Check if there was a partial line. */
	if (last_i < size) {
		print_nmi_seq_line(s, last_i, size - 1);
		pr_cont("\n");
	}

	/*
	 * Check that nothing has got added in the meantime and truncate
	 * the buffer. Note that atomic_cmpxchg() is an implicit memory
	 * barrier that makes sure that the data were copied before
	 * updating s->len.
	 */
	if (atomic_cmpxchg(&s->len, len, 0) != len)
		goto more;

out:
	raw_spin_unlock_irqrestore(&read_lock, flags);
}

/**
 * printk_nmi_flush - flush all per-cpu nmi buffers.
 *
 * The buffers are flushed automatically via IRQ work. This function
 * is useful only when someone wants to be sure that all buffers have
 * been flushed at some point.
 */
void printk_nmi_flush(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		__printk_nmi_flush(&per_cpu(nmi_print_seq, cpu).work);
}

/**
 * printk_nmi_flush_on_panic - flush all per-cpu nmi buffers when the system
 *	goes down.
 *
 * Similar to printk_nmi_flush() but it can be called even in NMI context when
 * the system goes down. It does the best effort to get NMI messages into
 * the main ring buffer.
 *
 * Note that it could try harder when there is only one CPU online.
 */
void printk_nmi_flush_on_panic(void)
{
	/*
	 * Make sure that we could access the main ring buffer.
	 * Do not risk a double release when more CPUs are up.
	 */
	if (in_nmi() && raw_spin_is_locked(&logbuf_lock)) {
		if (num_online_cpus() > 1)
			return;

		debug_locks_off();
		raw_spin_lock_init(&logbuf_lock);
	}

	printk_nmi_flush();
}

void __init printk_nmi_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct nmi_seq_buf *s = &per_cpu(nmi_print_seq, cpu);

		init_irq_work(&s->work, __printk_nmi_flush);
	}

	/* Make sure that IRQ works are initialized before enabling. */
	smp_wmb();
	printk_nmi_irq_ready = 1;

	/* Flush pending messages that did not have scheduled IRQ works. */
	printk_nmi_flush();
}

void printk_nmi_enter(void)
{
	this_cpu_write(printk_func, vprintk_nmi);
}

void printk_nmi_exit(void)
{
	this_cpu_write(printk_func, vprintk_default);
}
