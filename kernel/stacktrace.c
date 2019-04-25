/*
 * kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/stacktrace.h>

/**
 * stack_trace_print - Print the entries in the stack trace
 * @entries:	Pointer to storage array
 * @nr_entries:	Number of entries in the storage array
 * @spaces:	Number of leading spaces to print
 */
void stack_trace_print(unsigned long *entries, unsigned int nr_entries,
		       int spaces)
{
	unsigned int i;

	if (WARN_ON(!entries))
		return;

	for (i = 0; i < nr_entries; i++)
		printk("%*c%pS\n", 1 + spaces, ' ', (void *)entries[i]);
}
EXPORT_SYMBOL_GPL(stack_trace_print);

void print_stack_trace(struct stack_trace *trace, int spaces)
{
	stack_trace_print(trace->entries, trace->nr_entries, spaces);
}
EXPORT_SYMBOL_GPL(print_stack_trace);

/**
 * stack_trace_snprint - Print the entries in the stack trace into a buffer
 * @buf:	Pointer to the print buffer
 * @size:	Size of the print buffer
 * @entries:	Pointer to storage array
 * @nr_entries:	Number of entries in the storage array
 * @spaces:	Number of leading spaces to print
 *
 * Return: Number of bytes printed.
 */
int stack_trace_snprint(char *buf, size_t size, unsigned long *entries,
			unsigned int nr_entries, int spaces)
{
	unsigned int generated, i, total = 0;

	if (WARN_ON(!entries))
		return 0;

	for (i = 0; i < nr_entries && size; i++) {
		generated = snprintf(buf, size, "%*c%pS\n", 1 + spaces, ' ',
				     (void *)entries[i]);

		total += generated;
		if (generated >= size) {
			buf += size;
			size = 0;
		} else {
			buf += generated;
			size -= generated;
		}
	}

	return total;
}
EXPORT_SYMBOL_GPL(stack_trace_snprint);

int snprint_stack_trace(char *buf, size_t size,
			struct stack_trace *trace, int spaces)
{
	return stack_trace_snprint(buf, size, trace->entries,
				   trace->nr_entries, spaces);
}
EXPORT_SYMBOL_GPL(snprint_stack_trace);

/*
 * Architectures that do not implement save_stack_trace_*()
 * get these weak aliases and once-per-bootup warnings
 * (whenever this facility is utilized - for example by procfs):
 */
__weak void
save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	WARN_ONCE(1, KERN_INFO "save_stack_trace_tsk() not implemented yet.\n");
}

__weak void
save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	WARN_ONCE(1, KERN_INFO "save_stack_trace_regs() not implemented yet.\n");
}

__weak int
save_stack_trace_tsk_reliable(struct task_struct *tsk,
			      struct stack_trace *trace)
{
	WARN_ONCE(1, KERN_INFO "save_stack_tsk_reliable() not implemented yet.\n");
	return -ENOSYS;
}

/**
 * stack_trace_save - Save a stack trace into a storage array
 * @store:	Pointer to storage array
 * @size:	Size of the storage array
 * @skipnr:	Number of entries to skip at the start of the stack trace
 *
 * Return: Number of trace entries stored
 */
unsigned int stack_trace_save(unsigned long *store, unsigned int size,
			      unsigned int skipnr)
{
	struct stack_trace trace = {
		.entries	= store,
		.max_entries	= size,
		.skip		= skipnr + 1,
	};

	save_stack_trace(&trace);
	return trace.nr_entries;
}
EXPORT_SYMBOL_GPL(stack_trace_save);

/**
 * stack_trace_save_tsk - Save a task stack trace into a storage array
 * @task:	The task to examine
 * @store:	Pointer to storage array
 * @size:	Size of the storage array
 * @skipnr:	Number of entries to skip at the start of the stack trace
 *
 * Return: Number of trace entries stored
 */
unsigned int stack_trace_save_tsk(struct task_struct *task,
				  unsigned long *store, unsigned int size,
				  unsigned int skipnr)
{
	struct stack_trace trace = {
		.entries	= store,
		.max_entries	= size,
		.skip		= skipnr + 1,
	};

	save_stack_trace_tsk(task, &trace);
	return trace.nr_entries;
}

/**
 * stack_trace_save_regs - Save a stack trace based on pt_regs into a storage array
 * @regs:	Pointer to pt_regs to examine
 * @store:	Pointer to storage array
 * @size:	Size of the storage array
 * @skipnr:	Number of entries to skip at the start of the stack trace
 *
 * Return: Number of trace entries stored
 */
unsigned int stack_trace_save_regs(struct pt_regs *regs, unsigned long *store,
				   unsigned int size, unsigned int skipnr)
{
	struct stack_trace trace = {
		.entries	= store,
		.max_entries	= size,
		.skip		= skipnr,
	};

	save_stack_trace_regs(regs, &trace);
	return trace.nr_entries;
}

#ifdef CONFIG_HAVE_RELIABLE_STACKTRACE
/**
 * stack_trace_save_tsk_reliable - Save task stack with verification
 * @tsk:	Pointer to the task to examine
 * @store:	Pointer to storage array
 * @size:	Size of the storage array
 *
 * Return:	An error if it detects any unreliable features of the
 *		stack. Otherwise it guarantees that the stack trace is
 *		reliable and returns the number of entries stored.
 *
 * If the task is not 'current', the caller *must* ensure the task is inactive.
 */
int stack_trace_save_tsk_reliable(struct task_struct *tsk, unsigned long *store,
				  unsigned int size)
{
	struct stack_trace trace = {
		.entries	= store,
		.max_entries	= size,
	};
	int ret = save_stack_trace_tsk_reliable(tsk, &trace);

	return ret ? ret : trace.nr_entries;
}
#endif

#ifdef CONFIG_USER_STACKTRACE_SUPPORT
/**
 * stack_trace_save_user - Save a user space stack trace into a storage array
 * @store:	Pointer to storage array
 * @size:	Size of the storage array
 *
 * Return: Number of trace entries stored
 */
unsigned int stack_trace_save_user(unsigned long *store, unsigned int size)
{
	struct stack_trace trace = {
		.entries	= store,
		.max_entries	= size,
	};

	save_stack_trace_user(&trace);
	return trace.nr_entries;
}
#endif /* CONFIG_USER_STACKTRACE_SUPPORT */
