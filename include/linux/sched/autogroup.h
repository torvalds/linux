#ifndef _LINUX_SCHED_AUTOGROUP_H
#define _LINUX_SCHED_AUTOGROUP_H

#include <linux/sched.h>

struct signal_struct;
struct task_group;
struct seq_file;

#ifdef CONFIG_SCHED_AUTOGROUP
extern void sched_autogroup_create_attach(struct task_struct *p);
extern void sched_autogroup_detach(struct task_struct *p);
extern void sched_autogroup_fork(struct signal_struct *sig);
extern void sched_autogroup_exit(struct signal_struct *sig);
extern void sched_autogroup_exit_task(struct task_struct *p);
#ifdef CONFIG_PROC_FS
extern void proc_sched_autogroup_show_task(struct task_struct *p, struct seq_file *m);
extern int proc_sched_autogroup_set_nice(struct task_struct *p, int nice);
#endif
#else
static inline void sched_autogroup_create_attach(struct task_struct *p) { }
static inline void sched_autogroup_detach(struct task_struct *p) { }
static inline void sched_autogroup_fork(struct signal_struct *sig) { }
static inline void sched_autogroup_exit(struct signal_struct *sig) { }
static inline void sched_autogroup_exit_task(struct task_struct *p) { }
#endif

#ifdef CONFIG_CGROUP_SCHED
extern struct task_group root_task_group;
#endif /* CONFIG_CGROUP_SCHED */

#endif /* _LINUX_SCHED_AUTOGROUP_H */
