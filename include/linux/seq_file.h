/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SEQ_FILE_H
#define _LINUX_SEQ_FILE_H

#include <linux/types.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <linux/mutex.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/fs.h>
#include <linux/cred.h>

struct seq_operations;

struct seq_file {
	char *buf;
	size_t size;
	size_t from;
	size_t count;
	size_t pad_until;
	loff_t index;
	loff_t read_pos;
	u64 version;
	struct mutex lock;
	const struct seq_operations *op;
	int poll_event;
	const struct file *file;
	void *private;
};

struct seq_operations {
	void * (*start) (struct seq_file *m, loff_t *pos);
	void (*stop) (struct seq_file *m, void *v);
	void * (*next) (struct seq_file *m, void *v, loff_t *pos);
	int (*show) (struct seq_file *m, void *v);
};

#define SEQ_SKIP 1

/**
 * seq_has_overflowed - check if the buffer has overflowed
 * @m: the seq_file handle
 *
 * seq_files have a buffer which may overflow. When this happens a larger
 * buffer is reallocated and all the data will be printed again.
 * The overflow state is true when m->count == m->size.
 *
 * Returns true if the buffer received more than it can hold.
 */
static inline bool seq_has_overflowed(struct seq_file *m)
{
	return m->count == m->size;
}

/**
 * seq_get_buf - get buffer to write arbitrary data to
 * @m: the seq_file handle
 * @bufp: the beginning of the buffer is stored here
 *
 * Return the number of bytes available in the buffer, or zero if
 * there's no space.
 */
static inline size_t seq_get_buf(struct seq_file *m, char **bufp)
{
	BUG_ON(m->count > m->size);
	if (m->count < m->size)
		*bufp = m->buf + m->count;
	else
		*bufp = NULL;

	return m->size - m->count;
}

/**
 * seq_commit - commit data to the buffer
 * @m: the seq_file handle
 * @num: the number of bytes to commit
 *
 * Commit @num bytes of data written to a buffer previously acquired
 * by seq_buf_get.  To signal an error condition, or that the data
 * didn't fit in the available space, pass a negative @num value.
 */
static inline void seq_commit(struct seq_file *m, int num)
{
	if (num < 0) {
		m->count = m->size;
	} else {
		BUG_ON(m->count + num > m->size);
		m->count += num;
	}
}

/**
 * seq_setwidth - set padding width
 * @m: the seq_file handle
 * @size: the max number of bytes to pad.
 *
 * Call seq_setwidth() for setting max width, then call seq_printf() etc. and
 * finally call seq_pad() to pad the remaining bytes.
 */
static inline void seq_setwidth(struct seq_file *m, size_t size)
{
	m->pad_until = m->count + size;
}
void seq_pad(struct seq_file *m, char c);

char *mangle_path(char *s, const char *p, const char *esc);
int seq_open(struct file *, const struct seq_operations *);
ssize_t seq_read(struct file *, char __user *, size_t, loff_t *);
loff_t seq_lseek(struct file *, loff_t, int);
int seq_release(struct inode *, struct file *);
int seq_write(struct seq_file *seq, const void *data, size_t len);

__printf(2, 0)
void seq_vprintf(struct seq_file *m, const char *fmt, va_list args);
__printf(2, 3)
void seq_printf(struct seq_file *m, const char *fmt, ...);
void seq_putc(struct seq_file *m, char c);
void seq_puts(struct seq_file *m, const char *s);
void seq_put_decimal_ull_width(struct seq_file *m, const char *delimiter,
			       unsigned long long num, unsigned int width);
void seq_put_decimal_ull(struct seq_file *m, const char *delimiter,
			 unsigned long long num);
void seq_put_decimal_ll(struct seq_file *m, const char *delimiter, long long num);
void seq_put_hex_ll(struct seq_file *m, const char *delimiter,
		    unsigned long long v, unsigned int width);

void seq_escape(struct seq_file *m, const char *s, const char *esc);
void seq_escape_mem_ascii(struct seq_file *m, const char *src, size_t isz);

