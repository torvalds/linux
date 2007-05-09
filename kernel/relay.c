/*
 * Public API and common code for kernel->userspace relay file support.
 *
 * See Documentation/filesystems/relayfs.txt for an overview of relayfs.
 *
 * Copyright (C) 2002-2005 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 1999-2005 - Karim Yaghmour (karim@opersys.com)
 *
 * Moved to kernel/relay.c by Paul Mundt, 2006.
 * November 2006 - CPU hotplug support by Mathieu Desnoyers
 * 	(mathieu.desnoyers@polymtl.ca)
 *
 * This file is released under the GPL.
 */
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/relay.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/cpu.h>

/* list of open channels, for cpu hotplug */
static DEFINE_MUTEX(relay_channels_mutex);
static LIST_HEAD(relay_channels);

/*
 * close() vm_op implementation for relay file mapping.
 */
static void relay_file_mmap_close(struct vm_area_struct *vma)
{
	struct rchan_buf *buf = vma->vm_private_data;
	buf->chan->cb->buf_unmapped(buf, vma->vm_file);
}

/*
 * nopage() vm_op implementation for relay file mapping.
 */
static struct page *relay_buf_nopage(struct vm_area_struct *vma,
				     unsigned long address,
				     int *type)
{
	struct page *page;
	struct rchan_buf *buf = vma->vm_private_data;
	unsigned long offset = address - vma->vm_start;

	if (address > vma->vm_end)
		return NOPAGE_SIGBUS; /* Disallow mremap */
	if (!buf)
		return NOPAGE_OOM;

	page = vmalloc_to_page(buf->start + offset);
	if (!page)
		return NOPAGE_OOM;
	get_page(page);

	if (type)
		*type = VM_FAULT_MINOR;

	return page;
}

/*
 * vm_ops for relay file mappings.
 */
static struct vm_operations_struct relay_file_mmap_ops = {
	.nopage = relay_buf_nopage,
	.close = relay_file_mmap_close,
};

/**
 *	relay_mmap_buf: - mmap channel buffer to process address space
 *	@buf: relay channel buffer
 *	@vma: vm_area_struct describing memory to be mapped
 *
 *	Returns 0 if ok, negative on error
 *
 *	Caller should already have grabbed mmap_sem.
 */
int relay_mmap_buf(struct rchan_buf *buf, struct vm_area_struct *vma)
{
	unsigned long length = vma->vm_end - vma->vm_start;
	struct file *filp = vma->vm_file;

	if (!buf)
		return -EBADF;

	if (length != (unsigned long)buf->chan->alloc_size)
		return -EINVAL;

	vma->vm_ops = &relay_file_mmap_ops;
	vma->vm_private_data = buf;
	buf->chan->cb->buf_mapped(buf, filp);

	return 0;
}

/**
 *	relay_alloc_buf - allocate a channel buffer
 *	@buf: the buffer struct
 *	@size: total size of the buffer
 *
 *	Returns a pointer to the resulting buffer, %NULL if unsuccessful. The
 *	passed in size will get page aligned, if it isn't already.
 */
static void *relay_alloc_buf(struct rchan_buf *buf, size_t *size)
{
	void *mem;
	unsigned int i, j, n_pages;

	*size = PAGE_ALIGN(*size);
	n_pages = *size >> PAGE_SHIFT;

	buf->page_array = kcalloc(n_pages, sizeof(struct page *), GFP_KERNEL);
	if (!buf->page_array)
		return NULL;

	for (i = 0; i < n_pages; i++) {
		buf->page_array[i] = alloc_page(GFP_KERNEL);
		if (unlikely(!buf->page_array[i]))
			goto depopulate;
	}
	mem = vmap(buf->page_array, n_pages, VM_MAP, PAGE_KERNEL);
	if (!mem)
		goto depopulate;

	memset(mem, 0, *size);
	buf->page_count = n_pages;
	return mem;

depopulate:
	for (j = 0; j < i; j++)
		__free_page(buf->page_array[j]);
	kfree(buf->page_array);
	return NULL;
}

