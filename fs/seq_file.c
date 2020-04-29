// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/seq_file.c
 *
 * helper functions for making synthetic files from sequences of records.
 * initial implementation -- AV, Oct 2001.
 */

#include <linux/cache.h>
#include <linux/fs.h>
#include <linux/export.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/string_helpers.h>

#include <linux/uaccess.h>
#include <asm/page.h>

static struct kmem_cache *seq_file_cache __ro_after_init;

static void seq_set_overflow(struct seq_file *m)
{
	m->count = m->size;
}

static void *seq_buf_alloc(unsigned long size)
{
	return kvmalloc(size, GFP_KERNEL_ACCOUNT);
}

/**
 *	seq_open -	initialize sequential file
 *	@file: file we initialize
 *	@op: method table describing the sequence
 *
 *	seq_open() sets @file, associating it with a sequence described
 *	by @op.  @op->start() sets the iterator up and returns the first
 *	element of sequence. @op->stop() shuts it down.  @op->next()
 *	returns the next element of sequence.  @op->show() prints element
 *	into the buffer.  In case of error ->start() and ->next() return
 *	ERR_PTR(error).  In the end of sequence they return %NULL. ->show()
 *	returns 0 in case of success and negative number in case of error.
 *	Returning SEQ_SKIP means "discard this element and move on".
 *	Note: seq_open() will allocate a struct seq_file and store its
 *	pointer in @file->private_data. This pointer should not be modified.
 */
