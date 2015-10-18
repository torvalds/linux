/* 
 * medusa/l1/process_handlers.h
 *
 * prototypes of L2 process related handlers called from L1 hooks
 *
 */

#ifndef _MEDUSA_L1_PROCESS_HANDLERS_H
#define _MEDUSA_L1_PROCESS_HANDLERS_H

#include <linux/medusa/l3/constants.h>
//#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/signal.h>

extern medusa_answer_t medusa_setresuid(uid_t ruid, uid_t euid, uid_t suid);
extern medusa_answer_t medusa_capable(int cap);
extern medusa_answer_t medusa_fork(struct task_struct *new,
		unsigned long clone_flags);
extern medusa_answer_t medusa_init_process(struct task_struct *new);
extern medusa_answer_t medusa_sendsig(int sig, struct siginfo *info,
		struct task_struct *p); 
extern medusa_answer_t medusa_afterexec(char *filename, char **argv,
		char **envp);
extern int medusa_monitored_pexec(void);
extern void medusa_monitor_pexec(int flag);
extern int medusa_monitored_afterexec(void);
extern void medusa_monitor_afterexec(int flag);
extern medusa_answer_t medusa_sexec(struct linux_binprm * bprm);
extern medusa_answer_t medusa_ptrace(struct task_struct * tracer,
		struct task_struct * tracee);
extern void medusa_kernel_thread(int (*fn) (void *));

extern int process_kobj_validate_task(struct task_struct * ts);

#endif /* _MEDUSA_L1_PROCESS_HANDLERS_H */

