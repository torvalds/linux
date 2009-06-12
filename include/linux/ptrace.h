#ifndef _LINUX_PTRACE_H
#define _LINUX_PTRACE_H
/* ptrace.h */
/* structs and defines to help the user use the ptrace system call. */

/* has the defines to get at the registers. */

#define PTRACE_TRACEME		   0
#define PTRACE_PEEKTEXT		   1
#define PTRACE_PEEKDATA		   2
#define PTRACE_PEEKUSR		   3
#define PTRACE_POKETEXT		   4
#define PTRACE_POKEDATA		   5
#define PTRACE_POKEUSR		   6
#define PTRACE_CONT		   7
#define PTRACE_KILL		   8
#define PTRACE_SINGLESTEP	   9

#define PTRACE_ATTACH		  16
#define PTRACE_DETACH		  17

#define PTRACE_SYSCALL		  24

/* 0x4200-0x4300 are reserved for architecture-independent additions.  */
#define PTRACE_SETOPTIONS	0x4200
#define PTRACE_GETEVENTMSG	0x4201
#define PTRACE_GETSIGINFO	0x4202
#define PTRACE_SETSIGINFO	0x4203

/* options set using PTRACE_SETOPTIONS */
#define PTRACE_O_TRACESYSGOOD	0x00000001
#define PTRACE_O_TRACEFORK	0x00000002
#define PTRACE_O_TRACEVFORK	0x00000004
#define PTRACE_O_TRACECLONE	0x00000008
#define PTRACE_O_TRACEEXEC	0x00000010
#define PTRACE_O_TRACEVFORKDONE	0x00000020
#define PTRACE_O_TRACEEXIT	0x00000040

#define PTRACE_O_MASK		0x0000007f

/* Wait extended result codes for the above trace options.  */
#define PTRACE_EVENT_FORK	1
#define PTRACE_EVENT_VFORK	2
#define PTRACE_EVENT_CLONE	3
#define PTRACE_EVENT_EXEC	4
#define PTRACE_EVENT_VFORK_DONE	5
#define PTRACE_EVENT_EXIT	6

#include <asm/ptrace.h>

#ifdef __KERNEL__
/*
 * Ptrace flags
 *
 * The owner ship rules for task->ptrace which holds the ptrace
 * flags is simple.  When a task is running it owns it's task->ptrace
 * flags.  When the a task is stopped the ptracer owns task->ptrace.
 */

#define PT_PTRACED	0x00000001
#define PT_DTRACE	0x00000002	/* delayed trace (used on m68k, i386) */
#define PT_TRACESYSGOOD	0x00000004
#define PT_PTRACE_CAP	0x00000008	/* ptracer can follow suid-exec */
#define PT_TRACE_FORK	0x00000010
#define PT_TRACE_VFORK	0x00000020
#define PT_TRACE_CLONE	0x00000040
#define PT_TRACE_EXEC	0x00000080
#define PT_TRACE_VFORK_DONE	0x00000100
#define PT_TRACE_EXIT	0x00000200

#define PT_TRACE_MASK	0x000003f4

/* single stepping state bits (used on ARM and PA-RISC) */
#define PT_SINGLESTEP_BIT	31
#define PT_SINGLESTEP		(1<<PT_SINGLESTEP_BIT)
#define PT_BLOCKSTEP_BIT	30
#define PT_BLOCKSTEP		(1<<PT_BLOCKSTEP_BIT)

#include <linux/compiler.h>		/* For unlikely.  */
#include <linux/sched.h>		/* For struct task_struct.  */


extern long arch_ptrace(struct task_struct *child, long request, long addr, long data);
extern struct task_struct *ptrace_get_task_struct(pid_t pid);
extern int ptrace_traceme(void);
extern int ptrace_readdata(struct task_struct *tsk, unsigned long src, char __user *dst, int len);
extern int ptrace_writedata(struct task_struct *tsk, char __user *src, unsigned long dst, int len);
extern int ptrace_attach(struct task_struct *tsk);
extern int ptrace_detach(struct task_struct *, unsigned int);
extern void ptrace_disable(struct task_struct *);
extern int ptrace_check_attach(struct task_struct *task, int kill);
extern int ptrace_request(struct task_struct *child, long request, long addr, long data);
extern void ptrace_notify(int exit_code);
extern void __ptrace_link(struct task_struct *child,
			  struct task_struct *new_parent);