int seq_open(struct file *file, const struct seq_operations *op)
{
	struct seq_file *p;

	WARN_ON(file->private_data);

	p = kmem_cache_zalloc(seq_file_cache, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	file->private_data = p;

	mutex_init(&p->lock);
	p->op = op;

	// No refcounting: the lifetime of 'p' is constrained
	// to the lifetime of the file.
	p->file = file;

	/*
	 * seq_files support lseek() and pread().  They do not implement
	 * write() at all, but we clear FMODE_PWRITE here for historical
	 * reasons.
	 *
	 * If a client of seq_files a) implements file.write() and b) wishes to
	 * support pwrite() then that client will need to implement its own
	 * file.open() which calls seq_open() and then sets FMODE_PWRITE.
	 */
	file->f_mode &= ~FMODE_PWRITE;
	return 0;
}
EXPORT_SYMBOL(seq_open);

static int traverse(struct seq_file *m, loff_t offset)
{
	loff_t pos = 0;
	int error = 0;
	void *p;

	m->index = 0;
	m->count = m->from = 0;
	if (!offset)
		return 0;

	if (!m->buf) {
		m->buf = seq_buf_alloc(m->size = PAGE_SIZE);
		if (!m->buf)
			return -ENOMEM;
	}
	p = m->op->start(m, &m->index);
	while (p) {
		error = PTR_ERR(p);
		if (IS_ERR(p))
			break;
		error = m->op->show(m, p);
		if (error < 0)
			break;
		if (unlikely(error)) {
			error = 0;
			m->count = 0;
		}
		if (seq_has_overflowed(m))
			goto Eoverflow;
		p = m->op->next(m, p, &m->index);
		if (pos + m->count > offset) {
			m->from = offset - pos;
			m->count -= m->from;
			break;
		}
		pos += m->count;
		m->count = 0;
		if (pos == offset)
			break;
	}
	m->op->stop(m, p);
	return error;

Eoverflow:
	m->op->stop(m, p);
	kvfree(m->buf);
	m->count = 0;
	m->buf = seq_buf_alloc(m->size <<= 1);
	return !m->buf ? -ENOMEM : -EAGAIN;
}

/**
 *	seq_read -	->read() method for sequential files.
 *	@file: the file to read from
 *	@buf: the buffer to read to
 *	@size: the maximum number of bytes to read
 *	@ppos: the current position in the file
 *
 *	Ready-made ->f_op->read()
 */
ssize_t seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	size_t copied = 0;
	size_t n;
	void *p;
	int err = 0;

	mutex_lock(&m->lock);

	/*
	 * if request is to read from zero offset, reset iterator to first
	 * record as it might have been already advanced by previous requests
	 */
	if (*ppos == 0) {
		m->index = 0;
		m->count = 0;
	}

	/* Don't assume *ppos is where we left it */
	if (unlikely(*ppos != m->read_pos)) {
		while ((err = traverse(m, *ppos)) == -EAGAIN)
			;
		if (err) {
			/* With prejudice... */
			m->read_pos = 0;
			m->index = 0;
			m->count = 0;
			goto Done;
		} else {
			m->read_pos = *ppos;
		}
	}

	/* grab buffer if we didn't have one */
	if (!m->buf) {
		m->buf = seq_buf_alloc(m->size = PAGE_SIZE);
		if (!m->buf)
			goto Enomem;
	}
	/* if not empty - flush it first */
	if (m->count) {
		n = min(m->count, size);
		err = copy_to_user(buf, m->buf + m->from, n);
		if (err)
			goto Efault;
		m->count -= n;
		m->from += n;
		size -= n;
		buf += n;
		copied += n;
		if (!size)
			goto Done;
	}
	/* we need at least one record in buffer */
	m->from = 0;
	p = m->op->start(m, &m->index);
	while (1) {
		err = PTR_ERR(p);
		if (!p || IS_ERR(p))
			break;
		err = m->op->show(m, p);
		if (err < 0)
			break;
		if (unlikely(err))
			m->count = 0;
		if (unlikely(!m->count)) {
			p = m->op->next(m, p, &m->index);
			continue;
		}
		if (m->count < m->size)
			goto Fill;
		m->op->stop(m, p);
		kvfree(m->buf);
		m->count = 0;
		m->buf = seq_buf_alloc(m->size <<= 1);
		if (!m->buf)
			goto Enomem;
		p = m->op->start(m, &m->index);
	}
	m->op->stop(m, p);
	m->count = 0;
	goto Done;
Fill:
	/* they want more? let's try to get some more */
	while (1) {
		size_t offs = m->count;
		loff_t pos = m->index;

		p = m->op->next(m, p, &m->index);
		if (pos == m->index) {
			pr_info_ratelimited("buggy seq_file .next function %ps "
				"did not updated position index\n",
				m->op->next);
			m->index++;
		}
		if (!p || IS_ERR(p)) {
			err = PTR_ERR(p);
			break;
		}
		if (m->count >= size)
			break;
		err = m->op->show(m, p);
		if (seq_has_overflowed(m) || err) {
			m->count = offs;
			if (likely(err <= 0))
				break;
		}
	}
	m->op->stop(m, p);
	n = min(m->count, size);
	err = copy_to_user(buf, m->buf, n);
	if (err)
		goto Efault;
	copied += n;
	m->count -= n;
	m->from = n;
Done:
	if (!copied)
		copied = err;
	else {
		*ppos += copied;
		m->read_pos += copied;
	}
	mutex_unlock(&m->lock);
	return copied;
Enomem:
	err = -ENOMEM;
	goto Done;
Efault:
	err = -EFAULT;
	goto Done;
}
EXPORT_SYMBOL(seq_read);

/**
 *	seq_lseek -	->llseek() method for sequential files.
 *	@file: the file in question
 *	@offset: new position
 *	@whence: 0 for absolute, 1 for relative position
 *
 *	Ready-made ->f_op->llseek()
 */
loff_t seq_lseek(struct file *file, loff_t offset, int whence)
{
	struct seq_file *m = file->private_data;
	loff_t retval = -EINVAL;

	mutex_lock(&m->lock);
	switch (whence) {
	case SEEK_CUR:
		offset += file->f_pos;
		/* fall through */
	case SEEK_SET:
		if (offset < 0)
			break;
		retval = offset;
		if (offset != m->read_pos) {
			while ((retval = traverse(m, offset)) == -EAGAIN)
				;
			if (retval) {
				/* with extreme prejudice... */
				file->f_pos = 0;
				m->read_pos = 0;
				m->index = 0;
				m->count = 0;
			} else {
				m->read_pos = offset;
				retval = file->f_pos = offset;
			}
		} else {
			file->f_pos = offset;
		}
	}
	mutex_unlock(&m->lock);
	return retval;
}
EXPORT_SYMBOL(seq_lseek);

