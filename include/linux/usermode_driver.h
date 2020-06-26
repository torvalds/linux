#ifndef __LINUX_USERMODE_DRIVER_H__
#define __LINUX_USERMODE_DRIVER_H__

#include <linux/umh.h>

#ifdef CONFIG_BPFILTER
void __exit_umh(struct task_struct *tsk);

static inline void exit_umh(struct task_struct *tsk)
{
	if (unlikely(tsk->flags & PF_UMH))
		__exit_umh(tsk);
}
#else
static inline void exit_umh(struct task_struct *tsk)
{
}
#endif

struct umd_info {
	const char *cmdline;
	struct file *pipe_to_umh;
	struct file *pipe_from_umh;
	struct list_head list;
	void (*cleanup)(struct umd_info *info);
	pid_t pid;
};
int fork_usermode_blob(void *data, size_t len, struct umd_info *info);

#endif /* __LINUX_USERMODE_DRIVER_H__ */
