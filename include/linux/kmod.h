#ifndef __LINUX_KMOD_H__
#define __LINUX_KMOD_H__

/*
 *	include/linux/kmod.h
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/gfp.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/workqueue.h>
#include <linux/sysctl.h>

#define KMOD_PATH_LEN 256

#ifdef CONFIG_MODULES
extern char modprobe_path[]; /* for sysctl */
/* modprobe exit status on success, -ve on error.  Return value
 * usually useless though. */
extern __printf(2, 3)
int __request_module(bool wait, const char *name, ...);
#define request_module(mod...) __request_module(true, mod)
#define request_module_nowait(mod...) __request_module(false, mod)
#define try_then_request_module(x, mod...) \
	((x) ?: (__request_module(true, mod), (x)))
#else
static inline int request_module(const char *name, ...) { return -ENOSYS; }
static inline int request_module_nowait(const char *name, ...) { return -ENOSYS; }
#define try_then_request_module(x, mod...) (x)
#endif


struct cred;
struct file;

#define UMH_NO_WAIT	0	/* don't wait at all */
#define UMH_WAIT_EXEC	1	/* wait for the exec, but not the process */
#define UMH_WAIT_PROC	2	/* wait for the process to complete */
#define UMH_KILLABLE	4	/* wait for EXEC/PROC killable */

struct subprocess_info {
	struct work_struct work;
	struct completion *complete;
	char *path;
	char **argv;
	char **envp;
	int wait;
	int retval;
	int (*init)(struct subprocess_info *info, struct cred *new);
	void (*cleanup)(struct subprocess_info *info);
	void *data;
};

extern int
call_usermodehelper(char *path, char **argv, char **envp, int wait);

extern struct subprocess_info *
call_usermodehelper_setup(char *path, char **argv, char **envp, gfp_t gfp_mask,
			  int (*init)(struct subprocess_info *info, struct cred *new),
			  void (*cleanup)(struct subprocess_info *), void *data);

extern int
call_usermodehelper_exec(struct subprocess_info *info, int wait);

extern struct ctl_table usermodehelper_table[];

enum umh_disable_depth {
	UMH_ENABLED = 0,
	UMH_FREEZING,
	UMH_DISABLED,
};

extern void usermodehelper_init(void);

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

#endif /* __LINUX_KMOD_H__ */