/**
 *	relay_create_buf - allocate and initialize a channel buffer
 *	@chan: the relay channel
 *
 *	Returns channel buffer if successful, %NULL otherwise.
 */
struct rchan_buf *relay_create_buf(struct rchan *chan)
{
	struct rchan_buf *buf = kzalloc(sizeof(struct rchan_buf), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->padding = kmalloc(chan->n_subbufs * sizeof(size_t *), GFP_KERNEL);
	if (!buf->padding)
		goto free_buf;

	buf->start = relay_alloc_buf(buf, &chan->alloc_size);
	if (!buf->start)
		goto free_buf;

	buf->chan = chan;
	kref_get(&buf->chan->kref);
	return buf;

free_buf:
	kfree(buf->padding);
	kfree(buf);
	return NULL;
}

/**
 *	relay_destroy_channel - free the channel struct
 *	@kref: target kernel reference that contains the relay channel
 *
 *	Should only be called from kref_put().
 */
void relay_destroy_channel(struct kref *kref)
{
	struct rchan *chan = container_of(kref, struct rchan, kref);
	kfree(chan);
}

/**
 *	relay_destroy_buf - destroy an rchan_buf struct and associated buffer
 *	@buf: the buffer struct
 */
void relay_destroy_buf(struct rchan_buf *buf)
{
	struct rchan *chan = buf->chan;
	unsigned int i;

	if (likely(buf->start)) {
		vunmap(buf->start);
		for (i = 0; i < buf->page_count; i++)
			__free_page(buf->page_array[i]);
		kfree(buf->page_array);
	}
	chan->buf[buf->cpu] = NULL;
	kfree(buf->padding);
	kfree(buf);
	kref_put(&chan->kref, relay_destroy_channel);
}

/**
 *	relay_remove_buf - remove a channel buffer
 *	@kref: target kernel reference that contains the relay buffer
 *
 *	Removes the file from the fileystem, which also frees the
 *	rchan_buf_struct and the channel buffer.  Should only be called from
 *	kref_put().
 */
void relay_remove_buf(struct kref *kref)
{
	struct rchan_buf *buf = container_of(kref, struct rchan_buf, kref);
	buf->chan->cb->remove_buf_file(buf->dentry);
	relay_destroy_buf(buf);
}

/**
 *	relay_buf_empty - boolean, is the channel buffer empty?
 *	@buf: channel buffer
 *
 *	Returns 1 if the buffer is empty, 0 otherwise.
 */
int relay_buf_empty(struct rchan_buf *buf)
{
	return (buf->subbufs_produced - buf->subbufs_consumed) ? 0 : 1;
}
EXPORT_SYMBOL_GPL(relay_buf_empty);

/**
 *	relay_buf_full - boolean, is the channel buffer full?
 *	@buf: channel buffer
 *
 *	Returns 1 if the buffer is full, 0 otherwise.
 */
int relay_buf_full(struct rchan_buf *buf)
{
	size_t ready = buf->subbufs_produced - buf->subbufs_consumed;
	return (ready >= buf->chan->n_subbufs) ? 1 : 0;
}
EXPORT_SYMBOL_GPL(relay_buf_full);

/*
 * High-level relay kernel API and associated functions.
 */

/*
 * rchan_callback implementations defining default channel behavior.  Used
 * in place of corresponding NULL values in client callback struct.
 */

/*
 * subbuf_start() default callback.  Does nothing.
 */
static int subbuf_start_default_callback (struct rchan_buf *buf,
					  void *subbuf,
					  void *prev_subbuf,
					  size_t prev_padding)
{
	if (relay_buf_full(buf))
		return 0;

	return 1;
}

/*
 * buf_mapped() default callback.  Does nothing.
 */
static void buf_mapped_default_callback(struct rchan_buf *buf,
					struct file *filp)
{
}

/*
 * buf_unmapped() default callback.  Does nothing.
 */
static void buf_unmapped_default_callback(struct rchan_buf *buf,
					  struct file *filp)
{
}

/*
 * create_buf_file_create() default callback.  Does nothing.
 */
static struct dentry *create_buf_file_default_callback(const char *filename,
						       struct dentry *parent,
						       int mode,
						       struct rchan_buf *buf,
						       int *is_global)
{
	return NULL;
}

/*
 * remove_buf_file() default callback.  Does nothing.
 */
static int remove_buf_file_default_callback(struct dentry *dentry)
{
	return -EINVAL;
}

/* relay channel default callbacks */
static struct rchan_callbacks default_channel_callbacks = {
	.subbuf_start = subbuf_start_default_callback,
	.buf_mapped = buf_mapped_default_callback,
	.buf_unmapped = buf_unmapped_default_callback,
	.create_buf_file = create_buf_file_default_callback,
	.remove_buf_file = remove_buf_file_default_callback,
};

/**
 *	wakeup_readers - wake up readers waiting on a channel
 *	@data: contains the channel buffer
 *
 *	This is the timer function used to defer reader waking.
 */
static void wakeup_readers(unsigned long data)
{
	struct rchan_buf *buf = (struct rchan_buf *)data;
	wake_up_interruptible(&buf->read_wait);
}

/**
 *	__relay_reset - reset a channel buffer
 *	@buf: the channel buffer
 *	@init: 1 if this is a first-time initialization
 *
 *	See relay_reset() for description of effect.
 */
static void __relay_reset(struct rchan_buf *buf, unsigned int init)
{
	size_t i;

	if (init) {
		init_waitqueue_head(&buf->read_wait);
		kref_init(&buf->kref);
		setup_timer(&buf->timer, wakeup_readers, (unsigned long)buf);
	} else
		del_timer_sync(&buf->timer);

	buf->subbufs_produced = 0;
	buf->subbufs_consumed = 0;
	buf->bytes_consumed = 0;
	buf->finalized = 0;
	buf->data = buf->start;
	buf->offset = 0;

	for (i = 0; i < buf->chan->n_subbufs; i++)
		buf->padding[i] = 0;

	buf->chan->cb->subbuf_start(buf, buf->data, NULL, 0);
}

/**
 *	relay_reset - reset the channel
 *	@chan: the channel
 *
 *	This has the effect of erasing all data from all channel buffers
 *	and restarting the channel in its initial state.  The buffers
 *	are not freed, so any mappings are still in effect.
 *
 *	NOTE. Care should be taken that the channel isn't actually
 *	being used by anything when this call is made.
 */
void relay_reset(struct rchan *chan)
{
	unsigned int i;

	if (!chan)
		return;

 	if (chan->is_global && chan->buf[0]) {
		__relay_reset(chan->buf[0], 0);
		return;
	}

	mutex_lock(&relay_channels_mutex);
	for_each_online_cpu(i)
		if (chan->buf[i])
			__relay_reset(chan->buf[i], 0);
	mutex_unlock(&relay_channels_mutex);
}
EXPORT_SYMBOL_GPL(relay_reset);

/*
 *	relay_open_buf - create a new relay channel buffer
 *
 *	used by relay_open() and CPU hotplug.
 */
static struct rchan_buf *relay_open_buf(struct rchan *chan, unsigned int cpu)
{
 	struct rchan_buf *buf = NULL;
	struct dentry *dentry;
 	char *tmpname;

