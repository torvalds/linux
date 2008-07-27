/*
 * Task I/O accounting operations
 */
#ifndef __TASK_IO_ACCOUNTING_OPS_INCLUDED
#define __TASK_IO_ACCOUNTING_OPS_INCLUDED

#include <linux/sched.h>

#ifdef CONFIG_TASK_IO_ACCOUNTING
static inline void task_io_account_read(size_t bytes)
{
	current->ioac.blk.read_bytes += bytes;
}

/*
 * We approximate number of blocks, because we account bytes only.
 * A 'block' is 512 bytes
 */
static inline unsigned long task_io_get_inblock(const struct task_struct *p)
{
	return p->ioac.blk.read_bytes >> 9;
}

static inline void task_io_account_write(size_t bytes)
{
	current->ioac.blk.write_bytes += bytes;
}

/*
 * We approximate number of blocks, because we account bytes only.
 * A 'block' is 512 bytes
 */
static inline unsigned long task_io_get_oublock(const struct task_struct *p)
{
	return p->ioac.blk.write_bytes >> 9;
}

static inline void task_io_account_cancelled_write(size_t bytes)
{
	current->ioac.blk.cancelled_write_bytes += bytes;
}

static inline void task_io_accounting_init(struct proc_io_accounting *ioac)
{
	memset(ioac, 0, sizeof(*ioac));
}

static inline void task_blk_io_accounting_add(struct proc_io_accounting *dst,
						struct proc_io_accounting *src)
{
	dst->blk.read_bytes += src->blk.read_bytes;
	dst->blk.write_bytes += src->blk.write_bytes;
	dst->blk.cancelled_write_bytes += src->blk.cancelled_write_bytes;
}

#else

static inline void task_io_account_read(size_t bytes)
{
}

static inline unsigned long task_io_get_inblock(const struct task_struct *p)
{
	return 0;
}

static inline void task_io_account_write(size_t bytes)
{
}

static inline unsigned long task_io_get_oublock(const struct task_struct *p)
{
	return 0;
}

static inline void task_io_account_cancelled_write(size_t bytes)
{
}

static inline void task_io_accounting_init(struct proc_io_accounting *ioac)
{
}

static inline void task_blk_io_accounting_add(struct proc_io_accounting *dst,
						struct proc_io_accounting *src)
{
}

#endif /* CONFIG_TASK_IO_ACCOUNTING */

#ifdef CONFIG_TASK_XACCT
static inline void task_chr_io_accounting_add(struct proc_io_accounting *dst,
						struct proc_io_accounting *src)
{
	dst->chr.rchar += src->chr.rchar;
	dst->chr.wchar += src->chr.wchar;
	dst->chr.syscr += src->chr.syscr;
	dst->chr.syscw += src->chr.syscw;
}
#else
static inline void task_chr_io_accounting_add(struct proc_io_accounting *dst,
						struct proc_io_accounting *src)
{
}
#endif /* CONFIG_TASK_XACCT */

static inline void task_io_accounting_add(struct proc_io_accounting *dst,
						struct proc_io_accounting *src)
{
	task_chr_io_accounting_add(dst, src);
	task_blk_io_accounting_add(dst, src);
}
#endif /* __TASK_IO_ACCOUNTING_OPS_INCLUDED */