/**
 *	seq_release -	free the structures associated with sequential file.
 *	@file: file in question
 *	@inode: its inode
 *
 *	Frees the structures associated with sequential file; can be used
 *	as ->f_op->release() if you don't have private data to destroy.
 */
int seq_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	kvfree(m->buf);
	kmem_cache_free(seq_file_cache, m);
	return 0;
}
EXPORT_SYMBOL(seq_release);

/**
 *	seq_escape -	print string into buffer, escaping some characters
 *	@m:	target buffer
 *	@s:	string
 *	@esc:	set of characters that need escaping
 *
 *	Puts string into buffer, replacing each occurrence of character from
 *	@esc with usual octal escape.
 *	Use seq_has_overflowed() to check for errors.
 */
void seq_escape(struct seq_file *m, const char *s, const char *esc)
{
	char *buf;
	size_t size = seq_get_buf(m, &buf);
	int ret;

	ret = string_escape_str(s, buf, size, ESCAPE_OCTAL, esc);
	seq_commit(m, ret < size ? ret : -1);
}
EXPORT_SYMBOL(seq_escape);

void seq_escape_mem_ascii(struct seq_file *m, const char *src, size_t isz)
{
	char *buf;
	size_t size = seq_get_buf(m, &buf);
	int ret;

	ret = string_escape_mem_ascii(src, isz, buf, size);
	seq_commit(m, ret < size ? ret : -1);
}
EXPORT_SYMBOL(seq_escape_mem_ascii);

void seq_vprintf(struct seq_file *m, const char *f, va_list args)
{
	int len;

	if (m->count < m->size) {
		len = vsnprintf(m->buf + m->count, m->size - m->count, f, args);
		if (m->count + len < m->size) {
			m->count += len;
			return;
		}
	}
	seq_set_overflow(m);
}
EXPORT_SYMBOL(seq_vprintf);

void seq_printf(struct seq_file *m, const char *f, ...)
{
	va_list args;

	va_start(args, f);
	seq_vprintf(m, f, args);
	va_end(args);
}
EXPORT_SYMBOL(seq_printf);

/**
 *	mangle_path -	mangle and copy path to buffer beginning
 *	@s: buffer start
 *	@p: beginning of path in above buffer
 *	@esc: set of characters that need escaping
 *
 *      Copy the path from @p to @s, replacing each occurrence of character from
 *      @esc with usual octal escape.
 *      Returns pointer past last written character in @s, or NULL in case of
 *      failure.
 */
char *mangle_path(char *s, const char *p, const char *esc)
{
	while (s <= p) {
		char c = *p++;
		if (!c) {
			return s;
		} else if (!strchr(esc, c)) {
			*s++ = c;
		} else if (s + 4 > p) {
			break;
		} else {
			*s++ = '\\';
			*s++ = '0' + ((c & 0300) >> 6);
			*s++ = '0' + ((c & 070) >> 3);
			*s++ = '0' + (c & 07);
		}
	}
	return NULL;
}
EXPORT_SYMBOL(mangle_path);

/**
 * seq_path - seq_file interface to print a pathname
 * @m: the seq_file handle
 * @path: the struct path to print
 * @esc: set of characters to escape in the output
 *
 * return the absolute path of 'path', as represented by the
 * dentry / mnt pair in the path parameter.
 */
int seq_path(struct seq_file *m, const struct path *path, const char *esc)
{
	char *buf;
	size_t size = seq_get_buf(m, &buf);
	int res = -1;

	if (size) {
		char *p = d_path(path, buf, size);
		if (!IS_ERR(p)) {
			char *end = mangle_path(buf, p, esc);
			if (end)
				res = end - buf;
		}
	}
	seq_commit(m, res);

	return res;
}
EXPORT_SYMBOL(seq_path);

/**
 * seq_file_path - seq_file interface to print a pathname of a file
 * @m: the seq_file handle
 * @file: the struct file to print
 * @esc: set of characters to escape in the output
 *
 * return the absolute path to the file.
 */
int seq_file_path(struct seq_file *m, struct file *file, const char *esc)
{
	return seq_path(m, &file->f_path, esc);
}
EXPORT_SYMBOL(seq_file_path);

/*
 * Same as seq_path, but relative to supplied root.
 */