 	if (chan->is_global)
		return chan->buf[0];

	tmpname = kzalloc(NAME_MAX + 1, GFP_KERNEL);
 	if (!tmpname)
 		goto end;
 	snprintf(tmpname, NAME_MAX, "%s%d", chan->base_filename, cpu);

	buf = relay_create_buf(chan);
	if (!buf)
 		goto free_name;

 	buf->cpu = cpu;
 	__relay_reset(buf, 1);

	/* Create file in fs */
 	dentry = chan->cb->create_buf_file(tmpname, chan->parent, S_IRUSR,
 					   buf, &chan->is_global);
 	if (!dentry)
 		goto free_buf;

	buf->dentry = dentry;

 	if(chan->is_global) {
 		chan->buf[0] = buf;
 		buf->cpu = 0;
  	}

 	goto free_name;

free_buf:
 	relay_destroy_buf(buf);
free_name:
 	kfree(tmpname);
end:
	return buf;
}

/**
 *	relay_close_buf - close a channel buffer
 *	@buf: channel buffer
 *
 *	Marks the buffer finalized and restores the default callbacks.
 *	The channel buffer and channel buffer data structure are then freed
 *	automatically when the last reference is given up.
 */
static void relay_close_buf(struct rchan_buf *buf)
{
	buf->finalized = 1;
	del_timer_sync(&buf->timer);
	kref_put(&buf->kref, relay_remove_buf);
}

static void setup_callbacks(struct rchan *chan,
				   struct rchan_callbacks *cb)
{
	if (!cb) {
		chan->cb = &default_channel_callbacks;
		return;
	}

