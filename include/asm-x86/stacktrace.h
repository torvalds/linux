#ifndef ASM_X86__STACKTRACE_H
#define ASM_X86__STACKTRACE_H

extern int kstack_depth_to_print;

/* Generic stack tracer with callbacks */

struct stacktrace_ops {
	void (*warning)(void *data, char *msg);
	/* msg must contain %s for the symbol */
	void (*warning_symbol)(void *data, char *msg, unsigned long symbol);
	void (*address)(void *data, unsigned long address, int reliable);
	/* On negative return stop dumping */
	int (*stack)(void *data, char *name);
};

void dump_trace(struct task_struct *tsk, struct pt_regs *regs,
		unsigned long *stack, unsigned long bp,
		const struct stacktrace_ops *ops, void *data);

#endif /* ASM_X86__STACKTRACE_H */