int seq_path_root(struct seq_file *m, const struct path *path,
		  const struct path *root, const char *esc)
{
	char *buf;
	size_t size = seq_get_buf(m, &buf);
	int res = -ENAMETOOLONG;

	if (size) {
		char *p;

		p = __d_path(path, root, buf, size);
		if (!p)
			return SEQ_SKIP;
		res = PTR_ERR(p);
		if (!IS_ERR(p)) {
			char *end = mangle_path(buf, p, esc);
			if (end)
				res = end - buf;
			else
				res = -ENAMETOOLONG;
		}
	}
	seq_commit(m, res);

	return res < 0 && res != -ENAMETOOLONG ? res : 0;
}

/*
 * returns the path of the 'dentry' from the root of its filesystem.
 */
int seq_dentry(struct seq_file *m, struct dentry *dentry, const char *esc)
{
	char *buf;
	size_t size = seq_get_buf(m, &buf);
	int res = -1;

	if (size) {
		char *p = dentry_path(dentry, buf, size);
		if (!IS_ERR(p)) {
			char *end = mangle_path(buf, p, esc);
			if (end)
				res = end - buf;
		}
	}
	seq_commit(m, res);

	return res;
}
EXPORT_SYMBOL(seq_dentry);

static void *single_start(struct seq_file *p, loff_t *pos)
{
	return NULL + (*pos == 0);
}

