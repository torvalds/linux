/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wrapper functions for accessing the file_struct fd array.
 */

#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/posix_types.h>
#include <linux/errno.h>
#include <linux/cleanup.h>
#include <linux/err.h>

struct file;

extern void fput(struct file *);

struct file_operations;
struct task_struct;
struct vfsmount;
struct dentry;
struct inode;
struct path;
extern struct file *alloc_file_pseudo(struct inode *, struct vfsmount *,
	const char *, int flags, const struct file_operations *);
extern struct file *alloc_file_pseudo_noaccount(struct inode *, struct vfsmount *,
	const char *, int flags, const struct file_operations *);
extern struct file *alloc_file_clone(struct file *, int flags,
	const struct file_operations *);

/* either a reference to struct file + flags
 * (cloned vs. borrowed, pos locked), with
 * flags stored in lower bits of value,
 * or empty (represented by 0).
 */
struct fd {
	unsigned long word;
};
#define FDPUT_FPUT       1
#define FDPUT_POS_UNLOCK 2

#define fd_file(f) ((struct file *)((f).word & ~(FDPUT_FPUT|FDPUT_POS_UNLOCK)))
static inline bool fd_empty(struct fd f)
{
	return unlikely(!f.word);
}

#define EMPTY_FD (struct fd){0}
static inline struct fd BORROWED_FD(struct file *f)
{
	return (struct fd){(unsigned long)f};
}
static inline struct fd CLONED_FD(struct file *f)
{
	return (struct fd){(unsigned long)f | FDPUT_FPUT};
}

static inline void fdput(struct fd fd)
{
	if (unlikely(fd.word & FDPUT_FPUT))
		fput(fd_file(fd));
}

extern struct file *fget(unsigned int fd);
extern struct file *fget_raw(unsigned int fd);
extern struct file *fget_task(struct task_struct *task, unsigned int fd);
extern struct file *fget_task_next(struct task_struct *task, unsigned int *fd);
extern void __f_unlock_pos(struct file *);

struct fd fdget(unsigned int fd);
struct fd fdget_raw(unsigned int fd);
struct fd fdget_pos(unsigned int fd);

static inline void fdput_pos(struct fd f)
{
	if (f.word & FDPUT_POS_UNLOCK)
		__f_unlock_pos(fd_file(f));
	fdput(f);
}

DEFINE_CLASS(fd, struct fd, fdput(_T), fdget(fd), int fd)
DEFINE_CLASS(fd_raw, struct fd, fdput(_T), fdget_raw(fd), int fd)
DEFINE_CLASS(fd_pos, struct fd, fdput_pos(_T), fdget_pos(fd), int fd)

extern int f_dupfd(unsigned int from, struct file *file, unsigned flags);
extern int replace_fd(unsigned fd, struct file *file, unsigned flags);
extern void set_close_on_exec(unsigned int fd, int flag);
extern bool get_close_on_exec(unsigned int fd);
extern int __get_unused_fd_flags(unsigned flags, unsigned long nofile);
extern int get_unused_fd_flags(unsigned flags);
extern void put_unused_fd(unsigned int fd);

DEFINE_CLASS(get_unused_fd, int, if (_T >= 0) put_unused_fd(_T),
	     get_unused_fd_flags(flags), unsigned flags)
DEFINE_FREE(fput, struct file *, if (!IS_ERR_OR_NULL(_T)) fput(_T))

/*
 * take_fd() will take care to set @fd to -EBADF ensuring that
 * CLASS(get_unused_fd) won't call put_unused_fd(). This makes it
 * easier to rely on CLASS(get_unused_fd):
 *
 * struct file *f;
 *
 * CLASS(get_unused_fd, fd)(O_CLOEXEC);
 * if (fd < 0)
 *         return fd;
 *
 * f = dentry_open(&path, O_RDONLY, current_cred());
 * if (IS_ERR(f))
 *         return PTR_ERR(f);
 *
 * fd_install(fd, f);
 * return take_fd(fd);
 */
#define take_fd(fd) __get_and_null(fd, -EBADF)

extern void fd_install(unsigned int fd, struct file *file);

int receive_fd(struct file *file, int __user *ufd, unsigned int o_flags);

int receive_fd_replace(int new_fd, struct file *file, unsigned int o_flags);

extern void flush_delayed_fput(void);
extern void __fput_sync(struct file *);

extern unsigned int sysctl_nr_open_min, sysctl_nr_open_max;

/*
 * fd_prepare: Combined fd + file allocation cleanup class.
 * @err: Error code to indicate if allocation succeeded.
 * @__fd: Allocated fd (may not be accessed directly)
 * @__file: Allocated struct file pointer (may not be accessed directly)
 *
 * Allocates an fd and a file together. On error paths, automatically cleans
 * up whichever resource was successfully allocated. Allows flexible file
 * allocation with different functions per usage.
 *
 * Do not use directly.
 */