extern void __ptrace_unlink(struct task_struct *child);
extern void exit_ptrace(struct task_struct *tracer);
#define PTRACE_MODE_READ   1
#define PTRACE_MODE_ATTACH 2
/* Returns 0 on success, -errno on denial. */
extern int __ptrace_may_access(struct task_struct *task, unsigned int mode);
/* Returns true on success, false on denial. */
extern bool ptrace_may_access(struct task_struct *task, unsigned int mode);

static inline int ptrace_reparented(struct task_struct *child)
{
	return child->real_parent != child->parent;
}
static inline void ptrace_link(struct task_struct *child,
			       struct task_struct *new_parent)
{
	if (unlikely(child->ptrace))
		__ptrace_link(child, new_parent);
}
static inline void ptrace_unlink(struct task_struct *child)
{
	if (unlikely(child->ptrace))
		__ptrace_unlink(child);
}

int generic_ptrace_peekdata(struct task_struct *tsk, long addr, long data);
int generic_ptrace_pokedata(struct task_struct *tsk, long addr, long data);

/**
 * task_ptrace - return %PT_* flags that apply to a task
 * @task:	pointer to &task_struct in question
 *
 * Returns the %PT_* flags that apply to @task.
 */
static inline int task_ptrace(struct task_struct *task)
{
	return task->ptrace;
}

/**
 * ptrace_event - possibly stop for a ptrace event notification
 * @mask:	%PT_* bit to check in @current->ptrace
 * @event:	%PTRACE_EVENT_* value to report if @mask is set
 * @message:	value for %PTRACE_GETEVENTMSG to return
 *
 * This checks the @mask bit to see if ptrace wants stops for this event.
 * If so we stop, reporting @event and @message to the ptrace parent.
 *
 * Returns nonzero if we did a ptrace notification, zero if not.
 *
 * Called without locks.
 */
static inline int ptrace_event(int mask, int event, unsigned long message)
{
	if (mask && likely(!(current->ptrace & mask)))
		return 0;
	current->ptrace_message = message;
	ptrace_notify((event << 8) | SIGTRAP);
	return 1;
}

/**
 * ptrace_init_task - initialize ptrace state for a new child
 * @child:		new child task
 * @ptrace:		true if child should be ptrace'd by parent's tracer
 *
 * This is called immediately after adding @child to its parent's children
 * list.  @ptrace is false in the normal case, and true to ptrace @child.
 *
 * Called with current's siglock and write_lock_irq(&tasklist_lock) held.
 */
static inline void ptrace_init_task(struct task_struct *child, bool ptrace)
{
	INIT_LIST_HEAD(&child->ptrace_entry);
	INIT_LIST_HEAD(&child->ptraced);
	child->parent = child->real_parent;
	child->ptrace = 0;
	if (unlikely(ptrace)) {
		child->ptrace = current->ptrace;
		ptrace_link(child, current->parent);
	}
}

/**
 * ptrace_release_task - final ptrace-related cleanup of a zombie being reaped
 * @task:	task in %EXIT_DEAD state
 *
 * Called with write_lock(&tasklist_lock) held.
 */
static inline void ptrace_release_task(struct task_struct *task)
{
	BUG_ON(!list_empty(&task->ptraced));
	ptrace_unlink(task);
	BUG_ON(!list_empty(&task->ptrace_entry));
}

#ifndef force_successful_syscall_return
/*
 * System call handlers that, upon successful completion, need to return a
 * negative value should call force_successful_syscall_return() right before
 * returning.  On architectures where the syscall convention provides for a
 * separate error flag (e.g., alpha, ia64, ppc{,64}, sparc{,64}, possibly
 * others), this macro can be used to ensure that the error flag will not get
 * set.  On architectures which do not support a separate error flag, the macro
 * is a no-op and the spurious error condition needs to be filtered out by some
 * other means (e.g., in user-level, by passing an extra argument to the
 * syscall handler, or something along those lines).
 */
#define force_successful_syscall_return() do { } while (0)
#endif

/*
 * <asm/ptrace.h> should define the following things inside #ifdef __KERNEL__.
 *
 * These do-nothing inlines are used when the arch does not
 * implement single-step.  The kerneldoc comments are here
 * to document the interface for all arch definitions.
 */

