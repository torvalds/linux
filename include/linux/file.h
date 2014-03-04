/*
 * Wrapper functions for accessing the file_struct fd array.
 */

#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/posix_types.h>

struct file;

extern void fput(struct file *);

struct file_operations;
struct vfsmount;
struct dentry;
struct path;
extern struct file *alloc_file(struct path *, fmode_t mode,
	const struct file_operations *fop);

static inline void fput_light(struct file *file, int fput_needed)
{
	if (fput_needed)
		fput(file);
}

struct fd {
	struct file *file;
	unsigned int flags;
};
#define FDPUT_FPUT       1
#define FDPUT_POS_UNLOCK 2

static inline void fdput(struct fd fd)
{
	if (fd.flags & FDPUT_FPUT)
		fput(fd.file);
}

extern struct file *fget(unsigned int fd);
extern struct file *fget_raw(unsigned int fd);
extern unsigned long __fdget(unsigned int fd);
extern unsigned long __fdget_raw(unsigned int fd);
extern unsigned long __fdget_pos(unsigned int fd);

static inline struct fd __to_fd(unsigned long v)
{
	return (struct fd){(struct file *)(v & ~3),v & 3};
}

static inline struct fd fdget(unsigned int fd)
{
	return __to_fd(__fdget(fd));
}

static inline struct fd fdget_raw(unsigned int fd)
{
	return __to_fd(__fdget_raw(fd));
}

extern int f_dupfd(unsigned int from, struct file *file, unsigned flags);
extern int replace_fd(unsigned fd, struct file *file, unsigned flags);
extern void set_close_on_exec(unsigned int fd, int flag);
extern bool get_close_on_exec(unsigned int fd);
extern void put_filp(struct file *);
extern int get_unused_fd_flags(unsigned flags);
#define get_unused_fd() get_unused_fd_flags(0)
extern void put_unused_fd(unsigned int fd);

extern void fd_install(unsigned int fd, struct file *file);

extern void flush_delayed_fput(void);
extern void __fput_sync(struct file *);

#endif /* __LINUX_FILE_H */