static void *single_next(struct seq_file *p, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void single_stop(struct seq_file *p, void *v)
{
}

int single_open(struct file *file, int (*show)(struct seq_file *, void *),
		void *data)
{
	struct seq_operations *op = kmalloc(sizeof(*op), GFP_KERNEL_ACCOUNT);
	int res = -ENOMEM;

	if (op) {
		op->start = single_start;
		op->next = single_next;
		op->stop = single_stop;
		op->show = show;
		res = seq_open(file, op);
		if (!res)
			((struct seq_file *)file->private_data)->private = data;
		else
			kfree(op);
	}
	return res;
}
EXPORT_SYMBOL(single_open);

int single_open_size(struct file *file, int (*show)(struct seq_file *, void *),
		void *data, size_t size)
{
	char *buf = seq_buf_alloc(size);
	int ret;
	if (!buf)
		return -ENOMEM;
	ret = single_open(file, show, data);
	if (ret) {
		kvfree(buf);
		return ret;
	}
	((struct seq_file *)file->private_data)->buf = buf;
	((struct seq_file *)file->private_data)->size = size;
	return 0;
}
EXPORT_SYMBOL(single_open_size);

int single_release(struct inode *inode, struct file *file)
{
	const struct seq_operations *op = ((struct seq_file *)file->private_data)->op;
	int res = seq_release(inode, file);
	kfree(op);
	return res;
}
EXPORT_SYMBOL(single_release);

int seq_release_private(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;

	kfree(seq->private);
	seq->private = NULL;
	return seq_release(inode, file);
}
EXPORT_SYMBOL(seq_release_private);

void *__seq_open_private(struct file *f, const struct seq_operations *ops,
		int psize)
{
	int rc;
	void *private;
	struct seq_file *seq;

	private = kzalloc(psize, GFP_KERNEL_ACCOUNT);
	if (private == NULL)
		goto out;

	rc = seq_open(f, ops);
	if (rc < 0)
		goto out_free;

	seq = f->private_data;
	seq->private = private;
	return private;

out_free:
	kfree(private);
out:
	return NULL;
}
EXPORT_SYMBOL(__seq_open_private);

int seq_open_private(struct file *filp, const struct seq_operations *ops,
		int psize)
{
	return __seq_open_private(filp, ops, psize) ? 0 : -ENOMEM;
}
EXPORT_SYMBOL(seq_open_private);

void seq_putc(struct seq_file *m, char c)
{
	if (m->count >= m->size)
		return;

	m->buf[m->count++] = c;
}
EXPORT_SYMBOL(seq_putc);

void seq_puts(struct seq_file *m, const char *s)
{
	int len = strlen(s);

	if (m->count + len >= m->size) {
		seq_set_overflow(m);
		return;
	}
	memcpy(m->buf + m->count, s, len);
	m->count += len;
}
EXPORT_SYMBOL(seq_puts);

/**
 * A helper routine for putting decimal numbers without rich format of printf().
 * only 'unsigned long long' is supported.
 * @m: seq_file identifying the buffer to which data should be written
 * @delimiter: a string which is printed before the number
 * @num: the number
 * @width: a minimum field width
 *
 * This routine will put strlen(delimiter) + number into seq_filed.
 * This routine is very quick when you show lots of numbers.
 * In usual cases, it will be better to use seq_printf(). It's easier to read.
 */
void seq_put_decimal_ull_width(struct seq_file *m, const char *delimiter,
			 unsigned long long num, unsigned int width)
{
	int len;

	if (m->count + 2 >= m->size) /* we'll write 2 bytes at least */
		goto overflow;

	if (delimiter && delimiter[0]) {
		if (delimiter[1] == 0)
			seq_putc(m, delimiter[0]);
		else
			seq_puts(m, delimiter);
	}

	if (!width)
		width = 1;

	if (m->count + width >= m->size)
		goto overflow;

	len = num_to_str(m->buf + m->count, m->size - m->count, num, width);
	if (!len)
		goto overflow;

	m->count += len;
	return;

overflow:
	seq_set_overflow(m);
}

void seq_put_decimal_ull(struct seq_file *m, const char *delimiter,
			 unsigned long long num)
{
	return seq_put_decimal_ull_width(m, delimiter, num, 0);
}
EXPORT_SYMBOL(seq_put_decimal_ull);

/**
 * seq_put_hex_ll - put a number in hexadecimal notation
 * @m: seq_file identifying the buffer to which data should be written
 * @delimiter: a string which is printed before the number
 * @v: the number
 * @width: a minimum field width
 *
 * seq_put_hex_ll(m, "", v, 8) is equal to seq_printf(m, "%08llx", v)
 *
 * This routine is very quick when you show lots of numbers.
 * In usual cases, it will be better to use seq_printf(). It's easier to read.
 */
void seq_put_hex_ll(struct seq_file *m, const char *delimiter,
				unsigned long long v, unsigned int width)
{
	unsigned int len;
	int i;

	if (delimiter && delimiter[0]) {
		if (delimiter[1] == 0)
			seq_putc(m, delimiter[0]);
		else
			seq_puts(m, delimiter);
	}

	/* If x is 0, the result of __builtin_clzll is undefined */
	if (v == 0)
		len = 1;
	else
		len = (sizeof(v) * 8 - __builtin_clzll(v) + 3) / 4;

	if (len < width)
		len = width;

	if (m->count + len > m->size) {
		seq_set_overflow(m);
		return;
	}

	for (i = len - 1; i >= 0; i--) {
		m->buf[m->count + i] = hex_asc[0xf & v];
		v = v >> 4;
	}
	m->count += len;
}

void seq_put_decimal_ll(struct seq_file *m, const char *delimiter, long long num)
{
	int len;

	if (m->count + 3 >= m->size) /* we'll write 2 bytes at least */
		goto overflow;

	if (delimiter && delimiter[0]) {
		if (delimiter[1] == 0)
			seq_putc(m, delimiter[0]);
		else
			seq_puts(m, delimiter);
	}

	if (m->count + 2 >= m->size)
		goto overflow;

	if (num < 0) {
		m->buf[m->count++] = '-';
		num = -num;
	}

	if (num < 10) {
		m->buf[m->count++] = num + '0';
		return;
	}

	len = num_to_str(m->buf + m->count, m->size - m->count, num, 0);
	if (!len)
		goto overflow;

	m->count += len;
	return;

overflow:
	seq_set_overflow(m);
}
EXPORT_SYMBOL(seq_put_decimal_ll);

/**
 * seq_write - write arbitrary data to buffer
 * @seq: seq_file identifying the buffer to which data should be written
 * @data: data address
 * @len: number of bytes
 *
 * Return 0 on success, non-zero otherwise.
 */
int seq_write(struct seq_file *seq, const void *data, size_t len)
{
	if (seq->count + len < seq->size) {
		memcpy(seq->buf + seq->count, data, len);
		seq->count += len;
		return 0;
	}
	seq_set_overflow(seq);
	return -1;
}
EXPORT_SYMBOL(seq_write);

/**
 * seq_pad - write padding spaces to buffer
 * @m: seq_file identifying the buffer to which data should be written
 * @c: the byte to append after padding if non-zero
 */
void seq_pad(struct seq_file *m, char c)
{
	int size = m->pad_until - m->count;
	if (size > 0) {
		if (size + m->count > m->size) {
			seq_set_overflow(m);
			return;
		}
		memset(m->buf + m->count, ' ', size);
		m->count += size;
	}
	if (c)
		seq_putc(m, c);
}
EXPORT_SYMBOL(seq_pad);

/* A complete analogue of print_hex_dump() */
void seq_hex_dump(struct seq_file *m, const char *prefix_str, int prefix_type,
		  int rowsize, int groupsize, const void *buf, size_t len,
		  bool ascii)
{
	const u8 *ptr = buf;
	int i, linelen, remaining = len;
	char *buffer;
	size_t size;
	int ret;

	if (rowsize != 16 && rowsize != 32)
		rowsize = 16;

	for (i = 0; i < len && !seq_has_overflowed(m); i += rowsize) {
		linelen = min(remaining, rowsize);
		remaining -= rowsize;

		switch (prefix_type) {
		case DUMP_PREFIX_ADDRESS:
			seq_printf(m, "%s%p: ", prefix_str, ptr + i);
			break;
		case DUMP_PREFIX_OFFSET:
			seq_printf(m, "%s%.8x: ", prefix_str, i);
			break;
		default:
			seq_printf(m, "%s", prefix_str);
			break;
		}

		size = seq_get_buf(m, &buffer);
		ret = hex_dump_to_buffer(ptr + i, linelen, rowsize, groupsize,
					 buffer, size, ascii);
		seq_commit(m, ret < size ? ret : -1);

		seq_putc(m, '\n');
	}
}
EXPORT_SYMBOL(seq_hex_dump);

struct list_head *seq_list_start(struct list_head *head, loff_t pos)
{
	struct list_head *lh;

	list_for_each(lh, head)
		if (pos-- == 0)
			return lh;

	return NULL;
}
EXPORT_SYMBOL(seq_list_start);

struct list_head *seq_list_start_head(struct list_head *head, loff_t pos)
{
	if (!pos)
		return head;

	return seq_list_start(head, pos - 1);
}
EXPORT_SYMBOL(seq_list_start_head);

struct list_head *seq_list_next(void *v, struct list_head *head, loff_t *ppos)
{
	struct list_head *lh;

	lh = ((struct list_head *)v)->next;
	++*ppos;
	return lh == head ? NULL : lh;
}
EXPORT_SYMBOL(seq_list_next);

/**
 * seq_hlist_start - start an iteration of a hlist
 * @head: the head of the hlist
 * @pos:  the start position of the sequence
 *
 * Called at seq_file->op->start().
 */
struct hlist_node *seq_hlist_start(struct hlist_head *head, loff_t pos)
{
	struct hlist_node *node;

	hlist_for_each(node, head)
		if (pos-- == 0)
			return node;
	return NULL;
}
EXPORT_SYMBOL(seq_hlist_start);

/**
 * seq_hlist_start_head - start an iteration of a hlist
 * @head: the head of the hlist
 * @pos:  the start position of the sequence
 *
 * Called at seq_file->op->start(). Call this function if you want to
 * print a header at the top of the output.
 */
struct hlist_node *seq_hlist_start_head(struct hlist_head *head, loff_t pos)
{
	if (!pos)
		return SEQ_START_TOKEN;

	return seq_hlist_start(head, pos - 1);
}
EXPORT_SYMBOL(seq_hlist_start_head);

/**
 * seq_hlist_next - move to the next position of the hlist
 * @v:    the current iterator
 * @head: the head of the hlist
 * @ppos: the current position
 *
 * Called at seq_file->op->next().
 */
struct hlist_node *seq_hlist_next(void *v, struct hlist_head *head,
				  loff_t *ppos)
{
	struct hlist_node *node = v;

	++*ppos;
	if (v == SEQ_START_TOKEN)
		return head->first;
	else
		return node->next;
}
EXPORT_SYMBOL(seq_hlist_next);

/**
 * seq_hlist_start_rcu - start an iteration of a hlist protected by RCU
 * @head: the head of the hlist
 * @pos:  the start position of the sequence
 *
 * Called at seq_file->op->start().
 *
 * This list-traversal primitive may safely run concurrently with
 * the _rcu list-mutation primitives such as hlist_add_head_rcu()
 * as long as the traversal is guarded by rcu_read_lock().
 */
struct hlist_node *seq_hlist_start_rcu(struct hlist_head *head,
				       loff_t pos)
{
	struct hlist_node *node;

	__hlist_for_each_rcu(node, head)
		if (pos-- == 0)
			return node;
	return NULL;
}
EXPORT_SYMBOL(seq_hlist_start_rcu);

/**
 * seq_hlist_start_head_rcu - start an iteration of a hlist protected by RCU
 * @head: the head of the hlist
 * @pos:  the start position of the sequence
 *
 * Called at seq_file->op->start(). Call this function if you want to
 * print a header at the top of the output.
 *
 * This list-traversal primitive may safely run concurrently with
 * the _rcu list-mutation primitives such as hlist_add_head_rcu()
 * as long as the traversal is guarded by rcu_read_lock().
 */
struct hlist_node *seq_hlist_start_head_rcu(struct hlist_head *head,
					    loff_t pos)
{
	if (!pos)
		return SEQ_START_TOKEN;

	return seq_hlist_start_rcu(head, pos - 1);
}
EXPORT_SYMBOL(seq_hlist_start_head_rcu);

/**
 * seq_hlist_next_rcu - move to the next position of the hlist protected by RCU
 * @v:    the current iterator
 * @head: the head of the hlist
 * @ppos: the current position
 *
 * Called at seq_file->op->next().
 *
 * This list-traversal primitive may safely run concurrently with
 * the _rcu list-mutation primitives such as hlist_add_head_rcu()
 * as long as the traversal is guarded by rcu_read_lock().
 */
struct hlist_node *seq_hlist_next_rcu(void *v,
				      struct hlist_head *head,
				      loff_t *ppos)
{
	struct hlist_node *node = v;

	++*ppos;
	if (v == SEQ_START_TOKEN)
		return rcu_dereference(head->first);
	else
		return rcu_dereference(node->next);
}
EXPORT_SYMBOL(seq_hlist_next_rcu);

/**
 * seq_hlist_start_precpu - start an iteration of a percpu hlist array
 * @head: pointer to percpu array of struct hlist_heads
 * @cpu:  pointer to cpu "cursor"
 * @pos:  start position of sequence
 *
 * Called at seq_file->op->start().
 */
struct hlist_node *
seq_hlist_start_percpu(struct hlist_head __percpu *head, int *cpu, loff_t pos)
{
	struct hlist_node *node;

	for_each_possible_cpu(*cpu) {
		hlist_for_each(node, per_cpu_ptr(head, *cpu)) {
			if (pos-- == 0)
				return node;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(seq_hlist_start_percpu);

/**
 * seq_hlist_next_percpu - move to the next position of the percpu hlist array
 * @v:    pointer to current hlist_node
 * @head: pointer to percpu array of struct hlist_heads
 * @cpu:  pointer to cpu "cursor"
 * @pos:  start position of sequence
 *
 * Called at seq_file->op->next().
 */
struct hlist_node *
seq_hlist_next_percpu(void *v, struct hlist_head __percpu *head,
			int *cpu, loff_t *pos)
{
	struct hlist_node *node = v;

	++*pos;

	if (node->next)
		return node->next;

	for (*cpu = cpumask_next(*cpu, cpu_possible_mask); *cpu < nr_cpu_ids;
	     *cpu = cpumask_next(*cpu, cpu_possible_mask)) {
		struct hlist_head *bucket = per_cpu_ptr(head, *cpu);

		if (!hlist_empty(bucket))
			return bucket->first;
	}
	return NULL;
}
EXPORT_SYMBOL(seq_hlist_next_percpu);

void __init seq_file_init(void)
{
	seq_file_cache = KMEM_CACHE(seq_file, SLAB_ACCOUNT|SLAB_PANIC);
}