	if (!cb->subbuf_start)
		cb->subbuf_start = subbuf_start_default_callback;
	if (!cb->buf_mapped)
		cb->buf_mapped = buf_mapped_default_callback;
	if (!cb->buf_unmapped)
		cb->buf_unmapped = buf_unmapped_default_callback;
	if (!cb->create_buf_file)
		cb->create_buf_file = create_buf_file_default_callback;
	if (!cb->remove_buf_file)
		cb->remove_buf_file = remove_buf_file_default_callback;
	chan->cb = cb;
}

/**
 * 	relay_hotcpu_callback - CPU hotplug callback
 * 	@nb: notifier block
 * 	@action: hotplug action to take
 * 	@hcpu: CPU number
 *
 * 	Returns the success/failure of the operation. (%NOTIFY_OK, %NOTIFY_BAD)
 */
static int __cpuinit relay_hotcpu_callback(struct notifier_block *nb,
				unsigned long action,
				void *hcpu)
{
	unsigned int hotcpu = (unsigned long)hcpu;
	struct rchan *chan;

	switch(action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		mutex_lock(&relay_channels_mutex);
		list_for_each_entry(chan, &relay_channels, list) {
			if (chan->buf[hotcpu])
				continue;
			chan->buf[hotcpu] = relay_open_buf(chan, hotcpu);
			if(!chan->buf[hotcpu]) {
				printk(KERN_ERR
					"relay_hotcpu_callback: cpu %d buffer "
					"creation failed\n", hotcpu);
				mutex_unlock(&relay_channels_mutex);
				return NOTIFY_BAD;
			}
		}
		mutex_unlock(&relay_channels_mutex);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		/* No need to flush the cpu : will be flushed upon
		 * final relay_flush() call. */
		break;
	}
	return NOTIFY_OK;
}

/**
 *	relay_open - create a new relay channel
 *	@base_filename: base name of files to create
 *	@parent: dentry of parent directory, %NULL for root directory
 *	@subbuf_size: size of sub-buffers
 *	@n_subbufs: number of sub-buffers
 *	@cb: client callback functions
 *	@private_data: user-defined data
 *
 *	Returns channel pointer if successful, %NULL otherwise.
 *
 *	Creates a channel buffer for each cpu using the sizes and
 *	attributes specified.  The created channel buffer files
 *	will be named base_filename0...base_filenameN-1.  File
 *	permissions will be %S_IRUSR.
 */
struct rchan *relay_open(const char *base_filename,
			 struct dentry *parent,
			 size_t subbuf_size,
			 size_t n_subbufs,
			 struct rchan_callbacks *cb,
			 void *private_data)
{
	unsigned int i;
	struct rchan *chan;
	if (!base_filename)
		return NULL;

	if (!(subbuf_size && n_subbufs))
		return NULL;

	chan = kzalloc(sizeof(struct rchan), GFP_KERNEL);
	if (!chan)
		return NULL;

	chan->version = RELAYFS_CHANNEL_VERSION;
	chan->n_subbufs = n_subbufs;
	chan->subbuf_size = subbuf_size;
	chan->alloc_size = FIX_SIZE(subbuf_size * n_subbufs);
	chan->parent = parent;
	chan->private_data = private_data;
	strlcpy(chan->base_filename, base_filename, NAME_MAX);
	setup_callbacks(chan, cb);
	kref_init(&chan->kref);

	mutex_lock(&relay_channels_mutex);
	for_each_online_cpu(i) {
		chan->buf[i] = relay_open_buf(chan, i);
		if (!chan->buf[i])
			goto free_bufs;
	}
	list_add(&chan->list, &relay_channels);
	mutex_unlock(&relay_channels_mutex);

	return chan;

free_bufs:
	for_each_online_cpu(i) {
		if (!chan->buf[i])
			break;
		relay_close_buf(chan->buf[i]);
	}

	kref_put(&chan->kref, relay_destroy_channel);
	mutex_unlock(&relay_channels_mutex);
	return NULL;
}
EXPORT_SYMBOL_GPL(relay_open);

/**
 *	relay_switch_subbuf - switch to a new sub-buffer
 *	@buf: channel buffer
 *	@length: size of current event
 *
 *	Returns either the length passed in or 0 if full.
 *
 *	Performs sub-buffer-switch tasks such as invoking callbacks,
 *	updating padding counts, waking up readers, etc.
 */
size_t relay_switch_subbuf(struct rchan_buf *buf, size_t length)
{
	void *old, *new;
	size_t old_subbuf, new_subbuf;

	if (unlikely(length > buf->chan->subbuf_size))
		goto toobig;

	if (buf->offset != buf->chan->subbuf_size + 1) {
		buf->prev_padding = buf->chan->subbuf_size - buf->offset;
		old_subbuf = buf->subbufs_produced % buf->chan->n_subbufs;
		buf->padding[old_subbuf] = buf->prev_padding;
		buf->subbufs_produced++;
		buf->dentry->d_inode->i_size += buf->chan->subbuf_size -
			buf->padding[old_subbuf];
		smp_mb();
		if (waitqueue_active(&buf->read_wait))
			/*
			 * Calling wake_up_interruptible() from here
			 * will deadlock if we happen to be logging
			 * from the scheduler (trying to re-grab
			 * rq->lock), so defer it.
			 */
			__mod_timer(&buf->timer, jiffies + 1);
	}

	old = buf->data;
	new_subbuf = buf->subbufs_produced % buf->chan->n_subbufs;
	new = buf->start + new_subbuf * buf->chan->subbuf_size;
	buf->offset = 0;
	if (!buf->chan->cb->subbuf_start(buf, new, old, buf->prev_padding)) {
		buf->offset = buf->chan->subbuf_size + 1;
		return 0;
	}
	buf->data = new;
	buf->padding[new_subbuf] = 0;

	if (unlikely(length + buf->offset > buf->chan->subbuf_size))
		goto toobig;

	return length;

toobig:
	buf->chan->last_toobig = length;
	return 0;
}
EXPORT_SYMBOL_GPL(relay_switch_subbuf);

/**
 *	relay_subbufs_consumed - update the buffer's sub-buffers-consumed count
 *	@chan: the channel
 *	@cpu: the cpu associated with the channel buffer to update
 *	@subbufs_consumed: number of sub-buffers to add to current buf's count
 *
 *	Adds to the channel buffer's consumed sub-buffer count.
 *	subbufs_consumed should be the number of sub-buffers newly consumed,
 *	not the total consumed.
 *
 *	NOTE. Kernel clients don't need to call this function if the channel
 *	mode is 'overwrite'.
 */
void relay_subbufs_consumed(struct rchan *chan,
			    unsigned int cpu,
			    size_t subbufs_consumed)
{
	struct rchan_buf *buf;

	if (!chan)
		return;

	if (cpu >= NR_CPUS || !chan->buf[cpu])
		return;

	buf = chan->buf[cpu];
	buf->subbufs_consumed += subbufs_consumed;
	if (buf->subbufs_consumed > buf->subbufs_produced)
		buf->subbufs_consumed = buf->subbufs_produced;
}
EXPORT_SYMBOL_GPL(relay_subbufs_consumed);

/**
 *	relay_close - close the channel
 *	@chan: the channel
 *
 *	Closes all channel buffers and frees the channel.
 */
void relay_close(struct rchan *chan)
{
	unsigned int i;

	if (!chan)
		return;

	mutex_lock(&relay_channels_mutex);
	if (chan->is_global && chan->buf[0])
		relay_close_buf(chan->buf[0]);
	else
		for_each_possible_cpu(i)
			if (chan->buf[i])
				relay_close_buf(chan->buf[i]);

	if (chan->last_toobig)
		printk(KERN_WARNING "relay: one or more items not logged "
		       "[item size (%Zd) > sub-buffer size (%Zd)]\n",
		       chan->last_toobig, chan->subbuf_size);

	list_del(&chan->list);
	kref_put(&chan->kref, relay_destroy_channel);
	mutex_unlock(&relay_channels_mutex);
}
EXPORT_SYMBOL_GPL(relay_close);

/**
 *	relay_flush - close the channel
 *	@chan: the channel
 *
 *	Flushes all channel buffers, i.e. forces buffer switch.
 */
void relay_flush(struct rchan *chan)
{
	unsigned int i;

	if (!chan)
		return;

	if (chan->is_global && chan->buf[0]) {
		relay_switch_subbuf(chan->buf[0], 0);
		return;
	}

	mutex_lock(&relay_channels_mutex);
	for_each_possible_cpu(i)
		if (chan->buf[i])
			relay_switch_subbuf(chan->buf[i], 0);
	mutex_unlock(&relay_channels_mutex);
}
EXPORT_SYMBOL_GPL(relay_flush);

/**
 *	relay_file_open - open file op for relay files
 *	@inode: the inode
 *	@filp: the file
 *
 *	Increments the channel buffer refcount.
 */
static int relay_file_open(struct inode *inode, struct file *filp)
{
	struct rchan_buf *buf = inode->i_private;
	kref_get(&buf->kref);
	filp->private_data = buf;

	return 0;
}

/**
 *	relay_file_mmap - mmap file op for relay files
 *	@filp: the file
 *	@vma: the vma describing what to map
 *
 *	Calls upon relay_mmap_buf() to map the file into user space.
 */
static int relay_file_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct rchan_buf *buf = filp->private_data;
	return relay_mmap_buf(buf, vma);
}

