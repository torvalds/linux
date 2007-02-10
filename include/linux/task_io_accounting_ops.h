/*
 * Task I/O accounting operations
 */
#ifndef __TASK_IO_ACCOUNTING_OPS_INCLUDED
#define __TASK_IO_ACCOUNTING_OPS_INCLUDED

#ifdef CONFIG_TASK_IO_ACCOUNTING
static inline void task_io_account_read(size_t bytes)
{
	current->ioac.read_bytes += bytes;
}

static inline void task_io_account_write(size_t bytes)
{
	current->ioac.write_bytes += bytes;
}

static inline void task_io_account_cancelled_write(size_t bytes)
{
	current->ioac.cancelled_write_bytes += bytes;
}

static inline void task_io_accounting_init(struct task_struct *tsk)
{
	memset(&tsk->ioac, 0, sizeof(tsk->ioac));
}

#else

static inline void task_io_account_read(size_t bytes)
{
}

static inline void task_io_account_write(size_t bytes)
{
}

static inline void task_io_account_cancelled_write(size_t bytes)
{
}

static inline void task_io_accounting_init(struct task_struct *tsk)
{
}

#endif		/* CONFIG_TASK_IO_ACCOUNTING */
#endif		/* __TASK_IO_ACCOUNTING_OPS_INCLUDED */
