/*
 * linux/fs/seq_file.c
 *
 * helper functions for making synthetic files from sequences of records.
 * initial implementation -- AV, Oct 2001.
 */

#include <linux/fs.h>
#include <linux/export.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/cred.h>

#include <asm/uaccess.h>
#include <asm/page.h>


/*
 * seq_files have a buffer which can may overflow. When this happens a larger
 * buffer is reallocated and all the data will be printed again.
 * The overflow state is true when m->count == m->size.
 */
static bool seq_overflow(struct seq_file *m)
{
	return m->count == m->size;
}

static void seq_set_overflow(struct seq_file *m)
{
	m->count = m->size;
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
 */
int seq_open(struct file *file, const struct seq_operations *op)
{
	struct seq_file *p = file->private_data;

	if (!p) {
		p = kmalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		file->private_data = p;
	}
	memset(p, 0, sizeof(*p));
	mutex_init(&p->lock);
	p->op = op;
#ifdef CONFIG_USER_NS
	p->user_ns = file->f_cred->user_ns;
#endif

	/*
	 * Wrappers around seq_open(e.g. swaps_open) need to be
	 * aware of this. If they set f_version themselves, they
	 * should call seq_open first and then set f_version.
	 */
	file->f_version = 0;

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
	loff_t pos = 0, index;
	int error = 0;
	void *p;

	m->version = 0;
	index = 0;
	m->count = m->from = 0;
	if (!offset) {
		m->index = index;
		return 0;
	}
	if (!m->buf) {
		m->buf = kmalloc(m->size = PAGE_SIZE, GFP_KERNEL);
		if (!m->buf)
			return -ENOMEM;
	}
	p = m->op->start(m, &index);
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
		if (seq_overflow(m))
			goto Eoverflow;
		if (pos + m->count > offset) {
			m->from = offset - pos;
			m->count -= m->from;
			m->index = index;
			break;
		}
		pos += m->count;
		m->count = 0;
		if (pos == offset) {
			index++;
			m->index = index;
			break;
		}
		p = m->op->next(m, p, &index);
	}
	m->op->stop(m, p);
	m->index = index;
	return error;

Eoverflow:
	m->op->stop(m, p);
	kfree(m->buf);
	m->buf = kmalloc(m->size <<= 1, GFP_KERNEL);
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
	loff_t pos;
	size_t n;
	void *p;
	int err = 0;

	mutex_lock(&m->lock);

	/*
	 * seq_file->op->..m_start/m_stop/m_next may do special actions
	 * or optimisations based on the file->f_version, so we want to
	 * pass the file->f_version to those methods.
	 *
	 * seq_file->version is just copy of f_version, and seq_file
	 * methods can treat it simply as file version.
	 * It is copied in first and copied out after all operations.
	 * It is convenient to have it as  part of structure to avoid the
	 * need of passing another argument to all the seq_file methods.
	 */
	m->version = file->f_version;

	/* Don't assume *ppos is where we left it */
	if (unlikely(*ppos != m->read_pos)) {
		while ((err = traverse(m, *ppos)) == -EAGAIN)
			;
		if (err) {
			/* With prejudice... */
			m->read_pos = 0;
			m->version = 0;
			m->index = 0;
			m->count = 0;
			goto Done;
		} else {
			m->read_pos = *ppos;
		}
	}

	/* grab buffer if we didn't have one */
	if (!m->buf) {
		m->buf = kmalloc(m->size = PAGE_SIZE, GFP_KERNEL);
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
		if (!m->count)
			m->index++;
		if (!size)
			goto Done;
	}
	/* we need at least one record in buffer */
	pos = m->index;
	p = m->op->start(m, &pos);
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
			p = m->op->next(m, p, &pos);
			m->index = pos;
			continue;
		}
		if (m->count < m->size)
			goto Fill;
		m->op->stop(m, p);
		kfree(m->buf);
		m->buf = kmalloc(m->size <<= 1, GFP_KERNEL);
		if (!m->buf)
			goto Enomem;
		m->count = 0;
		m->version = 0;
		pos = m->index;
		p = m->op->start(m, &pos);
	}
	m->op->stop(m, p);
	m->count = 0;
	goto Done;
Fill:
	/* they want more? let's try to get some more */
	while (m->count < size) {
		size_t offs = m->count;
		loff_t next = pos;
		p = m->op->next(m, p, &next);
		if (!p || IS_ERR(p)) {
			err = PTR_ERR(p);
			break;
		}
		err = m->op->show(m, p);
		if (seq_overflow(m) || err) {
			m->count = offs;
			if (likely(err <= 0))
				break;
		}
		pos = next;
	}
	m->op->stop(m, p);
	n = min(m->count, size);
	err = copy_to_user(buf, m->buf, n);
	if (err)
		goto Efault;
	copied += n;
	m->count -= n;
	if (m->count)
		m->from = n;
	else
		pos++;
	m->index = pos;