/**
 *	relay_file_poll - poll file op for relay files
 *	@filp: the file
 *	@wait: poll table
 *
 *	Poll implemention.
 */
static unsigned int relay_file_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	struct rchan_buf *buf = filp->private_data;

	if (buf->finalized)
		return POLLERR;

	if (filp->f_mode & FMODE_READ) {
		poll_wait(filp, &buf->read_wait, wait);
		if (!relay_buf_empty(buf))
			mask |= POLLIN | POLLRDNORM;
	}

	return mask;
}

/**
 *	relay_file_release - release file op for relay files
 *	@inode: the inode
 *	@filp: the file
 *
 *	Decrements the channel refcount, as the filesystem is
 *	no longer using it.
 */
static int relay_file_release(struct inode *inode, struct file *filp)
{
	struct rchan_buf *buf = filp->private_data;
	kref_put(&buf->kref, relay_remove_buf);

	return 0;
}

/*
 *	relay_file_read_consume - update the consumed count for the buffer
 */
static void relay_file_read_consume(struct rchan_buf *buf,
				    size_t read_pos,
				    size_t bytes_consumed)
{
	size_t subbuf_size = buf->chan->subbuf_size;
	size_t n_subbufs = buf->chan->n_subbufs;
	size_t read_subbuf;

	if (buf->bytes_consumed + bytes_consumed > subbuf_size) {
		relay_subbufs_consumed(buf->chan, buf->cpu, 1);
		buf->bytes_consumed = 0;
	}

	buf->bytes_consumed += bytes_consumed;
	read_subbuf = read_pos / buf->chan->subbuf_size;
	if (buf->bytes_consumed + buf->padding[read_subbuf] == subbuf_size) {
		if ((read_subbuf == buf->subbufs_produced % n_subbufs) &&
		    (buf->offset == subbuf_size))
			return;
		relay_subbufs_consumed(buf->chan, buf->cpu, 1);
		buf->bytes_consumed = 0;
	}
}