#ifndef arch_has_single_step
/**
 * arch_has_single_step - does this CPU support user-mode single-step?
 *
 * If this is defined, then there must be function declarations or
 * inlines for user_enable_single_step() and user_disable_single_step().
 * arch_has_single_step() should evaluate to nonzero iff the machine
 * supports instruction single-step for user mode.
 * It can be a constant or it can test a CPU feature bit.
 */
#define arch_has_single_step()		(0)

/**
 * user_enable_single_step - single-step in user-mode task
 * @task: either current or a task stopped in %TASK_TRACED
 *
 * This can only be called when arch_has_single_step() has returned nonzero.
 * Set @task so that when it returns to user mode, it will trap after the
 * next single instruction executes.  If arch_has_block_step() is defined,
 * this must clear the effects of user_enable_block_step() too.
 */
static inline void user_enable_single_step(struct task_struct *task)
{
	BUG();			/* This can never be called.  */
}

/**
 * user_disable_single_step - cancel user-mode single-step
 * @task: either current or a task stopped in %TASK_TRACED
 *
 * Clear @task of the effects of user_enable_single_step() and
 * user_enable_block_step().  This can be called whether or not either
 * of those was ever called on @task, and even if arch_has_single_step()
 * returned zero.
 */
static inline void user_disable_single_step(struct task_struct *task)
{
}
#endif	/* arch_has_single_step */

#ifndef arch_has_block_step
/**
 * arch_has_block_step - does this CPU support user-mode block-step?
 *
 * If this is defined, then there must be a function declaration or inline
 * for user_enable_block_step(), and arch_has_single_step() must be defined
 * too.  arch_has_block_step() should evaluate to nonzero iff the machine
 * supports step-until-branch for user mode.  It can be a constant or it
 * can test a CPU feature bit.
 */
#define arch_has_block_step()		(0)

/**
 * user_enable_block_step - step until branch in user-mode task
 * @task: either current or a task stopped in %TASK_TRACED
 *
 * This can only be called when arch_has_block_step() has returned nonzero,
 * and will never be called when single-instruction stepping is being used.
 * Set @task so that when it returns to user mode, it will trap after the
 * next branch or trap taken.
 */
static inline void user_enable_block_step(struct task_struct *task)
{
	BUG();			/* This can never be called.  */
}
#endif	/* arch_has_block_step */

#ifndef arch_ptrace_stop_needed
/**
 * arch_ptrace_stop_needed - Decide whether arch_ptrace_stop() should be called
 * @code:	current->exit_code value ptrace will stop with
 * @info:	siginfo_t pointer (or %NULL) for signal ptrace will stop with
 *
 * This is called with the siglock held, to decide whether or not it's
 * necessary to release the siglock and call arch_ptrace_stop() with the
 * same @code and @info arguments.  It can be defined to a constant if
 * arch_ptrace_stop() is never required, or always is.  On machines where
 * this makes sense, it should be defined to a quick test to optimize out
 * calling arch_ptrace_stop() when it would be superfluous.  For example,
 * if the thread has not been back to user mode since the last stop, the
 * thread state might indicate that nothing needs to be done.
 */
#define arch_ptrace_stop_needed(code, info)	(0)
#endif

#ifndef arch_ptrace_stop
/**
 * arch_ptrace_stop - Do machine-specific work before stopping for ptrace
 * @code:	current->exit_code value ptrace will stop with
 * @info:	siginfo_t pointer (or %NULL) for signal ptrace will stop with
 *
 * This is called with no locks held when arch_ptrace_stop_needed() has
 * just returned nonzero.  It is allowed to block, e.g. for user memory
 * access.  The arch can have machine-specific work to be done before
 * ptrace stops.  On ia64, register backing store gets written back to user
 * memory here.  Since this can be costly (requires dropping the siglock),
 * we only do it when the arch requires it for this particular stop, as
 * indicated by arch_ptrace_stop_needed().
 */
#define arch_ptrace_stop(code, info)		do { } while (0)
#endif

#ifndef arch_ptrace_untrace
/*
 * Do machine-specific work before untracing child.
 *
 * This is called for a normal detach as well as from ptrace_exit()
 * when the tracing task dies.
 *
 * Called with write_lock(&tasklist_lock) held.
 */
#define arch_ptrace_untrace(task)		do { } while (0)
#endif

extern int task_current_syscall(struct task_struct *target, long *callno,
				unsigned long args[6], unsigned int maxargs,
				unsigned long *sp, unsigned long *pc);

#endif

#endif