Done:
	if (!copied)
		copied = err;
	else {
		*ppos += copied;
		m->read_pos += copied;
	}
	file->f_version = m->version;
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
	m->version = file->f_version;
	switch (whence) {
		case 1:
			offset += file->f_pos;
		case 0:
			if (offset < 0)
				break;
			retval = offset;
			if (offset != m->read_pos) {
				while ((retval=traverse(m, offset)) == -EAGAIN)
					;
				if (retval) {
					/* with extreme prejudice... */
					file->f_pos = 0;
					m->read_pos = 0;
					m->version = 0;
					m->index = 0;
					m->count = 0;
				} else {
					m->read_pos = offset;
					retval = file->f_pos = offset;
				}
			}
	}
	file->f_version = m->version;
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
	kfree(m->buf);
	kfree(m);
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
 *	@esc with usual octal escape.  Returns 0 in case of success, -1 - in
 *	case of overflow.
 */
int seq_escape(struct seq_file *m, const char *s, const char *esc)
{
	char *end = m->buf + m->size;
        char *p;
	char c;

        for (p = m->buf + m->count; (c = *s) != '\0' && p < end; s++) {
		if (!strchr(esc, c)) {
			*p++ = c;
			continue;
		}
		if (p + 3 < end) {
			*p++ = '\\';
			*p++ = '0' + ((c & 0300) >> 6);
			*p++ = '0' + ((c & 070) >> 3);
			*p++ = '0' + (c & 07);
			continue;
		}
		seq_set_overflow(m);
		return -1;
        }
	m->count = p - m->buf;
        return 0;
}
EXPORT_SYMBOL(seq_escape);

int seq_vprintf(struct seq_file *m, const char *f, va_list args)
{
	int len;

	if (m->count < m->size) {
		len = vsnprintf(m->buf + m->count, m->size - m->count, f, args);
		if (m->count + len < m->size) {
			m->count += len;
			return 0;
		}
	}
	seq_set_overflow(m);
	return -1;
}
EXPORT_SYMBOL(seq_vprintf);

int seq_printf(struct seq_file *m, const char *f, ...)
{
	int ret;
	va_list args;

	va_start(args, f);
	ret = seq_vprintf(m, f, args);
	va_end(args);

	return ret;
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

int seq_bitmap(struct seq_file *m, const unsigned long *bits,
				   unsigned int nr_bits)
{
	if (m->count < m->size) {
		int len = bitmap_scnprintf(m->buf + m->count,
				m->size - m->count, bits, nr_bits);
		if (m->count + len < m->size) {
			m->count += len;
			return 0;
		}
	}
	seq_set_overflow(m);
	return -1;
}
EXPORT_SYMBOL(seq_bitmap);

int seq_bitmap_list(struct seq_file *m, const unsigned long *bits,
		unsigned int nr_bits)
{
	if (m->count < m->size) {
		int len = bitmap_scnlistprintf(m->buf + m->count,
				m->size - m->count, bits, nr_bits);
		if (m->count + len < m->size) {
			m->count += len;
			return 0;
		}
	}
	seq_set_overflow(m);
	return -1;
}
EXPORT_SYMBOL(seq_bitmap_list);

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
	struct seq_operations *op = kmalloc(sizeof(*op), GFP_KERNEL);
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

	private = kzalloc(psize, GFP_KERNEL);
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

int seq_putc(struct seq_file *m, char c)
{
	if (m->count < m->size) {
		m->buf[m->count++] = c;
		return 0;
	}
	return -1;
}
EXPORT_SYMBOL(seq_putc);

int seq_puts(struct seq_file *m, const char *s)
{
	int len = strlen(s);
	if (m->count + len < m->size) {
		memcpy(m->buf + m->count, s, len);
		m->count += len;
		return 0;
	}
	seq_set_overflow(m);
	return -1;
}
EXPORT_SYMBOL(seq_puts);

/*
 * A helper routine for putting decimal numbers without rich format of printf().
 * only 'unsigned long long' is supported.
 * This routine will put one byte delimiter + number into seq_file.
 * This routine is very quick when you show lots of numbers.
 * In usual cases, it will be better to use seq_printf(). It's easier to read.
 */
int seq_put_decimal_ull(struct seq_file *m, char delimiter,
			unsigned long long num)
{
	int len;

	if (m->count + 2 >= m->size) /* we'll write 2 bytes at least */
		goto overflow;

	if (delimiter)
		m->buf[m->count++] = delimiter;

	if (num < 10) {
		m->buf[m->count++] = num + '0';
		return 0;
	}

	len = num_to_str(m->buf + m->count, m->size - m->count, num);
	if (!len)
		goto overflow;
	m->count += len;
	return 0;
overflow:
	seq_set_overflow(m);
	return -1;
}
EXPORT_SYMBOL(seq_put_decimal_ull);

int seq_put_decimal_ll(struct seq_file *m, char delimiter,
			long long num)
{
	if (num < 0) {
		if (m->count + 3 >= m->size) {
			seq_set_overflow(m);
			return -1;
		}
		if (delimiter)
			m->buf[m->count++] = delimiter;
		num = -num;
		delimiter = '-';
	}
	return seq_put_decimal_ull(m, delimiter, num);

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