/*
 *	relay_file_read_avail - boolean, are there unconsumed bytes available?
 */
static int relay_file_read_avail(struct rchan_buf *buf, size_t read_pos)
{
	size_t subbuf_size = buf->chan->subbuf_size;
	size_t n_subbufs = buf->chan->n_subbufs;
	size_t produced = buf->subbufs_produced;
	size_t consumed = buf->subbufs_consumed;

	relay_file_read_consume(buf, read_pos, 0);

	if (unlikely(buf->offset > subbuf_size)) {
		if (produced == consumed)
			return 0;
		return 1;
	}

	if (unlikely(produced - consumed >= n_subbufs)) {
		consumed = (produced / n_subbufs) * n_subbufs;
		buf->subbufs_consumed = consumed;
	}
	
	produced = (produced % n_subbufs) * subbuf_size + buf->offset;
	consumed = (consumed % n_subbufs) * subbuf_size + buf->bytes_consumed;

	if (consumed > produced)
		produced += n_subbufs * subbuf_size;
	
	if (consumed == produced)
		return 0;

	return 1;
}

/**
 *	relay_file_read_subbuf_avail - return bytes available in sub-buffer
 *	@read_pos: file read position
 *	@buf: relay channel buffer
 */
static size_t relay_file_read_subbuf_avail(size_t read_pos,
					   struct rchan_buf *buf)
{
	size_t padding, avail = 0;
	size_t read_subbuf, read_offset, write_subbuf, write_offset;
	size_t subbuf_size = buf->chan->subbuf_size;

