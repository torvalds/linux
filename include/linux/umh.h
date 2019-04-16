#ifndef __LINUX_UMH_H__
#define __LINUX_UMH_H__

#include <linux/gfp.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/workqueue.h>
#include <linux/sysctl.h>

struct cred;
struct file;

#define UMH_NO_WAIT	0	/* don't wait at all */
#define UMH_WAIT_EXEC	1	/* wait for the exec, but not the process */
#define UMH_WAIT_PROC	2	/* wait for the process to complete */
#define UMH_KILLABLE	4	/* wait for EXEC/PROC killable */

struct subprocess_info {
	struct work_struct work;
	struct completion *complete;
	const char *path;
	char **argv;
	char **envp;
	struct file *file;
	int wait;
	int retval;
	pid_t pid;
	int (*init)(struct subprocess_info *info, struct cred *new);
	void (*cleanup)(struct subprocess_info *info);
	void *data;
} __randomize_layout;

extern int
call_usermodehelper(const char *path, char **argv, char **envp, int wait);

extern struct subprocess_info *
call_usermodehelper_setup(const char *path, char **argv, char **envp,
			  gfp_t gfp_mask,
			  int (*init)(struct subprocess_info *info, struct cred *new),
			  void (*cleanup)(struct subprocess_info *), void *data);

struct subprocess_info *call_usermodehelper_setup_file(struct file *file,
			  int (*init)(struct subprocess_info *info, struct cred *new),
			  void (*cleanup)(struct subprocess_info *), void *data);
struct umh_info {
	const char *cmdline;
	struct file *pipe_to_umh;
	struct file *pipe_from_umh;
	struct list_head list;
	void (*cleanup)(struct umh_info *info);
	pid_t pid;
};
int fork_usermode_blob(void *data, size_t len, struct umh_info *info);

extern int
call_usermodehelper_exec(struct subprocess_info *info, int wait);

extern struct ctl_table usermodehelper_table[];

enum umh_disable_depth {
	UMH_ENABLED = 0,
	UMH_FREEZING,
	UMH_DISABLED,
};

extern int __usermodehelper_disable(enum umh_disable_depth depth);
extern void __usermodehelper_set_disable_depth(enum umh_disable_depth depth);

static inline int usermodehelper_disable(void)
{
	return __usermodehelper_disable(UMH_DISABLED);
}

static inline void usermodehelper_enable(void)
{
	__usermodehelper_set_disable_depth(UMH_ENABLED);
}

extern int usermodehelper_read_trylock(void);
extern long usermodehelper_read_lock_wait(long timeout);
extern void usermodehelper_read_unlock(void);

#endif /* __LINUX_UMH_H__ */