struct fd_prepare {
	s32 err;
	s32 __fd; /* do not access directly */
	struct file *__file; /* do not access directly */
};

/* Typedef for fd_prepare cleanup guards. */
typedef struct fd_prepare class_fd_prepare_t;

/*
 * Accessors for fd_prepare class members.
 * _Generic() is used for zero-cost type safety.
 */
#define fd_prepare_fd(_fdf) \
	(_Generic((_fdf), struct fd_prepare: (_fdf).__fd))

#define fd_prepare_file(_fdf) \
	(_Generic((_fdf), struct fd_prepare: (_fdf).__file))

/* Do not use directly. */
static inline void class_fd_prepare_destructor(const struct fd_prepare *fdf)
{
	if (unlikely(fdf->__fd >= 0))
		put_unused_fd(fdf->__fd);
	if (unlikely(!IS_ERR_OR_NULL(fdf->__file)))
		fput(fdf->__file);
}

/* Do not use directly. */
static inline int class_fd_prepare_lock_err(const struct fd_prepare *fdf)
{
	if (unlikely(fdf->err))
		return fdf->err;
	if (unlikely(fdf->__fd < 0))
		return fdf->__fd;
	if (unlikely(IS_ERR(fdf->__file)))
		return PTR_ERR(fdf->__file);
	if (unlikely(!fdf->__file))
		return -ENOMEM;
	return 0;
}

/*
 * __FD_PREPARE_INIT - Helper to initialize fd_prepare class.
 * @_fd_flags: flags for get_unused_fd_flags()
 * @_file_owned: expression that returns struct file *
 *
 * Returns a struct fd_prepare with fd, file, and err set.
 * If fd allocation fails, fd will be negative and err will be set. If
 * fd succeeds but file_init_expr fails, file will be ERR_PTR and err
 * will be set. The err field is the single source of truth for error
 * checking.
 */
#define __FD_PREPARE_INIT(_fd_flags, _file_owned)                 \
	({                                                        \
		struct fd_prepare fdf = {                         \
			.__fd = get_unused_fd_flags((_fd_flags)), \
		};                                                \
		if (likely(fdf.__fd >= 0))                        \
			fdf.__file = (_file_owned);               \
		fdf.err = ACQUIRE_ERR(fd_prepare, &fdf);          \
		fdf;                                              \
	})

/*
 * FD_PREPARE - Macro to declare and initialize an fd_prepare variable.
 *
 * Declares and initializes an fd_prepare variable with automatic
 * cleanup. No separate scope required - cleanup happens when variable
 * goes out of scope.
 *
 * @_fdf: name of struct fd_prepare variable to define
 * @_fd_flags: flags for get_unused_fd_flags()
 * @_file_owned: struct file to take ownership of (can be expression)
 */
#define FD_PREPARE(_fdf, _fd_flags, _file_owned) \
	CLASS_INIT(fd_prepare, _fdf, __FD_PREPARE_INIT(_fd_flags, _file_owned))

/*
 * fd_publish - Publish prepared fd and file to the fd table.
 * @_fdf: struct fd_prepare variable
 */
#define fd_publish(_fdf)                                       \
	({                                                     \
		struct fd_prepare *fdp = &(_fdf);              \
		VFS_WARN_ON_ONCE(fdp->err);                    \
		VFS_WARN_ON_ONCE(fdp->__fd < 0);               \
		VFS_WARN_ON_ONCE(IS_ERR_OR_NULL(fdp->__file)); \
		fd_install(fdp->__fd, fdp->__file);            \
		retain_and_null_ptr(fdp->__file);              \
		take_fd(fdp->__fd);                            \
	})

/* Do not use directly. */
#define __FD_ADD(_fdf, _fd_flags, _file_owned)            \
	({                                                \
		FD_PREPARE(_fdf, _fd_flags, _file_owned); \
		s32 ret = _fdf.err;                       \
		if (likely(!ret))                         \
			ret = fd_publish(_fdf);           \
		ret;                                      \
	})

/*
 * FD_ADD - Allocate and install an fd and file in one step.
 * @_fd_flags: flags for get_unused_fd_flags()
 * @_file_owned: struct file to take ownership of
 *
 * Returns the allocated fd number, or negative error code on failure.
 */
#define FD_ADD(_fd_flags, _file_owned) \
	__FD_ADD(__UNIQUE_ID(fd_prepare), _fd_flags, _file_owned)

#endif /* __LINUX_FILE_H */