	write_subbuf = (buf->data - buf->start) / subbuf_size;
	write_offset = buf->offset > subbuf_size ? subbuf_size : buf->offset;
	read_subbuf = read_pos / subbuf_size;
	read_offset = read_pos % subbuf_size;
	padding = buf->padding[read_subbuf];

	if (read_subbuf == write_subbuf) {
		if (read_offset + padding < write_offset)
			avail = write_offset - (read_offset + padding);
	} else
		avail = (subbuf_size - padding) - read_offset;

	return avail;
}

/**
 *	relay_file_read_start_pos - find the first available byte to read
 *	@read_pos: file read position
 *	@buf: relay channel buffer
 *
 *	If the @read_pos is in the middle of padding, return the
 *	position of the first actually available byte, otherwise
 *	return the original value.
 */
static size_t relay_file_read_start_pos(size_t read_pos,
					struct rchan_buf *buf)
{
	size_t read_subbuf, padding, padding_start, padding_end;
	size_t subbuf_size = buf->chan->subbuf_size;
	size_t n_subbufs = buf->chan->n_subbufs;

	read_subbuf = read_pos / subbuf_size;
	padding = buf->padding[read_subbuf];
	padding_start = (read_subbuf + 1) * subbuf_size - padding;
	padding_end = (read_subbuf + 1) * subbuf_size;
	if (read_pos >= padding_start && read_pos < padding_end) {
		read_subbuf = (read_subbuf + 1) % n_subbufs;
		read_pos = read_subbuf * subbuf_size;
	}

	return read_pos;
}

/**
 *	relay_file_read_end_pos - return the new read position
 *	@read_pos: file read position
 *	@buf: relay channel buffer
 *	@count: number of bytes to be read
 */
static size_t relay_file_read_end_pos(struct rchan_buf *buf,
				      size_t read_pos,
				      size_t count)
{
	size_t read_subbuf, padding, end_pos;
	size_t subbuf_size = buf->chan->subbuf_size;
	size_t n_subbufs = buf->chan->n_subbufs;

	read_subbuf = read_pos / subbuf_size;
	padding = buf->padding[read_subbuf];
	if (read_pos % subbuf_size + count + padding == subbuf_size)
		end_pos = (read_subbuf + 1) * subbuf_size;
	else
		end_pos = read_pos + count;
	if (end_pos >= subbuf_size * n_subbufs)
		end_pos = 0;

	return end_pos;
}

/*
 *	subbuf_read_actor - read up to one subbuf's worth of data
 */