void seq_hex_dump(struct seq_file *m, const char *prefix_str, int prefix_type,
		  int rowsize, int groupsize, const void *buf, size_t len,
		  bool ascii);

int seq_path(struct seq_file *, const struct path *, const char *);
int seq_file_path(struct seq_file *, struct file *, const char *);
int seq_dentry(struct seq_file *, struct dentry *, const char *);
int seq_path_root(struct seq_file *m, const struct path *path,
		  const struct path *root, const char *esc);

int single_open(struct file *, int (*)(struct seq_file *, void *), void *);
int single_open_size(struct file *, int (*)(struct seq_file *, void *), void *, size_t);
int single_release(struct inode *, struct file *);
void *__seq_open_private(struct file *, const struct seq_operations *, int);
int seq_open_private(struct file *, const struct seq_operations *, int);
int seq_release_private(struct inode *, struct file *);

#define DEFINE_SHOW_ATTRIBUTE(__name)					\
static int __name ## _open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, __name ## _show, inode->i_private);	\
}									\
									\
static const struct file_operations __name ## _fops = {			\
	.owner		= THIS_MODULE,					\
	.open		= __name ## _open,				\
	.read		= seq_read,					\
	.llseek		= seq_lseek,					\
	.release	= single_release,				\
}

static inline struct user_namespace *seq_user_ns(struct seq_file *seq)
{
#ifdef CONFIG_USER_NS
	return seq->file->f_cred->user_ns;
#else
	extern struct user_namespace init_user_ns;
	return &init_user_ns;
#endif
}

/**
 * seq_show_options - display mount options with appropriate escapes.
 * @m: the seq_file handle
 * @name: the mount option name
 * @value: the mount option name's value, can be NULL
 */
static inline void seq_show_option(struct seq_file *m, const char *name,
				   const char *value)
{
	seq_putc(m, ',');
	seq_escape(m, name, ",= \t\n\\");
	if (value) {
		seq_putc(m, '=');
		seq_escape(m, value, ", \t\n\\");
	}
}

/**
 * seq_show_option_n - display mount options with appropriate escapes
 *		       where @value must be a specific length.
 * @m: the seq_file handle
 * @name: the mount option name
 * @value: the mount option name's value, cannot be NULL
 * @length: the length of @value to display
 *
 * This is a macro since this uses "length" to define the size of the
 * stack buffer.
 */
#define seq_show_option_n(m, name, value, length) {	\
	char val_buf[length + 1];			\
	strncpy(val_buf, value, length);		\
	val_buf[length] = '\0';				\
	seq_show_option(m, name, val_buf);		\
}

#define SEQ_START_TOKEN ((void *)1)
/*
 * Helpers for iteration over list_head-s in seq_files
 */

extern struct list_head *seq_list_start(struct list_head *head,
		loff_t pos);
extern struct list_head *seq_list_start_head(struct list_head *head,
		loff_t pos);
extern struct list_head *seq_list_next(void *v, struct list_head *head,
		loff_t *ppos);

/*
 * Helpers for iteration over hlist_head-s in seq_files
 */

extern struct hlist_node *seq_hlist_start(struct hlist_head *head,
					  loff_t pos);
extern struct hlist_node *seq_hlist_start_head(struct hlist_head *head,
					       loff_t pos);
extern struct hlist_node *seq_hlist_next(void *v, struct hlist_head *head,
					 loff_t *ppos);

extern struct hlist_node *seq_hlist_start_rcu(struct hlist_head *head,
					      loff_t pos);
extern struct hlist_node *seq_hlist_start_head_rcu(struct hlist_head *head,
						   loff_t pos);
extern struct hlist_node *seq_hlist_next_rcu(void *v,
						   struct hlist_head *head,
						   loff_t *ppos);

/* Helpers for iterating over per-cpu hlist_head-s in seq_files */
extern struct hlist_node *seq_hlist_start_percpu(struct hlist_head __percpu *head, int *cpu, loff_t pos);

extern struct hlist_node *seq_hlist_next_percpu(void *v, struct hlist_head __percpu *head, int *cpu, loff_t *pos);

void seq_file_init(void);
#endif
