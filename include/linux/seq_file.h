#ifndef _LINUX_SEQ_FILE_H
#define _LINUX_SEQ_FILE_H
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/string.h>
#include <asm/semaphore.h>

struct seq_operations;
struct file;
struct vfsmount;
struct dentry;
struct inode;

struct seq_file {
	char *buf;
	size_t size;
	size_t from;
	size_t count;
	loff_t index;
	loff_t version;
	struct semaphore sem;
	struct seq_operations *op;
	void *private;
};

struct seq_operations {
	void * (*start) (struct seq_file *m, loff_t *pos);
	void (*stop) (struct seq_file *m, void *v);
	void * (*next) (struct seq_file *m, void *v, loff_t *pos);
	int (*show) (struct seq_file *m, void *v);
};

int seq_open(struct file *, struct seq_operations *);
ssize_t seq_read(struct file *, char __user *, size_t, loff_t *);
loff_t seq_lseek(struct file *, loff_t, int);
int seq_release(struct inode *, struct file *);
int seq_escape(struct seq_file *, const char *, const char *);
int seq_putc(struct seq_file *m, char c);
int seq_puts(struct seq_file *m, const char *s);

int seq_printf(struct seq_file *, const char *, ...)
	__attribute__ ((format (printf,2,3)));

int seq_path(struct seq_file *, struct vfsmount *, struct dentry *, char *);

int single_open(struct file *, int (*)(struct seq_file *, void *), void *);
int single_release(struct inode *, struct file *);
int seq_release_private(struct inode *, struct file *);

#define SEQ_START_TOKEN ((void *)1)

#endif
#endif