static int subbuf_read_actor(size_t read_start,
			     struct rchan_buf *buf,
			     size_t avail,
			     read_descriptor_t *desc,
			     read_actor_t actor)
{
	void *from;
	int ret = 0;

	from = buf->start + read_start;
	ret = avail;
	if (copy_to_user(desc->arg.buf, from, avail)) {
		desc->error = -EFAULT;
		ret = 0;
	}
	desc->arg.data += ret;
	desc->written += ret;
	desc->count -= ret;

	return ret;
}

/*
 *	subbuf_send_actor - send up to one subbuf's worth of data
 */
static int subbuf_send_actor(size_t read_start,
			     struct rchan_buf *buf,
			     size_t avail,
			     read_descriptor_t *desc,
			     read_actor_t actor)
{
	unsigned long pidx, poff;
	unsigned int subbuf_pages;
	int ret = 0;

	subbuf_pages = buf->chan->alloc_size >> PAGE_SHIFT;
	pidx = (read_start / PAGE_SIZE) % subbuf_pages;
	poff = read_start & ~PAGE_MASK;
	while (avail) {
		struct page *p = buf->page_array[pidx];
		unsigned int len;

		len = PAGE_SIZE - poff;
		if (len > avail)
			len = avail;

		len = actor(desc, p, poff, len);
		if (desc->error)
			break;

		avail -= len;
		ret += len;
		poff = 0;
		pidx = (pidx + 1) % subbuf_pages;
	}

	return ret;
}

typedef int (*subbuf_actor_t) (size_t read_start,
			       struct rchan_buf *buf,
			       size_t avail,
			       read_descriptor_t *desc,
			       read_actor_t actor);

/*
 *	relay_file_read_subbufs - read count bytes, bridging subbuf boundaries
 */
static ssize_t relay_file_read_subbufs(struct file *filp, loff_t *ppos,
					subbuf_actor_t subbuf_actor,
					read_actor_t actor,
					read_descriptor_t *desc)
{
	struct rchan_buf *buf = filp->private_data;
	size_t read_start, avail;
	int ret;

	if (!desc->count)
		return 0;

	mutex_lock(&filp->f_path.dentry->d_inode->i_mutex);
	do {
		if (!relay_file_read_avail(buf, *ppos))
			break;

		read_start = relay_file_read_start_pos(*ppos, buf);
		avail = relay_file_read_subbuf_avail(read_start, buf);
		if (!avail)
			break;

		avail = min(desc->count, avail);
		ret = subbuf_actor(read_start, buf, avail, desc, actor);
		if (desc->error < 0)
			break;

		if (ret) {
			relay_file_read_consume(buf, read_start, ret);
			*ppos = relay_file_read_end_pos(buf, read_start, ret);
		}
	} while (desc->count && ret);
	mutex_unlock(&filp->f_path.dentry->d_inode->i_mutex);

	return desc->written;
}

static ssize_t relay_file_read(struct file *filp,
			       char __user *buffer,
			       size_t count,
			       loff_t *ppos)
{
	read_descriptor_t desc;
	desc.written = 0;
	desc.count = count;
	desc.arg.buf = buffer;
	desc.error = 0;
	return relay_file_read_subbufs(filp, ppos, subbuf_read_actor,
				       NULL, &desc);
}

static ssize_t relay_file_sendfile(struct file *filp,
				   loff_t *ppos,
				   size_t count,
				   read_actor_t actor,
				   void *target)
{
	read_descriptor_t desc;
	desc.written = 0;
	desc.count = count;
	desc.arg.data = target;
	desc.error = 0;
	return relay_file_read_subbufs(filp, ppos, subbuf_send_actor,
				       actor, &desc);
}

const struct file_operations relay_file_operations = {
	.open		= relay_file_open,
	.poll		= relay_file_poll,
	.mmap		= relay_file_mmap,
	.read		= relay_file_read,
	.llseek		= no_llseek,
	.release	= relay_file_release,
	.sendfile       = relay_file_sendfile,
};
EXPORT_SYMBOL_GPL(relay_file_operations);

static __init int relay_init(void)
{

	hotcpu_notifier(relay_hotcpu_callback, 0);
	return 0;
}

module_init(relay_init);
